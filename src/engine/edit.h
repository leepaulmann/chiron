// The game's EditBox and EditGroup classes (see edit.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define EDIT_THIS __attribute__((thiscall))

// EditBox: the text, the caret and the keys. `e` is the box; the handlers the Win's vtable holds take
// the box itself.
void EDIT_THIS edit_set_text(struct game_editbox *e, char *text);
int edit_caret_callback(struct game_editbox *e);  // (cdecl, the timer's callback: the box is the argument)
void EDIT_THIS edit_on_left_down(struct game_editbox *e, int x, int y);
void EDIT_THIS edit_on_left_up(struct game_editbox *e, int x, int y);
void EDIT_THIS edit_on_mouse_move(struct game_editbox *e, int x, int y);
int EDIT_THIS edit_text_fits(struct game_editbox *e);
int EDIT_THIS edit_text_position(struct game_editbox *e, int x);
int EDIT_THIS edit_on_key_click(struct game_editbox *e, int key, int a);
int EDIT_THIS edit_on_key_down(struct game_editbox *e, int key);
void EDIT_THIS edit_on_dialog_focus(struct game_editbox *e, int focus);
void EDIT_THIS edit_pass_dialog_focus(struct game_editbox *e);
int EDIT_THIS edit_in_focus(struct game_editbox *e);

// EditGroup. The first four and calculate_dimensions take the group; the others the Win 0x8c into it.
void EDIT_THIS group_set_text(struct game_editgroup *g, char *text, int i);
char *EDIT_THIS group_get_text(struct game_editgroup *g, int i);
void EDIT_THIS group_append_text(struct game_editgroup *g, char *out, int i);
void EDIT_THIS group_set_text_limits(struct game_editgroup *g, int n);
int EDIT_THIS group_calculate_dimensions(struct game_editgroup *g);
void EDIT_THIS group_on_dialog_focus(void *w, int focus);
void EDIT_THIS group_pass_dialog_focus(void *w);
