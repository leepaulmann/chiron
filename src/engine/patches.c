#include "patches.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

// Patches target terranx.exe 2.0 at its preferred base. Addresses follow the
// naming used by OpenSMACX and Thinker.

static uint8_t *stub_arena, *stub_end;

static uint8_t *stub_alloc(size_t n) {
    if (!stub_arena || stub_arena + n > stub_end) {
        stub_arena = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stub_arena == MAP_FAILED) return NULL;
        stub_end = stub_arena + 0x1000;
    }
    uint8_t *p = stub_arena;
    stub_arena += (n + 15) & ~15u;
    return p;
}

static void put_rel32(uint8_t *at, uint8_t opcode, uint32_t target) {
    at[0] = opcode;
    *(int32_t *)(at + 1) = (int32_t)(target - ((uint32_t)at + 5));
}

static int bytes_match(struct pe_image *exe, uint32_t addr, const uint8_t *expect, size_t n) {
    if (addr < exe->pref_base || addr + n > exe->pref_base + exe->size) return 0;
    uint8_t *p = exe->base + (addr - exe->pref_base);
    return !memcmp(p, expect, n);
}

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
    if (!bytes_match(exe, site, original, sizeof(original))) return -1;

    uint8_t *s = stub_alloc(48), *p = s;
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
    put_rel32(p, 0xe9, (uint32_t)exe->base + (resume - exe->pref_base));  // jmp back

    put_rel32(exe->base + (site - exe->pref_base), 0xe9, (uint32_t)s);
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
