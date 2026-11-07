// Spotting: what each faction has seen of the map, of bases and of units.
// Reimplemented from terranx.exe 2.0's disassembly; names follow OpenSMACX
// (Brendan Casey, GPL-3.0-or-later) and Thinker (MIT), whose spot_all() this
// follows in outline. Where Thinker "fixes" the original (bounds on a
// square's owner, which faction's visibility the deep radar pass looks at)
// the original's behaviour is kept here.
#include "spot.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "veh.h"

enum {
    ABL_DEEP_RADAR = 0x2, ABL_CLOAKED = 0x4, ABL_DEEP_PRESSURE_HULL = 0x40,
    VEH_FLAG_LURKER = 0x40,  // with VEH_FLAG_INVISIBLE, cleared when another faction sees the unit
    BSC_SPORE_LAUNCHER = 15, BSC_FUNGAL_TOWER = 19, BASIC_PROTOS = 64, PLAN_PROBE = 11,
    MORE_PREF_FOG = 0x1,  // the map shows the fog of war
    // map_tile.bits2: a square drawn as in view, and a square the player's
    // faction has just seen for the first time (the bit game.h knows as
    // MAP2_CHANGED; the map view clears it when it redraws).
    MAP2_IN_VIEW = 0x200000, MAP2_NEWLY_SEEN = 0x400000, MAP2_LANDMARK_OFF = 0x80000000u,
    VEH_STATE_ON_ALERT = 0x200, VEH_STATE_PATROL = 0x2000000, VEH_STATE_SENTRY_ALERT = 0x1000000,
    VEH_STATE_EXPLORE = 0x4000, VEH_STATE_TARGETED = 0x40000, VEH_STATE_RETARGET = 0x20000,
    ORDER_NONE = 0, ORDER_MOVE_TO = 0x18,
    TREATY_TRUCE = 0x4, TREATY_ANY = TREATY_PACT | TREATY_TREATY | TREATY_TRUCE,
    INTERLUDE_NEXUS = 41,
};

static inline int in_map(int x, int y) {
    return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width;
}
static inline struct map_tile *tile_at(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int is_sea(const struct map_tile *t) { return (t->climate & 0xe0) < ALT_SHORE_LINE << 5; }
// The faction marked on the square for a base or a unit (by `bits`), or -1.
static inline int owner_of(const struct map_tile *t, uint32_t bits) {
    if (!(t->bits & bits)) return -1;
    int o = t->owner_site & 0xf;
    return o < 8 ? o : -1;
}
// What a faction last saw of the square's bits. The game's word for
// faction 0 is bits2, which only the player's faction would read.
static inline uint32_t *seen_bits(struct map_tile *t, int faction) {
    return (uint32_t *)((uint8_t *)t + offsetof(struct map_tile, bits2)) + faction;
}
// A faction's treaties with another, read as the original does, without
// checking either: a square's owner of -1 reads the word before.
static inline uint32_t treaty(int faction, int other) {
    uintptr_t p = (uintptr_t)game_factions + (uintptr_t)(intptr_t)faction * sizeof(struct faction_data) +
                  offsetof(struct faction_data, diplo_status) + (uintptr_t)(intptr_t)other * 4;
    return *(const uint32_t *)p;
}
static inline uint8_t faction_bit(int faction) { return (uint8_t)(1u << (faction & 31)); }
static inline const struct rules_chassis *chassis_of(int veh) {
    return &game_chassis[game_protos[game_vehs[veh].proto].chassis];
}

// Whether a unit hides: 1 for one that is cloaked, has a deep pressure
// hull or is marked invisible; 2 for one in fungus (but a fungal tower),
// unless the fungus is deep at sea; else 0.
static int hides(int veh) {
    const struct veh *V = &game_vehs[veh];
    if (has_abil(V->proto, ABL_DEEP_PRESSURE_HULL) || has_abil(V->proto, ABL_CLOAKED) || (V->flags & VEH_FLAG_INVISIBLE))
        return 1;
    const struct map_tile *t = tile_at(V->x, V->y);
    if ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 2 << 5 && V->proto != BSC_FUNGAL_TOWER) return 2;
    return 0;
}

// The square seen by a faction. For the player's own faction a square it
// had not seen is marked to be drawn, unless the whole map is shown (the
// omniscient view, or a faction with the map revealed).
static void see_tile(struct map_tile *t, int faction) {
    if (faction == *game_current_faction && !(t->visibility & faction_bit(faction)) &&
        !(*game_state & GAME_STATE_OMNISCIENT) && !(game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED)) {
        t->bits2 |= MAP2_NEWLY_SEEN;
        *game_draw_state |= DRAW_TERRAIN;
    }
    t->visibility |= faction_bit(faction);
}

// The original at 0x5b5700, unnamed in the references; spot_base(),
// spot_stack() and spot_loc() inline it.
void spot_square(int x, int y, int faction) {
    if (!in_map(x, y)) return;
    see_tile(tile_at(x, y), faction);
    synch_bit(x, y, faction);
}

void spot_base(int base, int faction) {
    struct base *B = &game_bases[base];
    B->visibility |= faction_bit(faction);
    B->pop_seen[faction] = B->pop_size;
    spot_square(B->x, B->y, faction);
}

// The units of a stack seen by a faction; any but the natives also see
// through their cloaks.
static void see_stack(int veh, int faction) {
    for (int v = veh_top(veh); v >= 0; v = game_vehs[v].next_in_stack) {
        struct veh *V = &game_vehs[v];
        V->visibility |= faction_bit(faction);
        if (faction) V->flags &= (uint16_t)~(VEH_FLAG_INVISIBLE | VEH_FLAG_LURKER);
    }
}

void spot_stack(int veh, int faction) {
    if (veh >= 0) spot_square(game_vehs[veh].x, game_vehs[veh].y, faction);
    see_stack(veh, faction);
}

void unspot_stack(int veh) {
    for (int v = veh_top(veh); v >= 0; v = game_vehs[v].next_in_stack) game_vehs[v].visibility = 0;
}

void spot_loc(int x, int y, int faction) {
    spot_square(x, y, faction);
    int base = base_at(x, y);
    if (base >= 0) spot_base(base, faction);
    else spot_stack(veh_at(x, y), faction);
}

// A unit that sees an enemy drops its alert: a patrol not yet done, or a
// sentry on alert, comes off it, exploring stops and a move is cancelled.
static void stop_alert(struct veh *V) {
    if ((V->state & (VEH_STATE_PATROL | VEH_STATE_ON_ALERT)) == (VEH_STATE_PATROL | VEH_STATE_ON_ALERT) &&
        V->patrol_point < V->waypoint_count)
        V->state &= ~(uint32_t)(VEH_STATE_PATROL | VEH_STATE_ON_ALERT);
    if ((V->state & (VEH_STATE_SENTRY_ALERT | VEH_STATE_ON_ALERT)) == (VEH_STATE_SENTRY_ALERT | VEH_STATE_ON_ALERT))
        V->state &= ~(uint32_t)(VEH_STATE_SENTRY_ALERT | VEH_STATE_ON_ALERT);
    V->state &= ~(uint32_t)VEH_STATE_EXPLORE;
    if (V->order == ORDER_MOVE_TO) V->order = ORDER_NONE;
}

static inline void at_least(int *draw, int level) {
    if (*draw <= level) *draw = level;
}

// Whether a faction keeps watch over the square (x, y), for the fog of war
// around its unit at (vx, vy): a base or unit of its own (or of a pact
// partner, holding one of its units) next to it; a sensor of its own
// territory within two squares, or any sensor there while the base nearest
// the unit, at most two squares away on both axes, has a geosynchronous
// survey pod; its base within two squares; or a unit of its own with deep
// radar within two squares, of a triad that sees it.
static int watched(int faction, int vx, int vy, int x, int y) {
    for (int j = 0; j < 25; j++) {
        int x3 = xrange(x + game_radius_x[j]), y3 = y + game_radius_y[j];
        if (!in_map(x3, y3)) continue;
        const struct map_tile *t = tile_at(x3, y3);
        if (j < 9) {
            int owner = owner_of(t, MAP_BASE | MAP_UNIT);
            if (owner == faction) return 1;
            if (treaty(faction, owner) & TREATY_PACT)
                for (int k = veh_at(x3, y3); k >= 0; k = game_vehs[k].next_in_stack)
                    if (game_vehs[k].faction == faction) return 1;
        }
        if (is_sensor(x3, y3)) {
            if (t->territory == faction) return 1;
            int b = base_find(vx, vy);
            if (b != -1) {
                const struct base *B = &game_bases[b];
                int dx = abs(vx - B->x);
                if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
                int dy = abs(vy - B->y);
                if ((dx == 0 || dx == 2) && (dy == 0 || dy == 2) && game_has_facility(B, FAC_GEOSYNC_SURVEY_POD)) return 1;
            }
        }
        if (owner_of(t, MAP_BASE) == faction) return 1;
        int owner = owner_of(t, MAP_UNIT);
        if (owner != faction && !(treaty(owner, faction) & TREATY_PACT)) continue;
        for (int k = veh_top(veh_at(x3, y3)); k >= 0; k = game_vehs[k].next_in_stack) {
            const struct veh *K = &game_vehs[k];
            if (K->faction != faction || !has_abil(K->proto, ABL_DEEP_RADAR)) continue;
            int tr = game_veh_triad(k);
            if (tr == TRIAD_AIR || is_sea(tile_at(x, y)) == (tr == TRIAD_SEA)) return 1;
        }
    }
    return 0;
}

// What spot_all() works with.
struct spotter {
    int veh, flag, x, y, faction, triad, stealth, radar, on_ocean;
    uint8_t bit;
};

// A sensor's square near the unit: its territory's owner (another player
// than the unit's) sees the unit unless it is cloaked or the square holds a
// base, and without a treaty the AI sets goals there.
static void spot_sensor(const struct spotter *s, int x, int y) {
    if (!is_sensor(x, y)) return;
    const struct map_tile *t = tile_at(x, y);
    int owner = t->territory;
    if (owner <= 0 || owner == s->faction || s->stealth == 1 || owner_of(t, MAP_BASE) >= 0) return;
    spot_stack(s->veh, owner);
    if ((treaty(s->faction, owner) & TREATY_ANY) || !s->flag) return;
    if (s->triad == TRIAD_LAND) {
        game_add_goal(owner, GOAL_ATTACK, 2, x, y, -1);
        game_add_goal(owner, GOAL_DEFEND, 1, x, y, -1);
    } else {
        game_add_goal(owner, GOAL_UNK_6, 2, x, y, -1);
    }
}

// The fog of war around the player's own unit: how far the view must be
// redrawn for squares whose "in view" mark no longer matches. Those in its
// sight are in view; beyond, out to two squares (three with radar), a
// square is in view while the faction watches it from elsewhere.
static void fog(const struct spotter *s, int *draw) {
    int visible_end = s->radar ? 25 : 9, scan_end = s->radar ? 49 : 25;
    for (int i = 0; i < scan_end; i++) {
        int x = xrange(s->x + game_radius_x[i]), y = s->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        int seen = 1;
        if (i >= visible_end) {
            if (tile_at(x, y)->bits2 & MAP2_IN_VIEW) continue;
            seen = watched(s->faction, s->x, s->y, x, y);
        }
        if (!(tile_at(x, y)->bits2 & MAP2_IN_VIEW) != !seen) continue;
        at_least(draw, i < 9 ? 1 : i < 25 ? 2 : 3);
    }
}

// The squares in the unit's sight: next to it, and two squares out (with
// radar) for those it can see across (an air unit, or land from land and
// sea from sea). Counts the Manifold Nexus squares among them.
static int sight(const struct spotter *s, int *draw) {
    int visible_end = s->radar ? 25 : 9, nexus = 0;
    for (int i = 0; i < visible_end; i++) {
        int x = xrange(s->x + game_radius_x[i]), y = s->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        struct map_tile *t = tile_at(x, y);
        if ((t->bits2 & (MAP2_NEXUS | MAP2_LANDMARK_OFF)) == MAP2_NEXUS) nexus++;
        if (i >= 9 && s->triad != TRIAD_AIR && is_sea(t) != (s->triad == TRIAD_SEA)) continue;
        if (s->faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT) &&
            !(game_factions[s->faction].flags & FACTION_FLAG_MAP_REVEALED)) {
            if (!(t->visibility & s->bit)) {
                t->bits2 |= MAP2_NEWLY_SEEN;
                *game_draw_state |= DRAW_TERRAIN;
            }
            if (((*seen_bits(t, s->faction) ^ t->bits) & ~(uint32_t)(MAP_BASE | MAP_UNIT)) || !(t->visibility & s->bit))
                at_least(draw, i >= 9 ? 3 : 2);
        }
        if (s->faction) *seen_bits(t, s->faction) = t->bits;
        t->visibility |= s->bit;
    }
    return nexus;
}

// Beyond its sight, out to two squares (three with radar), a square the
// faction hasn't seen is filled in when enough of its neighbours have
// been: fewer than 4 unseen (6 at sea or with radar), sides counting twice.
static void fill_in(const struct spotter *s, int *draw) {
    int visible_end = s->radar ? 25 : 9, scan_end = s->radar ? 49 : 25;
    for (int i = visible_end; i < scan_end; i++) {
        int x = xrange(s->x + game_radius_x[i]), y = s->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        struct map_tile *t = tile_at(x, y);
        if (t->visibility & s->bit) continue;
        int unseen = 0;
        for (int j = 0; j < 8; j++) {
            int dx = game_radius_near_x[j], dy = game_radius_near_y[j];
            int x3 = xrange(x + dx), y3 = y + dy;
            if (!in_map(x3, y3) || (tile_at(x3, y3)->visibility & s->bit)) continue;
            unseen++;
            if (!dx || !dy) unseen++;
        }
        if (unseen >= (s->radar || is_sea(t) ? 6 : 4)) continue;
        if (s->faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT) &&
            !(game_factions[s->faction].flags & FACTION_FLAG_MAP_REVEALED)) {
            t->bits2 |= MAP2_NEWLY_SEEN;
            *game_draw_state |= DRAW_TERRAIN;
        }
        if (s->faction) *seen_bits(t, s->faction) = t->bits;
        t->visibility |= s->bit;
        int b = base_at(x, y);
        if (b >= 0) spot_base(b, s->faction);
        at_least(draw, s->radar ? 3 : 2);
    }
}

// What spot_all() gathers for the end: factions met, and whether a fungal
// tower or a native unit was among them.
struct meeting {
    int mask, tower, native;
};

// The squares next to the unit: other factions' bases and units it sees
// and that see it, the units woken, and the goals the AI sets.
static void adjacent(const struct spotter *s, int *draw, struct meeting *m) {
    struct veh *V = &game_vehs[s->veh];
    int faction = s->faction;
    for (int i = 0; i < 9; i++) {
        int x = xrange(s->x + game_radius_x[i]), y = s->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        spot_sensor(s, x, y);
        if (!i) continue;
        int b = base_at(x, y);
        if (b >= 0 && game_bases[b].faction != faction) {
            struct base *B = &game_bases[b];
            int bf = B->faction;
            if (s->triad != TRIAD_AIR || !chassis_of(s->veh)->range) {
                if (!(s->on_ocean || s->stealth) || !(treaty(faction, bf) & TREATY_COMMLINK)) m->mask |= 1 << bf;
            }
            if (faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT)) {
                if (!(B->visibility & s->bit)) at_least(draw, 2);
                else if ((uint8_t)B->pop_seen[faction] != B->pop_size) at_least(draw, 1);
            }
            spot_base(b, faction);
            if (!s->stealth || !(treaty(faction, bf) & TREATY_COMMLINK)) spot_stack(s->veh, bf);
            if (s->triad == TRIAD_LAND && !(treaty(bf, faction) & TREATY_ANY) && s->flag) {
                game_add_goal(bf, GOAL_ATTACK, 2, x, y, -1);
                game_add_goal(bf, GOAL_DEFEND, 1, x, y, -1);
            }
        }
        int target_ocean = is_sea(tile_at(x, y));
        if (has_abil(V->proto, ABL_DEEP_PRESSURE_HULL) && !target_ocean) continue;
        int other = stack_fix(veh_at(x, y));
        if (other < 0) continue;
        struct veh *O = &game_vehs[other];
        int of = O->faction;
        if (of == faction) continue;
        // Contact both ways, unless the unit hides from a faction it talks
        // to that has no deep radar there.
        if (!s->stealth || has_abil(O->proto, ABL_DEEP_RADAR) || !(treaty(faction, of) & TREATY_COMMLINK)) {
            if (b < 0) spot_stack(s->veh, of);
            if (!(treaty(faction, of) & TREATY_PACT) && s->flag) {
                wake_stack(other);
                if (game_is_human(of)) {
                    if (s->on_ocean == target_ocean && want_to_wake(faction, other, s->veh)) stop_alert(O);
                } else if (!(treaty(faction, of) & TREATY_ANY) && (target_ocean || !s->on_ocean)) {
                    stop_alert(O);
                }
            }
        }
        // A hidden unit stays unseen by a faction without radar that talks
        // to its owner, if it is alone (stack_check() type 1).
        if (hides(other) && !s->radar && stack_check(other, 1, -1, -1, -1) == 1 &&
            (treaty(faction, O->faction) & TREATY_COMMLINK))
            continue;
        if (b < 0) {
            if (faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT) && !(O->visibility & s->bit))
                at_least(draw, 1);
            spot_stack(other, faction);
        }
        if (game_is_human(faction)) {
            if ((s->on_ocean == target_ocean || s->triad == TRIAD_AIR) && want_to_wake(of, s->veh, other)) stop_alert(V);
        } else if (!(treaty(faction, of) & TREATY_ANY) && (s->on_ocean || !target_ocean || s->triad == TRIAD_AIR)) {
            stop_alert(V);
        }
        if (target_ocean && of && !(treaty(faction, of) & TREATY_ANY))
            game_add_goal(faction, GOAL_UNK_6, has_abil(O->proto, ABL_DEEP_PRESSURE_HULL | ABL_CLOAKED) ? 3 : 2, x, y, -1);
        if (!(treaty(faction, of) & TREATY_ANY)) game_add_goal(faction, GOAL_ATTACK, faction ? 3 : 1, x, y, -1);
        if (game_protos[V->proto].plan >= PLAN_COLONY && !(treaty(faction, of) & TREATY_PACT) &&
            whose_territory(of, x, y, NULL, 0) == of && game_protos[O->proto].plan >= PLAN_COLONY) {
            game_add_goal(of, GOAL_DEFEND, 2, x, y, -1);
            game_add_goal(of, GOAL_ATTACK, 1, x, y, -1);
            game_add_goal(of, GOAL_SCOUT, 1, x, y, -1);
        }
        // A meeting, unless either is an air unit with a range, or they are
        // on land and sea apart (but for a land unit beside a base), where
        // the factions already talk.
        if (s->triad == TRIAD_AIR && chassis_of(s->veh)->range) continue;
        if (chassis_of(other)->triad == TRIAD_AIR && chassis_of(other)->range) continue;
        if (s->on_ocean != target_ocean && !(b >= 0 && !s->on_ocean) && (treaty(faction, of) & TREATY_COMMLINK)) continue;
        if (O->proto == BSC_FUNGAL_TOWER) m->tower = 1;
        else if (O->proto < BASIC_PROTOS &&
                 (game_weapons[game_protos[O->proto].weapon].offense < 0 || O->proto == BSC_SPORE_LAUNCHER))
            m->native = 1;
        m->mask |= 1 << of;
    }
}

// Two squares out: what radar sees, deep radar seeing the unit, and bases
// that see it coming.
static void two_squares(const struct spotter *s, int *draw) {
    struct veh *V = &game_vehs[s->veh];
    struct map_tile *vt = tile_at(s->x, s->y);
    int faction = s->faction;
    for (int i = 9; i < 25; i++) {
        int x = xrange(s->x + game_radius_x[i]), y = s->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        spot_sensor(s, x, y);
        struct map_tile *t = tile_at(x, y);
        // An empty square (-1) carries on, with the treaties read before
        // the faction's (Thinker skips it).
        int owner = owner_of(t, MAP_BASE | MAP_UNIT);
        if (owner == faction) continue;
        int other = stack_fix(veh_at(x, y));
        int b = base_at(x, y);
        if (s->radar && (s->triad == TRIAD_AIR || is_sea(t) == (s->triad == TRIAD_SEA))) {
            if (b >= 0) {
                const struct base *B = &game_bases[b];
                if (faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT)) {
                    if (!(B->visibility & s->bit)) at_least(draw, 3);
                    else if ((uint8_t)B->pop_seen[faction] != B->pop_size) at_least(draw, 2);
                }
                spot_base(b, faction);
            }
            if (other >= 0 && b < 0 && !hides(other)) {
                // The original looks at whether the faction sees its own
                // unit, not the one spotted (which Thinker fixes).
                if (faction == *game_current_faction && !(*game_state & GAME_STATE_OMNISCIENT) && !(V->visibility & s->bit))
                    at_least(draw, 2);
                spot_stack(other, faction);
                if (is_sea(t) && owner && !(treaty(faction, owner) & TREATY_TREATY))
                    game_add_goal(faction, GOAL_UNK_6, 2, x, y, -1);
            }
        }
        if (other >= 0 && !s->stealth && has_abil(game_vehs[other].proto, ABL_DEEP_RADAR)) {
            int ot = game_veh_triad(other);
            if (ot == TRIAD_AIR || (ot == TRIAD_SEA) == s->on_ocean) {
                if (!(treaty(faction, owner) & TREATY_PACT) && s->flag) wake_stack(other);
                if (owner_of(vt, MAP_BASE) < 0) spot_stack(s->veh, owner);
                if (!is_sea(t) && owner && !(treaty(owner, faction) & TREATY_TREATY))
                    game_add_goal(owner, GOAL_UNK_6, 2, s->x, s->y, -1);
            }
        }
        if (b < 0) continue;
        // A base sees the unit if its faction has seen the unit's square,
        // the unit doesn't hide, and it is on the same land or sea (or
        // across the shore).
        struct faction_data *F = &game_factions[owner];
        if (!(F->flags & FACTION_FLAG_MAP_REVEALED) && !(vt->visibility & faction_bit(owner))) continue;
        if (s->stealth) continue;
        if (s->on_ocean == is_sea(t) && vt->region != t->region) continue;
        spot_stack(s->veh, owner);
        // A land unit at the base the faction targets: the target is
        // dropped and units sent there pick another.
        if (s->triad == TRIAD_LAND && vt->region == t->region && x == F->target_x && y == F->target_y) {
            F->target_y = F->target_x = -1;
            for (int k = veh_at(x, y); k >= 0; k = game_vehs[k].next_in_stack)
                if (game_vehs[k].state & VEH_STATE_TARGETED) game_vehs[k].state |= VEH_STATE_TARGETED | VEH_STATE_RETARGET;
        }
        if (game_protos[V->proto].plan == PLAN_PROBE && !(treaty(faction, owner) & TREATY_PACT)) {
            game_add_goal(owner, GOAL_ATTACK, 2, s->x, s->y, -1);
            game_add_goal(owner, GOAL_DEFEND, 1, s->x, s->y, -1);
            game_add_goal(owner, GOAL_SCOUT, 1, s->x, s->y, -1);
        }
    }
}

void spot_all(int veh, int flag) {
    if (!*game_net_multiplayer) flag = 1;
    struct veh *V = &game_vehs[veh];
    struct spotter s = {.veh = veh, .flag = flag, .x = V->x, .y = V->y, .faction = V->faction};
    s.triad = game_chassis[game_protos[V->proto].chassis].triad;
    s.stealth = hides(veh);
    s.bit = faction_bit(s.faction);
    s.radar = has_abil(V->proto, ABL_DEEP_RADAR) ||
              (is_sensor(s.x, s.y) && whose_territory(s.faction, s.x, s.y, NULL, 0) == s.faction);
    struct map_tile *vt = tile_at(s.x, s.y);
    s.on_ocean = is_sea(vt);
    int draw = 0;
    struct meeting m = {0};
    // A native unit is seen only by those who see its square.
    if (!s.faction) V->visibility &= vt->visibility;
    if ((*game_more_preferences & MORE_PREF_FOG) && s.faction == *game_current_faction &&
        !(*game_state & GAME_STATE_OMNISCIENT))
        fog(&s, &draw);
    int nexus = sight(&s, &draw);
    fill_in(&s, &draw);
    // Other factions' units in its stack see it.
    if (veh >= 0)
        for (int v = veh_top(veh); v >= 0; v = game_vehs[v].next_in_stack)
            if (game_vehs[v].faction != s.faction) spot_stack(v, game_vehs[v].faction);
    adjacent(&s, &draw, &m);
    two_squares(&s, &draw);
    if (!*game_turn) return;
    game_draw_radius(s.x, s.y, s.faction == *game_current_faction ? draw : 0, 2);
    if (s.faction == *game_current_faction && draw) game_graphic_win_redraw(game_world_win);
    if (nexus > 8 && s.faction == *game_current_faction && (game_faction_defs[s.faction].rule_flags & RULE_FLAG_ALIEN))
        game_interlude(INTERLUDE_NEXUS, NULL, 1, 0);
    if (m.mask & 1) game_encounter(s.faction, 0, s.x, s.y, m.tower, m.native);
    if (s.faction && flag)
        for (int i = 1; i < FACTION_COUNT; i++)
            if (i != s.faction && (m.mask & (1 << i))) game_encounter(s.faction, i, s.x, s.y, 0, 0);
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module spot_module;
#define SPOT (&spot_module)

static void writes_map(struct hook_writes *w) {
    if (*game_map) hooks_writes_add(w, *game_map, (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile));
    hooks_writes_add(w, game_draw_state, sizeof *game_draw_state);
}
#define WRITES_MAP writes_map(&w_)
#define WRITES_UNITS (writes_map(&w_), WRITES(game_vehs, (VEH_COUNT + 1) * sizeof(struct veh)))
#define WRITES_ALL (WRITES_UNITS, WRITES(game_bases, BASE_COUNT * sizeof(struct base)))

SHADOW_WRITER(SPOT, void, spot_square, (int a, int b, int c), (a, b, c), WRITES_MAP, "%d, %d, %d", a, b, c)
SHADOW_WRITER(SPOT, void, spot_base, (int a, int b), (a, b), (WRITES_MAP, WRITES(&game_bases[a], sizeof(struct base))),
              "%d, %d", a, b)
SHADOW_WRITER(SPOT, void, spot_stack, (int a, int b), (a, b), WRITES_UNITS, "%d, %d", a, b)
SHADOW_WRITER(SPOT, void, unspot_stack, (int a), (a), WRITES_UNITS, "%d", a)
SHADOW_WRITER(SPOT, void, spot_loc, (int a, int b, int c), (a, b, c), WRITES_ALL, "%d, %d, %d", a, b, c)

// Whether spot_all() could meet another faction: a base or unit of another
// faction next to the unit, when the game has begun. Meeting (encounter())
// may change anything in the game, so such a call's check covers all of
// it; any other writes units, bases, the map, the factions' goals and
// targets, and what the functions it calls write (base_find()'s distance,
// and the lifted unit's globals for stack_fix() in a network game).
static int may_meet(int veh) {
    if (!*game_turn) return 0;
    const struct veh *V = &game_vehs[veh];
    int f = V->faction;
    if (!f) return 0;
    for (int i = 1; i < 9; i++) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].x == x && game_bases[b].y == y && game_bases[b].faction != f) return 1;
        for (int v = 0; v < *game_veh_count; v++)
            if (game_vehs[v].x == x && game_vehs[v].y == y && game_vehs[v].faction != f) return 1;
    }
    return 0;
}
static void writes_spot_all(struct hook_writes *w, int veh) {
    if (may_meet(veh)) {
        hooks_writes_game(w);
        return;
    }
    writes_map(w);
    hooks_writes_add(w, game_vehs, (VEH_COUNT + 1) * sizeof(struct veh));
    hooks_writes_add(w, game_bases, BASE_COUNT * sizeof(struct base));
    hooks_writes_add(w, game_factions, FACTION_COUNT * sizeof(struct faction_data));
    hooks_writes_add(w, game_base_find_dist, sizeof *game_base_find_dist);
    hooks_writes_add(w, game_veh_lift_x, 0x18);  // to game_veh_at_rebuilt
}
// While any faction is human, a call that may meet another faction runs
// the original unchecked: meeting the player asks them questions, whose
// answers a check would leave unanswered.
SHADOW_WRITER_CHECK(void, spot_all, (int a, int b), (a, b), orig_spot_all(a, b), writes_spot_all(&w_, a), "%d, %d", a, b)
static void shadow_spot_all(int a, int b) {
    if (hooks_nested) {
        orig_spot_all(a, b);
        return;
    }
    if (shadow_bypass()) {
        spot_all(a, b);
        return;
    }
    stats_spot_all.calls++;
    shadow_tick(SPOT);
    if (*game_human_factions && may_meet(a)) orig_spot_all(a, b);
    else check_spot_all(a, b);
}

// ---- The sweep ---------------------------------------------------------------

// A unit to sweep: alive, on the map, in a stack whose links hold.
static int sweepable(int veh) {
    if (veh < 0 || veh >= *game_veh_count) return 0;
    const struct veh *V = &game_vehs[veh];
    if (!in_map(V->x, V->y)) return 0;
    for (int v = veh, n = 0; v >= 0; v = game_vehs[v].prev_in_stack)
        if (v >= *game_veh_count || ++n > *game_veh_count) return 0;
    for (int v = veh, n = 0; v >= 0; v = game_vehs[v].next_in_stack)
        if (v >= *game_veh_count || ++n > *game_veh_count) return 0;
    return 1;
}

// The squares around (x, y) out to `n` of the radius table forgotten by a
// faction, so that it sees them anew.
static void forget(int x, int y, int n, int faction) {
    for (int i = 0; i < n; i++) {
        int x2 = xrange(x + game_radius_x[i]), y2 = y + game_radius_y[i];
        if (in_map(x2, y2)) tile_at(x2, y2)->visibility &= (uint8_t)~faction_bit(faction);
    }
}

// The small functions: squares (some off the map), bases and stacks seen
// by each faction, as the player's faction or not, with the map shown whole
// or not, and with their squares unseen.
static void sweep_small(int t) {
    int vehs = *game_veh_count, bases = *game_base_count, w = *game_map_width, h = *game_map_height;
    int32_t current = *game_current_faction;
    uint32_t state = *game_state;
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int pass = 0; pass < 4; pass++) {
            *game_current_faction = pass ? f : current;
            *game_state = pass == 2 ? state | GAME_STATE_OMNISCIENT : state & ~(uint32_t)GAME_STATE_OMNISCIENT;
            uint32_t flags = game_factions[f].flags;
            if (pass == 3) game_factions[f].flags ^= FACTION_FLAG_MAP_REVEALED;
            for (int k = 0; k < 4; k++) {
                int y = (t * 7 + k * 13 + f) % h, x = ((t * 11 + k * 23) % w & ~1) | (y & 1);
                if (x >= w) continue;
                if (pass) forget(x, y, 1, f);
                check_spot_square(x, y, f);
                check_spot_loc(x, y, f);
            }
            static const int off[][2] = {{-1, 0}, {0, -1}, {-2, 2}, {0, 9999}, {9999, 0}};
            for (unsigned k = 0; k < sizeof off / sizeof off[0]; k++) {
                int x = off[k][0] == 9999 ? w : off[k][0], y = off[k][1] == 9999 ? h : off[k][1];
                check_spot_square(x, y, f);
                check_spot_loc(x, y, f);
            }
            for (int b = (t + f) % 4; b < bases; b += 4) {
                const struct base *B = &game_bases[b];
                if (!in_map(B->x, B->y)) continue;
                if (pass) forget(B->x, B->y, 1, f);
                if (b % 8 == pass) game_bases[b].pop_seen[f] = (int8_t)(B->pop_size + 1);
                check_spot_base(b, f);
                if (b % 3 == 0) check_spot_loc(B->x, B->y, f);
            }
            for (int v = (t + f + pass) % 8; v < vehs; v += 8) {
                if (!sweepable(v)) continue;
                const struct veh *V = &game_vehs[v];
                if (pass) forget(V->x, V->y, 1, f);
                if (v % 5 == pass) game_vehs[v].flags |= VEH_FLAG_INVISIBLE | VEH_FLAG_LURKER;
                check_spot_stack(v, f);
                if (v % 3 == 0) check_spot_loc(V->x, V->y, f);
                if (v % 7 == f) check_unspot_stack(v);
            }
            check_spot_stack(-1, f);
            game_factions[f].flags = flags;
        }
    }
    check_unspot_stack(-1);
    *game_current_faction = current, *game_state = state;
}

// spot_all() for one unit, in a run of variations: each faction made the
// player's, with the fog of war shown and the squares around unseen; the
// unit given to each faction in turn (so that its neighbours become
// strangers); as a network game with and without the flag; with radar,
// cloaked, with a deep pressure hull or invisible; each treaty between
// every pair; with humans (before the game begins, so that no question is
// put); as a probe team or colony pod; next to sensors, the Manifold Nexus
// and squares marked in view; and a base nearby made a faction's target.
static void sweep_unit(int v, int t) {
    struct veh *V = &game_vehs[v];
    struct unit_proto *P = &game_protos[V->proto];
    int f0 = V->faction;
    check_spot_all(v, 1);
    // The fog of war, from each side.
    *game_more_preferences |= MORE_PREF_FOG;
    *game_current_faction = f0;
    forget(V->x, V->y, 49, f0);
    for (int i = 9 + t % 4; i < 49; i += 5) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if (in_map(x, y)) tile_at(x, y)->bits2 ^= MAP2_IN_VIEW;
    }
    check_spot_all(v, 1);
    // To each faction alive, as the player's faction and not.
    for (int f = 0; f < FACTION_COUNT; f++) {
        if (f && !(*game_factions_alive >> f & 1)) continue;
        V->faction = (uint8_t)f;
        *game_current_faction = (f + t) & 1 ? f : f0;
        check_spot_all(v, 1);
    }
    V->faction = (uint8_t)f0;
    // A network game: the flag counts.
    int32_t net = *game_net_multiplayer;
    *game_net_multiplayer = 1;
    check_spot_all(v, 0);
    check_spot_all(v, 1);
    *game_net_multiplayer = net;
    // Abilities and flags that hide or reveal.
    uint32_t abilities = P->abilities;
    uint16_t vflags = V->flags;
    static const uint32_t abls[] = {ABL_DEEP_RADAR, ABL_CLOAKED, ABL_DEEP_PRESSURE_HULL, ABL_DEEP_RADAR | ABL_CLOAKED};
    for (unsigned k = 0; k < sizeof abls / sizeof abls[0]; k++) {
        P->abilities = abilities | abls[k];
        check_spot_all(v, 1);
    }
    P->abilities = abilities;
    V->flags |= VEH_FLAG_INVISIBLE;
    check_spot_all(v, 1);
    V->flags = vflags;
    // Treaties, the same between every pair.
    static const uint32_t treaties[] = {0, TREATY_COMMLINK, TREATY_PACT | TREATY_COMMLINK, TREATY_TREATY,
                                        TREATY_TRUCE | TREATY_COMMLINK};
    uint32_t kept[FACTION_COUNT][FACTION_COUNT];
    for (int a = 0; a < FACTION_COUNT; a++)
        for (int b = 0; b < FACTION_COUNT; b++) kept[a][b] = game_factions[a].diplo_status[b];
    for (unsigned k = 0; k < sizeof treaties / sizeof treaties[0]; k++) {
        for (int a = 0; a < FACTION_COUNT; a++)
            for (int b = 0; b < FACTION_COUNT; b++)
                game_factions[a].diplo_status[b] = (kept[a][b] & ~(uint32_t)(TREATY_ANY | TREATY_COMMLINK)) | treaties[k];
        check_spot_all(v, 1);
        if (k == 2) {
            P->abilities = abilities | ABL_CLOAKED;
            check_spot_all(v, 1);
            P->abilities = abilities;
        }
    }
    for (int a = 0; a < FACTION_COUNT; a++)
        for (int b = 0; b < FACTION_COUNT; b++) game_factions[a].diplo_status[b] = kept[a][b];
    // Humans, before the game begins: meeting a human puts questions to
    // the player (wants_to_speak() opens a window, which allocates), so
    // these are checked for the units woken only.
    uint8_t humans = *game_human_factions;
    int32_t turn = *game_turn;
    *game_turn = 0;
    *game_human_factions = (uint8_t)(1 << f0);
    check_spot_all(v, 1);
    *game_human_factions = 0xff;
    check_spot_all(v, 1);
    *game_human_factions = humans, *game_turn = turn;
    // Plans.
    uint8_t plan = P->plan;
    P->plan = PLAN_PROBE;
    check_spot_all(v, 1);
    P->plan = PLAN_COLONY;
    check_spot_all(v, 1);
    P->plan = plan;
    // Sensors around, in another faction's territory, and the Manifold
    // Nexus all around for an alien player.
    int other = (f0 + 1 + t % 6) % FACTION_COUNT;
    for (int i = 1; i < 25; i += 3) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if (!in_map(x, y)) continue;
        struct map_tile *s = tile_at(x, y);
        s->bits |= MAP_SENSOR;
        s->territory = (int8_t)(i & 4 ? other : f0);
    }
    check_spot_all(v, 1);
    for (int i = 0; i < 25; i++) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if (in_map(x, y)) tile_at(x, y)->bits2 = (tile_at(x, y)->bits2 & ~MAP2_LANDMARK_OFF) | MAP2_NEXUS;
    }
    game_faction_defs[f0].rule_flags |= RULE_FLAG_ALIEN;
    *game_current_faction = f0;
    check_spot_all(v, 1);
    // A base two squares away made its faction's target, with the unit's
    // square seen by all and the unit on the base's land.
    for (int i = 9; i < 25; i++) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        int b = base_at(x, y);
        if (b < 0) continue;
        struct faction_data *F = &game_factions[game_bases[b].faction];
        F->target_x = x, F->target_y = y;
        check_spot_all(v, 1);
        tile_at(V->x, V->y)->visibility = 0xff;
        tile_at(V->x, V->y)->region = tile_at(x, y)->region;
        P->chassis = 0;  // a land chassis
        check_spot_all(v, 1);
        break;
    }
}

// The small functions every turn, and spot_all() for a few units, on a
// protected copy of the game.
static void spot_sweep(void) {
    if (!*game_map || *game_veh_count <= 0) return;
    int t = *game_turn, sweeping = hooks_sweeping;
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    sweep_small(t);
    for (int k = 0, n = 0; k < 64 && n < 4; k++) {
        int v = (k * 37 + t * 11) % *game_veh_count;
        if (!sweepable(v)) continue;
        sweep_unit(v, t + k);
        n++;
    }
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook spot_hooks[] = {
    HOOK(0x5b5700, spot_square, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),  // mov edx, [ebp+12]
    HOOK(0x5b57d0, spot_base, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),    // mov eax, [ebp+8]
    HOOK(0x5b58e0, spot_stack, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),  // push ebx; mov ebx, [ebp+12]
    HOOK(0x5b5a70, unspot_stack, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),  // mov eax, [ebp+8]
    HOOK(0x5b5ad0, spot_loc, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),  // push ebx; mov ebx, [ebp+8]
    HOOK(0x5b6260, spot_all, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x64),     // sub esp, 0x64
};
struct hook_module spot_module = {"spot", spot_hooks, HOOK_COUNT(spot_hooks), spot_sweep, NULL, -1};
