#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "queue.h"
#include "pool.h"
#include "matcher.h"
#include "printer.h"
#include "searcher.h"
#include "walk.h"

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

// Portable helper to get CPU cores
static int get_cpu_cores(void) {
    int cores = 4; // Sensible default fallback
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    cores = (int)sysinfo.dwNumberOfProcessors;
#else
    #ifdef _SC_NPROCESSORS_ONLN
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n > 0) cores = (int)n;
    #endif
#endif
    return cores > 0 ? cores : 4;
}

// Portable helper to check if stdout is a TTY
static int is_terminal_stdout(void) {
    return isatty(fileno(stdout));
}

// Shared worker context for the thread pool
typedef struct {
    greg_matcher_t *matcher;
    greg_printer_t *printer;
    const greg_options_t *opts;
} greg_worker_ctx_t;

// Thread pool work function
static void pool_work_func(const char *filepath, pcre2_match_data *match_data, void *user_data) {
    greg_worker_ctx_t *ctx = (greg_worker_ctx_t *)user_data;
    greg_search_file(filepath, ctx->matcher, match_data, ctx->printer, ctx->opts);
}

static void print_usage(const char *prog) {
    printf("greg: A modern, high-performance text search utility in C.\n\n");
    printf("Usage:\n");
    printf("  %s [options] <pattern> [path]\n\n", prog);
    printf("Options:\n");
    printf("  -i, --ignore-case          Search case-insensitively.\n");
    printf("  -S, --smart-case           Search case-insensitively if pattern is all lowercase (default).\n");
    printf("  -v, --invert-match         Invert match: select non-matching lines.\n");
    printf("  -F, --fixed-strings        Treat the pattern as a literal string.\n");
    printf("  -n, --line-number          Always show line numbers (default if outputting to terminal).\n");
    printf("  -N, --no-line-number       Never show line numbers.\n");
    printf("  -l, --files-with-matches   Only print filenames of files containing matches.\n");
    printf("  -j, --threads <num>        Number of threads to use (default: CPU core count).\n");
    printf("  -a, --text                 Search binary files (do not skip them).\n");
    printf("      --heading              Group matches by file name (default on terminal).\n");
    printf("      --no-heading           Disable grouped matches.\n");
    printf("      --color <always|never> Control output coloring.\n");
    printf("  -h, --help                 Print this help message.\n");
}

int main(int argc, char **argv) {
    greg_options_t opts;
    opts.case_insensitive = 0;
    opts.smart_case = 1;
    opts.invert_match = 0;
    opts.fixed_strings = 0;
    opts.heading = is_terminal_stdout();
    opts.show_line_numbers = is_terminal_stdout();
    opts.color_enabled = is_terminal_stdout();
    opts.files_with_matches = 0;
    opts.search_binary = 0;
    opts.pattern = NULL;

    const char *search_path = ".";
    int num_threads = get_cpu_cores();
    int parsed_positionals = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ignore-case") == 0) {
                opts.case_insensitive = 1;
            } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--smart-case") == 0) {
                opts.smart_case = 1;
            } else if (strcmp(argv[i], "--no-smart-case") == 0) {
                opts.smart_case = 0;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--invert-match") == 0) {
                opts.invert_match = 1;
            } else if (strcmp(argv[i], "-F") == 0 || strcmp(argv[i], "--fixed-strings") == 0) {
                opts.fixed_strings = 1;
            } else if (strcmp(argv[i], "--heading") == 0) {
                opts.heading = 1;
            } else if (strcmp(argv[i], "--no-heading") == 0) {
                opts.heading = 0;
            } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--line-number") == 0) {
                opts.show_line_numbers = 1;
            } else if (strcmp(argv[i], "-N") == 0 || strcmp(argv[i], "--no-line-number") == 0) {
                opts.show_line_numbers = 0;
            } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--files-with-matches") == 0) {
                opts.files_with_matches = 1;
            } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--text") == 0) {
                opts.search_binary = 1;
            } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--threads") == 0) {
                if (i + 1 < argc) {
                    num_threads = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "Error: --threads option requires an argument.\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--color") == 0) {
                if (i + 1 < argc) {
                    const char *color_opt = argv[++i];
                    if (strcmp(color_opt, "always") == 0) opts.color_enabled = 1;
                    else if (strcmp(color_opt, "never") == 0) opts.color_enabled = 0;
                } else {
                    fprintf(stderr, "Error: --color option requires an argument (always|never).\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage(argv[0]);
                return 0;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        } else {
            if (parsed_positionals == 0) {
                opts.pattern = argv[i];
                parsed_positionals++;
            } else if (parsed_positionals == 1) {
                search_path = argv[i];
                parsed_positionals++;
            } else {
                fprintf(stderr, "Error: Too many positional arguments.\n");
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if (opts.pattern == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    // 1. Init matcher
    greg_matcher_t matcher;
    if (greg_matcher_init(&matcher, &opts) != 0) {
        return 1;
    }

    // 2. Init printer
    greg_printer_t printer;
    if (greg_printer_init(&printer, opts.color_enabled, opts.show_line_numbers, opts.files_with_matches) != 0) {
        greg_matcher_destroy(&matcher);
        return 1;
    }

    // 3. Init queue
    greg_queue_t queue;
    if (greg_queue_init(&queue) != 0) {
        greg_printer_destroy(&printer);
        greg_matcher_destroy(&matcher);
        return 1;
    }

    // 4. Init thread pool
    greg_worker_ctx_t ctx = { &matcher, &printer, &opts };
    greg_pool_t pool;
    if (greg_pool_init(&pool, num_threads, &queue, pool_work_func, &ctx) != 0) {
        greg_queue_destroy(&queue);
        greg_printer_destroy(&printer);
        greg_matcher_destroy(&matcher);
        return 1;
    }

    // 5. Start walking directory in main thread (producer)
    int walk_rc = greg_walk_directory(search_path, &queue, &opts);
    if (walk_rc != 0) {
        fprintf(stderr, "Error traversing path: %s\n", search_path);
    }

    // 6. Signal completion to workers
    greg_queue_deactivate(&queue);

    // 7. Join worker threads and clean up
    greg_pool_join(&pool);
    greg_pool_destroy(&pool);
    greg_queue_destroy(&queue);

    size_t total_matches = printer.match_count;
    greg_printer_destroy(&printer);
    greg_matcher_destroy(&matcher);

    if (walk_rc != 0) {
        return 1;
    }

    if (total_matches == 0) {
        // Developer friendly message printed to stderr if stdout is a TTY to avoid breaking stdout pipelines
        if (is_terminal_stdout()) {
            fprintf(stderr, "\033[1;30mNo matches found for pattern: '%s'\033[0m\n", opts.pattern);
        }
        return 1;
    }

    return 0;
}
