// Unit test for units' orders and the AI's courses, on a small made-up map
// with stand-ins for the game's step (order_veh) and messages: no game
// files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/move.h"
#include "../src/engine/veh.h"

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
static int32_t sound_played;

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

// Stand-ins for the actions' drawing, sounds and diplomacy.
static int draws, atrocities, last_atrocity_type, waves, popps;
static void draw_(int x, int y, int unk) { (void)x, (void)y, (void)unk, draws++; }
static void __attribute__((thiscall)) win_veh_(void *win, int veh) { (void)win, (void)veh; }
static void __attribute__((thiscall)) win_loc_(void *win, int x, int y, int unk) { (void)win, (void)x, (void)y, (void)unk; }
static void __attribute__((thiscall)) win_redraw_(void *win) { (void)win; }
static void wave_(int sound) { (void)sound, waves++; }
static int ambience_(int sound) { return (void)sound, 0; }
static void atrocity_(int faction, int other, int type, int unk) {
    (void)faction, (void)other, (void)unk;
    atrocities++, last_atrocity_type = type;
}
static int popp_(char *file, char *label, int flags, char *picture, int unk) {
    (void)file, (void)label, (void)flags, (void)picture, (void)unk;
    return popps++, 0;
}

static void test_terraform_cost(void) {
    land();
    put_base(0, 5, 3, 1);
    // Land at the shore line: 2 squared, twice, by the 2 squares to the
    // faction's base, halved.
    EXPECT(terraform_cost(1, 3, 1), 8);
    // Another faction's big base nearer than the faction's own (4 squares
    // off): by the distance to the faction's base and the other's size
    // (16), over the distance to the other's and the faction's size (1).
    land();
    put_base(0, 9, 3, 1);
    put_base(1, 5, 3, 2);
    bases[1].pop_size = 10;
    EXPECT(terraform_cost(1, 3, 1), 256);
    factions[1].diplo_status[2] = TREATY_PACT;  // not with a pact
    EXPECT(terraform_cost(1, 3, 1), 16);
    factions[1].diplo_status[2] = 0;
    faction_defs[1].rule_flags = 0x2000;  // the terraforming bonus
    EXPECT(terraform_cost(1, 3, 1), 128);
    faction_defs[1].rule_flags = 0;
    // Deep sea with no base: 4 squared, twice, twice at sea and twice more
    // while no one knows tech 39, halved.
    land();
    at(1, 3)->climate = 1 << 5;
    EXPECT(terraform_cost(1, 3, 1), 64);
    achieved[39] = 1;
    EXPECT(terraform_cost(1, 3, 1), 32);
    achieved[39] = 0;
}

static void test_can_terraform(void) {
    land();
    put_base(0, 5, 3, 1);
    EXPECT(can_terraform(1, 5, 3, 0, 0), 0);  // a base
    // Deep sea: raised, with the tech.
    at(1, 3)->climate = 1 << 5;
    EXPECT(can_terraform(1, 1, 3, 0, -1), 0);
    achieved[7] = 1 << 1;
    EXPECT(can_terraform(1, 1, 3, 0, -1), 20);
    // The shelf: a farm first, then a mine for a base that wants minerals.
    at(1, 3)->climate = 2 << 5;
    achieved[8] = 1 << 1;
    EXPECT(can_terraform(1, 1, 3, 0, 0), 4);
    at(1, 3)->bits |= 0x8000;
    EXPECT(can_terraform(1, 1, 3, 0, 0), 0);
    achieved[9] = 1 << 1;
    bases[0].state = 0x2000;
    EXPECT(can_terraform(1, 1, 3, 0, 0), 6);
    // Sea fungus: cleared by the AI, not for a human who hasn't asked.
    at(1, 3)->bits = MAP_FUNGUS;
    EXPECT(can_terraform(1, 1, 3, 0, -1), 0);
    achieved[10] = 1 << 1;
    EXPECT(can_terraform(1, 1, 3, 0, -1), 14);
    humans = 1 << 1;
    EXPECT(can_terraform(1, 1, 3, 0, -1), 0);
    humans = 0;
    for (int i = 7; i <= 10; i++) achieved[i] = 0;
    bases[0].state = 0;
}

static void test_terraform_work(void) {
    land();
    put_unit(0, 1, 3, 1, 3);
    // A road: two turns' work (four parts) at two parts a turn.
    vehs[0].moves_spent = 0;
    EXPECT(action_terraform(0, 5, 0), 1);  // this turn's work counts
    vehs[0].moves_spent = 255;
    EXPECT(action_terraform(0, 5, 0), 2);
    // Another former on the same order in the stack hands over its work.
    put_unit(1, 1, 3, 1, 3);
    vehs[0].next_in_stack = 1, vehs[1].prev_in_stack = 0;
    vehs[1].order = 9, vehs[1].movement_turns = 1;
    vehs[0].movement_turns = 1;
    EXPECT(action_terraform(0, 5, 1), 1);
    EXPECT(at(1, 3)->bits & MAP_ROAD, MAP_ROAD);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[1].order, 0);
    EXPECT(vehs[1].movement_turns, 0);
    EXPECT(vehs[0].movement_turns, 0);
    EXPECT(sound_played, 1);  // the player's former: a sound, once a turn
    EXPECT(waves, 1);
    // Unfinished: the order kept, the work stored.
    vehs[1].prev_in_stack = vehs[0].next_in_stack = -1;
    at(3, 3)->bits = 0;
    vehs[0].x = 3;
    EXPECT(action_terraform(0, 5, 1), 0);
    EXPECT(vehs[0].order, 9);
    EXPECT(vehs[0].movement_turns, 2);
    // In a base the work stops.
    put_base(0, 3, 3, 1);
    EXPECT(action_terraform(0, 0, 1), 0);
    EXPECT(vehs[0].order, 0);
}

static void test_action(void) {
    land();
    put_unit(0, 1, 3, 1, 0);
    vehs[0].order = 0x1a;
    EXPECT(action(0), 0);
    // The AI's go to: a step, and the order ends.
    vehs[0].order = 0x58, vehs[0].waypoint_x[0] = 5, vehs[0].waypoint_y[0] = 3;
    int n = steps;
    EXPECT(action(0), 1);
    EXPECT(steps, n + 1);
    EXPECT(vehs[0].order, 0);
}

static void test_staple(void) {
    land();
    put_base(0, 5, 3, 2);
    bases[0].state = BASE_STATE_DRONE_RIOT, bases[0].faction_former = 3;
    action_staple(0);
    EXPECT(bases[0].state, 0);
    EXPECT(bases[0].nerve_staple_turns, 10);
    EXPECT(bases[0].nerve_staple_count, 1);
    EXPECT(atrocities, 1);
    EXPECT(last_atrocity_type, 1);  // the worse, against its former owner
    // After the eighth time it fails.
    bases[0].nerve_staple_count = 8, bases[0].state = BASE_STATE_DRONE_RIOT;
    action_staple(0);
    EXPECT(bases[0].state, BASE_STATE_DRONE_RIOT);
    EXPECT(bases[0].nerve_staple_turns, 10);
    EXPECT(atrocities, 2);
}

// Stand-ins for the step's (order_veh's) drawing, sounds, windows and the
// calls it makes into parts of the game not reimplemented.
static int scoots, booms, monoliths, goodies;
static int32_t attack_flags, current_veh, moved_quickly, console_flag, zoc_count, fungus_count, moves_seen;
static int32_t native_life, expansion, ref_map_1, lift_x, lift_y, lifted, rebuilt = 1, refs[3], battle_state;
static uint32_t draw_state;
static struct rules_ability abilities[32];
static struct rules_armor armor[2];
static char ability_name[] = "Ability";
static void __attribute__((thiscall)) popup_(void *pop) { (void)pop; }
static int16_t __attribute__((stdcall)) key_state_(int key) { return (void)key, 0; }
static void __attribute__((thiscall)) fx_(void *fx, int id) { (void)fx, (void)id; }
static int __attribute__((thiscall)) focus_(void *console, int x, int y, int faction) {
    return (void)console, (void)x, (void)y, (void)faction, 0;
}
static void scoot_(int veh, int x, int y, int dir, int unk) { (void)veh, (void)x, (void)y, (void)dir, (void)unk, scoots++; }
static void clock_wait_(int ms) { (void)ms; }
static void radius_(int x, int y, int r, int unk) { (void)x, (void)y, (void)r, (void)unk; }
static void boom_(int x, int y, int flags) { (void)x, (void)y, (void)flags, booms++; }
static void log_say_(char *text, int a, int b, int c) { (void)text, (void)a, (void)b, (void)c; }
static void synch_veh_(int veh) { (void)veh; }
static void monolith_(int veh) { (void)veh, monoliths++; }
static int goody_box_(int veh, int goody) { return (void)veh, (void)goody, goodies++, 0; }
static int parse_say_(int slot, int text, int gender, int plural) { return (void)slot, (void)text, (void)gender, (void)plural, 0; }
static int parse_num_(int slot, int value) { return (void)slot, (void)value, 0; }
static int tut_check_(int bit) { return (void)bit, 0; }
static int random_(int min, int max) { return (void)min, max - 1; }  // the worst roll
static void __attribute__((thiscall)) win_base_(void *win, int base) { (void)win, (void)base; }
static void x_pop_(char *label, int unk) { (void)label, (void)unk; }
static void interlude_(int a, char *b, int c, int d) { (void)a, (void)b, (void)c, (void)d; }

// A land unit (design 0, speed 1, three road moves) of faction 1 and
// another faction's on a land map.
static void step_setup(void) {
    land();
    for (int f = 0; f < FACTION_COUNT; f++)
        for (int g = 0; g < FACTION_COUNT; g++) factions[f].diplo_status[g] = 0;
    put_unit(0, 3, 3, 1, 0);
    attack_flags = 0, current_faction = 7, humans = 0;
}

static void test_step(void) {
    // A step east onto open land: moved, a move spent, facing east.
    step_setup();
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(vehs[0].x, 5);
    EXPECT(vehs[0].y, 3);
    EXPECT(vehs[0].moves_spent, 3);
    EXPECT(vehs[0].unk_29, 1);
    EXPECT(vehs[0].state & 0x10004, 0x10004);
    EXPECT(attack_flags, 3);
    // Off the map: nothing, and the order ends.
    step_setup();
    vehs[0].x = 9, vehs[0].order = 0x18;
    EXPECT(order_veh(0, 1, 3), 0);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].iter_count, 1);
    // A stay (any direction past 7): its turn skipped.
    step_setup();
    EXPECT(order_veh(0, -1, 3), 1);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].state & 0x10000, 0);
    // Only seen, not happening: moved nowhere, but counted as done.
    step_setup();
    EXPECT(order_veh(0, 1, 1), 1);
    EXPECT(vehs[0].x, 3);
    // Out of moves: the step fails.
    step_setup();
    vehs[0].moves_spent = 3;
    EXPECT(order_veh(0, 1, 3), 0);
    EXPECT(vehs[0].x, 3);
    // The AI on its way into the territory of a faction it knows, with no
    // pact or vendetta: the order ends at once.
    step_setup();
    for (int i = 0; i < AREA; i++) tiles[i].territory = 2;
    vehs[0].order = 0x18, vehs[0].waypoint_x[0] = 7, vehs[0].waypoint_y[0] = 3;
    factions[1].diplo_status[2] = TREATY_COMMLINK | 0x8000000;
    EXPECT(order_veh(0, 1, 3), 0);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].order, 0);
    factions[1].diplo_status[2] = TREATY_VENDETTA | TREATY_COMMLINK | 0x8000000;
    vehs[0].order = 0x18;
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(vehs[0].x, 5);
    // Zones of control: from a square next to an enemy to another, empty.
    step_setup();
    put_unit(1, 4, 2, 2, 0);
    factions[1].diplo_status[2] = TREATY_COMMLINK, factions[2].diplo_status[1] = TREATY_COMMLINK;
    EXPECT(order_veh(0, 1, 3), 0);
    EXPECT(vehs[0].x, 3);
    // An attack where the defender stands: only its odds weighed for a
    // move not happening (no unit moves).
    // Onto the sea without a transport: no.
    step_setup();
    sea(5);
    vehs[0].x = 5;
    put_unit(0, 5, 3, 1, 0);
    EXPECT(order_veh(0, 5, 3), 0);
    EXPECT(vehs[0].x, 5);
    // Aircraft at the end of their range, out of moves away from a base:
    // lost.
    step_setup();
    put_unit(0, 3, 3, 1, 2);
    vehs[0].moves_spent = (uint8_t)(veh_speed(0, 0) - 1), vehs[0].movement_turns = 1;
    int count = veh_count;
    EXPECT(order_veh(0, 1, 3), 0);
    EXPECT(veh_count, count - 1);
    // A turn aloft counted short of the range's end.
    step_setup();
    put_unit(0, 3, 3, 1, 2);
    vehs[0].moves_spent = (uint8_t)(veh_speed(0, 0) - 1);
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(vehs[0].movement_turns, 1);
    // Short of the square's cost (two moves left of three): the roll (the
    // worst here) fails, and the unit waits for its next turn, slowed;
    // slowed, it goes.
    step_setup();
    vehs[0].moves_spent = 1;
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].state & 0x8000, 0x8000);
    vehs[0].moves_spent = 1;
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(vehs[0].x, 5);
    EXPECT(vehs[0].state & 0x8000, 0);
    // Aboard its faction's transport out at sea: asleep there.
    step_setup();
    sea(5);
    put_unit(0, 5, 3, 1, 0);
    put_unit(1, 3, 3, 1, 1);
    protos[1].carry = 2;
    EXPECT(order_veh(0, 5, 3), 1);
    EXPECT(vehs[0].x, 3);
    EXPECT(vehs[0].order, 1);
    EXPECT(vehs[0].waypoint_x[0], -1);
    // A full one (a land unit already aboard each place): no.
    put_unit(2, 5, 3, 1, 0);
    put_unit(3, 3, 3, 1, 0);
    vehs[3].next_in_stack = 0, vehs[0].prev_in_stack = 3, vehs[0].next_in_stack = 1, vehs[1].prev_in_stack = 0;
    EXPECT(order_veh(2, 5, 3), 0);
    EXPECT(vehs[2].x, 5);
    protos[1].carry = 0;
    // Monoliths and supply pods are visited.
    step_setup();
    at(5, 3)->bits |= MAP_MONOLITH;
    at(5, 5)->bits |= MAP_SUPPLY_POD;
    int m = monoliths, g = goodies;
    EXPECT(order_veh(0, 1, 3), 1);
    EXPECT(monoliths, m + 1);
    vehs[0].moves_spent = 0;
    EXPECT(order_veh(0, 3, 3), 1);
    EXPECT(vehs[0].y, 5);
    EXPECT(goodies, g + 1);
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
    static int32_t find_dist, difficulty, prefs, more_prefs, tut_seen[2], rain_base = -1, rain_faction = -1, gender, plural;
    game_parse_gender = &gender, game_parse_plural = &plural;
    static struct res_info res = {.forest = {1, 2, 0, 0}};
    static char *script = "script";
    game_base_find_dist = &find_dist, game_difficulty = &difficulty, game_preferences = &prefs;
    game_more_preferences = &more_prefs, game_tut_map_seen = tut_seen, game_terraform_sound_played = &sound_played;
    game_rain_base = &rain_base, game_rain_faction = &rain_faction, game_res_info = &res, game_script_file = &script;
    game_draw_tile = draw_, game_draw_tiles = draw_, game_base_win_check_veh = win_veh_, game_base_win_check_loc = win_loc_;
    game_graphic_win_redraw = win_redraw_, game_wave_it = wave_, game_ambience = ambience_, game_atrocity = atrocity_;
    game_popp = popp_;
    static struct rules_tech techs[TECH_COUNT];
    game_tech = techs;
    static struct rules_terraform terraform[TERRAFORM_COUNT];
    game_terraform = terraform;
    terraform[10].preq_tech = 4, terraform[6].preq_tech = 5;
    terraform[16].preq_tech_sea = 7, terraform[0].preq_tech_sea = 8, terraform[2].preq_tech_sea = 9;
    terraform[10].preq_tech_sea = 10, terraform[3].preq_tech_sea = terraform[1].preq_tech_sea = TECH_NONE - 1;
    terraform[5].rate = terraform[6].rate = 2, terraform[0].rate = 4, terraform[5].bit = MAP_ROAD, terraform[0].bit = 0x8000;
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
    test_terraform_cost();
    test_can_terraform();
    test_terraform_work();
    test_action();
    test_staple();
    game_popup_ctor = game_popup_dtor = popup_, game_get_async_key_state = key_state_, game_fx_play = fx_;
    game_console_focus = focus_, game_veh_scoot = scoot_, game_clock_wait = clock_wait_, game_draw_radius = radius_;
    game_boom = boom_, game_log_say = log_say_, game_synch_veh = synch_veh_, game_monolith = monolith_;
    game_goody_box = goody_box_, game_parse_say = parse_say_, game_parse_num = parse_num_, game_tut_check = tut_check_;
    game_random = random_, game_base_win_check_base = win_base_, game_x_pop = x_pop_, game_interlude = interlude_;
    game_veh_attack_flags = &attack_flags, game_current_veh = &current_veh, game_moved_quickly = &moved_quickly;
    game_console_23be4 = &console_flag, game_zoc_tutorial_count = &zoc_count, game_fungus_tutorial_count = &fungus_count;
    game_tutorial_moves_seen = &moves_seen, game_native_life = &native_life, game_expansion = &expansion;
    game_draw_state = &draw_state, game_veh_ref_map_1 = &ref_map_1, game_abilities = abilities, game_armor = armor;
    game_veh_ref_map_2 = &refs[0], game_veh_ref_3 = &refs[1], game_veh_ref_4 = &refs[2], game_battle_state = &battle_state;
    game_veh_at = veh_at, game_veh_at_rebuilt = &rebuilt, game_veh_lift_x = &lift_x, game_veh_lift_y = &lift_y, game_veh_lifted = &lifted;
    for (int i = 0; i < 32; i++) abilities[i].name = ability_name;
    test_step();
    if (failures) return 1;
    printf("engine_move: ok\n");
    return 0;
}
