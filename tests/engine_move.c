// Unit test for units' orders and the AI's courses, on a small made-up map
// with stand-ins for the game's step (order_veh) and messages: no game
// files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/move.h"

enum { W = 10, H = 8, AREA = W / 2 * H };
static struct map_tile tiles[AREA], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area = AREA;
static uint32_t flat = 1, state;
static int16_t queue_x[AREA], queue_y[AREA];
static int32_t path_map[AREA];
static struct game_path path = {path_map, queue_x, queue_y};
// The 8 neighbours clockwise from north-east; the radius table starts with
// the square and then the same 8, and the rest lies off this map.
static int32_t near_x[8] = {1, 2, 1, 0, -1, -2, -1, 0}, near_y[8] = {-1, 0, 1, 2, 1, 0, -1, -2};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], radius_count[9] = {1, 9, 9, 9, 9, 9, 9, 9, 9};
static struct unit_proto protos[4];
static struct rules_chassis chassis[3];
static struct rules_weapon weapons[2];
static struct rules_basic rules;
static struct faction_data factions[FACTION_COUNT];
static struct faction_def faction_defs[FACTION_COUNT];
static struct veh vehs[4];
static struct base bases[3];
static int32_t veh_count, base_count, net, turn, projects[SP_COUNT], current_faction = 1, control_turn, reporting;
static int32_t tectonic[FACTION_COUNT], *net_ref, sunspot, map_seed, goody_mult[2];
static uint32_t game_rules_;
static uint8_t map_win[0x1dd80];
static void *map_wins[1] = {map_win};
static uint8_t humans, achieved[TECH_COUNT];
static char battle_text[256], road_done[] = "ROAD", tube_done[] = "TUBE";
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
static void pump(void) {}

// Stand-ins: the step moves the unit's coordinates; the rest records.
static int steps, last_dir, terraforms, last_terraform, says, pops, contiguous_answer;
static const char *last_pop;
static int order_veh_(int veh, int dir, int type) {
    (void)type;
    steps++, last_dir = dir;
    vehs[veh].x = (int16_t)(vehs[veh].x + near_x[dir]), vehs[veh].y = (int16_t)(vehs[veh].y + near_y[dir]);
    return 1;
}
static int action_terraform_(int veh, int terraform, int unk) {
    (void)unk;
    terraforms++, last_terraform = terraform;
    vehs[veh].order = (uint8_t)(terraform + 4);
    return 0;
}
static void say_loc_(char *buf, int x, int y, int a, int b, int c) {
    (void)a, (void)b, (void)c;
    says++;
    sprintf(buf, "(%d, %d)", x, y);
}
static int parse_says_(int slot, char *text, int gender, int plural) {
    (void)slot, (void)text, (void)gender, (void)plural;
    return 0;
}
static void __attribute__((thiscall)) netmsg_pop_(void *netmsg, char *label, int delay, int unk, char *sound) {
    (void)netmsg, (void)delay, (void)unk, (void)sound;
    pops++, last_pop = label;
}
static int contiguous_(int faction, int other, int region, int by_sea) {
    (void)faction, (void)other, (void)region, (void)by_sea;
    return contiguous_answer;
}

static void land(void) {
    for (int i = 0; i < AREA; i++)
        tiles[i] = (struct map_tile){.climate = 3 << 5, .region = 1, .owner_site = 0xf, .territory = -1};
    veh_count = base_count = 0;
    path.x_dst = path.y_dst = -1;
}
static void sea(int x0) {  // sea west of x0
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < x0; x += 2) at(x, y)->climate = 1 << 5, at(x, y)->region = 0x41;
}
static void put_unit(int v, int x, int y, int faction, int proto) {
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .proto = (int16_t)proto, .faction = (uint8_t)faction,
                           .home_base = -1, .next_in_stack = -1, .prev_in_stack = -1};
    at(x, y)->bits |= MAP_UNIT;
    at(x, y)->owner_site = (uint8_t)faction;
    if (veh_count <= v) veh_count = v + 1;
}
static void put_base(int b, int x, int y, int faction) {
    bases[b] = (struct base){.x = (int16_t)x, .y = (int16_t)y, .faction = (uint8_t)faction};
    at(x, y)->bits |= MAP_BASE;
    at(x, y)->owner_site = (uint8_t)faction;
    if (base_count <= b) base_count = b + 1;
}

static void test_patrol(void) {
    land();
    for (int y = 0; y < H; y++) at(8 + (y & 1), y)->region = 2;
    put_unit(0, 1, 3, 1, 0);
    EXPECT(valid_patrol(0, 5, 3), 1);
    EXPECT(valid_patrol(0, 1, 3), 0);  // its own square
    EXPECT(valid_patrol(0, -1, 3), 0);
    EXPECT(valid_patrol(0, 9, 3), 0);  // another continent
    // A ship on its sea; an air unit in reach (with no base 0 at either end
    // both count as landing places: the original's test of base_at()).
    sea(6);
    put_unit(1, 2, 2, 1, 1);
    EXPECT(valid_patrol(1, 4, 4), 1);
    EXPECT(valid_patrol(1, 7, 3), 0);
    put_unit(2, 7, 3, 1, 2);
    EXPECT(valid_patrol(2, 1, 1), 1);
    // One move left and a turn aloft: a square away is in reach, but not
    // from base 0's square (no landing place, to the original), only from
    // another base's.
    land();
    put_unit(2, 7, 3, 1, 2);
    vehs[2].movement_turns = 1, vehs[2].moves_spent = 27;
    put_base(0, 7, 3, 1);
    EXPECT(valid_patrol(2, 8, 4), 0);
    put_base(1, 7, 3, 1);
    bases[0].x = 1;
    EXPECT(valid_patrol(2, 8, 4), 1);

    land();
    put_unit(0, 1, 3, 1, 0);
    vehs[0].waypoint_count = 1, vehs[0].waypoint_x[0] = 3, vehs[0].waypoint_y[0] = 3;
    action_patrol(0, 7, 5);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].waypoint_count, 2);
    EXPECT(vehs[0].waypoint_x[1], 7);
    EXPECT(vehs[0].waypoint_y[1], 5);
    EXPECT(vehs[0].waypoint_x[2], 1);
    EXPECT(vehs[0].waypoint_y[2], 3);
    EXPECT(vehs[0].state, 0x1000200);
    action_patrol(0, 1, 3);  // not valid: nothing changes
    EXPECT(vehs[0].waypoint_count, 2);
}

static void test_course(void) {
    // Sea west of x = 7; land (region 2) east of it with a base at (7, 3).
    land();
    for (int i = 0; i < AREA; i++) tiles[i].region = 2;
    sea(7);
    put_unit(0, 1, 3, 1, 1);
    protos[1].plan = 8;  // a colony pod's turns moving start again
    vehs[0].movement_turns = 4;
    set_course(0, 'c', 3, 5);  // its own sea: straight there
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].status_icon, 'c');
    EXPECT(vehs[0].waypoint_x[0], 3);
    EXPECT(vehs[0].waypoint_y[0], 5);
    EXPECT(vehs[0].movement_turns, 0);
    protos[1].plan = 7;
    // Land: the sea square next to the goal that touches its land.
    put_base(0, 7, 3, 2);
    set_course(0, 'b', 8, 4);
    EXPECT(vehs[0].waypoint_x[0], 6);
    EXPECT(vehs[0].waypoint_y[0], 4);
    EXPECT(vehs[0].status_icon, 'b');
    // None near: the orders stay.
    set_course(0, 'x', 9, 7);
    EXPECT(vehs[0].waypoint_x[0], 6);
    EXPECT(vehs[0].status_icon, 'b');
}

static void test_home(void) {
    land();
    put_unit(0, 1, 3, 1, 0);
    put_base(0, 7, 3, 1);
    put_base(1, 3, 3, 2);
    EXPECT(action_home(0, 0), 1);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].waypoint_x[0], 7);
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(action_home(0, 0), 1);
    EXPECT(vehs[0].waypoint_x[0], 3);
    factions[1].diplo_status[2] = 0;
    // A negative even type only asks; odd types are for air units with a
    // range.
    vehs[0].order = 0;
    EXPECT(action_home(0, -2), 1);
    EXPECT(vehs[0].order, 0);
    EXPECT(action_home(0, -1), 0);
    // An air unit on alert: its post, though further, as it is in reach.
    put_base(2, 3, 5, 1);
    put_unit(1, 1, 3, 1, 2);
    vehs[1].state = 0x200, vehs[1].order_auto_type = 0xb, vehs[1].waypoint_x[1] = 7, vehs[1].waypoint_y[1] = 3;
    EXPECT(action_home(1, 0), 1);
    EXPECT(vehs[1].waypoint_x[0], 7);
    vehs[1].state = 0;
    EXPECT(action_home(1, 0), 1);
    EXPECT(vehs[1].waypoint_x[0], 3);
    EXPECT(vehs[1].waypoint_y[0], 5);
    // Home already: the AI's unit is done for the turn.
    vehs[0].x = 7;
    EXPECT(action_home(0, 0), 1);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].moves_spent, 3);
    // No base on its continent.
    land();
    put_unit(0, 1, 3, 1, 0);
    EXPECT(action_home(0, 0), 0);
}

static void test_go_to(void) {
    land();
    put_unit(0, 1, 3, 1, 0);
    // In the direction kept: there, the order ends.
    vehs[0].order = 0x19, vehs[0].waypoint_x[0] = 1, vehs[0].waypoint_x[1] = 3, vehs[0].waypoint_y[1] = 3;
    action_go_to(0);
    EXPECT(last_dir, 1);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].order, 0);
    // A patrol's leg: the next waypoint moves down.
    land();
    put_unit(0, 1, 3, 1, 0);
    vehs[0].order = 0x18, vehs[0].state = 0x3000200, vehs[0].waypoint_count = 1;
    vehs[0].waypoint_x[0] = 3, vehs[0].waypoint_y[0] = 3, vehs[0].waypoint_x[1] = 1, vehs[0].waypoint_y[1] = 3;
    action_go_to(0);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].waypoint_x[0], 1);
    EXPECT(vehs[0].waypoint_count, 0);
    EXPECT(vehs[0].state, 0x1000000);
    // There already: no step, no order.
    int n = steps;
    vehs[0].waypoint_x[0] = 3;
    action_go_to(0);
    EXPECT(steps, n);
    EXPECT(vehs[0].order, 0);
}

static void test_road_to(void) {
    land();
    put_unit(0, 1, 3, 1, 3);
    vehs[0].order = 0x1b, vehs[0].waypoint_x[0] = 5, vehs[0].waypoint_y[0] = 3;
    action_road_to(0);  // a road first, keeping the order
    EXPECT(terraforms, 1);
    EXPECT(last_terraform, 5);
    EXPECT(vehs[0].order, 0x1b);
    at(1, 3)->bits |= MAP_ROAD;
    int n = steps;
    action_road_to(0);  // then on
    EXPECT(steps, n + 1);
    EXPECT(vehs[0].x, 3);
    // Tube: with the tech, the work starts; without, on it goes.
    at(3, 3)->bits |= MAP_ROAD;
    vehs[0].order = 0x1c;
    achieved[5] = 1 << 1;
    action_road_to(0);
    EXPECT(terraforms, 2);
    EXPECT(vehs[0].order, 0x1c);
    achieved[5] = 0;
    action_road_to(0);
    EXPECT(vehs[0].x, 5);
    // At the goal: done, and the player told.
    at(5, 3)->bits |= MAP_ROAD;
    vehs[0].order = 0x1b;
    action_road_to(0);
    EXPECT(vehs[0].order, 0);
    EXPECT(says, 1);
    EXPECT(pops, 1);
    EXPECT(last_pop == road_done, 1);
    // A goal off the map ends it.
    vehs[0].order = 0x1b, vehs[0].waypoint_x[0] = -1;
    action_road_to(0);
    EXPECT(vehs[0].order, 0);
}

static void test_passengers(void) {
    land();
    put_base(0, 5, 3, 1);
    factions[1].ai_growth = 3;
    for (int i = 0; i < GOAL_COUNT; i++) factions[1].goals[i].type = -1;
    assemble_passengers(0, 3, 2);
    EXPECT(bases[0].state & 1, 1);
    EXPECT(bases[0].ai_plan, 2);
    EXPECT(factions[1].goals[0].type, 8);
    EXPECT(factions[1].goals[0].priority, 2);
    EXPECT(factions[1].goals[1].type, 2);
    EXPECT(factions[1].goals[3].type, 0xb);
    // Type 8 without the region's plan: the other goals only.
    for (int i = 0; i < GOAL_COUNT; i++) factions[1].goals[i].type = -1;
    assemble_passengers(0, 8, 2);
    EXPECT(factions[1].goals[0].type, 2);
}

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_flat = &flat, game_state = &state;
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_radius_x = radius_x, game_radius_y = radius_y;
    game_radius_count = radius_count;
    for (int i = 0; i < 8; i++) radius_x[1 + i] = near_x[i], radius_y[1 + i] = near_y[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 4 * W, radius_y[i] = 0;
    game_do_all_non_input = pump;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_rules_basic = &rules;
    game_factions = factions, game_faction_defs = faction_defs, game_vehs = vehs, game_veh_count = &veh_count;
    game_bases = bases, game_base_count = &base_count, game_human_factions = &humans, game_net_multiplayer = &net;
    game_secret_projects = projects, game_turn = &turn, game_tectonic_detonations = tectonic;
    game_current_faction = &current_faction, game_control_turn = &control_turn, game_net_veh_ref = &net_ref;
    game_sunspot_duration = &sunspot, game_rules = &game_rules_, game_map_seed = &map_seed, game_goody_mult = goody_mult;
    game_map_wins = map_wins;
    game_tech_achieved = achieved, game_base_bits_reporting = &reporting, game_path = &path;
    game_battle_text = battle_text, game_text_road_to_done = road_done, game_text_tube_to_done = tube_done;
    game_order_veh = order_veh_, game_action_terraform = action_terraform_, game_say_loc = say_loc_;
    game_parse_says = parse_says_, game_netmsg_pop = netmsg_pop_, game_contiguous = contiguous_;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    static struct rules_tech techs[TECH_COUNT];
    game_tech = techs;
    static struct rules_terraform terraform[TERRAFORM_COUNT];
    game_terraform = terraform;
    terraform[10].preq_tech = 4, terraform[6].preq_tech = 5;
    rules.move_rate_roads = 3, rules.tech_road_fungus = TECH_NONE;
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    chassis[1] = (struct rules_chassis){.speed = 4, .triad = TRIAD_SEA, .cargo = 4};
    chassis[2] = (struct rules_chassis){.speed = 8, .triad = TRIAD_AIR, .range = 2};
    weapons[1].offense = 1;
    for (int i = 0; i < 3; i++) protos[i] = (struct unit_proto){.chassis = (uint8_t)i, .weapon = 1, .plan = 1, .reactor = 1};
    protos[3] = (struct unit_proto){.chassis = 0, .weapon = 0, .plan = 9, .reactor = 1};
    test_patrol();
    test_course();
    test_home();
    test_go_to();
    test_road_to();
    test_passengers();
    if (failures) return 1;
    printf("engine_move: ok\n");
    return 0;
}
