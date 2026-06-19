#include "walk.h"
#include "ignore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#define PATH_BUF_SIZE 4096

#ifdef _WIN32
#include <windows.h>

int greg_walk_single_directory(const char *dirpath, greg_queue_t *queue, greg_ignore_node_t *parent_ignore_node, const greg_options_t *opts) {
    greg_ignore_node_t *ignore_node = greg_ignore_node_create(parent_ignore_node, dirpath);
    if (!ignore_node) return -1;

    size_t dir_len = strlen(dirpath);
    if (dir_len + 3 >= PATH_BUF_SIZE) {
        greg_ignore_node_unref(ignore_node);
        return -1;
    }

    char search_path[PATH_BUF_SIZE];
    memcpy(search_path, dirpath, dir_len);
    memcpy(search_path + dir_len, "\\*", 3);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        greg_ignore_node_unref(ignore_node);
        return -1;
    }

    greg_work_item_t batch[64];
    int batch_count = 0;

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (greg_is_default_ignored(find_data.cFileName, is_dir, opts)) {
            continue;
        }

        size_t name_len = strlen(find_data.cFileName);
        if (dir_len + 1 + name_len + 1 > PATH_BUF_SIZE) {
            continue;
        }

        char full_path[PATH_BUF_SIZE];
        memcpy(full_path, dirpath, dir_len);
        full_path[dir_len] = '\\';
        memcpy(full_path + dir_len + 1, find_data.cFileName, name_len + 1);

        int is_reparse = (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;
        if (is_dir && is_reparse && !opts->follow_links) {
            continue;
        }

        if (greg_ignore_node_should_ignore(ignore_node, full_path, is_dir, opts)) {
            continue;
        }

        greg_work_item_t item;
        item.path = strdup(full_path);
        if (!item.path) continue;
        
        if (is_dir) {
            item.type = GREG_WORK_DIR;
            item.ignore_node = ignore_node;
            greg_ignore_node_ref(ignore_node); // dir walkers need the chain
        } else {
            item.type = GREG_WORK_FILE;
            item.ignore_node = NULL; // already filtered; no ref needed
        }

        if (batch_count >= 64) {
            greg_queue_push_batch(queue, batch, batch_count);
            batch_count = 0;
        }
        batch[batch_count++] = item;

    } while (FindNextFileA(hFind, &find_data));

    if (batch_count > 0) {
        greg_queue_push_batch(queue, batch, batch_count);
    }

    FindClose(hFind);
    greg_ignore_node_unref(ignore_node);
    return 0;
}

int greg_walk_directory(const char *root_path, greg_queue_t *queue, const greg_options_t *opts) {
    DWORD attrs = GetFileAttributesA(root_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "greg: error: cannot access path: %s\n", root_path);
        return -1;
    }

    int is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    
    greg_work_item_t item;
    item.path = strdup(root_path);
    if (!item.path) return -1;
    item.ignore_node = NULL;
    
    if (is_dir) {
        item.type = GREG_WORK_DIR;
    } else {
        item.type = GREG_WORK_FILE;
    }
    
    if (greg_queue_push(queue, item) != 0) {
        return -1;
    }
    return 0;
}

#else // POSIX

#include <dirent.h>

int greg_walk_single_directory(const char *dirpath, greg_queue_t *queue, greg_ignore_node_t *parent_ignore_node, const greg_options_t *opts) {
    greg_ignore_node_t *ignore_node = greg_ignore_node_create(parent_ignore_node, dirpath);
    if (!ignore_node) return -1;

    DIR *dir = opendir(dirpath);
    if (!dir) {
        greg_ignore_node_unref(ignore_node);
        return -1;
    }

    size_t dir_len = strlen(dirpath);
    struct dirent *entry;
    greg_work_item_t batch[64];
    int batch_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        int is_dir = 0;
        int is_file = 0;
        int need_stat = 0;

        #ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type == DT_DIR) {
            is_dir = 1;
        } else if (entry->d_type == DT_REG) {
            is_file = 1;
        } else if (entry->d_type == DT_LNK) {
            if (opts->follow_links) {
                need_stat = 1;
            } else {
                is_file = 1;
            }
        } else if (entry->d_type == DT_UNKNOWN) {
            need_stat = 1;
        }
        #else
        need_stat = 1;
        #endif

        if (!need_stat) {
            if (greg_is_default_ignored(entry->d_name, is_dir, opts)) {
                continue;
            }
        }

        size_t name_len = strlen(entry->d_name);
        if (dir_len + 1 + name_len + 1 > PATH_BUF_SIZE) {
            continue;
        }

        char full_path[PATH_BUF_SIZE];
        memcpy(full_path, dirpath, dir_len);
        full_path[dir_len] = '/';
        memcpy(full_path + dir_len + 1, entry->d_name, name_len + 1);

        if (need_stat) {
            struct stat st;
            int stat_rc = opts->follow_links ? stat(full_path, &st) : lstat(full_path, &st);
            if (stat_rc == 0) {
                if (S_ISDIR(st.st_mode)) {
                    is_dir = 1;
                } else if (S_ISREG(st.st_mode)) {
                    is_file = 1;
                }
            }
            if (greg_is_default_ignored(entry->d_name, is_dir, opts)) {
                continue;
            }
        }

        if (!is_dir && !is_file) {
            continue;
        }

        if (greg_ignore_node_should_ignore(ignore_node, full_path, is_dir, opts)) {
            continue;
        }

        greg_work_item_t item;
        item.path = strdup(full_path);
        if (!item.path) continue;

        if (is_dir) {
            item.type = GREG_WORK_DIR;
            item.ignore_node = ignore_node;
            greg_ignore_node_ref(ignore_node); // dir walkers need the chain
        } else {
            item.type = GREG_WORK_FILE;
            item.ignore_node = NULL; // already filtered; no ref needed
        }

        if (batch_count >= 64) {
            greg_queue_push_batch(queue, batch, batch_count);
            batch_count = 0;
        }
        batch[batch_count++] = item;
    }

    if (batch_count > 0) {
        greg_queue_push_batch(queue, batch, batch_count);
    }

    closedir(dir);
    greg_ignore_node_unref(ignore_node);
    return 0;
}

int greg_walk_directory(const char *root_path, greg_queue_t *queue, const greg_options_t *opts) {
    struct stat st;
    int stat_rc = opts->follow_links ? stat(root_path, &st) : lstat(root_path, &st);

    if (stat_rc != 0) {
        fprintf(stderr, "greg: error: cannot access path: %s\n", root_path);
        return -1;
    }

    int is_dir = S_ISDIR(st.st_mode);

    greg_work_item_t item;
    item.path = strdup(root_path);
    if (!item.path) return -1;
    item.ignore_node = NULL;

    if (is_dir) {
        item.type = GREG_WORK_DIR;
    } else {
        item.type = GREG_WORK_FILE;
    }

    if (greg_queue_push(queue, item) != 0) {
        return -1;
    }
    return 0;
}

#endif
