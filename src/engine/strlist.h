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
// The procedures the timers call (stdcall; see strings.c): the SetTimer one is given the object as the timer's id, the multimedia
// one as its user value.
void __attribute__((stdcall)) time_timer_proc(void *hwnd, unsigned msg, struct game_time *t, unsigned tick);
void __attribute__((stdcall)) time_mm_proc(unsigned id, unsigned msg, struct game_time *t, unsigned dw1, unsigned dw2);

// The string list: seeking, walking and sorting. `l` is the list part of the object.
int STR_THIS strlist_seek_id(struct game_strlist *l, int id);
int STR_THIS strlist_seek_pos(struct game_strlist *l, int pos);
int STR_THIS strlist_current_id(struct game_strlist *l);
void *STR_THIS strlist_next_entry(struct game_strlist *l);
void *STR_THIS strlist_current_entry(struct game_strlist *l);
void STR_THIS strlist_sort(struct game_strlist *l, int unused);

// Life cycle (see strings.c): the list's constructor (`most_derived`: the whole object, with its virtual base),
// close (given the virtual base, 0x1c into the list), remove_all and deleting destructor (also given the
// virtual base); StringList's entries; Strings. The constructors return the object.
void *STR_THIS strlist_ctor(struct game_strlist *l, int most_derived);
void STR_THIS strlist_close(void *vb);
void STR_THIS strlist_remove_all(struct game_strlist *l);
void *STR_THIS strlist_scalar_dtor(void *vb, int flag);
int STR_THIS stringlist_fill_entry(struct game_strlist *l, void *item);
int STR_THIS stringlist_kill_entry(struct game_strlist *l, void *item);
int STR_THIS stringlist_add_special(struct game_strlist *l, const char *text);
int STR_THIS strlist_add(struct game_strlist *l, int id);
void STR_THIS arena_close(struct game_strings *a);
void STR_THIS strings_dtor(struct game_strings *t);
void *STR_THIS strings_ctor(struct game_strings *t);
int STR_THIS strings_init(struct game_strings *t, int size);
int STR_THIS strings_shutdown(struct game_strings *t);
char *STR_THIS strings_put(struct game_strings *t, const char *text);
