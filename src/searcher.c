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

// --- Growable output buffer --------------------------------------------
// Instead of issuing a flurry of printf()/fwrite() calls per match (each one
// a libc call, several of them tiny writes), we render the entire file's
// output into one heap buffer and flush it with a single fwrite() at the
// end, under the printer lock. This means:
//   1. Far fewer stdio calls per file (less overhead).
//   2. The lock is held only long enough to do one big write, not for the
//      whole "format N matches" process - much better concurrency between
//      worker threads when a file has many matches.
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom; // set if any allocation failed; remaining appends become no-ops
} outbuf_t;

#define OUTBUF_INITIAL_CAP 4096

static int outbuf_init(outbuf_t *ob) {
    ob->data = malloc(OUTBUF_INITIAL_CAP);
    ob->cap = ob->data ? OUTBUF_INITIAL_CAP : 0;
    ob->len = 0;
    ob->oom = ob->data == NULL;
    return ob->oom ? -1 : 0;
}

static void outbuf_free(outbuf_t *ob) {
    free(ob->data);
    ob->data = NULL;
    ob->len = ob->cap = 0;
}

static void outbuf_reserve(outbuf_t *ob, size_t extra) {
    if (ob->oom) return;
    if (ob->len + extra <= ob->cap) return;
    size_t new_cap = ob->cap == 0 ? OUTBUF_INITIAL_CAP : ob->cap;
    while (new_cap < ob->len + extra) {
        new_cap *= 2;
    }
    char *tmp = realloc(ob->data, new_cap);
    if (!tmp) {
        ob->oom = 1;
        return;
    }
    ob->data = tmp;
    ob->cap = new_cap;
}

static void outbuf_append(outbuf_t *ob, const char *s, size_t n) {
    if (ob->oom || n == 0) return;
    outbuf_reserve(ob, n);
    if (ob->oom) return;
    memcpy(ob->data + ob->len, s, n);
    ob->len += n;
}

static void outbuf_append_str(outbuf_t *ob, const char *s) {
    outbuf_append(ob, s, strlen(s));
}

// Appends an unsigned size_t in decimal without going through printf/snprintf.
static void outbuf_append_size(outbuf_t *ob, size_t v) {
    char tmp[24];
    int i = (int)sizeof(tmp);
    if (v == 0) {
        tmp[--i] = '0';
    } else {
        while (v > 0) {
            tmp[--i] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    outbuf_append(ob, tmp + i, (size_t)sizeof(tmp) - (size_t)i);
}

static void outbuf_append_line_highlighted(outbuf_t *ob, int color_enabled, const char *line, size_t line_len, size_t match_start, size_t match_end) {
    if (match_start <= line_len && match_end <= line_len && match_start < match_end) {
        outbuf_append(ob, line, match_start);
        if (color_enabled) outbuf_append_str(ob, "\033[1;38;5;203m");
        outbuf_append(ob, line + match_start, match_end - match_start);
        if (color_enabled) outbuf_append_str(ob, "\033[0m");
        outbuf_append(ob, line + match_end, line_len - match_end);
    } else {
        outbuf_append(ob, line, line_len);
    }

    if (line_len == 0 || line[line_len - 1] != '\n') {
        outbuf_append(ob, "\n", 1);
    }
}

int greg_search_file(const char *filepath, greg_matcher_t *matcher, pcre2_match_data *match_data, greg_printer_t *printer, const greg_options_t *opts) {
    greg_file_view_t view;
    if (greg_file_map(filepath, &view) != 0) {
        // Soft-fail: permission denied, broken symlink, file vanished
        // between enumeration and open, etc. Skip it like grep does rather
        // than aborting the whole search.
        return -1;
    }

    const char *buffer = (const char *)view.data;
    size_t size = view.size;

    // 1. Binary check
    if (!opts->search_binary && size > 0) {
        size_t scan_len = (size < 1024) ? size : 1024;
        if (memchr(buffer, '\0', scan_len) != NULL) {
            greg_file_unmap(&view);
            return 0;
        }
    }

    // 2. Local match tracking buffer optimization (small files stay
    //    entirely on the stack - no heap traffic at all for the common case)
    greg_local_match_t stack_matches[128];
    size_t matches_cap = 128;
    greg_local_match_t *matches = stack_matches;
    int matches_is_heap = 0;

    size_t line_num = 1;
    size_t offset = 0;
    int match_count = 0;
    int alloc_failed = 0;

    // 3. Line scanning loop. Uses memchr (typically vectorized in libc) to
    //    find line ends instead of a byte-at-a-time scan.
    while (offset < size) {
        const void *nl = memchr(buffer + offset, '\n', size - offset);
        size_t line_end = nl ? (size_t)((const char *)nl - buffer) : size;

        size_t line_len = line_end - offset;
        if (line_end < size) { // found '\n'
            line_len++;
        }

        const char *line_start = buffer + offset;
        size_t match_start = 0, match_end = 0;

        int rc = greg_matcher_find(matcher, match_data, line_start, line_len, 0, &match_start, &match_end);
        int is_match = opts->invert_match ? (rc == 0) : (rc == 1);

        if (is_match) {
            if (opts->invert_match) {
                match_start = 0;
                match_end = 0;
            }

            if (opts->files_with_matches) {
                greg_printer_print_file(printer, filepath);
                if (matches_is_heap) free(matches);
                greg_file_unmap(&view);
                return 1;
            }

            if ((size_t)match_count >= matches_cap) {
                size_t new_cap = matches_cap * 2;
                if (!matches_is_heap) {
                    greg_local_match_t *temp = malloc(sizeof(greg_local_match_t) * new_cap);
                    if (!temp) {
                        alloc_failed = 1;
                        break;
                    }
                    memcpy(temp, matches, sizeof(greg_local_match_t) * (size_t)match_count);
                    matches = temp;
                    matches_is_heap = 1;
                } else {
                    greg_local_match_t *temp = realloc(matches, sizeof(greg_local_match_t) * new_cap);
                    if (!temp) {
                        alloc_failed = 1;
                        break;
                    }
                    matches = temp;
                }
                matches_cap = new_cap;
            }

            matches[match_count].line_num = line_num;
            matches[match_count].line_start = line_start;
            matches[match_count].line_len = line_len;
            matches[match_count].match_start = match_start;
            matches[match_count].match_end = match_end;
            match_count++;
        } else if (rc < 0) {
            // A genuine PCRE2 matching error (not just "no match") on this
            // line. Report once and keep scanning the rest of the file
            // instead of treating the whole search as fatal.
            fprintf(stderr, "greg: warning: match error in %s at line %zu (code %d)\n", filepath, line_num, rc);
        }

        offset += line_len;
        line_num++;
    }

    if (alloc_failed) {
        fprintf(stderr, "greg: warning: out of memory collecting matches in %s, results truncated\n", filepath);
    }

    // 4. Render all output for this file into a private buffer, then flush
    //    it to stdout with a single write under the lock.
    if (match_count > 0) {
        outbuf_t ob;
        if (outbuf_init(&ob) != 0) {
            // Last-resort fallback: can't even allocate a small output
            // buffer. Still report the matches were found via the count so
            // the exit-code/summary stays correct, but skip pretty-printing.
            fprintf(stderr, "greg: warning: out of memory formatting output for %s\n", filepath);
            greg_mutex_lock(&printer->mutex);
            printer->match_count += (size_t)match_count;
            greg_mutex_unlock(&printer->mutex);
        } else {
            if (opts->heading) {
                if (printer->color_enabled) {
                    outbuf_append_str(&ob, "\033[38;5;75m");
                    outbuf_append_str(&ob, filepath);
                    outbuf_append_str(&ob, "\033[0m\n");
                } else {
                    outbuf_append_str(&ob, filepath);
                    outbuf_append(&ob, "\n", 1);
                }

                for (int i = 0; i < match_count; i++) {
                    if (printer->show_line_numbers) {
                        if (printer->color_enabled) {
                            outbuf_append_str(&ob, "  \033[38;5;244m");
                            outbuf_append_size(&ob, matches[i].line_num);
                            outbuf_append_str(&ob, ":\033[0m ");
                        } else {
                            outbuf_append_str(&ob, "  ");
                            outbuf_append_size(&ob, matches[i].line_num);
                            outbuf_append_str(&ob, ": ");
                        }
                    } else {
                        outbuf_append_str(&ob, "  ");
                    }
                    outbuf_append_line_highlighted(&ob, printer->color_enabled, matches[i].line_start, matches[i].line_len, matches[i].match_start, matches[i].match_end);
                }
                outbuf_append(&ob, "\n", 1);
            } else {
                for (int i = 0; i < match_count; i++) {
                    if (printer->color_enabled) {
                        outbuf_append_str(&ob, "\033[38;5;75m");
                        outbuf_append_str(&ob, filepath);
                        outbuf_append_str(&ob, "\033[0m:");
                    } else {
                        outbuf_append_str(&ob, filepath);
                        outbuf_append(&ob, ":", 1);
                    }

                    if (printer->show_line_numbers) {
                        if (printer->color_enabled) {
                            outbuf_append_str(&ob, "\033[38;5;244m");
                            outbuf_append_size(&ob, matches[i].line_num);
                            outbuf_append_str(&ob, "\033[0m:");
                        } else {
                            outbuf_append_size(&ob, matches[i].line_num);
                            outbuf_append(&ob, ":", 1);
                        }
                    }
                    outbuf_append_line_highlighted(&ob, printer->color_enabled, matches[i].line_start, matches[i].line_len, matches[i].match_start, matches[i].match_end);
                }
            }

            if (ob.oom) {
                fprintf(stderr, "greg: warning: out of memory formatting output for %s (partial output shown)\n", filepath);
            }

            greg_mutex_lock(&printer->mutex);
            printer->match_count += (size_t)match_count;
            if (ob.len > 0) {
                fwrite(ob.data, 1, ob.len, stdout);
            }
            greg_mutex_unlock(&printer->mutex);

            outbuf_free(&ob);
        }
    }

    if (matches_is_heap) {
        free(matches);
    }
    greg_file_unmap(&view);
    return match_count;
}
