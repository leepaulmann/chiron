#pragma once
#include <stdint.h>

// Unit queries (veh.c): designs' abilities, costs, speeds and ratings, and
// units' morale.
int has_abil(int proto, uint32_t ability);
int best_reactor(int faction);
int planet_buster2(int proto);
int veh_cargo(int veh);
int psi_factor(int value, int faction, int attack, int fungus);
int weap_strat(int weapon, int faction);
int weap_val(int proto, int faction);
int arm_strat(int armor, int faction);
int arm_val(int armor, int faction);
int speed_proto(int proto);
int morale_mod(int base, int faction, int triad);
int breed_mod(int base, int faction);
int worm_mod(int base, int faction);
int morale_alien(int veh, int faction_vs);
int morale_veh(int veh, int riot, int faction_vs);
int base_cost(int proto);
int veh_cost(int proto, int base, int *prototype);
int garrison_check(int base);
int veh_avail(int proto, int faction, int base);
int can_arty(int proto, int sea);
int stack_check(int veh, int type, int c1, int c2, int c3);

// Stacks: the units on a square, as a list from its head.
int veh_at(int x, int y);
int veh_lift(int veh);
int veh_drop(int veh, int x, int y);
void veh_promote(int veh);
void veh_demote(int veh);
void stack_put(int veh, int x, int y);
void stack_sort(int veh);
void stack_sort_2(int veh);
int stack_fix(int veh);
void veh_clear(int veh, int proto, int faction);
void veh_sleep(int veh);
int veh_speed(int veh, int straight);
void veh_skip(int veh);

int offense_proto(int proto, int veh_def, int bombard);
int armor_proto(int proto, int veh_att, int bombard);
int prototype_factor(int proto);
int veh_find(int x, int y, int faction, int skip);
int veh_jail(int veh);
int veh_selectable(int veh);
int veh_unmoved(int veh);
int veh_ready(int veh);
int veh_fake(int proto, int faction);
int veh_wake(int veh);

int drop_range(int faction);
int planet_buster(int veh);
int veh_top(int veh);
int veh_moves(int veh);
int proto_power(int veh);
int armor_val(int proto, int faction);
int transport_val(int chassis, uint32_t ability, int reactor);
int abil_index(uint32_t ability);
int veh_put(int veh, int x, int y);
int veh_health(int veh);
int get_plan(int faction, int plan);
int want_monolith(int veh);
int want_to_wake(int faction, int veh, int spotted);
void wake_stack(int veh);
int wants_prototype(int proto, int faction);
void battle_init(void);
void add_bat(int side, int value, const char *label);
int defense_value(int faction, int x, int y, int veh_def, int veh_att);
int get_basic_offense(int veh_att, int veh_def, int psi, int bombard, int battle);
int get_basic_defense(int veh_def, int veh_att, int psi, int bombard);
int hex_cost(int proto, int faction, int xs, int ys, int xd, int yd, int fast);
void battle_compute(int veh_att, int veh_def, int *off_out, int *def_out, int combat);
int best_defender(int veh_def, int veh_att, int artillery);
int pick_chassis(int faction, int triad, int speed);
int weapon_budget(int faction, int cost, int combat_only);
int armor_budget(int faction, int cost);
int proto_cost(int chassis, int weapon, int armor, uint32_t abilities, int reactor);
void make_proto(int proto, int chassis, int weapon, int armor, uint32_t abilities, int reactor);
void go_to(int veh, int type, int x, int y);
void invasions(int base);
void veh_kill(int veh);
void stack_kill(int veh);
void kill(int veh);
int stack_veh(int veh, int type);
int veh_init(int proto, int faction, int x, int y);
int upgrade_cost(int faction, int proto_new, int proto_old);
void do_upgrade(int faction, int proto_new, int proto_old);
int upgrade_prototype(int faction, int proto_new, int proto_old, int quiet);
void upgrade_prototypes(int faction, int proto);
void promote(int veh);
void add_abil(uint32_t *abilities, uint32_t ability, int faction);
int abil_cond(int ability, int chassis, int weapon, int armor);
int is_port(int base, int base_radius);
int can_convoy(int x, int y);
int good_sensor(int faction, int x, int y);
int defensive_check(int base);
int guard_check(int faction, int region);
void enemy_capabilities(int faction);
int compute_odds(int odds, int faction, int veh, int target, int base);

// Designs: their names, and a faction's designs retired, marked obsolete
// and proposed.
int name_proto(char *name, int proto, int faction, int chassis, int weapon, int armor, uint32_t abilities, int reactor);
void retire_proto(int proto, int faction);
void prune_protos(int faction, int proto, int unk);
int propose_proto(int faction, int chassis, int weapon, int armor, uint32_t abilities, int reactor, int plan, char *name);
void consider_designs(int faction);
