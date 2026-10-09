// ============================================================
// GarudaLang compiler CLI
// Author: GalangTeam
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

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
    
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    return buffer;
}

// ============================================================
// Build command
// ============================================================
static int cmd_build(int argc, char** argv) {
    const char* input = NULL;
    const char* output = NULL;
    
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
    
    // Determine output name
    if (!output) {
        size_t len = strlen(input);
        output = malloc(len + 1);
        strcpy((char*)output, input);
        // Remove .ga extension
        if (len > 3 && strcmp(&((char*)output)[len - 3], ".ga") == 0) {
            ((char*)output)[len - 3] = '\0';
        }
    }
    
    // Read source
    char* source = read_file(input);
    if (!source) return 1;
    
    // Lexer
    int token_count = 0;
    Error error = ERROR_INIT();
    Token** tokens = lexer_tokenize(source, &token_count, &error);
    free(source);
    
    if (error.has_error) {
        fprintf(stderr, "Lexer Error: %s\n", error.message);
        return 1;
    }
    
    // Parser
    ASTProgram* program = parser_parse(tokens, token_count, &error);
    
    // Free tokens
    for (int i = 0; i < token_count; i++) {
        token_free(tokens[i]);
    }
    free(tokens);
    
    if (error.has_error) {
        fprintf(stderr, "Parse Error: %s\n", error.message);
        return 1;
    }
    
    // Type checker
    if (!checker_check(program, &error)) {
        fprintf(stderr, "Type Error: %s\n", error.message);
        ast_free((ASTNode*)program);
        return 1;
    }
    
    // Codegen
    if (!codegen_generate(program, output, &error)) {
        fprintf(stderr, "Codegen Error: %s\n", error.message);
        ast_free((ASTNode*)program);
        return 1;
    }
    
    printf("Built: %s\n", output);
    
    ast_free((ASTNode*)program);
    return 0;
}

// ============================================================
// Run command
// ============================================================
static int cmd_run(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[2];
    
    // Build first
    int ret = cmd_build(argc, argv);
    if (ret != 0) return ret;
    
    // Determine output name
    char* output = strdup(input);
    size_t len = strlen(output);
    if (len > 3 && strcmp(&output[len - 3], ".ga") == 0) {
        output[len - 3] = '\0';
    }
    
    // Execute
    ret = system(output);
    
    free(output);
    return ret;
}

// ============================================================
// Check command (type check only)
// ============================================================
static int cmd_check(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[2];
    char* source = read_file(input);
    if (!source) return 1;
    
    // Lexer
    int token_count = 0;
    Error error = ERROR_INIT();
    Token** tokens = lexer_tokenize(source, &token_count, &error);
    free(source);
    
    if (error.has_error) {
        fprintf(stderr, "Lexer Error: %s\n", error.message);
        return 1;
    }
    
    // Parser
    ASTProgram* program = parser_parse(tokens, token_count, &error);
    
    for (int i = 0; i < token_count; i++) {
        token_free(tokens[i]);
    }
    free(tokens);
    
    if (error.has_error) {
        fprintf(stderr, "Parse Error: %s\n", error.message);
        return 1;
    }
    
    // Type checker
    if (!checker_check(program, &error)) {
        fprintf(stderr, "Type Error: %s\n", error.message);
        ast_free((ASTNode*)program);
        return 1;
    }
    
    printf("OK: No errors found\n");
    ast_free((ASTNode*)program);
    return 0;
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
