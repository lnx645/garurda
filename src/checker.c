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

static void pool_free(void);   // defined below, alongside the type arena

static void env_init(void) {
    env.count = 0;
    env.capacity = 16;
    env.names = calloc(env.capacity, sizeof(char*));
    env.types = calloc(env.capacity, sizeof(char*));
}

static void env_free(void) {
    for (int i = 0; i < env.count; i++) {
        free(env.names[i]);
    }
    free(env.names);
    free(env.types);
    pool_free();
    env.count = 0;
    env.capacity = 0;
}

// Type name interning. Type strings must outlive the scope they were
// declared in: check_expr() returns a const char* that callers keep using
// after deeper scopes have been rewound. So they live in their own arena
// that is only released when the whole environment is freed.
typedef struct {
    char** items;
    int count;
    int capacity;
} TypePool;

static TypePool pool;

static const char* type_intern(const char* s) {
    if (!s) s = "unknown";
    for (int i = 0; i < pool.count; i++) {
        if (strcmp(pool.items[i], s) == 0) return pool.items[i];
    }
    if (pool.count >= pool.capacity) {
        pool.capacity = pool.capacity == 0 ? 16 : pool.capacity * 2;
        pool.items = realloc(pool.items, pool.capacity * sizeof(char*));
    }
    pool.items[pool.count] = strdup(s);
    return pool.items[pool.count++];
}

static void pool_free(void) {
    for (int i = 0; i < pool.count; i++) free(pool.items[i]);
    free(pool.items);
    pool.items = NULL;
    pool.count = 0;
    pool.capacity = 0;
}

static void env_declare(const char* name, const char* type) {
    if (env.count >= env.capacity) {
        env.capacity *= 2;
        env.names = realloc(env.names, env.capacity * sizeof(char*));
        env.types = realloc(env.types, env.capacity * sizeof(char*));
    }
    env.names[env.count] = strdup(name);
    // Store the interned pointer directly: env.types[] owns no memory,
    // the pool does. This keeps env_lookup() results valid after rewinds.
    env.types[env.count] = (char*)type_intern(type);
    env.count++;
}

static const char* env_lookup(const char* name) {
    for (int i = env.count - 1; i >= 0; i--) {
        if (strcmp(env.names[i], name) == 0) {
            return env.types[i];
        }
    }
    return NULL;
}

// Mark the current scope depth, so it can be rewound when the scope ends.
static int env_mark(void) {
    return env.count;
}

static void env_rewind(int mark) {
    while (env.count > mark) {
        env.count--;
        free(env.names[env.count]);
    }
}

// ============================================================
// Function signature registry
//
// check_expr() needs to know a function's return type to type-check calls
// like `n * factorial(n - 1)`. Signatures are collected up-front from the
// whole program, so order of definitions does not matter.
// ============================================================
typedef struct {
    char* name;
    char* return_type;
    int arity;
} FnSig;

static FnSig* sigs = NULL;
static int sig_count = 0;
static int sig_capacity = 0;

static const FnSig* sig_lookup(const char* name);

static void sigs_free(void) {
    for (int i = 0; i < sig_count; i++) {
        free(sigs[i].name);
        free(sigs[i].return_type);
    }
    free(sigs);
    sigs = NULL;
    sig_count = 0;
    sig_capacity = 0;
}

// Adds one signature. Returns false if the name is already taken.
static bool sigs_collect(ASTNode* node) {
    if (!node || node->type != AST_FUNCTION) return true;
    ASTFunction* fn = (ASTFunction*)node;

    // The flattened program can pull in the same module twice (diamond
    // import) or two modules that happen to define the same name. Either
    // way there is only one symbol-table slot for it, so catch it here
    // instead of letting sig_lookup() silently return the first match.
    if (sig_lookup(fn->name)) return false;

    if (sig_count >= sig_capacity) {
        sig_capacity = sig_capacity == 0 ? 16 : sig_capacity * 2;
        sigs = realloc(sigs, sig_capacity * sizeof(FnSig));
    }
    sigs[sig_count].name = strdup(fn->name);
    sigs[sig_count].return_type =
        strdup(fn->return_type ? fn->return_type->name : "void");
    sigs[sig_count].arity = fn->param_count;
    sig_count++;
    return true;
}

static const FnSig* sig_lookup(const char* name) {
    for (int i = 0; i < sig_count; i++) {
        if (strcmp(sigs[i].name, name) == 0) return &sigs[i];
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
                // Ordered comparisons (</>/<=/>=) require numbers.
                // Equality (==/!=) also allows strings and bools.
                int ordered = (bin->op[0] != '=' && bin->op[0] != '!');
                int ok;
                if (ordered) {
                    ok = (strcmp(left_type, "int") == 0 ||
                          strcmp(left_type, "float64") == 0) &&
                         strcmp(left_type, right_type) == 0;
                } else {
                    ok = strcmp(left_type, right_type) == 0;
                }
                if (!ok) {
                    snprintf(error->message, sizeof(error->message),
                             "Cannot compare %s with %s using '%s'",
                             left_type, right_type, bin->op);
                    error->has_error = true;
                    return NULL;
                }
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
            if (strcmp(call->callee, "print") == 0) return "void";

            // Check arguments first, then resolve the callee's signature.
            for (int i = 0; i < call->arg_count; i++) {
                check_expr(call->args[i], error);
                if (error->has_error) return NULL;
            }

            const FnSig* sig = sig_lookup(call->callee);
            if (!sig) {
                snprintf(error->message, sizeof(error->message),
                         "Call to undefined function '%s'", call->callee);
                error->has_error = true;
                return NULL;
            }
            if (call->arg_count != sig->arity) {
                snprintf(error->message, sizeof(error->message),
                         "'%s' expects %d argument(s), got %d",
                         call->callee, sig->arity, call->arg_count);
                error->has_error = true;
                return NULL;
            }
            return type_intern(sig->return_type);
        }
        
        case AST_UNARY: {
            ASTUnary* un = (ASTUnary*)node;
            const char* t = check_expr(un->operand, error);
            if (error->has_error) return NULL;
            if (strcmp(un->op, "not") == 0) {
                if (strcmp(t, "bool") != 0) {
                    snprintf(error->message, sizeof(error->message),
                             "'not' requires bool, got %s", t);
                    error->has_error = true;
                    return NULL;
                }
                return "bool";
            }
            // '-' and '+' require numbers
            if (strcmp(t, "int") != 0 && strcmp(t, "float64") != 0) {
                snprintf(error->message, sizeof(error->message),
                         "Unary '%s' requires a number, got %s", un->op, t);
                error->has_error = true;
                return NULL;
            }
            return t;
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
            int mark = env_mark();
            
            // Declare parameters
            for (int i = 0; i < fn->param_count; i++) {
                env_declare(fn->params[i]->name, 
                           fn->params[i]->type ? fn->params[i]->type->name : "unknown");
            }
            
            // Check body
            if (fn->body) {
                check_stmt((ASTNode*)fn->body, error);
            }
            // Function locals must not leak into the next function
            env_rewind(mark);
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
            int mark = env_mark();
            
            // The loop variable is implicitly declared, scoped to the loop.
            // Its type is always int: both bounds must be integers.
            const char* start_type = check_expr(f->start, error);
            if (error->has_error) return;
            const char* end_type = check_expr(f->end, error);
            if (error->has_error) return;
            
            if (strcmp(start_type, "int") != 0) {
                snprintf(error->message, sizeof(error->message),
                         "for loop start must be int, got %s", start_type);
                error->has_error = true;
                return;
            }
            if (strcmp(end_type, "int") != 0) {
                snprintf(error->message, sizeof(error->message),
                         "for loop end must be int, got %s", end_type);
                error->has_error = true;
                return;
            }
            
            env_declare(f->var_name, "int");
            check_stmt((ASTNode*)f->body, error);
            env_rewind(mark);
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
            int mark = env_mark();
            for (int i = 0; i < block->statement_count; i++) {
                check_stmt(block->statements[i], error);
                if (error->has_error) { env_rewind(mark); return; }
            }
            // Block-scoped: locals declared here are gone after the block
            env_rewind(mark);
            break;
        }
        
        case AST_PRINT: {
            ASTPrint* p = (ASTPrint*)node;
            for (int i = 0; i < p->arg_count; i++) {
                check_expr(p->args[i], error);
            }
            break;
        }
        
        case AST_IMPORT:
            // Resolved by the resolver; nothing left to check here.
            break;

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

    // Collect every function signature before checking any body, so calls
    // can be resolved regardless of declaration order.
    for (int i = 0; i < program->statement_count; i++) {
        if (!sigs_collect(program->statements[i])) {
            ASTNode* dup = program->statements[i];
            snprintf(error->message, sizeof(error->message),
                     "Duplicate definition of '%s' "
                     "(two modules export the same name)",
                     ((ASTFunction*)dup)->name);
            error->has_error = true;
            env_free();
            sigs_free();
            return false;
        }
    }

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
        sigs_free();
        return false;
    }
    
    // Check all statements
    check_stmt((ASTNode*)program, error);
    if (error->has_error) {
        result = false;
    }
    
    env_free();
    sigs_free();
    return result;
}

void checker_free(void) {
    env_free();
}
