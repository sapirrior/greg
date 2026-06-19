#ifndef GREG_WALK_H
#define GREG_WALK_H

#include "queue.h"
#include "types.h"

// Recursively walks a root directory path and enqueues valid files to queue.
// Respects .gitignore rules dynamically.
int greg_walk_directory(const char *root_path, greg_queue_t *queue, const greg_options_t *opts);

#endif // GREG_WALK_H
