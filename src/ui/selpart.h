// The design workshop's part picker (SelectPartWin): the grid of chassis, weapons, armor or reactors the workshop
// opens to choose a part. See selpart.c. The pure arithmetic is here, so a test can check it without the game.
#pragma once

#include <stdint.h>

#include "ui.h"

// The grid: parts lie in rows of `cols` cells, a page `rows` rows high, the first part shown being `first`.
// A cell is 108 apart across and 94 down, 91 high, and its drawn part starts 0x17 in from the cell's left and is
// 105 wide (the left margin is where the scroll bar's gutter lies).
enum { SELPART_COLS = 5, SELPART_ROWS = 4, SELPART_PAGE = 20, SELPART_PITCH_X = 108, SELPART_PITCH_Y = 94 };

// The rectangle of the cell for the part `slot` places after the first one shown (0 to page - 1).
void selpart_cell_rect(struct ui_rect *r, int slot, int cols);

// The part index under a point (x, y) of the window, or -1 outside the grid: the grid is the area from (4, 4)
// to 108 * cols + 4 across and 94 * rows + 4 down, which (as in the original) starts at the cell's own left edge
// rather than where its part is drawn.
int selpart_hit(int x, int y, int cols, int rows, int first);

// The key arrows. Left and Right move one place within the row of `sel` (the row's first place is its start, its
// last is the last part if the row is cut short), staying in the row.
int selpart_step_sideways(int sel, int delta, int cols, int count);
// Up: the part a row higher (a hover still beyond the page's end is taken up row by row until it is on the page).
int selpart_step_up(int sel, int first, int cols, int page);
// Down: the part a row lower (a hover above the page is taken down until it is on or after it).
int selpart_step_down(int sel, int first, int cols);
// End: the first part the page shows when it is moved to hold the last part, or `first` if every part fits.
int selpart_end_first(int first, int count, int cols, int page);

// The height of a line of text in a font of the given ascent (`first` is -1 while the font has no extra) and height.
int selpart_line_height(int first, int ascent, int height);
// Half of a sum, truncated toward zero (as the original centres a rectangle).
int selpart_centre(int a, int b);
// A number as text in `out`, as the original's itoa in base ten does (an unsigned byte or a signed one).
void selpart_number_text(char *out, int value);
