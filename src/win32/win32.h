// Minimal Win32 type definitions and the shim registry interface.
// Only what the game actually uses is defined here; we never include real
// Windows headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4, "Chiron's host must be built as 32-bit x86");

#define WINAPI __attribute__((stdcall))
#define WINAPIV __attribute__((cdecl))  // variadic Win32 functions

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t UINT;
typedef int BOOL;
typedef uint16_t WCHAR;
typedef void *HANDLE;
typedef void *HMODULE;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// Error codes used by the shims.
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_PROC_NOT_FOUND 127
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_EXISTS 80
#define ERROR_CALL_NOT_IMPLEMENTED 120

// One exported function of a shimmed DLL. `ordinal` is 0 when the game only
// imports it by name.
struct shim_export {
    const char *name;
    uint16_t ordinal;
    void *fn;
};

struct shim_dll {
    const char *name;
    const struct shim_export *exports;
};

#define SHIM(fn) {#fn, 0, (void *)fn}
#define SHIM_END {0, 0, 0}

// Registry (registry.c)
const struct shim_dll *win32_find_dll(const char *name);
void *win32_dll_export(const struct shim_dll *dll, const char *name, int ordinal);

// Thread environment (loader/teb.c)
void win32_set_last_error(DWORD err);
DWORD win32_get_last_error(void);

// Paths (path.c): the game sees the game folder as drive C:.
void path_init(const char *game_root);
const char *path_game_root(void);
// Translate a Windows path into a host path, matching each component
// case-insensitively. Returns 0 on success; the last component may not exist.
int path_to_host(const char *winpath, char *out, size_t outlen);
const char *path_get_cwd(void);
int path_set_cwd(const char *winpath);
