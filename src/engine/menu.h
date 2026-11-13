// The game's Menu (menu bar), PullDown (drop-down menu) and Spot (hit rectangles) classes (see menu.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define MENU_THIS __attribute__((thiscall))

// Spot: a list of rectangles a screen can be hit in.
int MENU_THIS spot_clear(struct game_spot *s);
int MENU_THIS spot_ctor(struct game_spot *s);
int MENU_THIS spot_add(struct game_spot *s, int id, int kind, int x, int y, int w, int h);
int MENU_THIS spot_replace(struct game_spot *s, int id, int kind, int x, int y, int w, int h);
void MENU_THIS spot_kill_pos(struct game_spot *s, int index);
void MENU_THIS spot_kill_specific(struct game_spot *s, int id, int kind);
void MENU_THIS spot_kill_type(struct game_spot *s, int kind);
int MENU_THIS spot_check(struct game_spot *s, int x, int y, int *id, int *kind);
int MENU_THIS spot_check_2(struct game_spot *s, int x, int y, int *id, int *kind, int32_t *rect);
int MENU_THIS spot_get_rect(struct game_spot *s, int id, int kind, int32_t *rect);
int in_box(int x, int y, int left, int top, int w, int h);
int in_box_2(int x, int y, const int32_t *rect);

// PullDown: its lines.
int MENU_THIS pulldown_add_separator(struct game_pulldown *p, int id);
int MENU_THIS pulldown_unk2(struct game_pulldown *p, int a);
int MENU_THIS pulldown_unk3(struct game_pulldown *p, int a, int b, int c);
int MENU_THIS pulldown_hide_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_show_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_disable_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_enable_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_check_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_uncheck_item(struct game_pulldown *p, int id);
int MENU_THIS pulldown_id_to_index(struct game_pulldown *p, int id);
int MENU_THIS pulldown_get_selected(struct game_pulldown *p);
void MENU_THIS pulldown_compute_length(struct game_pulldown *p, const char *text);
// PullDown: the mouse and the stubs of the Win's handlers.
int MENU_THIS pulldown_on_right_down(struct game_pulldown *p, int x, int y);
int MENU_THIS pulldown_on_right_up(struct game_pulldown *p, int x, int y);
void MENU_THIS pulldown_on_left_up(struct game_pulldown *p, int x, int y);
void MENU_THIS pulldown_on_mouse_leave(struct game_pulldown *p, int x, int y);
int MENU_THIS pulldown_unk5(struct game_pulldown *p);
int MENU_THIS pulldown_unk6(struct game_pulldown *p);

// Menu: the titles, and its PullDowns' lines by title id.
int MENU_THIS menu_add_separator(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_unk2(struct game_menu *m, int a);
int MENU_THIS menu_unk3(struct game_menu *m, int menu_id, int a);
int MENU_THIS menu_unk4(struct game_menu *m, int a, int b, int c);
int MENU_THIS menu_unk5(struct game_menu *m, int menu_id, int a, int b, int c);
int MENU_THIS menu_unk6(struct game_menu *m, int menu_id);
int MENU_THIS menu_unk7(struct game_menu *m, int menu_id);
int MENU_THIS menu_unk8(struct game_menu *m, int menu_id);
int MENU_THIS menu_unk9(struct game_menu *m, int menu_id);
int MENU_THIS menu_hide_menu_item(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_show_menu_item(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_disable_menu_item(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_enable_menu_item(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_check_menu_item(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_uncheck_menu_item(struct game_menu *m, int menu_id, int id);
void MENU_THIS menu_set_menu_proc(struct game_menu *m, void(__attribute__((cdecl)) * proc)(int));
int MENU_THIS menu_unk11(struct game_menu *m, int menu_id, int id);
int MENU_THIS menu_unk12(struct game_menu *m, int menu_id, int index);
int MENU_THIS menu_id_to_index(struct game_menu *m, int id);
void MENU_THIS menu_adjust(struct game_menu *m);
int MENU_THIS menu_requested_height(struct game_menu *m);
void MENU_THIS menu_on_adjust_pulldown_pos(struct game_menu *m, int a, int b);

// Drawing (slot 0xf8): a PullDown's lines and a Menu's titles.
void MENU_THIS pulldown_on_redraw(struct game_pulldown *pd);
void MENU_THIS menu_on_redraw(struct game_menu *m);
void menu_draw_sweep(void);  // (the drawing checks' sweep, run by edit.c's once the fonts exist)
