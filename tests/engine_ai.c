// Unit test for the AI's turn around its units, native life and planet
// busters, on a small made-up map with stand-ins for the game's functions
// (its enemy_move among them): no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/ai.h"
#include "../src/engine/game.h"
#include "../src/engine/veh.h"

enum { W = 16, H = 12 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area = W / 2 * H, base_count, veh_count, turn = 10;
static int32_t current = 7, find_dist, net, difficulty = 3, charter_a, charter_b, lift[3], projects[SP_COUNT];
static int32_t tectonic[FACTION_COUNT], attack_flags;
static uint32_t flat = 1, rules_flags, state;
static uint8_t humans;
static struct base base_slots[1 + BASE_COUNT], *bases = base_slots + 1;  // one before, for base -1
static struct veh veh_slots[1 + 16], *vehs = veh_slots + 1;  // one before, for unit -1
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static struct rules_armor armor[ARMOR_COUNT];
static struct rules_basic rules;
static int32_t near_x[9] = {1, 2, 1, 0, -1, -2, -1, 0, 0}, near_y[9] = {-1, 0, 1, 2, 1, 0, -1, -2, 0};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], radius_count[9];
static int16_t queue_x[W / 2 * H], queue_y[W / 2 * H];
static int32_t path_map[W / 2 * H];
static struct game_path path = {path_map, queue_x, queue_y};
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

// Stand-ins: the game's choice of a unit's move spends its moves (or says
// the unit is done) and counts the calls; messages and drawing are noted.
static int moves_made[16], move_done, rand_value, pops;
static int enemy_move_stub(int veh) {
    moves_made[veh]++;
    if (move_done) return 1;
    vehs[veh].moves_spent = (uint8_t)(vehs[veh].moves_spent + 3);
    vehs[veh].order = 0;
    return 0;
}
static int rand_stub(void) { return rand_value; }
static void pump(void) {}
static void draw_stub(int x, int y, int u) {}
static void log_say_stub(char *t, int a, int b, int c) {}
static int parse_says_stub(int slot, char *text, int g, int p) { return 0; }
static int popp_stub(char *file, char *label, int flags, char *picture, int unk) { return pops++, 0; }
static int climax_stub(void) { return 0; }
static int fights, fight_dir;
static int battle_stub(int veh, int dir, int a, int b, int c) { return fights++, fight_dir = dir, 0; }
static int __attribute__((thiscall)) focus_stub(void *console, int x, int y, int f) { return 1; }

static void land(void) {
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2)
            *at(x, y) = (struct map_tile){.climate = 3 << 5, .region = 1, .owner_site = 0xf, .territory = -1};
    base_count = veh_count = 0;
    memset(veh_slots, 0, sizeof veh_slots);
    memset(factions, 0, sizeof factions);
    humans = 0;
}
static void put_base(int b, int x, int y, int faction, int pop) {
    bases[b] = (struct base){.x = (int16_t)x, .y = (int16_t)y, .faction = (uint8_t)faction, .pop_size = (int8_t)pop};
    snprintf(bases[b].name, sizeof bases[b].name, "Base %d", b);
    at(x, y)->bits |= MAP_BASE;
    at(x, y)->owner_site = (uint8_t)faction;
    if (base_count <= b) base_count = b + 1;
}
static void put_unit(int v, int x, int y, int faction, int proto) {
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .proto = (int16_t)proto, .faction = (uint8_t)faction,
                           .home_base = -1, .next_in_stack = -1, .prev_in_stack = -1};
    for (int u = 0; u < veh_count; u++)
        if (u != v && vehs[u].x == x && vehs[u].y == y && vehs[u].next_in_stack < 0) {
            vehs[u].next_in_stack = (int16_t)v, vehs[v].prev_in_stack = (int16_t)u;
            break;
        }
    at(x, y)->bits |= MAP_UNIT;
    if (!(at(x, y)->bits & MAP_BASE)) at(x, y)->owner_site = (uint8_t)faction;
    if (veh_count <= v) veh_count = v + 1;
}

// Designs: 1 a land unit, 2 a planet buster, 3 an aircraft; 8 mind worms,
// 9 an isle of the deep, 12 the Unity's scout chopper, 15 a spore launcher.
static void designs(void) {
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    chassis[1] = (struct rules_chassis){.speed = 4, .triad = TRIAD_SEA, .cargo = 4};
    chassis[2] = (struct rules_chassis){.speed = 8, .triad = TRIAD_AIR, .range = 2};
    chassis[3] = (struct rules_chassis){.speed = 10, .triad = TRIAD_LAND};
    weapons[1].offense = 4, weapons[2].offense = -1, weapons[3].offense = 99;
    armor[1].defense = 2;
    protos[1] = (struct unit_proto){.chassis = 0, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1, .cost = 3};
    protos[2] = (struct unit_proto){.chassis = 3, .weapon = 3, .armor = 1, .plan = PLAN_PLANET_BUSTER, .reactor = 1,
                                    .cost = 2};
    protos[3] = (struct unit_proto){.chassis = 2, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1};
    protos[8] = (struct unit_proto){.chassis = 0, .weapon = 2, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1};
    protos[9] = (struct unit_proto){.chassis = 1, .weapon = 2, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1, .carry = 2};
    protos[12] = (struct unit_proto){.chassis = 2, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1};
    protos[15] = (struct unit_proto){.chassis = 0, .weapon = 2, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1,
                                     .abilities = 0x8000};  // artillery
}

static void test_alien_base(void) {
    land();
    put_base(0, 2, 2, 1, 3);
    put_base(1, 10, 4, 2, 3);
    put_unit(0, 4, 4, 0, 8);
    EXPECT(alien_base(0, 4, 4), 0);
    EXPECT(find_dist, 2);
    // Its home base counts half as far; the last of equals wins.
    vehs[0].home_base = 1;
    EXPECT(alien_base(0, 4, 4), 1);
    EXPECT(find_dist, 2);
    // A faction with PLANET above 0 and no ecological damage, twice as far.
    factions[2].se[SE_PLANET] = 1;
    EXPECT(alien_base(0, 4, 4), 0);
    vehs[0].order_auto_type = 2;  // unless the unit came from it
    EXPECT(alien_base(0, 4, 4), 1);
    // A big intake brings a base closer.
    vehs[0].home_base = -1, vehs[0].order_auto_type = 0, factions[2].se[SE_PLANET] = 0;
    bases[1].intake_2[1] = 64;
    EXPECT(alien_base(0, 4, 4), 1);
    EXPECT(find_dist, 1);
    // Only bases on its continent, unless it is at sea.
    at(10, 4)->region = 2;
    EXPECT(alien_base(0, 4, 4), 0);
    at(4, 4)->region = 0x41;
    EXPECT(alien_base(0, 4, 4), 1);
    base_count = 0;
    EXPECT(alien_base(0, 4, 4), -1);
    EXPECT(find_dist, 9999);
}

static void test_alien_move(void) {
    land();
    put_base(0, 6, 4, 1, 3);
    put_unit(0, 4, 4, 0, 8);
    put_unit(1, 6, 4, 1, 1);  // a defender, so the worm isn't alone
    // No moves left: nothing.
    vehs[0].moves_spent = 3;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].movement_turns, 0);
    // Hidden: it waits.
    vehs[0].moves_spent = 0, vehs[0].flags = VEH_FLAG_INVISIBLE;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].moves_spent, 3);
    // Next to a faction's base: it goes for it.
    vehs[0].moves_spent = 0, vehs[0].flags = 0;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].order, 0x58);
    EXPECT(vehs[0].waypoint_x[0], 6);
    EXPECT(vehs[0].waypoint_y[0], 4);
    EXPECT(vehs[0].movement_turns, 1);
    // Further off, it heads the way it faces, onto improvements.
    land();
    put_base(0, 14, 4, 1, 3);
    put_unit(0, 4, 4, 0, 8);
    vehs[0].unk_29 = 3;  // facing south
    at(5, 5)->bits |= MAP_ROAD | MAP_FARM;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].waypoint_x[0], 5);
    EXPECT(vehs[0].waypoint_y[0], 5);
    // The Unity's scout chopper waits.
    vehs[0].proto = 12, vehs[0].order = 0;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].order, 0);

    // A spore launcher fires at a unit two squares off (radius square 9).
    land();
    radius_x[9] = 4, radius_y[9] = 0;
    put_base(0, 14, 4, 1, 3);
    put_unit(0, 4, 4, 0, 15);
    put_unit(1, 8, 4, 1, 1);
    fights = 0;
    EXPECT(alien_move(0), 1);
    EXPECT(fights, 1);
    EXPECT(fight_dir, 9);
    // Out of artillery range it moves instead.
    rules.artillery_max_range = 1;
    EXPECT(alien_move(0), 0);
    EXPECT(fights, 1);
    rules.artillery_max_range = 2, radius_x[9] = 0, radius_y[9] = -40;

    // An isle of the deep with worms aboard lands them on its base's
    // continent: their orders end, and it waits.
    land();
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < 6; x += 2) at(x, y)->climate = 1 << 5, at(x, y)->region = 0x41;
    put_base(0, 10, 4, 1, 3);
    put_unit(0, 4, 4, 0, 9);
    put_unit(1, 4, 4, 0, 8);
    vehs[1].order = ORDER_SENTRY_BOARD;
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[1].order, 0);
    EXPECT(vehs[0].moves_spent, 12);
    EXPECT(pops, 0);  // not the player's base
    // With no land near to put them on, it sets course for its base.
    land();
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) at(x, y)->climate = 1 << 5, at(x, y)->region = 0x41;
    put_base(0, 10, 4, 1, 3);
    put_unit(0, 4, 4, 0, 9);
    put_unit(1, 4, 4, 0, 8);
    EXPECT(alien_move(0), 0);
    EXPECT(vehs[0].order, 0x18);
}

static void test_planet_buster(void) {
    land();
    put_base(0, 2, 2, 1, 3);
    put_base(1, 10, 4, 2, 6);
    put_unit(0, 4, 4, 1, 2);
    put_unit(1, 10, 4, 2, 1);  // the target's defender
    factions[1].ranking = 1, factions[2].ranking = 3;
    factions[1].flags = 0x1000000;  // commits atrocities wantonly
    // A human's planet buster is left alone.
    humans = 1 << 1;
    EXPECT(enemy_planet_buster(0), -1);
    humans = 0;
    // In reach: put next to the target, and the way to it returned.
    EXPECT(enemy_planet_buster(0), 4);
    EXPECT(vehs[0].x, 11);
    EXPECT(vehs[0].y, 3);
    // A pact rules the target out. With no target the original takes base
    // 0 for the best of the second pass, and as it is the faction's own,
    // puts the unit there.
    put_unit(0, 4, 4, 1, 2);
    at(11, 3)->bits = 0;
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(enemy_planet_buster(0), -1);
    EXPECT(vehs[0].x, 2);
    EXPECT(vehs[0].y, 2);
    // So does a base of its own in the blast.
    at(2, 2)->bits &= ~(uint32_t)MAP_UNIT;
    put_unit(0, 4, 4, 1, 2);
    factions[1].diplo_status[2] = 0;
    put_base(2, 11, 5, 1, 1);
    EXPECT(enemy_planet_buster(0), -1);
    EXPECT(vehs[0].x, 2);
    // With base 0 another's, the unit stays.
    at(2, 2)->bits &= ~(uint32_t)MAP_UNIT;
    put_unit(0, 4, 4, 1, 2);
    bases[0].faction = 3;
    EXPECT(enemy_planet_buster(0), -1);
    EXPECT(vehs[0].x, 4);
}

static void test_turn(void) {
    land();
    chassis[0].speed = 2;
    put_unit(0, 4, 4, 1, 1);
    put_unit(1, 8, 4, 1, 1);
    put_unit(2, 8, 6, 2, 1);
    put_unit(3, 6, 6, 1, 1);
    vehs[3].order = ORDER_SENTRY_BOARD;
    memset(moves_made, 0, sizeof moves_made);
    // enemy_veh: the move picked, then (no order) the unit waits.
    move_done = 0;
    EXPECT(enemy_veh(0), 0);
    EXPECT(moves_made[0], 1);
    EXPECT(vehs[0].moves_spent, 6);
    // With no moves left after it: 1.
    vehs[0].moves_spent = 3;
    EXPECT(enemy_veh(0), 1);
    EXPECT(vehs[0].moves_spent, 6);
    // Done, or without moves: 1.
    move_done = 1;
    EXPECT(enemy_veh(1), 1);
    // The faction's turn: each of its units but the one on board, until
    // it has no moves; the flag cleared.
    move_done = 0;
    memset(moves_made, 0, sizeof moves_made);
    for (int v = 0; v < 4; v++) vehs[v].moves_spent = 0;
    factions[1].flags = 0x10000;
    enemy_turn(1);
    EXPECT(moves_made[0], 1);
    EXPECT(moves_made[1], 1);
    EXPECT(moves_made[2], 0);
    EXPECT(moves_made[3], 0);
    EXPECT(factions[1].flags, 0);
    // Done at once: one call each.
    memset(moves_made, 0, sizeof moves_made);
    move_done = 1;
    for (int v = 0; v < 4; v++) vehs[v].moves_spent = 0;
    enemy_turn(1);
    EXPECT(moves_made[0], 6);  // in passes 3 to 7 and 9: combat units, undamaged, all
    chassis[0].speed = 1;
}

// Air units: designs 4 a missile, 5 a tectonic payload, 6 an interceptor,
// 7 a copter (a range of one turn).
static void test_air_power(void) {
    land();
    chassis[4] = (struct rules_chassis){.speed = 12, .triad = TRIAD_AIR, .range = 1, .missile = 1};
    chassis[5] = (struct rules_chassis){.speed = 8, .triad = TRIAD_AIR, .range = 1};
    protos[4] = (struct unit_proto){.chassis = 4, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1, .cost = 2};
    protos[5] = (struct unit_proto){.chassis = 4, .weapon = 1, .armor = 1, .plan = 13, .reactor = 1, .cost = 2};
    protos[6] = (struct unit_proto){.chassis = 2, .weapon = 1, .armor = 1, .plan = PLAN_AIR_SUPERIORITY, .reactor = 1};
    protos[7] = (struct unit_proto){.chassis = 5, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .reactor = 1};
    int x = 4, y = 4;
    // A go-to elsewhere is kept: -2, or -1 when asked to act now (not for
    // an interceptor).
    put_unit(0, 4, 4, 1, 3);
    vehs[0].order = 0x18, vehs[0].waypoint_x[0] = 8, vehs[0].waypoint_y[0] = 4;
    EXPECT(air_power(0, 0, 0, &x, &y), -2);
    EXPECT(air_power(0, 0, 1, &x, &y), -1);
    vehs[0].proto = 6;
    EXPECT(air_power(0, 0, 1, &x, &y), -2);
    // An explorer with two squares of reach left heads a square off, where
    // most is unseen.
    land();
    put_unit(0, 4, 4, 1, 7);
    vehs[0].state = 0x4000;
    vehs[0].moves_spent = (uint8_t)(veh_speed(0, 0) - 6);
    EXPECT(air_power(0, 0, 0, &x, &y), -2);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].status_icon, 'a');
    EXPECT(vehs[0].waypoint_x[0], 5);
    EXPECT(vehs[0].waypoint_y[0], 3);
    // All seen: a base of its own in reach, picked at random; with none, it
    // stops.
    for (int i = 0; i < W / 2 * H; i++) tiles[i].visibility = 1 << 1;
    vehs[0].order = 0;
    EXPECT(air_power(0, 0, 0, &x, &y), -3);
    EXPECT(vehs[0].order, 0);
    put_base(0, 6, 4, 1, 2);
    rand_value = 7;
    EXPECT(air_power(0, 0, 0, &x, &y), -2);
    EXPECT(vehs[0].status_icon, 'c');
    EXPECT(vehs[0].waypoint_x[0], 6);
    // A missile with no target: alone it stays; one of two goes to a base
    // in reach with fewer missiles, put down there.
    land();
    put_base(0, 6, 4, 1, 2);
    put_unit(0, 4, 4, 1, 4);
    x = 4, y = 4;
    EXPECT(air_power(0, 0, 0, &x, &y), -3);
    EXPECT(vehs[0].x, 4);
    put_unit(1, 4, 4, 1, 4);
    EXPECT(air_power(0, 0, 0, &x, &y), -3);
    EXPECT(vehs[0].x, 6);
    EXPECT(x, 6);
    EXPECT(y, 4);
    // A tectonic payload heads for a raise-land goal in reach.
    land();
    put_unit(0, 4, 4, 1, 5);
    factions[1].goals[3] = (struct goal){.type = GOAL_RAISE_LAND, .priority = 5, .x = 8, .y = 4};
    x = 4, y = 4;
    EXPECT(air_power(0, 0, 0, &x, &y), -3);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].status_icon, 'm');
    EXPECT(vehs[0].waypoint_x[0], 8);
    // An interceptor covers a grounded aircraft of its own out in the open.
    land();
    put_unit(0, 4, 4, 1, 6);
    put_unit(1, 8, 4, 1, 3);
    vehs[1].moves_spent = (uint8_t)veh_speed(1, 0);
    EXPECT(air_power(0, 0, 0, &x, &y), -2);
    EXPECT(vehs[0].status_icon, 'a');
    EXPECT(vehs[0].waypoint_x[0], 8);
    // With moves left it needs none: no target, nothing to do (-3), or home.
    vehs[1].moves_spent = 0;
    vehs[0].order = 0;
    EXPECT(air_power(0, 0, 0, &x, &y), -3);
    EXPECT(vehs[0].order, 0);
    // On alert to intercept, an aircraft goes back to its post, or stays.
    land();
    put_unit(0, 4, 4, 1, 3);
    vehs[0].state = 0x200, vehs[0].order_auto_type = 11;
    vehs[0].waypoint_x[1] = 6, vehs[0].waypoint_y[1] = 6;
    EXPECT(air_power(0, 0, 0, &x, &y), -2);
    EXPECT(vehs[0].status_icon, 'l');
    EXPECT(vehs[0].waypoint_x[0], 6);
    vehs[0].order = 0, vehs[0].waypoint_x[1] = 4, vehs[0].waypoint_y[1] = 4;
    EXPECT(air_power(0, 0, 0, &x, &y), -4);
    // On alert to bombard the enemy base at its post: it heads there.
    land();
    put_base(0, 8, 4, 2, 3);
    put_unit(1, 8, 4, 2, 1);
    put_unit(0, 4, 4, 1, 3);
    vehs[0].state = 0x200, vehs[0].order_auto_type = 10;
    vehs[0].waypoint_x[1] = 8, vehs[0].waypoint_y[1] = 4;
    EXPECT(air_power(0, 0, 1, &x, &y), -2);
    EXPECT(vehs[0].status_icon, 'a');
    EXPECT(vehs[0].waypoint_x[0], 8);
    // At peace, it has none.
    vehs[0].order = 0;
    factions[1].diplo_status[2] = 0x4;  // a truce
    EXPECT(air_power(0, 0, 1, &x, &y), -3);
    EXPECT(vehs[0].order, 0);
}

// A unit's move: a hidden AI unit waits; the player's automated former,
// told to farm a square below the hills, gives up its automation.
static void test_enemy_move(void) {
    land();
    protos[4] = (struct unit_proto){.chassis = 0, .weapon = 2, .armor = 1, .plan = 9, .reactor = 1};
    put_unit(0, 4, 4, 2, 1);
    vehs[0].flags = 0x440;
    EXPECT(enemy_move(0), 0);
    EXPECT(vehs[0].moves_spent > 0, 1);
    land();
    humans = 1 << 2;
    put_unit(0, 4, 4, 2, 4);
    at(4, 4)->climate = 1 << 5;
    vehs[0].state = 0x200, vehs[0].order_auto_type = 4;
    EXPECT(enemy_move(0), 0);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].state & 0x200, 0);
}

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_flat = &flat, game_bases = bases, game_base_count = &base_count;
    game_vehs = vehs, game_veh_count = &veh_count, game_turn = &turn, game_current_faction = &current;
    game_net_multiplayer = &net, game_human_factions = &humans, game_factions = factions, game_faction_defs = defs;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_armor = armor, game_rules_basic = &rules;
    game_rules = &rules_flags, game_difficulty = &difficulty, game_base_find_dist = &find_dist;
    game_un_charter_a = &charter_a, game_un_charter_b = &charter_b, game_secret_projects = projects;
    game_tectonic_detonations = tectonic, game_veh_attack_flags = &attack_flags, game_path = &path;
    game_veh_lift_x = &lift[0], game_veh_lift_y = &lift[1], game_veh_lifted = &lift[2];
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_radius_x = radius_x, game_radius_y = radius_y;
    game_radius_count = radius_count;
    for (int i = 0; i < 8; i++) radius_x[1 + i] = near_x[i], radius_y[1 + i] = near_y[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 0, radius_y[i] = -40;  // off the map
    for (int r = 0; r < 9; r++) radius_count[r] = r ? 9 : 1;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    game_enemy_move = enemy_move_stub, game_rand = rand_stub, game_do_all_non_input = pump;
    game_draw_tile = draw_stub, game_log_say = log_say_stub, game_parse_says = parse_says_stub, game_popp = popp_stub;
    game_console_focus = focus_stub, game_battle_fight = battle_stub, game_state = &state, game_climactic_battle = climax_stub;
    static char *script = "script";
    game_script_file = &script;
    static uint8_t image[0x300000];  // strings, from 0x400000; all empty
    game_test_image = image;
    rules.move_rate_roads = 3, rules.artillery_max_range = 2;
    designs();
    test_alien_base();
    test_alien_move();
    test_planet_buster();
    test_turn();
    test_air_power();
    test_enemy_move();
    if (failures) return 1;
    printf("engine_ai: ok\n");
    return 0;
}
