// The game's Win class, the base of every window and widget (see win.c): geometry, the tree
// of windows, the lists the game keeps of them, and the questions it asks about them.
#pragma once

#include <stdint.h>

#include "game.h"

#define WIN_THIS __attribute__((thiscall))

// Visibility and the tree.
int WIN_THIS win_is_visible(struct game_win *w);
int WIN_THIS win_is_descendant(struct game_win *w, struct game_win *other);
int WIN_THIS win_is_below(struct game_win *w, struct game_win *root);
int WIN_THIS win_has_child(struct game_win *w, struct game_win *child);
int WIN_THIS win_in_modal(struct game_win *w);
int WIN_THIS win_is_dialog_focus(struct game_win *w);
void win_set_def_focus(struct game_win *w);
struct game_win *win_get_key_window(void);
void win_add_parent(struct game_win *w);
void WIN_THIS win_add_child(struct game_win *w, struct game_win *child);
void WIN_THIS win_release_modal(struct game_win *w);

// The z-order list.
void win_recurse_zorder(struct game_win *w);
void win_update_zorder(void);
void WIN_THIS win_bring_to_top(struct game_win *w);
void win_bring_parent_to_top(struct game_win *w);
void WIN_THIS win_bring_child_to_top(struct game_win *w, struct game_win *child);

// Coordinates. The _2 ones take a point as two ints, the others a RECT (left, top, right,
// bottom); the nonclient ones also count the frame.
void WIN_THIS win_screen_to_client(struct game_win *w, int32_t *rect);
void WIN_THIS win_client_to_screen(struct game_win *w, int32_t *rect);
void WIN_THIS win_nonscreen_to_client(struct game_win *w, int32_t *rect);
void WIN_THIS win_nonclient_to_screen(struct game_win *w, int32_t *rect);
void WIN_THIS win_client_to_screen_2(struct game_win *w, int32_t *x, int32_t *y);
void WIN_THIS win_screen_to_client_2(struct game_win *w, int32_t *x, int32_t *y);
void WIN_THIS win_nonclient_to_screen_2(struct game_win *w, int32_t *x, int32_t *y);
void WIN_THIS win_screen_to_nonclient(struct game_win *w, int32_t *x, int32_t *y);
void WIN_THIS win_client_to_nonclient(struct game_win *w, int32_t *a, int32_t *b);
void WIN_THIS win_nonclient_to_client(struct game_win *w, int32_t *a, int32_t *b);
void WIN_THIS win_client_to_nonclient_2(struct game_win *w, int32_t *rect);
int WIN_THIS win_move(struct game_win *w, int x, int y);
int WIN_THIS win_on_nc_hittest(struct game_win *w, int x, int y);

// Scroll bars.
int WIN_THIS win_get_vert_pos(struct game_win *w);
int WIN_THIS win_get_horz_pos(struct game_win *w);
void WIN_THIS win_set_vert_paging(struct game_win *w, int paging);
void WIN_THIS win_set_horz_paging(struct game_win *w, int paging);
void WIN_THIS win_unk8(struct game_win *w, int value);
void WIN_THIS win_unk9(struct game_win *w, int value);

// The parent and child lists (a window is hidden through its vtable first).
void win_remove_parent(struct game_win *w);
void WIN_THIS win_remove_child(struct game_win *w, struct game_win *child);

// Scroll bars set through the window.
void WIN_THIS win_set_vert_pos(struct game_win *w, int pos);
void WIN_THIS win_set_horz_pos(struct game_win *w, int pos);
void WIN_THIS win_set_vert_range(struct game_win *w, int a, int b);
void WIN_THIS win_set_horz_range(struct game_win *w, int a, int b);

// Caption and borders, and the dialog list's focus.
void WIN_THIS win_set_caption_height(struct game_win *w, int h);
void WIN_THIS win_set_border_thickness(struct game_win *w, int b);
void WIN_THIS win_set_bottom_border_thickness(struct game_win *w, int b);
void WIN_THIS win_set_dialog_focus(struct game_win *w, struct game_win *win);
void WIN_THIS win_pass_dialog_focus(struct game_win *w);

// The mouse and the game's window (these call Windows).
int WIN_THIS win_set_mouse_pos(struct game_win *w, int x, int y);
void WIN_THIS win_get_mouse_pos(struct game_win *w, int32_t *px, int32_t *py);
int win_get_lbutton_state(void);
int win_get_rbutton_state(void);
int WIN_THIS win_maximize(struct game_win *w);
int WIN_THIS win_minimize(struct game_win *w);
int WIN_THIS win_show_maximize(struct game_win *w);

// Placing and sizing.
int WIN_THIS win_center(struct game_win *w);
void WIN_THIS win_nonclient_to_client_2(struct game_win *w, int32_t *rect);
void WIN_THIS win_redo_caption_buttons(struct game_win *w);
void WIN_THIS win_resize_event(struct game_win *w, int cw, int ch);
int WIN_THIS win_resize(struct game_win *w, int cx, int cy, int client);
void *WIN_THIS win_redraw_nc_buffer(struct game_win *w, unsigned which);
int WIN_THIS win_update_nc_buffer(struct game_win *w, int what);

// The mouse and the screen's drawing (see win.c).
struct game_win *win_get_mouse_window(int32_t *px, int32_t *py);
void win_update_screen(const int32_t *rect, struct game_win *win);
void WIN_THIS win_update_window(struct game_win *w, const int32_t *rect);
void WIN_THIS win_update_back_to_window(struct game_win *w, struct game_buffer *dest);
void WIN_THIS win_update_window_to_buffer(struct game_win *w, struct game_buffer *dest);
void win_redraw(void);
void win_update(void);

// The nonclient area (see win.c); the game's calls of these run the original in shadow mode.
void WIN_THIS win_set_rects(struct game_win *w);
void WIN_THIS win_on_redraw_nc(struct game_win *w, const int32_t *rect, int which);
void WIN_THIS win_draw_nc_to(struct game_win *w, struct game_buffer *dest, int x, int y);
void WIN_THIS win_on_nc_paint(struct game_win *w, const int32_t *rect, int which);

// Mouse and key events (see win.c): the one-event functions a window's handlers are called by, the
// raw events the game's window procedure gives a window, and the calls it makes to find the window.
// The functions call the window's virtual functions, so the game's own calls of them run the original
// in shadow mode.
void WIN_THIS win_left_down_event(struct game_win *w, int x, int y, int nc);
void WIN_THIS win_left_up_event(struct game_win *w, int x, int y, int nc);
void WIN_THIS win_right_click_event(struct game_win *w, int x, int y, int nc);
void WIN_THIS win_right_down_event(struct game_win *w, int x, int y, int nc);
void WIN_THIS win_right_up_event(struct game_win *w, int x, int y, int nc);
int WIN_THIS win_key_click_event(struct game_win *w, int a, int key);
int WIN_THIS win_key_down_event(struct game_win *w, int key);
int WIN_THIS win_key_up_event(struct game_win *w, int key);
void WIN_THIS win_on_mouse_move(struct game_win *w, int x, int y, int keys, int nc);
void WIN_THIS win_on_mouse_wheel(struct game_win *w, int keys, int delta, int x, int y, int nc);
void WIN_THIS win_on_l_button_down(struct game_win *w, int dbl, int x, int y, int keys, int nc);
void WIN_THIS win_on_l_button_up(struct game_win *w, int x, int y, int keys, int nc);
void WIN_THIS win_on_r_button_down(struct game_win *w, int dbl, int x, int y, int keys, int nc);
void WIN_THIS win_on_r_button_up(struct game_win *w, int x, int y, int keys, int nc);
int WIN_THIS win_on_sys_key(struct game_win *w, int vk, int down, int rep, int scan);
void win_OnKey(int unused, int vk, int down, int rep, int scan);
void win_OnChar(int unused, int ch, int rep);
void win_OnSysKey(int unused, int vk, int down, int rep, int scan);
void win_OnLButtonDown(int hwnd, int dbl, int x, int y, int keys);
void win_OnLButtonUp(int hwnd, int x, int y, int keys);
void win_OnRButtonDown(int hwnd, int dbl, int x, int y, int keys);
void win_OnRButtonUp(int hwnd, int x, int y, int keys);

// The GraphicWin class (a Win with a Buffer of its own at 0x444, see win.c): showing what was drawn in it,
// and drawing it into the back buffer.
int WIN_THIS graphic_win_update(struct game_win *w, int x, int y, int cw, int ch, int unused);
int WIN_THIS graphic_win_update_2(struct game_win *w, const int32_t *rect, int unused);
int WIN_THIS graphic_win_update_3(struct game_win *w, int unused);
int WIN_THIS graphic_win_soft_update(struct game_win *w, int x, int y, int cw, int ch);
int WIN_THIS graphic_win_soft_update_2(struct game_win *w);
int WIN_THIS graphic_win_soft_update_3(struct game_win *w, const int32_t *rect);
void WIN_THIS graphic_win_on_redraw_nc(struct game_win *w, const int32_t *rect, int which);
void WIN_THIS win_copy_into_frame(struct game_win *w, struct game_win *o, const int32_t *rect, int which);
void WIN_THIS graphic_win_on_paint(struct game_win *w, const int32_t *clip);

// Life cycle (win.c): the base class's defaults, a Win and a GraphicWin made and unmade. The constructors
// return the object; the destructors do not free it.
void *WIN_THIS winbase_ctor(struct game_win *w);
void WIN_THIS winbase_reset(struct game_win *w);
void *WIN_THIS win_ctor(struct game_win *w);
void WIN_THIS win_dtor(struct game_win *w);
void *WIN_THIS graphic_win_ctor(struct game_win *w);
void WIN_THIS graphic_win_dtor(struct game_win *w);
void WIN_THIS graphic_win_close(struct game_win *w);
