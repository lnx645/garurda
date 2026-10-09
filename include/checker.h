// ============================================================
// Type checker for GarudaLang
// Author: GalangTeam
// ============================================================

#ifndef CHECKER_H
#define CHECKER_H

#include "common.h"
#include "ast.h"

// ============================================================
// Type environment
// ============================================================
typedef struct {
    char** names;
    char** types;
    int count;
    int capacity;
} TypeEnv;

// ============================================================
// Public API
// ============================================================
bool checker_check(ASTProgram* program, Error* error);
void checker_free(void);

#endif // CHECKER_H
