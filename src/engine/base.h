// Bases: what the squares around them yield, and the totals worked out
// from it.
#pragma once

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
