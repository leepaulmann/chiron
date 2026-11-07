// Unit test for the reimplemented AI strategy, on a small made-up map of
// land: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/map.h"
#include "../src/engine/strategy.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static uint8_t abstract[4], *abstract_ptr = abstract;
static int32_t width = W, height = H, half_width = W / 2, abstract_width = 4, abstract_height = 2, area = W / 2 * H;
static uint32_t flat, state;
static struct base bases[2];
static int32_t base_count = 2, veh_count = 1, turn = 5, find_dist, current = 1, prefs, rankings[FACTION_COUNT];
static struct continent continents[REGION_COUNT];
static struct faction_data factions[FACTION_COUNT];
static struct veh vehs[1];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static struct rules_armor armor[ARMOR_COUNT];
static struct rules_tech techs[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static int32_t expansion = 1;
static uint32_t rules;
static uint8_t humans, alive = 1 << 1 | 1 << 2;
static int32_t near_x[9] = {1, 2, 1, 0, -1, -2, -1, 0, 0}, near_y[9] = {-1, 0, 1, 2, 1, 0, -1, -2, 0};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static int failures, resets, region_resets, kills;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }

// Stand-ins: goals go in the first free slot; strengths are fixed.
static void add_goal_stub(int f, int type, int pri, int x, int y, int base) {
    for (int g = 0; g < GOAL_COUNT; g++)
        if (factions[f].goals[g].type < 0) {
            factions[f].goals[g] = (struct goal){(int16_t)type, (int16_t)pri, x, y, base};
            return;
        }
}
static int veh_at_stub(int x, int y) {
    for (int i = 0; i < veh_count; i++)
        if (vehs[i].x == x && vehs[i].y == y) return i;
    return -1;
}
static int identity(int v) { return v; }
static int zero1(int a) { return 0; }
static int one1(int a) { return 1; }
static int zero2(int a, int b) { return 0; }
static int zero3(int a, int b, int c) { return 0; }
static int zero4(int a, int b, int c, int d) { return 0; }
static int stack_stub(int v, int t, int a, int b, int c) { return 0; }
static int weap_stub(int w, int f) { return 4; }
static int arm_stub(int a, int f) { return 2; }
static int morale_stub(int v, int a, int b) { return 2; }
static int terraform_stub(int x, int y, int f) { return 9999; }
static int contiguous_stub(int f, int e, int r, int s) { return 1; }
static void base_reset_stub(int b, int u) { resets++; }
static void bases_reset_stub(int r, int f, int same) { region_resets++; }
static void nothing1(int a) {}
static void del_site_stub(int f, int t, int x, int y, int u) {}
static int __attribute__((thiscall)) upgrade_stub(void *c, int v) { return 1; }
static int kill_stub(int v) { return kills++; }
static int world_site_stub(int x, int y, int u) { return 7; }

static void reset_goals(int f) {
    for (int g = 0; g < GOAL_COUNT; g++) factions[f].goals[g] = (struct goal){-1, 0, -1, -1, -1};
}

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_abstract_map = &abstract_ptr, game_abstract_width = &abstract_width;
    game_abstract_height = &abstract_height, game_map_area = &area, game_state = &state;
    game_bases = bases, game_base_count = &base_count, game_vehs = vehs, game_veh_count = &veh_count;
    game_factions = factions, game_protos = protos, game_chassis = chassis, game_continents = continents;
    game_turn = &turn, game_human_factions = &humans, game_factions_alive = &alive;
    game_current_faction = &current, game_preferences = &prefs, game_faction_rankings = rankings;
    game_base_find_dist = &find_dist, game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_radius_x = radius_x, game_radius_y = radius_y;
    for (int i = 1; i < RADIUS_SQUARES; i++) radius_x[i] = i < 9 ? near_x[i - 1] : 1000, radius_y[i] = i < 9 ? near_y[i - 1] : 0;
    game_add_goal = add_goal_stub, game_veh_at = veh_at_stub, game_stack_fix = identity;
    game_stack_check = stack_stub;
    game_rules = &rules, game_terraform_cost = terraform_stub, game_base_reset = base_reset_stub;
    game_weapons = weapons, game_armor = armor, game_tech = techs, game_tech_achieved = achieved;
    game_expansion = &expansion, game_weap_val = zero2, game_arm_val = zero2, game_planet_buster = zero1;
    game_console_upgrade = upgrade_stub, game_kill = kill_stub, game_veh_kill = kill_stub, game_veh_sleep = nothing1;
    game_garrison_check = one1, game_veh_cargo = zero1, game_weap_strat = weap_stub, game_arm_strat = arm_stub;
    game_morale_veh = morale_stub, game_great_satan = zero2, game_contiguous = contiguous_stub;
    game_wants_to_attack = zero3, game_bases_reset = bases_reset_stub, game_has_abil = (void *)zero2;
    game_want_monolith = zero1, game_del_site = del_site_stub, game_world_site = world_site_stub;
    game_wipe_goals = nothing1;
    (void)zero4;

    // Land everywhere (altitude 4), one region, nothing on it; a base of
    // faction 1 at (2, 2) with an offensive unit, and faction 2's at (6, 2),
    // at war with it.
    for (int i = 0; i < W / 2 * H; i++) tiles[i] = (struct map_tile){.climate = 4 << 5, .owner_site = 0xf, .region = 1, .territory = -1};
    continents[1].tile_count = W / 2 * H;
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .faction_former = 1, .pop_size = 3, .queue_size = 0};
    bases[1] = (struct base){.x = 6, .y = 2, .faction = 2, .faction_former = 2, .pop_size = 2, .visibility = 1 << 1};
    at(2, 2)->bits = MAP_BASE | MAP_UNIT, at(2, 2)->owner_site = 1;
    at(6, 2)->bits = MAP_BASE, at(6, 2)->owner_site = 2;
    vehs[0] = (struct veh){.x = 2, .y = 2, .faction = 1, .proto = 0, .home_base = 0};
    protos[0].plan = PLAN_OFFENSIVE;
    for (int f = 0; f < FACTION_COUNT; f++) factions[f].target_x = factions[f].target_y = -1, factions[f].attack_target_base = -1;
    factions[1].diplo_status[2] = TREATY_VENDETTA | TREATY_COMMLINK;
    factions[2].diplo_status[1] = TREATY_VENDETTA | TREATY_COMMLINK;
    factions[2].region_total_bases[1] = 1;  // its own strategy would count this
    reset_goals(1);
    enemy_strategy(1);
    const struct faction_data *f = &factions[1];
    // The census.
    EXPECT(f->base_count, 1);
    EXPECT(f->pop_total, 3);
    EXPECT(f->region_total_bases[1], 1);
    EXPECT(f->total_combat_units, 1);
    EXPECT(f->region_total_combat_units[1], 1);
    EXPECT(f->units_active[0], 1);
    EXPECT(f->mil_strength, (4 + 2) * (2 + 6) / 8);
    EXPECT(f->mil_attack, 4 * 8 / 8);
    EXPECT(f->region_force_rating[1], 4);
    // Goals: its own base to defend (nothing defends it as the stand-in
    // sees it), the undefended rival base to defend against and scout, and
    // to attack: 2, +1 for a region with its bases, +1 for no Perimeter
    // Defense.
    EXPECT(f->goals[0].type, GOAL_DEFEND);
    EXPECT(f->goals[0].priority, 4);
    EXPECT(f->goals[1].type, GOAL_DEFEND);
    EXPECT(f->goals[2].type, GOAL_SCOUT);
    EXPECT(f->goals[3].type, GOAL_ATTACK);
    EXPECT(f->goals[3].priority, 4);
    // A rival weaker in the region it can reach: offense.
    EXPECT(f->region_base_plan[1], PLAN_OFFENSIVE);
    EXPECT(f->region_flags[1] & 0x60, 0x60);  // a rival's bases, at war
    EXPECT(region_resets, 62);  // every land region but the bad ones
    // The base nearest the rival becomes the target, and the unit gathers
    // there.
    EXPECT(f->target_x, 2);
    EXPECT(f->target_units, 1);
    EXPECT(vehs[0].order, 0x18);
    EXPECT(vehs[0].status_icon, '0');
    EXPECT(vehs[0].waypoint_x[0], 2);

    // With targets ruled out the unit takes the attack goal, the cheapest
    // for its priority.
    factions[1].flags |= 0x20000000;
    factions[1].target_x = factions[1].target_y = -1, factions[1].target_units = 0;
    vehs[0].state = 0, vehs[0].order = 0, vehs[0].status_icon = 0;
    reset_goals(1);
    enemy_strategy(1);
    EXPECT(f->target_x, -1);
    EXPECT(vehs[0].status_icon, '1');
    EXPECT(vehs[0].waypoint_x[0], 6);
    EXPECT(vehs[0].waypoint_y0, 2);

    // A base stockpiling energy rethinks its build.
    bases[0].queue[0] = -FAC_STOCKPILE_ENERGY;
    reset_goals(1);
    resets = 0;
    enemy_strategy(1);
    EXPECT(resets, 1);
    // Not for a human, and no goals are taken by units not on automatic.
    humans = 1 << 1, resets = 0;
    vehs[0].state = 0, vehs[0].order = 0, vehs[0].status_icon = 0;
    reset_goals(1);
    enemy_strategy(1);
    EXPECT(resets, 0);
    EXPECT(vehs[0].status_icon, 0);
    EXPECT(kills, 0);

    // Goals and sites, on faction 3's.
    {
        struct faction_data *F = &factions[3];
        init_goals(3);
        EXPECT(F->goals[0].type, -1);
        EXPECT(F->sites[SITE_COUNT - 1].type, -1);
        add_goal(3, 2, 3, 4, 2, 7);
        EXPECT(F->goals[0].type, 2);
        EXPECT(F->goals[0].base, 7);
        EXPECT(at_goal(3, 2, 4, 2), 1);
        EXPECT(at_goal(3, 3, 4, 2), 0);
        add_goal(3, 2, 5, 4, 2, 9);  // raises the priority, keeps the base
        EXPECT(F->goals[0].priority, 5);
        EXPECT(F->goals[0].base, 7);
        add_goal(3, 2, 1, 4, 2, 9);  // never lowers it
        EXPECT(F->goals[0].priority, 5);
        add_goal(3, 2, 1, W, 2, -1);  // off the map
        add_goal(3, 2, 1, 0, -1, -1);
        EXPECT(F->goals[1].type, -1);
        // Full: the new goal replaces the one of least value below it.
        for (int i = 0; i < GOAL_COUNT; i++) F->goals[i] = (struct goal){1, 6, i % W, 0, 0};
        F->goals[10].priority = 4, F->goals[20].priority = -3, F->goals[30].priority = 2;
        add_goal(3, 5, 5, 1, 3, -1);
        EXPECT(F->goals[20].type, 5);  // worth 100 - 3, against 20 - 4 and 20 - 2
        EXPECT(F->goals[30].type, 1);
        add_goal(3, 5, 1, 2, 3, -1);  // nothing left below priority 1
        EXPECT(at_goal(3, 5, 2, 3), 0);
        // A new site adds a goal to match.
        init_goals(3);
        add_site(3, 3, 2, 4, 2);
        EXPECT(F->sites[0].type, 3);
        EXPECT(at_site(3, 3, 4, 2), 1);
        EXPECT(at_goal(3, 3, 4, 2), 1);
        add_site(3, 3, 4, 4, 2);
        EXPECT(F->sites[0].priority, 4);
        EXPECT(F->goals[0].priority, 2);  // only a new site adds a goal
        // wipe_goals: negates this turn's goals, frees last turn's, adds the sites back.
        F->goals[1] = (struct goal){6, -2, 1, 1, 0};
        F->goals[2] = (struct goal){6, 3, 3, 1, 0};
        wipe_goals(3);
        EXPECT(F->goals[1].type, -1);
        EXPECT(F->goals[2].priority, -3);
        EXPECT(F->goals[0].priority, 4);  // the site's goal again, at the site's priority
        // del_site: within the distance, and the goals at those sites.
        add_site(3, 8, 1, 2, 4);
        del_site(3, 8, 6, 4, 2);
        EXPECT(at_site(3, 8, 2, 4), 1);
        del_site(3, 8, 4, 4, 2);
        EXPECT(at_site(3, 8, 2, 4), 0);
        EXPECT(at_goal(3, 8, 2, 4), 0);
        EXPECT(at_site(3, 3, 4, 2), 1);
    }

    if (failures) return 1;
    printf("engine_strategy: all tests passed\n");
    return 0;
}
