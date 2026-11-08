// Scores, rankings and scenario objectives, reimplemented from terranx.exe
// 2.0; see score.c.
#pragma once
// The calendar year of a turn, and the current one added to `buf`.
int game_year(int turn);
void say_year(char *buf);
// A score scaled by the faction's difficulty, for the end of game screen.
int get_rating(int faction, int score);
// Whether the base counts as a scenario objective.
int is_objective(int base);
// The objectives a faction holds; with `pact`, in a cooperative game also
// those of its pact partners.
int num_objectives(int faction, int pact);
// The most objectives any faction holds, the (last) faction holding them
// and how many others tie with it; either pointer may be NULL.
int most_objectives(int *faction, int *tied);
// Whether the Voice of Planet stands, and whether the faction is also
// building (as the original reads its queue) the Ascent to Transcendence.
int ascending(int faction);
int transcending(int faction);
// Each faction's strength and, with `full`, the rankings from it and the
// AI's plotting against a faction that is far ahead.
void rankings(int full);
// A faction's score and its parts (into the faction's own fields if NULL);
// with `partial`, without the comparison with others or saving it.
void compute_score(int faction, int *score, int *parts, int partial);
// At the start of a turn (`detect`, finding any win) and as it ends:
// whether the game is over, with its end shown once. Returns 1 to leave
// the game.
int end_of_game(int detect);
// Whether end_of_game(detect) would show the end now (nothing changed).
int end_of_game_due(int detect);
