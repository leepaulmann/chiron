// The game's Palette class: 256 colours of red, green, blue and a flag, a
// serial number that changes with the colours, and five colour cycles; and
// the conversions between RGB and HSV that its colour tables use.
// Reimplemented from terranx.exe 2.0's disassembly; the class is called
// Palette in OpenSMACX (GPL-3.0-or-later).
//
// What is left to the original: Palette_ctor, _init, _set, _close, the
// cycle functions and the timer callback (they allocate from the game's heap,
// make Time objects, and call GDI and DirectDraw, none of which a check can
// put back). The tables made by hue call the game's own sin and cos.
#include "palette.h"

#include <stdio.h>
#include <string.h>

#include "shadow.h"

extern struct hook_module palette_module;
#define PALETTE (&palette_module)

enum {
    ERR_NULL = 3,    // a pointer the call needs is NULL
    ERR_NO_PALETTE = 7,
    ERR_NO_TABLE = 16,
    DIB_COLOURS = 0x28,        // where a BITMAPINFO's colour table starts
    DIB_FIRST = 10, DIB_LAST = 246,  // the colours a window may change: 10 up to 245
    BEST_START = 200000,       // nearest_in's first best squared distance; above any real one (3 * 255^2)
};

// A new serial number: never 0.
void __attribute__((thiscall)) palette_reseed(struct palette *p) {
    p->serial = 0;
    do p->serial = (uint32_t)game_random(0, 0xffff);
    while (!p->serial);
}

void *__attribute__((thiscall)) palette_assign(struct palette *p, const uint8_t *src) {
    memcpy(p, src, 0x400);
    palette_reseed(p);
    return p;
}

// The colours as the 4 bytes of a Windows RGBQUAD (blue, green, red, 0).
int __attribute__((thiscall)) palette_get_rgbquad(struct palette *p, uint8_t *out, int start, int count) {
    if (!out) return ERR_NULL;
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    for (int i = 0; i < count; i++) {
        const uint8_t *c = p->rgb[start + i];
        out[i * 4] = c[2], out[i * 4 + 1] = c[1], out[i * 4 + 2] = c[0], out[i * 4 + 3] = 0;
    }
    return 0;
}

// Sets colours from RGBQUADs; their flags become 5 (the game's PC_NOCOLLAPSE|PC_RESERVED mix).
int __attribute__((thiscall)) palette_set_rgbquad(struct palette *p, const uint8_t *src, int start, int count) {
    if (!src) return ERR_NULL;
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    for (int i = 0; i < count; i++) {
        uint8_t *c = p->rgb[start + i];
        c[0] = src[i * 4 + 2], c[1] = src[i * 4 + 1], c[2] = src[i * 4], c[3] = 5;
    }
    palette_reseed(p);
    return 0;
}

// Takes colours 10 to 245 from the colour table of a BITMAPINFO, leaving their flags.
int __attribute__((thiscall)) palette_set_from_dib(struct palette *p, const uint8_t *bitmap_info) {
    if (!bitmap_info) return ERR_NULL;
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    const uint8_t *q = bitmap_info + DIB_COLOURS + DIB_FIRST * 4;
    for (int i = DIB_FIRST; i < DIB_LAST; i++, q += 4) p->rgb[i][0] = q[2], p->rgb[i][1] = q[1], p->rgb[i][2] = q[0];
    palette_reseed(p);
    return 0;
}

// Which cycle slot an id has, or the first free one, or 5.
int __attribute__((thiscall)) palette_get_pos(struct palette *p, int id) {
    int i = 0;
    for (; i < 5; i++)
        if (id == p->cycle[i].id || p->cycle[i].id == -1) break;
    return i;
}

// Marks the colours that cycle.
static void cycling(const struct palette *p, uint8_t *mask) {
    for (int s = 0; s < 5; s++) {
        if (p->cycle[s].id == -1) continue;
        for (int j = p->cycle[s].start; j < p->cycle[s].start + p->cycle[s].count && j < 256; j++) mask[j] = 1;
    }
}

// The colour from start to start + count that is nearest (squared distance
// in RGB, the first of equals) to a colour, or with skip_cycles only among
// those that do not cycle. 7 if there is no GDI palette. Nothing in range,
// or nothing near enough, leaves the answer to stale stack in the original;
// this gives 0.
int __attribute__((thiscall)) palette_nearest_in(struct palette *p, int r, int g, int b, int start, int count,
                                                 int skip_cycles) {
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    uint8_t mask[256] = {0};
    if (skip_cycles) cycling(p, mask);
    int best = BEST_START, found = 0;
    for (int i = start; i < start + count; i++) {
        if (skip_cycles && (unsigned)i < 256 && mask[i]) continue;
        int dr = p->rgb[i][0] - (r & 0xff), dg = p->rgb[i][1] - (g & 0xff), db = p->rgb[i][2] - (b & 0xff);
        int d = dr * dr + dg * dg + db * db;
        if (d < best) best = d, found = i;
    }
    return found;
}

// The same over the whole palette, or with skip_cycles over colours 10 to 245 that do not cycle.
int __attribute__((thiscall)) palette_nearest(struct palette *p, int r, int g, int b, int skip_cycles) {
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    uint8_t mask[256] = {0};
    int from = 0, to = 256;
    if (skip_cycles) cycling(p, mask), from = DIB_FIRST, to = DIB_LAST;
    int best = BEST_START, found = 0;
    for (int i = from; i < to; i++) {
        if (skip_cycles && mask[i]) continue;
        int dr = p->rgb[i][0] - (r & 0xff), dg = p->rgb[i][1] - (g & 0xff), db = p->rgb[i][2] - (b & 0xff);
        int d = dr * dr + dg * dg + db * db;
        if (d < best) best = d, found = i;
    }
    return found;
}

static void table_identity(uint8_t *table, int start, int end) {
    for (int i = 0; i < start && i < 256; i++) table[i] = (uint8_t)i;
    for (int i = end; i < 256; i++) table[i] = (uint8_t)i;
}

// A table that sends each colour from start to start + count to the colour
// this palette has nearest to a mix of it and another palette's (the
// weights count: (this * weight_this + other * weight_other) / total).
// Colours outside the range, and those that cycle, send themselves.
int __attribute__((thiscall)) palette_blend_table(struct palette *p, const uint8_t *other, uint8_t *table, int start,
                                                  int count, int weight_this, int weight_other) {
    if (!other) return ERR_NO_TABLE;
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    if (!table) return ERR_NO_TABLE;
    int end = start + count;
    table_identity(table, start, end);
    int total = weight_this + weight_other;
    for (int i = start; i < end; i++) {
        int r = (other[i * 4] * weight_other + p->rgb[i][0] * weight_this) / total;
        int g = (other[i * 4 + 1] * weight_other + p->rgb[i][1] * weight_this) / total;
        int b = (other[i * 4 + 2] * weight_other + p->rgb[i][2] * weight_this) / total;
        table[i] = (uint8_t)palette_nearest(p, (uint8_t)r, (uint8_t)g, b, 0);
    }
    return 0;
}

// The same, mixing each colour with one colour of this palette; the
// cycling colours then send themselves.
int __attribute__((thiscall)) palette_table_from_color(struct palette *p, int index, uint8_t *table, int start,
                                                       int count, int weight_this, int weight_color) {
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    if (!table) return ERR_NO_TABLE;
    int end = start + count;
    table_identity(table, start, end);
    const uint8_t *c = p->rgb[index & 0xff];
    int total = weight_this + weight_color;
    for (int i = start; i < end; i++) {
        int r = (p->rgb[i][0] * weight_this + c[0] * weight_color) / total;
        int g = (p->rgb[i][1] * weight_this + c[1] * weight_color) / total;
        int b = (p->rgb[i][2] * weight_this + c[2] * weight_color) / total;
        table[i] = (uint8_t)palette_nearest(p, (uint8_t)r, (uint8_t)g, b, 1);
    }
    for (int s = 0; s < 5; s++) {
        if (p->cycle[s].id == -1) continue;
        for (int j = p->cycle[s].start; j < p->cycle[s].start + p->cycle[s].count && j < 256; j++) table[j] = (uint8_t)j;
    }
    return 0;
}

// A table that sends each colour from start to start + count of this
// palette to the nearest colour of another (outside the range they send themselves).
int __attribute__((thiscall)) palette_table_from_palette(struct palette *p, struct palette *target, uint8_t *table,
                                                         int start, int count) {
    if (!target || !table) return ERR_NO_TABLE;
    int end = start + count;
    table_identity(table, start, end);
    for (int i = start; i < end; i++)
        table[i] = (uint8_t)palette_nearest(target, p->rgb[i][0], p->rgb[i][1], p->rgb[i][2], 1);
    return 0;
}

// Fills the colour table of a BITMAPINFO with, for each of colours 10 to
// 245, the whole entry of the colour nearest to it from start to start +
// count (the first of the table gets the first, though it is colour 10's).
int __attribute__((thiscall)) palette_table_to_dib(struct palette *p, uint8_t *bitmap_info, int start, int count) {
    if (!bitmap_info) return ERR_NO_TABLE;
    uint8_t *dst = bitmap_info + DIB_COLOURS;
    for (int i = DIB_FIRST; i < DIB_LAST; i++, dst += 4) {
        int n = palette_nearest_in(p, p->rgb[i][0], p->rgb[i][1], p->rgb[i][2], start, count, 1);
        memcpy(dst, p->rgb[n], 4);
    }
    return 0;
}

// ---- RGB and HSV -------------------------------------------------------------

// The original works on the x87 stack, in 80 bits, and stores doubles only
// where it stores: rd() rounds to what it stored. Colours are taken as
// n/256. Its minimum is odd: unless blue is the largest, blue is taken as
// the smallest too, which changes the hue and saturation of a colour whose
// green is below its blue.
static double rd(long double x) {
    volatile double v = (double)x;
    return v;
}

void rgb_to_hsv(const uint8_t *rgb, struct hsv *out) {
    if (!rgb || !out) return;
    long double r = rgb[0] * 0.00390625L, g = rgb[1] * 0.00390625L, b = rgb[2] * 0.00390625L;
    long double max = g <= r ? r : g;
    if (b > max) max = b;
    long double min = g < r ? g : r;
    if (b < max) min = b;
    out->v = rd(max);
    long double s = max == 0 ? 0.0L : (max - min) / max;
    out->s = rd(s);
    if (s == 0) {
        out->h = -1.0;
        return;
    }
    double d = rd(max - min);
    long double h;
    if (r == max) h = ((long double)rd(g) - (long double)rd(b)) / d;
    else if (g != max) h = (r - g) / d + 4.0L;
    else h = ((long double)rd(b) - r) / d + 2.0L;
    double hd = rd(h);
    long double h60 = (long double)hd * 60.0L;
    out->h = h60 < 0 ? rd(h60 + 360.0L) : rd(h60);
}

// The channel as a byte: scaled by 256 and cut down, so that 1.0 gives 0.
static uint8_t channel(double x) { return (uint8_t)(int)(x * 256.0); }

// A hue outside 0 to 360 (which rgb_to_hsv can give: see above) is in no
// sector, and the original then uses red from the second sector's formula,
// blue from the first's, and for green a double whose low half is the sector
// number and whose high half it never set; it is taken as 0 here, so that
// green comes out 0. A grey whose hue is not -1 has nothing set at all: black.
void hsv_to_rgb(const struct hsv *hsv, uint8_t *rgb) {
    if (!hsv || !rgb) return;
    struct hsv c = *hsv;
    double r, g, b;
    if (c.s == 0) {
        if (c.h != -1.0) {
            rgb[0] = rgb[1] = rgb[2] = 0;
            return;
        }
        r = g = b = c.v;
    } else {
        double h = c.h == 360.0 ? 0.0 : c.h;
        double sector = rd((long double)h * (long double)0.016666666666666666);
        int i = (int)sector;
        if ((double)i > sector) i--;  // floor
        long double f = (long double)sector - i;
        double p = rd((1.0L - c.s) * c.v);
        double q = rd((1.0L - f * c.s) * c.v);
        double t = rd((1.0L - (1.0L - f) * c.s) * c.v);
        double v = c.v;
        switch ((unsigned)i > 5 ? -1 : i) {
        case -1: r = q, g = 0, b = p; break;
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
        }
    }
    rgb[0] = channel(r), rgb[1] = channel(g), rgb[2] = channel(b);
}

// ---- Nearest by hue ------------------------------------------------------------

// The colour (of the 256 in `hsv`, this palette's in HSV; with skip_cycles
// only colours 10 to 245 that do not cycle) nearest to an HSV colour. The
// distance is that between points of a cylinder: (sin h * s, cos h * s, v), so
// that the hue wraps round. It is squared and summed the way the original's
// x87 code does, with its stores to doubles (rd) and the squares of the two
// horizontal differences taken as the difference stored by the first times
// the difference left in a register for the second. The first colour of the
// least distance wins, and an unordered (NaN) distance always wins. 7 if
// there is no GDI palette.
int __attribute__((thiscall)) palette_nearest_hsv(struct palette *p, const struct hsv *target, const struct hsv *hsv,
                                                  int skip_cycles) {
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    uint8_t mask[256] = {0};
    int from = 0, to = 256;
    if (skip_cycles) cycling(p, mask), from = DIB_FIRST, to = DIB_LAST;
    double best = BEST_START;
    int found = 0;
    long double k = *game_palette_rad;
    for (int i = from; i < to; i++) {
        if (skip_cycles && mask[i]) continue;
        const struct hsv *c = &hsv[i];
        double dv = rd((long double)target->v - c->v);
        long double sin_t = game_crt_sin(rd((long double)target->h * k));
        double a = rd(sin_t * target->s);
        long double sin_i = game_crt_sin(rd((long double)c->h * k));
        long double sin_diff = a - sin_i * c->s;
        double ds = rd(sin_diff);
        double ds2 = rd(sin_diff * ds);
        long double cos_t = game_crt_cos(rd((long double)target->h * k));
        double b = rd(cos_t * target->s);
        long double cos_i = game_crt_cos(rd((long double)c->h * k));
        long double cos_diff = b - cos_i * c->s;
        double dc = rd(cos_diff);
        long double total = cos_diff * dc + ds2;
        total += (long double)dv * dv;
        if (!(total >= (long double)best)) best = rd(total), found = i;
    }
    return found;
}

// A table that sends each colour from start to start + count to the colour
// nearest to it made lighter or darker: its value (brightness) is scaled to
// (level + 100)% (level from -100 to 100, else 3). Colours outside the range send
// themselves. 7 if there is no GDI palette, 16 if there is no table. Only
// colours that do not cycle are the answers.
int __attribute__((thiscall)) palette_create_table(struct palette *p, uint8_t *table, int start, int count, int level) {
    if (!*game_palette_handle) return ERR_NO_PALETTE;
    if (!table) return ERR_NO_TABLE;
    if (level < -100 || level > 100) return ERR_NULL;
    int end = start + count;
    for (int i = 0; i < start; i++) table[i] = (uint8_t)i;
    for (int i = end; i < 256; i++) table[i] = (uint8_t)i;
    struct hsv hsv[256];
    for (int i = 0; i < 256; i++) rgb_to_hsv(p->rgb[i], &hsv[i]);
    for (int i = start; i < end; i++) {
        struct hsv c = hsv[i];
        c.v = rd((long double)(double)(level + 100) * c.v * 0.01);
        table[i] = (uint8_t)palette_nearest_hsv(p, &c, hsv, 1);
    }
    return 0;
}

// ---- Shadow checks -----------------------------------------------------------

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call

#define SERIAL WRITES(&p->serial, 4), WRITES(game_random_seed, 4)

SHADOW_WRITER_T(PALETTE, voidp, palette_assign, (struct palette *p, const uint8_t *src), (p, src),
                (WRITES(p, 0x404), WRITES(game_random_seed, 4)), "%p", (void *)p)
SHADOW_WRITER_T(PALETTE, int, palette_get_rgbquad, (struct palette *p, uint8_t *out, int start, int count),
                (p, out, start, count), (out && count > 0 ? WRITES(out, count * 4) : 0),
                "%d, %d, %d", out != NULL, start, count)
SHADOW_WRITER_T(PALETTE, int, palette_set_rgbquad, (struct palette *p, const uint8_t *src, int start, int count),
                (p, src, start, count), (WRITES(p, 0x404), WRITES(game_random_seed, 4)), "%d, %d, %d", src != NULL, start,
                count)
SHADOW_WRITER_T(PALETTE, int, palette_set_from_dib, (struct palette *p, const uint8_t *bmi), (p, bmi),
                (WRITES(p, 0x404), WRITES(game_random_seed, 4)), "%d", bmi != NULL)
SHADOW_WRITER_T(PALETTE, void, palette_reseed, (struct palette *p), (p), (SERIAL), "%d", 0)
SHADOW_THIS_FN(PALETTE, int, palette_get_pos, (struct palette *p, int id), (p, id), "%d", id)
SHADOW_THIS_FN(PALETTE, int, palette_nearest, (struct palette *p, int r, int g, int b, int skip),
               (p, r, g, b, skip), "%d, %d, %d, %d", r, g, b, skip)
SHADOW_THIS_FN(PALETTE, int, palette_nearest_in, (struct palette *p, int r, int g, int b, int start, int count, int skip),
               (p, r, g, b, start, count, skip), "%d, %d, %d, %d, %d, %d", r, g, b, start, count, skip)
SHADOW_WRITER_T(PALETTE, int, palette_blend_table,
                (struct palette *p, const uint8_t *other, uint8_t *table, int start, int count, int wa, int wb),
                (p, other, table, start, count, wa, wb), (table ? WRITES(table, 256) : 0), "%d, %d, %d, %d, %d", table != NULL,
                start, count, wa, wb)
SHADOW_WRITER_T(PALETTE, int, palette_table_from_color,
                (struct palette *p, int index, uint8_t *table, int start, int count, int wa, int wb),
                (p, index, table, start, count, wa, wb), (table ? WRITES(table, 256) : 0), "%d, %d, %d, %d, %d, %d", index,
                table != NULL, start, count, wa, wb)
SHADOW_WRITER_T(PALETTE, int, palette_table_from_palette,
                (struct palette *p, struct palette *target, uint8_t *table, int start, int count),
                (p, target, table, start, count), (table ? WRITES(table, 256) : 0), "%d, %d, %d", table != NULL, start,
                count)
SHADOW_WRITER_T(PALETTE, int, palette_table_to_dib, (struct palette *p, uint8_t *bmi, int start, int count),
                (p, bmi, start, count), (bmi ? WRITES(bmi + DIB_COLOURS, (DIB_LAST - DIB_FIRST) * 4) : 0), "%d, %d, %d",
                bmi != NULL, start, count)
SHADOW_THIS_FN(PALETTE, int, palette_nearest_hsv,
               (struct palette *p, const struct hsv *target, const struct hsv *hsv, int skip), (p, target, hsv, skip),
               "%g, %g, %g, %d", target->h, target->s, target->v, skip)
SHADOW_WRITER_T(PALETTE, int, palette_create_table, (struct palette *p, uint8_t *table, int start, int count, int level),
                (p, table, start, count, level), (table ? WRITES(table, 256) : 0), "%d, %d, %d, %d", table != NULL, start,
                count, level)
SHADOW_WRITER(PALETTE, void, rgb_to_hsv, (const uint8_t *rgb, struct hsv *out), (rgb, out),
              (out ? WRITES(out, sizeof *out) : 0), "%d, %d, %d", rgb ? rgb[0] : -1, rgb ? rgb[1] : -1, rgb ? rgb[2] : -1)
SHADOW_WRITER(PALETTE, void, hsv_to_rgb, (const struct hsv *hsv, uint8_t *rgb), (hsv, rgb), (rgb ? WRITES(rgb, 3) : 0),
              "%g, %g, %g", hsv->h, hsv->s, hsv->v)

// ---- Sweep -------------------------------------------------------------------

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// A palette of random colours with a few of its colours cycling, or none.
static void random_palette(struct palette *p) {
    for (int i = 0; i < 256; i++) {
        uint32_t c = rnd();
        // Some runs of grey and of near-equal colours, so that nearest has ties.
        if (i % 7 == 0) c = (c & 0xff) * 0x010101u;
        if (i % 11 == 0) c = rnd_to(4) * 0x08080808u;
        memcpy(p->rgb[i], &c, 4);
    }
    p->serial = rnd() | 1;
    int active = rnd_to(4);
    for (int s = 0; s < 5; s++) {
        p->cycle[s] = (struct palette_cycle){.id = -1};
        if (s < active) {
            p->cycle[s].id = 1 + rnd_to(100);
            p->cycle[s].start = (uint8_t)(rnd_to(230));
            p->cycle[s].count = (uint8_t)(1 + rnd_to(20));
        }
    }
}

// Whether a range holds a colour that does not cycle. When none does, the
// original's nearest_in answers with what was on its stack, which cannot be compared.
static int has_free(const struct palette *p, int start, int count) {
    uint8_t mask[256] = {0};
    cycling(p, mask);
    for (int i = start; i < start + count; i++)
        if (!mask[i]) return 1;
    return 0;
}

static void palette_sweep(void) {
    static struct palette pal, other;
    static uint8_t table256[256];
    static uint8_t table[256], rgbquad[256 * 4], dib[DIB_COLOURS + 256 * 4];
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x9e3779b9u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 24; round++) {
        random_palette(&pal), random_palette(&other);
        for (int i = 0; i < (int)sizeof rgbquad; i++) rgbquad[i] = (uint8_t)rnd();
        for (int i = 0; i < (int)sizeof dib; i++) dib[i] = (uint8_t)rnd();
        int start = rnd_to(256), count = rnd_to(257 - start);
        int wa = 1 + rnd_to(9), wb = rnd_to(9);
        check_palette_assign(&pal, other.rgb[0]);
        check_palette_get_rgbquad(&pal, rgbquad, start, count);
        check_palette_get_rgbquad(&pal, rgbquad, 0, 256);
        check_palette_get_rgbquad(&pal, NULL, start, count);
        check_palette_set_rgbquad(&pal, rgbquad, start, count);
        check_palette_set_rgbquad(&pal, NULL, start, count);
        check_palette_set_from_dib(&pal, dib);
        check_palette_set_from_dib(&pal, NULL);
        check_palette_reseed(&pal);
        for (int k = 0; k < 4; k++) {
            int id = k == 0 ? -1 : k == 1 ? pal.cycle[0].id : 1 + rnd_to(100);
            check_palette_get_pos(&pal, id);
        }
        for (int k = 0; k < 24; k++) {
            int r = rnd_to(256), g = rnd_to(256), b = rnd_to(256);
            if (k % 6 == 0) r = g = b = (int)(rnd() & 0xff);  // ties
            if (k % 6 == 1) r += 256 * (1 + rnd_to(3)), b -= 256;  // only the low byte counts
            check_palette_nearest(&pal, r, g, b, k & 1);
            int s = rnd_to(256), c = rnd_to(257 - s);
            if (k % 5 == 0) s = 10, c = 236;
            if (c == 0) c = 1;
            int skip = (k >> 1) & 1;
            check_palette_nearest_in(&pal, r, g, b, s, c, skip && has_free(&pal, s, c));
        }
        check_palette_blend_table(&pal, other.rgb[0], table, start, count, wa, wb);
        check_palette_blend_table(&pal, other.rgb[0], NULL, start, count, wa, wb);
        check_palette_blend_table(&pal, NULL, table, start, count, wa, wb);
        check_palette_table_from_color(&pal, rnd_to(256), table, start, count, wa, wb);
        check_palette_table_from_color(&pal, 300, NULL, start, count, wa, wb);
        check_palette_table_from_palette(&pal, &other, table, start, count);
        check_palette_table_from_palette(&pal, NULL, table, start, count);
        if (has_free(&pal, start, count ? count : 1)) check_palette_table_to_dib(&pal, dib, start, count ? count : 1);
        check_palette_table_to_dib(&pal, NULL, start, count);
    }
    // By hue: the colours of a random palette, each way, with the colours it has and some that
    // are off its scale.
    for (int round = 0; round < 6; round++) {
        static struct hsv tab[256];
        random_palette(&pal);
        for (int i = 0; i < 256; i++) rgb_to_hsv(pal.rgb[i], &tab[i]);
        for (int k = 0; k < 40; k++) {
            struct hsv t;
            if (k % 4 == 0) {
                t = tab[rnd_to(256)];
            } else if (k % 4 == 1) {
                uint8_t c[3] = {(uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd()};
                rgb_to_hsv(c, &t);
            } else {
                t.h = k % 7 == 0 ? -1.0 : (rnd() % 36001) / 100.0;
                t.s = (rnd() % 257) / 256.0, t.v = (rnd() % 257) / 256.0;
            }
            check_palette_nearest_hsv(&pal, &t, tab, k & 1);
        }
        for (int k = 0; k < 4; k++) {
            int start = rnd_to(256), count = rnd_to(257 - start);
            if (k == 0) start = 10, count = 236;
            check_palette_create_table(&pal, table256, start, count, rnd_to(201) - 100);
        }
        check_palette_create_table(&pal, table256, 0, 256, 0);
        check_palette_create_table(&pal, table256, 5, 40, 100);
        check_palette_create_table(&pal, table256, 5, 40, -100);
        check_palette_create_table(&pal, table256, 5, 40, 101);
        check_palette_create_table(&pal, NULL, 5, 40, 0);
    }
    // RGB and HSV: random colours, greys, the extremes, and every kind of hue; and back.
    for (int k = 0; k < 2500; k++) {
        uint8_t rgb[3] = {(uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd()};
        switch (k % 6) {
        case 0: rgb[1] = rgb[2] = rgb[0]; break;
        case 1: rgb[rnd_to(3)] = (uint8_t)(rnd_to(2) ? 255 : 0); break;
        case 2: rgb[0] = rgb[1]; break;
        case 3: rgb[1] = rgb[2]; break;
        case 4: rgb[0] = rgb[2]; break;
        }
        struct hsv h;
        check_rgb_to_hsv(rgb, &h);
        rgb_to_hsv(rgb, &h);
        check_hsv_to_rgb(&h, rgb);
        struct hsv r = {(rnd() % 36001) / 100.0, (rnd() % 257) / 256.0, (rnd() % 257) / 256.0};
        if (k % 5 == 0) r.h = (double)rnd_to(7) * 60.0;
        if (k % 9 == 0) r.s = 1.0;
        if (k % 17 == 0) r.h = 360.0;
        if (k % 13 == 0 || r.s == 0) r.s = 0, r.h = -1.0;
        check_hsv_to_rgb(&r, rgb);
    }
    hooks_sweeping = sweeping;
}

#define PROLOGUE_ARG1 0x8b, 0x44, 0x24, 0x04  // mov eax, [esp+4]
static const struct hook palette_hooks[] = {
    HOOK(0x5fe2f0, palette_assign, PROLOGUE_ARG1, 0x56, 0x8b, 0xf1),                // push esi; mov esi, ecx
    HOOK(0x5fe560, palette_get_rgbquad, PROLOGUE_ARG1, 0x33, 0xd2, 0x3b, 0xc2),      // xor edx, edx; cmp eax, edx
    HOOK(0x5fe5c0, palette_set_rgbquad, PROLOGUE_ARG1, 0x57, 0x85, 0xc0, 0x8b, 0xf9),  // push edi; test eax, eax; mov edi, ecx
    HOOK(0x5fe650, palette_set_from_dib, PROLOGUE_ARG1, 0x57, 0x85, 0xc0, 0x8b, 0xf9),
    HOOK(0x5fe6d0, palette_reseed, 0x56, 0x8b, 0xf1, 0xc7, 0x86, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x5fed10, palette_get_pos, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x33, 0xc0),      // push esi; mov esi, [esp+8]; xor eax, eax
    HOOK(0x5fee80, palette_blend_table, 0x81, 0xec, 0x08, 0x08, 0x00, 0x00, 0x53),  // sub esp, 0x808; push ebx
    HOOK(0x5feff0, palette_table_from_color, 0x81, 0xec, 0x20, 0x04, 0x00, 0x00, 0xa1, 0x78, 0x81, 0x9b, 0x00),
    HOOK(0x5ff1a0, palette_table_from_palette, PROLOGUE_ARG1, 0x55),                // push ebp
    HOOK(0x5ff220, palette_table_to_dib, PROLOGUE_ARG1, 0x57, 0x85, 0xc0, 0x8b, 0xf9),
    HOOK(0x5ff280, palette_nearest_in, 0x81, 0xec, 0x14, 0x04, 0x00, 0x00, 0xa1, 0x78, 0x81, 0x9b, 0x00),
    HOOK(0x5fed40, palette_create_table, 0xb8, 0x28, 0x18, 0x00, 0x00),  // mov eax, 0x1828 (then the stack probe)
    HOOK(0x5ff630, palette_nearest_hsv, 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0x3c, 0x04, 0x00, 0x00),
    HOOK(0x5ff470, palette_nearest, 0x81, 0xec, 0x10, 0x04, 0x00, 0x00, 0xa1, 0x78, 0x81, 0x9b, 0x00),
    HOOK(0x628b70, hsv_to_rgb, 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec, 0x3c),  // and esp, -8; sub esp, 0x3c
    HOOK(0x628db0, rgb_to_hsv, 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec, 0x18),
};
struct hook_module palette_module = {"palette", palette_hooks, HOOK_COUNT(palette_hooks), palette_sweep, NULL, -1};
