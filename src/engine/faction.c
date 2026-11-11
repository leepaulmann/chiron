// Factions: probes, the energy market and the AI's diplomacy. Reimplemented from terranx.exe
// 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "council.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "score.h"
#include "shadow.h"
#include "tech.h"
#include "veh.h"

enum {
    TREATY_SURRENDERED = 0x2000000,
    STACK_PLAN = 2, STACK_ABILITY = 6, ABL_POLY_ENCRYPTION = 0x1000, BASE_STATE_RIOTS_ = 0x2,
};

static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }

// How far a square is from the faction's headquarters: the distance to the
// first of its bases with one, 12 with none (vulnerable(), which
// mind_control() has inline).
static int vulnerable(int faction, int x, int y) {
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *hq = &game_bases[i];
        if (hq->faction != faction || !has_fac(FAC_HEADQUARTERS, i, 0)) continue;
        int dx = abs(x - hq->x);
        if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
        return vector_dist_offset(dx, abs(y - hq->y));
    }
    return 12;
}
static int vulnerability(const struct base *b) { return vulnerable(b->faction, b->x, b->y); }

// The cost grows with the defenders on the base's square (more with Polymorphic
// Encryption), the probing faction's past mind control, the base's size and
// its owner's reserves, and falls with its distance from headquarters. A
// Genejack Factory doubles it, and a Children's Creche, a Punishment Sphere
// or nerve stapling halves it. Against a human, an AI pays less on the
// harder levels. For the market, a pact or treaty with the owner halves it
// and it is scaled by the two factions' commerce; for a probe, a pact
// doubles it. A former owner, riots and the owner's grudges change it too.
int mind_control(int base, int faction, int market) {
    const struct base *b = &game_bases[base];
    int owner = b->faction;
    int dist = vulnerability(b);
    if (dist <= 0) {
        if (!market) return -1;
        dist = 1;
    }
    if (game_has_facility(b, FAC_GENEJACK_FACTORY)) dist *= 2;
    if (game_has_facility(b, FAC_CHILDREN_CRECHE)) dist /= 2;
    if (game_has_facility(b, FAC_PUNISHMENT_SPHERE)) dist /= 2;
    if (b->nerve_staple_turns) dist /= 2;
    int v = game_stack_fix(game_veh_at(b->x, b->y));
    int defense = game_stack_check(v, STACK_PLAN, PLAN_COMBAT, -1, -1) + game_stack_check(v, STACK_PLAN, PLAN_OFFENSIVE, -1, -1);
    defense *= game_stack_check(v, STACK_ABILITY, ABL_POLY_ENCRYPTION, -1, -1) + 1;
    const struct faction_data *f = &game_factions[faction], *o = &game_factions[owner];
    int cost = defense + f->mind_control_total / 4 + b->pop_size;
    cost *= (o->corner_market_cost + o->energy_credits + 1200) / (dist + 4);
    if (!is_human(faction) && is_human(owner) && o->diff_level > 3) cost = cost * 3 / o->diff_level;
    uint32_t ties = f->diplo_status[owner];
    if (market) {
        if (ties & TREATY_PACT) cost /= 2;
        if (ties & TREATY_TREATY) cost /= 2;
        cost = cost * (o->tech_commerce_bonus * o->tech_commerce_bonus + 1) /
               (f->tech_commerce_bonus * f->tech_commerce_bonus + 1);
    } else if (ties & TREATY_PACT) {
        cost *= 2;
    }
    if (b->faction_former == faction) cost /= 2;
    if (b->state & BASE_STATE_RIOTS_) cost /= 2;
    if (b->state & BASE_STATE_GOLDEN_AGE) cost *= 2;
    uint32_t grudge = o->diplo_status[faction];
    if (grudge & TREATY_ATROCITY_VICTIM) return cost * 2;
    if (grudge & TREATY_WANT_REVENGE) cost += cost / 2;
    return cost;
}

int corner_market(int faction) {
    int cost = 0;
    for (int i = 0; i < *game_base_count; i++) {
        int owner = game_bases[i].faction;
        if (owner == faction) continue;
        uint32_t ties = game_factions[owner].diplo_status[faction];
        if ((ties & TREATY_PACT) && (ties & TREATY_SURRENDERED)) continue;
        cost += mind_control(i, faction, 1);
    }
    return cost < 1000 ? 1000 : cost;
}

// ---- Treaties ---------------------------------------------------------------

static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline char *text(uint32_t addr) { return (char *)game_at(addr); }

enum {
    TREATY_TRUCE = 0x4, TREATY_UNK_100000 = 0x100000, TREATY_UNK_800000 = 0x800000, AGENDA_UNK_200 = 0x200,
    FACTION_FLAG_TEAM_UP_VS_HUMAN = 0x800000,  // faction_data.flags: the AI sides with AIs against humans
    RULES_VICTORY_CONQUEST = 0x2, RULES_VICTORY_DIPLOMATIC = 0x8, RULES_NO_TECH_TRADING = 0x2000000,
    SOUND_VENDETTA = 204,
};
GAME_AT(struct base, minerals, 0x40);

// Takes treaty bits off between two factions, both ways. Ending a truce,
// treaty or pact also clears 0x1000000 and 0x100000; a vendetta, 0x8000 and
// 0x40; and with the wish for revenge go the agenda's 0x400 and 0x800.
void treaty_off(int faction, int other, uint32_t status) {
    if (status & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) status |= TREATY_UNK_1000000 | TREATY_UNK_100000;
    if (status & TREATY_VENDETTA) status |= TREATY_UNK_8000 | TREATY_UNK_40;
    if (status & TREATY_WANT_REVENGE) {
        game_factions[faction].diplo_agenda[other] &= ~(uint32_t)(AGENDA_UNK_400 | AGENDA_UNK_800);
        game_factions[other].diplo_agenda[faction] &= ~(uint32_t)(AGENDA_UNK_400 | AGENDA_UNK_800);
    }
    game_factions[faction].diplo_status[other] &= ~status;
    game_factions[other].diplo_status[faction] &= ~status;
}

void agenda_off(int faction, int other, uint32_t agenda) {
    game_factions[faction].diplo_agenda[other] &= ~agenda;
    game_factions[other].diplo_agenda[faction] &= ~agenda;
}

// Puts treaty bits on between two factions, both ways. Peace of any kind
// first ends a vendetta and the grudges that go with it (and, for the
// player, the war's music flag); a pact brings a treaty. A vendetta resets
// the score of battles if there was peace, brings a commlink and bits 0x100
// and 0x80000000, and ends the peace, the betrayals planned and other
// standing (for the player, with the war's music). Either clears 0x800000.
// When the player is one of the two, the multiplayer window is redrawn if
// open. Unlike Thinker's, the original doesn't check the factions' range.
void treaty_on(int faction, int other, uint32_t status) {
    uint32_t *mine = &game_factions[faction].diplo_status[other], *theirs = &game_factions[other].diplo_status[faction];
    if (status & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) {
        if ((faction == *game_current_faction || other == *game_current_faction) && (*mine & TREATY_VENDETTA))
            *game_ambience_flags &= ~1;
        *mine &= 0xffee7fafu, *theirs &= 0xffee7fafu;
    }
    if (status & TREATY_PACT) status |= TREATY_TREATY;
    if (status & TREATY_VENDETTA) {
        if (*mine & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) {
            game_factions[other].diplo_combat_count[faction] = 0;
            game_factions[faction].diplo_combat_count[other] = 0;
        }
        status |= TREATY_UNK_80000000 | TREATY_UNK_100 | TREATY_COMMLINK;
        *mine &= 0xfaeef378u, *theirs &= 0xfaeef378u;
        if (faction == *game_current_faction || other == *game_current_faction) {
            *game_ambience_flags |= 1;
            game_ambience(SOUND_VENDETTA);
        }
    }
    if (status & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE | TREATY_VENDETTA)) {
        *mine &= ~(uint32_t)TREATY_UNK_800000;
        *theirs &= ~(uint32_t)TREATY_UNK_800000;
    }
    *mine |= status;
    *theirs |= status;
    if ((faction == *game_current_faction || other == *game_current_faction) &&
        (status & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE | TREATY_COMMLINK | TREATY_VENDETTA)) &&
        *game_multi_win_visible)
        game_graphic_win_redraw(game_multi_win);
}

// Agenda bits on, both ways; 0x20 first clears 0x40 (see enemy_diplomacy).
void agenda_on(int faction, int other, uint32_t agenda) {
    if (agenda & AGENDA_UNK_20) {
        game_factions[faction].diplo_agenda[other] &= ~(uint32_t)AGENDA_UNK_40;
        game_factions[other].diplo_agenda[faction] &= ~(uint32_t)AGENDA_UNK_40;
    }
    game_factions[faction].diplo_agenda[other] |= agenda;
    game_factions[other].diplo_agenda[faction] |= agenda;
}

// Treaty bits on or off one way only. Setting 0x40 starts the betrayal's
// mood for 50 turns. (Thinker adds counter-espionage of its own here.)
void set_treaty(int faction, int other, uint32_t status, int on) {
    if (on) {
        game_factions[faction].diplo_status[other] |= status;
        if (status & TREATY_UNK_40) game_factions[faction].diplo_merc[other] = 50;
    } else {
        game_factions[faction].diplo_status[other] &= ~status;
    }
}

// The factions in a pact with `victim` that have no pact with the
// aggressor, nor a vendetta already, take its side: the parser is given
// their names, and when a human is either party they declare a vendetta
// on the aggressor (noting when they last spoke if it is human). The
// player hears of it if it is one of the two.
void intervention(int victim, int aggressor) {
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == victim || i == aggressor || !(*game_factions_alive >> i & 1)) continue;
        if (!(game_factions[i].diplo_status[victim] & TREATY_PACT) ||
            (game_factions[i].diplo_status[aggressor] & (TREATY_PACT | TREATY_VENDETTA)))
            continue;
        struct faction_def *I = &game_faction_defs[i], *V = &game_faction_defs[victim], *A = &game_faction_defs[aggressor];
        *game_parse_gender = I->noun_gender, *game_parse_plural = I->noun_plural;
        game_parse_says(0, I->noun, -1, -1);
        *game_parse_gender = V->noun_gender, *game_parse_plural = V->noun_plural;
        game_parse_says(1, V->noun, -1, -1);
        *game_parse_gender = I->leader_female, *game_parse_plural = 0;
        game_parse_says(2, I->leader_title, -1, -1);
        *game_parse_gender = I->leader_female, *game_parse_plural = 0;
        game_parse_says(3, I->leader_name, -1, -1);
        *game_parse_plural = 0, *game_parse_gender = A->leader_female;
        game_parse_says(4, A->leader_title, -1, -1);
        *game_parse_gender = A->leader_female, *game_parse_plural = 0;
        game_parse_says(5, A->leader_name, -1, -1);
        *game_parse_gender = A->noun_gender, *game_parse_plural = A->noun_plural;
        game_parse_says(6, A->noun, -1, -1);
        if (is_human(aggressor) || is_human(victim)) {
            if (is_human(aggressor)) game_factions[aggressor].diplo_spoke[i] = *game_turn;
            treaty_on(i, aggressor, TREATY_UNK_80000000 | TREATY_VENDETTA | TREATY_COMMLINK);
            set_treaty(i, aggressor, TREATY_UNK_40, 1);
        }
        if (aggressor == *game_current_faction) game_x_pop(text(0x68a308), 0);
        else if (victim == *game_current_faction) game_x_pop(text(0x68a314), 0);
    }
}

// ---- The AI's reckoning -------------------------------------------------------

// Whether the player (human, and first in the rankings) is far enough
// ahead for the AI to unite against: more bases than a 50th of the turns
// (at least 4), and above the easiest levels, under intense rivalry or
// with `aggressive`.
int great_beelzebub(int faction, int aggressive) {
    if (!is_human(faction) || game_faction_rankings[7] != faction) return 0;
    int bases = (*game_turn + 25) / 50;
    if (bases < 4) bases = 4;
    const struct faction_data *f = &game_factions[faction];
    return f->base_count > bases && (f->diff_level > 1 || (*game_rules & RULES_INTENSE_RIVALRY) || aggressive);
}

// The same, and late enough (turn 100 under intense rivalry, else 50
// turns more for each level below the top): the player is about to win the
// Council, or its strength is ahead of the second's by a margin that
// shrinks with the rules and the level.
int great_satan(int faction, int aggressive) {
    if (!great_beelzebub(faction, aggressive)) return 0;  // inline in the original
    const struct faction_data *f = &game_factions[faction];
    int rivalry = (*game_rules & RULES_INTENSE_RIVALRY) != 0;
    if (*game_turn <= (rivalry ? 0 : (5 - f->diff_level) * 50) + 100) return 0;
    if (game_climactic_battle() && aah_ooga(faction, -1) == faction) return 1;
    int mul, div;
    if (*game_rules & RULES_INTENSE_RIVALRY) mul = 4, div = 5;
    else if (f->diff_level >= 3 || (*game_rules & RULES_VICTORY_CONQUEST) || *game_objectives_required <= 1000)
        mul = 2, div = 3;
    else mul = 1, div = 2;
    return game_ranking_scores[game_faction_rankings[7]] * mul >= game_ranking_scores[game_faction_rankings[6]] * div;
}

// With diplomatic victory allowed and `faction` knowing (or one step from)
// the Supreme Leader proposal's tech: the faction with half the Council's
// votes (itself first), leaving out `pact` and, in a cooperative game, its
// pact partners; else 0. The original reads the partners' standing towards
// `pact` for any `pact` but 0, so for -1 it reads the word before
// diplo_status (last_base_turn), where Thinker skips them.
int aah_ooga(int faction, int pact) {
    if (!(*game_rules & RULES_VICTORY_DIPLOMATIC)) return 0;
    int total = 0;
    for (int i = 1; i < FACTION_COUNT; i++) total += council_votes(i);
    int leader = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        int votes = council_votes(i);
        if (i == pact) continue;
        const uint32_t *status =
            (const uint32_t *)((const char *)&game_factions[i] + offsetof(struct faction_data, diplo_status));
        if (pact && (status[pact] & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE)) continue;
        int preq = game_proposals[PROP_SUPREME_LEADER].preq_tech;
        if (!has_tech(preq, faction) &&
            (preq < 0 || (!has_tech(game_tech[preq].preq1, faction) && !has_tech(game_tech[preq].preq2, faction))))
            continue;
        if (2 * votes >= total && (!leader || i == faction)) leader = i;
    }
    return leader;
}

// Whether an AI faction, not on the easiest level, sees the end coming
// when the player does (climactic_battle()): it could win the Council, a
// faction has cornered the energy market (one not in its pact, in a
// cooperative game), another faction may build the Ascent to
// Transcendence and it may not, or another's Ascent has more minerals
// than any of its own. Thinker reads the faction's own market, not the
// other's.
int at_climax(int faction) {
    if (is_human(faction) || (*game_state & GAME_STATE_DONE) || !*game_difficulty || !game_climactic_battle()) return 0;
    if (aah_ooga(faction, faction)) return 1;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && game_factions[i].corner_market_turn > *game_turn &&
            (!(game_factions[faction].diplo_status[i] & TREATY_PACT) || !(*game_rules & RULES_VICTORY_COOPERATIVE)))
            return 1;
    int ours = 0, theirs = 0;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->queue[0] != -FAC_ASCENT) continue;
        if (B->faction == faction) {
            if (ours <= B->minerals) ours = B->minerals;
        } else if (theirs <= B->minerals) {
            theirs = B->minerals;
        }
    }
    for (int i = 1; i < FACTION_COUNT; i++)
        if (i != faction && ascending(i) && !ascending(faction)) return 1;
    return theirs && theirs > ours;
}

// Friction added between two factions, kept within 0 to 20; the running
// total in game_diplo_friction, if it is theirs, takes it unclamped.
void cause_friction(int faction, int other, int friction) {
    int32_t *fr = &game_factions[faction].diplo_friction[other];
    *fr = clamp(*fr + friction, 0, 20);
    if (*game_diplo_friction_faction == faction && *game_diplo_friction_with == other) *game_diplo_friction += friction;
}

// Friction as a mood, 0 (magnanimous) to 8 (seething).
int get_mood(int friction) {
    if (friction <= 0) return 0;
    if (friction <= 2) return 1;
    if (friction <= 4) return 2;
    if (friction <= 8) return 3;
    if (friction <= 12) return 4;
    if (friction <= 15) return 5;
    if (friction <= 17) return 6;
    return friction <= 19 ? 7 : 8;
}

// How badly `faction` stands with `other`: its broken treaties less its
// gifts to it, 0 to 99.
int reputation(int faction, int other) {
    const struct faction_data *f = &game_factions[faction];
    return clamp(f->integrity_blemishes - f->diplo_gifts[other], 0, 99);
}

// How many times `faction` will hear `other` out: once in a vendetta, 6
// in a pact (500 if it surrendered), else 3, one more with a treaty, less
// an eighth of the friction in talks.
int get_patience(int other, int faction) {
    uint32_t st = game_factions[faction].diplo_status[other];
    if (st & TREATY_VENDETTA) return 1;
    if (st & TREATY_PACT) return st & TREATY_SURRENDERED ? 500 : 6;
    return (int)((st >> 1) & 1) - (*game_diplo_friction + 3) / 8 + 3;
}

// The goodwill a loan of `energy` buys: of a fifth of it, the first 10 at
// half, the next 20 at a third, the next at a quarter and so on; then a
// fifth of that, rounded.
int energy_value(int energy) {
    int value = 0, div = 2, step = 10;
    for (int left = energy / 5; left > 0; left -= step, step = 20, div++)
        value += (step >= 0 ? (left > step ? step : left) : 0) / div;
    return (value + 4) / 5;
}

// What `faction` would give for `base` from `other`, or -1 if it can't
// reach it (vulnerable()): its reserves or other's, the higher, over the
// distance from its headquarters (counting other's bases near the base on
// the same continent before one of its own, which counts less), times the
// base's size (at least 3), at least 100; halved with no unit there; more
// if other has no bases on that continent or it has one, four times with
// `coveted`; halved if other has five times its bases there or with
// `overmatched`. Then its facilities (each costing more than the last),
// its secret projects and its own units there; twice for the Believers and
// four times for a scenario objective.
int value_of_base(int base, int faction, int other, int overmatched, int coveted) {
    if (base < 0) return -1;
    const struct base *b = &game_bases[base];
    int dist = vulnerable(faction, b->x, b->y);
    if (dist <= 0) return -1;
    int region = region_at(b->x, b->y);
    for (int i = 1; i <= 168; i++) {
        int x = b->x + game_radius_x[i], y = b->y + game_radius_y[i];
        if (!(*game_map_flat & 1)) {
            if (x < 0) x += *game_map_width;
            else if (x >= *game_map_width) x -= *game_map_width;
        }
        if (y < 0 || y >= *game_map_height || x < 0 || x >= *game_map_width) continue;
        int near = base_at(x, y);
        if (near < 0) continue;
        int owner = game_bases[near].faction;
        if (owner == faction) {
            if (region_at(x, y) == region) dist--;
            break;
        }
        if (region_at(x, y) != region) continue;
        if (owner != other) break;
        dist++;
    }
    int veh = game_stack_fix(game_veh_at(b->x, b->y));
    int reserves = game_factions[other].energy_credits;
    if (game_factions[faction].energy_credits > reserves) reserves = game_factions[faction].energy_credits;
    int value = (reserves + 1000) / (dist + 4) * (b->pop_size < 3 ? 3 : b->pop_size);
    if (value < 100) value = 100;
    const struct map_tile *t = &(*game_map)[(b->x >> 1) + b->y * *game_map_half_width];
    if (!(t->bits & MAP_UNIT) || (t->owner_site & 0xf) >= 8) value /= 2;
    int theirs = game_factions[other].region_total_bases[region];
    int ours = game_factions[faction].region_total_bases[region];
    if (!theirs) value *= 2;
    if (ours == 1) {
        if (!theirs) value *= 2;
        if (theirs == 1) value *= 2;
    }
    if (coveted) value *= 4;
    if (theirs && ours) {
        if (theirs >= 5 * ours) value /= 2;
        if (overmatched) value /= 2;
    }
    for (int fac = 1, n = 0; fac < FAC_FIRST_PROJECT; fac++)
        if (fac < 65 && game_has_facility(b, fac)) value += game_facilities[fac].cost * ++n;
    for (int sp = 0; sp < SP_COUNT; sp++)
        if (game_secret_projects[sp] == base) value += 25 * game_facilities[FAC_FIRST_PROJECT + sp].cost;
    if (veh >= 0) {
        while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
        for (; veh >= 0; veh = game_vehs[veh].next_in_stack)
            if (game_vehs[veh].faction == faction) value += 2 * game_protos[game_vehs[veh].proto].cost;
    }
    if (!strcmp(game_faction_defs[faction].filename, text(0x68ddec))) value *= 2;
    if (is_objective(base)) value *= 4;
    return value;
}

// What `player` and `rival` could trade in techs (see game_trade_want_tech
// in game.h), unless the rules forbid trading. A tech the rival has is
// worth half to the player if the rival is building the secret project it
// allows (whose number is noted), or if the player could start one with
// it, which only a hard player or a network game's leader is told (noted
// as minus two less the number); the player looks for these if not on an
// easy level or if the rival is the Hive. A second offer that touches such
// a project is dropped.
void tech_analysis(int player, int rival) {
    *game_trade_project = -1;
    for (int i = 0; i < 4; i++) *game_trade_want_tech[i] = -1, *game_trade_want_value[i] = 0;
    for (int i = 0; i < 2; i++)
        *game_trade_offer_tech[i] = -1, *game_trade_offer_value[i] = 0, *game_trade_offer_project[i] = -1;
    if (*game_rules & RULES_NO_TECH_TRADING) return;
    int32_t **wt = game_trade_want_tech, **wv = game_trade_want_value;
    int32_t **ot = game_trade_offer_tech, **ov = game_trade_offer_value, **op = game_trade_offer_project;
    for (int t = 0; t < TECH_COUNT; t++) {
        if (has_tech(t, rival)) {
            if (has_tech(t, player)) continue;
            int project = -1, level = game_factions[player].diff_level;
            if (level >= 3 || !strcmp(game_faction_defs[rival].filename, text(0x68d6a4))) {
                for (int sp = 0; sp < SP_COUNT; sp++) {
                    if (game_secret_projects[sp] != -1 || game_facilities[FAC_FIRST_PROJECT + sp].preq_tech != t) continue;
                    for (int b = 0; b < *game_base_count; b++)
                        if (game_bases[b].faction == rival && game_bases[b].queue[0] == -(FAC_FIRST_PROJECT + sp))
                            project = sp, *game_trade_project = sp;
                    int net = *game_net_multiplayer, told;
                    if (level >= 5) told = !net || player != game_ranked_faction[1] || player == game_ranked_faction[0];
                    else told = net && player == game_ranked_faction[0];
                    if (told && *game_trade_project == -1) project = -2 - sp, *game_trade_project = -2 - sp;
                }
            }
            int v = tech_val(t, player, 0);
            if (project != -1) v = (v + 1) / 2;
            if (v >= *ov[0]) {
                *ov[1] = *ov[0], *ot[1] = *ot[0], *op[1] = *op[0];
                *ov[0] = v, *ot[0] = t, *op[0] = project;
            } else if (v >= *ov[1]) {
                *ov[1] = v, *ot[1] = t, *op[1] = project;
            }
        } else if (has_tech(t, player)) {
            int v = tech_val(t, rival, 0), k;
            if (v >= *wv[0]) k = 0;
            else if (v >= *wv[1]) k = 1;
            else if (v >= *wv[2]) k = 2;
            else if (v >= *wv[3]) k = 3;
            else continue;
            for (int j = 3; j > k; j--) *wv[j] = *wv[j - 1], *wt[j] = *wt[j - 1];
            *wv[k] = v, *wt[k] = t;
        }
    }
    if (*op[1] != -1) *ot[1] = -1;
}

// Whether `faction` would go to war with `other`, with `ally` (if above
// 0) fighting beside it. Yes between two alien factions, when it wants
// revenge or 0x40 is set, or when other has committed major atrocities
// and it hasn't; no with 0x4000000, a single base, or towards an AI when
// teaming up against humans. Then its reasons: its wars (and their
// strength), the player's lead, its human allies' wars and pacts, a
// partner's surrender; a peaceful AI goes on only beside a human ally at
// war or against the leader. It weighs its forces against other's on each
// land region both are in (where it has many bases or its headquarters,
// other's strength must not be above its own, its ally's counted at a
// quarter), and then commerce, weapons against armor, peace, its ally's
// standing, the agenda, the reputations and morale. Sharing no region and
// with nothing against it, it attacks.
//
// The original keeps the most bases each faction has in a region in locals
// it never clears (and for faction 0 reads the word below them); patches.c
// clears them, as Thinker does, so they start from 0 here.
int wants_to_attack(int faction, int other, int ally) {
    const struct faction_data *me = &game_factions[faction], *them = &game_factions[other];
    uint32_t st = me->diplo_status[other];
    if ((game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) &&
        (game_faction_defs[other].rule_flags & RULE_FLAG_ALIEN))
        return 1;
    if (st & (TREATY_WANT_REVENGE | TREATY_UNK_40 | TREATY_ATROCITY_VICTIM)) return 1;
    if (them->major_atrocities && !me->major_atrocities) return 1;
    if (st & TREATY_UNK_4000000) return 0;
    if (me->base_count <= 1) return 0;
    if (!is_human(other) && (me->flags & FACTION_FLAG_TEAM_UP_VS_HUMAN)) return 0;
    int odds = 0, surrendered = 0, ally_at_war = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || i == other) continue;
        uint32_t s = me->diplo_status[i];
        if ((s & TREATY_SURRENDERED) && (s & TREATY_PACT)) surrendered = i;
        if ((s & TREATY_VENDETTA) && !(them->diplo_status[i] & TREATY_PACT)) {
            odds++;
            if (game_factions[i].mil_strength > 3 * them->mil_strength / 2) odds++;
        }
        if (great_beelzebub(i, 0) && (*game_turn >= 100 || (*game_rules & RULES_INTENSE_RIVALRY))) {
            uint32_t u = them->diplo_status[i];
            if (u & TREATY_VENDETTA) odds++;
            if ((u & TREATY_COMMLINK) && (me->diplo_status[i] & TREATY_COMMLINK)) odds++;
        }
        s = me->diplo_status[i];
        if ((s & TREATY_PACT) && is_human(i)) {
            uint32_t u = them->diplo_status[i];
            if (u & TREATY_PACT) odds += 2;
            if ((s & TREATY_SURRENDERED) && (game_factions[i].diplo_status[other] & (TREATY_PACT | TREATY_TREATY)))
                return 0;
            if (u & TREATY_VENDETTA) {
                ally_at_war = 1;
                odds -= s & TREATY_SURRENDERED ? 4 : 2;
            }
        }
    }
    if (surrendered) {
        uint32_t u = them->diplo_status[surrendered];
        if (u & TREATY_VENDETTA) return 1;
        if (u & (TREATY_PACT | TREATY_TREATY)) return 0;
    }
    if (me->ai_fight < 0 && !ally_at_war && game_faction_rankings[7] != other) return 0;
    // Three quarters of the most bases each faction has in a land region.
    int32_t most[FACTION_COUNT] = {0};
    for (int r = 1; r < LAND_REGION_COUNT; r++)
        for (int j = 1; j < FACTION_COUNT; j++)
            if (most[j] <= game_factions[j].region_total_bases[r]) most[j] = game_factions[j].region_total_bases[r];
    for (int j = 1; j < FACTION_COUNT; j++) most[j] -= most[j] / 4;
    int home = -1, their_home = -1;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (!game_has_facility(B, FAC_HEADQUARTERS)) continue;
        if (B->faction == faction) home = region_at(B->x, B->y);
        else if (B->faction == other) their_home = region_at(B->x, B->y);
    }
    int ours = 0, theirs = 1, shared = 0;
    for (int r = 1; r < LAND_REGION_COUNT - 1; r++) {
        int force = me->region_force_rating[r], units = them->region_total_combat_units[r];
        if (!force || (!units && !them->region_total_bases[r])) continue;
        const struct faction_data *A = ally > 0 ? &game_factions[ally] : NULL;
        if ((me->region_total_bases[r] >= 3 * most[faction] / 4 || r == home) &&
            them->region_force_rating[r] > me->region_total_combat_units[r] + (A ? A->region_force_rating[r] >> 2 : 0) + force)
            return 0;
        if (them->region_total_bases[r]) ours += force + (A ? A->region_force_rating[r] >> 1 : 0);
        if ((them->region_total_bases[r] >= 3 * most[other] / 4 || r == their_home) && force > units)
            ours += force + (A ? A->region_force_rating[r] >> 1 : 0);
        theirs += units + (me->region_total_bases[r] ? them->region_force_rating[r] >> 1 : 0);
        if (me->region_total_bases[r]) shared++;
    }
    int c = odds - 2 * me->ai_fight;
    if (me->tech_commerce_bonus > 3 * them->tech_commerce_bonus / 2) c++;
    if (me->tech_commerce_bonus < 2 * them->tech_commerce_bonus / 3) c--;
    if (me->best_weapon_value > 2 * them->best_armor_value) c--;
    if (me->best_weapon_value <= them->best_armor_value) c++;
    st = me->diplo_status[other];
    if (!(st & TREATY_VENDETTA)) c++;
    if (st & TREATY_PACT) c++;
    if (ally > 0 && !great_satan(ally, 0)) c--;
    if ((me->diplo_agenda[other] & AGENDA_UNK_200) && !(*game_rules & RULES_INTENSE_RIVALRY)) c--;
    c -= clamp((them->integrity_blemishes - me->integrity_blemishes + 2) / 3, 0, 2);
    int morale = clamp(me->se_pending[SE_MORALE], -4, 4) + game_faction_defs[faction].rule_morale + 16;
    int their_morale = clamp(them->se_pending[SE_MORALE], -4, 4) + game_faction_defs[other].rule_morale + 16;
    if (!shared && c <= 0 && !(me->diplo_status[other] & TREATY_UNK_20000000)) return 1;
    return 6 * (morale * ours) / (their_morale * theirs) >= c + 6;
}

// ---- The talks ----------------------------------------------------------------

enum {
    TREATY_UNK_10000 = 0x10000, TREATY_UNK_200000 = 0x200000, TREATY_UNK_400000 = 0x400000,
    TREATY_MAJOR_ATROCITY_VICTIM = 0x80000, TREATY_UNK_40000000 = 0x40000000,
    AGENDA_UNK_1 = 0x1, AGENDA_UNK_4 = 0x4, AGENDA_FIGHT_TO_DEATH = 0x8, AGENDA_UNK_2000 = 0x2000,
    FACTION_FLAG_COOP_WITH_HUMAN = 0x400000, FACTION_FLAG_UNK_10000 = 0x10000,
    FACTION_FLAG_WANTON_ATROCITIES = 0x1000000,  // others have seen the player commit atrocities wantonly
    PROTO_FLAG_MENTIONED = 0x10, PROTO_FLAG_OFFERED = 0x20,
    TECH_PSYCH = 4, TECH_PRPSYCH = 0x4e,
    VEH_STATE_UNK_400 = 0x400, VEH_STATE_UNK_800 = 0x800,
};

static inline int alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
static inline struct map_tile *tile_at(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int un_charter(void) { return *game_un_charter_a <= *game_un_charter_b; }
// The faction on a square with a unit on it, or -1 (veh_who()).
static inline int unit_owner(const struct map_tile *t) {
    return (t->bits & MAP_UNIT) && (t->owner_site & 0xf) < 8 ? t->owner_site & 0xf : -1;
}
// A square of a base's radius (game_radius_x/y), wrapped, or 0 off the map.
static int radius_square(int x, int y, int k, int *rx, int *ry) {
    x += game_radius_x[k], y += game_radius_y[k];
    if (!(*game_map_flat & 1)) {
        if (x < 0) x += *game_map_width;
        else if (x >= *game_map_width) x -= *game_map_width;
    }
    *rx = x, *ry = y;
    return on_map(x, y);
}

// A faction's leader's title, the leader's name and the faction's noun, for
// the next message (with their grammar).
static void say_title(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(slot, d->leader_title, -1, -1);
}
static void say_name(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(slot, d->leader_name, -1, -1);
}
static void say_noun(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(slot, d->noun, -1, -1);
}

// How `other` (the AI) stands towards `faction` (the player) in talks, into
// the game_diplo_* variables: whether it sees the end coming, is outranked
// or treats the player as the great satan (or the player as a runaway
// leader), the friction and reputation, each side's pacts; its fear of the
// player and the tribute it would ask, from the forces they have on each
// region, bases left open or exposed, their combat records, arms, planet
// busters and reserves; its hostility (0 to 4), its chance to attack, the
// allies it shares enemies with, and its goodwill; whether it would
// surrender, and with `full` whether it would betray the player, with the
// factions it might ask for help against or mention, and the prototypes it
// might show (scan_prototypes). Thinker's diplomacy_check() adds its own
// biases, and checks the stack it reads next to a base, which the original
// reads even with no unit there.
void diplomacy_check(int faction, int other, int full) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[other];
    const uint32_t *their = &them->diplo_status[faction], *agenda = &them->diplo_agenda[faction];
    const uint32_t *ours = &me->diplo_status[other];
    int turn = *game_turn;
    *game_diplo_friction_with = faction, *game_diplo_friction_faction = other, *game_diplo_second = other;
    *game_diplo_hostility = *game_diplo_attack_chance = *game_diplo_tribute = *game_diplo_fear = 0;
    *game_diplo_conquest_fear = *game_diplo_exposed_bases = *game_diplo_open_base = *game_diplo_captured_bases = 0;
    *game_diplo_near_endgame = at_climax(faction);
    *game_diplo_rival_outranks = them->ranking > me->ranking;
    *game_diplo_great_satan = great_satan(faction, 0) && them->base_count > 1 &&
                              (!(*ours & TREATY_PACT) ||
                               (them->ranking >= 5 && (!(*game_rules & RULES_VICTORY_COOPERATIVE) || turn < 200)));
    *game_diplo_runaway_leader =
        game_ranking_scores[game_faction_rankings[7]] > 2 * game_ranking_scores[faction] && them->ranking != 7;
    if (them->flags & FACTION_FLAG_TEAM_UP_VS_HUMAN) *game_diplo_great_satan = 1, *game_diplo_runaway_leader = 0;
    if (them->flags & FACTION_FLAG_COOP_WITH_HUMAN) *game_diplo_great_satan = 0, *game_diplo_runaway_leader = 1;
    *game_diplo_friction = them->diplo_friction[faction];
    *game_diplo_reputation = reputation(faction, other);
    // The rival's leader minds the player's choice in its favoured
    // category, more with agenda bit 1.
    int ideology = 0;
    const struct faction_def *def = &game_faction_defs[other];
    if (def->ideology_category >= 0 && def->ideology_model) {
        int model = me->se_models_pending[def->ideology_category];
        if (model && model != def->ideology_model) ideology = *agenda & AGENDA_UNK_1 ? 2 : 1;
    }
    int our_record = me->diplo_combat_count[other] + turn / 50 + 1;
    int their_record = them->diplo_combat_count[faction] + turn / 50 + 1;
    if (!(*ours & TREATY_VENDETTA)) {
        our_record += turn / 25 + me->diplo_combat_total[other] + 2;
        their_record += turn / 25 + them->diplo_combat_total[faction] + 2;
    }
    int fight = them->ai_fight;
    if ((*their & TREATY_WANT_REVENGE) && fight < 0) fight = -fight;
    *game_diplo_player_pacts = *game_diplo_rival_pacts = *game_diplo_rival_strong_pacts = 0;
    *game_diplo_rival_pact_score = 0;
    uint8_t alive = *game_factions_alive;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!(alive >> i & 1)) continue;
        if ((me->diplo_status[i] & TREATY_PACT) && !(game_factions[i].diplo_status[faction] & TREATY_SURRENDERED))
            ++*game_diplo_player_pacts;
        if (them->diplo_status[i] & TREATY_PACT) {
            if (game_factions[i].ranking >= 5 && them->ranking < 5 && i != faction && !*game_diplo_runaway_leader)
                ++*game_diplo_rival_strong_pacts;
            ++*game_diplo_rival_pacts;
            *game_diplo_rival_pact_score += game_ranking_scores[i];
        }
    }
    if (full) *game_diplo_spoke_turn = me->diplo_spoke[other];
    int our_morale = game_faction_defs[faction].rule_morale + clamp(me->se_pending[SE_MORALE], -4, 4) + 16;
    int their_morale = game_faction_defs[other].rule_morale + clamp(them->se_pending[SE_MORALE], -4, 4) + 16;
    // The most bases each faction has on a region (at least 1); the
    // regions where each has its headquarters or near its most.
    int32_t most[FACTION_COUNT];
    for (int j = 1; j < FACTION_COUNT; j++) most[j] = 1;
    int32_t *regions = game_diplo_region_factions;
    memset(&regions[1], 0, (REGION_COUNT - 1) * sizeof(int32_t));
    for (int r = 1; r < REGION_COUNT; r++)
        for (int j = 1; j < FACTION_COUNT; j++)
            if (most[j] <= game_factions[j].region_total_bases[r]) most[j] = game_factions[j].region_total_bases[r];
    // Each of the two's bases: the player's bases the rival took, and
    // outside a pact the player's units next to the rival's empty bases
    // (which it fears for) and the rival's next to the player's (exposed),
    // or more of them next to a garrisoned base of the player's (an
    // opening to attack).
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (game_has_facility(B, FAC_HEADQUARTERS)) regions[tile_at(B->x, B->y)->region] |= 1 << B->faction;
        int owner = B->faction;
        if (owner == faction) {
            if (B->faction_former == other) ++*game_diplo_captured_bases;
        } else if (owner != other) {
            continue;
        }
        if (*ours & TREATY_PACT) continue;
        const struct map_tile *t = tile_at(B->x, B->y);
        int x, y;
        if (unit_owner(t) >= 0) {
            if (owner != faction) continue;
            int garrison = game_stack_check(game_veh_at(B->x, B->y), 14, -1, -1, -1), near = 0;
            for (int k = 0; k < 21; k++) {
                if (!radius_square(B->x, B->y, k, &x, &y)) continue;
                int v = game_stack_fix(game_veh_at(x, y));
                // With no unit there the original reads the word before the units.
                if (game_vehs[v].faction == other)
                    near += game_stack_check(v, STACK_PLAN, PLAN_COMBAT, -1, -1) +
                            game_stack_check(v, STACK_PLAN, PLAN_OFFENSIVE, -1, -1);
            }
            if (near > garrison) ++*game_diplo_attack_chance, ++*game_diplo_tribute;
            continue;
        }
        for (int k = 0; k < 21; k++) {
            if (!radius_square(B->x, B->y, k, &x, &y)) continue;
            const struct map_tile *n = tile_at(x, y);
            if (n->region != t->region) continue;
            if (owner == faction) {
                if (unit_owner(n) == other) {
                    ++*game_diplo_exposed_bases;
                    break;
                }
            } else if (unit_owner(n) == faction) {
                *game_diplo_open_base = b;
                *game_diplo_fear += 8 * B->pop_size;
                break;
            }
        }
    }
    // Their forces on each region where both have units or bases.
    for (int r = 1; r < REGION_COUNT; r++) {
        for (int j = 1; j < FACTION_COUNT; j++)
            if (game_factions[j].region_total_bases[r] >= most[j] - most[j] / 4) regions[r] |= 1 << j;
        int our_bases = me->region_total_bases[r], their_bases = them->region_total_bases[r];
        int our_units = me->region_total_combat_units[r], their_units = them->region_total_combat_units[r];
        if ((!our_units && !our_bases) || (!their_units && !their_bases)) continue;
        int our_arms = me->best_weapon_value + 1, their_arms = them->best_weapon_value + 1;
        int our_force = me->region_force_rating[r] * our_morale / their_morale * our_arms / their_arms;
        int their_force = them->region_force_rating[r] * their_morale / our_morale * their_arms / our_arms;
        int here = regions[r], theirs_here = here & (1 << other);
        if (theirs_here) {
            int against = their_units;
            if (our_bases) against = their_force / (!(here & (1 << faction)) - them->ai_fight + 2) + their_units;
            if (our_force > against) {
                *game_diplo_fear += 32 * (our_force + 1) / clamp(against, 1, 99999);
                continue;
            }
        }
        *game_diplo_hostility = 1;
        if (our_bases) {
            *game_diplo_tribute += their_force;
            if ((here & (1 << faction)) && their_force > our_units) {
                *game_diplo_tribute += their_force;
                if (their_force > 3 * (our_force + our_units) / 2) ++*game_diplo_attack_chance;
            }
            if (their_bases || (*ours & TREATY_PACT)) *game_diplo_tribute -= our_force;
        } else if (their_bases) {
            *game_diplo_tribute -= (our_force - their_units) / (theirs_here ? 1 : 2);
        }
    }
    *game_diplo_tribute *= *game_diplo_exposed_bases + 1;
    if (*game_diplo_exposed_bases && !(*ours & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE))) *game_diplo_hostility = 1;
    if (*game_diplo_near_endgame && *game_diplo_rival_outranks) *game_diplo_hostility = *game_diplo_great_satan ? 2 : 3;
    // The tribute grows with the rival's commerce and grudges, or shrinks
    // with the player's.
    if (me->base_count && them->base_count) {
        if (!(*their & TREATY_WANT_REVENGE) && !(*agenda & AGENDA_UNK_200)) {
            int bonus = clamp(them->ai_wealth - me->tech_commerce_bonus + them->tech_commerce_bonus - fight, 0, 99);
            *game_diplo_tribute -= me->energy_surplus_total / me->base_count * bonus;
            if (*their & TREATY_WANT_TO_TALK) *game_diplo_tribute /= 2;
        } else {
            int bonus = clamp(me->tech_commerce_bonus - them->tech_commerce_bonus + fight, 0, 99);
            *game_diplo_tribute = 2 * (*game_diplo_tribute + them->energy_surplus_total * bonus / them->base_count);
        }
    }
    if (me->best_weapon_value >= 3 * them->best_weapon_value / 2 || me->best_weapon_value > 2 * them->best_armor_value)
        *game_diplo_fear *= 2;
    if (our_record >= 2 * their_record) *game_diplo_fear *= 2;
    if (our_record >= 4 * their_record && !(*their & TREATY_WANT_REVENGE) && *game_diplo_fear <= 8) *game_diplo_fear = 8;
    if (2 * our_record < their_record) *game_diplo_fear /= 2;
    if (4 * our_record < their_record) *game_diplo_fear /= 2;
    if (*game_diplo_fear ||
        (me->diff_level <= (*their & TREATY_WANT_REVENGE ? 2 : 4) && !(*game_rules & RULES_INTENSE_RIVALRY)))
        *game_diplo_attack_chance = 0;
    *game_diplo_tribute = (turn / 10 + their_record) * *game_diplo_tribute / (turn / 10 + our_record);
    // Late enough, a leading player is the great satan to a rival near the
    // top, or one with strong pacts once they have met.
    int late = *game_net_multiplayer ? 0 : 25 * (5 - me->diff_level);
    if (turn >= late + 50 && !*game_diplo_runaway_leader && me->ranking >= (*game_difficulty < 3) + 5) {
        if (them->ranking >= (me->ranking < 6) + 5)
            *game_diplo_great_satan = 1;
        else if (*game_diplo_rival_strong_pacts && (*ours & TREATY_UNK_8000000) && me->ranking >= 6)
            *game_diplo_great_satan = 1;
    }
    if (*game_diplo_great_satan ||
        (((*their & TREATY_WANT_REVENGE) || (*agenda & AGENDA_UNK_200)) && !*game_diplo_open_base &&
         (!*game_diplo_fear || 2 * them->mil_strength >= me->mil_strength))) {
        *game_diplo_fear = 0;
        *game_diplo_hostility = (*game_diplo_great_satan != 0) + 1;
        if (*game_diplo_tribute < 2 * them->ranking) *game_diplo_tribute = 2 * them->ranking;
    }
    if (them->planet_busters && them->major_atrocities) *game_diplo_fear = 0;
    if (me->planet_busters && me->major_atrocities) {
        if (!them->planet_busters) *game_diplo_fear = 32;
        *game_diplo_tribute = them->planet_busters * *game_diplo_tribute / me->planet_busters;
    }
    *game_diplo_conquest_fear = *game_diplo_fear;
    int pact = *their & TREATY_PACT;
    if (pact || !*game_diplo_captured_bases) *game_diplo_conquest_fear = 0;
    int level = *game_rules & RULES_INTENSE_RIVALRY ? 5 : me->diff_level;
    *game_diplo_fear /= (clamp(me->integrity_blemishes / 2, 1, 99) + 1) * (level + 3) / 2;
    // Its enemies the player is allied with.
    *game_diplo_enemy_allies = *game_diplo_enemy_ally = 0;
    if (!pact || !(*their & TREATY_SURRENDERED)) {
        for (int i = 1; i < FACTION_COUNT; i++) {
            uint32_t s = game_factions[i].diplo_status[faction], m = me->diplo_status[i];
            if ((alive >> i & 1) && i != faction && i != other && (them->diplo_status[i] & TREATY_VENDETTA) &&
                (!(s & TREATY_PACT) || !(s & TREATY_SURRENDERED)) &&
                ((m & TREATY_PACT) || ((m & TREATY_TREATY) && fight > 0)))
                *game_diplo_enemy_ally = i, ++*game_diplo_enemy_allies;
        }
    }
    // The friction in talks.
    int *fr = game_diplo_friction;
    *fr += 3 * *game_diplo_enemy_allies;
    if (pact) *fr -= 5;
    if (*their & (TREATY_VENDETTA | TREATY_WANT_REVENGE)) *fr += 5;
    uint32_t a = *agenda;
    if (a & AGENDA_UNK_400) *fr += 3;
    if (a & AGENDA_UNK_800) *fr += 3;
    if (*game_diplo_reputation > 1) *fr += *game_diplo_reputation - 1;
    int commerce = me->tech_commerce_bonus - them->tech_commerce_bonus;
    if (commerce < 0) *fr += 2 * commerce;
    if (commerce > 0) *fr += commerce;
    if (ideology) {
        *fr += 5;
        if (*fr < 5) *fr = 5;
    }
    if (ideology > 1 && !*game_diplo_fear) *fr += 5;
    // Hostility from its agenda, the friction, its betrayal mood and a
    // rival without labs in a game it can't win by them; none if it fears
    // the player or the player is much stronger.
    int grudge = a & AGENDA_UNK_200;
    if (grudge) *game_diplo_hostility = 1;
    if (*fr <= 4) *game_diplo_hostility = 0;
    if (*fr >= 15) *game_diplo_hostility = 4;
    int betrayal = *their & TREATY_UNK_40;
    if (betrayal) *game_diplo_hostility = 1;
    if (!(*game_rules & RULES_VICTORY_TRANSCENDENCE) && !them->alloc_labs) *game_diplo_hostility = 1;
    if (*game_diplo_fear || me->mil_strength > 3 * them->mil_strength) *game_diplo_hostility = 0;
    if (me->best_weapon_value >= 2 * them->best_weapon_value) *game_diplo_hostility = 0;
    // The tribute it asks, in 25s.
    int value = (int32_t)((uint32_t)(level + 1) * (uint32_t)(*game_diplo_reputation + 2) * (uint32_t)(*fr + 10) *
                          (uint32_t)(ideology + 1) * (uint32_t)*game_diplo_tribute) >>
                6;
    if (betrayal) value *= 2;
    int credits = me->energy_credits;
    if (credits >= 2 * them->energy_credits) {
        if (credits >= 2000) value = 3 * value / 2;
        if (credits >= 4000) value = 3 * value / 2;
    }
    if (*game_diplo_great_satan || (((*their & TREATY_WANT_REVENGE) || grudge) && !*game_diplo_open_base &&
                                    (!*game_diplo_fear || 2 * them->mil_strength >= me->mil_strength))) {
        if (me->ranking >= 6) {
            int least = (me->ranking + them->ranking) * credits / 21;
            if (value < least) value = least;
        }
    }
    int tribute = 25 * ((value + 24) / 25);
    if (tribute == 25) tribute = 50;
    *game_diplo_tribute = tribute;
    if (tribute > credits && tribute < 2 * credits && credits >= (betrayal ? 100 : 50) &&
        !(*ours & (TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE)))
        *game_diplo_tribute = tribute = 25 * ((credits + 24) / 25);
    // No more than five turns of the player's income, if the rival is
    // well off for its rank.
    if (!(*their & (TREATY_UNK_40 | TREATY_WANT_REVENGE)) && (!*game_diplo_great_satan || (*their & TREATY_UNK_4000000))) {
        game_energy_compute(faction, 0);
        int income = *game_net_income;
        if (tribute > 5 * income && them->energy_credits >= them->ranking * (2 * me->energy_credits / 3) / me->ranking) {
            *game_diplo_tribute = tribute = 25 * (5 * income / 25);
            if (tribute < 50) *game_diplo_tribute = tribute = 50;
        }
    }
    if ((((*their & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) && !(*their & TREATY_UNK_10000)) || *game_diplo_fear) &&
        !*game_diplo_great_satan && !(*their & TREATY_WANT_REVENGE))
        *game_diplo_attack_chance = 0;
    if (!tribute && !*game_diplo_attack_chance && !(*agenda & AGENDA_UNK_200)) *game_diplo_hostility = 0;
    *game_diplo_prev_hostility = *game_diplo_hostility;
    *game_diplo_goodwill = 0;
    if (full) {
        *game_diplo_help_faction = *game_diplo_unmet_enemy = *game_diplo_player_enemy = 0;
        *game_diplo_unmet_faction = *game_diplo_mutual_faction = 0;
    }
    *game_diplo_rival_wars = 0;
    // The other factions the rival has a commlink with: its pact partners
    // stronger than the player, its wars (and whom it would ask for help,
    // or wants the player to meet), and those the player could meet.
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!(*game_factions_alive >> i & 1) || i == faction || i == other) continue;
        uint32_t ti = them->diplo_status[i];
        if (!(ti & TREATY_COMMLINK)) continue;
        uint32_t mi = me->diplo_status[i];
        const struct faction_data *I = &game_factions[i];
        if (full && (mi & TREATY_VENDETTA) && !(ti & TREATY_VENDETTA) && !(mi & TREATY_UNK_10000000))
            *game_diplo_player_enemy = i;
        if (ti & TREATY_PACT) {
            if (I->mil_strength > me->mil_strength) --*game_diplo_goodwill;
            if ((great_satan(faction, 0) || *game_diplo_great_satan) && (I->diplo_status[faction] & TREATY_VENDETTA)) {
                int f = *game_diplo_fear - 3;
                if (f > 9999) {
                    *game_diplo_fear = 9999;
                    continue;
                }
                *game_diplo_fear = f < 0 ? 0 : f;
                if (f <= 0)
                    *game_diplo_conquest_fear = 0, *game_diplo_prev_hostility = *game_diplo_hostility = 2;
            }
        } else if (ti & TREATY_VENDETTA) {
            ++*game_diplo_rival_wars;
            *game_diplo_goodwill += 2 * (I->mil_strength + them->mil_strength / 2) / (them->mil_strength + 1);
            mi = me->diplo_status[i];
            if (game_diplo_state[other] & 0x40) {
                if ((mi & (TREATY_COMMLINK | TREATY_VENDETTA)) == TREATY_COMMLINK && I->mil_strength > them->mil_strength)
                    *game_diplo_help_faction = i;
                continue;
            }
            uint32_t s = I->diplo_status[faction];
            if (!(mi & TREATY_VENDETTA) && ((mi & TREATY_COMMLINK) || (*ours & TREATY_PACT)) &&
                (!(s & TREATY_PACT) || !(s & TREATY_SURRENDERED))) {
                *game_diplo_help_faction = i;
                continue;
            }
            if (*game_diplo_help_faction || !full || (mi & TREATY_COMMLINK) || is_human(i)) continue;
            if (I->mil_strength > them->mil_strength || !wants_to_attack(other, i, 0) || *game_diplo_fear)
                *game_diplo_unmet_enemy = i;
        } else if (full && !is_human(i)) {
            if (mi & TREATY_COMMLINK) {
                if (!(mi & TREATY_UNK_10000000)) *game_diplo_mutual_faction = i;
            } else if ((!*game_diplo_unmet_faction || game_contiguous(faction, i, 0, 0)) &&
                       ((ti & TREATY_TREATY) || !*game_diplo_unmet_faction)) {
                *game_diplo_unmet_faction = i;
            }
        }
    }
    // Goodwill.
    a = *agenda;
    int32_t *gw = game_diplo_goodwill;
    if (a & AGENDA_UNK_800) --*gw;
    if (a & AGENDA_UNK_400) --*gw;
    int revenge = *their & TREATY_WANT_REVENGE;
    if (revenge) --*gw;
    if (a & AGENDA_UNK_200) --*gw;
    *gw += -*game_diplo_exposed_bases - (*game_diplo_reputation - 1) / 3 - fight;
    if (them->tech_commerce_bonus > me->tech_commerce_bonus && fight <= 0) ++*gw;
    if (!(*game_rules & RULES_VICTORY_TRANSCENDENCE) && !them->alloc_labs) --*gw;
    if (them->mil_strength > me->mil_strength) --*gw;
    if (them->best_weapon_value > 2 * me->best_armor_value) --*gw;
    uint32_t t = *their;
    if (*gw > (int)((*ours >> 4) & 1) && !*game_diplo_great_satan && !(t & (TREATY_UNK_40 | TREATY_PACT)) &&
        !*game_diplo_attack_chance)
        *game_diplo_hostility = *game_diplo_tribute = 0;
    if ((t & TREATY_PACT) && (t & TREATY_SURRENDERED)) {
        *game_diplo_attack_chance = *game_diplo_exposed_bases = *game_diplo_prev_hostility = *game_diplo_hostility = 0;
    } else if ((*game_diplo_exposed_bases || *game_diplo_attack_chance) && revenge) {
        *game_diplo_prev_hostility = *game_diplo_hostility = 1;
    }
    // A weak rival, alone, against a great satan may surrender.
    if (great_satan(faction, 1) && *game_diplo_rival_pacts < 2 &&
        game_ranking_scores[faction] >= *game_diplo_rival_pact_score + 3 * game_ranking_scores[other] &&
        them->ranking < 6 && them->mil_strength < me->mil_strength) {
        *game_diplo_can_surrender = 1;
        if ((*their & TREATY_VENDETTA) && !(*agenda & AGENDA_FIGHT_TO_DEATH) && !(*their & TREATY_ATROCITY_VICTIM))
            *game_diplo_hostility = 0;
    } else {
        *game_diplo_can_surrender = 0;
    }
    // Whether it would betray the player.
    if (full) {
        scan_prototypes(faction, other);
        int shameless = (them->flags_ext & FACTION_FLAG_EXT_SHAMELESS) != 0, host = *game_diplo_hostility;
        t = *their;
        *game_diplo_can_betray =
            (me->diff_level > 1 || shameless) && !*game_diplo_exposed_bases && !(t & TREATY_PACT) &&
            !(*agenda & AGENDA_FIGHT_TO_DEATH) && (!(t & TREATY_VENDETTA) || *game_diplo_captured_bases > 1 || shameless) &&
            (*game_diplo_near_endgame || host || ideology || (shameless && !*game_diplo_fear) ||
             (*game_diplo_enemy_allies && (t & TREATY_UNK_800000) && *fr >= 12)) &&
            ((t & TREATY_WANT_REVENGE) || (shameless && host) || *game_diplo_reputation > 5 ||
             ((host || ideology > 1) && (me->mil_strength > them->mil_strength || shameless)));
    }
    if ((them->flags & FACTION_FLAG_COOP_WITH_HUMAN) && !(*their & TREATY_WANT_REVENGE)) *game_diplo_hostility = 0;
    if (*agenda & AGENDA_FIGHT_TO_DEATH) {
        *game_diplo_hostility = 1;
    } else if (*game_diplo_can_betray) {
        *game_diplo_hostility = 0;
        if (*fr > 6) *fr = 6;
    }
    uint32_t o = *ours;
    int wars = *game_diplo_rival_wars;
    if ((o & TREATY_PACT) && (!(o & TREATY_WANT_REVENGE) || wars > 1) && (!(o & TREATY_UNK_200) || wars) &&
        them->ranking < me->ranking && ((*game_rules & RULES_VICTORY_COOPERATIVE) || wars))
        *game_diplo_hostility = 0;
    if (*agenda & AGENDA_UNK_2000) {
        *game_diplo_can_betray = 0;
        *game_diplo_hostility = (*their & TREATY_VENDETTA) != 0;
    }
    if (!*game_diplo_prev_hostility && (*game_diplo_fear || (o & TREATY_PACT)) && *fr > 14) *fr = 14;
    if ((*game_diplo_hostility || (o & TREATY_VENDETTA) || (*their & TREATY_WANT_REVENGE)) && *fr < 6) *fr = 6;
}

// The prototypes the rival `other` might show `faction`: its best (by
// speed, arms and plan; a sixteenth for one `faction` has a match for, of
// the same plan and triad), and the combat design it would mention, not
// already mentioned (nor given, in a pact) and unmatched, or in a vendetta
// a defensive one at a sixteenth. The original rates the armor of both
// designs by their weapons (arm_strat of the weapon), which Thinker fixes.
void scan_prototypes(int faction, int other) {
    *game_diplo_best_proto = *game_diplo_mention_proto = -1;
    int best = 0, mention = 0;
    for (int i = 0; i < 64; i++) {
        int id = other * 64 + i;
        const struct unit_proto *u = &game_protos[id];
        if (!(u->flags & PROTO_FLAG_ACTIVE) || !(u->flags & PROTO_FLAG_BUILT) || (u->obsolete_factions & (1 << other)))
            continue;
        const struct rules_chassis *ch = &game_chassis[u->chassis];
        int score = ch->speed + 3 * game_weapons[u->weapon].offense +
                    4 * (game_armor[u->armor].defense + (7 - u->plan) * (1 << 14));
        int better = 1;
        for (int j = 0; j < 64 && better; j++) {
            const struct unit_proto *p = &game_protos[faction * 64 + j];
            if (!(p->flags & PROTO_FLAG_ACTIVE) || !(p->flags & PROTO_FLAG_BUILT) ||
                (p->obsolete_factions & (1 << faction)) || p->plan != u->plan)
                continue;
            const struct rules_chassis *pc = &game_chassis[p->chassis];
            if (pc->triad != ch->triad) continue;
            if (p->reactor > u->reactor) better = 0;
            if (pc->speed > ch->speed) better = 0;
            int pw = game_weap_strat(p->weapon, faction), uw = game_weap_strat(u->weapon, faction);
            int pa = game_arm_strat(p->weapon, faction), ua = game_arm_strat(u->weapon, faction);
            if (pc->speed >= ch->speed && pw >= uw && pa >= ua && (pw > uw || pa > ua)) better = 0;
        }
        int rated = better ? score : score >> 4;
        if (rated > best) best = rated, *game_diplo_best_proto = id;
        uint32_t st = game_factions[faction].diplo_status[other];
        if (u->flags & (st & TREATY_PACT ? PROTO_FLAG_OFFERED : PROTO_FLAG_MENTIONED | PROTO_FLAG_OFFERED)) continue;
        if (u->plan > 6 || u->plan == PLAN_RECON) continue;
        if (!better) score = (st & TREATY_VENDETTA) && u->plan == PLAN_DEFENSIVE ? score >> 4 : 0;
        if (score > mention) mention = score, *game_diplo_mention_proto = id;
    }
}

// The rival mentions the prototype scan_prototypes() chose, if the player
// will talk: all its designs count as mentioned. In a pact, the original
// only notes it as offered and puts the text together; otherwise it offers
// to swap designs, and on a yes each side gets those of the other's it
// wants. (Thinker restores the pact's offer, which the original never
// shows.)
void mention_prototypes(int faction, int other) {
    *game_diplo_second = other;
    if (*game_diplo_mention_proto < 0 || !wants_to_speak(faction, other)) return;
    for (int i = 0; i < 64; i++) game_protos[other * 64 + i].flags |= PROTO_FLAG_MENTIONED;
    int id = *game_diplo_mention_proto;
    game_parse_says(0, game_protos[id].name, -1, -1);
    game_battle_text[0] = 0;
    game_say_stats_2(game_battle_text, id);
    game_parse_says(1, game_battle_text, -1, -1);
    game_protos[id].flags |= PROTO_FLAG_MENTIONED;
    game_battle_text[0] = 0;
    strcat(game_battle_text, text(0x68d598));
    if (game_factions[faction].diplo_status[other] & TREATY_PACT) {
        game_protos[id].flags |= PROTO_FLAG_OFFERED;
        strcat(game_battle_text, text(0x68d5a0));
        return;
    }
    strcat(game_battle_text, text(0x68d5a8));
    if (game_x_pops_11(game_battle_text, 0x100000, game_faction_portraits[other], game_pop_wait)) {
        for (int j = 0; j < 64; j++) {
            int mine = faction * 64 + j, theirs = other * 64 + j;
            if (game_wants_prototype(mine, other)) {
                if (*game_net_multiplayer) {
                    game_log_say(text(0x68d4f0), other, mine + 0x61, faction);
                    game_message_data(0x244b, 0, other, mine + 0x61, faction, 0);
                    game_net_daemon_await_diplo(game_net_daemon, 0x44b);
                } else {
                    game_tech_achieve(other, mine + 0x61, faction, 0);
                }
            }
            game_protos[mine].flags |= PROTO_FLAG_OFFERED;
            if (game_wants_prototype(theirs, faction)) {
                if (*game_net_multiplayer) {
                    game_log_say(text(0x68d4f0), faction, theirs + 0x61, other);
                    game_message_data(0x244b, 0, faction, theirs + 0x61, other, 0);
                    game_net_daemon_await_diplo(game_net_daemon, 0x44b);
                } else {
                    game_tech_achieve(faction, theirs + 0x61, other, 0);
                }
            }
            game_protos[theirs].flags |= PROTO_FLAG_OFFERED;
        }
    }
    *game_diplo_best_proto = *game_diplo_mention_proto = -1;
}

// Whether the player (`faction`) takes the rival's call, opening the
// diplomacy window if so. Without Psych (theirs or Progenitor Psych,
// between an alien faction and a human one) they can't talk. Once
// decided, the answer stands (game_diplo_caption_off). A call already
// taken opens with an urgent message, at 8 and up; otherwise the player
// is asked, and may first look at the rival's dossier; a rival with planet
// busters (bit 0x4000 unset) calls urgently instead, which in the original
// ends the call unanswered. The popup lives where the original's frame
// keeps it (see wants_to_speak below).
int wants_to_speak_in(uint8_t *pop, int faction, int other);
int wants_to_speak_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    *game_diplo_second = other;
    if (alien(faction) != alien(other)) {
        int psych = alien(faction) ? has_tech(TECH_PRPSYCH, other) || has_tech(TECH_PSYCH, faction)
                                   : has_tech(TECH_PRPSYCH, faction) || has_tech(TECH_PSYCH, other);
        if (!psych) {
            *game_diplo_caption_mode = 0;
            game_popup_dtor(pop);
            return 0;
        }
    }
    if (*game_diplo_caption_off) {
        int mode = *game_diplo_caption_mode;
        game_popup_dtor(pop);
        return mode;
    }
    if (*game_diplo_caption_mode) {
        *game_diplo_caption_off = 1;
        if (*game_diplo_caption_mode >= 8) {
            diplomacy_check(faction, other, 0);
            game_diplomacy_caption(faction, other);
            say_title(0, other), say_name(1, other), say_noun(2, other);
            game_parse_num(0, game_random(0, 10) + 1);
            game_parse_says(3, game_get_him_her(other, 0), -1, -1);
            game_x_pops(text(0x68d654), game_faction_portraits[other], game_pop_wait);
        }
    } else {
        game_info_win_set_state(game_info_win, 2, 1);
        int urgent = 0;
        while (!*game_diplo_caption_off) {
            if (game_factions[other].planet_busters && !(game_factions[other].diplo_status[faction] & 0x4000)) urgent = 1;
            diplomacy_check(faction, other, 0);
            int x = *game_encounter_x, y = *game_encounter_y;
            if (on_map(x, y) && !game_console_focus(game_console, x, y, faction))
                game_draw_tiles(*game_encounter_x, *game_encounter_y, 2);
            game_diplomacy_caption(faction, other);
            say_title(0, other), say_name(1, other), say_noun(2, other);
            game_parse_num(0, game_random(0, 10) + 1);
            game_parse_says(3, game_get_him_her(other, 0), -1, -1);
            if (urgent) {
                game_x_pops(text(0x68d660), game_faction_portraits[other], game_pop_wait);
                break;
            }
            game_popup_start(pop, game_pop_file, text(0x68d66c), -1, 0, 0, 0);
            void *portrait = game_faction_portraits[other];  // the popup's icon
            *(void **)(pop + 0x2144) = *(int32_t *)(pop + 0x2274) ? NULL : portrait;
            game_sprite_box_sprite(pop + 0x2240, portrait, 0, 0);
            int choice = game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait);
            if (choice <= 1) {
                *game_diplo_caption_off = 1;
                *game_diplo_caption_mode = choice == 0;
                break;
            }
            game_report_intel(other);
        }
    }
    if (*game_diplo_caption_mode) {
        game_diplo_pop_start(game_diplo_pop, other);
        game_introduce(faction, other);
    }
    int mode = *game_diplo_caption_mode;
    game_popup_dtor(pop);
    return mode;
}
__asm__(".text\n"
        ".globl wants_to_speak\n"
        "wants_to_speak:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5400, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5388(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call wants_to_speak_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// When a pact ends, each side's units in the other's bases or territory
// go to the nearest of their own bases (for a ship, one on the coast and
// on its sea, unless it is in a port): their orders are cleared, an
// aircraft's fuel refilled. Returns how many moved. For a ship the
// original asks whether the base numbered by the candidate base's x is on
// the coast, with its y as the third argument; Thinker asks of the base.
int pact_withdraw(int faction, int other) {
    int moved = 0;
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *V = &game_vehs[v];
        if (V->faction != faction) continue;
        int b = base_find(V->x, V->y);
        if (b < 0) continue;
        if (game_bases[b].faction != other && whose_territory(faction, V->x, V->y, NULL, 0) != other) continue;
        int best = -1, best_dist = 9999;
        for (int i = 0; i < *game_base_count; i++) {
            const struct base *B = &game_bases[i];
            if (B->faction != faction) continue;
            if (game_veh_triad(v) == TRIAD_SEA) {
                const struct base *Q = &game_bases[B->x];
                if (!is_coast(Q->x, Q->y, B->y)) continue;
            }
            if (game_veh_triad(v) == TRIAD_SEA) {
                const struct map_tile *t = tile_at(V->x, V->y);
                if ((t->climate & 0xe0) < 0x60 && !base_on_sea(i, t->region)) continue;
            }
            int dist = vector_dist_offset(x_dist(B->x, V->x), B->y - V->y);
            if (dist < best_dist) best_dist = dist, best = i;
        }
        if (best < 0) continue;
        veh_drop(veh_lift(v), game_bases[best].x, game_bases[best].y);
        V->order = 0;
        V->state &= 0xfcffbdffu;
        if (game_veh_triad(v) == TRIAD_AIR) V->movement_turns = 0;
        moved++;
        if (*game_console_veh_mode && v == *game_veh_ref_map_1) game_console_set_move(game_console, 1);
    }
    return moved;
}

// A pact ends: units withdraw both ways, and the player hears of it (with
// how many of its units came home), or of a pact between others it spies
// on or is allied with.
void pact_ends_in(uint8_t *pop, int faction, int other);
void pact_ends_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    *game_diplo_second = other;
    treaty_off(faction, other, TREATY_PACT);
    int moved = pact_withdraw(faction, other), moved_back = pact_withdraw(other, faction);
    game_draw_map(1);
    int n;
    if (faction == *game_current_faction) {
        say_noun(0, other);
        game_parse_says(1, game_faction_defs[other].adjective, -1, -1);
        n = moved;
    } else if (other == *game_current_faction) {
        say_noun(0, faction);
        game_parse_says(1, game_faction_defs[faction].adjective, -1, -1);
        n = moved_back;
    } else {
        int player = *game_current_faction;
        if (game_spying(faction) || game_spying(other) || (*game_state & GAME_STATE_OMNISCIENT) ||
            (game_factions[player].diplo_status[faction] & TREATY_PACT) ||
            (game_factions[player].diplo_status[other] & TREATY_PACT)) {
            say_noun(0, faction), say_noun(1, other);
            game_parse_says(2, game_get_pact_hood(faction, other), -1, -1);
            game_x_pop(text(0x68d5c8), 0);
        }
        game_popup_dtor(pop);
        return;
    }
    game_parse_num(0, n);
    game_parse_says(2, game_get_pact_hood(faction, other), -1, -1);
    int flags = *game_net_multiplayer ? 0 : 0x100000;
    game_popup_start(pop, game_pop_file, text(0x68d5b0), -1, 0, flags, 0);
    if (n) game_popup_start(pop, game_pop_file, text(0x68d5bc), -1, 0, flags | 0x80, 0);
    game_base_pop_exec_3(pop, 0, 0);
    game_popup_dtor(pop);
}
__asm__(".text\n"
        ".globl pact_ends\n"
        "pact_ends:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5400, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5390(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call pact_ends_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// Whether the player goes ahead with breaking `status` with `other`: a
// pact is broken with a notice; a treaty, a truce (without 0x10000) or
// peace (without 0x10000 in `status`) only if the player says yes.
int break_treaty(int faction, int other, uint32_t status) {
    uint32_t st = game_factions[faction].diplo_status[other];
    if ((st & TREATY_VENDETTA) || !(st & status)) return 0;
    say_title(0, other), say_name(1, other), say_noun(2, other);
    game_parse_says(3, game_get_pact_hood(faction, other), -1, -1);
    game_parse_says(4, game_faction_defs[other].adjective, -1, -1);
    st = game_factions[faction].diplo_status[other];
    if (st & TREATY_PACT) {
        game_netmsg_pop(game_netmsg, text(0x68e7b0), -5000, 0, text(0x68e7a0));
        return 1;
    }
    if (st & TREATY_TREATY) return !game_popp(*game_script_file, text(0x68e7d0), 0, text(0x68e7c0), 0);
    if ((st & TREATY_TRUCE) && !(st & TREATY_UNK_10000))
        return !game_popp(*game_script_file, text(0x68e7f0), 0, text(0x68e7e0), 0);
    if (status & TREATY_UNK_10000) return 0;
    return !game_popp(*game_script_file, text(0x68e810), 0, text(0x68e800), 0);
}

// The victim wants revenge on a human double-crosser, and remembers it.
static void wronged(int victim, int faction) {
    if (game_factions[victim].diplo_agenda[faction] & AGENDA_UNK_400) game_set_agenda(victim, faction, AGENDA_UNK_800, 1);
    if (game_factions[victim].diplo_status[faction] & TREATY_WANT_REVENGE)
        game_set_agenda(victim, faction, AGENDA_UNK_400, 1);
    set_treaty(victim, faction, TREATY_WANT_REVENGE, 1);
}

// `faction` turns on `victim`, incited by `other` (or -1): loans to it are
// void, and breaking a pact or treaty costs it reputation and integrity
// (unless the victim committed atrocities, or `other` has a better treaty
// with it), the inciter's friction, and the victim's revenge if it is
// human. A pact ends (with its units withdrawn); then a vendetta, the
// betrayal's mood between a human and another, and the victim's allies'
// aid. In a network game the victim's player is told first. The original
// compares the two factions' human bits as masks, so the mood is set
// unless both are AIs, where Thinker sets it when only one is human.
void double_cross(int faction, int victim, int other) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[victim];
    if (*game_net_multiplayer) {
        if (victim == *game_current_faction && other > 0) {
            say_title(0, other), say_name(1, other), say_noun(2, other);
            say_title(3, faction), say_name(4, faction), say_noun(5, faction);
            game_netmsg_pop(game_netmsg, text(0x68a31c), 5000, 0, NULL);
        } else if (victim == *game_current_faction && is_human(faction)) {
            say_title(0, faction), say_name(1, faction), say_noun(2, faction);
            game_netmsg_pop(game_netmsg, text(0x68a324), 5000, 0, NULL);
        }
    }
    them->loan_balance[faction] = 0;
    uint32_t st = me->diplo_status[victim];
    int deserved = (st & TREATY_ATROCITY_VICTIM) || them->major_atrocities, allowed = 0;
    if (other >= 0) {
        uint32_t s = me->diplo_status[other];
        if (s & TREATY_PACT) allowed = !(st & TREATY_PACT);
        else if (s & TREATY_TREATY) allowed = !(st & TREATY_TREATY);
        else if (s & TREATY_TRUCE) allowed = !(st & TREATY_TRUCE);
        me->diplo_gifts[other]++;
    }
    st = me->diplo_status[victim];
    int peace = !(me->diplo_agenda[victim] & AGENDA_UNK_20);
    if (st & TREATY_PACT) {
        if (peace) {
            me->diplo_reputation = me->diplo_reputation - 1 < 0 ? 0 : me->diplo_reputation - 1;
            if (me->diff_level && !deserved && !allowed) me->integrity_blemishes++, them->diplo_betrayed[faction]++;
            if (other < 0) {
                if (!deserved) me->integrity_blemishes++;
                me->diplo_wrongs[victim]++;
                them->diplo_betrayed[faction]++;
            } else {
                cause_friction(other, faction, -4);
            }
            if (is_human(faction)) wronged(victim, faction);
        }
        pact_ends(faction, victim);
        treaty_on(faction, victim, TREATY_VENDETTA);
        return;
    }
    if (peace) {
        if (st & (TREATY_TREATY | TREATY_TRUCE)) {
            if (!(st & TREATY_UNK_10000)) {
                me->diplo_reputation = me->diplo_reputation - 1 < 0 ? 0 : me->diplo_reputation - 1;
                if ((great_satan(faction, 0) || (me->ranking >= 6 && them->ranking <= 4)) && other < 0 && !deserved)
                    me->integrity_blemishes++, them->diplo_betrayed[faction]++;
            }
            if (me->diff_level && !deserved && !allowed) me->integrity_blemishes++, them->diplo_betrayed[faction]++;
            if (other < 0) me->diplo_wrongs[victim]++;
            if (is_human(faction)) wronged(victim, faction);
            if (other >= 0) cause_friction(other, faction, -2);
        } else if (other >= 0) {
            if (is_human(faction)) wronged(victim, faction);
            cause_friction(other, faction, -2);
        }
    }
    treaty_on(faction, victim, TREATY_VENDETTA);
    if (!(me->diplo_agenda[victim] & AGENDA_UNK_20)) {
        if (is_human(faction) || is_human(victim)) {
            set_treaty(faction, victim, TREATY_UNK_40, 1);
            me->diplo_spoke[victim] = *game_turn;
        }
        intervention(victim, faction);
    }
}

// Two factions meet: a commlink between them, and bit 0x40000000 when
// they met on the map.
static void meet(int faction, int other, uint32_t status, int x, int y) {
    treaty_on(faction, other, status);
    if (on_map(x, y)) treaty_on(faction, other, TREATY_UNK_40000000);
}

// The tutorial's note on meeting another faction, for the player `self`;
// `labels` are the five notes' addresses (one set for each order of the
// two).
static void meeting_note(int self, int them, int x, int y, const uint32_t labels[5]) {
    say_noun(0, them);
    int bit, k;
    if (!alien(self)) bit = 0x8000, k = !has_tech(TECH_PRPSYCH, self) && !has_tech(TECH_PSYCH, them) ? 0 : 1;
    else if (alien(them)) bit = 0x10000, k = 2;
    else bit = 0x4000, k = !has_tech(TECH_PSYCH, self) && !has_tech(TECH_PRPSYCH, them) ? 3 : 4;
    if (game_tut_check2(bit)) game_tut_map(game_tut_win, text(labels[k]), x, y, -1, NULL, 0, -1, -1);
}

// Two factions meet, at (x, y) or off the map, or a faction meets native
// life (`other` 0: a fungal tower or a native unit, which an alien player
// is told of). Two alien factions open a commlink, with the Caretakers'
// and the Usurpers' scenes. An alien faction and a human one can't talk
// without Psych (or the aliens' Progenitor Psych), and the player is told
// either way. Under sunspots they only open a commlink. Otherwise each may
// call the other (comm_check), the player may get the tutorial's note on
// a first meeting with aliens, and two AIs, on their first meeting or
// every fourth turn, go to war or make a treaty, or trade techs with
// agenda bit 0x2000.
void encounter(int faction, int other, int x, int y, int tower, int native) {
    if (!faction || !*game_turn) return;
    if (!other) {
        if (faction == *game_current_faction && alien(faction)) {
            if (tower) game_interlude(44, NULL, 1, 0);
            if (native) game_interlude(43, NULL, 1, 0);
        }
        return;
    }
    if (alien(faction) && alien(other)) {
        meet(faction, other, TREATY_UNK_80000000 | TREATY_COMMLINK, x, y);
        game_battle_text[0] = 0;
        strcat(game_battle_text, game_faction_defs[faction].search_key);
        (*game_char_upper)(game_battle_text);
        int caretakers = !strcmp(game_battle_text, text(0x68e698));
        if (faction == *game_current_faction) game_interlude(caretakers ? 39 : 40, NULL, 1, 0);
        if (other == *game_current_faction) game_interlude(caretakers ? 40 : 39, NULL, 1, 0);
        return;
    }
    if (alien(faction)) {
        if (!has_tech(TECH_PRPSYCH, other) && !has_tech(TECH_PSYCH, faction)) {
            meet(faction, other, TREATY_UNK_80000000 | TREATY_COMMLINK, x, y);
            if (faction == *game_current_faction) game_interlude(34, NULL, 1, 0);
            if (other == *game_current_faction) game_interlude(22, NULL, 1, 0);
            return;
        }
        if (faction == *game_current_faction) game_interlude(35, NULL, 1, 0);
        if (other == *game_current_faction) game_interlude(23, NULL, 1, 0);
    }
    if (alien(other)) {
        if (!has_tech(TECH_PRPSYCH, faction) && !has_tech(TECH_PSYCH, other)) {
            meet(faction, other, TREATY_UNK_80000000 | TREATY_COMMLINK, x, y);
            if (faction == *game_current_faction) game_interlude(22, NULL, 1, 0);
            if (other == *game_current_faction) game_interlude(34, NULL, 1, 0);
            return;
        }
        if (faction == *game_current_faction) game_interlude(23, NULL, 1, 0);
        if (other == *game_current_faction) game_interlude(35, NULL, 1, 0);
    }
    if (*game_sunspot_duration > 0) {
        meet(faction, other, TREATY_UNK_80000000 | TREATY_COMMLINK, x, y);
        return;
    }
    *game_encounter_x = x, *game_encounter_y = y;
    game_comm_check(faction, other);
    game_comm_check(other, faction);
    *game_encounter_y = *game_encounter_x = -1;
    uint32_t had = game_factions[faction].diplo_status[other] & TREATY_COMMLINK;
    if (!had && (alien(faction) || alien(other))) {
        static const uint32_t first[5] = {0x68e6b0, 0x68e6a4, 0x68e6c4, 0x68e6dc, 0x68e6d0};
        static const uint32_t second[5] = {0x68e6fc, 0x68e6f0, 0x68e710, 0x68e728, 0x68e71c};
        if (faction == *game_current_faction) meeting_note(faction, other, x, y, first);
        else if (other == *game_current_faction) meeting_note(other, faction, x, y, second);
    }
    meet(faction, other, TREATY_UNK_80000000 | TREATY_UNK_8000000 | TREATY_COMMLINK, x, y);
    if (is_human(faction) || is_human(other)) return;
    if (((*game_turn + faction + other) & 3) && had) return;
    if (alien(faction) && alien(other)) return;
    if (game_factions[faction].diplo_agenda[other] & AGENDA_UNK_2000) {
        game_enemies_trade_tech(faction, other);
        return;
    }
    if (wants_to_attack(faction, other, 0) || wants_to_attack(other, faction, 0)) game_enemies_war(faction, other);
    else game_enemies_treaty(faction, other, 1);
}

// How far `other` presses into `faction`'s territory, outside a pact or
// vendetta (or bit 0x200, unless `flags` has 2): its land units (combat,
// colony pods, convoys) on `faction`'s land, each counting one more on an
// improved square, but not on regions where both fight a common enemy
// (unless `faction` wants revenge or `other` is treacherous). With `flags`
// 1, a unit within two squares of the base, or anywhere when `faction` is
// set on betrayal or the friction runs high, is marked; one marked before
// (not the first time, nor under sunspots) counts 5 more with a treaty,
// else 1. `base` is the last base pressed, `count` the units warned twice.
// The original takes the distance as the sum of the two offsets, made even.
int territory(int faction, int other, int flags, int *base, int *count) {
    int n = 0;
    if (base) *base = -1;
    if (count) *count = 0;
    uint32_t st = game_factions[faction].diplo_status[other];
    if (st & (TREATY_PACT | TREATY_VENDETTA)) return 0;
    if (!(flags & 2) && (st & TREATY_UNK_200)) return 0;
    int skip[LAND_REGION_COUNT];
    for (int r = 0; r < LAND_REGION_COUNT; r++) {
        skip[r] = 0;
        if ((st & TREATY_WANT_REVENGE) || game_factions[other].integrity_blemishes >= 5) continue;
        for (int j = 1; j < FACTION_COUNT; j++)
            if (j != faction && j != other && (game_factions[faction].diplo_status[j] & TREATY_VENDETTA) &&
                (game_factions[other].diplo_status[j] & TREATY_VENDETTA) && game_factions[j].region_total_bases[r]) {
                skip[r] = 1;
                break;
            }
    }
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *V = &game_vehs[v];
        V->state &= ~(uint32_t)VEH_STATE_UNK_400;
        if (V->faction != other) continue;
        int plan = game_protos[V->proto].plan;
        if (plan > PLAN_COLONY && (plan != PLAN_SUPPLY || V->order != ORDER_CONVOY)) continue;
        const struct map_tile *t = tile_at(V->x, V->y);
        if ((t->climate & 0xe0) < 0x60 || (t->region < LAND_REGION_COUNT && skip[t->region])) continue;
        int b;
        if (whose_territory(other, V->x, V->y, &b, 1) != faction || b < 0) continue;
        if (base) *base = b;
        V->state |= VEH_STATE_UNK_400;
        n++;
        if (t->bits & 0x81608850) n++;
        if (!(flags & 1)) continue;
        const struct base *B = &game_bases[b];
        if (((x_dist(B->x, V->x) + abs(B->y - V->y)) & ~1) > 4 &&
            !(game_factions[faction].diplo_status[other] & (TREATY_SHALL_BETRAY | TREATY_UNK_800 | TREATY_WANT_REVENGE)) &&
            *game_diplo_friction <= 12)
            continue;
        if (count && (V->flags & 1)) ++*count;
        V->flags |= 3;
        if (!(V->state & VEH_STATE_UNK_800) && *game_sunspot_duration <= 0) {
            V->state |= VEH_STATE_UNK_800;
            continue;
        }
        n += game_factions[faction].diplo_status[other] & TREATY_TREATY ? 5 : 1;
    }
    if (n) game_factions[faction].flags |= FACTION_FLAG_UNK_10000;
    return n;
}

// `faction` commits an atrocity against `other`: the victim wants revenge.
// Under the UN Charter, the factions in contact with the culprit (aliens
// with a human one only by `alien_too`, in the expansion) take note, those
// with bit 0x200000 with friction, and an AI may declare a vendetta on a
// human culprit (sure if it wants revenge; by chance otherwise, likelier
// against a great satan or on harder levels). The first atrocity, or any
// with `always`, counts and blemishes the culprit's integrity; too many
// make a major atrocity, else sanctions for ten turns each. Under sunspots
// the player only hears that no one saw. The original marks the victim as
// having seen wanton atrocities by the human bit of faction 8 (its loop's
// end), always clear, where Thinker tests the victim's.
void atrocity(int faction, int other, int always, int alien_too) {
    struct faction_data *me = &game_factions[faction];
    uint32_t *victim = &game_factions[other].diplo_status[faction];
    uint32_t was_victim = *victim & TREATY_ATROCITY_VICTIM;
    *victim |= TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE;
    game_factions[other].diplo_agenda[faction] |= AGENDA_UNK_4;
    if (!un_charter()) return;
    if (*game_sunspot_duration > 0) {
        if (faction == *game_current_faction) game_netmsg_pop(game_netmsg, text(0x68e73c), 5000, 0, NULL);
        return;
    }
    if (*game_expansion && !alien_too && (alien(faction) || alien(other))) return;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || i == other) continue;
        uint32_t *s = &game_factions[i].diplo_status[faction];
        if (!(*s & TREATY_COMMLINK)) continue;
        if (*game_expansion && alien_too && !alien(faction) && alien(i)) continue;
        if (*s & TREATY_UNK_200000) {
            cause_friction(i, faction, 5);
            uint32_t mine = me->diplo_status[i];
            if (!is_human(i) && is_human(faction) && (mine & TREATY_COMMLINK) && !(mine & (TREATY_PACT | TREATY_VENDETTA)) &&
                !was_victim) {
                int war;
                if (*s & TREATY_WANT_REVENGE) {
                    war = 1;
                } else if (great_satan(faction, 0)) {
                    war = game_rand() % 16 - me->atrocities <= 8;
                } else {
                    int level = *game_rules & RULES_INTENSE_RIVALRY ? 5 : *game_difficulty;
                    war = !game_rnd(2 * (8 - level));
                }
                if (war) {
                    if (faction == *game_current_faction) {
                        say_title(0, faction), say_name(1, faction);
                        game_diplomacy_caption(faction, i);
                        game_x_pops(text(0x68e75c), game_faction_portraits[i], NULL);
                    }
                    treaty_on(faction, i, TREATY_VENDETTA);
                    *s |= TREATY_UNK_40;
                    game_factions[i].diplo_merc[faction] = 50;
                }
            }
        }
        if (!(*s & TREATY_VENDETTA)) treaty_on(i, faction, TREATY_UNK_400000);
    }
    if (always || !me->atrocities) {
        me->atrocities++;
        if (!(me->diplo_status[other] & TREATY_ATROCITY_VICTIM)) me->integrity_blemishes++;
    }
    if (me->atrocities > 4 * (8 - me->diff_level)) {
        major_atrocity(faction, other);
        return;
    }
    int turns = 10 * me->atrocities;
    if (is_human(faction) && !was_victim && me->atrocities >= 5) game_factions[other].flags |= FACTION_FLAG_WANTON_ATROCITIES;
    say_title(0, faction), say_name(1, faction), say_title(2, other), say_name(3, other);
    game_parse_num(0, 10 * me->atrocities);
    game_battle_text[0] = 0;
    strcat(game_battle_text, text(0x68e76c));
    if (faction == *game_current_faction) strcat(game_battle_text, text(0x68e778));
    else if (other == faction) strcat(game_battle_text, text(0x68e77c));
    if (!alien(faction)) game_netmsg_pop(game_netmsg, game_battle_text, 5000, 0, NULL);
    me->sanction_turns += turns;
}

// A major atrocity against `other` (or -1, against everyone): the victim's
// revenge; under the UN Charter, more blemishes on the culprit's integrity
// (at least 6, unless the victim already suffered one), and every other AI
// in the game not already at war with it ends its pact and declares a
// vendetta, which the player hears of.
void major_atrocity(int faction, int other) {
    struct faction_data *me = &game_factions[faction];
    if (other >= 0) {
        game_factions[other].diplo_status[faction] |= TREATY_MAJOR_ATROCITY_VICTIM | TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE;
        game_factions[other].diplo_agenda[faction] |= AGENDA_UNK_4;
    }
    if (!un_charter()) return;
    if (other < 0) me->integrity_blemishes += 2;
    else if (!(me->diplo_status[other] & TREATY_MAJOR_ATROCITY_VICTIM))
        me->integrity_blemishes = clamp(me->integrity_blemishes + 2, 6, 99);
    me->major_atrocities++;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || i == other || is_human(i) || !(*game_factions_alive >> i & 1)) continue;
        if (me->diplo_status[i] & TREATY_VENDETTA) continue;
        if (me->diplo_status[i] & TREATY_PACT) pact_ends(faction, i);
        treaty_on(i, faction, TREATY_VENDETTA | TREATY_COMMLINK);
        game_factions[i].diplo_status[faction] |= TREATY_UNK_40;
        game_factions[i].diplo_merc[faction] = 50;
        me->diplo_spoke[i] = *game_turn;
        say_title(0, i), say_name(1, i), say_noun(2, i);
        say_title(3, faction), say_name(4, faction), say_noun(5, faction);
        game_battle_text[0] = 0;
        strcat(game_battle_text, text(0x68e780));
        strcat(game_battle_text, faction != *game_current_faction ? "1" : "0");
        game_popp(*game_script_file, game_battle_text, 0, text(0x68e790), 0);
    }
}

// ---- enemies_treaty --------------------------------------------------------------

// Whether the player (the current faction) sees what passes between two
// AIs: through the Empath Guild or an infiltrator, or as the governor
// (game_spying(), of either), or in a game where all is seen
// (OMNISCIENT, when `omniscient`).
static int sees_deal(int faction, int other, int omniscient) {
    return game_spying(faction) || game_spying(other) || (omniscient && (*game_state & GAME_STATE_OMNISCIENT));
}

// The news of two AIs' truce or treaty, the player being in a pact with
// the first (`a`): "A pact partner (title, name, noun, he or she) and the
// other (title, name, noun)", or for a treaty only the other's noun.
static void news_pact(int a, int b, uint32_t label, int truce) {
    say_title(0, a), say_name(1, a), say_noun(2, a);
    game_parse_says(3, game_get_he_she(a, 0), -1, -1);
    if (truce) say_title(4, b), say_name(5, b), say_noun(6, b);
    else say_noun(4, b);
    game_netmsg_pop(game_netmsg, text(label), 5000, 0, NULL);
}
// And of one the player is not in a pact with, if it can see it: both
// nouns.
static void news_other(int a, int b, uint32_t label) {
    say_noun(0, a), say_noun(1, b);
    game_netmsg_pop(game_netmsg, text(label), 5000, 0, NULL);
}

// Two AIs (`faction` and `other`) who have met make peace, step by step:
// from a vendetta or a truce to a treaty (or a truce, if they have
// reason to be wary of each other), from a treaty to a pact, or back down
// to a truce. With `trade` they first trade techs
// (enemies_trade_tech()). The player is told of each step if it is in a
// pact with either, or sees what they do (a pact's news is its partner's
// own; the others are shown to whoever sees both: the Empath Guild, an
// infiltrator, or in an omniscient game); the news is a message that
// names them (and, for a pact, their enemy). Steps:
// - No treaty yet, and in a vendetta or a truce: the truce, at once, if
//   either wants revenge or has been betrayed (0x40 or 0x20), or both rank 6
//   or above, or a roll of 0 to 15 is below their fighting spirits (the AI's,
//   each, in -1 to 1) less the difficulty (5 with intense rivalry) plus 8. Failing
//   that, the treaty.
// - Otherwise no treaty: the treaty.
// - A treaty: they end it if the leader (the first in the rankings) is
//   one of them and the other ranks 5 or above, or either wants revenge
//   or was the victim of an atrocity, or has committed a major one.
//   Otherwise a third faction is sought, among those they have commlinks
//   with (both, or the great satan), that is a pact partner of either (it
//   counts against the pact, if it ranks as high as either) or that has
//   some level and is the great satan, or is at peace with neither and
//   either their pact partners already or at war with one of them and
//   outranking one; found, they pact against it unless they already did,
//   or unless the pacts counted are more than the third's blemishes (it
//   being the great satan, or nothing), when they part instead.
//   They team up against it (enemies_team_up(), each way), and the player,
//   if it is the third faction or sees it, is told.
// The two last steps, ending a pact and teaming up, are the game's own
// (enemies_unpact() and enemies_team_up()); Thinker has the same logic
// (its pact_truce() and the rest are inlined here as in the original, and
// its player_spying() is game_spying() of either).
void enemies_treaty(int faction, int other, int trade) {
    struct faction_data *F1 = &game_factions[faction], *F2 = &game_factions[other];
    if (trade) game_enemies_trade_tech(faction, other);
    uint32_t st = F1->diplo_status[other];
    if (!(st & TREATY_TREATY) && (st & (TREATY_VENDETTA | 0x4))) {
        int difficulty = (*game_rules & RULES_INTENSE_RIVALRY) ? 5 : *game_difficulty;
        if ((st & (TREATY_WANT_REVENGE | TREATY_UNK_40)) ||
            (F2->diplo_status[faction] & (TREATY_WANT_REVENGE | TREATY_UNK_40)) ||
            (F2->ranking >= 6 && F1->ranking >= 6) ||
            game_rand() % 16 < F1->ai_fight + F2->ai_fight - difficulty + 8) {
            int player = *game_current_faction;
            if (game_factions[player].diplo_status[faction] & TREATY_PACT) news_pact(faction, other, 0x68e620, 1);
            else if (game_factions[player].diplo_status[other] & TREATY_PACT) news_pact(other, faction, 0x68e620, 1);
            else if (sees_deal(faction, other, 1)) news_other(faction, other, 0x68e668);
            treaty_on(faction, other, 0x4);
            return;
        }
    }
    st = F1->diplo_status[other];
    if (!(st & TREATY_TREATY)) {
        int player = *game_current_faction;
        if (game_factions[player].diplo_status[faction] & TREATY_PACT) news_pact(faction, other, 0x68e62c, 0);
        else if (game_factions[player].diplo_status[other] & TREATY_PACT) news_pact(other, faction, 0x68e62c, 0);
        else if (sees_deal(faction, other, 1)) news_other(faction, other, 0x68e674);
        treaty_on(faction, other, TREATY_TREATY);
        return;
    }
    if ((game_faction_rankings[7] == faction && F2->ranking >= 5) || (game_faction_rankings[7] == other && F1->ranking >= 5) ||
        (st & (TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE)) ||
        (F2->diplo_status[faction] & (TREATY_ATROCITY_VICTIM | TREATY_WANT_REVENGE)) || F1->major_atrocities ||
        F2->major_atrocities) {
        game_enemies_unpact(faction, other);
        return;
    }
    int pacts = 0, third;
    for (third = 1; third < FACTION_COUNT; third++) {
        uint32_t s13 = F1->diplo_status[third], s23 = F2->diplo_status[third];
        if (third == faction || third == other) continue;
        if (!(s13 & TREATY_COMMLINK) && !(s23 & TREATY_COMMLINK)) continue;
        if (!((s13 & TREATY_COMMLINK) && (s23 & TREATY_COMMLINK)) && !great_satan(third, 0)) continue;
        struct faction_data *F3 = &game_factions[third];
        if ((s13 & TREATY_PACT) || (s23 & TREATY_PACT)) {
            if (F3->ranking >= F1->ranking || F3->ranking >= F2->ranking) pacts++;
            continue;
        }
        if (!F3->diff_level) continue;
        if (great_satan(third, 0)) break;
        if ((s13 & (TREATY_PACT | TREATY_TREATY | 0x4)) || (s23 & (TREATY_PACT | TREATY_TREATY | 0x4))) continue;
        if (F1->diplo_status[other] & TREATY_PACT) break;
        if (!(s13 & TREATY_VENDETTA)) continue;
        if (F1->ranking < F3->ranking || F2->ranking < F3->ranking) break;
    }
    if (third >= FACTION_COUNT) {
        game_enemies_unpact(faction, other);
        return;
    }
    if (F1->diplo_status[other] & TREATY_PACT) return;
    int blemishes = great_satan(third, 0) ? game_factions[third].integrity_blemishes : 0;
    if (pacts > blemishes) {
        game_enemies_unpact(faction, other);
        return;
    }
    treaty_on(faction, other, TREATY_PACT);
    game_enemies_team_up(faction, other, third);
    game_enemies_team_up(other, faction, third);
    // Told if the third is the player, or the player sees them (but here
    // not in an omniscient game).
    int player = *game_current_faction;
    if (third != player && !sees_deal(faction, other, 0)) return;
    say_noun(0, faction), say_noun(1, other), say_noun(2, third);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x68e680));
    if (third != player) strcat(game_str_buffer, text(0x68e690));
    else if (!(F1->diplo_status[third] & TREATY_VENDETTA) && !(F2->diplo_status[third] & TREATY_VENDETTA))
        strcat(game_str_buffer, text(0x68e694));
    game_netmsg_pop(game_netmsg, game_str_buffer, 5000, 0, NULL);
}

// ---- Hooks and shadow checks ------------------------------------------------

// stack_fix(), called by mind_control(), changes the stack only in a
// network game, so both are checked as reading only.
static inline int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}

// The AI talking to another AI: trading techs in a pact, meeting when
// they have reason to (the Empath Guild, a treaty late in the game), or
// noting why it might attack. Each pair is visited from the higher
// faction, every eighth turn or when one wants to talk.
static void ai_meets_ai(int faction, int i) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[i];
    uint32_t *st = &me->diplo_status[i];
    if (faction <= i || *game_sunspot_duration > 0) return;
    if (((*game_turn + i + faction) & 7) && !(*st & TREATY_WANT_TO_TALK)) return;
    if (!(*st & TREATY_UNK_80000000) || (*st & TREATY_WANT_TO_TALK)) {
        *st &= ~TREATY_WANT_TO_TALK;
        them->diplo_status[faction] &= ~TREATY_WANT_TO_TALK;
        if (*st & TREATY_PACT)
            game_enemies_trade_tech(faction, i);
        else if (((*st & TREATY_TREATY) && (*game_state & GAME_STATE_RANKED)) || holds_project(SP_EMPATH_GUILD, faction) ||
                 holds_project(SP_EMPATH_GUILD, i) || (*st & TREATY_UNK_20000000) ||
                 ((*st & TREATY_COMMLINK) && (*st & TREATY_UNK_10000000)))
            game_encounter(faction, i, -1, -1, 0, 0);
        treaty_on(faction, i, TREATY_UNK_80000000);
        *st &= ~TREATY_UNK_4000000;
        them->diplo_status[faction] &= ~TREATY_UNK_4000000;
    } else {
        *st &= ~TREATY_UNK_80000000;
        them->diplo_status[faction] &= ~TREATY_UNK_80000000;
        for (int j = 1; j < FACTION_COUNT; j++)
            if (is_human(j) && ((me->diplo_status[j] & TREATY_VENDETTA) || (them->diplo_status[j] & TREATY_VENDETTA))) {
                *st &= ~TREATY_UNK_4000000;
                them->diplo_status[faction] &= ~TREATY_UNK_4000000;
            }
    }
}

// The AI calling the player (who is human faction `i`): to ask for help
// against a stronger common enemy, or now and then to talk. In a network
// or hotseat game the call is only noted. Then, unless something is
// already to be raised, diplomacy_check() may give it an opening to attack,
// which it takes when it wants revenge, betrays humans anyway, or plays
// against a hard player under intense rivalry.
static void ai_calls_player(int faction, int i) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[i];
    uint32_t *st = &me->diplo_status[i];
    int turn = *game_turn;
    if (i == *game_current_faction && (*st & (TREATY_ATROCITY_VICTIM | TREATY_VENDETTA | TREATY_COMMLINK)) == TREATY_COMMLINK) {
        int j;
        for (j = 1; j < FACTION_COUNT; j++)
            if ((*game_factions_alive >> j & 1) && j != faction && j != i &&
                (me->diplo_status[j] & (TREATY_UNK_8000 | TREATY_VENDETTA | TREATY_COMMLINK)) ==
                    (TREATY_UNK_8000 | TREATY_VENDETTA | TREATY_COMMLINK) &&
                (them->diplo_status[j] & (TREATY_VENDETTA | TREATY_COMMLINK)) == TREATY_COMMLINK &&
                game_factions[j].mil_strength > me->mil_strength)
                break;
        if (j < FACTION_COUNT && !(*st & TREATY_PACT) && reputation(i, faction) < 5 &&
            ((turn - them->diplo_spoke[faction] > 8 && (turn & 0xf) == 2 * faction) || (*st & TREATY_WANT_TO_TALK))) {
            game_diplo_state[faction] |= 0x41;
            game_diplo_state_with[i][faction] |= 0x41;
            if (!*game_net_multiplayer && !*game_pbem) {
                game_communicate(i, faction, 0);
                treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
            }
        } else if (!(*st & TREATY_WANT_REVENGE) &&
                   (turn - them->diplo_spoke[faction] > 32 || ((*st & TREATY_UNK_20000000) && turn == faction + 8) ||
                    ((*st & TREATY_PACT) && (*st & TREATY_WANT_TO_TALK))) &&
                   (!game_random(0, 16) || (*st & TREATY_WANT_TO_TALK) || turn == faction + 8)) {
            if (!*game_net_multiplayer && !*game_pbem) {
                game_communicate(i, faction, 0);
                treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
            } else {
                game_diplo_state[faction] |= 1;
                game_diplo_state_with[i][faction] |= 1;
            }
        }
    }
    if (*game_pbem ? game_diplo_state_with[i][faction] : game_diplo_state[faction]) return;
    game_diplomacy_check(i, faction, 0);
    if (*game_diplo_attack_chance && ((*st & TREATY_WANT_REVENGE) || (me->flags_ext & FACTION_FLAG_EXT_SHAMELESS) ||
                                      (them->diff_level > 4 && (*game_rules & RULES_INTENSE_RIVALRY)))) {
        *st = (*st & ~(TREATY_UNK_20000 | TREATY_SHALL_BETRAY)) | TREATY_UNK_800 | TREATY_UNK_40;
        me->diplo_merc[i] = 50;
    }
}

// The AI's complaint about the player's pressing into its territory
// (`result`, with `count` from territory()): under sunspots, which cut the
// commlinks, it may resolve to betray instead.
static void ai_territory(int faction, int i, int result, int count) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[i];
    uint32_t *st = &me->diplo_status[i];
    int32_t *state = &game_diplo_state[faction], *with = &game_diplo_state_with[i][faction];
    if (*st & TREATY_VENDETTA) return;
    if (*game_sunspot_duration > 0) {
        game_diplomacy_check(i, faction, 0);
        if (*game_diplo_hostility || (*st & TREATY_WANT_REVENGE) ||
            result + *game_diplo_friction + 2 * them->integrity_blemishes >= 2 * (9 - me->ai_fight)) {
            uint32_t s = *st;
            if (s & TREATY_TREATY) *st = s | TREATY_UNK_800;
            else if (!(s & (TREATY_SHALL_BETRAY | TREATY_UNK_800)))
                *st = s | (s & TREATY_WANT_REVENGE ? TREATY_UNK_800 : TREATY_SHALL_BETRAY);
        }
        return;
    }
    int turn = *game_turn, escalate = 0;
    if (*st & TREATY_TREATY) {
        if (result >= 5) *state |= 4, *with |= 4;
        else if (((turn + faction) & 3) == 0) *state |= 0x20, *with |= 0x20;
    } else {
        uint32_t agenda = me->diplo_agenda[i];
        int revenge = (*st & TREATY_WANT_REVENGE) != 0;
        if ((agenda & AGENDA_UNK_800) || ((agenda & AGENDA_UNK_400) && count)) {
            *state |= 0x18, *with |= 0x18;
        } else if (revenge && !count) {
            *state |= 8, *with |= 8;
            escalate = game_random(0, result + 5 * (them->integrity_blemishes + 1)) > 10;
        } else if (((turn + faction) & 3) == 0) {
            *state |= 8, *with |= 8;
            escalate = game_random(0, result + 5 * (them->integrity_blemishes + count) + (revenge ? 5 : 0)) > 10;
        }
        if (escalate) {
            if (i == *game_current_faction) *state |= 0x10;
            *with |= 0x10;
        }
    }
    if (*state && !*game_net_multiplayer && !*game_pbem) {
        game_communicate(i, faction, 0);
        treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
    }
}

// The friction a turn adds between the AI and the player: the pressure on
// its territory, then in a pact their standing in techs and rank and the
// player's dealings with its enemies, and outside one their military
// strength and lead. Its leader's ideology, its fight priority, and every
// fourth turn treaties, ranks, Morgan's envy of riches and the player's
// techs come in.
static int ai_friction(int faction, int i, int result, int vendettas) {
    struct faction_data *me = &game_factions[faction], *them = &game_factions[i];
    const uint32_t *st = &me->diplo_status[i];
    int turn = *game_turn;
    int fr = ((turn & 1) + result + 3) / 5;
    if (*st & TREATY_PACT) {
        if (!vendettas) {
            int ours = me->tech_ranking / 2, theirs = them->tech_ranking / 2;
            if (theirs > me->ai_tech - me->ai_power - me->ai_fight + ours + 5) fr++;
            if (ours > theirs + 5) fr--;
            if (them->ranking > me->ranking + 1 && them->diff_level) fr++;
        } else {
            fr += 1 - vendettas;
        }
        if (!(*st & TREATY_UNK_1000000))
            for (int j = 1; j < FACTION_COUNT; j++)
                if ((me->diplo_status[j] & TREATY_VENDETTA) && (them->diplo_status[j] & TREATY_COMMLINK) &&
                    !(them->diplo_status[j] & TREATY_VENDETTA))
                    fr++;
        if ((*game_rules & RULES_VICTORY_COOPERATIVE) && me->ranking < 6 && turn >= 200 && fr >= 0) fr = -1;
    } else {
        if ((*game_rules & RULES_INTENSE_RIVALRY) && (turn & 1)) fr++;
        int theirs = them->mil_strength, ours = me->mil_strength;
        fr += 2 * ours / clamp(3 * theirs, 1, 99999);
        fr += -2 * theirs / clamp(3 * ours, 1, 99999);
        if (great_satan(i, 0)) fr++;
        if (aah_ooga(i, -1)) fr++;
    }
    const struct faction_def *def = &game_faction_defs[faction];
    if ((turn & 1) && def->ideology_category >= 0 && def->ideology_model) {
        int model = them->se_models_pending[def->ideology_category];
        if (model == def->ideology_model) fr--;
        else if (model) fr++;
    }
    fr += me->ai_fight;
    if ((turn & 3) == 0) {
        if (*st & TREATY_TREATY) fr--;
        int ours = me->ranking, theirs = them->ranking;
        if (ours >= 6 && theirs >= 6) {
            fr += 3;
        } else if (ours >= 5 && theirs >= 5) {
            if (turn >= 100) fr++;
        } else {
            int d = theirs - ours, sign = (d > 0) - (d < 0);
            fr += 3 * sign - (d + sign) / 2;
        }
        if (them->energy_credits > me->energy_credits && !strcmp(def->filename, "MORGAN")) fr++;
        if (them->tech_ranking > me->tech_ranking) fr += me->ai_power - me->ai_tech + 1;
    }
    if ((*st & TREATY_PACT) && (*game_rules & RULES_VICTORY_COOPERATIVE) && them->ranking > me->ranking && turn >= 200 &&
        fr > 0)
        fr = 0;
    return fr;
}

// The AI's orbital defense pods beyond what the busters of factions
// outside its pacts call for (half those of a treaty partner that doesn't
// want revenge) attack the satellites of a faction it is at war with or
// means to attack, while it has more pods than that faction: the pods
// first, when they can match its own busters; otherwise mineral, energy
// and nutrient satellites, a Geosynchronous Survey Pod, then the pods.
// `base` carries the last base looked at into attacks that don't use it,
// as the original's register does.
static void ai_sat_attacks(int faction, int base) {
    struct faction_data *me = &game_factions[faction];
    int most = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint32_t st = me->diplo_status[i];
        if (i == faction || (st & TREATY_PACT)) continue;
        int busters = game_factions[i].planet_busters;
        if ((st & TREATY_TREATY) && !(st & TREATY_WANT_REVENGE)) busters /= 2;
        if (most <= busters) most = busters;
    }
    int kind = 0;
    while (me->satellites[3] - me->odp_deployed > most) {
        if (kind < 0) return;
        kind = -1;
        for (int i = 1; i < FACTION_COUNT; i++) {
            const struct faction_data *them = &game_factions[i];
            if (!(me->diplo_status[i] & (TREATY_VENDETTA | TREATY_UNK_800))) continue;
            int theirs = them->satellites[3];
            if (me->satellites[3] <= theirs) continue;
            if (theirs && theirs >= me->planet_busters) {
                kind = 3;
            } else {
                int geosync = 0;
                for (base = 0; base < *game_base_count; base++)
                    if (game_bases[base].faction == i && game_has_facility(&game_bases[base], FAC_GEOSYNC_SURVEY_POD)) geosync++;
                if (them->satellites[1]) kind = 1;
                else if (them->satellites[2]) kind = 2;
                else if (them->satellites[0]) kind = 0;
                else if (geosync) {
                    kind = 4;
                    for (base = 0; base < *game_base_count; base++)
                        if (game_bases[base].faction == i && game_has_facility(&game_bases[base], FAC_GEOSYNC_SURVEY_POD)) break;
                } else if (theirs) kind = 3;
                else continue;
            }
            game_action_sat_attack(faction, i, kind, base);
            break;
        }
    }
}

// An AI faction's diplomacy for the turn. For every faction first (humans
// too) the agenda's two-turn flag moves on. Then, towards each other
// faction in the game: its betrayal mood counts down, its patience on
// odd turns; a betrayal it was set on happens (by chance, more likely
// against the treacherous), ending a pact with friction or turning it
// against a faction without a target; aggression is forgotten every 16
// turns. Other AIs it meets with (see ai_meets_ai); the player it may
// call (ai_calls_player). Towards humans it has a commlink with it then
// weighs their pressure on its territory, at least every fourth turn
// since they spoke, and the friction between them grows or eases; and
// spare orbital defense pods attack satellites.
void enemy_diplomacy(int faction) {
    if (!faction) return;
    struct faction_data *me = &game_factions[faction];
    for (int i = 1; i < FACTION_COUNT; i++) {
        me->diplo_status[i] &= ~TREATY_UNK_100;
        uint32_t agenda = me->diplo_agenda[i];
        if (!(agenda & AGENDA_UNK_20)) continue;
        if (agenda & AGENDA_UNK_40) {
            me->diplo_agenda[i] = agenda & ~AGENDA_UNK_20;
            game_factions[i].diplo_agenda[faction] &= ~AGENDA_UNK_20;
        } else {
            me->diplo_agenda[i] = agenda | AGENDA_UNK_40;
            game_factions[i].diplo_agenda[faction] |= AGENDA_UNK_40;
        }
    }
    if (is_human(faction)) return;
    int vendettas = 0, base = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || !(*game_factions_alive >> i & 1)) continue;
        struct faction_data *them = &game_factions[i];
        uint32_t *st = &me->diplo_status[i];
        int turn = *game_turn;
        if (*st & TREATY_VENDETTA) vendettas++;
        if (me->diplo_merc[i] && !--me->diplo_merc[i]) *st &= ~TREATY_UNK_40;
        if ((turn & 1) && me->diplo_patience[i] > 0) me->diplo_patience[i]--;
        if (*st & TREATY_SHALL_BETRAY) {
            int chance = clamp(8 - (int)((me->diplo_agenda[i] >> 9) & 2) - them->integrity_blemishes, 1, 8);
            if (chance - 1 <= 0 || game_rand() % chance == 0 || (*st & TREATY_PACT)) {
                if (*st & TREATY_PACT) {
                    cause_friction(faction, i, 20);
                    *st &= ~(TREATY_UNK_20000 | TREATY_UNK_800 | TREATY_SHALL_BETRAY);
                    treaty_on(faction, i, TREATY_WANT_TO_TALK);
                } else if (them->target_x >= 0) {
                    *st |= TREATY_UNK_20000;
                } else {
                    if (them->diplo_spoke[faction] < turn - 8) them->diplo_spoke[faction] = turn - 8;
                    *st = (*st & ~(TREATY_UNK_20000 | TREATY_SHALL_BETRAY)) | TREATY_UNK_800 | TREATY_UNK_40;
                    me->diplo_merc[i] = 50;
                }
            }
        }
        if ((turn & 0xf) == 2 * faction + 1) {
            *st &= ~TREATY_UNK_8000;
            them->diplo_status[faction] &= ~TREATY_UNK_8000;
        }
        if (!is_human(i) || *game_sunspot_duration > 0) ai_meets_ai(faction, i);
        else ai_calls_player(faction, i);
    }
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint32_t *st = &me->diplo_status[i];
        if (!is_human(i) || !(*st & TREATY_COMMLINK)) continue;
        int result = 0, count = 0;
        if (*game_turn - game_factions[i].diplo_spoke[faction] >= 4 || *game_sunspot_duration > 0 ||
            (*st & TREATY_WANT_REVENGE)) {
            result = game_territory(faction, i, 1, &base, &count);
            if (result && i == *game_current_faction) ai_territory(faction, i, result, count);
        }
        int fr = ai_friction(faction, i, result, vendettas);
        *st &= ~TREATY_UNK_200;
        game_factions[i].diplo_status[faction] &= ~TREATY_UNK_200;
        if (!(*st & TREATY_VENDETTA) || fr >= 0) cause_friction(faction, i, fr);
    }
    ai_sat_attacks(faction, base);
}

extern struct hook_module faction_module;
#define FACTION (&faction_module)
SHADOW3(FACTION, int, mind_control, int, int, int)
SHADOW1(FACTION, int, corner_market, int)
SHADOW2(FACTION, int, great_beelzebub, int, int)
SHADOW2(FACTION, int, great_satan, int, int)
SHADOW2(FACTION, int, aah_ooga, int, int)
SHADOW1(FACTION, int, at_climax, int)
SHADOW1(FACTION, int, get_mood, int)
SHADOW2(FACTION, int, reputation, int, int)
SHADOW2(FACTION, int, get_patience, int, int)
SHADOW1(FACTION, int, energy_value, int)
// stack_fix() changes the stack only in a network game.
SHADOW5(FACTION, int, value_of_base, int, int, int, int, int)

// The treaty setters write the factions; treaty_on() also the war music's
// flag (the music itself is left to play), intervention() the parser's
// grammar (its texts and messages are held), cause_friction() the running
// friction and tech_analysis() its findings.
#define WRITES_FACTIONS WRITES(game_factions, sizeof(struct faction_data) * FACTION_COUNT)
#define WRITES_TREATY WRITES_FACTIONS; WRITES(game_ambience_flags, sizeof(int32_t))
#define WRITES_INTERVENTION                                                        \
    WRITES_TREATY;                                                                 \
    WRITES(game_parse_gender, sizeof(int32_t)); WRITES(game_parse_plural, sizeof(int32_t))
#define WRITES_TRADE                                                               \
    WRITES(game_trade_want_value[3], (size_t)((char *)game_trade_want_tech[0] - (char *)game_trade_want_value[3]) + 4)
SHADOW_WRITER(FACTION, void, treaty_on, (int a, int b, uint32_t c), (a, b, c), WRITES_TREATY, "%d, %d, %#x", a, b, c)
SHADOW_WRITER(FACTION, void, treaty_off, (int a, int b, uint32_t c), (a, b, c), WRITES_FACTIONS, "%d, %d, %#x", a, b, c)
SHADOW_WRITER(FACTION, void, agenda_on, (int a, int b, uint32_t c), (a, b, c), WRITES_FACTIONS, "%d, %d, %#x", a, b, c)
SHADOW_WRITER(FACTION, void, agenda_off, (int a, int b, uint32_t c), (a, b, c), WRITES_FACTIONS, "%d, %d, %#x", a, b, c)
SHADOW_WRITER(FACTION, void, set_treaty, (int a, int b, uint32_t c, int d), (a, b, c, d), WRITES_FACTIONS,
              "%d, %d, %#x, %d", a, b, c, d)
SHADOW_WRITER(FACTION, void, cause_friction, (int a, int b, int c), (a, b, c),
              WRITES_FACTIONS; WRITES(game_diplo_friction, sizeof(int32_t)), "%d, %d, %d", a, b, c)
SHADOW_WRITER(FACTION, void, intervention, (int a, int b), (a, b), WRITES_INTERVENTION, "%d, %d", a, b)
SHADOW_WRITER(FACTION, void, tech_analysis, (int a, int b), (a, b), WRITES_TRADE, "%d, %d", a, b)

SHADOW3(FACTION, int, wants_to_attack, int, int, int)
// The AI's diplomacy writes the factions and the game's diplomacy state.
// While any faction is human the original runs unchecked: the AI may call
// the player, whose answers a check would leave unanswered.
SHADOW_WRITER_CHECK(void, enemy_diplomacy, (int a), (a), orig_enemy_diplomacy(a), hooks_writes_game(&w_), "%d", a)
static void shadow_enemy_diplomacy(int a) {
    if (hooks_nested) {
        orig_enemy_diplomacy(a);
        return;
    }
    if (shadow_bypass()) {
        enemy_diplomacy(a);
        return;
    }
    stats_enemy_diplomacy.calls++;
    shadow_tick(FACTION);
    if (*game_human_factions) orig_enemy_diplomacy(a);
    else check_enemy_diplomacy(a);
}

// The AI's diplomacy with faction `human` made the player for the check, in
// a hotseat game so that no call is put to them (the AI only notes what it
// would raise). A sweep's check puts back all else.
static void check_diplomacy_with_player(int faction, int human) {
    uint8_t humans = *game_human_factions;
    int32_t current = *game_current_faction, pbem = *game_pbem;
    *game_human_factions = (uint8_t)(1 << human), *game_current_faction = human, *game_pbem = 1;
    check_enemy_diplomacy(faction);
    *game_human_factions = humans, *game_current_faction = current, *game_pbem = pbem;
}

// The talks write widely (the factions, the talks' variables, units moved
// home, the game's messages), so most are checked against all of the
// game's state. Those that would put a question or a window to a human
// player run the original unchecked when one is involved (`player`):
// the sweep checks them on a copy, where no question is put.
#define SHADOW_TALK(ret, name, params, args, player, fmt, ...)                                                  \
    SHADOW_WRITER_CHECK(ret, name, params, args, orig_##name args, hooks_writes_game(&w_), fmt, __VA_ARGS__)     \
    static ret shadow_##name params {                                                                         \
        if (hooks_nested) SHADOW_TAIL_##ret(orig_##name args);                                                \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);                                                    \
        stats_##name.calls++;                                                                                 \
        shadow_tick(FACTION);                                                                                 \
        if (player) SHADOW_TAIL_##ret(orig_##name args);                                                      \
        SHADOW_TAIL_##ret(check_##name args);                                                                 \
    }
// diplomacy_check() asks no one, but energy_compute() writes widely.
SHADOW_TALK(void, diplomacy_check, (int a, int b, int c), (a, b, c), 0, "%d, %d, %d", a, b, c)
SHADOW_WRITER(FACTION, void, scan_prototypes, (int a, int b), (a, b),
              WRITES(game_diplo_best_proto, 4); WRITES(game_diplo_mention_proto, 4), "%d, %d", a, b)
// Only ever in the player's talks: the swap of designs is the player's to
// accept, and the call opens the diplomacy window.
SHADOW_TALK(void, mention_prototypes, (int a, int b), (a, b), 1, "%d, %d", a, b)
SHADOW_TALK(int, wants_to_speak, (int a, int b), (a, b), 1, "%d, %d", a, b)
// Units withdrawn home; the console's unit is the player's.
SHADOW_TALK(int, pact_withdraw, (int a, int b), (a, b), is_human(a), "%d, %d", a, b)
// The player is told of a pact's end in a popup, which a check would drop.
SHADOW_TALK(void, pact_ends, (int a, int b), (a, b), is_human(a) || is_human(b), "%d, %d", a, b)
// The player's answer.
SHADOW_TALK(int, break_treaty, (int a, int b, uint32_t c), (a, b, c), 1, "%d, %d, %#x", a, b, c)
SHADOW_TALK(void, double_cross, (int a, int b, int c), (a, b, c), is_human(a) || is_human(b), "%d, %d, %d", a, b, c)
// A human may be called (comm_check).
SHADOW_TALK(void, encounter, (int a, int b, int c, int d, int e, int f), (a, b, c, d, e, f), is_human(a) || is_human(b),
            "%d, %d, %d, %d, %d, %d", a, b, c, d, e, f)
// A human culprit hears an AI declare war in a window; a major atrocity
// ends its pacts.
SHADOW_TALK(void, atrocity, (int a, int b, int c, int d), (a, b, c, d), is_human(a), "%d, %d, %d, %d", a, b, c, d)
SHADOW_TALK(void, major_atrocity, (int a, int b), (a, b), is_human(a), "%d, %d", a, b)
// Between AIs, with a message for the player; a human involved may be put
// a question by the techs traded.
SHADOW_TALK(void, enemies_treaty, (int a, int b, int c), (a, b, c), is_human(a) || is_human(b), "%d, %d, %d", a, b, c)

// territory() also answers in its caller's variables, which are compared
// apart from the game's state.
int territory(int faction, int other, int flags, int *base, int *count);
SHADOW_DECLARE(int, territory, (int a, int b, int c, int *d, int *e))
static int check_territory(int a, int b, int c, int *base, int *count) {
    struct hook_writes w_ = {0};
    WRITES_FACTIONS;
    WRITES(game_vehs, sizeof(struct veh) * *game_veh_count);
    hooks_writes_begin(&w_);
    hooks_nested++;
    int want_base = -2, want_count = -2, got_base = -2, got_count = -2;
    int want = orig_territory(a, b, c, base ? &want_base : NULL, count ? &want_count : NULL);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int got = territory(a, b, c, base ? &got_base : NULL, count ? &got_count : NULL);
    const char *diff = hooks_writes_end(&w_, got, want);
    if (diff)
        hooks_mismatch(&stats_territory, "(%d, %d, %d) %s", a, b, c, diff);
    else if (got_base != want_base || got_count != want_count)
        hooks_mismatch(&stats_territory, "(%d, %d, %d) base %d count %d, original %d %d", a, b, c, got_base, got_count,
                       want_base, want_count);
    hooks_ui_release();
    if (base) *base = want_base;
    if (count) *count = want_count;
    return want;
}
static int shadow_territory(int a, int b, int c, int *d, int *e) {
    if (hooks_nested) return orig_territory(a, b, c, d, e);
    if (shadow_bypass()) return territory(a, b, c, d, e);
    stats_territory.calls++;
    shadow_tick(FACTION);
    return check_territory(a, b, c, d, e);
}

// ---- The diplomacy sweep ------------------------------------------------------------

// The sweep's own random numbers, so that the game's are left alone.
static uint32_t sweep_seed;
static int rnd(int n) {
    sweep_seed = sweep_seed * 1103515245u + 12345u;
    return (int)((sweep_seed >> 16) % (uint32_t)n);
}

static uint32_t random_treaty(void) {
    static const uint32_t bits[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_COMMLINK, TREATY_VENDETTA,
                                    TREATY_WANT_REVENGE, TREATY_UNK_40, 0x80, TREATY_UNK_100, TREATY_SHALL_BETRAY,
                                    TREATY_UNK_8000, 0x10000, TREATY_UNK_100000, TREATY_UNK_800000,
                                    TREATY_UNK_1000000, TREATY_SURRENDERED, TREATY_UNK_4000000, TREATY_UNK_80000000};
    uint32_t s = 0;
    for (unsigned k = 0; k < sizeof(bits) / sizeof(bits[0]); k++)
        if (!rnd(5)) s |= bits[k];
    return s;
}

// Every function over every faction (or pair) and a range of inputs; the
// heavier ones over a sample. `full` checks value_of_base for every base.
static void sweep_reckoning(int full) {
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int a = 0; a < 2; a++) {
            check_great_beelzebub(f, a);
            check_great_satan(f, a);
        }
        check_at_climax(f);
        for (int p = -1; p < FACTION_COUNT; p++) check_aah_ooga(f, p);
        for (int o = 0; o < FACTION_COUNT; o++) {
            check_reputation(f, o);
            check_get_patience(o, f);
        }
    }
    for (int x = -2; x <= 22; x++) check_get_mood(x);
    static const int loans[] = {-7, 0, 4, 5, 49, 50, 51, 99, 150, 151, 249, 400, 1234, 5000};
    for (unsigned k = 0; k < sizeof(loans) / sizeof(loans[0]); k++) check_energy_value(loans[k] + rnd(5));
    for (int f = 1; f < FACTION_COUNT; f++)
        for (int o = 1; o < FACTION_COUNT; o++) {
            if (o == f) continue;
            check_wants_to_attack(f, o, 0);
            int ally = rnd(FACTION_COUNT);
            if (ally != f && ally != o) check_wants_to_attack(f, o, ally);
        }
    for (int n = 0; n < (full ? *game_base_count : 24); n++) {
        int b = full ? n : rnd(*game_base_count), owner = game_bases[b].faction;
        int f = 1 + rnd(FACTION_COUNT - 1);
        check_value_of_base(b, f, owner, rnd(2), rnd(2));
        check_value_of_base(b, owner, f, rnd(2), rnd(2));
    }
    // tech_analysis, sometimes as a network game's players see it.
    for (int n = 0; n < 4; n++) {
        int f = 1 + rnd(FACTION_COUNT - 1), o = 1 + rnd(FACTION_COUNT - 1);
        int32_t net = *game_net_multiplayer;
        if (!rnd(4)) *game_net_multiplayer = 1;
        check_tech_analysis(f, o);
        *game_net_multiplayer = net;
    }
}

// The setters between some pairs with random bits. A sweep's checks leave
// the game as it was.
static void sweep_treaties(void) {
    for (int n = 0; n < 12; n++) {
        int f = 1 + rnd(FACTION_COUNT - 1), o = 1 + rnd(FACTION_COUNT - 1);
        check_treaty_on(f, o, random_treaty());
        check_treaty_off(f, o, random_treaty());
        uint32_t agenda = rnd(2) ? AGENDA_UNK_20 | AGENDA_UNK_40 : (uint32_t)rnd(0x1000);
        check_agenda_on(f, o, agenda);
        check_agenda_off(f, o, (uint32_t)rnd(0x1000));
        check_set_treaty(f, o, random_treaty(), rnd(2));
        check_cause_friction(f, o, rnd(41) - 20);
        check_intervention(f, o);
    }
    // The running friction for a pair.
    *game_diplo_friction_faction = 1 + rnd(FACTION_COUNT - 1), *game_diplo_friction_with = 1 + rnd(FACTION_COUNT - 1);
    check_cause_friction(*game_diplo_friction_faction, *game_diplo_friction_with, rnd(30) - 10);
}

// A different game for diplomacy: treaties, agendas and grudges; each
// faction's priorities, level, strength, commerce, arms, reputation,
// gifts, atrocities, morale, reserves and market, and its forces in some
// regions; who is human, the player, the rankings and their scores, the
// rules, the turn and difficulty, Ascents and secret projects being built,
// and which projects stand.
static void perturb(void) {
    static const uint32_t bits[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_COMMLINK, TREATY_VENDETTA,
                                    TREATY_WANT_REVENGE, TREATY_UNK_40, TREATY_SHALL_BETRAY, TREATY_UNK_800,
                                    TREATY_ATROCITY_VICTIM, TREATY_SURRENDERED, TREATY_UNK_4000000,
                                    TREATY_UNK_20000000};
    for (int i = 1; i < FACTION_COUNT; i++)
        for (int j = 1; j < FACTION_COUNT; j++) {
            if (i == j) continue;
            struct faction_data *F = &game_factions[i];
            for (unsigned k = 0; k < sizeof(bits) / sizeof(bits[0]); k++)
                if (!rnd(5)) F->diplo_status[j] ^= bits[k];
            if (!rnd(4)) F->diplo_agenda[j] ^= AGENDA_UNK_200;
            if (!rnd(4)) F->diplo_friction[j] = rnd(21);
            if (!rnd(3)) F->diplo_gifts[j] = rnd(5);
        }
    int turn = *game_turn;
    for (int f = 1; f < FACTION_COUNT; f++) {
        struct faction_data *F = &game_factions[f];
        if (rnd(2)) F->ai_fight = rnd(3) - 1;
        if (rnd(2)) F->diff_level = rnd(6);
        if (!rnd(3)) F->base_count = rnd(14);
        if (rnd(2)) F->mil_strength = rnd(300);
        if (rnd(2)) F->tech_commerce_bonus = rnd(6);
        if (rnd(2)) F->best_weapon_value = rnd(12), F->best_armor_value = rnd(8);
        if (rnd(2)) F->integrity_blemishes = rnd(8);
        if (!rnd(4)) F->major_atrocities = rnd(2);
        if (!rnd(6)) F->flags ^= FACTION_FLAG_TEAM_UP_VS_HUMAN;
        if (rnd(2)) F->se_pending[SE_MORALE] = rnd(11) - 5;
        if (!rnd(4)) F->corner_market_turn = turn + rnd(10) - 3;
        if (rnd(2)) F->energy_credits = rnd(1500);
        for (int n = rnd(4); n > 0; n--) {
            int r = 1 + rnd(LAND_REGION_COUNT - 1);
            F->region_force_rating[r] = (uint16_t)(rnd(3) ? rnd(60) : 0);
            F->region_total_combat_units[r] = (uint16_t)(rnd(3) ? rnd(30) : 0);
            F->region_total_bases[r] = (uint8_t)(rnd(3) ? rnd(9) : 0);
        }
        if (!rnd(10)) game_faction_defs[f].rule_flags ^= RULE_FLAG_ALIEN;
        if (!rnd(4)) game_faction_defs[f].rule_morale = rnd(5) - 2;
        if (!rnd(3)) game_ranking_scores[f] = rnd(400);
    }
    if (rnd(2)) *game_human_factions = (uint8_t)(rnd(256) & rnd(256) & 0xfe);
    *game_current_faction = 1 + rnd(FACTION_COUNT - 1);
    for (int k = 7; k > 0 && rnd(3); k--) {  // the rankings shuffled
        int j = rnd(k + 1), x = game_faction_rankings[k];
        game_faction_rankings[k] = game_faction_rankings[j], game_faction_rankings[j] = x;
    }
    if (rnd(2)) game_ranked_faction[0] = 1 + rnd(7);
    if (rnd(2)) game_ranked_faction[1] = 1 + rnd(7);
    static const uint32_t rules[] = {RULES_INTENSE_RIVALRY, RULES_VICTORY_COOPERATIVE, RULES_VICTORY_CONQUEST,
                                     RULES_VICTORY_DIPLOMATIC, RULES_NO_TECH_TRADING};
    for (unsigned k = 0; k < sizeof(rules) / sizeof(rules[0]); k++)
        if (!rnd(3)) *game_rules ^= rules[k];
    if (!rnd(3)) *game_objectives_required = rnd(2) ? 1000 : 1001;
    if (rnd(2)) *game_turn = rnd(3) ? 80 + rnd(250) : rnd(120);
    if (!rnd(3)) *game_difficulty = rnd(6);
    if (!rnd(6)) *game_state ^= GAME_STATE_DONE;
    if (!rnd(3)) *game_diplo_friction = rnd(30) - 5;
    if (!rnd(4)) *game_factions_alive &= (uint8_t)~(1 << (1 + rnd(7)));
    int n = *game_base_count;
    // The end in sight: a human player has cornered the market (in a pact
    // with some, in a cooperative game), and another faction about now.
    if (!rnd(3)) {
        int h = 1 + rnd(FACTION_COUNT - 1), g = 1 + rnd(FACTION_COUNT - 1);
        *game_human_factions |= (uint8_t)(1 << h);
        game_factions[h].corner_market_turn = *game_turn + 1 + rnd(5);
        game_factions[g].corner_market_turn = *game_turn + rnd(2);
        if (rnd(2)) *game_rules |= RULES_VICTORY_COOPERATIVE;
        for (int f = 1; f < FACTION_COUNT; f++)
            if (rnd(2)) game_factions[f].diplo_status[h] |= TREATY_PACT;
    }
    // Two factions with half the Council's votes each, under a proposal
    // anyone may make.
    if (!rnd(3)) {
        int a = 1 + rnd(FACTION_COUNT - 1), b = 1 + rnd(FACTION_COUNT - 1);
        for (int k = 0; k < n; k++) {
            game_bases[k].faction = (uint8_t)(k & 1 ? a : b);
            game_bases[k].pop_size = (int8_t)(k + 1 < n || !(n & 1) ? 2 : 0);
        }
        *game_rules |= RULES_VICTORY_DIPLOMATIC;
        if (rnd(2)) game_proposals[PROP_SUPREME_LEADER].preq_tech = TECH_NONE;
    }
    for (int k = 0; k < 6; k++) {
        struct base *B = &game_bases[rnd(n)];
        int sp = rnd(SP_COUNT);
        switch (rnd(4)) {
        case 0: B->queue[0] = -FAC_ASCENT, B->minerals = rnd(400); break;
        case 1: B->queue[0] = -(FAC_FIRST_PROJECT + sp), game_secret_projects[sp] = -1; break;
        case 2: game_secret_projects[sp] = rnd(3) ? -1 : rnd(n); break;
        default: B->faction = (uint8_t)(1 + rnd(FACTION_COUNT - 1));
        }
    }
}

// More of the talks' inputs varied: treaty and agenda bits, rankings
// (1 to 7), reserves, labs, arms, records, planet busters, atrocities,
// prototypes mentioned or offered, the AI's flags and moods, sunspots, the
// UN Charter and the expansion.
static void perturb_talks(void) {
    static const uint32_t bits[] = {TREATY_UNK_200, TREATY_WANT_TO_TALK, TREATY_UNK_10000, TREATY_UNK_200000,
                                    TREATY_MAJOR_ATROCITY_VICTIM, TREATY_UNK_800000, TREATY_UNK_8000000,
                                    TREATY_UNK_10000000, 0x4000};
    static const uint32_t agendas[] = {AGENDA_UNK_1, AGENDA_UNK_4, AGENDA_FIGHT_TO_DEATH, AGENDA_UNK_20, AGENDA_UNK_200,
                                       AGENDA_UNK_400, AGENDA_UNK_800, AGENDA_UNK_2000};
    for (int i = 1; i < FACTION_COUNT; i++) {
        struct faction_data *F = &game_factions[i];
        for (int j = 1; j < FACTION_COUNT; j++) {
            if (i == j) continue;
            for (unsigned k = 0; k < sizeof(bits) / sizeof(bits[0]); k++)
                if (!rnd(6)) F->diplo_status[j] ^= bits[k];
            for (unsigned k = 0; k < sizeof(agendas) / sizeof(agendas[0]); k++)
                if (!rnd(7)) F->diplo_agenda[j] ^= agendas[k];
            if (!rnd(3)) F->diplo_combat_count[j] = rnd(20);
            if (!rnd(3)) F->diplo_combat_total[j] = rnd(40);
            if (!rnd(4)) F->diplo_spoke[j] = *game_turn - rnd(40);
            if (!rnd(5)) game_diplo_state[j] ^= 0x40;
        }
        if (rnd(2)) F->energy_credits = rnd(3) ? rnd(800) : 1500 + rnd(4000);
        if (rnd(2)) F->energy_surplus_total = rnd(200) - 20;
        if (!rnd(3)) F->alloc_labs = rnd(2) * rnd(6);
        if (!rnd(3)) F->planet_busters = rnd(3);
        if (!rnd(3)) F->atrocities = rnd(4) ? rnd(6) : rnd(40);
        if (!rnd(3)) F->ai_wealth = rnd(3) - 1;
        if (!rnd(6)) F->flags ^= FACTION_FLAG_COOP_WITH_HUMAN;
        if (!rnd(6)) F->flags_ext ^= FACTION_FLAG_EXT_SHAMELESS;
        if (!rnd(3)) F->diplo_reputation = rnd(4);
        if (!rnd(3)) F->se_models_pending[rnd(4)] = rnd(4);
        if (!rnd(4)) game_faction_defs[i].ideology_category = rnd(5) - 1, game_faction_defs[i].ideology_model = rnd(4);
        for (int k = 0; k < 64; k++)
            if (!rnd(8)) game_protos[i * 64 + k].flags ^= (uint16_t)(rnd(2) ? PROTO_FLAG_MENTIONED : PROTO_FLAG_OFFERED);
    }
    // The rankings, 1 to 7, among the factions.
    if (rnd(2))
        for (int k = 7; k > 1; k--) {
            int j = 1 + rnd(k), x = game_factions[k].ranking;
            game_factions[k].ranking = game_factions[j].ranking, game_factions[j].ranking = x;
        }
    if (!rnd(4)) *game_sunspot_duration = rnd(2) ? 0 : 1 + rnd(5);
    if (!rnd(6)) *game_un_charter_a = *game_un_charter_b + 1;
    if (!rnd(3)) *game_expansion = !*game_expansion;
    if (!rnd(3)) *game_rules ^= RULES_VICTORY_TRANSCENDENCE;
    if (!rnd(5)) *game_state ^= GAME_STATE_OMNISCIENT;
    if (!rnd(3)) *game_diplo_friction = rnd(25);
    *game_diplo_can_betray = rnd(2);
}

// A faction other than those given that isn't human, made the player (the
// current faction), so that no one human is called; 0 if none.
static int bystander(int a, int b) {
    for (int k = 0; k < FACTION_COUNT - 1; k++) {
        int i = 1 + (k + *game_turn) % (FACTION_COUNT - 1);
        if (i != a && i != b && !is_human(i)) return i;
    }
    return 0;
}

static int some_faction(void) { return 1 + rnd(FACTION_COUNT - 1); }
// Two different factions.
static void some_pair(int *f, int *o) {
    *f = some_faction();
    *o = 1 + (*f - 1 + 1 + rnd(FACTION_COUNT - 2)) % (FACTION_COUNT - 1);
}

// The talks over pairs of factions: in the game as it is (`full`) every
// pair for diplomacy_check() and territory(), else a sample; the rest over
// a sample. Questions, popups and messages are held or dropped; a human
// in the game plays hotseat (game_pbem), with someone else the player.
static void sweep_talks(int full) {
    int32_t current = *game_current_faction, pbem = *game_pbem;
    if (*game_human_factions) {
        *game_pbem = 1;
        int p = bystander(0, 0);
        if (p) *game_current_faction = p;
    }
    for (int n = 0; n < (full ? 42 : 6); n++) {
        int f, o;
        some_pair(&f, &o);
        check_scan_prototypes(f, o);
    }
    for (int n = 0; n < (full ? 42 : 4); n++) {
        int f = full ? 1 + n / 6 : some_faction(), o = full ? 1 + n % 6 : some_faction();
        if (full && o >= f) o++;
        // The original divides by the player's ranking.
        if (f == o || !game_factions[f].ranking || !(*game_factions_alive >> f & 1) || !(*game_factions_alive >> o & 1))
            continue;
        check_diplomacy_check(f, o, (n + *game_turn) & 1);
    }
    for (int n = 0; n < (full ? 42 : 8); n++) {
        int f, o, base = -1, count = -1, r = rnd(4);
        some_pair(&f, &o);
        check_territory(f, o, rnd(4), r & 1 ? &base : NULL, r & 2 ? &count : NULL);
    }
    static const uint32_t breaks[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_PACT | TREATY_TREATY | TREATY_TRUCE,
                                      TREATY_TRUCE | TREATY_UNK_10000, TREATY_UNK_10000, TREATY_VENDETTA};
    for (int n = 0; n < 6; n++) {
        int f, o;
        some_pair(&f, &o);
        check_break_treaty(f, o, breaks[rnd(sizeof(breaks) / sizeof(breaks[0]))]);
    }
    for (int n = 0; n < 2; n++) {
        int f, o, x = rnd(8) - 2, y = rnd(8) - 2;
        some_pair(&f, &o);
        if (!rnd(4)) o = 0;
        if (o && (is_human(f) || is_human(o))) {
            int p = bystander(f, o);
            if (!p) continue;
            *game_current_faction = p;
        } else if (rnd(2)) {
            *game_current_faction = rnd(2) ? f : o;  // the player's scenes and notes
        }
        check_encounter(f, o, x, y, rnd(2), rnd(2));
    }
    // Two AIs making peace, from each step: the pair's standing and the
    // others' ties to them (commlinks, pacts, treaties, vendettas), their
    // fighting spirits, levels, rankings, blemishes and atrocities, and who
    // is the player (not one of them; the news names whoever it is in a
    // pact with). Trading techs first now and then.
    for (int n = 0; n < (full ? 16 : 6); n++) {
        int f, o;
        some_pair(&f, &o);
        if (is_human(f) || is_human(o)) continue;
        struct faction_data *F1 = &game_factions[f], *F2 = &game_factions[o];
        static const uint32_t kinds[] = {TREATY_VENDETTA, 0x4, 0x4 | TREATY_VENDETTA, TREATY_TREATY, TREATY_TREATY,
                                         TREATY_TREATY | TREATY_PACT, 0};
        const uint32_t mask = TREATY_VENDETTA | 0x4 | TREATY_TREATY | TREATY_PACT;
        if (rnd(4)) {
            uint32_t k = kinds[rnd(7)];
            F1->diplo_status[o] = (F1->diplo_status[o] & ~mask) | k | TREATY_COMMLINK;
            F2->diplo_status[f] = (F2->diplo_status[f] & ~mask) | k | TREATY_COMMLINK;
        }
        for (int t = 1; t < FACTION_COUNT; t++) {
            if (t == f || t == o) continue;
            if (rnd(3)) F1->diplo_status[t] |= TREATY_COMMLINK, F2->diplo_status[t] |= TREATY_COMMLINK;
            else if (rnd(2)) F1->diplo_status[t] ^= TREATY_COMMLINK;
            static const uint32_t ties[] = {TREATY_PACT, TREATY_TREATY, 0x4, TREATY_VENDETTA};
            for (int j = 0; j < 4; j++) {
                if (!rnd(5)) F1->diplo_status[t] ^= ties[j];
                if (!rnd(5)) F2->diplo_status[t] ^= ties[j];
            }
            if (!rnd(4)) game_factions[t].diff_level = rnd(6);
            if (!rnd(4)) game_factions[t].integrity_blemishes = rnd(6);
        }
        if (!rnd(3)) F1->ai_fight = rnd(3) - 1, F2->ai_fight = rnd(3) - 1;
        if (!rnd(4)) F1->major_atrocities = rnd(2);
        if (!rnd(4)) F2->major_atrocities = rnd(2);
        int p = bystander(f, o);
        if (p && rnd(4)) *game_current_faction = p;
        check_enemies_treaty(f, o, rnd(4) == 0);
    }
    {
        // A human culprit who is the player would be told in a window.
        int f, o;
        some_pair(&f, &o);
        if (is_human(f) && f == *game_current_faction) *game_current_faction = bystander(f, 0);
        check_atrocity(f, o, rnd(2), rnd(2));
        some_pair(&f, &o);
        check_major_atrocity(f, rnd(4) ? o : -1);
    }
    for (int n = 0; n < 2; n++) {
        int32_t net = *game_net_multiplayer;
        if (!rnd(6)) *game_net_multiplayer = 1;
        int f, o, i = rnd(9) - 1;
        some_pair(&f, &o);
        check_double_cross(f, o, i == f || i == o ? -1 : i);
        *game_net_multiplayer = net;
    }
    for (int n = 0; n < 3; n++) {
        int f, o;
        some_pair(&f, &o);
        if (n) check_pact_withdraw(f, o);
        else check_pact_ends(f, o);
    }
    // The talks' answers already given, or alien factions that can't talk:
    // the rest opens the diplomacy window.
    {
        int f, o;
        some_pair(&f, &o);
        *game_diplo_caption_off = rnd(3) != 0, *game_diplo_caption_mode = rnd(3) ? rnd(2) : 8;
        if (!*game_diplo_caption_off) game_faction_defs[f].rule_flags ^= RULE_FLAG_ALIEN, *game_diplo_caption_off = 1;
        check_wants_to_speak(f, o);
        // mention_prototypes(), unless it would offer to swap designs.
        some_pair(&f, &o);
        *game_diplo_mention_proto = rnd(4) ? o * 64 + rnd(64) : -1;
        *game_diplo_caption_off = 1, *game_diplo_caption_mode = rnd(2);
        if (*game_diplo_caption_mode && !(game_factions[f].diplo_status[o] & TREATY_PACT)) *game_diplo_caption_mode = 0;
        check_mention_prototypes(f, o);
    }
    *game_current_faction = current, *game_pbem = pbem;
}

// The game as it is, then variations of it, on a protected copy that is
// put back after. The music's preference is off meanwhile, so that a
// vendetta declared in a check plays nothing.
static void diplomacy_sweep(void) {
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    *game_preferences &= ~0x800;
    *game_console_veh_mode = 0;
    sweep_seed = (uint32_t)*game_turn * 104729u;
    sweep_reckoning(1);
    sweep_treaties();
    sweep_talks(1);
    for (int v = 0; v < 8; v++) {
        sweep_seed = (uint32_t)*game_turn * 7919u + (uint32_t)v;
        perturb();
        perturb_talks();
        sweep_reckoning(0);
        sweep_treaties();
        sweep_talks(0);
    }
    hooks_writes_abandon(&outer);
}

// Every base against every faction, for a probe and the market; each
// faction's market cost; and, in a game of AIs only, each one's diplomacy,
// on its own and towards another faction made the player, a different one
// each turn.
static void faction_sweep(void) {
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int b = 0; b < *game_base_count; b++) {
            check_mind_control(b, f, 0);
            check_mind_control(b, f, 1);
        }
        check_corner_market(f);
        if (f && !*game_human_factions) {
            check_enemy_diplomacy(f);
            int human = (*game_turn + f) % (FACTION_COUNT - 2) + 1;
            if (human >= f) human++;
            if (*game_factions_alive >> human & 1) check_diplomacy_with_player(f, human);
        }
    }
    diplomacy_sweep();
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook faction_hooks[] = {
    HOOK(0x59ea80, mind_control, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),  // sub esp, 12
    HOOK(0x55f930, enemy_diplomacy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),  // sub esp, 0x34
    HOOK(0x59ee50, corner_market, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xcc, 0x64, 0x9a, 0x00),  // push ecx; mov eax, [0x9a64cc]
    HOOK(0x539b70, great_beelzebub, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),             // mov ecx, [ebp+8]
    HOOK(0x539c00, great_satan, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57, 0x8b, 0x7d, 0x08),  // push ebx, esi, edi; mov edi, [ebp+8]
    HOOK(0x539d40, aah_ooga, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),                    // sub esp, 12
    HOOK(0x539ef0, at_climax, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57, 0x8b, 0x7d, 0x08),
    HOOK(0x53a030, cause_friction, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),              // mov eax, [ebp+8]
    HOOK(0x53a090, get_mood, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x53a100, reputation, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x53a150, get_patience, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),                // mov eax, [ebp+12]
    HOOK(0x53a1c0, energy_value, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),                // mov ecx, [ebp+8]
    HOOK(0x50ba50, intervention, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                // sub esp, 8
    HOOK(0x55bc80, wants_to_attack, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x34, 0x01, 0x00, 0x00),  // sub esp, 0x134
    HOOK(0x54cb50, value_of_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),               // sub esp, 16
    HOOK(0x53e800, tech_analysis, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),
    HOOK(0x55b760, treaty_off, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x10),                  // mov edx, [ebp+16]
    HOOK(0x55b820, agenda_off, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x55b870, treaty_on, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x0d, 0x84, 0x92, 0x93, 0x00),  // mov ecx, [current_faction]
    HOOK(0x55ba80, agenda_on, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),
    HOOK(0x55bb30, set_treaty, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x14),                  // mov eax, [ebp+20]
    HOOK(0x53a980, diplomacy_check, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x68),             // sub esp, 0x68
    HOOK(0x53a4a0, scan_prototypes, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),             // sub esp, 0x28
    HOOK(0x53a230, mention_prototypes, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08, 0xa1, 0x40, 0xfa, 0x93, 0x00),
    HOOK(0x53dce0, wants_to_speak, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
    HOOK(0x53c370, pact_withdraw, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),               // sub esp, 0x1c
    HOOK(0x53c650, pact_ends, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x55f770, break_treaty, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),          // push ebx; mov ebx, [ebp+8]
    HOOK(0x50bcc0, double_cross, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),                // sub esp, 12
    HOOK(0x55e370, encounter, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),                         // push ebx; push esi
    HOOK(0x55eb80, territory, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x18, 0x01, 0x00, 0x00),  // sub esp, 0x118
    HOOK(0x55eee0, atrocity, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),                    // sub esp, 12
    HOOK(0x55f450, major_atrocity, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x58),              // sub esp, 0x58
    HOOK(0x55d760, enemies_treaty, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x14),              // sub esp, 0x14
};
// The diplomacy talks (diplomacy.c) are swept with it: the game calls them
// only when a human player talks.
extern struct hook_module diplomacy_module;
struct hook_module faction_module = {"faction", faction_hooks, HOOK_COUNT(faction_hooks), faction_sweep, &diplomacy_module, -1};
