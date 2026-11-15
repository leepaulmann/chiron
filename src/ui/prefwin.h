// The Preferences window (PrefWin): the pages of options the menu's Preferences command opens. The pure parts (where each option
// lives in the game's three preference words, the order the boxes are asked and told their states in, the arrow keys, the
// labels of the volume buttons, the window's place and the boxes' heights) are kept apart so a test can check them without the
// game. See prefwin.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// The window has seven boxes of options (CheckBoxes), kept in this order in the object; a page of the group picks one of them
// (and the volume page also the four bars and four buttons). Each page is a tab: Basic, Advanced, the first of the base
// warnings, Automation, Audio/Visual, Map, and the second of the base warnings (which the More button of the first gives).
enum { PREFWIN_BOXES = 7, PREFWIN_PAGES = 7 };
enum { BOX_BASIC, BOX_ADVANCED, BOX_AUTOMATION, BOX_AUDIO, BOX_WARNINGS_A, BOX_WARNINGS_B, BOX_MAP };
int prefwin_box_of_page(int page);

// The options the window keeps are eleven words, each a copy of one of the game's three preference words (0x9a6490, 0x9a6494,
// 0x9a6498) with the window's changes in it. Which of the three a word is a copy of: A, B or C.
enum { PREFWIN_WORDS = 11 };
enum { PREF_WORD_A, PREF_WORD_B, PREF_WORD_C };
int prefwin_word_kind(int field);

// Where one option (a box item, by its id) lives: the field (0 to 10) of the eleven words that holds it, the bit, and whether
// the box shows the opposite (an item that is checked when the bit is clear). Answers 0 for an id that is no option.
struct prefwin_place {
    int field;
    uint32_t mask;
    int inverted;
};
int prefwin_place(int id, struct prefwin_place *p);

// The items of a box, by its place in the object, in the order the window makes them (the order they are listed in the box and
// in the script file; the answer is 0 past the last):
int prefwin_make_item(int box, int i);

// The items of a box, by its place in the object: in the order set_preferences asks the box for each one's state, and in the
// order get_preferences tells it. They are not the same order (and the first asks for ids the box does not have), and that
// matters: a box looks an item up by walking its ring, which leaves it where the walk ended. The answer is 0 past the last.
int prefwin_set_item(int box, int i);
int prefwin_get_item(int box, int i);

// What the eleven words come to: the three words the window publishes are the or of the fields of each kind. `out` has the
// three words as they stand (the window starts them at zero).
void prefwin_merge(const int32_t words[PREFWIN_WORDS], int32_t out[3]);

// The page an arrow key goes to: Up goes back, Down goes on, and both go round (the group of tabs has six pages).
int prefwin_step_page(int page, int up);

// The entry of the labels file that names a volume button (ids 0x55 to 0x58), turned on or off.
int prefwin_toggle_label(int id, int on);

// Where the window opens, from the screen's size and the window's width. An 800 pixel wide screen has the window at its top
// and the volume bars laid 0x32 higher, and the audio box 0x50 shorter.
struct prefwin_origin {
    int x, y;
    int small;
};
void prefwin_origin(struct prefwin_origin *o, int screen_w, int screen_h, int win_w);

// The height of each box, from the height of the area they lie in, the gap, what is left under the frame, and (for the audio
// box) what a small screen takes off.
int prefwin_box_height(int box, int area_h, int gap, int below, int small_loss);
