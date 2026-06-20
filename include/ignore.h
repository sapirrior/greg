#ifndef GREG_IGNORE_H
#define GREG_IGNORE_H

#include <stddef.h>
#include "types.h"

typedef struct {
    char *pattern;
    int is_dir_only;
    int is_negation; // Starts with !
    int is_anchored; // Starts with / (relative to the directory level)
    int is_glob;     // 1 if pattern contains wildcards (*, ?)
} greg_ignore_rule_t;

typedef struct {
    greg_ignore_rule_t *rules;
    size_t count;
    size_t capacity;
} greg_ignore_list_t;

typedef struct greg_ignore_node {
    greg_ignore_list_t list;
    struct greg_ignore_node *parent;
    int ref_count;
} greg_ignore_node_t;

// Simple global checker for defaults (e.g. skipping .git, node_modules)
int greg_is_default_ignored(const char *name, int is_dir, const greg_options_t *opts);

// Node-based ignore API for parallel walking
greg_ignore_node_t *greg_ignore_node_create(greg_ignore_node_t *parent, const char *dirpath);
void greg_ignore_node_ref(greg_ignore_node_t *node);
void greg_ignore_node_unref(greg_ignore_node_t *node);
int greg_ignore_node_should_ignore(const greg_ignore_node_t *node, const char *path, int is_dir, const greg_options_t *opts);

#endif // GREG_IGNORE_H

