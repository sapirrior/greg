#include "searcher.h"
#include "greg_mmap.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    size_t line_num;
    const char *line_start;
    size_t line_len;
    size_t match_start;
    size_t match_end;
} greg_local_match_t;

static void print_line_highlighted(int color_enabled, const char *line, size_t line_len, size_t match_start, size_t match_end) {
    if (match_start <= line_len && match_end <= line_len && match_start < match_end) {
        fwrite(line, 1, match_start, stdout);
        if (color_enabled) {
            printf("\033[1;38;5;203m");
        }
        fwrite(line + match_start, 1, match_end - match_start, stdout);
        if (color_enabled) {
            printf("\033[0m");
        }
        fwrite(line + match_end, 1, line_len - match_end, stdout);
    } else {
        fwrite(line, 1, line_len, stdout);
    }

    if (line_len == 0 || line[line_len - 1] != '\n') {
        putchar('\n');
    }
}

int greg_search_file(const char *filepath, greg_matcher_t *matcher, pcre2_match_data *match_data, greg_printer_t *printer, const greg_options_t *opts) {
    size_t size = 0;
    void *data = greg_mmap_file(filepath, &size);
    if (!data) {
        return -1;
    }

    const char *buffer = (const char *)data;

    // 1. Binary check
    if (!opts->search_binary && size > 0) {
        size_t scan_len = (size < 1024) ? size : 1024;
        if (memchr(buffer, '\0', scan_len) != NULL) {
            greg_munmap_file(data, size);
            return 0;
        }
    }

    // 2. Local match tracking buffer optimization
    // Use stack buffer of 128 elements by default to avoid malloc/free in 99% of searches
    greg_local_match_t stack_matches[128];
    size_t matches_cap = 128;
    greg_local_match_t *matches = stack_matches;
    int matches_is_heap = 0;

    size_t line_num = 1;
    size_t offset = 0;
    int match_count = 0;

    // 3. Line scanning loop
    while (offset < size) {
        size_t line_end = offset;
        while (line_end < size && buffer[line_end] != '\n') {
            line_end++;
        }
        
        size_t line_len = line_end - offset;
        if (line_end < size && buffer[line_end] == '\n') {
            line_len++;
        }

        const char *line_start = buffer + offset;
        size_t match_start = 0, match_end = 0;

        int rc = greg_matcher_find(matcher, match_data, line_start, line_len, 0, &match_start, &match_end);
        
        // Match condition handles inversion (-v)
        int is_match = opts->invert_match ? (rc == 0) : (rc == 1);

        if (is_match) {
            // In case of invert match, we don't want highlighting
            if (opts->invert_match) {
                match_start = 0;
                match_end = 0;
            }

            if (opts->files_with_matches) {
                greg_printer_print_file(printer, filepath);
                if (matches_is_heap) free(matches);
                greg_munmap_file(data, size);
                return 1; // Return early for files-with-matches
            }

            if ((size_t)match_count >= matches_cap) {
                matches_cap *= 2;
                if (!matches_is_heap) {
                    greg_local_match_t *temp = malloc(sizeof(greg_local_match_t) * matches_cap);
                    if (!temp) {
                        greg_munmap_file(data, size);
                        return -1;
                    }
                    memcpy(temp, matches, sizeof(greg_local_match_t) * match_count);
                    matches = temp;
                    matches_is_heap = 1;
                } else {
                    greg_local_match_t *temp = realloc(matches, sizeof(greg_local_match_t) * matches_cap);
                    if (!temp) {
                        free(matches);
                        greg_munmap_file(data, size);
                        return -1;
                    }
                    matches = temp;
                }
            }

            matches[match_count].line_num = line_num;
            matches[match_count].line_start = line_start;
            matches[match_count].line_len = line_len;
            matches[match_count].match_start = match_start;
            matches[match_count].match_end = match_end;
            match_count++;
        }

        offset += line_len;
        line_num++;
    }

    // 4. Output results atomically under the printer lock
    if (match_count > 0) {
        greg_mutex_lock(&printer->mutex);
        printer->match_count += match_count;
        if (opts->heading) {
            // GitHub style path format: Soft blue
            if (printer->color_enabled) {
                printf("\033[38;5;75m%s\033[0m\n", filepath);
            } else {
                printf("%s\n", filepath);
            }

            // Print each match underneath
            for (int i = 0; i < match_count; i++) {
                if (printer->show_line_numbers) {
                    if (printer->color_enabled) {
                        // GitHub line number color: neutral gray (no pipe divider)
                        printf("  \033[38;5;244m%zu:\033[0m ", matches[i].line_num);
                    } else {
                        printf("  %zu: ", matches[i].line_num);
                    }
                } else {
                    printf("  ");
                }
                print_line_highlighted(printer->color_enabled, matches[i].line_start, matches[i].line_len, matches[i].match_start, matches[i].match_end);
            }
            printf("\n"); // spacing between files
        } else {
            // Standard grep layout
            for (int i = 0; i < match_count; i++) {
                if (printer->color_enabled) {
                    printf("\033[38;5;75m%s\033[0m:", filepath);
                } else {
                    printf("%s:", filepath);
                }

                if (printer->show_line_numbers) {
                    if (printer->color_enabled) {
                        printf("\033[38;5;244m%zu\033[0m:", matches[i].line_num);
                    } else {
                        printf("%zu:", matches[i].line_num);
                    }
                }
                print_line_highlighted(printer->color_enabled, matches[i].line_start, matches[i].line_len, matches[i].match_start, matches[i].match_end);
            }
        }
        greg_mutex_unlock(&printer->mutex);
    }

    if (matches_is_heap) {
        free(matches);
    }
    greg_munmap_file(data, size);
    return match_count;
}
