// The info panel on the main screen (InfoWin): the two text boxes under the map that show the calendar, the
// economy, the leader, the ranking, the chart of the factions' strength and the planet, one at a time, a letter at
// a time. See infowin.c. The pure arithmetic is here, so a test can check it without the game.
#pragma once

#include <stdint.h>

#include "ui.h"

// What the original fills the two boxes' rectangles from: the game's area for the panel (left, top, right, bottom)
// is taken in 7 from the left, top and bottom and 0x27 from the right. On a screen 800 wide both boxes are that
// rectangle; on any other the area is split in two, the first half (the width less 3, halved toward zero) from the
// left and the second as far on as the first's width and 3 more.
void infowin_layout(struct ui_rect *first, struct ui_rect *second, const struct ui_rect *area, int screen_w);

// The height of a line of text in a font of the given first (-1 while the font has no extra), ascent and height.
int infowin_line_height(int first, int ascent, int height);

// What decides whether a mode can be shown: the game's turn counter (zero before play starts), the difficulty of
// the faction shown, whether the player has met anyone (the words at 0x93fab4), the flags byte at 0x9a64c0, and
// whether another faction has a pact (bit 8) with the one shown.
struct infowin_facts {
    int turn;
    int difficulty;
    int contact;
    int flags;
    int pact;
};
// Whether a mode (0 to 11 ask; the panel only ever shows 0 to 7) can be shown now.
int infowin_mode_valid(int mode, const struct infowin_facts *facts);

// The mode after `mode`, wrapping at eight, as the original's remainder does (a mode of -1 goes to 0).
int infowin_next_mode(int mode);

// What one tick of the panel's clock does with a box whose text has `length` characters and which has been
// scrolled `scroll` characters into it: draw it again, ask whether to hold or move on, or nothing.
enum infowin_tick { INFOWIN_TICK_NOTHING, INFOWIN_TICK_DRAW, INFOWIN_TICK_HOLD };
enum infowin_tick infowin_tick_action(int scroll, int length);

// How a line is drawn when `scroll` characters of the box's text are left to show and the line is `length` long:
// in full, not at all (nothing left), or part way (the first scroll - 1 characters, then the one at the front of the
// scroll in the highlight colour).
enum infowin_line { INFOWIN_LINE_FULL, INFOWIN_LINE_PARTIAL, INFOWIN_LINE_NONE };
enum infowin_line infowin_line_action(int scroll, int length);

// The graph's title is the label in capitals when the game has fewer than 0x2328 of either of the first two counts
// and the third is zero, else the plain label.
int infowin_title_upper(int a, int b, int c);

// The ranking panel's bars: the faction's value drawn `value * span / most` high (at least 1), and where the seven
// bars lie across the rectangle: each `width / 7` wide, the run centred (the leftover halved toward zero).
int infowin_bar_height(int value, int span, int most);
void infowin_bar_run(int left, int right, int *first, int *width);

// The history chart: how many turns to skip between points (one, then more while the turns outnumber the columns of
// the rectangle's width), and by how much a height is divided to fit in the rectangle (one, or the smallest whole
// number of heights that reaches the greatest value).
int infowin_chart_step(int turns, int width);
int infowin_chart_scale(int height, int most);

// The planet line's label: which of the game's names it is for a given state (0 to 5 are the labels 0xebc to 0xed0
// in order, 6 to 9 the words at 0x94adf4, 0x94ae14, 0x94adfc and 0x94ae34, and -1 a name from the table at 0x691878,
// `(state - 6) * 32` in).
int infowin_planet_name(int state);
int infowin_planet_table_offset(int state);
