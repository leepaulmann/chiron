// Unit test for the Buffer drawing primitives, on a small made-up DIB buffer:
// no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/buffer.h"
#include "../src/engine/game.h"

enum { W = 10, H = 6, PITCH = 12 };  // 12 bytes a row: wider than the buffer
static uint8_t pixels[PITCH * H + 4];
static struct game_buffer b;
static int32_t gap;
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
}
static int px(int x, int y) { return pixels[y * PITCH + x]; }
static int count(int color) {
    int n = 0;
    for (int i = 0; i < PITCH * H; i++) n += pixels[i] == color;
    return n;
}

int main(void) {
    game_fill_gap = &gap;
    reset();

    // A horizontal line includes both ends, goes either way, and stays in the clip rectangle.
    buffer_hline(&b, 2, 5, 1, 7);
    EXPECT(px(1, 1), 0), EXPECT(px(2, 1), 7), EXPECT(px(5, 1), 7), EXPECT(px(6, 1), 0), EXPECT(count(7), 4);
    buffer_hline(&b, 8, -3, 2, 8);
    EXPECT(count(8), 9);  // from the left edge to x = 8
    buffer_hline(&b, 4, 4, 3, 9);  // one pixel long: nothing
    EXPECT(count(9), 0);
    b.clip[0] = 3, b.clip[2] = 6, b.clip[1] = 1, b.clip[3] = 4;
    buffer_hline(&b, 0, 20, 3, 5);
    EXPECT(count(5), 3);
    buffer_hline(&b, 0, 20, 4, 6);  // below the clip
    buffer_hline(&b, 0, 20, 0, 6);  // above it
    EXPECT(count(6), 0);
    EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);

    // Vertical lines the same.
    reset();
    buffer_vline(&b, 3, 4, 1, 2);
    EXPECT(count(2), 4), EXPECT(px(3, 1), 2), EXPECT(px(3, 4), 2), EXPECT(px(3, 5), 0);
    buffer_vline(&b, 3, -5, 99, 3);
    EXPECT(count(3), H);
    buffer_vline(&b, W, 0, 3, 4);
    EXPECT(count(4), 0);

    // Dotted lines: every other pixel from the first, half the length rounded down.
    reset();
    buffer_dotted_hline(&b, 2, 7, 1, 7);
    EXPECT(count(7), 3), EXPECT(px(2, 1), 7), EXPECT(px(3, 1), 0), EXPECT(px(4, 1), 7), EXPECT(px(6, 1), 7);
    buffer_dotted_hline(&b, 0, 4, 2, 8);  // five pixels: 0 and 2
    EXPECT(count(8), 2), EXPECT(px(4, 2), 0);
    buffer_dotted_vline(&b, 9, 5, 0, 3);
    EXPECT(count(3), 3), EXPECT(px(9, 0), 3), EXPECT(px(9, 2), 3), EXPECT(px(9, 4), 3), EXPECT(px(9, 1), 0);
    EXPECT(b.locks, 0);

    // Single pixels: not clipped, but kept in the buffer.
    reset();
    b.clip[2] = 2;
    EXPECT(buffer_put_pixel(&b, 5, 2, 0x1ff), 0);
    EXPECT(px(5, 2), 0xff), EXPECT(buffer_get_pixel(&b, 5, 2), 0xff);
    EXPECT(buffer_put_pixel(&b, W, 0, 1), 0), EXPECT(buffer_put_pixel(&b, 0, H, 1), 0), EXPECT(count(1), 0);
    EXPECT(buffer_get_pixel(&b, -1, 0), 0);
    b.bits = NULL;
    EXPECT(buffer_put_pixel(&b, 1, 1, 1), 7);

    // Rectangles: clipped, and the gap global left behind.
    reset();
    int32_t r[4] = {-2, 1, 4, 3};
    EXPECT(buffer_fill_rect(&b, r, 9), 0);
    EXPECT(count(9), 4 * 2), EXPECT(px(0, 1), 9), EXPECT(px(3, 2), 9), EXPECT(px(4, 2), 0), EXPECT(gap, PITCH - 4);
    EXPECT(buffer_fill_rect(&b, NULL, 1), 3);
    int32_t away[4] = {20, 0, 30, 3};
    EXPECT(buffer_fill_rect(&b, away, 1), 0), EXPECT(count(1), 0);
    EXPECT(buffer_fill(&b, 6, 4, 3, 5, 4), 0);
    EXPECT(count(4), 3 * 2);
    // The whole buffer in whole units of 4: the padding up to 12 is filled too.
    EXPECT(buffer_fill_all(&b, 6), 0);
    EXPECT(count(6), 12 * H);
    reset();
    b.clip[0] = 1, b.clip[1] = 1, b.clip[2] = 3, b.clip[3] = 3;
    buffer_fill_all(&b, 5);
    EXPECT(count(5), 4);

    printf(failures ? "buffer: %d failures\n" : "buffer: all passed\n", failures);
    return failures != 0;
}
