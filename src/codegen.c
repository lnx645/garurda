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
    int offset;   // NEGATIVE byte offset from %rbp
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
static int ctx_add_var(CodegenCtx* ctx, const char* name, int offset) {
    if (ctx->var_count >= ctx->var_capacity) {
        ctx->var_capacity = ctx->var_capacity == 0 ? 16 : ctx->var_capacity * 2;
        ctx->vars = realloc(ctx->vars, ctx->var_capacity * sizeof(VarInfo));
    }
    snprintf(ctx->vars[ctx->var_count].name,
             sizeof(ctx->vars[ctx->var_count].name), "%s", name);
    ctx->vars[ctx->var_count].offset = offset;
    return ctx->var_count++;
}

// Returns the slot offset, or 0 when unknown (0 is never a valid slot,
// since every real slot is negative).
static int ctx_find_var(CodegenCtx* ctx, const char* name) {
    for (int i = ctx->var_count - 1; i >= 0; i--) {
        if (strcmp(ctx->vars[i].name, name) == 0) return ctx->vars[i].offset;
    }
    return 0;
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

// Convert the integer in %rax to decimal ASCII on stdout.
static void emit_write_int(CodegenCtx* ctx) {
    asm_emit(ctx->buf,
             "\tmovq %%rax, %%rdi\n"
             "\tcall .int_to_ascii\n"
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
            int off = ctx_find_var(ctx, ((ASTIdent*)arg)->name);
            if (!off) return;   // unknown; the checker reports this
            asm_emit(ctx->buf, "\tmovq %d(%%rbp), %%rax\n", off);
            emit_write_int(ctx);
            break;
        }
        case AST_BINARY: {
            emit_expr(ctx, arg);
            emit_write_int(ctx);
            break;
        }
        case AST_CALL:
            // A call in print position: print its integer result.
            emit_expr(ctx, arg);
            emit_write_int(ctx);
            break;
        case AST_UNARY: {
            emit_expr(ctx, arg);
            emit_write_int(ctx);
            break;
        }
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
            int off = ctx_new_slot(ctx);
            emit_expr(ctx, let->value);
            asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", off);
            ctx_add_var(ctx, let->name, off);
            break;
        }

        case AST_CONST: {
            ASTConst* c = (ASTConst*)stmt;
            int off = ctx_new_slot(ctx);
            emit_expr(ctx, c->value);
            asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", off);
            ctx_add_var(ctx, c->name, off);
            break;
        }

        // An assignment used as a statement: x = expr;
        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)stmt;
            if (strcmp(bin->op, "=") == 0 &&
                bin->left && bin->left->type == AST_IDENT) {
                int off = ctx_find_var(ctx, ((ASTIdent*)bin->left)->name);
                if (off) {
                    emit_expr(ctx, bin->right);
                    asm_emit(ctx->buf, "\tmovq %%rax, %d(%%rbp)\n", off);
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
            ctx_add_var(ctx, f->var_name, var_off);

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

static void emit_expr(CodegenCtx* ctx, ASTNode* expr) {
    if (!expr) return;

    switch (expr->type) {
        case AST_INT:
            asm_emit(ctx->buf, "\tmovq $%lld, %%rax\n",
                     (long long)((ASTInt*)expr)->value);
            break;

        case AST_FLOAT:
            // Floats are not implemented yet; treat as integer truncation.
            asm_emit(ctx->buf, "\tmovq $%lld, %%rax\n",
                     (long long)((ASTFloat*)expr)->value);
            break;

        case AST_BOOL:
            asm_emit(ctx->buf, "\tmovq $%d, %%rax\n",
                     ((ASTBool*)expr)->value ? 1 : 0);
            break;

        case AST_STRING:
            asm_emit(ctx->buf, "\tleaq str_%d(%%rip), %%rax\n",
                     add_string(ctx->buf, ((ASTString*)expr)->value));
            break;

        case AST_IDENT: {
            int off = ctx_find_var(ctx, ((ASTIdent*)expr)->name);
            if (off) {
                asm_emit(ctx->buf, "\tmovq %d(%%rbp), %%rax\n", off);
            } else {
                asm_emit(ctx->buf, "\txorl %%eax, %%eax\n");
            }
            break;
        }

        case AST_UNARY: {
            ASTUnary* un = (ASTUnary*)expr;
            emit_expr(ctx, un->operand);
            if (strcmp(un->op, "-") == 0) {
                asm_emit(ctx->buf, "\tnegq %%rax\n");
            } else if (strcmp(un->op, "not") == 0) {
                // Comparison results are normalised to 0/1, so xor is enough.
                asm_emit(ctx->buf, "\txorq $1, %%rax\n");
            }
            break;
        }

        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)expr;

            // left -> %rax, spill to the stack, then evaluate right.
            //
            // The left operand must survive the evaluation of the right one,
            // and the right one may be a function call. %rbx is callee-saved
            // but the callee is free to destroy it, so the spill goes on the
            // stack, which a call cannot clobber. Keeping the left operand in
            // %rbx makes every recursive expression silently evaluate to the
            // base case.
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
            if (strcmp(bin->op, "and") == 0) {
                asm_emit(ctx->buf, "\tandq %%rbx, %%rax\n");
                break;
            }
            if (strcmp(bin->op, "or") == 0) {
                asm_emit(ctx->buf, "\torq %%rbx, %%rax\n");
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
            // SysV integer argument registers.
            static const char* arg_regs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
            int n = call->arg_count < 6 ? call->arg_count : 6;
            for (int i = 0; i < n; i++) {
                emit_expr(ctx, call->args[i]);
                asm_emit(ctx->buf, "\tmovq %%rax, %s\n", arg_regs[i]);
            }
            asm_emit(ctx->buf, "\tcall %s\n", call->callee);
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
    static const char* arg_regs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
    int nparams = fn->param_count < 6 ? fn->param_count : 6;
    for (int i = 0; i < nparams; i++) {
        int off = ctx_new_slot(ctx);
        asm_emit(ctx->buf, "\tmovq %s, %d(%%rbp)\n", arg_regs[i], off);
        ctx_add_var(ctx, fn->params[i]->name, off);
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
        "\tcall main\n"
        "\tmovq %%rax, %%rdi\n"
        "\tmovq $60, %%rax\n"
        "\tsyscall\n");

    emit_int_to_ascii_helper(b);

    for (int i = 0; i < program->statement_count; i++) {
        if (program->statements[i]->type == AST_FUNCTION) {
            emit_function(&ctx, (ASTFunction*)program->statements[i]);
        }
    }

    free(ctx.vars);

    asm_emit(b, ".section .rodata\n");
    for (int i = 0; i < b->string_count; i++) {
        asm_emit(b, "str_%d:\n\t.ascii ", i);
        emit_escaped_string(b, b->strings[i]);
        asm_emit(b, "\n");
    }
    asm_emit(b, ".str_bool_true:\n\t.ascii \"true\"\n");
    asm_emit(b, ".str_bool_false:\n\t.ascii \"false\"\n");

    // Writable scratch buffer for integer formatting.
    asm_emit(b,
        ".data\n"
        ".align 8\n"
        ".int_buf:\n\t.zero 32\n");

    // Mark the stack non-executable (silences newer ld).
    asm_emit(b, ".section .note.GNU-stack,\"\",@progbits\n");

    (void)error;
}

// ============================================================
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

    snprintf(cmd, sizeof(cmd), "ld -o %s %s 2>&1", output_path, obj_path);
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
