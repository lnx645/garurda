// ============================================================
// Parser implementation for GarudaLang
// Recursive descent parser
// Author: GalangTeam
// ============================================================

#include "parser.h"
#include "lexer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
static ASTNode* parse_expression(Parser* parser);
static ASTNode* parse_primary(Parser* parser);
static ASTBlock* parse_block(Parser* parser);
static ASTType* parse_type(Parser* parser);

// ============================================================
// Parse type (e.g., int, string)
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
    
    // fn keyword already consumed by caller
    // Function name
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
        
        parser_consume(parser, TOKEN_COLON, "Expected ':' after parameter name", 
                      &parser->error);
        
        ASTType* param_type = parse_type(parser);
        fn->params[fn->param_count++] = ast_param_new(param_name, 
                                                        param_type ? param_type->name : NULL);
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
    
    // Token already consumed by caller
    
    // Variable name
    parser_consume(parser, TOKEN_IDENT, "Expected variable name", &parser->error);
    char* name = strdup(parser_previous(parser)->value);
    
    // Type annotation
    ASTType* type = NULL;
    if (parser_match(parser, TOKEN_COLON)) {
        type = parse_type(parser);
    }
    
    // Value
    parser_consume(parser, TOKEN_EQ, "Expected '=' in declaration", &parser->error);
    ASTNode* value = parse_expression(parser);
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
    
    // Condition
    iff->condition = parse_expression(parser);
    
    // Then branch
    iff->then_branch = parse_block(parser);
    
    // Else branch (optional)
    if (parser_match(parser, TOKEN_ELSE)) {
        if (parser_check(parser, TOKEN_IF)) {
            // else if
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
// Parse return statement
// ============================================================
static ASTNode* parse_return(Parser* parser) {
    ASTReturn* ret = calloc(1, sizeof(ASTReturn));
    ret->base.type = AST_RETURN;
    
    if (!parser_check(parser, TOKEN_SEMICOLON)) {
        ret->value = parse_expression(parser);
    }
    
    parser_consume(parser, TOKEN_SEMICOLON, "Expected ';'", &parser->error);
    return (ASTNode*)ret;
}

// ============================================================
// Parse for loop
// ============================================================
static ASTNode* parse_for(Parser* parser) {
    ASTFor* f = calloc(1, sizeof(ASTFor));
    f->base.type = AST_FOR;
    
    // for already consumed
    
    // Variable name
    parser_consume(parser, TOKEN_IDENT, "Expected variable name", &parser->error);
    f->var_name = strdup(parser_previous(parser)->value);
    
    // "in"
    if (!parser_match(parser, TOKEN_IDENT) || 
        strcmp(parser_previous(parser)->value, "in") != 0) {
        snprintf(parser->error.message, sizeof(parser->error.message),
                 "Expected 'in' in for loop at line %d:%d",
                 parser->error.line, parser->error.column);
        parser->error.has_error = true;
    }
    
    // Range: 0..10
    f->start = parse_expression(parser);
    parser_consume(parser, TOKEN_DOTDOT, "Expected '..' in range", &parser->error);
    f->end = parse_expression(parser);
    
    // Body
    f->body = parse_block(parser);
    
    return (ASTNode*)f;
}

// ============================================================
// Parse while loop
// ============================================================
static ASTNode* parse_while(Parser* parser) {
    ASTWhile* w = calloc(1, sizeof(ASTWhile));
    w->base.type = AST_WHILE;
    
    w->condition = parse_expression(parser);
    w->body = parse_block(parser);
    
    return (ASTNode*)w;
}

// ============================================================
// Parse statement (top-level expression)
// ============================================================
static ASTNode* parse_statement(Parser* parser) {
    if (parser_check(parser, TOKEN_FN)) {
        parser_advance(parser); // consume fn
        return parse_function(parser);
    }
    
    if (parser_check(parser, TOKEN_LET)) {
        parser_advance(parser); // consume let
        return parse_let(parser, false);
    }
    
    if (parser_check(parser, TOKEN_CONST)) {
        parser_advance(parser); // consume const
        return parse_let(parser, true);
    }
    
    if (parser_check(parser, TOKEN_IF)) {
        parser_advance(parser); // consume if
        return parse_if(parser);
    }
    
    if (parser_check(parser, TOKEN_FOR)) {
        parser_advance(parser); // consume for
        return parse_for(parser);
    }
    
    if (parser_check(parser, TOKEN_WHILE)) {
        parser_advance(parser); // consume while
        return parse_while(parser);
    }
    
    if (parser_check(parser, TOKEN_RETURN)) {
        parser_advance(parser); // consume return
        return parse_return(parser);
    }
    
    if (parser_check(parser, TOKEN_PRINT)) {
        parser_advance(parser); // consume print
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
            p->args[p->arg_count++] = parse_expression(parser);
            if (!parser_match(parser, TOKEN_COMMA)) {
                break;
            }
        }
        
        parser_consume(parser, TOKEN_RPAREN, "Expected ')' after print args", 
                      &parser->error);
        parser_consume(parser, TOKEN_SEMICOLON, "Expected ';'", &parser->error);
        
        return (ASTNode*)p;
    }
    
    // Expression statement
    return parse_expression(parser);
}

// ============================================================
// Parse expression
// ============================================================
static ASTNode* parse_expression(Parser* parser) {
    if (parser_check(parser, TOKEN_EOF) || 
        parser_check(parser, TOKEN_RBRACE) ||
        parser_check(parser, TOKEN_COMMA)) {
        return NULL;
    }
    return parse_primary(parser);
}

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
    
    // Some
    if (parser_match(parser, TOKEN_SOME)) {
        ASTSome* some = calloc(1, sizeof(ASTSome));
        some->base.type = AST_SOME;
        some->value = parse_primary(parser);
        return (ASTNode*)some;
    }
    
    // None
    if (parser_match(parser, TOKEN_NONE)) {
        ASTNone* none = calloc(1, sizeof(ASTNone));
        none->base.type = AST_NONE;
        return (ASTNode*)none;
    }
    
    // Identifier or function call
    if (parser_match(parser, TOKEN_IDENT)) {
        char* name = strdup(token->value);
        
        // Check if function call
        if (parser_match(parser, TOKEN_LPAREN)) {
            ASTCall* call = calloc(1, sizeof(ASTCall));
            call->base.type = AST_CALL;
            call->callee = name;
            call->arg_capacity = 4;
            call->args = calloc(call->arg_capacity, sizeof(ASTNode*));
            
            while (!parser_check(parser, TOKEN_RPAREN)) {
                if (call->arg_count >= call->arg_capacity) {
                    call->arg_capacity *= 2;
                    call->args = realloc(call->args, call->arg_capacity * sizeof(ASTNode*));
                }
                call->args[call->arg_count++] = parse_expression(parser);
                if (!parser_match(parser, TOKEN_COMMA)) {
                    break;
                }
            }
            
            parser_consume(parser, TOKEN_RPAREN, "Expected ')'", &parser->error);
            return (ASTNode*)call;
        }
        
        // Just identifier
        ASTIdent* ident = calloc(1, sizeof(ASTIdent));
        ident->base.type = AST_IDENT;
        ident->name = name;
        return (ASTNode*)ident;
    }
    
    // Grouped expression
    if (parser_match(parser, TOKEN_LPAREN)) {
        ASTNode* expr = parse_expression(parser);
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'", &parser->error);
        return expr;
    }
    
    // Unknown
    if (!parser_check(parser, TOKEN_EOF)) {
        snprintf(parser->error.message, sizeof(parser->error.message),
                 "Unexpected token '%s' at line %d:%d",
                 token->value, token->line, token->column);
        parser->error.has_error = true;
        parser->error.line = token->line;
        parser->error.column = token->column;
        parser_advance(parser);
    }
    return NULL;
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
