// The Planetary Council's window (CouncWin): the pure parts (which slot a leader is shown in, how the votes are counted and a
// proposal decided, how the info panel's buttons and the window are laid out, what the vote of a faction looks like, and the
// price and the test of an offer made to the player), kept apart so a test can check them without the game. See councwin.c.
#pragma once

#include <stdint.h>

#include "ui.h"

// The seven factions are numbered 1 to 7 (0 is the planet's own, which does not vote); a set of them is a mask with bit n for
// faction n. `alien_mask` has the factions of the aliens (the ones with the aliens' rule flag, which have no seat); `alive_mask`
// those still in the game.

// The place a leader's portrait takes among the seats, 1 to 7: the factions with a seat stand in order, the seats centred
// (there are seven places, and fewer factions leave one or two at each end). The seat in the middle (4) is the Planetary
// Governor's, and the faction that would have stood there takes the governor's own seat instead.
int councwin_slot(int faction, int governor, unsigned alien_mask);

// Which of the window's sixteen seat rectangles a seat uses: two to a seat, the second the one for when there is no governor
// (the seats are then drawn shorter, as the middle is not taken).
int councwin_rect_index(int slot, int governor);

// What the votes come to. `vote` is what each faction has voted (0 not yet, -1 yea, -2 nay, -3 abstain, -5 no vote, above 0 the
// candidate it votes for) and `count` how many votes it has. For a proposal above the first two (the ones with a yea or a nay
// and no candidate) every faction has one vote, else a faction's own number. A faction not in the game, one with no seat and one
// that has abstained take no part. Answers 0 when a faction with a seat has not voted yet (the rest of the tally is then
// partial), else 1.
struct councwin_tally {
    int yea, nay, total;
    int veto;      // the governor voted against a proposal above the second, which one vote short of unanimous does not count
    int cand[8];   // the votes each candidate has (a faction's count goes to the one it voted for)
};
int councwin_tally(struct councwin_tally *t, const int32_t vote[8], const int32_t count[8], unsigned alien_mask, unsigned alive_mask,
                   int prop, int governor);

// The outcome of a proposal that has a yea and a nay: -1 failed, 1 passed, -2 failed on the governor's veto. It fails when the
// nays are as many as the yeas, and for the second proposal (the Supreme Leader's) when the yeas are under three fourths.
int councwin_result(const struct councwin_tally *t, int prop);

// The faction elected governor: the candidate with the most votes, the factions taken in an order that starts at the turn's
// number (so that the same one doesn't always win a tie), a tie with the governor going to the governor, and any other tie
// leaving no one (0). `initial` is what the window's target field holds when the count begins (it can take part in a tie).
int councwin_winner(const int cand[8], int turn, int governor, int initial);

// The votes the yeas (-1) or the nays (-2) make at the foot of the panel, and the votes the candidate has, which the list of
// candidates shows (it counts a faction whether it is in the game or not).
int councwin_side_total(int side, const int32_t vote[8], const int32_t count[8], unsigned alien_mask, int prop);
int councwin_support(int candidate, const int32_t vote[8], const int32_t count[8], unsigned alien_mask);

// The window's own place and size: its height is the room under the interface's top (the part of the screen above it), at most
// 0x18b, it stands at the bottom, and its width is 0x32c, centred.
struct councwin_position {
    int x, y, h;
};
void councwin_position(struct councwin_position *p, int screen_w, int screen_h, int top);

// The panel of the interface the window draws its proposal in, with its four buttons: `panel` is the rectangle the interface
// keeps for it. The text area is the panel less a frame of four, and above its foot by 0x1a; the buttons stand in a row at the
// foot, three of equal widths (the vote buttons) and, as wide as all three, the one that ends the window (or the governor's
// choice of candidate). The third is taken from the width less the margin, which rounds toward zero.
struct councwin_layout {
    int32_t info[4];
    int32_t close[4];
    int32_t button[3][4];
};
void councwin_layout(struct councwin_layout *l, const int32_t panel[4]);

// The height of a line of a font: its height and the spacing it was given (the font's first word, when not negative), or its
// ascent when it has no spacing.
int councwin_line_height(int first, int ascent, int height);

// How a faction's vote looks under its portrait: the box the label is drawn in (the game's address of its frame) and the label
// (an index into the labels file) for a vote that is a word; a candidate's number gives the default box and no label (-1), the
// name of the faction being written.
uint32_t councwin_vote_look(int vote, int *label);

// The label that follows the number of votes a faction has (one vote, or more votes), and the label the election of a governor ends
// with in the result box: one when the governor is a different faction than the one before (elected), the other when it is the same.
int councwin_count_label(int count);
int councwin_governor_label(int governor, int old_governor);

// The price an AI pays the human for his vote: the turn's number and five times its bases, taken to at most three quarters of the
// energy it has, and rounded down to a multiple of 25.
int councwin_offer_price(int turn, int bases, int energy);

// Whether the human's votes could decide the election of a candidate: they would put it above the strongest other candidate,
// and the other would put it above the candidate.
int councwin_pivotal(int own, int support, int strongest_other);

// The state of the window as it is saved, the names of the fields the original keeps by address elsewhere.
enum { COUNCWIN_BASE = 0x6fec80, COUNCWIN_SIZE = 0x515c };
