// Unit test for the part picker's pure arithmetic (src/ui/selpart.c): no game files needed. The numbers are worked
// out by hand from the original's code: cells 108 across and 94 down, a part drawn from 0x17 to 0x80 within its cell.
#include <stdio.h>
#include <string.h>

#include "../src/ui/selpart.h"

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
        printf("FAIL line %d: %s = {%d, %d, %d, %d}, expected {%d, %d, %d, %d}\n", line, what, r->left, r->top, r->right, r->bottom, left, top, right,
               bottom);
        failures++;
    }
}
#define EXPECT_RECT(r, l, t, rr, b) expect_rect(__LINE__, #r, &(r), l, t, rr, b)

int main(void) {
    // The cells: slot 0 is at (4, 4) with its part from 27 to 132 across and 91 high; the next across is 108 on; the
    // sixth part starts the next row, 94 down.
    struct ui_rect r;
    selpart_cell_rect(&r, 0, 5);
    EXPECT_RECT(r, 27, 4, 132, 95);
    selpart_cell_rect(&r, 1, 5);
    EXPECT_RECT(r, 135, 4, 240, 95);
    selpart_cell_rect(&r, 4, 5);
    EXPECT_RECT(r, 459, 4, 564, 95);
    selpart_cell_rect(&r, 5, 5);
    EXPECT_RECT(r, 27, 98, 132, 189);
    selpart_cell_rect(&r, 19, 5);
    EXPECT_RECT(r, 459, 286, 564, 377);

    // Hit test: the grid spans (4, 4) to (544, 380) for five columns and four rows, from the cell's left edge (not the
    // part's), and the index counts from the first part on the page.
    EXPECT(selpart_hit(3, 10, 5, 4, 0), -1);
    EXPECT(selpart_hit(10, 3, 5, 4, 0), -1);
    EXPECT(selpart_hit(4, 4, 5, 4, 0), 0);
    EXPECT(selpart_hit(111, 97, 5, 4, 0), 0);
    EXPECT(selpart_hit(112, 97, 5, 4, 0), 1);
    EXPECT(selpart_hit(111, 98, 5, 4, 0), 5);
    EXPECT(selpart_hit(543, 379, 5, 4, 0), 19);
    EXPECT(selpart_hit(544, 379, 5, 4, 0), -1);
    EXPECT(selpart_hit(543, 380, 5, 4, 0), -1);
    EXPECT(selpart_hit(200, 100, 5, 4, 10), 16);

    // Left and Right stay in the row: row 1 of 12 parts is places 5 to 9; the last row is cut short at 11.
    EXPECT(selpart_step_sideways(7, -1, 5, 12), 6);
    EXPECT(selpart_step_sideways(5, -1, 5, 12), 5);
    EXPECT(selpart_step_sideways(9, 1, 5, 12), 9);
    EXPECT(selpart_step_sideways(8, 1, 5, 12), 9);
    EXPECT(selpart_step_sideways(10, 1, 5, 12), 11);
    EXPECT(selpart_step_sideways(11, 1, 5, 12), 11);
    EXPECT(selpart_step_sideways(11, -1, 5, 12), 10);
    EXPECT(selpart_step_sideways(-1, 1, 5, 12), 0);  // no hover: the first row's start
    EXPECT(selpart_step_sideways(-1, -1, 5, 12), 0);

    // Up and Down move a row; a hover beyond the page is brought back row by row, one above the page is taken down.
    EXPECT(selpart_step_up(12, 0, 5, 20), 7);
    EXPECT(selpart_step_up(45, 0, 5, 20), 15);  // 45 - 5 = 40, 35, 30, 25, 20, then 15: the first on the page
    EXPECT(selpart_step_up(7, 5, 5, 20), 2);
    EXPECT(selpart_step_down(7, 0, 5), 12);
    EXPECT(selpart_step_down(2, 20, 5), 22);  // 7, 12, 17, 22
    EXPECT(selpart_step_down(-1, 0, 5), 4);

    // End: when the parts overflow the page, the first shown puts the last row at the page's bottom.
    EXPECT(selpart_end_first(0, 26, 5, 20), 10);  // 26 parts end in row 5 (rows of 5): six rows' worth, 30, less a page
    EXPECT(selpart_end_first(0, 25, 5, 20), 5);
    EXPECT(selpart_end_first(0, 20, 5, 20), 0);
    EXPECT(selpart_end_first(3, 18, 5, 20), 3);  // all fit: unchanged

    // Text helpers.
    EXPECT(selpart_line_height(-1, 11, 14), 11);
    EXPECT(selpart_line_height(2, 11, 14), 16);
    EXPECT(selpart_centre(7, 4), 5);
    EXPECT(selpart_centre(-7, 0), -3);  // toward zero
    char out[16];
    selpart_number_text(out, 255);
    EXPECT(strcmp(out, "255"), 0);
    selpart_number_text(out, -3);
    EXPECT(strcmp(out, "-3"), 0);

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("ui_selpart: ok\n");
    return 0;
}
