// ole32: COM start-up only. soundx.dll asks COM for DirectSound and EAX
// objects, which are not available yet.
#include <stdio.h>

#include "win32.h"

#define REGDB_E_CLASSNOTREG 0x80040154u

DWORD WINAPI CoInitialize(void *reserved) { return 0; }
void WINAPI CoUninitialize(void) {}

DWORD WINAPI CoCreateInstance(const void *clsid, void *outer, DWORD ctx, const void *iid, void **out) {
    const uint8_t *c = clsid;
    fprintf(stderr, "chiron: CoCreateInstance {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} not available\n",
            *(const uint32_t *)c, *(const uint16_t *)(c + 4), *(const uint16_t *)(c + 6), c[8], c[9], c[10], c[11],
            c[12], c[13], c[14], c[15]);
    if (out) *out = NULL;
    return REGDB_E_CLASSNOTREG;
}
