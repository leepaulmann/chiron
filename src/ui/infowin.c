// The info panel on the main screen, the game's InfoWin: the two text boxes in the strip under the map, which show
// one thing at a time (the calendar, the economy, the faction's leader, the ranking, a chart, the planet) and type it
// out a letter at a time. It is one global object (0x7ad2a0) built by a static initializer and torn down by an atexit
// handler; it is not a window and has no virtual table. The main screen's code reaches it by absolute address: the
// click handler (left click, and the right click that opens a menu of the modes), the turn code (the window's
// set_state from the faction and probe code, which also write its `advance_from` word), the reset the main window's
// layout calls, and the words at 0x7ad348 (`hold`) and 0x7ad34c (`suspended`) that a few places set. It draws into
// the main window's Buffer (0x7aec64), clipped to the box, and asks the window to copy the box to the screen.
//
// The clock: a Time object inside it ticks every 50 ms and calls the timer daemon (0x459260, a function the object
// hands the game, so a hook target too), which does nothing while a word at 0x68f21c is set, else the tick (timer_proc)
// moves the typing one character on. When a box has been typed past its text plus 0x32 the panel moves to the next
// mode, unless the mouse is over it or it is held, when it holds the text (flashing) instead.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=infowin gives them all back. The object keeps the original's layout, the toolkit is called directly, and
// what it draws, in what order, and what it writes are the original's. What isn't rebuilt: the game functions the text
// is built from (the year, the rankings, the energy figures, say_special, the council's votes), the PullDown's line
// add and show, and the arrow the tutorial draws over the screen.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the original's mode
// handling of a word at +0xb0 (a size selector nothing ever sets, whose cases only fed locals that the text builder
// never uses) and of the offsets the layout pass computed for a negative mode (a mode that is -1 shows nothing) are
// not built; a line drawn with nothing left to scroll (the original reads a stack byte that, as the text builder's
// frame is where it lies, is always zero) is drawn as an empty string.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "infowin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/council.h"
#include "../engine/faction.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/score.h"
#include "../engine/strlist.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure arithmetic ----------------------------------------------------------------------------------------

void infowin_layout(struct ui_rect *first, struct ui_rect *second, const struct ui_rect *area, int screen_w) {
    int left = area->left + 7, top = area->top + 7, right = area->right - 0x20 - 7, bottom = area->bottom - 7;
    if (screen_w == 0x320) {
        *first = (struct ui_rect){left, top, right, bottom};
        *second = *first;
        return;
    }
    int half = (right - left - 3) / 2;
    int mid = left + half;
    *first = (struct ui_rect){left, top, mid, bottom};
    int step = mid - left + 3;
    *second = (struct ui_rect){left + step, top, mid + step, bottom};
}

int infowin_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

int infowin_mode_valid(int mode, const struct infowin_facts *f) {
    if (mode == 0) return 1;
    if (f->turn == 0 || (unsigned)mode > 11) return 0;
    switch (mode) {
    case 1:
        if (f->turn < 0x32) return 0;
        return f->turn >= 0x64 || f->difficulty >= 2;
    case 2:
        return f->contact != 0;
    case 3:
        if (f->contact) return 1;
        if (f->turn < 0x32 && f->difficulty < 2) return 0;
        return f->pact != 0;
    case 4:
    case 5:
    case 6:
        return f->turn >= 0x14 || (f->flags & 0x10) != 0;
    case 7:
        return (f->flags & 0x80) && (f->flags & 0x40);
    case 11:
        return 1;
    default:
        return 0;
    }
}

int infowin_next_mode(int mode) { return (mode + 1) % 8; }

enum infowin_tick infowin_tick_action(int scroll, int length) {
    if (scroll > length + 0x32) return INFOWIN_TICK_HOLD;
    return scroll > length + 1 ? INFOWIN_TICK_NOTHING : INFOWIN_TICK_DRAW;
}

enum infowin_line infowin_line_action(int scroll, int length) {
    if (scroll > length) return INFOWIN_LINE_FULL;
    return scroll == 0 ? INFOWIN_LINE_NONE : INFOWIN_LINE_PARTIAL;
}

int infowin_title_upper(int a, int b, int c) { return (a < 0x2328 || b < 0x2328) && c == 0; }

int infowin_bar_height(int value, int span, int most) {
    int h = value * span / most;
    return h < 1 ? 1 : h;
}

void infowin_bar_run(int left, int right, int *first, int *width) {
    int w = (right - left) / 7;
    *width = w;
    *first = left + (right - 7 * w - left) / 2;
}

int infowin_chart_step(int turns, int width) {
    int step = 1;
    if (turns > width)
        do step++;
        while (turns / step > width);
    return step;
}

int infowin_chart_scale(int height, int most) {
    if (height >= most) return 1;
    if (height <= 0) return 1;  // (the original would go round for ever)
    int sum = height, k = 1;
    do {
        sum += height;
        k++;
    } while (sum < most);
    return k;
}

int infowin_planet_name(int state) {
    if (state >= 0 && state <= 5) return state;
    if (state >= 26 && state <= 29) return state - 20;
    return -1;
}

int infowin_planet_table_offset(int state) { return (state - 6) * 32; }

// ---- The object -----------------------------------------------------------------------------------------------

enum { INFOWIN_BASE = 0x7ad2a0, LINES = 12, LINE_LEN = 0x50, PAGES = 2 };

struct game_infowin {
    struct ui_rect bounds;      // 0x0: both boxes together
    struct ui_rect box[PAGES];  // 0x10: the first box, 0x20: the second (the same rectangle on an 800-wide screen)
    struct game_time timer;     // 0x30: the 50 ms tick
    struct font font;           // 0x58: the small print the boxes are written in (its `first` is 4, the extra line spacing)
    int32_t page;               // 0x80: the box that shows (0 or 1)
    int32_t one;                // 0x84: always 1, and nothing reads it
    int32_t mode[PAGES];        // 0x88: what each box shows: 0 to 7 as the box's mode, or -1 before any
    int32_t advance_from;       // 0x90: the mode the next change moves on from (set by the game's turn code), or -1
    int32_t flash;              // 0x94: whether the held text is in the flash colours
    GAME_PAD(0x98, 0xa8);
    int32_t hold;               // 0xa8: set while the program wants the text kept (a click, a message)
    int32_t suspended;          // 0xac: set while the clock is to do nothing (by the game, around some screens)
    int32_t selector;           // 0xb0: nothing ever sets it
    int32_t minus_one;          // 0xb4: set to -1, which nothing reads
    char text[PAGES][LINES][LINE_LEN];  // 0xb8: the lines of each box
    int32_t x[PAGES * LINES];           // 0x838: where each line is written
    int32_t y[PAGES * LINES];           // 0x898
    int32_t color[PAGES * LINES];       // 0x8f8: a colour for the line, or 0 for the game's usual two
    int32_t color2[PAGES * LINES];      // 0x958: the colour the line takes when it is held
    int32_t lines[PAGES];               // 0x9b8: how many lines each box has
    int32_t length[PAGES];              // 0x9c0: how many characters of text each box has
    int32_t scroll[PAGES];              // 0x9c8: how many characters of each box are shown
    struct game_push_button button;     // 0x9d0: built and torn down with the object and not used
};
GAME_AT(struct game_infowin, bounds, 0x0);
GAME_AT(struct game_infowin, box, 0x10);
GAME_AT(struct game_infowin, timer, 0x30);
GAME_AT(struct game_infowin, font, 0x58);
GAME_AT(struct game_infowin, page, 0x80);
GAME_AT(struct game_infowin, one, 0x84);
GAME_AT(struct game_infowin, mode, 0x88);
GAME_AT(struct game_infowin, advance_from, 0x90);
GAME_AT(struct game_infowin, flash, 0x94);
GAME_AT(struct game_infowin, hold, 0xa8);
GAME_AT(struct game_infowin, suspended, 0xac);
GAME_AT(struct game_infowin, selector, 0xb0);
GAME_AT(struct game_infowin, minus_one, 0xb4);
GAME_AT(struct game_infowin, text, 0xb8);
GAME_AT(struct game_infowin, x, 0x838);
GAME_AT(struct game_infowin, y, 0x898);
GAME_AT(struct game_infowin, color, 0x8f8);
GAME_AT(struct game_infowin, color2, 0x958);
GAME_AT(struct game_infowin, lines, 0x9b8);
GAME_AT(struct game_infowin, length, 0x9c0);
GAME_AT(struct game_infowin, scroll, 0x9c8);
GAME_AT(struct game_infowin, button, 0x9d0);
// (the next object of the game, an array of words at 0x7ae778, follows)
_Static_assert(sizeof(struct game_infowin) <= 0x7ae778 - INFOWIN_BASE, "layout of the game's InfoWin");

// The game's data it reads (by address).
enum {
    MAIN_WIN = 0x7ae820,        // the main window, whose Buffer (at 0x444) the boxes are drawn in
    MAIN_BUFFER = 0x7aec64,
    BOX_FRAME = 0x78d7f8,       // the frame sprite the boxes are drawn with
    STATUS_WIN = 0x8c5568,
    TUT_WIN = 0x8c6e68,
    PANEL_AREA = 0x7af5c0,      // the rectangle the panel lies in (left, top, right, bottom)
    SCREEN_WIDTH = 0x9b7b1c,
    FONT_FACE = 0x691b2c,       // char *: the name of the face
    MAP_OWNER = 0x939284,       // the faction whose map the player sees
    VIEWED_FACTION = 0x93fac0,  // the faction the leader and treaty boxes show
    CONTACT = 0x93fab4,
    TURN = 0x9a64d4,            // how many turns have been played
    GAME_FLAGS = 0x9a64c0,
    GAME_STATE = 0x9a649c,
    ACTIVE_MASK_BYTE = 0x9a64e9,  // bit per faction that is in the game, in the byte after the word at 0x9a64e8
    RANK_ORDER = 0x9a6508,      // the factions' order, strongest first, the words going down from here
    RANK_COUNT = 0x9a650c,
    TIMER_BLOCK = 0x68f21c,     // while it is set the clock does nothing
    BUSY_TABLE = 0x7ae778,      // words by the game's mode (0x7d392c): while one is set the panel stays as it is
    BUSY_INDEX = 0x7d392c,
    POPUP_WINDOW = 0x7b7264,    // a window whose being up stops the clock
    STATUS_SIDE = 0x939ef8,     // the rectangle (and its flags at 0x939ed8) the panel registers
    STATUS_FLAGS = 0x939ed8,
    STATUS_WIDTH = 0x939e5c,
    STATUS_BASE = 0x9156b0,
    STATUS_VBASE_FIELD = 0x915b78,
    LEFT_CLICK_FLAG = 0x6e8150,
    DIRTY = 0x7af174,           // four words the game's drawing reads, cleared after a box is drawn in part
    TEXT_COLORS = 0x8c6bb4,     // the colours of the game's text: [0] normal and [2] the flash, in pairs
    FACTION_COLORS = 0x68f9f0,  // per faction: the bar's colour, then (0x20 on) the line's, the text's...
    GRAMMAR_GENDER = 0x9bbfec,  // what the game's text builder reads for the word it is given
    GRAMMAR_PLURAL = 0x9bbff0,
    RANKING_VALUES = 0x945ddc,  // seven words: each faction's strength as the rankings left it
    HISTORY = 0x9a68be,         // 16 bytes a turn, seven halfwords from here: each faction's strength that turn
    FACTIONS = 0x96c9e0,        // faction records, 0x20cc apart
    FACTION_DEFS = 0x946a50,    // faction files' data, 0x59c apart
    MOOD_NAMES = 0x94c9e4,
    MENU_SEPARATOR_ID = 0x63,
    MENU_HELP_ID = 0x14,
};
enum { FACTION_SIZE = 0x20cc, FDEF_SIZE = 0x59c };

// The strings the text is put together from (found in the game's data): the colon, the space, the percent sign and
// the brackets, and the "{" and "}" that mark a stretch of the text for the text's special colour.
enum { S_COLON = 0x682e94, S_SPACE = 0x682820, S_PERCENT = 0x682ea0, S_OPEN = 0x682e9c, S_CLOSE = 0x682e98, S_PLUS = 0x6851a0 };

static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static char *text(uint32_t addr) { return game_at(addr); }
static const char *label(int byte_offset) { return game_label(byte_offset / 4); }
// The text of a string the game keeps by its id.
static const char *string(const char *id) { return game_strings_get(game_strings, (char *)id); }
// The string a word of the game's data points to, from the string table.
static const char *string_at(uint32_t addr) { return string((const char *)(uintptr_t)word(addr)); }
static struct game_buffer *main_buffer(void) { return game_at(MAIN_BUFFER); }
static struct game_win *main_win(void) { return game_at(MAIN_WIN); }
static int32_t fac(int f, int off) { return *(int32_t *)game_at(FACTIONS + f * FACTION_SIZE + off); }
static char *fdef(int f, int off) { return game_at(FACTION_DEFS + f * FDEF_SIZE + off); }
static int32_t fdef_word(int f, int off) { return *(int32_t *)fdef(f, off); }

static void cat(char *t, const char *s) { strcat(t, s); }
static void cat_number(char *t, int v) {
    char n[34];
    sprintf(n, "%d", v);
    strcat(t, n);
}
static void set_grammar(int32_t gender, int32_t plural) {
    *(int32_t *)text(GRAMMAR_GENDER) = gender;
    *(int32_t *)text(GRAMMAR_PLURAL) = plural;
}
// A faction's name as the panel writes it: the noun, a space, the adjective (the grammar words set before each).
static void cat_faction(char *t, int f) {
    set_grammar(fdef_word(f, 0), 0);
    cat(t, fdef(f, 0x4c));
    cat(t, text(S_SPACE));
    set_grammar(fdef_word(f, 0), 0);
    cat(t, fdef(f, 0x34));
}
static void cat_leader(char *t, int f) {
    set_grammar(fdef_word(f, 0x2fc), fdef_word(f, 0x300));
    cat(t, fdef(f, 0x384));
}
static void put(struct game_infowin *iw, int page, int slot, const char *t) { strcpy(iw->text[page][slot], t); }

// ---- Whether a mode can be shown -------------------------------------------------------------------------

static int mode_valid(int mode) {
    struct infowin_facts facts = {word(TURN), fac(word(MAP_OWNER), 8), word(CONTACT), *(uint8_t *)text(GAME_FLAGS), 0};
    if (mode == 3) {
        int f = word(MAP_OWNER);
        for (int other = 1; other < 8; other++)
            if (other != f && (fac(f, 0x18 + 4 * other) & 8)) {
                facts.pact = 1;
                break;
            }
    }
    return infowin_mode_valid(mode, &facts);
}

static int UI_THIS infowin_state_valid(struct game_infowin *iw, int mode) {
    (void)iw;
    return mode_valid(mode);
}

// ---- The panel's text ---------------------------------------------------------------------------------------

// The calendar box: the year, the research, and the economy split.
static int mode_calendar(struct game_infowin *iw, int p, int f) {
    char t[0x100];
    t[0] = 0;
    cat(t, label(0));
    cat(t, text(S_COLON));
    cat(t, text(0x68518c));
    say_year(t);
    cat(t, text(0x685190));
    put(iw, p, 0, t);
    game_energy_compute(f, 0);
    int research = fac(f, 0x3cc);
    if (research >= 0 && ((*(uint8_t *)text(GAME_FLAGS) & 0x80) || !(word(GAME_STATE) & 0x200))) {
        t[0] = 0;
        cat(t, label(0x1a8));
        cat(t, text(S_COLON));
        cat(t, text(0x685194));
        cat(t, string_at(0x94f35c + 44 * research));
        cat(t, text(0x685198));
        put(iw, p, 1, t);
    }
    t[0] = 0;
    cat(t, string_at(0x94a318));
    cat(t, text(S_COLON));
    cat(t, text(0x68519c));
    cat_number(t, (10 - fac(f, 0x328) - fac(f, 0x324)) * 10);
    cat(t, text(S_PERCENT));
    cat(t, text(S_SPACE));
    cat(t, text(S_SPACE));
    cat(t, text(S_OPEN));
    if (word(0x749c0c) >= 0) cat(t, text(S_PLUS));
    cat_number(t, word(0x749c0c));
    cat(t, text(0x6851a4));
    cat(t, label(0xc0));
    cat(t, text(S_CLOSE));
    cat(t, text(0x6851a8));
    put(iw, p, 2, t);
    t[0] = 0;
    cat(t, string_at(0x94a320));
    cat(t, text(S_COLON));
    cat(t, text(0x6851ac));
    cat_number(t, fac(f, 0x324) * 10);
    cat(t, text(S_PERCENT));
    cat(t, text(0x6851b0));
    put(iw, p, 3, t);
    t[0] = 0;
    cat(t, string_at(0x94a328));
    cat(t, text(S_COLON));
    cat(t, text(0x6851b4));
    cat_number(t, fac(f, 0x328) * 10);
    cat(t, text(S_PERCENT));
    if (word(0x749ae4) < 0x2328) {
        cat(t, text(S_SPACE));
        cat(t, text(S_SPACE));
        cat(t, text(S_OPEN));
        cat_number(t, word(0x749ae4));
        cat(t, text(S_SPACE));
        cat(t, label(0xc4));
        cat(t, text(S_CLOSE));
    }
    cat(t, text(0x6851b8));
    put(iw, p, 4, t);
    return 5;
}

// The economy box: the energy figures.
static int mode_economy(struct game_infowin *iw, int p, int f) {
    char t[0x100];
    game_energy_compute(f, 0);
    t[0] = 0;
    cat(t, label(0x4b0));
    cat(t, text(S_COLON));
    cat(t, text(0x6851bc));
    cat_number(t, fac(f, 0x304) + 1);
    cat(t, text(0x6851c0));
    put(iw, p, 0, t);
    t[0] = 0;
    cat(t, label(0x4b4));
    cat(t, text(S_COLON));
    cat(t, text(0x6851c4));
    cat_number(t, fac(f, 0x308));
    cat(t, label(0x4c0));
    cat(t, text(0x6851c8));
    put(iw, p, 1, t);
    t[0] = 0;
    cat(t, label(0x4b8));
    cat(t, text(S_COLON));
    cat(t, text(0x6851cc));
    cat_number(t, word(0x749ae8));
    cat(t, label(0x4c0));
    cat(t, text(0x6851d0));
    put(iw, p, 2, t);
    int n = 3;
    if (word(0x9bc054) == 0) {
        t[0] = 0;
        cat(t, text(0x6851d4));
        cat(t, text(S_COLON));
        cat(t, text(0x6851e0));
        cat_number(t, word(0x749c0c) - word(0x749ae8));
        cat(t, label(0x4c0));
        cat(t, text(0x6851e4));
        put(iw, p, 3, t);
        n = 4;
    }
    t[0] = 0;
    cat(t, label(0xefc));
    cat(t, text(S_COLON));
    cat(t, text(0x6851e8));
    cat_number(t, word(0x749c0c));
    cat(t, label(0x4c0));
    cat(t, text(0x6851ec));
    put(iw, p, n, t);
    return n + 1;
}

// The leader box of the faction being looked at: name, title, mood, treaty.
static int mode_leader(struct game_infowin *iw, int p, int f) {
    char t[0x100];
    int viewed = word(VIEWED_FACTION);
    t[0] = 0;
    cat_leader(t, viewed);
    put(iw, p, 0, t);
    t[0] = 0;
    cat_faction(t, viewed);
    put(iw, p, 1, t);
    t[0] = 0;
    int mood = get_mood(word(0x93fa74));
    game_say_special(t, string((const char *)(uintptr_t)*(int32_t *)text(MOOD_NAMES + 4 * mood)), viewed);
    put(iw, p, 2, t);
    t[0] = 0;
    uint32_t treaty = fac(f, 0x18 + 4 * viewed);
    if (treaty & 1) cat(t, label(0x34c));
    else if (treaty & 2) cat(t, label(0x350));
    else if (treaty & 4) cat(t, label(0x354));
    else if (treaty & 0x10) cat(t, label(0x358));
    put(iw, p, 3, t);
    return 4;
}

// The faction's own box: leader, title, integrity, ranking, council votes.
static int mode_faction(struct game_infowin *iw, int p, int f) {
    char t[0x100];
    t[0] = 0;
    cat_leader(t, f);
    put(iw, p, 0, t);
    t[0] = 0;
    cat_faction(t, f);
    put(iw, p, 1, t);
    t[0] = 0;
    cat(t, label(0x378));
    cat(t, text(S_COLON));
    cat(t, text(0x6851f0));
    int blemishes = fac(f, 0x124);
    blemishes = blemishes < 0 ? 0 : blemishes > 7 ? 7 : blemishes;
    game_say_special(t, string((const char *)(uintptr_t)*(int32_t *)text(0x946a30 + 4 * blemishes)), f);
    cat(t, text(0x6851f4));
    put(iw, p, 2, t);
    t[0] = 0;
    cat(t, label(0x37c));
    cat(t, text(S_COLON));
    cat(t, text(0x6851f8));
    int rank = fac(f, 4);
    rank = rank < 1 ? 1 : rank > 7 ? 7 : rank;
    game_say_special(t, string((const char *)(uintptr_t)*(int32_t *)text(0x94c590 - 8 * rank)), f);
    cat(t, text(0x6851fc));
    put(iw, p, 3, t);
    t[0] = 0;
    cat(t, label(0xf00));
    cat(t, text(S_COLON));
    cat(t, text(0x685200));
    cat_number(t, council_votes(f));
    cat(t, text(0x685204));
    put(iw, p, 4, t);
    return 5;
}

// The faction in the game that has the greatest of a figure (at `off` in a faction's record), the player's if none
// has any: a later faction wins a tie.
static int leader_of(int f, int off) {
    int best = 0, who = f;
    int mask = *(uint8_t *)text(ACTIVE_MASK_BYTE);
    for (int g = 1; g < 8; g++)
        if ((mask & (1 << g)) && fac(g, off) >= best) best = fac(g, off), who = g;
    return who;
}

// The records box: who leads in each of five figures.
static int mode_records(struct game_infowin *iw, int p, int f) {
    char t[0x100];
    static const struct {
        int label, off, open, close;
    } records[4] = {{0x41c, 0x35c, 0x685208, 0x68520c},
                    {0x420, 0x1060, 0x685210, 0x685214},
                    {0x424, 0x1068, 0x685218, 0x68521c},
                    {0x4f0, 0x220, 0x685220, 0x685224}};
    for (int k = 0; k < 4; k++) {
        int who = leader_of(f, records[k].off);
        t[0] = 0;
        cat(t, label(records[k].label));
        cat(t, text(S_COLON));
        cat(t, text(records[k].open));
        cat_faction(t, who);
        cat(t, text(records[k].close));
        put(iw, p, k, t);
    }
    // The fifth: the most units lost (the sum of the first sixty-two prototypes' losses in the unit-loss table).
    int best = 0, who = f;
    int mask = *(uint8_t *)text(ACTIVE_MASK_BYTE);
    for (int g = 1; g < 8; g++) {
        if (!(mask & (1 << g))) continue;
        int sum = 0;
        for (int u = 1; u < 0x40; u++)
            if (u != 0x3f) sum += *(uint16_t *)text(0x96de5c + 2 * (0x1066 * g + u));
        if (sum >= best) best = sum, who = g;
    }
    t[0] = 0;
    cat(t, label(0x97c));
    cat(t, text(S_COLON));
    cat(t, text(0x685228));
    cat_faction(t, who);
    cat(t, text(0x68522c));
    put(iw, p, 4, t);
    int count = 5;
    if (word(RANK_COUNT) > 0) {
        int top = word(RANK_ORDER);
        t[0] = 0;
        cat(t, label(0x428));
        cat(t, text(S_COLON));
        cat(t, text(0x685230));
        cat_faction(t, top);
        cat(t, text(0x685234));
        put(iw, p, 5, t);
        count = 6;
    }
    return count;
}

// The title of the two chart boxes: "ranking" in capitals while the game has not yet counted, else the plain label.
static void cat_title(char *t) {
    if (infowin_title_upper(word(0x94b4c0), word(0x94b4c4), word(0x94b4c8))) {
        cat(t, label(0xe0c));
        (*game_char_upper)(t);
    } else {
        cat(t, label(0x6e0));
    }
}

static int32_t *buffer_full(struct game_buffer *b) { return b->full; }

// The bars box: each faction's strength as a bar, with the faction's initial over it.
static int mode_bars(struct game_infowin *iw, struct game_buffer *buf, int p, struct ui_rect *r, int line_h) {
    char t[0x100];
    buffer_set_clip(buf, (int32_t *)r);
    t[0] = 0;
    cat_title(t);
    put(iw, p, 0, t);
    int span = r->bottom - r->top - line_h - 4;
    int bottom = r->bottom - 2;
    rankings(0);
    int most = 1;
    for (int i = 0; i < 7; i++)
        if (word(RANKING_VALUES + 4 * i) > most) most = word(RANKING_VALUES + 4 * i);
    int first, width;
    infowin_bar_run(r->left, r->right, &first, &width);
    r->left = first;
    r->right = first + width;
    for (int i = 0; i < 7; i++) {
        int f = i + 1;
        int h = infowin_bar_height(word(RANKING_VALUES + 4 * i), span, most);
        r->top = bottom - h;
        r->bottom = bottom;
        buffer_box_rect(buf, (int32_t *)r, word(0x68fa14 + 4 * i), word(0x68fa14 + 4 * i));
        r->left++, r->top++, r->right--, r->bottom--;
        game_graphic_win_fill_rect(main_win(), (int32_t *)r, word(0x68f9f4 + 4 * i));
        r->left--, r->top--, r->right++, r->bottom++;
        t[0] = 0;
        set_grammar(fdef_word(f, 0x2fc), fdef_word(f, 0x300));
        cat(t, fdef(f, 0x2e4));
        t[1] = 0;
        buffer_set_font(buf, &iw->font, 0, 0, 0);
        buffer_set_text_color(buf, word(0x68fa34 + 4 * i), word(0x68fa54 + 4 * i), 1, 1);
        buffer_write_cent_l_7(buf, t, (int32_t *)r, (int)strlen(t));
        r->left += width;
        r->right += width;
    }
    buffer_set_clip(buf, buffer_full(buf));
    return 1;
}

// The chart box: each faction's strength over the turns as a line, and the names of the factions below in order.
static int mode_chart(struct game_infowin *iw, struct game_buffer *buf, int p, struct ui_rect *r, int line_h) {
    char t[0x100];
    buffer_set_clip(buf, (int32_t *)r);
    t[0] = 0;
    cat_title(t);
    put(iw, p, 0, t);
    int height = r->bottom - r->top - line_h - 4;
    rankings(0);
    int most = 1;
    for (int i = 0; i < 7; i++)
        if (word(RANKING_VALUES + 4 * i) > most) most = word(RANKING_VALUES + 4 * i);
    int width = r->right - r->left;
    int turns = word(TURN);
    int step = infowin_chart_step(turns, width);
    int scale = infowin_chart_scale(height, most);
    int x = r->left;
    for (int i = 0; i < 7; i++) {
        int prev_y = r->bottom - 1, prev_x = r->left;
        x = r->left;
        int counter = 1;
        if (turns >= 1) {
            const uint8_t *ptr = (const uint8_t *)text(HISTORY + 2 * i);
            do {
                int y = r->bottom - *(const int16_t *)ptr / scale - 1;
                if (counter > 1) {
                    int color = strcmp(fdef(i + 1, 0x1c), text(0x685238)) ? word(0x68f9f4 + 4 * i) : word(0x68fa14 + 4 * i);
                    buffer_line(buf, prev_x, prev_y, x, y, color);
                }
                prev_y = y;
                prev_x = x;
                x++;
                ptr += step << 4;
                counter += step;
            } while (counter <= turns);
        }
    }
    for (int k = 0; k < 7; k++) {
        int f = word(RANK_ORDER - 4 * k);
        t[0] = 0;
        set_grammar(fdef_word(f, 0x2fc), fdef_word(f, 0x300));
        cat(t, fdef(f, 0x2e4));
        if (strlen(t)) {
            do {
                int room = r->right - r->left - buffer_text_width(buf, t) - 2;
                if (room >= x) break;
                t[strlen(t) - 1] = 0;
            } while (strlen(t));
        }
        put(iw, p, 1 + k, t);
    }
    buffer_set_clip(buf, buffer_full(buf));
    return 8;
}

// The planet box: the planet's state and the sea level.
static int mode_planet(struct game_infowin *iw, int p) {
    char t[0x100];
    t[0] = 0;
    cat(t, label(0x6ac));
    put(iw, p, 0, t);
    t[0] = 0;
    cat(t, label(0x6a8));
    cat(t, text(S_COLON));
    cat(t, text(0x685244));
    cat(t, text(0x94a2bc));
    cat(t, text(0x685248));
    put(iw, p, 1, t);
    t[0] = 0;
    cat(t, label(0x6a4));
    cat(t, text(S_COLON));
    int state = word(0x945ae0);
    int named = infowin_planet_name(state);
    if (named >= 0 && named <= 5) cat(t, label(0xebc + 4 * named));
    else if (named >= 6) cat(t, string_at(named == 6 ? 0x94adf4 : named == 7 ? 0x94ae14 : named == 8 ? 0x94adfc : 0x94ae34));
    else cat(t, string_at(0x691878 + infowin_planet_table_offset(state)));
    put(iw, p, 2, t);
    t[0] = 0;
    cat(t, label(0x684));
    cat(t, text(S_COLON));
    int steps = word(0x945ae4);
    if (steps == 1) cat(t, label(0x688));
    else cat_number(t, steps ? steps - 1 : 0);
    put(iw, p, 3, t);
    t[0] = 0;
    cat(t, label(0xd7c));
    cat(t, text(S_COLON));
    cat_number(t, word(0x949870) / 2);
    cat(t, text(0x68524c));
    cat_number(t, word(0x949874));
    cat(t, text(S_SPACE));
    cat(t, label((word(0x94988c) & 1) ? 0xd80 : 0xd84));
    put(iw, p, 4, t);
    t[0] = 0;
    cat(t, label(0x84));
    cat(t, text(S_COLON));
    cat(t, text(0x685250));
    cat(t, string((const char *)(uintptr_t)(*game_labels)[0x14 + word(0x94a2b4)]));
    cat(t, text(0x685254));
    put(iw, p, 5, t);
    return 6;
}

// Puts the text of the shown box's lines together, and works out each box's length. (The original does the same for
// each box from the shown one up to the one after it, which is the shown box alone.) The frame of the box is
// drawn first, and the clock's scroll is not touched.
static void UI_THIS infowin_setup_text(struct game_infowin *iw) {
    struct game_buffer *buf = main_buffer();
    int faction = word(MAP_OWNER);
    int line_h = infowin_line_height(iw->font.first, iw->font.ascent, iw->font.height);
    for (int pg = 0; pg < PAGES; pg++) {
        iw->lines[pg] = 0;
        int y = iw->box[pg].top + 5;
        for (int k = 0; k < LINES; k++) {
            int idx = pg * LINES + k;
            iw->text[pg][k][0] = 0;
            iw->x[idx] = iw->box[pg].left + 5;
            iw->y[idx] = y;
            iw->color[idx] = 0;
            iw->color2[idx] = 0;
            y += line_h;
        }
    }
    int p = iw->page;
    buffer_box_sprite(buf, (int32_t *)&iw->box[p], game_at(BOX_FRAME));
    struct ui_rect r = {iw->box[p].left + 3, iw->box[p].top + 3, iw->box[p].right - 3, iw->box[p].bottom - 3};
    int count = 0;
    switch (iw->mode[p] % 8) {
    case 0:
        count = mode_calendar(iw, p, faction);
        break;
    case 1:
        count = mode_economy(iw, p, faction);
        break;
    case 2:
        count = mode_leader(iw, p, faction);
        break;
    case 3:
        count = mode_faction(iw, p, faction);
        break;
    case 4:
        count = mode_chart(iw, buf, p, &r, line_h);
        break;
    case 5:
        count = mode_bars(iw, buf, p, &r, line_h);
        break;
    case 6:
        count = mode_records(iw, p, faction);
        break;
    case 7:
        count = mode_planet(iw, p);
        break;
    default:
        break;
    }
    iw->lines[p] = count;
    iw->length[p] = 0;
    for (int j = 0; j < count; j++) iw->length[p] += (int32_t)strlen(iw->text[p][j]);
}

// ---- Drawing -------------------------------------------------------------------------------------------------

// What the original's draw leaves in two places of its stack frame and then reads. When a box's scroll is zero at the start of
// a line it draws "the character before its copy" at "the end of the last part drawn": the byte before its copy is the top byte
// of the EBX its caller had (a register that held a pointer in the caller, so a character, usually backspace, drawn as an empty box),
// and the x is the word of the frame that held where the last part ended, which the next call at the same depth finds
// still there. `stale_byte` is that byte (the top of EBX when the clock's tick, a click or the reset entered; the other callers
// hold zero in EBX when they draw) and `stale_x` the last x.
static int stale_x;

// Writes the boxes' text, a letter further each time: page 0 or 1, or -1 for both. Each line is shown while
// characters of the box's scroll are left: whole when the scroll is past it, else its first scroll - 1 characters
// and the one at the front in the flash colour. (The original carries the line number from the first box into the
// second.)
static void draw_text_b(struct game_infowin *iw, int arg, uint8_t stale_byte) {
    struct game_buffer *buf = main_buffer();
    buffer_set_font(buf, &iw->font, 0, 0, 0);
    int first_page = arg == -1 ? 0 : arg, end_page = arg == -1 ? 2 : arg + 1;
    int i = 0;
    for (int p = first_page; p < end_page; p++) {
        int base = LINES * p;
        buffer_set_clip(buf, (int32_t *)&iw->box[p]);
        int left = iw->box[p].left + 3, right = iw->box[p].right - 3;
        int s = iw->scroll[p];
        int count = iw->lines[p];
        while (i < count && s >= 0) {
            int idx = base + i;
            char *line = (char *)iw->text + idx * LINE_LEN;
            int len = (int)strlen(line);
            int ranked = iw->mode[p] == 4 && i > 0;
            int x = iw->x[idx], y = iw->y[idx];
            if (len != 0) {
                if (ranked) {
                    int who = word(RANK_COUNT - 4 * i);
                    buffer_set_text_color(buf, word(FACTION_COLORS + 4 * who), word(0x68fa10 + 4 * who), 1, 1);
                } else if (iw->color[idx] != 0) {
                    buffer_set_text_color(buf, iw->flash ? iw->color2[idx] : iw->color[idx], -1, 1, 1);
                } else if (iw->flash) {
                    buffer_set_text_color(buf, word(TEXT_COLORS + 8), word(TEXT_COLORS + 12), 1, 1);
                    buffer_set_text_color2(buf, word(TEXT_COLORS + 0x18), word(TEXT_COLORS + 0x1c), 1, 1);
                } else {
                    buffer_set_text_color(buf, word(TEXT_COLORS), word(TEXT_COLORS + 4), 1, 1);
                    buffer_set_text_color2(buf, word(TEXT_COLORS + 0x10), word(TEXT_COLORS + 0x14), 1, 1);
                }
                enum infowin_line action = infowin_line_action(s, len);
                switch (action) {
                case INFOWIN_LINE_FULL:
                    if (ranked) buffer_write_right_l_4(buf, line, 2, y, (right - left) - 4, len);
                    else buffer_write_l_3(buf, line, x, y, len);
                    break;
                default: {
                    // Part way: the first s - 1 characters in the line's colours, then the one at the front. A scroll
                    // of nothing leaves nothing (the byte before the copy is a zero, as in the original's frame).
                    char scratch[1 + 0x88];
                    char *tmp = scratch + 1;
                    scratch[0] = (char)stale_byte;
                    int next_x = stale_x;
                    if (action == INFOWIN_LINE_PARTIAL) {
                        strcpy(tmp, line);
                        tmp[s - 1] = 0;
                        if (ranked) {
                            int where = (right - left) - buffer_text_width(buf, line) - 2;
                            next_x = buffer_write_l_3(buf, tmp, where, y, (int)strlen(tmp));
                        } else {
                            next_x = buffer_write_l_3(buf, tmp, x, y, (int)strlen(tmp));
                        }
                    }
                    strcpy(tmp, line);
                    tmp[s] = 0;
                    if (!ranked) {
                        if (iw->color[idx] != 0) {
                            buffer_set_text_color(buf, iw->color2[idx], -1, 1, 1);
                        } else {
                            buffer_set_text_color(buf, word(TEXT_COLORS + 8), word(TEXT_COLORS + 4), 1, 1);
                            buffer_set_text_color2(buf, word(TEXT_COLORS + 0x18), word(TEXT_COLORS + 0x1c), 1, 1);
                        }
                    }
                    stale_x = next_x;
                    buffer_write_l_3(buf, tmp + s - 1, next_x, y, (int)strlen(tmp + s - 1));
                    for (int k = 0; k < 4; k++) *(int32_t *)text(DIRTY + 4 * k) = 0;
                    break;
                }
                }
            }
            s -= len;
            i++;
        }
    }
    buffer_set_clip(buf, buffer_full(buf));
    graphic_win_update_2(main_win(), (int32_t *)(arg == -1 ? &iw->bounds : &iw->box[arg]), 0);
    game_tutwin_draw_arrow(text(TUT_WIN));
}
static void UI_THIS infowin_draw_text(struct game_infowin *iw, int arg) { draw_text_b(iw, arg, 0); }

// The top byte of EBX as the clock's tick, a click or the reset was entered (see draw_text_b), kept by the entry stubs below.
uint8_t infowin_entry_byte;

// ---- Changing what a box shows ------------------------------------------------------------------------------

// The frame drawn again and the text put together and drawn, after the box's mode changed. The scrolls are cleared:
// the typing starts over.
static void redraw_box(struct game_infowin *iw) {
    buffer_box_sprite(main_buffer(), (int32_t *)&iw->box[iw->page], game_at(BOX_FRAME));
    iw->scroll[0] = 0;
    iw->scroll[1] = 0;
    infowin_setup_text(iw);
    draw_text_b(iw, iw->page, 0);
}

static void UI_THIS infowin_set_state(struct game_infowin *iw, int mode, int flag) {
    int page = iw->page;
    if (mode < 0) mode = 0;
    iw->mode[page] = mode;
    while (!mode_valid(mode)) {
        mode = infowin_next_mode(iw->mode[page]);
        iw->mode[page] = mode;
    }
    iw->flash = 0;
    iw->scroll[0] = flag ? 0 : iw->length[0] + 1;
    iw->scroll[1] = flag ? 0 : iw->length[1] + 1;
    iw->hold = 0;
    redraw_box(iw);
}

static void UI_THIS infowin_timer_change(struct game_infowin *iw, int unused) {
    (void)unused;
    if (word(BUSY_TABLE + 4 * word(BUSY_INDEX)) != 0) return;
    int old = iw->page;
    iw->flash = 0;
    iw->page = word(SCREEN_WIDTH) == 0x320 ? 0 : (old - 1) & 1;
    int first = 1;
    for (;;) {
        if (iw->advance_from >= 0) {
            iw->mode[iw->page] = infowin_next_mode(iw->advance_from);
        } else {
            int from = first ? old : iw->page;
            iw->mode[iw->page] = infowin_next_mode(iw->mode[from]);
        }
        iw->advance_from = -1;
        first = 0;
        if (mode_valid(iw->mode[iw->page])) break;
    }
    for (int k = 0; k < 4; k++) *(int32_t *)text(DIRTY + 4 * k) = 0;
    iw->scroll[0] = 0;
    iw->scroll[1] = 0;
    iw->hold = 0;
    redraw_box(iw);
}

// One tick of the clock: the typing goes a character on, and a box typed to its end and beyond moves on to the next mode
// (or holds, flashing, while the mouse is over the panel or it is held).
void UI_THIS infowin_timer_proc_c(struct game_infowin *iw, int arg) {
    if (iw->suspended != 0) return;
    if (word(BUSY_TABLE + 4 * word(BUSY_INDEX)) != 0) return;
    if (win_is_visible(game_at(POPUP_WINDOW))) return;
    int page = iw->page;
    iw->scroll[page]++;
    switch (infowin_tick_action(iw->scroll[page], iw->length[page])) {
    case INFOWIN_TICK_HOLD: {
        int32_t x, y;
        win_get_mouse_pos(main_win(), &x, &y);
        if (!in_box_2(x, y, (int32_t *)iw) && iw->hold == 0 && iw->flash == 0) {
            infowin_timer_change(iw, arg);
            return;
        }
        iw->scroll[page] = iw->length[page] + 0x28;
        iw->flash = iw->flash == 0;
        draw_text_b(iw, -1, infowin_entry_byte);
        return;
    }
    case INFOWIN_TICK_DRAW:
        draw_text_b(iw, page, infowin_entry_byte);
        return;
    default:
        return;
    }
}

void UI_CDECL infowin_timer_daemon_c(int arg) {
    if (word(TIMER_BLOCK) == 0) infowin_timer_proc_c(game_at(INFOWIN_BASE), arg);
}

void UI_THIS infowin_reset_c(struct game_infowin *iw) {
    if (iw->hold != 0) {
        iw->hold = 0;
        infowin_timer_proc_c(iw, 1);
    }
}

// ---- The mouse ------------------------------------------------------------------------------------------------

// A left click: types out what is left of the first unfinished box at once, else moves on to the next mode.
void UI_THIS infowin_on_left_click_c(struct game_infowin *iw, int x, int y) {
    (void)x, (void)y;
    *(int32_t *)text(LEFT_CLICK_FLAG) = 0;
    iw->hold = 0;
    game_status_win_reset(game_at(STATUS_WIN));
    int i;
    for (i = 0; i < PAGES; i++)
        if (iw->scroll[i] < iw->length[i] + 1) break;
    if (i == PAGES) {
        infowin_timer_change(iw, 1);
        iw->scroll[0] = iw->length[0] + 1;
        iw->scroll[1] = iw->length[1] + 1;
        draw_text_b(iw, -1, infowin_entry_byte);
        return;
    }
    iw->scroll[i] = iw->length[i] + 1;
    draw_text_b(iw, i, infowin_entry_byte);
    iw->advance_from = 0;
}

// A right click: a menu of the modes that can be shown now, and help; the choice becomes the shown box's mode.
static void UI_THIS infowin_right_menu(struct game_infowin *iw, int x, int y) {
    static const int16_t labels[8] = {0x92c, 0x930, 0x934, 0x938, 0x948, 0x94c, 0x950, 0x954};
    struct game_pulldown menu;
    pulldown_ctor(&menu);
    win_client_to_screen_2(main_win(), &x, &y);
    pulldown_init(&menu.win, 0);
    for (int m = 0; m < 8; m++)
        if (mode_valid(m)) game_pulldown_add_item(&menu, m, label(labels[m]));
    pulldown_add_separator(&menu, MENU_SEPARATOR_ID);
    game_pulldown_add_item(&menu, MENU_HELP_ID, label(0x8fc));
    game_pulldown_show(&menu, x, y, game_at(0x539920));
    if (!(word(GAME_FLAGS) & 0x800)) {
        int selected = pulldown_get_selected(&menu);
        if (selected < 0x40) {
            int id = ((int32_t *)&menu)[(0xa20 + selected * 0x14) / 4];
            if (id >= 0 && id <= 0x10 && mode_valid(id)) {
                int page = iw->page;
                iw->mode[page] = id;
                while (!mode_valid(iw->mode[page])) iw->mode[page] = infowin_next_mode(iw->mode[page]);
                iw->flash = 0;
                iw->hold = 0;
                redraw_box(iw);
                pulldown_dtor(&menu);
                return;
            }
            if (id == MENU_HELP_ID) game_open_help();
        }
    }
    pulldown_dtor(&menu);
}

static void UI_THIS infowin_on_right_click(struct game_infowin *iw, int x, int y) { infowin_right_menu(iw, x, y); }

// The entries of the functions whose draw reads what the caller left (see draw_text_b): they keep the top byte of EBX, which the
// original's own code passes on untouched, and go on to the function. (The daemon is cdecl, the rest thiscall.)
#define ENTRY(name, to)                                                  \
    __asm__(".text\n"                                                    \
            ".globl " #name "\n" #name ":\n"                              \
            "    movl %ebx, %eax\n"                                        \
            "    shrl $24, %eax\n"                                         \
            "    movb %al, infowin_entry_byte\n"                           \
            "    jmp " #to "\n")
ENTRY(infowin_timer_proc, infowin_timer_proc_c);
ENTRY(infowin_timer_daemon, infowin_timer_daemon_c);
ENTRY(infowin_reset, infowin_reset_c);
ENTRY(infowin_on_left_click, infowin_on_left_click_c);
void UI_THIS infowin_timer_proc(struct game_infowin *iw, int arg);
void UI_CDECL infowin_timer_daemon(int arg);
void UI_THIS infowin_reset(struct game_infowin *iw);
void UI_THIS infowin_on_left_click(struct game_infowin *iw, int x, int y);

// ---- Setting up and taking down ------------------------------------------------------------------------------

// The rectangle the game keeps for the panel's strip (left, top, right, bottom) and the bit in its flags that says it is
// set: wider and lower on a wide screen. Both the constructor-time set-up and a function nothing calls make it.
static void register_strip(void) {
    uint32_t *flags = game_at(STATUS_FLAGS);
    int wide = word(STATUS_WIDTH) >= 1000;
    int right = wide ? 0xfa : 0xc8, bottom = wide ? 0xd4 : 0xb4;
    char *base = game_at(STATUS_BASE);
    uint8_t *vbptr = *(uint8_t **)base;  // the object's table of offsets to its virtual base
    int32_t vbase = *(int32_t *)(vbptr + 4);
    int32_t field = *(int32_t *)((char *)game_at(STATUS_VBASE_FIELD) + vbase);
    int32_t *rect = game_at(STATUS_SIDE);
    rect[0] = 0;
    rect[2] = right;
    rect[1] = -field;
    rect[3] = -field + bottom;
    *flags |= 4;
}

// What USER32's UnionRect does: the smallest rectangle holding both (an empty one, with no area, counts for nothing).
static void union_rect(struct ui_rect *dst, const struct ui_rect *a, const struct ui_rect *b) {
    int a_empty = a->right <= a->left || a->bottom <= a->top, b_empty = b->right <= b->left || b->bottom <= b->top;
    if (a_empty && b_empty) *dst = (struct ui_rect){0, 0, 0, 0};
    else if (a_empty) *dst = *b;
    else if (b_empty) *dst = *a;
    else
        *dst = (struct ui_rect){a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
                                a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom};
}

static void UI_THIS infowin_init(struct game_infowin *iw) {
    iw->mode[0] = iw->mode[1] = iw->advance_from = -1;
    iw->minus_one = -1;
    iw->suspended = 0;
    iw->flash = 0;
    iw->hold = 0;
    iw->one = 1;
    iw->page = 1;
    if (!(*(uint32_t *)text(STATUS_FLAGS) & 4)) register_strip();
    int small = word(SCREEN_WIDTH) == 0x320;
    font_init(&iw->font, (const char *)(uintptr_t)word(FONT_FACE), small ? 10 : 12, 0);
    iw->font.first = 4;
    infowin_layout(&iw->box[0], &iw->box[1], (const struct ui_rect *)text(PANEL_AREA), word(SCREEN_WIDTH));
    union_rect(&iw->bounds, &iw->box[0], &iw->box[1]);
    time_start(&iw->timer, game_at(0x459260), (void *)10, 0x32, 0x32);
}

static void UI_CDECL infowin_strip(void) { register_strip(); }

static void *UI_THIS infowin_ctor(struct game_infowin *iw) {
    time_ctor(&iw->timer);
    font_ctor(&iw->font);
    push_button_ctor(&iw->button);
    return iw;
}

static void UI_THIS infowin_dtor(struct game_infowin *iw) {
    push_button_dtor(&iw->button);
    font_dtor(&iw->font);
    time_dtor(&iw->timer);
}

// Three fonts made and unmade on the stack, which nothing calls.
static void __attribute__((stdcall)) infowin_unused_fonts(int a, int b, int c, int d, int e) {
    (void)a, (void)b, (void)c, (void)d, (void)e;
    struct font f[3];
    for (int i = 0; i < 3; i++) font_ctor(&f[i]);
    for (int i = 2; i >= 0; i--) font_dtor(&f[i]);
}

// Functions that do nothing, or that nothing calls: the object's change and redraw handlers (a stub each), the
// text and clip reset that clears the scrolls and draws (called by nothing), and the strip registration.
static void UI_CDECL infowin_stub_ret(void) {}
static void UI_THIS infowin_stub_change(struct game_infowin *iw, int a) { (void)iw, (void)a; }
static void UI_THIS infowin_stub_redraw(struct game_infowin *iw) { (void)iw; }
static void UI_THIS infowin_stub_unk3(struct game_infowin *iw, int a, int b) { (void)iw, (void)a, (void)b; }

static void UI_THIS infowin_unused_rescroll(struct game_infowin *iw) {
    buffer_box_sprite(main_buffer(), (int32_t *)&iw->box[iw->page], game_at(BOX_FRAME));
    iw->scroll[0] = 0;
    iw->scroll[1] = 0;
    infowin_setup_text(iw);
    infowin_draw_text(iw, iw->page);
}

// ---- The hook table ------------------------------------------------------------------------------------------

static const struct hook hooks[] = {
    UI_HOOK(0x4562f0, infowin_stub_ret, 0xc3, 0x90, 0x90, 0x90, 0x90),
    UI_HOOK(0x456300, infowin_setup_text, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x04, 0x05, 0x00, 0x00),
    UI_HOOK(0x458420, infowin_draw_text, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00),
    UI_HOOK(0x4588b0, infowin_unused_rescroll, 0x56, 0x8b, 0xf1, 0x68, 0xf8, 0xd7, 0x78, 0x00),
    UI_HOOK(0x458900, infowin_stub_change, 0xc2, 0x04, 0x00, 0x90, 0x90),
    UI_HOOK(0x458910, infowin_stub_redraw, 0xc3, 0x90, 0x90, 0x90, 0x90),
    UI_HOOK(0x458920, infowin_stub_unk3, 0xc2, 0x08, 0x00, 0x90, 0x90),
    UI_HOOK(0x458930, infowin_on_left_click, 0x56, 0x8b, 0xf1, 0x57, 0xc7, 0x05, 0x50, 0x81, 0x6e, 0x00),
    UI_HOOK(0x4589c0, infowin_right_menu, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x8b, 0x52, 0x65, 0x00),
    UI_HOOK(0x458d40, infowin_on_right_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x8b, 0x55, 0x08),
    UI_HOOK(0x458d60, infowin_strip, 0x81, 0x3d, 0x5c, 0x9e, 0x93, 0x00, 0xe8, 0x03, 0x00, 0x00),
    UI_HOOK(0x458dc0, infowin_state_valid, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x57),
    UI_HOOK(0x458f10, infowin_set_state, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x53, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x458ff0, infowin_timer_change, 0x55, 0x8b, 0xec, 0x51, 0xa1, 0x2c, 0x39, 0x7d, 0x00),
    UI_HOOK(0x459150, infowin_timer_proc, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x459260, infowin_timer_daemon, 0x55, 0x8b, 0xec, 0xa1, 0x1c, 0xf2, 0x68, 0x00),
    UI_HOOK(0x459280, infowin_reset, 0x8b, 0x81, 0xa8, 0x00, 0x00, 0x00, 0x85, 0xc0),
    UI_HOOK(0x4592a0, infowin_init, 0x53, 0x56, 0x8b, 0xf1, 0x83, 0xc8, 0xff, 0x33, 0xdb),
    UI_HOOK(0x459420, infowin_unused_fonts, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xb0, 0x52, 0x65, 0x00),
    UI_HOOK(0x4594a0, infowin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x56, 0x52, 0x65, 0x00),
    UI_HOOK(0x459500, infowin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x76, 0x52, 0x65, 0x00),
};

struct hook_module infowin_module = {"infowin", hooks, UI_HOOK_COUNT(hooks), NULL, NULL, -1};
