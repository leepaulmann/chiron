// The design workshop's part picker, the game's SelectPartWin: the window DesignWin opens (from its
// select_chassis, select_weapon, select_armor and select_reactor) to choose one part of a design. It shows the parts
// the rules allow as a grid of five cells across and four down (a scroll bar at its right when there are more than
// eighteen), each cell a 3-D model of the part (drawn by the game's own model viewer, an object inside the
// window) with its name and numbers, or the part's look when the player's faction cannot build it yet. The mouse
// hovers a cell, a click (a left button release) chooses the part under it, and the keys move the hover and
// Return or Escape end the window.
//
// The window is a local of each of the four DesignWin functions (they build it on the stack, call exec, and
// take it down with the destructor's body inlined), so nothing outside the class holds its address; DesignWin reads
// the answer from exec's result alone. Its virtual table (0x66b2b8) holds this class's mouse move (0x44), left up
// (0x54), key (0x64), the two scroll callbacks (0xc0, 0xc4) and redraw (0x30); the scroll bar tells its parent through
// slots 0xc0 and 0xc4, so those two are reached only by virtual call. The class stores no function pointer of
// its own.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=selpart gives them all back. The object keeps the original's layout, the toolkit is called directly, and
// what it draws, in what order, and what it writes are the original's. What isn't rebuilt: the model viewer inside
// the window and the four functions that build its models (the game_model_* pointers in game.h), the two functions
// that show and hide the scroll bar, the angles-to-matrix function, and the game's modal loop (Win_set_modal, which
// runs the loop inside exec and ends when the window releases it).
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the destructor's body,
// which DesignWin's functions inline, is not reached (its callers do not call it), so it is here for the virtual
// call only.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "selpart.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/buffer.h"
#include "../engine/bufimg.h"
#include "../engine/bufobj.h"
#include "../engine/button.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/scroll.h"
#include "../engine/sprite.h"
#include "../engine/tech.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure arithmetic ----------------------------------------------------------------------------------------

void selpart_cell_rect(struct ui_rect *r, int slot, int cols) {
    int row = slot / cols, col = slot % cols;
    int x = SELPART_PITCH_X * col + 4;
    r->left = x + 0x17;
    r->right = x + 0x80;
    r->top = SELPART_PITCH_Y * row + 4;
    r->bottom = r->top + 0x5b;
}

int selpart_hit(int x, int y, int cols, int rows, int first) {
    if (x < 4 || y < 4 || x >= SELPART_PITCH_X * cols + 4 || y >= SELPART_PITCH_Y * rows + 4) return -1;
    return (y - 4) / SELPART_PITCH_Y * cols + (x - 4) / SELPART_PITCH_X + first;
}

int selpart_step_sideways(int sel, int delta, int cols, int count) {
    int start = sel / cols * cols;
    int end = start + cols - 1;
    if (end > count - 1) end = count - 1;
    int to = sel + delta;
    if (to < start || end < start) return start;
    return to > end ? end : to;
}

int selpart_step_up(int sel, int first, int cols, int page) {
    do sel -= cols;
    while (sel >= first + page);
    return sel;
}

int selpart_step_down(int sel, int first, int cols) {
    sel += cols;
    while (sel < first) sel += cols;
    return sel;
}

int selpart_end_first(int first, int count, int cols, int page) {
    if (page >= count) return first;
    return ((count - 1) / cols + 1) * cols - page;
}

int selpart_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

int selpart_centre(int a, int b) { return (a + b) / 2; }

void selpart_number_text(char *out, int value) { sprintf(out, "%d", value); }

// ---- The object -----------------------------------------------------------------------------------------------

// The model viewer's size, and the one field of it the picker clears for every part drawn (its "drawn" flag).
enum { MODEL_SIZE = 0x14d0, MODEL_FLAG = 0x13cc, MODEL_BYTE = 0xa4, MODEL_LIGHT_A = 0x60, MODEL_LIGHT_B = 0x6c };

struct game_selpart {
    struct game_win win;     // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    uint8_t scroll[0x214c];  // 0xa14: the scroll bar (a Scroll, whose last four bytes are what follows)
    int32_t first;           // 0x2b60: the first part on the page
    int32_t page;            // 0x2b64: how many parts a page holds (twenty)
    int32_t faction;         // 0x2b68: the faction whose tech decides which parts it can build
    int32_t cols;            // 0x2b6c: parts across a page (five)
    int32_t rows;            // 0x2b70: rows a page (four)
    int32_t count;           // 0x2b74: how many parts the rules allow
    GAME_PAD(0x2b78, 0x2b80);
    int32_t hover;           // 0x2b80: the part under the mouse or chosen with the keys, or -1
    int32_t kind;            // 0x2b84: 0 chassis, 1 weapon, 2 armor, 3 reactor
    int32_t chassis;         // 0x2b88: the design's parts as the window was opened with them
    int32_t weapon;          // 0x2b8c
    int32_t armor;           // 0x2b90
    int32_t abilities;       // 0x2b94: the design's special abilities
    int32_t reactor;         // 0x2b98
    uint8_t model[MODEL_SIZE];  // 0x2b9c: the 3-D model viewer
    struct game_buffer buf2;    // 0x406c: a second Buffer, closed with the window and not drawn into
    struct font font_name;      // 0x45f4: the font of names and the left numbers
    struct font font_figure;    // 0x461c: the font of the right-hand figures
    const char *title;          // 0x4644: the text 0x6feb7c, which nothing reads
    int32_t hit;                // 0x4648: whether the hover is on a part (what the last mouse move or key drew)
};
GAME_AT(struct game_selpart, scroll, 0xa14);
GAME_AT(struct game_selpart, first, 0x2b60);
GAME_AT(struct game_selpart, page, 0x2b64);
GAME_AT(struct game_selpart, faction, 0x2b68);
GAME_AT(struct game_selpart, cols, 0x2b6c);
GAME_AT(struct game_selpart, rows, 0x2b70);
GAME_AT(struct game_selpart, count, 0x2b74);
GAME_AT(struct game_selpart, hover, 0x2b80);
GAME_AT(struct game_selpart, kind, 0x2b84);
GAME_AT(struct game_selpart, chassis, 0x2b88);
GAME_AT(struct game_selpart, weapon, 0x2b8c);
GAME_AT(struct game_selpart, armor, 0x2b90);
GAME_AT(struct game_selpart, abilities, 0x2b94);
GAME_AT(struct game_selpart, reactor, 0x2b98);
GAME_AT(struct game_selpart, model, 0x2b9c);
GAME_AT(struct game_selpart, buf2, 0x406c);
GAME_AT(struct game_selpart, font_name, 0x45f4);
GAME_AT(struct game_selpart, font_figure, 0x461c);
GAME_AT(struct game_selpart, title, 0x4644);
GAME_AT(struct game_selpart, hit, 0x4648);
_Static_assert(sizeof(struct game_selpart) == 0x464c, "layout of the game's SelectPartWin");

enum { PART_CHASSIS, PART_WEAPON, PART_ARMOR, PART_REACTOR };

// The slots of the virtual tables the class uses (byte offsets), and the tables it sets: the window's and the
// Buffer's.
enum {
    VT_HIDE_SCROLL = 0x8,  // (the scroll bar's)
    VT_HOVER_END = 0x10,
    VT_MOUSE_MOVE = 0x44,
    VT_SCROLLED = 0xc0,
    VT_SET_MODAL = 0xe4,
    VT_RELEASE_MODAL = 0xe8,
    VT_REDRAW = 0xf8,
    VT_UPDATE = 0xfc,
};
enum { VTABLE_WIN = 0x66b2b8, VTABLE_BUF = 0x66b2b0 };

// The game's data it reads (by address). The word at COLOR_* pairs are text colours (the name's, then the figures').
enum {
    DESIGN_WIN = 0x71f2b0,       // the DesignWin, which is the picker's parent; its Buffer's width and height are the picker's size
    BLANK_TILE = 0x6b33f8,       // the sprite an empty cell is filled with, and from which the frame's pattern is drawn
    FRAME_CELL = 0x78d528,       // the cell frame (two looks: plain, and selected 0x50 on)
    FONT_FACE = 0x691b2c,        // the name of the face of the fonts
    COLOR_NAME = 0x8c6dd4,
    COLOR_FIGURE = 0x8c6ddc,
    TRIAD_NAMES = 0x94f1a8,      // char *[3]: land, sea, air
    ICON_SPRITES = 0x778400,     // the weapon icons, 0x160 apart
    ICON_FOR_PSI = 0x9ab9ce,     // the icon of a psi weapon (a signed byte)
    SPECIAL_FOOTER = 0x6eef7c,   // the data the special armor's model is drawn with
    MAP_OWNER = 0x939284,        // the faction whose map the player sees
    LIGHT = 0x687ad8,            // three floats
    CAMERA_SCALE = 0x66aa40,     // a float
};
enum { WEAPON_PSI_ICON_MODE = 11 };

static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static struct game_scroll *bar(struct game_selpart *pw) { return (struct game_scroll *)pw->scroll; }
static void vcall0(void *obj, int slot) { UI_VCALL(void(UI_THIS *)(void *), obj, slot); }

// Starts the text colours: the name's (0) or the figures'.
static void text_colors(struct game_buffer *b, uint32_t addr) { buffer_set_text_color(b, word(addr), word(addr + 4), 1, 1); }

// The text a string id (an entry of the rules' names) stands for, put in the game's scratch string, in capitals when
// asked.
static void put_string(const char *id, int upper) {
    strcpy(game_str_buffer, game_strings_get(game_strings, (char *)id));
    if (upper) (*game_char_upper)(game_str_buffer);
}
static void put_label(int index) { strcpy(game_str_buffer, game_label(index)); }
static void put_number(int value) {
    char tmp[0x40];
    game_str_buffer[0] = 0;
    selpart_number_text(tmp, value);
    strcpy(game_str_buffer, tmp);
}

// Writes the scratch string left-aligned, or right-aligned, in a rectangle.
static void write_left(struct game_buffer *b, const struct ui_rect *r) { buffer_write_l_4(b, game_str_buffer, &r->left, strlen(game_str_buffer)); }
static void write_right(struct game_buffer *b, const struct ui_rect *r) { buffer_write_right_l_6(b, game_str_buffer, &r->left, strlen(game_str_buffer)); }

// The rectangles of a cell's two text lines: the name line, the height of a line from the cell's top, and the figures'
// line, the same height up from its bottom.
static struct ui_rect name_line(const struct game_selpart *pw, const struct ui_rect *r) {
    struct ui_rect l = *r;
    l.bottom = r->top + selpart_line_height(pw->font_name.first, pw->font_name.ascent, pw->font_name.height);
    return l;
}
static struct ui_rect figure_line(const struct game_selpart *pw, const struct ui_rect *r) {
    struct ui_rect l = *r;
    l.top = r->bottom - selpart_line_height(pw->font_name.first, pw->font_name.ascent, pw->font_name.height);
    return l;
}

static void model_unavailable(struct game_selpart *pw, const struct ui_rect *r) {
    game_model_draw_unavailable(pw->model, &pw->buf, selpart_centre(r->right, r->left), selpart_centre(r->bottom, r->top), 0xb9, 6);
}
static void model_draw(struct game_selpart *pw, const struct ui_rect *r) {
    game_model_draw(pw->model, &pw->buf, selpart_centre(r->right, r->left), selpart_centre(r->bottom, r->top), 6);
}
static void model_clear_flag(struct game_selpart *pw) { *(int32_t *)(pw->model + MODEL_FLAG) = 0; }

// ---- Construction -------------------------------------------------------------------------------------------

static void start_fields(struct game_selpart *pw) {
    pw->cols = 0;
    pw->rows = 0;
    pw->hover = 0;
    pw->hit = 0;
    pw->first = 0;
}

static void *UI_THIS selpart_ctor(struct game_selpart *pw) {
    graphic_win_ctor(&pw->win);
    scroll_ctor(bar(pw));
    game_model_ctor(pw->model);
    buffer_ctor(&pw->buf2);
    font_ctor(&pw->font_name);
    font_ctor(&pw->font_figure);
    start_fields(pw);
    pw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    return pw;
}

// Closing the window: the fields start again (the faction is none) and its Win and second Buffer are closed.
static void UI_THIS selpart_close(struct game_selpart *pw) {
    start_fields(pw);
    pw->faction = -1;
    graphic_win_close(&pw->win);
    buffer_close(&pw->buf2);
}

static void UI_THIS selpart_dtor(struct game_selpart *pw) {
    pw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    pw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    start_fields(pw);
    pw->faction = -1;
    graphic_win_close(&pw->win);
    buffer_close(&pw->buf2);
    font_dtor(&pw->font_figure);
    font_dtor(&pw->font_name);
    buffer_dtor(&pw->buf2);
    game_model_close(pw->model);
    scroll_dtor(bar(pw));
    graphic_win_dtor(&pw->win);
}

static void *UI_THIS selpart_scalar_dtor(struct game_selpart *pw, int flags) {
    selpart_dtor(pw);
    if (flags & 1) game_free(pw);
    return pw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS selpart_operator_delete(struct game_buffer *buf, int flags) {
    return selpart_scalar_dtor((struct game_selpart *)((uint8_t *)buf - offsetof(struct game_selpart, buf)), flags);
}

// ---- Drawing a part ------------------------------------------------------------------------------------------

// The chassis `index` places down the list of those the rules allow: its model, and its name and its triad and
// speed (or the unavailable look when the faction lacks the tech). Answers 0 when there is no such chassis, else
// whether it is available.
static int UI_THIS selpart_draw_chassis(struct game_selpart *pw, int index, const struct ui_rect *r, int sel) {
    int missing = 0, id, seen = 0;
    for (id = 0; id < CHASSIS_COUNT; id++) {
        if (game_chassis[id].preq_tech < -1) continue;
        if (seen == index) {
            if (!has_tech(game_chassis[id].preq_tech, pw->faction)) missing = 1;
            break;
        }
        seen++;
    }
    if (id == CHASSIS_COUNT) return 0;
    model_clear_flag(pw);
    game_model_build_chassis(pw->faction, id, 0, 0, pw->reactor, pw->model);
    game_model_scale(pw->model, 0x3d75c28f);
    if (missing) {
        model_unavailable(pw, r);
        return 0;
    }
    model_draw(pw, r);
    struct game_buffer *b = &pw->buf;
    text_colors(b, COLOR_NAME);
    struct ui_rect line = name_line(pw, r);
    put_string(game_chassis[id].offsv1_name, 1);
    buffer_wrap_cent_3(b, &pw->font_name, game_str_buffer, &line.left);
    text_colors(b, COLOR_FIGURE);
    line = figure_line(pw, r);
    strcpy(game_str_buffer, game_strings_get(game_strings, *(char **)game_at(TRIAD_NAMES + 4 * game_chassis[id].triad)));
    buffer_set_font(b, &pw->font_name, NULL, NULL, NULL);
    write_left(b, &line);
    put_number(game_chassis[id].speed);
    buffer_set_font(b, &pw->font_figure, NULL, NULL, NULL);
    write_right(b, &line);
    return 1;
}

// The model viewer's data blocks for the weapons that are drawn from game data instead of built (by weapon id).
static const struct {
    int id;
    uint32_t first, second;
    uint32_t scale;
} weapon_models[] = {
    {0, 0x7871c0, 0, 0x3e4ccccd},
    {0x10, 0x78a5f0, 0, 0x3d75c28f},
    {0x17, 0x791d78, 0, 0x3d75c28f},
    {0x18, 0x791d78, 0x788028, 0x3d75c28f},
    {0x19, 0x791d78, 0x7acc10, 0x3d75c28f},
};

static int UI_THIS selpart_draw_weapon(struct game_selpart *pw, int index, const struct ui_rect *r, int sel) {
    int missing = 0, id, seen = 0;
    for (id = 0; id < WEAPON_COUNT; id++) {
        if (game_weapons[id].preq_tech < -1) continue;
        if (seen == index) {
            if (!has_tech(game_weapons[id].preq_tech, pw->faction)) missing = 1;
            break;
        }
        seen++;
    }
    if (id == WEAPON_COUNT) return 0;
    model_clear_flag(pw);
    uint32_t scale = 0;
    int built = 0;
    for (size_t i = 0; i < sizeof(weapon_models) / sizeof(weapon_models[0]); i++)
        if (weapon_models[i].id == id) {
            game_model_add(pw->model, game_at(weapon_models[i].first), 0);
            if (weapon_models[i].second) game_model_add(pw->model, game_at(weapon_models[i].second), 0);
            scale = weapon_models[i].scale;
            built = 1;
            break;
        }
    if (!built) {
        game_model_build_weapon(pw->faction, -1, id, pw->model);
        int mode = game_weapons[id].mode;
        scale = mode < 3 ? 0x3e4ccccd : mode == 9 ? 0x3da3d70a : mode == 0xb ? 0x3e4ccccd : 0x3dcccccd;
    }
    game_model_scale(pw->model, scale);

    // The weapon's icon (for the modes that have one: 8 and 9 their own, 11 the psi weapon's), drawn behind the
    // model in the middle of the cell.
    int icon = -1;
    switch (game_weapons[id].mode) {
    case 8:
    case 9: icon = game_weapons[id].icon; break;
    case WEAPON_PSI_ICON_MODE: icon = *(int8_t *)game_at(ICON_FOR_PSI); break;
    }
    if (icon >= 0) {
        int x = r->left + selpart_centre(r->right - r->left, -100);
        int y = r->top + selpart_centre(r->bottom - r->top, -0x4b);
        struct game_sprite *spr = game_at(ICON_SPRITES + 0x160 * icon);
        sprite_draw_3(spr, &pw->buf, spr->key, x, y);
    }
    if (missing) {
        model_unavailable(pw, r);
        return 0;
    }
    model_draw(pw, r);
    struct game_buffer *b = &pw->buf;
    text_colors(b, COLOR_NAME);
    struct ui_rect line = name_line(pw, r);
    put_string(game_weapons[id].name, 0);
    buffer_set_font(b, &pw->font_name, NULL, NULL, NULL);
    (*game_char_upper)(game_str_buffer);
    buffer_wrap_cent_6(b, game_str_buffer, &line.left);
    text_colors(b, COLOR_FIGURE);
    line = figure_line(pw, r);
    // The left figure names the kind (the label for a weapon of a mode below three, else the other).
    put_label(game_weapons[id].mode < 3 ? 0x81 : 0x80);
    write_left(b, &line);
    buffer_set_font(b, &pw->font_figure, NULL, NULL, NULL);
    // The right one is the attack, "psi" for a psi weapon, the cost for the modes from three up.
    if ((uint8_t)game_weapons[id].offense == 0xff) put_label(0xc4);
    else put_number(game_weapons[id].mode >= 3 ? game_weapons[id].cost : (uint8_t)game_weapons[id].offense);
    write_right(b, &line);
    return 1;
}

static int UI_THIS selpart_draw_armor(struct game_selpart *pw, int index, const struct ui_rect *r, int sel) {
    int missing = 0, id = 0;
    int missile = game_chassis[pw->chassis].missile;
    struct game_buffer *b = &pw->buf;
    if (!missile) {
        int seen = 0;
        for (id = 0; id < ARMOR_COUNT; id++) {
            if (game_armor[id].preq_tech < -1) continue;
            if (seen == index) {
                if (!has_tech(game_armor[id].preq_tech, pw->faction)) missing = 1;
                break;
            }
            seen++;
        }
        if (id == ARMOR_COUNT) return 0;
        if (id != 0) {
            model_clear_flag(pw);
            game_model_build_armor(word(MAP_OWNER), pw->chassis, pw->weapon, id, 1, pw->model);
            game_model_scale(pw->model, 0x3dcccccd);
        }
        if (missing) {
            if (id == 0) return 0;
            model_unavailable(pw, r);
            return 0;
        }
        if (id != 0) {
            if (id == 5)
                game_model_draw_special(pw->model, b, selpart_centre(r->right, r->left), selpart_centre(r->bottom, r->top), 0x18,
                                        game_at(SPECIAL_FOOTER), 6);
            else model_draw(pw, r);
        }
    }
    text_colors(b, COLOR_NAME);
    struct ui_rect line = name_line(pw, r);
    buffer_set_font(b, &pw->font_name, NULL, NULL, NULL);
    put_string(game_armor[id].name, 1);
    buffer_wrap_cent_6(b, game_str_buffer, &line.left);
    text_colors(b, COLOR_FIGURE);
    line = figure_line(pw, r);
    game_str_buffer[0] = 0;
    if (game_armor[id].mode < 3) put_label(0x7e);
    write_left(b, &line);
    int defense = game_armor[id].defense;
    if (defense < 0) put_label(0xc4);
    else put_number(defense);
    buffer_set_font(b, &pw->font_figure, NULL, NULL, NULL);
    write_right(b, &line);
    return 1;
}

static int UI_THIS selpart_draw_reactor(struct game_selpart *pw, int index, const struct ui_rect *r, int sel) {
    int missing = 0, id = 1, seen = 0;
    for (; id <= REACTOR_COUNT; id++) {
        if (game_reactors[id - 1].preq_tech < -1) continue;
        if (seen == index) {
            if (!has_tech(game_reactors[id - 1].preq_tech, pw->faction)) missing = 1;
            break;
        }
        seen++;
    }
    if (id == REACTOR_COUNT + 1) return 0;
    model_clear_flag(pw);
    game_model_build_reactor(word(MAP_OWNER), pw->chassis, pw->weapon, id, pw->abilities, pw->model);
    game_model_scale(pw->model, 0x3e4ccccd);
    if (missing) {
        model_unavailable(pw, r);
        return 0;
    }
    model_draw(pw, r);
    struct game_buffer *b = &pw->buf;
    text_colors(b, COLOR_NAME);
    struct ui_rect line = name_line(pw, r);
    buffer_set_font(b, &pw->font_name, NULL, NULL, NULL);
    put_string(game_reactors[id - 1].name, 1);
    buffer_wrap_cent_6(b, game_str_buffer, &line.left);
    text_colors(b, COLOR_FIGURE);
    line = figure_line(pw, r);
    put_label(0x79);
    write_left(b, &line);
    buffer_set_font(b, &pw->font_figure, NULL, NULL, NULL);
    put_number(id);
    write_right(b, &line);
    return 1;
}

// Draws the cell of part `index` if it is on the page: an empty cell (past the last part) is filled with the tile, a
// part's cell gets the frame (the selected look when `sel`), is clipped to the cell, and has its part drawn by kind.
// Answers 1 (or whether the part is available).
static int UI_THIS selpart_draw_part(struct game_selpart *pw, int index, int sel) {
    if (index < pw->first || index >= pw->first + pw->page) return 1;
    struct ui_rect r;
    selpart_cell_rect(&r, index - pw->first, pw->cols);
    struct game_buffer *b = &pw->buf;
    if (index >= pw->count) {
        buffer_tile_2(b, game_at(BLANK_TILE), r.left, r.top, r.left, r.top, r.right - r.left, r.bottom - r.top);
        return 1;
    }
    buffer_set_clip(b, &r.left);
    buffer_box_sprite(b, &r.left, (const struct game_box_frame *)((const uint8_t *)game_at(FRAME_CELL) + (sel ? 0x50 : 0)));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    int result;
    switch (pw->kind) {
    case PART_CHASSIS: result = selpart_draw_chassis(pw, index, &r, sel); break;
    case PART_WEAPON: result = selpart_draw_weapon(pw, index, &r, sel); break;
    case PART_ARMOR: result = selpart_draw_armor(pw, index, &r, sel); break;
    case PART_REACTOR: result = selpart_draw_reactor(pw, index, &r, sel); break;
    default: result = (int)(uintptr_t)b; break;
    }
    buffer_set_clip(b, b->full);
    return result;
}

// ---- Window events -------------------------------------------------------------------------------------------

// Redraws every cell of the page.
static void UI_THIS selpart_on_redraw(struct game_selpart *pw) {
    for (int i = pw->first; i < pw->first + pw->page; i++) selpart_draw_part(pw, i, pw->hover == i);
}

// The mouse moved: the hover goes to the part under it (an empty cell or nowhere clears it), the cell left is
// drawn plain and the new one selected, and the window is shown.
static void UI_THIS selpart_on_mouse_move(struct game_selpart *pw, int x, int y) {
    int index = selpart_hit(x, y, pw->cols, pw->rows, pw->first);
    if (index >= 0) {
        if (index == pw->hover) return;
        if (index < pw->count) {
            pw->hit = selpart_draw_part(pw, index, 1);
            if (pw->hover >= 0) selpart_draw_part(pw, pw->hover, 0);
            pw->hover = index;
            vcall0(pw, VT_UPDATE);
            return;
        }
        if (pw->hover >= 0) selpart_draw_part(pw, pw->hover, 0);
    } else if (pw->hover >= 0) {
        selpart_draw_part(pw, pw->hover, 0);
    }
    pw->hit = 0;
    pw->hover = -1;
}

// The left button was released: the mouse is moved to the point (a click is where it is let go), and the window
// ends. A release on a part keeps it chosen; elsewhere (hit is 0) the hover is cleared first, so exec answers that
// nothing was chosen.
static void UI_THIS selpart_on_left_up(struct game_selpart *pw, int x, int y) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pw, VT_MOUSE_MOVE, x, y);
    if (!pw->hit) pw->hover = -1;
    if (*game_win_capture == &pw->win) {
        *game_win_capture = NULL;
        vcall0(pw, VT_HOVER_END);
    }
    if (*game_win_hover == &pw->win) *game_win_hover = NULL;
    vcall0(pw, VT_RELEASE_MODAL);
}

static void scroll_to(struct game_selpart *pw, int pos) { scroll_set_pos(bar(pw), pos); }

// A key: Return and Escape end the window (Escape clears the choice), the arrows, Page Up and Down, Home and End
// (and the number pad's 2, 4, 6 and 8) move the hover, and the page, when it must, with the scroll bar.
static int UI_THIS selpart_on_key_click(struct game_selpart *pw, int a, int key) {
    int first = pw->first, hover = pw->hover;
    struct game_scroll *sc = bar(pw);
    int cols = pw->cols;
    switch (key) {
    case 0x0d: vcall0(pw, VT_RELEASE_MODAL); break;
    case 0x1b:
        pw->hover = -1;
        vcall0(pw, VT_RELEASE_MODAL);
        break;
    case 0x26:
    case 0x68:  // Up
        if (hover < cols) return 1;
        pw->hover = selpart_step_up(hover, first, cols, pw->page);
        if (pw->hover < first) {
            pw->first = first - cols;
            scroll_to(pw, sc->pos - 1);
        }
        break;
    case 0x28:
    case 0x62:  // Down
        if (hover > pw->count - cols - 1) {
            if (pw->count <= pw->page) return 1;
            scroll_to(pw, sc->pos + 1);
            pw->first = cols * sc->pos;
        } else {
            int to = selpart_step_down(pw->hover, first, cols);
            pw->hover = to;
            if (to >= first + pw->page) {
                pw->first = first + cols;
                scroll_to(pw, sc->pos + 1);
            }
        }
        break;
    case 0x25:
    case 0x64: pw->hover = selpart_step_sideways(hover, -1, cols, pw->count); break;  // Left
    case 0x27:
    case 0x66: pw->hover = selpart_step_sideways(hover, 1, cols, pw->count); break;   // Right
    case 0x23:  // End
        pw->hover = pw->count - 1;
        pw->first = selpart_end_first(first, pw->count, cols, pw->page);
        break;
    case 0x24:  // Home
        pw->first = 0;
        pw->hover = 0;
        break;
    case 0x21:  // Page Up
        if (pw->count <= pw->page) return 1;
        scroll_to(pw, sc->pos - sc->paging);
        pw->first = cols * sc->pos;
        break;
    case 0x22:  // Page Down
        if (pw->count <= pw->page) return 1;
        scroll_to(pw, sc->paging + sc->pos);
        pw->first = cols * sc->pos;
        break;
    default: break;
    }
    if (first != pw->first) {
        vcall0(pw, VT_REDRAW);
    } else if (hover != pw->hover) {
        selpart_draw_part(pw, hover, 0);
        pw->hit = selpart_draw_part(pw, pw->hover, 1);
    }
    vcall0(pw, VT_UPDATE);
    return 1;
}

// The scroll bar was moved: the page starts at the row it says.
static void UI_THIS selpart_on_scrolled(struct game_selpart *pw, int a, int pos) {
    pw->first = pw->cols * pos;
    vcall0(pw, VT_REDRAW);
}

// The scroll bar is being dragged: the same as when it has been moved, by virtual call.
static void UI_THIS selpart_on_scrolling(struct game_selpart *pw, int a, int pos) {
    UI_VCALL(void(UI_THIS *)(void *, int, int), pw, VT_SCROLLED, a, pos);
}

// ---- Opening the window -------------------------------------------------------------------------------------

// How many parts of a kind the rules allow (a table entry whose tech is -2 or less is switched off): for armor, a
// missile chassis has just the one choice.
static int count_parts(int kind, int chassis) {
    int n = 0;
    switch (kind) {
    case PART_CHASSIS:
        for (int i = 0; i < CHASSIS_COUNT; i++) n += game_chassis[i].preq_tech >= -1;
        break;
    case PART_WEAPON:
        for (int i = 0; i < WEAPON_COUNT; i++) n += game_weapons[i].preq_tech >= -1;
        break;
    case PART_ARMOR:
        if (game_chassis[chassis].missile) return 1;
        for (int i = 0; i < ARMOR_COUNT; i++) n += game_armor[i].preq_tech >= -1;
        break;
    case PART_REACTOR:
        for (int i = 0; i < REACTOR_COUNT; i++) n += game_reactors[i].preq_tech >= -1;
        break;
    }
    return n;
}

// The scene host's view of the live window (see scene.c); not linked into the unit test.
extern void scene_live_state(const char *name, const void *p, size_t size) __attribute__((weak));

static void live_state(struct game_selpart *pw, int on) {
    if (!scene_live_state) return;
    scene_live_state("live_selpart_a", on ? &pw->first : NULL, 0x18);
    scene_live_state("live_selpart_b", on ? &pw->hover : NULL, 0x1c);
    scene_live_state("live_selpart_c", on ? &pw->hit : NULL, 4);
    scene_live_state("live_selpart_scroll", on ? (uint8_t *)pw + 0xa14 + 0xa20 : NULL, 0x18);
    scene_live_state("live_selpart_scroll2", on ? (uint8_t *)pw + 0xa14 + 0xa40 : NULL, 8);
}

// The window's only entry: opens it at (x, y) over the workshop for a faction and a kind of part, with the
// design's present parts (as they bear on what the models show), the place in the list of the part chosen now, and
// a text nothing reads; runs the game's modal loop until the window releases it; and answers the part chosen as an
// index in the list (for reactors, the reactor's id, one more), or the place it was opened with when none was.
static int UI_THIS selpart_exec(struct game_selpart *pw, int x, int y, int faction, int kind, int pos, int chassis, int weapon,
                                int armor, int abilities, int reactor, const char *unused) {
    struct game_buffer tile;
    buffer_ctor(&tile);
    buffer_init(&tile, 10, 10, 0, NULL);
    struct game_sprite *blank = game_at(BLANK_TILE);
    sprite_draw_2(blank, &tile, blank->key, 0, 0, 1, 1);

    struct game_box_frame frame;
    memcpy(&frame, game_at(FRAME_CELL), sizeof(frame));
    frame.fill = &tile;
    int opened_with = pos;
    pw->faction = faction;
    pw->chassis = chassis;
    pw->weapon = weapon;
    pw->armor = armor;
    pw->abilities = abilities;
    pw->reactor = reactor;
    pw->kind = kind;
    pw->title = game_at(0x6feb7c);
    if (kind == PART_REACTOR) pos--;

    game_model_reset(pw->model);
    pw->model[MODEL_BYTE] = 0;
    char *face = (char *)(uintptr_t)word(FONT_FACE);
    font_init(&pw->font_name, face, 10, 0);
    font_init(&pw->font_figure, face, 12, 1);
    buffer_set_font(&pw->buf, &pw->font_name, NULL, NULL, NULL);

    // The camera: the matrix from a fixed angle, its last row turned round and scaled.
    float angles[3] = {0, 0, 0}, matrix[16], eye[3];
    uint32_t bits = 0x3f48f5c3;
    memcpy(&angles[2], &bits, 4);
    game_matrix_from_angles(angles, matrix);
    float scale;
    memcpy(&scale, game_at(CAMERA_SCALE), 4);
    matrix[6] = -matrix[6], matrix[7] = -matrix[7], matrix[8] = -matrix[8];
    eye[0] = matrix[6] * scale, eye[1] = matrix[7] * scale, eye[2] = matrix[8] * scale;
    game_model_view(pw->model, eye, matrix);
    game_model_light(pw->model, 0x3f05f92d, 0, 0);
    memcpy(pw->model + MODEL_LIGHT_B, game_at(LIGHT), 12);
    memcpy(pw->model + MODEL_LIGHT_A, game_at(LIGHT), 12);

    pw->count = count_parts(kind, chassis);
    pw->cols = SELPART_COLS;
    pw->rows = SELPART_ROWS;
    pw->page = SELPART_PAGE;

    // The window covers the workshop's Buffer (its parent); the cells' frame is drawn over its right part.
    struct game_win *parent = game_at(DESIGN_WIN);
    int width = word(DESIGN_WIN + 0x4c4), height = -word(DESIGN_WIN + 0x4c8);
    graphic_win_init(&pw->win, 0, 0, width, height, NULL, 0x1000020, parent, NULL, NULL);
    graphic_win_fill(&pw->win, 9);
    struct ui_rect area = {0x17, 0, width - 0x37, height};
    buffer_box_sprite(&pw->buf, &area.left, &frame);

    struct game_scroll *sc = bar(pw);
    if (pw->count > 0x12) {
        game_scroll_init(sc, pw->buf.width - 0x2b, 0x15, 0x18, 0xf6, pw, NULL, 1);
        vcall0(sc, VT_HIDE_SCROLL);
        scroll_set_range(sc, 0, pw->count / pw->cols - pw->rows + 2);
        sc->paging = pw->rows - 1;
        sc->delay = 0;
        vcall0(sc, VT_REDRAW);
    }

    // The page that holds the chosen part: moved down a row at a time, with the scroll bar.
    pw->first = 0;
    if (pos >= pw->page) {
        do {
            pw->first += pw->cols;
            scroll_set_pos(sc, pw->first / pw->cols);
        } while (pos >= pw->first + pw->page);
    }
    for (int i = pw->first; i < pw->first + pw->page; i++) selpart_draw_part(pw, i, pos == i);
    pw->hover = pos;
    if (pw->page < pw->count) game_scroll_show(pw, sc);
    live_state(pw, 1);
    UI_VCALL(void(UI_THIS *)(void *, int, int, int), pw, VT_SET_MODAL, 1, (int)(uintptr_t)game_pop_wait, 0);
    live_state(pw, 0);
    if (pw->page < pw->count) game_scroll_hide(pw, sc);
    UI_VCALL(void(UI_THIS *)(void *, int), sc, 4, 0);
    vcall0(pw, 8);
    game_do_all_draws();

    int answer = opened_with;
    if (pw->hover >= 0 && pw->hit) {
        if (pw->kind == PART_REACTOR) pw->hover++;
        answer = pw->hover;
    }
    buffer_dtor(&tile);
    return answer;
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook selpart_hooks[] = {
    UI_HOOK(0x4328b0, selpart_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x50, 0x3f, 0x65, 0x00),
    UI_HOOK(0x432970, selpart_close, 0x56, 0x8b, 0xf1, 0x33, 0xc0, 0x89, 0x86, 0x60, 0x2b, 0x00, 0x00),
    UI_HOOK(0x4329b0, selpart_draw_chassis, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x68),
    UI_HOOK(0x432cc0, selpart_draw_weapon, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x6c),
    UI_HOOK(0x433170, selpart_draw_armor, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x6c),
    UI_HOOK(0x433520, selpart_draw_reactor, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x6c),
    UI_HOOK(0x433820, selpart_on_mouse_move, 0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08),
    UI_HOOK(0x433940, selpart_on_left_up, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08),
    UI_HOOK(0x4339c0, selpart_on_redraw, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0xb7, 0x60, 0x2b, 0x00, 0x00),
    UI_HOOK(0x433a10, selpart_on_key_click, 0x55, 0x8b, 0xec, 0x51, 0x8b, 0x45, 0x0c),
    UI_HOOK(0x433d60, selpart_draw_part, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10),
    UI_HOOK(0x433f30, selpart_on_scrolled, 0x55, 0x8b, 0xec, 0x8b, 0x81, 0x6c, 0x2b, 0x00, 0x00),
    UI_HOOK(0x433f50, selpart_on_scrolling, 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x0c),
    UI_HOOK(0x433f70, selpart_exec, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x65, 0x3f, 0x65, 0x00),
    UI_HOOK(0x43ede0, selpart_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x43ee10, selpart_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xa2, 0x47, 0x65, 0x00),
    UI_HOOK(0x43efb0, selpart_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),
};

struct hook_module selpart_module = {"selpart", selpart_hooks, UI_HOOK_COUNT(selpart_hooks), NULL, NULL, -1};
