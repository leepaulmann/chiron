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

// The game as it is, then variations of it, on a protected copy that is
// put back after. The music's preference is off meanwhile, so that a
// vendetta declared in a check plays nothing.
static void diplomacy_sweep(void) {
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    *game_preferences &= ~0x800;
    sweep_seed = (uint32_t)*game_turn * 104729u;
    sweep_reckoning(1);
    sweep_treaties();
    for (int v = 0; v < 8; v++) {
        sweep_seed = (uint32_t)*game_turn * 7919u + (uint32_t)v;
        perturb();
        sweep_reckoning(0);
        sweep_treaties();
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
};
struct hook_module faction_module = {"faction", faction_hooks, HOOK_COUNT(faction_hooks), faction_sweep, NULL, -1};
