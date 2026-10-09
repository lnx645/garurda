// ============================================================
// Type checker implementation for GarudaLang
// Author: GalangTeam
// ============================================================

#include "checker.h"
#include "ast.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// Type environment (symbol table)
// ============================================================
static TypeEnv env;

static void env_init(void) {
    env.count = 0;
    env.capacity = 16;
    env.names = calloc(env.capacity, sizeof(char*));
    env.types = calloc(env.capacity, sizeof(char*));
}

static void env_free(void) {
    for (int i = 0; i < env.count; i++) {
        free(env.names[i]);
        free(env.types[i]);
    }
    free(env.names);
    free(env.types);
    env.count = 0;
    env.capacity = 0;
}

static void env_declare(const char* name, const char* type) {
    if (env.count >= env.capacity) {
        env.capacity *= 2;
        env.names = realloc(env.names, env.capacity * sizeof(char*));
        env.types = realloc(env.types, env.capacity * sizeof(char*));
    }
    env.names[env.count] = strdup(name);
    env.types[env.count] = strdup(type);
    env.count++;
}

static const char* env_lookup(const char* name) {
    for (int i = 0; i < env.count; i++) {
        if (strcmp(env.names[i], name) == 0) {
            return env.types[i];
        }
    }
    return NULL;
}

// ============================================================
// Check a single expression
// ============================================================
static const char* check_expr(ASTNode* node, Error* error) {
    if (!node) return "void";
    
    switch (node->type) {
        case AST_INT:
            return "int";
        
        case AST_FLOAT:
            return "float64";
        
        case AST_STRING:
            return "string";
        
        case AST_BOOL:
            return "bool";
        
        case AST_IDENT: {
            ASTIdent* ident = (ASTIdent*)node;
            const char* type = env_lookup(ident->name);
            if (!type) {
                snprintf(error->message, sizeof(error->message),
                         "Undefined variable '%s'", ident->name);
                error->has_error = true;
            }
            return type;
        }
        
        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)node;
            const char* left_type = check_expr(bin->left, error);
            const char* right_type = check_expr(bin->right, error);
            
            if (error->has_error) return NULL;
            
            // Arithmetic operators
            if (strcmp(bin->op, "+") == 0 ||
                strcmp(bin->op, "-") == 0 ||
                strcmp(bin->op, "*") == 0 ||
                strcmp(bin->op, "/") == 0 ||
                strcmp(bin->op, "%") == 0) {
                if (strcmp(left_type, right_type) == 0 &&
                    (strcmp(left_type, "int") == 0 || 
                     strcmp(left_type, "float64") == 0)) {
                    return left_type;
                }
                snprintf(error->message, sizeof(error->message),
                         "Type mismatch in binary expression '%s'", bin->op);
                error->has_error = true;
                return NULL;
            }
            
            // Comparison operators
            if (strcmp(bin->op, "==") == 0 ||
                strcmp(bin->op, "!=") == 0 ||
                strcmp(bin->op, "<") == 0 ||
                strcmp(bin->op, ">") == 0 ||
                strcmp(bin->op, "<=") == 0 ||
                strcmp(bin->op, ">=") == 0) {
                return "bool";
            }
            
            // String concatenation
            if (strcmp(bin->op, "+") == 0 && 
                strcmp(left_type, "string") == 0 &&
                strcmp(right_type, "string") == 0) {
                return "string";
            }
            
            return "unknown";
        }
        
        case AST_CALL: {
            ASTCall* call = (ASTCall*)node;
            // For now, assume builtin functions return their natural types
            if (strcmp(call->callee, "print") == 0) {
                return "void";
            }
            
            // Check arguments
            for (int i = 0; i < call->arg_count; i++) {
                check_expr(call->args[i], error);
            }
            
            return "unknown";
        }
        
        case AST_SOME: {
            ASTSome* some = (ASTSome*)node;
            return check_expr(some->value, error);
        }
        
        case AST_NONE:
            return "option";
        
        default:
            return "unknown";
    }
}

// ============================================================
// Check statements
// ============================================================
static void check_stmt(ASTNode* node, Error* error) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM: {
            ASTProgram* prog = (ASTProgram*)node;
            for (int i = 0; i < prog->statement_count; i++) {
                check_stmt(prog->statements[i], error);
                if (error->has_error) return;
            }
            break;
        }
        
        case AST_FUNCTION: {
            ASTFunction* fn = (ASTFunction*)node;
            
            // Declare parameters
            for (int i = 0; i < fn->param_count; i++) {
                env_declare(fn->params[i]->name, 
                           fn->params[i]->type ? fn->params[i]->type->name : "unknown");
            }
            
            // Check body
            if (fn->body) {
                check_stmt((ASTNode*)fn->body, error);
            }
            break;
        }
        
        case AST_LET: {
            ASTLet* let = (ASTLet*)node;
            if (!let->type) {
                snprintf(error->message, sizeof(error->message),
                         "Missing type annotation for variable '%s'", let->name);
                error->has_error = true;
                return;
            }
            
            const char* value_type = check_expr(let->value, error);
            if (error->has_error) return;
            
            // Type checking: declared type vs value type
            if (value_type && strcmp(let->type->name, value_type) != 0) {
                // Allow int -> float64 coercion
                if (strcmp(let->type->name, "float64") == 0 &&
                    strcmp(value_type, "int") == 0) {
                    // OK: int can be promoted to float64
                } else if (strcmp(let->type->name, value_type) != 0) {
                    snprintf(error->message, sizeof(error->message),
                             "Type mismatch: cannot assign %s to %s", 
                             value_type, let->type->name);
                    error->has_error = true;
                    return;
                }
            }
            
            env_declare(let->name, let->type->name);
            break;
        }
        
        case AST_CONST: {
            ASTConst* c = (ASTConst*)node;
            if (!c->type) {
                snprintf(error->message, sizeof(error->message),
                         "Missing type annotation for constant '%s'", c->name);
                error->has_error = true;
                return;
            }
            check_expr(c->value, error);
            env_declare(c->name, c->type->name);
            break;
        }
        
        case AST_RETURN: {
            ASTReturn* ret = (ASTReturn*)node;
            if (ret->value) {
                check_expr(ret->value, error);
            }
            break;
        }
        
        case AST_IF: {
            ASTIf* iff = (ASTIf*)node;
            const char* cond_type = check_expr(iff->condition, error);
            if (error->has_error) return;
            if (strcmp(cond_type, "bool") != 0) {
                snprintf(error->message, sizeof(error->message),
                         "Condition must be bool, got %s", cond_type);
                error->has_error = true;
                return;
            }
            check_stmt((ASTNode*)iff->then_branch, error);
            if (error->has_error) return;
            if (iff->else_branch) {
                check_stmt((ASTNode*)iff->else_branch, error);
            }
            break;
        }
        
        case AST_FOR: {
            ASTFor* f = (ASTFor*)node;
            check_expr(f->start, error);
            check_expr(f->end, error);
            if (error->has_error) return;
            check_stmt((ASTNode*)f->body, error);
            break;
        }
        
        case AST_WHILE: {
            ASTWhile* w = (ASTWhile*)node;
            const char* cond_type = check_expr(w->condition, error);
            if (error->has_error) return;
            if (strcmp(cond_type, "bool") != 0) {
                snprintf(error->message, sizeof(error->message),
                         "Condition must be bool, got %s", cond_type);
                error->has_error = true;
                return;
            }
            check_stmt((ASTNode*)w->body, error);
            break;
        }
        
        case AST_BLOCK: {
            ASTBlock* block = (ASTBlock*)node;
            for (int i = 0; i < block->statement_count; i++) {
                check_stmt(block->statements[i], error);
                if (error->has_error) return;
            }
            break;
        }
        
        case AST_PRINT: {
            ASTPrint* p = (ASTPrint*)node;
            for (int i = 0; i < p->arg_count; i++) {
                check_expr(p->args[i], error);
            }
            break;
        }
        
        default:
            // For other nodes, just check sub-expressions if any
            break;
    }
}

// ============================================================
// Main checker entry
// ============================================================
bool checker_check(ASTProgram* program, Error* error) {
    env_init();
    bool result = true;
    
    // Check if main exists
    bool has_main = false;
    for (int i = 0; i < program->statement_count; i++) {
        if (program->statements[i]->type == AST_FUNCTION) {
            ASTFunction* fn = (ASTFunction*)program->statements[i];
            if (strcmp(fn->name, "main") == 0) {
                has_main = true;
                break;
            }
        }
    }
    
    if (!has_main) {
        snprintf(error->message, sizeof(error->message),
                 "Missing entrypoint: fn main()");
        error->has_error = true;
        env_free();
        return false;
    }
    
    // Check all statements
    check_stmt((ASTNode*)program, error);
    if (error->has_error) {
        result = false;
    }
    
    env_free();
    return result;
}

void checker_free(void) {
    env_free();
}
