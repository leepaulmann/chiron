// The AI's strategy: enemy_strategy(), the AI's planning at the start of
// each faction's turn. Reimplemented from terranx.exe 2.0's disassembly;
// names follow Thinker (MIT), whose own version was a guide but leaves out
// parts of the original (the disbanding of obsolete units, formers moved
// to other bases) and changes others, so the code here follows the
// original.
#include <string.h>

#include "game.h"
#include "map.h"
#include "shadow.h"
#include "strategy.h"

enum {
    ORDER_SENTRY_BOARD = 1, ORDER_HOLD = 2, ORDER_FARM = 4, ORDER_MOVE_TO = 0x18,
    PLAN_NAVAL_SUPERIORITY = 6, PLAN_TERRAFORM = 9, PLAN_PROBE = 11, PLAN_ARTIFACT = 12,
    PROTO_SCOUT_PATROL = 2, PROTO_UNITY_FOIL = 13, PROTO_GROUP_NO_GOALS = 21,
    FAC_PERIMETER_DEFENSE = 4,
    ABL_DROP_POD = 0x10, ABL_EMPATH = 0x800, ABL_ARTILLERY = 0x8000,
    STACK_ALL = 1, STACK_PLAN = 2, STACK_TRIAD = 3, STACK_CARGO = 8, STACK_STATE = 11, STACK_FACTION = 14,
    STACK_GUARDS = 16,
};
enum {  // veh.flags
    VFLAG_OBJECTIVE = 0x20, VFLAG_LURKER = 0x40, VFLAG_INVISIBLE = 0x400, VFLAG_FORMER_DONE = 0x800,
};
enum {  // veh.state
    VSTATE_ALERT = 0x200, VSTATE_WAITING = 0x20000, VSTATE_TARGET = 0x40000, VSTATE_UNK_1000000 = 0x1000000,
    VSTATE_PATROL = 0x2000000, VSTATE_GARRISON = 0x10000000, VSTATE_TRANSPORT = 0x20000000,
    VSTATE_SCOUTING = 0x40000000, VSTATE_TURN_CLEARED = 0x30000024,
};
enum {  // base.state
    BSTATE_FORMER_LAND = 0x20, BSTATE_FORMER_SEA = 0x80000, BSTATE_GUARD_AWAY = 0x4000000,
    BSTATE_DEFEND = 0x8000000,
};
enum {  // faction_data.flags
    FFLAG_TARGET_COAST = 0x800, FFLAG_SEA_FORMERS = 0x80000, FFLAG_ATTACK_HQ = 0x200000,
    FFLAG_NO_TARGETS = 0x20000000, FFLAG_DEFEND_ALL = 0x40000000,
};
enum {  // faction_data.region_flags
    REGION_EXPAND = 0x1, REGION_COLONY = 0x2, REGION_CONFLICT = 0x4, REGION_KEEP = 0x8, REGION_OWN = 0x10,
    REGION_RIVAL = 0x20, REGION_WAR = 0x40, REGION_COAST = 0x80, REGION_QUIET = 0x100, REGION_FRONTS = 0x200,
    REGION_LOST_BASE = 0x400, REGION_COLONIES = 0x800, REGION_PROBE = 0x1000, REGION_RECON = 0x2000,
    REGION_OUTMATCHED = 0x4000,
};
enum { TREATY_HOSTILE = TREATY_VENDETTA | TREATY_SHALL_BETRAY | TREATY_UNK_800 };

static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int triad_of(int proto) { return game_chassis[game_protos[proto].chassis].triad; }
static inline int plan_of(int proto) { return game_protos[proto].plan; }
static inline int site_of(int x, int y) { return (*game_map)[(x >> 1) + y * *game_map_half_width].owner_site >> 4; }

// Units sent against the target return to their own plans.
static void clear_target(int faction) {
    struct faction_data *me = &game_factions[faction];
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].faction == faction && (game_vehs[v].state & VSTATE_TARGET)) game_vehs[v].state |= VSTATE_WAITING;
    me->target_x = me->target_y = -1;
    me->target_kinds = 0;
    me->target_units = 0;
}

// A rival's base it can see, at war or about to be: goals to attack it, more
// so for an undefended base, one it lost, one in a region where it has
// bases, one without a Perimeter Defense, one building the Ascent, a
// headquarters (above all of a faction cornering the energy market), the
// base it means to take and the game's objectives; and to raise land at the
// square its owner terraforms, when that's cheap.
static void rival_base_goals(int faction, int b) {
    struct faction_data *me = &game_factions[faction];
    struct base *base = &game_bases[b];
    int x = base->x, y = base->y, owner = base->faction;
    if (!(base->visibility & (1 << faction))) return;
    uint32_t st = me->diplo_status[owner];
    if (!(st & TREATY_HOSTILE) || (st & TREATY_PACT)) return;
    int pri = 1;
    if (veh_who(x, y) < 0) {
        pri = 2;
        game_add_goal(faction, GOAL_DEFEND, 2, x, y, -1);
        game_add_goal(faction, GOAL_SCOUT, 2, x, y, -1);
    }
    if (base->faction_former == faction) pri++;
    if (me->region_total_bases[region_at(x, y)]) pri++;
    if (!game_has_facility(base, FAC_PERIMETER_DEFENSE)) pri++;
    if (base->queue[0] == -FAC_ASCENT) pri *= 4;
    if (game_has_facility(base, FAC_HEADQUARTERS)) {
        pri++;
        if (me->flags & FFLAG_ATTACK_HQ) pri *= 2;
        if (game_factions[owner].corner_market_turn > *game_turn) pri *= 8;
    }
    if ((me->attack_target_base == b && (me->diplo_status[owner] & TREATY_VENDETTA)) || game_is_objective(b)) pri *= 4;
    game_add_goal(faction, GOAL_ATTACK, pri, x, y, -1);
    int tx = base->terraform_x, ty = base->terraform_y;
    if (!on_map(tx, ty)) return;
    int t = game_base_find(tx, ty);
    if (t >= 0 && game_bases[t].faction == owner && game_terraform_cost(tx, ty, faction) <= me->energy_credits / 8)
        game_add_goal(faction, GOAL_RAISE_LAND, 2, tx, ty, -1);
}

// Its own base: flags cleared, the AI's plan counted down, the census, a
// goal to defend it when no unit of its own (or recon unit) is there, its
// queue counted, the region's bases and coast, and every fourth turn its
// events forgotten.
static void own_base(int faction, int b) {
    struct faction_data *me = &game_factions[faction];
    struct base *base = &game_bases[b];
    int x = base->x, y = base->y;
    base->state &= ~(uint32_t)(BSTATE_GUARD_AWAY | BSTATE_DEFEND);
    if (base->ai_plan) base->ai_plan--;
    me->base_count++;
    me->pop_total += base->pop_size;
    int stack = game_stack_fix(game_veh_at(x, y));
    if (!game_stack_check(stack, STACK_FACTION, faction, -1, -1) && !game_stack_check(stack, STACK_PLAN, PLAN_RECON, faction, -1))
        game_add_goal(faction, GOAL_DEFEND, 4, x, y, -1);
    const int32_t *queue = base->queue;  // not limited to 10, as in the original
    for (int i = 0; i <= base->queue_size; i++)
        if (queue[i] >= 0) me->units_queue[queue[i]]++;
    int region = region_at(x, y);
    me->region_total_bases[region]++;
    if (is_coast(x, y, 0)) me->region_flags[region] |= REGION_COAST;
    if (((b + *game_turn) & 3) == 0) base->event &= ~0xffu;
}

// A unit of a faction it has a commlink with and no peace (or a treaty it
// means to break): the rival's transports, naval power, air attackers and
// missiles are counted, and a goal set at a former working at sea.
static void enemy_unit(int faction, int v) {
    struct faction_data *me = &game_factions[faction];
    struct veh *u = &game_vehs[v];
    uint32_t st = me->diplo_status[u->faction];
    if (!(st & TREATY_COMMLINK)) return;
    if ((st & (TREATY_TREATY | TREATY_PACT)) && !(st & (TREATY_UNK_800 | TREATY_SHALL_BETRAY | TREATY_WANT_REVENGE))) return;
    const struct unit_proto *p = &game_protos[u->proto];
    const struct rules_chassis *c = &game_chassis[p->chassis];
    if (p->plan == PLAN_NAVAL_TRANSPORT) me->enemy_transports++;
    if (p->plan == PLAN_NAVAL_SUPERIORITY) me->enemy_naval_reactors += p->reactor;
    if (c->triad == TRIAD_AIR) {
        if (p->plan == PLAN_OFFENSIVE && !c->missile && game_weap_val(u->proto, faction) >= me->enemy_best_armor_value)
            me->enemy_air_attackers++;
        if (c->missile && !game_planet_buster(v)) me->enemy_missiles++;
    }
    if (p->plan != PLAN_TERRAFORM || c->triad != TRIAD_SEA) return;
    int x = u->x, y = u->y;
    if (!is_ocean(x, y) || base_who(x, y) >= 0) return;
    if ((u->faction == faction && (u->flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != (VFLAG_LURKER | VFLAG_INVISIBLE)) ||
        (u->visibility & (1 << faction)))
        game_add_goal(faction, GOAL_UNK_6, (me->flags & FFLAG_SEA_FORMERS) ? 4 : 2, x, y, -1);
}

// Whether an obsolete unit the AI couldn't upgrade goes: one turn in four
// after turn 50, outside a base (or with company there), on land not held
// for defense and not in an enemy's zone of control, unless its plan is the
// region's or it's needed there; a combat unit goes only when the faction
// has a better active design for the same plan and triad.
static int disband_obsolete(int faction, int v, int region) {
    struct faction_data *me = &game_factions[faction];
    struct veh *u = &game_vehs[v];
    int x = u->x, y = u->y, turn = *game_turn;
    if (((turn + v) & 3) || turn <= 50 || u->status_icon == 'F' || u->status_icon == '1' || u->status_icon == '2') return 0;
    int plan = plan_of(u->proto), region_plan = me->region_base_plan[region];
    if (plan == PLAN_COMBAT && region_plan <= PLAN_DEFENSIVE) return 0;
    if (plan >= PLAN_COLONY || plan == region_plan) return 0;
    if (plan == PLAN_RECON && region_plan == PLAN_COLONY && u->order != ORDER_HOLD) return 0;
    if (base_who(x, y) >= 0 && game_stack_check(v, STACK_ALL, faction, -1, -1) <= 1) return 0;
    if (is_ocean(x, y) || region_plan == PLAN_DEFENSIVE || zoc_sea(x, y, faction)) return 0;
    if (plan <= PLAN_DEFENSIVE) {
        int better = 0;
        for (int k = 0; k < 64; k++) {
            int q = faction * 64 + k;
            const struct unit_proto *p = &game_protos[q];
            if (!(p->flags & PROTO_FLAG_ACTIVE) || (p->obsolete_factions & (1 << faction))) continue;
            if (p->plan != plan_of(u->proto) || triad_of(q) != triad_of(u->proto)) continue;
            if (plan <= PLAN_COMBAT) {
                int theirs = game_weap_val(q, faction);
                if (theirs > game_weap_val(u->proto, faction)) better = 1;
            } else {
                int ours = game_arm_val(u->proto, faction);
                if (game_arm_val(q, faction) > ours) better = 1;
            }
        }
        if (!better) return 0;
    }
    return 1;
}

// Another base in the region for a former whose base already has one,
// the nearest without one that makes less than 2 minerals; any faction's.
static void move_former(struct veh *u, int region, uint32_t bit) {
    int best = -1, best_dist = 9999;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *base = &game_bases[b];
        if (b == u->home_base || region_at(base->x, base->y) != region || (base->state & bit)) continue;
        if (base->surplus[RESOURCE_MINERAL] >= 2) continue;
        int d = vector_dist(u->x, u->y, base->x, base->y);
        if (d < best_dist) best_dist = d, best = b;
    }
    if (best >= 0) {
        game_bases[best].state |= bit;
        u->home_base = (int16_t)best;
    }
}

struct census {
    int region_formers[REGION_COUNT];
    int waiting, transports, max_cargo;
};

// Its own unit: counted, its lurking counted down, disbanded when obsolete
// or a spare scout, and units stranded on the wrong side of the shore lost
// (a land unit at sea without a transport goes; one with a transport
// sleeps). Spare defenders are marked to garrison, formers and colony pods
// counted and the region's flags set by plan, and the unit's strength goes
// into the faction's and the region's. Units sent against the target are
// counted, or wait when the target is gone or elsewhere.
static void own_unit(int faction, int v, struct census *c) {
    struct faction_data *me = &game_factions[faction];
    struct veh *u = &game_vehs[v];
    int x = u->x, y = u->y, proto = u->proto;
    int region = region_at(x, y), ocean = is_ocean(x, y);
    int plan = plan_of(proto), triad = triad_of(proto);
    int human = is_human(faction);
    me->units_active[proto]++;
    if (u->lurking && !--u->lurking) u->flags &= ~(uint16_t)(VFLAG_LURKER | VFLAG_INVISIBLE);
    if (!human && (game_protos[proto].obsolete_factions & (1 << faction)) && !game_console_upgrade(game_console, v) &&
        disband_obsolete(faction, v, region)) {
        if (!(u->flags & VFLAG_OBJECTIVE)) game_kill(v);
        return;
    }
    if (!is_human(faction) && ((*game_turn + v) & 3) && *game_turn > 25 && proto == PROTO_SCOUT_PATROL &&
        (me->region_base_plan[region] == PLAN_NAVAL_TRANSPORT || u->order == ORDER_HOLD)) {
        if (!(base_who(x, y) >= 0 && game_stack_check(v, STACK_FACTION, faction, -1, -1) <= 1) && !ocean &&
            me->region_base_plan[region] != PLAN_DEFENSIVE && !zoc_sea(x, y, faction) && u->status_icon != '1') {
            game_kill(v);
            return;
        }
    }
    u->state &= ~(uint32_t)VSTATE_TURN_CLEARED;
    if (!is_human(faction) && u->order == ORDER_SENTRY_BOARD && !(triad == TRIAD_LAND && ocean)) u->order = 0;
    if (ocean) {
        if (triad == TRIAD_LAND && base_who(x, y) < 0) {
            if (!game_stack_check(v, STACK_PLAN, PLAN_NAVAL_TRANSPORT, -1, -1)) {
                if (!(u->flags & VFLAG_OBJECTIVE)) game_veh_kill(v);
                return;
            }
            if (u->order != ORDER_SENTRY_BOARD) game_veh_sleep(v);
        }
    } else if (triad == TRIAD_SEA && base_who(x, y) < 0) {
        if (!(u->flags & VFLAG_OBJECTIVE)) game_veh_kill(v);
        return;
    }
    if (plan <= PLAN_COMBAT && !(u->state & VSTATE_TARGET)) {
        int gb = game_base_at(u->x, u->y);
        if (gb < 0 && u->status_icon == 'F') gb = u->home_base;
        if (gb >= 0) {
            int garrison = game_garrison_check(gb), needed = game_garrison_check(gb);
            if (needed > 5) needed--;
            if (needed > 2) needed--;
            int held = game_stack_check(game_veh_at(game_bases[gb].x, game_bases[gb].y), STACK_STATE, VSTATE_GARRISON,
                                        VSTATE_GARRISON, faction);
            if (garrison > needed + held) u->state |= VSTATE_GARRISON;
        }
    }
    if (u->status_icon == 'F' && plan == PLAN_DEFENSIVE) {
        int home = u->home_base;
        if (home >= 0 && (game_bases[home].x != u->x || game_bases[home].y != u->y)) game_bases[home].state |= BSTATE_GUARD_AWAY;
    } else if (plan == PLAN_TERRAFORM) {
        u->flags &= ~(uint16_t)VFLAG_FORMER_DONE;
        c->region_formers[region]++;
        int home = u->home_base;
        if (home >= 0) {
            uint32_t bit = triad == TRIAD_SEA ? BSTATE_FORMER_SEA : BSTATE_FORMER_LAND;
            if ((game_bases[home].state & bit) && !is_human(faction)) move_former(u, region, bit);
            else game_bases[home].state |= bit;
        }
    } else if (plan == PLAN_COLONY) {
        me->region_flags[region] |= (me->region_flags[region] & REGION_COLONY) ? REGION_COLONIES : REGION_COLONY;
    } else if (plan == PLAN_PROBE) {
        me->region_flags[region] |= REGION_PROBE;
    } else if (plan == PLAN_RECON && base_who(x, y) < 0) {
        me->region_flags[region] |= REGION_RECON;
    }
    if (plan <= PLAN_NAVAL_TRANSPORT) {
        me->total_combat_units++;
        if (triad == TRIAD_LAND && !ocean) me->region_total_combat_units[region]++;
    }
    if (triad == TRIAD_SEA) me->naval++;
    else if (triad == TRIAD_AIR && plan == PLAN_OFFENSIVE) me->air_offense++;
    else if (triad == TRIAD_AIR && plan == PLAN_AIR_SUPERIORITY) me->air_superiority++;
    if (plan == PLAN_NAVAL_TRANSPORT) {
        c->transports++;
        if (c->max_cargo <= game_veh_cargo(v)) c->max_cargo = game_veh_cargo(v);
    }
    if (game_planet_buster(v)) {
        me->planet_busters++;
        return;
    }
    int wpn = game_weap_strat(game_protos[u->proto].weapon, faction);
    int arm = game_arm_strat(game_protos[u->proto].armor, faction);
    int morale = game_morale_veh(v, 0, 0) + 6;
    me->mil_strength += (arm + wpn) * morale / 8;
    if (wpn > arm || u->order != ORDER_HOLD) {
        int attack = morale * wpn / 8;
        me->mil_attack += attack;
        me->region_force_rating[region] += (uint16_t)attack;
    }
    uint32_t sent = u->state & (VSTATE_TARGET | VSTATE_WAITING);
    if (sent == VSTATE_TARGET) {
        if (on_map(me->target_x, me->target_y) && region_at(me->target_x, me->target_y) == region) {
            me->target_units++;
            if (plan == PLAN_PROBE) me->target_kinds |= 2;
            else if (arm > wpn) me->target_kinds |= 1;
        } else {
            u->state |= VSTATE_WAITING;
            c->waiting++;
        }
    } else if (sent == (VSTATE_TARGET | VSTATE_WAITING)) {
        c->waiting++;
    }
}

// Whether the attack on its target base (the base it gathers units at) is
// called off: the base gone or no longer its own; enough defenders
// gathered, or the region held for defense or fought on several fronts;
// at sea, enough to fill the transports, or as many as are aboard; a
// colony region, or an inland base for a seaborne attack.
static int drop_target(int faction, int human, int *target_region) {
    struct faction_data *me = &game_factions[faction];
    int skip = human;
    if (me->target_x < 0) return skip;
    int tb = game_base_at(me->target_x, me->target_y);
    *target_region = region_at(me->target_x, me->target_y);
    if (tb < 0 || game_bases[tb].faction != faction) return 1;
    int tr = *target_region;
    int stack = game_stack_fix(game_veh_at(game_bases[tb].x, game_bases[tb].y));
    if (stack >= 0) {
        int defenders = game_stack_check(stack, STACK_STATE, VSTATE_TARGET | VSTATE_WAITING, VSTATE_TARGET, -1);
        int needed = clamp(2 * me->region_total_bases[tr] / 3, 3, 8);
        if (defenders - game_bit_count(me->target_kinds) >= needed) skip = 1;
        int plan = me->region_base_plan[tr];
        if (plan == PLAN_DEFENSIVE || (me->region_flags[tr] & (REGION_OUTMATCHED | REGION_FRONTS))) skip = 1;
        if (plan > PLAN_DEFENSIVE && game_stack_check(stack, STACK_PLAN, PLAN_NAVAL_TRANSPORT, -1, -1)) {
            if (defenders >= 2 * (me->naval / 0x400000)) skip = 1;
            if (game_stack_check(stack, STACK_PLAN, PLAN_NAVAL_TRANSPORT, -1, -1) > 1 &&
                defenders >= game_stack_check(stack, STACK_CARGO, -1, -1, -1) + game_stack_check(stack, STACK_TRIAD, TRIAD_LAND, -1, -1))
                return 1;
        }
    }
    if (skip) return skip;
    int plan = me->region_base_plan[tr];
    if (plan <= PLAN_DEFENSIVE) me->flags &= ~(uint32_t)FFLAG_TARGET_COAST;
    else if (plan == PLAN_COLONY || !is_coast(game_bases[tb].x, game_bases[tb].y, 0)) return 1;
    else me->flags |= FFLAG_TARGET_COAST;
    return 0;
}

// A goal to raise land where the region could reach another across the
// water: on the abstract map's cells in the region, each neighbouring
// cell of another region scores, more when the shore can be walked to from
// the cell's centre square in its direction ending at an empty square, and
// more again when the other region is big; the best end square, if cheap
// enough to terraform.
static void raise_land_goal(int faction, int region) {
    struct faction_data *me = &game_factions[faction];
    int best_score = 0, best_x = 0, best_y = 0, cand_x = 0, cand_y = 0;
    int flat = *game_map_flat & 1;
    for (int ay = 0; ay < *game_abstract_height; ay++)
        for (int ax = ay & 1; ax < *game_abstract_width; ax += 2) {
            if (abstract_at(ax, ay) != region) continue;
            int score = 0, best_dir = -1;
            for (int dir = 0; dir < 8; dir++) {
                int aw = *game_abstract_width, w = *game_map_width;
                int nx = ax + game_radius_near_x[dir], ny = ay + game_radius_near_y[dir];
                if (!flat) nx = nx < 0 ? nx + aw : nx >= aw ? nx - aw : nx;
                if (nx < 0 || nx >= aw || ny < 0 || ny >= *game_abstract_height) continue;
                int neighbor = abstract_at(nx, ny);
                if (neighbor == region) {
                    score--;
                    continue;
                }
                score += 2;
                int found = 0, tx = 0, ty = 0;
                for (int j = 0; j < 8 && !found; j++) {
                    tx = ax * 5 + game_radius_near_x[j], ty = ay * 5 + game_radius_near_y[j];
                    if (!flat) tx = tx < 0 ? tx + w : tx >= w ? tx - w : tx;
                    if (!on_map(tx, ty) || is_ocean(tx, ty)) continue;
                    for (int steps = 1;;) {
                        int wx = tx + game_radius_near_x[dir], wy = ty + game_radius_near_y[dir];
                        if (!flat) wx = wx < 0 ? wx + w : wx >= w ? wx - w : wx;
                        if (!on_map(wx, wy) || is_ocean(wx, wy)) {
                            found = 1;
                            break;
                        }
                        tx = wx, ty = wy;
                        if (++steps >= 100) break;
                    }
                }
                if (!found || base_who(tx, ty) >= 0) continue;
                best_dir = dir;
                score += 4;
                cand_x = tx, cand_y = ty;
                if (neighbor && game_continents[neighbor].tile_count >= 40) score += 2;
            }
            if (score > best_score && best_dir >= 0) best_score = score, best_x = cand_x, best_y = cand_y;
        }
    if (best_score > 0 && game_terraform_cost(best_x, best_y, faction) <= me->energy_credits / 8)
        game_add_goal(faction, GOAL_RAISE_LAND, 2, best_x, best_y, -1);
}

struct region_plan {
    int skip, target_region, best_value, best_region;
};

// A region's plan from the factions present: whether rivals can be reached
// (estimated on big continents in most turns, otherwise by contiguity),
// those at war or about to be counted as threats or as weaker; whether to
// expand into it; then offense, defense or colonization. A region fought
// on by two or more ends a target there. The best region to attack from is
// noted, and the AI raises land towards other regions and rethinks its
// bases' builds there.
static void plan_region(int faction, int r, struct region_plan *rp) {
    struct faction_data *me = &game_factions[faction];
    me->region_offensive[r] = 0;
    if (((r & 0x3f) == 0x3f || (r & 0x3f) == 0) && !me->region_total_bases[r]) {
        me->region_base_plan[r] = PLAN_COLONY;
        return;
    }
    int old_plan = me->region_base_plan[r];
    int plan_flags = 0, threats = 0, weak = 0, present = 0, contacts = 0, land_contacts = 0, conflict = 0;
    int my_units = me->region_total_combat_units[r], my_force = me->region_force_rating[r];
    int my_bases = me->region_total_bases[r];
    for (int e = 1; e < FACTION_COUNT; e++) {
        const struct faction_data *en = &game_factions[e];
        uint32_t st = me->diplo_status[e];
        int force = en->region_force_rating[r], units = en->region_total_combat_units[r], bases = en->region_total_bases[r];
        if (st & TREATY_PACT) units *= 2, bases *= 2;
        if (bases) {
            present++;
            me->region_flags[r] |= e != faction ? REGION_RIVAL : REGION_OWN;
            if (st & TREATY_VENDETTA) me->region_flags[r] |= REGION_WAR;
        } else if (!units || my_force > 5 * force) {
            continue;
        }
        if (e == faction || (st & TREATY_PACT) || !(st & TREATY_COMMLINK)) continue;
        int any = 0, land = 0, estimated = 0;
        if (*game_turn > 1) {
            int tiles = game_continents[r].tile_count;
            if (tiles >= 500 || en->region_total_bases[r] * my_bases >= 50) {
                int period = tiles < 1000 ? (*game_turn + faction) & 7 : (*game_turn + 2 * faction) & 0xf;
                if (period) {
                    estimated = 1;
                    any = land = !(bases && me->region_base_plan[r] > PLAN_DEFENSIVE);
                }
            }
        }
        if (!estimated) {
            if (!bases || game_contiguous(faction, e, r, 0)) any = land = 1;
            else any = game_contiguous(faction, e, r, 1) != 0;
        }
        if (st & TREATY_HOSTILE) {
            if (any) contacts++;
            if (land && ++land_contacts > 1) me->region_flags[r] |= REGION_FRONTS;
            if ((my_force <= units || my_force <= force) && my_units <= force && my_bases && any) threats++;
            else weak++;
            if (force >= 3 * my_force / 2 && my_bases > bases && any) me->region_flags[r] |= REGION_OUTMATCHED;
            if (is_human(e) && (land || any))
                for (int b = 0; b < *game_base_count; b++) {
                    const struct base *base = &game_bases[b];
                    if (base->faction == e && base->faction_former == faction && region_at(base->x, base->y) == r) {
                        me->region_flags[r] |= REGION_LOST_BASE;
                        break;
                    }
                }
        } else if (my_bases && (st & TREATY_WANT_REVENGE) && force > my_units) {
            plan_flags |= ((my_force > units && bases && force < 2 * my_force) || !any) ? 2 : 1;
        }
        if (!(me->diplo_status[e] & (TREATY_PACT | TREATY_TREATY)) ||
            ((game_wants_to_attack(faction, e, 0) || game_wants_to_attack(e, faction, 0)) && any))
            conflict = 1;
    }
    int best_site = me->region_best_site[r];
    int growth = game_great_satan(game_faction_rankings[7], 0) + (best_site < 4) - ((*game_state >> 7) & 2) +
                 (me->base_count > 4) + present + (*game_map_area > 3200) + (*game_turn > 75) + 3;
    int expand = 0;
    if (my_bases) {
        int good = me->region_good_tiles[r], spare = good - me->region_good_base_tiles[r];
        int flags = (best_site < 2 && me->region_total_bases[r]) | (me->region_flags[r] & REGION_KEEP) | (my_bases >= 20) |
                    (spare < 4);
        expand = (2 * my_bases * (me->ai_growth - me->ai_wealth + growth) > game_factions[0].region_good_tiles[r] + good &&
                  spare / ((my_bases + 1) / 2) + best_site < 2 * growth) |
                 flags;
    }
    if (conflict) me->region_flags[r] |= REGION_CONFLICT;
    if (expand) me->region_flags[r] |= REGION_EXPAND;
    me->region_base_plan[r] = expand ? PLAN_NAVAL_TRANSPORT : PLAN_COLONY;
    if (!contacts && me->region_total_bases[r]) {
        me->region_flags[r] |= REGION_QUIET;
    } else {
        if (weak) plan_flags = 0, me->region_base_plan[r] = PLAN_OFFENSIVE;
        else if (plan_flags & 2) me->region_base_plan[r] = PLAN_OFFENSIVE;
        if (threats || (plan_flags & 1)) me->region_base_plan[r] = PLAN_DEFENSIVE;
    }
    if (r >= LAND_REGION_COUNT) {
        me->region_base_plan[r] = PLAN_NAVAL_TRANSPORT;
        return;
    }
    if (!rp->skip && r == rp->target_region && land_contacts >= 2) {
        clear_target(faction);
        rp->target_region = -1;
    }
    if (me->target_x < 0 && !rp->skip && !is_human(faction)) {
        int plan = me->region_base_plan[r];
        if (plan < PLAN_DEFENSIVE && land_contacts < 2 && !(me->region_flags[r] & REGION_OUTMATCHED)) {
            int value = me->region_total_bases[r] + 1000;
            if (value > rp->best_value) {
                rp->best_value = value, rp->best_region = r;
                int ab = me->attack_target_base;
                if (ab >= 0 && (me->diplo_status[game_bases[ab].faction] & TREATY_VENDETTA) &&
                    region_at(game_bases[ab].x, game_bases[ab].y) == r)
                    rp->best_value = 0;
            }
        } else if (plan == PLAN_NAVAL_TRANSPORT && me->region_total_bases[r] > rp->best_value) {
            rp->best_value = me->region_total_bases[r], rp->best_region = r;
        }
    }
    if (!is_human(faction) || (faction == *game_current_faction && (*game_preferences & 0x20000))) {
        int plan = me->region_base_plan[r];
        if (me->region_total_bases[r] &&
            (plan == PLAN_NAVAL_TRANSPORT || (plan == PLAN_COLONY && game_continents[r].tile_count < 150)))
            raise_land_goal(faction, r);
    }
    if (!is_human(faction)) game_bases_reset(r, faction, me->region_base_plan[r] == old_plan);
}

// A new target: the rival with the most bases in the best region (any at
// war first, and one it means to attack only when few units are waiting),
// and its own base there nearest that rival's (or the base it means to
// take), on the coast when the rival is only reachable by sea. Up to 8
// land units in the region whose way to the rival passes it gather there.
static void choose_target(int faction, int region, int waiting) {
    struct faction_data *me = &game_factions[faction];
    int best = -1, rival = -1;
    for (int e = 1; e < FACTION_COUNT; e++) {
        if (e == faction) continue;
        uint32_t st = me->diplo_status[e];
        int bases = game_factions[e].region_total_bases[region];
        if (!(st & (TREATY_HOSTILE | 0x4))) continue;
        if (!(st & (TREATY_UNK_800 | TREATY_VENDETTA)) && (waiting > 3 || !bases)) continue;
        int value = (int)((st & TREATY_VENDETTA) << 7) + bases;
        if (value > best) best = value, rival = e;
    }
    if (rival <= 0) return;
    int coast_only = best == 0x800, best_dist = 9999, best_base = -1;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *base = &game_bases[b];
        if (base->faction != faction || is_ocean(base->x, base->y) || region_at(base->x, base->y) != region) continue;
        if (coast_only && !is_coast(base->x, base->y, 0)) continue;
        int dist, ab = me->attack_target_base;
        if (ab >= 0 && (me->diplo_status[game_bases[ab].faction] & (TREATY_UNK_800 | TREATY_VENDETTA)) &&
            region_at(game_bases[ab].x, game_bases[ab].y) == region) {
            dist = *game_base_find_dist = vector_dist(base->x, base->y, game_bases[ab].x, game_bases[ab].y);
        } else if (game_base_find3(base->x, base->y, rival, region, -1, -1) >= 0) {
            dist = *game_base_find_dist;
        } else {
            game_base_find2(base->x, base->y, rival);
            dist = *game_base_find_dist *= 2;
        }
        if (dist < best_dist) best_dist = dist, best_base = b;
    }
    if (best_base < 0) return;
    me->target_x = game_bases[best_base].x, me->target_y = game_bases[best_base].y;
    if (coast_only) me->flags |= FFLAG_TARGET_COAST;
    else me->flags &= ~(uint32_t)FFLAG_TARGET_COAST;
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *u = &game_vehs[v];
        const struct unit_proto *p = &game_protos[u->proto];
        if (u->faction != faction || (u->state & (VSTATE_TARGET | VSTATE_WAITING))) continue;
        if (p->plan > PLAN_DEFENSIVE || game_chassis[p->chassis].triad != TRIAD_LAND || game_has_abil(u->proto, ABL_DROP_POD))
            continue;
        if (region_at(u->x, u->y) != region || (u->order != 0 && u->order != ORDER_MOVE_TO)) continue;
        game_base_find2(u->x, u->y, rival);
        if (*game_base_find_dist <= vector_dist(u->x, u->y, me->target_x, me->target_y)) continue;
        int wpn = game_weap_strat(p->weapon, faction), arm = game_arm_strat(p->armor, faction);
        if (wpn < arm && ((me->target_kinds & 1) || coast_only)) continue;
        u->state = (u->state & ~(uint32_t)VSTATE_WAITING) | VSTATE_TARGET;
        me->target_units++;
        if (wpn < arm) me->target_kinds |= 1;
        if (me->target_units >= 8) break;
        if (coast_only && me->target_units >= (int32_t)((uint32_t)me->naval << 1) / 0x400000) break;
    }
}

// The cheapest of its goals for a unit, cost being distance over priority:
// goals of its plan (or scouting for early units), in its region and on
// its terrain. Monoliths, colony sites already claimed, empaths, goals
// others are already given, closer formers, territory of peaceful
// factions and artillery's targets weigh in; a colony site counts by its
// value.
static int best_goal(int faction, int v, int plan, int reassign, int region, const int32_t *assigned_count) {
    struct faction_data *me = &game_factions[faction];
    struct veh *u = &game_vehs[v];
    int x = u->x, y = u->y;
    int best_cost = 99999, best = -1;
    for (int g = 0; g < GOAL_COUNT; g++) {
        struct goal *goal = &me->goals[g];
        if (goal->priority <= 0 || goal->type < 0) continue;
        if ((goal->type & 0xf) != plan && (reassign || goal->type != GOAL_SCOUT || plan > PLAN_RECON || u->order == ORDER_HOLD))
            continue;
        if (!on_map(goal->x, goal->y)) {
            goal->x = goal->y = -1;
            continue;
        }
        int gx = goal->x, gy = goal->y;
        if (region_at(gx, gy) != region) continue;
        const struct unit_proto *p = &game_protos[u->proto];
        int triad = game_chassis[p->chassis].triad;
        if (triad == TRIAD_AIR || is_ocean(gx, gy) != (triad == TRIAD_SEA) || p->group == PROTO_GROUP_NO_GOALS) continue;
        if (plan == PLAN_OFFENSIVE) {
            if ((bit_at(gx, gy) & MAP_MONOLITH) && (!game_want_monolith(v) || reassign || assigned_count[g])) continue;
        } else if (plan == PLAN_COLONY) {
            if (base_territory(faction, gx, gy) >= 0) {
                game_del_site(faction, GOAL_COLONIZE, gx, gy, 0);
                continue;
            }
        } else if (plan == PLAN_RECON && game_has_abil(u->proto, ABL_EMPATH)) {
            continue;
        }
        int gb = game_base_at(gx, gy);
        int cost = vector_dist(x, y, gx, gy) << 8;
        int assigned =
            (reassign || (gb >= 0 && game_bases[gb].faction == faction && game_bases[gb].ai_plan)) ? 0 : assigned_count[g];
        int prio = goal->priority - assigned;
        if (prio <= 0) continue;
        if (reassign && base_who(gx, gy) < 0) continue;
        if (goal->type == GOAL_SCOUT && plan != PLAN_RECON) {
            cost *= 2;
            if ((u->state & VSTATE_GARRISON) || base_who(gx, gy) < 0) continue;
        }
        if (assigned && (goal->type == GOAL_SCOUT || goal->type == GOAL_COLONIZE)) continue;
        if (!(cost <= 6 / (assigned + 1) * game_chassis[p->chassis].speed * prio - assigned + 4 ||
              me->region_base_plan[region] == plan || goal->type == GOAL_SCOUT || (!assigned && goal->type == GOAL_TERRAFORM)))
            continue;
        if (goal->type == GOAL_COLONIZE) {
            int site;
            if (bit_at(gx, gy) & MAP_BASE_RADIUS) {
                site = 1;
            } else {
                site = site_of(gx, gy);
                if (!site) {
                    site = game_world_site(gx, gy, 0);
                    site_set(gx, gy, site);
                }
                site = clamp(site, 1, 15);
            }
            prio *= site;
        }
        if (u->state & VSTATE_GARRISON) {
            int s = game_veh_at(gx, gy);
            if (s >= 0 && game_stack_check(s, STACK_STATE, VSTATE_GARRISON, VSTATE_GARRISON, faction) > (cost == 0)) continue;
        }
        if (goal->type == GOAL_TERRAFORM) {
            int gbase = goal->base;
            int contested = gbase >= 0 && (game_bases[gbase].state & BSTATE_FORMER_LAND);
            if (contested && u->home_base != gbase) cost *= 3;
            int closer = 0;
            for (int i = 0; i < *game_veh_count && !closer; i++) {
                const struct veh *o = &game_vehs[i];
                if (o->faction != faction || plan_of(o->proto) != PLAN_TERRAFORM || i == v) continue;
                int other = vector_dist(o->x, o->y, gx, gy) << 8;
                if (contested && o->home_base != gbase) other *= 3;
                if (other < cost) closer = 1;
            }
            if (closer) continue;
            if (assigned_count[g] && (gbase < 0 || u->home_base != gbase)) continue;
        }
        int owner = whose_territory(faction, gx, gy, NULL, 0), total = cost;
        if (owner >= 0 && owner != faction) {
            uint32_t st = me->diplo_status[owner];
            if (!(st & TREATY_HOSTILE)) {
                if (st & 0x100000) continue;
                if (!(st & TREATY_PACT)) {
                    if (st & TREATY_TREATY) continue;
                    total = 2 * cost;
                }
            }
        }
        if (goal->type == GOAL_ARTILLERY) {
            if (!game_has_abil(u->proto, ABL_ARTILLERY) || triad_of(u->proto) != TRIAD_LAND) continue;
            total /= 2;
        }
        if (total / prio < best_cost) best_cost = total / prio, best = g;
    }
    return best;
}

// A unit's orders for the turn: sent to the target, or to its best goal.
// Units given a goal this turn are passed over in the second pass unless
// their plan is offense in an attacking region. Units busy terraforming
// and humans' units not on automatic are left alone; defenders are held
// back in each land region, up to its bases over a divisor that grows with
// colonizing and shrinks with defense; transports wait to fill, fortified
// defenders and units holding a base that needs them stay.
static void assign_unit(int faction, int v, int pass, int32_t *assigned_count) {
    struct faction_data *me = &game_factions[faction];
    struct veh *u = &game_vehs[v];
    int x = u->x, y = u->y, region = region_at(x, y);
    int plan = plan_of(u->proto);
    if (pass && u->status_icon == '1') {
        if (plan == PLAN_TERRAFORM) return;
        if (me->region_base_plan[region] <= PLAN_DEFENSIVE && plan <= PLAN_DEFENSIVE) return;
    }
    u->state &= ~(uint32_t)VSTATE_SCOUTING;
    if (u->order >= ORDER_FARM && u->order < ORDER_MOVE_TO) return;
    if (is_human(faction)) {
        uint32_t s = u->state;
        if (!(s & VSTATE_ALERT)) return;
        if (plan_of(u->proto) == PLAN_TERRAFORM && u->order_auto_type) return;
        if ((s & (VSTATE_UNK_1000000 | VSTATE_ALERT)) == (VSTATE_UNK_1000000 | VSTATE_ALERT)) return;
        if ((s & (VSTATE_PATROL | VSTATE_ALERT)) == (VSTATE_PATROL | VSTATE_ALERT) && u->patrol_point < u->waypoint_count) return;
    }
    if (u->state & VSTATE_GARRISON) {
        me->region_offensive[region]++;
        plan = PLAN_DEFENSIVE;
    } else if (region < LAND_REGION_COUNT) {
        int divisor = 2 - me->ai_fight;
        int region_plan = me->region_base_plan[region];
        if (region_plan == PLAN_COLONY) divisor += 2;
        else if (region_plan == PLAN_DEFENSIVE) divisor = 1;
        if (me->flags & FFLAG_DEFEND_ALL) divisor = 1;
        int most = (me->region_total_bases[region] + divisor - 1) / divisor;
        if (most && plan <= PLAN_COMBAT &&
            (region_plan > PLAN_DEFENSIVE ||
             (base_who(x, y) == faction && (region_plan == PLAN_DEFENSIVE || (me->region_flags[region] & REGION_OUTMATCHED)) &&
              game_stack_check(v, STACK_STATE, VSTATE_GARRISON, VSTATE_GARRISON, -1) < 1)) &&
            !(u->state & (VSTATE_TARGET | VSTATE_WAITING)) && me->region_offensive[region] < most) {
            me->region_offensive[region]++;
            u->state |= VSTATE_GARRISON;
            plan = PLAN_DEFENSIVE;
        }
    }
    // A unit on the wrong side of the shore outside a base is left; the
    // original then looks around a base for a square of the right kind, but
    // compares the base's square with itself, so finds none.
    int triad = triad_of(u->proto);
    if (triad != TRIAD_AIR && is_ocean(x, y) != (triad == TRIAD_SEA) && base_who(x, y) < 0) return;
    if (plan == PLAN_COMBAT) {
        int region_plan = me->region_base_plan[region];
        plan = region_plan == PLAN_DEFENSIVE || region_plan == PLAN_COLONY ? PLAN_DEFENSIVE : PLAN_OFFENSIVE;
    }
    if ((u->state & (VSTATE_TARGET | VSTATE_WAITING)) == VSTATE_TARGET && me->target_x >= 0) {
        u->order = ORDER_MOVE_TO;
        u->status_icon = '0';
        u->waypoint_x[0] = (int16_t)me->target_x;
        u->waypoint_y0 = (int16_t)me->target_y;
        if (plan_of(u->proto) == PLAN_COLONY) u->movement_turns = 0;
        return;
    }
    if (plan == PLAN_NAVAL_TRANSPORT && game_stack_check(v, STACK_CARGO, -1, -1, -1) < game_veh_cargo(v)) return;
    if (u->status_icon == 'F' && plan == PLAN_DEFENSIVE) return;
    int reassign = 0;
    if ((u->state & (VSTATE_TARGET | VSTATE_WAITING)) == (VSTATE_TARGET | VSTATE_WAITING)) plan = PLAN_OFFENSIVE, reassign = 1;
    if (u->order == ORDER_HOLD && !reassign && base_who(x, y) >= 0) {
        int artifacts = game_stack_check(v, STACK_PLAN, PLAN_ARTIFACT, -1, -1);
        if (game_stack_check(v, STACK_ALL, -1, -1, -1) - artifacts <= 1) return;
        if (plan == PLAN_DEFENSIVE || plan == PLAN_RECON || plan == PLAN_COMBAT) {
            int b = game_base_at(x, y);
            if (game_stack_check(v, STACK_GUARDS, -1, -1, -1) < game_garrison_check(b)) return;
        }
    }
    if (plan == PLAN_TERRAFORM && (u->flags & VFLAG_FORMER_DONE)) return;
    int g = best_goal(faction, v, plan, reassign, region, assigned_count);
    if (g < 0) return;
    const struct goal *goal = &me->goals[g];
    if (u->status_icon < '3' || u->status_icon > '5') assigned_count[g]++;
    u->order = ORDER_MOVE_TO;
    u->status_icon = '1';
    u->waypoint_x[0] = (int16_t)goal->x;
    u->waypoint_y0 = (int16_t)goal->y;
    if (plan_of(u->proto) == PLAN_COLONY) u->movement_turns = 0;
    if (goal->type == GOAL_SCOUT) u->state |= VSTATE_SCOUTING;
    if (goal->type == GOAL_DEFEND) {
        int b = game_base_at(goal->x, goal->y);
        if (b >= 0 && game_bases[b].faction == faction) {
            game_bases[b].state |= BSTATE_DEFEND;
            if (!is_human(faction)) game_base_reset(b, 0);
        }
    }
}

// The AI's planning at the start of a faction's turn: a census of its
// bases and units (and its rivals'), the goals they suggest, regional
// plans, an attack target, and every unit's orders.
void enemy_strategy(int faction) {
    struct faction_data *me = &game_factions[faction];
    struct census c = {.max_cargo = 2};
    int32_t assigned_count[GOAL_COUNT];
    me->total_combat_units = me->base_count = me->mil_strength = me->mil_attack = 0;
    me->pop_total = me->naval = me->planet_busters = me->target_units = 0;
    me->target_kinds = 0;
    for (int r = 0; r < REGION_COUNT; r++) {
        me->region_total_combat_units[r] = 0;
        me->region_total_bases[r] = 0;
        me->region_force_rating[r] = 0;
        me->region_flags[r] &= ((faction + *game_turn + r) & 0xf) ? REGION_KEEP : 0;
    }
    for (int i = 0; i < PROTO_COUNT; i++) me->units_active[i] = me->units_queue[i] = 0;
    for (int b = 0; b < *game_base_count; b++) {
        if (game_bases[b].faction == faction) own_base(faction, b);
        else rival_base_goals(faction, b);
    }
    if (!is_human(faction))
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == faction && game_bases[b].queue[0] == -FAC_STOCKPILE_ENERGY) game_base_reset(b, 0);
    game_enemy_capabilities(faction);
    me->enemy_missiles = me->enemy_air_attackers = me->enemy_naval_reactors = me->enemy_transports = 0;
    me->air_superiority = me->air_offense = 0;
    for (int v = *game_veh_count - 1; v >= 0; v--) {
        if (v >= *game_veh_count) continue;
        if (game_vehs[v].faction != faction) enemy_unit(faction, v);
        else own_unit(faction, v, &c);
    }
    me->naval += (int32_t)(((uint32_t)(c.max_cargo << 11) + (uint32_t)c.transports) << 11);
    int rival_cargo = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint32_t st = me->diplo_status[i];
        if (i == faction || !(st & TREATY_COMMLINK) || (st & TREATY_PACT)) continue;
        int cargo = game_factions[i].naval / 0x400000;
        if (rival_cargo <= cargo) rival_cargo = cargo;
    }
    me->enemy_transports += rival_cargo << 11;
    if (c.transports > 1)
        for (int v = 0; v < *game_veh_count; v++) {
            struct veh *u = &game_vehs[v];
            if (u->faction == faction && triad_of(u->proto) == TRIAD_SEA && plan_of(u->proto) == PLAN_NAVAL_TRANSPORT &&
                (u->proto == PROTO_UNITY_FOIL || me->units_active[u->proto] >= 2)) {
                u->state |= VSTATE_TRANSPORT;
                break;
            }
        }
    for (int v = 0, n = *game_veh_count; v < n; v++) {
        struct veh *u = &game_vehs[v];
        if (u->faction != faction || plan_of(u->proto) != PLAN_TERRAFORM) continue;
        int r = region_at(u->x, u->y);
        if (r >= LAND_REGION_COUNT || c.region_formers[r] < 3 || me->region_total_bases[r] < 3) continue;
        u->flags |= VFLAG_FORMER_DONE;
        if (u->home_base >= 0) game_bases[u->home_base].state &= ~(uint32_t)BSTATE_FORMER_LAND;
        c.region_formers[r] = 0;
    }
    struct region_plan rp = {.target_region = -1, .best_region = -1};
    rp.skip = drop_target(faction, is_human(faction), &rp.target_region);
    if (rp.skip) clear_target(faction);
    for (int r = 1; r < REGION_COUNT; r++) plan_region(faction, r, &rp);
    if (rp.best_region > 0 && rp.best_region < 63 && !rp.skip && !is_human(faction) && !(me->flags & FFLAG_NO_TARGETS))
        choose_target(faction, rp.best_region, c.waiting);
    if (!*game_turn || !faction) return;
    memset(assigned_count, 0, sizeof assigned_count);
    for (int v = *game_veh_count - 1; v >= 0; v--)
        if (game_vehs[v].faction == faction && game_vehs[v].status_icon == '1') game_vehs[v].status_icon = '2';
    for (int pass = 0; pass < 2; pass++)
        for (int v = *game_veh_count - 1; v >= 0; v--) {
            if (v >= *game_veh_count) continue;
            const struct veh *u = &game_vehs[v];
            if (u->faction != faction || (!pass && u->status_icon != '2')) continue;
            assign_unit(faction, v, pass, assigned_count);
        }
    game_wipe_goals(faction);
}

extern struct hook_module strategy_module;
#define STRATEGY (&strategy_module)

// The AI's strategy writes the game's state and, through bases_reset(),
// may search paths.
static void writes_path(struct hook_writes *w) {
    struct { int32_t *map; int16_t *x, *y; } *p = game_path;
    size_t area = (size_t)*game_map_area;
    if (p->map) hooks_writes_add(w, p->map, area * 4);
    if (p->x) hooks_writes_add(w, p->x, area * 2);
    if (p->y) hooks_writes_add(w, p->y, area * 2);
}
SHADOW_WRITER(STRATEGY, void, enemy_strategy, (int a), (a), (hooks_writes_game(&w_), writes_path(&w_)), "%d", a)

// Each faction's strategy, and on alternate turns one of them as the
// player's would run.
static void strategy_sweep(void) {
    if (!*game_map) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int f = 1; f < FACTION_COUNT; f++)
        if (*game_factions_alive >> f & 1) check_enemy_strategy(f);
    int f = *game_turn % (FACTION_COUNT - 1) + 1;
    if ((*game_factions_alive >> f & 1) && !(*game_human_factions >> f & 1)) {
        uint8_t humans = *game_human_factions;
        *game_human_factions = (uint8_t)(humans | 1 << f);
        check_enemy_strategy(f);
        *game_human_factions = humans;
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook strategy_hooks[] = {
    HOOK(0x561080, enemy_strategy, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x88, 0x02, 0x00, 0x00),  // sub esp, 0x288
};
struct hook_module strategy_module = {"strategy", strategy_hooks, HOOK_COUNT(strategy_hooks), strategy_sweep, NULL, -1};
