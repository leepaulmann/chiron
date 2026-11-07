// The Planetary Council: each faction's votes, who may stand for governor,
// the AI's stand on each proposal and its vote, and whether the Council can
// be called. Reimplemented from terranx.exe 2.0's disassembly; names follow
// OpenSMACX (Brendan Casey, GPL-3.0-or-later) and Thinker (MIT). Neither
// has wants_prop(), council_get_vote() or can_call_council().
#include "council.h"

#include <stdint.h>

#include "base.h"
#include "game.h"
#include "shadow.h"
#include "tech.h"

enum {
    BONUS_VOTES = 11,  // faction_def bonus: votes multiplied
    TREATY_SURRENDERED = 0x2000000,
    TREATY_MAJOR_ATROCITY_VICTIM = 0x80000,
    TREATY_HOSTILE = TREATY_VENDETTA | TREATY_WANT_REVENGE | TREATY_UNK_40,  // 0x70
    TREATY_GRUDGE = TREATY_WANT_REVENGE | TREATY_UNK_40,                     // 0x60
    // A faction whose promised vote for governor no longer holds.
    TREATY_PLEDGE_VOID = TREATY_VENDETTA | TREATY_WANT_REVENGE | TREATY_SHALL_BETRAY | TREATY_UNK_800 | TREATY_ATROCITY_VICTIM,
    STATE_SCENARIO_EDITOR = 0x40,
    CHASSIS_MISSILE = 8, WEAPON_PLANET_BUSTER = 16,
    ALT_SHORE = 3 << 5,  // map_tile.climate's altitude bits at sea level
    // council_get_vote()'s answers on a proposal.
    VOTE_YEA = -1, VOTE_NAY = -2, VOTE_ABSTAIN = -3,
    NOT_A_CANDIDATE = -9,
};

static inline uint32_t bit(int n) { return 1u << (n & 31); }
static inline struct faction_data *fac(int f) { return &game_factions[f]; }
static inline const struct faction_def *def(int f) { return &game_faction_defs[f]; }
static inline uint32_t status(int f, int other) { return game_factions[f].diplo_status[other]; }
static inline int alien(int f) { return (def(f)->rule_flags & RULE_FLAG_ALIEN) != 0; }
static inline int holds(int project, int f) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == f;
}

// A faction's votes: its citizens, half again with the Empath Guild, twice
// that with Clinical Immortality, times any votes bonus of its faction
// file (one that is negative is ignored). The aliens of Alien Crossfire
// have none.
int council_votes(int faction) {
    if (*game_expansion && alien(faction)) return 0;
    int votes = 0;
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction) votes += game_bases[b].pop_size;
    if (holds(SP_EMPATH_GUILD, faction)) votes += votes / 2;
    if (holds(SP_CLINICAL_IMMORTALITY, faction)) votes *= 2;
    const struct faction_def *d = def(faction);
    for (int i = 0; i < d->bonus_count; i++)
        if (d->bonus_id[i] == BONUS_VOTES && d->bonus_val1[i] >= 0) votes *= d->bonus_val1[i];
    return votes;
}

// Whether the faction may stand for governor: fewer than two living
// factions have more votes. Not the aliens of Alien Crossfire.
int eligible(int faction) {
    if (*game_expansion && alien(faction)) return 0;
    int votes = council_votes(faction), more = 0;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && (*game_factions_alive & bit(i)) && council_votes(i) > votes) more++;
    return more < 2;
}

// The last word on a vote for governor. A candidate who voted for someone
// else loses a point, and half of what is left unless it is human. A
// faction whose ideology clashes with the candidate's has nothing for it
// without a pact. Towards a human (or as a human) only a dislike counts,
// unless the two have status bit 0x8000000. For itself a faction is at
// least 1.
static int governor_settle(int faction, int target, int r, int clash) {
    int voted = game_council_candidate[target];
    uint8_t humans = *game_human_factions;
    if (voted > 0 && voted != target && target != faction) {
        r--;
        if (!(humans & bit(target))) r /= 2;
    }
    if (faction == target) return r >= 1 ? r : 1;
    if (clash && !(status(faction, target) & TREATY_PACT) && r > 0) r = 0;
    if (!(humans & bit(faction)) && !(humans & bit(target))) return r;
    if (status(faction, target) & TREATY_UNK_8000000) return r;
    return r > 0 ? 0 : r;
}

// How much `faction` wants `target` for governor, given that it isn't
// the faction itself.
static int want_governor(int faction, int target, int behind) {
    const struct faction_data *me = fac(faction), *them = fac(target);
    uint32_t st = status(faction, target);
    int pact = st & TREATY_PACT, r = 0, clash = 0;
    // A faction that has surrendered to it, or a pact partner that has
    // and voted for it, decides it.
    if (pact && (st & TREATY_SURRENDERED)) return governor_settle(faction, target, 99, 0);
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint32_t s = status(faction, i);
        if (i != faction && (s & TREATY_PACT) && (s & TREATY_SURRENDERED) && game_council_candidate[i] == target) r = 999;
    }
    if (*game_governor_faction == faction) r -= 2;
    if (st & TREATY_VENDETTA) r--;
    if (st & TREATY_GRUDGE) r--;
    if (st & TREATY_ATROCITY_VICTIM) r--;
    if (r) return governor_settle(faction, target, r, 0);
    if (pact) {
        // A pact partner it has no quarrel with, either way.
        r = 1;
        if (!game_wants_to_attack(faction, target, 0) && !game_wants_to_attack(target, faction, 0)) r = 2;
    } else {
        const struct faction_def *d = def(faction), *t = def(target);
        if (me->ai_fight == -them->ai_fight) r = -1;
        // Ideology: the model it favours (or another in that category),
        // the one it dislikes, and the candidate's dislike of its own.
        int cat = d->ideology_category;
        if (cat >= 0) {
            int model = them->se_models_pending[cat];
            if (model) {
                if (model == d->ideology_model) r++;
                else r--, clash = 1;
            }
        }
        if (d->anti_ideology_category >= 0) {
            int model = them->se_models_pending[d->anti_ideology_category];
            if (model && model == d->anti_ideology_model) r--, clash = 1;
        }
        if (cat == t->anti_ideology_category && d->ideology_model == t->anti_ideology_model) r -= 2, clash = 2;
        if ((status(faction, target) & TREATY_TREATY) && !game_wants_to_attack(faction, target, 0) &&
            !game_wants_to_attack(target, faction, 0))
            r++;
        uint8_t humans = *game_human_factions;
        if (game_great_satan(target, 0)) {
            r--;
        } else if (humans & bit(target)) {
            // A human: the first ranked faction loses a point in a network
            // game when it is among the top three, else gains one unless
            // its difficulty is Talent or higher; any other human gains one.
            int ranked = game_ranked_faction[0];
            if (target != ranked) r++;
            else if (*game_net_multiplayer && them->ranking >= 5) r--;
            else if (them->diff_level < 2) r++;
        } else {
            // The strongest AI it holds no grudge against gains a point.
            // The original looks the grudge up, and compares the
            // candidate, by the place in the rankings, not the faction
            // there.
            int k = 7;
            for (; k >= 0; k--)
                if (!(humans & bit(game_faction_rankings[k])) && !(status(faction, k) & TREATY_HOSTILE)) break;
            if (target == k) r++;
        }
    }
    // Its stand towards the present governor, when the candidate is
    // someone else: a pact or treaty the candidate lacks counts against
    // it, a quarrel with the governor for it.
    int gov = *game_governor_faction;
    if (gov >= 0 && target != gov && faction != gov) {
        uint32_t g = status(faction, gov);
        if ((g & TREATY_PACT) && !(status(faction, target) & TREATY_PACT)) r -= 2;
        if ((g & TREATY_TREATY) && !(status(faction, target) & TREATY_TREATY)) r--;
        if (g & TREATY_VENDETTA) r++;
        if (g & TREATY_GRUDGE) r += 2;
    }
    if (r >= 0) {
        if (behind <= 1) r++;
        if (!r && target == gov) r = 1;
    }
    // Without a pact, a human candidate on the hardest levels, or in a game
    // of intense rivalry, loses a point each, to no less than -2.
    if (!(status(faction, target) & TREATY_PACT)) {
        int human = *game_human_factions & bit(target);
        if (human && them->diff_level >= 3 && r > -2) r--;
        if (human && (*game_rules & RULES_INTENSE_RIVALRY) && r > -2) r--;
    }
    return governor_settle(faction, target, r, clash);
}

// Governor: -9 for a candidate that is dead, alien or not eligible. For
// itself, 5 to 7 by how few factions have as many votes, nearly doubled
// with no governor yet, 2 more if it is governor, 1 more if it has a
// quarrel with the governor.
static int prop_governor(int faction, int target) {
    if (!(*game_factions_alive & bit(target))) return NOT_A_CANDIDATE;
    if (*game_expansion && alien(target)) return NOT_A_CANDIDATE;
    if (!eligible(target)) return NOT_A_CANDIDATE;
    // The factions, dead or alive, with at least as many votes.
    int behind = 0;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && council_votes(i) >= council_votes(faction)) behind++;
    if (target != faction) return want_governor(faction, target, behind);
    int r = behind > 1 ? 5 : 6;
    if (!behind) r++;
    int gov = *game_governor_faction;
    if (gov < 0) r = 2 * r - 1;
    if (gov == faction) r += 2;
    else if (gov >= 0 && (status(faction, gov) & TREATY_HOSTILE)) r++;
    return governor_settle(faction, target, r, 0);
}

// Supreme Leader, for `target`: for itself 5 if ranked first, else 0. A
// faction that surrendered to it gives 2. A pact partner, under the
// cooperative victory rule only, backs a stronger and more populous
// partner after turn 200 (1), opposes a weaker one less populous than
// itself (-2), else is indifferent after turn 200 and opposed before.
// Others oppose it: -5 after an atrocity against them, else -4, or -2 if
// it is first, they are not in the top two, it is past turn 200 and it
// has twice their military strength, attack and population.
static int prop_supreme_leader(int faction, int target) {
    if (!(*game_factions_alive & bit(target))) return NOT_A_CANDIDATE;
    if (*game_expansion && alien(target)) return NOT_A_CANDIDATE;
    if (!eligible(target)) return NOT_A_CANDIDATE;
    const struct faction_data *me = fac(faction), *them = fac(target);
    if (target == faction) return me->ranking == 7 ? 5 : 0;
    uint32_t st = status(faction, target);
    if ((st & TREATY_PACT) && (st & TREATY_SURRENDERED)) return 2;
    int turn = *game_turn;
    if (st & TREATY_PACT) {
        if (!(*game_rules & RULES_VICTORY_COOPERATIVE)) return 0;
        if (them->ranking > me->ranking && them->pop_total > me->pop_total && turn > 200) return 1;
        if (me->ranking > them->ranking && me->pop_total > them->pop_total) return -2;
        return turn > 200 ? 0 : -1;
    }
    if (st & TREATY_ATROCITY_VICTIM) return -5;
    if (st & (TREATY_VENDETTA | TREATY_WANT_REVENGE)) return -4;
    if (them->ranking != 7 || me->ranking > 5 || turn < 200) return -4;
    if (them->mil_strength < 2 * me->mil_strength || them->mil_attack < 2 * me->mil_attack ||
        them->pop_total < 2 * me->pop_total)
        return -4;
    return -2;
}

// Salvaging the Unity fusion core (energy for all): wanted by the power
// minded and the poor, by one with less energy and income than most; not
// by the warlike or the rich.
static int prop_unity_core(int faction) {
    const struct faction_data *me = fac(faction);
    int r = me->ai_power ? 1 : 0;
    if (me->ai_fight < 0) r--;
    int credits = me->energy_credits;
    if (credits >= 500) r--;
    if (credits < 100) r++;
    if (credits < 200) r++;
    int poorer = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction) continue;
        const struct faction_data *o = fac(i);
        poorer += o->energy_credits > me->energy_credits ? 1 : -1;
        poorer += o->energy_surplus_total - o->facility_maint_total > me->energy_surplus_total - me->facility_maint_total
                      ? 1
                      : -1;
    }
    return poorer > 0 ? r + poorer / 2 : r;
}

// The global trade pact, or (negated) its repeal: one for each pact or
// treaty, less one for each faction with more commerce; the wealth minded
// and a strong economy favour it, the power minded, the warlike and a
// weak economy don't.
static int prop_trade_pact(int faction, int prop) {
    const struct faction_data *me = fac(faction);
    int r = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction) continue;
        if (fac(i)->tech_commerce_bonus > me->tech_commerce_bonus) r--;
        r += game_bit_count(status(faction, i) & (TREATY_PACT | TREATY_TREATY));
    }
    if (me->ai_wealth) r++;
    if (me->ai_power) r--;
    if (me->ai_fight > 0) r--;
    int economy = me->se_pending[SE_ECONOMY];
    if (economy > 1) r++;
    if (economy < 0) r--;
    return prop == PROP_REPEAL_TRADE_PACT ? -r : r;
}

// The solar shade and its increase lower the sea (dir -1); melting the
// caps raises it (dir 1). A growth minded faction, or one with a psi
// rule, wants it (2, doubled with the psi rule) unless the council has
// already moved the sea that way, else not (-2, or -4). Moving the sea
// again the same way costs 2, while the sea is where it was 1; otherwise
// the priorities weigh in. Then each base's people count by how the
// change touches it, its owner's against friends' and against enemies'
// counted as hostility toward them, divided by the faction's population.
static int prop_sea_level(int faction, int prop) {
    const struct faction_data *me = fac(faction);
    int dir = prop == PROP_MELT_CAPS ? 1 : -1, r = 0;
    int moved = *game_sea_level_council, psi = def(faction)->rule_psi;
    if (me->ai_growth || psi) {
        int sign = moved > 0 ? 1 : moved < 0 ? -1 : 0;
        if (moved && dir != sign) r = psi ? 4 : 2;
        else r = psi ? -4 : -2;
    }
    int sign = moved > 0 ? 1 : moved < 0 ? -1 : 0;
    if (sign == dir) {
        r -= 2;
    } else if (!moved) {
        r--;
    } else {
        if (*game_sea_level >= 0) r++;
        if (me->ai_power) r += dir > 0 ? -1 : 1;
        if (me->ai_wealth) r += dir > 0 ? 1 : -1;
        if (me->ai_tech) r += dir > 0 ? 1 : -1;
    }
    int sum = 0;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        int x = B->x, y = B->y;
        const struct map_tile *t = &(*game_map)[*game_map_half_width * y + (x >> 1)];
        int alt = t->climate & 0xe0, m = 0;
        if (alt < ALT_SHORE) {
            // A base at sea: a falling sea helps it, twice where it is deep
            // enough for a pressure dome; a rising sea hurts it.
            if (dir >= 0) m = -1;
            else m = t->contour - *game_sea_level < *game_dome_depth_a - *game_dome_depth_b ? 1 : 2;
        } else if (dir <= 0) {
            // A falling sea costs a base above the shore.
            if (alt > ALT_SHORE) m = -1;
        } else {
            // A rising sea: a base with a pressure dome at the shore is
            // safe; one low enough is flooded (-3); else one at the shore
            // loses and one above it gains.
            int offset;
            uint32_t mask;
            bitmask(FAC_PRESSURE_DOME, &offset, &mask);
            if (!((B->facilities_built[offset] & mask) && alt == ALT_SHORE)) {
                if (t->contour - *game_sea_level <= *game_dome_depth_a + *game_dome_depth_b) m = -3;
                else if (alt == ALT_SHORE) m = -1;
                else m = 1;
            }
        }
        int w = B->pop_size * m * 2;
        if (B->faction == faction) sum += 2 * w;
        else
            sum -= game_bit_count(status(faction, B->faction) &
                                  (TREATY_VENDETTA | TREATY_WANT_REVENGE | TREATY_ATROCITY_VICTIM |
                                   TREATY_MAJOR_ATROCITY_VICTIM)) *
                   w;
    }
    int pop = me->pop_total;
    if (pop < 1) pop = 1;
    if (pop > 99999) pop = 99999;
    return r + sum / pop;
}

// Whether anyone can build planet busters: 0 if the faction can itself
// (or no one can), else -2.
static int busters_known(int faction) {
    int missile = game_chassis[CHASSIS_MISSILE].preq_tech, buster = game_weapons[WEAPON_PLANET_BUSTER].preq_tech;
    if (has_tech(missile, faction) && has_tech(buster, faction)) return 0;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (has_tech(buster, i) || has_tech(missile, i)) return -2;
    return 0;
}

// Repealing the UN charter (dir 1), or reinstating it (dir -1), with the
// signs reversed: a faction without planet busters is against repeal when
// others can build them or have them; the peaceful, and one minded to
// both growth and research, are too; its atrocities make it for it. Once
// it is for, the warlike and the power minded are more so.
static int prop_charter(int faction, int dir) {
    const struct faction_data *me = fac(faction);
    int r = dir * busters_known(faction);
    if (!me->planet_busters) {
        if (r * dir < 0) r -= dir;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (fac(i)->planet_busters) r -= dir;
    }
    if (me->ai_fight < 0) r -= dir;
    if (me->ai_tech && me->ai_growth) r -= 2 * dir;
    r += dir * me->atrocities;
    if (r * dir < 0) return r;
    if (me->ai_fight > 0) r += dir;
    if (!me->ai_power) return r;
    return r + dir;
}

// How much `faction` wants proposal `prop` passed, for `target` where it
// names a faction (governor, Supreme Leader): positive for, negative
// against. Proposal 10 gives -9, and any other 0.
int wants_prop(int faction, int prop, int target) {
    switch (prop) {
    case PROP_GOVERNOR: return prop_governor(faction, target);
    case PROP_SUPREME_LEADER: return prop_supreme_leader(faction, target);
    case PROP_UNITY_CORE: return prop_unity_core(faction);
    case PROP_TRADE_PACT:
    case PROP_REPEAL_TRADE_PACT: return prop_trade_pact(faction, prop);
    case PROP_SOLAR_SHADE:
    case PROP_INCREASE_SHADE:
    case PROP_MELT_CAPS: return prop_sea_level(faction, prop);
    case PROP_REPEAL_CHARTER: return prop_charter(faction, 1);
    case PROP_REINSTATE_CHARTER: return prop_charter(faction, -1);
    case 10: return NOT_A_CANDIDATE;
    default: return 0;
    }
}

// The AI's vote. For governor: the faction it wants most (with a score
// above 0), or -3 for none, unless it promised its vote to a candidate it
// has no quarrel with. On a proposal `target` put forward: its promise if
// it made one, else VOTE_YEA or VOTE_NAY by how much it wants it, a
// vendetta or grudge against the proposer counting against and a pact
// for; when it is indifferent, -1 to -3 by the turn.
int council_get_vote(int faction, int prop, int target) {
    const struct faction_data *me = fac(faction);
    if (prop == PROP_GOVERNOR) {
        int best = 0, choice = VOTE_ABSTAIN;
        for (int i = 1; i < FACTION_COUNT; i++) {
            int w = wants_prop(faction, PROP_GOVERNOR, i);
            if (w > best) best = w, choice = i;
        }
        if (me->council_pledge[PROP_GOVERNOR] &&
            !(status(faction, me->council_pledge_to[PROP_GOVERNOR]) & TREATY_PLEDGE_VOID))
            return me->council_pledge_to[PROP_GOVERNOR];
        return choice;
    }
    int w = wants_prop(faction, prop, target);
    uint32_t st = status(faction, target);
    if (st & TREATY_VENDETTA) w--;
    if (st & TREATY_WANT_REVENGE) w--;
    if (st & TREATY_PACT) w++;
    if (me->council_pledge[prop]) return me->council_pledge[prop];
    if (w > 0) return VOTE_YEA;
    if (w < 0) return VOTE_NAY;
    return -1 - (*game_turn + faction) % 3;
}

// The "cannot call the Council" notice, for a human calling it on its
// own turn.
static int refuse(int faction, int tell, uint32_t label, uint32_t sound) {
    if (tell && faction == *game_current_faction && (*game_human_factions & bit(faction)))
        game_netmsg_pop(game_netmsg, (char *)game_at(label), -5000, 0, sound ? (char *)game_at(sound) : NULL);
    return 0;
}

// Whether `faction` can call the Council: not the aliens; not where the
// rules have no council, unless the scenario editor is in omniscient
// view; not during sunspots, or while a hotseat or e-mail game's council
// is pending. The Empath Guild's holder can; anyone else needs a commlink
// (or status bit 0x8000000) with every other living faction but the
// aliens. A human playing its own turn in omniscient view, or one in a
// network game missing another human's, is let off. With `tell`, a human
// told why not.
int can_call_council(int faction, int tell) {
    if (alien(faction)) return 0;
    uint32_t state = *game_state;
    if ((*game_more_rules & MORE_RULES_NO_COUNCIL) && !((state & STATE_SCENARIO_EDITOR) && (state & GAME_STATE_OMNISCIENT)))
        return 0;
    if (*game_sunspot_duration > 0) return refuse(faction, tell, 0x68c030, 0x68c020);
    if (*game_pbem && *game_council_pending) return refuse(faction, tell, 0x68c03c, 0);
    if (holds(SP_EMPATH_GUILD, faction)) return 1;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (alien(i) || !(*game_factions_alive & bit(i)) || i == faction) continue;
        if (status(faction, i) & (TREATY_COMMLINK | TREATY_UNK_8000000)) continue;
        uint8_t humans = *game_human_factions;
        if ((*game_state & GAME_STATE_OMNISCIENT) && (humans & bit(faction)) && faction == *game_current_faction)
            continue;
        if (*game_net_multiplayer && (humans & bit(faction)) && (humans & bit(i))) continue;
        return refuse(faction, tell, 0x68c058, 0x68c048);
    }
    return 1;
}

extern struct hook_module council_module;
#define COUNCIL (&council_module)
SHADOW1(COUNCIL, int, council_votes, int)
SHADOW1(COUNCIL, int, eligible, int)
SHADOW3(COUNCIL, int, wants_prop, int, int, int)
SHADOW3(COUNCIL, int, council_get_vote, int, int, int)
// Checked as a writer of nothing, so that its notice is held and the two
// runs' notices compared.
SHADOW_WRITER(COUNCIL, int, can_call_council, (int a, int b), (a, b), (void)0, "%d, %d", a, b)

// The sweep's own random numbers, so that the game's are left alone.
static uint32_t sweep_seed;
static int rnd(int n) {
    sweep_seed = sweep_seed * 1103515245u + 12345u;
    return (int)((sweep_seed >> 16) % (uint32_t)n);
}

// Every function for every faction, proposal and candidate.
static void sweep_all(void) {
    for (int f = 1; f < FACTION_COUNT; f++) {
        check_council_votes(f);
        check_eligible(f);
        check_can_call_council(f, 0);
        check_can_call_council(f, 1);
        for (int prop = -1; prop <= PROP_COUNT; prop++)
            for (int t = 1; t < FACTION_COUNT; t++) {
                check_wants_prop(f, prop, t);
                check_council_get_vote(f, prop, t);
            }
    }
}

// A different game around the Council: treaties and grudges, the
// governor and the votes cast, promises, the factions' priorities,
// standing, wealth, population and ideologies, the sea, the rules and
// game state, who is human, the Empath Guild and Clinical Immortality,
// pressure domes and who can build planet busters.
static void perturb(void) {
    static const uint32_t bits[] = {TREATY_PACT, TREATY_TREATY, TREATY_COMMLINK, TREATY_VENDETTA, TREATY_WANT_REVENGE,
                                    TREATY_UNK_40, TREATY_SHALL_BETRAY, TREATY_UNK_800, TREATY_ATROCITY_VICTIM,
                                    TREATY_MAJOR_ATROCITY_VICTIM, TREATY_SURRENDERED, TREATY_UNK_8000000};
    for (int i = 1; i < FACTION_COUNT; i++)
        for (int j = 1; j < FACTION_COUNT; j++)
            for (unsigned k = 0; k < sizeof(bits) / sizeof(bits[0]); k++)
                if (i != j && !rnd(4)) game_factions[i].diplo_status[j] ^= bits[k];
    if (rnd(3)) *game_governor_faction = rnd(9) - 1;
    for (int i = 0; i < FACTION_COUNT; i++) game_council_candidate[i] = rnd(3) ? 0 : rnd(FACTION_COUNT);
    for (int f = 1; f < FACTION_COUNT; f++) {
        struct faction_data *F = &game_factions[f];
        for (int p = 0; p < PROP_COUNT; p++) F->council_pledge[p] = rnd(5) ? 0 : p ? -1 - rnd(3) : 1;
        F->council_pledge_to[0] = rnd(FACTION_COUNT);
        if (rnd(4)) {
            F->ai_fight = rnd(3) - 1, F->ai_growth = rnd(2), F->ai_tech = rnd(2), F->ai_wealth = rnd(2);
            F->ai_power = rnd(2);
        }
        if (rnd(2)) F->ranking = rnd(8), F->diff_level = rnd(6);
        if (rnd(2)) {
            F->energy_credits = rnd(700), F->energy_surplus_total = rnd(80), F->facility_maint_total = rnd(40);
            F->tech_commerce_bonus = rnd(5), F->se_pending[SE_ECONOMY] = rnd(5) - 2;
        }
        for (int c = 0; c < 4; c++)
            if (!rnd(3)) F->se_models_pending[c] = rnd(5);
        if (!rnd(3)) F->atrocities = rnd(3);
        if (!rnd(3)) F->planet_busters = rnd(4) ? 0 : 1 + rnd(2);
        if (rnd(2)) F->mil_strength = rnd(300), F->mil_attack = rnd(150), F->pop_total = rnd(5) ? rnd(120) : 0;
        struct faction_def *D = &game_faction_defs[f];
        if (!rnd(3)) D->rule_psi = rnd(2) ? 0 : 25;
        if (!rnd(3)) {
            D->ideology_category = rnd(5) - 1, D->ideology_model = rnd(5);
            D->anti_ideology_category = rnd(5) - 1, D->anti_ideology_model = rnd(5);
        }
        if (!rnd(12)) D->rule_flags ^= RULE_FLAG_ALIEN;
        if (!rnd(6) && D->bonus_count < 8) {
            D->bonus_id[D->bonus_count] = BONUS_VOTES, D->bonus_val1[D->bonus_count] = rnd(4) - 1;
            D->bonus_count++;
        }
    }
    if (rnd(2)) *game_sea_level_council = rnd(5) - 2;
    if (!rnd(3)) *game_sea_level = rnd(5) - 2;
    if (rnd(2)) *game_human_factions = (uint8_t)(rnd(256) & rnd(256) & 0xfe);
    *game_current_faction = 1 + rnd(7);
    if (!rnd(4)) *game_net_multiplayer = !*game_net_multiplayer;
    if (!rnd(3)) *game_pbem = !*game_pbem;
    *game_council_pending = rnd(2);
    if (!rnd(4)) *game_sunspot_duration = rnd(3);
    if (!rnd(3)) *game_state ^= STATE_SCENARIO_EDITOR;
    if (!rnd(3)) *game_state ^= GAME_STATE_OMNISCIENT;
    if (!rnd(3)) *game_more_rules ^= MORE_RULES_NO_COUNCIL;
    if (rnd(2)) *game_rules ^= RULES_INTENSE_RIVALRY;
    if (rnd(2)) *game_rules ^= RULES_VICTORY_COOPERATIVE;
    if (!rnd(6)) *game_expansion = !*game_expansion;
    if (rnd(2)) *game_turn = 150 + rnd(100);
    if (!rnd(4)) *game_factions_alive &= (uint8_t)~bit(1 + rnd(7));
    if (rnd(2)) game_ranked_faction[0] = 1 + rnd(7);
    for (int k = 7; k > 0 && rnd(2); k--) {  // the rankings shuffled
        int j = rnd(k + 1), x = game_faction_rankings[k];
        game_faction_rankings[k] = game_faction_rankings[j], game_faction_rankings[j] = x;
    }
    int n = *game_base_count;
    if (rnd(2)) game_secret_projects[SP_EMPATH_GUILD] = rnd(3) ? rnd(n) : -1;
    if (rnd(2)) game_secret_projects[SP_CLINICAL_IMMORTALITY] = rnd(3) ? rnd(n) : -1;
    int offset;
    uint32_t mask;
    bitmask(FAC_PRESSURE_DOME, &offset, &mask);
    for (int b = 0; b < n; b++)
        if (!rnd(4)) game_bases[b].facilities_built[offset] ^= (uint8_t)mask;
    // The techs for missiles and planet busters: none needed, or one some
    // factions may have.
    if (rnd(2)) game_chassis[CHASSIS_MISSILE].preq_tech = (int16_t)(rnd(3) ? rnd(TECH_COUNT) : TECH_NONE);
    if (rnd(2)) game_weapons[WEAPON_PLANET_BUSTER].preq_tech = (int16_t)(rnd(3) ? rnd(TECH_COUNT) : TECH_NONE);
}

// The game as it is, then a dozen variations of it, on a protected copy
// that is put back after.
static void council_sweep(void) {
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    sweep_all();
    for (int v = 0; v < 12; v++) {
        sweep_seed = (uint32_t)*game_turn * 7919u + (uint32_t)v;
        perturb();
        sweep_all();
    }
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook council_hooks[] = {
    HOOK(0x52ad30, council_votes, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x88, 0x64, 0x9a, 0x00),  // mov eax, [0x9a6488]
    HOOK(0x52ae20, eligible, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0x88, 0x64, 0x9a, 0x00),  // push ecx; mov eax, [0x9a6488]
    HOOK(0x52aeb0, wants_prop, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),                  // sub esp, 0x1c
    HOOK(0x52be60, council_get_vote, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),      // push ebx; mov ebx, [ebp+12]
    HOOK(0x52c670, can_call_council, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),            // mov eax, [ebp+8]
};
struct hook_module council_module = {"council", council_hooks, HOOK_COUNT(council_hooks), council_sweep, NULL, -1};
