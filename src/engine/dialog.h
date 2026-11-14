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
char *findnum(char *s);
int parse_string(char *src, char *dst);
// The popup entry points (cdecl): each builds the arguments of pops_18 (or pop_ask_6) from its own.
int popup(int a0, int a1);
int popup_2(int a0, int a1, int a2);
int popup_3(int a0, int a1, int a2, int a3, int a4);
int popup_4(int a0, int a1, int a2, int a3);
int popup_5(int a0, int a1, int a2, int a3, int a4);
int popup_6(int a0, int a1, int a2);
int popup_7(int a0, int a1, int a2, int a3);
int popup_8(int a0, int a1, int a2, int a3, int a4);
int popup_9(int a0, int a1, int a2, int a3, int a4, int a5);
int popups(int a0, int a1, int a2);
int popups_2(int a0, int a1, int a2, int a3, int a4);
int popups_3(int a0, int a1, int a2, int a3);
int popups_4(int a0, int a1, int a2, int a3, int a4, int a5);
int popups_5(int a0, int a1, int a2, int a3, int a4);
int popups_6(int a0, int a1, int a2, int a3, int a4, int a5, int a6);
int popups_7(int a0, int a1, int a2, int a3, int a4, int a5);
int popups_8(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7);
int popups_9(int a0, int a1, int a2, int a3, int a4, int a5);
int popups_10(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7);
int popups_11(int a0, int a1, int a2, int a3);
int popups_12(int a0, int a1, int a2, int a3, int a4, int a5);
int popups_13(int a0, int a1, int a2, int a3, int a4);
int popups_14(int a0, int a1, int a2, int a3, int a4, int a5, int a6);
int popups_15(int a0, int a1, int a2, int a3, int a4, int a5);
int popups_16(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7);
int popups_17(int a0, int a1, int a2, int a3, int a4, int a5, int a6);
int popup_ask(int a0, int a1, int a2, int a3, int a4);
int popup_ask_2(int a0, int a1, int a2, int a3, int a4);
int popup_ask_3(int a0, int a1, int a2, int a3);
int popup_ask_4(int a0, int a1, int a2, int a3, int a4);
int popup_ask_5(int a0, int a1, int a2, int a3);
int stoi(const char *s);
int btoi(const char *s);
int htoi(const char *s);
int text_item(void);  // (the address of the item buffer, as a number)
int text_item_number(void);
int text_item_binary(void);
int text_item_hex(void);
int DLG_THIS text_obj_item(void *t);
int DLG_THIS text_obj_item_number(void *t);
int DLG_THIS text_obj_item_binary(void *t);
int DLG_THIS text_obj_item_hex(void *t);
int DLG_THIS text_index_search(void *t, const char *name, const char *label);
int text_search_index(const char *name, const char *label);

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

// Life cycle (see dialog.c): a Dialog made and unmade. The constructor returns the object; the destructor does not
// free it.
void *DLG_THIS dialog_ctor(struct game_dialog *d);
void DLG_THIS dialog_dtor(struct game_dialog *d);

// The popup's life cycle and setters, and the SpriteBox's (see dialog.c). The constructors return the object; the destructors do not free it.
// A dialog list (the Dialog class's, which a BasePop and a StringBox keep too): made, or unmade with the game's list head put back.
void dlist_ctor(struct game_dlist *l);
void dlist_dtor(struct game_dlist *l);
void *DLG_THIS pop_ctor(struct game_base_pop *p);
void DLG_THIS pop_close(struct game_base_pop *p);
void DLG_THIS pop_dtor(struct game_base_pop *p);
int DLG_THIS pop_set_ok_text(struct game_base_pop *p, const char *text);
int DLG_THIS pop_set_cancel_text(struct game_base_pop *p, const char *text);
int pop_set_def_ok_text(const char *text);
int pop_set_def_cancel_text(const char *text);
// The SpriteBox's: `most_derived` says it is the whole object; the destructor is given the GraphicWin (0x8c into it).
void *DLG_THIS sprite_box_ctor(uint8_t *o, int most_derived);
void DLG_THIS sprite_box_close(uint8_t *o);
void DLG_THIS sprite_box_dtor(uint8_t *win);
void *DLG_THIS dialog_scalar_dtor(struct game_dialog *d, int flag);
