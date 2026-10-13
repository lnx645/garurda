// ============================================================
// GarudaLang compiler CLI
// Author: GalangTeam
// ============================================================

// Define feature macros before any libc header, otherwise strdup() and
// friends are not declared.
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/wait.h>
#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "resolver.h"

// ============================================================
// Usage
// ============================================================
static void print_usage(void) {
    fprintf(stderr, "GarudaLang Compiler (GaLang)\n");
    fprintf(stderr, "Author: GalangTeam\n\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  gar build <file.ga> [-o output]\n");
    fprintf(stderr, "  gar run   <file.ga>\n");
    fprintf(stderr, "  gar check <file.ga>\n");
    fprintf(stderr, "  gar fmt   <file.ga>\n");
    fprintf(stderr, "  gar repl\n");
}

// ============================================================
// Read file
// ============================================================
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        fprintf(stderr, "Error: Out of memory\n");
        return NULL;
    }
    
    size_t nread = fread(buffer, 1, (size_t)size, f);
    buffer[nread] = '\0';
    fclose(f);
    
    return buffer;
}

// ============================================================
// Compile pipeline: resolve imports -> type check -> codegen
//
// Shared by `build`, `check` and `run` so there is exactly one code
// path; the commands only differ in how far they take it.
// ============================================================
static int compile(const char* input, const char* output, bool stop_after_check) {
    Error error = ERROR_INIT();
    ModuleSet modules;

    // Module resolver: parses the entry file and every module it imports,
    // then flattens them into one program, dependencies first.
    ASTProgram* program = resolver_flatten(input, &modules, &error);
    if (!program) {
        fprintf(stderr, "Module Error: %s\n", error.message);
        resolver_free(&modules);
        return 1;
    }

    if (modules.verbose) {
        fprintf(stderr, "[gar] %d module(s):\n", modules.count);
        for (int i = 0; i < modules.count; i++) {
            fprintf(stderr, "        %s\n", modules.mods[i].module_name);
        }
    }

    int rc = 0;
    if (!checker_check(program, &error)) {
        fprintf(stderr, "Type Error: %s\n", error.message);
        rc = 1;
    } else if (!stop_after_check) {
        if (!codegen_generate(program, output, &error)) {
            fprintf(stderr, "Codegen Error: %s\n", error.message);
            rc = 1;
        }
    }

    ast_free((ASTNode*)program);
    resolver_free(&modules);
    return rc;
}

// ============================================================
// Build command
// ============================================================
static int cmd_build(int argc, char** argv) {
    const char* input = NULL;
    char* output = NULL;
    bool owns_output = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (!input) {
            input = argv[i];
        }
    }

    if (!input) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }

    // Default output: the input path without its .ga extension.
    if (!output) {
        size_t len = strlen(input);
        char* buf = malloc(len + 1);
        strcpy(buf, input);
        if (len > 3 && strcmp(buf + len - 3, ".ga") == 0) buf[len - 3] = '\0';
        output = buf;
        owns_output = true;
    }

    int rc = compile(input, output, false);
    if (rc == 0) printf("Built: %s\n", output);

    if (owns_output) free(output);
    return rc;
}

// ============================================================
// Run command
// ============================================================
static int cmd_run(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }

    // Build first.
    int ret = cmd_build(argc, argv);
    if (ret != 0) return ret;

    // cmd_build honours -o; without it the binary sits next to the source
    // with the .ga extension stripped.
    char* output = NULL;
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) { output = argv[i + 1]; break; }
    }

    char* owned = NULL;
    if (!output) {
        owned = strdup(argv[2]);
        size_t len = strlen(owned);
        if (len > 3 && strcmp(owned + len - 3, ".ga") == 0) owned[len - 3] = '\0';
        output = owned;
    }

    // Prefix with ./ so a bare name is not looked up in $PATH.
    char cmd[PATH_MAX];
    snprintf(cmd, sizeof(cmd), "./%s", output);

    int status = system(cmd);

    free(owned);
    if (status == -1) {
        fprintf(stderr, "Error: cannot execute '%s'\n", output);
        return 1;
    }
    // Propagate the child's exit code instead of the raw wait status.
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

// ============================================================
// Check command (resolve + type check, no output binary)
// ============================================================
static int cmd_check(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }

    int rc = compile(argv[2], NULL, true);
    if (rc == 0) printf("OK: No errors found\n");
    return rc;
}

// ============================================================
// Fmt command (basic formatting)
// ============================================================
static int cmd_fmt(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[2];
    char* source = read_file(input);
    if (!source) return 1;
    
    // TODO: implement formatter
    // For now, just print the source
    printf("%s", source);
    
    free(source);
    return 0;
}

// ============================================================
// Repl command
// ============================================================
static int cmd_repl(void) {
    printf("GarudaLang REPL (GaLang)\n");
    printf("Type .exit to quit\n\n");
    
    char line[4096];
    
    while (true) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        if (strcmp(line, ".exit") == 0) {
            break;
        }
        
        if (strlen(line) == 0) {
            continue;
        }
        
        // Lexer
        int token_count = 0;
        Error error = ERROR_INIT();
        Token** tokens = lexer_tokenize(line, &token_count, &error);
        
        if (error.has_error) {
            fprintf(stderr, "Error: %s\n", error.message);
            continue;
        }
        
        // Parser
        ASTProgram* program = parser_parse(tokens, token_count, &error);
        
        for (int i = 0; i < token_count; i++) {
            token_free(tokens[i]);
        }
        free(tokens);
        
        if (error.has_error) {
            fprintf(stderr, "Error: %s\n", error.message);
            continue;
        }
        
        // Type check
        if (!checker_check(program, &error)) {
            fprintf(stderr, "Error: %s\n", error.message);
            ast_free((ASTNode*)program);
            continue;
        }
        
        // Codegen
        char output[] = "/tmp/ga_repl_out";
        if (!codegen_generate(program, output, &error)) {
            fprintf(stderr, "Error: %s\n", error.message);
            ast_free((ASTNode*)program);
            continue;
        }
        
        // Execute
        int ret = system(output);
        (void)ret;
        
        ast_free((ASTNode*)program);
    }
    
    return 0;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "build") == 0) {
        return cmd_build(argc, argv);
    }
    else if (strcmp(command, "run") == 0) {
        return cmd_run(argc, argv);
    }
    else if (strcmp(command, "check") == 0) {
        return cmd_check(argc, argv);
    }
    else if (strcmp(command, "fmt") == 0) {
        return cmd_fmt(argc, argv);
    }
    else if (strcmp(command, "repl") == 0) {
        return cmd_repl();
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage();
        return 1;
    }
}
