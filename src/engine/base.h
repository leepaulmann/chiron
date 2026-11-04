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
