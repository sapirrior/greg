#ifndef GREG_PRINTER_H
#define GREG_PRINTER_H

#include "greg_thread.h"
#include <stddef.h>

typedef struct {
    greg_mutex_t mutex;
    int color_enabled;
    int show_line_numbers;
    int files_with_matches; // -l: print only filenames that match
    size_t match_count;     // Track total matches for UX summary/exit code
} greg_printer_t;

int greg_printer_init(greg_printer_t *printer, int color_enabled, int show_line_numbers, int files_with_matches);
void greg_printer_destroy(greg_printer_t *printer);

// Prints only the file name
void greg_printer_print_file(greg_printer_t *printer, const char *filepath);

#endif // GREG_PRINTER_H
