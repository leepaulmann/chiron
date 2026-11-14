// The research picker, the game's PickTech: the window tech_pick() opens to choose a tech to research (when a tech has
// been discovered, and when the player presses R to change what is being researched). It is not a global: tech_pick()
// builds one on its own stack (the constructor and destructor are called from there, and its own inlined ones are
// the original's), asks it to pick, and hands back the tech chosen. Nothing else touches the object.
//
// The window is a grid of cards, one per tech that may be chosen, each showing the tech's icon, its name and the
// number of the techs behind it, and (when there are eight or fewer) the things it makes possible, as small hint lines at
// the foot of the card. The mouse over a card marks it, a click chooses it, a right click opens the Datalinks at
// it, and the keys move the mark (the arrow keys, Tab, Backspace and the keypad numbers), choose (Return, Space) or look
// a letter up among the names.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=picktech gives them all back. The object keeps the original's layout, the toolkit is called directly, and
// what it draws, in what order, and what it writes are the original's.
//
// Game logic mixed with the screen. PickTech_pick is two things: the part that lists the techs that may be chosen
// (and, for a faction that no human plays, picks the best of them by tech_val) and the window that shows the list. The
// listing runs for every faction that calls tech_pick(), the AI's too (when it steals a tech: steal_tech() calls it
// with the victim's number), and it draws from the game's random numbers, so it has to stay exactly as it is: the
// original weighs every listed tech twice with random() (two rolls whose sums are then thrown away), and the rolls keep the
// generator in step. It also builds, for every listed tech, a line of what the tech makes possible and adds it to a
// pop-up's list (Dialogs_item), which nothing ever shows: the pop-up is a leftover of an earlier version of the screen.
// Both are kept, in the order the original does them. The listing is here, in `list_techs`, apart from the window; it is
// not in tech.c because it is welded to that dead dialog work and to the screen's own object (the list array and the count
// are the object's fields). Which techs it admits and the rolls are small pure functions of picktech.h, with a unit test.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the four pop-ups tech_pick's
// callee builds are built as one array and unbuilt in the reverse order by the game's own destructor (the original's
// inlined copies of it run the same calls); the many copies the original keeps of the strings "{", "}" and "+" are read
// from one address each; the unreachable code in draw_tech that would draw a unit's picture for techs past 0x60 is left
// out (a tech from 0x59 up returns after its heading). The scratch values the rolls leave are not kept.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "picktech.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/bufobj.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/sprite.h"
#include "../engine/strlist.h"
#include "../engine/tech.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

// The columns for a list of n techs (n up to 99; the original keeps them in a table it builds on its stack).
int picktech_columns(int count) {
    static const uint8_t cols[100] = {0, 1, 2, 3, 4, 5, 3, 4, 4, 5, 5, 6, 4, 5, 5, 5, 4, 5, 6, 5, 5, 7, 6, 6, 6, 5, 7, 7, 7,
                                      8, 8, 8, 8, 7, 7, 7, 8, 8, 8, 8, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                      10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                      10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
    return cols[count];
}

void picktech_grid(struct picktech_grid *g, int count, int screen_w, int screen_h) {
    g->cols = picktech_columns(count);
    g->rows = count / g->cols + (count % g->cols != 0);
    // A short list is as wide as the screen less a margin a column, a long one the screen less a margin.
    g->cell_w = count <= 8 ? screen_w / g->cols - 0x14 : (screen_w - 0x14) / g->cols;
    if (g->cell_w >= screen_w / 3) g->cell_w = screen_w / 3;
    g->cell_h = g->cell_w * 3 / 2;
    if (g->cell_h <= screen_h / 2) g->cell_h = screen_h / 2;
    if (g->cell_h * g->rows + 0x1d > screen_h) g->cell_h = count <= 8 ? screen_h / g->rows - 0x28 : (screen_h - 0x45) / g->rows;
    g->margin = 10;
    g->win_w = (g->cell_w + 1) * g->cols + 1;
    g->win_h = (g->cell_h + 1) * g->rows + 0x1e;
}

void picktech_cell_rect(struct ui_rect *r, const struct picktech_grid *g, int index) {
    int col = index % g->cols, row = index / g->cols;
    r->left = col * (g->cell_w + 1) + 1;
    r->right = r->left + g->cell_w;
    r->top = row * (g->cell_h + 1) + 0x1e;
    r->bottom = r->top + g->cell_h;
}

void picktech_cell_clip(struct ui_rect *r, int buf_w, int buf_h) {
    r->right -= 3, r->bottom -= 3;
    if (r->left <= 3) r->left = 3;
    if (r->right >= buf_w - 3) r->right = buf_w - 3;
    if (r->top <= 0x1d) r->top = 0x1d;
    if (r->bottom >= buf_h - 3) r->bottom = buf_h - 3;
}

int picktech_cell_at(const struct picktech_grid *g, int x, int y) {
    y -= 0x1d;
    if (x < 0 || y < 0 || x >= g->cols * (g->cell_w + 1) || y >= g->rows * (g->cell_h + 1)) return -1;
    return y / (g->cell_h + 1) * g->cols + x / (g->cell_w + 1);
}

enum picktech_key picktech_key_action(int key) {
    switch (key) {
    case 0x08: case 0x25: case 0x64: return PTK_LEFT;
    case 0x09: case 0x27: case 0x66: return PTK_RIGHT;
    case 0x26: case 0x68: return PTK_UP;
    case 0x28: case 0x62: return PTK_DOWN;
    case 0x0d: case 0x20: case 0x48: case 0x70: case 0x1000d: return PTK_CHOOSE;
    default: return PTK_NONE;
    }
}

void picktech_key_step(int *col, int *row, enum picktech_key action, int cols, int rows) {
    switch (action) {
    case PTK_LEFT:
        if (--*col < 0) *col = cols - 1;
        break;
    case PTK_RIGHT: *col = (*col + 1) % cols; break;
    case PTK_UP:
        if (--*row < 0) *row = rows - 1;
        break;
    case PTK_DOWN: *row = (*row + 1) % rows; break;
    default: break;
    }
}

void picktech_fonts(int count, int screen_w, int *size, int *hint_size) {
    if (count <= 8) *size = screen_w == 0x320 ? 0xc : 0xe, *hint_size = screen_w == 0x320 ? 0xe : 0x12;
    else *size = 10, *hint_size = 0xc;
}

int picktech_card_font(int count, int screen_w) { return count <= 8 ? (screen_w == 0x320 ? 0x12 : 0x14) : 0xc; }

int picktech_icon_step(int screen_w, int cell_w) {
    if (screen_w == 0x320) return cell_w >= 0xf0 ? -0xc : -0xe;
    if (cell_w >= 0xb4) return -0xa;
    return cell_w <= 0x8c ? -0xe : -0xc;
}

int picktech_icon_size(int scale_num, int scale_den, int step) {
    if (scale_num > 0) return 215 * scale_num / scale_den;
    int v = 215 * (step + 0x10);
    return (v + (v < 0 ? 15 : 0)) >> 4;
}

int picktech_roll_range(int s) { return 2 * s / 3 + 1; }
int picktech_roll_base(int s) { return (s + 2) / 3; }

int picktech_third_listed(int tech, int ranking, int faction) {
    return (tech - ranking + faction) % 3 != 0 || (tech < 7 && ranking == 0);
}

void picktech_break_line(char *out, const char *text, int limit, int (*width)(void *ctx, const char *s), void *ctx) {
    strcpy(out, text);
    if (width(ctx, text) <= limit || strchr(text, ' ')) return;
    if (width(ctx, out) > limit) {
        while (strlen(out)) {
            out[strlen(out) - 1] = 0;
            if (width(ctx, out) <= limit) break;
        }
    }
    strcat(out, " ");
    strcat(out, text + strlen(out) - 1);
}

// ---- The object -------------------------------------------------------------------------------------------

enum { LISTED_TECHS = 0x59 };

struct game_picktech {
    struct game_win win;     // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t cell_w;          // 0xa14: the width of a cell
    int32_t cell_h;          // 0xa18
    int32_t margin;          // 0xa1c: always 10
    int32_t faction;         // 0xa20: who is choosing
    int32_t cols;            // 0xa24
    int32_t rows;            // 0xa28
    int32_t count;           // 0xa2c: how many techs are listed
    GAME_PAD(0xa30, 0xa38);  // (never used)
    int32_t hover;           // 0xa38: the cell marked, or -1 (past the last tech too)
    int32_t hover_ok;        // 0xa3c: whether the marked cell is a tech (what drawing it answered)
    int32_t text_x;          // 0xa40: the working numbers draw_tech lays a card's text out with
    int32_t text_bottom;     // 0xa44: the foot of the card, which the hint lines are stacked up from
    int32_t text_w;          // 0xa48
    int32_t line_h;          // 0xa4c: the height of a line in the first font
    int32_t text_y;          // 0xa50: where the next piece of text goes
    int32_t hinted;          // 0xa54: set once the first hint line is put
    int32_t list[LISTED_TECHS];  // 0xa58: the techs listed, -1 where there are none
    struct font font[4];     // 0xbbc: the three styles of the small print and the hint font
};
GAME_AT(struct game_picktech, cell_w, 0xa14);
GAME_AT(struct game_picktech, faction, 0xa20);
GAME_AT(struct game_picktech, cols, 0xa24);
GAME_AT(struct game_picktech, count, 0xa2c);
GAME_AT(struct game_picktech, hover, 0xa38);
GAME_AT(struct game_picktech, hover_ok, 0xa3c);
GAME_AT(struct game_picktech, text_x, 0xa40);
GAME_AT(struct game_picktech, text_y, 0xa50);
GAME_AT(struct game_picktech, hinted, 0xa54);
GAME_AT(struct game_picktech, list, 0xa58);
GAME_AT(struct game_picktech, font, 0xbbc);
_Static_assert(sizeof(struct game_picktech) == 0xc5c, "layout of the game's PickTech");

// The slots of the window's vtable the screen uses (byte offsets).
enum {
    VT_SHOW = 0x4,
    VT_HIDE = 0x8,
    VT_RELEASE_CAPTURE = 0x10,
    VT_MOUSE_MOVE = 0x44,
    VT_RELEASE_MODAL = 0xe8,
    VT_SET_MODAL = 0xe4,
    VT_UPDATE = 0xfc,
};
// The vtables the constructor sets: the window's and the Buffer's.
enum { VTABLE_WIN = 0x66cfcc, VTABLE_BUF = 0x66cfc4 };

// The tech the cell holds, read as the original does, without a bound: the cell under the mouse may be past the list, and the
// words after the list are then the object's next fields.
static int32_t cell_tech(const struct game_picktech *pt, int cell) { return ((const int32_t *)pt->list)[cell]; }

// ---- The game's data it reads (by address) ---------------------------------------------------------------

static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static int16_t half(uint32_t addr) { return *(int16_t *)game_at(addr); }
static uint8_t byte(uint32_t addr) { return *(uint8_t *)game_at(addr); }
static char *text(uint32_t addr) { return game_at(addr); }
// A string the game keeps by its place in its string table.
static char *name_of(int32_t p) { return (char *)(uintptr_t)strings_get((struct game_strings *)game_strings, (unsigned)p); }

enum {
    COLOR_TEXT = 0x68a5a4,    // the card's text colour
    COLOR_BY_CATEGORY = 0x8c6de4,  // two words a category: the colour its icon is drawn in
    CATEGORY_NAME = 0x94b4a4,      // two words a category: its name
    TECH_ICONS = 0x759e28,    // a Sprite a tech
    TECH_TABLE = 0x94f35c,    // 0x2c bytes a tech: its name, then (at 0x20 and 0x24) the techs behind it
    ICON_SCALE = 0x691e6c,    // the game's own icon scale: a numerator and a denominator
    BOX_WINDOW = 0x78d690,    // box frames: the window's, the title's, and a card's (plain and marked)
    BOX_TITLE = 0x78d618,
    BOX_CARD = 0x78d528,
    BOX_CARD_MARKED = 0x78d578,
    POPUP_FILE = 0x9b8aa8,    // where the pop-up's script is read from (empty)
    CONSOLE = 0x9156b0,
    FX = 0x749cf8,
    DATALINKS = 0x703ea0,
    POPT_SCRIPT = 0x691b14,   // char *: the pop-up script the game's messages are read from
    PREFERENCES = 0x9a6490,
    SCREEN_WIDTH_800 = 0x320,
};
// Strings.
enum {
    S_SPACE = 0x682820,
    S_OPEN = 0x682e9c,  // "("
    S_CLOSE = 0x682e98,
    S_COLON = 0x682e94,
    S_TECHNOLOGY_NEW = 0x686a30,  // the pop-up's label, in three places
    S_TECHNOLOGY_SOON = 0x686a3c,
    S_TECHNOLOGY = 0x686a48,
    S_BANG = 0x686a54,   // "(!)": a tech whose first steps this faction has not yet
    S_BAR = 0x686a58,
    S_PLUS = 0x686a60,   // (the game has a copy at each place it is used; they are all the same)
    S_LBRACKET = 0x6869b8,
    S_RBRACKET = 0x6869bc,
    S_LBRACE = 0x6869c8,
    S_RBRACE = 0x6869cc,
    S_TECH = 0x6869a0,
    S_SHORTS = 0x6869a8,
};
// Entries of the labels file are given by their byte offset in the table, as the original reads them.
static char *label_at(int byte_offset) { return game_label(byte_offset / 4); }

static void cat(const char *s) { strcat(game_battle_text, s); }
static void cat_num(int v) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", v);
    cat(digits);
}

static int font_line(const struct font *f) { return f->first >= 0 ? f->height + f->first : f->ascent; }

// ---- Construction ----------------------------------------------------------------------------------------

static void *UI_THIS picktech_ctor(struct game_picktech *pt) {
    graphic_win_ctor(&pt->win);
    for (int i = 0; i < 4; i++) font_ctor(&pt->font[i]);
    pt->cols = 0, pt->rows = 0, pt->hover = 0, pt->hover_ok = 0;
    pt->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pt->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return pt;
}

static void UI_THIS picktech_close(struct game_picktech *pt) {
    pt->cols = 0, pt->rows = 0, pt->hover = 0, pt->hover_ok = 0;
    graphic_win_close(&pt->win);
}

static void UI_THIS picktech_dtor(struct game_picktech *pt) {
    pt->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pt->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    pt->cols = 0, pt->rows = 0, pt->hover = 0, pt->hover_ok = 0;
    graphic_win_close(&pt->win);
    for (int i = 3; i >= 0; i--) font_dtor(&pt->font[i]);
    graphic_win_dtor(&pt->win);
}

static void *UI_THIS picktech_scalar_dtor(struct game_picktech *pt, int flags) {
    picktech_dtor(pt);
    if (flags & 1) game_free(pt);
    return pt;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS picktech_operator_delete(struct game_buffer *buf, int flags) {
    return picktech_scalar_dtor((struct game_picktech *)((uint8_t *)buf - offsetof(struct game_picktech, buf)), flags);
}

// ---- Drawing ---------------------------------------------------------------------------------------------

// A line at the foot of a card, stacked upward from the bottom: the first is set in the hint font, the others in the
// card's small print, and a line that no longer fits above the text already there is left out.
static void UI_THIS picktech_put_hint(struct game_picktech *pt, char *line) {
    struct game_buffer *b = &pt->buf;
    int height;
    if (!pt->hinted) {
        struct font *f = &pt->font[3];
        buffer_set_font(b, f, f, f, NULL);
        pt->hinted = 1;
        height = font_line(f);
    } else {
        buffer_set_font(b, &pt->font[0], &pt->font[1], &pt->font[2], NULL);
        height = font_line(&pt->font[0]);
    }
    if (pt->text_bottom - height >= pt->text_y) {
        int h = buffer_wrap_height(b, line, pt->text_w - 2);
        pt->text_bottom -= h;
        buffer_wrap_cent_4(b, line, pt->text_x + 1, pt->text_bottom, pt->text_w - 2);
    }
}

static int text_width_of(void *ctx, const char *s) { return buffer_text_width(ctx, s); }

// A hint line: the opening and closing marks round the name are put only before the first (the original asks; its first
// line is always already counted, so they never are).
static void hint_name(struct game_picktech *pt, int32_t name) {
    game_battle_text[0] = 0;
    if (!pt->hinted) cat(text(S_LBRACE));
    cat(name_of(name));
    if (!pt->hinted) cat(text(S_RBRACE));
    picktech_put_hint(pt, game_battle_text);
}

// A card's text, set up as a StringList the game's own loader reads the short description from: an object the original
// builds in place (a list with its virtual base, which keeps the game's list head and clears it).
enum { SL_VTABLE = 0x6698c4, SL_PART_VTABLE = 0x6698c0, SL_BASE_VTABLE = 0x6693a4, SL_BASE_PART = 0x6693a0, SL_VBPTR = 0x66b0ec,
       SL_VB_VTABLE = 0x6693ac, SL_HEAD = 0x9b3374 };

static void list_set_part(uint8_t *o, uint32_t vtable) {
    const int32_t *vbtable = (const int32_t *)(uintptr_t)((struct game_strlist *)o)->vbptr;
    *(uint32_t *)(o + 4 + vbtable[1]) = vtable;
}

static void description_open(uint8_t *sl) {
    memset(sl, 0, 0x40);
    uint32_t *head = game_at(SL_HEAD);
    *(uint32_t *)(sl + 4) = game_va(SL_VBPTR);
    *(uint32_t *)(sl + 0x28) = game_va(SL_VB_VTABLE);
    *(uint32_t *)(sl + 0x2c) = *head;
    *head = 0;
    *(uint32_t *)(sl + 0x30) = 1;
    strlist_ctor((struct game_strlist *)sl, 0);
    *(uint32_t *)sl = game_va(SL_VTABLE);
    list_set_part(sl, game_va(SL_PART_VTABLE));
}

static void description_close(uint8_t *sl) {
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

// What a tech makes possible, as the hint lines at the foot of its card, in the order the original goes through the game's
// tables. Each table is walked by its address and stride, and a line is put for every entry that names the tech.
static void card_hints(struct game_picktech *pt, int tech) {
    char *bt = game_battle_text;
    char buf[0x100];

    // The techs that need this one (their names in brackets).
    for (uint32_t p = 0x94f380; p < 0x9502cc; p += 0x2c) {
        if (word(p - 4) != tech && word(p) != tech) continue;
        bt[0] = 0;
        cat(text(S_LBRACKET)), cat(name_of(word(p - 0x24))), cat(text(S_RBRACKET));
        picktech_put_hint(pt, bt);
    }
    // The facilities it lets a faction build (not the secret projects), with what each is.
    for (uint32_t p = 0x9a4b98; p < 0x9a6488; p += 0x30) {
        if (word(p + 0x18) != tech || p >= 0x9a5888) continue;
        bt[0] = 0;
        if (!pt->hinted) cat(text(S_LBRACE));
        cat(name_of(word(p))), cat(text(S_SPACE)), cat(label_at(0x4dc));
        if (!pt->hinted) cat(text(S_RBRACE));
        picktech_put_hint(pt, bt);
    }
    // Reactors, abilities, and the abilities' groups.
    for (uint32_t p = 0x946020; p < 0x9460e4; p += 0x1c)
        if (word(p + 8) == tech) hint_name(pt, word(p));
    for (uint32_t p = 0x9ab538; p < 0x9ab864; p += 0x1c)
        if (half(p + 0x18) == tech) hint_name(pt, word(p));
    for (int g = 0; g < 19; g++)
        for (int e = 0; e < 2; e++) {
            uint32_t base = 0x691880 + 0x20 * g;
            if (word(0x691880 + (8 * g + e) * 4) != tech) continue;
            if (e == 1 && word(base) == word(base + 4)) continue;
            hint_name(pt, word(0x96c8a8 + (3 * g + e) * 4));
        }
    // The three things the game's own numbers name: each in braces.
    static const struct {
        uint32_t tech_word;
        int label;
    } specials[] = {{0x94978c, 0x460}, {0x949788, 0x45c}, {0x949784, 0x458}};
    for (int i = 0; i < 3; i++) {
        if (tech != word(specials[i].tech_word)) continue;
        bt[0] = 0;
        cat(text(S_LBRACE)), cat(label_at(specials[i].label)), cat(text(S_RBRACE));
        picktech_put_hint(pt, bt);
    }
    // What this tech makes obsolete: facilities (a project's name alone, a facility's with its short name), and the modules.
    for (uint32_t p = 0x9a4b98; p < 0x9a6488; p += 0x30) {
        if (word(p + 0x14) != tech) continue;
        bt[0] = 0;
        if (p < 0x9a5888 || !pt->hinted) cat(text(S_LBRACE));
        cat(name_of(word(p)));
        if (p < 0x9a5888) {
            cat(text(S_SPACE)), cat(text(S_OPEN)), cat(name_of(word(p + 4))), cat(text(S_CLOSE));
            cat(text(S_RBRACE));
        } else if (!pt->hinted) {
            cat(text(S_RBRACE));
        }
        picktech_put_hint(pt, bt);
    }
    for (uint32_t row = 0x94b014; row < 0x94b364; row += 0xd4)
        for (int i = 0; i < 4; i++) {
            uint32_t p = row + 4 * i;
            if (word(p - 0x10) != tech) continue;
            bt[0] = 0;
            cat(text(S_LBRACE)), cat(name_of(word(p))), cat(text(S_RBRACE));
            picktech_put_hint(pt, bt);
        }
    // Chassis: name, then (kind and speed).
    for (uint32_t p = 0x94a330; p < 0x94a840; p += 0x90) {
        if (half(p + 0x8e) != tech) continue;
        bt[0] = 0;
        cat(text(S_LBRACE)), cat(name_of(word(p))), cat(text(S_SPACE)), cat(text(S_OPEN));
        cat(name_of(word(0x94f1a8 + 4 * byte(p + 0x49)))), cat(text(S_SPACE));
        cat_num(byte(p + 0x48));
        cat(text(S_CLOSE)), cat(text(S_RBRACE));
        picktech_put_hint(pt, bt);
    }
    // Armour: name, then (strength), or a label alone for one that has a negative value.
    for (uint32_t p = 0x94f280; p < 0x94f360; p += 0x10) {
        if (half(p + 4) != tech) continue;
        bt[0] = 0;
        cat(text(S_LBRACE)), cat(name_of(word(p - 8))), cat(text(S_SPACE)), cat(text(S_OPEN));
        int8_t v = (int8_t)byte(p);
        if (v < 0) {
            cat(label_at(0x310));  // (and nothing closes it)
        } else {
            cat_num(v);
            cat(text(S_CLOSE)), cat(text(S_RBRACE));
        }
        picktech_put_hint(pt, bt);
    }
    // Weapons: name, then (strength, or for a value of 0 the first letter of a label and a number).
    for (uint32_t p = 0x94ae68; p < 0x94b008; p += 0x10) {
        if (half(p + 4) != tech) continue;
        bt[0] = 0;
        cat(text(S_LBRACE)), cat(name_of(word(p - 8))), cat(text(S_SPACE)), cat(text(S_OPEN));
        int8_t v = (int8_t)byte(p);
        if (v == 0) {
            buf[0] = 0;
            strcat(buf, label_at(0x200));
            buf[1] = 0;
            cat(buf), cat(text(S_SPACE));
            cat_num(byte(p + 3));
        } else if (v < 0) {
            cat(label_at(0x310));
        } else {
            cat_num(v);
        }
        cat(text(S_CLOSE)), cat(text(S_RBRACE));
        picktech_put_hint(pt, bt);
    }
    // Terrain improvements: name, then (the first letter of a label, a colon and the number).
    for (int i = 0; i < 4; i++) {
        uint32_t p = 0x9527f8 + 0xc * i;
        if (half(p + 8) != tech) continue;
        bt[0] = 0;
        cat(name_of(word(p))), cat(text(S_SPACE)), cat(text(S_OPEN));
        buf[0] = 0;
        strcat(buf, label_at(0x13c));
        buf[1] = 0;
        cat(buf), cat(text(S_COLON));
        cat_num(i + 1);
        cat(text(S_CLOSE));
        picktech_put_hint(pt, bt);
    }
    // Unit designs the tech makes possible ({label: name}).
    for (uint32_t p = 0x9ab89a; p < 0x9ac59a; p += 0x34) {
        if (!(byte(p - 2) & 1) || half(p) != tech) continue;
        bt[0] = 0;
        cat(text(S_LBRACE)), cat(label_at(0x314)), cat(text(S_COLON)), cat((char *)game_at(p - 0x32)), cat(text(S_RBRACE));
        picktech_put_hint(pt, bt);
    }
}

// One tech's card, in the rectangle given: the heading, then (a short list of up to eight) the name's category with the
// number of the techs behind it, the icon, the game's short description of it and the hint lines. Answers 0 for no tech.
static int UI_THIS picktech_draw_tech(struct game_picktech *pt, int tech, const int32_t *rect, int hilite) {
    (void)hilite;
    if (tech < 0) return 0;
    struct game_buffer *b = &pt->buf;
    char *bt = game_battle_text;
    char line[0x100], digits[0x50];

    pt->hinted = 0;
    int ink = word(COLOR_TEXT);
    buffer_set_text_color(b, ink, 0, 1, 1);
    buffer_set_text_color2(b, ink, 0, 1, 1);
    pt->text_w = rect[2] - rect[0];
    pt->line_h = font_line(&pt->font[0]);
    pt->text_x = rect[0];
    pt->text_y = pt->margin + rect[1];
    pt->text_bottom = rect[3] - pt->margin / 2;
    bt[0] = 0;

    int size = picktech_card_font(pt->count, *game_screen_width);
    struct font *f2 = game_find_font(size, 2);
    struct font *f1 = game_find_font(size, 1);
    struct font *f0 = game_find_font(size, 1);
    buffer_set_font(b, f0, f1, f2, NULL);

    say_tech(bt, tech, 0);
    if (tech == 0x58) {
        cat(text(S_SPACE));
        snprintf(digits, sizeof(digits), "%d", game_factions[pt->faction].transcendent_techs + 1);
        cat(digits);
    }
    picktech_break_line(line, bt, pt->text_w - 2, text_width_of, b);

    int category = tech_category(tech);
    int depth = tech_recurse(tech, 0);
    if (pt->count > 8) {
        buffer_set_text_color(b, game_mandate_color(category), 0, 1, 1);
        int w = rect[2] - rect[0] - 4;
        int h = buffer_wrap_height(b, line, w);
        pt->text_y = buffer_wrap_cent_4(b, line, rect[0] + 2, rect[1] + (rect[3] - rect[1] - h) / 2, w);
        return 1;
    }
    pt->text_y = buffer_wrap_cent_4(b, line, pt->text_x + 2, pt->text_y, pt->text_w - 6);

    buffer_set_font(b, &pt->font[0], &pt->font[1], &pt->font[2], NULL);
    buffer_set_text_color(b, game_mandate_color(category), 0, 1, 1);
    bt[0] = 0;
    cat(name_of(word(CATEGORY_NAME + 8 * category))), cat(text(S_SPACE)), cat_num(depth);
    pt->text_y = buffer_wrap_cent_4(b, bt, pt->text_x + 1, pt->text_y, pt->text_w - 2);
    buffer_set_text_color(b, ink, 0, 1, 1);
    buffer_set_text_color2(b, 0x5f, 0, 1, 1);

    // The icon, scaled by the game's own numbers or one a step down from full size.
    int step = picktech_icon_step(*game_screen_width, pt->cell_w);
    if (tech < LISTED_TECHS) {
        pt->text_y += pt->margin;
        int num = word(ICON_SCALE), den = word(ICON_SCALE + 4);
        if (num > 0) game_sprite_scale[0] = num, game_sprite_scale[1] = den;
        else game_sprite_scale[1] = 0x10, game_sprite_scale[0] = step + 0x10;
        int size_px = picktech_icon_size(num, den, step);
        int colour = word(COLOR_BY_CATEGORY + 8 * tech_category(tech));
        struct game_sprite *icon = (struct game_sprite *)game_at(TECH_ICONS) + tech;
        sprite_draw_mono_2(icon, b, icon->key, rect[0] + (pt->text_w - size_px) / 2, pt->text_y, colour);
        pt->text_y += picktech_icon_size(word(ICON_SCALE), word(ICON_SCALE + 4), step) + pt->margin;
        game_sprite_scale[0] = 1, game_sprite_scale[1] = 1;
    }
    // (The original goes on, for a tech past 0x60, to draw a unit's picture; it never gets there.)
    if (tech >= LISTED_TECHS) return 1;

    // The short description, read from a file of them by the game's StringList.
    uint8_t sl[0x40] __attribute__((aligned(4)));
    description_open(sl);
    bt[0] = 0;
    cat(text(S_TECH)), cat_num(tech);
    if (!game_stringlist_load(sl, text(S_SHORTS), bt, 1, 0)) {
        pt->text_y = buffer_write_strings(b, (struct str_list *)sl, pt->text_x + 4, pt->text_y, pt->text_w - 8, 0);
        strlist_remove_all((struct game_strlist *)sl);
        ((struct game_strlist *)sl)->idx = 0;
    }
    pt->hinted = 1;
    card_hints(pt, tech);
    description_close(sl);
    return 1;
}

// A cell of the grid: the frame (marked or plain) and its tech's card, clipped to the cell. Answers what the card did.
static int UI_THIS picktech_draw_item(struct game_picktech *pt, int index, int marked) {
    if (index >= pt->count) return 0;
    struct picktech_grid g = {.cols = pt->cols, .rows = pt->rows, .cell_w = pt->cell_w, .cell_h = pt->cell_h, .margin = pt->margin};
    struct ui_rect r;
    picktech_cell_rect(&r, &g, index);
    struct game_buffer *b = &pt->buf;
    buffer_set_clip(b, &r.left);
    picktech_cell_clip(&r, b->width, -b->neg_height);
    buffer_box_sprite(b, &r.left, game_at(marked ? BOX_CARD_MARKED : BOX_CARD));
    int answer = picktech_draw_tech(pt, pt->list[index], &r.left, marked);
    buffer_set_clip(b, b->full);
    return answer;
}

// ---- Input -----------------------------------------------------------------------------------------------

static void update(struct game_picktech *pt) { UI_VCALL(void(UI_THIS *)(void *), pt, VT_UPDATE); }
static void release_modal(struct game_picktech *pt) {
    if (*game_win_capture == &pt->win) {
        *game_win_capture = NULL;
        UI_VCALL(void(UI_THIS *)(void *), pt, VT_RELEASE_CAPTURE);
    }
    if (*game_win_hover == &pt->win) *game_win_hover = NULL;
    UI_VCALL(void(UI_THIS *)(void *), pt, VT_RELEASE_MODAL);
}

// Marks the cell under the mouse (and unmarks the one before it); outside the grid, unmarks.
static void UI_THIS picktech_on_mouse_move(struct game_picktech *pt, int x, int y) {
    struct picktech_grid g = {.cols = pt->cols, .rows = pt->rows, .cell_w = pt->cell_w, .cell_h = pt->cell_h, .margin = pt->margin};
    int cell = picktech_cell_at(&g, x, y);
    if (cell >= 0) {
        if (cell == pt->hover && pt->hover_ok) return;
        pt->hover_ok = picktech_draw_item(pt, cell, 1);
        if (pt->hover >= 0 && pt->hover != cell) picktech_draw_item(pt, pt->hover, 0);
        pt->hover = cell;
        update(pt);
        return;
    }
    if (pt->hover >= 0) {
        picktech_draw_item(pt, pt->hover, 0);
        graphic_win_soft_update_2(&pt->win);
    }
    pt->hover_ok = 0;
    pt->hover = -1;
}

// A click on a card ends the window (the mouse is first moved there, as a click does).
static void UI_THIS picktech_on_left_click(struct game_picktech *pt, int x, int y) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pt, VT_MOUSE_MOVE, x, y);
    if (pt->hover_ok) release_modal(pt);
}

// A right click opens the Datalinks at the card's tech, over the window, which is hidden and shown again.
static void UI_THIS picktech_on_right_click(struct game_picktech *pt, int x, int y) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pt, VT_MOUSE_MOVE, x, y);
    if (!pt->hover_ok) return;
    UI_VCALL(void(UI_THIS *)(void *), pt, VT_HIDE);
    game_do_all_draws();
    game_datalink_exec(game_at(DATALINKS), 0xe, cell_tech(pt, pt->hover));
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), pt, VT_SET_MODAL, 0, 0, 0);
}

// Keys move the mark, choose, or look a letter up among the names; always answers that the key was used. The first
// argument is the letter typed (0 for none), the second the key code.
static int UI_THIS picktech_on_key_click(struct game_picktech *pt, int letter, int key) {
    int old = pt->hover;
    int col = old % pt->cols, row = old / pt->cols;
    enum picktech_key action = picktech_key_action(key);
    if (action == PTK_CHOOSE) {
        if (old < 0 || old >= pt->count) {
            if (old >= 0) picktech_draw_item(pt, old, 0);
            pt->hover = 0;
            picktech_draw_item(pt, 0, 1);
            update(pt);
        }
        if (cell_tech(pt, pt->hover) < 0) return 1;
        if (key == 0x70 || key == 0x48) game_open_help();
        else release_modal(pt);
        return 1;
    }
    picktech_key_step(&col, &row, action, pt->cols, pt->rows);
    int cell = pt->cols * row + col;
    if (old < 0) pt->hover = 0;
    // A letter jumps to the first card whose name begins with it.
    if (letter != 0 && pt->count > 0)
        for (int i = 0; i < pt->count; i++) {
            game_battle_text[0] = 0;
            say_tech(game_battle_text, pt->list[i], 0);
            if (toupper(letter) == (int)(signed char)game_battle_text[0]) {
                cell = i;
                break;
            }
        }
    if (cell != pt->hover) {
        pt->hover_ok = picktech_draw_item(pt, cell, 1);
        if (pt->hover >= 0) picktech_draw_item(pt, pt->hover, 0);
        pt->hover = cell;
        update(pt);
    }
    return 1;
}

// ---- Choosing ----------------------------------------------------------------------------------------------

enum { POPUP_SIZE = 0x537c, POPUP_DIALOGS = 0x21d0, POPUP_TITLE = 0xa48, POPUP_ONLY = 0xa44 };

// What the listing leaves behind for the caller.
struct pick_result {
    int best;  // the tech tech_val() weighs highest (-1 if none)
};

// Lists the techs that may be chosen into the object's array, category by category (four passes), and keeps the best by
// tech_val. `other` is the faction a tech is being taken from (-1 for none): its techs this faction lacks and can take;
// `blind` and `flag` are the rules' third-of-the-techs filter. Each listed tech is rolled for twice, its line of text
// built and handed to the pop-up's Dialogs (see the file's header).
static int list_techs(struct game_picktech *pt, uint8_t *popup, int faction, int flag, int other) {
    char digits[0x50];
    int accepted = 0, best_val = -1, best_tech = -1;
    for (int category = 0; category < 4; category++) {
        for (int t = 0; t < LISTED_TECHS; t++) {
            if (other >= 0) {
                if (has_tech(t, faction) || !has_tech(t, other)) continue;
                if (!has_tech(word(0x94f37c + 0x2c * t), faction) && !has_tech(word(0x94f380 + 0x2c * t), faction)) continue;
            } else {
                if (!tech_avail(t, faction)) continue;
                if (accepted && !flag && !picktech_third_listed(t, game_factions[faction].tech_ranking, faction)) continue;
            }
            if (tech_category(t) != category) continue;
            accepted++;

            // Two rolls, which only keep the random numbers in step.
            int s = tech_tech(t) + tech_mil(t) * 2;
            int r = game_random(0, picktech_roll_range(s));
            (void)(r + picktech_roll_base(s));
            s = tech_tech(t) + (tech_colonize(t) + tech_infra(t)) * 2;
            r = game_random(0, picktech_roll_range(s));
            (void)(r + picktech_roll_base(s));

            pt->list[pt->count++] = t;
            int val = tech_val(t, faction, 0);
            if (val >= best_val) best_val = val, best_tech = t;

            // The tech's line for the pop-up: its name (the number of the next transcendent tech for 0x58), a mark for a
            // tech whose first steps the faction lacks, then what it makes possible.
            char *bt = game_battle_text;
            bt[0] = 0;
            say_tech(bt, t, 0);
            if (t == 0x58) {
                cat(text(S_SPACE));
                snprintf(digits, sizeof(digits), "%d", game_factions[faction].transcendent_techs + 1);
                cat(digits);
            }
            if (other == 0 && (!has_tech(word(0x94f37c + 0x2c * t), faction) || !has_tech(word(0x94f380 + 0x2c * t), faction)))
                cat(text(S_BANG));
            if (t < LISTED_TECHS) {
                cat(text(S_BAR));
                int any = 0;
                // Each item is "(" and its name in a pair of marks for the first, a "+" for every one after it.
                if (t == word(0x949784)) any = 1, cat(text(S_OPEN)), cat(label_at(0x458)), cat(text(S_CLOSE));
                if (t == word(0x949788)) {
                    if (any) cat(text(S_PLUS));
                    else any = 1, cat(text(S_OPEN)), cat(label_at(0x45c)), cat(text(S_CLOSE));
                }
                if (t == word(0x94978c)) {
                    if (any) cat(text(S_PLUS));
                    else any = 1, cat(text(S_OPEN)), cat(label_at(0x460)), cat(text(S_CLOSE));
                }
                // Unit designs, with their kind.
                for (uint32_t p = 0x9ab89a; p < 0x9ac59a; p += 0x34) {
                    if (!(byte(p - 2) & 1) || half(p) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), cat(label_at(0x314)), cat(text(S_COLON)), cat((char *)game_at(p - 0x32)), cat(text(S_CLOSE)), any = 1;
                }
                // Terrain improvements.
                for (uint32_t p = 0x9527f8; p < 0x952828; p += 0xc) {
                    if (half(p + 8) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), cat(label_at(0x13c)), cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE)), any = 1;
                }
                // Weapons, armour, chassis.
                for (uint32_t p = 0x94ae68; p < 0x94b008; p += 0x10) {
                    if (half(p + 4) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else {
                        cat(text(S_OPEN)), any = 1;
                        cat(game_label(0x80 + (byte(p) != 0))), cat(text(S_COLON)), cat(name_of(word(p - 8))), cat(text(S_CLOSE));
                    }
                }
                for (uint32_t p = 0x94f278; p < 0x94f358; p += 0x10) {
                    if (half(p + 0xc) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), any = 1, cat(label_at(0x154)), cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE));
                }
                for (uint32_t p = 0x94a330; p < 0x94a840; p += 0x90) {
                    if (half(p + 0x8e) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), any = 1, cat(label_at(0x140)), cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE));
                }
                // Facilities (the secret projects only while not yet built), then the groups of abilities.
                for (uint32_t p = 0x9a4b98, built = 0x9a6400; built < 0x9a6614; p += 0x30, built += 4) {
                    if (word(p + 0x14) != t) continue;
                    if (any) {
                        cat(text(S_PLUS));
                        continue;
                    }
                    if (built >= 0x9a6514 && word(built) != -1) continue;
                    cat(text(S_OPEN));
                    cat(label_at(built >= 0x9a6514 ? 0x2cc : 0x26c));
                    any = 1;
                    cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE));
                }
                for (int g = 0; g < 19; g++)
                    for (int e = 0; e < 2; e++) {
                        uint32_t base = 0x691880 + 0x20 * g;
                        if (word(0x691880 + (8 * g + e) * 4) != t) continue;
                        if (e == 1 && word(base) == word(base + 4)) continue;
                        if (any) cat(text(S_PLUS));
                        else cat(text(S_OPEN)), any = 1, cat(name_of(word(0x96c8a8 + (3 * g + e) * 4))), cat(text(S_CLOSE));
                    }
                for (uint32_t p = 0x9ab538; p < 0x9ab864; p += 0x1c) {
                    if (half(p + 0x18) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), any = 1, cat(label_at(0x270)), cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE));
                }
                for (uint32_t p = 0x946020; p < 0x9460e4; p += 0x1c) {
                    if (word(p + 8) != t) continue;
                    if (any) cat(text(S_PLUS));
                    else cat(text(S_OPEN)), any = 1, cat(label_at(0x4d0)), cat(text(S_COLON)), cat(name_of(word(p))), cat(text(S_CLOSE));
                }
                // The first tech that this one leads to.
                for (uint32_t p = 0x94f380; p < 0x9502cc; p += 0x2c) {
                    if ((word(p - 4) != t && word(p) != t) || any) continue;
                    cat(text(S_OPEN)), any = 1;
                    cat(label_at(0x274)), cat(text(S_COLON)), cat(name_of(word(p - 0x24))), cat(text(S_CLOSE));
                }
            }
            game_dialogs_item(popup + POPUP_DIALOGS, bt, t);
            if (pt->count == 1) *(int32_t *)(popup + POPUP_ONLY) = t;
        }
    }
    return best_tech;
}

static void unbuild(uint8_t popup[4][POPUP_SIZE]) {
    for (int i = 3; i >= 0; i--) game_popup_dtor(popup[i]);
}

// Chooses a tech for the faction. `flag` is not zero when the faction's own list is wanted whoever it is, `other` the faction a tech is
// to be taken from (-1: researching), `label` the name of the pop-up (the default is "TECHNOLOGY"). Answers the tech chosen, -1 for
// none. A faction the AI plays gets the best of its list, or tech_ai()'s choice when it researches; a human gets a window.
static int UI_THIS picktech_pick(struct game_picktech *pt, int faction, int flag, int other, char *label) {
    uint8_t popup[4][POPUP_SIZE] __attribute__((aligned(4)));
    for (int i = 0; i < 4; i++) game_popup_ctor(popup[i]);
    pt->faction = faction;

    // With blind research (and not while taking a tech) the player's own console is handed over for the first
    // turns, one choice in five, to the AI.
    int blind = (*game_rules & 0x200) && other <= 0;
    if (blind && faction == *game_current_faction && !*game_net_multiplayer) {
        int rank = game_factions[faction].tech_ranking;
        if ((rank >> 1) % 5 == 1 || *game_turn <= 11) {
            if (!*game_skip_tech_screen) {
                if ((word(PREFERENCES) & 0x20) && game_tut_check2(1))
                    game_popt(*(char **)game_at(POPT_SCRIPT), text(S_TECHNOLOGY_NEW), -1, 0, 0);
                game_console_set_ai(game_at(CONSOLE), faction);
            }
        }
    }
    int asks_human = flag != 0 || (*game_turn != 0 && game_is_human(faction) && !blind);
    if (!asks_human && other < 0) {
        int tech = tech_ai(faction);
        unbuild(popup);
        return tech;
    }

    if ((word(PREFERENCES) & 0x20) && !*game_skip_tech_screen && !blind && faction == *game_current_faction && game_tut_check2(1))
        game_popt(*(char **)game_at(POPT_SCRIPT), text(S_TECHNOLOGY_SOON), -1, 0, 0);
    if (*game_net_multiplayer && *game_control_turn) game_fx_play(game_at(FX), 0x21);
    game_popup_start(popup[0], game_at(POPUP_FILE), label ? label : text(S_TECHNOLOGY), -1, 0, 2, 0);

    pt->count = 0;
    for (int i = 0; i < LISTED_TECHS; i++) pt->list[i] = -1;
    int best = list_techs(pt, popup[0], faction, flag, other);
    int n = pt->count;
    if (n == 0) {
        unbuild(popup);
        return -1;
    }
    if (!game_is_human(faction)) {
        unbuild(popup);
        return best;
    }

    // The window.
    int sw = *game_screen_width, sh = *game_win_screen_h;
    int size, hint_size;
    picktech_fonts(n, sw, &size, &hint_size);
    font_init(&pt->font[0], *game_font_face, size, 0);
    font_init(&pt->font[1], *game_font_face, size, 1);
    font_init(&pt->font[2], *game_font_face, size, 2);
    font_init(&pt->font[3], *game_font_face, hint_size, 1);
    buffer_set_font(&pt->buf, &pt->font[0], &pt->font[1], &pt->font[2], NULL);

    struct picktech_grid g;
    picktech_grid(&g, n, sw, sh);
    pt->cols = g.cols, pt->rows = g.rows, pt->cell_w = g.cell_w, pt->cell_h = g.cell_h, pt->margin = g.margin;
    graphic_win_init(&pt->win, 0, 0, g.win_w, g.win_h, NULL, 0x5000800, NULL, NULL, NULL);
    win_center(&pt->win);
    pt->win.title_h = 0x1d;

    // The title: the pop-up's heading in capitals, in a frame across the top.
    struct game_buffer *b = &pt->buf;
    struct ui_rect r = {b->full[0], b->full[1], b->full[2], b->full[3]};
    buffer_box_sprite(b, &r.left, game_at(BOX_WINDOW));
    r.right -= 3, r.top += 3, r.left += 3;
    r.bottom = r.top + 0x17;
    buffer_box_sprite(b, &r.left, game_at(BOX_TITLE));
    r.right -= 3, r.left += 3, r.bottom -= 3, r.top += 3;
    game_battle_text[0] = 0;
    cat(*(char **)(popup[0] + POPUP_TITLE));
    (*game_char_upper)(game_battle_text);
    buffer_set_text_color(b, 0x9f, -1, 1, 1);
    buffer_set_font(b, game_find_font(0x12, 1), NULL, NULL, NULL);
    buffer_write_cent_l_7(b, game_battle_text, &r.left, (int)strlen(game_battle_text));

    pt->hover = 0, pt->hover_ok = 0;
    for (int i = 0; i < pt->count; i++) picktech_draw_item(pt, i, pt->hover == i);
    UI_VCALL(void(UI_THIS *)(void *, int), pt, VT_SHOW, 0);
    if (!*game_win_capture) *game_win_capture = &pt->win, *game_win_hover = NULL;
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), pt, VT_SET_MODAL, 1, 0, 0);
    UI_VCALL(void(UI_THIS *)(void *), pt, VT_HIDE);
    game_do_all_draws();
    int chosen = cell_tech(pt, pt->hover);
    unbuild(popup);
    return chosen;
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook picktech_hooks[] = {
    UI_HOOK(0x484ac0, picktech_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x62, 0x76, 0x65, 0x00),  // push -1; push handler
    UI_HOOK(0x484b60, picktech_close, 0x33, 0xc0, 0x89, 0x81, 0x24, 0x0a, 0x00, 0x00),  // xor eax, eax; mov [ecx+0xa24], eax
    UI_HOOK(0x484b80, picktech_put_hint, 0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0xf1, 0x57),
    UI_HOOK(0x484c60, picktech_draw_tech, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x96, 0x76, 0x65, 0x00),
    UI_HOOK(0x485fc0, picktech_on_mouse_move, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x53),
    UI_HOOK(0x4860b0, picktech_on_left_click, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08, 0x56),
    UI_HOOK(0x486110, picktech_on_right_click, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08, 0x56),
    UI_HOOK(0x486170, picktech_on_key_click, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53),
    UI_HOOK(0x4863f0, picktech_draw_item, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x45, 0x08),
    UI_HOOK(0x486510, picktech_pick, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // push ebp; mov ebp, esp; mov eax, fs:0
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x488660, picktech_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0x25, 0x00, 0x00, 0x00),
    UI_HOOK(0x488690, picktech_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x60, 0x7c, 0x65, 0x00),
    UI_HOOK(0x488740, picktech_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module picktech_module = {"picktech", picktech_hooks, UI_HOOK_COUNT(picktech_hooks), NULL, NULL, -1};
