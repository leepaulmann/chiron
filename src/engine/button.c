// The game's button classes (OpenSMACX's and Thinker's BaseButton and its relatives): a button
// is a Win with a Buffer behind it; a ButtonGroup keeps at most one of its buttons selected; a
// CheckBox and a RadioButton keep their items in the Dialog class's list, which they hold as a
// virtual base, so their functions find the Win and the list through the object's vbtable. The
// layouts are in game.h.
//
// Here are the functions that change a button's or group's state and tell the parent about it,
// the defaults a new button takes, and the CheckBox, RadioButton and Dialog list functions. Where
// the original calls through a vtable (a screen subclasses a button's parent to hear of clicks;
// the redraw is slot 0xf8) or through a function pointer a screen set, so does this.
//
// Drawing: a button draws into its own Buffer after taking what is behind it (GraphicWin_fill, which
// belongs to the Win class but is only called by the buttons and a few more; it is here because
// win.c is another file's). ImageButton's, PushButton's, FlatButton's and CheckButton's redraws are
// here, and the CheckBox's and RadioButton's layout (calculate_dimensions), item drawing (draw_item)
// and redraw, whose text comes from the Dialog's string list (the list box's are in scroll.c).
//
// Left to the original: the constructors and destructors (they build objects on the game's heap
// and keep an exception frame), set_name, set_bubble_text and init (they copy text onto the heap),
// everything that starts or stops a Time object (a multimedia timer, which the journal can't put
// back): left_down, left_up, mouse_move, timer_callback and its daemon, and right_click; the
// FlatButton's mouse functions (they start the bubble's timer) and the CheckBox's and RadioButton's
// on_left_down and on_key_down.
//
// Differences from OpenSMACX and Thinker: neither lays the CheckBox or RadioButton out. The
// functions the inventory calls CheckBox_item and the like are the Dialog list's own with a
// different virtual base, and this file's names for the CheckBox ones say what they do:
// CheckBox_UNK1 flips the state at a position, CheckBox_UNK2 reads it. A group click returns 0
// whatever happens, as the original does, so a button in a group never tells its parent itself.
// The fields at 0xa94 and 0xa98 are colours (the focus frame's), not fonts as OpenSMACX has them;
// the three Fonts a new button's Buffer gets are the ones set_def_font keeps.
//
// Checks. The functions that only touch the object and call the redraw slot are checked as they
// are called, on the memory they write. The ones that call a parent's handler or a function a
// screen gave the button have effects no check can put back (a handler may open a window), so
// the game's calls of them run the original in shadow mode and the sweep checks them on scratch
// buttons and parents in static memory whose vtables log the calls. Each check lists the memory
// its function can change, and a plant that writes nothing, or a wrong value, shows as a
// difference. A redraw is checked as it is called (for a Buffer with a DIB) on the Buffer's
// object and pixels, the device contexts text goes through, and the globals text leaves; the
// sweep draws on hand-built Buffers and sprites in static memory, and a real Buffer with a
// device context and a font for the names.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "bufobj.h"
#include "bufimg.h"
#include "button.h"
#include "game.h"
#include "hooks.h"
#include "shadow.h"
#include "sprite.h"
#include "strlist.h"
#include "text.h"
#include "win.h"

#define BUTTON (&button_module)
extern struct hook_module button_module;

// ---- Calls through a vtable --------------------------------------------------------------------

// What the button and window classes put at these slots of a vtable (byte offsets).
enum {
    VT_CAPTURE_LOST = 0x10,  // the window lost the mouse capture
    VT_SOUND_PLAY = 0x1c,    // a sound object: start it
    VT_MOUSE_MOVE = 0x44,
    VT_SOUND_BUSY = 0x5c,    // a sound object: whether it is playing
    VT_FLAT_TEXT = 0x14,     // the parent: draw a FlatButton's name (button, name, id), or answer 0
    VT_WIN_BACK = 0x28,      // the parent: draw a box's background (its Win), or answer 0
    VT_ITEM_BACK = 0x2c,     // the parent: draw a box item's background (Win, id, state, rectangle), or answer 0
    VT_BUTTON_CLICK = 0xb0,  // the parent: a button was clicked, with its id
    VT_BUTTON_SET = 0xb4,    // the parent: a button's state was set (id, state)
    VT_GROUP_USER = 0xb8,    // the parent: a group's selection changed (user, old id, new id)
    VT_GROUP_CHANGED = 0xbc, // the parent: the same (old id, new id)
    VT_PARENT_COPY = 0xf4,   // the parent: whether a see-through child may copy from it
    VT_REDRAW = 0xf8,
};
// The sounds a Win keeps: pressed down, up, clicked (at 0x70 to 0x7c in the parent).
enum { SND_DOWN = 0x70, SND_UP = 0x74, SND_CLICK = 0x7c };

static void *vslot(void *o, int slot) { return *(void **)((uint8_t *)*(void ***)o + slot); }
static int vcall0(void *o, int slot) { return ((int(THISCALL *)(void *))vslot(o, slot))(o); }
static int vcall1(void *o, int slot, int a) { return ((int(THISCALL *)(void *, int))vslot(o, slot))(o, a); }
static int vcall2(void *o, int slot, int a, int b) { return ((int(THISCALL *)(void *, int, int))vslot(o, slot))(o, a, b); }
static int vcall4(void *o, int slot, int a, int b, int c, int d) {
    return ((int(THISCALL *)(void *, int, int, int, int))vslot(o, slot))(o, a, b, c, d);
}
static int vcall3(void *o, int slot, int a, int b, int c) {
    return ((int(THISCALL *)(void *, int, int, int))vslot(o, slot))(o, a, b, c);
}

// ---- BaseButton: the defaults a new button takes ------------------------------------------------

// Each sets one of the three numbers the Buffer takes for each of its four colour slots.
static void set_def_text(int which, int a, int b, int c, int d) {
    game_button_defaults->text[0][which] = a;
    game_button_defaults->text[1][which] = b;
    game_button_defaults->text[2][which] = c;
    game_button_defaults->text[3][which] = d;
}
void button_set_def_text_color(int a, int b, int c, int d) { set_def_text(0, a, b, c, d); }
void button_set_def_text_color2(int a, int b, int c, int d) { set_def_text(1, a, b, c, d); }
void button_set_def_text_color3(int a, int b, int c, int d) { set_def_text(2, a, b, c, d); }

// The Fonts a new button's init gives its Buffer: the first is kept only if it has a GDI font
// (at 0x8); the other two always. 3 if there is no first font.
int button_set_def_font(void *font, void *a, void *b) {
    if (!font) return 3;
    if (*(void **)((uint8_t *)font + 8)) game_button_fonts->def = font;
    game_button_fonts->def_a = a;
    game_button_fonts->def_b = b;
    return 0;
}

// ---- BaseButton: state ----------------------------------------------------------------------------

// Set the state; if it changed, redraw the button and tell the parent (the button's id and the state).
void BUTTON_THIS button_set(struct game_button *b, int state) {
    if (state == b->state) return;
    b->state = state;
    vcall0(b, VT_REDRAW);
    if (b->win.parent) vcall2(b->win.parent, VT_BUTTON_SET, b->id, state);
}

// Up becomes down and anything else up, the same way.
void BUTTON_THIS button_toggle(struct game_button *b) {
    int state = b->state == 0;
    if (state == b->state) return;
    b->state = state;
    vcall0(b, VT_REDRAW);
    if (b->win.parent) vcall2(b->win.parent, VT_BUTTON_SET, b->id, state);
}

void BUTTON_THIS button_on_dialog_focus(struct game_button *b, int unused) {
    (void)unused;
    vcall0(b, VT_REDRAW);
}

// The mouse left while this button had a press going: let it know, as a move.
void BUTTON_THIS button_on_mouse_leave(struct game_button *b, int x, int y) {
    struct game_win *w = *game_win_press1;
    if (!w) w = *game_win_press2;
    if (w == &b->win && *game_win_press_inside == 1) vcall2(b, VT_MOUSE_MOVE, x, y);
}

// A sound object on the parent, started unless it is already playing.
static void play(struct game_win *parent, int where) {
    void *snd = *(void **)((uint8_t *)parent + where);
    if (snd && !vcall0(snd, VT_SOUND_BUSY)) vcall0(snd, VT_SOUND_PLAY);
}

// A click: the mouse capture and hover are let go, and unless the button is disabled the globals'
// functions for the ids -1 and -2 are called, the click sound plays, and the group (or else the
// parent) is told. The button's own function, if it has one, is called last, with the id, also
// when it is disabled. The group's click answers 0 whatever happens, so a button in a group
// never tells its parent itself (the group does).
void BUTTON_THIS button_on_left_click(struct game_button *b, int x, int y) {
    (void)x, (void)y;
    if (*game_win_capture == &b->win) {
        *game_win_capture = NULL;
        vcall0(b, VT_CAPTURE_LOST);
    }
    if (*game_win_hover == &b->win) *game_win_hover = NULL;
    *game_win_cur = b->win.parent;
    if (!b->disabled) {
        int id = b->id;
        if (id == -1) {
            if (game_button_hooks[0]) game_button_hooks[0]();
        } else if (id == -2) {
            if (game_button_hooks[1]) game_button_hooks[1]();
        }
        play(b->win.parent, SND_CLICK);
        if (!b->group || button_group_click(b->group, id)) vcall1(b->win.parent, VT_BUTTON_CLICK, id);
    }
    if (b->on_click) b->on_click(b->id);
}

// The Buffer's text colours, once the button has a parent (and so a palette to sync to).
static void set_text(struct game_button *b, void(THISCALL *set)(struct game_buffer *, int, int, int, int), int a, int c, int d,
                     int e) {
    if (!b->win.parent) return;
    buffer_sync_to_palette(&b->buf, *game_palette_copy);
    set(&b->buf, a, c, d, e);
}
void BUTTON_THIS button_set_text_color(struct game_button *b, int a, int c, int d, int e) {
    set_text(b, buffer_set_text_color, a, c, d, e);
}
void BUTTON_THIS button_set_text_color2(struct game_button *b, int a, int c, int d, int e) {
    set_text(b, buffer_set_text_color2, a, c, d, e);
}
void BUTTON_THIS button_set_text_color3(struct game_button *b, int a, int c, int d, int e) {
    set_text(b, buffer_set_text_color3, a, c, d, e);
}

// ---- ButtonGroup ---------------------------------------------------------------------------------------

static void group_clear(struct game_button_group *g) {
    g->count = 0;
    g->user = NULL;
    g->silent = 0;
    memset(g->buttons, 0, sizeof g->buttons);
}
// (A group's selection and flags are not touched by the constructor or close.)
void *BUTTON_THIS button_group_ctor(struct game_button_group *g) {
    group_clear(g);
    return g;
}
int BUTTON_THIS button_group_close(struct game_button_group *g) {
    group_clear(g);
    return 0;
}
// A fresh group takes its owner's number and the flags, with nothing selected (-2). Returns the number.
int BUTTON_THIS button_group_init(struct game_button_group *g, int user, int flags) {
    group_clear(g);
    g->user = (void *)(intptr_t)user;
    g->sel = -2;
    g->flags = flags;
    return user;
}
// Add a button, which learns its group. Returns the button.
int BUTTON_THIS button_group_add(struct game_button_group *g, struct game_button *b) {
    g->buttons[g->count] = b;
    b->group = g;
    g->count++;
    return (int)(intptr_t)b;
}
// Select by id; the second argument says whether the parent is told (with 0, `silent` is raised
// for the click).
int BUTTON_THIS button_group_set(struct game_button_group *g, int id, int notify) {
    if (!notify) g->silent = 1;
    button_group_click(g, id);
    g->silent = 0;
    return 0;
}

// A click on the button with this id. The currently selected button and the clicked one are
// found by id (the last of each, if two share one). With flag 2 a click on the selected one
// deselects it; with neither flag a click on it again changes nothing at all. The old one is set
// to 0 and redrawn, the new one set to 1 and redrawn, and the selection kept; then, unless silent,
// the first button's parent is told twice, of the ids selected before and after (-1 for none): by
// slot 0xbc, and by 0xb8 with the group's number. Last, every button that was marked changed has
// the mark cleared and is redrawn. Always answers 0.
int BUTTON_THIS button_group_click(struct game_button_group *g, int id) {
    int have_cur = 0, have_new = 0, cur_i = 0, new_i = 0;
    int sel = g->sel;
    for (int i = 0; i < g->count; i++) {
        int bid = g->buttons[i]->id;
        if (bid == sel) cur_i = i, have_cur = 1;
        if (bid == id) new_i = i, have_new = 1;
    }
    int flags = g->flags;
    if (flags & 2) {
        if (g->sel == id) {
            g->sel = -1;
            have_new = 0;
        } else {
            goto apply;
        }
    }
    if (g->sel == id && !(flags & 1)) goto done;
apply:
    if (have_cur) {
        button_set(g->buttons[cur_i], 0);
        vcall0(g->buttons[cur_i], VT_REDRAW);
    }
    if (have_new) {
        button_set(g->buttons[new_i], 1);
        vcall0(g->buttons[new_i], VT_REDRAW);
        g->sel = id;
    } else {
        g->sel = -1;
    }
    if (!g->silent && g->count) {
        int nid = have_new ? g->buttons[new_i]->id : -1;
        int cid = have_cur ? g->buttons[cur_i]->id : -1;
        vcall2(g->buttons[0]->win.parent, VT_GROUP_CHANGED, cid, nid);
        if (g->count) {
            nid = have_new ? g->buttons[new_i]->id : -1;
            cid = have_cur ? g->buttons[cur_i]->id : -1;
            vcall3(g->buttons[0]->win.parent, VT_GROUP_USER, (int)(intptr_t)g->user, cid, nid);
        }
    }
done:
    for (int i = 0; i < g->count; i++) {
        struct game_button *b = g->buttons[i];
        if (b->changed) {
            b->changed = 0;
            vcall0(b, VT_REDRAW);
        }
    }
    return 0;
}

// ---- The Dialog list ---------------------------------------------------------------------------------

// Find the item with this id from the head: `cur` and `idx` are left at it, or after a full turn
// of the ring if there is none. With no items nothing is touched.
static void dlg_seek(struct game_dlg *d, int id) {
    if (!d->head) return;
    int n = d->count;
    d->idx = 0;
    d->cur = d->head;
    for (int i = 0; i < n; i++) {
        if (d->cur->key == id) break;
        d->idx++;
        d->cur = d->cur->next;
    }
}
// Go to a position (back from the head if it is negative) and answer the id there. A position
// past the end, or further back than a full turn, leaves `cur` at the head and `idx` as it was.
static int dlg_walk(struct game_dlg *d, int pos) {
    if (pos <= d->count - 1) {
        d->cur = d->head;
        if (pos < 0) {
            int n = pos < 0 ? (int)(0u - (uint32_t)pos) : pos;
            if (n <= d->count) {
                if (n > 0) {
                    do d->cur = d->cur->prev;
                    while (--n != 0);
                }
                d->idx = d->count + pos;
            }
        } else {
            if (pos > 0) {
                for (int n = pos; n != 0; n--) d->cur = d->cur->next;
            }
            d->idx = pos;
        }
    }
    return d->head ? d->cur->key : 0;
}
void BUTTON_THIS dialog_set_selected_id(struct game_dlg *d, int id) {
    dlg_seek(d, id);
    d->sel = d->idx;
}
int BUTTON_THIS dialog_get_selected_id(struct game_dlg *d) { return dlg_walk(d, d->sel); }
int BUTTON_THIS dialog_id_to_pos(struct game_dlg *d, int id) {
    dlg_seek(d, id);
    return d->idx;
}
int BUTTON_THIS dialog_pos_to_id(struct game_dlg *d, int pos) { return dlg_walk(d, pos); }

// ---- CheckBox and RadioButton -----------------------------------------------------------------------

// A CheckBox's object starts with a pointer to its vbtable: the entry at 0x4 is where the Win is,
// the one at 0x8 where the Dialog is. Its `sel` is a mask of the checked positions.
static int32_t *vbtable(void *cb) { return *(int32_t **)cb; }
static struct game_dlg *cb_dlg(void *cb) { return (struct game_dlg *)((uint8_t *)cb + vbtable(cb)[2]); }
static void *cb_win(void *cb) { return (uint8_t *)cb + vbtable(cb)[1]; }
static uint32_t bit(int pos) { return 1u << (pos & 31); }

void BUTTON_THIS checkbox_flip_state_id(void *cb, int id) {
    struct game_dlg *d = cb_dlg(cb);
    dlg_seek(d, id);
    int was = (d->sel & bit(d->idx)) != 0;
    dlg_seek(d, id);  // (the original looks it up again for each branch)
    if (was) d->sel &= ~bit(d->idx);
    else d->sel |= bit(d->idx);
}
void BUTTON_THIS checkbox_flip_state_pos(void *cb, int pos) { cb_dlg(cb)->sel ^= bit(pos); }
void BUTTON_THIS checkbox_set_state_id(void *cb, int id, int on) {
    struct game_dlg *d = cb_dlg(cb);
    dlg_seek(d, id);
    if (on) d->sel |= bit(d->idx);
    else d->sel &= ~bit(d->idx);
}
int BUTTON_THIS checkbox_get_state_id(void *cb, int id) {
    struct game_dlg *d = cb_dlg(cb);
    dlg_seek(d, id);
    return d->sel & bit(d->idx);
}
void BUTTON_THIS checkbox_set_state_pos(void *cb, int pos, int on) {
    struct game_dlg *d = cb_dlg(cb);
    if (on) d->sel |= bit(pos);
    else d->sel &= ~bit(pos);
}
int BUTTON_THIS checkbox_get_state_pos(void *cb, int pos) { return cb_dlg(cb)->sel & bit(pos); }
// All the states at once, and a redraw.
void BUTTON_THIS checkbox_set_state_flag(void *cb, int mask) {
    cb_dlg(cb)->sel = mask;
    vcall0(cb_win(cb), VT_REDRAW);
}

// The handlers below are the Win's slots, so they get the Win, which sits 0x1c bytes into a
// CheckBox and 0x18 into a RadioButton; the original adds the vbtable's offsets to the Win's
// address and then a field offset that makes up for it, which this does too.
#define VB(w, disp) (*(int32_t **)((uint8_t *)(w) - (disp)))
#define AT(w, off) ((uint8_t *)(w) + (off))

// A double click: the parent becomes the window being worked on, and the box's function, if it has
// one (at 0xc in a CheckBox), is called with the mask of checked items.
void BUTTON_THIS checkbox_on_left_double_click(void *w, int x, int y) {
    (void)x, (void)y;
    int32_t *vb = VB(w, 0x1c);
    *game_win_cur = *(struct game_win **)AT(w, vb[1] + 0xa8);
    void(*fn)(int) = *(void(**)(int))AT(w, -0x10);
    if (fn) fn(*(int32_t *)AT(w, vb[2] + 0xd0));
}
// The same for a RadioButton, whose function (at 0x10) takes the id of the selected item.
void BUTTON_THIS radio_on_left_double_click(void *w, int x, int y) {
    (void)x, (void)y;
    int32_t *vb = VB(w, 0x18);
    *game_win_cur = *(struct game_win **)AT(w, vb[1] + 0xac);
    void(*fn)(int) = *(void(**)(int))AT(w, -0x8);
    if (fn) fn(dialog_get_selected_id((struct game_dlg *)(AT(w, -0x18) + vb[2])));
}
void BUTTON_THIS checkbox_on_dialog_focus(void *w, int unused) {
    (void)unused;
    vcall0((uint8_t *)w - 0x1c + VB(w, 0x1c)[1], VT_REDRAW);
}
void BUTTON_THIS radio_on_dialog_focus(void *w, int unused) {
    (void)unused;
    vcall0((uint8_t *)w - 0x18 + VB(w, 0x18)[1], VT_REDRAW);
}
// The mouse left: no item is under it any more, and the box is redrawn.
void BUTTON_THIS checkbox_on_mouse_leave(void *w, int x, int y) {
    (void)x, (void)y;
    *(int32_t *)AT(w, VB(w, 0x1c)[2] + 0xd4) = -1;
    vcall0((uint8_t *)w - 0x1c + VB(w, 0x1c)[1], VT_REDRAW);
}
void BUTTON_THIS radio_on_mouse_leave(void *w, int x, int y) {
    (void)x, (void)y;
    *(int32_t *)AT(w, VB(w, 0x18)[2] + 0xd8) = -1;
    vcall0((uint8_t *)w - 0x18 + VB(w, 0x18)[1], VT_REDRAW);
}
// The item under the mouse, by the grid the box lays its items out in (a cell is the item's width
// or height and the gap at 0x44; columns at 0x64 and rows at 0x68 wide; 0x58 cells to a column).
// If that item's bit is set in the mask the box keeps at 0xec the hover is cleared (and nothing
// is redrawn if it was clear already); otherwise it is the item.
void BUTTON_THIS checkbox_on_mouse_move(void *w, int x, int y) {
    int32_t *vb = VB(w, 0x1c);
    uint8_t *e = AT(w, vb[2]);
    int gap = *(int32_t *)(e + 0x28);
    int cell = (x / (*(int32_t *)(e + 0x48) + gap)) * *(int32_t *)(e + 0x3c) + y / (*(int32_t *)(e + 0x4c) + gap);
    int32_t *hover = (int32_t *)(e + 0xd4);
    if (bit(cell) & *(uint32_t *)(e + 0xd0)) {
        if (*hover == -1) return;
        *hover = -1;
    } else {
        *hover = cell;
    }
    vcall0((uint8_t *)w - 0x1c + vb[1], VT_REDRAW);
}

// ---- CheckButton ------------------------------------------------------------------------------------

// A click turns it over, redraws, calls its function and the parent's slot 0xb0 with its id, and
// redraws again. Without a parent nothing happens.
void BUTTON_THIS checkbtn_on_left_click(struct game_check_button *c, int x, int y) {
    (void)x, (void)y;
    struct game_win *parent = c->win.parent;
    if (!parent) return;
    c->state = c->state == 0;
    vcall0(c, VT_REDRAW);
    if (c->on_click) c->on_click(c->id);
    vcall1(parent, VT_BUTTON_CLICK, c->id);
    vcall0(c, VT_REDRAW);
}
void BUTTON_THIS checkbtn_on_left_double_click(struct game_check_button *c, int x, int y) {
    (void)x, (void)y;
    if (c->on_double_click) c->on_double_click(c->id);
}

// ---- Drawing -------------------------------------------------------------------------------------------
//
// A button draws into its own Buffer, which its parent copies from afterwards; it begins by taking
// what is behind it (GraphicWin_fill). The Buffer sits at 0x444 in a Win.

static struct game_buffer *win_buf(void *w) { return (struct game_buffer *)((uint8_t *)w + 0x444); }

// Fill a window's Buffer: copy from the parent's where the window is see-through (flag 0x80000)
// and the parent's slot 0xf4 says it can be, darkened by the table the game keeps for that if it
// has one; otherwise fill with the colour.
void BUTTON_THIS graphic_win_fill(struct game_win *w, int color) {
    struct game_win *parent = w->parent;
    if ((w->flags & 0x80000) && parent && vcall0(parent, VT_PARENT_COPY)) {
        struct game_buffer *b = win_buf(w);
        buffer_copy_full(win_buf(parent), b, w->outer[0] + w->inner[0], w->outer[1] + w->inner[1], 0, 0, b->width, -b->neg_height);
        if (*game_win_tint) buffer_map_colors(b, 0, 0, b->width - 1, -1 - b->neg_height, *game_win_tint);
        return;
    }
    buffer_fill_all(win_buf(w), color);
}

static void shrink(int32_t *r) { r[0]++, r[1]++, r[2]--, r[3]--; }

// An ImageButton: the fill, a frame in the focus colour (twice if it has the dialog's focus),
// the border in two colours (swapped while pressed), the picture centred (and, if the room left is
// narrower than it, squeezed to the room; pressed, moved by the border's width and given less
// room), and the name centred in what is left (moved by the border's width while pressed).
// Without a window or a parent nothing is drawn.
void BUTTON_THIS image_button_on_redraw(struct game_button *b) {
    if (!*game_win_hwnd) return;
    struct game_win *parent = b->win.parent;
    if (!parent) return;
    *game_win_cur = parent;
    struct game_buffer *buf = &b->buf;
    buffer_sync_to_palette(buf, *game_palette_copy);
    int32_t r[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
    graphic_win_fill(&b->win, b->fill);
    int frames = 0;
    if (b->focus_a != -1) {
        buffer_box_rect(buf, r, b->focus_a, b->focus_a);
        shrink(r);
        if (win_is_dialog_focus(&b->win)) {
            buffer_box_rect(buf, r, b->focus_a, b->focus_a);
            shrink(r);
            frames = 1;
        }
    }
    struct game_sprite *pic = b->picture;
    int px = 0, py = 0;
    if (pic) {
        px = (buf->full[2] - pic->full_w - b->border_w - buf->full[0]) >> 1;
        py = (buf->full[3] - pic->full_h - buf->full[1] - b->border_w) >> 1;
    }
    if (!b->pressed) {
        for (int i = frames; i < b->border_w; i++) {
            buffer_box_rect(buf, r, b->border_a, b->border_b);
            shrink(r);
        }
        if (pic) {
            int room = r[2] - r[0];
            if (room < pic->width) sprite_draw_2(pic, buf, pic->key, px, py, room, pic->width);
            else sprite_draw_2(pic, buf, pic->key, px, py, 1, 1);
        }
    } else {
        for (int i = frames; i < b->border_w; i++) {
            buffer_box_rect(buf, r, b->border_b, b->border_a);
            shrink(r);
        }
        if (pic) {
            int room = r[2] - b->border_w - r[0];
            if (room < pic->width) sprite_draw_2(pic, buf, pic->key, px + b->border_w, py + b->border_w, room, pic->width);
            else sprite_draw_2(pic, buf, pic->key, px + b->border_w, py + b->border_w, 1, 1);
        }
        for (int k = 0; k < 4; k++) r[k] += b->border_w;
    }
    if (b->name) buffer_write_cent_l_7(buf, b->name, r, (int)strlen(b->name));
}

// A CheckButton: what is behind it, then its picture for the state.
void BUTTON_THIS checkbtn_on_redraw(struct game_check_button *c) {
    struct game_win *parent = c->win.parent;
    if (!parent) return;
    struct game_buffer *buf = &c->buf;
    buffer_copy_full(win_buf(parent), buf, c->win.outer[0] - buf->full[0], c->win.outer[1] - buf->full[1], 0, 0, buf->width,
                     -buf->neg_height);
    struct game_sprite *pic = c->state ? c->pic_on : c->pic_off;
    sprite_draw_3(pic, buf, pic->key, 0, 0);
}


// A sprite put at a place, and a tile of a sprite spread over an area.
static void put(struct game_buffer *buf, struct game_sprite *s, int x, int y) { sprite_draw_2(s, buf, s->key, x, y, 1, 1); }
static void tile(struct game_buffer *buf, struct game_sprite *s, int x, int y, int w, int h) { buffer_tile_2(buf, s, 0, 0, x, y, w, h); }

// The frame of a PushButton from the eight pieces for one state, in this order: the four corners
// (top left, top right, bottom left, bottom right), then the edges (left, right, top, bottom), each
// edge tiled between the corners it needs; and the middle, if all eight were drawn. `count` is what the
// original counts as it goes: the top right corner sets it to 2 rather than adding one, as an
// oddity of the original, which changes nothing while all the corners are there.
static void push_frame(struct game_buffer *buf, struct game_sprite *tl, struct game_sprite *tr, struct game_sprite *bl,
                       struct game_sprite *br, struct game_sprite *left, struct game_sprite *right, struct game_sprite *top,
                       struct game_sprite *bottom, struct game_sprite *mid, int pressed) {
    int w = buf->width, nh = buf->neg_height, count = 0;
    if (pressed) {
        if (tl) put(buf, tl, 0, 0), count = 1;
        if (tr) count++, put(buf, tr, w - tr->full_w, 0);
    } else {
        count = 1;
        put(buf, tl, 0, 0);
        if (tr) put(buf, tr, w - tr->full_w, 0), count = 2;
    }
    if (bl) count++, put(buf, bl, 0, -(bl->full_h + nh));
    if (br) count++, put(buf, br, w - br->full_w, -(br->full_h + nh));
    if (left && tl && bl) count++, tile(buf, left, 0, tl->full_h, left->full_w, -(bl->full_h + nh + tl->full_h));
    if (right && tr && br) count++, tile(buf, right, w - right->full_w, tr->full_h, right->full_w, -(br->full_h + nh + tr->full_h));
    if (top && tl && tr) count++, tile(buf, top, tl->full_w, 0, w - tr->full_w - tl->full_w, top->full_h);
    if (bottom && bl && br) count++, tile(buf, bottom, bl->full_w, -(nh + bottom->full_h), w - br->full_w - bl->full_w, bottom->full_h);
    if (mid && count == 8) tile(buf, mid, right->full_w, top->full_h, w - left->full_w - right->full_w, -(bottom->full_h + nh + top->full_h));
}

// A PushButton. With a top left corner sprite it is the frame of sprites (and the text rectangle is
// the Buffer's less the border, moved in by the border's width again when pressed); without one it is
// as an ImageButton's frame and border without a picture, but the focus frames only when the parent is
// not a dialog (and twice if the button has the dialog's focus). Unlike the ImageButton, the window the
// parent makes current is set before the window or parent is looked at.
void BUTTON_THIS push_button_on_redraw(struct game_push_button *p) {
    struct game_button *b = &p->base;
    struct game_win *parent = b->win.parent;
    *game_win_cur = parent;
    if (!*game_win_hwnd) return;
    if (!parent) return;
    struct game_buffer *buf = &b->buf;
    buffer_sync_to_palette(buf, *game_palette_copy);
    int32_t r[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
    graphic_win_fill(&b->win, b->fill);
    int bw = b->border_w;
    if (p->tl) {
        r[0] += bw, r[1] += bw, r[2] -= bw, r[3] -= bw;
        if (!b->pressed) {
            push_frame(buf, p->tl, p->tr, p->bl, p->br, b->picture, p->right, p->top, p->bottom, p->mid, 0);
        } else {
            for (int k = 0; k < 4; k++) r[k] += bw;
            push_frame(buf, p->ptl, p->ptr, p->pbl, p->pbr, p->pleft, p->pright, p->ptop, p->pbottom, p->pmid, 1);
        }
    } else {
        int frames = 0;
        if (b->focus_a != -1) {
            buffer_box_rect(buf, r, b->focus_a, b->focus_a);
            shrink(r);
            if (!(parent->flags & WIN_F_DIALOG) && win_is_dialog_focus(&b->win)) {
                buffer_box_rect(buf, r, b->focus_a, b->focus_a);
                shrink(r);
                frames = 1;
            }
        }
        if (!b->pressed) {
            for (int i = frames; i < bw; i++) buffer_box_rect(buf, r, b->border_a, b->border_b), shrink(r);
        } else {
            for (int i = frames; i < bw; i++) buffer_box_rect(buf, r, b->border_b, b->border_a), shrink(r);
            for (int k = 0; k < 4; k++) r[k] += bw;
        }
    }
    if (b->name) buffer_write_cent_l_7(buf, b->name, r, (int)strlen(b->name));
}

// ---- FlatButton ---------------------------------------------------------------------------------------
//
// A FlatButton looks one of three ways (0 as it lies, 1 changed, 2 pressed or down; the field at
// 0xab8 can force one) and for each has a text colour and, in order of preference, a picture centred
// in it, a frame of the box class, or the eight sprites of a frame (the same pieces and order as a
// PushButton's, see push_frame); with none of them it is a plain border like a PushButton's.

// The name: the parent may draw it (its slot 0x14, with the button, the name and the id); otherwise it is
// wrapped and centred in the rectangle `r` (less 3 on each side) if it fits, or else written from the left
// in what is left after the dots (a string the game keeps) and the dots after it. (OpenSMACX and Thinker
// don't have this function's drawing.)
static void flat_name(struct game_button *b, struct game_buffer *buf, int32_t *r) {
    struct game_win *parent = b->win.parent;
    if (!parent) return;
    if (vcall3(parent, VT_FLAT_TEXT, (int)(intptr_t)b, (int)(intptr_t)b->name, b->id)) return;
    int wide = r[2] - r[0] - 6;
    int h = buffer_wrap_height(buf, b->name, wide);
    h = h - buffer_text_line_height(buf) + buffer_text_height(buf);
    int y = (r[3] - r[1] - h) / 2 + r[1];
    if (y >= 0 && (*(int32_t *)((uint8_t *)buf + 0x18) >= 2 || buffer_text_width(buf, b->name) <= wide)) {
        buffer_wrap_cent_4(buf, b->name, r[0] + 3, y, wide);
        return;
    }
    r[0] += 3, r[2] -= 3;
    r[2] -= buffer_text_width(buf, game_flat_dots_a);
    buffer_set_clip(buf, r);
    if (b->name) buffer_write_l_4(buf, b->name, r, (int)strlen(b->name));
    buffer_set_clip(buf, buf->full);
    int w = r[2] - r[0];
    r[0] += w, r[2] += w;
    buffer_write_l_4(buf, game_flat_dots_b, r, (int)strlen(game_flat_dots_b));
}

void BUTTON_THIS flat_button_on_redraw(struct game_flat_button *f) {
    struct game_button *b = &f->base;
    struct game_win *parent = b->win.parent;
    *game_win_cur = parent;
    if (!*game_win_hwnd) return;
    if (!parent) return;
    struct game_buffer *buf = &b->buf;
    buffer_sync_to_palette(buf, *game_palette_copy);
    int32_t r[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
    graphic_win_fill(&b->win, *game_flat_fill);
    int look = (int)(intptr_t)b->picture;
    if (look < 0 || look >= 3) look = (b->pressed || b->state) ? 2 : b->changed != 0;
    int w = buf->width, nh = buf->neg_height;
    if (f->pic[look]) {
        struct game_sprite *s = f->pic[look];
        sprite_draw_2(s, buf, s->key, (w - s->full_w) / 2, -((s->full_h + nh) / 2), 1, 1);
    } else if (f->frame[look]) {
        buffer_box_sprite(buf, r, f->frame[look]);
    } else if (f->tl[look]) {
        push_frame(buf, f->tl[look], f->tr[look], f->bl[look], f->br[look], f->left[look], f->right[look], f->top[look],
                   f->bottom[look], f->mid[look], 0);
    } else {
        int frames = 0;
        if (b->focus_a != -1) {
            buffer_box_rect(buf, r, b->focus_a, b->focus_a);
            shrink(r);
            if (!(parent->flags & WIN_F_DIALOG) && win_is_dialog_focus(&b->win)) {
                buffer_box_rect(buf, r, b->focus_a, b->focus_a);
                shrink(r);
                frames = 1;
            }
        }
        if (!b->pressed) {
            for (int i = frames; i < b->border_w; i++) buffer_box_rect(buf, r, b->border_a, b->border_b), shrink(r);
        } else {
            for (int i = frames; i < b->border_w; i++) buffer_box_rect(buf, r, b->border_b, b->border_a), shrink(r);
            for (int k = 0; k < 4; k++) r[k] += b->border_w;
        }
    }
    if (!b->name) return;
    if (f->text_color[look] >= 0) button_set_text_color(b, f->text_color[look], -1, 1, 1);
    flat_name(b, buf, r);
}

// ---- CheckBox and RadioButton: layout and drawing ----------------------------------------------------------
//
// The Dialog part of a CheckBox or RadioButton (see game_dlg; DF reads a field by its offset) holds what
// the layout works from and leaves: 0x20 flags (1: the height is asked for, 2: the width), 0x24 and 0x28
// where the box's pixels are in its parent, 0x2c and 0x30 the width and height asked for, 0x34 the width the
// picture takes, 0x40 the margin, 0x44 the gap between items, 0x48 the item height asked for (-1 for the
// pictures'), 0x4c the columns, 0x50 the most rows; then what it works out: 0x54 the columns, 0x58 the rows,
// 0x5c and 0x60 the box's width and height, 0x64 and 0x68 an item's. 0x70 to 0x78 are the Fonts and 0x7c to
// 0xa8 the colours for the Buffer's three colour settings (four numbers each, a setting to a column), 0xb8 the
// focus frame's colour (-1 for none), 0xbc the string list of the items, and 0xf0 the item under the mouse. A
// CheckBox's pictures for unchecked and checked are at 0x10 and 0x14 of the object, a RadioButton's at 0x4
// and 0x8. Neither OpenSMACX nor Thinker has any of this.

#define DF DLG_F
static struct game_strlist *dlg_list(struct game_dlg *d) { return (struct game_strlist *)((uint8_t *)d + 0xbc); }
static struct game_list_item *cur_item(struct game_strlist *l) { return l->head ? (struct game_list_item *)l->cur->item : NULL; }
static char *cur_text(struct game_strlist *l) { return l->head ? cur_item(l)->text : NULL; }
static int cur_lines(struct game_strlist *l) { return l->head ? cur_item(l)->lines : 0; }
static struct game_buffer *box_buf(void *box, int32_t *vb) { return win_buf((uint8_t *)box + vb[1]); }

// The Win's Buffer's text colours and Fonts as the Dialog part says (the CheckBox's, RadioButton's and
// ListBox's layouts all begin this way).
void dlg_text_setup(struct game_buffer *buf, struct game_dlg *d) {
    buffer_set_text_color(buf, DF(d, 0x7c), DF(d, 0x88), DF(d, 0x94), DF(d, 0xa0));
    buffer_set_text_color2(buf, DF(d, 0x80), DF(d, 0x8c), DF(d, 0x98), DF(d, 0xa4));
    buffer_set_text_color3(buf, DF(d, 0x84), DF(d, 0x90), DF(d, 0x9c), DF(d, 0xa8));
    buffer_set_font(buf, (struct font *)(intptr_t)DF(d, 0x70), (struct font *)(intptr_t)DF(d, 0x74),
                    (struct font *)(intptr_t)DF(d, 0x78), NULL);
}

// The width of the picture, and the height of an item, if the Dialog part hasn't them already.
static void dlg_picture_size(struct game_dlg *d, struct game_sprite *a, struct game_sprite *b) {
    if (DF(d, 0x34) == 0 && a && b) DF(d, 0x34) = b->full_w > a->full_w ? b->full_w : a->full_w;
    if (DF(d, 0x48) == -1) DF(d, 0x68) = a && b ? (a->full_h > b->full_h ? a->full_h : b->full_h) : 0;
    else DF(d, 0x68) = DF(d, 0x48);
}

// Every item's text is measured as the cursor goes round the ring (it ends where it began), and the widest
// is left in *widest, which starts as it was. With `wrap` (a RadioButton's) each item's line count is set to
// its width over that, plus one.
void dlg_measure(struct game_buffer *buf, struct game_dlg *d, int wrap, int *widest) {
    struct game_strlist *l = dlg_list(d);
    for (int i = 0; i < d->count; i++) {
        strlist_next_entry(l);
        int w = buffer_text_width(buf, l->cur ? cur_text(l) : NULL);
        if (w > *widest) *widest = w;
        if (wrap && l->head) cur_item(l)->lines = w / wrap + 1;
    }
}

// What follows the item measuring, the same in both: an item's cell and height (the taller of the box's and
// the text's), the box's width, and its height.
static void dlg_finish(struct game_buffer *buf, struct game_dlg *d, int widest) {
    DF(d, 0x64) = DF(d, 0x44) + DF(d, 0x34) + widest;
    int h = DF(d, 0x68), th = buffer_text_height(buf);
    DF(d, 0x68) = h > th ? h : th;
    if ((DF(d, 0x20) & 2) && DF(d, 0x2c)) {
        DF(d, 0x5c) = DF(d, 0x2c);
        DF(d, 0x64) = (DF(d, 0x2c) - 2 * DF(d, 0x40)) / DF(d, 0x4c) - DF(d, 0x44);
    } else {
        int t = DF(d, 0x64) * DF(d, 0x4c) + 2 * DF(d, 0x40), u = 2 * DF(d, 0x40) + DF(d, 0x2c);
        DF(d, 0x5c) = u > t ? u : t;
        DF(d, 0x64) = (DF(d, 0x5c) - 2 * DF(d, 0x40)) / DF(d, 0x4c) - DF(d, 0x44);
        DF(d, 0x5c) -= DF(d, 0x44);
    }
    DF(d, 0x54) = DF(d, 0x4c);
    if ((DF(d, 0x20) & 1) && DF(d, 0x30)) {
        DF(d, 0x60) = DF(d, 0x30);
        DF(d, 0x58) = DF(d, 0x30) / DF(d, 0x68);
    } else {
        DF(d, 0x60) = (DF(d, 0x68) + DF(d, 0x44)) * DF(d, 0x58);
    }
}

// Lay the items out in one column of as many rows as there are. 7 if there are none.
int BUTTON_THIS checkbox_calculate_dimensions(void *cb) {
    int32_t *vb = vbtable(cb);
    struct game_dlg *d = cb_dlg(cb);
    if (!d->count) return 7;
    struct game_buffer *buf = box_buf(cb, vb);
    dlg_text_setup(buf, d);
    struct game_sprite *a = *(struct game_sprite **)((uint8_t *)cb + 0x10), *b = *(struct game_sprite **)((uint8_t *)cb + 0x14);
    dlg_picture_size(d, a, b);
    DF(d, 0x58) = d->count;
    int widest = 0;
    dlg_measure(buf, d, 0, &widest);
    dlg_finish(buf, d, widest);
    return 0;
}

// As a CheckBox's, but in as many columns as the most rows asked for needs (a column is as many items as
// the number of items over the number of columns, and a column more if they don't all fit), and each
// item's line count set from how often its text goes into the width a column leaves (100000 if the
// width isn't asked for).
int BUTTON_THIS radio_calculate_dimensions(void *rb) {
    int32_t *vb = vbtable(rb);
    struct game_dlg *d = cb_dlg(rb);
    if (!d->count) return 7;
    struct game_buffer *buf = box_buf(rb, vb);
    dlg_text_setup(buf, d);
    struct game_sprite *a = *(struct game_sprite **)((uint8_t *)rb + 0x4), *b = *(struct game_sprite **)((uint8_t *)rb + 0x8);
    dlg_picture_size(d, a, b);
    DF(d, 0x58) = d->count < DF(d, 0x50) ? d->count : DF(d, 0x50);
    if (DF(d, 0x58) < d->count) {
        DF(d, 0x4c) = d->count / DF(d, 0x58) + 1;
        DF(d, 0x58) = d->count / DF(d, 0x4c);
        if (DF(d, 0x4c) * DF(d, 0x58) < d->count) DF(d, 0x58)++;
    }
    int wrap = (DF(d, 0x20) & 2) && DF(d, 0x2c) ? DF(d, 0x2c) / DF(d, 0x4c) - 2 * DF(d, 0x40) - DF(d, 0x34) : 100000;
    int widest = 0;
    dlg_measure(buf, d, wrap, &widest);
    dlg_finish(buf, d, widest);
    return 0;
}

// Item `pos`, drawn into the box's Buffer. `hot` is whether it is checked (a CheckBox's) or selected, and
// with it the picture for that. The place: the box's columns run down (as many rows as 0x58), and an
// item that wraps to more lines than the item height holds pushes the ones after it down by the
// difference; the list's cursor is walked to find the ones before it. If the parent doesn't draw the item's
// background (its slot 0x2c, with the Win, the item's id, 0 or 1 for the mouse over it or 2 for hot, and
// the rectangle) the item takes it from the parent's Buffer. Then the picture, the text (on the left of a
// `|`, and what is right of that on the right; wrapped in the item's rectangle if it has more than one
// line), and for a hot item in a window with the dialog's focus a dotted frame. `shift` is what the
// CheckBox moves the text down by, the Font's 0x1c; a RadioButton doesn't.
static void box_draw_item(void *box, int pos, int hot, struct game_sprite *off, struct game_sprite *on, int shift) {
    int32_t *vb = vbtable(box);
    struct game_win *win = (struct game_win *)((uint8_t *)box + vb[1]);
    struct game_win *parent = win->parent;
    if (!parent) return;
    struct game_dlg *d = cb_dlg(box);
    struct game_strlist *l = dlg_list(d);
    struct game_buffer *buf = win_buf(win);
    int extra = 0;
    strlist_seek_pos(l, -1);
    for (int n = pos; n > 0; n--) {
        strlist_next_entry(l);
        if (l->cur && cur_lines(l) > 1) {
            int h = DF(d, 0x68), lines = cur_lines(l);
            int tall = buffer_text_line_height(buf) * lines;
            extra += (h > tall ? h : tall) - DF(d, 0x68);
        }
    }
    int rows = DF(d, 0x58), col = pos / rows;
    int x = (DF(d, 0x64) + DF(d, 0x44)) * col + DF(d, 0x40);
    int y = extra + (pos - col * rows) * (DF(d, 0x68) + DF(d, 0x44)) + (DF(d, 0x44) >> 1);
    if (!strlist_seek_pos(l, pos)) return;
    int h = DF(d, 0x68), tall = buffer_text_line_height(buf) * cur_lines(l);
    if (h <= tall) h = tall;
    int32_t r[4] = {x, y, x + DF(d, 0x64), y + h};
    int bottom = r[3];
    int state = hot ? 2 : DF(d, 0xf0) == pos;
    int id = dialog_pos_to_id(d, pos);
    if (!vcall4(parent, VT_ITEM_BACK, (int)(intptr_t)win, id, state, (int)(intptr_t)r)) {
        buffer_copy_full(win_buf(parent), buf, DF(d, 0x24) + x, DF(d, 0x28) + y, x, y, DF(d, 0x64), h);
        if (on && off) {
            struct game_sprite *s = hot ? on : off;
            sprite_draw_2(s, buf, s->key, x, ((bottom - s->full_h - r[1]) >> 1) + y, 1, 1);
        }
    }
    r[1] += shift ? *(int32_t *)(*(uint8_t **)((uint8_t *)buf + 0x52c) + 0x1c) : 0;
    r[0] += DF(d, 0x34);
    char *text = cur_text(l);
    char *bar = text ? strchr(text, '|') : NULL;
    int one = cur_lines(l) == 1;
    if (bar) *bar = 0;
    if (text) {
        if (one) buffer_write_l_4(buf, text, r, (int)strlen(text));
        else buffer_wrap_2(buf, text, r);
    }
    if (bar) {
        *bar = '|';
        buffer_write_right_l_6(buf, bar + 1, r, (int)strlen(bar + 1));
    }
    if (DF(d, 0xb8) != -1 && hot && win_is_dialog_focus(win)) {
        int wide = r[2] - r[0];
        r[0] = DF(d, 0x34), r[1] = y, r[2] = r[0] + wide, r[3] = bottom;
        buffer_dotted_box_rect(buf, r, DF(d, 0xb8));
    }
}

void BUTTON_THIS checkbox_draw_item(void *cb, int pos, int checked, int unused) {
    (void)unused;
    box_draw_item(cb, pos, checked, *(struct game_sprite **)((uint8_t *)cb + 0x10), *(struct game_sprite **)((uint8_t *)cb + 0x14), 1);
}
void BUTTON_THIS radio_draw_item(void *rb, int pos, int selected) {
    box_draw_item(rb, pos, selected, *(struct game_sprite **)((uint8_t *)rb + 0x4), *(struct game_sprite **)((uint8_t *)rb + 0x8), 0);
}

// The box's redraw (the Win's slot): the parent may draw the background (slot 0x28 with the Win); if it
// doesn't, it is copied from the parent's Buffer; then every item is drawn (a CheckBox's with its
// checked bit and a flag that its object's field at 0x4 is the item; a RadioButton's with whether it is
// the selected one), and a RadioButton's window is marked as changed (GraphicWin_soft_update_2).
// Nothing is drawn without a parent.
void BUTTON_THIS checkbox_on_redraw(void *w) {
    void *cb = (uint8_t *)w - 0x1c;
    int32_t *vb = vbtable(cb);
    struct game_win *win = (struct game_win *)((uint8_t *)cb + vb[1]);
    struct game_win *parent = win->parent;
    if (!parent) return;
    *game_win_cur = parent;
    struct game_dlg *d = cb_dlg(cb);
    if (!vcall1(parent, VT_WIN_BACK, (int)(intptr_t)win)) {
        struct game_buffer *buf = win_buf(win);
        buffer_copy_full(win_buf(parent), buf, DF(d, 0x24), DF(d, 0x28), 0, 0, buf->width, -buf->neg_height);
    }
    for (int i = 0; i < d->count; i++) checkbox_draw_item(cb, i, d->sel & bit(i), *(int32_t *)((uint8_t *)cb + 4) == i);
}
void BUTTON_THIS radio_on_redraw(void *w) {
    void *rb = (uint8_t *)w - 0x18;
    int32_t *vb = vbtable(rb);
    struct game_win *win = (struct game_win *)((uint8_t *)rb + vb[1]);
    struct game_win *parent = win->parent;
    if (!parent) return;
    *game_win_cur = parent;
    struct game_dlg *d = cb_dlg(rb);
    if (!vcall1(parent, VT_WIN_BACK, (int)(intptr_t)win)) {
        struct game_buffer *buf = win_buf(win);
        buffer_copy_full(win_buf(parent), buf, DF(d, 0x24), DF(d, 0x28), 0, 0, buf->width, -buf->neg_height);
    }
    for (int i = 0; i < d->count; i++) radio_draw_item(rb, i, DF(d, 0xec) == i);
    graphic_win_soft_update_2(win);
}

// ---- Shadow checks -------------------------------------------------------------------------------------

#define P(p) ((int)(uintptr_t)(p))

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call

// The stand-ins a sweep gives buttons and parents (see below): what they leave behind is a count
// and a hash of their calls and arguments, so a different call, argument or order shows as a
// difference in these eight bytes.
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }

// The scratch objects the sweep builds, all in one block so one span covers what a function of a
// button can change in them.
enum { NB = 6, NG = 3, NP = 3, NN = 8 };
struct scratch {
    struct game_button btn[NB];
    struct game_button_group grp[NG];
    struct game_check_button chk[2];
    struct game_win parent[NP];
    uint8_t sound[2][16];
    struct game_dlg_node nodes[2][NN];
    int32_t vbtable[2][4];
    uint8_t box[2][0xb40];  // a CheckBox and a RadioButton: the vbtable pointer, the Win and the Dialog
};
static struct scratch S;
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, &S, sizeof S);
    writes_vlog(w);
}
// The globals the click and the handlers write.
static void writes_mouse(struct hook_writes *w) {
    hooks_writes_add(w, game_win_cur, 4);
    hooks_writes_add(w, game_win_capture, 4);
    hooks_writes_add(w, game_win_hover, 4);
}

// A check that can't be run on the game's own objects (see the top): in shadow mode the game's calls
// run the original (and are counted, to start the sweep); the sweep calls check_NAME.
#define SWEEP_ONLY(ret, name, params, args, writes, fmt, ...)                \
    SHADOW_DECLARE_T(ret, name, params)                                      \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__)    \
    static ret THISCALL shadow_##name params {                               \
        if (!hooks_nested && !shadow_bypass()) {                             \
            stats_##name.calls++;                                            \
            shadow_tick(BUTTON);                                             \
        }                                                                    \
        SHADOW_TAIL_##ret(orig_##name args);                                 \
    }

SHADOW_WRITER(BUTTON, void, button_set_def_text_color, (int a, int b, int c, int d), (a, b, c, d),
              WRITES(game_button_defaults, sizeof *game_button_defaults), "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER(BUTTON, void, button_set_def_text_color2, (int a, int b, int c, int d), (a, b, c, d),
              WRITES(game_button_defaults, sizeof *game_button_defaults), "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER(BUTTON, void, button_set_def_text_color3, (int a, int b, int c, int d), (a, b, c, d),
              WRITES(game_button_defaults, sizeof *game_button_defaults), "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER(BUTTON, int, button_set_def_font, (void *font, void *a, void *b), (font, a, b),
              WRITES(game_button_fonts, sizeof *game_button_fonts), "%d, %d, %d", P(font), P(a), P(b))

SWEEP_ONLY(void, button_set, (struct game_button * b, int state), (b, state), (writes_scratch(&w_)), "%d, %d", P(b), state)
SWEEP_ONLY(void, button_toggle, (struct game_button * b), (b), (writes_scratch(&w_)), "%d", P(b))
SHADOW_WRITER_T(BUTTON, void, button_on_dialog_focus, (struct game_button * b, int unused), (b, unused), writes_vlog(&w_), "%d",
                P(b))
SHADOW_WRITER_T(BUTTON, void, button_on_mouse_leave, (struct game_button * b, int x, int y), (b, x, y), writes_vlog(&w_),
                "%d, %d, %d", P(b), x, y)
SWEEP_ONLY(void, button_on_left_click, (struct game_button * b, int x, int y), (b, x, y), (writes_scratch(&w_), writes_mouse(&w_)),
           "%d, %d, %d", P(b), x, y)
#define TEXT_WRITES (WRITES(&b->buf, sizeof b->buf), hooks_writes_dc(&w_, (void *)(intptr_t)*(int32_t *)((uint8_t *)&b->buf + 0x64)))
SWEEP_ONLY(void, button_set_text_color, (struct game_button * b, int a, int c, int d, int e), (b, a, c, d, e), TEXT_WRITES,
           "%d, %d, %d, %d, %d", P(b), a, c, d, e)
SWEEP_ONLY(void, button_set_text_color2, (struct game_button * b, int a, int c, int d, int e), (b, a, c, d, e), TEXT_WRITES,
           "%d, %d, %d, %d, %d", P(b), a, c, d, e)
SWEEP_ONLY(void, button_set_text_color3, (struct game_button * b, int a, int c, int d, int e), (b, a, c, d, e), TEXT_WRITES,
           "%d, %d, %d, %d, %d", P(b), a, c, d, e)

// A group's own memory: the buttons it holds and their `group` fields need only be the group's.
#define GROUP_WRITES WRITES(g, sizeof *g)
SHADOW_WRITER_T(BUTTON, voidp, button_group_ctor, (struct game_button_group * g), (g), GROUP_WRITES, "%d", P(g))
SHADOW_WRITER_T(BUTTON, int, button_group_close, (struct game_button_group * g), (g), GROUP_WRITES, "%d", P(g))
SHADOW_WRITER_T(BUTTON, int, button_group_init, (struct game_button_group * g, int user, int flags), (g, user, flags), GROUP_WRITES,
                "%d, %d, %d", P(g), user, flags)
SHADOW_WRITER_T(BUTTON, int, button_group_add, (struct game_button_group * g, struct game_button *b), (g, b),
                (GROUP_WRITES, b ? WRITES(&b->group, 4) : (void)0), "%d, %d", P(g), P(b))
SWEEP_ONLY(int, button_group_set, (struct game_button_group * g, int id, int notify), (g, id, notify), (writes_scratch(&w_)),
           "%d, %d, %d", P(g), id, notify)
SWEEP_ONLY(int, button_group_click, (struct game_button_group * g, int id), (g, id), (writes_scratch(&w_)), "%d, %d", P(g), id)

// The Dialog list's cursor and selection.
#define DLG_WRITES(d) WRITES(&(d)->cur, 0xf4 - 0xc8)
SHADOW_WRITER_T(BUTTON, void, dialog_set_selected_id, (struct game_dlg * d, int id), (d, id), DLG_WRITES(d), "%d, %d", P(d), id)
SHADOW_WRITER_T(BUTTON, int, dialog_get_selected_id, (struct game_dlg * d), (d), DLG_WRITES(d), "%d", P(d))
SHADOW_WRITER_T(BUTTON, int, dialog_id_to_pos, (struct game_dlg * d, int id), (d, id), DLG_WRITES(d), "%d, %d", P(d), id)
SHADOW_WRITER_T(BUTTON, int, dialog_pos_to_id, (struct game_dlg * d, int pos), (d, pos), DLG_WRITES(d), "%d, %d", P(d), pos)

#define CB_WRITES DLG_WRITES(cb_dlg(cb))
SHADOW_WRITER_T(BUTTON, void, checkbox_flip_state_id, (void *cb, int id), (cb, id), CB_WRITES, "%d, %d", P(cb), id)
SHADOW_WRITER_T(BUTTON, void, checkbox_flip_state_pos, (void *cb, int pos), (cb, pos), CB_WRITES, "%d, %d", P(cb), pos)
SHADOW_WRITER_T(BUTTON, void, checkbox_set_state_id, (void *cb, int id, int on), (cb, id, on), CB_WRITES, "%d, %d, %d", P(cb), id, on)
SHADOW_WRITER_T(BUTTON, int, checkbox_get_state_id, (void *cb, int id), (cb, id), CB_WRITES, "%d, %d", P(cb), id)
SHADOW_WRITER_T(BUTTON, void, checkbox_set_state_pos, (void *cb, int pos, int on), (cb, pos, on), CB_WRITES, "%d, %d, %d", P(cb), pos,
                on)
SHADOW_WRITER_T(BUTTON, int, checkbox_get_state_pos, (void *cb, int pos), (cb, pos), CB_WRITES, "%d, %d", P(cb), pos)
SHADOW_WRITER_T(BUTTON, void, checkbox_set_state_flag, (void *cb, int mask), (cb, mask), (CB_WRITES, writes_vlog(&w_)), "%d, %d",
                P(cb), mask)
SWEEP_ONLY(void, checkbox_on_left_double_click, (void *w, int x, int y), (w, x, y), (writes_scratch(&w_), writes_mouse(&w_)),
           "%d, %d, %d", P(w), x, y)
SWEEP_ONLY(void, radio_on_left_double_click, (void *w, int x, int y), (w, x, y), (writes_scratch(&w_), writes_mouse(&w_)),
           "%d, %d, %d", P(w), x, y)
SHADOW_WRITER_T(BUTTON, void, checkbox_on_dialog_focus, (void *w, int unused), (w, unused), writes_vlog(&w_), "%d", P(w))
SHADOW_WRITER_T(BUTTON, void, radio_on_dialog_focus, (void *w, int unused), (w, unused), writes_vlog(&w_), "%d", P(w))
SHADOW_WRITER_T(BUTTON, void, checkbox_on_mouse_leave, (void *w, int x, int y), (w, x, y),
                (WRITES(AT(w, VB(w, 0x1c)[2] + 0xd4), 4), writes_vlog(&w_)), "%d, %d, %d", P(w), x, y)
SHADOW_WRITER_T(BUTTON, void, radio_on_mouse_leave, (void *w, int x, int y), (w, x, y),
                (WRITES(AT(w, VB(w, 0x18)[2] + 0xd8), 4), writes_vlog(&w_)), "%d, %d, %d", P(w), x, y)
SHADOW_WRITER_T(BUTTON, void, checkbox_on_mouse_move, (void *w, int x, int y), (w, x, y),
                (WRITES(AT(w, VB(w, 0x1c)[2] + 0xd4), 4), writes_vlog(&w_)), "%d, %d, %d", P(w), x, y)

SWEEP_ONLY(void, checkbtn_on_left_click, (struct game_check_button * c, int x, int y), (c, x, y), (writes_scratch(&w_)),
           "%d, %d, %d", P(c), x, y)
SWEEP_ONLY(void, checkbtn_on_left_double_click, (struct game_check_button * c, int x, int y), (c, x, y),
           (writes_scratch(&w_)), "%d, %d, %d", P(c), x, y)


// ---- Checks of drawing ----------------------------------------------------------------------------------
//
// Drawing changes the Buffer's own object and pixels, and text goes through its device context (whose
// state lives in Chiron's GDI) and leaves a few globals. A call is only checked for Buffers with a DIB: the
// original's use of a DirectDraw surface can't be put back.

static void writes_pixels(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, b, sizeof *b);
    if (!b->bits || b->dd) return;
    int64_t rows = -(int64_t)b->neg_height, pitch = b->pitch;
    if (rows <= 0 || pitch == 0) return;
    uint8_t *a = b->bits, *c = b->bits + pitch * (rows - 1);
    hooks_writes_add(w, a < c ? a : c, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}
static void writes_text_state(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    int32_t mem = *(int32_t *)((uint8_t *)b + 0x64), now = *(int32_t *)((uint8_t *)b + 0x60);
    hooks_writes_dc(w, (void *)(intptr_t)mem);
    if (now != mem) hooks_writes_dc(w, (void *)(intptr_t)now);
    if (game_font_dc) hooks_writes_dc(w, *game_font_dc);
}
static int win_dd(struct game_win *w) { return win_buf(w)->dd || (w->parent && win_buf(w->parent)->dd); }

#define DRAW_CHECKED(name, params, args, dd_expr, writes, fmt, ...)                       \
    SHADOW_DECLARE_T(void, name, params)                                                  \
    SHADOW_WRITER_BODY(void, name, params, args, writes, fmt, __VA_ARGS__)                \
    static void THISCALL shadow_##name params {                                           \
        if (hooks_nested || (dd_expr)) {                                                  \
            orig_##name args;                                                             \
            return;                                                                       \
        }                                                                                 \
        if (shadow_bypass()) {                                                            \
            name args;                                                                    \
            return;                                                                       \
        }                                                                                 \
        stats_##name.calls++;                                                             \
        shadow_tick(BUTTON);                                                              \
        check_##name args;                                                                \
    }

DRAW_CHECKED(graphic_win_fill, (struct game_win * w, int color), (w, color), win_dd(w),
             (writes_pixels(&w_, win_buf(w)), writes_vlog(&w_)), "%d, %d", P(w), color)
DRAW_CHECKED(image_button_on_redraw, (struct game_button * b), (b), win_dd(&b->win),
             (writes_pixels(&w_, &b->buf), writes_text_state(&w_, &b->buf), writes_vlog(&w_), WRITES(game_win_cur, 4)), "%d", P(b))
DRAW_CHECKED(push_button_on_redraw, (struct game_push_button * p), (p), win_dd(&p->base.win),
             (writes_pixels(&w_, &p->base.buf), writes_text_state(&w_, &p->base.buf), writes_vlog(&w_), WRITES(game_win_cur, 4)), "%d",
             P(p))
DRAW_CHECKED(checkbtn_on_redraw, (struct game_check_button * c), (c), win_dd(&c->win),
             (writes_pixels(&w_, &c->buf), writes_vlog(&w_)), "%d", P(c))

// The FlatButton's redraw calls the parent (its slot 0x14 may draw the name), so the game's calls run the
// original and the sweep checks it. The box functions (CheckBox's and RadioButton's drawing) likewise
// call the parent's slots 0x28 and 0x2c and are checked in the sweep, on scratch boxes (X, below), which
// write the Buffer, the device context text goes through, the list's cursor, the items' text (a `|`
// in it is cut and put back) and line counts, and the parent's call log. The CheckBox's layout calls no
// parent and is checked as it is called, on the Dialog part's numbers and cursor, and the Buffer's
// object and device context (its text colours and Fonts).
SWEEP_ONLY(void, flat_button_on_redraw, (struct game_flat_button * f), (f),
           (writes_pixels(&w_, &f->base.buf), writes_text_state(&w_, &f->base.buf), writes_vlog(&w_), WRITES(game_win_cur, 4)), "%d",
           P(f))

static void writes_layout(struct hook_writes *w, void *box, int32_t *vb) {
    struct game_dlg *d = cb_dlg(box);
    struct game_buffer *buf = box_buf(box, vb);
    hooks_writes_add(w, (uint8_t *)d + 0x34, 0xd4 - 0x34);
    hooks_writes_add(w, buf, sizeof *buf);
    hooks_writes_dc(w, (void *)(intptr_t) * (int32_t *)((uint8_t *)buf + 0x64));
}
SHADOW_WRITER_T(BUTTON, int, checkbox_calculate_dimensions, (void *cb), (cb), writes_layout(&w_, cb, vbtable(cb)), "%d", P(cb))

// The scratch boxes: a CheckBox and a RadioButton laid out as the original does (the vbtable, the Win, the
// Dialog), a list of items with texts, a parent with a Buffer, and pictures.
enum { BXN = 6, BXT = 56 };
static struct {
    uint8_t obj[2][0xb40];
    int32_t vbt[2][4];
    struct game_list_item item[2][BXN];
    struct game_dlg_node node[2][BXN];
    char text[2][BXN][BXT];
    struct {
        struct game_win win;
        struct game_buffer buf;
    } parent;
    uint8_t pix[72 * 48 + 16];
    struct game_sprite spr[3];
    uint8_t sdata[3][1024];
} X;
static void writes_box(struct hook_writes *w, void *box) {
    int32_t *vb = vbtable(box);
    struct game_buffer *buf = box_buf(box, vb);
    writes_pixels(w, buf);
    writes_text_state(w, buf);
    writes_vlog(w);
    hooks_writes_add(w, game_win_cur, 4);
    hooks_writes_add(w, &cb_dlg(box)->cur, 12);
    hooks_writes_add(w, X.text, sizeof X.text);
    hooks_writes_add(w, X.item, sizeof X.item);
    hooks_writes_add(w, (uint8_t *)cb_dlg(box) + 0x34, 0xd4 - 0x34);
}
SWEEP_ONLY(int, radio_calculate_dimensions, (void *rb), (rb), (writes_box(&w_, rb)), "%d", P(rb))
SWEEP_ONLY(void, checkbox_draw_item, (void *cb, int pos, int checked, int unused), (cb, pos, checked, unused), (writes_box(&w_, cb)),
           "%d, %d, %d", P(cb), pos, checked)
SWEEP_ONLY(void, radio_draw_item, (void *rb, int pos, int selected), (rb, pos, selected), (writes_box(&w_, rb)), "%d, %d, %d", P(rb),
           pos, selected)
SWEEP_ONLY(void, checkbox_on_redraw, (void *w), (w), (writes_box(&w_, (uint8_t *)w - 0x1c)), "%d", P(w))
SWEEP_ONLY(void, radio_on_redraw, (void *w), (w), (writes_box(&w_, (uint8_t *)w - 0x18)), "%d", P(w))

// ---- Sweep ---------------------------------------------------------------------------------------------
//
// Scratch buttons, groups, parents and boxes in static memory (S), with a random state each round,
// and a stand-in vtable for all of them whose slots only log the call (see vlog) and answer a number
// of their own, so what a function calls, and with what, is compared. Sounds on the parents are
// objects with the same vtable; the functions a button, a box or the globals hold log too.

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void vrec(int slot, const void *o, int a, int b, int c) {
    vlog.hash = (((vlog.hash * 31u + (uint32_t)slot) * 31u + (uint32_t)(uintptr_t)o) * 31u + (uint32_t)a) * 31u + (uint32_t)b * 7u +
                (uint32_t)c * 13u;
    vlog.n++;
}
#define STUB0(slot) \
    static int THISCALL stub_##slot(void *o) { return vrec(slot, o, 0, 0, 0), slot + 1; }
#define STUB1(slot) \
    static int THISCALL stub_##slot(void *o, int a) { return vrec(slot, o, a, 0, 0), slot + 1; }
#define STUB2(slot) \
    static int THISCALL stub_##slot(void *o, int a, int b) { return vrec(slot, o, a, b, 0), slot + 1; }
#define STUB3(slot) \
    static int THISCALL stub_##slot(void *o, int a, int b, int c) { return vrec(slot, o, a, b, c), slot + 1; }
STUB0(0x10) STUB0(0x1c) STUB2(0x44) STUB1(0xb0) STUB2(0xb4) STUB3(0xb8) STUB2(0xbc) STUB0(0xf8)
// The slots the FlatButton's name, a box's background and a box item's background go through: each answers
// 0 and 1 by turns, so both ways are taken.
static int THISCALL stub_0x14(void *o, int a, int b, int c) { return vrec(0x14, o, a, b, c), vlog.n & 1; }
static int THISCALL stub_0x28(void *o, int a) { return vrec(0x28, o, a, 0, 0), vlog.n & 1; }
static int THISCALL stub_0x2c(void *o, int a, int b, int c, const int32_t *r) {
    return vrec(0x2c, o, a * 3 + b, c, r[0] * 31 + r[1] * 131 + r[2] * 1031 + r[3] * 10007), vlog.n & 1;
}
// A parent that lets a child copy from it half the time.
static int THISCALL stub_0xf4(void *o) { return vrec(0xf4, o, 0, 0, 0), (vlog.n & 2) != 0; }
// A sound that is busy every other time it is asked.
static int THISCALL stub_0x5c(void *o) { return vrec(0x5c, o, 0, 0, 0), vlog.n & 1; }
static void *stub_vt[0x180 / 4];
static void fill_stub_vt(void) {
    stub_vt[0x10 / 4] = (void *)stub_0x10, stub_vt[0x1c / 4] = (void *)stub_0x1c, stub_vt[0x44 / 4] = (void *)stub_0x44;
    stub_vt[0x5c / 4] = (void *)stub_0x5c, stub_vt[0xb0 / 4] = (void *)stub_0xb0, stub_vt[0xb4 / 4] = (void *)stub_0xb4;
    stub_vt[0xb8 / 4] = (void *)stub_0xb8, stub_vt[0xbc / 4] = (void *)stub_0xbc, stub_vt[0xf8 / 4] = (void *)stub_0xf8, stub_vt[0xf4 / 4] = (void *)stub_0xf4;
    stub_vt[0x14 / 4] = (void *)stub_0x14, stub_vt[0x28 / 4] = (void *)stub_0x28, stub_vt[0x2c / 4] = (void *)stub_0x2c;
}
static void cb_click(int id) { vrec(0x401, NULL, id, 0, 0); }
static void cb_double(int id) { vrec(0x402, NULL, id, 0, 0); }
static void cb_hook_a(void) { vrec(0x403, NULL, 0, 0, 0); }
static void cb_hook_b(void) { vrec(0x404, NULL, 0, 0, 0); }

static const int IDS[] = {-2, -1, 0, 1, 2, 3, 4, 5};
static struct game_button *any_btn(void) { return &S.btn[rnd_to(NB)]; }
static struct game_win *any_parent(void) { return rnd_to(10) ? &S.parent[rnd_to(NP)] : NULL; }

// A box's Win is where its vbtable says, and has a parent; its Dialog holds a ring of items.
static uint8_t *box_win(int k) { return S.box[k] + S.vbtable[k][1]; }
static void build(void) {
    memset(&S, 0, sizeof S);
    for (int p = 0; p < NP; p++) {
        S.parent[p].vtable = stub_vt;
        static const int snd[] = {SND_DOWN, SND_UP, 0x78, SND_CLICK};
        for (int i = 0; i < 4; i++) {
            void *s = rnd_to(3) ? S.sound[rnd_to(2)] : NULL;
            memcpy((uint8_t *)&S.parent[p] + snd[i], &s, sizeof s);
        }
    }
    for (int k = 0; k < 2; k++) *(void ***)S.sound[k] = stub_vt;
    for (int i = 0; i < NB; i++) {
        struct game_button *b = &S.btn[i];
        b->win.vtable = stub_vt;
        b->win.parent = rnd_to(12) ? &S.parent[rnd_to(NP)] : NULL;
        b->state = rnd_to(3), b->changed = rnd_to(2), b->id = IDS[rnd_to(8)], b->disabled = rnd_to(4) == 0;
        b->on_click = rnd_to(2) ? cb_click : NULL;
        b->pressed = rnd_to(2);
    }
    for (int i = 0; i < NG; i++) {
        struct game_button_group *g = &S.grp[i];
        g->count = rnd_to(NB + 1);
        for (int k = 0; k < g->count; k++) {
            g->buttons[k] = any_btn();
            if (rnd_to(4)) g->buttons[k]->win.parent = &S.parent[rnd_to(NP)];  // the first one's parent is called
            if (rnd_to(2)) g->buttons[k]->group = g;
        }
        g->sel = rnd_to(4) ? IDS[rnd_to(8)] : -1;
        g->flags = rnd_to(4), g->user = (void *)(intptr_t)rnd_to(100), g->silent = rnd_to(4) == 0;
    }
    for (int i = 0; i < 2; i++) {
        struct game_check_button *c = &S.chk[i];
        c->win.vtable = stub_vt;
        c->win.parent = rnd_to(6) ? &S.parent[rnd_to(NP)] : NULL;
        c->state = rnd_to(2), c->id = rnd_to(20) - 3;
        c->on_click = rnd_to(2) ? cb_click : NULL, c->on_double_click = rnd_to(2) ? cb_double : NULL;
    }
    for (int k = 0; k < 2; k++) {
        S.vbtable[k][1] = k ? 0x18 : 0x1c, S.vbtable[k][2] = k ? 0xa30 : 0xa34;
        *(int32_t **)S.box[k] = S.vbtable[k];
        uint8_t *win = box_win(k);
        *(void ***)win = stub_vt;
        void *parent = any_parent();
        memcpy(win + 0xc4, &parent, sizeof parent);
        void *fn = rnd_to(2) ? (void *)cb_double : NULL;
        memcpy(S.box[k] + (k ? 0x10 : 0xc), &fn, sizeof fn);
        struct game_dlg *d = (struct game_dlg *)(S.box[k] + S.vbtable[k][2]);
        int n = rnd_to(NN + 1);
        for (int i = 0; i < n; i++) {
            S.nodes[k][i].key = rnd_to(14) - 2;
            S.nodes[k][i].next = &S.nodes[k][(i + 1) % n], S.nodes[k][i].prev = &S.nodes[k][(i + n - 1) % n];
        }
        d->count = !n ? 0 : rnd_to(8) ? n : 1 + rnd_to(NN);
        d->head = n ? &S.nodes[k][0] : NULL;
        d->cur = n ? &S.nodes[k][rnd_to(n)] : NULL, d->idx = rnd_to(NN);
        d->sel = k ? rnd_to(2 * NN + 3) - NN - 1 : (int)rnd();
        d->hover = rnd_to(NN + 2) - 1;
        // The grid the mouse move divides by (never by zero): from the Win's address, which is
        // 0x1c into the box, these are 0x28, 0x48, 0x4c and 0x3c past the Dialog's offset.
        uint8_t *e = (uint8_t *)d + 0x1c;
        *(int32_t *)(e + 0x28) = rnd_to(5), *(int32_t *)(e + 0x48) = 8 + rnd_to(40), *(int32_t *)(e + 0x4c) = 6 + rnd_to(30);
        *(int32_t *)(e + 0x3c) = 1 + rnd_to(4);
    }
}

static void sweep_dialog(struct game_dlg *d) {
    check_dialog_get_selected_id(d);
    check_dialog_id_to_pos(d, rnd_to(14) - 2);
    check_dialog_set_selected_id(d, rnd_to(14) - 2);
    check_dialog_pos_to_id(d, rnd_to(2 * NN + 6) - NN - 3);
    // The first item of the ring, and the last.
    if (d->head) check_dialog_set_selected_id(d, d->head->key), check_dialog_set_selected_id(d, d->head->prev->key);
    d->sel = rnd_to(2 * NN + 6) - NN - 3;
    check_dialog_get_selected_id(d);
}

// The drawing functions on a scratch window with a Buffer of its own (hand-built pixels in static
// memory, or a real one with a device context for the name's text), a parent with a Buffer, and
// sprites of a few colours.
enum { DWID = 40, DHGT = 24, PWID = 72, PHGT = 48 };
static struct {
    struct game_button btn;
    struct game_check_button chk;
    struct game_push_button push;
    struct game_flat_button flat;
    struct game_box_frame frame[2];
    struct {
        struct game_win win;
        struct game_buffer buf;
    } parent;
    struct game_sprite spr[3];
    uint8_t sdata[3][1024];
    uint8_t pix[5][PWID * PHGT + 16];
    uint8_t tint[256];
} D;

static void hand_buffer(struct game_buffer *b, uint8_t *pixels, int w, int h) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < w * h + 8; i++) pixels[i] = (uint8_t)rnd();
    b->bits = pixels;
    b->width = w, b->neg_height = -h, b->pitch = w;
    b->full[2] = w, b->full[3] = h;
    b->clip[2] = w, b->clip[3] = h;
}
static void make_sprite(struct game_sprite *s, uint8_t *data) {
    memset(s, 0, sizeof *s);
    int w = 1 + rnd_to(18), h = 1 + rnd_to(14), key = rnd_to(8);
    for (int i = 0; i < 1024; i++) {
        int c = rnd_to(6);
        data[i] = c == 0 ? (uint8_t)key : c == 1 ? 0 : (uint8_t)(1 + rnd_to(7));
    }
    s->data = data, s->key = (uint8_t)key;
    s->width = w, s->stride = w, s->height = h, s->full_w = w + rnd_to(6), s->full_h = h + rnd_to(6);
}
static void random_rect(int32_t *r) {
    r[0] = rnd_to(20) - 5, r[1] = rnd_to(20) - 5, r[2] = r[0] + rnd_to(30), r[3] = r[1] + rnd_to(30);
}

static void draw_sweep(struct game_buffer *real, int has_font) {
    uint32_t saved_flags = 0;
    (void)saved_flags;
    struct palette *pal = *game_palette_copy;
    uint32_t hwnd = *game_win_hwnd;
    const uint8_t *tint = *game_win_tint;
    *game_win_hwnd = 1;
    static char name[] = "Ok";
    for (int round = 0; round < 40; round++) {
        memset(&D, 0, sizeof D);
        for (int i = 0; i < 3; i++) make_sprite(&D.spr[i], D.sdata[i]);
        for (int i = 0; i < 256; i++) D.tint[i] = (uint8_t)rnd();
        *game_win_tint = rnd_to(2) ? D.tint : NULL;
        hand_buffer(&D.parent.buf, D.pix[0], PWID, PHGT);
        D.parent.win.vtable = stub_vt;
        int real_variant = has_font && round & 1;
        *game_palette_copy = real_variant ? pal : NULL;
        // The window: where it lies in its parent, its flags, and its Buffer.
        struct game_win *w = &D.btn.win;
        w->vtable = stub_vt, w->parent = rnd_to(10) ? &D.parent.win : NULL;
        w->flags = (rnd_to(2) ? 0x80000 : 0) | (rnd_to(3) == 0 ? WIN_F_DIALOG : 0);
        random_rect(w->inner), random_rect(w->outer);
        if (real_variant) memcpy(&D.btn.buf, real, sizeof *real);
        else hand_buffer(&D.btn.buf, D.pix[1], DWID, DHGT);
        D.btn.fill = rnd_to(256), D.btn.focus_a = rnd_to(3) ? rnd_to(256) : -1, D.btn.border_a = rnd_to(256), D.btn.border_b = rnd_to(256);
        D.btn.border_w = rnd_to(4), D.btn.pressed = rnd_to(2);
        D.btn.picture = rnd_to(4) ? &D.spr[rnd_to(3)] : NULL;
        D.btn.name = real_variant && rnd_to(3) ? name : NULL;
        // Its CheckButton cousin.
        struct game_check_button *c = &D.chk;
        c->win.vtable = stub_vt, c->win.parent = rnd_to(8) ? &D.parent.win : NULL;
        random_rect(c->win.outer);
        hand_buffer(&c->buf, D.pix[2], DWID, DHGT);
        c->state = rnd_to(2), c->pic_off = &D.spr[0], c->pic_on = &D.spr[1 + rnd_to(2)];
        // A PushButton: the frame of sprites (all of them half the time) or without.
        struct game_push_button *pb = &D.push;
        pb->base.win.vtable = stub_vt, pb->base.win.parent = rnd_to(10) ? &D.parent.win : NULL;
        pb->base.win.flags = (rnd_to(2) ? 0x80000 : 0) | (rnd_to(3) == 0 ? WIN_F_DIALOG : 0);
        random_rect(pb->base.win.inner), random_rect(pb->base.win.outer);
        if (pb->base.win.parent) pb->base.win.parent->flags = rnd_to(3) == 0 ? WIN_F_DIALOG : 0;
        if (real_variant) memcpy(&pb->base.buf, real, sizeof *real);
        else hand_buffer(&pb->base.buf, D.pix[3], DWID, DHGT);
        pb->base.fill = rnd_to(256), pb->base.focus_a = rnd_to(3) ? rnd_to(256) : -1, pb->base.border_a = rnd_to(256);
        pb->base.border_b = rnd_to(256), pb->base.border_w = rnd_to(4), pb->base.pressed = rnd_to(2);
        pb->base.name = real_variant && rnd_to(3) ? name : NULL;
        int complete = rnd_to(2);
        struct game_sprite **slots[] = {(struct game_sprite **)&pb->base.picture, &pb->right, &pb->top, &pb->bottom, &pb->tl, &pb->tr,
                                        &pb->bl, &pb->br, &pb->mid, &pb->pleft, &pb->pright, &pb->ptop, &pb->pbottom, &pb->ptl,
                                        &pb->ptr, &pb->pbl, &pb->pbr, &pb->pmid};
        for (int i = 0; i < 18; i++) *slots[i] = (complete || rnd_to(5)) ? &D.spr[rnd_to(3)] : NULL;
        if (rnd_to(5) == 0) pb->tl = NULL;
        for (int k = 0; k < 4; k++) {
            if (pb->base.win.parent) {
                *game_win_hwnd = rnd_to(8) ? 1 : 0;
                check_push_button_on_redraw(pb);
                *game_win_hwnd = 1;
            }
            pb->base.pressed = !pb->base.pressed, pb->base.border_w = rnd_to(4);
            for (int i = 0; i < 18; i++) *slots[i] = (complete || rnd_to(5)) ? &D.spr[rnd_to(3)] : NULL;
            if (rnd_to(5) == 0) pb->tl = NULL;
            if (pb->base.win.parent) pb->base.win.parent->flags = rnd_to(3) == 0 ? WIN_F_DIALOG : 0;
        }
        // A FlatButton: one of the looks, with a picture, a frame, the eight sprites, or none of them, and a
        // name (short or too long for it) when it has the real Buffer.
        struct game_flat_button *fb = &D.flat;
        static char flat_long[] = "A button name too long to fit";
        for (int k = 0; k < 4; k++) {
            fb->base.win.vtable = stub_vt, fb->base.win.parent = rnd_to(10) ? &D.parent.win : NULL;
            fb->base.win.flags = (rnd_to(2) ? 0x80000 : 0) | (rnd_to(3) == 0 ? WIN_F_DIALOG : 0);
            random_rect(fb->base.win.inner), random_rect(fb->base.win.outer);
            if (fb->base.win.parent) fb->base.win.parent->flags = rnd_to(3) == 0 ? WIN_F_DIALOG : 0;
            if (real_variant) memcpy(&fb->base.buf, real, sizeof *real);
            else hand_buffer(&fb->base.buf, D.pix[4], DWID, DHGT);
            fb->base.focus_a = rnd_to(3) ? rnd_to(256) : -1, fb->base.border_a = rnd_to(256), fb->base.border_b = rnd_to(256);
            fb->base.border_w = rnd_to(4), fb->base.pressed = rnd_to(2), fb->base.state = rnd_to(2), fb->base.changed = rnd_to(2);
            fb->base.picture = (void *)(intptr_t)(rnd_to(5) - 1);
            fb->base.name = real_variant && rnd_to(3) ? (rnd_to(2) ? name : flat_long) : NULL;
            fb->base.id = rnd_to(20);
            for (int l = 0; l < 3; l++) {
                fb->text_color[l] = rnd_to(3) ? rnd_to(256) : -1;
                D.frame[0] = (struct game_box_frame){&D.spr[rnd_to(3)], &D.spr[rnd_to(3)], &D.spr[rnd_to(3)], &D.spr[rnd_to(3)],
                                                      &D.spr[rnd_to(3)], &D.spr[rnd_to(3)], &D.spr[rnd_to(3)], &D.spr[rnd_to(3)], NULL, 0};
                D.frame[1] = (struct game_box_frame){rnd_to(2) ? &D.spr[0] : NULL, NULL, NULL, &D.spr[1], NULL, &D.spr[2], NULL, NULL, NULL, 0};
                fb->frame[l] = rnd_to(3) == 0 ? &D.frame[rnd_to(2)] : NULL;
                fb->pic[l] = rnd_to(4) == 0 ? &D.spr[rnd_to(3)] : NULL;
                struct game_sprite **parts[] = {&fb->left[l], &fb->right[l], &fb->top[l], &fb->bottom[l], &fb->tl[l],
                                                &fb->tr[l], &fb->bl[l], &fb->br[l], &fb->mid[l]};
                int all = rnd_to(2);
                for (int i = 0; i < 9; i++) *parts[i] = rnd_to(3) && (all || rnd_to(5)) ? &D.spr[rnd_to(3)] : NULL;
            }
            if (fb->base.win.parent) {
                *game_win_hwnd = rnd_to(8) ? 1 : 0;
                check_flat_button_on_redraw(fb);
                *game_win_hwnd = 1;
            }
        }
        for (int k = 0; k < 4; k++) {
            if (D.btn.win.parent) {
                check_graphic_win_fill(&D.btn.win, rnd_to(256));
                *game_win_hwnd = rnd_to(8) ? 1 : 0;
                check_image_button_on_redraw(&D.btn);
                *game_win_hwnd = 1;
            }
            if (c->win.parent) check_checkbtn_on_redraw(c);
            D.btn.pressed = !D.btn.pressed, D.btn.border_w = rnd_to(4), D.btn.picture = rnd_to(4) ? &D.spr[rnd_to(3)] : NULL;
            c->state = !c->state;
            random_rect(w->inner), random_rect(w->outer), random_rect(c->win.outer);
            D.btn.win.flags ^= 0x80000;
        }
    }
    *game_win_hwnd = hwnd;
    *game_win_tint = tint;
    *game_palette_copy = pal;
}

// The boxes: a CheckBox and a RadioButton on a real Buffer (a copy of one with a device context and a Font) with
// items of the texts below (some with a `|`, long ones to wrap, one empty), a parent that draws the
// background half the time, and every number the layout reads varied; the layout is checked first and the
// drawing on what it made, then with the numbers changed so the drawing sees what the layout hasn't.
static const char *const WORDS[] = {"Ok", "Alpha|Omega", "An item with enough words in it to wrap", "", "x|", "|y",
                                    "Wide wide wide wide wide wide wide", "WWWW|MMMM"};
static void box_build(int k, struct game_buffer *big, struct font *font) {
    memset(X.obj[k], 0, sizeof X.obj[k]);
    int32_t *vb = X.vbt[k];
    vb[1] = k ? 0x18 : 0x1c, vb[2] = k ? 0xa30 : 0xa34;
    *(int32_t **)X.obj[k] = vb;
    struct game_win *win = (struct game_win *)(X.obj[k] + vb[1]);
    win->vtable = stub_vt;
    win->parent = rnd_to(10) ? &X.parent.win : NULL;
    win->flags = rnd_to(3) == 0 ? WIN_F_DIALOG : 0;
    memcpy(win_buf(win), big, sizeof *big);
    struct game_sprite *off = rnd_to(5) ? &X.spr[rnd_to(3)] : NULL, *on = rnd_to(5) ? &X.spr[rnd_to(3)] : NULL;
    memcpy(X.obj[k] + (k ? 0x4 : 0x10), &off, sizeof off), memcpy(X.obj[k] + (k ? 0x8 : 0x14), &on, sizeof on);
    struct game_dlg *d = cb_dlg(X.obj[k]);
    int n = rnd_to(BXN + 1);
    for (int i = 0; i < n; i++) {
        snprintf(X.text[k][i], BXT, "%s", WORDS[rnd_to(8)]);
        X.item[k][i].text = X.text[k][i], X.item[k][i].lines = rnd_to(3);
        X.node[k][i].pad8 = (int32_t)(intptr_t)&X.item[k][i], X.node[k][i].key = 10 + i;
        X.node[k][i].next = &X.node[k][(i + 1) % n], X.node[k][i].prev = &X.node[k][(i + n - 1) % n];
    }
    d->head = n ? X.node[k] : NULL, d->cur = n ? &X.node[k][rnd_to(n)] : NULL, d->count = n, d->idx = n ? rnd_to(n) : 0;
    DF(d, 0x20) = rnd_to(4), DF(d, 0x24) = rnd_to(10), DF(d, 0x28) = rnd_to(10);
    DF(d, 0x2c) = rnd_to(3) ? 100 + rnd_to(60) : 0, DF(d, 0x30) = rnd_to(3) ? 10 + rnd_to(40) : 0;
    DF(d, 0x34) = rnd_to(3) ? 6 + rnd_to(8) : 0, DF(d, 0x40) = rnd_to(4), DF(d, 0x44) = rnd_to(4);
    DF(d, 0x48) = rnd_to(2) ? -1 : 8 + rnd_to(10), DF(d, 0x4c) = 1 + rnd_to(3), DF(d, 0x50) = 1 + rnd_to(3);
    DF(d, 0x58) = 1 + rnd_to(3), DF(d, 0x5c) = rnd_to(50), DF(d, 0x64) = 10 + rnd_to(30), DF(d, 0x68) = 6 + rnd_to(10);
    DF(d, 0x70) = DF(d, 0x74) = DF(d, 0x78) = (int32_t)(intptr_t)font;
    for (int o = 0x7c; o <= 0xa8; o += 4) DF(d, o) = rnd_to(256);
    if (k && (DF(d, 0x20) & 2) && DF(d, 0x2c)) {
        // The width a RadioButton's items wrap to is never 0 (the original divides by it).
        int rows = n < DF(d, 0x50) ? n : DF(d, 0x50), cols = DF(d, 0x4c);
        if (rows && rows < n) cols = n / rows + 1;
        int pic = DF(d, 0x34) ? DF(d, 0x34) : off && on ? (on->full_w > off->full_w ? on->full_w : off->full_w) : 0;
        if (cols && DF(d, 0x2c) / cols - 2 * DF(d, 0x40) - pic == 0) DF(d, 0x2c) += cols;
    }
    DF(d, 0xb8) = rnd_to(3) ? rnd_to(256) : -1;
    DF(d, 0xec) = k ? rnd_to(BXN + 2) - 1 : (int)rnd();
    DF(d, 0xf0) = rnd_to(BXN + 2) - 1;
}

static void box_sweep(struct game_buffer *big, struct font *font) {
    int32_t f1c = font->f1c;
    struct game_buffer *saved_back = NULL;
    (void)saved_back;
    memset(&X, 0, sizeof X);
    for (int i = 0; i < 3; i++) make_sprite(&X.spr[i], X.sdata[i]);
    hand_buffer(&X.parent.buf, X.pix, 72, 48);
    X.parent.win.vtable = stub_vt;
    for (int round = 0; round < 30; round++) {
        font->f1c = rnd_to(4);
        for (int i = 0; i < 3; i++) make_sprite(&X.spr[i], X.sdata[i]);
        for (int i = 0; i < 72 * 48; i++) X.pix[i] = (uint8_t)rnd();
        for (int k = 0; k < 2; k++) {
            box_build(k, big, font);
            void *box = X.obj[k];
            struct game_dlg *d = cb_dlg(box);
            struct game_win *win = (struct game_win *)(X.obj[k] + X.vbt[k][1]);
            if (k) check_radio_calculate_dimensions(box);
            else check_checkbox_calculate_dimensions(box);
            for (int j = 0; j < 3; j++) {
                for (int pos = 0; pos < d->count + 2; pos++) {
                    int hot = rnd_to(2);
                    if (k) check_radio_draw_item(box, pos, hot);
                    else check_checkbox_draw_item(box, pos, hot ? (int)rnd() | 1 : 0, 0);
                }
                *game_win_cur = NULL;
                if (k) check_radio_on_redraw(win);
                else check_checkbox_on_redraw(win);
                // The numbers the drawing reads, changed.
                DF(d, 0x24) = rnd_to(10), DF(d, 0x28) = rnd_to(10), DF(d, 0x34) = rnd_to(14), DF(d, 0x44) = rnd_to(4);
                DF(d, 0x40) = rnd_to(4), DF(d, 0x58) = 1 + rnd_to(3), DF(d, 0x64) = 10 + rnd_to(30), DF(d, 0x68) = 6 + rnd_to(10);
                DF(d, 0xb8) = rnd_to(3) ? rnd_to(256) : -1, DF(d, 0xf0) = rnd_to(BXN + 2) - 1;
                DF(d, 0xec) = k ? rnd_to(BXN + 2) - 1 : (int)rnd();
                win->flags ^= WIN_F_DIALOG;
                for (int i = 0; i < d->count; i++) X.item[k][i].lines = rnd_to(3);
                if (rnd_to(3) == 0) {
                    uint8_t *spr = (uint8_t *)box + (k ? 0x4 : 0x10);
                    struct game_sprite *s = rnd_to(2) ? &X.spr[rnd_to(3)] : NULL;
                    memcpy(spr, &s, sizeof s);
                }
            }
        }
    }
    font->f1c = f1c;
}

// A larger Buffer with a device context and a Font of its own, made once outside any check, for the sweeps of
// what draws text in boxes (here and in scroll.c); NULL until the game has set up its fonts.
static struct game_buffer big;
static struct font sweep_font;
static int sweep_fonted;
struct game_buffer *button_sweep_buffer(struct font **font) {
    if (!sweep_fonted) return NULL;
    *font = &sweep_font;
    return &big;
}

static void button_sweep(void) {
    static struct game_buffer real;
    static struct font font_a;
    struct font *const mine_p = &sweep_font;
#define mine (*mine_p)
    static int made;
#define fonted sweep_fonted
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x1f123bb5u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    fill_stub_vt();
    // What the sweep sets and puts back by hand.
    struct game_win *capture = *game_win_capture, *hover = *game_win_hover, *press1 = *game_win_press1, *press2 = *game_win_press2,
                    *cur = *game_win_cur;
    int32_t inside = *game_win_press_inside;
    void(__attribute__((cdecl)) * hooks[2])(void) = {game_button_hooks[0], game_button_hooks[1]};
    struct game_button_defaults defaults = *game_button_defaults;
    struct game_button_fonts fonts = *game_button_fonts;
    // A Buffer with pixels of its own, made outside any check, for the text colours.
    if (!made) {
        made = 1;
        game_buffer_ctor(&real);
        game_buffer_init(&real, 24, 8, 0, 0);
        game_buffer_ctor(&big);
        game_buffer_init(&big, 400, 200, 0, 0);
    }
    // (It stays for good, with a font of its own once the game has set up its fonts.)
    if (!fonted && real.bits && *game_font_default && *game_font_dc) {
        font_ctor(&mine), game_font_init(&mine, "Arial", 12, 0);
        if (mine.hfont) buffer_set_font(&real, &mine, &mine, &mine, &mine), buffer_set_font(&big, &mine, &mine, &mine, &mine), fonted = 1;
    }
    for (int round = 0; round < 30; round++) {
        build();
        check_button_set_def_text_color((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_button_set_def_text_color2((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_button_set_def_text_color3((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        font_a.hfont = rnd_to(2) ? (void *)0x1234 : NULL;
        check_button_set_def_font(NULL, (void *)1, (void *)2);
        check_button_set_def_font(&font_a, (void *)(intptr_t)rnd(), (void *)(intptr_t)rnd());
        game_button_hooks[0] = rnd_to(3) ? cb_hook_a : NULL, game_button_hooks[1] = rnd_to(3) ? cb_hook_b : NULL;
        // A button with a Buffer that has pixels.
        S.btn[0].win.parent = rnd_to(8) ? &S.parent[0] : NULL;
        memcpy(&S.btn[0].buf, &real, sizeof real);
        // A group's first button is the one whose parent hears of it, so it needs one.
        for (int i = 0; i < NG; i++)
            if (S.grp[i].count && !S.grp[i].buttons[0]->win.parent) S.grp[i].buttons[0]->win.parent = &S.parent[0];
        for (int k = 0; k < NB; k++) {
            struct game_button *b = &S.btn[k];
            check_button_set(b, rnd_to(4) - 1);
            check_button_set(b, b->state);
            check_button_toggle(b);
            check_button_on_dialog_focus(b, 0);
            *game_win_press1 = rnd_to(2) ? &b->win : NULL, *game_win_press2 = rnd_to(2) ? &b->win : NULL;
            *game_win_press_inside = rnd_to(3);
            check_button_on_mouse_leave(b, rnd_to(100), rnd_to(100));
            if (b->win.parent) {
                *game_win_capture = rnd_to(2) ? &b->win : NULL, *game_win_hover = rnd_to(2) ? &b->win : NULL;
                check_button_on_left_click(b, rnd_to(100), rnd_to(100));
            }
            if (!k) {
                check_button_set_text_color(b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
                check_button_set_text_color2(b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
                check_button_set_text_color3(b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            }
        }
        for (int i = 0; i < NG; i++) {
            struct game_button_group *g = &S.grp[i];
            for (int k = 0; k < 8; k++) {
                int id = k < 5 ? IDS[rnd_to(8)] : k == 5 ? g->sel : k == 6 ? -1 : 77;
                if (g->count && !g->buttons[0]->win.parent) g->buttons[0]->win.parent = &S.parent[0];
                check_button_group_click(g, id);
                check_button_group_set(g, id, rnd_to(2));
                g->flags = rnd_to(4), g->silent = rnd_to(4) == 0;
            }
            if (g->count < 32) check_button_group_add(g, any_btn());
            g->count -= g->count > 0;
            check_button_group_init(g, rnd_to(100), rnd_to(4));
            check_button_group_close(g);
            check_button_group_ctor(g);
        }
        for (int k = 0; k < 2; k++) {
            void *cb = S.box[k];
            uint8_t *win = box_win(k);
            struct game_dlg *d = (struct game_dlg *)(S.box[k] + S.vbtable[k][2]);
            if (!k) {
                for (int j = 0; j < 4; j++) {
                    check_checkbox_flip_state_id(cb, rnd_to(14) - 2);
                    check_checkbox_set_state_id(cb, rnd_to(14) - 2, rnd_to(2));
                    check_checkbox_get_state_id(cb, rnd_to(14) - 2);
                    check_checkbox_flip_state_pos(cb, rnd_to(40) - 4);
                    check_checkbox_set_state_pos(cb, rnd_to(40) - 4, rnd_to(2));
                    check_checkbox_get_state_pos(cb, rnd_to(40) - 4);
                    check_checkbox_set_state_flag(cb, (int)rnd());
                    *game_win_cur = NULL;
                    check_checkbox_on_left_double_click(win, 3, 4);
                    check_checkbox_on_mouse_leave(win, 0, 0);
                    check_checkbox_on_mouse_move(win, rnd_to(200), rnd_to(200));
                    check_checkbox_on_dialog_focus(win, 0);
                    d->sel = (int)rnd();
                }
            } else {
                for (int j = 0; j < 4; j++) {
                    *game_win_cur = NULL;
                    check_radio_on_left_double_click(win, 3, 4);
                    check_radio_on_mouse_leave(win, 0, 0);
                    check_radio_on_dialog_focus(win, 0);
                    d->sel = rnd_to(2 * NN + 6) - NN - 3;
                }
            }
            sweep_dialog(d);
        }
        for (int i = 0; i < 2; i++) {
            check_checkbtn_on_left_click(&S.chk[i], 1, 2);
            check_checkbtn_on_left_click(&S.chk[i], 1, 2);
            check_checkbtn_on_left_double_click(&S.chk[i], 1, 2);
        }
    }
    draw_sweep(&real, fonted);
    if (fonted && big.bits) box_sweep(&big, &mine);
    *game_win_capture = capture, *game_win_hover = hover, *game_win_press1 = press1, *game_win_press2 = press2, *game_win_cur = cur;
    *game_win_press_inside = inside;
    game_button_hooks[0] = hooks[0], game_button_hooks[1] = hooks[1];
    *game_button_defaults = defaults;
    *game_button_fonts = fonts;
    hooks_sweeping = sweeping;
#undef mine
#undef fonted
}

// ---- Hooks ------------------------------------------------------------------------------------------------

static const struct hook button_hooks[] = {
    HOOK(0x607420, button_set_def_text_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x607450, button_set_def_text_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x607480, button_set_def_text_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x6074b0, button_set_def_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x607c80, button_set, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x607c30, button_toggle, 0x56, 0x8b, 0xf1, 0x33, 0xc9, 0x57, 0x8b, 0x86, 0x18, 0x0a, 0x00, 0x00),
    HOOK(0x607b00, button_on_dialog_focus, 0x8b, 0x01, 0xff, 0x90, 0xf8, 0x00, 0x00, 0x00),
    HOOK(0x607820, button_on_mouse_leave, 0xa1, 0xcc, 0x7a, 0x9b, 0x00, 0x85, 0xc0),
    HOOK(0x607a20, button_on_left_click, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x8b, 0xf1),
    HOOK(0x607360, button_set_text_color, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x6073a0, button_set_text_color2, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x6073e0, button_set_text_color3, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x62b7c0, button_group_ctor, 0x8b, 0xd1, 0x57, 0x33, 0xc0, 0xb9, 0x20, 0x00, 0x00, 0x00),
    HOOK(0x62b7f0, button_group_close, 0x57, 0x8b, 0xf9, 0x33, 0xc0, 0xb9, 0x20, 0x00, 0x00, 0x00),
    HOOK(0x62b810, button_group_init, 0x8b, 0xd1, 0x57, 0x33, 0xc0, 0xb9, 0x20, 0x00, 0x00, 0x00),
    HOOK(0x62b850, button_group_add, 0x8b, 0x91, 0x80, 0x00, 0x00, 0x00, 0x8b, 0x44, 0x24, 0x04),
    HOOK(0x62b870, button_group_set, 0x8b, 0x44, 0x24, 0x08, 0x56, 0x85, 0xc0, 0x8b, 0xf1),
    HOOK(0x62b8a0, button_group_click, 0x83, 0xec, 0x10, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x609a50, dialog_get_selected_id, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x86, 0xcc, 0x00, 0x00, 0x00),
    HOOK(0x6099d0, dialog_set_selected_id, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x33, 0xd2),
    HOOK(0x609af0, dialog_id_to_pos, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x33, 0xd2),
    HOOK(0x609b50, dialog_pos_to_id, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x60ea10, checkbox_flip_state_id, 0x53, 0x8b, 0xd9, 0x33, 0xd2, 0x55, 0x8b, 0x03),
    HOOK(0x60eb50, checkbox_flip_state_pos, 0x8b, 0x01, 0x8b, 0x50, 0x08, 0xb8, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x60eb80, checkbox_set_state_id, 0x8b, 0x01, 0x53, 0x55, 0x56, 0x8b, 0x50, 0x08),
    HOOK(0x60ec10, checkbox_get_state_id, 0x8b, 0x01, 0x53, 0x55, 0x56, 0x8b, 0x50, 0x08),
    HOOK(0x60ec80, checkbox_set_state_pos, 0x8b, 0x44, 0x24, 0x08, 0x85, 0xc0, 0x8b, 0x01),
    HOOK(0x60ecc0, checkbox_get_state_pos, 0x8b, 0xc1, 0x56, 0xbe, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x60ece0, checkbox_set_state_flag, 0x8b, 0x01, 0x8b, 0x50, 0x08, 0x8b, 0x44, 0x24, 0x04),
    HOOK(0x60fa80, checkbox_on_left_double_click, 0x8b, 0x41, 0xe4, 0x8b, 0x50, 0x04, 0x8b, 0x84, 0x0a, 0xa8, 0x00, 0x00, 0x00),
    HOOK(0x60e1e0, radio_on_left_double_click, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0xe8, 0x8b, 0x48, 0x04),
    HOOK(0x60fb90, checkbox_on_dialog_focus, 0x8b, 0x41, 0xe4, 0x8b, 0x50, 0x04, 0x8b, 0x44, 0x0a, 0xe4),
    HOOK(0x60e300, radio_on_dialog_focus, 0x8b, 0x41, 0xe8, 0x8b, 0x50, 0x04, 0x8b, 0x44, 0x0a, 0xe8),
    HOOK(0x60fc30, checkbox_on_mouse_leave, 0x8b, 0x41, 0xe4, 0x8b, 0x50, 0x08),
    HOOK(0x60e4a0, radio_on_mouse_leave, 0x8b, 0x41, 0xe8, 0x8b, 0x50, 0x08),
    HOOK(0x60fbb0, checkbox_on_mouse_move, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0xe4),
    HOOK(0x5d5250, graphic_win_fill, 0x83, 0xec, 0x10, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x62c110, push_button_on_redraw, 0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x6253d0, image_button_on_redraw, 0xa1, 0x28, 0x7b, 0x9b, 0x00, 0x83, 0xec, 0x18),
    HOOK(0x633850, checkbtn_on_redraw, 0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x633920, checkbtn_on_left_click, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0xbe, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x633980, checkbtn_on_left_double_click, 0x8b, 0x81, 0x20, 0x0a, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x607e50, flat_button_on_redraw, 0x83, 0xec, 0x14, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x60ed10, checkbox_calculate_dimensions, 0x56, 0x8b, 0xf1, 0x8b, 0x0e),
    HOOK(0x60d380, radio_calculate_dimensions, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb, 0x8b, 0x0e),
    HOOK(0x60f030, checkbox_draw_item, 0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x60d750, radio_draw_item, 0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x60fab0, checkbox_on_redraw, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x46, 0xe4),
    HOOK(0x60e220, radio_on_redraw, 0x53, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0xe8),
};
struct hook_module button_module = {"button", button_hooks, HOOK_COUNT(button_hooks), button_sweep, NULL, -1};
