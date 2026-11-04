// Bases: what the squares around them yield.
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
// The current base's nutrients and growth rating, and its minerals and
// ecological damage, from the intake base_yield() found.
void base_nutrient(void);
void base_minerals(void);
