// The game's Time, StringStruct (string list) and StringList classes and the string table's lookup
// (see strings.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define STR_THIS __attribute__((thiscall))

// Strings (the table the game's text is looked up in).
unsigned STR_THIS strings_get(struct game_strings *t, unsigned p);

// Time: a timer an object owns. The arguments of the start and pulse functions are the function or
// functions to call and what to call them with, then the delay and the resolution; each returns 0, or
// 2 if no timer could be made, or 7 if there is nothing to call.
void *STR_THIS time_ctor(struct game_time *t);
void STR_THIS time_dtor(struct game_time *t);
int STR_THIS time_init(struct game_time *t, void *cb, void *arg, uint32_t delay, int res);
int STR_THIS time_init_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res);
int STR_THIS time_start(struct game_time *t, void *cb, void *arg, uint32_t delay, int res);
int STR_THIS time_start_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res);
int STR_THIS time_pulse(struct game_time *t, void *cb, void *arg, uint32_t delay, int res);
int STR_THIS time_pulse_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res);
int STR_THIS time_start_3(struct game_time *t);
int STR_THIS time_pulse_3(struct game_time *t);
void STR_THIS time_stop(struct game_time *t);
void STR_THIS time_close(struct game_time *t);

// The string list: seeking, walking and sorting. `l` is the list part of the object.
int STR_THIS strlist_seek_id(struct game_strlist *l, int id);
int STR_THIS strlist_seek_pos(struct game_strlist *l, int pos);
int STR_THIS strlist_current_id(struct game_strlist *l);
void *STR_THIS strlist_next_entry(struct game_strlist *l);
void *STR_THIS strlist_current_entry(struct game_strlist *l);
void STR_THIS strlist_sort(struct game_strlist *l, int unused);
