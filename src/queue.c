#include "queue.h"
#include "ignore.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Allocate a node from the free-list or a slab. Caller holds mutex.
static greg_queue_node_t *alloc_node(greg_queue_t *q) {
    if (q->free_list) {
        greg_queue_node_t *n = q->free_list;
        q->free_list = n->next;
        return n;
    }
    // Need a new slab
    greg_queue_slab_t *slab = q->slabs;
    if (!slab || slab->used >= GREG_QUEUE_SLAB_SIZE) {
        slab = malloc(sizeof(greg_queue_slab_t));
        if (!slab) return NULL;
        slab->used = 0;
        slab->next_slab = q->slabs;
        q->slabs = slab;
    }
    return &slab->nodes[slab->used++];
}

// Return a node to the free-list. Caller holds mutex.
static void free_node(greg_queue_t *q, greg_queue_node_t *n) {
    n->next = q->free_list;
    q->free_list = n;
}

int greg_queue_init(greg_queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->active = 1;
    q->size = 0;
    q->active_tasks = 0;
    q->free_list = NULL;
    q->slabs = NULL;
    if (greg_mutex_init(&q->mutex) != 0) return -1;
    if (greg_cond_init(&q->cond) != 0) {
        greg_mutex_destroy(&q->mutex);
        return -1;
    }
    return 0;
}

void greg_queue_destroy(greg_queue_t *q) {
    greg_mutex_lock(&q->mutex);
    greg_queue_node_t *curr = q->head;
    while (curr) {
        greg_queue_node_t *next = curr->next;
        free(curr->item.path);
        if (curr->item.ignore_node) {
            greg_ignore_node_unref(curr->item.ignore_node);
        }
        curr = next;
    }
    // Free all slabs
    greg_queue_slab_t *slab = q->slabs;
    while (slab) {
        greg_queue_slab_t *next = slab->next_slab;
        free(slab);
        slab = next;
    }
    q->head = q->tail = NULL;
    q->free_list = NULL;
    q->slabs = NULL;
    greg_mutex_unlock(&q->mutex);
    greg_mutex_destroy(&q->mutex);
    greg_cond_destroy(&q->cond);
}

int greg_queue_push(greg_queue_t *q, greg_work_item_t item) {
    greg_mutex_lock(&q->mutex);
    if (!q->active) {
        greg_mutex_unlock(&q->mutex);
        free(item.path);
        if (item.ignore_node) greg_ignore_node_unref(item.ignore_node);
        return -1;
    }
    greg_queue_node_t *node = alloc_node(q);
    if (!node) {
        greg_mutex_unlock(&q->mutex);
        free(item.path);
        if (item.ignore_node) greg_ignore_node_unref(item.ignore_node);
        return -1;
    }
    node->item = item;
    node->next = NULL;
    if (q->tail) { q->tail->next = node; q->tail = node; }
    else { q->head = q->tail = node; }
    q->size++;
    q->active_tasks++;
    greg_cond_signal(&q->cond);
    greg_mutex_unlock(&q->mutex);
    return 0;
}

int greg_queue_push_batch(greg_queue_t *q, greg_work_item_t *items, int count) {
    if (count == 0) return 0;

    greg_mutex_lock(&q->mutex);
    if (!q->active) {
        greg_mutex_unlock(&q->mutex);
        for (int i = 0; i < count; i++) {
            free(items[i].path);
            if (items[i].ignore_node) greg_ignore_node_unref(items[i].ignore_node);
        }
        return -1;
    }

    int pushed = 0;
    for (int i = 0; i < count; i++) {
        greg_queue_node_t *node = alloc_node(q);
        if (!node) {
            free(items[i].path);
            if (items[i].ignore_node) greg_ignore_node_unref(items[i].ignore_node);
            continue;
        }
        node->item = items[i];
        node->next = NULL;
        if (q->tail) { q->tail->next = node; q->tail = node; }
        else { q->head = q->tail = node; }
        pushed++;
    }

    q->size += pushed;
    q->active_tasks += pushed;
    if (pushed > 0) greg_cond_broadcast(&q->cond);
    greg_mutex_unlock(&q->mutex);
    return pushed > 0 ? 0 : -1;
}

int greg_queue_pop_batch(greg_queue_t *q, greg_work_item_t *out_items, int max_batch) {
    greg_mutex_lock(&q->mutex);
    while (q->head == NULL && q->active) {
        greg_cond_wait(&q->cond, &q->mutex);
    }

    if (q->head == NULL) {
        // Queue empty and deactivated
        greg_mutex_unlock(&q->mutex);
        return 0;
    }

    int count = 0;
    while (q->head != NULL && count < max_batch) {
        greg_queue_node_t *node = q->head;
        q->head = node->next;
        if (!q->head) q->tail = NULL;
        out_items[count++] = node->item;
        free_node(q, node);
    }

    q->size -= count;
    // Note: active_tasks is NOT decremented here — caller must call tasks_done()
    greg_mutex_unlock(&q->mutex);
    return count;
}

void greg_queue_tasks_done(greg_queue_t *q, int count) {
    greg_mutex_lock(&q->mutex);
    q->active_tasks -= count;
    if (q->active_tasks <= 0 && q->head == NULL) {
        q->active = 0;
        greg_cond_broadcast(&q->cond);
    }
    greg_mutex_unlock(&q->mutex);
}

void greg_queue_deactivate(greg_queue_t *q) {
    greg_mutex_lock(&q->mutex);
    q->active = 0;
    greg_cond_broadcast(&q->cond);
    greg_mutex_unlock(&q->mutex);
}
