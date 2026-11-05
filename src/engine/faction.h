// Factions: probes and the energy market.
#pragma once

// What it costs `faction` to take `base` over, by its probes or, with
// `market`, when cornering the energy market; -1 for a probe when the
// base is out of reach.
int mind_control(int base, int faction, int market);
// What it costs `faction` to corner the energy market: all other bases'
// mind control costs but those of factions that have surrendered to it in
// a pact, and at least 1000.
int corner_market(int faction);
