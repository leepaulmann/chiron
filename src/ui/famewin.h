// The Hall of Fame window (FameWin) and the function that opens it: the pure parts (the layout numbers, the way a new
// entry finds its place and the records it takes), kept apart so a test can check them without the game. See famewin.c.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui.h"

// The list the game keeps and writes to its fame file: for each of the seven factions the five best games, 0x8c bytes each.
enum { FAME_SETS = 7, FAME_RANKS = 5, FAME_ENTRIES = FAME_SETS * FAME_RANKS, FAME_NAME_LEN = 0x18 };

// One game in the list. An entry is in use when bit 0 of `flags` is set; the other bits say how the game ended.
struct fame_entry {
    uint8_t flags;       // 0x0: 1 in use, 2 an ironman game, 4 won by transcendence, 8 by conquest, 0x10 economic, 0x20 (the
                         // other victory), 0x40 a scenario
    uint8_t records;     // 0x1: the records the game holds (see FAME_RECORD_*)
    uint8_t pad_2[2];
    int32_t year;        // 0x4: the calendar year the game ended in
    int32_t score;       // 0x8: the faction's score
    int32_t credits;     // 0xc: the faction's energy credits
    int32_t rating;      // 0x10: the score as the end of game screen rates it (get_rating); what the list is kept in order by
    int32_t part_a;      // 0x14: score_parts[4]
    int32_t part_b;      // 0x18: score_parts[0]
    int32_t part_c;      // 0x1c: score_parts[2] and score_parts[3] added
    uint8_t difficulty;  // 0x20
    uint8_t planet;      // 0x21: 0 or how the game left the planet: 1, or 2 while the planet's native life is on (the game's flag)
    char title[FAME_NAME_LEN];  // 0x22: the leader's title
    char name[FAME_NAME_LEN];   // 0x3a: the leader's name
    char noun[FAME_NAME_LEN];   // 0x52: the faction's name
    uint8_t pad_6a[2];
    int32_t tail[8];     // 0x6c: never set by a game; copied along with the entry
};
_Static_assert(sizeof(struct fame_entry) == 0x8c, "layout of the game's fame entry");
_Static_assert(offsetof(struct fame_entry, year) == 0x4, "year");
_Static_assert(offsetof(struct fame_entry, rating) == 0x10, "rating");
_Static_assert(offsetof(struct fame_entry, part_c) == 0x1c, "part_c");
_Static_assert(offsetof(struct fame_entry, difficulty) == 0x20, "difficulty");
_Static_assert(offsetof(struct fame_entry, title) == 0x22, "title");
_Static_assert(offsetof(struct fame_entry, name) == 0x3a, "name");
_Static_assert(offsetof(struct fame_entry, noun) == 0x52, "noun");
_Static_assert(offsetof(struct fame_entry, tail) == 0x6c, "tail");

enum {
    FAME_IN_USE = 1, FAME_IRONMAN = 2, FAME_TRANSCENDENCE = 4, FAME_CONQUEST = 8, FAME_ECONOMIC = 0x10, FAME_OTHER_VICTORY = 0x20,
    FAME_SCENARIO = 0x40,
};
// The bits of `records`, the six records a game can hold, in the order of the six buttons: the best rating, the best part_c,
// the most credits, the best part_b, and the earliest conquest win and the earliest transcendence win.
enum {
    FAME_RECORD_RATING = 1, FAME_RECORD_C = 2, FAME_RECORD_CREDITS = 4, FAME_RECORD_B = 8, FAME_RECORD_CONQUEST = 0x10,
    FAME_RECORD_TRANSCENDENCE = 0x20, FAME_RECORD_COUNT = 6,
    FAME_NO_PLACE = FAME_RANKS,
};

// The place a game rated `rating` takes among the five of its faction (0 to 4), or FAME_NO_PLACE. The original walks the
// list from the last place up and keeps the last place it finds that is empty or that the game does not rate below, so
// a list not in order is read as it comes.
int famewin_place(const struct fame_entry *row, int rating);

// The flags a game ends with: in use, ironman if the rules say so, and one of the ways it was won (the first that holds,
// in this order: conquered, economic win, the other victory, the faction's transcendence part, scenario).
uint8_t famewin_flags(uint32_t state, uint32_t rules, int transcended);

// Takes the records a new game beats from the other games of the list (in `table`, the seven rows one after the other),
// leaving the entry itself, at `own_row` and `pos`, out. `n` is that entry. Each record a game beats or ties moves to it;
// with no other game in use the new one holds the first four, and the date records go to it when no other game of its
// kind has one.
void famewin_award(struct fame_entry *table, struct fame_entry *n, int own_row, int pos);

// Copies an entry as the original's demotion does: every field but the two spare bytes at 0x6a.
void famewin_copy(struct fame_entry *to, const struct fame_entry *from);

// The first game of the list that holds the record `bit` (a button's number, 0 to 5): the row and place. 0 if none.
int famewin_find_record(const struct fame_entry *table, int bit, int *row, int *pos);

// The index of the entry shown for faction `sel` (1 to 7) at place `rank` (0 to 4); outside the list it is read as a
// number and not checked.
int famewin_entry_index(int sel, int rank);

// The six rectangles of the pieces of the picture (the new game, the six panels), moved by the window's offset: each is
// four numbers, left, top, right, bottom.
void famewin_rects(int32_t rects[6][4], int dx, int dy);

// The width of each of `count` buttons laid across `left` to `right`, and the distance a picture of `size` is moved in to
// centre it in a cell (both are divisions that round toward zero).
int famewin_cell_width(int left, int right, int count);
int famewin_cell_inset(int cell, int size);

// The height of a line of a font: its ascent while `first` is below zero, else its height and `first`.
int famewin_line_height(int first, int ascent, int height);

// The window's position and size: for a screen of the small kind (what the game keeps in 0x8a4168 as zero) the window is 800
// by 600 at the screen's corner, else 1024 by 768 centred across, with its y a whole `screen_h - 0x300` from the top (the
// original does not halve it). `offset` is where the 800 by 600 part of the picture lies in it: (0, 0), or (0x77, 0x50).
struct famewin_position {
    int x, y, w, h;
    int offset_x, offset_y;
};
void famewin_position(struct famewin_position *p, int big, int screen_w, int screen_h);

// The box behind the heading of a record: its left (the text centred about the window's offset plus 0x186) and width.
void famewin_banner(int *left, int *width, int offset_x, int text_width);

// A file name made of a prefix, the faction, a middle, the place and a suffix (the pictures kept with the entries).
void famewin_file_name(char *out, const char *prefix, int faction, const char *middle, int place, const char *suffix);
