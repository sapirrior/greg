#ifndef GREG_MMAP_H
#define GREG_MMAP_H

#include <stddef.h>

// Maps a file into memory and returns a pointer to its content.
// Sets out_size to the size of the file in bytes.
// Returns NULL on failure.
void *greg_mmap_file(const char *filepath, size_t *out_size);

// Unmaps a previously mapped file.
void greg_munmap_file(void *addr, size_t size);

#endif // GREG_MMAP_H
