// The faction picker (PickWin): the screen config_game opens on the way into a new game. See pickwin.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// Where everything lies in the picker's window: each rectangle is in the window's client coordinates.
struct pickwin_layout {
    struct ui_rect frame;    // the whole window
    struct ui_rect header;   // the title bar
    struct ui_rect pack[3];  // the three pictures that choose a group of factions
    struct ui_rect list;     // the faction list
    struct ui_rect row[7];   // a small button beside each of the first seven list rows
    // The buttons, in the order the object keeps them (the enum below).
    struct ui_rect button[6];
    // What the original's working numbers (x, y, width, height) are left holding when it has laid the window out.
    int32_t last_x, last_y, last_w, last_h;
};
enum { PW_BTN_A, PW_BTN_B, PW_BTN_CANCEL, PW_BTN_CHOOSE, PW_BTN_RANDOM, PW_BTN_OK };

// The layout of a window of the given client size (the picker's is 790 by 300).
void pickwin_layout(struct pickwin_layout *l, int width, int height);

// A list line for a faction: "<formal name><separator><open><description><close>", put in `out` (which must hold
// all of it).
void pickwin_item_text(char *out, const char *formal_name, const char *description, const char *separator, const char *open,
                       const char *close);

// The y of list row `row` when row 0 is at `top`: the rows are 19 pixels apart.
int pickwin_row_y(int top, int row);
