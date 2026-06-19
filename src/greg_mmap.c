#include "greg_mmap.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

void *greg_mmap_file(const char *filepath, size_t *out_size) {
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        return NULL;
    }
    *out_size = (size_t)size.QuadPart;

    if (*out_size == 0) {
        CloseHandle(hFile);
        // Map empty file as a special case or return a dummy pointer
        return ""; 
    }

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile); // Can close hFile after CreateFileMapping
    if (hMap == NULL) {
        return NULL;
    }

    void *addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap); // Can close hMap after MapViewOfFile
    return addr;
}

void greg_munmap_file(void *addr, size_t size) {
    if (size == 0) {
        return; // Dummy pointer for empty file
    }
    UnmapViewOfFile(addr);
}

#else // Unix/Linux/macOS

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void *greg_mmap_file(const char *filepath, size_t *out_size) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return NULL;
    }
    *out_size = (size_t)sb.st_size;

    if (*out_size == 0) {
        close(fd);
        return ""; // Return safe dummy pointer for empty files
    }

    void *addr = mmap(NULL, *out_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd); // Can close fd after mmap

    if (addr == MAP_FAILED) {
        return NULL;
    }

    return addr;
}

void greg_munmap_file(void *addr, size_t size) {
    if (size == 0) {
        return; // Dummy pointer for empty file
    }
    munmap(addr, size);
}

#endif
