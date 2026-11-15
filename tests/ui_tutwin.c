// Unit test for the tutorial window's pure helpers (src/ui/tutwin.c): no game files needed. The numbers are what the original's
// own arithmetic gives (its place for a note, its test of whether a note is due, the centre of a rectangle), worked out by hand
// from its code: the divisions by 2 and 8 go toward zero, as its shifts with a bias do.
#include <stdio.h>

#include "../src/ui/tutwin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

static void expect_place(int line, int mode, int w, int h, int tx, int ty, int pw, int ph, int x, int y, int tail_x, int tail_y) {
    struct tutwin_place p;
    tutwin_place(&p, mode, w, h, tx, ty, pw, ph);
    if (p.x != x || p.y != y || p.tail_x != tail_x || p.tail_y != tail_y) {
        printf("FAIL line %d: mode %d point (%d, %d): place = (%d, %d) tail (%d, %d), expected (%d, %d) tail (%d, %d)\n", line, mode, tx,
               ty, p.x, p.y, p.tail_x, p.tail_y, x, y, tail_x, tail_y);
        failures++;
    }
}
#define EXPECT_PLACE(mode, w, h, tx, ty, pw, ph, x, y, tail_x, tail_y) \
    expect_place(__LINE__, mode, w, h, tx, ty, pw, ph, x, y, tail_x, tail_y)

int main(void) {
    // A 1024 by 768 screen and a popup 300 by 120. Mode 0: the side limit is 512 + 128 = 640 and the height limit 768 * 5 / 8 = 480.
    // A point in the upper left: the popup stands at the left (x 5, the arrow from 150 + 5) and at the bottom (768 - 120 - 8).
    EXPECT_PLACE(0, 1024, 768, 100, 100, 300, 120, 5, 640, 155, 640);
    // At the lower right: at the right (1024 - 300 - 5; the arrow from 1024 - 150 - 5) and, the point being below the limit, at an
    // eighth of the point's y (75) with the arrow from its bottom edge (75 + 120).
    EXPECT_PLACE(0, 1024, 768, 900, 600, 300, 120, 719, 75, 869, 195);
    // On the limits themselves the note stays left and low (the tests are "not above").
    EXPECT_PLACE(0, 1024, 768, 640, 480, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(0, 1024, 768, 641, 481, 300, 120, 719, 60, 869, 180);

    // The other modes move the limits. Mode 1 and 2 are mode 0's (one eighth added to the side). Mode 3 takes an eighth from it
    // (512 - 128 = 384), mode 4 does too and takes an eighth (96) from the height (480 - 96 = 384), mode 5 has the added eighth and
    // the lower height, mode 6 neither.
    EXPECT_PLACE(1, 1024, 768, 640, 480, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(2, 1024, 768, 640, 480, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(3, 1024, 768, 385, 480, 300, 120, 719, 640, 869, 640);
    EXPECT_PLACE(3, 1024, 768, 384, 480, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(4, 1024, 768, 384, 384, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(4, 1024, 768, 384, 385, 300, 120, 5, 48, 155, 168);
    EXPECT_PLACE(5, 1024, 768, 640, 384, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(5, 1024, 768, 641, 385, 300, 120, 719, 48, 869, 168);
    EXPECT_PLACE(6, 1024, 768, 512, 480, 300, 120, 5, 640, 155, 640);
    EXPECT_PLACE(6, 1024, 768, 513, 481, 300, 120, 719, 60, 869, 180);
    // A mode below zero is below 3, so it takes mode 0's limits.
    EXPECT_PLACE(-1, 1024, 768, 640, 480, 300, 120, 5, 640, 155, 640);

    // An odd screen (801 by 601) and an odd popup (301 wide, 121 high): half 400, an eighth 100, five eighths of 601 is 375 (3005
    // over 8, toward zero), and the popup's half is 150.
    EXPECT_PLACE(0, 801, 601, 500, 375, 301, 121, 5, 472, 155, 472);   // 601 - 121 - 8 = 472
    EXPECT_PLACE(0, 801, 601, 501, 376, 301, 121, 495, 47, 646, 168);  // 376 / 8 = 47; 801 - 301 - 5 = 495; 801 - 150 - 5 = 646
    EXPECT_PLACE(0, 801, 601, 500, 600, 301, 121, 5, 75, 155, 196);

    // The test of whether a note is due: network game, setting off, already shown, aliens: any of them says no.
    EXPECT(tutwin_note_due(0, 0x20, 0, 4, 0), 1);
    EXPECT(tutwin_note_due(1, 0x20, 0, 4, 0), 0);
    EXPECT(tutwin_note_due(0, 0x00, 0, 4, 0), 0);
    EXPECT(tutwin_note_due(0, 0xdf, 0, 4, 0), 0);  // every setting but bit 0x20
    EXPECT(tutwin_note_due(0, 0x20, 4, 4, 0), 0);
    EXPECT(tutwin_note_due(0, 0x20, 0xfb, 4, 0), 1);  // the other notes shown do not matter
    EXPECT(tutwin_note_due(0, 0x20, 4, 0x1004, 0), 0);  // a bit among several that is already shown
    EXPECT(tutwin_note_due(0, 0x20, 0, 4, 0x80), 0);
    EXPECT(tutwin_note_due(0, 0x20, 0, 4, 0x7f), 1);
    EXPECT(tutwin_note_due(0, 0x20, 0, 4, 0xffffff7f), 1);

    // The middle of a span: half of its length, toward zero, from its start.
    EXPECT(tutwin_middle(3, 10), 6);
    EXPECT(tutwin_middle(10, 3), 7);
    EXPECT(tutwin_middle(0, 0), 0);
    EXPECT(tutwin_middle(-5, 4), -1);  // 9 / 2 = 4
    EXPECT(tutwin_middle(4, -5), 0);   // -9 / 2 = -4
    EXPECT(tutwin_middle(100, 101), 100);

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("tutwin: all passed\n");
    return 0;
}
