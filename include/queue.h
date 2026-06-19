#ifndef GREG_QUEUE_H
#define GREG_QUEUE_H

#include "greg_thread.h"

typedef struct greg_queue_node {
    char *filepath;
    struct greg_queue_node *next;
} greg_queue_node_t;

typedef struct {
    greg_queue_node_t *head;
    greg_queue_node_t *tail;
    greg_mutex_t mutex;
    greg_cond_t cond;
    int active;
} greg_queue_t;

int greg_queue_init(greg_queue_t *q);
void greg_queue_destroy(greg_queue_t *q);
int greg_queue_push(greg_queue_t *q, const char *filepath);
char *greg_queue_pop(greg_queue_t *q);
int greg_queue_pop_batch(greg_queue_t *q, char **out_filepaths, int max_batch);
void greg_queue_deactivate(greg_queue_t *q);

#endif // GREG_QUEUE_H
