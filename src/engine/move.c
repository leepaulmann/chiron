// Units on the move: the orders that send a unit somewhere (go to a square,
// build a road or tube along the way, return home, patrol) and the AI's
// courses for its transports. Reimplemented from terranx.exe 2.0's
// disassembly; names follow Thinker (MIT) and OpenSMACX (Brendan Casey,
// GPL-3.0-or-later).
//
// The step itself is the game's order_veh(), which moves the unit with all
// that may happen on the way (fights, supply pods, monoliths), and is not
// here. Nor is veh_scoot(), which the game calls from it to show a unit
// sliding into the next square: it is all animation (the sound, the
// squares' pictures, a timed loop of drawing) and leaves nothing behind but
// the unit's facing, so the game's own is kept.
#include "move.h"

#include <stdlib.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "path.h"
#include "shadow.h"
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

// Held in the sweep's checks: the step and the start of a former's work.
// A held step returns `hold_result`; with `hold_moves` it also puts the
// unit on the square it stepped to (its coordinates only), as the move
// would, so that both runs see an arrival.
static int holding, hold_result, hold_moves;
static int (*orig_order_veh)(int veh, int dir, int type);
static struct hook_stats stats_order_veh = {"(held order_veh)"};
static void replay_none(const struct hook_ui_call *c) { (void)c; }
static int shadow_order_veh(int veh, int dir, int type) {
    if (!holding || !hooks_protecting()) return orig_order_veh(veh, dir, type);
    stats_order_veh.calls++;
    struct hook_ui_call c = {.name = "order_veh", .replay = replay_none, .arg = {veh, dir, type}};
    hooks_ui_hold(&c);
    if (hold_moves && hold_result && dir >= 0 && dir < 8) {
        struct veh *V = &game_vehs[veh];
        int x = xrange(V->x + game_radius_near_x[dir]), y = V->y + game_radius_near_y[dir];
        if (on_map(x, y)) V->x = (int16_t)x, V->y = (int16_t)y;
    }
    return hold_result;
}
static int (*orig_action_terraform)(int veh, int terraform, int unk);
static struct hook_stats stats_action_terraform = {"(held action_terraform)"};
static int shadow_action_terraform(int veh, int terraform, int unk) {
    if (!holding || !hooks_protecting()) return orig_action_terraform(veh, terraform, unk);
    stats_action_terraform.calls++;
    struct hook_ui_call c = {.name = "action_terraform", .replay = replay_none, .arg = {veh, terraform, unk}};
    hooks_ui_hold(&c);
    return 0;
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
    holding = 1;
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
    holding = 0;
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
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
#define HELD_HOOK(addr, name, ...)                                                                      \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), NULL, (void *)shadow_##name, \
     (void **)&orig_##name, &stats_##name, 1}
static const struct hook move_hooks[] = {
    HOOK(0x564890, set_course, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),            // sub esp, 28
    HOOK(0x564b90, assemble_passengers, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),   // sub esp, 16
    HOOK(0x4cb310, action_go_to, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x35, 0x60, 0xf6, 0x93, 0x00),  // push esi; mov esi, [net_multiplayer]
    HOOK(0x4cb580, action_road_to, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),        // sub esp, 24
    HOOK(0x4cbaa0, action_home, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),           // sub esp, 44
    HOOK(0x4cd6a0, valid_patrol, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53),                // push ecx; push ebx
    HOOK(0x4cda30, action_patrol, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),   // push ebx; mov ebx, [ebp+12]
    HELD_HOOK(0x5947c0, order_veh, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
    HELD_HOOK(0x4c9b00, action_terraform, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),  // sub esp, 52
};
struct hook_module move_module = {"move", move_hooks, HOOK_COUNT(move_hooks), move_sweep, NULL, -1};
