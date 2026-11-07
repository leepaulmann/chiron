// Spotting: what each faction has seen of the map, of bases and of units,
// reimplemented from terranx.exe 2.0. Behaviour follows the originals
// exactly, quirks included; see spot.c.
#pragma once

// A square seen by a faction (nothing off the map).
void spot_square(int x, int y, int faction);
// A base seen by a faction: its size as it is now, and its square.
void spot_base(int base, int faction);
// A unit's stack seen by a faction: its square and every unit in it.
void spot_stack(int veh, int faction);
// A unit's stack seen by no one.
void unspot_stack(int veh);
// A square seen by a faction, with the base or the stack on it.
void spot_loc(int x, int y, int faction);
// What a unit sees around it and who sees it, with the AI's goals, units
// woken and factions met that follow; `flag` (forced on outside a network
// game) lets it wake units, set goals and meet factions.
void spot_all(int veh, int flag);
