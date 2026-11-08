// Probe teams' missions (probe): infiltrating a faction's datalinks,
// procuring research data, a sabotage virus, draining energy reserves,
// inciting drone riots, assassinating researchers, mind control of a base
// or a unit, the genetic plague, and freeing a captured faction leader; who
// is framed for it, whether the team is caught or comes home, and what the
// victim makes of it. Reimplemented from terranx.exe 2.0's disassembly;
// names follow Thinker (MIT) and OpenSMACX (Brendan Casey,
// GPL-3.0-or-later). Thinker's probe() served as a map; it has options
// and fixes of its own, and where it differs from the original the
// original is followed, as noted below. The helpers it calls
// (success_rates, vulnerable, steal_tech, steal_energy) are in combat.c,
// mind_control in faction.c.
//
// The original is one function of 27 KB that jumps back to its start to
// pick another action: the gotos below follow it.
#include "probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "combat.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "setup.h"
#include "shadow.h"
#include "spot.h"
#include "tech.h"
#include "veh.h"

enum {
    ACT_UNIT = -1,  // mind control of a unit (no base targeted)
    ACT_INFILTRATE = 0, ACT_RESEARCH = 1, ACT_SABOTAGE = 2, ACT_DRAIN = 3, ACT_RIOTS = 4, ACT_ASSASSINATE = 5,
    ACT_MIND_CONTROL = 6, ACT_PLAGUE = 7, ACT_FREE_LEADER = 8,
    SAB_PRODUCTION = 0, SAB_PLANET_BUSTERS = 98, SAB_ABORT = 99,  // sabotage targets besides facilities
    TECH_NOTHING = 9999,  // research data: no tech to take (the map is taken instead)
    ABL_AMPHIBIOUS_ = 0x8, ABL_AIR_SUPERIORITY_ = 0x20, ABL_POLY_ENCRYPTION_ = 0x1000, ABL_ALGO_ENHANCEMENT = 0x10000000,
    SP_HUNTER_SEEKER = 15, SP_NETHACK_TERMINUS = 34,
    FAC_PERIMETER_DEFENSE_ = 4, FAC_TACHYON_FIELD_ = 5, FAC_COMMAND_CENTER_ = 27, FAC_COVERT_OPS_CENTER_ = 34,
    FAC_SABOTAGE_LIMIT = 65,  // facilities a virus can reach: those below the orbital ones
    PLAN_PLANET_BUSTER_ = 5, PLAN_TERRAFORM__ = 9, WPN_PROBE_TEAM = 0x15, PROTO_SPORE_LAUNCHER_ = 15,
    ORDER_HOLD_ = 2,
    BONUS_PROBE_COST = 0x10,           // faction_def.bonus_id: probe costs in percent
    RULE_FLAG_MIND_IMMUNE = 0x800,     // faction_def.rule_flags: immune to mind control
    BASE_STATE_DATA_STOLEN = 0x40, BASE_STATE_PLAGUED = 0x20000, BASE_STATE_DRAINED = 0x400000,
    TREATY_PACT_TREATY_TRUCE = 0x7, TREATY_TREATY_TRUCE = 0x6, TREATY_INFILTRATOR = 0x1000,
    TREATY_SURRENDERED_ = 0x2000000, TREATY_REVENGE_VENDETTA = 0x30,
    FACTION_FLAG_MAP_STOLEN = 0x200,    // procuring research data takes the map when there is no tech
    FACTION_FLAG_PLAGUED = 0x400,       // the genetic plague has been brought on the faction before
    FACTION_FLAG_WANTON = 0x1000000,    // the AI commits atrocities wantonly
    RULES_NO_TECH_TRADING = 0x2000000,  // game_rules (a scenario's)
    STATE_RANKED_ = 0x200, STATE_OMNISCIENT_ = 0x80,  // game_state
    TECH_FLAG_GENE_WARFARE = 0x10, TECH_FLAG_GENE_DEFENSE = 0x20,
    LABEL_PLANET_BUSTERS = 0x404, LABEL_PRODUCTION = 0xf9, LABEL_OF_THE = 0xb4,
};

// Offsets the structs don't name.
enum {
    F_TECH_ACCUMULATED = 0x3c8,  // faction_data: research put into the current tech
    RULES_STEAL_TECH = 0xfc,     // rules_basic: probe teams may procure research data
    POP_SPRITE = 0x2118, POP_ICON = 0x2144, POP_DIALOGS = 0x21d0,  // in the popup object
};

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int sea_xy(int x, int y) { return (tile(x, y)->climate & 0xe0) < 0x60; }
static inline int human(int faction) { return game_is_human(faction); }
static inline int current(void) { return *game_current_faction; }
static inline int attack_flags(int bit) { return *game_veh_attack_flags & bit; }
static inline struct faction_def *def(int faction) { return &game_faction_defs[faction]; }
static inline int alien(int faction) { return (def(faction)->rule_flags & RULE_FLAG_ALIEN) != 0; }
static inline int32_t *tech_accumulated(int faction) {
    return (int32_t *)((char *)&game_factions[faction] + F_TECH_ACCUMULATED);
}
static inline int steal_tech_allowed(void) { return *(int32_t *)((char *)game_rules_basic + RULES_STEAL_TECH); }
// A word of diplo_status by index, which the original reads one past the
// end of the array at times (see free_leader_target).
static inline uint32_t diplo_word(int faction, int i) {
    return *(uint32_t *)((char *)&game_factions[faction] + 0x18 + 4 * i);
}
// The captive leaders' records: event 15 + i of the holder's monuments.
static inline int32_t *captive(int holder, int i, int offset) {
    return (int32_t *)(game_monuments + holder * MONUMENT_SIZE + (MONUMENT_CAPTIVES + i) * MONUMENT_EVENT_SIZE + offset);
}
enum { CAPTIVE_FACTION = 0x8, CAPTIVE_HELD = 0x10 };
static inline int built(int base, int fac) {
    int offset;
    uint32_t mask;
    bitmask(fac, &offset, &mask);
    return (game_bases[base].facilities_built[offset] & mask) != 0;
}

// The game's text helpers: a name into a parse slot, with the grammar
// for it, and the battle text built up.
static void say(int slot, char *s) { game_parse_says(slot, s, -1, -1); }
static char *title_of(int f) {
    *game_parse_plural = 0;
    *game_parse_gender = def(f)->leader_female;
    return def(f)->leader_title;
}
static char *name_of(int f) {
    *game_parse_plural = 0;
    *game_parse_gender = def(f)->leader_female;
    return def(f)->leader_name;
}
static char *noun_of(int f) {
    *game_parse_gender = def(f)->noun_gender;
    *game_parse_plural = def(f)->noun_plural;
    return def(f)->noun;
}
static char *adj_of(int f) { return def(f)->adjective; }
static void clear(void) { game_battle_text[0] = 0; }
static void says(char *s) { strcat(game_battle_text, s); }
static void say_num(int n) {
    char num[16];
    snprintf(num, sizeof num, "%d", n);
    says(num);
}
// A notice in the message bar, with a picture.
static void notice(char *label, char *picture) { game_netmsg_pop(game_netmsg, label, -5000, 0, picture); }
static void notice_at(uint32_t label, uint32_t picture) { notice(text(label), picture ? text(picture) : NULL); }
static int pop_up(char *label, char *picture) { return game_popp(*game_script_file, label, 0, picture, 0); }
static int pop_up_at(uint32_t label, uint32_t picture) { return pop_up(text(label), text(picture)); }
static void popup_start(uint8_t *pop, char *label, int flags) {
    game_popup_start(pop, game_pop_file, label, -1, 0, flags, 0);
}
static void popup_picture(uint8_t *pop, uint32_t file, uint32_t sprite, int *w, int *h) {
    if (!game_pcx_dimensions(text(file), w, h) && !game_sprite_init(pop + POP_SPRITE, text(sprite), *w, *h))
        *(void **)(pop + POP_ICON) = pop + POP_SPRITE;
}
static void popup_item(uint8_t *pop, char *s, int id) { game_dialogs_item(pop + POP_DIALOGS, s, id); }
static int popup_exec(uint8_t *pop) { return game_base_pop_exec_3(pop, 0, 0); }
static int time_bit(void) { return (int)(game_time_get_time() & 1); }

// The faction's first port (on the unit's sea, when at sea), or -1.
static int port_for(int veh, int faction) {
    const struct veh *V = &game_vehs[veh];
    for (int b = 0; b < *game_base_count; b++) {
        if (game_bases[b].faction != faction || !is_port(b, 0)) continue;
        if (!is_ocean(V->x, V->y) || base_on_sea(b, region_at(V->x, V->y))) return b;
    }
    return -1;
}

// Costs scaled by the faction's file (probe costs in percent) and by the
// Nethack Terminus (three quarters).
static int scaled_cost(int faction, int cost) {
    const struct faction_def *d = def(faction);
    for (int i = 0; i < d->bonus_count; i++)
        if (d->bonus_id[i] == BONUS_PROBE_COST) cost = cost * d->bonus_val1[i] / 100;
    if (has_project(SP_NETHACK_TERMINUS, faction)) cost = cost * 3 / 4;
    return cost;
}

int probe_in(uint8_t *pop, int veh, int base, int tveh, int toggle);
int probe_in(uint8_t *pop, int veh, int base, int tveh, int toggle) {
#define RETURN(r)                \
    do {                         \
        game_popup_dtor(pop);    \
        return (r);              \
    } while (0)
    game_popup_ctor(pop);
    char name[256];  // the sabotaged production's name
    int faction = game_vehs[veh].faction;
    int tfac = base >= 0 ? game_bases[base].faction : game_vehs[tveh].faction;
    struct faction_data *F = &game_factions[faction], *TF = &game_factions[tfac];
    int al = alien(faction), w = 0, h = 0;
    int checked = 0, busted_action = 0, leader = -1, home_saved = -1;
    int action = 0, diff = 0, sab = 0, frame = 0, cost = 0, home = -1, act1 = 0;
    *game_probe_target = tfac;

    // A team crossing between land and sea needs to be amphibious (or a
    // ship).
    {
        struct veh *V = &game_vehs[veh];
        int vsea = sea_xy(V->x, V->y);
        int tx = base >= 0 ? game_bases[base].x : game_vehs[tveh].x;
        int ty = base >= 0 ? game_bases[base].y : game_vehs[tveh].y;
        if (vsea && !sea_xy(tx, ty) && game_veh_triad(veh) != TRIAD_SEA && !has_abil(V->proto, ABL_AMPHIBIOUS_)) {
            if (faction == current() && toggle && human(faction) && attack_flags(1)) {
                game_parse_say(0, (int)(intptr_t)game_abilities[abil_index(ABL_AMPHIBIOUS_)].name, -1, -1);
                game_netmsg_pop(game_netmsg, text(0x6902cc), 5000, 0, NULL);
            }
            RETURN(0);
        }
        if (sea_xy(tx, ty) && !vsea && !has_abil(V->proto, ABL_AMPHIBIOUS_)) {
            if (faction == current() && toggle && human(faction) && attack_flags(1)) {
                game_parse_say(0, (int)(intptr_t)game_abilities[abil_index(ABL_AMPHIBIOUS_)].name, -1, -1);
                game_netmsg_pop(game_netmsg, text(0x6902e0), 5000, 0, NULL);
            }
            RETURN(0);
        }
        // The Hunter-Seeker Algorithm stops a team without the algorithmic
        // enhancement (it dies, the move made), and halves an enhanced
        // team's chances; only a move made goes ahead against it.
        *game_probe_target_hsa = 0;
        *game_probe_algo = 0;
        if (has_abil(V->proto, ABL_ALGO_ENHANCEMENT)) {
            *game_probe_algo = 1;
            if (has_project(SP_HUNTER_SEEKER, tfac)) {
                if (!toggle && human(faction)) RETURN(0);
                if (!attack_flags(2) || !toggle) RETURN(1);
                *game_probe_target_hsa = 1;
            }
        } else if (has_project(SP_HUNTER_SEEKER, tfac)) {
            if (!toggle && human(faction)) RETURN(0);
            if (attack_flags(2) && toggle) {
                if (faction == current()) {
                    say(0, adj_of(tfac));
                    notice_at(0x690308, 0x6902f8);
                } else if (tfac == current()) {
                    say(0, adj_of(faction));
                    notice_at(0x690328, 0x690318);
                }
                kill(veh);
            }
            RETURN(1);
        }
    }

    // Genetic warfare: whether the faction knows a tech that allows it,
    // and how many of the techs that defend against it the target knows.
    int gene_allow = 0, gene_techs = 0, gene_known = 0, tech;
    for (tech = 0; tech < TECH_COUNT; tech++) {
        uint32_t flags = game_tech[tech].flags;
        if ((flags & TECH_FLAG_GENE_WARFARE) && has_tech(tech, faction)) gene_allow = 1;
        if (flags & TECH_FLAG_GENE_DEFENSE) {
            gene_techs++;
            if (has_tech(tech, tfac)) gene_known++;
        }
    }

    // The action: the player's from the menu (or from the unit, replayed),
    // the AI's its own choice.
    if (base < 0) {
        action = ACT_UNIT;
    } else if (faction == current() && human(faction) && attack_flags(1)) {
        say(1, adj_of(tfac));
        popup_start(pop, text(0x690338), 0x40);
        popup_picture(pop, al ? 0x690340 : 0x690350, al ? 0x690360 : 0x690370, &w, &h);
        if (game_x_text_open(*game_script_file, text(0x690380))) RETURN(0);
        game_text_get();
        if (!(F->diplo_status[tfac] & TREATY_INFILTRATOR)) popup_item(pop, *game_text_line, ACT_INFILTRATE);
        game_text_get();
        if (steal_tech_allowed()) popup_item(pop, *game_text_line, ACT_RESEARCH);
        game_text_get();
        popup_item(pop, *game_text_line, ACT_SABOTAGE);
        game_text_get();
        popup_item(pop, *game_text_line, ACT_DRAIN);
        game_text_get();
        popup_item(pop, *game_text_line, ACT_RIOTS);
        game_text_get();
        if (built(base, FAC_HEADQUARTERS)) popup_item(pop, *game_text_line, ACT_ASSASSINATE);
        game_text_get();
        if (!built(base, FAC_HEADQUARTERS)) popup_item(pop, *game_text_line, ACT_MIND_CONTROL);
        game_text_get();
        if (gene_allow) popup_item(pop, *game_text_line, ACT_PLAGUE);
        if (*game_expansion && !*game_net_multiplayer) {
            game_text_get();
            if (built(base, FAC_HEADQUARTERS)) {
                // Seven records looked at, where the AI's choice looks at
                // the same seven (see free_leader_target).
                int any = 0;
                for (int i = 0; i < 7; i++)
                    if (*captive(tfac, i, CAPTIVE_HELD)) any = 1;
                if (any) popup_item(pop, *game_text_line, ACT_FREE_LEADER);
            }
        }
        action = popup_exec(pop);
        if (action < 0) RETURN(0);
        game_vehs[veh].unk_2c[0] = (uint8_t)action;
    } else if (human(faction)) {
        action = game_vehs[veh].unk_2c[0] & 7;
    } else {
        goto ai_choice;
    }

start:
    // Infiltration, made: the datalinks open for good, and the victim
    // learns of it.
    if (action == ACT_INFILTRATE && attack_flags(2)) {
        if (!(F->diplo_status[tfac] & TREATY_INFILTRATOR)) {
            set_treaty(faction, tfac, TREATY_INFILTRATOR, 1);
            if (faction == current()) game_report_intel(tfac);
        }
        if (tfac == current()) {
            say(0, title_of(faction));
            say(1, name_of(faction));
            say(2, noun_of(faction));
            notice_at(0x6903b4, al ? 0x690394 : 0x6903a4);
        }
    }
    // The player, at peace with the target (that hasn't surrendered to
    // the player under a pact), is asked whether to risk it.
    if (faction == current() && human(faction) && attack_flags(1) && (F->diplo_status[tfac] & TREATY_PACT_TREATY_TRUCE)) {
        uint32_t d = TF->diplo_status[faction];
        if (!((d & TREATY_PACT) && (d & TREATY_SURRENDERED_)) && action > 0) {
            say(0, title_of(tfac));
            say(1, name_of(tfac));
            if (!pop_up_at(0x6903e8, al ? 0x6903c8 : 0x6903d8)) RETURN(0);
        }
    }
    diff = 0;
    const struct base *B = base >= 0 ? &game_bases[base] : NULL;
    if (action == ACT_RESEARCH) {
        // Data stolen from the base before: harder. The tech is drawn from
        // where the last draw left off (a network game's human draws with
        // the game's own generator, seeded the same on every machine).
        if (B->state & BASE_STATE_DATA_STOLEN) {
            diff = 1;
            if (faction == current() && human(faction) && attack_flags(1)) {
                say(0, (char *)B->name);
                success_rates(1, morale_veh(veh, 1, 0), -1, base);
                if (!success_rates(2, morale_veh(veh, 1, 0), 1, base)) {
                    notice_at(0x6903f4, 0);
                    RETURN(0);
                }
                if (!pop_up_at(0x690424, al ? 0x690404 : 0x690414)) RETURN(0);
            }
        }
        int r;
        if (*game_net_multiplayer && human(faction)) {
            game_random_reseed(*game_map_seed + *game_turn + faction);
            r = game_random(0, TECH_COUNT);
            game_random_reseed((int)game_time_get_time());
        } else {
            r = game_rand() % TECH_COUNT;
        }
        // Each try adds the draw again to the last tech tried (OpenSMACX
        // and Thinker's fix add the count of tries instead).
        int k;
        for (k = 0; k < TECH_COUNT; k++) {
            tech = (r + tech) % TECH_COUNT;
            if (has_tech(tech, tfac) && !has_tech(tech, faction)) break;
        }
        if (k >= TECH_COUNT) tech = TECH_NOTHING;
        if (faction == current() && human(faction)) {
            success_rates(1, morale_veh(veh, 1, 0), diff, base);
            if (success_rates(2, morale_veh(veh, 1, 0), diff + 1, base)) {
                int choice;
                if (!attack_flags(2)) {
                    choice = game_popp(*game_script_file, text(0x69045c), 0x40, text(al ? 0x69043c : 0x69044c), 0);
                    if (choice < 0) RETURN(0);
                    game_vehs[veh].unk_2c[0] |= (uint8_t)(choice << 3);
                } else {
                    choice = game_vehs[veh].unk_2c[0] >> 3 & 1;
                }
                if (choice == 1) {  // a tech of the player's choosing, harder
                    diff++;
                    tech = -1;
                    goto frame_up;
                }
            }
        }
        if (tech != TECH_NOTHING || !(F->flags & FACTION_FLAG_MAP_STOLEN)) goto frame_up;
        if (!human(faction)) {
            action = F->diplo_status[tfac] & TREATY_REVENGE_VENDETTA ? ACT_SABOTAGE : ACT_INFILTRATE;
            goto start;
        }
        if (faction == current() && attack_flags(2)) {
            notice_at(0x690468, 0);
            RETURN(0);
        }
        goto frame_up;
    }
    if (action == ACT_DRAIN) {
        if (B->state & BASE_STATE_DRAINED) {
            diff = 1;
            if (faction == current() && human(faction) && attack_flags(1)) {
                say(0, (char *)B->name);
                success_rates(1, morale_veh(veh, 1, 0), -1, base);
                if (!success_rates(2, morale_veh(veh, 1, 0), 1, base)) {
                    notice_at(0x690478, 0);
                    RETURN(0);
                }
                if (!pop_up_at(0x6904a4, al ? 0x690484 : 0x690494)) RETURN(0);
            }
        }
        goto frame_up;
    }
    if (action == ACT_SABOTAGE) {
        // The target: the production, if it is far enough along (and a
        // draw by the team's morale comes up 0), or a facility drawn at
        // random (planet busters for headquarters, no pressure dome at
        // sea). With nothing to hit, the AI turns to infiltration or riots.
        sab = SAB_PRODUCTION;
        name[0] = 0;
        int q = B->queue[0], item_cost;
        if (q >= 0) {
            strcat(name, game_protos[q].name);
            item_cost = game_protos[q].cost;
        } else {
            strcat(name, game_strings_get(game_strings, game_facilities[-q].name));
            item_cost = game_facilities[-q].cost;
        }
        int r;
        if (*game_net_multiplayer && human(faction)) {
            game_random_reseed(*game_map_seed + *game_turn + faction);
            r = game_random(0, morale_veh(veh, 1, 0) / 2 + 2);
            game_random_reseed((int)game_time_get_time());
        } else {
            r = game_rnd(morale_veh(veh, 1, 0) / 2 + 2);
        }
        if (B->minerals >= cost_factor(faction, 1, -1) * item_cost && !r) goto sabotage_chosen;
        int first = game_rand() % 69;
        for (int k = 1; k < 70; k++) {
            int fac = (k + first) % 69 + 1;
            if (fac == FAC_HEADQUARTERS) {
                if (stack_check(veh_at(B->x, B->y), 2, PLAN_PLANET_BUSTER_, -1, -1)) {
                    sab = SAB_PLANET_BUSTERS;
                    goto sabotage_chosen;
                }
                continue;
            }
            if (fac == FAC_PRESSURE_DOME && sea_xy(B->x, B->y)) continue;
            if (fac >= FAC_SABOTAGE_LIMIT) continue;
            if (built(base, fac)) {
                sab = fac;
                goto sabotage_chosen;
            }
        }
        if (!human(faction)) {
            action = B->pop_size <= 3 ? ACT_INFILTRATE : ACT_RIOTS;
            goto start;
        }
        goto sabotage_chosen;
    }
    if (action == ACT_ASSASSINATE || action == ACT_FREE_LEADER) {
        diff = 1;
        if (success_rates(1, morale_veh(veh, 1, 0), 1, base)) {
            if (faction == current() && human(faction) && attack_flags(1)) {
                if (action == ACT_ASSASSINATE ? !pop_up_at(0x690588, al ? 0x690568 : 0x690578)
                                              : !pop_up_at(0x6905c0, al ? 0x6905a0 : 0x6905b0))
                    RETURN(0);
            }
            goto frame_up;
        }
        // No chance: the player is told (of freeing a leader, even with
        // the menu skipped).
        if (faction == current() && human(faction) && (attack_flags(1) || action == ACT_FREE_LEADER))
            notice_at(action == ACT_ASSASSINATE ? 0x69055c : 0x690594, 0);
        RETURN(0);
    }
    if (action == ACT_UNIT) {
        struct veh *T = &game_vehs[tveh];
        const struct unit_proto *TP = &game_protos[T->proto];
        if (stack_check(tveh, 1, -1, -1, -1) > 1) RETURN(0);
        if (game_veh_triad(tveh) == TRIAD_AIR && !has_abil(game_vehs[veh].proto, ABL_AIR_SUPERIORITY_)) {
            if (faction == current() && toggle && human(faction) && attack_flags(1)) {
                game_parse_say(0, (int)(intptr_t)game_abilities[abil_index(ABL_AIR_SUPERIORITY_)].name, -1, -1);
                game_netmsg_pop(game_netmsg, text(0x6905cc), 5000, 0, NULL);
            }
            RETURN(0);
        }
        // Native life can't be subverted: faction 0's units, and basic
        // psi units and spore launchers (OpenSMACX and Thinker test for
        // native units otherwise).
        if (!tfac || (T->proto < 64 && (game_weapons[TP->weapon].offense < 0 || T->proto == PROTO_SPORE_LAUNCHER_))) {
            if (faction == current() && toggle && human(faction) && attack_flags(1)) notice_at(0x6905e4, 0);
            RETURN(0);
        }
        // The cost: the unit's, times the target's reserves over its
        // distance from headquarters (halved by a punishment sphere at its
        // home); a third less (by difficulty) for the AI against a human.
        int dist = vulnerable(tfac, T->x, T->y);
        if (T->home_base >= 0 && built(T->home_base, FAC_PUNISHMENT_SPHERE)) dist /= 2;
        cost = (TF->energy_credits + 800) / (dist + 2) * TP->cost;
        int fhuman = human(faction);
        if (!fhuman && human(tfac) && TF->diff_level > 3) cost = cost * 3 / TF->diff_level;
        if (def(tfac)->rule_flags & RULE_FLAG_MIND_IMMUNE) {
            if (faction == current() && fhuman && toggle && attack_flags(1)) {
                say(0, adj_of(tfac));
                notice_at(0x6905f0, 0);
            }
            RETURN(0);
        }
        cost = scaled_cost(faction, cost);
        // The target's PROBE: -2 and below halve the cost, -1 takes a
        // quarter off, 1 adds half, 2 doubles it, and 3 to 9 make it
        // immune; out of -6 to 9 it counts as 0 (Thinker lets an enhanced
        // team through at 3 and above).
        int p = TF->se[SE_PROBE];
        if (p >= -6 && p <= 9) {
            if (p <= -2) {
                cost /= 2;
            } else if (p == -1) {
                cost -= cost / 4;
            } else if (p == 1) {
                cost += cost / 2;
            } else if (p == 2) {
                cost *= 2;
            } else if (p >= 3) {
                if (faction == current() && fhuman && toggle && attack_flags(1)) {
                    say(0, adj_of(tfac));
                    notice_at(0x690600, 0);
                }
                RETURN(0);
            }
        }
        if (has_abil(T->proto, ABL_POLY_ENCRYPTION_)) cost *= 2;
        if (TP->plan != PLAN_COLONY && TP->plan != PLAN_TERRAFORM__) cost /= 2;
        say(0, (char *)TP->name);
        // The AI (or the player's team on alert) weighs it: worth more for
        // a better unit than its own, in the target's strength, on rough
        // ground, a defender; not taken outside its own territory from a
        // faction it keeps a truce or treaty with and doesn't hold a grudge
        // against.
        if (!fhuman || (game_vehs[veh].state & 0x200)) {
            int worth = (F->ai_power - F->ai_wealth + 2) * cost * 4;
            if (human(tfac)) worth /= 2;
            int ws = weap_strat(TP->weapon, faction), as = arm_strat(TP->armor, faction);
            int speed = game_chassis[TP->chassis].speed;
            int region = tile(T->x, T->y)->region;
            if (F->region_total_bases[region] &&
                TF->region_force_rating[region] > F->region_total_combat_units[region] + F->region_force_rating[region] / 2)
                worth /= 2;
            if (F->region_force_rating[region] > TF->region_total_combat_units[region]) worth *= 2;
            if (ws > F->best_weapon_value) worth /= 2;
            if (as > F->best_armor_value) worth /= 2;
            if (speed > F->best_land_speed) worth = worth * 2 / 3;
            if (F->diplo_status[tfac] & TREATY_TREATY) worth *= 2;
            if (!(F->diplo_status[tfac] & TREATY_WANT_REVENGE) && !(*game_state & STATE_RANKED_) && !at_climax(faction)) {
                uint32_t d = F->diplo_status[tfac];
                if ((d & TREATY_UNK_1000000) && (d & TREATY_TREATY_TRUCE) &&
                    whose_territory(faction, T->x, T->y, NULL, 0) != faction)
                    RETURN(0);
            }
            if (ws < F->best_weapon_value && as < F->best_armor_value) worth *= 2;
            if (ws > F->best_weapon_value) worth /= 2;
            const struct map_tile *t = tile(T->x, T->y);
            if ((t->rocky_lock & 0xc0) >= 0x80 || ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40) ||
                (t->bits & 0x800))
                worth /= 2;
            if (TP->plan <= PLAN_COMBAT) worth /= 2;
            if (TP->plan >= PLAN_DEFENSIVE) worth *= 3;
            if (F->energy_credits < worth || F->energy_credits < cost) RETURN(0);
        }
        if (!toggle) RETURN(1);
        if (fhuman) {
            int choice;
            if (faction == current() && attack_flags(1)) {
                game_parse_num(0, cost);
                game_parse_num(1, F->energy_credits);
                success_rates(1, morale_veh(veh, 1, 0), 0, -1);
                success_rates(2, morale_veh(veh, 1, 0), 1, -1);
                popup_start(pop, text(0x69060c), 0);
                if (F->energy_credits >= cost) popup_start(pop, text(0x690614), 0x80);
                choice = popup_exec(pop);
                if (!choice) RETURN(0);
                game_vehs[veh].unk_2c[0] |= (uint8_t)((choice + 0x1f) << 3);
            } else {
                choice = ((game_vehs[veh].unk_2c[0] & 8) != 0) + 1;
            }
            diff = choice - 1;
        }
        if (!attack_flags(2)) goto frame_up;
        F->energy_credits -= cost;
        if (faction == current()) game_console_update_data(game_console, 0);
        goto frame_up;
    }
    if (action != ACT_MIND_CONTROL) goto frame_up;
    // Mind control of a base: its price (mind_control), scaled as for a
    // unit, then by the target's PROBE, less one for a genejack factory and
    // two more for a covert ops center: -2 halves it, -1 takes a quarter
    // off, 1 adds half, 2 doubles it, 3 makes it immune; any other value
    // leaves it, so a PROBE above 3 makes the base easier to take, not
    // harder (Thinker fixes this).
    cost = mind_control(base, faction, 0);
    if (cost < 0) {
        if (!human(faction)) {
            action = ACT_RESEARCH;
            goto start;
        }
        RETURN(0);
    }
    if (def(tfac)->rule_flags & RULE_FLAG_MIND_IMMUNE) {
        if (!human(faction)) goto ai_choice_again;
        if (faction == current() && attack_flags(1)) {
            say(0, adj_of(tfac));
            notice_at(0x690620, 0);
        }
        RETURN(0);
    }
    cost = scaled_cost(faction, cost);
    {
        int p = TF->se[SE_PROBE];
        if (built(base, FAC_GENEJACK_FACTORY)) p--;
        if (built(base, FAC_COVERT_OPS_CENTER_)) p += 2;
        if (p == -2) {
            cost /= 2;
        } else if (p == -1) {
            cost -= cost / 4;
        } else if (p == 1) {
            cost += cost / 2;
        } else if (p == 2) {
            cost *= 2;
        } else if (p == 3) {
            if (!human(faction)) {
                action = ACT_RESEARCH;
                goto start;
            }
            if (faction == current() && attack_flags(1)) {
                say(0, adj_of(tfac));
                notice_at(0x690630, 0);
            }
            RETURN(0);
        }
    }
    // The AI pays only with enough to spare: against a human, the price
    // plus a tenth of its reserves (up to 50) and 2; otherwise two thirds
    // of the price times its leaning to power over wealth, plus 3.
    if (!human(faction)) {
        if (human(tfac)) {
            if (F->energy_credits < cost + game_range(F->energy_credits / 10, 0, 50) + 2) goto ai_choice_again;
        } else if (F->energy_credits < (F->ai_power - F->ai_wealth + 3) * cost * 2 / 3) {
            goto ai_choice_again;
        }
    } else {
        int choice;
        if (faction == current() && attack_flags(1)) {
            say(0, (char *)B->name);
            game_parse_num(0, cost);
            game_parse_num(1, F->energy_credits);
            success_rates(1, morale_veh(veh, 1, 0), 0, -1);
            success_rates(2, morale_veh(veh, 1, 0), 1, -1);
            if (F->energy_credits < cost) {
                game_wave_it(9);
                notice_at(0x69063c, 0);
                RETURN(0);
            }
            game_wave_it(0x20);
            popup_start(pop, text(0x69064c), 0);
            popup_start(pop, text(0x69065c), 0x80);
            choice = popup_exec(pop);
            if (choice <= 0) RETURN(0);
            game_vehs[veh].unk_2c[0] |= (uint8_t)((choice + 0x1f) << 3);
        } else {
            choice = ((game_vehs[veh].unk_2c[0] & 8) != 0) + 1;
        }
        if (choice > 1) diff = 1;  // total thought control
    }
    if (!attack_flags(2)) goto frame_up;
    F->energy_credits -= cost;
    F->mind_control_total += 4;
    TF->diplo_mind_control[faction] += 4;
    if (faction == current()) game_console_update_data(game_console, 0);
    goto frame_up;

sabotage_chosen:
    if (human(faction)) {
        // The player may pick the target instead, a harder task; a
        // headquarters, or perimeter defenses or a tachyon field, harder
        // still.
        success_rates(1, morale_veh(veh, 1, 0), 0, base);
        int choice;
        if (faction == current() && attack_flags(1)) {
            choice = success_rates(2, morale_veh(veh, 1, 0), 1, base)
                         ? pop_up_at(0x6904e8, al ? 0x6904c8 : 0x6904d8)
                         : 0;
            game_vehs[veh].unk_2c[0] |= (uint8_t)(choice << 3);
        } else {
            choice = game_vehs[veh].unk_2c[0] >> 3 & 1;
        }
        if (!choice) goto frame_up;
        diff = 1;
        if (faction == current() && attack_flags(1)) {
            popup_start(pop, text(0x6904f4), 0);
            if (stack_check(veh_at(B->x, B->y), 2, PLAN_PLANET_BUSTER_, -1, -1))
                popup_item(pop, game_label(LABEL_PLANET_BUSTERS), SAB_PLANET_BUSTERS);
            for (int i = 1; i < 70; i++) {
                if (i == FAC_HEADQUARTERS) continue;
                if (i == FAC_PRESSURE_DOME ? sea_xy(B->x, B->y) : i >= FAC_SABOTAGE_LIMIT) continue;
                if (built(base, i)) popup_item(pop, game_strings_get(game_strings, game_facilities[i].name), i);
            }
            clear();
            says(name);
            says(text(0x682820));
            says(text(0x682e9c));
            says(game_label(LABEL_PRODUCTION));
            says(text(0x682e98));
            popup_item(pop, game_battle_text, SAB_PRODUCTION);
            game_parse_it(*game_script_file, text(0x6904fc));
            popup_item(pop, game_battle_text, SAB_ABORT);
            sab = popup_exec(pop);
            if (sab < 0) RETURN(0);
            game_vehs[veh].unk_2c[1] = (uint8_t)sab;
        } else {
            sab = (int8_t)game_vehs[veh].unk_2c[1];
        }
        if (sab == SAB_ABORT) goto frame_up;
        if (built(base, FAC_HEADQUARTERS)) {
            if (faction == current() && attack_flags(1)) {
                say(0, (char *)B->name);
                say(1, adj_of(tfac));
                success_rates(2, morale_veh(veh, 1, 0), 2, base);
                if (!pop_up_at(0x690528, al ? 0x690508 : 0x690518)) RETURN(0);
            }
            diff = 2;
        } else if (sab == FAC_PERIMETER_DEFENSE_ || sab == FAC_TACHYON_FIELD_) {
            if (faction == current() && attack_flags(1)) {
                say(0, (char *)B->name);
                game_parse_say(1, (int)(intptr_t)game_facilities[sab].name, -1, -1);
                success_rates(2, morale_veh(veh, 1, 0), 2, base);
                if (!pop_up_at(0x690550, al ? 0x690530 : 0x690540)) RETURN(0);
            }
            diff = 2;
        }
        goto frame_up;
    }
    // A veteran AI team goes for planet busters or the defenses, harder.
    if (morale_veh(veh, 1, 0) < 5) goto frame_up;
    if (stack_check(veh_at(B->x, B->y), 2, PLAN_PLANET_BUSTER_, -1, -1)) {
        sab = SAB_PLANET_BUSTERS;
        diff = 1;
        goto frame_up;
    }
    {
        static const int defenses[] = {FAC_TACHYON_FIELD_, FAC_PERIMETER_DEFENSE_, FAC_CHILDREN_CRECHE, FAC_COMMAND_CENTER_};
        for (int i = 0; i < 4; i++)
            if (built(base, defenses[i])) {
                sab = defenses[i];
                diff = 1;
                break;
            }
    }
    goto frame_up;

ai_choice_again:  // from mind control the AI gave up on: checked is set by then
ai_choice: {
    // The AI's action against a base: sabotage against a faction it holds
    // a grudge against (or riots, if the base is balanced on the edge and
    // the region is in its plans), else infiltration; the genetic plague
    // for an atrocity victim's or a wanton faction's targets; sabotage
    // with its own units near; research data if allowed and worth it;
    // and, once, against a headquarters, assassination or freeing a
    // captive leader, or else mind control.
    B = &game_bases[base];
    uint32_t d = F->diplo_status[tfac];
    action = d & TREATY_REVENGE_VENDETTA ? ACT_SABOTAGE : ACT_INFILTRATE;
    int region = tile(B->x, B->y)->region;
    if ((d & TREATY_REVENGE_VENDETTA) && F->region_base_plan[region] && B->talent_total == B->drone_total &&
        !(game_rand() % 2) && B->pop_size > 3)
        action = ACT_RIOTS;
    if (gene_allow) {
        if (B->pop_size >= 6) {
            uint32_t d2 = F->diplo_status[tfac];
            if (((d2 & TREATY_ATROCITY_VICTIM) || (*game_un_charter_a > *game_un_charter_b && (d2 & TREATY_WANT_REVENGE))) &&
                F->region_base_plan[region] == PLAN_DEFENSIVE)
                action = ACT_PLAGUE;
        }
        if (B->pop_size >= 4 && (F->flags & FACTION_FLAG_WANTON)) {
            uint32_t d2 = F->diplo_status[tfac];
            if (!(d2 & TREATY_PACT_TREATY_TRUCE) || (d2 & TREATY_WANT_REVENGE)) action = ACT_PLAGUE;
        }
    }
    int near = 0;
    if (F->diplo_status[tfac] & TREATY_REVENGE_VENDETTA) {
        for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
            int x = B->x + game_radius_x[i], y = B->y + game_radius_y[i];
            if (!(*game_map_flat & 1)) {
                if (x < 0) x += *game_map_width;
                else if (x >= *game_map_width) x -= *game_map_width;
            }
            if (y < 0 || y >= *game_map_height || x < 0 || x >= *game_map_width) continue;
            if (sea_xy(x, y)) continue;
            int s = stack_fix(veh_at(x, y));
            if (s >= 0 && game_vehs[s].faction == faction && stack_check(s, 4, faction, -1, -1)) {
                near = 1;
                action = ACT_SABOTAGE;
                break;
            }
        }
    }
    if (steal_tech_allowed() &&
        (!(B->state & BASE_STATE_DATA_STOLEN) || morale_veh(veh, 1, 0) >= 4 ||
         (action == ACT_INFILTRATE && (F->diplo_status[tfac] & TREATY_INFILTRATOR))) &&
        !near && TF->region_base_plan[region] != PLAN_DEFENSIVE)
        action = ACT_RESEARCH;
    if (gene_allow && B->pop_size >= 4 && aah_ooga(faction, faction) == tfac && game_climactic_battle()) {
        int fight = F->ai_fight;
        if ((fight > 0 || (!fight && (F->diplo_status[tfac] & TREATY_WANT_REVENGE))) &&
            (F->diplo_status[tfac] & (TREATY_UNK_800 | TREATY_SHALL_BETRAY | TREATY_REVENGE_VENDETTA)))
            action = ACT_PLAGUE;
    }
    if (checked) goto start;
    checked = built(base, FAC_HEADQUARTERS);
    if (checked) {
        if (action != ACT_RESEARCH && *tech_accumulated(tfac) >= TF->tech_cost / 2 &&
            ((F->diplo_status[tfac] & TREATY_REVENGE_VENDETTA) || TF->tech_ranking / 2 > F->tech_ranking / 2 + 4))
            action = ACT_ASSASSINATE;
        if (*game_expansion && !*game_net_multiplayer) {
            // free_leader_target: the target's captives, as if the i-th
            // record held faction i + 1 (the records name their faction,
            // which the original ignores here), seven of them where there
            // are six (the seventh is the end of the record), preferring a
            // pact partner, then a treaty partner whose... next word is no
            // pact: the test reads the status one faction on (Thinker
            // corrects both).
            int found = 0;
            checked = built(base, FAC_HEADQUARTERS);
            if (checked) {
                for (int i = 0; i < 7; i++) {
                    if (!*captive(tfac, i, CAPTIVE_HELD)) continue;
                    uint32_t di = F->diplo_status[i + 1];
                    if (di & TREATY_VENDETTA) continue;
                    found = 1;
                    if (leader == -1 || (di & TREATY_PACT) ||
                        ((di & TREATY_TREATY) && !(diplo_word(faction, leader + 1) & TREATY_PACT)))
                        leader = i + 1;
                }
                if (found) {
                    action = ACT_FREE_LEADER;
                    checked = 1;
                    goto start;
                }
            }
        }
    } else if ((F->diplo_status[tfac] & TREATY_REVENGE_VENDETTA) && (TF->diff_level > 2 || !human(tfac)) &&
               TF->se[SE_PROBE] < 3) {
        action = ACT_MIND_CONTROL;
    }
    checked = 1;
    goto start;
}

frame_up:
    // Someone to frame for it: the player picks a faction the team has a
    // commlink with, harder; a veteran AI team on an easy task frames the
    // human player in the lead, if the target talks to the player and
    // isn't at war with them.
    frame = 0;
    if ((action >= ACT_MIND_CONTROL || action <= ACT_INFILTRATE) && action != ACT_FREE_LEADER) goto defend;
    if (human(faction)) {
        if (human(tfac)) goto defend;
        if (faction == current() && attack_flags(1)) {
            popup_start(pop, text(0x690668), 0);
            success_rates(2, morale_veh(veh, 1, 0), diff, base);
            if (success_rates(3, morale_veh(veh, 1, 0), diff + 1, base) &&
                !game_x_text_open(*game_script_file, text(0x690670))) {
                game_text_get();
                game_parse_string(*game_text_line, game_battle_text);
                popup_item(pop, game_battle_text, 0);
                game_text_get();
                int n = 0;
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (i == faction || i == tfac) continue;
                    if (!(F->diplo_status[i] & TREATY_COMMLINK) || !(*game_factions_alive & (1u << i))) continue;
                    n++;
                    *game_parse_gender = def(i)->leader_female;
                    *game_parse_plural = 0;
                    say(0, def(i)->leader_title);
                    say(1, name_of(i));
                    clear();
                    game_parse_string(*game_text_line, game_battle_text);
                    popup_item(pop, game_battle_text, i);
                }
                frame = n ? popup_exec(pop) : 0;
            }
            game_vehs[veh].unk_2c[0] |= (uint8_t)(frame << 5);
        } else {
            frame = game_vehs[veh].unk_2c[0] >> 5;
        }
        if (frame) diff++;
        goto defend;
    }
    if (morale_veh(veh, 1, 0) < 5 || diff || human(tfac)) goto defend;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || i == tfac || !human(i) || !(*game_factions_alive & (1u << i))) continue;
        if (i != *game_ranked_faction) continue;
        uint32_t d = TF->diplo_status[i];
        if (d & TREATY_VENDETTA) continue;
        if (d & TREATY_COMMLINK) {
            frame = i;
            diff++;
            break;
        }
    }

defend: {
    // Only a move made goes on. Caught (a draw by morale against the
    // target's PROBE, with a covert ops center, falls below the
    // difficulty), the team dies, unless the algorithmic enhancement gets
    // it through half the time; the Hunter-Seeker Algorithm catches half
    // of those that make it. The halves are the clock's lowest bit.
    if (!attack_flags(2)) RETURN(1);
    act1 = action + 1;
    int covert = act1 ? (has_fac(FAC_COVERT_OPS_CENTER_, base, 0) ? 2 : 0) : 0;
    int m = morale_veh(veh, 1, 0);
    if (m < 1) m = 1;
    int pv = game_range(TF->se[SE_PROBE] + covert, -2, 0);
    int caught;
    uint32_t label, picture;
    if (game_rnd(m / 2 - pv + 1) < diff) {
        caught = !*game_probe_algo || *game_probe_target_hsa || time_bit();
        label = al ? 0x690698 : 0x6906b0, picture = al ? 0x690688 : 0x6906a0;
    } else {
        caught = *game_probe_target_hsa && time_bit();
        label = al ? 0x6906c8 : 0x6906e0, picture = al ? 0x6906b8 : 0x6906d0;
    }
    if (caught) {
        frame = -frame;
        if (faction == current()) notice_at(label, picture);
        if (tfac == current()) busted_action = action;
        kill(veh);
        goto upkeep;
    }
}

    {
        struct base *TB = base >= 0 ? &game_bases[base] : NULL;
        switch (action) {
        case ACT_RESEARCH:
            TF->diplo_stolen_techs[faction]++;
            if (tech < 0) {
                steal_tech(faction, tfac, 1);
            } else {
                if (faction == current() || tfac == current()) {
                    say(0, adj_of(tfac));
                    say(2, adj_of(faction));
                    clear();
                    if (tech == TECH_NOTHING) {
                        says(text(0x6906e8));
                    } else {
                        say_tech(game_battle_text, tech, 0);
                        say(1, game_battle_text);
                        clear();
                        says(text(0x6906f4));
                    }
                    says(text(faction == current() ? 0x690700 : 0x690704));
                    notice(game_battle_text, text(al ? 0x690708 : 0x690718));
                }
                game_tech_achieve(faction, tech, tfac, 0);
            }
            TB->state |= BASE_STATE_DATA_STOLEN;
            break;
        case ACT_SABOTAGE:
            if (sab == SAB_ABORT) {
                notice_at(0x690728, 0);
                RETURN(1);
            }
            clear();
            if (sab == SAB_PRODUCTION) {
                TB->minerals_2 = 0;
                TB->minerals = 0;
                say(0, name);
                says(text(0x690734));
                say(1, TB->name);
                say(2, adj_of(faction));
                says(text(faction == current() ? 0x690740 : 0x690744));
                if (faction == current() || tfac == current()) notice(game_battle_text, text(al ? 0x690748 : 0x690758));
            } else if (sab == SAB_PLANET_BUSTERS) {
                int v = veh_at(TB->x, TB->y);
                if (v < 0) RETURN(1);
                while (game_protos[game_vehs[v].proto].plan != PLAN_PLANET_BUSTER_) {
                    v = game_vehs[v].next_in_stack;
                    if (v < 0) break;
                }
                if (v >= 0) {
                    const struct veh *PB = &game_vehs[v];
                    if (PB->faction == current()) {
                        say(0, game_protos[PB->proto].name);
                        say(1, adj_of(faction));
                        notice_at(0x690788, al ? 0x690768 : 0x690778);
                    } else if (faction == current()) {
                        say(0, game_protos[PB->proto].name);
                        say(2, adj_of(PB->faction));
                        notice_at(0x6907b8, al ? 0x690798 : 0x6907a8);
                    }
                    kill(v);
                    if (veh > v) veh--;
                }
            } else {
                set_fac(sab, base, 0);
                game_parse_say(0, (int)(intptr_t)game_facilities[sab].name, -1, -1);
                says(text(0x6907c8));
                say(1, TB->name);
                say(2, adj_of(faction));
                says(text(faction == current() ? 0x6907d4 : 0x6907d8));
                if (faction == current() || tfac == current()) notice(game_battle_text, text(al ? 0x6907dc : 0x6907ec));
            }
            game_base_win_check_base(game_base_win, base);
            game_draw_tile(TB->x, TB->y, 2);
            break;
        case ACT_DRAIN: {
            // The base's share of the reserves, at least a sixth of it per
            // morale level and a random part of the rest; 1 is nothing.
            int steal = steal_energy(base);
            int least = steal * morale_veh(veh, 1, 0) / 6;
            int amount = game_range(game_rnd(steal - least) / 2 + least, 0, TF->energy_credits);
            if (amount == 1) amount = 0;
            TF->energy_credits -= amount;
            F->energy_credits += amount;
            if (faction == current()) {
                game_console_update_data(game_console, 0);
                game_parse_num(0, amount);
                say(0, TB->name);
                say(2, adj_of(tfac));
                notice_at(amount ? 0x690828 : 0x690834, al ? 0x690808 : 0x690818);
            } else if (tfac == current()) {
                game_console_update_data(game_console, 0);
                game_parse_num(0, amount);
                say(0, TB->name);
                say(2, adj_of(faction));
                notice_at(amount ? 0x690860 : 0x69086c, al ? 0x690840 : 0x690850);
            }
            break;
        }
        case ACT_RIOTS: {
            // Ten more turns of unrest, up to two and a half per citizen
            // (or what it had).
            int most = TB->pop_size * 10 / 4, turns = TB->assimilation_turns;
            if (turns > most) most = turns;
            TB->assimilation_turns = (uint8_t)game_range(turns + 10, 0, most);
            set_base(base);
            base_compute(1);
            say(0, TB->name);
            struct base *C = *game_current_base;
            if (C->drone_total > C->talent_total) {
                C->state |= BASE_STATE_DRONE_RIOT;
                game_draw_tile(TB->x, TB->y, 2);
                if (faction == current() || tfac == current())
                    notice_at(al ? 0x69088c : 0x6908ac, al ? 0x69087c : 0x69089c);
            } else if (faction == current() || tfac == current()) {
                notice_at(al ? 0x6908cc : 0x6908ec, al ? 0x6908bc : 0x6908dc);
            }
            break;
        }
        case ACT_ASSASSINATE: {
            int lost = game_rnd(*tech_accumulated(tfac));
            *tech_accumulated(tfac) -= lost;
            if (faction == current() || tfac == current()) {
                game_parse_num(0, lost);
                say(0, TB->name);
                say(1, adj_of(tfac));
                notice_at(0x69092c, al ? 0x69090c : 0x69091c);
            }
            break;
        }
        case ACT_FREE_LEADER: {
            // The player picks among the captives (six records, by the
            // faction they name); the AI takes the one it chose. The
            // leader's faction lands again as a probe team's escape, its
            // techs go to the liberator (but in a scenario without tech
            // trading), and it owes them: a pact, having surrendered.
            int l;
            if (human(faction) && faction == current()) {
                say(1, adj_of(tfac));
                popup_start(pop, text(0x69093c), 0x40);
                popup_picture(pop, al ? 0x690944 : 0x690954, al ? 0x690964 : 0x690974, &w, &h);
                for (int i = 0; i < 6; i++) {
                    if (!*captive(tfac, i, CAPTIVE_HELD)) continue;
                    int f = *captive(tfac, i, CAPTIVE_FACTION);
                    clear();
                    says(title_of(f));
                    says(text(0x682820));
                    says(name_of(f));
                    says(text(0x682820));
                    says(game_label(LABEL_OF_THE));
                    says(text(0x682820));
                    says(noun_of(f));
                    popup_item(pop, game_battle_text, f);
                }
                l = popup_exec(pop);
                if (l < 0) RETURN(0);
            } else {
                l = leader;
            }
            if (l == -1) break;
            home_saved = game_vehs[veh].home_base;
            if (home_saved < 0) veh_kill(veh);
            *game_factions_alive |= (uint8_t)(1u << l);
            setup_player(l, tfac, 1);
            TF->unk_20a0--;
            for (int i = 0; i < 6; i++)
                if (*captive(tfac, i, CAPTIVE_HELD) && *captive(tfac, i, CAPTIVE_FACTION) == l)
                    *captive(tfac, i, CAPTIVE_HELD) = 0;
            if (faction == current() || tfac == current()) {
                say(0, title_of(l));
                say(1, name_of(l));
                say(2, noun_of(l));
                notice_at(0x690998, 0x690984);
            }
            if (!(*game_rules & RULES_NO_TECH_TRADING))
                for (int t = 0; t < TECH_COUNT; t++)
                    if (has_tech(t, l) && !has_tech(t, faction)) game_net_tech(faction, t, l, 0);
            *(int32_t *)((char *)game_info_win + 0x90) = 0;
            game_console_update_data(game_console, 0);
            F->diplo_friction[l] = 0;
            game_net_treaty_off(l, faction, TREATY_WANT_REVENGE, 1);
            game_net_treaty_on(faction, l, TREATY_UNK_8000000 | TREATY_COMMLINK | TREATY_TREATY, 0);
            game_net_set_treaty(l, faction, TREATY_SURRENDERED_, 1, 0);
            game_net_treaty_on(l, faction, TREATY_UNK_1000000 | TREATY_PACT, 0);
            game_factions[l].diplo_patience[faction] = 0;
            F->diplo_spoke[l] = 0;
            diplomacy_check(faction, l, 0);
            F->diplo_reputation++;
            game_factions[l].diplo_reputation++;
            break;
        }
        case ACT_PLAGUE: {
            // The base loses citizens by the share of the defensive techs
            // the target lacks (one more known if plagued before), halved
            // for a base plagued before and by each hospital, plus one, but
            // keeps one; its units are hurt by up to their health.
            atrocity(faction, tfac, 1, 0);
            if (gene_techs < 1) gene_techs++;
            if (TF->flags & FACTION_FLAG_PLAGUED) gene_known++;
            int known = game_range(gene_known, 1, gene_techs);
            TF->flags |= FACTION_FLAG_PLAGUED;
            int loss = (gene_techs - known) * TB->pop_size / gene_techs;
            if (TB->state & BASE_STATE_PLAGUED) loss /= 2;
            if (has_fac(FAC_RESEARCH_HOSPITAL, base, 0)) loss /= 2;
            if (has_fac(FAC_NANOHOSPITAL, base, 0)) loss /= 2;
            loss++;
            if (loss >= TB->pop_size) loss = TB->pop_size - 1;
            for (int v = veh_at(TB->x, TB->y); v >= 0; v = game_vehs[v].next_in_stack) {
                int r = game_rnd((veh_health(v) + 1) / 2);
                int damage = r + veh_health(v) / 2;
                if (has_fac(FAC_RESEARCH_HOSPITAL, base, 0)) damage /= 2;
                if (has_fac(FAC_NANOHOSPITAL, base, 0)) damage /= 2;
                if (TB->state & BASE_STATE_PLAGUED) damage /= 2;
                game_vehs[v].damage += (uint8_t)damage;
            }
            TB->state |= BASE_STATE_PLAGUED;
            if (loss) TB->pop_size -= (int8_t)loss;
            TB->nutrients = 0;
            spot_base(base, faction);
            game_draw_tile(TB->x, TB->y, 2);
            if (faction == current() || tfac == current() || (*game_state & STATE_OMNISCIENT_)) {
                say(0, TB->name);
                say(1, noun_of(faction));
                clear();
                says(text(0x6909a4));
                says(text(faction == current() ? 0x6909b4 : 0x6909b8));
                pop_up(game_battle_text, text(al ? 0x6909bc : 0x6909cc));
            }
            break;
        }
        case ACT_UNIT: {
            // The unit changes sides: a design of the target's own is
            // proposed to the faction (one above 64: not faction 1's first
            // design, as Thinker has it), and the unit stays where it is,
            // with a home of the faction's nearest base.
            struct veh *T = &game_vehs[tveh];
            TF->units_active[T->proto]--;
            if (T->proto > 64) {
                struct unit_proto *P = &game_protos[T->proto];
                int p = propose_proto(faction, P->chassis, P->weapon, P->armor, P->abilities, P->reactor, -2, P->name);
                if (p >= 0) T->proto = (int16_t)p;
            }
            T->faction = (uint8_t)faction;
            F->units_active[T->proto]++;
            F->mind_control_total++;
            TF->diplo_mind_control[faction]++;
            owner_set(T->x, T->y, faction);
            T->home_base = (int16_t)base_find_2(T->x, T->y, faction);
            T->moves_spent = 0;
            T->order = 0;
            T->state &= 0xfcffbdffu;
            spot_stack(tveh, tfac);
            game_draw_tile(T->x, T->y, 2);
            if (tfac == current()) {
                game_console_focus(game_console, T->x, T->y, faction);
                say(1, noun_of(faction));
                notice_at(0x6909fc, al ? 0x6909dc : 0x6909ec);
            }
            if (diff) RETURN(1);
            goto aftermath;
        }
        default:
            break;
        }

        // The way home: by a draw by morale against the target's PROBE
        // (a covert ops center counted but for a unit, which doesn't come
        // here), past the difficulty, to the faction's nearest base (a
        // ship to its first port that reaches the ship's sea); the
        // algorithmic enhancement gets half of the others home, the
        // Hunter-Seeker Algorithm stops half of the ships rerouted. Else
        // the team is lost (a liberator other than a probe team goes back
        // to its home base, where it had one).
        struct veh *V = &game_vehs[veh];
        int covert = act1 ? (has_fac(FAC_COVERT_OPS_CENTER_, base, 0) ? 2 : 0) : 0;
        int pv = game_range(TF->se[SE_PROBE] + covert, -2, 0);
        int r = game_rnd(morale_veh(veh, 1, 0) - pv);
        home = -1;
        if (r > diff) {
            home = base_find_2(V->x, V->y, faction);
            if (game_veh_triad(veh) == TRIAD_SEA && home >= 0 && !is_port(home, 0)) {
                home = port_for(veh, faction);
                if (*game_probe_target_hsa && time_bit()) {
                    frame = -frame;
                    home = -1;
                }
            }
        } else if (*game_probe_target_hsa || !*game_probe_algo || !time_bit()) {
            frame = -frame;
        } else {
            home = base_find_2(V->x, V->y, faction);
            if (game_veh_triad(veh) == TRIAD_SEA && home >= 0 && !is_port(home, 0)) home = port_for(veh, faction);
        }
        if (home < 0 && action == ACT_FREE_LEADER && game_protos[V->proto].weapon != WPN_PROBE_TEAM) home = home_saved;
        if (home < 0) {
            veh_kill(veh);
        } else {
            struct base *H = &game_bases[home];
            veh_put(veh, H->x, H->y);
            V->movement_turns = 0;
            game_draw_tiles(TB->x, TB->y, 2);
            game_draw_tile(H->x, H->y, 2);
            say(1, H->name);
            int before = morale_veh(veh, 1, 0);
            V->morale = (uint8_t)game_range(V->morale + 1, 0, 6);
            veh_skip(veh);
            if (faction == current()) {
                say(0, game_protos[V->proto].name);
                game_parse_say(2, (int)(intptr_t)game_morale_names[morale_veh(veh, 1, 0)][0], -1, -1);
                clear();
                says(text(0x690a18));
                if (action == ACT_FREE_LEADER) says(text(0x690a28));
                else say_num(before != morale_veh(veh, 1, 0) && morale_veh(veh, 1, 0) >= 1);
                notice(game_battle_text, text(al ? 0x690a2c : 0x690a3c));
            }
        }

        if (action == ACT_MIND_CONTROL) {
            // The base changes hands, with the target's units in it and
            // next to it (those not in a base).
            if (faction == current() || tfac == current() || game_spying(faction) || game_spying(tfac) ||
                (*game_state & STATE_OMNISCIENT_)) {
                if ((*game_state & STATE_OMNISCIENT_) || TB->faction == current() || (TB->visibility & (1u << current())))
                    game_console_focus(game_console, TB->x, TB->y, TB->faction);
                say(0, title_of(faction));
                say(1, name_of(faction));
                say(2, noun_of(faction));
                say(3, TB->name);
                say(4, title_of(tfac));
                say(5, name_of(tfac));
                clear();
                says(text(0x690a4c));
                says(text(faction == current() ? 0x690a58 : tfac == current() ? 0x690a5c : 0x690a60));
                pop_up(game_battle_text, text(al ? 0x690a64 : 0x690a74));
            }
            for (int v = *game_veh_count - 1; v >= 0; v--) {
                if (v >= *game_veh_count) continue;
                struct veh *U = &game_vehs[v];
                if (U->faction != tfac) continue;
                int d = vector_dist(U->x, U->y, TB->x, TB->y);
                if (d > 1 || (d && base_who(U->x, U->y) >= 0)) continue;
                TF->units_active[U->proto]--;
                if (U->proto > 64) {
                    struct unit_proto *P = &game_protos[U->proto];
                    int p = propose_proto(faction, P->chassis, P->weapon, P->armor, P->abilities, P->reactor, -2, P->name);
                    if (p >= 0) U->proto = (int16_t)p;
                }
                U->faction = (uint8_t)faction;
                U->home_base = (int16_t)base;
                U->moves_spent = 0;
                F->units_active[U->proto]++;
                if (game_chassis[game_protos[U->proto].chassis].range) U->movement_turns = 0;
                if (U->order != ORDER_HOLD_) U->order = 0;
                owner_set(U->x, U->y, faction);
                U->visibility |= (uint8_t)(1u << tfac);
            }
            if (!diff) act_of_aggression(faction, tfac);
            game_capture_base(base, faction, 1);
        }
        if (faction == current() && home < 0)
            notice_at(action == ACT_MIND_CONTROL ? (al ? 0x690a94 : 0x690ac0) : (al ? 0x690aa4 : 0x690ad0),
                      al ? 0x690a84 : 0x690ab0);
    }

upkeep:
    if (action == ACT_INFILTRATE) RETURN(1);

aftermath:
    // What the victim, at peace with the faction (and not its surrendered
    // vassal), makes of it. Total thought control: a betrayal. A framed AI
    // caught out declares vendetta on the faction; a framed human is asked
    // whether to end the pact or declare it. A human victim is asked the
    // same of the faction; a framed faction gets the blame, in a vendetta
    // with the victim. Else the AI victim may let it go (by rankings and
    // its fears), or declare vendetta (double-cross for mind control).
    if (!has_treaty(faction, tfac, TREATY_PACT_TREATY_TRUCE)) RETURN(1);
    if (has_treaty(tfac, faction, TREATY_PACT) && has_treaty(tfac, faction, TREATY_SURRENDERED_)) RETURN(1);
    if (action == ACT_MIND_CONTROL && diff) {
        set_treaty(tfac, faction, TREATY_SHALL_BETRAY | TREATY_WANT_REVENGE, 1);
        RETURN(1);
    }
    if (frame < 0) {
        int f = -frame;
        if (!human(f)) {
            if (faction == current()) {
                say(0, title_of(f));
                say(1, name_of(f));
                notice_at(al ? 0x690aec : 0x690b08, al ? 0x690adc : 0x690af8);
            }
            // The original negates the frame twice here, so it stays
            // caught: the faction isn't blamed (Thinker makes it positive,
            // which blames it).
            treaty_on(f, faction, TREATY_VENDETTA);
        } else {
            say(0, title_of(faction));
            say(1, name_of(faction));
            say(2, game_get_pact_hood(faction, f));
            say(3, noun_of(faction));
            say(4, game_get_his_her(faction, 0));
            say(5, game_get_him_her(faction, 0));
            say(6, title_of(tfac));
            say(7, name_of(tfac));
            say(8, noun_of(tfac));
            if (*game_net_multiplayer) {
                agenda_on(f, faction, 0x20);
                if (f == current()) notice_at(0x690b14, 0);
                if (faction == current()) {
                    say(0, title_of(f));
                    say(1, name_of(f));
                    say(2, noun_of(f));
                    notice_at(al ? 0x690b34 : 0x690b54, al ? 0x690b24 : 0x690b44);
                }
            } else if (has_treaty(f, faction, TREATY_PACT)) {
                if (pop_up_at(al ? 0x690b74 : 0x690b94, al ? 0x690b64 : 0x690b84)) {
                    pact_ends(faction, f);
                    game_factions[f].diplo_spoke[faction] = *game_turn;
                }
            } else if (pop_up_at(al ? 0x690bb4 : 0x690bd0, al ? 0x690ba4 : 0x690bc0)) {
                treaty_on(faction, f, TREATY_VENDETTA);
                game_factions[f].diplo_spoke[faction] = *game_turn;
            }
            frame = -f;
        }
    }
    if (human(tfac)) {
        say(0, title_of(faction));
        say(1, name_of(faction));
        say(2, game_get_pact_hood(faction, tfac));
        say(3, noun_of(faction));
        say(4, game_get_his_her(faction, 0));
        say(5, game_get_him_her(faction, 0));
        // The victim is shown the team caught, if it was.
        uint32_t busted_label = 0, file = 0, sprite = 0, ask = 0;
        if (*game_net_multiplayer) {
            agenda_on(tfac, faction, 0x20);
            if (action >= ACT_MIND_CONTROL) F->integrity_blemishes++;
            if (tfac == current()) {
                if (busted_action) {
                    clear();
                    says(text(0x690bdc));
                    say_num(busted_action);
                    popup_start(pop, game_battle_text, 0);
                    popup_picture(pop, al ? 0x690be4 : 0x690c04, al ? 0x690bf4 : 0x690c14, &w, &h);
                }
                popup_start(pop, text(0x690c24), busted_action ? 0x80 : 0);
                popup_exec(pop);
            }
            if (faction == current()) {
                say(0, title_of(tfac));
                say(1, name_of(tfac));
                say(2, noun_of(tfac));
                notice_at(0x690c30, 0);
            }
            RETURN(1);
        }
        int pact = has_treaty(tfac, faction, TREATY_PACT) != 0;
        if (pact) busted_label = 0x690c40, file = al ? 0x690c48 : 0x690c68, sprite = al ? 0x690c58 : 0x690c78, ask = 0x690c88;
        else busted_label = 0x690c94, file = al ? 0x690c9c : 0x690cbc, sprite = al ? 0x690cac : 0x690ccc, ask = 0x690cdc;
        if (busted_action) {
            clear();
            says(text(busted_label));
            say_num(busted_action);
            popup_start(pop, game_battle_text, 0);
            popup_picture(pop, file, sprite, &w, &h);
        }
        popup_start(pop, text(ask), busted_action ? 0x80 : 0);
        if (popup_exec(pop)) {
            if (pact) pact_ends(faction, tfac);
            else if (action < ACT_MIND_CONTROL) treaty_on(faction, tfac, TREATY_VENDETTA);
            else double_cross(faction, tfac, -1);
            TF->diplo_spoke[faction] = *game_turn;
        }
        RETURN(1);
    }
    if (frame > 0) {
        treaty_on(tfac, frame, TREATY_UNK_40 | TREATY_VENDETTA | TREATY_COMMLINK);
        set_treaty(tfac, frame, TREATY_WANT_REVENGE, 1);
        if (frame == current()) {
            say(0, title_of(tfac));
            say(1, name_of(tfac));
            say(2, noun_of(tfac));
            notice_at(0x690d04, al ? 0x690ce4 : 0x690cf4);
        }
        if (faction == current()) {
            say(0, title_of(tfac));
            say(1, name_of(tfac));
            say(2, noun_of(tfac));
            say(3, title_of(frame));
            say(4, name_of(frame));
            say(5, noun_of(frame));
            notice_at(0x690d30, al ? 0x690d10 : 0x690d20);
        }
        RETURN(1);
    }
    diplomacy_check(faction, tfac, 0);
    game_diplomacy_caption(faction, tfac);
    say(0, title_of(faction));
    say(1, name_of(faction));
    say(2, game_get_pact_hood(faction, tfac));
    void *portrait = game_faction_portraits[tfac];
    if (has_treaty(faction, tfac, TREATY_PACT)) {
        // A pact partner keeps the pact over research data if behind in
        // techs (and nobody is the great satan), or over lesser actions
        // when it outranks the faction (ranked below the top three).
        int lesser = action < ACT_MIND_CONTROL;
        if (action == ACT_RESEARCH && TF->tech_ranking > F->tech_ranking && F->ranking < 6 &&
            !*game_diplo_great_satan) {
            cause_friction(tfac, faction, game_rnd(4));
            if (faction == current()) {
                clear();
                says(text(0x690d44));
                say_num(1);
                game_x_pops(game_battle_text, portrait, NULL);
            }
            RETURN(1);
        }
        if (lesser && F->ranking < TF->ranking && F->ranking < 5) {
            if (faction == current()) game_x_pops(text(0x690d50), portrait, NULL);
            RETURN(1);
        }
        if (faction == current()) game_x_pops(text(0x690d5c), portrait, NULL);
        if (lesser) {
            pact_ends(faction, tfac);
        } else {
            double_cross(faction, tfac, -1);
            treaty_on(faction, tfac, TREATY_VENDETTA);
        }
        RETURN(1);
    }
    if (!has_treaty(tfac, faction, TREATY_WANT_REVENGE) && action < ACT_MIND_CONTROL) {
        int lets_go = 0;
        if ((action == ACT_RESEARCH || action == ACT_ASSASSINATE) && TF->tech_ranking > F->tech_ranking &&
            F->ranking < 6 && !*game_diplo_great_satan && game_rnd(8))
            lets_go = 1;
        if (!lets_go && (action == ACT_SABOTAGE || action == ACT_RIOTS)) {
            if (*game_diplo_fear && !*game_diplo_great_satan) lets_go = 1;
            else if (TF->ranking > F->ranking && !*game_diplo_tribute && game_rnd(8)) lets_go = 1;
        }
        if (lets_go) {
            cause_friction(tfac, faction, game_rnd(4));
            if (faction == current()) {
                clear();
                says(text(0x690d68));
                say_num(action);
                game_x_pops(game_battle_text, portrait, NULL);
            }
            RETURN(1);
        }
    }
    if (faction == current()) game_x_pops(text(0x690d70), portrait, NULL);
    if (action < ACT_MIND_CONTROL) treaty_on(faction, tfac, TREATY_VENDETTA);
    else double_cross(faction, tfac, -1);
    RETURN(1);
#undef RETURN
}

// The original keeps its popup at [ebp-0x53fc] (its frame starts below
// the SEH record), and the game may record its address; ours is given the
// same place.
__asm__(".text\n"
        ".globl probe\n"
        "probe:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5500, %esp\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x53fc(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call probe_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Shadow mode ---------------------------------------------------------------------
//
// A check runs only among AI factions: a human probing faction is asked
// what to do in menus that open script.txt and build lists on the game's
// heap, a human victim (or a human framed for it) is asked whether to
// declare vendetta, and a held question answers 0, which only the AI could
// mean. Network and hotseat games aren't checked, nor a mission against a
// base with messages listed (mind control hands it over; see
// battle_checkable). The sweep makes factions human, hotseat style, in its
// copies of the game: the player's own team (the questions in its menus)
// is left out, the rest compared with every question answered 0.

extern struct hook_module probe_module;
#define PROBE (&probe_module)

static int probe_checkable(int base) {
    if (*game_net_multiplayer || *game_pbem || *game_human_factions) return 0;
    return base < 0 || !base_listed(base);
}

// Mind control of a base ends in the game's capture_base(), which takes
// techs from it (steal_tech(): tech_pick() builds its list on the game's
// heap even for the AI) and may put the target out of the game. While a
// check of probe() runs it is held: both runs see the base as it was,
// and the original's call is made after the check (in a sweep, dropped).
// Nothing probe() does after it reads what it changes but the treaties
// the aftermath looks at, which act_of_aggression() has broken by then.
static int holding_capture;
static void (*orig_capture_base)(int base, int faction, int probe);
static struct hook_stats stats_capture_base = {"(held capture_base)"};
static void replay_capture_base(const struct hook_ui_call *c) { orig_capture_base(c->arg[0], c->arg[1], c->arg[2]); }
static void shadow_capture_base(int base, int faction, int probe) {
    if (!holding_capture || !hooks_protecting()) {
        orig_capture_base(base, faction, probe);
        return;
    }
    stats_capture_base.calls++;
    struct hook_ui_call c = {.name = "capture_base", .replay = replay_capture_base, .arg = {base, faction, probe}};
    hooks_ui_hold(&c);
}

SHADOW_WRITER_CHECK(int, probe, (int a, int b, int c, int d), (a, b, c, d), orig_probe(a, b, c, d),
                    hooks_writes_game(&w_), "%d, %d, %d, %d", a, b, c, d)
static int checked_probe(int veh, int base, int tveh, int toggle) {
    holding_capture++;
    int r = check_probe(veh, base, tveh, toggle);
    holding_capture--;
    return r;
}
static int shadow_probe(int veh, int base, int tveh, int toggle) {
    if (hooks_nested) return orig_probe(veh, base, tveh, toggle);
    if (shadow_bypass()) return probe(veh, base, tveh, toggle);
    stats_probe.calls++;
    shadow_tick(PROBE);
    if (!probe_checkable(base)) return orig_probe(veh, base, tveh, toggle);
    return checked_probe(veh, base, tveh, toggle);
}

// ---- Sweep -------------------------------------------------------------------------
//
// Once a turn, on protected copies of the game: a unit of some faction
// (any unit: a team is whatever probe() is given) sent against a base or
// a unit of another, with the action stored on the unit, the factions'
// treaties, reserves, PROBE, plans and rankings, the target base's
// facilities and state, the projects that matter, the algorithmic
// enhancement and amphibious ability, captive leaders, and who is human
// varied, and rand() seeded anew for each.

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static inline int coin(uint32_t h, int bit, int n) { return (int)((h >> bit) % (uint32_t)n) == 0; }

static void case_begin(struct hook_writes *w) {
    *w = (struct hook_writes){.protect_only = 1};
    hooks_writes_game(w);
    hooks_writes_begin(w);
}

static int base_listed_of(int faction) {
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction && base_listed(b)) return 1;
    return 0;
}
static int base_of(int faction, uint32_t h) {
    int n = *game_base_count;
    for (int k = 0; k < n; k++) {
        int b = (int)((h + (uint32_t)k) % (uint32_t)n);
        if (game_bases[b].faction == faction) return b;
    }
    return -1;
}
static int target_base(int faction, uint32_t h) {
    int n = *game_base_count;
    for (int k = 0; k < n; k++) {
        int b = (int)((h + (uint32_t)k) % (uint32_t)n);
        if (game_bases[b].faction != faction && !base_listed(b)) return b;
    }
    return -1;
}
static int target_veh(int faction, uint32_t h) {
    int n = *game_veh_count;
    for (int k = 0; k < n; k++) {
        int v = (int)((h + (uint32_t)k) % (uint32_t)n);
        if (game_vehs[v].faction != faction) return v;
    }
    return -1;
}
static void flip_facility(int base, int fac) {
    int offset;
    uint32_t mask;
    bitmask(fac, &offset, &mask);
    game_bases[base].facilities_built[offset] ^= (uint8_t)mask;
}

// Captive leaders the target holds: factions other than the two, out of
// the game (their bases handed to the target, so none is destroyed when
// they land again), in the records the AI and the player read.
static void captives(int faction, int tfac, uint32_t h) {
    for (int i = 0; i < 7; i++) {
        uint32_t c = mix(h + (uint32_t)i);
        int l = i + 1;
        *captive(tfac, i, CAPTIVE_HELD) = 0;
        if (i < 6) *captive(tfac, i, CAPTIVE_FACTION) = l;
        if (!coin(c, 0, 3) || l == faction || l == tfac || l >= FACTION_COUNT || base_listed_of(l)) continue;
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == l) game_bases[b].faction = (uint8_t)tfac;
        *game_factions_alive &= (uint8_t)~(1u << l);
        *captive(tfac, i, CAPTIVE_HELD) = 1;
    }
}

static void sweep_case(int turn, int k) {
    uint32_t h = mix((uint32_t)turn * 0x9e3779b1u + (uint32_t)k), g = mix(h), s = mix(g), u = mix(s), x = mix(u);
    int veh = (int)(h % (uint32_t)*game_veh_count);
    int faction = game_vehs[veh].faction;
    if (!faction) return;
    int base = -1, tveh = -1;
    if (coin(g, 0, 4)) tveh = target_veh(faction, s);
    else base = target_base(faction, s);
    if (base < 0 && tveh < 0) return;
    if (!on_map(game_vehs[veh].x, game_vehs[veh].y) || (tveh >= 0 && !on_map(game_vehs[tveh].x, game_vehs[tveh].y))) return;
    int tfac = base >= 0 ? game_bases[base].faction : game_vehs[tveh].faction;
    struct hook_writes w;
    case_begin(&w);
    struct faction_data *F = &game_factions[faction], *TF = &game_factions[tfac];
    struct veh *V = &game_vehs[veh];

    // Who is human (never the probing faction while it is the player's),
    // and who the player is.
    uint8_t humans = 0;
    if (tfac && coin(u, 0, 3)) humans |= (uint8_t)(1u << tfac);
    if (coin(u, 2, 2)) humans |= (uint8_t)(1u << faction);
    if (coin(u, 4, 3)) {
        int r = 1 + (int)((u >> 6) % 7);
        humans |= (uint8_t)(1u << r);
        *game_ranked_faction = r;
    }
    *game_human_factions = humans;
    int cur = coin(u, 9, 3) ? tfac : coin(u, 11, 2) ? faction : 1 + (int)((u >> 13) % 7);
    if (cur == faction && (humans & (1u << faction))) cur = tfac;
    *game_current_faction = cur;
    *game_veh_attack_flags = coin(x, 0, 6) ? (int32_t)((x >> 3) & 1) : 2 | (int32_t)((x >> 4) & 1);
    int toggle = !coin(x, 5, 6);

    // The team: its stored choices, morale and abilities.
    V->unk_2c[0] = (uint8_t)(x >> 8);
    static const int sabs[] = {0, 98, 99, 4, 5, 2, 1, 26};
    V->unk_2c[1] = (uint8_t)(coin(x, 16, 3) ? (int)((x >> 18) % 70) : sabs[(x >> 18) % 8]);
    if (coin(x, 24, 2)) V->morale = (uint8_t)((x >> 25) % 7);
    struct unit_proto *P = &game_protos[V->proto];
    if (coin(s, 1, 4)) P->abilities ^= ABL_ALGO_ENHANCEMENT;
    if (coin(s, 3, 6)) P->abilities ^= ABL_AMPHIBIOUS_;
    if (coin(s, 5, 4)) P->abilities ^= ABL_AIR_SUPERIORITY_;
    if (tveh >= 0 && coin(s, 7, 4)) game_protos[game_vehs[tveh].proto].abilities ^= ABL_POLY_ENCRYPTION_;

    // Projects, PROBE, treaties, reserves, flags and rankings.
    if (coin(s, 9, 4)) game_secret_projects[SP_HUNTER_SEEKER] = coin(s, 11, 3) ? -1 : base_of(tfac, s);
    if (coin(s, 13, 4)) game_secret_projects[SP_NETHACK_TERMINUS] = coin(s, 15, 3) ? -1 : base_of(faction, s);
    if (coin(s, 17, 2)) TF->se[SE_PROBE] = (int32_t)((s >> 19) % 18) - 7;
    static const uint32_t ours[] = {TREATY_PACT, TREATY_TREATY, 0x4, TREATY_COMMLINK, TREATY_VENDETTA, TREATY_WANT_REVENGE,
                                    TREATY_INFILTRATOR, TREATY_UNK_1000000, TREATY_ATROCITY_VICTIM, TREATY_SHALL_BETRAY,
                                    TREATY_UNK_800};
    for (unsigned i = 0; i < sizeof ours / sizeof ours[0]; i++)
        if (coin(mix(g + i), 0, 4)) F->diplo_status[tfac] ^= ours[i];
    static const uint32_t theirs[] = {TREATY_PACT, TREATY_SURRENDERED_, TREATY_WANT_REVENGE, TREATY_TREATY, 0x4};
    for (unsigned i = 0; i < sizeof theirs / sizeof theirs[0]; i++)
        if (coin(mix(g ^ (0x100 + i)), 0, 4)) TF->diplo_status[faction] ^= theirs[i];
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint32_t c = mix(u + 0x200 + (uint32_t)i);
        if (coin(c, 0, 4)) TF->diplo_status[i] ^= TREATY_COMMLINK;
        if (coin(c, 2, 5)) TF->diplo_status[i] ^= TREATY_VENDETTA;
        if (coin(c, 4, 4)) F->diplo_status[i] ^= TREATY_COMMLINK;
        if (coin(c, 6, 6)) F->diplo_status[i] ^= TREATY_PACT;
        if (coin(c, 8, 6)) F->diplo_status[i] ^= TREATY_TREATY;
    }
    if (coin(x, 26, 2)) F->energy_credits = (int32_t)(mix(x) % 4000);
    if (coin(x, 27, 3)) TF->energy_credits = (int32_t)(mix(x + 1) % 3000);
    if (coin(g, 4, 3)) F->flags ^= FACTION_FLAG_MAP_STOLEN;
    if (coin(g, 6, 4)) F->flags ^= FACTION_FLAG_WANTON;
    if (coin(g, 8, 3)) TF->flags ^= FACTION_FLAG_PLAGUED;
    if (coin(g, 10, 3)) F->ai_power = (int32_t)((g >> 12) % 2), F->ai_wealth = (int32_t)((g >> 13) % 2);
    if (coin(g, 14, 3)) F->ai_fight = (int32_t)((g >> 16) % 3) - 1;
    if (coin(g, 18, 3)) TF->diff_level = (int32_t)((g >> 20) % 6);
    // Rankings from 1 (diplomacy_check() divides by them).
    if (coin(g, 23, 3)) F->ranking = 1 + (int32_t)((g >> 25) % 7), TF->ranking = 1 + (int32_t)((g >> 28) % 7);
    if (coin(h, 4, 3)) F->tech_ranking = (int32_t)((h >> 6) % 40), TF->tech_ranking = (int32_t)((h >> 12) % 40);
    if (coin(h, 18, 3)) *tech_accumulated(tfac) = (int32_t)((h >> 20) % 300), TF->tech_cost = (int32_t)((h >> 22) % 400);
    if (coin(h, 26, 3))
        *game_diplo_great_satan = (int32_t)((h >> 28) & 1), *game_diplo_fear = (int32_t)((h >> 29) & 1),
        *game_diplo_tribute = (int32_t)((h >> 30) & 1);
    if (coin(u, 16, 4)) *game_un_charter_a = (int32_t)((u >> 18) % 3), *game_un_charter_b = (int32_t)((u >> 20) % 3);
    if (coin(u, 22, 8)) *game_state ^= STATE_OMNISCIENT_;
    if (coin(u, 25, 4)) *game_state ^= STATE_RANKED_;
    if (coin(u, 27, 6)) *game_rules ^= RULES_NO_TECH_TRADING;
    if (coin(u, 29, 4)) *(int32_t *)((char *)game_rules_basic + RULES_STEAL_TECH) ^= 1;
    if (coin(x, 28, 4)) *game_expansion = !*game_expansion;
    // Genetic warfare known, or not.
    for (int t = 0; t < TECH_COUNT; t++)
        if ((game_tech[t].flags & TECH_FLAG_GENE_WARFARE) && coin(mix(h + 0x300 + (uint32_t)t), 0, 3))
            game_tech_achieved[t] ^= (uint8_t)(1u << faction);

    // The target base: its facilities, state, size and the plans for it.
    if (base >= 0) {
        struct base *B = &game_bases[base];
        static const int facs[] = {FAC_HEADQUARTERS, FAC_COVERT_OPS_CENTER_, FAC_GENEJACK_FACTORY, FAC_PERIMETER_DEFENSE_,
                                   FAC_TACHYON_FIELD_, FAC_CHILDREN_CRECHE, FAC_COMMAND_CENTER_, FAC_RESEARCH_HOSPITAL,
                                   FAC_NANOHOSPITAL, FAC_PRESSURE_DOME};
        for (unsigned i = 0; i < sizeof facs / sizeof facs[0]; i++)
            if (coin(mix(s + 0x400 + i), 0, 4)) flip_facility(base, facs[i]);
        if (coin(s, 21, 3)) B->state ^= BASE_STATE_DATA_STOLEN;
        if (coin(s, 23, 3)) B->state ^= BASE_STATE_DRAINED;
        if (coin(s, 25, 3)) B->state ^= BASE_STATE_PLAGUED;
        if (coin(s, 27, 3)) B->pop_size = (int8_t)(1 + (s >> 29) % 8 + (h >> 29));
        if (coin(h, 2, 3)) B->talent_total = B->drone_total;
        if (coin(x, 30, 2)) B->minerals = (int32_t)(mix(x + 2) % 200);
        int region = tile(B->x, B->y)->region;
        if (coin(h, 8, 2)) F->region_base_plan[region] = (uint8_t)((h >> 10) % 4);
        if (coin(h, 14, 2)) TF->region_base_plan[region] = (uint8_t)((h >> 16) % 4);
        if (coin(u, 30, 3)) captives(faction, tfac, mix(u));
    } else {
        struct veh *T = &game_vehs[tveh];
        if (T->home_base >= 0 && coin(s, 21, 4)) flip_facility(T->home_base, FAC_PUNISHMENT_SPHERE);
        if (coin(h, 8, 3)) F->region_total_bases[tile(T->x, T->y)->region] ^= 1;
    }
    *game_rand_state() = mix(x + 3);
    checked_probe(veh, base, tveh, toggle);
    hooks_writes_abandon(&w);
}

static void probe_sweep(void) {
    if (!*game_map || *game_veh_count <= 0 || *game_base_count <= 0) return;
    if (*game_net_multiplayer || *game_pbem) return;
    int sweeping = hooks_sweeping, turn = *game_turn;
    hooks_sweeping = 1;
    for (int k = 0; k < 24; k++) sweep_case(turn, k);
    hooks_sweeping = sweeping;
}

static const struct hook probe_hooks[] = {
    HOOK(0x59f120, probe, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // push ebp; mov ebp, esp; mov eax, fs:[0]
    {0x50c510, (const uint8_t[]){0x55, 0x8b, 0xec, 0x81, 0xec, 0x70, 0x01, 0x00, 0x00}, 9, NULL, (void *)shadow_capture_base,
     (void **)&orig_capture_base, &stats_capture_base, 1},  // push ebp; mov ebp, esp; sub esp, 0x170
};
struct hook_module probe_module = {"probe", probe_hooks, HOOK_COUNT(probe_hooks), probe_sweep, NULL, -1};
