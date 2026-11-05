// Unit test for the reimplemented base functions, on a small made-up map:
// no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/base.h"
#include "../src/engine/game.h"
#include "../src/engine/hooks.h"

enum { W = 8, H = 6 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, seed;
static uint32_t flat, rules;
// The originals look up base -1's facilities, so the table has an entry
// before it.
static struct base base_table[5], *bases = base_table + 1;
static int32_t base_count = 1;
static struct faction_data factions[FACTION_COUNT];
static struct faction_def faction_defs[FACTION_COUNT];
static struct rules_tech techs[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static int32_t expansion;
static struct res_info res = {
    .ocean = {1, 0, 2}, .base = {2, 1, 1}, .bonus = {2, 1, 2}, .forest = {1, 2, 0}, .recycling = {1, 1, 1},
    .improved_land = {1, 0, 0}, .improved_sea = {1, 1, 2}, .monolith = {2, 2, 2}, .borehole = {0, 6, 6},
};
static struct rules_basic rules_basic = {
    .mine_road_limit = 1, .nutrient_effect_mine = -1,
    .tech_three_nutrients = 10, .tech_three_minerals = 11, .tech_three_energy = 12, .tech_mining_platform_bonus = 13,
};
static int32_t projects[SP_COUNT], governor, dust, flares, upkeep_a, reduce, terraform_energy;
static struct base *current;
static int32_t near_x[9] = {1, 2, 1, 0, -1, -2, -1, 0, 0}, near_y[9] = {-1, 0, 1, 2, 1, 0, -1, -2, 0};
static int failures;

#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        int got_ = (expr);                                                    \
        if (got_ != (want)) {                                                 \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }

// A square: altitude level, rainfall, rockiness and bits.
static void put(int x, int y, int alt, int rain, int rocky, uint32_t bits) {
    struct map_tile *t = at(x, y);
    memset(t, 0, sizeof(*t));
    t->climate = (uint8_t)(alt << 5 | rain << 3);
    t->rocky_lock = (uint8_t)(rocky << 6);
    t->owner_site = 0xf;
    t->territory = -1;
    t->bits = bits;
}

static int best_specialist_stub(void) { return 2; }
static int satan;
static int great_satan_stub(int faction, int aggressive) {
    (void)faction, (void)aggressive;
    return satan;
}
static int32_t reserved, square_flags[25];
static int goals, hostile;
static void add_goal_stub(int faction, int type, int priority, int x, int y, int base) {
    (void)faction, (void)type, (void)priority, (void)x, (void)y, (void)base;
    goals++;
}
static int stack_check_stub(int veh, int type, int c1, int c2, int c3) {
    (void)veh, (void)type, (void)c1, (void)c2, (void)c3;
    return hostile;
}
// A made-up radius table: the base square, the 8 around it, 12 more.
static int32_t radius_x[21] = {0, 1, 2, 1, 0, -1, -2, -1, 0, 2, 3, 2, -2, -3, -2, 0, 4, 0, -4, 1, -1};
static int32_t radius_y[21] = {0, -1, 0, 1, 2, 1, 0, -1, -2, -2, -1, 2, 2, 1, -2, 4, 0, -4, 0, 3, -3};

// What the worked squares of base `id` yield, as base_yield() adds it up.
static int worked_yield(int id, int type) {
    const struct base *base = &bases[id];
    int sum = 0;
    for (int i = 0; i < 21; i++) {
        if (!(base->worked_tiles & (1u << i))) continue;
        int x = (base->x + radius_x[i] + W) % W, y = base->y + radius_y[i];
        if (y >= 0 && y < H) sum += resource_yield(type, base->faction, id, x, y);
    }
    return sum;
}
// One unit can stand on base 1's square, as veh 0.
static struct veh vehs[6];
static struct unit_proto protos[3];
static struct rules_weapon weapons[2];
static struct rules_chassis chassis[2];
static int unit_here;
static int veh_at_stub(int x, int y) { return unit_here && x == bases[1].x && y == bases[1].y ? 0 : -1; }
static int stack_fix_stub(int veh) { return veh; }
static int has_abil_stub(int proto, uint32_t ability) { return (protos[proto].abilities & ability) != 0; }

static void give(int base, int facility) { bases[base].facilities_built[facility / 8] |= (uint8_t)(1 << (facility % 8)); }

// Growth's messages and what it calls when a base is lost.
static char growth_texts[0x200];
static char *popb_label, *say_text;
static int popb_flags, popb_count, objective, killed, eliminated;
static int popb_stub(char *label, int flags, int sound, char *picture, void *sprite) {
    popb_label = label, popb_flags = flags, popb_count++;
    return 0;
}
static int parse_say_stub(int slot, int text, int gender, int plural) {
    say_text = (char *)(intptr_t)text;
    return 0;
}
static int is_objective_stub(int base) { return objective; }
static void base_kill_stub(int base) { killed = base + 1; }
static int eliminate_stub(int faction, int by) { return eliminated = faction; }
static void draw_tile_stub(int x, int y, int unk) {}
static void draw_map_stub(int unk) {}
// The queue: units 5 and up and facility 9 can't be built.
static int sound;
static int veh_avail_stub(int proto, int faction, int base) { return proto < 5; }
static int facility_avail_stub(int facility, int faction, int base, int queue) { return facility != 9; }
static void wave_stub(int id) { sound = id; }
static void draw_radius_stub(int x, int y, int radius, int unk) {}
// Production: what it calls in the game.
static int resets, vehs_made, monuments;
static void __attribute__((thiscall)) pop_stub(void *pop) {}
static void reset_stub(int base, int unk) { resets++; }
static int veh_cost_stub(int proto, int base, int *unk) { return 3; }
static int veh_init_stub(int proto, int faction, int x, int y) {
    memset(&vehs[5], 0, sizeof(vehs[5]));
    vehs[5].proto = (int16_t)proto, vehs[5].faction = (uint8_t)faction, vehs[5].x = (int16_t)x, vehs[5].y = (int16_t)y;
    vehs_made++;
    return 5;
}
static int morale_stub(int base, int faction, int triad) { return 1; }
static void mon_stub(int faction) { monuments++; }
static void mon_facility_stub(int faction, char *name) { monuments++; }
static int parse_num_stub(int slot, int value) { return 0; }
static int parse_says_stub(int slot, char *text, int gender, int plural) { return 0; }
static void set_dirty_stub(void) {}
static int news;
static void __attribute__((thiscall)) pop_init_stub(void *pop, int a, int b) {}
static void __attribute__((thiscall)) pop_width_stub(void *pop, int w) {}
static void __attribute__((thiscall)) pop_string_stub(void *pop, char *s) {}
static int __attribute__((thiscall)) pop_start_stub(void *pop, char *f, char *l, int a, int b, int c, int d) { return 0; }
static int __attribute__((thiscall)) pop_exec_stub(void *pop, int a, int b) { return news++, 0; }
static int pcx_stub(char *file, int *w, int *h) { return 1; }
static void interlude_stub(int id, char *text, int a, int b) {}
static void help_stub(int project) {}
static int reactor = 2, researched = -1;
static int best_reactor_stub(int faction) { return reactor; }
static void tech_research_stub(int faction, int labs) { researched = labs; }
// A faction file: land names, then sea names; the shared list is empty.
static const char *file_lines[2][4] = {{"Alpha", "Beta", "Gamma", ""}, {"Reef", "#END", "", ""}};
static int file_section = -1, file_line;
static char file_buffer[32], *file_ptr = file_buffer;
static int text_open_stub(char *file, char *label) {
    file_section = strcmp(file, "x") ? -1 : label[0] == 's' ? 1 : 0;
    file_line = 0;
    return file_section < 0;
}
static void text_get_stub(void) { strcpy(file_buffer, file_line < 4 ? file_lines[file_section][file_line++] : ""); }
static char *text_item_stub(void) { return file_buffer; }
static void text_close_stub(void) { file_section = -1; }
static int can_terraform_value = 1, connects;
static int can_terraform_stub(int faction, int x, int y, int unk, int base) { return can_terraform_value; }
static void base_connect_stub(int level) { connects++; }
static int terrain_avail_stub(int terraform, int sea, int faction) { return 0; }
static int abil_index_stub(uint32_t flag) { return 0; }
static int base_find_stub(int x, int y) { return 1; }
static int rand_value, rand_calls, replays, spotted, proposed, centred, territory;
static int rand_stub(void) { return rand_calls++, rand_value; }
static uint32_t __attribute__((stdcall)) time_stub(void) { return 1234; }
static void replay_stub(int event, int x, int y, int faction) { replays++; }
static void spot_stub(int base, int faction) { spotted++; }
static int propose_stub(int faction, int chassis, int weapon, int armor, uint32_t abilities, int reactor, int plan,
                        char *name) {
    return proposed++, -1;
}
static void __attribute__((thiscall)) centre_stub(void *win, int x, int y, int unk) { centred++; }
static void territory_stub(void) { territory++; }
static int killed_veh = -1;
static int kill_stub(int veh) {
    killed_veh = veh;
    vehs[veh].state &= ~(uint32_t)VEH_STATE_SUPPORTED;
    return 0;
}
static int no_goal(int faction, int type, int x, int y) { return 0; }
static int no_buster(int proto) { return 0; }
static int no_climax(int faction) { return 0; }
static int popps;
static int popp_stub(char *file, char *label, int flags, char *picture, int unk) { return popps++, 0; }
static int focused, redrawn, zoomed;
static int __attribute__((thiscall)) focus_stub(void *console, int x, int y, int faction) { return focused++, 0; }
static void __attribute__((thiscall)) redraw_stub(void *win) { redrawn++; }
static void __attribute__((thiscall)) zoom_stub(void *win, int base, int unk) { zoomed = base + 1; }
static void log_say_stub(char *text, int a, int b, int c) {}
static void log_say5_stub(char *a, char *b, int n1, int n2, int n3) {}
static int pumps, advances;
static void pump_stub(void) { pumps++; }
static void advance_stub(int faction) { advances++; }

int main(void) {
    game_map = &tiles_ptr;
    game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_map_seed = &seed, game_rules = &rules;
    game_bases = bases, game_base_count = &base_count;
    game_factions = factions, game_faction_defs = faction_defs;
    game_tech = techs, game_tech_achieved = achieved, game_expansion = &expansion;
    game_res_info = &res, game_rules_basic = &rules_basic;
    game_secret_projects = projects, game_governor_faction = &governor;
    game_dust_cloud = &dust, game_solar_flares = &flares, game_control_upkeep_a = &upkeep_a;
    game_current_base = &current, game_terraform_reduce = &reduce, game_terraform_energy = &terraform_energy;
    game_radius_near_x = near_x, game_radius_near_y = near_y;
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_NONE;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    bases[0].x = 2, bases[0].y = 2, bases[0].faction = 1;

    // Rainy flat land: 2 nutrients, nothing else; a farm makes 3, cut to 2
    // until the faction has the tech.
    put(4, 2, 3, 2, 0, 0);
    EXPECT(crop_yield(1, -1, 4, 2, 0), 2);
    EXPECT(mine_yield(1, -1, 4, 2, 0), 0);
    EXPECT(energy_yield(1, -1, 4, 2, 0), 0);
    at(4, 2)->bits = MAP_FARM;
    EXPECT(crop_yield(1, -1, 4, 2, 0), 2);
    EXPECT(reduce, 1);
    achieved[10] = 1 << 1;
    EXPECT(crop_yield(1, -1, 4, 2, 0), 3);
    EXPECT(crop_yield(2, -1, 4, 2, 0), 2);
    // A soil enricher adds half; a mine costs a nutrient above 1.
    at(4, 2)->bits = MAP_FARM | MAP_SOIL_ENRICHER;
    EXPECT(crop_yield(1, -1, 4, 2, 0), 4);
    at(4, 2)->bits = MAP_MINE;
    EXPECT(crop_yield(1, -1, 4, 2, 0), 1);
    // A river adds an energy; so does ECONOMY 2.
    at(4, 2)->bits = MAP_RIVER;
    EXPECT(energy_yield(1, -1, 4, 2, 0), 1);
    factions[1].se_pending[SE_ECONOMY] = 2;
    EXPECT(energy_yield(1, -1, 4, 2, 0), 2);
    factions[1].se_pending[SE_ECONOMY] = 0;

    // Rocky land with a mine: rockiness, then 2 more on a road (capped at
    // 2 in all), or the road limit of 1 without.
    put(6, 2, 3, 0, 2, MAP_MINE | MAP_ROAD);
    reduce = 0;
    EXPECT(mine_yield(1, -1, 6, 2, 0), 2);
    EXPECT(reduce, 2);
    achieved[11] = 1 << 1;
    EXPECT(mine_yield(1, -1, 6, 2, 0), 4);
    at(6, 2)->bits = MAP_MINE;
    reduce = 0;
    EXPECT(mine_yield(1, -1, 6, 2, 0), 3);
    EXPECT(reduce, 1);
    at(6, 2)->bits = 0;
    EXPECT(mine_yield(1, -1, 6, 2, 0), 1);
    EXPECT(mine_yield(1, -1, 6, 2, 1), 4);  // as if mined, and the road limit waived
    EXPECT(crop_yield(1, -1, 6, 2, 0), 0);

    // A solar collector at altitude 4 gives 2, and 1 more for a mirror
    // next to it in the territory of the base being worked on.
    put(1, 1, 4, 0, 0, MAP_SOLAR);
    put(2, 0, 4, 0, 0, MAP_ECHELON_MIRROR);
    at(2, 0)->territory = 1;
    current = &bases[0];
    reduce = 0;
    EXPECT(energy_yield(1, -1, 1, 1, 0), 2);
    EXPECT(reduce, 1);
    achieved[12] = 1 << 1;
    EXPECT(energy_yield(1, -1, 1, 1, 0), 3);
    at(2, 0)->territory = 2;
    EXPECT(energy_yield(1, -1, 1, 1, 0), 2);

    // The base square.
    put(2, 2, 3, 1, 0, MAP_BASE);
    at(2, 2)->owner_site = 1;
    give(0, FAC_HEADQUARTERS);
    factions[1].se_pending[SE_ECONOMY] = 3;
    EXPECT(energy_yield(1, 0, 2, 2, 0), 5);  // 1, headquarters 1, ECONOMY 3 gives 2, and 1 for ECONOMY 2
    EXPECT(terraform_energy, 5);
    factions[1].se_pending[SE_ECONOMY] = 5;
    EXPECT(energy_yield(1, 0, 2, 2, 0), 7);
    factions[1].se_pending[SE_ECONOMY] = -1;
    factions[1].base_count = 3;
    EXPECT(energy_yield(1, 0, 2, 2, 0), 1);  // headquarters takes the -1
    factions[1].base_count = 1;
    EXPECT(energy_yield(1, 0, 2, 2, 0), 2);
    factions[1].se_pending[SE_ECONOMY] = 0;
    governor = 1;
    EXPECT(energy_yield(1, 0, 2, 2, 0), 3);
    governor = 0;
    EXPECT(crop_yield(1, 0, 2, 2, 0), 2);
    give(0, FAC_RECYCLING_TANKS);
    EXPECT(crop_yield(1, 0, 2, 2, 0), 3);
    EXPECT(crop_yield(1, -1, 2, 2, 0), 2);  // no base: no facilities
    EXPECT(mine_yield(1, 0, 2, 2, 0), 2);
    bases[0].event = BASE_EVENT_BUMPER | BASE_EVENT_BUST;
    EXPECT(crop_yield(1, 0, 2, 2, 0), 4);
    EXPECT(mine_yield(1, 0, 2, 2, 0), 1);
    bases[0].event = 0;

    // Fungus: the faction's fungus techs less a negative PLANET; Manifold
    // Harmonics adds 1 at PLANET 1. With the flag, crop_yield ignores it.
    put(0, 4, 3, 2, 0, MAP_FUNGUS);
    factions[1].tech_fungus[FUNGUS_NUTRIENT] = 1;
    factions[1].se_pending[SE_PLANET] = -1;
    EXPECT(crop_yield(1, -1, 0, 4, 0), 0);
    factions[1].se_pending[SE_PLANET] = 1;
    EXPECT(crop_yield(1, -1, 0, 4, 0), 1);
    projects[SP_MANIFOLD_HARMONICS] = 0;
    EXPECT(crop_yield(1, -1, 0, 4, 0), 2);
    EXPECT(mine_yield(1, -1, 0, 4, 0), 0);    // needs PLANET 2
    EXPECT(energy_yield(1, -1, 0, 4, 0), 1);  // from PLANET 0
    EXPECT(crop_yield(1, -1, 0, 4, 1), 2);
    projects[SP_MANIFOLD_HARMONICS] = -1;
    factions[1].se_pending[SE_PLANET] = 0;

    // The shelf: kelp, an aquafarm, a mining platform, tidal harnesses.
    put(2, 4, 2, 0, 0, MAP_FARM);
    achieved[10] = 0;
    EXPECT(crop_yield(1, -1, 2, 4, 0), 2);
    give(0, FAC_AQUAFARM);
    reduce = 0;
    EXPECT(crop_yield(1, 0, 2, 4, 0), 2);
    EXPECT(reduce, 1);
    EXPECT(energy_yield(1, -1, 2, 4, 0), 2);
    achieved[12] = 0;
    EXPECT(energy_yield(1, -1, 2, 4, 1), 2);
    EXPECT(mine_yield(1, 0, 2, 4, 0), 0);
    faction_defs[1].rule_flags = RULE_FLAG_AQUATIC;
    EXPECT(mine_yield(1, 0, 2, 4, 0), 1);
    EXPECT(mine_yield(1, -1, 2, 4, 0), 0);  // base -1's faction is 0
    at(2, 4)->bits = MAP_MINE;
    achieved[13] = 1 << 1;
    EXPECT(mine_yield(1, 0, 2, 4, 0), 3);  // aquatic, platform, and the tech for it
    faction_defs[1].rule_flags = 0;
    // Deep water yields energy only in Alien Crossfire.
    put(4, 4, 1, 0, 0, 0);
    EXPECT(energy_yield(1, -1, 4, 4, 0), 0);
    expansion = 1;
    EXPECT(energy_yield(1, -1, 4, 4, 0), 2);
    expansion = 0;

    // Forest, a monolith and a borehole.
    put(6, 4, 3, 1, 0, MAP_FOREST);
    EXPECT(crop_yield(1, 0, 6, 4, 0), 1);
    give(0, FAC_TREE_FARM);
    give(0, FAC_HYBRID_FOREST);
    EXPECT(crop_yield(1, 0, 6, 4, 0), 2);  // 3, cut to 2
    EXPECT(crop_yield(1, -1, 6, 4, 0), 1);
    EXPECT(mine_yield(1, -1, 6, 4, 0), 2);
    EXPECT(energy_yield(1, 0, 6, 4, 0), 1);
    at(6, 4)->bits = MAP_MONOLITH;
    EXPECT(crop_yield(1, -1, 6, 4, 0), 2);
    EXPECT(mine_yield(1, -1, 6, 4, 0), 2);
    EXPECT(energy_yield(1, -1, 6, 4, 0), 2);
    at(6, 4)->bits = MAP_THERMAL_BORE;
    achieved[11] = achieved[12] = 0;
    EXPECT(crop_yield(1, -1, 6, 4, 0), 0);
    EXPECT(mine_yield(1, -1, 6, 4, 0), 2);
    EXPECT(energy_yield(1, -1, 6, 4, 0), 2);

    // Landmarks: the jungle adds a nutrient unless switched off.
    put(0, 0, 3, 1, 0, 0);
    at(0, 0)->bits2 = MAP2_LANDMARK(LM_JUNGLE);
    EXPECT(crop_yield(1, -1, 0, 0, 0), 2);
    at(0, 0)->bits2 |= MAP2_UNK_80000000;
    EXPECT(crop_yield(1, -1, 0, 0, 0), 1);
    at(0, 0)->bits2 = MAP2_LANDMARK(LM_CANYON);
    EXPECT(mine_yield(1, -1, 0, 0, 0), 1);

    // Resources: a nutrient doubles the base square's.
    at(2, 2)->bits |= MAP_RESOURCE | MAP_NUTRIENT_RESOURCE;
    EXPECT(crop_yield(1, -1, 2, 2, 0), 4);

    // Dust clouds and solar flares.
    dust = 1;
    EXPECT(energy_yield(1, -1, 4, 2, 0), 0);
    dust = 0, flares = 1;
    EXPECT(energy_yield(1, -1, 4, 2, 0), 3);
    upkeep_a = 1;
    EXPECT(energy_yield(1, -1, 4, 2, 0), 1);
    flares = upkeep_a = 0;

    // resource_yield picks one, or clears the reduction.
    EXPECT(resource_yield(RESOURCE_NUTRIENT, 1, -1, 4, 2), crop_yield(1, -1, 4, 2, 0));
    EXPECT(resource_yield(RESOURCE_ENERGY, 1, -1, 4, 2), 1);
    reduce = 7;
    EXPECT(resource_yield(RESOURCE_UNUSED, 1, -1, 4, 2), 0);
    EXPECT(reduce, 0);
    EXPECT(resource_yield(9, 1, -1, 4, 2), 0);

    // has_fac: built, or in the queue when asked; projects never.
    EXPECT(has_fac(FAC_HEADQUARTERS, 0, 0), 1);
    EXPECT(has_fac(FAC_NANOREPLICATOR, 0, 3), 0);
    bases[0].queue[2] = -FAC_NANOREPLICATOR;
    EXPECT(has_fac(FAC_NANOREPLICATOR, 0, 2), 0);
    EXPECT(has_fac(FAC_NANOREPLICATOR, 0, 3), 1);
    give(0, FAC_FIRST_PROJECT);
    EXPECT(has_fac(FAC_FIRST_PROJECT, 0, 0), 0);
    bases[0].queue[2] = 0;

    // The current base's nutrients.
    int32_t growth, from[4] = {1, 2}, to[4] = {3, 4}, upkeep, upkeep_state = 1, current_id = 0, detonations[8];
    game_base_growth = &growth, game_base_convoy_from = from, game_base_convoy_to = to;
    game_base_forces_upkeep = &upkeep, game_base_upkeep_state = &upkeep_state;
    game_current_base_id = &current_id, game_tectonic_detonations = detonations;
    struct base *b = &bases[0];
    current = b;
    rules_basic.nutrient_intake_citizen = 2;
    b->pop_size = 3;
    b->intake_2[RESOURCE_NUTRIENT] = 4;
    factions[1].se_pending[SE_GROWTH] = 1;
    give(0, FAC_CHILDREN_CRECHE);
    base_nutrient();
    EXPECT(growth, 3);
    EXPECT(b->intake_2[RESOURCE_NUTRIENT], 7);
    EXPECT(b->consumption[RESOURCE_NUTRIENT], 7);
    EXPECT(b->surplus[RESOURCE_NUTRIENT], 0);
    EXPECT(factions[1].nutrient_surplus_total, 0);
    b->intake_2[RESOURCE_NUTRIENT] = 3;
    base_nutrient();
    EXPECT(b->surplus[RESOURCE_NUTRIENT], -1);
    EXPECT(b->nutrients, -1);
    b->intake_2[RESOURCE_NUTRIENT] = 200;
    base_nutrient();
    EXPECT(b->nutrients, 0);
    EXPECT(factions[1].nutrient_surplus_total, 99);

    // Minerals: a robotic assembly plant adds half; a base with no
    // damage before and a little over the clean limit.
    static struct rules_worldbuilder wb = {.sea_level_rises = 1};
    int32_t sea = 0, native = 0, difficulty = 3;
    uint32_t state = 0;
    game_worldbuilder = &wb, game_sea_level = &sea, game_native_life = &native, game_difficulty = &difficulty;
    game_state = &state;
    uint8_t humans = 0;
    game_human_factions = &humans;
    upkeep = 1;
    b->intake_2[RESOURCE_MINERAL] = 20;
    give(0, FAC_ROBOTIC_ASSEMBLY_PLANT);
    factions[1].tech_ranking = 20;
    b->eco_damage = 0;
    base_minerals();
    EXPECT(b->intake_2[RESOURCE_MINERAL], 36);  // (20 + 4) * 3 / 2
    EXPECT(b->consumption[RESOURCE_MINERAL], 3);
    EXPECT(b->surplus[RESOURCE_MINERAL], 33);
    EXPECT(b->mineral_surplus_final, 33);
    // (36 - 16) damage, times 20 techs, 3 for PLANET 0, 1, level 3 (6 - 3), / 6, / 100 rounded.
    EXPECT(b->eco_damage, (20 * 20 * 3 * 1 * 3 / 6 + 50) / 100);
    // Damage carried over uses up clean minerals first.
    b->intake_2[RESOURCE_MINERAL] = 20;
    b->eco_damage = 8 * 10;
    base_minerals();
    EXPECT(b->eco_damage, ((36 - 6) * 20 * 3 * 3 / 6 + 50) / 100);
    // The Space Elevator doubles minerals for an orbital facility.
    projects[SP_SPACE_ELEVATOR] = 0;
    b->queue[0] = -FAC_SKY_HYDRO_LAB;
    b->intake_2[RESOURCE_MINERAL] = 20;
    base_minerals();
    EXPECT(b->intake_2[RESOURCE_MINERAL], 72);
    EXPECT(b->surplus[RESOURCE_MINERAL], 69);
    EXPECT(b->mineral_surplus_final, 33);

    // Energy. A second base of faction 1, 3 squares from base 0's
    // headquarters, and a base of faction 2.
    int32_t import[8] = {0}, export[8] = {0}, commerce_techs = 1, passes[11] = {0}, area_sqrt = 10;
    static struct citizen citizens[CITIZEN_KINDS];
    game_base_commerce_import = import, game_base_commerce_export = export;
    game_tech_commerce_count = &commerce_techs, game_proposal_passes = passes, game_map_area_sqrt = &area_sqrt;
    game_citizens = citizens, game_best_specialist = best_specialist_stub;
    int32_t talents[5], drones[5], superdrones[5], pacifism = 0;
    game_psych_talents = talents, game_psych_drones = drones, game_psych_superdrones = superdrones;
    game_base_pacifism = &pacifism;
    game_vehs = vehs, game_protos = protos, game_weapons = weapons, game_chassis = chassis;
    int32_t veh_count = 1, supported = 0, paid = 0;
    game_veh_count = &veh_count, game_base_forces_supported = &supported, game_base_forces_paid = &paid;
    vehs[0].faction = 7;
    game_veh_at = veh_at_stub, game_stack_fix = stack_fix_stub, game_has_abil = has_abil_stub;
    vehs[0].next_in_stack = vehs[0].prev_in_stack = -1;
    for (int i = 0; i < CITIZEN_KINDS; i++) citizens[i].preq_tech = citizens[i].obsol_tech = TECH_DISABLED;
    base_count = 3;
    struct base *b1 = &bases[1];
    b1->x = 6, b1->y = 4, b1->faction = 1, b1->pop_size = 3;
    bases[2].x = 0, bases[2].y = 0, bases[2].faction = 2;

    // Content citizens and the base limit: 5 * 4 * 10 / 56 / 2 for the AI.
    int content, limit;
    psych_check(1, &content, &limit);
    EXPECT(content, 3);
    EXPECT(limit, 1);
    factions[1].se_pending[SE_EFFIC] = 3;
    psych_check(1, &content, &limit);
    EXPECT(limit, 3);  // 5 * 7 * 10 / 56 / 2
    humans = 1 << 1, factions[1].diff_level = 5;
    psych_check(1, &content, &limit);
    EXPECT(content, 1);
    EXPECT(limit, 1);  // 3 * 7 * 10 / 56 / 2
    humans = 0, factions[1].se_pending[SE_EFFIC] = 0;

    // Energy lost: 40 * 3 squares / 32 at EFFICIENCY 0; a creche counts as
    // 2 more; none at headquarters; 16 squares away without one.
    current = b1, current_id = 1, upkeep_state = 0;
    EXPECT(black_market(40), 3);
    EXPECT(black_market(0), 0);
    give(1, FAC_CHILDREN_CRECHE);
    EXPECT(black_market(40), 2);  // 120 / 48
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));
    factions[1].se_pending[SE_EFFIC] = -4;
    EXPECT(black_market(40), 40);
    factions[1].se_pending[SE_EFFIC] = 0;
    bases[0].faction = 3;
    EXPECT(black_market(40), 20);
    bases[0].faction = 1;
    current = b, current_id = 0;
    EXPECT(black_market(40), 0);
    current = b1, current_id = 1, upkeep_state = 1;
    black_market(40);
    EXPECT(factions[1].social_effic[0], 1);   // 120 / 64
    EXPECT(factions[1].social_effic[7], 15);  // 120 / 8
    EXPECT(factions[1].social_effic[8], 40);

    // Ranks by energy intake, ties to the later base.
    b->intake[RESOURCE_ENERGY] = 5, b1->intake[RESOURCE_ENERGY] = 9;
    EXPECT(base_rank(1, 0), 1);
    EXPECT(base_rank(1, 1), 0);
    EXPECT(base_rank(1, 2), -1);
    EXPECT(base_rank(2, 0), 2);
    b1->intake[RESOURCE_ENERGY] = 5;
    EXPECT(base_rank(1, 0), 1);

    // 20 energy, 1 lost: 19 split 2 psych, 5 labs, 3 economy in tenths,
    // then labs (allocated more) lose 8% and economy 16% to the imbalance.
    factions[1].alloc_psych = 2, factions[1].alloc_labs = 5;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    b->intake[RESOURCE_ENERGY] = 0, b1->intake[RESOURCE_ENERGY] = 4;
    base_energy();
    EXPECT(b1->inefficiency[RESOURCE_ENERGY], 1);
    EXPECT(b1->surplus[RESOURCE_ENERGY], 19);
    EXPECT(factions[1].energy_surplus_total, 19);
    EXPECT(b1->psych_total, 4);
    EXPECT(b1->labs_total, 8);     // 9 * 92%
    EXPECT(b1->economy_total, 5);  // 6 * 84%
    EXPECT(b1->unk_total, 4);

    // Trade with faction 2's best base: (12 + 4 + 7) / 8, times 3 for the
    // commerce bonus, over 2; halved without a pact.
    factions[1].diplo_status[2] = TREATY_TREATY;
    factions[2].base_count = 1, factions[1].tech_commerce_bonus = 2;
    bases[2].intake[RESOURCE_ENERGY] = 12;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    base_energy();
    EXPECT(import[2], 1);
    EXPECT(export[2], 0);
    factions[1].diplo_status[2] = TREATY_TREATY | TREATY_PACT;
    governor = 2;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    base_energy();
    EXPECT(import[2], 3);
    EXPECT(export[2], 2);  // 1, and 1 for the governor
    EXPECT(b1->intake_2[RESOURCE_ENERGY], 23);
    EXPECT(b1->surplus[RESOURCE_ENERGY], 21);
    passes[PROPOSAL_GLOBAL_TRADE_PACT] = 1;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    base_energy();
    EXPECT(import[2], 6);
    factions[2].sanction_turns = 1;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    base_energy();
    EXPECT(import[2], 0);
    EXPECT(b1->intake_2[RESOURCE_ENERGY], 20);
    factions[2].sanction_turns = 0, governor = 0, passes[PROPOSAL_GLOBAL_TRADE_PACT] = 0;
    factions[1].diplo_status[2] = 0;

    // An alien base's energy grid: half of 2 facilities and a project, and
    // of the 1 base_minerals() left behind (its eco-damage divisor).
    expansion = 1, faction_defs[1].rule_flags = RULE_FLAG_ALIEN;
    give(1, FAC_RECYCLING_TANKS);
    give(1, FAC_PRESSURE_DOME);
    projects[SP_VIRTUAL_WORLD] = 1;
    b1->intake_2[RESOURCE_ENERGY] = 20;
    base_energy();
    EXPECT(b1->intake_2[RESOURCE_ENERGY], 24);  // (1 + 2 + 5) / 2
    expansion = 0, faction_defs[1].rule_flags = 0, projects[SP_VIRTUAL_WORLD] = -1;
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));

    // A technician gives 3 economy; once obsolete, an engineer takes its
    // slot. No imbalance with 5 labs and 5 economy.
    factions[1].alloc_psych = 0, factions[1].alloc_labs = 5;
    citizens[0].preq_tech = TECH_NONE, citizens[0].econ = 3;
    citizens[1].preq_tech = 5, citizens[1].econ = 3, citizens[1].labs = 2;
    b1->specialist_total = 1, b1->specialist_types[0] = 0;
    b1->intake_2[RESOURCE_ENERGY] = 0;
    base_energy();
    EXPECT(b1->economy_total, 3);
    EXPECT(b1->labs_total, 0);
    citizens[0].obsol_tech = 5;
    base_energy();
    EXPECT(b1->specialist_types[0], 0);  // the engineer isn't available yet
    achieved[5] = 1 << 1;
    base_energy();
    EXPECT(b1->specialist_types[0], 1);
    EXPECT(b1->labs_total, 2);
    // A network node: labs times 6/4, rounded up; the Supercollider doubles.
    give(1, FAC_NETWORK_NODE);
    base_energy();
    EXPECT(b1->labs_total, 3);
    projects[SP_SUPERCOLLIDER] = 1;
    base_energy();
    EXPECT(b1->labs_total, 6);
    projects[SP_SUPERCOLLIDER] = -1;
    // Stockpile Energy from its second turn: half the minerals, rounded up.
    b1->queue[0] = b1->production_last = -FAC_STOCKPILE_ENERGY;
    b1->surplus[RESOURCE_MINERAL] = 7;
    base_energy();
    EXPECT(b1->economy_total, 7);
    b1->production_last = 0;
    base_energy();
    EXPECT(b1->economy_total, 3);
    // The seventeenth specialist has no slot and is chosen afresh.
    b1->specialist_total = 17;
    citizens[2].preq_tech = TECH_NONE, citizens[2].psych = 2;
    base_energy();
    EXPECT(b1->psych_total, 2);

    // Talents and drones, for base 1 of 5 citizens: 3 content at the AI's
    // level, so 2 drones.
    memset(b1, 0, sizeof(*b1));
    b1->x = 6, b1->y = 4, b1->faction = 1, b1->pop_size = 5;
    factions[1].base_count = 1;
    upkeep_state = 0;
    base_psych();
    EXPECT(b1->drone_total, 2);
    EXPECT(b1->talent_total, 0);
    // Psych energy: a talent for every 2; a talent then cancels a drone
    // when they no longer fit.
    b1->psych_total = 4;
    base_psych();
    EXPECT(b1->talent_total, 2);
    EXPECT(b1->drone_total, 2);
    b1->psych_total = 8;
    base_psych();
    EXPECT(b1->talent_total, 3);
    EXPECT(b1->drone_total, 1);
    EXPECT(talents[0], 0);
    EXPECT(drones[0], 2);
    EXPECT(talents[1], 3);
    b1->psych_total = 0;
    // TALENT both ways, and drones for too many bases (1 allowed here).
    factions[1].se_pending[SE_TALENT] = -2;
    base_psych();
    EXPECT(b1->drone_total, 4);
    factions[1].se_pending[SE_TALENT] = 2;
    base_psych();
    EXPECT(b1->talent_total, 2);
    factions[1].se_pending[SE_TALENT] = 0;
    factions[1].base_count = 3;
    base_psych();
    EXPECT(b1->drone_total, 4);
    // Drones past the population become superdrones.
    b1->pop_size = 2;
    factions[1].se_pending[SE_TALENT] = -2;
    base_psych();
    EXPECT(b1->drone_total, 2);
    EXPECT(b1->superdrone_total, 2);
    b1->pop_size = 5;
    factions[1].se_pending[SE_TALENT] = 0, factions[1].base_count = 1;
    // Police: a unit calms a drone, twice with the ability; POLICE 3 one
    // more. A unit that isn't for combat doesn't count.
    unit_here = 1;
    base_psych();
    EXPECT(b1->drone_total, 1);
    protos[0].abilities = ABILITY_POLICE_2X;
    base_psych();
    EXPECT(b1->drone_total, 0);
    protos[0].abilities = 0;
    factions[1].se_pending[SE_POLICE] = 3;
    base_psych();
    EXPECT(b1->drone_total, 0);
    factions[1].se_pending[SE_POLICE] = 0;
    protos[0].plan = 4;
    base_psych();
    EXPECT(b1->drone_total, 2);
    protos[0].plan = 0, unit_here = 0;
    // Units away from home: POLICE -3 counts all but one, -5 each twice.
    pacifism = 2;
    factions[1].se_pending[SE_POLICE] = -3;
    base_psych();
    EXPECT(b1->drone_total, 3);
    factions[1].se_pending[SE_POLICE] = -5;
    base_psych();
    EXPECT(b1->drone_total, 5);  // 6, at most the population
    factions[1].se_pending[SE_POLICE] = 0, pacifism = 0;
    // Facilities, projects, and a Punishment Sphere.
    rules_basic.drones_genejack = 1;
    give(1, FAC_GENEJACK_FACTORY);
    base_psych();
    EXPECT(b1->drone_total, 3);
    give(1, FAC_RECREATION_COMMONS);
    base_psych();
    EXPECT(b1->drone_total, 1);
    projects[SP_HUMAN_GENOME_PROJECT] = 1;
    base_psych();
    EXPECT(b1->talent_total, 1);
    projects[SP_HUMAN_GENOME_PROJECT] = -1;
    projects[SP_LONGEVITY_VACCINE] = 1;
    factions[1].se_models_pending[1] = ECONOMICS_PLANNED;
    b1->psych_total = 2;
    base_psych();
    EXPECT(b1->drone_total, 0);  // 1 - 2, kept at 0
    EXPECT(b1->talent_total, 1);
    projects[SP_LONGEVITY_VACCINE] = -1;
    factions[1].se_models_pending[1] = 0;
    give(1, FAC_PUNISHMENT_SPHERE);
    base_psych();
    EXPECT(b1->drone_total, 0);
    EXPECT(b1->talent_total, 0);
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));
    b1->psych_total = 0;
    // At the end of a turn, every TALENT and POLICE is scored: at 0 and 0,
    // 2 drones and no talents is -2, less 5, doubled for a small base; at
    // TALENT 4, 3 talents and a drone is 2, halved for the drone.
    upkeep_state = 1;
    memset(factions[1].social_psych, 0, sizeof(factions[1].social_psych));  // base_energy() ran it before
    base_psych();
    EXPECT(factions[1].social_psych[3][5], -14);
    EXPECT(factions[1].social_psych[7][5], 1);
    EXPECT(b1->drone_total, 2);

    // Support: three armed units at base 1, two free at SUPPORT 0, so the
    // third costs a mineral; all three cost 2 at SUPPORT -4.
    upkeep_state = 0;
    put(6, 4, 3, 1, 0, MAP_BASE);
    at(6, 4)->owner_site = 1, at(6, 4)->territory = 1;
    weapons[1].offense = 1;
    protos[1].weapon = 1, protos[1].plan = PLAN_OFFENSIVE;
    protos[2].plan = PLAN_SUPPLY;
    for (int v = 1; v <= 3; v++) vehs[v].x = 6, vehs[v].y = 4, vehs[v].faction = 1, vehs[v].home_base = 1, vehs[v].proto = 1;
    veh_count = 4;
    factions[1].armed_units = 0;
    base_support();
    EXPECT(supported, 3);
    EXPECT(paid, 1);
    EXPECT(upkeep, 1);
    EXPECT(vehs[2].state & VEH_STATE_SUPPORTED, 0);
    EXPECT(vehs[3].state & VEH_STATE_SUPPORTED, VEH_STATE_SUPPORTED);
    EXPECT(factions[1].armed_units, 6);  // offensive units count twice
    factions[1].se_pending[SE_SUPPORT] = -4;
    base_support();
    EXPECT(paid, 3);
    EXPECT(upkeep, 6);
    factions[1].se_pending[SE_SUPPORT] = 0;
    // At the end of a turn, the cost at each SUPPORT.
    upkeep_state = 1;
    b1->intake_2[RESOURCE_MINERAL] = 10;
    memset(factions[1].social_support, 0, sizeof(factions[1].social_support));
    base_support();
    EXPECT(factions[1].social_support[0], 6);
    EXPECT(factions[1].social_support[4], 1);
    EXPECT(factions[1].social_support[7], 0);
    // With few minerals a cost weighs more: 1 against 1 mineral counts 3.
    b1->intake_2[RESOURCE_MINERAL] = 1;
    memset(factions[1].social_support, 0, sizeof(factions[1].social_support));
    base_support();
    EXPECT(factions[1].social_support[4], 3);
    upkeep_state = 0;
    // Two units outside the faction's territory: at POLICE -3 the first is
    // let off, the second makes a drone.
    at(0, 0)->territory = -1;
    vehs[2].x = vehs[3].x = 0, vehs[2].y = vehs[3].y = 0;
    factions[1].se_pending[SE_POLICE] = -3;
    base_support();
    EXPECT(pacifism, 2);
    EXPECT(vehs[2].state & (VEH_STATE_PACIFISM_FREE | VEH_STATE_PACIFISM_DRONE), VEH_STATE_PACIFISM_FREE);
    EXPECT(vehs[3].state & (VEH_STATE_PACIFISM_FREE | VEH_STATE_PACIFISM_DRONE), VEH_STATE_PACIFISM_DRONE);
    factions[1].se_pending[SE_POLICE] = 0;
    base_support();
    EXPECT(vehs[3].state & VEH_STATE_PACIFISM_DRONE, 0);
    // Convoys: this base's brings in its square's nutrients; another
    // base's, standing here, brings in 1.
    vehs[4] = (struct veh){.x = 4, .y = 2, .faction = 1, .home_base = 1, .proto = 2, .order = ORDER_CONVOY,
                           .order_auto_type = RESOURCE_NUTRIENT, .next_in_stack = -1, .prev_in_stack = -1};
    vehs[5] = (struct veh){.x = 6, .y = 4, .faction = 1, .home_base = 0, .proto = 2, .order = ORDER_CONVOY,
                           .order_auto_type = RESOURCE_ENERGY, .next_in_stack = -1, .prev_in_stack = -1};
    veh_count = 6;
    base_support();
    EXPECT(to[RESOURCE_NUTRIENT], crop_yield(1, 1, 4, 2, 0));
    EXPECT(to[RESOURCE_ENERGY], 1);
    EXPECT(from[RESOURCE_NUTRIENT], 0);
    vehs[4].x = 6, vehs[4].y = 4;
    base_support();
    EXPECT(to[RESOURCE_NUTRIENT], 0);
    EXPECT(from[RESOURCE_NUTRIENT], 1);
    veh_count = 1;

    // Placing citizens: base 1 of 3, with nothing worked yet, works its own
    // square and 3 others, or keeps specialists; the intake is what the
    // worked squares yield.
    int32_t workers = 0, food_ok = 0, turn = 1;
    game_base_workers = &workers, game_base_reserved = &reserved, game_base_food_ok = &food_ok;
    game_current_turn = &turn, game_base_square_flags = square_flags;
    game_add_goal = add_goal_stub, game_stack_check = stack_check_stub;
    factions[1].flags |= FACTION_FLAG_MAP_REVEALED;
    game_radius_x = radius_x, game_radius_y = radius_y;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            if ((x + y) % 2 == 0 && !(at(x, y)->bits & MAP_BASE)) at(x, y)->climate = (uint8_t)(3 << 5 | (x % 3) << 3);
    veh_count = 1;
    memset(b1, 0, sizeof(*b1));
    b1->x = 6, b1->y = 4, b1->faction = 1, b1->pop_size = 3;
    current = b1, current_id = 1;
    base_yield();
    EXPECT(b1->worked_tiles & 1, 1);
    EXPECT(__builtin_popcount(b1->worked_tiles) + b1->specialist_total, 4);
    for (int r = 0; r < RESOURCE_UNUSED; r++) {
        EXPECT(b1->intake[r], worked_yield(1, r));
        EXPECT(b1->intake_2[r], b1->intake[r]);
    }
    // Worked again, the squares are kept.
    uint32_t chosen = b1->worked_tiles;
    base_yield();
    EXPECT(b1->worked_tiles, chosen);
    // A square in another faction's territory is let go and not chosen.
    int taken = __builtin_ctz(chosen & ~1u);
    struct map_tile *foreign = at((b1->x + radius_x[taken] + W) % W, b1->y + radius_y[taken]);
    foreign->territory = 2;
    factions[1].diplo_status[2] |= TREATY_COMMLINK | TREATY_UNK_8000000;  // known as faction 2's
    base_yield();
    EXPECT(square_flags[taken], SQUARE_FOREIGN);
    EXPECT(b1->worked_tiles & (1u << taken), 0);
    EXPECT(__builtin_popcount(b1->worked_tiles) + b1->specialist_total, 4);
    foreign->territory = -1;
    factions[1].diplo_status[2] = 0;
    // The other reasons: off the map, the base itself, past the radius, a
    // nearby base working the square, units stack_check() objects to.
    bases[0].worked_tiles = 1 << 2;  // (4, 2), radius entry 14 of base 1
    hostile = 1, unit_here = 1;
    farm_compute();
    EXPECT(square_flags[15], SQUARE_UNSEEN);  // y 8
    EXPECT(square_flags[0], SQUARE_BASE | SQUARE_HOSTILE);
    EXPECT(square_flags[22], SQUARE_OUTSIDE);
    EXPECT(square_flags[14], SQUARE_NEIGHBOUR);
    EXPECT(reserved & (1 << 14 | 1 << 15 | 1), 1 << 14 | 1 << 15 | 1);
    bases[0].worked_tiles = 0, hostile = 0, unit_here = 0;
    // Unseen squares are kept from the base too.
    factions[1].flags &= ~(uint32_t)FACTION_FLAG_MAP_REVEALED;
    farm_compute();
    EXPECT(reserved, (1 << 21) - 1);
    factions[1].flags |= FACTION_FLAG_MAP_REVEALED;
    // Satellites add up to one a citizen, halved without an Aerospace
    // Complex.
    projects[SP_SPACE_ELEVATOR] = -1;
    factions[1].satellites[RESOURCE_MINERAL] = 9;
    b1->worked_tiles = 1;
    base_yield();
    EXPECT(b1->intake[RESOURCE_MINERAL], 3 + worked_yield(1, RESOURCE_MINERAL));
    factions[1].satellites[RESOURCE_MINERAL] = 3;
    base_yield();
    EXPECT(b1->intake[RESOURCE_MINERAL], 2 + worked_yield(1, RESOURCE_MINERAL));
    factions[1].satellites[RESOURCE_MINERAL] = 0;
    // Terraforming damage: a mine counts once, twice on a worked square.
    // With every square unseen, only the base square is worked.
    factions[1].flags &= ~(uint32_t)FACTION_FLAG_MAP_REVEALED;
    b1->worked_tiles = 1;
    base_yield();
    EXPECT(b1->worked_tiles, 1);
    EXPECT(b1->specialist_total, 3);
    // They become doctors: psych 2 counts 6, more than the engineer's 3
    // economy and 2 labs.
    EXPECT(b1->specialist_types[0] & 0xfff, 0x222);
    int damage = b1->eco_damage;
    struct map_tile *mined = at((b1->x + radius_x[1]) % W, b1->y + radius_y[1]);
    uint32_t mined_bits = mined->bits;
    mined->bits = mined_bits & ~(uint32_t)0x1c8805c;
    base_yield();
    damage = b1->eco_damage;
    mined->bits |= MAP_MINE;
    base_yield();
    EXPECT(b1->eco_damage, damage + 1);
    at(6, 4)->bits |= MAP_MINE;
    base_yield();
    EXPECT(b1->eco_damage, damage + 3);
    at(6, 4)->bits &= ~(uint32_t)MAP_MINE;
    mined->bits = mined_bits;
    factions[1].flags |= FACTION_FLAG_MAP_REVEALED;
    give(1, FAC_TREE_FARM);
    give(1, FAC_HYBRID_FOREST);
    base_yield();
    EXPECT(b1->eco_damage, 0);
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));

    // base_compute: only for a new base, or when asked.
    int32_t computed = 1;
    game_computed_base_id = &computed;
    b1->worked_tiles = 1, b1->surplus[RESOURCE_ENERGY] = -77;
    base_compute(0);
    EXPECT(b1->surplus[RESOURCE_ENERGY], -77);
    base_compute(1);
    EXPECT(b1->surplus[RESOURCE_ENERGY] != -77, 1);
    EXPECT(b1->intake[RESOURCE_NUTRIENT], worked_yield(1, RESOURCE_NUTRIENT));
    computed = 0, b1->surplus[RESOURCE_ENERGY] = -77;
    base_compute(0);
    EXPECT(computed, 1);
    EXPECT(b1->surplus[RESOURCE_ENERGY] != -77, 1);

    // Population goals: a sixth of the way to 36, at least 6, held to the
    // habitation limits (7 and 14 here, 2 more with the Ascetic Virtues).
    rules_basic.pop_limit_hab_complex = 7, rules_basic.pop_limit_hab_dome = 14;
    b1->pop_size = 3;
    EXPECT(pop_goal(1), 7);  // 33 / 6 + 3 = 8, held to 7
    projects[SP_ASCETIC_VIRTUES] = 1;
    EXPECT(pop_goal(1), 8);
    projects[SP_ASCETIC_VIRTUES] = -1;
    b1->pop_size = 7;
    EXPECT(pop_goal_fac(1), FAC_HAB_COMPLEX);
    give(1, FAC_HAB_COMPLEX);
    EXPECT(pop_goal_fac(1), 0);
    EXPECT(pop_goal(1), 11);  // 29 / 6 + 7
    b1->pop_size = 14;
    EXPECT(pop_goal_fac(1), FAC_HABITATION_DOME);
    EXPECT(pop_goal(1), 14);
    b1->pop_size = 0;
    EXPECT(pop_goal(1), 6);  // 36 / 6
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));

    // Cost factors: 10 for a human; the AI's by difficulty, less 1 for a
    // far stronger faction at the top. GROWTH 2 makes growth cost 8/10.
    int32_t rankings[8] = {0}, cost_base[6] = {13, 12, 11, 10, 8, 7}, net = 0, settings[7] = {2};
    game_faction_rankings = rankings, game_cost_base = cost_base, game_net_multiplayer = &net;
    game_map_settings = settings, game_great_satan = great_satan_stub;
    rules_basic.nutrient_cost_multi = rules_basic.mineral_cost_multi = 10;
    difficulty = 3;
    EXPECT(cost_factor(1, 1, -1), 10);
    satan = 1;
    EXPECT(cost_factor(1, 1, -1), 9);
    rankings[7] = 1, humans = 1 << 1;
    EXPECT(cost_factor(1, 1, -1), 10);  // human
    EXPECT(cost_factor(2, 1, -1), 8);   // 10, less the satan, less a human on top
    humans = 0, satan = 0, rankings[7] = 0;
    factions[1].se_pending[SE_GROWTH] = 2;
    EXPECT(cost_factor(1, 0, -1), 8);  // (8 * 10 + 9) / 10
    give(1, FAC_CHILDREN_CRECHE);
    EXPECT(cost_factor(1, 0, 1), 6);   // GROWTH 4
    factions[1].se_pending[SE_GROWTH] = 9;
    EXPECT(cost_factor(1, 0, 1), 5);   // at most 5
    factions[1].se_pending[SE_GROWTH] = 0;
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));
    // INDUSTRY in steps; the planet size; the rules' multiplier.
    factions[1].se_pending[SE_INDUSTRY] = 2;
    EXPECT(cost_factor(1, 1, -1), 8);
    factions[1].se_pending[SE_INDUSTRY] = -3;
    EXPECT(cost_factor(1, 1, -1), 13);
    factions[1].se_pending[SE_INDUSTRY] = 9;
    EXPECT(cost_factor(1, 1, -1), 5);
    factions[1].se_pending[SE_INDUSTRY] = 0;
    settings[0] = 0;
    EXPECT(cost_factor(1, 1, -1), 8);
    settings[0] = 2, rules_basic.mineral_cost_multi = 15;
    EXPECT(cost_factor(1, 1, -1), 15);
    EXPECT(cost_factor(1, 2, -1), 15);  // any other type: the factor alone
    rules_basic.mineral_cost_multi = 10;

    // Growth. A citizen costs 10 nutrients here, so base 1 of size 3 grows
    // with 40 stored, into a store emptied and refilled by its surplus.
    static struct rules_facility facility_rules[FACILITY_COUNT] = {[FAC_HAB_COMPLEX] = {.name = "complex"}};
    game_facilities = facility_rules;
    game_growth_texts = growth_texts;
    game_popb = popb_stub, game_parse_say = parse_say_stub, game_is_objective = is_objective_stub;
    game_base_kill = base_kill_stub, game_eliminate_player = eliminate_stub;
    game_draw_tile = draw_tile_stub, game_draw_map = draw_map_stub;
    struct base saved_b1 = *b1;
    current = b1, current_id = 1, growth = 0;
    b1->pop_size = 3, b1->nutrients = 10, b1->surplus[RESOURCE_NUTRIENT] = 5;
    EXPECT(base_growth(), 0);
    EXPECT(b1->nutrients, 15);
    EXPECT(b1->pop_size, 3);
    b1->nutrients = 40;
    EXPECT(base_growth(), 0);
    EXPECT(b1->pop_size, 4);
    EXPECT(popb_count, 0);
    // At the habitation limit (7) it keeps half the store, and says what
    // it needs.
    b1->pop_size = 7, b1->nutrients = 80, b1->surplus[RESOURCE_NUTRIENT] = 1;
    base_growth();
    EXPECT(b1->pop_size, 7);
    EXPECT(b1->nutrients, 41);
    EXPECT(popb_label == game_growth_text(0x6894fc), 1);
    EXPECT(say_text == facility_rules[FAC_HAB_COMPLEX].name, 1);
    faction_defs[1].rule_flags |= RULE_FLAG_ALIEN;
    b1->nutrients = 80;
    base_growth();
    EXPECT(popb_label == game_growth_text(0x6894e0), 1);
    faction_defs[1].rule_flags &= ~RULE_FLAG_ALIEN;
    // In stagnation the store stays full.
    growth = -3, b1->pop_size = 3, b1->nutrients = 45;
    base_growth();
    EXPECT(b1->nutrients, 40);
    EXPECT(b1->pop_size, 3);
    // In a boom it grows on a surplus of a citizen's food, with the store
    // held to a citizen's cost.
    growth = 6, b1->nutrients = 50, b1->surplus[RESOURCE_NUTRIENT] = 2;
    base_growth();
    EXPECT(b1->pop_size, 4);
    EXPECT(b1->nutrients, 40);
    growth = 0;

    // Starving: a citizen lost and the store started again; nutrient
    // convoys in a base stop.
    struct veh saved_veh = vehs[0];
    int32_t saved_count = veh_count;
    veh_count = 1;
    memset(&vehs[0], 0, sizeof(vehs[0]));
    vehs[0].x = 6, vehs[0].y = 4, vehs[0].home_base = 1, vehs[0].order = ORDER_CONVOY, vehs[0].prev_in_stack = -1;
    vehs[0].next_in_stack = -1, vehs[0].proto = 2;
    uint8_t plan = protos[2].plan;
    protos[2].plan = PLAN_SUPPLY;
    uint32_t base_bits = at(6, 4)->bits;
    uint8_t owner = at(6, 4)->owner_site;
    at(6, 4)->bits |= MAP_BASE, at(6, 4)->owner_site = 1;
    popb_count = 0, from[RESOURCE_NUTRIENT] = 1;
    b1->pop_size = 3, b1->nutrients = -2, b1->surplus[RESOURCE_NUTRIENT] = -2;
    EXPECT(base_growth(), 0);
    EXPECT(b1->pop_size, 2);
    EXPECT(b1->nutrients, -2);
    EXPECT(vehs[0].order, 0);
    EXPECT(popb_label == game_growth_text(0x6894c8) && popb_flags == 0x4000, 1);
    // Not in a finished game, though.
    state = GAME_STATE_DONE, b1->nutrients = -1;
    base_growth();
    EXPECT(b1->pop_size, 2);
    EXPECT(popb_count, 1);
    state = 0;
    // A last objective of size 1 is kept; any other base is lost.
    b1->pop_size = 1, b1->nutrients = -1, objective = 1;
    EXPECT(base_growth(), 0);
    EXPECT(b1->pop_size, 1);
    EXPECT(killed, 0);
    objective = 0, b1->nutrients = -1;
    EXPECT(base_growth(), 1);
    EXPECT(killed, 2);
    EXPECT(eliminated, 1);
    protos[2].plan = plan;
    at(6, 4)->bits = base_bits, at(6, 4)->owner_site = owner;
    vehs[0] = saved_veh, veh_count = saved_count;

    // Stores that will run out within five turns: advice. With moist
    // land to farm, to build farms (with formers) or formers; or else a
    // warning. The only land in reach is one moist square.
    static struct map_tile saved_tiles[W / 2 * H];
    memcpy(saved_tiles, tiles, sizeof(tiles));
    for (int i = 0; i < W / 2 * H; i++) tiles[i].climate = 0;
    at(7, 3)->climate = 3 << 5 | 1 << 3, at(7, 3)->bits = 0;
    protos[1].preq_tech = -1;
    b1->pop_size = 3, b1->nutrients = 2, b1->surplus[RESOURCE_NUTRIENT] = -1, b1->worked_tiles = 1;
    base_growth();
    EXPECT(b1->nutrients, 1);
    EXPECT(popb_label == game_growth_text(0x6895ec), 1);
    factions[1].units_active[PROTO_FORMERS] = 1;
    b1->nutrients = 2;
    base_growth();
    EXPECT(popb_label == game_growth_text(0x6895c8), 1);
    factions[1].units_active[PROTO_FORMERS] = 0;
    // Worked rocky land is no farmland.
    at(7, 3)->rocky_lock = 0x80, b1->worked_tiles = 1 | 1 << 1;
    b1->nutrients = 2, b1->specialist_total = 0;
    base_growth();
    EXPECT(popb_label == game_growth_text(0x689658) && popb_flags == 0x80, 1);
    b1->nutrients = 2, b1->specialist_total = 2, drones[0] = 1;
    base_growth();
    EXPECT(popb_label == game_growth_text(0x689610), 1);
    // Not while there are six turns to go, nor for a governor placing
    // citizens.
    popb_count = 0;
    b1->nutrients = 7;
    base_growth();
    b1->nutrients = 2, b1->governor = GOVERNOR_ACTIVE | GOVERNOR_CITIZENS;
    base_growth();
    EXPECT(popb_count, 0);
    drones[0] = 0;
    memcpy(tiles, saved_tiles, sizeof(tiles));
    *b1 = saved_b1;

    // The queue moves up past what can no longer be built.
    game_veh_avail = veh_avail_stub, game_facility_avail = facility_avail_stub, game_wave_it = wave_stub;
    game_draw_radius = draw_radius_stub;
    saved_b1 = *b1;
    b1->queue_size = 4;
    b1->queue[0] = 2, b1->queue[1] = 7, b1->queue[2] = -9, b1->queue[3] = -(FAC_FIRST_PROJECT + 3);
    factions[1].units_queue[2] = 1, factions[1].units_queue[7] = 1;
    EXPECT(base_queue(1), 1);
    EXPECT(b1->queue_size, 1);
    EXPECT(b1->queue[0], -(FAC_FIRST_PROJECT + 3));
    EXPECT(factions[1].units_queue[2], 0);
    EXPECT(factions[1].units_queue[7], 0);
    EXPECT(sound, 36);
    // Emptied, it still tries what is left past the end: here unit 0.
    b1->queue[1] = 0;
    EXPECT(base_queue(1), 1);
    EXPECT(b1->queue_size, 0);
    b1->queue[1] = 7;
    b1->queue_size = 1;
    EXPECT(base_queue(1), 0);
    // A full queue moves up the word after it, the worked squares.
    b1->queue_size = 10, b1->worked_tiles = 3;
    for (int i = 0; i < 10; i++) b1->queue[i] = -9;
    EXPECT(base_queue(1), 1);
    EXPECT(b1->queue_size, 0);
    EXPECT(b1->queue[0], 3);
    *b1 = saved_b1;

    // Production, for an AI base with a unit in it.
    static struct rules_facility prod_rules[FACILITY_COUNT];
    prod_rules[FAC_RECYCLING_TANKS].cost = 4, prod_rules[FAC_SKY_HYDRO_LAB].cost = 12;
    prod_rules[FAC_FIRST_PROJECT + 5].cost = 20;
    game_facilities = prod_rules;
    game_popup_ctor = game_popup_close = game_scroll_close = game_flat_button_dtor = pop_stub;
    game_graphic_win_dtor = game_base_pop_dtor = pop_stub;
    game_base_reset = reset_stub, game_veh_cost = veh_cost_stub, game_veh_init = veh_init_stub;
    game_morale_mod = morale_stub;
    game_mon_prototype_built = game_mon_native_life_bred = game_mon_naval_unit_built = mon_stub;
    game_mon_air_unit_built = game_mon_first_in_space = game_mon_built_preserve = mon_stub;
    game_mon_facility_built = mon_facility_stub;
    game_parse_num = parse_num_stub, game_parse_says = parse_says_stub, game_set_dirty = set_dirty_stub;
    static char *script = "script";
    game_popp = popp_stub, game_script_file = &script;
    game_base_pop_init = pop_init_stub, game_base_pop_set_width = pop_width_stub, game_base_pop_string = pop_string_stub;
    game_popup_start = pop_start_stub, game_base_pop_exec_3 = pop_exec_stub, game_pcx_dimensions = pcx_stub;
    game_interlude = interlude_stub, game_help_project = game_amovie_project = help_stub;
    static uint8_t image[0x300000];  // the game's strings, all empty, up to 0x700000
    game_test_image = image;
    static char str_buffer[1024];
    game_str_buffer = str_buffer;
    int32_t screen_width = 1024, prefs = 0, more_prefs = 0;
    static char pop_file[16];
    game_screen_width = &screen_width, game_pop_file = pop_file, game_preferences = &prefs;
    game_more_preferences = &more_prefs;
    int32_t player = 7, warnings = 0, mp = 0, build_blocked = 0, proto_upkeep = 0, map_area = W / 2 * H;
    game_current_faction = &player, game_warnings = &warnings, game_net_multiplayer = &mp;
    game_base_build_blocked = &build_blocked, game_base_proto_upkeep = &proto_upkeep;
    if (!game_map_area) game_map_area = &map_area;
    saved_b1 = *b1;
    struct faction_data saved_f1 = factions[1];
    uint32_t garrison_bits = at(6, 4)->bits;
    uint8_t garrison_owner = at(6, 4)->owner_site;
    at(6, 4)->bits |= MAP_UNIT, at(6, 4)->owner_site = 1;
    current = b1, current_id = 1, humans = 0, difficulty = 3;
    rules_basic.mineral_cost_multi = 10;
    b1->state = 0, b1->nutrients = 0, b1->pop_size = 3, b1->queue_size = 0;
    b1->queue[0] = -FAC_RECYCLING_TANKS, b1->minerals = 10, b1->surplus[RESOURCE_MINERAL] = 5;
    // 15 minerals, not yet the 40 the tanks cost.
    resets = 0;
    base_production();
    EXPECT(b1->minerals, 15);
    EXPECT(resets, 0);
    EXPECT(has_fac(FAC_RECYCLING_TANKS, 1, 0), 0);
    // Done: built, paid for, and the AI chooses again.
    b1->minerals = 36;
    base_production();
    EXPECT(has_fac(FAC_RECYCLING_TANKS, 1, 0), 1);
    EXPECT(b1->minerals, 1);
    EXPECT(resets, 1);
    EXPECT((b1->state & 0x800000) != 0, 1);
    // Already built, with nothing after it: the AI chooses again.
    resets = 0;
    base_production();
    EXPECT(resets, 1);
    // In riots no minerals are added; with stores below zero, nothing at all.
    b1->state = 2, b1->minerals = 0;
    base_production();
    EXPECT(b1->minerals, 0);
    b1->state = 0, b1->nutrients = -1;
    resets = 0;
    base_production();
    EXPECT(b1->minerals, 0);
    EXPECT(resets, 0);
    b1->nutrients = 0;
    // Without a unit in the base, the AI chooses first.
    at(6, 4)->bits &= ~(uint32_t)MAP_UNIT;
    base_production();
    EXPECT(resets, 2);
    at(6, 4)->bits |= MAP_UNIT;
    // A unit: 30 minerals (3 rows of 10), with morale from the base, plus 1.
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));
    b1->queue[0] = 1, b1->minerals = 26, vehs_made = 0, resets = 0;
    base_production();
    EXPECT(vehs_made, 1);
    EXPECT(b1->minerals, 1);
    EXPECT(vehs[5].morale, 2);
    EXPECT((vehs[5].state & 0x2000) != 0, 1);
    EXPECT(resets, 1);
    // Blocked, nothing is built but it is paid for.
    build_blocked = 1, b1->minerals = 30;
    base_production();
    EXPECT(vehs_made, 1);
    EXPECT(b1->minerals, 5);
    build_blocked = 0;
    // A satellite: the faction's first reveals the map to it.
    b1->queue[0] = -FAC_SKY_HYDRO_LAB, b1->minerals = 120;
    factions[1].flags &= ~(uint32_t)FACTION_FLAG_MAP_REVEALED;
    factions[1].satellites[0] = 0, tiles[0].visibility = 0, monuments = 0;
    base_production();
    EXPECT(factions[1].satellites[0], 1);
    EXPECT((factions[1].flags & FACTION_FLAG_MAP_REVEALED) != 0, 1);
    EXPECT(tiles[0].visibility & 2, 2);
    EXPECT(monuments, 1);
    EXPECT(popps, 1);  // the player hears of it
    // A secret project another base holds: the AI chooses again; one free
    // to build is the base's once paid for.
    b1->queue[0] = -(FAC_FIRST_PROJECT + 5), b1->minerals = 300, resets = 0;
    projects[5] = 0;
    base_production();
    EXPECT(resets, 1);
    EXPECT(b1->minerals, 305);
    projects[5] = -1;
    game_mon_secret_project = (void (*)(int, int))mon_facility_stub;
    base_production();
    EXPECT(projects[5], 1);
    EXPECT(b1->minerals, 110);  // 305 and 5 more, less 200
    EXPECT(news, 1);  // the player is shown it
    projects[5] = -1;
    at(6, 4)->bits = garrison_bits, at(6, 4)->owner_site = garrison_owner;
    factions[1] = saved_f1;
    *b1 = saved_b1;

    // Facility upkeep: as the rules say, none for a free one, and the
    // command center's by reactor, at most half the difficulty.
    game_best_reactor = best_reactor_stub, game_tech_research = tech_research_stub;
    prod_rules[FAC_RECYCLING_TANKS].maint = 1, prod_rules[27].maint = 9;
    EXPECT(fac_maint(FAC_RECYCLING_TANKS, 1), 1);
    factions[1].diff_level = 3;
    EXPECT(fac_maint(27, 1), 2);
    factions[1].diff_level = 1, reactor = 3;
    EXPECT(fac_maint(27, 1), 1);
    reactor = -1;
    EXPECT(fac_maint(27, 1), 0);
    faction_defs[1].bonus_count = 1, faction_defs[1].bonus_id[0] = BONUS_FACILITY;
    faction_defs[1].bonus_val1[0] = FAC_RECYCLING_TANKS;
    EXPECT(fac_maint(FAC_RECYCLING_TANKS, 1), 0);
    faction_defs[1].bonus_count = 0;
    // Paid from reserves; halved by the Self-Aware Colony, odd units let
    // off every other time.
    saved_b1 = *b1, saved_f1 = factions[1];
    current = b1, current_id = 1;
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));
    give(1, FAC_RECYCLING_TANKS);
    factions[1].energy_credits = 10, factions[1].facility_maint_total = 0;
    base_maint();
    EXPECT(factions[1].energy_credits, 9);
    EXPECT(factions[1].facility_maint_total, 1);
    projects[25] = 1;
    base_maint();
    EXPECT(factions[1].energy_credits, 9);
    base_maint();
    EXPECT(factions[1].energy_credits, 8);
    projects[25] = -1;
    // Unpaid: at difficulty 3 the tanks are sold for their price; at 1 the
    // debt is forgiven.
    factions[1].diff_level = 3, factions[1].energy_credits = 0, popb_count = 0;
    base_maint();
    EXPECT(has_fac(FAC_RECYCLING_TANKS, 1, 0), 0);
    EXPECT(factions[1].energy_credits, 40);
    EXPECT(popb_count, 1);
    give(1, FAC_RECYCLING_TANKS);
    factions[1].diff_level = 1, factions[1].energy_credits = 0;
    base_maint();
    EXPECT(has_fac(FAC_RECYCLING_TANKS, 1, 0), 1);
    EXPECT(factions[1].energy_credits, 0);
    // Research: 7 labs at RESEARCH 0 are 700 hundredths; 33 left over make
    // a whole one with 70 more.
    factions[1].diff_level = 3, factions[1].se_pending[SE_RESEARCH] = 0, factions[1].labs_total = 0;
    factions[1].net_random_event = 33, b1->labs_total = 7, turn = 20;
    base_research();
    EXPECT(researched, 7);
    EXPECT(factions[1].labs_total, 7);
    factions[1].se_pending[SE_RESEARCH] = 2;  // 12 tenths: 840 hundredths
    factions[1].net_random_event = 70;
    base_research();
    EXPECT(researched, 9);
    EXPECT(factions[1].net_random_event, 10);
    factions[1].se_pending[SE_RESEARCH] = -5, turn = 20;  // none before turn 25
    base_research();
    EXPECT(researched, 0);
    // Energy in deficit stops energy convoys sitting in bases.
    struct veh saved_v0 = vehs[0];
    vehs[0].faction = 1, vehs[0].x = 6, vehs[0].y = 4, vehs[0].proto = 2, vehs[0].order = ORDER_CONVOY;
    vehs[0].order_auto_type = RESOURCE_ENERGY;
    protos[2].plan = PLAN_SUPPLY;
    at(6, 4)->bits |= MAP_BASE, at(6, 4)->owner_site = 1;
    veh_count = 1;
    b1->surplus[RESOURCE_ENERGY] = 0;
    base_energy_costs();
    EXPECT(vehs[0].order, ORDER_CONVOY);
    b1->surplus[RESOURCE_ENERGY] = -1;
    base_energy_costs();
    EXPECT(vehs[0].order, 0);
    vehs[0] = saved_v0, protos[2].plan = plan;
    at(6, 4)->bits = garrison_bits, at(6, 4)->owner_site = garrison_owner;
    // Golden ages: no drones, size 3, half talents.
    b1->pop_size = 4, b1->drone_total = 0, b1->talent_total = 2, b1->state = 0, popb_count = 0;
    base_drones();
    EXPECT(b1->state & BASE_STATE_GOLDEN_AGE, BASE_STATE_GOLDEN_AGE);
    EXPECT(popb_count, 1);
    base_drones();
    EXPECT(popb_count, 1);  // said once
    b1->talent_total = 1;
    base_drones();
    EXPECT(b1->state & BASE_STATE_GOLDEN_AGE, 0);
    EXPECT(popb_count, 2);
    // Riots while drones outnumber talents, unless nerve stapled: the first
    // turn warns, and the AI chooses again.
    b1->drone_total = 2, b1->state = 0, resets = 0, popb_count = 0;
    base_drones();
    EXPECT(b1->state & 2, 2);
    EXPECT(resets, 1);
    EXPECT(popb_count, 1);
    b1->nerve_staple_turns = 3, b1->state = 0;
    base_drones();
    EXPECT(b1->state & 2, 0);
    b1->nerve_staple_turns = 0;
    // Riots over: the AI chooses again.
    b1->drone_total = 0, b1->state = 2, resets = 0;
    base_drones();
    EXPECT(b1->state & 2, 0);
    EXPECT(resets, 1);
    // Rushing a headquarters: a tenth of the reserves, at most what is left
    // to build (50 of 100), scaled by the rows left; a little given back.
    int32_t sunspots = 0, ranked = 0;
    game_kill = kill_stub, game_at_goal = no_goal, game_planet_buster2 = no_buster, game_at_climax = no_climax;
    game_sunspot_duration = &sunspots, game_ranked_faction = &ranked;
    prod_rules[FAC_HEADQUARTERS].cost = 10;
    faction_defs[1].rule_hurry = 100;
    strcpy((char *)&faction_defs[1] + 4, "X");
    projects[SP_VOICE_OF_PLANET] = -1;
    humans = 0, difficulty = 3;
    b1->queue[0] = -FAC_HEADQUARTERS, b1->minerals = 50, b1->state = 0;
    factions[1].energy_credits = 1000;
    base_hurry();
    EXPECT(b1->minerals, 61);  // 50 + 50 * 50 / (50 * 50 / 20 + 100)
    EXPECT(factions[1].energy_credits, 951);  // 50 spent, 3 * 50 / 80 back
    b1->minerals = 0;
    base_hurry();
    EXPECT(b1->minerals, 0);  // nothing started, nothing rushed
    // Upkeep beyond the base's minerals: the supported unit furthest away
    // is disbanded.
    struct veh saved_vs[2] = {vehs[0], vehs[1]};
    memset(vehs, 0, 2 * sizeof(vehs[0]));
    for (int v = 0; v < 2; v++) {
        vehs[v].faction = 1, vehs[v].home_base = 1, vehs[v].proto = 1, vehs[v].state = VEH_STATE_SUPPORTED;
        vehs[v].prev_in_stack = vehs[v].next_in_stack = -1;
    }
    vehs[0].x = 6, vehs[0].y = 4, vehs[1].x = 2, vehs[1].y = 2;
    veh_count = 2, upkeep = 3;
    b1->intake_2[RESOURCE_MINERAL] = 2;
    factions[1].se_pending[SE_SUPPORT] = 0;
    base_check_support();
    EXPECT(killed_veh, 1);
    EXPECT(upkeep, 2);
    killed_veh = -1;
    base_check_support();
    EXPECT(killed_veh, -1);  // paid for now
    vehs[0] = saved_vs[0], vehs[1] = saved_vs[1], veh_count = 1;
    factions[1] = saved_f1;
    *b1 = saved_b1;

    // A later riot turn under POLICE -6, at a base of size 4 of a faction
    // with others: it loses the facility the dice pick, the skunkworks.
    static void *map_wins[8];
    game_rand = rand_stub, game_time_get_time = time_stub, game_replay_base = replay_stub, game_spot_base = spot_stub;
    game_propose_proto = propose_stub, game_map_win_set_center = centre_stub, game_reset_territory = territory_stub;
    game_map_wins = map_wins;
    struct faction_data saved_f2 = factions[2], saved_f3 = factions[3];
    factions[1].se_pending[SE_POLICE] = -6, factions[1].base_count = 2, factions[1].diff_level = 1;
    factions[2].base_count = factions[3].base_count = 1;
    b1->pop_size = 4, b1->assimilation_turns = 0, b1->state = 2, b1->faction_former = 1;
    memset(b1->facilities_built, 0, sizeof b1->facilities_built);
    b1->facilities_built[10 / 8] = 1 << (10 % 8);
    rand_value = 10, rand_calls = 0, popb_count = 0;
    struct hook_stale stale = {{0}};
    for (int k = 0; k < 7; k++) HOOK_STALE_AT(&stale, 0x5c - 4 * k) = (uint32_t)-1;
    HOOK_STALE_AT(&stale, 0x5c) = 200;  // no REVOLT anywhere: a chance that takes no pick
    hooks_stale_call(drone_riot, &stale);
    EXPECT(b1->facilities_built[1], 0);
    EXPECT(popb_count, 2);
    EXPECT(rand_calls, 1);
    EXPECT(b1->faction, 1);
    // With nothing left to lose, an AI base stays (after the 24 tries: size
    // 4, difficulty 1, ranking 0)...
    rand_calls = 0;
    hooks_stale_call(drone_riot, &stale);
    EXPECT(rand_calls, 24);
    EXPECT(b1->faction, 1);
    // ...but a human's defects: to the faction that founded it,
    b1->faction_former = 3, humans = 1 << 1, b1->queue_size = 0, b1->queue[0] = -FAC_HEADQUARTERS;
    hooks_stale_call(drone_riot, &stale);
    EXPECT(b1->faction, 3);
    EXPECT(factions[1].base_count, 1);
    EXPECT(factions[3].base_count, 2);
    EXPECT(factions[3].last_base_turn, *game_current_turn);
    EXPECT(b1->queue[0], -FAC_STOCKPILE_ENERGY);
    EXPECT(b1->assimilation_turns, 0);
    EXPECT(replays, 1);
    EXPECT(territory, 1);
    // or else to the nearest other faction's base (base 2's), to be
    // assimilated.
    EXPECT(b1->state & 2, 0);  // the riot is over
    b1->faction = 1, b1->faction_former = 1, factions[1].base_count = 2, b1->state = 2;
    hooks_stale_call(drone_riot, &stale);
    EXPECT(b1->faction, 2);
    EXPECT(b1->assimilation_turns, 50);
    EXPECT(factions[2].base_count, 2);
    // Under POLICE -2 it doesn't.
    b1->faction = 1, factions[1].base_count = 2, factions[1].se_pending[SE_POLICE] = -2, rand_value = 0, b1->state = 2;
    hooks_stale_call(drone_riot, &stale);
    EXPECT(b1->faction, 1);
    humans = 0;
    factions[1] = saved_f1, factions[2] = saved_f2, factions[3] = saved_f3;
    *b1 = saved_b1;

    // Ecological damage, on land everywhere: fungus at (7, 3) spreads to the
    // first square beside it that the dice favour, (8, 4), which wraps to
    // (0, 4). Damage weighs a quarter at PLANET 0.
    static int32_t eco_x[49], eco_y[49], adjacent_x[4] = {1, 1, -1, -1}, adjacent_y[4] = {-1, 1, 1, -1};
    static struct rules_terraform terraform[TERRAFORM_COUNT];
    static struct map_tile eco_tiles[W / 2 * H];
    memcpy(eco_tiles, tiles, sizeof tiles);
    for (int i = 0; i < 49; i++) eco_x[i] = i < 21 ? radius_x[i] : 0, eco_y[i] = i < 21 ? radius_y[i] : 100;
    uint32_t clears = 0;
    int32_t find_dist = 0;
    game_base_find_dist = &find_dist;
    game_radius_x = eco_x, game_radius_y = eco_y, game_adjacent_x = adjacent_x, game_adjacent_y = adjacent_y;
    game_terraform = terraform, game_fungus_clears = &clears;
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) put(x, y, 3, 1, 0, 0);
    at(6, 4)->bits = MAP_BASE, at(6, 4)->owner_site = 1;
    at(2, 2)->bits = MAP_BASE, at(2, 2)->owner_site = 1;
    at(0, 0)->bits = MAP_BASE, at(0, 0)->owner_site = 2;
    at(7, 3)->bits = MAP_FUNGUS;
    b1->eco_damage = 50, b1->pop_size = 3, rand_value = 5, state = 0, humans = 0;
    factions[1].clean_minerals = 0, factions[1].eco_damage_total = 0, factions[1].base_count = 2;
    projects[SP_VOICE_OF_PLANET] = -1;
    base_ecology();
    EXPECT(at(0, 4)->bits & MAP_FUNGUS, MAP_FUNGUS);
    EXPECT(factions[1].clean_minerals, 1);
    EXPECT(factions[1].eco_damage_total, 50);
    // After enough damage, native life: here two locusts (a third of it,
    // under the base's size) on the fungus it grew, at (7, 5) this time,
    // the first open square beside fungus.
    factions[1].clean_minerals = 7, vehs_made = 0;
    base_ecology();
    EXPECT(at(7, 5)->bits & MAP_FUNGUS, MAP_FUNGUS);
    EXPECT(vehs_made, 2);
    EXPECT(vehs[5].proto, 10);
    EXPECT(vehs[5].x * 10 + vehs[5].y, 75);
    // The faction spared this turn gets no native life.
    factions[1].clean_minerals = 7, factions[1].flags |= FACTION_FLAG_ECO_SPARED, vehs_made = 0;
    base_ecology();
    EXPECT(vehs_made, 0);
    EXPECT(factions[1].flags & FACTION_FLAG_ECO_SPARED, 0);
    // Too little damage: nothing.
    b1->eco_damage = 5, factions[1].clean_minerals = 0;
    base_ecology();
    EXPECT(factions[1].clean_minerals, 0);
    memcpy(tiles, eco_tiles, sizeof tiles);
    game_radius_x = radius_x, game_radius_y = radius_y;
    factions[1] = saved_f1;
    *b1 = saved_b1;

    // Base names: the next land name, the first one first; sea names while
    // they last; past the end, the noun and a number.
    game_text_open = text_open_stub, game_text_get = text_get_stub, game_text_item = text_item_stub;
    game_text_close = text_close_stub, game_text_line = &file_ptr;
    strcpy(faction_defs[2].filename, "x"), strcpy(faction_defs[2].noun, "Gaian");
    for (uint32_t a = 0x6893b0; a < 0x6893fc; a += 4) image[a - 0x400000] = 'l';
    image[0x6893b0 - 0x400000] = 's', image[0x6893ec - 0x400000] = 'y';  // the sea section; the shared file
    strcpy((char *)&image[0x6893bc - 0x400000], "#END");
    image[0x682820 - 0x400000] = ' ';
    int32_t map_seed = 0, gender = 0, plural = 0;
    game_map_seed = &map_seed, game_parse_gender = &gender, game_parse_plural = &plural;
    char name[25];
    factions[2].base_name_offset = 0, factions[2].base_sea_name_offset = 0;
    name_base(2, name, 1, 0);
    EXPECT(strcmp(name, "Alpha"), 0);
    EXPECT(factions[2].base_name_offset, 1);
    name_base(2, name, 0, 1);
    EXPECT(strcmp(name, "Reef"), 0);
    name_base(2, name, 1, 1);
    EXPECT(factions[2].base_sea_name_offset, 1);
    name_base(2, name, 1, 1);  // no more sea names: the land list, shuffled from the second on
    EXPECT(strcmp(name, "Beta") && strcmp(name, "Gamma"), 0);
    EXPECT(factions[2].base_name_offset, 2);
    factions[2].base_name_offset = 5;
    name_base(2, name, 1, 0);
    EXPECT(strcmp(name, "Gaian 6"), 0);
    EXPECT(factions[2].base_name_offset, 5);

    // The AI's terraforming, on land it owns: at size 3 with no priorities
    // the base wants minerals (4) more than energy (3); goals are set and
    // roads planned.
    static struct continent regions[4];
    static struct rules_ability abilities[1] = {{.preq_tech = TECH_NONE}};
    static int32_t counts[9] = {1, 9, 21, 21, 21, 21, 21, 21, 21};
    int32_t cloud = 0, area = W / 2 * H;
    static struct map_tile t_tiles[W / 2 * H];
    memcpy(t_tiles, tiles, sizeof tiles);
    game_continents = regions, game_abilities = abilities, game_radius_count = counts, game_cloud_cover = &cloud;
    game_map_area = &area, game_terraform = terraform;
    game_can_terraform = can_terraform_stub, game_base_connect = base_connect_stub;
    game_terrain_avail = terrain_avail_stub, game_abil_index = abil_index_stub, game_base_find = base_find_stub;
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) put(x, y, 3, 1, 0, 0), at(x, y)->territory = 1;
    at(6, 4)->bits = MAP_BASE, at(6, 4)->owner_site = 1;
    saved_b1 = *b1;
    b1->pop_size = 3, b1->state = BASE_STATE_GOLDEN_AGE, b1->worked_tiles = 0, b1->terraform_x = 5;
    memset(b1->intake, 0, sizeof b1->intake), memset(b1->surplus, 0, sizeof b1->surplus);
    factions[1].ai_tech = factions[1].ai_wealth = factions[1].ai_power = 0;
    humans = 0, goals = 0, connects = 0, terraform_energy = 0;
    base_terraform(0);
    EXPECT(b1->state & 0x7000, 0x2000);
    EXPECT(b1->state & BASE_STATE_GOLDEN_AGE, BASE_STATE_GOLDEN_AGE);
    EXPECT(goals > 0, 1);
    EXPECT(connects, 1);
    EXPECT(b1->terraform_x, 6);  // wet: the square 8 west, here the base's own column
    // A sea shelf square that can't be terraformed ends it there.
    put(7, 3, 2, 0, 0, 0);
    can_terraform_value = 0, goals = 0, connects = 0, b1->terraform_x = 5;
    base_terraform(0);
    EXPECT(connects, 0);
    EXPECT(b1->terraform_x, 5);
    can_terraform_value = 1;
    memcpy(tiles, t_tiles, sizeof tiles);
    *b1 = saved_b1;

    // A base's turn, the first: no drones, research or maintenance yet. Its
    // energy goes into the reserves, and its trade with each faction not
    // under sanctions; the countdowns run, assimilation twice as fast with
    // the base back with its old owner; some state bits are cleared.
    int32_t upkeep_flag = 0, redraw_base = 1;
    game_upkeep_flag = &upkeep_flag, game_base_win_redraw_base = &redraw_base;
    game_draw_tiles = draw_tile_stub, game_console_focus = focus_stub, game_base_win_on_redraw = redraw_stub;
    game_base_win_zoom = zoom_stub, game_log_say = log_say_stub;
    saved_b1 = *b1, saved_f1 = factions[1];
    saved_f3 = factions[3];
    turn = 1, upkeep_state = 0, state = 0, net = 0, prefs = 0;
    b1->assimilation_turns = 3, b1->faction_former = b1->faction, b1->nerve_staple_turns = 2;
    b1->state = 0x20200208;
    factions[3].sanction_turns = 2;
    factions[1].turn_commerce_income = 0, factions[1].sanction_turns = 0;
    int32_t credits = factions[1].energy_credits;
    EXPECT(base_upkeep(1), 0);
    EXPECT(factions[1].energy_credits - credits, b1->economy_total);
    int trade = 0;
    for (int i = 2; i < FACTION_COUNT; i++) trade += i != 3 ? import[i] : 0;  // base_energy() works them out
    EXPECT(factions[1].turn_commerce_income, trade);
    EXPECT(b1->assimilation_turns, 1);
    EXPECT(b1->nerve_staple_turns, 1);
    EXPECT(b1->state & 0x20200208, 0);
    EXPECT(computed, -1);
    EXPECT(upkeep_state, 0);
    EXPECT(redrawn, 1);
    // Under sanctions, no trade. The Progenitors trade with no one but have
    // their energy grid: a facility each, five a secret project, halved.
    factions[1].sanction_turns = 3, factions[1].turn_commerce_income = 0;
    EXPECT(base_upkeep(1), 0);
    EXPECT(factions[1].turn_commerce_income, 0);
    EXPECT(b1->faction_former, b1->faction);
    factions[1].sanction_turns = 0, faction_defs[1].rule_flags |= RULE_FLAG_ALIEN;
    memset(b1->facilities_built, 0, sizeof b1->facilities_built);
    give(1, 3), give(1, 12), projects[SP_VIRTUAL_WORLD] = 1;
    EXPECT(base_upkeep(1), 0);
    EXPECT(factions[1].turn_commerce_income, (2 + 5) / 2);
    faction_defs[1].rule_flags &= ~RULE_FLAG_ALIEN, projects[SP_VIRTUAL_WORLD] = -1;
    // A captured base renamed, a third of the time, by its new owner, who is
    // told; and the base window opened on it when production asked for it.
    strcpy(faction_defs[1].filename, "x"), factions[1].base_name_offset = 0;
    b1->state = 0x10000, rand_value = 3, popps = 0, player = 1, upkeep_flag = 0;
    EXPECT(base_upkeep(1), 0);
    EXPECT(strcmp(b1->name, "Alpha"), 0);
    EXPECT(b1->state & 0x10080, 0x80);
    EXPECT(popps, 1);
    *b1 = saved_b1, factions[1] = saved_f1, factions[3] = saved_f3;

    // A faction's turn: loans paid (all of it while it has enough, the debt
    // growing at war), the AI's discount on maintenance, one energy a base
    // for INTEREST 0, and the first turn's free techs.
    uint8_t alive = 0xff;
    int32_t start_credits = 0, skip_tech = 0;
    game_factions_alive = &alive, game_turn_start_credits = &start_credits, game_skip_tech_screen = &skip_tech;
    game_log_say5 = log_say5_stub, game_do_all_non_input = pump_stub, game_tech_advance = advance_stub;
    saved_b1 = *b1, saved_f1 = factions[1];
    struct faction_data saved_f2b = factions[2];
    saved_f3 = factions[3];
    struct faction_def saved_d1 = faction_defs[1];
    turn = 1, humans = 0, difficulty = 4, pumps = advances = 0;
    factions[1].energy_credits = 100, factions[2].energy_credits = 0, factions[2].sanction_turns = 0;
    factions[1].loan_balance[2] = 30, factions[1].loan_payment[2] = 10;
    factions[1].loan_balance[3] = 30, factions[1].loan_payment[3] = 10, factions[1].diplo_status[3] = TREATY_VENDETTA;
    factions[1].sanction_turns = 0;
    faction_defs[1].rule_flags = RULE_FLAG_INTEREST, faction_defs[1].rule_interest = 0, faction_defs[1].rule_tech_selected = 2;
    int owned = 0;
    for (int i = 0; i < base_count; i++) owned += bases[i].faction == 1;
    production_phase(1);
    EXPECT(start_credits, 90);
    EXPECT(factions[2].energy_credits, 10);
    EXPECT(factions[1].loan_balance[2], 20);
    EXPECT(factions[1].loan_balance[3], 40);
    EXPECT(pumps, owned);
    EXPECT(advances, 2);
    EXPECT(skip_tech, 0);
    EXPECT(factions[1].unk_310, factions[1].energy_surplus_total);
    EXPECT(factions[1].unk_30c, factions[1].turn_commerce_income - factions[1].facility_maint_total +
                                    factions[1].energy_surplus_total);
    // Short of the payment: what it has goes, the rest is added back.
    *b1 = saved_b1, factions[1] = saved_f1, factions[2] = saved_f2b, factions[3] = saved_f3, faction_defs[1] = saved_d1;
    factions[1].energy_credits = 4, factions[2].energy_credits = 0, factions[2].sanction_turns = 0;
    factions[1].loan_balance[2] = 30, factions[1].loan_payment[2] = 10, turn = 2;
    for (int i = 3; i < FACTION_COUNT; i++) factions[1].loan_balance[i] = 0;
    production_phase(1);
    EXPECT(factions[2].energy_credits, 4);
    EXPECT(factions[1].loan_balance[2], 32);
    *b1 = saved_b1, factions[1] = saved_f1, factions[2] = saved_f2b, factions[3] = saved_f3, faction_defs[1] = saved_d1;
    difficulty = 3;

    if (failures) return 1;
    printf("engine_base: all tests passed\n");
    return 0;
}
