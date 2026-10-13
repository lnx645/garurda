// ============================================================
// Parser implementation for GarudaLang
// Recursive descent parser with operator precedence
// Author: GalangTeam
// ============================================================

#include "parser.h"
#include "lexer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// Builtin registry (system access)
//
// Names here are recognized as builtin calls and lowered by the
// code generator to `ga_<name>` runtime functions. The checker
// validates arity and argument types; see docs/system-access.md.
// ============================================================
static const char* const BUILTIN_NAMES[] = {
    "fs_read", "fs_write",
    "sys_env", "sys_time", "sys_sleep",
    "http_get", "http_post", "http_code", "http_serve_once",
    "smtp_connect", "smtp_auth", "smtp_mail", "smtp_close",
    "serial_open", "serial_read", "serial_write", "serial_close",
    NULL
};

static bool is_builtin_name(const char* name) {
    for (int i = 0; BUILTIN_NAMES[i]; i++) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) return true;
    }
    return false;
}

// ============================================================
// Parser helpers
// ============================================================
static Token* parser_current(Parser* parser) {
    return parser->tokens[parser->current];
}

static Token* parser_previous(Parser* parser) {
    return parser->tokens[parser->current - 1];
}

static Token* parser_advance(Parser* parser) {
    Token* token = parser_current(parser);
    if (parser->current < parser->token_count) {
        parser->current++;
    }
    return token;
}

static bool parser_check(Parser* parser, TokenType type) {
    return parser_current(parser)->type == type;
}

static bool parser_match(Parser* parser, TokenType type) {
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

static bool parser_consume(Parser* parser, TokenType type, 
                           const char* message, Error* error) {
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    Token* token = parser_current(parser);
    snprintf(error->message, sizeof(error->message),
             "%s at line %d:%d", message, token->line, token->column);
    error->has_error = true;
    error->line = token->line;
    error->column = token->column;
    return false;
}

// ============================================================
// Forward declarations
// ============================================================
static ASTNode* parse_statement(Parser* parser);
static ASTNode* parse_assignment_expr(Parser* parser);
static ASTNode* parse_logical_or(Parser* parser);
static ASTNode* parse_logical_and(Parser* parser);
static ASTNode* parse_comparison_expr(Parser* parser);
static ASTNode* parse_additive_expr(Parser* parser);
static ASTNode* parse_multiplicative_expr(Parser* parser);
static ASTNode* parse_primary(Parser* parser);
static ASTNode* parse_unary(Parser* parser);
static ASTBlock* parse_block(Parser* parser);
static ASTType* parse_type(Parser* parser);

// ============================================================
// Parse type
// ============================================================
static ASTType* parse_type(Parser* parser) {
    Token* token = parser_current(parser);
    
    if (parser_match(parser, TOKEN_INT_TYPE)) {
        return ast_type_new("int");
    }
    if (parser_match(parser, TOKEN_FLOAT64_TYPE)) {
        return ast_type_new("float64");
    }
    if (parser_match(parser, TOKEN_BOOL_TYPE)) {
        return ast_type_new("bool");
    }
    if (parser_match(parser, TOKEN_STRING_TYPE)) {
        return ast_type_new("string");
    }
    if (parser_match(parser, TOKEN_IDENT)) {
        return ast_type_new(token->value);
    }
    
    parser_consume(parser, TOKEN_IDENT, "Expected type name", &parser->error);
    return NULL;
}

// ============================================================
// Parse block { ... }
// ============================================================
static ASTBlock* parse_block(Parser* parser) {
    ASTBlock* block = ast_block_new();
    parser_consume(parser, TOKEN_LBRACE, "Expected '{'", &parser->error);
    
    while (!parser_check(parser, TOKEN_RBRACE) && 
           !parser_check(parser, TOKEN_EOF)) {
        ASTNode* stmt = parse_statement(parser);
        if (stmt) {
            ast_block_add(block, stmt);
        }
        if (parser->error.has_error) {
            return block;
        }
    }
    
    parser_consume(parser, TOKEN_RBRACE, "Expected '}'", &parser->error);
    return block;
}

// ============================================================
// Parse function declaration
// ============================================================
static ASTNode* parse_function(Parser* parser) {
    ASTFunction* fn = calloc(1, sizeof(ASTFunction));
    fn->base.type = AST_FUNCTION;
    
    // fn already consumed
    parser_consume(parser, TOKEN_IDENT, "Expected function name", &parser->error);
    fn->name = strdup(parser_previous(parser)->value);
    
    // Parameters
    parser_consume(parser, TOKEN_LPAREN, "Expected '('", &parser->error);
    
    while (!parser_check(parser, TOKEN_RPAREN)) {
        if (fn->param_count >= fn->param_capacity) {
            fn->param_capacity = fn->param_capacity == 0 ? 4 : fn->param_capacity * 2;
            fn->params = realloc(fn->params, fn->param_capacity * sizeof(ASTParam*));
        }
        
        parser_consume(parser, TOKEN_IDENT, "Expected parameter name", &parser->error);
        char* param_name = strdup(parser_previous(parser)->value);
        
        parser_consume(parser, TOKEN_COLON, "Expected ':' after parameter name", &parser->error);
        
        ASTType* param_type = parse_type(parser);
        fn->params[fn->param_count++] = ast_param_new(param_name, 
                                                        param_type ? param_type->name : NULL);
        // ast_param_new() makes its own copy of the type name.
        ast_free((ASTNode*)param_type);
        free(param_name);
        
        if (!parser_match(parser, TOKEN_COMMA)) {
            break;
        }
    }
    
    parser_consume(parser, TOKEN_RPAREN, "Expected ')'", &parser->error);
    
    // Return type
    if (parser_match(parser, TOKEN_ARROW)) {
        fn->return_type = parse_type(parser);
    } else {
        fn->return_type = ast_type_new("void");
    }
    
    // Body
    fn->body = parse_block(parser);
    
    return (ASTNode*)fn;
}

// ============================================================
// Parse let / const declaration
// ============================================================
static ASTNode* parse_let(Parser* parser, bool is_const) {
    ASTNode* node = is_const ? (ASTNode*)calloc(1, sizeof(ASTConst)) 
                             : (ASTNode*)calloc(1, sizeof(ASTLet));
    node->type = is_const ? AST_CONST : AST_LET;
    
    // Token already consumed by parse_statement
    
    parser_consume(parser, TOKEN_IDENT, "Expected variable name", &parser->error);
    char* name = strdup(parser_previous(parser)->value);
    
    ASTType* type = NULL;
    if (parser_match(parser, TOKEN_COLON)) {
        type = parse_type(parser);
    }
    
    parser_consume(parser, TOKEN_EQ, "Expected '=' in declaration", &parser->error);
    ASTNode* value = parse_assignment_expr(parser);
    parser_consume(parser, TOKEN_SEMICOLON, "Expected ';'", &parser->error);
    
    if (is_const) {
        ASTConst* c = (ASTConst*)node;
        c->name = name;
        c->type = type;
        c->value = value;
    } else {
        ASTLet* l = (ASTLet*)node;
        l->name = name;
        l->type = type;
        l->value = value;
    }
    
    return node;
}

// ============================================================
// Parse if statement
// ============================================================
static ASTNode* parse_if(Parser* parser) {
    ASTIf* iff = calloc(1, sizeof(ASTIf));
    iff->base.type = AST_IF;
    
    // if already consumed
    
    iff->condition = parse_assignment_expr(parser);
    
    iff->then_branch = parse_block(parser);
    
    if (parser_match(parser, TOKEN_ELSE)) {
        if (parser_check(parser, TOKEN_IF)) {
            parser_advance(parser);
            ASTNode* else_if = parse_if(parser);
            ASTBlock* block = ast_block_new();
            ast_block_add(block, else_if);
            iff->else_branch = block;
        } else {
            iff->else_branch = parse_block(parser);
        }
    }
    
    return (ASTNode*)iff;
}

// ============================================================
// Parse for loop
// ============================================================
static ASTNode* parse_for(Parser* parser) {
    ASTFor* f = calloc(1, sizeof(ASTFor));
    f->base.type = AST_FOR;
    
    // for already consumed
    
    parser_consume(parser, TOKEN_IDENT, "Expected variable name", &parser->error);
    f->var_name = strdup(parser_previous(parser)->value);
    
    // "in" keyword
    parser_consume(parser, TOKEN_IDENT, "Expected 'in'", &parser->error);
    
    // Range start
    f->start = parse_assignment_expr(parser);
    
    // ".." operator
    if (!parser_match(parser, TOKEN_DOTDOT)) {
        snprintf(parser->error.message, sizeof(parser->error.message),
                 "Expected '..' in for loop at line %d:%d",
                 parser->error.line, parser->error.column);
        parser->error.has_error = true;
    }
    
    // Range end
    f->end = parse_assignment_expr(parser);
    
    f->body = parse_block(parser);
    
    return (ASTNode*)f;
}

// ============================================================
// Parse while loop
// ============================================================
static ASTNode* parse_while(Parser* parser) {
    ASTWhile* w = calloc(1, sizeof(ASTWhile));
    w->base.type = AST_WHILE;
    
    w->condition = parse_assignment_expr(parser);
    w->body = parse_block(parser);
    
    return (ASTNode*)w;
}

// ============================================================
// Parse return statement
// ============================================================
static ASTNode* parse_return(Parser* parser) {
    ASTReturn* ret = calloc(1, sizeof(ASTReturn));
    ret->base.type = AST_RETURN;
    
    if (!parser_check(parser, TOKEN_SEMICOLON)) {
        ret->value = parse_assignment_expr(parser);
    }
    
    parser_consume(parser, TOKEN_SEMICOLON, "Expected ';'", &parser->error);
    return (ASTNode*)ret;
}

// ============================================================
// Logical operators
//
//   or  <  and  <  not  <  comparison  <  additive
//
// `and` and `or` are short-circuiting at runtime; the checker enforces
// bool operands so the lowering never has to reason about non-bools.
// ============================================================
static ASTNode* parse_logical_or(Parser* parser) {
    ASTNode* left = parse_logical_and(parser);
    if (!left) return NULL;

    while (parser_check(parser, TOKEN_OR)) {
        parser_advance(parser);
        ASTNode* right = parse_logical_and(parser);
        if (!right) return left;

        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup("or");
        bin->left = left;
        bin->right = right;
        left = (ASTNode*)bin;
    }
    return left;
}

static ASTNode* parse_logical_and(Parser* parser) {
    ASTNode* left = parse_comparison_expr(parser);
    if (!left) return NULL;

    while (parser_check(parser, TOKEN_AND)) {
        parser_advance(parser);
        ASTNode* right = parse_comparison_expr(parser);
        if (!right) return left;

        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup("and");
        bin->left = left;
        bin->right = right;
        left = (ASTNode*)bin;
    }
    return left;
}

static ASTNode* parse_comparison_expr(Parser* parser) {
    ASTNode* left = parse_additive_expr(parser);
    if (!left) return NULL;
    
    while (parser_check(parser, TOKEN_LT) || 
           parser_check(parser, TOKEN_GT) ||
           parser_check(parser, TOKEN_LTE) ||
           parser_check(parser, TOKEN_GTE) ||
           parser_check(parser, TOKEN_EQEQ) ||
           parser_check(parser, TOKEN_NEQ)) {
        
        TokenType op_type = parser_current(parser)->type;
        parser_advance(parser);
        
        ASTNode* right = parse_additive_expr(parser);
        if (!right) {
            snprintf(parser->error.message, sizeof(parser->error.message),
                     "Expected expression after operator");
            parser->error.has_error = true;
            return left;
        }
        
        const char* op_str = NULL;
        switch (op_type) {
            case TOKEN_LT: op_str = "<"; break;
            case TOKEN_GT: op_str = ">"; break;
            case TOKEN_LTE: op_str = "<="; break;
            case TOKEN_GTE: op_str = ">="; break;
            case TOKEN_EQEQ: op_str = "=="; break;
            case TOKEN_NEQ: op_str = "!="; break;
            default: break; /* unreachable: loop condition limits op_type */
        }
        
        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup(op_str);
        bin->left = left;
        bin->right = right;
        left = (ASTNode*)bin;
    }
    
    return left;
}

static ASTNode* parse_additive_expr(Parser* parser) {
    ASTNode* left = parse_multiplicative_expr(parser);
    if (!left) return NULL;
    
    while (parser_check(parser, TOKEN_PLUS) || parser_check(parser, TOKEN_MINUS)) {
        TokenType op_type = parser_current(parser)->type;
        parser_advance(parser);
        
        ASTNode* right = parse_multiplicative_expr(parser);
        if (!right) break;
        
        const char* op_str = op_type == TOKEN_PLUS ? "+" : "-";
        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup(op_str);
        bin->left = left;
        bin->right = right;
        left = (ASTNode*)bin;
    }
    
    return left;
}

static ASTNode* parse_multiplicative_expr(Parser* parser) {
    ASTNode* left = parse_unary(parser);
    if (!left) return NULL;
    
    while (parser_check(parser, TOKEN_STAR) || 
           parser_check(parser, TOKEN_SLASH) ||
           parser_check(parser, TOKEN_PERCENT)) {
        TokenType op_type = parser_current(parser)->type;
        parser_advance(parser);
        
        ASTNode* right = parse_unary(parser);
        if (!right) break;
        
        const char* op_str = NULL;
        switch (op_type) {
            case TOKEN_STAR: op_str = "*"; break;
            case TOKEN_SLASH: op_str = "/"; break;
            case TOKEN_PERCENT: op_str = "%"; break;
            default: break; /* unreachable: loop condition limits op_type */
        }
        
        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup(op_str);
        bin->left = left;
        bin->right = right;
        left = (ASTNode*)bin;
    }
    
    return left;
}

static ASTNode* parse_assignment_expr(Parser* parser) {
    ASTNode* left = parse_logical_or(parser);
    if (!left) return NULL;
    
    // Assignment is right-associative and sits above the logical operators,
    // so `a = b and c` parses as `a = (b and c)`.
    if (parser_check(parser, TOKEN_EQ)) {
        parser_advance(parser);
        ASTNode* right = parse_assignment_expr(parser);
        
        ASTBinary* bin = calloc(1, sizeof(ASTBinary));
        bin->base.type = AST_BINARY;
        bin->op = strdup("=");
        bin->left = left;
        bin->right = right;
        return (ASTNode*)bin;
    }
    
    return left;
}

// ============================================================
// Parse unary prefix expression: -x, +x, not x
// Binds tighter than any binary operator.
// ============================================================
static ASTNode* parse_unary(Parser* parser) {
    if (parser_check(parser, TOKEN_MINUS) || parser_check(parser, TOKEN_PLUS)) {
        bool negate = parser_check(parser, TOKEN_MINUS);
        parser_advance(parser);
        
        ASTNode* operand = parse_unary(parser);
        if (!operand || !negate) return operand;   // unary plus is a no-op
        
        ASTUnary* un = calloc(1, sizeof(ASTUnary));
        un->base.type = AST_UNARY;
        un->op = strdup("-");
        un->operand = operand;
        return (ASTNode*)un;
    }
    
    if (parser_check(parser, TOKEN_NOT)) {
        parser_advance(parser);
        
        ASTNode* operand = parse_unary(parser);
        if (!operand) return NULL;
        
        ASTUnary* un = calloc(1, sizeof(ASTUnary));
        un->base.type = AST_UNARY;
        un->op = strdup("not");
        un->operand = operand;
        return (ASTNode*)un;
    }
    
    return parse_primary(parser);
}

// ============================================================
// Parse primary expression
// ============================================================
static ASTNode* parse_primary(Parser* parser) {
    Token* token = parser_current(parser);
    
    // Integer literal
    if (parser_match(parser, TOKEN_INT)) {
        ASTInt* node = calloc(1, sizeof(ASTInt));
        node->base.type = AST_INT;
        node->value = atoll(token->value);
        return (ASTNode*)node;
    }
    
    // Float literal
    if (parser_match(parser, TOKEN_FLOAT)) {
        ASTFloat* node = calloc(1, sizeof(ASTFloat));
        node->base.type = AST_FLOAT;
        node->value = atof(token->value);
        return (ASTNode*)node;
    }
    
    // String literal
    if (parser_match(parser, TOKEN_STRING)) {
        ASTString* node = calloc(1, sizeof(ASTString));
        node->base.type = AST_STRING;
        node->value = strdup(token->value);
        return (ASTNode*)node;
    }
    
    // Boolean literal
    if (parser_match(parser, TOKEN_TRUE)) {
        ASTBool* node = calloc(1, sizeof(ASTBool));
        node->base.type = AST_BOOL;
        node->value = true;
        return (ASTNode*)node;
    }
    if (parser_match(parser, TOKEN_FALSE)) {
        ASTBool* node = calloc(1, sizeof(ASTBool));
        node->base.type = AST_BOOL;
        node->value = false;
        return (ASTNode*)node;
    }
    
    // Some/None
    if (parser_match(parser, TOKEN_SOME)) {
        ASTSome* some = calloc(1, sizeof(ASTSome));
        some->base.type = AST_SOME;
        some->value = parse_primary(parser);
        return (ASTNode*)some;
    }
    if (parser_match(parser, TOKEN_NONE)) {
        ASTNone* none = calloc(1, sizeof(ASTNone));
        none->base.type = AST_NONE;
        return (ASTNode*)none;
    }
    
    // Identifier or function call
    if (parser_match(parser, TOKEN_IDENT)) {
        char* name = strdup(token->value);
        
        if (parser_match(parser, TOKEN_LPAREN)) {
            // System builtin (fs_read, sys_time, ...): parsed as
            // AST_BUILTIN so the checker/codegen can treat it specially.
            ASTNode* call_node;
            ASTNode*** args_ptr;
            int* argc_ptr;
            int* cap_ptr;
            if (is_builtin_name(name)) {
                ASTBuiltin* b = calloc(1, sizeof(ASTBuiltin));
                b->base.type = AST_BUILTIN;
                b->name = name;
                b->arg_capacity = 4;
                b->args = calloc(b->arg_capacity, sizeof(ASTNode*));
                call_node = (ASTNode*)b;
                args_ptr = &b->args; argc_ptr = &b->arg_count; cap_ptr = &b->arg_capacity;
            } else {
                ASTCall* call = calloc(1, sizeof(ASTCall));
                call->base.type = AST_CALL;
                call->callee = name;
                call->arg_capacity = 4;
                call->args = calloc(call->arg_capacity, sizeof(ASTNode*));
                call_node = (ASTNode*)call;
                args_ptr = &call->args; argc_ptr = &call->arg_count; cap_ptr = &call->arg_capacity;
            }

            while (!parser_check(parser, TOKEN_RPAREN)) {
                if (*argc_ptr >= *cap_ptr) {
                    *cap_ptr *= 2;
                    *args_ptr = realloc(*args_ptr, (*cap_ptr) * sizeof(ASTNode*));
                }
                (*args_ptr)[(*argc_ptr)++] = parse_assignment_expr(parser);
                if (!parser_match(parser, TOKEN_COMMA)) {
                    break;
                }
            }

            parser_consume(parser, TOKEN_RPAREN, "Expected ')'", &parser->error);
            return call_node;
        }
        
        ASTIdent* ident = calloc(1, sizeof(ASTIdent));
        ident->base.type = AST_IDENT;
        ident->name = name;
        return (ASTNode*)ident;
    }
    
    // Grouped expression
    if (parser_match(parser, TOKEN_LPAREN)) {
        ASTNode* expr = parse_assignment_expr(parser);
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'", &parser->error);
        return expr;
    }
    
    // Unknown
    if (!parser_check(parser, TOKEN_EOF)) {
        snprintf(parser->error.message, sizeof(parser->error.message),
                 "Unexpected token '%s' at line %d:%d",
                 token->value, token->line, token->column);
        parser->error.has_error = true;
        parser_advance(parser);
    }
    return NULL;
}

// ============================================================
// Parse statement
// ============================================================
// ============================================================
// Parse import statement
//
//   import "std/math.ga";
//   import "std/math.ga" as math;
//
// Imports are only valid at the top level; the resolver rejects them
// anywhere else.
// ============================================================
static ASTNode* parse_import(Parser* parser) {
    parser_advance(parser);   // 'import'

    ASTImport* imp = calloc(1, sizeof(ASTImport));
    imp->base.type = AST_IMPORT;

    if (!parser_consume(parser, TOKEN_STRING, "Expected a quoted path after 'import'", &parser->error)) {
        free(imp);
        return NULL;
    }
    imp->path = strdup(parser_previous(parser)->value);

    // Optional alias.
    if (parser_check(parser, TOKEN_IDENT) &&
        strcmp(parser_current(parser)->value, "as") == 0) {
        parser_advance(parser);
        parser_consume(parser, TOKEN_IDENT, "Expected a name after 'as'", &parser->error);
        imp->alias = strdup(parser_previous(parser)->value);
    }

    parser_consume(parser, TOKEN_SEMICOLON, "Expected ';' after import", &parser->error);

    return (ASTNode*)imp;
}

// ============================================================
// Parse statement
// ============================================================
static ASTNode* parse_statement(Parser* parser) {
    if (parser_check(parser, TOKEN_IMPORT)) {
        return parse_import(parser);
    }

    if (parser_check(parser, TOKEN_FN)) {
        parser_advance(parser);
        return parse_function(parser);
    }
    
    if (parser_check(parser, TOKEN_LET)) {
        parser_advance(parser);
        return parse_let(parser, false);
    }
    
    if (parser_check(parser, TOKEN_CONST)) {
        parser_advance(parser);
        return parse_let(parser, true);
    }
    
    if (parser_check(parser, TOKEN_IF)) {
        parser_advance(parser);
        return parse_if(parser);
    }
    
    if (parser_check(parser, TOKEN_FOR)) {
        parser_advance(parser);
        return parse_for(parser);
    }
    
    if (parser_check(parser, TOKEN_WHILE)) {
        parser_advance(parser);
        return parse_while(parser);
    }
    
    if (parser_check(parser, TOKEN_RETURN)) {
        parser_advance(parser);
        return parse_return(parser);
    }
    
    if (parser_check(parser, TOKEN_PRINT)) {
        parser_advance(parser);
        parser_consume(parser, TOKEN_LPAREN, "Expected '(' after print", &parser->error);
        
        ASTPrint* p = calloc(1, sizeof(ASTPrint));
        p->base.type = AST_PRINT;
        p->arg_capacity = 4;
        p->args = calloc(p->arg_capacity, sizeof(ASTNode*));
        
        while (!parser_check(parser, TOKEN_RPAREN)) {
            if (p->arg_count >= p->arg_capacity) {
                p->arg_capacity *= 2;
                p->args = realloc(p->args, p->arg_capacity * sizeof(ASTNode*));
            }
            p->args[p->arg_count++] = parse_assignment_expr(parser);
            if (!parser_match(parser, TOKEN_COMMA)) {
                break;
            }
        }
        
        parser_consume(parser, TOKEN_RPAREN, "Expected ')' after print args", &parser->error);
        parser_consume(parser, TOKEN_SEMICOLON, "Expected ';'", &parser->error);
        
        return (ASTNode*)p;
    }
    
    // Expression statement: must be terminated by ';'.
    ASTNode* expr = parse_assignment_expr(parser);
    if (parser->error.has_error) return expr;
    parser_consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression", &parser->error);
    return expr;
}

// ============================================================
// Parse program
// ============================================================
ASTProgram* parser_parse(Token** tokens, int token_count, Error* error) {
    Parser* parser = calloc(1, sizeof(Parser));
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->current = 0;
    parser->error = ERROR_INIT();
    
    ASTProgram* program = ast_program_new();
    
    while (!parser_check(parser, TOKEN_EOF)) {
        ASTNode* stmt = parse_statement(parser);
        if (stmt) {
            ast_program_add(program, stmt);
        }
        if (parser->error.has_error) {
            *error = parser->error;
            break;
        }
    }
    
    parser_free(parser);
    return program;
}

void parser_free(Parser* parser) {
    free(parser);
}
