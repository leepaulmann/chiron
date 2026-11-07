// What units find on the map: supply and unity pods (goody_box), monoliths,
// and the alien artifacts they bring home (study_artifact). Reimplemented
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

#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "shadow.h"
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
    MORE_RULES_NO_UNITS = 0x1, MORE_RULES_NO_TECH = 0x2,
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

// Once a turn, on a protected copy of the game.
static void events_sweep(void) {
    if (!*game_map || *game_veh_count <= 0) return;
    int sweeping = hooks_sweeping, turn = *game_turn;
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    sweep_pods(turn);
    sweep_monoliths(turn);
    sweep_artifacts(turn);
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook events_hooks[] = {
    HOOK(0x57a660, goody_box, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),       // sub esp, 0x34
    HOOK(0x57a050, monolith, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x5c),        // sub esp, 0x5c
    HOOK(0x57cfa0, study_artifact, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x60),  // sub esp, 0x60
};
struct hook_module events_module = {"events", events_hooks, HOOK_COUNT(events_hooks), events_sweep, NULL, -1};
