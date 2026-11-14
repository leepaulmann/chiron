// Unit test for the Monuments window's pure parts (src/ui/monuwin.c): no game files needed. The numbers are those the original's
// code gives (MonuWin_init's rectangles and cells of the picture sheets, MonuWin_on_redraw's sort and loops), worked out by hand
// from the disassembly.
#include <stdio.h>
#include <string.h>

#include "../src/ui/monuwin.h"

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

int main(void) {
    // The window: 800 by 600 in the corner for the small screen; for the big one 1024 by 768 with x half what the screen has over
    // 1024, and y the whole of what it has over 768 (not halved); the picture at (0x70, 0x54) in it.
    struct monuwin_position p;
    monuwin_position(&p, 0, 800, 600);
    EXPECT(p.x, 0), EXPECT(p.y, 0), EXPECT(p.w, 0x320), EXPECT(p.h, 0x258), EXPECT(p.offset_x, 0), EXPECT(p.offset_y, 0);
    monuwin_position(&p, 1, 1280, 1024);
    EXPECT(p.x, 128), EXPECT(p.y, 256), EXPECT(p.w, 0x400), EXPECT(p.h, 0x300), EXPECT(p.offset_x, 0x70), EXPECT(p.offset_y, 0x54);
    monuwin_position(&p, 1, 1000, 700);  // (a screen narrower than the window: the division rounds toward zero)
    EXPECT(p.x, -12), EXPECT(p.y, -68);

    // The rectangles with the picture at the corner, and moved by the big window's offset.
    struct monuwin_rects r;
    monuwin_rects(&r, 0, 0, 0);
    EXPECT_RECT(r.view, 0x13f, 0xca, 0x1df, 0x12b);
    EXPECT_RECT(r.caption, 0x90, 0x2f, 0x293, 0x5b);
    EXPECT_RECT(r.zoom, 0x65, 0x76, 0x2bd, 0x1de);
    EXPECT_RECT(r.slot[0], 0x12d, 0x1a9, 0x159, 0x1d3);
    EXPECT_RECT(r.slot[2], 0x12d, 0x175, 0x159, 0x19f);
    EXPECT_RECT(r.slot[8], 0x0, 0x1a9, 0x2c, 0x1d3);
    EXPECT_RECT(r.slot[12], 0x45, 0x141, 0x71, 0x16b);
    EXPECT_RECT(r.slot[13], 0x2aa, 0x141, 0x2d6, 0x16b);
    EXPECT_RECT(r.pair[0], 0xfd, 0x1f4, 0x14b, 0x233);
    EXPECT_RECT(r.pair[5], 0x1dc, 0x1f4, 0x2b1, 0x233);
    EXPECT_RECT(r.face[0], 0xfe, 0x1ff, 0x138, 0x231);
    EXPECT_RECT(r.face[2], 0xc0, 0x1ff, 0xfa, 0x231);
    EXPECT_RECT(r.face[5], 0x25d, 0x1ff, 0x297, 0x231);
    EXPECT_RECT(r.portrait, 0x16c, 0x6f, 0x1b3, 0xaf);
    monuwin_rects(&r, 0x70, 0x54, 1);
    EXPECT_RECT(r.view, 0x1af, 0x11e, 0x24f, 0x17f);
    EXPECT_RECT(r.slot[13], 0x31a, 0x195, 0x346, 0x1bf);
    EXPECT_RECT(r.portrait, 0x1b8, 0x5a, 0x246, 0xbf);  // the larger leader's place for fourteen monuments

    // The cells of the sheet of icons.
    int x, y;
    monuwin_icon_cell(0, &x, &y), EXPECT(x, 2), EXPECT(y, 0x226);
    monuwin_icon_cell(7, &x, &y), EXPECT(x, 0x13d), EXPECT(y, 0x226);
    monuwin_icon_cell(8, &x, &y), EXPECT(x, 2), EXPECT(y, 0x251);
    monuwin_icon_cell(14, &x, &y), EXPECT(x, 0x110), EXPECT(y, 0x251);
    monuwin_big_cell(0, &x, &y), EXPECT(x, 1), EXPECT(y, 0x89);
    monuwin_big_cell(4, &x, &y), EXPECT(x, 0x285), EXPECT(y, 0x89);
    monuwin_big_cell(5, &x, &y), EXPECT(x, 1), EXPECT(y, 0xeb);
    monuwin_big_cell(14, &x, &y), EXPECT(x, 0x285), EXPECT(y, 0x14d);

    // The sort: by date, the unused (-1) to the end, equal dates keeping their places.
    int order[MONU_KINDS];
    int32_t when[MONU_KINDS] = {5, 3, 4};
    for (int i = 0; i < MONU_KINDS; i++) order[i] = i < 3 ? i : MONU_NO_KIND;
    monuwin_sort(order, when);
    EXPECT(order[0], 1), EXPECT(order[1], 2), EXPECT(order[2], 0), EXPECT(order[3], -1), EXPECT(order[14], -1);
    for (int i = 0; i < MONU_KINDS; i++) order[i] = MONU_NO_KIND;
    order[1] = 2, order[4] = 0;
    monuwin_sort(order, when);
    EXPECT(order[0], 2), EXPECT(order[1], 0), EXPECT(order[2], -1), EXPECT(order[4], -1);
    for (int i = 0; i < MONU_KINDS; i++) order[i] = MONU_NO_KIND;
    order[0] = 0, order[1] = 1;
    int32_t same[MONU_KINDS] = {4, 4};
    monuwin_sort(order, same);
    EXPECT(order[0], 0), EXPECT(order[1], 1);

    // Which pictures of the wins a count shows: positions of two, the first of a count at the position (count - 1) / 2, the
    // second at (count - 2) / 2.
    EXPECT(monuwin_pair_slot(1, 0), 0);
    EXPECT(monuwin_pair_slot(2, 0), 0), EXPECT(monuwin_pair_slot(2, 1), 1);
    EXPECT(monuwin_pair_slot(3, 0), 2), EXPECT(monuwin_pair_slot(3, 1), 1);
    EXPECT(monuwin_pair_slot(4, 0), 2), EXPECT(monuwin_pair_slot(4, 1), 3);
    EXPECT(monuwin_pair_slot(5, 0), 4), EXPECT(monuwin_pair_slot(5, 1), 3);
    EXPECT(monuwin_pair_slot(6, 0), 4), EXPECT(monuwin_pair_slot(6, 1), 5);

    // The walk over the beaten factions: an unused kind costs two kinds of the walk but one step.
    int used[12] = {1, 1};
    int kinds[6], places[6];
    EXPECT(monuwin_face_walk(2, used, kinds, places), 2);
    EXPECT(kinds[0], 15), EXPECT(places[0], 0), EXPECT(kinds[1], 16), EXPECT(places[1], 1);
    int used2[12] = {1, 0, 1, 0};
    EXPECT(monuwin_face_walk(2, used2, kinds, places), 1);
    EXPECT(kinds[0], 15), EXPECT(places[0], 0);
    int used3[12] = {0, 1, 1, 1};
    EXPECT(monuwin_face_walk(3, used3, kinds, places), 2);
    EXPECT(kinds[0], 17), EXPECT(places[0], 1), EXPECT(kinds[1], 18), EXPECT(places[1], 2);
    int none[12] = {0};
    EXPECT(monuwin_face_walk(0, none, kinds, places), 0);
    EXPECT(monuwin_face_walk(3, none, kinds, places), 0);

    // The picture a zoomed monument shows.
    EXPECT(monuwin_zoom_picture(13, &x, &y), 0), EXPECT(x, 0x155), EXPECT(y, 0xb);
    EXPECT(monuwin_zoom_picture(15, &x, &y), 1), EXPECT(x, 0x155), EXPECT(y, 0x174);
    EXPECT(monuwin_zoom_picture(20, &x, &y), 1);
    EXPECT(monuwin_zoom_picture(5, &x, &y), 2), EXPECT(x, 0), EXPECT(y, 0);
    EXPECT(monuwin_zoom_picture(14, &x, &y), 2);

    EXPECT(monuwin_line_height(-1, 12, 20), 12);
    EXPECT(monuwin_line_height(0, 12, 20), 20);
    EXPECT(monuwin_line_height(3, 12, 20), 23);

    if (failures) printf("%d failures\n", failures);
    else printf("ui_monuwin: all passed\n");
    return failures != 0;
}
