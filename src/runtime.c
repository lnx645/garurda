// ============================================================
// Runtime library for GarudaLang
// Author: GalangTeam
// ============================================================

#include "runtime.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// Memory allocation (using system malloc for now)
// ============================================================
void* ga_malloc(size_t size) {
    return malloc(size);
}

void* ga_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

void* ga_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

void ga_free(void* ptr) {
    free(ptr);
}

// ============================================================
// String operations
// ============================================================
int ga_strlen(const char* s) {
    return (int)strlen(s);
}

char* ga_strdup(const char* s) {
    size_t len = strlen(s);
    char* dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

// ============================================================
// Print functions
// ============================================================
void ga_print_string(const char* s) {
    if (!s) return;
    int len = (int)strlen(s);
    write(STDOUT_FILENO, s, len);
}

void ga_print_int(int64_t val) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)val);
    write(STDOUT_FILENO, buf, len);
}

void ga_print_float(double val) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", val);
    write(STDOUT_FILENO, buf, len);
}

void ga_print_bool(bool val) {
    const char* s = val ? "true" : "false";
    write(STDOUT_FILENO, s, strlen(s));
}

// ============================================================
// Runtime initialization
// ============================================================
void ga_runtime_init(void) {
    // TODO: initialize GC, scheduler, etc.
}

void ga_runtime_shutdown(void) {
    // TODO: shutdown GC, scheduler, etc.
}
