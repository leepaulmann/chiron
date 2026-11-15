// The status panel on the main screen, the game's StatusWin: the panel at the bottom right that shows the square under the
// mouse (the picture of its terrain, its altitude, rainfall, rockiness, landmarks, resources and improvements, or what
// it yields), the unit or base that is active on it (its name, stats, orders, damage, home, production), and under that the
// strip of the units stacked on the square, each an icon that can be clicked. It is one global object (0x8c5568) built by a
// static initializer and torn down by an atexit handler; it has no virtual table. The main screen's code reaches it by absolute
// address: the console and the map windows move the square (set_loc), ask for a redraw (redraw, draw, reset_active), set the
// title text (set_title), and the combat code makes it show a battle (draw with the units, reset when it is over, the word at
// 0x8c6b3c that says one is shown). The class has no callbacks and no timer; it clicks through a Spot list (the icons and the
// pictures are hit rectangles of kinds 0 the unit, 1 and 0xb the base, 3 the square's picture, 0xa the active unit's
// picture) and one menu on the right button.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=statuswin gives them all back. The object keeps the original's layout, the toolkit is called directly, and
// what it draws, in what order, and what it writes are the original's. What isn't rebuilt: the game functions the text is put
// together by (say_stats_3, say_orders, say_morale, say_loc, the yield functions), the picture of a square (the map
// window's terrain poly), the unit and base pictures (veh_draw, base_draw), the 3-D model viewer in the object, the pop-up
// and base windows the menu opens, the scroll arrows the unit strip shows (three windows by address, called through their
// virtual table).
//
// In shadow mode three calls are held while a check protects state (draw, reset and redraw, the ones the original's quiet
// hooks held), as the code that was in quiet.c did.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the Popup object of the right
// button's menu (built and unbuilt on every right click by the original, but only a menu of three lines on the square's
// picture uses it) is built only then; locals the original keeps in registers or frame slots that nothing later reads
// (the x extents the terrain lines tracked) are not kept; the info panel's reset that a left click makes is called straight
// (infowin_reset_c, with the top byte of EBX the click came in with, as infowin.c's own entries keep it), so CHIRON_SKIP=infowin
// does not give the original's back for that one call; the right click takes the original's third argument and ignores it.
// Branches the scenes cannot reach: the menu's results 1 and 2 (no menu line has those ids) and spot kind 2 (nothing adds one),
// the base picked or opened by a left click on a base icon (it opens the base window, which the scenes do not close), the
// menu's Zoom, Change production and Hurry lines (the same, or a modal question), the editor line of the debug menu, and the
// constructor and destructor (the object is built when the game starts and torn down when it quits). Differences from
// OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "statuswin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/base.h"
#include "../engine/buffer.h"
#include "../engine/bufobj.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/map.h"
#include "../engine/menu.h"
#include "../engine/move.h"
#include "../engine/tech.h"
#include "../engine/text.h"
#include "../engine/veh.h"
#include "../engine/win.h"

// ---- Pure arithmetic ----------------------------------------------------------------------------------------

int statuswin_font_size(int screen_w) { return screen_w == 0x320 ? 10 : 12; }

int statuswin_line_height(int first, int ascent, int height) { return first < 0 ? ascent : height + first; }

int statuswin_scaled(int value, int mul, int div, int fallback) { return value > 0 ? value * mul / div : fallback; }

int statuswin_centre_x(int left, int right, int size) { return (right - size - left) / 2 + left; }

int statuswin_altitude_class(int climate) { return ((climate & 0xff) >> 5) - 3; }

int statuswin_text_top(int left, int right, int top) { return (right - left) / 2 + top + 3; }

int statuswin_strip_capacity(int width, int icon) { return width / (icon + 5); }

int statuswin_strip_icon(int width, int capacity) { return width / capacity - 5; }

int statuswin_moves_left(int speed, int spent) {
    int left = speed - spent;
    if (left < 0) return 0;
    return left > 999 ? 999 : left;
}

int statuswin_hit_points(int plan, int reactor) {
    if (plan == 12) return 1;
    if (reactor < 1) reactor = 1;
    if (reactor > 100) reactor = 100;
    return reactor * 10;
}

int statuswin_damage_percent(int hit_points, int damage) {
    int remaining = hit_points - damage;
    if (remaining < 0) remaining = 0;
    if (remaining > 9999) remaining = 9999;
    return (hit_points - remaining) * 100 / hit_points;
}

int statuswin_damage_level(int hit_points, int damage) {
    if (damage >= hit_points * 2 / 3) return 2;
    return damage >= hit_points / 3 ? 1 : 0;
}

int statuswin_flight_turns(int range, int moves_left, int flown) { return range + (moves_left == 0) - flown; }

// ---- The object -----------------------------------------------------------------------------------------------

enum { STATUS_BASE = 0x8c5568, MODEL_SIZE = 0x14d0, MODEL_LIGHT_A = 0x60, MODEL_LIGHT_B = 0x6c, MODEL_BYTE = 0xa4 };

struct game_statuswin {
    GAME_PAD(0x0, 0x30);
    uint8_t model[MODEL_SIZE];  // 0x30: the 3-D model viewer, built and unbuilt with the object and never drawn in
    float angles[3];            // 0x1500: the camera's turn (only the third is not zero)
    float matrix[9];            // 0x150c: the matrix those make, its last row turned round
    struct font font[3];        // 0x1530: the panel's plain, bold and italic print
    int32_t yields;             // 0x15a8: whether the square shows what it yields (set by the menu, flipped by a click on the picture)
    int32_t font_size;          // 0x15ac: the fonts' size, 10 or 12, or -1 before they are made
    GAME_PAD(0x15b0, 0x15b4);
    int32_t x, y;               // 0x15b4: the square shown
    int32_t sel_veh;            // 0x15bc: the unit picked on the square, or -1
    int32_t sel_base;           // 0x15c0: the base picked on it, or -1
    int32_t pending_a;          // 0x15c4: set to -1 when the panel has been drawn; the game's code sets one of these to ask
    int32_t pending_b;          // 0x15c8: for the active picture to be drawn again (reset_active)
    int32_t active_veh;         // 0x15cc: the unit and base the active picture last showed
    int32_t active_base;        // 0x15d0
    int32_t battle;             // 0x15d4: set while the panel shows a battle (by the combat code)
    int32_t double_click;       // 0x15d8: set while a double click is passed on as a click, which then does nothing
    int32_t has_unit;           // 0x15dc: whether a unit was shown on the square
    struct game_spot spot;      // 0x15e0: where the icons and pictures are
};
GAME_AT(struct game_statuswin, model, 0x30);
GAME_AT(struct game_statuswin, angles, 0x1500);
GAME_AT(struct game_statuswin, matrix, 0x150c);
GAME_AT(struct game_statuswin, font, 0x1530);
GAME_AT(struct game_statuswin, yields, 0x15a8);
GAME_AT(struct game_statuswin, font_size, 0x15ac);
GAME_AT(struct game_statuswin, x, 0x15b4);
GAME_AT(struct game_statuswin, sel_veh, 0x15bc);
GAME_AT(struct game_statuswin, sel_base, 0x15c0);
GAME_AT(struct game_statuswin, pending_a, 0x15c4);
GAME_AT(struct game_statuswin, pending_b, 0x15c8);
GAME_AT(struct game_statuswin, active_veh, 0x15cc);
GAME_AT(struct game_statuswin, active_base, 0x15d0);
GAME_AT(struct game_statuswin, battle, 0x15d4);
GAME_AT(struct game_statuswin, double_click, 0x15d8);
GAME_AT(struct game_statuswin, has_unit, 0x15dc);
GAME_AT(struct game_statuswin, spot, 0x15e0);
_Static_assert(sizeof(struct game_statuswin) == 0x15ec, "layout of the game's StatusWin");

// The game's data it reads (by address).
enum {
    MAIN_BUFFER = 0x7aec64,
    MAIN_WIN = 0x7ae820,
    FRAME_OUTER = 0x78d870,     // the two frame sprites the panels are boxed with
    FRAME_INNER = 0x78d7f8,
    SQUARE_RECT = 0x7af4ec,     // the rectangles (left, top, right, bottom): the square's panel, the active unit's, the strip's
    ACTIVE_RECT = 0x7af4fc,
    STRIP_RECT = 0x7af50c,
    STRIP_AREA_LEFT = 0x7af2d4, // the area the strip's icons spread over when there are more than fit
    STRIP_AREA_RIGHT = 0x7af2ec,
    PICTURE_SCALE = 0x691e6c,   // two words that scale the pictures while a square's picture is drawn: -1 otherwise
    PICTURE_DIV = 0x691e70,
    BUSY_TABLE = 0x7ae778,      // words by the game's mode (0x7d392c): while one is set the panel stays as it is
    BUSY_INDEX = 0x7d392c,
    STACK_SORTED = 0x7d3934,    // whether the stack is kept in order, so that the strip does not sort it
    TEXT_A = 0x8c6ba4,          // pairs of colours for the panel's text
    TEXT_PLAIN = 0x8c6b94,      // (0x8c6b8c the unit's name, 0x8c6bac and 0x8c6bb0 a unit in the strip)
    TEXT_NAME = 0x8c6b8c,
    TEXT_STRIP = 0x8c6bac,
    TEXT_DAMAGE = 0x68a5a4,
    BATTLE_WIN = 0x6eeed8,
    DATALINKS = 0x703ea0,
    BASE_WIN = 0x6a7628,
    CONSOLE = 0x9156b0,
    NET_DAEMON = 0x93cd90,
    TITLE_WIN = 0x937118,       // a window the panel shows over the map for a unit on the way to somewhere
    TITLE_DEST = 0x6874c4,
    STATUS_FLAGS = 0x939ed8,
    STATE = 0x9a64c0,
    MAP_FLAG = 0x939288,        // the title's "map" or "view" kind
    SEL_VEH = 0x93928c,         // the unit the player has picked
    NET_ENTRY = 0x93f660,
    VEH_FLAG_A = 0x93e944,
    VEH_FLAG_B = 0x93e8c8,
    FACTION_MASK = 0x93e8ec,
    CONTROL_TURN = 0x93a938,
    PREFERENCES = 0x9a6494,
    LANGUAGE = 0x9bc054,
    FACTION_DEFS = 0x946a50,
    CHASSIS_BASE = 0x94a330,
    TERRAIN_TECH = 0x949784,
    NEIGHBOUR_X = 0x66ef50,     // the eight squares round one: x then y offsets
    NEIGHBOUR_Y = 0x66ef74,
    FONT_FACE = 0x691b2c,       // char *: the name of the face
};
enum { FACTION_SIZE = 0x20cc, FDEF_SIZE = 0x59c };

// The strings the text is put together from (found in the game's data): the colon, the space, the comma and the brackets.
enum { S_COLON = 0x682e94, S_SPACE = 0x682820, S_COMMA = 0x682e90, S_OPEN = 0x682e9c, S_CLOSE = 0x682e98 };

static int32_t word(uint32_t addr) { return *(int32_t *)game_at(addr); }
static char *text(uint32_t addr) { return game_at(addr); }
static const char *label(int byte_offset) { return game_label(byte_offset / 4); }
static const char *string(const char *id) { return game_strings_get(game_strings, (char *)id); }
static const char *string_at(uint32_t addr) { return string((const char *)(uintptr_t)word(addr)); }
static struct game_buffer *main_buffer(void) { return game_at(MAIN_BUFFER); }
static struct game_win *main_win(void) { return game_at(MAIN_WIN); }

// The game's scratch text (0x9b86a0) the panel puts each line together in, and its helpers.
static void clear(void) { game_battle_text[0] = 0; }
static void cat(const char *s) { strcat(game_battle_text, s); }
static void cat_number(int v) {
    char n[34];
    sprintf(n, "%d", v);
    strcat(game_battle_text, n);
}
static int text_empty(void) { return game_battle_text[0] == 0; }
// Writes the scratch text at (x, y) in the current colours and returns where it ended.
static int write_line(int x, int y) { return buffer_write_l_3(main_buffer(), game_battle_text, x, y, (int)strlen(game_battle_text)); }
static void color(int a, int c) { buffer_set_text_color(main_buffer(), a, c, 1, 1); }
static void color_at(uint32_t addr) { color(word(addr), word(addr + 4)); }

static struct faction_def *fdef(int f) { return &game_faction_defs[f]; }
static int me(void) { return *game_current_faction; }
static int diplo(int faction, int toward) { return (int)game_factions[faction].diplo_status[toward]; }
static int flags80(void) { return *(uint8_t *)text(STATE) & 0x80; }
static int debug_on(void) { return (word(STATE) & 0x40) && (word(STATE) & 0x1000); }
static struct map_tile *tile_at(int x, int y) { return &(*game_map)[*game_map_half_width * y + (x >> 1)]; }
static void rect_inset(int32_t *r, int n) {
    r[0] += n;
    r[1] += n;
    r[2] -= n;
    r[3] -= n;
}
static void rect_inflate(int32_t *r, int n) { rect_inset(r, -n); }

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

// The three fonts made again when the screen's size changed what they should be, and their extra line spacing set on a wide screen.
static void resize_fonts(struct game_statuswin *sw) {
    int size = statuswin_font_size(*game_screen_width);
    if (size != sw->font_size) {
        sw->font_size = size;
        font_init(&sw->font[0], (const char *)(uintptr_t)word(FONT_FACE), size, 0);
        font_init(&sw->font[1], (const char *)(uintptr_t)word(FONT_FACE), size, 2);
        font_init(&sw->font[2], (const char *)(uintptr_t)word(FONT_FACE), size, 1);
    }
    if (*game_screen_width != 0x320) sw->font[0].first = sw->font[1].first = sw->font[2].first = 4;
}

// The union of the three rectangles the panel draws in, asked of the main window to copy to the screen.
static void update_panels(void) {
    struct ui_rect all;
    union_rect(&all, (struct ui_rect *)text(STRIP_RECT), (struct ui_rect *)text(SQUARE_RECT));
    union_rect(&all, &all, (struct ui_rect *)text(ACTIVE_RECT));
    graphic_win_soft_update_3(main_win(), (int32_t *)&all);
}

static void UI_THIS statuswin_draw_status(struct game_statuswin *sw, int a, int b, int c, int d, int e);
static void UI_THIS statuswin_draw_active(struct game_statuswin *sw, int veh, int base, int reg);
static int32_t *buffer_full(struct game_buffer *b) { return b->full; }

// A redraw of the square's panel after the fonts or the picked unit changed (every handler that does one makes it so).
static void redraw_panels(struct game_statuswin *sw) {
    resize_fonts(sw);
    statuswin_draw_status(sw, -1, -1, -1, -1, 0);
    update_panels();
}

// ---- The square ----------------------------------------------------------------------------------------------

// Which unit of the square the player may see, from the unit the console picked or the top one: the unit itself, or -1 when the
// faction can't see it (its square unexplored, or nothing of the unit or base there in view).
static int visible_unit(int veh, int x, int y, int bid) {
    int tail = veh;
    while (game_vehs[tail].prev_in_stack >= 0) tail = game_vehs[tail].prev_in_stack;
    int mine = me();
    if (game_vehs[veh].faction == mine || flags80()) return veh;
    if (!(game_factions[mine].flags & 0x200) && !(tile_at(x, y)->visibility & (1 << mine))) return -1;
    if (bid >= 0) {
        struct base *b = &game_bases[bid];
        int seen;
        if (diplo(b->faction, mine) & 1) {
            seen = 1;
        } else if (game_spying(b->faction)) {
            seen = 1;
        } else {
            seen = 0;
            int flat = word(0x94988c) & 1;
            int width = *game_map_width, height = *game_map_height;
            for (int k = 0; k < 8; k++) {
                int nx = b->x + word(NEIGHBOUR_X + 4 * k);
                if (!flat) {
                    if (nx >= 0) {
                        if (nx >= width) nx -= width;
                    } else {
                        nx += width;
                    }
                }
                int ny = b->y + word(NEIGHBOUR_Y + 4 * k);
                if (ny < 0 || ny >= height || nx < 0 || nx >= width) continue;
                struct map_tile *t = tile_at(nx, ny);
                int owner = (t->bits & 2) ? (t->owner_site & 0xf) : -1;
                if (owner >= 8) owner = -1;
                if (owner == mine) {
                    seen = 1;
                    break;
                }
            }
        }
        if (b->faction != mine && !flags80() && !(b->visibility & (1 << mine))) return -1;
        return seen ? veh : -1;
    }
    struct veh *t = &game_vehs[tail];
    if (t->faction == mine && (t->flags & 0x440) != 0x440) return veh;
    return (t->visibility & (1 << mine)) ? veh : -1;
}

// The pieces of text that make up the square's lines, from the squares' bits (the landmark ones are the rules' own names).
static void landmark_names(const struct map_tile *t, int expansion) {
    static const struct {
        uint32_t mask;
        uint32_t addr;
    } names[] = {{2, 0x94adec}, {1, 0x94ade4}, {4, 0x94adf4}, {8, 0x94adfc}, {0x40, 0x94ae14}, {0x100, 0x94ae24},
                 {0x200, 0x94ae2c}, {0x800, 0x94ae3c}, {0x2000, 0x94ae4c}};
    for (int i = 0; i < 9; i++) {
        uint32_t m = names[i].mask;
        if ((t->bits2 & (0x80000000u | m)) != m) continue;
        if (m == 4 && (t->climate & 0xe0) < 0x60) continue;
        cat(string_at(names[i].addr));
    }
    if (expansion != 0 && (t->bits2 & 0x80004000u) == 0x4000) cat(string_at(0x94ae54));
}

// The terrain's lines of a square that isn't shown as what it yields. `ypos` is where the first goes; the rectangle's top is
// moved to where the improvements start, as the original's wrapped text takes it. Returns the y after the last line.
static int terrain_lines(int x, int y, int left, int ypos, int lh, int32_t *r) {
    struct game_buffer *buf = main_buffer();
    struct map_tile *t = tile_at(x, y);
    int level = statuswin_altitude_class(t->climate);
    color_at(TEXT_A);
    if (level < 0) {
        clear();
        if (level == -3) cat(label(0x568));
        else if (level == -2) cat(label(0x304));
        else if (level == -1) cat(label(0x564));
        if (!text_empty()) write_line(left, ypos);
        ypos += lh;
    }
    clear();
    cat(label(level >= 0 ? 0x2c : 0x30));
    cat(text(S_COLON));
    int elev = elev_at(x, y);
    cat_number(elev < 0 ? -elev : elev);
    cat(label(0x34));
    if (!text_empty()) write_line(left, ypos);
    ypos += lh;
    int change = *game_climate_change;
    int shown_flood = 0;
    if (level < 0) {
        if (change < 0 && t->contour - *game_sea_level >= *game_dome_depth_a + change) shown_flood = 1;
    } else if (change > 0 && t->contour - *game_sea_level < *game_dome_depth_a + change) {
        shown_flood = 1;
    }
    if (shown_flood) {
        clear();
        cat(label(0x7fc));
        color(0xf9, -1);
        write_line(left, ypos);
        ypos += lh;
    }
    color_at(TEXT_A);
    int rock = t->rocky_lock >> 6;
    if (level >= 0) {
        clear();
        cat(label(0x164 + rock * 4));
        cat(text(S_SPACE));
        cat(text(0x6874fc));
        cat(text(S_SPACE));
        cat(label(0x50 + ((t->climate >> 3) & 3) * 4));
        write_line(left, ypos);
        ypos += lh;
        clear();
        landmark_names(t, *game_expansion);
        if (!text_empty()) {
            write_line(left, ypos);
            ypos += lh;
        }
        clear();
        if (t->bits & MAP_RIVER) cat(label(0x3a8));
        if ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40) {
            if (!text_empty()) cat(text(S_COMMA));
            cat(label(0x21c));
        } else if (t->bits & MAP_FOREST) {
            if (!text_empty()) cat(text(S_COMMA));
            cat(label(0x48c));
        }
        if (!text_empty()) {
            write_line(left, ypos);
            ypos += lh;
        }
    } else {
        clear();
        if (*game_expansion != 0 && (t->bits2 & 0x80008000u) == 0x8000) cat(string_at(0x94ae5c));
        if ((t->bits2 & 0x80) && (t->climate & 0xe0) < 0x60) cat(string_at(0x94ae1c));
        if ((t->bits2 & 0x80000400u) == 0x400) cat(string_at(0x94ae34));
        if ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40) {
            if (!text_empty()) cat(text(S_COMMA));
            cat(label(0x398));
        }
        if (!text_empty()) {
            write_line(left, ypos);
            ypos += lh;
        }
    }
    int bonus = bonus_at(x, y, 0);
    if (bonus > 0) {
        clear();
        cat(label(0x16c + bonus * 4));
        cat(text(S_SPACE));
        cat(label(0x17c));
        write_line(left, ypos);
        ypos += lh;
    }
    clear();
    uint32_t bits = t->bits;
    for (int i = 0; i < TERRAFORM_COUNT; i++) {
        uint32_t mask = game_terraform[i].bit;
        if (mask == 0 || !(bits & mask) || i == 4 || i == 10 || i == 11) continue;
        if (i == 5) {
            if (bits & 8) continue;
        } else if (i == 0) {
            if (bits & 0x80000) continue;
        }
        if (!text_empty()) cat(text(S_COMMA));
        int sea = (t->climate & 0xe0) < 0x60;
        cat(string(sea ? game_terraform[i].name_sea : game_terraform[i].name));
    }
    if (bits & 0x2000) {
        if (!text_empty()) cat(text(S_COMMA));
        cat(label(0x238));
    }
    if (!text_empty()) {
        r[1] = ypos;
        ypos = buffer_wrap_2(buf, game_battle_text, r);
    }
    return ypos;
}

// The square's lines when it is shown as what it yields: the nutrients, minerals and energy it gives the faction (and what
// they would be improved, where that differs or might be).
static int yield_lines(int x, int y, int left, int ypos, int lh) {
    struct map_tile *t = tile_at(x, y);
    int mine = me();
    ypos -= 4;
    color_at(TEXT_A);
    clear();
    cat(string_at(0x94615c));
    cat(text(S_COLON));
    cat(text(S_SPACE));
    int crop = crop_yield(mine, -1, x, y, 0);
    cat_number(crop);
    if (!text_empty()) write_line(left, ypos);
    ypos += lh;
    uint32_t bits = t->bits;
    if ((t->rocky_lock & 0xc0) < 0x80 && !(((bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40)) && !(bits & 0xa000)) {
        int print = 0;
        if (crop >= 2) {
            if (has_tech(game_rules_basic->tech_three_nutrients, mine)) print = 1;
            else if (bonus_at(x, y, 0) == 1) print = 1;
            else if (bits & MAP_CONDENSER) print = 1;
        } else {
            print = 1;
        }
        if (print) {
            clear();
            cat(text(S_SPACE));
            cat(text(S_SPACE));
            cat(label(0xfc4));
            cat(text(S_COLON));
            cat(text(S_SPACE));
            cat_number(crop + 1);
            if (!text_empty()) write_line(left, ypos);
        }
    }
    ypos += lh;
    clear();
    cat(string_at(0x946164));
    cat(text(S_COLON));
    cat(text(S_SPACE));
    int minerals = mine_yield(mine, -1, x, y, 0);
    cat_number(minerals);
    if (!text_empty()) write_line(left, ypos);
    ypos += lh;
    bits = t->bits;
    if (!(bits & 0x1002000) && !(bits & 0x10) && !(((bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40))) {
        if (mine_yield(mine, -1, x, y, 1) > minerals) {
            clear();
            cat(text(S_SPACE));
            cat(text(S_SPACE));
            cat(label(0xfc8));
            cat(text(S_COLON));
            cat(text(S_SPACE));
            cat_number(mine_yield(mine, -1, x, y, 1));
            if (!text_empty()) write_line(left, ypos);
        }
    }
    ypos += lh;
    clear();
    cat(string_at(0x94616c));
    cat(text(S_COLON));
    cat(text(S_SPACE));
    cat_number(energy_yield(mine, -1, x, y, 0));
    if (!text_empty()) write_line(left, ypos);
    ypos += lh;
    bits = t->bits;
    if (!(bits & 0x1002000) && !(bits & 0x40) && !(((bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40))) {
        clear();
        cat(text(S_SPACE));
        cat(text(S_SPACE));
        cat(label(0xfcc));
        cat(text(S_COLON));
        cat(text(S_SPACE));
        cat_number(energy_yield(mine, -1, x, y, 1));
        if (!text_empty()) write_line(left, ypos);
    }
    ypos += lh;
    return ypos;
}


// ---- The strip of stacked units ----------------------------------------------------------------------------

static void *strip_arrow(uint32_t addr) { return game_at(addr); }
static void arrow_hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, WIN_VT_HIDE); }
static void arrow_show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, WIN_VT_SHOW, 0); }

// The strip under the active unit's panel: a frame, and in it the icons of the base and of the units stacked on the square,
// from the last of the stack to the first, as many as fit (when more do, they are spread over a wider area and two of the
// three arrow windows show). `veh` is the unit the panel shows, `sel_base` the base shown on the square, or -1; `lh` the line
// height of the panel's font. Returns 0 when the game has not begun (nothing more is done then). Each icon is a hit rectangle of the object's Spot list (kind 0xb a base, 0 a unit).
static int draw_strip(struct game_statuswin *sw, int veh, int x, int y, int sel_base, int lh) {
    struct game_buffer *buf = main_buffer();
    int32_t r[4];
    int32_t *strip = (int32_t *)text(STRIP_RECT);
    memcpy(r, strip, 16);
    buffer_box_sprite(buf, r, game_at(FRAME_OUTER));
    rect_inset(r, 3);
    buffer_box_sprite(buf, r, game_at(FRAME_INNER));
    rect_inset(r, 3);
    if (*game_turn == 0) return 0;
    int icon = statuswin_scaled(word(PICTURE_SCALE), 100, word(PICTURE_DIV), 0x32) + 0xb;
    int strip_h = strip[3] - strip[1] - 0xc;
    int count = 0;
    int base = base_at(x, y);
    if (base >= 0) {
        if (flags80() || game_bases[base].faction == me() || (game_bases[base].visibility & (1 << me()))) count = 1;
        else base = -1;
    }
    int tail = veh;
    if (veh >= 0) {
        while (game_vehs[tail].prev_in_stack >= 0) tail = game_vehs[tail].prev_in_stack;
        for (int u = tail; u >= 0; u = game_vehs[u].next_in_stack) count++;
    }
    int capacity = statuswin_strip_capacity(r[2] - r[0], icon);
    if (count <= capacity) {
        arrow_hide(strip_arrow(0x7b5080));
        arrow_hide(strip_arrow(0x7b5bcc));
        arrow_hide(strip_arrow(0x7b6718));
    } else {
        r[0] = word(STRIP_AREA_LEFT) + 1;
        r[2] = word(STRIP_AREA_RIGHT) - 1;
        icon = statuswin_strip_icon(r[2] - r[0], capacity);
        arrow_show(strip_arrow(0x7b5080));
        arrow_show(strip_arrow(0x7b6718));
    }
    int ix = r[0];
    int top = r[1];
    r[2] = ix + icon;
    r[3] = top + strip_h;
    int32_t clip[4] = {r[0] - 2, r[1] - 2, r[2] + 2, r[3] + 2};
    buffer_set_clip(buf, clip);
    int placed = 0;
    if (base >= 0) {
        placed = 1;
        game_base_draw(buf, base, ix + 0xb, top - 1, -8, 0x40);
        game_base_draw(buf, base, ix, top - 1, 0, 0x1800);
        spot_add(&sw->spot, base, 0xb, r[0], r[1], r[2] + 5 - r[0], r[3] - r[1]);
        if (base == sel_base) {
            int32_t b[4] = {r[0], r[1], r[2], r[3]};
            rect_inflate(b, 2);
            buffer_box_rect(buf, b, 0xae, 0xae);
        }
        r[0] += icon + 5;
        r[2] += icon + 5;
        ix = ix + icon + 5;
    }
    if (veh < 0) return 1;
    if (word(STACK_SORTED) == 0) stack_sort_2(veh);
    // (the sort may have moved units, so the end of the stack is found again)
    tail = veh;
    while (game_vehs[tail].prev_in_stack >= 0) tail = game_vehs[tail].prev_in_stack;
    for (int u = tail; u >= 0 && placed < capacity; u = game_vehs[u].next_in_stack, placed++) {
        int32_t c[4] = {r[0], r[1], r[2], r[3]};
        rect_inflate(c, 2);
        buffer_set_clip(buf, c);
        game_veh_draw(buf, u, ix + 0xb, top - 1, -8, 0x400, 0);
        game_veh_draw(buf, u, ix, top - 1, 0, 0x1800, 0);
        buffer_set_font(buf, &sw->font[0], NULL, NULL, NULL);
        clear();
        struct veh *v = &game_vehs[u];
        if (v->order >= 4 && v->order < 0x18 && (v->faction == me() || flags80() || (diplo(me(), v->faction) & 1))) {
            int turns = game_action_terraform(u, v->order - 4, 0);
            cat_number(turns + 1);
            cat(text(S_SPACE));
            cat(label(turns == 1 ? 0xc0 : 0xc4));
            color(0xfb, 3);
        } else {
            game_say_stats_3(game_battle_text, v->proto);
            if (v->state & 0x80) cat(text(0x687500));
            if (debug_on()) {
                cat(text(S_SPACE));
                cat(text(S_OPEN));
                cat_number(u);
                cat(text(S_CLOSE));
            }
            color(word(TEXT_STRIP), word(TEXT_STRIP + 4));
        }
        int text_y = sw->font[0].f1c - lh + r[3] + 1;
        buffer_write_cent_l_5(buf, game_battle_text, ix, text_y, icon, (int)strlen(game_battle_text));
        if (u == veh && sw->sel_base < 0) {
            int32_t b[4] = {r[0], r[1], r[2], r[3]};
            rect_inflate(b, 2);
            buffer_box_rect(buf, b, 0xae, 0xae);
        }
        spot_add(&sw->spot, u, 0, r[0], r[1], r[2] + 5 - r[0], r[3] - r[1]);
        r[0] += icon + 5;
        r[2] += icon + 5;
        ix += icon + 5;
    }
    return 1;
}

// ---- The square's panel ---------------------------------------------------------------------------------------

// The panel for the square at (sw->x, sw->y): the frame, the picture of the square and its lines, the active unit or base
// drawn by draw_active, the strip of units. With a and b (two units) it is the report of a battle instead, drawn by the battle
// window. Does nothing while the game's mode says the panel is to stay as it is.
static void UI_THIS statuswin_draw_status(struct game_statuswin *sw, int a, int b, int c, int d, int e) {
    sw->has_unit = 0;
    if (a >= 0 && b >= 0) {
        game_battle_report(game_at(BATTLE_WIN), a, b, c, d, e);
        return;
    }
    if (word(BUSY_TABLE + 4 * word(BUSY_INDEX)) != 0) return;
    static const int spot_kinds[] = {0, 1, 2, 3, 0xa, 0xb};
    for (int i = 0; i < 6; i++) spot_kill_type(&sw->spot, spot_kinds[i]);
    struct font *f0 = &sw->font[0], *f1 = &sw->font[1], *f2 = &sw->font[2];
    font_width(f0, text(0x6874f8));
    int lh = statuswin_line_height(f0->first, f0->ascent, f0->height);
    struct game_buffer *buf = main_buffer();
    buffer_set_font(buf, f0, f2, f1, NULL);

    int mine = me();
    int picked = word(SEL_VEH);
    int turn = *game_turn;
    int veh = picked;
    int x, y;
    if (word(MAP_FLAG) != 0 && picked >= 0 && turn != 0 && sw->x == game_vehs[picked].x && sw->y == game_vehs[picked].y) {
        x = game_vehs[picked].x;
        y = game_vehs[picked].y;
    } else {
        x = sw->x;
        y = sw->y;
        veh = turn ? veh_at(x, y) : -1;
        int bid = base_at(x, y);
        if (veh >= 0) veh = visible_unit(veh, x, y, bid);
    }

    // The base or unit picked on this square takes the panel.
    int base_here = -1;
    if (sw->sel_veh >= 0) {
        if (game_vehs[sw->sel_veh].x == x && game_vehs[sw->sel_veh].y == y) veh = sw->sel_veh;
    } else if (sw->sel_base >= 0) {
        if (game_bases[sw->sel_base].x == x && game_bases[sw->sel_base].y == y) base_here = sw->sel_base;
    }
    sw->has_unit = veh >= 0;
    if (base_here >= 0 && !flags80()) {
        struct base *bb = &game_bases[base_here];
        if (bb->faction != mine && !(bb->visibility & (1 << mine))) {
            base_here = -1;
            sw->sel_base = -1;
        }
    }

    int32_t r[4];
    memcpy(r, text(SQUARE_RECT), 16);
    buffer_box_sprite(buf, r, game_at(FRAME_OUTER));
    rect_inset(r, 3);
    buffer_box_sprite(buf, r, game_at(FRAME_INNER));
    rect_inset(r, 3);
    buffer_set_clip(buf, r);
    color_at(TEXT_A);

    int unexplored = 0;
    if (!flags80() && !(game_factions[mine].flags & 0x200) && !(tile_at(x, y)->visibility & (1 << mine))) {
        unexplored = 1;
        clear();
        cat(label(0x300));
        buffer_write_cent_l_7(buf, game_battle_text, r, (int)strlen(game_battle_text));
    }
    int ypos = 1;
    if (!unexplored) {
        *(int32_t *)text(PICTURE_SCALE) = r[2] - r[0] - 4;
        *(int32_t *)text(PICTURE_DIV) = 100;
        game_terrain_poly(buf, 0, 0, flags80() ? -1 : mine, 0, 0x621, x, y, r[0] + 2, r[1] + 1);
        *(int32_t *)text(PICTURE_SCALE) = -1;
        *(int32_t *)text(PICTURE_DIV) = -1;
        int left = r[0];
        ypos = statuswin_text_top(left, r[2], r[1]);
        if (sw->yields) ypos = yield_lines(x, y, left, ypos, lh);
        else ypos = terrain_lines(x, y, left, ypos, lh, r);
    }

    // What the debug flags add: the square's region at the bottom left, and its coordinates at the bottom right.
    int bottom = r[3];
    if (debug_on()) {
        clear();
        cat(text(S_OPEN));
        cat_number(tile_at(x, y)->region);
        cat(text(S_CLOSE));
        color_at(TEXT_A);
        write_line(r[0] + 2, bottom - lh);
    }
    if (debug_on() || ypos < bottom - lh) {
        clear();
        cat(text(S_OPEN));
        cat_number(x);
        cat(text(S_SPACE));
        cat(text(S_COMMA));
        cat_number(y);
        cat(text(S_CLOSE));
        color_at(TEXT_A);
        buffer_write_right_l_5(buf, game_battle_text, r[2] - 2, bottom - lh, (int)strlen(game_battle_text));
    }

    buffer_set_clip(buf, buffer_full(buf));
    int32_t *square = (int32_t *)text(SQUARE_RECT);
    spot_add(&sw->spot, 0, 3, square[0], square[1], square[2] - square[0], square[3] - square[1]);
    graphic_win_soft_update_3(main_win(), square);
    sw->pending_b = sw->pending_a = -1;
    sw->active_veh = veh;
    sw->active_base = base_here;
    statuswin_draw_active(sw, veh, base_here, 1);
    if (!draw_strip(sw, veh, x, y, base_here, lh)) return;
    game_find_font(0x14, 0);
    graphic_win_soft_update_3(main_win(), (int32_t *)text(STRIP_RECT));
    buffer_set_clip(buf, buffer_full(buf));
}

// ---- The active unit or base ------------------------------------------------------------------------------------

// The faction's name as the panel shows it under a foreign unit's or base's name: its adjective, or in other languages its noun
// in brackets (the grammar words set first), wrapped and centred; returns where the next line goes.
static int owner_line(int faction, int left, int width, int ypos) {
    clear();
    struct faction_def *def = fdef(faction);
    if (*game_language == 0) {
        cat(def->adjective);
    } else {
        cat(text(S_OPEN));
        *(int32_t *)text(0x9bbfec) = def->noun_gender;
        *(int32_t *)text(0x9bbff0) = def->noun_plural;
        cat(def->noun);
        cat(text(S_CLOSE));
    }
    return buffer_wrap_cent_4(main_buffer(), game_battle_text, left, ypos, width);
}

// The active unit: its picture, name and stats, the faction it belongs to, orders and (for the player's own units) home base,
// moves, range, cargo and damage. `reg` has the hit rectangle of the picture added, else the panel is copied to the screen.
static void active_unit(struct game_statuswin *sw, int veh, int reg, int32_t *r, int lh) {
    struct game_buffer *buf = main_buffer();
    struct font *f0 = &sw->font[0], *f2 = &sw->font[2];
    int mine = me();
    int left = r[0], top = r[1], right = r[2];
    int width = right - left;
    int size = statuswin_scaled(word(PICTURE_SCALE), 100, word(PICTURE_DIV), 0x4b);
    buffer_set_clip(buf, r);
    game_veh_draw(buf, veh, statuswin_centre_x(left, right, size), top, -4, 0x48, 0);
    game_veh_draw(buf, veh, left, top, 0, 0x1800, 0);
    buffer_set_font(buf, f0, NULL, NULL, NULL);
    int ypos = statuswin_scaled(word(PICTURE_SCALE), 75, word(PICTURE_DIV), 0x38) + top + 8;
    struct veh *v = &game_vehs[veh];
    struct unit_proto *proto = &game_protos[v->proto];
    clear();
    cat(proto->name);
    if (debug_on()) {
        cat(text(S_OPEN));
        cat_number(veh);
        cat(text(S_CLOSE));
    }
    color_at(TEXT_NAME);
    ypos = buffer_wrap_cent(buf, f2, game_battle_text, left, ypos, width);
    color_at(TEXT_PLAIN);
    if (v->faction != 0 && v->faction != mine) ypos = owner_line(v->faction, left, width, ypos);

    // The stats, with the unit's abilities after them.
    clear();
    cat(text(S_OPEN));
    game_say_stats_3(game_battle_text, v->proto);
    if (v->state & 0x80) cat(text(0x6874d8));
    for (int i = 0; i < 29; i++) {
        if (!has_abil(v->proto, 1u << i)) continue;
        if (strlen(string(game_abilities[i].name)) == 0) continue;
        cat(text(S_COMMA));
        cat(string(game_abilities[i].name));
    }
    cat(text(S_CLOSE));
    ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);

    int show_morale;
    if (v->faction != 0 && proto->plan < 8) show_morale = 1;
    else show_morale = proto->plan == 0xb && morale_veh(veh, 1, 0) > 1;
    if (show_morale) {
        clear();
        game_say_morale(game_battle_text, veh, 0);
        if (v->faction != mine && !flags80()) ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
        else ypos = buffer_wrap_3(buf, game_battle_text, left, ypos, width);
    }

    if (v->faction == mine || flags80()) {
        // Where it comes from.
        clear();
        if (v->home_base < 0) cat(label(0x308));
        else cat(game_bases[v->home_base].name);
        ypos = buffer_wrap_3(buf, game_battle_text, left, ypos, width);
        int mps = game_rules_basic->move_rate_roads;
        if (v->faction == mine) {
            if (veh_speed(veh, 0) - v->moves_spent <= 0) {
                clear();
                cat(label(0x9e4));
                color(0xf9, -1);
                ypos = buffer_wrap_3(buf, game_battle_text, left, ypos, width);
            } else {
                clear();
                cat(label(0x1c));
                cat(text(S_COLON));
                int moves = statuswin_moves_left(veh_speed(veh, 0), v->moves_spent);
                cat_number(moves / mps);
                if (moves % mps != 0) {
                    cat(text(S_SPACE));
                    cat_number(moves % mps);
                    cat(text(0x6874dc));
                    cat_number(mps);
                }
                ypos = buffer_wrap_3(buf, game_battle_text, left, ypos, width);
            }
        }
        // Fliers: the turns they have in the air.
        int range = *(uint8_t *)((char *)game_chassis + proto->chassis * 0x90 + 0x4a);
        if (range != 0) {
            clear();
            cat(label(0x30c));
            cat(text(S_COLON));
            int label_end = write_line(left, ypos);
            clear();
            int moves_left = statuswin_moves_left(veh_speed(veh, 0), v->moves_spent);
            int turns = statuswin_flight_turns(range, moves_left, v->movement_turns);
            cat_number((veh_speed(veh, 0) * turns - v->moves_spent) / mps);
            cat(text(0x6874e0));
            cat_number(veh_speed(veh, 0) * range / mps);
            color(0xfa, -1);
            if (v->movement_turns != 0 && v->movement_turns >= (range >> 1)) color(0xfb, -1);
            if (range == 1 && statuswin_moves_left(veh_speed(veh, 0), v->moves_spent) <= veh_speed(veh, 0) / 2) color(0xfb, -1);
            if (action_home(veh, -1)) color(0xf9, -1);
            write_line(label_end, ypos);
            ypos += lh;
        }
        color_at(TEXT_PLAIN);
        // Cargo.
        if (veh_cargo(veh) != 0) {
            clear();
            cat(label(0x360));
            cat(text(S_COLON));
            int carried = stack_veh(veh, 0);
            cat_number(veh_cargo(veh) - carried);
            cat(text(0x6874e4));
            cat_number(veh_cargo(veh));
            write_line(left, ypos);
            ypos += lh;
        }
    }
    // The damage the unit has taken.
    if (v->damage != 0) {
        clear();
        cat(label(0x628));
        cat(text(S_COLON));
        int damage_x = write_line(left, ypos);
        int hit_points = statuswin_hit_points(proto->plan, proto->reactor);
        color(0xfa, -1);
        int level = statuswin_damage_level(hit_points, v->damage);
        if (level >= 1) color(0xfb, -1);
        if (level >= 2) color(0xf9, -1);
        clear();
        cat_number(statuswin_damage_percent(hit_points, v->damage));
        cat(text(0x6874e8));
        write_line(damage_x, ypos);
        color(word(0x68a5a4), word(0x68a5b0));
        ypos += lh;
    }
    if (v->faction == mine || flags80() || (diplo(mine, v->faction) & 1)) {
        if (v->state & 0x80) {
            clear();
            cat(label(0x1048));
            color(0xfb, -1);
            write_line(left, ypos);
            ypos += lh;
        }
    }
    color_at(TEXT_PLAIN);
    if (v->faction == mine || flags80() || (diplo(mine, v->faction) & 1)) {
        clear();
        game_say_orders(game_battle_text, veh);
        int after = buffer_wrap_3(buf, game_battle_text, left, ypos, width);
        if (v->order >= 4 && v->order < 0x18) {
            int turns = game_action_terraform(veh, v->order - 4, 0);
            clear();
            cat(text(0x6874ec));
            cat(label(0x71c));
            cat(text(S_COLON));
            cat_number(turns + 1);
            cat(text(0x6874f0));
            color(0xfb, -1);
            write_line(left, after);
        }
    }
    if (reg != 0) spot_add(&sw->spot, veh, 0xa, r[0], r[1], r[2] - r[0], r[3] - r[1]);
    if ((v->faction == mine || flags80()) && v->order == 0x18 && !(word(PREFERENCES) & 0x2000000)) {
        game_draw_my_dest(veh);
        game_win_show(game_at(TITLE_WIN), 0);
    } else {
        *(int32_t *)text(TITLE_DEST) = -1;
        game_win_hide(game_at(TITLE_WIN));
    }
    color_at(TEXT_PLAIN);
    buffer_set_clip(buf, buffer_full(buf));
    if (reg == 0) graphic_win_update_2(main_win(), r, 0);
}

// The active base: its picture and name, the faction it belongs to, and for a foreign base the headquarters or size, its
// defences and what it is building (for the player's own, or one it has a treaty with or spies on).
static void active_base(struct game_statuswin *sw, int bid, int reg, int32_t *r) {
    struct game_buffer *buf = main_buffer();
    struct font *f0 = &sw->font[0], *f2 = &sw->font[2];
    int mine = me();
    int left = r[0], top = r[1], right = r[2];
    int width = right - left;
    struct base *b = &game_bases[bid];
    int size = statuswin_scaled(word(PICTURE_SCALE), 100, word(PICTURE_DIV), 0x4b);
    game_base_draw(buf, bid, statuswin_centre_x(left, right, size), top, -4, 0x40);
    game_base_draw(buf, bid, statuswin_centre_x(left, right, size), top, 0, 0x1800);
    color_at(TEXT_PLAIN);
    buffer_set_font(buf, f0, NULL, NULL, NULL);
    int ypos = statuswin_scaled(word(PICTURE_SCALE), 75, word(PICTURE_DIV), 0x38) + top + 8;
    clear();
    cat(b->name);
    if (debug_on()) {
        cat(text(S_SPACE));
        cat(text(S_OPEN));
        cat_number(bid);
        cat(text(S_CLOSE));
    }
    ypos = buffer_wrap_cent(buf, f2, game_battle_text, left, ypos, width);
    if (b->faction != mine) {
        ypos = owner_line(b->faction, left, width, ypos);
        clear();
        int offset;
        uint32_t mask;
        bitmask(1, &offset, &mask);
        if (b->facilities_built[offset] & mask) cat(label(0x5c4));
        else if (b->pop_size <= 1) cat(label(0x2f0));
        else if (b->pop_size <= game_rules_basic->pop_limit_hab_complex) cat(label(0x2f4));
        else cat(label(0x570));
        ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
        clear();
        bitmask(5, &offset, &mask);
        if (b->facilities_built[offset] & mask) {
            cat(string(game_facilities[5].name));
        } else {
            bitmask(4, &offset, &mask);
            if (b->facilities_built[offset] & mask) cat(string(game_facilities[4].name));
        }
        if (!text_empty()) ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
        if (b->state & 2) {
            clear();
            cat(label(0xb04));
            color(0xf9, -1);
            ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
        }
        if (b->nerve_staple_turns != 0) {
            clear();
            cat(label(0xb08));
            cat(text(S_COLON));
            cat_number(b->nerve_staple_turns);
            color(0xfb, -1);
            ypos = buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
        }
    }
    color_at(TEXT_PLAIN);
    if (b->faction == mine || flags80() || (diplo(mine, b->faction) & 1) || game_spying(b->faction)) {
        clear();
        cat(label(0x798));
        cat(text(S_COLON));
        int q = b->queue[0];
        int cost;
        if (q >= 0) {
            cat(game_protos[q].name);
            cost = game_protos[q].cost;
        } else {
            cat(string(game_facilities[-q].name));
            cost = game_facilities[-q].cost;
        }
        if (q != -69) {
            cat(text(S_SPACE));
            cat(text(S_OPEN));
            cat_number(b->minerals);
            cat(text(0x6874f4));
            cat_number(cost_factor(b->faction, 1, -1) * cost);
            cat(text(S_CLOSE));
        }
        if (!text_empty()) buffer_wrap_cent_4(buf, game_battle_text, left, ypos, width);
    }
    if (reg != 0) spot_add(&sw->spot, bid, 1, r[0], r[1], r[2] - r[0], r[3] - r[1]);
    else graphic_win_update_2(main_win(), r, 0);
}

// The active picture of the panel: the unit (or, if there is none, the base) on the square, in a frame of its own. `reg` says
// the hit rectangle is added (the square's panel was just drawn), else the panel is copied to the screen.
static void UI_THIS statuswin_draw_active(struct game_statuswin *sw, int veh, int base, int reg) {
    struct font *f0 = &sw->font[0];
    int lh = statuswin_line_height(f0->first, f0->ascent, f0->height);
    struct game_buffer *buf = main_buffer();
    buffer_set_font(buf, f0, &sw->font[2], &sw->font[1], NULL);
    int32_t r[4];
    memcpy(r, text(ACTIVE_RECT), 16);
    buffer_box_sprite(buf, r, game_at(FRAME_OUTER));
    rect_inset(r, 3);
    buffer_box_sprite(buf, r, game_at(FRAME_INNER));
    rect_inset(r, 3);
    if (base < 0 && veh >= 0) {
        active_unit(sw, veh, reg, r, lh);
        return;
    }
    *(int32_t *)text(TITLE_DEST) = -1;
    game_win_hide(game_at(TITLE_WIN));
    color_at(TEXT_PLAIN);
    buffer_set_clip(buf, buffer_full(buf));
    if (base >= 0) active_base(sw, base, reg, r);
    else if (reg == 0) graphic_win_update_2(main_win(), r, 0);
}

// ---- The three entries a check holds ----------------------------------------------------------------------------

static struct hook_stats stats_held_draw = {"(held StatusWin_draw)"};
static struct hook_stats stats_held_reset = {"(held StatusWin_reset)"};
static struct hook_stats stats_held_redraw = {"(held StatusWin_redraw)"};

// The square's panel drawn for the combat code (and its picture copied to the screen). While a check protects state the
// original's quiet hook held the call (drawing in a check produces spurious differences), and so does this.
static void UI_THIS statuswin_draw(struct game_statuswin *sw, int a, int b, int c, int d, int e) {
    if (hooks_protecting()) {
        stats_held_draw.calls++;
        return;
    }
    statuswin_draw_status(sw, a, b, c, d, e);
    update_panels();
}

// Asks the game's battle mode to end, if the panel shows a battle.
static void UI_THIS statuswin_reset(struct game_statuswin *sw) {
    if (hooks_protecting()) {
        stats_held_reset.calls++;
        return;
    }
    if (sw->battle == 0) return;
    sw->battle = 0;
    game_release_iface_mode(game_at(BATTLE_WIN));
}

// The panel drawn again after the screen's mode changed (the fonts made again first).
static void UI_THIS statuswin_redraw(struct game_statuswin *sw) {
    if (hooks_protecting()) {
        stats_held_redraw.calls++;
        return;
    }
    redraw_panels(sw);
}
static void UI_THIS statuswin_on_redraw(struct game_statuswin *sw) { redraw_panels(sw); }

// The fonts made again for the screen's size (called by nothing else in the class).
static void UI_THIS statuswin_resize_fonts(struct game_statuswin *sw) { resize_fonts(sw); }

// The active picture drawn again when the game asked for it (the word at +0x15c4 or +0x15c8 set), and the asking cleared.
static void UI_THIS statuswin_reset_active(struct game_statuswin *sw) {
    if (sw->pending_a < 0 && sw->pending_b < 0) return;
    sw->pending_b = sw->pending_a = -1;
    statuswin_draw_active(sw, sw->active_veh, sw->active_base, 0);
}

// ---- The title -----------------------------------------------------------------------------------------------

// The text of the line over the panel (the game's scratch text): a fixed label for the kinds of title (flags 1, 0x10, 8, 2),
// else "<label>: <view or map> <the active unit's name> <where>".
static void UI_THIS statuswin_set_title(struct game_statuswin *sw, int flags) {
    clear();
    if (flags != 0) {
        int off;
        if (flags & 1) off = 0x6b4;
        else if (flags & 0x10) off = 0x6f8;
        else if (flags & 8) off = 0x6fc;
        else if (flags & 2) off = 0x6b8;
        else off = 0x250;
        cat(label(off));
        return;
    }
    cat(label(0x8c));
    if (*game_turn == 0) return;
    cat(text(S_COLON));
    int view = word(MAP_FLAG);
    int x, y;
    if (view == 0) {
        cat(label(0xc));
        x = sw->x;
        y = sw->y;
    } else {
        cat(label(8));
        int picked = word(SEL_VEH);
        if (picked < 0) return;
        cat(text(S_SPACE));
        x = game_vehs[picked].x;
        y = game_vehs[picked].y;
        cat(game_protos[game_vehs[picked].proto].name);
        cat(text(S_SPACE));
    }
    cat(text(S_SPACE));
    game_say_loc(game_battle_text, x, y, view, view == 0, view);
}

// ---- Moving about -----------------------------------------------------------------------------------------------

static void UI_THIS statuswin_set_loc(struct game_statuswin *sw, int x, int y) {
    sw->x = x;
    sw->y = y;
    sw->sel_base = -1;
    sw->sel_veh = -1;
}

// The arrow keys on the stack: the next unit up the stack (or, past the first, the base on the square).
static void UI_THIS statuswin_arrow_left(struct game_statuswin *sw) {
    if (sw->sel_veh == -1) return;
    int next = game_vehs[sw->sel_veh].prev_in_stack;
    if (next >= 0) {
        sw->sel_veh = next;
        redraw_panels(sw);
        return;
    }
    int base = base_at(sw->x, sw->y);
    if (base < 0) return;
    sw->sel_base = base;
    sw->sel_veh = -1;
    redraw_panels(sw);
}

// The other way: from the base back to the unit on the square, or the previous unit down the stack.
static void UI_THIS statuswin_arrow_right(struct game_statuswin *sw) {
    if (sw->sel_base >= 0) {
        int veh = veh_at(sw->x, sw->y);
        if (sw->has_unit == 0 || veh < 0) return;
        sw->sel_base = -1;
        sw->sel_veh = veh;
        redraw_panels(sw);
        return;
    }
    if (sw->sel_veh < 0) return;
    int prev = game_vehs[sw->sel_veh].next_in_stack;
    if (prev < 0) return;
    sw->sel_veh = prev;
    redraw_panels(sw);
}

// The action on what is picked: the unit becomes the one the player moves, or the base's window opens.
static void UI_THIS statuswin_activate(struct game_statuswin *sw) {
    int picked = sw->sel_veh;
    if (picked >= 0) {
        veh_wake(picked);
        *(int32_t *)text(SEL_VEH) = picked;
        game_console_set_move(game_at(CONSOLE), 1);
        return;
    }
    if (sw->sel_base >= 0) game_base_win_zoom(game_at(BASE_WIN), sw->sel_base, 0);
}

static void UI_CDECL statuswin_hide_title(void) {
    *(int32_t *)text(TITLE_DEST) = -1;
    game_win_hide(game_at(TITLE_WIN));
}

static void UI_CDECL statuswin_flag(void) { *(uint32_t *)text(STATUS_FLAGS) |= 0x20; }

// ---- The mouse -----------------------------------------------------------------------------------------------

// Makes `veh` the unit the player moves, as the console does for a click on a unit: locked over the network, woken, and the
// console told to take it.
static void make_active(int veh) {
    if (!veh_selectable(veh)) return;
    int id = veh;
    *(int32_t *)text(SEL_VEH) = veh;
    if (game_net_lock_veh(game_at(NET_DAEMON), &id, 0, -1, -1, 0) == 0) {
        veh_wake(id);
        game_synch_veh(id);
        game_net_await_synch(game_at(NET_DAEMON));
        game_net_unlock_veh(game_at(NET_DAEMON));
    }
    game_console_set_move(game_at(CONSOLE), 1);
}

// Whether the player may look into a base: all of them with the view on everything, else its own, one it spies on or has a
// treaty with.
static int base_open_ok(int bid) {
    if (flags80()) return 1;
    int owner = game_bases[bid].faction;
    if (owner == me()) return 1;
    if (game_spying(owner)) return 1;
    return (diplo(me(), owner) & 1) != 0;
}

void UI_THIS statuswin_on_left_click_c(struct game_statuswin *sw, int x, int y);
extern uint8_t infowin_entry_byte;
extern void UI_THIS infowin_reset_c(void *iw);

// A left click: the battle's report ends, and what is under the mouse takes the panel: a unit or base icon picks it (one
// click activates a unit, opens a base), the square's picture flips between its description and its yields. A double click
// is passed on with a flag that makes this do nothing at all.
void UI_THIS statuswin_on_left_click_c(struct game_statuswin *sw, int x, int y) {
    if (sw->double_click != 0) return;
    infowin_reset_c(game_at(0x7ad2a0));
    if (sw->battle != 0) {
        sw->battle = 0;
        game_release_iface_mode(game_at(BATTLE_WIN));
    }
    *(int32_t *)text(0x6e8150) = 0;
    int id, kind;
    if (spot_check(&sw->spot, x, y, &id, &kind) < 0) return;
    switch (kind) {
    case 0:
    case 10:
        make_active(id);
        break;
    case 1:
    case 11:
        if (base_open_ok(id)) game_base_win_zoom(game_at(BASE_WIN), id, 0);
        break;
    case 3:
        sw->yields = ~sw->yields;
        statuswin_draw_status(sw, -1, -1, -1, -1, 0);
        break;
    default:
        break;
    }
}

// The click's entry keeps the top byte of EBX, which the original's own code hands to the info panel's reset untouched
// (see infowin.c).
__asm__(".text\n"
        ".globl statuswin_on_left_click\n"
        "statuswin_on_left_click:\n"
        "    movl %ebx, %eax\n"
        "    shrl $24, %eax\n"
        "    movb %al, infowin_entry_byte\n"
        "    jmp statuswin_on_left_click_c\n");
void UI_THIS statuswin_on_left_click(struct game_statuswin *sw, int x, int y);

static void UI_THIS statuswin_on_left_double_click(struct game_statuswin *sw, int x, int y) {
    sw->double_click = 1;
    statuswin_on_left_click_c(sw, x, y);
    sw->double_click = 0;
}

// ---- The right button's menu --------------------------------------------------------------------------------

enum { POPMENU_SIZE = 0x62d4, POPMENU_PULLDOWN = 0x537c, POPMENU_DIALOGS = 0x21d0 };
enum { VTABLE_POPMENU = 0x66a8a8, VTABLE_POPMENU_BUF = 0x66a8a0, VTABLE_PULLDOWN = 0x66a738, VTABLE_PULLDOWN_BUF = 0x66a730 };
enum { MENU_SEPARATOR_ID = 0x63, MENU_HELP_ID = 0x14 };

// The menu on the square's picture: whether it shows what the square is or what it yields, or a Datalinks page on the planet.
static void picture_menu(struct game_statuswin *sw) {
    uint8_t menu[POPMENU_SIZE] __attribute__((aligned(4)));
    game_popup_ctor(menu);
    pulldown_ctor((struct game_pulldown *)(menu + POPMENU_PULLDOWN));
    *(uint32_t *)menu = game_va(VTABLE_POPMENU);
    *(uint32_t *)(menu + 0x444) = game_va(VTABLE_POPMENU_BUF);
    *(uint32_t *)(menu + POPMENU_PULLDOWN) = game_va(VTABLE_PULLDOWN);
    *(uint32_t *)(menu + POPMENU_PULLDOWN + 0x444) = game_va(VTABLE_PULLDOWN_BUF);
    game_popmenu_init(menu);
    game_dialogs_item(menu + POPMENU_DIALOGS, (char *)(uintptr_t)label(0xfec), 0);
    game_dialogs_item(menu + POPMENU_DIALOGS, (char *)(uintptr_t)label(0xff0), 1);
    game_dialogs_item(menu + POPMENU_DIALOGS, (char *)(uintptr_t)label(0xff4), 2);
    int32_t mx, my;
    win_get_mouse_pos(main_win(), &mx, &my);
    win_client_to_screen_2(main_win(), &mx, &my);
    int pick = game_popmenu_exec(menu, mx, my, 0);
    if (pick >= 0) {
        if (pick == 0 || pick == 1) {
            sw->yields = pick == 0 ? 0 : -1;
            statuswin_draw_status(sw, -1, -1, -1, -1, 0);
        } else if (pick == 2) {
            game_datalink_exec(game_at(DATALINKS), 9, 0);
        }
    }
    pulldown_dtor((struct game_pulldown *)(menu + POPMENU_PULLDOWN));
    game_popup_dtor(menu);
}

// A right click: a menu by what is under the mouse (a unit: activate or pick it; a base: open, production, hurry, governor
// settings; the square's picture: its own menu) and Help; the choice is carried out here.
static void UI_THIS statuswin_on_right_click(struct game_statuswin *sw, int x, int y, int unused) {
    (void)unused;  // (the original pops three arguments: ret 0xc)
    struct game_pulldown menu;
    pulldown_ctor(&menu);
    pulldown_init(&menu.win, 0);
    int id, kind;
    int found = spot_check(&sw->spot, x, y, &id, &kind) >= 0;
    int base_id = -1;
    int mine = me();
    if (found && kind == 3) {
        picture_menu(sw);
        pulldown_dtor(&menu);
        return;
    }
    if (found && kind != 2) {
        if (kind == 0 || kind == 10) {
            if (kind == 10) id = word(SEL_VEH);
            game_pulldown_add_item(&menu, 6, label(0x958));
            game_pulldown_add_item(&menu, 7, label(0x112c));
            if (word(STATE) & 0x40) game_pulldown_add_item(&menu, 0x5a, label(0xdfc));
            pulldown_add_separator(&menu, MENU_SEPARATOR_ID);
        } else {
            base_id = id;
            struct base *b = &game_bases[id];
            int owner = b->faction;
            if (flags80() || owner == mine || game_spying(owner) || (diplo(mine, owner) & 1)) game_pulldown_add_item(&menu, 5, label(0x894));
            if (owner == mine || flags80()) {
                clear();
                cat(text(S_SPACE));
                cat(label(0x898));
                game_pulldown_add_item(&menu, 0x33, game_battle_text);
                clear();
                cat(text(S_SPACE));
                cat(label(0x89c));
                cat(text(S_SPACE));
                int q = b->queue[0];
                if (q < 0) cat(string(game_facilities[-q].name));
                else cat(game_protos[q].name);
                game_pulldown_add_item(&menu, 0x34, game_battle_text);
                static const int items[4][2] = {{0x35, 0x8a0}, {0x36, 0x8a4}, {0x37, 0x8a8}, {0x38, 0x8ac}};
                for (int i = 0; i < 4; i++) {
                    clear();
                    cat(text(S_SPACE));
                    cat(label(items[i][1]));
                    game_pulldown_add_item(&menu, items[i][0], game_battle_text);
                }
                pulldown_add_separator(&menu, MENU_SEPARATOR_ID);
            }
        }
    }
    game_pulldown_add_item(&menu, MENU_HELP_ID, label(0x8fc));
    win_client_to_screen_2(main_win(), &x, &y);
    game_pulldown_show(&menu, x, y, NULL);
    int selected = pulldown_get_selected(&menu);
    int chosen = selected < 0x40 ? ((int32_t *)&menu)[(0xa20 + selected * 0x14) / 4] : -1;
    int net_a = 0, net_b = 0;
    if (word(NET_ENTRY) != 0) {
        net_a = !(word(CONTROL_TURN) == 0 && word(VEH_FLAG_A) == 0 && word(VEH_FLAG_B) == 0);
        net_b = (1 << mine) & word(FACTION_MASK);
    }
    switch (chosen) {
    case 1:
        sw->sel_veh = -1;
        sw->sel_base = base_id;
        redraw_panels(sw);
        break;
    case 2:
        sw->sel_base = -1;
        sw->sel_veh = id;
        redraw_panels(sw);
        break;
    case 5:
        game_base_win_zoom(game_at(BASE_WIN), base_id, 0);
        break;
    case 6:
        make_active(id);
        break;
    case 7:
        sw->sel_veh = id;
        redraw_panels(sw);
        break;
    case MENU_HELP_ID:
        game_open_help();
        break;
    case 0x33:
        if (net_a == 0 && net_b == 0) {
            game_base_win_soft_zoom(game_at(BASE_WIN), base_id);
            game_base_win_production(game_at(BASE_WIN), 0, 0);
        }
        break;
    case 0x34:
        if (net_a == 0 && net_b == 0) {
            game_base_win_soft_zoom(game_at(BASE_WIN), base_id);
            game_base_win_hurry(game_at(BASE_WIN));
        }
        break;
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
        if (net_a == 0 && net_b == 0) {
            game_base_win_soft_zoom(game_at(BASE_WIN), base_id);
            game_base_win_gov_on(game_at(BASE_WIN), 0x1000000 << (chosen - 0x35));
        }
        break;
    case 0x5a:
        game_editor_edit_veh(game_at(CONSOLE), id);
        break;
    default:
        break;
    }
    if (chosen != 5 && chosen != 6 && (sw->pending_a >= 0 || sw->pending_b >= 0)) {
        sw->pending_b = sw->pending_a = -1;
        statuswin_draw_active(sw, sw->active_veh, sw->active_base, 0);
    }
    pulldown_dtor(&menu);
}

// ---- Setting up and taking down ------------------------------------------------------------------------------

static void UI_THIS statuswin_init(struct game_statuswin *sw) {
    sw->yields = 0;
    sw->double_click = 0;
    sw->x = 0;
    sw->y = 0;
    sw->sel_veh = sw->sel_base = -1;
    sw->pending_a = sw->pending_b = -1;
    sw->active_base = sw->active_veh = -1;
    sw->font_size = -1;
    sw->battle = 0;
    uint32_t *flags = game_at(STATUS_FLAGS);
    if (!(*flags & 0x20)) *flags |= 0x20;
    spot_init(&sw->spot, 0x18);
    game_model_reset(sw->model);
    sw->model[MODEL_BYTE] = 0;
    sw->angles[1] = 0;
    uint32_t bits = 0x3f48f5c3;
    memcpy(&sw->angles[2], &bits, 4);
    sw->angles[0] = 0;
    game_matrix_from_angles(sw->angles, sw->matrix);
    sw->matrix[6] = -sw->matrix[6];
    sw->matrix[7] = -sw->matrix[7];
    sw->matrix[8] = -sw->matrix[8];
    float scale;
    memcpy(&scale, game_at(0x66aa40), 4);
    float eye[3] = {sw->matrix[6] * scale, sw->matrix[7] * scale, sw->matrix[8] * scale};
    game_model_view(sw->model, eye, sw->matrix);
    game_model_scale(sw->model, 0x3e3851ec);
    float *light_a = (float *)(sw->model + MODEL_LIGHT_A), *light_b = (float *)(sw->model + MODEL_LIGHT_B);
    light_a[0] = light_b[0] = 0;
    light_a[1] = light_b[1] = 0;
    light_a[2] = light_b[2] = 1.0f;
}

static void *UI_THIS statuswin_ctor(struct game_statuswin *sw) {
    game_model_ctor(sw->model);
    font_ctor(&sw->font[0]);
    font_ctor(&sw->font[1]);
    font_ctor(&sw->font[2]);
    spot_ctor(&sw->spot);
    return sw;
}

static void UI_THIS statuswin_dtor(struct game_statuswin *sw) {
    spot_dtor(&sw->spot);
    font_dtor(&sw->font[2]);
    font_dtor(&sw->font[1]);
    font_dtor(&sw->font[0]);
    game_model_close(sw->model);
}

static void UI_THIS statuswin_stub_close(struct game_statuswin *sw) { (void)sw; }

// ---- The hook table ------------------------------------------------------------------------------------------

static const struct hook hooks[] = {
    UI_HOOK(0x4b4990, statuswin_resize_fonts, 0x56, 0x8b, 0x35, 0x1c, 0x7b, 0x9b, 0x00),
    UI_HOOK(0x4b4a20, statuswin_set_title, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x4b4cd0, statuswin_hide_title, 0xc7, 0x05, 0xc4, 0x74, 0x68, 0x00, 0xff, 0xff, 0xff, 0xff),
    UI_HOOK(0x4b4cf0, statuswin_draw_active, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x7c),
    UI_HOOK(0x4b6530, statuswin_reset_active, 0x8b, 0x81, 0xc4, 0x15, 0x00, 0x00, 0x85, 0xc0),
    UI_HOOK(0x4b6570, statuswin_draw_status, 0x55, 0x8b, 0xec, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00),
    UI_HOOK(0x4b8830, statuswin_draw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x45, 0x18),
    UI_HOOK(0x4b8890, statuswin_on_redraw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x56),
    UI_HOOK(0x4b8970, statuswin_reset, 0x8b, 0x81, 0xd4, 0x15, 0x00, 0x00, 0x85, 0xc0),
    UI_HOOK(0x4b8990, statuswin_on_left_double_click, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x56),
    UI_HOOK(0x4b89c0, statuswin_arrow_left, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x56),
    UI_HOOK(0x4b8bd0, statuswin_arrow_right, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x56),
    UI_HOOK(0x4b8df0, statuswin_activate, 0x56, 0x8b, 0xb1, 0xbc, 0x15, 0x00, 0x00),
    UI_HOOK(0x4b8e40, statuswin_on_left_click, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x56),
    UI_HOOK(0x4b9010, statuswin_on_right_click, 0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    UI_HOOK(0x4b9e90, statuswin_flag, 0xa1, 0xd8, 0x9e, 0x93, 0x00, 0x0c, 0x20),
    UI_HOOK(0x4b9ea0, statuswin_redraw, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x56),
    UI_HOOK(0x4b9f80, statuswin_stub_close, 0xc3, 0x90, 0x90, 0x90, 0x90),
    UI_HOOK(0x4b9f90, statuswin_set_loc, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x8b, 0x55, 0x0c),
    UI_HOOK(0x4b9fc0, statuswin_init, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x14, 0x53),
    UI_HOOK(0x4ba120, statuswin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x55, 0x9a, 0x65, 0x00),
    UI_HOOK(0x4ba1a0, statuswin_ctor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x95, 0x9a, 0x65, 0x00),
};

struct hook_module statuswin_module = {"statuswin", hooks, UI_HOOK_COUNT(hooks), NULL, NULL, -1};
