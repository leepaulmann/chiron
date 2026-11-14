// The game's Scroll (scroll bar), ListBox and StringBox classes (see scroll.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define SCROLL_THIS __attribute__((thiscall))

// Scroll: state, colours and pictures.
void SCROLL_THIS scroll_set_range(struct game_scroll *s, int lo, int hi);
void SCROLL_THIS scroll_set_button_color(struct game_scroll *s, int color);
void SCROLL_THIS scroll_set_bevel_thickness(struct game_scroll *s, int n);
void SCROLL_THIS scroll_set_bevel_upper(struct game_scroll *s, int color);
void SCROLL_THIS scroll_set_bevel_lower(struct game_scroll *s, int color);
void SCROLL_THIS scroll_set_border_color(struct game_scroll *s, int color);
void SCROLL_THIS scroll_set_bar_thickness(struct game_scroll *s, int n);
void SCROLL_THIS scroll_set_sprite_left(struct game_scroll *s, int a, int b, int c);
void SCROLL_THIS scroll_set_sprite_right(struct game_scroll *s, int a, int b, int c);
void SCROLL_THIS scroll_set_sprite_up(struct game_scroll *s, int a, int b, int c);
void SCROLL_THIS scroll_set_sprite_down(struct game_scroll *s, int a, int b, int c);
void SCROLL_THIS scroll_set_pos(struct game_scroll *s, int pos);
void SCROLL_THIS scroll_compute_thumb_rect(struct game_scroll *s, int32_t *out);
void SCROLL_THIS scroll_set_thumb_rect(struct game_scroll *s);

// Scroll: the mouse, the keyboard, the arrows, the size and the drawing.
void SCROLL_THIS scroll_on_left_down(struct game_scroll *s, int x, int y);
void SCROLL_THIS scroll_on_left_up(struct game_scroll *s, int x, int y);
void SCROLL_THIS scroll_on_mousewheel_down(struct game_scroll *s, int notify);
void SCROLL_THIS scroll_on_mousewheel_up(struct game_scroll *s, int notify);
int SCROLL_THIS scroll_on_key_click(struct game_scroll *s, int a, int key);
void SCROLL_THIS scroll_on_button_clicked(struct game_scroll *s, int which);
int SCROLL_THIS scroll_on_resize(struct game_scroll *s, int a, int b);
void SCROLL_THIS scroll_on_redraw(struct game_scroll *s);

// ListBox: `l` is the ListBox; the handlers the Win's vtable holds take the Win, 0x48 into it.
void SCROLL_THIS listbox_set_selected_id(struct game_listbox *l, int id);
void SCROLL_THIS listbox_set_selected_pos(struct game_listbox *l, int pos, int notify);
int SCROLL_THIS listbox_find(struct game_listbox *l, const char *text);
void SCROLL_THIS listbox_on_scrolled(void *w, int a, int b);
void SCROLL_THIS listbox_on_right_down(void *w, int x, int y);
void SCROLL_THIS listbox_on_right_double_click(void *w, int x, int y);
void SCROLL_THIS listbox_on_left_double_click(void *w, int x, int y);
void SCROLL_THIS listbox_on_mousewheel(void *w, int delta);
void SCROLL_THIS listbox_on_mouse_leave(void *w, int x, int y);
int SCROLL_THIS listbox_calculate_dimensions(struct game_listbox *l);
void SCROLL_THIS listbox_on_redraw(void *w);
void SCROLL_THIS listbox_draw_item(struct game_listbox *l, int pos);

// StringBox.
void SCROLL_THIS stringbox_on_scrolled(struct game_stringbox *b, int a, int pos);
int SCROLL_THIS stringbox_on_link_clicked(struct game_stringbox *b, int a, int c);

// Life cycle (see scroll.c): the constructor returns the object; the destructor does not free it.
void *SCROLL_THIS scroll_ctor(struct game_scroll *s);
void SCROLL_THIS scroll_close(struct game_scroll *s);
void SCROLL_THIS scroll_dtor(struct game_scroll *s);

// ListBox's life cycle (see scroll.c): `o` is the ListBox, whose virtual bases lie where its table says; the
// destructor is given the GraphicWin base (0x48 in).
void *SCROLL_THIS listbox_ctor(uint8_t *o, int most_derived);
void SCROLL_THIS listbox_close(uint8_t *o);
void SCROLL_THIS listbox_dtor(uint8_t *win);
