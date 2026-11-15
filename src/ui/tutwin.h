// The tutorial's window (TutWin): the pure parts (where a note and its arrow go, the test of whether a note is due, the centre of
// a rectangle), kept apart so a test can check them without the game, and the arrow's redraw, which the info panel calls. See
// tutwin.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// Where a note goes. The tutorial's window is a popup that sits at the left or the right of the screen and, in the lower or the
// upper part of it, with an arrow drawn from it to what it is about. `mode` is how the note was asked for (0 a note, 1 a note
// that goes on to a tour, 2 to 5 notes placed to one side or the other and high or low, 6 a note whose place is the popup's own);
// `target_x` and `target_y` the point on the screen it is about, and `popup_w` and `popup_h` the popup's size.
struct tutwin_place {
    int x, y;            // the popup's top left corner
    int tail_x, tail_y;  // where the arrow leaves the popup: the middle of its side, on the edge that faces the point
};
void tutwin_place(struct tutwin_place *p, int mode, int screen_w, int screen_h, int target_x, int target_y, int popup_w,
                  int popup_h);

// Whether a tutorial note is due: never in a network game, never with the setting off (bit 0x20 of the preferences), never twice
// (`bit` is already among the `seen` ones), and never for the player whose faction is the aliens (rule flag 0x80). tut_check and
// tut_check2 are this test over their own two words of `seen`; when it holds they add `bit` to them.
int tutwin_note_due(int net_game, uint32_t preferences, uint32_t seen, uint32_t bit, uint32_t rule_flags);

// The middle of the span from `from` to `to`, the half taken toward zero (the original's own division).
int tutwin_middle(int from, int to);

// Draws the arrow of the note again (the note's window must be up; `tw` is the window object, game_tut_win).
void __attribute__((thiscall)) tutwin_draw_arrow(void *tw);
