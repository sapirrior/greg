#ifndef GREG_TYPES_H
#define GREG_TYPES_H

typedef struct {
    int case_insensitive;
    int show_line_numbers;
    int color_enabled;
    int files_with_matches;
    int search_binary;    // If 1, searches binary files; if 0, skips them
    int smart_case;       // -S
    int invert_match;     // -v
    int fixed_strings;    // -F
    int heading;          // --heading
    const char *pattern;
} greg_options_t;

#endif // GREG_TYPES_H
