// Technology: whether a faction has or can research a tech, a tech's level,
// category and prerequisites, its name, its cost and its value to the AI, and
// the bonuses techs give; which facilities and terraforming a faction may
// use, and the AI's choice of research. Reimplemented from terranx.exe 2.0, with behaviour
// as documented by OpenSMACX (Brendan Casey, GPL-3.0-or-later), whose names
// we use, and checked against the original code where the two differ.
#include "tech.h"

#include <stdio.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "social.h"
#include "spot.h"
#include "veh.h"

static const struct rules_tech *tech_entry(uint32_t tech) { return &game_tech[tech]; }

// Whether `faction` has `tech`. "No tech" is always had; disabled techs, and
// Transcendent Thought, never are. Faction 0 (the natives) has nothing.
int has_tech(int tech, int faction) {
    if (faction <= 0) return 0;
    if (tech == TECH_NONE) return 1;
    if (tech < 0 || tech >= TECH_TRANSCEND) return 0;
    const struct rules_tech *t = tech_entry(tech);
    if (t->preq1 == TECH_DISABLED || (t->preq2 == TECH_DISABLED && t->preq1 != TECH_NONE)) return 0;
    // As on x86, the shift count wraps at 32.
    return (game_tech_achieved[tech] & (1u << (faction & 31))) != 0;
}

// The tech's level: `level` plus the length of its longest prerequisite chain.
int tech_recurse(int tech, int level) {
    if (tech < 0 || tech >= TECH_COUNT) return level;
    const struct rules_tech *t = tech_entry(tech);
    int a = tech_recurse(t->preq1, level + 1), b = tech_recurse(t->preq2, level + 1);
    return a > b ? a : b;
}

// The tech's main purpose: growth (0), tech (1), wealth (2) or power (3).
// Ties go to the earlier one. The original does not check the range.
int tech_category(uint32_t tech) {
    const struct rules_tech *t = tech_entry(tech);
    int category = 0, best = t->growth;
    if (t->tech > best) category = 1, best = t->tech;
    if (t->wealth > best) category = 2, best = t->wealth;
    return t->power > best ? 3 : category;
}

// Whether `faction` can research `tech` now: it doesn't have it, it has both
// prerequisites, and the tech exists in the game being played.
int tech_avail(uint32_t tech, int faction) {
    if (has_tech((int)tech, faction) || tech >= TECH_COUNT) return 0;
    if (!*game_expansion && tech >= TECH_FIRST_SMACX && tech <= TECH_LAST_SMACX) return 0;
    const struct rules_tech *t = tech_entry(tech);
    if (t->preq1 < TECH_NONE || t->preq2 < TECH_NONE) return 0;
    return has_tech(t->preq1, faction) && has_tech(t->preq2, faction);
}

// Whether `preq` is `parent` or one of its prerequisites, at most `range`
// steps back.
int tech_is_preq(int preq, int parent, uint32_t range) {
    if (preq < 0 || parent < 0) return 0;
    if (preq == parent) return 1;
    if (!range) return 0;
    const struct rules_tech *t = tech_entry(parent);
    return tech_is_preq(preq, t->preq1, range - 1) || tech_is_preq(preq, t->preq2, range - 1);
}

// The tech's value for each purpose. Past the end of the table tech_mil gives
// 0 and the others 1, as in the original. The original compares signed, so a
// negative tech reads whatever lies before the table; the game never asks.
int tech_mil(int tech) { return tech >= TECH_COUNT ? 0 : game_tech[tech].power; }
int tech_tech(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].tech; }
int tech_infra(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].wealth; }
int tech_colonize(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].growth; }

// Whether `faction` may jump straight to `tech` without its prerequisites:
// it must have their prerequisites, the tech must not unlock a reactor, and
// a weapon tech is only for the two weakest factions, and not far ahead of
// what they can build.
int valid_tech_leap(int tech, int faction) {
    const struct rules_tech *t = tech_entry(tech);
    for (int i = 0; i < 2; i++) {
        int preq = i ? t->preq2 : t->preq1;
        if (preq >= 0 && !(has_tech(game_tech[preq].preq1, faction) && has_tech(game_tech[preq].preq2, faction)))
            return 0;
    }
    if (t->preq1 <= TECH_DISABLED || t->preq2 <= TECH_DISABLED) return 0;
    for (int i = 0; i < REACTOR_COUNT; i++)
        if (tech == game_reactors[i].preq_tech) return 0;
    // Only the first weapon with this tech counts.
    for (int i = 0; i < WEAPON_COUNT; i++)
        if (game_weapons[i].preq_tech == tech)
            return game_factions[faction].ranking <= 2 &&
                   game_weapons[i].offense <= weapon_budget(faction, 99, 0) + 2;
    return 1;
}

// Names: a tech, or one of the other things the game trades and values like
// techs (9999 is the world map, 89-96 a faction's comm frequency, 97 and up
// a prototype). Appends to `out`; with `category`, adds the tech's category
// letter and level, as in "Biogenetics (E2)".
void say_tech(char *out, int tech, int category) {
    if (tech < TECH_NONE) {
        strcat(out, game_label(310));  // "Not Available"
    } else if (tech < 0) {
        strcat(out, game_label(25));  // "NONE"
    } else if (tech == 9999) {
        strcat(out, game_label(306));  // "World Map"
    } else if (tech < TECH_COUNT) {
        strcat(out, game_strings_get(game_strings, game_tech[tech].name));
        if (category) {
            char level[16];
            strcat(out, " ");
            strcat(out, "(");
            strcat(out, game_label(629 + tech_category(tech)));
            snprintf(level, sizeof(level), "%d", tech_recurse(tech, 0));
            strcat(out, level);
            strcat(out, ")");
        }
    } else if (tech < TECH_COUNT + FACTION_COUNT) {
        const struct faction_def *f = &game_faction_defs[tech - TECH_COUNT];
        if (!*game_language) {
            strcat(out, f->adjective);
            strcat(out, " ");
            strcat(out, game_label(487));  // "Comm Frequency"
        } else {
            strcat(out, game_label(487));
            strcat(out, " ");
            strcat(out, "(");
            *game_parse_gender = f->noun_gender;  // for the translation's grammar
            *game_parse_plural = f->noun_plural;
            strcat(out, f->noun);
            strcat(out, ")");
        }
    } else {
        strcat(out, game_protos[tech - TECH_COUNT - FACTION_COUNT].name);
        strcat(out, " ");
        strcat(out, game_label(185));  // "Prototype"
    }
}

// say_tech() into the game's own buffer, which it returns.
char *tech_name(int tech, int category) {
    game_tech_name_buffer[0] = 0;
    say_tech(game_tech_name_buffer, tech, category);
    return game_tech_name_buffer;
}

// Recompute a faction's bonuses from its techs: commerce, and extra output
// from fungus squares. Faction-file bonuses add to the fungus ones, which
// cannot go below zero; a strong economy adds commerce.
void tech_effects(int faction) {
    struct faction_data *fd = &game_factions[faction];
    const struct faction_def *def = &game_faction_defs[faction];
    fd->tech_commerce_bonus = def->rule_commerce;
    for (int i = 0; i < FUNGUS_COUNT; i++) fd->tech_fungus[i] = 0;
    for (int t = 0; t < TECH_COUNT; t++) {
        if (!has_tech(t, faction)) continue;
        uint32_t flags = game_tech[t].flags;
        if (flags & TECH_FLAG_FUNGUS_ENERGY) fd->tech_fungus[FUNGUS_ENERGY]++;
        if (flags & TECH_FLAG_FUNGUS_MINERALS) fd->tech_fungus[FUNGUS_MINERALS]++;
        if (flags & TECH_FLAG_FUNGUS_NUTRIENT) fd->tech_fungus[FUNGUS_NUTRIENT]++;
        if (flags & TECH_FLAG_COMMERCE) fd->tech_commerce_bonus++;
    }
    for (int i = 0; i < def->bonus_count; i++) {
        int id = def->bonus_id[i], value = def->bonus_val1[i];
        if (id == BONUS_FUNGUS_NUTRIENT) fd->tech_fungus[FUNGUS_NUTRIENT] += value;
        if (id == BONUS_FUNGUS_MINERALS) fd->tech_fungus[FUNGUS_MINERALS] += value;
        if (id == BONUS_FUNGUS_ENERGY) fd->tech_fungus[FUNGUS_ENERGY] += value;
    }
    for (int i = 0; i < FUNGUS_COUNT; i++)
        if (fd->tech_fungus[i] < 0) fd->tech_fungus[i] = 0;
    int economy = fd->se_pending[SE_ECONOMY];
    fd->tech_commerce_bonus += (economy > 2) + (economy > 3) + (economy > 4);
}

// How much research the next tech costs `faction`, unless already set. It
// grows with the techs the faction has, eases for factions behind the
// leader, and depends on difficulty, the map size and the game's rules.
int tech_rate(int faction) {
    const struct faction_data *fd = &game_factions[faction];
    if (fd->tech_cost >= 0) return fd->tech_cost;
    if (!*game_tech_discovery_rate) return 999999999;
    int own = game_range(fd->earned_techs_saved * 2 - fd->unk_360 + fd->tech_ranking, 2, 9999);
    int top = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        int v = game_factions[i].earned_techs_saved * 2 + game_factions[i].tech_ranking;
        if (v > top) top = v;
    }
    own /= 2;
    top /= 2;
    int human = game_is_human(faction);
    int diff = human ? fd->diff_level : *game_difficulty;
    diff += diff < 3;
    int diff_lvl = human ? 3 : *game_difficulty;
    diff = human ? diff * 4 + 8 : 29 - diff * 3;
    diff = game_range(diff, 12 - own, own + 12);
    int stagnation = *game_rules & RULES_TECH_STAGNATION, rule = stagnation | 0x40;  // 64, or 96
    int base = diff + game_range(own - *game_turn / (rule >> 3), 0, (diff * (rule >> 5)) >> 1);
    int ease = game_range((top - diff_lvl - own + 7) / (8 - diff_lvl), 0, base * diff_lvl / 10 + 1);
    int rate = (base - ease) * game_range(own - game_range(fd->se_base[SE_RESEARCH], -1, 1), 1, 99999);
    if (*game_tech_discovery_rate != 100) rate = rate * 100 / *game_tech_discovery_rate;
    int techcost = game_faction_defs[faction].rule_techcost;
    if (techcost != 100) rate = rate * techcost / 100;
    int cost = rate * *game_map_area_sqrt / 56;
    if (stagnation) cost += cost / 2;
    return game_range(cost, 1, 99999999);
}

// How much `faction` wants `tech`, or another tradeable (see say_tech). The
// AI researches, trades and steals by this. `simple` skips the strategic
// part, for quick comparisons.
int tech_val(int tech, int faction, int simple) {
    if (tech == 9999) return 2;
    const struct faction_data *fd = &game_factions[faction];
    int vendettas = 0;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && game_has_treaty(faction, i, TREATY_VENDETTA)) vendettas++;
    int weight = simple ? 1 : (*game_rules & RULES_BLIND_RESEARCH) ? 4 : 2;

    if (tech >= TECH_COUNT && tech < TECH_COUNT + FACTION_COUNT) {  // a comm frequency
        int other = tech - TECH_COUNT, factor = 1;
        if (!game_wants_to_attack(faction, other, 0)) factor = 2;
        if (!game_wants_to_attack(other, faction, 0)) factor++;
        return factor / (fd->ai_fight + 2) * factor;
    }
    if (tech >= TECH_COUNT + FACTION_COUNT) {  // a prototype
        const struct unit_proto *u = &game_protos[tech - TECH_COUNT - FACTION_COUNT];
        return game_range(game_weapons[u->weapon].offense, 1, 2) + game_range(game_armor[u->armor].defense, 1, 2) +
               game_range(game_chassis[u->chassis].speed, 1, 2) + u->reactor - 2;
    }

    const struct rules_tech *t = tech_entry(tech);
    int growth = t->growth, techv = t->tech, wealth = t->wealth, power = t->power;
    int value = growth * (weight * fd->ai_growth + 1) + wealth * (weight * fd->ai_wealth + 1) +
                techv * (weight * fd->ai_tech + 1) + power * (weight * fd->ai_power + 1);
    // Halved when it serves none of the faction's priorities.
    if ((!power || (!fd->ai_power && !vendettas)) && (!techv || !fd->ai_tech) && (!wealth || !fd->ai_wealth) &&
        (!growth || (!fd->ai_growth && fd->base_count >= 4)))
        value = (value + 1) / 2;
    int human = game_is_human(faction);
    if (!human && !has_tech(tech, faction) && simple) {
        // The AI favours techs others already have, and those not far
        // below its most advanced.
        int achieved = game_bit_count(game_tech_achieved[tech]);
        if (achieved > 1) value += 2 - 2 * achieved;
        int top = 1;
        for (int i = 0; i < TECH_COUNT; i++)
            if (has_tech(i, faction)) {
                int level = tech_recurse(i, 0);
                if (level > top) top = level;
            }
        int level = tech_recurse(tech, 0);
        if (level < top) value = value * (level + 1) / (top + 1);
        if (value < 1) value = 1;
    }
    if (simple) return value;

    int bases = fd->base_count;
    if (bases) {
        for (int r = 1; r < LAND_REGION_COUNT; r++) {
            if (game_bad_region(r)) continue;
            int strength = fd->region_total_bases[r] * power, plan = fd->region_base_plan[r];
            if (plan == PLAN_NAVAL_TRANSPORT && vendettas && !human) {
                value += strength / bases;
            } else if (plan == PLAN_DEFENSIVE) {
                value += strength * 4 / (bases * (human + 1));
            } else if (plan == PLAN_OFFENSIVE) {
                int factor = fd->best_weapon_value >= fd->enemy_best_weapon_value ? 2 : 4;
                value += strength * factor / (bases * (human + 1));
            } else {
                for (int i = 1; i < FACTION_COUNT; i++)
                    if (i != faction && game_factions[i].region_total_bases[r] && fd->region_total_bases[r] &&
                        game_has_treaty(faction, i, TREATY_COMMLINK) &&
                        (!game_has_treaty(faction, i, TREATY_PACT | TREATY_TREATY) ||
                         game_has_treaty(faction, i, TREATY_WANT_REVENGE)))
                        value += strength / (bases * (human + 1));
            }
        }
    }
    if (has_tech(tech, faction)) return value;

    const struct faction_def *def = &game_faction_defs[faction];
    const struct rules_facility *fac = game_facilities;
    if (game_climactic_battle() && tech_is_preq(tech, fac[FAC_ASCENT].preq_tech, 2)) value *= 4;
    if (fd->se_base[SE_PLANET] > 0 && fd->ai_growth) {
        if (tech_is_preq(tech, TECH_CENTMED, 9999)) value *= 3;
        if (tech_is_preq(tech, TECH_PLAECON, 9999)) value *= 2;
        if (tech_is_preq(tech, TECH_ALPHCEN, 3)) value *= 2;
    }
    if (fd->se_base[SE_PROBE] <= 0 && tech_is_preq(tech, fac[FAC_HUNTER_SEEKER].preq_tech, fd->ai_tech + 2)) {
        if (!fd->ai_power) value *= 2;
        if (fd->ai_tech) value *= 2;
    }
    if (fd->ai_growth && tech_is_preq(tech, TECH_DOCINIT, 2)) value *= 2;
    if ((fd->ai_wealth || !*game_cloud_cover) && tech_is_preq(tech, TECH_ENVECON, 9999)) value *= 2;
    if ((t->flags & TECH_FLAG_SECRETS) && !game_tech_achieved[tech] && !(*game_rules & RULES_BLIND_RESEARCH))
        value *= (fd->ai_power + 1) * 2;
    if (def->rule_psi > 0) {
        if (tech_is_preq(tech, fac[FAC_DREAM_TWISTER].preq_tech, 9999)) value *= 2;
    } else {
        int fusion = game_reactors[REACTOR_FUSION - 1].preq_tech;
        if (tech == fusion) value *= 2;
        if (tech == game_reactors[REACTOR_QUANTUM - 1].preq_tech) value *= 2;
        if (tech_is_preq(tech, fusion, 9999)) value++;
        if (tech_is_preq(tech, fusion, 1) && !(*game_rules & RULES_BLIND_RESEARCH)) value *= 2;
    }
    int damage = fd->eco_damage_total / game_range(bases, 1, 9999);
    if (damage > fd->ai_power &&
        (tech_is_preq(tech, fac[FAC_HYBRID_FOREST].preq_tech, 9999) ||
         tech_is_preq(tech, fac[FAC_TREE_FARM].preq_tech, 9999) ||
         tech_is_preq(tech, fac[FAC_CENTAURI_PRESERVE].preq_tech, 9999) ||
         tech_is_preq(tech, fac[FAC_TEMPLE_OF_PLANET].preq_tech, 9999)))
        value += damage;
    if (def->rule_population > 0) {
        if (tech_is_preq(tech, fac[FAC_HAB_COMPLEX].preq_tech, 9999)) value *= 2;
        else if (tech_is_preq(tech, fac[FAC_HABITATION_DOME].preq_tech, 9999) && *game_turn > 250)
            value = value * 3 / 2;
    }
    if (fd->ai_power) {
        for (int i = 0; i < WEAPON_COUNT; i++) {
            if (!game_weapons[i].offense) continue;
            int preq = game_weapons[i].preq_tech;
            if (tech == preq) value *= human + 3;
            else if (tech_is_preq(tech, preq, 1)) value *= human + 2;
        }
    }
    if (fd->ai_tech || !fd->ai_power) {
        for (int i = 0; i < TECH_COUNT; i++)
            if (!has_tech(i, faction) && (game_tech[i].flags & TECH_FLAG_SECRETS) && !game_tech_achieved[i] &&
                tech_is_preq(tech, i, 1))
                value *= 3;
    }
    int formers = game_protos[PROTO_FORMERS].preq_tech;
    if (tech_is_preq(tech, formers, 9999) && !has_tech(formers, faction)) {
        value *= 2;
        if (human) value *= 2;
    }
    int foil = game_chassis[CHASSIS_FOIL].preq_tech;
    if (tech_is_preq(tech, foil, 9999) && !has_tech(foil, faction)) {
        // Sea transport matters to a faction that has explored all of a
        // continent it shares with no one.
        int search = 0;
        for (int r = 1; r < LAND_REGION_COUNT; r++) {
            if (!fd->region_total_bases[r]) continue;
            for (int i = 1; i < FACTION_COUNT; i++)
                if (i != faction && !game_factions[i].region_total_bases[r]) {
                    search = 1;
                    break;
                }
            if (search && fd->region_visible_tiles[r] >= game_continents[r].tile_count) {
                value *= 3;
                if (human) value *= 2;
                break;
            }
        }
        if (search) value = value * 2 + 4;
    }
    return value;
}

// ---- Facilities, terraforming and the AI's research ---------------------------

// Facility ids and secret project indices used below.
enum {
    FA_PERIMETER_DEFENSE = 4, FA_TACHYON_FIELD = 5, FA_SKUNKWORKS = 10, FA_COMMAND_CENTER = 27, FA_NAVAL_YARD = 28,
    FA_BIOENHANCEMENT = 30, FA_COVERT_OPS = 34, FA_FLECHETTE_DEFENSE = 39, FA_SUBSPACE_GENERATOR = 40,
    FA_VOICE_OF_PLANET = 101, FA_MANIFOLD_HARMONICS = 103, FA_PLANETARY_ENERGY_GRID = 106,
    SPA_COMMAND_NEXUS = 1, SPA_WEATHER_PARADIGM = 2, SPA_CITIZENS_DEFENSE_FORCE = 5, SPA_MARITIME_CONTROL_CENTER = 10,
    SPA_CYBORG_FACTORY = 17,
};
// Scenario rules: no secret projects; no raising or lowering land.
#define RULES_SCN_NO_BUILDING_SP 0x80000000u
#define RULES_SCN_NO_TERRAFORMING 0x800000u
enum { RULE_FLAG_FREE_PROTO = 0x200 };  // faction_def.rule_flags: prototypes cost nothing
// The terraforming the Weather Paradigm opens up: condenser to level terrain.
enum { TERRA_CONDENSER = 12, TERRA_RAISE_LAND = 16, TERRA_LOWER_LAND = 17, TERRA_LEVEL_TERRAIN = 18 };

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }

// The check facility_avail() and terrain_avail() make that `faction` (above
// zero) has a tech: has_tech() written out, except that any prerequisite
// below TECH_NONE, not just TECH_DISABLED, counts as disabled.
static int tech_known(int tech, int faction) {
    if (tech == TECH_NONE) return 1;
    if (tech < 0 || tech >= TECH_TRANSCEND) return 0;
    const struct rules_tech *t = tech_entry((uint32_t)tech);
    if (t->preq1 < TECH_NONE || (t->preq2 < TECH_NONE && t->preq1 != TECH_NONE)) return 0;
    return (game_tech_achieved[tech] & (1u << (faction & 31))) != 0;
}

// Whether a secret project the faction holds does the facility's job:
// Maritime Control Center for the Naval Yard, Citizens' Defense Force for
// Perimeter Defense, Command Nexus for the Command Center, Cyborg Factory
// for the Bioenhancement Center, Singularity Inductor for the Quantum
// Converter, Cloudbase Academy for the Aerospace Complex and Planetary
// Energy Grid for the Energy Bank.
int redundant(int fac, int faction) {
    int sp;
    switch (fac) {
    case FA_NAVAL_YARD: sp = SPA_MARITIME_CONTROL_CENTER; break;
    case FA_PERIMETER_DEFENSE: sp = SPA_CITIZENS_DEFENSE_FORCE; break;
    case FA_COMMAND_CENTER: sp = SPA_COMMAND_NEXUS; break;
    case FA_BIOENHANCEMENT: sp = SPA_CYBORG_FACTORY; break;
    case FAC_QUANTUM_CONVERTER: sp = SP_SINGULARITY_INDUCTOR; break;
    case FAC_AEROSPACE_COMPLEX: sp = SP_CLOUDBASE_ACADEMY; break;
    case FAC_ENERGY_BANK: sp = SP_PLANETARY_ENERGY_GRID; break;
    default: return 0;
    }
    return has_project(sp, faction);
}

// Whether `faction` may build facility (or secret project) `fac`; at
// `base`, if not negative, it must not be built there already nor be
// among the first `queue` items of the production queue. Unless it answers
// before, it leaves the faction's file name in capitals in
// game_battle_text, as the original does.
//
// Unlike OpenSMACX: Ascent to Transcendence needs only the Voice of Planet
// built (even if since lost) and the transcendence victory: no tech, any
// faction, Caretakers too. A Paradise Garden doesn't rule out a Punishment
// Sphere.
int facility_avail(int fac, int faction, int base, int queue) {
    if (!fac) return 0;
    if (fac == FA_SKUNKWORKS) {
        if (*game_difficulty <= 1) return 0;
    } else {
        if (fac >= FAC_FIRST_PROJECT && (*game_rules & RULES_SCN_NO_BUILDING_SP)) return 0;
        if (fac == FAC_ASCENT)
            return game_secret_projects[SP_VOICE_OF_PLANET] != -1 && (*game_rules & RULES_VICTORY_TRANSCENDENCE);
    }
    if (faction <= 0) return 0;
    if (!tech_known(game_facilities[fac].preq_tech, faction)) return 0;
    if (!*game_expansion && fac >= FA_MANIFOLD_HARMONICS && fac <= FA_PLANETARY_ENERGY_GRID) return 0;
    const struct faction_def *def = &game_faction_defs[faction];
    game_battle_text[0] = 0;
    strcat(game_battle_text, def->filename);
    (*game_char_upper)(game_battle_text);
    // The Caretakers can't build the Voice of Planet. The original names
    // Ascent here too, which never gets this far.
    if (!strcmp(game_battle_text, text(0x6916e4)) && fac == FA_VOICE_OF_PLANET) return 0;
    if (fac >= FAC_FIRST_PROJECT) return game_secret_projects[fac - FAC_FIRST_PROJECT] == -1;
    if (base < 0) return 1;
    if (has_fac(fac, base, queue)) return 0;  // nothing in orbit counts
    if (redundant(fac, faction)) return 0;
    const struct base *B = &game_bases[base];
    if (fac == FA_NAVAL_YARD && !is_coast(B->x, B->y, 0)) return 0;
    if (fac == FAC_AQUAFARM || fac == FAC_SUBSEA_TRUNKLINE || fac == FAC_THERMOCLINE_TRANSDUCER)
        if (!*game_expansion || !is_coast(B->x, B->y, 0)) return 0;
    if (!*game_expansion && (fac == FAC_GEOSYNC_SURVEY_POD || fac == FA_COVERT_OPS || fac == FAC_BROOD_PIT ||
                             fac == FA_FLECHETTE_DEFENSE || fac == FA_SUBSPACE_GENERATOR))
        return 0;
    if (fac == FA_SUBSPACE_GENERATOR && !(def->rule_flags & RULE_FLAG_ALIEN)) return 0;
    switch (fac) {
    case FAC_RECYCLING_TANKS: return !has_fac(FAC_PRESSURE_DOME, base, queue);
    case FA_TACHYON_FIELD:
        return has_fac(FA_PERIMETER_DEFENSE, base, queue) || has_project(SPA_CITIZENS_DEFENSE_FORCE, faction);
    case FAC_HOLOGRAM_THEATRE:
        return has_fac(FAC_RECREATION_COMMONS, base, queue) && !has_project(SP_VIRTUAL_WORLD, faction);
    case FAC_HYBRID_FOREST: return has_fac(FAC_TREE_FARM, base, queue);
    case FAC_QUANTUM_LAB: return has_fac(FAC_FUSION_LAB, base, queue);
    case FAC_NANOHOSPITAL: return has_fac(FAC_RESEARCH_HOSPITAL, base, queue);
    case FA_SKUNKWORKS: return !(def->rule_flags & RULE_FLAG_FREE_PROTO);
    case FAC_PUNISHMENT_SPHERE: return !has_fac(FAC_PARADISE_GARDEN, base, queue);
    case FAC_HABITATION_DOME: return has_fac(FAC_HAB_COMPLEX, base, queue);
    }
    // In orbit, and the Geosync Survey Pod: an Aerospace Complex, or a
    // project that serves as one.
    if ((fac >= FAC_SKY_HYDRO_LAB && fac <= FAC_ORBITAL_DEFENSE_POD) || fac == FAC_GEOSYNC_SURVEY_POD)
        if (!has_fac(FAC_AEROSPACE_COMPLEX, base, queue) && !has_project(SP_CLOUDBASE_ACADEMY, faction) &&
            !has_project(SP_SPACE_ELEVATOR, faction))
            return 0;
    if (fac == FAC_NANOREPLICATOR)
        return has_fac(FAC_ROBOTIC_ASSEMBLY_PLANT, base, queue) || has_fac(FAC_GENEJACK_FACTORY, base, queue);
    if (fac == FAC_QUANTUM_CONVERTER) return has_fac(FAC_ROBOTIC_ASSEMBLY_PLANT, base, queue);
    if (fac == FAC_TEMPLE_OF_PLANET) return has_fac(FAC_CENTAURI_PRESERVE, base, queue);
    return 1;
}

// Whether `faction` may carry out terraforming action `terraform`, on land
// or (`sea` 1) at sea. The Weather Paradigm opens up the condenser to level
// terrain without their techs; the scenario rules can rule out raising and
// lowering land.
int terrain_avail(int terraform, int sea, int faction) {
    int preq = (&game_terraform[terraform].preq_tech)[sea];
    if (preq < TECH_NONE) return 0;
    if ((terraform == TERRA_RAISE_LAND || terraform == TERRA_LOWER_LAND) && (*game_rules & RULES_SCN_NO_TERRAFORMING))
        return 0;
    if (terraform >= TERRA_CONDENSER && terraform <= TERRA_LEVEL_TERRAIN && has_project(SPA_WEATHER_PARADIGM, faction))
        return 1;
    if (faction <= 0) return 0;
    return tech_known(preq, faction);
}

// The message that a faction has discovered a tech that wins a free one:
// script.txt's THESECRET0 for the player's own faction, THESECRET1 for
// another, with the tech and the faction filled in.
void tech_secret(int tech, int faction) {
    game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
    game_parse_says(1, game_faction_defs[faction].adjective, -1, -1);
    game_battle_text[0] = 0;
    strcat(game_battle_text, text(0x6916f0));
    char number[16];
    snprintf(number, sizeof(number), "%d", faction != *game_current_faction);
    strcat(game_battle_text, number);
    game_x_pop(game_battle_text, 0);
}

// The tech an AI (or a human's governor) researches next: of those it can
// research, the one whose tech_val() gives the highest draw from zero up to
// it. With blind research the value is per level of the tech, so cheaper
// techs come first, and a human's faction that cares for growth or wealth
// takes the terraforming tech as soon as it comes up. A human's draw uses
// the game's own generator, the AI's the C runtime's.
int tech_ai(int faction) {
    int best = -999, pick = -1;
    int human = game_is_human(faction);
    const struct faction_data *fd = &game_factions[faction];
    for (int t = 0; t < TECH_COUNT; t++) {
        if (!tech_avail((uint32_t)t, faction)) continue;
        int value = tech_val(t, faction, 0);
        if (*game_rules & RULES_BLIND_RESEARCH) {
            if (human && (fd->ai_growth || fd->ai_wealth) && t == game_protos[PROTO_FORMERS].preq_tech) return t;
            const struct rules_tech *r = tech_entry((uint32_t)t);
            int a = tech_recurse(r->preq1, 1), b = tech_recurse(r->preq2, 1);
            value = (int)((uint32_t)value << 8) / (a > b ? a : b);
        }
        int draw = human ? game_random(0, value + 1) : game_rnd(value + 1);
        if (draw > best) best = draw, pick = t;
    }
    return pick;
}

// ---- tech_achieved ---------------------------------------------------------------
//
// A faction gains a tech (or, past the end of the tree, a map, a comm link
// or a prototype). The original keeps its New Tech popup, one of the game's
// big window objects, on its stack at [ebp-0x5400], where the help text for
// the tech records its address; ours is given the same place by an assembly
// entry, and the popup is built and taken down as the original does, on
// whatever path. The names follow Thinker's tech_achieved() (MIT), which was
// a map; where it differs from the original (it fixes some of its quirks)
// the original is followed.

enum {
    TA_MAPS = 9999,                          // the tech is the map of `from`
    TA_COMMLINKS = TECH_COUNT,               // TECH_COUNT + faction: a comm link with it
    TA_PROTOS = TECH_COUNT + FACTION_COUNT,  // then 0x59 + 8 + proto: a stock design is made over
    BONUS_FREE_FACILITY = 0xc, BONUS_FREE_ABILITY = 0xf,  // faction_def.bonus_id
    TECH_FLAG_REVEALS_MAP = 0x8,
    FACTION_FLAG_NEW_TECH = 0x2,       // a network game: designs to bring up to date
    FACTION_FLAG_SECRET_FIRST = 0x40,  // the first to learn a secret (its free tech is pending)
    FACTION_FLAG_MAP_SEEN = 0x200,     // a tech has shown the whole map
    PREF_AUTO_DESIGN = 0x4000,         // game_preferences: new designs are shown to the player
    PROTO_FLAG_PROTOTYPED_ = 0x4, PROTO_FLAG_NEW_DESIGN = 0x80, PROTO_FLAG_UPGRADE = 0x100,
    PLAN_AUTO_ = -1, PLAN_PROTOTYPE_DONE_ = -2,
    SP_PLANETARY_DATALINKS_ = 11,
    DATALINKS_FACTIONS = 3,  // a tech this many know is given to the Datalinks' holder
    FAC_BIOLOGY_LAB_ = 9, FORMER_FOREST_ = 4,
    TECH_SELF_AWARE = 34, TECH_MATTER_COMP = 57, TECH_SOCIAL_PSYCH = 4,  // the tutorial's notes
    RULE_FLAG_SHARE_TECH = 0x20,  // faction_def.rule_flags (with sharetech): spies on those it shares with
    DIPLO_INFILTRATOR = 0x1000,
    // Offsets in the popup object.
    TA_POP_ICON = 0x2144, TA_POP_SPRITE_BOX = 0x2240, TA_POP_SPRITE_SET = 0x2274, TA_POP_FIELD_A = 0x3104,
    TA_POP_FIELD_B = 0x3108,
};

// A string in the game's image, and the value of a pointer or integer in it.
static char *image_text(uint32_t addr) { return (char *)game_at(addr); }
static char *image_str(uint32_t addr) { return *(char **)game_at(addr); }
static int32_t image_int(uint32_t addr) { return *(int32_t *)game_at(addr); }

// A design propose_proto() made, for a stock design handed over or an old
// one with a better reactor: in a network game a human's is marked to be
// brought up to date later, else the faction's old designs are pruned and
// brought up to date at once. (Nothing keeps the original from doing this
// for a design number of -1.)
static void ta_new_design(int faction, int proto) {
    if (*game_net_multiplayer && game_is_human(faction)) {
        *((uint8_t *)game_protos + proto * (int)sizeof(struct unit_proto) + 0x31) |= 1;
    } else {
        prune_protos(faction, proto, 0);
        game_upgrade_any_prototypes(faction);
    }
}

// The map of another faction: what it has seen, and what it has seen of the
// items on each square, and the bases it knows of.
static void ta_map(int f, int from) {
    uint32_t bit_from = 1u << (from & 31), bit = 1u << (f & 31);
    game_factions[f].maps_traded |= game_factions[from].maps_traded | bit_from;
    struct map_tile *tile = *game_map;
    for (int i = 0; i < *game_map_area; i++, tile++) {
        if (!(tile->visibility & (uint8_t)bit_from)) continue;
        tile->visibility |= (uint8_t)bit;
        if (f && from) tile->bits_seen[f - 1] |= tile->bits & tile->bits_seen[from - 1];
    }
    for (int b = 0; b < *game_base_count; b++)
        if ((game_bases[b].visibility & (uint8_t)bit_from) || game_bases[b].faction == from) spot_base(b, f);
    if (f == *game_current_faction) {
        game_draw_map(1);
        *game_draw_state |= 4;
        game_graphic_win_redraw(game_world_win);
    }
}

// The reactor a tech unlocks: every design of the faction with a weaker one
// is made again with it. Returns the reactor, or 0 if no design was.
static int ta_reactors(int f, int tech) {
    uint8_t bit = (uint8_t)(1u << (f & 31));
    int upgraded = 0;
    for (int n = 2; n <= 4; n++) {
        if (tech != game_reactors[n - 1].preq_tech) continue;
        for (int j = 0; j < 64; j++) {
            int id = f * 64 + j;
            struct unit_proto *P = &game_protos[id];
            if (!(P->flags & PROTO_FLAG_ACTIVE) || (P->obsolete_factions & bit) || P->reactor >= n) continue;
            int plan = (P->flags & PROTO_FLAG_PROTOTYPED_) ? PLAN_PROTOTYPE_DONE_ : PLAN_AUTO_;
            int made = propose_proto(f, P->chassis, P->weapon, P->armor, P->abilities, n, plan, NULL);
            upgraded = n;
            if (made != id) ta_new_design(f, made);
        }
    }
    return upgraded;
}

// The New Tech popup (and, for the player, the window after it) for a
// faction that sees it: the player, or anyone when the game is shown in
// full. The popup's text and picture are the game's; the window that
// follows is held in a check (quiet.c), as are the sounds and messages.
static void ta_announce(uint8_t *pop, int f, int tech, int from, int steal) {
    char *text = game_battle_text, number[16];
    const struct faction_def *D = &game_faction_defs[f];
    int cur = *game_current_faction;
    text[0] = 0;
    say_tech(text, tech, 0);
    game_parse_says(0, text, -1, -1);
    *game_parse_gender = D->noun_gender, *game_parse_plural = D->noun_plural;
    game_parse_says(1, (char *)D->noun, -1, -1);
    if (from > 0) {
        const struct faction_def *S = &game_faction_defs[from];
        *game_parse_gender = S->noun_gender, *game_parse_plural = S->noun_plural;
        game_parse_says(2, (char *)S->noun, -1, -1);
    } else if (from < 0) {
        // -1: the Datalinks gave it; below: shared by a faction's file.
        if (from == -1) game_parse_say(2, image_int(0x9a5a98), -1, -1);
        else game_parse_says(2, game_label(215), -1, -1);
    }
    int flags = 0;
    if (f == cur) {
        text[0] = 0;
        strcat(text, image_text(0x69171c));
        snprintf(number, sizeof(number), "%d", tech);
        strcat(text, number);
        game_base_pop_init(pop, 0, 0);
        game_base_pop_set_width(pop, 500);
        game_popup_start(pop, image_str(0x691b04), text, -1, 0, 0x80, 0);
        game_base_pop_string(pop, image_text(0x691724));
        game_base_pop_string(pop, image_text(0x691728));
        game_base_pop_string(pop, image_text(0x691730));
        flags = 0x80;
    }
    text[0] = 0;
    strcat(text, image_text(f == cur ? 0x691734 : 0x691738));
    strcat(text, image_text(from ? 0x691740 : 0x691748));
    if (steal) {
        text[0] = 0;  // the text so far is dropped, as in the original
        strcat(text, image_text(0x691750));
    }
    // The popup's picture is the tech's icon, unless the sprite box says it
    // has one (the original reads that word of the popup as it is; a check
    // never shows the popup, so only its address, kept by the help text,
    // would tell two runs apart, and the assembly entry gives both the same).
    void *icon = game_tech_sprites + tech * 0x2c;
    *(void **)(pop + TA_POP_ICON) = *(int32_t *)(pop + TA_POP_SPRITE_SET) ? NULL : icon;
    game_sprite_box_sprite(pop + TA_POP_SPRITE_BOX, icon, 0, tech);
    *(int32_t *)(pop + TA_POP_FIELD_A) = 1, *(int32_t *)(pop + TA_POP_FIELD_B) = 2;
    game_popup_start(pop, game_pop_file, text, -1, 0, flags, 0);
    if (f == cur) game_help_tech_info_2(pop, tech, f);
    game_fx_play(game_fx, 0x33);
    if (f != cur) {
        if (!*game_net_multiplayer) game_base_pop_exec_3(pop, 0, 0);
    } else if (*game_net_multiplayer && (image_int(0x93fab4) || image_int(0x7492cc) || *game_council_session)) {
        // A network game with the talks or the Council open: a message
        // instead of the window.
        game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
        game_netmsg_pop(game_netmsg, image_text(0x691760), 5000, 0, NULL);
    } else {
        game_new_tech_win_exec(game_new_tech_win, tech, from);
    }
}

// The faction's free facilities and abilities from its file, whose tech it
// now has: a facility goes up in all its bases, an ability is only told of.
static void ta_free_bonuses(int f, int tech) {
    const struct faction_def *D = &game_faction_defs[f];
    int cur = *game_current_faction, any = 0;
    for (int i = 0; i < D->bonus_count; i++) {
        if (D->bonus_id[i] != BONUS_FREE_FACILITY) continue;
        int fac = D->bonus_val1[i];
        if (tech != game_facilities[fac].preq_tech) continue;
        any = 1;
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == f) set_fac(fac, b, 1);
        if (f == cur) {
            game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
            game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
            game_netmsg_pop(game_netmsg, image_text(0x691770), -5000, 0, NULL);
        }
    }
    if (any) game_net_daemon_synch(game_net_daemon, 0x14, 0, 0, 0, 0, 1, 0x2101);
    for (int i = 0; i < D->bonus_count; i++) {
        if (D->bonus_id[i] != BONUS_FREE_ABILITY) continue;
        int abl = D->bonus_val1[i];
        if (tech != game_abilities[abl].preq_tech || f != cur) continue;
        game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
        game_parse_say(1, (int)(intptr_t)game_abilities[abl].name, -1, -1);
        game_netmsg_pop(game_netmsg, image_text(0x69177c), -5000, 0, NULL);
    }
}

// The player's offer to look at the designs the tech brings: the stock
// designs it makes possible (but not a psi design or the Spore Launcher,
// design 15), then the faction's own newly flagged ones. Asked once, and the
// answer holds for all.
static void ta_designs(int f, int tech) {
    uint8_t bit = (uint8_t)(1u << (f & 31));
    int asked = 0;
    *game_base_proto_upkeep = 0;
    *(int32_t *)game_at(0x73396c) = 1;
    for (int i = 0; i < 64; i++) {
        const struct unit_proto *P = &game_protos[i];
        if (!(P->flags & PROTO_FLAG_ACTIVE) || tech != P->preq_tech) continue;
        if (game_weapons[P->weapon].offense < 0 || i == 15) continue;
        if (!asked) {
            if (!game_x_pop(image_text(0x691798), 0)) {
                asked = -1;
                continue;
            }
            asked = 1;
        } else if (asked < 0) {
            continue;
        }
        game_design_new_veh(-f, i);
    }
    for (int j = 0; j < 64; j++) {
        struct unit_proto *P = &game_protos[f * 64 + j];
        if (!(P->flags & PROTO_FLAG_NEW_DESIGN) || (P->obsolete_factions & bit)) continue;
        if (!asked) {
            if (!game_x_pop(image_text(0x6917a8), 0)) {
                asked = -1;
            } else {
                asked = 1;
                game_design_new_veh(-f, f * 64 + j);
            }
        } else if (asked > 0) {
            game_design_new_veh(-f, f * 64 + j);
        }
        P->flags &= (uint16_t)~PROTO_FLAG_NEW_DESIGN;
    }
    game_design_win_shutdown(game_design_win);
    for (int j = 0; j < 64; j++) {
        int id = f * 64 + j;
        const struct unit_proto *P = &game_protos[id];
        if ((P->obsolete_factions & bit) || !(P->flags & PROTO_FLAG_UPGRADE)) continue;
        if (*game_net_multiplayer && game_is_human(f)) continue;
        upgrade_prototypes(f, id);
    }
}

// A social model the tech opens: the player is asked whether to change the
// faction's society, and again for an answer of 2 (look at it first).
static void ta_social(int f, int tech) {
    for (int cat = 0; cat < 4; cat++) {
        for (int m = 0; m < 4; m++) {
            if (tech != game_social_preqs[cat * SOCIAL_PREQ_STRIDE + m] || !society_avail(cat, m, f)) continue;
            int choice;
            do {
                game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
                game_parse_say(1, game_social_preqs[cat * SOCIAL_PREQ_STRIDE + 4 + m], -1, -1);
                game_battle_text[0] = 0;
                strcat(game_battle_text, image_text(0x6917b8));
                choice = game_x_pop2(image_str(0x691b14), game_battle_text, 0);
                if (choice) game_social_select(f);
            } while (choice == 2);
        }
    }
}

// A tech that shows the whole map, the first time the faction learns one.
static void ta_reveal_map(int f, int tech) {
    uint8_t bit = (uint8_t)(1u << (f & 31));
    struct map_tile *tile = *game_map;
    for (int i = 0; i < *game_map_area; i++, tile++) {
        tile->visibility |= bit;
        if (f) tile->bits_seen[f - 1] = tile->bits;
    }
    for (int b = 0; b < *game_base_count; b++) {
        game_bases[b].visibility |= bit;
        game_bases[b].pop_seen[f] = game_bases[b].pop_size;
    }
    game_factions[f].flags |= FACTION_FLAG_MAP_SEEN;
    if (*game_turn && f == *game_current_faction) {
        game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
        game_popp(image_str(0x691b0c), image_text(0x6917d0), 0, image_text(0x6917c0), 0);
        game_draw_map(1);
        game_set_dirty();
        game_graphic_win_redraw(game_world_win);
    }
}

// The tutorial's notes, for the player.
static void ta_notes(int f, int tech) {
    if (tech == game_facilities[FAC_BIOLOGY_LAB_].preq_tech) game_interlude(1, NULL, 1, 0);
    if (tech == TECH_CENTMED) game_interlude(2, NULL, 1, 0);
    if (game_secret_projects[SP_VOICE_OF_PLANET] != -1 && f > 0 && has_tech(TECH_SELF_AWARE, f) &&
        has_tech(TECH_MATTER_COMP, f)) {
        game_interlude(0xf, NULL, 1, 0);
        game_interlude(0x10, NULL, 0, 0);
    }
    if (tech == TECH_SOCIAL_PSYCH && (game_faction_defs[f].rule_flags & RULE_FLAG_ALIEN)) {
        for (int i = 1; i < FACTION_COUNT; i++)
            if (f != i && !(game_faction_defs[i].rule_flags & RULE_FLAG_ALIEN) &&
                (game_factions[f].diplo_status[i] & TREATY_COMMLINK))
                game_interlude(0x24, NULL, 1, 0);
    }
}

// The Planetary Datalinks hand a tech that three of the factions have to
// their holder; and a faction whose file has it share tech takes a tech that
// enough others have, if they have been spied on (or the original's other
// ways, which are as written here).
static void ta_spread(int f, int tech) {
    int links = game_secret_projects[SP_PLANETARY_DATALINKS_];
    if (links >= 0) {
        int owner = game_bases[links].faction;
        if (owner > 0 && f > 0 && has_tech(tech, f) && !has_tech(tech, owner)) {
            int known = 0;
            for (int i = 1; i < FACTION_COUNT; i++) known += has_tech(tech, i);
            if (known >= DATALINKS_FACTIONS) tech_achieved(owner, tech, -1, 0);
        }
    }
    if (f <= 0 || !has_tech(tech, f)) return;
    for (int i = 1; i < FACTION_COUNT; i++) {
        const struct faction_def *D = &game_faction_defs[i];
        if (D->rule_sharetech <= 0 || has_tech(tech, i)) continue;
        int count = 0;
        for (int j = 1; j < FACTION_COUNT; j++) {
            if (!has_tech(tech, j)) continue;
            uint32_t status = game_factions[i].diplo_status[j];
            if (!*game_expansion || !(D->rule_flags & RULE_FLAG_SHARE_TECH) || (status & DIPLO_INFILTRATOR) ||
                (status & TREATY_PACT) || has_project(SP_EMPATH_GUILD, i) ||
                (i == *game_governor_faction && !(game_faction_defs[j].rule_flags & RULE_FLAG_ALIEN)))
                count++;
        }
        if (count >= D->rule_sharetech) tech_achieved(i, tech, -2, 1);
    }
}

// What a real tech does besides being known: the faction's ranking, the
// reactors and designs it opens, the window and notes, the facilities and
// maps it brings, the Datalinks and sharing.
static void ta_tech(uint8_t *pop, int f, int tech, int from, int steal, int first) {
    struct faction_data *F = &game_factions[f];
    const struct faction_def *D = &game_faction_defs[f];
    int cur = *game_current_faction;
    // The tech ranking counts it twice, and what the faction owes for its
    // last techs comes down by as much.
    F->tech_ranking++;
    if (F->unk_360) F->unk_360--;
    F->tech_ranking++;
    if (F->unk_360) F->unk_360--;
    if (tech == TECH_TRANSCEND) F->transcendent_techs++;

    int reactor = ta_reactors(f, tech);
    if (reactor && f == cur && !*game_net_multiplayer) {
        game_parse_say(0, (int)(intptr_t)game_reactors[reactor - 1].name, -1, -1);
        game_x_pop(image_text(0x69170c), 0);
    }

    // Designs: in a network game they wait for the faction's next turn;
    // else the faction's are made anew (for the AI, or a player who has
    // chosen it), and the player is shown which stock design is new.
    *game_first_new_proto = -1;
    if (*game_net_multiplayer) {
        F->flags |= FACTION_FLAG_NEW_TECH;
    } else if (!game_is_human(f) || (*game_preferences & PREF_AUTO_DESIGN)) {
        for (int j = 0; j < 64; j++) game_protos[f * 64 + j].flags &= (uint16_t)~0xc0;
        consider_designs(f);
        if (f == cur) {
            for (int i = 0; i < 64; i++)
                if ((game_protos[i].flags & PROTO_FLAG_ACTIVE) && tech == game_protos[i].preq_tech &&
                    *game_first_new_proto < 0)
                    *game_first_new_proto = i;
        }
    }
    *(int32_t *)game_at(0x7591a4) = -1;
    *(int32_t *)game_at(0x7591a8) = -1;

    if (f == cur || (*game_state & GAME_STATE_OMNISCIENT)) ta_announce(pop, f, tech, from, steal);
    ta_free_bonuses(f, tech);
    if (game_tech[tech].flags & TECH_FLAG_SECRETS) game_mon_secrets_of_tech(f);

    if (f == cur) {
        if (tech == game_terraform[FORMER_FOREST_].preq_tech && !*game_net_multiplayer) {
            game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
            game_x_pop2(image_str(0x691b14), image_text(0x69178c), 0);
        }
        if ((*game_preferences & PREF_AUTO_DESIGN) && *game_first_new_proto >= 0 && !*game_net_multiplayer)
            ta_designs(f, tech);
        if (F->diff_level > 1 && !*game_net_multiplayer) ta_social(f, tech);
    }
    if ((game_tech[tech].flags & TECH_FLAG_REVEALS_MAP) && !(F->flags & FACTION_FLAG_MAP_SEEN)) ta_reveal_map(f, tech);

    tech_effects(f);
    if (f == cur) ta_notes(f, tech);
    ta_spread(f, tech);

    // The first to learn a secret is told, and may take a free tech.
    if (*game_turn && first && (game_tech[tech].flags & TECH_FLAG_SECRETS)) {
        char number[16];
        F->flags |= FACTION_FLAG_SECRET_FIRST;
        game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
        game_parse_says(1, (char *)D->adjective, -1, -1);
        game_battle_text[0] = 0;
        strcat(game_battle_text, image_text(0x6916f0));
        snprintf(number, sizeof(number), "%d", f != cur);
        strcat(game_battle_text, number);
        game_x_pop(game_battle_text, 0);
        game_log_say5(image_text(0x6917d8), (char *)D->leader_name, f, tech, game_tech_achieved[tech]);
    }
}

static void ta_body(uint8_t *pop, int f, int tech, int from, int steal) {
    if (tech < 0) return;
    struct faction_data *F = &game_factions[f];
    game_log_say5(image_text(0x6916fc), game_faction_defs[f].leader_name, f, tech, from);
    if (game_is_human(f)) game_ambience(0xc9);
    if (tech >= TA_MAPS) {
        ta_map(f, from);
        return;
    }
    if (tech >= TA_COMMLINKS && tech < TA_PROTOS) {
        treaty_on(f, tech - TA_COMMLINKS, TREATY_COMMLINK);
        return;
    }
    if (tech >= TA_PROTOS) {
        const struct unit_proto *P = &game_protos[tech - TA_PROTOS];
        int made = propose_proto(f, P->chassis, P->weapon, P->armor, P->abilities, P->reactor, PLAN_PROTOTYPE_DONE_,
                                 (char *)P->name);
        if (made >= 0) ta_new_design(f, made);
        return;
    }
    if (F->tech_research_id == tech) F->tech_research_id = -1;
    uint8_t old = game_tech_achieved[tech];
    F->tech_source[tech] = (uint8_t)(from > 0 ? from : 0);
    game_tech_achieved[tech] = (uint8_t)(old | (1u << (f & 31)));
    F->techs_achieved++;
    // At the start of the game, and when the New Tech window is skipped,
    // that is all.
    if (!*game_turn || *game_skip_tech_screen) return;
    ta_tech(pop, f, tech, from, steal, old == 0);
}

// The popup, which the assembly entry places.
__attribute__((used)) void tech_achieved_in(uint8_t *pop, int f, int tech, int from, int steal);
void tech_achieved_in(uint8_t *pop, int f, int tech, int from, int steal) {
    game_popup_ctor(pop);
    ta_body(pop, f, tech, from, steal);
    game_popup_dtor(pop);
}

__asm__(".text\n"
        ".globl tech_achieved\n"
        "tech_achieved:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5500, %esp\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5400(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call tech_achieved_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module tech_module;
#define TECH (&tech_module)

SHADOW2(TECH, int, has_tech, int, int)
SHADOW2(TECH, int, tech_recurse, int, int)
SHADOW1(TECH, int, tech_category, uint32_t)
SHADOW2(TECH, int, tech_avail, uint32_t, int)
SHADOW3(TECH, int, tech_is_preq, int, int, uint32_t)
SHADOW1(TECH, int, tech_mil, int)
SHADOW1(TECH, int, tech_tech, int)
SHADOW1(TECH, int, tech_infra, int)
SHADOW1(TECH, int, tech_colonize, int)
SHADOW2(TECH, int, valid_tech_leap, int, int)
SHADOW1(TECH, int, tech_rate, int)
SHADOW3(TECH, int, tech_val, int, int, int)
SHADOW2(TECH, int, redundant, int, int)
SHADOW3(TECH, int, terrain_avail, int, int, int)
// facility_avail leaves a faction's name in game_battle_text; tech_secret
// puts its label there, and its messages are held and compared; tech_ai
// draws from both random generators.
SHADOW_DECLARE(int, facility_avail, (int fac, int faction, int base, int queue))
// facility_avail reads and writes nothing else, and is called often, so its
// check keeps the text apart itself, without holding the clock.
static int check_facility_avail(int fac, int faction, int base, int queue) {
    enum { TEXT = 32 };
    char before[TEXT], want_text[TEXT];
    memcpy(before, game_battle_text, TEXT);
    hooks_nested++;
    int want = orig_facility_avail(fac, faction, base, queue);
    hooks_nested--;
    memcpy(want_text, game_battle_text, TEXT);
    memcpy(game_battle_text, before, TEXT);
    int got = facility_avail(fac, faction, base, queue);
    if (got != want || memcmp(game_battle_text, want_text, TEXT))
        hooks_mismatch(&stats_facility_avail, "(%d, %d, %d, %d) = %d \"%.31s\", original %d \"%.31s\"", fac, faction,
                       base, queue, got, game_battle_text, want, want_text);
    memcpy(game_battle_text, want_text, TEXT);
    return want;
}
SHADOW_ENTRY(TECH, int, facility_avail, (int fac, int faction, int base, int queue), (fac, faction, base, queue))
SHADOW_WRITER(TECH, void, tech_secret, (int a, int b), (a, b), WRITES(game_battle_text, 32), "%d, %d", a, b)
SHADOW_WRITER(TECH, int, tech_ai, (int a), (a), (WRITES(game_rand_state(), 4), WRITES(game_random_seed, 4)), "%d", a)

// tech_achieved: a check of the whole game. The windows and messages it
// makes are held or dropped (quiet.c), but a held question answers 0, which
// only the AI could mean, and a network game sends messages: both run the
// original as it is. The calls it makes to itself (the Datalinks, the
// sharing of tech) go straight to the original or to ours within a check.
SHADOW_WRITER_CHECK(void, tech_achieved, (int a, int b, int c, int d), (a, b, c, d), orig_tech_achieved(a, b, c, d),
                    hooks_writes_game(&w_), "%d, %d, %d, %d", a, b, c, d)
static int tech_achieved_checkable(void) { return !*game_net_multiplayer && !*game_human_factions; }
static void shadow_tech_achieved(int faction, int tech, int from, int steal) {
    if (hooks_nested) {
        orig_tech_achieved(faction, tech, from, steal);
        return;
    }
    if (shadow_bypass()) {
        tech_achieved(faction, tech, from, steal);
        return;
    }
    stats_tech_achieved.calls++;
    shadow_tick(TECH);
    if (!tech_achieved_checkable()) {
        orig_tech_achieved(faction, tech, from, steal);
        return;
    }
    check_tech_achieved(faction, tech, from, steal);
}

// The functions below write instead of returning a value. Each check runs
// the original on a copy of what it writes, runs ours on another, compares,
// and leaves the original's result in place.

// say_tech appends to `out`, and may set the grammar for the next noun.
static void (*orig_say_tech)(char *out, int tech, int category);
static struct hook_stats stats_say_tech = {"say_tech"};
static void check_say_tech(char *out, int tech, int category) {
    char want[1024], got[1024];
    size_t n = strlen(out);
    if (n >= 512) {  // longer than the game ever builds here; just run the original
        hooks_nested++;
        orig_say_tech(out, tech, category);
        hooks_nested--;
        return;
    }
    int gender = *game_parse_gender, plural = *game_parse_plural;
    memcpy(want, out, n + 1);
    memcpy(got, out, n + 1);
    hooks_nested++;
    orig_say_tech(want, tech, category);
    hooks_nested--;
    int want_gender = *game_parse_gender, want_plural = *game_parse_plural;
    *game_parse_gender = gender, *game_parse_plural = plural;
    say_tech(got, tech, category);
    if (strcmp(got, want) || *game_parse_gender != want_gender || *game_parse_plural != want_plural)
        hooks_mismatch(&stats_say_tech, "(\"%s\", %d, %d) = \"%s\" (grammar %d %d), original \"%s\" (%d %d)", out, tech,
                       category, got, *game_parse_gender, *game_parse_plural, want, want_gender, want_plural);
    strcpy(out, want);
    *game_parse_gender = want_gender, *game_parse_plural = want_plural;
}
static void shadow_say_tech(char *out, int tech, int category) {
    if (hooks_nested) {
        orig_say_tech(out, tech, category);
        return;
    }
    stats_say_tech.calls++;
    hooks_shadow_tick(TECH);
    check_say_tech(out, tech, category);
}

// tech_name fills the game's buffer and returns it.
static char *(*orig_tech_name)(int tech, int category);
static struct hook_stats stats_tech_name = {"tech_name"};
static char *check_tech_name(int tech, int category) {
    char want[256];
    hooks_nested++;
    char *w = orig_tech_name(tech, category);
    hooks_nested--;
    snprintf(want, sizeof(want), "%s", w);
    char *g = tech_name(tech, category);
    if (g != w || strcmp(g, want))
        hooks_mismatch(&stats_tech_name, "(%d, %d) = \"%s\" at %p, original \"%s\" at %p", tech, category, g,
                       (void *)g, want, (void *)w);
    strcpy(w, want);
    return w;
}
static char *shadow_tech_name(int tech, int category) {
    if (hooks_nested) return orig_tech_name(tech, category);
    stats_tech_name.calls++;
    hooks_shadow_tick(TECH);
    return check_tech_name(tech, category);
}

// tech_effects rewrites fields of the faction's record; compare it all.
static void (*orig_tech_effects)(int faction);
static struct hook_stats stats_tech_effects = {"tech_effects"};
static void check_tech_effects(int faction) {
    static struct faction_data before, want;
    struct faction_data *fd = &game_factions[faction];
    before = *fd;
    hooks_nested++;
    orig_tech_effects(faction);
    hooks_nested--;
    want = *fd;
    *fd = before;
    tech_effects(faction);
    if (memcmp(fd, &want, sizeof(want))) {
        size_t at = 0;
        while (((uint8_t *)fd)[at] == ((uint8_t *)&want)[at]) at++;
        at &= ~(size_t)3;
        hooks_mismatch(&stats_tech_effects, "(%d): faction field 0x%zx = %d, original %d", faction, at,
                       *(int32_t *)((uint8_t *)fd + at), *(int32_t *)((uint8_t *)&want + at));
    }
    *fd = want;
}
static void shadow_tech_effects(int faction) {
    if (hooks_nested) {
        orig_tech_effects(faction);
        return;
    }
    stats_tech_effects.calls++;
    hooks_shadow_tick(TECH);
    check_tech_effects(faction);
}

// Input sweeps, run once a turn in shadow mode: every tech, a little beyond
// the table at both ends, against every faction and a few out of range.
static void sweep_has_tech(void) {
    for (int t = -3; t <= 100; t++)
        for (int f = -1; f <= 8; f++) check_has_tech(t, f);
}
static void sweep_tech_recurse(void) {
    for (int t = -3; t <= 100; t++)
        for (int l = 0; l <= 2; l++) check_tech_recurse(t, l);
}
static void sweep_tech_category(void) {
    for (uint32_t t = 0; t < TECH_COUNT; t++) check_tech_category(t);  // unchecked range in the original
}
static void sweep_tech_avail(void) {
    for (int t = -3; t <= 100; t++)
        for (int f = -1; f <= 8; f++) check_tech_avail((uint32_t)t, f);
}
static void sweep_tech_is_preq(void) {
    for (int p = -1; p < TECH_COUNT; p++)
        for (int q = -1; q < TECH_COUNT; q++)
            for (uint32_t r = 0; r <= 3; r++) check_tech_is_preq(p, q, r);
}
#define SWEEP_VALUE(name) \
    static void sweep_##name(void) { for (int t = -3; t <= 100; t++) check_##name(t); }
SWEEP_VALUE(tech_mil)
SWEEP_VALUE(tech_tech)
SWEEP_VALUE(tech_infra)
SWEEP_VALUE(tech_colonize)

// The functions that read faction records only take real factions.
static void sweep_valid_tech_leap(void) {
    for (int t = 0; t < TECH_COUNT; t++)
        for (int f = 0; f < FACTION_COUNT; f++) check_valid_tech_leap(t, f);
}
static void sweep_tech_rate(void) {
    for (int f = 0; f < FACTION_COUNT; f++) check_tech_rate(f);
}
static void sweep_tech_val(void) {
    for (int f = 1; f < FACTION_COUNT; f++)
        for (int simple = 0; simple <= 1; simple++) {
            for (int t = 0; t < TECH_COUNT + FACTION_COUNT; t++) check_tech_val(t, f, simple);
            check_tech_val(9999, f, simple);
        }
    // A prototype's value depends on nothing else.
    for (int p = 0; p < PROTO_COUNT; p++) check_tech_val(TECH_COUNT + FACTION_COUNT + p, 1, 1);
}
// Every kind of name, with and without the category, and every prototype
// that has a name.
static int sweep_name_ids(int *ids) {
    int n = 0;
    for (int t = -3; t < TECH_COUNT + FACTION_COUNT; t++) ids[n++] = t;
    ids[n++] = 9999;
    for (int p = 0; p < PROTO_COUNT; p++)
        if (game_protos[p].name[0]) ids[n++] = TECH_COUNT + FACTION_COUNT + p;
    return n;
}
static void sweep_say_tech(void) {
    static int ids[TECH_COUNT + FACTION_COUNT + PROTO_COUNT + 8];
    int n = sweep_name_ids(ids);
    for (int i = 0; i < n; i++)
        for (int c = 0; c <= 1; c++) {
            char out[256] = "Tech: ";
            check_say_tech(out, ids[i], c);
        }
}
static void sweep_tech_name(void) {
    static int ids[TECH_COUNT + FACTION_COUNT + PROTO_COUNT + 8];
    int n = sweep_name_ids(ids);
    char saved[256];
    snprintf(saved, sizeof(saved), "%s", game_tech_name_buffer);  // the game may still be showing it
    for (int i = 0; i < n; i++)
        for (int c = 0; c <= 1; c++) check_tech_name(ids[i], c);
    strcpy(game_tech_name_buffer, saved);
}
static void sweep_tech_effects(void) {
    for (int f = 0; f < FACTION_COUNT; f++) check_tech_effects(f);
}

// Each faction's first base, or -1.
static void first_bases(int *first) {
    for (int f = 0; f < FACTION_COUNT; f++) first[f] = -1;
    for (int b = 0; b < *game_base_count; b++) {
        int f = game_bases[b].faction;
        if (f < FACTION_COUNT && first[f] < 0) first[f] = b;
    }
}

// The projects redundant() and facility_avail() look for.
static const int swept_projects[] = {
    SPA_COMMAND_NEXUS, SPA_CITIZENS_DEFENSE_FORCE, SP_VIRTUAL_WORLD, SPA_MARITIME_CONTROL_CENTER,
    SPA_CYBORG_FACTORY, SP_SPACE_ELEVATOR, SP_SINGULARITY_INDUCTOR, SP_CLOUDBASE_ACADEMY,
    SP_PLANETARY_ENERGY_GRID, SP_VOICE_OF_PLANET,
};
enum { SWEPT_PROJECTS = sizeof(swept_projects) / sizeof(swept_projects[0]) };
static void set_projects(const int32_t *to) {
    for (int i = 0; i < SWEPT_PROJECTS; i++) game_secret_projects[swept_projects[i]] = to[i];
}
static void save_projects(int32_t *saved) {
    for (int i = 0; i < SWEPT_PROJECTS; i++) saved[i] = game_secret_projects[swept_projects[i]];
}
static void hold_projects(int base) {
    for (int i = 0; i < SWEPT_PROJECTS; i++) game_secret_projects[swept_projects[i]] = base;
}

// Every facility, and a little past both ends, against every faction and a
// few out of range; then with each faction holding all the projects that
// make one redundant.
static void sweep_redundant(void) {
    for (int fac = -1; fac <= FACILITY_COUNT; fac++)
        for (int f = -1; f <= FACTION_COUNT; f++) check_redundant(fac, f);
    int first[FACTION_COUNT];
    int32_t saved[SWEPT_PROJECTS];
    first_bases(first);
    save_projects(saved);
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (first[f] < 0) continue;
        for (int i = 0; i < SWEPT_PROJECTS; i++) {
            set_projects(saved);
            game_secret_projects[swept_projects[i]] = first[f];
            for (int fac = 0; fac < FAC_FIRST_PROJECT; fac++)
                for (int g = 1; g < FACTION_COUNT; g++) check_redundant(fac, g);
        }
    }
    set_projects(saved);
}

// Every facility and project for a faction: anywhere, and at `base` (if
// any) counting none of its queue and all of it.
static void facility_avail_all(int faction, int base) {
    for (int fac = 0; fac < FACILITY_COUNT; fac++) {
        check_facility_avail(fac, faction, -1, 0);
        if (base < 0) continue;
        check_facility_avail(fac, faction, base, 0);
        check_facility_avail(fac, faction, base, 10);
    }
}
static void facility_avail_firsts(const int *first) {
    for (int f = 1; f < FACTION_COUNT; f++) facility_avail_all(f, first[f]);
}

// Each faction's first base with nothing built; with each facility
// another needs built, and queued, on its own; and with all built. Then
// the projects, the rules and the faction's own rules varied.
static void facility_avail_variants(const int *first) {
    static const int needed[] = {
        FAC_PRESSURE_DOME, FA_PERIMETER_DEFENSE, FAC_RECREATION_COMMONS, FAC_TREE_FARM, FAC_FUSION_LAB,
        FAC_RESEARCH_HOSPITAL, FAC_PARADISE_GARDEN, FAC_HAB_COMPLEX, FAC_AEROSPACE_COMPLEX, FAC_ROBOTIC_ASSEMBLY_PLANT,
        FAC_GENEJACK_FACTORY, FAC_CENTAURI_PRESERVE,
    };
    enum { NEEDED = sizeof(needed) / sizeof(needed[0]) };
    for (int f = 1; f < FACTION_COUNT; f++) {
        int b = first[f];
        if (b < 0) continue;
        struct base *B = &game_bases[b], saved = *B;
        memset(B->facilities_built, 0, sizeof(B->facilities_built));
        memset(B->queue, 0, sizeof(B->queue));
        facility_avail_all(f, b);
        for (int i = 0; i < NEEDED; i++) {
            B->facilities_built[needed[i] / 8] = (uint8_t)(1 << needed[i] % 8);
            facility_avail_all(f, b);
            B->facilities_built[needed[i] / 8] = 0;
            B->queue[9] = -needed[i];
            facility_avail_all(f, b);
            B->queue[9] = 0;
        }
        memset(B->facilities_built, 0xff, FAC_FIRST_PROJECT / 8);
        facility_avail_all(f, b);
        *B = saved;
    }
    // Each project held by each faction in turn, then all never built,
    // then all lost.
    int32_t projects[SWEPT_PROJECTS];
    save_projects(projects);
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (first[f] < 0) continue;
        for (int i = 0; i < SWEPT_PROJECTS; i++) {
            set_projects(projects);
            game_secret_projects[swept_projects[i]] = first[f];
            facility_avail_all(f, first[f]);
        }
    }
    hold_projects(-1);
    facility_avail_firsts(first);
    hold_projects(-2);
    facility_avail_firsts(first);
    set_projects(projects);
    // The scenario rules and the transcendence victory flipped, the lowest
    // difficulties, the other game, and each faction's file's rules for
    // free prototypes and alien factions flipped.
    uint32_t rules = *game_rules;
    *game_rules ^= RULES_SCN_NO_BUILDING_SP | RULES_VICTORY_TRANSCENDENCE;
    facility_avail_firsts(first);
    *game_rules = rules;
    int32_t difficulty = *game_difficulty;
    for (int d = 1; d <= 2; d++) {
        *game_difficulty = d;
        for (int f = 1; f < FACTION_COUNT; f++) check_facility_avail(FA_SKUNKWORKS, f, first[f], 0);
    }
    *game_difficulty = difficulty;
    *game_expansion = !*game_expansion;
    facility_avail_firsts(first);
    *game_expansion = !*game_expansion;
    for (int f = 1; f < FACTION_COUNT; f++) {
        int32_t flags = game_faction_defs[f].rule_flags;
        game_faction_defs[f].rule_flags ^= RULE_FLAG_FREE_PROTO | RULE_FLAG_ALIEN;
        facility_avail_all(f, first[f]);
        game_faction_defs[f].rule_flags = flags;
    }
}

static void sweep_facility_avail(void) {
    int first[FACTION_COUNT];
    first_bases(first);
    for (int fac = 0; fac < FACILITY_COUNT; fac++) {
        check_facility_avail(fac, -1, -1, 0);
        check_facility_avail(fac, 0, -1, 0);
    }
    // Up to 48 bases, each with its own faction.
    int step = *game_base_count / 48 + 1;
    for (int b = 0; b < *game_base_count; b += step) facility_avail_all(game_bases[b].faction, b);
    facility_avail_variants(first);
    // Again with every tech for every faction.
    uint8_t achieved[TECH_COUNT];
    memcpy(achieved, game_tech_achieved, sizeof(achieved));
    memset(game_tech_achieved, 0xfe, sizeof(achieved));
    facility_avail_variants(first);
    memcpy(game_tech_achieved, achieved, sizeof(achieved));
}

// Every action on land and sea for every faction and a few out of range;
// then with the Weather Paradigm held by each faction in turn, and the
// scenario rule on raising and lowering land flipped. Each time also with
// every tech for every faction.
static void terrain_avail_all(void) {
    uint8_t achieved[TECH_COUNT];
    memcpy(achieved, game_tech_achieved, sizeof(achieved));
    for (int all = 0; all <= 1; all++) {
        if (all) memset(game_tech_achieved, 0xfe, sizeof(achieved));
        for (int t = 0; t < TERRAFORM_COUNT; t++)
            for (int sea = 0; sea <= 1; sea++)
                for (int f = -1; f <= FACTION_COUNT; f++) check_terrain_avail(t, sea, f);
    }
    memcpy(game_tech_achieved, achieved, sizeof(achieved));
}
static void sweep_terrain_avail(void) {
    terrain_avail_all();
    int first[FACTION_COUNT];
    first_bases(first);
    int32_t saved = game_secret_projects[SPA_WEATHER_PARADIGM];
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (first[f] < 0) continue;
        game_secret_projects[SPA_WEATHER_PARADIGM] = first[f];
        terrain_avail_all();
    }
    game_secret_projects[SPA_WEATHER_PARADIGM] = saved;
    uint32_t rules = *game_rules;
    *game_rules ^= RULES_SCN_NO_TERRAFORMING;
    terrain_avail_all();
    *game_rules = rules;
}

// A few techs for every faction; the messages are held and dropped.
static void sweep_tech_secret(void) {
    for (int f = 1; f < FACTION_COUNT; f++)
        for (int t = f; t < TECH_COUNT; t += 13) check_tech_secret(t, f);
}

// Every faction as it is and with the player's bit flipped, with and
// without blind research; and a faction that cares for growth, lacking the
// terraforming tech, as a human with blind research.
static void tech_ai_all(void) {
    uint8_t humans = *game_human_factions;
    for (int f = 1; f < FACTION_COUNT; f++) {
        check_tech_ai(f);
        *game_human_factions ^= (uint8_t)(1 << f);
        check_tech_ai(f);
        *game_human_factions = humans;
    }
}
static void sweep_tech_ai(void) {
    uint32_t rules = *game_rules;
    tech_ai_all();
    *game_rules ^= RULES_BLIND_RESEARCH;
    tech_ai_all();
    *game_rules = rules | RULES_BLIND_RESEARCH;
    int formers = game_protos[PROTO_FORMERS].preq_tech;
    uint8_t humans = *game_human_factions;
    if (formers >= 0 && formers < TECH_COUNT) {
        uint8_t achieved = game_tech_achieved[formers];
        for (int f = 1; f < FACTION_COUNT; f++) {
            struct faction_data *fd = &game_factions[f];
            int32_t growth = fd->ai_growth;
            game_tech_achieved[formers] &= (uint8_t)~(1 << f);
            *game_human_factions |= (uint8_t)(1 << f);
            fd->ai_growth = 1;
            check_tech_ai(f);
            fd->ai_growth = growth;
            *game_human_factions = humans;
            game_tech_achieved[formers] = achieved;
        }
    }
    *game_rules = rules;
}

// tech_achieved over a spread of cases on protected copies of the game: an
// ordinary tech, the last one, a map, a comm link, a stock design, and a
// number that is none of them; given by a faction or found, taken from
// another or from the Datalinks or by sharing; with the faction the
// player, a human (hotseat), or neither; the tech the first of its kind, a
// secret, one that shows the map, that unlocks a reactor, a free facility
// or ability, a social model, a design; the tutorial's techs. The windows
// and messages are held and dropped, and every question answers 0. A
// network game is never set up here: its branches are covered by reading.
static uint32_t ta_mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static inline int ta_coin(uint32_t h, int bit, int n) { return (int)((h >> bit) % (uint32_t)n) == 0; }

static int ta_base_of(int faction, uint32_t h) {
    int n = *game_base_count;
    for (int k = 0; k < n; k++) {
        int b = (int)((h + (uint32_t)k) % (uint32_t)n);
        if (game_bases[b].faction == faction) return b;
    }
    return -1;
}

static void ta_case(int turn, int k) {
    uint32_t h = ta_mix((uint32_t)turn * 0x9e3779b1u + (uint32_t)k), g = ta_mix(h), s = ta_mix(g), u = ta_mix(s),
             x = ta_mix(u);
    int f = 1 + (int)(h % 7);
    struct hook_writes w = {.protect_only = 1};
    hooks_writes_game(&w);
    hooks_writes_begin(&w);

    // The tech.
    int tech;
    static const int special[] = {TECH_SOCIAL_PSYCH, TECH_SELF_AWARE, TECH_CENTMED, TECH_MATTER_COMP, TECH_TRANSCEND, 0, 1};
    switch (g % 16) {
    case 0: tech = TA_MAPS + (int)((g >> 4) % 3); break;
    case 1: tech = TA_COMMLINKS + (int)((g >> 4) % 8); break;
    case 2: tech = TA_PROTOS + (int)((g >> 4) % 64); break;
    case 3: tech = (g >> 4) % 4 ? -1 - (int)((g >> 6) % 3) : TA_PROTOS + (int)((g >> 8) % PROTO_COUNT); break;
    case 4: case 5: tech = special[(g >> 4) % (sizeof special / sizeof special[0])]; break;
    default: tech = (int)((g >> 4) % TECH_COUNT); break;
    }
    // Where it comes from: nobody, the Datalinks, sharing, or a faction.
    int from;
    switch ((g >> 10) % 8) {
    case 0: case 1: from = 0; break;
    case 2: from = -1; break;
    case 3: from = -2; break;
    default: from = 1 + (int)((g >> 13) % 7); break;
    }
    int steal = ta_coin(g, 17, 4);
    if (tech >= TA_MAPS && from <= 0) from = 1 + (int)((g >> 13) % 7);  // a map comes from a faction

    // Who is the player, and who is human.
    int cur = ta_coin(u, 0, 3) ? f : 1 + (int)((u >> 2) % 7);
    *game_current_faction = cur;
    uint8_t humans = 0;
    if (ta_coin(u, 5, 4)) humans |= (uint8_t)(1u << f);
    if (ta_coin(u, 7, 4)) humans |= (uint8_t)(1u << cur);
    if (ta_coin(u, 9, 5)) humans |= (uint8_t)(1u << (1 + (u >> 11) % 7));
    *game_human_factions = humans;
    if (ta_coin(u, 14, 8)) *game_turn = 0;
    if (ta_coin(u, 17, 10)) *game_skip_tech_screen = 1;
    if (ta_coin(u, 20, 4)) *game_state ^= GAME_STATE_OMNISCIENT;
    if (ta_coin(u, 22, 3)) *game_preferences ^= PREF_AUTO_DESIGN;
    if (ta_coin(u, 25, 3)) *game_expansion = !*game_expansion;
    if (ta_coin(u, 30, 2)) *game_difficulty = 2 + (int32_t)((x >> 20) % 5);  // at 1 or lower every design is built
    if (ta_coin(u, 31, 3)) {
        game_tech_achieved[TECH_SELF_AWARE] |= (uint8_t)(1u << f);
        game_tech_achieved[TECH_MATTER_COMP] |= (uint8_t)(1u << f);
    }
    if (ta_coin(u, 28, 3)) *game_governor_faction = 1 + (int)((u >> 29) % 7);
    if (ta_coin(h, 20, 4)) game_factions[f].diff_level = (int32_t)((h >> 22) % 4);
    if (ta_coin(h, 24, 3)) game_factions[f].tech_research_id = tech;
    if (ta_coin(h, 26, 3)) game_factions[f].unk_360 = (int32_t)((h >> 28) % 3);

    if (tech >= 0 && tech < TECH_COUNT) {
        struct rules_tech *T = &game_tech[tech];
        // Who has the tech already: none (so it is a first), or some.
        if (ta_coin(s, 0, 3)) game_tech_achieved[tech] = 0;
        else if (ta_coin(s, 2, 2)) game_tech_achieved[tech] = (uint8_t)(s >> 4) & 0xfe;
        if (ta_coin(s, 12, 3)) T->flags ^= TECH_FLAG_SECRETS;
        if (ta_coin(s, 14, 3)) T->flags ^= TECH_FLAG_REVEALS_MAP;
        if (ta_coin(s, 16, 4)) game_factions[f].flags ^= FACTION_FLAG_MAP_SEEN;
        // A reactor, a facility, an ability, a social model, the forest
        // and the Biology Lab it opens.
        if (ta_coin(x, 0, 4)) game_reactors[(x >> 2) % 3 + 1].preq_tech = (int16_t)tech;
        if (ta_coin(x, 4, 5)) {
            struct faction_def *D = &game_faction_defs[f];
            D->bonus_count = 1 + (int32_t)((x >> 6) % 2);
            int abl = ta_coin(x, 8, 2);
            for (int i = 0; i < D->bonus_count; i++) {
                D->bonus_id[i] = abl ? BONUS_FREE_ABILITY : BONUS_FREE_FACILITY;
                D->bonus_val1[i] = abl ? (int32_t)((x >> 9) % 16) : (int32_t)((x >> 9) % 66);
                if (abl) game_abilities[D->bonus_val1[i]].preq_tech = (int16_t)tech;
                else game_facilities[D->bonus_val1[i]].preq_tech = tech;
            }
        }
        if (ta_coin(x, 16, 5)) game_social_preqs[(x >> 18) % 4 * SOCIAL_PREQ_STRIDE + (x >> 20) % 4] = tech;
        if (ta_coin(x, 22, 6)) game_terraform[FORMER_FOREST_].preq_tech = tech;
        if (ta_coin(x, 25, 6)) game_facilities[FAC_BIOLOGY_LAB_].preq_tech = tech;
        // The Datalinks and the Voice of Planet held, and others that
        // know the tech, sharing, spying and the Empath Guild.
        if (ta_coin(s, 18, 3)) game_secret_projects[SP_PLANETARY_DATALINKS_] = ta_base_of(1 + (int)((s >> 20) % 7), s);
        if (ta_coin(s, 23, 4)) game_secret_projects[SP_VOICE_OF_PLANET] = ta_base_of(f, s);
        if (ta_coin(s, 25, 4)) game_secret_projects[SP_EMPATH_GUILD] = ta_base_of(1 + (int)((s >> 27) % 7), s);
        if (ta_coin(h, 1, 3)) game_tech_achieved[tech] |= (uint8_t)(h >> 3);
        for (int i = 1; i < FACTION_COUNT; i++) {
            uint32_t c = ta_mix(x + 0x100 + (uint32_t)i);
            struct faction_def *D = &game_faction_defs[i];
            if (ta_coin(c, 0, 5)) D->rule_sharetech = 1 + (int32_t)((c >> 2) % 3);
            if (ta_coin(c, 5, 3)) D->rule_flags ^= RULE_FLAG_SHARE_TECH;
            if (ta_coin(c, 7, 8)) D->rule_flags ^= RULE_FLAG_ALIEN;
            for (int j = 1; j < FACTION_COUNT; j++) {
                uint32_t d = ta_mix(c + (uint32_t)j);
                if (ta_coin(d, 0, 5)) game_factions[i].diplo_status[j] ^= DIPLO_INFILTRATOR;
                if (ta_coin(d, 3, 5)) game_factions[i].diplo_status[j] ^= TREATY_PACT;
                if (ta_coin(d, 6, 4)) game_factions[i].diplo_status[j] ^= TREATY_COMMLINK;
            }
        }
    }
    // A stock design offered (made over from one of the faction's, so it
    // is a design the faction lacks), the stock designs a tech makes
    // possible, and designs flagged as new.
    if (tech >= TA_PROTOS && tech < TA_PROTOS + PROTO_COUNT) {
        struct unit_proto *P = &game_protos[tech - TA_PROTOS], *B = &game_protos[f * 64 + (h >> 3) % 8];
        if (ta_coin(x, 1, 4)) {
            *P = *B;
            P->armor = (uint8_t)((B->armor + 1 + (x >> 4) % 3) % ARMOR_COUNT);
        }
        if (ta_coin(x, 6, 3)) P->reactor = (uint8_t)(1 + (x >> 8) % 4);
        if (ta_coin(x, 10, 3)) P->weapon = (uint8_t)((x >> 12) % 6);
    }
    if (ta_coin(x, 28, 4) && tech >= 0) game_protos[(x >> 30) * 3 + 1].preq_tech = (int16_t)tech;
    if (ta_coin(g, 28, 4)) *game_first_new_proto = (int32_t)((g >> 30) % 4);
    if (ta_coin(h, 5, 3)) game_protos[f * 64 + (h >> 7) % 8].flags ^= PROTO_FLAG_NEW_DESIGN;
    if (ta_coin(h, 10, 3)) game_protos[f * 64 + (h >> 12) % 8].flags ^= PROTO_FLAG_UPGRADE;
    if (ta_coin(h, 14, 3)) game_protos[f * 64 + (h >> 16) % 8].flags ^= PROTO_FLAG_PROTOTYPED_;
    if (ta_coin(h, 18, 3)) game_protos[f * 64 + (h >> 20) % 8].reactor = 1;
    // Another faction's map to take, and bases seen by it.
    if (tech >= TA_MAPS && from > 0) {
        int n = *game_map_area;
        struct map_tile *tile = *game_map;
        for (int i = 0; i < n; i += 1 + (int)(g % 5)) tile[i].visibility |= (uint8_t)(1u << from);
    }
    *game_rand_state() = ta_mix(x + 3);
    check_tech_achieved(f, tech, from, steal);
    hooks_writes_abandon(&w);
}

static void ta_sweep_cases(void) {
    if (!*game_map || *game_base_count <= 0 || *game_net_multiplayer) return;
    int turn = *game_turn;
    for (int k = 0; k < 24; k++) ta_case(turn, k);
}

static void tech_sweep(void) {
    sweep_has_tech();
    sweep_tech_recurse();
    sweep_tech_category();
    sweep_tech_avail();
    sweep_tech_is_preq();
    sweep_tech_mil();
    sweep_tech_tech();
    sweep_tech_infra();
    sweep_tech_colonize();
    sweep_valid_tech_leap();
    sweep_tech_rate();
    sweep_tech_val();
    sweep_say_tech();
    sweep_tech_name();
    sweep_tech_effects();
    // The rest change the game to test on, or write: each check puts back
    // what the original wrote, and drops its messages.
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    char text[32];
    memcpy(text, game_battle_text, sizeof(text));
    sweep_redundant();
    sweep_facility_avail();
    sweep_terrain_avail();
    sweep_tech_secret();
    sweep_tech_ai();
    ta_sweep_cases();
    memcpy(game_battle_text, text, sizeof(text));
    hooks_sweeping = sweeping;
}

// The first bytes of each original: whole instructions, no relative jumps.
static const struct hook tech_hooks[] = {
    HOOK(0x5b9f20, has_tech, 0x55, 0x8b, 0xec, 0x56, 0x57),                       // push ebp; mov ebp, esp; push esi; push edi
    HOOK(0x5b9f90, tech_recurse, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),             // ...; mov eax, [ebp+8]
    HOOK(0x5b9fe0, tech_category, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08),            // ...; mov ecx, [ebp+8]
    HOOK(0x5bac20, tech_avail, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c),               // ...; mov ecx, [ebp+12]
    HOOK(0x5bcb60, tech_is_preq, 0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08),       // ...; push ebx; mov ebx, [ebp+8]
    HOOK(0x5bdd70, tech_mil, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bdd90, tech_tech, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bddc0, tech_infra, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bddf0, tech_colonize, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x57ce50, valid_tech_leap, 0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08),    // ...; push ebx; mov ebx, [ebp+8]
    HOOK(0x5b9c40, say_tech, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x50),                 // ...; sub esp, 0x50
    HOOK(0x5b9ef0, tech_name, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c),                // ...; mov eax, [ebp+12]
    HOOK(0x5bae60, tech_effects, 0x55, 0x8b, 0xec, 0x53, 0x56),                   // ...; push ebx; push esi
    HOOK(0x5bcbe0, tech_val, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x20),                 // ...; sub esp, 0x20
    HOOK(0x5be6b0, tech_rate, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),                // ...; sub esp, 0x10
    HOOK(0x5ba030, redundant, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),                // ...; mov eax, [ebp+8]
    HOOK(0x5ba0e0, facility_avail, 0x55, 0x8b, 0xec, 0x51, 0x53, 0x8b, 0x5d, 0x08),  // ...; push ecx; push ebx; mov ebx, [ebp+8]
    HOOK(0x5bab40, terrain_avail, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),            // ...; mov eax, [ebp+8]
    HOOK(0x5badc0, tech_secret, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x50),              // ...; sub esp, 0x50
    HOOK(0x5bdc10, tech_ai, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),                  // ...; sub esp, 0x10
    HOOK(0x5bb000, tech_achieved, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // ...; mov eax, fs:[0]
};
struct hook_module tech_module = {"technology", tech_hooks, HOOK_COUNT(tech_hooks), tech_sweep, NULL, -1};
