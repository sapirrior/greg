#ifndef GREG_MMAP_H
#define GREG_MMAP_H

#include <stddef.h>

// Represents a mapped or read file buffer
typedef struct {
    void *data;
    size_t size;
    int is_mmap; // 1 if mmap was used, 0 if malloc/read was used
#ifdef _WIN32
    void *hMap;  // Store mapping handle to close safely
#endif
} greg_file_view_t;

// Maps a file into memory or reads it into a buffer if it's small.
// Returns 0 on success, non-zero on failure.
int greg_file_map(const char *filepath, greg_file_view_t *view);

// Releases the memory or unmaps the file.
void greg_file_unmap(greg_file_view_t *view);

#endif // GREG_MMAP_H

