// DSOUND: no sound device yet. The game and soundx.dll fall back to
// running silently. Real audio through SDL arrives with milestone 3.
#include <stdio.h>

#include "win32.h"

#define DSERR_NODRIVER 0x88780078u

// ordinal: DirectSoundCreate 1
DWORD WINAPI DirectSoundCreate(const void *guid, void **ds, void *outer) {
    fprintf(stderr, "chiron: DirectSound is not implemented yet; running without sound\n");
    if (ds) *ds = NULL;
    return DSERR_NODRIVER;
}

// ordinal: DirectSoundEnumerateA 2
DWORD WINAPI DirectSoundEnumerateA(void *callback, void *context) { return 0; }
