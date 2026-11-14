// The production picker (ProdPicker): the list of things a base can build, in the base screen. See prodpicker.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// How the choices are laid out: `per_row` cells a row and `rows` rows shown at once (`visible` cells), each `cell_w` by
// `cell_h`. The window is `win_w` wide; the cells start 0x17 from its left edge and the first ones are 4 down.
struct prodpicker_grid {
    int per_row, rows, visible;
    int cell_w, cell_h;
};

// The grid for a window of the given width on a screen of the given width: cells a fifth of the window less 0x51 wide (5 of
// them fit, but a window that comes out odd may fit another), 3 rows on a screen of 800 pixels or less and 4 on a wider one.
void prodpicker_grid(struct prodpicker_grid *g, int win_w, int screen_w, int cell_h);

// The rectangle of the cell `slot` counts from the first one shown (left, top, right, bottom), which the cell's frame is drawn
// in. The cell's picture and text lie inside it, a pixel in.
void prodpicker_cell_rect(struct ui_rect *r, const struct prodpicker_grid *g, int slot);

// How far into the cells shown a click at (x, y) of the window lies: the grid's own origin is not taken off, as the original does.
int prodpicker_click_slot(const struct prodpicker_grid *g, int x, int y);

// The sizes of the two fonts that grow with the cell: the choice's name and the line under it (the third, for the cost, is the
// same with a lower limit).
void prodpicker_font_sizes(int cell_w, int *name_size, int *cost_size);

// The kinds a choice is sorted into (and which the colour of its frame comes from) for a unit design and for a facility: a
// design that is not built and is a custom one by its number is 1, otherwise its plan gives 0, 2 or 3.
int prodpicker_unit_kind(int proto, int flags, int plan);
int prodpicker_facility_kind(int fac);

// The turns a thing takes to build at a surplus of minerals: the cost left, over the surplus, rounded up, between 0 and a limit.
int prodpicker_turns(int surplus, int remaining, int limit);

// The minerals the base already has that count towards a different kind of thing: all of them, or (past the game's own
// threshold) the part of them left after the game's own percent (`threshold` is the amount that is kept whole).
int prodpicker_retool_stock(int stock, int threshold, int percent);

// How many buttons are in the row at the foot (the base's own: 4 or 5 of them for a faction a human plays; 6 asking for a
// unit or a facility), and where the row starts and each button after it. The buttons are `width` wide with 3 between and
// the row is centred on a window `win_w` wide less 0x20.
int prodpicker_button_count(int mode, int human, int state, uint32_t governor);
int prodpicker_row_start(int win_w, int width, int count);
int prodpicker_button_step(int width);

// What a key does: the page and cursor keys (and the keypad's 2, 4, 6 and 8) move the cell chosen or the view.
enum prodpicker_key { PPK_NONE, PPK_PGUP, PPK_PGDN, PPK_END, PPK_HOME, PPK_LEFT, PPK_UP, PPK_RIGHT, PPK_DOWN };
enum prodpicker_key prodpicker_key_action(int key);

// A cell one to the left or right of the one chosen, kept in its row (the last row may be short, `total` is how many there are).
int prodpicker_step_sideways(int sel, int dir, int per_row, int total);

// The pictures' sizes: a unit's is two thirds of the cell's height (and 75 percent of that high for its model), a facility's three
// sevenths. Where a picture w by h goes: centred in the cell's rectangle (each half cut towards zero).
int prodpicker_unit_art(int cell_h);
int prodpicker_unit_model(int cell_h);
int prodpicker_facility_art(int cell_h);
void prodpicker_art_origin(const struct ui_rect *cell, int w, int h, int *x, int *y);

// Whether the menu a right click opens offers to hide the design: a design numbered below 0x40 is not offered when it is number 15
// or its weapon is psi (an offence below zero); the others are, and a facility (-1) never is.
int prodpicker_menu_offers_hide(int proto, int weapon_offense);
