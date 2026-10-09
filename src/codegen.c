// ============================================================
// Native code generator: x86-64 Assembly -> ELF (via as/ld)
// Author: GalangTeam
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

static AsmBuffer* asm_new(void) {
    AsmBuffer* b = calloc(1, sizeof(AsmBuffer));
    b->capacity = 8192;
    b->buffer = calloc(b->capacity, 1);
    b->strings_capacity = 64;
    b->strings = calloc(b->strings_capacity, sizeof(char*));
    return b;
}

static void asm_free(AsmBuffer* b) {
    for (int i = 0; i < b->string_count; i++) {
        free(b->strings[i]);
    }
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

// ============================================================
// String management
// ============================================================
static int add_string(AsmBuffer* b, const char* str) {
    for (int i = 0; i < b->string_count; i++) {
        if (strcmp(b->strings[i], str) == 0) {
            return i;
        }
    }
    
    if (b->string_count >= b->strings_capacity) {
        b->strings_capacity *= 2;
        b->strings = realloc(b->strings, b->strings_capacity * sizeof(char*));
    }
    
    b->strings[b->string_count] = strdup(str);
    return b->string_count++;
}

// ============================================================
// Escape string for assembly
// ============================================================
static void emit_escaped_string(AsmBuffer* b, const char* str) {
    asm_emit(b, "\"");
    for (const char* c = str; *c; c++) {
        switch (*c) {
            case '\\': asm_emit(b, "\\\\"); break;
            case '"': asm_emit(b, "\\\""); break;
            case '\n': asm_emit(b, "\\n"); break;
            case '\t': asm_emit(b, "\\t"); break;
            case '\r': asm_emit(b, "\\r"); break;
            default:
                if (*c >= 32 && *c < 127) {
                    asm_emit(b, "%c", *c);
                } else {
                    asm_emit(b, "\\x%02x", (unsigned char)*c);
                }
                break;
        }
    }
    asm_emit(b, "\"");
}

// ============================================================
// Emit assembly for expressions
// ============================================================
static void emit_expr_asm(AsmBuffer* b, ASTNode* expr);

static void emit_stmt_asm(AsmBuffer* b, ASTNode* stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case AST_PRINT: {
            ASTPrint* p = (ASTPrint*)stmt;
            for (int i = 0; i < p->arg_count; i++) {
                ASTNode* arg = p->args[i];
                if (arg->type == AST_STRING) {
                    ASTString* str = (ASTString*)arg;
                    int str_idx = add_string(b, str->value);
                    
                    // syscall: write(1, str, len)
                    asm_emit(b, "\tmovq $1, %%rax\n");
                    asm_emit(b, "\tmovq $1, %%rdi\n");
                    asm_emit(b, "\tleaq str_%d(%%rip), %%rsi\n", str_idx);
                    asm_emit(b, "\tmovq $%d, %%rdx\n", (int)strlen(str->value));
                    asm_emit(b, "\tsyscall\n");
                } else if (arg->type == AST_INT) {
                    ASTInt* n = (ASTInt*)arg;
                    asm_emit(b, "\tmovq $%lld, %%rax\n", (long long)n->value);
                    asm_emit(b, "\tmovq $1, %%rdi\n");
                    asm_emit(b, "\tleaq str_int_out(%%rip), %%rsi\n");
                    asm_emit(b, "\tmovq $32, %%rdx\n");
                    asm_emit(b, "\tsyscall\n");
                }
            }
            break;
        }
        
        case AST_RETURN: {
            ASTReturn* ret = (ASTReturn*)stmt;
            if (ret->value) {
                emit_expr_asm(b, ret->value);
            }
            asm_emit(b, "\tmovq %%rbp, %%rsp\n");
            asm_emit(b, "\tpopq %%rbp\n");
            asm_emit(b, "\tret\n");
            break;
        }
        
        default:
            break;
    }
}

static void emit_expr_asm(AsmBuffer* b, ASTNode* expr) {
    if (!expr) return;
    
    switch (expr->type) {
        case AST_INT: {
            ASTInt* n = (ASTInt*)expr;
            asm_emit(b, "\tmovq $%lld, %%rax\n", (long long)n->value);
            break;
        }
        
        case AST_STRING: {
            ASTString* s = (ASTString*)expr;
            int str_idx = add_string(b, s->value);
            asm_emit(b, "\tleaq str_%d(%%rip), %%rax\n", str_idx);
            break;
        }
        
        default:
            break;
    }
}

// ============================================================
// Emit function
// ============================================================
static void emit_function_asm(AsmBuffer* b, ASTFunction* fn, bool is_main) {
    asm_emit(b, "\t.globl %s\n", fn->name);
    asm_emit(b, "%s:\n", fn->name);
    
    // Prologue
    asm_emit(b, "\tpushq %%rbp\n");
    asm_emit(b, "\tmovq %%rsp, %%rbp\n");
    asm_emit(b, "\tsubq $16, %%rsp\n");
    
    // Emit body
    for (int i = 0; i < fn->body->statement_count; i++) {
        emit_stmt_asm(b, fn->body->statements[i]);
    }
    
    // Epilogue
    asm_emit(b, "\tmovq %%rbp, %%rsp\n");
    asm_emit(b, "\tpopq %%rbp\n");
    asm_emit(b, "\tret\n");
}

// ============================================================
// Generate assembly program
// ============================================================
static void generate_assembly(ASTProgram* program, AsmBuffer* b, Error* error) {
    asm_emit(b, ".text\n");
    asm_emit(b, ".globl _start\n");
    asm_emit(b, "_start:\n");
    
    // Call main
    asm_emit(b, "\tcall main\n");
    
    // Exit
    asm_emit(b, "\tmovq $60, %%rax\n");
    asm_emit(b, "\txor %%rdi, %%rdi\n");
    asm_emit(b, "\tsyscall\n");
    
    // Generate functions
    for (int i = 0; i < program->statement_count; i++) {
        if (program->statements[i]->type == AST_FUNCTION) {
            ASTFunction* fn = (ASTFunction*)program->statements[i];
            emit_function_asm(b, fn, (strcmp(fn->name, "main") == 0));
        }
    }
    
    // Data section
    asm_emit(b, ".data\n");
    asm_emit(b, ".align 8\n");
    
    // Write string literals
    for (int i = 0; i < b->string_count; i++) {
        asm_emit(b, "str_%d:\n\t.ascii ", i);
        emit_escaped_string(b, b->strings[i]);
        asm_emit(b, "\n");
    }
    
    // Integer output buffer
    asm_emit(b, "str_int_out:\n\t.zero 32\n");
}

// ============================================================
// Compile assembly to binary
// ============================================================
static bool compile_assembly(const char* asm_path, const char* output_path, Error* error) {
    // Assemble with GNU assembler
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "as -o /tmp/galang_temp.o %s 2>&1", asm_path);
    int status = system(cmd);
    if (status != 0) {
        set_error(error, "Assembly failed", 0, 0);
        return false;
    }
    
    // Link with GNU linker
    snprintf(cmd, sizeof(cmd), "ld -o %s /tmp/galang_temp.o 2>&1", output_path);
    status = system(cmd);
    if (status != 0) {
        set_error(error, "Linking failed", 0, 0);
        unlink("/tmp/galang_temp.o");
        return false;
    }
    
    // Cleanup
    unlink("/tmp/galang_temp.o");
    unlink(asm_path);
    
    return true;
}

// ============================================================
// Main entry point
// ============================================================
bool codegen_generate(ASTProgram* program, const char* output_path, Error* error) {
    clear_error(error);
    
    AsmBuffer* b = asm_new();
    
    generate_assembly(program, b, error);
    if (error->has_error) {
        asm_free(b);
        return false;
    }
    
    // Write assembly to temp file
    char asm_path[] = "/tmp/galang_temp.asm";
    FILE* f = fopen(asm_path, "w");
    if (!f) {
        set_error(error, "Cannot create temp file", 0, 0);
        asm_free(b);
        return false;
    }
    fwrite(b->buffer, 1, b->length, f);
    fclose(f);
    
    // Compile
    bool ok = compile_assembly(asm_path, output_path, error);
    
    asm_free(b);
    return ok;
}

void codegen_free(void) {}
