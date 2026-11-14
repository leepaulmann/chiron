// Unit test for the production picker's pure parts (src/ui/prodpicker.c): no game files needed. The numbers are worked out by
// hand from the original's code, for a base screen 1024 wide on a screen 1024 wide, cells 108 high.
#include <stdio.h>
#include <string.h>

#include "../src/ui/prodpicker.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

int main(void) {
    // The grid: a window less 0x51 is 943, a fifth of it 188 a cell, five to a row (943 / 188), four rows on a wide screen.
    struct prodpicker_grid g;
    prodpicker_grid(&g, 1024, 1024, 108);
    EXPECT(g.cell_w, 188), EXPECT(g.cell_h, 108), EXPECT(g.per_row, 5), EXPECT(g.rows, 4), EXPECT(g.visible, 20);
    // An 800 pixel screen (not past 0x320) has three rows: 719 / 5 = 143, 719 / 143 = 5.
    prodpicker_grid(&g, 800, 800, 108);
    EXPECT(g.cell_w, 143), EXPECT(g.per_row, 5), EXPECT(g.rows, 3), EXPECT(g.visible, 15);
    prodpicker_grid(&g, 1280, 1280, 108);
    EXPECT(g.cell_w, 239), EXPECT(g.per_row, 5), EXPECT(g.rows, 4);
    // A narrow window makes a cell too small to fit exactly: 24 / 5 = 4 a cell, and 24 / 4 = 6 of them to a row.
    prodpicker_grid(&g, 105, 800, 108);
    EXPECT(g.cell_w, 4), EXPECT(g.per_row, 6), EXPECT(g.visible, 18);

    // The cells' rectangles: the first at 0x17 + 4 across and 4 down, the frame three short of the next cell's start.
    prodpicker_grid(&g, 1024, 1024, 108);
    struct ui_rect r;
    prodpicker_cell_rect(&r, &g, 0);
    EXPECT(r.left, 27), EXPECT(r.top, 4), EXPECT(r.right, 212), EXPECT(r.bottom, 109);
    prodpicker_cell_rect(&r, &g, 7);  // second row, third cell
    EXPECT(r.left, 403), EXPECT(r.top, 112), EXPECT(r.right, 588), EXPECT(r.bottom, 217);
    // A click's cell: the grid's own origin is not taken off.
    EXPECT(prodpicker_click_slot(&g, 410, 130), 7);
    EXPECT(prodpicker_click_slot(&g, 0, 0), 0);
    EXPECT(prodpicker_click_slot(&g, 939, 107), 4);
    EXPECT(prodpicker_click_slot(&g, 5, 330), 15);

    // The fonts: a twelve of the cell's width, at least 24 and at least 10.
    int a, b;
    prodpicker_font_sizes(188, &a, &b);
    EXPECT(a, 24), EXPECT(b, 12);
    prodpicker_font_sizes(143, &a, &b);
    EXPECT(a, 24), EXPECT(b, 10);
    prodpicker_font_sizes(450, &a, &b);
    EXPECT(a, 30), EXPECT(b, 30);

    // The kinds: a custom design not built (number 0x40 and over, flag 4 clear) is 1, else its plan: 3, 7 and 8 give 0, 9 and 10 give 2,
    // 11 gives 1, anything else 3.
    EXPECT(prodpicker_unit_kind(3, 0, 3), 0);
    EXPECT(prodpicker_unit_kind(3, 0, 4), 3);
    EXPECT(prodpicker_unit_kind(3, 0, 5), 3);
    EXPECT(prodpicker_unit_kind(3, 0, 6), 3);
    EXPECT(prodpicker_unit_kind(3, 0, 7), 0);
    EXPECT(prodpicker_unit_kind(3, 0, 8), 0);
    EXPECT(prodpicker_unit_kind(3, 0, 9), 2);
    EXPECT(prodpicker_unit_kind(3, 0, 10), 2);
    EXPECT(prodpicker_unit_kind(3, 0, 11), 1);
    EXPECT(prodpicker_unit_kind(3, 0, 2), 3);
    EXPECT(prodpicker_unit_kind(0x40, 0, 3), 1);
    EXPECT(prodpicker_unit_kind(0x40, 4, 9), 2);
    EXPECT(prodpicker_unit_kind(0x3f, 0, 9), 2);
    // Facilities: below 3 and from 0x45 (the table ends at 0x44) up to 0x45 give 2, 0x46 and over 1; between, the table's.
    EXPECT(prodpicker_facility_kind(0), 2);
    EXPECT(prodpicker_facility_kind(2), 2);
    EXPECT(prodpicker_facility_kind(3), 0);
    EXPECT(prodpicker_facility_kind(4), 3);
    EXPECT(prodpicker_facility_kind(5), 2);
    EXPECT(prodpicker_facility_kind(8), 1);
    EXPECT(prodpicker_facility_kind(9), 2);
    EXPECT(prodpicker_facility_kind(15), 1);
    EXPECT(prodpicker_facility_kind(17), 1);
    EXPECT(prodpicker_facility_kind(18), 2);
    EXPECT(prodpicker_facility_kind(27), 3);
    EXPECT(prodpicker_facility_kind(30), 3);
    EXPECT(prodpicker_facility_kind(31), 0);
    EXPECT(prodpicker_facility_kind(32), 0);
    EXPECT(prodpicker_facility_kind(33), 2);
    EXPECT(prodpicker_facility_kind(67), 2);
    EXPECT(prodpicker_facility_kind(68), 3);
    EXPECT(prodpicker_facility_kind(0x45), 2);
    EXPECT(prodpicker_facility_kind(0x46), 1);
    EXPECT(prodpicker_facility_kind(0x80), 1);

    // The turns: rounded up; clamped to 0 and a limit, or not clamped (limit -1).
    EXPECT(prodpicker_turns(3, 10, 0xf423f), 4);
    EXPECT(prodpicker_turns(5, 0, 0xf423f), 0);
    EXPECT(prodpicker_turns(5, -20, 0xf423f), 0);
    EXPECT(prodpicker_turns(5, -20, -1), -3);
    EXPECT(prodpicker_turns(1, 2000000, 0xf423f), 0xf423f);
    EXPECT(prodpicker_turns(2, 40000, 9999), 9999);
    // The minerals carried over to another kind of thing: all up to the threshold, past it a share.
    EXPECT(prodpicker_retool_stock(100, 40, 25), 85);
    EXPECT(prodpicker_retool_stock(30, 40, 25), 30);
    EXPECT(prodpicker_retool_stock(40, 40, 25), 40);
    EXPECT(prodpicker_retool_stock(41, 40, 50), 40);  // (1 * 50) / 100 is 0

    // The row of buttons.
    EXPECT(prodpicker_button_count(1, 1, 0, 0), 6);
    EXPECT(prodpicker_button_count(0, 0, 0x40, 0x80000001u), 5);
    EXPECT(prodpicker_button_count(0, 1, 0, 0x80000001u), 4);
    EXPECT(prodpicker_button_count(0, 1, 0x40, 0), 4);
    EXPECT(prodpicker_button_count(0, 1, 0x40, 0x80000000u), 4);
    EXPECT(prodpicker_button_count(0, 1, 0x40, 1), 4);
    EXPECT(prodpicker_button_count(0, 1, 0x40, 0x80000001u), 5);
    EXPECT(prodpicker_row_start(1024, 100, 5), 650);
    EXPECT(prodpicker_row_start(1024, 100, 4), 599);
    EXPECT(prodpicker_row_start(50, 100, 5), 164);  // (50 - 515 - 32) / 2 truncates towards zero
    EXPECT(prodpicker_button_step(100), -103);

    // The keys.
    EXPECT(prodpicker_key_action(0x21), PPK_PGUP);
    EXPECT(prodpicker_key_action(0x22), PPK_PGDN);
    EXPECT(prodpicker_key_action(0x23), PPK_END);
    EXPECT(prodpicker_key_action(0x24), PPK_HOME);
    EXPECT(prodpicker_key_action(0x25), PPK_LEFT);
    EXPECT(prodpicker_key_action(0x26), PPK_UP);
    EXPECT(prodpicker_key_action(0x27), PPK_RIGHT);
    EXPECT(prodpicker_key_action(0x28), PPK_DOWN);
    EXPECT(prodpicker_key_action(0x62), PPK_DOWN);
    EXPECT(prodpicker_key_action(0x64), PPK_LEFT);
    EXPECT(prodpicker_key_action(0x66), PPK_RIGHT);
    EXPECT(prodpicker_key_action(0x68), PPK_UP);
    EXPECT(prodpicker_key_action(0x20), PPK_NONE);
    EXPECT(prodpicker_key_action(0x0d), PPK_NONE);
    EXPECT(prodpicker_key_action(0x61), PPK_NONE);
    // Stepping sideways stays in the row (the last has 2 of its 5: 12 choices).
    EXPECT(prodpicker_step_sideways(7, -1, 5, 12), 6);
    EXPECT(prodpicker_step_sideways(5, -1, 5, 12), 5);
    EXPECT(prodpicker_step_sideways(9, 1, 5, 12), 9);
    EXPECT(prodpicker_step_sideways(10, 1, 5, 12), 11);
    EXPECT(prodpicker_step_sideways(11, 1, 5, 12), 11);
    EXPECT(prodpicker_step_sideways(-1, 1, 5, 12), 0);

    // The pictures: 2/3 of 108 is 72 for a unit and 54 for its model; 3/7 of it 46 for a facility; centred in the first cell.
    EXPECT(prodpicker_unit_art(108), 72);
    EXPECT(prodpicker_unit_model(108), 54);
    EXPECT(prodpicker_facility_art(108), 46);
    struct ui_rect cell = {27, 4, 212, 109};
    int x, y;
    prodpicker_art_origin(&cell, 72, 54, &x, &y);
    EXPECT(x, 83), EXPECT(y, 29);
    prodpicker_art_origin(&cell, 46, 46, &x, &y);
    EXPECT(x, 96), EXPECT(y, 33);

    // The right click's menu offers to hide a design that is not a facility, not 15 and without a psi weapon.
    EXPECT(prodpicker_menu_offers_hide(-1, 0), 0);
    EXPECT(prodpicker_menu_offers_hide(0, 1), 1);
    EXPECT(prodpicker_menu_offers_hide(0x10, -1), 0);
    EXPECT(prodpicker_menu_offers_hide(15, 1), 0);
    EXPECT(prodpicker_menu_offers_hide(0x40, -5), 1);
    EXPECT(prodpicker_menu_offers_hide(0x3f, 0), 1);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
