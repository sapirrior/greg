#include "greg_mmap.h"
#include <stdio.h>
#include <stdlib.h>

#define MMAP_THRESHOLD (64 * 1024) // 64KB threshold for mmap vs standard read

#ifdef _WIN32
#include <windows.h>

int greg_file_map(const char *filepath, greg_file_view_t *view) {
    view->data = NULL;
    view->size = 0;
    view->is_mmap = 0;
    view->hMap = NULL;

    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        return -1;
    }
    view->size = (size_t)size.QuadPart;

    if (view->size == 0) {
        CloseHandle(hFile);
        view->data = "";
        return 0;
    }

    // Use fast standard read for small files
    if (view->size < MMAP_THRESHOLD) {
        view->data = malloc(view->size);
        if (!view->data) {
            CloseHandle(hFile);
            return -1;
        }
        DWORD bytesRead;
        if (!ReadFile(hFile, view->data, (DWORD)view->size, &bytesRead, NULL) || bytesRead != view->size) {
            free(view->data);
            CloseHandle(hFile);
            return -1;
        }
        CloseHandle(hFile);
        return 0;
    }

    // Fallback to memory mapping for large files
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile);
    if (hMap == NULL) return -1;

    void *addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        CloseHandle(hMap);
        return -1;
    }

    view->data = addr;
    view->hMap = hMap;
    view->is_mmap = 1;
    return 0;
}

void greg_file_unmap(greg_file_view_t *view) {
    if (view->size == 0) return;
    if (view->is_mmap) {
        UnmapViewOfFile(view->data);
        CloseHandle((HANDLE)view->hMap);
    } else {
        free(view->data);
    }
}

#else // Unix/Linux/macOS (POSIX)

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

int greg_file_map(const char *filepath, greg_file_view_t *view) {
    view->data = NULL;
    view->size = 0;
    view->is_mmap = 0;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return -1;
    }
    view->size = (size_t)sb.st_size;

    if (view->size == 0) {
        close(fd);
        view->data = ""; // Safe dummy pointer
        return 0;
    }

    // Fast standard memory read for small files (bypasses heavy mmap kernel overhead)
    if (view->size < MMAP_THRESHOLD) {
        view->data = malloc(view->size);
        if (!view->data) {
            close(fd);
            return -1;
        }
        
        ssize_t bytes_read = 0;
        size_t total_read = 0;
        while (total_read < view->size) {
            bytes_read = read(fd, (char*)view->data + total_read, view->size - total_read);
            if (bytes_read < 0) {
                free(view->data);
                close(fd);
                return -1;
            }
            if (bytes_read == 0) break; // EOF reached
            total_read += bytes_read;
        }
        close(fd);
        
        if (total_read != view->size) {
            free(view->data);
            return -1;
        }
        return 0;
    }

    // Heavy mmap for large files
    void *addr = mmap(NULL, view->size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) return -1;

    view->data = addr;
    view->is_mmap = 1;
    return 0;
}

void greg_file_unmap(greg_file_view_t *view) {
    if (view->size == 0) return;
    if (view->is_mmap) {
        munmap(view->data, view->size);
    } else {
        free(view->data);
    }
}

#endif

