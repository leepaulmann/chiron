#include "patches.h"

#include "code.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

// wants_to_attack (0x55BC80) counts the bases each faction has on the
// continents the two factions share, in an array of seven on its stack that
// it never clears: it takes the maximum of each entry and what it finds.
// What decides war then depends on whatever was on the stack, so the same
// question asked from two places can get two answers. Thinker clears the
// array too. Asked about faction 0 (as the game does), it reads the word
// below the array as well. The stub clears the eight words once the frame
// is set up and the registers saved (after the prologue, which faction.c's
// hook takes over).
static int patch_wants_to_attack_counts(struct pe_image *exe) {
    const uint32_t site = 0x55bc89, resume = 0x55bc8f;
    static const uint8_t original[] = {0x53, 0x56, 0x57, 0x8b, 0x7d, 0x08};  // push ebx, esi, edi; mov edi, [ebp+8]
    if (!code_matches(exe, site, original, sizeof(original))) return -1;

    uint8_t *s = code_stub_alloc(sizeof(original) + 8 * 10 + 5), *p = s;
    if (!s) return -1;
    memcpy(p, original, sizeof(original));  // the instructions we replaced
    p += sizeof(original);
    for (int32_t off = -0x134; off < -0x130 + 7 * 4; off += 4) {
        *p++ = 0xc7;  // mov dword [ebp+off], 0
        *p++ = 0x85;
        memcpy(p, &off, 4);
        p += 4;
        memset(p, 0, 4);
        p += 4;
    }
    code_put_rel32(p, 0xe9, (uint32_t)code_at(exe, resume));  // jmp back

    // The site is 6 bytes; the jump takes 5, and the 6th is never reached.
    code_put_rel32(code_at(exe, site), 0xe9, (uint32_t)s);
    return 0;
}

struct patch {
    const char *name;
    int (*apply)(struct pe_image *exe);
};

// For scripted scenes (tools/snapcheck.py) every run must play out the same.
// With CHIRON_FIXED_TIME set, the five places that seed the game's random
// numbers from the millisecond timer (timeGetTime) at startup, around map
// generation and after a game is set up get a constant instead, so Quick
// Start builds the same map in the original's run and in Chiron's.
static int patch_fixed_seeds(struct pe_image *exe) {
    if (!getenv("CHIRON_FIXED_TIME")) return 0;
    static const uint8_t call_esi[] = {0xff, 0xd6};                                // call esi
    static const uint8_t call_tgt[] = {0xff, 0x15, 0x68, 0x93, 0x66, 0x00};        // call [timeGetTime]
    static const uint8_t xor_eax[] = {0x31, 0xc0};                                 // xor eax, eax
    static const uint8_t mov_eax[] = {0xb8, 0x2a, 0x00, 0x00, 0x00, 0x90};         // mov eax, 42; nop
    static const uint32_t short_sites[] = {0x58f304, 0x58f30f};
    static const uint32_t long_sites[] = {0x5a0040, 0x5a0607, 0x5c5a7b};
    for (size_t i = 0; i < 2; i++)
        if (!code_matches(exe, short_sites[i], call_esi, sizeof(call_esi))) return -1;
    for (size_t i = 0; i < 3; i++)
        if (!code_matches(exe, long_sites[i], call_tgt, sizeof(call_tgt))) return -1;
    for (size_t i = 0; i < 2; i++) memcpy(code_at(exe, short_sites[i]), xor_eax, sizeof(xor_eax));
    for (size_t i = 0; i < 3; i++) memcpy(code_at(exe, long_sites[i]), mov_eax, sizeof(mov_eax));
    return 0;
}

static const struct patch patches[] = {
    {"initialise save header in game_data", patch_game_data_header},
    {"clear base counts in wants_to_attack", patch_wants_to_attack_counts},
    {"fixed random seeds (CHIRON_FIXED_TIME)", patch_fixed_seeds},
};

void patches_apply(struct pe_image *exe) {
    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); i++)
        if (patches[i].apply(exe))
            fprintf(stderr, "chiron: patch \"%s\" not applied: unexpected code\n", patches[i].name);
}
