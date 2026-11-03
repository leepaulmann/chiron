#include "patches.h"

#include "code.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Patches target terranx.exe 2.0 at its preferred base. Addresses follow the
// naming used by OpenSMACX and Thinker.

// game_data (0x5A6AC0) saves and loads games. It keeps the save header on the
// stack without initialising it, writes it to the file as-is, then checks the
// header's version field: below 11 it takes the old-format branch and copies
// the whole header over the global game settings, even when saving. With
// whatever happens to be on the stack this can overwrite VehCount and other
// globals, losing every unit and producing multi-gigabyte autosaves. Thinker
// fixes the same bug by zeroing the header and setting version 12; we do that
// with a stub run just before the header is read or written.
static int patch_game_data_header(struct pe_image *exe) {
    const uint32_t site = 0x5a6aeb, resume = 0x5a6af0;
    static const uint8_t original[] = {0x68, 0x1c, 0x03, 0x00, 0x00};  // push 0x31c
    if (!code_matches(exe, site, original, sizeof(original))) return -1;

    uint8_t *s = code_stub_alloc(48), *p = s;
    if (!s) return -1;
    *p++ = 0x9c;                                                   // pushfd: the next jump uses flags
    *p++ = 0x60;                                                   // pushad
    memcpy(p, (uint8_t[]){0x8d, 0xbd, 0x8c, 0xfc, 0xff, 0xff}, 6);  // lea edi, [ebp-0x374]
    p += 6;
    *p++ = 0x31, *p++ = 0xc0;                                      // xor eax, eax
    memcpy(p, (uint8_t[]){0xb9, 0xc7, 0x00, 0x00, 0x00}, 5);        // mov ecx, 0x31c / 4
    p += 5;
    *p++ = 0xfc;                                                   // cld
    *p++ = 0xf3, *p++ = 0xab;                                      // rep stosd
    memcpy(p, (uint8_t[]){0xc7, 0x45, 0x90, 0x0c, 0x00, 0x00, 0x00}, 7);  // mov dword [ebp-0x70], 12
    p += 7;
    *p++ = 0x61;                                                   // popad
    *p++ = 0x9d;                                                   // popfd
    memcpy(p, original, sizeof(original));                         // the instruction we replaced
    p += sizeof(original);
    code_put_rel32(p, 0xe9, (uint32_t)code_at(exe, resume));  // jmp back

    code_put_rel32(code_at(exe, site), 0xe9, (uint32_t)s);
    return 0;
}

struct patch {
    const char *name;
    int (*apply)(struct pe_image *exe);
};

static const struct patch patches[] = {
    {"initialise save header in game_data", patch_game_data_header},
};

void patches_apply(struct pe_image *exe) {
    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); i++)
        if (patches[i].apply(exe))
            fprintf(stderr, "chiron: patch \"%s\" not applied: unexpected code\n", patches[i].name);
}
