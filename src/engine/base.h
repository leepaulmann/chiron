// Bases: what the squares around them yield, and the totals worked out
// from it.
#pragma once

#include <stdint.h>

// What a square yields to `base` (or -1 for none) of `faction`; with `flag`
// set, as if the square were improved for it (the game's terraforming
// advice). Output above 2 a square is cut until the faction has the tech for
// it, and the cut is added to game_terraform_reduce.
int crop_yield(int faction, int base, int x, int y, int flag);
int mine_yield(int faction, int base, int x, int y, int flag);
int energy_yield(int faction, int base, int x, int y, int flag);
// One of the three by RESOURCE_* type; RESOURCE_UNUSED clears
// game_terraform_reduce.
int resource_yield(int type, int faction, int base, int x, int y);

// Whether the base has the facility, or has it among the first `queue`
// items of its production queue.
int has_fac(int facility, int base, int queue);
// The squares of the current base's radius it may not work, into
// game_base_reserved.
void farm_compute(void);
// Places the current base's citizens on squares or as specialists, and
// sets its intake and terraforming damage.
void base_yield(void);
// The current base's supply convoys, unit upkeep and units away from home.
void base_support(void);
// The current base's nutrients and growth rating, and its minerals and
// ecological damage, from the intake base_yield() found.
void base_nutrient(void);
void base_minerals(void);

// Energy lost to distance from headquarters, out of `energy` at the
// current base.
int black_market(int energy);
// Citizens content before anything else, and bases allowed before more
// make drones, for the faction.
void psych_check(int faction, int *content, int *base_limit);
// The faction's base with `rank` of its others above it by energy intake,
// or -1.
int base_rank(int faction, int rank);
// The current base's energy and its split into economy, psych and labs,
// then base_psych().
void base_energy(void);
// The current base's talents, drones and superdrones; at the end of a
// turn also how content it would be at each TALENT and POLICE, for the AI.
void base_psych(void);

// Works out the current base from scratch: support, squares, nutrients,
// minerals, energy. Only when `update` is set or the base has changed.
void base_compute(int update);

// The population the base aims for, and the habitation facility it needs
// to grow further (or 0).
int pop_goal(int base);
int pop_goal_fac(int base);
// The faction's cost of growth (type 0) or building (1), in tenths; base
// may be -1.
int cost_factor(int faction, int type, int base);
int base_growth(void);
int base_queue(int base);
void base_production(void);
void base_energy_costs(void);
void base_research(void);
int fac_maint(int facility, int faction);
void base_maint(void);
void base_drones(void);
// A riot's turn: warnings, a facility lost, or a human's base defecting.
void drone_riot(void);
void base_doctors(void);
void base_check_support(void);
void base_hurry(void);
// Ecological damage: fungus, native life, warming and volcanoes.
void base_ecology(void);
// A new base's name, from its faction's file.
void name_base(int faction, char *name, int save, int sea);
// Shadow mode: holds the name the faction's next base gets (from a buffer as
// it stands), so a check can name a base without the file.
void name_base_hold(int faction, const char *name, int sea);
void name_base_release(void);
// The AI's terraforming plans for the current base.
void base_terraform(int instant);
// A road goal (or sensor goal) towards the current base's other bases, at
// `priority`; 1 if one was set.
int base_connect(int priority);
// A base's turn at the end of its faction's; 1 if the base was lost.
int base_upkeep(int base);
// A faction's turn for its bases, loans and reserves.
void production_phase(int faction);
// What the faction's energy goes to, for the AI; the player is warned.
int energy_limit(int faction);
void allocate_energy(int faction);
// A faction's units at the start of its turn: moves, orders and repairs.
void repair_phase(int faction);
// A faction's whole turn.
void faction_upkeep(int faction);

// Finding, marking and setting up bases (base.c).
void bitmask(int n, int *offset, uint32_t *mask);
void set_base(int base);
void say_base(char *buf, int base);
int base_at(int x, int y);
int base_find(int x, int y);
int base_find_2(int x, int y, int faction);
int base_find_3(int x, int y, int faction, int region, int other, int seen_by);
// The base searches counted (ours, and the original's while it runs inside
// a check), and the last one: kind 1 base_find, 2 base_find_2, 3
// base_find_3; its square, faction and the distance it left.
struct base_find_log {
    int count, kind, x, y, faction, dist;
};
extern struct base_find_log base_find_log;
void base_mark(int base);
int base_lose_minerals(int base);
void set_fac(int fac, int base, int on);
int has_fac_announced(int faction, int fac);
void set_fac_announced(int faction, int fac, int on);
void base_first(int base);
void make_base_unique(int base);
int has_project(int sp, int faction);
int base_project(int sp);
int base_claimed(int item, int score, int a, int b, int c, int d);
int has_fac_built(int fac);
uint32_t has_treaty(int faction, int other, uint32_t bits);
// Founds a base; its number, or -1 at the limit.
int base_init(int faction, int x, int y);
// Destroys a base; later bases move down.
void base_kill(int base);
// Whether a message on the main interface's list is about the base or a
// later one.
int base_listed(int base);
