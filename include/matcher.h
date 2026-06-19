#ifndef GREG_MATCHER_H
#define GREG_MATCHER_H

#include <stddef.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "types.h"

typedef struct {
    pcre2_code *code;
} greg_matcher_t;

// Compiles a PCRE2 pattern using the options.
// Returns 0 on success, non-zero on failure.
int greg_matcher_init(greg_matcher_t *matcher, const greg_options_t *opts);

// Destroys the compiled pattern and releases resources.
void greg_matcher_destroy(greg_matcher_t *matcher);

// Checks if the pattern matches a specific subject text of a given length.
// Returns 1 if a match is found, 0 if not, or negative on error.
// If start_out and end_out are non-NULL, sets them to the start and end byte offsets of the first match.
int greg_matcher_find(greg_matcher_t *matcher, pcre2_match_data *match_data, const char *subject, size_t length, size_t start_offset, size_t *start_out, size_t *end_out);

#endif // GREG_MATCHER_H
