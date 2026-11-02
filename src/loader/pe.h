// Loader for 32-bit PE images (the game executable and its DLLs).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct pe_image {
    char name[64];       // file name as found on disk, e.g. "terranx.exe"
    uint8_t *base;       // where the image is mapped
    uint32_t size;       // SizeOfImage
    uint32_t pref_base;  // ImageBase from the header
    uint32_t entry_rva;  // 0 if none
    int is_dll;
    uint32_t export_rva, export_size;
    uint32_t tls_rva;
    struct pe_image *next;
};

// Called for every import. Returns the address to place in the IAT; it must
// never return NULL (return a trap thunk instead).
typedef void *(*pe_resolver)(const char *dll, const char *name, int ordinal);

// Map, relocate and bind the PE file at `host_path`. Writes a message to `err`
// on failure and returns -1.
int pe_load(const char *host_path, struct pe_image *img, pe_resolver resolve,
            char *err, size_t errlen);

// Look up an export by name, or by ordinal when `name` is NULL.
void *pe_find_export(const struct pe_image *img, const char *name, int ordinal);
