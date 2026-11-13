// The game's Dialog, SpriteBox and BasePop (popup) classes and the text functions popups use (see dialog.c).
#pragma once

#include <stdint.h>

#include "game.h"
#include "text.h"

#define DLG_THIS __attribute__((thiscall))

// Text.
int purge_leading(char *s);
void purge_trailing(char *s);
void purge_spaces(char *s);
void kill_lf(char *s);
void add_lf(char *s);

// BasePop.
void DLG_THIS pop_set_height(struct game_base_pop *p, int h);
void DLG_THIS pop_set_loc(struct game_base_pop *p, int x, int y);
int DLG_THIS pop_read_check(struct game_base_pop *p);
int DLG_THIS pop_on_key_click(struct game_base_pop *p, int a, int b);
int DLG_THIS pop_on_key_up(struct game_base_pop *p, int a);
int DLG_THIS pop_on_key_down(struct game_base_pop *p, int key);
void pop_double_click(void);
void DLG_THIS pop_on_left_down(struct game_base_pop *p, int x, int y);
void DLG_THIS pop_set_external_window(struct game_base_pop *p, void *win, int x, int y, int w, int h);
void pop_set_def_external_window(int a, int b, int c, int d, int e);
int DLG_THIS pop_set_string_font(struct game_base_pop *p, struct font *f, int a, int b, int c);
int DLG_THIS pop_set_button_font(struct game_base_pop *p, struct font *f, int a, int b);
void DLG_THIS pop_set_string_color(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_string_color2(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_string_color3(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_string_color_hyper(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_button_color(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_button_color2(struct game_base_pop *p, int a, int b, int c, int d);
void DLG_THIS pop_set_button_color3(struct game_base_pop *p, int a, int b, int c, int d);
int pop_set_def_string_font(struct font *f, int a, int b, int c);
int pop_set_def_button_font(struct font *f, int a, int b);
void pop_set_def_string_color(int a, int b, int c, int d);
void pop_set_def_string_color2(int a, int b, int c, int d);
void pop_set_def_string_color3(int a, int b, int c, int d);
void pop_set_def_string_color_hyper(int a, int b, int c, int d);
void pop_set_def_button_color(int a, int b, int c, int d);
void pop_set_def_button_color2(int a, int b, int c, int d);
void pop_set_def_button_color3(int a, int b, int c, int d);
int pop_set_def_file(const char *name);
int pop_on_modal(void);
void pop_fallout(void);
void DLG_THIS pop_unk3(struct game_base_pop *p, int on);
void DLG_THIS pop_unk4(struct game_base_pop *p, int on);

// Dialog.
int DLG_THIS dialog_close(struct game_dialog *d);
int DLG_THIS dialog_init(struct game_dialog *d);
int DLG_THIS dialog_init_2(struct game_dialog *d, int x, int y, int w, int h, int size);
int DLG_THIS dialog_init_3(struct game_dialog *d, const int32_t *r, int size);
int DLG_THIS dialog_init_4(struct game_dialog *d, int size);
int DLG_THIS dialog_init_5(struct game_dialog *d, int x, int y, int w, int h, void *ext);
int DLG_THIS dialog_init_6(struct game_dialog *d, const int32_t *r, void *ext);
int DLG_THIS dialog_init_7(struct game_dialog *d, void *ext);
int DLG_THIS dialog_unk1(struct game_dialog *d, int a, int b);
int DLG_THIS dialog_set_font(struct game_dialog *d, struct font *f, int a, int b);
void DLG_THIS dialog_set_text_color(struct game_dialog *d, int a, int b, int c, int e);
void DLG_THIS dialog_set_text_color2(struct game_dialog *d, int a, int b, int c, int e);
void DLG_THIS dialog_set_text_color3(struct game_dialog *d, int a, int b, int c, int e);
int dialog_set_def_font(struct font *f, int a, int b);
void dialog_set_def_text_color(int a, int b, int c, int d, int idx);

// SpriteBox (the mouse functions take the Win part, 0x8c into the object).
int DLG_THIS sprite_box_id_to_pos(struct game_sprite_box *sb, int id);
int DLG_THIS sprite_box_pos_to_id(struct game_sprite_box *sb, int pos);
void DLG_THIS sprite_box_on_dialog_focus(void *w, int unused);
void DLG_THIS sprite_box_on_left_down(void *w, int x, int y);
void DLG_THIS sprite_box_on_left_up(void *w, int x, int y);
void DLG_THIS sprite_box_on_right_click(void *w, int x, int y);
void DLG_THIS sprite_box_on_right_down(void *w, int x, int y);
void DLG_THIS sprite_box_on_right_up(void *w, int x, int y);
void DLG_THIS sprite_box_on_left_double_click(void *w, int x, int y);
void DLG_THIS sprite_box_on_right_double_click(void *w, int x, int y);
