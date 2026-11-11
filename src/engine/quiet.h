// Shadow mode's quiet hooks (quiet.c): what a sweep may set.
#pragma once

// The answer a held question gives (popp(), and a menu's choice in
// base_pop_exec_3()): 0 unless a sweep sets it, around its own checks.
extern int quiet_answer;
// Or, while quiet_menu_count is set, the answers to the menus one call
// shows, in turn (-1, cancelled, once they run out); a check sets
// quiet_menu_next to 0 for each run.
enum { QUIET_MENU_ANSWERS = 4 };
extern int quiet_menu_answers[QUIET_MENU_ANSWERS], quiet_menu_count, quiet_menu_next;
// Non-zero while a sweep checks a menu read from script.txt: the file is
// not opened, and no line is read (x_text_open() and text_get()).
extern int quiet_text;
