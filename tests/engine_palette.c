// Unit test for the Palette class's colour tables and the RGB/HSV
// conversions, on a made-up palette: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/palette.h"

static uint32_t handle = 1, seed;
static int randoms;
static struct palette pal, other;
static int failures;

static void check_(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s = %d, expected %d\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (expr), (want))

// A stand-in for the game's random(): 0 the first time, so the serial must be drawn again.
static int fake_random(int lo, int hi) { return randoms++ ? lo + hi / 2 : 0; }

static void grey_ramp(struct palette *p) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 256; i++) p->rgb[i][0] = p->rgb[i][1] = p->rgb[i][2] = (uint8_t)i, p->rgb[i][3] = 4;
    for (int s = 0; s < 5; s++) p->cycle[s].id = -1;
}

static int hue_of(int r, int g, int b) {
    uint8_t c[3] = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
    struct hsv h;
    rgb_to_hsv(c, &h);
    return (int)(h.h < 0 ? h.h - 0.5 : h.h + 0.5);
}

int main(void) {
    game_palette_handle = &handle;
    game_random = fake_random;
    game_random_seed = &seed;
    grey_ramp(&pal);

    // The nearest colour: the first of equals; the low byte of each channel counts.
    EXPECT(palette_nearest(&pal, 100, 100, 100, 0), 100);
    EXPECT(palette_nearest(&pal, 356, 100, 100 - 256, 0), 100);
    EXPECT(palette_nearest(&pal, 255, 0, 0, 0), 85);  // (85,85,85) is nearest: 170^2 + 2 * 85^2 = 43350
    pal.rgb[40][0] = 255, pal.rgb[40][1] = pal.rgb[40][2] = 0;
    pal.rgb[41][0] = 255, pal.rgb[41][1] = pal.rgb[41][2] = 0;
    EXPECT(palette_nearest(&pal, 250, 3, 0, 0), 40);
    // With skip_cycles: only colours 10 to 245 that do not cycle.
    pal.cycle[0] = (struct palette_cycle){.id = 7, .start = 40, .count = 1};
    EXPECT(palette_nearest(&pal, 250, 3, 0, 1), 41);
    EXPECT(palette_nearest(&pal, 0, 0, 0, 1), 10);
    EXPECT(palette_nearest(&pal, 255, 255, 255, 1), 245);
    EXPECT(palette_nearest_in(&pal, 0, 0, 0, 100, 5, 0), 100);
    EXPECT(palette_nearest_in(&pal, 250, 3, 0, 30, 20, 0), 40);
    EXPECT(palette_nearest_in(&pal, 250, 3, 0, 30, 20, 1), 41);
    handle = 0;
    EXPECT(palette_nearest(&pal, 1, 2, 3, 0), 7);
    EXPECT(palette_nearest_in(&pal, 1, 2, 3, 0, 9, 0), 7);
    EXPECT(palette_get_rgbquad(&pal, (uint8_t[4]){0}, 0, 1), 7);
    handle = 1;

    // Slots: an id's own, else the first free one, else 5.
    grey_ramp(&pal);
    EXPECT(palette_get_pos(&pal, 3), 0);
    pal.cycle[0].id = 3, pal.cycle[1].id = 4;
    EXPECT(palette_get_pos(&pal, 4), 1), EXPECT(palette_get_pos(&pal, 9), 2), EXPECT(palette_get_pos(&pal, 3), 0);
    for (int s = 0; s < 5; s++) pal.cycle[s].id = 20 + s;
    EXPECT(palette_get_pos(&pal, 99), 5);

    // RGBQUADs: blue, green, red; setting them gives the colours flag 5 and a new serial (never 0).
    grey_ramp(&pal);
    pal.rgb[2][0] = 11, pal.rgb[2][1] = 22, pal.rgb[2][2] = 33;
    uint8_t quad[8] = {0};
    EXPECT(palette_get_rgbquad(&pal, quad, 2, 2), 0);
    EXPECT(quad[0], 33), EXPECT(quad[1], 22), EXPECT(quad[2], 11), EXPECT(quad[3], 0), EXPECT(quad[4], 3);
    EXPECT(palette_get_rgbquad(&pal, NULL, 2, 2), 3);
    uint8_t set[8] = {1, 2, 3, 0, 4, 5, 6, 0};
    randoms = 0, pal.serial = 0;
    EXPECT(palette_set_rgbquad(&pal, set, 10, 2), 0);
    EXPECT(pal.rgb[10][0], 3), EXPECT(pal.rgb[10][1], 2), EXPECT(pal.rgb[10][2], 1), EXPECT(pal.rgb[10][3], 5);
    EXPECT(pal.rgb[11][0], 6), EXPECT(randoms, 2), EXPECT((int)pal.serial, 0xffff / 2);
    EXPECT(palette_set_rgbquad(&pal, NULL, 0, 1), 3);

    // A BITMAPINFO's table: colours 10 to 245 only, flags kept.
    static uint8_t dib[0x28 + 256 * 4];
    memset(dib, 0, sizeof dib);
    dib[0x28 + 10 * 4] = 9, dib[0x28 + 10 * 4 + 2] = 7;  // blue 9, red 7
    dib[0x28 + 9 * 4] = 99;
    EXPECT(palette_set_from_dib(&pal, dib), 0);
    EXPECT(pal.rgb[10][0], 7), EXPECT(pal.rgb[10][2], 9), EXPECT(pal.rgb[10][3], 5), EXPECT(pal.rgb[9][2], 9);  // colour 9 is left alone
    EXPECT(palette_set_from_dib(&pal, NULL), 3);

    // Tables that send colours to their nearest.
    grey_ramp(&pal), grey_ramp(&other);
    uint8_t table[256];
    EXPECT(palette_table_from_palette(&pal, &other, table, 20, 5), 0);
    EXPECT(table[0], 0), EXPECT(table[19], 19), EXPECT(table[20], 20), EXPECT(table[24], 24), EXPECT(table[25], 25);
    EXPECT(palette_table_from_palette(&pal, NULL, table, 20, 5), 16);
    // A half and half mix of each grey with another palette's colour 0 is halfway down.
    memset(other.rgb, 0, sizeof other.rgb);
    EXPECT(palette_blend_table(&pal, other.rgb[0], table, 100, 3, 1, 1), 0);
    EXPECT(table[99], 99), EXPECT(table[100], 50), EXPECT(table[101], 50), EXPECT(table[102], 51), EXPECT(table[103], 103);
    EXPECT(palette_blend_table(&pal, NULL, table, 0, 1, 1, 1), 16);
    EXPECT(palette_blend_table(&pal, other.rgb[0], NULL, 0, 1, 1, 1), 16);
    // A mix with one of its own colours; cycling colours stay.
    pal.cycle[0] = (struct palette_cycle){.id = 1, .start = 101, .count = 1};
    EXPECT(palette_table_from_color(&pal, 0, table, 100, 4, 1, 1), 0);
    EXPECT(table[100], 50), EXPECT(table[101], 101), EXPECT(table[102], 51), EXPECT(table[104], 104);
    // The nearest of a range, for a BITMAPINFO.
    EXPECT(palette_table_to_dib(&pal, dib, 0, 256), 0);
    EXPECT(dib[0x28 + 5 * 4], 15), EXPECT(dib[0x28 + 5 * 4 + 3], 4);  // the first entry is colour 10's nearest
    EXPECT(palette_table_to_dib(&pal, NULL, 0, 256), 16);

    // A copy gets a serial of its own.
    randoms = 0;
    EXPECT(palette_assign(&pal, other.rgb[0]) == &pal, 1), EXPECT(randoms, 2);

    // RGB to HSV: hue in degrees, grey is -1.
    EXPECT(hue_of(255, 0, 0), 0), EXPECT(hue_of(0, 255, 0), 120), EXPECT(hue_of(0, 0, 255), 240);
    EXPECT(hue_of(128, 128, 128), -1), EXPECT(hue_of(0, 0, 0), -1);
    EXPECT(hue_of(255, 255, 0), 60);
    struct hsv h;
    rgb_to_hsv((uint8_t[]){128, 64, 0}, &h);
    EXPECT(h.v == 0.5, 1), EXPECT(h.s == 1.0, 1), EXPECT(h.h == 30.0, 1);
    // HSV to RGB: a value of 1 gives 0 (256 does not fit in a byte).
    uint8_t rgb[3];
    hsv_to_rgb(&(struct hsv){0, 1, 0.5}, rgb);
    EXPECT(rgb[0], 128), EXPECT(rgb[1], 0), EXPECT(rgb[2], 0);
    hsv_to_rgb(&(struct hsv){120, 1, 0.5}, rgb);
    EXPECT(rgb[0], 0), EXPECT(rgb[1], 128), EXPECT(rgb[2], 0);
    hsv_to_rgb(&(struct hsv){-1, 0, 0.25}, rgb);
    EXPECT(rgb[0], 64), EXPECT(rgb[1], 64), EXPECT(rgb[2], 64);
    hsv_to_rgb(&(struct hsv){0, 1, 1}, rgb);
    EXPECT(rgb[0], 0);
    hsv_to_rgb(&(struct hsv){360, 1, 0.5}, rgb);
    EXPECT(rgb[0], 128);

    printf(failures ? "palette: %d failures\n" : "palette: all passed\n", failures);
    return failures != 0;
}
