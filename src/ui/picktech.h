// The research picker (PickTech): the window the game opens to choose a tech to research. See picktech.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// How the techs of the list are laid out: a grid of cells, `cols` across and `rows` down. A cell is `cell_w` by `cell_h`
// with one pixel between cells; the window is the grid plus a 30 pixel title (0x1d for hit tests) at the top.
struct picktech_grid {
    int cols, rows;
    int cell_w, cell_h;
    int margin;         // the gap the text keeps from the top of a cell
    int win_w, win_h;   // the window's client size
};

// How many columns a list of `count` techs is cut into (a table the original fills in; 10 from 40 on).
int picktech_columns(int count);

// The grid for `count` techs on a screen of the given size: a window a third of the screen wide at most, cells one and a
// half times as high as wide and at least half the screen's height, squeezed to fit when that is too much.
void picktech_grid(struct picktech_grid *g, int count, int screen_w, int screen_h);

// The rectangle of cell `index` in the window's client area (left, top, right, bottom), and the same clipped to a Buffer of
// the given size as the original does before it draws a cell's frame.
void picktech_cell_rect(struct ui_rect *r, const struct picktech_grid *g, int index);
void picktech_cell_clip(struct ui_rect *r, int buf_w, int buf_h);

// The cell a point of the window lies in, or -1 outside the grid (a cell past the last tech counts). The grid starts 0x1d
// below the top.
int picktech_cell_at(const struct picktech_grid *g, int x, int y);

// What a key does to the cursor (the cell under the mouse), and the step it takes: the arrow keys and their keypad
// numbers, Tab and Backspace (right and left); Space, Return, 'H' and 'p' choose; any other key only looks a letter up.
enum picktech_key { PTK_NONE, PTK_LEFT, PTK_RIGHT, PTK_UP, PTK_DOWN, PTK_CHOOSE };
enum picktech_key picktech_key_action(int key);
// Moves a cursor given as a column and row one step, wrapping at the edges.
void picktech_key_step(int *col, int *row, enum picktech_key action, int cols, int rows);

// The sizes of the fonts of a list of `count` techs: the first three styles of the small print, and the fourth, which the
// hint lines at the foot of a card use.
void picktech_fonts(int count, int screen_w, int *size, int *hint_size);
// The size of the font of a card's own text.
int picktech_card_font(int count, int screen_w);

// How much smaller than the full picture a card's icon is drawn: a step (minus ten, minus twelve or minus fourteen)
// that depends on the screen and the width of a cell; the icon's size from that step or from the game's own scale (a
// numerator and a denominator, the numerator not above zero when it has none).
int picktech_icon_step(int screen_w, int cell_w);
int picktech_icon_size(int scale_num, int scale_den, int step);

// The random part of the weighing a tech gets as it is listed: a roll of 0 up to `roll_range(s) - 1` is added to
// `roll_base(s)`, where s is the tech's weighted sum of what it is good for. The values only keep the game's random
// numbers in step; the picker does not use them.
int picktech_roll_range(int s);
int picktech_roll_base(int s);

// Whether a tech is left out of a list when only some are offered (blind research): every third, counted from the
// faction's tech ranking, is left out, except the first techs for a faction that has no ranking yet.
int picktech_third_listed(int tech, int ranking, int faction);

// A heading that does not fit on one line and has no space to break at is cut where it fits and a space put there: `out`
// is `text`, or the part of it that fits (`width` of a string says how wide it is), a space, and the rest.
void picktech_break_line(char *out, const char *text, int limit, int (*width)(void *ctx, const char *s), void *ctx);
