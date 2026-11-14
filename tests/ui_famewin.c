// Unit test for the Hall of Fame's pure parts (src/ui/famewin.c): no game files needed. The numbers are those the original's
// code gives (FameWin_init's panel table and divisions, FameWin_try_to_add_me's place search and record loops), worked out by
// hand from the disassembly.
#include <stdio.h>
#include <string.h>

#include "../src/ui/famewin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

static void expect_rect(int line, const char *what, const int32_t *r, int left, int top, int right, int bottom) {
    if (r[0] != left || r[1] != top || r[2] != right || r[3] != bottom) {
        printf("FAIL line %d: %s = {%d, %d, %d, %d}, expected {%d, %d, %d, %d}\n", line, what, r[0], r[1], r[2], r[3], left, top, right,
               bottom);
        failures++;
    }
}
#define EXPECT_RECT(r, l, t, rr, b) expect_rect(__LINE__, #r, (r), l, t, rr, b)

static struct fame_entry make(uint8_t flags, uint8_t records, int year, int rating, int credits, int part_b, int part_c) {
    struct fame_entry e;
    memset(&e, 0, sizeof e);
    e.flags = flags, e.records = records, e.year = year, e.rating = rating, e.credits = credits, e.part_b = part_b, e.part_c = part_c;
    return e;
}

int main(void) {
    // The panels in a window with the picture at the corner, and moved by the big window's offset (0x77, 0x50).
    int32_t r[6][4];
    famewin_rects(r, 0, 0);
    EXPECT_RECT(r[0], 0x1e, 0x10e, 0x123, 0x147);
    EXPECT_RECT(r[3], 0x15, 0x7b, 0x303, 0xed);
    EXPECT_RECT(r[5], 0x128, 0x187, 0x224, 0x248);
    famewin_rects(r, 0x77, 0x50);
    EXPECT_RECT(r[0], 0x95, 0x15e, 0x19a, 0x197);
    EXPECT_RECT(r[1], 0x1a6, 0x15d, 0x294, 0x1d3);
    EXPECT_RECT(r[2], 0x29f, 0x15d, 0x371, 0x26f);
    EXPECT_RECT(r[3], 0x8c, 0xcb, 0x37a, 0x13d);
    EXPECT_RECT(r[4], 0xc3, 0x77, 0x34b, 0xc7);
    EXPECT_RECT(r[5], 0x19f, 0x1d7, 0x29b, 0x298);

    // The six record buttons: the top panel (0x4c to 0x2d4) in six cells of 108, a button 0x4f in each, centred (14 in);
    // the panel is 0x50 high, so the button sits 0 in.
    EXPECT(famewin_cell_width(0x4c, 0x2d4, 6), 108);
    EXPECT(famewin_cell_inset(108, 0x4f), 14);
    EXPECT(famewin_cell_inset(0x77 - 0x27, 0x4f), 0);
    // The seven portraits: the second panel (0x15 to 0x303) in seven cells of 107, a portrait 0x46 in each (18 in), 22 down.
    EXPECT(famewin_cell_width(0x15, 0x303, 7), 107);
    EXPECT(famewin_cell_inset(107, 0x46), 18);
    EXPECT(famewin_cell_inset(0xed - 0x7b, 0x46), 22);
    // Halves round toward zero.
    EXPECT(famewin_cell_inset(0x4f, 0x50), 0);
    EXPECT(famewin_cell_inset(10, 21), -5);

    // A line of a font: the ascent when the font has no extra, else its height and the extra.
    EXPECT(famewin_line_height(-1, 12, 15), 12);
    EXPECT(famewin_line_height(0, 12, 15), 15);
    EXPECT(famewin_line_height(2, 12, 15), 17);

    // The window: 800 by 600 at the corner, or 1024 by 768 across the middle with y a whole `screen_h - 0x300`.
    struct famewin_position p;
    famewin_position(&p, 0, 1024, 768);
    EXPECT(p.x, 0), EXPECT(p.y, 0), EXPECT(p.w, 0x320), EXPECT(p.h, 0x258), EXPECT(p.offset_x, 0), EXPECT(p.offset_y, 0);
    famewin_position(&p, 1, 1280, 1024);
    EXPECT(p.x, 128), EXPECT(p.y, 256), EXPECT(p.w, 0x400), EXPECT(p.h, 0x300), EXPECT(p.offset_x, 0x77), EXPECT(p.offset_y, 0x50);
    famewin_position(&p, 1, 1023, 768);
    EXPECT(p.x, 0);  // minus one half, toward zero

    // The box behind a record's heading: its text is centred on the offset plus 0x186.
    int left, width;
    famewin_banner(&left, &width, 0x77, 100);
    EXPECT(left, 0x77 - 50 + 0x186), EXPECT(width, 120);
    famewin_banner(&left, &width, 0, 101);
    EXPECT(left, -50 + 0x186), EXPECT(width, 121);

    // Where a game of a rating takes its place among the five: the last place it finds that it beats (or that is empty).
    struct fame_entry row[5];
    for (int i = 0; i < 5; i++) row[i] = make(1, 0, 0, 90 - 10 * i, 0, 0, 0);
    EXPECT(famewin_place(row, 85), 1);
    EXPECT(famewin_place(row, 90), 0);
    EXPECT(famewin_place(row, 50), 4);
    EXPECT(famewin_place(row, 49), FAME_NO_PLACE);
    row[2].flags = 0, row[3].flags = 0, row[4].flags = 0;
    EXPECT(famewin_place(row, 10), 2);  // the first empty place, after the two it does not beat
    row[0].flags = 0;
    EXPECT(famewin_place(row, 0), 0);
    // A list out of order is read as it comes: place 0 (10) is beaten by 30 though place 1 (90) and 3 (90) are not.
    int rating[5] = {10, 90, 20, 90, 90};
    for (int i = 0; i < 5; i++) row[i] = make(1, 0, 0, rating[i], 0, 0, 0);
    EXPECT(famewin_place(row, 30), 0);

    // How a game ends: ironman from the rules, then the first of the ways it was won.
    EXPECT(famewin_flags(0, 0, 0), 1);
    EXPECT(famewin_flags(0, 0x400, 0), 3);
    EXPECT(famewin_flags(0x2000, 0, 0), 9);
    EXPECT(famewin_flags(0x400000, 0, 0), 0x11);
    EXPECT(famewin_flags(0x200000, 0, 0), 0x21);
    EXPECT(famewin_flags(0, 0, 1), 5);
    EXPECT(famewin_flags(0x10, 0, 0), 0x41);
    EXPECT(famewin_flags(0x2000 | 0x400000, 0x400, 1), 0xb);
    EXPECT(famewin_flags(0x400000 | 0x200000, 0, 1), 0x11);
    EXPECT(famewin_flags(0x200000 | 0x10, 0, 1), 0x21);
    EXPECT(famewin_flags(0x10, 0, 1), 5);  // transcended is checked before the scenario

    // The records. A list of nothing but the new game: it holds the first four, and the date record of its kind.
    struct fame_entry table[FAME_ENTRIES];
    memset(table, 0, sizeof table);
    struct fame_entry *n = &table[2 * FAME_RANKS + 1];
    *n = make(9, 0, 2300, 500, 70, 80, 90);  // in use, conquest
    famewin_award(table, n, 2, 1);
    EXPECT(n->records, 0x1f);
    n->records = 0;
    n->flags = 5;  // transcendence
    famewin_award(table, n, 2, 1);
    EXPECT(n->records, 0x2f);

    // With others: a game of the same row at place 0 holds the rating record and a worse credits record; another row's game
    // holds the part_c record and the conquest date.
    memset(table, 0, sizeof table);
    table[2 * FAME_RANKS + 0] = make(1, FAME_RECORD_RATING | FAME_RECORD_CREDITS, 2200, 400, 100, 5, 5);
    table[5 * FAME_RANKS + 3] = make(9, FAME_RECORD_C | FAME_RECORD_CONQUEST | FAME_RECORD_B, 2250, 300, 10, 100, 200);
    table[0] = make(1, 0, 2100, 450, 0, 0, 0);  // in use, no records
    n = &table[2 * FAME_RANKS + 1];
    *n = make(9, 0, 2240, 450, 50, 80, 150);  // conquest game
    famewin_award(table, n, 2, 1);
    // Rating 450 beats 400 (taken); credits 50 < 100 (kept by the other); part_c 150 < 200 (kept); part_b 80 < 100 (kept);
    // conquest in 2240 is earlier than 2250 (taken).
    EXPECT(n->records, FAME_RECORD_RATING | FAME_RECORD_CONQUEST);
    EXPECT(table[2 * FAME_RANKS + 0].records, FAME_RECORD_CREDITS);
    EXPECT(table[5 * FAME_RANKS + 3].records, FAME_RECORD_C | FAME_RECORD_B);
    // A tie takes the record, too; a later game of the kind does not take the date record.
    memset(table, 0, sizeof table);
    table[0] = make(9, FAME_RECORD_B | FAME_RECORD_CONQUEST, 2000, 1, 1, 80, 1);
    n = &table[FAME_RANKS];
    *n = make(9, 0, 2001, 0, 0, 80, 0);
    famewin_award(table, n, 1, 0);
    EXPECT(n->records, FAME_RECORD_B);
    EXPECT(table[0].records, FAME_RECORD_CONQUEST);
    // An unused entry holds no record to take.
    memset(table, 0, sizeof table);
    table[0] = make(0, 0xff, 1, 1000, 1000, 1000, 1000);
    n = &table[3];
    *n = make(1, 0, 5, 1, 1, 1, 1);
    famewin_award(table, n, 0, 3);
    EXPECT(n->records, 0xf);
    EXPECT(table[0].records, 0xff);

    // Which game a record's button shows: the first in use that holds the bit.
    memset(table, 0, sizeof table);
    table[4 * FAME_RANKS + 2] = make(1, 4, 0, 0, 0, 0, 0);
    table[6 * FAME_RANKS + 0] = make(1, 4, 0, 0, 0, 0, 0);
    table[1] = make(0, 4, 0, 0, 0, 0, 0);  // not in use
    int srow = -1, spos = -1;
    EXPECT(famewin_find_record(table, 2, &srow, &spos), 1);
    EXPECT(srow, 4), EXPECT(spos, 2);
    EXPECT(famewin_find_record(table, 3, &srow, &spos), 0);
    EXPECT(srow, 4);  // untouched

    // The entry index: place `rank` of faction `sel`.
    EXPECT(famewin_entry_index(1, 0), 0);
    EXPECT(famewin_entry_index(3, 2), 12);
    EXPECT(famewin_entry_index(7, 4), 34);
    EXPECT(famewin_entry_index(0, -1), -6);

    // The copy leaves the two spare bytes alone and copies the strings with strncpy's padding.
    struct fame_entry a = make(0x0b, 0x13, 2250, 100, 200, 300, 400);
    a.part_a = 7, a.difficulty = 3, a.planet = 2, a.pad_6a[0] = 0x55;
    strcpy(a.title, "Sister"), strcpy(a.name, "Miriam"), strcpy(a.noun, "Believers");
    for (int i = 0; i < 8; i++) a.tail[i] = i + 1;
    struct fame_entry b;
    memset(&b, 0xee, sizeof b);
    famewin_copy(&b, &a);
    EXPECT(b.flags, 0x0b), EXPECT(b.records, 0x13), EXPECT(b.year, 2250), EXPECT(b.part_a, 7), EXPECT(b.part_c, 400);
    EXPECT(b.difficulty, 3), EXPECT(b.planet, 2), EXPECT(strcmp(b.title, "Sister"), 0), EXPECT(strcmp(b.noun, "Believers"), 0);
    EXPECT(b.title[10], 0), EXPECT(b.pad_6a[0], 0xee), EXPECT(b.tail[7], 8), EXPECT(b.pad_2[1], 0xee);

    // The file name of a picture: the pieces and the numbers in base ten.
    char name[64];
    famewin_file_name(name, "fame", 3, "_", 12, ".pcx");
    EXPECT(strcmp(name, "fame3_12.pcx"), 0);

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("ui_famewin: ok\n");
    return 0;
}
