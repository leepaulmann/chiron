// The set of loaded modules: the game executable, the game's own DLLs, and our
// shimmed system DLLs (which have handles but no mapped image).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pe.h"

extern int modules_trace;  // wrap every resolved import in a trace stub

// Load the main executable from the game folder (Windows path, e.g. "terranx.exe").
struct pe_image *modules_load_exe(const char *winpath, char *err, size_t errlen);

// LoadLibraryA/GetModuleHandleA/GetProcAddress semantics. Shim DLLs get fake
// handles that are only meaningful to these functions.
void *modules_load_library(const char *name);
void *modules_get_handle(const char *name);  // NULL name = main executable
void *modules_get_proc(void *handle, const char *name, int ordinal);
int modules_get_filename(void *handle, char *out, size_t outlen);

// "terranx.exe+0x1234" style description for diagnostics.
void modules_describe_addr(uint32_t addr, char *out, size_t outlen);
