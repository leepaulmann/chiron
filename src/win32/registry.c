// Table of shimmed system DLLs.
#include <string.h>
#include <strings.h>

#include "win32.h"

extern const struct shim_export kernel32_exports[];
extern const struct shim_export user32_exports[];
extern const struct shim_export gdi32_exports[];
extern const struct shim_export advapi32_exports[];
extern const struct shim_export winmm_exports[];
extern const struct shim_export dsound_exports[];
extern const struct shim_export ole32_exports[];
extern const struct shim_export dplayx_exports[];

// System DLLs we do not implement yet still belong here: their imports become
// traps instead of falling through to copies in the game folder (GOG bundles
// a third-party ddraw.dll, which must never be loaded).
static const struct shim_export no_exports[] = {SHIM_END};

static const struct shim_dll dlls[] = {
    {"KERNEL32.dll", kernel32_exports},
    {"USER32.dll", user32_exports},
    {"GDI32.dll", gdi32_exports},
    {"ADVAPI32.dll", advapi32_exports},
    {"SHELL32.dll", no_exports},
    {"WINMM.dll", winmm_exports},
    {"DDRAW.dll", no_exports},
    {"DSOUND.dll", dsound_exports},
    {"MSVFW32.dll", no_exports},
    {"DPLAYX.dll", dplayx_exports},
    {"comdlg32.dll", no_exports},
    {"ole32.dll", ole32_exports},
    {"oleaut32.dll", no_exports},
    {"VERSION.dll", no_exports},
    {"MSACM32.dll", no_exports},
    {"WSOCK32.dll", no_exports},
    {"comctl32.dll", no_exports},
};

const struct shim_dll *win32_find_dll(const char *name) {
    for (const char *p = name; *p; p++)
        if (*p == '\\' || *p == '/') name = p + 1;
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp(name + n - 4, ".dll")) n -= 4;
    for (size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]); i++) {
        size_t m = strlen(dlls[i].name) - 4;
        if (n == m && !strncasecmp(name, dlls[i].name, n)) return &dlls[i];
    }
    return NULL;
}

void *win32_dll_export(const struct shim_dll *dll, const char *name, int ordinal) {
    for (const struct shim_export *e = dll->exports; e->name; e++)
        if (name ? !strcmp(e->name, name) : e->ordinal == ordinal) return e->fn;
    return NULL;
}
