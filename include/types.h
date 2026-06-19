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
    int raw;              // --raw
    int count_only;       // -c, --count
    int max_count;        // -m, --max-count
    int no_ignore;        // --no-ignore
    int follow_links;     // --follow
    int hidden;           // --hidden
    const char *pattern;
} greg_options_t;

#endif // GREG_TYPES_H
