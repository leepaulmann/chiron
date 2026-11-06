// Factions: probes, the energy market and the AI's diplomacy. Reimplemented from terranx.exe
// 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "shadow.h"

enum {
    TREATY_SURRENDERED = 0x2000000,
    STACK_PLAN = 2, STACK_ABILITY = 6, ABL_POLY_ENCRYPTION = 0x1000, BASE_STATE_RIOTS_ = 0x2,
};

static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }

// How hard the base is to reach from its owner's headquarters: the
// distance to the first base with one, 12 with none. OpenSMACX calls this
// vulnerable(); the original has it inline.
static int vulnerability(const struct base *b) {
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *hq = &game_bases[i];
        if (hq->faction != b->faction || !has_fac(FAC_HEADQUARTERS, i, 0)) continue;
        int dx = abs(b->x - hq->x);
        if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
        return vector_dist_offset(dx, abs(b->y - hq->y));
    }
    return 12;
}

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

// ---- Hooks and shadow checks ------------------------------------------------

// stack_fix(), called by mind_control(), changes the stack only in a
// network game, so both are checked as reading only.
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

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
        game_treaty_on(faction, i, TREATY_UNK_80000000);
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
        if (j < FACTION_COUNT && !(*st & TREATY_PACT) && game_reputation(i, faction) < 5 &&
            ((turn - them->diplo_spoke[faction] > 8 && (turn & 0xf) == 2 * faction) || (*st & TREATY_WANT_TO_TALK))) {
            game_diplo_state[faction] |= 0x41;
            game_diplo_state_with[i][faction] |= 0x41;
            if (!*game_net_multiplayer && !*game_pbem) {
                game_communicate(i, faction, 0);
                game_treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
            }
        } else if (!(*st & TREATY_WANT_REVENGE) &&
                   (turn - them->diplo_spoke[faction] > 32 || ((*st & TREATY_UNK_20000000) && turn == faction + 8) ||
                    ((*st & TREATY_PACT) && (*st & TREATY_WANT_TO_TALK))) &&
                   (!game_random(0, 16) || (*st & TREATY_WANT_TO_TALK) || turn == faction + 8)) {
            if (!*game_net_multiplayer && !*game_pbem) {
                game_communicate(i, faction, 0);
                game_treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
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
        game_treaty_on(i, faction, TREATY_UNK_80000000 | 0x8000000 | TREATY_COMMLINK);
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
        if (game_great_satan(i, 0)) fr++;
        if (game_aah_ooga(i, -1)) fr++;
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
                    game_cause_friction(faction, i, 20);
                    *st &= ~(TREATY_UNK_20000 | TREATY_UNK_800 | TREATY_SHALL_BETRAY);
                    game_treaty_on(faction, i, TREATY_WANT_TO_TALK);
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
        if (!(*st & TREATY_VENDETTA) || fr >= 0) game_cause_friction(faction, i, fr);
    }
    ai_sat_attacks(faction, base);
}

extern struct hook_module faction_module;
#define FACTION (&faction_module)
SHADOW3(FACTION, int, mind_control, int, int, int)
SHADOW1(FACTION, int, corner_market, int)
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
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook faction_hooks[] = {
    HOOK(0x59ea80, mind_control, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),  // sub esp, 12
    HOOK(0x55f930, enemy_diplomacy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),  // sub esp, 0x34
    HOOK(0x59ee50, corner_market, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xcc, 0x64, 0x9a, 0x00),  // push ecx; mov eax, [0x9a64cc]
};
struct hook_module faction_module = {"faction", faction_hooks, HOOK_COUNT(faction_hooks), faction_sweep, NULL, -1};
