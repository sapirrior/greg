#include "walk.h"
#include "ignore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#define BATCH_MAX 64
#define PATH_BUF_SIZE 4096

typedef struct {
    char *paths[BATCH_MAX];
    int count;
} walk_batch_t;

static void flush_batch(greg_queue_t *queue, walk_batch_t *b) {
    if (b->count > 0) {
        greg_queue_push_batch(queue, b->paths, b->count);
        b->count = 0;
    }
}

// Pushes filepath into a batch, taking ownership of it. Flushes the batch
// to the queue first if full. Frees filepath on allocation failure instead
// of leaking it.
static void batch_add(greg_queue_t *queue, walk_batch_t *b, char *filepath) {
    if (!filepath) return;
    if (b->count >= BATCH_MAX) {
        flush_batch(queue, b);
    }
    b->paths[b->count++] = filepath;
}

#ifdef _WIN32
#include <windows.h>

static void walk_recursive_win(const char *dirpath, greg_queue_t *queue, greg_ignore_stack_t *ignore_stack, const greg_options_t *opts, walk_batch_t *b) {
    greg_ignore_stack_push(ignore_stack, dirpath);
    char search_path[PATH_BUF_SIZE];
    if (snprintf(search_path, sizeof(search_path), "%s\\*", dirpath) >= (int)sizeof(search_path)) {
        fprintf(stderr, "greg: warning: path too long, skipping: %s\n", dirpath);
        greg_ignore_stack_pop(ignore_stack);
        return;
    }

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        greg_ignore_stack_pop(ignore_stack);
        return;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char full_path[PATH_BUF_SIZE];
        int n = snprintf(full_path, sizeof(full_path), "%s\\%s", dirpath, find_data.cFileName);
        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            fprintf(stderr, "greg: warning: path too long, skipping: %s\\%s\n", dirpath, find_data.cFileName);
            continue;
        }

        int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (greg_ignore_stack_should_ignore(ignore_stack, full_path, is_dir)) {
            continue;
        }

        if (is_dir) {
            walk_recursive_win(full_path, queue, ignore_stack, opts, b);
        } else {
            batch_add(queue, b, strdup(full_path));
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    greg_ignore_stack_pop(ignore_stack);
}

int greg_walk_directory(const char *root_path, greg_queue_t *queue, const greg_options_t *opts) {
    greg_ignore_stack_t ignore_stack;
    greg_ignore_stack_init(&ignore_stack);

    walk_batch_t b = {0};

    DWORD attrs = GetFileAttributesA(root_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "greg: error: cannot access path: %s\n", root_path);
        greg_ignore_stack_destroy(&ignore_stack);
        return -1;
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        walk_recursive_win(root_path, queue, &ignore_stack, opts, &b);
    } else {
        if (!greg_ignore_stack_should_ignore(&ignore_stack, root_path, 0)) {
            batch_add(queue, &b, strdup(root_path));
        }
    }

    flush_batch(queue, &b);
    greg_ignore_stack_destroy(&ignore_stack);
    return 0;
}

#else // POSIX

#include <dirent.h>

static void walk_recursive_posix(const char *dirpath, greg_queue_t *queue, greg_ignore_stack_t *ignore_stack, const greg_options_t *opts, walk_batch_t *b) {
    greg_ignore_stack_push(ignore_stack, dirpath);
    DIR *dir = opendir(dirpath);
    if (!dir) {
        // Likely permission denied or a broken symlink target - skip and
        // keep walking rather than aborting the whole search.
        greg_ignore_stack_pop(ignore_stack);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_BUF_SIZE];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            fprintf(stderr, "greg: warning: path too long, skipping: %s/%s\n", dirpath, entry->d_name);
            continue;
        }

        int is_dir = 0;
#ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type == DT_DIR) {
            is_dir = 1;
        } else if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                is_dir = 1;
            }
        }
#else
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }
#endif

        if (greg_ignore_stack_should_ignore(ignore_stack, full_path, is_dir)) {
            continue;
        }

        if (is_dir) {
            walk_recursive_posix(full_path, queue, ignore_stack, opts, b);
        } else {
            batch_add(queue, b, strdup(full_path));
        }
    }

    closedir(dir);
    greg_ignore_stack_pop(ignore_stack);
}

int greg_walk_directory(const char *root_path, greg_queue_t *queue, const greg_options_t *opts) {
    greg_ignore_stack_t ignore_stack;
    greg_ignore_stack_init(&ignore_stack);

    walk_batch_t b = {0};

    struct stat st;
    if (stat(root_path, &st) != 0) {
        fprintf(stderr, "greg: error: cannot access path: %s\n", root_path);
        greg_ignore_stack_destroy(&ignore_stack);
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        walk_recursive_posix(root_path, queue, &ignore_stack, opts, &b);
    } else {
        if (!greg_ignore_stack_should_ignore(&ignore_stack, root_path, 0)) {
            batch_add(queue, &b, strdup(root_path));
        }
    }

    flush_batch(queue, &b);
    greg_ignore_stack_destroy(&ignore_stack);
    return 0;
}

#endif
