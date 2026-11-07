// Combat and probe team helpers, reimplemented from terranx.exe 2.0; see
// combat.c.
#pragma once

// A unit killed in combat by veh_att of faction_att (either may be -1).
// An artifact taken by a living attacker on land, or on a transport, is
// captured instead (returns 0, *captured counted); else it dies (returns
// 1, *killed counted), and a native unit killed by a faction's unit earns
// it planetpearls in *credits. The pointers may be NULL.
int battle_kill(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att);
// The same for every unit in the unit's stack; chaff: only the native
// units among them and those with no hit points left.
void battle_kill_stack(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att);
void battle_kill_chaff(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att);
// An attack's diplomatic cost: a truce, treaty or pact broken (returns 1),
// and vendetta.
int act_of_aggression(int faction, int victim);
// A probe team's stolen tech: 1 if there was none to take.
int steal_tech(int faction, int victim, int steal);
// The energy a probe team takes from a base.
int steal_energy(int base);
// The distance from the faction's headquarters, or 12 without one.
int vulnerable(int faction, int x, int y);
// A probe action's chance of success, its text put in parse slot `slot`.
int success_rates(int slot, int morale, int difficulty, int base);
// An air superiority unit of faction_def flying to (x, y) to meet an
// attack: 1 if one did.
int interceptor(int faction_def, int faction_att, int x, int y);
// Orbital defense pods and flechette defenses against a missile at (x, y):
// 1 if it was shot down.
int shoot(int faction_att, int faction_def, int x, int y);
// A planet buster's detonation at (x, y).
void planet_busting(int veh, int x, int y);
