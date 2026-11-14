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
void BUTTON_THIS button_on_left_down(struct game_button *b, int x, int y);
void BUTTON_THIS button_on_left_up(struct game_button *b, int x, int y);
void BUTTON_THIS button_on_mouse_move(struct game_button *b, int x, int y);
// The mouse handlers of FlatButton (which has no left down or up of its own: BaseButton's), and BaseButton's timer callbacks (the
// bubble text's delay, the auto-repeat; the first is the one the Time class calls, which finds the button in its second argument).
void BUTTON_THIS flat_button_on_mouse_move(struct game_button *b, int x, int y);
void BUTTON_THIS flat_button_on_mouse_leave(struct game_button *b, int x, int y);
void BUTTON_THIS button_timer_callback(struct game_button *b, int which);
void button_timer_callback_daemon(int which, struct game_button *b);

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

// A field of the Dialog part by its offset (the layout is in button.c).
#define DLG_F(d, off) (*(int32_t *)((uint8_t *)(d) + (off)))
// What a CheckBox's, RadioButton's and ListBox's layouts begin with: the Buffer's text colours and Fonts as
// the Dialog part says, and the widest item (the cursor goes round the ring; with `wrap` each item's line
// count is set from its width over that).
void dlg_text_setup(struct game_buffer *buf, struct game_dlg *d);
void dlg_measure(struct game_buffer *buf, struct game_dlg *d, int wrap, int *widest);

// FlatButton's redraw; CheckBox's and RadioButton's layout, item drawing and redraw (the redraws take the
// Win, as the handlers do).
void BUTTON_THIS flat_button_on_redraw(struct game_flat_button *f);
int BUTTON_THIS checkbox_calculate_dimensions(void *cb);
int BUTTON_THIS radio_calculate_dimensions(void *rb);
void BUTTON_THIS checkbox_draw_item(void *cb, int pos, int checked, int unused);
void BUTTON_THIS radio_draw_item(void *rb, int pos, int selected);
void BUTTON_THIS checkbox_on_redraw(void *w);
void BUTTON_THIS radio_on_redraw(void *w);

// The sweeps' Buffer with a device context and a Font (see button.c), or NULL before the game has fonts.
struct font;
struct game_buffer *button_sweep_buffer(struct font **font);

// Life cycle: BaseButton and what derives from it, and CheckButton (see button.c). The constructors return the
// object; the destructors do not free it.
void *BUTTON_THIS base_button_ctor(struct game_button *b);
void BUTTON_THIS base_button_close(struct game_button *b);
void BUTTON_THIS base_button_dtor(struct game_button *b);
int BUTTON_THIS base_button_set_name(struct game_button *b, const char *name);
int BUTTON_THIS base_button_set_bubble_text(struct game_button *b, const char *text);
void *BUTTON_THIS flat_button_ctor(struct game_flat_button *f);
void BUTTON_THIS flat_button_close(struct game_flat_button *f);
void BUTTON_THIS flat_button_dtor(struct game_flat_button *f);
void *BUTTON_THIS push_button_ctor(struct game_push_button *p);
void BUTTON_THIS push_button_dtor(struct game_push_button *p);
void *BUTTON_THIS image_button_ctor(struct game_button *b);
void BUTTON_THIS image_button_dtor(struct game_button *b);
void *BUTTON_THIS check_button_ctor(struct game_check_button *c);
void BUTTON_THIS check_button_close(struct game_check_button *c);
void BUTTON_THIS check_button_dtor(struct game_check_button *c);

// CheckBox and RadioButton: objects with a GraphicWin and a Dialog as virtual bases (see button.c). `o` is the
// object; the destructors are given the GraphicWin base (0x1c, or 0x18, into it).
void *BUTTON_THIS checkbox_ctor(uint8_t *o, int most_derived);
void BUTTON_THIS checkbox_dtor(uint8_t *win);
void *BUTTON_THIS radio_ctor(uint8_t *o, int most_derived);
void BUTTON_THIS radio_close(uint8_t *o);
void *BUTTON_THIS radio_dtor(uint8_t *win, int flag);

// The buttons' scalar deleting destructors (button.c).
void *BUTTON_THIS base_button_scalar_dtor(struct game_button *b, int flag);
void *BUTTON_THIS flat_button_scalar_dtor(struct game_flat_button *f, int flag);
void *BUTTON_THIS push_button_scalar_dtor(struct game_push_button *p, int flag);
void *BUTTON_THIS image_button_scalar_dtor(struct game_button *b, int flag);
void *BUTTON_THIS check_button_scalar_dtor(struct game_check_button *c, int flag);
