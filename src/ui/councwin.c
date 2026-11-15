// The Planetary Council's window, the game's CouncWin. It is one global object (0x6fec80) built by a static initializer and
// destroyed by an atexit handler. The window sits over the interface, in the strip below the map: the seven leaders' portraits
// along its top (a leader is drawn by draw_leader into the window's Buffer, and a click on one is a click on a spot the draw
// registers), and, in the interface's info panel (the window draws there too, in the interface's own Buffer), the proposal, its
// text and the tally of the votes. Under each portrait is how the leader voted. At the foot of the panel stand the buttons, which
// are children of the interface: Yea, Nay (labelled differently for the governor's own) and Abstain for the proposals with a vote;
// for the governor's election one that asks which candidate the player votes for; and OK, which takes their place once the vote is
// decided. Clicking on a leader asks for his vote (the player buys it: buy_council_vote, which is the engine's) and the AI's votes come
// from council_get_vote. When every faction has voted the window decides (make_resolution) and shows the result, and a click or a key
// closes it, whereupon the game's council_action carries the proposal out.
//
// Outside the class: the game opens the window with council() (0x428350, which the engine holds in shadow mode: it is not part of
// this class, and does game things of its own as well: it makes the factions acquainted and records the call), which calls init;
// buy_council_vote() tells the window the vote it has bought with set_vote, and the window's own state is read by absolute address
// elsewhere: 0x703de0 (the council is in session), 0x703de4, 0x703de8 (a play by e-mail game's council is waiting) and the
// per-faction proposal and result records at 0x703df0 and 0x703e10, which make_resolution writes. The game's proposals (name,
// description, tech) are the table at 0x9a6828.
//
// The vote of a faction (0xa54 + 4 * faction): 0 not yet, -1 yea, -2 nay, -3 abstain, -5 not voting (not in the game), above 0 the
// candidate it votes for (the governor's election); and its number of votes (0xa74) is from council_votes.
//
// Boundary: the Council's logic (call_council, council_get_vote, council_action, buy_council_vote, council_votes, eligible, and
// the diplomatic functions the AI's offers use) is the engine's and is called here through the game's pointers; so are the
// windows that the game's other classes own, which this one hides while it is open (0x7fd648 and 0x9380dc) and the interface's own
// functions. This file's init includes what the original does at the start of a council on the player's behalf: the AI factions
// offer the player their vote's price (a tech, or energy) when his vote could decide the election of a governor.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=councwin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. Static initializers run after hooks_apply, so the constructor and
// destructor are ours too. set_vote was a function the shadow mode held while a check protects the game's state (quiet.c: a vote
// bought for the player in a check, which a check leaves unanswered); it is held here (shadow_set_vote) so the hook table has one
// entry for the address.
//
// Odd things the original does that are kept: draw_info, when the list of candidates is longer than the panel has lines (the
// fourth, the third on a screen narrower than 0x400), draws the whole panel again with no candidates in it; the Yea button is made
// twice for a proposal with a vote (the first of its two initialisations leaks what it allocated); the window's own first button
// (at 0xd94) is never set up but is shown, hidden and closed with the others; the font at 0xb7c is made and never closed; for a
// faction that is not in the game the click on its seat tests the y of the click where the others test the answer to a question; an
// offer of energy that the player takes ends the offers, where an offer of techs that he takes only stops the later offers of techs.
// What no scene reaches: the network game's branches (they need net_multiplayer, which a scene does not set), among them the
// asking of a human faction for his vote, which the original also cannot reach, as a human's seat is left out before it.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the stack objects the original
// destroys in a different inline order are destroyed in the one order here (the Popup's own); the rectangles the original sets
// twice (for the two screen sizes) before it sets them from the panel are set once; the seats' rectangles are the constants the
// original stores.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "councwin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufimg.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/dialog.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/sprite.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

static int is_alien(unsigned mask, int f) { return (mask >> f) & 1; }

int councwin_slot(int faction, int governor, unsigned alien_mask) {
    int count = 0;
    for (int f = 1; f <= 7; f++)
        if (!is_alien(alien_mask, f)) count++;
    int shift = (7 - count) / 2;
    int before = 0;
    for (int f = 1; f < faction; f++)
        if (!is_alien(alien_mask, f)) before++;
    int slot = shift + before + 1;
    if (governor == faction) return 4;
    if (governor >= 0 && slot == 4) {
        before = 0;
        for (int f = 1; f < governor; f++)
            if (!is_alien(alien_mask, f)) before++;
        slot = shift + before + 1;
    }
    return slot;
}

int councwin_rect_index(int slot, int governor) { return 2 * slot + (governor < 0 ? 1 : 0); }

int councwin_tally(struct councwin_tally *t, const int32_t vote[8], const int32_t count[8], unsigned alien_mask, unsigned alive_mask,
                   int prop, int governor) {
    *t = (struct councwin_tally){0};
    for (int f = 1; f <= 7; f++) {
        if (is_alien(alien_mask, f)) continue;
        int v = vote[f];
        if (v == 0) return 0;
        if (v < -2 || !((alive_mask >> f) & 1)) continue;
        int w = prop > 1 ? 1 : count[f];
        t->total += w;
        if (v == -1) {
            t->yea += w;
        } else {
            t->nay += w;
            if (f == governor && prop > 1) t->veto = 1;
        }
        if (v > 0) t->cand[v] += count[f];
    }
    if (t->veto && t->yea == t->total - 1) t->veto = 0;
    return 1;
}

int councwin_result(const struct councwin_tally *t, int prop) {
    if (t->nay >= t->yea) return -1;
    if (prop == 1 && t->yea < t->total * 3 / 4) return -1;
    return t->veto ? -2 : 1;
}

int councwin_winner(const int cand[8], int turn, int governor, int initial) {
    int winner = initial, best = 0;
    for (int i = 0; i < 8; i++) {
        int f = (turn + i) % 8;
        if (f == 0) continue;
        int c = cand[f];
        if (c < best) continue;
        if (c > best) {
            best = c;
            winner = f;
        } else if (governor == f) {
            winner = f;
        } else if (governor != winner) {
            winner = 0;
        }
    }
    return winner;
}

int councwin_side_total(int side, const int32_t vote[8], const int32_t count[8], unsigned alien_mask, int prop) {
    int sum = 0;
    for (int f = 1; f <= 7; f++)
        if (!is_alien(alien_mask, f) && vote[f] == side) sum += prop > 1 ? 1 : count[f];
    return sum;
}

int councwin_support(int candidate, const int32_t vote[8], const int32_t count[8], unsigned alien_mask) {
    int sum = 0;
    for (int f = 1; f <= 7; f++)
        if (!is_alien(alien_mask, f) && vote[f] == candidate) sum += count[f];
    return sum;
}

void councwin_position(struct councwin_position *p, int screen_w, int screen_h, int top) {
    int room = screen_h - top;
    p->h = room > 0x18b ? 0x18b : room;
    p->x = (screen_w - 0x30c) / 2;
    p->y = room - p->h;
}

void councwin_layout(struct councwin_layout *l, const int32_t panel[4]) {
    int left = panel[0] + 4, top = panel[1] + 4, right = panel[2] - 4, bottom = panel[3] - 4;
    l->info[0] = left, l->info[1] = top, l->info[2] = right, l->info[3] = bottom - 0x1a;
    int btn_top = bottom - 0x17;
    l->close[0] = left + 3, l->close[1] = btn_top, l->close[2] = right - 3, l->close[3] = bottom - 3;
    int third = (right - left - 3) / 3;
    for (int i = 0; i < 3; i++) {
        l->button[i][0] = left + (i ? 3 + i * third : 3);
        l->button[i][1] = btn_top;
        l->button[i][2] = left + (i + 1) * third;
        l->button[i][3] = bottom - 3;
    }
}

int councwin_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

// The three boxes the words of a vote are drawn in: yea, nay, and the others.
enum { FRAME_VOTE_YEA = 0x78da00, FRAME_VOTE_NAY = 0x78d9d8, FRAME_VOTE_OTHER = 0x78da28, FRAME_CANDIDATE = 0x78d528 };

uint32_t councwin_vote_look(int vote, int *label) {
    switch (vote) {
    case -1: *label = 0x1ef; return FRAME_VOTE_YEA;
    case -2: *label = 0x1f0; return FRAME_VOTE_NAY;
    case -3: *label = 0x1f1; return FRAME_VOTE_OTHER;
    case -4: *label = 0x1f5; return FRAME_VOTE_OTHER;
    case -5: *label = 0x1f6; return FRAME_VOTE_OTHER;
    default: *label = -1; return FRAME_CANDIDATE;
    }
}

int councwin_count_label(int count) { return count == 1 ? 0x202 : 0x203; }

int councwin_governor_label(int governor, int old_governor) { return governor != old_governor ? 0x1fd : 0x200; }

int councwin_offer_price(int turn, int bases, int energy) {
    int price = turn + 5 * bases, cap = energy * 3 / 4;
    if (cap < price) price = cap;
    return price / 25 * 25;
}

int councwin_pivotal(int own, int support, int strongest_other) { return own + support > strongest_other && own + strongest_other > support; }

// ---- The object -------------------------------------------------------------------------------------------

// The window's buttons are numbered by what they tell the window: 1, 2 and 3 the vote buttons (yea, nay, abstain), 4 the
// governor's candidate choice, 5 the one that ends the window. The six button objects, in the original's order, hold: 0 never set up,
// 1 id 1, 2 id 2, 3 id 3, 4 id 4, 5 id 5.
enum { BUTTONS = 6, FONTS = 6, LEADER_RECTS = 16, SPRITE_PAIRS = 16 };

struct game_councwin {
    struct game_win win;     // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    void **iface_vtable;  // 0xa14: the part the interface calls (its vtable at 0x66aa94): the interface's status and the buttons' clicks
    int32_t iface_mode;   // 0xa18: the interface's mode for this window (7)
    int32_t prop;         // 0xa1c: the proposal (0 the governor, 1 the Supreme Leader, ...)
    int32_t target;       // 0xa20: the candidate it is about (the faction elected, when it is decided)
    int32_t result;       // 0xa24: 0 undecided, 1 passed, -1 failed, -2 failed on the governor's veto
    int32_t governor;     // 0xa28: the governor when the window opened
    GAME_PAD(0xa2c, 0xa34);
    int32_t hover;        // 0xa34: the seat the mouse is over, or -1
    uint32_t opened;      // 0xa38: timeGetTime when it opened
    int32_t timeout;      // 0xa3c: a minute for each human, for a network game
    int32_t waiting;      // 0xa40: a network game's flag: set while the humans are waited for
    uint32_t arrived;     // 0xa44: the humans that have come to the window (a network game), all of them for a play by e-mail one
    struct game_spot spot;  // 0xa48: the places of the seats
    int32_t vote[8];      // 0xa54: what each faction has voted
    int32_t votes[8];     // 0xa74: how many votes each has
    GAME_PAD(0xa94, 0xab4);
    struct font font[FONTS];  // 0xab4: 0 the buttons', 1 the panel's title, 2 the leaders' names, 3 the votes' words, 4 unused, 5 the result's
    int32_t rect_result[4];   // 0xba4: the box the result is written in, in the window
    int32_t rect_info[4];     // 0xbb4: the panel's text area
    int32_t rect_title[4];    // 0xbc4: set and never read
    int32_t rect_seat[LEADER_RECTS][4];  // 0xbd4: the seats, two to each of the seven (the second for no governor); the first two unused
    int32_t rect_button[4][4];  // 0xcd4: the buttons 1, 2 and 3, and (as the foot's) the button that closes
    struct game_sprite *face_a[SPRITE_PAIRS];  // 0xd14: the pictures under the portrait that the mouse over it changes: two to a faction
    struct game_sprite *face_b[SPRITE_PAIRS];  // 0xd54: the portraits: two to a faction (the second when it is the governor)
    struct game_flat_button button[BUTTONS];   // 0xd94
};
GAME_AT(struct game_councwin, iface_vtable, 0xa14);
GAME_AT(struct game_councwin, prop, 0xa1c);
GAME_AT(struct game_councwin, result, 0xa24);
GAME_AT(struct game_councwin, governor, 0xa28);
GAME_AT(struct game_councwin, hover, 0xa34);
GAME_AT(struct game_councwin, opened, 0xa38);
GAME_AT(struct game_councwin, timeout, 0xa3c);
GAME_AT(struct game_councwin, waiting, 0xa40);
GAME_AT(struct game_councwin, arrived, 0xa44);
GAME_AT(struct game_councwin, spot, 0xa48);
GAME_AT(struct game_councwin, vote, 0xa54);
GAME_AT(struct game_councwin, votes, 0xa74);
GAME_AT(struct game_councwin, font, 0xab4);
GAME_AT(struct game_councwin, rect_result, 0xba4);
GAME_AT(struct game_councwin, rect_info, 0xbb4);
GAME_AT(struct game_councwin, rect_title, 0xbc4);
GAME_AT(struct game_councwin, rect_seat, 0xbd4);
GAME_AT(struct game_councwin, rect_button, 0xcd4);
GAME_AT(struct game_councwin, face_a, 0xd14);
GAME_AT(struct game_councwin, face_b, 0xd54);
GAME_AT(struct game_councwin, button, 0xd94);
_Static_assert(sizeof(struct game_councwin) == COUNCWIN_SIZE, "layout of the game's CouncWin");

// The vtables the constructor sets: the window's, the Buffer's and the interface part's.
enum { VTABLE_WIN = 0x66aae8, VTABLE_BUF = 0x66aae0, VTABLE_IFACE = 0x66aa94, IFACE_PART = 0xa14 };
// The slots of the window's vtable the class uses (byte offsets), and the buttons'.
enum { VT_SHOW = 0x4, VT_HIDE = 0x8, VT_REDRAW_FN = 0x30, VT_ON_KEY_CLICK = 0x64, VT_SET_MODAL = 0xe4, VT_RELEASE_MODAL = 0xe8, VT_REDRAW = 0xf8,
       VT_FORWARD = 0xb0, VT_BUTTON_CLOSE = 0x168 };

// The game's data it reads, by address.
enum {
    IFACE_WIN = 0x7ae820,      // the interface's window; its Buffer at 0x444
    IFACE_BUF = 0x7aec64,
    IFACE_SLOT = 0x7ae80c,     // the interface's pointer to the part that gets its status and its buttons' clicks
    IFACE_PANEL = 0x7af5c0,    // the panel's rectangle in it (four words)
    IFACE_TOP = 0x7af59c,
    OVERLAY_WIN = 0x7fd648,    // two windows of other classes that are put out of the way while the Council is on
    OTHER_WIN = 0x9380dc,
    SESSION = 0x703de0,        // the Council is in session
    SESSION_B = 0x703de4,      // set when the window is made and cleared when it ends
    PENDING = 0x703de8,        // a hotseat game's council is waiting on its players
    PBEM_PROP = 0x703df0,      // for each faction (1 to 7), the proposal and the result a play by e-mail game keeps for it
    PBEM_RESULT = 0x703e10,
    COLORS = 0x68fa30,         // the faction's text colours (by faction number)
    PORTRAITS = 0x78edc4,      // the first faction's sprites (0x65c bytes a faction)
    OFFER_SPRITES = 0x6846d8,  // the picture of each faction an offer's popup shows (by faction number)
    TALK_FLAG = 0x7492cc, TALK_FACTION = 0x73b790,  // set for a human a talk is asked of (a path that cannot be reached)
    NET_HOST_FLAG = 0x93e8c0,  // a network game's flag, set on the machine that counts the votes
    CONSOLE = 0x9156b0,        // the console object, whose Win part the window's parent is
};
// The frames (a sprite for each corner and edge) the boxes are drawn with.
enum { FRAME_TEXT = 0x78d528, FRAME_TITLE = 0x78d618, FRAME_AREA = 0x78d5a0, FRAME_BACK = 0x78d690 };
// The text colours (the game's palette entries).
enum { COLOR_PANEL = 0xa0, COLOR_YEA = 0xfa, COLOR_NAY = 0xf9, COLOR_VOTE = 0x41 };
// The strings it builds text from (the game's addresses).
enum {
    S_SPACE = 0x682820, S_SEP = 0x682e94, S_OPEN = 0x682e9c, S_CLOSE = 0x682e98, S_TAIL = 0x683224,
    S_NOT_ALL_BUTTON = 0x68318c, S_NOT_ALL_CHOOSE = 0x683194, S_CHOOSE = 0x68319c, S_CHOOSE_PIC = 0x6831a4, S_CHOOSE_SPRITE = 0x6831b4,
    S_REFUSE = 0x6831c4, S_ASK = 0x6831d0, S_ASK_LIST = 0x6831dc, S_NOT_ALL_ASK = 0x6831ec, S_HUMAN = 0x6831f4, S_DEAD_NET = 0x683200,
    S_BUSY = 0x68320c, S_DEAD = 0x683218, S_ALL_ARRIVED = 0x683228, S_OFFER_MONEY = 0x683234, S_OFFER_TECH = 0x683240,
};
// The labels (labels.txt entries).
enum {
    L_YEA = 0x1ef, L_NAY = 0x1f0, L_ABSTAIN = 0x1f1, L_TITLE = 0x1f2, L_OWN_NAY = 0x1f4, L_PASSED = 0x1f7, L_FAILED = 0x1f8, L_VETO = 0x1f9,
    L_CHOOSE = 0x1fb, L_FAILED_GOV = 0x201, L_SUPREME = 0x384, L_SUPREME_FAILED = 0x39b, L_OK = 0x65,
};

static char *text(uint32_t addr) { return game_at(addr); }
static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static void put_word(uint32_t addr, int32_t v) { *(int32_t *)game_at(addr) = v; }
static struct faction_def *def(int f) { return &game_faction_defs[f]; }
static struct faction_data *fac(int f) { return &game_factions[f]; }
static const struct game_box_frame *frame_at(uint32_t addr) { return game_at(addr); }
static struct game_win *iface_win(void) { return game_at(IFACE_WIN); }
static struct game_buffer *iface_buf(void) { return game_at(IFACE_BUF); }
static struct game_win *button_win(struct game_flat_button *b) { return &b->base.win; }
static int color_of(int f) { return word(COLORS + 4 * f); }
static int human_mask(void) { return *game_human_factions; }
static int alive_mask(void) { return *game_factions_alive; }
static int is_alive(int f) { return (alive_mask() >> f) & 1; }
static int alien(int f) { return (def(f)->rule_flags & 0x80) != 0; }
static unsigned alien_mask(void) {
    unsigned m = 0;
    for (int f = 1; f <= 7; f++)
        if (alien(f)) m |= 1u << f;
    return m;
}
static void *win_object(uint32_t addr) { return game_at(addr); }
// The console's Win part (it is a virtual base: the vbtable says how far in), the parent the window is made under.
static struct game_win *console_win_part(void) {
    uint8_t *console = game_at(CONSOLE);
    return (struct game_win *)(console + ((int32_t *)*(void **)console)[1]);
}
static int net_game(void) { return *game_net_multiplayer != 0; }

static void show(void *w, int arg) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, arg); }
static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(struct game_councwin *cw) { UI_VCALL(void(UI_THIS *)(void *), cw, VT_RELEASE_MODAL); }
static void button_close(struct game_flat_button *b) { UI_VCALL(void(UI_THIS *)(void *), b, VT_BUTTON_CLOSE); }

static void cat(const char *s) { strcat(game_str_buffer, s); }
static void clear(void) { game_str_buffer[0] = 0; }
static void upper(void) { (*game_char_upper)(game_str_buffer); }
static void cat_number(int n) {
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%d", n);
    cat(tmp);
}
// A leader's gender word for the text that is put together (the plural is cleared), as the original sets both before every name.
static void gender_of(int f) {
    *game_parse_plural = 0;
    *game_parse_gender = def(f)->leader_female;
}
static void say_leader(int title_slot, int f) {
    gender_of(f);
    game_parse_says(title_slot, def(f)->leader_title, -1, -1);
    gender_of(f);
    game_parse_says(title_slot + 1, def(f)->leader_name, -1, -1);
}
static void write_cent(struct game_buffer *b, const int32_t *rect) { buffer_write_cent_l_7(b, game_str_buffer, rect, (int)strlen(game_str_buffer)); }
// Whether every human faction in the game has come to the window (a network game).
static int all_arrived(const struct game_councwin *cw) {
    unsigned humans = (unsigned)human_mask();
    return ((cw->arrived & (unsigned)alive_mask() & humans) == humans);
}

// ---- Construction ------------------------------------------------------------------------------------------

static void *UI_THIS councwin_ctor(struct game_councwin *cw) {
    graphic_win_ctor(&cw->win);
    cw->iface_vtable = (void **)(uintptr_t)game_va(0x66a6e4);
    spot_ctor(&cw->spot);
    for (int i = 0; i < FONTS; i++) font_ctor(&cw->font[i]);
    for (int i = 0; i < BUTTONS; i++) flat_button_ctor(&cw->button[i]);
    cw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    cw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    cw->iface_vtable = (void **)(uintptr_t)game_va(VTABLE_IFACE);
    return cw;
}

static void UI_THIS councwin_dtor(struct game_councwin *cw) {
    for (int i = BUTTONS - 1; i >= 0; i--) flat_button_dtor(&cw->button[i]);
    for (int i = FONTS - 1; i >= 0; i--) font_dtor(&cw->font[i]);
    spot_dtor(&cw->spot);
    graphic_win_dtor(&cw->win);
}

static void *UI_THIS councwin_scalar_dtor(struct game_councwin *cw, int flags) {
    councwin_dtor(cw);
    if (flags & 1) game_free(cw);
    return cw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS councwin_operator_delete(struct game_buffer *buf, int flags) {
    return councwin_scalar_dtor((struct game_councwin *)((uint8_t *)buf - offsetof(struct game_councwin, buf)), flags);
}

// ---- Votes -------------------------------------------------------------------------------------------------

static int UI_THIS councwin_make_resolution(struct game_councwin *cw, int announce);

// A faction has voted (the AI's, bought from the player, the player's own click, or what an offer to the player came to): the
// vote is written and, when no human has voted yet, the AI's votes are asked for (three times over, as one's vote can depend on
// another's); the window is drawn again and the resolution made if everyone has voted. In a network game the humans are told
// instead, and the resolution is made when they have all come.
static void UI_THIS councwin_set_vote(struct game_councwin *cw, int faction, int vote) {
    if (cw->result != 0) return;
    cw->vote[faction] = vote;
    int ai_vote = 1;
    for (int i = 0; i < 8; i++)
        if (((human_mask() >> i) & 1) && cw->vote[i] != 0) ai_vote = 0;
    if (ai_vote) {
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < 8; i++)
                if (((alive_mask() >> i) & 1) && !((human_mask() >> i) & 1)) cw->vote[i] = game_council_get_vote(i, cw->prop, cw->target);
    }
    redraw(cw);
    if (net_game()) {
        if (word(NET_HOST_FLAG) && councwin_make_resolution(cw, 0)) game_message_data(0x2603, 0, 0, 0, 0, 0);
    } else {
        councwin_make_resolution(cw, 1);
    }
    if (*game_pbem && faction == *game_current_faction) {
        for (int i = 0; i < BUTTONS; i++) hide(&cw->button[i]);
        show(&cw->button[5], 0);
    }
}

// Decides the vote when every faction with a seat has made one: the proposal passes or fails, or the governor is elected. With
// `announce` clear it only answers whether everyone has voted. The result is written, the pledges the factions made on this
// proposal forgotten, the buttons put away but for the last, and (a network game) the proposal carried out.
static int UI_THIS councwin_make_resolution(struct game_councwin *cw, int announce) {
    struct councwin_tally t;
    if (!councwin_tally(&t, cw->vote, cw->votes, alien_mask(), (unsigned)alive_mask(), cw->prop, *game_governor_faction)) return 0;
    if (!announce) return 1;
    if (cw->prop == 0) {
        cw->target = councwin_winner(t.cand, *game_turn, *game_governor_faction, cw->target);
        cw->result = cw->target ? 1 : -1;
    } else {
        cw->result = councwin_result(&t, cw->prop);
    }
    put_word(PENDING, 0);
    if (*game_pbem) {
        int cur = *game_current_faction;
        for (int f = 1; f <= 7; f++)
            if (((human_mask() >> f) & 1) && f != cur) {
                put_word(PBEM_PROP + 4 * (f - 1), cw->prop);
                put_word(PBEM_RESULT + 4 * (f - 1), cw->result);
            }
    }
    redraw(cw);
    game_proposal_turn[cw->prop] = *game_turn;
    for (int f = 1; f <= 7; f++) fac(f)->council_pledge[cw->prop] = 0, fac(f)->council_pledge_to[cw->prop] = 0;
    for (int i = 0; i < BUTTONS; i++) hide(&cw->button[i]);
    show(&cw->button[5], 0);
    if (cw->result == 1) {
        if (cw->prop == 0) {
            *game_governor_faction = cw->target;
            redraw(cw);
        }
        if (net_game()) game_council_action(cw->prop, cw->target);
    }
    return 1;
}

// ---- Drawing -----------------------------------------------------------------------------------------------

// A leader's seat: the portrait, the name, the picture under it, how he voted and, for the governor's election and the Supreme
// Leader's, the number of votes he has. A faction with no seat (the aliens) is not drawn; the seat is also made a spot the mouse
// finds the leader by. The seat is drawn in the window and copied to the screen.
static void UI_THIS councwin_draw_leader(struct game_councwin *cw, int n) {
    if (n == 0) return;
    if (alien(n)) return;
    int governor = *game_governor_faction;
    int slot = councwin_slot(n, governor, alien_mask());
    const int32_t *seat = cw->rect_seat[councwin_rect_index(slot, governor)];
    int32_t seat_rect[4] = {seat[0], seat[1], seat[2], seat[3]};
    spot_kill_type(&cw->spot, slot);
    spot_add(&cw->spot, n, slot, seat_rect[0], seat_rect[1], seat_rect[2] - seat_rect[0], seat_rect[3] - seat_rect[1]);
    struct game_buffer *b = &cw->buf;
    buffer_box_sprite(b, seat_rect, frame_at(FRAME_TEXT));
    int32_t r[4] = {seat_rect[0] + 1, seat_rect[1] + 1, seat_rect[2] - 1, seat_rect[3] - 1};

    struct game_sprite *face = cw->face_b[2 * n + (*game_governor_faction == n)];
    sprite_draw_2(face, b, face->key, r[0], r[1], 1, 1);
    int mid = (r[0] + r[2]) / 2;
    face = cw->face_b[2 * n + (*game_governor_faction == n)];
    int y = face->full_h + r[1] + 4;
    buffer_set_text_color(b, COLOR_PANEL, -1, 1, 1);
    buffer_set_font(b, &cw->font[2], NULL, NULL, NULL);
    clear();
    gender_of(n);
    cat(def(n)->leader_title);
    buffer_write_cent_l_8(b, game_str_buffer, mid, y, (int)strlen(game_str_buffer));
    y += councwin_line_height(cw->font[2].first, cw->font[2].ascent, cw->font[2].height);
    clear();
    gender_of(n);
    cat(def(n)->leader_name);
    buffer_write_cent_l_8(b, game_str_buffer, mid, y, (int)strlen(game_str_buffer));

    // The picture under the name (the mouse over the seat changes it), centred by the width of the first of the two.
    int y3 = councwin_line_height(cw->font[2].first, cw->font[2].ascent, cw->font[2].height) + y + 10;
    struct game_sprite *under = cw->face_a[2 * n + (cw->hover != n)];
    struct game_sprite *wide = cw->face_a[2 * n];
    int x3 = r[0] + (r[2] - wide->full_w - r[0]) / 2;
    sprite_draw_2(under, b, under->key, x3, y3, 1, 1);

    // How he voted, in a box at the foot of the seat (in a network game, once the humans have all come).
    int32_t box[4] = {r[0] + 3, r[3] - 0x17, r[2] - 3, r[3] - 3};
    int vote = cw->vote[n];
    if (vote != 0 && (!net_game() || all_arrived(cw))) {
        buffer_set_text_color(b, COLOR_VOTE, -1, 1, 1);
        clear();
        int label;
        uint32_t look = councwin_vote_look(vote, &label);
        if (vote >= -5 && vote <= -1) {
            buffer_box_sprite(b, box, frame_at(look));
            cat(game_label(label));
        } else {
            buffer_box_sprite(b, box, frame_at(FRAME_CANDIDATE));
            buffer_set_text_color(b, color_of(vote), -1, 1, 1);
            gender_of(vote);
            cat(def(vote)->leader_name);
        }
        upper();
        buffer_set_font(b, &cw->font[3], NULL, NULL, NULL);
        write_cent(b, box);
    }

    // The votes the leader has, for the two proposals that count them (when the faction is in the game and has no major atrocity).
    int32_t atrocities = *(int32_t *)((uint8_t *)fac(n) + 0x1d8);
    if (cw->prop <= 1 && cw->votes[n] != 0 && is_alive(n) && atrocities == 0) {
        clear();
        box[3] -= 0x14;
        box[1] -= 0x14;
        cat_number(cw->votes[n]);
        cat(text(S_SPACE));
        cat(game_label(councwin_count_label(cw->votes[n])));
        upper();
        buffer_set_text_color(b, COLOR_PANEL, -1, 1, 1);
        write_cent(b, box);
    }
    graphic_win_soft_update_3(&cw->win, r);
}

static void UI_THIS councwin_draw_all_leaders(struct game_councwin *cw) {
    for (int n = 1; n < 8; n++) councwin_draw_leader(cw, n);
}

// One pass of the panel's drawing. Answers non-zero when it is to be made again from the top (see the file's header).
static int draw_info_pass(struct game_councwin *cw, int *line) {
    struct game_buffer *ib = iface_buf();
    buffer_set_clip(ib, ib->full);
    const int32_t *info = cw->rect_info;
    int32_t r1[4] = {info[0], info[1], info[2], info[3]};
    int32_t r2[4] = {info[0], info[1], info[2], info[1] + 0x14};
    buffer_box_sprite(ib, r2, frame_at(FRAME_TITLE));
    buffer_set_font(ib, &cw->font[1], NULL, NULL, NULL);
    buffer_set_text_color(ib, COLOR_PANEL, -1, 1, 1);
    if (cw->prop == 1) buffer_set_text_color(ib, color_of(cw->target), -1, 1, 1);
    clear();
    cat(game_label(L_TITLE));
    upper();
    write_cent(ib, r2);
    r1[1] = r2[3] + 2;
    buffer_box_sprite(ib, r1, frame_at(FRAME_TEXT));
    r1[0] += 3, r1[2] -= 3, r1[1] += 3, r1[3] -= 3;
    clear();
    const struct proposal *p = &game_proposals[cw->prop];
    cat(game_strings_get(game_strings, p->name));
    upper();
    int h = buffer_wrap_height(ib, game_str_buffer, r1[2] - r1[0]);
    buffer_wrap_cent_6(ib, game_str_buffer, r1);
    r1[1] += h, r1[3] += h;
    if (cw->prop != 0) {
        clear();
        if (cw->prop == 1) {
            cat(game_label(L_SUPREME));
            cat(text(S_SEP));
            gender_of(cw->target);
            cat(def(cw->target)->leader_name);
        } else {
            cat(game_strings_get(game_strings, p->description));
        }
        buffer_wrap_cent_6(ib, game_str_buffer, r1);
    }

    // The foot of the panel: one line, the candidates' votes or the yeas at the left and the nays at the right.
    int fh = councwin_line_height(cw->font[1].first, cw->font[1].ascent, cw->font[1].height);
    int32_t r3[4] = {info[0] + 2, info[3] - fh, info[2] - 2, info[3]};
    if (cw->prop == 0) {
        for (int f = 1; f <= 7; f++) {
            if (!game_council_eligible(f)) continue;
            clear();
            gender_of(f);
            cat(def(f)->leader_name);
            cat(text(S_SEP));
            cat_number(councwin_support(f, cw->vote, cw->votes, alien_mask()));
            buffer_set_text_color(ib, color_of(f), -1, 1, 1);
            if (*line < 0x14) {
                if (*line > 2) {
                    *line = 0x14;
                    return 1;
                }
                if (*line > 1) {
                    if (*game_screen_width < 0x400) {
                        *line = 0x14;
                        return 1;
                    }
                    write_cent(ib, r3);
                } else if (*line) {
                    buffer_write_right_l_6(ib, game_str_buffer, r3, (int)strlen(game_str_buffer));
                } else {
                    buffer_write_l_4(ib, game_str_buffer, r3, (int)strlen(game_str_buffer));
                }
            }
            (*line)++;
        }
    } else {
        int yea = councwin_side_total(-1, cw->vote, cw->votes, alien_mask(), cw->prop);
        buffer_set_text_color(ib, COLOR_YEA, -1, 1, 1);
        clear();
        cat(game_label(L_YEA));
        cat(text(S_SEP));
        cat_number(yea);
        buffer_write_l_4(ib, game_str_buffer, r3, (int)strlen(game_str_buffer));
        int nay = councwin_side_total(-2, cw->vote, cw->votes, alien_mask(), cw->prop);
        buffer_set_text_color(ib, COLOR_NAY, -1, 1, 1);
        clear();
        cat(game_label(L_NAY));
        cat(text(S_SEP));
        cat_number(nay);
        buffer_write_right_l_6(ib, game_str_buffer, r3, (int)strlen(game_str_buffer));
    }
    graphic_win_soft_update_3(iface_win(), cw->rect_info);
    return 0;
}

// The panel of the interface: the proposal's title and text and, at its foot, the tally.
static void UI_THIS councwin_draw_info(struct game_councwin *cw) {
    int line = 0;
    while (draw_info_pass(cw, &line)) {
    }
}

// The whole window: the interface's panel frame, the window's own frame, the panel, the seats, and, once the vote is decided, the
// box with the result (passed or failed, and for the governor's election and the Supreme Leader's, whom).
static void UI_THIS councwin_on_redraw(struct game_councwin *cw) {
    struct game_buffer *ib = iface_buf(), *b = &cw->buf;
    buffer_set_clip(ib, ib->full);
    buffer_set_clip(b, b->full);
    game_main_interface_restore_back(iface_win(), 1);
    int32_t r[4] = {word(IFACE_PANEL) + 4, word(IFACE_PANEL + 4) + 4, word(IFACE_PANEL + 8) - 4, word(IFACE_PANEL + 12) - 4};
    buffer_box_sprite(ib, r, frame_at(FRAME_AREA));
    int32_t back[4] = {0x17, 0, b->width - 0x37, 1 - b->neg_height};
    buffer_box_sprite(b, back, frame_at(FRAME_BACK));
    back[0] += 4, back[1] += 4, back[2] -= 4;
    buffer_box_sprite(b, back, frame_at(FRAME_AREA));
    councwin_draw_info(cw);
    for (int n = 1; n < 8; n++) councwin_draw_leader(cw, n);
    if (cw->result == 0) return;

    int32_t box[4] = {cw->rect_result[0], cw->rect_result[1], cw->rect_result[2], cw->rect_result[3]};
    buffer_box_sprite(b, box, frame_at(FRAME_TEXT));
    box[0] += 3, box[2] -= 3, box[1] += 4, box[3] -= 2;
    clear();
    int prop = cw->prop, target = cw->target;
    switch (cw->result) {
    case 1:
        buffer_set_text_color(b, prop == 0 ? color_of(target) : COLOR_YEA, -1, 1, 1);
        if (prop == 0) {
            gender_of(target);
            cat(def(target)->leader_name);
            cat(text(S_SPACE));
            cat(game_label(councwin_governor_label(*game_governor_faction, cw->governor)));
        } else if (prop == 1) {
            cat(game_label(L_SUPREME));
            cat(text(S_SEP));
            gender_of(target);
            cat(def(target)->leader_name);
            cat(text(S_TAIL));
            buffer_set_text_color(b, color_of(target), -1, 1, 1);
        } else {
            cat(game_label(L_PASSED));
        }
        break;
    case -1:
        buffer_set_text_color(b, COLOR_NAY, -1, 1, 1);
        if (prop == 0) {
            cat(game_label(L_FAILED_GOV));
        } else {
            cat(game_label(L_FAILED));
            if (prop == 1) {
                cat(text(S_SPACE));
                cat(text(S_OPEN));
                cat(game_label(L_SUPREME_FAILED));
                cat(text(S_CLOSE));
            }
        }
        break;
    case -2:
        buffer_set_text_color(b, COLOR_NAY, -1, 1, 1);
        cat(game_label(L_VETO));
        break;
    default: break;
    }
    buffer_set_font(b, &cw->font[5], NULL, NULL, NULL);
    upper();
    buffer_wrap_cent_6(b, game_str_buffer, box);
}

// ---- The mouse ---------------------------------------------------------------------------------------------

static void UI_THIS councwin_on_group_clicked(struct game_councwin *cw, int a, int b) { (void)cw, (void)a, (void)b; }

static void UI_THIS councwin_on_mouse_leave(struct game_councwin *cw, int a, int b) {
    (void)a, (void)b;
    if (cw->hover != -1) {
        int old = cw->hover;
        cw->hover = -1;
        councwin_draw_leader(cw, old);
    }
}

// The mouse over a seat shows the picture under that leader's portrait as the pointer's one (the "under" sprites), and the seat
// the mouse left is drawn again as it was; nothing changes while the button that ends the window is showing, or over the player's
// own seat.
static void UI_THIS councwin_on_mouse_move(struct game_councwin *cw, int x, int y) {
    int id = y;
    if (spot_check(&cw->spot, x, y, &id, NULL) == -1) {
        if (cw->hover != -1) {
            int old = cw->hover;
            cw->hover = -1;
            councwin_draw_leader(cw, old);
        }
        return;
    }
    if (win_is_visible(button_win(&cw->button[5]))) return;
    if (id == *game_current_faction) return;
    if (cw->hover == id) return;
    if (cw->hover != -1) {
        int old = cw->hover;
        cw->hover = -1;
        councwin_draw_leader(cw, old);
    }
    cw->hover = id;
    councwin_draw_leader(cw, id);
}

static void UI_THIS councwin_on_button_passover(struct game_councwin *cw, int a, int b) { (void)cw, (void)a, (void)b; }
static int UI_THIS councwin_on_button_text_draw(struct game_councwin *cw, int a, int b, int c) { (void)cw, (void)a, (void)b, (void)c; return 0; }

// The widgets are put away: the six buttons closed and five of the six fonts (the sixth is never closed).
static void UI_THIS councwin_close(struct game_councwin *cw) {
    for (int i = 0; i < BUTTONS; i++) button_close(&cw->button[i]);
    for (int i = 0; i < 5; i++) font_close(&cw->font[i]);
}

// The window ends: the modal loop is let go, the session flags cleared, the widgets put away and the draw loop run once.
static void close_window(struct game_councwin *cw) {
    release_modal(cw);
    put_word(SESSION, 0);
    put_word(SESSION_B, 0);
    councwin_close(cw);
    game_do_all_draws();
}

// The popup the originals keep on their stacks, taken down as they inline it (see engine/council.c).
static void popup_end(uint8_t *pop) {
    *(uint32_t *)pop = game_va(0x6695c8);
    *(uint32_t *)(pop + 0x444) = game_va(0x6695c0);
    game_popup_close(pop);
    *(uint32_t *)(pop + 0x3230) = game_va(0x669d58);
    *(uint32_t *)(pop + 0x3674) = game_va(0x669d50);
    game_scroll_close(pop + 0x3230);
    game_flat_button_dtor(pop + 0x4828);
    game_flat_button_dtor(pop + 0x3cdc);
    game_graphic_win_dtor(pop + 0x3230);
    game_base_pop_dtor(pop);
}
enum { POP_SPRITE = 0x2118, POP_ICON = 0x2144, POP_DIALOGS = 0x21d0, POP_PICKED = 0xa44 };

static void ask_item(uint8_t *pop, const char *s, int id) { game_dialogs_item(pop + POP_DIALOGS, (char *)s, id); }

static void notice(const char *label, int delay, int unk) { game_netmsg_pop(game_netmsg, (char *)label, delay, unk, NULL); }

// The AI factions that have not yet voted vote (for each faction not a human's that has no vote).
static void fill_ai_votes(struct game_councwin *cw) {
    for (int i = 0; i < 8; i++)
        if (cw->vote[i] == 0 && !((human_mask() >> i) & 1)) cw->vote[i] = game_council_get_vote(i, cw->prop, cw->target);
}

// ---- Keys and clicks ---------------------------------------------------------------------------------------

// Return and Escape end the window when the player's faction is not a human's, or the vote is decided (or, in the scenario
// editor's debugging mode, whatever it is); before that, for a human, they fill in the votes of the AI factions that have none yet,
// or, in a play by e-mail game whose council waits and in which the player has voted, put the window away without carrying the
// proposal out.
static int UI_THIS councwin_on_key_click(struct game_councwin *cw, int a, int key) {
    (void)a;
    if (key != 0x1b && key != 0xd) return 1;
    int cur = *game_current_faction;
    uint32_t state = *game_state;
    if ((human_mask() >> cur) & 1) {
        int close_now = 0;
        if ((state & 0x40) && (state & 0x1000) && *game_pbem == 0) close_now = 1;
        if (!close_now && cw->result == 0) {
            if (*game_pbem && word(PENDING) && cw->vote[cur] != 0) {
                close_window(cw);
                return 1;
            }
            fill_ai_votes(cw);
            return 1;
        }
    }
    close_window(cw);
    if (!net_game() && cw->result == 1) game_council_action(cw->prop, cw->target);
    return 1;
}

// A click on a seat: the player asks the leader for his vote. A faction that is not in the game answers with a notice; one at war
// or wanting revenge refuses; one that has promised its vote to the player already is left be; a vote for the governor asks which
// candidate, the others yea or nay; the player then buys the vote (buy_council_vote, the engine's) and the seat is drawn again.
static void UI_THIS councwin_on_left_click(struct game_councwin *cw, int x, int y) {
    uint8_t pop[BASE_POP_SIZE] __attribute__((aligned(4)));
    game_popup_ctor(pop);
    int id, kind;  // (the kind of the spot, the seat's number, is not used)
    if (spot_check(&cw->spot, x, y, &id, &kind) < 0) {
        popup_end(pop);
        return;
    }
    int f = id, cur = *game_current_faction;
    unsigned bit = 1u << f;
    int choice = 0;
    if (f != cur && cw->result == 0 && !((human_mask() >> f) & 1)) {
        if (alive_mask() & bit) {
            if (fac(f)->diplo_status[cur] & (TREATY_VENDETTA | TREATY_WANT_REVENGE)) {
                // He will not talk: said in his own words.
                say_leader(0, f);
                game_x_pop(text(S_REFUSE), (int)(intptr_t)game_pop_wait);
                popup_end(pop);
                return;
            }
            if (fac(f)->council_pledge[cw->prop] && fac(f)->council_pledge_to[cw->prop] == cur) {
                popup_end(pop);
                return;
            }
            if (cw->prop != 0) {
                choice = -1 - game_x_pop(text(S_ASK), (int)(intptr_t)game_pop_wait);
                if (choice == 0) goto done;
            } else {
                game_popup_start(pop, game_pop_file, text(S_ASK_LIST), -1, 0, 0x200000, 0);
                for (int c = 1; c <= 7; c++) {
                    if (!(alive_mask() & (1u << c)) || !game_council_eligible(c)) continue;
                    clear();
                    gender_of(c);
                    cat(def(c)->leader_title);
                    cat(text(S_SPACE));
                    gender_of(c);
                    cat(def(c)->leader_name);
                    ask_item(pop, game_str_buffer, c);
                    if (c == cur) *(int32_t *)(pop + POP_PICKED) = c;
                }
                choice = game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait);
                if (choice < 0) goto done;
            }
        } else {
            choice = y;
        }
        if (choice == 9999) goto done;
        if (net_game()) {
            cw->waiting = 0;
            unsigned humans = (unsigned)human_mask();
            if ((cw->arrived & (unsigned)alive_mask() & humans) != humans) {
                notice(text(S_NOT_ALL_ASK), 0x1388, 1);
                cw->waiting = 1;
                goto done;
            }
            if (humans & bit) {
                // (not reached: the humans were left out above)
                put_word(TALK_FLAG, 1);
                put_word(TALK_FACTION, f);
                game_message_data(0x1501, f, cur, f, 0, 0);
                say_leader(0, f);
                game_parse_num(0, f);
                notice(text(S_HUMAN), 0x1388, 0);
                cw->waiting = 1;
                goto done;
            }
            if (!(alive_mask() & bit)) {
                say_leader(0, f);
                *game_parse_gender = def(f)->noun_gender;
                *game_parse_plural = def(f)->noun_plural;
                game_parse_says(2, def(f)->noun, -1, -1);
                notice(text(S_DEAD_NET), 0x1388, 0);
                cw->waiting = 1;
                popup_end(pop);
                return;
            }
            if (game_net_talk(game_net_daemon, cur, f)) {
                say_leader(0, f);
                notice(text(S_BUSY), 0x1388, 0);
                cw->waiting = 1;
                popup_end(pop);
                return;
            }
            game_buy_council_vote(cur, f, cw->prop, choice);
            game_diplomacy_ends(cur, f);
            redraw(cw);
            cw->waiting = 1;
        } else if (alive_mask() & bit) {
            game_buy_council_vote(cur, f, cw->prop, choice);
            redraw(cw);
        } else {
            say_leader(0, f);
            *game_parse_gender = def(f)->noun_gender;
            *game_parse_plural = def(f)->noun_plural;
            game_parse_says(2, def(f)->noun, -1, -1);
            notice(text(S_DEAD), 0x1388, 0);
            popup_end(pop);
            return;
        }
    }
done:
    councwin_draw_leader(cw, f);
    popup_end(pop);
}

// The buttons: 1 to 3 vote yea, nay or abstain (the votes are the negated id; in a network game the vote is sent and not set),
// 4 asks for the candidate the player votes for (the governor's election), 5 ends the window as Return does. Id -2 is the button
// no one has: it ends the window when it can be, and otherwise fills in the AI's votes (if no human has voted).
static void UI_THIS councwin_on_button_clicked(struct game_councwin *cw, int id) {
    uint8_t pop[BASE_POP_SIZE] __attribute__((aligned(4)));
    game_popup_ctor(pop);
    int cur = *game_current_faction;
    switch (id) {
    case 1:
    case 2:
    case 3:
        if (cw->result != 0) break;
        if (net_game()) {
            if (!all_arrived(cw)) {
                notice(text(S_NOT_ALL_BUTTON), 0x1388, 1);
                break;
            }
            game_message_data(0x2601, 0, cur, -id, 0, 0);
        } else {
            councwin_set_vote(cw, cur, -id);
        }
        break;
    case 5: UI_VCALL(int(UI_THIS *)(void *, int, int), cw, VT_ON_KEY_CLICK, 0, 0xd); break;
    case 4: {
        if (cw->result != 0) break;
        if (net_game() && !all_arrived(cw)) {
            notice(text(S_NOT_ALL_CHOOSE), 0x1388, 1);
            break;
        }
        game_popup_start(pop, game_pop_file, text(S_CHOOSE), -1, 0, 0x200040, 0);
        int w, h;
        if (!game_pcx_dimensions(text(S_CHOOSE_PIC), &w, &h) && !game_sprite_init(pop + POP_SPRITE, text(S_CHOOSE_SPRITE), w, h))
            *(void **)(pop + POP_ICON) = pop + POP_SPRITE;
        ask_item(pop, game_label(L_ABSTAIN), 0);
        for (int c = 1; c <= 7; c++) {
            if (alien(c) || !(alive_mask() & (1u << c)) || !game_council_eligible(c)) continue;
            clear();
            gender_of(c);
            cat(def(c)->leader_name);
            ask_item(pop, game_str_buffer, c);
        }
        int choice = game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait);
        if (choice < 0) break;
        if (choice == 0) choice = -3;
        if (net_game()) game_message_data(0x2601, 0, cur, choice, 0, 0);
        else councwin_set_vote(cw, cur, choice);
        break;
    }
    case -2: {
        int close_now = cw->result != 0;
        if (!close_now) {
            int editor = (*game_state & 0x40) && !net_game();
            if (editor ? !*game_pbem : 0) close_now = 1;
            else if (*game_pbem && cw->vote[cur] != 0) close_now = 1;
        }
        if (close_now) {
            close_window(cw);
            if (!net_game() && cw->result == 1) game_council_action(cw->prop, cw->target);
            break;
        }
        int human_voted = 0;
        for (int i = 0; i < 8; i++)
            if (((human_mask() >> i) & 1) && cw->vote[i] != 0) human_voted = 1;
        if (!human_voted) fill_ai_votes(cw);
        break;
    }
    default: break;
    }
    popup_end(pop);
}

// ---- The interface's part -----------------------------------------------------------------------------------

// What the interface calls when the window is to be shown (status non-zero: the window drawn, the buttons but the last shown, the
// window slid in) or put away (the buttons hidden, the window slid out and hidden).
static void UI_THIS councwin_on_status(void *iface, int status) {
    struct game_councwin *cw = (struct game_councwin *)((uint8_t *)iface - IFACE_PART);
    if (status) {
        UI_VCALL(void(UI_THIS *)(void *), cw, VT_REDRAW_FN);
        for (int i = 0; i < BUTTONS; i++)
            if (i != 5) show(&cw->button[i], 0);
        graphic_win_update_3(iface_win(), 0);
        game_do_all_draws();
        game_slide_show(cw, 0);
        game_win_show(cw, 3);
    } else {
        for (int i = 0; i < BUTTONS; i++) hide(&cw->button[i]);
        game_release_iface_mode(iface);
        game_slide_hide(cw, 0);
        game_win_hide(cw);
        game_do_all_draws();
    }
}

// The interface's calls the window does nothing for.
static void UI_THIS councwin_iface_stub_8(void *iface, int a, int b) { (void)iface, (void)a, (void)b; }
static void UI_THIS councwin_iface_stub_c(void *iface, int a, int b, int c) { (void)iface, (void)a, (void)b, (void)c; }

// The interface's button click, handed to the window's own handler (slot 0xb0).
static void UI_THIS councwin_on_iface_button(void *iface, int id) {
    struct game_councwin *cw = (struct game_councwin *)((uint8_t *)iface - IFACE_PART);
    UI_VCALL(void(UI_THIS *)(void *, int), cw, VT_FORWARD, id);
}

// ---- Showing the window ----------------------------------------------------------------------------------

static void UI_THIS councwin_show(struct game_councwin *cw, int arg) {
    (void)arg;
    if (!win_is_visible(&cw->win)) game_set_iface_mode((uint8_t *)cw + IFACE_PART);
}

static int UI_THIS councwin_hide(struct game_councwin *cw) { return win_is_visible(&cw->win); }

// A human faction has come to the window (a network game, or the play by e-mail game's turn): it is marked, the window drawn again
// and, when every human has come, the players are told.
static void UI_THIS councwin_arrived(struct game_councwin *cw, int faction) {
    cw->arrived |= 1u << faction;
    if (!word(SESSION)) return;
    redraw(cw);
    if (all_arrived(cw)) notice(text(S_ALL_ARRIVED), 0x3e8, 1);
}

// Makes one of the buttons the window has: its text is the label (as capitals), its place the rectangle, its parent the interface.
static void make_button(struct game_councwin *cw, int index, int id, int label, const int32_t *rect) {
    clear();
    cat(game_label(label));
    upper();
    game_base_button_init(&cw->button[index], game_str_buffer, id, rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], iface_win(), 0);
}

// What an AI faction offers the player for his vote: for each faction that could be elected, whose election his votes could decide,
// a tech or two, or a price in energy, and the offer is taken or refused with a popup; the first tech offer taken ends the offers.
static void make_offers(struct game_councwin *cw, int cur) {
    int tot[8] = {0};
    for (int a = 1; a <= 7; a++) {
        tot[a] = 0;
        if (alien(a) || !(alive_mask() & (1u << a))) continue;
        for (int b = 1; b <= 7; b++)
            if (!alien(b) && (alive_mask() & (1u << b)) && cw->vote[b] == a) tot[a] += cw->votes[b];
    }
    int taken = 0;
    for (int a = 1; a <= 7; a++) {
        if (alien(a) || !(alive_mask() & (1u << a)) || !game_council_eligible(a)) continue;
        int strongest = 0;
        for (int b = 1; b <= 7; b++)
            if (!alien(b) && b != a && strongest <= tot[b]) strongest = tot[b];
        int own = cw->votes[cur];
        if (!councwin_pivotal(own, tot[a], strongest)) continue;
        game_diplomacy_check(cur, a, 0);
        if (*game_diplo_friction >= 0xf || *game_diplo_hostility != 0 || taken) continue;
        if (fac(a)->diplo_status[cur] & 0x40c31) continue;
        int price = councwin_offer_price(*game_turn, fac(a)->base_count, fac(a)->energy_credits);
        void *sprite = (void *)(uintptr_t)word(OFFER_SPRITES + 4 * a);
        if (price != 0 && fac(a)->energy_credits >= price) {
            game_parse_num(0, price);
            game_diplomacy_caption(cur, a);
            say_leader(0, cur);
            say_leader(2, a);
            game_clock_wait(0x1f4);
            if (game_x_pops_11(text(S_OFFER_MONEY), 0, sprite, NULL)) {
                fac(a)->energy_credits -= price;
                fac(cur)->energy_credits += price;
                game_console_update_data(game_console, 0);
                councwin_set_vote(cw, cur, a);
                return;  // (the original falls out of the loop here: a price paid ends the offers)
            }
            continue;
        }
        game_tech_analysis(cur, a);
        if (*game_trade_offer_tech[0] < 0 || *game_trade_offer_tech[1] < 0) continue;
        clear();
        game_say_tech(game_str_buffer, *game_trade_offer_tech[0], 0);
        game_parse_says(4, game_str_buffer, -1, -1);
        clear();
        game_say_tech(game_str_buffer, *game_trade_offer_tech[1], 0);
        game_parse_says(5, game_str_buffer, -1, -1);
        game_diplomacy_caption(cur, a);
        say_leader(0, cur);
        say_leader(2, a);
        game_clock_wait(0x1f4);
        if (game_x_pops_11(text(S_OFFER_TECH), 0, sprite, NULL)) {
            taken = 1;
            game_tech_achieve(cur, *game_trade_offer_tech[0], a, 0);
            game_tech_achieve(cur, *game_trade_offer_tech[1], a, 0);
            councwin_set_vote(cw, cur, a);
        }
    }
}

// Opens the window for a proposal (`prop`, about the faction `target`) and runs it until it is closed: the layout from the interface's
// panel, the fonts and buttons made, the seats' portraits taken from the game's sheets, the AI factions' votes asked for (the player,
// and a network game's humans, have none yet), the window shown, offers made to the player if his votes could decide the election of
// a governor, and then the modal loop, which ends with close().
static void UI_THIS councwin_init(struct game_councwin *cw, int prop, int target) {
    struct game_buffer scratch;
    buffer_ctor(&scratch);
    for (int i = 0; i < BUTTONS; i++) button_close(&cw->button[i]);
    for (int i = 0; i < 5; i++) font_close(&cw->font[i]);
    spot_init(&cw->spot, 8);
    uint8_t *iface = (uint8_t *)cw + IFACE_PART;
    *(int32_t *)(iface + 4) = 7;
    *(uint32_t *)game_at(IFACE_SLOT) = (uint32_t)(uintptr_t)iface;

    static const int32_t seats[LEADER_RECTS - 2][4] = {
        {0x1e, 0x35, 0x80, 0x14f},  {0x20, 0x1d, 0x82, 0x137},  {0x82, 0x2d, 0xe4, 0x147},  {0x87, 0x1d, 0xe9, 0x137},
        {0xe6, 0x25, 0x148, 0x13f}, {0xee, 0x1d, 0x150, 0x137}, {0x14a, 0x1d, 0x1c2, 0x160}, {0x155, 0x1d, 0x1b7, 0x137},
        {0x1c4, 0x25, 0x226, 0x13f}, {0x1bc, 0x1d, 0x21e, 0x137}, {0x228, 0x2d, 0x28a, 0x147}, {0x223, 0x1d, 0x285, 0x137},
        {0x28c, 0x35, 0x2ee, 0x14f}, {0x28a, 0x1d, 0x2ec, 0x137}};
    for (int i = 0; i < LEADER_RECTS - 2; i++) memcpy(cw->rect_seat[2 + i], seats[i], 16);
    cw->rect_title[0] = 0x1e, cw->rect_title[1] = 7, cw->rect_title[2] = 0x2ee, cw->rect_title[3] = 0x1b;
    cw->rect_result[0] = 0xf5, cw->rect_result[1] = 0x163, cw->rect_result[2] = 0x21a, cw->rect_result[3] = 0x17a;

    // The panel's text area and the buttons' places come from the interface's panel.
    struct councwin_layout lay;
    int32_t panel[4] = {word(IFACE_PANEL), word(IFACE_PANEL + 4), word(IFACE_PANEL + 8), word(IFACE_PANEL + 12)};
    councwin_layout(&lay, panel);
    memcpy(cw->rect_info, lay.info, 16);
    memcpy(cw->rect_button[3], lay.close, 16);
    for (int i = 0; i < 3; i++) memcpy(cw->rect_button[i], lay.button[i], 16);

    // Fonts: the buttons', the panel's title, the names, the votes' words (the third with its spacing), the unused, the result.
    font_init(&cw->font[5], *game_font_face, 0x14, 1);
    font_init(&cw->font[1], *game_font_face, 0xe, 1);
    font_init(&cw->font[0], *game_font_face, 0xe, 1);
    font_init(&cw->font[2], *game_font_face, 0xc, 1);
    cw->font[2].first = 5;
    font_init(&cw->font[3], *game_font_face, 0xc, 1);
    font_init(&cw->font[4], *game_font_face, 0xe, 1);

    cw->prop = prop;
    cw->target = target;
    cw->governor = *game_governor_faction;
    cw->arrived = *game_pbem ? 0xff : ~(uint32_t)(human_mask() & 0xff);
    put_word(SESSION_B, 1);
    cw->opened = game_time_get_time();
    cw->timeout = game_bit_count((uint32_t)(human_mask() & 0xff)) * 60000;
    cw->waiting = 1;
    int cur = *game_current_faction;
    if (!(*game_pbem && word(PENDING))) {
        cw->result = 0;
        for (int i = 0; i < 8; i++) cw->votes[i] = game_council_votes(i);
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < 8; i++) {
                int v;
                if (((human_mask() >> i) & 1) && (i == cur || net_game() || *game_pbem)) v = 0;
                else v = game_council_get_vote(i, cw->prop, target);
                cw->vote[i] = v;
                if (!((alive_mask() >> i) & 1)) cw->vote[i] = -5;
            }
        if (*game_pbem) put_word(PENDING, cur);
    }

    // The faction's pictures: each faction has four sprites in the game's sheet, 0x2c bytes each.
    for (int i = 0; i < 8; i++) {
        uint32_t base = PORTRAITS + 0x65c * (uint32_t)i;
        cw->face_a[2 * i] = game_at(base - 0x2c);
        cw->face_a[2 * i + 1] = game_at(base);
        cw->face_b[2 * i] = game_at(base + 0x2c);
        cw->face_b[2 * i + 1] = game_at(base + 0x58);
    }

    struct councwin_position pos;
    councwin_position(&pos, *game_win_screen_w, *game_win_screen_h, word(IFACE_TOP));
    graphic_win_init(&cw->win, pos.x, pos.y, 0x32c, pos.h, NULL, 0x1000000, console_win_part(), NULL, NULL);
    graphic_win_fill(&cw->win, 9);

    make_button(cw, 1, 1, L_YEA, cw->rect_button[0]);
    if (prop == 0) {
        make_button(cw, 4, 4, L_CHOOSE, cw->rect_button[3]);
    } else {
        make_button(cw, 2, 2, cur == *game_governor_faction ? L_OWN_NAY : L_NAY, cw->rect_button[1]);
        make_button(cw, 3, 3, L_ABSTAIN, cw->rect_button[2]);
        make_button(cw, 1, 1, L_YEA, cw->rect_button[0]);
    }
    make_button(cw, 5, 5, L_OK, cw->rect_button[3]);
    hide(&cw->button[5]);
    for (int i = 0; i < BUTTONS; i++) {
        hide(&cw->button[i]);
        buffer_set_font(&cw->button[i].base.buf, &cw->font[0], NULL, NULL, NULL);
        redraw(&cw->button[i]);
    }
    put_word(SESSION, 1);
    if (net_game()) game_message_data(0x2602, 0, cur, 0, 0, 0);
    int hid_overlay = 0, hid_other = 0;
    if (win_is_visible(win_object(OVERLAY_WIN))) {
        hid_overlay = 1;
        game_overlay_open(win_object(OVERLAY_WIN));
    }
    if (win_is_visible(win_object(OTHER_WIN))) {
        hid_other = 1;
        game_window_opening(win_object(OTHER_WIN));
    }
    show(cw, 0);
    if (!net_game() && cw->prop == 0 && !game_council_eligible(cur) && fac(cur)->diff_level != 0) make_offers(cw, cur);
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), cw, VT_SET_MODAL, 0, (int)(intptr_t)game_pop_wait, (int)(intptr_t)iface_win());
    game_release_iface_mode(iface);
    if (hid_other) game_window_closing(win_object(OTHER_WIN));
    if (hid_overlay) game_overlay_close(win_object(OVERLAY_WIN));
    buffer_dtor(&scratch);
}

// ---- Hooks -------------------------------------------------------------------------------------------------

// set_vote's shadow side: a check that buys the player a vote holds the call (it is made when the check is over), as quiet.c held it.
static void replay_set_vote(const struct hook_ui_call *c) {
    const int *a = c->arg;
    councwin_set_vote((struct game_councwin *)(intptr_t)a[4], a[0], a[1]);
}

static struct hook_stats stats_set_vote = {"councwin_set_vote"};
static void *orig_set_vote;

static void UI_THIS shadow_set_vote(struct game_councwin *cw, int faction, int vote) {
    if (!hooks_protecting()) {
        councwin_set_vote(cw, faction, vote);
        return;
    }
    stats_set_vote.calls++;
    struct hook_ui_call c = {.name = "council_win_set_vote", .replay = replay_set_vote, .arg = {faction, vote, 0, 0, (int)(intptr_t)cw}};
    hooks_ui_hold(&c);
}

static const struct hook councwin_hooks[] = {
    UI_HOOK(0x424410, councwin_make_resolution, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x38),  // push ebp; mov ebp, esp; sub esp, 0x38
    {0x424720, (const uint8_t[]){0x55, 0x8b, 0xec, 0x53, 0x8b, 0xd9}, 6, (void *)councwin_set_vote, (void *)shadow_set_vote, &orig_set_vote,
     &stats_set_vote},
    UI_HOOK(0x424870, councwin_on_button_clicked, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    UI_HOOK(0x425050, councwin_on_left_click, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    UI_HOOK(0x425ba0, councwin_on_key_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c),
    UI_HOOK(0x425d90, councwin_draw_all_leaders, 0x56, 0x57, 0x8b, 0xf9, 0xbe, 0x01, 0x00, 0x00, 0x00),
    UI_HOOK(0x425db0, councwin_draw_leader, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x78),
    UI_HOOK(0x426470, councwin_draw_info, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c),
    UI_HOOK(0x426aa0, councwin_on_redraw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),
    UI_HOOK(0x426eb0, councwin_on_group_clicked, 0xc2, 0x08, 0x00, 0x90, 0x90),  // ret 8
    UI_HOOK(0x426ec0, councwin_on_mouse_leave, 0x8b, 0x81, 0x34, 0x0a, 0x00, 0x00),  // mov eax, [ecx+0xa34]
    UI_HOOK(0x426ee0, councwin_on_mouse_move, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08),
    UI_HOOK(0x426f80, councwin_on_button_passover, 0xc2, 0x08, 0x00, 0x90, 0x90),  // ret 8
    UI_HOOK(0x426f90, councwin_on_button_text_draw, 0x33, 0xc0, 0xc2, 0x0c, 0x00),  // xor eax, eax; ret 0xc
    UI_HOOK(0x426fa0, councwin_on_status, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x427060, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),  // the interface's calls that do nothing: ret 8
    UI_HOOK(0x427070, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x427080, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x427090, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270a0, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270b0, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270c0, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270d0, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270e0, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x4270f0, councwin_on_iface_button, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08),
    UI_HOOK(0x427110, councwin_iface_stub_8, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x427120, councwin_iface_stub_c, 0xc2, 0x0c, 0x00, 0x90, 0x90),  // ret 0xc
    UI_HOOK_CALL(0x427130, councwin_show, 0x56, 0x8b, 0xf1, 0xe8, 0x58, 0x0d, 0x1d, 0x00),  // push esi; mov esi, ecx; call win_is_visible
    UI_HOOK(0x427150, councwin_hide, 0xe9, 0x3b, 0x0d, 0x1d, 0x00),  // jmp win_is_visible
    UI_HOOK(0x427160, councwin_close, 0x53, 0x8b, 0xd9, 0x56, 0x57),
    UI_HOOK(0x4271c0, councwin_arrived, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x427230, councwin_init, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xff, 0x30, 0x65, 0x00),
    UI_HOOK(0x428550, councwin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xaa, 0x2c, 0x65, 0x00),
    UI_HOOK(0x428620, councwin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x2a, 0x2d, 0x65, 0x00),
    UI_HOOK(0x428710, councwin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x428740, councwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module councwin_module = {"councwin", councwin_hooks, UI_HOOK_COUNT(councwin_hooks), NULL, NULL, -1};
