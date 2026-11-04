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
static int32_t projects[SP_COUNT], governor, dust, flares, upkeep_a, reduce, base_energy;
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
    game_current_base = &current, game_terraform_reduce = &reduce, game_terraform_energy = &base_energy;
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
    EXPECT(base_energy, 5);
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

    if (failures) return 1;
    printf("engine_base: all tests passed\n");
    return 0;
}
