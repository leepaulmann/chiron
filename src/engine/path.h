// The path finder's map work (see path.c).
#pragma once

#include "game.h"

void __attribute__((thiscall)) path_make_abstract(struct game_path *p);
void __attribute__((thiscall)) path_merge(struct game_path *p, int from, int to);
void __attribute__((thiscall)) path_territory(struct game_path *p, int x, int y, int unused, int faction);
void __attribute__((thiscall)) path_continent(struct game_path *p, int x, int y, int region);
void __attribute__((thiscall)) path_continents(struct game_path *p);
int __attribute__((thiscall)) path_zoc_path(struct game_path *p, int x, int y, int faction);
// The first step from (x1, y1) towards (x2, y2), as a direction, or -1.
int __attribute__((thiscall)) path_find(struct game_path *p, int x1, int y1, int x2, int y2, int proto, int faction,
                                        int flags, int unk);
// The same, given what the original would read from below its stack.
int path_find_at(struct game_path *p, int x1, int y1, int x2, int y2, int proto, int faction, int flags, int unk,
                 int32_t stale);
int __attribute__((thiscall)) path_move(struct game_path *p, int veh, int faction);
int __attribute__((thiscall)) path_sensors(struct game_path *p, int faction, int *x, int *y);

// For checks of other modules' functions that search paths (path.c): the
// game's state and the path object's tables, as hooks_writes_add spans;
// and the word the original's Path_find last found below its stack, handed
// to ours as path_move's own check does.
struct hook_writes;
void writes_paths(struct hook_writes *w, struct game_path *p);
void path_stale_reset(void);
int32_t path_stale_take(void);
void path_stale_put(int32_t saved);
// The same for a check whose original searches many paths: record the
// words while the original runs, then replay them to ours' path_move
// calls in order, and stop.
void path_stale_record(void);
void path_stale_replay(void);
void path_stale_stop(void);
// The word for a Path_find call of one's own made outside path_move (as
// path_find_at's `stale`): the next one replayed, or path_move's.
int32_t path_stale_next(void);
// How many Path_find calls the original's run made (recorded) and ours
// took words for (replayed), for a check to compare.
int path_stale_logged(void);
int path_stale_used(void);
