#include "pool.h"
#include "matcher.h"
#include <stdlib.h>

static GREG_THREAD_ROUTINE worker_thread_routine(void *arg) {
    greg_pool_t *pool = (greg_pool_t *)arg;
    
    // Safely cast user data to retrieve matcher structure for match context creation
    greg_worker_ctx_t *ctx = (greg_worker_ctx_t *)pool->user_data;

    pcre2_match_data *match_data = NULL;
    if (ctx && ctx->matcher && ctx->matcher->code) {
        match_data = pcre2_match_data_create_from_pattern(ctx->matcher->code, NULL);
    }

    char *batch[16];
    while (1) {
        int count = greg_queue_pop_batch(pool->queue, batch, 16);
        if (count == 0) {
            break; // Queue was deactivated and is empty
        }
        for (int i = 0; i < count; i++) {
            pool->work_func(batch[i], match_data, pool->user_data);
            free(batch[i]);
        }
    }

    if (match_data) {
        pcre2_match_data_free(match_data);
    }
    return 0;
}

int greg_pool_init(greg_pool_t *pool, int num_threads, greg_queue_t *queue, greg_work_fn work_func, void *user_data) {
    pool->num_threads = num_threads;
    pool->queue = queue;
    pool->work_func = work_func;
    pool->user_data = user_data;

    pool->threads = malloc(sizeof(greg_thread_t) * num_threads);
    if (!pool->threads) {
        return -1;
    }

    for (int i = 0; i < num_threads; i++) {
        if (greg_thread_create(&pool->threads[i], worker_thread_routine, pool) != 0) {
            // Error handling: join already started threads
            pool->num_threads = i;
            greg_pool_join(pool);
            free(pool->threads);
            pool->threads = NULL;
            return -1;
        }
    }

    return 0;
}

void greg_pool_join(greg_pool_t *pool) {
    if (!pool->threads) return;
    for (int i = 0; i < pool->num_threads; i++) {
        greg_thread_join(pool->threads[i]);
    }
}

void greg_pool_destroy(greg_pool_t *pool) {
    if (pool->threads) {
        free(pool->threads);
        pool->threads = NULL;
    }
}

