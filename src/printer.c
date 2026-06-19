#include "printer.h"
#include <stdio.h>

int greg_printer_init(greg_printer_t *printer, int color_enabled, int show_line_numbers, int files_with_matches) {
    printer->color_enabled = color_enabled;
    printer->show_line_numbers = show_line_numbers;
    printer->files_with_matches = files_with_matches;
    printer->match_count = 0;
    return greg_mutex_init(&printer->mutex);
}

void greg_printer_destroy(greg_printer_t *printer) {
    greg_mutex_destroy(&printer->mutex);
}

void greg_printer_print_match(greg_printer_t *printer, const char *filepath, size_t line_num, const char *line, size_t line_len, size_t match_start, size_t match_end) {
    greg_mutex_lock(&printer->mutex);

    // If files-with-matches is on, we don't print lines. We only print the filename once.
    // However, that is typically called via greg_printer_print_file, so this shouldn't be reached or we can print just file.
    printer->match_count++;
    if (printer->files_with_matches) {
        printf("%s\n", filepath);
        greg_mutex_unlock(&printer->mutex);
        return;
    }

    // Print File Path
    if (printer->color_enabled) {
        printf("\033[35m%s\033[0m:", filepath);
    } else {
        printf("%s:", filepath);
    }

    // Print Line Number
    if (printer->show_line_numbers) {
        if (printer->color_enabled) {
            printf("\033[32m%zu\033[0m:", line_num);
        } else {
            printf("%zu:", line_num);
        }
    }

    // Print the line with match highlighted
    if (match_start <= line_len && match_end <= line_len && match_start < match_end) {
        // Print pre-match
        fwrite(line, 1, match_start, stdout);
        
        // Print match (highlighted)
        if (printer->color_enabled) {
            printf("\033[1;38;5;203m");
        }
        fwrite(line + match_start, 1, match_end - match_start, stdout);
        if (printer->color_enabled) {
            printf("\033[0m");
        }

        // Print post-match
        fwrite(line + match_end, 1, line_len - match_end, stdout);
    } else {
        // Fallback: print whole line
        fwrite(line, 1, line_len, stdout);
    }

    // Ensure we end with a newline (if the file's line didn't have one, or always for consistency)
    if (line_len == 0 || line[line_len - 1] != '\n') {
        putchar('\n');
    }

    greg_mutex_unlock(&printer->mutex);
}

void greg_printer_print_file(greg_printer_t *printer, const char *filepath) {
    greg_mutex_lock(&printer->mutex);
    printer->match_count++;
    if (printer->color_enabled) {
        printf("\033[35m%s\033[0m\n", filepath);
    } else {
        printf("%s\n", filepath);
    }
    greg_mutex_unlock(&printer->mutex);
}
