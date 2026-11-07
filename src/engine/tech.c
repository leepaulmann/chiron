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
#include "game.h"
#include "map.h"
#include "shadow.h"
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
};
struct hook_module tech_module = {"technology", tech_hooks, HOOK_COUNT(tech_hooks), tech_sweep, NULL, -1};
