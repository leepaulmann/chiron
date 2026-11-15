// Unit test for the Council window's pure parts (src/ui/councwin.c): no game files needed. The numbers are those the original's
// code gives (CouncWin_draw_leader's slot, make_resolution's count, init's layout and the AI's offers), worked out by hand from
// the disassembly.
#include <stdio.h>
#include <string.h>

#include "../src/ui/councwin.h"

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
    // The seats. All seven factions seated and no governor: faction n in seat n. With the aliens (faction 7, and then 6 as well)
    // the others are centred: a faction less shifts nothing (7 - 6 is 1, halved to 0), two less shift each seat by one.
    for (int n = 1; n <= 7; n++) EXPECT(councwin_slot(n, -1, 0), n);
    EXPECT(councwin_slot(1, -1, 1u << 7), 1);
    EXPECT(councwin_slot(6, -1, 1u << 7), 6);
    EXPECT(councwin_slot(1, -1, (1u << 7) | (1u << 6)), 2);
    EXPECT(councwin_slot(5, -1, (1u << 7) | (1u << 6)), 6);
    // An alien among the seven takes no seat: the ones after it close up.
    EXPECT(councwin_slot(3, -1, 1u << 2), 2);
    EXPECT(councwin_slot(5, -1, 1u << 2), 4);
    // The governor sits in the middle seat (4), and the faction that would have stood there takes the governor's own place.
    EXPECT(councwin_slot(3, 3, 0), 4);
    EXPECT(councwin_slot(4, 3, 0), 3);
    EXPECT(councwin_slot(5, 3, 0), 5);
    EXPECT(councwin_slot(1, 3, 0), 1);
    EXPECT(councwin_slot(1, 1, 0), 4);
    EXPECT(councwin_slot(4, 1, 0), 1);
    EXPECT(councwin_slot(4, 7, 0), 7);
    // (a governor of 0 counts as one: the faction in the middle goes to the first seat)
    EXPECT(councwin_slot(4, 0, 0), 1);
    // The rectangle of a seat: two to a seat, the second when there is no governor.
    EXPECT(councwin_rect_index(1, 3), 2);
    EXPECT(councwin_rect_index(1, -1), 3);
    EXPECT(councwin_rect_index(4, -1), 9);
    EXPECT(councwin_rect_index(7, 2), 14);
    EXPECT(councwin_rect_index(7, -1), 15);

    // The tally. Seven factions, with these votes counts (the first, 0, has none).
    const int32_t count[8] = {0, 3, 2, 1, 1, 1, 1, 1};
    struct councwin_tally t;
    int32_t vote[8] = {0, -1, -1, -1, -2, -2, -3, -1};
    // For a proposal above the second a faction has one vote; an abstainer takes no part: four yeas and two nays of six.
    EXPECT(councwin_tally(&t, vote, count, 0, 0xff, 2, -1), 1);
    EXPECT(t.yea, 4), EXPECT(t.nay, 2), EXPECT(t.total, 6), EXPECT(t.veto, 0);
    EXPECT(councwin_result(&t, 2), 1);
    // The governor voting nay on a proposal above the second is a veto, unless the others were all for it.
    EXPECT(councwin_tally(&t, vote, count, 0, 0xff, 2, 4), 1);
    EXPECT(t.veto, 1);
    EXPECT(councwin_result(&t, 2), -2);
    int32_t all_yea[8] = {0, -1, -1, -1, -2, -1, -1, -1};
    EXPECT(councwin_tally(&t, all_yea, count, 0, 0xff, 2, 4), 1);
    EXPECT(t.yea, 6), EXPECT(t.total, 7), EXPECT(t.veto, 0);
    EXPECT(councwin_result(&t, 2), 1);
    // The governor's veto is only for a proposal above the second.
    EXPECT(councwin_tally(&t, vote, count, 0, 0xff, 3, 4), 1);
    EXPECT(t.veto, 1);
    EXPECT(councwin_tally(&t, vote, count, 0, 0xff, 1, 4), 1);
    EXPECT(t.veto, 0);
    // The votes count for the first two proposals: Supreme Leader needs three fourths of the total (of ten, 7).
    int32_t sup[8] = {0, -1, -1, -2, -2, -2, -1, -2};
    EXPECT(councwin_tally(&t, sup, count, 0, 0xff, 1, -1), 1);
    EXPECT(t.yea, 6), EXPECT(t.nay, 4), EXPECT(t.total, 10);
    EXPECT(councwin_result(&t, 1), -1);
    sup[7] = -1;
    EXPECT(councwin_tally(&t, sup, count, 0, 0xff, 1, -1), 1);
    EXPECT(t.yea, 7), EXPECT(t.nay, 3);
    EXPECT(councwin_result(&t, 1), 1);
    // As many nays as yeas fails.
    int32_t even[8] = {0, -1, -2, -3, -3, -3, -3, -3};
    EXPECT(councwin_tally(&t, even, count, 0, 0xff, 2, -1), 1);
    EXPECT(councwin_result(&t, 2), -1);
    // A faction that has not voted leaves the count open; an alien's missing vote, and one of a faction out of the game, do not.
    int32_t open[8] = {0, -1, -1, 0, -2, -2, -3, -1};
    EXPECT(councwin_tally(&t, open, count, 0, 0xff, 2, -1), 0);
    EXPECT(councwin_tally(&t, open, count, 1u << 3, 0xff, 2, -1), 1);
    EXPECT(t.total, 5);
    int32_t dead[8] = {0, -1, -1, -1, -5, -2, -3, -1};
    EXPECT(councwin_tally(&t, dead, count, 0, 0xff & ~(1u << 4), 2, -1), 1);
    EXPECT(t.total, 5);
    // The governor's election: each faction's votes go to the candidate it voted for.
    int32_t cand[8] = {0, 3, 3, 1, 3, 4, 1, 4};
    EXPECT(councwin_tally(&t, cand, count, 0, 0xff, 0, -1), 1);
    EXPECT(t.cand[3], 3 + 2 + 1), EXPECT(t.cand[1], 1 + 1), EXPECT(t.cand[4], 1 + 1), EXPECT(t.yea, 0);

    // The winner: most votes; the turn picks where the count starts so a tie is not always the same; a tie with the governor goes
    // to him, and any other leaves no one.
    int tie[8] = {0, 5, 0, 5, 0, 0, 0, 0};
    EXPECT(councwin_winner(tie, 0, 4, 0), 0);
    EXPECT(councwin_winner(tie, 0, 1, 0), 1);
    EXPECT(councwin_winner(tie, 0, 3, 0), 3);
    EXPECT(councwin_winner(tie, 2, 4, 0), 0);
    EXPECT(councwin_winner(tie, 2, 3, 0), 3);
    int clear_win[8] = {0, 1, 0, 6, 2, 0, 0, 0};
    EXPECT(councwin_winner(clear_win, 5, 2, 0), 3);
    int none[8] = {0};
    EXPECT(councwin_winner(none, 0, 2, 0), 2);   // (everyone at nought: the governor's own place wins)
    EXPECT(councwin_winner(none, 0, 2, 5), 2);
    EXPECT(councwin_winner(none, 0, 9, 5), 0);   // (and with a governor out of the running it is a tie with no one)

    // The totals at the foot of the panel and the list of candidates.
    int32_t foot[8] = {0, -1, -1, -2, -1, 3, 3, -3};
    EXPECT(councwin_side_total(-1, foot, count, 0, 1), 3 + 2 + 1);
    EXPECT(councwin_side_total(-1, foot, count, 0, 2), 3);
    EXPECT(councwin_side_total(-2, foot, count, 0, 1), 1);
    EXPECT(councwin_side_total(-1, foot, count, 1u << 1, 1), 2 + 1);
    EXPECT(councwin_support(3, foot, count, 0), 2);
    EXPECT(councwin_support(3, foot, count, 1u << 5), 1);

    // The window's place: the room under the interface's top, at most 0x18b high, at the bottom; x half of what the screen has over 0x30c.
    struct councwin_position p;
    councwin_position(&p, 1024, 768, 0x80);
    EXPECT(p.x, 122), EXPECT(p.y, 245), EXPECT(p.h, 0x18b);
    councwin_position(&p, 800, 600, 0x80);
    EXPECT(p.x, 10), EXPECT(p.y, 77), EXPECT(p.h, 0x18b);
    councwin_position(&p, 1024, 768, 500);
    EXPECT(p.y, 0), EXPECT(p.h, 268);
    councwin_position(&p, 700, 600, 0x80);
    EXPECT(p.x, -40);
    councwin_position(&p, 701, 600, 0x80);
    EXPECT(p.x, -39);  // (rounded toward zero)

    // The buttons: the panel less four, the buttons in a row at its foot and a third of the width (less 3) apiece.
    struct councwin_layout l;
    const int32_t panel[4] = {10, 20, 310, 220};
    councwin_layout(&l, panel);
    EXPECT_RECT(l.info, 14, 24, 306, 190);
    EXPECT_RECT(l.close, 17, 193, 303, 213);
    EXPECT_RECT(l.button[0], 17, 193, 110, 213);
    EXPECT_RECT(l.button[1], 113, 193, 206, 213);
    EXPECT_RECT(l.button[2], 209, 193, 302, 213);
    const int32_t odd[4] = {0, 0, 101, 100};  // (the third rounds toward zero: (93 - 3) / 3 = 30)
    councwin_layout(&l, odd);
    EXPECT_RECT(l.button[0], 7, 73, 34, 93);
    EXPECT_RECT(l.button[2], 67, 73, 94, 93);

    EXPECT(councwin_line_height(5, 12, 14), 19);
    EXPECT(councwin_line_height(-1, 12, 14), 12);
    EXPECT(councwin_line_height(0, 12, 14), 14);

    // How a vote looks: the three boxes and the five labels; a candidate's number the plain box and no label.
    int label;
    EXPECT(councwin_vote_look(-1, &label), 0x78da00), EXPECT(label, 0x1ef);
    EXPECT(councwin_vote_look(-2, &label), 0x78d9d8), EXPECT(label, 0x1f0);
    EXPECT(councwin_vote_look(-3, &label), 0x78da28), EXPECT(label, 0x1f1);
    EXPECT(councwin_vote_look(-4, &label), 0x78da28), EXPECT(label, 0x1f5);
    EXPECT(councwin_vote_look(-5, &label), 0x78da28), EXPECT(label, 0x1f6);
    EXPECT(councwin_vote_look(3, &label), 0x78d528), EXPECT(label, -1);
    EXPECT(councwin_count_label(1), 0x202);
    EXPECT(councwin_count_label(2), 0x203);
    EXPECT(councwin_count_label(0), 0x203);
    EXPECT(councwin_governor_label(3, 2), 0x1fd);
    EXPECT(councwin_governor_label(3, 3), 0x200);

    // The AI's offer: the turn and five bases' worth, to three quarters of its energy at most, in whole 25s.
    EXPECT(councwin_offer_price(50, 4, 400), 50);
    EXPECT(councwin_offer_price(50, 4, 40), 25);
    EXPECT(councwin_offer_price(50, 4, 0), 0);
    EXPECT(councwin_offer_price(100, 20, 1000), 200);
    EXPECT(councwin_offer_price(24, 0, 1000), 0);
    EXPECT(councwin_pivotal(3, 4, 6), 1);
    EXPECT(councwin_pivotal(3, 0, 6), 0);
    EXPECT(councwin_pivotal(3, 10, 6), 0);
    EXPECT(councwin_pivotal(1, 5, 6), 0);   // 1 + 5 does not pass 6
    EXPECT(councwin_pivotal(2, 5, 6), 1);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ui_councwin: ok\n");
    return 0;
}
