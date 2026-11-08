// The Planetary Council: each faction's votes, who may stand for governor,
// the AI's stand on each proposal and its vote, and whether the Council can
// be called. Reimplemented from terranx.exe 2.0's disassembly; names follow
// OpenSMACX (Brendan Casey, GPL-3.0-or-later) and Thinker (MIT). Neither
// has wants_prop(), council_get_vote() or can_call_council().
#include "council.h"

#include <stdint.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "quiet.h"
#include "score.h"
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

// ---- The Council's acts ----------------------------------------------------------
//
// A faction puts a proposal to the Council in call_council() below. The
// session itself (the window, each faction's vote, the tally and the
// result) is the game's own council() and its window's methods, which call
// council_action() for a proposal passed and buy_council_vote() when the
// player asks a leader for its vote.

enum {
    RULES_UNK_2 = 0x2,               // the wronged may object to a Supreme Leader
    RULES_VICTORY_DIPLOMATIC = 0x8,  // the Supreme Leader proposal may be put
    GAME_STATE_UNK_200000 = 0x200000,
    MORE_RULES_UNK_10 = 0x10,  // a network game's council waits on one player (game_at 0x9a6820)
    FACTION_FLAG_SUPREME_LEADER = 0x20000,
    VICTORY_DIPLOMATIC = 6, VICTORY_COOPERATIVE = 0xa, VICTORY_LOST = 0xb,  // game_victory_type
    AGENDA_UNK_8 = 0x8, AGENDA_UNK_2000 = 0x2000,
    UNITY_CORE_CREDITS = 500,
    // In the popup object.
    POP_SPRITE = 0x2118, POP_ICON = 0x2144, POP_DIALOGS = 0x21d0, POP_SPRITE_BOX = 0x2240, POP_HAS_ICON = 0x2274,
};

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline int human(int f) { return (*game_human_factions & bit(f)) != 0; }
static inline int living(int f) { return (*game_factions_alive & bit(f)) != 0; }
static int popp(uint32_t label, uint32_t picture) {
    return game_popp(*game_script_file, text(label), 0, text(picture), 0);
}
static void notice(uint32_t label, int delay, uint32_t sound) {
    game_netmsg_pop(game_netmsg, text(label), delay, 0, sound ? text(sound) : NULL);
}
// A leader's title and name, and a faction's noun, into the text
// variables. `f` may be below 1 (buy_council_vote()).
static void say_title(int slot, int f) {
    *game_parse_plural = 0;
    *game_parse_gender = def(f)->leader_female;
    game_parse_says(slot, (char *)def(f)->leader_title, -1, -1);
}
static void say_name(int slot, int f) {
    *game_parse_gender = def(f)->leader_female;
    *game_parse_plural = 0;
    game_parse_says(slot, (char *)def(f)->leader_name, -1, -1);
}
static void say_noun(int slot, int f) {
    *game_parse_gender = def(f)->noun_gender;
    *game_parse_plural = def(f)->noun_plural;
    game_parse_says(slot, (char *)def(f)->noun, -1, -1);
}
static void say_leader(int f) { say_title(0, f), say_name(1, f), say_noun(2, f); }

// The popup the originals keep on their stacks, destroyed as they inline
// it.
static void pop_end(uint8_t *pop) {
    *(uint32_t *)pop = game_va(0x6695c8);
    *(uint32_t *)(pop + 0x444) = game_va(0x6695c0);
    game_popup_close(pop);
    *(uint32_t *)(pop + 0x3230) = game_va(0x669d58);
    *(uint32_t *)(pop + 0x3674) = game_va(0x669d50);
    game_scroll_close(pop + 0x3230);
    game_flat_button_dtor(pop + 0x4828);
    game_flat_button_dtor(pop + 0x3cdc);
    game_graphic_win_dtor(pop + 0x3230);
    game_base_pop_dtor(pop);
}

// The sea moved by the Council: 3 lower for a solar shade (dir -1), 3
// higher for melting the caps, the coming change from warming moved the
// same way, and the planet monitor and the player told.
static void sea_change(int dir) {
    int step = *game_dome_depth_b;
    *game_sea_level_council += 3 * dir;
    *game_climate_change += dir * step;
    int change = *game_climate_change;
    game_climate_shown[0] = change < 0 ? -change : change;
    game_climate_shown[1] = 20, game_climate_shown[2] = 0;
    game_parse_num(0, 20);
    game_parse_num(1, step * 1000 / 15);
    int al = alien(*game_current_faction);
    if (dir < 0) popp(al ? 0x68bf54 : 0x68bf74, al ? 0x68bf44 : 0x68bf64);
    else popp(al ? 0x68bf90 : 0x68bfb0, al ? 0x68bf80 : 0x68bfa0);
}

// The player refuses a Supreme Leader: every faction but the player's
// partners that surrendered to it in a pact declares vendetta on the
// player, and the rest side with the Supreme Leader.
static void resist(int target) {
    popp(0x68c018, 0x68c008);
    for (int i = 1; i < FACTION_COUNT; i++) {
        int cur = *game_current_faction;
        if (i == cur || !living(i)) continue;
        uint32_t st = status(i, cur);
        if ((st & TREATY_PACT) && (st & TREATY_SURRENDERED)) continue;
        treaty_on(i, cur, TREATY_VENDETTA | TREATY_UNK_40);
        if (i != target) {
            treaty_on(i, target, TREATY_PACT);
            agenda_on(i, target, AGENDA_UNK_2000);
        }
        game_set_agenda(i, *game_current_faction, AGENDA_UNK_8, 1);
    }
}

// A Supreme Leader elected. The faction playing its turn: under the rule
// that lets them, the factions it wronged by atrocities (or with agenda
// bits 0x8 or 0x800 towards it) object, one message each, and declare
// vendetta; then every other faction without bit 0x8 makes peace and
// surrenders to it, and the game goes on. With no objection (or in a
// network game) it wins. Another: the player is asked whether to accept
// it (a pact partner's under the cooperative victory rule; in a network
// game it is accepted): accepted, the game is over; refused, resist().
static void supreme_leader(int target) {
    *game_state |= GAME_STATE_UNK_200000;
    if (target == *game_current_faction) {
        if (!*game_net_multiplayer) {
            int objected = 0;
            for (int i = 1; i < FACTION_COUNT; i++) {
                if (i == target || !living(i) || !(*game_rules & RULES_UNK_2)) continue;
                uint32_t agenda = fac(i)->diplo_agenda[target];
                if (!(status(i, target) & (TREATY_ATROCITY_VICTIM | TREATY_MAJOR_ATROCITY_VICTIM)) &&
                    !(status(target, i) & TREATY_MAJOR_ATROCITY_VICTIM) && !(agenda & AGENDA_UNK_800) &&
                    !(agenda & AGENDA_UNK_8))
                    continue;
                objected = 1;
                say_leader(i);
                popp(0x68bfcc, 0x68bfbc);
                treaty_on(i, target, TREATY_VENDETTA);
                agenda_on(i, target, AGENDA_UNK_2000 | AGENDA_UNK_8);
            }
            if (objected) {
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (i == target || !living(i) || (fac(i)->diplo_agenda[target] & AGENDA_UNK_8)) continue;
                    treaty_on(i, target, TREATY_PACT | TREATY_TREATY | TREATY_UNK_40);
                    treaty_off(i, target, TREATY_UNK_800 | TREATY_SHALL_BETRAY | TREATY_WANT_REVENGE);
                    agenda_off(i, target, AGENDA_UNK_800 | AGENDA_UNK_400);
                    set_treaty(i, target, TREATY_SURRENDERED, 1);
                }
                return;
            }
        }
        *game_victory_type = VICTORY_DIPLOMATIC;
        fac(target)->flags |= FACTION_FLAG_SUPREME_LEADER;
        *game_state |= GAME_STATE_DONE;
        return;
    }
    int coop = (*game_rules & RULES_VICTORY_COOPERATIVE) && (status(*game_current_faction, target) & TREATY_PACT);
    say_leader(target);
    if (!*game_net_multiplayer && popp(coop ? 0x68bfe4 : 0x68c000, coop ? 0x68bfd4 : 0x68bff0)) {
        resist(target);
        return;
    }
    *game_state |= GAME_STATE_DONE;
    *game_victory_type = coop ? VICTORY_COOPERATIVE : VICTORY_LOST;
    fac(target)->flags |= FACTION_FLAG_SUPREME_LEADER;
}

// A proposal passed: counted, its turn noted, then its effect. The
// governor, the trade pact and the charter need nothing more here (the
// window and the rules read the counts); salvaging the Unity core gives
// every faction but the aliens 500 credits.
void council_action(int prop, int target) {
    game_proposal_passes[prop]++;
    game_proposal_turn[prop] = *game_turn;
    switch (prop) {
    case PROP_SUPREME_LEADER:
        supreme_leader(target);
        return;
    case PROP_UNITY_CORE:
        for (int i = 1; i < FACTION_COUNT; i++)
            if (!alien(i)) fac(i)->energy_credits += UNITY_CORE_CREDITS;
        game_console_update_data(game_console, 0);
        return;
    case PROP_SOLAR_SHADE:
    case PROP_INCREASE_SHADE:
        sea_change(-1);
        return;
    case PROP_MELT_CAPS:
        sea_change(1);
        return;
    default:
        return;
    }
}

// ---- Calling the Council -------------------------------------------------------------

// Whether `faction` may put `prop`. The governor: a human any time; the AI
// when it is eligible or there is no governor, it is not governor itself,
// it isn't a pact partner of a governor ranked above it or one it has
// surrendered to, and either half the votes would be cast for it or there
// is no governor or a human one. The Supreme Leader: under the diplomatic
// victory rule, while the game goes on, not passed before, by an eligible
// faction (noting whether an alien faction lives). The rest by what the
// Council has passed before.
static int offered(int faction, int prop, int32_t *aliens) {
    const int32_t *passes = game_proposal_passes;
    switch (prop) {
    case PROP_GOVERNOR: {
        int me = human(faction), gov = *game_governor_faction;
        if (!eligible(faction) && !me && gov > 0) return 0;
        if (gov == faction && !me) return 0;
        if (me) return 1;
        if (gov > 0 && (status(faction, gov) & TREATY_PACT)) {
            if (fac(faction)->ranking < fac(gov)->ranking) return 0;
            if (status(faction, gov) & TREATY_SURRENDERED) return 0;
        }
        int total = 0, mine = 0;
        for (int i = 1; i < FACTION_COUNT; i++) {
            total += council_votes(i);
            if (council_get_vote(i, PROP_GOVERNOR, faction) == faction) mine += council_votes(i);
        }
        if (2 * mine >= total) return 1;
        gov = *game_governor_faction;
        return gov <= 0 || human(gov);
    }
    case PROP_SUPREME_LEADER:
        if (!(*game_rules & RULES_VICTORY_DIPLOMATIC) || (*game_state & GAME_STATE_DONE) || passes[prop] ||
            !eligible(faction))
            return 0;
        *aliens = 0;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (alien(i) && living(i)) *aliens = 1;
        return 1;
    case PROP_UNITY_CORE:
    case PROP_SOLAR_SHADE: return !passes[prop];
    case PROP_TRADE_PACT: return passes[PROP_TRADE_PACT] <= passes[PROP_REPEAL_TRADE_PACT];
    case PROP_REPEAL_TRADE_PACT: return passes[PROP_REPEAL_TRADE_PACT] != passes[PROP_TRADE_PACT];
    case PROP_INCREASE_SHADE: return passes[PROP_SOLAR_SHADE] != 0;
    case PROP_REPEAL_CHARTER: return passes[PROP_REPEAL_CHARTER] <= passes[PROP_REINSTATE_CHARTER];
    case PROP_REINSTATE_CHARTER: return passes[PROP_REINSTATE_CHARTER] < passes[PROP_REPEAL_CHARTER];
    default: return 1;
    }
}

static int omniscient_debug(void) {
    return (*game_state & GAME_STATE_OMNISCIENT) && (*game_state & GAME_STATE_DEBUG);
}

// A faction calls the Council: the proposals it may put are listed (from
// which a human chooses; the AI takes the one it wants most, above
// nothing), the waits between its calls (but for an election; half for
// the governor) and between a proposal's last putting or passing and its
// return are kept, and the session opens (in a network game,
// through the other players). The AI calls only one turn in 16 while there
// is a governor, never once the game is over, nor while a human faction is
// alien.
//
// `aliens` is the original's local for an alien faction living. Only
// weighing the Supreme Leader proposal sets it; before that it holds the
// caller's ECX, which the stack probe pushes there (see call_council
// below), and while it is non-zero the AI puts nothing but the governor.
// faction_upkeep() calls it with the human factions in ECX, so in a game
// with a human player the AI proposes nothing but elections, until it can
// put the Supreme Leader (then the word says whether an alien faction
// lives). OpenSMACX and Thinker have no call_council().
static void council_call_body(uint8_t *pop, int faction, int32_t aliens) {
    if (*game_expansion && alien(faction)) return;
    if (!faction) return;
    int me = human(faction);
    if (*game_control_turn && (me || *game_net_multiplayer)) return;
    if (!can_call_council(faction, faction == *game_current_faction && me)) return;
    if (!me && *game_governor_faction > 0 && ((*game_turn + 2 * faction) & 0xf)) return;
    if (!me && (*game_state & GAME_STATE_DONE)) return;
    if (*game_net_multiplayer && (*game_more_rules & MORE_RULES_UNK_10) && me &&
        *(int32_t *)game_at(0x9a6820) != faction) {
        notice(0x68c068, 5000, 0);
        return;
    }
    for (int i = 1; i < FACTION_COUNT; i++)
        if (!me && human(i) && alien(i)) return;

    int best = 0, choice = -1, target = faction, listed = 0;
    game_popup_start(pop, game_pop_file, text(0x68c070), -1, 0, 0x40, 0);
    int w, h;
    if (!game_pcx_dimensions(text(0x68c080), &w, &h) && !game_sprite_init(pop + POP_SPRITE, text(0x68c090), w, h))
        *(void **)(pop + POP_ICON) = pop + POP_SPRITE;
    for (int prop = 0; prop < PROP_COUNT; prop++) {
        const struct proposal *p = &game_proposals[prop];
        if (!has_tech(p->preq_tech, faction) && !omniscient_debug()) continue;
        if (p->preq_tech < TECH_NONE) continue;
        if (!offered(faction, prop, &aliens)) continue;
        char *bt = game_battle_text;
        bt[0] = 0;
        strcat(bt, game_strings_get(game_strings, p->name));
        strcat(bt, text(0x68c0a0));
        strcat(bt, game_strings_get(game_strings, p->description));
        game_dialogs_item(pop + POP_DIALOGS, bt, prop);
        listed++;
        if (me) continue;
        // The AI: a vote it promised for governor to someone eligible
        // stands, and a promise on another proposal keeps it from putting
        // it.
        const struct faction_data *F = fac(faction);
        if (F->council_pledge[prop]) {
            if (prop || eligible(F->council_pledge_to[PROP_GOVERNOR])) continue;
        } else if (prop) {
            int want = wants_prop(faction, prop, faction);
            if (!aliens && want > best) best = want, choice = prop, target = faction;
            continue;
        }
        // The candidate it wants most, but the governor.
        for (int i = 1; i < FACTION_COUNT; i++) {
            if (i == *game_governor_faction || (*game_expansion && alien(i)) || !eligible(i)) continue;
            int want = wants_prop(faction, PROP_GOVERNOR, i);
            if (want > best) best = want, choice = PROP_GOVERNOR, target = i;
        }
    }
    if (!listed) return;
    if (me) choice = game_base_pop_exec_3(pop, 0, 0);
    int cur = *game_current_faction;
    if (aliens && choice == PROP_SUPREME_LEADER) {
        if (faction == cur && me) notice(0x68c0b4, -5000, 0x68c0a4);
        return;
    }
    if (choice < 0) return;
    int turn = *game_turn, wait = game_rules_basic->min_turns_councils;
    if (choice != PROP_GOVERNOR && !(omniscient_debug() && me)) {
        // A governor may call twice as often.
        int limit = faction == *game_governor_faction ? wait / 2 : wait;
        if (turn - fac(faction)->council_call_turn < limit) {
            if (faction == cur && me) {
                game_parse_num(0, game_year(fac(cur)->council_call_turn));
                game_parse_num(1, game_rules_basic->min_turns_councils);
                game_parse_num(2, game_rules_basic->min_turns_councils / 2);
                notice(0x68c0d8, -5000, 0x68c0c8);
            }
            return;
        }
    }
    int last = game_proposal_turn[choice];
    if (turn - last < wait && !(omniscient_debug() && me)) {
        if (faction == cur && me) {
            game_parse_num(0, game_year(last));
            game_parse_num(1, game_rules_basic->min_turns_councils);
            notice(0x68c0fc, -5000, 0x68c0ec);
        }
        return;
    }
    if (!*game_net_multiplayer) {
        game_council(faction, choice, target);
        return;
    }
    if (!me) {
        game_council_net_call[0] = faction, game_council_net_call[1] = choice, game_council_net_call[2] = target;
        return;
    }
    notice(0x68c110, 3000, 0);
    if (game_diplo_lock(0x32)) {
        game_netmsg_close(game_netmsg);
        return;
    }
    game_message_data(0x2600, 0, faction, choice, target, 0);
    *game_council_session = 1;
    while (!*game_control_turn_a) {
        game_wait_loop();
        if (!*game_council_session) break;
    }
    game_diplo_unlock();
}
void call_council_in(uint8_t *pop, int faction, int32_t aliens);
void call_council_in(uint8_t *pop, int faction, int32_t aliens) {
    game_popup_ctor(pop);
    council_call_body(pop, faction, aliens);
    pop_end(pop);
}

// The entry: the original's unset local ([ebp-0x14]) is where its stack
// probe (_chkstk, for its 21 KB frame) pushes ECX, so it holds the caller's
// ECX; and the popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl call_council\n"
        "call_council:\n"
        "    movl %ecx, %eax\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x53c0, %esp\n"
        "    pushl %eax\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x53b4(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call call_council_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");
__asm__(".text\n"
        ".globl call_council_ecx\n"
        "call_council_ecx:\n"
        "    movl 8(%esp), %ecx\n"
        "    pushl 4(%esp)\n"
        "    movl game_call_council, %eax\n"
        "    call *%eax\n"
        "    addl $4, %esp\n"
        "    ret\n");

// ---- Buying a vote ----------------------------------------------------------------------

// The techs on offer (tech_analysis()'s four for the AI), leading the list.
static int techs_offered(void) {
    int n = 0;
    while (n < 4 && *game_trade_want_tech[n] >= 0) n++;
    return n;
}

// The player (`player`) asks the AI leader `ai` for its vote on `prop`:
// for `target` on the governor, or on another proposal VOTE_YEA, VOTE_NAY
// or VOTE_ABSTAIN. The AI refuses a faction it has a vendetta or grudge
// with or that wronged it, keeps a vote promised to someone it has no
// quarrel with, and refuses one far against its mind (below -3; for
// exactly -3 on a proposal it offers to abstain, at triple). Otherwise it
// names a price by how far the vote is from what it wants (1 to 4 times):
// the two factions' energy incomes (at least their base counts), and a
// fifth of the player's reserves, halved when the AI fears the player
// (and halved again oddly when that flag is at least the AI's base count),
// doubled for the governor's vote or the vote of the strongest for itself,
// three quarters outside a session; nothing from a pact partner that
// surrendered to the player. It may take techs instead, those the player
// could give it worth three quarters of the price. A vote the AI casts
// already is only told (and in a network game promised to a treaty
// partner). The player chooses; the vote is promised, and in a session
// cast. OpenSMACX doesn't have it.
static void buy_vote_body(uint8_t *pop, int player, int ai, int prop, int target) {
    *game_diplo_second = ai;
    diplomacy_check(player, ai, 0);
    game_diplomacy_caption(player, ai);
    tech_analysis(player, ai);
    if (*game_expansion && (alien(player) || alien(ai))) return;
    struct faction_data *A = fac(ai), *P = fac(player);
    uint32_t st = A->diplo_status[player];
    if (st & (TREATY_VENDETTA | TREATY_WANT_REVENGE | TREATY_ATROCITY_VICTIM)) {
        if (*game_council_session) {
            say_title(0, ai), say_name(1, ai);
            game_x_pop(text(0x68d6ac), (int)(intptr_t)game_pop_wait);
        }
        return;
    }
    int want = wants_prop(ai, prop, target > 0 ? target : player);
    if (prop != PROP_SUPREME_LEADER && *game_diplo_fear) want++;
    if (target == VOTE_NAY) want = -want;
    if (prop == PROP_GOVERNOR) {
        // How far the candidate falls behind any other it might vote for.
        int least = want;
        for (int i = 1; i < FACTION_COUNT; i++) {
            if (i == target || !eligible(i)) continue;
            if (least >= want - wants_prop(ai, PROP_GOVERNOR, i)) least = want - wants_prop(ai, PROP_GOVERNOR, i);
        }
        want = least;
    }
    if (A->council_pledge[prop]) {
        int to = A->council_pledge_to[prop];
        if (!(A->diplo_status[to] & (TREATY_VENDETTA | TREATY_WANT_REVENGE))) {
            say_title(0, to), say_name(1, to);
            game_x_pops_11(text(0x68d6b8), 0x100000, game_faction_portraits[ai], game_pop_wait);
            return;
        }
    }
    // A positive economy keeps a proposal past Supreme Leader from more
    // than -2 against, without a grudge; the price below still goes by
    // `want`.
    int vote = want;
    if (A->se_base[SE_ECONOMY] > 0 && prop > PROP_SUPREME_LEADER && !(st & TREATY_WANT_REVENGE) && vote < -2) vote = -2;
    if (*game_council_session && target == game_council_candidate[ai]) {
        say_title(0, player), say_name(1, player);
        game_x_pop(text(0x68d6c4), (int)(intptr_t)game_pop_wait);
        if (*game_net_multiplayer && (P->diplo_status[ai] & TREATY_TREATY) && !(st & TREATY_WANT_REVENGE)) {
            A->council_pledge[prop] = target;
            A->council_pledge_to[prop] = prop ? player : target;
            game_synch_leader(ai);
        }
        return;
    }
    int mult, ahead = 0;
    if ((st & TREATY_PACT) && (st & TREATY_SURRENDERED)) {
        mult = 0;
    } else if (prop == PROP_GOVERNOR && target == ai) {
        if (vote < 0) game_x_pops_11(text(0x68d6d0), 0x100000, game_faction_portraits[ai], game_pop_wait);
        return;
    } else if (vote < -3) {
        game_x_pops_11(text(0x68d6d8), 0x100000, game_faction_portraits[ai], game_pop_wait);
        return;
    } else if (vote == -3) {
        if (prop) target = VOTE_ABSTAIN, mult = 3;
        else mult = 4;
    } else if (vote == -2) {
        mult = 3;
    } else {
        mult = vote == -1 ? 2 : 1;
    }
    if (prop == PROP_GOVERNOR)
        for (int i = 1; i < FACTION_COUNT; i++)
            if (i != ai && council_votes(i) > council_votes(ai)) ahead++;
    int mine = P->energy_surplus_total - P->facility_maint_total;
    int theirs = A->energy_surplus_total - A->facility_maint_total;
    if (mine < P->base_count) mine = P->base_count;
    if (theirs < A->base_count) theirs = A->base_count;
    int price = P->energy_credits / 5 + 2 * ((mine + theirs) * mult);
    int fear = *game_diplo_fear;
    if (fear) price /= 2;
    if (fear >= A->base_count) price /= 2;
    if (ai == *game_governor_faction) price *= 2;
    if (!*game_council_session) price = price * 3 / 4;
    if (!prop && !ahead && game_council_candidate[ai] == ai) price *= 2;
    // The techs worth three quarters of it to the AI (more as the game
    // goes on), as few as will do, or none.
    int32_t **techs = game_trade_want_tech;
    if (*techs[0] >= 0) {
        int need = price * 3 / 4, worth = 0, k = 0;
        for (; k < 4; k++) {
            if (*techs[k] < 0) break;
            worth += tech_val(*techs[k], ai, 1) * ((*game_turn + 7) / 8);
            if (worth >= need) {
                k++;
                break;
            }
        }
        for (; k < 4; k++) *techs[k] = -1;
        if (worth < need)
            for (int j = 0; j < 4; j++) *techs[j] = -1;
    }
    char *bt = game_battle_text;
    bt[0] = 0;
    strcat(bt, text(0x68d6e0));
    if (!price) {
        // For nothing: the label's own number.
        strcat(bt, "0");
        say_title(0, player), say_name(1, player);
        game_x_pops_11(bt, 0x100000, game_faction_portraits[ai], game_pop_wait);
    } else {
        if (!prop) strcat(bt, text(0x68d6f8));
        else if (target == VOTE_ABSTAIN) strcat(bt, text(0x68d6f0));
        else if (target == VOTE_NAY) strcat(bt, text(0x68d6ec));
        else if (target == VOTE_YEA) strcat(bt, text(0x68d6e8));
        say_title(2, player), say_name(3, player);
        // The candidate's: on a proposal the original reads the faction
        // definitions at -1 to -3, before the table.
        say_title(6, target), say_name(8, target);
        game_parse_num(0, price);
        int n = techs_offered();
        if (n) {
            static const int slots[4] = {0, 1, 4, 5};
            static const uint32_t labels[4] = {0x68d708, 0x68d704, 0x68d700, 0x68d6fc};
            for (int k = 0; k < n; k++) game_parse_say(slots[k], (int)(intptr_t)game_tech[*techs[k]].name, -1, -1);
            strcat(bt, text(labels[n - 1]));
        }
        if (!prop && target > 0 && target != player) strcat(bt, text(0x68d70c));
        game_popup_start(pop, game_pop_file, bt, -1, 0, 0x100000, 0);
        void *portrait = game_faction_portraits[ai];
        *(void **)(pop + POP_ICON) = *(int32_t *)(pop + POP_HAS_ICON) ? NULL : portrait;
        game_sprite_box_sprite(pop + POP_SPRITE_BOX, portrait, 0, 0);
        if (game_x_text_open(*game_script_file, text(0x68d710))) return;
        // Never mind; the energy, if the player has it; or the techs, all
        // of them.
        for (int item = 0; item < 6; item++) {
            game_text_get();
            bt[0] = 0;
            game_parse_string(*game_text_line, bt);
            int offer = item == 0 || (item == 1 ? P->energy_credits >= price : techs_offered() == item - 1);
            if (offer) game_dialogs_item(pop + POP_DIALOGS, bt, item);
        }
        int choice = game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait);
        if (choice <= 0) return;
        if (choice == 1) {
            if (*game_net_multiplayer) {
                game_log_say(text(0x68d4bc), player, ai, -price);
                game_message_data(0x2447, 0, player, -price, ai, price);
                game_net_daemon_await_diplo(game_net_daemon, 0x447);
            } else {
                if (player) P->energy_credits -= price;
                if (ai) A->energy_credits += price;
                game_console_update_data(game_console, 1);
            }
        } else {
            for (int k = 0; k < 4 && choice >= k + 2; k++) {
                int tech = *techs[k];
                if (*game_net_multiplayer) {
                    game_log_say(text(0x68d4f0), ai, tech, player);
                    game_message_data(0x244b, 0, ai, tech, player, 0);
                    game_net_daemon_await_diplo(game_net_daemon, 0x44b);
                } else {
                    game_tech_achieve(ai, tech, player, 0);
                }
            }
        }
        for (int k = 3; k >= 0; k--) *techs[k] = -1;
    }
    A->council_pledge[prop] = target;
    A->council_pledge_to[prop] = prop ? player : target;
    if (*game_net_multiplayer) {
        game_synch_leader(ai);
        if (*game_council_session) game_message_data(0x2601, 0, ai, target, 0, 0);
    } else if (*game_council_session) {
        game_council_win_set_vote(game_council_win, ai, target);
    }
}
void buy_council_vote_in(uint8_t *pop, int player, int ai, int prop, int target);
void buy_council_vote_in(uint8_t *pop, int player, int ai, int prop, int target) {
    game_popup_ctor(pop);
    buy_vote_body(pop, player, ai, prop, target);
    pop_end(pop);
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl buy_council_vote\n"
        "buy_council_vote:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5400, %esp\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x53e8(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call buy_council_vote_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

extern struct hook_module council_module;
#define COUNCIL (&council_module)
SHADOW1(COUNCIL, int, council_votes, int)
SHADOW1(COUNCIL, int, eligible, int)
SHADOW3(COUNCIL, int, wants_prop, int, int, int)
SHADOW3(COUNCIL, int, council_get_vote, int, int, int)
// Checked as a writer of nothing, so that its notice is held and the two
// runs' notices compared.
SHADOW_WRITER(COUNCIL, int, can_call_council, (int a, int b), (a, b), (void)0, "%d, %d", a, b)

// ---- Shadow mode: the acts ------------------------------------------------------------
//
// council_action() is checked on all of the game's state, its messages held
// and compared, unless the player is asked to accept another's Supreme
// Leader: a held question answers 0 (accept), so that call runs the
// original unchecked. The sweep answers both ways (quiet_answer).
SHADOW_WRITER_CHECK(void, council_action, (int a, int b), (a, b), orig_council_action(a, b), hooks_writes_game(&w_),
                    "%d, %d", a, b)
static void shadow_council_action(int prop, int target) {
    if (hooks_nested) {
        orig_council_action(prop, target);
        return;
    }
    if (shadow_bypass()) {
        council_action(prop, target);
        return;
    }
    stats_council_action.calls++;
    shadow_tick(COUNCIL);
    if (prop == PROP_SUPREME_LEADER && target != *game_current_faction && !*game_net_multiplayer) {
        orig_council_action(prop, target);
        return;
    }
    check_council_action(prop, target);
}

// Both runs of a check get the same ECX, where call_council() finds its
// unset local: council_call() makes the call with `word` in ECX (the
// arguments after the first are for buy_council_vote()), at the same
// stack depth each time, so the popup on the callee's stack is at the
// same address too.
void council_call(void *fn, int a, int b, int c, int d, int32_t word);
__asm__(".text\n"
        "council_call:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl 24(%ebp)\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    movl 28(%ebp), %ecx\n"
        "    call *8(%ebp)\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// A call is checked for the AI outside network and e-mail games, with the
// session itself (council()) held: the two runs' calls of it are compared,
// and the original's is made after the check. A human chooses the
// proposal in a menu whose answer a check can't give, so its call runs the
// original unchecked; the sweep checks it with each answer.
static struct hook_stats stats_call_council = {"call_council"};
static void (*orig_call_council)(int faction);
void call_council(int faction);
static void check_call_council(int faction, int32_t word) {
    struct hook_writes w_ = {0};
    hooks_writes_game(&w_);
    hooks_writes_begin(&w_);
    hooks_nested++;
    council_call((void *)orig_call_council, faction, 0, 0, 0, word);
    hooks_nested--;
    hooks_writes_middle(&w_);
    council_call((void *)call_council, faction, 0, 0, 0, word);
    const char *diff_ = hooks_writes_end(&w_, 0, 0);
    if (diff_) hooks_mismatch(&stats_call_council, "(%d, word %d) %s", faction, word, diff_);
    hooks_ui_release();
}
static int call_checkable(int faction) { return !*game_net_multiplayer && !*game_pbem && !human(faction); }
void shadow_call_council_in(int faction, int32_t word);
void shadow_call_council_in(int faction, int32_t word) {
    if (hooks_nested) {
        council_call((void *)orig_call_council, faction, 0, 0, 0, word);
        return;
    }
    if (shadow_bypass()) {
        council_call((void *)call_council, faction, 0, 0, 0, word);
        return;
    }
    stats_call_council.calls++;
    shadow_tick(COUNCIL);
    if (!call_checkable(faction)) {
        council_call((void *)orig_call_council, faction, 0, 0, 0, word);
        return;
    }
    check_call_council(faction, word);
}
void shadow_call_council(void);
__asm__(".text\n"
        "shadow_call_council:\n"
        "    pushl %ecx\n"
        "    pushl 8(%esp)\n"
        "    call shadow_call_council_in\n"
        "    addl $8, %esp\n"
        "    ret\n");

// The player buying a vote is asked what to pay, in a menu read from
// script.txt: the game's calls run the original unchecked. The sweep
// checks them with the menu's lines left unread and each answer.
static struct hook_stats stats_buy_council_vote = {"buy_council_vote"};
static void (*orig_buy_council_vote)(int player, int ai, int prop, int target);
void buy_council_vote(int player, int ai, int prop, int target);
static void check_buy_council_vote(int player, int ai, int prop, int target) {
    struct hook_writes w_ = {0};
    hooks_writes_game(&w_);
    hooks_writes_begin(&w_);
    hooks_nested++;
    council_call((void *)orig_buy_council_vote, player, ai, prop, target, 0);
    hooks_nested--;
    hooks_writes_middle(&w_);
    council_call((void *)buy_council_vote, player, ai, prop, target, 0);
    const char *diff_ = hooks_writes_end(&w_, 0, 0);
    if (diff_)
        hooks_mismatch(&stats_buy_council_vote, "(%d, %d, %d, %d; answer %d) %s", player, ai, prop, target, quiet_answer,
                       diff_);
    hooks_ui_release();
}
static void shadow_buy_council_vote(int player, int ai, int prop, int target) {
    if (hooks_nested) {
        orig_buy_council_vote(player, ai, prop, target);
        return;
    }
    if (shadow_bypass()) {
        buy_council_vote(player, ai, prop, target);
        return;
    }
    stats_buy_council_vote.calls++;
    shadow_tick(COUNCIL);
    orig_buy_council_vote(player, ai, prop, target);
}

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


// ---- Sweep: the acts -----------------------------------------------------------------
//
// Councils are seldom called, so each turn the acts are checked on
// protected copies of the game, made different around the Council: who is
// human and whose turn it is, treaties and agendas, the governor, what
// has passed and when, promises, reserves and incomes, the rules, and the
// answer to a held question or menu. Network games aren't swept (their
// sessions wait on other players).

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static inline int coin(uint32_t h, int bit_, int n) { return (int)((h >> bit_) % (uint32_t)n) == 0; }
static inline int pick(uint32_t h, int bit_, int n) { return (int)((h >> bit_) % (uint32_t)n); }

// A living faction, from h; 0 if none lives.
static int living_faction(uint32_t h) {
    for (int k = 0; k < 7; k++) {
        int f = 1 + (int)((h + (uint32_t)k) % 7);
        if (living(f)) return f;
    }
    return 0;
}

static void case_begin(struct hook_writes *w) {
    *w = (struct hook_writes){.protect_only = 1};
    hooks_writes_game(w);
    hooks_writes_begin(w);
}

// Treaties, agendas and atrocities among the living, both ways where the
// game keeps them so.
static void vary_relations(uint32_t h) {
    static const uint32_t both[] = {TREATY_PACT, TREATY_TREATY, TREATY_COMMLINK, TREATY_VENDETTA};
    static const uint32_t one[] = {TREATY_WANT_REVENGE, TREATY_UNK_40, TREATY_ATROCITY_VICTIM, TREATY_MAJOR_ATROCITY_VICTIM,
                                   TREATY_SURRENDERED, TREATY_SHALL_BETRAY, TREATY_UNK_800};
    for (int i = 1; i < FACTION_COUNT; i++)
        for (int j = i + 1; j < FACTION_COUNT; j++) {
            uint32_t c = mix(h + (uint32_t)(i * 8 + j));
            for (unsigned k = 0; k < sizeof both / sizeof both[0]; k++)
                if (coin(c, 3 * (int)k, 6)) {
                    fac(i)->diplo_status[j] ^= both[k];
                    fac(j)->diplo_status[i] = (fac(j)->diplo_status[i] & ~both[k]) | (fac(i)->diplo_status[j] & both[k]);
                }
            uint32_t d = mix(c);
            for (unsigned k = 0; k < sizeof one / sizeof one[0]; k++) {
                if (coin(d, 4 * (int)k, 9)) fac(i)->diplo_status[j] ^= one[k];
                if (coin(d, 4 * (int)k + 2, 9)) fac(j)->diplo_status[i] ^= one[k];
            }
            if (coin(c, 14, 8)) fac(i)->diplo_agenda[j] ^= AGENDA_UNK_8;
            if (coin(c, 17, 8)) fac(i)->diplo_agenda[j] ^= AGENDA_UNK_800;
            if (coin(c, 20, 8)) fac(j)->diplo_agenda[i] ^= AGENDA_UNK_8;
        }
}

// Who is human: nobody, or one or two living factions.
static void vary_humans(uint32_t h, int must) {
    uint8_t humans = 0;
    if (must) humans |= (uint8_t)bit(must);
    if (coin(h, 0, 3)) {
        int f = living_faction(mix(h));
        if (f) humans |= (uint8_t)bit(f);
    }
    *game_human_factions = humans;
}

// What the Council has passed and when, and the governor.
static void vary_council(uint32_t h) {
    int turn = *game_turn;
    for (int p = 0; p < PROP_COUNT; p++) {
        uint32_t c = mix(h + 0x100 + (uint32_t)p);
        if (coin(c, 0, 2)) game_proposal_passes[p] = pick(c, 2, 4) ? 0 : 1 + pick(c, 4, 2);
        if (coin(c, 6, 2)) game_proposal_turn[p] = pick(c, 8, 3) ? -9999 : turn - pick(c, 10, 40);
        // Its tech: none needed, sometimes.
        if (coin(c, 16, 3)) game_proposals[p].preq_tech = TECH_NONE;
    }
    if (coin(h, 3, 2)) *game_governor_faction = coin(h, 5, 3) ? -1 : living_faction(mix(h + 1));
    for (int f = 1; f < FACTION_COUNT; f++) {
        uint32_t c = mix(h + 0x200 + (uint32_t)f);
        struct faction_data *F = fac(f);
        if (coin(c, 0, 2)) F->council_call_turn = pick(c, 2, 3) ? -9999 : turn - pick(c, 4, 40);
        for (int p = 0; p < PROP_COUNT; p++)
            if (coin(c, 10 + p, 12)) {
                int to = living_faction(mix(c + (uint32_t)p));
                F->council_pledge[p] = p ? -1 - pick(c, 22, 3) : to;
                F->council_pledge_to[p] = p ? living_faction(mix(c + 0x40 + (uint32_t)p)) : to;
            }
        if (coin(c, 24, 3)) game_council_candidate[f] = coin(c, 26, 3) ? 0 : living_faction(mix(c + 9));
        if (coin(c, 28, 3)) F->energy_credits = (int32_t)(mix(c + 3) % 1500);
    }
    if (coin(h, 7, 3)) *game_rules ^= RULES_VICTORY_DIPLOMATIC;
    if (coin(h, 9, 4)) *game_rules ^= RULES_VICTORY_COOPERATIVE;
    if (coin(h, 11, 3)) *game_rules ^= RULES_UNK_2;
    if (coin(h, 13, 12)) *game_state ^= GAME_STATE_DONE;
    if (coin(h, 15, 12)) *game_state ^= GAME_STATE_OMNISCIENT | GAME_STATE_DEBUG;
    if (coin(h, 17, 2)) *game_turn = turn + pick(h, 19, 16);
}

static void sweep_council_action(int turn, int k) {
    uint32_t h = mix((uint32_t)turn * 977u + (uint32_t)k);
    int target = living_faction(h), cur = coin(h, 3, 2) ? target : living_faction(mix(h + 5));
    if (!target || !cur) return;
    struct hook_writes w;
    case_begin(&w);
    vary_relations(mix(h + 1));
    vary_humans(mix(h + 2), coin(h, 5, 2) ? cur : 0);
    vary_council(mix(h + 3));
    *game_current_faction = cur;
    if (coin(h, 7, 3)) *game_climate_change = pick(h, 9, 9) - 4;
    if (coin(h, 13, 4)) game_faction_defs[cur].rule_flags ^= RULE_FLAG_ALIEN;
    quiet_answer = pick(h, 15, 2);
    int prop = k % 10;
    check_council_action(prop, target);
    quiet_answer = 0;
    hooks_writes_abandon(&w);
}

// The proposals the menu would list for `faction` (as a bit set).
static unsigned listed_props(int faction) {
    unsigned listed = 0;
    int32_t aliens = 0;
    for (int prop = 0; prop < PROP_COUNT; prop++) {
        const struct proposal *p = &game_proposals[prop];
        if (!has_tech(p->preq_tech, faction) && !omniscient_debug()) continue;
        if (p->preq_tech < TECH_NONE) continue;
        if (offered(faction, prop, &aliens)) listed |= 1u << prop;
    }
    return listed;
}

static void sweep_call_council(int turn, int k) {
    uint32_t h = mix((uint32_t)turn * 1931u + (uint32_t)k + 0x5000);
    int faction = living_faction(h);
    if (!faction) return;
    struct hook_writes w;
    case_begin(&w);
    vary_relations(mix(h + 1));
    // Commlinks with everyone, mostly, so that it may call.
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && !coin(mix(h + 0x30 + (uint32_t)i), 0, 12))
            fac(faction)->diplo_status[i] |= TREATY_COMMLINK, fac(i)->diplo_status[faction] |= TREATY_COMMLINK;
    int me = coin(h, 3, 3);
    vary_humans(mix(h + 2), me ? faction : 0);
    if (!me) *game_human_factions &= (uint8_t)~bit(faction);
    vary_council(mix(h + 3));
    *game_current_faction = me || coin(h, 5, 2) ? faction : living_faction(mix(h + 4));
    int32_t word = coin(h, 7, 2) ? 0 : (int32_t)(mix(h + 6) | 1);
    quiet_answer = -1;
    if (me) {
        // A proposal from the menu, or none.
        unsigned listed = listed_props(faction);
        int n = 0, choices[PROP_COUNT];
        for (int p = 0; p < PROP_COUNT; p++)
            if (listed & (1u << p)) choices[n++] = p;
        if (n && !coin(h, 9, 5)) quiet_answer = choices[pick(h, 12, n)];
    }
    check_call_council(faction, word);
    quiet_answer = 0;
    hooks_writes_abandon(&w);
}

static void sweep_buy_council_vote(int turn, int k) {
    uint32_t h = mix((uint32_t)turn * 2477u + (uint32_t)k + 0x9000);
    int player = living_faction(h), ai = living_faction(mix(h + 1));
    if (!player || !ai || ai == player || !*game_text_line) return;
    struct hook_writes w;
    case_begin(&w);
    vary_relations(mix(h + 2));
    *game_human_factions = (uint8_t)bit(player);
    vary_council(mix(h + 3));
    *game_current_faction = player;
    int prop = k % 10, target;
    if (prop) target = -1 - pick(h, 4, 3);
    else target = coin(h, 6, 5) ? ai : living_faction(mix(h + 4));
    *game_council_session = coin(h, 8, 2);
    struct faction_data *A = fac(ai);
    if (coin(h, 10, 3)) A->se_base[SE_ECONOMY] = pick(h, 12, 5) - 2;
    if (coin(h, 14, 4)) fac(player)->energy_credits = (int32_t)(mix(h + 5) % 3000);
    if (coin(h, 16, 6))
        A->diplo_status[player] |= TREATY_PACT | TREATY_SURRENDERED, fac(player)->diplo_status[ai] |= TREATY_PACT;
    if (coin(h, 18, 3)) game_council_candidate[ai] = target;
    quiet_text = 1;
    quiet_answer = pick(h, 20, 7) - 1;
    check_buy_council_vote(player, ai, prop, target);
    quiet_text = 0, quiet_answer = 0;
    hooks_writes_abandon(&w);
}

static void sweep_acts(void) {
    if (*game_net_multiplayer) return;
    int turn = *game_turn;
    for (int k = 0; k < 20; k++) sweep_council_action(turn, k);
    for (int k = 0; k < 21; k++) sweep_call_council(turn, k);
    for (int k = 0; k < 20; k++) sweep_buy_council_vote(turn, k);
}

// The game as it is, then a dozen variations of it, on a protected copy
// that is put back after.
static void council_sweep(void) {
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    sweep_acts();
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
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
    HOOK(0x52bf60, council_action, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x08),         // push ecx; mov eax, [ebp+8]
    HOOK(0x52c880, call_council, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
    HOOK(0x53eb50, buy_council_vote, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
};
struct hook_module council_module = {"council", council_hooks, HOOK_COUNT(council_hooks), council_sweep, NULL, -1};
