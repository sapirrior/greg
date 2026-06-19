#include "pool.h"
#include "matcher.h"
#include <stdlib.h>
#include <stdio.h>

static GREG_THREAD_ROUTINE worker_thread_routine(void *arg) {
    greg_pool_t *pool = (greg_pool_t *)arg;

    // Safely cast user data to retrieve matcher structure for match context creation
    greg_worker_ctx_t *ctx = (greg_worker_ctx_t *)pool->user_data;

    pcre2_match_data *match_data = NULL;
    if (ctx && ctx->matcher && ctx->matcher->code) {
        match_data = pcre2_match_data_create_from_pattern(ctx->matcher->code, NULL);
    }
    if (!match_data) {
        // Out-of-memory (or no compiled pattern). We can't search without
        // match data, so drain and discard this thread's share of the queue
        // instead of dereferencing NULL inside pcre2_match().
        fprintf(stderr, "greg: warning: worker thread failed to allocate match data, skipping its work\n");
        char *drain[16];
        int count;
        while ((count = greg_queue_pop_batch(pool->queue, drain, 16)) > 0) {
            for (int i = 0; i < count; i++) free(drain[i]);
        }
        return 0;
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

    pcre2_match_data_free(match_data);
    return 0;
}

int greg_pool_init(greg_pool_t *pool, int num_threads, greg_queue_t *queue, greg_work_fn work_func, void *user_data) {
    if (num_threads < 1) {
        num_threads = 1;
    }

    pool->num_threads = num_threads;
    pool->queue = queue;
    pool->work_func = work_func;
    pool->user_data = user_data;

    pool->threads = malloc(sizeof(greg_thread_t) * (size_t)num_threads);
    if (!pool->threads) {
        return -1;
    }

    for (int i = 0; i < num_threads; i++) {
        if (greg_thread_create(&pool->threads[i], worker_thread_routine, pool) != 0) {
            fprintf(stderr, "greg: warning: failed to start worker thread %d/%d\n", i + 1, num_threads);
            // Error handling: join already started threads, then bail out
            // with however many we did manage to start (graceful
            // degradation instead of a hard failure, as long as we got at
            // least one worker running).
            pool->num_threads = i;
            if (i == 0) {
                free(pool->threads);
                pool->threads = NULL;
                return -1;
            }
            return 0;
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
