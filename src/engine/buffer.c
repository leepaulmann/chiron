// The game's Buffer class: a surface of 8-bit pixels the game draws on. The
// first drawing primitives are here: fills, horizontal and vertical lines,
// single pixels. Reimplemented from terranx.exe 2.0's disassembly; the class
// is called Buffer in OpenSMACX (GPL-3.0-or-later).
//
// A buffer is a DIB (its pixels are at `bits`) or a DirectDraw surface
// (`dd`), which each drawing call locks through the surface's methods. Only
// the DIB is done here: for a surface every function runs the original. The
// original locks a DIB buffer too, by counting (`locks`) and pointing `base`
// at the pixels while a call runs.
//
// What is left to the original: the constructor, close, init and resize
// (they allocate from the game's heap and make GDI objects), set_clip (GDI
// regions), and the drawing that works through tables, fonts or sprites.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "shadow.h"

extern struct hook_module buffer_module;
#define BUFFER (&buffer_module)

typedef struct game_buffer buffer_t;

// The pixels are drawn on while `locks` counts the calls in progress.
static inline void lock_dib(buffer_t *b) {
    b->base = b->bits;
    b->locks++;
}
static inline void unlock_dib(buffer_t *b) {
    if (--b->locks <= 0) b->base = NULL, b->locks = 0;
}

// The Windows IntersectRect: false, and an empty result, if they do not overlap.
static int intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2],
                    a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) {
        memset(out, 0, 16);
        return 0;
    }
    memcpy(out, r, 16);
    return 1;
}

SHADOW_DECLARE_T(void, buffer_hline, (buffer_t * b, int x1, int x2, int y, int color))
SHADOW_DECLARE_T(void, buffer_vline, (buffer_t * b, int x, int y1, int y2, int color))
SHADOW_DECLARE_T(void, buffer_dotted_hline, (buffer_t * b, int x1, int x2, int y, int color))
SHADOW_DECLARE_T(void, buffer_dotted_vline, (buffer_t * b, int x, int y1, int y2, int color))
SHADOW_DECLARE_T(int, buffer_get_pixel, (buffer_t * b, int x, int y))
SHADOW_DECLARE_T(int, buffer_put_pixel, (buffer_t * b, int x, int y, int color))
SHADOW_DECLARE_T(int, buffer_fill_rect, (buffer_t * b, const int32_t *rect, int color))
SHADOW_DECLARE_T(int, buffer_fill_all, (buffer_t * b, int color))
SHADOW_DECLARE_T(int, buffer_fill, (buffer_t * b, int x, int y, int w, int h, int color))

// A horizontal line from x1 up to and including x2 (either way round; one
// pixel long draws nothing), clipped. Dotted, only every other pixel from
// the first is drawn, for half the length rounded down; a dotted line cut
// down to one pixel makes the original count down from 0 and draw until it
// faults, which this does not.
static void draw_hline(buffer_t *b, int x1, int x2, int y, int color, int dotted) {
    if (!b->bits) return;
    if (y < b->clip[1] || y >= b->clip[3]) return;
    int lo = x1, hi = x2;
    if (lo == hi) return;
    if (lo > hi) lo = x2, hi = x1;
    if (lo >= b->clip[2] || hi < b->clip[0]) return;
    if (lo < b->clip[0]) lo = b->clip[0];
    if (hi >= b->clip[2]) hi = b->clip[2] - 1;
    if (lo >= b->width || y >= -b->neg_height) return;
    lock_dib(b);
    uint8_t *at = b->base + b->pitch * y + lo;
    if (!at) return;  // the original leaves the buffer locked
    if (dotted)
        for (int n = (hi - lo + 1) >> 1; n > 0; n--, at += 2) *at = (uint8_t)color;
    else
        memset(at, color, (size_t)(hi - lo + 1));
    unlock_dib(b);
}

// A vertical line the same way.
static void draw_vline(buffer_t *b, int x, int y1, int y2, int color, int dotted) {
    if (!b->bits) return;
    if (x < b->clip[0] || x >= b->clip[2]) return;
    int lo = y1, hi = y2;
    if (lo == hi) return;
    if (lo > hi) lo = y2, hi = y1;
    if (lo >= b->clip[3] || hi < b->clip[1]) return;
    if (lo < b->clip[1]) lo = b->clip[1];
    if (hi >= b->clip[3]) hi = b->clip[3] - 1;
    if (x >= b->width || lo >= -b->neg_height) return;
    lock_dib(b);
    uint8_t *at = b->base + b->pitch * lo + x;
    if (!at) return;
    int step = dotted ? 2 * b->pitch : b->pitch;
    for (int n = dotted ? (hi - lo + 1) >> 1 : hi - lo + 1; n > 0; n--, at += step) *at = (uint8_t)color;
    unlock_dib(b);
}

void __attribute__((thiscall)) buffer_hline(buffer_t *b, int x1, int x2, int y, int color) {
    if (b->dd) return orig_buffer_hline(b, x1, x2, y, color);
    draw_hline(b, x1, x2, y, color, 0);
}
void __attribute__((thiscall)) buffer_vline(buffer_t *b, int x, int y1, int y2, int color) {
    if (b->dd) return orig_buffer_vline(b, x, y1, y2, color);
    draw_vline(b, x, y1, y2, color, 0);
}
void __attribute__((thiscall)) buffer_dotted_hline(buffer_t *b, int x1, int x2, int y, int color) {
    if (b->dd) return orig_buffer_dotted_hline(b, x1, x2, y, color);
    draw_hline(b, x1, x2, y, color, 1);
}
void __attribute__((thiscall)) buffer_dotted_vline(buffer_t *b, int x, int y1, int y2, int color) {
    if (b->dd) return orig_buffer_dotted_vline(b, x, y1, y2, color);
    draw_vline(b, x, y1, y2, color, 1);
}

// A pixel, or 0 outside the buffer (not clipped).
int __attribute__((thiscall)) buffer_get_pixel(buffer_t *b, int x, int y) {
    if (b->dd) return orig_buffer_get_pixel(b, x, y);
    if (!b->bits || x < 0 || y < 0 || x >= b->width || y >= -b->neg_height) return 0;
    lock_dib(b);
    uint8_t *at = b->base + b->pitch * y + x;
    unlock_dib(b);
    return at ? *at : 0;
}

// Sets a pixel (not clipped); 7 if the buffer has no pixels, else 0.
int __attribute__((thiscall)) buffer_put_pixel(buffer_t *b, int x, int y, int color) {
    if (b->dd) return orig_buffer_put_pixel(b, x, y, color);
    if (!b->bits) return 7;
    if (x < 0 || y < 0 || x >= b->width || y >= -b->neg_height) return 0;
    lock_dib(b);
    uint8_t *at = b->base + b->pitch * y + x;
    if (at) *at = (uint8_t)color;
    unlock_dib(b);
    return 0;
}

// Fills a rectangle (left, top, right, bottom; right and bottom excluded)
// clipped to the clip rectangle. The original leaves the pitch less the
// width in a global.
int __attribute__((thiscall)) buffer_fill_rect(buffer_t *b, const int32_t *rect, int color) {
    if (b->dd) return orig_buffer_fill_rect(b, rect, color);
    if (!b->bits) return 7;
    if (!rect) return 3;
    int32_t r[4];
    if (!intersect(r, rect, b->clip)) return 0;
    if (r[0] >= b->width || r[1] >= -b->neg_height) return 0;
    lock_dib(b);
    uint8_t *at = b->base + b->pitch * r[1] + r[0];
    if (!at) return 0;
    int rows = r[3] - r[1], w = r[2] - r[0];
    *game_fill_gap = b->pitch - w;
    for (; rows > 0; rows--, at += b->pitch) memset(at, color, (size_t)w);
    unlock_dib(b);
    return 0;
}

// Fills the whole clip rectangle. When that is the whole buffer, it fills
// whole rows of 4-byte units in one go, as if the pitch were the width
// rounded up to 4. Returns what the original leaves in eax: the lock count
// after, or fill_rect's answer.
int __attribute__((thiscall)) buffer_fill_all(buffer_t *b, int color) {
    if (b->dd) return orig_buffer_fill_all(b, color);
    if (!b->bits) return 0;
    if (memcmp(b->clip, b->full, 16)) return buffer_fill_rect(b, b->clip, color);
    lock_dib(b);
    uint32_t w = (uint32_t)b->full[2], rows = (uint32_t)b->full[3];
    uint32_t units = ((w >> 2) + ((w & 3) != 0)) * rows;
    uint8_t *at = b->base;
    for (uint32_t i = 0; i < units; i++, at += 4) memset(at, color, 4);
    int left = b->locks - 1;
    unlock_dib(b);
    return left;
}

// A rectangle by its corner and size.
int __attribute__((thiscall)) buffer_fill(buffer_t *b, int x, int y, int w, int h, int color) {
    int32_t rect[4] = {x, y, x + w, y + h};
    return buffer_fill_rect(b, rect, color);
}

// ---- Shadow checks -----------------------------------------------------------
//
// A check puts back what a call may write: the object (its lock fields) and
// the pixels of the rectangle drawn on. The pixels live outside the game's
// writable image, so they are named here; for a sweep's scratch buffer
// everything is, to catch a stray write as well.

static int sweep_everything;

static uint8_t *pixel_at(const buffer_t *b, int x, int y) { return b->bits + (intptr_t)b->pitch * y + x; }

// The pixels of a rectangle (clamped to the buffer) and what the original keeps in the object.
static void writes_rect(struct hook_writes *w, buffer_t *b, int l, int t, int r, int bt) {
    hooks_writes_add(w, (void *)b, sizeof *b);
    hooks_writes_add(w, (void *)game_fill_gap, 4);
    if (!b->bits || b->dd) return;
    int rows = -b->neg_height;
    if (rows < 0) rows = 0;
    if (sweep_everything) {
        hooks_writes_add(w, (void *)b->bits, (size_t)(b->pitch > 0 ? b->pitch : -b->pitch) * (size_t)rows + 8);
        return;
    }
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > b->width) r = b->width;
    if (bt > rows) bt = rows;
    if (l >= r || t >= bt) return;
    uint8_t *a = pixel_at(b, l, t), *c = pixel_at(b, r - 1, bt - 1);
    uint8_t *lo = a < pixel_at(b, l, bt - 1) ? a : pixel_at(b, l, bt - 1);
    uint8_t *hi = c > pixel_at(b, r - 1, t) ? c : pixel_at(b, r - 1, t);
    hooks_writes_add(w, (void *)lo, (size_t)(hi - lo) + 1);
}

// The whole pixel area fill_all fills in one go: whole units of 4 bytes per row.
static void writes_all(struct hook_writes *w, buffer_t *b) {
    hooks_writes_add(w, (void *)b, sizeof *b);
    hooks_writes_add(w, (void *)game_fill_gap, 4);
    uint32_t wd = (uint32_t)b->full[2], rows = (uint32_t)b->full[3];
    if (b->bits && !b->dd && rows < 0x10000) hooks_writes_add(w, (void *)b->bits, (((wd >> 2) + ((wd & 3) != 0)) * rows) * 4);
}

#define BUF_ENTRY(ret, name, params, args)                      \
    static ret THISCALL shadow_##name params {                   \
        if (hooks_nested || b->dd) SHADOW_TAIL_##ret(orig_##name args); \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);       \
        stats_##name.calls++;                                    \
        shadow_tick(BUFFER);                                     \
        SHADOW_TAIL_##ret(check_##name args);                    \
    }

#define BUF_WRITER(ret, name, params, args, writes, fmt, ...)                 \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__) \
    BUF_ENTRY(ret, name, params, args)

BUF_WRITER(void, buffer_hline, (buffer_t * b, int x1, int x2, int y, int color), (b, x1, x2, y, color),
           writes_rect(&w_, b, x1 < x2 ? x1 : x2, y, x1 < x2 ? x2 + 1 : x1 + 1, y + 1), "%d, %d, %d, %d", x1, x2, y,
           color)
BUF_WRITER(void, buffer_vline, (buffer_t * b, int x, int y1, int y2, int color), (b, x, y1, y2, color),
           writes_rect(&w_, b, x, y1 < y2 ? y1 : y2, x + 1, y1 < y2 ? y2 + 1 : y1 + 1), "%d, %d, %d, %d", x, y1, y2,
           color)
BUF_WRITER(void, buffer_dotted_hline, (buffer_t * b, int x1, int x2, int y, int color), (b, x1, x2, y, color),
           writes_rect(&w_, b, x1 < x2 ? x1 : x2, y, x1 < x2 ? x2 + 1 : x1 + 1, y + 1), "%d, %d, %d, %d", x1, x2, y,
           color)
BUF_WRITER(void, buffer_dotted_vline, (buffer_t * b, int x, int y1, int y2, int color), (b, x, y1, y2, color),
           writes_rect(&w_, b, x, y1 < y2 ? y1 : y2, x + 1, y1 < y2 ? y2 + 1 : y1 + 1), "%d, %d, %d, %d", x, y1, y2,
           color)
BUF_WRITER(int, buffer_get_pixel, (buffer_t * b, int x, int y), (b, x, y), writes_rect(&w_, b, 0, 0, 0, 0), "%d, %d", x,
           y)
BUF_WRITER(int, buffer_put_pixel, (buffer_t * b, int x, int y, int color), (b, x, y, color),
           writes_rect(&w_, b, x, y, x + 1, y + 1), "%d, %d, %d", x, y, color)
BUF_WRITER(int, buffer_fill_rect, (buffer_t * b, const int32_t *rect, int color), (b, rect, color),
           rect ? writes_rect(&w_, b, rect[0], rect[1], rect[2], rect[3]) : writes_rect(&w_, b, 0, 0, 0, 0), "%d, %d",
           rect != NULL, color)
BUF_WRITER(int, buffer_fill_all, (buffer_t * b, int color), (b, color),
           (memcmp(b->clip, b->full, 16) ? writes_rect(&w_, b, b->clip[0], b->clip[1], b->clip[2], b->clip[3])
                                         : writes_all(&w_, b)),
           "%d", color)
BUF_WRITER(int, buffer_fill, (buffer_t * b, int x, int y, int w, int h, int color), (b, x, y, w, h, color),
           writes_rect(&w_, b, x, y, x + w, y + h), "%d, %d, %d, %d, %d", x, y, w, h, color)

// ---- Sweep -------------------------------------------------------------------

enum { SW = 61, SH = 40, SPITCH = 64 };  // not a multiple of 4 wide, so the ragged ends are used

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// A made-up DIB buffer: random pixels, a random clip rectangle, and some
// calls already in progress (a lock count above 0), which the drawing
// functions leave in the lock.
static void reset_buffer(buffer_t *b, uint8_t *pixels) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < SPITCH * SH + 8; i++) pixels[i] = (uint8_t)rnd();
    b->bits = pixels;
    b->width = SW, b->neg_height = -SH, b->pitch = SPITCH;
    b->full[2] = SW, b->full[3] = SH;
    b->clip[0] = 0, b->clip[1] = 0, b->clip[2] = SW, b->clip[3] = SH;
    if (rnd_to(3)) {
        int l = rnd_to(SW), t = rnd_to(SH);
        // Never empty: the original's vline then counts down from 0 and draws until it faults.
        b->clip[0] = l, b->clip[1] = t, b->clip[2] = l + 1 + rnd_to(SW - l), b->clip[3] = t + 1 + rnd_to(SH - t);
    }
    int nested = rnd_to(4) == 0 ? 1 + rnd_to(2) : 0;
    b->locks = nested;
    b->base = nested ? pixels : NULL;
}

static int coord(int span) { return rnd_to(span + 24) - 12; }  // a little way outside as well

static void buffer_sweep(void) {
    static buffer_t b;
    static uint8_t pixels[SPITCH * SH + 8];
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    sweep_everything = 1;
    rng_state = 0x2545f491u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 64; round++) {
        reset_buffer(&b, pixels);
        for (int k = 0; k < 6; k++) {
            int color = (int)rnd() >> (k & 1 ? 0 : 8);  // above the low byte too: only that counts
            check_buffer_hline(&b, coord(SW), coord(SW), coord(SH), color);
            check_buffer_vline(&b, coord(SW), coord(SH), coord(SH), color);
            // A dotted line cut to one pixel is skipped: the original would not stop.
            int dx1 = coord(SW), dx2 = coord(SW), dy = coord(SH), dy1 = coord(SH), dy2 = coord(SH);
            int hlen = (dx1 < dx2 ? dx2 : dx1) < b.clip[2] ? (dx1 < dx2 ? dx2 : dx1) : b.clip[2] - 1;
            hlen -= (dx1 < dx2 ? dx1 : dx2) > b.clip[0] ? (dx1 < dx2 ? dx1 : dx2) : b.clip[0];
            int vlen = (dy1 < dy2 ? dy2 : dy1) < b.clip[3] ? (dy1 < dy2 ? dy2 : dy1) : b.clip[3] - 1;
            vlen -= (dy1 < dy2 ? dy1 : dy2) > b.clip[1] ? (dy1 < dy2 ? dy1 : dy2) : b.clip[1];
            if (hlen != 0) check_buffer_dotted_hline(&b, dx1, dx2, dy, color);
            if (vlen != 0) check_buffer_dotted_vline(&b, dx1, dy1, dy2, color);
            check_buffer_get_pixel(&b, coord(SW), coord(SH));
            check_buffer_put_pixel(&b, coord(SW), coord(SH), color);
            int32_t r[4] = {coord(SW), coord(SH), coord(SW), coord(SH)};
            if (k & 1) r[2] = r[0] + rnd_to(SW), r[3] = r[1] + rnd_to(SH);
            check_buffer_fill_rect(&b, r, color);
            check_buffer_fill(&b, coord(SW), coord(SH), rnd_to(SW + 8) - 4, rnd_to(SH + 8) - 4, color);
        }
        check_buffer_fill_rect(&b, NULL, 3);
        check_buffer_hline(&b, 5, 5, 5, 1);
        check_buffer_vline(&b, 5, 5, 5, 1);
        check_buffer_fill_all(&b, (int)rnd());
        memcpy(b.clip, b.full, 16);  // the whole buffer: the other fill
        check_buffer_fill_all(&b, (int)rnd());
        b.bits = NULL;  // no pixels at all
        check_buffer_hline(&b, 1, 9, 2, 3);
        check_buffer_get_pixel(&b, 1, 1);
        check_buffer_put_pixel(&b, 1, 1, 1);
        check_buffer_fill_rect(&b, b.full, 1);
        check_buffer_fill_all(&b, 1);
    }
    sweep_everything = 0;
    hooks_sweeping = sweeping;
}

static const struct hook buffer_hooks[] = {
    HOOK(0x5d8240, buffer_fill, 0x8b, 0x44, 0x24, 0x08, 0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1),  // mov eax, [esp+8]; sub esp, 16
    HOOK(0x5dfb50, buffer_fill_all, 0x55, 0x8b, 0xec, 0x81, 0xec, 0xe0, 0x00, 0x00, 0x00),  // sub esp, 0xe0
    HOOK(0x5dfcd0, buffer_fill_rect, 0x55, 0x8b, 0xec, 0x81, 0xec, 0xec, 0x00, 0x00, 0x00),  // sub esp, 0xec
    HOOK(0x5e1a80, buffer_hline, 0x83, 0xec, 0x6c, 0x53, 0x55, 0x56, 0x8b, 0xf1),  // sub esp, 0x6c; push ebx, ebp, esi; mov esi, ecx
    HOOK(0x5e1bf0, buffer_vline, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x70, 0x53),         // sub esp, 0x70
    HOOK(0x5e1d70, buffer_dotted_hline, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x6c, 0x53),
    HOOK(0x5e1ef0, buffer_dotted_vline, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x70, 0x53),
    HOOK(0x5e2210, buffer_get_pixel, 0x83, 0xec, 0x6c, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x5e2330, buffer_put_pixel, 0x83, 0xec, 0x6c, 0x53, 0x55, 0x56, 0x8b, 0xf1),
};
// The palette's sweep runs with this one: the game seldom calls its functions.
extern struct hook_module palette_module;
struct hook_module buffer_module = {"buffer", buffer_hooks, HOOK_COUNT(buffer_hooks), buffer_sweep, &palette_module, -1};
