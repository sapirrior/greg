#ifndef GREG_QUEUE_H
#define GREG_QUEUE_H

#include "greg_thread.h"

typedef enum {
    GREG_WORK_FILE,
    GREG_WORK_DIR
} greg_work_type_t;

struct greg_ignore_node;

typedef struct {
    greg_work_type_t type;
    char *path;
    struct greg_ignore_node *ignore_node; // only non-NULL for GREG_WORK_DIR
} greg_work_item_t;

// Queue node — pooled in slabs, never individually malloc'd
typedef struct greg_queue_node {
    greg_work_item_t item;
    struct greg_queue_node *next;
} greg_queue_node_t;

// Slab of queue nodes — avoids per-item malloc
#define GREG_QUEUE_SLAB_SIZE 256
typedef struct greg_queue_slab {
    struct greg_queue_slab *next_slab;
    int used;
    greg_queue_node_t nodes[GREG_QUEUE_SLAB_SIZE];
} greg_queue_slab_t;

typedef struct {
    greg_queue_node_t *head;
    greg_queue_node_t *tail;
    greg_mutex_t mutex;
    greg_cond_t cond;
    int active;
    int size;
    int active_tasks; // termination detection
    // Free-list for node recycling
    greg_queue_node_t *free_list;
    // Slab chain for bulk allocation
    greg_queue_slab_t *slabs;
} greg_queue_t;

int greg_queue_init(greg_queue_t *q);
void greg_queue_destroy(greg_queue_t *q);

int greg_queue_push(greg_queue_t *q, greg_work_item_t item);
int greg_queue_push_batch(greg_queue_t *q, greg_work_item_t *items, int count);

// Returns count popped; atomically marks them as in-flight.
// When done processing, call greg_queue_tasks_done(q, count).
int greg_queue_pop_batch(greg_queue_t *q, greg_work_item_t *out_items, int max_batch);
void greg_queue_tasks_done(greg_queue_t *q, int count);

void greg_queue_deactivate(greg_queue_t *q);

#endif // GREG_QUEUE_H
