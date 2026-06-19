#include "pool.h"
#include "matcher.h"
#include "walk.h"
#include "ignore.h"
#include <stdlib.h>
#include <stdio.h>

static GREG_THREAD_ROUTINE worker_thread_routine(void *arg) {
    greg_thread_arg_t *targ = (greg_thread_arg_t *)arg;
    if (!targ) return 0;

    greg_worker_ctx_t *ctx = targ->ctx;
    pcre2_match_data *match_data = NULL;
    if (ctx && ctx->matcher && ctx->matcher->code) {
        match_data = pcre2_match_data_create_from_pattern(ctx->matcher->code, NULL);
    }

    if (!match_data) {
        fprintf(stderr, "greg: warning: worker thread failed to allocate match data\n");
        greg_work_item_t drain[16];
        int count;
        while ((count = greg_queue_pop_batch(targ->queue, drain, 16)) > 0) {
            for (int i = 0; i < count; i++) {
                free(drain[i].path);
                if (drain[i].ignore_node) greg_ignore_node_unref(drain[i].ignore_node);
            }
            greg_queue_tasks_done(targ->queue, count);
        }
        free(targ);
        return 0;
    }

    greg_work_item_t batch[32];
    while (1) {
        int count = greg_queue_pop_batch(targ->queue, batch, 32);
        if (count == 0) break;

        for (int i = 0; i < count; i++) {
            if (batch[i].type == GREG_WORK_DIR) {
                // Walk this directory — will push child files/dirs back into queue
                greg_walk_single_directory(batch[i].path, targ->queue,
                                           batch[i].ignore_node, ctx->opts);
            } else {
                targ->work_func(batch[i].path, match_data, ctx);
            }
            free(batch[i].path);
            if (batch[i].ignore_node) greg_ignore_node_unref(batch[i].ignore_node);
        }
        greg_queue_tasks_done(targ->queue, count);
    }

    pcre2_match_data_free(match_data);
    free(targ);
    return 0;
}

int greg_pool_init(greg_pool_t *pool, int num_threads, greg_queue_t *queue,
                   greg_work_fn work_func, void *user_data) {
    if (num_threads < 1) num_threads = 1;

    pool->num_threads = num_threads;
    pool->queue = queue;
    pool->work_func = work_func;
    pool->user_data = user_data;

    pool->threads = malloc(sizeof(greg_thread_t) * (size_t)num_threads);
    if (!pool->threads) return -1;

    for (int i = 0; i < num_threads; i++) {
        greg_thread_arg_t *arg = malloc(sizeof(greg_thread_arg_t));
        if (!arg) {
            pool->num_threads = i;
            if (i == 0) { free(pool->threads); pool->threads = NULL; return -1; }
            return 0;
        }
        arg->queue = queue;
        arg->work_func = work_func;
        arg->ctx = (greg_worker_ctx_t *)user_data;

        if (greg_thread_create(&pool->threads[i], worker_thread_routine, arg) != 0) {
            free(arg);
            pool->num_threads = i;
            if (i == 0) { free(pool->threads); pool->threads = NULL; return -1; }
            return 0;
        }
    }
    return 0;
}

void greg_pool_wait_and_destroy(greg_pool_t *pool) {
    if (!pool) return;
    if (pool->threads) {
        for (int i = 0; i < pool->num_threads; i++)
            greg_thread_join(pool->threads[i]);
        free(pool->threads);
        pool->threads = NULL;
    }
    pool->num_threads = 0;
}
