#include "walk.h"
#include "ignore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#define BATCH_MAX 64

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

#ifdef _WIN32
#include <windows.h>

static void walk_recursive_win(const char *dirpath, greg_queue_t *queue, greg_ignore_stack_t *ignore_stack, const greg_options_t *opts, walk_batch_t *b) {
    greg_ignore_stack_push(ignore_stack, dirpath);
    char search_path[2048];
    snprintf(search_path, sizeof(search_path), "%s\\*", dirpath);

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

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dirpath, find_data.cFileName);
        int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (greg_ignore_stack_should_ignore(ignore_stack, full_path, is_dir)) {
            continue;
        }

        if (is_dir) {
            walk_recursive_win(full_path, queue, ignore_stack, opts, b);
        } else {
            b->paths[b->count++] = strdup(full_path);
            if (b->count >= BATCH_MAX) flush_batch(queue, b);
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
        greg_ignore_stack_destroy(&ignore_stack);
        return -1;
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        walk_recursive_win(root_path, queue, &ignore_stack, opts, &b);
    } else {
        if (!greg_ignore_stack_should_ignore(&ignore_stack, root_path, 0)) {
            b.paths[b.count++] = strdup(root_path);
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
        greg_ignore_stack_pop(ignore_stack);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);

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
            b->paths[b->count++] = strdup(full_path);
            if (b->count >= BATCH_MAX) flush_batch(queue, b);
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
        greg_ignore_stack_destroy(&ignore_stack);
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        walk_recursive_posix(root_path, queue, &ignore_stack, opts, &b);
    } else {
        if (!greg_ignore_stack_should_ignore(&ignore_stack, root_path, 0)) {
            b.paths[b.count++] = strdup(root_path);
        }
    }

    flush_batch(queue, &b);
    greg_ignore_stack_destroy(&ignore_stack);
    return 0;
}

#endif

