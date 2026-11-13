// Unit test for a Buffer's set-up, clipping and closing, with stand-ins for the Windows calls
// that record what they were asked: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/bufobj.h"
#include "../src/engine/game.h"

static int failures;
static void check_(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s = %d, expected %d\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (expr), (want))
#define AT(b, o) (*(int32_t *)((uint8_t *)(b) + (o)))

// The Windows objects the stand-ins hand out: small numbers, so a test can see which.
static int next_handle = 100, dcs, dibs, regions, deleted, selected_rgn = -1, bk_mode, text_align;
static uint8_t dib_pixels[64 * 64];
static int32_t last_rgn[4];

#define STD __attribute__((stdcall))
static void *STD create_dc(void *dc) { return dcs++, (void *)(intptr_t)next_handle++; }
static void *STD create_dib(void *dc, void *info, int usage, void **bits, void *s, int o) {
    *bits = dib_pixels;
    dibs++;
    return (void *)(intptr_t)next_handle++;
}
static int STD delete_dc(void *dc) { return dcs--, 1; }
static int STD delete_object(void *o) { return deleted++, 1; }
static void *STD create_rgn(const int32_t *r) {
    memcpy(last_rgn, r, sizeof last_rgn);
    regions++;
    return (void *)(intptr_t)next_handle++;
}
static int STD select_clip_rgn(void *dc, void *rgn) { return selected_rgn = (int)(intptr_t)rgn, 1; }
static void *STD select_object(void *dc, void *o) { return (void *)7; }
static int STD set_bk_mode(void *dc, int m) { return bk_mode = m, 0; }
static int STD set_text_align(void *dc, int a) { return text_align = a, 0; }
static int STD intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) {
        memset(out, 0, 16);
        return 0;
    }
    memcpy(out, r, 16);
    return 1;
}
static int STD equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static int STD set_rect(int32_t *r, int l, int t, int rt, int b) { return r[0] = l, r[1] = t, r[2] = rt, r[3] = b, 1; }

static int spot_inits;
static void __attribute__((thiscall)) spot_ctor(void *s) { memset(s, 0, 12); }
static void __attribute__((thiscall)) spot_dtor(void *s) { memset(s, 0, 12); }
static void __attribute__((thiscall)) spot_init(void *s, int n) { spot_inits++, ((int32_t *)s)[2] = n; }
static int frees;
static void game_free_stub(void *p) { frees++; }
static void *__attribute__((thiscall)) vt_dtor(void *b) { return NULL; }
static void *__attribute__((thiscall)) vt_owner(void *b) { return NULL; }
static void *vtable[4] = {(void *)vt_dtor, (void *)vt_owner};

int main(void) {
    static void *ddraw, *palette_now, *font_default, *font_slack_dummy;
    static int32_t default_520 = 0x1234;
    game_create_compatible_dc = create_dc, game_create_dib_section = create_dib, game_delete_dc = delete_dc;
    game_delete_object = delete_object, game_create_rect_rgn_indirect = create_rgn, game_select_clip_rgn = select_clip_rgn;
    game_select_object = select_object, game_set_bk_mode = (void *)set_bk_mode, game_set_text_align = set_text_align;
    game_intersect_rect = intersect, game_equal_rect = equal, game_set_rect = set_rect;
    game_ddraw = &ddraw, game_palette_now = &palette_now, game_font_default = (void **)&font_default;
    game_buffer_vtable = vtable, game_buffer_520_default = &default_520;
    game_spot_ctor = spot_ctor, game_spot_dtor = spot_dtor, game_spot_init = spot_init, game_free = game_free_stub;
    (void)font_slack_dummy;

    static struct game_buffer b;
    memset(&b, 0x5a, sizeof b);
    buffer_ctor(&b);
    // A new Buffer: nothing made yet, an 8-bit BITMAPINFOHEADER, and without a palette a ramp of blue.
    EXPECT(b.vtable == vtable, 1), EXPECT(AT(&b, 0x7c), 0x28), EXPECT(*(uint16_t *)((uint8_t *)&b + 0x8a), 8);
    EXPECT(AT(&b, 0x9c), 0x100), EXPECT(AT(&b, 0x64), 0), EXPECT(b.bits == NULL, 1), EXPECT(AT(&b, 0x520), 0x1234);
    EXPECT(((uint8_t *)&b)[0xa4 + 4 * 200], 200), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 200 + 2], 0);
    EXPECT(AT(&b, 0x54c), -1), EXPECT(AT(&b, 0x55c), 2), EXPECT(AT(&b, 0x20), 0x5a5a5a5a);  // the clip isn't set

    // Set up at 30 x 20: a memory DC, a DIB, clipped to the whole of it with no region.
    EXPECT(buffer_init(&b, 30, 20, 2, NULL), 0);
    EXPECT(dcs, 1), EXPECT(dibs, 1), EXPECT(b.bits == dib_pixels, 1), EXPECT(b.width, 30), EXPECT(b.neg_height, -20);
    EXPECT(b.pitch, 32), EXPECT(AT(&b, 0x90), 600), EXPECT(b.clip[2], 30), EXPECT(b.clip[3], 20);
    EXPECT(regions, 0), EXPECT(selected_rgn, 0), EXPECT(bk_mode, 1), EXPECT(text_align, 0x18);
    EXPECT(AT(&b, 0x60), 0), EXPECT(AT(&b, 0x68), 0), EXPECT(AT(&b, 0x1c), 2), EXPECT(spot_inits, 1);
    EXPECT(buffer_init(&b, 30, 20, 0, NULL), 0);  // the same size: nothing happens
    EXPECT(dcs, 1), EXPECT(AT(&b, 0x1c), 2);
    EXPECT(buffer_init(&b, -1, 20, 0, NULL), 3);

    // Clipping to part of it makes a region; to all of it, none.
    int32_t r[4] = {-5, 4, 12, 50};
    EXPECT(buffer_set_clip(&b, r), 0);
    EXPECT(b.clip[0], 0), EXPECT(b.clip[1], 4), EXPECT(b.clip[2], 12), EXPECT(b.clip[3], 20);
    EXPECT(regions, 1), EXPECT(last_rgn[2], 12), EXPECT(selected_rgn, AT(&b, 0x70)), EXPECT(AT(&b, 0x60), 0);
    EXPECT(buffer_set_clip_2(&b, 0, 0, 30, 20), 0);
    EXPECT(AT(&b, 0x70), 0), EXPECT(selected_rgn, 0), EXPECT(deleted, 1);
    int32_t away[4] = {40, 40, 50, 50};
    EXPECT(buffer_set_clip(&b, away), 1);
    EXPECT(b.clip[2], 0);  // emptied
    EXPECT(buffer_set_clip(&b, NULL), 3);

    // Clearing the links: the spot list is made new, its count 0, every block hung on it freed.
    {
        int inits = spot_inits, before = frees;
        AT(&b, 0x4ac) = 5, AT(&b, 0x4bc) = 0x1000, AT(&b, 0x4bc + 4 * 7) = 0x2000, AT(&b, 0x4bc + 4 * 19) = 0x3000;
        buffer_clear_links(&b);
        EXPECT(spot_inits, inits + 1), EXPECT(AT(&b, 0x4b8), 0x28), EXPECT(AT(&b, 0x4ac), 0), EXPECT(frees, before + 3);
        EXPECT(AT(&b, 0x4bc), 0), EXPECT(AT(&b, 0x4bc + 4 * 7), 0), EXPECT(AT(&b, 0x4bc + 4 * 19), 0);
        buffer_clear_links(&b);
        EXPECT(frees, before + 3);
    }

    // A new size closes and starts again.
    EXPECT(buffer_resize(&b, 8, 8), 0);
    EXPECT(dcs, 1), EXPECT(dibs, 2), EXPECT(b.pitch, 8), EXPECT(deleted, 2);

    buffer_dtor(&b);
    EXPECT(dcs, 0), EXPECT(deleted, 3), EXPECT(b.bits == NULL, 1), EXPECT(b.width, 0), EXPECT(AT(&b, 0x1c), 0);
    EXPECT(buffer_set_clip(&b, r), 7);  // no pixels

    // Tiling. The drawn-on Buffer is 12 by 8 (the stand-in DIB), the pattern 3 by 2 pixels.
    {
        static int32_t copy_scratch[4], scale[4], min_size[2];
        static struct game_sprite_scratch scratch;
        static float ratio;
        static double max_ratio = 4.0;
        game_copy_rem = &copy_scratch[0], game_mask_units = &copy_scratch[1];
        game_mask_gap_a = &copy_scratch[2], game_mask_gap_b = &copy_scratch[3];
        game_sprite_scratch = &scratch, game_sprite_scale = scale, game_sprite_ratio = &ratio;
        game_sprite_max_ratio = &max_ratio, game_sprite_min_size = min_size;
        scale[0] = scale[1] = 1;
        static uint8_t spix[8] = {1, 2, 3, 0, 4, 5, 6, 0};
        static struct game_buffer pat;
        memset(&pat, 0, sizeof pat);
        pat.bits = spix, pat.width = 3, pat.neg_height = -2, pat.pitch = 4;
        pat.full[2] = pat.clip[2] = 3, pat.full[3] = pat.clip[3] = 2;
        buffer_ctor(&b);
        EXPECT(buffer_init(&b, 12, 8, 0, NULL), 0);
#define PX(x, y) dib_pixels[(y)*12 + (x)]
        memset(dib_pixels, 0, sizeof dib_pixels);
        // Tiles from the corner of the area, cut by the area's right and bottom edges.
        EXPECT(buffer_tile(&b, &pat, 0, 0, 2, 1, 7, 3), 0);
        EXPECT(PX(2, 1), 1), EXPECT(PX(4, 1), 3), EXPECT(PX(5, 1), 1), EXPECT(PX(8, 1), 1), EXPECT(PX(9, 1), 0);
        EXPECT(PX(2, 2), 4), EXPECT(PX(2, 3), 1), EXPECT(PX(2, 4), 0), EXPECT(PX(1, 1), 0);
        EXPECT(b.clip[0], 0), EXPECT(b.clip[2], 12), EXPECT(b.clip[3], 8);  // the clip comes back
        // An offset into the pattern moves it back: sx 1 starts the tiles one pixel to the left.
        memset(dib_pixels, 0, sizeof dib_pixels);
        EXPECT(buffer_tile(&b, &pat, 1, 0, 2, 1, 4, 2), 0);
        EXPECT(PX(2, 1), 2), EXPECT(PX(3, 1), 3), EXPECT(PX(4, 1), 1), EXPECT(PX(2, 2), 5);
        // Cut to the clip rectangle as well.
        memset(dib_pixels, 0, sizeof dib_pixels);
        buffer_set_clip_2(&b, 3, 0, 2, 8);
        EXPECT(buffer_tile(&b, &pat, 0, 0, 0, 0, 12, 8), 0);
        EXPECT(PX(2, 0), 0), EXPECT(PX(3, 0), 1), EXPECT(PX(4, 0), 2), EXPECT(PX(5, 0), 0), EXPECT(b.clip[0], 3);
        buffer_set_clip_2(&b, 0, 0, 12, 8);
        // The masked tile puts the pattern over the key colour only.
        memset(dib_pixels, 9, sizeof dib_pixels);
        PX(2, 1) = 0;
        EXPECT(buffer_tile_mask(&b, &pat, 0, 0, 2, 1, 4, 2, 0), 0);
        EXPECT(PX(2, 1), 1), EXPECT(PX(3, 1), 9);
        // An area that is nothing: 0 and nothing drawn.
        EXPECT(buffer_tile(&b, &pat, 0, 0, 20, 20, 4, 4), 0), EXPECT(PX(0, 0), 9);
        // The sprite tile: 2 by 2 pixels of 5, drawn at its own key colour 9 (not drawn).
        static uint8_t five[4] = {5, 5, 5, 5};
        static struct game_sprite sp = {.data = five, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};
        memset(dib_pixels, 0, sizeof dib_pixels);
        EXPECT(buffer_tile_2(&b, &sp, 0, 0, 0, 0, 4, 4), 0);
        EXPECT(PX(0, 0), 5), EXPECT(PX(3, 3), 5), EXPECT(PX(4, 0), 0), EXPECT(PX(0, 4), 0);
        // A frame: a corner sprite, with and without the pattern inside.
        static struct game_box_frame fr;
        memset(&fr, 0, sizeof fr);
        fr.top_left = &sp;
        int32_t rect[4] = {1, 1, 9, 7};
        memset(dib_pixels, 0, sizeof dib_pixels);
        buffer_box_sprite(&b, rect, &fr);
        EXPECT(PX(1, 1), 5), EXPECT(PX(2, 2), 5), EXPECT(PX(3, 3), 0);
        fr.fill = &pat, fr.fill_key = 0;
        memset(dib_pixels, 0, sizeof dib_pixels);
        buffer_box_sprite(&b, rect, &fr);
        EXPECT(PX(8, 6), 3), EXPECT(PX(1, 1), 5), EXPECT(PX(9, 6), 0);  // the pattern covers the corner too, from the origin
        buffer_box_sprite(&b, NULL, &fr);
        buffer_dtor(&b);
        EXPECT(buffer_tile(&b, &pat, 0, 0, 0, 0, 4, 4), 7);
    }

    if (failures) return 1;
    printf("engine_bufobj: ok\n");
    return 0;
}
