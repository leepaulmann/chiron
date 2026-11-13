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
