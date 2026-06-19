#include "ignore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple recursive glob matcher supporting * and ** wildcards
static int match_glob(const char *pattern, const char *str) {
    if (*pattern == '\0' && *str == '\0') return 1;
    if (*pattern == '*' && *(pattern + 1) != '\0' && *str == '\0') return 0;
    
    // ** matches zero or more path components (including slashes)
    if (pattern[0] == '*' && pattern[1] == '*') {
        const char *p = pattern + 2;
        while (*p == '*') p++;
        if (*p == '\0') return 1; // ** at the end matches everything
        while (*str) {
            if (match_glob(p, str)) return 1;
            str++;
        }
        return 0;
    }
    
    if (*pattern == '*') {
        return match_glob(pattern + 1, str) || (*str != '\0' && match_glob(pattern, str + 1));
    }
    if (*pattern == '?' || *pattern == *str) {
        return (*str != '\0') && match_glob(pattern + 1, str + 1);
    }
    return 0;
}

int greg_is_default_ignored(const char *name, int is_dir, const greg_options_t *opts) {
    if (opts && opts->no_ignore) {
        return 0;
    }

    // Skip dotfiles/dot-directories by default, except standard dot files if needed
    if (name[0] == '.' && name[1] != '\0' && strcmp(name, "..") != 0) {
        if (!opts || !opts->hidden) {
            return 1;
        }
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

// Grows a greg_ignore_rule_t array safely. Returns 0 on success, -1 on OOM.
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

        int is_negation = 0;
        if (*trimmed == '!') {
            is_negation = 1;
            trimmed++;
            while (*trimmed == ' ' || *trimmed == '\t') {
                trimmed++;
            }
        }

        int is_anchored = 0;
        if (*trimmed == '/') {
            is_anchored = 1;
            trimmed++;
        }

        int is_dir_only = 0;
        size_t t_len = strlen(trimmed);
        if (t_len > 0 && trimmed[t_len - 1] == '/') {
            is_dir_only = 1;
            trimmed[--t_len] = '\0';
        }
        if (t_len == 0) {
            continue; // pattern was just "/" or "!/"
        }

        int is_glob = (strchr(trimmed, '*') != NULL || strchr(trimmed, '?') != NULL);

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
        list->rules[list->count].is_negation = is_negation;
        list->rules[list->count].is_anchored = is_anchored;
        list->rules[list->count].is_glob = is_glob;
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

int greg_ignore_stack_should_ignore(const greg_ignore_stack_t *stack, const char *path, int is_dir, const greg_options_t *opts) {
    if (opts && opts->no_ignore) {
        return 0;
    }

    // Extract base name of the path (e.g. "main.o" from "src/main.o")
    const char *filename = strrchr(path, '/');
#ifdef _WIN32
    const char *win_slash = strrchr(path, '\\');
    if (!filename || (win_slash && win_slash > filename)) {
        filename = win_slash;
    }
#endif
    filename = filename ? filename + 1 : path;

    // Check defaults first (cheapest check, short-circuits)
    if (greg_is_default_ignored(filename, is_dir, opts)) {
        return 1;
    }

    // Traverse active rules from the most nested level outwards.
    // If a level matches, its decision overrides outer levels.
    for (size_t d = stack->depth; d > 0; d--) {
        const greg_ignore_list_t *list = &stack->levels[d - 1];
        if (list->count == 0) {
            continue;
        }
        int has_match = 0;
        int decision = 0;
        for (size_t i = 0; i < list->count; i++) {
            const greg_ignore_rule_t *rule = &list->rules[i];
            if (rule->is_dir_only && !is_dir) {
                continue;
            }
            int rule_matched = 0;
            if (rule->is_glob) {
                if (rule->is_anchored) {
                    rule_matched = match_glob(rule->pattern, path);
                } else {
                    rule_matched = match_glob(rule->pattern, filename) || match_glob(rule->pattern, path);
                }
            } else {
                if (rule->is_anchored) {
                    rule_matched = (strcmp(rule->pattern, path) == 0);
                } else {
                    rule_matched = (strcmp(rule->pattern, filename) == 0) || (strcmp(rule->pattern, path) == 0);
                }
            }

            if (rule_matched) {
                decision = rule->is_negation ? 0 : 1;
                has_match = 1;
            }
        }
        if (has_match) {
            return decision;
        }
    }

    return 0;
}

#ifdef _WIN32
    #include <windows.h>
    #define ATOMIC_INC(x) InterlockedIncrement(&(x))
    #define ATOMIC_DEC(x) InterlockedDecrement(&(x))
#else
    #define ATOMIC_INC(x) __sync_add_and_fetch(&(x), 1)
    #define ATOMIC_DEC(x) __sync_sub_and_fetch(&(x), 1)
#endif

greg_ignore_node_t *greg_ignore_node_create(greg_ignore_node_t *parent, const char *dirpath) {
    greg_ignore_node_t *node = malloc(sizeof(greg_ignore_node_t));
    if (!node) return NULL;
    node->list.rules = NULL;
    node->list.count = 0;
    node->list.capacity = 0;
    node->parent = parent;
    node->ref_count = 1;

    if (parent) {
        greg_ignore_node_ref(parent);
    }

    // Load ignore files in dirpath
    char path_buf[4096];
    int n = snprintf(path_buf, sizeof(path_buf), "%s/.gitignore", dirpath);
    if (n > 0 && (size_t)n < sizeof(path_buf)) {
        load_ignore_file(&node->list, path_buf);
    }

    n = snprintf(path_buf, sizeof(path_buf), "%s/.ignore", dirpath);
    if (n > 0 && (size_t)n < sizeof(path_buf)) {
        load_ignore_file(&node->list, path_buf);
    }

    return node;
}

void greg_ignore_node_ref(greg_ignore_node_t *node) {
    if (node) {
        ATOMIC_INC(node->ref_count);
    }
}

void greg_ignore_node_unref(greg_ignore_node_t *node) {
    if (!node) return;
    if (ATOMIC_DEC(node->ref_count) == 0) {
        free_ignore_list(&node->list);
        greg_ignore_node_t *parent = node->parent;
        free(node);
        if (parent) {
            greg_ignore_node_unref(parent);
        }
    }
}

int greg_ignore_node_should_ignore(const greg_ignore_node_t *node, const char *path, int is_dir, const greg_options_t *opts) {
    if (opts && opts->no_ignore) {
        return 0;
    }

    const char *filename = strrchr(path, '/');
#ifdef _WIN32
    const char *win_slash = strrchr(path, '\\');
    if (!filename || (win_slash && win_slash > filename)) {
        filename = win_slash;
    }
#endif
    filename = filename ? filename + 1 : path;

    if (greg_is_default_ignored(filename, is_dir, opts)) {
        return 1;
    }

    const greg_ignore_node_t *curr = node;
    while (curr) {
        const greg_ignore_list_t *list = &curr->list;
        if (list->count > 0) {
            int has_match = 0;
            int decision = 0;
            for (size_t i = 0; i < list->count; i++) {
                const greg_ignore_rule_t *rule = &list->rules[i];
                if (rule->is_dir_only && !is_dir) {
                    continue;
                }
                int rule_matched = 0;
                if (rule->is_glob) {
                    if (rule->is_anchored) {
                        rule_matched = match_glob(rule->pattern, path);
                    } else {
                        rule_matched = match_glob(rule->pattern, filename) || match_glob(rule->pattern, path);
                    }
                } else {
                    if (rule->is_anchored) {
                        rule_matched = (strcmp(rule->pattern, path) == 0);
                    } else {
                        rule_matched = (strcmp(rule->pattern, filename) == 0) || (strcmp(rule->pattern, path) == 0);
                    }
                }

                if (rule_matched) {
                    decision = rule->is_negation ? 0 : 1;
                    has_match = 1;
                }
            }
            if (has_match) {
                return decision;
            }
        }
        curr = curr->parent;
    }

    return 0;
}

