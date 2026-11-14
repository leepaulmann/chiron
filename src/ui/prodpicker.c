// The production picker, the game's ProdPicker: the list of what a base can build, which the base screen opens to change the
// base's production (its CHANGE button) or one line of its queue (a click on the line: `mode` is the line, 0 for the production).
// It is a member of the base screen's object (BaseWin, 0x6a7628), at 0xa1c into it (0x6a8044); BaseWin's constructor builds and its
// destructor unbuilds it. Outside the class the base screen calls exec (opens the window and answers the choice), calculate (to make
// the list again) and the two sorting functions (edbc_unit and edbc_fac, which the base screen's reports call too).
//
// The window is a grid of cells, five to a row and three or four rows (the screen's width tells), over the base screen. A
// cell frames one thing a base can build: a unit design, with its picture drawn by the game's own unit drawer (which gets the unit
// ready, and when it answers yes the game's 3D model draws it), or a facility's picture; its name above, how many turns it takes (and
// what it would take after changing, for a design that is a prototype) and its strengths below. A scroll bar at the right moves the
// view a row at a time when there are more than fit. A row of buttons at the foot says what to do with the choice: for the production
// HELP, WORKSHOP, OK and CANCEL (and, for a governed base, one more), for a line of the queue HELP, OK, REPLACE, INSERT, DELETE and
// CANCEL. The mouse over a cell does nothing; a click marks it, a double click marks it and answers (OK, or in a queue line
// INSERT), a right click opens a menu (set the production, add to the queue, hide this design, show every hidden one, help), and the keys move
// the cell marked (the cursor keys and the keypad's 2, 4, 6 and 8) or the view (Page Up and Page Down, Home and End).
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=prodpicker gives them all back. The object keeps the original's layout, the toolkit is called directly, and what
// it draws, in what order, and what it writes are the original's. BaseWin's constructor and destructor call ours.
//
// Game logic mixed with the screen. `calculate` is the listing: it asks the game which designs and facilities the base can
// build (veh_avail, facility_avail), what each costs and how long it takes (veh_cost, cost_factor, base_making for the cost of
// changing) and puts a line for each (the name, the turns and the strengths) into three lists, the first holding each choice's
// number and kind. It is the part that decides what the player is offered, so it is kept exactly (a wrong list is a different
// picture, and a wrong choice a different queue, which the scenes' state compare shows). It stays here because it is welded to the
// object (it fills the object's lists and the grid the window draws, and the choice's number and kind are what exec answers with);
// nothing else uses it but exec and the base screen, which calls it to make the list again. It also sorts the designs (proto_sort)
// as it goes. The turns, the cost carried over to another kind of thing and the kinds a design or a facility belongs to (what
// edbc_unit and edbc_fac answer) are pure functions in prodpicker.h with a unit test. The menu's choices that hide a design change
// the game's prototype table (the right click's Obsolete and Cancel all obsolete).
//
// Differences from the original that can't be seen: the exception frames are not reproduced; a search the original makes in
// `calculate` through the weapons, armour and chassis for a technology the faction has (its answer is thrown away: nothing reads
// it and the lookups only read) is left out; the original's inlined copies of the lists' and the Scroll's destructors run
// the same calls as ours (as `list_unbuild` and the toolkit's own). Nothing else.
// Quirks kept: a cell frame is drawn for a click or key past the last choice (and the clip left at the cell: draw_item returns
// before putting it back); the click's cell is worked out without taking the grid's own corner off; the menu left without a
// choice reads the word before its first line (as `items[-1]`) for the choice; the Down key on the last row only scrolls.
// Dead code the original has: the timer callback (UNK2) that was to advance the picture's turn is never started (nothing
// refers to it), so the turn counter, always 0, never moves; UNK1 and UNK3 are called by nothing, the first answers 1.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "prodpicker.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/base.h"
#include "../engine/buffer.h"
#include "../engine/bufimg.h"
#include "../engine/bufobj.h"
#include "../engine/build.h"
#include "../engine/button.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/scroll.h"
#include "../engine/sprite.h"
#include "../engine/strlist.h"
#include "../engine/tech.h"
#include "../engine/text.h"
#include "../engine/veh.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

void prodpicker_grid(struct prodpicker_grid *g, int win_w, int screen_w, int cell_h) {
    g->cell_w = (win_w - 0x51) / 5;
    g->cell_h = cell_h;
    g->per_row = (win_w - 0x51) / g->cell_w;
    g->rows = 3 + (screen_w > 0x320);
    g->visible = g->rows * g->per_row;
}

void prodpicker_cell_rect(struct ui_rect *r, const struct prodpicker_grid *g, int slot) {
    int row = slot / g->per_row, col = slot % g->per_row;
    r->left = col * g->cell_w + 0x17 + 4;
    r->top = row * g->cell_h + 4;
    r->right = (col + 1) * g->cell_w + 0x17 + 4 - 3;
    r->bottom = row * g->cell_h + g->cell_h + 4 - 3;
}

int prodpicker_click_slot(const struct prodpicker_grid *g, int x, int y) { return y / g->cell_h * g->per_row + x / g->cell_w; }

void prodpicker_font_sizes(int cell_w, int *name_size, int *cost_size) {
    int s = cell_w / 15;
    *name_size = s < 0x18 ? 0x18 : s;
    *cost_size = s < 10 ? 10 : s;
}

int prodpicker_unit_kind(int proto, int flags, int plan) {
    if (proto >= 0x40 && !(flags & 4)) return 1;
    switch (plan) {
    case 3: case 7: case 8: return 0;
    case 9: case 10: return 2;
    case 11: return 1;
    default: return 3;
    }
}

int prodpicker_facility_kind(int fac) {
    if (fac >= 0x46) return 1;
    // The original compiles this as a switch over facilities 3 to 0x44.
    switch (fac) {
    case 3: case 31: case 32: return 0;
    case 8: case 15: case 16: case 17: return 1;
    case 4: case 27: case 28: case 29: case 30: case 68: return 3;
    default: return 2;
    }
}

int prodpicker_turns(int surplus, int remaining, int limit) {
    int n = (surplus + remaining - 1) / surplus;
    if (limit < 0) return n;
    return n < 0 ? 0 : n > limit ? limit : n;
}

int prodpicker_retool_stock(int stock, int threshold, int percent) {
    if (stock <= threshold) return stock;
    return threshold + (stock - threshold) * (100 - percent) / 100;
}

int prodpicker_button_count(int mode, int human, int state, uint32_t governor) {
    if (mode) return 6;
    if (!human) return 5;
    if (!(state & 0x40)) return 4;
    if (!(governor & 0x80000000u)) return 4;
    return (governor & 1) ? 5 : 4;
}

int prodpicker_row_start(int win_w, int width, int count) {
    return (win_w - (width + 3) * count - 0x20) / 2 + (count - 1) * (width + 3);
}

int prodpicker_button_step(int width) { return -3 - width; }

enum prodpicker_key prodpicker_key_action(int key) {
    switch (key) {
    case 0x21: return PPK_PGUP;
    case 0x22: return PPK_PGDN;
    case 0x23: return PPK_END;
    case 0x24: return PPK_HOME;
    case 0x25: case 0x64: return PPK_LEFT;
    case 0x26: case 0x68: return PPK_UP;
    case 0x27: case 0x66: return PPK_RIGHT;
    case 0x28: case 0x62: return PPK_DOWN;
    default: return PPK_NONE;
    }
}

int prodpicker_step_sideways(int sel, int dir, int per_row, int total) {
    int row_start = sel / per_row * per_row;
    int row_end = row_start + per_row - 1;
    if (row_end > total - 1) row_end = total - 1;
    int n = sel + dir;
    if (n < row_start || row_end < row_start) return row_start;
    return n > row_end ? row_end : n;
}

int prodpicker_unit_art(int cell_h) { return cell_h * 2 / 3; }
int prodpicker_unit_model(int cell_h) { return prodpicker_unit_art(cell_h) * 75 / 100; }
int prodpicker_facility_art(int cell_h) { return cell_h * 3 / 7; }

void prodpicker_art_origin(const struct ui_rect *cell, int w, int h, int *x, int *y) {
    *x = (cell->right - cell->left) / 2 - w / 2 + cell->left;
    *y = (cell->bottom - cell->top) / 2 - h / 2 + cell->top;
}

int prodpicker_menu_offers_hide(int proto, int weapon_offense) {
    if (proto < 0) return 0;
    return proto >= 0x40 || (weapon_offense >= 0 && proto != 15);
}

// ---- The object -------------------------------------------------------------------------------------------

enum { BASEWIN = 0x6a7628 };
// The base fields it reads.
_Static_assert(offsetof(struct base, production_last) == 0x44, "base production_last");
_Static_assert(offsetof(struct base, minerals) == 0x40, "base minerals");
_Static_assert(offsetof(struct base, governor) == 0x38, "base governor");
_Static_assert(offsetof(struct base, surplus) + sizeof(int32_t) == 0xd4, "base mineral surplus");
_Static_assert(offsetof(struct base, minerals_2) == 0x9c, "base minerals_2");

// The object's three lists are StringLists (0x30 bytes each): the choice's number and kind (a unit's kind has 0x20 set, a facility's
// 0x10, 0x80 on a prototype), the line of what it takes, and the line of its strengths.
struct game_prodpicker {
    struct game_win win;       // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;    // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t opened;            // 0xa14: cleared by close (and set by nothing here)
    struct game_sprite sprite[3];  // 0xa18: built and unbuilt, never used
    struct font font[4];       // 0xa9c: the choice's name, the cost line, the strengths (set to 3 for its leading) and the buttons
    struct game_time timer;    // 0xb3c: started by nothing
    int32_t anim;              // 0xb64: how many turns of the picture the callback that nothing starts would have counted
    int32_t mode;              // 0xb68: 0 for the base's production, else the line of its queue the window is for
    int32_t n_buttons;         // 0xb6c: how many buttons the row at the foot holds
    int32_t button_w;          // 0xb70: how wide each is (100)
    struct game_flat_button button[9];  // 0xb74
    uint8_t scroll[0x214c];    // 0x7120: a Scroll (see scroll.c), of which the last four bytes are the model's
    uint8_t model[0x1500];     // 0x926c: the model drawer's object (the game's own, called through pointers)
    int32_t faction;           // 0xa76c: whose list it is
    int32_t base;              // 0xa770: what exec was given as the base (never used)
    int32_t selected;          // 0xa774: the cell chosen, -1 for none
    int32_t action;            // 0xa778: what ended the window: 0 for none or Cancel, else which button
    int32_t first;             // 0xa77c: the first cell shown
    uint8_t list[3][0x30];     // 0xa780
    int32_t per_row;           // 0xa810
    int32_t rows;              // 0xa814: how many rows are shown
    int32_t total;             // 0xa818: how many choices
    int32_t cell_w;            // 0xa81c
    int32_t cell_h;            // 0xa820
    int32_t win_w, win_h;      // 0xa824: the base screen's size, which the window takes
    int32_t visible;           // 0xa82c: how many cells are shown
    int32_t c830, c834;        // 0xa830: 0x86 both, never used
    int32_t cell_h_set;        // 0xa838: the cell height (0x6c)
    int32_t c83c;              // 0xa83c: 0x5e, never used
};
GAME_AT(struct game_prodpicker, opened, 0xa14);
GAME_AT(struct game_prodpicker, sprite, 0xa18);
GAME_AT(struct game_prodpicker, font, 0xa9c);
GAME_AT(struct game_prodpicker, timer, 0xb3c);
GAME_AT(struct game_prodpicker, anim, 0xb64);
GAME_AT(struct game_prodpicker, mode, 0xb68);
GAME_AT(struct game_prodpicker, n_buttons, 0xb6c);
GAME_AT(struct game_prodpicker, button_w, 0xb70);
GAME_AT(struct game_prodpicker, button, 0xb74);
GAME_AT(struct game_prodpicker, scroll, 0x7120);
GAME_AT(struct game_prodpicker, model, 0x926c);
GAME_AT(struct game_prodpicker, faction, 0xa76c);
GAME_AT(struct game_prodpicker, selected, 0xa774);
GAME_AT(struct game_prodpicker, action, 0xa778);
GAME_AT(struct game_prodpicker, first, 0xa77c);
GAME_AT(struct game_prodpicker, list, 0xa780);
GAME_AT(struct game_prodpicker, per_row, 0xa810);
GAME_AT(struct game_prodpicker, total, 0xa818);
GAME_AT(struct game_prodpicker, visible, 0xa82c);
GAME_AT(struct game_prodpicker, cell_h_set, 0xa838);
_Static_assert(sizeof(struct game_prodpicker) == 0xa840, "layout of the game's ProdPicker");

// The slots of the window's vtable the screen uses (byte offsets).
enum {
    VT_SHOW = 0x4,
    VT_HIDE = 0x8,
    VT_LEFT_CLICK = 0x4c,
    VT_BUTTON_CLICKED = 0xb0,
    VT_SET_MODAL = 0xe4,
    VT_RELEASE_MODAL = 0xe8,
    VT_REDRAW = 0xf8,
};
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66a250, VTABLE_BUF = 0x66a248 };

// What the object's fields past the window's own are called by their place in the BaseWin: the model's data.
enum { MODEL = 0x926c };
static int32_t *at32(struct game_prodpicker *pp, uint32_t off) { return (int32_t *)((uint8_t *)pp + off); }

static struct game_strlist *plist(struct game_prodpicker *pp, int i) { return (struct game_strlist *)pp->list[i]; }

// ---- The game's data it reads (by address) -----------------------------------------------------------------

static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static float real(uint32_t addr) { return *(float *)game_at(addr); }
static char *text(uint32_t addr) { return game_at(addr); }
// A string the game keeps by its place in its string table.
static char *name_of(int32_t p) { return (char *)(uintptr_t)strings_get((struct game_strings *)game_strings, (unsigned)p); }
// Entries of the labels file are given by their byte offset in the table, as the original reads them.
static char *label_at(int byte_offset) { return game_label(byte_offset / 4); }

enum {
    CARD = 0x78d528,         // box frames: a cell's, the marked cell's, and the button row's
    CARD_MARKED = 0x78d578,
    BOX_BUTTONS = 0x78d5a0,
    ICON_SCALE = 0x691e6c,   // the game's own icon scale, a numerator and a denominator
    UNIT_ORDER = 0x945b44,   // 128 numbers: the designs in the order proto_sort puts them
    FAC_PICS = 0x787200,     // 0x2c bytes a facility: its picture (the first one's height is every one's)
    SECRET_PICS = 0x759320,
    FAC_TABLE = 0x9a4b68,    // 0x30 bytes a facility: its name (0x0), what it is (0x4), its cost (0xc)
    CONSOLE = 0x9156b0,
    BASEWIN_RIGHT = 0x6b4e84,  // the base screen's 22 buttons, whose flag 8 is raised while this window is modal
    BASEWIN_RIGHT_END = 0x6c470c,
    MODAL_CALL = 0x539920,
    MODAL_ARG = 0x7ae820,
    SCREEN_WIDE = 0x320,
};
// Strings it puts together.
enum {
    S_SPACE = 0x682820,
    S_OPEN = 0x682e9c,
    S_CLOSE = 0x682e98,
    S_COMMA = 0x686df0,
    S_TURNS_OPEN = 0x686df4,
    S_TURNS_CLOSE = 0x686df8,
    S_NEVER_OPEN = 0x686dfc,
    S_NEVER_CLOSE = 0x686e00,
    S_PROTOTYPE_OPEN = 0x686dec,
    S_PROTOTYPE_CLOSE = 0x682ea0,
};

static void cat_num(char *s, int v) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", v);
    strcat(s, digits);
}

static int font_line(const struct font *f) { return f->first >= 0 ? f->height + f->first : f->ascent; }

static struct prodpicker_grid grid_of(const struct game_prodpicker *pp) {
    return (struct prodpicker_grid){pp->per_row, pp->rows, pp->visible, pp->cell_w, pp->cell_h};
}

// The cell a list is at (its cursor) and what it holds there.
static int32_t list_id(const struct game_strlist *l) { return l->head ? l->cur->id : 0; }
static int32_t list_flags(const struct game_strlist *l) { return l->head ? *(int32_t *)((uint8_t *)l->cur->item + 8) : 0; }
static char *list_text(const struct game_strlist *l) { return l->head ? *(char **)((uint8_t *)l->cur->item + 4) : NULL; }

// What a new entry is given: the text, and the two numbers (the first is the choice's kind for the first list).
static int list_add(struct game_strlist *l, int id, char *s, int kind) {
    uint8_t *o = (uint8_t *)l;
    *(char **)(o + 0x1c) = s;
    *(int32_t *)(o + 0x20) = kind;
    *(int32_t *)(o + 0x24) = 0;
    return strlist_add(l, id);
}

// ---- The lists ------------------------------------------------------------------------------------------

// A list as the original builds it in place: the list and its virtual base. Its own constructor, inlined.
enum { SL_VTABLE = 0x6698c4, SL_PART_VTABLE = 0x6698c0, SL_BASE_VTABLE = 0x6693a4, SL_BASE_PART = 0x6693a0, SL_VBPTR = 0x66b0ec,
       SL_VB_VTABLE = 0x6693ac, SL_HEAD = 0x9b3374 };

static void list_set_part(uint8_t *o, uint32_t vtable) {
    const int32_t *vbtable = (const int32_t *)(uintptr_t)((struct game_strlist *)o)->vbptr;
    *(uint32_t *)(o + 4 + vbtable[1]) = vtable;
}

static void list_build(uint8_t *o) {
    uint32_t *head = game_at(SL_HEAD);
    *(uint32_t *)(o + 4) = game_va(SL_VBPTR);
    *(uint32_t *)(o + 0x28) = game_va(SL_VB_VTABLE);
    *(uint32_t *)(o + 0x2c) = *head;
    *head = 0;
    *(uint32_t *)o = game_va(SL_BASE_VTABLE);
    list_set_part(o, game_va(SL_BASE_PART));
    memset(o + 8, 0, 0x14);
    *(uint32_t *)o = game_va(SL_VTABLE);
    list_set_part(o, game_va(SL_PART_VTABLE));
}

// And its destructor, inlined: empty it as the list and as the base, and put back the global it saved.
static void list_unbuild(uint8_t *o) {
    struct game_strlist *l = (struct game_strlist *)o;
    *(uint32_t *)o = game_va(SL_VTABLE);
    list_set_part(o, game_va(SL_PART_VTABLE));
    strlist_remove_all(l);
    l->idx = 0;
    *(uint32_t *)o = game_va(SL_BASE_VTABLE);
    list_set_part(o, game_va(SL_BASE_PART));
    strlist_remove_all(l);
    l->idx = 0;
    *(uint32_t *)(o + 0x28) = game_va(SL_VB_VTABLE);
    *(uint32_t *)game_at(SL_HEAD) = *(uint32_t *)(o + 0x2c);
}

// Empties a list to make it again: the entries, the cursor's place and the arena word.
static void list_empty(struct game_strlist *l) {
    strlist_remove_all(l);
    l->idx = 0;
    *(uint32_t *)((uint8_t *)l + 0x18) = 0;
}

// ---- Construction ----------------------------------------------------------------------------------------

static void *UI_THIS prodpicker_ctor(struct game_prodpicker *pp) {
    graphic_win_ctor(&pp->win);
    for (int i = 0; i < 3; i++) sprite_ctor(&pp->sprite[i]);
    for (int i = 0; i < 4; i++) font_ctor(&pp->font[i]);
    time_ctor(&pp->timer);
    for (int i = 0; i < 9; i++) flat_button_ctor(&pp->button[i]);
    scroll_ctor((struct game_scroll *)pp->scroll);
    game_model_ctor(pp->model);
    for (int i = 0; i < 3; i++) list_build(pp->list[i]);
    pp->c830 = pp->c834 = 0x86;
    pp->opened = 0;
    pp->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pp->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    pp->cell_h_set = 0x6c;
    pp->c83c = 0x5e;
    return pp;
}

static void UI_THIS prodpicker_close(struct game_prodpicker *pp) {
    pp->opened = 0;
    graphic_win_close(&pp->win);
}

static void UI_THIS prodpicker_dtor(struct game_prodpicker *pp) {
    pp->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pp->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    prodpicker_close(pp);
    for (int i = 2; i >= 0; i--) list_unbuild(pp->list[i]);
    game_model_dtor(pp->model);
    scroll_dtor((struct game_scroll *)pp->scroll);
    for (int i = 8; i >= 0; i--) flat_button_dtor(&pp->button[i]);
    time_dtor(&pp->timer);
    for (int i = 3; i >= 0; i--) font_dtor(&pp->font[i]);
    for (int i = 2; i >= 0; i--) sprite_close(&pp->sprite[i]);
    graphic_win_dtor(&pp->win);
}

static void *UI_THIS prodpicker_scalar_dtor(struct game_prodpicker *pp, int flags) {
    prodpicker_dtor(pp);
    if (flags & 1) game_free(pp);
    return pp;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS prodpicker_operator_delete(struct game_buffer *buf, int flags) {
    return prodpicker_scalar_dtor((struct game_prodpicker *)((uint8_t *)buf - offsetof(struct game_prodpicker, buf)), flags);
}

// ---- Showing and hiding ------------------------------------------------------------------------------------

static void UI_THIS prodpicker_show(struct game_prodpicker *pp, int unused) {
    (void)unused;
    if (win_is_visible(&pp->win)) return;
    if (pp->mode) {
        void *w = game_at(0x6c2fd8);
        *(int32_t *)game_at(0x6c3a74) = 1;
        UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW);
    }
    if (!win_is_visible(game_at(BASEWIN))) game_slide_show(&pp->win, 0);
    if (pp->visible < pp->total) game_scroll_show(&pp->win, pp->scroll);
    game_win_show(&pp->win, 2);
}

static void UI_THIS prodpicker_hide(struct game_prodpicker *pp) {
    if (!win_is_visible(&pp->win)) return;
    if (pp->mode) {
        void *w = game_at(0x6c2fd8);
        *(int32_t *)game_at(0x6c3a74) = 0;
        UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW);
    }
    if (pp->visible < pp->total) game_scroll_hide(&pp->win, pp->scroll);
    if (!win_is_visible(game_at(BASEWIN))) game_slide_hide(&pp->win, 0);
    game_win_hide(&pp->win);
}

// ---- Sorting the choices -----------------------------------------------------------------------------------

static int UI_THIS prodpicker_edbc_unit(void *self, int proto) {
    (void)self;
    return prodpicker_unit_kind(proto, game_protos[proto].flags, game_protos[proto].plan);
}

static int UI_THIS prodpicker_edbc_fac(void *self, int fac) {
    (void)self;
    return prodpicker_facility_kind(fac);
}

static int UI_THIS prodpicker_unk1(void *self, int unused) {
    (void)self, (void)unused;
    return 1;
}

static void UI_THIS prodpicker_unk3(void *self, int unused) { (void)self, (void)unused; }

// ---- Making the list ---------------------------------------------------------------------------------------

static void UI_THIS prodpicker_calculate(struct game_prodpicker *pp) {
    int base_id = *game_current_base_id;
    for (int i = 0; i < 3; i++) list_empty(plist(pp, i));
    struct base *B = *game_current_base;
    pp->total = 0;
    pp->selected = -1;
    pp->first = 0;
    int current = B->queue[pp->mode];
    int last = B->production_last;
    game_proto_sort(pp->faction);

    char a[0x100], t[0x100], c[0x100], num[0x20];
    int prototype = 0;
    const int32_t *order = game_at(UNIT_ORDER);
    // The designs, in the game's order.
    for (int slot = 0; slot < 128; slot++) {
        int id = order[slot];
        if (!veh_avail(id, pp->faction, base_id)) continue;
        int kind_flags = 0;
        struct unit_proto *proto = &game_protos[id];
        a[0] = 0;
        strcat(a, proto->name);
        if (*game_state & GAME_STATE_OMNISCIENT) {
            strcat(a, text(S_OPEN));
            cat_num(a, game_factions[pp->faction].units_active[id]);
            strcat(a, text(S_COMMA));
            cat_num(a, ((uint8_t *)game_factions[pp->faction].units_lost)[id]);
            strcat(a, text(S_CLOSE));
        }
        t[0] = 0;
        int target = cost_factor(B->faction, 1, -1) * proto->cost;
        int per = cost_factor(B->faction, 1, -1);
        int total = per * veh_cost(id, base_id, &prototype);
        int surplus = B->surplus[1];
        if (surplus <= 0) {
            strcat(t, text(S_TURNS_OPEN));
            strcat(t, label_at(0x10c));
            strcat(t, text(S_TURNS_CLOSE));
        } else {
            if (pp->mode == 0) {
                int same = base_making(id, base_id) == base_making(last, base_id);
                int v = B->minerals_2;
                if (!same && game_factions[pp->faction].diff_level > 1) v = prodpicker_retool_stock(v, word(0x949830), word(0x94982c));
                target -= v;
                total -= v;
            }
            cat_num(t, prodpicker_turns(surplus, total, 0xf423f));
            if (target != total && proto->plan != 8) {
                strcat(t, text(S_OPEN));
                cat_num(t, prodpicker_turns(surplus, target, 0xf423f));
                strcat(t, text(S_CLOSE));
            }
            strcat(t, text(S_SPACE));
            strcat(t, label_at(0xc4));
        }
        c[0] = 0;
        game_say_stats_3(c, id);
        if (prototype) kind_flags = 0x80;
        int kind = prodpicker_unit_kind(id, proto->flags, proto->plan);
        list_add(plist(pp, 0), id, a, kind | kind_flags | 0x20);
        list_add(plist(pp, 1), 0, t, 0);
        list_add(plist(pp, 2), 0, c, 0);
        if (id == current) pp->selected = pp->total;
        pp->total++;
    }
    // The facilities, and the secret projects after them.
    for (int fac = 1; fac < 0x86; fac++) {
        if (!facility_avail(fac, B->faction, base_id, pp->mode)) continue;
        a[0] = 0;
        if (fac == 1) {
            strcat(a, text(S_OPEN));
            strcat(a, label_at(0x230));
            strcat(a, text(S_CLOSE));
            strcat(a, text(S_SPACE));
        }
        uint8_t *row = (uint8_t *)game_at(FAC_TABLE) + 0x30 * fac;
        strcat(a, name_of(*(int32_t *)row));
        t[0] = 0;
        int cost = cost_factor(B->faction, 1, -1) * *(int32_t *)(row + 0xc);
        if (pp->mode == 0) {
            int same = base_making(-fac, base_id) == base_making(last, base_id);
            if (!same && game_factions[pp->faction].diff_level > 1) cost -= B->minerals / 2;
            else cost -= B->minerals;
        }
        if (fac == 0x45) {
            int half = (B->surplus[1] + 1) / 2;
            half = half < 0 ? 0 : half > 0x270f ? 0x270f : half;
            snprintf(num, sizeof(num), "%d", half);
            strcat(t, num);
            strcat(t, text(S_SPACE));
            strcat(t, label_at(0x90));
            strcat(t, text(S_SPACE));
            strcat(t, label_at(0x264));
        } else if (B->surplus[1] <= 0) {
            strcat(t, text(S_NEVER_OPEN));
            strcat(t, label_at(0x10c));
            strcat(t, text(S_NEVER_CLOSE));
        } else {
            cat_num(t, prodpicker_turns(B->surplus[1], cost, -1));
            strcat(t, text(S_SPACE));
            strcat(t, label_at(0xc4));
        }
        c[0] = 0;
        if (strlen(name_of(*(int32_t *)(row + 4)))) strcat(c, name_of(*(int32_t *)(row + 4)));
        list_add(plist(pp, 0), fac, a, prodpicker_facility_kind(fac) | 0x10);
        list_add(plist(pp, 1), 0, t, 0);
        list_add(plist(pp, 2), 0, c, 0);
        if (-fac == current) pp->selected = pp->total;
        pp->total++;
    }
    if (!pp->total) return;

    pp->win_w = *(int32_t *)game_at(BASEWIN + 0x4c4);
    pp->win_h = -*(int32_t *)game_at(BASEWIN + 0x4c8);
    struct prodpicker_grid g;
    prodpicker_grid(&g, pp->win_w, *game_screen_width, pp->cell_h_set);
    pp->rows = g.rows;
    pp->cell_h = pp->cell_h_set;
    pp->cell_w = g.cell_w;
    pp->per_row = g.per_row;
    pp->visible = g.visible;
    int name_size, cost_size;
    prodpicker_font_sizes(g.cell_w, &name_size, &cost_size);
    font_init(&pp->font[0], *game_font_face, 0xc, 1);
    font_init(&pp->font[1], *game_font_face, name_size, 1);
    font_init(&pp->font[2], *game_font_face, cost_size, 0);
    font_init(&pp->font[3], *game_font_face, 0xe, 1);
}

// ---- Drawing ---------------------------------------------------------------------------------------------

// A cell of the grid: its frame, the choice's name above, its picture, the line of what it takes and the strengths at the foot.
static void UI_THIS prodpicker_draw_item(struct game_prodpicker *pp, int index, int hilite) {
    if (index < pp->first || index >= pp->first + pp->visible) return;
    struct prodpicker_grid g = grid_of(pp);
    struct ui_rect r;
    prodpicker_cell_rect(&r, &g, index - pp->first);
    struct ui_rect outer = r;
    struct game_buffer *b = &pp->buf;
    buffer_set_clip(b, &r.left);
    buffer_box_sprite(b, &r.left, game_at(pp->selected == index ? CARD_MARKED : CARD));
    if (index >= pp->total) return;
    r.left++, r.top++, r.right--, r.bottom--;

    struct game_strlist *la = plist(pp, 0), *lb = plist(pp, 1), *lc = plist(pp, 2);
    strlist_seek_pos(la, index);
    strlist_seek_pos(lc, index);
    strlist_seek_pos(lb, index);
    int flags = list_flags(la);
    int prod = list_id(la);
    if (!(flags & 0x20)) prod = -prod;
    game_battle_text[0] = 0;
    int colour = game_mandate_color(flags & 0xf);
    game_battle_text[1] = 0;
    buffer_set_text_color(b, colour, -1, 1, 1);
    buffer_set_font(b, &pp->font[0], NULL, NULL, NULL);

    // The name, at the top.
    int line0 = font_line(&pp->font[0]);
    struct ui_rect u = {r.left, r.top + 5, r.right, r.top + 3 * line0 + 5};
    buffer_wrap_cent_6(b, list_text(la), &u.left);
    // The strengths, at the foot, then the line of what it takes above them.
    buffer_set_text_color(b, 0xa1, -1, 1, 1);
    pp->font[2].first = 3;
    buffer_set_font(b, &pp->font[2], NULL, NULL, NULL);
    u = (struct ui_rect){r.left + 2, r.top, r.right - 2, r.bottom};
    char *strengths = list_text(lc);
    int h = buffer_wrap_height(b, strengths, u.right - u.left);
    u.top = u.bottom - h;
    buffer_wrap_cent_6(b, strengths, &u.left);
    u.bottom = u.top;
    u.top -= font_line(&pp->font[2]);
    char *turns = list_text(lb);
    if (turns) buffer_write_cent_l_7(b, turns, &u.left, (int)strlen(turns));

    if (prod >= 0) {
        // A unit: the game's drawer gets it ready and, when it answers yes, the game's 3D model draws it (answering no it draws nothing).
        int art = prodpicker_unit_art(pp->cell_h), model = prodpicker_unit_model(pp->cell_h);
        int fake = veh_fake(prod, (*game_current_base)->faction);
        int frame = pp->selected == index ? pp->anim : 0;
        int x, y;
        prodpicker_art_origin(&r, art, model, &x, &y);
        *(int32_t *)game_at(ICON_SCALE) = art;
        *(int32_t *)game_at(ICON_SCALE + 4) = 100;
        if (game_veh_draw(b, fake, x, y, 1, 0xcc, 0)) {
            *at32(pp, 0xa638) = 0;
            game_vehdraw_construct_vehicle(fake, pp->model);
            game_model_set_scale(pp->model, (float)art * real(0x66aa30));
            game_model_set_angle(pp->model, (float)(frame + 0x1e) * real(0x66aa2c), 0, 0);
            game_model_draw(pp->model, b, (r.right - r.left) / 2 + r.left, (r.bottom - r.top) / 2 + r.top, 0);
        }
        *(int32_t *)game_at(ICON_SCALE) = -1;
        *(int32_t *)game_at(ICON_SCALE + 4) = -1;
        int prototype = 0;
        veh_cost(prod, *game_current_base_id, &prototype);
        if (prototype) {
            buffer_set_font(b, &pp->font[2], NULL, NULL, NULL);
            buffer_set_text_color(b, 0xfa, -1, 1, 1);
            game_battle_text[0] = 0;
            strcat(game_battle_text, label_at(0x2e4));
            strcat(game_battle_text, text(S_PROTOTYPE_OPEN));
            cat_num(game_battle_text, prototype_factor(prod));
            strcat(game_battle_text, text(S_PROTOTYPE_CLOSE));
            buffer_write_cent_l_7(b, game_battle_text, &r.left, (int)strlen(game_battle_text));
        }
    } else {
        // A facility's picture, a seventh of the cell across.
        int fac = -prod;
        int s = prodpicker_facility_art(pp->cell_h);
        struct game_sprite *spr;
        int den;
        if (fac < 0x46) spr = (struct game_sprite *)game_at(FAC_PICS) + fac, den = word(FAC_PICS + 0x18);
        else spr = (struct game_sprite *)game_at(SECRET_PICS) + (fac - 0x46), den = word(SECRET_PICS + 0x18);
        int x, y;
        prodpicker_art_origin(&r, s, s, &x, &y);
        sprite_draw_mono(spr, b, spr->key, x, y, colour, s, den);
    }
    if (pp->selected == index) buffer_box_rect(b, &r.left, 0xff, 0xff);
    buffer_set_clip(b, b->full);
    if (hilite) graphic_win_soft_update_3(&pp->win, &outer.left);
}

static void UI_THIS prodpicker_on_redraw(struct game_prodpicker *pp) {
    for (int i = pp->first; i < pp->first + pp->visible; i++) prodpicker_draw_item(pp, i, 0);
}

// ---- Scrolling -----------------------------------------------------------------------------------------------

static void UI_THIS prodpicker_on_mousewheel(struct game_prodpicker *pp, int delta) {
    struct game_scroll *s = (struct game_scroll *)pp->scroll;
    if (delta < 0)
        for (int n = -delta; n > 0; n--) scroll_on_mousewheel_down(s, 1);
    else if (delta > 0)
        for (int n = delta; n > 0; n--) scroll_on_mousewheel_up(s, 1);
}

// The view moved to a row (the scroll bar's position): the picture below the rows moved is slid up or down a row and the row
// that came into view drawn; a longer jump draws everything again.
static void UI_THIS prodpicker_on_scrolled(struct game_prodpicker *pp, int unused, int pos) {
    (void)unused;
    int old_first = pp->first;
    int first = pp->per_row * pos;
    pp->first = first;
    int moved = first - old_first;
    if (moved < 0) moved = -moved;
    if (moved > pp->per_row) {
        UI_VCALL(void(UI_THIS *)(void *), pp, VT_REDRAW);
        graphic_win_soft_update_2(&pp->win);
        return;
    }
    struct game_buffer *b = &pp->buf;
    if (first < old_first) {
        buffer_set_clip(b, b->full);
        int w = pp->cell_w * pp->per_row + 0x17 + 2 - 0x15;
        int h = (pp->rows - 1) * pp->cell_h;
        buffer_copy_full(b, b, 0x15, 4, 0x15, pp->cell_h + 4, w, h);
        for (int i = pp->first; i < pp->per_row + pp->first; i++) prodpicker_draw_item(pp, i, 0);
    } else if (first > old_first) {
        buffer_set_clip(b, b->full);
        int w = pp->cell_w * pp->per_row + 0x17 + 2 - 0x15;
        int h = (pp->rows - 1) * pp->cell_h;
        buffer_copy_full(b, b, 0x15, pp->cell_h + 4, 0x15, 4, w, h);
        int last_row = (pp->rows - 1) * pp->per_row + pp->first;
        for (int i = last_row; i < pp->per_row + last_row; i++) prodpicker_draw_item(pp, i, 0);
    }
    graphic_win_soft_update_2(&pp->win);
}

static void UI_THIS prodpicker_on_scrolling(struct game_prodpicker *pp, int a, int b) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pp, 0xc0, a, b);
}

static void UI_THIS prodpicker_on_mouse_move(struct game_prodpicker *pp, int x, int y) { (void)pp, (void)x, (void)y; }
static void UI_THIS prodpicker_on_redraw_nc(struct game_prodpicker *pp, int a, int b) { (void)pp, (void)a, (void)b; }

// ---- The mouse ------------------------------------------------------------------------------------------------

// A click marks the cell under the mouse (the grid's own origin is not taken off).
static void UI_THIS prodpicker_on_left_click(struct game_prodpicker *pp, int x, int y) {
    struct prodpicker_grid g = grid_of(pp);
    int old = pp->selected;
    int sel = pp->first + prodpicker_click_slot(&g, x, y);
    pp->selected = sel;
    if (old == sel) return;
    pp->anim = 0;
    prodpicker_draw_item(pp, old, 0);
    prodpicker_draw_item(pp, pp->selected, 0);
    graphic_win_soft_update_2(&pp->win);
}

static void UI_THIS prodpicker_on_button_clicked(struct game_prodpicker *pp, int id);

static void UI_THIS prodpicker_on_left_double_click(struct game_prodpicker *pp, int x, int y) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pp, VT_LEFT_CLICK, x, y);
    UI_VCALL(void(UI_THIS *)(void *, int), pp, VT_BUTTON_CLICKED, pp->mode ? 5 : -1);
}

// A right click marks the cell and opens a menu at the mouse: hide the design from the lists (when it is one a faction may hide),
// show them all again, the Datalinks' page and the two buttons' (cancel, and, for the base's own production, another).
static void UI_THIS prodpicker_on_right_click(struct game_prodpicker *pp, int x, int y) {
    struct game_pulldown menu;
    pulldown_ctor(&menu);
    UI_VCALL(void(UI_THIS *)(void *, int, int), pp, VT_LEFT_CLICK, x, y);
    win_client_to_screen_2(&pp->win, &x, &y);
    struct game_strlist *la = plist(pp, 0);
    strlist_seek_pos(la, pp->selected);
    int proto = (list_flags(la) & 0x20) ? list_id(la) : -1;
    pulldown_init(&menu.win, 0);
    if (pp->mode == 0) game_pulldown_add_item(&menu, 5, label_at(0x1148));
    game_pulldown_add_item(&menu, 4, label_at(0x1144));
    int offense = proto >= 0 && proto < 0x40 ? game_weapons[game_protos[proto].weapon].offense : 0;
    if (prodpicker_menu_offers_hide(proto, offense)) game_pulldown_add_item(&menu, 1, label_at(0xe40));
    if (!*game_net_multiplayer) game_pulldown_add_item(&menu, 3, label_at(0x1140));
    game_pulldown_add_item(&menu, 2, label_at(0x8fc));
    game_pulldown_show(&menu, x, y, 0);
    int chosen = pulldown_get_selected(&menu);
    // (A menu left without a choice answers -1, and the original then reads the word before the first line.)
    int id = chosen < 0x40 ? *(int32_t *)((uint8_t *)&menu + 0xa20 + 20 * chosen) : -1;
    struct base *B = *game_current_base;
    switch (id) {
    case 1:
        game_protos[proto].obsolete_factions |= (uint8_t)(1u << B->faction);
        game_synch_proto(proto);
        prodpicker_calculate(pp);
        UI_VCALL(void(UI_THIS *)(void *), pp, VT_REDRAW);
        break;
    case 2: UI_VCALL(void(UI_THIS *)(void *, int), pp, VT_BUTTON_CLICKED, 0); break;
    case 3:
        for (int i = 0; i < PROTO_COUNT; i++)
            if (game_protos[i].flags & PROTO_FLAG_ACTIVE) game_protos[i].obsolete_factions &= (uint8_t)~(1u << B->faction);
        prodpicker_calculate(pp);
        UI_VCALL(void(UI_THIS *)(void *), pp, VT_REDRAW);
        break;
    case 4: UI_VCALL(void(UI_THIS *)(void *, int), pp, VT_BUTTON_CLICKED, 5); break;
    case 5: UI_VCALL(void(UI_THIS *)(void *, int), pp, VT_BUTTON_CLICKED, -1); break;
    default: break;
    }
    pulldown_dtor(&menu);
}

// ---- The keys ------------------------------------------------------------------------------------------------

static void scroll_to(struct game_prodpicker *pp, int pos) { scroll_set_pos((struct game_scroll *)pp->scroll, pos); }

// Always answers that the key was used. The first argument is the letter typed, the second the key code.
static int UI_THIS prodpicker_on_key_click(struct game_prodpicker *pp, int letter, int key) {
    (void)letter;
    struct game_scroll *sc = (struct game_scroll *)pp->scroll;
    int old_first = pp->first, old_sel = pp->selected;
    int first = old_first;
    int per_row = pp->per_row;
    switch (prodpicker_key_action(key)) {
    case PPK_PGUP:
        if (pp->total <= pp->visible) return 1;
        scroll_to(pp, sc->pos - sc->paging);
        pp->first = per_row * sc->pos;
        break;
    case PPK_PGDN:
        if (pp->total <= pp->visible) return 1;
        scroll_to(pp, sc->paging + sc->pos);
        pp->first = per_row * sc->pos;
        break;
    case PPK_END:
        pp->selected = pp->total - 1;
        if (pp->visible < pp->total) pp->first = ((pp->total - 1) / per_row + 1) * per_row - pp->visible;
        scroll_to(pp, pp->first / per_row);
        break;
    case PPK_HOME:
        scroll_to(pp, 0);
        pp->first = pp->selected = 0;
        break;
    case PPK_LEFT: pp->selected = prodpicker_step_sideways(old_sel, -1, per_row, pp->total); break;
    case PPK_RIGHT: pp->selected = prodpicker_step_sideways(old_sel, 1, per_row, pp->total); break;
    case PPK_UP: {
        if (old_sel < per_row) return 1;
        int sel = old_sel;
        do sel -= per_row;
        while (sel >= first + pp->visible);
        pp->selected = sel;
        if (sel < first) {
            pp->first = first - per_row;
            scroll_to(pp, sc->pos - 1);
        }
        break;
    }
    case PPK_DOWN: {
        if (old_sel > pp->total - per_row - 1) {
            if (pp->total <= pp->visible) return 1;
            scroll_to(pp, sc->pos + 1);
            pp->first = sc->pos * per_row;
            break;
        }
        int sel = old_sel + per_row;
        while (sel < first) sel += per_row;
        pp->selected = sel;
        if (sel >= first + pp->visible) {
            pp->first = per_row + first;
            scroll_to(pp, sc->pos + 1);
        }
        break;
    }
    default: break;
    }
    if (old_first != pp->first) {
        pp->anim = 0;
        UI_VCALL(void(UI_THIS *)(void *), pp, VT_REDRAW);
    } else if (old_sel != pp->selected) {
        pp->anim = 0;
        prodpicker_draw_item(pp, old_sel, 0);
        prodpicker_draw_item(pp, pp->selected, 0);
    }
    graphic_win_soft_update_2(&pp->win);
    return 1;
}

// ---- The buttons -----------------------------------------------------------------------------------------------

// A button was clicked (-2 Cancel, -1 OK or the double click, 0 Help, 1 Workshop, 2 to 7 the others: see the buttons in exec), or the
// window was left: which sets what exec answers with (`action`, a number a button gives, 0 for Cancel and OK). The window is released
// from its modal loop, and a window for a line of the queue is hidden for Cancel and OK (the others go on to a window of the base screen's),
// which does it itself.
static void UI_THIS prodpicker_on_button_clicked(struct game_prodpicker *pp, int id) {
    if (id + 2 > 9u) return;
    switch (id) {
    case -2:
        pp->action = 0;
        pp->selected = -1;
        break;
    case -1: pp->action = 0; break;
    case 0:
        if (pp->selected == -1) return;
        pp->action = 1;
        break;
    case 1: pp->action = 2; break;
    case 2:
        if (pp->selected == -1) return;
        pp->action = 7;
        break;
    case 3: pp->action = 3; break;
    case 4: pp->action = 4; break;
    case 5:
        if (pp->selected == -1) return;
        pp->action = 5;
        break;
    case 6: pp->action = 6; break;
    case 7: pp->action = 7; break;
    }
    UI_VCALL(void(UI_THIS *)(void *), pp, VT_RELEASE_MODAL);
    if (pp->mode && (id == -1 || id == -2)) UI_VCALL(void(UI_THIS *)(void *), pp, VT_HIDE);
}

// The timer's callback that nothing starts: it counts a turn of the picture and draws the choice again.
static void UI_CDECL prodpicker_unk2(int unused, struct game_prodpicker *pp) {
    (void)unused;
    if (!pp) return;
    pp->anim++;
    prodpicker_draw_item(pp, pp->selected, 1);
}

// ---- Opening the window and choosing ----------------------------------------------------------------------------

static void button_at(struct game_prodpicker *pp, int slot, int label_offset, int id, int x, int y) {
    game_battle_text[0] = 0;
    strcat(game_battle_text, label_at(label_offset));
    if (*game_language != 2) (*game_char_upper)(game_battle_text);
    game_base_button_init(&pp->button[slot], game_battle_text, id, x, y, pp->button_w, 0x14, pp, 0);
}

// Builds the lists' model of the window: the 3D model's set-up (a view, a scale and its two vectors).
static void model_start(struct game_prodpicker *pp) {
    game_model_reset(pp->model);
    *at32(pp, 0xa740) = 0;
    ((uint8_t *)pp)[0x9310] = 0;
    *at32(pp, 0xa744) = 0x3f48f5c3;
    *at32(pp, 0xa73c) = 0;
    game_model_aim(at32(pp, 0xa73c), at32(pp, 0xa748));
    float *v = (float *)at32(pp, 0xa760);
    v[0] = -v[0], v[1] = -v[1], v[2] = -v[2];
    float view[3] = {v[0] * real(0x66aa40), v[1] * real(0x66aa40), v[2] * real(0x66aa40)};
    game_model_set_view(pp->model, view, at32(pp, 0xa748));
    union {
        uint32_t u;
        float f;
    } tenth = {0x3dcccccd};
    game_model_set_scale(pp->model, tenth.f);
    int32_t *m = at32(pp, 0x92d8);
    m[0] = 0, m[1] = 0, m[2] = 0x3f800000;
    m = at32(pp, 0x92cc);
    m[0] = 0, m[1] = 0, m[2] = 0x3f800000;
}

static int UI_THIS prodpicker_exec(struct game_prodpicker *pp, void *base, int faction, void *unused, int mode) {
    (void)unused;
    struct game_buffer scratch, texture;
    buffer_ctor(&scratch);
    buffer_ctor(&texture);
    int change = 0;
    if (win_is_visible(&pp->win)) {
        if (mode ? pp->mode == 0 : pp->mode != 0) change = 1;
    } else {
        change = 2;
    }
    pp->mode = mode;
    struct game_box_frame frame;
    if (change) {
        if (change == 2) {
            buffer_init(&texture, 10, 10, 0, NULL);
            sprite_draw_2(game_at(0x6b33f8), &texture, *(uint8_t *)game_at(0x6b3400), 0, 0, 1, 1);
            memcpy(&frame, game_at(CARD), sizeof(frame));
            frame.fill = &texture;
            pp->faction = faction;
            pp->anim = 0;
            pp->base = (int32_t)(uintptr_t)base;
            model_start(pp);
            prodpicker_calculate(pp);
            if (win_is_visible(game_at(BASEWIN))) {
                graphic_win_init(&pp->win, 0, 0, pp->win_w, pp->win_h, NULL, 0x1004020, game_at(BASEWIN), NULL, NULL);
            } else {
                uint8_t *console = game_at(CONSOLE);
                void *parent = console + ((const int32_t *)(uintptr_t) * (uint32_t *)console)[1];
                graphic_win_init(&pp->win, word(0x6a7764), word(0x6a7768), pp->win_w, pp->win_h, NULL, 0x1000020, parent, NULL, NULL);
            }
            graphic_win_fill(&pp->win, 9);
            struct ui_rect r = {0x17, 0, pp->win_w - 0x4e + 0x17, pp->win_h};
            buffer_box_sprite(&pp->buf, &r.left, &frame);
            pp->button_w = 100;
            struct game_scroll *sc = (struct game_scroll *)pp->scroll;
            if (pp->visible < pp->total) {
                game_scroll_init(sc, pp->buf.width - 0x14, 0x15, 0x18, 0x13a, pp, 0, 1);
                scroll_set_range(sc, 0, pp->total / pp->per_row - pp->rows + 2);
                sc->paging = pp->rows - 1;
                sc->delay = 0;
                UI_VCALL(void(UI_THIS *)(void *), sc, VT_REDRAW);
                UI_VCALL(void(UI_THIS *)(void *), sc, VT_HIDE);
            }
            pp->first = 0;
            if (pp->selected >= pp->visible) {
                int f = 0;
                do {
                    f += pp->per_row;
                    pp->first = f;
                    scroll_to(pp, f / pp->per_row);
                } while (pp->selected >= pp->first + pp->visible);
            }
        }
        // The row of buttons.
        struct base *B = *game_current_base;
        pp->n_buttons = prodpicker_button_count(mode, (*game_human_factions & (1u << (faction & 31))) != 0, (int)*game_state, B->governor);
        int step = prodpicker_button_step(pp->button_w);
        int x = prodpicker_row_start(pp->buf.width, pp->button_w, pp->n_buttons);
        int y = pp->cell_h * pp->rows + 8;
        int right = x + pp->button_w, bottom = y + 0x14;
        button_at(pp, 0, 0x198, -2, x, y);
        x += step;
        if (mode) {
            button_at(pp, 3, 0x728, 4, x, y);
            x += step;
            button_at(pp, 2, 0x72c, 5, x, y);
            x += step;
            button_at(pp, 1, 0xfe8, 7, x, y);
            x += step;
            button_at(pp, 4, 0x194, -1, x, y);
            x += step;
        } else {
            uint32_t gov = B->governor;
            if (!game_is_human(B->faction) || ((gov & 1) && (gov & 0x80000000u) && (*game_state & 0x40))) {
                button_at(pp, 6, 0x4f4, 6, x, y);
                x += step;
            }
            button_at(pp, 2, 0x194, -1, x, y);
            x += step;
            button_at(pp, 4, 0x9b4, 1, x, y);
            x += step;
        }
        int last_x = x;
        button_at(pp, 5, 0x19c, 0, x, y);
        for (int i = 0; i < 8; i++) {
            struct game_flat_button *f = &pp->button[i];
            buffer_set_font(&f->base.buf, &pp->font[3], NULL, NULL, NULL);
            UI_VCALL(void(UI_THIS *)(void *), f, VT_REDRAW);
            button_set_text_color(&f->base, 0x41, -1, 1, 1);
            UI_VCALL(void(UI_THIS *)(void *), f, VT_REDRAW);
        }
        struct ui_rect r = {last_x - 3, y - 3, right + 3, bottom + 3};
        buffer_box_sprite(&pp->buf, &r.left, game_at(BOX_BUTTONS));
    }
    uint32_t *flags = game_at(BASEWIN_RIGHT);
    if (pp->mode) {
        for (uint8_t *p = (uint8_t *)flags; p < (uint8_t *)game_at(BASEWIN_RIGHT_END); p += 0xb4c) *(uint32_t *)p |= 8;
        UI_VCALL(void(UI_THIS *)(void *, int, int, int), pp, VT_SET_MODAL, 0, (int)game_va(MODAL_CALL), (int)game_va(MODAL_ARG));
        for (uint8_t *p = (uint8_t *)flags; p < (uint8_t *)game_at(BASEWIN_RIGHT_END); p += 0xb4c) *(uint32_t *)p &= ~8u;
    } else {
        UI_VCALL(void(UI_THIS *)(void *, int, int, int), pp, VT_SET_MODAL, 0, (int)game_va(MODAL_CALL), 0);
    }
    if (!mode) UI_VCALL(void(UI_THIS *)(void *), pp, VT_HIDE);

    int result;
    if (pp->selected == -1) {
        result = pp->action ? 0 : -9999;
    } else {
        struct game_strlist *la = plist(pp, 0);
        strlist_seek_pos(la, pp->selected);
        int id = list_id(la);
        result = (list_flags(la) & 0x20) ? id : -id;
    }
    buffer_dtor(&texture);
    buffer_dtor(&scratch);
    return result;
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook prodpicker_hooks[] = {
    UI_HOOK(0x421100, prodpicker_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x6c, 0x13, 0x65, 0x00),  // push -1; push handler
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x4213a0, prodpicker_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0x55, 0xfd, 0xff, 0xff),
    UI_HOOK(0x421970, prodpicker_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
    UI_HOOK(0x492420, prodpicker_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xff, 0x8b, 0x65, 0x00),
    // mov dword [ecx+0xa14], 0 (then a jump to graphic_win_close)
    UI_HOOK(0x4926b0, prodpicker_close, 0xc7, 0x81, 0x14, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    UI_HOOK_CALL(0x4926c0, prodpicker_show, 0x56, 0x8b, 0xf1, 0xe8, 0xc8, 0x57, 0x16, 0x00),  // push esi; mov esi, ecx; call Win_is_visible
    UI_HOOK_CALL(0x492740, prodpicker_hide, 0x56, 0x8b, 0xf1, 0xe8, 0x48, 0x57, 0x16, 0x00),
    UI_HOOK(0x4927c0, prodpicker_draw_item, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00),
    UI_HOOK(0x492fa0, prodpicker_calculate, 0x55, 0x8b, 0xec, 0x81, 0xec, 0x64, 0x04, 0x00, 0x00),
    UI_HOOK(0x493af0, prodpicker_edbc_unit, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x493b70, prodpicker_edbc_fac, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x493c10, prodpicker_unk1, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc2, 0x04, 0x00),  // mov eax, 1; ret 4
    UI_HOOK(0x493c20, prodpicker_on_mousewheel, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x493c70, prodpicker_on_redraw, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0xb7, 0x7c, 0xa7, 0x00, 0x00),
    UI_HOOK(0x493cb0, prodpicker_on_scrolled, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),
    UI_HOOK(0x493e70, prodpicker_on_scrolling, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x0c, 0x8b, 0x01),
    UI_HOOK(0x493e90, prodpicker_on_mouse_move, 0xc2, 0x08, 0x00, 0x90, 0x90),  // ret 8
    UI_HOOK(0x493ea0, prodpicker_on_right_click, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x14, 0x8c, 0x65, 0x00),
    UI_HOOK(0x4941c0, prodpicker_on_left_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c),
    UI_HOOK(0x494230, prodpicker_on_left_double_click, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08),
    UI_HOOK(0x494280, prodpicker_on_key_click, 0x55, 0x8b, 0xec, 0x51, 0x8b, 0x45, 0x0c),
    UI_HOOK(0x4945c0, prodpicker_on_redraw_nc, 0xc2, 0x08, 0x00, 0x90, 0x90),  // ret 8
    UI_HOOK(0x4945d0, prodpicker_on_button_clicked, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x4946d0, prodpicker_unk2, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c, 0x85, 0xc9),
    UI_HOOK(0x494700, prodpicker_exec, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x34, 0x8c, 0x65, 0x00),
    UI_HOOK(0x495160, prodpicker_unk3, 0xc2, 0x04, 0x00, 0x90, 0x90),  // ret 4
};

struct hook_module prodpicker_module = {"prodpicker", prodpicker_hooks, UI_HOOK_COUNT(prodpicker_hooks), NULL, NULL, -1};
