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

// Stand-ins: the generators count up. Before the game begins (turn 0)
// alt_set() only sets the altitude.
static int32_t turn;
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
static char *__attribute__((thiscall)) fake_strings_get(void *table, char *s) {
    (void)table;
    return s;
}
// For alt_set() during a game.
static struct base bases[2];
static int landmarks_deleted, bases_killed, towers;
static int32_t skip_territory, battle_state, net_game, add_temperature, halted, native_life = 1;
static int32_t erosive_forces, settings[7], rain_base, rain_faction, current_faction = 1, control_turn;
static uint32_t fungus_clears = MAP_FOREST;
static uint8_t human_factions;
static int fake_veh_at(int x, int y) { (void)x, (void)y; return -1; }
static int fake_stack_fix(int v) { return v; }
static void fake_delete_landmark(int x, int y) { (void)x, (void)y; landmarks_deleted++; }
static void fake_base_kill(int base) { (void)base; bases_killed++; }
static int fake_veh_init(int unit, int f, int x, int y) { (void)unit, (void)f, (void)x, (void)y; return towers++; }

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
    game_turn = &turn, game_rand = fake_rand, game_getptd = fake_getptd;
    game_random = fake_random, game_random_reseed = fake_reseed, game_do_all_non_input = fake_pump;
    game_path_continents = fake_path;
    game_ocean_coverage = &ocean_coverage, game_brush_total = &brush_total, game_brush_land_wanted = &brush_wanted;
    game_brush_stroke = &brush_stroke, game_cloud_cover = &cloud_cover, game_state = &state;
    game_landmarks = landmarks, game_natural = natural, game_strings_get = fake_strings_get, game_expansion = &expansion;
    game_bases = bases, game_veh_at = fake_veh_at, game_stack_fix = fake_stack_fix;
    game_delete_landmark = fake_delete_landmark;
    game_base_kill = fake_base_kill, game_veh_init = fake_veh_init;
    game_world_skip_territory = &skip_territory, game_battle_state = &battle_state, game_net_multiplayer = &net_game;
    game_control_turn = &control_turn, game_world_add_temperature = &add_temperature, game_halted = &halted;
    game_native_life = &native_life, game_erosive_forces = &erosive_forces, game_map_settings = settings;
    game_rain_base = &rain_base, game_rain_faction = &rain_faction, game_current_faction = &current_faction;
    game_fungus_clears = &fungus_clears, game_human_factions = &human_factions;
    static int32_t diag_x[5] = {1, 1, -1, -1, 0}, diag_y[5] = {-1, 1, 1, -1, 0};
    game_diag_x = diag_x, game_diag_y = diag_y;
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

    // alt_set() during a game. Raising land may make it rockier: one roll in
    // (rockiness + 4) does.
    map_wipe();
    turn = 5;
    at(2, 2)->climate = 3 << 5, at(2, 2)->rocky_lock = 0;
    rand_next = 8, draw_state = 0;
    alt_set(2, 2, 4);
    EXPECT(alt_at(2, 2), 4);
    EXPECT(at(2, 2)->rocky_lock >> 6, 1);
    EXPECT(at(2, 2)->bits2 & MAP2_CHANGED, MAP2_CHANGED);
    EXPECT(draw_state, DRAW_TERRAIN | DRAW_LANDMARKS);
    EXPECT(landmarks_deleted, 1);
    rand_next = 9;  // 9 % 5: no
    alt_set(2, 2, 5);
    EXPECT(at(2, 2)->rocky_lock >> 6, 1);
    // Lowering it may make it less rocky: all rolls but one in
    // (rockiness + 1) do.
    rand_next = 1;
    alt_set(2, 2, 3);
    EXPECT(at(2, 2)->rocky_lock >> 6, 0);
    // Land sunk under the sea loses its improvements, and what each faction
    // saw of them.
    at(2, 2)->bits = MAP_FOREST | MAP_RIVER | MAP_BASE;
    skip_territory = 1;
    alt_set(2, 2, 1);
    EXPECT(bit_at(2, 2), MAP_BASE | MAP_RIVER);  // 0x7e1377a3 keeps these
    EXPECT(at(2, 2)->bits_seen[3], MAP_BASE | MAP_RIVER);
    // A base there floods: it loses citizens (half, at least 2 to 4) and is
    // given a pressure dome.
    skip_territory = 0, battle_state = 1;
    at(2, 2)->climate = 3 << 5;
    base_count = 1, bases[0].x = 2, bases[0].y = 2, bases[0].faction = 1, bases[0].pop_size = 9;
    rand_next = 3;  // 3 % 3 + 2 = 2 lost, under half
    alt_set(2, 2, 2);
    EXPECT(bases[0].pop_size, 5);
    EXPECT(bases[0].facilities_built[FAC_PRESSURE_DOME / 8] >> FAC_PRESSURE_DOME % 8 & 1, 1);
    EXPECT(submerged[1], 1);
    EXPECT(factions[1].flags & FACTION_FLAG_BASE_SUBMERGED, FACTION_FLAG_BASE_SUBMERGED);
    // With a dome, nothing more happens.
    at(2, 2)->climate = 3 << 5;
    alt_set(2, 2, 2);
    EXPECT(bases[0].pop_size, 5);
    // A base with too few citizens is lost, unless it is its faction's last.
    memset(bases[0].facilities_built, 0, sizeof(bases[0].facilities_built));
    at(2, 2)->climate = 3 << 5, bases[0].pop_size = 2, factions[1].base_count = 1;
    alt_set(2, 2, 2);
    EXPECT(bases[0].pop_size, 1);
    EXPECT(bases_killed, 0);
    memset(bases[0].facilities_built, 0, sizeof(bases[0].facilities_built));
    at(2, 2)->climate = 3 << 5, bases[0].pop_size = 2, factions[1].base_count = 4;
    alt_set(2, 2, 2);
    EXPECT(bases_killed, 1);
    base_count = 0, at(2, 2)->bits &= ~(uint32_t)MAP_BASE, turn = 0;

    // Shorelines: each square's altitude from its contour, then slopes made
    // smooth from the peaks down.
    for (int i = 0; i < W / 2 * H; i++) tiles[i].contour = 15, tiles[i].climate = 0;
    at(4, 2)->contour = 45;
    world_shorelines();
    EXPECT(alt_at(4, 2), 4);
    EXPECT(alt_at(5, 3), 3);
    EXPECT(alt_at(0, 2), 2);  // ring 2, four west
    EXPECT(alt_at(2, 0), 2);

    // Rockiness and resource kinds: the sea gets the energy kind, high land
    // the mineral kind, low dry land neither.
    map_wipe();
    at(2, 2)->climate = 5 << 5, at(4, 2)->climate = 3 << 5;
    world_rocky();
    EXPECT(bit_at(0, 0) & (MAP_ENERGY_RESOURCE | MAP_MINERAL_RESOURCE), MAP_ENERGY_RESOURCE);
    EXPECT(bit_at(2, 2) & (MAP_ENERGY_RESOURCE | MAP_MINERAL_RESOURCE), MAP_MINERAL_RESOURCE);
    EXPECT(bit_at(4, 2) & (MAP_ENERGY_RESOURCE | MAP_MINERAL_RESOURCE), 0);

    // Fungus: with bands wide enough everywhere, every square but rocky land
    // gets it, losing what fungus clears. The noise takes 324 rolls, and each
    // square given fungus one more, for a fungal tower.
    map_wipe();
    native_life = 2, worldbuilder.fungus = 100, expansion = 0;
    at(2, 2)->climate = 3 << 5, at(2, 2)->rocky_lock = 2 << 6;
    at(4, 2)->bits = MAP_FOREST;
    rand_next = 0;
    world_fungus();
    EXPECT(rand_next, 324 + W / 2 * H - 1);
    EXPECT(bit_at(4, 2), MAP_FUNGUS);
    EXPECT(bit_at(2, 2), 0);
    EXPECT(towers, 0);
    expansion = 1;

    // Rivers: from a riverbed down to the sea, marked as river all the way,
    // with no path marks left.
    map_wipe();
    for (int i = 0; i < W / 2 * H; i++) tiles[i].climate = 3 << 5, tiles[i].contour = 40;
    at(2, 2)->contour = 60, at(2, 2)->bits = MAP_RIVERBED;
    at(3, 3)->contour = 50;
    at(4, 4)->contour = 10, at(4, 4)->climate = 1 << 5;
    random_next = 0;
    world_rivers();
    EXPECT(bit_at(2, 2) & MAP_RIVER, MAP_RIVER);
    int path_left = 0, river_squares = 0;
    for (int i = 0; i < W / 2 * H; i++) {
        path_left |= (tiles[i].bits2 & MAP2_RIVER_PATH) != 0;
        river_squares += (tiles[i].bits & MAP_RIVER) != 0;
    }
    EXPECT(path_left, 0);
    EXPECT(river_squares > 1, 1);

    // Rainfall: flat dry land gets none; wind off a sea square carries
    // moisture east along its row. The top and bottom rows get none.
    map_wipe();
    for (int i = 0; i < W / 2 * H; i++) tiles[i].climate = 3 << 5 | 2;
    at(0, 2)->climate = 1 << 5 | 2;
    cloud_cover = 0;
    world_rainfall();
    EXPECT(at(3, 1)->climate >> 3 & 3, 0);
    EXPECT(at(2, 2)->climate >> 3 & 3, 1);
    EXPECT(at(4, 2)->climate >> 3 & 3, 1);
    EXPECT(at(0, 0)->climate >> 3 & 3, 0);
    EXPECT(rain_base, -1);

    if (failures) return 1;
    puts("PASS");
    return 0;
}
