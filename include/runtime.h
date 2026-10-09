// ============================================================
// Runtime library for GarudaLang
// Author: GalangTeam
// ============================================================

#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Memory allocation
// ============================================================
void* ga_malloc(size_t size);
void* ga_calloc(size_t nmemb, size_t size);
void* ga_realloc(void* ptr, size_t size);
void ga_free(void* ptr);

// ============================================================
// String operations
// ============================================================
int ga_strlen(const char* s);
char* ga_strdup(const char* s);

// ============================================================
// Print (builtin)
// ============================================================
void ga_print_string(const char* s);
void ga_print_int(int64_t val);
void ga_print_float(double val);
void ga_print_bool(bool val);

// ============================================================
// Runtime initialization
// ============================================================
void ga_runtime_init(void);
void ga_runtime_shutdown(void);

#endif // RUNTIME_H
