// Unit test for the reimplemented map generation passes, on a small made-up
// map with stand-ins for the game functions they call: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/map.h"
#include "../src/engine/mapgen.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static uint8_t *abstract_ptr;
static int32_t width = W, height = H, half_width = W / 2, area = W / 2 * H, seed = 5, sea_level, council;
static uint32_t flat, rules, draw_state;
static int32_t elev_detail[11], alt_natural_table[11], alt_default[11];
static int32_t landmark_count = 3, base_count, veh_count, coverage = 2, orbit = 1;
static int32_t submerged[8], submerged_veh[8];
static int32_t ocean_coverage, brush_total, brush_wanted = 1000, brush_stroke, cloud_cover;
static uint32_t state;
static struct landmark landmarks[LANDMARK_COUNT];
static struct rules_natural natural[LM_COUNT];
static char *names[LM_COUNT] = {[LM_NEXUS] = "Nexus", [LM_FRESH] = "Fresh", [LM_CRATER] = "Crater"};
static int32_t expansion = 1;
static struct continent continents[REGION_COUNT];
static struct faction_data factions[FACTION_COUNT];
static struct rules_worldbuilder worldbuilder;
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
static int bit2_at_test(const struct map_tile *t) { return (t->bits2 & MAP2_LANDMARK(LM_FRESH)) != 0; }

// Stand-ins. alt_set only sets the altitude; the generators count up.
static int alt_set_calls;
static void fake_alt_set(int x, int y, int alt) {
    alt_set_calls++;
    at(x, y)->climate = (uint8_t)((at(x, y)->climate & 0x1f) | alt << 5);
}
static int rand_next;
static int fake_rand(void) { return rand_next++; }
static uint32_t thread_data[6];  // rand()'s state is the word at 0x14
static void *fake_getptd(void) { return thread_data; }
static int random_next, reseeded;
static int fake_random(int min, int max) { return max > min ? min + random_next++ % (max - min) : min; }
static void fake_reseed(int s) { reseeded = s; }
static int pumps;
static void fake_pump(void) { pumps++; }
static int path_calls;
static void __attribute__((thiscall)) fake_path(void *path) {
    (void)path;
    path_calls++;
}
static int fake_base_find(int x, int y) { (void)x, (void)y; return -1; }
static char *__attribute__((thiscall)) fake_strings_get(void *table, char *s) {
    (void)table;
    return s;
}

int main(void) {
    game_map = &tiles_ptr, game_abstract_map = &abstract_ptr;
    game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width, game_map_area = &area;
    game_map_flat = &flat, game_map_seed = &seed, game_sea_level = &sea_level, game_sea_level_council = &council;
    game_elev_detail = elev_detail, game_alt_natural = alt_natural_table, game_alt_natural_default = alt_default;
    game_landmark_count = &landmark_count, game_base_count = &base_count, game_veh_count = &veh_count;
    game_continents = continents, game_factions = factions, game_rules = &rules, game_draw_state = &draw_state;
    game_land_coverage = &coverage, game_planet_orbit = &orbit, game_worldbuilder = &worldbuilder;
    game_base_submerged_count = submerged, game_base_submerged_veh = submerged_veh;
    game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_count = radius_count;
    game_alt_set = fake_alt_set, game_rand = fake_rand, game_getptd = fake_getptd;
    game_random = fake_random, game_random_reseed = fake_reseed, game_do_all_non_input = fake_pump;
    game_path_continents = fake_path, game_base_find = fake_base_find;
    game_ocean_coverage = &ocean_coverage, game_brush_total = &brush_total, game_brush_land_wanted = &brush_wanted;
    game_brush_stroke = &brush_stroke, game_cloud_cover = &cloud_cover, game_state = &state;
    game_landmarks = landmarks, game_natural = natural, game_strings_get = fake_strings_get, game_expansion = &expansion;
    for (int i = 0; i < LM_COUNT; i++) natural[i].name = names[i];
    radius_x[0] = radius_y[0] = 0;
    for (int i = 1; i < RADIUS_SQUARES; i++) {
        radius_x[i] = i < 9 ? near_x[i - 1] : 100;
        radius_y[i] = i < 9 ? near_y[i - 1] : 100;
    }
    // Ring 2: (4, 0) and (0, 4) away, and the diagonals between.
    static const int ring2[][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4}, {2, 2}, {2, -2}, {-2, 2}, {-2, -2},
                                   {3, 1}, {3, -1}, {-3, 1}, {-3, -1}, {1, 3}, {1, -3}, {-1, 3}, {-1, -3}};
    for (int i = 0; i < 16; i++) radius_x[9 + i] = ring2[i][0], radius_y[9 + i] = ring2[i][1];
    for (int i = 0; i < 11; i++) elev_detail[i] = 20 * i, alt_natural_table[i] = 10 * i, alt_default[i] = 10 * i;

    // A blank map is all ocean at altitude 1, with a new seed.
    map_wipe();
    EXPECT(seed, 1);  // random(0, 0x7fff) gave 0
    EXPECT(landmark_count, 0);
    EXPECT(at(3, 1)->climate, 1 << 5);
    EXPECT(at(3, 1)->contour, 20);
    EXPECT(at(3, 1)->owner_site, 0xf);

    // Raising (2, 2) to 4 slopes ring 1 up to 3 and ring 2 up to 2.
    world_alt_set(2, 2, 4, 0);
    EXPECT(alt_at(2, 2), 4);
    EXPECT(alt_at(3, 1), 3);
    EXPECT(alt_at(1, 3), 3);
    EXPECT(alt_at(6, 2), 2);  // four east
    EXPECT(alt_at(5, 5), 1);  // ring 3: no slope down to 1 needed
    EXPECT(submerged_veh[3], -1);
    // Lowering it to 0 lowers ring 1 to at most 1 and ring 2 to at most 2.
    world_alt_set(2, 2, 0, 0);
    EXPECT(alt_at(2, 2), 0);
    EXPECT(alt_at(3, 1), 1);
    EXPECT(alt_at(6, 2), 2);

    // With both, the contour follows the altitude band, and squares with
    // nothing on them lose their owner.
    at(3, 1)->owner_site = 0x13;
    rand_next = 7;
    world_raise_alt(2, 2);  // from 0 to 1
    EXPECT(alt_at(2, 2), 1);
    EXPECT(alt_detail_at(2, 2), 10 + 7);  // band 1 starts at 10, and is 10 wide
    world_raise_alt(3, 1);  // from 1 to 2: ring 1 to at least 1
    EXPECT(alt_at(3, 1), 2);
    EXPECT(at(3, 1)->owner_site, 0x13);  // the raised square keeps its owner
    world_lower_alt(3, 1);
    EXPECT(alt_at(3, 1), 1);
    world_alt_set(3, 1, 4, 1);
    EXPECT(alt_at(2, 2), 3);
    EXPECT(at(2, 2)->owner_site & 0xf, 0xf);

    // The second largest land region must be under half the largest.
    continents[1].tile_count = 100;
    continents[2].tile_count = 49;
    continents[63].tile_count = 1000;  // not a real region
    EXPECT(world_validate(), 1);
    EXPECT(path_calls, 1);
    continents[2].tile_count = 50;
    EXPECT(world_validate(), 0);
    coverage = 1;  // two thirds
    EXPECT(world_validate(), 1);
    continents[2].tile_count = 65;
    EXPECT(world_validate(), 1);
    continents[2].tile_count = 66;  // 100 * 2 / 3
    EXPECT(world_validate(), 0);

    // Polar caps: shore-level contours on the top and bottom rows.
    world_polar_caps();
    EXPECT(alt_detail_at(0, 0), 30);
    EXPECT(alt_detail_at(7, 5), 30);
    EXPECT(alt_detail_at(1, 5), 30);

    // Contours map from the default bands onto the elevation bands, which
    // then become the altitude bands.
    at(4, 4)->contour = 35;  // altitude band 3, halfway
    world_linearize_contours();
    EXPECT(alt_detail_at(4, 4), 70);  // elevation band 3 is 60 to 80
    EXPECT(alt_natural_table[3], 60);
    EXPECT(pumps, H);

    // Temperature. Every setting 1, so each is the map's height, 6; the
    // stand-in generator counts up. (4, 4) is the 19th square, so its rolls
    // are 36 % 12 = 0 and 1: v = 3 - 0 - 4 + 6 = 5, (3 + 2 * 5) / 6 = 2,
    // which gives 3, less one: 2. Its altitude, 2, doesn't cool it.
    for (int i = 0; i < 11; i++) alt_natural_table[i] = 10 * i;
    worldbuilder.solar_energy = worldbuilder.thermal_band = worldbuilder.thermal_deviance = 1;
    worldbuilder.global_warming = 1;
    random_next = 0;
    world_temperature();
    EXPECT(reseeded, 1 + 17);
    EXPECT(alt_at(4, 4), 2);
    EXPECT(at(4, 4)->climate & 7, 2);

    // Analysis: the open land of each region, and every site cleared.
    for (int i = 0; i < W / 2 * H; i++) {
        tiles[i].climate = 4 << 5 | 1 << 3;  // moist land
        tiles[i].region = 7;
        tiles[i].owner_site = 0x51;
    }
    at(0, 0)->climate = 1 << 5;  // one sea square
    world_analysis();
    EXPECT(continents[7].open_terrain, W / 2 * H - 1);
    EXPECT(at(3, 1)->owner_site, 1);

    // Painting. On a blank map, a brush at shore level raises the square and
    // its north-east and south-east neighbours, and marks them region 1.
    map_wipe();
    brush_total = brush_stroke = 0;
    brush(2, 2, ALT_SHORE_LINE);
    EXPECT(alt_at(2, 2), ALT_SHORE_LINE);
    EXPECT(alt_at(3, 1), ALT_SHORE_LINE);
    EXPECT(alt_at(3, 3), ALT_SHORE_LINE);
    EXPECT(region_at(3, 3), 1);
    EXPECT(alt_at(4, 2), 2);  // only sloped
    EXPECT(brush_stroke, 3);
    EXPECT(brush_total, 3);
    // Land of no region yet next to a square stops the brush there.
    map_wipe();
    at(6, 2)->climate = ALT_SHORE_LINE << 5;  // region 0: another continent
    brush(4, 2, 4);
    EXPECT(alt_at(4, 2), 1);  // (6, 2) is its neighbour
    EXPECT(alt_at(5, 1), 1);
    EXPECT(alt_at(5, 3), 1);
    // At shore level, a square with a shore square two east and sea on both
    // sides between stays: raising it would pinch the channel shut.
    map_wipe();
    at(4, 2)->climate = ALT_SHORE_LINE << 5, at(4, 2)->region = 1;
    brush(2, 2, ALT_SHORE_LINE);
    EXPECT(alt_at(2, 2), 2);  // not painted, only sloped by painting (3, 1)
    EXPECT(alt_at(3, 1), ALT_SHORE_LINE);  // its own neighbours differ

    // A walk paints until the stroke reaches the size.
    map_wipe();
    rand_next = 0;
    paint_land(2, 2, ALT_SHORE_LINE, 4);
    EXPECT(brush_stroke >= 4, 1);

    // Hills give up when no site has its neighbourhood high enough.
    map_wipe();
    brush_stroke = -1;
    build_hills(5);
    EXPECT(brush_stroke, -1);

    // Riverbeds: none wanted on a map without rain settings, and old ones
    // are cleared.
    at(2, 2)->bits = MAP_RIVERBED;
    worldbuilder.rivers_base = worldbuilder.rivers_rain_mod = 0;
    world_riverbeds();
    EXPECT(bit_at(2, 2), 0);

    // Landmarks on a given square. The Manifold Nexus marks the square and
    // its neighbours, each with its place in the radius table.
    map_wipe();
    world_temple(4, 2);
    EXPECT(landmark_count, 1);
    EXPECT(strcmp(landmarks[0].name, "Nexus"), 0);
    EXPECT(landmarks[0].x * 10 + landmarks[0].y, 42);
    EXPECT(alt_at(4, 2), ALT_SHORE_LINE);
    EXPECT(at(5, 3)->bits2, MAP2_LANDMARK(LM_NEXUS) | 3u << 24);  // south-east: radius-table square 3
    EXPECT(bit_at(4, 2) & MAP_POD_TAKEN, MAP_POD_TAKEN);
    // The crater: rolling ground of one temperature over a ring of hills.
    map_wipe();
    at(4, 2)->climate |= 2;  // its temperature
    world_crater(4, 2);
    EXPECT(alt_at(4, 2), ALT_SHORE_LINE);
    EXPECT(at(3, 1)->climate & 7, 2);
    EXPECT(at(3, 1)->rocky_lock >> 6, 1);
    EXPECT(at(3, 1)->bits2 >> 24, 7);  // north-west: radius-table square 7
    EXPECT(strcmp(landmarks[0].name, "Crater"), 0);
    // A freshwater sea: the region given, all of it, named in its first row
    // from the south.
    map_wipe();
    landmark_count = 0;
    for (int i = 0; i < W / 2 * H; i++) tiles[i].region = 70;
    at(0, 4)->region = at(2, 4)->region = at(1, 5)->region = 90;
    world_fresh(0, 4);
    EXPECT(bit2_at_test(at(2, 4)), 1);
    EXPECT(bit2_at_test(at(3, 3)), 0);
    EXPECT(landmark_count, 1);
    EXPECT(landmarks[0].y, 5);  // the first row with the region, from the south

    if (failures) return 1;
    puts("PASS");
    return 0;
}
