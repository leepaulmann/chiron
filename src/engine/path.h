// The path finder's map work (see path.c).
#pragma once

#include "game.h"

void __attribute__((thiscall)) path_make_abstract(struct game_path *p);
void __attribute__((thiscall)) path_merge(struct game_path *p, int from, int to);
void __attribute__((thiscall)) path_territory(struct game_path *p, int x, int y, int unused, int faction);
void __attribute__((thiscall)) path_continent(struct game_path *p, int x, int y, int region);
void __attribute__((thiscall)) path_continents(struct game_path *p);
