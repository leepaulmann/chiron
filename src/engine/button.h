// The game's button classes: BaseButton and what derives from it, ButtonGroup, CheckButton, and
// the Dialog list CheckBox and RadioButton keep their items in (see button.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define BUTTON_THIS __attribute__((thiscall))

// BaseButton's defaults (cdecl: no object) and its state, text colours, events.
void button_set_def_text_color(int a, int b, int c, int d);
void button_set_def_text_color2(int a, int b, int c, int d);
void button_set_def_text_color3(int a, int b, int c, int d);
int button_set_def_font(void *font, void *a, void *b);
void BUTTON_THIS button_set(struct game_button *b, int state);
void BUTTON_THIS button_toggle(struct game_button *b);
void BUTTON_THIS button_on_dialog_focus(struct game_button *b, int unused);
void BUTTON_THIS button_on_mouse_leave(struct game_button *b, int x, int y);
void BUTTON_THIS button_on_left_click(struct game_button *b, int x, int y);
void BUTTON_THIS button_set_text_color(struct game_button *b, int a, int c, int d, int e);
void BUTTON_THIS button_set_text_color2(struct game_button *b, int a, int c, int d, int e);
void BUTTON_THIS button_set_text_color3(struct game_button *b, int a, int c, int d, int e);

// ButtonGroup.
void *BUTTON_THIS button_group_ctor(struct game_button_group *g);
int BUTTON_THIS button_group_close(struct game_button_group *g);
int BUTTON_THIS button_group_init(struct game_button_group *g, int user, int flags);
int BUTTON_THIS button_group_add(struct game_button_group *g, struct game_button *b);
int BUTTON_THIS button_group_set(struct game_button_group *g, int id, int unused);
int BUTTON_THIS button_group_click(struct game_button_group *g, int id);

// The Dialog list: positions and ids of the items.
void BUTTON_THIS dialog_set_selected_id(struct game_dlg *d, int id);
int BUTTON_THIS dialog_get_selected_id(struct game_dlg *d);
int BUTTON_THIS dialog_id_to_pos(struct game_dlg *d, int id);
int BUTTON_THIS dialog_pos_to_id(struct game_dlg *d, int pos);

// CheckBox: `cb` is the CheckBox object, whose virtual bases (the Win and the Dialog) are found
// through its vbtable. The handlers take the Win (at 0x1c in a CheckBox, 0x18 in a RadioButton).
void BUTTON_THIS checkbox_flip_state_id(void *cb, int id);
void BUTTON_THIS checkbox_flip_state_pos(void *cb, int pos);
void BUTTON_THIS checkbox_set_state_id(void *cb, int id, int on);
int BUTTON_THIS checkbox_get_state_id(void *cb, int id);
void BUTTON_THIS checkbox_set_state_pos(void *cb, int pos, int on);
int BUTTON_THIS checkbox_get_state_pos(void *cb, int pos);
void BUTTON_THIS checkbox_set_state_flag(void *cb, int mask);
void BUTTON_THIS checkbox_on_left_double_click(void *w, int x, int y);
void BUTTON_THIS checkbox_on_dialog_focus(void *w, int unused);
void BUTTON_THIS checkbox_on_mouse_leave(void *w, int x, int y);
void BUTTON_THIS checkbox_on_mouse_move(void *w, int x, int y);
void BUTTON_THIS radio_on_left_double_click(void *w, int x, int y);
void BUTTON_THIS radio_on_dialog_focus(void *w, int unused);
void BUTTON_THIS radio_on_mouse_leave(void *w, int x, int y);

// CheckButton.
void BUTTON_THIS checkbtn_on_left_click(struct game_check_button *c, int x, int y);
void BUTTON_THIS checkbtn_on_left_double_click(struct game_check_button *c, int x, int y);

// Drawing: GraphicWin_fill, ImageButton's and CheckButton's redraws.
void BUTTON_THIS graphic_win_fill(struct game_win *w, int color);
void BUTTON_THIS image_button_on_redraw(struct game_button *b);
void BUTTON_THIS checkbtn_on_redraw(struct game_check_button *c);
void BUTTON_THIS push_button_on_redraw(struct game_push_button *p);
