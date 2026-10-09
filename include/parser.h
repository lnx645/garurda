// ============================================================
// Parser for GarudaLang
// Author: GalangTeam
// ============================================================

#ifndef PARSER_H
#define PARSER_H

#include "common.h"
#include "ast.h"

// ============================================================
// Parser state
// ============================================================
typedef struct {
    Token** tokens;
    int token_count;
    int current;
    Error error;
} Parser;

// ============================================================
// Public API
// ============================================================
ASTProgram* parser_parse(Token** tokens, int token_count, Error* error);
void parser_free(Parser* parser);

#endif // PARSER_H
