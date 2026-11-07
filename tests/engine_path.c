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

// ---- The path finder ----------------------------------------------------------

static struct unit_proto protos[3];
static struct rules_chassis chassis[3];
static struct rules_weapon weapons[2];
static struct rules_basic rules;
static struct faction_data factions[FACTION_COUNT];
static struct veh vehs[4];
static int32_t veh_count, base_count, net, sunspot, projects[SP_COUNT], map_seed, goody_mult[2];
static uint32_t game_rules_;
static uint8_t humans, map_win[0x1dd80];
static void *map_wins[1] = {map_win};



static void land(void) {
    for (int i = 0; i < AREA; i++) tiles[i] = (struct map_tile){.climate = 3 << 5, .region = 1, .owner_site = 0xf, .territory = -1};
}
static void put_unit(int v, int x, int y, int faction, int proto) {
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .proto = (int16_t)proto, .faction = (uint8_t)faction,
                           .next_in_stack = -1, .prev_in_stack = -1, .waypoint_x = {-1}, .waypoint_y = {-1},
                           .state = 0x1000200};  // moving on a path: no zones of control, no speed asked
    at(x, y)->bits |= MAP_UNIT;
    at(x, y)->owner_site = (uint8_t)faction;
    if (veh_count <= v) veh_count = v + 1;
}

static void test_find(void) {
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_rules_basic = &rules;
    game_factions = factions, game_vehs = vehs, game_veh_count = &veh_count, game_base_count = &base_count;
    game_human_factions = &humans, game_net_multiplayer = &net, game_sunspot_duration = &sunspot;
    game_secret_projects = projects, game_rules = &game_rules_, game_map_seed = &map_seed, game_goody_mult = goody_mult;
    game_map_wins = map_wins;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    rules.move_rate_roads = 3;
    flat = 1;  // so that west of x = 0 is off the map
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    chassis[1] = (struct rules_chassis){.speed = 4, .triad = TRIAD_SEA};
    chassis[2] = (struct rules_chassis){.speed = 8, .triad = TRIAD_AIR};
    weapons[1].offense = 1;
    for (int i = 0; i < 3; i++) protos[i] = (struct unit_proto){.chassis = (uint8_t)i, .weapon = 1, .plan = 1};
    for (int i = 0; i < 3; i++) radius_x[9 + i] = 2 * (i + 2), radius_y[9 + i] = 0;  // never on this map

    // Open land: east, two squares at a time, is the way.
    land();
    memset(path_map, 0, sizeof(path_map));
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 1, -1), 1);
    EXPECT(path.x_dst, 7);
    EXPECT(path_map[3 / 2 + 3 * (W / 2)], 3);  // (3, 3): two steps from the goal
    EXPECT(path_map[7 / 2 + 3 * (W / 2)], 1);
    // Again for the same goal: the table is kept.
    path_map[0] = 77;
    EXPECT(path_find(&path, 3, 3, 7, 3, 0, 1, 1, -1), 1);
    EXPECT(path_map[0], 77);
    // Next to the goal: straight there.
    EXPECT(path_find(&path, 6, 4, 7, 3, 0, 1, 1, -1), 0);
    // Other flags: a new table.
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 1 | 0x10, -1), 1);
    EXPECT(path_map[0] != 77, 1);
    // Not back the way it came: with unk 5, east (5 ^ 4) costs more, and
    // of the two diagonals that are left the later wins.
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 1, 5), 2);

    // A sea between: land units find no way, but a ship's way along it
    // goes ashore at the goal.
    land();
    for (int y = 0; y < H; y++) at(4 + (y & 1), y)->climate = 1 << 5;
    EXPECT(path_find(&path, 1, 3, 8, 4, 0, 1, 1, -1), -1);
    EXPECT(path_find(&path, 5, 1, 4, 6, 1, 1, 1, -1), 4);  // south-west, down the sea
    EXPECT(path_find(&path, 5, 5, 6, 6, 1, 1, 1, -1), 2);  // ashore at the goal

    // Roads only.
    land();
    for (int x = 1; x <= 7; x += 2) at(x, 3)->bits |= MAP_ROAD;
    at(6, 2)->bits |= MAP_ROAD;
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 2 | 1, -1), 1);
    at(5, 3)->bits &= ~MAP_ROAD;
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 2 | 1, -1), 1);  // the old table, still
    path.x_dst = -1;
    EXPECT(path_find(&path, 1, 3, 7, 3, 0, 1, 2 | 1, -1), -1);

    // A zone of control next to the goal: the original reads the goal's
    // flag before setting it, and only with it set does a square next to
    // the goal cost more from there.
    land();
    veh_count = 0;
    put_unit(0, 6, 2, 2, 0);
    EXPECT(path_zoc_path(&path, 4, 2, 1), 3);
    EXPECT(path_zoc_path(&path, 4, 4, 1), 0);
    EXPECT(path_find_at(&path, 1, 3, 5, 3, 0, 1, 0x41, -1, 0), 1);
    EXPECT(path_map[4 / 2 + 2 * (W / 2)], 2);
    path.x_dst = -1;
    EXPECT(path_find_at(&path, 1, 3, 5, 3, 0, 1, 0x41, -1, 1), 1);
    EXPECT(path_map[4 / 2 + 2 * (W / 2)], 3);  // 3002 at first, then 3 by way of (3, 3)
    EXPECT(path_map[4 / 2 + 4 * (W / 2)], 2);
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(path_zoc_path(&path, 4, 2, 1), 0);
    factions[1].diplo_status[2] = 0;
    // A human sees only the units it can see.
    humans = 1 << 1;
    EXPECT(path_zoc_path(&path, 4, 2, 1), 0);
    vehs[0].visibility = 1 << 1;
    EXPECT(path_zoc_path(&path, 4, 2, 1), 3);
    humans = 0;
    // With a base, none.
    at(4, 2)->bits |= MAP_BASE, at(4, 2)->owner_site = 1;
    EXPECT(path_zoc_path(&path, 4, 2, 1), 0);

    // Moving a unit to its waypoint.
    land();
    veh_count = 0;
    put_unit(0, 1, 3, 1, 0);
    vehs[0].waypoint_x[0] = 7, vehs[0].waypoint_y[0] = 3;
    EXPECT(path_move(&path, 0, 1), 1);
    EXPECT(path.faction, 1);
    vehs[0].waypoint_x[0] = 1;
    EXPECT(path_move(&path, 0, 1), -1);  // there already
    // An air unit takes the step that looks nearest.
    put_unit(1, 1, 3, 1, 2);
    vehs[1].waypoint_x[0] = 1, vehs[1].waypoint_y[0] = 7;
    EXPECT(path_move(&path, 1, 1), 3);
    // Boxed in by others' units: no way, and no more orders or path.
    land();
    veh_count = 0;
    put_unit(0, 1, 3, 1, 0);
    for (int i = 0; i < 8; i++) {
        int x = 1 + near_x[i], y = 3 + near_y[i];
        if (x >= 0) put_unit(1 + (i & 1), x, y, 3, 0);
    }
    vehs[0].waypoint_x[0] = 7, vehs[0].waypoint_y[0] = 3, vehs[0].order = 5;
    EXPECT(path_move(&path, 0, 1), -1);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].state & 0x1000200, 0);

    // A sensor site: own land next to an own base without cover (the
    // map's radius table reaches only the next squares).
    land();
    veh_count = 0;
    for (int i = 0; i < AREA; i++) tiles[i].territory = 1, tiles[i].rocky_lock = 0x80;
    at(6, 4)->bits |= MAP_BASE, at(6, 4)->owner_site = 1;
    int x = 2, y = 4;
    EXPECT(path_sensors(&path, 1, &x, &y), 0);
    EXPECT(x, 6);
    EXPECT(y, 2);
    EXPECT(path.x_dst, -1);
    // Flat rainy ground isn't wanted.
    for (int i = 0; i < AREA; i++) tiles[i].rocky_lock = 0, tiles[i].climate |= 1 << 3;
    x = 2, y = 4;
    EXPECT(path_sensors(&path, 1, &x, &y), 1);
    EXPECT(x, 2);
}

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

    test_find();

    if (failures) return 1;
    printf("engine_path: all tests passed\n");
    return 0;
}
