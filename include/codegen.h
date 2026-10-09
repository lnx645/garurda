// ============================================================
// Native code generator (x86-64 ELF64)
// Author: GalangTeam
// ============================================================

#ifndef CODEGEN_H
#define CODEGEN_H

#include "common.h"
#include "ast.h"

// ============================================================
// Code generator state
// ============================================================
typedef struct {
    uint8_t* code;
    int code_capacity;
    int code_length;
    int label_counter;
    int string_count;
    char** strings;
} CodeGen;

// ============================================================
// Public API
// ============================================================
bool codegen_generate(ASTProgram* program, const char* output_path, 
                      Error* error);
void codegen_free(void);

#endif // CODEGEN_H
