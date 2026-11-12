// Unit test for the Sprite drawing functions, on a small made-up DIB buffer and
// made-up sprites: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/sprite.h"

enum { W = 12, H = 8, PITCH = 16 };
static uint8_t pixels[PITCH * H + 8];
static struct game_buffer b;
static struct game_sprite_scratch scratch;
static int32_t scale[4], min_size[2];
static float ratio;
static double max_ratio = 4.0;
static int failures;

static void check_(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s = %d, expected %d\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (expr), (want))

static void reset(void) {
    memset(pixels, 0, sizeof pixels);
    memset(&b, 0, sizeof b);
    b.bits = pixels;
    b.width = W, b.neg_height = -H, b.pitch = PITCH;
    b.full[2] = W, b.full[3] = H;
    b.clip[2] = W, b.clip[3] = H;
    scale[0] = scale[1] = 1, scale[2] = scale[3] = 0;
}
static int px(int x, int y) { return pixels[y * PITCH + x]; }
static int count(int color) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) n += px(x, y) == color;
    return n;
}
static void set_scale(int n, int d) { scale[0] = n, scale[1] = d; }

// A 3 by 2 sprite, key colour 9: 1 9 2 / 0 3 9; one pixel in from its corner.
static uint8_t data[6] = {1, 9, 2, 0, 3, 9};
static struct game_sprite spr = {.data = data, .key = 9, .stride = 3, .width = 3, .height = 2, .full_w = 5, .full_h = 4, .left = 1, .top = 1};

int main(void) {
    game_sprite_scratch = &scratch;
    game_sprite_scale = scale;
    game_sprite_ratio = &ratio;
    game_sprite_max_ratio = &max_ratio;
    game_sprite_min_size = min_size;
    reset();

    // Unscaled copy: the key colour is skipped, the margin is added to the corner.
    memset(pixels, 5, sizeof pixels);
    EXPECT(sprite_draw_3(&spr, &b, 9, 2, 3), 0);
    EXPECT(px(3, 4), 1), EXPECT(px(4, 4), 5), EXPECT(px(5, 4), 2);
    EXPECT(px(3, 5), 0), EXPECT(px(4, 5), 3), EXPECT(px(5, 5), 5);
    EXPECT(px(2, 3), 5), EXPECT(px(6, 4), 5);
    EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
    EXPECT(scratch.rows, 2), EXPECT(scratch.width, 3), EXPECT(scratch.xgroups, 1), EXPECT(scratch.dst_gap, PITCH - 3);

    // Clipped on the left and top: the first row and column of the sprite are cut.
    reset();
    b.clip[0] = 4, b.clip[1] = 5;
    sprite_draw_3(&spr, &b, 9, 2, 3);
    EXPECT(count(0), W * H - 1);  // only (4,5) = 3 is left: (5,5) is the key
    EXPECT(px(4, 5), 3), EXPECT(px(3, 4), 0);
    reset();
    EXPECT(sprite_draw_3(&spr, &b, 9, -20, 3), 0);  // wholly off the buffer: nothing
    EXPECT(count(0), W * H);
    EXPECT(b.locks, 0);

    // No picture or no buffer: 3.
    struct game_sprite empty = spr;
    empty.data = NULL;
    EXPECT(sprite_draw_3(&empty, &b, 9, 0, 0), 3);
    EXPECT(sprite_draw_3(&spr, NULL, 9, 0, 0), 3);
    b.bits = NULL;
    EXPECT(sprite_draw_3(&spr, &b, 9, 0, 0), 3);

    // One colour; the table draws.
    reset();
    sprite_draw_mono_2(&spr, &b, 9, 0, 0, 0x107);  // only the low byte counts
    EXPECT(count(7), 4);
    uint8_t table[65536];
    for (int i = 0; i < 65536; i++) table[i] = (uint8_t)(i + 1);
    reset();
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest(&spr, &b, 9, 0, 0, table);  // colour 0 darkens (+1 here), the rest copies
    EXPECT(px(1, 1), 1), EXPECT(px(3, 1), 2), EXPECT(px(1, 2), 21), EXPECT(px(2, 2), 3), EXPECT(px(3, 2), 20);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_unk1(&spr, &b, 0, 0, table);  // only colour 0
    EXPECT(count(21), 1), EXPECT(px(1, 2), 21);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_source(&spr, &b, 9, 0, 0, table);  // everything but the key
    EXPECT(count(21), 4);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_4(&spr, &b, 9, 0, 0, table);  // the colour from the table: sprite colour + 1
    EXPECT(px(1, 1), 2), EXPECT(px(3, 1), 3), EXPECT(px(1, 2), 1), EXPECT(px(2, 2), 4), EXPECT(count(20), W * H - 4);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_multi_table_dest_2(&spr, &b, 9, 0, 0, table);  // from sprite colour and what is under it
    EXPECT(px(1, 1), (uint8_t)((1 << 8 | 20) + 1)), EXPECT(px(2, 2), (uint8_t)((3 << 8 | 20) + 1));

    // The scaling table: 2 to 1 is 2 each; 3 to 2 gives 1 or 2 entries that sum to 3 for every 2.
    set_scale(2, 1);
    sprite_fill_scaling_table(&spr);
    EXPECT(scratch.scale[0], 2), EXPECT(scratch.scale[799], 2);
    set_scale(3, 2);
    sprite_fill_scaling_table(&spr);
    int sum = 0;
    for (int i = 0; i < 100; i++) sum += scratch.scale[i];
    EXPECT(sum, 150);

    // Scaled up by 2: every pixel makes 2 by 2.
    reset();
    set_scale(2, 1);
    EXPECT(sprite_draw_3(&spr, &b, 9, 0, 0), 0);
    // The corner is (1, 1) * 2 = (2, 2); the picture is 6 by 4.
    EXPECT(px(2, 2), 1), EXPECT(px(3, 3), 1), EXPECT(px(4, 2), 0), EXPECT(px(6, 3), 2), EXPECT(px(7, 3), 2);
    EXPECT(px(2, 4), 0), EXPECT(px(4, 5), 3), EXPECT(px(5, 5), 3), EXPECT(px(6, 4), 0), EXPECT(px(2, 6), 0);
    EXPECT(count(1), 4), EXPECT(count(2), 4), EXPECT(count(3), 4);
    EXPECT(scale[2], 2), EXPECT(scale[3], 1), EXPECT(ratio == 2.0f, 1);
    // Cut on the left and the top part way through a pixel.
    reset();
    set_scale(2, 1);
    b.clip[0] = 3, b.clip[1] = 3;
    sprite_draw_3(&spr, &b, 9, 0, 0);
    EXPECT(count(1), 1), EXPECT(px(3, 3), 1), EXPECT(count(2), 2), EXPECT(count(3), 4);
    EXPECT(scratch.ycount, 0), EXPECT(b.locks, 0);
    // Past the limit of 4: nothing is drawn.
    reset();
    set_scale(9, 2);
    EXPECT(sprite_draw_3(&spr, &b, 9, 0, 0), 0);
    EXPECT(count(0), W * H), EXPECT(b.locks, 0);

    // Scaled down by 2 (the sprite is 3 by 2 at (1,1): half of that, 1 by 1 at (0,0) rounded down).
    reset();
    set_scale(1, 2);
    sprite_draw_3(&spr, &b, 9, 4, 4);  // corner (4 + 0, 4 + 0); the one pixel is the sprite's first
    EXPECT(px(4, 4), 1), EXPECT(count(0), W * H - 1);
    EXPECT(scratch.rows, 0), EXPECT(b.locks, 0);
    // Too small to draw: the game's limit.
    reset();
    set_scale(1, 2);
    min_size[0] = 5;
    EXPECT(sprite_draw_3(&spr, &b, 9, 4, 4), 0);
    EXPECT(count(0), W * H);
    min_size[0] = 0;

    // Changing a colour.
    EXPECT(sprite_change_color(&spr, 9, 4), 0);
    EXPECT(data[1], 4), EXPECT(data[5], 4), EXPECT(data[0], 1);
    EXPECT(sprite_change_color(&empty, 9, 4), 7);

    if (failures) return 1;
    puts("sprite: all passed");
    return 0;
}
