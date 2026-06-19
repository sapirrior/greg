#ifndef GREG_POOL_H
#define GREG_POOL_H

#include "greg_thread.h"
#include "queue.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "matcher.h"
#include "printer.h"
#include "types.h"

// Shared worker context for the thread pool
typedef struct {
    greg_matcher_t *matcher;
    greg_printer_t *printer;
    const greg_options_t *opts;
} greg_worker_ctx_t;

typedef void (*greg_work_fn)(const char *filepath, pcre2_match_data *match_data, void *user_data);

typedef struct {
    greg_thread_t *threads;
    int num_threads;
    greg_queue_t *queue;
    greg_work_fn work_func;
    void *user_data;
} greg_pool_t;

int greg_pool_init(greg_pool_t *pool, int num_threads, greg_queue_t *queue, greg_work_fn work_func, void *user_data);
void greg_pool_join(greg_pool_t *pool);
void greg_pool_destroy(greg_pool_t *pool);

#endif // GREG_POOL_H

