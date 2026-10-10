// ============================================================
// Module resolver implementation
// Author: GalangTeam
// ============================================================

#include "resolver.h"
#include "lexer.h"
#include "parser.h"
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_DEPTH 64

// ============================================================
// Small helpers
// ============================================================
static char* xstrdup(const char* s) { return s ? strdup(s) : NULL; }

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) { fclose(f); return NULL; }

    char* buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

// Directory part of a path, as a new string. "." when there is none.
static char* dirname_of(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    if (slash == path) return strdup("/");
    size_t len = (size_t)(slash - path);
    char* out = malloc(len + 1);
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

// Join a directory and a relative path.
static char* path_join(const char* dir, const char* rel) {
    if (rel[0] == '/') return strdup(rel);
    size_t dl = strlen(dir);
    if (dl > 0 && dir[dl - 1] == '/') {
        char* out = malloc(dl + strlen(rel) + 1);
        sprintf(out, "%s%s", dir, rel);
        return out;
    }
    char* out = malloc(dl + strlen(rel) + 2);
    sprintf(out, "%s/%s", dir, rel);
    return out;
}

// Canonicalise; falls back to the input when the file does not exist yet.
static char* canonicalise(const char* path) {
    char* real = realpath(path, NULL);
    if (real) return real;
    return strdup(path);
}

// "std/math.ga" -> "std/math"
static char* module_name_of(const char* path) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    if (len > 3 && strcmp(base + len - 3, ".ga") == 0) len -= 3;
    char* out = malloc(len + 1);
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

// ============================================================
// Module set
// ============================================================
static Module* set_add(ModuleSet* set) {
    if (set->count >= set->capacity) {
        set->capacity = set->capacity == 0 ? 8 : set->capacity * 2;
        set->mods = realloc(set->mods, set->capacity * sizeof(Module));
    }
    Module* m = &set->mods[set->count++];
    memset(m, 0, sizeof(*m));
    return m;
}

// Returns the existing entry for a canonical path, or NULL.
static Module* set_find(ModuleSet* set, const char* canon) {
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->mods[i].path, canon) == 0) return &set->mods[i];
    }
    return NULL;
}

// ============================================================
// Parsing one file
// ============================================================
static ASTProgram* parse_file(const char* path, Error* error) {
    char* source = read_file(path);
    if (!source) {
        snprintf(error->message, sizeof(error->message),
                 "Cannot read module '%s'", path);
        error->has_error = true;
        return NULL;
    }

    int token_count = 0;
    Token** tokens = lexer_tokenize(source, &token_count, error);
    free(source);

    if (!tokens || error->has_error) {
        if (tokens) lexer_tokens_free(tokens, token_count);
        return NULL;
    }

    ASTProgram* program = parser_parse(tokens, token_count, error);

    lexer_tokens_free(tokens, token_count);

    if (error->has_error) {
        // parser_parse() hands back a partial program on failure; it owns
        // whatever it managed to build, so release it here.
        ast_free((ASTNode*)program);
        return NULL;
    }
    return program;
}

// ============================================================
// Import graph walk
//
// Depth-first, post-order: a module is appended to the flattened
// program only after everything it imports has been appended. That
// ordering matters once real linking happens, and it costs nothing now.
// ============================================================
static bool visit(ModuleSet* set, const char* path, int depth,
                  const char* from_name,
                  ASTProgram* flat, Error* error);

static bool load_imports(ModuleSet* set, Module* mod, int depth,
                         ASTProgram* flat, Error* error) {
    char* mod_dir = dirname_of(mod->path);

    for (int i = 0; i < mod->ast->statement_count; i++) {
        ASTNode* stmt = mod->ast->statements[i];
        if (!stmt || stmt->type != AST_IMPORT) continue;

        ASTImport* imp = (ASTImport*)stmt;

        // Try the importing module's own directory, then the project root.
        char* candidate = path_join(mod_dir, imp->path);
        if (!file_exists(candidate)) {
            free(candidate);
            candidate = path_join(set->root_dir, imp->path);
        }
        if (!file_exists(candidate)) {
            snprintf(error->message, sizeof(error->message),
                     "Cannot find module '%s' imported from %s",
                     imp->path, mod->module_name);
            error->has_error = true;
            free(mod_dir);
            free(candidate);
            return false;
        }

        if (!visit(set, candidate, depth + 1, mod->module_name, flat, error)) {
            free(mod_dir);
            free(candidate);
            return false;
        }

        // Record where it landed so later phases (and error messages)
        // can point at the real file.
        char* canon = canonicalise(candidate);
        imp->resolved = xstrdup(canon);
        free(canon);
        free(candidate);
    }

    free(mod_dir);
    return true;
}

static bool visit(ModuleSet* set, const char* path, int depth,
                  const char* from_name,
                  ASTProgram* flat, Error* error) {
    if (depth > MAX_DEPTH) {
        snprintf(error->message, sizeof(error->message),
                 "Import nesting too deep (>%d) at '%s'", MAX_DEPTH, path);
        error->has_error = true;
        return false;
    }

    char* canon = canonicalise(path);

    Module* existing = set_find(set, canon);
    if (existing) {
        if (existing->visited == 2) {
            // Back edge into a module that is still being loaded.
            snprintf(error->message, sizeof(error->message),
                     "Circular import: '%s' is imported (directly or "
                     "indirectly) by itself, via '%s'",
                     existing->module_name,
                     from_name ? from_name : "the entry module");
            error->has_error = true;
            free(canon);
            return false;
        }
        // Already fully loaded. A module reachable through two paths is
        // parsed once and emitted once.
        if (set->verbose) {
            fprintf(stderr, "[gar] already loaded: %s\n", existing->module_name);
        }
        free(canon);
        return true;
    }

    if (set->verbose) fprintf(stderr, "[gar] loading: %s\n", path);

    Module* mod = set_add(set);
    mod->module_name = module_name_of(path);
    mod->path = canon;          // takes ownership
    mod->visited = 2;           // in progress

    mod->ast = parse_file(mod->path, error);
    if (!mod->ast) {
        snprintf(error->message + strlen(error->message),
                 sizeof(error->message) - strlen(error->message),
                 " (in %s)", mod->path);
        return false;
    }

    if (!load_imports(set, mod, depth, flat, error)) return false;

    mod->visited = 1;

    // Post-order: dependencies first.
    for (int i = 0; i < mod->ast->statement_count; i++) {
        ast_program_add(flat, mod->ast->statements[i]);
        mod->ast->statements[i] = NULL;   // ownership moved to `flat`
    }
    return true;
}

// ============================================================
// Public API
// ============================================================
ASTProgram* resolver_flatten(const char* entry_path, ModuleSet* set, Error* error) {
    memset(set, 0, sizeof(*set));
    clear_error(error);
    set->verbose = getenv("GAR_VERBOSE") != NULL;

    if (!file_exists(entry_path)) {
        snprintf(error->message, sizeof(error->message),
                 "Cannot open file '%s'", entry_path);
        error->has_error = true;
        return NULL;
    }

    char* entry_canon = canonicalise(entry_path);
    set->root_dir = dirname_of(entry_canon);

    ASTProgram* flat = ast_program_new();

    if (!visit(set, entry_canon, 0, NULL, flat, error)) {
        ast_free((ASTNode*)flat);
        free(entry_canon);
        resolver_free(set);
        return NULL;
    }

    free(entry_canon);
    return flat;
}

void resolver_free(ModuleSet* set) {
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        free(set->mods[i].module_name);
        free(set->mods[i].path);
        // The first slot may own the flattened program; the others own
        // whatever was not moved into it.
        ast_free((ASTNode*)set->mods[i].ast);
    }
    free(set->mods);
    free(set->root_dir);
    memset(set, 0, sizeof(*set));
}
