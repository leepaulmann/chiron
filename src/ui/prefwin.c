// The Preferences window, the game's PrefWin: the menu's Game, Preferences command opens it. It is one global object (0x8578d8)
// built by a static initializer and destroyed by an atexit handler. Outside the class, only the menu's six commands touch it
// (each calls display with the page to open on), and the game's startup calls init. The window's numbers (where each area lies)
// are statics next to the object (0x8577d8 to 0x8578d0 and a few after it), worked out by the game's own static initializers
// from numbers in its data; this code reads them by address, and init writes two of them on an 800 pixel screen.
//
// What the window shows: a column of tabs down its left (a group of buttons: Basic, Advanced, Base Warnings, Automation,
// Audio/Visual and Map, and a seventh tab, the Gamma button, that is built and shown but chosen by no group), and, to their
// right, the page the tab chose. A page is a box of check items (CheckBox) read from the game's script file (its basic,
// advanced, automation, audio/visual, base warnings (two boxes) and map sections: a line an item, a space in front of each),
// except the volume page, which has four sliders (Main, Voice, SFX and Music volume) and four buttons (Sound, Music,
// Voice... turned on and off) beside its box, and the Base Warnings page, whose More button swaps its box for a second one.
// Defaults, Cancel and OK stand under the tabs. Return and Escape click OK and Cancel; Up and Down change the page.
//
// What the window writes: the game keeps its preferences in three words (0x9a6490, 0x9a6494 and 0x9a6498). When the window
// opens each of its eleven option words (0xa30 to 0xa58) is a copy of the word it is of, the boxes are set from them
// (get_preferences), and when OK is clicked the boxes and the sound buttons are read back into them (set_preferences), the
// results merged into the three words the window hands the game (0x94b464 to 0x94b46c), and the game's own functions apply them
// and save the preferences file. The sliders' positions are saved in the preferences file, one name for each. The volume buttons and
// sliders act at once (the sound object, at 0x90d978, is told); Cancel undoes none of it.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=prefwin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. Static initializers run after hooks_apply, so the constructor
// and destructor are ours too.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the destructor calls the boxes',
// bars' and buttons' own destructors where the original has some of them written out inline; the Gamma button's number dialog
// is the original's own object, made and unmade by the game's functions (in a buffer of 0x5200 bytes, a little more than the
// original's stack frame holds it in) on every click of a button, as the original does, though only the Gamma button uses it.
// redraw_option and redraw_volume, which nothing in the game calls, are kept (only the scene host's open step reaches them), and so
// are the 800 pixel screen's numbers (nothing in the scenes has such a screen).
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "prefwin.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/dialog.h"
#include "../engine/game.h"
#include "../engine/palette.h"
#include "../engine/scroll.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ---------------------------------------------------------------------------------------------

// The tab a page of the group is shown by, and the box that tab shows: the pages go Basic, Advanced, the base warnings,
// Automation, Audio/Visual, Map and the base warnings' second box, while the boxes lie in the object in the order the
// script file gives them.
int prefwin_box_of_page(int page) {
    switch (page) {
    case 0: return BOX_BASIC;
    case 1: return BOX_ADVANCED;
    case 2: return BOX_WARNINGS_A;
    case 3: return BOX_AUTOMATION;
    case 4: return BOX_AUDIO;
    case 5: return BOX_MAP;
    case 6: return BOX_WARNINGS_B;
    default: return -1;
    }
}

// The eleven words (the fields at 0xa30 to 0xa58, in the order of their offsets) are copies of the game's words: A for the ones
// at 0xa30, 0xa38, 0xa40, 0xa4c and 0xa54, B for 0xa34, 0xa3c, 0xa44, 0xa50 and 0xa58, and C for 0xa48.
int prefwin_word_kind(int field) {
    switch (field) {
    case 0: case 2: case 4: case 7: case 9: return PREF_WORD_A;
    case 6: return PREF_WORD_C;
    default: return PREF_WORD_B;
    }
}

int prefwin_place(int id, struct prefwin_place *p) {
    struct prefwin_place none = {0, 0, 0};
    *p = none;
#define PLACE(field_, mask_, inverted_) \
    do {                                \
        p->field = (field_), p->mask = (mask_), p->inverted = (inverted_); \
        return 1;                       \
    } while (0)
    switch (id) {
    // Basic
    case 0x07: PLACE(0, 0x20, 0);
    case 0x08: PLACE(0, 0x4000, 0);
    case 0x09: PLACE(1, 0x100000, 0);
    case 0x0a: PLACE(0, 0x1000, 0);
    case 0x0b: PLACE(0, 0x2, 0);
    case 0x0c: PLACE(0, 0x1, 0);
    case 0x0d: PLACE(0, 0x4, 1);
    case 0x0e: PLACE(0, 0x8000, 1);
    // Advanced
    case 0x0f: PLACE(2, 0x20000000, 1);
    case 0x10: PLACE(3, 0x40000, 0);
    case 0x11: PLACE(3, 0x8000, 0);
    case 0x12: PLACE(3, 0x10, 0);
    case 0x13: PLACE(2, 0x8, 0);
    case 0x14: PLACE(3, 0x80, 0);
    case 0x15: PLACE(2, 0x80000, 0);
    case 0x17: PLACE(3, 0x4, 0);
    case 0x18: PLACE(3, 0x200000, 0);
    case 0x19: PLACE(3, 0x400, 0);
    case 0x1a: PLACE(3, 0x100, 0);
    case 0x1c: PLACE(3, 0x40, 0);
    // Automation
    case 0x1d: PLACE(4, 0x10000, 0);
    case 0x1e: PLACE(4, 0x1000000, 0);
    case 0x1f: PLACE(4, 0x20000, 0);
    case 0x20: PLACE(4, 0x2000000, 0);
    case 0x21: PLACE(5, 0x8, 0);
    case 0x22: PLACE(5, 0x20, 0);
    case 0x23: PLACE(4, 0x100000, 0);
    case 0x24: PLACE(4, 0x200000, 0);
    case 0x25: PLACE(4, 0x400000, 0);
    case 0x26: PLACE(4, 0x800000, 0);
    case 0x27: PLACE(4, 0x40000000, 0);
    case 0x28: PLACE(4, 0x80000000u, 0);
    case 0x29: PLACE(5, 0x800, 0);
    case 0x2a: PLACE(5, 0x400000, 1);
    // Base warnings and the second page of them (word C)
    case 0x2b: PLACE(6, 0x1, 0);
    case 0x2c: PLACE(6, 0x2, 0);
    case 0x2d: PLACE(6, 0x400, 0);
    case 0x2e: PLACE(6, 0x4, 0);
    case 0x2f: PLACE(6, 0x2000, 0);
    case 0x30: PLACE(6, 0x8, 0);
    case 0x31: PLACE(6, 0x10, 0);
    case 0x32: PLACE(6, 0x20, 0);
    case 0x33: PLACE(6, 0x40, 0);
    case 0x34: PLACE(6, 0x80, 0);
    case 0x35: PLACE(6, 0x100, 0);
    case 0x36: PLACE(6, 0x200, 0);
    case 0x37: PLACE(6, 0x800, 0);
    case 0x38: PLACE(6, 0x1000, 0);
    case 0x39: PLACE(6, 0x4000, 0);
    case 0x3a: PLACE(6, 0x8000, 0);
    case 0x3b: PLACE(6, 0x10000, 0);
    case 0x3c: PLACE(6, 0x20000, 0);
    // Audio/Visual: the first three are the buttons' (Sound, Music and Voice), which the window keeps in buttons, not in items
    case 0x3d: PLACE(7, 0x800, 0);
    case 0x3e: PLACE(7, 0x400, 0);
    case 0x3f: PLACE(8, 0x4000, 0);
    case 0x40: PLACE(8, 0x20000, 0);
    case 0x41: PLACE(7, 0x10000000, 0);
    case 0x42: PLACE(7, 0x40000, 1);
    case 0x43: PLACE(7, 0x80, 0);
    case 0x44: PLACE(7, 0x8000000, 0);
    case 0x45: PLACE(8, 0x80000, 0);
    case 0x16: PLACE(8, 0x200, 0);
    case 0x47: PLACE(8, 0x1000000, 1);
    // Map
    case 0x48: PLACE(10, 0x1, 0);
    case 0x49: PLACE(9, 0x100, 0);
    case 0x4a: PLACE(9, 0x200, 0);
    case 0x4b: PLACE(10, 0x2000, 0);
    case 0x4c: PLACE(10, 0x1000, 0);
    case 0x4d: PLACE(10, 0x10000, 0);
    case 0x4e: PLACE(10, 0x800000, 0);
    case 0x4f: PLACE(10, 0x2000000, 1);
    default: return 0;
    }
#undef PLACE
}

// The items of each box in the order the window makes them (the order they are listed down the box): the script file has a line
// for each, in this order.
int prefwin_make_item(int box, int i) {
    switch (box) {
    case BOX_AUTOMATION:
        switch (i) { case 0: return 0x1d; case 1: return 0x1e; case 2: return 0x1f; case 3: return 0x20; case 4: return 0x21;
                     case 5: return 0x22; case 6: return 0x2a; case 7: return 0x23; case 8: return 0x24; case 9: return 0x25;
                     case 10: return 0x26; case 11: return 0x27; case 12: return 0x28; case 13: return 0x29; }
        return 0;
    case BOX_AUDIO:
        switch (i) { case 0: return 0x40; case 1: return 0x41; case 2: return 0x42; case 3: return 0x47; case 4: return 0x43;
                     case 5: return 0x44; case 6: return 0x45; case 7: return 0x16; }
        return 0;
    case BOX_WARNINGS_A:
        if (i < 0 || i >= 13) return 0;
        return i < 10 ? 0x2b + i : 0x36 + (i - 10);
    case BOX_MAP:
        switch (i) { case 0: return 0x48; case 1: return 0x49; case 2: return 0x4e; case 3: return 0x4a; case 4: return 0x4b;
                     case 5: return 0x4c; case 6: return 0x4d; case 7: return 0x4f; }
        return 0;
    default:
        return prefwin_set_item(box, i);  // (Basic, Advanced and the second page of base warnings are made in the order they are asked)
    }
}

// The items of each box in the order set_preferences asks for their states. The first three of the audio box are no items
// of it (the window keeps those in buttons), but it is asked all the same, and the walk that finds nothing leaves the box
// where it ended.
int prefwin_set_item(int box, int i) {
    switch (box) {
    case BOX_BASIC:
        switch (i) { case 0: return 0x07; case 1: return 0x08; case 2: return 0x09; case 3: return 0x0a; case 4: return 0x0b;
                     case 5: return 0x0c; case 6: return 0x0d; case 7: return 0x0e; }
        return 0;
    case BOX_ADVANCED:
        switch (i) { case 0: return 0x0f; case 1: return 0x10; case 2: return 0x11; case 3: return 0x12; case 4: return 0x13;
                     case 5: return 0x1c; case 6: return 0x14; case 7: return 0x15; case 8: return 0x17; case 9: return 0x18;
                     case 10: return 0x19; case 11: return 0x1a; }
        return 0;
    case BOX_AUTOMATION:
        switch (i) { case 0: return 0x1d; case 1: return 0x1e; case 2: return 0x1f; case 3: return 0x20; case 4: return 0x2a;
                     case 5: return 0x21; case 6: return 0x22; case 7: return 0x23; case 8: return 0x24; case 9: return 0x25;
                     case 10: return 0x26; case 11: return 0x27; case 12: return 0x28; case 13: return 0x29; }
        return 0;
    case BOX_AUDIO:
        switch (i) { case 0: return 0x3d; case 1: return 0x3e; case 2: return 0x3f; case 3: return 0x40; case 4: return 0x41;
                     case 5: return 0x42; case 6: return 0x47; case 7: return 0x43; case 8: return 0x44; case 9: return 0x45;
                     case 10: return 0x16; }
        return 0;
    case BOX_WARNINGS_A:
        return i >= 0 && i < 14 ? 0x2b + i : 0;
    case BOX_WARNINGS_B:
        return i >= 0 && i < 4 ? 0x39 + i : 0;
    case BOX_MAP:
        return i >= 0 && i < 8 ? 0x48 + i : 0;
    default:
        return 0;
    }
}

// ... and in the order get_preferences tells them.
int prefwin_get_item(int box, int i) {
    switch (box) {
    case BOX_BASIC:
        switch (i) { case 0: return 0x07; case 1: return 0x08; case 2: return 0x0a; case 3: return 0x0b; case 4: return 0x0c;
                     case 5: return 0x0d; case 6: return 0x0e; case 7: return 0x09; }
        return 0;
    case BOX_ADVANCED:
        switch (i) { case 0: return 0x0f; case 1: return 0x10; case 2: return 0x11; case 3: return 0x12; case 4: return 0x13;
                     case 5: return 0x1c; case 6: return 0x14; case 7: return 0x15; case 8: return 0x18; case 9: return 0x17;
                     case 10: return 0x19; case 11: return 0x1a; }
        return 0;
    case BOX_AUTOMATION:
        return i >= 0 && i < 14 ? 0x1d + i : 0;
    case BOX_AUDIO:
        switch (i) { case 0: return 0x3d; case 1: return 0x3e; case 2: return 0x3f; case 3: return 0x40; case 4: return 0x41;
                     case 5: return 0x42; case 6: return 0x43; case 7: return 0x44; case 8: return 0x45; case 9: return 0x16;
                     case 10: return 0x47; }
        return 0;
    case BOX_WARNINGS_A:
        if (i < 0 || i >= 13) return 0;
        return i < 10 ? 0x2b + i : 0x36 + (i - 10);  // (there is no 0x35 among the items)
    case BOX_WARNINGS_B:
        return i >= 0 && i < 4 ? 0x39 + i : 0;
    case BOX_MAP:
        return i >= 0 && i < 8 ? 0x48 + i : 0;
    default:
        return 0;
    }
}

void prefwin_merge(const int32_t words[PREFWIN_WORDS], int32_t out[3]) {
    for (int f = 0; f < PREFWIN_WORDS; f++) out[prefwin_word_kind(f)] |= words[f];
}

int prefwin_step_page(int page, int up) {
    if (up) return page > 0 ? page - 1 : 5;
    return page >= 5 ? 0 : page + 1;
}

int prefwin_toggle_label(int id, int on) {
    switch (id) {
    case 0x55: return on ? 0x439 : 0x438;
    case 0x56: return on ? 0x43d : 0x43c;
    case 0x57: return on ? 0x43b : 0x43a;
    case 0x58: return on ? 0x43f : 0x43e;
    default: return -1;
    }
}

void prefwin_origin(struct prefwin_origin *o, int screen_w, int screen_h, int win_w) {
    o->small = screen_w == 0x320;
    o->x = screen_w / 2 - win_w / 2;
    o->y = o->small ? 0 : screen_h - 0x26a;
}

int prefwin_box_height(int box, int area_h, int gap, int below, int small_loss) {
    switch (box) {
    case BOX_AUDIO: return area_h - gap - below - small_loss;
    case BOX_WARNINGS_A: return area_h - gap - below + 0x28;
    case BOX_WARNINGS_B: return area_h - gap - below;
    default: return area_h;
    }
}

// ---- The object ---------------------------------------------------------------------------------------------

enum { PREFWIN_BASE = 0x8578d8 };
enum { BOX_SIZE = 0xb28, BAR_SIZE = 0x214c, BARS = 5, VOLUME_BARS = 4, BUTTONS = 9, FONTS = 7 };

// The ids of the sliders and buttons (the tabs have 0 to 6; the group's six are 0 to 5).
enum {
    ID_GAMMA = 6,  // the seventh tab
    ID_BAR_MAIN = 0x50,
    ID_BAR_VOICE,
    ID_BAR_SFX,
    ID_BAR_MUSIC,
    ID_SOUND = 0x55,  // the buttons beside the sliders: all the sound, SFX, the voices and the music
    ID_SFX,
    ID_VOICE,
    ID_MUSIC,
    ID_DEFAULTS = 0x5a,
    ID_CANCEL,
    ID_OK,
    ID_MORE = 0x5e,
};
// The place of each of the nine FlatButtons that follow the sliders (btn[]): the sound buttons are in the order of their ids.
enum { B_SOUND, B_SFX, B_VOICE, B_MUSIC, B_SPARE, B_DEFAULTS, B_CANCEL, B_OK, B_MORE };

struct game_prefwin {
    struct game_win win;     // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t page;            // 0xa14: the page shown (0 to 6)
    int32_t last_page;       // 0xa18: the one before it
    int32_t by_key;          // 0xa1c: set while an arrow key has changed the page (the group's click then keeps last_page)
    int32_t out[3];          // 0xa20: the three words handed to the game, built when OK is clicked (the window starts them at zero)
    int32_t unused_a2c;      // 0xa2c: nothing reads or writes it
    int32_t opt[PREFWIN_WORDS];  // 0xa30: the eleven option words (see prefwin_word_kind)
    int32_t accepted;        // 0xa5c: OK was clicked: close saves
    struct game_flat_button tab[PREFWIN_PAGES];  // 0xa60: the tabs, ids 0 to 6
    uint8_t box[PREFWIN_BOXES][BOX_SIZE];        // 0x5974: the CheckBoxes (see the BOX_ names)
    uint8_t bar[BARS][BAR_SIZE];                 // 0xa78c: five Scrolls, of which the last is made and not used
    struct game_flat_button btn[BUTTONS];        // 0x14e08: the sound buttons, a spare, Defaults, Cancel, OK, More
    struct font *font[FONTS];                    // 0x1b3b4: the fonts the window looks up (10, 12 and 14 points in three styles)
};
GAME_AT(struct game_prefwin, page, 0xa14);
GAME_AT(struct game_prefwin, last_page, 0xa18);
GAME_AT(struct game_prefwin, by_key, 0xa1c);
GAME_AT(struct game_prefwin, out, 0xa20);
GAME_AT(struct game_prefwin, opt, 0xa30);
GAME_AT(struct game_prefwin, accepted, 0xa5c);
GAME_AT(struct game_prefwin, tab, 0xa60);
GAME_AT(struct game_prefwin, box, 0x5974);
GAME_AT(struct game_prefwin, bar, 0xa78c);
GAME_AT(struct game_prefwin, btn, 0x14e08);
GAME_AT(struct game_prefwin, font, 0x1b3b4);
_Static_assert(sizeof(struct game_prefwin) == 0x1b3d0, "layout of the game's PrefWin");

// The slots of the window's vtable the screen uses (byte offsets).
enum {
    VT_SHOW = 0x4,
    VT_HIDE = 0x8,
    VT_REDRAW = 0xf8,
    VT_BUTTON_CLICKED = 0xb0,
    VT_SCROLLED = 0xc4,
    VT_SET_MODAL = 0xe4,
    VT_RELEASE_MODAL = 0xe8,
    VT_DIALOG_ATTACH = 0x4,  // (of a box's Dialog)
};
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66d424, VTABLE_BUF = 0x66d41c };


// ---- The game's data it reads (by address) --------------------------------------------------------------------

enum {
    // The statics next to the object, which the game's initializers made from the numbers at 0x686b6c and after: where each
    // area of the window lies. The sound area's frame (a left, a top, a width and a height) under the volume page's box:
    S_VOL_X = 0x8577d8,
    S_VOL_Y = 0x8577dc,
    S_VOL_H = 0x8577e0,
    S_VOL_W = 0x8577e4,
    S_GROUP = 0x8577f0,  // the ButtonGroup of the tabs
    S_MARGIN_Y = 0x857884,
    S_COLUMN_X = 0x857888,  // the frame round the tab column: its left, and (a game number) its width
    S_COLUMN_W = 0x686b84,
    // The column of tabs, and below it Defaults, Cancel, OK, and the More button's own place:
    S_TAB_X = 0x85788c,
    S_TAB_Y = 0x857890,
    S_TAB_W = 0x857894,
    // The frame round the options' area (the page frame): its top, its left and its width, and the area's width and height:
    S_PAGE_Y = 0x857898,
    S_PAGE_X = 0x85789c,
    S_AREA_W = 0x8578a0,
    S_AREA_H = 0x8578a4,
    S_PAGE_W = 0x8578a8,
    // The sound buttons, the sliders, the boxes:
    S_TOGGLE_X = 0x8578b4,
    S_BAR_W = 0x8578b8,
    S_TOGGLE_Y = 0x8578bc,
    S_BOTTOM = 0x8578c0,
    S_BOX_Y = 0x8578c4,
    S_BOX_X = 0x8578c8,
    S_BAR_X = 0x8578cc,
    S_BAR_Y = 0x8578d0,
    // The game's own numbers for the layout (data), and what a small screen shifts by (0 until init changes it):
    S_GAP = 0x686b78,
    S_WIN_W = 0x686b7c,
    S_WIN_H = 0x686b80,
    S_TAB_H = 0x686b88,
    S_TOGGLE_W = 0x686b8c,
    S_TOGGLE_H = 0x686b90,
    S_SHIFT = 0x872ca8,
    S_FONT_A = 0x872c90,  // two Font pointers the window's Buffer is given (nothing ever sets them)
    S_FONT_B = 0x872c9c,
    S_TITLE = 0x872cac,   // the (empty) title the window is made with
    SCREEN_WIDTH = 0x9b7b1c,
    SCREEN_HEIGHT = 0x9b7b20,
    CONSOLE = 0x9156b0,
    TEXT_FILE = 0x691b0c,        // char *: the script file's name
    TEXT_COLOR = 0x68a5a4,       // (two words, and two more 0xc on)
    FONT_BUTTONS = 0x6e84b4,     // a Font object, for the buttons
    CHECK_PICTURE_A = 0x779504,  // the two Sprites the check items are drawn with
    CHECK_PICTURE_B = 0x779530,
    FRAME_WINDOW = 0x78d690,     // box frames: the window's, and the options' area's
    FRAME_AREA = 0x78d7f8,
    PREFS_A = 0x9a6490,  // the game's three preference words
    PREFS_B = 0x9a6494,
    PREFS_C = 0x9a6498,
    PREFS_OUT = 0x94b464,  // the three the window hands over
    SOUND = 0x90d978,      // the sound object
    VOICE = 0x74daa0,      // a pointer to the music object, or 0
    VOICE_ARG = 0x939284,
    FLAGS_UI = 0x90d91c,
    FLAGS_MIRROR = 0x6970dc,
    WINDOW_OPENING = 0x9380dc,
    NUM_PALETTE = 0x94c590,  // the palette the Gamma dialog is given, and the one it is first copied from
    NUM_PALETTE_FROM = 0x9523a0,
    GAMMA = 0x6846a8,    // a float: the Gamma dialog's number
    HUNDRED = 0x66d58c,  // a float: 100
};
// Strings.
enum {
    SECTION_BASIC = 0x686b98,
    SECTION_ADVANCED = 0x686bc4,
    SECTION_AUTOMATION = 0x686c04,
    SECTION_AUDIO = 0x686c4c,
    SECTION_WARNINGS = 0x686c80,
    SECTION_MAP = 0x686cd4,
    ITEM_PREFIX = 0x686ba4,  // a space (the game has a copy at each place it is used; they are all the same)
    NAME_MAIN = 0x686d00,    // the preferences' names, read ...
    NAME_VOICE = 0x686d0c,
    NAME_SFX = 0x686d1c,
    NAME_MUSIC = 0x686d28,
    SAVE_MAIN = 0x686d38,  // ... and written (the same words)
    SAVE_VOICE = 0x686d44,
    SAVE_MUSIC = 0x686d54,
    SAVE_SFX = 0x686d64,
    SAVE_GAMMA = 0x686d70,
};
// Entries of the labels file.
enum {
    LABEL_TAB = 0x2ee,      // the first five tabs' labels
    LABEL_TAB_MAP = 0x435,  // the sixth and the seventh
    LABEL_TAB_GAMMA = 0x436,
    LABEL_DEFAULTS = 0x437,
    LABEL_OK = 0x65,
    LABEL_CANCEL = 0x66,
    LABEL_MORE = 0x440,
    LABEL_FEWER = 0x298,
};

static char *text(uint32_t addr) { return game_at(addr); }
static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static void put_word(uint32_t addr, int32_t v) { *(int32_t *)game_at(addr) = v; }
static struct font *button_font(void) { return game_at(FONT_BUTTONS); }
static const struct game_box_frame *frame_at(uint32_t addr) { return game_at(addr); }

// ---- Parts --------------------------------------------------------------------------------------------------

static struct game_win *button_win(struct game_flat_button *b) { return &b->base.win; }

// A CheckBox: its Win and its Dialog lie where its vbtable says.
static struct game_win *box_win(struct game_prefwin *pw, int box) {
    uint8_t *o = pw->box[box];
    return (struct game_win *)(o + ((int32_t *)*(void **)o)[1]);
}
static struct game_dlg *box_dlg(struct game_prefwin *pw, int box) {
    uint8_t *o = pw->box[box];
    return (struct game_dlg *)(o + ((int32_t *)*(void **)o)[2]);
}
static struct game_scroll *bar_at(struct game_prefwin *pw, int i) { return (struct game_scroll *)pw->bar[i]; }
static struct game_button_group *group(void) { return game_at(S_GROUP); }

static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, 0); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(struct game_prefwin *pw) { UI_VCALL(void(UI_THIS *)(void *), pw, VT_RELEASE_MODAL); }

// ---- Construction -------------------------------------------------------------------------------------------

static void *UI_THIS prefwin_ctor(struct game_prefwin *pw) {
    graphic_win_ctor(&pw->win);
    for (int i = 0; i < PREFWIN_PAGES; i++) flat_button_ctor(&pw->tab[i]);
    for (int i = 0; i < PREFWIN_BOXES; i++) checkbox_ctor(pw->box[i], 1);
    for (int i = 0; i < BARS; i++) scroll_ctor(bar_at(pw, i));
    for (int i = 0; i < BUTTONS; i++) flat_button_ctor(&pw->btn[i]);
    pw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return pw;
}

static void UI_THIS prefwin_dtor(struct game_prefwin *pw) {
    for (int i = BUTTONS - 1; i >= 0; i--) flat_button_dtor(&pw->btn[i]);
    for (int i = BARS - 1; i >= 0; i--) scroll_dtor(bar_at(pw, i));
    for (int i = PREFWIN_BOXES - 1; i >= 0; i--) {
        struct game_win *w = box_win(pw, i);
        struct game_dlg *d = box_dlg(pw, i);
        checkbox_dtor((uint8_t *)w);
        dialog_dtor((struct game_dialog *)d);
        graphic_win_dtor(w);
    }
    for (int i = PREFWIN_PAGES - 1; i >= 0; i--) flat_button_dtor(&pw->tab[i]);
    graphic_win_dtor(&pw->win);
}

static void *UI_THIS prefwin_scalar_dtor(struct game_prefwin *pw, int flags) {
    prefwin_dtor(pw);
    if (flags & 1) game_free(pw);
    return pw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS prefwin_operator_delete(struct game_buffer *buf, int flags) {
    return prefwin_scalar_dtor((struct game_prefwin *)((uint8_t *)buf - offsetof(struct game_prefwin, buf)), flags);
}

// ---- Setting up ---------------------------------------------------------------------------------------------

// Gives a button the buttons' Font and draws it (it is hidden or not as it stands).
static void style_button(struct game_flat_button *b) {
    buffer_set_font(&b->base.buf, button_font(), NULL, NULL, NULL);
    redraw(button_win(b));
}

static void make_button(struct game_prefwin *pw, struct game_flat_button *b, const char *name, int id, int x, int y, int w, int h) {
    game_base_button_init(b, name, id, x, y, w, h, pw, 0);
}

// A label's text in the shared text buffer, which some buttons are made from.
static char *label_in_buffer(int label) {
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, game_label(label));
    return game_str_buffer;
}

// Opens a section of the script file; answers non-zero when it is not there.
static int open_section(uint32_t section) { return game_x_text_open(*(char **)game_at(TEXT_FILE), text(section)); }

static void skip_lines(int n) {
    while (n-- > 0) game_text_get();
}

// A box, made over the options' area at the height given, with the two pictures its items are drawn with; an item for each of
// its ids in the order of its lines in the script (the next line, with a space before it); and attached to the window.
static void make_box(struct game_prefwin *pw, int box, int height) {
    game_checkbox_close(pw->box[box]);
    dialog_init_2((struct game_dialog *)box_dlg(pw, box), word(S_BOX_X), word(S_BOX_Y), word(S_AREA_W), height, 0);
    *(void **)(pw->box[box] + 0x10) = game_at(CHECK_PICTURE_A);
    *(void **)(pw->box[box] + 0x14) = game_at(CHECK_PICTURE_B);
    for (int i = 0, id; (id = prefwin_make_item(box, i)); i++) {
        game_str_buffer[0] = 0;
        game_text_get();
        strcat(game_str_buffer, text(ITEM_PREFIX));
        strcat(game_str_buffer, *game_text_line);
        game_checkbox_item(pw->box[box], game_str_buffer, id, 0);
    }
    UI_VCALL(void(UI_THIS *)(void *, void *, int, int, int), box_dlg(pw, box), VT_DIALOG_ATTACH, pw, word(S_BOX_X), word(S_BOX_Y), 0x1000120);
}

static void tell_scrolled(struct game_prefwin *pw, int id, int pos) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pw, VT_SCROLLED, id, pos);
}

// The sliders (four are used): made at their place, 0x16 apart, over 0 to 0x7f, and set to the volume saved, with the window
// told of each. The Music slider is set only when there is a music object (else it is put at 0 and not told).
static void make_bars(struct game_prefwin *pw) {
    for (int i = 0; i < VOLUME_BARS; i++)
        game_scroll_init_bar(bar_at(pw, i), word(S_BAR_X), word(S_BAR_Y) + 0x16 * i, word(S_BAR_W), pw, ID_BAR_MAIN + i);
    for (int i = 0; i < VOLUME_BARS; i++) scroll_set_range(bar_at(pw, i), 0, 0x7f);
    scroll_set_pos(bar_at(pw, 0), game_prefs_get_2(text(NAME_MAIN), 0x7f, 0));
    tell_scrolled(pw, ID_BAR_MAIN, bar_at(pw, 0)->pos);
    scroll_set_pos(bar_at(pw, 1), game_prefs_get_2(text(NAME_VOICE), 0x55, 0));
    tell_scrolled(pw, ID_BAR_VOICE, bar_at(pw, 1)->pos);
    scroll_set_pos(bar_at(pw, 2), game_prefs_get_2(text(NAME_SFX), 0x5f, 0));
    tell_scrolled(pw, ID_BAR_SFX, bar_at(pw, 2)->pos);
    if (word(VOICE)) {
        scroll_set_pos(bar_at(pw, 3), game_prefs_get_2(text(NAME_MUSIC), 0x3f, 0));
        tell_scrolled(pw, ID_BAR_MUSIC, bar_at(pw, 3)->pos);
    } else {
        scroll_set_pos(bar_at(pw, 3), 0);
    }
}

static void UI_THIS prefwin_init(struct game_prefwin *pw) {
    pw->page = 0, pw->last_page = 0, pw->by_key = 0, pw->accepted = 0;

    struct prefwin_origin where;
    prefwin_origin(&where, word(SCREEN_WIDTH), word(SCREEN_HEIGHT), word(S_WIN_W));
    int small_loss = 0;
    if (where.small) {
        put_word(S_SHIFT, 0x32);
        put_word(S_BAR_Y, word(S_BAR_Y) - 0x32);
        small_loss = 0x50;
    }
    uint8_t *console = game_at(CONSOLE);
    struct game_win *parent = (struct game_win *)(console + ((int32_t *)*(void **)console)[1]);
    graphic_win_init(&pw->win, where.x, where.y, word(S_WIN_W) + 0x4e, word(S_WIN_H), game_at(S_TITLE), 0x1000000, parent, NULL, NULL);
    graphic_win_fill(&pw->win, 9);
    static const struct {
        int size, style;
    } fonts[FONTS] = {{10, 0}, {12, 0}, {12, 2}, {12, 1}, {14, 0}, {14, 2}, {14, 1}};
    for (int i = 0; i < FONTS; i++) pw->font[i] = game_find_font(fonts[i].size, fonts[i].style);
    buffer_set_font(&pw->buf, *(struct font **)game_at(S_FONT_A), *(struct font **)game_at(S_FONT_A), *(struct font **)game_at(S_FONT_B), NULL);
    buffer_set_text_color(&pw->buf, word(TEXT_COLOR), word(TEXT_COLOR + 0xc), 1, 1);

    // The seven tabs down the left, each from a label in the shared buffer, 0x16 apart (the seventh, Gamma, three places lower); the
    // first six are the group, the first chosen.
    static const int tab_label[PREFWIN_PAGES] = {LABEL_TAB, LABEL_TAB + 1, LABEL_TAB + 2, LABEL_TAB + 3, LABEL_TAB + 4, LABEL_TAB_MAP, LABEL_TAB_GAMMA};
    for (int i = 0; i < PREFWIN_PAGES; i++) {
        make_button(pw, &pw->tab[i], label_in_buffer(tab_label[i]), i, word(S_TAB_X), word(S_TAB_Y) + (i < PREFWIN_PAGES - 1 ? 0x16 * i : 0x9a),
                    word(S_TAB_W), word(S_TAB_H));
        style_button(&pw->tab[i]);
    }
    button_group_init(group(), 0x5d, 0);
    for (int i = 0; i < PREFWIN_PAGES - 1; i++) button_group_add(group(), &pw->tab[i].base);
    button_group_set(group(), 0, 1);

    // The boxes, from the script's sections (each section starts with lines the window does not use). A section that is not
    // there ends the setting up where it stands.
    if (open_section(SECTION_BASIC)) return;
    skip_lines(3);
    make_box(pw, BOX_BASIC, word(S_AREA_H));
    game_text_close();
    if (open_section(SECTION_ADVANCED)) return;
    skip_lines(3);
    make_box(pw, BOX_ADVANCED, word(S_AREA_H));
    game_text_close();
    if (open_section(SECTION_AUTOMATION)) return;
    skip_lines(3);
    make_box(pw, BOX_AUTOMATION, word(S_AREA_H));
    game_text_close();
    if (open_section(SECTION_AUDIO)) return;
    skip_lines(6);
    make_box(pw, BOX_AUDIO, prefwin_box_height(BOX_AUDIO, word(S_AREA_H), word(S_GAP), word(S_VOL_H), small_loss));
    game_text_close();
    if (open_section(SECTION_WARNINGS)) return;
    skip_lines(4);
    make_box(pw, BOX_WARNINGS_A, prefwin_box_height(BOX_WARNINGS_A, word(S_AREA_H), word(S_GAP), word(S_VOL_H), 0));
    make_box(pw, BOX_WARNINGS_B, prefwin_box_height(BOX_WARNINGS_B, word(S_AREA_H), word(S_GAP), word(S_VOL_H), 0));
    game_text_close();
    if (open_section(SECTION_MAP)) return;
    skip_lines(4);
    make_box(pw, BOX_MAP, word(S_AREA_H));
    game_text_close();

    make_bars(pw);
    for (int i = 0; i < VOLUME_BARS; i++) hide(bar_at(pw, i));
    show(box_win(pw, BOX_BASIC));
    for (int box = 1; box < PREFWIN_BOXES; box++) hide(box_win(pw, box));

    // The sound buttons (made in the order Sound, Voice, SFX, Music, down a column 0x16 apart), and the buttons under the tabs.
    // The buttons start unpressed and hidden.
    static const struct {
        int button, id, line, label;
    } toggles[] = {{B_SOUND, ID_SOUND, 0, 0x438}, {B_VOICE, ID_VOICE, 1, 0x43a}, {B_SFX, ID_SFX, 2, 0x43c}, {B_MUSIC, ID_MUSIC, 3, 0x43e}};
    for (int i = 0; i < 4; i++)
        make_button(pw, &pw->btn[toggles[i].button], game_label(toggles[i].label), toggles[i].id, word(S_TOGGLE_X),
                    word(S_TOGGLE_Y) - word(S_SHIFT) + 0x16 * toggles[i].line, word(S_TOGGLE_W), word(S_TOGGLE_H));
    for (int i = 0; i < 4; i++) style_button(&pw->btn[toggles[i].button]);
    for (int i = 0; i < 4; i++) pw->btn[i].base.state = 0;
    for (int i = 0; i < 4; i++) hide(button_win(&pw->btn[toggles[i].button]));

    int bottom = word(S_BOTTOM) - word(S_SHIFT) + word(S_MARGIN_Y);
    make_button(pw, &pw->btn[B_DEFAULTS], game_label(LABEL_DEFAULTS), ID_DEFAULTS, word(S_TAB_X), bottom - 0x45, word(S_TAB_W), word(S_TAB_H));
    make_button(pw, &pw->btn[B_OK], label_in_buffer(LABEL_OK), ID_OK, word(S_TAB_X), bottom - 0x2e, word(S_TAB_W), word(S_TAB_H));
    make_button(pw, &pw->btn[B_CANCEL], label_in_buffer(LABEL_CANCEL), ID_CANCEL, word(S_TAB_X), bottom - 0x17, word(S_TAB_W), word(S_TAB_H));
    style_button(&pw->btn[B_DEFAULTS]);
    style_button(&pw->btn[B_OK]);
    style_button(&pw->btn[B_CANCEL]);
    pw->btn[B_DEFAULTS].base.state = 0, pw->btn[B_OK].base.state = 0, pw->btn[B_CANCEL].base.state = 0;
    hide(button_win(&pw->btn[B_DEFAULTS]));
    hide(button_win(&pw->btn[B_OK]));
    hide(button_win(&pw->btn[B_CANCEL]));
    make_button(pw, &pw->btn[B_MORE], game_label(LABEL_MORE), ID_MORE, word(S_BOX_X), word(S_PAGE_Y) + word(S_AREA_H) - 0x17, word(S_TAB_W),
                word(S_TAB_H));
    pw->btn[B_MORE].base.state = 0;
    hide(button_win(&pw->btn[B_MORE]));
    hide(pw);
}

// ---- The sound buttons ---------------------------------------------------------------------------------------

// The button of a sound id (0x55 to 0x58), or NULL for any other id.
static struct game_flat_button *sound_button(struct game_prefwin *pw, int id) {
    return id >= ID_SOUND && id <= ID_MUSIC ? &pw->btn[id - ID_SOUND] : NULL;
}

// A sound button's name is the label of its state (on or off).
static void UI_THIS prefwin_change_volume_button_text(struct game_prefwin *pw, int id) {
    struct game_flat_button *b = sound_button(pw, id);
    if (!b) return;
    base_button_set_name(&b->base, game_label(prefwin_toggle_label(id, b->base.state != 0)));
}

// A sound button was clicked: it turns over and the sound object and the game's preference flags follow. The Sound button turns
// all of them on or off; the others turn on the Sound button when they are turned on while it is off.
static void UI_THIS prefwin_change_volume_button_state(struct game_prefwin *pw, int id) {
    void *sound = game_at(SOUND);
    struct game_flat_button *all = &pw->btn[B_SOUND];
    switch (id) {
    case ID_SOUND:
        if (all->base.state) {
            all->base.state = 0;
            pw->btn[B_MUSIC].base.state = 0;
            pw->btn[B_VOICE].base.state = 0;
            pw->btn[B_SFX].base.state = 0;
            game_sound_all_off(sound);
            put_word(PREFS_A, word(PREFS_A) & ~0xc00);
            put_word(PREFS_B, word(PREFS_B) & ~0x4000);
        } else {
            all->base.state = 1;
            pw->btn[B_MUSIC].base.state = 1;
            pw->btn[B_VOICE].base.state = 1;
            pw->btn[B_SFX].base.state = 1;
            game_sound_all_on(sound);
        }
        prefwin_change_volume_button_text(pw, ID_MUSIC);
        prefwin_change_volume_button_text(pw, ID_VOICE);
        prefwin_change_volume_button_text(pw, ID_SFX);
        return;
    case ID_SFX:
        if (pw->btn[B_SFX].base.state) {
            game_sound_channel_off(sound, 1);
            pw->btn[B_SFX].base.state = 0;
            return;
        }
        pw->btn[B_SFX].base.state = 1;
        if (!all->base.state) {
            all->base.state = 1;
            prefwin_change_volume_button_text(pw, ID_SOUND);
        }
        game_sound_channel_on(sound, 1);
        return;
    case ID_VOICE:
        if (pw->btn[B_VOICE].base.state) {
            pw->btn[B_VOICE].base.state = 0;
            game_sound_channel_off(sound, 0);
            return;
        }
        pw->btn[B_VOICE].base.state = 1;
        if (!all->base.state) {
            all->base.state = 1;
            prefwin_change_volume_button_text(pw, ID_SOUND);
        }
        game_sound_channel_on(sound, 0);
        return;
    case ID_MUSIC: {
        void **music = game_at(VOICE);
        if (pw->btn[B_MUSIC].base.state) {
            if (*music) {
                UI_VCALL(void(UI_THIS *)(void *, int), *music, 0x8, 1);  // (the music object's deleting destructor)
                *music = NULL;
            }
            pw->btn[B_MUSIC].base.state = 0;
            return;
        }
        pw->btn[B_MUSIC].base.state = 1;
        if (!all->base.state) {
            all->base.state = 1;
            prefwin_change_volume_button_text(pw, ID_SOUND);
        }
        put_word(PREFS_A, word(PREFS_A) | 0x800);
        game_voice_start(word(VOICE_ARG));
        if (*music) UI_VCALL(void(UI_THIS *)(void *), *music, 0x12c);
        return;
    }
    default:
        return;
    }
}

// ---- The options ---------------------------------------------------------------------------------------------

// Whether a box's item is checked (asked through the box's own function, which walks its ring to the id).
static int item_checked(struct game_prefwin *pw, int box, int id) { return checkbox_get_state_id(pw->box[box], id) != 0; }

// Reads the boxes (and the sound buttons) back into the eleven words: each option sets its bit when the box item is checked,
// or, for the few the box shows opposite, when it is not. The words start clear. (A box is asked for each id in its own order,
// ids it has no item for included: see prefwin_set_item.)
static void UI_THIS prefwin_set_preferences(struct game_prefwin *pw) {
    memset(pw->opt, 0, sizeof(pw->opt));
    for (int box = 0; box < PREFWIN_BOXES; box++) {
        for (int i = 0, id; (id = prefwin_set_item(box, i)); i++) {
            struct prefwin_place p;
            if (!prefwin_place(id, &p)) continue;
            int checked = item_checked(pw, box, id);
            if (p.inverted ? !checked : checked) pw->opt[p.field] |= p.mask;
        }
        // The audio page's three buttons (Voice, SFX and Music): pressed or not.
        if (box == BOX_AUDIO) {
            static const struct {
                int button, id;
            } pressed[] = {{B_VOICE, 0x3f}, {B_SFX, 0x3e}, {B_MUSIC, 0x3d}};
            for (int k = 0; k < 3; k++) {
                struct prefwin_place p;
                prefwin_place(pressed[k].id, &p);
                if (pw->btn[pressed[k].button].base.state) pw->opt[p.field] |= p.mask;
            }
        }
    }
}

// Puts the eleven words into the boxes' items and the sound buttons.
static void UI_THIS prefwin_get_preferences(struct game_prefwin *pw) {
    for (int box = 0; box < PREFWIN_BOXES; box++) {
        for (int i = 0, id; (id = prefwin_get_item(box, i)); i++) {
            struct prefwin_place p;
            prefwin_place(id, &p);
            int bit = (pw->opt[p.field] & p.mask) != 0;
            if (box == BOX_AUDIO && (id == 0x3d || id == 0x3e || id == 0x3f)) {
                // Not items of the box: the buttons take them, and their text is made again.
                int button = id == 0x3d ? B_MUSIC : id == 0x3e ? B_SFX : B_VOICE;
                pw->btn[button].base.state = bit;
                prefwin_change_volume_button_text(pw, ID_SOUND + button);
            } else if (id == 0x0f) {
                // The one item that is set and then turned over (the box shows the opposite of the bit).
                checkbox_set_state_id(pw->box[box], id, bit);
                checkbox_flip_state_id(pw->box[box], id);
            } else {
                checkbox_set_state_id(pw->box[box], id, p.inverted ? !bit : bit);
            }
        }
    }
}

// ---- Showing pages -------------------------------------------------------------------------------------------

// The page that was shown is hidden.
static void UI_THIS prefwin_hide_option_buttons(struct game_prefwin *pw) {
    switch (pw->last_page) {
    case 0: hide(box_win(pw, BOX_BASIC)); break;
    case 1: hide(box_win(pw, BOX_ADVANCED)); break;
    case 2:
        hide(box_win(pw, BOX_WARNINGS_A));
        hide(button_win(&pw->btn[B_MORE]));
        break;
    case 3: hide(box_win(pw, BOX_AUTOMATION)); break;
    case 4:
        hide(box_win(pw, BOX_AUDIO));
        for (int i = 0; i < VOLUME_BARS; i++) hide(bar_at(pw, i));
        hide(button_win(&pw->btn[B_SOUND]));
        hide(button_win(&pw->btn[B_VOICE]));
        hide(button_win(&pw->btn[B_SFX]));
        hide(button_win(&pw->btn[B_MUSIC]));
        break;
    case 5: hide(box_win(pw, BOX_MAP)); break;
    case 6:
        hide(box_win(pw, BOX_WARNINGS_B));
        hide(button_win(&pw->btn[B_MORE]));
        break;
    default: break;
    }
}

// The page to be shown is shown (the More button is named for the page it swaps to).
static void UI_THIS prefwin_update_pref_buttons(struct game_prefwin *pw) {
    switch (pw->page) {
    case 0: show(box_win(pw, BOX_BASIC)); break;
    case 1: show(box_win(pw, BOX_ADVANCED)); break;
    case 2:
        show(box_win(pw, BOX_WARNINGS_A));
        base_button_set_name(&pw->btn[B_MORE].base, game_label(LABEL_MORE));
        show(button_win(&pw->btn[B_MORE]));
        break;
    case 3: show(box_win(pw, BOX_AUTOMATION)); break;
    case 4:
        show(box_win(pw, BOX_AUDIO));
        for (int i = 0; i < VOLUME_BARS; i++) show(bar_at(pw, i));
        show(button_win(&pw->btn[B_SOUND]));
        show(button_win(&pw->btn[B_VOICE]));
        show(button_win(&pw->btn[B_SFX]));
        show(button_win(&pw->btn[B_MUSIC]));
        break;
    case 5: show(box_win(pw, BOX_MAP)); break;
    case 6:
        show(box_win(pw, BOX_WARNINGS_B));
        base_button_set_name(&pw->btn[B_MORE].base, label_in_buffer(LABEL_FEWER));
        show(button_win(&pw->btn[B_MORE]));
        break;
    default: break;
    }
}

// ---- Drawing -------------------------------------------------------------------------------------------------

// The frame round the options' area, and (on the audio page) the one round the sliders under it.
static void draw_page_frame(struct game_prefwin *pw) {
    struct ui_rect r = {word(S_PAGE_X), word(S_PAGE_Y), word(S_PAGE_X) + word(S_PAGE_W), word(S_PAGE_Y) + word(S_AREA_H)};
    buffer_box_sprite(&pw->buf, &r.left, frame_at(FRAME_AREA));
}

static void draw_volume_frame(struct game_prefwin *pw) {
    int top = word(S_VOL_Y) - word(S_SHIFT);
    struct ui_rect r = {word(S_VOL_X), top, word(S_VOL_W) + word(S_VOL_X), word(S_VOL_H) + top};
    if (pw->page == 4) buffer_box_sprite(&pw->buf, &r.left, frame_at(FRAME_WINDOW));
}

// Draws the frame of the options' area. (Nothing calls this: it is the first part of on_redraw, written out separately.)
static void UI_THIS prefwin_redraw_option(struct game_prefwin *pw) { draw_page_frame(pw); }

// Draws the frame of the sliders when the audio page is shown. (Nothing calls this either.)
static void UI_THIS prefwin_redraw_volume(struct game_prefwin *pw) { draw_volume_frame(pw); }

// The frames, then the page to hide and the page to show, and every box and button drawn.
static void UI_THIS prefwin_on_redraw(struct game_prefwin *pw) {
    draw_page_frame(pw);
    draw_volume_frame(pw);
    prefwin_hide_option_buttons(pw);
    prefwin_update_pref_buttons(pw);
    static const int boxes[] = {BOX_BASIC, BOX_ADVANCED, BOX_WARNINGS_A, BOX_WARNINGS_B, BOX_AUTOMATION, BOX_AUDIO, BOX_MAP};
    for (int i = 0; i < PREFWIN_BOXES; i++) redraw(box_win(pw, boxes[i]));
    redraw(button_win(&pw->btn[B_SOUND]));
    redraw(button_win(&pw->btn[B_VOICE]));
    redraw(button_win(&pw->btn[B_SFX]));
    redraw(button_win(&pw->btn[B_MUSIC]));
    redraw(button_win(&pw->btn[B_OK]));
    redraw(button_win(&pw->btn[B_CANCEL]));
    redraw(button_win(&pw->btn[B_DEFAULTS]));
}


// ---- Opening and closing -------------------------------------------------------------------------------------

// The game's preference word a kind of the eleven is a copy of.
static int32_t game_word(int kind) { return word(kind == PREF_WORD_A ? PREFS_A : kind == PREF_WORD_B ? PREFS_B : PREFS_C); }

// Opens the window on a page (a negative page keeps the one it had): the eleven words are copies of the game's three, the
// frames drawn, the boxes and buttons set from the words, the three buttons under the tabs shown, and the window run as a modal
// one. What was the preferences' bit for the windows' own sake (0x6970dc's bit 2 follows the game's bit 29, inverted) is set at the end.
static void UI_THIS prefwin_display(struct game_prefwin *pw, int page) {
    game_window_opening(game_at(WINDOW_OPENING));
    pw->out[0] = pw->out[1] = pw->out[2] = 0;
    for (int f = 0; f < PREFWIN_WORDS; f++) pw->opt[f] = game_word(prefwin_word_kind(f));

    // The window's frame (two boxes) and the frame of the tab column.
    int w = word(S_WIN_W), h = word(S_WIN_H);
    struct ui_rect outer = {0x17, 0, w + 0x17, h};
    struct ui_rect inner = {0x1b, 4, w + 0x13, h - 4};
    buffer_box_sprite(&pw->buf, &outer.left, frame_at(FRAME_WINDOW));
    buffer_box_sprite(&pw->buf, &inner.left, frame_at(FRAME_WINDOW));
    struct ui_rect column = {word(S_COLUMN_X), word(S_MARGIN_Y), word(S_COLUMN_W) + word(S_COLUMN_X), word(S_BOTTOM) + word(S_MARGIN_Y)};
    buffer_box_sprite(&pw->buf, &column.left, frame_at(FRAME_AREA));

    prefwin_get_preferences(pw);
    pw->btn[B_SOUND].base.state = pw->btn[B_MUSIC].base.state || pw->btn[B_VOICE].base.state || pw->btn[B_SFX].base.state;
    prefwin_change_volume_button_text(pw, ID_SOUND);
    draw_page_frame(pw);
    draw_volume_frame(pw);

    show(button_win(&pw->btn[B_DEFAULTS]));
    show(button_win(&pw->btn[B_OK]));
    show(button_win(&pw->btn[B_CANCEL]));
    if (page >= 0) {
        pw->last_page = pw->page;
        pw->page = page;
        button_group_click(group(), page);
    }
    redraw(pw);
    game_slide_show(pw, 0);
    UI_VCALL(void(UI_THIS *)(void *, int), pw, VT_SHOW, 3);
    win_set_def_focus(&pw->win);
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), pw, VT_SET_MODAL, 0, (int)(uintptr_t)game_pop_wait, 0);
    int32_t mirror = word(FLAGS_MIRROR);
    put_word(FLAGS_MIRROR, (~word(PREFS_A) & 0x20000000) ? (mirror | 4) : (mirror & ~4));
}

// Ends the window: if OK was clicked the boxes are read into the words, the words merged into the three the game is handed, and
// the game applies them and saves its preferences file and the sliders' positions. In either case the window is hidden and the
// map drawn again.
static void UI_THIS prefwin_close_window(struct game_prefwin *pw) {
    if (pw->accepted) {
        prefwin_set_preferences(pw);
        prefwin_merge(pw->opt, pw->out);
        put_word(PREFS_OUT, pw->out[0]);
        put_word(PREFS_OUT + 4, pw->out[1]);
        put_word(PREFS_OUT + 8, pw->out[2]);
        game_prefs_use();
        game_prefs_save(0);
        game_prefs_put_2(text(SAVE_MAIN), bar_at(pw, 0)->pos, 0);
        game_prefs_put_2(text(SAVE_VOICE), bar_at(pw, 1)->pos, 0);
        game_prefs_put_2(text(SAVE_MUSIC), bar_at(pw, 3)->pos, 0);
        game_prefs_put_2(text(SAVE_SFX), bar_at(pw, 2)->pos, 0);
        pw->accepted = 0;
    }
    game_draw_map(1);
    put_word(FLAGS_UI, word(FLAGS_UI) | 4);
    pw->btn[B_OK].base.pressed = 0;
    pw->btn[B_CANCEL].base.pressed = 0;
    game_slide_hide(pw, 0);
    hide(pw);
    uint8_t *console = game_at(CONSOLE);
    win_set_def_focus((struct game_win *)(console + ((int32_t *)*(void **)console)[1]));
    release_modal(pw);
}

// ---- Input ---------------------------------------------------------------------------------------------------

// Return clicks OK and Escape Cancel; Up and Down go to the page before and after (round), through the group, which tells
// this window. Every key is answered as used.
static int UI_THIS prefwin_on_key_click(struct game_prefwin *pw, int unused, int key) {
    (void)unused;
    switch (key) {
    case 0x0d:
        pw->accepted = 1;
        prefwin_close_window(pw);
        break;
    case 0x1b:
        prefwin_close_window(pw);
        break;
    case 0x26:
    case 0x28:
        pw->last_page = pw->page;
        pw->page = prefwin_step_page(pw->page, key == 0x26);
        pw->by_key = 1;
        button_group_click(group(), pw->page);
        break;
    default:
        break;
    }
    return 1;
}

// The group of tabs changed (its `user` is not looked at): a click, not a key, keeps the tab it left (when it was one of the six);
// the page becomes the new tab, and the window is told a button was clicked with it as the id.
static void UI_THIS prefwin_on_group_clicked(struct game_prefwin *pw, int user, int old_id, int new_id) {
    (void)user;
    if (!pw->by_key) {
        if (old_id >= 0 && old_id <= 5) pw->last_page = old_id;
    } else {
        pw->by_key = 0;
    }
    pw->page = new_id;
    UI_VCALL(void(UI_THIS *)(void *, int), pw, VT_BUTTON_CLICKED, new_id);
}

// A slider moved: the sound object is told (the Main bar sets the master volume, Voice and SFX a channel each), and the music
// object, when there is one, takes the Music bar's.
static void UI_THIS prefwin_on_scrolling(struct game_prefwin *pw, int id, int pos) {
    (void)pw;
    switch (id) {
    case ID_BAR_MAIN: game_sound_master(game_at(SOUND), pos); break;
    case ID_BAR_VOICE: game_sound_channel_volume(game_at(SOUND), 0, pos); break;
    case ID_BAR_SFX: game_sound_channel_volume(game_at(SOUND), 1, pos); break;
    case ID_BAR_MUSIC: {
        void *music = *(void **)game_at(VOICE);
        if (music) UI_VCALL(void(UI_THIS *)(void *, int), music, 0x40, pos);
        break;
    }
    default: break;
    }
}

// The number dialog (Gamma) is an object the game makes and unmakes with its own functions; its size is the original's frame
// less the little that is not it.
enum { NUMDLG_SIZE = 0x5200 };

// Sets all the options to the game's defaults, the sound buttons on and the sliders to their default places.
static void set_defaults(struct game_prefwin *pw) {
    pw->opt[0] = game_pref_default_a();
    pw->opt[1] = game_pref_default_b();
    pw->opt[2] = game_pref_default_a();
    pw->opt[3] = game_pref_default_b();
    pw->opt[4] = game_pref_default_a();
    pw->opt[5] = game_pref_default_b();
    pw->opt[7] = game_pref_default_a();
    pw->opt[8] = game_pref_default_b();
    pw->opt[6] = game_pref_default_c();
    pw->opt[9] = game_pref_default_a();
    pw->opt[10] = game_pref_default_b();
    pw->btn[B_SOUND].base.state = 1, pw->btn[B_VOICE].base.state = 1, pw->btn[B_SFX].base.state = 1, pw->btn[B_MUSIC].base.state = 1;
    scroll_set_pos(bar_at(pw, 0), 0x7f);
    game_sound_master(game_at(SOUND), 0x7f);
    scroll_set_pos(bar_at(pw, 1), 0x7f);
    game_sound_channel_volume(game_at(SOUND), 0, 0x7f);
    scroll_set_pos(bar_at(pw, 2), 0x5f);
    game_sound_channel_volume(game_at(SOUND), 1, 0x5f);
    scroll_set_pos(bar_at(pw, 3), 0x3f);
    void *music = *(void **)game_at(VOICE);
    if (music) UI_VCALL(void(UI_THIS *)(void *, int), music, 0x40, 0x3f);
    prefwin_get_preferences(pw);
}

// A button was clicked, or a tab chosen. The number dialog is made first and unmade last, as the original does on every call.
static void UI_THIS prefwin_on_button_clicked(struct game_prefwin *pw, int id) {
    uint8_t numdlg[NUMDLG_SIZE] __attribute__((aligned(16)));
    game_numdlg_ctor(numdlg);
    switch (id) {
    case ID_GAMMA: {
        // The Gamma button asks for a number (1.0 is no change) and saves it, times a hundred, with the preferences.
        palette_assign(game_at(NUM_PALETTE), game_at(NUM_PALETTE_FROM));
        float value = *(float *)game_at(GAMMA);
        *(float *)game_at(GAMMA) = game_numdlg_run(numdlg, game_at(NUM_PALETTE), value, 0);
        game_prefs_put_2(text(SAVE_GAMMA), (int)((double)*(float *)game_at(GAMMA) * (double)*(float *)game_at(HUNDRED)), 0);
        break;
    }
    case ID_MORE:
        // The Base Warnings page and its second page swap.
        if (pw->page == 2) {
            pw->last_page = 2;
            pw->page = 6;
        } else {
            pw->last_page = 6;
            pw->page = 2;
        }
        break;
    case ID_CANCEL:
        prefwin_close_window(pw);
        break;
    case ID_OK:
        pw->accepted = 1;
        prefwin_close_window(pw);
        break;
    case ID_DEFAULTS:
        set_defaults(pw);
        break;
    default:
        break;
    }
    prefwin_change_volume_button_state(pw, id);
    prefwin_change_volume_button_text(pw, id);
    redraw(pw);
    game_numdlg_dtor(numdlg);
}

// ---- Hooks ---------------------------------------------------------------------------------------------------

static const struct hook prefwin_hooks[] = {
    UI_HOOK(0x48d840, prefwin_init, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x14),                 // sub esp, 0x14
    UI_HOOK(0x48fa00, prefwin_display, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x40),              // sub esp, 0x40
    UI_HOOK(0x48fcd0, prefwin_on_key_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c),          // mov eax, [ebp+0xc]
    UI_HOOK(0x48fdd0, prefwin_close_window, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x5c, 0x0a, 0x00, 0x00),   // push esi; mov esi, ecx; mov eax, [esi+0xa5c]
    UI_HOOK(0x48ff60, prefwin_on_group_clicked, 0x55, 0x8b, 0xec, 0x8b, 0x81, 0x1c, 0x0a, 0x00, 0x00),  // mov eax, [ecx+0xa1c]
    UI_HOOK(0x48ffb0, prefwin_hide_option_buttons, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x18, 0x0a, 0x00, 0x00),  // mov eax, [esi+0xa18]
    UI_HOOK(0x490140, prefwin_update_pref_buttons, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x14, 0x0a, 0x00, 0x00),  // mov eax, [esi+0xa14]
    UI_HOOK(0x490350, prefwin_set_preferences, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xc0),      // push esi; mov esi, ecx; push edi; xor eax, eax
    UI_HOOK(0x490b00, prefwin_get_preferences, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08),      // sub esp, 8
    UI_HOOK(0x491380, prefwin_on_scrolling, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x83, 0xc0, 0xb0),  // add eax, -0x50
    UI_HOOK(0x491400, prefwin_on_button_clicked, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:0
    UI_HOOK(0x491810, prefwin_redraw_option, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),        // sub esp, 0x10
    UI_HOOK(0x491860, prefwin_redraw_volume, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),
    UI_HOOK(0x4918c0, prefwin_change_volume_button_text, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),  // mov eax, [ebp+8]
    UI_HOOK(0x4919f0, prefwin_change_volume_button_state, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x491c00, prefwin_on_redraw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),
    UI_HOOK(0x491df0, prefwin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff),                       // push -1
    UI_HOOK(0x4921e0, prefwin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff),
    UI_HOOK(0x4923e0, prefwin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),          // push esi; mov esi, ecx (then a call to the destructor)
    UI_HOOK(0x492410, prefwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),      // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module prefwin_module = {"prefwin", prefwin_hooks, UI_HOOK_COUNT(prefwin_hooks), NULL, NULL, -1};
