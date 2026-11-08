// Scores, rankings and scenario objectives. Reimplemented from terranx.exe
// 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT), whose score.cpp reimplements most of
// these (with some fixes of its own, noted below where the original differs).
#include "score.h"

#include <stdio.h>
#include <string.h>

#include "base.h"
#include "game.h"
#include "setup.h"
#include "shadow.h"
#include "tech.h"

enum {
    RULES_CONQUEST = 0x2, RULES_IRONMAN = 0x400,
    RULES_UNITS_REACH_OBJECTIVE = 0x8000000,  // objective units count only in an objective base of theirs
    RULES_UNITS_REACH_HQ = 0x10000000,        // or in a headquarters of theirs
    RULES_ALL_BASES_OBJECTIVES = 0x20000000, RULES_PROJECTS_OBJECTIVES = 0x40000000,
    STATE_SCENARIO = 0x10, STATE_CHEATED = 0x20, STATE_CONQUERED = 0x2000,
    STATE_TERRITORY_OBJECTIVES = 0x8000,       // each square owned counts
    STATE_HIGHEST_SCORE_WINS = 0x80000,
    STATE_IMPROVEMENT_OBJECTIVES = 0x2000000,  // each improvement on a square owned counts
    STATE_FACILITY_OBJECTIVES = 0x4000000,     // bases with game_scn_victory_facility count
    STATE_POPULATION_OBJECTIVES = 0x20000000, STATE_TECH_OBJECTIVES = 0x40000000,
    BASE_EVENT_OBJECTIVE = 0x8000, VEH_FLAG_OBJECTIVE = 0x20,
    TREATY_SURRENDERED = 0x2000000,
    // faction_data.flags of a faction that has won, by the score screen's
    // reckoning: the first two give up to 1200 points, the third 2000.
    FACTION_FLAG_WON_A = 0x20000, FACTION_FLAG_WON_B = 0x40000, FACTION_FLAG_WON_C = 0x100000,
    PROTO_FIRST_FACTION = 64,  // designs below are the basic ones everyone shares
    TURN_MIGHT_TURNS = 1000,
    OBJECTIVES_NONE = 9000,
};
#define STATE_CREDIT_OBJECTIVES 0x80000000u

// Improvements an owned square counts for: sensor, thermal borehole,
// echelon mirror, condenser, forest, airbase, farm, bunker, solar
// collector, mine, magtube and road.
#define MAP_IMPROVEMENTS 0x81e4885cu

static inline int alive(int faction) { return (*game_factions_alive & (1u << (faction & 31))) != 0; }
static inline int human(int faction) { return (*game_human_factions & (1u << (faction & 31))) != 0; }
static inline int imax(int a, int b) { return a >= b ? a : b; }

// ---- The calendar ---------------------------------------------------------------

int game_year(int turn) { return turn + *game_start_year; }

void say_year(char *buf) {
    char year[16];
    snprintf(year, sizeof year, "%d", *game_start_year + *game_turn);
    strcat(buf, year);
}

// ---- Objectives ---------------------------------------------------------------------

// The difficulty adds to 6 tenths of a percent: 1 a level, and from the
// third level up (level - 1) / 2 more for each; intense rivalry adds 1.
int get_rating(int faction, int score) {
    int diff = game_factions[faction].diff_level, r = diff;
    for (int level = 3; level <= diff; level++) r += (level - 1) / 2;
    if (*game_rules & RULES_INTENSE_RIVALRY) r++;
    return (int)((uint32_t)(r + 6) * (uint32_t)score) / 160;
}

// Whether the base has the scenario's objective facility, which counts only
// up to facility 64. OpenSMACX uses has_fac(), which also looks at the
// production queue's first item; the original reads the built bits only.
static int has_objective_facility(int base) {
    if (!(*game_state & STATE_FACILITY_OBJECTIVES) || *game_scn_victory_facility >= 65) return 0;
    int offset;
    uint32_t mask;
    bitmask(*game_scn_victory_facility, &offset, &mask);
    return (game_bases[base].facilities_built[offset] & (uint8_t)mask) != 0;
}

int is_objective(int base) {
    if (*game_rules & RULES_ALL_BASES_OBJECTIVES) return 1;
    if (game_bases[base].event & BASE_EVENT_OBJECTIVE) return 1;
    if (*game_rules & RULES_PROJECTS_OBJECTIVES)
        for (int i = 0; i < SP_COUNT; i++)
            if (game_secret_projects[i] == base) return 1;
    return has_objective_facility(base);
}

// A unit that has reached a base counts if the base is its faction's, or in
// a cooperative game a pact partner's.
static int reached(int faction, int base) {
    int owner = game_bases[base].faction;
    return owner == faction ||
           ((game_factions[faction].diplo_status[owner] & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE));
}

// What counts depends on the scenario: objective units (anywhere, or in an
// objective base or a headquarters of the faction's or a partner's), bases
// marked as objectives (or all), and, by the game state, population, techs,
// energy credits, bases with the objective facility, improved squares,
// squares owned, and secret projects.
int num_objectives(int faction, int pact) {
    if (!alive(faction)) return 0;
    const struct faction_data *f = &game_factions[faction];
    int n = f->objective_bonus;
    for (int i = 0; i < *game_veh_count; i++) {
        const struct veh *v = &game_vehs[i];
        if (v->faction != faction || !(v->flags & VEH_FLAG_OBJECTIVE)) continue;
        if (*game_rules & RULES_UNITS_REACH_OBJECTIVE) {
            int b = base_at(v->x, v->y);
            if (b >= 0 && is_objective(b) && reached(faction, b)) n++;
        } else if (*game_rules & RULES_UNITS_REACH_HQ) {
            int b = base_at(v->x, v->y);
            if (b >= 0 && game_has_facility(&game_bases[b], FAC_HEADQUARTERS) && reached(faction, b)) n++;
        } else {
            n++;
        }
    }
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *b = &game_bases[i];
        if (b->faction != faction) continue;
        if (*game_state & STATE_POPULATION_OBJECTIVES) n += b->pop_size;
        if ((*game_rules & RULES_ALL_BASES_OBJECTIVES) || (b->event & BASE_EVENT_OBJECTIVE)) n++;
    }
    if (*game_state & STATE_TECH_OBJECTIVES) {
        for (int t = 0; t < TECH_COUNT; t++)
            if (has_tech(t, faction)) n++;
        n += f->transcendent_techs;
    }
    if (*game_state & STATE_CREDIT_OBJECTIVES) n += f->energy_credits;
    if (*game_state & STATE_FACILITY_OBJECTIVES)
        for (int i = 0; i < *game_base_count; i++)
            if (game_bases[i].faction == faction && has_objective_facility(i)) n++;
    // The squares are walked in storage order, x + y even.
    if (*game_state & STATE_IMPROVEMENT_OBJECTIVES) {
        const struct map_tile *t = *game_map;
        for (int y = 0; y < *game_map_height; y++)
            for (int x = y & 1; x < *game_map_width; x += 2, t++)
                if (t->territory == faction) n += game_bit_count(t->bits & MAP_IMPROVEMENTS);
    }
    if (*game_state & STATE_TERRITORY_OBJECTIVES) {
        const struct map_tile *t = *game_map;
        for (int y = 0; y < *game_map_height; y++)
            for (int x = y & 1; x < *game_map_width; x += 2, t++)
                if (t->territory == faction) n++;
    }
    if (*game_rules & RULES_PROJECTS_OBJECTIVES)
        for (int i = 0; i < SP_COUNT; i++) {
            int b = game_secret_projects[i];
            if (b >= 0 && game_bases[b].faction == faction) n++;
        }
    if (pact && (*game_rules & RULES_VICTORY_COOPERATIVE))
        for (int i = 1; i < FACTION_COUNT; i++)
            if (i != faction && (f->diplo_status[i] & TREATY_PACT)) n += num_objectives(i, 0);
    return n;
}

// The last faction with the most wins ties. A tie counts each faction
// holding as many as the best so far, unless in a cooperative game it has a
// pact with that faction; the first faction (best still 0, faction 0) is
// checked against faction 0.
int most_objectives(int *faction, int *tied) {
    int best = 0, best_faction = 0;
    if (tied) *tied = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        int n = num_objectives(i, *game_rules & RULES_VICTORY_COOPERATIVE);
        if (n < best) continue;
        if (tied) {
            if (n > best) *tied = 0;
            else if (!(game_factions[i].diplo_status[best_faction] & TREATY_PACT) ||
                     !(*game_rules & RULES_VICTORY_COOPERATIVE))
                (*tied)++;
        }
        best = n, best_faction = i;
    }
    if (faction) *faction = best_faction;
    return best;
}

// The Voice of Planet starts the Ascent, for every faction alike.
int ascending(int faction) {
    (void)faction;
    return game_secret_projects[SP_VOICE_OF_PLANET] != -1;
}

// Once the Voice of Planet stands, whether a base of the faction has item
// 102 first in its queue. Facilities are queued as negative numbers, so this
// is unit design 102 rather than the Ascent (-102); OpenSMACX notes the
// slip, and that the game never calls this.
int transcending(int faction) {
    if (game_secret_projects[SP_VOICE_OF_PLANET] == -1) return 0;
    for (int i = 0; i < *game_base_count; i++)
        if (game_bases[i].faction == faction && game_bases[i].queue[0] == FAC_ASCENT) return 1;
    return 0;
}

// ---- Rankings -------------------------------------------------------------------------

// A faction's strength: in a scenario won by objectives alone (no points
// for them), 10 per objective; otherwise 4 per citizen and transcendent
// tech, the AI weights of its techs, 10 per secret project, the scenario's
// points per objective, and the cost of its units: all for those with a
// weapon of 99 or more, a quarter for unarmed ones, half for psi, and for
// the rest the cost times its weapon over the best weapon in play, divided
// first (so mostly 0). Thinker counts units itself and multiplies first; the
// original skips designs with more than 250 units.
static int strength(int faction, int best_weapon) {
    const struct faction_data *f = &game_factions[faction];
    if ((*game_objectives_required < OBJECTIVES_NONE || *game_objectives_sudden_death < OBJECTIVES_NONE) &&
        !*game_objective_points)
        return 10 * num_objectives(faction, 0);
    int s = 4 * (f->pop_total + f->transcendent_techs);
    for (int t = 0; t < TECH_COUNT; t++)
        if (has_tech(t, faction)) s += game_tech[t].growth + game_tech[t].tech + game_tech[t].wealth + game_tech[t].power;
    for (int i = 0; i < SP_COUNT; i++) {
        int b = game_secret_projects[i];
        if (b >= 0 && game_bases[b].faction == faction) s += 10;
    }
    s += *game_objective_points * num_objectives(faction, 0);
    for (int u = 0; u < PROTO_COUNT; u++) {
        const struct unit_proto *p = &game_protos[u];
        if (u < PROTO_FIRST_FACTION && !has_tech(p->preq_tech, faction)) continue;
        int count = f->units_active[u];
        if (count > 250) continue;
        int value = p->cost * count;
        if (!value) continue;
        int offense = game_weapons[p->weapon].offense;
        if (!offense) s += value / 4;
        else if (offense < 0) s += value / 2;
        else if (offense >= 99) s += value;
        else s += offense / best_weapon * value;
    }
    return s;
}

// The leader the AI may plot against: the top faction if great_satan()
// says it is far ahead, or near the end, a human on a high level (or any
// human with intense rivalry). An AI with a truce or treaty (not a pact) with
// it may then resolve to betray it, more likely the more others are at war
// with it, the less warlike the AI, and the worse the leader's reputation.
static void plot_against_leader(void) {
    int top = game_faction_rankings[7];
    int leader = game_great_satan(top, 0) ? game_faction_rankings[7] : 0;
    if (game_climactic_battle())
        for (int i = 1; i < FACTION_COUNT; i++)
            if (human(i) && (game_factions[i].diff_level >= 4 || (*game_rules & RULES_INTENSE_RIVALRY))) leader = i;
    if (!leader) {
        *game_state &= ~GAME_STATE_RANKED;
        return;
    }
    *game_state |= GAME_STATE_RANKED;
    const struct faction_data *L = &game_factions[leader];
    int vendettas = 0;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (game_factions[i].diplo_status[leader] & TREATY_VENDETTA) vendettas++;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (human(i)) continue;
        const struct faction_data *f = &game_factions[i];
        uint32_t st = f->diplo_status[leader];
        if (!(st & 0x7) || (st & (TREATY_PACT | TREATY_SHALL_BETRAY | TREATY_UNK_800))) continue;
        if ((st & TREATY_UNK_4000000) && !(st & TREATY_UNK_800)) continue;
        int diff = (*game_rules & RULES_INTENSE_RIVALRY) ? 5 : L->diff_level;
        int revenge = L->diplo_status[i] & TREATY_WANT_REVENGE;
        int chance = game_reputation(leader, i);
        chance += game_climactic_battle() ? 4 : 0;
        chance += (f->flags_ext >> 3) & 4;  // FACTION_FLAG_EXT_SHAMELESS
        chance += revenge ? 4 : 0;
        chance *= diff;
        int range = (int)((uint32_t)(vendettas - f->ai_fight + 2) << 8);
        if (game_rnd(range) < chance) game_set_treaty(i, leader, TREATY_SHALL_BETRAY, 1);
    }
}

// Every faction's strength, also by turn for the replay. With `full`, the
// rankings: the strongest 7, then down to 1 (a faction left over keeps 0;
// were all to tie at -1, faction 0 would take the rank), the alive factions
// by rank, the highest and lowest ranked humans, and the plotting above.
void rankings(int full) {
    int best_weapon = 1;
    for (int u = PROTO_FIRST_FACTION; u < PROTO_COUNT; u++) {
        const struct unit_proto *p = &game_protos[u];
        if (!(p->flags & PROTO_FLAG_ACTIVE) || !(p->flags & PROTO_FLAG_BUILT)) continue;
        int offense = game_weapons[p->weapon].offense;
        if (offense >= best_weapon && offense < 99) best_weapon = offense;
    }
    for (int i = 0; i < FACTION_COUNT; i++) {
        if (full) game_factions[i].ranking = 0;
        if (!i) continue;
        game_ranking_scores[i] = alive(i) ? strength(i, best_weapon) : 0;
        if (*game_turn < TURN_MIGHT_TURNS) game_turn_might[*game_turn][i] = (int16_t)game_ranking_scores[i];
    }
    if (!full) return;
    for (int rank = 7; rank > 0; rank--) {
        int best = -1, id = 0;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (game_ranking_scores[i] > best) best = game_ranking_scores[i], id = i;
        game_ranking_scores[id] = -1;
        game_factions[id].ranking = rank;
    }
    memset(game_faction_rankings, 0, FACTION_COUNT * sizeof(int32_t));
    for (int i = 1; i < FACTION_COUNT; i++)
        if (alive(i)) game_faction_rankings[game_factions[i].ranking] = i;
    game_ranked_faction[0] = game_ranked_faction[1] = 0;
    for (int rank = 7; rank >= 0; rank--)
        if (human(game_faction_rankings[rank])) {
            game_ranked_faction[0] = game_faction_rankings[rank];
            break;
        }
    for (int rank = 0; rank < FACTION_COUNT; rank++)
        if (human(game_faction_rankings[rank])) {
            game_ranked_faction[1] = game_faction_rankings[rank];
            break;
        }
    plot_against_leader();
}

// ---- The score ---------------------------------------------------------------------

// faction_data.diplo_status[other] for other -1 too, which the original
// reads (as the word before the array) when no faction holds the Ascent.
static uint32_t diplo_status(int faction, int other) {
    const uint8_t *p = (const uint8_t *)&game_factions[faction] + offsetof(struct faction_data, diplo_status);
    uint32_t v;
    memcpy(&v, p + 4 * other, sizeof v);
    return v;
}

// Parts 0 to 8: citizens (its own, those of factions that surrendered to
// it in a pact, half of a cooperative partner's), commerce, techs,
// transcendent techs, secret projects, the native life bonus, objectives or
// the Ascent, victory, and part 8, which nothing here fills.
void compute_score(int faction, int *score, int *parts, int partial) {
    struct faction_data *f = &game_factions[faction];
    if ((*game_state & GAME_STATE_FINAL_SCORE_DONE) && !partial) return;
    if (!score) score = &f->score_total;
    if (!parts) parts = f->score_parts;
    *score = 0;
    if (!partial) f->score = 0;
    memset(parts, 0, 9 * sizeof(int));
    // Citizens, and the commerce each of the faction's bases gets from
    // others (worked out afresh), unless either side is under sanctions or
    // alien.
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *b = &game_bases[i];
        int owner = b->faction;
        if (owner == faction) {
            parts[0] += b->pop_size;
            set_base(i);
            base_compute(0);
            for (int j = 1; j < FACTION_COUNT; j++) {
                if ((game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) ||
                    (game_faction_defs[j].rule_flags & RULE_FLAG_ALIEN))
                    continue;
                parts[1] += !f->sanction_turns && !game_factions[j].sanction_turns ? game_base_commerce_import[j] : 0;
            }
        } else if (!(f->flags & (FACTION_FLAG_WON_A | FACTION_FLAG_WON_B))) {
            uint32_t st = game_factions[owner].diplo_status[faction];
            if (st & TREATY_SURRENDERED) {
                if (st & TREATY_PACT) parts[0] += b->pop_size;
            } else if ((st & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE)) {
                parts[0] += b->pop_size / 2;
            }
        } else if ((f->diplo_status[owner] & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE)) {
            parts[0] += b->pop_size;
        } else {
            parts[0] += b->pop_size / 2;
        }
    }
    for (int t = 0; t < TECH_COUNT; t++)
        if (has_tech(t, faction)) parts[2]++;
    parts[3] = 10 * f->transcendent_techs;
    int points = imax(*game_objective_points, 1);
    parts[6] = num_objectives(faction, *game_rules & RULES_VICTORY_COOPERATIVE) * points;
    for (int i = 0; i < SP_COUNT; i++) {
        int b = game_secret_projects[i];
        if (b >= 0 && game_bases[b].faction == faction) parts[4] += 25;
    }
    // The Ascent's points go to its builder and pact partners by
    // population, the builder's counted twice. Thinker guards the faction
    // and the division; the original divides by zero if no one counts.
    *game_score_ascent_points = 0;
    memset(game_score_ascent_pop, 0, FACTION_COUNT * sizeof(int32_t));
    int ascent = game_secret_projects[SP_ASCENT_TO_TRANSCENDENCE];
    if (ascent >= 0) {
        *game_score_ascent_points = imax(2 * (1000 - *game_turn), 300);
        int mine = 0, all = 0, holder = -1;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (game_bases[ascent].faction == i) {
                holder = i;
                all = 2 * game_factions[i].pop_total;
                game_score_ascent_pop[i] = all;
                if (i == faction) mine = all;
                break;
            }
        for (int i = 1; i < FACTION_COUNT; i++)
            if (holder != i && (diplo_status(i, holder) & TREATY_PACT)) {
                int pop = game_factions[i].pop_total;
                all += pop;
                game_score_ascent_pop[i] = pop;
                if (i == faction) mine += pop;
            }
        parts[6] = *game_score_ascent_points * mine / all;
        game_score_ascent_pop[0] = all;
    }
    // Victory: by conquest, a share of the faction's earlier victory part by
    // the population it and those surrendered to it hold; or by its own win
    // flags, or a cooperative partner's.
    int v = (int)((uint32_t)(500 - *game_turn) * 2);
    parts[7] = v < 0 ? 0 : v;
    if (!(*game_rules & RULES_CONQUEST)) parts[7] = 0;
    if ((*game_state & STATE_CONQUERED) && (*game_rules & RULES_CONQUEST) && parts[7]) {
        int mine = 0, all = 0;
        if (*game_base_count > 0) {
            for (int i = 0; i < *game_base_count; i++) {
                const struct base *b = &game_bases[i];
                int pop = b->pop_size;
                all += pop;
                if (b->faction == faction) {
                    mine += pop;
                } else {
                    uint32_t st = game_factions[b->faction].diplo_status[faction];
                    if ((st & TREATY_PACT) && (st & TREATY_SURRENDERED)) mine += pop;
                }
            }
            if (all) parts[7] = f->score_parts[7] * mine / all;
        }
    } else {
        parts[7] = 0;
        uint32_t flags = f->flags;
        if (flags & (FACTION_FLAG_WON_A | FACTION_FLAG_WON_B)) {
            parts[7] = imax(2 * (600 - *game_turn), 200);
        } else if (flags & FACTION_FLAG_WON_C) {
            parts[7] = imax(2 * (1000 - *game_turn), 300);
        } else {
            for (int i = 1; i < FACTION_COUNT; i++)
                if ((game_factions[i].flags & (FACTION_FLAG_WON_A | FACTION_FLAG_WON_B)) &&
                    (f->diplo_status[i] & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE))
                    parts[7] = imax(600 - *game_turn, 100);
        }
    }
    if (!alive(faction)) parts[7] = parts[8] = 0;
    // In a scenario won by objectives (unless the highest score wins), the
    // faction's own parts but the objectives are cleared, whichever parts
    // were asked for; then, if its own victory part is clear, holding
    // enough objectives (at least the most anyone holds, if no number is
    // set) gives the victory points.
    int required = *game_objectives_required;
    if ((required < OBJECTIVES_NONE || *game_objectives_sudden_death < OBJECTIVES_NONE) &&
        !(*game_state & STATE_HIGHEST_SCORE_WINS))
        for (int i = 0; i < 9; i++)
            if (i != 6) f->score_parts[i] = 0;
    if (!f->score_parts[7]) {
        if (!required) {
            int need = most_objectives(NULL, NULL) < 1 ? 1 : most_objectives(NULL, NULL);
            if (num_objectives(faction, *game_rules & RULES_VICTORY_COOPERATIVE) >= need)
                parts[7] = *game_victory_points;
        } else if (num_objectives(faction, *game_rules & RULES_VICTORY_COOPERATIVE) >= *game_objectives_required) {
            parts[7] = *game_victory_points;
        }
    }
    for (int i = 0; i < 9; i++) *score += parts[i];
    if (*game_native_life != 1 && !(*game_state & STATE_SCENARIO)) {
        parts[5] = (*game_native_life - 1) * *score / 4;
        *score += parts[5];
    }
    // Where the highest score wins, the victory part (set after the total)
    // goes to a faction at least level with the best.
    if (*game_state & STATE_HIGHEST_SCORE_WINS) {
        if (partial) return;
        int best = 0, other, other_parts[9];
        for (int i = 1; i < FACTION_COUNT; i++) {
            compute_score(i, &other, other_parts, 1);
            if (other > best) best = other;
        }
        if (*score >= best) parts[7] = *game_victory_points ? *game_victory_points : 1000;
    } else if (partial) {
        return;
    }
    f->score = *score;
    if ((*game_rules & RULES_IRONMAN) && !(*game_state & STATE_CHEATED)) f->score = 2 * *score;
}

// ---- The game's end ----------------------------------------------------------------

enum {
    // game_victory_type: how the game ended, for the player.
    VICTORY_TRANSCEND_A = 1, VICTORY_TRANSCEND_C = 3, VICTORY_CONQUEST = 4, VICTORY_CONQUEST_COOP = 5,
    VICTORY_DIPLOMATIC = 6, VICTORY_LOST_CAPTURE = 7, VICTORY_TIME = 8, VICTORY_SUDDEN_DEATH = 9,
    VICTORY_DIPLOMATIC_COOP = 10, VICTORY_DIPLOMATIC_LOSS = 11, VICTORY_ECONOMIC = 12, VICTORY_ECONOMIC_COOP = 13,
    VICTORY_ECONOMIC_LOSS = 14, VICTORY_LOST = 15, VICTORY_ALIEN = 16, VICTORY_ALIEN_COOP = 17, VICTORY_ALIEN_LOSS = 18,
    STATE_ECONOMIC_WON = 0x400000,
    RULES_SCN_SOLO_MISSION = 0x80000,  // a scenario's sudden death counts for human factions only
    MORE_RULES_ALIENS_WON = 0x80,
    FACTION_FLAG_ECONOMIC_WON = FACTION_FLAG_WON_B, FACTION_FLAG_ALIEN_WON = FACTION_FLAG_WON_C,
    FAC_SUBSPACE_GENERATOR = 40,
    PREF_PROJECT_MOVIES = 0x10000000, PREF_NO_INTERLUDES = 0x40000,
    INTERLUDE_WON = 0x13, INTERLUDE_WON_COOP = 0x14, INTERLUDE_ALIENS_WON = 0x21, INTERLUDE_BEACON = 0x20,
    INTERLUDE_BEACON_LOST = 0x1d,
};
static char *text_at(uint32_t addr) { return (char *)game_at(addr); }
static void say_text(int slot, char *text) { game_parse_says(slot, text, -1, -1); }
static int is_alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
// The faction's leader's title, name and noun in text slots 0 to 2.
static void say_faction(int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_parse_plural = 0;
    *game_parse_gender = d->leader_female;
    say_text(0, (char *)d->leader_title);
    *game_parse_gender = d->leader_female;
    *game_parse_plural = 0;
    say_text(1, (char *)d->leader_name);
    *game_parse_gender = d->noun_gender;
    *game_parse_plural = d->noun_plural;
    say_text(2, (char *)d->noun);
}
static void pop_end(uint32_t label, uint32_t picture) {
    game_popp(*game_script_file, text_at(label), 0, text_at(picture), 0);
}

// Whether some faction won this turn, as end_of_game(1) finds out: the
// time limit; an alien faction with enough subspace generators running
// (bases with one, of the size the rules ask); a faction whose cornering
// of the energy market comes due (which ends the search when it is the
// player); a faction holding a scenario's sudden-death objectives (humans
// only, under the rule for that). The victory type is the player's view:
// its own win, its pact partner's in a cooperative game, or its loss. The
// winner is the first found, or the player.
static int end_detect(void) {
    int winner = -1, current = *game_current_faction;
    if (*game_mission_year >= *game_ending_year) {
        *game_victory_type = VICTORY_TIME;
        *game_state |= GAME_STATE_DONE;
    }
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (!alive(f)) continue;
        struct faction_data *F = &game_factions[f];
        int running = 0;
        if (is_alien(f))
            for (int b = 0; b < *game_base_count; b++)
                if (game_bases[b].faction == f && has_fac(FAC_SUBSPACE_GENERATOR, b, 0) &&
                    game_bases[b].pop_size >= game_ascent_numbers[0])
                    running++;
        int coop = (F->diplo_status[current] & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE);
        int vt = *game_victory_type;
        if (running >= game_ascent_numbers[1]) {
            F->flags |= FACTION_FLAG_ALIEN_WON;
            if (f == current) *game_victory_type = VICTORY_ALIEN;
            else if (coop && vt != VICTORY_ALIEN) *game_victory_type = VICTORY_ALIEN_COOP;
            else if (!coop && vt != VICTORY_ALIEN && vt != VICTORY_ALIEN_COOP) *game_victory_type = VICTORY_ALIEN_LOSS;
            *game_state |= GAME_STATE_DONE;
            if (winner < 0 || f == current) winner = f;
            *game_more_rules |= MORE_RULES_ALIENS_WON;
        }
        vt = *game_victory_type;
        if (F->corner_market_turn > 0 && F->corner_market_turn == *game_turn && F->corner_market_cost > 0) {
            F->flags |= FACTION_FLAG_ECONOMIC_WON;
            if (f == current) *game_victory_type = VICTORY_ECONOMIC;
            else if (coop && vt != VICTORY_ECONOMIC) *game_victory_type = VICTORY_ECONOMIC_COOP;
            else if (!coop && vt != VICTORY_ECONOMIC && vt != VICTORY_ECONOMIC_COOP)
                *game_victory_type = VICTORY_ECONOMIC_LOSS;
            *game_state |= STATE_ECONOMIC_WON | GAME_STATE_DONE;
            winner = f;
            if (f == current) break;
            continue;
        }
        if (!human(f) && (*game_rules & RULES_SCN_SOLO_MISSION)) continue;
        if (*game_objectives_sudden_death < 1) continue;
        if (num_objectives(f, *game_rules & RULES_VICTORY_COOPERATIVE) >= *game_objectives_sudden_death) {
            *game_victory_type = VICTORY_SUDDEN_DEATH;
            *game_state |= GAME_STATE_DONE;
            if (winner < 0 || f == *game_current_faction) winner = f;
        }
    }
    return winner;
}

// The year the game ends, told 20 years ahead (in a scenario, 20 and 10).
static void end_warning(void) {
    uint32_t st = *game_state;
    int year = *game_mission_year, end = *game_ending_year;
    int scenario = (st & STATE_SCENARIO) != 0;
    if (st & GAME_STATE_DONE) return;
    if (!scenario ? year != end - 20 : year != end - 20 && year != end - 10) return;
    char num[16];
    game_parse_num(0, end);
    game_battle_text[0] = 0;
    snprintf(num, sizeof num, "%d", end);
    strcat(game_battle_text, num);
    say_text(0, game_battle_text);
    if (!scenario) game_x_pop(text_at(0x68b6e4), 0);
    else game_x_pop2(text_at(0x68b704), text_at(0x68b6f4), 0);
}

// The time limit: the year, and in a scenario its winner by objectives or
// score (the player on a tie), or that time ran out.
static void end_time_limit(void) {
    int current = *game_current_faction;
    game_parse_num(0, *game_mission_year);
    game_battle_text[0] = 0;
    say_year(game_battle_text);
    say_text(0, game_battle_text);
    if (!(*game_state & STATE_SCENARIO)) {
        game_x_pop(text_at(0x68b78c), 0);
        return;
    }
    if (*game_objectives_required && !(*game_state & STATE_HIGHEST_SCORE_WINS)) {
        game_x_pop2(text_at(0x68b780), text_at(0x68b770), 0);
        return;
    }
    int best = current, best_score = -9999;
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (!alive(f)) continue;
        int score;
        if (*game_state & STATE_HIGHEST_SCORE_WINS) {
            int parts[9];
            compute_score(f, &score, parts, 1);
        } else {
            score = num_objectives(f, *game_rules & RULES_VICTORY_COOPERATIVE);
        }
        if (score > best_score || (score == best_score && f == *game_current_faction)) best = f, best_score = score;
    }
    say_faction(best);
    if (best == *game_current_faction) game_x_pop2(text_at(0x68b748), text_at(0x68b73c), 0);
    else game_x_pop2(text_at(0x68b764), text_at(0x68b754), 0);
}

// How the game ended, told the player, with its movies and the monuments
// to the win; the credits, score, hall of fame (not for a scenario) and
// replay; and the question whether to play on, which isn't asked in a
// network game or of a player whose last base was taken. Returns 1 to
// leave the game.
static int end_finish(int winner) {
    int current = *game_current_faction, victor = 0;
    for (int f = 1; f < FACTION_COUNT; f++) {
        uint32_t flags = game_factions[f].flags;
        if (flags & FACTION_FLAG_WON_A) victor = f;
        if ((flags & FACTION_FLAG_WON_B) &&
            (!victor || (game_factions[victor].diplo_status[*game_current_faction] & TREATY_PACT) ||
             victor == *game_current_faction))
            victor = f;
    }
    int credits = 0, vt = *game_victory_type;
    const struct faction_def *P = &game_faction_defs[current];
    switch (vt) {
    case 1:
    case 2:
    case 3:
        credits = 1;
        break;
    case VICTORY_DIPLOMATIC:
        pop_end(0x68b7a8, 0x68b798);
        break;
    case VICTORY_LOST_CAPTURE:
        if (*game_preferences & PREF_PROJECT_MOVIES)
            game_amovie_project_2(text_at(is_alien(current) ? 0x68b710 : P->leader_female ? 0x68b71c : 0x68b728));
        break;
    case VICTORY_TIME:
        end_time_limit();
        break;
    case VICTORY_SUDDEN_DEATH:
        say_faction(winner);
        if (winner == *game_current_faction) game_x_pop2(text_at(0x68b88c), text_at(0x68b87c), 0);
        else game_x_pop2(text_at(0x68b8a4), text_at(0x68b898), 0);
        break;
    case VICTORY_DIPLOMATIC_COOP:
        say_faction(victor);
        pop_end(0x68b7cc, 0x68b7bc);
        break;
    case VICTORY_DIPLOMATIC_LOSS:
        say_faction(victor);
        pop_end(game_factions[victor].diplo_status[*game_current_faction] & (TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE)
                    ? 0x68b7ec : 0x68b7fc,
                0x68b7dc);
        break;
    case VICTORY_ECONOMIC:
        pop_end(0x68b81c, 0x68b80c);
        break;
    case VICTORY_ECONOMIC_COOP:
        say_faction(victor);
        pop_end(0x68b83c, 0x68b82c);
        break;
    case VICTORY_ECONOMIC_LOSS:
        say_faction(victor);
        pop_end(game_factions[victor].diplo_status[*game_current_faction] & (TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE)
                    ? 0x68b85c : 0x68b86c,
                0x68b84c);
        break;
    case VICTORY_LOST:
        if (*game_language) game_x_pop(text_at(0x68b730), 0);
        break;
    case VICTORY_ALIEN:
        game_interlude(INTERLUDE_BEACON, NULL, 4, 0);
        say_faction(victor);
        pop_end(0x68b8c0, 0x68b8b0);
        if (*game_preferences & PREF_PROJECT_MOVIES) {
            game_battle_text[0] = 0;
            strcat(game_battle_text, P->search_key);
            (*game_char_upper)(game_battle_text);
            game_amovie_project_2(text_at(strcmp(game_battle_text, text_at(0x68b8cc)) ? 0x68b8d8 : 0x68b8e4));
        }
        break;
    case VICTORY_ALIEN_COOP:
        say_faction(victor);
        pop_end(0x68b900, 0x68b8f0);
        break;
    case VICTORY_ALIEN_LOSS:
        if (!is_alien(*game_current_faction)) {
            const struct faction_def *V = &game_faction_defs[victor];
            *game_parse_gender = V->noun_gender;
            *game_parse_plural = V->noun_plural;
            game_interlude(INTERLUDE_BEACON_LOST, (char *)V->noun, 1, 0);
        }
        say_faction(victor);
        pop_end(0x68b924, 0x68b910);
        break;
    }
    vt = *game_victory_type;
    switch (vt) {
    case VICTORY_CONQUEST:
    case VICTORY_CONQUEST_COOP:
    case VICTORY_DIPLOMATIC:
    case VICTORY_ECONOMIC:
        game_mon_winning_unify(*game_current_faction, -1);
        break;
    case VICTORY_DIPLOMATIC_COOP:
    case VICTORY_ECONOMIC_COOP:
        game_mon_winning_unify(victor, -1);
        game_mon_winning_unify(*game_current_faction, victor);
        break;
    case VICTORY_DIPLOMATIC_LOSS:
    case VICTORY_ECONOMIC_LOSS:
        game_mon_winning_unify(victor, -1);
        break;
    }
    if (!(*game_preferences & PREF_NO_INTERLUDES)) {
        switch (*game_victory_type) {
        case VICTORY_CONQUEST:
        case VICTORY_DIPLOMATIC:
        case VICTORY_ECONOMIC:
            game_interlude(is_alien(*game_current_faction) ? INTERLUDE_ALIENS_WON : INTERLUDE_WON, NULL, 4, 0);
            credits = 1;
            break;
        case VICTORY_CONQUEST_COOP:
        case VICTORY_DIPLOMATIC_COOP:
        case VICTORY_ECONOMIC_COOP:
            game_interlude(INTERLUDE_WON_COOP, NULL, 4, 0);
            credits = 1;
            break;
        }
    }
    vt = *game_victory_type;
    if (vt == 1 || vt == 2 || vt == 3 || credits) game_show_credits();
    game_report_score(1);
    if (!(*game_state & STATE_SCENARIO)) {
        game_quayle(*game_current_faction);
        game_hall_of_fame(1);
    }
    game_show_replay();
    *game_state |= GAME_STATE_FINAL_SCORE_DONE;
    if (!*game_net_multiplayer && *game_victory_type != VICTORY_LOST_CAPTURE &&
        game_popp(*game_script_file, text_at(0x68b944), 0, text_at(0x68b934), 0))
        return 0;
    *game_control_turn_b = !*game_net_multiplayer;
    if (*game_net_multiplayer) net_game_close();
    *game_control_turn_a = 1;
    return 1;
}

// Once at the start of a turn (`detect`) and as it ends: whether the game
// is over, and if so its end shown, once; returns 1 to leave the game. The
// original's victory types follow Thinker's names; Thinker adds a fix or
// two (the time limit's number in its warning), not followed here.
int end_of_game(int detect) {
    uint32_t st = *game_state;
    if ((st & GAME_STATE_DONE) && (st & GAME_STATE_FINAL_SCORE_DONE)) return 0;
    int winner = -1;
    if (!(st & GAME_STATE_DONE) && detect) winner = end_detect();
    if (detect) end_warning();
    if (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE)) return 0;
    return end_finish(winner);
}

// Whether end_of_game(detect) would reach the end's screens and question:
// the detection run and put back (it writes the game state, the victory
// type, the rules' extras and the factions' flags, nothing else).
int end_of_game_due(int detect) {
    uint32_t st = *game_state, more = *game_more_rules, flags[FACTION_COUNT];
    int32_t vt = *game_victory_type;
    for (int f = 0; f < FACTION_COUNT; f++) flags[f] = game_factions[f].flags;
    if ((st & GAME_STATE_DONE) && (st & GAME_STATE_FINAL_SCORE_DONE)) return 0;
    if (!(st & GAME_STATE_DONE) && detect) end_detect();
    int due = (*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE);
    *game_state = st, *game_more_rules = more, *game_victory_type = vt;
    for (int f = 0; f < FACTION_COUNT; f++) game_factions[f].flags = flags[f];
    return due;
}

// ---- Shadow mode ---------------------------------------------------------------------

extern struct hook_module score_module;
#define SCORE (&score_module)
SHADOW1(SCORE, int, game_year, int)
SHADOW_WRITER(SCORE, void, say_year, (char *a), (a), WRITES(a, strlen(a) + 16), "%p", (void *)a)
SHADOW2(SCORE, int, get_rating, int, int)
SHADOW1(SCORE, int, is_objective, int)
SHADOW2(SCORE, int, num_objectives, int, int)
SHADOW_WRITER(SCORE, int, most_objectives, (int *a, int *b), (a, b), do {
    if (a) WRITES(a, sizeof *a);
    if (b) WRITES(b, sizeof *b);
} while (0), "%p, %p", (void *)a, (void *)b)
SHADOW1(SCORE, int, ascending, int)
SHADOW1(SCORE, int, transcending, int)
// rankings() writes the factions and the game state, and may draw on rand().
SHADOW_WRITER(SCORE, void, rankings, (int a), (a), hooks_writes_game(&w_), "%d", a)
// compute_score() also works out the faction's bases (base_compute()).
SHADOW_WRITER(SCORE, void, compute_score, (int a, int *b, int *c, int d), (a, b, c, d), do {
    hooks_writes_game(&w_);
    if (b) WRITES(b, sizeof *b);
    if (c) WRITES(c, 9 * sizeof *c);
} while (0), "%d, %p, %p, %d", a, (void *)b, (void *)c, d)
// The game's end is checked on all of the game while it goes on (the
// warnings, and the detection of a win); once due, its screens (several
// allocate) and the question whether to play on run the original
// unchecked. The sweep checks them with the screens held. The game calls
// it many times a turn with nothing to do (no detection, the game not
// over, or over and shown): those calls are checked on the game state and
// victory type alone.
SHADOW_WRITER_CHECK(int, end_of_game, (int a), (a), orig_end_of_game(a), hooks_writes_game(&w_), "%d", a)
static int check_end_of_game_idle(int detect) {
    struct hook_writes w_ = {0};
    WRITES(game_state, sizeof *game_state);
    WRITES(game_victory_type, sizeof *game_victory_type);
    hooks_writes_begin(&w_);
    hooks_nested++;
    int want = orig_end_of_game(detect);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int got = end_of_game(detect);
    const char *diff = hooks_writes_end(&w_, got, want);
    if (diff) hooks_mismatch(&stats_end_of_game, "(%d) %s", detect, diff);
    hooks_ui_release();
    return want;
}
static int shadow_end_of_game(int detect) {
    if (hooks_nested) return orig_end_of_game(detect);
    if (shadow_bypass()) return end_of_game(detect);
    stats_end_of_game.calls++;
    shadow_tick(SCORE);
    uint32_t st = *game_state;
    if ((st & GAME_STATE_DONE) ? (st & GAME_STATE_FINAL_SCORE_DONE) != 0 : !detect) return check_end_of_game_idle(detect);
    if (end_of_game_due(detect)) return orig_end_of_game(detect);
    return check_end_of_game(detect);
}

// ---- The sweep ---------------------------------------------------------------------------

static int slice;

// The queries on the game as it stands.
static void sweep_queries(void) {
    static const int values[] = {0, 1, 37, 160, 1000, 25000, -999};
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int i = 0; i < (int)(sizeof values / sizeof values[0]); i++) check_get_rating(f, values[i]);
        check_num_objectives(f, 0);
        check_num_objectives(f, 1);
        check_ascending(f);
        check_transcending(f);
    }
    for (int b = 0; b < *game_base_count; b++) check_is_objective(b);
    int who = -1, tied = -1;
    check_most_objectives(&who, &tied);
    check_most_objectives(&who, NULL);
    check_most_objectives(NULL, &tied);
    check_most_objectives(NULL, NULL);
}

// The scores: one faction in full (with the comparison and saving), and
// partly, into the faction's own fields or the caller's.
static void sweep_scores(int faction) {
    int score = -1, parts[9];
    memset(parts, 0xff, sizeof parts);
    check_compute_score(faction, NULL, NULL, 0);
    check_compute_score(faction, &score, parts, 1);
    check_compute_score(faction, &score, NULL, 0);
}

// A scenario made up on the protected copy: objective units (some moved
// into another faction's base), objective bases, pacts, bonuses, and one
// of the ways objectives count, from `variant`.
static void make_scenario(int variant) {
    static const uint32_t rules[] = {0, RULES_UNITS_REACH_OBJECTIVE, RULES_UNITS_REACH_HQ,
                                     RULES_UNITS_REACH_OBJECTIVE | RULES_ALL_BASES_OBJECTIVES,
                                     RULES_PROJECTS_OBJECTIVES, RULES_UNITS_REACH_HQ | RULES_PROJECTS_OBJECTIVES};
    static const uint32_t states[] = {STATE_POPULATION_OBJECTIVES, STATE_TECH_OBJECTIVES, STATE_CREDIT_OBJECTIVES,
                                      STATE_FACILITY_OBJECTIVES, STATE_IMPROVEMENT_OBJECTIVES,
                                      STATE_TERRITORY_OBJECTIVES};
    static const int facilities[] = {FAC_HEADQUARTERS, 4, 64, 65, 10, -1};
    *game_rules = (*game_rules & ~(uint32_t)(RULES_UNITS_REACH_OBJECTIVE | RULES_UNITS_REACH_HQ |
                                             RULES_ALL_BASES_OBJECTIVES | RULES_PROJECTS_OBJECTIVES)) |
                  rules[variant % 6];
    *game_state |= states[variant % 6] | states[(variant / 6 + variant + 1) % 6];
    if (variant & 1) *game_rules ^= RULES_VICTORY_COOPERATIVE;
    *game_scn_victory_facility = facilities[(variant / 2) % 6];
    for (int i = 0; i < *game_veh_count; i++) {
        struct veh *v = &game_vehs[i];
        if ((i + variant) % 3) continue;
        v->flags |= VEH_FLAG_OBJECTIVE;
        if (i % 2 && *game_base_count > 0) {
            const struct base *b = &game_bases[(i / 2 + variant) % *game_base_count];
            v->x = b->x, v->y = b->y;
        }
    }
    for (int i = variant % 4; i < *game_base_count; i += 4) game_bases[i].event |= BASE_EVENT_OBJECTIVE;
    for (int f = 1; f < FACTION_COUNT; f++) {
        game_factions[f].objective_bonus = (f + variant) % 3;
        int other = (f + variant) % (FACTION_COUNT - 1) + 1;
        if (other != f && (f + variant) % 2) {
            game_factions[f].diplo_status[other] |= TREATY_PACT;
            game_factions[other].diplo_status[f] |= TREATY_PACT | ((variant & 2) ? TREATY_SURRENDERED : 0);
        }
    }
}

// The Voice of Planet and the Ascent held by a base of a faction with
// citizens, and a base with item 102 first in its queue.
static void make_ascent(int variant) {
    int n = *game_base_count;
    if (n <= 0) return;
    game_secret_projects[SP_VOICE_OF_PLANET] = variant % n;
    for (int k = 0; k < n; k++) {
        int b = (variant + k) % n, f = game_bases[b].faction;
        if (f > 0 && f < FACTION_COUNT && game_factions[f].pop_total > 0) {
            game_secret_projects[SP_ASCENT_TO_TRANSCENDENCE] = b;
            break;
        }
    }
    game_bases[(variant * 7) % n].queue[0] = FAC_ASCENT;
}

// The victory parts: conquest, the win flags, the objectives settings, the
// highest score winning, native life and ironman.
static void make_victory(int variant) {
    static const int32_t required[] = {0, 3, 9000, 1, 20000};
    static const int32_t sudden[] = {9000, 5, 9000, 20000};
    static const int32_t points[] = {0, 5, 1, 0};
    *game_objectives_required = required[variant % 5];
    *game_objectives_sudden_death = sudden[variant % 4];
    *game_objective_points = points[(variant / 2) % 4];
    *game_victory_points = (variant % 3) * 250;
    if (variant & 1) *game_rules ^= RULES_CONQUEST;
    if (variant & 2) *game_state ^= STATE_CONQUERED;
    if (variant & 4) *game_state ^= STATE_HIGHEST_SCORE_WINS;
    if (variant & 8) *game_state ^= STATE_SCENARIO;
    if (variant % 3 == 1) *game_rules ^= RULES_IRONMAN;
    if (variant % 5 == 2) *game_state ^= STATE_CHEATED;
    *game_native_life = variant % 3;
    static const uint32_t won[] = {FACTION_FLAG_WON_A, FACTION_FLAG_WON_B, FACTION_FLAG_WON_C};
    int f = variant % (FACTION_COUNT - 1) + 1;
    game_factions[f].flags |= won[variant % 3];
    game_factions[f].score_parts[7] = (variant % 4) * 100;
    for (int i = 1; i < FACTION_COUNT; i++) game_factions[i].sanction_turns = (i + variant) % 4 == 0;
    *game_turn = (variant * 97) % 1100;
}

// A human near the end of the game (the energy market about to be
// cornered) on a high level, AIs at truce or treaty with it, and some
// designs with more than 250 units.
static void make_leader(int variant) {
    int h = variant % (FACTION_COUNT - 1) + 1;
    if (!alive(h)) return;
    *game_human_factions = (uint8_t)(1 << h);
    game_factions[h].corner_market_turn = *game_turn + 5;
    game_factions[h].diff_level = 3 + variant % 3;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == h) continue;
        uint32_t *st = &game_factions[i].diplo_status[h];
        *st &= ~(uint32_t)(TREATY_PACT | TREATY_SHALL_BETRAY | TREATY_UNK_800 | TREATY_UNK_4000000);
        *st |= (i + variant) % 3 ? TREATY_TREATY : 0x4;
        if ((i + variant) % 4 == 0) *st |= TREATY_VENDETTA;
        game_factions[i].ai_fight = (i + variant) % 3 - 1;
        game_factions[i].units_active[(i * 37 + variant) % PROTO_COUNT] = (uint8_t)(250 + (i & 1));
    }
}

// The game's end on copies of the game put back after: the time limit
// reached or near (in a scenario too, won by objectives or score), an
// alien faction's subspace generators, an energy market cornered, a
// scenario's sudden death, and each victory type already decided, from
// the player's side or another's, as a pact partner or not, with the
// movies and interludes switched on or off. The screens and the question
// whether to play on are held (it answers no).
static void sweep_end(int variant) {
    static const int32_t types[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    for (int k = 0; k < 6; k++) {
        int v = variant * 6 + k;
        struct hook_writes w = {.protect_only = 1};
        hooks_writes_game(&w);
        hooks_writes_begin(&w);
        uint32_t h = (uint32_t)v * 0x9e3779b9u;
        h ^= h >> 15, h *= 0x85ebca6bu, h ^= h >> 13;
        int f = (int)(h % (FACTION_COUNT - 1)) + 1, g = (int)((h >> 3) % (FACTION_COUNT - 1)) + 1;
        if (alive(f)) *game_current_faction = f;
        *game_human_factions = (uint8_t)(h >> 6 & 1 ? 1u << *game_current_faction : 0);
        static const int ahead[] = {0, 1, 10, 20, 30, -5};
        *game_mission_year = *game_ending_year - ahead[(h >> 7) % 6];
        if (h >> 10 & 1) *game_state ^= STATE_SCENARIO;
        if (h >> 11 & 1) *game_state ^= STATE_HIGHEST_SCORE_WINS;
        *game_objectives_required = (h >> 12 & 1) ? 3 : 0;
        *game_objectives_sudden_death = (h >> 13 & 3) == 0 ? 1 : (h >> 13 & 3) == 1 ? 9000 : 0;
        if (h >> 15 & 1) game_faction_defs[g].rule_flags ^= RULE_FLAG_ALIEN;
        if (h >> 16 & 1) {
            for (int b = 0; b < *game_base_count; b++) {
                if (game_bases[b].faction != g || (b + v) % 2) continue;
                int offset;
                uint32_t mask;
                bitmask(FAC_SUBSPACE_GENERATOR, &offset, &mask);
                game_bases[b].facilities_built[offset] |= (uint8_t)mask;
            }
            game_ascent_numbers[1] = (int32_t)(h >> 17 & 3);
        }
        if (h >> 19 & 1) {
            game_factions[g].corner_market_turn = *game_turn;
            game_factions[g].corner_market_cost = (h >> 20 & 1) ? 100 : 0;
        }
        if (h >> 21 & 1) game_factions[g].diplo_status[*game_current_faction] ^= TREATY_PACT;
        if (h >> 22 & 1) *game_rules ^= RULES_VICTORY_COOPERATIVE;
        if (h >> 23 & 1) *game_rules ^= RULES_SCN_SOLO_MISSION;
        if (h >> 24 & 1) *game_preferences ^= PREF_PROJECT_MOVIES;
        if (h >> 25 & 1) *game_preferences ^= PREF_NO_INTERLUDES;
        if (h >> 26 & 1) *game_language ^= 1;
        if (h >> 27 & 1) {
            *game_state |= GAME_STATE_DONE;
            *game_victory_type = types[(h >> 28) % 20];
            static const uint32_t won[] = {FACTION_FLAG_WON_A, FACTION_FLAG_WON_B, 0};
            game_factions[g].flags |= won[v % 3];
            game_factions[f].flags |= won[(v + 1) % 3];
        }
        if (k == 5) *game_state |= GAME_STATE_DONE | GAME_STATE_FINAL_SCORE_DONE;
        check_end_of_game((int)(h >> 5 & 1) | (k < 3));
        hooks_writes_abandon(&w);
    }
}

// Once a turn: the queries and scores as the game stands, then on a
// protected copy, made-up scenarios, the Ascent, victory settings and a
// leader to plot against, each checked in turn; get_rating() at each
// difficulty.
static void score_sweep(void) {
    if (!*game_map) return;
    slice++;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int t = -1; t < 3; t++) check_game_year(*game_turn + t * 400);
    char buf[64] = "Year ";
    check_say_year(buf);
    sweep_queries();
    check_rankings(0);
    check_rankings(1);
    sweep_scores(slice % FACTION_COUNT);

    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    for (int f = 1; f < FACTION_COUNT; f++) {
        game_factions[f].diff_level = (slice + f) % 7;
        check_get_rating(f, 1000 + slice);
    }
    *game_rules ^= RULES_INTENSE_RIVALRY;
    for (int f = 1; f < FACTION_COUNT; f++) check_get_rating(f, 999);
    make_scenario(slice);
    sweep_queries();
    make_victory(slice);
    check_rankings(1);
    sweep_scores(1 + slice % (FACTION_COUNT - 1));
    make_ascent(slice);
    for (int f = 0; f < FACTION_COUNT; f++) {
        check_ascending(f);
        check_transcending(f);
    }
    sweep_scores(1 + (slice + 3) % (FACTION_COUNT - 1));
    make_leader(slice);
    check_rankings(1);
    check_rankings(0);
    hooks_writes_abandon(&outer);
    if (!*game_net_multiplayer && !*game_pbem) {
        hooks_writes_begin(&outer);
        sweep_end(slice);
        hooks_writes_abandon(&outer);
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook score_hooks[] = {
    HOOK(0x5c89a0, game_year, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                         // mov eax, [ebp+8]
    HOOK(0x5c89b0, say_year, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x50),                          // sub esp, 0x50
    HOOK(0x5abff0, get_rating, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                        // mov eax, [ebp+8]
    HOOK(0x5ac060, is_objective, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0x9c, 0x64, 0x9a, 0x00),    // push ecx; mov eax, [rules]
    HOOK(0x5ac110, num_objectives, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),                    // sub esp, 0x20
    HOOK(0x5ac5a0, most_objectives, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),             // push ecx; mov eax, [ebp+12]
    HOOK(0x5ac630, transcending, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x65, 0x9a, 0x00),          // mov eax, [Voice of Planet]
    HOOK(0x5ac680, ascending, 0x8b, 0x0d, 0x90, 0x65, 0x9a, 0x00),                             // mov ecx, [Voice of Planet]
    HOOK(0x5ac690, rankings, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),                          // sub esp, 0x10
    HOOK(0x5acbe0, compute_score, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x38),                     // sub esp, 0x38
    HOOK(0x524870, end_of_game, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00),       // sub esp, 0x90
};
struct hook_module score_module = {"score", score_hooks, HOOK_COUNT(score_hooks), score_sweep, NULL, -1};
