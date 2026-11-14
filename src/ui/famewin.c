// The Hall of Fame window, the game's FameWin, and hall_of_fame(), the function that opens it. The window is one global
// object (0x74daf8) built by a static initializer and destroyed by an atexit handler; hall_of_fame() is the only code
// outside the class that touches it (nothing else reads its address or fields), and is called by the game's end
// (end_of_game, with 1) and nowhere else.
//
// The window shows the seven factions as portraits along the top of the picture; under the chosen faction's portrait the
// five best games of that faction are listed (the leader's title and name, the faction's name, the rating and the year),
// and at the right the chosen game's figures: the records it holds, how it ended, and its score, credits, rating, difficulty
// and how the planet fared. Six more buttons, one for each record, jump to the game that holds it. A click on a game in
// the list shows it; Return, Escape, any letter or digit and the OK button close the window.
//
// What hall_of_fame() does around the window is the Hall's own logic and is rebuilt with it: it reads the fame file (0x1324
// bytes, seven rows of five entries; any trouble reading it leaves the list empty), builds the window, and, at the end of a
// game, adds the game that has just ended (FameWin_try_to_add_me: where it ranks among its faction's five, which records
// it takes from the others, a picture of the screen kept with it, FameWin_demote moving the lower ones, pictures too, down
// a place). It opens the window as a modal one, and writes the file back. That logic is not used anywhere else, so it
// stays here, in the same order as the original: the file is the original's byte for byte, and the pure parts (where a game
// ranks, the records it takes, the flags it ends with) are small functions of famewin.h with a unit test.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=famewin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. Static initializers run after hooks_apply, so the constructor
// and destructor are ours too. hall_of_fame() was a function the shadow mode held while a check protects the game's state
// (quiet.c); it is held here (shadow_hall_of_fame) so the hook table has one entry for the address.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the inlined copies of the close
// sequence are one function; the original's "is it a window" test of the Buffer's address (a null object is never given) is
// left out; the itoa, strcat and strncpy calls are the C library's.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "famewin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufimg.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/score.h"
#include "../engine/sprite.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

int famewin_place(const struct fame_entry *row, int rating) {
    int pos = FAME_NO_PLACE;
    for (int i = FAME_RANKS - 1; i >= 0; i--)
        if (rating >= row[i].rating || !(row[i].flags & FAME_IN_USE)) pos = i;
    return pos;
}

// The conditions are the game's state bits (0x2000 conquered, 0x400000 and 0x200000 the other two wins) and the rules (0x400
// ironman); a scenario (0x10) only counts when none of them holds. A game won by transcendence is told by the faction's
// own seventh score part, which the caller reads.
uint8_t famewin_flags(uint32_t state, uint32_t rules, int transcended) {
    uint8_t flags = FAME_IN_USE;
    if (rules & 0x400) flags |= FAME_IRONMAN;
    if (state & 0x2000) flags |= FAME_CONQUEST;
    else if (state & 0x400000) flags |= FAME_ECONOMIC;
    else if (state & 0x200000) flags |= FAME_OTHER_VICTORY;
    else if (transcended) flags |= FAME_TRANSCENDENCE;
    else if (state & 0x10) flags |= FAME_SCENARIO;
    return flags;
}

void famewin_award(struct fame_entry *table, struct fame_entry *n, int own_row, int pos) {
    int count = 0;
    for (int row = 0; row < FAME_SETS; row++)
        for (int k = 0; k < FAME_RANKS; k++) {
            if (row == own_row && k == pos) continue;
            struct fame_entry *e = &table[row * FAME_RANKS + k];
            if (!(e->flags & FAME_IN_USE)) continue;
            count++;
            if ((e->records & FAME_RECORD_RATING) && n->rating >= e->rating) n->records |= FAME_RECORD_RATING, e->records &= ~FAME_RECORD_RATING;
            if ((e->records & FAME_RECORD_C) && n->part_c >= e->part_c) n->records |= FAME_RECORD_C, e->records &= ~FAME_RECORD_C;
            if ((e->records & FAME_RECORD_CREDITS) && n->credits >= e->credits)
                n->records |= FAME_RECORD_CREDITS, e->records &= ~FAME_RECORD_CREDITS;
            if ((e->records & FAME_RECORD_B) && n->part_b >= e->part_b) n->records |= FAME_RECORD_B, e->records &= ~FAME_RECORD_B;
        }
    if (!count) n->records = 0xf;

    // The date records: the earliest win of a kind. A game takes it from the others of that kind that are no earlier, and
    // holds it if none of that kind is in the list.
    static const struct {
        uint8_t kind, record;
    } dates[] = {{FAME_CONQUEST, FAME_RECORD_CONQUEST}, {FAME_TRANSCENDENCE, FAME_RECORD_TRANSCENDENCE}};
    for (int d = 0; d < 2; d++) {
        count = 0;
        for (int row = 0; row < FAME_SETS; row++)
            for (int k = 0; k < FAME_RANKS; k++) {
                if (row == own_row && k == pos) continue;
                struct fame_entry *e = &table[row * FAME_RANKS + k];
                if (!(e->flags & FAME_IN_USE) || !(e->flags & dates[d].kind)) continue;
                count++;
                if (!(e->records & dates[d].record) || !(n->flags & dates[d].kind) || n->year > e->year) continue;
                n->records |= dates[d].record;
                e->records &= ~dates[d].record;
            }
        if (!count && (n->flags & dates[d].kind)) n->records |= dates[d].record;
    }
}

void famewin_copy(struct fame_entry *to, const struct fame_entry *from) {
    to->flags = from->flags;
    to->records = from->records;
    to->year = from->year;
    to->score = from->score;
    to->credits = from->credits;
    to->rating = from->rating;
    to->part_a = from->part_a;
    to->part_b = from->part_b;
    to->part_c = from->part_c;
    to->difficulty = from->difficulty;
    to->planet = from->planet;
    strncpy(to->title, from->title, FAME_NAME_LEN);
    strncpy(to->name, from->name, FAME_NAME_LEN);
    strncpy(to->noun, from->noun, FAME_NAME_LEN);
    for (int i = 0; i < 8; i++) to->tail[i] = from->tail[i];
}

int famewin_find_record(const struct fame_entry *table, int bit, int *row, int *pos) {
    for (int s = 0; s < FAME_SETS; s++)
        for (int k = 0; k < FAME_RANKS; k++) {
            const struct fame_entry *e = &table[s * FAME_RANKS + k];
            if ((e->flags & FAME_IN_USE) && (e->records & (1 << bit))) {
                *row = s, *pos = k;
                return 1;
            }
        }
    return 0;
}

int famewin_entry_index(int sel, int rank) { return rank + 5 * sel - 5; }

void famewin_rects(int32_t rects[6][4], int dx, int dy) {
    static const int32_t base[6][4] = {{0x1e, 0x10e, 0x123, 0x147}, {0x12f, 0x10d, 0x21d, 0x183}, {0x228, 0x10d, 0x2fa, 0x21f},
                                       {0x15, 0x7b, 0x303, 0xed},   {0x4c, 0x27, 0x2d4, 0x77},   {0x128, 0x187, 0x224, 0x248}};
    for (int i = 0; i < 6; i++) {
        rects[i][0] = base[i][0] + dx;
        rects[i][1] = base[i][1] + dy;
        rects[i][2] = base[i][2] + dx;
        rects[i][3] = base[i][3] + dy;
    }
}

int famewin_cell_width(int left, int right, int count) { return (right - left) / count; }

int famewin_cell_inset(int cell, int size) { return (cell - size) / 2; }

int famewin_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

void famewin_position(struct famewin_position *p, int big, int screen_w, int screen_h) {
    if (!big) {
        *p = (struct famewin_position){0, 0, 0x320, 0x258, 0, 0};
        return;
    }
    *p = (struct famewin_position){(screen_w - 0x400) / 2, screen_h - 0x300, 0x400, 0x300, 0x77, 0x50};
}

void famewin_banner(int *left, int *width, int offset_x, int text_width) {
    *left = offset_x - text_width / 2 + 0x186;
    *width = text_width + 0x14;
}

void famewin_file_name(char *out, const char *prefix, int faction, const char *middle, int place, const char *suffix) {
    out[0] = 0;
    strcat(out, prefix);
    char number[16];
    snprintf(number, sizeof number, "%d", faction);
    strcat(out, number);
    strcat(out, middle);
    snprintf(number, sizeof number, "%d", place);
    strcat(out, number);
    strcat(out, suffix);
}

// ---- The object -------------------------------------------------------------------------------------------

enum { FAMEWIN_BASE = 0x74daf8, FAME_TABLE = 0x9a3840, FAME_FILE_FLAG = 0x759194 };

struct game_famewin {
    struct game_win win;       // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;    // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t x, y, w, h;        // 0xa14: the working numbers the window lays itself out with: where text goes next, and its width
    struct ui_rect clip;       // 0xa24: the part of the picture that is drawn over (the whole window, or the 800 by 600 of it)
    struct ui_rect spare;      // 0xa34: the working rectangle for a button, and for a line of the list
    int32_t line_h;            // 0xa44: the height of a line of the window's font
    int32_t big;               // 0xa48: the game's flag for a screen larger than the 800 by 600 picture
    int32_t faction;           // 0xa4c: the faction whose game FameWin_try_to_add_me added
    struct game_spot spot;     // 0xa50: the list's lines, for a click
    struct game_flat_button faction_btn[7];  // 0xa5c: the portraits, ids 1 to 7
    struct game_button_group group;          // 0x5970: the portraits, one of them chosen
    struct game_flat_button record_btn[6];   // 0x5a04: the records, ids 0x8ae to 0x8b3
    struct game_flat_button ok_btn;          // 0x9dcc: id 0x7b8
    int32_t selected;          // 0xa918: the place of the game shown, or -1
    struct game_buffer back;   // 0xa91c: the 1024 by 768 backdrop
    struct game_buffer back2;  // 0xaea4: the 800 by 600 picture
    int32_t rect[6][4];        // 0xb42c: the picture's panels, in the window's coordinates
    struct game_sprite sprite[12];  // 0xb48c: the pictures of the record buttons, two of each
};
GAME_AT(struct game_famewin, x, 0xa14);
GAME_AT(struct game_famewin, clip, 0xa24);
GAME_AT(struct game_famewin, spare, 0xa34);
GAME_AT(struct game_famewin, line_h, 0xa44);
GAME_AT(struct game_famewin, big, 0xa48);
GAME_AT(struct game_famewin, faction, 0xa4c);
GAME_AT(struct game_famewin, spot, 0xa50);
GAME_AT(struct game_famewin, faction_btn, 0xa5c);
GAME_AT(struct game_famewin, group, 0x5970);
GAME_AT(struct game_famewin, record_btn, 0x5a04);
GAME_AT(struct game_famewin, ok_btn, 0x9dcc);
GAME_AT(struct game_famewin, selected, 0xa918);
GAME_AT(struct game_famewin, back, 0xa91c);
GAME_AT(struct game_famewin, back2, 0xaea4);
GAME_AT(struct game_famewin, rect, 0xb42c);
GAME_AT(struct game_famewin, sprite, 0xb48c);
_Static_assert(sizeof(struct game_famewin) == 0xb69c, "layout of the game's FameWin");

// The slots of the window's vtable the screen uses (byte offsets).
enum { VT_SHOW = 0x4, VT_HIDE = 0x8, VT_SET_MODAL = 0xe4, VT_RELEASE_MODAL = 0xe8, VT_REDRAW = 0xf8 };
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66c268, VTABLE_BUF = 0x66c260 };

enum { ID_OK = 0x7b8, ID_RECORD_BASE = 0x8ae };

// The game's data it reads, by address.
enum {
    FONT_LIST = 0x8a40c8,    // struct font *: the font of the lists and of the window
    FONT_FIGURES = 0x8a40d4,  // the font of the three column headings
    FONT_HEADING = 0x8a40ec,  // the font of a record's heading
    FONT_BUTTON = 0x6e84b4,   // the Font of the buttons (an object, not a pointer)
    SMALL_SCREEN = 0x8a4168,  // 0 when the picture is the whole window
    MAIN_BUFFER = 0x8ea3a4,   // the Buffer the game draws its screen in (the picture kept with a game is a copy of it)
    COLOR_TEXT = 0xe6, COLOR_HEADING = 0xf4, COLOR_CHOSEN = 0x9b,
    FRAME_OK = 0x78dc30,      // the OK button's three frames, 0x28 bytes apart, the first being the one it lies in
    PORTRAITS = 0x78eea0,     // the portrait of each faction, 0x65c bytes each
    PORTRAIT_SPRITES = 0x78f5d8,  // the buttons' pictures of them
    DIFFICULTY_NAMES = 0x96c85c,
    VICTORY_NAME = 0x9a5e88,  // the label of how a transcendence game ended
    NET_PEERS = 0x93e8ec, NET_FLAG_A = 0x93e944, NET_FLAG_B = 0x93e8c8,  // a network game's state: the list is not clickable in it
};
// The text it builds file names and lines with.
enum {
    S_SPACE = 0x682820,
    S_BACK = 0x684578, S_BACK2 = 0x684584, S_SHEET = 0x68458c,
    S_PIC_A = 0x68459c, S_PIC_B = 0x6845a4, S_PIC_C = 0x6845a8,  // the name of the picture of the game shown
    S_PLANET_2 = 0x6845b0, S_PLANET_1 = 0x6845b8,                // what follows the planet's line for a game of planet 2 and 1
    S_PLUS = 0x6845c0, S_RATING_SUFFIX = 0x6845c8, S_DASH = 0x6845cc,
    S_NEW_A = 0x6845d0, S_NEW_B = 0x6845d8, S_NEW_C = 0x6845dc,  // the name of the picture of a game that is added
    S_OLD_A = 0x6845e4, S_OLD_B = 0x6845ec, S_OLD_C = 0x6845f0,  // a picture moved down: its old name
    S_MOVED_A = 0x6845f8, S_MOVED_B = 0x684600, S_MOVED_C = 0x684604,  // and its new one
    S_READ_MODE = 0x684620, S_FILE_NAME = 0x684624, S_WRITE_MODE = 0x68460c, S_WRITE_NAME = 0x684610,
};
// The labels (labels.txt entries).
enum {
    L_RECORD = 0x3ce,  // the six headings of the records, and the buttons' bubble text
    L_CONQUEST_A = 0x20c, L_CONQUEST_B = 0x384, L_ECONOMIC = 0x3a2, L_OTHER = 0x39c, L_SCENARIO = 0x405,
    L_FACTION = 0x147, L_ENERGY = 0x4c, L_CREDITS = 0x39, L_PART_B = 0x2ad, L_DIFFICULTY = 0x428, L_PLANET_2 = 0x242,
    L_PLANET_1 = 0x241, L_IRONMAN = 0x411, L_SCORE = 0x1b0, L_RATING = 0x424, L_OF = 0xb4, L_YEAR = 0x2da,
    L_OK = 0x299, L_HEAD_A = 0x427, L_HEAD_B = 0x132, L_HEAD_C = 0x3c4,
};

static struct game_famewin *the_famewin(void) { return game_at(FAMEWIN_BASE); }
static char *text(uint32_t addr) { return game_at(addr); }
static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static struct font *font_in(uint32_t addr) { return *(struct font **)game_at(addr); }
static struct fame_entry *fame(void) { return game_at(FAME_TABLE); }

static void show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, 0); }
static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(struct game_famewin *fw) { UI_VCALL(void(UI_THIS *)(void *), fw, VT_RELEASE_MODAL); }

static void cat(const char *s) { strcat(game_str_buffer, s); }
static void cat_number(int v) {
    char t[16];
    snprintf(t, sizeof t, "%d", v);
    strcat(game_str_buffer, t);
}
static void upper(void) { (*game_char_upper)(game_str_buffer); }

// ---- Construction ------------------------------------------------------------------------------------------

static void *UI_THIS famewin_ctor(struct game_famewin *fw) {
    graphic_win_ctor(&fw->win);
    spot_ctor(&fw->spot);
    for (int i = 0; i < 7; i++) flat_button_ctor(&fw->faction_btn[i]);
    button_group_ctor(&fw->group);
    for (int i = 0; i < 6; i++) flat_button_ctor(&fw->record_btn[i]);
    flat_button_ctor(&fw->ok_btn);
    buffer_ctor(&fw->back);
    buffer_ctor(&fw->back2);
    for (int i = 0; i < 12; i++) sprite_ctor(&fw->sprite[i]);
    fw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    fw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return fw;
}

static void UI_THIS famewin_dtor(struct game_famewin *fw) {
    for (int i = 11; i >= 0; i--) sprite_close(&fw->sprite[i]);
    buffer_dtor(&fw->back2);
    buffer_dtor(&fw->back);
    flat_button_dtor(&fw->ok_btn);
    for (int i = 5; i >= 0; i--) flat_button_dtor(&fw->record_btn[i]);
    button_group_close(&fw->group);
    for (int i = 6; i >= 0; i--) flat_button_dtor(&fw->faction_btn[i]);
    spot_dtor(&fw->spot);
    graphic_win_dtor(&fw->win);
}

static void *UI_THIS famewin_scalar_dtor(struct game_famewin *fw, int flags) {
    famewin_dtor(fw);
    if (flags & 1) game_free(fw);
    return fw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS famewin_operator_delete(struct game_buffer *buf, int flags) {
    return famewin_scalar_dtor((struct game_famewin *)((uint8_t *)buf - offsetof(struct game_famewin, buf)), flags);
}

// ---- Opening the window ------------------------------------------------------------------------------------

// A button is made hidden over a rectangle given as left, top, width and height; its own Buffer takes the buttons' font.
static void make_button(struct game_famewin *fw, struct game_flat_button *b, const char *name, int id, int x, int y, int w, int h) {
    game_base_button_init(b, name, id, x, y, w, h, fw, 0);
}

// Builds the window: the pictures are read, the panels placed, and the records' buttons, the portraits and OK made (hidden).
// Answers non-zero when a picture can't be read.
static int UI_THIS famewin_init(struct game_famewin *fw) {
    struct game_buffer sheet;
    buffer_ctor(&sheet);
    int result = 1;
    fw->w = 0x320, fw->h = 0x258;
    spot_init(&fw->spot, 0x40);
    fw->big = word(SMALL_SCREEN);

    struct famewin_position pos;
    famewin_position(&pos, fw->big != 0, *game_win_screen_w, *game_win_screen_h);
    if (!fw->big) {
        fw->x = 0, fw->y = 0;
        fw->clip.left = 0, fw->clip.top = 0;
        fw->clip.right = fw->w, fw->clip.bottom = fw->h;
    } else {
        fw->x = pos.offset_x, fw->y = pos.offset_y;
        fw->clip.left = pos.offset_x, fw->clip.top = pos.offset_y;
        fw->clip.right = fw->w + pos.offset_x, fw->clip.bottom = fw->h + pos.offset_y;
    }
    graphic_win_init(&fw->win, pos.x, pos.y, pos.w, pos.h, game_at(fw->big ? 0x7591a0 : 0x75919c), 0x1000000, NULL, NULL, NULL);

    const struct font *list_font = font_in(FONT_LIST);
    fw->line_h = famewin_line_height(list_font->first, list_font->ascent, list_font->height);
    if (buffer_load_pcx(&fw->back, text(S_BACK), NULL, 10, 0xec)) goto done;
    if (buffer_load_pcx(&fw->back2, text(S_BACK2), NULL, 10, 0xec)) goto done;
    famewin_rects(fw->rect, fw->clip.left, fw->clip.top);
    if (buffer_load_pcx(&sheet, text(S_SHEET), NULL, 10, 0xec)) goto done;

    // The record buttons' pictures are cut from the sheet: two looks of each, one above the other.
    int key = *game_default_key;
    fw->x = 1;
    struct game_sprite *sprite = fw->sprite;
    for (int col = 0; col < 6; col++) {
        fw->y = 1;
        for (int row = 0; row < 2; row++) {
            sprite_extract_2(sprite++, &sheet, key, fw->x, fw->y, 0x4f, 0x4f, NULL);
            fw->y += 0x50;
        }
        fw->x += 0x50;
    }

    // The six record buttons, across the top panel.
    int32_t *top = fw->rect[4];
    int cell = famewin_cell_width(top[0], top[2], 6);
    fw->w = cell;
    fw->x = famewin_cell_inset(cell, 0x4f) + top[0];
    fw->y = famewin_cell_inset(top[3] - top[1], 0x4f) + top[1];
    for (int i = 0; i < 6; i++) {
        struct game_flat_button *b = &fw->record_btn[i];
        game_str_buffer[0] = 0;
        cat(game_label(L_RECORD + i));
        make_button(fw, b, NULL, ID_RECORD_BASE + i, i * fw->w + fw->x, fw->y, 0x4f, 0x4f);
        b->pic[0] = &fw->sprite[2 * i + 1];
        b->pic[1] = &fw->sprite[2 * i];
        b->pic[2] = &fw->sprite[2 * i];
        base_button_set_bubble_text(&b->base, game_str_buffer);
        buffer_set_font(&b->base.buf, game_at(FONT_BUTTON), NULL, NULL, NULL);
        redraw(&b->base.win);
        redraw(&b->base.win);
        hide(&b->base.win);
    }

    // The seven portraits, across the second panel.
    int32_t *row = fw->rect[3];
    fw->w = famewin_cell_width(row[0], row[2], 7);
    fw->x = famewin_cell_inset(fw->w, 0x46) + row[0];
    fw->y = famewin_cell_inset(row[3] - row[1], 0x46) + row[1];
    button_group_init(&fw->group, 0x15, 0);
    fw->selected = -1;
    const struct faction_def *def = &game_faction_defs[1];
    for (int i = 0; i < 7; i++, def++) {
        struct game_flat_button *b = &fw->faction_btn[i];
        // "title name of the noun", the leader as the bubble text names them.
        game_str_buffer[0] = 0;
        *game_default_gender = def->leader_female;
        *game_default_plural = 0;
        cat(def->leader_title);
        cat(text(S_SPACE));
        *game_default_plural = 0;
        *game_default_gender = def->leader_female;
        cat(def->leader_name);
        cat(text(S_SPACE));
        cat(game_label(L_OF));
        cat(text(S_SPACE));
        *game_default_gender = def->noun_gender;
        *game_default_plural = def->noun_plural;
        cat(def->noun);

        fw->spare.top = fw->y;
        fw->spare.left = i * fw->w + fw->x;
        fw->spare.right = fw->spare.left + 0x46;
        fw->spare.bottom = fw->y + 0x46;
        make_button(fw, b, NULL, i + 1, fw->spare.left, fw->spare.top, fw->spare.right - fw->spare.left, fw->spare.bottom - fw->spare.top);
        uint8_t *portrait = (uint8_t *)game_at(PORTRAIT_SPRITES) + 0x65c * i;
        b->pic[1] = b->pic[2] = (struct game_sprite *)portrait;
        b->pic[0] = (struct game_sprite *)(portrait + 0x2c);
        base_button_set_bubble_text(&b->base, game_str_buffer);
        buffer_set_font(&b->base.buf, game_at(FONT_BUTTON), NULL, NULL, NULL);
        redraw(&b->base.win);
        redraw(&b->base.win);
        hide(&b->base.win);
        button_group_add(&fw->group, &b->base);
    }

    // OK.
    game_str_buffer[0] = 0;
    cat(game_label(L_OK));
    fw->spare.left = fw->clip.left + 0x22a;
    fw->spare.top = fw->clip.top + 0x22c;
    fw->spare.right = fw->spare.left + 0xce;
    fw->spare.bottom = fw->spare.top + 0x16;
    make_button(fw, &fw->ok_btn, game_str_buffer, ID_OK, fw->spare.left, fw->spare.top, fw->spare.right - fw->spare.left,
                fw->spare.bottom - fw->spare.top);
    fw->ok_btn.frame[0] = game_at(FRAME_OK + 0x50);
    fw->ok_btn.frame[1] = game_at(FRAME_OK + 0x28);
    fw->ok_btn.frame[2] = game_at(FRAME_OK);
    buffer_set_font(&fw->ok_btn.base.buf, game_at(FONT_BUTTON), NULL, NULL, NULL);
    redraw(&fw->ok_btn.base.win);
    hide(&fw->ok_btn.base.win);
    graphic_win_fill(&fw->win, 0);
    result = 0;
done:
    buffer_dtor(&sheet);
    return result;
}

// Takes the window down: the backdrop's Buffer closed, the modal loop released, the window closed.
static void UI_THIS famewin_close(struct game_famewin *fw) {
    buffer_close(&fw->back);
    release_modal(fw);
    graphic_win_close(&fw->win);
}

// ---- Drawing -----------------------------------------------------------------------------------------------

// A line of text in the buffer, placed by the working numbers: at the left, at the right of the column, or centred and
// wrapped (and the working y moved down by its height).
static void write_left(struct game_famewin *fw) {
    buffer_write_l_3(&fw->buf, game_str_buffer, fw->x, fw->y, (int)strlen(game_str_buffer));
}
static void write_right(struct game_famewin *fw) {
    buffer_write_right_l_4(&fw->buf, game_str_buffer, fw->x, fw->y, fw->w, (int)strlen(game_str_buffer));
}
static void write_wrapped(struct game_famewin *fw) {
    buffer_wrap_cent_4(&fw->buf, game_str_buffer, fw->x, fw->y, fw->w);
    fw->y += buffer_wrap_height(&fw->buf, game_str_buffer, fw->w);
}

static void set_font_and_color(struct game_famewin *fw, uint32_t font_addr, int color) {
    buffer_set_font(&fw->buf, font_in(font_addr), NULL, NULL, NULL);
    buffer_set_text_color(&fw->buf, color, -1, 1, 1);
}

// The words that tell how a game was won, drawn centred at the top of the figures.
static void draw_victory(struct game_famewin *fw, const struct fame_entry *e) {
    if (e->flags & FAME_TRANSCENDENCE) {
        game_str_buffer[0] = 0;
        cat(game_strings_get(game_strings, (char *)(uintptr_t)word(VICTORY_NAME)));
    } else if (e->flags & FAME_CONQUEST) {
        game_str_buffer[0] = 0;
        cat(game_label(L_CONQUEST_A));
        cat(text(S_SPACE));
        cat(game_label(L_CONQUEST_B));
    } else if (e->flags & FAME_ECONOMIC) {
        game_str_buffer[0] = 0;
        cat(game_label(L_ECONOMIC));
    } else if (e->flags & FAME_OTHER_VICTORY) {
        game_str_buffer[0] = 0;
        cat(game_label(L_OTHER));
    } else if (e->flags & FAME_SCENARIO) {
        game_str_buffer[0] = 0;
        cat(game_label(L_SCENARIO));
        cat(text(S_SPACE));
        cat(game_label(L_CONQUEST_B));
    } else {
        return;
    }
    upper();
    write_wrapped(fw);
}

// The figures of one game, in the panel at the right: the records it holds, how it was won, and the figures.
static void draw_figures(struct game_famewin *fw, const struct fame_entry *e) {
    struct game_buffer *b = &fw->buf;
    buffer_set_clip(b, fw->rect[2]);
    fw->x = fw->rect[2][0] + 3;
    fw->y = fw->rect[2][1] + 3;
    fw->w = fw->rect[2][2] - fw->rect[2][0] - 6;
    fw->h = fw->rect[2][3] - fw->rect[2][1] - 6;

    for (int i = 0; i < FAME_RECORD_COUNT; i++) {
        if (!(e->records & (1 << i))) continue;
        game_str_buffer[0] = 0;
        cat(game_label(L_RECORD + i));
        write_wrapped(fw);
    }
    draw_victory(fw, e);

    // The faction, then each figure: its name at the left, the number at the right of the same line.
    game_str_buffer[0] = 0;
    cat(game_label(L_FACTION));
    upper();
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->part_a);
    write_right(fw);
    fw->y += fw->line_h;

    game_str_buffer[0] = 0;
    cat(game_label(L_ENERGY));
    upper();
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->part_c);
    write_right(fw);
    fw->y += fw->line_h;

    game_str_buffer[0] = 0;
    cat(game_label(L_CREDITS));
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->credits);
    write_right(fw);
    fw->y += fw->line_h;

    game_str_buffer[0] = 0;
    cat(game_label(L_PART_B));
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->part_b);
    write_right(fw);
    fw->y += fw->line_h;

    game_str_buffer[0] = 0;
    cat(game_label(L_DIFFICULTY));
    write_left(fw);
    game_str_buffer[0] = 0;
    cat(game_strings_get(game_strings, (char *)(uintptr_t)word(DIFFICULTY_NAMES + 4 * e->difficulty)));
    upper();
    write_right(fw);
    fw->y += fw->line_h;

    if (e->planet) {
        game_str_buffer[0] = 0;
        if (e->planet == 2) {
            cat(game_label(L_PLANET_2));
            upper();
            write_left(fw);
            game_str_buffer[0] = 0;
            cat(text(S_PLANET_2));
            write_right(fw);
        } else {
            cat(game_label(L_PLANET_1));
            upper();
            write_left(fw);
            game_str_buffer[0] = 0;
            cat(text(S_PLANET_1));
            write_right(fw);
        }
        fw->y += fw->line_h;
    }

    if (e->flags & FAME_IRONMAN) {
        game_str_buffer[0] = 0;
        cat(game_label(L_IRONMAN));
        upper();
        write_left(fw);
        game_str_buffer[0] = 0;
        cat(text(S_PLUS));
        buffer_write_right_l_5(b, game_str_buffer, fw->x + fw->w, fw->y, (int)strlen(game_str_buffer));
        fw->y += fw->line_h;
    }

    game_str_buffer[0] = 0;
    cat(game_label(L_SCORE));
    upper();
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->score);
    write_right(fw);
    fw->y += fw->line_h;

    game_str_buffer[0] = 0;
    cat(game_label(L_RATING));
    upper();
    write_left(fw);
    game_str_buffer[0] = 0;
    cat_number(e->rating);
    cat(text(S_RATING_SUFFIX));
    write_right(fw);
    fw->y += fw->line_h;
}

// The heading of the first record the game holds, in a box above the figures (the rest are in the figures' own list).
static void draw_banner(struct game_famewin *fw, const struct fame_entry *e) {
    struct game_buffer *b = &fw->buf;
    buffer_set_clip(b, b->full);
    buffer_set_font(b, font_in(FONT_HEADING), NULL, NULL, NULL);
    buffer_set_text_color(b, COLOR_HEADING, -1, 1, 1);
    int drawn = 0;
    for (int i = 0; i < FAME_RECORD_COUNT; i++) {
        if (drawn || !(e->records & (1 << i))) continue;
        game_str_buffer[0] = 0;
        cat(game_label(L_RECORD + i));
        int w1 = buffer_text_width(b, game_str_buffer);
        int w2 = buffer_text_width(b, game_str_buffer);
        int left, width;
        famewin_banner(&left, &width, fw->clip.left, w2);
        buffer_fill(b, left, fw->clip.top + 0xf, w1 + 0x14, 0x15, 0x41);
        (void)width;
        buffer_write_cent_l_5(b, game_str_buffer, fw->clip.left, fw->clip.top + 0x10, 0x320, (int)strlen(game_str_buffer));
        drawn = 1;
    }
    set_font_and_color(fw, FONT_LIST, COLOR_TEXT);
}

// The window: the picture, the buttons in the state the game shown asks for, the figures of the game shown and the list of
// the faction's five games.
static void UI_THIS famewin_on_redraw(struct game_famewin *fw) {
    struct game_buffer tmp;
    buffer_ctor(&tmp);
    struct game_buffer *b = &fw->buf;
    buffer_set_clip(b, b->full);
    if (fw->big) buffer_copy_full(&fw->back, b, 0, 0, 0, 0, 0x400, 0x300);
    buffer_copy_full(&fw->back2, b, 0, 0, fw->clip.left, fw->clip.top, 0x320, 0x258);
    buffer_set_clip(b, &fw->clip.left);
    set_font_and_color(fw, FONT_LIST, COLOR_TEXT);

    for (int i = 0; i < 6; i++) show(&fw->record_btn[i]);
    for (int i = 0; i < 7; i++) show(&fw->faction_btn[i]);
    show(&fw->ok_btn);

    // The game shown: faction `sel`'s place `rank`.
    int sel = fw->group.sel, rank = fw->selected;
    game_str_buffer[0] = 0;
    cat(text(S_PIC_A));
    cat_number(sel);
    cat(text(S_PIC_B));
    cat_number(rank + 1);
    cat(text(S_PIC_C));
    struct fame_entry *e = fame() + famewin_entry_index(sel, rank);
    int in_use = e->flags & FAME_IN_USE;
    if (in_use) {
        // The picture kept with the game, in the panel at the right of the list.
        if (!buffer_load_pcx(&tmp, game_str_buffer, NULL, 0, 0xec))
            buffer_copy_full(&tmp, b, 0, 0, fw->rect[1][0], fw->rect[1][1], fw->rect[1][2] - fw->rect[1][0],
                             fw->rect[1][3] - fw->rect[1][1]);
        for (int i = 0; i < 6; i++) {
            fw->record_btn[i].base.picture = (e->records & (1 << i)) ? (void *)2 : (void *)-1;
            redraw(&fw->record_btn[i]);
        }
        // The portrait, in the panel's corner.
        const struct game_sprite *portrait = (struct game_sprite *)((uint8_t *)game_at(PORTRAITS) + 0x65c * sel);
        sprite_draw_3((struct game_sprite *)portrait, b, portrait->key, fw->rect[5][0] + 9, fw->rect[5][1] + 0xa);
        draw_figures(fw, e);
        draw_banner(fw, e);
    } else {
        for (int i = 0; i < 6; i++) {
            fw->record_btn[i].base.picture = (void *)-1;
            redraw(&fw->record_btn[i]);
        }
    }

    // The three column headings.
    buffer_set_font(b, font_in(FONT_FIGURES), NULL, NULL, NULL);
    buffer_set_text_color(b, COLOR_HEADING, -1, 1, 1);
    static const struct {
        int label, left, width;
        int caps;
    } headings[] = {{L_HEAD_A, 0x1c, 0x109, 0}, {L_HEAD_B, 0x12e, 0xf0, 1}, {L_HEAD_C, 0x227, 0xd4, 0}};
    for (int i = 0; i < 3; i++) {
        game_str_buffer[0] = 0;
        cat(game_label(headings[i].label));
        if (headings[i].caps) upper();
        buffer_write_cent_l_5(b, game_str_buffer, fw->clip.left + headings[i].left, fw->clip.top + 0xf7, headings[i].width,
                              (int)strlen(game_str_buffer));
    }
    set_font_and_color(fw, FONT_LIST, COLOR_TEXT);

    // The faction's five games in the left panel, each a title and name, the faction's name and the figures.
    spot_kill_type(&fw->spot, 0);
    int32_t *r = fw->rect[0];
    fw->spare.left = r[0], fw->spare.top = r[1], fw->spare.right = r[2], fw->spare.bottom = r[3];
    struct fame_entry *row = fame() + (sel - 1) * FAME_RANKS;
    for (int j = 0; j < FAME_RANKS; j++, row++) {
        fw->x = fw->spare.left;
        fw->y = fw->spare.top + 3;
        fw->w = fw->spare.right - fw->spare.left;
        if (row->flags & FAME_IN_USE) {
            buffer_set_text_color(b, fw->selected == j ? COLOR_CHOSEN : COLOR_TEXT, -1, 1, 1);
            game_str_buffer[0] = 0;
            cat(row->title);
            cat(text(S_SPACE));
            cat(row->name);
            buffer_write_cent_l_5(b, game_str_buffer, fw->x, fw->y, fw->w, (int)strlen(game_str_buffer));
            fw->y += fw->line_h;
            game_str_buffer[0] = 0;
            cat(row->noun);
            buffer_write_cent_l_5(b, game_str_buffer, fw->x, fw->y, fw->w, (int)strlen(game_str_buffer));
            fw->y += fw->line_h;
            game_str_buffer[0] = 0;
            cat_number(row->rating);
            cat(text(S_DASH));
            cat(text(S_SPACE));
            cat(text(S_SPACE));
            cat(game_label(L_YEAR));
            cat_number(row->year);
            buffer_write_cent_l_5(b, game_str_buffer, fw->x, fw->y, fw->w, (int)strlen(game_str_buffer));
            spot_add(&fw->spot, j, 0, fw->spare.left, fw->spare.top, fw->spare.right - fw->spare.left, fw->spare.bottom - fw->spare.top);
        }
        int step = fw->rect[0][3] - fw->rect[0][1] + 6;
        fw->spare.top += step;
        fw->spare.bottom += step;
    }
    buffer_dtor(&tmp);
}

// ---- Clicks ------------------------------------------------------------------------------------------------

// A click on the list: the game under it is the one shown. A network game's list can't be clicked.
static void UI_THIS famewin_on_left_click(struct game_famewin *fw, int x, int y) {
    if ((1u << (*game_current_faction & 31)) & (uint32_t)word(NET_PEERS)) return;
    if (word(NET_FLAG_A) || word(NET_FLAG_B)) return;
    int id = y, kind = x;
    if (spot_check(&fw->spot, x, y, &id, &kind) != -1) fw->selected = id;
    redraw(fw);
}

// A letter, a digit, Return or Escape closes the window.
static int UI_THIS famewin_on_key_click(struct game_famewin *fw, int unused, int key) {
    (void)unused;
    if ((key > 0x2f && key < 0x5b) || key == 0x1b || key == 0xd) famewin_close(fw);
    return 1;
}

// OK closes the window; a record's button shows the first game that holds the record, if there is one.
static void UI_THIS famewin_on_button_clicked(struct game_famewin *fw, int id) {
    if (id == ID_OK) {
        famewin_close(fw);
        return;
    }
    int bit = id - ID_RECORD_BASE;
    if (bit < 0 || bit > 5) return;
    int row, pos;
    if (famewin_find_record(fame(), bit, &row, &pos)) {
        button_group_set(&fw->group, row + 1, 1);
        fw->selected = pos;
    }
    redraw(fw);
}

// A portrait was chosen: its faction's first game is shown.
static void UI_THIS famewin_on_group_clicked(struct game_famewin *fw, int a, int b, int c) {
    (void)a, (void)b, (void)c;
    fw->selected = 0;
    redraw(fw);
}

// ---- A game added to the list ------------------------------------------------------------------------------

// A game moves from `rank` to the next place down: the picture kept with it is read and written under its new name, and
// the entry copied (the entry below it is lost). Only ranks 0 to 3 move.
static void UI_THIS famewin_demote(struct game_famewin *fw, int rank) {
    struct game_buffer tmp;
    buffer_ctor(&tmp);
    if (rank < 4) {
        famewin_file_name(game_str_buffer, text(S_OLD_A), fw->faction, text(S_OLD_B), rank + 1, text(S_OLD_C));
        buffer_load_pcx(&tmp, game_str_buffer, NULL, 0, 0xec);
        famewin_file_name(game_str_buffer, text(S_MOVED_A), fw->faction, text(S_MOVED_B), rank + 2, text(S_MOVED_C));
        buffer_write_pcx(&tmp, game_str_buffer);
        struct fame_entry *from = fame() + famewin_entry_index(fw->faction, rank);
        famewin_copy(from + 1, from);
    }
    buffer_dtor(&tmp);
}

// Adds the game that has just ended, for the faction now playing (the player's): unless the game was cheated in, where it
// ranks among its faction's five, the entry itself, the records it takes, and a picture of the screen kept with it.
static void UI_THIS famewin_try_to_add_me(struct game_famewin *fw) {
    if (*game_state & 0x20) return;
    int faction = *game_current_faction;
    fw->faction = faction;
    compute_score(faction, NULL, NULL, 0);
    struct faction_data *f = &game_factions[faction];
    int score = f->score;
    int rating = get_rating(faction, score);

    struct fame_entry *row = fame() + (faction - 1) * FAME_RANKS;
    int pos = famewin_place(row, rating);
    if (pos >= FAME_NO_PLACE) return;
    for (int r = 3; r >= 0; r--)
        if (pos <= r) famewin_demote(fw, r);
    fw->selected = pos;

    struct fame_entry *n = row + pos;
    n->rating = rating;
    n->score = score;
    n->credits = f->energy_credits;
    n->records = 0;
    n->flags = FAME_IN_USE;
    n->year = game_year(*game_turn);
    n->part_c = f->score_parts[2] + f->score_parts[3];
    n->part_a = f->score_parts[4];
    n->part_b = f->score_parts[0];
    n->planet = f->score_parts[5] ? (*game_native_life ? 2 : 1) : 0;
    const struct faction_def *def = &game_faction_defs[faction];
    *game_default_plural = 0;
    *game_default_gender = def->leader_female;
    strncpy(n->title, def->leader_title, FAME_NAME_LEN);
    *game_default_plural = 0;
    *game_default_gender = def->leader_female;
    strncpy(n->name, def->leader_name, FAME_NAME_LEN);
    *game_default_gender = def->noun_gender;
    *game_default_plural = def->noun_plural;
    strncpy(n->noun, def->noun, FAME_NAME_LEN);
    n->difficulty = (uint8_t)f->diff_level;
    n->flags = famewin_flags(*game_state, *game_rules, f->score_parts[6] != 0);
    // (the flags' bit 1 and the other bits are set on the byte as it stands, so the in-use bit set above is kept)
    famewin_award(fame(), n, faction - 1, pos);

    famewin_file_name(game_str_buffer, text(S_NEW_A), faction, text(S_NEW_B), pos + 1, text(S_NEW_C));
    buffer_write_pcx(game_at(MAIN_BUFFER), game_str_buffer);
}

// ---- hall_of_fame ------------------------------------------------------------------------------------------

// Every entry unused: the first byte of each of the 35 is cleared (the rest of the list is left as it is).
static void clear_list(void) {
    for (int i = 0; i < FAME_ENTRIES; i++) fame()[i].flags = 0;
}

static void hall_of_fame(int final) {
    int32_t *file_flag = game_at(FAME_FILE_FLAG);
    void *fp = game_crt_fopen(text(S_FILE_NAME), text(S_READ_MODE));
    if (!fp) {
        clear_list();
    } else {
        *file_flag = 0;
        if (!game_fread(fame(), sizeof(struct fame_entry) * FAME_ENTRIES, 1, fp)) clear_list();
        game_fclose(fp);
    }

    struct game_famewin *fw = the_famewin();
    if (famewin_init(fw)) return;
    if (final) famewin_try_to_add_me(fw);
    button_group_set(&fw->group, *game_current_faction, 1);
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), fw, VT_SET_MODAL, 0, (int)(uintptr_t)game_pop_wait, 0);

    fp = game_crt_fopen(text(S_WRITE_NAME), text(S_WRITE_MODE));
    if (fp) {
        *file_flag = 1;
        if (!game_fwrite(fame(), sizeof(struct fame_entry) * FAME_ENTRIES, 1, fp)) clear_list();
        game_fclose(fp);
    }
}

// In shadow mode a check that reaches the game's end holds the call (the window allocates, and waits for the player) and
// it is made when the check is over.
static void replay_hall_of_fame(const struct hook_ui_call *c) { hall_of_fame(c->arg[0]); }

static void UI_CDECL shadow_hall_of_fame(int final) {
    if (!hooks_protecting()) {
        hall_of_fame(final);
        return;
    }
    struct hook_ui_call c = {.name = "hall_of_fame", .replay = replay_hall_of_fame};
    c.arg[0] = final;
    hooks_ui_hold(&c);
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static struct hook_stats stats_hall_of_fame = {"hall_of_fame"};
static void *orig_hall_of_fame;

static const struct hook famewin_hooks[] = {
    UI_HOOK(0x448400, famewin_init, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x9b, 0x4e, 0x65, 0x00),  // push -1; push handler
    UI_HOOK(0x448d60, famewin_close, 0x56, 0x8b, 0xf1, 0x8d, 0x8e, 0x1c, 0xa9, 0x00, 0x00),  // push esi; mov esi, ecx; lea ecx, [esi+0xa91c]
    UI_HOOK(0x448d90, famewin_on_redraw, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xb0, 0x4e, 0x65, 0x00),
    UI_HOOK(0x44a4b0, famewin_on_key_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x56, 0x83, 0xf8, 0x2f),
    UI_HOOK(0x44a500, famewin_on_left_click, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),  // push esi; mov esi, ecx
    UI_HOOK(0x44a570, famewin_on_button_clicked, 0x55, 0x8b, 0xec, 0x51, 0x53, 0x8b, 0xd9, 0x8b, 0x4d, 0x08),
    UI_HOOK(0x44a640, famewin_on_group_clicked, 0x8b, 0x01, 0xc7, 0x81, 0x18, 0xa9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    UI_HOOK(0x44a660, famewin_try_to_add_me, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0xa0, 0xc0, 0x64, 0x9a, 0x00),
    UI_HOOK(0x44ac10, famewin_demote, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xc5, 0x4e, 0x65, 0x00),
    // hall_of_fame: the machinery's own entry, whose shadow side is the held call
    {0x44ae50, (const uint8_t[]){0x55, 0x8b, 0xec, 0x56, 0x68, 0x20, 0x46, 0x68, 0x00}, 9, (void *)hall_of_fame,
     (void *)shadow_hall_of_fame, &orig_hall_of_fame, &stats_hall_of_fame},
    UI_HOOK(0x44b100, famewin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xec, 0x4d, 0x65, 0x00),
    UI_HOOK(0x44b200, famewin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x84, 0x4e, 0x65, 0x00),
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x44b300, famewin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0xf5, 0xfd, 0xff, 0xff),
    UI_HOOK(0x44b330, famewin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module famewin_module = {"famewin", famewin_hooks, UI_HOOK_COUNT(famewin_hooks), NULL, NULL, -1};
