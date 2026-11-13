// Unit test for the Texture polygon fills, on a small made-up DIB buffer and
// made-up textures: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/texture.h"

enum { W = 16, H = 12, PITCH = 16 };
static uint8_t pixels[PITCH * H + 8];
static struct game_buffer b;
static uint8_t scratch[0x9bb570 - 0x9bb490];
static int32_t tbl[4] = {0, 0, -256, 256};
static int failures;

// A texture that wraps must sit on a 64 KB boundary.
static uint8_t texdata[0x10000] __attribute__((aligned(0x10000)));
static uint8_t overdata[0x10000] __attribute__((aligned(0x10000)));
static struct game_texture tex = {.data = texdata, .pitch = 256, .height = 256, .ready = 1};

#define SCR(a) (*(uint32_t *)(scratch + ((a)-0x9bb490)))

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
static int texel(int u, int v) { return texdata[v * 256 + u]; }

// The corners of a rectangle (2,2)-(6,6) and the part of the picture it shows.
static const int32_t rect[8] = {2, 2, 6, 2, 6, 6, 2, 6};
static const int32_t rect_uv[8] = {10, 20, 14, 20, 14, 24, 10, 24};

int main(void) {
    game_tex_scratch = scratch;
    game_tex_tbl = tbl;
    for (int v = 0; v < 256; v++)
        for (int u = 0; u < 256; u++) texdata[v * 256 + u] = (uint8_t)(1 + ((u * 3 + v * 5) % 200));
    memset(overdata, 1, sizeof overdata);
    reset();

    // Edges: from the top left corner, the left edge (direction -1) goes down the left
    // side and the right one (+1) along the flat top and down the right side.
    SCR(0x9bb4f4) = (uint32_t)(uintptr_t)rect, SCR(0x9bb490) = (uint32_t)(uintptr_t)rect_uv;
    SCR(0x9bb4b0) = 4, SCR(0x9bb4b4) = 2;
    int32_t e[15] = {-1};
    EXPECT(texture_setup_edge(e, 0), 1);
    EXPECT(e[1], 4), EXPECT(e[2], 3), EXPECT(e[3], 10 << 16), EXPECT(e[4], 20 << 16);
    EXPECT(e[5], 0), EXPECT(e[6], 1 << 16), EXPECT(e[7], 2), EXPECT(e[9], 1), EXPECT(e[12], 4);
    int32_t r[15] = {1};
    EXPECT(texture_setup_edge(r, 0), 1);  // the flat top is walked over
    EXPECT(r[2], 2), EXPECT(r[1], 4), EXPECT(r[7], 6), EXPECT(r[3], 14 << 16), EXPECT(r[5], 0);
    // An edge that goes up, or reaches the lowest corner, is none.
    int32_t up[15] = {-1};
    EXPECT(texture_setup_edge(up, 2), 0);
    SCR(0x9bb4b4) = 99;
    int32_t sl[15] = {1};
    int32_t slant_pts[8] = {2, 2, 8, 2, 5, 6, 2, 6};
    SCR(0x9bb4f4) = (uint32_t)(uintptr_t)slant_pts;
    EXPECT(texture_setup_edge(sl, 1), 1);  // from (8,2) to (5,6): x goes left, by 3 over 4 rows
    EXPECT(sl[9], -1), EXPECT(sl[8], 0), EXPECT(sl[11], 3), EXPECT(sl[10], 1 - 4);
    // The shaded edge starts half a texel in.
    int32_t zs[4] = {10, 20, 30, 40};
    SCR(0x9bb4fc) = (uint32_t)(uintptr_t)zs, SCR(0x9bb4f4) = (uint32_t)(uintptr_t)rect;
    SCR(0x9bb4b4) = 2;
    int32_t g[15] = {-1};
    EXPECT(texture_setup_edge_gouraud(g, 0), 1);
    EXPECT(g[3], (10 << 16) + 0x8000), EXPECT(g[13], 10 << 16), EXPECT(g[14], (int32_t)(((int64_t)(40 - 10) << 32) / (4 << 16)));

    // A fill that wraps draws a pixel more than the others: x = 2 to 6 on each row.
    reset();
    texture_draw_trans(&tex, &b, rect, 4, rect_uv);
    for (int y = 2; y < 6; y++)
        for (int x = 2; x <= 6; x++) EXPECT(px(x, y), texel(10 + (x - 2), 20 + (y - 2)));
    EXPECT(px(1, 2), 0), EXPECT(px(7, 2), 0), EXPECT(px(2, 1), 0), EXPECT(px(2, 6), 0);
    EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);

    // The same cut by the clip: nothing outside it, and not even the extra pixel.
    reset();
    b.clip[0] = 3, b.clip[1] = 3, b.clip[2] = 5, b.clip[3] = 5;
    texture_draw_trans(&tex, &b, rect, 4, rect_uv);
    EXPECT(px(2, 3), 0), EXPECT(px(3, 3), texel(11, 21)), EXPECT(px(5, 3), texel(13, 21)), EXPECT(px(6, 3), 0);
    EXPECT(px(3, 2), 0), EXPECT(px(3, 5), 0);

    // A texture that does not wrap (any width) with a key colour: the others stop at the corner.
    static uint8_t small[64 * 64];
    for (int i = 0; i < 64 * 64; i++) small[i] = (uint8_t)(1 + i % 7);
    struct game_texture st = {.data = small, .pitch = 64, .height = 64, .ready = 0};
    SCR(0x9bb548) = 3;  // the key colour
    reset();
    texture_draw_upper_threshold_trans(&st, &b, rect, 4, rect_uv);
    for (int y = 2; y < 6; y++)
        for (int x = 2; x < 6; x++) {
            int want = small[(20 + (y - 2)) * 64 + 10 + (x - 2)];
            EXPECT(px(x, y), want == 3 ? 0 : want);
        }
    EXPECT(px(6, 2), 0), EXPECT(px(1, 2), 0);

    // The dithered one: even rows (counted from the top of the polygon's row 2) are a plain colour.
    reset();
    texture_draw_upper_threshold_trans_2(&st, &b, rect, 4, rect_uv, 200);
    for (int x = 2; x < 6; x++) {
        int want0 = small[20 * 64 + 10 + (x - 2)], want1 = small[21 * 64 + 10 + (x - 2)];
        EXPECT(px(x, 2), want0 == 3 ? 0 : 200);
        EXPECT(px(x, 3), want1 == 3 ? 0 : want1);
    }
    // With -1 it is the plain one.
    reset();
    texture_draw_upper_threshold_trans_2(&st, &b, rect, 4, rect_uv, -1);
    EXPECT(px(2, 2), small[20 * 64 + 10] == 3 ? 0 : small[20 * 64 + 10]);

    // A shaded fill adds the shading value (zs here all 0: nothing added).
    int32_t z0[4] = {0, 0, 0, 0};
    reset();
    texture_draw_2(&st, &b, rect, z0, 4, rect_uv);
    EXPECT(px(3, 3), small[21 * 64 + 11]);
    // Values past the limits are cut, in place.
    int32_t zbig[4] = {1000, -1000, 5, 256};
    reset();
    texture_draw_2(&st, &b, rect, zbig, 4, rect_uv);
    EXPECT(zbig[0], 256), EXPECT(zbig[1], -256), EXPECT(zbig[2], 5), EXPECT(zbig[3], 256);
    EXPECT(b.locks, 0);

    // The threshold fill draws nothing if no corner is below the threshold.
    reset();
    int32_t zt[4] = {50, 60, 70, 80};
    texture_draw(&st, &b, rect, zt, 4, rect_uv, 40);
    EXPECT(px(3, 3), 0);
    EXPECT(b.locks, 0);

    // The mask fill leaves pixels alone where the mask is 0 and otherwise looks the pixel up.
    uint8_t mask[256], table[512];
    memset(mask, 0, sizeof mask);
    memset(table, 77, sizeof table);
    mask[small[20 * 64 + 10]] = 1;
    reset();
    memset(pixels, 9, sizeof pixels);
    texture_draw_mask_dest(&st, &b, rect, mask, table, 4, rect_uv);
    EXPECT(px(2, 2), 77);
    // No points: nothing, and the buffer is unlocked all the same (the original's way).
    b.locks = 0;
    texture_draw_mask_dest(&st, &b, NULL, mask, table, 4, rect_uv);
    EXPECT(b.locks, 0);

    // The coast fill: 0 in the table draws nothing, 1 the texel shaded, anything else that colour.
    struct game_texture over = {.data = overdata, .pitch = 64, .height = 64};
    uint8_t ctab[256] = {0};
    ctab[1] = 1;
    reset();
    texture_draw_coast(&st, &b, rect, z0, &over, ctab, 4, rect_uv);
    EXPECT(px(3, 3), small[21 * 64 + 11]);
    ctab[1] = 99;
    reset();
    texture_draw_coast(&st, &b, rect, z0, &over, ctab, 4, rect_uv);
    EXPECT(px(3, 3), 99);

    // Cutting a texture out of the buffer: 6 by 4 pixels from (2, 3), row after row, with the
    // corner tables for a four-point and an eight-point polygon.
    {
        game_mem_get = malloc;
        game_free = free;
        struct game_texture cut = {0};
        reset();
        for (int i = 0; i < PITCH * H; i++) pixels[i] = (uint8_t)i;
        EXPECT(texture_extract_2(&cut, &b, 2, 3, 6, 4, NULL), 0);
        EXPECT(cut.pitch, 6), EXPECT(cut.height, 4), EXPECT(cut.ready, 0);
        EXPECT(cut.data[0], 3 * PITCH + 2), EXPECT(cut.data[6], 4 * PITCH + 2), EXPECT(cut.data[23], 6 * PITCH + 7);
        EXPECT(cut.uv4[2], 5), EXPECT(cut.uv4[5], 3), EXPECT(cut.uv4[7], 3), EXPECT(cut.uv4[0], 0);
        EXPECT(cut.uv8[2], 2), EXPECT(cut.uv8[7], 1), EXPECT(cut.uv8[8], 5), EXPECT(cut.uv8[9], 3), EXPECT(cut.uv8[15], 1);
        EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
        // By rectangle, over what is there (freed); a missing rectangle or buffer is 0x10, a
        // rectangle past the buffer 3 (with the old pixels gone), a corner outside 7.
        int32_t rect[4] = {0, 0, 4, 2};
        EXPECT(texture_extract(&cut, &b, rect, NULL), 0);
        EXPECT(cut.pitch, 4), EXPECT(cut.height, 2), EXPECT(cut.data[4], PITCH);
        EXPECT(texture_extract(&cut, &b, NULL, NULL), 0x10), EXPECT(texture_extract_2(&cut, NULL, 0, 0, 1, 1, NULL), 0x10);
        EXPECT(texture_extract_2(&cut, &b, 10, 0, 8, 2, NULL), 3), EXPECT(cut.data == NULL, 1), EXPECT(cut.pitch, 0);
        EXPECT(texture_extract_2(&cut, &b, W, 0, -2, 1, NULL), 7), EXPECT(cut.pitch, -2);
        // Pixels that belong to a pool are not freed.
        static uint8_t pool_block[16];
        struct game_texture pooled = {.data = pool_block, .ready = 1};
        EXPECT(texture_extract_2(&pooled, &b, 20, 0, 1, 1, NULL), 3), EXPECT(pooled.data == pool_block, 1);
    }

    if (failures) return 1;
    puts("engine_texture ok");
    return 0;
}
