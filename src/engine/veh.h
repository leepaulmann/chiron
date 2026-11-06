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
