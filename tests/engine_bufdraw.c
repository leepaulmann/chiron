// Unit test for a Buffer drawn onto another Buffer (bufdraw.c), on small made-up DIB buffers.
#include <stdio.h>
#include <string.h>

#include "../src/engine/bufdraw.h"
#include "../src/engine/game.h"

enum { W = 12, H = 8, PITCH = 16, SW = 3, SH = 2 };
static uint8_t pixels[PITCH * H + 8], spix[SW * SH + 8];
static struct game_buffer dst, src;
static struct game_sprite_scratch scratch;
static struct game_sprite static_sprite;
static uint8_t default_key = 7;
static int32_t scale[4], min_size[2];
static float ratio;
static double max_ratio = 4.0;
static uint8_t table[65536];
static int failures;

static void check_(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s = %d, expected %d\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (expr), (want))

static int px(int x, int y) { return pixels[y * PITCH + x]; }

static void reset(void) {
    memset(pixels, 5, sizeof pixels);
    memset(&dst, 0, sizeof dst);
    dst.bits = pixels;
    dst.width = W, dst.neg_height = -H, dst.pitch = PITCH;
    dst.full[2] = W, dst.full[3] = H;
    dst.clip[2] = W, dst.clip[3] = H;
    // The source: 1 9 2 / 0 3 9, three wide.
    static const uint8_t p[6] = {1, 9, 2, 0, 3, 9};
    memcpy(spix, p, sizeof p);
    memset(&src, 0, sizeof src);
    src.bits = spix;
    src.width = SW, src.neg_height = -SH, src.pitch = SW;
    src.full[2] = SW, src.full[3] = SH;
    src.clip[2] = SW, src.clip[3] = SH;
    scale[0] = scale[1] = 1, scale[2] = scale[3] = 0;
    memset(&static_sprite, 0, sizeof static_sprite);
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)(i + 1);
}

int main(void) {
    game_sprite_scratch = &scratch;
    game_sprite_scale = scale;
    game_sprite_ratio = &ratio;
    game_sprite_max_ratio = &max_ratio;
    game_sprite_min_size = min_size;
    game_buffer_sprite = &static_sprite;
    game_default_key = &default_key;

    // The plain draw skips the key colour, and the Sprite is pointed at the source and let go.
    reset();
    EXPECT(buffer_draw(&src, &dst, 9, 2, 3), 0);
    EXPECT(px(2, 3), 1), EXPECT(px(3, 3), 5), EXPECT(px(4, 3), 2);
    EXPECT(px(2, 4), 0), EXPECT(px(3, 4), 3), EXPECT(px(4, 4), 5);
    EXPECT(static_sprite.data == NULL, 1);
    EXPECT(static_sprite.key, 9), EXPECT(static_sprite.width, SW), EXPECT(static_sprite.height, SH);
    EXPECT(static_sprite.stride, SW), EXPECT(static_sprite.full_w, SW), EXPECT(static_sprite.left, 0);
    EXPECT(src.locks, 0), EXPECT(src.base == NULL, 1);

    // -1 asks for the game's key colour (7: nothing in the picture, so every pixel is drawn).
    reset();
    EXPECT(buffer_draw(&src, &dst, -1, 0, 0), 0);
    EXPECT(static_sprite.key, 7);
    EXPECT(px(1, 0), 9), EXPECT(px(2, 1), 9);

    // A caller's lock stays: the count is back where it was and the base kept.
    reset();
    src.locks = 2, src.base = spix;
    buffer_draw(&src, &dst, 9, 0, 0);
    EXPECT(src.locks, 2), EXPECT(src.base == spix, 1);

    // No destination: 0x10 and nothing done. No pixels in the source: nothing drawn, no lock left.
    reset();
    EXPECT(buffer_draw(&src, NULL, 9, 0, 0), 0x10);
    src.bits = NULL, src.locks = 1;
    buffer_draw(&src, &dst, 9, 0, 0);
    EXPECT(src.locks, 0), EXPECT(px(0, 0), 5);

    // The single colour draw, and one through a table (pixel 0 darkens what is under it).
    reset();
    EXPECT(buffer_draw_mono(&src, &dst, 9, 1, 1, 6), 0);
    EXPECT(px(1, 1), 6), EXPECT(px(2, 1), 5), EXPECT(px(1, 2), 6);
    reset();
    buffer_draw_dest(&src, &dst, 9, 0, 0, table);
    EXPECT(px(0, 0), 1), EXPECT(px(0, 1), 6);
    // The one with no key colour of its own uses the game's.
    reset();
    EXPECT(static_sprite.key, 0);
    buffer_draw_dest_unk1(&src, &dst, 0, 0, table);
    EXPECT(static_sprite.key, 7);
    EXPECT(px(0, 1), 6);

    // The stubs only point the Sprite.
    reset();
    EXPECT(buffer_draw_stub(&src, &dst, 3, 0, 0, table), 0);
    EXPECT(static_sprite.key, 3), EXPECT(px(0, 0), 5);

    // Scaled up by two, with the scale given: the picture is doubled and the old scale is back.
    reset();
    buffer_draw_2(&src, &dst, 9, 0, 0, 2, 1);
    EXPECT(px(0, 0), 1), EXPECT(px(1, 1), 1), EXPECT(px(2, 0), 5), EXPECT(px(4, 0), 2);
    EXPECT(scale[0], 1);

    // Set-up alone leaves the Sprite pointing at the pixels.
    reset();
    buffer_setup_buff_sprite(&src, 4);
    EXPECT(static_sprite.data == spix, 1), EXPECT(static_sprite.key, 4), EXPECT(src.locks, 0);

    if (failures) return 1;
    puts("ok");
    return 0;
}
