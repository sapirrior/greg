#include "queue.h"
#include <stdlib.h>
#include <string.h>

int greg_queue_init(greg_queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->active = 1;
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
        free(curr->filepath);
        free(curr);
        curr = next;
    }
    q->head = q->tail = NULL;
    greg_mutex_unlock(&q->mutex);

    greg_mutex_destroy(&q->mutex);
    greg_cond_destroy(&q->cond);
}

int greg_queue_push(greg_queue_t *q, const char *filepath) {
    greg_queue_node_t *node = malloc(sizeof(greg_queue_node_t));
    if (!node) return -1;

    node->filepath = strdup(filepath);
    if (!node->filepath) {
        free(node);
        return -1;
    }
    node->next = NULL;

    greg_mutex_lock(&q->mutex);
    if (!q->active) {
        free(node->filepath);
        free(node);
        greg_mutex_unlock(&q->mutex);
        return -1;
    }

    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }

    greg_cond_signal(&q->cond);
    greg_mutex_unlock(&q->mutex);
    return 0;
}

char *greg_queue_pop(greg_queue_t *q) {
    greg_mutex_lock(&q->mutex);
    while (q->head == NULL && q->active) {
        greg_cond_wait(&q->cond, &q->mutex);
    }

    if (q->head == NULL && !q->active) {
        greg_mutex_unlock(&q->mutex);
        return NULL;
    }

    greg_queue_node_t *node = q->head;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }

    char *filepath = node->filepath;
    free(node);

    greg_mutex_unlock(&q->mutex);
    return filepath;
}

int greg_queue_pop_batch(greg_queue_t *q, char **out_filepaths, int max_batch) {
    greg_mutex_lock(&q->mutex);
    while (q->head == NULL && q->active) {
        greg_cond_wait(&q->cond, &q->mutex);
    }

    if (q->head == NULL && !q->active) {
        greg_mutex_unlock(&q->mutex);
        return 0;
    }

    int count = 0;
    while (q->head != NULL && count < max_batch) {
        greg_queue_node_t *node = q->head;
        q->head = node->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        out_filepaths[count++] = node->filepath;
        free(node);
    }

    greg_mutex_unlock(&q->mutex);
    return count;
}

void greg_queue_deactivate(greg_queue_t *q) {
    greg_mutex_lock(&q->mutex);
    q->active = 0;
    greg_cond_broadcast(&q->cond); 
    greg_mutex_unlock(&q->mutex);
}
