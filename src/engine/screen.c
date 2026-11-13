// How the game's pictures reach the window. A Buffer is a DIB with a memory device context; the
// game's windows are drawn into the main Buffer (the back buffer, 0x9b7490) and the Buffers
// they keep, and the screen shows what is copied to the window's own device context with BitBlt
// or StretchBlt; Chiron's GDI (src/win32) draws that into the window's framebuffer and presents it.
// Reimplemented from terranx.exe 2.0's disassembly; the classes are called Win, Buffer and
// Palette in OpenSMACX (GPL-3.0-or-later).
//
// The window's device context is counted (win_get_dc, win_release_dc): the first user gets it, from
// the DirectDraw primary surface when the game has one (never under Chiron) and otherwise with GetDC
// for the game's window, and the last gives it back. Everything below that draws in the window takes
// it that way, nested, so a function that draws many times (tiling a background) gets it once.
//
//   win_flip                 the main Buffer, with a line of text and the cursor drawn over it, to the window
//   buffer_copy_to_window    a Buffer's area to the window, cut to the window's rectangle at 0x15c
//   buffer_copy_to_window_2  the same stretched, between two rectangles
//   win_on_paint             a window's background: a picture tiled or centred over a colour
//   win_window_line_raw      a line with a pen of a palette index
//   palette_set              the palette Windows is showing (the main Buffer's colours, and the system palette)
//   win_sync_palette         a window notes the serial number of the palette it last used
//
// Differences from OpenSMACX and Thinker: none of them has these functions; the names are the
// inventory's. Where a Buffer is on a DirectDraw surface (b->dd) the copies return 0x12 as the original
// does, and win_flip leaves everything to the original while the game has a primary surface (the
// DirectDraw paths are not used under Chiron and not reimplemented). The first IntersectRect the second
// copy makes, of the destination rectangle with the Buffer's, is dead (its answer and result are never
// read) and is left out. The original's LOGPEN for the line's pen leaves one field to stale stack; it is
// the width's y, which CreatePenIndirect doesn't read.
//
// Checks. Each function is checked in the game, in shadow mode, with the memory it writes put back:
// the window's framebuffer (hooks_window_pixels, so a blit that is missing or wrong is seen), the
// Buffers' objects, the counted device context and the palette globals. What Windows is given and
// can't be put back (the colour table of a Buffer's DC, the system palette, ValidateRect) goes
// through the Windows-call journal. The game's window class has its own DC, so GetDC gives the same one
// every time and what is selected into it stays (the palette, the pen, the position): its state is put back
// and compared where a function changes it.
#include "screen.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "bufimg.h"
#include "bufobj.h"
#include "hooks.h"
#include "palette.h"
#include "shadow.h"
#include "sprite.h"
#include "text.h"
#include "win.h"

#define SCREEN (&screen_module)
extern struct hook_module screen_module;

typedef struct game_buffer buffer_t;

enum {
    DC_NOW = 0x60,       // a Buffer's device context in use, 0 when none
    MEM_DC = 0x64,       // its memory device context
    DC_COUNT = 0x68,     // how many are using it
    PALETTE = 0xa4,      // the colours as an RGBQUAD table
    PAL_SERIAL = 0x4a4,  // the serial number of the palette it took them from
    PAL_FLAG = 0x57c,    // set once it has a palette
    PAL_PTR = 0x584,     // which palette it took them from
    ERR_NO_DC = 7,       // there is nothing to draw with
    ERR_NULL = 0x10,     // a pointer the call needs is NULL
    ERR_DD = 0x12,       // the Buffer is on a DirectDraw surface
    SRCCOPY = 0xcc0020,
    DD_GET_DC = 0x44,    // the DirectDraw surface's GetDC and ReleaseDC
    DD_RELEASE_DC = 0x68,
    DD_SET_ENTRIES = 0x18,  // the DirectDraw palette's SetEntries
};

static int32_t *at(void *b, int offset) { return (int32_t *)((uint8_t *)b + offset); }
static void *hwnd(void) { return (void *)(uintptr_t)*game_win_hwnd; }

// ---- The window's device context ---------------------------------------------------------------

// The device context of the window, counting one more user. The first one makes it: the surface's
// GetDC (which leaves it in game_dc) or GetDC for the window. Gives 0 if there is none.
void *win_get_dc(void) {
    if (*game_dc_count) {
        ++*game_dc_count;
        return *game_dc;
    }
    void *dc;
    struct game_com *dd = *game_dd_surface;
    if (dd) {
        ((int(GAME_STD *)(void *, void **))dd->vtable[DD_GET_DC / 4])(dd, game_dc);
        dc = *game_dc;
    } else {
        dc = game_get_dc(hwnd());
        *game_dc = dc;
    }
    if (dc) *game_dc_count = 1;
    return dc;
}

// One user less; the last gives the device context back.
void win_release_dc(void) {
    if (--*game_dc_count) return;
    struct game_com *dd = *game_dd_surface;
    if (dd) ((int(GAME_STD *)(void *, void *))dd->vtable[DD_RELEASE_DC / 4])(dd, *game_dc);
    else game_release_dc(hwnd(), *game_dc);
    *game_dc = NULL;
}

// ---- The palette -------------------------------------------------------------------------------

// Gives Windows the palette: the main Buffer is brought to its colours, and unless something holds the
// system palette (game_palette_hold) and the palette's serial number is the one already given, the
// system palette's colours 10 to 245 are animated to it. With DirectDraw the surface's palette is set instead.
// Gives 7 while there is no GDI palette.
int SCREEN_THIS palette_set(struct palette *p) {
    if (*game_ddraw) {
        struct game_com *s = *game_dd_palette;
        if (!s) return 0;
        ((int(GAME_STD *)(void *, int, int, int, void *))s->vtable[DD_SET_ENTRIES / 4])(s, 0, 0, 0x100, p);
        return 0;
    }
    if (!*game_palette_handle) return ERR_NO_DC;
    *game_palette_copy = p;
    buffer_sync_to_palette(game_win_back, p);
    if (!*game_palette_hold && *game_palette_serial != p->serial) {
        game_animate_palette((void *)(uintptr_t)*game_palette_handle, 10, 0xec, p->rgb[10]);
        *game_palette_serial = p->serial;
    }
    return 0;
}

// The window notes the serial number of the palette in use. (The original tells the window's
// palette function first, 0x5fe4f0, which does nothing.)
void SCREEN_THIS win_sync_palette(struct game_win *w) {
    if (w->pal_serial != (*game_palette_copy)->serial) w->pal_serial = (*game_palette_copy)->serial;
}

// The palette's colour cycling, one step. The Time object of the cycle calls this on its own thread (the
// cycle's slot is found again by its id: a stale call finds none and does nothing). The colours of the
// cycle (start, count) each take the next one's place and the first goes last (their flags stay), and
// the system palette is animated to them. Looking for the slot, a palette with all five slots in use and
// none of them the id's reads the word after the palette, as the original does.
void palette_timer_callback(int id, struct palette *p) {
    if (!p || *game_palette_hold) return;
    int pos = palette_get_pos(p, id);
    if (*(int32_t *)((uint8_t *)p + offsetof(struct palette, cycle) + pos * sizeof(struct palette_cycle)) != id) return;
    int start = p->cycle[pos].start, count = p->cycle[pos].count;
    uint8_t *e = p->rgb[start];
    uint8_t r = e[0], g = e[1], b = e[2];
    for (int k = 0; k < count - 1; k++) {
        e[4 * k] = e[4 * k + 4];
        e[4 * k + 1] = e[4 * k + 5];
        e[4 * k + 2] = e[4 * k + 6];
    }
    e[4 * count - 4] = r, e[4 * count - 3] = g, e[4 * count - 2] = b;
    game_animate_palette((void *)(uintptr_t)*game_palette_handle, (unsigned)start, (unsigned)count, e);
    *game_palette_serial = p->serial;
}

// ---- A Buffer copied to the window --------------------------------------------------------------

static void buffer_unlock(buffer_t *b) { ((void(__attribute__((thiscall)) *)(void *))b->vtable[1])(b); }

// A Buffer's colours made from `pal` (when its serial number differs): as buffer_sync_to_palette, but the
// Buffer also keeps the palette it took them from and notes it has one. The colour table of its memory
// device context is set with the Buffer's DC taken for the call.
static void take_palette(buffer_t *b, struct palette *pal) {
    if (!pal) return;
    if ((uint32_t)*at(b, PAL_SERIAL) != pal->serial) {
        *at(b, PAL_SERIAL) = (int32_t)pal->serial;
        palette_get_rgbquad(pal, (uint8_t *)b + PALETTE, 0, 0x100);
        if (buffer_get_hdc(b)) {
            game_set_dib_color_table((void *)(intptr_t)*at(b, DC_NOW), 0, 0x100, (uint8_t *)b + PALETTE);
            buffer_release_hdc(b, 1);
        }
    }
    *at(b, PAL_FLAG) = 1;
    *at(b, PAL_PTR) = (int32_t)(intptr_t)pal;
}

// The Buffer's memory DC counted in, as buffer_get_hdc does it for a Buffer with no DirectDraw surface (the
// copies have refused one already) but without letting go of the pixels a second time.
static void *take_dc(buffer_t *b) {
    *at(b, DC_NOW) = *at(b, MEM_DC);
    ++*at(b, DC_COUNT);
    return (void *)(intptr_t)*at(b, MEM_DC);
}

// Copies the area (sx, sy, cw, ch) of the Buffer to the window at (dx, dy): cut to the Buffer's width and
// height, then to the window's rectangle (the part cut off at the left or top moves the source along),
// and drawn in the window's DC after the palette is brought up to date. The area is made valid so the
// window isn't asked to paint it again. A DC that can't be had gives 7.
int SCREEN_THIS buffer_copy_to_window(buffer_t *b, struct game_win *w, int sx, int sy, int dx, int dy, int cw, int ch) {
    if (b->dd) return ERR_DD;
    if (!b->bits) return ERR_NO_DC;
    if (!w) return ERR_NULL;
    void *wdc = win_get_dc();
    if (!wdc) return ERR_NO_DC;
    if (b->base) buffer_unlock(b);
    void *bdc = take_dc(b);
    if (!bdc) {
        win_release_dc();
        return ERR_NO_DC;
    }
    if (sx + cw > b->width) cw = b->width - sx;
    if (ch + sy > -b->neg_height) ch = -b->neg_height - sy;
    int32_t r[4] = {dx, dy, cw + dx, ch + dy};
    if (!game_intersect_rect(r, r, w->clip)) {
        win_release_dc();
        buffer_release_hdc(b, 1);
        return 0;
    }
    sx += r[0] - dx;
    sy += r[1] - dy;
    int width = r[2] - r[0], height = r[3] - r[1];
    // The palette the Buffer's colours come from: its own, unless the game says otherwise.
    take_palette(b, !*game_copy_own_palette && *at(b, PAL_PTR) ? (struct palette *)(intptr_t)*at(b, PAL_PTR) : *game_palette_copy);
    win_sync_palette(w);
    palette_set(*game_palette_copy);
    int32_t ox = 0, oy = 0;
    win_client_to_screen_2(w, &ox, &oy);
    int32_t v[4] = {r[0] + ox, r[1] + oy, r[2] + ox, r[3] + oy};
    game_bit_blt(wdc, v[0], v[1], width, height, bdc, sx, sy, SRCCOPY);
    game_validate_rect(hwnd(), v);
    win_release_dc();
    buffer_release_hdc(b, 1);
    return 0;
}

// The stretched copy: the area `to` of the Buffer to the area `from` of the window (cut to the window's
// rectangle, but the Buffer's area is not cut to the Buffer's: StretchBlt's source is the whole of `to`). The
// palette is the game's own.
int SCREEN_THIS buffer_copy_to_window_2(buffer_t *b, struct game_win *w, const int32_t *from, const int32_t *to) {
    if (b->dd) return ERR_DD;
    if (!b->bits) return ERR_NO_DC;
    if (!w || !from || !to) return ERR_NULL;
    if (b->base) buffer_unlock(b);
    void *bdc = take_dc(b);
    if (!bdc) return ERR_NO_DC;
    void *wdc = win_get_dc();
    if (!wdc) {
        buffer_release_hdc(b, 1);
        return ERR_NO_DC;
    }
    int32_t r[4];
    if (!game_intersect_rect(r, from, w->clip)) {
        buffer_release_hdc(b, 1);
        win_release_dc();
        return 0;
    }
    take_palette(b, *game_palette_copy);
    win_sync_palette(w);
    palette_set(*game_palette_copy);
    int32_t ox = 0, oy = 0;
    win_client_to_screen_2(w, &ox, &oy);
    int32_t v[4] = {r[0] + ox, r[1] + oy, r[2] + ox, r[3] + oy};
    game_stretch_blt(wdc, v[0], v[1], v[2] - v[0], v[3] - v[1], bdc, to[0], to[1], to[2] - to[0], to[3] - to[1], SRCCOPY);
    game_validate_rect(hwnd(), v);
    win_release_dc();
    buffer_release_hdc(b, 1);
    return 0;
}

// ---- Drawing in the window's DC -----------------------------------------------------------------

// A line from (x1, y1) to (x2, y2), without its end point, in a pen of the given palette index, width and
// style. The window notes the palette's serial number first.
void SCREEN_THIS win_window_line_raw(struct game_win *w, int x2, int y2, int x1, int y1, int color, int width, int style) {
    if (!win_get_dc()) return;
    win_sync_palette(w);
    int32_t pen[4] = {style, width, 0, (color & 0xffff) | 0x1000000};  // style, width, (the y of the width), PALETTEINDEX
    void *hp = game_create_pen_indirect(pen);
    if (!hp) return;  // (the DC stays counted)
    void *old = game_select_object(*game_dc, hp);
    game_move_to_ex(*game_dc, x1, y1, NULL);
    game_line_to(*game_dc, x2, y2);
    game_select_object(*game_dc, old);
    game_delete_object(hp);
    win_release_dc();
}

// A window's background over `rect` (in its client coordinates): with no picture, the colour (an entry
// of the palette, the window's bg_color); with one, the picture tiled from the window's top left (or, for
// the tiles, shifted so the picture is centred) and, for a centred picture, the colour round it.
// Differences from what the original lets happen: none; a tile width or height of 0 divides by zero in both.
void SCREEN_THIS win_on_paint(struct game_win *w, const int32_t *rect) {
    void *dc = win_get_dc();
    const uint8_t *c = (*game_palette_copy)->rgb[w->bg_color];
    void *brush = game_create_solid_brush((uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0]);
    if (!brush) return;  // (the DC stays counted)
    buffer_t *pic = w->bg_pic;
    if (!pic) {
        game_fill_rect(dc, rect, brush);
    } else {
        int pw = pic->width, rows = -pic->neg_height;
        int iw = w->inner[2] - w->inner[0], ih = w->inner[3] - w->inner[1];
        int hx = (iw - pw) / 2, hy = (ih - rows) / 2;  // half of what is left, towards zero
        if (!w->bg_center) {
            // Tiles from the top left: the first one starts at most a tile before it, in the phase the centred
            // picture's would have.
            int x0 = hx > 0 ? hx % pw - pw : hx;
            int y0 = hy > 0 ? hy % rows - rows : hy;
            for (int y = y0; y < rect[3]; y += rows)
                for (int x = x0; x < rect[2]; x += pw) buffer_copy_to_window(pic, w, 0, 0, x, y, pic->width, -pic->neg_height);
        } else {
            int bottom = hy + rows;
            int32_t top[4] = {0, 0, iw, hy}, left[4] = {0, hy, hx, bottom}, right[4] = {hx + pw, hy, iw, bottom},
                    below[4] = {0, bottom, iw, ih};
            game_fill_rect(dc, top, brush);
            buffer_copy_to_window(pic, w, 0, 0, hx, hy, pic->width, -pic->neg_height);
            game_fill_rect(dc, left, brush);
            game_fill_rect(dc, right, brush);
            game_fill_rect(dc, below, brush);
        }
    }
    game_delete_object(brush);
    win_release_dc();
}

// ---- The flip ----------------------------------------------------------------------------------

SHADOW_DECLARE(void, win_flip, (const int32_t *rect))

// The text in the flip's box, lines separated by ^: drawn centred, a line below the other by the line height of the font.
static void flip_text(buffer_t *main, const int32_t *rect) {
    int32_t *box = game_flip_box;
    if (rect) buffer_set_clip(main, rect);
    int c = *game_flip_border;
    buffer_box_rect(main, box, c, c);
    box[0]++, box[2]--, box[1]++, box[3]--;
    buffer_fill_rect(main, box, *game_flip_fill);
    buffer_set_font(main, *game_flip_font, NULL, NULL, NULL);
    buffer_set_text_color(main, *game_flip_text_color, -1, 1, 1);
    if (strchr(*game_flip_text, '^')) {
        int y = box[1] + 1;
        char *s = *game_flip_text, *p;
        do {
            p = strchr(s, '^');
            if (p) *p = 0;
            buffer_write_cent_l_5(main, s, box[0], y, box[2] - box[0], (int)strlen(s));
            struct font *f = *game_flip_font;
            y += f->first < 0 ? f->ascent : f->height + f->first;
            if (p) *p = '^';
            s = p + 1;
        } while (p);
    } else if (*game_flip_text) {
        buffer_write_cent_l_7(main, *game_flip_text, box, (int)strlen(*game_flip_text));
    }
    box[0]--, box[2]++, box[1]--, box[3]++;
    if (rect) buffer_set_clip(main, main->full);
}

// Shows the main Buffer in the window: first the game's own callback (if it has one), the box of text (if
// there is any: its border, fill and lines; the rectangle becomes the Buffer's clip meanwhile) and the cursor sprite
// over the Buffer, then the whole Buffer, or the rectangle of it, copied to the window's DC with BitBlt after the
// palette is selected into the DC and realized. The game's DirectDraw paths are left to the original.
void win_flip(const int32_t *rect) {
    if (*game_dd_surface) {
        orig_win_flip(rect);
        return;
    }
    if (*game_flip_hook) (*game_flip_hook)();
    buffer_t *main = game_win_back;
    if (*game_flip_text) flip_text(main, rect);
    if (*game_win_overlay_on) {
        struct game_sprite *s = (struct game_sprite *)(uintptr_t)(uint32_t)*game_win_overlay_on;
        sprite_draw_2(s, main, s->key, game_win_overlay_pos[0], game_win_overlay_pos[1], 1, 1);
    }
    void *wdc = win_get_dc();
    if (!wdc) return;
    void *bdc = buffer_get_hdc(main);
    if (!bdc) {
        win_release_dc();
        return;
    }
    buffer_sync_to_palette(main, *game_palette_copy);
    if (!*game_ddraw && win_get_dc()) {
        game_select_palette(*game_dc, (void *)(uintptr_t)*game_palette_handle, 0);
        game_realize_palette(*game_dc);
        win_release_dc();
    }
    if (!rect) game_bit_blt(wdc, 0, 0, main->width, -main->neg_height, bdc, 0, 0, SRCCOPY);
    else game_bit_blt(wdc, rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], bdc, rect[0], rect[1], SRCCOPY);
    win_release_dc();
    buffer_release_hdc(main, 1);
}

// ---- Shadow mode -------------------------------------------------------------------------------
//
// The functions are checked in the game, with the window's framebuffer put back as well as the
// memory they write (see the top). Under DirectDraw, or for what is nested in a check, the original runs.

#define STD_NOT_DD (*game_ddraw || *game_dd_surface)

static int flip_hook_calls;  // how often the sweep's stand-in for the flip's callback is called
static void flip_hook(void) { flip_hook_calls++; }

SHADOW_DECLARE(void *, win_get_dc, (void))
SHADOW_DECLARE(void, win_release_dc, (void))
SHADOW_DECLARE_T(int, palette_set, (struct palette * p))
SHADOW_DECLARE_T(void, win_sync_palette, (struct game_win * w))
SHADOW_DECLARE_T(int, buffer_copy_to_window,
                 (buffer_t * b, struct game_win *w, int sx, int sy, int dx, int dy, int cw, int ch))
SHADOW_DECLARE_T(int, buffer_copy_to_window_2, (buffer_t * b, struct game_win *w, const int32_t *from, const int32_t *to))
SHADOW_DECLARE_T(void, win_window_line_raw,
                 (struct game_win * w, int x2, int y2, int x1, int y1, int color, int width, int style))
SHADOW_DECLARE_T(void, win_on_paint, (struct game_win * w, const int32_t *rect))

#define P(p) ((int)(uintptr_t)(p))

// The counted device context and the window's pixels; the palette the window and the main Buffer are on.
static void add(struct hook_writes *w, void *p, size_t n) { hooks_writes_add(w, p, n); }
static void writes_dc_users(struct hook_writes *w) {
    add(w, game_dc_count, 4);
    add(w, game_dc, 4);
}
static void writes_palette(struct hook_writes *w) {
    add(w, game_palette_copy, 4);
    add(w, game_palette_serial, 4);
    add(w, game_win_back, sizeof *game_win_back);
    hooks_writes_dc(w, (void *)(intptr_t)*at(game_win_back, MEM_DC));
}
// The state of the window's own DC (which GetDC gives each time).
static void writes_window_dc(struct hook_writes *w) {
    void *dc = game_get_dc(hwnd());
    if (!dc) return;
    hooks_writes_dc(w, dc);
    game_release_dc(hwnd(), dc);
}
static void writes_copy(struct hook_writes *w, buffer_t *b, struct game_win *win) {
    writes_dc_users(w);
    writes_palette(w);
    if (b) add(w, b, sizeof *b);
    if (win) add(w, &win->pal_serial, 4);
    hooks_writes_window(w, hwnd());
}

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v

SHADOW_WRITER_BODY(voidp, win_get_dc, (void), (), writes_dc_users(&w_), "%d", 0)
static void *shadow_win_get_dc(void) {
    if (hooks_nested || STD_NOT_DD) return orig_win_get_dc();
    if (shadow_bypass()) return win_get_dc();
    stats_win_get_dc.calls++;
    shadow_tick(SCREEN);
    return check_win_get_dc();
}

SHADOW_WRITER_BODY(void, win_release_dc, (void), (), writes_dc_users(&w_), "%d", 0)
static void shadow_win_release_dc(void) {
    if (hooks_nested || STD_NOT_DD) {
        orig_win_release_dc();
        return;
    }
    if (shadow_bypass()) {
        win_release_dc();
        return;
    }
    stats_win_release_dc.calls++;
    shadow_tick(SCREEN);
    check_win_release_dc();
}

#define SCREEN_ENTRY(ret, name, params, args)                                      \
    static ret SCREEN_THIS shadow_##name params {                                  \
        if (hooks_nested || STD_NOT_DD) return orig_##name args;                   \
        if (shadow_bypass()) return name args;                                     \
        stats_##name.calls++;                                                      \
        shadow_tick(SCREEN);                                                       \
        return check_##name args;                                                  \
    }
#define SCREEN_ENTRY_VOID(name, params, args)                                      \
    static void SCREEN_THIS shadow_##name params {                                 \
        if (hooks_nested || STD_NOT_DD) {                                          \
            orig_##name args;                                                      \
            return;                                                                \
        }                                                                          \
        if (shadow_bypass()) {                                                     \
            name args;                                                             \
            return;                                                                \
        }                                                                          \
        stats_##name.calls++;                                                      \
        shadow_tick(SCREEN);                                                       \
        check_##name args;                                                         \
    }

SHADOW_WRITER_BODY(int, palette_set, (struct palette * p), (p), (writes_palette(&w_)), "%d", P(p))
SCREEN_ENTRY(int, palette_set, (struct palette * p), (p))
// The callback runs on the timer's own thread, where a check can't hold the others still: the game's
// calls run the original, and the sweep checks Chiron's code.
SHADOW_DECLARE(void, palette_timer_callback, (int id, struct palette *p))
SHADOW_WRITER_BODY(void, palette_timer_callback, (int id, struct palette *p), (id, p),
                   ((p ? add(&w_, p, sizeof *p) : (void)0), add(&w_, game_palette_serial, 4)), "%d, %d", id, P(p))
static void shadow_palette_timer_callback(int id, struct palette *p) {
    if (!hooks_nested && !shadow_bypass()) stats_palette_timer_callback.calls++, shadow_tick(SCREEN);
    orig_palette_timer_callback(id, p);
}
SHADOW_WRITER_BODY(void, win_sync_palette, (struct game_win * w), (w), (add(&w_, &w->pal_serial, 4)), "%d", P(w))
SCREEN_ENTRY_VOID(win_sync_palette, (struct game_win * w), (w))
SHADOW_WRITER_BODY(int, buffer_copy_to_window, (buffer_t * b, struct game_win *w, int sx, int sy, int dx, int dy, int cw, int ch),
                   (b, w, sx, sy, dx, dy, cw, ch), (writes_copy(&w_, b, w)), "%d, %d, %d, %d, %d, %d, %d", P(w), sx, sy, dx, dy, cw, ch)
SCREEN_ENTRY(int, buffer_copy_to_window, (buffer_t * b, struct game_win *w, int sx, int sy, int dx, int dy, int cw, int ch),
             (b, w, sx, sy, dx, dy, cw, ch))
SHADOW_WRITER_BODY(int, buffer_copy_to_window_2, (buffer_t * b, struct game_win *w, const int32_t *from, const int32_t *to),
                   (b, w, from, to), (writes_copy(&w_, b, w)), "%d, %d", from ? from[0] : -1, to ? to[0] : -1)
SCREEN_ENTRY(int, buffer_copy_to_window_2, (buffer_t * b, struct game_win *w, const int32_t *from, const int32_t *to),
             (b, w, from, to))
SHADOW_WRITER_BODY(void, win_window_line_raw, (struct game_win * w, int x2, int y2, int x1, int y1, int color, int width, int style),
                   (w, x2, y2, x1, y1, color, width, style), (writes_dc_users(&w_), add(&w_, &w->pal_serial, 4), hooks_writes_window(&w_, hwnd()), writes_window_dc(&w_)),
                   "%d, %d, %d, %d, %d, %d, %d", x2, y2, x1, y1, color, width, style)
SCREEN_ENTRY_VOID(win_window_line_raw, (struct game_win * w, int x2, int y2, int x1, int y1, int color, int width, int style),
                  (w, x2, y2, x1, y1, color, width, style))
SHADOW_WRITER_BODY(void, win_on_paint, (struct game_win * w, const int32_t *rect), (w, rect),
                   (writes_copy(&w_, w->bg_pic, w)), "%d, %d", rect ? rect[2] : -1, rect ? rect[3] : -1)
SCREEN_ENTRY_VOID(win_on_paint, (struct game_win * w, const int32_t *rect), (w, rect))

// The flip: the main Buffer's object and pixels, the box and the text drawing's scratch, and the window.
void screen_writes_flip(struct hook_writes *w) {
    buffer_t *m = game_win_back;
    writes_dc_users(w);
    add(w, game_flip_box, 16);
    add(w, m, sizeof *m);
    hooks_writes_dc(w, (void *)(intptr_t)*at(m, MEM_DC));
    if (game_font_dc) hooks_writes_dc(w, *game_font_dc);
    // (from the first of the text's flying words to the line-break slack: the link table and the last x are inside)
    add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    if (m->bits && !m->dd && m->neg_height < 0) {
        int64_t pitch = m->pitch, rows = -(int64_t)m->neg_height;
        uint8_t *a = m->bits, *z = m->bits + pitch * (rows - 1);
        add(w, a < z ? a : z, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
    }
    add(w, &flip_hook_calls, 4);
    writes_window_dc(w);
    hooks_writes_window(w, hwnd());
}
SHADOW_WRITER_BODY(void, win_flip, (const int32_t *rect), (rect), screen_writes_flip(&w_), "%d", rect ? rect[0] : -1)
static void shadow_win_flip(const int32_t *rect) {
    if (hooks_nested || STD_NOT_DD) {
        orig_win_flip(rect);
        return;
    }
    if (shadow_bypass()) {
        win_flip(rect);
        return;
    }
    stats_win_flip.calls++;
    shadow_tick(SCREEN);
    check_win_flip(rect);
}

// ---- Sweep -------------------------------------------------------------------------------------
//
// Once a game turn every function is checked over a range of arguments on scratch Buffers and windows
// (made once from the game's own code, with their pixels scattered again each round), with the real
// window and main Buffer: the framebuffer, the main Buffer and the globals are put back after each check.
// The counted DC is tried not held and held (nested), the palette serial numbers equal and not, the Buffers
// with their own palette and without, and the text of the flip with no, one and many lines.

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

enum { NB = 4, NW = 3 };
static buffer_t sb[NB], empty_buf, dd_buf;
static struct game_win sw[NW];
static struct palette spal;
static int scratch_state;  // 0 not made, 1 made, -1 failed

static void scatter(buffer_t *b) {
    if (!b->bits || b->dd) return;
    for (int y = 0; y < -b->neg_height; y++)
        for (int x = 0; x < b->width; x++) b->bits[(intptr_t)b->pitch * y + x] = (uint8_t)rnd();
}

static int make_scratch(void) {
    if (scratch_state) return scratch_state > 0;
    scratch_state = 1;
    for (int i = 0; i < NB; i++) {
        memset(&sb[i], 0, sizeof sb[i]);
        game_buffer_ctor(&sb[i]);
        game_buffer_init(&sb[i], 8 + (i * 13) % 31, 7 + (i * 9) % 23, 0, 0);
        if (!sb[i].bits || sb[i].dd || sb[i].neg_height >= 0) scratch_state = -1;
    }
    memset(&empty_buf, 0, sizeof empty_buf);
    game_buffer_ctor(&empty_buf);
    dd_buf = sb[0];
    dd_buf.dd = (void *)1;
    return scratch_state > 0;
}

// A scratch window with random places and clip, noting the palette's serial number or another.
static struct game_win *random_window(int sw_w, int sw_h) {
    struct game_win *w = &sw[rnd_to(NW)];
    memset(w, 0, sizeof *w);
    w->flags = rnd_to(3) == 0 ? 0x20 | (rnd_to(2) ? 0x8000 : 0) : 0;
    w->flags2 = rnd_to(2) ? WIN_F2_FRAME : 0;
    if ((w->flags & 0x20) && w != &sw[0]) w->parent = &sw[0];
    for (int k = 0; k < 4; k++) w->inner[k] = rnd_to(20) - 4, w->outer[k] = rnd_to(20) - 4;
    w->inner[2] = w->inner[0] + 20 + rnd_to(90), w->inner[3] = w->inner[1] + 15 + rnd_to(70);
    w->clip[0] = rnd_to(sw_w / 3) - 10, w->clip[1] = rnd_to(sw_h / 3) - 10;
    w->clip[2] = w->clip[0] + 10 + rnd_to(sw_w), w->clip[3] = w->clip[1] + 10 + rnd_to(sw_h);
    w->pal_serial = rnd_to(2) ? (*game_palette_copy)->serial : (uint32_t)rnd();
    w->bg_color = rnd_to(256);
    return w;
}

// A Buffer with scattered pixels, its own palette or not, and a colour table up to date or not.
static buffer_t *random_buffer(void) {
    buffer_t *b = &sb[rnd_to(NB)];
    scatter(b);
    *at(b, PAL_SERIAL) = rnd_to(2) ? (int32_t)(*game_palette_copy)->serial : (int32_t)rnd();
    *at(b, PAL_PTR) = rnd_to(2) ? 0 : (int32_t)(intptr_t)&spal;
    *at(b, PAL_FLAG) = rnd_to(2);
    b->base = rnd_to(8) == 0 ? b->bits : NULL;
    return b;
}

static void random_rect(int32_t *r, int w, int h) {
    r[0] = rnd_to(w + 10) - 5, r[1] = rnd_to(h + 10) - 5;
    r[2] = r[0] + rnd_to(w + 6) - 2, r[3] = r[1] + rnd_to(h + 6) - 2;
}

static void sweep_copies(int sw_w, int sw_h) {
    for (int k = 0; k < 14; k++) {
        buffer_t *b = rnd_to(12) == 0 ? (rnd_to(2) ? &empty_buf : &dd_buf) : random_buffer();
        struct game_win *w = random_window(sw_w, sw_h);
        int bw = b->width > 0 ? b->width : 10, bh = b->neg_height < 0 ? -b->neg_height : 10;
        int sx = rnd_to(bw + 8) - 4, sy = rnd_to(bh + 8) - 4, dx = rnd_to(sw_w / 2) - 10, dy = rnd_to(sw_h / 2) - 10;
        int cw = rnd_to(bw + 8) - 2, ch = rnd_to(bh + 8) - 2;
        if (k % 4 == 0) sx = sy = 0, cw = bw, ch = bh;
        *game_copy_own_palette = rnd_to(2);
        *game_dc_count = 0;
        check_buffer_copy_to_window(b, rnd_to(20) ? w : NULL, sx, sy, dx, dy, cw, ch);
        int32_t from[4], to[4];
        random_rect(from, sw_w / 2, sw_h / 2);
        random_rect(to, bw, bh);
        if (rnd_to(2)) to[0] = rnd_to(3), to[1] = rnd_to(3), to[2] = bw - rnd_to(3), to[3] = bh - rnd_to(3);
        int pick = rnd_to(16);
        check_buffer_copy_to_window_2(b, pick == 0 ? NULL : w, pick == 1 ? NULL : from, pick == 2 ? NULL : to);
    }
}

static void sweep_paint(int sw_w, int sw_h) {
    for (int k = 0; k < 10; k++) {
        struct game_win *w = random_window(sw_w, sw_h);
        w->bg_pic = rnd_to(4) ? random_buffer() : NULL;
        w->bg_center = rnd_to(2);
        *game_copy_own_palette = rnd_to(2);
        *game_dc_count = 0;
        int32_t r[4] = {rnd_to(30) - 10, rnd_to(20) - 10, 0, 0};
        r[2] = r[0] + rnd_to(90), r[3] = r[1] + rnd_to(70);
        if (k == 3) r[2] = r[0], r[3] = r[1];
        check_win_on_paint(w, r);
        check_win_window_line_raw(w, rnd_to(sw_w / 2), rnd_to(sw_h / 2), rnd_to(sw_w / 2), rnd_to(sw_h / 2), rnd_to(256),
                                  rnd_to(4), rnd_to(7));
    }
}

// The main Buffer's pixels made up, so that a flip changes the window (it shows what the last flip put there, which
// is the Buffer's own pixels); the real ones are put back after the sweep.
static uint8_t *main_saved;
static size_t main_bytes;
static uint8_t *main_base(buffer_t *m) {
    return m->pitch < 0 ? m->bits + (int64_t)m->pitch * (-(int64_t)m->neg_height - 1) : m->bits;
}
static void main_save(buffer_t *m) {
    main_bytes = (size_t)(m->pitch < 0 ? -m->pitch : m->pitch) * (size_t)-m->neg_height;
    free(main_saved);
    main_saved = malloc(main_bytes);
    if (main_saved) memcpy(main_saved, main_base(m), main_bytes);
}
static void main_scramble(buffer_t *m) {
    if (!main_saved) return;
    uint8_t *p = main_base(m);
    for (size_t i = 0; i < main_bytes; i++) p[i] = (uint8_t)rnd();
}
static void main_restore(buffer_t *m) {
    if (main_saved) memcpy(main_base(m), main_saved, main_bytes);
}

static void sweep_flip(void) {
    static char text[80];
    static const char *const lines[] = {"", "One line", "Two^lines", "A^^B", "^leading", "trailing^", "Three^short^lines here and now", "x"};
    struct font *font = game_font_default ? (struct font *)*game_font_default : NULL;
    static struct game_sprite sp;
    static uint8_t spdata[48];
    char *saved_text = *game_flip_text;
    void (*saved_hook)(void) = *game_flip_hook;
    int32_t saved_box[4], saved_pos[2], saved_over = *game_win_overlay_on;
    memcpy(saved_box, game_flip_box, 16), memcpy(saved_pos, game_win_overlay_pos, 8);
    struct font *saved_font = *game_flip_font;
    int32_t saved_border = *game_flip_border, saved_color = *game_flip_text_color, saved_fill = *game_flip_fill;
    int w = game_win_back->width, h = -game_win_back->neg_height;
    sp.data = spdata, sp.key = (uint8_t)rnd(), sp.width = sp.stride = 8, sp.height = 6, sp.full_w = 8, sp.full_h = 6;
    main_save(game_win_back);
    for (int k = 0; k < 10; k++) {
        main_scramble(game_win_back);
        for (int i = 0; i < 48; i++) spdata[i] = (uint8_t)rnd();
        *game_dc_count = 0;
        flip_hook_calls = 0;
        *game_flip_hook = rnd_to(2) ? flip_hook : NULL;
        if (font && k % 5 != 4) {
            strcpy(text, lines[rnd_to((int)(sizeof lines / sizeof *lines))]);
            *game_flip_text = text;
            *game_flip_font = font;
            game_flip_box[0] = rnd_to(w / 2), game_flip_box[1] = rnd_to(h / 2);
            game_flip_box[2] = game_flip_box[0] + 20 + rnd_to(w / 2), game_flip_box[3] = game_flip_box[1] + 10 + rnd_to(60);
            *game_flip_border = rnd_to(256), *game_flip_text_color = rnd_to(256), *game_flip_fill = rnd_to(256);
        } else {
            *game_flip_text = NULL;
        }
        *game_win_overlay_on = rnd_to(2) ? (int32_t)(intptr_t)&sp : 0;
        game_win_overlay_pos[0] = rnd_to(w) - 8, game_win_overlay_pos[1] = rnd_to(h) - 8;
        int32_t r[4];
        r[0] = rnd_to(w / 2), r[1] = rnd_to(h / 2), r[2] = r[0] + rnd_to(w / 2), r[3] = r[1] + rnd_to(h / 2);
        check_win_flip(rnd_to(2) ? r : NULL);
    }
    main_restore(game_win_back);
    *game_flip_text = saved_text, *game_flip_hook = saved_hook, *game_flip_font = saved_font;
    *game_flip_border = saved_border, *game_flip_text_color = saved_color, *game_flip_fill = saved_fill;
    memcpy(game_flip_box, saved_box, 16), memcpy(game_win_overlay_pos, saved_pos, 8);
    *game_win_overlay_on = saved_over;
}

static void screen_sweep(void) {
    int size;
    if (*game_ddraw || *game_dd_surface || !*game_win_hwnd || !hooks_window_pixels || !hooks_window_pixels(hwnd(), &size)) return;
    if (!*game_palette_copy || !*game_palette_handle || !game_win_back->bits || game_win_back->dd) return;
    if (!make_scratch()) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x1d872b41u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    int sw_w = game_win_back->width, sw_h = -game_win_back->neg_height;
    int32_t saved_count = *game_dc_count, saved_own = *game_copy_own_palette;
    void *saved_dc = *game_dc;
    uint32_t saved_serial = *game_palette_serial, saved_hold = *game_palette_hold;
    for (int i = 0; i < 0x400; i++) ((uint8_t *)&spal)[i] = (uint8_t)rnd();
    spal.serial = rnd() | 1;
    // Windows is given a made-up palette handle for the sweep, which it rejects: the animations the originals
    // make with the made-up colours change nothing real (the calls are compared by the journal all the same).
    uint32_t saved_handle = *game_palette_handle;
    *game_palette_handle = 1;
    for (int round = 0; round < 3; round++) {
        sweep_copies(sw_w, sw_h);
        sweep_paint(sw_w, sw_h);
    }
    sweep_flip();
    // The counted DC: not held, held once (nested), held twice.
    for (int k = 0; k < 6; k++) {
        *game_dc_count = 0;
        *game_dc = NULL;
        void *dc = check_win_get_dc();
        if (dc && rnd_to(2)) {
            *game_dc_count = 1 + rnd_to(2);
            *game_dc = dc;
            check_win_get_dc();
            check_win_release_dc();
            *game_dc_count = 1;
            check_win_release_dc();
        }
    }
    // The palette: with and without the hold, the serial numbers equal and not, on the game's palette and a made-up one.
    for (int k = 0; k < 8; k++) {
        struct palette *p = k % 3 == 2 ? &spal : *game_palette_copy;
        *game_palette_hold = rnd_to(3) == 0;
        *game_palette_serial = rnd_to(2) ? p->serial : (uint32_t)rnd();
        check_palette_set(p);
        struct game_win *w = random_window(sw_w, sw_h);
        check_win_sync_palette(w);
    }
    // The colour cycle: a made-up palette with cycles (and after the five a word of its own to read), the
    // id in a slot, in none, and in a free one, the hold on, and the whole range of colours.
    static struct {
        struct palette p;
        int32_t after[4];
    } cyc;
    for (int k = 0; k < 12; k++) {
        memcpy(&cyc.p, &spal, sizeof cyc.p);
        for (int i = 0; i < 5; i++) {
            cyc.p.cycle[i].id = rnd_to(4) ? rnd_to(8) : -1;
            cyc.p.cycle[i].start = (uint8_t)(10 + rnd_to(200));
            cyc.p.cycle[i].count = (uint8_t)(k % 4 == 3 ? rnd_to(2) : rnd_to(40));
        }
        cyc.after[0] = rnd_to(2) ? (int32_t)rnd_to(8) : -1;
        int id = rnd_to(8);
        if (k % 3 == 0) {
            int slot = rnd_to(5);
            for (int i = 0; i < slot; i++) cyc.p.cycle[i].id = 20 + i;  // (the slots fill from the first)
            cyc.p.cycle[slot].id = id;
        }
        *game_palette_hold = rnd_to(6) == 0;
        *game_palette_serial = rnd_to(2) ? cyc.p.serial : (uint32_t)rnd();
        check_palette_timer_callback(id, rnd_to(20) ? &cyc.p : NULL);
    }
    *game_palette_handle = saved_handle;
    *game_dc_count = saved_count, *game_dc = saved_dc, *game_copy_own_palette = saved_own;
    *game_palette_serial = saved_serial, *game_palette_hold = saved_hold;
    hooks_sweeping = sweeping;
}

static const struct hook screen_hooks[] = {
    // The game's calls run the original in normal mode too, for now: with Chiron's version (the same steps
    // as the original's) the popups scene's terrain came out in its other look in 3 runs of 5, never with the
    // original's own code (6 of 6 alike). The sweep still checks Chiron's code against the original's.
    {0x5fead0, (const uint8_t[]){0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x85, 0xed, 0x56}, 8, (void *)shadow_palette_timer_callback,
     (void *)shadow_palette_timer_callback, (void **)&orig_palette_timer_callback, &stats_palette_timer_callback},
    HOOK(0x5ec690, win_get_dc, 0xa1, 0xb0, 0x3a, 0x9b, 0x00, 0x85, 0xc0),  // mov eax, [0x9b3ab0]; test eax, eax
    HOOK(0x5ec6f0, win_release_dc, 0xa1, 0xb0, 0x3a, 0x9b, 0x00, 0x48, 0xa3, 0xb0, 0x3a, 0x9b, 0x00),  // mov eax, [..]; dec eax; mov [..], eax
    HOOK(0x5f2c60, win_sync_palette, 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x80, 0x81, 0x9b, 0x00),  // push esi; mov esi, ecx; mov ecx, [0x9b8180]
    HOOK(0x5fe460, palette_set, 0xa1, 0x94, 0xc4, 0x9b, 0x00, 0x56, 0x85, 0xc0, 0x8b, 0xf1),  // mov eax, [0x9bc494]; push esi; test eax, eax; mov esi, ecx
    HOOK(0x5d9be0, buffer_copy_to_window, 0x83, 0xec, 0x20, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57),  // sub esp, 0x20; push ebx, ebp, esi; mov esi, ecx; push edi
    HOOK(0x5d9fb0, buffer_copy_to_window_2, 0x83, 0xec, 0x30, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x33, 0xed),  // sub esp, 0x30; push ebx, ebp, esi; mov esi, ecx; xor ebp, ebp
    HOOK(0x5f2ac0, win_window_line_raw, 0xa1, 0xb0, 0x3a, 0x9b, 0x00, 0x83, 0xec, 0x10, 0x85, 0xc0, 0x56, 0x8b, 0xf1),  // mov eax, [0x9b3ab0]; sub esp, 0x10; test eax, eax; push esi; mov esi, ecx
    HOOK(0x5f54e0, win_on_paint, 0xa1, 0xb0, 0x3a, 0x9b, 0x00, 0x83, 0xec, 0x30, 0x85, 0xc0, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xd9),
    HOOK(0x5efd20, win_flip, 0xa1, 0x48, 0x7a, 0x9b, 0x00, 0x83, 0xec, 0x14, 0x85, 0xc0),  // mov eax, [0x9b7a48]; sub esp, 0x14; test eax, eax
};
struct hook_module screen_module = {"screen", screen_hooks, HOOK_COUNT(screen_hooks), screen_sweep, NULL, -1};
