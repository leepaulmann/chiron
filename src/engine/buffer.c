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

SHADOW_DECLARE_T(int, buffer_copy_full, (buffer_t * d, buffer_t *s, int x, int y, int sx, int sy, int w, int h))
SHADOW_DECLARE_T(int, buffer_copy_mask, (buffer_t * d, buffer_t *s, int x, int y, int sx, int sy, int w, int h, int key))
SHADOW_DECLARE_T(void, buffer_pat_hline,(buffer_t * b, int x1, int x2, int y, int c1, int c2, int a, int bl, int phase))
SHADOW_DECLARE_T(void, buffer_pat_vline, (buffer_t * b, int x, int y1, int y2, int c1, int c2, int a, int bl, int phase))
SHADOW_DECLARE_T(void, buffer_line, (buffer_t * b, int x1, int y1, int x2, int y2, int color))
SHADOW_DECLARE_T(void, buffer_pat_line, (buffer_t * b, int x1, int y1, int x2, int y2, int c1, int c2, int a, int bl, int phase))
SHADOW_DECLARE_T(void, buffer_box, (buffer_t * b, int x1, int y1, int x2, int y2, int c1, int c2))
SHADOW_DECLARE_T(int, buffer_box_rect, (buffer_t * b, const int32_t *rect, int c1, int c2))
SHADOW_DECLARE_T(void, buffer_dotted_box, (buffer_t * b, int x1, int y1, int x2, int y2, int color))
SHADOW_DECLARE_T(int, buffer_dotted_box_rect, (buffer_t * b, const int32_t *rect, int color))
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

// ---- Patterned lines, plain lines, boxes ----------------------------------------
//
// A pattern alternates runs of `a` pixels in colour c2 and `bl` pixels in c1
// (-1 draws nothing for that colour). `phase` says where in the pattern the
// line starts. The original reduces it modulo a + bl, counts from the left
// (or top) end, and does not move it on when the clip rectangle cuts the
// start off. It divides by a + bl, and faults when that is 0 (as this does).
struct pen {
    int solid, color;
    int c1, c2, a, bl, phase;
};

// Draws one pixel with the pen and moves the pattern on.
static inline void plot(struct pen *p, uint8_t *at) {
    if (p->solid) {
        *at = (uint8_t)p->color;
    } else if (p->phase < 0) {
        if (p->c1 != -1) *at = (uint8_t)p->c1;
        if (++p->phase == 0) p->phase = p->bl;
    } else {
        if (p->c2 != -1) *at = (uint8_t)p->c2;
        if (--p->phase == 0) p->phase = -p->a;
    }
}

// The phase the caller gave as the working form: the remainder in the
// period, counting up from below 0 in a c1 run or down from above in a c2 run.
static void pen_start(struct pen *p) {
    int period = p->a + p->bl;
    int rem = p->phase % period;
    p->phase = rem < p->a ? rem - p->a : p->a - rem + p->bl;
}

// Reverses a pattern for a line drawn from the other end: runs and colours swap.
static void pen_reverse(struct pen *p) {
    int t = p->c1;
    p->c1 = p->c2, p->c2 = t;
    t = p->a, p->a = p->bl, p->bl = t;
    p->phase = p->a - p->phase + p->bl;
}

// A patterned horizontal (vertical = 0) or vertical line, clipped, from `along1`
// to `along2` at `across`. One pixel long draws nothing, nor do two colours of -1.
static void draw_pat_line(buffer_t *b, int along1, int along2, int across, int vertical, struct pen *p) {
    int lo_clip = vertical ? b->clip[1] : b->clip[0], hi_clip = vertical ? b->clip[3] : b->clip[2];
    int across_lo = vertical ? b->clip[0] : b->clip[1], across_hi = vertical ? b->clip[2] : b->clip[3];
    int across_size = vertical ? b->width : -b->neg_height;
    int along_size = vertical ? -b->neg_height : b->width;
    if (!b->bits) return;
    if (across < across_lo || across >= across_hi) return;
    if (along1 == along2) return;
    if (p->c1 == -1 && p->c2 == -1) return;
    if (along1 > along2) {
        int t = along1;
        along1 = along2, along2 = t;
        pen_reverse(p);
    }
    if (along1 >= hi_clip || along2 < lo_clip) return;
    if (along1 < lo_clip) along1 = lo_clip;
    if (along2 >= hi_clip) along2 = hi_clip - 1;
    if (along1 >= along_size || across >= across_size) return;
    lock_dib(b);
    uint8_t *at = vertical ? b->base + b->pitch * along1 + across : b->base + b->pitch * across + along1;
    if (!at) return;
    pen_start(p);
    int step = vertical ? b->pitch : 1;
    for (int n = along2 - along1 + 1; n != 0; n--, at += step) plot(p, at);
    unlock_dib(b);
}

// The line itself, left end first (x1 < x2, y1 != y2), with the pen: Bresenham
// stepping along x when the line is no steeper than 45 degrees and along y
// when it is. It first runs past the part left of or above (below, going up)
// the clip rectangle, then draws until it leaves it. The first pixel is drawn
// before the far end is looked at.
static void draw_line_body(buffer_t *b, int x1, int y1, int x2, int y2, struct pen *pen) {
    int L = b->clip[0], T = b->clip[1], R = b->clip[2], B = b->clip[3];
    int dx = x2 - x1, dy = y2 - y1;
    int dir = dy > 0 ? 1 : -1, a = dy > 0 ? dy : -dy;
    if (dir > 0 ? (x1 > R || y1 > B || x2 < L || y2 < T) : (x1 > R || y1 < T || x2 < L || y2 > B)) return;
    int x = x1, y = y1, err, inc1, inc2;
    uint8_t *at;
#define BEFORE_START (dir > 0 ? y < T : y >= B)
#define PAST_END (dir > 0 ? y >= B : y < T)
    if (dx >= a) {
        err = 2 * a - dx, inc1 = 2 * a, inc2 = 2 * (a - dx);
        while (x < L || BEFORE_START) {
            if (err > 0) err += inc2, y += dir;
            else err += inc1;
            x++;
        }
        if (x >= R || PAST_END) return;
        at = b->base + b->pitch * y + x;
        plot(pen, at);
        int xe = x2 >= R ? R - 1 : x2;
        if (x >= xe) return;
        do {
            if (err > 0) {
                y += dir;
                if (PAST_END) return;
                err += inc2, at += dir * b->pitch;
            } else {
                err += inc1;
            }
            x++, at++;
            plot(pen, at);
        } while (x < xe);
    } else {
        err = 2 * dx - a, inc1 = 2 * dx, inc2 = 2 * (dx - a);
        while (x < L || BEFORE_START) {
            if (err > 0) err += inc2, x++;
            else err += inc1;
            y += dir;
        }
        if (x >= R || PAST_END) return;
        at = b->base + b->pitch * y + x;
        plot(pen, at);
        int ye = dir > 0 ? (y2 >= B ? B - 1 : y2) : (y2 < T ? T : y2);
        if (dir > 0 ? y >= ye : y <= ye) return;
        do {
            if (err > 0) {
                x++;
                if (x >= R) return;
                err += inc2, at++;
            } else {
                err += inc1;
            }
            y += dir, at += dir * b->pitch;
            plot(pen, at);
        } while (dir > 0 ? y < ye : y > ye);
    }
#undef BEFORE_START
#undef PAST_END
}

// A line between two points, clipped. A vertical or horizontal one goes to
// those functions, and one running right to left is turned round first.
void __attribute__((thiscall)) buffer_line(buffer_t *b, int x1, int y1, int x2, int y2, int color) {
    if (b->dd) return orig_buffer_line(b, x1, y1, x2, y2, color);
    if (!b->bits) return;
    if (x1 == x2) return buffer_vline(b, x1, y1, y2, color);
    if (y1 == y2) return buffer_hline(b, x1, x2, y1, color);
    if (x2 < x1) {
        int t = x1;
        x1 = x2, x2 = t;
        t = y1, y1 = y2, y2 = t;
    }
    lock_dib(b);
    struct pen pen = {1, color, 0, 0, 0, 0, 0};
    draw_line_body(b, x1, y1, x2, y2, &pen);
    unlock_dib(b);
}

// The same with a pattern; turning the line round reverses the pattern.
void __attribute__((thiscall)) buffer_pat_line(buffer_t *b, int x1, int y1, int x2, int y2, int c1, int c2, int a, int bl,
                                               int phase) {
    if (b->dd) return orig_buffer_pat_line(b, x1, y1, x2, y2, c1, c2, a, bl, phase);
    if (!b->bits) return;
    if (x1 == x2) return buffer_pat_vline(b, x1, y1, y2, c1, c2, a, bl, phase);
    if (y1 == y2) return buffer_pat_hline(b, x1, x2, y1, c1, c2, a, bl, phase);
    struct pen pen = {0, 0, c1, c2, a, bl, phase};
    if (x2 < x1) {
        int t = x1;
        x1 = x2, x2 = t;
        t = y1, y1 = y2, y2 = t;
        pen_reverse(&pen);
    }
    lock_dib(b);
    pen_start(&pen);
    draw_line_body(b, x1, y1, x2, y2, &pen);
    unlock_dib(b);
}

void __attribute__((thiscall)) buffer_pat_hline(buffer_t *b, int x1, int x2, int y, int c1, int c2, int a, int bl, int phase) {
    if (b->dd) return orig_buffer_pat_hline(b, x1, x2, y, c1, c2, a, bl, phase);
    struct pen p = {0, 0, c1, c2, a, bl, phase};
    draw_pat_line(b, x1, x2, y, 0, &p);
}
void __attribute__((thiscall)) buffer_pat_vline(buffer_t *b, int x, int y1, int y2, int c1, int c2, int a, int bl, int phase) {
    if (b->dd) return orig_buffer_pat_vline(b, x, y1, y2, c1, c2, a, bl, phase);
    struct pen p = {0, 0, c1, c2, a, bl, phase};
    draw_pat_line(b, y1, y2, x, 1, &p);
}

// A one-pixel frame round the rectangle with corners (x1, y1) and (x2, y2),
// both included: the top and left edges in c1, the bottom and right in c2.
// Each corner shared by the two colours is drawn in one of them only.
void __attribute__((thiscall)) buffer_box(buffer_t *b, int x1, int y1, int x2, int y2, int c1, int c2) {
    if (b->dd) return orig_buffer_box(b, x1, y1, x2, y2, c1, c2);
    buffer_hline(b, x1 + 1, x2, y1, c1);
    buffer_hline(b, x1, x2 - 1, y2, c2);
    buffer_vline(b, x1, y1, y2 - 1, c1);
    buffer_vline(b, x2, y1 + 1, y2, c2);
}

// The same for a rectangle (left, top, right, bottom; right and bottom
// excluded). 3 if there is none.
int __attribute__((thiscall)) buffer_box_rect(buffer_t *b, const int32_t *rect, int c1, int c2) {
    if (b->dd) return orig_buffer_box_rect(b, rect, c1, c2);
    if (!rect) return 3;
    buffer_box(b, rect[0], rect[1], rect[2] - 1, rect[3] - 1, c1, c2);
    return 0;
}

// The same dotted, in one colour.
void __attribute__((thiscall)) buffer_dotted_box(buffer_t *b, int x1, int y1, int x2, int y2, int color) {
    if (b->dd) return orig_buffer_dotted_box(b, x1, y1, x2, y2, color);
    buffer_dotted_hline(b, x1 + 1, x2, y1, color);
    buffer_dotted_hline(b, x1, x2 - 1, y2, color);
    buffer_dotted_vline(b, x1, y1, y2 - 1, color);
    buffer_dotted_vline(b, x2, y1 + 1, y2, color);
}

// 0x10 if there is no rectangle.
int __attribute__((thiscall)) buffer_dotted_box_rect(buffer_t *b, const int32_t *rect, int color) {
    if (b->dd) return orig_buffer_dotted_box_rect(b, rect, color);
    if (!rect) return 0x10;
    buffer_dotted_box(b, rect[0], rect[1], rect[2] - 1, rect[3] - 1, color);
    return 0;
}

// ---- Copies between buffers --------------------------------------------------

// Copies a rectangle of the buffer `s` to this one: (sx, sy) in `s`, to (x, y)
// here, w by h pixels. It is cut to this buffer first and then to `s`'s clip
// rectangle, and the part cut off the left or top of the destination moves the
// source corner with it. 7 if this buffer has no pixels, 3 if `s` is missing
// or a corner is outside, else 0.
//
// The original also copies within one buffer, choosing the order of rows and
// bytes from where the rectangles lie; that is left to it, as are surfaces.
int __attribute__((thiscall)) buffer_copy_full(buffer_t *d, buffer_t *s, int x, int y, int sx, int sy, int w, int h) {
    if (d->dd || (s && s->dd) || s == d) return orig_buffer_copy_full(d, s, x, y, sx, sy, w, h);
    if (!d->bits) return 7;
    if (!s) return 3;
    if (x < 0) w += x, sx -= x, x = 0;
    if (y < 0) h += y, sy -= y, y = 0;
    int width = d->width, rows = -d->neg_height;
    if (x > width || y > rows) return 0;
    if (x + w > width) w = width - x;
    if (y + h > rows) h = rows - y;
    int32_t r[4] = {sx, sy, sx + w, sy + h};
    if (!intersect(r, r, s->clip)) return 0;
    x += r[0] - sx, y += r[1] - sy;
    w = r[2] - r[0], h = r[3] - r[1];
    int srcx = r[0], srcy = r[1];
    if (srcx >= s->width || srcy >= -s->neg_height) return 3;
    s->base = s->bits;
    if (!s->base) return 3;
    s->locks++;
    uint8_t *sp = s->base + s->pitch * srcy + srcx;
    if (!sp) return 3;
    uint8_t *dp = NULL;
    if (x < width && y < rows) {
        lock_dib(d);
        dp = d->base + d->pitch * y + x;
    }
    if (!dp) {
        unlock_dib(s);
        return 3;
    }
    *game_copy_rem = w & 3;
    for (int row = 0; row < h; row++) memcpy(dp + (intptr_t)d->pitch * row, sp + (intptr_t)s->pitch * row, (size_t)w);
    unlock_dib(d);
    unlock_dib(s);
    return 0;
}

// (The game's two shorter copies, which take a rectangle at the same place in
// both buffers, call this one, and stay with the original.)

// A pixel of the pixels at (x, y), or 0 outside the buffer, locking the buffer.
static uint8_t *lock_at(buffer_t *b, int x, int y) {
    if (x >= b->width || y >= -b->neg_height) return NULL;
    b->base = b->bits;
    if (!b->base) return NULL;
    b->locks++;
    return b->base + b->pitch * y + x;
}

// Cut the same way as the copy, but the pixels of the buffer given that are
// the key colour are replaced by the pixels of *this* buffer, not the other
// way round (OpenSMACX has it as a copy to this buffer; the original writes
// into the other and only reads this). The other buffer's clip rectangle
// still decides which part is drawn. Does not look at overlap.
int __attribute__((thiscall)) buffer_copy_mask(buffer_t *d, buffer_t *s, int x, int y, int sx, int sy, int w, int h, int key) {
    if (d->dd || (s && s->dd)) return orig_buffer_copy_mask(d, s, x, y, sx, sy, w, h, key);
    if (!d->bits) return 7;
    if (!s) return 3;
    if (x < 0) w += x, sx -= x, x = 0;
    if (y < 0) h += y, sy -= y, y = 0;
    int width = d->width, rows = -d->neg_height;
    if (x > width || y > rows) return 0;
    if (x + w > width) w = width - x;
    if (y + h > rows) h = rows - y;
    int32_t r[4] = {sx, sy, sx + w, sy + h};
    if (!intersect(r, r, s->clip)) return 0;
    x += r[0] - sx, y += r[1] - sy;
    w = r[2] - r[0], h = r[3] - r[1];
    uint8_t *wp = lock_at(s, r[0], r[1]);
    if (!wp) return 3;
    uint8_t *rp = lock_at(d, x, y);
    if (!rp) return 3;  // leaves the other buffer locked
    *game_mask_units = (w >> 2) + ((w & 3) != 0);
    *game_mask_gap_a = d->pitch - w;
    *game_mask_gap_b = s->pitch - w;
    for (int row = 0; row < h; row++, wp += s->pitch, rp += d->pitch)
        for (int i = 0; i < w; i++)
            if (wp[i] == (uint8_t)key) wp[i] = rp[i];
    unlock_dib(d);
    unlock_dib(s);
    return 0;
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

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// A line, patterned or not, writes within the box between its ends.
#define LINE_BOX writes_rect(&w_, b, imin(x1, x2), imin(y1, y2), imax(x1, x2) + 1, imax(y1, y2) + 1)
BUF_WRITER(void, buffer_line, (buffer_t * b, int x1, int y1, int x2, int y2, int color), (b, x1, y1, x2, y2, color), LINE_BOX,
           "%d, %d, %d, %d, %d", x1, y1, x2, y2, color)
BUF_WRITER(void, buffer_pat_line, (buffer_t * b, int x1, int y1, int x2, int y2, int c1, int c2, int a, int bl, int phase),
           (b, x1, y1, x2, y2, c1, c2, a, bl, phase), LINE_BOX, "%d, %d, %d, %d, %d, %d, %d, %d, %d", x1, y1, x2, y2, c1, c2,
           a, bl, phase)
BUF_WRITER(void, buffer_pat_hline, (buffer_t * b, int x1, int x2, int y, int c1, int c2, int a, int bl, int phase),
           (b, x1, x2, y, c1, c2, a, bl, phase), writes_rect(&w_, b, imin(x1, x2), y, imax(x1, x2) + 1, y + 1),
           "%d, %d, %d, %d, %d, %d, %d, %d", x1, x2, y, c1, c2, a, bl, phase)
BUF_WRITER(void, buffer_pat_vline, (buffer_t * b, int x, int y1, int y2, int c1, int c2, int a, int bl, int phase),
           (b, x, y1, y2, c1, c2, a, bl, phase), writes_rect(&w_, b, x, imin(y1, y2), x + 1, imax(y1, y2) + 1),
           "%d, %d, %d, %d, %d, %d, %d, %d", x, y1, y2, c1, c2, a, bl, phase)
BUF_WRITER(void, buffer_box, (buffer_t * b, int x1, int y1, int x2, int y2, int c1, int c2), (b, x1, y1, x2, y2, c1, c2),
           LINE_BOX, "%d, %d, %d, %d, %d, %d", x1, y1, x2, y2, c1, c2)
BUF_WRITER(void, buffer_dotted_box, (buffer_t * b, int x1, int y1, int x2, int y2, int color), (b, x1, y1, x2, y2, color),
           LINE_BOX, "%d, %d, %d, %d, %d", x1, y1, x2, y2, color)
BUF_WRITER(int, buffer_box_rect, (buffer_t * b, const int32_t *rect, int c1, int c2), (b, rect, c1, c2),
           rect ? writes_rect(&w_, b, rect[0], rect[1], rect[2], rect[3]) : writes_rect(&w_, b, 0, 0, 0, 0), "%d, %d, %d",
           rect != NULL, c1, c2)
BUF_WRITER(int, buffer_dotted_box_rect, (buffer_t * b, const int32_t *rect, int color), (b, rect, color),
           rect ? writes_rect(&w_, b, rect[0], rect[1], rect[2], rect[3]) : writes_rect(&w_, b, 0, 0, 0, 0), "%d, %d",
           rect != NULL, color)

// The copies write the scratch the original leaves in the game's globals; the
// destination's pixels (and, for the masked one, the other buffer's) as well.
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, (void *)game_copy_rem, 4);
    hooks_writes_add(w, (void *)game_mask_units, 4);
    hooks_writes_add(w, (void *)game_mask_gap_a, 4);
    hooks_writes_add(w, (void *)game_mask_gap_b, 4);
}
static void writes_copy(struct hook_writes *w, buffer_t *d, buffer_t *s, int x, int y, int cw, int ch) {
    writes_rect(w, d, x, y, x + cw, y + ch);
    if (s) hooks_writes_add(w, (void *)s, sizeof *s);
    writes_scratch(w);
}
// The masked copy writes the other buffer, at the place its corner is given, less what was cut off here.
static void writes_mask(struct hook_writes *w, buffer_t *d, buffer_t *s, int x, int y, int sx, int sy, int cw, int ch) {
    hooks_writes_add(w, (void *)d, sizeof *d);
    writes_scratch(w);
    if (!s) return;
    int cx = x < 0 ? -x : 0, cy = y < 0 ? -y : 0;
    writes_rect(w, s, sx + cx, sy + cy, sx + cw, sy + ch);
}

// A check of a copy between two buffers: not for surfaces, and for the plain
// copy not within one buffer (left to the original).
#define BUF2_WRITER(ret, name, params, args, writes, extra_orig, fmt, ...)                       \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__)                         \
    static ret THISCALL shadow_##name params {                                                    \
        if (hooks_nested || d->dd || (s && s->dd) || (extra_orig)) SHADOW_TAIL_##ret(orig_##name args); \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);                                        \
        stats_##name.calls++;                                                                     \
        shadow_tick(BUFFER);                                                                      \
        SHADOW_TAIL_##ret(check_##name args);                                                     \
    }

BUF2_WRITER(int, buffer_copy_full, (buffer_t * d, buffer_t *s, int x, int y, int sx, int sy, int w, int h),
            (d, s, x, y, sx, sy, w, h), writes_copy(&w_, d, s, x, y, w, h), s == d, "%d, %d, %d, %d, %d, %d", x, y, sx, sy,
            w, h)
BUF2_WRITER(int, buffer_copy_mask, (buffer_t * d, buffer_t *s, int x, int y, int sx, int sy, int w, int h, int key),
            (d, s, x, y, sx, sy, w, h, key), writes_mask(&w_, d, s, x, y, sx, sy, w, h), 0, "%d, %d, %d, %d, %d, %d, %d", x,
            y, sx, sy, w, h, key)

// ---- Sweep -------------------------------------------------------------------

enum { SW = 61, SH = 40, SPITCH = 64 };  // not a multiple of 4 wide, so the ragged ends are used
enum { SBW = 50, SBH = 35, SBPITCH = 56 };  // the buffer copied from

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// A made-up DIB buffer: random pixels, a random clip rectangle, and some
// calls already in progress (a lock count above 0), which the drawing
// functions leave in the lock.
static void reset_sized(buffer_t *b, uint8_t *pixels, int bw, int bh, int pitch) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < pitch * bh + 8; i++) pixels[i] = (uint8_t)rnd();
    b->bits = pixels;
    b->width = bw, b->neg_height = -bh, b->pitch = pitch;
    b->full[2] = bw, b->full[3] = bh;
    b->clip[0] = 0, b->clip[1] = 0, b->clip[2] = bw, b->clip[3] = bh;
    if (rnd_to(3)) {
        int l = rnd_to(bw), t = rnd_to(bh);
        // Never empty: the original's vline then counts down from 0 and draws until it faults.
        b->clip[0] = l, b->clip[1] = t, b->clip[2] = l + 1 + rnd_to(bw - l), b->clip[3] = t + 1 + rnd_to(bh - t);
    }
    int nested = rnd_to(4) == 0 ? 1 + rnd_to(2) : 0;
    b->locks = nested;
    b->base = nested ? pixels : NULL;
}
static void reset_buffer(buffer_t *b, uint8_t *pixels) { reset_sized(b, pixels, SW, SH, SPITCH); }

static int coord(int span) { return rnd_to(span + 24) - 12; }  // a little way outside as well

// A dotted line the clip rectangle cuts down to one pixel: the original does not stop.
static int dotted_stuck(int lo_a, int hi_a, int lo_clip, int hi_clip) {
    int lo = imin(lo_a, hi_a), hi = imax(lo_a, hi_a);
    return imin(hi, hi_clip - 1) - imax(lo, lo_clip) == 0;
}

// Lines, patterned lines and boxes, over the same kinds of input.
static void sweep_lines(buffer_t *b, int color, int k) {
    int x1 = coord(SW), y1 = coord(SH), x2 = coord(SW), y2 = coord(SH);
    if (k == 2) x2 = x1;  // vertical
    if (k == 3) y2 = y1;  // horizontal
    check_buffer_line(b, x1, y1, x2, y2, color);
    int c1 = rnd_to(4) ? (int)rnd() : -1, c2 = rnd_to(4) ? (int)rnd() : -1;
    int pa = rnd_to(6), pb = rnd_to(6), phase = rnd_to(40) - 20;
    if (pa + pb == 0) pb = 1;  // the original divides by the two
    check_buffer_pat_line(b, x1, y1, x2, y2, c1, c2, pa, pb, phase);
    int hx1 = coord(SW), hx2 = coord(SW), hy = coord(SH), vx = coord(SW), vy1 = coord(SH), vy2 = coord(SH);
    check_buffer_pat_hline(b, hx1, hx2, hy, c1, c2, pb, pa + 1, phase);
    check_buffer_pat_vline(b, vx, vy1, vy2, c2, c1, pa + 1, pb, -phase);
    check_buffer_box(b, x1, y1, x2, y2, c1, c2);
    int32_t r[4] = {x1, y1, x2 + 1, y2 + 1};
    check_buffer_box_rect(b, r, c2, c1);
    // Dotted boxes: not if one of their four lines would be cut to one pixel.
    if (!dotted_stuck(x1 + 1, x2, b->clip[0], b->clip[2]) && !dotted_stuck(x1, x2 - 1, b->clip[0], b->clip[2]) &&
        !dotted_stuck(y1, y2 - 1, b->clip[1], b->clip[3]) && !dotted_stuck(y1 + 1, y2, b->clip[1], b->clip[3])) {
        check_buffer_dotted_box(b, x1, y1, x2, y2, color);
        check_buffer_dotted_box_rect(b, r, color);
    }
}

static void buffer_sweep(void) {
    static buffer_t b, sb;
    static uint8_t pixels[SPITCH * SH + 8], spixels[SBPITCH * SBH + 8];
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
            sweep_lines(&b, color, k);
            check_buffer_get_pixel(&b, coord(SW), coord(SH));
            check_buffer_put_pixel(&b, coord(SW), coord(SH), color);
            int32_t r[4] = {coord(SW), coord(SH), coord(SW), coord(SH)};
            if (k & 1) r[2] = r[0] + rnd_to(SW), r[3] = r[1] + rnd_to(SH);
            check_buffer_fill_rect(&b, r, color);
            check_buffer_fill(&b, coord(SW), coord(SH), rnd_to(SW + 8) - 4, rnd_to(SH + 8) - 4, color);
        }
        // Copies from another buffer, of another size and pitch.
        reset_sized(&sb, spixels, SBW, SBH, SBPITCH);
        if (rnd_to(2))
            for (int i = 0; i < SBPITCH * SBH + 8; i++) spixels[i] &= 3;  // few colours, so the key is found
        for (int k = 0; k < 6; k++) {
            int x = coord(SW), y = coord(SH), sx = coord(SBW), sy = coord(SBH), cw = rnd_to(SW + 8) - 4, ch = rnd_to(SH + 8) - 4;
            check_buffer_copy_full(&b, &sb, x, y, sx, sy, cw, ch);
            if (k & 1) check_buffer_copy_mask(&b, &sb, x, y, sx, sy, cw, ch, rnd_to(4));
        }
        check_buffer_copy_full(&b, NULL, 1, 1, 1, 1, 3, 3);
        check_buffer_copy_mask(&b, NULL, 1, 1, 1, 1, 3, 3, 0);
        sb.bits = NULL;
        check_buffer_copy_full(&b, &sb, 1, 1, 1, 1, 3, 3);
        check_buffer_copy_mask(&b, &sb, 1, 1, 1, 1, 3, 3, 0);
        check_buffer_fill_rect(&b, NULL, 3);
        check_buffer_hline(&b, 5, 5, 5, 1);
        check_buffer_vline(&b, 5, 5, 5, 1);
        check_buffer_fill_all(&b, (int)rnd());
        memcpy(b.clip, b.full, 16);  // the whole buffer: the other fill
        check_buffer_fill_all(&b, (int)rnd());
        b.bits = NULL;  // no pixels at all
        reset_sized(&sb, spixels, SBW, SBH, SBPITCH);
        check_buffer_copy_full(&b, &sb, 1, 1, 1, 1, 3, 3);
        check_buffer_copy_mask(&b, &sb, 1, 1, 1, 1, 3, 3, 0);
        check_buffer_hline(&b, 1, 9, 2, 3);
        check_buffer_line(&b, 1, 2, 9, 7, 3);
        check_buffer_pat_line(&b, 1, 2, 9, 7, 3, 4, 2, 2, 0);
        check_buffer_pat_hline(&b, 1, 9, 2, 3, 4, 2, 2, 0);
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
    // sub esp, 0x14; push ebx, esi, edi; mov edi, ecx
    HOOK(0x5dff00, buffer_copy_full, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x14, 0x53, 0x56, 0x57, 0x8b, 0xf9),
    HOOK(0x5e2b00, buffer_copy_mask, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x38, 0x53, 0x56, 0x57),  // sub esp, 0x38
    HOOK(0x5e09d0, buffer_pat_line, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x84, 0x00, 0x00, 0x00),  // sub esp, 0x84
    HOOK(0x5e15f0, buffer_line, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00),      // sub esp, 0x80
    HOOK(0x5e10c0, buffer_pat_hline, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x70, 0x53, 0x56, 0x8b, 0xf1),
    HOOK(0x5e1350, buffer_pat_vline, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x74, 0x53, 0x56, 0x8b, 0xf1),
    HOOK(0x5e3193, buffer_box, 0x8b, 0x44, 0x24, 0x14, 0x53, 0x8b, 0x5c, 0x24, 0x10),
    HOOK(0x5e3203, buffer_box_rect, 0x8b, 0x44, 0x24, 0x04, 0x53, 0x85, 0xc0, 0x8b, 0xd9),
    HOOK(0x5e3283, buffer_dotted_box, 0x8b, 0x44, 0x24, 0x04, 0x53, 0x8b, 0x5c, 0x24, 0x0c),
    HOOK(0x5e32f3, buffer_dotted_box_rect, 0x8b, 0x44, 0x24, 0x04, 0x53, 0x85, 0xc0, 0x8b, 0xd9),
};
// The palette's sweep runs with this one: the game seldom calls its functions.
extern struct hook_module palette_module;
struct hook_module buffer_module = {"buffer", buffer_hooks, HOOK_COUNT(buffer_hooks), buffer_sweep, &palette_module, -1};
