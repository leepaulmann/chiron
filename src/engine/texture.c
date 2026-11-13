// The game's Texture class: the polygon fills that draw the terrain textures
// of the map, and the edge set-up they share. Reimplemented from terranx.exe
// 2.0's disassembly; the class is called Texture in OpenSMACX
// (GPL-3.0-or-later), which names the functions but has none of the logic.
//
// A fill takes a polygon (x, y pairs), the texture's own (u, v) pair for each
// corner, and draws it on a buffer one row at a time. The polygon is walked
// down the page by two edges, the left one (direction -1) and the right one
// (+1), each set up by setup_edge (or setup_edge_gouraud, which also carries
// a shading value) as a start and a step for u, v and x, with x walked like
// a line (a whole step and an error term). A row is then a span from the left
// edge to the right with a step for u and v (and shading) worked out by the
// FPU, in 16.16 fixed point. Sixteen versions of the fill exist, which differ
// in
//   how the texture is read    wrapping at 256 by 256 (the two plain "trans"
//                              fills), or from a texture of any width, one
//                              texel offset at a time from a table of two
//                              steps (whole, and whole plus a row);
//   what is done to a pixel    copy; copy but for a key colour; add a shading
//                              value; a shading value on a key; look the
//                              pixel up in a table (mask_dest) or two
//                              (coast); fill with a colour;
//   whether alternate rows are solid ("dither": the odd rows are textured,
//                              the even ones a plain colour or a silhouette).
// All of them keep their working values in globals, which the originals
// leave behind; those are written here too (named by address below), so a
// later call that reads a stale one, or a check that compares them, sees the
// same. The one thing left out is the stack pointer the loops save in
// 0x9bb4ac: a loop's counter lives in esp, and the original keeps the real
// one there to put back.
//
// Quirks of the originals that are reproduced:
//   - the two wrapping fills draw one pixel more than the other fills do (a
//     row is A.x to B.x inclusive), which on the right edge of the clip is
//     one pixel past it;
//   - the wrapping fills address the texture as (data & 0xffff0000) | v << 8 |
//     (data's low byte + u) & 0xff: the texture must be 64 KB aligned;
//   - draw_3 (the dithered fill with shading) reloads a texel before it moves
//     on, so each pixel is drawn from the previous pixel's texel;
//   - draw's shading test is a signed byte compare against the threshold;
//   - mask_dest does not interpolate or clip the shading value, and reads
//     the pointer to the shading array that the last fill that set it left
//     (its own edges are set up with that array);
//   - an early exit in mask_dest, coast and coast_2 unlocks a buffer it never
//     locked.
// The FPU is the original's: finit, then single precision and truncation,
// and finit again at the end, so what the original leaves in the control
// word is left here too.
//
// DirectDraw buffers, and buffers with no pixels, go to the original (which
// locks the surface through its methods).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shadow.h"
#include "texture.h"

extern struct hook_module texture_module;
#define TEXTURE (&texture_module)

typedef struct game_texture tex_t;
typedef struct game_buffer buf_t;

// The scratch: a word, an int, a byte or a short at the game's address.
#define S8 (game_tex_scratch)
#define T(a) (*(uint32_t *)(S8 + ((a)-0x9bb490)))
#define TI(a) (*(int32_t *)(S8 + ((a)-0x9bb490)))
#define TB(a) (*(uint8_t *)(S8 + ((a)-0x9bb490)))
#define TW(a) (*(uint16_t *)(S8 + ((a)-0x9bb490)))
// 0x6972c4, 0x6972c8: the two steps; 0x6972cc, 0x6972d0: the limits of the shading values.
#define TBL(i) (game_tex_tbl[i])
#define P32(p) ((uint32_t)(uintptr_t)(p))
#define PTR(v) ((uint8_t *)(uintptr_t)(uint32_t)(v))

// The scratch addresses by what they hold, for the reader of the disassembly.
//   9bb490 uv table     9bb498 shading (edge, then span)   9bb49c buffer pitch
//   9bb4a4 saved edx    9bb4a8 texture pitch               9bb4b0 corner count
//   9bb4b4 lowest corner (index)                           9bb4c0 unused copy of texture height
//   9bb4c4, 9bb530, 9bb4f0 right edge u, v, x of the row   9bb4f8, 9bb53c, 9bb4cc left edge u, v, x
//   9bb4c8 v step       9bb4e4 u step      9bb52c shading step (16 bits)
//   9bb4d0 parity of the row   9bb4dc, 9bb534 clip left, right  9bb4e0 clip top (then its row pointer)
//   9bb4ec buffer pixels  9bb4f4 points table  9bb4fc shading table  9bb504 texel of the row start
//   9bb508 row pointer    9bb50c texture pitch (copy)  9bb514 key colour or threshold
//   9bb528 v step as the low half of a word   9bb538 clip bottom (then its row pointer)
//   9bb544 texture data   9bb550 mask table   9bb554 coast texture data  9bb558 its texel of the row start
//   9bb564 second table (mask_dest)   9bb494 colour (a byte)   9bb548 the key colour the caller set

static inline void finit(void) { __asm__ volatile("finit" ::: "memory"); }

// The buffer's lock, the way the original counts it for a DIB.
static inline void lock_dib(buf_t *b) {
    b->base = b->bits;
    b->locks++;
}
static inline void unlock_dib(buf_t *b) {
    if (--b->locks <= 0) b->base = NULL, b->locks = 0;
}

// A buffer the fills draw on themselves: a DIB with pixels.
#define NEEDS_ORIG(b) (!(b) || (b)->dd || !(b)->bits)

SHADOW_DECLARE_T(void, texture_draw_trans, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw_trans_2, (tex_t * t, buf_t *b, const int32_t *pts, int big))
SHADOW_DECLARE_T(void, texture_draw_trans_3, (tex_t * t, buf_t *b, const int32_t *pts, int big))
SHADOW_DECLARE_T(void, texture_draw_upper_threshold_trans, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr))
SHADOW_DECLARE_T(void, texture_draw_2, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw_trans_4, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw_mask_dest, (tex_t * t, buf_t *b, const int32_t *pts, const uint8_t *mask, const uint8_t *table, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw_coast, (tex_t * t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table, int n, const int32_t *uv))
SHADOW_DECLARE_T(void, texture_draw_trans_5, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color))
SHADOW_DECLARE_T(void, texture_draw_3, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color))
SHADOW_DECLARE_T(void, texture_draw_trans_6, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color))
SHADOW_DECLARE_T(void, texture_draw_upper_threshold_trans_2, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color))
SHADOW_DECLARE_T(void, texture_draw_coast_2, (tex_t * t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table, int n, const int32_t *uv, int color))
SHADOW_DECLARE_T(void *, texture_ctor, (tex_t * t))
SHADOW_DECLARE_T(void, texture_dtor, (tex_t * t))
SHADOW_DECLARE_T(void, texture_close, (tex_t * t))
SHADOW_DECLARE_T(int, texture_extract_2, (tex_t * t, buf_t *b, int x, int y, int w, int h, void *pool))
SHADOW_DECLARE_T(int, texture_extract, (tex_t * t, buf_t *b, const int32_t *rect, void *unused))
SHADOW_DECLARE_T(void, texture_draw_4, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr, int color))

// ---- Edges ------------------------------------------------------------------------

// A fixed-point divide: a / b with a 16.16 result. (The original divides a 64-bit a << 16, and faults when the answer does not fit.)
static int32_t fixdiv(int32_t a, int32_t b) {
    if (b == 0) return 0;
    return (int32_t)(((int64_t)a * 65536) / b);
}
static inline int32_t shl16(int32_t v) { return (int32_t)((uint32_t)v << 16); }

// An edge: dir (+1 right, -1 left), rows left, the corner it ends at, then u, v,
// their steps, x and its step (a whole part, the sign of an extra step, the
// error term and what it grows by, and the rows it is spread over), and for
// the shaded edges the shading and its step.
struct edge {
    int32_t dir, rows, next, u, v, du, dv, x, dxq, xstep, err, rem, den, z, dz;
};
_Static_assert(sizeof(struct edge) == 0x3c, "edge");

// Walks from corner idx in the edge's direction (round the polygon, from the
// last corner to the first and back) to the first corner lower than this one,
// and sets up the edge for it. Edges that go up, and all the way to the lowest corner, are
// no edge: 0.
static int edge_setup(int32_t *e, int idx, int gouraud) {
    int32_t maxidx = TI(0x9bb4b4);
    if (idx == maxidx) return 0;
    const int32_t *pts = (const int32_t *)PTR(T(0x9bb4f4));
    const int32_t *uv = (const int32_t *)PTR(T(0x9bb490));
    int edi;
    int32_t dy;
    for (;;) {
        edi = idx + e[0];
        int last = TI(0x9bb4b0) - 1;
        if (edi > last)
            edi = 0;
        else if (edi < 0)
            edi = last;
        dy = pts[edi * 2 + 1] - pts[idx * 2 + 1];
        e[1] = dy;
        if (dy != 0) break;
        idx = edi;
        if (edi == maxidx) return 0;
    }
    if (dy < 0) return 0;
    e[2] = edi;
    int32_t dy16 = shl16(dy);
    int32_t bias = gouraud ? 0x8000 : 0;
    e[3] = shl16(uv[idx * 2]) + bias;
    e[4] = shl16(uv[idx * 2 + 1]) + bias;
    e[5] = fixdiv(shl16(uv[edi * 2]) - e[3], dy16);
    e[6] = fixdiv(shl16(uv[edi * 2 + 1]) - e[4], dy16);
    e[7] = pts[idx * 2];
    int32_t dx = pts[edi * 2] - pts[idx * 2];
    // The plain one divides the magnitude and negates, the shaded one divides
    // the signed value: the same answer.
    e[8] = dx / dy;
    if (dx < 0) {
        e[9] = -1;
        e[10] = 1 - dy;
        dx = -dx;
    } else {
        e[9] = 1;
        e[10] = 0;
    }
    e[11] = dx % dy;
    e[12] = dy;
    if (gouraud) {
        const int32_t *z = (const int32_t *)PTR(T(0x9bb4fc));
        e[13] = shl16(z[idx]);
        e[14] = fixdiv(shl16(z[edi]) - e[13], dy16);
    }
    return 1;
}
int texture_setup_edge(int32_t *e, int idx) { return edge_setup(e, idx, 0); }
int texture_setup_edge_gouraud(int32_t *e, int idx) { return edge_setup(e, idx, 1); }

// One row down an edge.
static inline void edge_step(struct edge *e, int gouraud) {
    e->u = (int32_t)((uint32_t)e->u + (uint32_t)e->du);
    e->v = (int32_t)((uint32_t)e->v + (uint32_t)e->dv);
    if (gouraud) e->z = (int32_t)((uint32_t)e->z + (uint32_t)e->dz);
    e->x = (int32_t)((uint32_t)e->x + (uint32_t)e->dxq);
    e->err = (int32_t)((uint32_t)e->err + (uint32_t)e->rem);
    if (e->err > 0) {
        e->x = (int32_t)((uint32_t)e->x + (uint32_t)e->xstep);
        e->err = (int32_t)((uint32_t)e->err - (uint32_t)e->den);
    }
}

// ---- The FPU ----------------------------------------------------------------------

// What the originals keep on the FPU stack between rows: the width of the
// span (in 16.16) and what u, v and the shading change by across it.
struct pend {
    int32_t w, du, dv, dz;
};

// Divides the changes by the width and scales them to the 16.16 steps, in
// the original's FPU state (what finit leaves, with single precision and
// truncation set): du and dv by 65536 and, for the shaded fills, dz by 256
// kept in 16 bits.
static inline void fpu_steps(const struct pend *p, int shaded) {
    static const float k65536 = 65536.0f, k256 = 256.0f;
    static const uint16_t cw = 0x0c7f;  // 0x037f, with the precision cleared to single and rounding set to truncate
    int32_t w = p->w, u = p->du, v = p->dv, z = p->dz;
    uint32_t *ou = &T(0x9bb4e4), *ov = &T(0x9bb4c8);
    uint16_t *oz = &TW(0x9bb52c);
    if (!shaded) {
        __asm__ volatile("fldcw %[cw]\n\t"
                         "fld1\n\t"
                         "fildl %[v]\n\t"
                         "fildl %[u]\n\t"
                         "fildl %[w]\n\t"
                         "fdivr %%st(3), %%st\n\t"
                         "fmul %%st, %%st(1)\n\t"
                         "fmulp %%st, %%st(2)\n\t"
                         "fmuls %[k]\n\t"
                         "fistpl %[ou]\n\t"
                         "fmuls %[k]\n\t"
                         "fistpl %[ov]\n\t"
                         "fstp %%st(0)\n\t"
                         : [ou] "=m"(*ou), [ov] "=m"(*ov)
                         : [cw] "m"(cw), [v] "m"(v), [u] "m"(u), [w] "m"(w), [k] "m"(k65536)
                         : "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)");
    } else {
        __asm__ volatile("fldcw %[cw]\n\t"
                         "fld1\n\t"
                         "fildl %[z]\n\t"
                         "fildl %[v]\n\t"
                         "fildl %[u]\n\t"
                         "fildl %[w]\n\t"
                         "fdivr %%st(4), %%st\n\t"
                         "fmul %%st, %%st(1)\n\t"
                         "fmul %%st, %%st(2)\n\t"
                         "fmulp %%st, %%st(3)\n\t"
                         "fmuls %[k]\n\t"
                         "fistpl %[ou]\n\t"
                         "fmuls %[k]\n\t"
                         "fistpl %[ov]\n\t"
                         "fmuls %[k2]\n\t"
                         "fistps %[oz]\n\t"
                         "fstp %%st(0)\n\t"
                         : [ou] "=m"(*ou), [ov] "=m"(*ov), [oz] "=m"(*oz)
                         : [cw] "m"(cw), [z] "m"(z), [v] "m"(v), [u] "m"(u), [w] "m"(w), [k] "m"(k65536), [k2] "m"(k256)
                         : "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)");
    }
}

// ---- The fill ---------------------------------------------------------------------

enum loop { LP_WRAP, LP_KEY, LP_SHADE_THR, LP_SHADE, LP_SHADE_DELAY, LP_SHADE_KEY, LP_MASK, LP_COAST };
enum even { EV_NONE, EV_FILL_EARLY, EV_FILL, EV_SIL_KEY, EV_SIL_MASK };
struct variant {
    uint8_t gouraud;    // edges with a shading value (and the FPU works it out)
    uint8_t zclip;      // the shading value is moved on to where the clip cuts a row, and halved into a 16-bit
    uint8_t ready;      // gives up if the texture's ready word is 0
    uint8_t loop;       // what an odd row (or any row) does
    uint8_t dither;     // rows alternate
    uint8_t even;       // what an even row does
};
static const struct variant V_TRANS = {0, 0, 1, LP_WRAP, 0, EV_NONE};
static const struct variant V_TRANS_3 = {0, 0, 0, LP_KEY, 0, EV_NONE};
static const struct variant V_UTT_2 = {0, 0, 0, LP_KEY, 1, EV_SIL_KEY};
static const struct variant V_DRAW = {1, 1, 0, LP_SHADE_THR, 0, EV_NONE};
static const struct variant V_DRAW_2 = {1, 1, 0, LP_SHADE, 0, EV_NONE};
static const struct variant V_TRANS_4 = {1, 1, 0, LP_SHADE_KEY, 0, EV_NONE};
static const struct variant V_MASK = {1, 0, 0, LP_MASK, 0, EV_NONE};
static const struct variant V_COAST = {1, 1, 0, LP_COAST, 0, EV_NONE};
static const struct variant V_TRANS_5 = {1, 1, 0, LP_SHADE_KEY, 1, EV_SIL_KEY};
static const struct variant V_DRAW_3 = {1, 1, 0, LP_SHADE_DELAY, 1, EV_FILL_EARLY};
static const struct variant V_COAST_2 = {1, 1, 0, LP_COAST, 1, EV_SIL_MASK};
static const struct variant V_DRAW_4 = {1, 1, 0, LP_SHADE_THR, 1, EV_FILL};

#define TEXB(i) (*(const uint8_t *)PTR(ebx + (uint32_t)(i)))

// A row of a texture that wraps: the pixels from A.x to B.x inclusive.
static void row_wrap(int32_t count, uint32_t dst) {
    uint32_t ebx = T(0x9bb544);
    ebx = (ebx & ~0xff00u) | (((T(0x9bb53c) >> 16) & 0xff) << 8);
    ebx = (ebx & ~0xffu) | ((ebx + ((T(0x9bb4f8) >> 16) & 0xff)) & 0xff);
    uint32_t u_frac = T(0x9bb4f8) << 16, step_u = T(0x9bb4e4) << 16;
    uint32_t v_frac = T(0x9bb53c) << 16, step_v = T(0x9bb4c8) << 16;
    uint8_t dl = (T(0x9bb4e4) >> 16) & 0xff, dh = (T(0x9bb4c8) >> 16) & 0xff;
    int32_t ecx = (int32_t)((uint32_t)count & 0xffff);
    uint8_t *edi = PTR(dst);
    do {
        uint8_t al = *PTR(ebx);
        uint32_t t = u_frac + step_u;
        unsigned c1 = t < u_frac;
        u_frac = t;
        *edi = al;
        uint32_t bl = (ebx & 0xff) + dl + c1;
        ebx = (ebx & ~0xffu) | (bl & 0xff);
        t = v_frac + step_v;
        unsigned c3 = t < v_frac;
        v_frac = t;
        uint32_t bh = ((ebx >> 8) & 0xff) + dh + c3;
        ebx = (ebx & ~0xff00u) | ((bh & 0xff) << 8);
        edi++;
    } while (--ecx >= 0);
}

// A row of a texture of any width.
static void row_linear(const struct variant *V, int32_t count, uint32_t dst) {
    int odd = V->dither ? (int)(T(0x9bb4d0) & 1) : 1;
    int loop = odd ? V->loop : -1;
    uint8_t *edi = PTR(dst);
    if (V->even == EV_FILL_EARLY && !odd) {  // draw_3: no texture reads at all
        uint8_t c = TB(0x9bb494);
        for (int32_t i = count; i != 0; i--) *edi++ = c;
        return;
    }
    // The texel the row starts at.
    uint32_t ebx = T(0x9bb544) + T(0x9bb4a8) * ((T(0x9bb53c) >> 16) & 0xffff) + ((T(0x9bb4f8) >> 16) & 0xffff);
    T(0x9bb504) = ebx;
    if (V->loop == LP_COAST) T(0x9bb558) = T(0x9bb554) + (ebx - T(0x9bb544));
    if (V->zclip) TI(0x9bb498) = TI(0x9bb498) >> 8;
    int32_t steps = (int32_t)((uint32_t)(TI(0x9bb4c8) >> 16) * T(0x9bb4a8) + (uint32_t)(TI(0x9bb4e4) >> 16));
    TBL(1) = steps;
    TBL(0) = (int32_t)((uint32_t)steps + T(0x9bb4a8));
    T(0x9bb528) = (T(0x9bb4c8) & 0xffff) << 16;
    const uint32_t vstep = T(0x9bb528);
    uint32_t edx = ((T(0x9bb53c) & 0xffff) << 16) | (T(0x9bb4e4) & 0xffff);
    const uint32_t dxlow = edx & 0xffff;
    uint32_t sp = ((uint32_t)(count - 1) << 16) | (T(0x9bb4f8) & 0xffff);  // the u fraction and, in the top half, the count
    int32_t esi = 0;
    const uint8_t ah = TB(0x9bb514);
    const uint8_t cl = TB(0x9bb494);
    const uint16_t dz = TW(0x9bb52c);
    uint16_t cx = (uint16_t)T(0x9bb498);
    const uint8_t *mask = PTR(T(0x9bb550));
    uint8_t al = TEXB(esi);
    int32_t ebp;
    unsigned cu;

#define VADV()                         \
    do {                               \
        uint32_t t_ = edx + vstep;     \
        ebp = t_ < edx;                \
        edx = t_;                      \
    } while (0)
#define STEP()                                                  \
    do {                                                        \
        uint32_t s_ = (sp & 0xffff) + dxlow;                    \
        cu = s_ >> 16;                                          \
        sp = (sp & 0xffff0000u) | (s_ & 0xffff);                \
        esi += (ebp ? TBL(0) : TBL(1)) + (int32_t)cu;           \
    } while (0)
#define MORE() (!((sp -= 0x10000) & 0x80000000u))
#define CH ((uint8_t)(cx >> 8))

    if (loop == -1) {  // an even row
        switch (V->even) {
        case EV_FILL:
            al = cl;
            do {
                VADV();
                *edi = al;
                STEP();
                edi++;
            } while (MORE());
            break;
        case EV_SIL_KEY:
            do {
                VADV();
                if (al != ah) *edi = cl;
                STEP();
                edi++;
                al = TEXB(esi);
            } while (MORE());
            break;
        case EV_SIL_MASK: {
            const uint8_t *over = PTR(T(0x9bb558));
            do {
                T(0x9bb4a4) = edx;
                uint8_t m = mask[over[esi]];
                if (m != 0) *edi = cl;
                VADV();
                STEP();
                edi++;
                al = TEXB(esi);
            } while (MORE());
            break;
        }
        default: break;
        }
        return;
    }
    switch (loop) {
    case LP_KEY:
        do {
            VADV();
            if (al != ah) *edi = al;
            STEP();
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    case LP_SHADE_THR:
        do {
            uint8_t a = (uint8_t)(al + CH);
            VADV();
            if ((int8_t)CH < (int8_t)ah) *edi = a;
            STEP();
            cx = (uint16_t)(cx + dz);
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    case LP_SHADE:
        do {
            uint8_t a = (uint8_t)(al + CH);
            VADV();
            *edi = a;
            cx = (uint16_t)(cx + dz);
            STEP();
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    case LP_SHADE_DELAY:  // the texel is read before the step: each pixel is drawn from the one before's
        do {
            uint8_t a = (uint8_t)(al + CH);
            VADV();
            *edi = a;
            cx = (uint16_t)(cx + dz);
            al = TEXB(esi);
            STEP();
            edi++;
        } while (MORE());
        break;
    case LP_SHADE_KEY:
        do {
            VADV();
            if (al != ah) *edi = (uint8_t)(al + CH);
            cx = (uint16_t)(cx + dz);
            STEP();
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    case LP_MASK: {
        const uint8_t *table = PTR(T(0x9bb564));
        do {
            T(0x9bb4a4) = edx;
            uint8_t m = mask[al];
            if (m != 0) *edi = table[((uint32_t)(uint8_t)(m - 1) << 8) + *edi];
            VADV();
            STEP();
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    }
    case LP_COAST: {
        const uint8_t *over = PTR(T(0x9bb558));
        do {
            T(0x9bb4a4) = edx;
            uint8_t m = mask[over[esi]];
            if (m != 0) *edi = m == 1 ? (uint8_t)(al + CH) : m;
            cx = (uint16_t)(cx + dz);
            VADV();
            STEP();
            edi++;
            al = TEXB(esi);
        } while (MORE());
        break;
    }
    default: break;
    }
#undef VADV
#undef STEP
#undef MORE
#undef CH
}

// One row: the part of the span that the clip leaves.
static void draw_row(const struct variant *V) {
    int32_t bx = TI(0x9bb4f0), ax = TI(0x9bb4cc);
    int32_t left = TI(0x9bb4dc), right = TI(0x9bb534);
    if (bx <= left) return;
    if (ax >= right) return;
    if ((int32_t)((uint32_t)bx - (uint32_t)ax) <= 0) return;
    int32_t eb = bx < right ? bx : right;
    int32_t ea = ax;
    if (ea < left) {
        // Starts left of the clip: u, v and the shading move on by the part that is cut.
        int32_t cut = shl16(left - ea);
        T(0x9bb4f8) += (uint32_t)(int32_t)(((int64_t)TI(0x9bb4e4) * cut) >> 16);
        T(0x9bb53c) += (uint32_t)(int32_t)(((int64_t)TI(0x9bb4c8) * cut) >> 16);
        if (V->zclip) T(0x9bb498) += (uint32_t)(int32_t)(((int64_t)TW(0x9bb52c) * cut) >> 8);
        ea = left;
        TI(0x9bb4cc) = left;
    }
    int32_t count = eb - ea;
    uint32_t dst = T(0x9bb508) + T(0x9bb4cc);
    if (V->loop == LP_WRAP)
        row_wrap(count, dst);
    else
        row_linear(V, count, dst);
}

// The fill, from the clip rectangle on. Everything else was set by the caller, in the scratch.
static void poly(const struct variant *V, buf_t *b, tex_t *t) {
    T(0x9bb534) = (uint32_t)b->clip[2];
    T(0x9bb4dc) = (uint32_t)b->clip[0];
    T(0x9bb538) = (uint32_t)b->clip[3];
    T(0x9bb4e0) = (uint32_t)b->clip[1];
    int32_t n = TI(0x9bb4b0);
    if (n <= 0) return;
    const int32_t *pts = (const int32_t *)PTR(T(0x9bb4f4));
    int32_t miny = 0x7fff, maxy = (int32_t)0xffff8003;
    int minidx = 0;
    for (int i = 0; i < n; i++) {
        int32_t y = pts[i * 2 + 1];
        if (y < miny) miny = y, minidx = i;
        if (y > maxy) maxy = y, TI(0x9bb4b4) = i;
    }
    if (miny >= maxy) return;
    uint32_t pitch = T(0x9bb49c), base = T(0x9bb4ec);
    T(0x9bb508) = base + (uint32_t)miny * pitch;
    T(0x9bb538) = base + T(0x9bb538) * pitch;
    T(0x9bb4e0) = base + T(0x9bb4e0) * pitch;
    if (V->dither) T(0x9bb4d0) = (uint32_t)miny & 1;
    struct edge A, B;
    int (*setup)(int32_t *, int) = V->gouraud ? texture_setup_edge_gouraud : texture_setup_edge;
    A.dir = -1;
    if (!setup(&A.dir, minidx)) return;
    B.dir = 1;
    if (!setup(&B.dir, minidx)) return;
    if (V->ready && !t->ready) return;

    struct pend pd = {0, 0, 0, 0};
    int32_t wcur = shl16(B.x - A.x);
#define CAPTURE()                                                \
    do {                                                         \
        wcur = shl16(B.x - A.x);                                 \
        if (wcur > 0) {                                          \
            pd.w = wcur;                                         \
            pd.du = (int32_t)((uint32_t)B.u - (uint32_t)A.u);    \
            pd.dv = (int32_t)((uint32_t)B.v - (uint32_t)A.v);    \
            pd.dz = (int32_t)((uint32_t)B.z - (uint32_t)A.z);    \
        }                                                        \
    } while (0)
    CAPTURE();
    if (TI(0x9bb508) >= TI(0x9bb538)) return;
    int done = 0;
    for (;;) {
        T(0x9bb4c4) = (uint32_t)B.u;
        T(0x9bb530) = (uint32_t)B.v;
        T(0x9bb4f0) = (uint32_t)B.x;
        T(0x9bb4f8) = (uint32_t)A.u;
        T(0x9bb53c) = (uint32_t)A.v;
        T(0x9bb4cc) = (uint32_t)A.x;
        if (V->gouraud) T(0x9bb498) = (uint32_t)A.z;
        if (--A.rows == 0) {
            if (!setup(&A.dir, A.next)) done = 1;
        } else {
            edge_step(&A, V->gouraud);
        }
        if (--B.rows == 0) {
            if (!setup(&B.dir, B.next)) done = 1;
        } else {
            edge_step(&B, V->gouraud);
        }
        if (wcur > 0) fpu_steps(&pd, V->gouraud);
        CAPTURE();
        if (TI(0x9bb508) >= TI(0x9bb538)) return;
        if (TI(0x9bb508) >= TI(0x9bb4e0)) draw_row(V);
        if (done) return;
        T(0x9bb508) += pitch;
        if (V->dither) T(0x9bb4d0)++;
    }
#undef CAPTURE
}

// ---- The fills --------------------------------------------------------------------

// What every fill starts with, after the buffer: the lock, the FPU reset, and the table of points.
static inline void begin(buf_t *b, const int32_t *pts) {
    lock_dib(b);
    T(0x9bb4ec) = P32(b->bits);
    finit();
    (void)pts;
}
static inline void end(buf_t *b) {
    unlock_dib(b);
    finit();
}

// The shading values are cut to the limits the game keeps (-256 and 256).
static void clamp_z(int32_t *z, int n) {
    for (int i = 0; i < n; i++) {
        int32_t v = z[i];
        if (v >= TBL(3)) v = TBL(3);
        z[i] = v;
        if (v <= TBL(2)) v = TBL(2);
        z[i] = v;
    }
}

void TEX_THIS texture_draw_trans(tex_t *t, buf_t *b, const int32_t *pts, int n, const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans(t, b, pts, n, uv);
        return;
    }
    begin(b, pts);
    T(0x9bb4f4) = P32(pts);
    T(0x9bb544) = P32(t->data);
    T(0x9bb50c) = (uint32_t)t->pitch;
    T(0x9bb4c0) = (uint32_t)t->height;
    T(0x9bb4b0) = (uint32_t)n;
    T(0x9bb490) = P32(uv);
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    poly(&V_TRANS, b, t);
    end(b);
}

// Both of these draw a 4-corner or an 8-corner polygon with the texture's own coordinates.
static inline void trans_2_3(const struct variant *V, tex_t *t, buf_t *b, const int32_t *pts, int big, int key) {
    begin(b, pts);
    T(0x9bb4f4) = P32(pts);
    T(0x9bb544) = P32(t->data);
    T(0x9bb50c) = (uint32_t)t->pitch;
    T(0x9bb4c0) = (uint32_t)t->height;
    if (big)
        T(0x9bb4b0) = 8, T(0x9bb490) = P32(t->uv8);
    else
        T(0x9bb4b0) = 4, T(0x9bb490) = P32(t->uv4);
    if (key) T(0x9bb514) = T(0x9bb548);
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    poly(V, b, t);
    end(b);
}
void TEX_THIS texture_draw_trans_2(tex_t *t, buf_t *b, const int32_t *pts, int big) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans_2(t, b, pts, big);
        return;
    }
    trans_2_3(&V_TRANS, t, b, pts, big, 0);
}
void TEX_THIS texture_draw_trans_3(tex_t *t, buf_t *b, const int32_t *pts, int big) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans_3(t, b, pts, big);
        return;
    }
    trans_2_3(&V_TRANS_3, t, b, pts, big, 1);
}

// The same as the plain fill but with a key colour (trans_3's with the points given).
static inline void utt(const struct variant *V, tex_t *t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color, int dither) {
    begin(b, pts);
    if (dither) TB(0x9bb494) = (uint8_t)color;
    T(0x9bb4f4) = P32(pts);
    T(0x9bb544) = P32(t->data);
    T(0x9bb50c) = (uint32_t)t->pitch;
    T(0x9bb4c0) = (uint32_t)t->height;
    T(0x9bb490) = P32(uv);
    T(0x9bb4b0) = (uint32_t)n;
    T(0x9bb514) = T(0x9bb548);
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    poly(V, b, t);
    end(b);
}
void TEX_THIS texture_draw_upper_threshold_trans(tex_t *t, buf_t *b, const int32_t *pts, int n, const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_upper_threshold_trans(t, b, pts, n, uv);
        return;
    }
    static const struct variant v = {0, 0, 0, LP_KEY, 0, EV_NONE};
    utt(&v, t, b, pts, n, uv, 0, 0);
}
void TEX_THIS texture_draw_upper_threshold_trans_2(tex_t *t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_upper_threshold_trans_2(t, b, pts, n, uv, color);
        return;
    }
    if (color == -1) return texture_draw_upper_threshold_trans(t, b, pts, n, uv);
    utt(&V_UTT_2, t, b, pts, n, uv, color, 1);
}
void TEX_THIS texture_draw_trans_6(tex_t *t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans_6(t, b, pts, n, uv, color);
        return;
    }
    if (color == -1) texture_draw_trans(t, b, pts, n, uv);
}

// The fills with a shading table: the draws with a threshold stop at once if no corner is below it.
static inline void shaded(const struct variant *V, tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv,
                          int thr, int use_thr, int color, int dither) {
    begin(b, pts);
    T(0x9bb514) = use_thr ? (uint32_t)thr : T(0x9bb548);
    if (dither) TB(0x9bb494) = (uint8_t)color;
    T(0x9bb490) = P32(uv);
    T(0x9bb4f4) = P32(pts);
    T(0x9bb4b0) = (uint32_t)n;
    T(0x9bb4fc) = P32(z);
    T(0x9bb544) = P32(t->data);
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    if (use_thr) {
        int i = 0;
        if (n > 0)
            do {
                if (z[i] < thr) break;
                i++;
            } while (i < n);
        if (i == n) goto out;
    } else if (n > 0) {
        clamp_z(z, n);
    }
    poly(V, b, t);
out:
    end(b);
}
void TEX_THIS texture_draw(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw(t, b, pts, z, n, uv, thr);
        return;
    }
    shaded(&V_DRAW, t, b, pts, z, n, uv, thr, 1, 0, 0);
}
void TEX_THIS texture_draw_2(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_2(t, b, pts, z, n, uv);
        return;
    }
    shaded(&V_DRAW_2, t, b, pts, z, n, uv, 0, 0, 0, 0);
}
void TEX_THIS texture_draw_trans_4(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans_4(t, b, pts, z, n, uv);
        return;
    }
    shaded(&V_TRANS_4, t, b, pts, z, n, uv, 0, 0, 0, 0);
}
void TEX_THIS texture_draw_trans_5(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_trans_5(t, b, pts, z, n, uv, color);
        return;
    }
    if (color == -1) return texture_draw_trans_4(t, b, pts, z, n, uv);
    shaded(&V_TRANS_5, t, b, pts, z, n, uv, 0, 0, color, 1);
}
void TEX_THIS texture_draw_3(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_3(t, b, pts, z, n, uv, color);
        return;
    }
    if (color == -1) return texture_draw_2(t, b, pts, z, n, uv);
    shaded(&V_DRAW_3, t, b, pts, z, n, uv, 0, 0, color, 1);
}
void TEX_THIS texture_draw_4(tex_t *t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_4(t, b, pts, z, n, uv, thr, color);
        return;
    }
    if (color == -1) return texture_draw(t, b, pts, z, n, uv, thr);
    shaded(&V_DRAW_4, t, b, pts, z, n, uv, thr, 1, color, 1);
}

// Gives the early exits the original makes before it has locked the buffer: it unlocks all the same.
static inline void unlock_unlocked(buf_t *b) {
    if (b) unlock_dib(b);
    finit();
}

void TEX_THIS texture_draw_mask_dest(tex_t *t, buf_t *b, const int32_t *pts, const uint8_t *mask, const uint8_t *table, int n,
                                     const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_mask_dest(t, b, pts, mask, table, n, uv);
        return;
    }
    if (!pts) return unlock_unlocked(b);
    if (!t->data || !mask) return unlock_unlocked(b);
    begin(b, pts);
    T(0x9bb4f4) = P32(pts);
    T(0x9bb550) = P32(mask);
    T(0x9bb544) = P32(t->data);
    T(0x9bb50c) = (uint32_t)t->pitch;
    T(0x9bb490) = P32(uv);
    T(0x9bb4c0) = (uint32_t)t->height;
    T(0x9bb4b0) = (uint32_t)n;
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    T(0x9bb564) = P32(table);
    poly(&V_MASK, b, t);
    end(b);
}

static inline void coast(const struct variant *V, tex_t *t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over,
                         const uint8_t *table, int n, const int32_t *uv, int color, int dither) {
    if (!pts) return unlock_unlocked(b);
    if (!t->data || !z || !table || !over) return unlock_unlocked(b);
    begin(b, pts);
    if (dither) TB(0x9bb494) = (uint8_t)color;
    T(0x9bb4f4) = P32(pts);
    T(0x9bb4fc) = P32(z);
    T(0x9bb550) = P32(table);
    T(0x9bb544) = P32(t->data);
    T(0x9bb50c) = (uint32_t)t->pitch;
    T(0x9bb4c0) = (uint32_t)t->height;
    T(0x9bb4b0) = (uint32_t)n;
    T(0x9bb490) = P32(uv);
    T(0x9bb49c) = (uint32_t)b->pitch;
    T(0x9bb4a8) = (uint32_t)t->pitch;
    T(0x9bb554) = P32(over->data);
    if (!over->data) return unlock_unlocked(b);
    poly(V, b, t);
    end(b);
}
void TEX_THIS texture_draw_coast(tex_t *t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table,
                                 int n, const int32_t *uv) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_coast(t, b, pts, z, over, table, n, uv);
        return;
    }
    coast(&V_COAST, t, b, pts, z, over, table, n, uv, 0, 0);
}
void TEX_THIS texture_draw_coast_2(tex_t *t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table,
                                   int n, const int32_t *uv, int color) {
    if (NEEDS_ORIG(b)) {
        orig_texture_draw_coast_2(t, b, pts, z, over, table, n, uv, color);
        return;
    }
    if (color == -1) return texture_draw_coast(t, b, pts, z, over, table, n, uv);
    coast(&V_COAST_2, t, b, pts, z, over, table, n, uv, color, 1);
}

// ---- Cutting a texture out of a Buffer -----------------------------------------------------
//
// Makes the texture from the rectangle (x, y, w, h) of the Buffer: a block of the game's heap
// holding the pixels row after row (what it held before is freed, unless it came from a pool),
// the size, and the corners of the picture for a four-point polygon and an eight-point one
// (the corners and the middles of the sides, in pixels). 0x10 if there is no Buffer, 3 if the
// rectangle is past the Buffer, 7 if the corner has no pixels behind it, 4 if memory ran out.
// A pool, and a Buffer drawing into a DirectDraw surface, are left to the original.
// A new Texture holds nothing (its corner coordinates are left as they were). Returns itself.
void *TEX_THIS texture_ctor(tex_t *t) {
    t->data = NULL, t->pitch = 0, t->height = 0, t->ready = 0;
    return t;
}
// The picture is freed unless it is another's (extract sets `ready` for a picture it was given),
// and then the pointer is cleared; one that is not freed is left in the pointer.
void TEX_THIS texture_dtor(tex_t *t) {
    if (t->data && !t->ready) game_free(t->data), t->data = NULL;
}
// The same, and the size and `ready` are cleared whatever happened.
void TEX_THIS texture_close(tex_t *t) {
    texture_dtor(t);
    t->pitch = 0, t->height = 0, t->ready = 0;
}

int TEX_THIS texture_extract_2(tex_t *t, buf_t *b, int x, int y, int w, int h, void *pool) {
    if (!b) return 0x10;
    if (b->dd || pool) return orig_texture_extract_2(t, b, x, y, w, h, pool);
    if (t->data && !t->ready) game_free(t->data), t->data = NULL;
    t->pitch = 0, t->height = 0, t->ready = 0;
    if (x + w > b->width || y + h > -b->neg_height) return 3;
    t->height = h, t->pitch = w;
    if (x >= b->width || y >= -b->neg_height) return 7;
    b->base = b->bits;
    if (!b->base) return 7;
    b->locks++;
    const uint8_t *src = b->base + (intptr_t)b->pitch * y + x;
    t->data = game_mem_get((size_t)h * w);
    if (!t->data) {
        t->pitch = 0, t->height = 0, t->ready = 0;
        unlock_dib(b);
        return 4;
    }
    uint8_t *dst = t->data;
    for (int r = h; r; r--, dst += w, src += b->pitch) memcpy(dst, src, (size_t)w);
    unlock_dib(b);
    int hw = w / 2 - 1, hh = h / 2 - 1;
    const int32_t uv4[8] = {0, 0, w - 1, 0, w - 1, h - 1, 0, h - 1};
    const int32_t uv8[16] = {0, 0, hw, 0, w - 1, 0, w - 1, hh, w - 1, h - 1, hw, h - 1, 0, h - 1, 0, hh};
    memcpy(t->uv4, uv4, sizeof uv4);
    memcpy(t->uv8, uv8, sizeof uv8);
    return 0;
}

// The same for a rectangle (left, top, right, bottom); 0x10 if there is none. (The third
// argument the game passes is not used.)
int TEX_THIS texture_extract(tex_t *t, buf_t *b, const int32_t *rect, void *unused) {
    (void)unused;
    if (!rect) return 0x10;
    return texture_extract_2(t, b, rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], NULL);
}

// ---- Shadow checks ----------------------------------------------------------------
//
// A check puts back what a fill may write: the buffer object (its lock
// fields), the scratch, the little table, the shading values it cuts, and
// the pixels of the rows the polygon covers (a pixel past the clip's right
// edge for the fills that wrap). For a sweep's scratch buffer everything is,
// to catch a stray write as well.

static int sweep_everything;

static void writes_scratch(struct hook_writes *w) {
    // Everything but the stack pointer the loops keep at 0x9bb4ac.
    hooks_writes_add(w, S8, 0x9bb4ac - 0x9bb490);
    hooks_writes_add(w, S8 + (0x9bb4b0 - 0x9bb490), 0x9bb570 - 0x9bb4b0);
    hooks_writes_add(w, &TBL(0), 8);
}

static void writes_pixels(struct hook_writes *w, buf_t *b, const int32_t *pts, int n) {
    if (!b->bits || b->dd) return;
    int32_t rows = -b->neg_height;
    if (rows < 0) rows = 0;
    intptr_t pitch = b->pitch;
    if (sweep_everything) {
        size_t len = (size_t)(pitch > 0 ? pitch : -pitch) * (size_t)rows + 8;
        hooks_writes_add(w, pitch > 0 ? (void *)b->bits : (void *)(b->bits + pitch * (rows - 1)), len);
        return;
    }
    if (!pts || n <= 0) return;
    int32_t miny = 0x7fff, maxy = (int32_t)0xffff8003;
    for (int i = 0; i < n; i++) {
        int32_t y = pts[i * 2 + 1];
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    if (miny < b->clip[1]) miny = b->clip[1];
    if (maxy > b->clip[3]) maxy = b->clip[3];
    if (miny >= maxy) return;
    int32_t left = b->clip[0], right = b->clip[2];
    uint8_t *r0 = b->bits + pitch * miny + left, *r1 = b->bits + pitch * (maxy - 1) + right + 1;
    uint8_t *lo = r0 < r1 ? r0 : r1, *hi = r0 < r1 ? r1 : r0;
    hooks_writes_add(w, lo, (size_t)(hi - lo) + 1);
}

static void writes_fill(struct hook_writes *w, buf_t *b, const int32_t *pts, int n, int32_t *z, int clamp) {
    writes_scratch(w);
    if (!b) return;
    hooks_writes_add(w, (void *)b, sizeof *b);
    if (clamp && z && n > 0) hooks_writes_add(w, z, (size_t)n * 4);
    writes_pixels(w, b, pts, n);
}

#define TEX_WRITER(fn, params, args, writes, fmt, ...)                      \
    SHADOW_WRITER_BODY(void, fn, params, args, writes, fmt, __VA_ARGS__)    \
    static void THISCALL shadow_##fn params {                               \
        if (hooks_nested || NEEDS_ORIG(b)) {                                \
            orig_##fn args;                                                 \
            return;                                                         \
        }                                                                   \
        if (shadow_bypass()) {                                              \
            fn args;                                                        \
            return;                                                         \
        }                                                                   \
        stats_##fn.calls++;                                                 \
        shadow_tick(TEXTURE);                                               \
        check_##fn args;                                                    \
    }

// The fills with a number of corners and a table of them.
#define DETAIL_N(pts, n) (n), (pts) ? (pts)[0] : 0, (pts) ? (pts)[1] : 0, b->clip[0], b->clip[1], b->clip[2], b->clip[3]
#define FMT_N "n %d p0 %d,%d clip %d,%d,%d,%d"

TEX_WRITER(texture_draw_trans, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv), (t, b, pts, n, uv),
           writes_fill(&w_, b, pts, n, NULL, 0), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_trans_2, (tex_t * t, buf_t *b, const int32_t *pts, int big), (t, b, pts, big),
           writes_fill(&w_, b, pts, big ? 8 : 4, NULL, 0), FMT_N, DETAIL_N(pts, big ? 8 : 4))
TEX_WRITER(texture_draw_trans_3, (tex_t * t, buf_t *b, const int32_t *pts, int big), (t, b, pts, big),
           writes_fill(&w_, b, pts, big ? 8 : 4, NULL, 0), FMT_N, DETAIL_N(pts, big ? 8 : 4))
TEX_WRITER(texture_draw_upper_threshold_trans, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv), (t, b, pts, n, uv),
           writes_fill(&w_, b, pts, n, NULL, 0), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_trans_6, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color), (t, b, pts, n, uv, color),
           writes_fill(&w_, b, pts, n, NULL, 0), FMT_N " color %d", DETAIL_N(pts, n), color)
TEX_WRITER(texture_draw_upper_threshold_trans_2, (tex_t * t, buf_t *b, const int32_t *pts, int n, const int32_t *uv, int color),
           (t, b, pts, n, uv, color), writes_fill(&w_, b, pts, n, NULL, 0), FMT_N " color %d", DETAIL_N(pts, n), color)
TEX_WRITER(texture_draw, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr),
           (t, b, pts, z, n, uv, thr), writes_fill(&w_, b, pts, n, z, 0), FMT_N " thr %d", DETAIL_N(pts, n), thr)
TEX_WRITER(texture_draw_2, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv), (t, b, pts, z, n, uv),
           writes_fill(&w_, b, pts, n, z, 1), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_trans_4, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv), (t, b, pts, z, n, uv),
           writes_fill(&w_, b, pts, n, z, 1), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_trans_5, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color),
           (t, b, pts, z, n, uv, color), writes_fill(&w_, b, pts, n, z, 1), FMT_N " color %d", DETAIL_N(pts, n), color)
TEX_WRITER(texture_draw_3, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color),
           (t, b, pts, z, n, uv, color), writes_fill(&w_, b, pts, n, z, 1), FMT_N " color %d", DETAIL_N(pts, n), color)
TEX_WRITER(texture_draw_4, (tex_t * t, buf_t *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int thr, int color),
           (t, b, pts, z, n, uv, thr, color), writes_fill(&w_, b, pts, n, z, 0), FMT_N " thr %d color %d", DETAIL_N(pts, n), thr, color)
TEX_WRITER(texture_draw_mask_dest, (tex_t * t, buf_t *b, const int32_t *pts, const uint8_t *mask, const uint8_t *table, int n, const int32_t *uv),
           (t, b, pts, mask, table, n, uv), writes_fill(&w_, b, pts, n, NULL, 0), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_coast, (tex_t * t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table, int n, const int32_t *uv),
           (t, b, pts, z, over, table, n, uv), writes_fill(&w_, b, pts, n, NULL, 0), FMT_N, DETAIL_N(pts, n))
TEX_WRITER(texture_draw_coast_2, (tex_t * t, buf_t *b, const int32_t *pts, const int32_t *z, const tex_t *over, const uint8_t *table, int n, const int32_t *uv, int color),
           (t, b, pts, z, over, table, n, uv, color), writes_fill(&w_, b, pts, n, NULL, 0), FMT_N " color %d", DETAIL_N(pts, n), color)

// The edge set-up writes the edge (13 ints, 15 for the shaded one).
SHADOW_WRITER(TEXTURE, int, texture_setup_edge, (int32_t * e, int idx), (e, idx), hooks_writes_add(&w_, e, 13 * 4), "idx %d dir %d", idx, e[0])
SHADOW_WRITER(TEXTURE, int, texture_setup_edge_gouraud, (int32_t * e, int idx), (e, idx), hooks_writes_add(&w_, e, 15 * 4), "idx %d dir %d", idx, e[0])

// ---- Sweep ------------------------------------------------------------------------
//
// Fills made-up polygons on a made-up buffer, every way: every fill, with
// the polygon cut by the clip rectangle on every side, with textures that
// wrap and with others of any width, key colours, shading values in and out
// of range, and the early exits of the fills that check their arguments.

enum { SW = 61, SH = 40, SPITCH = 64 };

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void reset_buffer(buf_t *b, uint8_t *pixels) {
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

// Textures of 64 KB, aligned, as the wrapping fills need, with a little past the end.
static uint8_t tex_a[0x11000] __attribute__((aligned(0x10000)));
static uint8_t tex_b[0x11000] __attribute__((aligned(0x10000)));

static void sweep_fill(buf_t *b, int round) {
    static tex_t tex, over;
    static int32_t pts[16], uv[16], z[8], pts4[8], pts8[16];
    static uint8_t mask[256], table[1280];
    for (int i = 0; i < 0x11000; i++) tex_a[i] = (uint8_t)(rnd_to(4) ? rnd_to(8) : rnd());
    for (int i = 0; i < 0x11000; i++) tex_b[i] = (uint8_t)rnd_to(4);
    for (int i = 0; i < 256; i++) mask[i] = (uint8_t)(rnd_to(3) == 0 ? 0 : rnd_to(4));
    for (int i = 0; i < 1280; i++) table[i] = (uint8_t)rnd();
    TBL(2) = -256, TBL(3) = 256;
    static const int widths[] = {256, 256, 64, 100, 128, 200};
    for (int k = 0; k < 6; k++) {
        int w = widths[rnd_to(6)];
        int h = 0x10000 / w - 2;
        if (h > 250) h = 250;
        memset(&tex, 0, sizeof tex);
        memset(&over, 0, sizeof over);
        tex.data = tex_a, tex.pitch = w, tex.height = h, tex.ready = rnd_to(6) != 0;
        over.data = tex_b, over.pitch = w, over.height = h;
        // Corners: random, a few of them in line; the texture coordinates stay inside the picture.
        int n = 3 + rnd_to(6);
        int style = rnd_to(4);
        for (int i = 0; i < n; i++) {
            if (style == 0) {  // a diamond-ish quad, as the map's tiles are
                static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
                int cx = rnd_to(SW + 20) - 10, cy = rnd_to(SH + 20) - 10, r = 2 + rnd_to(18);
                n = 4;
                pts[i * 2] = cx + dx[i & 3] * r * 2, pts[i * 2 + 1] = cy + dy[i & 3] * r;
            } else {
                pts[i * 2] = rnd_to(SW + 60) - 30, pts[i * 2 + 1] = rnd_to(SH + 40) - 20;
            }
            uv[i * 2] = 4 + rnd_to(w - 8), uv[i * 2 + 1] = 4 + rnd_to(h - 8);
        }
        for (int i = 0; i < 4; i++) tex.uv4[i * 2] = 4 + rnd_to(w - 8), tex.uv4[i * 2 + 1] = 4 + rnd_to(h - 8);
        for (int i = 0; i < 8; i++) tex.uv8[i * 2] = 4 + rnd_to(w - 8), tex.uv8[i * 2 + 1] = 4 + rnd_to(h - 8);
        for (int i = 0; i < 4; i++) pts4[i * 2] = pts[(i % n) * 2], pts4[i * 2 + 1] = pts[(i % n) * 2 + 1];
        for (int i = 0; i < 8; i++) pts8[i * 2] = pts[(i % n) * 2], pts8[i * 2 + 1] = pts[(i % n) * 2 + 1];
        if (style == 0 && n == 4)
            for (int i = 0; i < 4; i++) pts8[i * 2] = pts4[i * 2], pts8[i * 2 + 1] = pts4[i * 2 + 1];
        // Each fill gets a polygon of its own corner count, wrapping ones the 256-wide texture.
        T(0x9bb548) = (uint32_t)rnd() & 0xff;
        int thr = rnd_to(8) == 0 ? (int)(rnd() & 0xff) : (int)(rnd_to(600) - 300);
        int color = rnd_to(8) == 0 ? -1 : (int)(rnd() & 0xff);
        int big = rnd_to(2);
        int32_t zs[8];
        for (int i = 0; i < 8; i++) zs[i] = rnd_to(5) == 0 ? (int32_t)rnd_to(1200) - 600 : (int32_t)rnd_to(300) - 150;
        // Wrapping fills: a 256-wide texture only.
        tex_t wt = tex;
        wt.pitch = 256, wt.height = 254;
#define Z() (memcpy(z, zs, sizeof z), z)
        check_texture_draw_trans(&wt, b, pts, n, uv);
        check_texture_draw_trans_2(&wt, b, big ? pts8 : pts4, big);
        check_texture_draw_trans_3(&tex, b, big ? pts8 : pts4, big);
        check_texture_draw_upper_threshold_trans(&tex, b, pts, n, uv);
        check_texture_draw_trans_6(&tex, b, pts, n, uv, color);
        check_texture_draw_upper_threshold_trans_2(&tex, b, pts, n, uv, color);
        check_texture_draw(&tex, b, pts, Z(), n, uv, thr);
        check_texture_draw_2(&tex, b, pts, Z(), n, uv);
        check_texture_draw_trans_4(&tex, b, pts, Z(), n, uv);
        check_texture_draw_trans_5(&tex, b, pts, Z(), n, uv, color);
        check_texture_draw_3(&tex, b, pts, Z(), n, uv, color);
        check_texture_draw_4(&tex, b, pts, Z(), n, uv, thr, color);
        // These read the shading array through a pointer an earlier fill left; give it a valid one.
        T(0x9bb4fc) = P32(zs);
        check_texture_draw_mask_dest(&tex, b, pts, mask, table, n, uv);
        check_texture_draw_coast(&tex, b, pts, zs, &over, table, n, uv);
        check_texture_draw_coast_2(&tex, b, pts, zs, &over, table, n, uv, color);
        // The early exits.
        if (round % 4 == 0) {
            check_texture_draw_mask_dest(&tex, b, NULL, mask, table, n, uv);
            check_texture_draw_mask_dest(&tex, b, pts, NULL, table, n, uv);
            check_texture_draw_coast(&tex, b, pts, NULL, &over, table, n, uv);
            check_texture_draw_coast_2(&tex, b, pts, zs, NULL, table, n, uv, color);
            tex_t blank = tex;
            blank.data = NULL;
            check_texture_draw_coast(&blank, b, pts, zs, &over, table, n, uv);
            tex_t blank2 = over;
            blank2.data = NULL;
            check_texture_draw_coast(&tex, b, pts, zs, &blank2, table, n, uv);
        }
        // Edges: from a made-up table, every corner both ways.
        T(0x9bb4f4) = P32(pts), T(0x9bb490) = P32(uv), T(0x9bb4fc) = P32(zs), TI(0x9bb4b0) = n;
        TI(0x9bb4b4) = rnd_to(n);
        for (int idx = 0; idx < n; idx++)
            for (int dir = -1; dir <= 1; dir += 2) {
                int32_t e[15];
                memset(e, 0x55, sizeof e);
                e[0] = dir;
                check_texture_setup_edge(e, idx);
                memset(e, 0x55, sizeof e);
                e[0] = dir;
                check_texture_setup_edge_gouraud(e, idx);
            }
    }
}

// ---- Twin check of extract -------------------------------------------------------------------
//
// Extract allocates from the game's heap, which a check can't rewind, so the game's calls run the
// original in shadow mode and the sweep makes two Buffers and two Textures alike, runs the
// original on one and ours on the other, and compares every field of the texture (its pixels by
// content), the Buffer's lock fields and pixels, and the result. The blocks are freed after.

#define PASS_TEX(ret, name, params, args)                                      \
    static ret TEX_THIS shadow_##name params {                                \
        if (!hooks_nested && !shadow_bypass()) {                               \
            stats_##name.calls++;                                              \
            shadow_tick(TEXTURE);                                              \
        }                                                                      \
        return orig_##name args;                                               \
    }
PASS_TEX(void *, texture_ctor, (tex_t * t), (t))
PASS_TEX(void, texture_dtor, (tex_t * t), (t))
PASS_TEX(void, texture_close, (tex_t * t), (t))
PASS_TEX(int, texture_extract_2, (tex_t * t, buf_t *b, int x, int y, int w, int h, void *pool), (t, b, x, y, w, h, pool))
PASS_TEX(int, texture_extract, (tex_t * t, buf_t *b, const int32_t *rect, void *unused), (t, b, rect, unused))

// A texture as it was before: nothing, pixels of its own from the heap, pixels of a pool's (not
// to be freed), or a stale size.
static uint8_t pool_pixels[64];
static void build_tex(tex_t *t, int variant, int w, int h) {
    memset(t, 0, sizeof *t);
    if (variant == 0) return;
    t->pitch = w, t->height = h;
    for (int i = 0; i < 16; i++) t->uv4[i % 8] = i, t->uv8[i] = -i;
    if (variant == 1) t->data = game_mem_get((size_t)w * h), memset(t->data, 7, (size_t)w * h);
    if (variant == 2) t->data = pool_pixels, t->ready = 1;
}
static void free_tex(tex_t *t) {
    if (t->data && !t->ready && t->data != pool_pixels) game_free(t->data);  // (a failed cut leaves the pool pointer)
    memset(t, 0, sizeof *t);
}
static const char *tex_diff(const tex_t *a, const tex_t *b) {
    static char msg[100];
    if (!a->data != !b->data) return "pixels set";
    if (a->ready != b->ready) return "ready";
    if (a->pitch != b->pitch || a->height != b->height) return "size";
    const int32_t *x = a->uv4, *y = b->uv4;  // uv4 and uv8 follow each other
    for (int i = 0; i < 24; i++)
        if (x[i] != y[i]) {
            snprintf(msg, sizeof msg, "field 0x%x = %d, original %d", 0xc + 4 * i, (int)y[i], (int)x[i]);
            return msg;
        }
    if (a->data && !a->ready && memcmp(a->data, b->data, (size_t)a->pitch * a->height)) return "pixel contents";
    return NULL;
}

static void buf_start(buf_t *b, uint8_t *pixels, int w, int h, int pitch, int locks) {
    memset(b, 0, sizeof *b);
    b->bits = pixels, b->width = w, b->neg_height = -h, b->pitch = pitch;
    b->locks = locks, b->base = locks ? pixels : NULL;
}

static void extract_sweep(void) {
    enum { PAD = 64, PIX = 28 * 20 + 2 * PAD };
    static buf_t ba, bb;
    static uint8_t pa[PIX], pb[PIX];
    static tex_t ta, tb;
    rng_state = 0x3c6ef372u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int i = 0; i < 64; i++) pool_pixels[i] = (uint8_t)rnd();
    for (int round = 0; round < 30; round++) {
        int W = 4 + rnd_to(20), H = 4 + rnd_to(14), pitch = (W + 3) & ~3;
        for (int i = 0; i < PIX; i++) pa[i] = (uint8_t)rnd();
        memcpy(pb, pa, sizeof pb);
        for (int k = 0; k < 6; k++) {
            int x, y, w, h;
            switch (rnd_to(7)) {
            case 0: x = 0, y = 0, w = W, h = H; break;
            case 1: w = 1 + rnd_to(W), x = W - w, h = 1 + rnd_to(H), y = H - h; break;
            case 2: x = 0, y = 0, w = 1 + rnd_to(W), h = 1 + rnd_to(H); break;
            case 3: x = rnd_to(W + 4) - 2, y = rnd_to(H + 4) - 2, w = 1 + rnd_to(W + 2), h = 1 + rnd_to(H + 2); break;
            case 4: x = rnd_to(W), y = rnd_to(H), w = 1, h = 1; break;
            case 5: x = W + rnd_to(3), y = rnd_to(H), w = 1, h = 1; break;  // the corner is past the Buffer
            default: x = rnd_to(W), y = rnd_to(H), w = 1 + rnd_to(W - x), h = 1 + rnd_to(H - y); break;
            }
            int variant = rnd_to(4), locks = rnd_to(4) == 0;
            int sw = 1 + rnd_to(6), sh = 1 + rnd_to(6);
            buf_start(&ba, pa + PAD, W, H, pitch, locks), buf_start(&bb, pb + PAD, W, H, pitch, locks);
            build_tex(&ta, variant, sw, sh), build_tex(&tb, variant, sw, sh);
            int want = orig_texture_extract_2(&ta, &ba, x, y, w, h, NULL);
            int got = texture_extract_2(&tb, &bb, x, y, w, h, NULL);
            const char *d = got != want ? "result" : tex_diff(&ta, &tb);
            if (!d && (ba.locks != bb.locks || !ba.base != !bb.base)) d = "buffer lock";
            if (!d && memcmp(pa, pb, sizeof pa)) d = "buffer pixels";
            if (d) hooks_mismatch(&stats_texture_extract_2, " (%d, %d, %d, %d on %dx%d, start %d): %s", x, y, w, h, W, H, variant, d);
            free_tex(&ta), free_tex(&tb);
            // The wrapper takes a rectangle.
            int32_t rect[4] = {x, y, x + w, y + h};
            buf_start(&ba, pa + PAD, W, H, pitch, locks), buf_start(&bb, pb + PAD, W, H, pitch, locks);
            build_tex(&ta, variant, sw, sh), build_tex(&tb, variant, sw, sh);
            want = orig_texture_extract(&ta, &ba, rect, NULL);
            got = texture_extract(&tb, &bb, rect, NULL);
            if (got != want || tex_diff(&ta, &tb) || ba.locks != bb.locks)
                hooks_mismatch(&stats_texture_extract, " (%d, %d, %d, %d on %dx%d)", x, y, w, h, W, H);
            free_tex(&ta), free_tex(&tb);
        }
    }
    // The lifecycle: a Texture of every kind of start, made new, closed or deleted (with its
    // corner coordinates left as they were: both start from the same bytes).
    for (int round = 0; round < 24; round++) {
        int variant = rnd_to(4), w = 1 + rnd_to(6), h = 1 + rnd_to(6);
        build_tex(&ta, variant, w, h), build_tex(&tb, variant, w, h);
        int kind = rnd_to(3);
        void *wa = NULL, *gb = NULL;
        struct hook_stats *st = kind == 0 ? &stats_texture_ctor : kind == 1 ? &stats_texture_dtor : &stats_texture_close;
        if (kind == 0) {
            wa = orig_texture_ctor(&ta);
            gb = texture_ctor(&tb);
            if ((wa == &ta) != (gb == &tb)) hooks_mismatch(st, " (result)");
        } else if (kind == 1) {
            orig_texture_dtor(&ta), texture_dtor(&tb);
        } else {
            orig_texture_close(&ta), texture_close(&tb);
        }
        if (tex_diff(&ta, &tb)) hooks_mismatch(st, " (start %d): %s", variant, tex_diff(&ta, &tb));
        free_tex(&ta), free_tex(&tb);
    }
    // No rectangle, no Buffer, and a Buffer with no pixels.
    buf_start(&ba, NULL, 8, 8, 8, 0), buf_start(&bb, NULL, 8, 8, 8, 0);
    build_tex(&ta, 1, 2, 2), build_tex(&tb, 1, 2, 2);
    int want = orig_texture_extract(&ta, &ba, NULL, NULL), got = texture_extract(&tb, &bb, NULL, NULL);
    if (got != want) hooks_mismatch(&stats_texture_extract, " (no rectangle)");
    want = orig_texture_extract_2(&ta, NULL, 0, 0, 2, 2, NULL), got = texture_extract_2(&tb, NULL, 0, 0, 2, 2, NULL);
    if (got != want) hooks_mismatch(&stats_texture_extract_2, " (no buffer)");
    want = orig_texture_extract_2(&ta, &ba, 0, 0, 2, 2, NULL), got = texture_extract_2(&tb, &bb, 0, 0, 2, 2, NULL);
    if (got != want || tex_diff(&ta, &tb)) hooks_mismatch(&stats_texture_extract_2, " (no pixels)");
    free_tex(&ta), free_tex(&tb);
}

static void texture_sweep(void) {
    static buf_t b;
    static uint8_t pixels[SPITCH * SH + 8];
    static uint8_t saved_scratch[0x9bb570 - 0x9bb490];
    int sweeping = hooks_sweeping;
    int32_t saved_tbl[4];
    memcpy(saved_scratch, S8, sizeof saved_scratch);
    memcpy(saved_tbl, game_tex_tbl, sizeof saved_tbl);
    hooks_sweeping = 1;
    sweep_everything = 1;
    rng_state = 0x2545f491u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 24; round++) {
        reset_buffer(&b, pixels);
        sweep_fill(&b, round);
    }
    // A buffer with no pixels or a DirectDraw one goes to the original: nothing to check.
    sweep_everything = 0;
    memcpy(S8, saved_scratch, sizeof saved_scratch);
    memcpy(game_tex_tbl, saved_tbl, sizeof saved_tbl);
    extract_sweep();
    hooks_sweeping = sweeping;
}

static const struct hook texture_hooks[] = {
    // mov eax, ecx; xor ecx, ecx; mov [eax], ecx
    HOOK(0x619650, texture_ctor, 0x8b, 0xc1, 0x33, 0xc9, 0x89, 0x08),
    // push esi; mov esi, ecx; mov eax, [esi]; test eax, eax
    HOOK(0x619660, texture_dtor, 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0x85, 0xc0),
    HOOK(0x619690, texture_close, 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0x85, 0xc0),
    // push ebp; mov ebp, esp; sub esp, 0x84; push ebx; push esi; mov esi, [ebp+8]
    HOOK(0x6198e0, texture_draw_trans, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x84, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8b, 0x75, 0x08),
    // push ebp; mov ebp, esp; sub esp, 0x84; push ebx; push esi; push edi
    HOOK(0x619d80, texture_draw_trans_2, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x84, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57),
    // sub esp, 0x80
    HOOK(0x61a240, texture_draw_trans_3, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57),
    HOOK(0x61a760, texture_draw_upper_threshold_trans, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57),
    // sub esp, 0x78; push ebx; mov ebx, [ebp+8]
    HOOK(0x61ac60, texture_draw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x53, 0x8b, 0x5d, 0x08),
    // sub esp, 0x7c; push ebx; push esi; push edi
    HOOK(0x61b1f0, texture_draw_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c, 0x53, 0x56, 0x57),
    HOOK(0x61c090, texture_draw_trans_4, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c, 0x53, 0x56, 0x57),
    // sub esp, 0x7c; push ebx; mov ebx, [ebp+0xc]
    HOOK(0x61e540, texture_draw_mask_dest, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c, 0x53, 0x8b, 0x5d, 0x0c),
    // sub esp, 0x78; mov eax, [ebp+0xc]
    HOOK(0x61ead0, texture_draw_coast, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x8b, 0x45, 0x0c),
    // sub esp, 0x78; push ebx; mov ebx, [ebp+0x1c]
    HOOK(0x622db0, texture_draw_trans_5, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x53, 0x8b, 0x5d, 0x1c),
    HOOK(0x6233c0, texture_draw_3, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x53, 0x8b, 0x5d, 0x1c),
    // cmp dword [esp+0x14], -1 (the jne after it is not copied)
    HOOK(0x6239b0, texture_draw_trans_6, 0x83, 0x7c, 0x24, 0x14, 0xff),
    // sub esp, 0x7c; push ebx; mov ebx, [ebp+0x18]
    HOOK(0x6239e0, texture_draw_upper_threshold_trans_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c, 0x53, 0x8b, 0x5d, 0x18),
    // sub esp, 0x78; mov eax, [ebp+0x24]
    HOOK(0x623f80, texture_draw_coast_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x8b, 0x45, 0x24),
    // sub esp, 0x78; mov eax, [ebp+0x20]
    HOOK(0x624670, texture_draw_4, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78, 0x8b, 0x45, 0x20),
    // mov eax, [0x9bb4b4]; push ebx; mov ebx, [esp+0xc]; push ebp; push esi
    // mov eax, [esp+8]; test eax, eax
    HOOK(0x6196d0, texture_extract, 0x8b, 0x44, 0x24, 0x08, 0x85, 0xc0),
    // push ebx, ebp, esi, edi; mov edi, [esp+0x14]
    HOOK(0x619710, texture_extract_2, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x14),
    HOOK(0x624d80, texture_setup_edge, 0xa1, 0xb4, 0xb4, 0x9b, 0x00, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x55, 0x56),
    HOOK(0x61fc90, texture_setup_edge_gouraud, 0xa1, 0xb4, 0xb4, 0x9b, 0x00, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x55, 0x56),
};
struct hook_module texture_module = {"texture", texture_hooks, HOOK_COUNT(texture_hooks), texture_sweep, NULL, -1};
