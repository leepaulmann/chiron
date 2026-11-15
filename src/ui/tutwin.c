// The tutorial's window, the game's TutWin, and the functions that only open or drive it. It is one global object (0x8c6e68) built
// by a static initializer and destroyed by an atexit handler. The window is a Popup (the game's text-and-buttons dialog) that
// sits at one side of the screen with an arrow drawn from it to what the note is about (a square of the map, a unit, a base, or
// a part of one of the game's screens); the picture in the popup is the unit, the base or the terrain the note is about, drawn
// by one of three callbacks the object hands the popup (the draw function in the popup's field 0x3168).
//
// Outside the class: the tutorial's notes are asked for from the rest of the game by tut_map (a note about a square: the game's
// moves, combat, the first sight of a thing, the meeting of another faction; 38 call sites), by reset (22 call sites: whenever
// a turn or a screen would leave a note standing), by tut_win and the rectangle helpers (the tour's code), and by the arrow's
// redraw (called by the info panel's drawing); the object's fields are read and written by absolute address by other code: 0x8cc210
// (shown: a note is up), 0x8cc228 (the tour to take), 0x8cc244 and 0x8cc248 (two flags the buttons' answers set). The tutorial's
// "has this note been shown" words (0x9a64ac, 0x9a64b0, 0x9a64b8, 0x9a64bc) are kept by tut_check and tut_check2 (below) and the
// window's own button handler.
//
// What a note does: tut_win builds the popup for a label of the script file, works out the side and height it sits at from the
// point it is about (tutwin_place), draws the arrow with a pen over the map window (and a second arrow, when two points are
// given), shows the popup (a modal window: it returns when a button or a key has ended it), and, for a note that goes on to a tour
// (mode 1), calls the tour or hands a key that closed it to the console. The buttons and keys come back as on_button_clicked and
// on_key_click, which take the window down.
//
// Boundary: the five tours (the functions the original calls TutWin_tour, tour1, tour5, tour6 and tour7, 12 KB between them, each
// a script of notes over the game's screens) are not rebuilt here: this file's tut_win calls the original's tour through
// game_tutwin_tour, and the tours call these functions as the game's own code. The tour's own tests of what the player has done
// and which flags it sets stay the original's.
//
// Rebuilt from the original as one class (see "Rebuilding a screen" in CLAUDE.md): all its functions but the tours are here and
// CHIRON_SKIP=tutwin gives them all back. The object keeps the original's layout, the toolkit is called directly, and what it
// draws, in what order, and what it writes are the original's. Static initializers run after hooks_apply, so the constructor and
// destructor are ours too. tut_map was a function the shadow mode held while a check protects the game's state (quiet.c: the
// window builds a popup, which allocates from the game's heap); it is held here (shadow_tut_map) so the hook table has one entry
// for the address.
//
// What the original does that looks odd and is kept: tut_map sets the note's subject and square and tut_win, which it calls, begins by
// clearing both (it clears everything a finished note leaves), so the picture callbacks never run in the game's own flow; the notes'
// own buttons answer id 0 (Continue, Close) or -2 (End Tour), so the branches of the button handler for a positive id are the
// tour's; and the flag at 0x8cc248 the handler sets is taken by the game's console loop a moment later.
//
// Differences from the original that can't be seen: the exception frames are not reproduced; the destructor is the Popup's own
// (the original inlines a copy of it after closing the popup, the same calls in the same order); the two second-time reads of
// fields the first draw has just written are taken from the same variables.
// Differences from OpenSMACX/Thinker: they have none of these functions' bodies; the names here are our own.
#include "tutwin.h"

#include <stddef.h>
#include <stdint.h>

#include "../engine/dialog.h"
#include "../engine/game.h"
#include "../engine/screen.h"
#include "../engine/sprite.h"
#include "../engine/win.h"

// ---- Pure parts ------------------------------------------------------------------------------------------

int tutwin_middle(int from, int to) { return from + (to - from) / 2; }

int tutwin_note_due(int net_game, uint32_t preferences, uint32_t seen, uint32_t bit, uint32_t rule_flags) {
    if (net_game) return 0;
    if (!(preferences & 0x20)) return 0;
    if (bit & seen) return 0;
    if (rule_flags & 0x80) return 0;
    return 1;
}

// The place works in eighths of the screen: the popup goes to the right of the middle (and a little more) when the point is at its
// right, and above the point when the point is in the lower part (below five eighths of the height, less an eighth for the modes
// that ask for it).
void tutwin_place(struct tutwin_place *p, int mode, int screen_w, int screen_h, int target_x, int target_y, int popup_w,
                  int popup_h) {
    int limit_x = screen_w / 2, wd8 = screen_w / 8, hd8 = screen_h / 8, limit_y = screen_h * 5 / 8;
    if (mode < 3 || mode == 5) limit_x += wd8;
    if (mode == 3 || mode == 4) limit_x -= wd8;
    if (mode == 4 || mode == 5) limit_y -= hd8;

    // At or above the limit the popup stands at the bottom, 8 up from the edge, and the arrow leaves its top... as the original
    // reads it: its top is the height less the popup's less 8 and the arrow leaves from there; below the limit the popup's top
    // is an eighth of the point's y (the screen's own scaling) and the arrow leaves from its bottom.
    if (target_y <= limit_y) {
        p->y = screen_h - popup_h - 8;
        p->tail_y = p->y;
    } else {
        p->y = target_y / 8;
        p->tail_y = p->y + popup_h;
    }
    if (target_x <= limit_x) {
        p->x = 5;
        p->tail_x = popup_w / 2 + 5;
    } else {
        p->x = screen_w - popup_w - 5;
        p->tail_x = screen_w - popup_w / 2 - 5;
    }
}

// ---- The object -------------------------------------------------------------------------------------------

enum { TUTWIN_BASE = 0x8c6e68 };

struct game_tutwin {
    void **vtable;  // 0x0
    GAME_PAD(0x4, 0x444);
    void **buf_vtable;  // 0x444: the Buffer part's
    GAME_PAD(0x448, 0x2144);
    void *icon;  // 0x2144: the picture the popup shows (the unit, base or terrain the note is about), or none
    GAME_PAD(0x2148, 0x2240);
    uint8_t sprite_box[0x34];  // 0x2240: the SpriteBox the picture is set in
    int32_t has_sprite_set;    // 0x2274: non-zero when the popup keeps its pictures in the box and not at 0x2144
    GAME_PAD(0x2278, 0x30bc);
    int32_t width, height;  // 0x30bc: the popup's size, once it has been laid out
    GAME_PAD(0x30c4, 0x3168);
    void *draw_fn;  // 0x3168: what draws the picture (cdecl: picture, window, id, x, y), or none
    GAME_PAD(0x316c, 0x537c);
    int32_t tile_x, tile_y;      // 0x537c: the map square the note is about, or -1 for none
    int32_t target_x, target_y;  // 0x5384: the point on the screen it is about (the arrow's head)
    int32_t tail_x, tail_y;      // 0x538c: where the arrow leaves the popup
    int32_t second_x, second_y;  // 0x5394: the head of the second arrow
    int32_t second_from_x;       // 0x539c: where the second arrow leaves the popup (-1 for no second arrow)
    int32_t second_from_y;       // 0x53a0
    int32_t arrows_on;           // 0x53a4: the arrows are drawn (the window is up)
    int32_t shown;               // 0x53a8: a note has been shown (the game reads it as 0x8cc210)
    int32_t subject;             // 0x53ac: the unit the note is about (0 up), a base (below -1, as -2 less its number), or -1
    GAME_PAD(0x53b0, 0x53b4);
    int32_t answer;       // 0x53b4: 0 once the note has been answered with the button that ends the tour
    int32_t mode;         // 0x53b8: how the note was asked for while it is up, else 0
    int32_t button;       // 0x53bc: what ended the last note (a button's id, a negative one less one), and so whether a tour follows
    int32_t tour;         // 0x53c0: which tour is on (the game sets it by address as 0x8cc228)
    int32_t key_pending;  // 0x53c4: a key ended the note and is to be handed to the console
    int32_t key, key_b;   // 0x53c8: the key's two arguments
    uint32_t flags_high;  // 0x53d0: the high half of the mode, which the answer adds to the words at 0x9a64bc
    void *win;            // 0x53d4: the window the arrows are drawn in while the note is up
};
GAME_AT(struct game_tutwin, buf_vtable, 0x444);
GAME_AT(struct game_tutwin, icon, 0x2144);
GAME_AT(struct game_tutwin, sprite_box, 0x2240);
GAME_AT(struct game_tutwin, has_sprite_set, 0x2274);
GAME_AT(struct game_tutwin, width, 0x30bc);
GAME_AT(struct game_tutwin, draw_fn, 0x3168);
GAME_AT(struct game_tutwin, tile_x, 0x537c);
GAME_AT(struct game_tutwin, target_x, 0x5384);
GAME_AT(struct game_tutwin, tail_x, 0x538c);
GAME_AT(struct game_tutwin, second_x, 0x5394);
GAME_AT(struct game_tutwin, second_from_x, 0x539c);
GAME_AT(struct game_tutwin, arrows_on, 0x53a4);
GAME_AT(struct game_tutwin, shown, 0x53a8);
GAME_AT(struct game_tutwin, subject, 0x53ac);
GAME_AT(struct game_tutwin, answer, 0x53b4);
GAME_AT(struct game_tutwin, mode, 0x53b8);
GAME_AT(struct game_tutwin, button, 0x53bc);
GAME_AT(struct game_tutwin, tour, 0x53c0);
GAME_AT(struct game_tutwin, key_pending, 0x53c4);
GAME_AT(struct game_tutwin, key, 0x53c8);
GAME_AT(struct game_tutwin, flags_high, 0x53d0);
GAME_AT(struct game_tutwin, win, 0x53d4);
_Static_assert(sizeof(struct game_tutwin) == 0x53d8, "layout of the game's TutWin");

// The vtables the constructor sets (the window's and the Buffer's), and the slots of the window's the class uses (byte offsets).
enum { VTABLE_WIN = 0x66dd84, VTABLE_BUF = 0x66dd7c };
enum { VT_HIDE = 0x8, VT_RELEASE_MODAL = 0xe8, VT_WIN_REDRAW = 0xfc };

// The game's data it reads (by address).
enum {
    PREFERENCES = 0x9a6490,   // the player's settings: bit 0x20 shows the tutorial's notes
    SEEN_A = 0x9a64ac,        // the notes shown, as bits (tut_check)
    SEEN_B = 0x9a64b0,        // and the second set (tut_check2)
    ANSWERED = 0x9a64bc,      // what the answers to the notes add up to
    HELD = 0x8cc244,          // set while a note's mode has the high bits asked for (see tut_win), cleared with the state
    HELD_ANSWER = 0x8cc248,   // set by a button that ends a note which was held
    POPUP_FILE = 0x691b14,    // char *: the file the popup's text is read from
    COLOR_ARROW = 0x68a5a0,   // the arrows' pen
    ICON_FIGURE = 0x7777a0,   // the sprites the first layout is made with
    ICON_TERRAIN = 0x798c40,
    CONSOLE = 0x939444,       // the object that takes a key the note did not
    PICTURE_TERRAIN = 0x4bdf20,  // the three picture callbacks as the object holds them: the game's own addresses (hooked, so they are ours)
    PICTURE_VEH = 0x4bdf70,
    PICTURE_BASE = 0x4bdfa0,
    WIN_BASE = 0x6a7628,      // the game's screens the tour points at (the base screen, the interface, the social and the design screens)
    WIN_IFACE = 0x7ae820,
    WIN_SOC = 0x8a6270,
    WIN_DES = 0x71f2b0,
};
// Entries of the labels file: the OK and the Cancel button's text.
enum { LABEL_OK_NOTE = 0xdb, LABEL_OK = 0xec, LABEL_CANCEL = 0xeb };

static struct game_tutwin *the_tutwin(void) { return game_at(TUTWIN_BASE); }
static int32_t *word_at(uint32_t addr) { return game_at(addr); }

static void *map_win_object(void) { return game_map_wins[0]; }
// The map window's Win part (it is a virtual base: the vbtable says how far in), or none.
static void *map_win_part(void) {
    void *w = map_win_object();
    return w ? (uint8_t *)w + ((int32_t *)*(void **)w)[1] : NULL;
}
// A field of the map window by offset.
enum { MAP_VIEW_X = 0x1dda4, MAP_VIEW_Y = 0x1dda8, MAP_PIX_X = 0x1ddbc, MAP_PIX_Y = 0x1ddc0, MAP_SIZE_XA = 0x1ddcc, MAP_SIZE_YA = 0x1ddd0,
       MAP_SIZE_XB = 0x1ddd4, MAP_SIZE_YB = 0x1ddd8 };
static int32_t map_field(void *w, int off) { return *(int32_t *)((uint8_t *)w + off); }

static void hide(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_HIDE); }
static void release_modal(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_RELEASE_MODAL); }
static void redraw_win(void *w) { UI_VCALL(void(UI_THIS *)(void *), w, VT_WIN_REDRAW); }

// ---- The tutorial's flags ---------------------------------------------------------------------------------

// A note that is due is marked shown. Note the word of seen bits it asks (tut_check and tut_check2 differ only in that).
static int note_due(uint32_t seen_addr, int bit) {
    const struct faction_def *def = &game_faction_defs[*game_current_faction];
    if (!tutwin_note_due(*game_net_multiplayer, *(uint32_t *)game_at(PREFERENCES), *(uint32_t *)game_at(seen_addr), (uint32_t)bit,
                         (uint32_t)def->rule_flags))
        return 0;
    *(uint32_t *)game_at(seen_addr) |= (uint32_t)bit;
    return 1;
}
static int UI_CDECL tutwin_check(int bit) { return note_due(SEEN_A, bit); }
static int UI_CDECL tutwin_check2(int bit) { return note_due(SEEN_B, bit); }

// ---- Construction ------------------------------------------------------------------------------------------

// What a window with no note up holds: no window, no arrows, no square, no subject, no mode. Both the first lines of the original's
// ctor (written there by hand) and the "forget" the other functions end with.
static void UI_THIS tutwin_clear(struct game_tutwin *tw) {
    tw->win = NULL;
    tw->arrows_on = 0;
    tw->tile_y = -1;
    tw->tile_x = -1;
    tw->second_from_x = -1;
    tw->shown = 0;
    tw->subject = -1;
    tw->mode = 0;
    tw->key_pending = 0;
    *word_at(HELD) = 0;
}

static void *UI_THIS tutwin_ctor(struct game_tutwin *tw) {
    game_popup_ctor(tw);
    tw->vtable = (void **)(uintptr_t)game_va(VTABLE_WIN);
    tw->buf_vtable = (void **)(uintptr_t)game_va(VTABLE_BUF);
    tw->answer = 0;
    tw->tour = 0;
    tw->flags_high = 0;
    tw->win = NULL;
    tw->arrows_on = 0;
    tw->tile_y = -1;
    tw->tile_x = -1;
    tw->second_from_x = -1;
    tw->shown = 0;
    tw->subject = -1;
    tw->mode = 0;
    tw->key_pending = 0;
    *word_at(HELD) = 0;
    return tw;
}

// The destructor is the Popup's: it sets the Popup's vtables and takes its parts down.
static void UI_THIS tutwin_dtor(struct game_tutwin *tw) { game_popup_dtor(tw); }

static void *UI_THIS tutwin_scalar_dtor(struct game_tutwin *tw, int flags) {
    tutwin_dtor(tw);
    if (flags & 1) game_free(tw);
    return tw;
}

// The Buffer's deleting destructor: the Buffer is the window's second part.
static void *UI_THIS tutwin_operator_delete(void *buf, int flags) {
    return tutwin_scalar_dtor((struct game_tutwin *)((uint8_t *)buf - 0x444), flags);
}

// ---- Taking the note down -----------------------------------------------------------------------------------

// What every way of ending a note does first: the arrows off, the popup hidden, the window it drew in redrawn, and the screen's
// pending drawing done.
static void take_down(struct game_tutwin *tw) {
    tw->arrows_on = 0;
    hide(tw);
    redraw_win(tw->win);
    game_do_all_draws();
}

// A key pressed while a note is up: it ends the note, and is kept for the console unless the note was not a tour's.
static int UI_THIS tutwin_on_key_click(struct game_tutwin *tw, int a, int key) {
    if (!tw->mode) return 0;
    release_modal(tw);
    if (tw->win) take_down(tw);
    tutwin_clear(tw);
    if (key == 0x1b) tw->answer = 0;
    tw->key_pending = 1;
    tw->key = a;
    tw->key_b = key;
    return 1;
}

// The centre of a rectangle, which both of the original's one-line helpers (TutWin_UNK2 and the *_rect functions) compute
// the same way.
static void UI_THIS tutwin_rect_centre(void *unused, const int32_t *rect, int32_t *x, int32_t *y) {
    (void)unused;
    *x = tutwin_middle(rect[0], rect[2]);
    *y = tutwin_middle(rect[1], rect[3]);
}

// A rectangle of one of the game's screens, as the point of its centre on the screen.
static void rect_on(const void *win, const int32_t *rect, int32_t *x, int32_t *y) {
    *x = tutwin_middle(rect[0], rect[2]);
    *y = tutwin_middle(rect[1], rect[3]);
    win_client_to_screen_2((struct game_win *)win, x, y);
}
static void UI_THIS tutwin_base_rect(void *unused, const int32_t *rect, int32_t *x, int32_t *y) { (void)unused, rect_on(game_at(WIN_BASE), rect, x, y); }
static void UI_THIS tutwin_iface_rect(void *unused, const int32_t *rect, int32_t *x, int32_t *y) { (void)unused, rect_on(game_at(WIN_IFACE), rect, x, y); }
static void UI_THIS tutwin_soc_rect(void *unused, const int32_t *rect, int32_t *x, int32_t *y) { (void)unused, rect_on(game_at(WIN_SOC), rect, x, y); }
static void UI_THIS tutwin_des_rect(void *unused, const int32_t *rect, int32_t *x, int32_t *y) { (void)unused, rect_on(game_at(WIN_DES), rect, x, y); }

static int UI_THIS tutwin_tut_win(struct game_tutwin *tw, void *win, char *label, int x, int y, void *sprite, int mode, int rx, int ry);

// A note about a rectangle of one of the game's screens: the arrow points to the rectangle's centre.
static int note_on(struct game_tutwin *tw, const void *screen, const int32_t *rect, char *label, int mode) {
    int x = tutwin_middle(rect[0], rect[2]), y = tutwin_middle(rect[1], rect[3]);
    win_client_to_screen_2((struct game_win *)screen, &x, &y);
    return tutwin_tut_win(tw, map_win_part(), label, x, y, NULL, mode, -1, -1);
}
static int UI_THIS tutwin_do_base(struct game_tutwin *tw, const int32_t *rect, char *label, int mode) { return note_on(tw, game_at(WIN_BASE), rect, label, mode); }
static int UI_THIS tutwin_do_iface(struct game_tutwin *tw, const int32_t *rect, char *label, int mode) { return note_on(tw, game_at(WIN_IFACE), rect, label, mode); }
static int UI_THIS tutwin_do_soc(struct game_tutwin *tw, const int32_t *rect, char *label, int mode) { return note_on(tw, game_at(WIN_SOC), rect, label, mode); }
static int UI_THIS tutwin_do_des(struct game_tutwin *tw, const int32_t *rect, char *label, int mode) { return note_on(tw, game_at(WIN_DES), rect, label, mode); }

// ---- Buttons and the window's own calls ----------------------------------------------------------------------

// A button of the popup was pressed (id above 0 a button; 0 and the negatives are the ways the tour asks the note to end). The note
// is taken down, what the button was is kept (a negative one less one, so that -1 is nothing and only the others go on to a tour),
// and a positive one marks the note answered, or the held one's flag.
static int UI_THIS tutwin_on_button_clicked(struct game_tutwin *tw, int id) {
    int held = *word_at(HELD);
    if (tw->mode) release_modal(tw);
    if (tw->win) take_down(tw);
    tutwin_clear(tw);
    tw->button = id < 0 ? id + 1 : id;
    if (id < -1) tw->answer = 0;
    if (id > 0) {
        if (tw->answer == 0) {
            if (held) *word_at(HELD_ANSWER) = 1;
        } else {
            tw->answer = 0;
        }
        if (tw->flags_high) *(uint32_t *)word_at(ANSWERED) |= tw->flags_high;
    }
    return id;
}

// The window the arrows lie over was moved or redrawn.
static void UI_THIS tutwin_on_move(struct game_tutwin *tw, int a, int b) {
    (void)a, (void)b;
    if (tw->win) redraw_win(tw->win);
}

static void UI_THIS tutwin_set_win(struct game_tutwin *tw, void *win) { tw->win = win; }

// Takes the note down if one is up, and forgets it; answers whether there was one.
static int UI_THIS tutwin_reset(struct game_tutwin *tw) {
    if (tw->win) {
        take_down(tw);
        tutwin_clear(tw);
        return 1;
    }
    tutwin_clear(tw);
    return 0;
}

static void draw_arrows(struct game_tutwin *tw) {
    int color = *word_at(COLOR_ARROW);
    win_window_line_raw(tw->win, tw->tail_x, tw->tail_y, tw->target_x, tw->target_y, color, 2, 2);
    if (tw->second_from_x >= 0) win_window_line_raw(tw->win, tw->second_from_x, tw->second_from_y, tw->second_x, tw->second_y, color, 2, 2);
}

void UI_THIS tutwin_draw_arrow(void *obj) {
    struct game_tutwin *tw = obj;
    if (tw->arrows_on) draw_arrows(tw);
}

// ---- What draws the popup's picture ----------------------------------------------------------------------------

// The three functions the popup calls (with the picture, the window it is drawn in, an id and the position) to draw the picture of
// the note's subject: the terrain of the square, a unit, a base. The Buffer is the window's second part; none for no window.
static void *window_buffer(void *win) { return win ? (uint8_t *)win + 0x444 : NULL; }

static int UI_CDECL tutwin_terrain_draw(void *pic, void *win, int id, int x, int y) {
    (void)pic, (void)id;
    struct game_tutwin *tw = the_tutwin();
    return game_terrain_poly(window_buffer(win), 0, 0, *game_current_faction, -1, 0x621, tw->tile_x, tw->tile_y, x, y);
}

static int UI_CDECL tutwin_veh_draw(void *pic, void *win, int id, int x, int y) {
    (void)pic;
    return game_veh_draw(window_buffer(win), id, x, y, 0, 0, 0);
}

// A base's id comes through as 2 less than its number's complement (-2 for the first): the number is the absolute value of
// id plus 2.
static int UI_CDECL tutwin_base_draw(void *pic, void *win, int id, int x, int y) {
    (void)pic;
    int base = id + 2 < 0 ? -2 - id : id + 2;
    return game_base_draw(window_buffer(win), base, x, y, 0, 0);
}

// ---- The note ----------------------------------------------------------------------------------------------

// Sets the picture of the popup: the sprite given, else the one for the subject. The first layout (which only measures the popup)
// is made with the game's stand-in sprites (`stand_in` for a unit or a base, `stand_in_terrain` for a square's terrain) and the id
// 0, the second with a sprite of its own and the subject's id. As the original does, tut_win forgets the note's subject and square
// (the same clearing that ends a note) before it gets here, so only the given sprite is ever used and the three picture callbacks
// below never run in the game's own flow; they are here, and set, as the original has them.
static void set_picture(struct game_tutwin *tw, void *sprite, void *stand_in, void *stand_in_terrain, int final) {
    if (sprite) {
        tw->icon = tw->has_sprite_set == 0 ? sprite : NULL;
        game_sprite_box_sprite(tw->sprite_box, sprite, 0, 0);
        return;
    }
    int subject = tw->subject;
    if (subject >= 0 || subject < -1) {
        tw->icon = tw->has_sprite_set == 0 ? stand_in : NULL;
        game_sprite_box_sprite(tw->sprite_box, stand_in, 0, final ? subject : 0);
        tw->draw_fn = game_at(subject >= 0 ? PICTURE_VEH : PICTURE_BASE);
    } else if (tw->tile_x >= 0) {
        tw->icon = tw->has_sprite_set == 0 ? stand_in_terrain : NULL;
        game_sprite_box_sprite(tw->sprite_box, stand_in_terrain, 0, 0);
        tw->draw_fn = game_at(PICTURE_TERRAIN);
    }
}

static void set_buttons(struct game_tutwin *tw, int mode) {
    if (mode == 0) {
        pop_set_ok_text((struct game_base_pop *)tw, game_label(LABEL_OK_NOTE));
    } else {
        pop_set_ok_text((struct game_base_pop *)tw, game_label(LABEL_OK));
        pop_set_cancel_text((struct game_base_pop *)tw, game_label(LABEL_CANCEL));
    }
}

// Shows a note: `label` is its text in the popup's file; (x, y) the point on the screen it is about and `win` the window the arrow
// is drawn in; `sprite` a picture for the popup or none; `mode` how it is placed (see tutwin_place), with the bits above 0xff
// (and above 0xffff) asking for more, below; (rx, ry), when it is not negative, a second point to draw an arrow to. When a note is
// already up it is taken down first. Answers the tour's "answer" word (see on_button_clicked) once the note has ended.
static int UI_THIS tutwin_tut_win(struct game_tutwin *tw, void *win, char *label, int x, int y, void *sprite, int mode, int rx, int ry) {
    struct game_sprite local;
    sprite_ctor(&local);
    tw->key_pending = 0;
    tw->flags_high = 0;
    if (mode & 0xffff0000) {
        tw->flags_high = (uint32_t)mode & 0xffff0000;
        mode &= 0xffff;
    }
    int kind = mode & 0xff;
    int style = kind == 0 ? 0x4000 : kind == 1 ? 0 : 0x40;
    tw->target_x = x;
    tw->target_y = y;
    int screen_w = *game_win_screen_w, screen_h = *game_win_screen_h;
    game_pop_style_tutorial();
    if (tw->win) take_down(tw);
    tutwin_clear(tw);
    tw->win = win;
    if (mode & 0xff00) {
        *word_at(HELD) = 1;
        mode = kind;
    }
    char *file = *(char **)game_at(POPUP_FILE);
    game_popup_start(tw, file, label, -1, 0, style, 0);
    // (the first layout gives one button below mode 2, the second below mode 1: the original's two tests differ)
    if (mode < 2) {
        pop_set_ok_text((struct game_base_pop *)tw, game_label(LABEL_OK_NOTE));
    } else {
        pop_set_ok_text((struct game_base_pop *)tw, game_label(LABEL_OK));
        pop_set_cancel_text((struct game_base_pop *)tw, game_label(LABEL_CANCEL));
    }
    set_picture(tw, sprite, game_at(ICON_FIGURE), game_at(ICON_TERRAIN), 0);
    game_base_pop_exec_3(tw, 1, 0);

    struct tutwin_place place;
    tutwin_place(&place, mode, screen_w, screen_h, x, y, tw->width, tw->height);
    int popup_x = place.x, popup_y = place.y;
    tw->tail_x = place.tail_x;
    tw->tail_y = place.tail_y;
    if (rx >= 0) {
        tw->second_x = rx;
        tw->second_y = ry;
        tw->second_from_x = tw->tail_x;
        tw->second_from_y = tw->tail_y;
    }
    tw->mode = mode;
    tw->arrows_on = 1;
    draw_arrows(tw);

    game_popup_start(tw, file, label, -1, 0, style, 0);
    set_buttons(tw, mode);
    if (mode != 6) pop_set_loc((struct game_base_pop *)tw, popup_x, popup_y);
    set_picture(tw, sprite, &local, &local, 1);
    game_base_pop_exec_3(tw, 0, 0);
    game_pop_style_normal();
    tw->shown = 1;
    if (mode == 1) {
        int tour = tw->button, pending = tw->key_pending, key = tw->key, key_b = tw->key_b;
        if (tour) game_tutwin_tour(tw);
        if (pending) game_console_on_key_click(game_at(CONSOLE), key, key_b);
    }
    int answer = tw->answer;
    sprite_close(&local);
    return answer;
}

// A note about a square of the map: the map is moved to show the square (and the second one, when it is on the screen), the
// square's place in the window is found, and the note is shown with an arrow to it. `a` is the unit (0 up) or the base (below -1)
// the note is about, or -1; with neither and no sprite the note is about the square's terrain. A square of the second point that
// is not on the screen leaves the second arrow out (its place stays -1).
static int UI_THIS tutwin_tut_map(struct game_tutwin *tw, char *label, int x, int y, int a, void *sprite, int mode, int x2, int y2) {
    int second_x = -1, second_y = -1;
    if (a >= 0) {
        tw->subject = a;
    } else if (!sprite) {
        tw->tile_x = x;
        tw->tile_y = y;
    }
    void *mw = map_win_object();
    game_map_win_focus(mw, x, y);
    int px, py;
    game_map_win_tile_to_pixel(mw, x, y, &px, &py);
    px += map_field(mw, MAP_PIX_X);
    py += map_field(mw, MAP_PIX_Y);
    if (x2 >= 0) {
        int view_y = map_field(mw, MAP_VIEW_Y);
        if (y2 >= view_y && y2 < map_field(mw, MAP_SIZE_YB) + map_field(mw, MAP_SIZE_YA) + view_y) {
            // On a round map a column left of the view is the one at the other edge, and one past its right the same.
            int ex = x2;
            if (!(*game_map_flat & 1)) {
                int view_x = map_field(mw, MAP_VIEW_X);
                if (ex < view_x) ex += *game_map_width;
                if (ex >= map_field(mw, MAP_SIZE_XB) + map_field(mw, MAP_SIZE_XA) + view_x) ex -= *game_map_width;
            }
            int view_x = map_field(mw, MAP_VIEW_X);
            if (ex >= view_x && ex < map_field(mw, MAP_SIZE_XB) + map_field(mw, MAP_SIZE_XA) + view_x) {
                game_map_win_tile_to_pixel(mw, x2, y2, &second_x, &second_y);
                second_x += map_field(mw, MAP_PIX_X);
                second_y += map_field(mw, MAP_PIX_Y);
            }
        }
    }
    return tutwin_tut_win(tw, map_win_part(), label, px, py, sprite, mode, second_x, second_y);
}

// In shadow mode a check that asks for a note holds the call (the window builds a popup, which allocates from the game's heap, and
// waits for the player; it answers 0 meanwhile) and it is made when the check is over.
static void replay_tut_map(const struct hook_ui_call *c) {
    const int *a = c->arg;
    tutwin_tut_map((struct game_tutwin *)(intptr_t)a[7], c->has_text & 1 ? (char *)c->text[0] : NULL, a[0], a[1], a[2], (void *)(intptr_t)a[3], a[4], a[5], a[6]);
}

static struct hook_stats stats_tut_map = {"tutwin_tut_map"};
static void *orig_tut_map;

static int UI_THIS shadow_tut_map(struct game_tutwin *tw, char *label, int x, int y, int a, void *sprite, int mode, int x2, int y2) {
    if (!hooks_protecting()) return tutwin_tut_map(tw, label, x, y, a, sprite, mode, x2, y2);
    stats_tut_map.calls++;
    struct hook_ui_call c = {.name = "tut_map", .replay = replay_tut_map, .arg = {x, y, a, (int)(intptr_t)sprite, mode, x2, y2, (int)(intptr_t)tw}};
    if (label) hooks_ui_text(&c, 0, label);
    hooks_ui_hold(&c);
    return 0;
}

// ---- Hooks -------------------------------------------------------------------------------------------------

static const struct hook tutwin_hooks[] = {
    UI_HOOK(0x4ba5f0, tutwin_check, 0x55, 0x8b, 0xec, 0xa1, 0x60, 0xf6, 0x93, 0x00),  // push ebp; mov ebp, esp; mov eax, [0x93f660]
    UI_HOOK(0x4ba650, tutwin_check2, 0x55, 0x8b, 0xec, 0xa1, 0x60, 0xf6, 0x93, 0x00),
    // push esi; mov esi, ecx; call the Popup's constructor
    UI_HOOK_CALL(0x4ba6b0, tutwin_ctor, 0x56, 0x8b, 0xf1, 0xe8, 0xe8, 0xa1, 0xf4, 0xff),
    UI_HOOK(0x4ba720, tutwin_clear, 0x33, 0xc0, 0x83, 0xca, 0xff),  // xor eax, eax; or edx, -1
    UI_HOOK(0x4ba770, tutwin_on_key_click, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    UI_HOOK(0x4ba830, tutwin_rect_centre, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x8b, 0x31),
    UI_HOOK(0x4ba870, tutwin_do_base, 0x55, 0x8b, 0xec, 0x51, 0x56, 0x57, 0x8b, 0xf9),
    UI_HOOK(0x4ba8f0, tutwin_do_iface, 0x55, 0x8b, 0xec, 0x51, 0x56, 0x57, 0x8b, 0xf9),
    UI_HOOK(0x4ba970, tutwin_do_soc, 0x55, 0x8b, 0xec, 0x51, 0x56, 0x57, 0x8b, 0xf9),
    UI_HOOK(0x4ba9f0, tutwin_do_des, 0x55, 0x8b, 0xec, 0x51, 0x56, 0x57, 0x8b, 0xf9),
    UI_HOOK(0x4bc5a0, tutwin_iface_rect, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x57),
    UI_HOOK(0x4bc5f0, tutwin_base_rect, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x57),
    UI_HOOK(0x4bc640, tutwin_soc_rect, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x57),
    UI_HOOK(0x4bc690, tutwin_des_rect, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08, 0x56, 0x57),
    UI_HOOK(0x4bdcc0, tutwin_on_button_clicked, 0x55, 0x8b, 0xec, 0x53, 0x8b, 0x1d, 0x44, 0xc2, 0x8c, 0x00),
    UI_HOOK(0x4bddb0, tutwin_on_move, 0x8b, 0x89, 0xd4, 0x53, 0x00, 0x00, 0x85, 0xc9),  // mov ecx, [ecx+0x53d4]; test ecx, ecx
    UI_HOOK(0x4bddd0, tutwin_set_win, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    UI_HOOK(0x4bdde0, tutwin_reset, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    UI_HOOK(0x4bdea0, tutwin_draw_arrow, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xa4, 0x53, 0x00, 0x00),
    UI_HOOK(0x4bdf20, tutwin_terrain_draw, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x85, 0xc0),
    UI_HOOK(0x4bdf70, tutwin_veh_draw, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x85, 0xc0),
    UI_HOOK(0x4bdfa0, tutwin_base_draw, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x10, 0x8d, 0x41, 0x02),
    UI_HOOK(0x4bdfe0, tutwin_tut_win, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xe8, 0x9c, 0x65, 0x00),  // push -1; push handler
    // tut_map: the machinery's own entry, whose shadow side is the held call
    {0x4be650, (const uint8_t[]){0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c}, 6, (void *)tutwin_tut_map, (void *)shadow_tut_map, &orig_tut_map,
     &stats_tut_map},
    UI_HOOK(0x4be7d0, tutwin_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xcd, 0x9c, 0x65, 0x00),
    // push ebp; mov ebp, esp; push esi; mov esi, ecx; call the destructor
    UI_HOOK_CALL(0x4be9f0, tutwin_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0xe8, 0xd5, 0xfd, 0xff, 0xff),
    UI_HOOK(0x4bea20, tutwin_operator_delete, 0x81, 0xe9, 0x44, 0x04, 0x00, 0x00),  // sub ecx, 0x444 (then a jump to the scalar destructor)
};

struct hook_module tutwin_module = {"tutwin", tutwin_hooks, UI_HOOK_COUNT(tutwin_hooks), NULL, NULL, -1};
