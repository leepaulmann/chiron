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
static struct veh vehs[4];
static int32_t veh_count;
static struct unit_proto protos[3];  // a land, a sea and an air unit
static struct rules_chassis chassis[3];
static uint32_t state, draw_state;
static int32_t net, find_dist;
static uint8_t humans;
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

// Stand-ins for the game functions the map code calls.
static int fake_veh_at(int x, int y) {
    for (int i = 0; i < veh_count; i++)
        if (vehs[i].x == x && vehs[i].y == y) return i;
    return -1;
}
// The nearest base; ties go to the last, as in the game.
static int world_site_stub(int x, int y, int unk) { return 7; }
static int sites_added, site_faction, site_x, site_y;
static void add_site_stub(int faction, int type, int priority, int x, int y) {
    if (!sites_added++) site_faction = faction, site_x = x, site_y = y;  // the first
}
static int at_site_stub(int faction, int type, int x, int y) { return 0; }

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
    game_vehs = vehs, game_veh_count = &veh_count, game_protos = protos, game_chassis = chassis;
    game_state = &state, game_draw_state = &draw_state, game_net_multiplayer = &net;
    game_base_find_dist = &find_dist, game_human_factions = &humans;
    game_veh_at = fake_veh_at;
    for (int i = 0; i < 3; i++) protos[i].chassis = (uint8_t)i, chassis[i].triad = (uint8_t)i;
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

    // Territory: faction 1 holds everything but (1, 5), which faction 3
    // holds, and (2, 4), which faction 2 holds.
    for (int i = 0; i < W / 2 * H; i++) tiles[i].territory = 1;
    at(1, 5)->territory = 3;
    at(2, 4)->territory = 2;
    int base = -5;
    EXPECT(whose_territory(1, 2, 4, &base, 0), -1);  // no commlink
    EXPECT(whose_territory(1, 2, 4, &base, 1), 2);
    EXPECT(base, 0);  // the nearest base in its region
    EXPECT(whose_territory(2, 2, 4, NULL, 0), 2);
    at(2, 4)->territory = 0;  // the natives count as no one
    EXPECT(whose_territory(1, 2, 4, NULL, 1), -1);
    at(2, 4)->territory = 2;
    state = GAME_STATE_OMNISCIENT;
    EXPECT(whose_territory(1, 2, 4, NULL, 0), 2);
    state = 0;
    factions[1].diplo_status[2] = TREATY_COMMLINK | TREATY_UNK_8000000;
    EXPECT(whose_territory(1, 2, 4, NULL, 0), 2);
    EXPECT(base_territory(1, 2, 4), -1);  // neither is human
    humans = 1 << 1;
    EXPECT(base_territory(1, 2, 4), 0);
    factions[1].diplo_status[2] |= TREATY_VENDETTA;
    EXPECT(base_territory(1, 2, 4), -1);
    factions[1].diplo_status[2] = 0;
    EXPECT(base_territory(1, 0, 4), -1);  // its own
    humans = 0;

    EXPECT(coast_or_border(2, 2, 2, 2, 1), 1);  // the sea north-east
    EXPECT(coast_or_border(0, 4, 0, 4, 1), 2);  // faction 2's square east
    at(2, 4)->territory = 1;
    EXPECT(coast_or_border(0, 4, 0, 4, 1), 3);  // faction 3's south-east
    at(1, 5)->territory = 1;
    EXPECT(coast_or_border(0, 4, 0, 4, 1), 0);
    at(2, 4)->territory = 2;
    EXPECT(coast_or_border(0, 4, 3, 1, 1), 1);  // another region given
    EXPECT(coast_or_border(2, 4, 2, 4, 1), 0);  // not its territory

    EXPECT(valid_landmark(0, 4, 1), 1);
    EXPECT(valid_landmark(2, 4, 1), 1);  // faction 2's unknown to it, so base 0, its own, decides
    factions[1].diplo_status[2] = TREATY_COMMLINK | TREATY_UNK_8000000;
    EXPECT(valid_landmark(2, 4, 1), 0);  // faction 2's
    factions[1].diplo_status[2] = 0;
    at(0, 4)->territory = -1;
    EXPECT(valid_landmark(0, 4, 1), 1);  // no one's, and base 0 nearest is its own
    EXPECT(valid_landmark(0, 4, 2), 0);
    at(0, 4)->territory = 1;

    EXPECT(is_sensor(2, 4), 0);
    bases[0].facilities_built[FAC_GEOSYNC_SURVEY_POD / 8] = 1 << (FAC_GEOSYNC_SURVEY_POD % 8);
    EXPECT(is_sensor(2, 4), 2);  // two south of base 0
    EXPECT(is_sensor(3, 3), 0);  // one off diagonally
    at(3, 3)->bits |= MAP_SENSOR;
    EXPECT(is_sensor(3, 3), 1);

    at(4, 4)->bits2 = MAP2_NEXUS;
    at(4, 4)->visibility = 1 << 1;
    EXPECT(has_temple(1), 1);
    EXPECT(has_temple(2), 0);
    at(4, 4)->bits2 = MAP2_NEXUS | 1u << 24;  // not its first square
    EXPECT(has_temple(1), 0);

    // Units: a land unit of faction 1 at base 0, homed at base 1, and a sea
    // unit of faction 2 at (3, 1).
    vehs[0] = (struct veh){.x = 2, .y = 2, .proto = 0, .faction = 1, .home_base = 1, .next_in_stack = -1};
    vehs[1] = (struct veh){.x = 3, .y = 1, .proto = 1, .faction = 2, .home_base = -1, .next_in_stack = -1};
    veh_count = 2;
    EXPECT(convoy(0, 0), 1);  // both bases in region 5
    EXPECT(convoy(0, 1), 0);  // its home
    EXPECT(convoy(1, 0), 0);  // no home
    vehs[0].proto = 1;
    EXPECT(convoy(0, 0), 1);  // a ship: the two ports share a sea
    vehs[0].proto = 2;
    EXPECT(convoy(0, 0), 1);
    vehs[0].proto = 0;

    EXPECT(get_there(0, 6, 2), 1);
    EXPECT(get_there(0, 3, 1), 0);
    EXPECT(get_there(1, 2, 2), 1);  // into base 0's port
    EXPECT(get_there(1, 4, 2), 0);  // another sea
    EXPECT(get_there(1, 3, 1), 1);
    EXPECT(get_there(1, 0, 4), 0);
    continents[5].sea_coasts[0] = 1 << 6;  // region 5's shore touches sea 70
    EXPECT(get_there(1, 0, 4), 1);

    EXPECT(zoc_sea(2, 2, 1), 0);  // the unit is at sea, the square land
    EXPECT(zoc_sea(4, 2, 1), 3);
    EXPECT(zoc_move(4, 2, 1), 3);
    EXPECT(zoc_sea(4, 2, 2), 0);  // its own
    vehs[1].flags = VEH_FLAG_INVISIBLE;
    EXPECT(zoc_sea(4, 2, 1), 0);
    vehs[1].visibility = 1 << 1;
    EXPECT(zoc_sea(4, 2, 1), 3);
    at(4, 2)->bits |= MAP_BASE, at(4, 2)->owner_site = 1;
    EXPECT(zoc_move(4, 2, 1), 0);
    at(4, 2)->bits = 0, at(4, 2)->owner_site = 0xf;
    int zx = -1, zy = -1;
    quick_zoc(4, 2, 1, 0, 0, &zx, &zy);
    EXPECT(zx, 3);
    EXPECT(zy, 1);
    zx = zy = -1;
    quick_zoc(4, 2, 2, 0, 0, &zx, &zy);
    EXPECT(zx, -1);

    // Setters.
    temp_set(0, 0, 13);
    EXPECT(at(0, 0)->climate & 7, 5);
    climate_set(0, 0, 6);
    EXPECT(at(0, 0)->climate >> 3 & 3, 2);
    EXPECT(at(0, 0)->bits2 & MAP2_CHANGED, MAP2_CHANGED);
    EXPECT(draw_state, DRAW_TERRAIN);
    alt_put_detail(0, 0, 0x1ff);
    EXPECT(alt_detail_at(0, 0), 0xff);
    owner_set(0, 0, 0x13);
    site_set(0, 0, 0x2a);
    EXPECT(at(0, 0)->owner_site, 0xa3);
    region_set(0, 0, 9);
    EXPECT(region_at(0, 0), 9);
    at(0, 0)->rocky_lock = 0;
    using_set(0, 0, 0xd);
    EXPECT(at(0, 0)->rocky_lock, 5);
    EXPECT(lock_map(0, 0, 3), 0);
    EXPECT(at(0, 0)->rocky_lock, 3 << 3 | 5);
    EXPECT(lock_map(0, 0, 4), 1);
    EXPECT(lock_map(0, 0, 3), 0);
    unlock_map(0, 0, 4);
    EXPECT(at(0, 0)->rocky_lock, 3 << 3 | 5);
    unlock_map(0, 0, 3);
    EXPECT(at(0, 0)->rocky_lock, 5);
    lock_set(0, 0, 9);
    EXPECT(at(0, 0)->rocky_lock, 1 << 3 | 5);
    rocky_set(0, 0, 2);
    EXPECT(at(0, 0)->rocky_lock, 0x80 | 1 << 3 | 5);
    bit_put(0, 0, 0x30);
    bit_set(0, 0, 0x5, 1);
    bit_set(0, 0, 0x10, 0);
    EXPECT(bit_at(0, 0), 0x25);
    bit2_set(0, 0, MAP2_CHANGED, 0);
    bit2_set(0, 0, 0x8, 1);
    draw_state = 0;
    code_set(0, 0, 7);
    EXPECT(at(0, 0)->bits2, 7u << 24 | 0x8);
    EXPECT(draw_state, DRAW_LANDMARKS);
    synch_bit(0, 0, 2);
    EXPECT(at(0, 0)->bits_seen[1], 0x25);
    synch_bit(0, 0, 0);
    EXPECT(at(0, 0)->bits_seen[0], 0);
    abstract_set(2, 1, 4);
    EXPECT(abstract[1 + 1 * 2], 4);
    site_set(3, 1, 5);
    site_radius(2, 2, 0);
    EXPECT(at(3, 1)->owner_site, 2);  // site cleared, owner kept

    EXPECT(new_landmark(1, 1, "Hill"), 1);
    kill_landmark(2, 2);  // "Sea" at (3, 1) is next to it
    EXPECT(landmark_count, 1);
    EXPECT(strcmp(landmarks[0].name, "Hill"), 0);
    kill_landmark(7, 5);
    EXPECT(landmark_count, 1);
    landmark_count = LANDMARK_COUNT;
    EXPECT(new_landmark(1, 1, "Full"), -1);
    landmark_count = 1;

    // The whole map: units at (2, 2) and (3, 1), bases at (2, 2) and (6, 2).
    at(5, 5)->bits = MAP_UNIT;
    rebuild_vehicle_bits();
    EXPECT(bit_at(5, 5), 0);
    EXPECT(bit_at(3, 1), MAP_UNIT);
    EXPECT(bit_at(2, 2), MAP_BASE | MAP_UNIT);
    EXPECT(at(2, 2)->owner_site & 0xf, 1);
    bit_set(6, 2, MAP_BASE, 0);
    bit_set(2, 2, MAP_BASE, 0);
    at(2, 2)->owner_site = 0xf;
    rebuild_base_bits();
    EXPECT(bit_at(6, 2), MAP_BASE);
    EXPECT(bit_at(2, 2), MAP_BASE | MAP_UNIT);
    EXPECT(at(2, 2)->owner_site & 0xf, 1);
    EXPECT(at(6, 2)->owner_site & 0xf, 1);

    // Territory, on a fresh map: land (region 1) west of x = 6, sea
    // (region 2) east of it; bases at (0, 2) for faction 1 and (4, 4) for
    // faction 2, reaching 1 square (so only their own on land, none by
    // distance at sea).
    static struct rules_basic rules_basic = {.territory_max_dist = 1};
    static int32_t difficulty = 4;
    game_rules_basic = &rules_basic, game_difficulty = &difficulty;
    game_world_site = world_site_stub, game_add_site = add_site_stub, game_at_site = at_site_stub;
    memset(tiles, 0, sizeof tiles), memset(factions, 0, sizeof factions), memset(continents, 0, sizeof continents);
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) put(x, y, x < 6 ? 3 : 1, x < 6 ? 1 : 2), at(x, y)->territory = -1;
    flat = 0, seed = 0, rules = 0, humans = 1 << 1, draw_state = 0;
    base_count = 3;
    bases[0].x = 0, bases[0].y = 2, bases[0].faction = 1;
    bases[1].x = 4, bases[1].y = 4, bases[1].faction = 2;
    bases[2].x = 4, bases[2].y = 2, bases[2].faction = 2;  // 2 squares from the pod at (2, 0)
    at(4, 2)->bits = MAP_BASE;
    at(0, 2)->territory = 1;
    at(0, 2)->climate |= 1 << 3, at(0, 2)->bits = MAP_BASE_RADIUS | MAP_BASE;  // farmable, in a base's radius
    at(4, 4)->bits = MAP_BASE;
    at(2, 0)->owner_site = 0x9f;  // site 9 already
    at(7, 1)->bits = at(2, 0)->bits = MAP_SUPPLY_POD;
    at(7, 1)->visibility = 1 << 1;
    factions[1].flags = FACTION_FLAG_MAP_REVEALED;
    at(5, 5)->visibility = 1 << 2;
    factions[3].flags = FACTION_FLAG_UNK_20000000, at(1, 1)->bits = MAP_POD_TAKEN;
    factions[1].region_total_bases[1] = factions[2].region_total_bases[1] = 1;
    factions[4].region_visible_tiles[5] = 9;
    reset_territory();
    EXPECT(factions[4].region_visible_tiles[5], 0);
    EXPECT(at(0, 2)->territory, 1);
    EXPECT(at(4, 4)->territory, 2);
    EXPECT(at(2, 2)->territory, -1);
    EXPECT(at(6, 4)->territory, 2);  // next to (4, 4)
    EXPECT(at(7, 1)->territory, 1);  // next to (0, 2) around the edge
    EXPECT(at(7, 3)->territory, 1);
    EXPECT(at(6, 2)->territory, 1);
    EXPECT(at(6, 0)->territory, -1);
    EXPECT(at(0, 2)->bits2 & MAP2_CHANGED, 0);
    EXPECT(at(2, 2)->bits2 & MAP2_CHANGED, 0);
    EXPECT(at(4, 4)->bits2 & MAP2_CHANGED, MAP2_CHANGED);
    EXPECT(draw_state, DRAW_TERRAIN);
    EXPECT(factions[1].region_territory_tiles[1], 1);
    EXPECT(factions[1].region_territory_tiles[2], 3);  // (6, 2) too
    EXPECT(factions[2].region_territory_tiles[1], 2);  // and (4, 2)
    EXPECT(factions[2].region_territory_tiles[2], 1);
    EXPECT(factions[0].region_territory_tiles[1], 15);
    EXPECT(factions[1].region_visible_tiles[1], 18);
    EXPECT(factions[1].region_visible_tiles[2], 6);
    EXPECT(factions[2].region_visible_tiles[1], 1);
    EXPECT(factions[3].region_visible_tiles[1], 1);
    EXPECT(factions[1].region_good_tiles[1], 1);
    EXPECT(factions[1].region_good_base_tiles[1], 1);
    EXPECT(at(0, 2)->owner_site >> 4, 0);  // in a base's radius: no site
    EXPECT(at(2, 2)->owner_site >> 4, 7);
    EXPECT(at(2, 0)->owner_site >> 4, 9);
    EXPECT(continents[1].best_site, 9);
    EXPECT(continents[2].best_site, 7);
    EXPECT(factions[1].region_best_site[1], 0);
    EXPECT(factions[2].region_best_site[1], 7);
    EXPECT(factions[1].region_best_site[2], 7);
    EXPECT(factions[0].region_best_site[1], 0);  // nobody's isn't kept
    // Pods: (7, 1) is faction 1's and seen by it; (2, 0) is no one's, and
    // faction 2 (an AI, at a high difficulty) has a base 2 squares off.
    // Faction 1 (human) hasn't seen it.
    EXPECT(continents[1].pods, 1);
    EXPECT(continents[2].pods, 1);
    EXPECT(factions[1].region_territory_pods[2], 1);
    EXPECT(factions[0].region_territory_pods[1], 1);
    EXPECT(sites_added, 2);
    EXPECT(site_faction, 2);
    EXPECT(site_x, 2);
    EXPECT(site_y, 0);
    // A flat map doesn't wrap.
    flat = 1;
    reset_territory();
    EXPECT(at(7, 1)->territory, -1);
    EXPECT(at(7, 1)->bits2 & MAP2_CHANGED, MAP2_CHANGED);

    if (failures) return 1;
    puts("PASS");
    return 0;
}
