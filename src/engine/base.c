// Bases: what each square of a base's radius yields. Reimplemented from
// terranx.exe 2.0's disassembly. Names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT); Thinker's mod_crop_yield and friends
// were a guide, but they change the game's rules in places (the economy
// bonus on a base square, Manifold Harmonics, monolith energy) and the code
// here follows the original.
//
// Like the originals, these don't check their arguments: a base of -1 still
// has its facilities looked up for some squares, which reads the memory
// before the base table, and the faction indexes the faction tables as given.
#include "base.h"

#include "game.h"
#include "map.h"
#include "shadow.h"
#include "tech.h"

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }

static inline int has_facility(int base, int facility) { return game_has_facility(&game_bases[base], facility); }

// A landmark that marks the square and isn't switched off.
static inline int landmark_on(uint32_t bits2, int id) {
    return (bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(id))) == MAP2_LANDMARK(id);
}

// Which square of its landmark this is; negative when the landmark is off.
static inline int landmark_code(uint32_t bits2) { return (int32_t)bits2 >> 24; }

static inline int alt_of(const struct map_tile *t) { return t->climate >> 5; }

// A base square, as these functions see it: the base bit and a faction.
static inline int is_base_square(const struct map_tile *t) { return (t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8; }

// Manifold Harmonics: true when the faction holds it.
static int has_manifold(int faction) {
    int b = game_secret_projects[SP_MANIFOLD_HARMONICS];
    return b >= 0 && game_bases[b].faction == faction;
}

static inline int planet(int faction) { return game_factions[faction].se_pending[SE_PLANET]; }

// A fungus square: the faction's fungus techs, less for a negative PLANET
// rating (down to -3), kept within 0 to 99.
static int fungus_yield(int faction, int kind) {
    int p = planet(faction);
    p = p < -3 ? -3 : p > 0 ? 0 : p;
    int v = game_factions[faction].tech_fungus[kind] + p;
    return v < 0 ? 0 : v > 99 ? 99 : v;
}

// Squares yield at most 2 of each until the faction has the tech that lifts
// it; what was taken off is added up for the terraforming advice.
static int limit_two(int v, int tech, int faction, int exempt) {
    if (v > 2 && !has_tech(tech, faction) && !exempt) {
        *game_terraform_reduce += v - 2;
        v = 2;
    }
    return v;
}

static int base_events(int v, int base, uint32_t more, uint32_t less) {
    if (base >= 0) {
        uint32_t event = game_bases[base].event;
        if (event & more) v++;
        if ((event & less) && v) v--;
    }
    return v;
}

int crop_yield(int faction, int base, int x, int y, int flag) {
    const struct map_tile *t = tile(x, y);
    const struct res_info *r = game_res_info;
    uint32_t bits = t->bits, bits2 = t->bits2;
    int alt = alt_of(t), ocean = alt < ALT_SHORE_LINE;
    int bonus = bonus_at(x, y, 0) == 1;
    // The Fresh Water Sea counts even when switched off.
    int landmark = (landmark_on(bits2, LM_JUNGLE) && !ocean) || ((bits2 & MAP2_LANDMARK(LM_FRESH)) && ocean);
    int v;
    if (is_base_square(t)) {
        v = r->base.nutrient;
        if (bonus) v *= 2;
        if (landmark) v++;
        if (base < 0) return v;
        if (has_facility(base, FAC_RECYCLING_TANKS) || has_facility(base, FAC_PRESSURE_DOME)) v += r->recycling.nutrient;
        return base_events(v, base, BASE_EVENT_BUMPER, BASE_EVENT_FAMINE);
    }
    if (bits & MAP_THERMAL_BORE) {
        // The bonus is added whenever a borehole yields any nutrients at
        // all, resource or not.
        v = r->borehole.nutrient;
        if (v) v += r->bonus.nutrient;
        return base_events(v, base, BASE_EVENT_BUMPER, BASE_EVENT_FAMINE);
    }
    if (bits & MAP_MONOLITH) {
        v = r->monolith.nutrient + (bonus ? r->bonus.nutrient : 0);
        if (has_manifold(faction) && planet(faction) > 0) v++;
        return base_events(v, base, BASE_EVENT_BUMPER, BASE_EVENT_FAMINE);
    }
    if ((bits & MAP_FUNGUS) && !flag && alt >= ALT_SHORE_LINE - 1) {
        v = fungus_yield(faction, FUNGUS_NUTRIENT);
        if (has_manifold(faction) && planet(faction) > 0) v++;
        return base_events(v, base, BASE_EVENT_BUMPER, BASE_EVENT_FAMINE);
    }
    if (ocean) {
        v = r->ocean.nutrient + (bonus ? r->bonus.nutrient : 0) + landmark;
        // Deep water yields more only in Alien Crossfire.
        if (alt == ALT_SHORE_LINE - 1 || *game_expansion) {
            if (bits & MAP_FARM) {
                v += r->improved_sea.nutrient;
                if (has_facility(base, FAC_AQUAFARM)) v++;
            }
            if ((bits & MAP_MINE) && v > 1) v += game_rules_basic->nutrient_effect_mine;
        }
    } else if (bits & MAP_FOREST) {
        v = r->forest.nutrient + (bonus ? r->bonus.nutrient : 0);
        if (base >= 0) v += has_facility(base, FAC_HYBRID_FOREST) + has_facility(base, FAC_TREE_FARM);
        v += landmark;
    } else {
        int rocky = t->rocky_lock >> 6;
        v = rocky > 1 ? 0 : t->climate >> 3 & 3;
        v += (bonus ? r->bonus.nutrient : 0) + landmark;
        if (v < 0) v = 0;
        if ((bits & MAP_FARM) && rocky <= 1) v += r->improved_land.nutrient;
        if ((bits & MAP_MINE) && v > 1) v += game_rules_basic->nutrient_effect_mine;
    }
    if (bits & MAP_SOIL_ENRICHER) v += v / 2;
    if (bits & MAP_CONDENSER) v += v / 2;
    v = limit_two(v, game_rules_basic->tech_three_nutrients, faction, bonus || (bits & MAP_CONDENSER));
    return base_events(v, base, BASE_EVENT_BUMPER, BASE_EVENT_FAMINE);
}

int mine_yield(int faction, int base, int x, int y, int flag) {
    const struct map_tile *t = tile(x, y);
    const struct res_info *r = game_res_info;
    uint32_t bits = t->bits, bits2 = t->bits2;
    int code = landmark_code(bits2), alt = alt_of(t);
    int bonus = bonus_at(x, y, 0) == 2;
    int landmark = (landmark_on(bits2, LM_VOLCANO) && code < 9) || (landmark_on(bits2, LM_CRATER) && code < 9) ||
                   (landmark_on(bits2, LM_FOSSIL) && code < 6) || landmark_on(bits2, LM_CANYON);
    int v = (bonus ? r->bonus.mineral : 0) + landmark;
    if (is_base_square(t)) {
        v += r->base.mineral;
        if (base < 0) return v;
        if (has_facility(base, FAC_RECYCLING_TANKS) || has_facility(base, FAC_PRESSURE_DOME)) v += r->recycling.mineral;
        return base_events(v, base, BASE_EVENT_INDUSTRY, BASE_EVENT_BUST);
    }
    if (bits & MAP_MONOLITH) {
        v = r->monolith.mineral + (bonus ? r->bonus.mineral : 0);
        if (has_manifold(faction) && planet(faction) > 1) v++;
        return base_events(v, base, BASE_EVENT_INDUSTRY, BASE_EVENT_BUST);
    }
    if (bits & MAP_THERMAL_BORE) {
        v = r->borehole.mineral + (bonus ? r->bonus.mineral : 0);
    } else if ((bits & MAP_FUNGUS) && alt >= ALT_SHORE_LINE - 1) {
        v = fungus_yield(faction, FUNGUS_MINERALS);
        if (has_manifold(faction) && planet(faction) > 1) v++;
        return base_events(v, base, BASE_EVENT_INDUSTRY, BASE_EVENT_BUST);
    } else if (alt < ALT_SHORE_LINE) {
        v += r->ocean.mineral;
        // Aquatic factions get +1 on the shelf; the faction asked about is
        // the base's, not the one passed in.
        if ((game_faction_defs[game_bases[base].faction].rule_flags & RULE_FLAG_AQUATIC) && alt == ALT_SHORE_LINE - 1)
            v++;
        if (alt == ALT_SHORE_LINE - 1 || *game_expansion) {
            if ((bits & MAP_MINE) || flag) {
                v += r->improved_sea.mineral;
                v += has_tech(game_rules_basic->tech_mining_platform_bonus, faction) != 0;
                v += has_facility(base, FAC_SUBSEA_TRUNKLINE);
            }
        }
    } else {
        int rocky = t->rocky_lock >> 6;
        if (bits & MAP_FOREST) {
            v += r->forest.mineral;
        } else if (!(bits & (MAP_MINE | MAP_THERMAL_BORE)) && !flag) {
            v += rocky != 0;
        } else {
            // A mine: rockiness, then up to the road limit more.
            v += rocky;
            int mine = rocky ? rocky : 1;
            if (bonus || landmark) mine++;
            int limit = game_rules_basic->mine_road_limit;
            if (!(bits & MAP_ROAD) && !flag && mine > limit) {
                *game_terraform_reduce += mine - limit;
                mine = limit;
            }
            v += mine;
        }
    }
    v = limit_two(v, game_rules_basic->tech_three_minerals, faction, bonus);
    return base_events(v, base, BASE_EVENT_INDUSTRY, BASE_EVENT_BUST);
}

// Solar collectors gain 1 for each echelon mirror next to them in the
// territory of the base being worked on.
static int mirrors_near(int x, int y) {
    int n = 0, w = *game_map_width, h = *game_map_height;
    for (int i = 0; i < 8; i++) {
        int x2 = x + game_radius_near_x[i], y2 = y + game_radius_near_y[i];
        if (!(*game_map_flat & 1)) {
            if (x2 < 0) x2 += w;
            else if (x2 >= w) x2 -= w;
        }
        if (y2 < 0 || y2 >= h || x2 < 0 || x2 >= w) continue;
        const struct map_tile *t = tile(x2, y2);
        if ((t->bits & MAP_ECHELON_MIRROR) && t->territory == (*game_current_base)->faction) n++;
    }
    return n;
}

int energy_yield(int faction, int base, int x, int y, int flag) {
    const struct map_tile *t = tile(x, y);
    const struct res_info *r = game_res_info;
    const struct faction_data *f = &game_factions[faction];
    uint32_t bits = t->bits, bits2 = t->bits2;
    int code = landmark_code(bits2), alt = alt_of(t), ocean = alt < ALT_SHORE_LINE, is_base = is_base_square(t);
    int bonus = bonus_at(x, y, 0) == 3;
    int landmark = (landmark_on(bits2, LM_VOLCANO) && code < 9) || landmark_on(bits2, LM_URANIUM) ||
                   landmark_on(bits2, LM_GEOTHERMAL) || landmark_on(bits2, LM_RIDGE);
    int v = 0;
    if (is_base) {
        // Headquarters is looked up even without a base.
        int hq = has_facility(base, FAC_HEADQUARTERS), golden = 0;
        v = r->base.energy + hq;
        if (base >= 0) {
            if (has_facility(base, FAC_RECYCLING_TANKS) || has_facility(base, FAC_PRESSURE_DOME)) v += r->recycling.energy;
            golden = (game_bases[base].state & BASE_STATE_GOLDEN_AGE) != 0;
        }
        // ECONOMY: 3 gives +2 and 4 or more +4 here; below 0 costs as much,
        // less one unless only headquarters at -1 with other bases.
        int e = f->se_pending[SE_ECONOMY] + golden;
        if (e < 0) {
            if (!hq || e < -1 || f->base_count == 1) e++;
        } else if (e > 1) {
            e = 2 * (e > 4 ? 4 : e) - 4;
        }
        v += e;
        if (*game_governor_faction == faction) v++;
    } else if (bits & MAP_MONOLITH) {
        v = r->monolith.energy;
        if (has_manifold(faction)) v += (planet(faction) >= 0) + (planet(faction) >= 3);
    } else if (bits & MAP_THERMAL_BORE) {
        v = r->borehole.energy;
    } else if ((bits & MAP_FUNGUS) && alt >= ALT_SHORE_LINE - 1) {
        // Fungus gets none of the bonuses below, nor the limit.
        v = fungus_yield(faction, FUNGUS_ENERGY);
        if (has_manifold(faction)) v += (planet(faction) >= 0) + (planet(faction) >= 3);
        v = base_events(v, base, BASE_EVENT_HEAT_WAVE, BASE_EVENT_CLOUD_COVER);
        goto world;
    } else if (ocean) {
        if (alt == ALT_SHORE_LINE - 1 || *game_expansion) {
            v = r->ocean.energy;
            if ((bits & MAP_SOLAR) || flag) {
                v += r->improved_sea.energy;
                v += has_facility(base, FAC_THERMOCLINE_TRANSDUCER);
            }
        }
        if (bits & MAP_FOREST) v--;
    } else if (bits & MAP_FOREST) {
        v = r->forest.energy;
        if (base >= 0) v += has_facility(base, FAC_HYBRID_FOREST);
    } else {
        // Solar collectors and mirrors yield more the higher they stand.
        if ((bits & (MAP_SOLAR | MAP_ECHELON_MIRROR | MAP_THERMAL_BORE)) || flag) v = alt - 2;
        if (bits & MAP_SOLAR) v += mirrors_near(x, y);
    }
    if ((bits & MAP_RIVER) && !ocean) v++;
    if (bonus) v += r->bonus.energy;
    v += landmark;
    int golden = 0;
    if (base >= 0) {
        if (game_secret_projects[SP_MERCHANT_EXCHANGE] == base) v++;
        golden = (game_bases[base].state & BASE_STATE_GOLDEN_AGE) != 0;
    }
    if (f->se_pending[SE_ECONOMY] + golden >= 2) v++;
    if (is_base) {
        v = v < 0 ? 0 : v > 99 ? 99 : v;
        *game_terraform_energy = v;
    } else {
        v = limit_two(v, game_rules_basic->tech_three_energy, faction, bonus);
    }
    v = base_events(v, base, BASE_EVENT_HEAT_WAVE, BASE_EVENT_CLOUD_COVER);
world:
    if (*game_dust_cloud && v) v--;
    if (*game_solar_flares & (*game_control_upkeep_a ? 2 : 1)) v *= 3;
    return v;
}

int resource_yield(int type, int faction, int base, int x, int y) {
    switch ((unsigned)type) {
    case RESOURCE_NUTRIENT: return crop_yield(faction, base, x, y, 0);
    case RESOURCE_MINERAL: return mine_yield(faction, base, x, y, 0);
    case RESOURCE_ENERGY: return energy_yield(faction, base, x, y, 0);
    case RESOURCE_UNUSED: *game_terraform_reduce = 0; return 0;
    default: return 0;
    }
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module base_module;
#define BASE (&base_module)

#define WRITES_YIELD WRITES(game_terraform_reduce, 4), WRITES(game_terraform_energy, 4)
#define YIELD(name)                                                                                              \
    SHADOW_WRITER(BASE, int, name, (int a, int b, int c, int d, int e), (a, b, c, d, e), WRITES_YIELD, \
                  "%d, %d, %d, %d, %d", a, b, c, d, e)
YIELD(crop_yield)
YIELD(mine_yield)
YIELD(energy_yield)
YIELD(resource_yield)

static int slice;

// Every square for every faction with no base, the flag both ways; and
// each base's own radius as the base. The originals reach a NULL current
// base for solar collectors, so those wait for one.
static void sweep_yields(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int y = 0; y < h; y++)
        for (int x = y & 1; x < w; x += 2) {
            int solar = (tile(x, y)->bits & MAP_SOLAR) && !*game_current_base;
            for (int f = 0; f < FACTION_COUNT; f++) {
                int flag = (x + y + f + slice) & 1;
                check_crop_yield(f, -1, x, y, flag);
                check_mine_yield(f, -1, x, y, flag);
                if (!solar) check_energy_yield(f, -1, x, y, flag);
            }
        }
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *base = &game_bases[b];
        for (int i = 0; i < 21; i++) {
            int x = base->x + game_radius_x[i], y = base->y + game_radius_y[i];
            if (!(*game_map_flat & 1)) x = x < 0 ? x + w : x >= w ? x - w : x;
            if (y < 0 || y >= h || x < 0 || x >= w) continue;
            int solar = (tile(x, y)->bits & MAP_SOLAR) && !*game_current_base;
            for (int type = 0; type <= RESOURCE_COUNT; type++)
                if (type != RESOURCE_ENERGY || !solar) check_resource_yield(type, base->faction, b, x, y);
            check_crop_yield(base->faction, b, x, y, 1);
        }
    }
}

static void base_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    sweep_yields();
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook base_hooks[] = {
    HOOK(0x4e6e50, crop_yield, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                    // sub esp, 8
    HOOK(0x4e7310, mine_yield, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xf0, 0xfa, 0x68, 0x00),        // mov eax, [0x68faf0]
    HOOK(0x4e7750, energy_yield, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),                  // sub esp, 24
    HOOK(0x4e7dc0, resource_yield, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
};
struct hook_module base_module = {"base", base_hooks, HOOK_COUNT(base_hooks), base_sweep, NULL, -1};
