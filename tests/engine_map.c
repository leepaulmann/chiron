// Unit test for the reimplemented map queries, on a small made-up map: no
// game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/map.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static uint8_t abstract[4], *abstract_ptr = abstract;
static int32_t width = W, height = H, half_width = W / 2, seed, sea_level, abstract_width = 4, abstract_height = 2;
static uint32_t flat;
static int32_t elev_detail[11], alt_natural_table[11], goody_mult[2] = {5, 3};
static int32_t landmark_count;
static struct landmark landmarks[LANDMARK_COUNT];
static struct base bases[4];
static int32_t base_count;
static struct continent continents[REGION_COUNT];
static struct faction_data factions[FACTION_COUNT];
static uint32_t rules;
static int32_t turn;
// The 8 neighbours clockwise from north-east, then the square itself; the
// radius table starts with the square and then the same 8, and the rest
// lies far off the map.
static int32_t near_x[9] = {1, 2, 1, 0, -1, -2, -1, 0, 0}, near_y[9] = {-1, 0, 1, 2, 1, 0, -1, -2, 0};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static int32_t radius_count[9] = {1, 9, 25, 49, 81, 121, 169, 225, 289};
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

static void put(int x, int y, int alt, int region) {
    struct map_tile *t = at(x, y);
    t->climate = (uint8_t)(alt << 5 | (t->climate & 0x1f));
    t->region = (uint8_t)region;
}

int main(void) {
    game_map = &tiles_ptr;
    game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_map_seed = &seed, game_sea_level = &sea_level;
    game_elev_detail = elev_detail, game_alt_natural = alt_natural_table, game_goody_mult = goody_mult;
    game_abstract_map = &abstract_ptr, game_abstract_width = &abstract_width, game_abstract_height = &abstract_height;
    game_landmark_count = &landmark_count, game_landmarks = landmarks;
    game_bases = bases, game_base_count = &base_count;
    game_continents = continents, game_factions = factions, game_rules = &rules, game_turn = &turn;
    game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_count = radius_count;
    radius_x[0] = radius_y[0] = 0;
    for (int i = 1; i < RADIUS_SQUARES; i++) {
        radius_x[i] = i < 9 ? near_x[i - 1] : 100;
        radius_y[i] = i < 9 ? near_y[i - 1] : 100;
    }
    for (int i = 0; i < 11; i++) elev_detail[i] = 10 * i, alt_natural_table[i] = 10 * i;

    // Coordinates and distances.
    EXPECT(on_map(0, 0), 1);
    EXPECT(on_map(7, 5), 1);
    EXPECT(on_map(8, 0), 0);
    EXPECT(on_map(-1, 0), 0);
    EXPECT(on_map(0, 6), 0);
    EXPECT(xrange(-1), 7);
    EXPECT(xrange(8), 0);
    EXPECT(xrange(3), 3);
    EXPECT(bad_reg(0), 1);
    EXPECT(bad_reg(63), 1);
    EXPECT(bad_reg(64), 1);
    EXPECT(bad_reg(65), 0);
    EXPECT(bad_reg(127), 1);
    EXPECT(x_dist(1, 7), 2);  // around the back
    EXPECT(vector_dist_offset(2, 0), 1);
    EXPECT(vector_dist_offset(2, 2), 2);
    EXPECT(vector_dist_offset(3, -1), 2);
    EXPECT(vector_dist_offset(4, 0), 3);
    EXPECT(vector_dist(1, 0, 7, 2), 2);
    EXPECT(radius_move_offset(2, 0, 9), 2);
    EXPECT(radius_move_offset(2, 0, 2), -1);
    EXPECT(radius_move_offset(0, 0, 1), 0);
    EXPECT(radius_move(7, 2, 1, 2, 9), 2);  // east, around the back
    EXPECT(compass_move(1, 2, 3, 2), 1);
    EXPECT(compass_move(3, 2, 1, 2), 5);
    EXPECT(compass_move(2, 2, 2, 2), 8);
    EXPECT(compass_move(2, 2, 5, 0), 0);  // north-east
    flat = 1;
    EXPECT(xrange(-1), -1);
    EXPECT(x_dist(1, 7), 6);
    EXPECT(radius_move(7, 2, 1, 2, 9), 2);  // radius_move wraps even so
    flat = 0;

    // The scene: land at altitude 3 in region 5, with a base at (2, 2) and
    // the sea to its north-east and east.
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) put(x, y, 3, 5), at(x, y)->owner_site = 0xf;
    put(2, 2, 4, 5);
    at(2, 2)->bits = MAP_BASE, at(2, 2)->owner_site = 1, at(2, 2)->contour = 100;
    at(2, 2)->visibility = 1 << 2;
    at(2, 2)->climate |= 0x8;  // moist
    put(3, 1, 2, 70);
    at(3, 1)->bits = MAP_UNIT, at(3, 1)->owner_site = 2;
    put(4, 2, 1, 71);

    EXPECT(alt_at(2, 2), 4);
    EXPECT(alt_detail_at(2, 2), 100);
    EXPECT(region_at(3, 1), 70);
    EXPECT(bit_at(2, 2), MAP_BASE);
    EXPECT(is_ocean(4, 2), 1);
    EXPECT(is_ocean(2, 2), 0);
    EXPECT(base_who(2, 2), 1);
    EXPECT(veh_who(2, 2), -1);
    EXPECT(veh_who(3, 1), 2);
    EXPECT(anything_at(3, 1), 2);
    at(5, 5)->bits = MAP_UNIT;  // no owner recorded
    EXPECT(anything_at(5, 5), -1);
    abstract[1 + 1 * 2] = 9;
    EXPECT(abstract_at(2, 1), 9);
    EXPECT(is_known(2, 2, 2), 1);
    EXPECT(is_known(2, 2, 1), 0);
    factions[1].flags = FACTION_FLAG_MAP_REVEALED;
    EXPECT(is_known(2, 2, 1), 1);
    factions[1].flags = 0;

    EXPECT(crappy(2, 2), 1);  // moist, but flat
    at(0, 0)->rocky_lock = 0x80;
    EXPECT(crappy(0, 0), 2);  // arid and rocky
    at(1, 1)->climate |= 0x10;
    at(1, 1)->rocky_lock = 0x40;
    EXPECT(crappy(1, 1), 0);  // rainy and rolling

    EXPECT(alt_natural(2, 2), 6);
    at(1, 3)->contour = 25;
    EXPECT(alt_natural(1, 3), 2);
    sea_level = 10;
    EXPECT(alt_natural(1, 3), 1);
    sea_level = 0;
    at(2, 2)->contour = 40;
    EXPECT(elev_at(2, 2), (40 - 30) * 50 + 10);  // at its altitude's floor
    put(0, 2, 4, 5);
    at(0, 2)->contour = 41;
    EXPECT(elev_at(0, 2), (41 - 30) * 50 + (217 * 2) % 50);
    at(0, 2)->contour = 200;
    EXPECT(elev_at(0, 2), 3500);
    at(2, 2)->contour = 100;

    EXPECT(minerals_at(3, 0), 2);  // poles
    EXPECT(minerals_at(2, 5), 2);
    EXPECT(minerals_at(2, 2), 1);  // altitude 4, pattern 2

    put(0, 4, 4, 5);
    at(0, 4)->bits = MAP_RESOURCE | MAP_NUTRIENT_RESOURCE | MAP_MINERAL_RESOURCE;
    EXPECT(bonus_at(0, 4, 0), 2);
    at(0, 4)->bits = MAP_RESOURCE | MAP_ENERGY_RESOURCE | MAP_NUTRIENT_RESOURCE;
    EXPECT(bonus_at(0, 4, 0), 3);
    EXPECT(bonus_at(2, 4, 0), 0);  // no seed, no mark

    at(5, 1)->bits = MAP_SUPPLY_POD;
    EXPECT(goody_at(5, 1), 1);
    at(5, 1)->bits |= MAP_POD_TAKEN;
    EXPECT(goody_at(5, 1), 0);
    at(5, 1)->bits = 0x4000000;
    rules = RULES_NO_UNITY_SCATTERING;
    EXPECT(goody_at(5, 1), 2);
    rules = 0;
    EXPECT(goody_at(5, 1), 0);

    // Around a square.
    EXPECT(is_coast(2, 2, 0), 1);
    EXPECT(is_coast(6, 2, 0), 6);  // the sea is west, 6th in the radius table
    EXPECT(is_coast(0, 4, 0), 0);
    landmarks[0] = (struct landmark){3, 1, "Sea"};
    landmark_count = 1;
    EXPECT(find_landmark(2, 2, 1), 0);
    EXPECT(find_landmark(2, 2, 0), -1);
    at(4, 4)->bits2 = 0x01000000;
    EXPECT(near_landmark(3, 3), 1);
    EXPECT(near_landmark(0, 0), 0);
    EXPECT(zoc_any(2, 2, 1), 3);
    EXPECT(zoc_veh(2, 2, 1), 3);
    EXPECT(zoc_any(2, 2, 2), 0);  // its own unit
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(zoc_any(2, 2, 1), 0);
    EXPECT(zoc_veh(2, 2, 1), 0);
    factions[1].diplo_status[2] = 0;

    // Regions and coasts. Region 70's shore touches land regions 5 and 6.
    continents[70].sea_coasts[0] = 1 << 5 | 1 << 6;
    EXPECT(sea_coast(70, 5), 1);
    EXPECT(sea_coast(70, 69), 1);  // 69 is taken modulo 64
    EXPECT(sea_coast(70, 7), 0);
    continents[10].sea_coasts[0] = 1 << 5;
    continents[11].sea_coasts[0] = 1 << 5;
    EXPECT(sea_coasts(5), 2);

    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1};
    bases[1] = (struct base){.x = 6, .y = 2, .faction = 2};
    base_count = 2;
    EXPECT(base_on_sea(0, 70), 1);
    EXPECT(base_on_sea(0, 6), 1);  // also taken modulo 64
    EXPECT(base_on_sea(0, 63), 0);
    EXPECT(base_on_sea(1, 70), 0);
    EXPECT(base_on_sea(1, 71), 1);
    EXPECT(base_coast(0), 70);  // found north-east, then skipped the sea to the east
    EXPECT(base_coast(1), 71);
    EXPECT(port_to_port(0, 1), 1);
    EXPECT(port_to_coast(0, 5), 1);  // the base's own region
    continents[12].sea_coasts[0] = 1 << 6;  // touches region 70 (6 modulo 64)
    EXPECT(port_to_coast(0, 12), 1);
    EXPECT(port_to_coast(0, 13), 0);
    EXPECT(transport_base(0), 0);  // its sea touches only region 12
    continents[13].sea_coasts[0] = 1 << 6;
    EXPECT(transport_base(0), 1);
    EXPECT(naval_base(0), 1);
    bases[1].faction = 1;
    EXPECT(naval_base(0), 0);  // the other port is its own

    if (failures) return 1;
    puts("PASS");
    return 0;
}
