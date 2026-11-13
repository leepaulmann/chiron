// The game's Buffer class (OpenSMACX's name): setting a Buffer up, cutting it to a clip
// rectangle, and closing it. Drawing into one is in buffer.c and text.c.
//
// A Buffer with pixels of its own is an 8-bit DIB section selected into a memory device
// context, with a BITMAPINFO (header and 256 colours) at 0x7c. A Buffer may instead draw
// into a DirectDraw surface (0x58, with a clipper at 0x5c) when the game has a DirectDraw
// object; Chiron never creates one for Buffers, so those paths are left with the original.
//
// These functions make and free Windows objects (DCs, DIB sections, clip regions) and use
// the game's heap (each Buffer holds a hot-spot list), so a shadow check can't run the
// original and then rewind it. Instead the sweep builds two Buffers side by side, runs the
// original on one and Chiron's code on the other, and compares them: every field, with
// handles and heap pointers compared by whether they are set, and the two DCs' state and
// bitmaps by content (hooks_dc_same). In shadow mode the game's own calls run the original.
//
// The tiling functions (tile, tile_mask, tile_2, and box_sprite which draws a frame with them)
// are here too: each cuts the drawing to a clip rectangle through buffer_set_clip, which makes
// and frees a Windows region, so they are checked on twins as well (the pixels of both are
// compared besides). They lay a picture (a Buffer, or a Sprite) over an area from a corner
// that moves with the offset asked for, as many times as it takes to cover the area; the
// drawing of each copy is the code of buffer.c and sprite.c.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "bufobj.h"
#include "game.h"
#include "hooks.h"
#include "palette.h"
#include "shadow.h"
#include "sprite.h"

#define BUFOBJ (&bufobj_module)
extern struct hook_module bufobj_module;

typedef struct game_buffer buffer_t;

// The originals, reached through their trampolines.
SHADOW_DECLARE_T(void *, buffer_ctor, (buffer_t * b))
SHADOW_DECLARE_T(void, buffer_dtor, (buffer_t * b))
SHADOW_DECLARE_T(void, buffer_close, (buffer_t * b))
SHADOW_DECLARE_T(void, buffer_clear_links, (buffer_t * b))
SHADOW_DECLARE_T(int, buffer_init, (buffer_t * b, int w, int h, int flags, void **surface))
SHADOW_DECLARE_T(int, buffer_resize, (buffer_t * b, int w, int h))
SHADOW_DECLARE_T(int, buffer_set_clip, (buffer_t * b, const int32_t *rect))
SHADOW_DECLARE_T(int, buffer_set_clip_2, (buffer_t * b, int x, int y, int w, int h))
SHADOW_DECLARE_T(int, buffer_tile_mask, (buffer_t * b, buffer_t *src, int sx, int sy, int x, int y, int w, int h, int key))
SHADOW_DECLARE_T(int, buffer_tile, (buffer_t * b, buffer_t *src, int sx, int sy, int x, int y, int w, int h))
SHADOW_DECLARE_T(int, buffer_tile_2, (buffer_t * b, struct game_sprite *spr, int sx, int sy, int x, int y, int w, int h))
SHADOW_DECLARE_T(void, buffer_box_sprite, (buffer_t * b, const int32_t *rect, const struct game_box_frame *f))

static int32_t *at(buffer_t *b, int offset) { return (int32_t *)((uint8_t *)b + offset); }

// Where a Buffer keeps what this file sets up.
enum {
    FLAGS = 0x1c,      // as given to init; bit 2: the surface comes from the caller
    CLIP = 0x20,       // left, top, right, bottom: what drawing is clipped to
    FULL = 0x30,       // the whole surface
    BASE = 0x50,       // the pixels while locked
    BITS = 0x54,       // the DIB's pixels
    SURFACE = 0x58,    // a DirectDraw surface
    CLIPPER = 0x5c,    // its DirectDraw clipper
    DC_NOW = 0x60,     // the device context in use, 0 when none
    MEM_DC = 0x64,     // the memory device context
    DC_COUNT = 0x68,   // how many users the device context has
    REGION = 0x70,     // the clip region selected into the device context, if not the whole surface
    OLD_BITMAP = 0x74, // what the memory device context held before the DIB
    DIB = 0x78,        // the DIB section
    INFO = 0x7c,       // BITMAPINFOHEADER: size 0x28, width (0x80), minus the height (0x84), ...
    PIXELS = 0x90,     // biSizeImage: width times height
    COLORS_USED = 0x9c,
    PALETTE = 0xa4,    // 256 RGBQUADs
    PITCH = 0x4a8,     // bytes from one row to the next
    SPOTS = 0x4b0,     // the hot-spot list (Spot: pointer, count, size)
    SPOT_COUNT = 0x4ac,
    HEAP = 0x4bc,      // 20 blocks of the game's heap, freed when the Buffer is closed
    FONTS = 0x52c,     // fonts 0-3
    TEXT_COLORS = 0x53c,
};

typedef void(THISCALL *buffer_method)(void *);
typedef int(__attribute__((stdcall)) * dd_release)(void *obj);
typedef int(__attribute__((stdcall)) * dd_get_dc)(void *surface, int32_t *dc);
typedef int(__attribute__((stdcall)) * dd_release_dc)(void *surface, int32_t dc);

static void *dd_method(void *obj, int offset) { return (*(void ***)obj)[offset / 4]; }

// The Buffer's second virtual method, which lets go of the pixels.
static void unlock(buffer_t *b) { ((buffer_method)b->vtable[1])(b); }

// The fields a new or closed Buffer starts with for drawing text: slots 0-3 of the four text
// modes (colour, shadow colour, shadow x, shadow y) are 0, -1, 2, 2 for mode 0 and -1, -1, 2, 2
// for the others; fonts 1-3 none.
static void reset_text(buffer_t *b) {
    *at(b, 0x50c) = -1;
    for (int o = 0x510; o <= 0x51c; o += 4) *at(b, o) = 0;
    *at(b, 0x520) = *game_buffer_520_default;
    *at(b, FONTS) = (int32_t)(intptr_t)*game_font_default;
    for (int m = 0; m < 4; m++) {
        if (m) *at(b, FONTS + 4 * m) = 0;
        *at(b, TEXT_COLORS + 4 * m) = m ? -1 : 0;
        *at(b, TEXT_COLORS + 0x10 + 4 * m) = -1;
        *at(b, TEXT_COLORS + 0x20 + 4 * m) = 2;
        *at(b, TEXT_COLORS + 0x30 + 4 * m) = 2;
    }
    *at(b, 0x57c) = 0;
    *((uint8_t *)b + 0x580) = 0;
    *at(b, 0x584) = 0;
}

void *THISCALL buffer_ctor(buffer_t *b) {
    game_spot_ctor(at(b, SPOTS));
    memset(at(b, HEAP), 0, 20 * 4);
    b->vtable = game_buffer_vtable;
    static const int zero[] = {0x4, 0x8, 0xc, 0x10, 0x14, 0x18, FLAGS, BASE, BITS, SURFACE, CLIPPER, DC_NOW, MEM_DC,
                               DC_COUNT, 0x6c, REGION, OLD_BITMAP, DIB, 0x4a4, PITCH, 0x524};
    for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++) *at(b, zero[i]) = 0;
    reset_text(b);
    // The BITMAPINFOHEADER of an empty 8-bit bitmap with 256 colours.
    *at(b, INFO) = 0x28;
    *at(b, 0x80) = 0;
    *at(b, 0x84) = 0;
    *(uint16_t *)at(b, 0x88) = 1;
    *(uint16_t *)at(b, 0x8a) = 8;
    for (int o = 0x8c; o <= 0x98; o += 4) *at(b, o) = 0;
    *at(b, COLORS_USED) = 0x100;
    *at(b, 0xa0) = 0;
    uint8_t *pal = (uint8_t *)at(b, PALETTE);
    if (*game_palette_now) {
        palette_get_rgbquad(*game_palette_now, pal, 0, 0x100);
    } else {
        // Without a palette the colours are a ramp of blue (an RGBQUAD's first byte), not grey.
        for (int i = 0; i < 0x100; i++) pal[4 * i] = (uint8_t)i, pal[4 * i + 1] = pal[4 * i + 2] = pal[4 * i + 3] = 0;
    }
    return b;
}

// Let go of the device context taken for set-up (as Buffer_release_hdc does for one).
static void release_dc(buffer_t *b) {
    void *dd = b->dd;
    --*at(b, DC_COUNT);
    if (!dd) {
        if (*at(b, DC_COUNT) <= 0) *at(b, DC_NOW) = 0, *at(b, DC_COUNT) = 0;
        return;
    }
    if (*at(b, DC_NOW) && *at(b, DC_COUNT) <= 0) {
        if (((dd_release_dc)dd_method(dd, 0x68))(dd, *at(b, DC_NOW))) unlock(b);
        *at(b, DC_COUNT) = 0;
        *at(b, DC_NOW) = 0;
    }
}

void THISCALL buffer_close(buffer_t *b) {
    if (*game_ddraw) {
        orig_buffer_close(b);
        return;
    }
    for (int i = 0; i < 20; i++)
        if (*at(b, HEAP + 4 * i)) game_free((void *)(intptr_t)*at(b, HEAP + 4 * i)), *at(b, HEAP + 4 * i) = 0;
    if (*at(b, MEM_DC)) {
        *at(b, DC_COUNT) = 0;
        *at(b, 0x6c) = 0;
        // A Buffer drawing into the caller's surface leaves its device context alone.
        if (!(*at(b, FLAGS) & 4)) {
            void *dc = (void *)(intptr_t)*at(b, MEM_DC);
            if (*at(b, OLD_BITMAP)) game_select_object(dc, (void *)(intptr_t)*at(b, OLD_BITMAP)), *at(b, OLD_BITMAP) = 0;
            game_delete_dc(dc);
            *at(b, DC_NOW) = 0;
            *at(b, MEM_DC) = 0;
        }
    }
    if (*at(b, DIB)) game_delete_object((void *)(intptr_t)*at(b, DIB)), *at(b, DIB) = 0;
    if (*at(b, REGION)) game_delete_object((void *)(intptr_t)*at(b, REGION)), *at(b, REGION) = 0;
    b->dd = NULL;
    *at(b, CLIPPER) = 0;
    for (int o = 0x8; o <= 0x18; o += 4) *at(b, o) = 0;
    *at(b, 0x4a4) = 0;
    *at(b, PITCH) = 0;
    *at(b, 0x80) = 0;
    *at(b, 0x84) = 0;
    *at(b, BASE) = 0;
    *at(b, BITS) = 0;
    reset_text(b);
    *at(b, FLAGS) = 0;
}

// Forget the hot spots and the blocks of heap hung on the Buffer (20 of them): the spot list is
// made new, as init does it, and every block is freed. Surfaces make no difference to it.
void THISCALL buffer_clear_links(buffer_t *b) {
    game_spot_init(at(b, SPOTS), 0x28);
    *at(b, SPOT_COUNT) = 0;
    for (int i = 0; i < 20; i++)
        if (*at(b, HEAP + 4 * i)) game_free((void *)(intptr_t)*at(b, HEAP + 4 * i)), *at(b, HEAP + 4 * i) = 0;
}

void THISCALL buffer_dtor(buffer_t *b) {
    b->vtable = game_buffer_vtable;
    buffer_close(b);
    game_spot_dtor(at(b, SPOTS));
}

int THISCALL buffer_set_clip(buffer_t *b, const int32_t *rect) {
    if (b->dd || *game_ddraw) return orig_buffer_set_clip(b, rect);
    if (!*at(b, BITS)) return 7;
    if (!rect) return 3;
    int32_t r[4] = {rect[0], rect[1], rect[2], rect[3]};
    if (!game_intersect_rect(at(b, CLIP), at(b, FULL), r)) return 1;
    int took = 0;
    if (!*at(b, DC_NOW)) {
        took = 1;
        if (*at(b, BASE)) unlock(b);
        *at(b, DC_NOW) = *at(b, MEM_DC);
        ++*at(b, DC_COUNT);
    }
    if (*at(b, DC_NOW)) {
        void *dc = (void *)(intptr_t)*at(b, DC_NOW);
        if (*at(b, REGION)) game_delete_object((void *)(intptr_t)*at(b, REGION)), *at(b, REGION) = 0;
        if (game_equal_rect(at(b, CLIP), at(b, FULL))) {
            game_select_clip_rgn(dc, NULL);
        } else {
            void *rgn = game_create_rect_rgn_indirect(at(b, CLIP));
            *at(b, REGION) = (int32_t)(intptr_t)rgn;
            if (!rgn) return 1;  // the device context stays taken
            game_select_clip_rgn(dc, rgn);
        }
    }
    if (took) release_dc(b);
    return 0;
}

int THISCALL buffer_set_clip_2(buffer_t *b, int x, int y, int w, int h) {
    int32_t r[4];
    game_set_rect(r, x, y, x + w, y + h);
    return buffer_set_clip(b, r);
}

int THISCALL buffer_init(buffer_t *b, int w, int h, int flags, void **surface) {
    // The caller's surface, and anything with DirectDraw, stay with the original.
    if ((flags & 4) || *game_ddraw) return orig_buffer_init(b, w, h, flags, surface);
    if (w < 0 || h < 0) return 3;
    if (w == b->width && h == -b->neg_height) return 0;
    buffer_close(b);
    game_spot_init(at(b, SPOTS), 0x28);
    *at(b, SPOT_COUNT) = 0;
    for (int i = 0; i < 20; i++)
        if (*at(b, HEAP + 4 * i)) game_free((void *)(intptr_t)*at(b, HEAP + 4 * i)), *at(b, HEAP + 4 * i) = 0;
    void *dc = game_create_compatible_dc(NULL);
    *at(b, MEM_DC) = (int32_t)(intptr_t)dc;
    if (!dc) return 1;
    b->width = w;
    b->neg_height = -h;
    void *dib = game_create_dib_section(dc, at(b, INFO), 0, (void **)at(b, BITS), NULL, 0);
    *at(b, DIB) = (int32_t)(intptr_t)dib;
    if (!dib) {
        game_message_box_a(NULL, game_dib_error_text, game_dib_error_title, 0x30);
        game_exit(4);
    }
    int32_t *full = at(b, FULL);
    full[0] = 0, full[1] = 0, full[2] = w, full[3] = h;
    buffer_set_clip(b, full);
    b->width = w;
    b->neg_height = -h;
    *at(b, PIXELS) = w * h;
    *at(b, COLORS_USED) = 0x100;
    *at(b, OLD_BITMAP) = (int32_t)(intptr_t)game_select_object(dc, dib);
    b->pitch = (w + 3) & ~3;
    void **font = *game_font_default;
    if (font) {
        if (font[2]) *at(b, FONTS) = (int32_t)(intptr_t)font;
        *at(b, FONTS + 4) = *at(b, FONTS + 8) = *at(b, FONTS + 12) = 0;
    }
    if (*at(b, BASE)) unlock(b);
    *at(b, DC_NOW) = *at(b, MEM_DC);
    ++*at(b, DC_COUNT);
    game_set_bk_mode((void *)(intptr_t)*at(b, DC_NOW), 1);         // TRANSPARENT
    game_set_text_align((void *)(intptr_t)*at(b, DC_NOW), 0x18);  // TA_BASELINE
    release_dc(b);
    *at(b, FLAGS) = flags;
    return 0;
}

// Set up again at another size; a Buffer whose second virtual method gives another object
// (a subclass's owner) hands the resize to that object's fourth method instead.
typedef int(THISCALL *owner_resize)(void *owner, int w, int h, int c);
typedef void *(THISCALL *buffer_owner)(void *b);

int THISCALL buffer_resize(buffer_t *b, int w, int h) {
    void *owner = ((buffer_owner)b->vtable[1])(b);
    if (owner) {
        owner = ((buffer_owner)b->vtable[1])(b);
        return ((owner_resize)(*(void ***)owner)[3])(owner, w, h, 1);
    }
    return buffer_init(b, w, h, *at(b, FLAGS), NULL);
}

// ---- Tiling ----------------------------------------------------------------------------------

// What the three tiles share (they have returned 7 if there is nothing to draw on). The area (x, y, w, h) is cut to
// the clip rectangle, which is set to what is left for the drawing and put back after; 0 at once
// if nothing is left. A picture with no width or height ends it with the clip as it was set
// (the original does not put it back). The copies lie in rows from (x - sx % tw, y - sy % th),
// tw by th each, as many columns and rows as it takes to reach the right and bottom edges of
// the area (the unclipped one), and draw() puts one at (x, y).
typedef void (*tile_draw)(void *ctx, buffer_t *b, int x, int y);

static int tile_run(buffer_t *b, int sx, int sy, int x, int y, int w, int h, int tw, int th, tile_draw draw, void *ctx) {
    int32_t clip[4] = {at(b, CLIP)[0], at(b, CLIP)[1], at(b, CLIP)[2], at(b, CLIP)[3]};
    int32_t area[4] = {x, y, x + w, y + h};
    int right = x + w, bottom = y + h;
    if (!game_intersect_rect(area, area, clip)) return 0;
    buffer_set_clip(b, area);
    if (!tw || !th) return 0;
    int cols = w / tw;
    if (cols * tw < w) cols++;
    int rows = h / th;
    if (rows * th < h) rows++;
    x -= sx % tw;
    y -= sy % th;
    if (cols * tw + x < right) cols++;
    if (rows * th + y < bottom) rows++;
    for (int r = rows; r > 0; r--, y += th)
        for (int c = cols, xx = x; c > 0; c--, xx += tw) draw(ctx, b, xx, y);
    buffer_set_clip(b, clip);
    return 0;
}

// A Buffer for a pattern is copied from, over the key colour of the area, or in full.
struct tile_buffer {
    buffer_t *src;
    int key;
};
static void draw_mask(void *ctx, buffer_t *b, int x, int y) {
    const struct tile_buffer *t = ctx;
    buffer_copy_mask(t->src, b, 0, 0, x, y, t->src->width, -t->src->neg_height, t->key);
}
static void draw_full(void *ctx, buffer_t *b, int x, int y) {
    const struct tile_buffer *t = ctx;
    buffer_copy_full(t->src, b, 0, 0, x, y, t->src->width, -t->src->neg_height);
}
static void draw_sprite(void *ctx, buffer_t *b, int x, int y) {
    struct game_sprite *s = ctx;
    sprite_draw_2(s, b, s->key, x, y, 1, 1);
}

// The Buffer `src` laid over the area as many times as it takes, offset (sx, sy) into the
// pattern, but only over the pixels of the area that are the key colour (a Buffer with the key
// colour shows the pattern through).
int THISCALL buffer_tile_mask(buffer_t *b, buffer_t *src, int sx, int sy, int x, int y, int w, int h, int key) {
    if (!*at(b, BITS) && !*at(b, SURFACE)) return 7;
    struct tile_buffer t = {src, key};
    return tile_run(b, sx, sy, x, y, w, h, src->width, -src->neg_height, draw_mask, &t);
}

// The same with plain copies.
int THISCALL buffer_tile(buffer_t *b, buffer_t *src, int sx, int sy, int x, int y, int w, int h) {
    if (!*at(b, BITS) && !*at(b, SURFACE)) return 7;
    struct tile_buffer t = {src, 0};
    return tile_run(b, sx, sy, x, y, w, h, src->width, -src->neg_height, draw_full, &t);
}

// The same with a Sprite, drawn unscaled with its own key colour: the copies are as wide and
// high as the sprite was before its margins were cut (it draws itself at its own offset).
int THISCALL buffer_tile_2(buffer_t *b, struct game_sprite *spr, int sx, int sy, int x, int y, int w, int h) {
    if (!*at(b, BITS) && !*at(b, SURFACE)) return 7;
    return tile_run(b, sx, sy, x, y, w, h, spr->full_w, spr->full_h, draw_sprite, spr);
}

// Draws a frame in the rectangle (left, top, right, bottom): the corners at the corners, the
// edge sprites tiled between them, and if there is a pattern it is laid over each sprite's
// area (the key colour of the sprites shows it) and over the inside.
void THISCALL buffer_box_sprite(buffer_t *b, const int32_t *rect, const struct game_box_frame *f) {
    if (!rect || !f) return;
    int left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    struct game_sprite *tl = f->top_left, *tr = f->top_right, *bl = f->bottom_left, *br = f->bottom_right;
    // A corner sprite drawn at (cx, cy), and the pattern over it.
#define CORNER(spr, cx_, cy_)                                                                               \
    if (spr) {                                                                                              \
        int cx = (cx_), cy = (cy_);                                                                         \
        sprite_draw_2(spr, b, spr->key, cx, cy, 1, 1);                                                      \
        if (f->fill) buffer_tile_mask(b, f->fill, cx, cy, cx, cy, spr->full_w, spr->full_h, f->fill_key);  \
    }
    CORNER(tl, left, top)
    CORNER(tr, right - tr->full_w, top)
    CORNER(bl, left, bottom - bl->full_h)
    CORNER(br, right - br->full_w, bottom - br->full_h)
#undef CORNER
    // The edges, between the corners, each with the pattern over it.
    if (f->top) {
        int x = left, w = right - left, h = f->top->full_h;
        if (tl) x += tl->full_w, w -= tl->full_w;
        if (tr) w -= tr->full_w;
        buffer_tile_2(b, f->top, 0, 0, x, top, w, h);
        if (f->fill) buffer_tile_mask(b, f->fill, x, top, x, top, w, h, f->fill_key);
    }
    if (f->bottom) {
        int x = left, w = right - left, h = f->bottom->full_h, y = bottom - h;
        if (bl) x += bl->full_w, w -= bl->full_w;
        if (br) w -= br->full_w;
        buffer_tile_2(b, f->bottom, 0, 0, x, y, w, h);
        if (f->fill) buffer_tile_mask(b, f->fill, x, y, x, y, w, h, f->fill_key);
    }
    if (f->left) {
        int y = top, h = bottom - top, w = f->left->full_w;
        if (tl) y += tl->full_h, h -= tl->full_h;
        if (bl) h -= bl->full_h;
        buffer_tile_2(b, f->left, 0, 0, left, y, w, h);
        if (f->fill) buffer_tile_mask(b, f->fill, left, y, left, y, w, h, f->fill_key);
    }
    if (f->right) {
        int x = right - f->right->full_w, y = top, h = bottom - top, w = f->right->full_w;
        if (tr) y += tr->full_h, h -= tr->full_h;
        if (br) h -= br->full_h;
        buffer_tile_2(b, f->right, 0, 0, x, y, w, h);
        if (f->fill) buffer_tile_mask(b, f->fill, x, y, x, y, w, h, f->fill_key);
    }
    // The inside.
    if (f->fill) {
        int x = left, y = top, w = right - left, h = bottom - top;
        if (f->left) x += f->left->full_w, w -= f->left->full_w;
        if (f->top) y += f->top->full_h, h -= f->top->full_h;
        if (f->right) w -= f->right->full_w;
        if (f->bottom) h -= f->bottom->full_h;
        buffer_tile(b, f->fill, x, y, x, y, w, h);
    }
}

// ---- Shadow mode ---------------------------------------------------------------------------

// The game's calls run the original; the sweep checks Chiron's code on twin Buffers.
#define PASS_THROUGH(ret, name, params, args)                                       \
    static ret THISCALL shadow_##name params {                                     \
        if (!hooks_nested && !shadow_bypass()) {                                    \
            stats_##name.calls++;                                                   \
            shadow_tick(BUFOBJ);                                                    \
        }                                                                           \
        return orig_##name args;                                                    \
    }

PASS_THROUGH(void *, buffer_ctor, (buffer_t * b), (b))
PASS_THROUGH(void, buffer_dtor, (buffer_t * b), (b))
PASS_THROUGH(void, buffer_close, (buffer_t * b), (b))
PASS_THROUGH(void, buffer_clear_links, (buffer_t * b), (b))
PASS_THROUGH(int, buffer_init, (buffer_t * b, int w, int h, int flags, void **surface), (b, w, h, flags, surface))
PASS_THROUGH(int, buffer_resize, (buffer_t * b, int w, int h), (b, w, h))
PASS_THROUGH(int, buffer_set_clip, (buffer_t * b, const int32_t *rect), (b, rect))
PASS_THROUGH(int, buffer_set_clip_2, (buffer_t * b, int x, int y, int w, int h), (b, x, y, w, h))
PASS_THROUGH(int, buffer_tile_mask, (buffer_t * b, buffer_t *src, int sx, int sy, int x, int y, int w, int h, int key),
             (b, src, sx, sy, x, y, w, h, key))
PASS_THROUGH(int, buffer_tile, (buffer_t * b, buffer_t *src, int sx, int sy, int x, int y, int w, int h), (b, src, sx, sy, x, y, w, h))
PASS_THROUGH(int, buffer_tile_2, (buffer_t * b, struct game_sprite *spr, int sx, int sy, int x, int y, int w, int h),
             (b, spr, sx, sy, x, y, w, h))
PASS_THROUGH(void, buffer_box_sprite, (buffer_t * b, const int32_t *rect, const struct game_box_frame *f), (b, rect, f))

// Fields that hold a handle or a heap pointer: the twins' differ, so only whether each is set
// is compared (the DCs and bitmaps behind them are compared by content).
static int is_handle(int o) {
    return o == BASE || o == BITS || o == DC_NOW || o == MEM_DC || o == REGION || o == OLD_BITMAP || o == DIB || o == SPOTS;
}

// The twins after the original ran on `a` and Chiron's code on `b`: NULL if they match.
static const char *twin_diff(buffer_t *a, buffer_t *b, int got, int want) {
    static char diff[160];
    for (int o = 0; o < (int)sizeof(buffer_t); o += 4) {
        int32_t x = *at(a, o), y = *at(b, o);
        if (is_handle(o) ? !x != !y : x != y) {
            snprintf(diff, sizeof diff, "field 0x%x = 0x%x, original 0x%x", o, (unsigned)y, (unsigned)x);
            return diff;
        }
    }
    // Whether the device context in use is the memory one.
    if ((*at(a, DC_NOW) == *at(a, MEM_DC)) != (*at(b, DC_NOW) == *at(b, MEM_DC))) return "device context in use";
    if (hooks_dc_same && !hooks_dc_same((void *)(intptr_t)*at(a, MEM_DC), (void *)(intptr_t)*at(b, MEM_DC)))
        return "device context state (clip, colours, modes or bitmap)";
    if (got != want) {
        snprintf(diff, sizeof diff, "= %d, original %d", got, want);
        return diff;
    }
    return NULL;
}

// After a difference the twins are built again alike with the original, so each check that
// follows is of its own function rather than of the first difference.
static void twin_check(struct hook_stats *s, buffer_t *a, buffer_t *b, int got, int want, const char *what) {
    const char *d = twin_diff(a, b, got, want);
    if (!d) return;
    hooks_mismatch(s, " (%s): %s", what, d);
    int w = a->width, h = -a->neg_height, flags = *at(a, FLAGS) & 3;
    orig_buffer_dtor(a), orig_buffer_dtor(b);
    memset(a, 0, sizeof *a), memset(b, 0, sizeof *b);
    orig_buffer_ctor(a), orig_buffer_ctor(b);
    if (w > 0 && h > 0) orig_buffer_init(a, w, h, flags, NULL), orig_buffer_init(b, w, h, flags, NULL);
}

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void random_rect(int32_t *r, int w, int h) {
    r[0] = rnd_to(w + 8) - 4, r[1] = rnd_to(h + 8) - 4;
    r[2] = r[0] + rnd_to(w + 4) - 2, r[3] = r[1] + rnd_to(h + 4) - 2;
    if (rnd_to(4) == 0) r[0] = 0, r[1] = 0, r[2] = w, r[3] = h;
}

// ---- The tiles' sweep ------------------------------------------------------------------------

// The pixels of two Buffers, which the twin check of a drawing function compares as well.
static const char *pixels_diff(buffer_t *a, buffer_t *b) {
    if (!a->bits || !b->bits) return a->bits == b->bits ? NULL : "pixels (one has none)";
    size_t n = (size_t)a->pitch * (size_t)-a->neg_height;
    return memcmp(a->bits, b->bits, n) ? "pixels" : NULL;
}

// Two Buffers alike, made by the original and filled with the same pixels of a few colours.
static void make_pair(buffer_t *a, buffer_t *b, int w, int h) {
    memset(a, 0, sizeof *a), memset(b, 0, sizeof *b);
    orig_buffer_ctor(a), orig_buffer_ctor(b);
    orig_buffer_init(a, w, h, 0, NULL), orig_buffer_init(b, w, h, 0, NULL);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) a->bits[y * a->pitch + x] = b->bits[y * b->pitch + x] = (uint8_t)rnd_to(5);
}
static void close_pair(buffer_t *a, buffer_t *b) { orig_buffer_dtor(a), orig_buffer_dtor(b); }

// The tiles' twins: the Buffers drawn on and the pattern ones (read, but their locks move).
static buffer_t td_a, td_b, ts_a, ts_b;
static int td_w, td_h, ts_w, ts_h;

static void make_tile_pairs(int w, int h, int sw, int sh) {
    td_w = w, td_h = h, ts_w = sw, ts_h = sh;
    make_pair(&td_a, &td_b, w, h);
    make_pair(&ts_a, &ts_b, sw, sh);
}

// Compare the twins after one call of each; after a difference, build them again alike.
static void tile_check(struct hook_stats *s, int got, int want, const char *what) {
    const char *d = twin_diff(&td_a, &td_b, got, want);
    if (!d) d = pixels_diff(&td_a, &td_b);
    if (!d && (d = twin_diff(&ts_a, &ts_b, 0, 0))) {
        static char msg[200];
        snprintf(msg, sizeof msg, "pattern: %s", d);
        d = msg;
    }
    if (!d) return;
    hooks_mismatch(s, " (%s): %s", what, d);
    close_pair(&td_a, &td_b), close_pair(&ts_a, &ts_b);
    make_tile_pairs(td_w, td_h, ts_w, ts_h);
}

// A sprite of the size a picture would be before its margins are cut (full_w by full_h), with
// pixels of a few colours around its key colour; a zero size now and then.
static void make_tile_sprite(struct game_sprite *sp, uint8_t *data) {
    memset(sp, 0, sizeof *sp);
    int w = 1 + rnd_to(6), h = 1 + rnd_to(5);
    for (int i = 0; i < 64; i++) data[i] = (uint8_t)rnd_to(5);
    sp->data = data, sp->key = (uint8_t)rnd_to(5);
    sp->width = sp->stride = w, sp->height = h;
    sp->full_w = w + rnd_to(3), sp->full_h = h + rnd_to(3);
    sp->left = rnd_to(2), sp->top = rnd_to(2);
}

// An area and offset on the Buffer drawn on: crossing every edge of the clip rectangle and the
// Buffer, sometimes the whole of it, sometimes small, sometimes nothing.
static void random_area(int *x, int *y, int *w, int *h, int *sx, int *sy) {
    *x = rnd_to(td_w + 12) - 6, *y = rnd_to(td_h + 12) - 6;
    *w = rnd_to(td_w + 8) - 1, *h = rnd_to(td_h + 8) - 1;
    switch (rnd_to(6)) {
    case 0: *x = 0, *y = 0, *w = td_w, *h = td_h; break;
    case 1: *w = 1 + rnd_to(3), *h = 1 + rnd_to(3); break;
    }
    *sx = rnd_to(41) - 20, *sy = rnd_to(41) - 20;
}

// The clip rectangle of both drawn-on Buffers, by the original (the clip's own check is
// elsewhere): the whole Buffer, or a rectangle cut out of it.
static void random_clip(void) {
    int32_t r[4];
    if (rnd_to(3) == 0) r[0] = 0, r[1] = 0, r[2] = td_w, r[3] = td_h;
    else random_rect(r, td_w, td_h);
    orig_buffer_set_clip(&td_a, r), orig_buffer_set_clip(&td_b, r);
}

static void tiles_sweep(void) {
    static struct game_sprite sp, frame_sp[8];
    static uint8_t spdata[64], frame_data[8][64];
    static uint8_t saved_scratch[sizeof(struct game_sprite_scratch)];
    int32_t saved_scale[4];
    float saved_ratio = *game_sprite_ratio;
    memcpy(saved_scratch, game_sprite_scratch, sizeof saved_scratch);
    memcpy(saved_scale, game_sprite_scale, sizeof saved_scale);
    for (int round = 0; round < 6; round++) {
        // Patterns smaller and larger than the area, drawn on a Buffer from 8 to 40 wide.
        make_tile_pairs(8 + rnd_to(33), 8 + rnd_to(28), 1 + rnd_to(14), 1 + rnd_to(12));
        for (int k = 0; k < 5; k++) {
            int x, y, w, h, sx, sy, want, got;
            random_clip();
            random_area(&x, &y, &w, &h, &sx, &sy);
            int key = rnd_to(5);
            want = orig_buffer_tile_mask(&td_a, &ts_a, sx, sy, x, y, w, h, key);
            got = buffer_tile_mask(&td_b, &ts_b, sx, sy, x, y, w, h, key);
            tile_check(&stats_buffer_tile_mask, got, want, "tile_mask");
            random_clip();
            random_area(&x, &y, &w, &h, &sx, &sy);
            want = orig_buffer_tile(&td_a, &ts_a, sx, sy, x, y, w, h);
            got = buffer_tile(&td_b, &ts_b, sx, sy, x, y, w, h);
            tile_check(&stats_buffer_tile, got, want, "tile");
            random_clip();
            random_area(&x, &y, &w, &h, &sx, &sy);
            make_tile_sprite(&sp, spdata);
            if (rnd_to(12) == 0) sp.full_w = 0;
            if (rnd_to(12) == 0) sp.full_h = 0;
            want = orig_buffer_tile_2(&td_a, &sp, sx, sy, x, y, w, h);
            got = buffer_tile_2(&td_b, &sp, sx, sy, x, y, w, h);
            tile_check(&stats_buffer_tile_2, got, want, "tile_2");
            // A frame: sprites for most of the corners and edges, and a pattern for most.
            random_clip();
            struct game_box_frame fa = {0}, fb;
            struct game_sprite **slot[8] = {&fa.top_left, &fa.top_right, &fa.bottom_left, &fa.bottom_right,
                                            &fa.top, &fa.bottom, &fa.left, &fa.right};
            for (int i = 0; i < 8; i++) {
                make_tile_sprite(&frame_sp[i], frame_data[i]);
                if (rnd_to(4)) *slot[i] = &frame_sp[i];
            }
            fa.fill = rnd_to(4) ? &ts_a : NULL, fa.fill_key = rnd_to(5);
            fb = fa;
            if (fb.fill) fb.fill = &ts_b;
            int32_t rect[4];
            random_rect(rect, td_w, td_h);
            if (rnd_to(3) == 0) rect[0] = 0, rect[1] = 0, rect[2] = td_w, rect[3] = td_h;
            orig_buffer_box_sprite(&td_a, rect, &fa);
            buffer_box_sprite(&td_b, rect, &fb);
            tile_check(&stats_buffer_box_sprite, 0, 0, "box_sprite");
        }
        close_pair(&td_a, &td_b), close_pair(&ts_a, &ts_b);
    }
    // A Buffer with no pixels.
    {
        static buffer_t a, b;
        memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
        orig_buffer_ctor(&a), orig_buffer_ctor(&b);
        make_tile_pairs(10, 10, 3, 3);
        int want = orig_buffer_tile_mask(&a, &ts_a, 0, 0, 1, 1, 4, 4, 0);
        int got = buffer_tile_mask(&b, &ts_b, 0, 0, 1, 1, 4, 4, 0);
        tile_check(&stats_buffer_tile_mask, got, want, "tile_mask, no pixels");
        want = orig_buffer_tile(&a, &ts_a, 0, 0, 1, 1, 4, 4), got = buffer_tile(&b, &ts_b, 0, 0, 1, 1, 4, 4);
        tile_check(&stats_buffer_tile, got, want, "tile, no pixels");
        orig_buffer_dtor(&a), orig_buffer_dtor(&b);
        close_pair(&td_a, &td_b), close_pair(&ts_a, &ts_b);
    }
    memcpy(game_sprite_scratch, saved_scratch, sizeof saved_scratch);
    memcpy(game_sprite_scale, saved_scale, sizeof saved_scale);
    *game_sprite_ratio = saved_ratio;
}

static void bufobj_sweep(void) {
    if (*game_ddraw || !hooks_dc_same) return;
    static buffer_t a, b;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x6d2b79f5u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 8; round++) {
        // Construct both from the same bytes, with and without a palette to take colours from.
        uint8_t fill = (uint8_t)rnd();
        memset(&a, fill, sizeof a), memset(&b, fill, sizeof b);
        void *pal = *game_palette_now;
        if (round & 1) *game_palette_now = NULL;
        orig_buffer_ctor(&a);
        buffer_ctor(&b);
        *game_palette_now = pal;
        twin_check(&stats_buffer_ctor, &a, &b, 0, 0, "new");
        for (int k = 0; k < 6; k++) {
            int w = rnd_to(48), h = rnd_to(40), flags = rnd_to(4);
            if (rnd_to(8) == 0) w = -w - 1;
            if (k == 3) w = a.width, h = -a.neg_height;  // the same size again: nothing happens
            int want = orig_buffer_init(&a, w, h, flags, NULL), got = buffer_init(&b, w, h, flags, NULL);
            twin_check(&stats_buffer_init, &a, &b, got, want, "init");
            int W = a.width, H = -a.neg_height;
            for (int c = 0; c < 4; c++) {
                int32_t r[4];
                random_rect(r, W, H);
                want = orig_buffer_set_clip(&a, r), got = buffer_set_clip(&b, r);
                twin_check(&stats_buffer_set_clip, &a, &b, got, want, "set_clip");
                want = orig_buffer_set_clip_2(&a, r[0], r[1], r[2] - r[0], r[3] - r[1]);
                got = buffer_set_clip_2(&b, r[0], r[1], r[2] - r[0], r[3] - r[1]);
                twin_check(&stats_buffer_set_clip_2, &a, &b, got, want, "set_clip_2");
            }
            want = orig_buffer_set_clip(&a, NULL), got = buffer_set_clip(&b, NULL);
            twin_check(&stats_buffer_set_clip, &a, &b, got, want, "set_clip NULL");
            if (rnd_to(2)) {
                int rw = rnd_to(40), rh = rnd_to(40);
                want = orig_buffer_resize(&a, rw, rh), got = buffer_resize(&b, rw, rh);
                twin_check(&stats_buffer_resize, &a, &b, got, want, "resize");
            }
            if (k == 2) {
                // Blocks of heap hung on both, in some of the slots, and a count of spots.
                for (int i = 0; i < 20; i++)
                    if (rnd_to(3) == 0) {
                        void *x = game_mem_get(16), *y = game_mem_get(16);
                        *at(&a, HEAP + 4 * i) = (int32_t)(intptr_t)x, *at(&b, HEAP + 4 * i) = (int32_t)(intptr_t)y;
                    }
                *at(&a, SPOT_COUNT) = *at(&b, SPOT_COUNT) = rnd_to(4);
                orig_buffer_clear_links(&a);
                buffer_clear_links(&b);
                twin_check(&stats_buffer_clear_links, &a, &b, 0, 0, "clear_links");
            }
            if (k == 4) {
                orig_buffer_close(&a);
                buffer_close(&b);
                twin_check(&stats_buffer_close, &a, &b, 0, 0, "close");
                // Clipping a closed Buffer, which has no pixels.
                int32_t r[4] = {0, 0, 4, 4};
                want = orig_buffer_set_clip(&a, r), got = buffer_set_clip(&b, r);
                twin_check(&stats_buffer_set_clip, &a, &b, got, want, "set_clip closed");
            }
        }
        orig_buffer_dtor(&a);
        buffer_dtor(&b);
        twin_check(&stats_buffer_dtor, &a, &b, 0, 0, "delete");
    }
    tiles_sweep();
    hooks_sweeping = sweeping;
}

static const struct hook bufobj_hooks[] = {
    HOOK(0x5d7210, buffer_ctor, 0x6a, 0xff, 0x68, 0x5e, 0x2b, 0x66, 0x00),  // push -1; push handler
    HOOK(0x5d7410, buffer_dtor, 0x6a, 0xff, 0x68, 0x76, 0x2b, 0x66, 0x00),
    HOOK(0x5d7470, buffer_close, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57),       // push ebx, ebp, esi; mov esi, ecx
    HOOK(0x5d7670, buffer_init, 0x83, 0xec, 0x74, 0x8b, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00),
    // push esi; mov esi, ecx; push edi; push 0x28
    HOOK(0x5def90, buffer_clear_links, 0x56, 0x8b, 0xf1, 0x57, 0x6a, 0x28),
    HOOK(0x5d79c0, buffer_resize, 0x56, 0x8b, 0xf1, 0x8b, 0x06),            // push esi; mov esi, ecx; mov eax, [esi]
    HOOK(0x5d8000, buffer_set_clip, 0x83, 0xec, 0x40, 0x53, 0x55, 0x56),
    HOOK(0x5d8200, buffer_set_clip_2, 0x8b, 0x44, 0x24, 0x08, 0x83, 0xec, 0x10),
    // sub esp, 0x24 or 0x28; push ebx, ebp, esi, edi
    HOOK(0x5da6c0, buffer_tile_mask, 0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x57),
    HOOK(0x5da860, buffer_tile, 0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x57),
    HOOK(0x5da9f0, buffer_tile_2, 0x83, 0xec, 0x28, 0x53, 0x55, 0x56, 0x57),
    // push ecx, ebx, ebp, esi, edi; mov edi, [esp+0x18]
    HOOK(0x5e2dd3, buffer_box_sprite, 0x51, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x18),
};
struct hook_module bufobj_module = {"bufobj", bufobj_hooks, HOOK_COUNT(bufobj_hooks), bufobj_sweep, NULL, -1};
