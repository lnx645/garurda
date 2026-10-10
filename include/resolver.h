// ============================================================
// Module resolver
//
// Walks the import graph starting from an entry file, parses every
// reachable module exactly once, and produces a single flattened
// ASTProgram with dependencies emitted before their dependents.
//
// Imports are resolved relative to the importing file's directory
// first, then relative to the directory of the entry file, so
// `import "math.ga"` works for sibling modules while
// `import "std/math.ga"` works from anywhere in the project.
//
// Author: GalangTeam
// ============================================================

#ifndef RESOLVER_H
#define RESOLVER_H

#include "common.h"
#include "ast.h"

typedef struct {
    char* module_name;    // "std/math", derived from the path
    char* path;           // absolute, canonicalised path
    ASTProgram* ast;      // owns the AST
    int visited;          // 0 = new, 1 = loaded, 2 = in progress
} Module;

typedef struct {
    Module* mods;
    int count;
    int capacity;
    char* root_dir;       // directory of the entry file
    int verbose;
} ModuleSet;

// Loads the entry file and everything it imports. On success the
// returned program is flattened (dependencies first) and *set owns
// the per-module ASTs. Release both with:
//     ast_free((ASTNode*)program);
//     resolver_free(&set);
ASTProgram* resolver_flatten(const char* entry_path, ModuleSet* set, Error* error);

void resolver_free(ModuleSet* set);

#endif // RESOLVER_H
