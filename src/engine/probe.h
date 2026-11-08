// Probe teams' missions (probe.c).
#pragma once

// The probe team's action against a base (veh_target -1) or a unit (base
// -1). For the player, the action and its choices are asked in menus (or,
// replaying a move, read back from the unit); the AI picks its own. Made
// only with bit 2 of the game's attack flags; `toggle` 0 only asks whether
// a unit could be subverted. Returns 0 if nothing was done (or the player
// backed out), else 1.
int probe(int veh, int base, int veh_target, int toggle);
