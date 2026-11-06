// Unit test for the reimplemented choice of a base's build, on a small
// made-up map of land: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/build.h"
#include "../src/engine/game.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area_sqrt = 5, base_count = 1, veh_count = 1;
static int32_t turn = 30, difficulty = 3, current = 0, net_mp = 0, cloud = 1, ocean_cov = 0, climate = 0, sea = 0;
static int32_t depth_a = 0, depth_b = 0, tech_unk = -2, forces = 0, governor_faction = -1, ranked[8], rankings[8];
static int32_t cost_base[6] = {10, 10, 10, 10, 10, 10}, map_settings[7] = {2}, ascent[2] = {10, 10};
static int32_t claims[4] = {-999, -999, -999, -999}, claim_scores[4], projects[SP_COUNT];
static uint32_t flat, state, rules;
static uint8_t humans, achieved[TECH_COUNT];
static struct base bases[2], *current_base = &bases[0];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct veh vehs[1];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static struct rules_armor armor[ARMOR_COUNT];
static struct rules_reactor reactors[REACTOR_COUNT];
static struct rules_facility facilities[FACILITY_COUNT];
static struct rules_basic rules_basic;
static struct res_info res;
static struct continent continents[REGION_COUNT];
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static char str_buffer[256];
static int failures, defenders, allowed_fac[FACILITY_COUNT], computed, parsed[2];

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static int zero1(int a) { return 0; }
static int zero2(int a, int b) { return 0; }
static int zero3(int a, int b, int c) { return 0; }
static int one1(int a) { return 1; }
static int one2(int a, int b) { return 1; }
static int identity(int v) { return v; }
static int zero_void(void) { return 0; }
static int has_abil_stub(int proto, uint32_t ability) { return 0; }
static int veh_at_stub(int x, int y) { return 0; }
static int stack_stub(int veh, int type, int c1, int c2, int c3) { return type == 2 && c1 == 2 ? defenders : 0; }
static int veh_avail_stub(int proto, int faction, int base) { return proto == 0 || proto == 2; }
static int fac_avail_stub(int fac, int faction, int base, int queue) { return allowed_fac[fac]; }
static int veh_cost_stub(int proto, int base, int *unk) { return protos[proto].cost; }
static int at_goal_stub(int f, int t, int x, int y) { return 0; }
static int morale_stub(int base, int faction, int triad) { return 0; }
static int generators_stub(int faction, int *count) { return 0; }
static int parse_stub(int slot, int value) { return parsed[slot & 1] = value; }
static uint32_t __attribute__((stdcall)) upper_stub(char *s) { return 0; }
static void *upper_ptr = (void *)upper_stub;
static void compute_stub(int update) { computed++; }
static int popps, helps, sounds, answers[4], answer_at;
static int32_t current_faction = 0, skip_news = 0, sunspots = 0, gender, plural, interlude_state;
static char *script_file = "script";
static void radius_stub(int x, int y, int r, int u) {}
static int says_stub(int slot, char *text, int g, int p) { return 0; }
static int say_stub(int slot, int text, int g, int p) { return 0; }
static int popp_stub(char *file, char *label, int flags, char *pic, int unk) { popps++; return answers[answer_at++ & 3]; }
static void wave_stub(int sound) { sounds = sound; }
static void interlude_stub(int id, char *text, int a, int b) {}
static void help_stub(int kind, int id) { helps++; }

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_state = &state, game_rules = &rules, game_rules_basic = &rules_basic;
    game_bases = bases, game_base_count = &base_count, game_vehs = vehs, game_veh_count = &veh_count;
    game_current_base = &current_base, game_current_base_id = &current, game_factions = factions;
    game_faction_defs = defs, game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_armor = armor;
    game_reactors = reactors, game_facilities = facilities, game_res_info = &res, game_continents = continents;
    game_radius_x = radius_x, game_radius_y = radius_y, game_human_factions = &humans, game_tech_achieved = achieved;
    game_turn = &turn, game_difficulty = &difficulty, game_map_area_sqrt = &area_sqrt, game_net_multiplayer = &net_mp;
    game_cloud_cover = &cloud, game_ocean_coverage = &ocean_cov, game_climate_change = &climate, game_sea_level = &sea;
    game_dome_depth_a = &depth_a, game_dome_depth_b = &depth_b, game_tech_unk_9a5e9c = &tech_unk;
    game_base_forces_supported = &forces, game_governor_faction = &governor_faction, game_ranked_faction = ranked;
    game_faction_rankings = rankings, game_cost_base = cost_base, game_map_settings = map_settings;
    game_ascent_numbers = ascent, game_gov_claims = claims, game_gov_claim_scores = claim_scores;
    game_secret_projects = projects, game_str_buffer = str_buffer, game_char_upper = (void *)&upper_ptr;
    game_name_believers = "BELIEVE", game_name_peacekeepers = "PEACE", game_name_caretakers = "CARETAKE";
    game_best_reactor = one1, game_has_abil = has_abil_stub, game_veh_cargo = zero1, game_stack_fix = identity;
    game_veh_at = veh_at_stub, game_stack_check = stack_stub, game_garrison_check = one1, game_veh_avail = veh_avail_stub;
    game_facility_avail = fac_avail_stub, game_veh_cost = veh_cost_stub, game_at_goal = at_goal_stub;
    game_weap_val = one2, game_arm_val = one2, game_speed_proto = one1, game_worm_mod = zero2, game_morale_mod = morale_stub;
    game_arm_strat = one2, game_weap_strat = one2, game_planet_buster2 = zero1, game_base_lose_minerals = zero2;
    game_aah_ooga = zero2, game_generators = generators_stub, game_climactic_battle = zero_void, game_at_climax = zero1;
    game_parse_num = parse_stub, game_great_satan = zero2;
    build_compute = compute_stub;
    game_draw_radius = radius_stub, game_parse_says = says_stub, game_parse_say = say_stub, game_popp = popp_stub;
    game_wave_it = wave_stub, game_interlude = interlude_stub, game_help_topic = help_stub;
    game_current_faction = &current_faction, game_skip_project_news = &skip_news, game_sunspot_duration = &sunspots;
    game_parse_gender = &gender, game_parse_plural = &plural, game_interlude_state = &interlude_state;
    game_script_file = &script_file;
    (void)zero3;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 1; i < 121; i++) radius_x[i] = (i % 3) - 1, radius_y[i] = (i / 3 % 3) - 1;
    rules_basic.move_rate_roads = 3, rules_basic.nutrient_intake_citizen = 2, rules_basic.mineral_cost_multi = 10;
    rules_basic.nutrient_cost_multi = 10, rules_basic.pop_limit_hab_complex = 7, rules_basic.pop_limit_hab_dome = 14;
    for (int i = 0; i < REACTOR_COUNT; i++) reactors[i].preq_tech = TECH_DISABLED;
    for (int i = 0; i < CHASSIS_COUNT; i++) chassis[i].speed = 1, chassis[i].preq_tech = TECH_DISABLED;
    for (int i = 0; i < WEAPON_COUNT; i++) weapons[i].preq_tech = TECH_DISABLED;
    for (int i = 0; i < FACILITY_COUNT; i++) facilities[i].cost = 4, facilities[i].preq_tech = TECH_NONE;
    weapons[0].offense = 1, weapons[0].mode = 0;  // a gun
    weapons[1].mode = 3;                          // a colony module
    armor[1].defense = 2;
    // A colony pod and a defender, both already built.
    protos[0] = (struct unit_proto){.chassis = 0, .weapon = 1, .cost = 3, .plan = PLAN_COLONY, .flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT};
    protos[2] = (struct unit_proto){.chassis = 0, .weapon = 0, .armor = 1, .cost = 2, .plan = PLAN_DEFENSIVE,
                                    .flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT};

    // Land everywhere in region 1; faction 1's base at (2, 2), with its
    // defender on it.
    for (int i = 0; i < W / 2 * H; i++) tiles[i] = (struct map_tile){.climate = 4 << 5, .owner_site = 0xf, .region = 1, .territory = -1};
    tiles[1 + 2 * (W / 2)].bits = MAP_BASE | MAP_UNIT, tiles[1 + 2 * (W / 2)].owner_site = 1;
    continents[1].tile_count = W / 2 * H;
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .faction_former = 1, .pop_size = 3};
    bases[0].intake_2[1] = 4, bases[0].surplus[0] = 1, bases[0].surplus[1] = 2, bases[0].queue[0] = -FAC_STOCKPILE_ENERGY;
    vehs[0] = (struct veh){.x = 2, .y = 2, .faction = 1, .proto = 2, .home_base = 0};
    struct faction_data *F = &factions[1];
    F->target_x = F->target_y = -1, F->attack_target_base = -1;
    F->region_base_plan[1] = PLAN_RECON, F->region_total_bases[1] = 5, F->base_count = 1, F->best_mineral_output = 8;
    allowed_fac[FAC_RECYCLING_TANKS] = 1;
    strcpy(defs[1].filename, "TEST");

    // Undefended (its unit doesn't count as a defender here): it builds a
    // defender at no cost, the best combat choice too.
    int second = 0, third = 0, score = -1;
    defenders = 0;
    EXPECT(base_build(0, &second, &third, &score), 2);
    EXPECT(computed, 1);
    EXPECT(score, 0);
    EXPECT(third, 2);
    EXPECT(second == 0 || second == -FAC_RECYCLING_TANKS, 1);
    EXPECT(parsed[0], 0);  // the combat choice's score

    // A human's base with no governor: nothing passes its settings, and
    // the pick stays the default, stockpiling energy.
    humans = 1 << 1, defenders = 1;
    EXPECT(base_build(0, NULL, NULL, &score), -FAC_STOCKPILE_ENERGY);
    EXPECT(score, 999999);
    // With a governor allowed facilities but no units: the recycling
    // tanks; with secret projects too (and Recreation Commons already
    // built, which a project otherwise gives way to), the project, except
    // in the quick pick.
    bases[0].governor = 0x80000000u | 0x10000;
    EXPECT(base_build(0, NULL, NULL, NULL), -FAC_RECYCLING_TANKS);
    allowed_fac[FAC_FIRST_PROJECT] = 1;
    bases[0].governor |= 0x40000;
    bases[0].facilities_built[0] = 1 << FAC_RECREATION_COMMONS;
    EXPECT(base_build(0, NULL, NULL, NULL), -FAC_FIRST_PROJECT);
    EXPECT(base_build(-1, NULL, NULL, NULL), -FAC_RECYCLING_TANKS);
    EXPECT(strcmp(str_buffer, "TEST"), 0);  // the faction's name, put together for a check


    // The class of item for retooling, by strictness; -1 while the base
    // is set on a facility it has.
    rules_basic.retool_strictness = 0;
    EXPECT(base_making(-10, 0), 0);
    rules_basic.retool_strictness = 2;
    EXPECT(base_making(-75, 0), -FAC_FIRST_PROJECT);
    EXPECT(base_making(5, 0), 5);
    rules_basic.retool_strictness = 1;
    EXPECT(base_making(3, 0), 0);
    EXPECT(base_making(-75, 0), 1);
    EXPECT(base_making(-10, 0), 3);
    EXPECT(base_making(-66, 0), 2);
    bases[0].queue[0] = -FAC_RECREATION_COMMONS;
    EXPECT(base_making(-10, 0), -1);
    EXPECT(base_making(3, 0), 0);

    // Changing: the faction's count of units being built follows, and a
    // human's base loses half its minerals over the exemption of 10
    // between a unit and a facility.
    humans = 0, bases[0].queue[0] = -FAC_RECYCLING_TANKS, bases[0].minerals_2 = 30;
    base_change(0, 2);
    EXPECT(factions[1].units_queue[2], 1);
    EXPECT(bases[0].minerals, 30);
    humans = 1 << 1, rules_basic.retool_penalty = 50, rules_basic.retool_exemption = 10, bases[0].production_last = 2;
    base_change(0, -FAC_RECYCLING_TANKS);
    EXPECT(factions[1].units_queue[2], 0);
    EXPECT(bases[0].minerals, 20);
    base_change(0, 99999);
    EXPECT(bases[0].queue[0], -FAC_RECYCLING_TANKS);

    // The player hears once of another faction's unbuilt project; a
    // faction leaving one unbuilt project for another moves the news.
    humans = 0;
    base_change(0, -75);
    EXPECT(popps, 1);
    EXPECT(sounds, 0x26);
    EXPECT(factions[1].project_intel[0], 1 << 5);
    base_change(0, -FAC_RECYCLING_TANKS);
    base_change(0, -75);
    EXPECT(popps, 1);
    base_change(0, -76);
    EXPECT(popps, 2);
    EXPECT(factions[1].project_intel[0], 1 << 6);
    current_faction = 1;  // the player's own: no news
    base_change(0, -77);
    EXPECT(popps, 2);
    current_faction = 0;

    // Resetting an AI base: its undefended pick.
    defenders = 0;
    base_reset(0, 0);
    EXPECT(bases[0].queue[0], 2);
    // A human's governor turning to a project asks the player; help asks
    // again, and the third answer takes the quick pick and turns off the
    // governor's projects.
    humans = 1 << 1, defenders = 1, skip_news = 1, popps = 0;
    factions[1].base_governor_adv = 0x40000;
    answers[0] = 3, answers[1] = 2, answer_at = 0;
    base_reset(0, 0);
    EXPECT(popps, 2);
    EXPECT(helps, 1);
    EXPECT(bases[0].queue[0], -FAC_RECYCLING_TANKS);
    EXPECT(bases[0].governor & 0x40000, 0);
    EXPECT(factions[1].base_governor_adv, 0);
    // The governor's four kinds of work each claim an item first, and its
    // settings are put back.
    bases[0].governor |= 0x0f000000;
    base_reset(0, 1);
    EXPECT(claims[0] != 0 && claim_scores[0] != 999999, 1);
    EXPECT(bases[0].governor >> 24, 0x8f);

    // All an AI faction's bases, or those in a region, or undefended.
    humans = 0, defenders = 0, bases[0].queue[0] = -FAC_STOCKPILE_ENERGY;
    bases_reset(2, 1, 0);
    EXPECT(bases[0].queue[0], -FAC_STOCKPILE_ENERGY);
    bases_reset(-1, 1, 1);  // its unit stands on it
    EXPECT(bases[0].queue[0], -FAC_STOCKPILE_ENERGY);
    bases_reset(1, 1, 0);
    EXPECT(bases[0].queue[0], 2);
    humans = 1 << 1, bases[0].queue[0] = -FAC_STOCKPILE_ENERGY;
    bases_reset(-1, 1, 0);
    EXPECT(bases[0].queue[0], -FAC_STOCKPILE_ENERGY);

    if (failures) return 1;
    printf("engine_build: all tests passed\n");
    return 0;
}
