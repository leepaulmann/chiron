// Unit test for the research picker's pure parts (src/ui/picktech.c): no game files needed. The numbers are worked out by
// hand from the original's code for a 1024 by 768 screen.
#include <stdio.h>
#include <string.h>

#include "../src/ui/picktech.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

static int width_of(void *ctx, const char *s) { return (int)strlen(s) * *(int *)ctx; }

int main(void) {
    // Columns: one to five for the first five, then the table's, and ten from forty.
    EXPECT(picktech_columns(1), 1);
    EXPECT(picktech_columns(5), 5);
    EXPECT(picktech_columns(6), 3);
    EXPECT(picktech_columns(11), 6);
    EXPECT(picktech_columns(29), 8);
    EXPECT(picktech_columns(39), 8);
    EXPECT(picktech_columns(40), 10);
    EXPECT(picktech_columns(89), 10);

    // Five techs: one row; cells 1024 / 5 - 20 = 184 wide, 276 high, made half the screen high (384).
    struct picktech_grid g;
    picktech_grid(&g, 5, 1024, 768);
    EXPECT(g.cols, 5), EXPECT(g.rows, 1), EXPECT(g.cell_w, 184), EXPECT(g.cell_h, 384), EXPECT(g.margin, 10);
    EXPECT(g.win_w, 185 * 5 + 1), EXPECT(g.win_h, 385 + 30);
    // Six: three columns, the cells 321 wide and 481 high, two rows of them too tall (991 > 768): 768 / 2 - 40 = 344.
    picktech_grid(&g, 6, 1024, 768);
    EXPECT(g.cols, 3), EXPECT(g.rows, 2), EXPECT(g.cell_w, 321), EXPECT(g.cell_h, 344);
    EXPECT(g.win_w, 322 * 3 + 1), EXPECT(g.win_h, 345 * 2 + 30);
    // Twenty: over eight the margin comes off the whole screen first (1004 / 5 = 200), and a squeezed height is (768 - 69) / 4.
    picktech_grid(&g, 20, 1024, 768);
    EXPECT(g.cols, 5), EXPECT(g.rows, 4), EXPECT(g.cell_w, 200), EXPECT(g.cell_h, 174);
    EXPECT(g.win_w, 201 * 5 + 1), EXPECT(g.win_h, 175 * 4 + 30);
    // A wide cell is held to a third of the screen: two techs on a 1024 screen would be 492 wide.
    picktech_grid(&g, 2, 1024, 768);
    EXPECT(g.cell_w, 341);
    // All eighty-nine: ten columns, nine rows.
    picktech_grid(&g, 89, 1024, 768);
    EXPECT(g.cols, 10), EXPECT(g.rows, 9), EXPECT(g.cell_w, 100), EXPECT(g.cell_h, 77);

    // Cells of the twenty grid: index 7 is column 2, row 1; clipped to a buffer the window's size.
    picktech_grid(&g, 20, 1024, 768);
    struct ui_rect r;
    picktech_cell_rect(&r, &g, 7);
    EXPECT(r.left, 403), EXPECT(r.top, 205), EXPECT(r.right, 603), EXPECT(r.bottom, 379);
    picktech_cell_clip(&r, g.win_w, g.win_h);
    EXPECT(r.left, 403), EXPECT(r.top, 205), EXPECT(r.right, 600), EXPECT(r.bottom, 376);
    picktech_cell_rect(&r, &g, 0);
    picktech_cell_clip(&r, g.win_w, g.win_h);
    EXPECT(r.left, 3), EXPECT(r.top, 30), EXPECT(r.right, 198), EXPECT(r.bottom, 201);
    // The last cell ends at 4 * 201 + 1 + 200 = 1005 and 3 * 175 + 30 + 174 = 729; three in from that is 1002 and 726, inside the
    // 1006 by 730 window's own margin of three (1003 and 727).
    picktech_cell_rect(&r, &g, 19);
    picktech_cell_clip(&r, g.win_w, g.win_h);
    EXPECT(r.right, 1002), EXPECT(r.bottom, 726);

    // Hit tests: the grid starts 0x1d down; its edges are exclusive; the gap between cells belongs to the cell before it.
    EXPECT(picktech_cell_at(&g, 403, 0x1d + 175), 7);
    EXPECT(picktech_cell_at(&g, 1004, 0x1d), 4);
    EXPECT(picktech_cell_at(&g, 1005, 0x1d), -1);
    EXPECT(picktech_cell_at(&g, 0, 0x1c), -1);
    EXPECT(picktech_cell_at(&g, -1, 0x1d), -1);
    EXPECT(picktech_cell_at(&g, 0, 0x1d + 4 * 175 - 1), 15);
    EXPECT(picktech_cell_at(&g, 0, 0x1d + 4 * 175), -1);
    EXPECT(picktech_cell_at(&g, 200, 0x1d), 0);
    EXPECT(picktech_cell_at(&g, 201, 0x1d), 1);

    // Keys.
    EXPECT(picktech_key_action(0x25), PTK_LEFT), EXPECT(picktech_key_action(0x64), PTK_LEFT), EXPECT(picktech_key_action(8), PTK_LEFT);
    EXPECT(picktech_key_action(0x27), PTK_RIGHT), EXPECT(picktech_key_action(9), PTK_RIGHT), EXPECT(picktech_key_action(0x66), PTK_RIGHT);
    EXPECT(picktech_key_action(0x26), PTK_UP), EXPECT(picktech_key_action(0x68), PTK_UP);
    EXPECT(picktech_key_action(0x28), PTK_DOWN), EXPECT(picktech_key_action(0x62), PTK_DOWN);
    EXPECT(picktech_key_action(0x0d), PTK_CHOOSE), EXPECT(picktech_key_action(0x20), PTK_CHOOSE);
    EXPECT(picktech_key_action(0x48), PTK_CHOOSE), EXPECT(picktech_key_action(0x70), PTK_CHOOSE), EXPECT(picktech_key_action(0x1000d), PTK_CHOOSE);
    EXPECT(picktech_key_action(0x1b), PTK_NONE), EXPECT(picktech_key_action(0x41), PTK_NONE), EXPECT(picktech_key_action(7), PTK_NONE);
    EXPECT(picktech_key_action(0x29), PTK_NONE), EXPECT(picktech_key_action(0x63), PTK_NONE), EXPECT(picktech_key_action(0x67), PTK_NONE);
    int col = 0, row = 0;
    picktech_key_step(&col, &row, PTK_LEFT, 5, 4);
    EXPECT(col, 4);
    picktech_key_step(&col, &row, PTK_RIGHT, 5, 4);
    EXPECT(col, 0);
    picktech_key_step(&col, &row, PTK_UP, 5, 4);
    EXPECT(row, 3);
    picktech_key_step(&col, &row, PTK_DOWN, 5, 4);
    EXPECT(row, 0);
    picktech_key_step(&col, &row, PTK_NONE, 5, 4);
    EXPECT(col, 0), EXPECT(row, 0);

    // Fonts: up to eight techs the small print is 12 (14 on other screens than 800 wide), the hint font two or four larger.
    int size, hint;
    picktech_fonts(8, 0x320, &size, &hint);
    EXPECT(size, 12), EXPECT(hint, 14);
    picktech_fonts(8, 1024, &size, &hint);
    EXPECT(size, 14), EXPECT(hint, 18);
    picktech_fonts(9, 1024, &size, &hint);
    EXPECT(size, 10), EXPECT(hint, 12);
    EXPECT(picktech_card_font(8, 0x320), 0x12), EXPECT(picktech_card_font(8, 1024), 0x14), EXPECT(picktech_card_font(9, 1024), 0xc);

    // The icon: the step by cell width, then the size from it (215 * (step + 16) / 16, toward zero) or from the game's scale.
    EXPECT(picktech_icon_step(0x320, 0xf0), -12), EXPECT(picktech_icon_step(0x320, 0xef), -14);
    EXPECT(picktech_icon_step(1024, 0xb4), -10), EXPECT(picktech_icon_step(1024, 0xb3), -12);
    EXPECT(picktech_icon_step(1024, 0x8d), -12), EXPECT(picktech_icon_step(1024, 0x8c), -14);
    EXPECT(picktech_icon_size(0, 1, -10), 80), EXPECT(picktech_icon_size(0, 1, -14), 26), EXPECT(picktech_icon_size(0, 1, -12), 53);
    EXPECT(picktech_icon_size(3, 2, -10), 322);
    EXPECT(picktech_icon_size(0, 1, -20), -53);  // a negative size rounds toward zero (-860 / 16 = -53.75)

    // The rolls: 2 * s / 3 + 1 values from 0, added to (s + 2) / 3.
    EXPECT(picktech_roll_range(10), 7), EXPECT(picktech_roll_base(10), 4);
    EXPECT(picktech_roll_range(0), 1), EXPECT(picktech_roll_base(0), 0);
    EXPECT(picktech_roll_range(-3), -1), EXPECT(picktech_roll_base(-3), 0);

    // Blind research: every third (counted from the ranking and the faction) is left out, but the first seven techs of a
    // faction with no ranking stay.
    EXPECT(picktech_third_listed(5, 0, 1), 1);
    EXPECT(picktech_third_listed(8, 0, 1), 0);
    EXPECT(picktech_third_listed(7, 0, 1), 1);
    EXPECT(picktech_third_listed(5, 2, 1), 1);   // (5 - 2 + 1) % 3 = 1: listed
    EXPECT(picktech_third_listed(4, 2, 1), 0);   // (4 - 2 + 1) % 3 = 0, and a faction with a ranking has no first techs
    EXPECT(picktech_third_listed(8, 2, 1), 1);   // (8 - 2 + 1) % 3 = 1

    // Breaking a heading: it is cut where it fits (10 a letter, 50 wide) and a space put in, unless it has one or fits.
    int ten = 10;
    char out[64];
    picktech_break_line(out, "ABCDEFGHIJ", 50, width_of, &ten);
    EXPECT(strcmp(out, "ABCDE FGHIJ"), 0);
    picktech_break_line(out, "ABCDE FGHIJ", 50, width_of, &ten);
    EXPECT(strcmp(out, "ABCDE FGHIJ"), 0);
    picktech_break_line(out, "ABCDE", 50, width_of, &ten);
    EXPECT(strcmp(out, "ABCDE"), 0);
    picktech_break_line(out, "ABCDEF", 50, width_of, &ten);
    EXPECT(strcmp(out, "ABCDE F"), 0);
    // Nothing fits: all is cut away, a space is put, and the rest follows (the text from one before the end of the one-space string).
    picktech_break_line(out, "ABC", 0, width_of, &ten);
    EXPECT(strcmp(out, " ABC"), 0);

    if (failures) return 1;
    printf("ui_picktech: ok\n");
    return 0;
}
