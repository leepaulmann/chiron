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
static uint8_t poly_scratch[0x50];
#define POLY(off) (*(int32_t *)(poly_scratch + (off)))
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
    game_poly_scratch = poly_scratch;
    static int32_t scratch[4];
    game_copy_rem = &scratch[0], game_mask_units = &scratch[1], game_mask_gap_a = &scratch[2], game_mask_gap_b = &scratch[3];
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

    // A diagonal line includes both ends and is the same drawn from either end.
    reset();
    buffer_line(&b, 1, 1, 7, 4, 5);
    EXPECT(count(5), 7), EXPECT(px(1, 1), 5), EXPECT(px(2, 1), 5), EXPECT(px(3, 2), 5), EXPECT(px(4, 2), 5), EXPECT(px(5, 3), 5),
        EXPECT(px(6, 3), 5), EXPECT(px(7, 4), 5);
    uint8_t first[sizeof pixels];
    memcpy(first, pixels, sizeof pixels);
    reset();
    buffer_line(&b, 7, 4, 1, 1, 5);
    EXPECT(memcmp(first, pixels, sizeof pixels), 0);
    // Steep, and going up; clipped to the clip rectangle.
    reset();
    buffer_line(&b, 2, 5, 4, 0, 6);
    EXPECT(count(6), 6), EXPECT(px(2, 5), 6), EXPECT(px(4, 0), 6);
    reset();
    b.clip[0] = 3, b.clip[1] = 2, b.clip[2] = 6, b.clip[3] = 5;
    buffer_line(&b, 0, 0, 9, 5, 7);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (px(x, y)) EXPECT(x >= 3 && x < 6 && y >= 2 && y < 5, 1);
    EXPECT(count(7) > 0, 1), EXPECT(b.locks, 0);
    // Straight lines go to the line functions.
    reset();
    buffer_line(&b, 1, 2, 6, 2, 3);
    EXPECT(count(3), 6);
    buffer_line(&b, 3, 5, 3, 0, 4);
    EXPECT(count(4), 6);

    // A pattern alternates c1 (a pixels) and c2 (bl pixels), from the phase.
    reset();
    buffer_pat_hline(&b, 0, 9, 1, 1, 2, 2, 3, 0);
    const int want[10] = {1, 1, 2, 2, 2, 1, 1, 2, 2, 2};
    for (int x = 0; x < 10; x++) EXPECT(px(x, 1), want[x]);
    reset();
    buffer_pat_hline(&b, 0, 9, 1, -1, 2, 2, 3, 0);  // -1: that colour is not drawn
    EXPECT(count(2), 6), EXPECT(count(1), 0), EXPECT(px(0, 1), 0), EXPECT(px(2, 1), 2);
    reset();
    buffer_pat_vline(&b, 4, 0, 5, 7, 8, 1, 1, 1);  // phase 1: starts in the c2 run
    EXPECT(px(4, 0), 8), EXPECT(px(4, 1), 7), EXPECT(px(4, 2), 8), EXPECT(px(4, 3), 7);
    reset();
    buffer_pat_line(&b, 1, 1, 7, 4, 3, 4, 1, 1, 0);
    EXPECT(count(3) + count(4), 7), EXPECT(px(1, 1), 3);

    // A box: top and left in the first colour, bottom and right in the second.
    reset();
    buffer_box(&b, 1, 1, 6, 4, 7, 8);
    EXPECT(px(1, 1), 7), EXPECT(px(6, 1), 7), EXPECT(px(3, 1), 7), EXPECT(px(1, 3), 7), EXPECT(px(1, 4), 8), EXPECT(px(5, 4), 8),
        EXPECT(px(6, 4), 8), EXPECT(px(6, 2), 8), EXPECT(px(3, 2), 0), EXPECT(count(7) + count(8), 5 + 5 + 3 + 3);
    reset();
    int32_t rect[4] = {1, 1, 7, 5};
    EXPECT(buffer_box_rect(&b, rect, 7, 8), 0), EXPECT(px(6, 4), 8), EXPECT(px(1, 1), 7);
    EXPECT(buffer_box_rect(&b, NULL, 7, 8), 3), EXPECT(buffer_dotted_box_rect(&b, NULL, 1), 0x10);
    reset();
    buffer_dotted_box(&b, 0, 0, 8, 4, 9);
    EXPECT(count(9) > 0, 1), EXPECT(px(0, 0), 9);

    // Copies: this buffer's pixels go into another buffer, of another pitch and size.
    static uint8_t spix[8 * 5 + 4];
    static struct game_buffer sb;
    memset(&sb, 0, sizeof sb);
    sb.bits = spix, sb.width = 8, sb.neg_height = -5, sb.pitch = 8;
    sb.full[2] = 8, sb.full[3] = 5, sb.clip[2] = 8, sb.clip[3] = 5;
    reset();
    for (int i = 0; i < PITCH * H; i++) pixels[i] = (uint8_t)(i + 1);
    EXPECT(buffer_copy_full(&b, &sb, 2, 1, 1, 2, 3, 2), 0);  // from (2, 1) here to (1, 2) there
    EXPECT(spix[2 * 8 + 1], px(2, 1)), EXPECT(spix[2 * 8 + 3], px(4, 1)), EXPECT(spix[3 * 8 + 1], px(2, 2)),
        EXPECT(spix[2 * 8 + 4], 0), EXPECT(spix[4 * 8 + 1], 0), EXPECT(*game_copy_rem, 3 & 3);
    EXPECT(b.locks, 0), EXPECT(sb.locks, 0), EXPECT(b.base == NULL, 1), EXPECT(px(2, 1), 1 * PITCH + 2 + 1);
    // Cut at the left and top of this buffer: the corner in the other moves with it.
    memset(spix, 0, sizeof spix);
    EXPECT(buffer_copy_full(&b, &sb, -2, -1, 0, 0, 4, 3), 0);
    EXPECT(spix[1 * 8 + 2], px(0, 0)), EXPECT(spix[1 * 8 + 3], px(1, 0)), EXPECT(spix[1 * 8 + 4], 0), EXPECT(spix[2 * 8 + 2], px(0, 1));
    EXPECT(spix[0], 0);
    // Cut by the other buffer's clip rectangle, and at the right and bottom of this one.
    memset(spix, 0, sizeof spix);
    sb.clip[0] = 2, sb.clip[1] = 1, sb.clip[2] = 5, sb.clip[3] = 3;
    EXPECT(buffer_copy_full(&b, &sb, 0, 0, 0, 0, 8, 5), 0);
    EXPECT(spix[0], 0), EXPECT(spix[1 * 8 + 2], px(2, 1)), EXPECT(spix[2 * 8 + 4], px(4, 2)), EXPECT(spix[1 * 8 + 5], 0), EXPECT(spix[3 * 8 + 2], 0);
    sb.clip[0] = 0, sb.clip[1] = 0, sb.clip[2] = 8, sb.clip[3] = 5;
    memset(spix, 0, sizeof spix);
    EXPECT(buffer_copy_full(&b, &sb, W - 3, H - 2, 0, 0, 8, 5), 0);  // only 3 by 2 of it is in this buffer
    EXPECT(spix[0], px(W - 3, H - 2)), EXPECT(spix[2], px(W - 1, H - 2)), EXPECT(spix[3], 0), EXPECT(spix[8], px(W - 3, H - 1)), EXPECT(spix[16], 0);
    EXPECT(buffer_copy_full(&b, NULL, 0, 0, 0, 0, 1, 1), 3);
    EXPECT(buffer_copy_full(&b, &sb, W + 1, 0, 0, 0, 1, 1), 0);
    b.bits = NULL;
    EXPECT(buffer_copy_full(&b, &sb, 0, 0, 0, 0, 1, 1), 7);

    // The masked copy replaces the key colour in the other buffer with this buffer's pixels.
    reset();
    for (int i = 0; i < PITCH * H; i++) pixels[i] = 100;
    for (int i = 0; i < 40; i++) spix[i] = (uint8_t)(i + 1);
    spix[1 * 8 + 2] = 7, spix[1 * 8 + 3] = 7;
    EXPECT(buffer_copy_mask(&b, &sb, 2, 1, 2, 1, 3, 2, 7), 0);
    EXPECT(spix[1 * 8 + 2], 100), EXPECT(spix[1 * 8 + 3], 100), EXPECT(spix[1 * 8 + 4], 1 * 8 + 4 + 1), EXPECT(px(2, 1), 100);
    EXPECT(b.locks, 0), EXPECT(sb.locks, 0);

    // Recolouring through a table: the rectangle, cut to the clip rectangle.
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)i;
    table[3] = 9;
    reset();
    memset(pixels, 3, sizeof pixels);
    EXPECT(buffer_map_colors(&b, 1, 1, 4, 3, table), 0);
    EXPECT(count(9), 3 * 2), EXPECT(px(1, 1), 9), EXPECT(px(3, 2), 9), EXPECT(px(4, 2), 3), EXPECT(px(0, 1), 3), EXPECT(b.locks, 0);
    EXPECT(buffer_map_colors(&b, 1, 1, 4, 3, NULL), 0x10);
    b.clip[0] = 0, b.clip[1] = 0, b.clip[2] = 2, b.clip[3] = 2;
    EXPECT(buffer_map_colors(&b, -5, -5, 20, 20, table), 0);
    EXPECT(count(9), 3 * 2 + 3);  // the 2 by 2 corner, 1 of which was done before
    EXPECT(buffer_map_colors(&b, 7, 7, 9, 9, table), 0);
    EXPECT(buffer_map_colors(&b, W, 0, W + 3, 3, table), 0);  // empty after the cut: nothing
    // The rectangle form takes one less off the right and bottom.
    reset();
    memset(pixels, 3, sizeof pixels);
    int32_t mr[4] = {1, 1, 5, 4};
    EXPECT(buffer_map_colors_2(&b, mr, table), 0);
    EXPECT(count(9), 3 * 2), EXPECT(px(3, 2), 9), EXPECT(px(4, 2), 3), EXPECT(px(3, 3), 3);
    EXPECT(buffer_map_colors_2(&b, NULL, table), 0x10), EXPECT(buffer_map_colors_2(&b, mr, NULL), 0x10);
    b.bits = NULL;
    EXPECT(buffer_map_colors_2(&b, mr, table), 7);
    EXPECT(buffer_map_colors(&b, 1, 1, 4, 3, table), 7);

    // Changing a colour: the whole buffer, or a rectangle, cut to the clip rectangle.
    reset();
    memset(pixels, 4, sizeof pixels);
    b.clip[0] = 3, b.clip[1] = 2, b.clip[2] = 5, b.clip[3] = 4;
    EXPECT(buffer_change_color(&b, 4, 6), 0);
    EXPECT(count(6), 2 * 2), EXPECT(px(3, 2), 6), EXPECT(px(2, 2), 4), EXPECT(px(5, 3), 4), EXPECT(b.locks, 0);
    reset();
    memset(pixels, 4, sizeof pixels);
    int32_t cr[4] = {1, 1, 4, 3};
    EXPECT(buffer_change_color_2(&b, cr, 0x104, 7), 0);  // only the low byte of the colour counts
    EXPECT(count(7), 3 * 2), EXPECT(px(1, 1), 7), EXPECT(px(3, 2), 7), EXPECT(px(4, 2), 4);
    EXPECT(buffer_change_color_2(&b, NULL, 4, 7), 3);
    EXPECT(buffer_change_color_3(&b, 2, 2, 3, 2, 4, 8), 0);
    EXPECT(count(8), 3 * 2 - 2), EXPECT(px(2, 2), 7), EXPECT(px(4, 3), 8);  // (2,2), (3,2) were already 7
    b.clip[0] = 0, b.clip[1] = 0, b.clip[2] = 3, b.clip[3] = 3;
    EXPECT(buffer_change_color_3(&b, 0, 0, 20, 20, 0, 5), 0);
    EXPECT(count(5), 0);  // no pixel was 0
    EXPECT(buffer_change_color_3(&b, 0, 0, 20, 20, 4, 5), 0);
    EXPECT(count(5) > 0, 1), EXPECT(px(5, 5), 4), EXPECT(px(2, 2), 7);
    b.bits = NULL;
    EXPECT(buffer_change_color(&b, 1, 2), 7), EXPECT(buffer_change_color_3(&b, 0, 0, 1, 1, 1, 2), 7);
    EXPECT(buffer_change_color_2(&b, cr, 1, 2), 7), EXPECT(buffer_change_color_2(&b, NULL, 1, 2), 3);

    // A flat polygon: a square (2,1)-(8,5), the rows from the top to the one before the
    // clip bottom less one, the columns from the left up to the one before the right.
    reset();
    const int32_t sq[8] = {2, 1, 8, 1, 8, 5, 2, 5};
    EXPECT(buffer_polygon(&b, sq, 4, 5), 0);
    EXPECT(count(5), 6 * 4), EXPECT(px(2, 1), 5), EXPECT(px(7, 4), 5), EXPECT(px(8, 1), 0), EXPECT(px(2, 5), 0), EXPECT(px(1, 1), 0);
    EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
    EXPECT(POLY(0x38), 4), EXPECT(POLY(0x08), 5), EXPECT(POLY(0x40), PITCH), EXPECT(POLY(0x44), W - 1), EXPECT(POLY(0x48), H - 1);
    EXPECT(POLY(0x10), 2), EXPECT(POLY(0x3c), 4), EXPECT(POLY(0x0c) != 0, 1);
    // The last column and the last row of the clip rectangle are not drawn.
    reset();
    b.clip[0] = 3, b.clip[1] = 2, b.clip[2] = 6, b.clip[3] = 5;
    buffer_polygon(&b, sq, 4, 5);
    EXPECT(count(5), 2 * 2), EXPECT(px(3, 2), 5), EXPECT(px(4, 3), 5), EXPECT(px(5, 2), 0), EXPECT(px(3, 4), 0), EXPECT(px(2, 2), 0);
    // A triangle with a sloping edge.
    reset();
    const int32_t tri[6] = {2, 0, 9, 0, 2, 5};
    buffer_polygon(&b, tri, 3, 6);
    EXPECT(count(6) > 6, 1), EXPECT(px(2, 0), 6), EXPECT(px(8, 0), 6), EXPECT(px(9, 0), 0), EXPECT(px(2, 4), 6), EXPECT(px(8, 2), 0);
    // Through a table, a pixel at a time.
    reset();
    memset(pixels, 7, sizeof pixels);
    uint8_t t2[256];
    for (int i = 0; i < 256; i++) t2[i] = (uint8_t)(i == 7 ? 8 : i);
    EXPECT(buffer_polygon_dest(&b, sq, 4, t2), 0);
    EXPECT(count(8), 6 * 4), EXPECT(px(2, 1), 8), EXPECT(px(8, 1), 7), EXPECT(POLY(0x30) != 0, 1);
    // Too few corners, no corners, no polygon, and a buffer with no pixels.
    reset();
    const int32_t line[4] = {1, 1, 9, 1};
    EXPECT(buffer_polygon(&b, line, 2, 5), 0), EXPECT(buffer_polygon(&b, sq, 0, 5), 0), EXPECT(count(5), 0);
    EXPECT(buffer_polygon(&b, NULL, 4, 5), 0x10), EXPECT(buffer_polygon_dest(&b, NULL, 4, t2), 0x10);
    b.bits = NULL;
    EXPECT(buffer_polygon(&b, sq, 4, 5), 0), EXPECT(b.locks, 0), EXPECT(POLY(0x0c), 0);

    printf(failures ? "buffer: %d failures\n" : "buffer: all passed\n", failures);
    return failures != 0;
}
