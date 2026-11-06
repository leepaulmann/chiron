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
static int32_t lift[6];
static uint8_t achieved[TECH_COUNT];
static struct base bases[2];
static struct veh vehs[2];
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
static int proto_cost_stub(int c, int w, int a, int abl, int r) { return 20; }
static int veh_at_stub(int x, int y) { return -1; }

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_rules = &rules, game_rules_basic = &rules_basic, game_turn = &turn;
    game_bases = bases, game_base_count = &base_count, game_vehs = vehs, game_veh_count = &veh_count;
    game_factions = factions, game_faction_defs = defs, game_protos = protos, game_chassis = chassis;
    game_weapons = weapons, game_armor = armor, game_reactors = reactors, game_abilities = abilities;
    game_tech = techs, game_tech_achieved = achieved, game_expansion = &expansion, game_secret_projects = projects;
    game_net_multiplayer = &net_mp, game_tectonic_detonations = tectonic;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_veh_lift_x = &lift[0], game_veh_lifted = &lift[2], game_veh_lift_y = &lift[3], game_veh_at_rebuilt = &lift[5];
    game_state = &state;
    game_is_objective = objective_stub, game_proto_cost = proto_cost_stub, game_veh_at = veh_at_stub;
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
    EXPECT(veh_cost(64, 0, &charged), 10 + 10);
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

    if (failures) return 1;
    printf("engine_veh: all tests passed\n");
    return 0;
}
