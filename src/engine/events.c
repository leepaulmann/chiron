// What units find on the map: supply and unity pods (goody_box), monoliths,
// and the alien artifacts they bring home (study_artifact); the turn's
// random events (random_events); a faction's end (eliminate_player) and
// its social choices taking effect (social_set, social_select). Reimplemented
// from terranx.exe 2.0's disassembly; names follow Thinker (MIT) and
// OpenSMACX (Brendan Casey, GPL-3.0-or-later). Thinker's goody_box and
// monolith served as a map of the outcomes; where its versions differ from
// the original (options of its own, a fix or two) the original is followed,
// and the differences are noted below.
//
// The game's crash_landing() (the opening animation of the colony pod
// coming down, and the naming of the first base) is not here: it is all
// drawing, sound, frames timed on the clock and keyboard input, with
// buffers it allocates from the game's heap, and asks the player a
// question, so shadow mode could not check it.
#include "events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "score.h"
#include "shadow.h"
#include "social.h"
#include "spot.h"
#include "strategy.h"
#include "tech.h"
#include "veh.h"

enum {
    ALT_SHORE = 3 << 5, ALT_SHELF = 2 << 5, ALT_HILLS = 5 << 5,  // climate's altitude bits
    ORDER_MOVE_TO = 0x18,
    PLAN_TERRAFORM_ = 9, PLAN_ARTIFACT = 12,
    VEH_STATE_MONOLITH = 0x100,  // its morale raised at a monolith already
    MAP_RIVER_SOURCE = 0x100, MAP_RESOURCE_BONUS = 0x400, MAP_UNITY_ROVER = 0x4000000,
    MAP_MONOLITH_STAYS = 0x2000000,  // a monolith used before turn 100, or first used: it doesn't vanish yet
    BASE_STATE_ARTIFACT = 0x400, BASE_STATE_ARTIFACT_TOLD = 0x800,
    FACTION_FLAG_ARTIFACT_STUDIED = 0x10, FACTION_FLAG_ARTIFACTS_STUDIED = 0x4,
    STATE_MESSAGE_SHOWN = 0x4,  // game_state: a message shown this turn
    MORE_PREF_AUTO_MONOLITH = 0x800,  // the player's preference to use monoliths without asking
    // game_rules: scenario rules that switch outcomes off.
    RULES_FORCE_DIFFICULTY = 0x1000000, RULES_NO_NATIVE_LIFE = 0x200000, RULES_NO_MONOLITHS = 0x20000,
    RULES_NO_RESOURCES = 0x10000, RULES_NO_ARTIFACTS = 0x40000, RULES_NO_TECH = 0x4000000,
    MORE_RULES_NO_UNITS = 0x1, MORE_RULES_NO_TECH = 0x2, MORE_RULES_NO_SOCIAL = 0x8,
    STATE_ALL_ARTIFACTS = 0x40000,  // game_state: a scenario won by collecting every artifact
    // Basic designs a pod brings.
    PROTO_ARTIFACT = 7, PROTO_MIND_WORMS = 8, PROTO_ISLE = 9, PROTO_ROVER = 11, PROTO_CHOPPER = 12, PROTO_FOIL = 13,
    PROTO_SPORE_LAUNCHER = 15, PROTO_OGRE_1 = 16, PROTO_OGRE_2 = 17, PROTO_OGRE_3 = 18, PROTO_FUNGAL_TOWER = 19,
    // Techs: Orbital Spaceflight ends the rovers, Fossil Fuels brings the
    // chopper, Bioadaptive and Sentient Resonance the bigger ogres; a tech
    // that leads to Secrets of the Human Brain needs it known by someone.
    TECH_ORBITAL = 77, TECH_FOSSIL = 38, TECH_BRAIN = 48, TECH_BIOADAP = 82, TECH_SENT_RES = 83,
    SP_UNIVERSAL_TRANSLATOR = 20,
    POD_NEXT = -1, POD_DONE = -2,
};

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int alt_of(const struct map_tile *t) { return t->climate & 0xe0; }
static inline int sea_at(const struct map_tile *t) { return alt_of(t) < ALT_SHORE; }
static inline int fungus_on(const struct map_tile *t) { return (t->bits & MAP_FUNGUS) && alt_of(t) >= ALT_SHELF; }
// A unit or base of a faction on the square.
static inline int occupied(const struct map_tile *t) { return (t->bits & (MAP_BASE | MAP_UNIT)) && (t->owner_site & 0xf) < 8; }
static inline const struct unit_proto *proto_of(const struct veh *V) { return &game_protos[V->proto]; }
static inline int armed(const struct veh *V) { return game_weapons[proto_of(V)->weapon].offense != 0; }
static inline char *text(uint32_t addr) { return addr ? (char *)game_at(addr) : NULL; }
static inline int cur_faction(void) { return *game_current_faction; }
static void notice(char *label, int delay, uint32_t picture) {
    game_netmsg_pop(game_netmsg, label, delay, 0, text(picture));
}

// Counters kept in a part of the faction's data game.h leaves as padding
// (Thinker's names): techs earned towards the next, and pods opened,
// artifacts, earthquakes and techs they gave.
enum { F_TECH_ACCUMULATED = 0x3c8, F_GOODY_OPENED = 0x2040, F_GOODY_ARTIFACT = 0x2044, F_GOODY_EARTHQUAKE = 0x2048,
       F_GOODY_TECH = 0x204c };
static inline int32_t *faction_word(int faction, int offset) { return (int32_t *)((char *)&game_factions[faction] + offset); }

// ---- Monoliths -------------------------------------------------------------------

// Every faction that has seen the monolith there is reminded of it.
static void monolith_sites(int x, int y) {
    const struct map_tile *t = tile(x, y);
    for (int i = 1; i < 8; i++)
        if (t->bits_seen[i - 1] & MAP_MONOLITH) add_site(i, 0, 2, x, y);
}
static void monolith_notice(uint32_t label, uint32_t picture) {
    notice(text(label), 5000, picture);
    *game_state |= STATE_MESSAGE_SHOWN;
}

// A unit at a monolith. Battle ogres are left alone; the player's unit
// that only passes by on its way elsewhere too. An unarmed unit is healed,
// or once in its life told what the monolith is. An armed one is healed
// and has its morale raised by a level, once in its life and up to elite;
// a monolith may then vanish (one in 32, from turn 100 on, if it has been
// used before). A human is asked first unless a preference says not to; the
// AI uses a monolith only for the morale (healing on the way).
void monolith(int veh) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, x = V->x, y = V->y;
    if (V->proto == PROTO_OGRE_1 || V->proto == PROTO_OGRE_2 || V->proto == PROTO_OGRE_3) return;
    if (faction == cur_faction() && !*game_net_multiplayer) {
        if (V->order == ORDER_MOVE_TO && V->waypoint_x[0] == x && V->waypoint_y[0] == y) V->order = 0;
        if (V->order) return;
    }
    if (!armed(V)) {
        if (V->damage) {
            V->damage = 0;
            veh_skip(veh);
            game_draw_tile(x, y, 2);
            if (faction == cur_faction()) monolith_notice(0x68eb54, 0x68eb44);
        } else if (!(V->state & VEH_STATE_MONOLITH)) {
            V->state |= VEH_STATE_MONOLITH;
            if (faction == cur_faction() && !(*game_more_preferences & MORE_PREF_AUTO_MONOLITH) && !*game_net_multiplayer) {
                game_netmsg_pop(game_netmsg, text(0x68eb74), -5000, 0, text(0x68eb64));
                *game_state |= STATE_MESSAGE_SHOWN;
            }
        }
        return;
    }
    for (int i = 1; i < 8; i++) del_site(i, 0, x, y, 0);
    if (game_is_human(faction)) {
        if (!*game_net_multiplayer && !(*game_more_preferences & MORE_PREF_AUTO_MONOLITH)) {
            game_fx_play(game_fx, 6);
            int answer = game_popp(*game_script_file, text(0x68eb90), 0, text(0x68eb80), 0);
            game_fx_stop(game_fx, 6);
            if (answer == 2) {  // always, from now on
                *game_more_preferences |= MORE_PREF_AUTO_MONOLITH;
                game_prefs_save(0);
            } else if (!answer) {
                monolith_sites(x, y);
                return;
            }
        }
    } else if ((V->state & VEH_STATE_MONOLITH) || morale_veh(veh, 1, 0) >= 6 || V->morale >= 6 || !armed(V)) {
        if (V->damage) {
            V->damage = 0;
            veh_skip(veh);
            game_draw_tile(x, y, 2);
        }
        monolith_sites(x, y);
        return;
    }
    int auto_ = *game_more_preferences & MORE_PREF_AUTO_MONOLITH;
    if (V->damage) {
        V->damage = 0;
        veh_skip(veh);
        game_draw_tile(x, y, 2);
        if ((V->state & VEH_STATE_MONOLITH) || morale_veh(veh, 1, 0) >= 6) {
            if (faction != cur_faction()) return;
            if (auto_) monolith_notice(0x68ebac, 0x68eb9c);
            else {
                game_netmsg_pop(game_netmsg, text(0x68ebcc), -5000, 0, text(0x68ebbc));
                *game_state |= STATE_MESSAGE_SHOWN;
            }
            return;
        }
    }
    if ((V->state & VEH_STATE_MONOLITH) || morale_veh(veh, 1, 0) >= 6) {
        if (faction != cur_faction()) return;
        if (*game_more_preferences & MORE_PREF_AUTO_MONOLITH)
            game_netmsg_pop(game_netmsg, text(0x68ebec), 5000, 0, text(0x68ebdc));
        else game_netmsg_pop(game_netmsg, text(0x68ec08), -5000, 0, text(0x68ebf8));
        return;
    }
    int stays = game_rand() % 32;
    if (!(tile(x, y)->bits & MAP_MONOLITH_STAYS) || *game_turn < 100) {
        stays = 1;
        bit_set(x, y, MAP_MONOLITH_STAYS, 1);
    }
    V->state |= VEH_STATE_MONOLITH;
    int morale = V->morale + 1;
    V->morale = (uint8_t)(morale > 6 ? 6 : morale);
    veh_skip(veh);
    // The new morale, in lower case: the original lowers the text variable
    // parse_says() has just filled.
    game_battle_text[0] = 0;
    game_say_morale_2(veh, 0);
    game_parse_says(0, game_battle_text, -1, -1);
    (*game_char_lower)(game_parse_text);
    if (stays) monolith_sites(x, y);
    else {
        bit_set(x, y, MAP_MONOLITH, 0);
        synch_bit(x, y, faction);
    }
    game_draw_tile(x, y, 2);
    if (faction != cur_faction()) return;
    game_fx_play(game_fx, 0x22);
    game_battle_text[0] = 0;
    strcat(game_battle_text, text(0x68ec1c));  // "MONOLITH", and 1 if it stays or 0
    strcat(game_battle_text, stays ? "1" : "0");
    if (*game_more_preferences & MORE_PREF_AUTO_MONOLITH)
        game_netmsg_pop(game_netmsg, game_battle_text, 5000, 0, text(0x68ec28));
    else game_netmsg_pop(game_netmsg, game_battle_text, -5000, 0, text(0x68ec38));
    *game_state |= STATE_MESSAGE_SHOWN;
}

// ---- Alien artifacts ----------------------------------------------------------------

// An artifact brought to a base (or a network node). A base already linked
// to one (but the one with the Universal Translator) takes no more, and its
// owner is told so once. Otherwise the artifact is used up, the faction
// marked as having studied one (and then two), the base as linked, and a
// tech it could research is won: among those whose prerequisites it has
// both of, or one of where a leap is allowed (weighed by its ranking), the
// one with the highest roll. Thinker leaves out the second kind as never
// possible; the original asks.
int study_artifact(int veh) {
    struct veh *V = &game_vehs[veh];
    int x = V->x, y = V->y, faction = V->faction;
    char label[96];  // the original copies the message's label first
    label[0] = 0;
    strcat(label, text(0x68f0a0));
    int base = base_at(x, y);
    if (base != game_secret_projects[SP_UNIVERSAL_TRANSLATOR] && base >= 0) {
        struct base *B = &game_bases[base];
        if (B->state & BASE_STATE_ARTIFACT) {
            if (!(B->state & BASE_STATE_ARTIFACT_TOLD) && faction == cur_faction()) {
                game_parse_says(0, B->name, -1, -1);
                game_netmsg_pop(game_netmsg, text(0x68f0c0), -5000, 0, text(0x68f0b0));
                B->state |= BASE_STATE_ARTIFACT_TOLD;
            }
            return 0;
        }
    }
    veh_kill(veh);
    game_draw_tile(x, y, 2);
    if (faction == cur_faction()) {
        game_fx_play(game_fx, 0x23);
        game_popp(*game_script_file, label, 0, text(0x68f0d0), 0);
    }
    struct faction_data *F = &game_factions[faction];
    if (F->flags & FACTION_FLAG_ARTIFACT_STUDIED) F->flags |= FACTION_FLAG_ARTIFACTS_STUDIED;
    else F->flags |= FACTION_FLAG_ARTIFACT_STUDIED;
    if (base >= 0) game_bases[base].state |= BASE_STATE_ARTIFACT;
    int best = -1, pick = -1;
    for (int t = 0; t < TECH_COUNT; t++) {
        if (has_tech(t, faction) || !tech_avail((uint32_t)t, faction)) continue;
        int first = has_tech(game_tech[t].preq1, faction), second = has_tech(game_tech[t].preq2, faction);
        if (!first && !second) continue;
        int range = 8;
        if (!first || !second) {
            if (!valid_tech_leap(t, faction)) continue;
            range = (11 - F->ranking) * 2;
        }
        int roll = game_rnd(range);
        if (roll > best) best = roll, pick = t;
    }
    if (pick >= 0) game_tech_achieve(faction, pick, 0, 0);
    return 1;
}

// ---- Supply and unity pods ------------------------------------------------------------
//
// goody_box() picks an outcome and tries it; an outcome that can't happen
// here hands on to another (a fixed one, or a new roll). Each outcome below
// returns POD_DONE, POD_NEXT for a new roll, or the outcome to try instead.

struct pod {
    int veh, faction, x, y;
    int sea;      // the square was sea when the pod was opened
    int player;   // the faction is the one at this console
    int landing;  // one of the unity pods around the landing site
    int flag;     // 0 for the first pick, 1 after a new roll, 2 once a resource was found, 3 after that
    int event;
};

static inline struct faction_data *pod_faction(const struct pod *p) { return &game_factions[p->faction]; }
static inline int pod_plan(const struct pod *p) { return proto_of(&game_vehs[p->veh])->plan; }
// A square of the radius table around the pod, if on the map.
static int pod_square(const struct pod *p, int i, int *x, int *y) {
    *x = xrange(p->x + game_radius_x[i]), *y = p->y + game_radius_y[i];
    return on_map(*x, *y);
}
static int near_square(const struct pod *p, int i, int *x, int *y) {
    *x = xrange(p->x + game_radius_near_x[i]), *y = p->y + game_radius_near_y[i];
    return on_map(*x, *y);
}
static inline int difficulty_forced_low(void) { return (*game_rules & RULES_FORCE_DIFFICULTY) && *game_difficulty <= 1; }

// A new roll: back on the unit's square, an outcome of 0 to 13, natives
// more often at the poles and a clone now and then. After a resource was
// found it is the last try; returns -1 for none.
static int pod_next(struct pod *p) {
    const struct veh *V = &game_vehs[p->veh];
    p->x = V->x, p->y = V->y;
    int event = game_rand() % 14;
    if (!p->flag) p->flag = 1;
    else if (p->flag > 2) return -1;
    else if (p->flag == 2) p->flag = 3;
    if ((!p->y || p->y == *game_map_height - 1) && !(game_rand() % 3)) event = 12;
    if (!(game_rand() % 100)) event = 18;
    return event;
}

// 0: energy credits, more at sea and later in the game.
static int pod_energy(struct pod *p) {
    int credits = (game_rnd(p->sea + 3) + 1) * (*game_turn >= (p->sea ? 100 : 50) ? 50 : 25);
    pod_faction(p)->energy_credits += credits;
    if (p->player) {
        game_parse_num(0, credits);
        game_fx_play(game_fx, 0x16);
        game_netmsg_pop(game_netmsg, text(p->sea ? 0x68ec84 : 0x68ec94), -5000, 0, text(p->sea ? 0 : 0x68ec74));
        game_fx_fade(game_fx, 0x16);
        // The original then redraws the info window, whose method is empty.
    }
    return POD_DONE;
}

// 1: a river springs up, on land away from rivers and the sea.
static int pod_river(struct pod *p) {
    if (p->sea || (tile(p->x, p->y)->bits & MAP_RIVER)) return POD_NEXT;
    for (int i = 0; i < 9; i++) {
        int x, y;
        if (!pod_square(p, i, &x, &y)) continue;
        const struct map_tile *t = tile(x, y);
        if ((t->bits & MAP_RIVER) || sea_at(t)) return POD_NEXT;
    }
    if (p->player) {
        game_fx_play(game_fx, 0x26);
        notice(text(0x68ecb4), 5000, 0x68eca4);
    }
    bit_set(p->x, p->y, MAP_RIVER_SOURCE, 1);
    world_climate();
    game_draw_map(1);
    return POD_DONE;
}

// 2: a view from a height (or by sonar at sea) of the squares around; on
// lower land early in the game a monolith instead.
static int pod_view(struct pod *p) {
    if (!p->sea && alt_of(tile(p->x, p->y)) < ALT_HILLS) return *game_turn >= 50 ? POD_NEXT : 8;
    for (int i = 0; i < 121; i++) {
        int x, y;
        if (!pod_square(p, i, &x, &y)) continue;
        if (p->sea) {
            const struct map_tile *t = tile(x, y);
            if (!sea_at(t) || t->region != tile(p->x, p->y)->region) continue;
        }
        spot_loc(x, y, p->faction);
    }
    if (p->player) {
        game_fx_play(game_fx, 0x16);
        notice(text(p->sea ? 0x68ecd0 : 0x68ecdc), 5000, 0x68ecc0);
        game_fx_fade(game_fx, 0x16);
        game_draw_map(1);
    }
    return POD_DONE;
}

// 3: an earthquake raises the land by two levels and breaks roads, at most
// rarer each time for the faction; only with two bases or more.
static int pod_quake(struct pod *p) {
    struct faction_data *F = pod_faction(p);
    if (difficulty_forced_low() || alt_of(tile(p->x, p->y)) >= ALT_HILLS || F->base_count < 2 || p->sea ||
        *game_net_multiplayer)
        return POD_NEXT;
    int32_t *quakes = faction_word(p->faction, F_GOODY_EARTHQUAKE);
    if (game_rnd(*quakes * 2 + 2)) return POD_NEXT;
    ++*quakes;
    game_fx_play(game_fx, 8);
    world_raise_alt(p->x, p->y);
    world_raise_alt(p->x, p->y);
    for (int i = 0; i < 25; i++) {
        int x, y;
        if (!pod_square(p, i, &x, &y)) continue;
        if ((tile(x, y)->bits & MAP_ROAD) && !(game_rand() % 3)) {
            bit_set(x, y, MAP_ROAD, 0);
            synch_bit(x, y, p->faction);
        }
        if (i < 9) rocky_set(x, y, minerals_at(x, y));
    }
    world_climate();
    game_draw_map(1);
    if (p->player) {
        game_netmsg_pop(game_netmsg, text(0x68ed0c), -5000, 0, text(0x68ecfc));
        if (game_tut_check(0x800))
            game_tut_map(game_tut_win, text(0x68ed18), p->x, p->y, -1, NULL, 0, -1, -1);
    }
    return POD_DONE;
}

// 4: a gate takes the unit (and its stack) to an unseen square of the same
// region, 5 to 24 squares away (8 for formers, transports and ships).
// During perihelion on land, native life instead; early in the game a
// monolith.
static int pod_gate(struct pod *p) {
    struct veh *V = &game_vehs[p->veh];
    if (!p->sea && (*game_state & GAME_STATE_PERIHELION)) return 7;
    if (*game_net_multiplayer || pod_plan(p) == PLAN_ARTIFACT || difficulty_forced_low()) return POD_NEXT;
    if (*game_turn < 25 || (*game_turn < 50 && *game_difficulty < 2)) return 8;
    int region = tile(p->x, p->y)->region, tries = 0, reach;
    if (game_chassis[proto_of(V)->chassis].triad == TRIAD_LAND && pod_plan(p) != PLAN_COLONY)
        reach = pod_plan(p) != PLAN_TERRAFORM_ ? 24 : 8;
    else if (game_rand() % 2) reach = 8;
    else return POD_NEXT;
    int x, y;
    for (;;) {
        int rx = game_rnd(*game_map_width);
        y = game_rnd(*game_map_height);
        x = rx & 1 ? rx - 1 : rx;
        if (y & 1) x++;
        if (++tries >= 1000) return POD_NEXT;
        const struct map_tile *t = tile(x, y);
        if (sea_at(t) != p->sea || t->region != region || occupied(t)) continue;
        if (zoc_any(x, y, p->faction)) continue;
        if (pod_faction(p)->flags & FACTION_FLAG_MAP_REVEALED) continue;
        if (tile(x, y)->visibility & (1u << p->faction)) continue;
        int d = x_dist(p->x, x) + abs(p->y - y);
        if ((d & ~1) <= 8 || d >> 1 > reach) continue;
        break;
    }
    if (p->player)
        game_netmsg_pop(game_netmsg, text(p->sea ? 0x68ed50 : 0x68ed5c), -5000, 0, text(p->sea ? 0 : 0x68ed40));
    V->moves_spent = 0;
    V->order = 0;
    V->state &= 0xfcfffdffu;
    stack_put(stack_veh(p->veh, 1), x, y);
    if (V->faction == cur_faction()) game_draw_tile_fixup(p->x, p->y, has_abil(V->proto, 2), 2);
    else game_draw_tile(p->x, p->y, 2);
    if (p->player) {
        game_console_focus(game_console, x, y, p->faction);
        game_graphic_win_redraw(game_world_win);
    }
    spot_all(p->veh, 1);
    return POD_DONE;
}

// 6 (and 5 when nothing is being built): an alien artifact, while the
// faction has fewer than it has bases (its own and those linked to one);
// at sea only for a transport with room. Never carried by an artifact.
static int pod_artifact(struct pod *p, int event) {
    if (*game_rules & RULES_NO_ARTIFACTS) return POD_NEXT;
    if (p->flag > 1 && event == 6) return 5;
    if (p->sea && stack_check(p->veh, 8, -1, -1, -1) <= 0) return POD_NEXT;
    int32_t *found = faction_word(p->faction, F_GOODY_ARTIFACT);
    if (*game_net_multiplayer && !(*game_state & STATE_ALL_ARTIFACTS))
        for (int i = 1; i < 8; i++)
            if (i != p->faction && game_is_human(i) && *found > *faction_word(i, F_GOODY_ARTIFACT)) return POD_NEXT;
    ++*found;
    if (pod_plan(p) == PLAN_ARTIFACT) return POD_NEXT;
    int a = veh_init(PROTO_ARTIFACT, p->faction, p->x, p->y);
    if (a < 0) return POD_DONE;
    veh_skip(a);
    game_draw_tile(p->x, p->y, 2);
    if (p->player) {
        if (p->sea) game_netmsg_pop(game_netmsg, text(0x68edb0), -5000, 0, text(0x68eda0));
        else game_netmsg_pop(game_netmsg, text(0x68edd0), -5000, 0, text(0x68edc0));
        game_fx_fade(game_fx, 0x1b);
    }
    return POD_DONE;
}

// 5: the production of the nearest own base is completed, if it is under
// three quarters done (but not a colony pod from a base of 1); or else an
// artifact. During perihelion on land, native life instead.
static int pod_build(struct pod *p) {
    if ((*game_state & GAME_STATE_PERIHELION) && !p->sea) return 7;
    int b = base_find_2(p->x, p->y, p->faction);
    if (!*game_net_multiplayer && b >= 0 && game_bases[b].queue[0] > -70) {
        struct base *B = &game_bases[b];
        int item = B->queue[0], cost;
        if (item >= 0) {
            if (game_protos[item].plan == PLAN_COLONY && B->pop_size == 1) return POD_NEXT;
            cost = veh_cost(item, b, NULL);
            game_parse_says(0, game_protos[item].name, -1, -1);
        } else {
            cost = game_facilities[-item].cost;
            game_parse_say(0, (int)(intptr_t)game_facilities[-item].name, -1, -1);
        }
        int minerals = cost_factor(p->faction, 1, -1) * cost;
        if (B->minerals < minerals * 3 / 4) {
            B->minerals = minerals;
            if (p->player) {
                game_parse_says(1, B->name, -1, -1);
                if (p->sea) game_netmsg_pop(game_netmsg, text(0x68ed78), -5000, 0, text(0x68ed68));
                else game_netmsg_pop(game_netmsg, text(0x68ed94), -5000, 0, text(0x68ed84));
            }
            return POD_DONE;
        }
    }
    if (p->sea || (*game_rules & RULES_NO_ARTIFACTS)) return POD_NEXT;
    const struct faction_data *F = pod_faction(p);
    int artifacts = F->units_active[PROTO_ARTIFACT];
    for (int i = 0; i < *game_base_count; i++)
        if (game_bases[i].faction == p->faction && (game_bases[i].state & BASE_STATE_ARTIFACT)) artifacts++;
    if (artifacts >= F->base_count && !(*game_state & STATE_ALL_ARTIFACTS)) return POD_NEXT;
    return pod_artifact(p, 5);
}

// 7: fungus spreads over the land around, clearing what was built there,
// with a fungal tower (Alien Crossfire) one time in two. At sea, a
// monolith is tried instead.
static int pod_fungus(struct pod *p) {
    if ((*game_rules & RULES_NO_NATIVE_LIFE) || difficulty_forced_low() || p->flag > 1) return POD_NEXT;
    if (p->sea) return 8;
    base_find(p->x, p->y);
    if (*game_base_find_dist < 3) return POD_NEXT;
    if (*game_net_multiplayer && p->faction == game_ranked_faction[1] && !(game_rand() % 2)) return POD_NEXT;
    int tower = 0, want = 0;
    if (game_rand() % 2 && *game_expansion) want = 1;
    if (p->player) {
        game_fx_play(game_fx, 9);
        game_netmsg_pop(game_netmsg, text(0x68edfc), -5000, 0, text(0x68edf0));
    }
    site_radius(p->x, p->y, 0);
    for (int i = 0; i < 9; i++) {
        int x, y;
        if (!pod_square(p, i, &x, &y) || alt_of(tile(x, y)) < ALT_SHELF) continue;
        bit_set(x, y, MAP_FUNGUS, 1);
        bit_set(x, y, 0x28805c, 0);  // forest, soil enricher, farm, solar, mine, road and tube
        synch_bit(x, y, p->faction);
        if (!tower && want == 1 && game_rand() % 2) {
            const struct map_tile *t = tile(x, y);
            if (!occupied(t) && *game_expansion && !sea_at(t)) {
                veh_init(PROTO_FUNGAL_TOWER, 0, x, y);
                want = 0, tower = 1;
            }
        }
        game_draw_tile(x, y, 2);
    }
    return POD_DONE;
}

// 8: a monolith rises (on land with no resource), and the unit uses it.
static int pod_monolith(struct pod *p) {
    if ((*game_rules & RULES_NO_MONOLITHS) || bonus_at(p->x, p->y, 0) || p->sea) return POD_NEXT;
    site_radius(p->x, p->y, 0);
    bit_set(p->x, p->y, MAP_MONOLITH, 1);
    bit_set(p->x, p->y, MAP_FUNGUS, 0);
    synch_bit(p->x, p->y, p->faction);
    game_draw_tiles(p->x, p->y, 2);
    monolith(p->veh);
    return POD_DONE;
}

// 9 (and the rover pods of the landing site): a unity rover, a scout
// chopper once someone has Fossil Fuels and a base is near, a foil by or at
// sea; until Orbital Spaceflight. With enough of them already, native life
// instead. A rover may be a battle ogre (Alien Crossfire), stronger once
// someone has Bioadaptive Resonance or Sentient Resonance.
static int pod_unit(struct pod *p) {
    struct faction_data *F = pod_faction(p);
    if ((*game_more_rules & MORE_RULES_NO_UNITS) || game_tech_achieved[TECH_ORBITAL]) return POD_NEXT;
    if (p->sea && stack_check(p->veh, 8, -1, -1, -1) <= 0) return POD_NEXT;
    int unit = PROTO_ROVER;
    if (game_tech_achieved[TECH_FOSSIL] && !(game_rand() % 3) && !F->units_active[PROTO_CHOPPER]) {
        base_find_2(p->x, p->y, p->faction);
        if (*game_base_find_dist <= speed_proto(PROTO_CHOPPER) * 3) unit = PROTO_CHOPPER;
    }
    int foils = F->units_active[PROTO_FOIL];
    int n = foils + 3;
    if (game_continents[tile(p->x, p->y)->region].tile_count <= 50) n--;
    if (foils < 2 && !game_rnd(n)) {
        if (p->sea) unit = PROTO_FOIL;
        else
            for (int i = 0; i < 8; i++) {
                int x, y;
                if (!near_square(p, i, &x, &y)) continue;
                const struct map_tile *t = tile(x, y);
                if (sea_at(t) && game_continents[t->region].tile_count >= 80) {
                    p->x = x, p->y = y, unit = PROTO_FOIL;
                    break;
                }
            }
    }
    int have = F->units_active[unit];
    if (have) {
        int pods = F->units_active[PROTO_ROVER] + F->units_active[PROTO_CHOPPER] + F->units_active[PROTO_FOIL];
        if (pods > (F->base_count + 1) / 2 || have > 1) {
            int event = game_rand() % 2 + 12;
            p->x = game_vehs[p->veh].x, p->y = game_vehs[p->veh].y;
            return event;
        }
    }
    if (unit == PROTO_ROVER && !(game_rand() % 5) && *game_expansion) {
        int level = 0;
        for (int i = 1; i < 8; i++) {
            if (has_tech(TECH_BIOADAP, i) && level < 1) level = 1;
            if (has_tech(TECH_SENT_RES, i)) level = 2;
        }
        unit = level > 1 ? PROTO_OGRE_3 : level > 0 ? PROTO_OGRE_2 : PROTO_OGRE_1;
    }
    int v = veh_init(unit, p->faction, p->x, p->y);
    if (v < 0) return POD_NEXT;
    struct veh *N = &game_vehs[v];
    if (p->landing || *game_turn < 50) N->home_base = -1;
    N->morale = unit == PROTO_OGRE_2 ? 4 : unit == PROTO_OGRE_3 ? 6 : 2;
    if (p->sea && game_chassis[proto_of(N)->chassis].triad == TRIAD_LAND) veh_sleep(v);
    if (p->player) {
        game_parse_says(0, game_protos[unit].name, -1, -1);
        int ogre = unit == PROTO_OGRE_1 || unit == PROTO_OGRE_2 || unit == PROTO_OGRE_3;
        if (ogre) game_netmsg_pop(game_netmsg, text(p->sea ? 0x68ee30 : 0x68ee44), p->sea ? 5000 : -5000, 0,
                                  text(p->sea ? 0x68ee24 : 0x68ee38));
        else game_netmsg_pop(game_netmsg, text(p->sea ? 0x68ee5c : 0x68ee78), p->sea ? 5000 : -5000, 0,
                             text(p->sea ? 0x68ee4c : 0x68ee68));
    }
    return POD_DONE;
}

// The leader's title and name and the faction's noun, for a message.
static void say_faction(int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(0, (char *)d->leader_title, -1, -1);
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(1, (char *)d->leader_name, -1, -1);
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(2, (char *)d->noun, -1, -1);
}

// 10 (and the other pods of the landing site): a human may first make
// contact with an AI faction it hasn't met; else a tech from early in the
// tree (both prerequisites none or first-level), but none of five Alien
// Crossfire techs, nor one leading to the Human Brain before someone knows
// it.
static int pod_tech(struct pod *p) {
    struct faction_data *F = pod_faction(p);
    if (*game_more_rules & MORE_RULES_NO_TECH) return POD_NEXT;
    if (!(game_rand() % 4)) {
        int other = game_rand() % 7 + 1;
        if (other != p->faction && !(F->diplo_status[other] & TREATY_COMMLINK) && (*game_factions_alive & (1u << other)) &&
            !game_is_human(other) && game_is_human(p->faction) &&
            !(game_faction_defs[other].rule_flags & RULE_FLAG_ALIEN)) {
            treaty_on(p->faction, other, TREATY_COMMLINK);
            if (p->player) {
                say_faction(other);
                if (p->sea) game_netmsg_pop(game_netmsg, text(0x68eeac), 5000, 0, text(0x68ee9c));
                else game_netmsg_pop(game_netmsg, text(0x68eec8), -5000, 0, text(0x68eeb8));
            }
            return POD_DONE;
        }
    }
    if (*game_rules & RULES_NO_TECH) return POD_NEXT;
    int32_t *given = faction_word(p->faction, F_GOODY_TECH);
    if (*game_net_multiplayer && !p->landing)
        for (int i = 1; i < 8; i++)
            if (i != p->faction && game_is_human(i) && *given > *faction_word(i, F_GOODY_TECH) + 1 &&
                (F->ranking > game_factions[i].ranking || F->tech_ranking > game_factions[i].tech_ranking))
                return POD_NEXT;
    int start = game_rand() % TECH_COUNT, tech = -1;
    for (int k = 0; k < TECH_COUNT && tech < 0; k++) {
        int t = (start + k) % TECH_COUNT;
        if (has_tech(t, p->faction) || !tech_avail((uint32_t)t, p->faction)) continue;
        if (tech_is_preq(t, TECH_BRAIN, 9999) && !game_tech_achieved[TECH_BRAIN]) continue;
        int early = 0;
        for (int j = 0; j < 2; j++) {
            int q = j ? game_tech[t].preq2 : game_tech[t].preq1;
            if (q == TECH_NONE) early++;
            if (q >= 0 && game_tech[q].preq1 == TECH_NONE && game_tech[q].preq2 == TECH_NONE) {
                early++;
                break;
            }
        }
        if (early >= 2) tech = t;
    }
    if (tech < 0 || tech == 78 || tech == 79 || tech == 81 || tech == 82 || tech == 83) return POD_NEXT;
    if (!p->landing) ++*given;
    if (p->player) {
        game_battle_text[0] = 0;
        strcat(game_battle_text, text(0x68eee0));
        if (*game_net_multiplayer) {
            strcat(game_battle_text, text(0x68eeec));
            game_parse_say(0, (int)(intptr_t)game_tech[tech].name, -1, -1);
        }
        if (p->sea) game_netmsg_pop(game_netmsg, game_battle_text, 5000, 0, text(0x68eef0));
        else game_netmsg_pop(game_netmsg, game_battle_text, -5000, 0, text(0x68ef00));
    }
    game_tech_achieve(p->faction, tech, 0, 0);
    if (p->landing) F->tech_ranking--;
    return POD_DONE;
}

// 11: the land around is improved, all one way (forest, farms, mines and
// roads on rolling or rocky land, or solar collectors), or kelp at sea;
// the square itself always and others two times in three (one in two for
// mines and solar). Away from bases; on shallow sea or land, else natives.
static int pod_terrain(struct pod *p) {
    const struct map_tile *t = tile(p->x, p->y);
    if (game_bad_region(t->region)) return POD_NEXT;
    if (!p->sea && (t->rocky_lock & 0xc0) > 0x40) return POD_NEXT;
    if (alt_of(t) < ALT_SHELF) return 12;
    int choice = 0;
    if (!p->sea) {
        base_find(p->x, p->y);
        if (*game_base_find_dist < 3) return POD_NEXT;
        choice = game_rand() % 4;
    }
    site_radius(p->x, p->y, 0);
    int done = 0;
    for (int i = 0; i < 9; i++) {
        if (i && !game_rnd(3 - (choice > 1))) continue;
        int x, y;
        if (!pod_square(p, i, &x, &y)) continue;
        const struct map_tile *s = tile(x, y);
        if (i && (s->bits & MAP_POD_TAKEN)) continue;
        if (!p->sea) {
            if (sea_at(s)) continue;
            int rocky = s->rocky_lock & 0xc0;
            if (rocky > 0x40 && choice < 2) continue;
            if (!rocky && choice == 2) continue;
        } else {
            if (alt_of(s) >= ALT_SHORE) continue;
            if (alt_of(s) < ALT_SHELF) {
                if (i) continue;
                alt_set_both(x, y, 2);
            }
        }
        bit_set(x, y, 0x288070, 0);  // forest, soil enricher, farm, solar, fungus and mine
        if (p->sea) bit_set(x, y, MAP_FARM, 1);
        else
            switch (choice) {
            case 0:
                bit_set(x, y, MAP_FOREST, 1);
                bit_set(x, y, MAP_FARM | MAP_SOLAR | MAP_MINE, 0);
                break;
            case 1:
                bit_set(x, y, MAP_FARM, 1);
                bit_set(x, y, MAP_FOREST, 0);
                break;
            case 2:
                bit_set(x, y, MAP_MINE | MAP_ROAD, 1);
                bit_set(x, y, MAP_FOREST | MAP_SOLAR, 0);
                break;
            case 3:
                bit_set(x, y, MAP_SOLAR, 1);
                bit_set(x, y, MAP_FOREST | MAP_MINE, 0);
                break;
            }
        synch_bit(x, y, p->faction);
        game_draw_tile(x, y, 2);
        done++;
    }
    if (!done) return POD_NEXT;
    if (p->player) {
        static const uint32_t labels[4][2] = {
            {0x68ef54, 0x68ef44}, {0x68ef70, 0x68ef60}, {0x68ef8c, 0x68ef7c}, {0x68efa8, 0x68ef98}};
        if (p->sea) game_netmsg_pop(game_netmsg, text(0x68ef38), -5000, 0, text(0x68ef2c));
        else game_netmsg_pop(game_netmsg, text(labels[choice][0]), -5000, 0, text(labels[choice][1]));
    }
    return POD_DONE;
}

// 12 to 17: native life (mind worms, or spore launchers in Alien
// Crossfire; an Isle of the Deep with worms aboard at sea) on the squares
// next to the pod, away from bases, the higher outcomes only farther from
// the faction's bases (nearer during perihelion and with more native
// life). At sea only for 12 (13 in perihelion), and the unit loses moves.
static int pod_natives(struct pod *p, int event) {
    struct faction_data *F = pod_faction(p);
    int spores = 0;
    if ((*game_rules & RULES_NO_NATIVE_LIFE) || difficulty_forced_low()) return POD_NEXT;
    base_find_2(p->x, p->y, p->faction);
    int own = *game_base_find_dist;
    base_find(p->x, p->y);
    if (*game_base_find_dist <= 1) return POD_NEXT;
    int perihelion = (*game_state & GAME_STATE_PERIHELION) != 0;
    if (event > 13) {
        int per = fungus_on(tile(p->x, p->y)) ? 2 : 4;
        if ((perihelion + 1) * own < per * (event - 13) + (4 - *game_native_life) * 2) return POD_NEXT;
    }
    if (*game_net_multiplayer && p->faction == game_ranked_faction[1] && !(game_rand() % 2)) return POD_NEXT;
    if (p->sea) {
        if (event > 12 + perihelion) return POD_NEXT;
        struct veh *V = &game_vehs[p->veh];
        V->moves_spent = (uint8_t)(V->moves_spent + (uint8_t)game_rules_basic->move_rate_roads);
    }
    int spawned = 0;
    for (int k = 0; k < 8;) {
        int j = (*game_turn + k) % 8, x, y;
        if (near_square(p, j, &x, &y) && sea_at(tile(x, y)) == p->sea && !occupied(tile(x, y))) {
            if (p->sea) {
                int isle = veh_init(PROTO_ISLE, 0, x, y);
                if (isle >= 0) {
                    spawned++;
                    game_vehs[isle].visibility |= tile(x, y)->visibility;
                    int worms = game_rand() % 2 + 1;
                    if (worms >= veh_cargo(isle)) worms = veh_cargo(isle);
                    for (; worms > 0; worms--) {
                        if (!(game_rand() % 5) && *game_expansion) {
                            veh_init(PROTO_SPORE_LAUNCHER, 0, x, y);
                            spores = 1;
                        } else veh_init(PROTO_MIND_WORMS, 0, x, y);
                    }
                    break;
                }
            } else {
                base_find(x, y);
                if (*game_base_find_dist > 1) {
                    int w;
                    if (!(game_rand() % 5) && *game_expansion) {
                        w = veh_init(PROTO_SPORE_LAUNCHER, 0, x, y);
                        spores = 1;
                    } else w = veh_init(PROTO_MIND_WORMS, 0, x, y);
                    if (w >= 0) {
                        spawned++;
                        game_vehs[w].visibility |= tile(x, y)->visibility;
                        game_draw_tile(x, y, 2);
                        if (event > 12 + perihelion || *game_turn < 15 || *game_base_find_dist <= 3) break;
                    }
                }
            }
        }
        int step = 1;
        if (spawned) {
            step = 5 - F->base_count / 2;
            if (step < 1) step = 1;
            else if (step > 4) step = 4;
        }
        k += step;
    }
    if (!spawned) return POD_NEXT;
    if (p->player) {
        game_fx_play(game_fx, 0x11);
        if (spores) game_netmsg_pop(game_netmsg, text(p->sea ? 0x68efe8 : 0x68eff4), -5000, 0,
                                    text(p->sea ? 0x68efcc : 0x68efd8));
        else game_netmsg_pop(game_netmsg, text(p->sea ? 0x68f01c : 0x68f028), -5000, 0, text(p->sea ? 0x68f000 : 0x68f00c));
        game_fx_fade(game_fx, 0x11);
    }
    return POD_DONE;
}

// 18: the unit is cloned (an armed one on land; at sea one time in four),
// its morale and damage with it.
static int pod_clone(struct pod *p) {
    const struct veh *V = &game_vehs[p->veh];
    if (p->sea) {
        if (game_rand() % 4) return POD_NEXT;
    } else if (!armed(V)) return POD_NEXT;
    if (pod_plan(p) == PLAN_ARTIFACT) return POD_NEXT;
    if (*game_net_multiplayer) {
        const struct faction_data *F = pod_faction(p);
        int behind = 0;
        for (int i = 1; i < 8; i++) {
            if (!(*game_factions_alive & (1u << i)) || !game_is_human(i) || i == p->faction) continue;
            const struct faction_data *G = &game_factions[i];
            if (p->sea ? G->naval >= F->naval : G->total_combat_units >= F->total_combat_units) behind = 1;
            if (G->total_combat_units < F->total_combat_units / 2) return POD_NEXT;
        }
        if (!behind) return POD_NEXT;
    }
    int c = veh_init(V->proto, p->faction, p->x, p->y);
    if (c < 0) return POD_NEXT;
    V = &game_vehs[p->veh];
    struct veh *C = &game_vehs[c];
    C->home_base = -1;
    C->morale = V->morale;
    C->damage = V->damage;
    game_parse_says(0, game_protos[V->proto].name, -1, -1);
    if (p->player) game_netmsg_pop(game_netmsg, text(0x68f04c), -5000, 0, text(0x68f03c));
    return POD_DONE;
}

// Any other: a resource on the square (not on fungus, deep sea or by the
// poles); one time in three another outcome follows, rolled once more.
static int pod_resource(struct pod *p) {
    if (p->flag > 1) return POD_DONE;
    const struct map_tile *t = tile(p->x, p->y);
    if (p->sea && alt_of(t) < ALT_SHELF) return POD_NEXT;
    if (fungus_on(t)) return POD_NEXT;
    if (p->y <= 1 || p->y >= *game_map_height - 2 || (*game_rules & RULES_NO_RESOURCES)) return POD_NEXT;
    bit_set(p->x, p->y, MAP_RESOURCE_BONUS, 1);
    int bonus = bonus_at(p->x, p->y, 0);
    site_radius(p->x, p->y, 0);
    if (p->player && bonus) {
        if (bonus == 1) game_wave_it(0x28);
        else if (bonus == 2) game_wave_it(0x2a);
        else if (bonus == 3) {
            game_ambience(0xd2);
            game_wave_it(0x29);
        }
        game_parse_say(0, (int)(intptr_t)(*game_labels)[91 + bonus], -1, -1);
        game_netmsg_pop(game_netmsg, text(0x68f084), 5000, 0, text(0x68f074));
    }
    game_draw_tile(p->x, p->y, 2);
    if (!(game_rand() % 3) || p->flag) return POD_DONE;
    p->flag = 2;
    return POD_NEXT;
}

// The unit opens the pod on its square. A unity pod around the landing site
// gives a rover (or what follows from one) or a tech; else an outcome is
// rolled, more widely next to the faction's bases and for formers and
// colony pods; a pod by another faction's base brings native life. An
// artifact unit may vanish into it (returns 1). Thinker's version adds
// options of its own (rarer pods, early research, which natives spawn)
// and checks a few things the original doesn't.
int goody_box(int veh) {
    struct veh *V = &game_vehs[veh];
    struct pod p = {.veh = veh, .x = V->x, .y = V->y, .faction = V->faction};
    p.sea = sea_at(tile(p.x, p.y));
    if (game_do_unity_crash(p.x, p.y, p.faction)) return 0;
    p.player = p.faction == cur_faction();
    struct faction_data *F = pod_faction(&p);
    int32_t opened = ++*faction_word(p.faction, F_GOODY_OPENED);
    if (*game_net_multiplayer) game_my_srand((int)((uint32_t)*game_map_seed + (uint32_t)opened * 37u));
    if (*game_turn >= -5 * F->se_pending[SE_RESEARCH] && !(*game_rules & RULES_NO_TECH))
        ++*faction_word(p.faction, F_TECH_ACCUMULATED);
    int base = base_find(p.x, p.y);
    const struct map_tile *t = tile(p.x, p.y);
    uint32_t bits = t->bits;
    int event;
    if ((bits & MAP_UNITY_POD) && (!F->base_count || (base >= 0 && game_bases[base].faction == p.faction))) {
        event = bits & MAP_UNITY_ROVER ? 9 : 10;
        p.landing = 1;
        bit_set(p.x, p.y, MAP_UNITY_POD, 0);
    } else if (*game_base_find_dist <= 2 && base >= 0 && game_bases[base].faction != p.faction) {
        event = 16;
        p.flag = 1;
    } else if (((bits & MAP_FUNGUS) && alt_of(t) >= ALT_SHELF) || p.sea) {
        event = pod_next(&p);
    } else {
        int range = *game_base_find_dist > 12 && F->region_total_bases[t->region] ? 20 : 40;
        int plan = pod_plan(&p);
        if ((plan == PLAN_TERRAFORM_ && (*game_base_find_dist < 12 || *game_turn < 40)) || plan == PLAN_COLONY)
            range += 10;
        event = game_rnd(range);
    }
    for (;;) {
        if (event < 0) return 0;
        if (pod_plan(&p) == PLAN_ARTIFACT && !(game_rand() % 3) && !(V->flags & VEH_FLAG_UNK_20)) event = 9999;
        if (event == 9999) {  // the artifact vanishes into a whirlpool, or the ground
            if (p.player) game_netmsg_pop(game_netmsg, text(p.sea ? 0x68f058 : 0x68f068), 5000, 0, NULL);
            kill(veh);
            return 1;
        }
        p.event = event;
        int r;
        switch (event) {
        case 0: r = pod_energy(&p); break;
        case 1: r = pod_river(&p); break;
        case 2: r = pod_view(&p); break;
        case 3: r = pod_quake(&p); break;
        case 4: r = pod_gate(&p); break;
        case 5: r = pod_build(&p); break;
        case 6: r = pod_artifact(&p, 6); break;
        case 7: r = pod_fungus(&p); break;
        case 8: r = pod_monolith(&p); break;
        case 9: r = pod_unit(&p); break;
        case 10: r = pod_tech(&p); break;
        case 11: r = pod_terrain(&p); break;
        case 12: case 13: case 14: case 15: case 16: case 17: r = pod_natives(&p, event); break;
        case 18: r = pod_clone(&p); break;
        default: r = pod_resource(&p); break;
        }
        if (r == POD_DONE) return 0;
        event = r == POD_NEXT ? pod_next(&p) : r;
    }
}

// ---- The turn's random events ------------------------------------------------------
//
// Once a turn (flag 0) the timed effects count down first: solar flares,
// sunspots, the dust cloud of a volcano or a meteor, and the events at
// bases; the AI is set to watch every human faction it isn't already at
// odds with; perihelion begins or ends. Then, from turn 75 (less 10 a level
// of difficulty) and unless the bell curve rule is on, one base may have an
// event: a base out of 100 (or as many as there are), with more than three
// citizens, of a faction with another base, and none of the base events
// under way. game_random() picks both, from a seed of the map's and the
// turn's on flag 0.
//
// Thinker's version (random_events in its gameturn.cpp) fixes several of
// the original's quirks and keeps its own generator; the original's are
// kept here and noted at each event.

enum {
    RULES_DO_OR_DIE = 0x1, RULES_BELL_CURVE = 0x8000,
    STATE_SCENARIO = 0x10,
    PREF_PROJECT_MOVIES = 0x10000000,
    // Base events, and the turns they last.
    BASE_EVENTS = 0x7e00, BASE_EVENT_TURNS = 10,
    EV_FAC_CRECHE = 2, EV_FAC_ENERGY_BANK = 7, EV_FAC_NETWORK_NODE = 8, EV_FAC_BIOLOGY_LAB = 9,
    EV_FAC_RESEARCH_HOSPITAL = 17, EV_FAC_NANOHOSPITAL = 18,
    // Secret projects: the Human Genome Project, Longevity Vaccine,
    // Clinical Immortality and the Planetary Energy Grid; their facilities
    // count from 70.
    SP_GENOME = 0, SP_LONGEVITY = 14, SP_IMMORTALITY = 26, SP_ENERGY_GRID = 36, FAC_SP_FIRST = 70,
    MAP2_VOLCANO = 0x2,
    VK_SHIFT = 0x10,
    TREATY_INFILTRATOR = 0x1000,
    F_ELIMINATED = 0x20a0,  // factions it has put out of the game
};

// Sensors, bores, mirrors, condensers, forest, soil enrichers, farms,
// bunkers, solar collectors, fungus, mines, tubes and roads.
#define MAP_LOST_TO_DISASTER 0x81e8887cu
#define MAP2_VOLCANO_LIVE 0x80000002u  // the volcano landmark, unless disabled

struct event {
    int base, faction, x, y;
    struct base *B;
    struct faction_data *F;
};

static inline int shift_held(void) { return (game_get_async_key_state(VK_SHIFT) & 0xff00) != 0; }
// The pauses that animate an event, skipped while shift is held.
static void pause_unless_shift(int ms) {
    if (!shift_held() && !*game_net_multiplayer) game_clock_wait(ms);
}
static int built(int base, int fac) {
    int offset;
    uint32_t mask;
    bitmask(fac, &offset, &mask);
    return (game_bases[base].facilities_built[offset] & mask) != 0;
}
static inline int fac_name(int fac) { return (int)(intptr_t)game_facilities[fac].name; }
// Whether the faction knows the square.
static int knows(int faction, int x, int y) {
    return (game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED) || (tile(x, y)->visibility & (1u << faction));
}
// A square no other faction's territory.
static int territory_ok(int faction, int x, int y) {
    int owner = whose_territory(faction, x, y, NULL, 0);
    return owner < 0 || owner == faction;
}
// What faction j last saw of the square's bits, for j of 1 to 7 (j of 0 is
// the landmark word before them, as the original's arithmetic has it).
static inline uint32_t *seen_word(struct map_tile *t, int j) { return (uint32_t *)((char *)t + 0xc + 4 * j); }
static void forget_bits(struct map_tile *t, uint32_t bits) {
    for (int j = 1; j < 8; j++) *seen_word(t, j) &= ~bits;
}
static int event_square(int cx, int cy, int i, int *x, int *y) {
    *x = xrange(cx + game_radius_x[i]), *y = cy + game_radius_y[i];
    return on_map(*x, *y);
}
// A bad turn falls on a faction from fifth place down (fourth from turn
// 150).
static inline int ranked_low(const struct event *e) { return e->F->ranking >= (*game_turn >= 150) + 4; }
static inline int alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
// The base's own message: to the player, or anyone in a hotseat game.
static void base_message(const struct event *e, uint32_t label, uint32_t picture) {
    if (e->faction != cur_faction() && !*game_pbem) return;
    game_pop2(text(label), text(picture), e->base);
}
// The map moved to the base for the player.
static void focus_player(const struct event *e) {
    if (e->faction == cur_faction()) game_console_focus(game_console, e->x, e->y, e->faction);
}

// 0 to 5: the base events, for ten turns. The good ones (a bumper crop, an
// industrial boom, a heat wave) pass the faction in first place by, but
// one time in four or on a human's two lowest difficulties; the bad ones
// (famine, a bust, cloud cover) fall on a low-ranked faction. The original
// puts the message's label together first and goes on only if it isn't
// empty.
static void base_event(const struct event *e, int good, uint32_t bit, uint32_t label, uint32_t picture) {
    if (good) {
        if (game_faction_rankings[7] == e->faction && (!game_is_human(e->faction) || e->F->diff_level > 1) &&
            game_random(0, 4))
            return;
    } else if (!ranked_low(e))
        return;
    e->B->event |= bit;
    strcat(game_battle_text, text(label));
    if (!strlen(game_battle_text)) return;
    if (e->B->event & BASE_EVENTS) e->B->random_event_turns = BASE_EVENT_TURNS;
    if (e->faction != cur_faction() && !*game_pbem) return;
    game_pop2(game_battle_text, text(picture), e->base);
}

// 6: a plague, unless the base has a research hospital or nanohospital, or
// the faction one of three medical projects (each named instead). It halves
// the bases within half the base's size (rounded up), but those protected
// so. Its message goes to the last of the player's bases struck, or to base
// 0 when the player only sees the base: the original keeps the base's
// number plus one, but starts it at 1 there (Thinker names the base).
static void plague(const struct event *e) {
    int f = e->faction;
    if (built(e->base, EV_FAC_RESEARCH_HOSPITAL) || built(e->base, EV_FAC_NANOHOSPITAL)) {
        int nano = built(e->base, EV_FAC_NANOHOSPITAL);
        game_parse_say(1, fac_name(nano ? EV_FAC_NANOHOSPITAL : EV_FAC_RESEARCH_HOSPITAL), -1, -1);
        if (f == cur_faction()) {
            game_console_focus(game_console, e->x, e->y, cur_faction());
            game_pop2(text(0x68b1ac), text(0x68b19c), e->base);
        } else if (*game_pbem)
            game_pop2(text(0x68b1c8), text(0x68b1b8), e->base);
        return;
    }
    if (has_project(SP_GENOME, f) || has_project(SP_LONGEVITY, f) || has_project(SP_IMMORTALITY, f)) {
        int sp = has_project(SP_GENOME, f) ? SP_GENOME : has_project(SP_LONGEVITY, f) ? SP_LONGEVITY : SP_IMMORTALITY;
        game_parse_say(1, fac_name(FAC_SP_FIRST + sp), -1, -1);
        if (f == cur_faction()) {
            game_console_focus(game_console, e->x, e->y, cur_faction());
            game_pop2(text(0x68b1e4), text(0x68b1d4), e->base);
        } else if (*game_pbem)
            game_pop2(text(0x68b200), text(0x68b1f0), e->base);
        return;
    }
    if (!ranked_low(e)) return;
    int reach = (e->B->pop_size + 1) / 2, shown = 0, cur = cur_faction();
    game_parse_num(1, reach);
    if (e->B->faction == cur || (e->B->visibility & (1u << cur))) {
        shown = 1;
        game_console_focus(game_console, e->x, e->y, cur);
    }
    for (int i = 0; i < *game_base_count; i++) {
        struct base *B = &game_bases[i];
        if ((x_dist(B->x, e->B->x) + abs(B->y - e->B->y)) >> 1 > reach) continue;
        if (built(i, EV_FAC_RESEARCH_HOSPITAL) || built(i, EV_FAC_NANOHOSPITAL)) continue;
        if (has_project(SP_GENOME, B->faction) || has_project(SP_LONGEVITY, B->faction) ||
            has_project(SP_IMMORTALITY, B->faction))
            continue;
        B->pop_size = (int8_t)((B->pop_size + 1) / 2);
        game_draw_tile(B->x, B->y, 2);
        if (B->faction == cur_faction()) {
            game_console_focus(game_console, B->x, B->y, cur_faction());
            shown = i + 1;
        } else if (*game_pbem)
            shown = i + 1;
    }
    if (shown) game_pop2(text(0x68b21c), text(0x68b20c), shown - 1);
}

// A resource blinked on the map for the player, ten times, ending as it is.
static void blink_resource(int faction, int x, int y, int ends_on) {
    game_map_win_set_center(game_map_wins[0], x, y, 1);
    if (shift_held()) return;
    for (int k = 0; k < 10; k++) {
        for (int on = !ends_on, n = 0; n < 2; n++, on = !on) {
            bit_set(x, y, MAP_RESOURCE, on);
            synch_bit(x, y, faction);
            game_draw_tile(x, y, 2);
            pause_unless_shift(20);
        }
    }
}
static int resource_name(int kind) { return (int)(intptr_t)game_resource_names[2 * kind + 1]; }

// 7 and 8: a resource near the base peters out (from turn 75), or a new one
// is found. The first of the nine squares around the base with a resource
// the faction knows, at sea or in its territory, peters out; else a
// resource comes up on one of the 20 squares around (known, at sea or in
// its territory), even where there is one already (Thinker checks first).
static void resources(const struct event *e) {
    if (*game_pbem) return;
    int f = e->faction, pick = game_random(0, 20) + 1, nearby = 0, x, y;
    for (int i = 0; i < 9; i++) {
        if (!event_square(e->x, e->y, i, &x, &y) || !bonus_at(x, y, 0) || !knows(f, x, y)) continue;
        if (!sea_at(tile(x, y)) && !territory_ok(f, x, y)) continue;
        pick = i, nearby = 1;
        break;
    }
    if (!event_square(e->x, e->y, pick, &x, &y) || !knows(f, x, y)) return;
    if (!sea_at(tile(x, y)) && !territory_ok(f, x, y)) return;
    if (nearby && bonus_at(x, y, 0)) {
        if (*game_turn < 75) return;
        game_parse_say(1, resource_name(bonus_at(x, y, 0) - 1), -1, -1);
        bit_set(x, y, MAP_RESOURCE, 0);
        synch_bit(x, y, f);
        game_draw_tile(x, y, 2);
        if (bonus_at(x, y, 0) || f != cur_faction()) return;
        blink_resource(f, x, y, 0);
        pause_unless_shift(100);
        game_pop2(text(0x68b23c), text(0x68b228), -1);
        return;
    }
    bit_set(x, y, MAP_RESOURCE, 1);
    synch_bit(x, y, f);
    game_draw_tile(x, y, 2);
    int kind = bonus_at(x, y, 0) - 1;
    if (kind < 0 || f != cur_faction()) return;
    blink_resource(f, x, y, 1);
    game_parse_say(1, resource_name(kind), -1, -1);
    int sea = sea_at(tile(x, y));
    uint32_t picture = !kind ? (sea ? 0x68b248 : 0x68b258) : (sea ? 0x68b268 : 0x68b278);
    game_pop2(text(0x68b2a8), text(picture), -1);
}

// 9: one time in six, a solar flare (energy tripled for a turn; see
// game_solar_flares), which burns out every energy satellite and orbital
// defense pod.
static void solar_flare(void) {
    if (*game_solar_flares || game_random(0, 5)) return;
    int burnt = 0;
    for (int i = 1; i < 8; i++) {
        int32_t *s = game_factions[i].satellites;
        if (s[2] || s[3]) s[2] = s[3] = 0, burnt = 1;
    }
    *game_solar_flares = 1;
    if (burnt) game_pop2(text(0x68b2c0), text(0x68b2b4), -1);
    else game_pop2(text(0x68b2d8), text(0x68b2cc), -1);
}

// 10: one time in six, an asteroid shower wrecks the mineral satellites.
static void asteroids(void) {
    if (game_random(0, 5)) return;
    int hit = 0;
    for (int i = 1; i < 8; i++)
        if (game_factions[i].satellites[1]) game_factions[i].satellites[1] = 0, hit = 1;
    if (hit) game_pop2(text(0x68b2f0), text(0x68b2e4), -1);
}

// 11: sunspots for 10 to 20 turns, from turn 50, at least 40 turns after
// the last ones end (80, or one time in three from turn 80).
static void sunspots(void) {
    int32_t since = *game_sunspot_duration;
    if (*game_turn < 50 || since > -40) return;
    if (since > -80 && *game_turn >= 80 && game_random(0, 3)) return;
    *game_sunspot_duration = game_random(0, 11) + 10;
    game_parse_num(0, 20);
    game_pop2(text(0x68b30c), text(0x68b2fc), -1);
}

// 12: the energy market crashes for a faction with over 1000 credits (a
// human's, or the AI's over 2000), leaving a quarter (Thinker a half); or
// booms for a faction ranked fifth or higher with 500 or less, doubling
// them.
static void energy_market(const struct event *e) {
    struct faction_data *F = e->F;
    int32_t credits = F->energy_credits;
    if (credits <= 1000) {
        if (F->ranking > 4 || credits > 500) return;
        F->energy_credits = credits * 2;
        if (e->faction != cur_faction()) return;
        game_parse_num(0, F->energy_credits);
        game_console_update_data(game_console, 0);
        game_pop2(text(0x68b328), text(0x68b318), -1);
        return;
    }
    if (!game_is_human(e->faction) && credits <= 2000) return;
    F->energy_credits = credits / 4;
    if (e->faction == cur_faction()) {
        game_parse_num(0, F->energy_credits);
        game_console_update_data(game_console, 0);
        game_pop2(text(0x68b344), text(0x68b334), -1);
    } else if (*game_pbem) {
        game_parse_num(0, F->energy_credits);
        game_pop2(text(0x68b360), text(0x68b350), -1 - e->faction);
    }
}

// 13, 20 and 21: storms wreck improvements of one kind on the 21 squares of
// the base's radius (known to the faction, in its territory), shown when
// the player sees the base. 13 takes solar collectors on land (from turn
// 75, for a low-ranked faction). 20 and 21 would take kelp farms and mining
// platforms at sea, but the original then also asks for land, so they never
// happen (Thinker fixes that).
static void storm(const struct event *e, uint32_t bit, int at_sea, uint32_t label, uint32_t picture) {
    if (at_sea && *game_pbem) return;
    if (*game_turn < 75 || !ranked_low(e)) return;
    int f = e->faction, shown = 0;
    for (int i = 0; i < 21; i++) {
        int x, y;
        if (!event_square(e->x, e->y, i, &x, &y)) continue;
        struct map_tile *t = tile(x, y);
        if (at_sea && !sea_at(t)) continue;
        if (!(t->bits & bit) || !knows(f, x, y) || !territory_ok(f, x, y) || sea_at(t)) continue;
        int cur = cur_faction();
        if (!at_sea && *game_pbem) shown = 1;
        else if (f == cur || e->B->faction == cur || (e->B->visibility & (1u << cur))) {
            if (!shown) {
                game_console_focus(game_console, e->x, e->y, cur);
                pause_unless_shift(200);
            }
            shown = at_sea ? 1 : shown | (f == cur ? 2 : 1);
        }
        bit_set(x, y, bit, 0);
        synch_bit(x, y, f);
        forget_bits(t, bit);
        if (shown) {
            game_draw_tile(x, y, 2);
            if (!shift_held() && !*game_net_multiplayer && !*game_pbem) game_clock_wait(50);
        }
    }
    if (!shown) return;
    if (!at_sea) {
        game_pop2(text(label), text(picture), e->base);
        return;
    }
    game_parse_says(0, e->B->name, -1, -1);
    game_pop2(text(label), NULL, e->faction == cur_faction() ? e->base : -1);
}

// A unit's hit points: 1 for an artifact, else 10 a reactor level.
static int hit_points(const struct veh *V) {
    const struct unit_proto *P = proto_of(V);
    if (P->plan == PLAN_ARTIFACT) return 1;
    int reactor = P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor;
    return reactor * 10;
}
// The player's base among those struck, named in the message (once; the
// original compares the faction of the last base it named).
static void name_struck(int base, int *named, int *shown) {
    int cur = cur_faction();
    if (game_bases[base].faction != cur || game_bases[*named].faction == cur) return;
    game_parse_says(0, game_bases[base].name, -1, -1);
    *named = base, *shown = 1;
}

// 14: Mount Planet erupts, for a faction with eight bases or more ranked
// third or lower, whose base is within 5 of it and who knows it (from turn
// 75, not in a scenario). The 81 squares around lose what was built on
// them and turn rocky; units on the volcano die and others lose half their
// hit points left; bases on the nine squares nearest die (but scenario
// objectives), the others lose people, the nearer the more. A dust cloud
// follows for ten turns.
static void volcano(struct event *e) {
    if (*game_pbem || *game_turn < 75 || e->F->base_count < 8 || (*game_state & STATE_SCENARIO)) return;
    int mx = *game_mount_planet_x, my = *game_mount_planet_y;
    if (my < 0 || my >= *game_map_height || mx < 0 || mx >= *game_map_width) return;
    if ((tile(mx, my)->bits2 & MAP2_VOLCANO_LIVE) != MAP2_VOLCANO) return;
    if (((x_dist(e->x, mx) + abs(e->y - my)) & ~1) > 10) return;
    if (!knows(e->faction, mx, my) || e->F->ranking < 6) return;
    int cur = cur_faction(), shown = knows(cur, mx, my), named = e->base;
    for (int i = 0; i < 81; i++) {
        int x, y;
        if (!event_square(mx, my, i, &x, &y)) continue;
        bit_set(x, y, MAP_LOST_TO_DISASTER, 0);
        synch_bit(x, y, cur_faction());
        rocky_set(x, y, 2);
        if ((tile(x, y)->bits2 & MAP2_VOLCANO_LIVE) == MAP2_VOLCANO) {
            for (int v = *game_veh_count - 1; v >= 0; v--)
                if (game_vehs[v].x == x && game_vehs[v].y == y) veh_kill(v);
        } else
            for (int v = *game_veh_count - 1; v >= 0; v--) {
                struct veh *V = &game_vehs[v];
                if (V->x != x || V->y != y) continue;
                int left = hit_points(V) - V->damage;
                left = left < 0 ? 0 : left > 9999 ? 9999 : left;
                V->damage = (uint8_t)(V->damage + left / 2);
            }
        int b = base_at(x, y);
        if (b < 0) continue;
        name_struck(b, &named, &shown);
        struct base *B = &game_bases[b];
        if (i < 9 && !is_objective(b)) base_kill(b);
        else if (i < 25) B->pop_size = (int8_t)((B->pop_size + 3) / 4);
        else if (i < 49) B->pop_size = (int8_t)((B->pop_size + 1) / 2);
        else B->pop_size = (int8_t)((B->pop_size + 1) * 3 / 4);
    }
    if (!shown || !game_console_focus(game_console, mx, my, cur_faction())) game_draw_map(1);
    *game_dust_cloud = 10;
    game_popp(*game_script_file, text(0x68b38c), 0, text(0x68b380), 0);
}

// 15: a meteor strikes the base of a faction with eight bases or more
// ranked second or lower (from turn 75, not in a scenario, away from
// landmarks), if the 25 squares around are land and at most five more of
// the 49 sea (Thinker four). Everything on the 49 squares is lost, bases
// and units too, and a crater is left; a dust cloud follows.
static void meteor(struct event *e) {
    if (*game_pbem || *game_turn < 75 || e->F->base_count < 8 || (*game_state & STATE_SCENARIO)) return;
    if (near_landmark(e->x, e->y)) return;
    int cur = cur_faction(), shown = knows(cur, e->x, e->y), sea = 0, x, y;
    for (int i = 0; i < 49; i++) {
        if (!event_square(e->x, e->y, i, &x, &y) || !sea_at(tile(x, y))) continue;
        if (i < 25 || ++sea > 5) return;
    }
    if (e->F->ranking < 7) return;
    int named = e->base;
    for (int i = 0; i < 49; i++) {
        if (!event_square(e->x, e->y, i, &x, &y)) continue;
        bit_set(x, y, MAP_LOST_TO_DISASTER, 0);
        synch_bit(x, y, cur_faction());
        int b = base_at(x, y);
        if (b >= 0) {
            name_struck(b, &named, &shown);
            base_kill(b);
        }
        for (int v = *game_veh_count - 1; v >= 0; v--) {
            const struct veh *V = &game_vehs[v];
            if (V->x != x || V->y != y) continue;
            if (V->faction == cur_faction()) shown = 1;
            veh_kill(v);
        }
    }
    world_crater(e->x, e->y);
    world_climate();
    if (!shown || !game_console_focus(game_console, e->x, e->y, cur_faction())) game_draw_map(1);
    *game_dust_cloud = 10;
    game_popp(*game_script_file, text(0x68b3a8), 0, text(0x68b39c), 0);
}

// 16 and 17: a blight takes the forests, soil enrichers and farms around
// the base (16), or the mines collapse (17), on the land of its radius it
// knows and holds; once the faction has the tech for a biology lab or an
// energy bank, for a low-ranked faction. A base with the lab (or with the
// bank or the Planetary Energy Grid) is spared: it gains a bumper crop
// instead (16) or 50 credits (17). The blight's squares are forgotten only
// by the player's faction (seven times over), the mines' by every faction.
static void blight(struct event *e, int mines) {
    int fac = mines ? EV_FAC_ENERGY_BANK : EV_FAC_BIOLOGY_LAB;
    if (!has_tech(game_facilities[fac].preq_tech, e->faction)) return;
    int spared = built(e->base, fac) || (mines && has_project(SP_ENERGY_GRID, e->faction)), shown = 0;
    if (!spared && !ranked_low(e)) return;
    uint32_t bit = mines ? MAP_MINE : MAP_FOREST | MAP_FARM, lost = mines ? MAP_MINE : MAP_FOREST | MAP_FARM | MAP_SOIL_ENRICHER;
    for (int i = 0; i < 21; i++) {
        int x, y;
        if (!event_square(e->x, e->y, i, &x, &y)) continue;
        struct map_tile *t = tile(x, y);
        if (sea_at(t) || !(t->bits & bit) || !knows(e->faction, x, y) || !territory_ok(e->faction, x, y)) continue;
        if (!spared) {
            if (!shown) {
                game_console_focus(game_console, e->x, e->y, cur_faction());
                pause_unless_shift(200);
            }
            bit_set(x, y, lost, 0);
            synch_bit(x, y, e->faction);
            if (mines) forget_bits(t, lost);
            else
                for (int k = 0; k < 7; k++) *seen_word(t, cur_faction()) &= ~lost;
            game_draw_tile(x, y, 2);
            pause_unless_shift(50);
        }
        shown = 1;
    }
    if (!shown) return;
    if (spared) {
        if (mines) e->F->energy_credits += 50;
        else {
            e->B->event |= BASE_EVENT_BUMPER;
            e->B->random_event_turns = BASE_EVENT_TURNS;
        }
    }
    if (e->faction == cur_faction() && !game_console_focus(game_console, e->x, e->y, e->faction))
        game_draw_radius(e->x, e->y, 2, 2);
    if (mines) base_message(e, spared ? 0x68b3e0 : 0x68b3e8, 0x68b3d0);
    else base_message(e, spared ? 0x68b3c0 : 0x68b3c8, 0x68b3b0);
}

// 18: once the faction has the tech for a network node, and research a
// quarter of the way to its next tech, a base with a node completes the
// tech's research; else a low-ranked faction's network crashes, losing it.
static void network(struct event *e) {
    if (!has_tech(game_facilities[EV_FAC_NETWORK_NODE].preq_tech, e->faction)) return;
    int32_t *research = faction_word(e->faction, F_TECH_ACCUMULATED);
    if (*research < tech_rate(e->faction) / 4) return;
    if (built(e->base, EV_FAC_NETWORK_NODE)) {
        *research = tech_rate(e->faction);
        focus_player(e);
        base_message(e, 0x68b400, 0x68b3f0);
        return;
    }
    if (!ranked_low(e)) return;
    *research = 0;
    e->F->net_random_event = 0;
    focus_player(e);
    base_message(e, 0x68b41c, 0x68b40c);
}

// 19: once the faction has the tech for a children's creche, a base with
// one grows by up to two while it has food to spare (Thinker checks the
// size limit); else a low-ranked faction's base gets restless, its people
// assimilating for five turns.
static void creche(struct event *e) {
    if (!has_tech(game_facilities[EV_FAC_CRECHE].preq_tech, e->faction)) return;
    if (built(e->base, EV_FAC_CRECHE)) {
        if (e->B->surplus[RESOURCE_NUTRIENT] <= 0) return;
        for (int grown = 1; grown < 3 && e->B->surplus[RESOURCE_NUTRIENT] > 0; grown++) {
            e->B->pop_size++;
            set_base(e->base);
            base_compute(1);
        }
        if (e->faction == cur_faction() && !game_console_focus(game_console, e->x, e->y, e->faction))
            game_draw_tile(e->x, e->y, 2);
        if (alien(e->faction)) base_message(e, 0x68b438, 0x68b428);
        else base_message(e, 0x68b44c, 0x68b440);
        return;
    }
    if (!ranked_low(e) || e->B->assimilation_turns) return;
    e->B->assimilation_turns = 5;
    focus_player(e);
    if (alien(e->faction)) base_message(e, 0x68b464, 0x68b454);
    else base_message(e, 0x68b478, 0x68b46c);
}

// The countdowns and perihelion (flag 0); returns 1 if perihelion began or
// ended, which ends the turn's events.
static int events_upkeep(int flag) {
    if (!flag) {
        // A flare lasts the turn after it (bit 1), then ends.
        if (*game_solar_flares & 2) *game_solar_flares = 0;
        else if (*game_solar_flares & 1) *game_solar_flares = 2;
        int32_t spots = *game_sunspot_duration;
        if (spots < -1000) spots = -1000;
        if (*game_dust_cloud < 0) *game_dust_cloud = 0;
        *game_sunspot_duration = --spots;
        if (!spots) game_pop2(text(0x68b094), text(0x68b084), -1);
        for (int i = 1; i < 8; i++) {
            if (!game_is_human(i)) continue;
            for (int j = 1; j < 8; j++)
                if (!game_is_human(j) && !(game_factions[j].diplo_status[i] & (TREATY_VENDETTA | TREATY_WANT_REVENGE)))
                    set_treaty(j, i, TREATY_SHALL_BETRAY | TREATY_UNK_800, 0);
        }
        if (*game_dust_cloud > 0 && !--*game_dust_cloud) game_pop2(text(0x68b0b0), text(0x68b0a0), -1);
        // A base's event ends as its turns run out: from 0 they go to -1,
        // and the event stays (Thinker stops at 0).
        for (int b = 0; b < *game_base_count; b++) {
            struct base *B = &game_bases[b];
            if (!(B->event & BASE_EVENTS)) continue;
            if (B->random_event_turns >= 0) B->random_event_turns--;
            if (!B->random_event_turns) B->event &= ~(uint32_t)BASE_EVENTS;
        }
        if ((*game_state & GAME_STATE_PERIHELION) && !((game_year(*game_turn) - 2210) % 80)) {
            *game_state &= ~(uint32_t)GAME_STATE_PERIHELION;
            game_pop2(text(0x68b0cc), text(0x68b0bc), -1);
            return 1;
        }
    }
    if (*game_turn > 50 && !flag && !((game_year(*game_turn) - 2190) % 80)) {
        *game_state |= GAME_STATE_PERIHELION;
        game_pop2(text(0x68b0e8), text(0x68b0dc), -1);
        return 1;
    }
    return 0;
}

void random_events(int flag) {
    if (events_upkeep(flag)) return;
    if ((*game_rules & RULES_BELL_CURVE) || *game_turn < 75 - *game_difficulty * 10) return;
    if (!flag) game_random_reseed(*game_map_seed + *game_turn * 13);
    int base = game_random(0, *game_base_count > 100 ? *game_base_count : 100);
    if (base >= *game_base_count) return;
    struct base *B = &game_bases[base];
    if (B->pop_size <= 3 || game_factions[B->faction].base_count <= 1 || (B->event & BASE_EVENTS)) return;
    int kind = game_random(0, 22);
    struct event e = {base, B->faction, B->x, B->y, B, &game_factions[B->faction]};
    game_parse_num(0, BASE_EVENT_TURNS);
    game_parse_says(0, B->name, -1, -1);
    game_battle_text[0] = 0;
    switch (kind) {
    case 0: base_event(&e, 1, BASE_EVENT_BUMPER, 0x68b0f4, 0x68b0fc); break;
    case 1: base_event(&e, 0, BASE_EVENT_FAMINE, 0x68b108, alien(e.faction) ? 0x68b110 : 0x68b120); break;
    case 2: base_event(&e, 1, BASE_EVENT_INDUSTRY, 0x68b130, 0x68b13c); break;
    case 3: base_event(&e, 0, BASE_EVENT_BUST, 0x68b14c, 0x68b154); break;
    case 4: base_event(&e, 1, BASE_EVENT_HEAT_WAVE, 0x68b168, 0x68b174); break;
    case 5: base_event(&e, 0, BASE_EVENT_CLOUD_COVER, 0x68b180, 0x68b18c); break;
    case 6: plague(&e); break;
    case 7: case 8: resources(&e); break;
    case 9: solar_flare(); break;
    case 10: asteroids(); break;
    case 11: sunspots(); break;
    case 12: energy_market(&e); break;
    case 13: storm(&e, MAP_SOLAR, 0, 0x68b378, 0x68b36c); break;
    case 14: volcano(&e); break;
    case 15: meteor(&e); break;
    case 16: blight(&e, 0); break;
    case 17: blight(&e, 1); break;
    case 18: network(&e); break;
    case 19: creche(&e); break;
    case 20: storm(&e, MAP_FARM, 1, 0x68b480, 0); break;
    case 21: storm(&e, MAP_MINE, 1, 0x68b48c, 0); break;
    }
}

// ---- A faction's end, and its social engineering --------------------------------------

enum { BONUS_SOCIAL = 3 };

// The player who put the faction out sees where its colony pod escaped to
// (one time in four, or two with an infiltrator among them): the squares
// around its units, the colony pod in the stack alone. The map is moved to
// the last square looked at, the ninth of the radius table, as the
// original has it.
static void tracked(int faction, int killer) {
    int n = (game_factions[cur_faction()].diplo_status[faction] & TREATY_INFILTRATOR) ? 2 : 4;
    if (game_rnd(n)) return;
    int v = veh_find(0, 0, faction, -1);
    if (v < 0) return;
    int x = 0, y = 0;
    for (int i = 0; i < 9; i++) {
        x = xrange(game_vehs[v].x + game_radius_x[i]), y = game_vehs[v].y + game_radius_y[i];
        if (on_map(x, y)) spot_loc(x, y, killer);
    }
    int top = v, found = 0;
    while (game_vehs[top].prev_in_stack >= 0) top = game_vehs[top].prev_in_stack;
    for (int u = top; u >= 0; u = game_vehs[u].next_in_stack) {
        struct veh *U = &game_vehs[u];
        U->visibility = 0;
        if (proto_of(U)->plan == PLAN_COLONY && !found) {
            found = 1;
            U->visibility = (uint8_t)(1u << killer);
        }
    }
    game_draw_tiles(x, y, 2);
    game_map_win_set_center(game_map_wins[0], x, y, 1);
    game_parse_says(6, game_faction_defs[faction].adjective, -1, -1);
    if (alien(faction)) game_popp(*game_script_file, text(0x6915c4), 0, text(0x6915b4), 0);
    else game_popp(*game_script_file, text(0x6915dc), 0, text(0x6915cc), 0);
}

// A faction with no base left is out of the game (returns 1; 0 while it
// has a base): its units are disbanded, and setup_player() lets it escape
// in a colony pod early in the game, or ends it, or the game for the
// player. The killer (if any; 0 for none) counts another faction
// eliminated, and an escaped faction wants revenge on it. Everyone is told,
// with the faction's picture, but the player's own end is left to the
// caller; the player who put it out may see the movie, or where it
// escaped to.
int eliminate_player(int faction, int killer) {
    if (!faction) return 0;
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction) return 0;
    for (int v = *game_veh_count - 1; v >= 0; v--)
        if (v < *game_veh_count && game_vehs[v].faction == faction) veh_kill(v);
    game_factions[faction].base_count = 0;
    int escaped = game_setup_player(faction, killer, 0);
    if (!escaped) {
        if (killer > 0 && !(*game_state & (GAME_STATE_DONE | GAME_STATE_FINAL_SCORE_DONE)))
            ++*faction_word(killer, F_ELIMINATED);
        if (faction == cur_faction()) return 1;
    }
    if (killer < 0) killer = 0;
    else if (killer > 0) set_treaty(faction, killer, TREATY_WANT_TO_TALK | TREATY_WANT_REVENGE | TREATY_VENDETTA, 1);
    game_draw_map(1);
    struct faction_def *d = &game_faction_defs[faction], *k = &game_faction_defs[killer];
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(0, d->noun, -1, -1);
    *game_parse_gender = k->noun_gender, *game_parse_plural = k->noun_plural;
    game_parse_says(1, k->noun, -1, -1);
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(2, d->leader_title, -1, -1);
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(3, d->leader_name, -1, -1);
    game_parse_says(4, game_get_his_her(faction, 0), -1, -1);
    // The message's label: WIPEOUT, YOU if the player did it, and 2 with
    // no killer, else 1 if the faction escaped or 0.
    char number[16], picture[92];
    game_battle_text[0] = 0;
    strcat(game_battle_text, text(0x691580));
    if (killer && killer == cur_faction()) strcat(game_battle_text, text(0x691588));
    snprintf(number, sizeof number, "%d", killer ? escaped != 0 : 2);
    strcat(game_battle_text, number);
    picture[0] = 0;
    strcat(picture, d->filename);
    strcat(picture, text(0x69158c));
    uint32_t pod = alien(faction) ? 0x691594 : 0x6915a4;
    game_popp(*game_script_file, game_battle_text, 0, escaped ? text(pod) : picture, 0);
    if (!escaped) game_mon_killed_faction(killer, faction);
    if (!*game_net_multiplayer && killer == cur_faction()) {
        if (escaped) tracked(faction, killer);
        else if (*game_preferences & PREF_PROJECT_MOVIES) game_amovie_project_2(d->filename);
    }
    if (alien(faction) && alien(cur_faction()) && faction != cur_faction()) game_interlude(0x2a, NULL, 1, 0);
    return 1;
}

// A faction file's rules put back to their defaults before it is read:
// no free techs or bonuses, costs at 100 percent.
void clear_faction(struct faction_def *d) {
    d->rule_tech_selected = d->rule_morale = d->rule_research = d->rule_drone = 0;
    d->rule_talent = d->rule_energy = d->rule_interest = d->rule_population = 0;
    d->rule_hurry = d->rule_techcost = 100;
    d->rule_flags = d->bonus_count = 0;
    d->rule_psi = d->rule_sharetech = d->rule_commerce = 0;
}

// The social effects the faction's file gives it, as the base the social
// models add to.
void compute_faction_modifiers(int faction) {
    struct faction_data *F = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    memset(F->se_base, 0, sizeof F->se_base);
    for (int i = 0; i < d->bonus_count; i++)
        if (d->bonus_id[i] == BONUS_SOCIAL) F->se_base[d->bonus_val1[i]] += d->bonus_val2[i];
}

// The faction's chosen social models take effect: its pending effects are
// worked out again, and as its costs of growth and production change, each
// base's stores are scaled to keep its progress (rounded to nearest). A
// human's bases are worked out again.
void social_set(int faction) {
    struct faction_data *F = &game_factions[faction];
    int nutrient = cost_factor(faction, 0, -1), mineral = cost_factor(faction, 1, -1);
    social_calc(F->se_models_pending, F->se_pending, faction, 0, 0);
    int nutrient_now = cost_factor(faction, 0, -1), mineral_now = cost_factor(faction, 1, -1);
    for (int b = 0; b < *game_base_count; b++) {
        struct base *B = &game_bases[b];
        if (B->faction != faction) continue;
        if (nutrient_now != nutrient) B->nutrients = (nutrient / 2 + nutrient_now * B->nutrients) / nutrient;
        if (mineral_now != mineral) B->minerals = B->minerals_2 = (mineral / 2 + B->minerals * mineral_now) / mineral;
        if (!*game_net_multiplayer && game_is_human(faction)) {
            set_base(b);
            base_compute(1);
        }
    }
    if (faction == cur_faction()) game_graphic_win_redraw(game_base_win);
}

// The social engineering screen, for the faction (in the scenario editor,
// one the editor picks); then its choices take effect. Nothing in a game
// whose rules leave social engineering out.
void social_select(int faction) {
    uint8_t state = (uint8_t)*game_state;
    if (!(state & 0xc0) && (*game_more_rules & MORE_RULES_NO_SOCIAL)) return;
    if (state & GAME_STATE_OMNISCIENT) {
        faction = game_console_editor_who(game_console, faction, 1);
        if (faction < 0) return;
    }
    game_social_win_init(game_social_win, faction);
    game_social_win_close(game_social_win);
    game_synch_soc(faction);
    if (!*game_net_multiplayer) social_set(faction);
}

// ---- Hooks and shadow checks ------------------------------------------------------

extern struct hook_module events_module;
#define EVENTS (&events_module)

// A human about to be asked whether to use a monolith: the answer can't be
// had in a check (a held message answers 0), so the original runs
// unchecked.
static int monolith_asks(int veh) {
    return game_is_human(game_vehs[veh].faction) && !*game_net_multiplayer &&
           !(*game_more_preferences & MORE_PREF_AUTO_MONOLITH);
}
// A pod may also do that (raising a monolith for the unit), or send the
// unit through a gate to meet another faction, whose talks with a human
// ask questions too: while any faction is human, pods run the original
// unchecked (as spot_all's meetings do).
static int pod_asks(int veh) {
    (void)veh;
    return *game_human_factions != 0;
}
#define SHADOW_ASKING(ret, name, asks)                          \
    static ret shadow_##name(int veh) {                        \
        if (hooks_nested) SHADOW_TAIL_##ret(orig_##name(veh)); \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name(veh));     \
        stats_##name.calls++;                                  \
        shadow_tick(EVENTS);                                   \
        if (asks(veh)) SHADOW_TAIL_##ret(orig_##name(veh));    \
        SHADOW_TAIL_##ret(check_##name(veh));                  \
    }
SHADOW_WRITER_CHECK(int, goody_box, (int a), (a), orig_goody_box(a), hooks_writes_game(&w_), "%d", a)
SHADOW_WRITER_CHECK(void, monolith, (int a), (a), orig_monolith(a), hooks_writes_game(&w_), "%d", a)
SHADOW_ASKING(int, goody_box, pod_asks)
SHADOW_ASKING(void, monolith, monolith_asks)
// The player's study of an artifact only shows a message.
SHADOW_WRITER(EVENTS, int, study_artifact, (int a), (a), hooks_writes_game(&w_), "%d", a)
// The turn's events show messages, move the map and pause, all held; none
// asks a question, so every call is checked.
SHADOW_WRITER(EVENTS, void, random_events, (int a), (a), hooks_writes_game(&w_), "%d", a)
// A faction's end runs the original unchecked for a human faction, whose
// end may be the game's. An escaped faction may found a base at once,
// naming it from its file, which a check can't open: the name is found
// first, before any state is protected (on land, or at sea for an aquatic
// faction), and both runs take it, as base_init's check does.
SHADOW_WRITER_CHECK(int, eliminate_player, (int a, int b), (a, b), orig_eliminate_player(a, b), hooks_writes_game(&w_),
                    "%d, %d", a, b)
static void hold_base_name(int faction) {
    if (*game_base_count >= BASE_COUNT) return;
    char name[sizeof game_bases[0].name];
    memcpy(name, game_bases[*game_base_count].name, sizeof name);
    name[0] = 0;
    name_base_hold(faction, name, (game_faction_defs[faction].rule_flags & RULE_FLAG_AQUATIC) != 0);
}
static int shadow_eliminate_player(int faction, int killer) {
    if (hooks_nested) return orig_eliminate_player(faction, killer);
    if (shadow_bypass()) return eliminate_player(faction, killer);
    stats_eliminate_player.calls++;
    shadow_tick(EVENTS);
    if (game_is_human(faction)) return orig_eliminate_player(faction, killer);
    hold_base_name(faction);
    int r = check_eliminate_player(faction, killer);
    name_base_release();
    return r;
}
SHADOW_WRITER(EVENTS, void, clear_faction, (struct faction_def * a), (a), WRITES(a, sizeof *a), "%d",
              (int)(a - game_faction_defs))
SHADOW_WRITER(EVENTS, void, compute_faction_modifiers, (int a), (a),
              WRITES(game_factions[a].se_base, sizeof game_factions[a].se_base), "%d", a)
// A human's bases are worked out again, writing widely.
SHADOW_WRITER(EVENTS, void, social_set, (int a), (a), hooks_writes_game(&w_), "%d", a)
// The social engineering screen can't be checked (the player's choices,
// and a window that allocates): only a game without social engineering,
// where nothing happens, is.
SHADOW_WRITER_CHECK(void, social_select, (int a), (a), orig_social_select(a), hooks_writes_game(&w_), "%d", a)
static int social_screen(void) { return (*game_state & 0xc0) || !(*game_more_rules & MORE_RULES_NO_SOCIAL); }
static void shadow_social_select(int faction) {
    if (hooks_nested) {
        orig_social_select(faction);
        return;
    }
    if (shadow_bypass()) {
        social_select(faction);
        return;
    }
    stats_social_select.calls++;
    shadow_tick(EVENTS);
    if (social_screen()) orig_social_select(faction);
    else check_social_select(faction);
}

// ---- Sweep -------------------------------------------------------------------------
//
// On the sweep's copy of the whole game (put back at the end): units of
// every faction and kind open pods on squares of every kind, with the game
// state that steers the outcomes varied and rand() seeded anew for each;
// units use monoliths; artifacts are studied at bases.

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static inline int coin(uint32_t h, int bit, int n) { return (int)((h >> bit) % (uint32_t)n) == 0; }

// A unit of a faction (not native life) on the map, from a hash; or -1.
static int pick_unit(uint32_t h) {
    int n = *game_veh_count;
    for (int k = 0; k < 64 && n > 0; k++) {
        int v = (int)(mix(h + (uint32_t)k * 0x9e3779b9u) % (uint32_t)n);
        const struct veh *V = &game_vehs[v];
        if (V->faction && on_map(V->x, V->y)) return v;
    }
    return -1;
}

// A square of the map from a hash: land or sea as wanted (or either, -1),
// next to a base now and then.
static int pick_square(uint32_t h, int sea, int *x, int *y) {
    if (coin(h, 0, 4) && *game_base_count > 0) {
        const struct base *B = &game_bases[(h >> 3) % (uint32_t)*game_base_count];
        for (int k = 0; k < 8; k++) {
            int i = (int)(((h >> 9) + (uint32_t)k) % 8);
            *x = xrange(B->x + game_radius_near_x[i]), *y = B->y + game_radius_near_y[i];
            if (on_map(*x, *y) && (sea < 0 || sea_at(tile(*x, *y)) == sea)) return 1;
        }
    }
    int area = *game_map_area;
    for (int k = 0; k < 64; k++) {
        uint32_t n = (mix(h + (uint32_t)k) >> 1) % (uint32_t)area;
        *y = (int)n / *game_map_half_width;
        *x = (int)n % *game_map_half_width * 2 + (*y & 1);
        if (sea < 0 || sea_at(tile(*x, *y)) == sea) return 1;
    }
    return 0;
}

// The game's settings a case varies, kept to put back after it.
struct sweep_saved {
    uint32_t rules, more_rules, state;
    int32_t difficulty, expansion, native_life, turn, current, more_prefs;
    uint8_t humans, achieved[TECH_COUNT];
};
static void sweep_save(struct sweep_saved *s) {
    *s = (struct sweep_saved){*game_rules, *game_more_rules, *game_state, *game_difficulty, *game_expansion,
                              *game_native_life, *game_turn, *game_current_faction, *game_more_preferences,
                              *game_human_factions, {0}};
    memcpy(s->achieved, game_tech_achieved, TECH_COUNT);
}
static void sweep_restore(const struct sweep_saved *s) {
    *game_rules = s->rules, *game_more_rules = s->more_rules, *game_state = s->state;
    *game_difficulty = s->difficulty, *game_expansion = s->expansion, *game_native_life = s->native_life;
    *game_turn = s->turn, *game_current_faction = s->current, *game_more_preferences = s->more_prefs;
    *game_human_factions = s->humans;
    memcpy(game_tech_achieved, s->achieved, TECH_COUNT);
}

// The settings every case may vary: the turn, the faction at the console,
// difficulty, Alien Crossfire, perihelion and the scenario rules. With
// `human` the faction is made the only human one, one that won't be asked
// about monoliths, and the turn is 0, when no faction meets another (so no
// talks with a human, which would ask questions). Otherwise none is human.
static void vary_game(uint32_t h, int faction, int human) {
    if (coin(h, 0, 2)) *game_turn = (int)((h >> 1) % 300);
    if (coin(h, 10, 2)) *game_current_faction = faction;
    *game_human_factions = 0;
    if (human) {
        *game_human_factions = (uint8_t)(1u << faction);
        *game_more_preferences |= MORE_PREF_AUTO_MONOLITH;
        *game_turn = 0;
    }
    if (coin(h, 15, 3)) *game_difficulty = (int32_t)((h >> 17) % 6);
    if (coin(h, 20, 4)) *game_expansion = !*game_expansion;
    if (coin(h, 22, 4)) *game_state ^= GAME_STATE_PERIHELION;
    if (coin(h, 24, 3)) {
        static const uint32_t rules[] = {RULES_FORCE_DIFFICULTY, RULES_NO_NATIVE_LIFE, RULES_NO_MONOLITHS,
                                         RULES_NO_RESOURCES, RULES_NO_ARTIFACTS, RULES_NO_TECH};
        uint32_t g = mix(h);
        for (int i = 0; i < 6; i++)
            if (coin(g, i * 3, 4)) *game_rules ^= rules[i];
        if (coin(g, 20, 6)) *game_more_rules ^= MORE_RULES_NO_UNITS;
        if (coin(g, 23, 6)) *game_more_rules ^= MORE_RULES_NO_TECH;
        if (coin(g, 26, 6)) *game_state ^= STATE_ALL_ARTIFACTS;
    }
}

static void sweep_pods(int turn) {
    static const int techs[] = {TECH_ORBITAL, TECH_FOSSIL, TECH_BRAIN, TECH_BIOADAP, TECH_SENT_RES};
    for (int k = 0; k < 36; k++) {
        uint32_t h = mix((uint32_t)turn * 2654435761u + (uint32_t)k), g = mix(h ^ 0x5bd1e995u);
        int v = pick_unit(h);
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        int faction = V->faction, triad = game_chassis[proto_of(V)->chassis].triad, x, y;
        struct sweep_saved saved;
        sweep_save(&saved);
        // Where: its own square, or one of its kind (any for an air unit).
        if (!coin(h, 3, 4) && pick_square(g, triad == TRIAD_AIR ? -1 : triad == TRIAD_SEA, &x, &y) &&
            !occupied(tile(x, y)))
            veh_put(v, x, y);
        x = V->x, y = V->y;
        struct map_tile *t = tile(x, y);
        uint32_t b = mix(g);
        if (coin(b, 0, 4)) t->bits |= coin(b, 2, 2) ? MAP_UNITY_ROVER : 0x8000000u;
        if (coin(b, 3, 5)) t->bits ^= MAP_FUNGUS;
        if (coin(b, 6, 6)) t->bits ^= MAP_RIVER;
        if (coin(b, 9, 6)) t->bits ^= MAP_RESOURCE_BONUS;
        if (coin(b, 12, 6)) t->climate = (uint8_t)((t->climate & 0x1f) | ((b >> 15) % 8) << 5);
        if (coin(b, 18, 6)) t->rocky_lock = (uint8_t)((t->rocky_lock & 0x3f) | ((b >> 21) % 3) << 6);
        // Who: the unit's kind (an artifact, a former, a colony pod) and the
        // faction's counts.
        uint32_t u = mix(b);
        if (coin(u, 0, 5)) {
            static const int protos[] = {PROTO_ARTIFACT, 2, 4, 0, 1, 3, 5, 6};  // basic designs of each plan
            V->proto = (int16_t)protos[(u >> 3) % 8];
        }
        if (coin(u, 6, 3)) V->flags ^= VEH_FLAG_UNK_20;
        if (coin(u, 8, 3)) V->damage = (uint8_t)((u >> 10) % 8);
        if (coin(u, 13, 3)) V->morale = (uint8_t)((u >> 15) % 7);
        struct faction_data *F = &game_factions[faction];
        if (coin(u, 18, 6)) F->base_count = 0;
        if (coin(u, 21, 3)) {
            static const int counted[] = {PROTO_ARTIFACT, PROTO_ROVER, PROTO_CHOPPER, PROTO_FOIL};
            for (int i = 0; i < 4; i++) F->units_active[counted[i]] = (uint8_t)((mix(u + (uint32_t)i) >> 4) % 4);
        }
        if (coin(u, 24, 3)) *faction_word(faction, F_GOODY_EARTHQUAKE) = (int32_t)((u >> 26) % 4);
        if (coin(u, 28, 4)) F->se_pending[SE_RESEARCH] = (int32_t)((u >> 29) % 5) - 2;
        uint32_t s = mix(u);
        for (int i = 0; i < 5; i++)
            if (coin(s, i * 3, 3)) game_tech_achieved[techs[i]] ^= (uint8_t)(1u << ((s >> 16) % 8));
        if (coin(s, 20, 4)) *game_native_life = (int32_t)((s >> 22) % 3);
        vary_game(mix(s), faction, coin(s, 27, 6));
        *game_rand_state() = mix(s + 1);
        check_goody_box(v);
        sweep_restore(&saved);
    }
}

static void sweep_monoliths(int turn) {
    for (int k = 0; k < 16; k++) {
        uint32_t h = mix((uint32_t)turn * 2246822519u + (uint32_t)k), g = mix(h);
        int v = pick_unit(h);
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct sweep_saved saved;
        sweep_save(&saved);
        // Mostly units whose morale it may raise, later in the game, at a
        // monolith that may vanish.
        if (coin(g, 0, 3)) V->damage = (uint8_t)((g >> 2) % 8);
        if (coin(g, 5, 3)) V->state |= VEH_STATE_MONOLITH;
        else V->state &= ~(uint32_t)VEH_STATE_MONOLITH;
        if (!coin(g, 7, 3)) V->morale = (uint8_t)((g >> 9) % 6);
        if (coin(g, 12, 6)) V->proto = (int16_t)(PROTO_OGRE_1 + (g >> 15) % 3);
        if (coin(g, 17, 4)) V->order = coin(g, 19, 2) ? ORDER_MOVE_TO : 3;
        if (coin(g, 20, 3)) V->waypoint_x[0] = V->x, V->waypoint_y[0] = V->y;
        struct map_tile *t = tile(V->x, V->y);
        if (coin(g, 22, 3)) t->bits &= ~(uint32_t)MAP_MONOLITH_STAYS;
        else t->bits |= MAP_MONOLITH_STAYS;
        if (coin(g, 24, 3)) t->bits_seen[(g >> 26) % 7] |= MAP_MONOLITH;
        uint32_t s = mix(g);
        if (coin(s, 0, 4)) *game_more_preferences ^= MORE_PREF_AUTO_MONOLITH;
        vary_game(mix(s), V->faction, coin(s, 3, 5));
        if (!coin(s, 6, 3) && *game_turn < 100) *game_turn += 100;
        if (monolith_asks(v)) *game_more_preferences |= MORE_PREF_AUTO_MONOLITH;
        *game_rand_state() = mix(s + 7);
        check_monolith(v);
        sweep_restore(&saved);
    }
}

static void sweep_artifacts(int turn) {
    for (int k = 0; k < 8; k++) {
        uint32_t h = mix((uint32_t)turn * 3266489917u + (uint32_t)k), g = mix(h);
        int v = pick_unit(h);
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct sweep_saved saved;
        sweep_save(&saved);
        int32_t translator = game_secret_projects[SP_UNIVERSAL_TRANSLATOR];
        if (*game_base_count > 0 && !coin(g, 0, 4)) {
            int b = (int)((g >> 2) % (uint32_t)*game_base_count);
            struct base *B = &game_bases[b];
            veh_put(v, B->x, B->y);
            if (coin(g, 10, 2)) B->state ^= BASE_STATE_ARTIFACT;
            if (coin(g, 11, 2)) B->state ^= BASE_STATE_ARTIFACT_TOLD;
            if (coin(g, 12, 4)) game_secret_projects[SP_UNIVERSAL_TRANSLATOR] = b;
        }
        struct faction_data *F = &game_factions[V->faction];
        if (coin(g, 14, 2)) F->flags ^= FACTION_FLAG_ARTIFACT_STUDIED;
        if (coin(g, 15, 3)) F->ranking = (int32_t)((g >> 17) % 8);
        uint32_t s = mix(g);
        for (int i = 0; i < 6; i++)
            if (coin(s, i * 4, 2)) game_tech_achieved[(s >> (i * 4 + 1)) % TECH_COUNT] ^= (uint8_t)(1u << V->faction);
        vary_game(mix(s), V->faction, coin(s, 28, 5));
        *game_rand_state() = mix(s + 3);
        check_study_artifact(v);
        game_secret_projects[SP_UNIVERSAL_TRANSLATOR] = translator;
        sweep_restore(&saved);
    }
}

// A case of its own on the sweep's copy: made up, checked, put back.
static void case_begin(struct hook_writes *w) {
    *w = (struct hook_writes){.protect_only = 1};
    hooks_writes_game(w);
    hooks_writes_begin(w);
}

// game_random()'s generator, to find a seed that picks the base and the
// event: only the low 16 bits of the seed decide the next two picks.
static uint32_t random_step(uint32_t s) { return s * 0x19660du + 0x3c6ef35fu; }
static int random_pick(uint32_t s, int n) { return (int)((uint32_t)n * (s & 0xffff) >> 16); }
static uint32_t seed_for(uint32_t high, int base, int kind) {
    int n = *game_base_count > 100 ? *game_base_count : 100;
    for (uint32_t low = 0; low < 0x10000; low++) {
        uint32_t s = (high & 0xffff0000u) | ((low + high) & 0xffff), s1 = random_step(s);
        if (random_pick(s1, n) == base && random_pick(random_step(s1), 22) == kind) return s;
    }
    return high;
}

// The squares of the base's radius made known to the faction, or not, and
// its territory or another's or no one's, with things built on them (on
// land, at sea, or either).
static void vary_radius(const struct base *B, int faction, uint32_t h, uint32_t bits, int sea) {
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x, y;
        if (!event_square(B->x, B->y, i, &x, &y)) continue;
        struct map_tile *t = tile(x, y);
        uint32_t g = mix(h + (uint32_t)i * 0x9e3779b9u);
        if (!coin(g, 0, 4)) t->visibility |= (uint8_t)(1u << faction);
        if (coin(g, 2, 3)) t->territory = (int8_t)(coin(g, 4, 2) ? faction : (int)((g >> 5) % 8) - 1);
        if (bits && (sea < 0 || sea_at(t) == sea) && !coin(g, 8, 3)) t->bits |= bits;
        if (coin(g, 10, 4)) t->bits ^= MAP_RESOURCE;
        if (coin(g, 12, 2))  // every faction has seen them
            for (int j = 1; j < 8; j++) *seen_word(t, j) |= t->bits & bits;
    }
}
static void toggle_fac(struct base *B, int fac) {
    int offset;
    uint32_t mask;
    bitmask(fac, &offset, &mask);
    B->facilities_built[offset] ^= (uint8_t)mask;
}

// Each event forced by the seed, at a base of each faction in turn, with
// the conditions it looks at varied: the turn, the faction's rank, money,
// bases, techs and facilities, the satellites, sunspots, the squares
// around, Mount Planet nearby, who is the player. Then the turn's
// countdowns, perihelion and the AI's watch on humans (flag 0), with the
// seed coming from the map's.
static void sweep_random_events(int turn) {
    static const int32_t credits[] = {0, 250, 500, 501, 900, 1001, 1800, 2001, 5000};
    static const int labs[] = {EV_FAC_CRECHE, EV_FAC_ENERGY_BANK, EV_FAC_NETWORK_NODE, EV_FAC_BIOLOGY_LAB,
                               EV_FAC_RESEARCH_HOSPITAL, EV_FAC_NANOHOSPITAL};
    static const int projects[] = {SP_GENOME, SP_LONGEVITY, SP_IMMORTALITY, SP_ENERGY_GRID};
    static const uint32_t built_bits[] = {MAP_SOLAR, MAP_FOREST | MAP_FARM, MAP_MINE, MAP_FARM, MAP_MINE};
    int bases = *game_base_count;
    if (bases <= 0) return;
    for (int k = 0; k < 30; k++) {
        uint32_t h = mix((uint32_t)turn * 0x85ebca6bu + (uint32_t)k), g = mix(h ^ 0x27d4eb2fu), u = mix(g);
        uint32_t v = mix(u + 1), m = mix(v);
        // Each event, then the countdowns, then the volcano and the meteor
        // again.
        int kind = k < 22 ? (k + turn) % 22 : 14 + (k & 1), b = (int)(h % (uint32_t)bases), countdowns = k >= 22 && k < 28;
        struct base *B = &game_bases[b];
        int f = B->faction;
        struct faction_data *F = &game_factions[f];
        struct hook_writes w;
        case_begin(&w);
        if (!coin(h, 8, 8)) B->pop_size = (int8_t)(B->pop_size > 3 ? B->pop_size : 4 + (h >> 11) % 8);
        if (!coin(h, 14, 8)) B->event &= ~(uint32_t)BASE_EVENTS;
        if (F->base_count <= 1 && !coin(h, 17, 8)) F->base_count = 2 + (int32_t)((h >> 20) % 3);
        if (!coin(g, 0, 4)) F->ranking = (int32_t)((g >> 2) % 8);
        if (!coin(g, 5, 4)) *game_turn = 75 + (int32_t)((g >> 7) % 230);
        else if (coin(g, 15, 2)) *game_turn = 40 + (int32_t)((g >> 16) % 60);
        *game_current_faction = coin(g, 23, 2) ? f : (int32_t)((g >> 24) % 7) + 1;
        if (coin(g, 27, 10)) *game_pbem = 1;
        if (coin(u, 0, 3)) *game_human_factions ^= (uint8_t)(1u << f);
        if (coin(u, 2, 3)) F->diff_level = (int32_t)((u >> 4) % 6);
        if (coin(u, 7, 3)) game_faction_rankings[7] = f;
        if (coin(u, 9, 2)) B->visibility ^= (uint8_t)(1u << *game_current_faction);
        if (coin(u, 10, 3)) F->flags ^= FACTION_FLAG_MAP_REVEALED;
        if (coin(u, 12, 30)) *game_rules ^= RULES_BELL_CURVE;
        F->energy_credits = credits[(u >> 14) % 9];
        for (int i = 1; i < 8; i++)
            for (int j = 0; j < 4; j++) game_factions[i].satellites[j] = (int32_t)(mix(u + (uint32_t)(i * 4 + j)) % 3);
        if (!coin(u, 18, 4)) *game_solar_flares = 0;
        *game_sunspot_duration = 3 - (int32_t)((u >> 20) % 130);
        // The facilities that spare a base or bring a bonus, and their techs.
        for (int i = 0; i < 6; i++) {
            if (coin(v, i * 2, 3)) toggle_fac(B, labs[i]);
            int preq = game_facilities[labs[i]].preq_tech;
            if (preq >= 0 && preq < TECH_COUNT && !coin(v, 12 + i, 4)) game_tech_achieved[preq] |= (uint8_t)(1u << f);
        }
        for (int i = 0; i < 4; i++)
            if (coin(v, 18 + i, 5)) game_secret_projects[projects[i]] = coin(v, 22 + i, 2) ? b : -1;
        if (coin(v, 26, 3)) B->assimilation_turns = 0;
        B->surplus[RESOURCE_NUTRIENT] = (int32_t)((v >> 28) % 5) - 1;
        int rate = tech_rate(f);  // research done, around the quarter a network needs
        *faction_word(f, F_TECH_ACCUMULATED) =
            coin(m, 23, 2) ? rate / 4 + (int)((m >> 24) % 4) - 1 : rate * (int)((m >> 24) % 7) / 4;
        int which = kind == 13 ? 0 : kind == 16 ? 1 : kind == 17 ? 2 : kind == 20 ? 3 : kind == 21 ? 4 : (int)(m % 5);
        vary_radius(B, f, m, coin(m, 3, 4) ? 0 : built_bits[which], which >= 3);
        if (kind == 14 || kind == 15 || coin(m, 5, 8)) {
            if (!coin(m, 8, 6)) F->base_count = 8 + (int32_t)((m >> 9) % 4);
            if (!coin(m, 11, 4)) F->ranking = 6 + (int32_t)((m >> 13) % 2);
            if (!coin(m, 14, 6)) *game_state &= ~(uint32_t)STATE_SCENARIO;
            if (!coin(m, 15, 6)) *game_turn = 75 + (int32_t)((m >> 16) % 200);
        }
        if (kind == 14 && !coin(m, 20, 6)) {  // Mount Planet near the base, the base on any of its rings
            static const int rings[] = {0, 8, 9, 24, 25, 48, 49, 80};  // where the damage changes
            uint32_t q = mix(m + 3);
            int r = coin(q, 0, 2) ? rings[(q >> 1) % 8] : (int)((q >> 4) % 81);
            int mx = xrange(B->x - game_radius_x[r]), my = B->y - game_radius_y[r];
            if (on_map(mx, my)) {
                *game_mount_planet_x = mx, *game_mount_planet_y = my;
                tile(mx, my)->bits2 = (tile(mx, my)->bits2 & ~MAP2_VOLCANO_LIVE) | MAP2_VOLCANO;
                if (!coin(m, 26, 4)) tile(mx, my)->visibility |= (uint8_t)(1u << f);
            }
        }
        if (kind == 15 && !coin(m, 28, 4)) {  // land around but 3 to 7 squares of sea, and no landmark near
            int sea_left = 3 + (int)((m >> 29) % 5);
            for (int i = 0; i < RADIUS_SQUARES; i++) {
                int x, y;
                if (!event_square(B->x, B->y, i, &x, &y)) continue;
                struct map_tile *t = tile(x, y);
                t->bits2 &= ~(uint32_t)MAP2_LANDMARK_CODE;
                if (i >= 49 || !sea_at(t)) continue;
                if (i >= 25 && sea_left > 0) sea_left--;
                else t->climate = (uint8_t)((t->climate & 0x1f) | ALT_SHORE);
            }
        }
        uint32_t seed = seed_for(mix(m + 2), b, kind);
        if (countdowns) {
            // The turn's countdowns: perihelion's years come round, events
            // run out at bases, sunspots and the dust cloud end, and some
            // of the AI's factions are made human.
            static const int turns[] = {89, 109, 169, 189, 249, 90, 110};
            uint32_t y = mix(seed);
            if (!coin(y, 0, 3)) *game_turn = turns[(y >> 2) % 7];
            if (coin(y, 5, 2)) *game_state ^= GAME_STATE_PERIHELION;
            if (coin(y, 6, 3)) *game_solar_flares = (int32_t)((y >> 7) % 4);
            if (coin(y, 9, 3)) *game_sunspot_duration = (int32_t)((y >> 10) % 3) - (coin(y, 12, 2) ? 0 : 1001);
            if (coin(y, 13, 2)) *game_dust_cloud = (int32_t)((y >> 14) % 3) - 1;
            for (int i = 0; i < bases && i < 64; i++) {
                uint32_t z = mix(y + (uint32_t)i);
                if (!coin(z, 0, 3)) continue;
                game_bases[i].event |= (uint32_t)BASE_EVENT_BUMPER << (z >> 2) % 6;
                game_bases[i].random_event_turns = (int32_t)((z >> 5) % 3) - 1;
            }
            *game_human_factions = (uint8_t)(mix(y + 99) & 0xfe);
            *game_map_seed = (int32_t)(seed - (uint32_t)*game_turn * 13u);
        } else
            *game_random_seed = seed;
        *game_rand_state() = mix(seed + 5);
        check_random_events(!countdowns);
        hooks_writes_abandon(&w);
    }
}

// An AI faction for the sweep to put out of the game this turn, or 0.
static int pick_doomed(int turn) {
    for (int k = 0; k < 7; k++) {
        int f = (turn + k) % 7 + 1;
        if ((*game_factions_alive & (1u << f)) && !game_is_human(f)) return f;
    }
    return 0;
}

// The faction's bases handed to another, so it has none, then put out of
// the game by another faction, the player or no one, early in the game
// (when it may escape) or late, under each rule that decides that, with
// the movie preference and an infiltrator varied.
static void sweep_eliminate(int turn, int f) {
    for (int k = 0; k < 3; k++) {
        uint32_t h = mix((uint32_t)turn * 0xc2b2ae35u + (uint32_t)k), g = mix(h);
        int other = 0;
        for (int i = 0; i < 7 && !other; i++) {
            int o = (int)((h + (uint32_t)i) % 7) + 1;
            if (o != f && (*game_factions_alive & (1u << o))) other = o;
        }
        if (!other) return;
        struct hook_writes w;
        case_begin(&w);
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == f) game_bases[b].faction = (uint8_t)other;
        int killers[4] = {0, -1, other, *game_current_faction};
        int killer = killers[(h >> 3) % 4];
        if (killer == f) killer = other;
        if (coin(h, 5, 2) && killer > 0) *game_current_faction = killer;
        if (coin(h, 6, 3)) *game_turn = 20 + (int32_t)((h >> 8) % 81);
        else if (coin(h, 7, 2)) *game_turn = 101 + (int32_t)((h >> 8) % 200);
        if (coin(g, 0, 4)) *game_rules ^= RULES_DO_OR_DIE;
        if (coin(g, 2, 2)) *game_preferences ^= PREF_PROJECT_MOVIES;
        if (coin(g, 3, 2)) game_factions[*game_current_faction].diplo_status[f] ^= TREATY_INFILTRATOR;
        *game_rand_state() = mix(g + 1);
        *game_random_seed = mix(g + 2);
        check_eliminate_player(f, killer);
        hooks_writes_abandon(&w);
    }
}

// Factions' social models changed and put into effect (as a human's or
// the AI's), the screen skipped where the rules leave social engineering
// out, each faction file's bonuses worked out (some made up), and last the
// files' rules cleared.
static void sweep_social(int turn) {
    for (int k = 0; k < 4; k++) {
        uint32_t h = mix((uint32_t)turn * 0x165667b1u + (uint32_t)k);
        int f = (int)((h >> 4) % 7) + 1;
        if (!(*game_factions_alive & (1u << f))) continue;
        struct hook_writes w;
        case_begin(&w);
        struct faction_data *F = &game_factions[f];
        for (int i = 0; i < 4; i++)
            if (coin(h, 8 + i * 2, 2)) F->se_models_pending[i] = (int32_t)(mix(h + (uint32_t)i) % 4);
        if (coin(h, 16, 2)) *game_human_factions ^= (uint8_t)(1u << f);
        if (coin(h, 17, 2)) *game_current_faction = f;
        check_social_set(f);
        *game_state &= ~0xc0u;
        *game_more_rules |= MORE_RULES_NO_SOCIAL;
        check_social_select(f);
        hooks_writes_abandon(&w);
    }
    for (int f = 0; f < FACTION_COUNT; f++) {
        struct faction_def *d = &game_faction_defs[f];
        uint32_t h = mix((uint32_t)turn * 0x27d4eb2du + (uint32_t)f);
        if (coin(h, 0, 2)) {
            d->bonus_count = (int32_t)((h >> 1) % 9);
            for (int i = 0; i < d->bonus_count; i++) {
                uint32_t g = mix(h + (uint32_t)i);
                d->bonus_id[i] = coin(g, 0, 2) ? BONUS_SOCIAL : (int32_t)((g >> 1) % 15);
                d->bonus_val1[i] = (int32_t)((g >> 5) % SE_COUNT);
                d->bonus_val2[i] = (int32_t)((g >> 9) % 7) - 3;
            }
        }
        check_compute_faction_modifiers(f);
    }
    for (int f = 0; f < FACTION_COUNT; f++) check_clear_faction(&game_faction_defs[f]);
}

// Once a turn, on a protected copy of the game. A base the faction put out
// of the game may found is named first, as naming opens the faction's file
// (see shadow_eliminate_player).
static void events_sweep(void) {
    if (!*game_map || *game_veh_count <= 0) return;
    int sweeping = hooks_sweeping, turn = *game_turn, doomed = pick_doomed(turn);
    hooks_sweeping = 1;
    if (doomed) hold_base_name(doomed);
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    sweep_pods(turn);
    sweep_monoliths(turn);
    sweep_artifacts(turn);
    sweep_random_events(turn);
    if (doomed) sweep_eliminate(turn, doomed);
    sweep_social(turn);
    hooks_writes_abandon(&outer);
    name_base_release();
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook events_hooks[] = {
    HOOK(0x57a660, goody_box, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),       // sub esp, 0x34
    HOOK(0x57a050, monolith, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x5c),        // sub esp, 0x5c
    HOOK(0x57cfa0, study_artifact, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x60),  // sub esp, 0x60
    HOOK(0x51f2e0, random_events, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),   // sub esp, 0x20
    HOOK(0x5b3380, eliminate_player, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0xac, 0x00, 0x00, 0x00),  // sub esp, 0xac
    HOOK(0x585fe0, clear_faction, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),   // mov eax, [ebp+8]
    HOOK(0x5b0d70, compute_faction_modifiers, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x08),  // mov edx, [ebp+8]
    HOOK(0x5b4600, social_set, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),      // sub esp, 0x10
    HOOK(0x5b5620, social_select, PUSH_EBP_MOV_EBP_ESP, 0xa0, 0xc0, 0x64, 0x9a, 0x00),  // mov al, [0x9a64c0]
};
// The game calls setup.c's functions mostly as a game starts and as a faction
// is put out (eliminate_player): their sweep runs after this one.
extern struct hook_module setup_module;
struct hook_module events_module = {"events", events_hooks, HOOK_COUNT(events_hooks), events_sweep, &setup_module, -1};
