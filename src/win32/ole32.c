// ole32: COM start-up only. soundx.dll asks COM for DirectSound and EAX
// objects, which are not available yet.
#include "win32.h"

#define REGDB_E_CLASSNOTREG 0x80040154u

DWORD WINAPI CoInitialize(void *reserved) { return 0; }
void WINAPI CoUninitialize(void) {}

DWORD WINAPI CoCreateInstance(const void *clsid, void *outer, DWORD ctx, const void *iid, void **out) {
    if (out) *out = NULL;
    return REGDB_E_CLASSNOTREG;
}
