#ifndef GREG_SEARCHER_H
#define GREG_SEARCHER_H

#include "matcher.h"
#include "printer.h"
#include "types.h"

// Searches a single file. Returns the match count, or negative on failure.
int greg_search_file(const char *filepath, greg_matcher_t *matcher, pcre2_match_data *match_data, greg_printer_t *printer, const greg_options_t *opts);

#endif // GREG_SEARCHER_H
