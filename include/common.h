// ============================================================
// Common definitions for GarudaLang compiler
// Author: GalangTeam
// ============================================================

#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================
// Platform detection
// ============================================================
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_LINUX 0
    #define PLATFORM_MACOS 0
#elif defined(__linux__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 1
    #define PLATFORM_MACOS 0
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 0
    #define PLATFORM_MACOS 1
#else
    #error "Unsupported platform"
#endif

// Architecture detection
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X86_64 1
    #define ARCH_ARM64 0
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH_X86_64 0
    #define ARCH_ARM64 1
#else
    #error "Unsupported architecture"
#endif

// ============================================================
// Token types
// ============================================================
typedef enum {
    // Literals
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_BOOL,
    
    // Keywords
    TOKEN_FN,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_RETURN,
    TOKEN_MATCH,
    TOKEN_ENUM,
    TOKEN_STRUCT,
    TOKEN_IMPL,
    TOKEN_INTERFACE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_SOME,
    TOKEN_NONE,
    TOKEN_OPTION,
    TOKEN_PRINT,
    TOKEN_IMPORT,
    TOKEN_PUB,
    
    // Types
    TOKEN_INT_TYPE,
    TOKEN_FLOAT64_TYPE,
    TOKEN_BOOL_TYPE,
    TOKEN_STRING_TYPE,
    TOKEN_IDENT,
    
    // Symbols
    TOKEN_LPAREN,      // (
    TOKEN_RPAREN,      // )
    TOKEN_LBRACE,      // {
    TOKEN_RBRACE,      // }
    TOKEN_LBRACKET,    // [
    TOKEN_RBRACKET,    // ]
    TOKEN_COMMA,       // ,
    TOKEN_COLON,       // :
    TOKEN_DOUBLE_COLON,// ::
    TOKEN_SEMICOLON,   // ;
    TOKEN_DOT,         // .
    TOKEN_PLUS,        // +
    TOKEN_MINUS,       // -
    TOKEN_STAR,        // *
    TOKEN_SLASH,       // /
    TOKEN_PERCENT,     // %
    TOKEN_EQ,          // =
    TOKEN_EQEQ,        // ==
    TOKEN_NEQ,         // !=
    TOKEN_LT,          // <
    TOKEN_GT,          // >
    TOKEN_LTE,         // <=
    TOKEN_GTE,         // >=
    TOKEN_AND,         // and
    TOKEN_OR,          // or
    TOKEN_NOT,         // not
    TOKEN_ARROW,       // ->
    TOKEN_FAT_ARROW,   // =>
    TOKEN_DOTDOT,      // ..
    TOKEN_LARROW,      // <-
    TOKEN_PLUS_EQ,     // +=
    TOKEN_MINUS_EQ,    // -=
    TOKEN_STAR_EQ,     // *=
    TOKEN_SLASH_EQ,    // /=
    
    // Special
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

// ============================================================
// Token
// ============================================================
typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
} Token;

// ============================================================
// AST Node types
// ============================================================
typedef enum {
    AST_PROGRAM,
    AST_FUNCTION,
    AST_LET,
    AST_CONST,
    AST_RETURN,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_BINARY,
    AST_UNARY,
    AST_CALL,
    AST_IDENT,
    AST_INT,
    AST_FLOAT,
    AST_STRING,
    AST_BOOL,
    AST_BLOCK,
    AST_PARAM,
    AST_TYPE,
    AST_MATCH,
    AST_ENUM,
    AST_STRUCT,
    AST_IMPL,
    AST_INTERFACE,
    AST_SOME,
    AST_NONE,
    AST_OPTION_TYPE,
    AST_RESULT_TYPE,
    AST_PRINT,
    AST_IMPORT
} ASTNodeType;

// Forward declarations
struct ASTNode;
typedef struct ASTNode ASTNode;

// ============================================================
// Error handling
// ============================================================
typedef struct {
    bool has_error;
    char message[512];
    int line;
    int column;
} Error;

static inline Error ERROR_INIT(void) {
    Error e;
    e.has_error = false;
    e.message[0] = '\0';
    e.line = 0;
    e.column = 0;
    return e;
}

static inline void set_error(Error* err, const char* msg, int line, int column) {
    err->has_error = true;
    snprintf(err->message, sizeof(err->message), "%s at line %d:%d", msg, line, column);
    err->line = line;
    err->column = column;
}

static inline void clear_error(Error* err) {
    err->has_error = false;
    err->message[0] = '\0';
    err->line = 0;
    err->column = 0;
}

#endif // COMMON_H
