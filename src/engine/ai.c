// The AI's turn around its units (each unit given its moves, kind by kind,
// through the game's enemy_move), native life's moves and the base it
// heads for, and the AI's placing of planet busters. Reimplemented from
// terranx.exe 2.0's disassembly; names follow Thinker (MIT) and OpenSMACX
// (Brendan Casey, GPL-3.0-or-later).
//
// The AI's choice of a move for one of its own units, enemy_move(), is
// reimplemented here too, naval transports' section included; enemy_veh()
// calls it through the game's pointer, so that the sweeps can hold it.
#include "ai.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "combat.h"
#include "events.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "move.h"
#include "path.h"
#include "score.h"
#include "shadow.h"
#include "strategy.h"
#include "tech.h"
#include "veh.h"

enum {
    BSC_UNITY_SCOUT_CHOPPER = 12, BSC_SEALURK = 14, BSC_SPORE_LAUNCHER = 15, BASIC_PROTOS = 64,
    ABL_DROP_POD = 0x10, ABL_ARTILLERY = 0x8000,
    GROUP_NO_GOALS = 21,
    VEH_FLAG_LURKER = 0x40,  // with VEH_FLAG_INVISIBLE: hidden
    VEH_STATE_MONOLITH = 0x100, VEH_STATE_EXPLORE = 0x4000, VEH_STATE_UNK_10000 = 0x10000,
    ORDER_NONE = 0, ORDER_MOVE_TO = 0x18, ORDER_AI_MOVE_TO = 0x58,
    FACTION_FLAG_UNK_10000 = 0x10000,
    FACTION_FLAG_WANTON = 0x1000000,  // commits atrocities wantonly (Thinker's name)
    TREATY_TRUCE = 0x4, TREATY_UNK_80000 = 0x80000,
    ALT_SHELF = 2 << 5, ALT_SHORE = 3 << 5,
    REGION_FIRST_SEA = 0x40,
    // The Ascent to Transcendence (secret project 32) as a base's build
    // (minus its facility number).
    BUILD_ASCENT = -(70 + 32),
    SP_HUNTER_SEEKER = 15, SP_CLONING_VATS_ = 24,
    // Squares native life takes as targets: a sensor, thermal borehole,
    // forest, farm, bunker, solar collector, mine or road; and those it
    // counts as improvements to move onto (sensor, forest, farm, solar
    // collector, mine, road); and those it destroys where it stands.
    MAP_ARTY_TARGETS = 0x81208854, MAP_WORM_TARGETS = 0x80208054, MAP_WORM_EATS = 0x80e88058,
};

_Static_assert(offsetof(struct faction_data, satellites) + 12 == 0x814, "satellites");
_Static_assert(offsetof(struct faction_data, base_count) == 0x105c, "base_count");
_Static_assert(offsetof(struct faction_data, se) + 4 * SE_PLANET == 0x294, "se planet");
_Static_assert(offsetof(struct faction_data, corner_market_turn) == 0x207c, "corner_market_turn");
_Static_assert(offsetof(struct base, intake_2) == 0xc0, "intake_2");
_Static_assert(offsetof(struct base, eco_damage) == 0x48, "eco_damage");

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int alt_of(const struct map_tile *t) { return t->climate & 0xe0; }
static inline int owner_of(const struct map_tile *t) { return t->owner_site & 0xf; }
// A base or unit of a faction or native life (bits MAP_BASE and MAP_UNIT
// as given), as the original tests a square.
static inline int occupied(const struct map_tile *t, uint32_t bits) { return (t->bits & bits) && owner_of(t) < 8; }
static inline const struct unit_proto *proto_of(int proto) { return &game_protos[proto]; }
static inline int triad_of(int proto) { return game_chassis[proto_of(proto)->chassis].triad; }
static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
// Base b, also for b = -1, as the originals read it (the bytes before the
// base table).
static inline struct base *base_any(int b) {
    return (struct base *)((char *)game_bases + (intptr_t)b * (intptr_t)sizeof(struct base));
}
static int moves_left(int veh) { return veh_speed(veh, 0) - game_vehs[veh].moves_spent; }
// The original's test of moves left: none if negative, or zero.
static int no_moves(int veh) {
    int m = moves_left(veh);
    return m < 0 || (m <= 999 && m == 0);
}

// ---- Calls a check can't make twice -------------------------------------------
//
// A fight, or a unit's attack on a square (which ends in its step, and may
// become a fight), is made through these. While `probing`, a run made only
// to see what the function would do, they are noted instead.
static int probing, probe_fight, probe_destroy, probe_threat, probe_x, probe_y;
static void ai_destroy(int veh, int item, int x, int y) {
    if (probing) {
        probe_destroy = 1, probe_x = x, probe_y = y;
        return;
    }
    action_destroy(veh, item, x, y);
}
static void ai_fight(int veh, int dir) {
    if (probing) {
        probe_fight = 1;
        return;
    }
    game_battle_fight(veh, dir, 1, 1, 0);
}

// ---- Native life ----------------------------------------------------------------

// The base native life at (x, y) heads for: on its continent (any base
// from the sea; a sealurk also takes bases on the continent's shores), the
// nearest by vector distance over its mineral and energy intake (plus 32,
// over 32), halved for a base with an alien artifact in it (plan 12) and
// for its own home base, doubled for a base of a faction with
// PLANET above 0 and no ecological damage unless the unit came from it
// (order_auto_type). The last of equals wins. Its distance goes to
// *game_base_find_dist (9999 with none).
int alien_base(int veh, int x, int y) {
    const struct veh *V = &game_vehs[veh];
    int best = -1, best_dist = 9999;
    *game_base_find_dist = 9999;
    int region = tile(x, y)->region;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (V->proto == BSC_SEALURK) {
            if (region < REGION_FIRST_SEA && region != tile(B->x, B->y)->region && !base_on_sea(b, region)) continue;
        } else if (region < REGION_FIRST_SEA && region != tile(B->x, B->y)->region) {
            continue;
        }
        int d = vector_dist(x, y, B->x, B->y) * 32 / (B->intake_2[1] + B->intake_2[2] + 32);
        if (stack_check(veh_at(B->x, B->y), 2, 12, -1, -1)) d /= 2;
        if (b == V->home_base) d /= 2;
        if (V->order_auto_type != B->faction && game_factions[B->faction].se[SE_PLANET] > 0 && !B->eco_damage) d += d;
        if (d <= best_dist) best_dist = d, best = b;
    }
    if (best >= 0) *game_base_find_dist = best_dist;
    return best;
}

// A native unit with transport (an isle of the deep carrying worms)
// unloading near its base. The squares round it are taken in turn: one
// with a faction's unit at sea is its goal; one on the base's continent,
// without a faction's unit and within 6 of the base (any, without a base),
// is where its cargo lands, and the player is told if it is their base and
// they see it. With neither it sets course for the base, and waits once
// next to land there. Returns the direction to move, or 8 for none.
static int alien_unload(int veh, int base, int base_faction, int region) {
    struct veh *V = &game_vehs[veh];
    int vx = V->x, vy = V->y;
    struct base *B = base_any(base);
    for (int i = 0; i < 8; i++) {
        int nx = xrange(vx + game_radius_near_x[i]), ny = vy + game_radius_near_y[i];
        if (!on_map(nx, ny)) continue;
        struct map_tile *t = tile(nx, ny);
        if ((t->bits & MAP_UNIT) && owner_of(t) < 8 && owner_of(t) > 0) {
            if (alt_of(t) < ALT_SHORE) return i;
            continue;
        }
        if (alt_of(t) < ALT_SHORE || t->region != region) continue;
        if (base >= 0 && ((x_dist(nx, B->x) + abs(ny - B->y)) & ~1) > 12) continue;
        // The stack lands: their orders end, and they see what the square
        // has been seen by.
        uint8_t seen = t->visibility;
        if (veh >= 0) {
            int c = veh;
            while (game_vehs[c].prev_in_stack >= 0) c = game_vehs[c].prev_in_stack;
            for (; c >= 0; c = game_vehs[c].next_in_stack) {
                game_vehs[c].order = ORDER_NONE;
                game_vehs[c].visibility |= seen;
            }
        }
        veh_skip(veh);
        int cur = *game_current_faction;
        if (base_faction != cur) return 8;
        if (!(V->faction == cur && (V->flags & (VEH_FLAG_INVISIBLE | VEH_FLAG_LURKER)) !=
                                       (VEH_FLAG_INVISIBLE | VEH_FLAG_LURKER)) &&
            !((1u << (cur & 31)) & V->visibility))
            return 8;
        if (!game_console_focus(game_console, nx, ny, cur)) game_draw_tile(nx, ny, 2);
        game_parse_says(0, B->name, -1, -1);
        int spores = V->proto == BSC_SPORE_LAUNCHER;
        if (*game_net_multiplayer)
            game_netmsg_pop(game_netmsg, text(spores ? 0x68e834 : 0x68e848), 3000, 0, NULL);
        else
            game_popp(*game_script_file, text(spores ? 0x68e868 : 0x68e88c), 0, text(spores ? 0x68e858 : 0x68e87c), 0);
        return 8;
    }
    set_course(veh, 'c', B->x, B->y);
    if (V->order == ORDER_MOVE_TO) {
        int wx = V->waypoint_x[0], wy = V->waypoint_y[0];
        if (alt_of(tile(wx, wy)) >= ALT_SHORE && ((x_dist(vx, wx) + abs(vy - wy)) & ~1) <= 2) veh_skip(veh);
    }
    return 8;
}

// A native unit's move. Without moves it does nothing; hidden, or the
// Unity's scout chopper, it waits. It heads for alien_base()'s base; with
// none and no faction's unit next to it, it dies. Each move adds a turn to
// its count (movement_turns), and a transport with land units aboard (an
// isle with worms) dies after 24, or unloads near the base. Otherwise a
// unit far from its base (8 or more, or off its continent) with no faction
// near dies after 6 turns on land, 16 at sea or in the air, on one turn in
// eight (by unit and turn). It steers by the path to the base when that
// base's faction has more than 2 bases, after turn 50, unless that
// faction's PLANET is above 0 (a sealurk doesn't mind, nor a unit from that
// faction); else by its facing (unk_29). Artillery native life (spore
// launchers) fires at the best target within 2 squares (other than its
// own): stacks by their artillery and naval superiority units (plan 6),
// eight points each, and their attack strength (only for those next to it
// when a faction's unit is next to it), or improvements (sensors 32, others
// 2); out of artillery range a stack counts 1 at most. A target in range
// with nothing on it is bombarded (the square's improvements destroyed); a
// stack two squares off is fought. Otherwise it
// picks the best square next to it: a faction's base most, then a
// faction's unit (not native life, nor an aircraft for a unit that
// isn't one), then improvements, a monolith (above difficulty 2, if it
// isn't upgraded), the way it is heading, the more so with morale, and a
// random 0 to 7. Near a human's base, the angrier native life (morale,
// doubled if damaged) may instead eat the improvements where it stands. A
// damaged unit that hasn't moved may hold back (less so in fungus). A
// transport without cargo, with no good square, dies. It moves by an AI
// go-to order to the square chosen; with none it waits.
//
// The original reads base -1 (the bytes before the base table) for a unit
// with no base (where Thinker substitutes no region and no faction): its
// square's region, its name and its faction's humanity are used as a
// base's would be. It keeps the last score from the artillery's look in
// the variable the moves are scored in (where a sealurk's 12345 for a base
// square is kept); without artillery it is uninitialized there, taken here
// as 0.
int alien_move(int veh) {
    struct veh *V = &game_vehs[veh];
    if (no_moves(veh)) return 0;
    if (V->flags & (VEH_FLAG_INVISIBLE | VEH_FLAG_LURKER)) {
        veh_skip(veh);
        return 0;
    }
    if (V->proto == BSC_UNITY_SCOUT_CHOPPER) {
        veh_skip(veh);
        return 0;
    }
    int vx = V->x, vy = V->y;
    int base = alien_base(veh, vx, vy), base_faction;
    if (base >= 0) {
        base_faction = game_bases[base].faction;
    } else {
        if (!zoc_any(vx, vy, 0)) {
            kill(veh);
            return 1;
        }
        base_faction = 0;
        *game_base_find_dist = 99;
    }
    struct base *B = base_any(base);
    int base_region = tile(B->x, B->y)->region;
    V->movement_turns++;
    if (veh_cargo(veh) && stack_check(veh, 3, 0, -1, -1)) {
        if (V->movement_turns > 24) {
            kill(veh);
            return 1;
        }
        int dir = alien_unload(veh, base, base_faction, base_region);
        if (dir >= 8) return 0;
        V->order = ORDER_AI_MOVE_TO;
        V->waypoint_x[0] = (int16_t)xrange(vx + game_radius_near_x[dir]);
        V->waypoint_y[0] = (int16_t)(vy + game_radius_near_y[dir]);
        return 0;
    }
    int triad = triad_of(V->proto);
    if ((*game_base_find_dist >= 8 || tile(vx, vy)->region != base_region) && !zoc_any(vx, vy, 0) &&
        V->movement_turns > (triad ? 16 : 6) && !((*game_turn + veh) & 7)) {
        kill(veh);
        return 1;
    }
    int angle;
    const struct faction_data *BF = &game_factions[base_faction];
    if (base >= 0 && BF->base_count > 2 && *game_turn > 50 &&
        (V->order_auto_type == base_faction || BF->se[SE_PLANET] <= 0 || V->proto == BSC_SEALURK)) {
        V->waypoint_x[0] = B->x, V->waypoint_y[0] = B->y;
        angle = path_move(game_path, veh, 0);
    } else {
        angle = (int8_t)V->unk_29;
    }
    int score = 0, move = -1;
    if (can_arty(V->proto, 1)) {
        int best = 0, target = -1;
        for (int i = 0; i < 25; i++) {
            int x = xrange(vx + game_radius_x[i]), y = vy + game_radius_y[i];
            if (!on_map(x, y)) continue;
            struct map_tile *t = tile(x, y);
            if (occupied(t, MAP_BASE | MAP_UNIT)) {
                int s = stack_fix(veh_at(x, y));
                if (s < 0) continue;
                score = stack_check(s, 2, 6, -1, -1);
                score = (score + stack_check(s, 6, ABL_ARTILLERY, -1, -1)) * 8;
                if (!zoc_veh(vx, vy, 0) || i < 9) score += stack_check(s, 4, -1, -1, -1);
                if ((x_dist(vx, x) + abs(vy - y)) >> 1 > game_rules_basic->artillery_max_range && score > 1) score = 1;
            } else {
                if (alt_of(t) < ALT_SHORE || !(t->bits & MAP_ARTY_TARGETS)) continue;
                score = t->bits & MAP_SENSOR ? 32 : 2;
            }
            if (!i) score = 0;
            if (score > best) best = score, target = i;
        }
        if (target >= 0) {
            int tx = xrange(vx + game_radius_x[target]), ty = vy + game_radius_y[target];
            if ((x_dist(vx, tx) + abs(vy - ty)) >> 1 <= game_rules_basic->artillery_max_range) {
                if (!occupied(tile(tx, ty), MAP_BASE | MAP_UNIT)) {
                    game_log_say(text(0x68e89c), veh, tx, ty);
                    ai_destroy(veh, 0, tx, ty);
                    for (int f = 1; f < FACTION_COUNT; f++) synch_bit(tx, ty, f);
                    game_draw_tile(tx, ty, 2);
                    return 1;
                }
                if (target >= 9) {
                    game_log_say(text(0x68e8bc), veh, target, 0);
                    ai_fight(veh, target);
                    return 1;
                }
            }
        } else if (stack_check(veh, 15, -1, -1, -1) <= 1 && (tile(vx, vy)->bits & MAP_UNK_4000)) {
            move = -1;
        }
    }
    int best_move = 0, morale = morale_alien(-1, 0);
    for (int i = 0; i < 8; i++) {
        int x = xrange(vx + game_radius_near_x[i]), y = vy + game_radius_near_y[i];
        if (!on_map(x, y)) continue;
        struct map_tile *t = tile(x, y);
        if (alt_of(t) < ALT_SHORE) {
            if (triad == TRIAD_LAND) continue;
        } else {
            if (triad == TRIAD_SEA) {
                if (V->proto != BSC_SEALURK || base_at(x, y) == -1) continue;
                score = 12345;
            }
            // A land unit aboard a transport doesn't land on a faction's unit.
            if (alt_of(tile(vx, vy)) < ALT_SHORE && triad == TRIAD_LAND && (t->bits & MAP_UNIT) && owner_of(t) < 8 &&
                owner_of(t) > 0)
                continue;
        }
        // The sealurk's mark stays in `score` (as in the original) if this
        // square is then passed by.
        if (score != 12345) score = 0;
        int val = score;
        if ((t->bits & MAP_BASE) && owner_of(t) < 8 && owner_of(t) > 0) {
            val += 9999;
        } else {
            int u = veh_at(x, y);
            if (u >= 0 && game_vehs[u].faction) {
                int p = game_vehs[u].proto;
                if (p < BASIC_PROTOS && game_weapons[proto_of(p)->weapon].offense < 0) continue;
                if (triad_of(p) == TRIAD_AIR && triad != TRIAD_AIR) continue;
                val += 999;
            }
        }
        uint32_t bits = t->bits;
        val += game_bit_count(bits & MAP_WORM_TARGETS) * 4;
        if (*game_difficulty > 2) val += (bits & MAP_MONOLITH) && !(V->state & VEH_STATE_MONOLITH) ? 8 : 0;
        val += game_rand() % 8;
        int turn_by = ((angle ^ ~3) - i) & 7;
        if (turn_by > 4) turn_by = 8 - turn_by;
        score = val + (morale + 1) * turn_by;
        if (i == angle) score += morale * morale;
        if (score > best_move) best_move = score, move = i;
    }
    int threat = morale == 0 ? 0 : morale == 1 ? 1 : morale == 2 ? 2 : 4;
    if (V->damage) threat += threat;
    if (game_is_human(B->faction) && threat) {
        if (probing) probe_threat = 1;
        if (game_rand() % 4 < ((best_move < 999) * threat != 0) + 1) {
            int m = moves_left(veh);
            m = m < 0 ? 0 : m > 999 ? 999 : m;
            if (m <= game_rules_basic->move_rate_roads && triad != TRIAD_AIR) {
                int level = (*game_rules & RULES_INTENSE_RIVALRY) ? 5 : game_factions[B->faction].diff_level;
                if (game_rnd(level + 1) && (tile(vx, vy)->bits & MAP_WORM_EATS)) {
                    ai_destroy(veh, 0, -1, -1);
                    return 0;
                }
            }
        }
    }
    if (best_move < 999 && !V->moves_spent && V->damage) {
        const struct map_tile *t = tile(vx, vy);
        if (!((t->bits & MAP_FUNGUS) && alt_of(t) >= ALT_SHELF)) threat = (threat + 1) / 2;
        if (game_rnd(threat + 1)) move = -1;
    }
    if (best_move < 99 && veh_cargo(veh) && !stack_check(veh, 3, 0, -1, -1)) {
        kill(veh);
        return 1;
    }
    if (move >= 0) {
        V->order = ORDER_AI_MOVE_TO;
        V->waypoint_x[0] = (int16_t)xrange(vx + game_radius_near_x[move]);
        V->waypoint_y[0] = (int16_t)(vy + game_radius_near_y[move]);
        return 0;
    }
    V->order = ORDER_NONE;
    V->state &= ~(uint32_t)VEH_STATE_UNK_10000;
    veh_skip(veh);
    return 0;
}

// ---- Air units ----------------------------------------------------------------------
//
// air_power(), the AI's moves for its air units (and missiles), called
// from the game's enemy_move() with the square the unit stands on. It
// leaves a go-to order for most kinds, so that enemy_move() steers the
// unit; missiles it puts down (veh_lift and veh_drop) next to their
// target, in a base, or on the spot a payload goes off. Its own calls only
// weigh odds (battle_fight_2 with option 0) and set orders.
//
// Returns -1 for an order the caller should carry out now (an attack from
// where it stands), -2 when the unit has an order, -3 when it has none or
// was put down, -4 when it stays at its post, -5 when a payload went off,
// or the direction from the square it was put down on to its target.

enum {
    AIR_NEXT = 1,  // a phase's result: none, on to the next
    VEH_STATE_ALERT = 0x200,  // on alert (with an order_auto_type below)
    ALERT_BOMBARD = 10, ALERT_INTERCEPT = 11, ALERT_PATROL = 12,
    ORDER_RAISE_LAND = 0x14, ORDER_LOWER_LAND = 0x15,
    PLAN_NAVAL_SUPERIORITY = 6, PLAN_TERRAFORM_ = 9, PLAN_TECTONIC = 13, PLAN_FUNGAL = 14,
    STACK_UNITS = 1, STACK_PLAN = 2, STACK_TRIAD = 3, STACK_ABIL = 6, STACK_MISSILES = 13, STACK_UNARMED = 19,
    ABL_CARRIER = 0x80, MAP_AIRBASE = 0x40000,
    FACTION_FLAG_SEES_ALL = 0x200,  // its explorers find nothing unseen (Thinker: map revealed)
    FACTION_FLAG_UNK_80000 = 0x80000,
    BASE_STATE_UNK_8 = 0x8, BASE_STATE_UNK_100000 = 0x100000,
    REGION_PLAN_COLONIZE = 8,  // region_base_plan: where an explorer looks around
    // What a fungal payload counts on a square: roads, magtubes, farms,
    // mines, solar collectors, forests, condensers, echelon mirrors,
    // boreholes and the like.
    MAP_FUNGAL_WORTH = 0x1e8881c,
};

static inline int plan_of(int proto) { return proto_of(proto)->plan; }
static inline const struct rules_chassis *chassis_of(int proto) { return &game_chassis[proto_of(proto)->chassis]; }
// Half the distance in x (wrapped) plus y: the distance the AI's air units
// count in.
static int air_dist(int x1, int y1, int x2, int y2) { return (x_dist(x1, x2) + abs(y1 - y2)) >> 1; }
// The distance the unit flies this turn, less a road move: what a missile
// counts as in reach.
static int air_turn(int veh) {
    int rate = game_rules_basic->move_rate_roads;
    return veh_speed(veh, 0) / rate - rate;
}
// The faction (0 to 7) with a unit or base (bits) on the square, or -1.
static inline int owner_with(const struct map_tile *t, uint32_t bits) {
    return (t->bits & bits) && owner_of(t) < 8 ? owner_of(t) : -1;
}
// A go-to order to (x, y), with its letter; a colony pod aloft also gets its
// turns aloft back.
static void air_go(struct veh *V, int x, int y, char icon) {
    V->order = ORDER_MOVE_TO;
    V->status_icon = (uint8_t)icon;
    V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
    if (plan_of(V->proto) == PLAN_COLONY) V->movement_turns = 0;
}
// The unit's odds against the stack at (x, y) with `target` in it, as the
// AI weighs them.
static int air_odds(int veh, int faction, int x, int y, int target) {
    return compute_odds(battle_fight_2(veh, 0, x, y, 0, 0, NULL), faction, veh, target, -2);
}
// A sea square with a unit lowering the land within 4 of a base of the
// faction (where the stack works on the faction's own coast).
static int air_near_works(int faction, const struct veh *U) {
    if (U->order != ORDER_LOWER_LAND || alt_of(tile(U->x, U->y)) >= ALT_SHORE) return 0;
    int b = base_find_2(U->x, U->y, faction);
    if (b < 0) return 0;
    return ((x_dist(U->x, game_bases[b].x) + abs(U->y - game_bases[b].y)) & ~1) <= 4;
}
// Nearest to (tx, ty) of the faction's bases in this turn's reach: the
// unit's go-to (letter 'M'). All 512 base slots are looked at, the unused
// ones too.
static void air_stage_missile(int veh, int faction, int x, int y, int tx, int ty) {
    struct veh *V = &game_vehs[veh];
    int best = 9999;
    for (int b = 0; b < BASE_COUNT; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction || air_dist(x, y, B->x, B->y) > air_turn(veh)) continue;
        int d = air_dist(tx, ty, B->x, B->y);
        if (d >= best) continue;
        best = d;
        air_go(V, B->x, B->y, 'M');
    }
}

// The unit's reach in squares: its turns aloft left (or six turns for
// units without a range) at its speed. For a unit with a range of one turn
// (a missile, a copter) `reach` counts the turn after this one unless it is
// damaged, and `reach2` only this turn's moves left.
static void air_reach(int veh, int *reach, int *reach2) {
    const struct veh *V = &game_vehs[veh];
    int range = chassis_of(V->proto)->range, rate = game_rules_basic->move_rate_roads, a, b;
    if (!range) {
        a = b = veh_speed(veh, 0) * 6;
    } else {
        int left = veh_speed(veh, 0) - V->moves_spent;
        left = left < 0 ? 0 : left > 999 ? 999 : left;
        if (range == 1) {
            a = b = left;
            if (!V->damage) a = veh_speed(veh, 0) + left;
        } else {
            a = b = veh_speed(veh, 0) * (range - V->movement_turns - 1) + left;
        }
    }
    *reach = a / rate, *reach2 = b / rate;
}

// An explorer (or a unit over land where its region is to be settled):
// the square half its reach off in the direction with most squares around
// it its faction hasn't seen (letter 'a'); else one of its bases within
// reach, picked at random weighed by distance (letter 'c'). An explorer
// with neither stops (-3).
static int air_explore(int veh, int reach2, int x0, int y0) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, r = reach2 / 2, best = 0, bx = -1, by = -1;
    for (int i = 0; i < 8; i++) {
        int cx = xrange(x0 + game_radius_near_x[i] * r), cy = y0 + game_radius_near_y[i] * r;
        if (!on_map(cx, cy)) continue;
        int unseen = 0;
        for (int j = 0; j < 9; j++) {
            int nx = xrange(cx + game_radius_near_x[j]), ny = cy + game_radius_near_y[j];
            if (!on_map(nx, ny) || (game_factions[faction].flags & FACTION_FLAG_SEES_ALL)) continue;
            if (!(tile(nx, ny)->visibility & (1u << faction))) unseen++;
        }
        if (unseen > best) best = unseen, bx = cx, by = cy;
    }
    if (bx >= 0) {
        air_go(V, bx, by, 'a');
        return -2;
    }
    int pick = -1;
    best = -1;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction) continue;
        int d = air_dist(x0, y0, B->x, B->y);
        if (d > reach2) continue;
        int roll = d > 0 ? game_rand() % (d + 1) : 0;
        if (roll > best) best = roll, pick = b;
    }
    if (pick >= 0) {
        air_go(V, game_bases[pick].x, game_bases[pick].y, 'c');
        return -2;
    }
    if (V->state & VEH_STATE_EXPLORE) return -3;
    return AIR_NEXT;
}

// A missile without a target: one of several in its stack goes to a base
// of its faction within reach with fewer missiles than its stack has,
// put down there at once.
static int air_missile_spread(int veh, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, n = stack_check(veh, STACK_MISSILES, -1, -1, -1);
    if (n <= 1) return -3;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction) continue;
        if (stack_check(veh_at(B->x, B->y), STACK_MISSILES, -1, -1, -1) >= n) continue;
        if (air_dist(*px, *py, B->x, B->y) > air_turn(veh)) continue;
        veh_drop(veh_lift(veh), B->x, B->y);
        V->visibility = 0;
        *px = B->x, *py = B->y;
        return -3;
    }
    return -3;
}

// The missile's strike on its go-to square: at a base of its own it is put
// down there (-3); a payload is put down on the square and goes off (-5);
// any other missile is put down on the first free square next to it (its
// facing set), and the direction from there returned. With no order, or no
// free square, it may move to another base instead.
static int air_missile_strike(int veh, int *px, int *py, int tx, int ty) {
    struct veh *V = &game_vehs[veh];
    if (V->order != ORDER_MOVE_TO) return air_missile_spread(veh, px, py);
    int wx = V->waypoint_x[0], wy = V->waypoint_y[0];
    if (owner_with(tile(wx, wy), MAP_BASE) == V->faction) {
        *px = wx, *py = wy;
        V->order = ORDER_NONE;
        V->visibility = 0;
        veh_drop(veh_lift(veh), *px, *py);
        return -3;
    }
    V->order = ORDER_NONE;
    int plan = plan_of(V->proto);
    if (plan == PLAN_TECTONIC || plan == PLAN_FUNGAL) {
        veh_drop(veh_lift(veh), tx, ty);
        if (plan == PLAN_TECTONIC) action_tectonic(veh, tx, ty);
        else action_fungal(veh, tx, ty);
        return -5;
    }
    for (int i = 0; i < 8; i++) {
        int nx = xrange(wx + game_radius_near_x[i]), ny = wy + game_radius_near_y[i];
        if (!on_map(nx, ny) || occupied(tile(nx, ny), MAP_BASE | MAP_UNIT)) continue;
        *px = nx, *py = ny;
        V->visibility = 0;
        veh_drop(veh_lift(veh), nx, ny);
        V->unk_29 = (uint8_t)i;
        return i ^ 4;
    }
    V->order = ORDER_NONE;
    return air_missile_spread(veh, px, py);
}

// A tectonic payload: the best of the faction's raise-land goals (eight
// times their priority) and the squares four east of enemy bases on land
// (eight for each five citizens, rounded up) away from the faction's own
// bases, halved out of this turn's reach; free land squares only. In reach
// it heads there (letter 'm'), else to its nearest base in reach (-3 either
// way). With none, or an enemy base at the map's east edge, it may move to
// another base.
static int air_tectonic(int veh, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, best = 0, bx = -1, by = -1;
    const struct faction_data *F = &game_factions[faction];
    for (int g = 0; g < GOAL_COUNT; g++) {
        const struct goal *G = &F->goals[g];
        if (G->type != GOAL_RAISE_LAND) continue;
        int value = G->priority * 8;
        if (air_dist(*px, *py, G->x, G->y) > air_turn(veh)) value /= 2;
        const struct map_tile *t = tile(G->x, G->y);
        if (occupied(t, MAP_BASE | MAP_UNIT) || alt_of(t) < ALT_SHORE) continue;
        if (value > best) best = value, bx = G->x, by = G->y;
    }
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction == faction || !(F->diplo_status[B->faction] & TREATY_VENDETTA)) continue;
        const struct map_tile *t = tile(B->x, B->y);
        if (alt_of(t) < ALT_SHORE) continue;
        if (F->region_total_bases[t->region]) {
            base_find_2(B->x, B->y, faction);
            if (*game_base_find_dist < 10) continue;
        }
        int value = (B->pop_size + 4) / 5 * 8;
        int x = xrange(B->x + 4), y = B->y;
        if (!on_map(x, y)) return air_missile_spread(veh, px, py);
        if (occupied(tile(x, y), MAP_BASE | MAP_UNIT)) continue;
        if (air_dist(*px, *py, x, y) > air_turn(veh)) value /= 2;
        if (value > best) best = value, bx = x, by = y;
    }
    if (bx < 0) return air_missile_spread(veh, px, py);
    if (air_dist(*px, *py, bx, by) <= air_turn(veh)) {
        air_go(V, bx, by, 'm');
        return -3;
    }
    air_stage_missile(veh, faction, *px, *py, bx, by);
    return -3;
}

// A fungal payload: the best square in the radius of an enemy base (of a
// faction with no positive PLANET pending, away from the faction's own
// bases): eight for each citizen, halved out of this turn's reach, and one
// for each improvement there; free squares, not those in fungus already
// (but for fungus in the deep sea). In reach it heads there (letter 'm'),
// else to its nearest base in reach; then strikes.
static int air_fungal(int veh, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, best = 0, bx = -1, by = -1;
    const struct faction_data *F = &game_factions[faction];
    if (*game_base_count <= 0) return air_missile_spread(veh, px, py);
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction == faction || !(F->diplo_status[B->faction] & TREATY_VENDETTA)) continue;
        if (game_factions[B->faction].se_pending[SE_PLANET] > 0) continue;
        if (F->region_total_bases[tile(B->x, B->y)->region]) {
            base_find_2(B->x, B->y, faction);
            if (*game_base_find_dist < 5) continue;
        }
        int people = B->pop_size * 8;
        for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
            int x = xrange(B->x + game_radius_x[i]), y = B->y + game_radius_y[i];
            if (!on_map(x, y)) continue;
            const struct map_tile *t = tile(x, y);
            if ((t->bits & MAP_FUNGUS) && alt_of(t) >= ALT_SHELF) continue;
            if (occupied(t, MAP_BASE | MAP_UNIT)) continue;
            int value = people;
            if (air_dist(*px, *py, x, y) > air_turn(veh)) value = people / 2;
            value += game_bit_count(t->bits & MAP_FUNGAL_WORTH);
            if (value > best) best = value, bx = x, by = y;
        }
    }
    if (bx < 0) return air_missile_spread(veh, px, py);
    if (air_dist(*px, *py, bx, by) <= air_turn(veh)) air_go(V, bx, by, 'm');
    else air_stage_missile(veh, faction, *px, *py, bx, by);
    return air_missile_strike(veh, px, py, bx, by);
}

// Other missiles: the enemy unit (at war, seen, or working on the
// faction's coast) worth most, half its odds times a weight against its
// cost, the last of equals taking it. The weight is 16 at a base, where
// drop troops can follow (the faction has some, or a unit of its own
// within two squares) and 4 otherwise, out in the open 4 where transports,
// formers at sea or the faction's own territory are involved and 2
// elsewhere at a quarter of the odds; doubled with the odds where its
// stack has as many air units as armed ones; with drop troops, doubled for
// a human's units and for the top-ranked faction's; halved out of this
// turn's reach. The count of drop troops (taken from designs 64 plus
// multiples of the faction's number, as the original looks them up) only
// grows from one target to the next. In reach it heads there (letter 'm'),
// else to its nearest base in reach; then strikes.
static int air_missile_targets(int veh, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction;
    uint32_t me = 1u << faction;
    const struct faction_data *F = &game_factions[faction];
    int drops = 0;
    for (int k = 0; k < 64; k++) {
        int p = BASIC_PROTOS + k * faction;
        if ((game_protos[p].flags & PROTO_FLAG_ACTIVE) && has_abil(p, ABL_DROP_POD)) drops += F->units_active[p];
    }
    int best = proto_of(V->proto)->cost * 16;
    for (int i = 0; i < *game_veh_count; i++) {
        const struct veh *U = &game_vehs[i];
        int uf = U->faction;
        if (uf == faction || !(F->diplo_status[uf] & TREATY_VENDETTA)) continue;
        int base = base_at(U->x, U->y);
        int works = air_near_works(faction, U);
        if (!works && !(U->visibility & me)) {
            if (base < 0) continue;
            const struct base *B = &game_bases[base];
            if (B->faction != faction && !(B->visibility & me)) continue;
        }
        if (triad_of(U->proto) == TRIAD_AIR && base < 0) continue;
        int odds = air_odds(veh, faction, U->x, U->y, i), weight;
        if (base >= 0) {
            if (!drops) {
                const struct base *B = &game_bases[base];
                int near = 0;
                for (int j = 0; j < 25 && !near; j++) {
                    int x = xrange(B->x + game_radius_x[j]), y = B->y + game_radius_y[j];
                    if (on_map(x, y) && owner_with(tile(x, y), MAP_UNIT) == faction) near = 1;
                }
                if (near) drops = 1, weight = 16;
                else odds /= 2, weight = 4;
            } else {
                weight = 16;
            }
        } else {
            odds /= 2, weight = 4;
            if (stack_check(i, STACK_PLAN, PLAN_NAVAL_TRANSPORT, -1, -1) ||
                (stack_check(i, STACK_PLAN, PLAN_TERRAFORM_, -1, -1) && alt_of(tile(U->x, U->y)) < ALT_SHORE) ||
                whose_territory(faction, U->x, U->y, NULL, 0) == faction)
                drops++;
            else
                weight = 2, odds /= 2;
        }
        int unarmed = stack_check(i, STACK_UNARMED, -1, -1, -1), units = stack_check(i, STACK_UNITS, -1, -1, -1);
        if (stack_check(i, STACK_TRIAD, TRIAD_AIR, -1, -1) >= units - unarmed) odds *= 2, weight *= 2, drops++;
        if (drops) {
            if (*game_human_factions & (1u << uf)) weight *= 2;
            if (game_factions[uf].ranking == 7) weight *= 2;
        }
        if (works) odds <<= 3;
        int half = odds / 2;
        if (air_dist(*px, *py, U->x, U->y) > air_turn(veh)) weight /= 2;
        if (half < 8) continue;
        int value = half * weight;
        if (value < best) continue;
        best = value;
        if (air_dist(*px, *py, U->x, U->y) <= air_turn(veh)) air_go(V, U->x, U->y, 'm');
        else air_stage_missile(veh, faction, *px, *py, U->x, U->y);
    }
    return air_missile_strike(veh, px, py, -1, -1);
}

// A missile (none for a human's in a network game): by its payload.
static int air_missile(int veh, int *px, int *py) {
    const struct veh *V = &game_vehs[veh];
    if ((*game_human_factions & (1u << V->faction)) && *game_net_multiplayer) return -3;
    switch (plan_of(V->proto)) {
    case PLAN_TECTONIC: return air_tectonic(veh, px, py);
    case PLAN_FUNGAL: return air_fungal(veh, px, py);
    default: return air_missile_targets(veh, px, py);
    }
}

// Interceptors (air superiority), attack aircraft and patrols: the unit
// to go for, nearest (by its score) wins, the last of equals taking it.
// An interceptor covers the faction's own grounded aircraft and transports
// out in the open, without carriers or interceptors, by twice their
// distance plus one (four times that when out of a turn's flight) over the
// stack's size. Enemy units (seen, not at peace) count their distance,
// twice that out of half the reach, half in a region the faction defends
// and twice in one without its bases, nothing for those working on its
// coast; ground units only with the odds (less at a base, more on the
// coast), air units (out in the open) only for an interceptor; when the
// faction is short of air power, only those in its territory or near its
// bases; a patrol keeps to its territory and its coast. In half the reach
// (a unit of its own: less 8, halved) the unit heads for it (letter 'a'),
// -1 if already there; else for the base of its own or a pact partner's
// nearest the target, closer than it and in reach, unless it intercepts
// at its post.
static int air_intercept(int veh, int reach, int reach2, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, plan = plan_of(V->proto), best = -1, best_dist = 9999, best_score = 9999;
    uint32_t me = 1u << faction;
    const struct faction_data *F = &game_factions[faction];
    for (int i = 0; i < *game_veh_count; i++) {
        const struct veh *U = &game_vehs[i];
        if (U->faction == faction) {
            if (plan != PLAN_AIR_SUPERIORITY || chassis_of(V->proto)->range == 1) continue;
            const struct map_tile *t = tile(U->x, U->y);
            if (occupied(t, MAP_BASE) || (t->bits & MAP_AIRBASE)) continue;
            if (stack_check(i, STACK_ABIL, ABL_CARRIER, -1, -1) || !no_moves(i)) continue;
            if (stack_check(i, STACK_PLAN, PLAN_AIR_SUPERIORITY, -1, -1)) continue;
            int up = plan_of(U->proto);
            if (triad_of(U->proto) == TRIAD_AIR ? up == PLAN_AIR_SUPERIORITY : up != PLAN_NAVAL_TRANSPORT) continue;
            int d = air_dist(*px, *py, U->x, U->y), score = 2 * d + 1;
            if (veh_speed(veh, 0) / game_rules_basic->move_rate_roads < d) score *= 4;
            score /= stack_check(i, STACK_UNITS, -1, -1, -1) + 1;
            if (score > best_score) continue;
            best_score = score, best_dist = d, best = i;
            continue;
        }
        int works = air_near_works(faction, U);
        if (!works && !(U->visibility & me)) continue;
        if (F->diplo_status[U->faction] & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) continue;
        const struct map_tile *t = tile(U->x, U->y);
        if (triad_of(U->proto) == TRIAD_AIR) {
            if (plan != PLAN_AIR_SUPERIORITY || occupied(t, MAP_BASE) || (t->bits & MAP_AIRBASE)) continue;
        } else {
            if (plan == PLAN_AIR_SUPERIORITY && (V->state & VEH_STATE_ALERT) && V->order_auto_type == ALERT_INTERCEPT)
                continue;
            int odds = air_odds(veh, faction, U->x, U->y, i);
            if (occupied(t, MAP_BASE)) {
                if (plan == PLAN_OFFENSIVE) continue;
                int unarmed = stack_check(i, STACK_UNARMED, -1, -1, -1);
                if (stack_check(i, STACK_UNITS, -1, -1, -1) - unarmed != 1) odds /= 2;
            }
            odds = odds * 2 / 3;
            if (works) odds <<= 3;
            if (!(plan != PLAN_AIR_SUPERIORITY && (V->state & VEH_STATE_ALERT) && V->order_auto_type == ALERT_PATROL))
                odds /= 2;
            if (odds < 8) continue;
        }
        if (F->unk_84c && F->air_offense < 2 * F->unk_84c &&
            whose_territory(faction, U->x, U->y, NULL, 0) != faction) {
            int b = base_find(U->x, U->y);
            if (b >= 0 && game_bases[b].faction != faction) continue;
        }
        if ((V->state & VEH_STATE_ALERT) && V->order_auto_type == ALERT_PATROL) {
            if (alt_of(t) < ALT_SHORE) {
                base_find_2(U->x, U->y, faction);
                if (*game_base_find_dist > 2) continue;
            } else if (whose_territory(faction, U->x, U->y, NULL, 0) != faction) {
                continue;
            }
        }
        int d = air_dist(*px, *py, U->x, U->y), score;
        if (reach / 2 < d) {
            score = 2 * d;
        } else {
            int region = t->region;
            if (F->region_base_plan[region] == PLAN_DEFENSIVE) score = d / 2;
            else if (!F->region_total_bases[region]) score = 2 * d;
            else score = d;
            if (works) score = 0, d = 0;
        }
        if (score > best_score) continue;
        best_score = score, best_dist = d, best = i;
    }
    if (best < 0) return AIR_NEXT;
    const struct veh *T = &game_vehs[best];
    if (T->faction == faction) best_dist = (best_dist - 8) / 2;
    if (reach / 2 >= best_dist) {
        air_go(V, T->x, T->y, 'a');
        return *px == T->x && *py == T->y ? -1 : -2;
    }
    int alert = V->state & VEH_STATE_ALERT;
    if (alert && V->order_auto_type == ALERT_INTERCEPT) return AIR_NEXT;
    if (plan == PLAN_OFFENSIVE && !(alert && V->order_auto_type == ALERT_PATROL)) return AIR_NEXT;
    int tx = T->x, ty = T->y;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction && !(F->diplo_status[B->faction] & TREATY_PACT)) continue;
        int d = air_dist(tx, ty, B->x, B->y);
        if (reach2 < d || d >= best_dist) continue;
        air_go(V, B->x, B->y, 'a');
        best_dist = d;
    }
    return V->order == ORDER_MOVE_TO ? -2 : AIR_NEXT;
}

// A unit on alert to intercept (an attack aircraft or a naval superiority
// unit without an order) takes on the enemy (at war) ground or sea unit
// around it, within one ring for a reach of one or less, else two, seen
// and in its own element for a ship, with the best attack (three times
// for land units, twice more for formers at sea and again with the
// faction's flag 0x80000): letter 'L', -1 with `now`, else -2. Otherwise
// it goes back to its post (waypoint 1, letter 'l', -2), or stays there
// (-4).
static int air_alert(int veh, int reach, int now, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, plan = plan_of(V->proto);
    uint32_t me = 1u << faction;
    const struct faction_data *F = &game_factions[faction];
    if ((plan == PLAN_OFFENSIVE || plan == PLAN_NAVAL_SUPERIORITY) && V->order != ORDER_MOVE_TO) {
        int n = reach <= 1 ? 9 : 25, best = 0;
        for (int k = 1; k < n; k++) {
            int x = xrange(*px + game_radius_x[k]), y = *py + game_radius_y[k];
            if (!on_map(x, y)) continue;
            int s = stack_fix(veh_at(x, y));
            if (s < 0) continue;
            const struct veh *S = &game_vehs[s];
            if (S->faction == faction || !(F->diplo_status[S->faction] & TREATY_VENDETTA)) continue;
            if (triad_of(S->proto) == TRIAD_AIR) continue;
            int triad = triad_of(V->proto);
            const struct map_tile *t = tile(x, y);
            if (triad != TRIAD_AIR) {
                if (!(F->flags & FACTION_FLAG_SEES_ALL) && !(t->visibility & me)) continue;
                if (!occupied(t, MAP_BASE) && !(S->visibility & me)) continue;
                if ((alt_of(t) < ALT_SHORE) != (triad == TRIAD_SEA)) continue;
            }
            int attack = offense_proto(S->proto, -1, 0);
            if (triad_of(S->proto) == TRIAD_LAND) attack *= 3;
            if (alt_of(t) < ALT_SHORE && stack_check(s, STACK_PLAN, PLAN_TERRAFORM_, -1, -1)) {
                attack *= 2;
                if (F->flags & FACTION_FLAG_UNK_80000) attack *= 2;
            }
            if (attack > best) {
                best = attack;
                air_go(V, x, y, 'L');
            }
        }
        if (V->order == ORDER_MOVE_TO) return now ? -1 : -2;
    }
    if (V->order == ORDER_MOVE_TO) return -4;
    if (V->x == V->waypoint_x[1] && V->y == V->waypoint_y[1]) return -4;
    air_go(V, V->waypoint_x[1], V->waypoint_y[1], 'l');
    return -2;
}

// Attack aircraft, and units on alert to bombard (only the base at their
// post, waypoint 1): the enemy base (not at peace) worth most, with its
// defender's odds of 8 or more (halved unless a lone armed defender): its
// citizens and twice the faction's bases in its region, by 32; twice and
// one more with a unit of the faction next to it in the region, else an
// eighth where the faction lacks air power for the defenders or the drop
// pod tech; four times with event bit for the faction, three times for
// base state 0x8, twice for 0x100000, twice for the faction's target base,
// twice in the unit's region, four times where the faction's plan for the
// region is up to defence; over 1 + (distance - 1) / (half the reach,
// within 1 and 99). The last of equals wins. In half the reach the unit
// heads for it (letter 'a') unless weak; else for the base of its own or a
// pact partner's nearest it in reach. With an order, -1 with `now` (not at
// a post), else -2; without, -3.
static int air_bombard(int veh, int reach, int reach2, int region0, int now, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, best = 0, bx = -1, by = -1, weak_best = 0;
    const struct faction_data *F = &game_factions[faction];
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction == faction || (F->diplo_status[B->faction] & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)))
            continue;
        int post = (V->state & VEH_STATE_ALERT) && V->order_auto_type == ALERT_BOMBARD;
        if (post && (B->x != V->waypoint_x[1] || B->y != V->waypoint_y[1])) continue;
        int s = stack_fix(veh_at(B->x, B->y));
        if (s < 0) continue;
        if (!post) {
            const struct veh *S = &game_vehs[s];
            int odds = air_odds(veh, faction, S->x, S->y, s);
            int lone = 0;
            if (occupied(tile(S->x, S->y), MAP_BASE)) {
                int unarmed = stack_check(s, STACK_UNARMED, -1, -1, -1);
                lone = stack_check(s, STACK_UNITS, -1, -1, -1) - unarmed == 1;
            }
            if (!lone) odds /= 2;
            if (odds < 8) continue;
        }
        int region = tile(B->x, B->y)->region;
        int value = (B->pop_size + 2 * F->region_total_bases[region]) * 32, next_to = 0;
        for (int j = 1; j <= 8; j++) {
            int x = xrange(B->x + game_radius_x[j]), y = B->y + game_radius_y[j];
            if (!on_map(x, y)) continue;
            const struct map_tile *t = tile(x, y);
            if (t->region != region || owner_with(t, MAP_UNIT) != faction) continue;
            next_to = 1;
            value = value * 2 + 1;
            break;
        }
        int weak = 0;
        if (!next_to && !post) {
            if (F->air_offense < 2 * stack_check(veh_at(B->x, B->y), STACK_UNITS, -1, -1, -1)) weak = 1;
            if (!has_tech(game_abilities[abil_index(ABL_DROP_POD)].preq_tech, faction)) weak = 1;
            if (weak) value /= 8;
        }
        if (B->event & (1u << faction)) value *= 4;
        if (B->state & BASE_STATE_UNK_8) value *= 3;
        if (B->state & BASE_STATE_UNK_100000) value *= 2;
        if (b == F->attack_target_base) value *= 2;
        if (region == region0) value *= 2;
        if (F->region_base_plan[region] <= PLAN_DEFENSIVE) value *= 4;
        int d = air_dist(*px, *py, B->x, B->y), r = game_range(reach / 2, 1, 99);
        value /= (d - 1) / r + 1;
        if (value < best) continue;
        best = value, weak_best = weak, bx = B->x, by = B->y;
    }
    if (on_map(bx, by)) {
        if (air_dist(*px, *py, bx, by) <= reach / 2) {
            if (!weak_best) air_go(V, bx, by, 'a');
        } else {
            int best_dist = 9999;
            for (int b = 0; b < *game_base_count; b++) {
                const struct base *B = &game_bases[b];
                if (B->faction != faction && !(F->diplo_status[B->faction] & TREATY_PACT)) continue;
                int d = air_dist(bx, by, B->x, B->y);
                if (reach2 < d || d >= best_dist) continue;
                best_dist = d;
                air_go(V, B->x, B->y, 'a');
            }
        }
    }
    if (V->order != ORDER_MOVE_TO) return -3;
    if (!now || ((V->state & VEH_STATE_ALERT) && V->order_auto_type == ALERT_BOMBARD)) return -2;
    return -1;
}

// The phases in turn. `home` lets an interceptor without a target go home
// (action_home); `now` asks for -1 where the unit should attack at once.
// A unit with a go-to elsewhere keeps it (-1 with `now`, unless an
// interceptor). Aircraft aloft for a turn or more head home when they
// must (action_home type 4); on alert to intercept or patrol, damaged
// ones go home; explorers explore; missiles strike; then the phases above
// by plan.
int air_power(int veh, int home, int now, int *px, int *py) {
    struct veh *V = &game_vehs[veh];
    if (V->order == ORDER_MOVE_TO && (V->waypoint_x[0] != *px || V->waypoint_y[0] != *py)) {
        if (!now || plan_of(V->proto) == PLAN_AIR_SUPERIORITY) return -2;
        return -1;
    }
    int faction = V->faction, region0 = tile(*px, *py)->region, reach, reach2;
    air_reach(veh, &reach, &reach2);
    if (chassis_of(V->proto)->range && V->movement_turns) {
        action_home(veh, 4);
        if (V->order == ORDER_MOVE_TO) return -2;
    }
    uint32_t state = V->state;
    int alert = state & VEH_STATE_ALERT, r;
    if (alert && (V->order_auto_type == ALERT_INTERCEPT || V->order_auto_type == ALERT_PATROL)) {
        if (V->damage) {
            action_home(veh, 0);
            return -2;
        }
        goto attack;
    }
    if (alert && V->order_auto_type == ALERT_BOMBARD) goto bombard;
    if ((state & VEH_STATE_EXPLORE) ||
        (alt_of(tile(*px, *py)) >= ALT_SHORE && game_factions[faction].region_base_plan[region0] == REGION_PLAN_COLONIZE)) {
        if ((r = air_explore(veh, reach2, *px, *py)) != AIR_NEXT) return r;
    }
    if (chassis_of(V->proto)->missile) return air_missile(veh, px, py);
attack:;
    int plan = plan_of(V->proto);
    if (plan == PLAN_AIR_SUPERIORITY || (alert && V->order_auto_type == ALERT_PATROL) || plan == PLAN_OFFENSIVE) {
        if ((r = air_intercept(veh, reach, reach2, px, py)) != AIR_NEXT) return r;
        if (plan != PLAN_OFFENSIVE) {
            if (!home) return -3;
            action_home(veh, 0);
            return -2;
        }
    }
    alert = V->state & VEH_STATE_ALERT;
    if (alert && V->order_auto_type == ALERT_INTERCEPT) return air_alert(veh, reach, now, px, py);
bombard:
    if (plan_of(V->proto) != PLAN_OFFENSIVE && !(alert && V->order_auto_type == ALERT_BOMBARD)) return -3;
    return air_bombard(veh, reach, reach2, region0, now, px, py);
}

// ---- Planet busters ----------------------------------------------------------------

static int pb_weapon(int proto) { return game_weapons[proto_of(proto)->weapon].offense >= 99; }

// Whether the AI faction would use a planet buster on base_faction's base:
// `score` counts its own former bases among the target's (and 2 if it is
// at the climax). Returns 0 to pass the base by.
static int pb_willing(int faction, int other, int score) {
    const struct faction_data *F = &game_factions[faction], *O = &game_factions[other];
    int intense = (*game_rules & RULES_INTENSE_RIVALRY) != 0;
    int charter_repealed = *game_un_charter_a > *game_un_charter_b;
    if (!(F->diplo_status[other] & TREATY_WANT_REVENGE) && !(F->flags & FACTION_FLAG_WANTON)) {
        int level = intense ? 6 : O->diff_level;
        if (score < 9 - level) {
            if (!game_is_human(other)) return 0;
            if (!intense && (!charter_repealed || F->ai_fight < 1)) return 0;
            if (O->ranking < F->ranking) return 0;
            if (!reputation(other, faction)) return 0;
        }
    }
    if (at_climax(faction) && F->ai_fight >= 0 && intense) return 1;
    if (F->diplo_status[other] & TREATY_UNK_80000) {
        if (F->major_atrocities < O->major_atrocities) return 1;
        if (2 * F->mil_strength < O->mil_strength) return 1;
        if (at_climax(faction)) return 1;
        if (score < 4 && !charter_repealed) return 0;
        return F->ranking <= O->ranking;
    }
    if (F->flags & FACTION_FLAG_WANTON) return 1;
    int level = intense ? 6 : O->diff_level;
    if (O->integrity_blemishes + (charter_repealed ? 4 : 0) + level + score < 11) return 0;
    if (2 * F->mil_strength < O->mil_strength) return 1;
    if (score < 4) return 0;
    return F->ranking <= O->ranking;
}

// What a planet buster's blast on base b's square would be worth, over the
// squares it reaches: each enemy base by its size and its faction's rank
// (2000 more for a headquarters while its faction corners the energy
// market, twice the minerals of one building the Ascent, 1000 for the
// Hunter-Seeker Algorithm's base, 500 for the Cloning Vats', 60 for each
// secret project times the rank), and the units on each square by
// stack_check type 7 times their faction's rank (enemies' counted double
// on land the faction plans to take, half on land it plans otherwise for,
// half again where it has no bases; its own subtracted; type 7 is their
// cost). A base of its own
// or of a faction it has a truce or better with in reach rules the target
// out (-9999), as does an empty base square.
static int pb_value(int faction, int b, int reach) {
    const struct base *B = &game_bases[b];
    const struct faction_data *F = &game_factions[faction];
    int value = 0;
    for (int i = 0; i < reach; i++) {
        int x = xrange(B->x + game_radius_x[i]), y = B->y + game_radius_y[i];
        if (!on_map(x, y)) continue;
        int b3 = base_at(x, y);
        if (b3 >= 0) {
            const struct base *T = &game_bases[b3];
            int f3 = T->faction;
            if (f3 == faction || (F->diplo_status[f3] & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE))) return -9999;
            value += game_factions[f3].ranking * T->pop_size;
            int offset;
            uint32_t mask;
            bitmask(FAC_HEADQUARTERS, &offset, &mask);
            if ((T->facilities_built[offset] & (uint8_t)mask) &&
                game_factions[T->faction].corner_market_turn > *game_turn)
                value += 2000;
            if (T->queue[0] == BUILD_ASCENT) value += T->minerals * 2;
            if (game_secret_projects[SP_HUNTER_SEEKER] == b3) value += 1000;
            if (game_secret_projects[SP_CLONING_VATS_] == b3) value += 500;
            for (int p = 0; p < SP_COUNT; p++)
                if (game_secret_projects[p] == b3) value += game_factions[T->faction].ranking * 60;
        }
        int s = stack_fix(veh_at(x, y));
        if (s < 0) {
            if (i == 0) return -9999;
            continue;
        }
        const struct veh *S = &game_vehs[s];
        if (S->faction == faction) {
            value -= stack_check(s, 7, -1, -1, -1) * F->ranking;
            continue;
        }
        int c = stack_check(s, 7, -1, -1, -1) * game_factions[S->faction].ranking;
        int region = tile(S->x, S->y)->region;
        if (region < REGION_FIRST_SEA) {
            int plan = F->region_base_plan[region];
            if (plan == 2) {
                c += c;
            } else if (plan) {
                c /= 2;
                if (!F->region_total_bases[region]) c /= 2;
            }
        }
        value += c;
    }
    return value;
}

// An AI's planet buster, given its target: the most valuable enemy base
// (pb_value) of a faction the AI would use one on (pb_willing; only those
// without a pact, and with a truce or treaty only at the climax when it
// has reasons to break it, 0xc20), worth more than nothing, halved (twice
// below half) while the target has more orbital defense pods ready than
// the AI has planet busters, and passed by then while the AI is building
// one. First within the unit's reach this turn (in moves on roads): if
// worth more than the unit's cost times the AI's rank, the unit is put on
// a square next to the target without a faction's base or unit, and the
// direction from there to the target is returned. Then by bases of the
// AI's own within that reach of a target (its own former bases there
// counting for it): the unit is put in the base found for the best target,
// and -1 returned. The units are put down without being seen.
//
// The original measures reach as the sum of the distances along x and y,
// halved. With no target found it takes the first pass's base -1 (the
// bytes before the base table) if the unit's cost times the rank is not
// above 0, and the second pass's base 0, if it is the AI's.
int enemy_planet_buster(int veh) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction;
    if (game_is_human(faction)) return -1;
    int building = 0, busters = 0;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction == faction && B->queue[0] >= 0 && pb_weapon(B->queue[0])) building++;
    }
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].faction == faction && pb_weapon(game_vehs[v].proto)) busters++;
    int reach = game_radius_count[planet_buster(veh)];
    const struct faction_data *F = &game_factions[faction];
    for (int pass = 0; pass < 2; pass++) {
        int best_value = 0, best = -1, best_from = 0, from = 0;
        for (int b = 0; b < *game_base_count; b++) {
            const struct base *B = &game_bases[b];
            int other = B->faction;
            if (other == faction) continue;
            uint32_t st = F->diplo_status[other];
            if (st & TREATY_PACT) continue;
            if (st & (TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) {
                if (!(st & (TREATY_WANT_REVENGE | TREATY_SHALL_BETRAY | TREATY_UNK_800))) continue;
                if (!at_climax(faction)) continue;
            }
            int score = 0, range = veh_speed(veh, 0) / game_rules_basic->move_rate_roads;
            if (!pass) {
                if ((x_dist(V->x, B->x) + abs(V->y - B->y)) >> 1 > range) continue;
            } else {
                int found = 0;
                for (int b2 = 0; b2 < *game_base_count; b2++) {
                    const struct base *B2 = &game_bases[b2];
                    if (B2->faction == faction && (x_dist(B2->x, B->x) + abs(B2->y - B->y)) >> 1 <= range)
                        found = 1, from = b2;
                    if (B2->faction == other && B2->faction_former == faction) score++;
                }
                if (!found) continue;
            }
            if (at_climax(faction)) score += 2;
            if (!pb_willing(faction, other, score)) continue;
            int value = reach > 0 ? pb_value(faction, b, reach) : 0;
            const struct faction_data *O = &game_factions[other];
            int pods = O->satellites[3] - O->odp_deployed;
            if (busters < pods + 1) {
                if (building) continue;
                value /= 2;
                if (busters < pods / 2) value /= 2;
            }
            if (value > best_value) best_value = value, best = b, best_from = from;
        }
        if (!pass) {
            if (best_value < proto_of(V->proto)->cost * F->ranking) continue;
            const struct base *T = base_any(best);
            for (int d = 0; d < 8; d++) {
                int x = xrange(T->x + game_radius_near_x[d]), y = T->y + game_radius_near_y[d];
                if (!on_map(x, y)) continue;
                if (occupied(tile(x, y), MAP_BASE | MAP_UNIT)) continue;
                veh_drop(veh_lift(veh), x, y);
                V->visibility = 0;
                return d ^ 4;
            }
        } else if (best_from >= 0 && game_bases[best_from].faction == faction) {
            veh_drop(veh_lift(veh), game_bases[best_from].x, game_bases[best_from].y);
            V->visibility = 0;
        }
    }
    return -1;
}

// ---- A unit's move -------------------------------------------------------------------
//
// enemy_move(), the AI's choice of what one of its units does (and the
// automation of the player's): 56 KB of the original, taken here section by
// section in the original's order, with its jumps between sections kept as
// labels named after the original's addresses (at_56c9e1 is 0x56c9e1). The
// original keeps one set of locals for all its sections and reuses them
// for other ends as it goes (a section often reads what an earlier one
// left), so they are kept as one set here too, named after their offsets
// in its frame (l2c is [ebp-0x28], Ghidra's local_2c), with the clearer
// ones named for what they first hold.

enum {
    PLAN_RECONNAISSANCE = 3, PLAN_COLONIZATION = 8, PLAN_TERRAFORMING = 9,
    PLAN_SUPPLY_CONVOY = 10, PLAN_INFO_WARFARE = 11, PLAN_ALIEN_ARTIFACT = 12,
};

static int (*orig_enemy_move)(int veh) __attribute__((used));

// The tech a kind of terraforming needs, on land or at sea.
static int tf_preq(int tf, int sea) { return sea ? game_terraform[tf].preq_tech_sea : game_terraform[tf].preq_tech; }

// The first unit of a unit's stack.
static int stack_top_of(int veh) {
    if (veh < 0) return -1;
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    return veh;
}

// The original's frame: its 0x2c4 bytes of locals below the saved ebp,
// as words; FRAME_AT(f, k) is [ebp-k].
enum { FRAME_WORDS = 0x2c4 / 4 };
#define FRAME_AT(f, k) ((f)[(0x2c4 - (k)) / 4])


// The calls that carry a move out, rather than choose it: fights, attacks
// on squares (which end in a step), air drops, probe teams' missions, bases
// founded, obliterated or gated to, units given away, artifacts studied,
// aircraft's moves (air_power, the game's). While `em_probing`, a run made
// to see what enemy_move would do, they are only noted (em_risky).
static int em_probing, em_risky;
static void em_destroy(int veh, int item, int x, int y) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    action_destroy(veh, item, x, y);
}
static void em_fight(int veh, int dir, int table, int option) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    game_battle_fight(veh, dir, table, option, 0);
}
// A fight's odds only (battle_fight's option 0), with the defender.
static int em_odds(int veh, int dir, int *def) { return battle_fight(veh, dir, 0, 0, def); }
static void em_build(int veh) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    action_build(veh, NULL);
}
static void em_oblit(int veh, int base) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    action_oblit(veh, base);
}
static void em_gate(int veh, int base) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    action_gate(veh, base);
}
static int em_probe(int veh, int target) {
    if (em_probing) {
        em_risky = 1;
        return 0;
    }
    return game_probe(veh, -1, target, 1);
}
static void em_airdrop(int veh, int x, int y, int flags) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    game_action_airdrop(veh, x, y, flags);
}
static void em_give(int veh, int faction) {
    if (em_probing) {
        em_risky = 1;
        return;
    }
    game_action_give(veh, faction);
}
static int em_study(int veh) {
    if (em_probing) {
        em_risky = 1;
        return 1;
    }
    return study_artifact(veh);
}
static int em_air_power(int veh, int base_dist, int zoc, int *x, int *y) {
    if (em_probing) {
        em_risky = 1;
        return -4;
    }
    return game_air_power(veh, base_dist, zoc, x, y);
}

static int em_body(int veh, const int32_t *frame) {
    int forced = 0;
    if (veh < 0) veh = -1 - veh, forced = 1;
    struct veh *V = &game_vehs[veh];
    int net_own = *game_net_multiplayer && V->faction == *game_current_faction;
    game_do_all_non_input();
    if (!V->faction && !net_own) {
        // Native life (alien_move), whose fights and attacks are noted
        // too while probing.
        if (!em_probing) return alien_move(veh);
        probing = 1, probe_fight = probe_destroy = probe_threat = 0;
        int r = alien_move(veh);
        probing = 0;
        if (probe_fight || probe_destroy || probe_threat) em_risky = 1;
        return r;
    }
    int proto = V->proto, faction = V->faction, y = V->y, x = V->x;
    int plan = proto_of(proto)->plan, triad = triad_of(proto);
    struct faction_data *F = &game_factions[faction];
    const struct map_tile *here = tile(x, y);
    int alt = here->climate >> 5, at_sea = alt_of(here) < ALT_SHORE, region = here->region;
    int st4 = (V->state & 0x60000) == 0x40000, st6 = (V->state & 0x60000) == 0x60000;
    int recon_800 = plan == PLAN_RECONNAISSANCE && has_abil(proto, 0x800);
    int region_plan = F->region_base_plan[region];
    int damage = V->damage, hurt;
    {
        int hp = proto_of(V->proto)->plan == PLAN_ALIEN_ARTIFACT ? 1 : game_range(proto_of(V->proto)->reactor, 1, 100) * 10;
        if (damage >= hp / 2) {
            hurt = 4;
        } else {
            hp = proto_of(V->proto)->plan == PLAN_ALIEN_ARTIFACT ? 1 : game_range(proto_of(V->proto)->reactor, 1, 100) * 10;
            if (damage > hp / 3) hurt = 3;
            else if (damage > proto_of(V->proto)->reactor * 2) hurt = 2;
            else hurt = damage != 0;
        }
    }
    if (planet_buster(veh)) hurt = 0;
    if (proto == 16 || proto == 17 || proto == 18) hurt = 0;
    int zocm = zoc_move(x, y, faction), zocv = zoc_veh(x, y, faction);
    int near_base = base_find_3(x, y, -1, triad == TRIAD_LAND ? region : -1, -1, -1);
    int near_dist = *game_base_find_dist;
    int own_base = base_find_3(x, y, faction, triad == TRIAD_LAND ? region : -1, -1, -1);
    int own_dist = *game_base_find_dist;
    int terr = whose_territory(faction, x, y, NULL, 0);
    int own_terr = terr == faction;
    int exploring = V->state & 0x4000;
    int human_bit = 1 << faction;
    // Locals the original shares between its sections.
    // The six that some of its paths read before setting them hold what
    // lay below its stack (FRAME_AT): [ebp-0x4] (a site deleted, 0x570b60),
    // [ebp-0x24] (a base, 0x572cd3), [ebp-0x38] (five places), [ebp-0x70]
    // (0x5775b1), and [ebp-0x48] and [ebp-0x78] (naval transports', which
    // the original moves). No path reads the others before setting them,
    // so they start at 0 rather than at leftovers only ours could see.
    int dir84 = -1, l88 = 0, lc8 = 0, lc4 = 0, lb8 = 0, l90 = 0, lbc = 0;
    int l74 = FRAME_AT(frame, 0x70), l3c = FRAME_AT(frame, 0x38), l28 = FRAME_AT(frame, 0x24);
    int l8 = FRAME_AT(frame, 0x4), l4c = FRAME_AT(frame, 0x48), l7c = FRAME_AT(frame, 0x78);
    int l2c = 0, l20 = 0, lc = 0, l30 = 0, l58 = 0, l5c = 0, l24 = 0, l34 = 0;
    int l48 = recon_800, l50 = own_terr, l6c = human_bit;
    if (forced) goto at_57138d;
    if ((V->flags & 0x440) && !game_is_human(faction)) {
        veh_skip(veh);
        return 0;
    }
    if (!game_range(veh_speed(veh, 0) - V->moves_spent, 0, 999)) {
        lbc = 1;
        goto at_574a7e;
    }
    // A former at work.
    if (V->order >= 4 && V->order < 0x18) {
        if (!zocv) goto finish;
        if (zocv > 1 && (!game_is_human(faction) || !(F->diplo_status[zocv - 1] & 0x10))) goto finish;
        if ((V->state & 0x200) && (V->order_auto_type == 4 || V->order_auto_type == 5)) goto at_56c2d1;
        for (int i = 0; i < 8; i++) {
            int nx = xrange(x + game_radius_near_x[i]), ny = y + game_radius_near_y[i];
            l8 = nx, lc = ny;
            if (!on_map(nx, ny)) continue;
            const struct map_tile *t = tile(nx, ny);
            if (triad != TRIAD_AIR && (alt_of(t) < ALT_SHORE) != (triad == TRIAD_SEA)) continue;
            if (!(t->bits & 3)) {
                l74 = -1;
            } else {
                l74 = owner_of(t);
                if (l74 < 8) {
                    if (l74 != faction && !(F->diplo_status[l74] & TREATY_PACT)) continue;
                } else {
                    l74 = -1;
                }
            }
            if (!zoc_veh(nx, ny, faction) && (!zocm || !zoc_move(nx, ny, faction))) {
                dir84 = i;
                goto at_578b26;
            }
        }
        if (V->state & 0x200) goto at_56c9c2;
    }
    if (zocv < 2 || !st4 || !(F->diplo_status[zocv - 1] & 0x10)) {
        if (st6) goto at_56bcf8;
    } else {
        st4 = 0;
        V->state &= ~0x40000u;
        if (x == F->target_x && y == F->target_y) {
            F->target_x = -1;
            st6 = 1;
            for (int c = stack_top_of(veh); c >= 0; c = game_vehs[c].next_in_stack)
                if (c == veh || (game_vehs[c].state & 0x40000)) game_vehs[c].state |= 0x60000;
        at_56bcf8:
            if (V->moves_spent >= game_rules_basic->move_rate_roads && !zocv && !game_is_human(faction) &&
                stack_check(veh, 14, -1, -1, -1)) {
                veh_skip(veh);
                goto finish;
            }
        } else if (st6) {
            goto at_56bcf8;
        }
    }
    if (st4 && V->x == F->target_x && V->y == F->target_y) {
        lb8 = 1, l90 = 1;
        goto at_57707f;
    }
    // Patrols and waypoints.
    {
        uint32_t st = V->state;
        int a = (st & 0x1000200) == 0x1000200, b = (st & 0x2000200) == 0x2000200 && V->patrol_point < V->waypoint_count;
        if (a || b) {
            if (V->order != ORDER_MOVE_TO) {
                if (b && !a) {
                    V->order = ORDER_MOVE_TO;
                    for (int i = 0; i < V->waypoint_count; i++)
                        V->waypoint_x[i] = V->waypoint_x[i + 1], V->waypoint_y[i] = V->waypoint_y[i + 1];
                    if (!--V->waypoint_count) V->state &= 0xfdfefdffu;
                } else {
                    int16_t wx = V->waypoint_x[0], wy = V->waypoint_y[0];
                    for (int i = 0; i < V->waypoint_count; i++)
                        V->waypoint_x[i] = V->waypoint_x[i + 1], V->waypoint_y[i] = V->waypoint_y[i + 1];
                    V->waypoint_x[V->waypoint_count] = wx, V->waypoint_y[V->waypoint_count] = wy;
                    V->state &= ~0x10000u;
                    if (V->waypoint_x[0] == wx && V->waypoint_y[0] == wy && V->waypoint_count == 1) {
                        V->order = ORDER_NONE;
                        V->state &= 0xfefffdffu;
                    } else {
                        V->order = ORDER_MOVE_TO;
                    }
                }
            }
            goto finish;
        }
    }
    if (!game_is_human(faction) && (V->flags & 0x800)) {
        if (has_tech(game_terraform[6].preq_tech, faction)) V->order_auto_type = 2;
        goto at_56c30f;
    }
    if (!((V->state & 0x200) && plan == PLAN_TERRAFORMING)) goto at_56c9e1;
    // ---- An automated former.
    switch (V->order_auto_type) {
    case 3:
        l28 = V->home_base;
        if (l28 < 0 && (l28 = own_base) < 0) {
            V->state &= ~0x200u;
            veh_skip(veh);
            return 0;
        }
        goto at_572922;
    case 4:
    case 5: {
        const struct map_tile *t = here;
        uint32_t bits = t->bits;
        l50 = (int)bits;
        if (alt_of(t) < 0x40 || (bits & 0x2000) || ((t->bits & 1) && owner_of(t) < 8)) goto at_56c9c2;
        if (!(bits & MAP_FUNGUS)) {
            if (!at_sea && !(bits & 0x8000) && (t->rocky_lock & 0xc0) < 0x80 && ((t->climate & 0x18) < 0x10 || alt > 3) &&
                has_tech(game_terraform[0].preq_tech, faction)) {
                V->order = 4;
                goto finish;
            }
        } else if (has_tech(tf_preq(10, at_sea), faction)) {
            V->order = 0xe;
            goto finish;
        }
        if (!(t->bits & MAP_FUNGUS) || alt_of(t) < 0x40 || has_tech(game_rules_basic->tech_improve_fungus, faction)) {
            if (V->order_auto_type == 4 && !(t->bits & 0x40) && energy_yield(faction, -1, x, y, 1) &&
                has_tech(tf_preq(3, at_sea), faction)) {
                V->order = 7;
                goto finish;
            }
            if (V->order_auto_type == 5 && !(t->bits & 0x10) && has_tech(tf_preq(2, at_sea), faction)) {
                V->order = 6;
                goto finish;
            }
        }
        bits = t->bits;
        if (!(bits & 4)) {
            if (at_sea) goto at_56c9c2;
            if ((!(bits & MAP_FUNGUS) || alt_of(t) < 0x40 || has_tech(game_rules_basic->tech_road_fungus, faction)) &&
                has_tech(game_terraform[5].preq_tech, faction)) {
                V->order = 9;
                goto finish;
            }
        } else if (at_sea) {
            goto at_56c9c2;
        }
        if ((t->bits & 0x8000) || (t->rocky_lock & 0xc0) > 0x7f || !has_tech(game_terraform[0].preq_tech, faction))
            goto at_56c9c2;
        V->order = 4;
        goto finish;
    }
    case 1:
        goto at_56c30f;
    case 2:
        if (!has_tech(game_terraform[6].preq_tech, faction)) V->order_auto_type = 1;
        goto at_56c30f;
    }
    if (V->order_auto_type != 6 &&
        (!has_abil(V->proto, 0x2000) || V->order_auto_type || !(*game_more_preferences & 8))) {
        if (V->order_auto_type != 7) goto at_56c9e1;
        if (!good_sensor(faction, x, y)) {
            l8 = x, lc = y;
            if (game_path_sensors(game_path, faction, &l8, &lc)) goto at_56c2d1;
            if (l8 != x || lc != y) {
                go_to(veh, 's', l8, lc);
                return 0;
            }
        }
        V->order = 0xd;
        goto finish;
    }
    if (!has_tech(tf_preq(10, triad == TRIAD_SEA), faction) && !has_abil(V->proto, 0x2000) && V->order_auto_type == 6)
        goto at_56c2d1;
    if (V->order == ORDER_MOVE_TO) goto finish;
    if ((here->bits & MAP_FUNGUS) && alt_of(here) >= 0x40) {
        V->order = 0xe;
        goto finish;
    }
    if (triad == TRIAD_SEA && alt_of(here) >= ALT_SHORE) goto at_57717f;

    // Fungus to clear round the faction's bases, ring by ring.
    l2c = 99999;
    for (l4c = 2; l4c <= (V->order_auto_type != 6 ? 3 : 6); l4c++) {
        if (V->order == ORDER_MOVE_TO) goto finish;
        for (l28 = 0; l28 < *game_base_count; l28++) {
            const struct base *B = &game_bases[l28];
            if (B->faction != faction) continue;
            if (triad == TRIAD_SEA ? !base_on_sea(l28, region) : tile(B->x, B->y)->region != region) continue;
            for (int i = l4c == 2 ? 1 : game_radius_count[l4c - 1]; i < game_radius_count[l4c]; i++) {
                int sx = xrange(B->x + game_radius_x[i]), sy = B->y + game_radius_y[i];
                l8 = sx, lc = sy;
                if (!on_map(sx, sy)) continue;
                const struct map_tile *t = tile(sx, sy);
                if (t->region != region || !(t->bits & MAP_FUNGUS) || alt_of(t) < 0x40) continue;
                if (t->bits & 3) {
                    l74 = owner_of(t);
                    if (l74 < 8) {
                        if (l74 != faction || l3c) continue;
                    } else {
                        l74 = -1;
                    }
                } else {
                    l74 = -1;
                }
                l3c = vector_dist_offset(x_dist(x, sx), abs(y - sy));
                if (!zoc_move(sx, sy, faction) && l3c < l2c) {
                    V->order = ORDER_MOVE_TO;
                    V->status_icon = 'f';
                    V->waypoint_x[0] = (int16_t)sx, V->waypoint_y[0] = (int16_t)sy;
                    l2c = l3c;
                    if (proto_of(V->proto)->plan == PLAN_COLONIZATION) V->movement_turns = 0;
                }
            }
        }
    }
    if (V->order == ORDER_MOVE_TO) goto finish;
    if (V->order_auto_type == 6) goto at_56c2d1;
    goto at_56c9e1;
at_56c2d1:
    V->order = ORDER_NONE;
    V->state &= ~0x200u;
    return 0;
at_56c9c2:
    V->order = ORDER_NONE;
    V->state &= ~0x200u;
    return 0;
at_56c30f: {
    // Roads or magtubes to the faction's bases (and its pact friends').
    if (V->order == 0x1b || V->order == 0x1c) {
        int b = base_at(V->waypoint_x[0], V->waypoint_y[0]);
        if (b >= 0 && game_bases[b].faction == faction) goto finish;
        V->order = ORDER_NONE;
    }
    l2c = 9999, l34 = (V->order_auto_type == 2) + 0x1b;
    l7c = -1;
    for (l28 = 0; l28 < *game_base_count; l28++) {
        const struct base *B = &game_bases[l28];
        if (!(B->faction == faction ||
              ((F->diplo_status[B->faction] & TREATY_PACT) && (near_dist || game_bases[near_base].faction == faction))))
            continue;
        int bx = B->x, cx = x, cy = y;
        l5c = B->y;
        if (tile(bx, l5c)->region != region) continue;
        l58 = 1, l30 = 0, l3c = 0;
        for (;;) {
            int flags = !has_tech(game_rules_basic->tech_road_fungus, faction) &&
                                !has_tech(game_terraform[10].preq_tech, faction)
                            ? 4
                            : 0;
            dir84 = path_find_at(game_path, cx, cy, bx, l5c, 0, -1, flags, -1, path_stale_next());
            if (dir84 < 0) break;
            cx = xrange(cx + game_radius_near_x[dir84]), cy += game_radius_near_y[dir84];
            l3c++;
            if (cx == bx && cy == l5c) break;
            const struct map_tile *t = tile(cx, cy);
            if ((!(t->bits & 1) || owner_of(t) > 7) && (!(t->bits & 4) || (!(t->bits & 8) && V->order_auto_type == 2)))
                l58 = 0;
            if (++l30 > 100) break;
            if (!l58) break;
        }
        if (l58) continue;
        if (B->faction != faction) l3c *= 2;
        int o = whose_territory(faction, cx, cy, NULL, 0);
        if (o >= 0 && o != faction && !(F->diplo_status[o] & TREATY_PACT)) l3c += 100;
        if (zoc_sea(cx, cy, faction)) l3c += 10;
        if (l3c < l2c) l2c = l3c, l7c = l28;
    }
    if (l7c >= 0) {
        V->status_icon = 'R';
        V->order = (uint8_t)l34;
        V->waypoint_x[0] = game_bases[l7c].x, V->waypoint_y[0] = game_bases[l7c].y;
        goto finish;
    }
    if (game_is_human(faction) || !(V->flags & 0x800)) {
        V->order = ORDER_NONE;
        V->state &= ~0x200u;
        veh_skip(veh);
        if (faction != *game_current_faction) return 0;
        game_console_focus(game_console, x, y, faction);
        game_x_pop(text(V->order_auto_type == 1 ? 0x68e908 : 0x68e918), 0);
        return 0;
    }
    goto at_56c9e1;
}
at_56c9e1:
    if (near_dist == 1 && game_bases[near_base].faction != faction && triad != TRIAD_AIR &&
        game_weapons[proto_of(V->proto)->weapon].offense) {
        const struct base *B = &game_bases[near_base];
        const struct map_tile *t = tile(B->x, B->y);
        if ((!(t->bits & 2) || owner_of(t) > 7) && !(F->diplo_status[B->faction] & 7) &&
            (triad == TRIAD_SEA) == (alt_of(t) < ALT_SHORE) && plan < 10) {
            l88 = 1;
            hurt = 0;
            V->order = ORDER_NONE;
        }
    }
    if (near_base != own_base && (F->diplo_status[game_bases[near_base].faction] & 2) && !at_sea &&
        !(F->diplo_status[game_bases[near_base].faction] & TREATY_PACT) && V->order != ORDER_MOVE_TO) {
        int bf = game_bases[near_base].faction;
        if (whose_territory(faction, x, y, NULL, 0) == bf && F->diplo_friction[bf] < 10 &&
            !(F->diplo_status[bf] & 0xc00) &&
            ((V->faction == bf && (V->flags & 0x440) != 0x440) || ((1 << bf) & V->visibility))) {
            if (own_base >= 0 && region == tile(game_bases[own_base].x, game_bases[own_base].y)->region) {
                go_to(veh, 'w', game_bases[own_base].x, game_bases[own_base].y);
                goto at_57707f;
            }
            if (triad_of(V->proto) == TRIAD_LAND) {
                for (int i = 0; i < 8; i++) {
                    l8 = xrange(game_radius_near_x[i] + x), lc = game_radius_near_y[i] + y;
                    if (on_map(l8, lc) && alt_of(tile(l8, lc)) < ALT_SHORE) {
                        l20 = stack_fix(veh_at(l8, lc));
                        if (l20 >= 0 && game_vehs[l20].faction == faction && stack_check(l20, 8, -1, -1, -1) > 0)
                            {
                            dir84 = i;
                            goto at_578b26;
                        }
                    }
                }
            }
        }
    }
    if ((V->state & 0x200) && V->order_auto_type == 11 && triad != TRIAD_AIR) {
        l90 = 1;
        goto at_57707f;
    }
    if (V->status_icon == 'F') {
        if (V->order != ORDER_MOVE_TO) V->status_icon = '-';
        if (!(V->state & 0x100) || plan != PLAN_DEFENSIVE) goto at_56cfc4;
        V->status_icon = '-';
        if (V->home_base >= 0) {
            l28 = V->home_base;
            const struct base *B = &game_bases[l28];
            if (x == B->x && y == B->y) {
                game_bases[l28].state &= ~0x4000000u;
            } else if (tile(B->x, B->y)->region == region) {
                go_to(veh, 'F', B->x, B->y);
                goto at_57707f;
            }
        }
        goto at_56cdc1;
    }
at_56cfc4:
    if (plan == PLAN_ALIEN_ARTIFACT) {
        // An artifact: studied at a base with a network node, or given to a
        // base building a secret project; else taken to the best base.
        if (!(V->flags & 0x20)) {
            if (!own_dist) {
                if (net_own) {
                    veh_skip(veh);
                    game_synch_veh(veh);
                    return 1;
                }
                if (((has_fac(FAC_NETWORK_NODE, own_base, 0) && !(game_bases[own_base].state & 0x400)) ||
                     game_secret_projects[20] == own_base) &&
                    em_study(veh))
                    return 1;
            }
            if (!game_is_human(faction) && !own_dist && game_bases[own_base].queue[0] < -0x45 &&
                game_bases[own_base].minerals > 99) {
                int m = proto_of(V->proto)->cost * 10 / 2 + game_bases[own_base].minerals;
                game_bases[own_base].minerals = m;
                game_bases[own_base].minerals_2 = m;
                game_bases[own_base].production_last = game_bases[own_base].queue[0];
                kill(veh);
                return 1;
            }
        }
        l2c = 9999, l7c = -1;
        for (l28 = 0; l28 < *game_base_count; l28++) {
            const struct base *B = &game_bases[l28];
            if (B->faction != faction || tile(B->x, B->y)->region != region) continue;
            int d = vector_dist_offset(x_dist(x, B->x), abs(y - B->y));
            if (game_secret_projects[20] != l28 && (B->queue[0] > -0x47 || B->minerals < 100 || game_is_human(faction))) {
                if (!has_fac(FAC_NETWORK_NODE, l28, 0)) d += 1000;
                if (!(B->state & 0x400) &&
                    stack_check(veh_at(B->x, B->y), 2, 12, -1, -1) <= (x == B->x && y == B->y)) {
                } else {
                    d += 2000;
                }
            }
            if (d < l2c) l7c = l28, l2c = d;
        }
        if (l7c >= 0) {
            go_to(veh, 't', game_bases[l7c].x, game_bases[l7c].y);
            goto finish;
        }
        goto at_57707f;
    }
    if (plan != PLAN_SUPPLY_CONVOY) goto at_56cdc1;
    // A supply convoy: back into a base building a project, or to a square
    // to work.
    if (!game_is_human(faction)) {
        l2c = 9999, l7c = -1;
        for (l28 = 0; l28 < *game_base_count; l28++) {
            const struct base *B = &game_bases[l28];
            if (B->faction != faction || tile(B->x, B->y)->region != region) continue;
            int d = (abs(y - B->y) + x_dist(x, B->x)) >> 1;
            if (B->queue[0] < -0x45 && d < l2c) {
                l7c = l28, l2c = d;
                if (!d) {
                    int m = game_bases[l28].minerals + veh_cost(V->proto, -1, NULL) * 10;
                    game_bases[l28].minerals = m;
                    game_bases[l28].minerals_2 = m;
                    game_bases[l28].production_last = game_bases[l28].queue[0];
                    kill(veh);
                    return 1;
                }
            }
        }
        if (l7c >= 0) {
            go_to(veh, 'V', game_bases[l7c].x, game_bases[l7c].y);
            goto at_57707f;
        }
    }
    if (V->order == 3) {
        const struct map_tile *t = here;
        switch (V->order_auto_type) {
        case 0:
            if ((t->bits & 0x8000) || (t->rocky_lock & 0xc0) > 0x7f) goto finish;
            break;
        case 1:
            if (t->bits & 0x10) goto finish;
            break;
        default:
            if (V->order_auto_type != 2 || (t->bits & 0x40) || alt - 2 < 0 || alt == 2) goto finish;
        }
        add_goal(faction, 9, 1, x, y, -1);
        goto finish;
    }
    if (V->order != ORDER_MOVE_TO) {
        l2c = 0;
        for (l48 = 0; l48 < 0x484; l48 += 4) {
            int i = l48 / 4;
            l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
            if (!on_map(l8, lc)) continue;
            const struct map_tile *t = tile(l8, lc);
            if (t->region != region || (triad != TRIAD_AIR && (alt_of(t) < ALT_SHORE) != (triad == TRIAD_SEA))) continue;
            if (!can_convoy(l8, lc)) continue;
            if (!bonus_at(l8, lc, 0) && (tile(l8, lc)->rocky_lock & 0xc0) <= 0x7f && alt_of(tile(l8, lc)) <= 0xbf) continue;
            if (whose_territory(faction, l8, lc, NULL, 0) != faction) continue;
            base_find_2(l8, lc, faction);
            if (*game_base_find_dist <= 2) continue;
            int taken = 0;
            for (l20 = 0; l20 < *game_veh_count; l20++) {
                const struct veh *U = &game_vehs[l20];
                if (U->faction == faction && proto_of(U->proto)->plan == PLAN_SUPPLY_CONVOY && l20 != veh &&
                    ((U->x == l8 && U->y == lc && U->order == 3) ||
                     (U->order == ORDER_MOVE_TO && V->waypoint_x[0] == l8 && V->waypoint_y[0] == lc))) {
                    taken = 1;
                    break;
                }
            }
            if (taken) continue;
            int dy = abs(y - lc), dx = x_dist(x, l8);
            int best = 1;
            l7c = 0;
            for (int r = 0; r < 3; r++) {
                int v = resource_yield(r, faction, V->home_base, l8, lc) * 2;
                if (r == 1) v++;
                if (best <= v) best = v, l7c = r;
            }
            int score = best * 0x200 - ((dy + dx) >> 1);
            if (l2c < score) {
                l2c = score;
                go_to(veh, 'v', l8, lc);
                if (!l48) {
                    V->order = 3;
                    V->order_auto_type = (uint8_t)l7c;
                    goto finish;
                }
            }
        }
    }
    goto at_57707f;
at_56cdc1:
    // Units given to a faction the AI is friendly with.
    l74 = terr;
    if (!game_is_human(faction) && terr != faction && terr > 0) {
        uint32_t st = F->diplo_status[terr];
        if ((st & 8) && (game_weapons[proto_of(V->proto)->weapon].offense > 2 || plan == PLAN_DEFENSIVE) &&
            plan != PLAN_RECONNAISSANCE && !(st & 0x40c30) && (st & 3) && game_factions[terr].integrity_blemishes < 3) {
            struct faction_data *T = &game_factions[terr];
            if (V->order == ORDER_MOVE_TO) {
                int o = whose_territory(faction, V->waypoint_x[0], V->waypoint_y[0], NULL, 0);
                if (o != faction && o != terr && o > 0 && plan < 2 && triad == TRIAD_LAND &&
                    (T->ranking < F->ranking ||
                     (int)(((~F->diplo_status[terr] & 1) | 2) * T->mil_strength) / 2 < game_factions[o].mil_strength) &&
                    (F->diplo_status[o] & 0x10) && (T->diplo_status[o] & 0x10)) {
                    em_give(veh, terr);
                    return 1;
                }
            }
            int tplan = T->region_base_plan[region];
            if (tplan == 2 && plan < 3 && (F->diplo_status[terr] & TREATY_PACT)) {
                em_give(veh, terr);
                return 1;
            }
            if (!F->region_total_bases[region] && T->region_total_bases[region] && triad == TRIAD_LAND &&
                ((tplan == 2 && ((F->diplo_status[terr] & TREATY_PACT) || T->ranking < 7)) ||
                 (tplan != 8 && (F->diplo_status[terr] & TREATY_PACT)))) {
                em_give(veh, terr);
                return 1;
            }
        }
    }
    if (hurt == 0 || (st6 && hurt < 4)) goto at_56e011;
    goto at_hurt;
at_56e011:
    if (plan == PLAN_INFO_WARFARE) {
        if (net_own) {
            V->order = ORDER_NONE;
            V->state &= ~0x200u;
            game_synch_veh(veh);
            game_net_await_synch(game_net_daemon);
            game_net_unlock_veh(game_net_daemon);
            return 1;
        }
        l4c = triad == TRIAD_SEA;
        if (!exploring && at_sea == l4c) {
            // A target within two squares: a probe team or a unit worth
            // its work, of a faction it may act against.
            for (l5c = 0; l5c < 0x54; l5c += 4) {
                int i = l5c / 4;
                l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
                if (!on_map(l8, lc)) continue;
                const struct map_tile *t = tile(l8, lc);
                if (!((triad == TRIAD_AIR || (alt_of(t) < ALT_SHORE) == l4c) && t->region == region)) continue;
                l20 = stack_fix(veh_at(l8, lc));
                if (l20 < 0 || game_vehs[l20].faction == faction) continue;
                if (!stack_check(l20, 2, 11, -1, -1) && !(base_who(l8, lc) < 0 && game_probe(veh, -1, l20, 0))) continue;
                uint32_t st = F->diplo_status[game_vehs[l20].faction];
                if ((!(st & TREATY_PACT) || !(st & 0x2000000)) &&
                    ((st & 0x30) || whose_territory(faction, l8, lc, NULL, 0) == faction)) {
                    go_to(veh, 'P', l8, lc);
                    goto at_57707f;
                }
            }
            // A base to work against, or one of its own to guard.
            l2c = 99999;
            for (l28 = 0; l28 < *game_base_count; l28++) {
                const struct base *B = &game_bases[l28];
                l50 = B->x;
                if (region != tile(l50, B->y)->region) continue;
                terr = x;
                int d = (abs(y - B->y) + x_dist(x, l50)) >> 1, v = d * 16;
                if (B->faction == faction) {
                    base_find_3(B->x, B->y, -1, -1, faction, -1);
                    int n = stack_check(veh_at(B->x, B->y), 2, 11, -1, -1);
                    v += ((n + (v != 0)) * 0x40 + *game_base_find_dist) * 0x40;
                } else {
                    if (has_project(15, B->faction)) continue;
                    if (B->state & 0x40) {
                        l50 = B->faction;
                        if ((F->diplo_status[l50] & 7) &&
                            (!(F->diplo_status[l50] & 0x20) ||
                             (F->region_total_bases[region] &&
                              (unsigned)((F->region_force_rating[region] >> 1) + F->region_total_combat_units[region]) <
                                  game_factions[l50].region_force_rating[region])))
                            continue;
                        v = d * 32;
                    }
                    int bf = B->faction;
                    uint32_t st = F->diplo_status[bf];
                    if (st & 0x10) v /= 2;
                    l50 = st & 1;
                    if (l50) {
                        v *= 2;
                        if (game_factions[bf].ranking <= 5 || (st & 0x1000000)) continue;
                    }
                    if (st & 2) v *= 2;
                    if (!((!l50 || !(st & 0x2000000)) &&
                          (!(st & 7) || (st & 0x20) ||
                           (F->tech_ranking < game_factions[bf].tech_ranking &&
                            (at_climax(faction) || !(F->diplo_status[bf] & 0x1000000))))))
                        continue;
                }
                if (v < l2c) {
                    l2c = v;
                    go_to(veh, 'p', B->x, B->y);
                }
            }
            if (V->order != ORDER_MOVE_TO) {
                kill(veh);
                return 1;
            }
            goto at_57707f;
        }
    }
    if (triad != TRIAD_LAND) goto at_56ed87;
    if (has_abil(V->proto, ABL_DROP_POD) && !V->moves_spent &&
        game_weapons[proto_of(V->proto)->weapon].offense) {
        // A drop pod unit: dropped on an enemy base in range, or taken to
        // a base of its own in range of one.
        if (stack_check(veh, 2, 2, -1, -1) && !(V->state & 0x20) && !game_is_human(faction) && hurt < 4) {
            if (!near_dist || (here->bits & 0x40000)) {
                l3c = drop_range(faction);
                l2c = 99999, l7c = -1;
                int i;
                for (i = 0; i < *game_base_count; i++) {
                    const struct base *B = &game_bases[i];
                    if (B->faction == faction || (F->diplo_status[B->faction] & TREATY_PACT) ||
                        !(F->diplo_status[B->faction] & 0xc10) || has_fac(0x1d, i, 0) || has_project(0x23, B->faction) ||
                        veh_who(B->x, B->y) >= 0)
                        continue;
                    l5c = B->x, terr = x;
                    int d = (abs(B->y - y) + x_dist(l5c, terr)) >> 1;
                    if (d <= l3c) {
                        l28 = i;
                        em_airdrop(veh, game_bases[i].x, game_bases[i].y, 3);
                        return 1;
                    }
                    if (d < l2c) l7c = i, l2c = d;
                }
                l28 = i;
                if (l7c >= 0) {
                    l28 = l7c, l2c = 99999;
                    for (l58 = 0; l58 < *game_base_count; l58++) {
                        const struct base *B = &game_bases[l58];
                        if (B->faction != faction) continue;
                        terr = abs(B->y - game_bases[l7c].y);
                        if ((terr + x_dist(B->x, game_bases[l7c].x)) >> 1 > l3c) continue;
                        if (stack_check(veh_at(B->x, B->y), 6, ABL_DROP_POD, -1, -1)) continue;
                        terr = abs(B->y - y);
                        if ((terr + x_dist(B->x, x)) >> 1 <= l3c) {
                            veh_skip(veh);
                            em_airdrop(veh, game_bases[l58].x, game_bases[l58].y, 3);
                            return 1;
                        }
                        if (tile(B->x, B->y)->region == region) {
                            terr = x;
                            int d = (abs(B->y - y) + x_dist(B->x, terr)) >> 1;
                            if (d < l2c) {
                                l2c = d;
                                go_to(veh, 'i', B->x, B->y);
                            }
                        }
                    }
                    goto at_56e62d;
                }
                V->order = ORDER_NONE;
                if (F->units_active[proto] > 1) {
                    l5c = -1, terr = -1;
                    for (l28 = 0; l28 < *game_base_count; l28++) {
                        const struct base *B = &game_bases[l28];
                        l74 = B->faction;
                        if (l74 == faction || (F->diplo_status[l74] & TREATY_PACT) || !(F->diplo_status[l74] & 0xc10) ||
                            has_fac(0x1d, l28, 0) || has_project(0x23, l74))
                            continue;
                        l24 = B->pop_size;
                        int c = (int8_t)game_factions[l74].region_base_plan[region];
                        if (c == 7) l24 *= 3;
                        if (c == 8) l24 *= 2;
                        if (c == 0) l24 /= 2;
                        l20 = stack_fix(veh_at(B->x, B->y));
                        if (stack_check(l20, 2, 4, -1, -1)) continue;
                        l34 = stack_check(l20, 0x10, -1, -1, -1);
                        for (l58 = 0; l58 < 0x60; l58 += 4) {
                            int i2 = l58 / 4 + 1;
                            l8 = xrange(game_radius_x[i2] + B->x), lc = game_radius_y[i2] + B->y;
                            if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE) continue;
                            if ((abs(y - lc) + x_dist(x, l8)) >> 1 > l3c) continue;
                            int a = anything_at(l8, lc);
                            if (a >= 0 && a != faction) continue;
                            int s = game_rnd(8) + l24;
                            if (a == faction) s = s * 3 / 4;
                            if (l58 < 0x20 && l34 > 1) s /= 2;
                            if (tile(l8, lc)->bits & 0x1608050) s *= 2;
                            if (tile(l8, lc)->bits & 4) s = (s << 2) / 3;
                            for (l30 = 0; l30 < 0x20; l30 += 4) {
                                int j = l30 / 4;
                                int nx = xrange(game_radius_near_x[j] + l8), ny = game_radius_near_y[j] + lc;
                                if (!on_map(nx, ny)) continue;
                                if (whose_territory(faction, nx, ny, NULL, 0) == faction) s = 0;
                                l20 = stack_fix(veh_at(nx, ny));
                                if (l20 < 0) continue;
                                if (game_vehs[l20].faction == faction) {
                                    if (has_abil(game_vehs[l20].proto, ABL_DROP_POD)) s += 0x20;
                                } else if (game_vehs[l20].faction == l74 && proto_of(game_vehs[l20].proto)->plan < 2) {
                                    s /= 2;
                                }
                            }
                            if (l2c < s) terr = l8, l5c = lc, l2c = s;
                        }
                    }
                    if (on_map(terr, l5c)) {
                        em_airdrop(veh, terr, l5c, 3);
                        return 1;
                    }
                }
            } else if (whose_territory(faction, x, y, NULL, 0) == faction && V->order != ORDER_MOVE_TO && own_base >= 0) {
                go_to(veh, 'I', game_bases[own_base].x, game_bases[own_base].y);
            }
        }
    }
at_56e62d:
    if (own_dist || region_plan == plan) goto at_56ed87;
    if (!game_bases[own_base].ai_plan) {
        if (!stack_check(veh, 2, 7, -1, -1) || plan == PLAN_TERRAFORMING) goto at_56ed87;
        if (plan != 1 || region_plan > 2) goto at_56e693;
        goto at_56ef2f;
    }
at_56e693:
    l90 = 1, lb8 = 1;
    goto at_57708a;
at_56ed87:
    if (!((plan != PLAN_NAVAL_SUPERIORITY || !(V->state & 0x200) || V->order_auto_type != 11) && triad != TRIAD_AIR))
        goto at_air;
at_56ef2f:
    if (exploring) goto at_57460a;
    if (plan == PLAN_NAVAL_SUPERIORITY) {
        // A warship: to a transport of its own about to sail, or an enemy
        // ship near.
        if (V->order == ORDER_MOVE_TO)
            l2c = (abs(y - V->waypoint_y[0]) + x_dist(x, V->waypoint_x[0])) >> 1;
        else
            l2c = 9999;
        for (l30 = 0; l30 < game_radius_count[game_range(veh_moves(veh) / game_rules_basic->move_rate_roads, 1, 8)];
             l30++) {
            l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
            if (!on_map(l8, lc)) continue;
            const struct map_tile *t = tile(l8, lc);
            if (alt_of(t) >= ALT_SHORE) continue;
            if (!own_dist ? !base_on_sea(own_base, t->region) : t->region != region) continue;
            if (base_who(l8, lc) >= 0) continue;
            l20 = stack_fix(veh_at(l8, lc));
            if (l20 < 0) continue;
            while (triad_of(game_vehs[l20].proto) != TRIAD_SEA)
                if (l20 < 0 || (l20 = game_vehs[l20].next_in_stack) < 0) break;
            if (l20 < 0) continue;
            int d = (abs(y - lc) + x_dist(x, l8)) >> 1;
            l3c = d;
            if (game_vehs[l20].faction == faction) {
                if (proto_of(game_vehs[l20].proto)->plan != PLAN_NAVAL_TRANSPORT || veh_moves(l20)) continue;
                if (stack_check(l20, 2, 6, -1, -1) > (game_vehs[l20].x == x && game_vehs[l20].y == y)) continue;
            } else if (!(F->diplo_status[game_vehs[l20].faction] & 0x10)) {
                continue;
            }
            if (d < l2c) {
                l2c = d;
                go_to(veh, 'n', l8, lc);
            }
        }
        for (l20 = 0; l20 < *game_veh_count; l20++) {
            const struct veh *U = &game_vehs[l20];
            if (U->faction == faction || !(F->diplo_status[U->faction] & 0xc30) || !(human_bit & U->visibility)) continue;
            if (alt_of(tile(U->x, U->y)) >= ALT_SHORE || base_who(U->x, U->y) >= 0) continue;
            l3c = (abs(y - U->y) + x_dist(x, U->x)) >> 1;
            if (proto_of(U->proto)->plan != PLAN_NAVAL_TRANSPORT) l3c *= 2;
            if (stack_check(l20, 1, -1, -1, -1) > 1) l3c /= 2;
            if (l3c < l2c) {
                add_goal(faction, 6, 2, U->x, U->y, -1);
                go_to(veh, 'N', U->x, U->y);
            }
        }
    }
    if (can_arty(V->proto, 1) && (triad == TRIAD_SEA || alt_of(here) >= ALT_SHORE || !near_dist) && !l88) {
        // Artillery: the best target in range.
        l2c = 0, l7c = -1;
        l5c = own_dist ? 25 : 81;
        int t7c;
        for (l30 = 0; l30 < l5c; l30++) {
            l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
            if (!on_map(l8, lc)) continue;
            if (anything_at(l8, lc) < 0) {
                int o;
                if (alt_of(tile(l8, lc)) >= ALT_SHORE && (o = whose_territory(faction, l8, lc, NULL, 0)) >= 0 && o != faction &&
                    (F->diplo_status[o] & 0x10)) {
                    const struct map_tile *t = tile(l8, lc);
                    uint32_t bits = t->bits;
                    int v = (bits & 0x80000000u ? 0x1e : 0) + 2;
                    if ((!F->region_total_bases[t->region] || F->region_base_plan[t->region] == 2 || triad != TRIAD_LAND ||
                         (bits & 0x80000000u)) &&
                        (bits & 0x80001000u) && ((bits & 0x80000000u) || triad != TRIAD_SEA || V->order != ORDER_MOVE_TO) &&
                        (bits & MAP_ARTY_TARGETS) && l2c < v) {
                        l2c = v;
                        l7c = l30;
                    }
                }
            } else {
                l20 = stack_fix(veh_at(l8, lc));
                if (l20 < 0 || game_vehs[l20].faction == faction || !(F->diplo_status[game_vehs[l20].faction] & 0x10))
                    continue;
                int v = (stack_check(l20, 2, 6, -1, -1) + stack_check(l20, 6, ABL_ARTILLERY, -1, -1)) * 8;
                if (triad == TRIAD_SEA) {
                    if (alt_of(tile(l8, lc)) < ALT_SHORE && base_who(l8, lc) >= 0) {
                        l7c = -1;
                        goto at_56f841;
                    }
                    if (alt_of(tile(l8, lc)) < ALT_SHORE) {
                        terr = l8;
                        if ((abs(y - lc) + x_dist(x, terr)) >> 1 <= veh_moves(veh) / game_rules_basic->move_rate_roads) {
                            l7c = -1;
                            goto at_56f841;
                        }
                    }
                    if (V->order == ORDER_MOVE_TO) continue;
                }
                if (!zocv || l30 < 9) v += stack_check(l20, 4, -1, -1, -1);
                terr = x;
                if ((abs(y - lc) + x_dist(terr, l8)) >> 1 > game_rules_basic->artillery_max_range && v > 1) v = 1;
                if (l2c < v) {
                    l2c = v;
                    l7c = l30;
                }
            }
        }
        if (l7c >= 0) {
            t7c = l7c;
            l8 = xrange(game_radius_x[t7c] + x), lc = game_radius_y[t7c] + y;
            terr = x;
            if ((abs(y - lc) + x_dist(terr, l8)) >> 1 <= game_rules_basic->artillery_max_range) {
                if (anything_at(l8, lc) < 0) {
                    if (net_own) {
                        game_log_say(text(0x68e934), veh, l8, lc);
                        game_message_data(0x2413, veh, l8, lc, 0, 0);
                        game_net_await_exec(game_net_daemon, 1);
                        return 1;
                    }
                    game_log_say(text(0x68e954), veh, l8, lc);
                    em_destroy(veh, 0, l8, lc);
                    return 1;
                }
                if (t7c > 8 || (triad == TRIAD_SEA && alt_of(tile(l8, lc)) >= ALT_SHORE)) {
                    if (!net_own) {
                        game_log_say(text(0x68e990), veh, t7c, 0);
                        em_fight(veh, t7c, 1, 1);
                        return 1;
                    }
                    game_log_say(text(0x68e974), veh, faction, 0);
                    game_message_veh(0x2402, veh, t7c, 0);
                    goto net_exec;
                }
            }
            if (!own_dist && t7c > 0x18 && (!st6 || region_plan) && anything_at(l8, lc) >= 0) goto at_578b34;
            goto at_56f8ad;
        }
    at_56f841:
        if (!own_dist && !st6 && region_plan && stack_check(veh, 15, -1, -1, -1) < 2 && triad != TRIAD_SEA &&
            ((here->bits & 0x4000) || region_plan != 2))
            goto at_578b34;
    }
at_56f8ad:
    del_site(faction, 3, x, y, 0);
    if ((plan != PLAN_COLONIZATION && triad != TRIAD_LAND) || l88 || (l4c = triad == TRIAD_SEA, at_sea != l4c))
        goto at_570002;
    // A base site within reach (colony pods), noted for the faction's
    // other units.
    if (plan == PLAN_COLONIZATION) del_site(faction, 8, x, y, 1);
    l28 = 0;
    if (plan == PLAN_COLONIZATION && V->order == ORDER_MOVE_TO &&
        vector_dist_offset(x_dist(x, V->waypoint_x[0]), abs(y - V->waypoint_y[0])) > 2)
        l28 = game_site_at(V->waypoint_x[0], V->waypoint_y[0], 0) - 1;
    for (l30 = 0; l30 < 25; l30++) {
        l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
        if (!on_map(l8, lc)) continue;
        const struct map_tile *t = tile(l8, lc);
        int a = alt_of(t);
        if ((a < ALT_SHORE) != l4c) continue;
        if ((t->bits & MAP_FUNGUS) && a >= 0x40) continue;
        if ((t->rocky_lock & 0xc0) > 0x40 && a >= ALT_SHORE) continue;
        if (t->region != region || (t->bits & 0x2000)) continue;
        if (base_territory(faction, l8, lc) >= 0) continue;
        if (triad == TRIAD_LAND) {
            if (!(F->region_flags[region] & 0x80) && !is_coast(l8, lc, 0) &&
                (int)(game_continents[region].open_terrain - F->region_good_base_tiles[region]) < 0x14)
                continue;
        } else if (triad == TRIAD_SEA) {
            int bad = 0;
            for (l34 = 0; l34 < 0x144; l34 += 4) {
                int j = l34 / 4;
                int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                if (!on_map(nx, ny)) continue;
                int b = base_at(nx, ny);
                if (b >= 0 && game_bases[b].faction != faction && (F->diplo_status[game_bases[b].faction] & 0x10) &&
                    (is_port(b, 0) || l30 < 0x15)) {
                    bad = 1;
                    break;
                }
                l20 = stack_fix(veh_at(nx, ny));
                if (l20 >= 0 && game_vehs[l20].faction != faction && (F->diplo_status[game_vehs[l20].faction] & 0x10) &&
                    stack_check(l20, 2, 6, -1, -1)) {
                    bad = 1;
                    break;
                }
            }
            if (bad) continue;
        }
        int site = game_site_at(l8, lc, 0);
        if (site <= l28) continue;
        base_find(l8, lc);
        if (site < (8 - *game_base_find_dist) * 2 && F->region_total_bases[region] && F->base_count >= 2) {
            int lim = region_plan != 8 ? 4 : 1;
            if (F->region_best_site[region] > lim) lim = F->region_best_site[region];
            if (site < lim) {
                if (plan != PLAN_COLONIZATION || V->order == ORDER_MOVE_TO) continue;
                if (game_continents[region].open_terrain >= (F->region_total_bases[region] + 1) * 6) continue;
            }
        }
        l28 = site;
        dir84 = l30;
    }
    if (!l28) goto at_570002;
    if (!dir84 && plan == PLAN_COLONIZATION) l28 += ++V->movement_turns;
    if (!l28 || dir84 < 0) goto at_570002;
    if (zocm && !(F->diplo_status[zocm - 1] & 7)) goto at_570002;
    if (plan != PLAN_COLONIZATION) {
        if (dir84 >= 9) goto at_570002;
        l8 = xrange(game_radius_x[dir84] + x), lc = game_radius_y[dir84] + y;
        l24 = l28 < 6 ? 1 : 2;
        if (near_dist >= 0) {
            l3c = (abs(lc - game_bases[near_base].y) + x_dist(l8, game_bases[near_base].x)) >> 1;
            if (l3c <= 4) l24 = 1;
            if (l3c >= 6 && l3c < 12 && l28 >= 10) l24 = 3;
        }
        del_site(faction, 8, x, y, 2);
        add_site(faction, 8, l24, l8, lc);
        goto at_570002;
    }
    if (!dir84) {
        if (net_own) {
            game_log_say(text(0x68e9a4), veh, faction, 0);
            game_message_base(0x2408, veh, text(0x93fc30), 0);
            goto net_exec;
        }
        em_build(veh);
        if (F->base_count != 1) return 1;
        for (int u = 0; u < *game_veh_count; u++)
            if (game_vehs[u].faction == faction && proto_of(game_vehs[u].proto)->plan == PLAN_TERRAFORMING)
                go_to(u, 'C', x, y);
        return 1;
    }
    V->status_icon = 'b';
    if (dir84 < 9) {
        dir84--;
        goto at_578b26;
    }
    go_to(veh, 'b', xrange(game_radius_x[dir84] + x), game_radius_y[dir84] + y);
    goto at_57000c;
at_570002:
    if (plan != PLAN_COLONIZATION) {
        if (plan == PLAN_RECONNAISSANCE) goto at_5702f7;
        goto at_5702ed_speed;
    }
at_57000c:
    if (triad == TRIAD_SEA && V->order != ORDER_MOVE_TO && at_sea) {
        // A sea colony pod: the best site in its sea away from enemies.
        l2c = 0;
        for (lc = 0; lc < *game_map_height; lc++) {
            for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                if (!on_map(l8, lc) || tile(l8, lc)->region != region || anything_at(l8, lc) >= 0) continue;
                if ((tile(l8, lc)->bits & MAP_FUNGUS) && alt_of(tile(l8, lc)) >= 0x40) continue;
                int s = game_site_at(l8, lc, 0);
                int d = vector_dist_offset(x_dist(x, l8), abs(y - lc));
                l24 = s * 10000 / (d + 1);
                l28 = base_find(l8, lc);
                if (l28 >= 0 && game_bases[l28].faction != faction && (F->diplo_status[game_bases[l28].faction] & 0x10))
                    l24 /= 2;
                if (l2c > l24) continue;
                int bad = 0;
                for (l4c = 0; l4c < 0x144; l4c += 4) {
                    int j = l4c / 4;
                    int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                    if (!on_map(nx, ny) || alt_of(tile(nx, ny)) >= ALT_SHORE) continue;
                    l28 = base_at(nx, ny);
                    if (l28 >= 0 && game_bases[l28].faction != faction &&
                        (game_factions[game_bases[l28].faction].diplo_status[faction] & 0x10)) {
                        bad = 1;
                        break;
                    }
                    l20 = stack_fix(veh_at(nx, ny));
                    if (l20 >= 0 && game_vehs[l20].faction != faction &&
                        (game_factions[game_vehs[l20].faction].diplo_status[faction] & 0x10)) {
                        bad = 1;
                        break;
                    }
                }
                if (bad) continue;
                l2c = l24;
                go_to(veh, 'c', l8, lc);
            }
        }
    }
at_5702f7:
    if ((V->order != ORDER_MOVE_TO || !near_dist) && triad == TRIAD_LAND && !at_sea) {
        // Exploration and the faction's base sites on its continent.
        l34 = 0, l4c = 0;
        for (int k = 0; k < 25; k++) {
            const struct goal *G = &F->sites[k];
            if (G->type != plan || G->priority <= 0) continue;
            if (tile(G->x, G->y)->region != region) continue;
            l34++;
            if (plan == PLAN_RECONNAISSANCE) {
                if (!goody_at(G->x, G->y)) continue;
                l4c++;
            } else {
                int u = F->region_best_site[region] < 9 ? F->region_best_site[region] : 8;
                if (region_plan != 8 && u < 4) u = 4;
                if (game_site_at(G->x, G->y, 0) < u) continue;
                l4c++;
            }
        }
        l30 = V->order == ORDER_MOVE_TO;
        terr = 0;
        if (!l34 || !((*game_current_turn + faction) & (l4c ? 15 : 3)) || !near_dist) {
            l2c = 0;
            for (lc = 0; lc < *game_map_height; lc++) {
                for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                    if (!on_map(l8, lc) || tile(l8, lc)->region != region) continue;
                    l58 = whose_territory(faction, l8, lc, NULL, 0);
                    if (!(region_plan == 8 || l58 == faction || l58 < 0)) continue;
                    if (base_territory(faction, l8, lc) >= 0) continue;
                    if ((!l34 || !((*game_current_turn + faction) & 15)) &&
                        (is_known(l8, lc, faction) || (*game_difficulty > 3 && !game_is_human(faction))) &&
                        goody_at(l8, lc)) {
                        int b = base_find(l8, lc);
                        if ((b >= 0 && game_bases[b].faction == faction) || l58 == faction)
                            add_site(faction, 3, (*game_base_find_dist < 6) + 1, l8, lc);
                    }
                    const struct map_tile *t = tile(l8, lc);
                    int a = alt_of(t);
                    if (a < ALT_SHORE || ((t->bits & MAP_FUNGUS) && a >= 0x40) || (t->bits & 0x2000) ||
                        (t->rocky_lock & 0xc0) >= 0x41)
                        continue;
                    if ((l58 == faction || l58 < 0) && !is_known(l8, lc, faction)) {
                        l24 = 0;
                        int seen = 0;
                        for (l4c = 0; l4c < 0x54; l4c += 4) {
                            int j = l4c / 4;
                            int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                            if (!on_map(nx, ny) || tile(nx, ny)->region != region) continue;
                            if (at_site(faction, 3, nx, ny)) {
                                seen = 1;
                                break;
                            }
                            if (!is_known(nx, ny, faction)) {
                                l24++;
                                if (goody_at(nx, ny)) l24++;
                            } else if ((*game_difficulty > 3 && !game_is_human(faction)) || l4c < 0x24) {
                                l24 |= 0x20;
                            }
                        }
                        if (!seen && l24 > 0x20) add_site(faction, 3, l24 < 0x28 ? (l24 > 0x22) + 1 : 3, l8, lc);
                    }
                    if (region_plan != 8 && l58 != faction) continue;
                    l4c = region * 2 + faction * 0x20cc;
                    if (!((F->region_flags[region] & 0x80) || is_coast(l8, lc, 0) ||
                          (int)(game_continents[region].open_terrain - F->region_good_base_tiles[region]) > 0x13))
                        continue;
                    l24 = game_site_at(l8, lc, 0);
                    int rtb = F->region_total_bases[region];
                    if (!(l24 && ((8 - *game_base_find_dist) * 2 - (rtb < 2 ? 2 : 0) <= l24 ||
                                  F->region_best_site[region] <= l24 || !rtb || F->base_count < 2)))
                        continue;
                    l5c = 1;
                    for (terr = 0; terr < 0x60; terr += 4) {
                        int j = terr / 4 + 1;
                        int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                        if (!on_map(nx, ny)) continue;
                        const struct map_tile *n = tile(nx, ny);
                        int s2;
                        if (n->region == region && (!(n->bits & MAP_FUNGUS) || alt_of(n) < 0x40) && !(n->bits & 0x2000) &&
                            (n->rocky_lock & 0xc0) < 0x41 && base_territory(faction, nx, ny) < 0 && !(n->bits & 0x1000) &&
                            (s2 = game_site_at(nx, ny, 0)) >= l24 && (s2 != l24 || at_site(faction, 8, nx, ny)) &&
                            ((F->region_flags[region] & 0x80) || is_coast(nx, ny, 0) ||
                             (int)(game_continents[region].open_terrain - F->region_good_base_tiles[region]) > 0x13) &&
                            (region_plan == 8 || whose_territory(faction, nx, ny, NULL, 0) == faction)) {
                            l5c = 0;
                            break;
                        }
                    }
                    int v = (l24 << 16) / (((abs(y - lc) + x_dist(x, l8)) >> 1) + 1);
                    if (!l5c) continue;
                    if (!(l2c < v || is_known(l8, lc, faction)) && !(*game_difficulty > 3 && !game_is_human(faction)))
                        continue;
                    if (path_find_at(game_path, l8, lc, x, y, whose_territory(faction, l8, lc, NULL, 0), faction, 0x201, -1,
                                     path_stale_next()) < 0)
                        continue;
                    if (is_known(l8, lc, faction) || (*game_difficulty > 3 && !game_is_human(faction))) {
                        del_site(faction, 8, l8, lc, 2);
                        add_site(faction, 8, 2, l8, lc);
                        l34++;
                    }
                    if (l2c < v) {
                        l2c = v;
                        if (!l30 && plan == PLAN_COLONIZATION) go_to(veh, 'c', l8, lc);
                    }
                }
            }
            if (plan != PLAN_RECONNAISSANCE && !l30) {
                int n = l34;
                if (V->order == ORDER_MOVE_TO) {
                    if (!l34) n = 1;
                    add_site(faction, 3, 2, V->waypoint_x[0], V->waypoint_y[0]);
                    if (!is_known(V->waypoint_x[0], V->waypoint_y[0], faction) &&
                        (*game_difficulty < 4 || game_is_human(faction))) {
                        V->order = ORDER_NONE;
                    } else {
                        del_site(faction, 8, l8, lc, 2);
                        add_site(faction, 8, 3, V->waypoint_x[0], V->waypoint_y[0]);
                    }
                }
                if (!n) {
                    F->region_good_tiles[region] -= 10;
                    *(uint8_t *)&F->region_flags[region] |= 9;
                    if (region_plan == 8) region_plan = 7, F->region_base_plan[region] = 7;
                }
            }
        }
    } else {
        // Not a land unit on land, or on its way with a base near: the
        // original goes straight on (to 0x570c89), past the colony pod's
        // search below for its own base when in a friend's territory.
        goto at_5702ed_speed;
    }
    if (plan == PLAN_COLONIZATION && V->order != ORDER_MOVE_TO && (l74 = whose_territory(faction, x, y, NULL, 0)) >= 0 &&
        l74 != faction && (F->diplo_status[l74] & 3) && (l28 = base_find_3(x, y, faction, region, -1, -1)) >= 0) {
        go_to(veh, 'k', game_bases[l28].x, game_bases[l28].y);
        goto at_57717f;
    }
at_5702ed_speed:
    if (game_rules_basic->move_rate_roads >= veh_speed(veh, 0) && (here->bits & 0x800000) &&
        triad_of(V->proto) != TRIAD_AIR && near_base >= 0) {
        l8 = xrange(x + 6);
        l28 = base_find(l8, y);
        char *say;
        if (l28 < 0 || *game_base_find_dist > 2) {
            if (game_bases[near_base].faction != faction || near_dist > 2) goto at_570df9;
            if (!net_own) goto at_570dd5;
            say = text(0x68e9e4);
        } else {
            if (!(F->diplo_status[game_bases[l28].faction] & 0x10)) goto at_570df9;
            if (!net_own) {
            at_570dd5:
                em_destroy(veh, 0x800000, -1, -1);
                return 0;
            }
            say = text(0x68e9c4);
        }
        game_log_say(say, veh, 0x800000, 0);
        game_message_data(0x2406, veh, 0x800000, 0, 0, 0);
        goto net_exec;
    }
at_570df9:
    if (game_rules_basic->move_rate_roads >= veh_speed(veh, 0) && (here->bits & 0x400000) &&
        triad_of(V->proto) != TRIAD_AIR && near_base >= 0 && near_dist <= 2 && game_bases[near_base].faction != faction &&
        own_dist >= 4 && (F->diplo_status[game_bases[near_base].faction] & 0x10)) {
        if (!net_own) {
            em_destroy(veh, 0x400000, -1, -1);
            return 0;
        }
        game_log_say(text(0x68ea04), veh, 0x400000, 0);
        game_message_data(0x2406, veh, 0x400000, 0, 0, 0);
        goto net_exec;
    }
    if (game_rules_basic->move_rate_roads >= veh_speed(veh, 0) && near_base >= 0 &&
        game_is_human(game_bases[near_base].faction) && (F->diplo_status[game_bases[near_base].faction] & 0x10) &&
        triad != TRIAD_AIR && (alt_of(here) < ALT_SHORE) == (triad == TRIAD_SEA) &&
        game_weapons[proto_of(V->proto)->weapon].offense && !l88 &&
        whose_territory(faction, x, y, NULL, 0) == game_bases[near_base].faction &&
        ((here->bits & 0x81208850) || ((here->bits & 4) && !F->region_total_bases[region]))) {
        if (!net_own) {
            game_log_say(text(0x68ea44), veh, x, y);
            em_destroy(veh, 0, -1, -1);
            goto finish;
        }
        game_log_say(text(0x68ea24), veh, 0, 0);
        game_message_data(0x2406, veh, 0, 0, 0, 0);
        goto net_exec;
    }
    {
        int keep = 2;
        if (region_plan < 3 || (F->region_flags[region] & 0x2000) || zocv || plan > 3 || triad != TRIAD_LAND) {
            if (triad == TRIAD_AIR) goto at_57137d;
        } else {
            V->state |= 0x40000000;
        }
        if (plan == PLAN_COLONIZATION || plan == PLAN_TERRAFORMING) goto at_57137d;
        if (!own_dist && !(V->state & 0x40000000)) {
            if (plan == PLAN_RECONNAISSANCE) {
                if (V->home_base != own_base && !zocv) goto at_57117a;
            } else if (plan != PLAN_DEFENSIVE && plan != 1) {
                keep = triad == TRIAD_LAND;
            }
            if (stack_check(veh, 16, -1, -1, -1) <= keep || zocv) goto at_57137d;
        }
    at_57117a:
        if (l88) goto at_57137d;
        int c = V->order;
        if ((c == 0 && plan != PLAN_NAVAL_TRANSPORT) || plan == PLAN_RECONNAISSANCE || (V->state & 0x40000000)) {
        at_5711c6:
            if (plan == PLAN_NAVAL_SUPERIORITY && c == ORDER_MOVE_TO) goto at_57137d;
        at_5711d4:
            if (*game_current_turn < 100 || l50 || own_dist < 7 || c != ORDER_MOVE_TO || V->status_icon == 'X' ||
                (V->state & 0x40000000) || plan == PLAN_RECONNAISSANCE) {
                // A supply pod near.
                l58 = 25;
                if (plan == PLAN_RECONNAISSANCE || triad == TRIAD_SEA) l58 = 49;
                if (plan == PLAN_NAVAL_TRANSPORT && (stack_check(veh, 2, 8, -1, -1) || hurt > 1)) l58 = 9;
                if ((plan != PLAN_NAVAL_SUPERIORITY ? 100 : 400) <= *game_current_turn && V->order == ORDER_MOVE_TO &&
                    V->status_icon != 'X' && plan != PLAN_RECONNAISSANCE)
                    l58 = 9;
                for (l30 = 1; l30 < l58; l30++) {
                    l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
                    if (!on_map(l8, lc) || !goody_at(l8, lc)) continue;
                    if (!is_known(l8, lc, faction) && !(*game_difficulty > 3 && !game_is_human(faction))) continue;
                    if ((alt_of(tile(l8, lc)) < ALT_SHORE) != (triad == TRIAD_SEA) || tile(l8, lc)->region != region)
                        continue;
                    if (V->state & 0x40000000) F->region_flags[region] |= 0x2000;
                    go_to(veh, 'G', l8, lc);
                    goto at_57707f;
                }
            }
        } else if (triad != TRIAD_LAND) {
            if (plan != PLAN_NAVAL_TRANSPORT) goto at_5711c6;
            if (!(V->state & 0x20000000)) goto at_57137d;
            goto at_5711d4;
        }
    }
at_57137d:
    dir84 = -1;
    goto at_57138d;
at_hurt:
    // Home for repairs: an airbase or base, a monolith, a transport.
    if ((!near_dist || (triad == TRIAD_AIR && (here->bits & 0x40000))) &&
        ((!(V->state & 8) && !(game_bases[near_base].state & 0x200000)) ||
         (hurt > 3 && veh_moves(veh) <= game_rules_basic->move_rate_roads))) {
        V->status_icon = 'd';
        l90 = 1, lb8 = 1;
        goto at_57138d;
    }
    if (has_project(0x16, faction) && !l88 && triad != TRIAD_AIR && !V->moves_spent && (!zocm || hurt > 3) &&
        !(V->state & 8)) {
        V->status_icon = 'd';
        l90 = 1;
        goto at_56d919;
    }
    if (own_dist == 1 && (hurt > 3 || !(game_bases[own_base].state & 0x200000))) {
        go_to(veh, 'd', game_bases[own_base].x, game_bases[own_base].y);
        goto finish;
    }
    if (near_dist <= (triad + 1) * (triad + 1) * hurt * 2 && !(game_bases[near_base].state & 0x200000) &&
        get_there(veh, game_bases[near_base].x, game_bases[near_base].y) && near_base == own_base) {
    at_56da06:
        go_to(veh, 'd', game_bases[near_base].x, game_bases[near_base].y);
        goto at_56d919;
    }
    l5c = near_base * (int)sizeof(struct base);
    l34 = faction * 0x833;
    if (F->diplo_status[game_bases[near_base].faction] & TREATY_PACT) goto at_56da06;
    if (hurt > 2 && game_weapons[proto_of(V->proto)->weapon].offense && !l88 && near_dist && triad == TRIAD_LAND) {
        for (int i = 0; i < 9; i++) {
            l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
            if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE || !(tile(l8, lc)->bits & 0x2000)) continue;
            if (*game_expansion && (V->proto == 16 || V->proto == 17 || V->proto == 18)) continue;
            l74 = anything_at(l8, lc);
            if (!(l74 < 0 || l74 == faction || (F->diplo_status[l74] & TREATY_PACT))) continue;
            if (!i) {
                l90 = 1;
                V->status_icon = 'd';
                V->damage = 0;
            } else {
                go_to(veh, 'd', l8, lc);
            }
            goto at_57708a;
        }
    }
    if (hurt > 3 && !l88 && near_dist && !V->moves_spent && !game_chassis[proto_of(V->proto)->chassis].range) {
        if (veh_moves(veh) == game_rules_basic->move_rate_roads && !zocm && !(V->state & 8) &&
            speed_proto(V->proto) > game_rules_basic->move_rate_roads && V->proto != 16 && V->proto != 17 &&
            V->proto != 18) {
            V->status_icon = 'd';
            if (triad != TRIAD_SEA) goto at_578b34;
            l90 = 1;
            goto at_574a88;
        }
    }
    if (hurt > 2 && !l88 && triad == TRIAD_LAND && near_dist && !veh_cargo(veh) && !(V->state & 8)) {
        for (int i = 0; i < 8; i++) {
            l8 = xrange(game_radius_near_x[i] + x), lc = game_radius_near_y[i] + y;
            if (!on_map(l8, lc) || alt_of(tile(l8, lc)) >= ALT_SHORE) continue;
            for (l20 = stack_fix(veh_at(l8, lc)); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
                if (game_vehs[l20].faction == faction && proto_of(game_vehs[l20].proto)->plan == PLAN_NAVAL_TRANSPORT &&
                    stack_check(l20, 8, -1, -1, -1) > 0) {
                    V->status_icon = 'd';
                    dir84 = i;
                    goto at_578b26;
                }
            }
        }
    }
    if (hurt < 3 || !at_sea || triad != TRIAD_LAND) goto at_56deb8;
    if (near_dist) {
        int ok = 1;
        for (l20 = veh_top(veh); l20 >= 0; l20 = game_vehs[l20].next_in_stack)
            if (proto_of(game_vehs[l20].proto)->plan == PLAN_NAVAL_TRANSPORT &&
                proto_power(l20) / 3 < game_vehs[l20].damage)
                ok = 0;
        if (!ok) goto at_56deb8;
    }
    V->status_icon = 'd';
    goto at_578b34;
at_56deb8:
    if (!l50) {
        int bf = game_bases[l5c / (int)sizeof(struct base)].faction;
        if ((F->diplo_status[bf] & 2) && !(F->diplo_status[bf] & TREATY_PACT) && reputation(bf, faction) <= 5)
            goto at_56df59;
    }
    if (!V->moves_spent && !zocv && triad_of(V->proto) == TRIAD_LAND && !(V->state & 8) &&
        (hurt > 1 || has_project(0x16, faction))) {
        V->status_icon = 'd';
        goto at_578b34;
    }
at_56df59:
    if (hurt >= 3 && triad != TRIAD_LAND) {
        action_home(veh, 2);
        if (V->order == ORDER_MOVE_TO) {
            V->status_icon = 'd';
            goto at_56d919;
        }
    }
    if (own_base < 0 || (!(V->state & 8) && !zocv) || hurt < 4 ||
        !get_there(veh, game_bases[own_base].x, game_bases[own_base].y))
        goto at_56e011;
    go_to(veh, 'd', game_bases[own_base].x, game_bases[own_base].y);
at_56d919:
    if (triad != TRIAD_SEA) goto at_57707f;
    goto at_574a88;
at_57138d:
    l6c = human_bit;
    if (!game_is_human(faction) && !own_dist && !forced && zocv) {
        // Obliterating a former base of a human that wronged it.
        if (!is_objective(own_base) &&
            (region_plan == 2 || F->region_total_bases[region] == 1 || (F->flags & 0x2000000))) {
            int ff = game_bases[own_base].faction_former;
            l74 = ff;
            if (stack_check(veh, 1, -1, -1, -1) <= (game_bases[own_base].pop_size + 1) / 2 &&
                stack_check(veh, 16, -1, -1, -1) < (V->damage != 0) + 2 && ff != faction && game_is_human(ff)) {
                uint32_t st = F->diplo_status[ff];
                if ((st & 0x20) && !(st & 3) &&
                    ((st & 0x40000) || game_factions[ff].integrity_blemishes > 5 ||
                     F->ai_fight * -2 + 3 < F->diplo_betrayed[ff])) {
                    game_log_say(text(0x68ea70), veh, x, y);
                    em_oblit(veh, own_base);
                    return 1;
                }
            }
        }
    }
    if (near_dist || (own_dist && !hurt) || exploring) goto at_571977;
    {
        // The garrison its base wants.
        l4c = garrison_check(near_base);
        int dc = defensive_check(near_base);
        l34 = stack_check(veh, 11, 0x10000000, 0x10000000, -1);
        int n = game_range(stack_check(veh, 2, 2, faction, -1), 0, dc) + l34;
        if (V->state & 0x10000000) {
            int t = l4c - dc;
            if (t < 1) t = 1;
            if (l34 <= t) plan = PLAN_DEFENSIVE;
        }
        if (region_plan == 2 || region_plan == 8) {
            n += stack_check(veh, 2, 1, faction, -1);
            if (plan == 1) plan = PLAN_DEFENSIVE;
        }
        if (!n) n = stack_check(veh, 2, 3, faction, -1);
        if (!st4 && !st6) l4c += stack_check(veh, 11, 0x40000, 0x40000, -1);
        if (!zoc_veh(x, y, faction) && region_plan > 2) {
            if (forced) goto at_571723;
            if (plan != PLAN_RECONNAISSANCE) {
                if (plan > 2) goto at_57172c;
                if (F->region_total_combat_units[region] >= 2) goto at_571723;
            }
            if (!(F->region_flags[region] & 0x2000)) goto at_571982;
        }
    at_571723:
        if (plan == PLAN_DEFENSIVE) goto at_571853;
    at_57172c:
        if (plan == PLAN_RECONNAISSANCE || (plan == 1 && (region_plan == 2 || region_plan == 8))) goto at_571853;
        if (stack_check(veh, 2, 2, faction, -1) || stack_check(veh, 2, 3, faction, -1) ||
            stack_check(veh, 2, 1, faction, -1) > (plan == 1))
            goto at_571977;
        add_goal(faction, 2, stack_check(veh, game_range(game_bases[near_base].pop_size, 1, 3), -1, -1, -1), x, y, -1);
        if (!game_weapons[proto_of(V->proto)->weapon].offense && !zoc_sea(x, y, faction)) goto at_571977;
        lb8 = 1, l90 = 1;
        if (forced) return 1;
        goto at_57707f;
    at_571853:
        if (n < l4c) add_goal(faction, 2, 2, x, y, -1);
        if (((n > l4c && (plan != PLAN_DEFENSIVE || !l34 || n > 2)) || st4 || st6) && n > 1 && (!l48 || !zocv))
            goto at_571977;
        if (!own_dist && V->home_base >= 0 && !game_is_human(faction)) V->home_base = (int16_t)own_base;
        V->order = ORDER_NONE;
        l90 = 1;
        lb8 = (n <= 1) + 1;
        if (forced) return 1;
        if (plan == PLAN_DEFENSIVE && region_plan > 2 && n >= l4c && !(game_bases[own_base].state & 0x4000000) &&
            (n > 1 || (!game_factions[0].region_total_combat_units[region] && !(F->flags & 0x10000))))
            goto at_571982;
        goto at_57707f;
    }
at_571977:
    if (forced) return 0;
at_571982:
    if (V->order != ORDER_MOVE_TO && (l48 || plan == PLAN_DEFENSIVE)) {
        // A monolith, or a native or weak enemy unit in its territory.
        V->order = ORDER_NONE;
        for (forced = 0; forced < 0x484; forced += 4) {
            int i = forced / 4;
            l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
            if (!on_map(l8, lc) || tile(l8, lc)->region != region) continue;
            if (whose_territory(faction, l8, lc, NULL, 0) != faction) continue;
            l3c = vector_dist_offset(x_dist(x, l8), abs(y - lc));
            l20 = stack_fix(veh_at(l8, lc));
            if (l20 < 0 || game_vehs[l20].faction == faction) {
                if (!(tile(l8, lc)->bits & 0x2000) || (V->state & 0x100) || !want_monolith(veh)) continue;
                l3c += 1000;
            } else {
                const struct veh *U = &game_vehs[l20];
                if (!l48 || !(!U->faction || (stack_check(l20, 1, -1, -1, -1) < 2 && (F->diplo_status[U->faction] & 0x10) &&
                                              game_weapons[proto_of(U->proto)->weapon].offense < 0)) ||
                    triad_of(U->proto) != TRIAD_LAND)
                    continue;
            }
            if (l3c < 9999) {
                l3c = 9999;
                int c = 'F';
                if (V->home_base >= 0 && (l28 = V->home_base, game_bases[l28].state & 0x4000000) && V->status_icon != 'F')
                    c = 'y';
                go_to(veh, c, l8, lc);
            }
        }
        if (V->order == ORDER_MOVE_TO) {
            l90 = 0, lb8 = 0;
            goto at_57707f;
        }
        if (lb8) goto at_57707f;
        if (l48) {
            for (l28 = 0; l28 < *game_base_count; l28++) {
                const struct base *B = &game_bases[l28];
                if (B->faction != faction || tile(B->x, B->y)->region != region) continue;
                l3c = vector_dist_offset(x_dist(x, B->x), abs(y - B->y));
                if (l28 != V->home_base) l3c += 1000;
                int v = l3c;
                if (stack_check(veh_at(B->x, B->y), 0x11, 0x13, faction, -1)) v *= 2, l3c = v;
                if (v < 9999) {
                    l3c = 9999;
                    go_to(veh, 'f', B->x, B->y);
                }
            }
        }
    }
    // A gate to a base of its own on a continent that wants it.
    if (plan != region_plan && region_plan > 2 && !own_dist && has_fac(0x21, own_base, 0) &&
        !(game_bases[own_base].state & 0x20000000)) {
        for (l28 = 0; l28 < *game_base_count; l28++) {
            const struct base *B = &game_bases[l28];
            if (B->faction != faction || !has_fac(0x21, own_base, 0) || (B->state & 0x20000000)) continue;
            int r = tile(B->x, B->y)->region;
            if (r == region) continue;
            int rp = F->region_base_plan[r];
            if (rp != plan && !(plan < 3 && rp < 3)) continue;
            if (!net_own) {
                game_log_say(text(0x68ea84), veh, l28, 0);
                em_gate(veh, l28);
                return 1;
            }
            if (!game_net_add_lock(game_net_daemon, 0, B->x, B->y)) {
                game_message_veh(0x2410, veh, l28, 0);
                goto net_exec;
            }
        }
    }
    if (!st4 && !st6 && x == F->target_x && y == F->target_y) {
        const struct unit_proto *P = proto_of(V->proto);
        if ((P->plan == 0 || P->plan == 1) && triad_of(V->proto) == TRIAD_LAND && !has_abil(V->proto, ABL_DROP_POD))
            V->state |= 0x40000;
    }
    if (!((plan > 1 && !st6) || V->order != ORDER_MOVE_TO)) goto at_after_72922;
at_572922:
    if (exploring || (plan & 0xf) != PLAN_TERRAFORMING) goto at_5741cf;
    if (zocv == 1 || (zocv && game_is_human(faction) && (F->diplo_status[zocv - 1] & 0x10))) goto at_57707f;
    {
        int auto3 = (V->state & 0x200) && proto_of(V->proto)->plan == PLAN_TERRAFORMING && V->order_auto_type == 3;
        l34 = auto3;
        l50 = 0;
        if (V->order == ORDER_MOVE_TO) goto at_572cd3;
        // The base whose work it does: its home, or the nearest of its own.
        if (auto3 || own_base < 0) {
            if (!auto3) {
                l28 = V->home_base;
                goto at_5729eb_tail;
            }
            l28 = V->home_base;
            if (l28 < 0) l28 = own_base;
        } else {
            l28 = own_base;
        at_5729eb_tail:
            if (V->home_base >= 0 && l28 != V->home_base) {
                if (l28 < 0) goto at_572cd6_out;
                int hb = V->home_base;
                if (vector_dist_offset(x_dist(x, game_bases[hb].x), abs(y - game_bases[hb].y)) < own_dist * 2) l28 = hb;
            }
        }
        if (l28 >= 0) {
            // The faction's terraforming goals near that base.
            for (l48 = 0; l48 < 75; l48++) {
                const struct goal *G = &F->goals[l48];
                if (G->type < 0 || (G->type & 0xf) != 9 || G->type == 0x29) continue;
                if (!on_map(G->x, G->y) || !G->priority) continue;
                terr = G->x;
                int d = vector_dist_offset(x_dist(terr, game_bases[l28].x), abs(G->y - game_bases[l28].y));
                if (!(d < 3 || (!l34 && G->base == l28))) continue;
                terr = abs(y - G->y);
                if (!((terr + x_dist(x, G->x)) & ~1)) {
                    V->order = ORDER_NONE;
                    l50 = 1;
                    if (!own_dist) goto at_578b34;
                    break;
                }
                const struct map_tile *t = tile(G->x, G->y);
                int sea = alt_of(t) < ALT_SHORE;
                if (sea != (triad == TRIAD_SEA) || (t->region != region && at_sea == sea)) continue;
                l74 = anything_at(G->x, G->y);
                if (!(l74 < 0 || l74 == faction || (F->diplo_status[l74] & TREATY_PACT))) continue;
                for (l20 = 0; l20 < *game_veh_count; l20++) {
                    const struct veh *U = &game_vehs[l20];
                    if (U->faction == faction && proto_of(U->proto)->plan == PLAN_TERRAFORMING && l20 != veh &&
                        ((U->x == G->x && U->y == G->y) ||
                         (U->order == ORDER_MOVE_TO && U->waypoint_x[0] == G->x && U->waypoint_y[0] == G->y)))
                        break;
                }
                if (l20 < *game_veh_count) continue;
                go_to(veh, 'g', F->goals[l48].x, F->goals[l48].y);
                goto at_572cd3;
            }
        }
        goto at_572cd6;
    at_572cd3:
    at_572cd6:
        if (l34 && !l50) {
            l50 = abs(y - game_bases[l28].y);
            if (((l50 + x_dist(x, game_bases[l28].x)) & ~1) > 4 && V->order != ORDER_MOVE_TO) {
                go_to(veh, 'h', game_bases[l28].x, game_bases[l28].y);
                if (V->order == ORDER_MOVE_TO) goto at_57707f;
            }
        }
    at_572cd6_out:
        if (triad == TRIAD_SEA && V->order != ORDER_MOVE_TO) {
            // A sea former: the nearest shelf square to improve near its
            // bases.
            l2c = 9999999;
            for (l28 = 0; l28 < *game_base_count; l28++) {
                const struct base *B = &game_bases[l28];
                if (B->faction != faction) continue;
                if (l34 && l28 != (V->home_base >= 0 ? V->home_base : own_base)) continue;
                for (forced = 0; forced < 0x54; forced += 4) {
                    int i = forced / 4;
                    l8 = xrange(game_radius_x[i] + B->x), lc = game_radius_y[i] + B->y;
                    if (!on_map(l8, lc)) continue;
                    int a = alt_of(tile(l8, lc));
                    if (a >= ALT_SHORE || a < 0x40) continue;
                    l74 = anything_at(l8, lc);
                    if (!(l74 < 0 || l74 == faction || (F->diplo_status[l74] & TREATY_PACT))) continue;
                    if (!can_terraform(faction, l8, lc, 0, l28)) continue;
                    l3c = vector_dist_offset(x_dist(x, l8), abs(y - lc)) * 8;
                    if (alt_of(tile(B->x, B->y)) < ALT_SHORE) l3c /= 2;
                    if (l28 == V->home_base) l3c /= 2;
                    int v = l3c;
                    if (bonus_at(l8, lc, 0)) v /= 2, l3c = v;
                    for (l20 = 0; l20 < *game_veh_count; l20++) {
                        const struct veh *U = &game_vehs[l20];
                        if (U->faction == faction && proto_of(U->proto)->plan == PLAN_TERRAFORMING && l20 != veh &&
                            ((U->x == l8 && U->y == lc) ||
                             (U->order == ORDER_MOVE_TO && U->waypoint_x[0] == l8 && U->waypoint_y[0] == lc)))
                            break;
                    }
                    if (l20 < *game_veh_count) continue;
                    if (v < l2c) {
                        l2c = v;
                        go_to(veh, 'o', l8, lc);
                    }
                }
            }
        }
        l50 = 0;
        if (!F->base_count) {
            // No base yet: to a colony pod of its own.
            for (l20 = 0; l20 < *game_veh_count; l20++)
                if (game_vehs[l20].faction == faction && proto_of(game_vehs[l20].proto)->plan == PLAN_COLONIZATION) {
                    go_to(veh, '#', game_vehs[l20].x, game_vehs[l20].y);
                    goto at_57707f;
                }
        }
        l48 = can_terraform(faction, x, y, at_goal(faction, 9, x, y) || l34, own_base);
        if (!l88 && (triad_of(V->proto) == TRIAD_AIR || at_sea == (triad_of(V->proto) == TRIAD_SEA)) && own_dist) {
            // Work where it stands: the faction's goals first.
            del_site(faction, at_sea ? 0x19 : 9, x, y, 0);
            if (at_goal(faction, 0x79, x, y) && !is_sensor(x, y)) {
                V->order = 0xd;
                goto finish;
            }
            if (at_goal(faction, 0x29, x, y) && !l34 && alt < 6) {
                int ec = F->energy_credits;
                int cost = game_range(terraform_cost(x, y, faction), 0, ec / 4);
                F->energy_credits = game_range(F->energy_credits - cost, 0, 99999);
                V->order = 0x14;
                del_site(faction, 8, x, y, 1);
                goto finish;
            }
            if (at_goal(faction, 0x49, x, y) && game_terrain_avail(0xc, at_sea, faction)) {
                V->order = 0x10;
                goto finish;
            }
            if (at_goal(faction, 0xd, x, y) && game_terrain_avail(0xd, at_sea, faction)) {
                V->order = 0x11;
                goto finish;
            }
            if (at_goal(faction, 0x69, x, y) && game_terrain_avail(0xe, at_sea, faction)) {
                V->order = 0x12;
                goto finish;
            }
            if (!at_sea && !l34 && !game_is_human(faction) && game_terrain_avail(0x10, 0, faction) &&
                (game_continents[region].tile_count < 0x32 ||
                 (game_continents[region].tile_count < 0x96 && *game_base_find_dist > 3)) &&
                near_dist > 1 && alt_of(here) < 0xc0) {
                int high = 0;
                for (int i = 0; i < 49; i++) {
                    l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
                    if (on_map(l8, lc) && alt_of(tile(l8, lc)) >= 0xc0) {
                        high = 1;
                        break;
                    }
                }
                if (!high) {
                    V->order = 0x14;
                    goto finish;
                }
            }
            if (own_dist < 3) {
                uint32_t bits = here->bits;
                if (((bits & 0x10) || (bits & 0x200000)) && !(bits & 4) &&
                    (!(bits & MAP_FUNGUS) || alt_of(here) < 0x40) && !at_sea &&
                    (!game_is_human(faction) || !(*game_more_preferences & 0x400000))) {
                    V->order = 9;
                    goto finish;
                }
                if (own_dist > 2) {
                    l58 = 0;
                } else {
                    l58 = radius_move(game_bases[own_base].x, game_bases[own_base].y, x, y, 0x15);
                }
            } else {
                l58 = 0;
            }
            if (!at_goal(faction, at_sea ? 0x19 : 9, x, y) && (x != V->waypoint_x[0] || y != V->waypoint_y[0]) &&
                !stack_check(veh, 9, 3, -1, -1) && (!l58 || !(game_bases[own_base].worked_tiles & (1u << (l58 & 31)))))
                l30 = 0;
            else
                l30 = 1;
            if ((here->bits & MAP_FUNGUS) && alt_of(here) >= 0x40 && l48 &&
                (has_abil(V->proto, 0x2000) || has_tech(tf_preq(10, alt_of(here) < ALT_SHORE), faction)) &&
                (l30 || (own_dist < 3 && (game_bases[own_base].eco_damage || game_bases[own_base].pop_size > 5)))) {
                V->order = 0xe;
                goto finish;
            }
            if (l48 == 4 && (l30 || (l58 && game_bases[own_base].worked_tiles && (1u << (l58 & 31)) != 0))) {
                V->order = 4;
                V->status_icon = '+';
                goto finish;
            }
            if (!at_sea && (!game_is_human(faction) || !(*game_more_preferences & 0x400000))) {
                uint32_t bits;
                if (l30) goto at_5736cb;
                if (own_dist > 2) goto at_573762;
                bits = here->bits;
                if ((bits & 0x10) || (bits & 0x8000)) goto at_5736cb;
                if (!(bits & 0x200000) && !l48) goto at_573762_skip;
                if ((bits & MAP_FUNGUS) && alt_of(here) >= 0x40) goto at_573762;
            at_5736cb:
                bits = here->bits;
                if (!(bits & 4)) {
                    if (!(bits & MAP_FUNGUS) || alt_of(here) < 0x40 ||
                        has_tech(game_rules_basic->tech_improve_fungus, faction)) {
                        V->order = 9;
                        V->status_icon = 'r';
                        goto finish;
                    }
                } else if (has_tech(game_terraform[6].preq_tech, faction) && l30 && !(bits & 8)) {
                    V->order = 9;
                    V->status_icon = 'R';
                    goto finish;
                }
            }
        at_573762:
            if (l48 && (own_dist < 3 || stack_check(veh, 9, 3, -1, -1)) && (l30 || !V->order)) {
                V->order = (uint8_t)l48;
                V->status_icon = '+';
                goto finish;
            }
        at_573762_skip:
            if (game_is_human(faction) && !(*game_more_preferences & 0x20)) goto at_5739aa;
            if (!l30 && V->order) goto at_5739aa;
            if (is_sensor(x, y) && whose_territory(faction, x, y, NULL, 0) == faction && own_dist > 1 &&
                !(here->bits & 0x800) && has_tech(game_terraform[7].preq_tech, faction)) {
                V->order = 0xb;
                goto finish;
            }
            if (good_sensor(faction, x, y)) {
                V->order = 0xd;
                goto finish;
            }
            if (own_dist > 1 && own_dist < 4 && (here->bits & 4) && !(here->bits & 0x800) &&
                has_tech(game_terraform[7].preq_tech, faction)) {
                int near = 0;
                for (int i = 0; i < 9; i++) {
                    l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
                    if (on_map(l8, lc) && (tile(l8, lc)->bits & 0x800)) {
                        near = 1;
                        break;
                    }
                }
                if (!near) {
                    V->order = 0xb;
                    goto finish;
                }
            }
            if (l30 && has_tech(game_terraform[9].preq_tech, faction)) {
                l8 = x, lc = y;
                l50 = 1;
                if (!game_path_sensors(game_path, faction, &l8, &lc) && x == l8 && y == lc) {
                    V->order = 0xd;
                    goto finish;
                }
            }
        }
    at_5739aa:
        if (V->order != ORDER_MOVE_TO && !l88) {
            int i;
            if (!l34) {
                // The base with fewest formers for its distance.
                l28 = own_base;
                if (!(V->state & 0x200)) {
                    alt = x, l5c = y;
                } else {
                    alt = game_bases[own_base].x, l5c = game_bases[own_base].y;
                }
                l24 = 0;
                terr = own_base * (int)sizeof(struct base);
                for (i = 0; i < 20; i++) {
                    const struct base *O = &game_bases[terr / (int)sizeof(struct base)];
                    l8 = xrange(game_radius_x[i + 1] + O->x), lc = game_radius_y[i + 1] + O->y;
                    if (on_map(l8, lc) && tile(l8, lc)->region == region && (l20 = veh_at(l8, lc)) >= 0 &&
                        game_vehs[l20].faction == faction) {
                        l24 += stack_check(l20, 2, 9, -1, -1);
                        if (l8 == x && lc == y) l24--;
                    }
                }
                if (l24) {
                    l2c = 9999;
                    for (l58 = 0; l58 < *game_base_count; l58++) {
                        const struct base *B = &game_bases[l58];
                        if (B->faction != faction || tile(B->x, B->y)->region != region) continue;
                        int n = 0;
                        for (l48 = 0; l48 < 100; l48 += 4) {
                            int j = l48 / 4;
                            int nx = xrange(game_radius_x[j] + B->x), ny = game_radius_y[j] + B->y;
                            if (on_map(nx, ny) && tile(nx, ny)->region == region && (l20 = veh_at(nx, ny)) >= 0)
                                n += stack_check(l20, 2, 9, -1, -1);
                        }
                        if (n < l24) {
                            l3c = vector_dist_offset(x_dist(x, B->x), abs(y - B->y)) * (n + 1) * (n + 1);
                            if (l3c < l2c) l28 = l58, l2c = l3c;
                        }
                    }
                }
            } else {
                l28 = V->home_base;
                alt = game_bases[l28].x, l5c = game_bases[l28].y;
            }
            if (l28 >= 0) {
                l2c = 9999;
                for (l30 = 1; l30 < 21; l30++) {
                    int k = (l30 + *game_current_turn) % 21;
                    l8 = xrange(game_radius_x[k] + alt), lc = game_radius_y[k] + l5c;
                    if (!on_map(l8, lc) || base_who(l8, lc) >= 0) continue;
                    const struct map_tile *t = tile(l8, lc);
                    int sea = alt_of(t) < ALT_SHORE;
                    if (sea != (triad == TRIAD_SEA) || (t->region != region && at_sea == sea)) continue;
                    if (zoc_move(l8, lc, faction)) continue;
                    l20 = veh_at(l8, lc);
                    if (l20 >= 0 && (game_vehs[l20].faction != faction || stack_check(l20, 2, 9, -1, -1))) continue;
                    if (!can_terraform(faction, l8, lc, game_is_human(faction) && l34, l28)) continue;
                    l24 = vector_dist_offset(x_dist(game_bases[l28].x, l8), abs(game_bases[l28].y - lc));
                    if (l24 >= 3) continue;
                    if (!(game_bases[l28].worked_tiles & (1u << (l30 & 31))) && (!(t->bits & MAP_FUNGUS) || alt_of(t) < 0x40))
                        l24 *= 3;
                    if (l24 >= l2c) continue;
                    for (l20 = 0; l20 < *game_veh_count; l20++) {
                        const struct veh *U = &game_vehs[l20];
                        if (U->faction == faction && proto_of(U->proto)->plan == PLAN_TERRAFORMING && l20 != veh &&
                            ((U->x == l8 && U->y == lc) ||
                             (U->order == ORDER_MOVE_TO && U->waypoint_x[0] == l8 && U->waypoint_y[0] == lc)))
                            break;
                    }
                    if (l20 < *game_veh_count) continue;
                    l2c = l24;
                    go_to(veh, 'w', l8, lc);
                }
                if (V->order == ORDER_MOVE_TO) goto at_57707f;
            }
            if (!l50 && (!game_is_human(faction) || (*game_more_preferences & 0x20))) {
                l8 = x, lc = y;
                if (!game_path_sensors(game_path, faction, &l8, &lc)) {
                    if (l8 == x && lc == y) {
                        V->order = 0xd;
                        goto finish;
                    }
                    if (l34 && vector_dist_offset(x_dist(x, l8), abs(y - lc)) > 2) goto at_57408e;
                    for (l20 = 0; l20 < *game_veh_count; l20++) {
                        const struct veh *U = &game_vehs[l20];
                        if (U->faction == faction && proto_of(U->proto)->plan == PLAN_TERRAFORMING && l20 != veh &&
                            ((U->x == l8 && U->y == lc) ||
                             (U->order == ORDER_MOVE_TO && U->waypoint_x[0] == l8 && U->waypoint_y[0] == lc)))
                            goto at_57408e;
                    }
                    go_to(veh, 's', l8, lc);
                    goto at_57707f;
                }
            }
        at_57408e:
            if (own_dist && !at_sea && F->region_total_bases[region]) {
                int b = V->home_base < 0 ? own_base : V->home_base;
                if (b >= 0) {
                    if (tile(game_bases[b].x, game_bases[b].y)->region != region) b = own_base;
                    if (b >= 0) {
                        int d = vector_dist_offset(x_dist(x, game_bases[b].x), abs(y - game_bases[b].y));
                        if ((*game_current_turn < 0x33) + 3 < d || zocm) {
                            go_to(veh, 'b', game_bases[b].x, game_bases[b].y);
                            goto at_57707f;
                        }
                    }
                }
            }
            if (l34 && !V->order) {
                V->state &= ~0x200u;
                V->order = ORDER_NONE;
                return 0;
            }
        }
    }
at_5741cf:
    // Patrolling its borders, or the faction's territory most at risk.
    if (region_plan > 2 && game_continents[region].tile_count <= F->region_visible_tiles[region] && plan < 4 &&
        triad == TRIAD_LAND && V->order != ORDER_MOVE_TO && !at_sea) {
        if (coast_or_border(x, y, x, y, faction)) {
            int d = ((int8_t)V->unk_29 + 4) % 8;
            l8 = xrange(game_radius_near_x[d] + x), lc = game_radius_near_y[d] + y;
            if (!coast_or_border(l8, lc, l8, lc, faction)) {
                if (!(veh & 1)) V->state &= 0x7fffffffu;
                else V->state |= 0x80000000u;
            }
            l34 = 0;
            forced = 0;
            for (l50 = 3;;) {
                int k = V->state & 0x80000000u ? forced - 3 : l50;
                int dd = ((int8_t)V->unk_29 + 8 + k) % 8;
                l8 = xrange(game_radius_near_x[dd] + x), lc = game_radius_near_y[dd] + y;
                if (!l34) {
                    if (on_map(l8, lc)) {
                        if (!(tile(l8, lc)->region == region && whose_territory(faction, l8, lc, NULL, 0) == faction))
                            l34 = 1;
                    }
                } else if (on_map(l8, lc) && tile(l8, lc)->region == region &&
                           whose_territory(faction, l8, lc, NULL, 0) == faction) {
                    V->status_icon = 'p';
                    dir84 = dd;
                    goto at_578b26;
                }
                forced++;
                if (--l50 < -4) break;
            }
        }
        l2c = 0;
        for (lc = 0; lc < *game_map_height; lc++) {
            for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                if (tile(l8, lc)->region != region || whose_territory(faction, l8, lc, NULL, 0) != faction) continue;
                l24 = 1;
                for (l5c = 0; l5c < 0x24; l5c += 4) {
                    int j = l5c / 4;
                    int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                    if (!on_map(nx, ny)) continue;
                    if (tile(nx, ny)->region == region) {
                        if (whose_territory(faction, nx, ny, NULL, 0) != faction) l24 += 3;
                        if (anything_at(nx, ny) == faction) l24 -= 6;
                    } else {
                        l24++;
                    }
                }
                int v = ((abs(y - lc) + x_dist(x, l8)) >> 1) * l24;
                if (*game_veh_count > 0) {
                    for (int u = 0; u < *game_veh_count; u++)
                        if (game_vehs[u].faction == faction && game_vehs[u].waypoint_x[0] == l8 &&
                            game_vehs[u].waypoint_y[0] == lc)
                            v /= 2;
                    l50 = 0, l20 = *game_veh_count;
                }
                if (v <= l2c) continue;
                l2c = v;
                go_to(veh, 'P', l8, lc);
            }
        }
    }
    if (!exploring && !(plan == PLAN_RECONNAISSANCE && *game_current_turn < 100)) goto at_574a7e;
at_57460a:
    // Exploring: the unknown squares worth the trip, ring by ring.
    if (triad != TRIAD_AIR && (V->order != ORDER_MOVE_TO || V->status_icon != '/')) {
        l50 = 0, l2c = 0, terr = 0;
        for (l58 = 0; l58 < 9; l58++) {
            l48 = 0;
            for (l30 = l58 ? game_radius_count[l58 - 1] : 0; l30 < game_radius_count[l58]; l30++) {
                l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
                if (!on_map(l8, lc) || tile(l8, lc)->region != region) continue;
                l74 = whose_territory(faction, l8, lc, NULL, 0);
                if (!(l74 < 0 || l74 == faction || !(F->diplo_status[l74] & 7))) continue;
                unsigned v = is_known(l8, lc, faction) ? (goody_at(l8, lc) ? 0x200 : 0) : 0;
                for (l34 = 0; l34 < 9; l34++) {
                    int nx = xrange(game_radius_x[l30] + l8), ny = game_radius_y[l30] + lc;
                    if (!on_map(nx, ny)) continue;
                    if (!is_known(nx, ny, faction)) {
                        v += tile(nx, ny)->region == region ? 0x10 : 1;
                    } else {
                        l74 = veh_who(nx, ny);
                        if (l74 >= 0 && l74 != faction) {
                            v = 0;
                            break;
                        }
                    }
                }
                const struct map_tile *t = tile(l8, lc);
                if ((t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40 && F->se_pending[7] < 1) v = (int)v / 2;
                alt = (int)v;
                if (own_base >= 0) {
                    l5c = abs(lc - game_bases[own_base].y);
                    int d = 0x10 - ((l5c + x_dist(l8, game_bases[own_base].x)) >> 1);
                    v += d < 0 ? 0 : d;
                }
                l5c = abs(y - lc);
                l24 = (int)(v << 8) / (((l5c + x_dist(x, l8)) >> 1) + 2);
                if (l48 < l24) l48 = l24;
                if (l2c >= l24) continue;
                int taken = 0;
                for (l20 = 0; l20 < *game_veh_count; l20++) {
                    const struct veh *U = &game_vehs[l20];
                    if (U->faction != faction || l20 == veh || U->order != ORDER_MOVE_TO) continue;
                    l3c = (abs(lc - U->waypoint_y[0]) + x_dist(l8, U->waypoint_x[0])) >> 1;
                    if (l3c < 2) {
                        int mine = (abs(y - lc) + x_dist(x, l8)) >> 1;
                        l5c = abs(U->y - lc);
                        if (((l5c + x_dist(U->x, l8)) >> 1) - 1 <= mine) {
                            taken = 1;
                            break;
                        }
                    }
                }
                if (taken) continue;
                if (l24) {
                    l50 = alt;
                    l2c = l24;
                    go_to(veh, '/', l8, lc);
                }
            }
            if (V->order == ORDER_MOVE_TO && l48 < terr && l50 > 0xf) break;
            terr = l48;
        }
        if (V->order == ORDER_MOVE_TO) goto at_57707f;
    }
at_574a7e:
    if (triad != TRIAD_SEA) {
        if (lbc) return 0;
        goto at_57707f;
    }
at_574a88:
    if (plan == PLAN_NAVAL_SUPERIORITY) del_site(faction, 6, x, y, 0);
    forced = 0, l48 = 0, l30 = 0, l58 = 0, l34 = 0;
    if (plan == PLAN_NAVAL_TRANSPORT) {
        // ---- Naval transports (0x574ac4) ----
        // Its passengers: the land units on its square of its faction,
        // not carrying others, nor besieging (state 0x40000) unless a
        // base is near or (0x60000) the region's plan is 7. In port
        // (own_dist 0, where the stack is sorted first), units on hold
        // (order 2) come aboard when fit (damage up to a third of their
        // power), but artifacts only at sea; of those up to plan 8,
        // defenders, garrisons (state 0x10000000), combat or recon units
        // with no defender in their stack, and units of a stack without
        // defenders, combat or recon units (stack_check 16) are kept:
        // the first [ebp-0x48] of them (a word the original may not have
        // set on this path, as here) stay, and past those, where the
        // region's plan is 2 or less, only one comes aboard. At sea,
        // those sentried for it (order 1, waypoint the transport or none)
        // or idle with a base near count, the unfit apart (l58).
        // Otherwise artifacts hold, formers
        // are left, defenders up to [ebp-0x48] hold in port, the unfit
        // hold in port, and the rest come aboard. l30 collects the fit
        // passengers' plans as bits (combat units as plans 0-2), `forced`
        // counts them all.
        if (!own_dist) stack_sort_2(veh);
        l5c = 0;
        for (l20 = veh_top(veh); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
            struct veh *U = &game_vehs[l20];
            if (l20 == veh || triad_of(U->proto) != TRIAD_LAND) continue;
            if (veh_cargo(l20) || U->faction != faction) continue;
            if ((U->state & 0x40000) && !near_dist && !((U->state & 0x20000) && region_plan == 7)) continue;
            int fit = U->damage <= proto_power(l20) / 3;
            if (U->order == 2) {
                int up = proto_of(U->proto)->plan;
                if (own_dist || (up == PLAN_ALIEN_ARTIFACT && !at_sea) || !fit) continue;
                if (up <= 8) {
                    if (!(up == PLAN_DEFENSIVE || (U->state & 0x10000000) ||
                          (!stack_check(l20, 2, 2, -1, -1) && (proto_of(U->proto)->plan == 1 || proto_of(U->proto)->plan == 3)) ||
                          !stack_check(l20, 16, -1, -1, -1)))
                        goto tr_take;
                    if (++l48 <= l4c) continue;
                    if (region_plan <= 2 && l5c) continue;
                    l5c++;
                }
            tr_take:
                forced++;
                U->order = ORDER_NONE;
                up = proto_of(U->proto)->plan;
                if (up == 1) l30 |= 7;
                else l30 |= 1 << up;
                continue;
            }
            if (at_sea) {
                if ((U->order == 1 && (U->waypoint_x[0] < 0 || U->waypoint_x[0] == veh)) || (!U->order && near_dist)) {
                    forced++;
                    if (fit) {
                        int up = proto_of(U->proto)->plan;
                        if (up == 1) l30 |= 7;
                        else l30 |= 1 << up;
                    } else {
                        l58++;
                    }
                    continue;
                }
            } else if (proto_of(U->proto)->plan == PLAN_ALIEN_ARTIFACT) {
                U->order = 2;
                continue;
            }
            int up = proto_of(U->proto)->plan;
            if (up == PLAN_TERRAFORMING) continue;
            if (up == PLAN_DEFENSIVE && l48 < l4c) {
                if (!own_dist) {
                    l48++;
                    U->order = 2;
                    continue;
                }
            } else if (!own_dist && !fit) {
                U->order = 2;
                continue;
            }
            forced++;
            U->order = ORDER_NONE;
            if (fit) {
                up = proto_of(U->proto)->plan;
                if (up == 1) l30 |= 7;
                else l30 |= 1 << up;
            }
        }
        // Empty, it drops a course for a port or an invasion.
        if (!forced && V->order == ORDER_MOVE_TO &&
            (V->status_icon == 'J' || V->status_icon == 'r' || V->status_icon == 'I' || V->status_icon == 'E'))
            V->order = ORDER_NONE;
        // l34: 2 with combat units (or 0x800, plan 11) aboard, 1 with only
        // colony pods and no combat units.
        if ((l30 & 0xf) || (l30 & 0x800)) {
            l34 = 2;
            if ((l30 & 0x100) && !(l30 & 3)) l34 = 1;
        }
        if ((V->status_icon == 'E' || V->status_icon == 'r' || V->status_icon == 'I') &&
            (!l34 || V->order != ORDER_MOVE_TO))
            V->status_icon = '-';
        if (l90 || (V->order == ORDER_MOVE_TO && V->status_icon == 'd')) goto at_57543f;
        if (((l30 & 0x1000) && near_dist) ||
            (l58 && l58 >= forced && (near_dist <= 8 || *game_current_turn < 50))) {
            // Supply crawlers aboard, or mostly the hurt: to the nearest
            // port of its own on this sea (a base with state 0x400 a
            // thousand further), or stay in the one it is in.
            l2c = 9999;
            for (l28 = 0; l28 < *game_base_count; l28++) {
                const struct base *B = &game_bases[l28];
                if (B->faction != faction || !base_on_sea(l28, region)) continue;
                l3c = vector_dist_offset(x_dist(x, B->x), abs(y - B->y));
                if (!l3c) goto at_578b34;
                if (B->state & 0x400) l3c += 1000;
                if (l3c < l2c) {
                    l2c = l3c;
                    go_to(veh, 'a', B->x, B->y);
                }
            }
        }
        {
            // Room left: unless full (or held at 0x20000000 with nobody
            // aboard: none), it gathers passengers if empty, or (but in
            // port) when half empty or more, without colony pods.
            int room = veh_cargo(veh) - forced;
            if ((V->state & 0x20000000) && !forced) room = 0;
            if (room != veh_cargo(veh) && (own_dist || room < veh_cargo(veh) / 2 || (l30 & 0x100))) goto at_57543f;
        }
        // A port of its own to gather an invasion at (go_to 'e'): one on
        // the faction's target's continent when it has flag 0x800 (the
        // target itself, with fewer than two transports there), else ports
        // on continents whose plan isn't 2, with combat units to spare
        // (or region flags 3 or 0x1800), not planned for already (ai_plan)
        // unless bound there or the port it is in; reachable by sea. The
        // nearer the better, doubled unless the region's plan is 7, flag
        // 0x800 or its own course (then only every eighth turn), doubled
        // with other transports there; less the spare units.
        l2c = 9999, terr = 0;
        for (l28 = 0; l28 < *game_base_count; l28++) {
            const struct base *B = &game_bases[l28];
            if (B->faction != faction || !is_port(l28, 0)) continue;
            l6c = tile(B->x, B->y)->region;
            if (F->region_base_plan[l6c] == 2) continue;
            l50 = abs(y - B->y);
            l48 = (l50 + x_dist(x, B->x)) >> 1;
            l3c = l48;
            int at_wp = V->waypoint_x[0] == B->x && V->waypoint_y[0] == B->y;
            if (F->target_x >= 0 && l6c == tile(F->target_x, F->target_y)->region) {
                l50 = 1;
                if (!(F->flags & 0x800) || B->x != F->target_x || B->y != F->target_y) continue;
                if (stack_check(veh_at(B->x, B->y), 2, 7, -1, -1) >= (!l48 || at_wp ? 2 : 1)) continue;
            } else {
                if (B->ai_plan && !(V->order == ORDER_MOVE_TO && at_wp) &&
                    !(!own_dist && own_base == l28 && stack_check(veh, 2, 7, -1, -1) <= 1))
                    continue;
                at_wp = V->waypoint_x[0] == B->x && V->waypoint_y[0] == B->y;
                if (stack_check(veh_at(B->x, B->y), 2, 7, -1, -1) >= (!l48 || at_wp ? 2 : 1)) continue;
                l50 = 0;
            }
            if (!(own_dist ? base_on_sea(l28, region) : port_to_port(own_base, l28))) continue;
            if (!(F->region_total_combat_units[l6c] > F->region_total_bases[l6c] * 2 + 1 ||
                  (F->region_flags[l6c] & 3) || (F->region_flags[l6c] & 0x1800)))
                continue;
            int s = l48 + 4;
            l3c = s;
            if (F->region_base_plan[l6c] != 7 && !(F->region_flags[l6c] & 0x800) &&
                !(V->waypoint_x[0] == B->x && V->waypoint_y[0] == B->y)) {
                if ((*game_current_turn + l28 + veh) & 7) continue;
                s *= 2;
            }
            if (stack_check(veh_at(B->x, B->y), 2, 7, faction, -1) > !l48) s *= 2;
            s += F->region_total_bases[l6c] * 2 - F->region_total_combat_units[l6c];
            l3c = s;
            if (s < l2c) {
                l7c = l28, l2c = s, l5c = F->region_base_plan[l6c], terr = l50;
                go_to(veh, 'e', B->x, B->y);
            }
        }
        {
            // Passengers called to it (assemble_passengers), 6 at most
            // for the target or a region planned 7, else 2; when for the
            // target, or near enough. l7c may be a word the original
            // never set, when no port was found.
            int prio = terr || l5c == 7 ? 6 : 2;
            prio = game_range(prio, 0, veh_cargo(veh));
            if (terr || l2c <= veh_moves(veh) * prio / 2) assemble_passengers(l7c, l5c, prio);
        }
    at_57543f:
        // Arrived where it gathers: the base nearest it (which may be
        // none: the original then marks the word before the bases) is
        // marked.
        if (V->status_icon == 'e' && V->waypoint_x[0] == x && V->waypoint_y[0] == y)
            game_bases[near_base].state |= 1;
        if (forced > l58 && V->status_icon != 'E') {
            // Unloading: a land square next to it, of a continent (not
            // the pole regions), in its own or open land or an enemy's to
            // invade, not held by another's units; worth it when the
            // continent's plan isn't one its passengers serve, or for the
            // hurt (l58), or to found bases on an empty continent.
            for (l5c = 0; l5c < 0x20; l5c += 4) {
                int i = l5c / 4;
                l8 = xrange(game_radius_near_x[i] + x), lc = game_radius_near_y[i] + y;
                if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE) continue;
                l6c = tile(l8, lc)->region;
                if ((l6c & 0x3f) == 0x3f || !(l6c & 0x3f)) continue;
                l58 = hurt > 2;
                l74 = whose_territory(faction, l8, lc, NULL, 0);
                if (l74 >= 0 && l74 != faction) {
                    uint32_t st = F->diplo_status[l74];
                    if (st & 1) continue;
                    if (st & 0xc10) {
                        if (l34) l58 = 1;
                        goto tr_c65d;
                    }
                    if (st & 2) {
                        if (!(*game_human_factions & (1 << l74)) || !l34 ||
                            *game_current_turn - game_factions[l74].diplo_spoke[faction] < 16)
                            continue;
                        diplomacy_check(l74, faction, 0);
                        if (!*game_diplo_hostility && !*game_diplo_great_satan) continue;
                    } else if (st & 4) {
                        if (!(*game_human_factions & (1 << l74)) ||
                            *game_current_turn - game_factions[l74].diplo_spoke[faction] < 16)
                            continue;
                    }
                    if (!(F->diplo_status[l74] & 8) && l34 && !(l30 & 0x100)) l58 = 1;
                }
            tr_c65d:
                l74 = veh_who(l8, lc);
                if (l74 >= 0 && l74 != faction) continue;
                if ((l30 & 0x100) && game_continents[l6c].open_terrain >= 4 &&
                    (!F->region_total_bases[l6c] || F->region_base_plan[l6c] == 8)) {
                    if (!l34 && F->region_base_plan[region] <= 2) goto tr_c77c;
                    l58 = 1;
                }
                if (!l34) goto tr_c77c;
                if (!l58 && !F->region_total_bases[l6c] && F->region_base_plan[l6c] == 8) {
                    for (l74 = 1; l74 < 8; l74++)
                        if (game_factions[l74].region_total_bases[l6c] &&
                            ((F->diplo_status[l74] & 0x10) || !(F->diplo_status[l74] & 8) ||
                             *game_current_turn - game_factions[l74].diplo_spoke[faction] > 16))
                            l58 = 1;
                }
                if (F->region_base_plan[l6c] <= 2) {
                    l58 = 1;
                    goto tr_c787;
                }
            tr_c77c:
                if (!l58) continue;
            tr_c787:;
                int pick;
                if (!(l30 & (1 << F->region_base_plan[l6c])) && hurt <= 2 && !(*game_state & 0x100) &&
                    (V->order != ORDER_MOVE_TO || l6c != tile(V->waypoint_x[0], V->waypoint_y[0])->region ||
                     V->status_icon == 'e')) {
                    // A passenger sentried for it, from that continent:
                    // only into land it may enter.
                    for (l20 = veh_top(veh); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
                        const struct veh *U = &game_vehs[l20];
                        if (triad_of(U->proto) == TRIAD_LAND && U->order == 1 &&
                            (U->waypoint_x[0] < 0 || U->waypoint_x[0] == veh) && U->home_base >= 0 &&
                            tile(game_bases[U->home_base].x, game_bases[U->home_base].y)->region == l6c)
                            break;
                    }
                    if (l20 >= 0) {
                        l74 = zoc_veh(l8, lc, faction);
                        if (l74 <= 1) continue;
                        uint32_t st = F->diplo_status[l74 - 1];
                        if (!((st & 0x10) && l34 && (l30 & 3)) && (st & 8)) continue;
                        if (!l58) continue;
                        pick = -1;
                        goto tr_c949;
                    }
                }
                if (!l58) continue;
                pick = l58;
            tr_c949:
                // Unloaded: next to the square, among the nine round it on
                // its own sea (open or its own), the one with most of the
                // continent's squares round it (bases of factions counted
                // five times), with moves left and a base near; its land
                // units' orders cleared.
                dir84 = -1;
                if (veh_moves(veh) >= game_rules_basic->move_rate_roads && near_dist && pick > 0) {
                    l2c = -1;
                    for (l34 = 0; l34 < 9; l34++) {
                        l8 = xrange(game_radius_x[l34] + x), lc = game_radius_y[l34] + y;
                        if (!on_map(l8, lc) || tile(l8, lc)->region != region) continue;
                        if (veh_who(l8, lc) >= 0 && veh_who(l8, lc) != faction) continue;
                        l24 = 0;
                        for (int k = 1; k <= 8; k++) {
                            int nx = xrange(game_radius_x[k] + l8), ny = game_radius_y[k] + lc;
                            if (!on_map(nx, ny) || tile(nx, ny)->region != l6c) continue;
                            if (base_who(nx, ny) > 0) l24 += 4;
                            l24++;
                        }
                        if (l24 > l2c) l2c = l24, dir84 = l34 - 1;
                    }
                }
                for (l20 = veh_top(veh); l20 >= 0; l20 = game_vehs[l20].next_in_stack)
                    if (triad_of(game_vehs[l20].proto) == TRIAD_LAND) game_vehs[l20].order = ORDER_NONE;
                V->status_icon = 'u';
                if (lbc) return 0;
                goto at_578b26;
            }
        }
        if (lbc) return 0;
        if (l90) goto at_57717f;
        if (V->order == ORDER_MOVE_TO) {
            if (V->status_icon == 'd') goto at_57717f;
        } else if ((l30 & 0x100) && l34 < 1) {
            // Colony pods only: the empty continent of ten squares or
            // more, with few others' bases, that is biggest for its
            // distance, at a coast square between rows 10 and height - 10
            // where it has one (the last such found), else its last.
            int32_t cy[64], cx[64];
            for (int i = 0; i < 64; i++) cy[i] = -1;
            for (int i = 0; i < 64; i++) cx[i] = -1;
            for (lc = 1; lc < *game_map_height - 1; lc++) {
                for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                    int r = tile(l8, lc)->region;
                    if (r >= 0x40) continue;
                    if ((cy[r] < 10 || cy[r] > *game_map_height - 10) && is_coast(l8, lc, 0)) cx[r] = l8, cy[r] = lc;
                }
            }
            l7c = -1, l2c = 0;
            for (l6c = 1, l5c = 0; l5c < 0xfc; l6c++, l5c += 4) {
                const struct continent *C = &game_continents[l6c];
                if (C->tile_count < 10 || F->region_total_bases[l6c] || !on_map(cx[l6c], cy[l6c])) continue;
                int k = 0;
                for (l74 = 1; l74 < 8; l74++) {
                    if (!game_factions[l74].region_total_bases[l6c]) continue;
                    k++;
                    if (game_factions[l74].region_total_bases[l6c] * 20 > C->tile_count / 4) k += 2;
                    if (F->diplo_status[l74] & 7) k += 2;
                }
                if (k > 1) continue;
                int v = C->tile_count * 100 / (vector_dist_offset(x_dist(x, cx[l6c]), abs(y - cy[l6c])) + 1);
                if (v >= l2c) l2c = v, l7c = l6c;
            }
            // The original walks these with a pointer in [ebp-0x44].
            l48 = (int)(intptr_t)&game_continents[64];
            if (l7c >= 0) set_course(veh, 'c', cx[l7c], cy[l7c]);
        }
        if (V->order != ORDER_MOVE_TO && l34) {
            // Combat units aboard: its own base on a continent planned 2
            // (defence) it can reach, the nearer the better, halved when
            // empty and when a goal of type 2 is there, first when its
            // faction's attack target is on that continent (go_to 'E').
            l2c = 9999;
            for (l28 = 0; l28 < *game_base_count; l28++) {
                const struct base *B = &game_bases[l28];
                if (B->faction != faction) continue;
                l6c = tile(B->x, B->y)->region;
                if (F->region_base_plan[l6c] != 2) continue;
                if (!(own_dist ? base_on_sea(l28, region) : port_to_port(own_base, l28))) continue;
                l50 = x;
                int d = (abs(y - B->y) + x_dist(x, B->x)) >> 1;
                l3c = d;
                if (veh_who(B->x, B->y) < 0) l3c = d = d / 2;
                if (at_goal(faction, 2, B->x, B->y)) l3c = d = d / 2;
                int t = F->attack_target_base;
                if (t >= 0 && (F->diplo_status[game_bases[t].faction] & 0x10) &&
                    tile(game_bases[t].x, game_bases[t].y)->region == l6c)
                    l3c = d = 0;
                if (d < l2c) {
                    l2c = d;
                    go_to(veh, 'E', B->x, B->y);
                }
            }
            if (V->order != ORDER_MOVE_TO && l34 > 1) {
                // An invasion (set_course 'I'): an enemy base across the
                // sea, objectives, its attack target, or (every fourth
                // turn by base) any, of a faction it's at war with (or
                // a human under a treaty or truce it may break), on a
                // continent its passengers aren't from unless at war and
                // it isn't wanted; the nearer the better (doubled when
                // unseen), the Ascent, headquarters (more for flag
                // 0x200000 and a cornered market), objectives and those
                // marked 0x40 or vendetta (empty ones more) first, the
                // attack target at once, and weak garrisons of a human of
                // high difficulty or one it hates.
                l2c = 9999;
                for (l28 = 0; l28 < *game_base_count; l28++) {
                    const struct base *B = &game_bases[l28];
                    if (B->faction == faction) continue;
                    if (!(is_objective(l28) || (F->flags & 0x200000) || F->attack_target_base == l28 ||
                          !((*game_current_turn + l28) & 3)))
                        continue;
                    int bf = B->faction;
                    l34 = faction * 0x833;
                    uint32_t st = F->diplo_status[bf];
                    if (st & 1) continue;
                    if ((st & 2) && (!(*game_human_factions & (1 << bf)) || !(st & 0xc00))) continue;
                    if ((st & 4) && (!(*game_human_factions & (1 << bf)) ||
                                     (!(st & 0xc00) && *game_current_turn - game_factions[bf].diplo_spoke[faction] < 16)))
                        continue;
                    int r = tile(B->x, B->y)->region;
                    if (r == region && !at_sea) continue;
                    if (!(own_dist ? sea_coast(r, region) : port_to_coast(own_base, r))) continue;
                    l58 = 1;
                    for (l20 = veh_top(veh); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
                        int h = game_vehs[l20].home_base;
                        if (h >= 0 && tile(game_bases[h].x, game_bases[h].y)->region == r) l58 = 0;
                    }
                    if (!l58 && !(F->diplo_status[bf] & 0x10) && (F->diplo_status[bf] & 8) && !(*game_state & 0x100) &&
                        !(l30 & (1 << F->region_base_plan[r])))
                        continue;
                    int d = ((abs(y - B->y) + x_dist(x, B->x)) >> 1) << 5;
                    if (B->faction != faction && !(B->visibility & (1 << faction))) d *= 2;
                    if (B->queue[0] == BUILD_ASCENT) d /= 4;
                    if (has_fac(1, l28, 0)) {
                        d /= 2;
                        if (F->flags & 0x200000) d /= 2;
                        if (game_factions[bf].corner_market_turn > *game_current_turn) d /= 8;
                    }
                    if (is_objective(l28)) {
                        d /= 2;
                        if (F->flags & 0x80000000) d /= 2;
                    }
                    st = F->diplo_status[bf];
                    if (st & 0x40) d /= 2;
                    if (st & 0x10) {
                        d /= 2;
                        if (veh_who(B->x, B->y) < 0) d /= 2;
                        if (F->attack_target_base == l28) d = 0;
                    }
                    if (has_fac(1, l28, 0)) d /= 2;
                    l20 = veh_at(B->x, B->y);
                    if (l20 < 0 || (l50 = stack_check(l20, 14, -1, -1, -1)) < garrison_check(l28)) {
                        if ((*game_human_factions & (1 << bf)) &&
                            (game_factions[bf].diff_level >= 4 || (F->flags & 0x200) || (F->diplo_status[bf] & 0x1000)))
                            d /= 2;
                    }
                    l3c = d;
                    if (d < l2c) {
                        l2c = d;
                        set_course(veh, 'I', B->x, B->y);
                    }
                }
                // The original walks the bases with a pointer in [ebp-0x48].
                if (*game_base_count > 0) l4c = (int)(intptr_t)&game_bases[*game_base_count].faction;
            }
            if (V->order != ORDER_MOVE_TO) {
                // Elsewhere: its own port on another continent, of a plan
                // (not 7) its passengers serve, without colony pods aboard
                // (go_to 'r').
                l2c = 9999;
                for (l28 = 0; l28 < *game_base_count; l28++) {
                    const struct base *B = &game_bases[l28];
                    if (B->faction != faction) continue;
                    int r = tile(B->x, B->y)->region;
                    if (region == r || F->region_base_plan[r] == 7 || (l30 & 0x100) ||
                        !(l30 & (1 << F->region_base_plan[r])))
                        continue;
                    if (!(own_dist ? base_on_sea(l28, region) : port_to_port(own_base, l28))) continue;
                    l50 = abs(y - B->y);
                    l3c = (l50 + x_dist(x, B->x)) >> 1;
                    if (l3c < l2c) {
                        l2c = l3c;
                        go_to(veh, 'r', B->x, B->y);
                    }
                }
            }
            if (V->order != ORDER_MOVE_TO) {
                // Or the first empty continent two squares out (in the
                // radius table's order) on its sea, when its passengers
                // serve the plan of the region it is in (set_course
                // 'J').
                for (int k = 1; k < RADIUS_SQUARES; k++) {
                    l8 = xrange(x + game_radius_x[k] * 2), lc = y + game_radius_y[k] * 2;
                    if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE) continue;
                    int r = tile(l8, lc)->region;
                    if ((r & 0x3f) == 0x3f || !(r & 0x3f) || F->region_total_bases[r] ||
                        !(l30 & (1 << F->region_base_plan[region])))
                        continue;
                    if (own_dist ? sea_coast(r, region) : port_to_coast(own_base, r)) {
                        set_course(veh, 'J', l8, lc);
                        break;
                    }
                }
            }
        }
        goto at_57666a;
    }
    if (l90) goto at_577071;
at_57666a:
    if (V->order == ORDER_MOVE_TO && V->status_icon == 'd') goto at_577071;
    if (plan == PLAN_NAVAL_SUPERIORITY) {
        if (proto_of(V->proto)->group == GROUP_NO_GOALS) {
            // An escort: to the nearest transport at sea, the fewer escorts
            // with it the better.
            l2c = 9999;
            for (l20 = 0; l20 < *game_veh_count; l20++) {
                const struct veh *U = &game_vehs[l20];
                if (U->faction != faction || proto_of(U->proto)->plan != PLAN_NAVAL_TRANSPORT) continue;
                lc = U->y, l8 = U->x;
                if (alt_of(tile(l8, lc)) >= ALT_SHORE || base_who(l8, lc) >= 0) continue;
                int d = (abs(y - lc) + x_dist(x, l8)) >> 1;
                l3c = d;
                if (d) {
                    if (stack_check(l20, 2, 6, -1, -1)) continue;
                    l3c = d / stack_check(l20, 1, -1, -1, -1);
                }
                if (l3c < l2c) {
                    l2c = l3c;
                    go_to(veh, 'C', l8, lc);
                }
            }
            if (V->order == ORDER_MOVE_TO) {
                if (!l3c) goto at_578b34;
                goto at_577071;
            }
            goto at_576847;
        }
        if (V->order != ORDER_MOVE_TO || V->status_icon == '6') {
        at_576847:
            if (game_faction_defs[faction].rule_flags & 0x100) {
                // Raiding: the biggest enemy port in reach, fewer warships
                // of its own about.
                l2c = 0, l7c = -1;
                for (l28 = 0; l28 < *game_base_count; l28++) {
                    const struct base *B = &game_bases[l28];
                    if (B->faction == faction || !(F->diplo_status[B->faction] & 0x10)) continue;
                    if (!(own_dist ? base_on_sea(own_base, region) : port_to_port(own_base, l28))) continue;
                    l50 = B->x;
                    l24 = B->pop_size * B->pop_size * 0x20 / (((abs(y - B->y) + x_dist(x, l50)) >> 1) + 1);
                    for (l20 = 0; l20 < *game_veh_count; l20++) {
                        const struct veh *U = &game_vehs[l20];
                        if (l20 == veh || U->faction != faction || proto_of(U->proto)->plan != PLAN_NAVAL_SUPERIORITY)
                            continue;
                        l50 = abs(U->y - B->y);
                        if (((l50 + x_dist(U->x, B->x)) & ~1) < 5 ||
                            (U->status_icon == '6' && U->waypoint_x[0] == B->x && U->waypoint_y[0] == B->y))
                            l24 /= 2;
                    }
                    if (l2c < l24) {
                        l7c = l28, l2c = l24;
                        go_to(veh, '6', B->x, B->y);
                    }
                }
                if (l7c >= 0 &&
                    ((abs(y - game_bases[l7c].y) + x_dist(x, game_bases[l7c].x)) & ~1) < 3) {
                    V->order = ORDER_NONE;
                    goto at_577071;
                }
            }
        }
        if (V->order != ORDER_MOVE_TO && !((*game_current_turn + veh) & 3)) {
            // A strait between continents the enemy wants to cross.
            l2c = -9999;
            for (lc = 0; lc < *game_map_height; lc++) {
                for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                    const struct map_tile *t = tile(l8, lc);
                    if (alt_of(t) >= ALT_SHORE) continue;
                    if (!own_dist ? !base_on_sea(own_base, t->region) : t->region != region) continue;
                    if (((abs(y - lc) + x_dist(x, l8)) & ~1) <= 0xf) continue;
                    for (l20 = 0; l20 < *game_veh_count; l20++)
                        if (game_vehs[l20].faction == faction && l20 != veh && game_vehs[l20].waypoint_x[0] == l8 &&
                            game_vehs[l20].waypoint_y[0] == lc)
                            break;
                    if (l20 < *game_veh_count) continue;
                    l24 = 0;
                    for (l48 = 0; l48 < 0x120; l48 += 4) {
                        int j = l48 / 4 + 9;
                        int nx = xrange(game_radius_x[j] + l8), ny = game_radius_y[j] + lc;
                        if (!on_map(nx, ny) || alt_of(tile(nx, ny)) < ALT_SHORE) continue;
                        int mx = xrange(l8 - game_radius_x[j]);
                        l50 = lc - game_radius_y[j];
                        if (!on_map(mx, l50) || alt_of(tile(mx, l50)) < ALT_SHORE) continue;
                        int rn = tile(nx, ny)->region, rm = tile(mx, l50)->region;
                        if (F->region_total_bases[rn] <= 1 || F->region_total_bases[rm] >= 2) continue;
                        for (l74 = 1; l74 < 8; l74++)
                            if (l74 != faction && (F->diplo_status[l74] & 0xc10) &&
                                game_factions[l74].region_total_bases[rn] < 2 && game_factions[l74].region_total_bases[rm] > 1)
                                l24++;
                    }
                    if (l24) {
                        int v = (l24 << 8) / vector_dist_offset(x_dist(x, l8), abs(y - lc));
                        if (l2c < v) {
                            l2c = v;
                            go_to(veh, 'D', l8, lc);
                        }
                    }
                }
            }
        }
    }
    if ((V->order != ORDER_MOVE_TO || ((F->flags & 0x20000000) && stack_check(veh, 1, -1, -1, -1) == 1)) &&
        !((*game_current_turn + veh) & 3)) {
        // Seas to explore.
        l2c = -9999;
        for (lc = 0; lc < *game_map_height; lc++) {
            for (l8 = lc & 1; l8 < *game_map_width; l8 += 2) {
                if (!is_known(l8, lc, faction)) continue;
                const struct map_tile *t = tile(l8, lc);
                if (alt_of(t) >= ALT_SHORE) continue;
                if (!own_dist ? !base_on_sea(own_base, t->region) : t->region != region) continue;
                l24 = 0;
                if (F->flags & 0x20000000) {
                    uint32_t bits = t->bits;
                    if (!(bits & 0x100000)) {
                        l24 = 1;
                        if ((bits & MAP_FUNGUS) && alt_of(t) >= 0x40) l24 = 2;
                        if (bits & 0x2000) l24++;
                    }
                }
                for (int j = 0; j < 8; j++) {
                    int nx = xrange(game_radius_near_x[j] + l8), ny = game_radius_near_y[j] + lc;
                    if (!on_map(nx, ny)) continue;
                    if (!(F->flags & 0x20000000) || (tile(nx, ny)->bits & 0x100000)) {
                        if (is_known(nx, ny, faction)) continue;
                    }
                    l24 += 1 < ny && ny < *game_map_height - 3 ? 6 : 2;
                }
                if (!l24) continue;
                int d = own_base < 0 ? vector_dist_offset(x_dist(x, l8), abs(y - lc))
                                     : vector_dist_offset(x_dist(game_bases[own_base].x, l8),
                                                          abs(game_bases[own_base].y - lc));
                if (d) {
                    int v = (l24 << 8) / d;
                    if (l2c < v) {
                        l2c = v;
                        go_to(veh, 'X', l8, lc);
                    }
                }
            }
        }
    }
at_577071:
    if (lbc) return 0;
    goto at_57707f;
at_after_72922:
    l50 = abs(y - V->waypoint_y[0]);
    if (((l50 + x_dist(x, V->waypoint_x[0])) & ~1) != 4) goto at_572922;
    l28 = base_at(V->waypoint_x[0], V->waypoint_y[0]);
    if (l28 < 0) goto at_572922;
    {
        // An attack on an enemy base two squares off, with the faction's
        // other units round it.
        struct base *B = &game_bases[l28];
        forced = l28 * (int)sizeof(struct base);
        l34 = faction * 0x833;
        if (!(F->diplo_status[B->faction] & 0x10)) goto at_572922;
        B->event |= (uint32_t)l6c;
        l30 = 0, l58 = 0, l48 = 0;
        for (int i = 1; i < 9; i++) {
            l8 = xrange(game_radius_x[i] + B->x), lc = game_radius_y[i] + B->y;
            if (!on_map(l8, lc) || (l20 = stack_fix(veh_at(l8, lc))) < 0) continue;
            for (l20 = veh_top(l20); l20 >= 0; l20 = game_vehs[l20].next_in_stack)
                if (game_vehs[l20].faction == faction && proto_of(game_vehs[l20].proto)->plan <= 1 &&
                    !has_abil(game_vehs[l20].proto, ABL_ARTILLERY))
                    l30++;
        }
        for (int i = 9; i < 25; i++) {
            l8 = xrange(game_radius_x[i] + B->x), lc = game_radius_y[i] + B->y;
            if (!on_map(l8, lc) || (l20 = stack_fix(veh_at(l8, lc))) < 0) continue;
            for (l20 = veh_top(l20); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
                const struct veh *U = &game_vehs[l20];
                if (U->faction == faction && proto_of(U->proto)->plan <= 1 && !has_abil(U->proto, ABL_ARTILLERY) &&
                    ((U->status_icon > 0x32 && U->status_icon < 0x36) ||
                     (U->waypoint_x[0] == V->waypoint_x[0] && U->waypoint_y[0] == V->waypoint_y[0]))) {
                    if (!U->moves_spent) l58++;
                    else l48++;
                }
            }
        }
        for (int i = 25; i < 25 + 0x90; i++) {
            l8 = xrange(game_radius_x[i] + B->x), lc = game_radius_y[i] + B->y;
            if (!on_map(l8, lc) || (l20 = stack_fix(veh_at(l8, lc))) < 0) continue;
            for (l20 = veh_top(l20); l20 >= 0; l20 = game_vehs[l20].next_in_stack) {
                const struct veh *U = &game_vehs[l20];
                if (U->faction == faction && proto_of(U->proto)->plan < 2 && !has_abil(U->proto, ABL_ARTILLERY) &&
                    ((U->status_icon > 0x32 && V->status_icon < 0x36) ||
                     ((base_who(l8, lc) < 0 || (U->state & 0x10000)) && U->waypoint_x[0] == V->waypoint_x[0] &&
                      U->waypoint_y[0] == V->waypoint_y[0])))
                    l48++;
            }
        }
        if (l58 + l30 > 7 || l48 <= l30) {
            // Enough of them: the best square next to the base to attack from.
            if (!zoc_move(x, y, faction)) {
                l2c = -9999;
                for (l30 = 0; l30 < 8; l30++) {
                    l8 = xrange(game_radius_near_x[l30] + x), lc = game_radius_near_y[l30] + y;
                    if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE) continue;
                    if (((abs(lc - B->y) + x_dist(l8, B->x)) & ~1) != 2) continue;
                    l24 = defense_value(faction, l8, lc, veh, -1) << 3;
                    for (int j = 0; j < 8; j++) {
                        int nx = xrange(game_radius_near_x[j] + l8), ny = game_radius_near_y[j] + lc;
                        if (!on_map(nx, ny) || (nx == B->x && ny == B->y)) continue;
                        l20 = stack_fix(veh_at(nx, ny));
                        if (l20 >= 0 && game_vehs[l20].faction != faction &&
                            !(F->diplo_status[game_vehs[l20].faction] & 7) && stack_check(l20, 4, -1, -1, -1))
                            l24 /= 2;
                    }
                    int v = l24;
                    if (!V->moves_spent && (here->bits & 4) && (tile(l8, lc)->bits & 4))
                        v = game_chassis[proto_of(V->proto)->chassis].speed < 2 ? l24 + 8 : l24 + 0x100;
                    const struct map_tile *t = tile(l8, lc);
                    if ((t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40) {
                        if (!has_project(8, faction) && (game_weapons[proto_of(V->proto)->weapon].offense >= 0 || proto > 0x3f)) {
                            if (!has_tech(game_rules_basic->tech_ease_fungus_move, faction)) v -= 0x100;
                        } else if (V->proto < 0x40 && (game_weapons[proto_of(V->proto)->weapon].offense < 0 || V->proto == 15)) {
                            v += 0x100;
                        } else {
                            v += 8;
                        }
                    }
                    l20 = veh_at(l8, lc);
                    v -= stack_check(l20, 1, -1, -1, -1) * 8;
                    if (l2c <= v) dir84 = l30, l2c = v;
                }
                if (dir84 >= 0) {
                    V->status_icon = '3';
                    goto at_578b26;
                }
            }
            goto at_572922;
        }
        // Too few: wait two squares off for the rest.
        int n = stack_check(veh, 2, 1, -1, -1) + stack_check(veh, 2, 0, -1, -1);
        l34 = n;
        if (zoc_move(x, y, faction) || n < 2) {
            l90 = 1;
            V->status_icon = '4';
            goto at_57707f;
        }
        l7c = -1, l2c = 0;
        for (l30 = 0; l30 < 8; l30++) {
            l8 = xrange(game_radius_near_x[l30] + x), lc = game_radius_near_y[l30] + y;
            if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE) continue;
            if (((abs(lc - B->y) + x_dist(l8, B->x)) & ~1) != 4) continue;
            l74 = anything_at(l8, lc);
            if (l74 >= 0 && l74 != faction) continue;
            int v = 10;
            if (zoc_sea(l8, lc, faction)) {
                if (hex_cost(proto, faction, x, y, l8, lc, 0) >= veh_moves(veh) - 1) continue;
                v = 0x14;
            }
            int u = veh_at(l8, lc);
            if (u >= 0) {
                int c = stack_check(u, 2, 1, -1, -1) + stack_check(u, 2, 0, -1, -1);
                if (c >= l34) continue;
                v -= c;
            }
            if (v > l2c) l2c = v, l7c = l30;
        }
        if (l7c < 0) {
            l90 = 1;
            V->status_icon = '4';
            goto at_57707f;
        }
        dir84 = l7c;
        V->status_icon = '5';
        goto at_578b26;
    }
at_air:
    if (!planet_buster(veh) || triad != TRIAD_AIR) {
        int r = em_air_power(veh, own_dist, zocm, &x, &y);
        here = tile(x, y);
        if (r == -5) return 1;
        if (r == -1) {
            if (V->x == V->waypoint_x[0] && V->y == V->waypoint_y[0]) {
                l90 = 1;
            } else {
                l74 = anything_at(V->waypoint_x[0], V->waypoint_y[0]);
                if ((l74 >= 0 && l74 == faction) || (F->diplo_status[l74] & TREATY_PACT)) goto finish;
            }
        } else if (r == -2) {
            if (V->x != V->waypoint_x[0] || V->y != V->waypoint_y[0]) goto finish;
            l90 = 1;
        } else if (r == -4) {
            l90 = 1;
        } else {
            if (r == -3) {
                if ((V->state & 0x200) && V->order_auto_type == 10) {
                    V->order = ORDER_NONE;
                    V->state &= ~0x200u;
                    return 0;
                }
            } else if (r >= 0) {
                dir84 = r;
            }
            goto at_578b29;
        }
        goto at_57707f;
    }
    dir84 = enemy_planet_buster(veh);
    x = V->x, y = V->y;
    here = tile(x, y);
    goto at_578b29;
at_57707f:
    if (triad == TRIAD_LAND) {
    at_57708a:
        // A coast to be shipped from, for a continent without bases.
        if (plan != PLAN_COLONIZATION && !F->region_total_bases[region] && !(F->region_flags[region] & 6) &&
            F->region_base_plan[region] > 2 && ((y > 0 && y < *game_map_height - 1) || plan == PLAN_ALIEN_ARTIFACT)) {
            for (int i = 0; i < 8; i++) {
                l8 = xrange(game_radius_near_x[i] + x), lc = game_radius_near_y[i] + y;
                if (on_map(l8, lc) && alt_of(tile(l8, lc)) < ALT_SHORE) {
                    add_goal(faction, 7, 1, l8, lc, -1);
                    lc4 = 1;
                    break;
                }
            }
        }
    }
at_57717f:
    // On its way: the next step, unless it leads into an enemy's zone.
    if (V->order == ORDER_MOVE_TO &&
        (!game_weapons[proto_of(V->proto)->weapon].offense ||
         (V->moves_spent && veh_speed(veh, 0) > game_rules_basic->move_rate_roads && plan == 0 && triad == TRIAD_LAND)) &&
        !(V->state & 8)) {
        l50 = abs(y - V->waypoint_y[0]);
        if (((l50 + x_dist(x, V->waypoint_x[0])) & ~1) < 3) goto at_577361;
        dir84 = path_move(game_path, veh, faction);
        if (dir84 < 0) {
            if (zocv == 1) {
                V->order = ORDER_NONE;
                V->state &= 0xfcffbdffu;
            }
        } else {
            l8 = xrange(game_radius_near_x[dir84] + x), lc = game_radius_near_y[dir84] + y;
            l74 = zoc_sea(l8, lc, faction);
            if (l74 == 1) goto at_5772f1;
            if (l74 > 1 && (F->diplo_status[l74] & 0x10)) {
                if (plan == PLAN_INFO_WARFARE &&
                    veh_moves(veh) - hex_cost(proto, faction, x, y, l8, lc, 0) > 0)
                    goto at_577361;
            at_5772f1:
                if (!zocv || (plan != PLAN_INFO_WARFARE && !game_weapons[proto_of(V->proto)->weapon].offense))
                    V->order = ORDER_NONE;
                if (!zocv) goto at_578b34;
            }
        }
    }
at_577361:
    if (V->order == ORDER_MOVE_TO && !l88 && !l90) {
        if (!zoc_veh(x, y, faction)) goto finish;
        l50 = abs(y - V->waypoint_y[0]);
        if (((l50 + x_dist(x, V->waypoint_x[0])) & ~1) == 2 &&
            (l74 = anything_at(V->waypoint_x[0], V->waypoint_y[0])) >= 0 && l74 != faction &&
            !(F->diplo_status[l74] & TREATY_PACT) && V->status_icon != 'p')
            V->order = ORDER_NONE;
        if (V->order == ORDER_MOVE_TO) lc8 = 1;
    }
    l2c = -9999;
    alt = 0;
    if (zoc_sea(x, y, faction) || lc8 || st4 || st6 || (st4 = 1, plan == PLAN_ALIEN_ARTIFACT)) st4 = 0;
    // The best of the nine squares round it (its own included), in an
    // order starting at random.
    l48 = game_rnd(9);
    for (forced = 0; forced < 9; forced++) {
        l30 = (forced + l48) % 9;
        l8 = xrange(game_radius_x[l30] + x), lc = game_radius_y[l30] + y;
        if (!on_map(l8, lc)) continue;
        if (!l30 && !zocv && plan != PLAN_DEFENSIVE) continue;
        const struct map_tile *t = tile(l8, lc);
        l88 = alt_of(t) < ALT_SHORE;
        l20 = stack_fix(veh_at(l8, lc));
        l28 = base_at(l8, lc);
        l6c = t->region;
        if (triad == TRIAD_SEA && !l88) {
            if (l20 >= 0 && l74 != faction && at_sea) goto at_57762a;
            continue;
        }
        if (!(at_sea || l88 || !zocm || triad != TRIAD_LAND || l20 >= 0 || (proto_of(V->proto)->abilities & 4) ||
              proto_of(V->proto)->plan == PLAN_INFO_WARFARE || !zoc_move(l8, lc, faction)))
            continue;
    at_57762a:
        if (!game_weapons[proto_of(V->proto)->weapon].offense && (l20 < 0 || !l30) && l28 < 0 && !(V->state & 8)) {
            l74 = zoc_sea(l8, lc, faction);
            if (l74 > 0) {
                if (l74 == 1 || ((F->diplo_status[l74 - 1] & TREATY_PACT) && (F->diplo_status[l74 - 1] & 0x2000000)))
                    continue;
                if (plan != PLAN_INFO_WARFARE ||
                    (V->moves_spent && veh_moves(veh) - hex_cost(proto, faction, x, y, l8, lc, 0) < 1)) {
                    if (F->diplo_status[l74 - 1] & 0x10) continue;
                }
            }
        }
        l24 = game_rnd(6);
        if (l88 && triad == TRIAD_LAND) {
            // A land unit and a sea square: a base it can sail from, or a
            // transport of its own with room.
            if (l28 >= 0 && has_abil(V->proto, 8)) goto at_577865;
            if (l20 < 0 || game_vehs[l20].faction != faction || stack_check(l20, 8, -1, -1, -1) <= 0 ||
                game_vehs[l20].damage > proto_of(game_vehs[l20].proto)->reactor * 2)
                continue;
            if (plan == PLAN_ALIEN_ARTIFACT) {
                l24 += 99;
                goto at_577865;
            }
            if (plan == PLAN_COLONIZATION) {
                if (F->region_base_plan[region] != 7) continue;
            } else if (plan == PLAN_TERRAFORMING ||
                       !((F->region_base_plan[region] == 7 && plan < 2) ||
                         (!F->region_total_bases[region] && !(F->region_flags[region] & 6) &&
                          F->region_base_plan[region] > 2))) {
                continue;
            }
            l24 += 0x10;
        }
    at_577865:
        if (at_sea && !l88 && triad == TRIAD_LAND && !own_dist && !has_abil(V->proto, 8) &&
            !stack_check(veh, 2, 7, -1, -1))
            continue;
        if (at_sea == l88 && (V->state & 0x10000) && (!zocv || (zocv > 1 && (F->diplo_status[zocv - 1] & 7)))) {
            if (!l30) {
                l24 -= 12;
            } else {
                int t29 = (int8_t)V->unk_29 - l30 + 1;
                if (t29 < 0) t29 = l30 - (int8_t)V->unk_29 - 1;
                if (t29 > 4) t29 = 8 - t29;
                l24 -= t29 * 4;
                if (t29 == 4) l24 -= 0x100;
            }
        }
        if (plan == PLAN_ALIEN_ARTIFACT) {
            int p = 0;
            if (zoc_veh(l8, lc, faction)) p = -0x20;
            if (!is_coast(l8, lc, 0)) p -= 0x20;
            if (!l30 && !p) {
                l24 += (lc4 ? 0x18 : 0) + 8;
                base_find_2(l8, lc, faction);
                if (*game_base_find_dist <= own_dist) l24 += 8;
            }
            l24 += p;
        } else if (lc4 && !l30) {
            l24 += 8;
        }
        if (F->flags & 0x20000000) {
            uint32_t bits = t->bits;
            if (!(bits & 0x100000)) {
                l24 += (bits & MAP_FUNGUS) && alt_of(t) >= 0x40 ? 0x10 : 8;
                if (bits & 0x2000) l24 += 8;
            }
        }
        l74 = whose_territory(faction, l8, lc, NULL, 0);
        if (l74 >= 0 && l74 != faction && proto_of(V->proto)->plan != PLAN_INFO_WARFARE) {
            uint32_t st = F->diplo_status[l74];
            if (!(st & 0xc10)) {
                if ((F->diplo_agenda[l74] & 2) || (st & 0x100000)) continue;
                if (st & 4) l24 -= 6;
                if (st & 2) {
                    l24 -= 12;
                    if (!(st & 1) && (at_sea || !(st & 0x2020) || *game_sunspot_duration > 0)) continue;
                }
            }
        }
        if (st6 && plan == PLAN_DEFENSIVE && l20 >= 0 && l74 == faction && zoc_veh(l8, lc, faction))
            l24 -= stack_check(veh, 1, -1, -1, -1) * 2;
        if (at_sea && triad == TRIAD_LAND && own_dist) {
            int b = F->region_base_plan[l6c];
            if (b == plan) l24 += 0x20;
            if (b != 7) l24 += 0x20;
            base_find(l8, lc);
            l24 = l24 + (0x20 - *game_base_find_dist) - stack_check(l20, 1, -1, -1, -1);
        }
        if (goody_at(l8, lc)) {
            if (plan == PLAN_NAVAL_TRANSPORT && stack_check(veh, 1, -1, -1, -1) > 1) continue;
            l24 += 99;
        }
        {
            int score_type = 1;
            if (!at_sea && !l88 && region != l6c && F->region_total_bases[region]) {
                score_type = 0;
                if (!game_weapons[proto_of(V->proto)->weapon].offense) {
                    if (plan == PLAN_COLONIZATION && !F->region_best_site[region]) score_type = 1;
                } else if (plan == PLAN_RECONNAISSANCE || (region_plan > 2 && !(F->region_flags[region] & 0x20))) {
                    score_type = 1;
                }
            }
            if (score_type && (!st4 || !exploring)) {
                lbc = st6 ? 2 : plan;
                const struct veh *S = game_vehs + l20;
                int friendly = !(l20 < 0 || (S->faction != faction && !(F->diplo_status[S->faction] & TREATY_PACT)));
                if (!l30 && stack_check(l20, 1, l20, -1, -1) == 1) friendly = 0;
                if (at_sea && triad == TRIAD_LAND && lbc != 8 && S->faction == faction) {
                    if (stack_check(l20, 2, 8, -1, -1)) l24 += 8;
                    if (lbc < 2 || lbc == 11) {
                        if (F->region_flags[l6c] & 0x20) l24 += 0x10;
                        if (F->region_flags[l6c] & 0x40) l24 += 0x10;
                    }
                }
                switch (lbc) {
                case 0:
                    if (friendly && !st4) l24 += stack_check(l20, 2, 2, -1, -1) * 4;
                    l24 = l24 - stack_check(l20, 2, 0, -1, -1) * 2 -
                          hex_cost(proto, faction, x, y, l8, lc, 0) / game_rules_basic->move_rate_roads;
                    break;
                case 1:
                    if (zoc_veh(l8, lc, faction)) l24 += defense_value(faction, l8, lc, veh, -1) * 2;
                    break;
                case 2:
                    if (zoc_veh(l8, lc, faction)) l24 += defense_value(faction, l8, lc, veh, -1) * 2;
                    if (friendly && !st4) {
                        int a = stack_check(l20, 2, 0, -1, -1), d = stack_check(l20, 2, 2, -1, -1);
                        l24 = l24 + a * 4 - d * 2;
                    }
                    break;
                case 3:
                    l24 -= hex_cost(proto, faction, x, y, l8, lc, 0) / game_rules_basic->move_rate_roads;
                    break;
                case 6:
                    l24 += stack_check(l20, 2, 7, -1, -1) * 4;
                    l24 -= stack_check(l20, 2, 6, -1, -1) * 2;
                    break;
                case 7:
                    if (!st4) l24 += stack_check(l20, 2, 6, -1, -1) * 4;
                    l24 -= stack_check(l20, 2, 7, -1, -1) * 8;
                    break;
                case 8:
                    if (at_sea && zoc_sea(l8, lc, faction)) continue;
                    if (l74 >= 0 && l74 != faction && (F->diplo_status[l74] & 3) && V->order != ORDER_MOVE_TO) l24 -= 0x10;
                    if (at_sea && F->region_base_plan[l6c] == 8) {
                        l24 = l24 + 0x10 + game_continents[l6c].open_terrain;
                        if (!(F->region_flags[l6c] & 0x20)) l24 += 8;
                        if (!(F->region_flags[l6c] & 0x10)) l24 += 0x10;
                    }
                    break;
                case 11:
                    if (friendly) l24 += stack_check(l20, 14, -1, -1, -1) * 2;
                    break;
                }
                if (alt_of(here) < alt_of(t) &&
                    (game_rules_basic->combat_atk_higher || game_rules_basic->combat_pen_atk_lower))
                    l24 += 2;
            }
        }
        l4c = 0;
        if (l20 < 0) {
            if (!(l28 < 0 || game_bases[l28].faction == faction || (F->diplo_status[l74] & TREATY_PACT))) {
                // A base of another faction's: taken if the AI may.
                if (plan == PLAN_INFO_WARFARE && !(proto_of(V->proto)->abilities & 0x10000000) &&
                    has_project(15, game_bases[l28].faction))
                    continue;
                uint32_t st = F->diplo_status[l74];
                if ((st & 7) && !(st & 0x800)) {
                    if (!(st & 0x20)) continue;
                    if (st & 2) {
                        int r = game_rnd(6);
                        if (reputation(l74, faction) < r - F->ai_fight) continue;
                    }
                }
                if (!game_weapons[proto_of(V->proto)->weapon].offense) continue;
                l4c = 1;
                l24 += 9999;
            }
            if ((t->bits & 0x2000) && want_monolith(veh)) l24 += 99;
            goto at_57883f;
        }
        {
            // A stack: its own, to share; or an enemy's, to attack.
            int vf = game_vehs[l20].faction;
            l74 = vf;
            if (triad == TRIAD_AIR && game_chassis[proto_of(V->proto)->chassis].range && V->movement_turns &&
                V->order == ORDER_MOVE_TO && (V->waypoint_x[0] != l8 || V->waypoint_y[0] != lc))
                continue;
            if (stack_check(l20, 1, -1, -1, -1) == 1 && !(F->diplo_status[vf] & 0x11) &&
                proto_of(game_vehs[l20].proto)->plan == PLAN_ALIEN_ARTIFACT &&
                game_weapons[proto_of(V->proto)->weapon].offense && l28 < 0) {
                // A lone artifact to take.
                if ((F->diplo_status[vf] & 0x20) || whose_territory(faction, l8, lc, NULL, 0) == faction || game_rnd(2)) {
                    l4c = 1;
                    l24 += 8;
                    goto at_57883f;
                }
                continue;
            }
            if (plan == PLAN_INFO_WARFARE) {
                if (vf != faction) {
                    if (!stack_check(l20, 2, 11, -1, -1) ||
                        ((F->diplo_status[vf] & TREATY_PACT) && (F->diplo_status[vf] & 0x2000000)))
                        goto at_5782ee;
                    l4c = 1;
                    int r = em_odds(veh, l30 - 1, &l20);
                    if (whose_territory(faction, l8, lc, NULL, 0) == faction) r *= 2;
                    else if (l28 < 0 && (F->diplo_status[vf] & 7)) r /= 2;
                    if (r < 8) {
                        if (l28 < 0) continue;
                    } else {
                        l24 += r;
                    }
                }
                goto at_57883f;
            }
        at_5782ee:
            if (vf == faction || (l50 = vf + faction * 0x833, F->diplo_status[vf] & TREATY_PACT)) goto at_57883f;
            if (at_sea && !l88 && triad == TRIAD_SEA && !can_arty(proto, 1)) continue;
            if (stack_check(l20, 1, -1, -1, -1) == 1 && proto_of(game_vehs[l20].proto)->plan == PLAN_INFO_WARFARE &&
                (F->diplo_status[game_vehs[l20].faction] & 6) &&
                whose_territory(faction, game_vehs[l20].x, game_vehs[l20].y, NULL, 0) == faction) {
                // An enemy probe team in its territory.
                l4c = 1;
                l24 += 99;
                goto at_57883f;
            }
            if (plan == PLAN_NAVAL_TRANSPORT) {
                if (stack_check(veh, 1, -1, -1, -1) >= 2) continue;
            } else if (!(plan < 8 || plan == PLAN_INFO_WARFARE)) {
                continue;
            }
            if (!((!at_sea || triad != TRIAD_LAND || has_abil(V->proto, 8)) &&
                  (!stack_check(l20, 3, 2, -1, -1) || has_abil(V->proto, 0x20) || l28 >= 0 || (t->bits & 0x40000) ||
                   stack_check(l20, 6, 0x80, -1, -1))))
                continue;
            if (plan == PLAN_INFO_WARFARE) {
                if (stack_check(l20, 1, -1, -1, -1) < 2) {
                    int n = *game_veh_count;
                    if (base_who(game_vehs[l20].x, game_vehs[l20].y) < 0 && em_probe(veh, l20)) {
                        if (n != *game_veh_count) return 1;
                        l2c = 999;
                        dir84 = l30 - 1;
                        alt = 1;
                    }
                }
                continue;
            }
            if (!game_weapons[proto_of(V->proto)->weapon].offense || !(F->diplo_status[vf] & 0x810)) continue;
            l4c = 1;
            int r = em_odds(veh, l30 - 1, &l20);
            if (!lb8 && (veh_moves(veh) >= game_rules_basic->move_rate_roads || proto == 8) && (own_dist || plan < 1))
                goto at_57868c;
            {
                int d = em_odds(l20, (l30 + 3) & 7, NULL);
                if (!d) {
                    if (stack_check(veh, 1, -1, -1, -1) != 1 || proto_of(game_vehs[l20].proto)->plan == PLAN_INFO_WARFARE)
                        goto at_57868c;
                } else {
                    int k = lb8 ? lb8 + 1 : 0;
                    if (r * 4 <= (4 - k) * d && d > 7 && lb8 + 6 <= r) goto at_57868c;
                }
                continue;
            }
        at_57868c:
            r = compute_odds(r, faction, veh, l20, l28);
            if (!l74 && (own_base < 0 || !game_bases[own_base].eco_damage) &&
                game_vehs[l20].order_auto_type != faction)
                r = F->se[7] < 3 ? r * 2 : r << 3;
            if (r <= 7) continue;
            l24 += r;
        }
    at_57883f:
        if ((st4 || exploring) && l30 && (triad == TRIAD_AIR || l88 == (triad == TRIAD_SEA))) {
            // Exploring: the unknown squares further on in that direction.
            if (exploring) {
                base_find_2(l8, lc, faction);
                l24 += *game_base_find_dist * 2;
            }
            for (l34 = 0; l34 < 8; l34++) {
                l50 = xrange(game_radius_x[l30] * l34 + l8);
                terr = game_radius_y[l30] * l34 + lc;
                if (!on_map(l50, terr) || !(triad == TRIAD_AIR || (alt_of(tile(l50, terr)) < ALT_SHORE) == (triad == TRIAD_SEA)))
                    continue;
                for (l5c = 0; l5c < 0x20; l5c += 4) {
                    int j = l5c / 4;
                    int nx = xrange(game_radius_near_x[j] + l50), ny = game_radius_near_y[j] + terr;
                    if (!on_map(nx, ny)) continue;
                    if (F->flags & 0x20000000) {
                        uint32_t bits = tile(nx, ny)->bits;
                        if (!(bits & 0x100000)) {
                            l24 += (bits & MAP_FUNGUS) && alt_of(tile(nx, ny)) >= 0x40 ? 0x10 : 8;
                            if (bits & 0x2000) l24 += 8;
                        }
                    }
                    if (!is_known(nx, ny, faction)) {
                        l24 += 2 + exploring * 2;
                        if (triad == TRIAD_SEA && alt_of(tile(nx, ny)) >= ALT_SHORE &&
                            !F->region_total_bases[tile(nx, ny)->region])
                            l24 += 8;
                    }
                    if (goody_at(nx, ny)) l24 += 2 + exploring * 2;
                    if (plan == PLAN_COLONIZATION) {
                        l74 = whose_territory(faction, nx, ny, NULL, 0);
                        if (l74 >= 0 && l74 != faction && (F->diplo_status[l74] & 3)) l24 -= 6;
                    }
                }
            }
        }
        if (l2c < l24) {
            dir84 = l30 - 1;
            l2c = l24;
            alt = l4c;
        }
    }
    if (!alt) {
        if (l90) {
            dir84 = -1;
            V->order = ORDER_NONE;
        }
        if (lc8 && V->order == ORDER_MOVE_TO) goto finish;
        if (can_arty(proto, 1) && !V->order && (triad == TRIAD_SEA || alt_of(here) >= ALT_SHORE || !near_dist)) {
            // Nothing better: improvements to shell.
            for (l50 = 0; l50 < 100; l50 += 4) {
                int i = l50 / 4;
                l8 = xrange(game_radius_x[i] + x), lc = game_radius_y[i] + y;
                if (!on_map(l8, lc) || alt_of(tile(l8, lc)) < ALT_SHORE || anything_at(l8, lc) >= 0) continue;
                int o = whose_territory(faction, l8, lc, NULL, 0);
                if (o < 0 || o == faction || !(F->diplo_status[o] & 0x10)) continue;
                int r = tile(l8, lc)->region;
                if ((!F->region_total_bases[r] || F->region_base_plan[r] == 2 || triad != TRIAD_LAND) &&
                    (here->bits & 0x1000) && (here->bits & MAP_ARTY_TARGETS)) {
                    if (!net_own) {
                        game_log_say(text(0x68eae0), veh, l8, lc);
                        em_destroy(veh, 0, l8, lc);
                        return 1;
                    }
                    game_log_say(text(0x68eac0), veh, l8, lc);
                    game_message_data(0x2413, veh, l8, lc, 0, 0);
                    goto net_exec;
                }
            }
        }
    } else if (planet_buster(veh)) {
        dir84 = -1;
        V->order = ORDER_NONE;
    }
    V->status_icon = 'x';
at_578b26:
at_578b29:
    if (dir84 >= 0) {
        l8 = xrange(game_radius_near_x[dir84] + x), lc = game_radius_near_y[dir84] + y;
        V->order = ORDER_AI_MOVE_TO;
        V->waypoint_x[0] = (int16_t)l8, V->waypoint_y[0] = (int16_t)lc;
        goto finish;
    }
at_578b34:
    V->state &= ~0x10000u;
    V->order = ORDER_NONE;
finish:
    if ((int8_t)V->order < ORDER_MOVE_TO) V->state &= ~0x10000u;
    if (V->order == ORDER_NONE || V->order == 2) {
        if (triad == TRIAD_AIR && own_dist && !(tile(x, y)->bits & 0x40000)) {
            action_home(veh, 0);
        } else {
            V->order = !game_is_human(faction) || (V->state & 0x200) ? 2 : 0;
            veh_skip(veh);
        }
    }
    if (V->order == ORDER_MOVE_TO && V->waypoint_x[0] == x && V->waypoint_y[0] == y) {
        V->order = ORDER_NONE;
        veh_skip(veh);
    }
    {
        uint8_t o = V->order;
        if ((o == ORDER_MOVE_TO || o == ORDER_AI_MOVE_TO) && (plan == PLAN_TERRAFORMING || plan == PLAN_COLONIZATION)) {
            // A former or colony pod doesn't step next to an enemy.
            int d = path_move(game_path, veh, o & 0x40 ? -1 : V->faction);
            if (d < 0) {
                V->order = ORDER_NONE;
                return 0;
            }
            l8 = xrange(game_radius_near_x[d] + x), lc = game_radius_near_y[d] + y;
            if (base_who(l8, lc) >= 0) return 0;
            for (int j = 0; j < 8; j++) {
                int nx = xrange(game_radius_near_x[j] + l8), ny = game_radius_near_y[j] + lc;
                if (!on_map(nx, ny) || (alt_of(tile(nx, ny)) < ALT_SHORE) != (alt_of(tile(l8, lc)) < ALT_SHORE)) continue;
                l50 = abs(ny - y);
                if (((l50 + x_dist(nx, x)) & ~1) <= 2) continue;
                l20 = stack_fix(veh_at(nx, ny));
                if (l20 >= 0 && game_vehs[l20].faction != faction && (F->diplo_status[game_vehs[l20].faction] & 0x10) &&
                    (game_weapons[proto_of(game_vehs[l20].proto)->weapon].offense || stack_check(l20, 14, -1, -1, -1))) {
                    V->order = ORDER_NONE;
                    return 0;
                }
            }
            return 0;
        }
    }
    return 0;
net_exec:
    game_net_await_exec(game_net_daemon, 1);
    return 1;
}

// The original reads some of its locals before it has set them (in an
// automated former's search for fungus, [ebp-0x38]; others on some
// paths): whatever lay below its stack. The entry hands ours the bytes
// where the original's locals would be, and a check gives both runs the
// same ones.
int enemy_move_at(int veh, const int32_t *frame);
// fn(veh) with `frame` where its locals will be.
int em_call_frame(int (*fn)(int), int veh, const int32_t *frame);
__asm__(".text\n"
        ".globl em_call_frame\n"
        "em_call_frame:\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    movl 12(%esp), %eax\n"     // fn
        "    movl 20(%esp), %esi\n"     // frame
        "    leal -0x2d0(%esp), %edi\n" // fn's entry stack pointer will be 8 below this one, its ebp 12
        "    movl $0xb1, %ecx\n"        // FRAME_WORDS
        "    cld\n"
        "    rep movsl\n"
        "    pushl 16(%esp)\n"          // veh
        "    call *%eax\n"
        "    addl $4, %esp\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    ret\n"
        ".globl enemy_move\n"
        "enemy_move:\n"
        "    movl %esp, %eax\n"
        "    subl $0x2d0, %esp\n"       // the bytes below stay put
        "    leal -0x2c8(%eax), %ecx\n" // where the original's ebp-0x2c4 would be
        "    pushl %ecx\n"
        "    pushl 4(%eax)\n"           // veh
        "    call enemy_move_at\n"
        "    addl $0x2d8, %esp\n"
        "    ret\n");
_Static_assert(FRAME_WORDS == 0xb1, "the entries copy 0xb1 words");
int enemy_move_at(int veh, const int32_t *frame) { return em_body(veh, frame); }

// ---- The AI's turn around its units ---------------------------------------------

// One unit's move. A unit already on its way (moved this turn, on a go-to
// order, not a transport, not exploring, no faction's unit next to it)
// just carries its order out. Otherwise the game's enemy_move() picks its
// move; done (non-zero), or with no moves left, the unit is done (1). Its
// order is then carried out (action(); with none, it waits) and it may move
// again (0). In a network game the player's own units are handed to the
// daemon instead; the original's debugging log lines are kept.
int enemy_veh(int veh) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction;
    int net = faction == *game_current_faction && *game_net_multiplayer;
    if (*game_net_multiplayer) *game_veh_attack_flags = 3;
    game_do_all_non_input();
    int going = V->moves_spent && V->order == ORDER_MOVE_TO && proto_of(V->proto)->plan != PLAN_NAVAL_TRANSPORT &&
                !(V->state & VEH_STATE_EXPLORE) && !zoc_any(V->x, V->y, faction);
    if (!going) {
        if (game_enemy_move(veh)) return 1;
        if (no_moves(veh)) {
            if (*game_net_multiplayer) game_log_say(text(0x68eafc), veh, 0, 0);
            if (net) {
                game_synch_veh(veh);
                game_net_await_synch(game_net_daemon);
                game_net_unlock_veh(game_net_daemon);
            }
            return 1;
        }
    }
    if (net) {
        game_synch_veh(veh);
        game_net_await_synch(game_net_daemon);
        return game_net_daemon_action(game_net_daemon, veh, 1) ? 1 : 0;
    }
    if (*game_net_multiplayer) {
        char num[8];
        snprintf(num, sizeof num, "%d", veh);
        game_log_say5(text(0x68eb10), num, (int8_t)V->order, V->waypoint_x[0], V->waypoint_y[0]);
    }
    if (!action(veh)) veh_skip(veh);
    if (*game_net_multiplayer) {
        char icon[2] = {(char)V->status_icon, 0};
        game_log_say5(text(0x68eb28), icon, veh, V->x, V->y);
    }
    return 0;
}

// A faction's units given their moves, the last unit first, in ten
// passes: artillery that can fire from the sea; defensive units and
// transports carrying land units; aircraft other than air superiority;
// combat units; undamaged units; everything else twice; then (without the
// test below) all again, naval transports (not native life's), and all a
// last time. The first seven passes skip drop pods, air superiority and
// units that take no goals (group 21). A unit on board (sentry board)
// isn't moved; otherwise it moves while it has moves left (in the naval
// transports' pass, at least once), until it is done or a unit is gone,
// at most 32 times (each move not on a go-to order counting twice, but
// for aircraft), after which it is made to wait, and the moves go on
// while it has moves (unlike Thinker, which stops there). The faction's
// flag 0x10000 is cleared after.
void enemy_turn(int faction) {
    for (int pass = 0; pass < 10; pass++) {
        for (int veh = *game_veh_count - 1; veh >= 0; veh--) {
            if (veh >= *game_veh_count) continue;
            struct veh *V = &game_vehs[veh];
            if (V->faction != faction) continue;
            const struct unit_proto *P = proto_of(V->proto);
            switch (pass) {
            case 0:
                if (!can_arty(V->proto, 1)) continue;
                break;
            case 1:
                if (P->plan != PLAN_DEFENSIVE &&
                    (P->plan != PLAN_NAVAL_TRANSPORT || !stack_check(veh, 3, 0, -1, -1)))
                    continue;
                break;
            case 2:
                if (triad_of(V->proto) != TRIAD_AIR || P->plan == PLAN_AIR_SUPERIORITY) continue;
                break;
            case 3:
                if (P->plan != PLAN_COMBAT) continue;
                break;
            case 4:
                if (V->damage) continue;
                break;
            }
            if (pass < 7 && (has_abil(V->proto, ABL_DROP_POD) || proto_of(V->proto)->plan == PLAN_AIR_SUPERIORITY ||
                             proto_of(V->proto)->group == GROUP_NO_GOALS))
                continue;
            if (pass == 8 && (proto_of(V->proto)->plan != PLAN_NAVAL_TRANSPORT || !faction)) continue;
            int count = 0;
            while (V->order != ORDER_SENTRY_BOARD) {
                if ((pass != 8 || count) && no_moves(veh)) break;
                int n = *game_veh_count;
                if (enemy_veh(veh) || *game_veh_count != n) break;
                if (V->order != ORDER_MOVE_TO && triad_of(V->proto) != TRIAD_AIR) count++;
                if (++count > 32) veh_skip(veh);
            }
        }
    }
    game_factions[faction].flags &= ~(uint32_t)FACTION_FLAG_UNK_10000;
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module ai_module;
#define AI (&ai_module)

// The base native life heads for only reads the game (and leaves its
// distance).
SHADOW_WRITER(AI, int, alien_base, (int a, int b, int c), (a, b, c), WRITES(game_base_find_dist, 4), "%d, %d, %d", a, b,
              c)
// A planet buster put down moves the unit and shows squares: all of the
// game.
SHADOW_WRITER(AI, int, enemy_planet_buster, (int a), (a), hooks_writes_game(&w_), "%d", a)
// An air unit's move sets orders, puts missiles down and sets off payloads
// (no fight or step of its own): all of the game, and the square it
// returns.
SHADOW_WRITER(AI, int, air_power, (int a, int b, int c, int *d, int *e), (a, b, c, d, e),
              (hooks_writes_game(&w_), WRITES(d, sizeof(int)), WRITES(e, sizeof(int))), "%d, %d, %d", a, b, c)

// The checks below follow units that search paths: Path_find gets what
// the original's calls found below their stack, in turn (path.c).
static void check_begin(struct hook_writes *w) {
    writes_paths(w, game_path);
    hooks_writes_begin(w);
    hooks_nested++;
    path_stale_record();
}
static void check_middle(struct hook_writes *w) {
    hooks_nested--;
    hooks_writes_middle(w);
    path_stale_replay();
}

// Native life's moves end in a fight, or a unit's attack on a square
// (action_destroy, which ends in the unit's step and may become a fight),
// that a check would make twice. A run of ours first, on a protected copy
// of the game, sees whether it would: then the game's call runs the
// original, as it does when native life near a human's base might eat what
// is under it. Other calls are checked, with ours kept from fighting
// (which would be a mismatch). The sweep checks the rest with the step
// held.
SHADOW_DECLARE(int, alien_move, (int a))
static unsigned long long alien_move_unchecked;
static void probe_alien_move(int veh) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, game_path);
    hooks_writes_begin(&w_);
    probing = 1, probe_fight = probe_destroy = probe_threat = 0;
    alien_move(veh);
    probing = 0;
    hooks_writes_abandon(&w_);
}
static int check_alien_move(int veh, int quiet) {
    struct hook_writes w_ = {0};
    check_begin(&w_);
    int want = orig_alien_move(veh);
    check_middle(&w_);
    probing = quiet, probe_fight = probe_destroy = 0;
    int got = alien_move(veh);
    probing = 0;
    path_stale_stop();
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (!diff_ && (probe_fight || probe_destroy)) diff_ = "would fight or attack a square";
    if (diff_) hooks_mismatch(&stats_alien_move, "(%d) %s", veh, diff_);
    hooks_ui_release();
    return want;
}
static int shadow_alien_move(int veh) {
    if (hooks_nested) return orig_alien_move(veh);
    if (shadow_bypass()) return alien_move(veh);
    stats_alien_move.calls++;
    shadow_tick(AI);
    probe_alien_move(veh);
    if (probe_fight || probe_destroy || probe_threat) {
        alien_move_unchecked++;
        return orig_alien_move(veh);
    }
    return check_alien_move(veh, 1);
}

// A unit's move and the faction's turn around its units call enemy_move()
// through the game's pointer, with all it sets off (fights, steps,
// messages): the game's calls run the original, and the sweep checks them
// with enemy_move held (shadow_enemy_move, below) and the steps held.
static int (*orig_enemy_veh)(int veh) __attribute__((used));
static void (*orig_enemy_turn)(int faction) __attribute__((used));
static struct hook_stats stats_enemy_veh = {"enemy_veh"}, stats_enemy_turn = {"enemy_turn"};
static int hold_go_tos;
static int check_enemy_veh(int veh) {
    struct hook_writes w_ = {0};
    check_begin(&w_);
    hold_go_tos = 2;
    int want = orig_enemy_veh(veh);
    check_middle(&w_);
    hold_go_tos = 2;
    int got = enemy_veh(veh);
    path_stale_stop();
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_enemy_veh, "(%d) %s", veh, diff_);
    hooks_ui_release();
    return want;
}
static void check_enemy_turn(int faction) {
    struct hook_writes w_ = {0};
    check_begin(&w_);
    hold_go_tos = 3;
    orig_enemy_turn(faction);
    check_middle(&w_);
    hold_go_tos = 3;
    enemy_turn(faction);
    path_stale_stop();
    const char *diff_ = hooks_writes_end(&w_, 0, 0);
    if (diff_) hooks_mismatch(&stats_enemy_turn, "(%d) %s", faction, diff_);
    hooks_ui_release();
}

// The originals run as the game called them, on the stack as it left it:
// enemy_move() reads a local it never set (when it looks for a base for a
// unit with order_auto_type 6), and the sweep, which runs the AI's turn
// deep below the game's call, would leave its own values there. So the
// entries are in assembly: from a check they jump straight to the
// original; otherwise the 64 KB below the game's stack pointer are kept
// while ai_pre_*() counts the call and starts the sweep, then put back,
// and the entry jumps to the original (or, in a check or sweep, ours).
enum { STACK_KEPT = 0x10000 };
static uint32_t stack_kept[STACK_KEPT / 4] __attribute__((used));
static int stack_keeping __attribute__((used));
int ai_pre_enemy_veh(void);
int ai_pre_enemy_veh(void) {
    if (shadow_bypass()) return 1;
    stats_enemy_veh.calls++;
    shadow_tick(AI);
    return 0;
}
int ai_pre_enemy_turn(void);
int ai_pre_enemy_turn(void) {
    if (shadow_bypass()) return 1;
    stats_enemy_turn.calls++;
    shadow_tick(AI);
    return 0;
}
// The bytes kept are those from 16 below the entry stack pointer down
// (the entry's own three pushes stay clear of them).
#define KEEP_COPY(from, to)                  \
    "    pushl %esi\n"                       \
    "    pushl %edi\n"                       \
    "    pushl %ecx\n"                       \
    "    " from "\n"                          \
    "    " to "\n"                            \
    "    movl $0x4000, %ecx\n" /* STACK_KEPT / 4 */ \
    "    cld\n"                               \
    "    rep movsl\n"                         \
    "    popl %ecx\n"                         \
    "    popl %edi\n"                         \
    "    popl %esi\n"
#define KEEP_SAVE KEEP_COPY("leal -0x10004(%esp), %esi", "movl $stack_kept, %edi")
#define KEEP_RESTORE KEEP_COPY("movl $stack_kept, %esi", "leal -0x10004(%esp), %edi")
#define SHADOW_KEEP_STACK(name)                  \
    void shadow_##name(void);                    \
    __asm__(".text\n"                            \
            "shadow_" #name ":\n"                \
            "    cmpl $0, hooks_nested\n"        \
            "    jne 2f\n"                       \
            "    cmpl $0, stack_keeping\n"       \
            "    jne 3f\n"                       \
            "    movl $1, stack_keeping\n"       \
            KEEP_SAVE                             \
            "    call ai_pre_" #name "\n"        \
            KEEP_RESTORE                          \
            "    movl $0, stack_keeping\n"       \
            "    testl %eax, %eax\n"             \
            "    jnz 1f\n"                       \
            "2:  jmp *orig_" #name "\n"          \
            "1:  jmp " #name "\n"                \
            "3:  call ai_pre_" #name "\n"        \
            "    testl %eax, %eax\n"             \
            "    jnz 1b\n"                       \
            "    jmp *orig_" #name "\n");
SHADOW_KEEP_STACK(enemy_veh)
SHADOW_KEEP_STACK(enemy_turn)
_Static_assert(STACK_KEPT == 0x10000, "the entries' copies");

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}

// The game's enemy_move(), held in the sweep's checks: a made-up move,
// the same for both runs, from the unit's state. It is done (1), or it
// waits, or it spends some of its moves on an order: none, a former's work,
// or (a few times a run) a go-to order a square away; now and then the
// unit is killed, and then it is done, as the real enemy_move answers
// after a fight or anything else that may cost it the unit. (Answered 0,
// enemy_veh would carry out the dead unit's order: for the last unit, the
// slot left behind, lifted to (-1, -1), had action() find a path from
// there, reading the square before the map's first.)
static int holding_moves __attribute__((used));
static uint32_t hold_seed;
static struct hook_stats stats_enemy_move = {"enemy_move"};
static void replay_none(const struct hook_ui_call *c) { (void)c; }
// Not held, the game's calls go straight to the original (shadow_enemy_move,
// in assembly, leaves the stack alone).
int held_enemy_move(int veh);
int held_enemy_move(int veh) {
    if (!hooks_protecting()) return orig_enemy_move(veh);
    stats_enemy_move.calls++;
    struct veh *V = &game_vehs[veh];
    struct hook_ui_call c = {.name = "enemy_move", .replay = replay_none, .arg = {veh, V->x, V->y, V->moves_spent}};
    hooks_ui_hold(&c);
    uint32_t h = mix(hold_seed ^ (uint32_t)veh * 0x9e3779b9u ^ (uint32_t)V->moves_spent << 20 ^
                     (uint32_t)(uint16_t)V->x << 10 ^ (uint32_t)(uint16_t)V->y);
    int rate = game_rules_basic->move_rate_roads;
    switch (h % 16) {
    case 0:
    case 1:
        return 1;
    case 2:
        veh_skip(veh);
        return (h >> 4) & 1;
    case 3:
        if ((h >> 4) % 4 == 0) {
            veh_kill(veh);
            return 1;
        }
        break;
    }
    V->moves_spent = (uint8_t)(V->moves_spent + rate * (int)(1 + (h >> 8) % 3));
    switch ((h >> 12) % 4) {
    case 0:
    case 1:
        V->order = ORDER_NONE;
        break;
    case 2:
        V->order = (uint8_t)(4 + (h >> 16) % 8);  // a former's work
        break;
    default: {
        int d = (int)((h >> 16) % 8);
        int x = xrange(V->x + game_radius_near_x[d]), y = V->y + game_radius_near_y[d];
        if (hold_go_tos > 0 && on_map(x, y)) {
            hold_go_tos--;
            V->order = (h >> 20) & 1 ? ORDER_MOVE_TO : ORDER_AI_MOVE_TO;
            V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
        } else {
            V->order = ORDER_NONE;
        }
    }
    }
    return 0;
}

// ---- A unit's move: checks ----------------------------------------------------------
//
// A game's call of enemy_move is checked when the move only chooses: a run
// of ours first, on a protected copy of the game, sees whether it would
// carry a move out (em_risky: a fight, an attack on a square, an air drop,
// a probe team's mission, a base founded, a unit given away, an artifact
// studied, any aircraft's move); then the original runs unchecked. The
// rest are checked on all of the game, with both runs given the same word
// where the original reads its unset local, and the original's run kept.
// The sweep checks the rest of the choices in made-up situations.
static unsigned long long enemy_move_unchecked;
static int32_t em_arg_veh __attribute__((used)), em_ret __attribute__((used));
static int32_t em_arg_frame[FRAME_WORDS] __attribute__((used));
// The frames of the game's last calls, for the sweep.
enum { EM_FRAMES = 16 };
static int32_t em_frames[EM_FRAMES][FRAME_WORDS];
static unsigned em_frames_kept;
static void probe_enemy_move(int veh, const int32_t *frame) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, game_path);
    hooks_writes_begin(&w_);
    em_probing = 1, em_risky = 0;
    em_body(veh, frame);
    em_probing = 0;
    hooks_writes_abandon(&w_);
}
static int check_enemy_move(int veh, const int32_t *frame) {
    struct hook_writes w_ = {0};
    // The unit as it was, for the report.
    const struct veh *U_ = &game_vehs[veh < 0 ? -1 - veh : veh];
    char was_[128];
    snprintf(was_, sizeof was_, "proto %d plan %d faction %d order %d auto %d state %x flags %x icon %d at (%d, %d)",
             U_->proto, proto_of(U_->proto)->plan, U_->faction, U_->order, U_->order_auto_type, U_->state, U_->flags,
             U_->status_icon, U_->x, U_->y);
    check_begin(&w_);
    base_find_log = (struct base_find_log){0};
    int want = em_call_frame(orig_enemy_move, veh, frame);
    struct base_find_log found = base_find_log;
    base_find_log = (struct base_find_log){0};
    check_middle(&w_);
    em_probing = 1, em_risky = 0;
    int got = em_body(veh, frame);
    em_probing = 0;
    int searches = path_stale_logged(), searched = path_stale_used();
    path_stale_stop();
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (!diff_ && em_risky) diff_ = "would carry a move out";
    static char count_[160];
    const struct base_find_log *b_ = &base_find_log;
    if (!diff_ && searched != searches) {
        snprintf(count_, sizeof count_, "searched paths %d times, original %d", searched, searches);
        diff_ = count_;
    } else if (!diff_ && b_->count != found.count) {
        snprintf(count_, sizeof count_, "searched for bases %d times, original %d", b_->count, found.count);
        diff_ = count_;
    }
    // A difference in where the last base search left its distance: both
    // runs' last searches.
    if (diff_ && (b_->count != found.count || b_->dist != found.dist))
        fprintf(stderr,
                "chiron:   base searches: %d, last %d (%d, %d, %d) = %d; original's %d, last %d (%d, %d, %d) = %d\n",
                b_->count, b_->kind, b_->x, b_->y, b_->faction, b_->dist, found.count, found.kind, found.x, found.y,
                found.faction, found.dist);
    // The words it may read unset: [ebp-0x38] as "stale", the others after.
    if (diff_)
        hooks_mismatch(&stats_enemy_move, "(%d, stale %d) %s [%s; unset %x %x %x %x %x]", veh, FRAME_AT(frame, 0x38),
                       diff_, was_, FRAME_AT(frame, 0x4), FRAME_AT(frame, 0x24), FRAME_AT(frame, 0x48),
                       FRAME_AT(frame, 0x70), FRAME_AT(frame, 0x78));
    hooks_ui_release();
    return want;
}
// What a check can't run: network games.
static int em_checkable(int veh) {
    (void)veh;
    return !*game_net_multiplayer && !*game_pbem;
}
// The game's call (from shadow_enemy_move): 0 to run the original, 1 ours,
// 2 done (a check ran the original; its result in em_ret).
int ai_pre_enemy_move(void);
int ai_pre_enemy_move(void) {
    int veh = em_arg_veh;
    int32_t frame[FRAME_WORDS];
    memcpy(frame, em_arg_frame, sizeof frame);
    if (shadow_bypass()) return 1;
    memcpy(em_frames[em_frames_kept++ % EM_FRAMES], frame, sizeof frame);
    stats_enemy_move.calls++;
    shadow_tick(AI);
    int v = veh < 0 ? -1 - veh : veh;
    if (!em_checkable(v)) {
        enemy_move_unchecked++;
        return 0;
    }
    probe_enemy_move(veh, frame);
    if (em_risky) {
        enemy_move_unchecked++;
        return 0;
    }
    em_ret = check_enemy_move(veh, frame);
    return 2;
}
// The entry: the unit and the word below the stack noted; held in the
// sweep's checks of enemy_veh and enemy_turn; else the 64 KB below the
// stack kept while ai_pre_enemy_move() checks the call, as for enemy_veh.
void shadow_enemy_move(void);
__asm__(".text\n"
        "shadow_enemy_move:\n"
        "    xorl %ecx, %ecx\n"          // the frame below, before anything is pushed
        "5:  movl -0x2c8(%esp,%ecx,4), %eax\n"
        "    movl %eax, em_arg_frame(,%ecx,4)\n"
        "    incl %ecx\n"
        "    cmpl $0xb1, %ecx\n"
        "    jl 5b\n"
        "    movl 4(%esp), %eax\n"
        "    movl %eax, em_arg_veh\n"
        "    cmpl $0, holding_moves\n"
        "    jne held_enemy_move\n"
        "    cmpl $0, hooks_nested\n"
        "    jne 2f\n"
        "    cmpl $0, stack_keeping\n"
        "    jne 3f\n"
        "    movl $1, stack_keeping\n"
        KEEP_SAVE
        "    call ai_pre_enemy_move\n"
        KEEP_RESTORE
        "    movl $0, stack_keeping\n"
        "4:  testl %eax, %eax\n"
        "    jz 2f\n"
        "    cmpl $1, %eax\n"
        "    je 1f\n"
        "    movl em_ret, %eax\n"
        "    ret\n"
        "2:  jmp *orig_enemy_move\n"
        "1:  jmp enemy_move\n"
        "3:  call ai_pre_enemy_move\n"
        "    jmp 4b\n");

// ---- Sweep ------------------------------------------------------------------------
//
// Each case on a copy of the game of its own, put back after, so that a
// case may move units and change what it likes.

static uint32_t sweep_seed;
static uint32_t rnd32(void) { return sweep_seed = mix(sweep_seed + 0x9e3779b9u); }
static int rnd(int n) { return n > 0 ? (int)(rnd32() % (uint32_t)n) : 0; }

static void case_begin(struct hook_writes *w) {
    *w = (struct hook_writes){.protect_only = 1};
    writes_paths(w, game_path);
    hooks_writes_begin(w);
}

// A unit on the map, of the faction (mostly) or any (-1); or -1.
static int pick_unit(int faction) {
    int n = *game_veh_count;
    for (int k = 0; k < 64 && n > 0; k++) {
        int v = rnd(n);
        const struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        if (faction >= 0 && V->faction != faction && k < 48) continue;
        return v;
    }
    return -1;
}

// A square within `radius` rings (radius table) of (x, y), or (x, y).
static void near_square(int x, int y, int radius, int *sx, int *sy) {
    int i = rnd(game_radius_count[radius]);
    int nx = xrange(x + game_radius_x[i]), ny = y + game_radius_y[i];
    *sx = x, *sy = y;
    if (on_map(nx, ny)) *sx = nx, *sy = ny;
}

// Moves a unit to (x, y), as the game puts units down.
static void put(int veh, int x, int y) {
    if (on_map(x, y)) veh_drop(veh_lift(veh), x, y);
}

// Improvements and fungus made to vary on squares round (x, y).
static void vary_squares(int x, int y) {
    static const uint32_t bits[] = {MAP_ROAD, MAP_FARM, MAP_MINE, MAP_SOLAR, MAP_FOREST, MAP_SENSOR, MAP_FUNGUS,
                                    MAP_MONOLITH, MAP_THERMAL_BORE, MAP_CONDENSER, MAP_UNK_4000, MAP_SOIL_ENRICHER};
    for (int k = rnd(6); k > 0; k--) {
        int sx, sy;
        near_square(x, y, 2, &sx, &sy);
        tile(sx, sy)->bits ^= bits[rnd(12)];
    }
}

static void sweep_alien_base(void) {
    for (int k = 0; k < 40 && *game_base_count > 0; k++) {
        int v = pick_unit(k % 2 ? 0 : -1);
        if (v < 0) return;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        int x = V->x, y = V->y;
        if (rnd(2)) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            near_square(B->x, B->y, 1 + rnd(4), &x, &y);
        }
        if (rnd(3) == 0) V->proto = BSC_SEALURK;
        if (rnd(3) == 0) V->home_base = (int16_t)(rnd(4) ? rnd(*game_base_count) : -1);
        V->order_auto_type = (uint8_t)rnd(FACTION_COUNT);
        for (int j = rnd(4); j > 0; j--) {
            struct base *B = &game_bases[rnd(*game_base_count)];
            B->eco_damage = rnd(2) ? 0 : rnd(20);
            B->intake_2[1 + rnd(2)] = rnd(40);
            game_factions[B->faction].se[SE_PLANET] = rnd(7) - 3;
            int s = veh_at(B->x, B->y);
            if (s >= 0 && rnd(2)) game_vehs[s].proto = 7;  // an alien artifact
        }
        check_alien_base(v, x, y);
        hooks_writes_abandon(&w);
    }
}

static void sweep_alien_move(void) {
    static const int natives[] = {8, 9, 10, 12, 14, 15};
    move_hold_steps(1);
    for (int k = 0; k < 40; k++) {
        int v = pick_unit(k % 3 ? 0 : -1);
        if (v < 0) break;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        if (rnd(3) == 0) V->proto = (int16_t)natives[rnd(6)];
        if (k % 4 == 1) V->proto = BSC_SPORE_LAUNCHER;
        if (rnd(2)) V->faction = 0;
        // Near a base, or a transport of native life with worms aboard
        // somewhere at sea near one.
        if (k % 5 == 2 && *game_base_count) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            for (int i = 0; i < 40; i++) {
                int sx, sy;
                near_square(B->x, B->y, 3, &sx, &sy);
                if (alt_of(tile(sx, sy)) >= ALT_SHORE) continue;
                V->proto = 9;  // an isle of the deep
                put(v, sx, sy);
                int u = pick_unit(0);
                if (u >= 0 && u != v && triad_of(game_vehs[u].proto) == TRIAD_LAND) put(u, sx, sy);
                else if (u >= 0 && u != v) game_vehs[u].proto = 8, put(u, sx, sy);
                break;
            }
        } else if (rnd(3) == 0 && *game_base_count) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            int sx, sy;
            near_square(B->x, B->y, 1 + rnd(3), &sx, &sy);
            if (!occupied(tile(sx, sy), MAP_BASE)) put(v, sx, sy);
        }
        V->moves_spent = (uint8_t)(rnd(3) ? 0 : rnd(8));
        V->damage = (uint8_t)(rnd(3) ? 0 : rnd(10));
        V->movement_turns = (uint8_t)rnd(30);
        if (rnd(10) == 0) V->flags ^= VEH_FLAG_LURKER;
        if (rnd(10) == 0) V->flags ^= VEH_FLAG_INVISIBLE;
        V->order_auto_type = (uint8_t)rnd(FACTION_COUNT);
        if (rnd(4) == 0) V->state ^= VEH_STATE_MONOLITH;
        V->unk_29 = (uint8_t)(rnd(9) - (rnd(4) == 0));
        if (rnd(3) == 0) *game_turn = rnd(400);
        if (rnd(3) == 0) *game_difficulty = rnd(6);
        if (rnd(4) == 0) *game_rules ^= RULES_INTENSE_RIVALRY;
        int b = alien_base(v, V->x, V->y);
        if (b >= 0 && rnd(2)) {
            int f = game_bases[b].faction;
            *game_human_factions |= (uint8_t)(1u << f);
            game_factions[f].diff_level = rnd(6);
            if (rnd(2)) game_factions[f].se[SE_PLANET] = rnd(5) - 2;
            if (rnd(2)) *game_current_faction = f;
        }
        vary_squares(V->x, V->y);
        if (rnd(3) == 0) tile(V->x, V->y)->bits ^= MAP_FUNGUS;
        probe_alien_move(v);
        if (!probe_fight && !(probe_destroy && move_destroy_fights(v, probe_x, probe_y))) check_alien_move(v, 0);
        hooks_writes_abandon(&w);
    }
    move_hold_steps(0);
}

// A square near (x, y) within `radius` rings that is not another
// faction's base (an air unit is never over one), or (x, y).
static void air_square(int x, int y, int radius, int faction, int *sx, int *sy) {
    near_square(x, y, radius, sx, sy);
    int owner = owner_with(tile(*sx, *sy), MAP_BASE);
    if (owner >= 0 && owner != faction) *sx = x, *sy = y;
}

// An enemy unit (often at war with the faction and seen), or one of the
// faction's own (often an aircraft of a design from `air`, or a
// transport, grounded), moved near (x, y).
static void air_bring(int faction, int own, int x, int y, int radius, const int *air, int n_air) {
    int u = pick_unit(own ? faction : -1);
    if (u < 0 || (own ? game_vehs[u].faction != faction : game_vehs[u].faction == faction)) return;
    int sx, sy;
    air_square(x, y, radius, game_vehs[u].faction, &sx, &sy);
    put(u, sx, sy);
    struct veh *U = &game_vehs[u];
    if (own) {
        struct unit_proto *Q = &game_protos[U->proto];
        if (rnd(2)) {
            Q->chassis = (uint8_t)air[rnd(n_air)];
            if (Q->plan == PLAN_AIR_SUPERIORITY || rnd(3) == 0) Q->plan = PLAN_COMBAT;
        } else if (rnd(3) == 0) {
            Q->plan = PLAN_NAVAL_TRANSPORT;
        }
        if (rnd(3)) U->moves_spent = (uint8_t)veh_speed(u, 0);
    } else {
        if (rnd(4)) game_factions[faction].diplo_status[U->faction] |= TREATY_VENDETTA;
        if (rnd(4)) U->visibility |= (uint8_t)(1u << faction);
        if (rnd(3) == 0) U->moves_spent = (uint8_t)veh_speed(u, 0);
    }
    if (rnd(4) == 0) U->order = rnd(2) ? ORDER_LOWER_LAND : ORDER_RAISE_LAND;
    if (rnd(4) == 0) U->visibility ^= (uint8_t)(1u << faction);
}

// Air units of the AI's of every kind (a design made an air one, a missile
// or a payload where needed) and plan, on alert (to bombard, intercept or
// patrol, with a post), exploring, with a go-to, aloft for turns,
// damaged, near their bases, enemy bases and the sea, with enemy units and
// the faction's own grounded aircraft around them, formers on the coast,
// raise-land goals, and treaties, regions' plans, air power and bases'
// state varied.
static void sweep_air_power(void) {
    int air[CHASSIS_COUNT], n_air = 0, missiles[CHASSIS_COUNT], n_missiles = 0;
    for (int c = 0; c < CHASSIS_COUNT; c++) {
        if (game_chassis[c].triad != TRIAD_AIR) continue;
        air[n_air++] = c;
        if (game_chassis[c].missile) missiles[n_missiles++] = c;
    }
    if (!n_air) return;
    static const int plans[] = {PLAN_OFFENSIVE, PLAN_OFFENSIVE, PLAN_OFFENSIVE, PLAN_AIR_SUPERIORITY, PLAN_AIR_SUPERIORITY,
                                PLAN_AIR_SUPERIORITY, PLAN_COMBAT, PLAN_DEFENSIVE, PLAN_RECON, PLAN_NAVAL_SUPERIORITY,
                                PLAN_COLONY, PLAN_TECTONIC, PLAN_FUNGAL};
    static const uint32_t treaties[] = {TREATY_VENDETTA, TREATY_VENDETTA, TREATY_PACT, TREATY_TREATY, TREATY_TRUCE};
    for (int k = 0; k < 64; k++) {
        // An air unit of the AI's where there is one.
        int v = -1;
        for (int j = 0; j < 24; j++) {
            int u = pick_unit(1 + rnd(FACTION_COUNT - 1));
            if (u < 0 || !game_vehs[u].faction) continue;
            v = u;
            if (triad_of(game_vehs[u].proto) == TRIAD_AIR) break;
        }
        if (v < 0) break;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        int f = V->faction;
        struct faction_data *F = &game_factions[f];
        struct unit_proto *P = &game_protos[V->proto];
        if (triad_of(V->proto) != TRIAD_AIR || rnd(4) == 0) P->chassis = (uint8_t)air[rnd(n_air)];
        if (n_missiles && k % 3 == 2) P->chassis = (uint8_t)missiles[rnd(n_missiles)];
        if (rnd(3)) P->plan = (uint8_t)plans[rnd(13)];
        if (game_chassis[P->chassis].missile && rnd(2)) P->plan = (uint8_t)(rnd(2) ? PLAN_TECTONIC : PLAN_FUNGAL);
        if (rnd(4) == 0) P->cost = (uint8_t)(1 + rnd(12));  // not 0: compute_odds divides by it
        *game_human_factions &= (uint8_t) ~(1u << f);
        if (rnd(12) == 0) *game_human_factions |= (uint8_t)(1u << f);
        // Where: near an enemy base, near or in one of its own, or anywhere.
        int x = V->x, y = V->y;
        if (*game_base_count) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            switch (k % 4) {
            case 0:
                for (int j = 0; j < 8 && B->faction == f; j++) B = &game_bases[rnd(*game_base_count)];
                air_square(B->x, B->y, 1 + rnd(8), f, &x, &y);
                F->diplo_status[B->faction] |= TREATY_VENDETTA;
                if (rnd(3) == 0) F->diplo_status[B->faction] &= ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE);
                break;
            case 1:
                air_square(B->x, B->y, rnd(3), f, &x, &y);
                break;
            case 2: {
                int sx = rnd(*game_map_width), sy = rnd(*game_map_height);
                if ((sx ^ sy) & 1) sx ^= 1;
                if (on_map(sx, sy)) air_square(sx, sy, 0, f, &x, &y);
                break;
            }
            }
        }
        int owner = owner_with(tile(x, y), MAP_BASE);
        if (owner < 0 || owner == f) put(v, x, y);
        x = V->x, y = V->y;
        for (int j = rnd(5); j > 0; j--) air_bring(f, 0, x, y, 1 + rnd(rnd(2) ? 2 : 8), air, n_air);
        for (int j = rnd(4); j > 0; j--) air_bring(f, 1, x, y, 1 + rnd(8), air, n_air);
        // Its state.
        V->moves_spent = (uint8_t)(rnd(3) ? 0 : rnd(12));
        if (rnd(4) == 0) {  // a short reach left
            int left = veh_speed(v, 0) - game_rules_basic->move_rate_roads * (1 + rnd(3));
            V->moves_spent = (uint8_t)(left > 0 ? left : 0);
        }
        V->movement_turns = (uint8_t)(rnd(3) ? 0 : rnd(4));
        V->damage = (uint8_t)(rnd(3) ? 0 : rnd(10));
        if (rnd(3) == 0) V->state ^= VEH_STATE_ALERT;
        if (rnd(6) == 0) V->state ^= VEH_STATE_EXPLORE;
        static const int autos[] = {ALERT_BOMBARD, ALERT_INTERCEPT, ALERT_PATROL, 0};
        V->order_auto_type = (uint8_t)(rnd(5) ? autos[rnd(4)] : rnd(16));
        V->order = ORDER_NONE;
        if (rnd(4) == 0) {
            V->order = ORDER_MOVE_TO;
            int wx = x, wy = y;
            if (rnd(3) == 0) near_square(x, y, 1 + rnd(4), &wx, &wy);
            V->waypoint_x[0] = (int16_t)wx, V->waypoint_y[0] = (int16_t)wy;
        }
        // Its post: an enemy base, a base of its own, or a square near it.
        int px = x, py = y;
        if (*game_base_count && rnd(3)) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            px = B->x, py = B->y;
        } else if (rnd(2)) {
            near_square(x, y, 1 + rnd(3), &px, &py);
        }
        V->waypoint_x[1] = (int16_t)px, V->waypoint_y[1] = (int16_t)py;
        // The faction.
        for (int j = rnd(4); j > 0; j--) {
            int g = 1 + rnd(FACTION_COUNT - 1);
            if (g == f) continue;
            uint32_t t = treaties[rnd(5)];
            F->diplo_status[g] ^= t;
            if (rnd(2)) game_factions[g].diplo_status[f] ^= t;
            if (rnd(4) == 0) game_factions[g].se_pending[SE_PLANET] = rnd(5) - 2;
            if (rnd(4) == 0) game_factions[g].ranking = rnd(8);
        }
        int region = tile(x, y)->region;
        if (rnd(3) == 0) F->region_base_plan[region] = (uint8_t)(rnd(3) ? rnd(9) : REGION_PLAN_COLONIZE);
        if (rnd(3) == 0) F->region_total_bases[region] = (uint8_t)rnd(3);
        for (int j = rnd(4); j > 0 && *game_base_count; j--) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            F->region_base_plan[tile(B->x, B->y)->region] = (uint8_t)rnd(4);
        }
        if (rnd(3) == 0) F->unk_84c = rnd(3) ? 0 : rnd(10), F->air_offense = rnd(20);
        if (rnd(3) == 0 && *game_base_count) F->attack_target_base = rnd(*game_base_count);
        if (rnd(10) == 0) F->flags ^= FACTION_FLAG_SEES_ALL;
        if (rnd(6) == 0) F->flags ^= FACTION_FLAG_UNK_80000;
        for (int j = rnd(4); j > 0; j--) {
            struct goal *G = &F->goals[rnd(GOAL_COUNT)];
            G->type = GOAL_RAISE_LAND, G->priority = (int16_t)rnd(20);
            near_square(x, y, 1 + rnd(8), &px, &py);
            G->x = px, G->y = py;
        }
        for (int j = rnd(4); j > 0 && *game_base_count; j--) {
            struct base *B = &game_bases[rnd(*game_base_count)];
            if (rnd(2)) B->event ^= 1u << f;
            if (rnd(2)) B->state ^= rnd(2) ? BASE_STATE_UNK_8 : BASE_STATE_UNK_100000;
            if (rnd(2)) B->pop_size = (int8_t)rnd(12);
            if (rnd(2)) B->visibility ^= (uint8_t)(1u << f);
        }
        vary_squares(x, y);
        if (rnd(4) == 0) tile(x, y)->bits ^= MAP_AIRBASE;
        // Now and then an attack aircraft that sees no enemy unit (and so
        // finds none to go for): it looks at bases to bombard.
        if (k % 8 == 3) {
            if (P->plan != PLAN_NAVAL_SUPERIORITY) P->plan = PLAN_OFFENSIVE;
            if (rnd(2)) V->state &= ~(uint32_t)VEH_STATE_ALERT;
            for (int u = 0; u < *game_veh_count; u++)
                if (game_vehs[u].faction != f) {
                    game_vehs[u].visibility &= (uint8_t) ~(1u << f);
                    if (game_vehs[u].order == ORDER_LOWER_LAND) game_vehs[u].order = ORDER_NONE;
                }
        }
        // The square it is asked about: its own, now and then one near.
        int sx = x, sy = y;
        if (rnd(8) == 0) air_square(x, y, 1, f, &sx, &sy);
        // Not over another's base (a pact partner's, its treaty varied):
        // the original divides by zero there for a short reach.
        int over = owner_with(tile(sx, sy), MAP_BASE);
        if (over < 0 || over == f) check_air_power(v, rnd(2), rnd(2), &sx, &sy);
        hooks_writes_abandon(&w);
    }
}

// A weapon a planet buster has, or -1.
static int pb_weapon_index(void) {
    for (int i = 0; i < WEAPON_COUNT; i++)
        if (game_weapons[i].offense >= 99) return i;
    return -1;
}

static void sweep_planet_buster(void) {
    int weapon = pb_weapon_index();
    for (int k = 0; k < 16 && *game_base_count > 0; k++) {
        int b = rnd(*game_base_count), other = game_bases[b].faction;
        int v = -1;
        for (int j = 0; j < 8 && v < 0; j++) {
            v = pick_unit(1 + rnd(FACTION_COUNT - 1));
            if (v >= 0 && (game_vehs[v].faction == other || !game_vehs[v].faction)) v = -1;
        }
        if (v < 0) continue;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        int f = V->faction;
        struct faction_data *F = &game_factions[f], *O = &game_factions[other];
        if (rnd(8)) *game_human_factions &= (uint8_t)~(1u << f);
        if (rnd(4)) *game_human_factions ^= (uint8_t)(rnd(2) << other);
        struct unit_proto *P = &game_protos[V->proto];
        if (rnd(5)) {
            P->plan = PLAN_PLANET_BUSTER;
            P->reactor = (uint8_t)(1 + rnd(3));
            if (weapon >= 0 && rnd(2)) P->weapon = (uint8_t)weapon;
        }
        if (rnd(4) == 0) P->cost = (uint8_t)rnd(20);
        uint32_t *st = &F->diplo_status[other];
        static const uint32_t treaty[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_WANT_REVENGE,
                                          TREATY_SHALL_BETRAY, TREATY_UNK_800, TREATY_UNK_80000, TREATY_VENDETTA};
        for (int j = 0; j < 8; j++)
            if (rnd(3) == 0) *st ^= treaty[j];
        if (rnd(3) == 0) *st &= ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE);
        if (rnd(4) == 0) F->flags ^= FACTION_FLAG_WANTON;
        if (rnd(4) == 0) *game_rules ^= RULES_INTENSE_RIVALRY;
        if (rnd(3) == 0) *game_un_charter_a = rnd(3), *game_un_charter_b = rnd(3);
        F->ai_fight = rnd(3) - 1;
        if (rnd(2)) F->ranking = rnd(8), O->ranking = rnd(8);
        if (rnd(2)) F->mil_strength = rnd(100), O->mil_strength = rnd(100);
        if (rnd(3) == 0) F->major_atrocities = rnd(3), O->major_atrocities = rnd(3);
        O->integrity_blemishes = rnd(8);
        O->diff_level = rnd(6);
        O->satellites[3] = rnd(3) ? 0 : rnd(4);
        O->odp_deployed = rnd(2);
        if (rnd(3) == 0) O->corner_market_turn = *game_turn + rnd(10) - 3;
        if (rnd(4) == 0) game_bases[b].queue[0] = BUILD_ASCENT;
        // Within reach of the target, or in a base of its own near it.
        int x, y;
        near_square(game_bases[b].x, game_bases[b].y, rnd(3) ? 1 + rnd(2) : 1 + rnd(8), &x, &y);
        if (rnd(3)) put(v, x, y);
        V->moves_spent = (uint8_t)rnd(4);
        // Often at the edge of what the target is worth to it: its cost
        // times the AI's rank near the blast's value.
        if (rnd(2) && F->ranking > 0) {
            int value = pb_value(f, b, game_radius_count[planet_buster(v)]);
            if (value > 0) P->cost = (uint8_t)game_range(value / F->ranking + rnd(3) - 1, 0, 255);
        }
        check_enemy_planet_buster(v);
        hooks_writes_abandon(&w);
    }
}

// A unit's move, with enemy_move and the steps held.
static void sweep_enemy_veh(void) {
    holding_moves = 1;
    move_hold_steps(1);
    for (int k = 0; k < 24; k++) {
        int v = pick_unit(k % 4 ? 1 + rnd(FACTION_COUNT - 1) : -1);
        if (v < 0) break;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        V->moves_spent = (uint8_t)(rnd(3) ? 0 : rnd(12));
        switch (rnd(4)) {
        case 0: {
            V->order = ORDER_MOVE_TO;
            int x, y;
            near_square(V->x, V->y, 1, &x, &y);
            V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
            if (!V->moves_spent) V->moves_spent = 1;
            break;
        }
        case 1: V->order = ORDER_NONE; break;
        case 2: V->order = (uint8_t)(4 + rnd(8)); break;
        }
        if (rnd(4) == 0) V->state ^= VEH_STATE_EXPLORE;
        if (rnd(4) == 0) *game_current_faction = V->faction;
        hold_seed = rnd32();
        check_enemy_veh(v);
        hooks_writes_abandon(&w);
    }
    holding_moves = 0;
    move_hold_steps(0);
}

// A faction's turn around its units, with enemy_move and the steps held:
// most units start with all their moves.
static void sweep_enemy_turn(void) {
    holding_moves = 1;
    move_hold_steps(1);
    for (int k = 0; k < 2; k++) {
        int faction = k ? rnd(FACTION_COUNT) : (int)((uint32_t)*game_turn % FACTION_COUNT);
        struct hook_writes w;
        case_begin(&w);
        for (int v = 0; v < *game_veh_count; v++) {
            struct veh *V = &game_vehs[v];
            if (V->faction != faction) continue;
            if (rnd(6)) V->moves_spent = 0;
            if (rnd(8) == 0) V->damage = (uint8_t)rnd(5);
        }
        hold_seed = rnd32();
        check_enemy_turn(faction);
        hooks_writes_abandon(&w);
    }
    holding_moves = 0;
    move_hold_steps(0);
}

// A prototype for a unit of the faction on its square (land or sea), of
// a kind enemy_move handles and of the plan wanted (or any, -1): a basic
// one or the faction's own; or -1.
static int pick_proto(int faction, int sea, int plan) {
    for (int k = 0; k < 64; k++) {
        int p = rnd(2) ? rnd(BASIC_PROTOS) : faction * BASIC_PROTOS + rnd(BASIC_PROTOS);
        const struct unit_proto *P = proto_of(p);
        if (!P->name[0] || P->plan == PLAN_NAVAL_TRANSPORT || triad_of(p) == TRIAD_AIR) continue;
        if ((triad_of(p) == TRIAD_SEA) != sea || (plan >= 0 && P->plan != plan)) continue;
        return p;
    }
    return -1;
}

// Whether a unit of a faction without a pact with the unit's shares its
// square, which the game never allows but the cases below can make (units
// put down, treaties changed). enemy_move would weigh an attack on its own
// square (direction -1 of the near table, which reads as no move), and the
// defender's blow back lands on a neighbouring square, perhaps empty:
// battle_fight then reads the unit before the first as the defender.
static int rival_on_square(int v) {
    const struct veh *V = &game_vehs[v];
    for (int u = 0; u < *game_veh_count; u++) {
        const struct veh *U = &game_vehs[u];
        if (U->x == V->x && U->y == V->y && U->faction != V->faction &&
            !(game_factions[V->faction].diplo_status[U->faction] & TREATY_PACT))
            return 1;
    }
    return 0;
}

// The unit's move (now and then forced), with what lies where the
// original's locals go varied, unless it would carry a move out.
static unsigned em_swept, em_swept_risky;
static void em_sweep_one(int v) {
    int veh = rnd(10) ? v : -1 - v;
    // What lies where the original's locals go: nothing, small
    // leftovers, a stack's leftovers (return addresses and pointers
    // into Chiron's image, 0x08xxxxxx, among small words), or what lay
    // there in one of the game's recent calls. The words the original
    // reads unset are kept small enough to index the game's tables
    // (the fungus search's word, a flag, anything).
    int32_t frame[FRAME_WORDS];
    int fill = rnd(4);
    for (int i = 0; i < FRAME_WORDS; i++)
        frame[i] = !fill ? 0 : fill == 1 || rnd(2) ? rnd(64) - 8 : (int32_t)(0x08000000u | (rnd32() & 0x1ffffcu));
    if (fill == 3 && em_frames_kept)
        memcpy(frame, em_frames[rnd(em_frames_kept < EM_FRAMES ? (int)em_frames_kept : EM_FRAMES)], sizeof frame);
    static const int unset[] = {0x4, 0x24, 0x48, 0x70, 0x78};
    if (fill) for (int i = 0; i < 5; i++) FRAME_AT(frame, unset[i]) = rnd(64) - 8;
    FRAME_AT(frame, 0x38) = rnd(3) ? 0 : rnd(3) ? 1 : (int32_t)rnd32();
    em_swept++;
    probe_enemy_move(veh, frame);
    if (em_risky) em_swept_risky++;
    else check_enemy_move(veh, frame);
}

// A unit's move, of AI units of every kind and the player's automated
// ones, with their orders, state, damage and surroundings varied, and the
// faction's plans for the continent; with the step held. Moves carried
// out (see probe_enemy_move) are left out.
static void sweep_enemy_move(void) {
    static const uint8_t icons[] = {0, 'F', 'd', 'X', '/', '6', '-', 'p', '3', '4', '5', 'b', 'R'};
    static const uint32_t states[] = {0x200, 0x4000, 0x40000, 0x60000, 0x10000, 0x10000000, 0x40000000, 0x100, 0x8,
                                      0x20000000, 0x1000200, 0x2000200, 0x80000000u, 0x20};
    move_hold_steps(1);
    for (int k = 0; k < 48; k++) {
        int native = k % 16 == 15;  // now and then native life, through alien_move
        int v = pick_unit(native ? 0 : 1 + rnd(FACTION_COUNT - 1));
        if (v < 0) break;
        struct veh *V = &game_vehs[v];
        int f = V->faction;
        if ((f == 0) != native) continue;
        struct hook_writes w;
        case_begin(&w);
        struct faction_data *F = &game_factions[f];
        // Its kind, and where it is.
        if (rnd(3) && *game_base_count) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            int x, y;
            near_square(B->x, B->y, rnd(4), &x, &y);
            if (!occupied(tile(x, y), MAP_BASE) || tile(x, y)->owner_site % 16 == f) put(v, x, y);
        }
        // Each kind in turn: formers (automated ones too), convoys,
        // artifacts, probe teams, colony pods, scouts, combat units,
        // defenders, warships; or any.
        static const int kinds[] = {9, 9, 9, 10, 12, 11, 8, 8, 3, 1, 2, 6, 0, -1};
        int kind = kinds[k % 14];
        int sea = alt_of(tile(V->x, V->y)) < ALT_SHORE;
        if (sea && kind >= 0 && kind != PLAN_NAVAL_SUPERIORITY && *game_base_count) {
            const struct base *B = &game_bases[rnd(*game_base_count)];
            for (int j = 0; j < 8; j++) {
                int x, y;
                near_square(B->x, B->y, 1 + rnd(3), &x, &y);
                if (alt_of(tile(x, y)) >= ALT_SHORE && !occupied(tile(x, y), MAP_BASE)) {
                    put(v, x, y);
                    break;
                }
            }
            sea = alt_of(tile(V->x, V->y)) < ALT_SHORE;
        }
        int p = pick_proto(f, sea, kind);
        if (p < 0 && rnd(4)) p = pick_proto(f, sea, -1);
        if (rnd(4) == 0 && *game_base_count) {
            // A ship (a sea colony pod or former among them) in one of its
            // faction's bases.
            const struct base *B = &game_bases[rnd(*game_base_count)];
            int q = pick_proto(f, 1, kind);
            if (B->faction == f && q >= 0) put(v, B->x, B->y), p = q;
        }
        if (p >= 0) V->proto = (int16_t)p;
        // Its orders and state.
        switch (rnd(7)) {
        case 0: V->order = ORDER_NONE; break;
        case 1: V->order = 2; break;
        case 2: V->order = 3, V->order_auto_type = (uint8_t)rnd(4); break;
        case 3: V->order = (uint8_t)(4 + rnd(0x14)); break;
        case 4: V->order = (uint8_t)(0x1b + rnd(2)); break;
        default: {
            V->order = ORDER_MOVE_TO;
            int x, y;
            near_square(V->x, V->y, 1 + rnd(3), &x, &y);
            V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
        }
        }
        for (int j = rnd(4); j > 0; j--) V->state ^= states[rnd(14)];
        if (V->state & 0x200) V->order_auto_type = (uint8_t)rnd(12);
        int plan = proto_of(V->proto)->plan;
        if (plan == PLAN_TERRAFORMING && rnd(2)) {
            static const uint8_t autos[] = {1, 2, 3, 4, 5, 6, 7};
            V->state |= 0x200, V->order_auto_type = autos[rnd(7)];
            if (rnd(2)) V->order = ORDER_NONE;
        }
        if (plan == PLAN_SUPPLY_CONVOY && rnd(2)) V->order = rnd(2) ? 3 : ORDER_NONE, V->order_auto_type = (uint8_t)rnd(3);
        if ((plan == PLAN_ALIEN_ARTIFACT || plan == PLAN_SUPPLY_CONVOY) && rnd(2) && *game_base_count) {
            int b = rnd(*game_base_count);
            if (game_bases[b].faction == f) put(v, game_bases[b].x, game_bases[b].y);
            if (rnd(2)) game_bases[b].queue[0] = -(70 + rnd(64));
            if (rnd(2)) game_bases[b].minerals = rnd(200);
        }
        if ((plan == PLAN_RECONNAISSANCE || plan <= 1) && rnd(2)) V->state ^= 0x4000;
        if (rnd(4) == 0) V->order = ORDER_NONE;
        if ((V->state & 0x3000000) && rnd(2)) {
            V->waypoint_count = (uint8_t)rnd(4), V->patrol_point = (uint8_t)rnd(4);
            for (int j = 0; j < 4; j++) {
                int x, y;
                near_square(V->x, V->y, 1 + rnd(3), &x, &y);
                V->waypoint_x[j] = (int16_t)x, V->waypoint_y[j] = (int16_t)y;
            }
        }
        if (rnd(5) == 0) V->flags ^= 0x800;
        if (rnd(8) == 0) V->flags ^= 0x20;
        if (rnd(10) == 0) V->flags ^= 0x440;
        V->moves_spent = (uint8_t)(rnd(3) ? 0 : rnd(10));
        // Short of its hit points: with none left it would be dead.
        V->damage = (uint8_t)(rnd(3) || plan == PLAN_ALIEN_ARTIFACT
                                  ? 0
                                  : rnd(game_range(proto_of(V->proto)->reactor, 1, 100) * 10));
        V->status_icon = icons[rnd(13)];
        if (rnd(4) == 0) V->home_base = (int16_t)(rnd(5) ? rnd(*game_base_count + 1) - 1 : -1);
        V->movement_turns = (uint8_t)rnd(8);
        V->unk_29 = (uint8_t)(rnd(9) - 1);
        // Its faction's plans and its neighbours'.
        int region = tile(V->x, V->y)->region;
        if (rnd(3) == 0) F->region_base_plan[region] = (uint8_t)rnd(9);
        if (rnd(4) == 0) F->region_flags[region] ^= (uint16_t)(1u << rnd(16));
        if (rnd(4) == 0) F->region_total_bases[region] = (uint8_t)rnd(4);
        if (rnd(5) == 0) F->target_x = V->x, F->target_y = V->y;
        if (rnd(4) == 0) F->flags ^= rnd(2) ? 0x20000000u : rnd(2) ? 0x2000000u : 0x10000u;
        if (rnd(4) == 0) *game_more_preferences ^= rnd(2) ? 8 : rnd(2) ? 0x20 : 0x400000;
        if (rnd(6) == 0) *game_human_factions ^= (uint8_t)(1u << f);
        if (rnd(4) == 0) *game_current_turn = rnd(300);
        if (rnd(5) == 0) *game_difficulty = rnd(6);
        if (rnd(3) == 0) {
            int o = 1 + rnd(FACTION_COUNT - 1);
            static const uint32_t treaty[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_VENDETTA, 0x20, 0x800};
            if (o != f) F->diplo_status[o] ^= treaty[rnd(6)];
        }
        if (rnd(3) == 0) {
            // Another faction's unit next to it.
            int u = pick_unit(-1), x, y;
            near_square(V->x, V->y, 1 + rnd(2), &x, &y);
            if (u >= 0 && u != v && (x != V->x || y != V->y) && !occupied(tile(x, y), MAP_BASE)) {
                put(u, x, y);
                // Now and then a faction's mind worms, at war with it.
                struct veh *U = &game_vehs[u];
                if (rnd(3) == 0 && U->faction && U->faction != f && alt_of(tile(x, y)) >= ALT_SHORE) {
                    U->proto = 8;
                    U->order = rnd(2) ? ORDER_MOVE_TO : ORDER_NONE;
                    F->diplo_status[U->faction] = (F->diplo_status[U->faction] | TREATY_VENDETTA) &
                                                  ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE);
                }
            }
        }
        vary_squares(V->x, V->y);
        if (rival_on_square(v)) {
            hooks_writes_abandon(&w);
            continue;
        }
        em_sweep_one(v);
        hooks_writes_abandon(&w);
    }
    move_hold_steps(0);
}

// A transport design of the faction's or a basic one; or -1.
static int pick_transport(int faction) {
    for (int k = 0; k < 64; k++) {
        int p = rnd(2) ? rnd(BASIC_PROTOS) : faction * BASIC_PROTOS + rnd(BASIC_PROTOS);
        const struct unit_proto *P = proto_of(p);
        if (P->name[0] && P->plan == PLAN_NAVAL_TRANSPORT && triad_of(p) == TRIAD_SEA) return p;
    }
    return -1;
}

// A sea square within `radius` rings of (x, y) (`open`: away from any
// coast), or -1 in *sx.
static void sea_near(int x, int y, int radius, int open, int *sx, int *sy) {
    *sx = -1;
    for (int j = 0; j < 16; j++) {
        int nx, ny, coast = 0;
        near_square(x, y, radius, &nx, &ny);
        if (alt_of(tile(nx, ny)) >= ALT_SHORE || occupied(tile(nx, ny), MAP_BASE)) continue;
        for (int i = 0; i < 8 && open; i++) {
            int ax = xrange(nx + game_radius_near_x[i]), ay = ny + game_radius_near_y[i];
            if (on_map(ax, ay) && alt_of(tile(ax, ay)) >= ALT_SHORE) coast = 1;
        }
        if (coast) continue;
        *sx = nx, *sy = ny;
        return;
    }
}

// Naval transports' moves: a transport of a faction's at sea (next to a
// coast, its own or another's, or out at sea) or in one of its ports,
// carrying land units of its faction (sentried aboard for it or for none,
// holding or idle; of every plan, some hurt, besieging or garrisons, from
// bases on this continent or others) up to its capacity, or none; with
// escorts and other factions' units near, and the faction's target,
// attack target, flags, plans for continents, treaties and bases' plans
// varied; as enemy_move's sweep does for other units.
static void sweep_transport(void) {
    static const uint8_t icons[] = {0, 'e', 'E', 'r', 'I', 'J', 'a', 'd', '-', 'u', 'c', '6'};
    static const int plans[] = {0, 1, 2, 3, 8, 9, 10, 11, 12, -1}, settlers[] = {8, 2, 3, 11, 8};
    move_hold_steps(1);
    for (int k = 0; k < 48 && *game_base_count > 0; k++) {
        int f = 1 + rnd(FACTION_COUNT - 1);
        int v = pick_unit(f), p = pick_transport(f);
        if (v < 0 || p < 0 || game_vehs[v].faction != f) continue;
        struct hook_writes w;
        case_begin(&w);
        struct veh *V = &game_vehs[v];
        struct faction_data *F = &game_factions[f];
        V->proto = (int16_t)p;
        // Where: in a port of its own, or at sea near a base (any
        // faction's) or out from one, or near another unit.
        const struct base *B = &game_bases[rnd(*game_base_count)];
        int x = -1, y = -1, where = rnd(6);
        if (!where) {
            for (int j = 0; j < 8 && x < 0; j++, B = &game_bases[rnd(*game_base_count)])
                if (B->faction == f && is_port((int)(B - game_bases), 0)) x = B->x, y = B->y;
        } else if (where < 3) {
            sea_near(B->x, B->y, where, 0, &x, &y);
        } else if (where < 5) {
            // Out at sea, mostly off a port of its own.
            for (int j = 0; j < 8 && B->faction != f && rnd(4); j++) B = &game_bases[rnd(*game_base_count)];
            sea_near(B->x, B->y, 2 + rnd(6), 1, &x, &y);
        } else {
            int u = pick_unit(-1);
            if (u >= 0) sea_near(game_vehs[u].x, game_vehs[u].y, 1 + rnd(2), 0, &x, &y);
        }
        if (x < 0) {
            hooks_writes_abandon(&w);
            continue;
        }
        put(v, x, y);
        int sea = alt_of(tile(x, y)) < ALT_SHORE;
        // Its passengers.
        // Now and then only colony pods (with defenders, scouts or probe
        // teams), or one kind of unit.
        int room = veh_cargo(v), n = rnd(3) ? rnd(room + 1) : 0, mix = rnd(5), one = plans[rnd(10)];
        if (!mix && n < 2 && room >= 2) n = 2;
        for (int j = 0; j < n; j++) {
            int u = pick_unit(f);
            int q = pick_proto(f, 0, !mix ? settlers[j % 5] : mix == 1 ? one : mix == 2 ? PLAN_COLONIZATION : plans[rnd(10)]);
            if (u < 0 || u == v || q < 0 || game_vehs[u].faction != f || veh_cargo(u)) continue;
            struct veh *U = &game_vehs[u];
            U->proto = (int16_t)q;
            put(u, x, y);
            int r = rnd(8);
            U->order = (uint8_t)(sea ? (r < 5 ? 1 : r < 7 ? 0 : 2) : (r < 3 ? 1 : r < 6 ? 2 : 0));
            if (U->order == 1) U->waypoint_x[0] = (int16_t)(rnd(4) ? v : -1), U->waypoint_y[0] = 0;
            U->damage = (uint8_t)(rnd(3) || proto_of(q)->plan == PLAN_ALIEN_ARTIFACT
                                      ? 0
                                      : rnd(game_range(proto_of(q)->reactor, 1, 100) * 10));
            U->state &= ~0x10060000u;
            if (rnd(6) == 0) U->state |= rnd(2) ? 0x40000 : 0x60000;
            if (rnd(6) == 0) U->state |= 0x10000000;
            int h = rnd(*game_base_count);
            U->home_base = (int16_t)(rnd(5) && game_bases[h].faction == f ? h : rnd(4) ? U->home_base : -1);
        }
        // Its orders and state.
        V->order = ORDER_NONE;
        if (rnd(3) == 0) {
            const struct base *T = &game_bases[rnd(*game_base_count)];
            V->order = ORDER_MOVE_TO;
            V->waypoint_x[0] = T->x, V->waypoint_y[0] = T->y;
            if (rnd(3) == 0) V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
        } else if (rnd(4) == 0) {
            V->order = 2;
        }
        V->status_icon = icons[rnd(12)];
        V->state &= ~0x20010000u;
        if (rnd(5) == 0) V->state |= 0x20000000;
        if (rnd(4) == 0) V->state |= 0x10000;
        V->moves_spent = (uint8_t)(rnd(3) ? 0 : rnd(10));
        V->damage = (uint8_t)(rnd(3) ? 0 : rnd(game_range(proto_of(p)->reactor, 1, 100) * 10));
        if (rnd(4) == 0) V->home_base = (int16_t)(rnd(*game_base_count + 1) - 1);
        // An escort with it, now and then; others near.
        if (rnd(4) == 0) {
            int u = pick_unit(f), q = pick_proto(f, 1, PLAN_NAVAL_SUPERIORITY);
            if (u >= 0 && u != v && q >= 0 && game_vehs[u].faction == f && !veh_cargo(u) && sea)
                game_vehs[u].proto = (int16_t)q, put(u, x, y);
        }
        for (int j = rnd(3); j > 0; j--) {
            int u = pick_unit(-1), nx, ny;
            near_square(x, y, 1 + rnd(2), &nx, &ny);
            if (u >= 0 && u != v && (nx != x || ny != y) && !occupied(tile(nx, ny), MAP_BASE) && !veh_cargo(u) &&
                triad_of(game_vehs[u].proto) != TRIAD_AIR &&
                (alt_of(tile(nx, ny)) < ALT_SHORE) == (triad_of(game_vehs[u].proto) == TRIAD_SEA))
                put(u, nx, ny);
        }
        // The faction's plans: the continents near, its target and attack
        // target, its flags; treaties; bases' plans.
        for (int j = rnd(4); j > 0; j--) {
            int nx, ny;
            near_square(x, y, 1 + rnd(3), &nx, &ny);
            int r = tile(nx, ny)->region;
            switch (rnd(4)) {
            case 0: F->region_base_plan[r] = (uint8_t)rnd(9); break;
            case 1: F->region_flags[r] ^= (uint16_t)(rnd(2) ? 3 : rnd(2) ? 0x800 : 0x1800); break;
            case 2: F->region_total_combat_units[r] = (uint16_t)rnd(12); break;
            default: F->region_total_bases[r] = (uint8_t)rnd(4); break;
            }
        }
        if (rnd(3) == 0) F->region_base_plan[tile(x, y)->region] = (uint8_t)rnd(9);
        for (int j = rnd(3); j > 0; j--) {
            // Its ports' continents: plans its passengers may serve.
            const struct base *P = &game_bases[rnd(*game_base_count)];
            if (P->faction == f) F->region_base_plan[tile(P->x, P->y)->region] = (uint8_t)rnd(4);
        }
        if (rnd(4) == 0) {
            const struct base *T = &game_bases[rnd(*game_base_count)];
            F->target_x = rnd(5) ? T->x : -1, F->target_y = T->y;
        }
        if (rnd(4) == 0) F->attack_target_base = rnd(*game_base_count + 1) - 1;
        if (rnd(3) == 0) {
            static const uint32_t flags[] = {0x800, 0x200000, 0x80000000u, 0x200, 0x20000000};
            F->flags ^= flags[rnd(5)];
        }
        if (rnd(3) == 0) {
            int o = 1 + rnd(FACTION_COUNT - 1);
            static const uint32_t treaty[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_VENDETTA, 0x20, 0x40,
                                              0x400, 0x800, 0x1000, 0x8};
            if (o != f) F->diplo_status[o] ^= treaty[rnd(10)];
            if (o != f && rnd(3) == 0)
                F->diplo_status[o] = (F->diplo_status[o] | TREATY_VENDETTA) & ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE);
            if (rnd(2)) game_factions[o].diplo_spoke[f] = *game_current_turn - rnd(30);
        }
        if (rnd(4) == 0) game_bases[rnd(*game_base_count)].ai_plan = (uint8_t)rnd(3);
        int scene = rnd(4);
        if (scene) {
            // No continent of its own to defend (nowhere to reinforce);
            // at peace (nowhere to invade: its ports elsewhere, or with
            // its continents planned 7 not even those, and empty coasts
            // left), or at war with all.
            int seven = scene == 2, war = scene == 3;
            for (int o = 1; o < FACTION_COUNT; o++)
                if (o != f)
                    F->diplo_status[o] = war ? (F->diplo_status[o] | TREATY_VENDETTA) &
                                                   ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)
                                             : (F->diplo_status[o] | TREATY_TREATY) & ~(uint32_t)TREATY_VENDETTA;
            for (int b = 0; b < *game_base_count; b++) {
                static const uint8_t calm[] = {0, 1, 3, 0, 1, 8};
                if (game_bases[b].faction == f)
                    F->region_base_plan[tile(game_bases[b].x, game_bases[b].y)->region] = seven ? 7 : calm[rnd(6)];
            }
        }
        if (rnd(8) == 0) game_bases[rnd(*game_base_count)].queue[0] = BUILD_ASCENT;
        if (rnd(6) == 0) *game_human_factions ^= (uint8_t)(1u << f);
        if (rnd(4) == 0) *game_current_turn = rnd(300);
        if (rival_on_square(v)) {
            hooks_writes_abandon(&w);
            continue;
        }
        em_sweep_one(v);
        hooks_writes_abandon(&w);
    }
    move_hold_steps(0);
}

// Once a turn, on a protected copy of the game: the bases native life
// heads for, its moves (natives of every kind, transports with worms
// aboard, spore launchers among improvements, near humans' bases), air
// units' moves, planet busters near their targets, units' moves and
// factions' turns, and enemy_move's choices for units of every kind.
static void ai_sweep(void) {
    if (!*game_map || !game_path->x || !game_path->map || *game_veh_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    sweep_seed = (uint32_t)*game_turn * 7919u + 0x5eedu;
    struct hook_writes outer = {.protect_only = 1};
    writes_paths(&outer, game_path);
    hooks_writes_begin(&outer);
    sweep_alien_base();
    sweep_alien_move();
    sweep_air_power();
    sweep_planet_buster();
    sweep_enemy_veh();
    sweep_enemy_turn();
    sweep_enemy_move();
    sweep_transport();
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
    if (*game_turn % 25 == 0)
        fprintf(stderr, "chiron: alien_move: %llu of %llu game calls run unchecked (fights, attacks on squares)\n",
                alien_move_unchecked, stats_alien_move.calls);
    if (*game_turn % 25 == 0)
        fprintf(stderr,
                "chiron: enemy_move: %llu of %llu game calls run unchecked (moves carried out); swept %u, %u carried out\n",
                enemy_move_unchecked, stats_enemy_move.calls, em_swept, em_swept_risky);
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook ai_hooks[] = {
    HOOK(0x579510, enemy_turn, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),            // sub esp, 8
    HOOK(0x579240, enemy_veh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x58),             // sub esp, 0x58
    HOOK(0x5665d0, alien_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),            // sub esp, 16
    HOOK(0x566810, alien_move, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),            // sub esp, 0x2c
    HOOK(0x56a8a0, enemy_planet_buster, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x54),   // sub esp, 0x54
    HOOK(0x5678c0, air_power, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x48),             // sub esp, 0x48
    HOOK(0x56b5b0, enemy_move, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0xc4, 0x02, 0x00, 0x00),  // sub esp, 0x2c4
};
struct hook_module ai_module = {"ai", ai_hooks, HOOK_COUNT(ai_hooks), ai_sweep, NULL, -1};
