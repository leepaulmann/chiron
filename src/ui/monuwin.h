// The Monuments window (MonuWin) and the function that opens it: the pure parts (the layout numbers, the order the monuments
// are shown in, which cell of the picture sheets each icon is cut from, which pictures a zoomed monument shows), kept apart so a
// test can check them without the game. See monuwin.c.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui.h"

enum {
    MONU_KINDS = 15,       // the monuments the window lists, one icon each
    MONU_ALL_KINDS = 21,   // a faction's table has six more, the wins by a faction over another (shown as a faction's portrait)
    MONU_FACE_KINDS = 6,
    MONU_SLOTS = 14,       // the rectangles the listed icons are laid in
    MONU_PAIRS = 6,        // the places of the pictures that stand for the six wins
    MONU_NO_KIND = -1,
};

// One monument a faction holds, 0x38 bytes of the table the game keeps for each faction (the record is 0x4f4 bytes: 21 of
// these, the monument the window shows, and three names). A monument is in use when `used` is set; `first` says whether the
// faction was the first to have it (else `other` is the faction that had it before).
struct monu_entry {
    int32_t when;   // 0x0: the date it was made
    int32_t extra;  // 0x4: a number some kinds keep (-1 when there is none)
    int32_t what;   // 0x8: what it is of: a tech, a secret project, a faction (or -1), by kind
    int32_t other;  // 0xc: the faction that had it before
    int32_t used;   // 0x10
    int32_t first;  // 0x14: 1 for the first to have it
    uint8_t pad_18[0x38 - 0x18];
};
_Static_assert(sizeof(struct monu_entry) == 0x38, "layout of the game's monument");
_Static_assert(offsetof(struct monu_entry, what) == 0x8, "what");
_Static_assert(offsetof(struct monu_entry, used) == 0x10, "used");

// The window's position and size: for a screen of the small kind (what the game keeps in 0x8a4168 as zero) the window is 800
// by 600 at the screen's corner, else 1024 by 768 across the middle, with its y a whole `screen_h - 0x300` from the top (the
// original does not halve it). `offset` is where the 800 by 600 part of the picture lies in the window: (0, 0), or (0x70, 0x54).
struct monuwin_position {
    int x, y, w, h, offset_x, offset_y;
};
void monuwin_position(struct monuwin_position *p, int big, int screen_w, int screen_h);

// The rectangles the window's pieces lie in (left, top, right, bottom), moved by the window's offset (the picture's corner).
// `view` is where the picture of the monument shown is drawn and what clicking on it means, `caption` the box its words go
// in, `zoom` where a zoomed monument's own picture goes, `slot` the places of the listed icons (the first icon takes the
// first, and so on), `pair` the places of the pictures of the wins (two to a position), `face` the places of the portraits
// of the factions beaten, and `portrait` the leader's own picture. The `portrait` differs when the faction has fourteen
// monuments (the game's own count): it is then a larger one.
struct monuwin_rects {
    int32_t view[4], caption[4], zoom[4];
    int32_t slot[MONU_SLOTS][4];
    int32_t pair[MONU_PAIRS][4];
    int32_t face[MONU_FACE_KINDS][4];
    int32_t portrait[4];
};
void monuwin_rects(struct monuwin_rects *r, int dx, int dy, int fourteen);

// Where the pictures are cut from the sheet of the icons (monicons.pcx): the 0x2c by 0x2a icon of kind `i` (eight to a row,
// 0x2d apart, a second row 0x2b down) and the 0xa0 by 0x61 picture of it (five to a row, 0xa1 apart, 0x62 down).
void monuwin_icon_cell(int i, int *x, int *y);
void monuwin_big_cell(int i, int *x, int *y);

// The order the listed monuments are shown in: `order` holds each kind that is in use (or -1) at its own place, and is sorted
// by selection, the monument with the lowest `when` first and the unused at the end; `when[k]` is the date of kind k. The
// compare is signed, and ties stay where they are.
void monuwin_sort(int *order, const int32_t *when);

// Which of the pictures standing for the wins a count of them shows: the first is picture `monuwin_pair_slot(count, 0)` and,
// when there is more than one, the second `monuwin_pair_slot(count, 1)`; the pictures and rectangles of a position are
// two apart in their tables.
int monuwin_pair_slot(int count, int second);

// The portraits of the beaten factions the window draws: it walks `count` kinds from the first of the six, `used[k]` saying
// whether kind k is in use. For each one in use it gives the kind and the place (the number of steps taken). A kind passed
// over as unused moves the walk on by two kinds (and two entries of the table), not one, as the original's loop does, while
// the place moves on by one. Answers how many are drawn, up to six.
int monuwin_face_walk(int count, const int *used, int *kind, int *place);

// Which picture a zoomed monument shows: 0 the faction's own (the kind with the number 13), 1 that of the faction it is of
// (kinds 15 and up), 2 the one for its kind alone; and where in that picture the part shown starts.
int monuwin_zoom_picture(int kind, int *x, int *y);

// The height of a line of a font: its ascent while `first` is below zero, else its height and `first`.
int monuwin_line_height(int first, int ascent, int height);
