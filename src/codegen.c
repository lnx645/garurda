// ============================================================
// Native code generator: x86-64 Assembly -> ELF (via as/ld)
// Author: GalangTeam
//
// Conventions:
//   - All locals live in the frame, addressed as NEGATIVE offsets from %rbp.
//   - Slots are handed out downwards (-8, -16, ...) and recycled on block exit.
//   - Expression results are returned in %rax.
//   - Binary comparisons use `cmpq right, left` so setCC reads left OP right.
// ============================================================

#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/wait.h>

// ============================================================
// Assembly emitter
// ============================================================
typedef struct {
    char* buffer;
    int length;
    int capacity;
    int string_count;
    char** strings;
    int strings_capacity;
} AsmBuffer;

typedef struct {
    char name[64];
    int offset;      // NEGATIVE byte offset from %rbp
    TypeTag tag;     // how the slot is stored: int/bool as 8 bytes, float as a double
} VarInfo;

typedef struct {
    AsmBuffer* buf;
    VarInfo* vars;
    int var_count;
    int var_capacity;
    int next_slot;
    int label_count;
} CodegenCtx;

// Upper bound on 8-byte slots per function. The frame is sized from this.
#define MAX_SLOTS 512

static AsmBuffer* asm_new(void) {
    AsmBuffer* b = calloc(1, sizeof(AsmBuffer));
    b->capacity = 8192;
    b->buffer = calloc(b->capacity, 1);
    b->strings_capacity = 64;
    b->strings = calloc(b->strings_capacity, sizeof(char*));
    return b;
}

static void asm_free(AsmBuffer* b) {
    for (int i = 0; i < b->string_count; i++) free(b->strings[i]);
    free(b->strings);
    free(b->buffer);
    free(b);
}

static void asm_emit(AsmBuffer* b, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    va_start(args, fmt);
    while (b->length + needed + 1 >= b->capacity) {
        b->capacity *= 2;
        b->buffer = realloc(b->buffer, b->capacity);
    }
    vsnprintf(b->buffer + b->length, needed + 1, fmt, args);
    b->length += needed;
    va_end(args);
}

static char* new_label(CodegenCtx* ctx) {
    char buf[32];
    snprintf(buf, sizeof(buf), ".L%d", ctx->label_count++);
    return strdup(buf);
}

// ============================================================
// String management
// ============================================================
static int add_string(AsmBuffer* b, const char* str) {
    for (int i = 0; i < b->string_count; i++) {
        if (strcmp(b->strings[i], str) == 0) return i;
    }
    if (b->string_count >= b->strings_capacity) {
        b->strings_capacity *= 2;
        b->strings = realloc(b->strings, b->strings_capacity * sizeof(char*));
    }
    b->strings[b->string_count] = strdup(str);
    return b->string_count++;
}

static void emit_escaped_string(AsmBuffer* b, const char* str) {
    asm_emit(b, "\"");
    for (const char* c = str; *c; c++) {
        switch (*c) {
            case '\\': asm_emit(b, "\\\\"); break;
            case '"':  asm_emit(b, "\\\""); break;
            case '\n': asm_emit(b, "\\n"); break;
            case '\t': asm_emit(b, "\\t"); break;
            case '\r': asm_emit(b, "\\r"); break;
            default:
                if (*c >= 32 && *c < 127) asm_emit(b, "%c", *c);
                else                        asm_emit(b, "\\x%02x", (unsigned char)*c);
        }
    }
    asm_emit(b, "\"");
}

// ============================================================
// Variable / slot management
// ============================================================
static int ctx_add_var(CodegenCtx* ctx, const char* name, int offset, TypeTag tag) {
    if (ctx->var_count >= ctx->var_capacity) {
        ctx->var_capacity = ctx->var_capacity == 0 ? 16 : ctx->var_capacity * 2;
        ctx->vars = realloc(ctx->vars, ctx->var_capacity * sizeof(VarInfo));
    }
    snprintf(ctx->vars[ctx->var_count].name,
             sizeof(ctx->vars[ctx->var_count].name), "%s", name);
    ctx->vars[ctx->var_count].offset = offset;
    ctx->vars[ctx->var_count].tag = tag;
    return ctx->var_count++;
}

// Returns the slot offset, or 0 when unknown (0 is never a valid slot,
// since every real slot is negative). *tag reports how the slot is stored.
static int ctx_find_var(CodegenCtx* ctx, const char* name, TypeTag* tag) {
    for (int i = ctx->var_count - 1; i >= 0; i--) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            if (tag) *tag = ctx->vars[i].tag;
            return ctx->vars[i].offset;
        }
    }
    if (tag) *tag = TY_UNKNOWN;
    return 0;
}

// Tag of a declaration's written type, e.g. `let x: float64` -> TY_FLOAT.
static TypeTag decl_tag(const ASTType* type) {
    if (!type || !type->name) return TY_UNKNOWN;
    if (strcmp(type->name, "int") == 0)     return TY_INT;
    if (strcmp(type->name, "float64") == 0) return TY_FLOAT;
    if (strcmp(type->name, "bool") == 0)    return TY_BOOL;
    if (strcmp(type->name, "string") == 0)  return TY_STRING;
    return TY_UNKNOWN;
}

// Store the value produced by emit_expr() into a slot, converting to the
// slot's representation first.
static void emit_store_slot(CodegenCtx* ctx, int offset, TypeTag slot_tag) {
    if (slot_tag == TY_FLOAT) {
        asm_emit(ctx->buf, "\tmovsd %%xmm0, %d(%%rbp)\n", offset);
    } else {
        asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", offset);
    }
}

// Load a slot into the result registers (%rax, or %xmm0 for a float).
static void emit_load_slot(CodegenCtx* ctx, int offset, TypeTag slot_tag) {
    if (slot_tag == TY_FLOAT) {
        asm_emit(ctx->buf, "\tmovsd %d(%%rbp), %%xmm0\n", offset);
    } else {
        asm_emit(ctx->buf, "\tmovq %d(%%rbp), %%rax\n", offset);
    }
}

static int ctx_new_slot(CodegenCtx* ctx) {
    if (ctx->next_slot <= -(MAX_SLOTS * 8)) return 0;   // frame exhausted
    return ctx->next_slot -= 8;
}

static int ctx_var_mark(CodegenCtx* ctx)  { return ctx->var_count; }
static int ctx_slot_mark(CodegenCtx* ctx) { return ctx->next_slot; }

static void ctx_rewind(CodegenCtx* ctx, int vm, int sm) {
    ctx->var_count = vm;
    ctx->next_slot = sm;
}

// ============================================================
// Runtime helpers
// ============================================================

// Write a NUL-terminated string to stdout.
static void emit_write_string(CodegenCtx* ctx, const char* value) {
    int idx = add_string(ctx->buf, value);
    asm_emit(ctx->buf,
             "\tmovq $1, %%rax\n"
             "\tmovq $1, %%rdi\n"
             "\tleaq str_%d(%%rip), %%rsi\n"
             "\tmovq $%d, %%rdx\n"
             "\tsyscall\n",
             idx, (int)strlen(value));
}

// Write a NUL-terminated C string whose address is in %rax.
static void emit_write_cstr(CodegenCtx* ctx) {
    char* l1 = new_label(ctx);
    char* l2 = new_label(ctx);
    asm_emit(ctx->buf,
             "\tmovq %%rax, %%rsi\n"
             "\txorq %%rcx, %%rcx\n"
             "%s:\n"
             "\tcmpb $0, (%%rsi,%%rcx,1)\n"
             "\tje %s\n"
             "\tincq %%rcx\n"
             "\tjmp %s\n"
             "%s:\n"
             "\tmovq %%rcx, %%rdx\n"
             "\tmovq $1, %%rax\n"
             "\tmovq $1, %%rdi\n"
             "\tsyscall\n",
             l1, l2, l1, l2);
    free(l1); free(l2);
}

// Convert the integer in %rax to decimal ASCII on stdout.
static void emit_write_int(CodegenCtx* ctx) {
    asm_emit(ctx->buf,
             "\tmovq %%rax, %%rdi\n"
             "\tcall .int_to_ascii\n"
             "\tmovq $1, %%rax\n"
             "\tmovq $1, %%rdi\n"
             "\tsyscall\n");
}

// Convert the double in %xmm0 to ASCII on stdout.
static void emit_write_float(CodegenCtx* ctx) {
    asm_emit(ctx->buf,
             "\tcall .float_to_ascii\n"
             "\tmovq $1, %%rax\n"
             "\tmovq $1, %%rdi\n"
             "\tsyscall\n");
}

// in:  %rdi = signed value
// out: %rsi = buffer start, %rdx = byte length
//
// Only %rbx is pushed: it is the sole callee-saved register this helper
// touches. %rdi/%rdx/%rcx are caller-saved under the SysV ABI, and %rdx is
// an OUTPUT of this helper -- restoring it would silently discard the length
// that was just computed.
static void emit_int_to_ascii_helper(AsmBuffer* b) {
    asm_emit(b,
        ".int_to_ascii:\n"
        "\tpushq %%rbx\n"
        // Write backwards from the end of the buffer.
        "\tleaq .int_buf+31, %%rbx\n"
        "\tmovb $0, (%%rbx)\n"
        "\ttestq %%rdi, %%rdi\n"
        "\tjns .ita_nonneg\n"
        "\tnegq %%rdi\n"
        "\tmovq $1, %%rcx\n"        // remember "negative"
        "\tjmp .ita_loop\n"
        ".ita_nonneg:\n"
        "\tmovq $0, %%rcx\n"
        ".ita_loop:\n"
        "\tmovq $0, %%rdx\n"        // high half must be 0 for the division
        "\tmovq %%rdi, %%rax\n"
        "\tmovq $10, %%r8\n"
        "\tdivq %%r8\n"             // rax = q, rdx = r
        "\taddq $48, %%rdx\n"
        "\tdecq %%rbx\n"
        "\tmovb %%dl, (%%rbx)\n"
        "\tmovq %%rax, %%rdi\n"
        "\ttestq %%rdi, %%rdi\n"
        "\tjnz .ita_loop\n"
        "\ttestq %%rcx, %%rcx\n"
        "\tjz .ita_done\n"
        "\tdecq %%rbx\n"
        "\tmovb $45, (%%rbx)\n"     // '-'
        ".ita_done:\n"
        // The NUL terminator sits at .int_buf+31, so that is the end pointer.
        "\tleaq .int_buf+31, %%rax\n"
        "\tsubq %%rbx, %%rax\n"     // length = end - start
        "\tmovq %%rbx, %%rsi\n"     // start = where the digits actually begin
        "\tmovq %%rax, %%rdx\n"     // length (last: %rdx is an output register)
        "\tpopq %%rbx\n"
        "\tret\n");
}

// Convert the IEEE-754 double in %xmm0 to decimal text.
//
// Prints the magnitude, then a fixed six fractional digits (no trimming),
// then a leading '-' when negative. Fixed precision keeps the digit loop
// trivially correct; it is not a shortest-round-trip conversion.
//
// in:  %xmm0 = value
// out: %rsi = buffer start, %rdx = byte length
// Convert the IEEE-754 double in %xmm0 to decimal text.
//
// Prints the magnitude, a '.', then exactly six fractional digits.
//
// The string is built FORWARD from .fbuf with an explicit cursor. An
// earlier version filled the buffer backwards from a fixed end offset,
// which makes the length depend on a hand-computed end pointer; getting
// that off by one silently truncates or overruns the result.
//
// in:  %xmm0 = value
// out: %rsi = buffer start, %rdx = byte length
static void emit_float_to_ascii_helper(AsmBuffer* b) {
    asm_emit(b,
        ".float_to_ascii:\n"
        "\tpushq %%rbx\n"
        "\tpushq %%rcx\n"
        "\tpushq %%rdi\n"
        "\tpushq %%r8\n"
        "\tpushq %%r9\n"
        "\tpushq %%r10\n"
        "\tpushq %%r11\n"
        "\tpushq %%r12\n"
        "\tpushq %%r13\n"
        "\n"
        "\tleaq .fbuf(%%rip), %%rbx\n"      // forward cursor\n"
        "\n"
        "\t// xmm1 = original, xmm0 = magnitude. The magnitude is an AND\n"
        "\t// with the inverted sign bit, NOT an xor: xor would flip the sign\n"
        "\t// of every value instead of clearing it, turning +1.5 into -1.5.\n"
        "\t// That single wrong bit then poisons cvttsd2siq (rdi becomes -1,\n"
        "\t// i.e. the largest unsigned value) and the power-of-ten loop below\n"
        "\t// spins forever, because an unsigned compare against the maximum\n"
        "\t// can never exit.\n"
        "\tmovapd %%xmm0, %%xmm1\n"
        "\tandpd .f_absmask(%%rip), %%xmm0\n"
        "\tcomisd .f_zero(%%rip), %%xmm1\n"
        "\tjb .fta_neg\n"
        "\tjmp .fta_abs\n"
        ".fta_neg:\n"
        "\tmovb $45, (%%rbx)\n"             // '-'\n"
        "\tincq %%rbx\n"
        ".fta_abs:\n"
        "\n"
        "\t// Integer magnitude and six-digit fractional part.\n"
        "\tcvttsd2siq %%xmm0, %%rdi\n"
        "\tcvtsi2sdq %%rdi, %%xmm2\n"
        "\tsubsd %%xmm2, %%xmm0\n"          // xmm0 = frac\n"
        "\tmovsd .f_1e6(%%rip), %%xmm3\n"
        "\tmulsd %%xmm3, %%xmm0\n"
        "\tmovsd .f_half(%%rip), %%xmm3\n"
        "\taddsd %%xmm3, %%xmm0\n"
        "\tcvttsd2siq %%xmm0, %%r10\n"      // r10 = frac6, 0..999999\n"
        "\n"
        "\t// Divisor = largest power of ten <= magnitude (at least 1).\n"
        "\tmovq $1, %%r12\n"
        ".fta_pw:\n"
        "\tmovq %%r12, %%rcx\n"
        "\timulq $10, %%rcx\n"
        "\tcmpq %%rcx, %%rdi\n"
        "\tjb .fta_pwdone\n"
        "\tmovq %%rcx, %%r12\n"
        "\tjmp .fta_pw\n"
        ".fta_pwdone:\n"
        "\n"
        "\t// Emit the integer digits, most significant first. The dividend\n"
        "\t// is reloaded from rdi every round: after the first division rax\n"
        "\t// holds the old quotient, not the remaining value.\n"
        ".fta_iloop:\n"
        "\tmovq %%rdi, %%rax\n"
        "\tmovq $0, %%rdx\n"
        "\tdivq %%r12\n"
        "\taddq $48, %%rax\n"
        "\tmovb %%al, (%%rbx)\n"
        "\tincq %%rbx\n"
        "\tmovq %%rdx, %%rdi\n"
        "\tmovq %%r12, %%rax\n"         // divisor /= 10: dividend is the\n"
        "\tmovq $0, %%rdx\n"            // divisor itself, 10 is the divisor\n"
        "\tmovq $10, %%rcx\n"
        "\tdivq %%rcx\n"
        "\tmovq %%rax, %%r12\n"
        "\ttestq %%rdi, %%rdi\n"
        "\tjnz .fta_iloop\n"
        "\n"
        "\tmovb $46, (%%rbx)\n"             // '.'\n"
        "\tincq %%rbx\n"
        "\n"
        "\t// Exactly six fractional digits, zero padded.\n"
        "\tmovq $100000, %%r12\n"
        "\tmovq $6, %%r13\n"
        ".fta_floop:\n"
        "\tmovq %%r10, %%rax\n"
        "\tmovq $0, %%rdx\n"
        "\tdivq %%r12\n"                 // rax = digit, rdx = remainder\n"
        "\taddq $48, %%rax\n"
        "\tmovb %%al, (%%rbx)\n"
        "\tincq %%rbx\n"
        "\tmovq %%rdx, %%r10\n"         // keep only the remainder\n"
        "\tmovq %%r12, %%rax\n"         // divisor /= 10 (same ordering as above)\n"
        "\tmovq $0, %%rdx\n"
        "\tmovq $10, %%rcx\n"
        "\tdivq %%rcx\n"
        "\tmovq %%rax, %%r12\n"
        "\tdecq %%r13\n"
        "\tjnz .fta_floop\n"
        "\n"
        "\tmovb $0, (%%rbx)\n"
        "\tleaq .fbuf(%%rip), %%rax\n"
        "\tsubq %%rax, %%rbx\n"             // length\n"
        "\tmovq %%rax, %%rsi\n"
        "\tmovq %%rbx, %%rdx\n"
        "\tpopq %%r13\n"
        "\tpopq %%r12\n"
        "\tpopq %%r11\n"
        "\tpopq %%r10\n"
        "\tpopq %%r9\n"
        "\tpopq %%r8\n"
        "\tpopq %%rdi\n"
        "\tpopq %%rcx\n"
        "\tpopq %%rbx\n"
        "\tret\n");
}

// ============================================================
// Emission
// ============================================================
static void emit_expr(CodegenCtx* ctx, ASTNode* expr);
static void emit_stmt(CodegenCtx* ctx, ASTNode* stmt);
static void emit_block(CodegenCtx* ctx, ASTBlock* block);

static void emit_print_arg(CodegenCtx* ctx, ASTNode* arg) {
    if (!arg) return;
    switch (arg->type) {
        case AST_STRING:
            emit_write_string(ctx, ((ASTString*)arg)->value);
            break;
        case AST_FLOAT:
            emit_expr(ctx, arg);
            emit_write_float(ctx);
            break;
        case AST_INT:
            asm_emit(ctx->buf, "\tmovq $%lld, %%rax\n",
                     (long long)((ASTInt*)arg)->value);
            emit_write_int(ctx);
            break;
        case AST_BOOL: {
            int t = ((ASTBool*)arg)->value;
            asm_emit(ctx->buf,
                     "\tmovq $1, %%rax\n"
                     "\tmovq $1, %%rdi\n"
                     "\tleaq .str_bool_%s(%%rip), %%rsi\n"
                     "\tmovq $%d, %%rdx\n"
                     "\tsyscall\n",
                     t ? "true" : "false", t ? 4 : 5);
            break;
        }
        case AST_IDENT: {
            TypeTag t;
            int off = ctx_find_var(ctx, ((ASTIdent*)arg)->name, &t);
            if (!off) return;   // unknown; the checker reports this
            emit_load_slot(ctx, off, t);
            if (t == TY_FLOAT) emit_write_float(ctx);
            else if (t == TY_STRING) emit_write_cstr(ctx);
            else              emit_write_int(ctx);
            break;
        }
        // For everything else the result tag decides the writer, so a float
        // expression never falls through to the integer path.
        case AST_BINARY:
        case AST_CALL:
        case AST_BUILTIN:
        case AST_UNARY:
            emit_expr(ctx, arg);
            if (arg->tag == TY_FLOAT) emit_write_float(ctx);
            else if (arg->tag == TY_STRING) emit_write_cstr(ctx);
            else                     emit_write_int(ctx);
            break;
        default:
            break;
    }
}

static void emit_block(CodegenCtx* ctx, ASTBlock* block) {
    if (!block) return;
    int vm = ctx_var_mark(ctx);
    int sm = ctx_slot_mark(ctx);
    for (int i = 0; i < block->statement_count; i++) {
        emit_stmt(ctx, block->statements[i]);
    }
    ctx_rewind(ctx, vm, sm);
}

static void emit_stmt(CodegenCtx* ctx, ASTNode* stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case AST_LET: {
            ASTLet* let = (ASTLet*)stmt;
            TypeTag slot_tag = decl_tag(let->type);
            int off = ctx_new_slot(ctx);
            emit_expr(ctx, let->value);
            // An int initialiser in a float64 slot needs widening first.
            if (slot_tag == TY_FLOAT && let->value && let->value->tag != TY_FLOAT) {
                asm_emit(ctx->buf, "\tcvtsi2sdq %%rax, %%xmm0\n");
            }
            emit_store_slot(ctx, off, slot_tag);
            ctx_add_var(ctx, let->name, off, slot_tag);
            break;
        }

        case AST_CONST: {
            ASTConst* c = (ASTConst*)stmt;
            TypeTag slot_tag = decl_tag(c->type);
            int off = ctx_new_slot(ctx);
            emit_expr(ctx, c->value);
            if (slot_tag == TY_FLOAT && c->value && c->value->tag != TY_FLOAT) {
                asm_emit(ctx->buf, "\tcvtsi2sdq %%rax, %%xmm0\n");
            }
            emit_store_slot(ctx, off, slot_tag);
            ctx_add_var(ctx, c->name, off, slot_tag);
            break;
        }

        // An assignment used as a statement: x = expr;
        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)stmt;
            if (strcmp(bin->op, "=") == 0 &&
                bin->left && bin->left->type == AST_IDENT) {
                TypeTag t;
                int off = ctx_find_var(ctx, ((ASTIdent*)bin->left)->name, &t);
                if (off) {
                    emit_expr(ctx, bin->right);
                    if (t == TY_FLOAT && bin->right && bin->right->tag != TY_FLOAT) {
                        asm_emit(ctx->buf, "\tcvtsi2sdq %%rax, %%xmm0\n");
                    }
                    emit_store_slot(ctx, off, t);
                }
            } else {
                emit_expr(ctx, stmt);   // discard the result
            }
            break;
        }

        case AST_PRINT: {
            ASTPrint* p = (ASTPrint*)stmt;
            for (int i = 0; i < p->arg_count; i++) {
                emit_print_arg(ctx, p->args[i]);
            }
            break;
        }

        case AST_IF: {
            ASTIf* iff = (ASTIf*)stmt;
            char* else_label = new_label(ctx);
            char* end_label  = new_label(ctx);

            emit_expr(ctx, iff->condition);          // %rax = 0 or 1
            asm_emit(ctx->buf, "\ttestq %%rax, %%rax\n");
            asm_emit(ctx->buf, "\tje %s\n", else_label);

            emit_block(ctx, iff->then_branch);
            asm_emit(ctx->buf, "\tjmp %s\n", end_label);

            asm_emit(ctx->buf, "%s:\n", else_label);
            if (iff->else_branch) emit_block(ctx, iff->else_branch);
            asm_emit(ctx->buf, "%s:\n", end_label);

            free(else_label);
            free(end_label);
            break;
        }

        case AST_WHILE: {
            ASTWhile* w = (ASTWhile*)stmt;
            char* cond_label = new_label(ctx);
            char* body_label = new_label(ctx);
            char* end_label  = new_label(ctx);

            asm_emit(ctx->buf, "%s:\n", cond_label);
            emit_expr(ctx, w->condition);
            asm_emit(ctx->buf, "\ttestq %%rax, %%rax\n");
            asm_emit(ctx->buf, "\tje %s\n", end_label);

            asm_emit(ctx->buf, "%s:\n", body_label);
            emit_block(ctx, w->body);

            asm_emit(ctx->buf, "\tjmp %s\n", cond_label);
            asm_emit(ctx->buf, "%s:\n", end_label);

            free(cond_label); free(body_label); free(end_label);
            break;
        }

        // for <var> in <start>..<end> { body }
        //
        // The range end is evaluated ONCE and parked in a hidden slot, so a
        // bound that is a function call is not re-evaluated every iteration.
        case AST_FOR: {
            ASTFor* f = (ASTFor*)stmt;
            char* cond_label = new_label(ctx);
            char* body_label = new_label(ctx);
            char* end_label  = new_label(ctx);

            int var_off  = ctx_new_slot(ctx);   // loop counter
            int limit_off = ctx_new_slot(ctx);  // hidden range end

            emit_expr(ctx, f->start);
            asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", var_off);

            emit_expr(ctx, f->end);
            asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", limit_off);

            // Declare the loop variable only around the loop body.
            ctx_add_var(ctx, f->var_name, var_off, TY_INT);

            asm_emit(ctx->buf, "%s:\n", cond_label);
            // rax = counter, limit = limit_off; jge exits when counter >= limit
            asm_emit(ctx->buf, "\tmovq %d(%%rbp), %%rax\n", var_off);
            asm_emit(ctx->buf, "\tcmpq %d(%%rbp), %%rax\n", limit_off);
            asm_emit(ctx->buf, "\tjge %s\n", end_label);

            asm_emit(ctx->buf, "%s:\n", body_label);
            emit_block(ctx, f->body);

            asm_emit(ctx->buf, "\tmovq %d(%%rbp), %%rax\n", var_off);
            asm_emit(ctx->buf, "\taddq $1, %%rax\n");
            asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", var_off);
            asm_emit(ctx->buf, "\tjmp %s\n", cond_label);

            asm_emit(ctx->buf, "%s:\n", end_label);

            free(cond_label); free(body_label); free(end_label);
            break;
        }

        case AST_RETURN: {
            ASTReturn* ret = (ASTReturn*)stmt;
            if (ret->value) emit_expr(ctx, ret->value);
            asm_emit(ctx->buf,
                     "\tmovq %%rbp, %%rsp\n"
                     "\tpopq %%rbp\n"
                     "\tret\n");
            break;
        }

        default:
            break;
    }
}

// Emit call arguments per SysV AMD64: floats -> %xmm0.., the rest -> %rdi...
// NOTE: only the first 6 GPR args are passed; builtins are designed within
// this limit. (User functions with >6 args are a known limitation.)
static void emit_call_args(CodegenCtx* ctx, ASTNode** args, int arg_count) {
    static const char* gpr_args[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
    static const char* sse_args[] = { "%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                      "%xmm4", "%xmm5", "%xmm6", "%xmm7" };
    int gpr = 0, sse = 0;
    for (int i = 0; i < arg_count; i++) {
        ASTNode* a = args[i];
        emit_expr(ctx, a);
        if (a && a->tag == TY_FLOAT) {
            if (sse < 8) asm_emit(ctx->buf, "\tmovsd %%xmm0, %s\n", sse_args[sse++]);
        } else {
            if (gpr < 6) asm_emit(ctx->buf, "\tmovq %%rax, %s\n", gpr_args[gpr++]);
        }
    }
}

static void emit_expr(CodegenCtx* ctx, ASTNode* expr) {
    if (!expr) return;

    switch (expr->type) {
        case AST_INT:
            asm_emit(ctx->buf, "\tmovq $%lld, %%rax\n",
                     (long long)((ASTInt*)expr)->value);
            break;

        case AST_FLOAT: {
            // Materialise the double as its exact IEEE-754 bit pattern, then
            // move it into an SSE register. Going through .rodata keeps the
            // mantissa intact; loading the truncated integer would lose it.
            double d = ((ASTFloat*)expr)->value;
            uint64_t bits;
            memcpy(&bits, &d, sizeof(bits));
            asm_emit(ctx->buf,
                     "\tmovabsq $%llu, %%rax\n"
                     "\tmovq %%rax, %%xmm0\n",
                     (unsigned long long)bits);
            break;
        }

        case AST_BOOL:
            asm_emit(ctx->buf, "\tmovq $%d, %%rax\n",
                     ((ASTBool*)expr)->value ? 1 : 0);
            break;

        case AST_STRING:
            asm_emit(ctx->buf, "\tleaq str_%d(%%rip), %%rax\n",
                     add_string(ctx->buf, ((ASTString*)expr)->value));
            break;

        case AST_IDENT: {
            TypeTag t;
            int off = ctx_find_var(ctx, ((ASTIdent*)expr)->name, &t);
            if (!off) {
                asm_emit(ctx->buf, "\txorl %%eax, %%eax\n");
            } else {
                emit_load_slot(ctx, off, t);
            }
            break;
        }

        case AST_UNARY: {
            ASTUnary* un = (ASTUnary*)expr;
            emit_expr(ctx, un->operand);
            if (strcmp(un->op, "-") == 0) {
                if (un->operand && un->operand->tag == TY_FLOAT) {
                    // Negating a double flips its sign bit; the value
                    // itself lives in %xmm0, not %rax.
                    asm_emit(ctx->buf, "\txorpd .f_negmask(%%rip), %%xmm0\n");
                } else {
                    asm_emit(ctx->buf, "\tnegq %%rax\n");
                }
            } else if (strcmp(un->op, "not") == 0) {
                // Comparison results are normalised to 0/1, so xor is enough.
                asm_emit(ctx->buf, "\txorq $1, %%rax\n");
            }
            break;
        }

        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)expr;

            // Short-circuiting logical operators are lowered before the
            // generic form below, because evaluating the right operand
            // eagerly would defeat the whole point.
            if (strcmp(bin->op, "and") == 0 || strcmp(bin->op, "or") == 0) {
                int is_and = (bin->op[0] == 'a');

                // For `and` the decisive condition is "an operand is false",
                // so we branch when we see a zero. For `or` it is "an operand
                // is true", so we branch on a non-zero.
                const char* decisive = is_and ? "\tje %s\n" : "\tjne %s\n";
                // Reaching the short-circuit label means an operand decided
                // the result: false for `and`, true for `or`.
                const char* short_val = is_and ? "\txorl %%eax, %%eax\n"
                                               : "\tmovq $1, %%rax\n";
                // Falling through means no operand was decisive: both were
                // true for `and`, both were false for `or`.
                const char* fall_val = is_and ? "\tmovq $1, %%rax\n"
                                              : "\txorl %%eax, %%eax\n";

                char* short_label = new_label(ctx);
                char* end_label   = new_label(ctx);

                emit_expr(ctx, bin->left);
                asm_emit(ctx->buf, "\ttestq %%rax, %%rax\n");
                asm_emit(ctx->buf, decisive, short_label);

                emit_expr(ctx, bin->right);
                asm_emit(ctx->buf, "\ttestq %%rax, %%rax\n");
                asm_emit(ctx->buf, decisive, short_label);

                asm_emit(ctx->buf, fall_val);
                asm_emit(ctx->buf, "\tjmp %s\n", end_label);

                asm_emit(ctx->buf, "%s:\n", short_label);
                asm_emit(ctx->buf, short_val);
                asm_emit(ctx->buf, "%s:\n", end_label);

                free(short_label);
                free(end_label);
                break;
            }

            // Integer path: left -> %rax, spill, right -> %rax, right -> %rbx.
            //
            // The left operand must survive the evaluation of the right one,
            // and the right one may be a function call. %rbx is callee-saved
            // but the callee is free to destroy it, so the spill goes on the
            // stack, which a call cannot clobber. Keeping the left operand in
            // %rbx makes every recursive expression silently evaluate to the
            // base case.
            //
            // The float path keeps both operands in SSE registers, which the
            // ABI already preserves across calls.
            //
            // The choice is driven by the OPERAND tags, not the result tag:
            // a comparison always yields bool, so testing the result would
            // send every float comparison down the integer path.
            int is_float = (bin->left && bin->left->tag == TY_FLOAT) ||
                           (bin->right && bin->right->tag == TY_FLOAT);

            if (is_float) {
                emit_expr(ctx, bin->left);
                if (bin->left && bin->left->tag != TY_FLOAT) {
                    asm_emit(ctx->buf, "\tcvtsi2sdq %%rax, %%xmm0\n");
                }
                asm_emit(ctx->buf, "\tmovapd %%xmm0, %%xmm6\n");
                emit_expr(ctx, bin->right);
                if (bin->right && bin->right->tag != TY_FLOAT) {
                    asm_emit(ctx->buf, "\tcvtsi2sdq %%rax, %%xmm0\n");
                }

                if (strcmp(bin->op, "=") == 0) break;

                if (strcmp(bin->op, "+") == 0) {
                    asm_emit(ctx->buf, "\taddsd %%xmm0, %%xmm6\n\tmovapd %%xmm6, %%xmm0\n");
                    break;
                }
                if (strcmp(bin->op, "-") == 0) {
                    asm_emit(ctx->buf, "\tsubsd %%xmm0, %%xmm6\n\tmovapd %%xmm6, %%xmm0\n");
                    break;
                }
                if (strcmp(bin->op, "*") == 0) {
                    asm_emit(ctx->buf, "\tmulsd %%xmm0, %%xmm6\n\tmovapd %%xmm6, %%xmm0\n");
                    break;
                }
                if (strcmp(bin->op, "/") == 0) {
                    asm_emit(ctx->buf, "\tdivsd %%xmm0, %%xmm6\n\tmovapd %%xmm6, %%xmm0\n");
                    break;
                }
                if (strcmp(bin->op, "%") == 0) {
                    // Truncated remainder: x - trunc(x / y) * y. Done inline
                    // rather than calling fmod(), which would drag libc into
                    // the link and require a dynamic loader for a binary
                    // that is otherwise freestanding.
                    asm_emit(ctx->buf,
                             "\tdivsd %%xmm0, %%xmm6\n"    // q = x / y
                             "\tcvttsd2siq %%xmm6, %%rax\n"
                             "\tcvtsi2sdq %%rax, %%xmm7\n" // trunc toward zero
                             "\tmulsd %%xmm0, %%xmm7\n"
                             "\tsubsd %%xmm7, %%xmm6\n");
                    break;
                }
                // Comparisons: comisd sets CF/ZF like an unsigned compare, so
                // seta/setae give "greater" / "greater or equal".
                if (strcmp(bin->op, "==") == 0 || strcmp(bin->op, "!=") == 0 ||
                    strcmp(bin->op, "<")  == 0 || strcmp(bin->op, ">")  == 0 ||
                    strcmp(bin->op, "<=") == 0 || strcmp(bin->op, ">=") == 0) {
                    const char* cc;
                    if      (strcmp(bin->op, "==") == 0) cc = "seteq";
                    else if (strcmp(bin->op, "!=") == 0) cc = "setne";
                    else if (strcmp(bin->op, "<")  == 0) cc = "setb";
                    else if (strcmp(bin->op, ">")  == 0) cc = "seta";
                    else if (strcmp(bin->op, "<=") == 0) cc = "setbe";
                    else                                  cc = "setae";
                    asm_emit(ctx->buf,
                             "\tcomisd %%xmm0, %%xmm6\n"
                             "\tmovq $0, %%rax\n"
                             "\t%s %%al\n"
                             "\tmovzbq %%al, %%rax\n", cc);
                    break;
                }
                asm_emit(ctx->buf, "\txorl %%eax, %%eax\n");
                break;
            }

            emit_expr(ctx, bin->left);
            asm_emit(ctx->buf, "\tpushq %%rax\n");
            emit_expr(ctx, bin->right);
            asm_emit(ctx->buf,
                     "\tmovq %%rax, %%rbx\n"   // right -> %rbx
                     "\tpopq %%rax\n");        // left  -> %rax

            if (strcmp(bin->op, "=") == 0) break;   // value is already in %rax

            if (strcmp(bin->op, "+") == 0) {
                asm_emit(ctx->buf, "\taddq %%rbx, %%rax\n");
                break;
            }
            if (strcmp(bin->op, "-") == 0) {
                asm_emit(ctx->buf, "\tsubq %%rbx, %%rax\n");
                break;
            }
            if (strcmp(bin->op, "*") == 0) {
                asm_emit(ctx->buf, "\timulq %%rbx, %%rax\n");
                break;
            }
            if (strcmp(bin->op, "/") == 0 || strcmp(bin->op, "%") == 0) {
                // %rax is already the dividend; idiv divides rdx:rax by its
                // operand, so the divisor has to sit in a scratch register.
                asm_emit(ctx->buf,
                         "\tmovq %%rbx, %%rcx\n"
                         "\tcqo\n"
                         "\tidivq %%rcx\n");      // rax = q, rdx = r
                if (bin->op[0] == '%') asm_emit(ctx->buf, "\tmovq %%rdx, %%rax\n");
                break;
            }

            // Comparisons. AT&T `cmpq src, dst` computes dst - src, and here
            // dst = left, src = right, so setCC reads `left OP right`.
            if (strcmp(bin->op, "==") == 0 || strcmp(bin->op, "!=") == 0 ||
                strcmp(bin->op, "<")  == 0 || strcmp(bin->op, ">")  == 0 ||
                strcmp(bin->op, "<=") == 0 || strcmp(bin->op, ">=") == 0) {
                const char* cc;
                if      (strcmp(bin->op, "==") == 0) cc = "sete";
                else if (strcmp(bin->op, "!=") == 0) cc = "setne";
                else if (strcmp(bin->op, "<")  == 0) cc = "setl";
                else if (strcmp(bin->op, ">")  == 0) cc = "setg";
                else if (strcmp(bin->op, "<=") == 0) cc = "setle";
                else                                  cc = "setge";
                asm_emit(ctx->buf,
                         "\tcmpq %%rbx, %%rax\n"
                         "\tmovq $0, %%rcx\n"
                         "\tmovq $1, %%rdx\n"
                         "\t%s %%cl\n"
                         "\tmovzbq %%cl, %%rax\n", cc);
                break;
            }

            asm_emit(ctx->buf, "\txorl %%eax, %%eax\n");
            break;
        }

        case AST_CALL: {
            ASTCall* call = (ASTCall*)expr;
            emit_call_args(ctx, call->args, call->arg_count);
            asm_emit(ctx->buf, "\tcall %s\n", call->callee);
            break;
        }

        case AST_BUILTIN: {
            // System builtin: lowered to a raw-syscall helper in syslib.o.
            // All builtins take <= 6 GPR args, so the shared emitter fits.
            ASTBuiltin* b = (ASTBuiltin*)expr;
            emit_call_args(ctx, b->args, b->arg_count);
            asm_emit(ctx->buf, "\tcall ga_%s\n", b->name);
            break;
        }

        default:
            asm_emit(ctx->buf, "\txorl %%eax, %%eax\n");
            break;
    }
}

// ============================================================
// Function emission
// ============================================================
static void emit_function(CodegenCtx* ctx, ASTFunction* fn) {
    ctx->var_count = 0;
    ctx->next_slot = 0;

    asm_emit(ctx->buf, "\t.globl %s\n", fn->name);
    asm_emit(ctx->buf, "\t.type %s, @function\n", fn->name);
    asm_emit(ctx->buf, "%s:\n", fn->name);

    asm_emit(ctx->buf,
             "\tpushq %%rbp\n"
             "\tmovq %%rsp, %%rbp\n"
             "\tsubq $%d, %%rsp\n", MAX_SLOTS * 8);

    // Spill incoming registers into their slots.
    // SysV AMD64 classifies arguments into two independent sequences:
    // floats consume %xmm0..%xmm7, everything else consumes %rdi,%rsi,
    // %rdx,%rcx,%r8,%r9. Spilling a float parameter with movsd from a
    // general-purpose register is not encodable.
    static const char* gpr_args[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
    static const char* sse_args[] = { "%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                      "%xmm4", "%xmm5", "%xmm6", "%xmm7" };
    int gpr = 0, sse = 0;
    for (int i = 0; i < fn->param_count; i++) {
        TypeTag pt = decl_tag(fn->params[i]->type);
        int off = ctx_new_slot(ctx);
        if (pt == TY_FLOAT) {
            if (sse < 8) {
                asm_emit(ctx->buf, "\tmovsd %s, %d(%%rbp)\n", sse_args[sse++], off);
            }
        } else {
            if (gpr < 6) {
                asm_emit(ctx->buf, "\tmovq %s, %d(%%rbp)\n", gpr_args[gpr++], off);
            }
        }
        ctx_add_var(ctx, fn->params[i]->name, off, pt);
    }

    // The body gets its own scope, but the parameter slots must survive it.
    int vm = ctx_var_mark(ctx);
    int sm = ctx_slot_mark(ctx);
    for (int i = 0; i < fn->body->statement_count; i++) {
        emit_stmt(ctx, fn->body->statements[i]);
    }
    ctx_rewind(ctx, vm, sm);

    // Implicit `return 0` so falling off the end is well defined.
    asm_emit(ctx->buf,
             "\txorl %%eax, %%eax\n"
             "\tmovq %%rbp, %%rsp\n"
             "\tpopq %%rbp\n"
             "\tret\n");
}

// ============================================================
// Program emission
// ============================================================
static void generate_assembly(ASTProgram* program, AsmBuffer* b, Error* error) {
    CodegenCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.buf = b;

    asm_emit(b,
        ".text\n"
        ".globl _start\n"
        ".type _start, @function\n"
        "_start:\n"
        "\txorl %%ebp, %%ebp\n"
        // Save envp for ga_sys_env: rsp -> [argc][argv...][NULL][envp...]
        "\tmovq (%%rsp), %%rax\n"
        "\tleaq 16(%%rsp,%%rax,8), %%rax\n"
        "\tmovq %%rax, ga_envp(%%rip)\n"
        "\tcall main\n"
        "\tmovq %%rax, %%rdi\n"
        "\tmovq $60, %%rax\n"
        "\tsyscall\n");

    emit_int_to_ascii_helper(b);
    emit_float_to_ascii_helper(b);

    for (int i = 0; i < program->statement_count; i++) {
        if (program->statements[i]->type == AST_FUNCTION) {
            emit_function(&ctx, (ASTFunction*)program->statements[i]);
        }
    }

    free(ctx.vars);

    asm_emit(b, ".section .rodata\n");
    for (int i = 0; i < b->string_count; i++) {
        asm_emit(b, "str_%d:\n\t.asciz ", i);
        emit_escaped_string(b, b->strings[i]);
        asm_emit(b, "\n");
    }
    asm_emit(b, ".str_bool_true:\n\t.ascii \"true\"\n");
    asm_emit(b, ".str_bool_false:\n\t.ascii \"false\"\n");

    // Float formatting constants: the absolute-value mask (sign bit
    // cleared), the negation mask (sign bit only), 0.0 and 10.0.
    asm_emit(b, ".align 16\n");
    asm_emit(b, ".f_absmask:\n\t.quad 0x7fffffffffffffff\n\t.quad 0xffffffffffffffff\n");
    asm_emit(b, ".f_negmask:\n\t.quad 0x8000000000000000\n\t.quad 0\n");
    asm_emit(b, ".f_zero:\n\t.quad 0\n\t.quad 0\n");
    asm_emit(b, ".f_ten:\n\t.quad 0x4024000000000000\n\t.quad 0\n");
    asm_emit(b, ".f_half:\n\t.quad 0x3FE0000000000000\n\t.quad 0\n");
    asm_emit(b, ".f_1e6:\n\t.quad 0x412e848000000000\n\t.quad 0\n");

    // Writable scratch buffers for number formatting.
    asm_emit(b,
        ".data\n"
        ".align 8\n"
        ".int_buf:\n\t.zero 32\n"
        ".fbuf:\n\t.zero 64\n");

    // Mark the stack non-executable (silences newer ld).
    asm_emit(b, ".section .note.GNU-stack,\"\",@progbits\n");

    (void)error;
}

// ============================================================
// Locate syslib.o (freestanding system library). Search order:
//   1. $GAR_SYSLIB environment variable
//   2. directory containing the running `gar` binary (/proc/self/exe)
//   3. ./bin/syslib.o (development checkout)
static bool find_syslib(char* out, size_t out_size) {
    const char* env = getenv("GAR_SYSLIB");
    if (env && access(env, R_OK) == 0) {
        snprintf(out, out_size, "%s", env);
        return true;
    }
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char* slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(out, out_size, "%s/syslib.o", exe);
            if (access(out, R_OK) == 0) return true;
        }
    }
    if (access("./bin/syslib.o", R_OK) == 0) {
        snprintf(out, out_size, "./bin/syslib.o");
        return true;
    }
    return false;
}

// Compile assembly to binary
// ============================================================
static bool compile_assembly(const char* asm_path, const char* output_path, Error* error) {
    char cmd[1024];
    char obj_path[256];

    // Derive the object path from the assembly path so concurrent builds
    // (e.g. several `gar build` in parallel) never clobber each other.
    snprintf(obj_path, sizeof(obj_path), "%.*s.o", (int)(strlen(asm_path) - 4), asm_path);

    snprintf(cmd, sizeof(cmd), "as -o %s %s 2>&1", obj_path, asm_path);
    if (system(cmd) != 0) {
        set_error(error, "Assembler (as) failed", 0, 0);
        return false;
    }

    // Freestanding link: the generated code talks to the kernel directly and
    // references no libc symbol, so no dynamic loader is involved.
    // syslib.o (raw-syscall helpers) is linked statically.
    char syslib[512];
    if (!find_syslib(syslib, sizeof(syslib))) {
        set_error(error, "syslib.o not found (build with `make`; or set $GAR_SYSLIB)", 0, 0);
        unlink(obj_path);
        return false;
    }
    snprintf(cmd, sizeof(cmd), "ld -o %s %s %s 2>&1", output_path, obj_path, syslib);
    if (system(cmd) != 0) {
        set_error(error, "Linker (ld) failed", 0, 0);
        unlink(obj_path);
        return false;
    }

    unlink(obj_path);
    // Set GAR_KEEP_ASM=1 to inspect the generated assembly after building.
    if (getenv("GAR_KEEP_ASM")) {
        fprintf(stderr, "[gar] assembly kept at %s\n", asm_path);
    } else {
        unlink(asm_path);
    }
    return true;
}

// ============================================================
// Public API
// ============================================================
bool codegen_generate(ASTProgram* program, const char* output_path, Error* error) {
    clear_error(error);

    AsmBuffer* b = asm_new();
    generate_assembly(program, b, error);
    if (error->has_error) {
        asm_free(b);
        return false;
    }

    // Per-process assembly path so parallel builds don't collide.
    char asm_path[256];
    snprintf(asm_path, sizeof(asm_path), "/tmp/gar_%d.asm", (int)getpid());

    FILE* f = fopen(asm_path, "w");
    if (!f) {
        set_error(error, "Cannot create temp assembly file", 0, 0);
        asm_free(b);
        return false;
    }
    fwrite(b->buffer, 1, b->length, f);
    fclose(f);

    bool ok = compile_assembly(asm_path, output_path, error);

    asm_free(b);
    return ok;
}

void codegen_free(void) {}
