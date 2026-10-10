// ============================================================
// AST implementation
// Author: GalangTeam
// ============================================================

#include "ast.h"
#include <assert.h>

// ============================================================
// Program
// ============================================================
ASTProgram* ast_program_new(void) {
    ASTProgram* prog = calloc(1, sizeof(ASTProgram));
    prog->base.type = AST_PROGRAM;
    prog->statement_capacity = 16;
    prog->statements = calloc(prog->statement_capacity, sizeof(ASTNode*));
    return prog;
}

void ast_program_add(ASTProgram* prog, ASTNode* stmt) {
    if (prog->statement_count >= prog->statement_capacity) {
        prog->statement_capacity *= 2;
        prog->statements = realloc(prog->statements, 
                                   prog->statement_capacity * sizeof(ASTNode*));
    }
    prog->statements[prog->statement_count++] = stmt;
}

// ============================================================
// Block
// ============================================================
ASTBlock* ast_block_new(void) {
    ASTBlock* block = calloc(1, sizeof(ASTBlock));
    block->base.type = AST_BLOCK;
    block->statement_capacity = 16;
    block->statements = calloc(block->statement_capacity, sizeof(ASTNode*));
    return block;
}

void ast_block_add(ASTBlock* block, ASTNode* stmt) {
    if (block->statement_count >= block->statement_capacity) {
        block->statement_capacity *= 2;
        block->statements = realloc(block->statements,
                                    block->statement_capacity * sizeof(ASTNode*));
    }
    block->statements[block->statement_count++] = stmt;
}

// ============================================================
// Type
// ============================================================
ASTType* ast_type_new(const char* name) {
    ASTType* type = calloc(1, sizeof(ASTType));
    type->base.type = AST_TYPE;
    type->name = strdup(name);
    return type;
}

// ============================================================
// Parameter
// ============================================================
ASTParam* ast_param_new(const char* name, const char* type_name) {
    ASTParam* param = calloc(1, sizeof(ASTParam));
    param->base.type = AST_PARAM;
    param->name = strdup(name);
    if (type_name) {
        param->type = ast_type_new(type_name);
    }
    return param;
}

// ============================================================
// Free helpers
// ============================================================
void ast_free(ASTNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM: {
            ASTProgram* prog = (ASTProgram*)node;
            for (int i = 0; i < prog->statement_count; i++) {
                ast_free(prog->statements[i]);
            }
            free(prog->statements);
            free(prog);
            break;
        }
        case AST_BLOCK: {
            ASTBlock* block = (ASTBlock*)node;
            for (int i = 0; i < block->statement_count; i++) {
                ast_free(block->statements[i]);
            }
            free(block->statements);
            free(block);
            break;
        }
        case AST_FUNCTION: {
            ASTFunction* fn = (ASTFunction*)node;
            free(fn->name);
            for (int i = 0; i < fn->param_count; i++) {
                ast_free((ASTNode*)fn->params[i]);
            }
            free(fn->params);
            ast_free((ASTNode*)fn->return_type);
            ast_free((ASTNode*)fn->body);
            free(fn);
            break;
        }
        case AST_LET: {
            ASTLet* let = (ASTLet*)node;
            free(let->name);
            ast_free((ASTNode*)let->type);
            ast_free(let->value);
            free(let);
            break;
        }
        case AST_CONST: {
            ASTConst* c = (ASTConst*)node;
            free(c->name);
            ast_free((ASTNode*)c->type);
            ast_free(c->value);
            free(c);
            break;
        }
        case AST_RETURN: {
            ASTReturn* ret = (ASTReturn*)node;
            ast_free(ret->value);
            free(ret);
            break;
        }
        case AST_IF: {
            ASTIf* iff = (ASTIf*)node;
            ast_free(iff->condition);
            ast_free((ASTNode*)iff->then_branch);
            ast_free((ASTNode*)iff->else_branch);
            free(iff);
            break;
        }
        case AST_FOR: {
            ASTFor* f = (ASTFor*)node;
            free(f->var_name);
            ast_free(f->start);
            ast_free(f->end);
            ast_free((ASTNode*)f->body);
            free(f);
            break;
        }
        case AST_WHILE: {
            ASTWhile* w = (ASTWhile*)node;
            ast_free(w->condition);
            ast_free((ASTNode*)w->body);
            free(w);
            break;
        }
        case AST_BINARY: {
            ASTBinary* bin = (ASTBinary*)node;
            free(bin->op);
            ast_free(bin->left);
            ast_free(bin->right);
            free(bin);
            break;
        }
        case AST_UNARY: {
            ASTUnary* un = (ASTUnary*)node;
            free(un->op);
            ast_free(un->operand);
            free(un);
            break;
        }
        case AST_CALL: {
            ASTCall* call = (ASTCall*)node;
            free(call->callee);
            for (int i = 0; i < call->arg_count; i++) {
                ast_free(call->args[i]);
            }
            free(call->args);
            free(call);
            break;
        }
        case AST_IDENT: {
            ASTIdent* ident = (ASTIdent*)node;
            free(ident->name);
            free(ident);
            break;
        }
        case AST_INT: {
            free(node);
            break;
        }
        case AST_FLOAT: {
            free(node);
            break;
        }
        case AST_STRING: {
            ASTString* str = (ASTString*)node;
            free(str->value);
            free(str);
            break;
        }
        case AST_BOOL: {
            free(node);
            break;
        }
        case AST_TYPE: {
            ASTType* t = (ASTType*)node;
            free(t->name);
            free(t->generic_args);
            free(t);
            break;
        }
        case AST_PARAM: {
            ASTParam* p = (ASTParam*)node;
            free(p->name);
            ast_free((ASTNode*)p->type);
            free(p);
            break;
        }
        case AST_MATCH: {
            ASTMatch* m = (ASTMatch*)node;
            ast_free(m->subject);
            for (int i = 0; i < m->arm_count; i++) {
                ast_free(m->arms[i]);
            }
            free(m->arms);
            free(m);
            break;
        }
        case AST_ENUM: {
            ASTEnum* e = (ASTEnum*)node;
            free(e->name);
            for (int i = 0; i < e->variant_count; i++) {
                free(e->variants[i]);
            }
            free(e->variants);
            free(e);
            break;
        }
        case AST_STRUCT: {
            ASTStruct* s = (ASTStruct*)node;
            free(s->name);
            for (int i = 0; i < s->field_count; i++) {
                ast_free((ASTNode*)s->fields[i]);
            }
            free(s->fields);
            free(s);
            break;
        }
        case AST_IMPL: {
            ASTImpl* impl = (ASTImpl*)node;
            free(impl->type_name);
            for (int i = 0; i < impl->method_count; i++) {
                ast_free((ASTNode*)impl->methods[i]);
            }
            free(impl->methods);
            free(impl);
            break;
        }
        case AST_INTERFACE: {
            ASTInterface* iface = (ASTInterface*)node;
            free(iface->name);
            for (int i = 0; i < iface->method_count; i++) {
                ast_free((ASTNode*)iface->methods[i]);
            }
            free(iface->methods);
            free(iface);
            break;
        }
        case AST_SOME: {
            ASTSome* some = (ASTSome*)node;
            ast_free(some->value);
            free(some);
            break;
        }
        case AST_NONE: {
            free(node);
            break;
        }
        case AST_PRINT: {
            ASTPrint* p = (ASTPrint*)node;
            for (int i = 0; i < p->arg_count; i++) {
                ast_free(p->args[i]);
            }
            free(p->args);
            free(p);
            break;
        }
        case AST_IMPORT: {
            ASTImport* imp = (ASTImport*)node;
            free(imp->path);
            free(imp->resolved);
            free(imp->alias);
            free(imp);
            break;
        }
        default:
            assert(0 && "Unknown AST node type");
    }
}
