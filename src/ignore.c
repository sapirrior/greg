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

// Grows a greg_ignore_rule_t array safely. Returns 0 on success, -1 on OOM
// (leaving *rules/*capacity unchanged so the caller can fail gracefully
// instead of dereferencing a NULL pointer after a failed realloc).
static int grow_rules(greg_ignore_rule_t **rules, size_t *capacity) {
    size_t new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
    greg_ignore_rule_t *tmp = realloc(*rules, sizeof(greg_ignore_rule_t) * new_cap);
    if (!tmp) {
        return -1;
    }
    *rules = tmp;
    *capacity = new_cap;
    return 0;
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
        if (t_len == 0) {
            continue; // pattern was just "/"
        }

        if (list->count >= list->capacity) {
            if (grow_rules(&list->rules, &list->capacity) != 0) {
                fprintf(stderr, "greg: warning: out of memory loading %s, ignore rules truncated\n", filepath);
                break;
            }
        }

        char *dup = strdup(trimmed);
        if (!dup) {
            fprintf(stderr, "greg: warning: out of memory loading %s, ignore rules truncated\n", filepath);
            break;
        }

        list->rules[list->count].pattern = dup;
        list->rules[list->count].is_dir_only = is_dir_only;
        list->count++;
    }

    if (ferror(f)) {
        fprintf(stderr, "greg: warning: error reading %s\n", filepath);
    }
    fclose(f);
}

void greg_ignore_stack_push(greg_ignore_stack_t *stack, const char *dirpath) {
    if (stack->depth >= stack->capacity) {
        size_t new_cap = (stack->capacity == 0) ? 8 : (stack->capacity * 2);
        greg_ignore_list_t *tmp = realloc(stack->levels, sizeof(greg_ignore_list_t) * new_cap);
        if (!tmp) {
            // Out of memory: push an empty, harmless level rather than
            // crashing. Worst case we miss some ignore rules for this
            // subtree instead of corrupting memory.
            fprintf(stderr, "greg: warning: out of memory expanding ignore stack\n");
            if (stack->depth < stack->capacity) {
                greg_ignore_list_t *list = &stack->levels[stack->depth];
                list->rules = NULL;
                list->count = 0;
                list->capacity = 0;
                stack->depth++;
            }
            return;
        }
        stack->levels = tmp;
        stack->capacity = new_cap;
    }

    greg_ignore_list_t *list = &stack->levels[stack->depth];
    list->rules = NULL;
    list->count = 0;
    list->capacity = 0;

    // Load patterns from .gitignore
    char path_buf[4096];
    int n = snprintf(path_buf, sizeof(path_buf), "%s/.gitignore", dirpath);
    if (n > 0 && (size_t)n < sizeof(path_buf)) {
        load_ignore_file(list, path_buf);
    }

    // Load patterns from .ignore (greg specific / ripgrep compatible override files)
    n = snprintf(path_buf, sizeof(path_buf), "%s/.ignore", dirpath);
    if (n > 0 && (size_t)n < sizeof(path_buf)) {
        load_ignore_file(list, path_buf);
    }

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

    // Check defaults first (cheapest check, short-circuits before any
    // pattern matching against the (potentially large) rule stack).
    if (greg_is_default_ignored(filename, is_dir)) {
        return 1;
    }

    // Traverse active rules from the most nested level outwards, since the
    // closest .gitignore should take precedence and is also more likely to
    // produce an early-exit match for deeply nested trees.
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
