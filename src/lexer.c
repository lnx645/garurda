// ============================================================
// Lexer implementation for GarudaLang
// Author: GalangTeam
// ============================================================

#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

// ============================================================
// Helpers
// ============================================================
static void lexer_advance(Lexer* lexer) {
    if (lexer->pos < lexer->length) {
        if (lexer->current_char == '\n') {
            lexer->line++;
            lexer->column = 1;
        } else {
            lexer->column++;
        }
        lexer->pos++;
        lexer->current_char = (lexer->pos < lexer->length) 
                               ? lexer->source[lexer->pos] : '\0';
    }
}

static char lexer_peek(Lexer* lexer, int offset) {
    int p = lexer->pos + offset;
    if (p >= 0 && p < lexer->length) {
        return lexer->source[p];
    }
    return '\0';
}

static void lexer_skip_whitespace(Lexer* lexer) {
    while (lexer->current_char != '\0' && 
           isspace((unsigned char)lexer->current_char)) {
        lexer_advance(lexer);
    }
}

static void lexer_skip_comment(Lexer* lexer) {
    // Single-line comment: //
    if (lexer->current_char == '/' && lexer_peek(lexer, 1) == '/') {
        while (lexer->current_char != '\0' && lexer->current_char != '\n') {
            lexer_advance(lexer);
        }
        return;
    }
    
    // Multi-line comment: /* ... */
    if (lexer->current_char == '/' && lexer_peek(lexer, 1) == '*') {
        lexer_advance(lexer); // /
        lexer_advance(lexer); // *
        while (lexer->current_char != '\0') {
            if (lexer->current_char == '*' && lexer_peek(lexer, 1) == '/') {
                lexer_advance(lexer); // *
                lexer_advance(lexer); // /
                return;
            }
            lexer_advance(lexer);
        }
        return;
    }
}

// ============================================================
// Token management
// ============================================================
Token* token_new(TokenType type, const char* value, int line, int column) {
    Token* token = calloc(1, sizeof(Token));
    token->type = type;
    token->value = value ? strdup(value) : NULL;
    token->line = line;
    token->column = column;
    return token;
}

void token_free(Token* token) {
    if (!token) return;
    free(token->value);
    free(token);
}

// ============================================================
// Read string literal
// ============================================================
static Token* read_string(Lexer* lexer) {
    int start_line = lexer->line;
    int start_col = lexer->column;
    lexer_advance(lexer); // skip opening "
    
    char* value = malloc(256);
    int len = 0;
    while (lexer->current_char != '\0' && lexer->current_char != '"') {
        if (lexer->current_char == '\\' && len < 250) {
            lexer_advance(lexer);
            switch (lexer->current_char) {
                case 'n': value[len++] = '\n'; break;
                case 't': value[len++] = '\t'; break;
                case 'r': value[len++] = '\r'; break;
                case '\\': value[len++] = '\\'; break;
                case '"': value[len++] = '"'; break;
                default:
                    value[len++] = lexer->current_char;
                    break;
            }
            lexer_advance(lexer);
        } else {
            value[len++] = lexer->current_char;
            lexer_advance(lexer);
        }
    }
    value[len] = '\0';
    
    if (lexer->current_char == '"') {
        lexer_advance(lexer); // skip closing "
    }
    
    return token_new(TOKEN_STRING, value, start_line, start_col);
}

// ============================================================
// Read number literal
// ============================================================
static Token* read_number(Lexer* lexer) {
    int start_line = lexer->line;
    int start_col = lexer->column;
    
    bool is_float = false;
    int len = 0;
    int temp_pos = lexer->pos;
    
    while (temp_pos < lexer->length && 
           (isdigit((unsigned char)lexer->source[temp_pos]) || 
            lexer->source[temp_pos] == '.')) {
        if (lexer->source[temp_pos] == '.') {
            // Check if this is ".." range operator
            if (temp_pos + 1 < lexer->length && 
                lexer->source[temp_pos + 1] == '.') {
                break;
            }
            if (is_float) break;
            is_float = true;
        }
        temp_pos++;
        len++;
    }
    
    char* num_str = malloc(len + 1);
    memcpy(num_str, &lexer->source[lexer->pos], len);
    num_str[len] = '\0';
    
    for (int i = 0; i < len; i++) {
        lexer_advance(lexer);
    }
    
    TokenType type = is_float ? TOKEN_FLOAT : TOKEN_INT;
    return token_new(type, num_str, start_line, start_col);
}

// ============================================================
// Read identifier/keyword
// ============================================================
static Token* read_identifier(Lexer* lexer) {
    int start_line = lexer->line;
    int start_col = lexer->column;
    
    int len = 0;
    int temp_pos = lexer->pos;
    while (temp_pos < lexer->length && 
           (isalnum((unsigned char)lexer->source[temp_pos]) || 
            lexer->source[temp_pos] == '_')) {
        temp_pos++;
        len++;
    }
    
    char* ident = malloc(len + 1);
    memcpy(ident, &lexer->source[lexer->pos], len);
    ident[len] = '\0';
    
    for (int i = 0; i < len; i++) {
        lexer_advance(lexer);
    }
    
    TokenType type = TOKEN_IDENT;
    if (strcmp(ident, "fn") == 0) type = TOKEN_FN;
    else if (strcmp(ident, "let") == 0) type = TOKEN_LET;
    else if (strcmp(ident, "const") == 0) type = TOKEN_CONST;
    else if (strcmp(ident, "if") == 0) type = TOKEN_IF;
    else if (strcmp(ident, "else") == 0) type = TOKEN_ELSE;
    else if (strcmp(ident, "for") == 0) type = TOKEN_FOR;
    else if (strcmp(ident, "while") == 0) type = TOKEN_WHILE;
    else if (strcmp(ident, "return") == 0) type = TOKEN_RETURN;
    else if (strcmp(ident, "match") == 0) type = TOKEN_MATCH;
    else if (strcmp(ident, "enum") == 0) type = TOKEN_ENUM;
    else if (strcmp(ident, "struct") == 0) type = TOKEN_STRUCT;
    else if (strcmp(ident, "impl") == 0) type = TOKEN_IMPL;
    else if (strcmp(ident, "interface") == 0) type = TOKEN_INTERFACE;
    else if (strcmp(ident, "true") == 0) type = TOKEN_TRUE;
    else if (strcmp(ident, "false") == 0) type = TOKEN_FALSE;
    else if (strcmp(ident, "some") == 0) type = TOKEN_SOME;
    else if (strcmp(ident, "none") == 0) type = TOKEN_NONE;
    else if (strcmp(ident, "option") == 0) type = TOKEN_OPTION;
    else if (strcmp(ident, "print") == 0) type = TOKEN_PRINT;
    else if (strcmp(ident, "and") == 0) type = TOKEN_AND;
    else if (strcmp(ident, "or") == 0) type = TOKEN_OR;
    else if (strcmp(ident, "not") == 0) type = TOKEN_NOT;
    else if (strcmp(ident, "int") == 0) type = TOKEN_INT_TYPE;
    else if (strcmp(ident, "float64") == 0) type = TOKEN_FLOAT64_TYPE;
    else if (strcmp(ident, "bool") == 0) type = TOKEN_BOOL_TYPE;
    else if (strcmp(ident, "string") == 0) type = TOKEN_STRING_TYPE;
    
    return token_new(type, ident, start_line, start_col);
}

// ============================================================
// Two-character symbols
// ============================================================
static TokenType get_two_char_token(char c1, char c2) {
    if (c1 == '=' && c2 == '=') return TOKEN_EQEQ;
    if (c1 == '!' && c2 == '=') return TOKEN_NEQ;
    if (c1 == '<' && c2 == '=') return TOKEN_LTE;
    if (c1 == '>' && c2 == '=') return TOKEN_GTE;
    if (c1 == '=' && c2 == '>') return TOKEN_FAT_ARROW;
    if (c1 == '-' && c2 == '>') return TOKEN_ARROW;
    if (c1 == '.' && c2 == '.') return TOKEN_DOTDOT;
    if (c1 == '<' && c2 == '-') return TOKEN_LARROW;
    if (c1 == '+' && c2 == '=') return TOKEN_PLUS_EQ;
    if (c1 == '-' && c2 == '=') return TOKEN_MINUS_EQ;
    if (c1 == '*' && c2 == '=') return TOKEN_STAR_EQ;
    if (c1 == '/' && c2 == '=') return TOKEN_SLASH_EQ;
    return TOKEN_ERROR;
}

// ============================================================
// Main tokenization loop
// ============================================================
Token* lexer_next_token(Lexer* lexer) {
    while (lexer->current_char != '\0') {
        // Skip whitespace
        if (isspace((unsigned char)lexer->current_char)) {
            lexer_skip_whitespace(lexer);
            continue;
        }
        
        // Skip comments
        if (lexer->current_char == '/') {
            if (lexer_peek(lexer, 1) == '/' || lexer_peek(lexer, 1) == '*') {
                lexer_skip_comment(lexer);
                continue;
            }
        }
        
        int line = lexer->line;
        int col = lexer->column;
        char c = lexer->current_char;
        
        // String literal
        if (c == '"') {
            return read_string(lexer);
        }
        
        // Number literal
        if (isdigit((unsigned char)c)) {
            return read_number(lexer);
        }
        
        // Handle ".." range operator (but not float literals like ".5")
        if (c == '.' && lexer_peek(lexer, 1) == '.') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return token_new(TOKEN_DOTDOT, NULL, line, col);
        }
        
        if (c == '.' && isdigit((unsigned char)lexer_peek(lexer, 1))) {
            return read_number(lexer);
        }
        
        // Identifier or keyword
        if (isalpha((unsigned char)c) || c == '_') {
            return read_identifier(lexer);
        }
        
        // Two-character operators
        char c2 = lexer_peek(lexer, 1);
        if (c2 != '\0') {
            TokenType two_char = get_two_char_token(c, c2);
            if (two_char != TOKEN_ERROR) {
                lexer_advance(lexer);
                lexer_advance(lexer);
                return token_new(two_char, NULL, line, col);
            }
        }
        
        // Single-character symbols
        TokenType type = TOKEN_ERROR;
        switch (c) {
            case '(': type = TOKEN_LPAREN; break;
            case ')': type = TOKEN_RPAREN; break;
            case '{': type = TOKEN_LBRACE; break;
            case '}': type = TOKEN_RBRACE; break;
            case '[': type = TOKEN_LBRACKET; break;
            case ']': type = TOKEN_RBRACKET; break;
            case ',': type = TOKEN_COMMA; break;
            case ':': type = TOKEN_COLON; break;
            case ';': type = TOKEN_SEMICOLON; break;
            case '.': type = TOKEN_DOT; break;
            case '+': type = TOKEN_PLUS; break;
            case '-': type = TOKEN_MINUS; break;
            case '*': type = TOKEN_STAR; break;
            case '/': type = TOKEN_SLASH; break;
            case '%': type = TOKEN_PERCENT; break;
            case '=': type = TOKEN_EQ; break;
            case '<': type = TOKEN_LT; break;
            case '>': type = TOKEN_GT; break;
            case '&': type = TOKEN_AND; break;
            case '|': type = TOKEN_OR; break;
            case '!': type = TOKEN_NOT; break;
            default:
                return token_new(TOKEN_ERROR, 
                                 (char[2]){c, '\0'}, line, col);
        }
        
        lexer_advance(lexer);
        return token_new(type, NULL, line, col);
    }
    
    return token_new(TOKEN_EOF, NULL, lexer->line, lexer->column);
}

// ============================================================
// Tokenize entire source
// ============================================================
Token** lexer_tokenize(const char* source, int* token_count, Error* error) {
    *token_count = 0;
    clear_error(error);
    
    Lexer* lexer = lexer_new(source);
    if (!lexer) {
        set_error(error, "Failed to create lexer", 0, 0);
        return NULL;
    }
    
    int capacity = 128;
    Token** tokens = calloc(capacity, sizeof(Token*));
    Token* token;
    
    while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
        if (*token_count >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, capacity * sizeof(Token*));
        }
        tokens[(*token_count)++] = token;
        
        if (token->type == TOKEN_ERROR) {
            set_error(error, token->value, token->line, token->column);
            lexer_free(lexer);
            return NULL;
        }
    }
    free(token);
    
    if (*token_count >= capacity) {
        tokens = realloc(tokens, (capacity + 1) * sizeof(Token*));
    }
    tokens[*token_count] = token_new(TOKEN_EOF, NULL, lexer->line, lexer->column);
    
    lexer_free(lexer);
    return tokens;
}

// ============================================================
// Lexer lifecycle
// ============================================================
Lexer* lexer_new(const char* source) {
    Lexer* lexer = calloc(1, sizeof(Lexer));
    lexer->source = source;
    lexer->length = (int)strlen(source);
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->current_char = (lexer->length > 0) ? source[0] : '\0';
    return lexer;
}

void lexer_free(Lexer* lexer) {
    free(lexer);
}
