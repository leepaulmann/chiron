// The game's Buffer drawing onto another Buffer (OpenSMACX's Buffer_draw and its relatives,
// GPL-3.0-or-later, which names them but has none of the logic).
//
// A Buffer drawn onto another is drawn as a Sprite: each of these methods points the one
// Sprite the game keeps for the purpose (0x9b3a50) at the Buffer's pixels (its whole width and
// height, no margin, the key colour asked for or the game's default for -1), makes the same
// Sprite draw, and sets the Sprite's pixel pointer back to nothing. The pointing is done with the
// Buffer locked and unlocked again, which the original counts through the Buffer's `locks` and
// `base` fields: the call ends with `base` NULL and `locks` 0 unless a caller had the Buffer locked.
// The Sprite draws are in sprite.c, and are given the arguments the method was, unchanged (the
// key too: only the Sprite's own key is the default's).
//
// A Buffer that is a DirectDraw surface is drawn another way, by the surface's own blit, which
// Chiron never makes a surface for; those, and any drawing into a surface, are left to the
// original. Two of the methods (5d8650 and 5d8740, here draw_stub and draw_stub_2) call stubs in
// the game, functions that return 0 at once, so they only point the Sprite at the Buffer.
//
// What these write is what the Sprite draw does (the destination's pixels, the scratch the draw
// leaves in the game's globals) and the fields of the two Buffers and the Sprite set up, so a
// plain protected check does for them.
#include <stdint.h>
#include <string.h>

#include "bufdraw.h"
#include "game.h"
#include "hooks.h"
#include "shadow.h"
#include "sprite.h"

#define BUFDRAW (&bufdraw_module)
extern struct hook_module bufdraw_module;

typedef struct game_buffer buffer_t;
typedef struct game_sprite sprite_t;

// Points the game's Sprite at the Buffer's pixels. `key` is the colour the Sprite is to treat as
// transparent. The Buffer is locked for the length of this and unlocked after, which leaves it
// unlocked (base NULL) unless it was locked before; with no pixels it is not locked, and the unlock
// still counts down, which is why `locks` can come out 0 from -1.
static void point_sprite(buffer_t *b, uint8_t key) {
    sprite_t *s = game_buffer_sprite;
    b->base = b->bits;
    if (b->bits) b->locks++;
    s->data = b->bits;
    s->stride = b->pitch;
    s->width = b->width;
    s->height = -b->neg_height;
    s->full_w = b->width;
    s->full_h = -b->neg_height;
    s->left = 0;
    s->top = 0;
    s->key = key;
    s->shared = 0;
    if (--b->locks <= 0) b->base = NULL, b->locks = 0;
}

// The key of a call that is given one: -1 asks for the game's.
static uint8_t key_of(int key) { return key == -1 ? *game_default_key : (uint8_t)key; }

static void let_go(void) { game_buffer_sprite->data = NULL; }

static int stub_5(sprite_t *s, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    (void)s, (void)d, (void)key, (void)x, (void)y, (void)table;
    return 0;
}
static int stub_7(sprite_t *s, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    (void)s, (void)d, (void)key, (void)x, (void)y, (void)num, (void)den, (void)table;
    return 0;
}

#define DECLARE(name, params) SHADOW_DECLARE_T(int, name, params)

DECLARE(buffer_draw, (buffer_t * b, buffer_t *d, int key, int x, int y))
DECLARE(buffer_draw_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den))
DECLARE(buffer_draw_stub, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_stub_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_mono, (buffer_t * b, buffer_t *d, int key, int x, int y, int colour))
DECLARE(buffer_draw_mono_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int colour, int num, int den))
DECLARE(buffer_draw_dest, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_dest_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_dest_unk1, (buffer_t * b, buffer_t *d, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_dest_unk1_2, (buffer_t * b, buffer_t *d, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_dest_source, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_dest_source_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_4, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_3, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_dest_unk2_2, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_dest_unk2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
DECLARE(buffer_draw_multi_table_dest_2, (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table))
DECLARE(buffer_draw_multi_table_dest, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table))
SHADOW_DECLARE_T(void, buffer_setup_buff_sprite, (buffer_t * b, int key))

// Draws this Buffer on `d`. The original returns 0x10, doing nothing, for no destination.
int THISCALL buffer_draw(buffer_t *b, buffer_t *d, int key, int x, int y) {
    if (!d) return 0x10;
    if (b->dd) return orig_buffer_draw(b, d, key, x, y);
    point_sprite(b, key_of(key));
    int r = sprite_draw_3(game_buffer_sprite, d, key, x, y);
    let_go();
    return r;
}
// The same, scaled by num / den.
int THISCALL buffer_draw_2(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den) {
    if (b->dd) return orig_buffer_draw_2(b, d, key, x, y, num, den);
    point_sprite(b, key_of(key));
    int r = sprite_draw_2(game_buffer_sprite, d, key, x, y, num, den);
    let_go();
    return r;
}
int THISCALL buffer_draw_stub(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_stub(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = stub_5(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_stub_2(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_stub_2(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = stub_7(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_mono(buffer_t *b, buffer_t *d, int key, int x, int y, int colour) {
    if (b->dd) return orig_buffer_draw_mono(b, d, key, x, y, colour);
    point_sprite(b, key_of(key));
    int r = sprite_draw_mono_2(game_buffer_sprite, d, key, x, y, colour);
    let_go();
    return r;
}
int THISCALL buffer_draw_mono_2(buffer_t *b, buffer_t *d, int key, int x, int y, int colour, int num, int den) {
    if (b->dd) return orig_buffer_draw_mono_2(b, d, key, x, y, colour, num, den);
    point_sprite(b, key_of(key));
    int r = sprite_draw_mono(game_buffer_sprite, d, key, x, y, colour, num, den);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_2(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_2(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest_2(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}
// The two that are not given a key colour use the game's default.
int THISCALL buffer_draw_dest_unk1(buffer_t *b, buffer_t *d, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_unk1(b, d, x, y, table);
    point_sprite(b, *game_default_key);
    int r = sprite_draw_dest_unk1(game_buffer_sprite, d, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_unk1_2(buffer_t *b, buffer_t *d, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_unk1_2(b, d, x, y, num, den, table);
    point_sprite(b, *game_default_key);
    int r = sprite_draw_dest_unk1_2(game_buffer_sprite, d, x, y, num, den, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_source(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_source(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest_source(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_source_2(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_source_2(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest_source_2(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_4(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_4(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_4(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_3(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_3(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_unk2_2(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_unk2_2(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest_unk2_2(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_dest_unk2(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_dest_unk2(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_dest_unk2(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_multi_table_dest_2(buffer_t *b, buffer_t *d, int key, int x, int y, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_multi_table_dest_2(b, d, key, x, y, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_multi_table_dest_2(game_buffer_sprite, d, key, x, y, table);
    let_go();
    return r;
}
int THISCALL buffer_draw_multi_table_dest(buffer_t *b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table) {
    if (b->dd) return orig_buffer_draw_multi_table_dest(b, d, key, x, y, num, den, table);
    point_sprite(b, key_of(key));
    int r = sprite_draw_multi_table_dest(game_buffer_sprite, d, key, x, y, num, den, table);
    let_go();
    return r;
}

// Points the Sprite at this Buffer and leaves it so (-1 for the game's key colour). Nothing is
// done for a surface.
void THISCALL buffer_setup_buff_sprite(buffer_t *b, int key) {
    if (b->dd) {
        orig_buffer_setup_buff_sprite(b, key);
        return;
    }
    point_sprite(b, key_of(key));
}

// ---- Shadow checks ----------------------------------------------------------------------------

// The Buffer as the Sprite will see it, for the pixels the draw can reach.
static sprite_t as_sprite(const buffer_t *b) {
    sprite_t s;
    memset(&s, 0, sizeof s);
    s.data = b->bits;
    s.width = b->width;
    s.stride = b->pitch;
    s.height = -b->neg_height;
    return s;
}

// What the call writes: this Buffer's lock fields, the Sprite the game keeps, and what the Sprite
// draw would write.
static void writes_plain(struct hook_writes *w, buffer_t *b, buffer_t *d, int x, int y) {
    hooks_writes_add(w, (void *)b, sizeof *b);
    hooks_writes_add(w, (void *)game_buffer_sprite, sizeof *game_buffer_sprite);
    sprite_t s = as_sprite(b);
    sprite_writes_draw(w, &s, d, x, y);
}
static void writes_scaled(struct hook_writes *w, buffer_t *b, buffer_t *d, int x, int y, int num, int den) {
    hooks_writes_add(w, (void *)b, sizeof *b);
    hooks_writes_add(w, (void *)game_buffer_sprite, sizeof *game_buffer_sprite);
    sprite_t s = as_sprite(b);
    sprite_writes_wrapped(w, &s, d, x, y, num, den);
}

#define NEEDS_ORIG(b, d) ((b)->dd || ((d) && (d)->dd))
#define BD_WRITER(name, params, args, writes, fmt, ...)                                   \
    SHADOW_WRITER_BODY(int, name, params, args, writes, fmt, __VA_ARGS__)                 \
    static int THISCALL shadow_##name params {                                            \
        if (hooks_nested || NEEDS_ORIG(b, d)) return orig_##name args;                    \
        if (shadow_bypass()) return name args;                                            \
        stats_##name.calls++;                                                             \
        shadow_tick(BUFDRAW);                                                             \
        return check_##name args;                                                         \
    }
// A destination missing is the one case the original answers with 0x10 and no drawing, which
// is why it has a check of its own too.
#define P5 (buffer_t * b, buffer_t *d, int key, int x, int y)
#define P5T (buffer_t * b, buffer_t *d, int key, int x, int y, const uint8_t *table)
#define P7T (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den, const uint8_t *table)
#define PLAIN_FMT "key %d at %d, %d, locks %d bits %d"
#define PLAIN_DETAIL key, x, y, b->locks, b->bits != NULL
#define SCALED_FMT "key %d at %d, %d, %d/%d locks %d"
#define SCALED_DETAIL key, x, y, num, den, b->locks

BD_WRITER(buffer_draw, P5, (b, d, key, x, y), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int num, int den), (b, d, key, x, y, num, den),
          writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT, SCALED_DETAIL)
BD_WRITER(buffer_draw_stub, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_stub_2, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT,
          SCALED_DETAIL)
BD_WRITER(buffer_draw_mono, (buffer_t * b, buffer_t *d, int key, int x, int y, int colour), (b, d, key, x, y, colour),
          writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_mono_2, (buffer_t * b, buffer_t *d, int key, int x, int y, int colour, int num, int den),
          (b, d, key, x, y, colour, num, den), writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT, SCALED_DETAIL)
BD_WRITER(buffer_draw_dest, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_dest_2, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT,
          SCALED_DETAIL)
BD_WRITER(buffer_draw_dest_unk1, (buffer_t * b, buffer_t *d, int x, int y, const uint8_t *table), (b, d, x, y, table),
          writes_plain(&w_, b, d, x, y), "at %d, %d locks %d", x, y, b->locks)
BD_WRITER(buffer_draw_dest_unk1_2, (buffer_t * b, buffer_t *d, int x, int y, int num, int den, const uint8_t *table),
          (b, d, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den), "at %d, %d, %d/%d locks %d", x, y, num, den,
          b->locks)
BD_WRITER(buffer_draw_dest_source, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_dest_source_2, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den),
          SCALED_FMT, SCALED_DETAIL)
BD_WRITER(buffer_draw_4, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_3, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT,
          SCALED_DETAIL)
BD_WRITER(buffer_draw_dest_unk2_2, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_dest_unk2, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den), SCALED_FMT,
          SCALED_DETAIL)
BD_WRITER(buffer_draw_multi_table_dest_2, P5T, (b, d, key, x, y, table), writes_plain(&w_, b, d, x, y), PLAIN_FMT, PLAIN_DETAIL)
BD_WRITER(buffer_draw_multi_table_dest, P7T, (b, d, key, x, y, num, den, table), writes_scaled(&w_, b, d, x, y, num, den),
          SCALED_FMT, SCALED_DETAIL)

// The set-up alone writes the Buffer's lock fields and the Sprite.
SHADOW_WRITER_BODY(void, buffer_setup_buff_sprite, (buffer_t * b, int key), (b, key),
                   (hooks_writes_add(&w_, (void *)b, sizeof *b),
                    hooks_writes_add(&w_, (void *)game_buffer_sprite, sizeof *game_buffer_sprite)),
                   "key %d locks %d", key, b->locks)
static void THISCALL shadow_buffer_setup_buff_sprite(buffer_t *b, int key) {
    if (hooks_nested || b->dd) {
        orig_buffer_setup_buff_sprite(b, key);
        return;
    }
    if (shadow_bypass()) {
        buffer_setup_buff_sprite(b, key);
        return;
    }
    stats_buffer_setup_buff_sprite.calls++;
    shadow_tick(BUFDRAW);
    check_buffer_setup_buff_sprite(b, key);
}

// ---- Sweep ------------------------------------------------------------------------------------
//
// Draws of made-up Buffers on a made-up Buffer: the source with pixels of a few colours around
// the key, sometimes locked already, sometimes with no pixels; the destination clipped on every
// side; every kind of key (a colour, one the source has, -1 for the game's), the scale one,
// down and up; and with no destination at all.

enum { SW = 53, SH = 37, SPITCH = 56, BW = 13, BH = 11, BPITCH = 16 };

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void make_buffer(buffer_t *b, uint8_t *pixels, int w, int h, int pitch, int fewcolors) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < pitch * h + 8; i++) pixels[i] = fewcolors ? (uint8_t)rnd_to(5) : (uint8_t)rnd();
    b->bits = pixels;
    b->width = w, b->neg_height = -h, b->pitch = pitch;
    b->full[2] = w, b->full[3] = h;
    b->clip[2] = w, b->clip[3] = h;
}

static void sweep_draws(buffer_t *src, buffer_t *dst, const uint8_t *table, int key, int x, int y, int colour, int num, int den) {
    check_buffer_draw(src, dst, key, x, y);
    check_buffer_draw_2(src, dst, key, x, y, num, den);
    check_buffer_draw_stub(src, dst, key, x, y, table);
    check_buffer_draw_stub_2(src, dst, key, x, y, num, den, table);
    check_buffer_draw_mono(src, dst, key, x, y, colour);
    check_buffer_draw_mono_2(src, dst, key, x, y, colour, num, den);
    check_buffer_draw_dest(src, dst, key, x, y, table);
    check_buffer_draw_dest_2(src, dst, key, x, y, num, den, table);
    check_buffer_draw_dest_unk1(src, dst, x, y, table);
    check_buffer_draw_dest_unk1_2(src, dst, x, y, num, den, table);
    check_buffer_draw_dest_source(src, dst, key, x, y, table);
    check_buffer_draw_dest_source_2(src, dst, key, x, y, num, den, table);
    check_buffer_draw_4(src, dst, key, x, y, table);
    check_buffer_draw_3(src, dst, key, x, y, num, den, table);
    check_buffer_draw_dest_unk2_2(src, dst, key, x, y, table);
    check_buffer_draw_dest_unk2(src, dst, key, x, y, num, den, table);
    check_buffer_draw_multi_table_dest_2(src, dst, key, x, y, table);
    check_buffer_draw_multi_table_dest(src, dst, key, x, y, num, den, table);
    check_buffer_setup_buff_sprite(src, key);
}

static void bufdraw_sweep(void) {
    static buffer_t src, dst;
    static uint8_t spix[BPITCH * BH + 8], dpix[SPITCH * SH + 8], table[65536];
    static uint8_t saved_sprite[sizeof(sprite_t)];
    int sweeping = hooks_sweeping;
    int saved_every = sprite_sweep_everything;
    // The Sprite draws leave scratch and scale behind: put back what they were.
    static uint8_t saved_scratch[sizeof(struct game_sprite_scratch)];
    int32_t saved_scale[4];
    float saved_ratio = *game_sprite_ratio;
    memcpy(saved_sprite, game_buffer_sprite, sizeof saved_sprite);
    memcpy(saved_scratch, game_sprite_scratch, sizeof saved_scratch);
    memcpy(saved_scale, game_sprite_scale, sizeof saved_scale);
    hooks_sweeping = 1;
    sprite_sweep_everything = 1;
    rng_state = 0x2545f491u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    for (int round = 0; round < 40; round++) {
        make_buffer(&dst, dpix, SW, SH, SPITCH, 0);
        if (rnd_to(3)) {  // clipped
            int l = rnd_to(SW), t = rnd_to(SH);
            dst.clip[0] = l, dst.clip[1] = t, dst.clip[2] = l + 1 + rnd_to(SW - l), dst.clip[3] = t + 1 + rnd_to(SH - t);
        }
        int w = 1 + rnd_to(BW), h = 1 + rnd_to(BH);
        make_buffer(&src, spix, w, h, BPITCH, 1);
        src.pitch = (w + 3) & ~3;
        int locked = rnd_to(4);  // locked by a caller already: 0 none, 1 or 2 more
        src.locks = locked == 3 ? 0 : locked, src.base = src.locks ? spix : NULL;
        for (int i = 0; i < 65536; i++) table[i] = (uint8_t)rnd();
        for (int k = 0; k < 4; k++) {
            int num, den;
            switch (rnd_to(4)) {
            case 0: num = den = 1 + rnd_to(3); break;
            case 1: num = 1 + rnd_to(3), den = 1 + rnd_to(8); break;  // down
            default: num = 1 + rnd_to(8), den = 1 + rnd_to(3); break;  // up
            }
            game_sprite_scale[0] = num, game_sprite_scale[1] = den;
            game_sprite_scale[2] = game_sprite_scale[3] = 0;
            int key;
            switch (rnd_to(4)) {
            case 0: key = -1; break;  // the game's key colour
            case 1: key = rnd_to(5); break;  // one the pixels have
            case 2: key = rnd(); break;  // past the low byte
            default: key = rnd_to(256); break;
            }
            *game_default_key = (uint8_t)rnd_to(5);
            int x = rnd_to(SW + 30) - 20, y = rnd_to(SH + 30) - 15;
            sweep_draws(&src, &dst, table, key, x, y, (int)rnd(), num, den);
        }
        // No pixels of its own; a destination with none; no destination at all.
        src.bits = NULL;
        sweep_draws(&src, &dst, table, 1, 3, 3, 4, 1, 1);
        make_buffer(&src, spix, 5, 5, BPITCH, 1);
        sweep_draws(&src, NULL, table, 1, 3, 3, 4, 2, 1);
        dst.bits = NULL;
        sweep_draws(&src, &dst, table, 1, 3, 3, 4, 1, 1);
    }
    sprite_sweep_everything = saved_every;
    memcpy(game_buffer_sprite, saved_sprite, sizeof saved_sprite);
    memcpy(game_sprite_scratch, saved_scratch, sizeof saved_scratch);
    memcpy(game_sprite_scale, saved_scale, sizeof saved_scale);
    *game_sprite_ratio = saved_ratio;
    hooks_sweeping = sweeping;
}

static const struct hook bufdraw_hooks[] = {
    // sub esp, 8; push ebx; mov ebx, [esp+0x10]
    HOOK(0x5d8370, buffer_draw, 0x83, 0xec, 0x08, 0x53, 0x8b, 0x5c, 0x24, 0x10),
    // sub esp, 0x18; push ebx; push ebp; mov ebp, [esp+0x24]
    HOOK(0x5d84c0, buffer_draw_2, 0x83, 0xec, 0x18, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x24),
    // push ebx; mov ebx, [esp+0xc]; push esi; mov esi, ecx
    HOOK(0x5d8650, buffer_draw_stub, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8740, buffer_draw_stub_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8840, buffer_draw_mono, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8930, buffer_draw_mono_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8a30, buffer_draw_dest, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8b20, buffer_draw_dest_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    // push esi; mov esi, ecx; push edi; xor edi, edi
    HOOK(0x5d8c20, buffer_draw_dest_unk1, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    HOOK(0x5d8d00, buffer_draw_dest_unk1_2, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    HOOK(0x5d8df0, buffer_draw_dest_source, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8ee0, buffer_draw_dest_source_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d8fe0, buffer_draw_4, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d90d0, buffer_draw_3, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d91d0, buffer_draw_dest_unk2_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d92c0, buffer_draw_dest_unk2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d93c0, buffer_draw_multi_table_dest_2, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    HOOK(0x5d94b0, buffer_draw_multi_table_dest, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x8b, 0xf1),
    // push esi; mov esi, ecx; push edi; xor edi, edi
    HOOK(0x5d8290, buffer_setup_buff_sprite, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
};
struct hook_module bufdraw_module = {"bufdraw", bufdraw_hooks, HOOK_COUNT(bufdraw_hooks), bufdraw_sweep, NULL, -1};
