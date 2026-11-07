// The AI's turn around its units, and native life (ai.c).
#pragma once

// A faction's units each given their moves, kind by kind.
void enemy_turn(int faction);
// One unit's move (the game's enemy_move, or its order carried out): 1 if
// the unit is done or gone, 0 if it may move again.
int enemy_veh(int veh);
// The base native life heads for, from (x, y), with its distance in
// *game_base_find_dist; or -1.
int alien_base(int veh, int x, int y);
// A native unit's move: 1 if it fought, destroyed or died.
int alien_move(int veh);
// A planet buster of the AI's put next to a target (the direction to it
// returned) or in a base near one; -1 otherwise.
int enemy_planet_buster(int veh);
// The AI's move for an air unit (or missile) on (*x, *y), from enemy_move:
// mostly a go-to order; a missile may be put down elsewhere (*x, *y then
// its square). See ai.c for the results.
int air_power(int veh, int home, int now, int *x, int *y);
// The AI's move for one of its units (or the player's automated one): an
// order set, or a move carried out. 1 if the unit is done or gone. A
// negative veh (-1 - the unit) asks only whether it should hold its base.
int enemy_move(int veh);
