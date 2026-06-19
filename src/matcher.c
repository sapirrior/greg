#include "matcher.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int has_upper(const char *str) {
    while (*str) {
        if (isupper((unsigned char)*str)) return 1;
        str++;
    }
    return 0;
}

int greg_matcher_init(greg_matcher_t *matcher, const greg_options_t *opts) {
    matcher->code = NULL;

    if (opts->pattern == NULL || opts->pattern[0] == '\0') {
        fprintf(stderr, "greg: error: empty pattern\n");
        return -1;
    }

    int errorcode;
    PCRE2_SIZE erroroffset;
    uint32_t options = 0;

    int caseless = opts->case_insensitive;
    if (opts->smart_case && !caseless) {
        if (!has_upper(opts->pattern)) {
            caseless = 1;
        }
    }

    if (caseless) {
        options |= PCRE2_CASELESS;
    }

    if (opts->fixed_strings) {
        options |= PCRE2_LITERAL;
    }

    options |= PCRE2_MULTILINE;

    matcher->code = pcre2_compile(
        (PCRE2_SPTR)opts->pattern,
        PCRE2_ZERO_TERMINATED,
        options,
        &errorcode,
        &erroroffset,
        NULL
    );

    if (matcher->code == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
        fprintf(stderr, "greg: pattern error at offset %d: %s\n", (int)erroroffset, buffer);
        return -1;
    }

    // Enable the JIT compiler for faster matching. JIT is an optional
    // PCRE2 build feature, so a non-zero return just means we transparently
    // fall back to the (slower) interpreted matcher - not a fatal error.
    int jit_rc = pcre2_jit_compile(matcher->code, PCRE2_JIT_COMPLETE);
    if (jit_rc != 0 && jit_rc != PCRE2_ERROR_JIT_BADOPTION) {
        // PCRE2_ERROR_JIT_BADOPTION means JIT support wasn't built into the
        // linked libpcre2 at all; anything else is worth a heads-up since it
        // silently costs throughput.
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(jit_rc, buffer, sizeof(buffer));
        fprintf(stderr, "greg: warning: JIT compile failed (%s), using interpreted matcher\n", buffer);
    }

    return 0;
}

void greg_matcher_destroy(greg_matcher_t *matcher) {
    if (matcher->code) {
        pcre2_code_free(matcher->code);
        matcher->code = NULL;
    }
}

int greg_matcher_find(greg_matcher_t *matcher, pcre2_match_data *match_data, const char *subject, size_t length, size_t start_offset, size_t *start_out, size_t *end_out) {
    int rc = pcre2_match(
        matcher->code,
        (PCRE2_SPTR)subject,
        length,
        start_offset,
        0,
        match_data,
        NULL
    );

    if (rc < 0) {
        if (rc == PCRE2_ERROR_NOMATCH) {
            return 0;
        }
        // Other errors (e.g. PCRE2_ERROR_PARTIAL)
        return rc;
    }

    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (start_out) *start_out = ovector[0];
    if (end_out) *end_out = ovector[1];

    return 1;
}
