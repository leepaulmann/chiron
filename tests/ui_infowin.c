// Unit test for the info panel's pure arithmetic (src/ui/infowin.c): no game files needed. The numbers are worked out by hand
// from the original's code (InfoWin_init, the validity test, the clock's tick, the draw of a line, the ranking and chart boxes).
#include <stdio.h>
#include <string.h>

#include "../src/ui/infowin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

static void expect_rect(int line, const char *what, const struct ui_rect *r, int left, int top, int right, int bottom) {
    if (r->left != left || r->top != top || r->right != right || r->bottom != bottom) {
        printf("FAIL line %d: %s = {%d, %d, %d, %d}, expected {%d, %d, %d, %d}\n", line, what, r->left, r->top, r->right, r->bottom, left, top,
               right, bottom);
        failures++;
    }
}
#define EXPECT_RECT(r, l, t, rr, b) expect_rect(__LINE__, #r, &(r), l, t, rr, b)

int main(void) {
    // The boxes: the area is taken in 7 from the left, top and bottom and 0x27 from the right: {100, 200, 900, 400} is
    // {107, 207, 861, 393}. On an 800-wide screen both boxes are that. On 1024 it is split: (861 - 107 - 3) / 2 = 375 wide
    // for the first, the second as far on as 375 + 3 = 378.
    struct ui_rect a, b, area = {100, 200, 900, 400};
    infowin_layout(&a, &b, &area, 0x320);
    EXPECT_RECT(a, 107, 207, 861, 393);
    EXPECT_RECT(b, 107, 207, 861, 393);
    infowin_layout(&a, &b, &area, 1024);
    EXPECT_RECT(a, 107, 207, 482, 393);
    EXPECT_RECT(b, 485, 207, 860, 393);
    // An area with an odd width: (right - left - 3) = 8 halves to 4.
    struct ui_rect narrow = {0, 0, 7 + 0x27 + 11, 100};
    infowin_layout(&a, &b, &narrow, 1024);
    EXPECT_RECT(a, 7, 7, 11, 93);
    EXPECT_RECT(b, 14, 7, 18, 93);
    // A width that takes the half toward zero: (right - left - 3) = -5 gives -2, not -3.
    struct ui_rect tiny = {0, 0, 7 + 0x27 - 2, 50};
    infowin_layout(&a, &b, &tiny, 1024);
    EXPECT(a.right - a.left, -2);

    EXPECT(infowin_line_height(-1, 13, 15), 13);
    EXPECT(infowin_line_height(4, 13, 15), 19);
    EXPECT(infowin_line_height(0, 13, 15), 15);

    // Which modes can be shown. Mode 0 always, and nothing else before the first turn.
    struct infowin_facts f = {0, 3, 0, 0, 0};
    for (int m = 1; m < 12; m++) EXPECT(infowin_mode_valid(m, &f), 0);
    EXPECT(infowin_mode_valid(0, &f), 1);
    f.turn = 1;
    EXPECT(infowin_mode_valid(1, &f), 0);  // economy: from turn 50
    f.turn = 0x32, f.difficulty = 1;
    EXPECT(infowin_mode_valid(1, &f), 0);  // ... and at that, a faction that is not on its lower levels
    f.difficulty = 2;
    EXPECT(infowin_mode_valid(1, &f), 1);
    f.turn = 0x64, f.difficulty = 0;
    EXPECT(infowin_mode_valid(1, &f), 1);  // ... or any from turn 100
    f.turn = 5, f.contact = 0;
    EXPECT(infowin_mode_valid(2, &f), 0);  // the leader box needs a contact
    EXPECT(infowin_mode_valid(3, &f), 0);
    f.contact = 1;
    EXPECT(infowin_mode_valid(2, &f), 1);
    EXPECT(infowin_mode_valid(3, &f), 1);
    f.contact = 0, f.turn = 0x40, f.pact = 1;
    EXPECT(infowin_mode_valid(3, &f), 1);  // no contact: a pact does it (once past turn 50, or on a higher level)
    f.turn = 0x20, f.difficulty = 1;
    EXPECT(infowin_mode_valid(3, &f), 0);
    f.difficulty = 2;
    EXPECT(infowin_mode_valid(3, &f), 1);
    f.pact = 0;
    EXPECT(infowin_mode_valid(3, &f), 0);
    f.turn = 0x13, f.flags = 0;
    EXPECT(infowin_mode_valid(4, &f), 0);  // the charts from turn 20, or with the flag
    EXPECT(infowin_mode_valid(5, &f), 0);
    f.turn = 0x14;
    EXPECT(infowin_mode_valid(6, &f), 1);
    f.turn = 3, f.flags = 0x10;
    EXPECT(infowin_mode_valid(5, &f), 1);
    EXPECT(infowin_mode_valid(7, &f), 0);  // the planet box needs both of two flags
    f.flags = 0x80;
    EXPECT(infowin_mode_valid(7, &f), 0);
    f.flags = 0xc0;
    EXPECT(infowin_mode_valid(7, &f), 1);
    EXPECT(infowin_mode_valid(8, &f), 0);
    EXPECT(infowin_mode_valid(10, &f), 0);
    EXPECT(infowin_mode_valid(11, &f), 1);
    EXPECT(infowin_mode_valid(12, &f), 0);
    EXPECT(infowin_mode_valid(-1, &f), 0);

    EXPECT(infowin_next_mode(3), 4);
    EXPECT(infowin_next_mode(7), 0);
    EXPECT(infowin_next_mode(-1), 0);

    // The clock: a box of 10 characters is drawn again up to scroll 11, left alone from 12 to 60, and at 61 (past its
    // length and 0x32) holds or moves on.
    EXPECT(infowin_tick_action(1, 10), INFOWIN_TICK_DRAW);
    EXPECT(infowin_tick_action(11, 10), INFOWIN_TICK_DRAW);
    EXPECT(infowin_tick_action(12, 10), INFOWIN_TICK_NOTHING);
    EXPECT(infowin_tick_action(60, 10), INFOWIN_TICK_NOTHING);
    EXPECT(infowin_tick_action(61, 10), INFOWIN_TICK_HOLD);

    // A line of 4 characters with 5 or more left is whole; with 4 or less it is part way; with none, nothing.
    EXPECT(infowin_line_action(5, 4), INFOWIN_LINE_FULL);
    EXPECT(infowin_line_action(4, 4), INFOWIN_LINE_PARTIAL);
    EXPECT(infowin_line_action(1, 4), INFOWIN_LINE_PARTIAL);
    EXPECT(infowin_line_action(0, 4), INFOWIN_LINE_NONE);

    EXPECT(infowin_title_upper(0x2327, 0x2328, 0), 1);
    EXPECT(infowin_title_upper(0x2328, 0x2327, 0), 1);
    EXPECT(infowin_title_upper(0x2328, 0x2328, 0), 0);
    EXPECT(infowin_title_upper(0, 0, 1), 0);

    // The bars: 50 of 200 in a span of 100 is 25 high; a tiny value is one. Seven bars across 107 to 861 (754 / 7 = 107
    // each, 5 left over, 2 of it before the first); in a rectangle of width 6 each is 0 wide and the run starts 3 in.
    EXPECT(infowin_bar_height(50, 100, 200), 25);
    EXPECT(infowin_bar_height(1, 10, 200), 1);
    EXPECT(infowin_bar_height(200, 100, 200), 100);
    int first, width;
    infowin_bar_run(107, 861, &first, &width);
    EXPECT(first, 109);
    EXPECT(width, 107);
    infowin_bar_run(0, 6, &first, &width);
    EXPECT(first, 3);
    EXPECT(width, 0);

    // The chart: 100 turns on 400 columns is every turn; 400 turns on 100 columns, every fourth (200 and 133 still too many,
    // 100 fits). A height that reaches the greatest value is divided by one; 50 for 120 is reached by 3 of them (100 is not
    // enough, 150 is).
    EXPECT(infowin_chart_step(100, 400), 1);
    EXPECT(infowin_chart_step(400, 100), 4);
    EXPECT(infowin_chart_step(101, 100), 2);
    EXPECT(infowin_chart_scale(50, 40), 1);
    EXPECT(infowin_chart_scale(50, 50), 1);
    EXPECT(infowin_chart_scale(50, 51), 2);
    EXPECT(infowin_chart_scale(50, 120), 3);
    EXPECT(infowin_chart_scale(50, 150), 3);
    EXPECT(infowin_chart_scale(50, 151), 4);

    // The planet line: states 0 to 5 are the first six labels, 26 to 29 the four words, the rest names from the table.
    EXPECT(infowin_planet_name(0), 0);
    EXPECT(infowin_planet_name(5), 5);
    EXPECT(infowin_planet_name(6), -1);
    EXPECT(infowin_planet_name(25), -1);
    EXPECT(infowin_planet_name(26), 6);
    EXPECT(infowin_planet_name(29), 9);
    EXPECT(infowin_planet_name(30), -1);
    EXPECT(infowin_planet_name(-1), -1);
    EXPECT(infowin_planet_table_offset(6), 0);
    EXPECT(infowin_planet_table_offset(7), 32);
    EXPECT(infowin_planet_table_offset(25), 0x260);
    EXPECT(infowin_planet_table_offset(30), 0x300);

    if (failures) return 1;
    printf("ui_infowin: ok\n");
    return 0;
}
