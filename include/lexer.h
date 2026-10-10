// ============================================================
// Lexer (Tokenizer) for GarudaLang
// Author: GalangTeam
// ============================================================

#ifndef LEXER_H
#define LEXER_H

#include "common.h"
#include "ast.h"

// ============================================================
// Lexer state
// ============================================================
typedef struct {
    const char* source;
    int length;
    int pos;
    int line;
    int column;
    char current_char;
} Lexer;

// ============================================================
// Public API
// ============================================================
Lexer* lexer_new(const char* source);
void lexer_free(Lexer* lexer);
Token* lexer_next_token(Lexer* lexer);
Token** lexer_tokenize(const char* source, int* token_count, Error* error);
const char* token_type_str(TokenType type);

// ============================================================
// Token management
// ============================================================
void token_free(Token* token);

// Frees a token array from lexer_tokenize(), including the trailing
// EOF token it stores at index `count`.
void lexer_tokens_free(Token** tokens, int count);

#endif // LEXER_H
