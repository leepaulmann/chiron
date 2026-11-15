// The Social Engineering window (SocialWin): the pure parts (where the panels and buttons go, how the energy allocation gives and
// takes, which model a "maximize" picks, how the numbers are put into text), kept apart so a test can check them without the game.
// See socialwin.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// The window keeps 50 rectangles from 0x1efd0 on, in this order: where each part of the screen goes. Most lie on the main screen's
// own panels (the three alloc panels and the text over them, the overview), a few on the window.
enum {
    SW_R_LOCK = 0,        // 3: the lock check boxes, under the economy, psych and labs panels' left
    SW_R_ALLOC_BTN = 3,   // 6: the + and - buttons: economy, economy, labs, labs, psych, psych (ids 0x1d to 0x22)
    SW_R_PRESETS = 9,     // the frame on the right the window draws first
    SW_R_EFFECT = 10,     // 10: the buttons of the effects (ids 0x11 to 0x1a)
    SW_R_PANEL = 20,      // 4: the category panels, one above the other
    SW_R_MODEL = 24,      // 16: the model buttons, four to a category
    SW_R_OVERVIEW = 40,   // the effects' list, on the main screen
    SW_R_ECONOMY = 41,    // the three bars: economy, labs, psych (the order the buttons use)
    SW_R_LABS = 42,
    SW_R_PSYCH = 43,
    SW_R_HOVER = 44,      // what the model under the mouse does, on the main screen
    SW_R_BASAL = 45,      // the faction's own effects
    SW_R_CANCEL = 46,     // the three buttons along the bottom
    SW_R_RESET = 47,
    SW_R_OK = 48,
    SW_R_INFO = 49,       // the energy reserves and the upheaval's cost
    SW_RECTS = 50
};

// Works the 50 rectangles out from the main screen's four panels (the 16 words the game keeps from 0x7af5a0: left, top, right and
// bottom of each, in order). The window's own rectangles are fixed; the others hang from the panels.
void socialwin_layout(struct ui_rect r[SW_RECTS], const int32_t panels[16]);

// Which of the ten effect buttons (0 to 9) an effect (0 to 10) has, or -1 for the effect that has none (effect 3, Talent).
int socialwin_effect_button(int effect);
// The effect a button's slot (0 to 9) stands for.
int socialwin_button_effect(int slot);

// The allocation of the 10 tenths of energy among economy, psych and labs after the player sets one share: `alloc` [3] (economy,
// psych, labs) with `alloc[index]` taken as `value`, then given out (when under 10) or taken back (over 10), one tenth at a time
// to the next two (going round) that aren't `locked` and have room (below `limit`, or above 0 when taking), and at last to or from
// the one the player set.
void socialwin_allocate(int32_t alloc[3], const int32_t locked[3], int index, int value, int limit);

// The model a "maximize" of an effect picks in a category: the available one whose effect is highest and above 0 (the first of
// equals), or 0. `effects` are the four models' values.
int socialwin_best_model(const int32_t effects[4], const int avail[4]);

// The text of an effect's value with a plus before a positive one: "+2", "0", "-1".
void socialwin_signed(char *out, int value);
// The same for the mouse-over list, which draws the sign of the value and then its size: "+2", "-1" (never 0).
void socialwin_signed_abs(char *out, int value);

// What the loss from a skewed allocation comes to. `half` is half the gap between economy and labs (|psych + 2 labs - 10| / 2),
// `unit` twice the shortfall of EFFICIENCY from 4 (held between -4 and 4), and the loss in tenths of energy is held to `top`.
int socialwin_loss_half(int psych, int labs);
int socialwin_loss_unit(int efficiency);
int socialwin_clamp(int value, int low, int high);

// Where the energy window's three lines of text go (all measured from the panel, `line` the font's line height): the first line's
// y, and where the third goes (to the right of the first, at the same y, on a screen wider than 800; under the second else).
struct socialwin_info_lines {
    int y1, x3, y3;
};
void socialwin_info_lines(struct socialwin_info_lines *l, int screen_w, int left, int right, int top, int bottom, int line);

// A model button's icons: how many of the effects are drawn (non-zero, not cancelled: `hidden`), and how many of them are big (more
// than 1 in size), and where the first icon goes across a box from `left` to `right` (the rest halved toward zero).
void socialwin_icon_counts(const int32_t values[11], const int hidden[11], int *drawn, int *big);
int socialwin_icons_x(int left, int right, int drawn, int icon_w, int big, int big_w);
// The index of the big picture of a value (2 and over, or -2 and under): two to a size, the positive first.
int socialwin_big_index(int value);
// The index of the small picture of an effect: nine to a row, the row counting only the ten effects that have one.
int socialwin_small_index(int effect, int value);
// The effect a row of the overview is for: the hovered button's id (0x11 to 0x1a) less 0x11, and 1 more from the third on.
int socialwin_hover_effect(int id);
