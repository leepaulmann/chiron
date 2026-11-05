// Bases: what each square of a base's radius yields, and the totals
// base_compute() works out from it. Reimplemented from terranx.exe 2.0's
// disassembly. Names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT); Thinker's mod_crop_yield and friends
// were a guide, but they change the game's rules in places (the economy
// bonus on a base square, Manifold Harmonics, monolith energy) and the code
// here follows the original.
//
// Like the originals, these don't check their arguments: a base of -1 still
// has its facilities looked up for some squares, which reads the memory
// before the base table, and the faction indexes the faction tables as given.
#include "base.h"

#include <stddef.h>
#include <string.h>

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

// v kept within 0 and hi, and 0 when hi is negative.
static int clamp_to(int v, int hi) { return v < 0 || hi < 0 ? 0 : v > hi ? hi : v; }

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

// ---- The current base's totals ------------------------------------------------
//
// base_compute() works these out for *game_current_base in turn, after
// base_support() and base_yield() have filled in its intake.

// Whether the base has the facility, or (with `queue` > 0) has it among the
// first `queue` items it is building. Secret projects never count.
int has_fac(int facility, int base, int queue) {
    if (facility >= FAC_SKY_HYDRO_LAB) return 0;
    if (has_facility(base, facility)) return 1;
    for (int i = 0; i < queue; i++)
        if (game_bases[base].queue[i] == -facility) return 1;
    return 0;
}

static inline int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}

// A specialist's kind: from its slot for the first 16, chosen afresh past
// them.
static int specialist_kind(const struct base *b, int i) {
    return i < 16 ? (int)(b->specialist_types[i / 8] >> (i % 8 * 4) & 0xf) : game_best_specialist();
}

static void set_specialist(struct base *b, int i, int kind) {
    if (i >= 16) return;
    int shift = i % 8 * 4;
    b->specialist_types[i / 8] = (b->specialist_types[i / 8] & ~(0xfu << shift)) | (uint32_t)kind << shift;
}

// The square of the base's radius table entry `i`, if it is on the map.
static int radius_square(const struct base *b, int i, int *x, int *y) {
    int w = *game_map_width;
    *x = b->x + game_radius_x[i];
    if (!(*game_map_flat & 1)) *x = *x < 0 ? *x + w : *x >= w ? *x - w : *x;
    *y = b->y + game_radius_y[i];
    return *y >= 0 && *y < *game_map_height && *x >= 0 && *x < w;
}

// Adds (or, with sign -1, takes away) what the square yields to the
// base's intake, and notes an ocean square.
static void work_square(struct base *b, int i, int sign) {
    int x, y;
    if (!radius_square(b, i, &x, &y)) return;
    if (alt_of(tile(x, y)) < ALT_SHORE_LINE) b->state |= BASE_STATE_WORKS_OCEAN;
    for (int type = 0; type < RESOURCE_COUNT; type++)
        b->intake_2[type] += sign * resource_yield(type, b->faction, *game_current_base_id, x, y);
}

static int popcount(uint32_t v) { return __builtin_popcount(v); }

// Finds the squares of the current base's radius it may not work, with
// the reasons in game_base_square_flags and the squares in
// game_base_reserved. At the end of a turn, supply pods on land become
// goals for the AI.
void farm_compute(void) {
    const struct base *b = *game_current_base;
    int faction = b->faction;
    const struct faction_data *f = &game_factions[faction];
    int32_t *flags = game_base_square_flags;
    for (int i = 0; i < 25; i++) {
        int x, y;
        if (i >= BASE_RADIUS_SQUARES) {
            flags[i] = SQUARE_OUTSIDE;
            continue;
        }
        if (!radius_square(b, i, &x, &y)) {
            flags[i] = SQUARE_UNSEEN;
            continue;
        }
        const struct map_tile *t = tile(x, y);
        if (*game_base_upkeep_state == 1 && goody_at(x, y) && alt_of(t) >= ALT_SHORE_LINE) {
            game_add_goal(faction, 3, 1, x, y, -1);
            game_add_goal(faction, 2, 1, x, y, -1);
            game_add_goal(faction, 0, 1, x, y, -1);
        }
        if (!(f->flags & FACTION_FLAG_MAP_REVEALED) && !(t->visibility & (1u << faction))) {
            flags[i] = SQUARE_UNSEEN;
            continue;
        }
        flags[i] = is_base_square(t) ? SQUARE_BASE : 0;
        // Units: by stack_check()'s test, or any the faction can see of a
        // faction it has neither pact nor treaty with.
        int v = game_stack_fix(game_veh_at(x, y));
        if (v >= 0) {
            if (game_stack_check(v, 9, 3, -1, -1)) flags[i] |= SQUARE_HOSTILE;
            while (game_vehs[v].prev_in_stack >= 0) v = game_vehs[v].prev_in_stack;
            for (; v >= 0; v = game_vehs[v].next_in_stack) {
                int owner = game_vehs[v].faction;
                if (owner == faction || !(game_vehs[v].visibility & (1u << faction))) continue;
                if (!(f->diplo_status[owner] & (TREATY_PACT | TREATY_TREATY))) flags[i] |= SQUARE_HOSTILE;
            }
        }
        // The original also looks at the faction's pact or treaty with the
        // owner, but both ways come out the same.
        int owner = whose_territory(faction, x, y, NULL, 0);
        if (owner >= 0 && owner != faction) flags[i] |= SQUARE_FOREIGN;
    }
    // Squares within 2 of the base that a base within 5 works: any of its
    // own faction's, only ocean squares of another's.
    for (int j = 0; j < *game_base_count; j++) {
        const struct base *o = &game_bases[j];
        if (j == *game_current_base_id || vector_dist(o->x, o->y, b->x, b->y) > 5) continue;
        for (int k = 0; k < BASE_RADIUS_SQUARES; k++) {
            int x, y;
            if ((k && !(o->worked_tiles & (1u << k))) || !radius_square(o, k, &x, &y)) continue;
            if (o->faction != faction && alt_of(tile(x, y)) >= ALT_SHORE_LINE) continue;
            if (vector_dist(b->x, b->y, x, y) > 2) continue;
            for (int m = 0; m < BASE_RADIUS_SQUARES; m++) {
                int mx = b->x + game_radius_x[m], w = *game_map_width;
                if (!(*game_map_flat & 1)) mx = mx < 0 ? mx + w : mx >= w ? mx - w : mx;
                if (mx == x && b->y + game_radius_y[m] == y) flags[m] |= SQUARE_NEIGHBOUR;
            }
        }
    }
    uint32_t reserved = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++)
        if (flags[i]) reserved |= 1u << i;
    *game_base_reserved = (int32_t)reserved;
}

// How the square choice weighs each resource: the AI's priorities, a human
// governor's, or none.
struct priorities {
    int growth, tech, wealth, power, fight;
};

static struct priorities base_priorities(const struct base *b) {
    const struct faction_data *f = &game_factions[b->faction];
    struct priorities p = {0};
    if (!game_is_human(b->faction)) {
        p = (struct priorities){f->ai_growth, f->ai_tech, f->ai_wealth, f->ai_power, f->ai_fight};
    } else if (b->governor & GOVERNOR_ACTIVE) {
        uint32_t g = b->governor;
        p.growth = (g & GOVERNOR_EXPLORE) != 0;
        p.tech = g & GOVERNOR_DISCOVER ? 2 : 0;
        p.wealth = g & GOVERNOR_BUILD ? 2 : 0;
        p.power = g & GOVERNOR_CONQUER ? 2 : 0;
        p.fight = p.power ? 2 : p.growth || p.wealth ? 0 : p.tech ? -2 : f->ai_fight;
    }
    return p;
}

// What working square `i` is worth, for the citizens left to place. Food
// counts for more while the base is short of it; minerals and energy for
// more while the base makes little of them.
static int square_score(const struct base *b, const struct priorities *p, int x, int y, int *most_food) {
    int faction = b->faction, id = *game_current_base_id, pop = b->pop_size;
    int n = crop_yield(faction, id, x, y, 0), m = mine_yield(faction, id, x, y, 0), e = energy_yield(faction, id, x, y, 0);
    if (n > *most_food) *most_food = n;
    int workers = *game_base_workers, eat = game_rules_basic->nutrient_intake_citizen, need = pop * eat;
    int hunger = 0;
    if (b->intake_2[RESOURCE_NUTRIENT] + workers / eat < need) {
        hunger = b->intake_2[RESOURCE_NUTRIENT] + workers >= need ? 2 : 8;
        n += clamp_to(n, hunger);
    } else {
        *game_base_food_ok = 1;
    }
    n *= 2, m *= 2, e *= 2;
    // A square with no food is worth less until the base has plenty.
    if (b->intake_2[RESOURCE_NUTRIENT] <= pop + need && !n) m /= 2, e /= 2;
    if (!m && !hunger) n /= 2;
    int food = game_range(2 * (b->intake_2[RESOURCE_NUTRIENT] - pop / 3 - need + workers) - 2, 1, 99);
    int score = (p->growth + 4) * n * 256 / food;
    // Building a secret project (past the first) counts minerals for more.
    int project = b->queue[0] < -FAC_FIRST_PROJECT ? 2 : -p->tech;
    int minerals = game_range((b->intake_2[RESOURCE_MINERAL] - *game_base_forces_upkeep + 1) / 2, 1, 99);
    score += (project + p->fight + p->power + p->wealth + 6) * pop * m * 64 / minerals;
    int energy = game_range(b->intake_2[RESOURCE_ENERGY], 1, 99);
    score += (p->wealth + 2 * p->tech + 1) * game_range(pop - p->power, 1, 99) * e * 64 / energy;
    return score;
}

// Places the current base's citizens: keeps the squares it worked if they
// still fit, then adds the best square (or, for the AI and governed bases
// past turn 100, a specialist) one at a time; when every citizen is placed
// and food is short, tries one swap of the worst worked square for the
// best free one. The rest become specialists. Then the intake, and the
// damage the terraforming in the radius does.
void base_yield(void) {
    farm_compute();
    struct base *b = *game_current_base;
    int faction = b->faction, id = *game_current_base_id;
    const struct faction_data *f = &game_factions[faction];
    int eat = game_rules_basic->nutrient_intake_citizen;
    *game_base_workers = b->pop_size + 1;  // with the base square
    int automatic = !game_is_human(faction) || ((b->governor & GOVERNOR_CITIZENS) && (b->governor & GOVERNOR_ACTIVE));
    b->state &= ~(uint32_t)BASE_STATE_WORKS_OCEAN;
    uint32_t worked = 0;
    b->specialist_adjust = 0;
    b->worked_tiles &= ~(uint32_t)*game_base_reserved | 1;
    // Satellites, halved without an Aerospace Complex or the projects that
    // stand for one, up to one a citizen.
    for (int r = 0; r < RESOURCE_UNUSED; r++) {
        int v = f->satellites[r];
        if (!has_facility(id, FAC_AEROSPACE_COMPLEX) && !holds_project(SP_SPACE_ELEVATOR, faction) &&
            !holds_project(SP_CLOUDBASE_ACADEMY, faction))
            v = (v + 1) / 2;
        b->intake_2[r] = v > b->pop_size ? b->pop_size : v;
    }
    b->intake_2[RESOURCE_UNUSED] = 0;

    int keep = 1;
    if (automatic) {
        if (*game_control_upkeep_a) {
            keep = 0;
        } else {
            // Specialists that give no psych may be put to work.
            for (int i = 0; i < b->specialist_total; i++)
                if (!game_citizens[specialist_kind(b, i)].psych) b->specialist_adjust++;
            b->specialist_total -= b->specialist_adjust;
        }
    }
    if (keep && *game_base_workers >= popcount(b->worked_tiles)) {
        for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
            if (i && !(b->worked_tiles & (1u << i))) continue;
            worked |= 1u << i;
            work_square(b, i, 1);
            if (!--*game_base_workers) break;
        }
        if (*game_base_workers == b->specialist_adjust + b->specialist_total) goto done;
    }
    // Drop the specialists counted above from their slots. After dropping
    // one the original moves on, skipping the one that took its place.
    if (automatic && b->specialist_total)
        for (int i = 0; i < b->specialist_adjust + b->specialist_total; i++) {
            if (game_citizens[specialist_kind(b, i)].psych) continue;
            for (int j = i + 1; j <= b->specialist_adjust + b->specialist_total; j++)
                set_specialist(b, j - 1, specialist_kind(b, j));
            if (b->specialist_adjust) b->specialist_adjust--;
        }
    if (!(worked & 1)) {
        work_square(b, 0, 1);
        worked |= 1;
        --*game_base_workers;
    }

    struct priorities p = base_priorities(b);
    *game_base_food_ok = 0;
    for (;;) {
        int full = *game_base_workers <= b->specialist_adjust + b->specialist_total;
        uint32_t taken = (full ? 0 : worked) | (uint32_t)*game_base_reserved;
        int best = 0, best_score = 0, most_food = 0, worst = -1, worst_score = 9999;
        for (int i = 1; i < BASE_RADIUS_SQUARES; i++) {
            int x, y;
            if ((taken & (1u << i)) || !radius_square(b, i, &x, &y)) continue;
            int score = square_score(b, &p, x, y, &most_food);
            if (worked & (1u << i)) {
                if (score < worst_score) worst_score = score, worst = i;
            } else if (score > best_score) {
                best_score = score, best = i;
            }
        }
        if (automatic && b->pop_size >= 5 &&
            (b->intake_2[RESOURCE_NUTRIENT] > b->pop_size * eat || !most_food) && *game_current_turn > 100)
            for (int k = 0; k < CITIZEN_KINDS; k++) {
                const struct citizen *c = &game_citizens[k];
                if (!has_tech(c->preq_tech, faction) || has_tech(c->obsol_tech, faction)) continue;
                int energy = game_range(b->intake_2[RESOURCE_ENERGY] + b->specialist_adjust, 1, 99);
                int score = c->psych * 64 + (p.wealth + 2) * b->pop_size * c->econ * 64 / energy +
                            (p.tech + 2) * b->pop_size * c->labs * 64 / energy;
                if (score > best_score) best_score = score, best = -(k + 1);
            }
        if (full) {
            if (best <= 0 || best_score <= worst_score || worst <= 0) break;
            worked = (worked & ~(1u << worst)) | (1u << best);
            work_square(b, worst, -1);
            work_square(b, best, 1);
            if (b->intake_2[RESOURCE_NUTRIENT] < (b->pop_size + 1) * eat) {
                worked = (worked & ~(1u << best)) | (1u << worst);
                work_square(b, best, -1);
                work_square(b, worst, 1);
            }
            break;
        }
        if (best < 0) {
            if (b->specialist_adjust < 16) set_specialist(b, b->specialist_adjust, -best - 1);
            b->specialist_adjust++;
        } else if (!best) {
            break;
        } else {
            worked |= 1u << best;
            work_square(b, best, 1);
            --*game_base_workers;
        }
        if (*game_base_workers <= b->specialist_adjust + b->specialist_total &&
            b->intake_2[RESOURCE_NUTRIENT] >= b->pop_size * eat)
            break;
    }

done:
    // Citizens left over become the specialist with the most psych, which
    // counts three times, and in bases big enough economy and labs.
    for (int i = b->specialist_total; i < *game_base_workers; i++) {
        int kind = 0, most = -999;
        for (int k = 0; k < CITIZEN_KINDS; k++) {
            const struct citizen *c = &game_citizens[k];
            if (!has_tech(c->preq_tech, b->faction)) continue;
            int v = 3 * c->psych;
            if (b->pop_size >= game_rules_basic->min_base_size_specialists) v += c->econ + c->labs;
            if (v > most) most = v, kind = k;
        }
        set_specialist(b, i, kind);
    }
    b->worked_tiles = worked;
    b->specialist_total = *game_base_workers;
    b->state &= ~(uint32_t)BASE_STATE_UNK_100;
    for (int r = 0; r < RESOURCE_COUNT; r++) b->intake[r] = b->intake_2[r];
    // Terraforming damage: improvements count once, twice on a worked
    // square, with more for boreholes, mirrors and condensers and one less
    // for forest; tree farms halve it, both together clear it.
    b->eco_damage = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x, y;
        if (!radius_square(b, i, &x, &y)) continue;
        uint32_t bits = tile(x, y)->bits;
        int d = popcount(bits & 0x1c8805c);
        if (b->worked_tiles & (1u << i)) d *= 2;
        if (bits & MAP_THERMAL_BORE) d += 8;
        if (bits & MAP_ECHELON_MIRROR) d += 6;
        if (bits & MAP_CONDENSER) d += 4;
        if (bits & MAP_FOREST) d--;
        int tree = has_facility(id, FAC_TREE_FARM), hybrid = has_facility(id, FAC_HYBRID_FOREST);
        if (tree && hybrid) d = 0;
        if (tree || hybrid) d /= 2;
        b->eco_damage += d;
    }
}

// The current base's units: what its supply convoys bring in or carry out,
// the minerals its units cost beyond those SUPPORT keeps free, and its
// armed units away from home, which make drones under a low POLICE.
void base_support(void) {
    struct base *b = *game_current_base;
    int faction = b->faction, id = *game_current_base_id;
    struct faction_data *f = &game_factions[faction];
    for (int i = 0; i < RESOURCE_COUNT; i++) game_base_convoy_to[i] = game_base_convoy_from[i] = 0;
    *game_base_pacifism = *game_base_forces_paid = *game_base_forces_supported = *game_base_forces_upkeep = 0;
    // Units supported free at each SUPPORT from -4 to 3.
    int free_units[8];
    for (int s = -4; s <= 3; s++)
        free_units[s + 4] = s >= 3 ? (b->pop_size > 4 ? b->pop_size : 4) : s < -2 ? 0 : s == -2 ? 1 : s + 2;
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *veh = &game_vehs[v];
        if (veh->faction != faction) continue;
        int proto = veh->proto;
        const struct unit_proto *u = &game_protos[proto];
        const struct map_tile *t = tile(veh->x, veh->y);
        if (veh->home_base != id) {
            // Another base's convoy, standing here, brings in 1.
            if (u->plan == PLAN_SUPPLY && veh->x == b->x && veh->y == b->y && veh->order == ORDER_CONVOY &&
                veh->home_base >= 0)
                game_base_convoy_to[veh->order_auto_type]++;
            continue;
        }
        // This base's convoys bring in what their square yields, or, at
        // a base, carry 1 away.
        if (u->plan == PLAN_SUPPLY && !is_base_square(t) && veh->order == ORDER_CONVOY) {
            int type = veh->order_auto_type, value = 0;
            if (type == RESOURCE_NUTRIENT) value = crop_yield(faction, id, veh->x, veh->y, 0);
            else if (type == RESOURCE_MINERAL) value = mine_yield(faction, id, veh->x, veh->y, 0);
            else if (type == RESOURCE_ENERGY) value = energy_yield(faction, id, veh->x, veh->y, 0);
            else if (type == RESOURCE_UNUSED) *game_terraform_reduce = 0;
            if (type <= RESOURCE_UNUSED) game_base_convoy_to[type] += value;
        }
        int offense = game_weapons[u->weapon].offense;
        if (offense && (offense > 0 || proto >= 64) && u->plan != PLAN_RECON && u->plan != PLAN_PLANET_BUSTER)
            f->armed_units += u->plan == PLAN_OFFENSIVE || u->plan == PLAN_COMBAT ? 2 : 1;
        veh->state &= ~(uint32_t)(VEH_STATE_SUPPORTED | VEH_STATE_PACIFISM_DRONE | VEH_STATE_PACIFISM_FREE);
        if (u->plan == PLAN_SUPPLY && veh->order == ORDER_CONVOY && is_base_square(t))
            game_base_convoy_from[veh->order_auto_type]++;
        if (u->plan >= PLAN_SUPPLY) continue;  // convoys and the plans after them cost nothing

        // Clean reactors cost nothing; nor do native psi units (and id 15)
        // on fungus above the deep ocean, though the AI's tally still sees
        // them.
        if (!game_has_abil(proto, ABILITY_CLEAN_REACTOR)) {
            int native = proto < 64 && (offense < 0 || proto == 15) && (t->bits & MAP_FUNGUS) && alt_of(t) >= 2;
            int support = f->se_pending[SE_SUPPORT];
            if (!native && ++*game_base_forces_supported > free_units[game_range(support + 4, 0, 7)]) {
                veh->state |= VEH_STATE_SUPPORTED;
                ++*game_base_forces_paid;
                *game_base_forces_upkeep += support <= -4 ? 2 : 1;
            }
            // What the units would cost at each SUPPORT, weighed against
            // the base's minerals.
            if (*game_base_upkeep_state == 1)
                for (int s = -4; s <= 3; s++)
                    if (*game_base_forces_supported > free_units[s + 4]) {
                        int cost = s <= -4 ? 2 : 1, half = b->intake_2[RESOURCE_MINERAL] / 2;
                        if (cost >= b->intake_2[RESOURCE_MINERAL]) cost += 2 * (cost - half);
                        else if (cost > half) cost = 2 * cost - half;
                        f->social_support[s + 4] += cost;
                    }
        }
        // Away: outside the faction's territory, or an aircraft that isn't
        // an interceptor.
        if (offense && ((!is_base_square(t) && whose_territory(faction, veh->x, veh->y, NULL, 0) != faction) ||
                        (game_chassis[u->chassis].triad == TRIAD_AIR && u->plan != PLAN_AIR_SUPERIORITY))) {
            ++*game_base_pacifism;
            int police = f->se_pending[SE_POLICE] + 2 * has_facility(id, FAC_BROOD_PIT);
            if (police == -3) veh->state |= *game_base_pacifism == 1 ? VEH_STATE_PACIFISM_FREE : VEH_STATE_PACIFISM_DRONE;
            else if (police < -3) veh->state |= VEH_STATE_PACIFISM_DRONE;
        }
    }
}

void base_nutrient(void) {
    struct base *b = *game_current_base;
    int faction = b->faction;
    struct faction_data *f = &game_factions[faction];
    *game_base_growth = f->se_pending[SE_GROWTH];
    if (has_facility(*game_current_base_id, FAC_CHILDREN_CRECHE)) *game_base_growth += 2;
    if (b->state & BASE_STATE_GOLDEN_AGE) *game_base_growth += 2;
    b->intake_2[RESOURCE_NUTRIENT] += game_base_convoy_to[RESOURCE_NUTRIENT];
    b->consumption[RESOURCE_NUTRIENT] =
        b->pop_size * game_rules_basic->nutrient_intake_citizen + game_base_convoy_from[RESOURCE_NUTRIENT];
    b->surplus[RESOURCE_NUTRIENT] = b->intake_2[RESOURCE_NUTRIENT] - b->consumption[RESOURCE_NUTRIENT];
    // A base going hungry with nothing stored shows -1.
    if (b->surplus[RESOURCE_NUTRIENT] >= 0) {
        if (b->nutrients < 0) b->nutrients = 0;
    } else if (!b->nutrients) {
        b->nutrients = -1;
    }
    if (*game_base_upkeep_state == 1) f->nutrient_surplus_total += game_range(b->surplus[RESOURCE_NUTRIENT], 0, 99);
}

// What the original base_minerals() leaves in the stack word that
// base_energy() reads before setting it; see there.
static int minerals_left;

// Minerals, then the ecological damage they cause.
void base_minerals(void) {
    struct base *b = *game_current_base;
    int faction = b->faction, id = *game_current_base_id;
    const struct faction_data *f = &game_factions[faction];
    b->intake_2[RESOURCE_MINERAL] += game_base_convoy_to[RESOURCE_MINERAL];
    int bonus = has_fac(FAC_QUANTUM_CONVERTER, id, 0) || holds_project(SP_SINGULARITY_INDUCTOR, faction);
    bonus += has_fac(FAC_ROBOTIC_ASSEMBLY_PLANT, id, 0) + has_fac(FAC_GENEJACK_FACTORY, id, 0) +
             has_fac(FAC_NANOREPLICATOR, id, 0) + holds_project(SP_BULK_MATTER_TRANSMITTER, faction);
    b->intake_2[RESOURCE_MINERAL] = b->intake_2[RESOURCE_MINERAL] * (bonus + 2) / 2;
    b->consumption[RESOURCE_MINERAL] = *game_base_forces_upkeep + game_base_convoy_from[RESOURCE_MINERAL];
    b->surplus[RESOURCE_MINERAL] = b->intake_2[RESOURCE_MINERAL] - b->consumption[RESOURCE_MINERAL];
    b->inefficiency[RESOURCE_MINERAL] = 0;
    b->surplus[RESOURCE_MINERAL] -= b->inefficiency[RESOURCE_MINERAL];
    b->mineral_surplus_final = b->surplus[RESOURCE_MINERAL];

    // The terraforming damage base_yield() found, an eighth kept (rounding
    // down, as a shift), is first taken off the minerals the faction may
    // make cleanly.
    b->eco_damage >>= 3;
    int clean = f->clean_minerals + 16;
    if (b->eco_damage > 0) {
        int used = b->eco_damage >= clean ? clean : b->eco_damage;
        clean -= used;
        b->eco_damage -= used;
    }
    int cleaner = has_fac(FAC_NANOREPLICATOR, id, 0) || holds_project(SP_SINGULARITY_INDUCTOR, faction) ? 2 : 1;
    cleaner += has_facility(id, FAC_CENTAURI_PRESERVE) + has_facility(id, FAC_TEMPLE_OF_PLANET) +
               holds_project(SP_PHOLUS_MUTAGEN, faction);
    minerals_left = cleaner;
    b->eco_damage += (b->intake_2[RESOURCE_MINERAL] - clean - game_range(f->satellites[RESOURCE_MINERAL], 0, b->pop_size)) / cleaner;
    if (game_is_human(faction)) {
        // Atrocities and tectonic payloads, each divided on its own.
        int d = game_range(*game_sea_level, 0, 100) / game_range(game_worldbuilder->sea_level_rises, 1, 100) + cleaner;
        b->eco_damage += f->major_atrocities * 5 / d;
        b->eco_damage += game_tectonic_detonations[faction] * 5 / d;
    }
    if (b->eco_damage < 0) b->eco_damage = 0;
    // Any Ascent to Transcendence not unbuilt, even a destroyed one.
    if (game_secret_projects[SP_ASCENT] != -1 && (*game_rules & RULES_VICTORY_TRANSCENDENCE)) b->eco_damage *= 2;
    if (*game_state & GAME_STATE_PERIHELION) b->eco_damage *= 2;
    int level;
    if (game_is_human(faction)) {
        int diff = f->diff_level;
        level = !diff ? 2 : diff <= 3 ? 3 : 5;
    } else {
        level = game_range(6 - *game_difficulty, 1, 3);
    }
    b->eco_damage = (f->tech_ranking - f->transcendent_techs) * (3 - game_range(f->se_pending[SE_PLANET], -3, 2)) *
                    (*game_native_life + 1) * b->eco_damage * level / 6;
    b->eco_damage = (b->eco_damage + 50) / 100;

    if (holds_project(SP_SPACE_ELEVATOR, faction) && b->queue[0] <= -FAC_SKY_HYDRO_LAB &&
        b->queue[0] >= -FAC_ORBITAL_DEFENSE_POD) {
        // mineral_surplus_final keeps the figure from before.
        b->intake_2[RESOURCE_MINERAL] *= 2;
        b->surplus[RESOURCE_MINERAL] = b->intake_2[RESOURCE_MINERAL] - b->consumption[RESOURCE_MINERAL];
    }
}

// ---- Energy -------------------------------------------------------------------

static inline int is_alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }

// Energy lost to distance from headquarters, out of `energy`. Without
// headquarters the faction counts as 16 squares away. While bases are
// worked through at the end of a turn, the loss at every EFFICIENCY from 4
// down to -4 is added up for the AI.
int black_market(int energy) {
    if (energy <= 0) return 0;
    const struct base *b = *game_current_base;
    int faction = b->faction, dist = 999;
    for (int i = 0; i < *game_base_count; i++)
        if (game_bases[i].faction == faction && has_facility(i, FAC_HEADQUARTERS)) {
            int d = vector_dist(game_bases[i].x, game_bases[i].y, b->x, b->y);
            if (d < dist) dist = d;
        }
    if (dist == 999) dist = 16;
    else if (!dist) return 0;
    // A creche counts as 2 more EFFICIENCY.
    int creche = has_facility(*game_current_base_id, FAC_CHILDREN_CRECHE);
    struct faction_data *f = &game_factions[faction];
    if (*game_base_upkeep_state == 1)
        for (int i = 0; i < 9; i++) {
            int loss = creche ? i - 2 : i;
            f->social_effic[i] += loss >= 8 ? energy : energy * dist / (8 * (8 - loss));
        }
    int loss = 4 - f->se_pending[SE_EFFIC] - (creche ? 2 : 0);
    if (loss >= 8) return energy;
    int v = energy * dist / ((8 - loss) * 8);
    return v < 0 ? 0 : v > energy ? energy : v;
}

// How many citizens of a base are content before anything else counts
// (fewer on harder levels), and how many bases the faction may have
// before more make drones (OpenSMACX calls them drones and talents).
void psych_check(int faction, int *content, int *base_limit) {
    *content = 6 - (game_is_human(faction) ? game_factions[faction].diff_level : 3);
    int effic = game_factions[faction].se_pending[SE_EFFIC];
    *base_limit = (*content + 2) * (effic < 0 ? 4 : effic + 4) * *game_map_area_sqrt / 56 / 2;
}

// The faction's base that has `rank` of its others above it by energy
// intake, ties going to the later base; or -1.
int base_rank(int faction, int rank) {
    // The original reads outside its list for a negative rank; nothing
    // asks for one.
    if (rank < 0) return -1;
    for (int i = 0; i < *game_base_count; i++) {
        if (game_bases[i].faction != faction) continue;
        int v = game_bases[i].intake[RESOURCE_ENERGY] * 512 + i, above = 0;
        for (int j = 0; j < *game_base_count; j++)
            if (game_bases[j].faction == faction && game_bases[j].intake[RESOURCE_ENERGY] * 512 + j > v) above++;
        if (above == rank) return i;
    }
    return -1;
}


// The current base's energy: trade, the alien energy grid, losses, the
// split into economy, psych and labs with specialists, facilities and
// projects, then base_psych().
void base_energy(void) {
    struct base *b = *game_current_base;
    int faction = b->faction, id = *game_current_base_id;
    struct faction_data *f = &game_factions[faction];
    int32_t *intake = &b->intake_2[RESOURCE_ENERGY];
    *intake += game_base_convoy_to[RESOURCE_ENERGY];
    b->consumption[RESOURCE_ENERGY] = game_base_convoy_from[RESOURCE_ENERGY];
    b->inefficiency[RESOURCE_ENERGY] = black_market(*intake - b->consumption[RESOURCE_ENERGY]);
    b->surplus[RESOURCE_ENERGY] = *intake - b->consumption[RESOURCE_ENERGY] - b->inefficiency[RESOURCE_ENERGY];

    // Where the base ranks among the faction's by energy intake, and where
    // it sits in the base table.
    int rank = 0, self = id;
    int mine = b->intake[RESOURCE_ENERGY] * 512 + id;
    for (int i = 0; i < *game_base_count; i++)
        if (game_bases[i].faction == faction) {
            if (game_bases[i].intake[RESOURCE_ENERGY] * 512 + i > mine) rank++;
            if (&game_bases[i] == b) self = i;
        }

    // Trade with each faction it has a treaty with, base against the base
    // of the same rank there. The original keeps the last import in the
    // same local it later counts the alien energy grid in, without setting
    // that first: with no trade to count, the grid starts from what
    // base_minerals() left in that stack word, which every caller runs
    // just before.
    int grid = minerals_left;
    for (int i = 1; i < FACTION_COUNT; i++) {
        game_base_commerce_import[i] = game_base_commerce_export[i] = 0;
        const struct faction_data *other = &game_factions[i];
        if (i == faction) continue;
        if (!(f->diplo_status[i] & TREATY_TREATY) && !(other->diplo_status[faction] & TREATY_TREATY)) continue;
        if (!other->base_count) continue;
        int theirs = base_rank(i, rank);
        if (theirs < 0) continue;
        int value = (game_bases[theirs].intake[RESOURCE_ENERGY] + b->intake[RESOURCE_ENERGY] + 7) / 8;
        if (game_proposal_passes[PROPOSAL_GLOBAL_TRADE_PACT] > game_proposal_passes[PROPOSAL_REPEAL_GLOBAL_TRADE_PACT])
            value *= 2;
        int techs = *game_tech_commerce_count + 1;
        int out = ((other->tech_commerce_bonus + 1) * value + techs / 2) / techs;
        int in = ((f->tech_commerce_bonus + 1) * value + techs / 2) / techs;
        if (!(f->diplo_status[i] & TREATY_PACT)) in /= 2, out /= 2;
        if (*game_governor_faction == faction) in++;
        if (*game_governor_faction == i) out++;
        if (f->sanction_turns || other->sanction_turns || is_alien(faction) || is_alien(i)) in = out = 0;
        grid = in;
        game_base_commerce_import[i] = in;
        game_base_commerce_export[i] = out;
        *intake += in;
    }
    // Alien Crossfire's aliens get half of 1 a facility and 5 a project.
    if (*game_expansion && is_alien(faction)) {
        for (int i = 0; i < FAC_FIRST_PROJECT; i++)
            if (b->facilities_built[i / 8] & (1 << (i % 8))) grid++;
        for (int i = 0; i < SP_COUNT; i++)
            if (game_secret_projects[i] == self) grid += 5;
        grid /= 2;
        if (f->sanction_turns) grid = 0;
        *intake += grid;
    }

    // The faction total takes the surplus from before trade.
    if (*game_base_upkeep_state == 1) f->energy_surplus_total += game_range(b->surplus[RESOURCE_ENERGY], 0, 99999);
    b->inefficiency[RESOURCE_ENERGY] = black_market(*intake - b->consumption[RESOURCE_ENERGY]);
    b->surplus[RESOURCE_ENERGY] = *intake - b->consumption[RESOURCE_ENERGY] - b->inefficiency[RESOURCE_ENERGY];

    // The allocation, in tenths rounded; labs get what is left over.
    int total = game_range(b->surplus[RESOURCE_ENERGY], 0, 9999);
    int psych = (f->alloc_psych * total + 4) / 10;
    b->psych_total = psych < 0 ? 0 : psych > total ? total : psych;
    int left = total - b->psych_total;
    b->economy_total = clamp_to(((10 - f->alloc_labs - f->alloc_psych) * total + 4) / 10, left);
    b->labs_total = total - b->psych_total - b->economy_total;
    b->unk_total = clamp_to(((9 - f->alloc_labs - f->alloc_psych) * total + 4) / 10, left);

    // Specialists past the sixteenth have no slot and are chosen afresh.
    // One the faction's techs have made obsolete becomes the first kind
    // available that is at least as good at everything.
    for (int i = 0; i < b->specialist_total; i++) {
        int kind = specialist_kind(b, i);
        kind = kind < 0 ? 0 : kind > CITIZEN_KINDS - 1 ? CITIZEN_KINDS - 1 : kind;
        const struct citizen *c = &game_citizens[kind];
        if (has_tech(c->obsol_tech, faction))
            for (int j = 0; j < CITIZEN_KINDS; j++) {
                const struct citizen *n = &game_citizens[j];
                if (has_tech(n->preq_tech, faction) && !has_tech(n->obsol_tech, faction) && n->econ >= c->econ &&
                    n->psych >= c->psych && n->labs >= c->labs) {
                    kind = j;
                    set_specialist(b, i, j);
                    break;
                }
            }
        b->economy_total += game_citizens[kind].econ;
        b->psych_total += game_citizens[kind].psych;
        b->labs_total += game_citizens[kind].labs;
    }

    // Facilities raise each in quarters.
    b->labs_total += game_faction_defs[faction].rule_research;
    if (has_facility(id, FAC_BIOLOGY_LAB)) b->labs_total += 2;
    int econ_q = 4, labs_q = 4, psych_q = 4;
    if (has_facility(id, FAC_ENERGY_BANK) || holds_project(SP_PLANETARY_ENERGY_GRID, faction)) econ_q = 6;
    if (has_facility(id, FAC_NETWORK_NODE)) labs_q = 6;
    if (has_facility(id, FAC_HOLOGRAM_THEATRE) ||
        (has_facility(id, FAC_NETWORK_NODE) && holds_project(SP_VIRTUAL_WORLD, faction)))
        psych_q = 6;
    if (has_facility(id, FAC_RESEARCH_HOSPITAL)) psych_q++, labs_q += 2;
    if (has_facility(id, FAC_NANOHOSPITAL)) psych_q++, labs_q += 2;
    if (has_facility(id, FAC_TREE_FARM)) econ_q += 2, psych_q += 2;
    if (has_facility(id, FAC_HYBRID_FOREST)) econ_q += 2, psych_q += 2;
    if (has_facility(id, FAC_FUSION_LAB)) econ_q += 2, labs_q += 2;
    if (has_facility(id, FAC_QUANTUM_LAB)) econ_q += 2, labs_q += 2;
    b->psych_total = (b->psych_total * psych_q + 3) / 4;
    b->labs_total = (b->labs_total * labs_q + 3) / 4;
    if (game_secret_projects[SP_SUPERCOLLIDER] == id) b->labs_total *= 2;
    if (game_secret_projects[SP_THEORY_OF_EVERYTHING] == id) b->labs_total *= 2;
    b->economy_total = (b->economy_total * econ_q + 3) / 4;
    if (game_secret_projects[SP_SPACE_ELEVATOR] == id) b->economy_total *= 2;
    if (game_secret_projects[SP_LONGEVITY_VACCINE] == id && f->se_models_pending[1] == ECONOMICS_FREE_MARKET)
        b->economy_total += b->economy_total / 2;
    b->unk_total = (b->unk_total * econ_q + 3) / 4;
    // The Network Backbone: a lab for every network node anywhere, and the
    // base's trade or energy grid again.
    if (game_secret_projects[SP_NETWORK_BACKBONE] == id) {
        for (int i = 0; i < *game_base_count; i++)
            if (has_facility(i, FAC_NETWORK_NODE)) b->labs_total++;
        if (is_alien(faction)) b->labs_total += grid;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (!f->sanction_turns && !game_factions[i].sanction_turns && !is_alien(faction) && !is_alien(i))
                b->labs_total += game_base_commerce_import[i];
    }

    // Lopsided allocations cost labs and economy, unless EFFICIENCY is 4
    // or more: the side given more loses up to 40%, the other up to 80%.
    int labs = f->alloc_labs, psych_alloc = f->alloc_psych;
    int lopsided = psych_alloc + 2 * labs - 10;
    if (lopsided < 0) lopsided = 2 * (5 - labs) - psych_alloc;
    lopsided /= 2;
    int effic = 2 * (4 - game_range(f->se_pending[SE_EFFIC], -4, 4));
    if (lopsided && effic) {
        int p = effic * lopsided;
        int small_cut = 100 - game_range(p, 0, 40), big_cut = 100 - game_range(2 * p, 0, 80);
        int labs_more = labs > 10 - labs - psych_alloc;
        b->labs_total = (b->labs_total * (labs_more ? small_cut : big_cut) + 50) / 100;
        b->economy_total = (b->economy_total * (labs_more ? big_cut : small_cut) + 50) / 100;
    }

    // Stockpile Energy turns half the minerals into economy, rounded up,
    // and a quarter more with the Planetary Energy Grid; from its second
    // turn on.
    if (b->queue[0] == -FAC_STOCKPILE_ENERGY && b->surplus[RESOURCE_MINERAL] > 0 &&
        b->production_last == -FAC_STOCKPILE_ENERGY) {
        int v = (b->surplus[RESOURCE_MINERAL] + 1) / 2;
        if (holds_project(SP_PLANETARY_ENERGY_GRID, faction)) v = (v * 5 + 3) / 4;
        b->economy_total += v;
    }
    base_psych();
}

// ---- Talents and drones ------------------------------------------------------

// Until talents, drones and superdrones fit among the citizens at work:
// a talent cancels a superdrone or a drone, then drones beyond the workers
// go, and superdrones are kept to the drones.
static void psych_fit(struct base *b) {
    for (;;) {
        int workers = b->pop_size - b->specialist_total;
        if (b->talent_total + b->drone_total + b->superdrone_total <= workers) return;
        if (b->talent_total) {
            b->talent_total--;
            if (b->superdrone_total) b->superdrone_total--;
            else if (b->drone_total) b->drone_total--;
        } else if (b->drone_total > workers) {
            b->drone_total--;
        } else {
            b->superdrone_total = clamp_to(b->superdrone_total, b->drone_total);
            return;
        }
    }
}

// Kept within the population, fitted, and noted as one of the steps.
static void psych_step(struct base *b, int step) {
    b->drone_total = clamp_to(b->drone_total, b->pop_size);
    b->talent_total = clamp_to(b->talent_total, b->pop_size);
    psych_fit(b);
    game_psych_talents[step] = b->talent_total;
    game_psych_drones[step] = b->drone_total;
    game_psych_superdrones[step] = b->superdrone_total;
}

// Alien Crossfire's Cult of Planet: its psi land units police twice. So do
// the first prototype ids' psi units and id 15 (the Spore Launcher, in the
// standard rules).
static int worm_police(int proto, int faction) {
    const struct unit_proto *u = &game_protos[proto];
    return (game_faction_defs[faction].rule_flags & RULE_FLAG_WORM_POLICE) && proto < 64 &&
           (game_weapons[u->weapon].offense < 0 || proto == 15) && game_chassis[u->chassis].triad == TRIAD_LAND;
}

// What base_psych() works out once for the base, before TALENT and POLICE.
struct psych_base {
    int drones, talents;       // from the population, the level and the faction's rules
    int bureaucracy;           // drones for having too many bases
    int facility_drones;       // made (or, negative, calmed) by facilities
    int facility_talents;      // from a Paradise Garden
    int project_talents;       // from secret projects
    int punished;              // a Punishment Sphere
};

// One pass of the steps, at the given TALENT and POLICE.
static void psych_pass(struct base *b, const struct psych_base *p, int talent, int police) {
    int faction = b->faction, pop = b->pop_size;
    const struct faction_data *f = &game_factions[faction];
    // POLICE -2 allows no units; -1 and 0 one; 1 two; from 2 three.
    int units = game_range(police + 2, 0, 4);
    if (units > 1) units--;
    b->drone_total = p->drones;
    b->talent_total = p->talents;
    if (talent >= 0) b->talent_total += talent;
    else b->drone_total -= talent;
    b->drone_total = clamp_to(b->drone_total, pop) + p->bureaucracy;
    b->superdrone_total = clamp_to(b->drone_total - pop, pop);
    b->drone_total = clamp_to(b->drone_total, pop);
    psych_fit(b);
    game_psych_talents[0] = b->talent_total;
    game_psych_drones[0] = b->drone_total;
    game_psych_superdrones[0] = b->superdrone_total;

    // Psych energy: a talent for every 2.
    b->talent_total += b->psych_total / 2;
    psych_step(b, 1);

    b->drone_total += p->facility_drones;
    b->talent_total += p->facility_talents;
    psych_step(b, 2);

    // Police: the Self-Aware Colony counts as one unit. Units that police
    // twice go first, then the other combat units on the square, whoever
    // they belong to. POLICE 3 or more calms one more drone a unit.
    if (units > 0) {
        int used = 0;
        if (holds_project(SP_SELF_AWARE_COLONY, faction)) {
            used = 1;
            if (b->drone_total) b->drone_total--;
            if (police >= 3 && b->drone_total) b->drone_total--;
        }
        for (int pass = 0; pass < 2 && used < units; pass++)
            for (int v = game_stack_fix(game_veh_at(b->x, b->y)); v >= 0; v = game_vehs[v].next_in_stack) {
                int proto = game_vehs[v].proto;
                if (game_protos[proto].plan > PLAN_RECON) continue;
                int twice = game_has_abil(proto, ABILITY_POLICE_2X) != 0 || worm_police(proto, faction);
                if (twice == pass) continue;
                if (b->drone_total) b->drone_total--;
                if (game_has_abil(proto, ABILITY_POLICE_2X) && b->drone_total) b->drone_total--;
                if (police >= 3 && b->drone_total) b->drone_total--;
                if (worm_police(proto, faction) && b->drone_total) b->drone_total--;
                if (++used >= units) break;
            }
    }
    // Below -2, units away from home make drones.
    int away = *game_base_pacifism;
    if (police == -3) {
        if (away > 1) b->drone_total += away - 1;
    } else if (police == -4) {
        b->drone_total += away;
    } else if (police < -4) {
        b->drone_total += 2 * away;
    }
    psych_step(b, 3);

    // The Longevity Vaccine takes a drone under Planned, and another under
    // anything but Free Market, even with none left (the original
    // doesn't check).
    b->talent_total += p->project_talents;
    if (holds_project(SP_LONGEVITY_VACCINE, faction)) {
        if (f->se_models_pending[1] == ECONOMICS_PLANNED) b->drone_total--;
        if (f->se_models_pending[1] != ECONOMICS_FREE_MARKET) b->drone_total--;
    }
    if (p->punished || b->nerve_staple_turns) b->drone_total = b->talent_total = 0;
    psych_step(b, 4);
}

// How content a pass left the base, as the AI scores it.
static int psych_score(const struct base *b) {
    int score = b->talent_total - b->drone_total;
    if (score > 0) {
        if (b->drone_total || 2 * b->talent_total < b->pop_size) score /= 2;
    } else if (score < 0) {
        score -= b->pop_size;
        if (b->pop_size < 6) score *= 2;
    }
    return score;
}

void base_psych(void) {
    struct base *b = *game_current_base;
    int faction = b->faction, id = *game_current_base_id, pop = b->pop_size;
    struct faction_data *f = &game_factions[faction];
    const struct faction_def *m = &game_faction_defs[faction];
    struct psych_base p = {0};
    int content, limit;
    psych_check(faction, &content, &limit);
    if (limit) p.bureaucracy = clamp_to((id % limit + f->base_count - limit) / limit, pop);
    int diff = game_is_human(faction) ? f->diff_level : 3;
    p.drones = clamp_to(pop - content, pop);
    // A captured base, for a while.
    p.drones += clamp_to((b->assimilation_turns + 9) / 10, (pop + diff - 2) / 4);
    if (m->rule_drone) p.drones += pop / m->rule_drone;
    if (m->rule_talent) p.talents = (m->rule_talent + pop - 1) / m->rule_talent;
    for (int i = 0; i < m->bonus_count; i++)
        if (m->bonus_id[i] == BONUS_NODRONE) {
            p.drones -= m->bonus_val1[i];
            if (p.drones < 0) p.drones = 0;
            break;
        }
    if (has_facility(id, FAC_GENEJACK_FACTORY)) p.facility_drones = game_rules_basic->drones_genejack;
    if (has_facility(id, FAC_RECREATION_COMMONS)) p.facility_drones -= 2;
    if ((has_facility(id, FAC_NETWORK_NODE) && holds_project(SP_VIRTUAL_WORLD, faction)) ||
        has_facility(id, FAC_HOLOGRAM_THEATRE))
        p.facility_drones -= 2;
    if (holds_project(SP_PLANETARY_TRANSIT_SYSTEM, faction) && pop <= 3) p.facility_drones--;
    if (has_facility(id, FAC_PARADISE_GARDEN)) p.facility_talents = 2;
    if (has_facility(id, FAC_RESEARCH_HOSPITAL)) p.facility_drones--;
    if (has_facility(id, FAC_NANOHOSPITAL)) p.facility_drones--;
    p.project_talents = holds_project(SP_HUMAN_GENOME_PROJECT, faction) + 2 * holds_project(SP_CLINICAL_IMMORTALITY, faction);
    p.punished = has_facility(id, FAC_PUNISHMENT_SPHERE);

    // At the end of a turn, every TALENT from -3 to 4 against every POLICE
    // from -5 to 3, for the AI; then the faction's own, with 2 more POLICE
    // for a Brood Pit, which the base keeps.
    if (*game_base_upkeep_state == 1)
        for (int t = 7; t >= 0; t--)
            for (int q = 8; q >= 0; q--) {
                psych_pass(b, &p, t - 3, q - 5);
                f->social_psych[t][q] += psych_score(b);
            }
    psych_pass(b, &p, f->se_pending[SE_TALENT], f->se_pending[SE_POLICE] + 2 * has_facility(id, FAC_BROOD_PIT));
}

// ---- Growth -------------------------------------------------------------------

// The population a base aims for: a sixth of the way from its size to 36,
// at least 6, and no more than a habitation limit allows.
int pop_goal(int base) {
    const struct base *b = &game_bases[base];
    int faction = b->faction, ascetic = holds_project(SP_ASCETIC_VIRTUES, faction) ? 2 : 0;
    int rule = game_faction_defs[faction].rule_population;
    int goal = (36 - b->pop_size) / 6 + b->pop_size;
    if (goal <= 6) goal = 6;
    if (!has_facility(base, FAC_HAB_COMPLEX)) {
        int limit = game_rules_basic->pop_limit_hab_complex - rule + ascetic;
        if (goal >= limit) goal = limit;
    }
    if (!has_facility(base, FAC_HABITATION_DOME)) {
        int limit = game_rules_basic->pop_limit_hab_dome - rule + ascetic;
        if (goal >= limit) goal = limit;
    }
    return goal;
}

// The habitation facility the base needs to grow further, or 0.
int pop_goal_fac(int base) {
    const struct base *b = &game_bases[base];
    int faction = b->faction, ascetic = holds_project(SP_ASCETIC_VIRTUES, faction) ? 2 : 0;
    int pop = b->pop_size - ascetic + game_faction_defs[faction].rule_population;
    if (pop >= game_rules_basic->pop_limit_hab_complex && !has_facility(base, FAC_HAB_COMPLEX)) return FAC_HAB_COMPLEX;
    if (pop >= game_rules_basic->pop_limit_hab_dome && !has_facility(base, FAC_HABITATION_DOME)) return FAC_HABITATION_DOME;
    return 0;
}

// The faction's cost of growth (type 0) or of building (1), in tenths of
// the rules' rows: the AI's by difficulty, less 1 against a far stronger
// faction and 1 against a human at the top in a single-player game; then
// the rules' multiplier, the planet size, and GROWTH (with a creche and a
// golden age at `base`) or INDUSTRY.
int cost_factor(int faction, int type, int base) {
    int factor;
    if (game_is_human(faction)) {
        factor = 10;
    } else {
        int top = game_faction_rankings[7];
        int human_top = !*game_net_multiplayer && game_is_human(top);
        factor = game_cost_base[*game_difficulty] - (game_great_satan(top, 0) != 0) - human_top;
    }
    int multiplier = type ? game_rules_basic->mineral_cost_multi : game_rules_basic->nutrient_cost_multi;
    if (multiplier != 10) factor = factor * multiplier / 10;
    if (game_map_settings[0] == 1) factor = factor * 9 / 10;
    else if (game_map_settings[0] == 0) factor = factor * 8 / 10;
    const struct faction_data *f = &game_factions[faction];
    if (!type) {
        int growth = f->se_pending[SE_GROWTH];
        if (base >= 0) {
            if (has_facility(base, FAC_CHILDREN_CRECHE)) growth += 2;
            if (game_bases[base].state & BASE_STATE_GOLDEN_AGE) growth += 2;
        }
        return ((10 - game_range(growth, -2, 5)) * factor + 9) / 10;
    }
    if (type != 1) return factor;
    switch (f->se_pending[SE_INDUSTRY]) {
    case -7: case -6: case -5: case -4: case -3: return (factor * 13 + 9) / 10;
    case -2: return (factor * 6 + 4) / 5;
    case -1: return (factor * 11 + 9) / 10;
    case 0: return factor;
    case 1: return (factor * 9 + 9) / 10;
    case 2: return (factor * 4 + 4) / 5;
    case 3: return (factor * 7 + 9) / 10;
    case 4: return (factor * 3 + 4) / 5;
    default: return (factor + 1) / 2;
    }
}

// ---- The whole of it -------------------------------------------------------

// Works out the current base, unless it was the last one worked out and
// `update` is clear.
void base_compute(int update) {
    if (!update && *game_computed_base_id == *game_current_base_id) return;
    *game_computed_base_id = *game_current_base_id;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy();
}

// ---- Growing and starving -----------------------------------------------------

// base_growth()'s messages, as image addresses of each label and picture:
// for most factions, then for the Progenitors.
struct growth_message {
    uint32_t label, picture;
};
static const struct growth_message msg_starve[2] = {{0x6894c8, 0x6894b8}, {0x6894b0, 0x6894a0}};
static const struct growth_message msg_pop_limit[2] = {{0x6894fc, 0x6894f0}, {0x6894e0, 0x6894d0}};
// Short of nutrients: with farmland to improve, with and without formers;
// with specialists, with and without drones; and otherwise.
static const struct growth_message msg_farms[2] = {{0x6895c8, 0x6895b8}, {0x68951c, 0x68950c}};
static const struct growth_message msg_formers[2] = {{0x6895ec, 0x6895dc}, {0x689540, 0x689530}};
static const struct growth_message msg_specialists_drones[2] = {{0x689610, 0x689600}, {0x689564, 0x689554}};
static const struct growth_message msg_specialists[2] = {{0x689634, 0x689624}, {0x689588, 0x689578}};
static const struct growth_message msg_low_nutrient[2] = {{0x689658, 0x689648}, {0x6895ac, 0x68959c}};

static void growth_popb(const struct growth_message *m, int faction, int flags, int sound) {
    const struct growth_message *which = &m[(game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0];
    game_popb(game_growth_text(which->label), flags, sound, game_growth_text(which->picture), 0);
}

// The current base starves. Unless it is its faction's last objective of
// size 1, it loses a citizen, and with none left is lost. Its nutrient
// convoys sitting in bases stop. Returns whether it was lost.
static int starve(struct base *b, int id, int faction) {
    if (game_base_convoy_from[RESOURCE_NUTRIENT])
        for (int v = 0; v < *game_veh_count; v++) {
            struct veh *u = &game_vehs[v];
            if (u->home_base != id || game_protos[u->proto].plan != PLAN_SUPPLY || u->order != ORDER_CONVOY ||
                u->order_auto_type != RESOURCE_NUTRIENT)
                continue;
            const struct map_tile *t = tile(u->x, u->y);
            if ((t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8) u->order = 0;
        }
    growth_popb(msg_starve, faction, 0x4000, 14);
    if (!game_is_objective(id) || b->pop_size > 1) {
        b->pop_size--;
        if (b->pop_size <= 0) {
            game_base_kill(id);
            game_draw_map(1);
            game_eliminate_player(faction, 0);
            return 1;
        }
    }
    return 0;
}

// The tutorial's advice when the current base's stores will run out within
// five turns: improve farmland (with formers or without), turn specialists
// back into workers, or just a warning. Farmland is unimproved land, not
// forest with a tree farm or fungus the faction farms well, that is moist,
// or rainy with the tech for three nutrients, and not worked already as
// fungus, forest, rocky or arid land. (The original also counts the worked
// squares left out, and unfarmed ocean, but makes no use of the count.)
static void low_nutrient_advice(const struct base *b, int id, int faction) {
    int farmland = 0;
    for (int i = 1; i < BASE_RADIUS_SQUARES; i++) {
        int x, y;
        if (!radius_square(b, i, &x, &y)) continue;
        const struct map_tile *t = tile(x, y);
        int worked = (b->worked_tiles >> i & 1) != 0;
        if (alt_of(t) < ALT_SHORE_LINE || (t->bits & MAP_FARM)) continue;
        if ((t->bits & MAP_FOREST) && has_facility(id, FAC_TREE_FARM)) continue;
        int fungus = (t->bits & MAP_FUNGUS) != 0;
        if (fungus && game_factions[faction].tech_fungus[FUNGUS_NUTRIENT] > 1) continue;
        int rain = t->climate >> 3 & 3;
        if (worked && (fungus || (t->bits & MAP_FOREST) || (t->rocky_lock & 0xc0) == 0x80 || !rain)) continue;
        if (rain == 1 || has_tech(game_rules_basic->tech_three_nutrients, faction)) farmland++;
    }
    if (farmland && has_tech(game_protos[PROTO_FORMERS].preq_tech, faction)) {
        growth_popb(game_factions[faction].units_active[PROTO_FORMERS] ? msg_farms : msg_formers, faction, 0, -1);
    } else if (b->specialist_total <= 1 && (!b->specialist_total || b->pop_size >= 3)) {
        growth_popb(msg_low_nutrient, faction, 0x80, 16);
    } else {
        growth_popb(*game_psych_drones ? msg_specialists_drones : msg_specialists, faction, 0, -1);
    }
}

// The habitation facility the base needs before it can grow, or 0.
static int growth_blocked(const struct base *b, int id, int faction) {
    int pop = b->pop_size - (holds_project(SP_ASCETIC_VIRTUES, faction) ? 2 : 0) + game_faction_defs[faction].rule_population;
    if (pop >= game_rules_basic->pop_limit_hab_complex && !has_fac(FAC_HAB_COMPLEX, id, 0)) return FAC_HAB_COMPLEX;
    if (pop >= game_rules_basic->pop_limit_hab_dome && !has_fac(FAC_HABITATION_DOME, id, 0)) return FAC_HABITATION_DOME;
    return 0;
}

// The current base's nutrients at the end of its turn. With its stores
// below zero it starves, outside a finished game, and starts again from
// this turn's surplus. With a population boom (GROWTH 6 or the Cloning
// Vats) and a surplus of a citizen's food it grows at once; with full
// stores it grows and empties them, or at a habitation limit says so and
// keeps half; and at GROWTH -3 or below (stagnation) stores stay full.
// Then this turn's surplus is added, with advice when it will empty the
// stores within five turns, unless the governor places citizens. Returns
// whether the base was lost.
int base_growth(void) {
    struct base *b = *game_current_base;
    int id = *game_current_base_id, faction = b->faction, x = b->x, y = b->y;
    if (b->nutrients < 0) {
        if (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE))
            if (starve(b, id, faction)) return 1;
        b->nutrients = b->surplus[RESOURCE_NUTRIENT];
        game_draw_tile(x, y, 2);
        return 0;
    }
    int cost = cost_factor(faction, 0, id) * (b->pop_size + 1);
    int need = game_rules_basic->nutrient_intake_citizen;
    if ((holds_project(SP_CLONING_VATS, faction) || *game_base_growth >= 6) && need &&
        b->surplus[RESOURCE_NUTRIENT] >= need && !growth_blocked(b, id, faction)) {
        if (b->pop_size < 127) b->pop_size++;
        base_compute(1);
        game_draw_tile(x, y, 2);
        if (b->nutrients > cost) b->nutrients = cost;
        return 0;
    }
    if (b->nutrients >= cost) {
        if (*game_base_growth <= -3) {
            b->nutrients = cost;
            return 0;
        }
        int fac = growth_blocked(b, id, faction);
        if (fac) {
            b->nutrients = cost_factor(b->faction, 0, id) * (b->pop_size + 1) / 2;
            game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
            growth_popb(msg_pop_limit, faction, 0x800, 16);
        } else {
            b->nutrients = 0;
            b->pop_size++;  // no limit here: 127 wraps
            base_compute(1);
            game_draw_tile(x, y, 2);
        }
    }
    b->nutrients += b->surplus[RESOURCE_NUTRIENT];
    // The tests on the stores, as the original makes them, by the sign of
    // the 32-bit result.
    int32_t s = b->surplus[RESOURCE_NUTRIENT];
    if (s >= 0 || (int32_t)((uint32_t)b->nutrients - (uint32_t)s) < 0 ||
        (int32_t)((uint32_t)b->nutrients + 5u * (uint32_t)s) >= 0)
        return 0;
    if ((b->governor & (GOVERNOR_ACTIVE | GOVERNOR_CITIZENS)) == (GOVERNOR_ACTIVE | GOVERNOR_CITIZENS)) return 0;
    low_nutrient_advice(b, id, faction);
    return 0;
}

// ---- Production -------------------------------------------------------------

// Takes the item at the head of the base's queue off it (a unit no longer
// counts as being built), and the next ones the base can no longer build,
// until one it can is at the head; a secret project plays its sound.
// Returns whether one was found. Like the original, it tries the entry
// moved up from past the end even when the queue is now empty, and moving
// a full queue up reads one entry past its end (the base's worked squares).
int base_queue(int base) {
    struct base *b = &game_bases[base];
    int faction = b->faction;
    while (b->queue_size) {
        int item = b->queue[0];
        if (item >= 0) game_factions[faction].units_queue[item]--;
        if (b->queue_size > 0) memmove((char *)b + offsetof(struct base, queue), (char *)b + offsetof(struct base, queue) + 4, (size_t)b->queue_size * 4);
        b->queue_size--;
        item = b->queue[0];
        if (item >= 0 ? game_veh_avail(item, faction, base) : game_facility_avail(-item, faction, base, 0)) {
            if (b->queue[0] <= -FAC_FIRST_PROJECT) game_wave_it(36);
            game_draw_radius(b->x, b->y, 2, 2);
            return 1;
        }
    }
    return 0;
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module base_module;
#define BASE (&base_module)

#define WRITES_YIELD WRITES(game_terraform_reduce, 4); WRITES(game_terraform_energy, 4)
#define YIELD(name)                                                                                              \
    SHADOW_WRITER(BASE, int, name, (int a, int b, int c, int d, int e), (a, b, c, d, e), WRITES_YIELD, \
                  "%d, %d, %d, %d, %d", a, b, c, d, e)
YIELD(crop_yield)
YIELD(mine_yield)
YIELD(energy_yield)
YIELD(resource_yield)
SHADOW3(BASE, int, has_fac, int, int, int)

// The totals write the current base, the base-compute globals and the
// base's faction.
#define WRITES_CURRENT                                                         \
    WRITES(*game_current_base, sizeof(struct base));                           \
        WRITES(game_base_globals, BASE_GLOBALS_END - BASE_GLOBALS_START);      \
        WRITES(&game_factions[(*game_current_base)->faction], sizeof(struct faction_data))
#define WRITES_SUPPORT WRITES_CURRENT; WRITES(game_vehs, *game_veh_count * sizeof(struct veh))
SHADOW_WRITER(BASE, void, base_support, (void), (), WRITES_SUPPORT, "%s", "")
SHADOW_WRITER(BASE, void, base_yield, (void), (), WRITES_CURRENT, "%s", "")
#define WRITES_COMPUTE WRITES_SUPPORT; WRITES(game_computed_base_id, sizeof(*game_computed_base_id))
SHADOW_WRITER(BASE, void, base_compute, (int a), (a), WRITES_COMPUTE, "%d", a)
SHADOW_WRITER(BASE, void, farm_compute, (void), (), WRITES_CURRENT, "%s", "")
SHADOW_WRITER(BASE, void, base_nutrient, (void), (), WRITES_CURRENT, "%s", "")
SHADOW_WRITER(BASE, void, base_minerals, (void), (), WRITES_CURRENT, "%s", "")
SHADOW_WRITER(BASE, int, black_market, (int a), (a), WRITES_CURRENT, "%d", a)
#define WRITES_PSYCH_CHECK WRITES(b, sizeof(*b)); WRITES(c, sizeof(*c))
SHADOW_WRITER(BASE, void, psych_check, (int a, int *b, int *c), (a, b, c), WRITES_PSYCH_CHECK, "%d", a)
SHADOW2(BASE, int, base_rank, int, int)
SHADOW1(BASE, int, pop_goal, int)
SHADOW1(BASE, int, pop_goal_fac, int)
SHADOW3(BASE, int, cost_factor, int, int, int)

// Calls fn with `value` in the stack word its frame keeps at [ebp-4],
// where the original base_energy() reads before it writes.
void call_with_ebp4(void (*fn)(void), int value);
__asm__(".text\n"
        "call_with_ebp4:\n"
        "    movl 4(%esp), %eax\n"
        "    movl 8(%esp), %ecx\n"
        "    movl %ecx, -12(%esp)\n"  // below the return address and fn's saved ebp
        "    call *%eax\n"
        "    ret\n");
SHADOW_WRITER(BASE, void, base_psych, (void), (), WRITES_CURRENT, "%s", "")
SHADOW_WRITER_VIA(BASE, void, base_energy, (void), (), call_with_ebp4(orig_base_energy, minerals_left), WRITES_CURRENT,
                  "%s", "")

// Growth also writes the base's units' orders. When the base may starve to
// death the original runs unchecked: losing a base writes all over the game.
static int growth_may_kill(void) {
    const struct base *b = *game_current_base;
    return b->nutrients < 0 && b->pop_size <= 1;
}
SHADOW_WRITER_CHECK(int, base_growth, (void), (), orig_base_growth(), WRITES_COMPUTE, "%s", "")
static int shadow_base_growth(void) {
    if (hooks_nested) return orig_base_growth();
    if (shadow_bypass()) return base_growth();
    stats_base_growth.calls++;
    shadow_tick(BASE);
    if (growth_may_kill()) return orig_base_growth();
    return check_base_growth();
}

#define WRITES_QUEUE WRITES(&game_bases[a], sizeof(struct base)); WRITES(&game_factions[game_bases[a].faction], sizeof(struct faction_data))
SHADOW_WRITER(BASE, int, base_queue, (int a), (a), WRITES_QUEUE, "%d", a)

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

// Growth for the current base b as it is, and then with full stores, in a
// population boom, in stagnation, starving and running short; each from
// the base as it is.
static void sweep_growth(int b) {
    struct base *base = &game_bases[b], saved = *base;
    int growth = *game_base_growth;
    uint32_t state = *game_state;
    if (!growth_may_kill()) check_base_growth();
    int full = cost_factor(base->faction, 0, b) * (base->pop_size + 1);
    base->nutrients = full + slice % 3;
    check_base_growth();
    *game_base_growth = -3 - slice % 2;
    check_base_growth();
    *game_base_growth = 6;
    base->nutrients = slice % 5;
    base->surplus[RESOURCE_NUTRIENT] = game_rules_basic->nutrient_intake_citizen + slice % 2;
    check_base_growth();
    *game_base_growth = growth;
    if (base->pop_size > 1) {
        base->nutrients = -1 - slice % 4;
        if (slice & 1) *game_state |= GAME_STATE_DONE;
        check_base_growth();
        *game_state = state;
    }
    // Stores that run out within five turns: the advice, unless the
    // governor places citizens.
    base->nutrients = 2 + slice % 3;
    base->surplus[RESOURCE_NUTRIENT] = -1 - (b + slice) % 3;
    base->specialist_total = (b + slice) % 4;
    if (slice % 4 == 3) base->governor |= GOVERNOR_ACTIVE | GOVERNOR_CITIZENS;
    check_base_growth();
    *base = saved;
}

// The base's queue as it is, and then made up: facilities, secret
// projects and units, basic and the faction's own, up to a full queue.
static void sweep_queue(int b) {
    struct base *base = &game_bases[b], saved = *base;
    struct faction_data *f = &game_factions[base->faction];
    uint8_t queued[sizeof(f->units_queue)];
    memcpy(queued, f->units_queue, sizeof(queued));
    check_base_queue(b);
    // Short of full: the original reads past a full queue's end, and takes
    // the base's worked squares for a unit.
    base->queue_size = 1 + (b + slice) % 9;
    for (int i = 0; i <= base->queue_size && i < 10; i++) {
        int k = b * 7 + slice * 3 + i * 11;
        switch (k % 4) {
        case 0: base->queue[i] = -(1 + k % (FAC_FIRST_PROJECT - 1)); break;
        case 1: base->queue[i] = -(FAC_FIRST_PROJECT + k % SP_COUNT); break;
        case 2: base->queue[i] = k % 64; break;
        default: base->queue[i] = base->faction * 64 + k % 64; break;
        }
    }
    check_base_queue(b);
    *base = saved;
    memcpy(f->units_queue, queued, sizeof(queued));
}

// Every facility, with and without the queue; and the totals as if each
// base were the one being worked on, from what its fields hold now.
static void sweep_bases(void) {
    struct base *current = *game_current_base;
    int current_id = *game_current_base_id;
    for (int b = 0; b < *game_base_count; b++) {
        for (int fac = -1; fac <= FAC_FIRST_PROJECT + 1; fac++)
            for (int q = 0; q <= 10; q += 5) check_has_fac(fac, b, q);
        struct base *base = &game_bases[b], saved = *base;
        *game_current_base = base;
        *game_current_base_id = b;
        check_base_compute(1);
        check_base_compute(0);
        check_farm_compute();
        check_base_yield();
        check_base_support();
        check_base_nutrient();
        check_base_minerals();
        check_base_energy();
        for (int e = -1; e <= 40; e += 7) check_black_market(e);
        // Early on no base makes enough minerals to do damage, so also
        // with more minerals, and damage left from last turn.
        base->intake_2[RESOURCE_MINERAL] = 20 + (b * 7 + slice * 3) % 60;
        base->eco_damage = (b * 13 + slice * 5) % 300;
        check_base_minerals();
        // The squares chosen afresh from others worked.
        uint32_t tiles = base->worked_tiles;
        base->worked_tiles = (uint32_t)(b * 0x9e3779b9u + slice * 0x85ebca6bu) & 0x1fffff;
        check_base_yield();
        base->worked_tiles = 0;
        check_base_yield();
        base->worked_tiles = tiles;
        // And energy as at the end of a turn, with more of it, some
        // specialists and another allocation.
        struct faction_data *f = &game_factions[base->faction], saved_f = *f;
        int upkeep_state = *game_base_upkeep_state;
        *game_base_upkeep_state = 1;
        base->intake_2[RESOURCE_ENERGY] += (b * 5 + slice * 11) % 40;
        base->specialist_total = (b + slice) % 4;
        base->specialist_types[0] = (uint32_t)(b * 0x9e3779b9u + slice);
        f->alloc_psych = (b + slice) % 4;
        f->alloc_labs = (b * 3 + slice) % (11 - f->alloc_psych);
        check_base_support();
        check_base_energy();
        check_base_psych();
        *game_base_upkeep_state = upkeep_state;
        *f = saved_f;
        *base = saved;
        sweep_growth(b);
        sweep_queue(b);
    }
    *game_current_base = current;
    *game_current_base_id = current_id;
    for (int b = -1; b < *game_base_count; b++) {
        if (b >= 0) check_pop_goal(b), check_pop_goal_fac(b);
        for (int type = 0; type <= 2; type++) check_cost_factor(game_bases[b < 0 ? 0 : b].faction, type, b);
    }
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int type = 0; type <= 2; type++) check_cost_factor(f, type, -1);
        int content, limit;
        check_psych_check(f, &content, &limit);
        for (int r = 0; r <= *game_base_count + 1; r++) check_base_rank(f, r);
    }
}

static void base_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    sweep_yields();
    sweep_bases();
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook base_hooks[] = {
    HOOK(0x4e6e50, crop_yield, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                    // sub esp, 8
    HOOK(0x4e7310, mine_yield, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xf0, 0xfa, 0x68, 0x00),        // mov eax, [0x68faf0]
    HOOK(0x4e7750, energy_yield, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),                  // sub esp, 24
    HOOK(0x4e7dc0, resource_yield, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x421670, has_fac, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x56, 0x57),                       // push ecx; push esi; push edi
    HOOK(0x4ef090, pop_goal, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                      // sub esp, 8
    HOOK(0x4eef80, pop_goal_fac, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x08),            // push ecx; mov eax, [ebp+8]
    HOOK(0x4e4430, cost_factor, PUSH_EBP_MOV_EBP_ESP, 0x53, 0xa1, 0xe8, 0x64, 0x9a, 0x00), // push ebx; mov eax, [0x9a64e8]
    HOOK(0x4ec3b0, base_compute, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                  // mov eax, [ebp+8]
    HOOK(0x4e68b0, farm_compute, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),                  // sub esp, 28
    HOOK(0x4e80b0, base_yield, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x58),                    // sub esp, 88
    HOOK(0x4e9550, base_support, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x3c),                  // sub esp, 60
    HOOK(0x4e9b70, base_nutrient, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                 // sub esp, 8
    HOOK(0x4e9cb0, base_minerals, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),                 // sub esp, 16
    HOOK(0x4ea1f0, black_market, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),                  // sub esp, 28
    HOOK(0x4ea4a0, psych_check, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),                   // mov ecx, [ebp+8]
    HOOK(0x4eb490, base_rank, PUSH_EBP_MOV_EBP_ESP, 0xb8, 0x00, 0x10, 0x00, 0x00),         // mov eax, 0x1000
    HOOK(0x4ea540, base_psych, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x48),                    // sub esp, 72
    HOOK(0x4eb560, base_energy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),                   // sub esp, 40
    HOOK(0x4f06e0, base_queue, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),             // push ebx; mov ebx, [ebp+8]
    HOOK(0x4ef1c0, base_growth, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),                   // sub esp, 40
};
struct hook_module base_module = {"base", base_hooks, HOOK_COUNT(base_hooks), base_sweep, NULL, -1};
