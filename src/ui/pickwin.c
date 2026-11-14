// The faction picker, the game's PickWin: the window config_game opens on the way into a new game to choose the
// player's faction (and, for each of the seven faction rows, which faction stands there). It is one global object
// (0x822718) built by a static initializer and destroyed by an atexit handler, and config_game is the only
// code outside the class that touches it: it calls init and show_window, closes it, reads the answer and the
// action from its fields, and sets up its list box.
//
// The window shows three pictures that choose a group of factions (the game's own, the Alien Crossfire ones, and a
// group of custom ones listed in the rules file), a list of the seven factions of the group, two radio buttons
// (choose from the list, or take one at random) with lines drawn to the list row they stand for, a small button
// beside each list row that opens a menu to replace that row's faction, and the buttons Cancel and OK and two
// more whose meaning config_game gives (they are returned as the action).
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=pickwin gives them all back. The object keeps the original's layout, the toolkit is called directly,
// and what it draws, in what order, and what it writes are the original's. Static initializers run after
// hooks_apply, so the constructor and destructor are ours too.
//
// Differences from the original that can't be seen: the popup menu object is built when a row's button is clicked
// (the original builds and unbuilds it on every button click), and the exception frames are not reproduced.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "pickwin.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufimg.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/dialog.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/rules.h"
#include "../engine/scroll.h"
#include "../engine/sprite.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- The object -------------------------------------------------------------------------------------------

enum { PICKWIN_BASE = 0x822718 };

// The buttons' ids. A click on one comes to on_button_clicked; Cancel and OK are also what Escape and Return click.
enum {
    ID_PACK_FACTIONS = 0x100,  // the three pictures
    ID_PACK_NEW,
    ID_PACK_CUSTOM,
    ID_CANCEL,
    ID_CHOOSE,  // the radio buttons
    ID_RANDOM,
    ID_A,  // two buttons config_game answers: their id is the action
    ID_B,
    ID_OK,
    ID_ROW_BASE = 0x3039,  // the small buttons beside the list rows: this plus the row (0 to 6)
};
enum { ROWS = 7, PACKS = 3, MAX_CUSTOM = 10, FACTION_FILES = 2 * ROWS + MAX_CUSTOM, NAME_LEN = 0x18 };
// The picture of a group is 0x102 by 0x5a; a row button's, 0x10 square.
enum { PACK_W = 0x102, PACK_H = 0x5a, ROW_BUTTON = 0x10 };

struct game_pickwin {
    struct game_win win;       // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;    // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t x, y, w, h;        // 0xa14: the numbers the original lays out with; what they are left holding is state
    uint8_t popup[0x537c];     // 0xa24: a Popup that is built and unbuilt with the window and not used
    int32_t kind;              // 0x5da0: how config_game opened it; 2 is a choice among the factions still in the game
    int32_t action;            // 0x5da4: what ended the window: 0, or the id of the button (random, or one config_game answers)
    int32_t chosen;            // 0x5da8: the faction chosen, or -1 for none (Cancel)
    int32_t flag;              // 0x5dac: config_game's switch, which the label of button B follows
    int32_t n_custom;          // 0x5db0: how many custom factions the rules file lists (at most ten)
    int32_t saved_pick;        // 0x5db4: the list's choice, kept while Random is on
    char file[FACTION_FILES][NAME_LEN];   // 0x5db8: the faction files: the game's seven, the seven new, the custom ones
    char title[FACTION_FILES][NAME_LEN];  // 0x5ff8: each one's name as its file gives it, for the replace menu
    void(UI_CDECL *on_pick)(int faction);  // 0x6238: told which faction the list has chosen
    struct ui_rect frame;                  // 0x623c
    struct ui_rect header;                 // 0x624c
    struct ui_rect pack_rect[PACKS];       // 0x625c
    struct game_flat_button pack_btn[PACKS];  // 0x628c: ids 0x100 to 0x102
    struct game_sprite pack_pic[PACKS][3];    // 0x8470: each picture's three looks (as it lies, changed, pressed)
    struct game_sprite row_pic[3];            // 0x85fc: the row buttons' looks
    struct game_sprite marker;                // 0x8680: the arrow drawn at the chosen row
    struct ui_rect list_rect;                 // 0x86ac
    uint8_t list[0xb54];                      // 0x86bc: a ListBox (see scroll.c)
    struct ui_rect button_rect[6];            // 0x9210
    struct game_flat_button button[6];        // 0x9270: A, B, Cancel, Choose, Random, OK (the layout's order)
    struct ui_rect row_rect[ROWS];            // 0xd638
    struct game_flat_button row_btn[ROWS];    // 0xd6a8: ids 0x3039 and up
    struct game_button_group group;           // 0x125bc: Choose and Random, one of them chosen
};
GAME_AT(struct game_pickwin, x, 0xa14);
GAME_AT(struct game_pickwin, popup, 0xa24);
GAME_AT(struct game_pickwin, kind, 0x5da0);
GAME_AT(struct game_pickwin, action, 0x5da4);
GAME_AT(struct game_pickwin, chosen, 0x5da8);
GAME_AT(struct game_pickwin, flag, 0x5dac);
GAME_AT(struct game_pickwin, n_custom, 0x5db0);
GAME_AT(struct game_pickwin, saved_pick, 0x5db4);
GAME_AT(struct game_pickwin, file, 0x5db8);
GAME_AT(struct game_pickwin, title, 0x5ff8);
GAME_AT(struct game_pickwin, on_pick, 0x6238);
GAME_AT(struct game_pickwin, frame, 0x623c);
GAME_AT(struct game_pickwin, header, 0x624c);
GAME_AT(struct game_pickwin, pack_rect, 0x625c);
GAME_AT(struct game_pickwin, pack_btn, 0x628c);
GAME_AT(struct game_pickwin, pack_pic, 0x8470);
GAME_AT(struct game_pickwin, row_pic, 0x85fc);
GAME_AT(struct game_pickwin, marker, 0x8680);
GAME_AT(struct game_pickwin, list_rect, 0x86ac);
GAME_AT(struct game_pickwin, list, 0x86bc);
GAME_AT(struct game_pickwin, button_rect, 0x9210);
GAME_AT(struct game_pickwin, button, 0x9270);
GAME_AT(struct game_pickwin, row_rect, 0xd638);
GAME_AT(struct game_pickwin, row_btn, 0xd6a8);
GAME_AT(struct game_pickwin, group, 0x125bc);
_Static_assert(sizeof(struct game_pickwin) == 0x12650, "layout of the game's PickWin");

// The slots of the window's vtable the screen uses (byte offsets).
enum { VT_SHOW = 0x4, VT_HIDE = 0x8, VT_BUTTON_CLICKED = 0xb0, VT_SET_MODAL = 0xe4, VT_RELEASE_MODAL = 0xe8, VT_REDRAW = 0xf8 };
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66d13c, VTABLE_BUF = 0x66d134 };

static struct game_pickwin *the_pickwin(void) { return game_at(PICKWIN_BASE); }

// ---- The game's data it reads (by address) -----------------------------------------------------------------

enum {
    TEXT_FILE = 0x691afc,  // char *: the rules file's name
    FACTION_PICK = 0x94b354,  // config_game's current choice of faction
    FONT = 0x7cfe8c,          // the Font of the buttons and the window
    COLOR_TEXT = 0x8c6c6c,    // two words: the window's text colour
    COLORS_ITEM = 0x8c6c9c,   // per list item look: two words each, stepping down from here
    FRAME_FRAME = 0x78d690,   // box frames: the window's, the header's, the header's inner one
    FRAME_HEADER = 0x78d5a0,
    FRAME_INNER = 0x78d618,
    FRAMES_ITEM = 0x78d960,   // a frame per list item look, 0x28 bytes each
    EMPTY_TITLE = 0x834d6c,   // the (empty) title the window is made with
};
// The rules file's sections and the picture.
enum {
    SECTION_FACTIONS = 0x686a8c,
    SECTION_NEW = 0x686a98,
    SECTION_CUSTOM_COUNT = 0x686aa4,
    SECTION_CUSTOM = 0x686ab4,
    PICTURE = 0x686ac4,
    SECTION_FACTIONS_AGAIN = 0x686ad0,  // (the same text as the first, where the buttons use it)
    SECTION_NEW_AGAIN = 0x686adc,
    BLANK_FILE = 0x686ae8,   // the file name of an empty faction slot, "JENN282", and where it is used
    BLANK_KEY = 0x686af0,
    BLANK_NAME = 0x686af8,
    SEPARATOR = 0x686b00,    // what comes between the name and the description in a list line
    BLANK_FILE_CHOSEN = 0x686b04,
    BLANK_KEY_CHOSEN = 0x686b0c,
    OPEN_PAREN = 0x682e9c,
    CLOSE_PAREN = 0x682e98,
};
// Entries of the labels file.
enum {
    LABEL_TITLE = 0x45a,
    LABEL_PACK = 0x45b,  // and the next two
    LABEL_BUTTON_A = 0x45e,
    LABEL_BUTTON_B_ON = 0x45f,
    LABEL_BUTTON_B_OFF = 0x2af,
    LABEL_CANCEL = 0x66,
    LABEL_OK = 0x65,
    LABEL_RANDOM = 0x460,
    LABEL_CHOOSE = 0x461,
    LABEL_EMPTY_SLOT = 0x462,
};

static char *text(uint32_t addr) { return game_at(addr); }
static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static struct font *font(void) { return game_at(FONT); }
static const struct game_box_frame *frame_at(uint32_t addr) { return game_at(addr); }
static char *rules_file(void) { return *(char **)game_at(TEXT_FILE); }

// ---- Layout ------------------------------------------------------------------------------------------------

static struct ui_rect rect_of(int left, int top, int width, int height) {
    return (struct ui_rect){left, top, left + width, top + height};
}

// The original lays the window out through its working numbers (x, y, width, height), in this order, and the
// values are copied here: the pictures are 0x102 by 0x5a, a third of the header's width less a margin; the list
// stands to their right; the buttons run along the bottom and down the left.
void pickwin_layout(struct pickwin_layout *l, int width, int height) {
    enum { LIST_H = 0x88, ROW_PITCH = 0x13, BUTTON_H = 0x16 };
    l->frame = (struct ui_rect){0, 0, width, height};
    l->header = (struct ui_rect){4, 4, width - 4, height - 4};
    l->header.bottom = l->header.top + 0x1e;

    int x = l->header.left + 1, y = l->header.bottom + 4;
    int third = (l->header.right - l->header.left - 6) / 3;
    for (int i = 0; i < PACKS; i++) {
        l->pack[i] = rect_of(x, y, PACK_W, PACK_H);
        x = l->pack[i].right + 2;
    }

    // The list: right of the pictures' column and under them; its margin of 3 taken, then shifted left by 0x12 to leave
    // room for the row buttons.
    l->list = (struct ui_rect){l->header.left + 0x15b, l->pack[0].bottom + 3, l->header.right - 3, 0};
    l->list.bottom = l->list.top + LIST_H;
    l->list = (struct ui_rect){l->list.left + 3 - 0x12, l->list.top + 3, l->list.right - 3 - 0x12, l->list.bottom - 3};
    for (int i = 0; i < ROWS; i++) l->row[i] = rect_of(l->list.right + 2, l->list.top + ROW_PITCH * i, ROW_BUTTON, ROW_BUTTON);

    // Along the bottom, left to right: B, A, OK, Cancel; each three quarters of a picture's third less one wide.
    int w = 3 * third / 4 - 1;
    y = l->list.bottom + 7;
    l->button[PW_BTN_B] = rect_of(l->header.left + 1, y, w, BUTTON_H);
    l->button[PW_BTN_A] = rect_of(l->button[PW_BTN_B].right + 2, y, w, BUTTON_H);
    l->button[PW_BTN_OK] = rect_of(l->button[PW_BTN_A].right + 2, y, w, BUTTON_H);
    l->button[PW_BTN_CANCEL] = rect_of(l->button[PW_BTN_OK].right + 2, y, w, BUTTON_H);

    // The radio buttons: at the left of the list, one above the other.
    x = l->header.left + 2, y = l->list.top - 2, w = 0xd8;
    l->button[PW_BTN_CHOOSE] = rect_of(x, y, w, BUTTON_H);
    y += 0x19;
    l->button[PW_BTN_RANDOM] = rect_of(x, y, w, BUTTON_H);
    l->last_x = x, l->last_y = y, l->last_w = w, l->last_h = BUTTON_H;
}

void pickwin_item_text(char *out, const char *formal_name, const char *description, const char *separator, const char *open,
                       const char *close) {
    strcpy(out, formal_name);
    strcat(out, separator);
    strcat(out, open);
    strcat(out, description);
    strcat(out, close);
}

int pickwin_row_y(int top, int row) { return top + 19 * row; }

// ---- Parts -------------------------------------------------------------------------------------------------

static struct game_win *button_win(struct game_flat_button *b) { return &b->base.win; }

// The list box's window and dialog, which its vbtable places.
static uint8_t *list_part(struct game_pickwin *pw, int entry) { return pw->list + ((int32_t *)*(void **)pw->list)[entry]; }
static struct game_win *list_win(struct game_pickwin *pw) { return (struct game_win *)list_part(pw, 1); }
static struct game_dlg *list_dlg(struct game_pickwin *pw) { return (struct game_dlg *)list_part(pw, 2); }
// The Dialog part of a ListBox lies 0xa60 in; the list's attach takes it from there.
enum { LISTBOX_DIALOG = 0xa60 };

// The Dialog fields the picker sets for its list.
enum { DLG_FLAGS = 0x20, DLG_MARGIN = 0x40, DLG_GAP = 0x44, DLG_ROW_HEIGHT = 0x48, DLG_ITEM_COUNT = 0xb4, DLG_ATTACH_A = 0x24, DLG_ATTACH_B = 0x28 };

static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, 0); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(struct game_pickwin *pw) { UI_VCALL(void(UI_THIS *)(void *), pw, VT_RELEASE_MODAL); }

// What the list should show chosen: the player's faction in a choice among those in the game, else config_game's choice.
static int current_pick(const struct game_pickwin *pw) { return pw->kind & 2 ? *game_current_faction : word(FACTION_PICK); }

// ---- Construction ------------------------------------------------------------------------------------------

static void *UI_THIS pickwin_ctor(struct game_pickwin *pw) {
    graphic_win_ctor(&pw->win);
    game_popup_ctor(pw->popup);
    for (int i = 0; i < PACKS; i++) flat_button_ctor(&pw->pack_btn[i]);
    for (int i = 0; i < PACKS; i++)
        for (int look = 0; look < 3; look++) sprite_ctor(&pw->pack_pic[i][look]);
    for (int look = 0; look < 3; look++) sprite_ctor(&pw->row_pic[look]);
    sprite_ctor(&pw->marker);
    listbox_ctor(pw->list, 1);
    for (int i = 0; i < 6; i++) flat_button_ctor(&pw->button[i]);
    for (int i = 0; i < ROWS; i++) flat_button_ctor(&pw->row_btn[i]);
    button_group_ctor(&pw->group);
    pw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return pw;
}

static void UI_THIS pickwin_dtor(struct game_pickwin *pw) {
    button_group_close(&pw->group);
    for (int i = ROWS - 1; i >= 0; i--) flat_button_dtor(&pw->row_btn[i]);
    for (int i = 5; i >= 0; i--) flat_button_dtor(&pw->button[i]);
    listbox_dtor(list_part(pw, 1));
    dialog_dtor((struct game_dialog *)list_part(pw, 2));
    graphic_win_dtor(list_win(pw));
    sprite_close(&pw->marker);
    for (int look = 2; look >= 0; look--) sprite_close(&pw->row_pic[look]);
    for (int i = PACKS - 1; i >= 0; i--)
        for (int look = 2; look >= 0; look--) sprite_close(&pw->pack_pic[i][look]);
    for (int i = PACKS - 1; i >= 0; i--) flat_button_dtor(&pw->pack_btn[i]);
    game_popup_dtor(pw->popup);
    graphic_win_dtor(&pw->win);
}

static void *UI_THIS pickwin_scalar_dtor(struct game_pickwin *pw, int flags) {
    pickwin_dtor(pw);
    if (flags & 1) game_free(pw);
    return pw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS pickwin_operator_delete(struct game_buffer *buf, int flags) {
    return pickwin_scalar_dtor((struct game_pickwin *)((uint8_t *)buf - offsetof(struct game_pickwin, buf)), flags);
}

// ---- Opening the window ------------------------------------------------------------------------------------

// Reads the names of the factions in the three groups: the file name of each, then each one's own name from its file.
// Answers non-zero when the rules file or a faction file can't be read.
static int read_faction_names(struct game_pickwin *pw) {
    char *rules = rules_file();
    if (game_text_open(rules, text(SECTION_FACTIONS))) return 1;
    for (int i = 0; i < ROWS; i++) game_text_get(), strcpy(pw->file[i], game_text_item());
    if (game_text_open(rules, text(SECTION_NEW))) return 1;
    for (int i = 0; i < ROWS; i++) game_text_get(), strcpy(pw->file[ROWS + i], game_text_item());

    // The custom factions: counted, then read again.
    pw->n_custom = 0;
    if (game_text_open(rules, text(SECTION_CUSTOM_COUNT))) return 1;
    game_text_get();
    while (*game_text_item()) pw->n_custom++, game_text_get();
    if (game_text_open(rules, text(SECTION_CUSTOM))) return 1;
    if (pw->n_custom > MAX_CUSTOM) pw->n_custom = MAX_CUSTOM;
    for (int i = 0; i < pw->n_custom; i++) game_text_get(), strcpy(pw->file[2 * ROWS + i], game_text_item());

    // Each faction's own name is the third item of the first line of its file.
    for (int i = 0; i < pw->n_custom + 2 * ROWS; i++) {
        if (game_text_open(pw->file[i], pw->file[i])) return 1;
        game_text_get();
        game_text_item(), game_text_item();
        strncpy(pw->title[i], game_text_item(), NAME_LEN);
        pw->title[i][NAME_LEN - 1] = 0;
    }
    return 0;
}

// The pictures are cut out of one sheet: the three groups' looks in three rows of three, then the row buttons'
// looks in a strip below them and the marker.
static void cut_pictures(struct game_pickwin *pw, struct game_buffer *sheet) {
    enum { STRIP_Y = 0x139, MARKER_W = 7, MARKER_H = 0xd };
    int key = *game_default_key;
    for (int look = 0; look < 3; look++)
        for (int i = 0; i < PACKS; i++) {
            struct ui_rect r = rect_of(1 + i * (PACK_W + 1), 1 + look * (PACK_H + 1), PACK_W, PACK_H);
            sprite_extract(&pw->pack_pic[i][look], sheet, key, &r.left, NULL);
        }
    for (int look = 0; look < 3; look++) {
        struct ui_rect r = rect_of(1, STRIP_Y + look * (ROW_BUTTON + 1), ROW_BUTTON, ROW_BUTTON);
        sprite_extract(&pw->row_pic[look], sheet, key, &r.left, NULL);
    }
    sprite_extract_2(&pw->marker, sheet, key, 1, STRIP_Y + 3 * (ROW_BUTTON + 1), MARKER_W, MARKER_H, NULL);
}

// Makes a button (hidden): the name (NULL for none), the id and the rectangle; the Buffer's font is the window's.
static void make_button(struct game_pickwin *pw, struct game_flat_button *b, const char *name, int id, const struct ui_rect *r) {
    game_base_button_init(b, name, id, r->left, r->top, r->right - r->left, r->bottom - r->top, pw, 0);
}

// The entry of the labels file that names a button.
static int button_label(const struct game_pickwin *pw, int slot) {
    switch (slot) {
    case PW_BTN_A: return LABEL_BUTTON_A;
    case PW_BTN_B: return pw->flag ? LABEL_BUTTON_B_ON : LABEL_BUTTON_B_OFF;
    case PW_BTN_CANCEL: return LABEL_CANCEL;
    case PW_BTN_OK: return LABEL_OK;
    case PW_BTN_RANDOM: return LABEL_RANDOM;
    default: return LABEL_CHOOSE;
    }
}

// Puts the list in its starting state: closed and made again over its rectangle, with the look the picker gives it.
static void reset_list(struct game_pickwin *pw) {
    struct game_listbox *l = (struct game_listbox *)pw->list;
    struct game_dlg *dlg = list_dlg(pw);
    listbox_close(pw->list);
    dialog_init_3((struct game_dialog *)dlg, &pw->list_rect.left, 0);
    l->f2c = 0;
    l->f14 = -1;
    DLG_F(dlg, DLG_ITEM_COUNT) = 0;
    DLG_F(dlg, DLG_MARGIN) = 0;
    DLG_F(dlg, DLG_ROW_HEIGHT) = 0x13;
    DLG_F(dlg, DLG_GAP) = 3;
    list_win(pw)->flags = 0x40;
}

static void UI_THIS pickwin_init(struct game_pickwin *pw, void(UI_CDECL *on_pick)(int), int kind, int flag) {
    struct game_buffer sheet;
    buffer_ctor(&sheet);
    pw->flag = flag;
    pw->on_pick = on_pick;
    pw->kind = kind;
    pw->action = 0;
    if (!read_faction_names(pw)) {
        buffer_load_pcx(&sheet, text(PICTURE), NULL, 10, 0xec);
        cut_pictures(pw, &sheet);
        pw->w = 0x316, pw->h = 0x12c, pw->x = 5, pw->y = 5;
        graphic_win_init(&pw->win, 5, 5, 0x316, 0x12c, game_at(EMPTY_TITLE), 0x1000000, NULL, NULL, NULL);
        buffer_set_font(&pw->buf, font(), NULL, NULL, NULL);
        buffer_set_text_color(&pw->buf, word(COLOR_TEXT), word(COLOR_TEXT + 4), 1, 1);

        struct pickwin_layout lay;
        pickwin_layout(&lay, pw->w, pw->h);
        pw->frame = lay.frame, pw->header = lay.header, pw->list_rect = lay.list;
        for (int i = 0; i < PACKS; i++) pw->pack_rect[i] = lay.pack[i];
        for (int i = 0; i < ROWS; i++) pw->row_rect[i] = lay.row[i];
        for (int i = 0; i < 6; i++) pw->button_rect[i] = lay.button[i];
        pw->x = lay.last_x, pw->y = lay.last_y, pw->w = lay.last_w, pw->h = lay.last_h;

        // The row buttons, then the three pictures; all start hidden.
        for (int i = 0; i < ROWS; i++) {
            struct game_flat_button *b = &pw->row_btn[i];
            make_button(pw, b, NULL, ID_ROW_BASE + i, &pw->row_rect[i]);
            for (int look = 0; look < 3; look++) b->pic[look] = &pw->row_pic[look];
            hide(button_win(b));
        }
        for (int i = 0; i < PACKS; i++) {
            struct game_flat_button *b = &pw->pack_btn[i];
            strcpy(game_str_buffer, game_label(LABEL_PACK + i));
            make_button(pw, b, NULL, ID_PACK_FACTIONS + i, &pw->pack_rect[i]);
            for (int look = 0; look < 3; look++) b->pic[look] = &pw->pack_pic[i][look];
            base_button_set_bubble_text(&b->base, game_str_buffer);
            hide(button_win(b));
        }

        reset_list(pw);
        DLG_F(list_dlg(pw), DLG_FLAGS) &= ~4;
        ((struct game_listbox *)pw->list)->on_double = (void(UI_CDECL *)(int))(uintptr_t)game_va(0x488780);

        // The buttons, made in this order, each with its label (Cancel and OK in capitals).
        static const struct {
            int slot, id;
        } order[] = {{PW_BTN_A, ID_A}, {PW_BTN_B, ID_B}, {PW_BTN_CANCEL, ID_CANCEL}, {PW_BTN_OK, ID_OK}, {PW_BTN_RANDOM, ID_RANDOM}, {PW_BTN_CHOOSE, ID_CHOOSE}};
        for (int i = 0; i < 6; i++) {
            int slot = order[i].slot;
            struct game_flat_button *b = &pw->button[slot];
            strcpy(game_str_buffer, game_label(button_label(pw, slot)));
            if (slot == PW_BTN_CANCEL || slot == PW_BTN_OK) (*game_char_upper)(game_str_buffer);
            make_button(pw, b, game_str_buffer, order[i].id, &pw->button_rect[slot]);
            buffer_set_font(&b->base.buf, font(), NULL, NULL, NULL);
            redraw(button_win(b));
            hide(button_win(b));
        }
        button_group_init(&pw->group, 0, 0);
        button_group_add(&pw->group, &pw->button[PW_BTN_CHOOSE].base);
        button_group_add(&pw->group, &pw->button[PW_BTN_RANDOM].base);
        button_group_set(&pw->group, ID_CHOOSE, 1);
    }
    buffer_dtor(&sheet);
}

// Shows the window as a modal one and answers the faction chosen (-1 for Cancel) when it ends.
static int UI_THIS pickwin_show_window(struct game_pickwin *pw) {
    redraw(pw);
    show(pw);
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), pw, VT_SET_MODAL, 0, 0, 0);
    return pw->chosen;
}

static void UI_THIS pickwin_close(struct game_pickwin *pw) { graphic_win_close(&pw->win); }

// ---- Drawing -----------------------------------------------------------------------------------------------

// The window: the frame, the title, a frame round the list, and the lines that run from the chosen radio button
// to the list row (or, for Random, to all seven rows), each ending in the marker.
static void UI_THIS pickwin_on_redraw(struct game_pickwin *pw) {
    struct game_buffer *b = &pw->buf;
    enum { LINE = 0xe0 };
    graphic_win_fill(&pw->win, 9);
    buffer_box_sprite(b, &pw->frame.left, frame_at(FRAME_FRAME));
    buffer_box_sprite(b, &pw->header.left, frame_at(FRAME_HEADER));
    struct ui_rect inner = {pw->header.left + 3, pw->header.top + 3, pw->header.right - 3, pw->header.bottom - 3};
    buffer_box_sprite(b, &inner.left, frame_at(FRAME_INNER));
    strcpy(game_str_buffer, game_label(LABEL_TITLE));
    buffer_write_cent_l_7(b, game_str_buffer, &inner.left, strlen(game_str_buffer));

    struct ui_rect around = {pw->list_rect.left - 3, pw->list_rect.top - 3, pw->list_rect.right + 0x12 + 3, pw->list_rect.bottom + 3};
    buffer_box_sprite(b, &around.left, frame_at(FRAME_HEADER));

    int selected = dialog_get_selected_id(list_dlg(pw));
    const struct ui_rect *choose = &pw->button_rect[PW_BTN_CHOOSE], *random = &pw->button_rect[PW_BTN_RANDOM];
    int top = choose->top + 10;
    int below = top + 0x39;
    int mid = (choose->left + choose->right) / 2;  // (the original halves toward zero, as C does)
    int line_left = pw->list_rect.left - 10;
    int bend = (line_left + choose->right) / 2;
    struct game_sprite *marker = &pw->marker;

    if (pw->group.sel == ID_RANDOM) {
        // A line down from Random, and along to a bend from which a line runs to each of the seven rows.
        buffer_vline(b, mid, random->bottom - 2, below - 2, LINE);
        buffer_put_pixel(b, mid + 1, below - 1, LINE);
        buffer_hline(b, mid + 2, bend, below, LINE);
        buffer_vline(b, bend, top + 2, top + 0x70, LINE);
        for (int row = 0; row < ROWS; row++) {
            int y = pickwin_row_y(top, row);
            buffer_put_pixel(b, bend + 1, y + (row == 0 ? 1 : -(row == ROWS - 1)), LINE);
            buffer_hline(b, bend + 2, line_left, y, LINE);
            sprite_draw_3(marker, b, marker->key, line_left, y - 6);
        }
    } else if (selected > 0 && selected < ROWS + 1) {
        // A line from Choose to the chosen row.
        int row_y = pickwin_row_y(top, selected - 1);
        buffer_hline(b, choose->right, bend - 2, top, LINE);
        if (selected - 1 != 0) {
            buffer_put_pixel(b, bend - 1, top + 1, LINE);
            buffer_put_pixel(b, bend + 1, row_y - 1, LINE);
            buffer_vline(b, bend, top + 2, row_y - 2, LINE);
        } else {
            buffer_hline(b, bend - 2, bend + 2, top, LINE);
        }
        buffer_hline(b, bend + 2, line_left, row_y, LINE);
        sprite_draw_3(marker, b, marker->key, line_left, row_y - 6);
    }

    for (int i = 0; i < PACKS; i++) show(button_win(&pw->pack_btn[i]));
    for (int i = 0; i < ROWS; i++) show(button_win(&pw->row_btn[i]));
    show(list_win(pw));
    static const int shown[] = {PW_BTN_A, PW_BTN_B, PW_BTN_CANCEL, PW_BTN_OK, PW_BTN_RANDOM, PW_BTN_CHOOSE};
    for (int i = 0; i < 6; i++) show(button_win(&pw->button[shown[i]]));
}

// The background of a list item, which the list box asks for: the window of the list, the item's look, and the item's
// rectangle. The frame and the text colours are those of that look. Answers 0 without a window or a rectangle.
static int UI_THIS pickwin_on_dialog_item_back_draw(void *unused, struct game_win *win, int a, int look, const int32_t *rect) {
    if (!win || !rect) return 0;
    struct game_buffer *b = (struct game_buffer *)((uint8_t *)win + 0x444);
    buffer_set_clip(b, b->full);
    struct ui_rect r = {rect[0], rect[1], rect[2], rect[3]};
    buffer_box_sprite(b, &r.left, (const struct game_box_frame *)((const uint8_t *)game_at(FRAMES_ITEM) + 0x28 * look));
    const int32_t *colors = (const int32_t *)game_at(COLORS_ITEM) - 2 * look;
    buffer_set_text_color(b, colors[0], colors[1], 1, 1);
    return 1;
}

// ---- Input -------------------------------------------------------------------------------------------------

// Hovering over a button does nothing.
static void UI_THIS pickwin_on_button_passover(struct game_pickwin *pw, int a, int b) {}

// Escape clicks Cancel and Return clicks OK; any other key ends the window's action as none.
static int UI_THIS pickwin_on_key_click(struct game_pickwin *pw, int a, int key) {
    if (key == 0x1b) {
        UI_VCALL(void(UI_THIS *)(void *, int), pw, VT_BUTTON_CLICKED, ID_CANCEL);
        pw->action = 0;
    } else if (key == 0xd) {
        UI_VCALL(void(UI_THIS *)(void *, int), pw, VT_BUTTON_CLICKED, ID_OK);
    } else {
        pw->action = 0;
    }
    return 1;
}

// The radio buttons changed (the group's `user` is always 0): Random keeps the list's choice and clears the list's
// selection, Choose puts it back.
static void UI_THIS pickwin_on_group_clicked(struct game_pickwin *pw, int user, int old_id, int new_id) {
    if (user) return;
    if (new_id == ID_RANDOM) {
        pw->saved_pick = dialog_get_selected_id(list_dlg(pw));
        listbox_set_selected_pos((struct game_listbox *)pw->list, -1, 1);
    } else {
        listbox_set_selected_id((struct game_listbox *)pw->list, pw->saved_pick);
    }
    redraw(pw);
}

// Fills the list with the factions of the rows (all seven, or in a choice among those in the game just those that are
// in it), each as a line of its names, and attaches it to the window.
static void fill_list(struct game_pickwin *pw) {
    for (int row = 1; row <= ROWS; row++) {
        if (pw->kind == 2 && !(*game_factions_alive & (1 << row))) continue;
        const struct faction_def *def = &game_faction_defs[row];
        if (strcmp(def->filename, text(BLANK_NAME)) == 0) strcpy(game_str_buffer, game_label(LABEL_EMPTY_SLOT));
        else pickwin_item_text(game_str_buffer, def->formal_name, def->desc_name, text(SEPARATOR), text(OPEN_PAREN), text(CLOSE_PAREN));
        game_listbox_item(pw->list, game_str_buffer, row);
    }
    struct game_dlg *dlg = list_dlg(pw);
    game_listbox_attach(pw->list + LISTBOX_DIALOG, pw, DLG_F(dlg, DLG_ATTACH_A), DLG_F(dlg, DLG_ATTACH_B), 0x80020);
}

// The list has been made again over new factions: it is given the choice and the function config_game wants told of
// choices (which the list's closing cleared).
static void choose_in_list(struct game_pickwin *pw, int pick) {
    listbox_set_selected_id((struct game_listbox *)pw->list, pick);
    ((struct game_listbox *)pw->list)->on_select = pw->on_pick;
}

// A click on one of the three pictures: the group's seven faction files are read (the custom group has none, its rows
// are the empty slot), and the list is made again over them.
static void choose_pack(struct game_pickwin *pw, int id) {
    if (id != ID_PACK_CUSTOM) {
        if (game_text_open(rules_file(), text(id == ID_PACK_FACTIONS ? SECTION_FACTIONS_AGAIN : SECTION_NEW_AGAIN))) return;
        for (int row = 1; row <= ROWS; row++) {
            game_text_get();
            strcpy(game_faction_defs[row].filename, game_text_item());
            strcpy(game_faction_defs[row].search_key, game_text_item());
        }
    }
    for (int row = 1; row <= ROWS; row++) {
        if (id == ID_PACK_CUSTOM) {
            strcpy(game_faction_defs[row].filename, text(BLANK_FILE));
            strcpy(game_faction_defs[row].search_key, text(BLANK_KEY));
        } else {
            read_faction2(row);
        }
    }
    reset_list(pw);
    fill_list(pw);
    choose_in_list(pw, current_pick(pw));
    pw->on_pick(current_pick(pw));
    redraw(pw);
}

// The popup menu a row button opens: the files' names (and an empty slot) to put in its row.
enum { POPMENU_SIZE = 0x62d4, POPMENU_PULLDOWN = 0x537c, POPMENU_DIALOGS = 0x21d0 };
enum { VTABLE_POPMENU = 0x66a8a8, VTABLE_POPMENU_BUF = 0x66a8a0, VTABLE_PULLDOWN = 0x66a738, VTABLE_PULLDOWN_BUF = 0x66a730 };

static void replace_row(struct game_pickwin *pw, int id) {
    int32_t x, y;
    win_get_mouse_pos(&pw->win, &x, &y);
    win_client_to_screen_2(&pw->win, &x, &y);

    uint8_t menu[POPMENU_SIZE] __attribute__((aligned(4)));
    game_popup_ctor(menu);
    pulldown_ctor((struct game_pulldown *)(menu + POPMENU_PULLDOWN));
    *(uint32_t *)menu = game_va(VTABLE_POPMENU);
    *(uint32_t *)(menu + 0x444) = game_va(VTABLE_POPMENU_BUF);
    *(uint32_t *)(menu + POPMENU_PULLDOWN) = game_va(VTABLE_PULLDOWN);
    *(uint32_t *)(menu + POPMENU_PULLDOWN + 0x444) = game_va(VTABLE_PULLDOWN_BUF);
    game_popmenu_init(menu);
    int n = pw->n_custom + 2 * ROWS;
    for (int i = 0; i < n; i++) game_dialogs_item(menu + POPMENU_DIALOGS, pw->title[i], i);
    strcpy(game_str_buffer, game_label(LABEL_EMPTY_SLOT));
    game_dialogs_item(menu + POPMENU_DIALOGS, game_str_buffer, n);
    int pick = game_popmenu_exec(menu, x, y, 0);

    struct game_flat_button *button = &pw->row_btn[id - ID_ROW_BASE];
    button->base.changed = 0;
    redraw(button_win(button));
    if (pick != -1) {
        int row = id - ID_ROW_BASE + 1;
        struct faction_def *def = &game_faction_defs[row];
        if (pick == n) {
            strcpy(def->filename, text(BLANK_FILE_CHOSEN));
            strcpy(def->search_key, text(BLANK_KEY_CHOSEN));
        } else {
            strcpy(def->filename, pw->file[pick]);
            strcpy(def->search_key, pw->file[pick]);
            read_faction2(row);
        }
        reset_list(pw);
        fill_list(pw);
        choose_in_list(pw, row);
        pw->on_pick(row);
        redraw(pw);
    }
    pulldown_dtor((struct game_pulldown *)(menu + POPMENU_PULLDOWN));
    game_popup_dtor(menu);
}

// A button was clicked, or a list row double clicked (ids 1 to 7, the faction's row).
static void UI_THIS pickwin_on_button_clicked(struct game_pickwin *pw, int id) {
    switch (id) {
    case ID_PACK_FACTIONS:
    case ID_PACK_NEW:
    case ID_PACK_CUSTOM:
        choose_pack(pw, id);
        return;
    case ID_CANCEL:
        pw->chosen = -1;
        pw->action = 0;
        release_modal(pw);
        return;
    case ID_OK: {
        // With Random on, the faction is the one the list had before; the action says so.
        int random = pw->group.sel == ID_RANDOM;
        pw->chosen = random ? pw->saved_pick : dialog_get_selected_id(list_dlg(pw));
        pw->action = random ? ID_RANDOM : 0;
        release_modal(pw);
        return;
    }
    default:
        break;
    }
    if (id <= 0) return;
    if (id < ROWS + 1) {
        pw->chosen = id;
        pw->action = 0;
        release_modal(pw);
    } else if (id == ID_CHOOSE || id == ID_RANDOM) {
        // (the radio buttons tell the group, not this)
    } else if (id < ID_ROW_BASE) {
        pw->chosen = dialog_get_selected_id(list_dlg(pw));
        pw->action = id;
        release_modal(pw);
    } else {
        replace_row(pw, id);
    }
}

static void UI_CDECL pickwin_on_list_double(int id) {
    if (id >= 1 && id <= ROWS) pickwin_on_button_clicked(the_pickwin(), id);
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook pickwin_hooks[] = {
    UI_HOOK(0x488780, pickwin_on_list_double, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),  // push ebp; mov ebp, esp; mov eax, [ebp+8]
    UI_HOOK(0x4887a0, pickwin_init, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x7b, 0x7f, 0x65, 0x00),  // push -1; push handler
    UI_HOOK(0x489660, pickwin_on_redraw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x24, 0x53),  // sub esp, 0x24; push ebx
    UI_HOOK(0x489b00, pickwin_close, 0xe9, 0x3b, 0xb3, 0x14, 0x00),  // jmp graphic_win_close
    UI_HOOK(0x489b10, pickwin_on_button_passover, 0xc2, 0x08, 0x00, 0x90, 0x90),  // ret 8
    UI_HOOK(0x489b20, pickwin_on_group_clicked, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x56, 0x85, 0xc0),
    UI_HOOK(0x489ba0, pickwin_on_button_clicked, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff, 0x68, 0xbf, 0x81, 0x65, 0x00),
    UI_HOOK(0x48a7c0, pickwin_on_key_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x56, 0x83, 0xf8, 0x1b),
    UI_HOOK(0x48a810, pickwin_show_window, 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0xff, 0x90, 0xf8, 0x00, 0x00, 0x00),  // push esi; mov esi, ecx; mov eax, [esi]; call [eax+0xf8]
    UI_HOOK(0x48a840, pickwin_on_dialog_item_back_draw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x45, 0x08),
    UI_HOOK(0x48a8e0, pickwin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x25, 0x7e, 0x65, 0x00),  // push -1; push handler
    UI_HOOK(0x48ac10, pickwin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x5b, 0x7f, 0x65, 0x00),
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x48adc0, pickwin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0x15, 0xfb, 0xff, 0xff),
    UI_HOOK(0x48adf0, pickwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module pickwin_module = {"pickwin", pickwin_hooks, UI_HOOK_COUNT(pickwin_hooks), NULL, NULL, -1};
