#define _GNU_SOURCE
#include "modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../win32/win32.h"
#include "thunks.h"

int modules_trace;

static struct pe_image *images;  // newest first; the exe is last

// Shim DLL handles are addresses inside this array, so they are unique and
// recognisable without being mapped images.
#define MAX_SHIM_HANDLES 32
static const struct shim_dll *shim_handles[MAX_SHIM_HANDLES];
static char shim_handle_mem[MAX_SHIM_HANDLES];

static const char *basename_of(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    return b;
}

// Compare module names case-insensitively, treating a missing ".dll" as present.
static int same_module(const char *a, const char *b) {
    a = basename_of(a);
    b = basename_of(b);
    size_t la = strlen(a), lb = strlen(b);
    if (la > 4 && !strcasecmp(a + la - 4, ".dll")) la -= 4;
    if (lb > 4 && !strcasecmp(b + lb - 4, ".dll")) lb -= 4;
    return la == lb && !strncasecmp(a, b, la);
}

static struct pe_image *find_image(const char *name) {
    for (struct pe_image *i = images; i; i = i->next)
        if (same_module(i->name, name)) return i;
    return NULL;
}

static void *shim_handle(const struct shim_dll *dll) {
    for (int i = 0; i < MAX_SHIM_HANDLES; i++) {
        if (shim_handles[i] == dll) return &shim_handle_mem[i];
        if (!shim_handles[i]) {
            shim_handles[i] = dll;
            return &shim_handle_mem[i];
        }
    }
    abort();
}

static const struct shim_dll *shim_from_handle(void *h) {
    char *p = h;
    if (p < shim_handle_mem || p >= shim_handle_mem + MAX_SHIM_HANDLES) return NULL;
    return shim_handles[p - shim_handle_mem];
}

static struct pe_image *image_from_handle(void *h) {
    for (struct pe_image *i = images; i; i = i->next)
        if (i->base == h) return i;
    return NULL;
}

static struct pe_image *load_image(const char *winpath, char *err, size_t errlen);

static void *resolve_import(const char *dll, const char *name, int ordinal) {
    void *fn = NULL;
    const struct shim_dll *shim = win32_find_dll(dll);
    if (shim) {
        fn = win32_dll_export(shim, name, ordinal);
    } else {
        char err[256];
        struct pe_image *img = find_image(dll);
        if (!img) img = load_image(dll, err, sizeof(err));
        if (img) fn = pe_find_export(img, name, ordinal);
        else fprintf(stderr, "chiron: %s\n", err);
    }
    if (!fn) return thunk_trap(dll, name, ordinal);
    return modules_trace && shim ? thunk_trace(dll, name, ordinal, fn) : fn;
}

static struct pe_image *load_image(const char *winpath, char *err, size_t errlen) {
    char host[4096];
    if (path_to_host(winpath, host, sizeof(host))) {
        snprintf(err, errlen, "%s: not found", winpath);
        return NULL;
    }
    struct pe_image *img = calloc(1, sizeof(*img));
    // Register before binding so circular imports find it.
    if (pe_load(host, img, resolve_import, err, errlen)) {
        free(img);
        return NULL;
    }
    img->next = images;
    images = img;
    if (img->is_dll && img->entry_rva) {
        typedef BOOL(WINAPI * dllmain_fn)(void *, DWORD, void *);
        ((dllmain_fn)(img->base + img->entry_rva))(img->base, 1 /* PROCESS_ATTACH */, NULL);
    }
    return img;
}

struct pe_image *modules_load_exe(const char *winpath, char *err, size_t errlen) {
    return load_image(winpath, err, errlen);
}

void *modules_load_library(const char *name) {
    const struct shim_dll *shim = win32_find_dll(name);
    if (shim) return shim_handle(shim);
    struct pe_image *img = find_image(name);
    if (!img) {
        char err[256];
        img = load_image(name, err, sizeof(err));
        if (!img) {
            fprintf(stderr, "chiron: LoadLibrary: %s\n", err);
            win32_set_last_error(ERROR_MOD_NOT_FOUND);
            return NULL;
        }
    }
    return img->base;
}

void *modules_get_handle(const char *name) {
    if (!name) {
        struct pe_image *i = images;
        while (i && i->next) i = i->next;
        return i ? i->base : NULL;
    }
    const struct shim_dll *shim = win32_find_dll(name);
    if (shim) return shim_handle(shim);
    struct pe_image *img = find_image(name);
    if (!img) win32_set_last_error(ERROR_MOD_NOT_FOUND);
    return img ? img->base : NULL;
}

void *modules_get_proc(void *handle, const char *name, int ordinal) {
    void *fn = NULL;
    const struct shim_dll *shim = shim_from_handle(handle);
    if (shim) fn = win32_dll_export(shim, name, ordinal);
    else {
        struct pe_image *img = image_from_handle(handle);
        if (img) fn = pe_find_export(img, name, ordinal);
    }
    if (!fn) {
        fprintf(stderr, "chiron: GetProcAddress(%s) not found\n", name ? name : "#ordinal");
        win32_set_last_error(ERROR_PROC_NOT_FOUND);
    }
    return fn;
}

int modules_get_filename(void *handle, char *out, size_t outlen) {
    const char *name = NULL;
    if (!handle) handle = modules_get_handle(NULL);
    const struct shim_dll *shim = shim_from_handle(handle);
    struct pe_image *img = shim ? NULL : image_from_handle(handle);
    if (shim) name = shim->name;
    else if (img) name = img->name;
    if (!name) return -1;
    // Game files live at the root of drive C:; system DLLs in C:\WINDOWS\SYSTEM.
    return snprintf(out, outlen, shim ? "C:\\WINDOWS\\SYSTEM\\%s" : "C:\\%s", name);
}

void modules_describe_addr(uint32_t addr, char *out, size_t outlen) {
    for (struct pe_image *i = images; i; i = i->next)
        if (addr >= (uint32_t)i->base && addr < (uint32_t)i->base + i->size) {
            // Report addresses relative to the preferred base so they match
            // disassemblers and OpenSMACX/Thinker address lists.
            snprintf(out, outlen, "%s:0x%08x", i->name, addr - (uint32_t)i->base + i->pref_base);
            return;
        }
    snprintf(out, outlen, "0x%08x", addr);
}
