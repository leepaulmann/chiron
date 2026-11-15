// Unit test for the Social Engineering window's pure parts (src/ui/socialwin.c): no game files needed. The numbers are worked out by
// hand from the original's code (SocialWin_init's layout, allocate_energy, maximize_effect, draw_info and the icon drawing).
#include <stdio.h>
#include <string.h>

#include "../src/ui/socialwin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))
#define EXPECT_RECT(r, l, t, rr, b) \
    do {                            \
        EXPECT((r).left, l);        \
        EXPECT((r).top, t);         \
        EXPECT((r).right, rr);      \
        EXPECT((r).bottom, b);      \
    } while (0)

int main(void) {
    // The layout on made-up panels (800x600 style): A = (10, 20, 210, 100), B = (220, 20, 420, 100), C = (430, 20, 630, 100),
    // D = (10, 400, 410, 500).
    int32_t panels[16] = {10, 20, 210, 100, 220, 20, 420, 100, 430, 20, 630, 100, 10, 400, 410, 500};
    struct ui_rect r[SW_RECTS];
    socialwin_layout(r, panels);
    // The fixed ones.
    EXPECT_RECT(r[SW_R_PRESETS], 0x271, 4, 0x306, 0xfa);
    EXPECT_RECT(r[SW_R_BASAL], 0x271, 0xfd, 0x306, 0x12d);
    EXPECT_RECT(r[SW_R_OK], 0x9a, 0x13a, 0x13c, 0x14e);
    EXPECT_RECT(r[SW_R_RESET], 0x13f, 0x13a, 0x1e1, 0x14e);
    EXPECT_RECT(r[SW_R_CANCEL], 0x1e4, 0x13a, 0x286, 0x14e);
    EXPECT_RECT(r[SW_R_EFFECT], 0x274, 0x1d, 0x303, 0x31);
    EXPECT_RECT(r[SW_R_EFFECT + 9], 0x274, 0x1d + 0x16 * 9, 0x303, 0x1d + 0x16 * 9 + 0x14);
    EXPECT_RECT(r[SW_R_PANEL], 0x1a, 4, 0x26e, 0x4a);
    EXPECT_RECT(r[SW_R_PANEL + 3], 0x1a, 0xdf, 0x26e, 0x125);
    EXPECT_RECT(r[SW_R_MODEL], 0x1d, 0x1d, 0xaf, 0x31);
    EXPECT_RECT(r[SW_R_MODEL + 3], 0x1d9, 0x1d, 0x26b, 0x31);
    EXPECT_RECT(r[SW_R_MODEL + 4], 0x1d, 0x66, 0xaf, 0x7a);
    EXPECT_RECT(r[SW_R_MODEL + 9], 0xb1, 0xaf, 0x143, 0xc3);
    EXPECT_RECT(r[SW_R_MODEL + 14], 0x145, 0xf8, 0x1d7, 0x10c);
    // Those on the panels: the info box is panel C less 4.
    EXPECT_RECT(r[SW_R_INFO], 434, 24, 626, 96);
    EXPECT_RECT(r[SW_R_OVERVIEW], 224, 24, 416, 96);
    // The bars: x0 = 10 + 7 = 17, y0 = 400 + 7, y1 = 500 - 7; a third = ((410 - 7) - 17 + 3) / 3 = 389 / 3 = 129; x1 = 129 + 17 - 3 = 143.
    EXPECT_RECT(r[SW_R_ECONOMY], 17, 407, 143, 493);
    // w = 143 - 17 + 3 = 129: psych from 146 to 272; labs from 275 to the last panel's right less 7.
    EXPECT_RECT(r[SW_R_PSYCH], 146, 407, 272, 493);
    EXPECT_RECT(r[SW_R_LABS], 275, 407, 403, 493);
    // The mouse-over box: panel A less 4, to 6 above the bars.
    EXPECT_RECT(r[SW_R_HOVER], 14, 24, 206, 401);
    // The + and - buttons (economy, labs, psych) and the locks (economy, psych, labs).
    EXPECT_RECT(r[SW_R_ALLOC_BTN], 20, 410, 40, 430);
    EXPECT_RECT(r[SW_R_ALLOC_BTN + 1], 120, 410, 140, 430);
    EXPECT_RECT(r[SW_R_ALLOC_BTN + 2], 278, 410, 298, 430);
    EXPECT_RECT(r[SW_R_ALLOC_BTN + 5], 249, 410, 269, 430);
    EXPECT_RECT(r[SW_R_LOCK], 20, 476, 44, 490);
    EXPECT_RECT(r[SW_R_LOCK + 1], 149, 476, 173, 490);
    EXPECT_RECT(r[SW_R_LOCK + 2], 278, 476, 302, 490);

    // Effect buttons: ten, for the eleven effects (Talent has none).
    EXPECT(socialwin_effect_button(0), 0);
    EXPECT(socialwin_effect_button(2), 2);
    EXPECT(socialwin_effect_button(3), -1);
    EXPECT(socialwin_effect_button(4), 3);
    EXPECT(socialwin_effect_button(10), 9);
    EXPECT(socialwin_button_effect(2), 2);
    EXPECT(socialwin_button_effect(3), 4);
    EXPECT(socialwin_button_effect(9), 10);
    EXPECT(socialwin_hover_effect(0x11), 0);
    EXPECT(socialwin_hover_effect(0x13), 2);
    EXPECT(socialwin_hover_effect(0x14), 4);
    EXPECT(socialwin_hover_effect(0x1a), 10);

    // The allocation: 5 / 3 / 2 and the player sets psych (1) to 5: ten again means taking from labs then economy.
    {
        int32_t a[3] = {5, 3, 2}, lock[3] = {0, 0, 0};
        socialwin_allocate(a, lock, 1, 4, 10);  // psych 3 -> 4: over by one; the next (labs) gives
        EXPECT(a[0] + a[1] + a[2], 10);
        EXPECT(a[1], 4);
        EXPECT(a[2], 1);
        EXPECT(a[0], 5);
    }
    {
        int32_t a[3] = {5, 3, 2}, lock[3] = {0, 1, 0};  // labs next to give is psych (locked, it is the index itself here)
        socialwin_allocate(a, lock, 0, 6, 10);  // economy 5 -> 6: over by one; the next after 0 is 1 (locked), then 2
        EXPECT(a[0], 6);
        EXPECT(a[1], 3);
        EXPECT(a[2], 1);
    }
    {
        int32_t a[3] = {5, 3, 2}, lock[3] = {0, 0, 0};
        socialwin_allocate(a, lock, 0, 4, 10);  // under by one: the next (psych) takes it
        EXPECT(a[0], 4);
        EXPECT(a[1], 4);
        EXPECT(a[2], 2);
    }
    {
        int32_t a[3] = {5, 3, 2}, lock[3] = {0, 0, 0};
        socialwin_allocate(a, lock, 0, 3, 3);  // a limit of 3: psych (3) is full, labs (2) takes one, then only economy itself can
        EXPECT(a[0], 4);                       // take the last (past the limit: the one the player set always can)
        EXPECT(a[1], 3);
        EXPECT(a[2], 3);
    }
    {
        int32_t a[3] = {0, 0, 0}, lock[3] = {1, 1, 0};
        socialwin_allocate(a, lock, 2, 0, 10);  // all ten go to the one set when the others are locked
        EXPECT(a[2], 10);
    }
    {
        int32_t a[3] = {2, 2, 2}, lock[3] = {0, 0, 0};
        socialwin_allocate(a, lock, 1, 10, 10);  // psych 10: over by 4: labs (2) and economy (2) give them back
        EXPECT(a[0] + a[1] + a[2], 10);
        EXPECT(a[1], 10);
        EXPECT(a[0], 0);
        EXPECT(a[2], 0);
    }

    // The best model of a category for an effect: the first of the highest above 0 among those the faction may choose.
    {
        int32_t v[4] = {1, 3, 3, 2};
        int avail[4] = {1, 1, 1, 1};
        EXPECT(socialwin_best_model(v, avail), 1);
        avail[1] = 0;
        EXPECT(socialwin_best_model(v, avail), 2);
        int32_t z[4] = {0, -1, 0, -2};
        EXPECT(socialwin_best_model(z, avail), 0);
        int none[4] = {0, 0, 0, 0};
        EXPECT(socialwin_best_model(v, none), 0);
    }

    // Numbers as text.
    {
        char t[16];
        socialwin_signed(t, 2), EXPECT(strcmp(t, "+2"), 0);
        socialwin_signed(t, 0), EXPECT(strcmp(t, "0"), 0);
        socialwin_signed(t, -3), EXPECT(strcmp(t, "-3"), 0);
        socialwin_signed_abs(t, 4), EXPECT(strcmp(t, "+4"), 0);
        socialwin_signed_abs(t, -1), EXPECT(strcmp(t, "-1"), 0);
    }

    // The loss: half the gap between labs and economy, and twice the shortfall of efficiency from 4.
    EXPECT(socialwin_loss_half(0, 5), 0);   // 0 + 10 - 10
    EXPECT(socialwin_loss_half(2, 6), 2);   // 2 + 12 - 10 = 4 -> 2
    EXPECT(socialwin_loss_half(1, 1), 3);   // |1 + 2 - 10| = 7 -> 3 (toward zero)
    EXPECT(socialwin_loss_half(3, 0), 3);   // |3 - 10| = 7 -> 3
    EXPECT(socialwin_loss_unit(4), 0);
    EXPECT(socialwin_loss_unit(9), 0);
    EXPECT(socialwin_loss_unit(0), 8);
    EXPECT(socialwin_loss_unit(-2), 12);
    EXPECT(socialwin_loss_unit(-9), 16);
    EXPECT(socialwin_clamp(-3, 0, 40), 0);
    EXPECT(socialwin_clamp(41, 0, 40), 40);
    EXPECT(socialwin_clamp(17, 0, 40), 17);

    // The energy window's lines: on an 800 screen the first is a line below the top, the third two lines on; on a wider one the first
    // is centred and the third goes to the right at the same height.
    {
        struct socialwin_info_lines l;
        socialwin_info_lines(&l, 0x320, 100, 300, 50, 120, 14);
        EXPECT(l.y1, 64);
        EXPECT(l.y3, 92);
        EXPECT(l.x3, 100);
        socialwin_info_lines(&l, 1024, 100, 300, 50, 120, 14);  // 50 + (120 - 28 - 50) / 2 = 71
        EXPECT(l.y1, 71);
        EXPECT(l.y3, 71);
        EXPECT(l.x3, 200);
        socialwin_info_lines(&l, 1024, 100, 301, 50, 100, 30);  // 50 + (100 - 60 - 50) / 2 = 50 - 5 = 45 (toward zero)
        EXPECT(l.y1, 45);
        EXPECT(l.x3, 200);
    }

    // A model's icons: the effects drawn (not Talent, not those hidden) and the big ones.
    {
        int32_t v[11] = {2, 0, 1, 9, -2, 0, -1, 0, 0, 3, 0};
        int hidden[11] = {0};
        int drawn, big;
        socialwin_icon_counts(v, hidden, &drawn, &big);
        EXPECT(drawn, 5);  // 2, 1, -2, -1, 3 (the 9 is Talent's)
        EXPECT(big, 3);    // 2, -2, 3
        hidden[4] = 1;
        socialwin_icon_counts(v, hidden, &drawn, &big);
        EXPECT(drawn, 4);
        EXPECT(big, 2);
        // The first icon: centred over the box with each icon 26 + 1 wide, each big picture 13.
        EXPECT(socialwin_icons_x(100, 300, 5, 26, 3, 13), 100 + (300 - 27 * 5 - 13 * 3 - 100) / 2);
        EXPECT(socialwin_icons_x(100, 300, 0, 26, 0, 13), 100 + (300 - 27 - 100) / 2);  // nothing drawn takes the room of one
        EXPECT(socialwin_icons_x(0, 10, 1, 26, 0, 13), -8);  // -17 / 2, toward zero
    }
    // The pictures: the big ones two to a size from 2 (positive first), the small ones nine to a row from the sixth.
    EXPECT(socialwin_big_index(2), 0);
    EXPECT(socialwin_big_index(-2), 1);
    EXPECT(socialwin_big_index(3), 2);
    EXPECT(socialwin_big_index(-6), 9);
    EXPECT(socialwin_small_index(0, 1), 8);
    EXPECT(socialwin_small_index(0, -1), 7);
    EXPECT(socialwin_small_index(2, 3), 2 * 9 + 8);
    EXPECT(socialwin_small_index(4, -2), 3 * 9 + 7);
    EXPECT(socialwin_small_index(10, 1), 9 * 9 + 8);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ui_socialwin: ok\n");
    return 0;
}
