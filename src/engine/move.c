// Units on the move: the orders that send a unit somewhere (go to a square,
// build a road or tube along the way, return home, patrol) and the AI's
// courses for its transports; and units' other actions: formers' work and
// the AI's choice of it, founding bases, attacks on squares, missiles,
// nerve stapling, psi gates. Reimplemented from terranx.exe 2.0's
// disassembly; names follow Thinker (MIT) and OpenSMACX (Brendan Casey,
// GPL-3.0-or-later).
//
// The step itself is order_veh(), which moves the unit with all that may
// happen on the way (fights, supply pods, monoliths). Not here is
// veh_scoot(), which the game calls from it to show a unit sliding into the
// next square: it is all animation (the sound, the squares' pictures, a
// timed loop of drawing) and leaves nothing behind but the unit's facing,
// so the game's own is kept.
#include "move.h"

#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "combat.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "path.h"
#include "score.h"
#include "shadow.h"
#include "spot.h"
#include "strategy.h"
#include "tech.h"
#include "veh.h"

// The originals hand the network daemon the address of their argument, so
// that it follows the unit if its number changes; ours do the same.
#pragma GCC diagnostic ignored "-Wdangling-pointer"

enum {
    ALT_SHORE = 3 << 5,  // climate's altitude bits from the shore line up
    MAP_AIRBASE = 0x40000, MAP_MAGTUBE = 0x8,
    ORDER_NONE = 0, ORDER_MOVE_TO = 0x18, ORDER_MOVE = 0x19, ORDER_ROAD_TO = 0x1b, ORDER_MAGTUBE_TO = 0x1c,
    ORDER_ROAD = 9, ORDER_REMOVE_FUNGUS = 14, ORDER_AI = 0x40,  // ORDER_AI: or'ed into an order the AI gave
    TERRAFORM_ROAD = 5, TERRAFORM_MAG_TUBE = 6, TERRAFORM_REMOVE_FUNGUS = 10,
    VEH_STATE_UNK_200 = 0x200, VEH_STATE_PATROL = 0x1000200, VEH_STATE_PATROL_2 = 0x2000200,
    VEH_STATE_UNK_10000 = 0x10000, VEH_STATE_HOME_CLEAR = 0x3004200,
    ORDER_AUTO_ALERT = 0xb,  // order_auto_type: on alert, waypoint 1 its post
    PLAN_ARTIFACT = 12,
    ABL_CARRIER = 0x80, BASE_STATE_UNK_200000 = 0x200000,
    GOAL_ESCORT = 8, NET_MESSAGE_ORDER = 0x2405,
};

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int alt_of(const struct map_tile *t) { return t->climate & 0xe0; }
static inline int owner_of(const struct map_tile *t) { return t->owner_site & 0xf; }
// A base or unit of a faction (not native life) on the square.
static inline int faction_on(const struct map_tile *t, uint32_t bits) { return (t->bits & bits) && owner_of(t) < 8; }
static inline const struct unit_proto *proto_of(const struct veh *V) { return &game_protos[V->proto]; }
static inline const struct rules_chassis *chassis_of(const struct veh *V) {
    return &game_chassis[proto_of(V)->chassis];
}
static inline int16_t *waypoint_x(struct veh *V) { return (int16_t *)((char *)V + 0x14); }
static inline int16_t *waypoint_y(struct veh *V) { return (int16_t *)((char *)V + 0x1c); }
static inline int net_turn(void) { return *game_net_multiplayer && !*game_control_turn; }
// Distance in squares, as the game's movement code counts it.
static int range_to(int x1, int y1, int x2, int y2) { return (x_dist(x1, x2) + abs(y1 - y2)) >> 1; }
static int moves_left(int veh) {
    int m = veh_speed(veh, 0) - game_vehs[veh].moves_spent;
    return m < 0 ? 0 : m > 999 ? 999 : m;
}

// ---- The AI's courses ----------------------------------------------------------

// Sets a unit off for (x, y), with the icon given. A unit at sea headed for
// its own sea goes straight there. Otherwise, as for a transport taking
// units to land, it heads for the sea square within two of the goal that
// touches the most squares of the goal's land without a base (any base; the
// original doesn't look at whose), out of those its base's port reaches (if
// it is in one) or else on its own sea; a goal on land wants at least one
// such square, a goal at sea none. With no such square the unit's orders
// stay as they were. A colony pod's count of turns spent moving starts
// again.
void set_course(int veh, int icon, int x, int y) {
    struct veh *V = &game_vehs[veh];
    const struct map_tile *here = tile(V->x, V->y);
    int region = here->region, base = base_at(V->x, V->y);
    const struct map_tile *goal = tile(x, y);
    int goal_region = goal->region;
    if (region == goal_region && alt_of(tile(V->x, V->y)) < ALT_SHORE) {
        V->order = ORDER_MOVE_TO;
        V->status_icon = (uint8_t)icon;
        waypoint_x(V)[0] = (int16_t)x, waypoint_y(V)[0] = (int16_t)y;
        if (proto_of(V)->plan == PLAN_COLONY) V->movement_turns = 0;
        return;
    }
    int best = (alt_of(goal) >= ALT_SHORE) - 1;
    for (int i = 0; i < 21; i++) {
        int sx = xrange(x + game_radius_x[i]), sy = y + game_radius_y[i];
        if (!on_map(sx, sy)) continue;
        const struct map_tile *t = tile(sx, sy);
        if (alt_of(t) >= ALT_SHORE) continue;
        if (base >= 0 ? !base_on_sea(base, t->region) : t->region != region) continue;
        int count = 0;
        for (int k = 0; k < 8; k++) {
            int nx = xrange(sx + game_radius_near_x[k]), ny = sy + game_radius_near_y[k];
            if (!on_map(nx, ny)) continue;
            const struct map_tile *n = tile(nx, ny);
            if (alt_of(n) < ALT_SHORE || n->region != goal_region) continue;
            if (!faction_on(n, MAP_BASE)) count++;
        }
        if (count > best) {
            best = count;
            V->order = ORDER_MOVE_TO;
            V->status_icon = (uint8_t)icon;
            waypoint_x(V)[0] = (int16_t)sx, waypoint_y(V)[0] = (int16_t)sy;
            if (proto_of(V)->plan == PLAN_COLONY) V->movement_turns = 0;
        }
    }
}

// A base waiting for a transport: it is marked for one (state bit 1, plan
// 2), and the faction's goals get an escort (for a base wanting troops,
// type 8, only where the faction's plans for the region call for it, bit
// 0x800) while fewer enemies than the faction's growth over fight, less a
// point a century, plus two (0 to 99) can't reach the region over land: at
// priority 1 if a transport is already there, else `priority`. Bases of
// types above 2 (and type 8 without that plan) also get goals 2, 0 and 11;
// type 7 a goal 3 at priority 1.
void assemble_passengers(int base, int type, int priority) {
    struct base *B = &game_bases[base];
    int faction = B->faction, x = B->x, y = B->y, region = tile(x, y)->region;
    int enemies = 0;
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (f == faction || !(game_factions[faction].diplo_status[f] & TREATY_VENDETTA)) continue;
        if (!game_factions[f].region_total_bases[region] || !game_contiguous(f, faction, region, 1)) enemies++;
    }
    B->state |= 1;
    B->ai_plan = 2;
    if (type != 8 || (game_factions[faction].region_flags[region] & 0x800)) {
        const struct faction_data *F = &game_factions[faction];
        int want = F->ai_growth - F->ai_fight - *game_turn / 100 + 2;
        want = want < 0 ? 0 : want > 99 ? 99 : want;
        if (enemies < want)
            add_goal(faction, GOAL_ESCORT, stack_check(veh_at(x, y), 2, 8, faction, -1) ? 1 : priority, x, y, -1);
        if (type <= 2) goto last;
    }
    add_goal(faction, 2, priority, x, y, -1);
    add_goal(faction, 0, priority, x, y, -1);
    add_goal(faction, 0xb, priority, x, y, -1);
last:
    if (type == 7) add_goal(faction, 3, 1, x, y, -1);
}

// ---- Orders -----------------------------------------------------------------------

// In a network game a unit's order goes through the daemon; these are
// what the original does there.
static void net_synch(int veh) {
    game_synch_veh(veh);
    game_net_await_synch(game_net_daemon);
    game_net_unlock_veh(game_net_daemon);
}

// One step of a unit going to its first waypoint (or, for ORDER_MOVE, in
// the direction kept in waypoint_x[0], to waypoint 1). Path_move picks the
// step, for the unit's faction unless the AI gave the order (then for no
// faction). With no step the order ends. Arrived, it ends too; a patrol
// (both patrol bits) not past its last leg moves its waypoints down one
// and goes on, the patrol bits cleared once none are left. An AI unit of a
// chassis that carries others, arrived in a base, is done for the turn.
void action_go_to(int veh) {
    if (net_turn()) *game_net_veh_ref = (int32_t *)&veh;
    struct veh *V = &game_vehs[veh];
    int dir;
    if (V->order == ORDER_MOVE) {
        dir = waypoint_x(V)[0];
    } else {
        dir = path_move(game_path, veh, V->order & ORDER_AI ? -1 : V->faction);
    }
    if (dir < 0) {
        game_vehs[veh].order = ORDER_NONE;
        if (net_turn()) net_synch(veh);
        return;
    }
    if (net_turn()) {
        if (game_net_order_veh(game_net_daemon, veh, dir, 1)) game_net_await_exec(game_net_daemon, 1);
        return;
    }
    if (!game_order_veh(veh, dir, 3)) return;
    V = &game_vehs[veh];  // the unit may have a new number after the move
    if (V->order == ORDER_MOVE) {
        if (V->x == waypoint_x(V)[1] && V->y == waypoint_y(V)[1]) V->order = ORDER_NONE;
        return;
    }
    if (V->order != ORDER_MOVE_TO || V->x != waypoint_x(V)[0] || V->y != waypoint_y(V)[0]) return;
    V->order = ORDER_NONE;
    if ((V->state & VEH_STATE_PATROL_2) == VEH_STATE_PATROL_2 && V->patrol_point < V->waypoint_count) {
        int16_t *wx = waypoint_x(V);
        for (int i = 0; i < V->waypoint_count; i++) wx[i] = wx[i + 1], wx[i + 4] = wx[i + 5];
        V->order = ORDER_MOVE_TO;
        if (!--V->waypoint_count) V->state &= ~0x2010200u;
    }
    if (game_is_human(V->faction) || !chassis_of(V)->cargo) return;
    const struct map_tile *t = tile(V->x, V->y);
    if (faction_on(t, MAP_BASE)) veh_skip(veh);
}

// A former going to its first waypoint building road (or tube, for
// ORDER_MAGTUBE_TO) on each square on the way. From land only, to a square
// on the map; else the order ends. Fungus is cleared first where roads
// can't go through it yet, if the faction can clear it and the square has
// no base. A square without a road gets one; with a road, tube if the
// order wants it, the square has none and the faction knows how (the
// original starts the work with the road's terraform index, and the work
// itself turns road into tube). The work keeps the order, so the former
// goes on afterwards. On a finished square the former steps on (by the path
// finder for no unit, avoiding fungus without the tech for roads through
// it), or at the goal stops, telling the player if it is theirs and not on
// a patrol.
void action_road_to(int veh) {
    if (net_turn()) *game_net_veh_ref = (int32_t *)&veh;
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, gx = waypoint_x(V)[0], vy = V->y, gy = waypoint_y(V)[0], vx = V->x;
    struct map_tile *t = tile(vx, vy);
    if (alt_of(t) < ALT_SHORE || !on_map(gx, gy)) {
        V->order = ORDER_NONE;
        if (net_turn()) net_synch(veh);
        return;
    }
    if ((t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40 && !has_tech(game_rules_basic->tech_road_fungus, faction) &&
        has_tech(game_terraform[TERRAFORM_REMOVE_FUNGUS].preq_tech, faction) && !faction_on(t, MAP_BASE)) {
        int order = (int8_t)V->order;
        if (net_turn()) {
            game_message_data(NET_MESSAGE_ORDER, 0, veh, ORDER_REMOVE_FUNGUS, order, 0);
            game_net_await_exec(game_net_daemon, 1);
            return;
        }
        game_action_terraform(veh, TERRAFORM_REMOVE_FUNGUS, 1);
        game_vehs[veh].order = (uint8_t)order;
        return;
    }
    t = tile(vx, vy);
    if ((t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40 && !has_tech(game_rules_basic->tech_road_fungus, faction))
        goto step;
    if (faction_on(t, MAP_BASE)) goto step;
    if (!(t->bits & MAP_ROAD)) {
        int order = (int8_t)V->order;
        if (net_turn()) {
            game_message_data(NET_MESSAGE_ORDER, 0, veh, ORDER_ROAD, order, 0);
            game_net_await_exec(game_net_daemon, 1);
            return;
        }
        game_action_terraform(veh, TERRAFORM_ROAD, 1);
        game_vehs[veh].order = (uint8_t)order;
        return;
    }
    if (V->order == ORDER_MAGTUBE_TO && !(t->bits & MAP_MAGTUBE)) {
        if (!has_tech(game_terraform[TERRAFORM_MAG_TUBE].preq_tech, faction)) goto step;
        if (net_turn()) {
            game_message_data(NET_MESSAGE_ORDER, 0, veh, ORDER_ROAD, ORDER_MAGTUBE_TO, 0);
            game_net_await_exec(game_net_daemon, 1);
            return;
        }
        game_action_terraform(veh, TERRAFORM_ROAD, 1);
        game_vehs[veh].order = ORDER_MAGTUBE_TO;
        return;
    }
step:
    if (vx == gx && vy == gy) {
        int order = (int8_t)game_vehs[veh].order;
        game_vehs[veh].order = ORDER_NONE;
        if (net_turn()) net_synch(veh);
        if ((game_vehs[veh].state & VEH_STATE_UNK_200) || faction != *game_current_faction) return;
        game_battle_text[0] = 0;
        game_say_loc(game_battle_text, gx, gy, 0, 2, 0);
        game_parse_says(0, game_battle_text, -1, -1);
        game_netmsg_pop(game_netmsg, order == ORDER_ROAD_TO ? game_text_road_to_done : game_text_tube_to_done, 5000, 0,
                        0);
        return;
    }
    int flags = has_tech(game_rules_basic->tech_road_fungus, faction) ? 0 : 0x10;
    int dir = path_find(game_path, vx, vy, gx, gy, 0, -1, flags, -1);
    if (dir >= 0 && dir < 8) {
        if (net_turn()) {
            if (game_net_order_veh(game_net_daemon, veh, dir, 1)) game_net_await_exec(game_net_daemon, 1);
            return;
        }
        game_order_veh(veh, dir, 3);
        return;
    }
    game_vehs[veh].order = ORDER_NONE;
    if (net_turn()) net_synch(veh);
}

// A unit's way home: the nearest base of its own or a pact partner's that
// it can reach (land units on their continent, ships at a port on their
// sea or in the base they are in; for type 2, not bases with state bit
// 0x200000), and for air units also carriers and air supply units of its
// faction and airbases nearby. A unit on alert (state 0x200, auto type 11)
// takes its post (waypoint 1) if it is in reach, and an air unit there
// only undamaged. Types 1, 3 and other odd ones, and 4, are for air units
// that must get down: they count a carrier further off by its moves (for
// the AI), take a supply unit only within this turn's moves, give up
// without a range (no fuel limit), and odd types only once the nearest is
// at the edge of the unit's reach (type 4: within it). A negative type
// only asks. Returns 1 with the unit sent there (ORDER_MOVE_TO; already
// there, its order ends and the AI's unit is done for the turn; a human's
// air unit out of reach gets no order), else 0.
//
// The airbases looked at are those within the distance to the best so
// far, at most 8 (as in OpenSMACX), as found by the radius table. Unlike
// OpenSMACX and Thinker, an AI's airbase must have its own unit or base on
// it (the square's owner, with or without a unit there), and the first
// post in reach ends the look before an air unit's range is asked, even
// for one that has none.
int action_home(int veh, int type) {
    struct veh *V = &game_vehs[veh];
    int best = 9999, vy = V->y, faction = V->faction, vx = V->x, region = tile(vx, vy)->region;
    int bx = -1, by = -1;
    int left = moves_left(veh);
    const struct unit_proto *P = proto_of(V);
    int range = game_chassis[P->chassis].range;
    int total = veh_speed(veh, 0) * (range - V->movement_turns - 1) + left;
    if (range == 1) {
        int hp = P->plan == PLAN_ARTIFACT ? 1 : (P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor) * 10;
        hp -= V->damage;
        hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
        total += veh_speed(veh, 0) * ((hp - 1) / (int)proto_of(V)->reactor);
    }
    int reach = total / game_rules_basic->move_rate_roads;
    int on_alert = (V->state & VEH_STATE_UNK_200) && V->order_auto_type == ORDER_AUTO_ALERT;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction && !(game_factions[faction].diplo_status[B->faction] & TREATY_PACT)) continue;
        int triad = chassis_of(V)->triad;
        if (triad == TRIAD_LAND) {
            if (tile(B->x, B->y)->region != region) continue;
        } else if (triad == TRIAD_SEA) {
            if ((B->x != vx || B->y != vy) && !base_on_sea(b, region)) continue;
        }
        if (type == 2 && (B->state & BASE_STATE_UNK_200000)) continue;
        int d = range_to(vx, vy, B->x, B->y);
        if (on_alert && B->x == waypoint_x(V)[1] && B->y == waypoint_y(V)[1] && d <= reach) {
            best = d, bx = B->x, by = B->y;
            goto found;
        }
        if (d < best) best = d, bx = B->x, by = B->y;
    }
    if (chassis_of(V)->triad == TRIAD_AIR) {
        for (int j = 0; j < *game_veh_count; j++) {
            const struct veh *O = &game_vehs[j];
            if (O->faction != faction || j == veh) continue;
            if (!has_abil(O->proto, ABL_CARRIER) &&
                (game_protos[O->proto].plan != PLAN_SUPPLY ||
                 game_chassis[game_protos[O->proto].chassis].triad != TRIAD_AIR))
                continue;
            int d = range_to(vx, vy, O->x, O->y);
            if ((type & 1) || type == 4) {
                if (!game_is_human(faction)) d += veh_speed(j, 0) / game_rules_basic->move_rate_roads;
                if (game_protos[O->proto].plan == PLAN_SUPPLY &&
                    range_to(vx, vy, O->x, O->y) > moves_left(veh) / game_rules_basic->move_rate_roads)
                    continue;
            }
            if (d < best) best = d, bx = O->x, by = O->y;
        }
        int n = game_radius_count[best < 8 ? best : 8];
        for (int i = 0; i < n; i++) {
            int x = xrange(vx + game_radius_x[i]), y = vy + game_radius_y[i];
            if (!on_map(x, y)) continue;
            const struct map_tile *t = tile(x, y);
            if (!(t->bits & MAP_AIRBASE)) continue;
            if (faction_on(t, MAP_UNIT) && owner_of(t) != faction &&
                !(game_factions[faction].diplo_status[owner_of(t)] & TREATY_PACT))
                continue;
            if (!game_is_human(faction)) {
                if (whose_territory(faction, x, y, NULL, 0) != faction) continue;
                int o = owner_of(tile(x, y));
                if ((o < 8 ? o : -1) != faction) continue;
            }
            int d = range_to(vx, vy, x, y);
            if (on_alert && !V->damage && x == waypoint_x(V)[1] && y == waypoint_y(V)[1] && d <= reach) {
                bx = x, by = y;
                goto found;
            }
            if (d < best) best = d, bx = x, by = y;
        }
    }
    if (((type & 1) || type == 4) && !chassis_of(V)->range) return 0;
found:
    if (!on_map(bx, by)) return 0;
    if ((type & 1) && best + 1 <= reach - 1) return 0;
    if (type == 4) {
        if (best > reach) return 0;
    } else if (type < 0) {
        return 1;
    }
    if (bx == vx && by == vy) {
        V->order = ORDER_NONE;
        if (!game_is_human(faction)) veh_skip(veh);
        return 1;
    }
    V->order = ORDER_MOVE_TO;
    waypoint_x(V)[0] = (int16_t)bx, waypoint_y(V)[0] = (int16_t)by;
    if (chassis_of(V)->range && game_is_human(faction) && best > reach) {
        V->order = ORDER_NONE;
        V->state &= ~(uint32_t)VEH_STATE_HOME_CLEAR;
    }
    return 1;
}

// Whether a unit may patrol to (x, y): another square on the map; for land
// units on its continent; for ships on its sea, or between ports that share
// one; air units with a range reach it there and back (one way if both ends
// are bases or airbases). The original takes a base for "no base" and the
// lack of one for a base (it tests base_at() against 0, not -1), so for an
// air unit only base 0's square counts as no landing place unless it has an
// airbase; and its test for neither end having one never holds.
int valid_patrol(int veh, int x, int y) {
    struct veh *V = &game_vehs[veh];
    if (x == V->x && y == V->y) return 0;
    if (!on_map(x, y)) return 0;
    int triad = chassis_of(V)->triad;
    if (triad == TRIAD_LAND) return tile(x, y)->region == tile(V->x, V->y)->region;
    if (triad == TRIAD_SEA) {
        int from = base_at(V->x, V->y), to = base_at(x, y);
        if (from >= 0) return (to >= 0 ? port_to_port(from, to) : base_on_sea(from, tile(x, y)->region)) != 0;
        if (to >= 0) return base_on_sea(to, tile(V->x, V->y)->region) != 0;
        return tile(x, y)->region == tile(V->x, V->y)->region;
    }
    if (triad != TRIAD_AIR) return 1;
    int range = chassis_of(V)->range;
    if (!range) return 1;
    int here = base_at(V->x, V->y) ? 1 : (tile(V->x, V->y)->bits & MAP_AIRBASE) ? 1 : 0;
    int there = base_at(x, y) ? 1 : (tile(x, y)->bits & MAP_AIRBASE) ? 1 : 0;
    int left = moves_left(veh);
    int reach = (veh_speed(veh, 0) * (range - V->movement_turns - 1) + left) / game_rules_basic->move_rate_roads;
    int d = range_to(V->x, V->y, x, y);
    if (here && there) return d <= reach;
    return d <= reach / 2;
}

// A patrol between the unit's square and (x, y): (x, y) is added after the
// unit's waypoints, and its own square after that, with both patrol bits
// and the first leg next.
void action_patrol(int veh, int x, int y) {
    if (!valid_patrol(veh, x, y)) return;
    struct veh *V = &game_vehs[veh];
    V->order = ORDER_MOVE_TO;
    waypoint_x(V)[V->waypoint_count] = (int16_t)x;
    waypoint_y(V)[V->waypoint_count] = (int16_t)y;
    V->waypoint_count++;
    waypoint_x(V)[V->waypoint_count] = V->x;
    waypoint_y(V)[V->waypoint_count] = V->y;
    V->state |= 0x1000200;
    V->patrol_point = 0;
}

// ---- Terraforming ---------------------------------------------------------------

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline int cur_faction(void) { return *game_current_faction; }
static inline void notice(uint32_t label) { game_netmsg_pop(game_netmsg, text(label), 5000, 0, NULL); }
// The map revealed to the faction, or the square seen by it.
static int seen_by(int faction, const struct map_tile *t) {
    return (game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED) || (t->visibility & (1u << faction));
}
// The leader's title and name (with the leader's gender), and the
// faction's noun (with its own), for a message.
static void say_title(int slot, int faction) {
    *game_parse_plural = 0;
    *game_parse_gender = game_faction_defs[faction].leader_female;
    game_parse_says(slot, game_faction_defs[faction].leader_title, -1, -1);
}
static void say_leader(int slot, int faction) {
    *game_parse_gender = game_faction_defs[faction].leader_female;
    *game_parse_plural = 0;
    game_parse_says(slot, game_faction_defs[faction].leader_name, -1, -1);
}
static void say_noun(int slot, int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->noun_gender;
    *game_parse_plural = d->noun_plural;
    game_parse_says(slot, (char *)d->noun, -1, -1);
}
// A base's name, also for the base before the first (-1), as the
// original reads it there.
static char *base_name(int base) { return (char *)game_bases + base * (int)sizeof(struct base) + 0x13; }

enum {
    MAP_SOLAR_ = 0x40, MAP_MINE_ = 0x10, MAP_FARM_ = 0x8000, MAP_SOIL_ = 0x80000,
    MAP_CLIMATE_WORKS = 0x1c00000,  // a condenser, echelon mirror or thermal borehole
    MAP_RIVER_SOURCE = 0x100,
    // Base states the AI sets for the improvements it wants around a base.
    BASE_WANTS_ENERGY = 0x1000, BASE_WANTS_MINERALS = 0x2000, BASE_WANTS_NUTRIENTS = 0x4000,
    ABL_SUPER_TERRAFORMER = 0x1, ABL_FUNGICIDAL = 0x2000,
    TF_FARM = 0, TF_SOIL = 1, TF_MINE = 2, TF_SOLAR = 3, TF_FOREST = 4, TF_ROAD = 5, TF_MAGTUBE = 6, TF_SENSOR = 9,
    TF_REMOVE_FUNGUS = 10, TF_PLANT_FUNGUS = 11, TF_CONDENSER = 12, TF_BOREHOLE = 14, TF_AQUIFER = 15,
    TF_RAISE = 16, TF_LOWER = 17, TF_LEVEL = 18,
    ORDER_FIRST_TERRAFORM = 4,
    SP_WEATHER_PARADIGM = 2, SP_XENOEMPATHY_DOME = 8,
};
static inline int rocky_of(const struct map_tile *t) { return t->rocky_lock >> 6; }
static inline int fungus_on(const struct map_tile *t) { return (t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40; }

// What terraforming costs in energy at a square (as the AI's raising and
// lowering of land pays for it): by its height from the shore line, three
// times as much on fungus, twice that again under the sea (and twice more
// while no one knows tech 39, as Thinker has it Doctrine: Air Power), and
// by the distance in squares (1 to 100) to the faction's nearest base. A
// base of a faction without a pact further off than that one (by its
// distance, and size + 2, a third of each), the more so. Halved for a
// faction with the terraforming bonus, then halved again (1 to 30000).
int terraform_cost(int x, int y, int faction) {
    const struct map_tile *t = tile(x, y);
    int alt = t->climate >> 5, d = alt - 3 < 0 ? 3 - alt : alt - 3;
    int cost = (d + 2) * (d + 2);
    if (fungus_on(t)) cost *= 3;
    cost *= 2;
    if (alt < 3) {
        cost *= 2;
        if (!game_tech_achieved[39]) cost *= 2;
    }
    int own = base_find_2(x, y, faction), other = base_find_3(x, y, -1, -1, faction, -1);
    if (own >= 0) {
        const struct base *B = &game_bases[own];
        int r = range_to(x, y, B->x, B->y);
        cost *= r < 1 ? 1 : r > 100 ? 100 : r;
        if (other >= 0 && !(game_factions[faction].diplo_status[game_bases[other].faction] & TREATY_PACT)) {
            const struct base *O = &game_bases[other];
            int theirs = range_to(x, y, B->x, B->y) * (O->pop_size + 2) / 3;
            int ours = range_to(x, y, O->x, O->y) * (B->pop_size + 2) / 3;
            if (ours && theirs && ours < theirs) cost = theirs * cost / ours;
        }
    }
    if (game_faction_defs[faction].rule_flags & 0x2000) cost /= 2;
    cost /= 2;
    return cost < 1 ? 1 : cost > 30000 ? 30000 : cost;
}

// The AI's choice of terraforming for a square, as the order for it
// (ORDER_FIRST_TERRAFORM + the terraform), or 0 for none: base is the base
// it would be for, or -1, and `keen` makes the AI readier for solar
// collectors and farms. None on a base or on land claimed by a faction
// without a pact. Fungus is cleared (sea fungus where it yields less than
// the faction's fungus tech gives) unless a human hasn't asked for the AI's
// help with it; deep sea is raised. At sea: a farm first, then a mine or
// tidal harness as the base wants (or by what its radius has), then a soil
// enricher. On land a solar collector or mine where the square suits (its
// height, rainfall and rockiness), a farm or enricher where food is wanted,
// a forest where the square is worth less than a forest's yield, and for
// the AI with the tech, fungus where it is worth less than fungus. The
// original asks for the sea tech of fungus removal on land as at sea.
int can_terraform(int faction, int x, int y, int keen, int base) {
    const struct map_tile *t = tile(x, y);
    if (faction_on(t, MAP_BASE)) return 0;
    int fungus = fungus_on(t);
    uint32_t bits = t->bits;
    int mine = (bits & MAP_MINE_) != 0, solar = (bits & MAP_SOLAR_) != 0, farm = (bits & MAP_FARM_) != 0;
    int soil = (bits & MAP_SOIL_) != 0, forest = (bits & MAP_FOREST) != 0;
    int special = (bits & MAP_CLIMATE_WORKS) || ((t->bits2 & 0x80000002u) == 2 && !(t->bits2 & 0xff000000u));
    const struct faction_data *F = &game_factions[faction];
    int fungus_yield = F->tech_fungus[2] + F->tech_fungus[1] + F->tech_fungus[0];
    int forest_yield = game_res_info->forest.energy + game_res_info->forest.mineral + game_res_info->forest.nutrient;
    const struct base *B = base >= 0 ? &game_bases[base] : NULL;
    if (B) {
        if (game_has_facility(B, FAC_TREE_FARM)) forest_yield++;
        if (game_has_facility(B, FAC_HYBRID_FOREST)) forest_yield += 2;
    }
    if (F->ai_growth && !F->ai_power && !F->ai_wealth && !F->ai_tech && F->ai_fight < 0 && !game_is_human(faction))
        forest_yield++;
    const struct rules_terraform *T = game_terraform;
    int alt = alt_of(t);
    int human_alone = game_is_human(faction) && !(*game_more_preferences & 8);
    if (alt < ALT_SHORE) {
        if (fungus) {
            if (fungus_yield >= 4 || human_alone) return 0;
            return has_tech(T[TF_REMOVE_FUNGUS].preq_tech_sea, faction) ? 14 : 0;
        }
        if (alt < 0x40) return terrain_avail(TF_RAISE, 1, faction) ? 20 : 0;
        if (bits & MAP_THERMAL_BORE) return 0;
        if (!farm && has_tech(T[TF_FARM].preq_tech_sea, faction)) return 4;
        if (!solar && !mine && !special) {
            if (B) {
                if ((B->state & BASE_WANTS_MINERALS) && has_tech(T[TF_MINE].preq_tech_sea, faction)) return 6;
                if ((B->state & BASE_WANTS_ENERGY) && has_tech(T[TF_SOLAR].preq_tech_sea, faction)) return 7;
                if (((B->state & BASE_WANTS_NUTRIENTS) || B->surplus[0] < 2) &&
                    has_tech(T[TF_SOLAR].preq_tech_sea, faction) && game_rules_basic->nutrient_effect_mine < 0)
                    return 7;
                int mines = game_rules_basic->nutrient_effect_mine < 0, solars = 0;
                for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
                    int sx = xrange(B->x + game_radius_x[i]), sy = B->y + game_radius_y[i];
                    if (!on_map(sx, sy)) continue;
                    const struct map_tile *s = tile(sx, sy);
                    if (alt_of(s) >= ALT_SHORE) continue;
                    if (s->bits & MAP_MINE_) mines++;
                    if (s->bits & MAP_SOLAR_) solars++;
                }
                if (!has_tech(T[TF_MINE].preq_tech_sea, faction) + mines > solars &&
                    has_tech(T[TF_SOLAR].preq_tech_sea, faction))
                    return 7;
            }
            if (has_tech(T[TF_MINE].preq_tech_sea, faction) &&
                (game_rules_basic->nutrient_effect_mine >= 0 ||
                 (B && B->surplus[0] > 1 && !(B->state & BASE_WANTS_NUTRIENTS))))
                return 6;
        }
        if (farm && !soil && has_tech(T[TF_SOIL].preq_tech_sea, faction)) return 4;
        return 0;
    }
    int rain = (t->climate >> 3) & 3, rocky = rocky_of(t), result = 0;
    int yield = crop_yield(faction, -1, x, y, 0);
    int level = (t->climate >> 5) - 2;
    if (level < 0) level = 0;
    int bonus = bonus_at(x, y, 0);
    int score = level + rain + rocky + 1, worth = score;
    if (has_tech(T[TF_SOIL].preq_tech, faction) && farm) worth++;
    int owner = whose_territory(faction, x, y, NULL, 0);
    if (owner >= 0 && owner != faction && !(F->diplo_status[owner] & TREATY_PACT)) return 0;
    if (fungus) {
        if (B && B->eco_damage / 2 + worth <= fungus_yield) return 0;
        if (human_alone) return 0;
        return has_tech(T[TF_REMOVE_FUNGUS].preq_tech_sea, faction) ? 14 : 0;
    }
    if (tile(x, y)->bits & MAP_MONOLITH) return 0;
    if (forest && worth <= forest_yield) return 0;
    int mine_tech = has_tech(T[TF_MINE].preq_tech, faction), solar_tech = has_tech(T[TF_SOLAR].preq_tech, faction);
    has_tech(T[TF_FARM].preq_tech, faction);  // asked, and not used
    if (!mine && !solar && !special) {
        if (solar_tech && level && (((rocky == 1 || level >= 2) && yield >= farm + 1) || keen)) result = 7;
        if (mine_tech) {
            if (bonus == 2 || rocky == 2) result = 6;
            int e;
            if (bonus == 2 && rocky == 2) {
                result = 6;
                goto keen_mine;
            }
            if (bonus != 2 && rocky == 2) goto rocky_mine;
            if (game_rules_basic->nutrient_effect_mine >= 0) goto rocky_mine;
            e = !farm + yield;
            if (e <= 1) goto rocky_mine;
            if (rocky || e <= 2) goto keen_solar;
            goto keen_mine;
        rocky_mine:
            if (rocky == 1 && level == 0) result = 6;
            if (bonus == 3) result = 6;
        keen_mine:
            if (keen && farm && !result) result = 6;
        }
    keen_solar:
        if (keen && farm && !result && level && solar_tech) result = 7;
        if (B) {
            if ((B->state & BASE_WANTS_ENERGY) && solar_tech && level && (yield >= farm + 1 || level > 1)) result = 7;
            if ((B->state & BASE_WANTS_MINERALS) && mine_tech) {
                if (yield >= farm + 2) result = 6;
                if ((rocky > 1 || bonus == 2 || (rain && rocky < 2 && level < 2)) && (farm || yield >= 2)) result = 6;
            }
        }
    }
    t = tile(x, y);
    if (has_tech(T[TF_FARM].preq_tech, faction) && !farm && rocky < 2 && !(t->bits & MAP_THERMAL_BORE) &&
        (t->bits2 & 0x80000002u) != 2 && !(t->bits & MAP_ECHELON_MIRROR) &&
        !(yield >= 2 && !has_tech(game_rules_basic->tech_three_nutrients, faction) && bonus != 1)) {
        if (solar || mine) result = 4;
        if (B && (B->state & BASE_WANTS_NUTRIENTS)) {
            result = 4;
        } else if (result == 7) {
            if (yield < 2) result = 4;
        } else if (!result) {
            result = 4;
            goto farmed;
        }
        if (rain < 2) result = 4;
    farmed:;
    }
    if (has_tech(T[TF_SOIL].preq_tech, faction) && farm && !soil) {
        if (yield > 1 && (yield != 2 || has_tech(game_rules_basic->tech_three_nutrients, faction))) {
            if (solar || mine) result = 4;
            if (B && (B->state & BASE_WANTS_NUTRIENTS)) {
                result = 4;
                goto soiled;
            }
        }
        if (!result && (keen || yield > 1)) result = 4;
    soiled:;
    }
    if (has_tech(T[TF_SOIL].preq_tech, faction) && farm) score++;
    if (B) {
        if (B->state & BASE_WANTS_NUTRIENTS) {
            if (rain > 1) score++;
            if (rain == 0) score--;
        }
        if (B->state & BASE_WANTS_ENERGY) {
            if (level > 1) score++;
            if (level == 0) score--;
        }
        if ((B->state & BASE_WANTS_MINERALS) && !rocky) score--;
        if (!(B->state & (BASE_WANTS_MINERALS | BASE_WANTS_NUTRIENTS)) && solar) score++;
    }
    if (rain && level && rocky == 1 && *game_turn < 50) score++;
    if (has_tech(T[TF_FOREST].preq_tech, faction) && rocky < 2 && !mine && !forest &&
        !(farm && (level || rain >= 2 || game_is_human(faction))) && !special &&
        (tile(x, y)->bits2 & 0x80000002u) != 2 && score < forest_yield &&
        (!game_is_human(faction) || (faction == cur_faction() && (*game_preferences & 0x1000000))))
        result = 8;
    if (has_tech(T[TF_PLANT_FUNGUS].preq_tech, faction) && !game_is_human(faction)) {
        if (rocky == 2) score += mine + 1;
        if (!special && ++score < fungus_yield) result = 15;
    }
    return result;
}

// A former's work a turn on a terraform: twice as much with the super
// former ability, and twice that again on fungus with the Xenoempathy
// Dome, half as much again elsewhere with the Weather Paradigm (as the
// game's contribution(), which the original has inline).
static int work_rate(int veh, int terraform) {
    const struct veh *V = &game_vehs[veh];
    int rate = has_abil(V->proto, ABL_SUPER_TERRAFORMER) ? 4 : 2;
    if (terraform == TF_REMOVE_FUNGUS || terraform == TF_PLANT_FUNGUS) {
        int b = game_secret_projects[SP_XENOEMPATHY_DOME];
        if (b >= 0 && game_bases[b].faction == V->faction) rate *= 2;
    } else {
        int b = game_secret_projects[SP_WEATHER_PARADIGM];
        if (b >= 0 && game_bases[b].faction == V->faction) rate = rate * 3 / 2;
    }
    return rate;
}

// The top of a unit's stack.
static int stack_top(int veh) {
    int top = veh;
    for (int p = game_vehs[veh].prev_in_stack; p >= 0; p = game_vehs[p].prev_in_stack) top = p;
    return top;
}

// The tutorial's note on a former's first work of a kind.
static void terraform_note(int veh, int terraform, int sea, int x, int y) {
    uint32_t label = 0;
    void *sprite = NULL;
    int unit = -1;
    if (!sea) {
        switch (terraform) {
        case TF_FARM: label = 0x687bd0, sprite = game_sprite_farm; break;
        case TF_SOIL: label = 0x687bdc, sprite = game_sprite_soil; break;
        case TF_MINE: label = 0x687be8, sprite = game_sprite_mine; break;
        case TF_SOLAR: label = 0x687bf4, sprite = game_sprite_solar; break;
        case TF_FOREST: label = 0x687c00, unit = veh; break;
        case TF_ROAD: label = 0x687c0c, unit = veh; break;
        case TF_MAGTUBE: label = 0x687c18, unit = veh; break;
        case TF_SENSOR: label = 0x687c24, unit = veh; break;
        case TF_CONDENSER: label = 0x687c3c, unit = veh; break;
        case TF_BOREHOLE: label = 0x687c4c, unit = veh; break;
        case TF_AQUIFER: label = 0x687c30, unit = veh; break;
        }
    } else {
        switch (terraform) {
        case TF_FARM: label = 0x687c5c, sprite = game_sprite_farm; break;
        case TF_MINE: label = 0x687c68, sprite = game_sprite_mine; break;
        case TF_SOLAR: label = 0x687c74, sprite = game_sprite_solar; break;
        }
    }
    if (!label) {
        game_tut_map_seen[sea] &= ~(1 << terraform);
        return;
    }
    game_tut_map(game_tut_win, text(label), x, y, unit, sprite, 1, -1, -1);
}

// A former's terraforming at its square (a road where there is one already
// becomes tube with the tech; a land farm becomes an enricher with the
// farm's tech, where Thinker asks for the enricher's). With `toggle` zero
// it only answers how many more turns the work takes, counting the work
// of other formers in the stack on the same order (and this turn's, if the
// unit has moves left). Otherwise the unit takes the order, gathers the
// work its stack's formers on it had done and works a turn; the work isn't
// done in a base (the order ends). Once the work is done every former in
// the stack on it stops, and the square changes: the improvement's bit is
// set and those it replaces cleared, with what goes with each (sites,
// climate, river sources, height, rockiness), a nearby base told to look
// at its squares again, a borehole's and a cleared forest's messages (the
// forest's minerals going to the unit's home base or the nearest), and for
// changes of height or climate the faction whose rainfall it changed
// annoyed, if human. The player's formers on alert show the tutorial's
// note for a first improvement, and the first former to finish in a turn
// plays a sound. Returns 1 if the work is done.
//
// The AI at difficulty above 3 works a turn faster (on work over 3 turns).
// The message for bases whose units drowned is shown when the current
// faction has such a base, but names the working faction's.
int action_terraform(int veh, int terraform, int toggle) {
    struct veh *V = &game_vehs[veh];
    int y = V->y, x = V->x, faction = V->faction;
    struct map_tile *t = tile(x, y);
    int sea = alt_of(t) < ALT_SHORE, vehs = *game_veh_count;
    const struct rules_terraform *T = game_terraform;
    if (terraform == TF_ROAD) {
        if ((t->bits & MAP_ROAD) && has_tech((&T[TF_MAGTUBE].preq_tech)[sea], faction)) terraform = TF_MAGTUBE;
    } else if (terraform == TF_FARM && (t->bits & MAP_FARM_) && !sea) {
        if (has_tech((&T[TF_FARM].preq_tech)[sea], faction)) terraform = TF_SOIL;
    }
    if (toggle) V->order = (uint8_t)(terraform + ORDER_FIRST_TERRAFORM);
    int turns = T[terraform].rate;
    switch (terraform) {
    case TF_SOLAR: turns += rocky_of(t) * turns / 2; break;
    case TF_ROAD:
    case TF_MAGTUBE:
        turns = T[t->bits & MAP_ROAD ? TF_MAGTUBE : TF_ROAD].rate;
        if (t->bits & MAP_RIVER) turns++;
        if (fungus_on(t)) turns += 2;
        if (t->bits & MAP_FOREST) turns += 2;
        else turns += rocky_of(t);
        break;
    case TF_REMOVE_FUNGUS:
        if (has_abil(V->proto, ABL_FUNGICIDAL)) turns /= 2;
        break;
    }
    if (!game_is_human(faction) && *game_difficulty > 3 && turns > 3) turns--;
    int total = 2 * turns, rate = 0, done = 0, order = terraform + ORDER_FIRST_TERRAFORM;
    if (veh >= 0) {
        for (int c = stack_top(veh); c >= 0; c = game_vehs[c].next_in_stack) {
            if (c == veh || (int8_t)game_vehs[c].order != order) continue;
            if (!toggle) {
                rate += work_rate(c, terraform);
                done += game_vehs[c].movement_turns;
            } else {
                V->movement_turns = (uint8_t)(V->movement_turns + game_vehs[c].movement_turns);
                game_vehs[c].movement_turns = 0;
            }
        }
    }
    rate += work_rate(veh, terraform);
    done += V->movement_turns;
    if (!toggle) {
        if (veh_speed(veh, 0) - V->moves_spent > 0) done += rate;
        int r = rate < 1 ? 1 : rate > 255 ? 255 : rate;
        return (total - done + r - 1) / r;
    }
    veh_skip(veh);
    V->movement_turns = (uint8_t)(V->movement_turns + rate);
    if (faction_on(t, MAP_BASE)) {
        V->order = ORDER_NONE;
        return 0;
    }
    if (V->movement_turns < total) {
        game_draw_tile(x, y, 2);
        game_base_win_check_veh(game_base_win, veh);
        return 0;
    }
    V->movement_turns = 0;
    if (veh >= 0)
        for (int c = stack_top(veh); c >= 0; c = game_vehs[c].next_in_stack)
            if ((int8_t)game_vehs[c].order == order) game_vehs[c].order = ORDER_NONE;
    uint32_t had_forest = t->bits & MAP_FOREST;
    bit_set(x, y, T[terraform].bit_incompatible, 0);
    bit_set(x, y, T[terraform].bit, 1);
    synch_bit(x, y, faction);
    switch (terraform) {
    case TF_REMOVE_FUNGUS:
    case TF_PLANT_FUNGUS: site_radius(x, y, 0); break;
    case TF_CONDENSER:
    case TF_BOREHOLE:
        site_radius(x, y, 0);
        world_climate();
        game_draw_map(1);
        break;
    case TF_AQUIFER:
        bit_set(x, y, MAP_RIVER_SOURCE, 1);
        world_climate();
        game_draw_map(1);
        break;
    case TF_RAISE:
        world_raise_alt(x, y);
        world_climate();
        game_draw_map(1);
        break;
    case TF_LOWER:
        world_lower_alt(x, y);
        world_climate();
        game_draw_map(1);
        break;
    case TF_LEVEL:
        if (rocky_of(tile(x, y))) {
            rocky_set(x, y, rocky_of(tile(x, y)) - 1);
            site_radius(x, y, 0);
        }
        break;
    }
    if (T[terraform].bit & (MAP_THERMAL_BORE | MAP_FOREST | MAP_FARM_ | MAP_SOLAR_ | MAP_MINE_)) {
        int b = base_find_2(x, y, faction);
        if (b >= 0 && *game_base_find_dist <= 2) game_bases[b].state |= BASE_STATE_UNK_100;
        if (terraform == TF_BOREHOLE && faction == cur_faction() && b >= 0 && game_bases[b].faction == faction) {
            game_parse_says(0, game_bases[b].name, -1, -1);
            notice(0x687b80);
        }
    }
    t = tile(x, y);
    if (had_forest && !(t->bits & MAP_FOREST) && alt_of(t) >= ALT_SHORE && terraform != TF_RAISE) {
        int b = V->home_base;
        if (b < 0) b = base_find_2(x, y, faction);
        if (b >= 0) {
            struct base *B = &game_bases[b];
            int harvest = game_rules_basic->minerals_harvesting_forest;
            B->minerals += harvest;
            B->minerals_2 = B->minerals;
            B->production_last = B->queue[0];
            if (faction == cur_faction() && game_is_human(faction) && game_rules_basic->minerals_harvesting_forest) {
                game_parse_says(0, B->name, -1, -1);
                game_parse_num(0, game_rules_basic->minerals_harvesting_forest);
                notice(0x687b90);
            }
            game_base_win_check_base(game_base_win, b);
        }
    }
    if (terraform == TF_RAISE || terraform == TF_LOWER || terraform == TF_CONDENSER) {
        int rf = *game_rain_faction;
        if (rf >= 0 && !game_is_human(rf) && game_is_human(faction) &&
            !(game_factions[faction].diplo_status[rf] & (TREATY_PACT | TREATY_VENDETTA))) {
            if (game_factions[rf].diplo_status[faction] & TREATY_WANT_REVENGE)
                set_treaty(rf, faction, TREATY_SHALL_BETRAY, 1);
            else
                cause_friction(rf, faction, 5);
        }
        if (*game_rain_base >= 0) {
            game_parse_says(0, game_bases[*game_rain_base].name, -1, -1);
            game_parse_says(1, game_faction_defs[faction].adjective, -1, -1);
            notice(faction == cur_faction() ? 0x687ba0 : 0x687bac);
        }
        if (game_base_submerged_veh[cur_faction()] >= 0) {
            game_parse_says(0, base_name(game_base_submerged_veh[faction]), -1, -1);
            game_parse_says(1, game_faction_defs[faction].adjective, -1, -1);
            notice(faction == cur_faction() ? 0x687bb8 : 0x687bc4);
        }
    }
    synch_bit(x, y, faction);
    game_draw_tiles(x, y, 2);
    if (vehs != *game_veh_count) {
        game_graphic_win_redraw(game_base_win);
    } else {
        game_base_win_check_veh(game_base_win, veh);
        game_base_win_check_loc(game_base_win, x, y, 2);
    }
    if (faction != cur_faction()) return 1;
    if ((V->state & VEH_STATE_UNK_200) && (*game_preferences & 0x20) && !*game_net_multiplayer &&
        !(game_tut_map_seen[sea] & (1 << terraform))) {
        game_tut_map_seen[sea] |= 1 << terraform;
        terraform_note(veh, terraform, sea, x, y);
    }
    if (faction == cur_faction() && V->order == ORDER_NONE && !(V->state & VEH_STATE_UNK_200) &&
        !*game_terraform_sound_played) {
        *game_terraform_sound_played = 1;
        game_wave_it(20);
        game_ambience(0xcf);
    }
    return 1;
}

// The order a unit is on, carried out: a former's work, a road or tube
// built along the way, or a step towards its goal. Returns 0 for any other
// order; an order the AI gave ends once carried out.
int action(int veh) {
    int order = game_vehs[veh].order;
    if (order >= ORDER_FIRST_TERRAFORM && order < ORDER_MOVE_TO)
        action_terraform(veh, order - ORDER_FIRST_TERRAFORM, 1);
    else if (order == ORDER_ROAD_TO || order == ORDER_MAGTUBE_TO)
        action_road_to(veh);
    else if (order == ORDER_MOVE_TO || order == (ORDER_MOVE_TO | ORDER_AI) || order == ORDER_MOVE)
        action_go_to(veh);
    else
        return 0;
    if (game_vehs[veh].order & ORDER_AI) game_vehs[veh].order = ORDER_NONE;
    return 1;
}

// ---- Bases ------------------------------------------------------------------------

// A colony pod founding a base on its square (named `name`, if given, and
// made unique): rocks are smoothed to rolling, the view moves there if the
// player sees it, and founding in another's territory (with bases of its
// own, neither at war nor in a pact, a human on either side) breaks a
// treaty or declares war. A human's base takes the governor of the unit's
// home base, and is put under the governor if the unit was on alert. The
// unit is used up, no faction's AI keeps the square as a site, and the
// founding is noted in the faction's monuments. Returns the base, or
// (with the unit done for the turn) -1. Unlike Thinker the original has no
// check of the base limit.
int action_build(int veh, char *name) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, y = V->y, x = V->x;
    game_log_say(text(0x687b60), x, y, faction);
    game_log_say(text(0x687b6c), *game_turn, 0, 0);
    if ((tile(x, y)->rocky_lock & 0xc0) > 0x40) rocky_set(x, y, 1);
    int cur = cur_faction();
    if (faction == cur || (V->visibility & (1u << cur)) || (*game_state & 0x80))
        game_console_focus(game_console, x, y, faction);
    int owner = whose_territory(faction, x, y, NULL, 0);
    struct faction_data *F = &game_factions[faction];
    if (F->base_count && owner >= 0 && owner != faction) {
        uint32_t status = F->diplo_status[owner];
        if (!(status & (TREATY_PACT | TREATY_VENDETTA)) && (game_is_human(faction) || game_is_human(owner))) {
            if (status & TREATY_TREATY) game_double_cross(faction, owner, -1);
            else treaty_on(faction, owner, TREATY_VENDETTA);
        }
    }
    int base = base_init(faction, x, y);
    if (base < 0) {
        veh_skip(veh);
        return base;
    }
    struct base *B = &game_bases[base];
    if (game_is_human(faction)) {
        int home = V->home_base;
        if (home >= 0) B->governor = game_bases[home].governor;
        if (V->state & VEH_STATE_UNK_200) B->governor |= 0x80000000u;
    }
    F->last_base_turn = *game_turn;
    if (name && strlen(name)) {
        strcpy(B->name, name);
        make_base_unique(base);
    }
    veh_kill(veh);
    cur = cur_faction();
    if (faction == cur || B->faction == cur || (B->visibility & (1u << cur)) || (*game_state & 0x80)) game_draw_map(1);
    for (int f = 1; f < FACTION_COUNT; f++) del_site(f, 8, x, y, 2);
    game_mon_colony_founded(faction, B->name);
    return base;
}

// Nerve stapling a base: its riots end for ten more turns of stapling,
// told to the player (in a box for their own base, a notice for another's),
// unless it fails, as it may from the third time on (half the time; always
// after the eighth), which only the base's owner is told. Either way it is
// an atrocity, the worse against the base's former owner.
void action_staple(int base) {
    struct base *B = &game_bases[base];
    int count = ++B->nerve_staple_count, faction = B->faction;
    int alien = (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0;
    if (count > 2 && (count > 8 || !(game_rand() % 2))) {
        if (B->faction == cur_faction()) {
            game_parse_says(0, B->name, -1, -1);
            if (alien) game_popp(*game_script_file, text(0x687c94), 0, text(0x687c84), 0);
            else game_popp(*game_script_file, text(0x687cb4), 0, text(0x687ca4), 0);
        }
    } else {
        B->state &= ~(uint32_t)BASE_STATE_DRONE_RIOT;
        B->nerve_staple_turns = (uint8_t)(B->nerve_staple_turns + 10);
        game_draw_tile(B->x, B->y, 2);
        if (B->faction == cur_faction()) {
            game_parse_says(0, B->name, -1, -1);
            if (alien) game_popp(*game_script_file, text(0x687cd4), 0, text(0x687cc4), 0);
            else game_popp(*game_script_file, text(0x687cf4), 0, text(0x687ce4), 0);
        } else {
            game_parse_says(0, B->name, -1, -1);
            say_title(1, B->faction);
            say_leader(2, B->faction);
            notice(0x687d04);
        }
    }
    game_atrocity(B->faction, B->faction_former, B->faction_former != B->faction, 1);
}

// A base wiped out by a unit (its population, ten to a size, in the
// message the player is shown if it was theirs to destroy or lose, or they
// could see it), an atrocity when the rules make it one.
void action_oblit(int veh, int base) {
    int faction = game_vehs[veh].faction;
    struct base *B = &game_bases[base];
    int former = B->faction_former;
    game_parse_num(0, B->pop_size * 10);
    game_parse_says(0, B->name, -1, -1);
    say_title(1, cur_faction());
    game_parse_says(2, game_faction_defs[former].adjective, -1, -1);
    say_title(3, faction);
    say_leader(4, faction);
    int cur = cur_faction();
    int seen = B->faction == cur || (B->visibility & (1u << cur));
    base_kill(base);
    game_draw_map(1);
    if (faction == cur_faction())
        game_popp(*game_script_file, text(0x68800c), 0, text(0x687ffc), 0);
    else if (former == cur_faction() || seen)
        game_popp(*game_script_file, text(0x688028), 0, text(0x688018), 0);
    if (game_rules_basic->oblit_atrocity) game_atrocity(faction, former, 2, 0);
}

// A unit through a psi gate to the base: both bases' gates are used this
// turn, and the map shows it.
void action_gate(int veh, int base) {
    int from = base_at(game_vehs[veh].x, game_vehs[veh].y);
    if (from >= 0) game_bases[from].state |= 0x20000000;
    struct base *B = &game_bases[base];
    B->state |= 0x20000000;
    veh_drop(veh_lift(veh), B->x, B->y);
    if (from >= 0) game_draw_tile(game_bases[from].x, game_bases[from].y, 2);
    game_draw_tile(B->x, B->y, 2);
    if (veh == *game_veh_ref_map_1) {
        const struct veh *V = &game_vehs[veh];
        game_console_focus(game_console, V->x, V->y, V->faction);
    }
}

// ---- Attacks on squares -------------------------------------------------------------

// A unit (artillery, a bomber, native life) destroying what is on (x, y):
// `item` the map bits it aims at, or 0 for whatever is there, the sensor
// first, then by terraform; fungus only for a unit that clears it. Off the
// map it works on its own square. On the map it fails half the time (and
// on a base always), and otherwise it is a bombardment, with explosions
// where the player sees them: in another faction's territory a unit of
// that faction within two squares of the target (on the 24 squares round
// the attacker) is fought instead, if one is (Thinker measures from the
// attacker). An air unit other than a missile fails a quarter of the time.
// What is destroyed is told to whom it concerns: the territory's owner,
// who is in contact and sees the square, takes the attacker as a target,
// and the player hears native life eat their improvements. The unit is
// done for the turn (a step in no direction).
void action_destroy(int veh, int item, int x, int y) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, bombard = 0, forest = 0;
    veh_skip(veh);
    V->state |= 0x40;
    if (!on_map(x, y)) {
        x = V->x, y = V->y;
    } else {
        if (faction_on(tile(x, y), MAP_BASE)) {
            if (faction == cur_faction()) game_boom(x, y, 0x80);
            return;
        }
        if (game_rand() % 2) {
            if (faction == cur_faction()) notice(0x687d24);
            return;
        }
        bombard = 1;
        if (faction == cur_faction() || whose_territory(cur_faction(), x, y, NULL, 0) == cur_faction()) {
            if (faction && faction != cur_faction()) {
                game_boom(x, y, 0x80);
                game_boom(V->x, V->y, 0x80);
            }
            game_boom(x, y, 0x80);
        }
        int owner = whose_territory(faction, x, y, NULL, 0);
        if (owner != faction) {
            for (int i = 1; i < 25; i++) {
                int px = xrange(V->x + game_radius_x[i]), py = V->y + game_radius_y[i];
                if (!on_map(px, py) || ((x_dist(px, x) + abs(py - y)) & ~1) > 4) continue;
                int s = stack_fix(veh_at(px, py));
                if (s >= 0 && stack_check(s, 15, owner, -1, -1)) {
                    game_battle_fight(veh, i, 1, 1, 0);
                    return;
                }
            }
        }
    }
    const struct rules_chassis *C = chassis_of(V);
    if (C->triad == TRIAD_AIR && !C->missile && game_rand() % 4 == 0) {
        if (faction == cur_faction()) notice(0x687d44);
        goto done;
    }
    uint32_t bits;
    if (item > 0) {
        bits = tile(x, y)->bits & (uint32_t)item;
        item = (int)bits;
        if (!bits) goto done;
        bit_set(x, y, bits, 0);
    } else {
        bits = tile(x, y)->bits;
    }
    if (!has_abil(V->proto, ABL_FUNGICIDAL)) bits &= ~(uint32_t)MAP_FUNGUS;
    int k = -1, tf;
    for (;;) {
        tf = k < 0 ? TF_SENSOR : k;
        if (game_terraform[tf].bit & bits) break;
        if (++k >= TERRAFORM_COUNT) {
            game_log_say(text(0x687d50), item, (int)bits, 0);
            if (bombard) {
                if (faction == cur_faction()) notice(0x687d6c);
            } else if (chassis_of(V)->triad == TRIAD_AIR && faction == cur_faction()) {
                notice(0x687d7c);
            }
            goto done;
        }
    }
    item = (int)((uint32_t)item | game_terraform[tf].bit);
    bit_set(x, y, game_terraform[tf].bit, 0);
    int sea = alt_of(tile(x, y)) < ALT_SHORE;
    game_parse_says(2, game_strings_get(game_strings, sea ? game_terraform[tf].name_sea : game_terraform[tf].name), -1,
                    -1);
    if (tf == TF_FOREST) forest = 1;
    if (chassis_of(V)->triad == TRIAD_AIR && faction == cur_faction()) notice(0x687d88);
    synch_bit(x, y, faction);
    int owner = whose_territory(faction ? faction : cur_faction(), x, y, NULL, 0);
    synch_bit(x, y, owner);
    if ((uint32_t)item & MAP_CLIMATE_WORKS) {
        world_climate();
        game_draw_map(1);
    } else if (((uint32_t)item & MAP_SENSOR) && owner == cur_faction()) {
        game_draw_radius(x, y, 2, 2);
    } else {
        game_draw_tiles(x, y, 2);
    }
    if (faction) {
        if (bombard && faction == cur_faction()) notice(0x687d98);
        if (owner < 0 || owner == faction || !(game_factions[faction].diplo_status[owner] & TREATY_COMMLINK) ||
            !seen_by(owner, tile(x, y)))
            goto done;
        synch_bit(x, y, owner);
        add_goal(owner, 0, 5, V->x, V->y, -1);
        if (owner != cur_faction()) goto done;
        int b = base_find_3(x, y, -1, -1, -1, cur_faction());
        say_noun(0, faction);
        game_parse_says(1, base_name(b), -1, -1);
        game_parse_says(3, game_faction_defs[faction].adjective, -1, -1);
        notice(bombard ? 0x687dac : 0x687dbc);
    } else {
        if (!seen_by(cur_faction(), tile(x, y)) || owner != cur_faction()) goto done;
        int b = base_find_3(x, y, -1, -1, -1, cur_faction());
        if (b < 0 || game_bases[b].faction != cur_faction()) goto done;
        game_parse_says(0, game_bases[b].name, -1, -1);
        game_parse_says(1, game_protos[V->proto].name, -1, -1);
        game_console_focus(game_console, x, y, 0);
        if (forest) {
            if (V->proto == 15) game_popp(*game_script_file, text(0x687ddc), 0, text(0x687dcc), 0);
            else notice(0x687de8);
        } else {
            if (V->proto == 15) game_popp(*game_script_file, text(0x687e04), 0, text(0x687df4), 0);
            else notice(0x687e14);
        }
    }
done:
    game_order_veh(veh, -1, 3);
}

// Artillery fire at (x, y), within two squares: the player's unit must
// have moves left. An empty square has what is on it destroyed; a unit of
// a faction without a pact is fought, if the artillery's range table
// reaches it.
void action_arty(int veh, int x, int y) {
    if (game_vehs[veh].faction == cur_faction() && !veh_ready(veh)) {
        notice(0x687fe4);
        return;
    }
    const struct veh *V = &game_vehs[veh];
    if (((x_dist(V->x, x) + abs(V->y - y)) & ~1) > 4) {
        notice(0x687ff0);
        return;
    }
    int s = stack_fix(veh_at(x, y));
    if (s < 0) {
        if (!*game_net_multiplayer) {
            action_destroy(veh, 0, x, y);
            return;
        }
        if (!game_net_lock_veh(game_net_daemon, &veh, 4, x, y, 0)) {
            game_message_data(0x2413, 0, veh, x, y, 0);
            game_net_await_exec(game_net_daemon, 1);
        }
        return;
    }
    V = &game_vehs[veh];
    int other = game_vehs[s].faction;
    if (other == V->faction || (game_factions[V->faction].diplo_status[other] & TREATY_PACT)) return;
    int dir = radius_move(V->x, V->y, x, y, game_radius_count[game_rules_basic->artillery_max_range]);
    if (dir < 0) return;
    if (!*game_net_multiplayer) {
        *game_veh_attack_flags = 3;
        game_battle_fight(veh, dir, 1, 1, 0);
        return;
    }
    if (game_net_maybe_lock(game_net_daemon, &veh, 0, 0, x, y, 4)) return;
    *game_veh_attack_flags = 1;
    if (game_battle_fight(veh, dir, 1, 1, 0)) {
        game_net_unlock_veh(game_net_daemon);
    } else {
        game_message_veh(0x2402, veh, dir, 0);
        game_net_await_exec(game_net_daemon, 1);
    }
}

// A unit blowing itself up: damage of its weapon (1 to 20) times its
// reactor, halved, to every unit on its square and the 8 round it but in
// bases, killing those it leaves without hit points.
void action_destruct(int veh) {
    const struct veh *V = &game_vehs[veh];
    int x = V->x, y = V->y;
    game_boom(x, y, 0);
    int w = weap_val(V->proto, V->faction);
    w = w < 1 ? 1 : w > 20 ? 20 : w;
    int damage = game_protos[V->proto].reactor * w / 2;
    kill(veh);
    for (int i = 0; i < 9; i++) {
        int px = xrange(x + game_radius_x[i]), py = y + game_radius_y[i];
        if (!on_map(px, py) || base_at(px, py) >= 0) continue;
        for (int v = veh_at(px, py); v >= 0; v = game_vehs[v].next_in_stack)
            game_vehs[v].damage = (uint8_t)(game_vehs[v].damage + damage);
        int s;
        while ((s = stack_fix(veh_at(px, py))) >= 0) {
            for (; s >= 0; s = game_vehs[s].next_in_stack) {
                const struct unit_proto *P = &game_protos[game_vehs[s].proto];
                int hp = P->plan == PLAN_ARTIFACT ? 1 : (P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor) * 10;
                if (game_vehs[s].damage >= hp) break;
            }
            if (s < 0) break;
            stack_veh(s, 1);
            stack_kill(s);
        }
    }
}

// A missile's strike at (x, y): factions with units in its reach (by its
// reactor, or a square for the fungal missile) and the territory's owner
// each have a chance to shoot it down (shoot_it), and the player is shown
// a missile at their base coming and its effect if they saw it. The
// original reads the attacker's faction (and the fungal missile its
// reactor) again from the unit's slot after the unit is gone, where the
// next unit now is.
static int missile_shot_down(int faction, int x, int y, int squares, int fungal) {
    int shot[FACTION_COUNT] = {0}, down = 0;
    for (int i = 0; i < squares; i++) {
        int px = xrange(x + game_radius_x[i]), py = y + game_radius_y[i];
        if (!on_map(px, py)) continue;
        int s = stack_fix(veh_at(px, py));
        if (s < 0) continue;
        int other = game_vehs[s].faction;
        if (other == faction || (game_factions[faction].diplo_status[other] & TREATY_PACT) || shot[other]) continue;
        down = game_shoot_it(faction, other, x, y, fungal);
        if (down) break;
        shot[other] = 1;
    }
    int owner = whose_territory(faction, x, y, NULL, 0);
    if (owner >= 0 && owner != faction && !shot[owner]) down = game_shoot_it(faction, owner, x, y, fungal);
    return down;
}

// A missile's warning to the player of a strike at their base.
static void missile_warning(int faction, int base, uint32_t label, uint32_t picture) {
    say_title(0, faction);
    say_leader(1, faction);
    say_noun(2, faction);
    game_parse_says(3, game_bases[base].name, -1, -1);
    game_popp(*game_script_file, text(label), 0, text(picture), 0);
}
static void missile_done(int veh, int base, uint32_t label, uint32_t picture) {
    game_parse_says(0, game_bases[base].name, -1, -1);
    say_title(1, game_vehs[veh].faction);
    say_leader(2, game_vehs[veh].faction);
    say_noun(3, game_vehs[veh].faction);
    game_popp(*game_script_file, text(label), 0, text(picture), 0);
}

// The tectonic payload: the ground at (x, y) raised once per reactor
// level, and the climate worked out again.
void action_tectonic(int veh, int x, int y) {
    const struct veh *V = &game_vehs[veh];
    game_fx_play_loc(game_fx, 0x53, V->x, V->y);
    int reactor = game_protos[V->proto].reactor, squares = game_radius_count[reactor];
    int base = base_find(x, y), faction = V->faction, seen = 0;
    if (faction != cur_faction() && seen_by(cur_faction(), tile(x, y)) && base >= 0 &&
        game_bases[base].faction == cur_faction()) {
        game_console_focus(game_console, x, y, faction);
        seen = 1;
    }
    faction = game_vehs[veh].faction;
    kill(veh);
    if (faction != cur_faction() && base >= 0) missile_warning(faction, base, 0x68821c, 0x68820c);
    if (missile_shot_down(faction, x, y, squares, 0)) return;
    for (int k = reactor; k > 0; k--) world_raise_alt(x, y);
    world_climate();
    game_draw_map(1);
    if (seen) missile_done(veh, base, 0x68823c, 0x68822c);
    game_tectonic_detonations[game_vehs[veh].faction]++;
    *game_draw_state |= 4;
}

// The fungal payload: fungus spread round (x, y), twice per reactor level
// at a random square of the 9 (the first at the target), with native life
// raised half the time where no faction has a unit or base, a fungal tower
// at a land target. Squares already in fungus are left as they are; others
// lose their improvements and come up to the shelf if deeper.
void action_fungal(int veh, int x, int y) {
    int faction = game_vehs[veh].faction;
    int base = base_find(x, y), seen = 0, squares = game_radius_count[1];
    game_fx_play_loc(game_fx, 0x54, game_vehs[veh].x, game_vehs[veh].y);
    kill(veh);
    if (faction != cur_faction() && base >= 0) missile_warning(faction, base, 0x68825c, 0x688248);
    if (missile_shot_down(faction, x, y, squares, 1)) return;
    int count = game_protos[game_vehs[veh].proto].reactor * 2;
    for (int i = 0; i < count; i++) {
        int k = i ? game_rand() % 8 + 1 : 0;
        int px = xrange(x + game_radius_x[k]), py = y + game_radius_y[k];
        if (!on_map(px, py)) continue;
        if (game_rand() % 2) {
            const struct map_tile *t = tile(px, py);
            if (!(t->bits & (MAP_BASE | MAP_UNIT)) || owner_of(t) >= 8) veh_init(8, 0, px, py);
        }
        if (fungus_on(tile(px, py))) continue;
        int vf = game_vehs[veh].faction, cur = cur_faction();
        if (vf != cur && seen_by(cur, tile(px, py)) && base >= 0 && game_bases[base].faction == cur) {
            game_console_focus(game_console, px, py, vf);
            seen = 1;
        }
        bit_set(px, py, MAP_FUNGUS, 1);
        bit_set(px, py, 0x28805c, 0);
        bit_set(px, py, *game_fungus_clears, 0);
        if (alt_of(tile(px, py)) < 0x40) world_alt_set(px, py, 2, 0);
        cur = cur_faction();
        if (seen_by(cur, tile(px, py)) || (*game_state & 0x80)) {
            synch_bit(px, py, cur);
            game_draw_tile(px, py, 2);
        }
    }
    if (alt_of(tile(x, y)) >= ALT_SHORE) veh_init(0x13, 0, x, y);
    *game_draw_state |= 4;
    if (seen) missile_done(veh, base, 0x688280, 0x68826c);
}

// ---- A unit's step (order_veh) --------------------------------------------------------
//
// A unit's move to one of the eight squares around it, or (a direction of
// 8 or more, or a negative one) a stay where it is. On the way: the AI
// kept out of others' territory, an artifact's menu at its own base,
// probe teams' missions, boarding transports, zones of control, a supply
// crawler's menu, native life rising from fungus, hidden units spotted,
// the attack (battle_fight) or the capture of native life, and the
// passengers a transport lets ashore; then, past the checks on a foreign
// base, the cost of the square (a land unit short of it may fail the
// move), and the move itself with what it brings: a base captured or the
// worms' attack on it, bunkers, Gibraltar's guns, sleeping passengers
// woken, fuel for aircraft, monoliths, supply pods, an artifact taken by
// its neighbours, and a unit that boarded a transport put to sleep.
// `type` (kept in veh_attack_flags) is 1 for a move the player sees and 2
// for one that happens (3 for both; 1 alone only weighs a fight's odds).
// Returns 1 if the unit moved or may go on, 0 if not or if a unit is gone.
//
// Thinker (MIT) has a version of it that this follows in outline. Where
// they differ the original is kept: a land unit can't board an ally's
// transport at sea (the move fails); a probe team sent home goes to the
// base of its faction with the most citizens (with stack_put); the
// facilities a worm attack may destroy are tried 101 times, among those
// that aren't free to faction 0; the check for a carrier in an aircraft's
// stack looks at the moving unit's own design each time; an artifact that
// pays for production goes on through the rest of the move, its number by
// then another unit's; the disembark menu comes up only for the player
// and tells of failure when it found passengers but none with moves left;
// one shore next to a transport is enough to wake its passengers; an
// aircraft at its range's end with moves left has its fuel checked when
// it can't move more than a road square's worth or has used up its turns.

enum {
    OV_PLAN_NAVAL_TRANSPORT = 7, OV_PLAN_COLONY = 8, OV_PLAN_TERRAFORM = 9, OV_PLAN_SUPPLY = 10,
    OV_PLAN_PROBE = 11, OV_PLAN_ARTIFACT = 12,
    OV_ABL_CLOAKED = 0x4, OV_ABL_AMPHIBIOUS = 0x8, OV_ABL_AIR_SUPERIORITY = 0x20, OV_ABL_CARRIER = 0x80,
    OV_BSC_ARTIFACT = 7, OV_BSC_MIND_WORMS = 8, OV_BSC_ISLE = 9, OV_BSC_SEALURK = 14, OV_BSC_SPORE_LAUNCHER = 15,
    OV_BSC_BATTLE_OGRE_1 = 16, OV_BSC_BATTLE_OGRE_3 = 18, OV_BSC_FUNGAL_TOWER = 19, OV_BASIC_PROTOS = 64,
    OV_CHASSIS_COPTER = 6,
    OV_STATE_MOVED = 0x4, OV_STATE_ZOC = 0x8, OV_STATE_UNK_40 = 0x40, OV_STATE_MONOLITH = 0x100,
    OV_STATE_ALERT = 0x200, OV_STATE_EXPLORE = 0x4000, OV_STATE_AUTO = OV_STATE_ALERT | OV_STATE_EXPLORE,
    OV_STATE_SLOWED = 0x8000, OV_STATE_FACING = 0x10000,
    OV_STATE_SENTRY_ALERT = 0x1000200, OV_STATE_PATROL = 0x2000200,
    OV_FLAG_PROBE_PACT = 0x4, OV_FLAG_OBJECTIVE = 0x20, OV_FLAG_INVISIBLE = 0x400, OV_FLAG_LURKER = 0x440,
    OV_FLAG_NO_CAPTURE = 0x2000,
    OV_TREATY_KEEP_OUT = 0xc11,  // pact, vendetta, shall betray, unk_800: the AI may go in
    OV_TREATY_TRUCE_TREATY = 0x6, OV_TREATY_ANY = 0x7,
    OV_MAP_BUNKER = 0x800, OV_MAP_SUPPLY_REMOVE = 0x100000,
    OV_PREF_QUICK_ENEMY = 0x4, OV_PREF_TUTORIAL = 0x20, OV_PREF_QUICK_ALLY = 0x8000, OV_PREF_AIR_HOME = 0x10000,
    OV_PREF_NO_CENTER = 0x80000,
    OV_MPREF_QUICK_ORDERS = 0x40, OV_MPREF_QUICK_ALL = 0x80,
    OV_RULES_NO_NATIVE = 0x200000,
    OV_VK_SHIFT = 0x10, OV_VK_CONTROL = 0x11,
    OV_SP_XENOEMPATHY_DOME = 8, OV_SP_UNIVERSAL_TRANSLATOR = 20, OV_FAC_NETWORK_NODE = 8, OV_FAC_PRESSURE_DOME = 26,
    OV_BASE_ARTIFACT_LINKED = 0x800,
    OV_FX_SPOTTED = 13, OV_FX_FUNGUS = 9, OV_FX_NONCOMBATANT = 0x33,
    OV_GOAL_TRANSPORT = 6, OV_WMODE_PROBE = 11,
    OV_BONUS_FACILITY = 2, OV_BONUS_FACILITY_TECH = 12,
};
#define OV_PREF_WAKE_ON_LAND 0x80000000u  // a transport's passengers woken at a shore

// What the original keeps in its frame through the move.
struct step {
    uint8_t *pop;  // its popup, where the original's frame has it
    int veh;       // another unit's number after a won fight, a capture or the disembark menu
    int dir;
    int faction;   // another after an artifact is taken
    int fbit;      // the faction's bit as it was at first
    int vx, vy, tx, ty, region;
    int stack, base, tfac;  // the target square's units, base and faction (-1 for none)
    int from_sea, to_sea, goody, count;
    int result;        // what it returns
    int boarded;       // a land unit boarded its faction's transport out at sea
    int fought;        // it attacked
    int refuel_check;  // it fought and won: an aircraft with a range of 1 checks its fuel
    int probing;       // a probe team acted on a unit, or units were spotted
    int slow;          // it moves on its own (exploring or on alert); later, shown at full pace
    int seen;          // the player sees it (on some paths the original never sets it; see order_veh)
    int owner;         // the bunker's or the last Gibraltar gun's faction
    int cost;          // the square's cost
    int triad;
};

enum next { GO_ON, GO_END, GO_RETURN_0, GO_RETURN_1, GO_SPOT, GO_NAVAL, GO_UPKEEP, GO_LOOP, GO_ARRIVE };

// diplo_status of one faction towards another, read as the original does
// even for no faction (-1).
static inline uint32_t treaty_of(int faction, int other) {
    return *(const uint32_t *)((const char *)game_factions[faction].diplo_status + 4 * other);
}
// A base by number, also for no base (-1), as the original reads it.
static inline struct base *base_ref(int b) { return (struct base *)((char *)game_bases + b * (int)sizeof(struct base)); }
static inline int sea_at(int x, int y) { return alt_of(tile(x, y)) < ALT_SHORE; }
static inline int base_on(const struct map_tile *t) { return faction_on(t, MAP_BASE); }
static inline int is_human_(int faction) { return (*game_human_factions >> faction) & 1; }
static inline int attack_flags(void) { return *game_veh_attack_flags; }
static inline const struct unit_proto *veh_design(int veh) { return &game_protos[game_vehs[veh].proto]; }
static inline int veh_offense(int veh) { return game_weapons[veh_design(veh)->weapon].offense; }
static inline int veh_plan(int veh) { return veh_design(veh)->plan; }
static inline int veh_range(int veh) { return game_chassis[veh_design(veh)->chassis].range; }
static inline int key_down(int key) { return (game_get_async_key_state(key) & 0xff00) != 0; }
static inline int top_of(int veh) {
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    return veh;
}
static inline void say_name(int slot, int veh) { game_parse_says(slot, game_protos[game_vehs[veh].proto].name, -1, -1); }
static inline void say_adjective(int slot, int faction) {
    game_parse_says(slot, game_faction_defs[faction].adjective, -1, -1);
}
static inline void say_ability(uint32_t ability) {
    game_parse_say(0, (int)(intptr_t)game_abilities[abil_index(ability)].name, -1, -1);
}
static inline void step_synch(int veh) {
    game_synch_veh(veh);
    game_net_await_synch(game_net_daemon);
}
static inline int ask_x(uint32_t label) { return ((int (*)(char *, int))game_x_pop)(text(label), 0); }
static inline int ask(uint32_t label, uint32_t picture) { return game_popp(*game_script_file, text(label), 0, text(picture), 0); }
static inline char *next_line(void) { return ((char *(*)(void))game_text_get)(); }
// A line of a menu, put together in the battle text.
static void menu_item(struct step *s, char *line, int id) {
    game_parse_string(line, game_battle_text);
    game_dialogs_item(s->pop + 0x21d0, game_battle_text, id);
}
// The player's units' messages, for a move the player sees.
static inline int told(const struct step *s) { return s->faction == *game_current_faction && (attack_flags() & 1); }

// The AI doesn't go into the territory of a faction it has none of
// OV_TREATY_KEEP_OUT with: its order ends.
static enum next step_territory(struct step *s) {
    struct veh *V = &game_vehs[s->veh];
    if ((*game_human_factions & s->fbit) || V->order != ORDER_MOVE_TO) return GO_ON;
    s->owner = whose_territory(s->faction, V->waypoint_x[0], V->waypoint_y[0], NULL, 0);
    if (s->owner >= 0 && s->owner != s->faction && !(treaty_of(s->faction, s->owner) & OV_TREATY_KEEP_OUT)) {
        V->order = ORDER_NONE;
        return GO_END;
    }
    return GO_ON;
}

// An artifact entering its faction's base (for a move the player sees):
// what to do with it, kept in movement_turns (1 to link it to a Network
// Node, or study it at the Universal Translator, 2 to put it into
// production). The AI takes the last choice offered, the player is asked.
static void step_artifact_menu(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (veh_plan(veh) != OV_PLAN_ARTIFACT || (V->flags & OV_FLAG_OBJECTIVE) || !(attack_flags() & 1)) return;
    V->movement_turns = 0;
    if (s->base < 0 || game_bases[s->base].faction != s->faction) return;
    struct base *B = &game_bases[s->base];
    int human = (*game_human_factions & s->fbit) != 0;
    game_parse_says(0, B->name, -1, -1);
    game_popup_start(s->pop, game_pop_file, text(0x68fc90), -1, 0, 0, 0);
    int w, h;
    if (!game_pcx_dimensions(text(0x68fc9c), &w, &h) && !game_sprite_init(s->pop + 0x2118, text(0x68fcac), w, h))
        *(uint8_t **)(s->pop + 0x2144) = s->pop + 0x2118;
    game_parse_num(0, B->minerals);
    int item = B->queue[0], cost;
    if (item < 0) {
        cost = cost_factor(s->faction, 1, -1) * game_facilities[-item].cost;
    } else {
        cost = veh_cost(item, s->base, NULL);
        cost *= cost_factor(s->faction, 1, -1);
    }
    game_parse_num(1, cost);
    if (game_x_text_open(*game_script_file, text(0x68fcbc))) return;
    *game_battle_text = 0;
    menu_item(s, next_line(), 0);
    int items = 1;
    char *line = next_line();
    int offset;
    uint32_t mask;
    bitmask(OV_FAC_NETWORK_NODE, &offset, &mask);
    if ((B->facilities_built[offset] & mask) && game_secret_projects[OV_SP_UNIVERSAL_TRANSLATOR] != s->base &&
        !(B->state & OV_BASE_ARTIFACT_LINKED)) {
        *game_battle_text = 0;
        menu_item(s, line, items++);
        if (!human) V->movement_turns = 1;
    }
    line = next_line();
    if (game_secret_projects[OV_SP_UNIVERSAL_TRANSLATOR] == s->base) {
        *game_battle_text = 0;
        menu_item(s, line, 1);
        items++;
        if (!human) V->movement_turns = 1;
    }
    line = next_line();
    if (item <= -FAC_FIRST_PROJECT) {
        *game_battle_text = 0;
        game_parse_say(1, (int)(intptr_t)game_facilities[-item].name, -1, -1);
        menu_item(s, line, 2);
        items++;
        if (!human) V->movement_turns = 2;
    }
    line = next_line();
    if (item >= OV_BASIC_PROTOS && !(game_protos[item].flags & PROTO_FLAG_BUILT)) {
        *game_battle_text = 0;
        game_parse_says(2, game_protos[item].name, -1, -1);
        menu_item(s, line, 2);
        items++;
    }
    if (items > 1 && s->faction == *game_current_faction && human) {
        V->movement_turns = (uint8_t)game_base_pop_exec_3(s->pop, 0, 0);
        step_synch(veh);
    }
}

// A stay (no direction). An aircraft of the player's, away from a base,
// an airbase or a carrier (the original asks the moving unit's design
// each time round its stack) or a supply crawler aloft, with moves left
// and a range of 1 or turns spent aloft, heads home; on its way it
// returns 0. Otherwise it stays put (1), skipping its turn.
static enum next step_stay(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    const struct rules_chassis *C = chassis_of(V);
    if (C->triad == TRIAD_AIR && C->range && (attack_flags() & 1) && s->faction == *game_current_faction) {
        const struct map_tile *t = tile(s->tx, s->ty);
        if (!base_on(t) && !(t->bits & MAP_AIRBASE)) {
            for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack) {
                if (has_abil(V->proto, OV_ABL_CARRIER)) goto stay;
                if (veh_plan(u) == OV_PLAN_SUPPLY && game_veh_triad(u) == TRIAD_AIR) goto stay;
            }
            if (moves_left(veh) && (chassis_of(V)->range == 1 || V->movement_turns)) {
                action_home(veh, 0);
                if (V->order == ORDER_MOVE_TO) {
                    if (net_turn()) step_synch(veh);
                    return GO_RETURN_0;
                }
            }
        }
    }
stay:
    s->result = 1;
    if (!(attack_flags() & 2)) return GO_END;
    veh_skip(veh);
    V->state &= ~(uint32_t)OV_STATE_UNK_40;
    return GO_UPKEEP;
}

// A probe team moving onto another faction's units or base. For the
// player, a pact asks first. Units in the open (without a pact, and
// without a probe team of their own) or a base (without a probe team, for
// the AI only without a pact unless the base is its goal) get the team's
// mission; in a network game a mission that happened ends the move (1).
static enum next step_probe(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (veh_plan(veh) != OV_PLAN_PROBE || s->tfac < 0 || s->faction == s->tfac) return GO_ON;
    if (attack_flags() & 1) {
        V->flags &= ~OV_FLAG_PROBE_PACT;
        if ((treaty_of(s->faction, s->tfac) & TREATY_PACT) && !s->slow && s->faction == *game_current_faction) {
            if (s->base >= 0) {
                game_parse_says(0, game_bases[s->base].name, -1, -1);
                say_adjective(1, s->tfac);
                if (!ask_x(0x68fccc)) return GO_ON;
                V->flags |= OV_FLAG_PROBE_PACT;
            } else if (stack_check(s->stack, 2, OV_PLAN_PROBE, s->tfac, -1)) {
                say_adjective(0, s->tfac);
                if (!ask_x(0x68fcd8)) return GO_ON;
                V->flags |= OV_FLAG_PROBE_PACT;
            }
        }
    }
    if (s->base < 0) {
        if (treaty_of(s->faction, s->tfac) & TREATY_PACT) return GO_ON;
        if (s->stack >= 0 && stack_check(s->stack, 2, OV_PLAN_PROBE, -1, -1)) return GO_ON;
        s->probing = 1;
        int done = game_probe(veh, -1, s->stack, 1);
        int flags = attack_flags();
        if (done && net_turn() && (flags & 1) && !(flags & 2)) {
            step_synch(veh);
            return GO_RETURN_1;
        }
        if (*game_veh_count != s->count) return GO_RETURN_0;
        if (s->stack >= 0) s->tfac = game_vehs[s->stack].faction;
        return GO_ON;
    }
    if (s->stack >= 0 && stack_check(s->stack, 2, OV_PLAN_PROBE, -1, -1)) return GO_ON;
    const struct base *B = &game_bases[s->base];
    if (!(*game_human_factions & s->fbit) && (treaty_of(s->faction, s->tfac) & TREATY_PACT) &&
        (V->waypoint_x[0] != B->x || V->waypoint_y[0] != B->y))
        return GO_ON;
    if (s->tfac == *game_current_faction) {
        game_console_focus(game_console, s->vx, s->vy, s->faction);
        if (!*game_net_multiplayer) game_clock_wait(400);
        stack_veh(veh, 1);
        game_veh_scoot(veh, s->vx, s->vy, s->dir, 0);
        stack_put(veh, s->vx, s->vy);
        game_draw_tile(s->tx, s->ty, 2);
    }
    if (!game_probe(veh, s->base, -1, 1)) return GO_END;
    int flags = attack_flags();
    if (!net_turn() || !(flags & 1) || (flags & 2)) return GO_END;
    step_synch(veh);
    return GO_RETURN_1;
}

// A land unit headed out to sea: onto its faction's transports with room
// (in the open it boards), into its faction's base on the shore if
// amphibious or with a transport there or in its own stack; into another
// faction's sea base if amphibious; onto a stack in the open only an air
// superiority unit against aircraft with no carrier, as an attack. A
// pact is no way aboard.
static enum next step_to_sea(struct step *s) {
    if (s->triad != TRIAD_LAND || !s->to_sea) return GO_ON;
    int veh = s->veh, proto = game_vehs[veh].proto;
    if (s->faction == s->tfac) {
        if (stack_check(s->stack, 8, s->faction, -1, -1) <= 0) {
            if (s->base < 0) {
                int n = stack_check(s->stack, 2, OV_PLAN_NAVAL_TRANSPORT, -1, -1);
                if (n && told(s)) notice(stack_check(s->stack, 2, OV_PLAN_NAVAL_TRANSPORT, -1, -1) > 1 ? 0x68fd00 : 0x68fd10);
                return GO_END;
            }
            if (!has_abil(proto, OV_ABL_AMPHIBIOUS) && !stack_check(s->stack, 2, OV_PLAN_NAVAL_TRANSPORT, -1, -1) &&
                !stack_check(veh, 2, OV_PLAN_NAVAL_TRANSPORT, -1, -1)) {
                if (told(s)) {
                    say_ability(OV_ABL_AMPHIBIOUS);
                    notice(0x68fcf4);
                }
                return GO_END;
            }
        }
        if (s->base < 0) s->boarded = 1;
        return GO_ON;
    }
    if (treaty_of(s->faction, s->tfac) & TREATY_PACT) return GO_END;
    if (s->base >= 0) {
        if (has_abil(proto, OV_ABL_AMPHIBIOUS)) return GO_ON;
        if (told(s)) {
            say_ability(OV_ABL_AMPHIBIOUS);
            notice(0x68fce8);
        }
        return GO_END;
    }
    if (s->stack < 0 || !has_abil(proto, OV_ABL_AIR_SUPERIORITY) || !stack_check(s->stack, 18, -1, -1, -1) ||
        stack_check(s->stack, 6, OV_ABL_CARRIER, -1, -1))
        return GO_END;
    return GO_SPOT;
}

// Zones of control: a land unit (not a probe team, not cloaked) going
// between two squares next to enemies, onto an empty one, can't. The
// player hears of it (with the tutorial's note at times) and enemies
// around the square it wanted are spotted.
static enum next step_zoc(struct step *s) {
    if (s->from_sea || s->to_sea) return GO_ON;
    int veh = s->veh;
    const struct unit_proto *P = veh_design(veh);
    if (game_chassis[P->chassis].triad != TRIAD_LAND || (P->abilities & OV_ABL_CLOAKED) || P->plan == OV_PLAN_PROBE ||
        s->stack >= 0)
        return GO_ON;
    if (!zoc_move(s->vx, s->vy, s->faction) || !zoc_move(s->tx, s->ty, s->faction)) return GO_ON;
    if (s->faction != *game_current_faction || s->slow || !(attack_flags() & 1)) return GO_END;
    game_fx_play(game_fx, OV_FX_SPOTTED);
    if (*game_net_multiplayer) return GO_END;
    if ((*game_preferences & OV_PREF_TUTORIAL) ||
        (game_factions[s->faction].diff_level <= 1 && (++*game_zoc_tutorial_count & 3) == 1)) {
        say_ability(OV_ABL_CLOAKED);
        int ax, ay, bx, by;
        quick_zoc(s->vx, s->vy, s->faction, s->tx, s->ty, &ax, &ay);
        quick_zoc(s->tx, s->ty, s->faction, s->vx, s->vy, &bx, &by);
        game_tut_map(game_tut_win, text(0x68fd20), ax, ay, veh, NULL, 0, bx, by);
        if (*game_net_multiplayer) return GO_END;
    }
    s->probing = 0;
    for (int i = 0; i < 8; i++) {
        int x = xrange(s->tx + game_radius_near_x[i]), y = s->ty + game_radius_near_y[i];
        if (!on_map(x, y)) continue;
        int u = stack_fix(veh_at(x, y));
        if (u < 0 || game_vehs[u].faction == s->faction) continue;
        if ((treaty_of(s->faction, game_vehs[u].faction) & TREATY_PACT) || (s->fbit & game_vehs[u].visibility)) continue;
        spot_loc(x, y, s->faction);
        s->probing = 1;
    }
    if (s->probing) game_draw_radius(s->tx, s->ty, 1, 2);
    return GO_END;
}

// supply_options(): a supply crawler's menu at a base, for the player.
// The AI is offered nothing (0), on a popup made and taken down at once
// where the original's frame has it (the call's frame lies 0x542c below
// order_veh's, and its popup 0x53a0 below that).
static int supply_menu(struct step *s, int veh, int base) {
    if (is_human_(game_vehs[veh].faction)) return game_supply_options(veh, base);
    uint8_t *pop = s->pop + 0x5410 - 0xa7dc;
    game_popup_ctor(pop);
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
    return 0;
}
static enum next step_supply(struct step *s) {
    int veh = s->veh;
    if (veh_plan(veh) != OV_PLAN_SUPPLY || s->base < 0 || *game_net_multiplayer) return GO_ON;
    int chosen = supply_menu(s, veh, s->base);
    if (attack_flags() & 2) return chosen ? GO_END : GO_ON;
    if (!chosen) return GO_END;
    if (net_turn()) {
        step_synch(veh);
        return GO_RETURN_1;
    }
    return GO_ON;
}

// Native life rising from fungus a faction's unit (not aircraft) moves
// onto, on its own kind of square, empty, without a tube or a pod, more
// than a square from any base, unless native life is off or the
// Xenoempathy Dome is the faction's and the unit is unarmed or on its way
// somewhere. The chance grows with the distance to the nearest base and
// the native life setting, and is halved for a square native life rose
// from before, the sea, roads and native units, and for every sensor and
// base within two squares (quartered within one). Mind worms, or a spore
// launcher (one in five with Alien Crossfire), or at sea an Isle of the
// Deep; the unit stops.
static enum next step_native(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (!(attack_flags() & 2) || game_veh_triad(veh) == TRIAD_AIR || s->stack >= 0 || !s->faction) return GO_SPOT;
    const struct map_tile *t = tile(s->tx, s->ty);
    if (!fungus_on(t) || (t->bits & MAP_MAGTUBE)) return GO_SPOT;
    if (!veh_offense(veh) || V->order == ORDER_MOVE_TO) {
        int dome = game_secret_projects[OV_SP_XENOEMPATHY_DOME];
        if (dome >= 0 && game_bases[dome].faction == s->faction) return GO_SPOT;
    }
    if (s->from_sea != s->to_sea || goody_at(s->tx, s->ty) || (*game_rules & OV_RULES_NO_NATIVE)) return GO_SPOT;
    base_find(s->tx, s->ty);
    int dist = *game_base_find_dist;
    if (dist <= 1) return GO_SPOT;
    int chance = (dist < 0 ? 0 : dist > 32 ? 32 : dist) + 10 * *game_native_life;
    uint32_t bits = t->bits;
    if (bits & OV_MAP_SUPPLY_REMOVE) {
        chance /= 2;
        if (!*game_native_life) chance /= 2;
    }
    if (s->to_sea) {
        chance /= 2;
        if (!*game_native_life) chance /= 2;
    }
    if (bits & MAP_ROAD) chance /= 2;
    int proto = V->proto;
    if (proto < OV_BASIC_PROTOS && (game_weapons[game_protos[proto].weapon].offense < 0 || proto == OV_BSC_SPORE_LAUNCHER))
        chance /= 2;
    for (int i = 0; i < 25; i++) {
        int x = xrange(s->tx + game_radius_x[i]), y = s->ty + game_radius_y[i];
        if (!on_map(x, y)) continue;
        if (is_sensor(x, y) || base_on(tile(x, y))) chance = i < 9 ? chance / 4 : chance / 2;
    }
    s->seen = chance;
    if (game_rand() % 100 >= chance) return GO_SPOT;
    if (s->to_sea) proto = OV_BSC_ISLE;
    else if (!(game_rand() % 5) && *game_expansion) proto = OV_BSC_SPORE_LAUNCHER;
    else proto = OV_BSC_MIND_WORMS;
    s->stack = veh_init(proto, 0, s->tx, s->ty);
    if (s->stack < 0) return GO_NAVAL;
    game_vehs[veh].order = ORDER_NONE;
    spot_stack(s->stack, s->faction);
    game_draw_tile(s->tx, s->ty, 2);
    bit_set(s->tx, s->ty, OV_MAP_SUPPLY_REMOVE, 1);
    if (s->faction == *game_current_faction) {
        game_fx_play(game_fx, OV_FX_SPOTTED);
        say_adjective(0, s->tfac);
        say_name(1, s->stack);
        notice(s->to_sea ? 0x68fd34 : 0x68fd40);
    }
    return GO_END;
}

// A native unit captured instead of fought (for a faction with Planet,
// not by aircraft, not an artifact or fungal tower). The worms are
// agitated (the attack goes ahead) if the faction has them on its own
// orders or the nearest of its bases has eco-damage; once refused, a unit
// stays refused. Planet's chance, then a turn-weighted roll against the
// prototypes the faction already has (or one in ten); or a faction whose
// own Planet rating is positive and that has none of the kind. The stack
// changes sides, homed at the nearest base if the faction's and of size 3.
// Returns GO_ON for an attack.
static enum next step_capture(struct step *s) {
    int veh = s->veh, st = s->stack;
    int proto = game_vehs[st].proto;
    if (s->tfac || game_veh_triad(veh) == TRIAD_AIR || proto == OV_BSC_ARTIFACT || proto == OV_BSC_FUNGAL_TOWER)
        return GO_ON;
    struct faction_data *F = &game_factions[s->faction];
    if (F->se[SE_PLANET] <= 0) return GO_ON;
    if (moves_left(veh) < game_rules_basic->move_rate_roads && (F->se_base[SE_PLANET] <= 0 || F->units_active[proto]))
        return GO_ON;
    int near = base_find_2(s->tx, s->ty, s->faction);
    if (!(attack_flags() & 2)) return GO_ON;
    if (game_vehs[st].order_auto_type == s->faction || (near >= 0 && game_bases[near].eco_damage)) {
        game_log_say(text(0x68ff58), 0, 0, 0);
        if (s->faction == *game_current_faction) notice(0x68ff64);
        return GO_ON;
    }
    game_log_say(text(0x68fe54), s->faction, st, 0);
    game_log_say(text(0x68fe6c), F->se_base[SE_PLANET], F->units_active[proto], 0);
    game_log_say(text(0x68fe84), proto, F->se[SE_PLANET], 0);
    int capture = 0;
    if (!(game_vehs[st].flags & OV_FLAG_NO_CAPTURE)) {
        if (game_rand() % 4 < F->se[SE_PLANET] &&
            (game_rnd(*game_turn / 5 + 100) >= 10 * F->units_active[proto] || !(game_rand() % 10)))
            capture = 1;
        else if (F->se_base[SE_PLANET] > 0 && !F->units_active[proto])
            capture = 1;
    }
    if (!capture) {
        game_vehs[st].flags |= OV_FLAG_NO_CAPTURE;
        if (s->faction == *game_current_faction) notice(0x68ff3c);
        game_log_say(text(0x68ff4c), 0, 0, 0);
        return GO_ON;
    }
    for (int u = top_of(st); u >= 0; u = game_vehs[u].next_in_stack) {
        struct veh *U = &game_vehs[u];
        U->faction = (uint8_t)s->faction;
        U->morale = (uint8_t)morale_alien(u, 0);
        int b = base_find(s->tx, s->ty);
        const struct base *B = base_ref(b);
        U->home_base = (int16_t)(B->faction == s->faction && B->pop_size >= 3 ? b : -1);
        veh_skip(u);
    }
    bit_set(s->tx, s->ty, OV_MAP_SUPPLY_REMOVE, 1);
    owner_set(s->tx, s->ty, s->faction);
    if (s->faction == *game_current_faction) game_draw_tile_fixup(s->tx, s->ty, 1, 2);
    else game_draw_tile(s->tx, s->ty, 2);
    if (s->faction == *game_current_faction) {
        say_name(0, st);
        proto = game_vehs[st].proto;
        if (proto == OV_BSC_SPORE_LAUNCHER) {
            if (!F->units_active[OV_BSC_SPORE_LAUNCHER]) ask(0x68febc, 0x68feac);
            else notice(0x68fec8);
        } else if (proto == OV_BSC_SEALURK) {
            if (!F->units_active[OV_BSC_SEALURK]) ask(0x68fee4, 0x68fed4);
            else notice(0x68fef0);
        } else if (!F->units_active[proto]) {
            ask(0x68ff18, sea_at(s->tx, s->ty) ? 0x68fefc : 0x68ff08);
        } else {
            notice(0x68ff24);
        }
    }
    game_log_say(text(0x68ff30), 0, 0, 0);
    return GO_NAVAL;
}

// A probe team met at truce or treaty, alone in the open: near the
// faction's own base it is sent home (the AI always, the player chooses),
// elsewhere the player may let it be. Sent home, it goes to the base of
// its faction with the most citizens, or is lost if it has none (unless
// human).
static enum next step_enemy_probe(struct step *s) {
    int veh = s->veh, st = s->stack;
    int choice;
    int near = base_find(s->tx, s->ty);
    if (near >= 0 && game_bases[near].faction == s->faction) {
        say_adjective(0, s->tfac);
        say_name(1, st);
        say_title(2, s->faction);
        if (!(*game_human_factions & s->fbit)) {
            choice = 1;
        } else {
            int alien = game_faction_defs[s->tfac].rule_flags & RULE_FLAG_ALIEN;
            choice = alien ? ask(0x68fdc4, 0x68fdb4) : ask(0x68fde0, 0x68fdd0);
            if (!choice) return GO_END;
        }
        if (choice != 1) return GO_ON;
    } else {
        if (s->faction != *game_current_faction) return GO_ON;
        say_adjective(0, s->tfac);
        say_name(1, st);
        say_title(2, s->faction);
        int alien = game_faction_defs[s->tfac].rule_flags & RULE_FLAG_ALIEN;
        choice = alien ? ask(0x68fe18, 0x68fe08) : ask(0x68fe34, 0x68fe24);
        return choice ? GO_ON : GO_END;
    }
    if (s->faction == *game_current_faction || s->tfac == *game_current_faction) {
        game_console_focus(game_console, s->vx, s->vy, s->faction);
        stack_veh(veh, 1);
        game_veh_scoot(veh, s->vx, s->vy, s->dir, 0);
        stack_put(veh, s->vx, s->vy);
        game_draw_tile(s->vx, s->vy, 2);
    }
    int home = -1, most = -1;
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == s->tfac && game_bases[b].pop_size > most) most = game_bases[b].pop_size, home = b;
    if (home >= 0) {
        stack_put(st, game_bases[home].x, game_bases[home].y);
        game_vehs[st].visibility = 0;
        game_parse_says(2, game_bases[home].name, -1, -1);
    } else if (!is_human_(s->tfac)) {
        veh_kill(st);
    }
    game_draw_tile(s->tx, s->ty, 2);
    if (s->tfac == *game_current_faction) {
        say_adjective(0, s->faction);
        say_title(1, s->tfac);
        notice(0x68fdec);
    } else if (s->faction == *game_current_faction) {
        say_adjective(0, s->tfac);
        say_title(1, s->faction);
        notice(0x68fdf8);
    }
    return GO_END;
}

// Spotted (a hidden unit found on the square): the unit stops.
static enum next spotted(struct step *s, uint32_t label, uint32_t label0, int tell) {
    game_vehs[s->veh].order = ORDER_NONE;
    spot_stack(s->stack, s->faction);
    game_draw_tile(s->tx, s->ty, 2);
    if (tell) {
        game_fx_play(game_fx, OV_FX_SPOTTED);
        say_adjective(0, s->tfac);
        say_name(1, s->stack);
        notice(s->tfac ? label : label0);
    }
    return GO_END;
}

// Units on the target square. Its own faction's or an ally's (unless a
// probe team on a mission) are joined. Otherwise, after a probe team's
// mission nothing more; a hidden unit is spotted; units the player can't
// see are found by the player's units in the open; aircraft in the open
// can only be attacked by air superiority units unless a carrier is
// with them; sea units don't attack land (but by artillery or unarmed),
// nor land units from the sea without amphibious pods; the unarmed don't
// attack; then probe teams at truce, haste, and the attack itself (only
// its odds for a move that doesn't happen), or the capture of native life.
static enum next step_occupied(struct step *s) {
    if (s->stack < 0) return GO_NAVAL;
    int veh = s->veh, st = s->stack;
    struct veh *S = &game_vehs[st];
    s->seen = st * (int)sizeof(struct veh);
    uint16_t sflags = S->flags;
    if (!(sflags & OV_FLAG_INVISIBLE)) {
        if (s->faction == s->tfac) return GO_NAVAL;
        if ((treaty_of(s->faction, s->tfac) & TREATY_PACT) &&
            (veh_plan(veh) != OV_PLAN_PROBE || !(game_vehs[veh].flags & OV_FLAG_PROBE_PACT)))
            return GO_NAVAL;
    }
    if (s->probing) return GO_END;
    if (sflags & OV_FLAG_INVISIBLE) return spotted(s, 0x68fd50, 0x68fd58, s->faction == *game_current_faction);
    if ((S->faction != s->faction || (sflags & OV_FLAG_LURKER) == OV_FLAG_LURKER) && !(s->fbit & S->visibility) &&
        (*game_human_factions & s->fbit)) {
        if (s->base >= 0) goto armed;
        if (told(s)) return spotted(s, 0x68fd64, 0x68fd6c, 1);
    }
    if (s->base < 0 && !(tile(s->tx, s->ty)->bits & MAP_AIRBASE) && stack_check(st, 18, -1, -1, -1) &&
        !has_abil(game_vehs[veh].proto, OV_ABL_AIR_SUPERIORITY) && !stack_check(st, 6, OV_ABL_CARRIER, -1, -1)) {
        if (told(s)) {
            say_ability(OV_ABL_AIR_SUPERIORITY);
            notice(0x68fd78);
        }
        if (game_vehs[veh].state & OV_STATE_AUTO) veh_skip(veh);
        return GO_END;
    }
armed:;
    int proto = game_vehs[veh].proto;
    if (proto == OV_BSC_SEALURK || s->from_sea) {
        if (proto != OV_BSC_SEALURK && !s->to_sea && s->triad == TRIAD_SEA && !can_arty(proto, 1) && veh_offense(veh)) {
            if (told(s)) notice(0x68fd84);
            return GO_END;
        }
        if (s->from_sea && s->triad == TRIAD_LAND && !has_abil(game_vehs[veh].proto, OV_ABL_AMPHIBIOUS) &&
            veh_plan(veh) != OV_PLAN_PROBE) {
            abil_index(OV_ABL_AMPHIBIOUS);
            if (told(s)) {
                say_ability(OV_ABL_AMPHIBIOUS);
                notice(base_on(tile(s->vx, s->vy)) ? 0x68fd8c : 0x68fd98);
            }
            return GO_END;
        }
    }
    if (game_weapons[veh_design(veh)->weapon].mode != OV_WMODE_PROBE || !stack_check(st, 2, OV_PLAN_PROBE, -1, -1)) {
        if (!veh_offense(veh)) {
            if (told(s)) {
                game_fx_play(game_fx, OV_FX_NONCOMBATANT);
                notice(0x68fda4);
            }
            return GO_END;
        }
        if (veh_plan(st) == OV_PLAN_PROBE && stack_check(st, 1, -1, -1, -1) == 1 && !*game_net_multiplayer) {
            uint32_t d = treaty_of(s->faction, s->tfac);
            if ((d & OV_TREATY_TRUCE_TREATY) && !(d & TREATY_PACT)) {
                enum next n = step_enemy_probe(s);
                if (n != GO_ON) return n;
            }
        }
    }
    int rate = game_rules_basic->move_rate_roads;
    proto = game_vehs[veh].proto;
    if (moves_left(veh) < rate && proto != OV_BSC_SPORE_LAUNCHER && proto != OV_BSC_MIND_WORMS) {
        if (s->faction == *game_current_faction && !s->slow && (attack_flags() & 1)) {
            say_name(0, veh);
            game_parse_num(0, moves_left(veh));
            game_parse_num(1, game_rules_basic->move_rate_roads);
            if (ask(0x68fe4c, 0x68fe40)) return GO_END;
        } else if (!(*game_human_factions & s->fbit)) {
            if (moves_left(veh) <= game_rules_basic->move_rate_roads / 2) return GO_END;
        }
    }
    *game_current_veh = veh;
    if (!(attack_flags() & 2)) {
        s->result = battle_fight(veh, s->dir, 0, 1, NULL) == 0;
        return GO_END;
    }
    enum next n = step_capture(s);
    if (n != GO_ON) return n;
    s->fought = 1;
    int won = battle_fight(veh, s->dir, 0, 1, NULL);
    if (!won) return GO_END;
    if (won != 1) {
        s->result = 0;
        return GO_END;
    }
    s->veh = *game_current_veh;
    s->tx = s->vx, s->ty = s->vy;
    s->refuel_check = 1;
    s->goody = 0;
    return GO_UPKEEP;
}

// Shore and sea. A land unit leaving a base at sea for land (not
// amphibious, onto an empty square) needs a transport in its stack. A
// ship can't go onto land but into a base: for the player, it offers its
// passengers with moves left the way ashore, and the move begins again
// with the one chosen.
static enum next step_naval(struct step *s) {
    int veh = s->veh;
    if (s->from_sea) {
        if (s->to_sea) return GO_ARRIVE;
        if (s->triad == TRIAD_LAND && s->stack < 0 && !has_abil(game_vehs[veh].proto, OV_ABL_AMPHIBIOUS) &&
            base_on(tile(s->vx, s->vy)) && !stack_check(veh, 2, OV_PLAN_NAVAL_TRANSPORT, -1, -1)) {
            if (told(s)) {
                say_ability(OV_ABL_AMPHIBIOUS);
                notice(0x68ff74);
            }
            return GO_END;
        }
    } else if (s->to_sea) {
        return GO_ARRIVE;
    }
    if (s->base >= 0 || s->triad != TRIAD_SEA) return GO_ARRIVE;
    if (*game_net_multiplayer || !told(s)) return GO_END;
    game_vehs[veh].order = ORDER_NONE;
    say_name(0, veh);
    game_popup_start(s->pop, game_pop_file, text(0x68ff80), -1, 0, 0, 0);
    if (game_x_text_open(*game_script_file, text(0x68ff8c))) return GO_END;
    next_line();
    stack_veh(veh, 0);
    int last = -1, aboard = 0;
    for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack) {
        const struct veh *U = &game_vehs[u];
        if (U->order != ORDER_SENTRY_BOARD || U->waypoint_x[0] != veh) continue;
        aboard++;
        if (!moves_left(u)) continue;
        last = u;
        *game_battle_text = 0;
        say_name(1, u);
        menu_item(s, *game_text_line, u + 1);
    }
    if (last < 0) {
        if (aboard) notice(0x68ff9c);
        return GO_END;
    }
    int chosen = game_base_pop_exec_3(s->pop, 0, 0) - 1;
    if (chosen < 0) return GO_END;
    s->veh = chosen;
    game_vehs[chosen].order = ORDER_NONE;
    return GO_LOOP;
}

// Into another faction's base (not an ally's): aircraft with a range and
// ships can't take it (a ship not even from the sea), nor land units at
// sea without amphibious pods, nor the unarmed. At truce or better the AI
// stops, and the player is asked to break the treaty. The player's units
// on their way somewhere are asked before stopping at a supply pod.
static enum next step_conquer(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (s->base >= 0 && s->tfac != s->faction && !(treaty_of(s->faction, s->tfac) & TREATY_PACT)) {
        const struct unit_proto *P = veh_design(veh);
        if (s->triad == TRIAD_AIR) {
            if (game_chassis[P->chassis].range) {
                if (told(s)) {
                    game_parse_say(0, (int)(intptr_t)game_chassis[P->chassis].offsv1_name, -1, -1);
                    notice(0x68ffac);
                }
                return GO_END;
            }
        } else if (s->triad == TRIAD_SEA) {
            if (!s->to_sea) {
                if (told(s)) notice(0x68ffb8);
                return GO_END;
            }
        } else if (s->triad == TRIAD_LAND) {
            if (s->to_sea && !has_abil(V->proto, OV_ABL_AMPHIBIOUS)) {
                if (told(s)) notice(0x68ffc4);
                return GO_END;
            }
        }
        if (!veh_offense(veh)) {
            if (told(s)) notice(0x68ffd0);
            return GO_END;
        }
        if (treaty_of(s->faction, s->tfac) & OV_TREATY_ANY) {
            if (!(*game_human_factions & s->fbit)) {
                V->order = ORDER_NONE;
                return GO_END;
            }
            if (s->faction != *game_current_faction) return GO_ON;
            if ((attack_flags() & 1) && break_treaty(s->faction, s->tfac, OV_TREATY_ANY)) return GO_END;
        }
    }
    if (s->faction == *game_current_faction && (*game_human_factions & s->fbit) && (attack_flags() & 1) &&
        V->order == ORDER_MOVE_TO && (s->tx != V->waypoint_x[0] || s->ty != V->waypoint_y[0]) &&
        chassis_of(V)->triad != TRIAD_AIR && !(V->state & OV_STATE_AUTO) && goody_at(s->tx, s->ty) &&
        ask(0x68fff0, 0x68ffe0)) {
        V->order = ORDER_NONE;
        return GO_END;
    }
    return GO_ON;
}

// The square's cost. A land unit that hasn't got it all (short of a road
// square's worth, or going into fungus as a non-native, not a former or
// artifact, onto an empty square) makes it by chance, its moves (with a
// road square's worth more if it was slowed before) against a roll of
// the cost; failing, it waits for its next turn (1), slowed, the player
// shown it trying (and the tutorial's notes on fungus and fractional
// moves, which stop it). The player's units roll only for moves the
// player sees.
static enum next step_cost(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (!moves_left(veh)) return GO_END;
    s->cost = hex_cost(V->proto, s->faction, s->vx, s->vy, s->tx, s->ty, 0);
    if (s->cost <= moves_left(veh) || s->triad != TRIAD_LAND) return GO_ON;
    int rate = game_rules_basic->move_rate_roads;
    const struct map_tile *t = tile(s->tx, s->ty);
    if (moves_left(veh) >= rate) {
        if (!fungus_on(t)) return GO_ON;
        if (V->proto < OV_BASIC_PROTOS && veh_offense(veh) < 0) return GO_ON;
        if (veh_plan(veh) == OV_PLAN_TERRAFORM || veh_plan(veh) == OV_PLAN_ARTIFACT || s->stack >= 0) return GO_ON;
    }
    if (!(attack_flags() & 1) && (*game_human_factions & s->fbit)) return GO_ON;
    int roll = *game_control_turn ? game_rnd(s->cost) : game_random(0, s->cost);
    if (roll < moves_left(veh) + (V->state & OV_STATE_SLOWED ? game_rules_basic->move_rate_roads : 0)) return GO_ON;
    if (s->faction == *game_current_faction) {
        if (V->order == ORDER_NONE && !key_down(OV_VK_SHIFT) && (attack_flags() & 1) && !(V->state & OV_STATE_ALERT) &&
            !(*game_more_preferences & OV_MPREF_QUICK_ALL)) {
            game_console_focus(game_console, s->vx, s->vy, *game_current_faction);
            stack_veh(veh, 1);
            game_veh_scoot(veh, s->vx, s->vy, s->dir + 8, 0);
            stack_put(veh, s->vx, s->vy);
            for (int i = 0; i < 8; i++) {
                int x = xrange(s->tx + game_radius_near_x[i]), y = s->ty + game_radius_near_y[i];
                if (on_map(x, y)) spot_loc(x, y, s->faction);
            }
            game_draw_tiles(s->tx, s->ty, 2);
        }
        if (fungus_on(t) && V->order == ORDER_NONE && veh_plan(veh) != OV_PLAN_TERRAFORM &&
            veh_plan(veh) != OV_PLAN_ARTIFACT && (attack_flags() & 1)) {
            if (game_tut_check(0x1000)) {
                ++*game_fungus_tutorial_count;
                game_fx_play(game_fx, OV_FX_FUNGUS);
                game_tut_map(game_tut_win, text(0x68fffc), s->tx, s->ty, -1, NULL, 0, -1, -1);
                V->order = ORDER_NONE;
                return GO_END;
            }
            if (!s->slow) ++*game_fungus_tutorial_count;
        }
        if (V->order == ORDER_NONE && !*game_tutorial_moves_seen && (attack_flags() & 1) && !fungus_on(t) &&
            game_tut_check(0x4000)) {
            game_tut_map(game_tut_win, text(0x690008), s->tx, s->ty, veh, NULL, 0, s->vx, s->vy);
            V->order = ORDER_NONE;
            return GO_END;
        }
    }
    veh_skip(veh);
    V->state |= OV_STATE_SLOWED;
    if ((attack_flags() & 1) && net_turn()) step_synch(veh);
    s->result = 1;
    return GO_END;
}

// Whether the player sees the move, and how: a slow move (the player's own,
// or one the preferences want shown, or into a foreign base) or a quick
// one, or none at all off the player's map. The map follows the unit.
static void step_show(struct step *s) {
    int veh = s->veh, cur = *game_current_faction;
    struct veh *V = &game_vehs[veh];
    if (s->faction == cur) s->seen = 1;
    else if (V->faction == cur && (V->flags & OV_FLAG_LURKER) != OV_FLAG_LURKER) s->seen = 1;
    else if (V->visibility & (1 << cur)) s->seen = 1;
    else s->seen = (*game_state & GAME_STATE_OMNISCIENT) ? 1 : 0;
    if (!(*game_state & GAME_STATE_OMNISCIENT) && !(game_factions[cur].flags & FACTION_FLAG_MAP_REVEALED) &&
        !(tile(s->tx, s->ty)->visibility & (1 << cur))) {
        if (!s->seen) {
            s->slow = 0;
            return;
        }
    } else if (!s->seen) {
        // Seen by the player's sensors in its territory, units next to it
        // or bases within two, on the same land or sea (or across a shore).
        const struct map_tile *T = tile(s->tx, s->ty);
        for (int i = 0; i < 21 && !s->seen; i++) {
            int x = xrange(s->tx + game_radius_x[i]), y = s->ty + game_radius_y[i];
            if (!on_map(x, y)) continue;
            const struct map_tile *P = tile(x, y);
            if ((alt_of(P) < ALT_SHORE) == (alt_of(T) < ALT_SHORE) && P->region != T->region) continue;
            if (is_sensor(x, y) && whose_territory(cur, x, y, NULL, 0) == cur) s->seen = 1;
            int who = (i < 9 ? (P->bits & (MAP_BASE | MAP_UNIT)) : (P->bits & MAP_BASE)) && owner_of(P) < 8 ? owner_of(P) : -1;
            if (who == cur) s->seen = 1;
        }
        if (!s->seen) {
            s->slow = 0;
            return;
        }
    }
    int slow = 1;
    if (s->faction != cur) {
        uint32_t ours = treaty_of(cur, s->faction) & TREATY_PACT;
        if ((*game_preferences & OV_PREF_QUICK_ENEMY) && !ours) slow = 1;
        else if ((*game_preferences & OV_PREF_QUICK_ALLY) && ours) slow = 1;
        else slow = s->base >= 0 && game_bases[s->base].faction != s->faction &&
                    !(treaty_of(s->faction, game_bases[s->base].faction) & TREATY_PACT);
    }
    s->slow = slow;
    int shift = key_down(OV_VK_SHIFT);
    if (shift) s->slow = 0;
    int quick;
    if (!s->cost && (s->faction != cur || (V->state & OV_STATE_AUTO) || V->order == ORDER_MOVE_TO)) quick = 1;
    else if (shift || (*game_more_preferences & OV_MPREF_QUICK_ALL)) quick = 1;
    else if (!(*game_more_preferences & OV_MPREF_QUICK_ORDERS)) quick = 0;
    else if (s->faction == cur) quick = (V->state & OV_STATE_AUTO) || V->order == ORDER_MOVE_TO;
    else quick = s->triad == TRIAD_AIR;
    if (quick) {
        s->slow = 0;
        *game_moved_quickly = 1;
        if (s->faction == cur) {
            *game_moved_quickly = 2;
            game_console_cursor_next(game_console, s->tx, s->ty);
        }
    }
    if (s->seen && !key_down(OV_VK_CONTROL) && (s->slow || !(*game_preferences & OV_PREF_NO_CENTER)))
        game_console_focus(game_console, s->vx, s->vy, s->faction);
}

// A worm attack on a base: a facility destroyed (one of 101 tries at a
// random one that is built, not free to faction 0 and not a pressure dome
// on land), the worms gone and the native units around beyond the
// faction's clean-mineral count with them, and a citizen killed from turn
// 10 (an objective keeps one), the base lost with its last; without a
// facility found, its minerals.
static enum next step_worms(struct step *s) {
    int base = s->base, tries, fac = 0, built = 0;
    struct base *B = &game_bases[base];
    const struct faction_def *D = &game_faction_defs[0];
    for (tries = 0; tries <= 100; tries++) {
        fac = game_rand() % 70;
        int ok = 1;
        for (int j = 0; j < D->bonus_count; j++) {
            int id = D->bonus_id[j];
            if ((id == OV_BONUS_FACILITY ||
                 (id == OV_BONUS_FACILITY_TECH && has_tech(game_facilities[D->bonus_val1[j]].preq_tech, 0))) &&
                fac == D->bonus_val1[j]) {
                ok = 0;
                break;
            }
        }
        if (fac == OV_FAC_PRESSURE_DOME) {
            if (sea_at(s->tx, s->ty)) ok = 0;
        } else if (fac <= 1 || fac >= 65) {
            continue;
        }
        int offset;
        uint32_t mask;
        bitmask(fac, &offset, &mask);
        built = (B->facilities_built[offset] & mask) != 0;
        s->boarded = built;
        if (built && ok) break;
    }
    *game_battle_text = 0;
    strcat(game_battle_text, text(0x69003c));
    say_name(2, s->veh);
    if (tries <= 100) {
        strcat(game_battle_text, text(0x690048));
        set_fac(fac, base, 0);
        game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
    }
    if (s->seen && s->tfac == *game_current_faction) {
        game_parse_says(0, B->name, -1, -1);
        game_netmsg_pop(game_netmsg, game_battle_text, 5000, 0, NULL);
    }
    kill(s->veh);
    int spared = 0;
    for (int i = 0; i < 25; i++) {
        int x = xrange(s->tx + game_radius_x[i]), y = s->ty + game_radius_y[i];
        if (!on_map(x, y)) continue;
        int u = s->veh = stack_fix(veh_at(x, y));
        if (u < 0 || game_vehs[u].faction) continue;
        // The next unit's number is taken before a kill renumbers them.
        for (u = s->veh = top_of(u); u >= 0; u = s->veh) {
            int next = game_vehs[u].next_in_stack;
            if (spared < game_factions[s->tfac].clean_minerals) spared++;
            else veh_kill(u);
            s->veh = next;
        }
    }
    if (*game_turn >= 10) {
        B->pop_size--;
        if (is_objective(base) && B->pop_size < 1) B->pop_size = 1;
        if (!B->pop_size) {
            base_kill(base);
            game_draw_map(1);
            game_eliminate_player(s->tfac, 0);
            game_draw_map(1);
            return GO_END;
        }
    }
    if (tries > 100) B->minerals = 0;
    game_draw_tile(B->x, B->y, 2);
    game_base_win_check_base(game_base_win, base);
    return GO_END;
}

// The move itself, with what it sets off: a surprise attack told to the
// player, the slide shown, the unit's spotting begun again (native life
// stays seen only by those that saw the square, near their bases or in
// their territory), a bunker taken from an enemy, the worms' attack on a
// base, and then the unit on its square: a base captured (an Alien
// Crossfire unit other than a battle ogre fully repaired first), or at
// sea the guns of enemies' artillery on land within two squares, each
// costing a road square's worth of moves; passengers woken in a base, or
// for the player on reaching a shore at the end of a transport's moves;
// and a land unit from the sea counted on its new continent.
static enum next step_enter(struct step *s) {
    s->result = 1;
    if (!(attack_flags() & 2)) return GO_END;
    int veh = s->veh, cur = *game_current_faction;
    struct veh *V = &game_vehs[veh];
    V->moves_spent = (uint8_t)(V->moves_spent + s->cost);
    V->state &= ~(uint32_t)(OV_STATE_SLOWED | OV_STATE_UNK_40);
    if (s->base >= 0 && s->tfac != s->faction && !(treaty_of(s->faction, s->tfac) & TREATY_PACT)) {
        if ((treaty_of(s->tfac, s->faction) & OV_TREATY_ANY) && s->tfac == cur) {
            say_title(0, s->faction);
            say_leader(1, s->faction);
            say_noun(2, s->faction);
            notice(0x690018);
        }
        cur = *game_current_faction;
        if (s->faction != cur && s->tfac != cur) {
            const struct base *B = &game_bases[s->base];
            if ((B->faction == cur || (B->visibility & (1 << cur))) && s->faction) spot_stack(veh, cur);
        }
    }
    step_show(s);
    stack_veh(veh, 1);
    if (s->slow) {
        if (s->faction == *game_current_faction)
            game_fx_engine_pitch(game_fx, alt_of(tile(s->tx, s->ty)) <= alt_of(tile(s->vx, s->vy)) ? 200 : 300);
        game_veh_scoot(veh, s->vx, s->vy, s->dir, 0);
        if (s->faction == *game_current_faction) game_fx_engine_pitch(game_fx, 0);
    } else if (s->seen) {
        game_veh_scoot(veh, s->vx, s->vy, s->dir, 0x4000);
    } else {
        V->unk_29 = (uint8_t)s->dir;
    }
    if (s->faction) {
        unspot_stack(veh);
    } else {
        V->visibility &= tile(s->tx, s->ty)->visibility;
        for (int f = 1; f < FACTION_COUNT; f++) {
            if (!(V->visibility & (1 << f))) continue;
            base_find_2(s->tx, s->ty, f);
            if (*game_base_find_dist >= 8 && whose_territory(f, s->tx, s->ty, NULL, 0) != f)
                V->visibility &= (uint8_t)~(1 << f);
        }
        s->owner = FACTION_COUNT;
    }
    if ((tile(s->tx, s->ty)->bits & OV_MAP_BUNKER) && s->stack < 0 && game_veh_triad(veh) == TRIAD_LAND &&
        veh_plan(veh) != OV_PLAN_PROBE) {
        s->owner = whose_territory(s->faction, s->tx, s->ty, NULL, 0);
        if (s->owner >= 0 && s->owner != s->faction && !(treaty_of(s->faction, s->owner) & TREATY_PACT)) {
            veh_skip(veh);
            if (s->faction == *game_current_faction) notice(0x690024);
        }
    }
    if (s->base >= 0 && s->faction != s->tfac && !s->faction) return step_worms(s);
    stack_put(veh, s->tx, s->ty);
    if (s->base >= 0) {
        if (s->faction != s->tfac && !(treaty_of(s->faction, s->tfac) & TREATY_PACT)) {
            if (*game_expansion && (V->proto < OV_BSC_BATTLE_OGRE_1 || V->proto > OV_BSC_BATTLE_OGRE_3)) V->damage = 0;
            *game_current_veh = veh;
            act_of_aggression(s->faction, s->tfac);
            game_capture_base(s->base, s->faction, 0);
            s->veh = veh = *game_current_veh;
        }
    } else if (s->to_sea && game_veh_triad(veh) == TRIAD_SEA) {
        int guns = 0;
        for (int i = 0; i < 25; i++) {
            int x = xrange(s->tx + game_radius_x[i]), y = s->ty + game_radius_y[i];
            if (!on_map(x, y)) continue;
            for (int u = stack_fix(veh_at(x, y)); u >= 0; u = game_vehs[u].next_in_stack) {
                const struct veh *U = &game_vehs[u];
                if (!can_arty(U->proto, 1) || game_veh_triad(u) != TRIAD_LAND || U->faction == s->faction) continue;
                if (!(treaty_of(s->faction, U->faction) & TREATY_VENDETTA)) continue;
                if ((V->faction == U->faction && (V->flags & OV_FLAG_LURKER) != OV_FLAG_LURKER) ||
                    (V->visibility & (1 << U->faction))) {
                    guns++;
                    s->owner = U->faction;
                    spot_stack(u, s->faction);
                }
            }
        }
        if (guns) {
            V->moves_spent = (uint8_t)(V->moves_spent + game_rules_basic->move_rate_roads * guns);
            if (s->faction == *game_current_faction) {
                say_adjective(0, s->owner);
                notice(0x69004c);
            }
        }
    }
    V = &game_vehs[veh];
    cur = *game_current_faction;
    if (veh_cargo(veh) && s->base >= 0 &&
        (s->faction == cur || (*game_net_multiplayer && (*game_human_factions & s->fbit))) &&
        (V->order != ORDER_MOVE_TO || (V->waypoint_x[0] == s->tx && V->waypoint_y[0] == s->ty))) {
        for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack) {
            struct veh *U = &game_vehs[u];
            if (U->faction == s->faction && U->order == ORDER_SENTRY_BOARD &&
                (U->waypoint_x[0] < 0 || U->waypoint_x[0] == veh))
                U->order = ORDER_NONE;
        }
    }
    if (s->faction == cur && (*game_preferences & OV_PREF_WAKE_ON_LAND) && game_veh_triad(veh) == TRIAD_SEA &&
        veh_cargo(veh) && s->to_sea && !moves_left(veh) &&
        (V->order != ORDER_MOVE_TO || (V->waypoint_x[0] == s->tx && V->waypoint_y[0] == s->ty))) {
        int shore = 0;
        for (int i = 0; i < 8 && !shore; i++) {
            int x = xrange(s->tx + game_radius_near_x[i]), y = s->ty + game_radius_near_y[i];
            if (on_map(x, y) && (alt_of(tile(x, y)) >= ALT_SHORE || base_on(tile(x, y)))) shore = 1;
        }
        if (shore)
            for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack) {
                struct veh *U = &game_vehs[u];
                if (game_veh_triad(u) == TRIAD_LAND && U->order == ORDER_SENTRY_BOARD && moves_left(u)) {
                    U->order = ORDER_NONE;
                    *game_veh_ref_map_1 = u;
                }
            }
    }
    if (s->triad == TRIAD_LAND && s->from_sea && !s->to_sea)
        game_factions[s->faction].region_total_combat_units[s->region]++;
    return GO_UPKEEP;
}

// Whether the player's aircraft, out of moves, heads home: by preference
// or on its own orders, but not on alert at a sentry post or patrolling
// with legs to go.
static int aloft_home(const struct veh *V) {
    if (!(*game_preferences & OV_PREF_AIR_HOME) && !(V->state & OV_STATE_AUTO)) return 0;
    if ((V->state & OV_STATE_SENTRY_ALERT) == OV_STATE_SENTRY_ALERT) return 0;
    return (V->state & OV_STATE_PATROL) != OV_STATE_PATROL || V->patrol_point >= V->waypoint_count;
}

// Aircraft: refuelled at a base, an airbase or a carrier (the AI anywhere
// it can land; the player also on the square of a won fight with a range
// of 1 and moves left, and anywhere with moves left unless enemies are
// next to it or it can't go more than a road square or has used its last
// turn but one). Otherwise the AI and (by preference) the player head
// home; out of moves, a unit at its range's end is lost (a copter is
// damaged), elsewhere it counts a turn aloft, turning for home at half.
static enum next step_fuel(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    int human = (*game_human_factions & s->fbit) != 0;
    const struct map_tile *t = tile(s->tx, s->ty);
    int open = !base_on(t) && !(t->bits & MAP_AIRBASE);
    for (int u = veh < 0 ? -1 : top_of(veh); open && u >= 0; u = game_vehs[u].next_in_stack)
        if (has_abil(game_vehs[u].proto, OV_ABL_CARRIER)) open = 0;
    int aloft;
    if (!human || open) aloft = open;
    else if (veh_range(veh) == 1 && moves_left(veh) && s->refuel_check) aloft = 1;
    else if (!moves_left(veh)) aloft = 0;
    else if (moves_left(veh) > game_rules_basic->move_rate_roads && V->movement_turns + 1 < veh_range(veh)) aloft = 1;
    else aloft = zoc_veh(s->tx, s->ty, s->faction) != 0;
    if (!aloft) {
        V->movement_turns = 0;
        veh_skip(veh);
        veh_demote(veh);
        game_draw_tile(V->x, V->y, 2);
        return GO_ON;
    }
    int range = veh_range(veh);
    if (!range) return GO_ON;
    if (!human || (s->faction == *game_current_faction && aloft_home(V))) action_home(veh, 1);
    if (moves_left(veh)) return GO_ON;
    int turns = V->movement_turns + 1;
    if (turns < range) {
        if (turns >= range >> 1 &&
            (!human || (s->faction == *game_current_faction && aloft_home(V) && V->order != ORDER_MOVE_TO)))
            action_home(veh, 0);
        V->movement_turns++;
        return GO_ON;
    }
    V->state |= OV_STATE_MOVED;
    if (s->seen) game_boom(V->x, V->y, 0x80);
    const struct unit_proto *P = veh_design(veh);
    if (range == 1 && P->chassis == OV_CHASSIS_COPTER) {
        V->damage = (uint8_t)(V->damage + 3 * P->reactor);
    } else {
        int hp = P->plan == OV_PLAN_ARTIFACT ? 1 : 10 * (P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor);
        int d = hp - V->damage;
        V->damage = (uint8_t)(V->damage + (d < 0 ? 0 : d > 9999 ? 9999 : d));
    }
    P = veh_design(veh);
    int hp = P->plan == OV_PLAN_ARTIFACT ? 1 : 10 * (P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor);
    if (hp - V->damage <= 0) {
        kill(veh);
        if (s->seen && s->faction == *game_current_faction) notice(0x690058);
        return GO_END;
    }
    V->movement_turns = 0;
    game_draw_tile(V->x, V->y, 2);
    if (s->seen && s->faction == *game_current_faction) notice(0x690064);
    return GO_ON;
}

// After a move, a fight won or a stay: the unarmed lose morale, native
// life heals in fungus, the unit looks around, the AI's moves the player
// sees are paced, aircraft are fuelled; then monoliths (not after a
// fight), supply pods, an artifact alone in the open taken by a stronger
// neighbour, an artifact's choice at a base carried out (studied, or
// half its cost given to production), a colony pod's landing noted, a
// unit that boarded at sea put to sleep with the first unit of its stack
// able to move (its transport's waypoint) picked, the AI's transports out
// of moves at sea calling for passengers' pickup or stopping for
// threats, the player's long-moving units asked whether to go on, and
// the unit's facing.
static enum next step_upkeep(struct step *s) {
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (!veh_offense(veh)) veh_demote(veh);
    if (!s->faction && fungus_on(tile(s->tx, s->ty)) && V->damage) V->damage--;
    spot_all(veh, 1);
    if (s->faction != *game_current_faction && !(*game_human_factions & s->fbit) &&
        !(*game_state & GAME_STATE_OMNISCIENT) && s->seen && !*game_moved_quickly && !key_down(OV_VK_SHIFT) &&
        !*game_net_multiplayer)
        game_clock_wait(s->slow && !*game_moved_quickly ? 100 : 5);
    if (game_veh_triad(veh) == TRIAD_AIR && step_fuel(s) == GO_END) return GO_END;
    const struct map_tile *t = tile(s->tx, s->ty);
    if ((t->bits & MAP_MONOLITH) && s->triad != TRIAD_AIR &&
        (s->dir >= 0 || (veh_offense(veh) && (V->damage || !(V->state & OV_STATE_MONOLITH)))) && !s->fought)
        game_monolith(veh);
    if (s->goody && s->triad != TRIAD_AIR && s->faction && game_goody_box(veh, s->goody)) return GO_END;
    if (veh_plan(veh) == OV_PLAN_ARTIFACT && s->base < 0 && stack_check(veh, 1, -1, -1, -1) == 1 && s->dir < 8) {
        for (int i = 0; i < 8; i++) {
            int x = xrange(s->tx + game_radius_near_x[i]), y = s->ty + game_radius_near_y[i];
            if (!on_map(x, y) || alt_of(tile(x, y)) < ALT_SHORE) continue;
            int u = stack_fix(veh_at(x, y));
            if (u < 0) continue;
            int uf = game_vehs[u].faction;
            if (uf == s->faction || !uf || (treaty_of(s->faction, uf) & TREATY_PACT)) continue;
            int artifacts = stack_check(u, 2, OV_PLAN_ARTIFACT, -1, -1);
            if (stack_check(u, 1, -1, -1, -1) <= artifacts) continue;
            V->faction = game_vehs[u].faction;
            V->order = ORDER_NONE;
            game_draw_tile(s->tx, s->ty, 2);
            owner_set(s->tx, s->ty, V->faction);
            if (s->faction == *game_current_faction) {
                *game_veh_ref_map_1 = -1;
                say_noun(0, game_vehs[u].faction);
                ask(0x690084, 0x690070);
            } else if (game_vehs[u].faction == *game_current_faction) {
                say_name(0, u);
                say_noun(1, s->faction);
                ask(0x6900a8, 0x690094);
            }
            s->faction = V->faction;
            break;
        }
    }
    if (veh_plan(veh) == OV_PLAN_ARTIFACT && s->base >= 0 && V->movement_turns) {
        if (V->movement_turns == 1) {
            if (game_study_artifact(veh)) return GO_END;
        } else if (V->movement_turns == 2) {
            struct base *B = &game_bases[s->base];
            B->minerals += 10 * veh_design(veh)->cost / 2;
            B->minerals_2 = B->minerals;
            B->production_last = B->queue[0];
            kill(veh);
            game_base_win_check_base(game_base_win, s->base);
        }
    }
    if (s->from_sea && !s->to_sea && veh_plan(veh) == OV_PLAN_COLONY)
        game_factions[s->faction].region_flags[s->region] |= 2;
    if (s->boarded) {
        veh_sleep(veh);
        veh_skip(veh);
        for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack) {
            if (veh_cargo(u)) continue;
            struct veh *U = &game_vehs[u];
            if (U->moves_spent > veh_speed(u, 0)) {
                U->order = ORDER_NONE;
                continue;
            }
            stack_veh(u, 0);
            if (V->waypoint_x[0] == u) {
                *game_veh_ref_map_1 = u;
                U->order = ORDER_NONE;
                break;
            }
        }
        game_draw_tile(s->tx, s->ty, 2);
    }
    if (!is_human_(s->faction) && veh_plan(veh) == OV_PLAN_NAVAL_TRANSPORT && sea_at(s->tx, s->ty) && s->base < 0 &&
        !moves_left(veh)) {
        if (zoc_any(V->x, V->y, s->faction) || (*game_human_factions & V->visibility) || V->status_icon == 'u')
            for (int u = top_of(veh); u >= 0; u = game_vehs[u].next_in_stack)
                if (game_vehs[u].order == ORDER_SENTRY_BOARD && game_vehs[u].waypoint_x[0] == veh) V->order = ORDER_NONE;
        int x = s->tx, y = s->ty;
        if (V->order == ORDER_MOVE_TO) {
            int hx = (V->waypoint_x[0] + s->tx) / 2, hy = (V->waypoint_y[0] + s->ty) / 2;
            if (hx & 1) hx--;
            if (hy & 1) hx++;
            if (sea_at(hx, hy)) x = hx, y = hy;
        }
        if (sea_at(x, y)) add_goal(s->faction, OV_GOAL_TRANSPORT, 2, x, y, -1);
    }
    if (s->faction == *game_current_faction && (V->order == ORDER_MOVE_TO || (V->state & OV_STATE_AUTO))) {
        V->iter_count++;
        if ((V->state & OV_STATE_FACING) && s->dir == ((int8_t)V->unk_29 ^ 4)) V->iter_count += 8;
        if (V->iter_count >= 32) {
            say_name(0, veh);
            if (*game_net_multiplayer || ask_x(0x6900b8)) {
                V->order = ORDER_NONE;
                V->state &= ~0x3004200u;
            }
            V->iter_count = 0;
        }
    }
    if (s->dir >= 0 && s->dir < 8) {
        V->unk_29 = (uint8_t)s->dir;
        V->state |= OV_STATE_FACING;
    } else {
        V->state &= ~(uint32_t)OV_STATE_FACING;
    }
    return GO_END;
}

// The end: with as many units as before, a failed move ends the unit's
// orders (the AI's units and the player's on their own orders skip their
// turn, the AI's after 24 failures); a move that happened leaves a zone
// of control behind, and in a network game a unit arrived ends its order
// or takes its patrol's next leg.
static int step_end(struct step *s) {
    if (*game_veh_count != s->count) return 0;
    int veh = s->veh;
    struct veh *V = &game_vehs[veh];
    if (!s->result) {
        int human = is_human_(s->faction);
        V->order = ORDER_NONE;
        if (!human || (V->state & OV_STATE_AUTO)) {
            V->iter_count++;
            if (V->iter_count >= 24 || human) {
                V->iter_count = 0;
                veh_skip(veh);
            }
        }
        if (!(attack_flags() & 2) && net_turn()) step_synch(veh);
        return 0;
    }
    if (!(attack_flags() & 2)) return 1;
    if (s->dir >= 0 && s->dir < 8 && (V->state & OV_STATE_ZOC) && V->x == s->tx && V->y == s->ty &&
        !zoc_veh(s->tx, s->ty, s->faction))
        V->state &= ~(uint32_t)OV_STATE_ZOC;
    if (net_turn() && V->order == ORDER_MOVE_TO && V->x == V->waypoint_x[0] && V->y == V->waypoint_y[0]) {
        V->order = ORDER_NONE;
        if ((V->state & OV_STATE_PATROL) == OV_STATE_PATROL && V->patrol_point < V->waypoint_count) {
            V->patrol_point++;
            V->order = ORDER_MOVE_TO;
            int16_t *w = waypoint_x(V);  // the waypoints' x then y, read on past the end as the original does
            w[0] = w[V->patrol_point];
            w[4] = w[V->patrol_point + 4];
            V->state &= ~0x2010200u;
        }
    }
    return 1;
}

// One pass of the step, up to the move: run again with another unit after
// the disembark menu.
static enum next step_approach(struct step *s) {
    struct veh *V = &game_vehs[s->veh];
    s->slow = (V->state & OV_STATE_AUTO) != 0;
    s->triad = chassis_of(V)->triad;
    enum next n = step_territory(s);
    if (n != GO_ON) return n;
    step_artifact_menu(s);
    if (s->dir >= 8) return step_stay(s);
    game_vehs[s->veh].state |= OV_STATE_MOVED;
    n = step_probe(s);
    if (n == GO_ON) n = step_to_sea(s);
    if (n == GO_ON) n = step_zoc(s);
    if (n == GO_ON) n = step_supply(s);
    if (n == GO_ON) n = step_native(s);
    if (n == GO_SPOT) n = step_occupied(s);
    if (n == GO_NAVAL) n = step_naval(s);
    return n;
}

// The original keeps its popup at [ebp-0x5410], where the game records
// its address, and on some paths (a stay) reads [ebp-0x24], for whether
// the player sees the unit, without ever setting it. order_veh() below
// gives ours the same frame: `pop` is that popup's place, and the word
// 0x5410 - 0x24 above it is the one the original would read.
int order_veh_in(uint8_t *pop, int veh, int dir, int type);
int order_veh_in(uint8_t *pop, int veh, int dir, int type) {
    struct step st = {0}, *s = &st;
    s->pop = pop;
    s->seen = *(int32_t *)(pop + 0x5410 - 0x24);
    game_popup_ctor(pop);
    *game_veh_attack_flags = type;
    if (type & 1) *game_console_23be4 = 0;
    else *game_moved_quickly = 0;
    s->count = *game_veh_count;
    struct veh *V = &game_vehs[veh];
    s->veh = veh;
    s->vx = V->x, s->vy = V->y;
    s->faction = V->faction;
    if (dir < 0) dir = 8;
    s->dir = dir;
    s->tx = xrange(s->vx + game_radius_near_x[dir]);
    s->ty = s->vy + game_radius_near_y[dir];
    enum next n = GO_END;
    if (on_map(s->tx, s->ty)) {
        s->region = tile(s->tx, s->ty)->region;
        s->stack = stack_fix(veh_at(s->tx, s->ty));
        s->base = base_at(s->tx, s->ty);
        s->tfac = s->base >= 0 ? game_bases[s->base].faction : s->stack >= 0 ? game_vehs[s->stack].faction : -1;
        s->from_sea = sea_at(s->vx, s->vy);
        s->to_sea = sea_at(s->tx, s->ty);
        s->goody = goody_at(s->tx, s->ty);
        s->fbit = 1 << s->faction;
        do n = step_approach(s);
        while (n == GO_LOOP);
        if (n == GO_ARRIVE) {
            n = step_conquer(s);
            if (n == GO_ON) n = step_cost(s);
            if (n == GO_ON) n = step_enter(s);
        }
        if (n == GO_UPKEEP) n = step_upkeep(s);
    }
    int r = n == GO_RETURN_0 ? 0 : n == GO_RETURN_1 ? 1 : step_end(s);
    game_popup_dtor(pop);
    return r;
}
__asm__(".text\n"
        ".globl order_veh\n"
        "order_veh:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0xb000, %esp\n"  // room for the popups, here and in supply_menu()
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5410(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call order_veh_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module move_module;
#define MOVE (&move_module)

static void writes_veh(struct hook_writes *w, int veh) { hooks_writes_add(w, &game_vehs[veh], sizeof(struct veh)); }
#define WRITES_VEH(v) writes_veh(&w_, v)

SHADOW_WRITER(MOVE, void, set_course, (int a, int b, int c, int d), (a, b, c, d), WRITES_VEH(a), "%d, %d, %d, %d", a, b,
              c, d)
// add_goal writes the faction's goals; contiguous() fills the map's marks
// and runs the path finder.
SHADOW_WRITER(MOVE, void, assemble_passengers, (int a, int b, int c), (a, b, c), writes_paths(&w_, game_path),
              "%d, %d, %d", a, b, c)
SHADOW_WRITER(MOVE, int, action_home, (int a, int b), (a, b), WRITES_VEH(a), "%d, %d", a, b)
SHADOW3(MOVE, int, valid_patrol, int, int, int)
SHADOW_WRITER(MOVE, void, action_patrol, (int a, int b, int c), (a, b, c), WRITES_VEH(a), "%d, %d, %d", a, b, c)

// action_go_to and action_road_to end in the game's order_veh() (the step
// with all it sets off), and action_road_to may start a former's work: a
// check would make the move twice. The game's calls run the original; the
// sweep checks them with those calls held (below).
// Path_move's Path_find gets what the original's found below its stack, as
// in path_move's own check.
SHADOW_DECLARE(void, action_go_to, (int a))
static void check_action_go_to(int veh) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, game_path);
    hooks_writes_begin(&w_);
    hooks_nested++;
    path_stale_reset();
    orig_action_go_to(veh);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int32_t saved = path_stale_take();
    action_go_to(veh);
    path_stale_put(saved);
    const char *diff_ = hooks_writes_end(&w_, 0, 0);
    if (diff_) hooks_mismatch(&stats_action_go_to, "(%d) %s", veh, diff_);
    hooks_ui_release();
}
SHADOW_WRITER_CHECK(void, action_road_to, (int a), (a), orig_action_road_to(a), (writes_paths(&w_, game_path)), "%d",
                    a)
#define SHADOW_UNCHECKED(name)                     \
    static void shadow_##name(int a) {             \
        if (hooks_nested) {                        \
            orig_##name(a);                        \
            return;                                \
        }                                          \
        if (shadow_bypass()) {                     \
            name(a);                               \
            return;                                \
        }                                          \
        stats_##name.calls++;                      \
        shadow_tick(MOVE);                         \
        orig_##name(a);                            \
    }
SHADOW_UNCHECKED(action_go_to)
SHADOW_UNCHECKED(action_road_to)

// Held in the sweep's checks of the orders above: the step and the start
// of a former's work. A held step returns `hold_result`; with `hold_moves`
// it also puts the unit on the square it stepped to (its coordinates
// only), as the move would, so that both runs see an arrival. The step's
// own direction is only known once those orders have found it, too late
// to tell whether the step could be checked (see order_checkable), so the
// holds stay now that the step is checked on its own, and the game's
// calls of action_go_to and action_road_to still run the original (the
// step within them checked).
static int holding, hold_result, hold_moves;
static void replay_none(const struct hook_ui_call *c) { (void)c; }
static int hold_order_veh(int veh, int dir, int type) {
    struct hook_ui_call c = {.name = "order_veh", .replay = replay_none, .arg = {veh, dir, type}};
    hooks_ui_hold(&c);
    if (hold_moves && hold_result && dir >= 0 && dir < 8) {
        struct veh *V = &game_vehs[veh];
        int x = xrange(V->x + game_radius_near_x[dir]), y = V->y + game_radius_near_y[dir];
        if (on_map(x, y)) V->x = (int16_t)x, V->y = (int16_t)y;
    }
    return hold_result;
}

// ---- The step: shadow checks --------------------------------------------------------
//
// order_veh is checked on all of the game and the path finder's tables
// (action_home searches), the slides not shown (quiet.c's veh_scoot hook
// drops them, as in battles: shown afterwards they would find the unit
// back in its stack). Its two runs get the same frame from stale_call3(),
// so that their popups lie at the same address and the word the original
// reads unset (see order_veh_in) is the same. A base that fell in a check
// is told to the windows afterwards, as battle_fight's check does.

// fn(a, b, c) called with *s below its entry stack pointer (as
// hooks_stale_call() does for functions without arguments).
int stale_call3(int (*fn)(int, int, int), const struct hook_stale *s, int a, int b, int c);
__asm__(".text\n"
        ".globl stale_call3\n"
        "stale_call3:\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    movl 12(%esp), %eax\n"      // fn
        "    movl 16(%esp), %esi\n"      // s
        "    leal -0x110(%esp), %edi\n"  // fn's entry stack pointer will be 16 below this one
        "    movl $0x40, %ecx\n"
        "    cld\n"
        "    rep movsl\n"
        "    pushl 28(%esp)\n"  // c
        "    pushl 28(%esp)\n"  // b
        "    pushl 28(%esp)\n"  // a
        "    call *%eax\n"
        "    addl $12, %esp\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    ret\n");

SHADOW_DECLARE(int, order_veh, (int veh, int dir, int type))
extern int quiet_drop_scoot;

// The target of a step, as order_veh finds it.
static int step_target(int veh, int dir, int *x, int *y) {
    const struct veh *V = &game_vehs[veh];
    if (dir < 0) dir = 8;
    *x = xrange(V->x + game_radius_near_x[dir]);
    *y = V->y + game_radius_near_y[dir];
    return on_map(*x, *y);
}

// What a check can't run: network and hotseat games; a human's unit (it
// asks the player, of the attack, treaties, pods, haste, probe teams and
// more) and a step onto human units or bases, or that could take or wipe
// out a base with messages listed (see battle_checkable); a probe team's
// mission (stolen techs are chosen in a window, which allocates from the
// game's heap even for the AI); and the menus of an artifact at its
// faction's base and of a transport's passengers (they open script.txt
// and build their lists on the heap).
static int order_checkable(int veh, int dir, int type) {
    if (*game_net_multiplayer || *game_pbem) return 0;
    const struct veh *V = &game_vehs[veh];
    int f = V->faction;
    if (game_is_human(f)) return 0;
    int x, y;
    if (!step_target(veh, dir, &x, &y)) return 1;
    if (!battle_checkable(veh, x, y)) return 0;
    int b = base_at(x, y), s = stack_fix(veh_at(x, y));
    int tf = b >= 0 ? game_bases[b].faction : s >= 0 ? game_vehs[s].faction : -1;
    if (veh_plan(veh) == OV_PLAN_PROBE && tf >= 0 && tf != f) return 0;
    if (veh_plan(veh) == OV_PLAN_ARTIFACT && !(V->flags & OV_FLAG_OBJECTIVE) && (type & 1) && b >= 0 &&
        game_bases[b].faction == f)
        return 0;
    return !(game_veh_triad(veh) == TRIAD_SEA && !sea_at(x, y) && b < 0 && f == *game_current_faction && (type & 1));
}

static int check_order_veh(const struct hook_stale *st, int veh, int dir, int type) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, game_path);
    hooks_writes_begin(&w_);
    hooks_nested++;
    path_stale_reset();
    int want = stale_call3(orig_order_veh, st, veh, dir, type);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int32_t saved = path_stale_take();
    int got = stale_call3(order_veh, st, veh, dir, type);
    path_stale_put(saved);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_order_veh, "(%d, %d, %d) %s", veh, dir, type, diff_);
    hooks_ui_release();
    return want;
}

// A check, with the slides dropped and a fallen base told to the windows.
static int checked_order_veh(const struct hook_stale *st, int veh, int dir, int type) {
    int x, y, base = step_target(veh, dir, &x, &y) ? base_at(x, y) : -1, bases = *game_base_count;
    quiet_drop_scoot++;
    int r = check_order_veh(st, veh, dir, type);
    quiet_drop_scoot--;
    if (base >= 0 && *game_base_count < bases) {
        game_base_win_base_killed(game_base_win, base);
        game_listbox_update_changes(game_message_box);
    }
    return r;
}

int shadow_order_veh_in(const struct hook_stale *st, int veh, int dir, int type);
int shadow_order_veh_in(const struct hook_stale *st, int veh, int dir, int type) {
    if (holding && hooks_protecting()) return hold_order_veh(veh, dir, type);
    if (hooks_nested) return stale_call3(orig_order_veh, st, veh, dir, type);
    if (shadow_bypass()) return stale_call3(order_veh, st, veh, dir, type);
    stats_order_veh.calls++;
    shadow_tick(MOVE);
    if (!order_checkable(veh, dir, type)) return stale_call3(orig_order_veh, st, veh, dir, type);
    return checked_order_veh(st, veh, dir, type);
}
// The game's call: the bytes below its stack pointer kept for both runs.
int shadow_order_veh(int veh, int dir, int type);
__asm__(".text\n"
        ".globl shadow_order_veh\n"
        "shadow_order_veh:\n"
        "    subl $0x100, %esp\n"  // HOOK_STALE_SIZE: the bytes stay put
        "    movl %esp, %eax\n"
        "    pushl 0x10c(%eax)\n"  // type
        "    pushl 0x108(%eax)\n"  // dir
        "    pushl 0x104(%eax)\n"  // veh
        "    pushl %eax\n"
        "    call shadow_order_veh_in\n"
        "    addl $0x110, %esp\n"
        "    ret\n");

// ---- Actions: shadow checks ---------------------------------------------------------

// The AI's choices of terraforming and their costs only read the game
// (base_find_2() leaves the distance it found).
SHADOW_WRITER(MOVE, int, terraform_cost, (int a, int b, int c), (a, b, c), WRITES(game_base_find_dist, 4), "%d, %d, %d", a,
              b, c)
SHADOW_WRITER(MOVE, int, can_terraform, (int a, int b, int c, int d, int e), (a, b, c, d, e),
              WRITES(game_base_find_dist, 4), "%d, %d, %d, %d, %d", a, b, c, d, e)
// The rest change the game here and there: checked on all of it.
SHADOW_WRITER(MOVE, void, action_staple, (int a), (a), hooks_writes_game(&w_), "%d", a)
SHADOW_WRITER(MOVE, void, action_destruct, (int a), (a), hooks_writes_game(&w_), "%d", a)
SHADOW_WRITER(MOVE, void, action_tectonic, (int a, int b, int c), (a, b, c), hooks_writes_game(&w_), "%d, %d, %d", a, b,
              c)
SHADOW_WRITER(MOVE, void, action_fungal, (int a, int b, int c), (a, b, c), hooks_writes_game(&w_), "%d, %d, %d", a, b, c)
SHADOW_WRITER(MOVE, void, action_gate, (int a, int b), (a, b), hooks_writes_game(&w_), "%d, %d", a, b)

// Founding a base names it from its faction's file, which a check can't
// open: the name is found first and both runs take it (as base_init's own
// check does).
SHADOW_WRITER_CHECK(int, action_build, (int a, char *b), (a, b), orig_action_build(a, b), hooks_writes_game(&w_),
                    "%d, \"%s\"", a, b ? b : "")
static int checked_action_build(int veh, char *name) {
    const struct veh *V = &game_vehs[veh];
    if (*game_base_count < BASE_COUNT) {
        char held[sizeof game_bases[0].name];
        memcpy(held, game_bases[*game_base_count].name, sizeof held);
        held[0] = 0;
        name_base_hold(V->faction, held, alt_of(tile(V->x, V->y)) < ALT_SHORE);
    }
    int r = check_action_build(veh, name);
    name_base_release();
    return r;
}
static int shadow_action_build(int veh, char *name) {
    if (hooks_nested) return orig_action_build(veh, name);
    if (shadow_bypass()) return action_build(veh, name);
    stats_action_build.calls++;
    shadow_tick(MOVE);
    return checked_action_build(veh, name);
}

// Destroying a base renumbers the messages listed about later bases, on the
// heap a check can't put back: with any such message the original runs
// unchecked (as in base_kill's check), and the windows are told after.
SHADOW_WRITER_CHECK(void, action_oblit, (int a, int b), (a, b), orig_action_oblit(a, b), hooks_writes_game(&w_), "%d, %d",
                    a, b)
static void shadow_action_oblit(int veh, int base) {
    if (hooks_nested) {
        orig_action_oblit(veh, base);
        return;
    }
    if (shadow_bypass()) {
        action_oblit(veh, base);
        return;
    }
    stats_action_oblit.calls++;
    shadow_tick(MOVE);
    if (base_listed(base)) {
        orig_action_oblit(veh, base);
        return;
    }
    check_action_oblit(veh, base);
    game_base_win_base_killed(game_base_win, base);
    game_listbox_update_changes(game_message_box);
}

// A former's work: the count of turns left only reads; the work itself is
// checked on all of the game. Held, as before, in the sweep's checks of
// road_to (holding_terraform).
static int holding_terraform;
SHADOW_DECLARE(int, action_terraform, (int veh, int terraform, int toggle))
static int check_action_terraform(int veh, int terraform, int toggle) {
    struct hook_writes w_ = {0};
    if (toggle) hooks_writes_game(&w_);
    hooks_writes_begin(&w_);
    hooks_nested++;
    int want = orig_action_terraform(veh, terraform, toggle);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int got = action_terraform(veh, terraform, toggle);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_action_terraform, "(%d, %d, %d) %s", veh, terraform, toggle, diff_);
    hooks_ui_release();
    return want;
}

// The others end in a fight (battle_fight, with its animation) or the
// unit's step (order_veh): the game's calls run the original, and the
// sweep checks them with the step held, where no fight would start.
SHADOW_WRITER_CHECK(void, action_destroy, (int a, int b, int c, int d), (a, b, c, d), orig_action_destroy(a, b, c, d),
                    hooks_writes_game(&w_), "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER_CHECK(void, action_arty, (int a, int b, int c), (a, b, c), orig_action_arty(a, b, c), hooks_writes_game(&w_),
                    "%d, %d, %d", a, b, c)
// A step's Path_find gets what the original's found below its stack, as in
// action_go_to's check.
SHADOW_DECLARE(int, action, (int a))
static int check_action(int veh) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, game_path);
    hooks_writes_begin(&w_);
    hooks_nested++;
    path_stale_reset();
    int want = orig_action(veh);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int32_t saved = path_stale_take();
    int got = action(veh);
    path_stale_put(saved);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_action, "(%d) %s", veh, diff_);
    hooks_ui_release();
    return want;
}
#define SHADOW_RUN_ORIGINAL(ret, name, params, args) \
    static ret shadow_##name params {                \
        if (hooks_nested) return orig_##name args;   \
        if (shadow_bypass()) return name args;       \
        stats_##name.calls++;                        \
        shadow_tick(MOVE);                           \
        return orig_##name args;                     \
    }
SHADOW_RUN_ORIGINAL(void, action_destroy, (int a, int b, int c, int d), (a, b, c, d))
SHADOW_RUN_ORIGINAL(void, action_arty, (int a, int b, int c), (a, b, c))
SHADOW_RUN_ORIGINAL(int, action, (int a), (a))
static int shadow_action_terraform(int veh, int terraform, int toggle) {
    if (holding_terraform && hooks_protecting()) {
        struct hook_ui_call c = {.name = "action_terraform", .replay = replay_none, .arg = {veh, terraform, toggle}};
        hooks_ui_hold(&c);
        return 0;
    }
    if (hooks_nested) return orig_action_terraform(veh, terraform, toggle);
    if (shadow_bypass()) return action_terraform(veh, terraform, toggle);
    stats_action_terraform.calls++;
    shadow_tick(MOVE);
    return check_action_terraform(veh, terraform, toggle);
}

// ---- Sweep ------------------------------------------------------------------------

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}

// A unit on the map of the given triad (or any, -1), from a hash.
static int pick_unit(uint32_t h, int triad) {
    int n = *game_veh_count;
    for (int k = 0; k < 48 && n > 0; k++) {
        int v = (int)(mix(h + (uint32_t)k * 0x9e3779b9u) % (uint32_t)n);
        const struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        if (triad >= 0 && chassis_of(V)->triad != triad && k < 40) continue;
        return v;
    }
    return -1;
}

// A unit next to a base (any faction's), from a hash, with the direction
// to the base; or -1.
static int unit_by_base(uint32_t h, int *dir) {
    int n = *game_veh_count;
    for (int k = 0; k < n && k < 400; k++) {
        int v = (int)((h + (uint32_t)k) % (uint32_t)n);
        const struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        for (int d = 0; d < 8; d++) {
            int x = xrange(V->x + game_radius_near_x[d]), y = V->y + game_radius_near_y[d];
            if (on_map(x, y) && (tile(x, y)->bits & MAP_BASE)) {
                *dir = d;
                return v;
            }
        }
    }
    return -1;
}

// A square on the map from a hash: near (x, y), a base's, or anywhere.
static void pick_square(uint32_t h, int x, int y, int *sx, int *sy) {
    *sx = x, *sy = y;
    switch (h % 4) {
    case 0: {
        int i = (int)((h >> 2) % 120);
        int xx = xrange(x + game_radius_x[i]), yy = y + game_radius_y[i];
        if (on_map(xx, yy)) *sx = xx, *sy = yy;
        break;
    }
    case 1:
        if (*game_base_count > 0) {
            const struct base *B = &game_bases[(h >> 2) % (uint32_t)*game_base_count];
            *sx = B->x, *sy = B->y;
        }
        break;
    default: {
        uint32_t n = (h >> 2) % (uint32_t)*game_map_area;
        *sy = (int)n / *game_map_half_width;
        *sx = (int)n % *game_map_half_width * 2 + (*sy & 1);
    }
    }
}

// Flips a faction's knowledge of a tech (for has_tech) and back.
static void flip_tech(int tech, int faction) {
    if (tech >= 0 && tech < TECH_COUNT) game_tech_achieved[tech] ^= (uint8_t)(1u << faction);
}

static void sweep_courses(int turn) {
    for (int k = 0; k < 24; k++) {
        uint32_t h = mix((uint32_t)turn * 7919u + (uint32_t)k);
        int v = pick_unit(h, k % 3 == 0 ? TRIAD_SEA : -1);
        if (v < 0) return;
        const struct veh *V = &game_vehs[v];
        int x, y;
        pick_square(h >> 3, V->x, V->y, &x, &y);
        check_set_course(v, "abc1"[(h >> 20) % 4], x, y);
    }
    for (int k = 0; k < 6 && *game_base_count > 0; k++) {
        uint32_t h = mix((uint32_t)turn * 104729u + (uint32_t)k);
        int b = (int)(h % (uint32_t)*game_base_count);
        static const int types[] = {0, 2, 3, 7, 8, 8, 1, 5};
        check_assemble_passengers(b, types[(h >> 8) % 8], (int)((h >> 12) % 4));
    }
}

// Units' state made to vary for an order: alert posts, patrol bits,
// damage, moves and turns aloft. Restored by the caller's protection.
static void vary_unit(struct veh *V, uint32_t h) {
    if (h & 1) V->state |= VEH_STATE_UNK_200;
    else V->state &= ~(uint32_t)VEH_STATE_UNK_200;
    if ((h >> 1) % 3 == 0) V->order_auto_type = ORDER_AUTO_ALERT;
    if ((h >> 3) % 3 == 0) V->damage = (uint8_t)((h >> 5) % 12);
    if ((h >> 9) % 2) V->moves_spent = (uint8_t)((h >> 10) % 10);
    if ((h >> 14) % 3 == 0) V->movement_turns = (uint8_t)((h >> 16) % 3);
}

static void sweep_home(int turn) {
    for (int k = 0; k < 30; k++) {
        uint32_t h = mix((uint32_t)turn * 31337u + (uint32_t)k);
        int v = pick_unit(h, (int)(k % 3));
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        uint8_t human = *game_human_factions;
        vary_unit(V, h >> 4);
        // An alert post: a base, an airbase near, or a square near.
        if ((h >> 24) % 2) {
            int x, y;
            pick_square(h >> 25, V->x, V->y, &x, &y);
            for (int i = 0; i < 25 && (h >> 26) % 2; i++) {
                int xx = xrange(V->x + game_radius_x[i]), yy = V->y + game_radius_y[i];
                if (on_map(xx, yy) && (tile(xx, yy)->bits & MAP_AIRBASE)) x = xx, y = yy;
            }
            waypoint_x(V)[1] = (int16_t)x, waypoint_y(V)[1] = (int16_t)y;
        }
        if ((h >> 28) % 3 == 0) *game_human_factions ^= (uint8_t)(1u << V->faction);
        static const int types[] = {0, 1, 2, 3, 4, -1, 5, 0};
        check_action_home(v, types[(h >> 21) % 8]);
        *game_human_factions = human;
        *V = saved;
    }
}

static void sweep_patrol(int turn) {
    for (int k = 0; k < 30; k++) {
        uint32_t h = mix((uint32_t)turn * 65537u + (uint32_t)k);
        int v = pick_unit(h, (int)(k % 3));
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        vary_unit(V, h >> 4);
        int x, y;
        pick_square(h >> 8, V->x, V->y, &x, &y);
        switch ((h >> 24) % 8) {
        case 0: x = V->x, y = V->y; break;  // its own square
        case 1: x = -1 - (int)((h >> 27) % 2); break;
        case 2: y = *game_map_height; break;
        case 3:  // base 0's square
            if (*game_base_count > 0) x = game_bases[0].x, y = game_bases[0].y;
            break;
        }
        check_valid_patrol(v, x, y);
        V->waypoint_count = (uint8_t)((h >> 29) % 3);
        check_action_patrol(v, x, y);
        *V = saved;
    }
    // Air units with a range, at the edges of their reach and half of it,
    // with base 0 moved for a moment to the far end (no landing place, to
    // the original) or not. A unit of another kind is given the design.
    int air = -1;
    for (int k = 0; k < PROTO_COUNT && air < 0; k++) {
        int p = (int)((mix((uint32_t)turn) + (uint32_t)k) % PROTO_COUNT);
        const struct unit_proto *P = &game_protos[p];
        if (P->name[0] && game_chassis[P->chassis].triad == TRIAD_AIR && game_chassis[P->chassis].range) air = p;
    }
    for (int k = 0; k < 16 && air >= 0 && *game_base_count > 0; k++) {
        uint32_t h = mix((uint32_t)turn * 9973u + (uint32_t)k);
        int v = pick_unit(h, TRIAD_AIR);
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        if (chassis_of(V)->triad != TRIAD_AIR || !chassis_of(V)->range) V->proto = (int16_t)air;
        V->movement_turns = (uint8_t)((h >> 4) % 2);
        V->moves_spent = (uint8_t)((h >> 5) % 6);
        int range = chassis_of(V)->range;
        int reach = (veh_speed(v, 0) * (range - V->movement_turns - 1) + moves_left(v)) / game_rules_basic->move_rate_roads;
        int r = (int)((h >> 8) % 4), d = r < 2 ? reach / 2 + r : reach + r - 2;
        int x = xrange(V->x + 2 * d), y = V->y;
        if (!on_map(x, y) || (tile(x, y)->bits & MAP_BASE)) {
            *V = saved;
            continue;
        }
        struct base *B = &game_bases[0];
        struct map_tile *bt = tile(B->x, B->y), *t = tile(x, y), saved_t = *t;
        int bx = B->x, by = B->y;
        uint32_t bbits = bt->bits;
        if ((h >> 10) & 1) {
            bt->bits &= ~(uint32_t)MAP_BASE;
            t->bits = (t->bits | MAP_BASE) & ~(uint32_t)MAP_AIRBASE;
            B->x = (int16_t)x, B->y = (int16_t)y;
        }
        check_valid_patrol(v, x, y);
        B->x = (int16_t)bx, B->y = (int16_t)by;
        *t = saved_t;
        bt->bits = bbits;
        *V = saved;
    }
}

// Orders swept with the step and a former's work held.
static void sweep_orders(int turn) {
    holding = holding_terraform = 1;
    for (int k = 0; k < 24; k++) {
        uint32_t h = mix((uint32_t)turn * 2654435761u + (uint32_t)k);
        int to_base = -1;
        int v = k % 4 == 2 ? unit_by_base(h, &to_base) : pick_unit(h, k % 4 == 3 ? TRIAD_AIR : -1);
        if (v < 0) continue;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        uint8_t human = *game_human_factions;
        int x, y;
        pick_square(h >> 4, V->x, V->y, &x, &y);
        if ((h >> 12) % 4 == 0) x = V->x, y = V->y;
        if (to_base >= 0) x = xrange(V->x + game_radius_near_x[to_base]), y = V->y + game_radius_near_y[to_base];
        waypoint_x(V)[0] = (int16_t)x, waypoint_y(V)[0] = (int16_t)y;
        static const uint8_t orders[] = {ORDER_MOVE_TO, ORDER_MOVE_TO, ORDER_MOVE_TO | ORDER_AI, ORDER_MOVE};
        V->order = orders[(h >> 14) % 4];
        if (V->order == ORDER_MOVE) {
            int d = (int)((h >> 16) % 8);
            waypoint_x(V)[0] = (int16_t)d;
            int xx = xrange(V->x + game_radius_near_x[d]), yy = V->y + game_radius_near_y[d];
            waypoint_x(V)[1] = (int16_t)xx, waypoint_y(V)[1] = (int16_t)yy;
        } else if ((h >> 16) % 2 && to_base < 0) {  // the goal a step away, so that the step arrives
            int d = (int)((h >> 17) % 8);
            int xx = xrange(V->x + game_radius_near_x[d]), yy = V->y + game_radius_near_y[d];
            if (on_map(xx, yy)) waypoint_x(V)[0] = (int16_t)xx, waypoint_y(V)[0] = (int16_t)yy;
        }
        V->state &= ~0x3070200u;
        static const uint32_t states[] = {0, 0x2000200, 0x3000200, 0x1000200, 0x10000, 0x60000};
        V->state |= states[(h >> 20) % 6];
        V->waypoint_count = (uint8_t)((h >> 23) % 4);
        V->patrol_point = (uint8_t)((h >> 25) % 3);
        if ((h >> 27) % 2) *game_human_factions ^= (uint8_t)(1u << V->faction);
        hold_result = (h >> 28) % 4 != 0;
        hold_moves = (h >> 30) % 4 != 0;
        check_action_go_to(v);
        *game_human_factions = human;
        *V = saved;
    }
    // Formers (any unit) building their way.
    for (int k = 0; k < 24; k++) {
        uint32_t h = mix((uint32_t)turn * 40503u + (uint32_t)k + 0x777);
        int v = pick_unit(h, TRIAD_LAND);
        if (v < 0) break;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        struct map_tile *t = tile(V->x, V->y), saved_tile = *t;
        int faction = V->faction, cur = *game_current_faction;
        int x, y;
        pick_square(h >> 4, V->x, V->y, &x, &y);
        switch ((h >> 12) % 6) {
        case 0: x = V->x, y = V->y; break;
        case 1: x = -1; break;
        }
        waypoint_x(V)[0] = (int16_t)x, waypoint_y(V)[0] = (int16_t)y;
        V->order = (h >> 15) % 2 ? ORDER_ROAD_TO : ORDER_MAGTUBE_TO;
        static const uint32_t bits[] = {0, MAP_ROAD, MAP_ROAD | MAP_MAGTUBE, MAP_FUNGUS, MAP_FUNGUS | MAP_ROAD, MAP_ROAD};
        if ((h >> 16) % 3) t->bits = (t->bits & ~(uint32_t)(MAP_ROAD | MAP_MAGTUBE | MAP_FUNGUS)) | bits[(h >> 18) % 6];
        if ((h >> 21) % 2) V->state |= VEH_STATE_UNK_200;
        else V->state &= ~(uint32_t)VEH_STATE_UNK_200;
        if ((h >> 22) % 2) *game_current_faction = faction;
        int t1 = (h >> 23) % 3 == 0 ? game_rules_basic->tech_road_fungus : -1;
        int t2 = (h >> 25) % 3 == 0 ? game_terraform[TERRAFORM_REMOVE_FUNGUS].preq_tech : -1;
        int t3 = (h >> 27) % 3 == 0 ? game_terraform[TERRAFORM_MAG_TUBE].preq_tech : -1;
        flip_tech(t1, faction), flip_tech(t2, faction), flip_tech(t3, faction);
        hold_result = 1;
        hold_moves = 0;
        check_action_road_to(v);
        flip_tech(t1, faction), flip_tech(t2, faction), flip_tech(t3, faction);
        *game_current_faction = cur;
        *t = saved_tile;
        *V = saved;
    }
    holding = holding_terraform = 0;
}

// ---- Actions: sweep -------------------------------------------------------------------
//
// On the sweep's copy of the whole game (put back at the end): the AI's
// choices and costs of terraforming on squares of every kind, formers'
// work of every kind (finishing or not, alone and in stacks), and units'
// other actions, with the circumstances that steer them varied.

// Map bits a square is given for a case, from a hash.
static uint32_t vary_bits(uint32_t h) {
    static const uint32_t pick[] = {MAP_MINE_, MAP_SOLAR_, MAP_FARM_, MAP_SOIL_, MAP_FOREST, MAP_FUNGUS, MAP_ROAD,
                                    MAP_THERMAL_BORE, MAP_CONDENSER, MAP_ECHELON_MIRROR, MAP_MONOLITH, MAP_RIVER,
                                    MAP_SENSOR, MAP_MAGTUBE};
    uint32_t bits = 0;
    for (int i = 0; i < 14; i++)
        if ((mix(h + (uint32_t)i) % 5) == 0) bits |= pick[i];
    return bits;
}
static void vary_tile(struct map_tile *t, uint32_t h) {
    static const uint32_t cleared = MAP_MINE_ | MAP_SOLAR_ | MAP_FARM_ | MAP_SOIL_ | MAP_FOREST | MAP_FUNGUS | MAP_ROAD |
                                    MAP_THERMAL_BORE | MAP_CONDENSER | MAP_ECHELON_MIRROR | MAP_MONOLITH | MAP_RIVER |
                                    MAP_SENSOR | MAP_MAGTUBE;
    if (h % 4) t->bits = (t->bits & ~cleared) | vary_bits(h >> 2);
    if ((h >> 6) % 3 == 0) t->rocky_lock = (uint8_t)((t->rocky_lock & 0x3f) | ((h >> 8) % 3) << 6);
    if ((h >> 10) % 3 == 0) t->climate = (uint8_t)((t->climate & ~0x18) | ((h >> 12) % 4) << 3);
    if ((h >> 14) % 8 == 0) t->bits2 = (t->bits2 & ~0xff000002u) | 2 | ((h >> 17) % 3 == 0 ? 0x01000000u : 0);
}
// Flips the faction's knowledge of each tech of the list, by a hash.
static void flip_techs(const int *techs, int n, int faction, uint32_t h) {
    for (int i = 0; i < n; i++)
        if (mix(h + (uint32_t)i * 77u) % 3 == 0) flip_tech(techs[i], faction);
}
// A faction from a hash, 1 to 7.
static int pick_faction(uint32_t h) { return (int)(h % (FACTION_COUNT - 1)) + 1; }

static void sweep_terraform_choice(int turn) {
    const struct rules_terraform *T = game_terraform;
    int techs[] = {T[TF_FARM].preq_tech, T[TF_SOIL].preq_tech, T[TF_MINE].preq_tech, T[TF_SOLAR].preq_tech,
                   T[TF_FOREST].preq_tech, T[TF_PLANT_FUNGUS].preq_tech, T[TF_REMOVE_FUNGUS].preq_tech_sea,
                   T[TF_FARM].preq_tech_sea, T[TF_SOIL].preq_tech_sea, T[TF_MINE].preq_tech_sea,
                   T[TF_SOLAR].preq_tech_sea, T[TF_RAISE].preq_tech_sea, game_rules_basic->tech_three_nutrients, 39};
    for (int k = 0; k < 160 && *game_base_count > 0; k++) {
        uint32_t h = mix((uint32_t)turn * 6151u + (uint32_t)k + 0x5151);
        int b = (int)(h % (uint32_t)*game_base_count);
        struct base *B = &game_bases[b];
        int x, y;
        pick_square(h >> 9, B->x, B->y, &x, &y);
        if ((h >> 7) % 4) {  // in the base's radius, mostly
            int i = (int)((h >> 11) % BASE_RADIUS_SQUARES);
            int xx = xrange(B->x + game_radius_x[i]), yy = B->y + game_radius_y[i];
            if (on_map(xx, yy)) x = xx, y = yy;
        }
        if (!on_map(x, y)) continue;
        int faction = (h >> 16) % 3 ? B->faction : pick_faction(h >> 18);
        int base = (h >> 21) % 4 ? b : -1;
        struct map_tile *t = tile(x, y), saved_t = *t;
        struct base saved_b = *B;
        struct faction_data *F = &game_factions[faction];
        int32_t ai[5] = {F->ai_fight, F->ai_growth, F->ai_tech, F->ai_wealth, F->ai_power}, fungus[3];
        memcpy(fungus, F->tech_fungus, sizeof fungus);
        uint8_t human = *game_human_factions;
        int32_t prefs = *game_preferences, more = *game_more_preferences, cur = *game_current_faction, tn = *game_turn;
        int32_t mine_effect = game_rules_basic->nutrient_effect_mine;
        uint32_t v = mix(h);
        vary_tile(t, v);
        if ((v >> 20) % 5 == 0) t->climate = (uint8_t)((t->climate & 0x1f) | ((v >> 23) % 7 + 1) << 5);
        if ((v >> 26) % 2) B->state = (B->state & ~0x7000u) | (uint32_t)((v >> 27) % 8) << 12;
        uint32_t w = mix(v);
        if (w % 3 == 0) B->surplus[0] = (int32_t)((w >> 2) % 5) - 1;
        if ((w >> 5) % 3 == 0) B->eco_damage = (int32_t)((w >> 7) % 12);
        if ((w >> 11) % 4 == 0) *game_human_factions ^= (uint8_t)(1u << faction);
        if ((w >> 13) % 3 == 0) *game_more_preferences ^= 8;
        if ((w >> 15) % 3 == 0) *game_preferences ^= 0x1000000;
        if ((w >> 17) % 3 == 0) *game_current_faction = faction;
        if ((w >> 19) % 4 == 0) *game_turn = (int32_t)((w >> 21) % 100);
        if ((w >> 23) % 4 == 0) game_rules_basic->nutrient_effect_mine = -mine_effect - 1;
        if ((w >> 25) % 4 == 0) {
            F->ai_growth = 1, F->ai_power = F->ai_wealth = F->ai_tech = 0, F->ai_fight = -1;
        }
        if ((w >> 27) % 4 == 0)
            for (int i = 0; i < 3; i++) F->tech_fungus[i] = (int32_t)((w >> (29 - i)) % 3);
        flip_techs(techs, (int)(sizeof techs / sizeof techs[0]), faction, w);
        check_can_terraform(faction, x, y, (int)(h >> 23) & 1, base);
        if (k % 4 == 0) check_terraform_cost(x, y, (h >> 24) % 5 ? faction : pick_faction(h >> 25));
        flip_techs(techs, (int)(sizeof techs / sizeof techs[0]), faction, w);
        *t = saved_t, *B = saved_b;
        F->ai_fight = ai[0], F->ai_growth = ai[1], F->ai_tech = ai[2], F->ai_wealth = ai[3], F->ai_power = ai[4];
        memcpy(F->tech_fungus, fungus, sizeof fungus);
        *game_human_factions = human;
        *game_preferences = prefs, *game_more_preferences = more, *game_current_faction = cur, *game_turn = tn;
        game_rules_basic->nutrient_effect_mine = mine_effect;
    }
    // Terraforming's cost: the bonus for it, and doctrine 39 unknown to all.
    for (int k = 0; k < 24; k++) {
        uint32_t h = mix((uint32_t)turn * 4099u + (uint32_t)k + 0x777);
        int x, y;
        pick_square(h, 0, 0, &x, &y);
        if (!on_map(x, y)) continue;
        int faction = pick_faction(h >> 8);
        uint8_t doctrine = game_tech_achieved[39];
        int32_t flags = game_faction_defs[faction].rule_flags;
        if ((h >> 12) % 2) game_tech_achieved[39] = 0;
        if ((h >> 13) % 2) game_faction_defs[faction].rule_flags ^= 0x2000;
        check_terraform_cost(x, y, faction);
        game_tech_achieved[39] = doctrine;
        game_faction_defs[faction].rule_flags = flags;
    }
}

// A former's work of every kind, on squares made to suit or not, with some
// of its stack on the same order and its work nearly done.
static void sweep_terraform_work(int turn) {
    const struct rules_terraform *T = game_terraform;
    int techs[] = {T[TF_MAGTUBE].preq_tech, T[TF_MAGTUBE].preq_tech_sea, T[TF_FARM].preq_tech};
    for (int k = 0; k < 28; k++) {
        uint32_t h = mix((uint32_t)turn * 92821u + (uint32_t)k + 0x31);
        int v = pick_unit(h, k % 4 == 3 ? -1 : TRIAD_LAND);
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        int faction = V->faction, tf = (int)((h >> 6) % TERRAFORM_COUNT);
        if (k % 3 == 0) tf = (int)((h >> 6) % 7);  // the usual ones more often
        int toggle = (h >> 11) % 4 != 0;
        vary_tile(tile(V->x, V->y), mix(h));
        uint32_t w = mix(h + 1);
        V->movement_turns = (uint8_t)(w % 3 ? (w >> 2) % 30 : 0);
        if ((w >> 7) % 3 == 0) V->moves_spent = (uint8_t)((w >> 9) % 8);
        if ((w >> 12) % 2) V->state |= VEH_STATE_UNK_200;
        else V->state &= ~(uint32_t)VEH_STATE_UNK_200;
        if ((w >> 13) % 3 == 0) V->home_base = -1;
        for (int c = stack_top(v), i = 0; c >= 0; c = game_vehs[c].next_in_stack, i++)
            if (c != v && (mix(w + (uint32_t)i) % 2)) {
                game_vehs[c].order = (uint8_t)(tf + ORDER_FIRST_TERRAFORM);
                game_vehs[c].movement_turns = (uint8_t)(mix(w + (uint32_t)i + 9) % 6);
            }
        uint32_t u = mix(w);
        if (u % 3 == 0) *game_current_faction = faction;
        if ((u >> 2) % 2) *game_preferences |= 0x20;
        if ((u >> 3) % 2) game_tut_map_seen[0] = game_tut_map_seen[1] = 0;
        if ((u >> 4) % 2) *game_terraform_sound_played = 0;
        if ((u >> 5) % 4 == 0) *game_human_factions ^= (uint8_t)(1u << faction);
        if ((u >> 7) % 3 == 0) *game_difficulty = (int32_t)((u >> 9) % 6);
        if ((u >> 12) % 4 == 0 && *game_base_count > 0) {  // the projects that speed the work
            int b = (int)((u >> 14) % (uint32_t)*game_base_count);
            game_secret_projects[SP_WEATHER_PARADIGM] = (u >> 23) % 2 ? b : -1;
            game_secret_projects[SP_XENOEMPATHY_DOME] = (u >> 24) % 2 ? b : -1;
        }
        flip_techs(techs, 3, faction, u >> 3);
        check_action_terraform(v, tf, toggle);
        if (toggle && (u >> 25) % 2) check_action_terraform(v, tf, 0);
    }
}

// Whether action_destroy() might start a fight at (x, y): to be left out
// of the sweep's checks.
static int destroy_fights(int veh, int x, int y) {
    const struct veh *V = &game_vehs[veh];
    if (!on_map(x, y) || faction_on(tile(x, y), MAP_BASE)) return 0;
    int owner = whose_territory(V->faction, x, y, NULL, 0);
    if (owner == V->faction) return 0;
    for (int i = 1; i < 25; i++) {
        int px = xrange(V->x + game_radius_x[i]), py = V->y + game_radius_y[i];
        if (!on_map(px, py) || ((x_dist(px, x) + abs(py - y)) & ~1) > 4) continue;
        int s = stack_fix(veh_at(px, py));
        if (s >= 0 && stack_check(s, 15, owner, -1, -1)) return 1;
    }
    return 0;
}

// Units' attacks on squares, orders carried out, and the rest, with the
// step (order_veh) held.
static void sweep_unit_actions(int turn) {
    holding = 1, hold_result = 1, hold_moves = 0;
    for (int k = 0; k < 20; k++) {  // destroying
        uint32_t h = mix((uint32_t)turn * 15485863u + (uint32_t)k);
        int v = pick_unit(h, k % 3 == 0 ? TRIAD_AIR : -1);
        if (v < 0) break;
        struct veh *V = &game_vehs[v];
        int x = V->x, y = V->y;
        if ((h >> 4) % 6) {
            int i = (int)((h >> 7) % 25);
            x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        } else if ((h >> 7) % 2) {
            x = -1;
        }
        if (on_map(x, y)) vary_tile(tile(x, y), mix(h));
        uint32_t w = mix(h + 3);
        if (w % 5 == 0) V->faction = 0;
        static const int items[] = {0, 0, MAP_ROAD, MAP_FARM_ | MAP_SOIL_, MAP_MINE_, MAP_FOREST | MAP_FUNGUS,
                                    (int)MAP_SENSOR, -1, MAP_SOLAR_ | MAP_CONDENSER, 0x7fffffff};
        int item = items[(w >> 3) % 10];
        if ((w >> 7) % 3 == 0) *game_current_faction = V->faction;
        else if ((w >> 7) % 3 == 1 && on_map(x, y)) *game_current_faction = whose_territory(V->faction, x, y, NULL, 0) & 7;
        if ((w >> 9) % 3 == 0 && on_map(x, y)) tile(x, y)->visibility ^= (uint8_t)(1u << *game_current_faction);
        if ((w >> 11) % 3 == 0)
            for (int f = 0; f < FACTION_COUNT; f++) game_factions[V->faction].diplo_status[f] ^= TREATY_COMMLINK;
        if ((w >> 13) % 4 == 0) V->proto = 15;  // the spore launcher's own messages
        if (destroy_fights(v, x, y)) continue;
        check_action_destroy(v, item, x, y);
    }
    for (int k = 0; k < 14; k++) {  // artillery
        uint32_t h = mix((uint32_t)turn * 179424673u + (uint32_t)k);
        int v = pick_unit(h, -1);
        if (v < 0) break;
        struct veh *V = &game_vehs[v];
        int i = (int)((h >> 4) % 40);
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if ((h >> 10) % 3 == 0) *game_current_faction = V->faction;
        if ((h >> 12) % 3 == 0) V->moves_spent = (uint8_t)veh_speed(v, 0);
        if (on_map(x, y)) {
            int s = stack_fix(veh_at(x, y));
            if (s >= 0 && game_vehs[s].faction != V->faction &&
                !(game_factions[V->faction].diplo_status[game_vehs[s].faction] & TREATY_PACT))
                continue;  // a fight, or nothing (out of the range table)
            if (s < 0 && destroy_fights(v, x, y)) continue;
        }
        if (!on_map(x, y)) continue;
        check_action_arty(v, x, y);
    }
    for (int k = 0; k < 16; k++) {  // orders carried out
        uint32_t h = mix((uint32_t)turn * 2246822519u + (uint32_t)k);
        int v = pick_unit(h, -1);
        if (v < 0) break;
        struct veh *V = &game_vehs[v];
        static const uint8_t orders[] = {4, 5, 6, 7, 9, 13, 14, 20, 0x17, 0x18, 0x58, 0x19, 0x1b, 0x1c, 0, 0x1a, 0x5b, 3};
        V->order = orders[(h >> 4) % 18];
        if (V->order >= 0x18 && V->order != 0x1a) {
            int x, y;
            pick_square(h >> 9, V->x, V->y, &x, &y);
            waypoint_x(V)[0] = (int16_t)(V->order == ORDER_MOVE ? (h >> 20) % 8 : (uint32_t)x);
            waypoint_y(V)[0] = (int16_t)y;
            waypoint_x(V)[1] = (int16_t)x, waypoint_y(V)[1] = (int16_t)y;
        }
        V->movement_turns = (uint8_t)((h >> 24) % 12);
        check_action(v);
    }
    holding = 0;
}

static void sweep_other_actions(int turn) {
    int bases = *game_base_count;
    for (int k = 0; k < 6 && bases > 0; k++) {  // nerve stapling
        uint32_t h = mix((uint32_t)turn * 433494437u + (uint32_t)k);
        struct base *B = &game_bases[h % (uint32_t)bases];
        B->nerve_staple_count = (int32_t)((h >> 9) % 11);
        if ((h >> 13) % 3 == 0) *game_current_faction = B->faction;
        if ((h >> 15) % 3 == 0) B->faction_former = (uint8_t)pick_faction(h >> 17);
        if ((h >> 20) % 3 == 0) game_faction_defs[B->faction].rule_flags ^= RULE_FLAG_ALIEN;
        check_action_staple((int)(h % (uint32_t)bases));
    }
    for (int k = 0; k < 5; k++) {  // blowing up
        uint32_t h = mix((uint32_t)turn * 1597334677u + (uint32_t)k);
        int v = pick_unit(h, -1);
        if (v < 0) break;
        if ((h >> 6) % 3 == 0) game_vehs[v].damage = (uint8_t)((h >> 8) % 10);
        check_action_destruct(v);
    }
    for (int k = 0; k < 3 && bases > 1; k++) {  // obliterating
        uint32_t h = mix((uint32_t)turn * 3266489917u + (uint32_t)k);
        int v = pick_unit(h, -1), b = (int)((h >> 8) % (uint32_t)*game_base_count);
        if (v < 0 || base_listed(b)) continue;
        if ((h >> 4) % 3 == 0) *game_current_faction = game_vehs[v].faction;
        else if ((h >> 4) % 3 == 1) *game_current_faction = game_bases[b].faction_former;
        if ((h >> 6) % 2) game_rules_basic->oblit_atrocity = !game_rules_basic->oblit_atrocity;
        check_action_oblit(v, b);
    }
    for (int k = 0; k < 6; k++) {  // missiles
        uint32_t h = mix((uint32_t)turn * 668265263u + (uint32_t)k);
        int v = pick_unit(h, -1);
        if (v < 0) break;
        int x, y;
        pick_square(h >> 4, game_vehs[v].x, game_vehs[v].y, &x, &y);
        if (!on_map(x, y)) continue;
        if ((h >> 20) % 3 == 0) *game_current_faction = game_vehs[v].faction;
        else if ((h >> 20) % 3 == 1 && base_find(x, y) >= 0) *game_current_faction = game_bases[base_find(x, y)].faction;
        if ((h >> 22) % 3 == 0) tile(x, y)->visibility ^= (uint8_t)(1u << *game_current_faction);
        if (k % 2) check_action_tectonic(v, x, y);
        else check_action_fungal(v, x, y);
    }
    for (int k = 0; k < 4 && bases > 0; k++) {  // psi gates
        uint32_t h = mix((uint32_t)turn * 2654435789u + (uint32_t)k);
        int v = pick_unit(h, -1);
        if (v < 0) break;
        if ((h >> 4) % 4 == 0) *game_veh_ref_map_1 = v;
        check_action_gate(v, (int)((h >> 8) % (uint32_t)bases));
    }
}

// Units founding bases: not on the sweep's copy of the game, as a base's
// name is found in its faction's file, which mustn't be opened while state
// is protected (a check puts back each case, and the rest is put back by
// hand).
static void sweep_found(int turn) {
    int bases = *game_base_count;
    for (int k = 0; k < 5; k++) {
        uint32_t h = mix((uint32_t)turn * 2971215073u + (uint32_t)k);
        int v = pick_unit(h, (h >> 3) % 4 ? TRIAD_LAND : -1);
        if (v < 0 || *game_base_count >= BASE_COUNT) break;
        struct veh *V = &game_vehs[v];
        struct map_tile *t = tile(V->x, V->y);
        if (faction_on(t, MAP_BASE)) continue;
        struct veh saved = *V;
        uint8_t rocky = t->rocky_lock, human = *game_human_factions;
        int32_t cur = *game_current_faction;
        if (!V->faction) V->faction = (uint8_t)pick_faction(h >> 5);
        if ((h >> 8) % 3 == 0) t->rocky_lock = (uint8_t)((t->rocky_lock & 0x3f) | 0x80);
        if ((h >> 10) % 3 == 0) *game_current_faction = V->faction;
        if ((h >> 12) % 4 == 0) *game_human_factions ^= (uint8_t)(1u << V->faction);
        if ((h >> 14) % 2) V->state |= VEH_STATE_UNK_200;
        if ((h >> 15) % 3 == 0 && bases > 0) V->home_base = (int16_t)((h >> 17) % (uint32_t)bases);
        static char name[] = "Sweep Base";
        checked_action_build(v, (h >> 26) % 3 == 0 ? name : NULL);
        *V = saved;
        t->rocky_lock = rocky, *game_human_factions = human, *game_current_faction = cur;
    }
}

// ---- The step: sweep ------------------------------------------------------------------
//
// Steps on the sweep's copy of the game, among AI factions: units of every
// faction (native life too), triad and kind (some given another design:
// probe teams, supply crawlers, artifacts, transports, colony pods,
// formers, native units, air superiority, artillery), in each of the
// eight directions and staying, from squares by bases, transports,
// enemies, pods, fungus, bunkers, shores and monoliths, with their moves,
// damage, orders, states, turns aloft and treaties varied, the target
// square's improvements, pods and fungus toggled, native life's setting
// and the factions' Planet ratings changed, the player made the unit's
// faction, the target's or another, the player's preferences varied, and
// moves that happen, that are seen, and both. A case's unit and square are
// put back after it (its design too), unless units were renumbered;
// whatever else the steps leave stays on the copy. Cases the check
// leaves to the original (see order_checkable) are skipped.

// A unit standing next to another faction's units, from a hash, with the
// direction to them; or -1.
static int unit_by_enemy(uint32_t h, int *dir) {
    int n = *game_veh_count;
    for (int k = 0; k < n && k < 400; k++) {
        int v = (int)((h + (uint32_t)k * 7u) % (uint32_t)n);
        const struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        for (int d = 0; d < 8; d++) {
            int x = xrange(V->x + game_radius_near_x[d]), y = V->y + game_radius_near_y[d];
            if (!on_map(x, y)) continue;
            int s = stack_fix(veh_at(x, y));
            if (s >= 0 && game_vehs[s].faction != V->faction) {
                *dir = d;
                return v;
            }
        }
    }
    return -1;
}

// An existing design of the given plan, or -1.
static int design_of_plan(uint32_t h, int plan) {
    for (int k = 0; k < PROTO_COUNT; k++) {
        int p = (int)((h + (uint32_t)k) % PROTO_COUNT);
        if ((game_protos[p].flags & PROTO_FLAG_ACTIVE) && game_protos[p].plan == plan) return p;
    }
    return -1;
}

static void sweep_steps(int turn) {
    uint8_t human = *game_human_factions;
    int cur = *game_current_faction, native = *game_native_life;
    uint32_t prefs = *game_preferences, more = *game_more_preferences;
    static const uint8_t plans[] = {OV_PLAN_PROBE, OV_PLAN_SUPPLY, OV_PLAN_ARTIFACT, OV_PLAN_NAVAL_TRANSPORT,
                                    OV_PLAN_COLONY, OV_PLAN_TERRAFORM};
    static const int16_t natives[] = {OV_BSC_MIND_WORMS, OV_BSC_ISLE, OV_BSC_SEALURK, OV_BSC_SPORE_LAUNCHER};
    static const uint32_t bits[] = {MAP_FUNGUS, MAP_MONOLITH, MAP_SUPPLY_POD, OV_MAP_BUNKER, MAP_AIRBASE, MAP_ROAD,
                                    MAP_MAGTUBE, OV_MAP_SUPPLY_REMOVE, MAP_SENSOR};
    static const uint32_t states[] = {OV_STATE_ALERT, OV_STATE_EXPLORE, OV_STATE_SLOWED, OV_STATE_FACING,
                                      OV_STATE_ZOC, OV_STATE_PATROL, OV_STATE_SENTRY_ALERT, OV_STATE_MONOLITH};
    static const uint32_t prefs_varied[] = {OV_PREF_QUICK_ENEMY, OV_PREF_QUICK_ALLY, OV_PREF_TUTORIAL, OV_PREF_AIR_HOME,
                                            OV_PREF_NO_CENTER, (uint32_t)OV_PREF_WAKE_ON_LAND};
    static const uint32_t treaties[] = {TREATY_PACT, TREATY_TREATY, OV_TREATY_TRUCE_TREATY & ~TREATY_TREATY,
                                        TREATY_VENDETTA, TREATY_SHALL_BETRAY};
    *game_human_factions = 0;
    for (int k = 0; k < 72; k++) {
        uint32_t h = mix((uint32_t)turn * 3266489917u + (uint32_t)k * 668265263u + 0x0e7e);
        int dir = (int)(h % 10), v;
        switch (k % 6) {
        case 0: v = unit_by_base(h >> 4, &dir); break;
        case 1: v = unit_by_enemy(h >> 4, &dir); break;
        case 2: v = pick_unit(h >> 4, TRIAD_SEA); break;
        case 3: v = pick_unit(h >> 4, TRIAD_AIR); break;
        default: v = pick_unit(h >> 4, -1); break;
        }
        if (v < 0) continue;
        if (dir == 9) dir = -1;
        uint32_t u = mix(h + 1), w = mix(h + 2);
        struct veh *V = &game_vehs[v], saved = *V;
        int count = *game_veh_count;
        // The unit.
        if (u % 5 == 0) {
            int p = design_of_plan(u >> 3, plans[(u >> 8) % 6]);
            if (p >= 0) V->proto = (int16_t)p;
        } else if (u % 7 == 0) {
            V->proto = natives[(u >> 8) % 4];
            if ((u >> 10) % 2 && V->prev_in_stack < 0 && V->next_in_stack < 0) V->faction = 0;  // alone: native life
        }
        struct unit_proto *P = &game_protos[V->proto], saved_proto = *P;
        if ((u >> 11) % 6 == 0) P->abilities ^= (u >> 14) % 2 ? OV_ABL_AIR_SUPERIORITY : OV_ABL_AMPHIBIOUS;
        if ((u >> 12) % 9 == 0) P->abilities ^= OV_ABL_CLOAKED;
        V->moves_spent = (uint8_t)((u >> 15) % 3 ? (u >> 17) % 4 : veh_speed(v, 0) - (int)((u >> 17) % 3));
        if ((u >> 19) % 3 == 0) V->damage = (uint8_t)((u >> 21) % 12);
        if ((u >> 23) % 3 == 0) V->movement_turns = (uint8_t)((u >> 25) % 4);
        if ((u >> 27) % 4 == 0) V->iter_count = (uint8_t)(20 + (u >> 29) % 6);
        for (int i = 0; i < 8; i++)
            if ((mix(u + (uint32_t)i) % 5) == 0) V->state ^= states[i];
        if ((w % 4) == 0) {
            int x, y;
            pick_square(w >> 2, V->x, V->y, &x, &y);
            V->order = ORDER_MOVE_TO;
            V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
        } else if ((w % 4) == 1) {
            V->order = ORDER_NONE;
        }
        if ((w >> 6) % 5 == 0) V->flags ^= OV_FLAG_PROBE_PACT;
        V->waypoint_count = (uint8_t)((w >> 8) % 4), V->patrol_point = (uint8_t)((w >> 10) % 4);
        // The square it goes to.
        int x, y, f = V->faction;
        int on = step_target(v, dir, &x, &y);
        struct map_tile *T = on ? tile(x, y) : NULL;
        uint32_t toggled = 0;
        if (T)
            for (int i = 0; i < 9; i++)
                if ((mix(w + 16 + (uint32_t)i) % 6) == 0) toggled |= bits[i];
        if (T) T->bits ^= toggled;
        int s = T ? stack_fix(veh_at(x, y)) : -1, b = T ? base_at(x, y) : -1;
        int tf = b >= 0 ? game_bases[b].faction : s >= 0 ? game_vehs[s].faction : -1;
        uint32_t d1 = 0, d2 = 0;
        if (tf >= 0 && tf != f) {
            d1 = treaties[(w >> 12) % 5], d2 = (w >> 15) % 2 ? treaties[(w >> 16) % 5] : 0;
            game_factions[f].diplo_status[tf] ^= d1;
            game_factions[tf].diplo_status[f] ^= d2;
        }
        // The territory its order heads into made known, at times with a pact.
        int owner = -1;
        uint32_t d3 = 0;
        if (V->order == ORDER_MOVE_TO && on_map(V->waypoint_x[0], V->waypoint_y[0])) {
            owner = tile(V->waypoint_x[0], V->waypoint_y[0])->territory;
            if (owner > 0 && owner != f && owner != tf) {
                const uint32_t known = TREATY_COMMLINK | 0x8000000;  // what whose_territory() asks for
                d3 = (game_factions[f].diplo_status[owner] & known) ^ known;
                if (mix(w + 40) % 2) d3 ^= TREATY_PACT;
                game_factions[f].diplo_status[owner] ^= d3;
            } else {
                owner = -1;
            }
        }
        int planet = game_factions[f].se[SE_PLANET], planet_base = game_factions[f].se_base[SE_PLANET];
        if ((w >> 19) % 3 == 0) game_factions[f].se[SE_PLANET] = (int)((w >> 21) % 5) - 1;
        if ((w >> 24) % 3 == 0) game_factions[f].se_base[SE_PLANET] = (int)((w >> 26) % 4) - 1;
        *game_native_life = (int)((w >> 28) % 3);
        // The player and the preferences.
        uint32_t c = mix(h + 3);
        *game_current_faction = c % 3 == 0 ? f : c % 3 == 1 && tf >= 0 ? tf : cur;
        *game_preferences = prefs, *game_more_preferences = more;
        for (int i = 0; i < 6; i++)
            if ((mix(c + (uint32_t)i) % 4) == 0) *game_preferences ^= prefs_varied[i];
        if ((c >> 4) % 4 == 0) *game_more_preferences ^= (c >> 6) % 2 ? OV_MPREF_QUICK_ALL : OV_MPREF_QUICK_ORDERS;
        int type = (c >> 8) % 6 ? 3 : (c >> 11) % 2 ? 2 : 1;
        struct hook_stale st;
        for (int i = 0; i < HOOK_STALE_SIZE / 4; i++) st.w[i] = mix(c + 0x100 + (uint32_t)i) % 3 ? 0 : 1;
        if (order_checkable(v, dir, type)) checked_order_veh(&st, v, dir, type);
        // Put back, as far as the step left the unit where it was.
        game_factions[f].se[SE_PLANET] = planet, game_factions[f].se_base[SE_PLANET] = planet_base;
        if (owner >= 0) game_factions[f].diplo_status[owner] ^= d3;
        if (tf >= 0 && tf != f) {
            game_factions[f].diplo_status[tf] ^= d1;
            game_factions[tf].diplo_status[f] ^= d2;
        }
        if (T) T->bits ^= toggled;
        *P = saved_proto;
        if (*game_veh_count == count && V->x == saved.x && V->y == saved.y) {
            V->proto = saved.proto, V->faction = saved.faction, V->moves_spent = saved.moves_spent;
            V->damage = saved.damage, V->state = saved.state, V->order = saved.order, V->flags = saved.flags;
            V->movement_turns = saved.movement_turns, V->iter_count = saved.iter_count;
            memcpy(V->waypoint_x, saved.waypoint_x, sizeof saved.waypoint_x);
            memcpy(V->waypoint_y, saved.waypoint_y, sizeof saved.waypoint_y);
            V->waypoint_count = saved.waypoint_count, V->patrol_point = saved.patrol_point;
        }
    }
    *game_human_factions = human, *game_current_faction = cur, *game_native_life = native;
    *game_preferences = prefs, *game_more_preferences = more;
}

static void sweep_actions(int turn) {
    sweep_terraform_choice(turn);
    sweep_terraform_work(turn);
    sweep_unit_actions(turn);
    sweep_other_actions(turn);
}

// Once a turn, on a protected copy of the game: courses for units of
// every kind, transports' passengers, the way home, patrols, and the go to
// and road to orders.
static void move_sweep(void) {
    if (!*game_map || !game_path->x || !game_path->map) return;
    int sweeping = hooks_sweeping, turn = *game_turn;
    hooks_sweeping = 1;
    struct hook_writes outer = {.protect_only = 1};
    writes_paths(&outer, game_path);
    hooks_writes_begin(&outer);
    sweep_courses(turn);
    sweep_home(turn);
    sweep_patrol(turn);
    sweep_orders(turn);
    sweep_actions(turn);
    sweep_steps(turn);
    hooks_writes_abandon(&outer);
    sweep_found(turn);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook move_hooks[] = {
    HOOK(0x564890, set_course, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),            // sub esp, 28
    HOOK(0x564b90, assemble_passengers, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),   // sub esp, 16
    HOOK(0x4cb310, action_go_to, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x35, 0x60, 0xf6, 0x93, 0x00),  // push esi; mov esi, [net_multiplayer]
    HOOK(0x4cb580, action_road_to, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),        // sub esp, 24
    HOOK(0x4cbaa0, action_home, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),           // sub esp, 44
    HOOK(0x4cd6a0, valid_patrol, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53),                // push ecx; push ebx
    HOOK(0x4cda30, action_patrol, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),   // push ebx; mov ebx, [ebp+12]
    HOOK(0x4c9b00, action_terraform, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),     // sub esp, 52
    HOOK(0x4c9420, terraform_cost, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),       // sub esp, 8
    HOOK(0x565320, can_terraform, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x44),        // sub esp, 68
    HOOK(0x4c96e0, action_build, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x14),         // sub esp, 20
    HOOK(0x4ca7f0, action_staple, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),        // mov eax, [ebp+8]
    HOOK(0x4caa50, action_destroy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),       // sub esp, 28
    HOOK(0x4cd090, action_arty, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),          // mov eax, [ebp+8]
    HOOK(0x4cd2f0, action_destruct, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),      // sub esp, 12
    HOOK(0x4cd4e0, action_oblit, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),         // mov eax, [ebp+8]
    HOOK(0x4ce210, action_tectonic, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),      // sub esp, 52
    HOOK(0x4ce790, action_fungal, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),        // sub esp, 52
    HOOK(0x4cf380, action_gate, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),    // push ebx; mov ebx, [ebp+8]
    HOOK(0x4cf740, action, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),               // mov ecx, [ebp+8]
    HOOK(0x5947c0, order_veh, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
};
struct hook_module move_module = {"move", move_hooks, HOOK_COUNT(move_hooks), move_sweep, NULL, -1};
