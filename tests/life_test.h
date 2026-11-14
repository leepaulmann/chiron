// Helpers for the life cycle tests (constructors, destructors and what allocates) in the engine_<module>.c
// tests: a made-up game image whose every word is different, so that a constructor that reads the wrong
// global or writes the wrong place shows, a free that counts what it is given, and word access.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"

// The made-up image (from 0x400000): every word of the game's data holds a number of its own.
static __attribute__((unused)) uint8_t life_image[0x600000];
static __attribute__((unused)) void life_image_init(void) {
    game_test_image = life_image;
    for (uint32_t a = 0x400000; a < 0x9c0000; a += 4) *(uint32_t *)(life_image + (a - 0x400000)) = a * 2654435761u + 7;
}
// The word of the image at an address, and an address as the objects hold it.
#define IMGW(addr) (*(uint32_t *)game_at(addr))
#define IMGVA(addr) game_va(addr)
// A word of an object.
#define OW(o, off) (*(uint32_t *)((uint8_t *)(o) + (off)))

// The virtual base table at `addr`: where the first and second base lie.
static __attribute__((unused)) void life_vbtable(uint32_t addr, uint32_t first, uint32_t second) {
    IMGW(addr + 4) = first;
    IMGW(addr + 8) = second;
}

// A free that counts what it is given (and frees it, so the blocks must come from malloc).
static __attribute__((unused)) int life_freed;
static __attribute__((unused)) void *life_freed_list[64];
static __attribute__((unused)) void life_free(void *p) {
    if (life_freed < 64) life_freed_list[life_freed] = p;
    life_freed++;
    free(p);
}
static __attribute__((unused)) void *life_mem_get(size_t n) { return calloc(1, n); }
static __attribute__((unused)) int life_freed_has(void *p) {
    for (int i = 0; i < life_freed && i < 64; i++)
        if (life_freed_list[i] == p) return 1;
    return 0;
}

// The stand-ins a Buffer's constructor, close and destructor need when it has no pixels: no DirectDraw object,
// no palette or font, and Spot lists that do nothing (the free is the counting one).
#define LT_THIS __attribute__((thiscall))
static void LT_THIS life_spot(void *s) { memset(s, 0, 12); }
static void LT_THIS life_spot_init(void *s, int n) { (void)s, (void)n; }
static __attribute__((unused)) void life_buffer_standins(void) {
    static void *ddraw, *palette, *font, *vt[4];
    static int32_t default_520;
    game_ddraw = &ddraw, game_palette_now = &palette, game_font_default = &font;
    game_buffer_vtable = vt, game_buffer_520_default = &default_520;
    game_spot_ctor = life_spot, game_spot_dtor = life_spot, game_spot_init = life_spot_init;
    game_free = life_free, game_mem_get = life_mem_get;
}

// The stand-ins a Time object's close needs (it stops a timer it never started, and runs the game's pump).
static uint32_t __attribute__((stdcall)) life_kill_event(uint32_t id) { return (void)id, 0; }
static int __attribute__((stdcall)) life_kill_timer(uint32_t hwnd, uint32_t id) { return (void)hwnd, (void)id, 1; }
static void __attribute__((cdecl)) life_flush(void) {}
static __attribute__((unused)) void life_time_standins(void) {
    static uint32_t flag, hwnd = 1;
    game_time_kill_event = life_kill_event, game_kill_timer = life_kill_timer, game_time_flush = life_flush;
    game_time_flush_flag = &flag, game_win_hwnd = &hwnd;
}
