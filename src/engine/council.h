// The Planetary Council: votes, candidates and the AI's stand on each
// proposal, reimplemented from terranx.exe 2.0. See council.c.
#pragma once

enum {  // proposals
    PROP_GOVERNOR = 0, PROP_SUPREME_LEADER = 1, PROP_UNITY_CORE = 2, PROP_TRADE_PACT = 3, PROP_REPEAL_TRADE_PACT = 4,
    PROP_SOLAR_SHADE = 5, PROP_INCREASE_SHADE = 6, PROP_MELT_CAPS = 7, PROP_REPEAL_CHARTER = 8,
    PROP_REINSTATE_CHARTER = 9, PROP_COUNT = 11,
};

int council_votes(int faction);
int eligible(int faction);
int wants_prop(int faction, int prop, int target);
int council_get_vote(int faction, int prop, int target);
int can_call_council(int faction, int tell);
