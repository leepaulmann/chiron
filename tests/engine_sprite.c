// Unit test for the Sprite drawing functions, on a small made-up DIB buffer and
// made-up sprites: no game files needed.
#include <stdio.h>
#include <stdlib.h>
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

    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_unk2_2(&spr, &b, 9, 0, 0, table);  // not the key, not 0: darkens what is under
    EXPECT(count(21), 3), EXPECT(px(1, 2), 20), EXPECT(px(1, 1), 21), EXPECT(px(2, 2), 21);

    // The wrappers set a scale for the call and put the old one back.
    reset();
    EXPECT(sprite_draw_2(&spr, &b, 9, 0, 0, 2, 1), 0);
    EXPECT(count(1), 4), EXPECT(count(3), 4), EXPECT(scale[0], 1), EXPECT(scale[1], 1);
    reset();
    sprite_draw_mono(&spr, &b, 9, 0, 0, 7, 2, 1);
    EXPECT(count(7), 16), EXPECT(scale[0], 1), EXPECT(scale[1], 1);
    reset();
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_unk2(&spr, &b, 9, 0, 0, 1, 1, table);
    EXPECT(count(21), 3);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_unk1_2(&spr, &b, 0, 0, 2, 1, table);  // colour 0 scaled to 2 by 2
    EXPECT(count(21), 4);
    memset(pixels, 20, sizeof pixels);
    sprite_draw_dest_source_2(&spr, &b, 9, 0, 0, 1, 1, table);
    EXPECT(count(21), 4);
    sprite_draw_dest_2(&spr, &b, 9, 0, 0, 1, 1, table);
    sprite_draw(&spr, &b, 9, 0, 0, 1, 1, table);
    sprite_draw_multi_table_dest(&spr, &b, 9, 0, 0, 1, 1, table);
    EXPECT(scale[0], 1), EXPECT(scale[1], 1);

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

    // Draw_dest_unk2_2 scaled down: a one-pixel row starts at the last of an unrolled four, which
    // draws only a pixel of colour 0 (from table entry 0, here 1).
    {
        static uint8_t d0[6] = {0, 9, 2, 0, 3, 9}, d5[6] = {5, 9, 2, 0, 3, 9};
        struct game_sprite z = spr;
        static uint8_t tb[65536];
        for (int i = 0; i < 65536; i++) tb[i] = (uint8_t)(i + 1);
        z.data = d0;
        reset();
        set_scale(1, 2);
        sprite_draw_dest_unk2_2(&z, &b, 9, 4, 4, tb);
        EXPECT(px(4, 4), 1), EXPECT(count(0), W * H - 1);
        z.data = d5;
        reset();
        set_scale(1, 2);
        sprite_draw_dest_unk2_2(&z, &b, 9, 4, 4, tb);
        EXPECT(count(0), W * H);
    }

    // Changing a colour.
    EXPECT(sprite_change_color(&spr, 9, 4), 0);
    EXPECT(data[1], 4), EXPECT(data[5], 4), EXPECT(data[0], 1);
    EXPECT(sprite_change_color(&empty, 9, 4), 7);

    // Cutting a sprite out of the buffer: a 3 by 2 picture of 1 2 3 / 4 key 5 in a field of the
    // key colour 7, from a rectangle that has two empty rows above it, three columns to the right
    // of it, and so on.
    {
        static int32_t bytes;
        game_sprite_bytes = &bytes;
        game_mem_get = malloc;
        game_free = free;
        struct game_sprite out;
        memset(&out, 0, sizeof out);
        reset();
        memset(pixels, 7, sizeof pixels);
        static const uint8_t pic[6] = {1, 2, 3, 4, 7, 5};
        for (int i = 0; i < 6; i++) pixels[(3 + i / 3) * PITCH + 4 + i % 3] = pic[i];
        EXPECT(sprite_extract_2(&out, &b, 7, 2, 1, 8, 6, NULL), 0);
        EXPECT(out.left, 2), EXPECT(out.top, 2), EXPECT(out.width, 3), EXPECT(out.height, 2), EXPECT(out.stride, 3);
        EXPECT(out.full_w, 8), EXPECT(out.full_h, 6), EXPECT(out.key, 7), EXPECT(out.shared, 0);
        EXPECT(memcmp(out.data, pic, 6), 0), EXPECT(bytes, 6);
        EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
        // The same by rectangle, over the sprite that is there: its pixels are let go.
        int32_t rect[4] = {2, 1, 10, 7};
        EXPECT(sprite_extract(&out, &b, 7, rect, NULL), 0);
        EXPECT(out.width, 3), EXPECT(bytes, 6), EXPECT(sprite_extract(&out, &b, 7, NULL, NULL), 3);
        // A copy has its own block, and does not count in the bytes.
        struct game_sprite cp;
        memset(&cp, 0, sizeof cp);
        EXPECT(sprite_copy(&out, &cp), 0);
        EXPECT(cp.data != out.data, 1), EXPECT(memcmp(cp.data, pic, 6), 0), EXPECT(cp.left, 2), EXPECT(cp.key, 7);
        EXPECT(bytes, 6);
        EXPECT(sprite_copy(&empty, &cp), 7);
        free(cp.data);
        // Nothing but the key colour: 9, no pixels. A rectangle past the buffer: 3, and the
        // buffer is left locked. No pixels behind the corner: 7.
        EXPECT(sprite_extract_2(&out, &b, 7, 0, 0, 3, 1, NULL), 9);
        EXPECT(out.data == NULL, 1), EXPECT(bytes, 0), EXPECT(b.locks, 0);
        EXPECT(sprite_extract_2(&out, &b, 7, 6, 0, 8, 2, NULL), 3);
        EXPECT(b.locks, 1), EXPECT(sprite_extract_2(&out, &b, 7, 0, 0, 0, 2, NULL), 3);
        b.locks = 0;
        EXPECT(sprite_extract_2(&out, NULL, 7, 0, 0, 2, 2, NULL), 3);
        b.bits = NULL;
        EXPECT(sprite_extract_2(&out, &b, 7, 0, 0, 2, 2, NULL), 3);
    }

    // The lifecycle: new, named, closed (the key colour stays), the bytes counted along.
    {
        static int32_t bytes;
        game_sprite_bytes = &bytes;
        game_mem_get = malloc;
        game_free = free;
        bytes = 100;
        struct game_sprite sp;
        memset(&sp, 0x5a, sizeof sp);
        EXPECT(sprite_ctor(&sp) == &sp, 1), EXPECT(bytes, 100 + 0x2c), EXPECT(sp.key, 9), EXPECT(sp.name == NULL, 1);
        EXPECT(sp.data == NULL, 1), EXPECT(sp.width, 0), EXPECT(sp.shared, 0), EXPECT(sp.left, 0);
        bytes = 100;
        EXPECT(sprite_init(&sp, NULL, 1, 1), 0x10);
        EXPECT(sprite_init(&sp, "tiles.pcx", 12, 7), 0);
        EXPECT(strcmp(sp.name, "tiles.pcx"), 0), EXPECT(sp.width, 12), EXPECT(sp.stride, 12), EXPECT(sp.full_w, 12);
        EXPECT(sp.height, 7), EXPECT(sp.full_h, 7), EXPECT(sp.data == NULL, 1), EXPECT(bytes, 100);
        sp.key = 3;
        sp.data = malloc(6), sp.width = 3, sp.height = 2, bytes = 106;
        EXPECT(sprite_init(&sp, "x", 1, 2), 0);  // the pixels go, and the bytes with them
        EXPECT(bytes, 100), EXPECT(sp.data == NULL, 1), EXPECT(strcmp(sp.name, "x"), 0);
        sprite_close(&sp);
        EXPECT(sp.name == NULL, 1), EXPECT(sp.width, 0), EXPECT(sp.full_h, 0), EXPECT(sp.key, 3);
        // Pixels that are another's are neither freed nor counted, and the pointer stays.
        static uint8_t other[4];
        sp.data = other, sp.shared = 1, sp.width = 2, sp.height = 2;
        sprite_close(&sp);
        EXPECT(sp.data == other, 1), EXPECT(sp.shared, 0), EXPECT(bytes, 100);
    }

    if (failures) return 1;
    puts("sprite: all passed");
    return 0;
}
