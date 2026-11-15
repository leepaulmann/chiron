// The Social Engineering window, the game's SocialWin: the F-key (and the menu's) social engineering screen, opened by social_select
// (0x5b5620) through the class's init, which does not return until the window is closed. It is one global object (0x8a6270) built
// by a static initializer and destroyed by an atexit handler. Outside the class only social_select (init and close), the checks
// "is the window up" the main screen's key and mouse handlers make (Win's own is_visible), and the tutorial's tour touch it.
//
// What the window shows: four category panels (Politics, Economics, Values, Future Society), each with the four model buttons of the
// category (the ones the faction can't choose yet are drawn as empty frames) and under each button a box of icons for the effects it
// brings (a big picture for 2 or more, one picture for each point beyond, a small icon for each effect; a penalty that the Network
// Backbone or the Cloning Vats cancels isn't drawn); on the right the effects' buttons (Economy, Efficiency, Support, Morale, Police,
// Growth, Planet, Probe, Industry, Research: a button for each effect but Talent, which has none), each with its total now (the
// models chosen, with the faction's own effects added); under them the faction's own effects (its base values); and three buttons,
// Reset, OK and Cancel. A second part of the window is drawn on the main screen's own panels (it is "the interface": a sub
// interface at 0xa14 in the object that makes the main screen send its clicks and moves here): the energy reserves and what the
// upheaval costs; a list of what each effect does, with the effect's total, the one the mouse is on lit; the three allocation bars
// (Economy, Labs, Psych, in tenths of the energy), each with + and - buttons and a lock; and a box that tells what the model under the
// mouse does. Clicking an effect's button "maximizes" it: each category takes the available model whose effect is the highest.
// Changing a model, or an allocation, works the effects out again (the faction's pending effects are written while the totals are worked
// out and put back from its pending models at the end).
//
// What the window writes: the faction's pending models (OK, once the upheaval is paid: energy credits, the paid total, and the network
// messages the game sends), its allocation (psych and labs) as the + and - buttons set it, and the pending effects, the energy
// numbers and the faction's own copies as it goes; Cancel puts the allocation back (Reset too) and the pending models stand.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions are here and
// CHIRON_SKIP=socialwin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. The social logic (the effects, the cost of an upheaval, which models
// are available, the AI's choice, putting a choice into effect) is src/engine/social.c's and events.c's and is called directly.
// Static initializers run after hooks_apply, so the constructor and destructor are ours too.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the call of 0x5fd2b0 after each drawing
// (a function that does nothing) is left out; the mouse-over box, drawn for the faction's own box, asks the penalty check about a
// category and a model that the original never sets for it (it reads whatever its frame held: stack leftovers), where this asks nothing;
// the immune check the drawing repeats by hand is the one function here (the original has the same code written out in each place).
// Branches the scenes cannot reach: the destructor, the scalar destructor and the Buffer's deleting destructor (they run when the game
// quits, after the scene is over), the editor's two flags in exit (a scenario with both set pays nothing), the buttons' states other than
// the three the effects' buttons show (the text draw's default look), and the tutorial's own tour, which drives the window through
// init with a negative faction, show, hide and close (the scenes make those calls by hand).
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "socialwin.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../engine/base.h"
#include "../engine/buffer.h"
#include "../engine/bufobj.h"
#include "../engine/bufimg.h"
#include "../engine/button.h"
#include "../engine/events.h"
#include "../engine/font.h"
#include "../engine/game.h"
#include "../engine/menu.h"
#include "../engine/social.h"
#include "../engine/sprite.h"
#include "../engine/text.h"
#include "../engine/win.h"

// ---- Pure parts ---------------------------------------------------------------------------------------------

static struct ui_rect rect4(int l, int t, int r, int b) { return (struct ui_rect){l, t, r, b}; }

void socialwin_layout(struct ui_rect r[SW_RECTS], const int32_t p[16]) {
    const int32_t *A = p, *B = p + 4, *C = p + 8, *D = p + 12;
    r[SW_R_INFO] = rect4(C[0] + 4, C[1] + 4, C[2] - 4, C[3] - 4);
    r[SW_R_PRESETS] = rect4(0x271, 4, 0x306, 0xfa);
    for (int i = 0; i < 10; i++) r[SW_R_EFFECT + i] = rect4(0x274, 0x1d + 0x16 * i, 0x303, 0x1d + 0x16 * i + 0x14);
    r[SW_R_OK] = rect4(0x9a, 0x13a, 0x13c, 0x14e);
    r[SW_R_RESET] = rect4(0x13f, 0x13a, 0x1e1, 0x14e);
    r[SW_R_CANCEL] = rect4(0x1e4, 0x13a, 0x286, 0x14e);
    static const int panel_y[4][2] = {{4, 0x4a}, {0x4d, 0x93}, {0x96, 0xdc}, {0xdf, 0x125}};
    static const int model_y[4][2] = {{0x1d, 0x31}, {0x66, 0x7a}, {0xaf, 0xc3}, {0xf8, 0x10c}};
    static const int model_x[4][2] = {{0x1d, 0xaf}, {0xb1, 0x143}, {0x145, 0x1d7}, {0x1d9, 0x26b}};
    for (int c = 0; c < 4; c++) {
        r[SW_R_PANEL + c] = rect4(0x1a, panel_y[c][0], 0x26e, panel_y[c][1]);
        for (int m = 0; m < 4; m++) r[SW_R_MODEL + c * 4 + m] = rect4(model_x[m][0], model_y[c][0], model_x[m][1], model_y[c][1]);
    }
    r[SW_R_BASAL] = rect4(0x271, 0xfd, 0x306, 0x12d);

    // The three bars share the width of the last panel and the first's left: a third each, less the 3 between.
    int x0 = A[0] + 4 + 3, y0 = D[1] + 4 + 3, y1 = D[3] - 4 - 3;
    int third = ((D[2] - 4 - 3) - x0 + 3) / 3;
    int x1 = third + x0 - 3;
    r[SW_R_ECONOMY] = rect4(x0, y0, x1, y1);
    int w = x1 - x0 + 3;
    r[SW_R_PSYCH] = rect4(x0 + w, y0, x1 + w, y1);
    r[SW_R_LABS] = rect4(r[SW_R_PSYCH].right + 3, y0, D[2] - 7, y1);
    r[SW_R_HOVER] = rect4(A[0] + 4, A[1] + 4, A[2] - 4, r[SW_R_LABS].top - 6);
    r[SW_R_OVERVIEW] = rect4(B[0] + 4, B[1] + 4, B[2] - 4, B[3] - 4);

    // The + and - buttons at the top corners of each bar (economy, labs, psych: the ids' order), and the lock at the bottom left of
    // each (economy, psych, labs: the order of the allocation).
    static const int bar_of_buttons[3] = {SW_R_ECONOMY, SW_R_LABS, SW_R_PSYCH};
    static const int bar_of_lock[3] = {SW_R_ECONOMY, SW_R_PSYCH, SW_R_LABS};
    for (int k = 0; k < 3; k++) {
        const struct ui_rect *bar = &r[bar_of_buttons[k]];
        r[SW_R_ALLOC_BTN + 2 * k] = rect4(bar->left + 3, bar->top + 3, bar->left + 3 + 0x14, bar->top + 3 + 0x14);
        r[SW_R_ALLOC_BTN + 2 * k + 1] = rect4(bar->right - 3 - 0x14, bar->top + 3, bar->right - 3, bar->top + 3 + 0x14);
        bar = &r[bar_of_lock[k]];
        r[SW_R_LOCK + k] = rect4(bar->left + 3, bar->bottom - 3 - 0xe, bar->left + 3 + 0x18, bar->bottom - 3);
    }
}

int socialwin_effect_button(int effect) { return effect < 3 ? effect : effect == 3 ? -1 : effect - 1; }
int socialwin_button_effect(int slot) { return slot < 3 ? slot : slot + 1; }

void socialwin_allocate(int32_t alloc[3], const int32_t locked[3], int index, int value, int limit) {
    alloc[index] = value;
    while (alloc[0] + alloc[1] + alloc[2] < 10) {
        int k = index;
        for (int n = 0, c = index + 1; n < 3; n++, c++) {
            k = c % 3;
            if (k == index || (!locked[k] && alloc[k] < limit)) break;
        }
        alloc[k]++;
    }
    while (alloc[0] + alloc[1] + alloc[2] > 10) {
        int k = index;
        for (int n = 0, c = index + 1; n < 3; n++, c++) {
            k = c % 3;
            if (k == index || (!locked[k] && alloc[k] != 0)) break;
        }
        alloc[k]--;
    }
}

int socialwin_best_model(const int32_t effects[4], const int avail[4]) {
    int best = 0, value = 0;
    for (int m = 0; m < 4; m++)
        if (avail[m] && effects[m] > value) best = m, value = effects[m];
    return best;
}

void socialwin_signed(char *out, int value) { sprintf(out, value > 0 ? "+%d" : "%d", value); }
void socialwin_signed_abs(char *out, int value) { sprintf(out, "%c%d", value < 0 ? '-' : '+', value < 0 ? -value : value); }

int socialwin_clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
int socialwin_loss_half(int psych, int labs) {
    int s = psych + labs * 2 - 10;
    return (s < 0 ? -s : s) / 2;
}
int socialwin_loss_unit(int efficiency) { return 2 * (4 - socialwin_clamp(efficiency, -4, 4)); }

void socialwin_info_lines(struct socialwin_info_lines *l, int screen_w, int left, int right, int top, int bottom, int line) {
    if (screen_w == 0x320) {
        l->y1 = top + line;
        l->y3 = l->y1 + 2 * line;
        l->x3 = left;
    } else {
        l->y1 = top + (bottom - 2 * line - top) / 2;
        l->y3 = l->y1;
        l->x3 = left + (right - left) / 2;
    }
}

void socialwin_icon_counts(const int32_t values[11], const int hidden[11], int *drawn, int *big) {
    *drawn = *big = 0;
    for (int e = 0; e < 11; e++) {
        if (e == 3 || hidden[e]) continue;
        int v = values[e] < 0 ? -values[e] : values[e];
        *drawn += v != 0;
        *big += v > 1;
    }
}

int socialwin_icons_x(int left, int right, int drawn, int icon_w, int big, int big_w) {
    int n = drawn < 1 ? 1 : drawn;
    return left + (right - (icon_w + 1) * n - big_w * big - left) / 2;
}

int socialwin_big_index(int value) {
    int a = value < 0 ? -value : value;
    return 2 * (a - 2) + (value <= 0);
}
int socialwin_small_index(int effect, int value) { return socialwin_effect_button(effect) * 9 + 6 + (value >= 1 ? 2 : 1); }
int socialwin_hover_effect(int id) {
    int e = id - 0x11;
    return e >= 3 ? e + 1 : e;
}

// ---- The object -------------------------------------------------------------------------------------------

enum {
    SOCIALWIN_BASE = 0x8a6270,
    MAIN_INTERFACE = 0x7ae820,  // the main screen's window, whose buffer most of the drawing is done in
    MAIN_BUFFER = 0x7aec64,     // its Buffer (0x444 in)
};
enum { CATEGORIES = 4, MODELS = 4, EFFECTS = 11, BUTTONS = 35, EFFECT_BUTTONS = 10, FIRST_EFFECT_BUTTON = 17, ICON_ROW = 9 };

// The buttons' ids. The model buttons are 0 to 15 (category * 4 + model), then come the effects' buttons and the three along the bottom.
enum {
    ID_EFFECT_FIRST = 0x11,
    ID_CANCEL = 0x10,
    ID_RESET = 0x1b,
    ID_OK = 0x1c,
    ID_ALLOC_FIRST = 0x1d,  // + and - of economy, labs, psych (0x1d to 0x22)
};

struct game_socialwin {
    struct game_win win;                 // 0x0: a GraphicWin, the Win then its Buffer
    struct game_buffer buf;              // 0x444
    GAME_PAD(0x9cc, 0xa14);
    void **iface_vtable;                 // 0xa14: the sub interface (the main screen's clicks and moves come to its functions)
    int32_t iface_mode;                  // 0xa18: 2
    int32_t iface_c;                     // 0xa1c: -1 when a model is chosen
    struct game_spot spot;               // 0xa20
    int32_t hover;                       // 0xa2c: the model button the mouse is over, -2 the faction's own box, -1 none
    int32_t hover_effect;                // 0xa30: the effect's button (0x11 to 0x1b) the mouse is over, or -1
    GAME_PAD(0xa34, 0xcf4);
    int32_t faction;                     // 0xcf4
    int32_t models[CATEGORIES];          // 0xcf8: the models chosen
    int32_t effect[EFFECTS];             // 0xd08: what they bring
    int32_t effect_prev[EFFECTS];        // 0xd34: what the faction's pending effects were, as the overview last drew them
    int32_t energy_a, energy_b;          // 0xd60, 0xd64: energy_compute's answers (0x749c0c, 0x749ae4) for the chosen models
    GAME_PAD(0xd68, 0xd6c);
    struct game_check_button lock[3];    // 0xd6c: the locks under the allocation bars (economy, psych, labs)
    GAME_PAD(0x2bf0, 0x2c24);
    int32_t maximize;                    // 0x2c24: the effect being maximized, or -1
    int32_t resetting;                   // 0x2c28: set while reset works
    GAME_PAD(0x2c2c, 0x2c38);
    int32_t alloc_start[3];              // 0x2c38: the allocation the window opened with (economy, psych, labs)
    int32_t alloc_shown[3];              // 0x2c44
    int32_t alloc[3];                    // 0x2c50: and as it is now
    int32_t capped;                      // 0x2c5c: set when a + went over the limit
    struct game_sprite big[10];          // 0x2c60: the big pictures for 2 to 6, plus then minus
    struct game_sprite icon[90];         // 0x2e18: ten rows of nine: the effects' buttons' three looks (0, 3, 6), the icons (7, 8)
    struct game_sprite lock_pic[2];      // 0x3d90
    struct game_sprite arrow[6];         // 0x3de8: the + and - buttons' three looks each
    struct game_buffer tile[6];          // 0x3ef0: the bars' two kinds of tile, for each of the three
    struct game_flat_button button[BUTTONS];  // 0x6020
    struct game_button_group group[CATEGORIES];  // 0x1eb84: one a category
    struct game_button_group group_effect;       // 0x1edd4: the effects' buttons
    struct font font[9];                 // 0x1ee68
    struct ui_rect rect[SW_RECTS];       // 0x1efd0
};
GAME_AT(struct game_socialwin, iface_vtable, 0xa14);
GAME_AT(struct game_socialwin, iface_mode, 0xa18);
GAME_AT(struct game_socialwin, spot, 0xa20);
GAME_AT(struct game_socialwin, hover, 0xa2c);
GAME_AT(struct game_socialwin, hover_effect, 0xa30);
GAME_AT(struct game_socialwin, faction, 0xcf4);
GAME_AT(struct game_socialwin, models, 0xcf8);
GAME_AT(struct game_socialwin, effect, 0xd08);
GAME_AT(struct game_socialwin, effect_prev, 0xd34);
GAME_AT(struct game_socialwin, energy_a, 0xd60);
GAME_AT(struct game_socialwin, lock, 0xd6c);
GAME_AT(struct game_socialwin, maximize, 0x2c24);
GAME_AT(struct game_socialwin, resetting, 0x2c28);
GAME_AT(struct game_socialwin, alloc_start, 0x2c38);
GAME_AT(struct game_socialwin, alloc_shown, 0x2c44);
GAME_AT(struct game_socialwin, alloc, 0x2c50);
GAME_AT(struct game_socialwin, capped, 0x2c5c);
GAME_AT(struct game_socialwin, big, 0x2c60);
GAME_AT(struct game_socialwin, icon, 0x2e18);
GAME_AT(struct game_socialwin, lock_pic, 0x3d90);
GAME_AT(struct game_socialwin, arrow, 0x3de8);
GAME_AT(struct game_socialwin, tile, 0x3ef0);
GAME_AT(struct game_socialwin, button, 0x6020);
GAME_AT(struct game_socialwin, group, 0x1eb84);
GAME_AT(struct game_socialwin, group_effect, 0x1edd4);
GAME_AT(struct game_socialwin, font, 0x1ee68);
GAME_AT(struct game_socialwin, rect, 0x1efd0);
_Static_assert(sizeof(struct game_check_button) == 0xa2c, "layout of the game's CheckButton");
_Static_assert(sizeof(struct game_socialwin) == 0x1f2f0, "layout of the game's SocialWin");

// The slots of the window's vtable the screen uses (byte offsets).
enum { VT_SHOW = 0x4, VT_HIDE = 0x8, VT_SET_MODAL = 0xe4, VT_RELEASE_MODAL = 0xe8, VT_REDRAW = 0xf8, VT_BUTTON_CLOSE = 0x168 };
// The vtables the constructor sets: the window's, the Buffer's and the sub interface's (and the base the sub interface starts with).
enum { VTABLE_WIN = 0x66daa4, VTABLE_BUF = 0x66da9c, VTABLE_IFACE = 0x66da50, VTABLE_IFACE_BASE = 0x66a6e4 };

static struct game_buffer *mbuf(void) { return game_at(MAIN_BUFFER); }
static void *main_interface(void) { return game_at(MAIN_INTERFACE); }
static void *iface_of(struct game_socialwin *sw) { return &sw->iface_vtable; }
static struct game_socialwin *from_iface(void *iface) { return (struct game_socialwin *)((uint8_t *)iface - offsetof(struct game_socialwin, iface_vtable)); }

// ---- The game's data it reads (by address) -----------------------------------------------------------------

enum {
    FRAME_HEADER = 0x78d5a0,  // box frames: the panels', the titles' and the frame inside a panel
    FRAME_INNER = 0x78d618,
    FRAME_BODY = 0x78d528,
    FRAME_WINDOW = 0x78d690,
    RULES_FILE = 0x691afc,    // char *: the rules file's name
    POPUP_FILE = 0x691b0c,    // char *: the file the pop-ups' texts are in
    FONT_FACE = 0x691b2c,     // the two typefaces (char *)
    FONT_FACE_2 = 0x691b30,
    SOCIAL_NAMES = 0x94b000,  // the category's name's string id, 0xd4 a category; the models' string ids from 0x14
    SOCIAL_MODEL_NAMES = 0x94b014,
    EFFECT_KEYS = 0x946580,   // the effects' names: the key of its text (0x68 an effect), and the name at 0x18 in
    PANELS = 0x7af5a0,        // the main screen's four panels (left, top, right, bottom each): where the window's parts go
    PANEL_TOP = 0x7af59c,     // how far down the screen the main screen's panels start
    GLOBAL_ENERGY_A = 0x749c0c,
    GLOBAL_ENERGY_B = 0x749ae4,
    BUFFER_FIELD = 0x7aec74,  // a word the overview sets in the main screen's Buffer
    CURRENT_IFACE = 0x7ae7f8, // the sub interface that is up
    CONSOLE = 0x9156b0,       // the main screen's console
    TUT_WIN = 0x8c6e68,       // the tutorial's window (and the word and the rectangle its note about this window uses)
    TUT_NOTE_WORD = 0x8cc228,
    TUT_NOTE_RECT = 0x8c5380,
};
// The pictures sheet and the strings it uses.
enum {
    SHEET = 0x6874b0,
    SEPARATOR = 0x682e94,  // ": "
    FLYING_GAP = 0x682820, // " "
    COMMA = 0x682e90,
    PERCENT = 0x682ea0,
    PERCENT_SPACE_A = 0x687480,  // "% " four times over
    PERCENT_SPACE_B = 0x687484,
    PERCENT_SPACE_C = 0x687488,
    PERCENT_SPACE_D = 0x68748c,
    PLUS_A = 0x687490,
    SECTION = 0x687494,    // "SOCIO"
    SECTION_PREFIX = 0x68749c,  // "SOC"
    PLUS_B = 0x6874a0,
    MINUS = 0x6874a4,
    PLUS_C = 0x6874a8,
    PLUS_D = 0x6874ac,
    TUTORIAL_NOTE = 0x6874bc,
    TOO_MUCH_PICTURE = 0x687428,
    TOO_MUCH_LABEL = 0x68743c,
    ALIEN_LABEL = 0x687458,
    ALIEN_PICTURE = 0x68744c,
    PLAIN_LABEL = 0x687474,
    PLAIN_PICTURE = 0x687464,
};

static char *text(uint32_t addr) { return game_at(addr); }
static const struct game_box_frame *frame_at(uint32_t addr) { return game_at(addr); }
// An entry of the labels file by the offset the original reads it at (4 a label).
static char *lab(int offset) { return game_label(offset >> 2); }
static struct faction_data *fac(const struct game_socialwin *sw) { return &game_factions[sw->faction]; }
static char *effect_name(int e) { return game_at(EFFECT_KEYS + 0x68 * e + 0x18); }
static char *effect_key(int e) { return game_at(EFFECT_KEYS + 0x68 * e); }
static int32_t model_effect(int cat, int model, int e) {
    return ((const int32_t *)(game_social_effects + cat * SOCIAL_CATEGORY_SIZE + model * SOCIAL_MODEL_SIZE))[e];
}
static char *strings_at(int32_t id) { return game_strings_get(game_strings, (char *)(uintptr_t)id); }
static int32_t model_name_id(int cat, int model) { return *(int32_t *)game_at(SOCIAL_MODEL_NAMES + (cat * (SOCIAL_CATEGORY_SIZE / 4) + model) * 4); }

static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void show(void *w) { UI_VCALL(void(UI_THIS *)(void *, int), w, VT_SHOW, 0); }
static void redraw(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_REDRAW); }
static void release_modal(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_RELEASE_MODAL); }

// Whether the faction holds a secret project (its base is the faction's own).
static int holds(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}

// Whether a model's penalty (a negative effect) is cancelled for the faction: the Cloning Vats cancel those of Power and Thought
// Control, the Network Backbone those of Cybernetic. The game's own check (SocialWin_immune); the drawing repeats it by hand.
static int immune(const struct game_socialwin *sw, int cat, int model, int e) {
    if (holds(SP_CLONING_VATS, sw->faction) && ((cat == 2 && model == 1) || (cat == 3 && model == 3)) && model_effect(cat, model, e) < 0) return 1;
    if (holds(SP_NETWORK_BACKBONE, sw->faction) && cat == 3 && model == 1 && model_effect(3, 1, e) < 0) return 1;
    return 0;
}
static int UI_THIS socialwin_immune(struct game_socialwin *sw, int cat, int model, int e) { return immune(sw, cat, model, e); }

// ---- The window's own pictures and buttons, closed ---------------------------------------------------------

// What close does and open starts with: the buttons let go (their virtual close), the groups closed, and the window's markers put back.
static void close_parts(struct game_socialwin *sw) {
    for (int i = 0; i < BUTTONS; i++) UI_VCALL(void(UI_THIS *)(void *), &sw->button[i], VT_BUTTON_CLOSE);
    for (int c = 0; c < CATEGORIES; c++) button_group_close(&sw->group[c]);
    button_group_close(&sw->group_effect);
    sw->hover = -1;
    sw->hover_effect = -1;
    sw->resetting = 0;
    sw->maximize = -1;
}

static void UI_THIS socialwin_close(struct game_socialwin *sw) { close_parts(sw); }

// ---- Construction ------------------------------------------------------------------------------------------

static void *UI_THIS socialwin_ctor(struct game_socialwin *sw) {
    graphic_win_ctor(&sw->win);
    sw->iface_vtable = (void **)(uintptr_t)game_va(VTABLE_IFACE_BASE);
    game_spot_ctor(&sw->spot);
    for (int i = 0; i < 3; i++) check_button_ctor(&sw->lock[i]);
    for (int i = 0; i < 10; i++) sprite_ctor(&sw->big[i]);
    for (int i = 0; i < 90; i++) sprite_ctor(&sw->icon[i]);
    for (int i = 0; i < 2; i++) sprite_ctor(&sw->lock_pic[i]);
    for (int i = 0; i < 6; i++) sprite_ctor(&sw->arrow[i]);
    for (int i = 0; i < 6; i++) buffer_ctor(&sw->tile[i]);
    for (int i = 0; i < BUTTONS; i++) flat_button_ctor(&sw->button[i]);
    for (int c = 0; c < CATEGORIES; c++) button_group_ctor(&sw->group[c]);
    button_group_ctor(&sw->group_effect);
    for (int i = 0; i < 9; i++) font_ctor(&sw->font[i]);
    sw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    sw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    sw->iface_vtable = (void **)(uintptr_t)game_va(VTABLE_IFACE);
    sw->faction = -1;
    return sw;
}

static void UI_THIS socialwin_dtor(struct game_socialwin *sw) {
    sw->win.vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    sw->buf.vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    sw->iface_vtable = (void **)(uintptr_t)game_va(VTABLE_IFACE);
    close_parts(sw);
    for (int i = 8; i >= 0; i--) font_dtor(&sw->font[i]);
    button_group_close(&sw->group_effect);
    for (int c = CATEGORIES - 1; c >= 0; c--) button_group_close(&sw->group[c]);
    for (int i = BUTTONS - 1; i >= 0; i--) flat_button_dtor(&sw->button[i]);
    for (int i = 5; i >= 0; i--) buffer_dtor(&sw->tile[i]);
    for (int i = 5; i >= 0; i--) sprite_close(&sw->arrow[i]);
    for (int i = 1; i >= 0; i--) sprite_close(&sw->lock_pic[i]);
    for (int i = 89; i >= 0; i--) sprite_close(&sw->icon[i]);
    for (int i = 9; i >= 0; i--) sprite_close(&sw->big[i]);
    for (int i = 2; i >= 0; i--) check_button_dtor(&sw->lock[i]);
    game_spot_dtor(&sw->spot);
    graphic_win_dtor(&sw->win);
}

static void *UI_THIS socialwin_scalar_dtor(struct game_socialwin *sw, int flags) {
    socialwin_dtor(sw);
    if (flags & 1) game_free(sw);
    return sw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS socialwin_operator_delete(struct game_buffer *buf, int flags) {
    return socialwin_scalar_dtor((struct game_socialwin *)((uint8_t *)buf - offsetof(struct game_socialwin, buf)), flags);
}

// ---- Choosing ----------------------------------------------------------------------------------------------

static void draw_social(struct game_socialwin *sw, int cat);

// A model is chosen in a category (UNK1): the category drawn again, then the effects (kept as they were, then worked out for the
// models chosen, in the window and in the faction's pending effects) and the energy numbers they make. The faction's pending effects
// are put back from its pending models at the end.
static void UI_THIS socialwin_set_model(struct game_socialwin *sw, int cat, int model) {
    struct faction_data *f = fac(sw);
    sw->iface_c = -1;
    sw->models[cat] = model;
    draw_social(sw, cat);
    memcpy(sw->effect_prev, sw->effect, sizeof sw->effect);
    social_calc(sw->models, sw->effect, sw->faction, 0, 0);
    social_calc(sw->models, f->se_pending, sw->faction, 0, 0);
    game_energy_compute(sw->faction, 1);
    sw->energy_b = *(int32_t *)game_at(GLOBAL_ENERGY_B);
    sw->energy_a = *(int32_t *)game_at(GLOBAL_ENERGY_A);
    social_calc(f->se_models_pending, f->se_pending, sw->faction, 0, 0);
}

// The window put back as it opened: the AI's choice made for an AI faction (nothing for a human one), the models the faction's own
// pending ones, the allocation the one it opened with (the faction's pending effects worked out again when it changed), and the
// window drawn again if it is up.
static void UI_THIS socialwin_reset(struct game_socialwin *sw) {
    struct faction_data *f = fac(sw);
    sw->iface_c = -1;
    social_ai(sw->faction, -1, -1, -1, -1, NULL);
    sw->resetting = 1;
    for (int c = 0; c < CATEGORIES; c++) {
        sw->models[c] = f->se_models_pending[c];
        button_group_set(&sw->group[c], sw->models[c] + c * 4, 1);
    }
    for (int e = 0; e < EFFECTS; e++) sw->effect_prev[e] = sw->effect[e] = f->se_pending[e];
    sw->resetting = 0;
    int changed = !(f->alloc_psych == sw->alloc_start[1] && f->alloc_labs == sw->alloc_start[2]);
    f->alloc_psych = sw->alloc_start[1];
    f->alloc_labs = sw->alloc_start[2];
    for (int k = 0; k < 3; k++) sw->alloc[k] = sw->alloc_start[k];
    if (changed) {
        social_set(sw->faction);
        social_calc(sw->models, sw->effect, sw->faction, 0, 0);
        social_calc(sw->models, f->se_pending, sw->faction, 0, 0);
    }
    game_energy_compute(sw->faction, 1);
    sw->energy_b = *(int32_t *)game_at(GLOBAL_ENERGY_B);
    sw->energy_a = *(int32_t *)game_at(GLOBAL_ENERGY_A);
    social_calc(f->se_models_pending, f->se_pending, sw->faction, 0, 0);
    if (win_is_visible(&sw->win)) redraw(sw);
}

// OK: the upheaval paid for (asked first when it costs something and the faction can afford it), the models made the faction's pending
// ones, and the modal window ended. A cost the faction can't pay shows a pop-up and leaves the window up. (A scenario with the editor's
// two flags set pays nothing.)
static void UI_THIS socialwin_exit(struct game_socialwin *sw) {
    struct faction_data *f = fac(sw);
    int cost = social_upheaval(sw->faction, sw->models) - f->se_upheaval_paid;
    if ((*game_state & 0x40) && (*game_state & 0x80)) {
        cost = 0;
    } else if (cost > 0) {
        game_parse_num(0, cost);
        game_parse_num(1, f->energy_credits);
        char *file = *(char **)game_at(POPUP_FILE);
        int wait = (int)(uintptr_t)game_pop_wait;
        if (cost > f->energy_credits) {
            game_popp(file, text(TOO_MUCH_LABEL), 0, text(TOO_MUCH_PICTURE), wait);
            return;
        }
        int answer;
        if (game_faction_defs[sw->faction].rule_flags & RULE_FLAG_ALIEN) answer = game_popp(file, text(ALIEN_LABEL), 0, text(ALIEN_PICTURE), wait);
        else answer = game_popp(file, text(PLAIN_LABEL), 0, text(PLAIN_PICTURE), wait);
        if (!answer) return;
    }
    if (cost >= f->energy_credits) cost = f->energy_credits;
    f->se_upheaval_paid += cost;
    for (int c = 0; c < CATEGORIES; c++) f->se_models_pending[c] = sw->models[c];
    game_synch_alloc(sw->faction);
    game_synch_soc(sw->faction);
    game_net_energy(sw->faction, -cost, 0, 0, 0);
    social_calc(f->se_models_pending, f->se_pending, sw->faction, 0, 0);
    release_modal(sw);
}

// An effect's button was clicked: each category takes the model (of those the faction may choose) that brings the most of the
// effect, the first of equals, or the first model if none brings any; the window is drawn again.
static void UI_THIS socialwin_maximize_effect(struct game_socialwin *sw, int effect) {
    sw->maximize = effect;
    for (int cat = 0; cat < CATEGORIES; cat++) {
        int32_t v[MODELS];
        int avail[MODELS];
        for (int m = 0; m < MODELS; m++) {
            avail[m] = society_avail(cat, m, sw->faction);
            v[m] = model_effect(cat, m, effect);
        }
        int best = socialwin_best_model(v, avail);
        button_group_set(&sw->group[cat], best + cat * 4, 1);
        socialwin_set_model(sw, cat, best);
    }
    redraw(sw);
    sw->maximize = -1;
}

// A share of the energy was set: the others give and take up what is left (see socialwin_allocate).
static void UI_THIS socialwin_allocate_energy(struct game_socialwin *sw, int index, int value) {
    int32_t locked[3] = {sw->lock[0].state, sw->lock[1].state, sw->lock[2].state};
    socialwin_allocate(sw->alloc, locked, index, value, energy_limit(sw->faction));
}

// ---- Drawing: the window's own panels -----------------------------------------------------------------------

// Text is put together in the game's message buffer, as the original does.
static void put(const char *s) { strcat(game_str_buffer, s); }
static void put_int(int v) {
    char t[16];
    sprintf(t, "%d", v);
    strcat(game_str_buffer, t);
}
static void clear_text(void) { game_str_buffer[0] = 0; }
static void upper_text(void) { (*game_char_upper)(game_str_buffer); }
static int line_height(const struct font *f) { return f->first < 0 ? f->ascent : f->height + f->first; }
static void text_color(struct game_buffer *b, int c) { buffer_set_text_color(b, c, -1, 1, 1); }
static void font_of(struct game_buffer *b, struct font *f) { buffer_set_font(b, f, NULL, NULL, NULL); }
static void write_centered(struct game_buffer *b, const struct ui_rect *r) {
    if (game_str_buffer) buffer_write_cent_l_7(b, game_str_buffer, &r->left, strlen(game_str_buffer));
}
static void update_main(const struct ui_rect *r) { graphic_win_soft_update_3(main_interface(), &r->left); }
static struct ui_rect shrunk(struct ui_rect r, int d) { return rect4(r.left + d, r.top + d, r.right - d, r.bottom - d); }

// The frame on the right with its title (the label at 0x114).
static void UI_THIS socialwin_draw_presets(struct game_socialwin *sw) {
    struct game_buffer *b = &sw->buf;
    const struct ui_rect *src = &sw->rect[SW_R_PRESETS];
    struct ui_rect r = *src;
    buffer_set_clip(b, &r.left);
    buffer_box_sprite(b, &r.left, frame_at(FRAME_HEADER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(b, &title.left, frame_at(FRAME_INNER));
    clear_text();
    put(lab(0x114));
    upper_text();
    font_of(b, &sw->font[0]);
    text_color(b, 0x9f);
    write_centered(b, &title);
    graphic_win_soft_update_3(&sw->win, &src->left);
}

// One category's panel: its name, and for each model its button's frame and, under it, the box of icons for what it brings. A
// model the faction can't choose yet has two empty frames in the place of the button and the box.
static void draw_social(struct game_socialwin *sw, int cat) {
    struct game_buffer *b = &sw->buf;
    const struct ui_rect *panel = &sw->rect[SW_R_PANEL + cat];
    struct ui_rect r = *panel;
    buffer_set_clip(b, &r.left);
    buffer_box_sprite(b, &r.left, frame_at(FRAME_HEADER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(b, &title.left, frame_at(FRAME_INNER));
    clear_text();
    put(strings_at(*(int32_t *)game_at(SOCIAL_NAMES + cat * SOCIAL_CATEGORY_SIZE)));
    text_color(b, 0x9f);
    font_of(b, &sw->font[0]);
    upper_text();
    write_centered(b, &title);

    int icon_w = sw->icon[0].full_w;
    int big_w = sw->big[0].width;
    for (int model = 0; model < MODELS; model++) {
        const struct ui_rect *m = &sw->rect[SW_R_MODEL + cat * 4 + model];
        if (!society_avail(cat, model, sw->faction)) {
            struct ui_rect t = *m;
            buffer_box_sprite(b, &t.left, frame_at(FRAME_INNER));
            t.top += 0x16;
            t.bottom += 0x16;
            buffer_box_sprite(b, &t.left, frame_at(FRAME_INNER));
            continue;
        }
        struct ui_rect box = rect4(m->left, m->top + 0x16, m->right, m->bottom + 0x16);
        buffer_box_sprite(b, &box.left, frame_at(FRAME_INNER));

        // How many effects are drawn, and how many with a big picture; the icons go in the middle of the box.
        int32_t values[EFFECTS];
        int hidden[EFFECTS];
        for (int e = 0; e < EFFECTS; e++) {
            values[e] = model_effect(cat, model, e);
            hidden[e] = immune(sw, cat, model, e);
        }
        int drawn, big;
        socialwin_icon_counts(values, hidden, &drawn, &big);
        int x = socialwin_icons_x(box.left, box.right, drawn, icon_w, big, big_w);
        int y = box.top + (box.bottom - icon_w - box.top) / 2;
        // Three passes: the effects of 2 and more, then those of 1, then the penalties (a big picture for each of 2 and over).
        for (int pass = 0; pass < 3; pass++) {
            for (int e = 0; e < EFFECTS; e++) {
                if (e == 3 || hidden[e]) continue;
                int v = values[e];
                if (pass == 0 && v < 2) continue;
                if (pass == 1 && v != 1) continue;
                if (pass == 2 && v >= 0) continue;
                int a = v < 0 ? -v : v;
                if (a == 0) continue;
                if (a > 1) {
                    struct game_sprite *bs = &sw->big[socialwin_big_index(v)];
                    sprite_draw_2(bs, b, bs->key, x, box.bottom - bs->full_h, 1, 1);
                    x += big_w + 1;
                }
                struct game_sprite *ss = &sw->icon[socialwin_small_index(e, v)];
                sprite_draw_2(ss, b, ss->key, x, y, 1, 1);
                x += icon_w + 1;
            }
        }
    }
    graphic_win_soft_update_3(&sw->win, &panel->left);
}

// The faction's own effects (its base values): its noun as the title, and an icon for each point under it.
static void UI_THIS socialwin_draw_faction_basal(struct game_socialwin *sw) {
    struct game_buffer *b = &sw->buf;
    const struct ui_rect *src = &sw->rect[SW_R_BASAL];
    struct ui_rect r = *src;
    buffer_set_clip(b, &r.left);
    buffer_box_sprite(b, &r.left, frame_at(FRAME_HEADER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(b, &title.left, frame_at(FRAME_INNER));
    const struct faction_def *d = &game_faction_defs[sw->faction];
    clear_text();
    *game_parse_gender = d->noun_gender;
    *game_parse_plural = d->noun_plural;
    put(d->noun);
    upper_text();
    font_of(b, &sw->font[0]);
    text_color(b, 0x9f);
    write_centered(b, &title);
    struct ui_rect foot = rect4(r.left, r.bottom - 0x14, r.right, r.bottom);
    buffer_box_sprite(b, &foot.left, frame_at(FRAME_INNER));

    const int32_t *base = fac(sw)->se_base;
    int total = 0;
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        total += base[e] < 0 ? -base[e] : base[e];
    }
    if (total != 0) {
        int icon_w = sw->icon[0].full_w, icon_h = sw->icon[0].full_h;
        int step = icon_w + 2;
        int x = foot.left + (foot.right - step * total - foot.left) / 2;
        int y = foot.top + (foot.bottom - icon_h - foot.top) / 2;
        for (int e = 0; e < EFFECTS; e++) {
            int v = base[e];
            int a = v < 0 ? -v : v;
            for (int k = 0; k < a; k++) {
                // (the original doesn't skip effect 3 here, and draws it with the row of the effect before)
                int row = e < 3 ? e : e - 1;
                struct game_sprite *s = &sw->icon[row * ICON_ROW + 6 + (v >= 1 ? 2 : 1)];
                sprite_draw_2(s, b, s->key, x, y, 1, 1);
                x += step;
            }
        }
    }
    graphic_win_soft_update_3(&sw->win, &src->left);
}

// ---- Drawing: the main screen's panels -----------------------------------------------------------------------

// The energy reserves and what the upheaval costs: two lines of the faction's energy numbers for the models chosen, the loss a skewed
// allocation brings (a line of percentages), and the cost of the change or what it gives back. (The window's own Buffer is clipped to
// the panel, though the drawing goes to the main screen's.)
static void UI_THIS socialwin_draw_info(struct game_socialwin *sw) {
    struct game_buffer *mb = mbuf();
    const struct ui_rect *src = &sw->rect[SW_R_INFO];
    struct ui_rect r = *src;
    struct faction_data *f = fac(sw);
    buffer_set_clip(&sw->buf, &r.left);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(mb, &title.left, frame_at(FRAME_INNER));
    clear_text();
    put(lab(0xaec));
    upper_text();
    font_of(mb, &sw->font[0]);
    text_color(mb, 0x9c);
    write_centered(mb, &title);
    r.top = title.bottom + 2;
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_BODY));
    r = shrunk(r, 3);
    struct font *font = &sw->font[4];
    font_of(mb, font);
    int diff = social_upheaval(sw->faction, sw->models) - f->se_upheaval_paid;
    int x = r.left;
    int line = line_height(font);
    struct socialwin_info_lines L;
    socialwin_info_lines(&L, *game_screen_width, r.left, r.right, r.top, r.bottom, line);

    clear_text();
    put(lab(0xefc));
    put(text(SEPARATOR));
    put_int(*(int32_t *)game_at(GLOBAL_ENERGY_A));
    text_color(mb, 0xa0);
    if (game_str_buffer) buffer_write_l_3(mb, game_str_buffer, x, L.y1, strlen(game_str_buffer));
    int y2 = L.y1 + line;
    clear_text();
    put(lab(0x258));
    put(text(SEPARATOR));
    put_int(*(int32_t *)game_at(GLOBAL_ENERGY_B));
    put(text(FLYING_GAP));
    put(lab(0xc4));
    if (game_str_buffer) buffer_write_l_3(mb, game_str_buffer, x, y2, strlen(game_str_buffer));

    // The loss the allocation makes: how far economy and labs are from each other, over the efficiency.
    int half = socialwin_loss_half(f->alloc_psych, f->alloc_labs);
    int unit = socialwin_loss_unit(sw->effect[SE_EFFIC]);
    int y = L.y3;
    x = L.x3;
    if (half != 0 && unit != 0) {
        int labs = f->alloc_labs, econ = 10 - f->alloc_psych - labs;
        int v = unit * half;
        clear_text();
        if (labs > econ) {
            put_int(-socialwin_clamp(v, 0, 0x28));
            put(text(PERCENT_SPACE_A));
            put(lab(0xf0));
            if (econ != 0) {
                put(text(COMMA));
                put_int(-socialwin_clamp(2 * v, 0, 0x50));
                put(text(PERCENT_SPACE_B));
                put(lab(0x2a4));
            }
        } else {
            if (labs != 0) {
                put_int(-socialwin_clamp(2 * v, 0, 0x50));
                put(text(PERCENT_SPACE_C));
                put(lab(0xf0));
                put(text(COMMA));
            }
            put_int(-socialwin_clamp(v, 0, 0x28));
            put(text(PERCENT_SPACE_D));
            put(lab(0x2a4));
        }
        text_color(mb, 0xf9);
        if (game_str_buffer) buffer_write_l_3(mb, game_str_buffer, x, y, strlen(game_str_buffer));
        y += line;
    }

    if (diff < 0) {
        clear_text();
        put(lab(0x578));
        put(text(SEPARATOR));
        put(text(PLUS_A));
        put_int(-diff);
        put(text(FLYING_GAP));
        put(lab(0x57c));
        text_color(mb, 0xa0);
        if (game_str_buffer) buffer_write_l_3(mb, game_str_buffer, x, y, strlen(game_str_buffer));
    } else if (diff > 0) {
        text_color(mb, 0xf9);
        clear_text();
        put(lab(0x574));
        put(text(SEPARATOR));
        put_int(diff);
        put(text(FLYING_GAP));
        put(lab(0x57c));
        if (game_str_buffer) buffer_write_l_3(mb, game_str_buffer, x, y, strlen(game_str_buffer));
    }
    graphic_win_soft_update_3(main_interface(), &src->left);
    buffer_set_clip(&sw->buf, sw->buf.full);
}

// The list of what each effect does, from the rules file ("SOC" and the effect's key, a section a line each; the line whose number
// reaches the effect's value is shown), each effect's value at the left (green above what the faction's pending models bring, red
// under); the effect whose button the mouse is on is white, the one being maximized yellow. The lines are put in the middle of the
// panel, wrapped to its width. (The original tests the length of the last line read, not each one's, to decide whether to wrap.)
static void UI_THIS socialwin_draw_overview(struct game_socialwin *sw) {
    struct game_buffer *mb = mbuf();
    const struct ui_rect *src = &sw->rect[SW_R_OVERVIEW];
    struct faction_data *f = fac(sw);
    int sel = sw->hover_effect < 0 ? -1 : socialwin_hover_effect(sw->hover_effect);
    buffer_set_clip(mb, mb->full);
    *(int32_t *)game_at(BUFFER_FIELD) = 5;
    struct ui_rect r = *src;
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_HEADER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(mb, &title.left, frame_at(FRAME_INNER));
    font_of(mb, &sw->font[0]);
    text_color(mb, 0x9f);
    char *heading = lab(0xb90);
    if (heading) buffer_write_cent_l_7(mb, heading, &title.left, strlen(heading));
    r.top = title.bottom + 2;
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_BODY));
    struct font *font = &sw->font[6];
    font_of(mb, font);
    r.left += 0x14;
    int value_x = r.left - 5;
    r.right -= 3;
    int total = 0, last_len = 0;
    char lines[EFFECTS][0x100];

    game_text_open(*(char **)game_at(RULES_FILE), text(SECTION));
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        clear_text();
        put(text(SECTION_PREFIX));
        put(effect_key(e));
        if (game_text_open(NULL, game_str_buffer)) {
            game_text_close();
            return;
        }
        lines[e][0] = 0;
        game_text_get();
        int n = game_text_item_number();
        strcpy(lines[e], game_text_item());
        if (n < sw->effect[e]) {
            do {
                game_text_get();
                if (!strlen(*game_text_line)) break;
                n = game_text_item_number();
                strcpy(lines[e], game_text_item());
            } while (n < sw->effect[e]);
        }
        last_len = (int)strlen(lines[e]);
        total += buffer_wrap_height(mb, lines[e], r.right - r.left);
    }
    game_text_close();

    // The lines' block is centred between the panel's top and bottom (the half taken toward zero).
    r.top += (r.bottom - total - r.top) / 2;
    int top = r.top;
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        if (sel == e) text_color(mb, 0xff);
        else if (sw->maximize == e) text_color(mb, 0x9c);
        else if (sw->effect[e] == f->se_pending[e]) text_color(mb, 0xa0);
        else text_color(mb, sw->effect[e] > f->se_pending[e] ? 0xfa : 0xf9);
        clear_text();
        if (sw->effect[e] > 0) put(text(PLUS_B));
        put_int(sw->effect[e]);
        font_of(mb, font);
        if (game_str_buffer) buffer_write_right_l_5(mb, game_str_buffer, value_x, top, strlen(game_str_buffer));
        sw->effect_prev[e] = f->se_pending[e];
        if (last_len != 0) {
            font_of(mb, font);
            struct ui_rect w = rect4(r.left, top, r.right, r.bottom);
            top = buffer_wrap_2(mb, lines[e], &w.left);
        }
    }
    *(int32_t *)game_at(BUFFER_FIELD) = 5;
    graphic_win_soft_update_3(main_interface(), &src->left);
}

// The three allocation bars are the same: a title with the share in percent, a bar tiled in two kinds of tile (the filled part
// first). `share` is in tenths, `label` the entry of the labels file with the bar's name, `first_tile` the pair of tiles it uses.
static void draw_alloc(struct game_socialwin *sw, int which, int label, int share, int first_tile) {
    struct game_buffer *mb = mbuf();
    const struct ui_rect *src = &sw->rect[which];
    buffer_set_clip(mb, mb->full);
    struct ui_rect r = *src;
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_INNER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left + 0x17, r.top, r.right - 0x17, r.top + 0x14);
    buffer_box_rect(mb, &title.left, 0xbc, 0xbc);
    clear_text();
    put(lab(label));
    put(text(FLYING_GAP));
    put_int(share * 10);
    put(text(PERCENT));
    upper_text();
    font_of(mb, &sw->font[0]);
    text_color(mb, 0x9f);
    write_centered(mb, &title);

    // The bar under the title: from the lock's width in, the filled part in tenths of what is left.
    int top = r.bottom - sw->lock_pic[0].full_h;
    int x = r.left + sw->lock_pic[0].full_w;
    int fill = x + (r.right - x) * share / 10;
    int end = r.right;
    if (fill < r.right) {
        end = fill + (share != 0);
        buffer_tile(mb, &sw->tile[first_tile], fill, top, fill, top, r.right - fill, r.bottom - top);
    }
    if (x < end) buffer_tile(mb, &sw->tile[first_tile + 1], x, top, x, top, end - x, r.bottom - top);
    graphic_win_soft_update_3(main_interface(), &src->left);
}

static void UI_THIS socialwin_draw_economy_alloc(struct game_socialwin *sw) {
    int share = 10 - fac(sw)->alloc_psych - fac(sw)->alloc_labs;
    draw_alloc(sw, SW_R_ECONOMY, 0x5b8, share, 0);
}
static void UI_THIS socialwin_draw_labs_alloc(struct game_socialwin *sw) { draw_alloc(sw, SW_R_LABS, 0xf0, fac(sw)->alloc_labs, 2); }
static void UI_THIS socialwin_draw_psych_alloc(struct game_socialwin *sw) { draw_alloc(sw, SW_R_PSYCH, 0x4f8, fac(sw)->alloc_psych, 4); }

// What the model (or the faction's own box) under the mouse does: its name as the title, and a line for each effect it has, the
// value (green with a plus, red with a minus) and the effect's picture and name. Two passes, the gains first, then the losses.
static void UI_THIS socialwin_draw_mouseover(struct game_socialwin *sw) {
    struct game_buffer *mb = mbuf();
    const struct ui_rect *src = &sw->rect[SW_R_HOVER];
    struct ui_rect r = *src;
    int hover = sw->hover;
    buffer_set_clip(mb, mb->full);
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_HEADER));
    r = shrunk(r, 3);
    struct ui_rect title = rect4(r.left, r.top, r.right, r.top + 0x14);
    buffer_box_sprite(mb, &title.left, frame_at(FRAME_INNER));
    int cat = 0, model = 0;
    if (hover != -1) {
        clear_text();
        if (hover == -2) {
            put(lab(0xb94));
        } else {
            cat = hover / 4;
            model = hover % 4;
            put(strings_at(model_name_id(cat, model)));
        }
        upper_text();
        font_of(mb, &sw->font[0]);
        text_color(mb, 0x9f);
        write_centered(mb, &title);
    }
    r.top = title.bottom + 2;
    buffer_box_sprite(mb, &r.left, frame_at(FRAME_BODY));
    if (hover == -1) {
        update_main(src);
        return;
    }
    r = shrunk(r, 3);
    const int32_t *base = fac(sw)->se_base;

    // The value of an effect of what is under the mouse, and whether it is drawn (a penalty a project cancels isn't; the faction's own
    // values have no such). (For the faction's box the original asks about a category and a model it hasn't set: they are left out.)
    int count = 0;
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        int v;
        if (hover == -2) {
            v = base[e];
        } else {
            if (immune(sw, cat, model, e)) continue;
            v = model_effect(cat, model, e);
        }
        count += v != 0;
    }
    int h = sw->icon[0].full_h, icon_w = sw->icon[0].full_w;
    font_of(mb, &sw->font[2]);
    int y = r.top + (r.bottom - h * count - r.top) / 2;
    r.left += 3;
    r.top = y;
    r.bottom = y + h;
    for (int pass = 0; pass < 2; pass++) {
        for (int e = 0; e < EFFECTS; e++) {
            if (e == 3) continue;
            if (hover != -2 && immune(sw, cat, model, e)) continue;
            int v = hover == -2 ? base[e] : model_effect(cat, model, e);
            if (v == 0) continue;
            if (pass == 0 ? v < 0 : v > 0) continue;
            clear_text();
            if (v < 0) {
                put(text(MINUS));
                text_color(mb, 0x67);
            } else {
                put(text(PLUS_C));
                text_color(mb, 0x98);
            }
            put_int(v < 0 ? -v : v);
            if (game_str_buffer) buffer_write_l_4(mb, game_str_buffer, &r.left, strlen(game_str_buffer));
            struct game_sprite *s = &sw->icon[socialwin_small_index(e, v)];
            sprite_draw_2(s, mb, s->key, r.left + 0x10, r.top, 1, 1);
            r.left += icon_w + 0x10;
            clear_text();
            put(effect_name(e));
            if (game_str_buffer) buffer_write_l_4(mb, game_str_buffer, &r.left, strlen(game_str_buffer));
            r.left -= 0x10 + icon_w;
            r.top += h;
            r.bottom += h;
        }
    }
    update_main(src);
}

// The text of an effect's button, which the button asks its parent for (id 0x11 to 0x1a, else the button draws its own): the picture
// (the three looks by the button's state), the effect's name, and its value, in green above what the faction's pending models bring
// and red under.
static int UI_THIS socialwin_on_button_text_draw(struct game_socialwin *sw, struct game_flat_button *btn, const char *name, int id) {
    if (id < ID_EFFECT_FIRST || id >= ID_RESET) return 0;
    int e = socialwin_hover_effect(id);
    struct ui_rect r = {btn->base.buf.full[0], btn->base.buf.full[1], btn->base.buf.full[2], btn->base.buf.full[3]};
    int y = (r.bottom - sw->icon[0].full_h - r.top) / 2;
    struct game_flat_button *self = &sw->button[id];
    int look;
    switch ((int)(intptr_t)self->base.picture) {
    case 0: look = 1; break;
    case 1:
    case 2: look = 0; break;
    default: look = self->base.state ? 0 : self->base.changed == 0; break;
    }
    struct game_buffer *b = btn ? &btn->base.buf : NULL;
    struct game_sprite *s = &sw->icon[9 * (id - FIRST_EFFECT_BUTTON) + 3 * look];
    sprite_draw_2(s, b, s->key, 5, y, 1, 1);
    struct game_buffer *bb = &btn->base.buf;
    text_color(bb, 0);
    r.left += 0x25;
    if (name) buffer_write_l_4(bb, name, &r.left, strlen(name));
    int cur = sw->effect[e], pending = fac(sw)->se_pending[e];
    if (cur > pending) text_color(bb, 0xfa);
    else if (cur < pending) text_color(bb, 0xf9);
    clear_text();
    if (sw->effect[e] > 0) put(text(PLUS_D));
    put_int(sw->effect[e]);
    r.right -= 3;
    if (game_str_buffer) buffer_write_right_l_6(bb, game_str_buffer, &r.left, strlen(game_str_buffer));
    return 1;
}

// The whole window: the main screen's panels cleared and the frames put round the allocation bars and the buttons, then every part,
// then the effects' buttons.
static void UI_THIS socialwin_on_redraw(struct game_socialwin *sw) {
    struct game_buffer *mb = mbuf();
    buffer_set_clip(mb, mb->full);
    buffer_set_clip(&sw->buf, sw->buf.full);
    game_main_interface_restore_back(main_interface(), 1);
    int32_t r[4];
    game_union_rect(r, &sw->rect[SW_R_LABS].left, &sw->rect[SW_R_PSYCH].left);
    game_union_rect(r, r, &sw->rect[SW_R_ECONOMY].left);
    r[0] -= 3, r[1] -= 3, r[2] += 3, r[3] += 3;
    buffer_box_sprite(mb, r, frame_at(FRAME_HEADER));
    game_union_rect(r, &sw->rect[SW_R_CANCEL].left, &sw->rect[SW_R_OK].left);
    game_union_rect(r, r, &sw->rect[SW_R_RESET].left);
    r[0] -= 3, r[1] -= 3, r[2] += 3, r[3] += 3;
    buffer_box_sprite(&sw->buf, r, frame_at(FRAME_HEADER));
    socialwin_draw_presets(sw);
    for (int cat = 0; cat < CATEGORIES; cat++) draw_social(sw, cat);
    socialwin_draw_faction_basal(sw);
    socialwin_draw_info(sw);
    socialwin_draw_overview(sw);
    socialwin_draw_economy_alloc(sw);
    socialwin_draw_labs_alloc(sw);
    socialwin_draw_psych_alloc(sw);
    socialwin_draw_mouseover(sw);
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        redraw(&sw->button[FIRST_EFFECT_BUTTON + socialwin_effect_button(e)]);
    }
}

// ---- Input -------------------------------------------------------------------------------------------------

static void tut_reset(void) { game_tutwin_reset(game_at(TUT_WIN)); }

// Puts the bubble text at a point of a window (client coordinates converted first); the rectangle is the point alone.
static void bubble_at(void *win, const char *label, int x, int y) {
    win_client_to_screen_2(win, &x, &y);
    int32_t rect[4] = {x, y, x, y};
    game_win_set_bubble_text(win, label, rect);
}

// bubble_me_elmo2: the same for the window itself, and for the main screen's window.
static void UI_THIS socialwin_bubble_me(struct game_socialwin *sw, const char *label, int x, int y) { bubble_at(&sw->win, label, x, y); }
static void UI_THIS socialwin_bubble_me_main(void *unused, const char *label, int x, int y) {
    (void)unused;
    bubble_at(main_interface(), label, x, y);
}

// A click on the window itself: the tutorial's note, if one is up, is taken down.
static void UI_THIS socialwin_on_left_click(struct game_socialwin *sw, int x, int y) {
    (void)sw, (void)x, (void)y;
    tut_reset();
}

// A right click gives the help of what it is on: the four category panels the models', the faction's box its own.
static void UI_THIS socialwin_on_right_click(struct game_socialwin *sw, int x, int y) {
    tut_reset();
    for (int c = 0; c < CATEGORIES; c++)
        if (in_box_2(x, y, &sw->rect[SW_R_PANEL + c].left)) {
            bubble_at(&sw->win, lab(0xcf0), x, y);
            return;
        }
    if (in_box_2(x, y, &sw->rect[SW_R_BASAL].left)) bubble_at(&sw->win, lab(0xd20), x, y);
}

// The mouse over a model button: its box of what it does is drawn (a hover of the same button isn't drawn again); over an effect's
// button the overview lights its line.
static void UI_THIS socialwin_on_button_passover(struct game_socialwin *sw, int id, int over) {
    if (id >= 0 && id < 16 && over != 0 && sw->hover != id) {
        sw->hover = id;
        socialwin_draw_mouseover(sw);
    }
    if (id >= ID_EFFECT_FIRST && id <= ID_RESET) {
        if (over == 0) {
            if (sw->hover_effect != -1) {
                sw->hover_effect = -1;
                socialwin_draw_overview(sw);
                game_do_all_draws();
                return;
            }
        } else if (sw->hover_effect != id) {
            sw->hover_effect = id;
            socialwin_draw_overview(sw);
        }
    }
    game_do_all_draws();
}

// The mouse over the window: the box shows the model button or the faction's box it is on (the first model button of each category
// that holds it; the faction's box -2).
static void UI_THIS socialwin_on_mouse_move(struct game_socialwin *sw, int x, int y) {
    game_win_clear_bubble();
    game_win_clear_bubble();
    int sel = -1;
    for (int cat = 0; cat < CATEGORIES; cat++) {
        for (int model = 0; model < MODELS; model++) {
            if (!society_avail(cat, model, sw->faction)) continue;
            const struct ui_rect *p = &sw->rect[SW_R_PANEL + cat];
            const struct ui_rect *m = &sw->rect[SW_R_MODEL + cat * 4 + model];
            struct ui_rect hit = rect4(m->left, p->top, m->right, p->bottom);
            if (in_box_2(x, y, &hit.left)) {
                sel = model + cat * 4;
                break;
            }
        }
    }
    int now = in_box_2(x, y, &sw->rect[SW_R_BASAL].left) ? -2 : sel;
    if (now != sw->hover) {
        sw->hover = now;
        socialwin_draw_mouseover(sw);
    }
}

static void UI_THIS socialwin_on_mouse_leave(struct game_socialwin *sw, int x, int y) {
    (void)x, (void)y;
    game_win_clear_bubble();
    game_win_clear_bubble();
    if (sw->hover >= 0) {
        sw->hover = -1;
        socialwin_draw_mouseover(sw);
    }
}

// A model's radio button changed (ids 0 to 15, `a` is the group's user), or an effect's button was pressed (0x11 to 0x1a): the model
// is chosen, or the effect maximized. A model chosen puts the effects' group at none when no maximize is going.
static void UI_THIS socialwin_on_group_clicked(struct game_socialwin *sw, int user, int id) {
    (void)user;
    if (id >= ID_EFFECT_FIRST && id < ID_RESET) {
        socialwin_maximize_effect(sw, socialwin_hover_effect(id));
        return;
    }
    if (id < 0 || id >= 16) return;
    socialwin_set_model(sw, id / 4, id % 4);
    if (sw->maximize == -1 && sw->resetting == 0) {
        button_group_set(&sw->group_effect, -1, 1);
        redraw(sw);
    }
}

// Reset, OK and Cancel (and the keys the original lets Escape and Return click): -2 and 16 reset and then end the window as OK does
// (so the pending models stand), -1 and 28 (OK) end it, 27 (Reset) resets.
static void UI_THIS socialwin_on_button_clicked(struct game_socialwin *sw, int id) {
    tut_reset();
    switch (id) {
    case -2:
    case ID_CANCEL:
        socialwin_reset(sw);
        socialwin_exit(sw);
        break;
    case -1:
    case ID_OK:
        socialwin_exit(sw);
        break;
    case ID_RESET:
        socialwin_reset(sw);
        break;
    default:
        break;
    }
}

// Return clicks OK; Escape resets and then clicks OK, as Cancel does.
static int UI_THIS socialwin_on_key_click(struct game_socialwin *sw, int a, int key) {
    (void)a;
    tut_reset();
    if (key == 0xd || key == 0x1b) {
        if (key == 0x1b) socialwin_reset(sw);
        socialwin_exit(sw);
    }
    return 1;
}

static void UI_THIS socialwin_on_status(void *iface, int shown);
static void UI_THIS socialwin_show(struct game_socialwin *sw, int unused) {
    (void)unused;
    if (!win_is_visible(&sw->win)) game_set_iface_mode(iface_of(sw));
}
static void UI_THIS socialwin_hide(struct game_socialwin *sw) {
    if (win_is_visible(&sw->win)) game_release_iface_mode(iface_of(sw));
}

// ---- The sub interface (what the main screen sends the window) ------------------------------------------------

// The sub interface is shown or hidden: the buttons and locks with it, and the window slid in or out.
static void UI_THIS socialwin_on_status(void *iface, int shown) {
    struct game_socialwin *sw = from_iface(iface);
    if (shown) {
        UI_VCALL(void(UI_THIS *)(void *), sw, 0x30);
        for (int i = 0; i < BUTTONS; i++) show(&sw->button[i]);
        for (int i = 0; i < 3; i++) show(&sw->lock[i]);
        graphic_win_update_3(main_interface(), 0);
        game_do_all_draws();
        game_slide_show(sw, 0);
        game_win_show(sw, 3);
    } else {
        game_slide_hide(sw, 0);
        game_win_hide(sw);
        game_do_all_draws();
        for (int i = 0; i < BUTTONS; i++) hide(&sw->button[i]);
        for (int i = 0; i < 3; i++) hide(&sw->lock[i]);
    }
}

static void UI_THIS socialwin_iface_left_click(void *iface, int x, int y) {
    (void)iface, (void)x, (void)y;
    tut_reset();
}

// A right click on one of the main screen's panels: the overview and the three bars give their help.
static void UI_THIS socialwin_iface_right_click(void *iface, int x, int y) {
    struct game_socialwin *sw = from_iface(iface);
    tut_reset();
    static const struct {
        int rect, label;
    } helps[] = {{SW_R_ECONOMY, 0xd24}, {SW_R_LABS, 0xd28}, {SW_R_PSYCH, 0xd2c}, {SW_R_OVERVIEW, 0xd1c}};
    for (int i = 0; i < 4; i++)
        if (in_box_2(x, y, &sw->rect[helps[i].rect].left)) {
            bubble_at(main_interface(), lab(helps[i].label), x, y);
            return;
        }
}

static void UI_THIS socialwin_iface_nothing_2(void *iface, int a, int b) { (void)iface, (void)a, (void)b; }
static void UI_THIS socialwin_iface_nothing_3(void *iface, int a, int b, int c) { (void)iface, (void)a, (void)b, (void)c; }
static void UI_THIS socialwin_iface_mouse_move(void *iface, int x, int y) {
    (void)iface, (void)x, (void)y;
    game_win_clear_bubble();
    game_win_clear_bubble();
}

// The + and - buttons of the three bars (ids 0x1d to 0x22): the share changes by one tenth (not under 0, not over the limit: the
// flag is set then), the others take it up (see socialwin_allocate), and the faction's allocation, the effects and the energy are put
// right. (The faction's economy share is what the others leave.)
static void UI_THIS socialwin_iface_button_clicked(void *iface, int id) {
    struct game_socialwin *sw = from_iface(iface);
    tut_reset();
    if (id < ID_ALLOC_FIRST || id > ID_ALLOC_FIRST + 5) return;
    static const struct {
        int index, delta;
    } steps[6] = {{0, -1}, {0, 1}, {2, -1}, {2, 1}, {1, -1}, {1, 1}};
    int index = steps[id - ID_ALLOC_FIRST].index;
    sw->alloc_shown[index] += steps[id - ID_ALLOC_FIRST].delta;
    if (sw->alloc_shown[index] < 0) sw->alloc_shown[index] = 0;
    if (sw->alloc_shown[index] > energy_limit(sw->faction)) {
        sw->capped = 1;
        sw->alloc_shown[index] = energy_limit(sw->faction);
    }
    socialwin_allocate_energy(sw, index, sw->alloc_shown[index]);
    for (int k = 0; k < 3; k++) sw->alloc_shown[k] = sw->alloc[k];
    struct faction_data *f = fac(sw);
    f->alloc_psych = sw->alloc_shown[1];
    f->alloc_labs = sw->alloc_shown[2];
    social_set(sw->faction);
    social_calc(sw->models, sw->effect, sw->faction, 0, 0);
    social_calc(sw->models, f->se_pending, sw->faction, 0, 0);
    game_energy_compute(sw->faction, 1);
    sw->energy_b = *(int32_t *)game_at(GLOBAL_ENERGY_B);
    sw->energy_a = *(int32_t *)game_at(GLOBAL_ENERGY_A);
    social_calc(f->se_models_pending, f->se_pending, sw->faction, 0, 0);
    redraw(sw);
}

// ---- Opening the window --------------------------------------------------------------------------------------

// Makes a button (hidden by the caller): the name (NULL for none), the id and the rectangle, on the given parent.
static void make_button(struct game_flat_button *b, const char *name, int id, const struct ui_rect *r, void *parent) {
    game_base_button_init(b, name, id, r->left, r->top, r->right - r->left, r->bottom - r->top, parent, 0);
}

// One of the buttons along the bottom and down the right: made, hidden, given the window's small font and drawn, the bubble text
// (the label at `bubble`, if any) set, and put in the group (if any).
static void make_text_button(struct game_socialwin *sw, struct game_flat_button *b, int id, const struct ui_rect *r, int bubble,
                             struct game_button_group *group) {
    make_button(b, game_str_buffer, id, r, sw);
    hide(b);
    font_of(&b->base.buf, &sw->font[1]);
    redraw(b);
    if (bubble) base_button_set_bubble_text(&b->base, lab(bubble));
    if (group) button_group_add(group, &b->base);
}

// The pictures are cut out of one sheet: ten rows of nine (the effects' buttons' looks and icons), the lock's two pictures, the
// + and - buttons' looks, the six tiles of the bars, and the big pictures for 2 to 6.
static void cut_pictures(struct game_socialwin *sw, struct game_buffer *sheet) {
    int key = *game_default_key;
    for (int row = 0; row < 10; row++)
        for (int col = 0; col < ICON_ROW; col++)
            sprite_extract_2(&sw->icon[row * ICON_ROW + col], sheet, key, 1 + 27 * col, 1 + 27 * row, 0x1a, 0x1a, NULL);
    sprite_extract_2(&sw->lock_pic[0], sheet, key, 0xf5, 1, 0x18, 0xe, NULL);
    sprite_extract_2(&sw->lock_pic[1], sheet, key, 0xf5, 0x10, 0x18, 0xe, NULL);
    for (int k = 0; k < 3; k++) {
        sprite_extract_2(&sw->arrow[k], sheet, key, 0x13b, 1 + 0x15 * k, 0x14, 0x14, NULL);
        sprite_extract_2(&sw->arrow[3 + k], sheet, key, 0x150, 1 + 0x15 * k, 0x14, 0x14, NULL);
    }
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 2; row++) {
            struct game_buffer *t = &sw->tile[col * 2 + row];
            buffer_init(t, 0xe, 0xe, 0, NULL);
            buffer_copy_full(sheet, t, 0x10e + 0xf * col, 1 + 0xf * row, 0, 0, 0xe, 0xe);
        }
}

// The window opens (init does not return until it is closed, unless `arg` is negative: then it is only made, for the tutorial's
// tour, which shows and closes it by hand). `arg` is the faction, or minus it. The rectangles are worked out, the fonts and
// pictures made (the pictures once), the window made, the models and allocation read from the faction, the buttons made and hidden,
// and the window shown as a modal one: the tutorial's note (once) first.
static void UI_THIS socialwin_init(struct game_socialwin *sw, int arg) {
    struct game_buffer sheet;
    buffer_ctor(&sheet);
    close_parts(sw);
    void *iface = iface_of(sw);
    sw->iface_mode = 2;
    *(void **)game_at(CURRENT_IFACE) = iface;
    int no_window = arg < 0;
    sw->faction = no_window ? -arg : arg;
    void *parent_win = main_interface();

    socialwin_layout(sw->rect, (const int32_t *)game_at(PANELS));

    struct font *fonts = sw->font;
    font_init(&fonts[0], *(char **)game_at(FONT_FACE), 0xe, 1);
    font_init(&fonts[1], *(char **)game_at(FONT_FACE), 0xe, 1);
    font_init(&fonts[2], *(char **)game_at(FONT_FACE_2), 0xe, 1);
    font_init(&fonts[3], *(char **)game_at(FONT_FACE_2), 0xe, 1);
    font_init(&fonts[4], *(char **)game_at(FONT_FACE), 0xc, 0);
    font_init(&fonts[5], *(char **)game_at(FONT_FACE), 0xb, 1);
    fonts[5].first = 3;
    font_init(&fonts[6], *(char **)game_at(FONT_FACE), 0xb, 0);
    fonts[6].first = 3;
    font_init(&fonts[7], *(char **)game_at(FONT_FACE_2), 0xe, 1);
    font_init(&fonts[8], *(char **)game_at(FONT_FACE_2), 0xe, 1);

    if (!sw->icon[0].data) {
        buffer_load_pcx(&sheet, text(SHEET), NULL, 10, 0xec);
        cut_pictures(sw, &sheet);
    }
    int key = *game_default_key;
    for (int k = 0; k < 5; k++) {
        sprite_extract_2(&sw->big[2 * k], &sheet, key, 0x115 + 0xe * k, 0x6f, 0xd, 0xd, NULL);
        sprite_extract_2(&sw->big[2 * k + 1], &sheet, key, 0x115 + 0xe * k, 0x7d, 0xd, 0xd, NULL);
    }

    int x = (*game_screen_width - 0x321) / 2;
    int y = *game_win_screen_h - *(int32_t *)game_at(PANEL_TOP) - 0x162;
    void *console = game_at(CONSOLE);
    void *console_win = (uint8_t *)console + ((int32_t *)*(void **)console)[1];
    graphic_win_init(&sw->win, x, y, 0x341, 0x162, NULL, 0x1000000, console_win, NULL, NULL);
    graphic_win_fill(&sw->win, 9);
    struct ui_rect frame = rect4(0x17, 0, 0x30a, 0x162);
    buffer_box_sprite(&sw->buf, &frame.left, frame_at(FRAME_WINDOW));

    // The allocation: economy is what psych and labs leave. All three copies start alike.
    struct faction_data *f = fac(sw);
    sw->alloc_start[0] = sw->alloc[0] = sw->alloc_shown[0] = 10 - f->alloc_labs - f->alloc_psych;
    sw->alloc_start[1] = sw->alloc[1] = sw->alloc_shown[1] = f->alloc_psych;
    sw->alloc_start[2] = sw->alloc[2] = sw->alloc_shown[2] = f->alloc_labs;
    socialwin_reset(sw);

    // The four groups of model buttons (the ones the faction can choose; the chosen one pressed), then the effects' group.
    for (int cat = 0; cat < CATEGORIES; cat++) {
        struct game_button_group *g = &sw->group[cat];
        button_group_init(g, cat, 0);
        for (int model = 0; model < MODELS; model++) {
            if (!society_avail(cat, model, sw->faction)) continue;
            int id = cat * 4 + model;
            clear_text();
            put(strings_at(model_name_id(cat, model)));
            upper_text();
            make_text_button(sw, &sw->button[id], id, &sw->rect[SW_R_MODEL + id], 0xcf0, g);
        }
        button_group_set(g, sw->models[cat] + cat * 4, 1);
    }
    button_group_init(&sw->group_effect, 2, 0);
    for (int e = 0; e < EFFECTS; e++) {
        if (e == 3) continue;
        int slot = socialwin_effect_button(e);
        clear_text();
        put(effect_name(e));
        upper_text();
        make_text_button(sw, &sw->button[FIRST_EFFECT_BUTTON + slot], ID_EFFECT_FIRST + slot, &sw->rect[SW_R_EFFECT + slot], 0xcf4 + 4 * slot, &sw->group_effect);
    }
    clear_text();
    put(lab(0x198));
    if (*game_language != 2) upper_text();
    make_text_button(sw, &sw->button[ID_CANCEL], ID_CANCEL, &sw->rect[SW_R_CANCEL], 0, NULL);
    clear_text();
    put(lab(0x4f4));
    upper_text();
    make_text_button(sw, &sw->button[ID_RESET], ID_RESET, &sw->rect[SW_R_RESET], 0xd34, NULL);
    clear_text();
    put(lab(0x194));
    upper_text();
    make_text_button(sw, &sw->button[ID_OK], ID_OK, &sw->rect[SW_R_OK], 0, NULL);

    // The locks under the bars (checked, the share can't be taken from or given to), then the + and - buttons, on the main screen.
    for (int k = 0; k < 3; k++) {
        sw->lock[k].pic_off = &sw->lock_pic[0];
        sw->lock[k].pic_on = &sw->lock_pic[1];
        game_check_button_init(&sw->lock[k], sw->rect[SW_R_LOCK + k].left, sw->rect[SW_R_LOCK + k].top, parent_win, 0);
    }
    for (int k = 0; k < 3; k++) hide(&sw->lock[k]);
    static const int bubbles[3] = {0xd24, 0xd28, 0xd2c};
    for (int i = 0; i < 6; i++) {
        struct game_flat_button *b = &sw->button[ID_ALLOC_FIRST + i];
        make_button(b, NULL, ID_ALLOC_FIRST + i, &sw->rect[SW_R_ALLOC_BTN + i], parent_win);
        hide(b);
        for (int look = 0; look < 3; look++) b->pic[look] = &sw->arrow[(i & 1) * 3 + look];
        base_button_set_bubble_text(&b->base, lab(bubbles[i / 2]));
    }

    if (!no_window) {
        if (game_tut_check2(8)) {
            show(sw);
            int note_x, note_y;
            *(int32_t *)game_at(TUT_NOTE_WORD) = 5;
            game_tutwin_soc_rect(game_at(TUT_WIN), game_at(TUT_NOTE_RECT), &note_x, &note_y);
            void *map = game_map_wins[0];
            void *map_win = map ? (uint8_t *)map + ((int32_t *)*(void **)map)[1] : NULL;
            game_tutwin_note(game_at(TUT_WIN), map_win, text(TUTORIAL_NOTE), note_x, note_y, NULL, 1, -1, -1);
        }
        UI_VCALL(void(UI_THIS *)(void *, int, int, int), sw, VT_SET_MODAL, 0, (int)(uintptr_t)game_pop_wait, (int)(uintptr_t)main_interface());
        game_release_iface_mode(iface);
    }
    buffer_dtor(&sheet);
}

// ---- Stubs and thunks -----------------------------------------------------------------------------------------

static void UI_THIS socialwin_stub(void *unused) { (void)unused; }  // UNK2 and UNK3 do nothing
static void UI_THIS socialwin_draw_social(struct game_socialwin *sw, int cat) { draw_social(sw, cat); }

// ---- Hooks -------------------------------------------------------------------------------------------------

#define PROLOGUE 0x55, 0x8b, 0xec  // push ebp; mov ebp, esp
#define SEH_PROLOGUE PROLOGUE, 0x6a, 0xff  // push -1 (the exception frame's)
#define RET8 0xc2, 0x08, 0x00, 0x90, 0x90

static const struct hook socialwin_hooks[] = {
    UI_HOOK(0x4ae9e0, socialwin_ctor, SEH_PROLOGUE),
    UI_HOOK(0x4aebf0, socialwin_set_model, PROLOGUE, 0x8b, 0x45, 0x08),
    UI_HOOK(0x4aece0, socialwin_reset, PROLOGUE, 0x51, 0x53),
    UI_HOOK(0x4aef00, socialwin_stub, 0xc3, 0x90, 0x90, 0x90, 0x90),
    UI_HOOK(0x4aef10, socialwin_exit, 0x53, 0x56, 0x8b, 0xf1, 0x57),
    UI_HOOK(0x4af110, socialwin_maximize_effect, PROLOGUE, 0x83, 0xec, 0x14),
    UI_HOOK(0x4af1e0, socialwin_draw_presets, PROLOGUE, 0x83, 0xec, 0x20),
    UI_HOOK(0x4af310, socialwin_draw_social, PROLOGUE, 0x83, 0xec, 0x3c),
    UI_HOOK(0x4af8c0, socialwin_draw_faction_basal, PROLOGUE, 0x83, 0xec, 0x34),
    UI_HOOK(0x4afb60, socialwin_draw_info, PROLOGUE, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00),
    UI_HOOK(0x4b0410, socialwin_draw_overview, PROLOGUE, 0x81, 0xec, 0x94, 0x0b, 0x00, 0x00),
    UI_HOOK(0x4b08a0, socialwin_draw_economy_alloc, PROLOGUE, 0x83, 0xec, 0x74),
    UI_HOOK(0x4b0b00, socialwin_draw_labs_alloc, PROLOGUE, 0x83, 0xec, 0x74),
    UI_HOOK(0x4b0d50, socialwin_draw_psych_alloc, PROLOGUE, 0x83, 0xec, 0x74),
    UI_HOOK(0x4b0fa0, socialwin_immune, PROLOGUE, 0xa1, 0x74, 0x65, 0x9a, 0x00),
    UI_HOOK(0x4b1080, socialwin_draw_mouseover, PROLOGUE, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00),
    UI_HOOK(0x4b1790, socialwin_stub, 0xc3, 0x90, 0x90, 0x90, 0x90),
    UI_HOOK(0x4b17a0, socialwin_on_button_text_draw, PROLOGUE, 0x83, 0xec, 0x60),
    UI_HOOK(0x4b19a0, socialwin_on_redraw, PROLOGUE, 0x83, 0xec, 0x10),
    UI_HOOK(0x4b1b20, socialwin_on_left_click, 0xb9, 0x68, 0x6e, 0x8c, 0x00),  // mov ecx, tut_win
    UI_HOOK(0x4b1b30, socialwin_bubble_me, PROLOGUE, 0x83, 0xec, 0x10),
    UI_HOOK(0x4b1b70, socialwin_bubble_me_main, PROLOGUE, 0x83, 0xec, 0x10),
    UI_HOOK(0x4b1bc0, socialwin_on_right_click, PROLOGUE, 0x83, 0xec, 0x18),
    UI_HOOK(0x4b1ce0, socialwin_on_group_clicked, PROLOGUE, 0x8b, 0x45, 0x0c),
    UI_HOOK(0x4b1d70, socialwin_on_button_passover, PROLOGUE, 0x53, 0x8b, 0x5d, 0x0c),
    UI_HOOK(0x4b1e00, socialwin_on_button_clicked, PROLOGUE, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x4b1e80, socialwin_on_key_click, PROLOGUE, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x4b1ec0, socialwin_on_mouse_move, PROLOGUE, 0x83, 0xec, 0x18),
    UI_HOOK_CALL(0x4b1fc0, socialwin_on_mouse_leave, 0x56, 0x8b, 0xf1, 0xe8, 0x38, 0x65, 0x14, 0x00),  // push esi; mov esi, ecx; call win_clear_bubble
    UI_HOOK(0x4b1ff0, socialwin_allocate_energy, PROLOGUE, 0x51, 0x53),
    UI_HOOK(0x4b2120, socialwin_on_status, PROLOGUE, 0x8b, 0x45, 0x08),
    UI_HOOK(0x4b2210, socialwin_iface_left_click, 0xb9, 0x68, 0x6e, 0x8c, 0x00),
    UI_HOOK(0x4b2220, socialwin_iface_right_click, PROLOGUE, 0x83, 0xec, 0x10),
    UI_HOOK(0x4b2400, socialwin_iface_nothing_2, RET8),
    UI_HOOK(0x4b2410, socialwin_iface_nothing_2, RET8),
    UI_HOOK_CALL(0x4b2420, socialwin_iface_mouse_move, 0xe8, 0xdb, 0x60, 0x14, 0x00),  // call win_clear_bubble
    UI_HOOK_CALL(0x4b2430, socialwin_iface_mouse_move, 0xe8, 0xcb, 0x60, 0x14, 0x00),
    UI_HOOK(0x4b2440, socialwin_iface_nothing_2, RET8),
    UI_HOOK(0x4b2450, socialwin_iface_nothing_2, RET8),
    UI_HOOK(0x4b2460, socialwin_iface_nothing_2, RET8),
    UI_HOOK(0x4b2470, socialwin_iface_button_clicked, PROLOGUE, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x4b2670, socialwin_iface_nothing_2, RET8),
    UI_HOOK(0x4b2680, socialwin_iface_nothing_3, 0xc2, 0x0c, 0x00, 0x90, 0x90),
    UI_HOOK_CALL(0x4b2690, socialwin_show, 0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0x57, 0x14, 0x00),  // push esi; mov esi, ecx; call win_is_visible
    UI_HOOK_CALL(0x4b26b0, socialwin_hide, 0x56, 0x8b, 0xf1, 0xe8, 0xd8, 0x57, 0x14, 0x00),
    UI_HOOK(0x4b26d0, socialwin_close, 0x53, 0x56, 0x57, 0x8b, 0xf9),
    UI_HOOK(0x4b2740, socialwin_init, SEH_PROLOGUE),
    UI_HOOK(0x4b3c80, socialwin_dtor, SEH_PROLOGUE),
    UI_HOOK(0x4b3f50, socialwin_scalar_dtor, PROLOGUE, 0x56, 0x8b, 0xf1),
    UI_HOOK(0x4b3f90, socialwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),
};

struct hook_module socialwin_module = {"socialwin", socialwin_hooks, UI_HOOK_COUNT(socialwin_hooks), NULL, NULL, -1};
