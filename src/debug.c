// ============================================================
// Debug: tokenize and print tokens
// Author: GalangTeam
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.ga>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    int token_count = 0;
    Error error = ERROR_INIT();
    Token** tokens = lexer_tokenize(source, &token_count, &error);
    free(source);
    
    if (error.has_error) {
        fprintf(stderr, "Lexer Error: %s\n", error.message);
        return 1;
    }
    
    const char* token_names[] = {
        "INT", "FLOAT", "STRING", "BOOL",
        "FN", "LET", "CONST", "IF", "ELSE", "FOR", "WHILE", "RETURN",
        "MATCH", "ENUM", "STRUCT", "IMPL", "INTERFACE",
        "TRUE", "FALSE", "SOME", "NONE", "OPTION", "PRINT",
        "INT_TYPE", "FLOAT64_TYPE", "BOOL_TYPE", "STRING_TYPE", "IDENT",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET",
        "COMMA", "COLON", "SEMICOLON", "DOT",
        "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
        "EQ", "EQEQ", "NEQ", "LT", "GT", "LTE", "GTE",
        "AND", "OR", "NOT", "ARROW", "FAT_ARROW", "DOTDOT", "LARROW",
        "PLUS_EQ", "MINUS_EQ", "STAR_EQ", "SLASH_EQ",
        "EOF", "ERROR"
    };
    
    for (int i = 0; i < token_count; i++) {
        Token* t = tokens[i];
        printf("%2d: %-15s %-20s [%d:%d]\n", 
               i, token_names[t->type], t->value ? t->value : "", 
               t->line, t->column);
    }
    
    for (int i = 0; i < token_count; i++) {
        token_free(tokens[i]);
    }
    free(tokens);
    
    return 0;
}
