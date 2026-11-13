// Shared pieces of the checks of drawing into a Buffer: the pixels of its rectangle, the state text drawing
// leaves behind (in globals and in the device contexts of Chiron's GDI), and the wrapper that runs the check
// as the game calls the function (but leaves a DirectDraw surface to the original, which no check can put back).
#pragma once

#include <stdint.h>

#include "game.h"
#include "hooks.h"
#include "shadow.h"

static inline struct game_buffer *dc_win_buf(void *w) { return (struct game_buffer *)((uint8_t *)w + 0x444); }

// The Buffer object and the pixels of its rows (a DIB's; a surface can't be put back).
static inline void dc_writes_pixels(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, b, sizeof *b);
    if (!b->bits || b->dd) return;
    int64_t rows = -(int64_t)b->neg_height, pitch = b->pitch;
    if (rows <= 0 || pitch == 0) return;
    uint8_t *a = b->bits, *c = b->bits + pitch * (rows - 1);
    hooks_writes_add(w, a < c ? a : c, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}

// The scratch the text calls leave in globals, and the device contexts they draw through.
static inline void dc_writes_text_state(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    int32_t mem = *(int32_t *)((uint8_t *)b + 0x64), now = *(int32_t *)((uint8_t *)b + 0x60);
    hooks_writes_dc(w, (void *)(intptr_t)mem);
    if (now != mem) hooks_writes_dc(w, (void *)(intptr_t)now);
    if (game_font_dc) hooks_writes_dc(w, *game_font_dc);
}

static inline int dc_win_dd(struct game_win *w) { return dc_win_buf(w)->dd || (w->parent && dc_win_buf(w->parent)->dd); }

#define DRAW_CHECKED(module, name, params, args, dd_expr, writes, fmt, ...)                 \
    SHADOW_DECLARE_T(void, name, params)                                                  \
    SHADOW_WRITER_BODY(void, name, params, args, writes, fmt, __VA_ARGS__)                \
    static void THISCALL shadow_##name params {                                           \
        if (hooks_nested || (dd_expr)) {                                                  \
            orig_##name args;                                                             \
            return;                                                                       \
        }                                                                                 \
        if (shadow_bypass()) {                                                            \
            name args;                                                                    \
            return;                                                                       \
        }                                                                                 \
        stats_##name.calls++;                                                             \
        shadow_tick(module);                                                              \
        check_##name args;                                                                \
    }
