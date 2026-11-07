// Setting up a game and its factions (setup.c).
#pragma once

// The setup_id setup_player() gets for a faction that lands late in the
// game, as a new one (turn_upkeep): set up as at the start.
enum { SETUP_LATE_ARRIVAL = -282 };

// A faction's start: its state reset, a landing site found and its first
// units (or base) put there. `setup_id` is the faction that put it out of
// the game (for an escape), -1 - the game's setup_game() argument at the
// start, or SETUP_LATE_ARRIVAL; `probe` makes it land wherever the search
// ends. Returns 1 if it landed, 0 if not (or if it is out of the game).
int setup_player(int faction, int setup_id, int probe);
// A new game's state, every faction set up and their agendas towards each
// other drawn. With `editor` (the scenario editor) no faction lands, and
// the agendas and the rules' extras (time warp, balance) are skipped.
void setup_game(int editor);
// The landing message for the faction.
void planetfall(int faction);
