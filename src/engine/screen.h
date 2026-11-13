// How the game's pictures reach the window: the window's device context (counted), the Palette
// the window shows, a Buffer copied to the window, a window's background and the screen flip (see screen.c).
#pragma once

#include <stdint.h>

#include "game.h"

struct hook_writes;

#define SCREEN_THIS __attribute__((thiscall))

// The window's device context: the first user gets it and the last gives it back.
void *win_get_dc(void);
void win_release_dc(void);

// The palette handed to Windows (the one the game is showing): the main Buffer takes its colours
// and, when its serial number has changed, the system palette is animated to it.
int SCREEN_THIS palette_set(struct palette *p);
// A window keeps the serial number of the palette it last used.
void SCREEN_THIS win_sync_palette(struct game_win *w);

// A Buffer copied to the window `w` (cut to w's rectangle at 0x15c): from (sx, sy) in the Buffer, a
// cw by ch area, to (dx, dy) in the window; or the second one, in rectangles (from, to) with the
// copy stretched to fit. They return 0, 7 for a Buffer without pixels (or no device context), 0x10 for
// a missing argument and 0x12 for a Buffer on a DirectDraw surface.
int SCREEN_THIS buffer_copy_to_window(struct game_buffer *b, struct game_win *w, int sx, int sy, int dx, int dy, int cw, int ch);
int SCREEN_THIS buffer_copy_to_window_2(struct game_buffer *b, struct game_win *w, const int32_t *from, const int32_t *to);

// A line drawn in the window's DC with a pen made of a palette index, a width and a style.
void SCREEN_THIS win_window_line_raw(struct game_win *w, int x2, int y2, int x1, int y1, int color, int width, int style);
// A window's background painted into the window's DC, over the rectangle `rect`.
void SCREEN_THIS win_on_paint(struct game_win *w, const int32_t *rect);
// The main Buffer (with the text and cursor the game puts over it) to the screen: the whole of it, or the rectangle.
void win_flip(const int32_t *rect);
// What a flip writes (for the checks of what calls it).
void screen_writes_flip(struct hook_writes *w);

// A colour cycle's timer fired (on the timer's own thread): the colours of the cycle `id` move one place down the
// palette (the first goes last) and Windows is given them. Nothing happens while the system palette is held.
void palette_timer_callback(int id, struct palette *p);
