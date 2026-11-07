// The AI's turn around its units (each unit given its moves, kind by kind,
// through the game's enemy_move), native life's moves and the base it
// heads for, and the AI's placing of planet busters. Reimplemented from
// terranx.exe 2.0's disassembly; names follow Thinker (MIT) and OpenSMACX
// (Brendan Casey, GPL-3.0-or-later).
//
// The AI's choice of a move for one of its own units, enemy_move(), is 56
// KB of the game's code and is kept: enemy_veh() calls it through a
// pointer.
#include "ai.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "move.h"
#include "path.h"
#include "shadow.h"
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

// A unit's move and the faction's turn around its units run the game's
// enemy_move(), with all it sets off (fights, steps, messages): the game's
// calls run the original, and the sweep checks them with enemy_move held
// (shadow_enemy_move, below) and the steps held.
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
// unit is killed.
static int holding_moves __attribute__((used));
static uint32_t hold_seed;
static int (*orig_enemy_move)(int veh) __attribute__((used));
static struct hook_stats stats_enemy_move = {"(held enemy_move)"};
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
            return (h >> 6) & 1;
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
void shadow_enemy_move(void);
__asm__(".text\n"
        "shadow_enemy_move:\n"
        "    cmpl $0, holding_moves\n"
        "    jne held_enemy_move\n"
        "    jmp *orig_enemy_move\n");

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

// Once a turn, on a protected copy of the game: the bases native life
// heads for, its moves (natives of every kind, transports with worms
// aboard, spore launchers among improvements, near humans' bases), planet
// busters near their targets, units' moves and factions' turns.
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
    sweep_planet_buster();
    sweep_enemy_veh();
    sweep_enemy_turn();
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
    if (*game_turn % 25 == 0)
        fprintf(stderr, "chiron: alien_move: %llu of %llu game calls run unchecked (fights, attacks on squares)\n",
                alien_move_unchecked, stats_alien_move.calls);
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
#define HELD_HOOK(addr, name, ...)                                                                      \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), NULL, (void *)shadow_##name, \
     (void **)&orig_##name, &stats_##name, 1}
static const struct hook ai_hooks[] = {
    HOOK(0x579510, enemy_turn, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),            // sub esp, 8
    HOOK(0x579240, enemy_veh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x58),             // sub esp, 0x58
    HOOK(0x5665d0, alien_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),            // sub esp, 16
    HOOK(0x566810, alien_move, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),            // sub esp, 0x2c
    HOOK(0x56a8a0, enemy_planet_buster, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x54),   // sub esp, 0x54
    HELD_HOOK(0x56b5b0, enemy_move, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0xc4, 0x02, 0x00, 0x00),  // sub esp, 0x2c4
};
struct hook_module ai_module = {"ai", ai_hooks, HOOK_COUNT(ai_hooks), ai_sweep, NULL, -1};
