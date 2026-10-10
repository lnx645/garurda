// ============================================================
// AST (Abstract Syntax Tree) definitions
// Author: GalangTeam
// ============================================================

#ifndef AST_H
#define AST_H

#include "common.h"

// Forward declarations
typedef struct ASTNode ASTNode;
typedef struct ASTProgram ASTProgram;
typedef struct ASTFunction ASTFunction;
typedef struct ASTLet ASTLet;
typedef struct ASTConst ASTConst;
typedef struct ASTReturn ASTReturn;
typedef struct ASTIf ASTIf;
typedef struct ASTFor ASTFor;
typedef struct ASTWhile ASTWhile;
typedef struct ASTBinary ASTBinary;
typedef struct ASTUnary ASTUnary;
typedef struct ASTCall ASTCall;
typedef struct ASTIdent ASTIdent;
typedef struct ASTInt ASTInt;
typedef struct ASTFloat ASTFloat;
typedef struct ASTString ASTString;
typedef struct ASTBool ASTBool;
typedef struct ASTBlock ASTBlock;
typedef struct ASTParam ASTParam;
typedef struct ASTType ASTType;
typedef struct ASTMatch ASTMatch;
typedef struct ASTEnum ASTEnum;
typedef struct ASTStruct ASTStruct;
typedef struct ASTImpl ASTImpl;
typedef struct ASTInterface ASTInterface;
typedef struct ASTSome ASTSome;
typedef struct ASTNone ASTNone;
typedef struct ASTPrint ASTPrint;
typedef struct ASTImport ASTImport;

// ============================================================
// AST Node (base)
// ============================================================
struct ASTNode {
    ASTNodeType type;
    TypeTag tag;      // filled in by the type checker
    int line;
    int column;
};

// ============================================================
// Program
// ============================================================
struct ASTProgram {
    ASTNode base;
    ASTNode** statements;
    int statement_count;
    int statement_capacity;
};

// ============================================================
// Function
// ============================================================
struct ASTFunction {
    ASTNode base;
    char* name;
    ASTParam** params;
    int param_count;
    int param_capacity;
    ASTType* return_type;
    ASTBlock* body;
};

// ============================================================
// Parameter
// ============================================================
struct ASTParam {
    ASTNode base;
    char* name;
    ASTType* type;
};

// ============================================================
// Type
// ============================================================
struct ASTType {
    ASTNode base;
    char* name;
    ASTType** generic_args;
    int generic_arg_count;
};

// ============================================================
// Let / Const declaration
// ============================================================
struct ASTLet {
    ASTNode base;
    char* name;
    ASTType* type;
    ASTNode* value;
};

struct ASTConst {
    ASTNode base;
    char* name;
    ASTType* type;
    ASTNode* value;
};

// ============================================================
// Return
// ============================================================
struct ASTReturn {
    ASTNode base;
    ASTNode* value;
};

// ============================================================
// If / Else
// ============================================================
struct ASTIf {
    ASTNode base;
    ASTNode* condition;
    ASTBlock* then_branch;
    ASTBlock* else_branch;  // NULL if no else
};

// ============================================================
// For loop (range-based)
// ============================================================
struct ASTFor {
    ASTNode base;
    char* var_name;
    ASTNode* start;
    ASTNode* end;
    ASTBlock* body;
};

// ============================================================
// While loop
// ============================================================
struct ASTWhile {
    ASTNode base;
    ASTNode* condition;
    ASTBlock* body;
};

// ============================================================
// Binary expression
// ============================================================
struct ASTBinary {
    ASTNode base;
    char* op;
    ASTNode* left;
    ASTNode* right;
};

// ============================================================
// Unary expression
// ============================================================
struct ASTUnary {
    ASTNode base;
    char* op;
    ASTNode* operand;
};

// ============================================================
// Function call
// ============================================================
struct ASTCall {
    ASTNode base;
    char* callee;
    ASTNode** args;
    int arg_count;
    int arg_capacity;
};

// ============================================================
// Identifier
// ============================================================
struct ASTIdent {
    ASTNode base;
    char* name;
};

// ============================================================
// Literals
// ============================================================
struct ASTInt {
    ASTNode base;
    int64_t value;
};

struct ASTFloat {
    ASTNode base;
    double value;
};

struct ASTString {
    ASTNode base;
    char* value;
};

struct ASTBool {
    ASTNode base;
    bool value;
};

// ============================================================
// Block (sequence of statements)
// ============================================================
struct ASTBlock {
    ASTNode base;
    ASTNode** statements;
    int statement_count;
    int statement_capacity;
};

// ============================================================
// Match
// ============================================================
struct ASTMatch {
    ASTNode base;
    ASTNode* subject;
    ASTNode** arms;
    int arm_count;
};

// ============================================================
// Enum
// ============================================================
struct ASTEnum {
    ASTNode base;
    char* name;
    char** variants;
    int variant_count;
};

// ============================================================
// Struct
// ============================================================
struct ASTStruct {
    ASTNode base;
    char* name;
    ASTParam** fields;
    int field_count;
};

// ============================================================
// Impl
// ============================================================
struct ASTImpl {
    ASTNode base;
    char* type_name;
    ASTFunction** methods;
    int method_count;
};

// ============================================================
// Interface
// ============================================================
struct ASTInterface {
    ASTNode base;
    char* name;
    ASTFunction** methods;
    int method_count;
};

// ============================================================
// Some / None (Option)
// ============================================================
struct ASTSome {
    ASTNode base;
    ASTNode* value;
};

struct ASTNone {
    ASTNode base;
};

// ============================================================
// Print statement
// ============================================================
struct ASTPrint {
    ASTNode base;
    ASTNode** args;
    int arg_count;
    int arg_capacity;
};

// ============================================================
// Import statement
//
// `path` is the raw text from the source, e.g. "std/net.ga". The
// resolver turns it into a filesystem path and records which module
// the statement resolved to.
// ============================================================
struct ASTImport {
    ASTNode base;
    char* path;          // as written in the source
    char* resolved;      // absolute path, filled in by the resolver
    char* alias;         // optional `as` name; NULL when absent
};

// ============================================================
// Constructors / helpers
// ============================================================
ASTProgram* ast_program_new(void);
void ast_program_add(ASTProgram* prog, ASTNode* stmt);

ASTBlock* ast_block_new(void);
void ast_block_add(ASTBlock* block, ASTNode* stmt);

ASTType* ast_type_new(const char* name);
ASTParam* ast_param_new(const char* name, const char* type_name);

void ast_free(ASTNode* node);

#endif // AST_H
