// Unit test for the reimplemented unit queries, on made-up tables and a
// small map of land: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/veh.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, base_count = 2, veh_count = 2, turn = 100;
static int32_t expansion = 1, projects[SP_COUNT], net_mp = 0, tectonic[FACTION_COUNT];
static uint32_t flat = 0, rules = 0, state = 0;
static int32_t lift[6], current_faction = 1;
static int contribution_stub(int veh, int t) { return 2; }
static uint8_t achieved[TECH_COUNT];
static struct base bases[2];
static struct veh vehs[VEH_COUNT + 1];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static struct rules_armor armor[ARMOR_COUNT];
static struct rules_reactor reactors[REACTOR_COUNT];
static struct rules_ability abilities[32];
static struct rules_tech techs[TECH_COUNT];
static struct rules_basic rules_basic;
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], near_x[8], near_y[8];
static int failures, objective;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static int objective_stub(int base) { return objective; }
static int veh_at_stub(int x, int y) { return -1; }
static int32_t refs[5], *net_ref, battle;
static int drawn_radius, drawn_tile, checked_base;
static void log_say_stub(char *t, int a, int b, int c) {}
static void __attribute__((thiscall)) deletion_stub(void *l, int v) {}
static void __attribute__((thiscall)) check_base_stub(void *w, int b) { checked_base = b; }
static void __attribute__((thiscall)) check_loc_stub(void *w, int x, int y, int u) {}
static void draw_radius_stub(int x, int y, int r, int u) { drawn_radius++; }
static void draw_tile_stub(int x, int y, int u) { drawn_tile++; }
static int rand_value;
static int32_t more_prefs;
static uint32_t draw_state;
static void __attribute__((thiscall)) redraw_stub(void *w) {}
static int32_t computed_base = -1, find_dist = 0;
static int rand_stub(void) { return rand_value; }
static char *__attribute__((thiscall)) strings_get_stub(void *table, char *s) { return s; }
static char battle_text[256];
static char *label_list[1200], **labels_ptr = label_list;
static int32_t mod_counts[4], mods[8], morales[2];
static char mod_labels[8][80], *terrain;
static int drawn_map, console_updates, goal_here;
static void draw_map_stub(int u) { drawn_map++; }
static void __attribute__((thiscall)) console_update_stub(void *c, int u) { console_updates++; }
static int at_goal_stub(int f, int type, int x, int y) { return goal_here; }

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_rules = &rules, game_rules_basic = &rules_basic, game_turn = &turn;
    game_bases = bases, game_base_count = &base_count, game_vehs = vehs, game_veh_count = &veh_count;
    game_factions = factions, game_faction_defs = defs, game_protos = protos, game_chassis = chassis;
    game_weapons = weapons, game_armor = armor, game_reactors = reactors, game_abilities = abilities;
    game_tech = techs, game_tech_achieved = achieved, game_expansion = &expansion, game_secret_projects = projects;
    game_net_multiplayer = &net_mp, game_tectonic_detonations = tectonic;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_veh_lift_x = &lift[0], game_veh_find_dist = &lift[1], game_veh_lifted = &lift[2], game_veh_lift_y = &lift[3], game_veh_at_rebuilt = &lift[5];
    game_state = &state, game_current_faction = &current_faction, game_contribution = contribution_stub;
    game_is_objective = objective_stub, game_veh_at = veh_at_stub;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 1; i < 9; i++) radius_x[i] = (i % 3) - 1, radius_y[i] = (i / 3 % 3) - 1;
    for (int i = 0; i < 8; i++) near_x[i] = radius_x[i + 1], near_y[i] = radius_y[i + 1];
    for (int i = 0; i < W / 2 * H; i++) tiles[i] = (struct map_tile){.climate = 3 << 5, .owner_site = 0xf, .region = 1};
    rules_basic.move_rate_roads = 3;
    for (int i = 0; i < 3; i++) rules_basic.extra_cost_prototype[i] = 50 * (i + 1);
    for (int i = 0; i < 3; i++) rules_basic.psi_combat_ratio_atk[i] = 3, rules_basic.psi_combat_ratio_def[i] = 2;
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_NONE;
    for (int i = 0; i < REACTOR_COUNT; i++) reactors[i].preq_tech = 10 + i;
    chassis[0].speed = 1, chassis[0].triad = TRIAD_LAND;
    chassis[1].speed = 4, chassis[1].triad = TRIAD_SEA;
    chassis[2].speed = 8, chassis[2].triad = TRIAD_AIR;
    weapons[0].offense = 4, weapons[1].offense = -1, weapons[7].mode = 7;
    armor[1].defense = 3, armor[2].defense = -1;

    // Faction 1's designs: a land unit (proto 64), a psi air unit (65).
    protos[64] = (struct unit_proto){.chassis = 0, .weapon = 0, .armor = 1, .reactor = 1, .cost = 10, .flags = PROTO_FLAG_ACTIVE};
    protos[65] = (struct unit_proto){.chassis = 2, .weapon = 1, .armor = 2, .reactor = 2, .cost = 8,
                                     .flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT, .abilities = 0x20};
    protos[2] = (struct unit_proto){.chassis = 0, .weapon = 0, .cost = 4, .flags = PROTO_FLAG_ACTIVE, .preq_tech = 5};
    factions[1].diff_level = 3, factions[1].enemy_best_armor_value = 4, factions[1].enemy_best_weapon_value = 6;

    // Abilities: the design's own, an alien's free deep radar, a faction
    // bonus once it has the tech.
    EXPECT(has_abil(65, 0x20), 1);
    EXPECT(has_abil(64, 0x2), 0);
    defs[1].rule_flags = RULE_FLAG_ALIEN;
    EXPECT(has_abil(64, 0x2), 1);
    defs[1].rule_flags = 0, defs[1].bonus_count = 1, defs[1].bonus_id[0] = 15, defs[1].bonus_val1[0] = 4;
    abilities[4].preq_tech = 7;
    EXPECT(has_abil(64, 0x10), 0);
    achieved[7] = 1 << 1;
    EXPECT(has_abil(64, 0x10), 1);

    // The best reactor with its tech.
    EXPECT(best_reactor(1), 1);
    achieved[12] = 1 << 1;
    EXPECT(best_reactor(1), 3);

    // Ratings: psi weapons and armor against the enemy's best, with the
    // faction's PSI bonus and the Neural Amplifier.
    EXPECT(weap_val(64, 1), 4);
    EXPECT(weap_val(65, 1), 6);  // 4 * 3 / 2
    EXPECT(arm_val(1, 1), 6);
    EXPECT(arm_val(2, 1), 8);   // 6 * 2 / 3, doubled
    EXPECT(arm_val(2, -1), -2);
    defs[1].rule_psi = 50;
    EXPECT(psi_factor(10, 1, 0, 1), 22);  // 15, and half again for fungus
    bases[1] = (struct base){.x = 6, .y = 4, .faction = 1};
    projects[9] = 1;  // the Neural Amplifier
    EXPECT(psi_factor(10, 1, 0, 0), 22);
    EXPECT(psi_factor(10, 1, 1, 1), 15);  // attacking: no Dream Twister
    defs[1].rule_psi = 0, projects[9] = -1;

    // Speed: an air unit's reactor counts twice, air superiority takes a
    // fifth; a fungal tower doesn't move.
    EXPECT(speed_proto(64), 3);
    EXPECT(speed_proto(65), (8 + 4) * 4 / 5 * 3);
    EXPECT(speed_proto(19), 0);

    // Cost: the prototype extra, by triad, unless already built or with a
    // Skunkworks; nothing extra for basic designs.
    int charged = -1;
    EXPECT(veh_cost(64, 0, &charged), 10 + (base_cost(64) * 50 + 50) / 100);
    EXPECT(charged, 1);
    EXPECT(veh_cost(65, 0, &charged), 8);
    EXPECT(charged, 0);
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .pop_size = 6};
    bases[0].facilities_built[1] = 1 << 2;  // Skunkworks
    EXPECT(veh_cost(64, 0, &charged), 10);
    EXPECT(charged, 0);
    EXPECT(veh_cost(2, -1, NULL), 4);

    // Availability: the tech, obsolescence, a design of its own faction.
    EXPECT(veh_avail(2, 1, -1), 0);
    achieved[5] = 1 << 1;
    EXPECT(veh_avail(2, 1, -1), 1);
    EXPECT(veh_avail(2, 0, -1), 0);
    EXPECT(veh_avail(64, 1, -1), 1);
    EXPECT(veh_avail(64, 2, -1), 0);
    protos[64].obsolete_factions = 1 << 1;
    EXPECT(veh_avail(64, 1, -1), 0);
    protos[64].obsolete_factions = 0;
    expansion = 0, protos[64].armor = 10;
    EXPECT(veh_avail(64, 1, -1), 0);
    expansion = 1, protos[64].armor = 1;

    // Morale from a base: the Command Center for land, the Bioenhancement
    // Center for all, halved under low MORALE.
    bases[0].facilities_built[3] = 1 << 3;  // Command Center (27)
    EXPECT(morale_mod(0, 1, TRIAD_LAND), 2);
    EXPECT(morale_mod(0, 1, TRIAD_SEA), 0);
    bases[0].facilities_built[3] |= 1 << 6;  // Bioenhancement Center (30)
    EXPECT(morale_mod(0, 1, TRIAD_LAND), 4);
    factions[1].se_pending[SE_MORALE] = -2;
    EXPECT(morale_mod(0, 1, TRIAD_LAND), 2);
    EXPECT(breed_mod(0, 1), 1);

    // A unit's morale: MORALE (softened), the faction bonus, capped at 6.
    vehs[0] = (struct veh){.x = 2, .y = 2, .faction = 1, .proto = 64, .morale = 2, .home_base = 0};
    factions[1].se[SE_MORALE] = 3;
    EXPECT(morale_veh(0, 0, 0), 4);
    factions[1].se[SE_MORALE] = -3;
    EXPECT(morale_veh(0, 0, 0), 0);
    bases[0].facilities_built[0] = 1 << 2;  // Children's Creche halves the penalty
    EXPECT(morale_veh(0, 0, 0), 1);
    defs[1].rule_morale = 9;
    EXPECT(morale_veh(0, 0, 0), 6);
    defs[1].rule_morale = 0;

    // Native life grows with the turn; a fungal tower counts fungus.
    vehs[1] = (struct veh){.x = 4, .y = 2, .faction = 0, .proto = 8};
    EXPECT(morale_veh(1, 0, 0), 2);
    turn = 400;
    EXPECT(morale_alien(1, 0), 6);
    vehs[1].proto = 19;
    EXPECT(morale_alien(1, 0), 0);
    for (int i = 0; i < W / 2 * H; i++) tiles[i].bits = MAP_FUNGUS;
    EXPECT(morale_alien(1, 0), 6);

    // Defenders wanted: size, headquarters, an objective.
    for (int i = 0; i < W / 2 * H; i++) tiles[i].bits = 0;
    bases[0].facilities_built[0] = 0;
    EXPECT(garrison_check(0), 2);  // (0 + 2) / 3 + (6 + 1) / 4 + 1
    bases[0].facilities_built[0] = 1 << 1;  // headquarters
    objective = 1, factions[1].flags = 0x40000000;
    EXPECT(garrison_check(0), 4);
    factions[1].region_base_plan[1] = PLAN_COLONY;
    EXPECT(garrison_check(0), 3);

    // Stacks: counted from the head, whichever unit is given.
    vehs[0].next_in_stack = 1, vehs[0].prev_in_stack = -1;
    vehs[1] = (struct veh){.x = 2, .y = 2, .faction = 1, .proto = 65, .morale = 1, .next_in_stack = -1, .prev_in_stack = 0};
    EXPECT(stack_check(1, 1, -1, -1, -1), 2);
    EXPECT(stack_check(1, 0, 65, 1, -1), 1);
    EXPECT(stack_check(1, 3, TRIAD_AIR, -1, -1), 1);
    EXPECT(stack_check(0, 4, 1, -1, -1), 4 - 1);   // weapon strengths
    EXPECT(stack_check(0, 8, -1, -1, -1), -1);     // a land unit wants a place aboard
    EXPECT(stack_check(0, 10, 2, -1, -1), 0);
    EXPECT(stack_check(-1, 1, -1, -1, -1), 0);
    EXPECT(stack_check(0, 25, -1, -1, -1), 0);
    EXPECT(can_arty(64, 1), 0);
    protos[64].abilities = 0x8000;
    EXPECT(can_arty(64, 0), 1);

    // Lifting, dropping, promoting and demoting keep the list and the
    // map's unit bits.
    for (int i = 0; i < W / 2 * H; i++) tiles[i].bits = 0, tiles[i].owner_site = 0xf;
    vehs[0] = (struct veh){.x = 4, .y = 2, .faction = 1, .proto = 64, .next_in_stack = -1, .prev_in_stack = -1};
    vehs[1] = (struct veh){.x = -1, .y = -1, .faction = 1, .proto = 65, .next_in_stack = -1, .prev_in_stack = -1};
    veh_count = 2;
    EXPECT(veh_at(4, 2), -1);  // the map doesn't mark it yet
    veh_lift(0);
    veh_drop(0, 4, 2);
    EXPECT(tiles[2 + 2 * (W / 2)].bits, MAP_UNIT | 0x100000);
    EXPECT(tiles[2 + 2 * (W / 2)].owner_site & 0xf, 1);
    veh_drop(1, 4, 2);  // the air unit, at the head
    EXPECT(veh_at(4, 2), 1);
    EXPECT(vehs[1].next_in_stack, 0);
    EXPECT(vehs[0].prev_in_stack, 1);
    veh_promote(0);
    EXPECT(veh_at(4, 2), 0);
    veh_demote(0);
    EXPECT(veh_at(4, 2), 1);
    EXPECT(vehs[0].prev_in_stack, 1);
    EXPECT(veh_lift(1), 1);
    EXPECT(lift[0], 4);
    EXPECT(lift[2], 1);
    EXPECT(veh_at(4, 2), 0);
    veh_lift(0);
    EXPECT(tiles[2 + 2 * (W / 2)].bits & MAP_UNIT, 0);
    veh_drop(0, 4, 2);
    veh_drop(1, 4, 2);
    stack_put(0, 6, 2);
    EXPECT(veh_at(6, 2) >= 0 && veh_at(4, 2) < 0, 1);
    EXPECT(stack_check(0, 1, -1, -1, -1), 2);
    EXPECT(stack_fix(0), 0);  // only in a network game
    veh_clear(1, 2, 1);
    EXPECT(vehs[1].x, -4);
    EXPECT(vehs[1].home_base, -1);
    EXPECT(vehs[1].morale, defs[1].rule_morale + 1);
    veh_sleep(0);
    EXPECT(vehs[0].order, 1);

    // Strengths in combat: psi odds against psi armor, eight times a
    // weapon against a unit, half again for a missile.
    vehs[0] = (struct veh){.x = 2, .y = 2, .faction = 1, .proto = 64, .next_in_stack = -1, .prev_in_stack = -1};
    vehs[1] = (struct veh){.x = 4, .y = 2, .faction = 2, .proto = 65, .next_in_stack = -1, .prev_in_stack = -1};
    EXPECT(offense_proto(64, -1, 0), 4);
    EXPECT(offense_proto(64, 0, 0), 32);
    EXPECT(offense_proto(64, 1, 0), 3 * 8);   // psi armor: the air psi ratio
    EXPECT(offense_proto(64, 1, 1), 32);      // bombarding: no psi
    chassis[0].missile = 1;
    EXPECT(offense_proto(64, 0, 0), 48);
    chassis[0].missile = 0;
    EXPECT(armor_proto(64, -1, 0), 3);
    EXPECT(armor_proto(64, 0, 0), 24);
    EXPECT(armor_proto(64, 1, 0), 2 * 8);     // a psi attacker: odds for its own (land) triad
    rules_basic.psi_combat_ratio_def[2] = 5;
    EXPECT(armor_proto(65, 0, 0), 5 * 8);
    EXPECT(prototype_factor(64), 50);
    EXPECT(prototype_factor(65), 150);

    // The nearest unit, and its distance.
    EXPECT(veh_find(4, 2, -1, -1), 1);
    EXPECT(lift[1], 0);
    EXPECT(veh_find(4, 2, 1, -1), 0);
    EXPECT(lift[1], 1);  // x two apart on a row is one square
    EXPECT(veh_find(4, 2, 3, -1), -1);
    EXPECT(lift[1], 9999);

    // Readiness for the player's faction.
    vehs[0].moves_spent = 0;
    EXPECT(veh_selectable(0), 1);
    EXPECT(veh_selectable(1), 0);  // another faction's
    EXPECT(veh_ready(0), 1);
    vehs[0].order = 2;
    EXPECT(veh_ready(0), 0);
    EXPECT(veh_unmoved(0), 1);
    vehs[0].moves_spent = 3;
    EXPECT(veh_unmoved(0), 0);
    vehs[0].order = 5, vehs[0].movement_turns = 3;  // terraforming
    EXPECT(veh_unmoved(0), 1);
    EXPECT(veh_wake(0), 0);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].moves_spent, 0);  // its 3 moves, less one square
    EXPECT(vehs[0].movement_turns, 1);
    EXPECT(veh_jail(0), 0);
    EXPECT(veh_fake(64, 1), VEH_COUNT);
    EXPECT(vehs[VEH_COUNT].proto, 64);

    // Health, moves and stacks. Unit 0 (proto 64, reactor 1) has 3 moves.
    vehs[0].damage = 4, vehs[0].moves_spent = 1;
    EXPECT(proto_power(0), 10);
    EXPECT(veh_health(0), 6);
    vehs[0].damage = 12;
    EXPECT(veh_health(0), 0);
    vehs[0].damage = 0;
    EXPECT(veh_moves(0), 2);
    vehs[0].moves_spent = 9;
    EXPECT(veh_moves(0), 0);
    vehs[0].moves_spent = 0;
    protos[64].plan = PLAN_PLANET_BUSTER;
    EXPECT(planet_buster(0), 1);
    protos[64].plan = 12;  // an alien artifact
    EXPECT(proto_power(0), 1);
    protos[64].plan = 0;
    EXPECT(veh_top(-1), -1);
    vehs[0].prev_in_stack = -1, vehs[0].next_in_stack = 1, vehs[1].prev_in_stack = 0, vehs[1].next_in_stack = -1;
    EXPECT(veh_top(1), 0);
    vehs[0].next_in_stack = vehs[1].prev_in_stack = -1;

    // Air drops: the rule's range, or the whole map with the Space Elevator.
    rules_basic.max_airdrop_range = 5, rules_basic.tech_orbital_insert = 30;
    EXPECT(drop_range(1), 5);
    projects[SP_SPACE_ELEVATOR] = 1;  // bases[1] is faction 1's
    EXPECT(drop_range(1), W);
    EXPECT(drop_range(2), 5);
    projects[SP_SPACE_ELEVATOR] = -1;

    // Transports: at sea times the reactor, halved if slow, half again
    // more (rounded up) for heavy transport.
    chassis[1].cargo = 2, chassis[0].cargo = 3;
    EXPECT(transport_val(1, 0, 3), 6);
    EXPECT(transport_val(0, 0, 3), 3);
    EXPECT(transport_val(0, 0x800000, 3), 1);
    EXPECT(transport_val(0, 0x80000, 3), 5);
    EXPECT(abil_index(0x80000), 19);
    EXPECT(abil_index(0x80000000u), 31);
    EXPECT(armor_val(64, -1), 6);

    // A design for a plan: basic ones need their tech, the faction's own
    // don't, and an obsolete one is skipped.
    protos[2].plan = 3, protos[64].plan = 3, achieved[5] = 0;
    EXPECT(get_plan(1, 3), 64);
    achieved[5] = 1 << 1;
    EXPECT(get_plan(1, 3), 2);
    protos[2].obsolete_factions = 1 << 1;
    EXPECT(get_plan(1, 3), 64);
    EXPECT(get_plan(1, 9), -1);
    protos[2].plan = protos[64].plan = 0;

    // A monolith for an armed unit below elite.
    vehs[0].morale = 2, vehs[0].state = 0;
    EXPECT(want_monolith(0), 1);
    vehs[0].state = 0x100;
    EXPECT(want_monolith(0), 0);
    vehs[0].state = 0, vehs[0].morale = 6;
    EXPECT(want_monolith(0), 0);
    vehs[0].morale = 0;

    // Killing: later units move down a slot and are renumbered.
    game_log_say = log_say_stub, game_deletion_add = deletion_stub, game_base_win_check_base = check_base_stub;
    game_base_win_check_loc = check_loc_stub, game_draw_radius = draw_radius_stub;
    game_draw_tile = draw_tile_stub, game_battle_state = &battle, game_net_veh_ref = &net_ref;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &refs[4];
    veh_count = 4;
    for (int i = 0; i < 4; i++)
        vehs[i] = (struct veh){.x = 2 * i, .y = 2, .proto = 64, .faction = 1, .home_base = (int16_t)i,
                               .next_in_stack = -1, .prev_in_stack = -1};
    vehs[3].x = 2;  // units 1 and 3 share a square
    vehs[1].next_in_stack = 3, vehs[3].prev_in_stack = 1;
    vehs[2].order = 1, vehs[2].waypoint_x[0] = 3;  // on board unit 3
    vehs[0].order = 1, vehs[0].waypoint_x[0] = 1;
    factions[1].units_active[64] = 4, factions[1].total_combat_units = 4;
    refs[0] = 1, refs[1] = 3, refs[2] = 0, refs[3] = -1, refs[4] = 3;
    veh_kill(1);
    EXPECT(veh_count, 3);
    EXPECT(vehs[0].order, 0);  // its transport is gone
    EXPECT(vehs[1].home_base, 2);
    EXPECT(vehs[1].waypoint_x[0], 2);
    EXPECT(vehs[2].home_base, 3);
    EXPECT(vehs[2].prev_in_stack, -1);
    EXPECT(refs[0], -1);
    EXPECT(refs[1], 2);
    EXPECT(refs[2], 0);
    EXPECT(refs[4], 2);
    EXPECT(checked_base, 1);
    EXPECT(factions[1].units_active[64], 3);
    EXPECT(factions[1].total_combat_units, 3);

    // A stack, from its head.
    vehs[0].x = 4, vehs[0].next_in_stack = 1, vehs[1].prev_in_stack = 0;
    stack_kill(1);
    EXPECT(veh_count, 1);
    EXPECT(vehs[0].home_base, 3);

    // kill(): a land unit alone; the player's square is then redrawn with
    // its surroundings.
    current_faction = 1, battle = 0;
    kill(0);
    EXPECT(veh_count, 0);
    EXPECT(drawn_radius, 1);
    // A transport at sea takes its cargo with it.
    veh_count = 2;
    vehs[0] = (struct veh){.x = 2, .y = 2, .proto = 66, .faction = 2, .next_in_stack = 1, .prev_in_stack = -1};
    vehs[1] = (struct veh){.x = 2, .y = 2, .proto = 64, .faction = 2, .next_in_stack = -1, .prev_in_stack = 0,
                           .order = 1, .waypoint_x = {0}};  // on board
    protos[66] = (struct unit_proto){.chassis = 1, .carry = 2, .flags = PROTO_FLAG_ACTIVE};
    factions[2].units_active[64] = factions[2].units_active[66] = 1;
    tiles[1 * 1 + 2 * (W / 2)].climate = 0;  // (2, 2) is ocean
    tiles[1 + 2 * (W / 2)].bits |= MAP_UNIT;
    kill(0);
    EXPECT(veh_count, 0);
    EXPECT(drawn_tile, 1);  // another faction's square
    tiles[1 + 2 * (W / 2)].climate = 3 << 5;
    // Loading a transport at sea (2, 2): the AI loads any land unit, up to
    // its capacity; counting marks them and gives the room left.
    {
        static uint8_t humans = 0;
        game_human_factions = &humans;
        tiles[1 + 2 * (W / 2)].climate = 0, tiles[1 + 2 * (W / 2)].bits |= MAP_UNIT;
        veh_count = 4;
        vehs[0] = (struct veh){.x = 2, .y = 2, .proto = 66, .faction = 2, .next_in_stack = 1, .prev_in_stack = -1};
        for (int i = 1; i < 4; i++)
            vehs[i] = (struct veh){.x = 2, .y = 2, .proto = 64, .faction = 2, .next_in_stack = (int16_t)(i < 3 ? i + 1 : -1),
                                   .prev_in_stack = (int16_t)(i - 1), .waypoint_x = {-1}};
        EXPECT(stack_veh(0, 0), 0);
        EXPECT(vehs[1].state & 1, 1);
        EXPECT(vehs[2].state & 1, 1);
        EXPECT(vehs[3].state & 1, 0);
        EXPECT(vehs[1].waypoint_x[0], 0);
        // A human's units: only those sentried for no transport in
        // particular, or any at sea.
        humans = 1 << 2;
        for (int i = 1; i < 4; i++) vehs[i].state = 0, vehs[i].order = 0, vehs[i].waypoint_x[0] = -1;
        vehs[3].order = 1;
        tiles[1 + 2 * (W / 2)].climate = 3 << 5;  // on land now
        EXPECT(stack_veh(0, 0), 1);
        EXPECT(vehs[3].state & 1, 1);
        EXPECT(vehs[1].state & 1, 0);
        // Type 1 takes the cargo and the transport off the map, to one stack.
        stack_veh(0, 1);
        EXPECT(vehs[0].x, -2);
        EXPECT(vehs[3].x, -2);
        EXPECT(vehs[1].x, 2);
        EXPECT(veh_top(3), 0);
        EXPECT(stack_veh(-1, 1), -1);
        EXPECT(stack_veh(-1, 0), 0);
        humans = 0;
    }

    // The player's square with a unit of theirs left on it: just the
    // square, or its surroundings for a unit with deep radar.
    for (int radar = 0; radar < 2; radar++) {
        veh_count = 2, drawn_tile = drawn_radius = 0;
        for (int i = 0; i < 2; i++)
            vehs[i] = (struct veh){.x = 4, .y = 2, .proto = 64, .faction = 1, .next_in_stack = (int16_t)(i ? -1 : 1),
                                   .prev_in_stack = (int16_t)(i ? 0 : -1)};
        factions[1].units_active[64] = 2;
        tiles[2 + 2 * (W / 2)].bits |= MAP_UNIT;
        protos[64].abilities = radar ? 0x2 : 0;
        kill(0);
        EXPECT(veh_count, 1);
        EXPECT(drawn_tile, !radar);
        EXPECT(drawn_radius, radar);
    }
    protos[64].abilities = 0, veh_count = 0;

    // New units: the next slot, homed at the faction's nearest base.
    game_rand = rand_stub, game_more_preferences = &more_prefs, game_draw_state = &draw_state;
    game_graphic_win_redraw = redraw_stub;
    game_computed_base_id = &computed_base, game_base_find_dist = &find_dist;
    veh_count = 0, factions[1].units_active[64] = 0, factions[1].total_combat_units = 0, factions[1].target_x = -1;
    computed_base = 1, defs[1].bonus_count = 0;  // no free drop pods
    bases[0].x = 0, bases[0].y = 0;  // so that base 1 is the nearest
    EXPECT(veh_init(64, 1, 4, 2), 0);
    EXPECT(veh_count, 1);
    EXPECT(vehs[0].home_base, 1);
    EXPECT(computed_base, -1);
    EXPECT(tiles[2 + 2 * (W / 2)].visibility, 1 << 1);  // spotted
    EXPECT(vehs[0].x, 4);
    EXPECT(factions[1].units_active[64], 1);
    EXPECT(factions[1].total_combat_units, 1);
    bases[0].x = 2, bases[0].y = 2, bases[0].faction = 2;  // the nearest now, another faction's
    EXPECT(veh_init(64, 1, 4, 2), 1);
    EXPECT(vehs[1].home_base, -1);
    EXPECT(vehs[1].next_in_stack, 0);  // on top of the first
    // An attacker sent to the target in its region, unless it stays home.
    factions[1].target_x = 6, factions[1].target_y = 4, factions[1].target_units = 0, factions[1].target_kinds = 0;
    rand_value = 1;
    EXPECT(veh_init(64, 1, 4, 2), 2);
    EXPECT(vehs[2].state & 0x40000, 0x40000);
    EXPECT(factions[1].target_units, 1);
    rand_value = 5;  // a multiple of 3 + 2
    EXPECT(veh_init(64, 1, 4, 2), 3);
    EXPECT(vehs[3].state & 0x60000, 0x20000);
    EXPECT(factions[1].target_units, 1);
    // At sea, nothing is sent.
    tiles[2 + 2 * (W / 2)].climate = 0;
    EXPECT(veh_init(64, 1, 4, 2), 4);
    EXPECT(vehs[4].state & 0x60000, 0);
    tiles[2 + 2 * (W / 2)].climate = 3 << 5;
    veh_count = VEH_COUNT;
    EXPECT(veh_init(64, 1, 4, 2), -1);
    veh_count = 0, factions[1].target_x = -1;

    // Waking on spotting a unit: by the preference for the treaty, the flag
    // as it is; in a network game always, but not for a pact partner.
    static int32_t prefs;
    game_preferences = &prefs;
    veh_count = 3;
    for (int i = 0; i < 3; i++)
        vehs[i] = (struct veh){.x = 4, .y = 2, .proto = 64, .faction = 1, .next_in_stack = (int16_t)(i < 2 ? i + 1 : -1),
                               .prev_in_stack = (int16_t)(i - 1)};
    vehs[1].proto = 65;  // an air unit
    prefs = 0x800000;
    EXPECT(want_to_wake(2, 0, -1), 0x800000);
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(want_to_wake(2, 0, -1), 0);
    factions[1].diplo_status[2] = TREATY_TREATY, prefs = 0x200000;
    EXPECT(want_to_wake(2, 0, -1), 0x200000);
    factions[1].diplo_status[2] = 0;
    // Only for the same triad: not for the air unit, unless flagged.
    prefs = 0x40800000;
    EXPECT(want_to_wake(2, 0, 2), 0x800000);
    EXPECT(want_to_wake(2, 0, 1), 0);
    vehs[0].state = 0x1000200;
    EXPECT(want_to_wake(2, 0, 1), 0x800000);
    EXPECT(want_to_wake(2, 1, 0), 0);  // an air unit spotting a land unit
    vehs[0].state = 0;
    net_mp = 1, prefs = 0;
    EXPECT(want_to_wake(2, 0, -1), 1);
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(want_to_wake(2, 0, -1), 0);
    net_mp = 0, factions[1].diplo_status[2] = 0;
    // An air unit aloft on its way to its own base doesn't stop.
    prefs = -1;
    vehs[1].order = 0x18, vehs[1].movement_turns = 1, vehs[1].waypoint_x[0] = 6, vehs[1].waypoint_y[0] = 4;
    tiles[3 + 4 * (W / 2)].bits |= MAP_BASE, tiles[3 + 4 * (W / 2)].owner_site = 1;
    EXPECT(want_to_wake(2, 1, -1), 0);
    vehs[1].movement_turns = 0;
    EXPECT(want_to_wake(2, 1, -1) != 0, 1);
    tiles[3 + 4 * (W / 2)].bits &= ~(uint32_t)MAP_BASE, tiles[3 + 4 * (W / 2)].owner_site = 0xf;
    vehs[1].order = 0;

    // Waking a stack: units on board wake, but not on an air transport
    // away from a base or an airbase.
    vehs[0].order = 1, vehs[0].waypoint_x[0] = 1, vehs[0].state = 0x4000;
    vehs[2].order = 1, vehs[2].waypoint_x[0] = -1, vehs[2].state = 0x4001;
    wake_stack(2);
    EXPECT(vehs[0].order, 1);
    EXPECT(vehs[2].order, 0);
    EXPECT(vehs[2].state, 1);
    tiles[2 + 2 * (W / 2)].bits |= 0x40000;  // an airbase
    wake_stack(1);
    EXPECT(vehs[0].order, 0);
    EXPECT(vehs[0].state, 0);
    tiles[2 + 2 * (W / 2)].bits &= ~0x40000u;
    vehs[1].order = 1;  // an air unit wakes anywhere
    wake_stack(0);
    EXPECT(vehs[1].order, 0);
    veh_count = 0;

    // A new design is wanted unless one of the faction's finished designs
    // of the same plan and triad is as good and as fast.
    chassis[3].speed = 10, chassis[3].triad = TRIAD_AIR;
    protos[3] = (struct unit_proto){.chassis = 3, .weapon = 1, .armor = 2, .flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT};
    EXPECT(wants_prototype(3, 1), 1);
    protos[3].chassis = 2;
    EXPECT(wants_prototype(3, 1), 0);
    protos[3].armor = 1;  // armor 1's value beats armor 2's
    EXPECT(wants_prototype(3, 1), 1);
    protos[3].armor = 2, protos[3].weapon = 0;  // not a psi weapon
    EXPECT(wants_prototype(3, 1), 1);
    protos[3].weapon = 1, protos[3].flags = PROTO_FLAG_ACTIVE;
    EXPECT(wants_prototype(3, 1), 0);
    protos[3] = (struct unit_proto){0};

    // The battle report: up to 4 non-zero modifiers a side.
    game_battle_mod_count = mod_counts, game_battle_mod = mods, game_battle_mod_label = mod_labels;
    game_battle_state = &mod_counts[2], game_battle_terrain = &terrain, game_battle_morale = morales;
    game_labels = &labels_ptr;
    label_list[91] = "Rocky", label_list[338] = "Fungus", label_list[291] = "Forest";
    mod_counts[0] = 3, mod_counts[2] = 1;
    battle_init();
    EXPECT(mod_counts[0] + mod_counts[2], 0);
    add_bat(1, 25, "Rocky");
    add_bat(1, 0, "None");
    EXPECT(mod_counts[1], 1);
    EXPECT(mods[4], 25);
    EXPECT(strcmp(mod_labels[4], "Rocky"), 0);
    mod_counts[0] = 4;
    add_bat(0, 10, "Full");
    EXPECT(mod_counts[0], 4);
    EXPECT(mods[3], 0);

    // Terrain in combat: rocks, fungus (more for a faction with the Pholus
    // Mutagen, none against native life), forest against land units only.
    veh_count = 2;
    vehs[0] = (struct veh){.x = 4, .y = 2, .proto = 64, .faction = 1, .home_base = -1, .next_in_stack = -1, .prev_in_stack = -1};
    vehs[1] = (struct veh){.x = 5, .y = 3, .proto = 64, .faction = 2, .home_base = -1, .next_in_stack = -1, .prev_in_stack = -1};
    struct map_tile *sq = &tiles[2 + 2 * (W / 2)];
    EXPECT(defense_value(1, 4, 2, 0, 1), 2);
    sq->rocky_lock = 0x80;
    EXPECT(defense_value(1, 4, 2, 0, 1), 3);
    EXPECT(strcmp(terrain, "Rocky"), 0);
    sq->rocky_lock = 0, sq->bits = MAP_FUNGUS;
    EXPECT(defense_value(1, 4, 2, 0, 1), 3);
    EXPECT(strcmp(terrain, "Fungus"), 0);
    projects[16] = 1;  // the Pholus Mutagen, faction 1's
    EXPECT(defense_value(1, 4, 2, 0, 1), 4);
    EXPECT(defense_value(2, 4, 2, 1, 0), 2);  // its units attack in fungus as natives do
    EXPECT(defense_value(2, 4, 2, 1, -1), 3);
    projects[16] = -1, vehs[1].faction = 0;
    EXPECT(defense_value(1, 4, 2, 0, 1), 2);
    vehs[1].faction = 2, sq->bits = MAP_FOREST;
    EXPECT(defense_value(1, 4, 2, 0, -1), 3);
    vehs[1].proto = 65;
    EXPECT(defense_value(1, 4, 2, 0, 1), 2);
    sq->climate = 2 << 5;
    EXPECT(defense_value(1, 4, 2, 0, -1), 2);
    sq->climate = 3 << 5, sq->bits = 0, vehs[1].proto = 64;

    // Strengths: the weapon or armor times morale plus 6; a Children's
    // Creche at the base takes MORALE off, softened.
    int m = morale_veh(0, 1, 0);
    EXPECT(get_basic_offense(0, 1, 0, 0, 0), offense_proto(64, 1, 0) * (m + 6) * 4);
    EXPECT(morales[0], m);
    bases[1].x = 4, bases[1].y = 2, sq->bits |= MAP_BASE;  // base 1 on the attacker's square
    bases[1].facilities_built[0] = 1 << 2, factions[1].se[SE_MORALE] = -3;
    m = morale_veh(0, 1, 0);
    EXPECT(get_basic_offense(0, 1, 0, 0, 0), offense_proto(64, 1, 0) * (m + 1 + 2 + 6) * 4);
    EXPECT(morales[0], m + 3);
    factions[1].se_pending[SE_MORALE] = 2;
    int b1 = m + 4 > 6 ? 6 : m + 4;
    EXPECT(get_basic_offense(0, 1, 0, 0, 1), offense_proto(64, 1, 0) * (b1 + 6) * 4);
    EXPECT(morales[1], b1);
    bases[1].x = 6, bases[1].y = 4, sq->bits &= ~(uint32_t)MAP_BASE;
    bases[1].facilities_built[0] = 0, factions[1].se[SE_MORALE] = 0, factions[1].se_pending[SE_MORALE] = 0;
    m = morale_veh(1, 1, 0);
    m = m < 1 ? 1 : m;
    EXPECT(get_basic_defense(1, 0, 0, 0), armor_proto(64, 0, 0) * (m + 6));
    EXPECT(morales[1], m);
    protos[64].plan = 12;  // an alien artifact
    EXPECT(get_basic_defense(1, 0, 0, 0), 1);
    protos[64].plan = 0;

    // Moves: a road's cost along roads, none between bases, rocks and
    // forest unless fast, and fungus.
    struct map_tile *to = &tiles[2 + 3 * (W / 2)];  // (5, 3), next to (4, 2)
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 3);
    sq->bits = to->bits = MAP_ROAD;
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 1);
    EXPECT(hex_cost(64, 0, 4, 2, 5, 3, 0), 3);
    sq->bits = MAP_BASE, to->bits = 0x8;  // a mag tube
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 0);
    sq->bits = 0, to->bits = MAP_FOREST, to->rocky_lock = 0x80;
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 9);
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 1), 3);
    to->bits = MAP_FUNGUS, to->rocky_lock = 0;
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 9);
    EXPECT(hex_cost(64, 0, 4, 2, 5, 3, 0), 1);
    factions[1].se[SE_PLANET] = 1;
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), speed_proto(64));
    factions[1].se[SE_PLANET] = 0, to->bits = MAP_RIVER, sq->bits = MAP_RIVER;
    EXPECT(hex_cost(64, 1, 4, 2, 5, 3, 0), 1);
    sq->bits = 0, to->bits = 0, to->climate = 2 << 5;
    EXPECT(hex_cost(65, 1, 4, 2, 5, 3, 0), 3);
    to->climate = 3 << 5;

    // A battle: the defender's terrain and the base's defenses double up
    // against the attacker's x4; a road attack counts; a probe team
    // attacking quadruples the defense.
    game_strings_get = strings_get_stub, game_battle_text = battle_text;
    for (int i = 0; i < 1200; i++)
        if (!label_list[i]) label_list[i] = "Label";
    vehs[1].x = 4, vehs[1].y = 2;  // on the attacker's square, for the test
    int att = 0, def = 0;
    battle_compute(0, 1, &att, &def, 0);
    EXPECT(att, get_basic_offense(0, 1, 0, 0, 0));
    EXPECT(def, get_basic_defense(1, 0, 0, 0) * 4);
    EXPECT(mod_counts[0] + mod_counts[1], 0);
    rules_basic.combat_atk_road = 25;
    battle_compute(0, 1, &att, &def, 0);
    EXPECT(att, get_basic_offense(0, 1, 0, 0, 0) * 125 / 100);
    EXPECT(mod_counts[0], 1);
    EXPECT(mods[0], 25);
    rules_basic.combat_atk_road = 0;
    bases[1].x = 4, bases[1].y = 2, sq->bits |= MAP_BASE, bases[1].facilities_built[0] = 1 << 4;  // a perimeter defense
    battle_compute(0, 1, NULL, &def, 0);
    EXPECT(def, get_basic_defense(1, 0, 0, 0) * 8);
    EXPECT(mods[4], 100);
    bases[1].x = 6, bases[1].y = 4, sq->bits &= ~(uint32_t)MAP_BASE, bases[1].facilities_built[0] = 0;
    protos[64].plan = 11;
    battle_compute(0, 1, &att, &def, 0);
    EXPECT(def, get_basic_defense(1, 0, 0, 0) * 4);
    protos[64].plan = 0;

    // The best defender: the undamaged unit, unless the other is told to
    // defend.
    veh_count = 3;
    vehs[2] = vehs[1];
    vehs[1].next_in_stack = 2, vehs[2].prev_in_stack = 1, vehs[2].damage = 8;
    EXPECT(best_defender(2, 0, 0), 1);
    vehs[2].state = 0x80;
    EXPECT(best_defender(2, 0, 0), 2);
    EXPECT(best_defender(1, -1, 0), 2);

    // The AI's parts: the fastest chassis (or one exactly as fast), the
    // strongest weapon and armor within a cost, a weapon by mode.
    for (int i = 0; i < CHASSIS_COUNT; i++) chassis[i].preq_tech = (int16_t)(i < 4 ? TECH_NONE : TECH_DISABLED);
    chassis[3].speed = 10, chassis[3].triad = TRIAD_AIR;
    EXPECT(pick_chassis(1, TRIAD_AIR, 0), 3);
    EXPECT(pick_chassis(1, -1, 4), 1);
    EXPECT(pick_chassis(1, TRIAD_LAND, 2), -1);
    for (int i = 0; i < WEAPON_COUNT; i++) weapons[i].preq_tech = (int16_t)(i < 4 ? TECH_NONE : TECH_DISABLED);
    weapons[0].cost = 2, weapons[2].offense = 6, weapons[2].cost = 5, weapons[3].mode = 7, weapons[3].cost = 1;
    EXPECT(weapon_budget(1, 3, 0), 0);
    EXPECT(weapon_budget(1, 9, 0), 2);
    EXPECT(weapon_budget(1, -7, 0), 3);
    for (int i = 0; i < ARMOR_COUNT; i++) armor[i].preq_tech = (int16_t)(i < 3 ? TECH_NONE : TECH_DISABLED);
    armor[0].cost = 1, armor[1].cost = 3;
    EXPECT(armor_budget(1, 2), 0);
    EXPECT(armor_budget(1, 5), 1);

    // A design's cost, with an ability; and a design filled in.
    chassis[0].cost = 1;
    EXPECT(proto_cost(0, 0, 1, 0, 1), 3);
    abilities[4].cost = 2;
    EXPECT(proto_cost(0, 0, 1, 1u << 4, 1), 5);
    make_proto(70, 0, 0, 1, 0, 1);
    EXPECT(protos[70].cost, 3);
    EXPECT(protos[70].plan, 1);   // combat: weapon as strong as the armor
    EXPECT(protos[70].group, 3);
    EXPECT(protos[70].flags, PROTO_FLAG_ACTIVE);
    EXPECT(protos[70].combat_factions, 1 << 1);
    EXPECT(protos[70].icon, 0xff);
    protos[70] = (struct unit_proto){0};

    // A unit sent somewhere.
    go_to(0, 7, 3, 1);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].status_icon, 7);
    EXPECT(vehs[0].waypoint_x[0] * 10 + vehs[0].waypoint_y[0], 31);

    // Upgrade costs: the new design's cost and its parts' and speed's
    // gains; much more for a design not yet built; halved with the Nano
    // Factory.
    EXPECT(upgrade_cost(1, 65, 64), 8 + 7);
    EXPECT(upgrade_cost(1, 64, 65), 15 * 16 + 2 + 3);
    projects[22] = 1;
    EXPECT(upgrade_cost(1, 64, 65), (15 * 16 + 2 + 3) / 2);
    projects[22] = -1;

    // An upgrade: paid for, the unit turned over (with a level of morale
    // for high morale training), the queue changed, the old design retired.
    {
        static uint8_t humans = 0;
        static int32_t difficulty = 3, ranked[2] = {2, 3}, warnings = 0, proto_upkeep = 0;
        game_human_factions = &humans, game_difficulty = &difficulty, game_ranked_faction = ranked;
        game_warnings = &warnings, game_base_proto_upkeep = &proto_upkeep;
        game_draw_map = draw_map_stub, game_console_update_data = console_update_stub;
        veh_count = 1;
        vehs[0] = (struct veh){.x = 2, .y = 2, .proto = 64, .faction = 1, .morale = 2, .next_in_stack = -1, .prev_in_stack = -1};
        factions[1].units_active[64] = 1, factions[1].energy_credits = 1000;
        bases[1].faction = 1, bases[1].queue_size = 1, bases[1].queue[0] = 3, bases[1].queue[1] = 64;
        protos[65].abilities |= 0x4000;
        do_upgrade(1, 65, 64);
        EXPECT(factions[1].energy_credits, 1000 - 150);
        EXPECT(vehs[0].proto, 65);
        EXPECT(vehs[0].morale, 3);
        EXPECT(factions[1].units_active[64] * 10 + factions[1].units_active[65], 1);
        EXPECT(bases[1].queue[1], 65);
        EXPECT(protos[64].flags & PROTO_FLAG_ACTIVE, 0);
        EXPECT(drawn_map * 10 + console_updates, 1);  // the player's, not human
        protos[64].flags |= PROTO_FLAG_ACTIVE;

        // The AI's choice: within an eighth of its energy for each step
        // of its priorities, less the discount.
        vehs[0].proto = 64, factions[1].units_active[64] = 1, factions[1].units_active[65] = 0;
        factions[1].energy_credits = 1200, factions[1].ai_power = 0, factions[1].ai_tech = 0;
        factions[2].ranking = 6, factions[3].energy_credits = 5000;
        EXPECT(upgrade_prototype(1, 65, 64, 1), 1);  // 150 - 3 * 150 / 10 / 2 = 128 <= 150
        EXPECT(vehs[0].proto, 65);
        vehs[0].proto = 64, factions[1].units_active[64] = 1, factions[1].units_active[65] = 0;
        factions[1].energy_credits = 1000;
        EXPECT(upgrade_prototype(1, 65, 64, 1), 0);  // 128 > 125
        factions[3].energy_credits = 0;
        EXPECT(upgrade_prototype(1, 65, 64, 1), 0);  // more energy than the other: 150 - 11 > 125
        factions[1].energy_credits = 2000;
        EXPECT(upgrade_prototype(1, 65, 64, 1), 1);  // no discount, but 150 <= 250
        state = GAME_STATE_DONE;
        EXPECT(upgrade_prototype(1, 65, 64, 1), 0);
        state = 0;

        // Obsolete designs: the faction's units of a design of the same
        // plan, group, triad and chassis, the new one's parts no better.
        vehs[0].proto = 64, factions[1].units_active[64] = 1, factions[1].units_active[65] = 0;
        factions[1].energy_credits = 5000, protos[64].flags |= PROTO_FLAG_ACTIVE;
        protos[66] = protos[64];
        protos[66].flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT, protos[66].armor = 0, protos[64].obsolete_factions = 1 << 1;
        upgrade_prototypes(1, 66);
        EXPECT(vehs[0].proto, 66);
        vehs[0].proto = 64, factions[1].units_active[64] = 1, factions[1].units_active[66] = 0;
        protos[66].weapon = 2;  // a stronger weapon
        upgrade_prototypes(1, 66);
        EXPECT(vehs[0].proto, 64);
        protos[66] = (struct unit_proto){0}, protos[64].obsolete_factions = 0, protos[65].abilities &= ~0x4000u;
        bases[1].queue_size = 0;

        // Promotion: a level, unless a missile; the player told.
        vehs[0].morale = 0;
        promote(0);
        EXPECT(vehs[0].morale, 1);
        chassis[0].missile = 1;
        promote(0);
        EXPECT(vehs[0].morale, 1);
        chassis[0].missile = 0;
        vehs[0].morale = 6;
        promote(0);
        EXPECT(vehs[0].morale, 6);
        veh_count = 0;
    }

    // Abilities: one the faction gets free isn't added, nor one without
    // its tech; nothing is added to a design with abilities.
    uint32_t abls = 0;
    defs[1].bonus_count = 1;
    add_abil(&abls, 1u << 4, 1);
    EXPECT(abls, 0);
    add_abil(&abls, 1u << 5, 1);
    EXPECT(abls, 0);
    abilities[5].preq_tech = TECH_NONE;
    add_abil(&abls, (1u << 5) | (1u << 9), 1);
    EXPECT(abls, 1u << 5);
    add_abil(&abls, 1u << 6, 1);
    EXPECT(abls, 1u << 5);
    abilities[4].flags = 0x1 | 0x8;  // land, combat
    EXPECT(abil_cond(4, 0, 0, 1), 1);
    EXPECT(abil_cond(4, 1, 0, 1), 0);  // at sea
    EXPECT(abil_cond(4, 0, 3, 1), 0);  // no weapon
    abilities[4].flags |= 0x20 | 0x100;  // non-combat, transports only
    EXPECT(abil_cond(4, 0, 3, 1), 1);
    weapons[3].mode = 8;
    EXPECT(abil_cond(4, 0, 3, 1), 0);
    weapons[3].mode = 7, abilities[4].flags |= 0x80;  // not psi
    EXPECT(abil_cond(4, 0, 3, 2), 0);
    abilities[24].flags = 0xff & ~0x80;
    EXPECT(abil_cond(24, 0, 0, 0), 1);
    expansion = 0;
    EXPECT(abil_cond(24, 0, 0, 0), 0);
    expansion = 1;

    // Guards for a region, and none past the land regions.
    factions[1].flags = 0;
    factions[1].region_total_bases[3] = 5;
    EXPECT(guard_check(1, 3), 3);
    factions[1].region_base_plan[3] = PLAN_COLONY;
    EXPECT(guard_check(1, 3), 2);
    factions[1].ai_fight = -1;
    EXPECT(guard_check(1, 3), 1);
    factions[1].region_base_plan[3] = PLAN_DEFENSIVE;
    EXPECT(guard_check(1, 3), 5);
    EXPECT(guard_check(1, 64), 0);
    factions[1].ai_fight = 0, factions[1].region_base_plan[3] = 0;

    // The enemy's best: from factions at war first, else those with no treaty.
    factions[2].best_weapon_value = 9, factions[3].best_weapon_value = 5, factions[3].best_land_speed = 2;
    factions[1].diplo_status[2] = TREATY_COMMLINK, factions[1].diplo_status[3] = TREATY_COMMLINK | TREATY_VENDETTA;
    enemy_capabilities(1);
    EXPECT(factions[1].best_weapon_value, 6);
    EXPECT(factions[1].enemy_best_weapon_value, 5);
    EXPECT(factions[1].enemy_best_land_speed, 2);
    factions[1].diplo_status[3] = TREATY_PACT;
    enemy_capabilities(1);
    EXPECT(factions[1].enemy_best_weapon_value, 9);
    EXPECT(factions[1].enemy_best_land_speed, 0);
    factions[1].diplo_status[2] = factions[1].diplo_status[3] = 0;

    // A port: by the sea.
    EXPECT(is_port(1, 0), 0);
    tiles[3 + 3 * (W / 2)].climate = 0;  // (7, 3) is ocean
    EXPECT(is_port(1, 0) != 0, 1);
    tiles[3 + 3 * (W / 2)].climate = 3 << 5;

    // Convoys: not on a square a base works.
    bases[0].worked_tiles = bases[1].worked_tiles = 0;
    EXPECT(can_convoy(6, 3), 1);
    bases[1].worked_tiles = 1u << 1;  // (6, 3)
    EXPECT(can_convoy(6, 3), 0);
    bases[1].worked_tiles = 0;

    // A sensor: in its territory next to its base, on rocky or arid
    // ground, but not on a mine or by another of its sensors.
    {
        static int32_t seed = 0;
        game_map_seed = &seed;
        struct map_tile *b = &tiles[3 + 4 * (W / 2)], *t = &tiles[2 + 3 * (W / 2)];  // (6, 4) and (5, 3)
        b->bits |= MAP_BASE;
        EXPECT(good_sensor(1, 5, 3), 0);  // no one's territory
        for (int i = 0; i < W / 2 * H; i++) tiles[i].territory = 1;
        t->climate = 3 << 5 | 1 << 3;
        EXPECT(good_sensor(1, 5, 3), 0);  // rainy flat land
        t->climate = 3 << 5;  // arid
        EXPECT(good_sensor(1, 5, 3), 1);
        t->bits |= MAP_MINE;
        EXPECT(good_sensor(1, 5, 3), 0);
        t->bits &= ~(uint32_t)MAP_MINE, tiles[1 + 2 * (W / 2)].bits |= MAP_SENSOR;  // (3, 2)
        EXPECT(good_sensor(1, 5, 3), 1);  // out of reach
        tiles[3 + 2 * (W / 2)].bits |= MAP_SENSOR;  // (6, 2)
        EXPECT(good_sensor(1, 5, 3), 0);
        tiles[1 + 2 * (W / 2)].bits &= ~MAP_SENSOR, tiles[3 + 2 * (W / 2)].bits &= ~MAP_SENSOR;
        for (int i = 0; i < W / 2 * H; i++) tiles[i].territory = 0;
        t->climate = 3 << 5, b->bits &= ~(uint32_t)MAP_BASE;
    }

    // The AI's odds: by the cost of what it would destroy against its own
    // unit's, half again on land where it means to attack, doubled at a
    // goal for a unit in its element.
    game_at_goal = at_goal_stub, factions[1].region_base_plan[1] = 0;
    veh_count = 2;
    vehs[0] = (struct veh){.x = 2, .y = 2, .proto = 64, .faction = 1, .next_in_stack = -1, .prev_in_stack = -1};
    vehs[1] = (struct veh){.x = 4, .y = 2, .proto = 65, .faction = 2, .next_in_stack = -1, .prev_in_stack = -1};
    EXPECT(compute_odds(100, 1, 0, 1, -1), 8 * 100 / 10 * 3 / 2);
    goal_here = 1;
    EXPECT(compute_odds(100, 1, 0, 1, -1), 8 * 100 / 10 * 3 / 2 * 2);
    goal_here = 0;
    vehs[1].flags = 0x20;
    EXPECT(compute_odds(100, 1, 0, 1, -1), 8 * 100 / 10 * 3 / 2 * 4);
    vehs[1].flags = 0;
    protos[64].plan = PLAN_AIR_SUPERIORITY;
    EXPECT(compute_odds(100, 1, 0, 1, -1), 80);  // against an air unit
    protos[64].plan = 0;
    veh_count = 0;
    veh_count = 0;
    if (failures) return 1;
    printf("engine_veh: all tests passed\n");
    return 0;
}
