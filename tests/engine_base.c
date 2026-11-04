// Unit test for the reimplemented base functions, on a small made-up map:
// no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/base.h"
#include "../src/engine/game.h"

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
static int32_t reserved;
static int farm_calls;
static void farm_compute_stub(void) { farm_calls++; }
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
    vehs[0].next_in_stack = -1;
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
                           .order_auto_type = RESOURCE_NUTRIENT, .next_in_stack = -1};
    vehs[5] = (struct veh){.x = 6, .y = 4, .faction = 1, .home_base = 0, .proto = 2, .order = ORDER_CONVOY,
                           .order_auto_type = RESOURCE_ENERGY, .next_in_stack = -1};
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
    game_current_turn = &turn, game_farm_compute = farm_compute_stub;
    game_radius_x = radius_x, game_radius_y = radius_y;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            if ((x + y) % 2 == 0 && !(at(x, y)->bits & MAP_BASE)) at(x, y)->climate = (uint8_t)(3 << 5 | (x % 3) << 3);
    veh_count = 1;
    memset(b1, 0, sizeof(*b1));
    b1->x = 6, b1->y = 4, b1->faction = 1, b1->pop_size = 3;
    current = b1, current_id = 1;
    base_yield();
    EXPECT(farm_calls, 1);
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
    // A square another base has taken is let go and not chosen.
    int taken = __builtin_ctz(chosen & ~1u);
    reserved = 1 << taken;
    base_yield();
    EXPECT(b1->worked_tiles & (1u << taken), 0);
    EXPECT(__builtin_popcount(b1->worked_tiles) + b1->specialist_total, 4);
    reserved = 0;
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
    // With every other square taken, only the base square is worked.
    reserved = ~1;
    b1->worked_tiles = 1;
    base_yield();
    EXPECT(b1->worked_tiles, 1);
    EXPECT(b1->specialist_total, 3);
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
    reserved = 0;
    give(1, FAC_TREE_FARM);
    give(1, FAC_HYBRID_FOREST);
    base_yield();
    EXPECT(b1->eco_damage, 0);
    memset(b1->facilities_built, 0, sizeof(b1->facilities_built));

    if (failures) return 1;
    printf("engine_base: all tests passed\n");
    return 0;
}
