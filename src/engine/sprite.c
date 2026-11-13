// The game's Sprite class: drawing a sprite on a Buffer, scaled, with a key
// colour, a single colour or a colour table, and the tables the scaled
// drawing works from. Reimplemented from terranx.exe 2.0's disassembly; the
// class is called Sprite in OpenSMACX (GPL-3.0-or-later), which names the
// functions but has none of the logic.
//
// A sprite is a rectangle of 8-bit pixels with the margins of one colour cut
// off (see game_sprite). Drawing it is one routine in the original, written
// out eight times with a different thing done to each pixel:
//   draw_3            copy                                   pixel != key
//   draw_mono_2       one colour                             pixel != key
//   draw_dest         pixel 0 darkens, else copy             pixel != key
//   draw_dest_unk1    darkens (no key colour)                pixel == 0
//   draw_dest_source  darkens                                pixel != key
//   draw_multi_table_dest_2  new colour from the pixel and what is under it
//   draw_4            new colour from the pixel
// Each has an unscaled path and one for scaling up and one for scaling down
// (the scale is a numerator and a denominator the game sets in globals, and
// a table made from it by fill_scaling_table says how many pixels each pixel
// becomes, or how many pixels each pixel drawn skips). They keep their
// working values in globals, which the original leaves behind; those are
// written here too (game_sprite_scratch), so a later call that reads a stale
// one, or a check that compares them, sees the same.
//
// draw_dest_unk2_2 (5e81bc) is draw_4 with a different test per pixel; its
// unrolled loops disagree with each other (see OP_UNK2_FIRST), and the game
// was not seen to call it, so the sweep is what checks it.
// What is left to the original: the constructor, close, init, extract, copy,
// create_blank, save and load (they allocate from the game's heap or read and
// write files), the draws of a sprite loaded by name (they go through the
// game's resource code), and the draws into a DirectDraw surface.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shadow.h"
#include "sprite.h"

extern struct hook_module sprite_module;
#define SPRITE (&sprite_module)

typedef struct game_sprite sprite_t;
typedef struct game_buffer buffer_t;
#define SC (game_sprite_scratch)

// The buffer's lock, the way the original counts it for a DIB. 0 if there are no pixels.
static int lock_buffer(buffer_t *b) {
    b->base = b->bits;
    if (!b->base) return 0;
    b->locks++;
    return 1;
}
static void unlock_buffer(buffer_t *b) {
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

SHADOW_DECLARE_T(int, sprite_draw_3, (sprite_t * s, buffer_t *b, int key, int x, int y))
SHADOW_DECLARE_T(int, sprite_draw_mono_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int colour))
SHADOW_DECLARE_T(int, sprite_draw_dest, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_unk1, (sprite_t * s, buffer_t *b, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_source, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_multi_table_dest_2, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_4, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_unk2_2, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_mono, (sprite_t * s, buffer_t *b, int key, int x, int y, int colour, int num, int den))
SHADOW_DECLARE_T(int, sprite_draw, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den))
SHADOW_DECLARE_T(int, sprite_draw_dest_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_unk1_2, (sprite_t * s, buffer_t *b, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_source_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_dest_unk2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(int, sprite_draw_multi_table_dest, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(void, sprite_fill_scaling_table, (sprite_t * s))
SHADOW_DECLARE_T(int, sprite_get_clipped_x_table_expand, (sprite_t * s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6))
SHADOW_DECLARE_T(int, sprite_get_clipped_y_table_expand, (sprite_t * s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6))
SHADOW_DECLARE_T(int, sprite_get_clipped_x_table_shrink, (sprite_t * s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6))
SHADOW_DECLARE_T(int, sprite_get_clipped_y_table_shrink, (sprite_t * s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6))
SHADOW_DECLARE_T(int, sprite_change_color, (sprite_t * s, int from, int to))

// ---- The scaling table ---------------------------------------------------------

// Makes the table for the scale in the globals: 800 entries. With a scale
// above 1 (numerator over denominator) entry i is how many pixels source
// pixel i becomes, and below 1 how many source pixels the i-th pixel drawn
// moves on by: either way a whole number, or one more for a share of the
// pixels that spreads the remainder evenly (an error term, as for a line).
void SPRITE_THIS sprite_fill_scaling_table(sprite_t *s) {
    (void)s;
    int32_t *tab = SC->scale;
    int32_t num = game_sprite_scale[0], den = game_sprite_scale[1];
    int32_t big = num > den ? num : den, small = num < den ? num : den;
    int32_t quot, rem2, step, err;
    if (small > 1) {
        quot = big / small;
        rem2 = (big % small) * 2;
        step = small * 2;
        err = (rem2 >> 1) - step;
    } else {
        quot = big, rem2 = 0, step = 0, err = 0;
    }
    if (quot & 1) err += big;
    if (small == 1) {
        for (int i = 0; i < 800; i++) tab[i] = quot;
        return;
    }
    for (int i = 0; i < 800; i++) {
        err += rem2;
        tab[i] = quot;
        if (err > 0) tab[i] = quot + 1, err -= step;
    }
}

// ---- The clipped tables --------------------------------------------------------
//
// Four functions, for the columns and for the rows, scaling up and down, work
// out the part of a scaled sprite on the buffer. `orig` is the rectangle the
// whole scaled sprite covers and `clip` what is left of it on the buffer;
// along the axis the table is for (0 across, 1 down).

// Scaling up. Of the table's entries, skips those the left (or top) cut off
// and gives the first one that is left whole, or what is left of one that is
// partly cut off, and so on for as much as `clip` is long, the last cut to
// fit: `out` is those entries, *count how many, *src_start the sprite pixel
// (or row) the first one is for, *dest_off how far into the scaled sprite the
// part starts, *size how long it is. NULL if it is nothing.
static int32_t *table_expand(int axis, const int32_t *orig, const int32_t *clip, int32_t *src_start, int32_t *dest_off,
                             int32_t *count, int32_t *size, int32_t *out) {
    int32_t lo = clip[axis], hi = clip[axis + 2];
    int32_t off = lo - orig[axis];
    int32_t len = hi - lo;
    if (len == 0) return NULL;
    *size = len;
    *dest_off = off;
    int k = 0;
    if (off > 0) {
        do {
            off -= SC->scale[k];
            k++;
        } while (off > 0);
    }
    const int32_t *sp = &SC->scale[k];
    int32_t *o;
    int n;
    if (off < 0) {
        off = -off;
        len -= off;
        k--;
        out[0] = off;
        *src_start = k;
        o = out + 1, n = 1;
    } else {
        *src_start = k;
        o = out, n = 0;
    }
    while (len > 0) {
        int32_t e = *sp++;
        *o++ = e;
        len -= e;
        n++;
    }
    if (len < 0) o[-1] += len;
    *count = n;
    return out;
}

// Scaling down. The pixels drawn start `dest_off` into the scaled sprite, so
// the first `dest_off` entries are skipped, their sum being how far into the
// sprite the part starts (*src_start); then *size entries are copied to
// `out`, and *total is their sum: the sprite pixels (or rows) it spans.
static int32_t *table_shrink(int axis, const int32_t *orig, const int32_t *clip, int32_t *dest_off, int32_t *src_start,
                             int32_t *size, int32_t *total, int32_t *out) {
    int32_t lo = clip[axis], hi = clip[axis + 2];
    int32_t off = lo - orig[axis];
    int32_t len = hi - lo;
    if (len == 0) return NULL;
    *dest_off = off;
    *size = len;
    *src_start = 0;
    int k = 0;
    if (off > 0) {
        k = off;
        for (int i = 0; i < off; i++) *src_start += SC->scale[i];
    }
    *total = 0;
    for (int i = 0; i < len; i++) {
        int32_t e = SC->scale[k + i];
        out[i] = e;
        *total += e;
    }
    return out;
}

#define PTR(p) ((int)(uintptr_t)(p))
int SPRITE_THIS sprite_get_clipped_x_table_expand(sprite_t *s, const int32_t *orig, const int32_t *clip, int32_t *a3,
                                                  int32_t *a4, int32_t *a5, int32_t *a6) {
    (void)s;
    return PTR(table_expand(0, orig, clip, a3, a4, a5, a6, SC->xtab));
}
int SPRITE_THIS sprite_get_clipped_y_table_expand(sprite_t *s, const int32_t *orig, const int32_t *clip, int32_t *a3,
                                                  int32_t *a4, int32_t *a5, int32_t *a6) {
    (void)s;
    return PTR(table_expand(1, orig, clip, a3, a4, a5, a6, SC->ytab));
}
int SPRITE_THIS sprite_get_clipped_x_table_shrink(sprite_t *s, const int32_t *orig, const int32_t *clip, int32_t *a3,
                                                  int32_t *a4, int32_t *a5, int32_t *a6) {
    (void)s;
    return PTR(table_shrink(0, orig, clip, a3, a4, a5, a6, SC->xtab));
}
int SPRITE_THIS sprite_get_clipped_y_table_shrink(sprite_t *s, const int32_t *orig, const int32_t *clip, int32_t *a3,
                                                  int32_t *a4, int32_t *a5, int32_t *a6) {
    (void)s;
    return PTR(table_shrink(1, orig, clip, a3, a4, a5, a6, SC->ytab));
}

// ---- Drawing -------------------------------------------------------------------

enum { OP_COPY, OP_MONO, OP_DEST, OP_DEST_ZERO, OP_DEST_SRC, OP_MULTI, OP_REMAP, OP_UNK2, OP_UNK2_FIRST, OP_UNK2_REST };
struct paint {
    uint8_t key, colour;
    const uint8_t *table;
};

// What is done to one pixel of the buffer (d) for one of the sprite (s).
static inline __attribute__((always_inline)) void paint(int op, const struct paint *p, uint8_t *d, uint8_t s) {
    switch (op) {
    case OP_COPY:
        if (s != p->key) *d = s;
        break;
    case OP_MONO:
        if (s != p->key) *d = p->colour;
        break;
    case OP_DEST:
        if (s == p->key) break;
        *d = s == 0 ? p->table[*d] : s;
        break;
    case OP_DEST_ZERO:
        if (s == 0) *d = p->table[*d];
        break;
    case OP_DEST_SRC:
        if (s != p->key) *d = p->table[*d];
        break;
    case OP_MULTI:
        if (s != p->key) *d = p->table[(s << 8) | *d];
        break;
    case OP_REMAP:
        if (s != p->key) *d = p->table[s];
        break;
    // draw_dest_unk2_2: neither the key nor colour 0 draws, the rest darken by the table.
    case OP_UNK2:
        if (s != p->key && s != 0) *d = p->table[*d];
        break;
    // Its scaled-down path is written out four pixels at a time, and the four
    // are not alike: the first of each four (counted from where the row's
    // width makes the unrolled loop start) looks the new colour up from the
    // sprite's own pixel when that is not 0, the other three do so only when
    // it is 0 (so from table entry 0).
    case OP_UNK2_FIRST:
        if (s != p->key && s != 0) *d = p->table[s];
        break;
    case OP_UNK2_REST:
        if (s != p->key && s == 0) *d = p->table[s];
        break;
    }
}

// What the scaled-up draws do to the pixels one pixel of the sprite makes,
// which differs from what they do one pixel at a time: the dest draws look up
// the new colour once, from the first pixel of the run in the top row (d),
// and draw that over the whole run, in every row. Draw_4's scaled-up path
// is different again: it leaves the pixels of colour 0 alone as well as the
// key, and the table gives the new colour for the sprite's own pixel when it
// makes 1 to 3 rows, but for the pixel under it when it makes 4 (n rows).
// Returns false if the run is left alone.
static inline __attribute__((always_inline)) int expand_value(int op, const struct paint *p, uint8_t s, uint8_t d, int n, uint8_t *v) {
    switch (op) {
    case OP_COPY: *v = s; return s != p->key;
    case OP_MONO: *v = p->colour; return s != p->key;
    case OP_DEST:
        if (s == p->key) return 0;
        *v = s == 0 ? p->table[d] : s;
        return 1;
    case OP_DEST_ZERO: *v = p->table[d]; return s == 0;
    case OP_DEST_SRC: *v = p->table[d]; return s != p->key;
    case OP_REMAP:
        *v = p->table[n < 4 ? s : d];
        return s != p->key && s != 0;
    case OP_MULTI: *v = p->table[(s << 8) | d]; return s != p->key;
    case OP_UNK2: *v = p->table[n < 4 ? s : d]; return s != p->key && s != 0;  // as draw_4's
    }
    return 0;
}

// Where the scaled sprite goes: its size and corner scaled from the sprite's,
// each cut to a whole number towards 0 (the original's float arithmetic, in
// the FPU's extended precision).
static void scaled_rect(const sprite_t *s, int x, int y, long double ratio, int32_t *r) {
    int h = (int)((long double)s->height * ratio);
    int w = (int)((long double)s->width * ratio);
    int t = (int)((long double)y + (long double)s->top * ratio);
    int l = (int)((long double)x + (long double)s->left * ratio);
    r[0] = l, r[1] = t, r[2] = l + w, r[3] = t + h;
}

// The sprite drawn at (x, y). `op` and `p` say what is done to the pixels. 3
// if there is no picture or buffer (or the buffer has no pixels), else 0.
static inline __attribute__((always_inline)) int draw_core(int op, const struct paint *p, sprite_t *s, buffer_t *b, int x, int y) {
    struct game_sprite_scratch *sc = SC;
    int32_t *scale = game_sprite_scale;
    if (!s->data || !b || !lock_buffer(b)) return 3;
    int32_t num = scale[0], den = scale[1];
    if (num != den && !(scale[2] == num && scale[3] == den)) {
        float r = (float)((long double)num / (long double)den);
        *game_sprite_ratio = r;
        if (r > *game_sprite_max_ratio) {
            unlock_buffer(b);
            return 0;
        }
        sprite_fill_scaling_table(s);
        scale[2] = num, scale[3] = den;
    }
    if (op == OP_MONO) sc->color = p->colour;
    sc->stride = s->stride;
    sc->pitch = b->pitch;
    const uint8_t *data = s->data;
    int32_t r0[4], r1[4];

    if (num == den) {
        r0[0] = x + s->left, r0[1] = y + s->top, r0[2] = r0[0] + s->width, r0[3] = r0[1] + s->height;
        memcpy(r1, r0, 16);
        if (!intersect(r1, r1, b->clip)) {
            unlock_buffer(b);
            return 0;
        }
        const uint8_t *src = data + (r1[0] - r0[0]) + (intptr_t)(r1[1] - r0[1]) * (op == OP_MONO ? s->width : s->stride);  // only the mono draw goes by the width
        uint8_t *dst = b->base + r1[0] + (intptr_t)r1[1] * sc->pitch;
        sc->rows = r1[3] - r1[1];
        sc->width = r1[2] - r1[0];
        sc->xgroups = sc->width >> 2;
        sc->src_gap = sc->stride - sc->width;
        sc->dst_gap = sc->pitch - sc->width;
        if (sc->width & 3) sc->xgroups++;
        for (int row = 0; row < sc->rows; row++, src += sc->stride, dst += sc->pitch)
            for (int i = 0; i < sc->width; i++) paint(op, p, dst + i, src[i]);
        // All but the first two count the rows down in memory as they go.
        if (op != OP_COPY && op != OP_MONO) sc->rows = 0;
        unlock_buffer(b);
        return 0;
    }

    scaled_rect(s, x, y, (long double)*game_sprite_ratio, r0);
    memcpy(r1, r0, 16);
    if (!intersect(r1, r1, b->clip)) {
        unlock_buffer(b);
        return 0;
    }
    uint8_t *dst = b->base + r0[0] + (intptr_t)r0[1] * sc->pitch;
    sc->pitch = b->pitch;
    int32_t sx, sy, dx, dy;
    if (num < den) {
        if (r0[2] - r0[0] < game_sprite_min_size[0] || r0[3] - r0[1] < game_sprite_min_size[1]) {
            unlock_buffer(b);
            return 0;
        }
        intersect(r1, r0, b->clip);
        int32_t *xt = table_shrink(0, r0, r1, &dx, &sx, &sc->width, &sc->x_count, sc->xtab);
        sc->xtab_ptr = (uint32_t)(uintptr_t)xt;
        if (!xt) {
            unlock_buffer(b);
            return 0;
        }
        int32_t *yt = table_shrink(1, r0, r1, &dy, &sy, &sc->rows, &sc->ycount, sc->ytab);
        sc->ytab_ptr = (uint32_t)(uintptr_t)yt;
        if (!yt) {
            unlock_buffer(b);
            return 0;
        }
        sc->xgroups = sc->width >> 2;
        sc->dst_gap = sc->pitch - sc->width;
        sc->src_gap = sc->stride - sc->x_count;
        dst += (intptr_t)sc->pitch * dy + dx;
        const uint8_t *src = data + (uint32_t)sc->stride * (uint32_t)sy + sx;
        if (sc->width & 3) sc->xgroups++;
        do {
            const int32_t *xs = (const int32_t *)(uintptr_t)sc->xtab_ptr;
            for (int i = 0; i < sc->width; i++) {
                int pop = op;
                if (op == OP_UNK2) pop = ((i + ((4 - (sc->width & 3)) & 3)) & 3) == 0 ? OP_UNK2_FIRST : OP_UNK2_REST;
                paint(pop, p, dst + i, *src);
                src += xs[i];
            }
            uint8_t yc = (uint8_t) * (const int32_t *)(uintptr_t)sc->ytab_ptr;
            sc->ytab_ptr += 4;
            src += (uint32_t)(yc - 1u) * (uint32_t)sc->stride;
            src += sc->src_gap;
            dst += sc->width + sc->dst_gap;
        } while (--sc->rows != 0);
    } else {
        intersect(r1, r0, b->clip);
        int32_t *xt = table_expand(0, r0, r1, &sx, &dx, &sc->x_count, &sc->width, sc->xtab);
        sc->xtab_ptr = (uint32_t)(uintptr_t)xt;
        if (!xt) {
            unlock_buffer(b);
            return 0;
        }
        int32_t *yt = table_expand(1, r0, r1, &sy, &dy, &sc->ycount, &sc->rows, sc->ytab);
        sc->ytab_ptr = (uint32_t)(uintptr_t)yt;
        if (!yt) {
            unlock_buffer(b);
            return 0;
        }
        dst += (intptr_t)sc->pitch * dy + dx;
        sc->dst_gap = sc->pitch - sc->width;
        sc->xgroups = sc->x_count >> 2;
        sc->x_rem = sc->x_count - (sc->xgroups << 2);
        sc->src_gap = sc->stride - sc->x_count;
        const uint8_t *src = data + (uint32_t)sc->stride * (uint32_t)sy + sx;
        if (sc->x_count & 3) sc->xgroups++;
        do {
            const int32_t *xs = (const int32_t *)(uintptr_t)sc->xtab_ptr;
            uint8_t yc = (uint8_t) * (const int32_t *)(uintptr_t)sc->ytab_ptr;
            sc->ytab_ptr += 4;
            int n = yc >= 1 && yc <= 3 ? yc : 4;  // each row of the sprite makes this many rows
            uint8_t *first = dst;
            for (int k = 0; k < sc->x_count; k++) {
                int c = xs[k];
                uint8_t v;
                if (expand_value(op, p, src[k], *first, n, &v))
                    for (int r = 0; r < n; r++) memset(first + (intptr_t)r * sc->pitch, v, c > 0 ? (size_t)c : 0);
                first += c;
            }
            src += sc->stride;
            dst += (intptr_t)n * sc->pitch;
        } while (--sc->ycount != 0);
    }
    unlock_buffer(b);
    return 0;
}

#define NEEDS_ORIG(s, b) ((b) && (b)->dd)

int SPRITE_THIS sprite_draw_3(sprite_t *s, buffer_t *b, int key, int x, int y) {
    if (s->name || NEEDS_ORIG(s, b)) return orig_sprite_draw_3(s, b, key, x, y);
    struct paint p = {(uint8_t)key, 0, NULL};
    return draw_core(OP_COPY, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_mono_2(sprite_t *s, buffer_t *b, int key, int x, int y, int colour) {
    if (s->name || NEEDS_ORIG(s, b)) return orig_sprite_draw_mono_2(s, b, key, x, y, colour);
    struct paint p = {(uint8_t)key, (uint8_t)colour, NULL};
    return draw_core(OP_MONO, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_dest(sprite_t *s, buffer_t *b, int key, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_dest(s, b, key, x, y, table);
    struct paint p = {(uint8_t)key, 0, table};
    return draw_core(OP_DEST, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_dest_unk1(sprite_t *s, buffer_t *b, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_dest_unk1(s, b, x, y, table);
    struct paint p = {0, 0, table};
    return draw_core(OP_DEST_ZERO, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_dest_source(sprite_t *s, buffer_t *b, int key, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_dest_source(s, b, key, x, y, table);
    struct paint p = {(uint8_t)key, 0, table};
    return draw_core(OP_DEST_SRC, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_multi_table_dest_2(sprite_t *s, buffer_t *b, int key, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_multi_table_dest_2(s, b, key, x, y, table);
    struct paint p = {(uint8_t)key, 0, table};
    return draw_core(OP_MULTI, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_4(sprite_t *s, buffer_t *b, int key, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_4(s, b, key, x, y, table);
    struct paint p = {(uint8_t)key, 0, table};
    return draw_core(OP_REMAP, &p, s, b, x, y);
}
int SPRITE_THIS sprite_draw_dest_unk2_2(sprite_t *s, buffer_t *b, int key, int x, int y, const uint8_t *table) {
    if (NEEDS_ORIG(s, b)) return orig_sprite_draw_dest_unk2_2(s, b, key, x, y, table);
    struct paint p = {(uint8_t)key, 0, table};
    return draw_core(OP_UNK2, &p, s, b, x, y);
}

// The wrappers: each sets the scale (a numerator and a denominator after the
// coordinates) for the one draw and puts the old one back.
#define WRAP(name, body, params, call)                                \
    int SPRITE_THIS name params {                                     \
        int32_t n0 = game_sprite_scale[0], d0 = game_sprite_scale[1]; \
        game_sprite_scale[0] = num, game_sprite_scale[1] = den;       \
        int r = body call;                                            \
        game_sprite_scale[0] = n0, game_sprite_scale[1] = d0;         \
        return r;                                                     \
    }
WRAP(sprite_draw_mono, sprite_draw_mono_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int colour, int num, int den), (s, b, key, x, y, colour))
WRAP(sprite_draw, sprite_draw_4, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, table))
WRAP(sprite_draw_2, sprite_draw_3, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den), (s, b, key, x, y))
WRAP(sprite_draw_dest_2, sprite_draw_dest, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, table))
WRAP(sprite_draw_dest_unk1_2, sprite_draw_dest_unk1, (sprite_t * s, buffer_t *b, int x, int y, int num, int den, const uint8_t *table), (s, b, x, y, table))
WRAP(sprite_draw_dest_source_2, sprite_draw_dest_source, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, table))
WRAP(sprite_draw_dest_unk2, sprite_draw_dest_unk2_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, table))
WRAP(sprite_draw_multi_table_dest, sprite_draw_multi_table_dest_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, table))

// Replaces one colour of the picture by another (only the low byte of each
// counts). 7 if there is no picture. (A picture of fewer than four pixels is
// left alone, and the original's remainder after the groups of four is right.)
int SPRITE_THIS sprite_change_color(sprite_t *s, int from, int to) {
    if (!s->data) return 7;
    int n = s->height * s->stride;
    if (n < 4) return 0;  // the original works in groups of four, and does nothing for fewer
    for (int i = 0; i < n; i++)
        if (s->data[i] == (uint8_t)from) s->data[i] = (uint8_t)to;
    return 0;
}

// ---- Shadow checks -------------------------------------------------------------
//
// A check puts back what a draw may write: the buffer object (its lock
// fields), the scratch the draw leaves behind, and the pixels of the buffer
// the sprite lands on. Those live outside the game's writable image, so they
// are named here; for a sweep's scratch buffer everything is, to catch a
// stray write as well.

static int sweep_everything;

// The scratch, less four words (0x9b3ac0, 0x9b3ac4, 0x9b3ad0, 0x9b53e4): the
// original's dest draws keep a loop counter, a jump address into their
// unrolled loops and a scaled row's tail there. Each call sets them before it
// reads them and nothing else does, so they are not reproduced.
static void writes_scratch(struct hook_writes *w) {
    uint8_t *base = (uint8_t *)SC;
    hooks_writes_add(w, base, 8);
    hooks_writes_add(w, base + 0x10, 8);
    hooks_writes_add(w, base + 0x1c, 0x9b53e4 - 0x9b3ad4);
    hooks_writes_add(w, base + (0x9b53e8 - 0x9b3ab8), 0x9b607c - 0x9b53e8);
}

static uint8_t *pixel_at(const buffer_t *b, int x, int y) { return b->bits + (intptr_t)b->pitch * y + x; }

// The pixels of a rectangle (clamped to the buffer).
static void writes_pixels(struct hook_writes *w, buffer_t *b, int l, int t, int r, int bt) {
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
    uint8_t *lo = pixel_at(b, l, t), *hi = pixel_at(b, r - 1, bt - 1);
    hooks_writes_add(w, (void *)lo, (size_t)(hi - lo) + 1);
}

// What a draw may write. The pixels it can reach are where the scaled sprite
// lands, within the clip rectangle.
static void writes_draw(struct hook_writes *w, const sprite_t *s, buffer_t *b, int x, int y) {
    writes_scratch(w);
    hooks_writes_add(w, (void *)&game_sprite_scale[0], 16);  // [0] and [1] too: the wrappers set them for a draw
    hooks_writes_add(w, (void *)game_sprite_ratio, 4);
    if (!b) return;
    hooks_writes_add(w, (void *)b, sizeof *b);
    if (!s->data) return;
    int32_t r[4], num = game_sprite_scale[0], den = game_sprite_scale[1];
    if (num == den)
        r[0] = x + s->left, r[1] = y + s->top, r[2] = r[0] + s->width, r[3] = r[1] + s->height;
    else
        scaled_rect(s, x, y, (long double)(float)((long double)num / (long double)den), r);
    int32_t c[4];
    if (!intersect(c, r, b->clip)) return;
    writes_pixels(w, b, c[0], c[1], c[2], c[3]);
}

#define DETAIL game_sprite_scale[0], game_sprite_scale[1], s->width, s->height, s->left, s->top, b ? b->clip[0] : 0, b ? b->clip[1] : 0, b ? b->clip[2] : 0, b ? b->clip[3] : 0
#define SPR_WRITER(fn, params, args, writes, fmt, ...)                       \
    SHADOW_WRITER_BODY(int, fn, params, args, writes, fmt, __VA_ARGS__)      \
    static int THISCALL shadow_##fn params {                                 \
        if (hooks_nested || s->name || NEEDS_ORIG(s, b)) return orig_##fn args; \
        if (shadow_bypass()) return fn args;                                 \
        stats_##fn.calls++;                                                  \
        shadow_tick(SPRITE);                                                 \
        return check_##fn args;                                              \
    }
// The draws of a sprite with no name (the dest family has no such path).
#define SPR_WRITER_NN(fn, params, args, writes, fmt, ...)                    \
    SHADOW_WRITER_BODY(int, fn, params, args, writes, fmt, __VA_ARGS__)      \
    static int THISCALL shadow_##fn params {                                 \
        if (hooks_nested || NEEDS_ORIG(s, b)) return orig_##fn args;         \
        if (shadow_bypass()) return fn args;                                 \
        stats_##fn.calls++;                                                  \
        shadow_tick(SPRITE);                                                 \
        return check_##fn args;                                              \
    }

SPR_WRITER(sprite_draw_3, (sprite_t * s, buffer_t *b, int key, int x, int y), (s, b, key, x, y),
           writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)
SPR_WRITER(sprite_draw_mono_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int colour), (s, b, key, x, y, colour),
           writes_draw(&w_, s, b, x, y), "%d, %d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, colour, DETAIL)
SPR_WRITER_NN(sprite_draw_dest, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table), (s, b, key, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)
SPR_WRITER_NN(sprite_draw_dest_unk1, (sprite_t * s, buffer_t *b, int x, int y, const uint8_t *table), (s, b, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", x, y, DETAIL)
SPR_WRITER_NN(sprite_draw_dest_source, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table), (s, b, key, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)
SPR_WRITER_NN(sprite_draw_multi_table_dest_2, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table), (s, b, key, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)
SPR_WRITER_NN(sprite_draw_4, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table), (s, b, key, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)

SPR_WRITER_NN(sprite_draw_dest_unk2_2, (sprite_t * s, buffer_t *b, int key, int x, int y, const uint8_t *table), (s, b, key, x, y, table),
              writes_draw(&w_, s, b, x, y), "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d", key, x, y, DETAIL)

// A wrapper writes what its body does for the scale it sets, and sets the
// old one back (writes_draw watches the two words to see that it does).
static void writes_wrapped(struct hook_writes *w, const sprite_t *s, buffer_t *b, int x, int y, int num, int den) {
    int32_t n0 = game_sprite_scale[0], d0 = game_sprite_scale[1];
    game_sprite_scale[0] = num, game_sprite_scale[1] = den;
    writes_draw(w, s, b, x, y);
    game_sprite_scale[0] = n0, game_sprite_scale[1] = d0;
}
#define WRAP_FMT "%d, %d, %d scale %d/%d sprite %dx%d+%d+%d clip %d,%d,%d,%d"
#define WDETAIL num, den, s->width, s->height, s->left, s->top, b ? b->clip[0] : 0, b ? b->clip[1] : 0, b ? b->clip[2] : 0, b ? b->clip[3] : 0
SPR_WRITER(sprite_draw_mono, (sprite_t * s, buffer_t *b, int key, int x, int y, int colour, int num, int den), (s, b, key, x, y, colour, num, den),
           writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER(sprite_draw_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den), (s, b, key, x, y, num, den),
           writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw_dest_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw_dest_unk1_2, (sprite_t * s, buffer_t *b, int x, int y, int num, int den, const uint8_t *table), (s, b, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, 0, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw_dest_source_2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw_dest_unk2, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)
SPR_WRITER_NN(sprite_draw_multi_table_dest, (sprite_t * s, buffer_t *b, int key, int x, int y, int num, int den, const uint8_t *table), (s, b, key, x, y, num, den, table),
              writes_wrapped(&w_, s, b, x, y, num, den), WRAP_FMT, key, x, y, WDETAIL)

// The scaling table is part of the scratch.
SHADOW_WRITER_BODY(void, sprite_fill_scaling_table, (sprite_t * s), (s), hooks_writes_add(&w_, (void *)SC->scale, sizeof SC->scale), "%d", 0)
static void THISCALL shadow_sprite_fill_scaling_table(sprite_t *s) {
    if (hooks_nested) return orig_sprite_fill_scaling_table(s);
    if (shadow_bypass()) return sprite_fill_scaling_table(s);
    stats_sprite_fill_scaling_table.calls++;
    shadow_tick(SPRITE);
    check_sprite_fill_scaling_table(s);
}

// The clipped tables write the scratch and the four places the caller gave.
static void writes_table(struct hook_writes *w, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6) {
    writes_scratch(w);
    hooks_writes_add(w, a3, 4);
    hooks_writes_add(w, a4, 4);
    hooks_writes_add(w, a5, 4);
    hooks_writes_add(w, a6, 4);
}
#define TABLE_PARAMS (sprite_t * s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6)
#define TABLE_WRITER(name)                                                                        \
    SHADOW_WRITER_BODY(int, name, TABLE_PARAMS, (s, orig, clip, a3, a4, a5, a6),                  \
                       writes_table(&w_, a3, a4, a5, a6), "%d, %d", orig[0], clip[0])             \
    static int THISCALL shadow_##name TABLE_PARAMS {                                              \
        if (hooks_nested) return orig_##name(s, orig, clip, a3, a4, a5, a6);                      \
        if (shadow_bypass()) return name(s, orig, clip, a3, a4, a5, a6);                          \
        stats_##name.calls++;                                                                     \
        shadow_tick(SPRITE);                                                                      \
        return check_##name(s, orig, clip, a3, a4, a5, a6);                                       \
    }
TABLE_WRITER(sprite_get_clipped_x_table_expand)
TABLE_WRITER(sprite_get_clipped_y_table_expand)
TABLE_WRITER(sprite_get_clipped_x_table_shrink)
TABLE_WRITER(sprite_get_clipped_y_table_shrink)

// A recolour writes the picture.
SHADOW_WRITER_BODY(int, sprite_change_color, (sprite_t * s, int from, int to), (s, from, to),
                   (s->data ? hooks_writes_add(&w_, s->data, (size_t)(s->height * s->stride)) : (void)0), "%d, %d", from, to)
static int THISCALL shadow_sprite_change_color(sprite_t *s, int from, int to) {
    if (hooks_nested) return orig_sprite_change_color(s, from, to);
    if (shadow_bypass()) return sprite_change_color(s, from, to);
    stats_sprite_change_color.calls++;
    shadow_tick(SPRITE);
    return check_sprite_change_color(s, from, to);
}

// ---- Sweep ---------------------------------------------------------------------
//
// Draws made-up sprites on a made-up buffer, every way: unscaled, scaled up
// and down, cut by the clip rectangle on every side, with every kind of
// pixel in the picture, and the scaling table fresh or already made. Then
// the tables for random rectangles. The game's scratch and scale are put
// back afterwards.

enum { SW = 61, SH = 40, SPITCH = 64 };  // not a multiple of 4 wide, so the ragged ends are used

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void reset_buffer(buffer_t *b, uint8_t *pixels) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < SPITCH * SH + 8; i++) pixels[i] = (uint8_t)rnd();
    b->bits = pixels;
    b->width = SW, b->neg_height = -SH, b->pitch = SPITCH;
    b->full[2] = SW, b->full[3] = SH;
    b->clip[2] = SW, b->clip[3] = SH;
    if (rnd_to(3)) {
        int l = rnd_to(SW), t = rnd_to(SH);
        b->clip[0] = l, b->clip[1] = t, b->clip[2] = l + 1 + rnd_to(SW - l), b->clip[3] = t + 1 + rnd_to(SH - t);
    }
    int nested = rnd_to(4) == 0 ? 1 + rnd_to(2) : 0;
    b->locks = nested;
    b->base = nested ? pixels : NULL;
}

// A sprite of up to 14 by 12 pixels of a few colours around the key colour and 0,
// with the pixels past it that a scaled-down draw reads on the margin.
static void reset_sprite(sprite_t *s, uint8_t *data) {
    memset(s, 0, sizeof *s);
    int w = 1 + rnd_to(14), h = 1 + rnd_to(12);
    int key = rnd_to(8);
    for (int i = 0; i < 1024; i++) {
        int c = rnd_to(6);
        data[i] = c == 0 ? (uint8_t)key : c == 1 ? 0 : (uint8_t)(1 + rnd_to(7));
    }
    s->data = data;
    s->key = (uint8_t)key;
    s->width = w, s->stride = w + (rnd_to(3) == 0 ? rnd_to(4) : 0), s->height = h, s->full_w = w + 2, s->full_h = h + 2;
    s->left = rnd_to(8) - 2, s->top = rnd_to(8) - 2;
}

static void sprite_sweep(void) {
    static buffer_t b;
    static sprite_t sp;
    static uint8_t pixels[SPITCH * SH + 8], data[1024], table[65536];
    static uint8_t saved_scratch[sizeof(struct game_sprite_scratch)];
    int sweeping = hooks_sweeping;
    int32_t saved_scale[4];
    float saved_ratio = *game_sprite_ratio;
    memcpy(saved_scratch, SC, sizeof saved_scratch);
    memcpy(saved_scale, game_sprite_scale, sizeof saved_scale);
    hooks_sweeping = 1;
    sweep_everything = 1;
    rng_state = 0x6d2b79f5u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 48; round++) {
        reset_buffer(&b, pixels);
        reset_sprite(&sp, data);
        for (int i = 0; i < 65536; i++) table[i] = (uint8_t)rnd();
        for (int k = 0; k < 6; k++) {
            int num, den;
            switch (rnd_to(5)) {
            case 0:
            case 1: num = den = 1 + rnd_to(4); break;  // not scaled
            case 2: num = 1 + rnd_to(4), den = 1 + rnd_to(12); break;  // down, or a little up
            default: num = 1 + rnd_to(12), den = 1 + rnd_to(4); break;  // up, sometimes past the limit
            }
            game_sprite_scale[0] = num, game_sprite_scale[1] = den;
            if (rnd_to(2) && num <= 4 * den) {  // the table already made for this scale
                sprite_fill_scaling_table(&sp);
                game_sprite_scale[2] = num, game_sprite_scale[3] = den;
                *game_sprite_ratio = (float)((long double)num / (long double)den);
            } else {
                game_sprite_scale[2] = game_sprite_scale[3] = 0;
            }
            check_sprite_fill_scaling_table(&sp);
            int x = rnd_to(SW + 40) - 24, y = rnd_to(SH + 40) - 24;
            int key = rnd_to(4) ? sp.key : rnd_to(8);
            int colour = (int)rnd();
            check_sprite_draw_3(&sp, &b, key, x, y);
            check_sprite_draw_mono_2(&sp, &b, key, x, y, colour);
            check_sprite_draw_dest(&sp, &b, key, x, y, table);
            check_sprite_draw_dest_unk1(&sp, &b, x, y, table);
            check_sprite_draw_dest_source(&sp, &b, key, x, y, table);
                        check_sprite_draw_multi_table_dest_2(&sp, &b, key, x, y, table);
            check_sprite_draw_4(&sp, &b, key, x, y, table);
            check_sprite_draw_dest_unk2_2(&sp, &b, key, x, y, table);
            // The wrappers, with a scale of their own (the globals' scale comes back).
            {
                int wn, wd;
                switch (rnd_to(4)) {
                case 0: wn = wd = 1 + rnd_to(4); break;
                case 1: wn = 1 + rnd_to(4), wd = 1 + rnd_to(12); break;
                default: wn = 1 + rnd_to(12), wd = 1 + rnd_to(4); break;
                }
                int wx = rnd_to(SW + 40) - 24, wy = rnd_to(SH + 40) - 24;
                check_sprite_draw_mono(&sp, &b, key, wx, wy, colour, wn, wd);
                check_sprite_draw(&sp, &b, key, wx, wy, wn, wd, table);
                check_sprite_draw_2(&sp, &b, key, wx, wy, wn, wd);
                check_sprite_draw_dest_2(&sp, &b, key, wx, wy, wn, wd, table);
                check_sprite_draw_dest_unk1_2(&sp, &b, wx, wy, wn, wd, table);
                check_sprite_draw_dest_source_2(&sp, &b, key, wx, wy, wn, wd, table);
                check_sprite_draw_dest_unk2(&sp, &b, key, wx, wy, wn, wd, table);
                check_sprite_draw_multi_table_dest(&sp, &b, key, wx, wy, wn, wd, table);
            }
            check_sprite_change_color(&sp, key, colour);
            // The tables for the part of a scaled sprite that a made-up rectangle leaves.
            if (num != den && num <= 4 * den) {
                game_sprite_scale[0] = num, game_sprite_scale[1] = den;
                sprite_fill_scaling_table(&sp);
                int32_t orig[4] = {rnd_to(20) - 10, rnd_to(20) - 10, 0, 0}, clip[4];
                orig[2] = orig[0] + 1 + rnd_to(50), orig[3] = orig[1] + 1 + rnd_to(40);
                clip[0] = orig[0] + rnd_to(orig[2] - orig[0]), clip[1] = orig[1] + rnd_to(orig[3] - orig[1]);
                clip[2] = rnd_to(8) ? clip[0] + rnd_to(orig[2] - clip[0] + 1) : clip[0];  // sometimes nothing
                clip[3] = clip[1] + rnd_to(orig[3] - clip[1] + 1);
                int32_t o[4];
                if (num > den) {
                    check_sprite_get_clipped_x_table_expand(&sp, orig, clip, &o[0], &o[1], &o[2], &o[3]);
                    check_sprite_get_clipped_y_table_expand(&sp, orig, clip, &o[0], &o[1], &o[2], &o[3]);
                } else {
                    check_sprite_get_clipped_x_table_shrink(&sp, orig, clip, &o[0], &o[1], &o[2], &o[3]);
                    check_sprite_get_clipped_y_table_shrink(&sp, orig, clip, &o[0], &o[1], &o[2], &o[3]);
                }
            }
        }
        // A sprite with no picture, and no buffer, and a buffer with no pixels.
        sp.data = NULL;
        check_sprite_draw_3(&sp, &b, 0, 1, 1);
        check_sprite_change_color(&sp, 1, 2);
        sp.data = data;
        check_sprite_draw_dest_unk1(&sp, NULL, 1, 1, table);
        b.bits = NULL;
        check_sprite_draw_4(&sp, &b, 0, 1, 1, table);
    }
    sweep_everything = 0;
    memcpy(SC, saved_scratch, sizeof saved_scratch);
    memcpy(game_sprite_scale, saved_scale, sizeof saved_scale);
    *game_sprite_ratio = saved_ratio;
    hooks_sweeping = sweeping;
}

static const struct hook sprite_hooks[] = {
    // push ebp; mov ebp, esp; sub esp, 0x64 or 0x5c; push ebx, esi, edi
    HOOK(0x5e3e50, sprite_draw_mono_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x64, 0x53, 0x56, 0x57),
    HOOK(0x5e4b9a, sprite_draw_3, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x64, 0x53, 0x56, 0x57),
    HOOK(0x5e5833, sprite_draw_dest, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    HOOK(0x5e66a2, sprite_draw_dest_unk1, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    HOOK(0x5e741c, sprite_draw_dest_source, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    HOOK(0x5e8ff5, sprite_draw_multi_table_dest_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    HOOK(0x5e9d44, sprite_draw_4, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    HOOK(0x5e81bc, sprite_draw_dest_unk2_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57),
    // The wrappers: mov eax, [esp+n]; mov edx, [esp+n+4]; push esi
    HOOK(0x5e3e00, sprite_draw_mono, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x54, 0x24, 0x1c, 0x56),
    HOOK(0x5e4afa, sprite_draw, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    HOOK(0x5e4b4a, sprite_draw_2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    HOOK(0x5e6652, sprite_draw_dest_2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    HOOK(0x5e73cc, sprite_draw_dest_unk1_2, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x54, 0x24, 0x14, 0x56),
    HOOK(0x5e811c, sprite_draw_dest_source_2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    HOOK(0x5e816c, sprite_draw_dest_unk2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    HOOK(0x5e8fa5, sprite_draw_multi_table_dest, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x18, 0x56),
    // mov eax, [0x696d18]; mov ecx, [0x696d1c]
    HOOK(0x5eb20f, sprite_fill_scaling_table, 0xa1, 0x18, 0x6d, 0x69, 0x00, 0x8b, 0x0d, 0x1c, 0x6d, 0x69, 0x00),
    // mov ecx, [esp+8]; push esi; mov esi, [esp+8]; push edi
    HOOK(0x5eab0f, sprite_get_clipped_x_table_expand, 0x8b, 0x4c, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57),
    HOOK(0x5eabbf, sprite_get_clipped_y_table_expand, 0x8b, 0x4c, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57),
    // mov edx, [esp+8]; push esi; mov esi, [esp+8]; push edi
    HOOK(0x5eac6f, sprite_get_clipped_x_table_shrink, 0x8b, 0x54, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57),
    HOOK(0x5eacff, sprite_get_clipped_y_table_shrink, 0x8b, 0x54, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57),
    // push ebp; mov ebp, esp; sub esp, 0xc; mov eax, [ecx+4]; push esi
    HOOK(0x5ead9f, sprite_change_color, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x8b, 0x41, 0x04, 0x56),
};
struct hook_module sprite_module = {"sprite", sprite_hooks, HOOK_COUNT(sprite_hooks), sprite_sweep, NULL, -1};
