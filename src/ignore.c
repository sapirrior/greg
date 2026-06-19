#include "ignore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple recursive glob matcher
static int match_glob(const char *pattern, const char *str) {
    if (*pattern == '\0' && *str == '\0') return 1;
    if (*pattern == '*' && *(pattern + 1) != '\0' && *str == '\0') return 0;
    if (*pattern == '?' || *pattern == *str) return match_glob(pattern + 1, str + 1);
    if (*pattern == '*') return match_glob(pattern + 1, str) || match_glob(pattern, str + 1);
    return 0;
}

int greg_is_default_ignored(const char *name, int is_dir) {
    // Skip dotfiles/dot-directories by default, except standard dot files if needed
    if (name[0] == '.' && name[1] != '\0' && strcmp(name, "..") != 0) {
        return 1; 
    }

    if (is_dir) {
        if (strcmp(name, "node_modules") == 0 ||
            strcmp(name, "build") == 0 ||
            strcmp(name, "dist") == 0 ||
            strcmp(name, "bin") == 0 ||
            strcmp(name, "obj") == 0 ||
            strcmp(name, "target") == 0 ||
            strcmp(name, ".idea") == 0 ||
            strcmp(name, ".vscode") == 0 ||
            strcmp(name, ".settings") == 0 ||
            strncmp(name, "cmake-build-", 12) == 0 ||
            strcmp(name, "vendor") == 0) {
            return 1;
        }
    }
    return 0;
}

void greg_ignore_stack_init(greg_ignore_stack_t *stack) {
    stack->levels = NULL;
    stack->depth = 0;
    stack->capacity = 0;
}

static void free_ignore_list(greg_ignore_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->rules[i].pattern);
    }
    free(list->rules);
    list->rules = NULL;
    list->count = 0;
    list->capacity = 0;
}

void greg_ignore_stack_destroy(greg_ignore_stack_t *stack) {
    for (size_t i = 0; i < stack->depth; i++) {
        free_ignore_list(&stack->levels[i]);
    }
    free(stack->levels);
    stack->levels = NULL;
    stack->depth = 0;
    stack->capacity = 0;
}

static void load_ignore_file(greg_ignore_list_t *list, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Trim newline and leading/trailing whitespace
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }

        if (*trimmed == '\0' || *trimmed == '#') {
            continue; // Skip comments and empty lines
        }

        int is_dir_only = 0;
        size_t t_len = strlen(trimmed);
        if (t_len > 0 && trimmed[t_len - 1] == '/') {
            is_dir_only = 1;
            trimmed[--t_len] = '\0';
        }

        if (list->count >= list->capacity) {
            list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
            list->rules = realloc(list->rules, sizeof(greg_ignore_rule_t) * list->capacity);
        }

        list->rules[list->count].pattern = strdup(trimmed);
        list->rules[list->count].is_dir_only = is_dir_only;
        list->count++;
    }
    fclose(f);
}

void greg_ignore_stack_push(greg_ignore_stack_t *stack, const char *dirpath) {
    if (stack->depth >= stack->capacity) {
        stack->capacity = stack->capacity == 0 ? 8 : stack->capacity * 2;
        stack->levels = realloc(stack->levels, sizeof(greg_ignore_list_t) * stack->capacity);
    }

    greg_ignore_list_t *list = &stack->levels[stack->depth];
    list->rules = NULL;
    list->count = 0;
    list->capacity = 0;

    // Load patterns from .gitignore
    char path_buf[2048];
    snprintf(path_buf, sizeof(path_buf), "%s/.gitignore", dirpath);
    load_ignore_file(list, path_buf);

    // Load patterns from .ignore (greg specific / ripgrep compatible override files)
    snprintf(path_buf, sizeof(path_buf), "%s/.ignore", dirpath);
    load_ignore_file(list, path_buf);

    stack->depth++;
}

void greg_ignore_stack_pop(greg_ignore_stack_t *stack) {
    if (stack->depth == 0) return;
    stack->depth--;
    free_ignore_list(&stack->levels[stack->depth]);
}

int greg_ignore_stack_should_ignore(const greg_ignore_stack_t *stack, const char *path, int is_dir) {
    // Extract base name of the path (e.g. "main.o" from "src/main.o")
    const char *filename = strrchr(path, '/');
#ifdef _WIN32
    const char *win_slash = strrchr(path, '\\');
    if (!filename || (win_slash && win_slash > filename)) {
        filename = win_slash;
    }
#endif
    filename = filename ? filename + 1 : path;

    // Check defaults first
    if (greg_is_default_ignored(filename, is_dir)) {
        return 1;
    }

    // Traverse active rules from the most nested level outwards
    for (size_t d = stack->depth; d > 0; d--) {
        const greg_ignore_list_t *list = &stack->levels[d - 1];
        for (size_t i = 0; i < list->count; i++) {
            const greg_ignore_rule_t *rule = &list->rules[i];
            if (rule->is_dir_only && !is_dir) {
                continue;
            }
            // Check match against base filename or full relative path
            if (match_glob(rule->pattern, filename) || match_glob(rule->pattern, path)) {
                return 1;
            }
        }
    }

    return 0;
}
