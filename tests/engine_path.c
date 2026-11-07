// Unit test for the path finder's map work, on a small made-up map: no
// game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/path.h"

enum { W = 10, H = 8, AREA = W / 2 * H };
static struct map_tile tiles[AREA], *tiles_ptr = tiles;
static uint8_t abstract[4], *abstract_ptr = abstract;
static int32_t width = W, height = H, half_width = W / 2, area = AREA, abstract_width = 4, abstract_height = 2;
static uint32_t flat, state;
static struct continent continents[REGION_COUNT];
static int16_t queue_x[AREA], queue_y[AREA];
static int32_t path_map[AREA];
static struct game_path path = {path_map, queue_x, queue_y};
static int pumps;
// The 8 neighbours clockwise from north-east; the radius table starts with
// the square and then the same 8.
static int32_t near_x[8] = {1, 2, 1, 0, -1, -2, -1, 0}, near_y[8] = {-1, 0, 1, 2, 1, 0, -1, -2};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static int failures;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }
static void pump(void) { pumps++; }

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_flat = &flat, game_state = &state, game_continents = continents;
    game_abstract_map = &abstract_ptr, game_abstract_width = &abstract_width, game_abstract_height = &abstract_height;
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_radius_x = radius_x, game_radius_y = radius_y;
    game_do_all_non_input = pump;
    for (int i = 1; i < RADIUS_SQUARES; i++) radius_x[i] = i < 9 ? near_x[i - 1] : 1000, radius_y[i] = i < 9 ? near_y[i - 1] : 0;

    // Sea everywhere, with an island of three squares and one of two.
    for (int i = 0; i < AREA; i++) tiles[i] = (struct map_tile){.climate = 1 << 5, .region = 9, .territory = -1};
    at(2, 2)->climate = at(3, 3)->climate = at(4, 2)->climate = 3 << 5;
    at(7, 5)->climate = at(8, 6)->climate = 3 << 5;
    state = 0x100;
    path_continents(&path);
    EXPECT(at(1, 1)->region, 0x41);  // the first square met: the sea
    EXPECT(at(5, 5)->region, 0x41);
    EXPECT(at(2, 2)->region, 1);
    EXPECT(at(4, 2)->region, 1);
    EXPECT(at(7, 5)->region, 2);
    EXPECT(at(8, 6)->region, 2);
    EXPECT(at(0, 0)->region, 0x7f);  // the top and bottom rows
    EXPECT(at(9, 7)->region, 0x7f);
    EXPECT(continents[1].tile_count, 3);
    EXPECT(continents[2].tile_count, 2);
    EXPECT(continents[0x7f].tile_count, W);
    EXPECT(state & 0x100, 0);  // 3 of 5 land squares is under 4/5
    EXPECT(continents[1].sea_coasts[0], 1 << 1);
    EXPECT(abstract[2], 2);  // (5, 5) has island 2 two squares east
    EXPECT(abstract[0], 0);
    EXPECT(pumps, 4);  // after each region, and at the end

    // Most land on one continent.
    at(6, 4)->climate = 3 << 5;  // joins (7, 5)
    at(9, 3)->climate = 3 << 5;
    at(5, 1)->climate = 3 << 5;
    state = 0;
    path_continents(&path);
    EXPECT(continents[at(7, 5)->region].tile_count, 3);
    EXPECT(state & 0x100, 0);
    at(4, 4)->climate = at(5, 5)->climate = at(5, 3)->climate = 3 << 5;  // all but (9, 3) in one
    continents[3].tile_count = 7;  // a region no longer used keeps its count, which still counts
    path_continents(&path);
    EXPECT(state & 0x100, 0);  // 10 of 18
    continents[3].tile_count = 0;
    path_continents(&path);
    EXPECT(at(2, 2)->region, at(8, 6)->region);
    EXPECT(state & 0x100, 0x100);  // 10 of 11

    // Merging.
    int r = at(2, 2)->region, other = at(9, 3)->region, n = continents[r].tile_count;
    path_merge(&path, other, r);
    EXPECT(at(9, 3)->region, r);
    EXPECT(continents[r].tile_count, n + 1);
    EXPECT(continents[other].tile_count, 0);

    // Territory: the faction's land joined to a square, the square itself
    // marked only when it is reached again from a neighbour.
    for (int i = 0; i < AREA; i++) tiles[i].unk_6 = 0;
    at(2, 2)->territory = at(3, 3)->territory = at(4, 2)->territory = 3;
    at(4, 4)->territory = 2;
    path_territory(&path, 2, 2, 0, 3);
    EXPECT(at(2, 2)->unk_6, 1);
    EXPECT(at(3, 3)->unk_6, 1);
    EXPECT(at(4, 2)->unk_6, 1);
    EXPECT(at(4, 4)->unk_6, 0);
    EXPECT(path.tail, 4);
    path.tail = 99;
    path_territory(&path, 1, 1, 0, 3);  // from the sea: nothing
    EXPECT(path.tail, 99);

    if (failures) return 1;
    printf("engine_path: all tests passed\n");
    return 0;
}
