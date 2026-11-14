// Unit test for the faction picker's layout and text helpers (src/ui/pickwin.c): no game files needed. The numbers
// are those the original's working numbers give for its 790 by 300 window, worked out by hand from its code.
#include <stdio.h>
#include <string.h>

#include "../src/ui/pickwin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

// A rectangle as four numbers.
static void expect_rect(int line, const char *what, const struct ui_rect *r, int left, int top, int right, int bottom) {
    check_(line, what, r->left == left && r->top == top && r->right == right && r->bottom == bottom, 1);
    if (r->left != left || r->top != top || r->right != right || r->bottom != bottom)
        printf("  %s = {%d, %d, %d, %d}, expected {%d, %d, %d, %d}\n", what, r->left, r->top, r->right, r->bottom, left, top, right, bottom);
}
#define EXPECT_RECT(r, l, t, rr, b) expect_rect(__LINE__, #r, &(r), l, t, rr, b)

int main(void) {
    struct pickwin_layout l;
    pickwin_layout(&l, 0x316, 0x12c);

    // The window, its header (inset by 4, 30 high), and the three pictures (0x102 by 0x5a, 2 apart) under it.
    EXPECT_RECT(l.frame, 0, 0, 790, 300);
    EXPECT_RECT(l.header, 4, 4, 786, 34);
    EXPECT_RECT(l.pack[0], 5, 38, 263, 128);
    EXPECT_RECT(l.pack[1], 265, 38, 523, 128);
    EXPECT_RECT(l.pack[2], 525, 38, 783, 128);

    // The list: to the right of the left column, under the pictures, inset by 3 and moved 0x12 left for the row buttons,
    // which are 16 square and 19 apart.
    EXPECT_RECT(l.list, 336, 134, 762, 264);
    EXPECT_RECT(l.row[0], 764, 134, 780, 150);
    EXPECT_RECT(l.row[1], 764, 153, 780, 169);
    EXPECT_RECT(l.row[6], 764, 248, 780, 264);

    // The row of buttons under the list, and the two radio buttons at the left.
    EXPECT_RECT(l.button[PW_BTN_B], 5, 271, 197, 293);
    EXPECT_RECT(l.button[PW_BTN_A], 199, 271, 391, 293);
    EXPECT_RECT(l.button[PW_BTN_OK], 393, 271, 585, 293);
    EXPECT_RECT(l.button[PW_BTN_CANCEL], 587, 271, 779, 293);
    EXPECT_RECT(l.button[PW_BTN_CHOOSE], 6, 132, 222, 154);
    EXPECT_RECT(l.button[PW_BTN_RANDOM], 6, 157, 222, 179);
    EXPECT(l.last_x, 6), EXPECT(l.last_y, 157), EXPECT(l.last_w, 0xd8), EXPECT(l.last_h, 0x16);

    // A window of another size moves what depends on it, and the divisions by three and four go toward zero: here the
    // header is 12 wide inside its margins, so a third of what is left is 2 and the buttons are 3 * 2 / 4 - 1 = 0 wide.
    pickwin_layout(&l, 20, 100);
    EXPECT_RECT(l.header, 4, 4, 16, 34);
    EXPECT(l.button[PW_BTN_B].right - l.button[PW_BTN_B].left, 0);
    EXPECT(l.button[PW_BTN_A].left, l.button[PW_BTN_B].right + 2);

    // The text of a list line.
    char line[64];
    pickwin_item_text(line, "The Gaia's Stepdaughters", "Deirdre Skye", " | ", "(", ")");
    EXPECT(strcmp(line, "The Gaia's Stepdaughters | (Deirdre Skye)"), 0);
    pickwin_item_text(line, "", "", "|", "(", ")");
    EXPECT(strcmp(line, "|()"), 0);

    EXPECT(pickwin_row_y(100, 0), 100), EXPECT(pickwin_row_y(100, 3), 157), EXPECT(pickwin_row_y(100, 6), 214);

    if (failures) printf("%d failure(s)\n", failures);
    else printf("ui_pickwin: ok\n");
    return failures != 0;
}
