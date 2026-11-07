// The AI's strategy.
#pragma once

// The AI's planning at the start of a faction's turn (see strategy.c):
// the census of its bases and units, its goals, regional plans, its
// attack target and its units' orders.
void enemy_strategy(int faction);

// The AI's goals (75 a faction, rebuilt each turn) and sites (25, kept
// from turn to turn), see strategy.c.
void add_goal(int faction, int type, int priority, int x, int y, int base);
void add_site(int faction, int type, int priority, int x, int y);
int at_goal(int faction, int type, int x, int y);
int at_site(int faction, int type, int x, int y);
void wipe_goals(int faction);
void init_goals(int faction);
void del_site(int faction, int type, int x, int y, int dist);
