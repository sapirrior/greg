#include "printer.h"
#include "ansi.h"
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

void greg_printer_print_file(greg_printer_t *printer, const char *filepath) {
    greg_mutex_lock(&printer->mutex);
    printer->match_count++;
    if (printer->color_enabled) {
        printf(ANSI_FILEPATH_FM "%s" ANSI_RESET "\n", filepath);
    } else {
        printf("%s\n", filepath);
    }
    greg_mutex_unlock(&printer->mutex);
}
