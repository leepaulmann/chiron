// DPLAYX: multiplayer is not supported yet. Reporting DirectPlay as
// unavailable leaves single-player untouched.
#include "win32.h"

#define DPERR_UNAVAILABLE 0x887703e8u

// ordinal: DirectPlayCreate 1
DWORD WINAPI DirectPlayCreate(const void *guid, void **out, void *outer) {
    if (out) *out = NULL;
    return DPERR_UNAVAILABLE;
}

// ordinal: DirectPlayEnumerateA 2
DWORD WINAPI DirectPlayEnumerateA(void *callback, void *context) { return 0; }

// ordinal: DirectPlayLobbyCreateA 4
DWORD WINAPI DirectPlayLobbyCreateA(const void *guid, void **out, void *outer, void *data, DWORD size) {
    if (out) *out = NULL;
    return DPERR_UNAVAILABLE;
}
