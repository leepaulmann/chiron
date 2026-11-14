// The Monuments window, the game's MonuWin, and monument(), the function that opens it. The window is one global object
// (0x7f9f58) built by a static initializer and destroyed by an atexit handler. Outside the class only monument() touches it
// (its address, its zoom word at 0xa4c, and the count of monuments the faction has, the word the object is followed by);
// monument() is called by the sixteen mon_* functions the game makes a monument with (they write the record, and call it when
// the faction is the player's) and by the console's menu (the Monuments command and its shortcut).
//
// What the window shows: the monuments the player's faction has made (a colony founded, a tech discovered, a secret of the
// planet learned, a unit type built, ..., fifteen kinds), as icons laid over a picture that grows with the count (a movie, played
// as many frames as there are monuments), in the order of their dates; at the right and below, the pictures of the factions it
// has beaten. The mouse over an icon shows the monument's picture, and a click on one zooms it: its picture, with a text under
// it of what happened (built from the game's text variables and read from the file of monument texts, in the way the original
// builds it: each kind has its own variables and its own text, one when the faction was the first to have it and another
// when another faction had it before). OK, Return, any key and a click close it (in the zoomed view they go back to the list).
//
// The records a monument is written in, and the kind the window last showed, are the faction's table (0x94ca08, 0x4f4 bytes
// each): the window writes one word of it, the kind shown, which a click on an icon sets (the game keeps it with the table, so
// it is saved with the game). The mon_* functions are not rebuilt here: they are the writers of the table, held in shadow mode
// by quiet.c (the window they end with allocates), and are kept as they are; this module opens the window for them.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=monuwin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. Static initializers run after hooks_apply, so the constructor
// and destructor are ours too.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the string list the redraw keeps
// on its stack is cleared whole before it is made (the original leaves most of it as it was); the movie player's block is
// a fixed size (the original's is the stack frame's); the original's "is it a window" test of the Buffer's address (the
// window is never null) is left out.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "monuwin.h"

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
#include "../engine/sprite.h"
#include "../engine/strlist.h"
#include "../engine/tech.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

void monuwin_position(struct monuwin_position *p, int big, int screen_w, int screen_h) {
    if (!big) {
        *p = (struct monuwin_position){0, 0, 0x320, 0x258, 0, 0};
        return;
    }
    *p = (struct monuwin_position){(screen_w - 0x400) / 2, screen_h - 0x300, 0x400, 0x300, 0x70, 0x54};
}

void monuwin_rects(struct monuwin_rects *r, int dx, int dy, int fourteen) {
    static const int32_t view[4] = {0x13f, 0xca, 0x1df, 0x12b};
    static const int32_t caption[4] = {0x90, 0x2f, 0x293, 0x5b};
    static const int32_t zoom[4] = {0x65, 0x76, 0x2bd, 0x1de};
    static const int32_t slot[MONU_SLOTS][4] = {
        {0x12d, 0x1a9, 0x159, 0x1d3}, {0x1c7, 0x1a9, 0x1f3, 0x1d3}, {0x12d, 0x175, 0x159, 0x19f}, {0x1c7, 0x175, 0x1f3, 0x19f},
        {0xa3, 0x1a9, 0xcf, 0x1d3},   {0x24e, 0x1a9, 0x27a, 0x1d3}, {0xb7, 0x175, 0xe3, 0x19f},   {0x23a, 0x175, 0x266, 0x19f},
        {0x0, 0x1a9, 0x2c, 0x1d3},    {0x2ee, 0x1a9, 0x31a, 0x1d3}, {0x25, 0x175, 0x51, 0x19f},   {0x2cc, 0x175, 0x2f8, 0x19f},
        {0x45, 0x141, 0x71, 0x16b},   {0x2aa, 0x141, 0x2d6, 0x16b}};
    static const int32_t pair[MONU_PAIRS][4] = {{0xfd, 0x1f4, 0x14b, 0x233}, {0x1dc, 0x1f4, 0x225, 0x233}, {0xbe, 0x1f4, 0x14b, 0x233},
                                                {0x1dc, 0x1f4, 0x262, 0x233}, {0x7f, 0x1f4, 0x14b, 0x233}, {0x1dc, 0x1f4, 0x2b1, 0x233}};
    static const int32_t face[MONU_FACE_KINDS][4] = {{0xfe, 0x1ff, 0x138, 0x231}, {0x1e0, 0x1ff, 0x21a, 0x231}, {0xc0, 0x1ff, 0xfa, 0x231},
                                                     {0x21e, 0x1ff, 0x258, 0x231}, {0x81, 0x1ff, 0xbb, 0x231}, {0x25d, 0x1ff, 0x297, 0x231}};
    static const int32_t portrait[2][4] = {{0x16c, 0x6f, 0x1b3, 0xaf}, {0x148, 0x6, 0x1d6, 0x6b}};
#define MOVE(dst, src)                                                                                                  \
    do {                                                                                                                \
        (dst)[0] = (src)[0] + dx, (dst)[1] = (src)[1] + dy, (dst)[2] = (src)[2] + dx, (dst)[3] = (src)[3] + dy;          \
    } while (0)
    MOVE(r->view, view);
    MOVE(r->caption, caption);
    MOVE(r->zoom, zoom);
    for (int i = 0; i < MONU_SLOTS; i++) MOVE(r->slot[i], slot[i]);
    for (int i = 0; i < MONU_PAIRS; i++) MOVE(r->pair[i], pair[i]);
    for (int i = 0; i < MONU_FACE_KINDS; i++) MOVE(r->face[i], face[i]);
    MOVE(r->portrait, portrait[fourteen ? 1 : 0]);
#undef MOVE
}

void monuwin_icon_cell(int i, int *x, int *y) {
    *x = 2 + 0x2d * (i <= 7 ? i : i - 8);
    *y = i <= 7 ? 0x226 : 0x251;
}

void monuwin_big_cell(int i, int *x, int *y) {
    *x = 1 + 0xa1 * (i % 5);
    *y = 0x89 + 0x62 * (i / 5);
}

void monuwin_sort(int *order, const int32_t *when) {
    for (int i = 1; i < MONU_KINDS; i++)
        for (int j = i; j < MONU_KINDS; j++) {
            int a = order[i - 1], b = order[j];
            if (b == MONU_NO_KIND) continue;
            if (a == MONU_NO_KIND || when[a] > when[b]) {
                order[i - 1] = b;
                order[j] = a;
            }
        }
}

int monuwin_pair_slot(int count, int second) { return second ? 2 * ((count - 2) / 2) + 1 : 2 * ((count - 1) / 2); }

int monuwin_face_walk(int count, const int *used, int *kind, int *place) {
    int e = 0, drawn = 0;
    for (int step = 0; step < count; step++) {
        if (used[e]) {
            kind[drawn] = MONU_KINDS + e;
            place[drawn] = step;
            drawn++;
            e += 1;
        } else {
            e += 2;
        }
    }
    return drawn;
}

int monuwin_zoom_picture(int kind, int *x, int *y) {
    if (kind == 13) {
        *x = 0x155, *y = 0xb;
        return 0;
    }
    if (kind >= MONU_KINDS) {
        *x = 0x155, *y = 0x174;
        return 1;
    }
    *x = 0, *y = 0;
    return 2;
}

int monuwin_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

// ---- The object -------------------------------------------------------------------------------------------

enum {
    MONUWIN_BASE = 0x7f9f58, MONU_COUNT = 0x7fd634, TITLE_SMALL = 0x7fd63c, TITLE_BIG = 0x7fd640,
    MONU_TABLE = 0x94ca08,
};

// A faction's record of monuments: twenty-one of them, the kind the window shows (written by a click on an icon, and kept with
// the game), and three names (a base's, for the kinds that name one).
struct monu_table {
    struct monu_entry m[MONU_ALL_KINDS];  // 0x0
    int32_t shown;                        // 0x498
    uint8_t pad_49c[0x4ac - 0x49c];
    char names[3][24];  // 0x4ac
};
_Static_assert(sizeof(struct monu_table) == 0x4f4, "layout of the game's monument table");
_Static_assert(offsetof(struct monu_table, shown) == 0x498, "shown");

struct game_monuwin {
    struct game_win win;     // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t x, y, w, h;      // 0xa14: the working numbers the window is laid out with: the corner for a picture, and its size
    struct ui_rect clip;     // 0xa24: the part of the window the picture lies in (the whole, or the 800 by 600 of it)
    struct ui_rect spare;    // 0xa34: the working rectangle for a button and for each icon
    int32_t line_h;          // 0xa44: the height of a line of the window's font
    int32_t big;             // 0xa48: the game's flag for a screen larger than the 800 by 600 picture
    int32_t zoomed;          // 0xa4c: 1 while a monument is zoomed, else the list is shown
    int32_t faction;         // 0xa50: the faction whose monuments are shown (the player's)
    int32_t last;            // 0xa54: the kind last shown or hovered
    struct game_flat_button list_btn;  // 0xa58: OK (shown with the list; id 0x3e7)
    struct game_flat_button zoom_btn;  // 0x15a4: the other (shown with a zoomed monument; the same id)
    struct game_spot spot;             // 0x20f0: the places of the icons, for a click
    struct game_buffer back;           // 0x20fc: the 1024 by 768 picture with the movie's last frame
    struct game_buffer zoom_back;      // 0x2684: the picture a zoomed monument is shown over
    struct game_sprite icon[MONU_KINDS];  // 0x2c0c: the icons
    struct game_sprite big_pic[MONU_KINDS];  // 0x2ea0: the pictures of the monuments
    int32_t view[4];                   // 0x3134: where the picture of the monument shown goes (and its spot, id 0x45)
    int32_t caption[4];                // 0x3144: the box the words of a zoomed monument go in
    int32_t zoom[4];                   // 0x3154: where the picture of a zoomed monument goes
    int32_t slot[MONU_SLOTS][4];       // 0x3164: the places of the listed icons
    struct game_sprite pair_pic[MONU_PAIRS];  // 0x3244: the pictures of the wins
    int32_t pair[MONU_PAIRS][4];       // 0x334c: where they go
    struct game_sprite face_small[7];  // 0x33ac: the small portrait of each faction
    struct game_sprite face_big[7];    // 0x34e0: the large one
    struct game_sprite own_picture;    // 0x3614: the player's faction's picture, for the monument of kind 13
    struct game_sprite own_portrait;   // 0x3640: the player's leader
    int32_t face[MONU_FACE_KINDS][4];  // 0x366c: where the small portraits of the beaten factions go
    int32_t portrait[4];               // 0x36cc: where the leader's goes
};
GAME_AT(struct game_monuwin, x, 0xa14);
GAME_AT(struct game_monuwin, clip, 0xa24);
GAME_AT(struct game_monuwin, spare, 0xa34);
GAME_AT(struct game_monuwin, line_h, 0xa44);
GAME_AT(struct game_monuwin, big, 0xa48);
GAME_AT(struct game_monuwin, zoomed, 0xa4c);
GAME_AT(struct game_monuwin, faction, 0xa50);
GAME_AT(struct game_monuwin, last, 0xa54);
GAME_AT(struct game_monuwin, list_btn, 0xa58);
GAME_AT(struct game_monuwin, zoom_btn, 0x15a4);
GAME_AT(struct game_monuwin, spot, 0x20f0);
GAME_AT(struct game_monuwin, back, 0x20fc);
GAME_AT(struct game_monuwin, zoom_back, 0x2684);
GAME_AT(struct game_monuwin, icon, 0x2c0c);
GAME_AT(struct game_monuwin, big_pic, 0x2ea0);
GAME_AT(struct game_monuwin, view, 0x3134);
GAME_AT(struct game_monuwin, caption, 0x3144);
GAME_AT(struct game_monuwin, zoom, 0x3154);
GAME_AT(struct game_monuwin, slot, 0x3164);
GAME_AT(struct game_monuwin, pair_pic, 0x3244);
GAME_AT(struct game_monuwin, pair, 0x334c);
GAME_AT(struct game_monuwin, face_small, 0x33ac);
GAME_AT(struct game_monuwin, face_big, 0x34e0);
GAME_AT(struct game_monuwin, own_picture, 0x3614);
GAME_AT(struct game_monuwin, own_portrait, 0x3640);
GAME_AT(struct game_monuwin, face, 0x366c);
GAME_AT(struct game_monuwin, portrait, 0x36cc);
_Static_assert(sizeof(struct game_monuwin) == 0x36dc, "layout of the game's MonuWin");

// The slots of the window's vtable the screen uses (byte offsets).
enum { VT_SHOW = 0x4, VT_HIDE = 0x8, VT_SET_MODAL = 0xe4, VT_RELEASE_MODAL = 0xe8, VT_REDRAW = 0xf8 };
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66c880, VTABLE_BUF = 0x66c878 };

enum { ID_BUTTON = 0x3e7, ID_VIEW = 0x45, KIND_OWN_PICTURE = 13 };

// The game's data it reads, by address.
enum {
    FONT_TEXT = 0x8a40bc,     // struct font *: the font of the window's words
    FONT_BUTTON = 0x6e84b4,   // the Font of the buttons (an object, not a pointer)
    SMALL_SCREEN = 0x8a4168,  // 0 when the picture is the whole window
    FRAME_OK = 0x78dc30,      // the buttons' three frames, 0x28 bytes apart, the first being the one it lies in
    SECRETS = 0x9a5888,       // the secret projects, 0x30 bytes each: the word at the start is the name's string
    NET_PEERS = 0x93e8ec, NET_FLAG_A = 0x93e944, NET_FLAG_B = 0x93e8c8,  // a network game's state: nothing is clickable in it
    MORE_PREFS = 0x9a6494, STATE_BYTE = 0x9a64c0,
    COLOR_TEXT = 0xf2,
};
// The labels (labels.txt entries).
enum { L_OK = 0x299, L_OTHER = 0x2d8 };
// The text it builds file names from.
enum {
    S_SHEET_FLC = 0x6863ac, S_ZOOM_A = 0x6863bc, S_ZOOM_B = 0x6863cc, S_FACTION_END = 0x6863dc, S_ICONS = 0x6863e4,
    S_OWN_END = 0x686750, S_OTHER_END = 0x686758, S_BIG_START = 0x686760, S_BIG_END = 0x686768,
};

static struct game_monuwin *the_monuwin(void) { return game_at(MONUWIN_BASE); }
static char *text(uint32_t addr) { return game_at(addr); }
static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static struct font *font_in(uint32_t addr) { return *(struct font **)game_at(addr); }
static struct monu_table *monu(int faction) { return (struct monu_table *)game_at(MONU_TABLE) + faction; }
static struct faction_def *def(int faction) { return &game_faction_defs[faction]; }
static uint8_t key_color(void) { return *game_default_key; }

static void show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, 0); }
static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(struct game_monuwin *mw) { UI_VCALL(void(UI_THIS *)(void *), mw, VT_RELEASE_MODAL); }

static void cat(const char *s) { strcat(game_str_buffer, s); }

// Whether a network game, which allows no mouse in the window: a peer of this faction's is set, or one of two flags is.
static int net_blocks(void) {
    return ((1u << *game_current_faction) & (uint32_t)word(NET_PEERS)) || word(NET_FLAG_A) || word(NET_FLAG_B);
}

// ---- Construction ------------------------------------------------------------------------------------------

static void *UI_THIS monuwin_ctor(struct game_monuwin *mw) {
    graphic_win_ctor(&mw->win);
    flat_button_ctor(&mw->list_btn);
    flat_button_ctor(&mw->zoom_btn);
    spot_ctor(&mw->spot);
    buffer_ctor(&mw->back);
    buffer_ctor(&mw->zoom_back);
    for (int i = 0; i < MONU_KINDS; i++) sprite_ctor(&mw->icon[i]);
    for (int i = 0; i < MONU_KINDS; i++) sprite_ctor(&mw->big_pic[i]);
    for (int i = 0; i < MONU_PAIRS; i++) sprite_ctor(&mw->pair_pic[i]);
    for (int i = 0; i < 7; i++) sprite_ctor(&mw->face_small[i]);
    for (int i = 0; i < 7; i++) sprite_ctor(&mw->face_big[i]);
    sprite_ctor(&mw->own_picture);
    sprite_ctor(&mw->own_portrait);
    mw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    mw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return mw;
}

static void UI_THIS monuwin_dtor(struct game_monuwin *mw) {
    sprite_close(&mw->own_portrait);
    sprite_close(&mw->own_picture);
    for (int i = 6; i >= 0; i--) sprite_close(&mw->face_big[i]);
    for (int i = 6; i >= 0; i--) sprite_close(&mw->face_small[i]);
    for (int i = MONU_PAIRS - 1; i >= 0; i--) sprite_close(&mw->pair_pic[i]);
    for (int i = MONU_KINDS - 1; i >= 0; i--) sprite_close(&mw->big_pic[i]);
    for (int i = MONU_KINDS - 1; i >= 0; i--) sprite_close(&mw->icon[i]);
    buffer_dtor(&mw->zoom_back);
    buffer_dtor(&mw->back);
    spot_dtor(&mw->spot);
    flat_button_dtor(&mw->zoom_btn);
    flat_button_dtor(&mw->list_btn);
    graphic_win_dtor(&mw->win);
}

static void *UI_THIS monuwin_scalar_dtor(struct game_monuwin *mw, int flags) {
    monuwin_dtor(mw);
    if (flags & 1) game_free(mw);
    return mw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS monuwin_operator_delete(struct game_buffer *buf, int flags) {
    return monuwin_scalar_dtor((struct game_monuwin *)((uint8_t *)buf - offsetof(struct game_monuwin, buf)), flags);
}

// ---- Opening the window ------------------------------------------------------------------------------------

// A movie player on the stack: the object, its Buffer (at 4) and its palette (at 0x5bc) are taken down in this order.
enum { FLIC_BUFFER = 4, FLIC_PALETTE = 0x5bc, FLIC_NEXT = 0x5ac, FLIC_DELAY = 0x5a4, FLIC_SIZE = 0xb00 };
static void flic_end(uint8_t *f) {
    game_flic_dtor(f);
    game_palette_dtor(f + FLIC_PALETTE);
    game_buffer_dtor(f + FLIC_BUFFER);
}

// A button is made hidden over the rectangle `spare` as it stands: its own Buffer takes the buttons' font and its three frames.
static void make_button(struct game_monuwin *mw, struct game_flat_button *b, int label) {
    game_str_buffer[0] = 0;
    cat(game_label(label));
    struct ui_rect *r = &mw->spare;
    if (mw->big) {
        r->left = 0x338, r->top = 0x2eb, r->right = 0x400, r->bottom = 0x300;
    } else {
        r->top = 0x243, r->left = 0x258, r->right = 0x320, r->bottom = 0x258;
    }
    game_base_button_init(b, game_str_buffer, ID_BUTTON, r->left, r->top, r->right - r->left, r->bottom - r->top, mw, 0);
    b->frame[0] = game_at(FRAME_OK + 0x50);
    b->frame[1] = game_at(FRAME_OK + 0x28);
    b->frame[2] = game_at(FRAME_OK);
    buffer_set_font(&b->base.buf, game_at(FONT_BUTTON), NULL, NULL, NULL);
    redraw(&b->base.win);
    hide(&b->base.win);
}

// Builds the window: the movie of the monuments is played into the picture, the pictures of the icons and of the factions are
// cut from their sheets, the places are laid out, and the buttons made (hidden). Answers non-zero when a picture can't be read.
static int UI_THIS monuwin_init(struct game_monuwin *mw) {
    struct game_buffer sheet;
    buffer_ctor(&sheet);
    uint8_t flic[FLIC_SIZE] __attribute__((aligned(4)));
    game_flic_ctor(flic);
    int result = 1;

    mw->w = 0x320, mw->h = 0x258;
    spot_init(&mw->spot, 0x40);
    mw->big = word(SMALL_SCREEN);
    struct monuwin_position pos;
    monuwin_position(&pos, mw->big != 0, *game_win_screen_w, *game_win_screen_h);
    if (!mw->big) {
        mw->x = 0, mw->y = 0;
        mw->clip.left = 0, mw->clip.top = 0;
        mw->clip.right = mw->w, mw->clip.bottom = mw->h;
    } else {
        mw->x = pos.offset_x, mw->y = pos.offset_y;
        mw->clip.left = pos.offset_x, mw->clip.top = pos.offset_y;
        mw->clip.right = mw->w + pos.offset_x, mw->clip.bottom = mw->h + pos.offset_y;
    }
    graphic_win_init(&mw->win, pos.x, pos.y, pos.w, pos.h, game_at(mw->big ? TITLE_BIG : TITLE_SMALL), 0x1000000, NULL, NULL, NULL);

    const struct font *font = font_in(FONT_TEXT);
    mw->line_h = monuwin_line_height(font->first, font->ascent, font->height);
    buffer_init(&mw->back, 0x400, 0x300, 0, NULL);
    if (game_flic_load(flic, &mw->back, text(S_SHEET_FLC), 0, 0, 0)) goto done;

    // The movie is played as many frames as there are monuments (thirteen of fourteen).
    *(int32_t *)(flic + FLIC_DELAY) = 10;
    for (int i = 0;; i++) {
        int count = word(MONU_COUNT);
        if (i >= (count == 14 ? 13 : count)) break;
        game_flic_frame(flic, *(int32_t *)(flic + FLIC_NEXT), *(int32_t *)(flic + FLIC_NEXT + 4));
    }
    if (buffer_load_pcx(&mw->zoom_back, text(word(MONU_COUNT) < 5 ? S_ZOOM_A : S_ZOOM_B), NULL, 10, 0xec)) goto done;

    struct monuwin_rects rects;
    monuwin_rects(&rects, mw->clip.left, mw->clip.top, word(MONU_COUNT) == 14);
    memcpy(mw->view, rects.view, sizeof mw->view);
    memcpy(mw->caption, rects.caption, sizeof mw->caption);
    memcpy(mw->zoom, rects.zoom, sizeof mw->zoom);
    memcpy(mw->slot, rects.slot, sizeof mw->slot);
    memcpy(mw->pair, rects.pair, sizeof mw->pair);
    memcpy(mw->face, rects.face, sizeof mw->face);
    memcpy(mw->portrait, rects.portrait, sizeof mw->portrait);

    // The seven factions' pictures: a sheet each (the faction's file name and "3.pcx"), cut into its large and small portrait,
    // and for the player's own faction its picture and leader as well.
    for (int f = 1; f <= 7; f++) {
        game_str_buffer[0] = 0;
        cat(def(f)->filename);
        cat(text(S_FACTION_END));
        if (buffer_load_pcx(&sheet, game_str_buffer, NULL, 10, 0xec)) goto done;
        if (f == *game_current_faction) {
            sprite_extract_2(&mw->own_picture, &sheet, key_color(), 0xa4, 0xb, 0xa0, 0x61, NULL);
            if (word(MONU_COUNT) == 14) sprite_extract_2(&mw->own_portrait, &sheet, key_color(), 0xa, 0xb, 0x8e, 0x65, NULL);
            else sprite_extract_2(&mw->own_portrait, &sheet, key_color(), 0xa, 0x71, 0x47, 0x40, NULL);
        }
        sprite_extract_2(&mw->face_big[f - 1], &sheet, key_color(), 0xa4, 0x6d, 0xa0, 0x61, NULL);
        sprite_extract_2(&mw->face_small[f - 1], &sheet, key_color(), 0xa, 0xb2, 0x3a, 0x32, NULL);
    }

    // The sheet of icons: the small ones, the large pictures, and the pictures of the wins.
    if (buffer_load_pcx(&sheet, text(S_ICONS), NULL, 10, 0xec)) goto done;
    for (int i = 0; i < MONU_KINDS; i++) {
        monuwin_icon_cell(i, &mw->x, &mw->y);
        sprite_extract_2(&mw->icon[i], &sheet, key_color(), mw->x, mw->y, 0x2c, 0x2a, NULL);
    }
    // (the working corner is put at the first picture's cell, and is left where the loop ends: the one after the last)
    mw->x = 1, mw->y = 0x89;
    for (int i = 0; i < MONU_KINDS; i++) {
        monuwin_big_cell(i, &mw->x, &mw->y);
        sprite_extract_2(&mw->big_pic[i], &sheet, key_color(), mw->x, mw->y, 0xa0, 0x61, NULL);
    }
    mw->x = 1, mw->y = 0x89 + 0x62 * 3;
    static const struct {
        int x, y, w, h;
    } pairs[MONU_PAIRS] = {{0x116, 0x41, 0x4e, 0x3f}, {0x165, 0x41, 0x49, 0x3f}, {1, 0x41, 0x8d, 0x3f},
                           {0x8f, 0x41, 0x86, 0x3f},  {1, 1, 0xcc, 0x3f},        {0xce, 1, 0xd5, 0x3f}};
    for (int i = 0; i < MONU_PAIRS; i++)
        sprite_extract_2(&mw->pair_pic[i], &sheet, key_color(), pairs[i].x, pairs[i].y, pairs[i].w, pairs[i].h, NULL);

    // The two buttons, one over the other at the foot of the window: OK for the list and the other for a monument zoomed.
    make_button(mw, &mw->list_btn, L_OK);
    make_button(mw, &mw->zoom_btn, L_OTHER);
    graphic_win_fill(&mw->win, 0);
    mw->zoomed = 0;
    mw->faction = *game_current_faction;
    result = 0;
done:
    flic_end(flic);
    buffer_dtor(&sheet);
    return result;
}

// Takes the window down: the modal loop released, the window closed.
static void UI_THIS monuwin_close(struct game_monuwin *mw) {
    release_modal(mw);
    graphic_win_close(&mw->win);
}

// ---- The words under a zoomed monument -----------------------------------------------------------------------

// The text variables the game's text lines are built from: numbers, and names with the grammar that goes with them. Each is a
// call of the game's own parse_num and parse_says, with the same defaults of gender and plural as the original sets before it.
static void say_num(int slot, int value) { game_parse_num(slot, value); }

// The leader's title or name of faction `who`, in the grammar of the leader's sex (the default), as the player's faction's
// leader's sex says it (the argument).
static void say_leader(const struct game_monuwin *mw, int slot, int who, int name) {
    *game_default_plural = 0;
    *game_default_gender = def(who)->leader_female;
    game_parse_says(slot, name ? def(who)->leader_name : def(who)->leader_title, def(mw->faction)->leader_female, -1);
}

// The faction's name (a noun, with its gender and number as the player's faction's own say them).
static void say_noun(const struct game_monuwin *mw, int slot, int who) {
    *game_default_gender = def(who)->noun_gender;
    *game_default_plural = def(who)->noun_plural;
    game_parse_says(slot, def(who)->noun, def(mw->faction)->noun_gender, def(mw->faction)->noun_plural);
}

static void say_adjective(int slot, int who) { game_parse_says(slot, def(who)->adjective, -1, -1); }

static void say_base(const struct game_monuwin *mw, int slot, int name) {
    game_parse_says(slot, monu(mw->faction)->names[name], -1, -1);
}

// The date of the same kind of monument of faction `who`.
static int when_of(int who, int kind) { return monu(who)->m[kind].when; }

// Fills the variables for the monument of kind `kind` and answers the label of its text in the file of them and the file's
// name, as addresses of the game's strings. 0 for a kind the original gives no text.
static void caption_words(struct game_monuwin *mw, int kind, uint32_t *label, uint32_t *file) {
    const struct monu_entry *m = &monu(mw->faction)->m[kind];
    int v = mw->faction;
    *label = *file = 0;
#define TEXT(l, f) (*label = (l), *file = (f))
    switch (kind) {
    case 0:
        say_num(0, m->when);
        say_leader(mw, 0, v, 0);
        say_leader(mw, 1, v, 1);
        say_base(mw, 2, 0);
        if (m->first) {
            TEXT(0x6863f4, 0x686404);
        } else {
            say_adjective(3, v);
            say_leader(mw, 4, m->other, 0);
            say_leader(mw, 5, m->other, 1);
            say_noun(mw, 6, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686410, 0x686420);
        }
        break;
    case 1:
        say_num(0, m->when);
        say_adjective(0, v);
        game_parse_says(1, tech_name(m->what, 0), -1, -1);
        if (m->first) {
            TEXT(0x68642c, 0x68643c);
        } else {
            say_noun(mw, 2, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686448, 0x686458);
        }
        break;
    case 2:
        say_num(0, m->when);
        say_adjective(0, v);
        if (m->first) {
            if (m->what + 1 == 0) {
                TEXT(0x686494, 0x6864a0);
            } else {
                say_num(1, m->what);
                if (m->extra + 1 == 0) {
                    TEXT(0x68647c, 0x686488);
                } else {
                    say_num(2, m->extra);
                    TEXT(0x686464, 0x686470);
                }
            }
        } else {
            say_noun(mw, 1, m->other);
            say_num(1, when_of(m->other, kind));
            if (m->what + 1 == 0) {
                TEXT(0x6864dc, 0x6864e8);
            } else {
                say_adjective(2, v);
                say_num(2, m->what);
                if (m->extra + 1 == 0) {
                    TEXT(0x6864c4, 0x6864d0);
                } else {
                    say_num(3, m->extra);
                    TEXT(0x6864ac, 0x6864b8);
                }
            }
        }
        break;
    case 3:
        say_num(0, m->when);
        if (m->first) {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_adjective(2, v);
            TEXT(0x6864f4, 0x686500);
        } else {
            say_adjective(0, v);
            say_leader(mw, 1, m->other, 0);
            say_leader(mw, 2, m->other, 1);
            say_noun(mw, 3, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x68650c, 0x686518);
        }
        break;
    case 4:
        say_num(0, m->when);
        say_adjective(0, v);
        if (m->first) {
            say_leader(mw, 1, v, 0);
            say_leader(mw, 2, v, 1);
            say_base(mw, 3, 1);
            TEXT(0x686524, 0x686530);
        } else {
            say_leader(mw, 1, m->other, 0);
            say_leader(mw, 2, m->other, 1);
            say_adjective(3, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x68653c, 0x686548);
        }
        break;
    case 5:
        say_num(0, m->when);
        if (m->first) {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            game_parse_says(2, game_strings_get(game_strings, (char *)(uintptr_t)word(SECRETS + 0x30 * m->what)), -1, -1);
            TEXT(0x686554, 0x686560);
        } else {
            say_adjective(0, v);
            game_parse_says(1, game_strings_get(game_strings, (char *)(uintptr_t)word(SECRETS + 0x30 * m->what)), -1, -1);
            say_leader(mw, 2, m->other, 0);
            say_leader(mw, 3, m->other, 1);
            say_noun(mw, 4, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x68656c, 0x686578);
        }
        break;
    case 6:
        say_num(0, m->when);
        say_leader(mw, 0, v, 0);
        say_leader(mw, 1, v, 1);
        if (m->first) {
            say_leader(mw, 2, m->what, 0);
            say_leader(mw, 3, m->what, 1);
            say_noun(mw, 4, m->what);
            TEXT(0x686584, 0x686594);
        } else {
            say_adjective(2, v);
            say_leader(mw, 3, m->other, 0);
            say_leader(mw, 4, m->other, 1);
            say_noun(mw, 5, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x6865a0, 0x6865b0);
        }
        break;
    case 7:
        say_num(0, m->when);
        say_leader(mw, 0, v, 0);
        say_leader(mw, 1, v, 1);
        if (m->first) {
            say_noun(mw, 2, v);
            say_base(mw, 3, 2);
            TEXT(0x6865bc, 0x6865c8);
        } else {
            say_base(mw, 2, 2);
            say_adjective(3, v);
            say_leader(mw, 4, m->other, 0);
            say_leader(mw, 5, m->other, 1);
            say_noun(mw, 6, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x6865d4, 0x6865e0);
        }
        break;
    case 8:
        say_num(0, m->when);
        if (m->first) {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_noun(mw, 2, v);
            TEXT(0x6865ec, 0x6865f4);
        } else {
            say_adjective(0, v);
            say_leader(mw, 1, m->other, 0);
            say_leader(mw, 2, m->other, 1);
            say_noun(mw, 3, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686600, 0x686608);
        }
        break;
    case 9:
        say_num(0, m->when);
        if (m->first) {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_noun(mw, 2, v);
            TEXT(0x686614, 0x68661c);
        } else {
            say_adjective(0, v);
            say_leader(mw, 1, m->other, 0);
            say_leader(mw, 2, m->other, 1);
            say_noun(mw, 3, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686628, 0x686630);
        }
        break;
    case 10:
        say_num(0, m->when);
        if (m->first) {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_adjective(2, v);
            TEXT(0x68663c, 0x686648);
        } else {
            say_adjective(0, v);
            say_leader(mw, 1, m->other, 0);
            say_leader(mw, 2, m->other, 1);
            say_noun(mw, 3, m->other);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686654, 0x686660);
        }
        break;
    case 11:
        say_num(0, m->when);
        if (m->first) {
            say_noun(mw, 0, v);
            say_leader(mw, 1, v, 0);
            say_leader(mw, 2, v, 1);
            TEXT(0x68666c, 0x686678);
        } else {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_adjective(2, v);
            say_noun(mw, 3, m->other);
            say_leader(mw, 4, m->other, 0);
            say_leader(mw, 5, m->other, 1);
            say_num(1, when_of(m->other, kind));
            TEXT(0x686684, 0x686690);
        }
        break;
    case 12:
        say_num(0, m->when);
        say_leader(mw, 0, v, 0);
        say_leader(mw, 1, v, 1);
        if (m->first) {
            say_noun(mw, 2, v);
            TEXT(0x68669c, 0x6866a8);
        } else {
            say_adjective(2, v);
            say_leader(mw, 3, m->other, 0);
            say_leader(mw, 4, m->other, 1);
            say_num(1, when_of(m->other, kind));
            TEXT(0x6866b4, 0x6866c0);
        }
        break;
    case 13:
        say_num(0, m->when);
        if (m->what + 1 != 0) {
            say_leader(mw, 0, m->what, 0);
            say_leader(mw, 1, m->what, 1);
            say_noun(mw, 2, m->what);
            say_noun(mw, 3, m->what);
            TEXT(0x6866cc, 0x6866dc);
        } else {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_adjective(2, v);
            TEXT(0x6866e8, 0x6866f4);
        }
        break;
    case 14:
        say_num(0, m->when);
        if (m->what + 1 != 0) {
            say_leader(mw, 0, m->what, 0);
            say_leader(mw, 1, m->what, 1);
            say_noun(mw, 2, m->what);
            say_noun(mw, 3, m->what);
            TEXT(0x686700, 0x686710);
        } else {
            say_leader(mw, 0, v, 0);
            say_leader(mw, 1, v, 1);
            say_noun(mw, 2, v);
            TEXT(0x68671c, 0x68672c);
        }
        break;
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        say_num(0, m->when);
        say_noun(mw, 0, v);
        say_noun(mw, 1, m->what);
        say_leader(mw, 2, m->what, 0);
        // (the name of the faction beaten, with the grammar of its own leader and a plural of none)
        *game_default_gender = def(m->what)->leader_female;
        *game_default_plural = 0;
        game_parse_says(3, def(m->what)->leader_name, def(mw->faction)->leader_female, -1);
        TEXT(0x686738, 0x686744);
        break;
    default:
        break;
    }
#undef TEXT
}

// ---- Drawing -----------------------------------------------------------------------------------------------

// A string list the words are read into, built on the stack as the original builds it: a list with its virtual base, which
// keeps the game's list head and clears it, and gives it back when the list is closed.
enum { SL_VTABLE = 0x6698c4, SL_PART_VTABLE = 0x6698c0, SL_BASE_VTABLE = 0x6693a4, SL_BASE_PART = 0x6693a0, SL_VBPTR = 0x66b0ec,
       SL_VB_VTABLE = 0x6693ac, SL_HEAD = 0x9b3374 };

static void list_set_part(uint8_t *o, uint32_t vtable) {
    const int32_t *vbtable = (const int32_t *)(uintptr_t)((struct game_strlist *)o)->vbptr;
    *(uint32_t *)(o + 4 + vbtable[1]) = vtable;
}

static void list_open(uint8_t *sl) {
    memset(sl, 0, 0x40);
    uint32_t *head = game_at(SL_HEAD);
    *(uint32_t *)(sl + 4) = game_va(SL_VBPTR);
    *(uint32_t *)(sl + 0x28) = game_va(SL_VB_VTABLE);
    *(uint32_t *)(sl + 0x2c) = *head;
    *head = 0;
    strlist_ctor((struct game_strlist *)sl, 0);
    *(uint32_t *)sl = game_va(SL_VTABLE);
    list_set_part(sl, game_va(SL_PART_VTABLE));
}

static void list_close(uint8_t *sl) {
    struct game_strlist *l = (struct game_strlist *)sl;
    *(uint32_t *)sl = game_va(SL_VTABLE);
    list_set_part(sl, game_va(SL_PART_VTABLE));
    strlist_remove_all(l);
    l->idx = 0;
    *(uint32_t *)sl = game_va(SL_BASE_VTABLE);
    list_set_part(sl, game_va(SL_BASE_PART));
    strlist_remove_all(l);
    l->idx = 0;
    *(uint32_t *)game_at(SL_HEAD) = *(uint32_t *)(sl + 0x2c);
}

static void draw_sprite(struct game_monuwin *mw, struct game_sprite *s, int x, int y) {
    sprite_draw_3(s, &mw->buf, s->key, x, y);
}

// The picture the monument of `kind` is shown by: the faction's own for 13, the one of the faction it is of for the wins, else
// its own big picture.
static struct game_sprite *picture_of(struct game_monuwin *mw, int kind) {
    if (kind == KIND_OWN_PICTURE) return &mw->own_picture;
    if (kind < MONU_KINDS) return &mw->big_pic[kind];
    // (the portraits are numbered from 1, and the original steps back over the one before the first)
    return (struct game_sprite *)((uint8_t *)mw + offsetof(struct game_monuwin, face_big) - sizeof(struct game_sprite)) +
           monu(mw->faction)->m[kind].what;
}

static struct game_sprite *small_face_of(struct game_monuwin *mw, int faction) {
    return (struct game_sprite *)((uint8_t *)mw + offsetof(struct game_monuwin, face_small) - sizeof(struct game_sprite)) + faction;
}

static void spare_from(struct game_monuwin *mw, const int32_t *r) {
    mw->spare.left = r[0], mw->spare.top = r[1], mw->spare.right = r[2], mw->spare.bottom = r[3];
}

// The listed view: the picture behind, the icons of the monuments in the order of their dates (each one a place a click or the
// mouse finds), the picture of the one last shown, the pictures of the wins and the beaten factions, and the leader.
static void draw_list(struct game_monuwin *mw) {
    struct game_buffer *b = &mw->buf;
    spot_kill_type(&mw->spot, 0);
    if (mw->big) buffer_copy_full(&mw->back, b, 0, 0, 0, 0, 0x400, 0x300);
    else buffer_copy_full(&mw->back, b, 0x70, 0x54, 0, 0, 0x320, 0x258);

    struct monu_table *t = monu(mw->faction);
    int order[MONU_KINDS], when[MONU_KINDS];
    for (int k = 0; k < MONU_KINDS; k++) {
        order[k] = t->m[k].used ? k : MONU_NO_KIND;
        when[k] = t->m[k].when;
    }
    monuwin_sort(order, when);
    for (int i = 0; i < MONU_KINDS; i++) {
        int k = order[i];
        if (k == MONU_NO_KIND || !monu(mw->faction)->m[k].used) continue;
        // (the original reads a fifteenth place past the table of fourteen, which is the first of the pictures of the wins)
        spare_from(mw, (const int32_t *)((uint8_t *)mw + offsetof(struct game_monuwin, slot) + 16 * i));
        draw_sprite(mw, &mw->icon[k], mw->spare.left, mw->spare.top);
        spot_add(&mw->spot, k, 0, mw->spare.left, mw->spare.top, mw->spare.right - mw->spare.left, mw->spare.bottom - mw->spare.top);
    }

    int kind = t->shown;
    mw->last = kind;
    spot_add(&mw->spot, ID_VIEW, 0, mw->view[0], mw->view[1], mw->view[2] - mw->view[0], mw->view[3] - mw->view[1]);
    draw_sprite(mw, picture_of(mw, kind), mw->view[0], mw->view[1]);

    int count = 0;
    for (int k = MONU_KINDS; k < MONU_ALL_KINDS; k++)
        if (monu(mw->faction)->m[k].used) count++;
    if (count) {
        int slot = monuwin_pair_slot(count, 0);
        spare_from(mw, mw->pair[slot]);
        draw_sprite(mw, &mw->pair_pic[slot], mw->spare.left, mw->spare.top);
        if (count - 1 != 0) {
            slot = monuwin_pair_slot(count, 1);
            spare_from(mw, mw->pair[slot]);
            draw_sprite(mw, &mw->pair_pic[slot], mw->spare.left, mw->spare.top);
        }
        // The beaten factions: the walk reads up to twice as many kinds as there are.
        int used[2 * MONU_FACE_KINDS], faces[MONU_FACE_KINDS], places[MONU_FACE_KINDS];
        for (int e = 0; e < 2 * MONU_FACE_KINDS; e++)
            used[e] = ((const struct monu_entry *)((const uint8_t *)monu(mw->faction) + 0x38 * (MONU_KINDS + e)))->used != 0;
        int n = monuwin_face_walk(count, used, faces, places);
        for (int i = 0; i < n; i++) {
            int32_t *r = mw->face[places[i]];
            int what = ((const struct monu_entry *)((const uint8_t *)monu(mw->faction) + 0x38 * faces[i]))->what;
            draw_sprite(mw, small_face_of(mw, what), r[0], r[1]);
            spot_add(&mw->spot, faces[i], 0, r[0], r[1], r[2] - r[0], r[3] - r[1]);
        }
    }
    draw_sprite(mw, &mw->own_portrait, mw->portrait[0], mw->portrait[1]);
    hide(&mw->zoom_btn.base.win);
    show(&mw->list_btn.base.win);
}

// The zoomed view: the picture behind, the words of the monument (read from the file of them by the game's list loader), and
// the monument's picture cut from a sheet by its kind.
static void draw_zoom(struct game_monuwin *mw, uint8_t *sl) {
    struct game_buffer *b = &mw->buf;
    struct game_buffer pic;
    buffer_ctor(&pic);
    if (mw->big) buffer_copy_full(&mw->zoom_back, b, 0, 0, 0, 0, 0x400, 0x300);
    else buffer_copy_full(&mw->zoom_back, b, 0x70, 0x54, 0, 0, 0x320, 0x258);
    game_str_buffer[0] = 0;
    int kind = monu(mw->faction)->shown;

    uint32_t label = 0, file = 0;
    caption_words(mw, kind, &label, &file);
    if (file) game_stringlist_load(sl, text(file), text(label), 1, 0);

    buffer_set_clip(b, mw->caption);
    buffer_write_strings(b, (struct str_list *)sl, mw->caption[0], mw->caption[1], mw->caption[2] - mw->caption[0], 0);
    buffer_set_clip(b, b->full);

    game_str_buffer[0] = 0;
    int x, y;
    int which = monuwin_zoom_picture(kind, &x, &y);
    if (which == 0) {
        cat(def(mw->faction)->filename);
        cat(text(S_OWN_END));
    } else if (which == 1) {
        cat(def(monu(mw->faction)->m[kind].what)->filename);
        cat(text(S_OTHER_END));
    } else {
        char number[16];
        cat(text(S_BIG_START));
        snprintf(number, sizeof number, "%d", kind);
        cat(number);
        cat(text(S_BIG_END));
    }
    buffer_load_pcx(&pic, game_str_buffer, NULL, 10, 0xec);
    buffer_copy_full(&pic, b, x, y, mw->zoom[0], mw->zoom[1], 0x258, 0x168);
    hide(&mw->list_btn.base.win);
    show(&mw->zoom_btn.base.win);
    buffer_dtor(&pic);
}

static void UI_THIS monuwin_on_redraw(struct game_monuwin *mw) {
    uint8_t sl[0x40] __attribute__((aligned(4)));
    list_open(sl);
    struct game_buffer *b = &mw->buf;
    buffer_set_font(b, font_in(FONT_TEXT), NULL, NULL, NULL);
    buffer_set_text_color(b, COLOR_TEXT, -1, 1, 1);
    mw->faction = *game_current_faction;
    buffer_set_clip(b, b->full);
    if (mw->zoomed) draw_zoom(mw, sl);
    else draw_list(mw);
    list_close(sl);
}

// ---- The mouse and the keys --------------------------------------------------------------------------------

// A click on an icon zooms it (a click on the picture shown zooms that one); a click while zoomed goes back to the list.
static void UI_THIS monuwin_on_left_click(struct game_monuwin *mw, int x, int y) {
    if (net_blocks()) return;
    if (mw->zoomed) {
        mw->zoomed = 0;
        redraw(mw);
        return;
    }
    int id, kind;
    if (spot_check(&mw->spot, x, y, &id, &kind) + 1 == 0) return;
    if (id == ID_VIEW) id = mw->last;
    monu(*game_current_faction)->shown = id;
    mw->last = id;
    mw->zoomed = 1;
    redraw(mw);
}

// The mouse over an icon draws the picture of that monument in the view (the list stays); nothing while zoomed.
static void UI_THIS monuwin_on_mouse_move(struct game_monuwin *mw, int x, int y) {
    if (mw->zoomed) return;
    if (net_blocks()) return;
    int id, kind;
    if (spot_check(&mw->spot, x, y, &id, &kind) + 1 == 0) return;
    if (id == ID_VIEW) return;
    draw_sprite(mw, picture_of(mw, id), mw->view[0], mw->view[1]);
    mw->last = id;
    graphic_win_soft_update_2(&mw->win);
}

static void UI_THIS monuwin_on_button_clicked(struct game_monuwin *mw, int id) {
    if (id != ID_BUTTON) return;
    if (mw->zoomed) {
        mw->zoomed = 0;
        redraw(mw);
        return;
    }
    release_modal(mw);
    graphic_win_close(&mw->win);
}

static int UI_THIS monuwin_on_key_click(struct game_monuwin *mw, int key, int mods) {
    (void)key, (void)mods;
    if (mw->zoomed) {
        mw->zoomed = 0;
        redraw(mw);
        return 1;
    }
    release_modal(mw);
    graphic_win_close(&mw->win);
    return 1;
}

// ---- monument ------------------------------------------------------------------------------------------------

// Opens the window for the player's faction, as a modal one (it answers when the window is closed): not when the faction's
// rules forbid monuments (a bit of its rule flags), in a network game, in some kinds of game (the preference bit, and when
// asked to show the window, two of the game's state bits), or when the faction has none. `zoom` starts the window on the
// kind last shown instead of the list.
static void UI_CDECL monument(int zoom) {
    int f = *game_current_faction;
    if (def(f)->rule_flags & 0x80) return;
    if (*game_net_multiplayer) return;
    if (zoom) {
        if (word(MORE_PREFS) & 0x1000000) return;
        uint8_t s = *(uint8_t *)game_at(STATE_BYTE);
        if (s & 0x10) return;
        if ((s & 1) && (s & 8)) return;
    }
    int32_t *count = game_at(MONU_COUNT);
    *count = 0;
    int n = 0;
    for (int k = 0; k < MONU_KINDS; k++)
        if (monu(f)->m[k].used) n++;
    *count = n;
    if (!n) return;
    struct game_monuwin *mw = the_monuwin();
    if (monuwin_init(mw)) return;
    mw->zoomed = zoom != 0;
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), mw, VT_SET_MODAL, 0, 0, 0);
    graphic_win_close(&mw->win);
    game_do_all_draws();
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook monuwin_hooks[] = {
    UI_HOOK(0x472230, monuwin_init, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // push ebp; mov ebp, esp; mov eax, fs:0
    UI_HOOK(0x4736e0, monuwin_on_redraw, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x23, 0x62, 0x65, 0x00),  // push -1; push handler
    UI_HOOK(0x476760, monuwin_close, 0x56, 0x8b, 0xf1, 0x8b, 0x06),  // push esi; mov esi, ecx; mov eax, [esi]
    UI_HOOK(0x476780, monuwin_on_left_click, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x84, 0x92, 0x93, 0x00),
    UI_HOOK(0x476850, monuwin_on_mouse_move, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x4c, 0x0a, 0x00, 0x00),
    UI_HOOK(0x4769b0, monuwin_on_button_clicked, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x56),  // push ebp; mov ebp, esp; mov eax, [ebp+8]; push esi
    UI_HOOK(0x476a00, monuwin_on_key_click, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x4c, 0x0a, 0x00, 0x00),
    UI_HOOK(0x476a50, monument, 0x55, 0x8b, 0xec, 0xa1, 0x84, 0x92, 0x93, 0x00, 0x56),  // push ebp; mov ebp, esp; mov eax, [0x939284]; push esi
    UI_HOOK(0x477b10, monuwin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x74, 0x60, 0x65, 0x00),
    UI_HOOK(0x477c80, monuwin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x54, 0x61, 0x65, 0x00),
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x477dd0, monuwin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0x35, 0xfd, 0xff, 0xff),
    UI_HOOK(0x477e00, monuwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module monuwin_module = {"monuwin", monuwin_hooks, UI_HOOK_COUNT(monuwin_hooks), NULL, NULL, -1};
