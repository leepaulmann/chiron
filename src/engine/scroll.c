// The game's Scroll (scroll bar), ListBox and StringBox classes (OpenSMACX's and Thinker's names).
// A scroll bar is a Win with a Buffer behind it and two ImageButtons for its arrows; it keeps a
// position between `lo` and `hi` and tells its parent (through slots 0xc0 and 0xc4 of the parent's
// vtable) and a function a screen may have set. A ListBox is a Win and a Dialog list (both virtual
// bases of the box, found through its vbtable) with a scroll bar the Win owns. The layouts are in
// game.h.
//
// Here are the functions that change a bar's state, colours and pictures, its thumb, the mouse, the
// wheel, the keyboard and the arrows (as far as they change state), a resize, and its redraw; the
// ListBox's selection, double clicks, wheel and mouse leave, its search by text, its layout
// (calculate_dimensions) and its drawing (on_redraw and draw_item); and two of StringBox's. Where the original calls through a vtable (a screen subclasses a bar's parent to hear
// of it; the redraw is slot 0xf8) or through a function pointer a screen set, so does this.
//
// Left to the original: the constructors, destructors, close and init functions (they build
// objects on the game's heap and keep an exception frame; the init_vert, init_horz and _nc ones
// only call init), on_mouse_move and on_button_pressed (the first reads timeGetTime and posts a
// message, the second also sleeps: timing no check can put back), on_left_click (which does
// nothing). ListBox: attach and item (the heap), and on_scrolling, on_mouse_move, on_left_down,
// on_key_down and UNK1, which draw or read the keyboard through Windows. StringBox:
// everything but on_scrolled and on_link_clicked (its strings are on the heap).
//
// Differences from OpenSMACX and Thinker: neither lays the Scroll out; the inventory's UNK1 and UNK2
// are the pictures for the up and down arrows (this file's set_sprite_up and set_sprite_down). A
// position is mirrored for an inverted bar (a range given high to low): the bar keeps `lo + hi -
// position` where a wheel turn sets it, and tells its parent the position itself; a key press or an
// arrow keeps the position and tells the parent the mirror. The thumb rectangle is taken from the
// bar's own at 0xa4c and put back there. The ListBox's 0x9b8ddc flag is the bars' "moved by a key".
//
// Checks. A function that only touches the object, a few globals and the redraw slot is checked
// as it is called, on the memory it writes. One that calls a parent's handler, a sound or a
// function a screen gave the bar has effects no check can put back (a handler may open a window),
// so the game's calls of it run the original in shadow mode and the sweep checks it on scratch
// bars, boxes and parents in static memory whose vtables log the calls. Each check lists the
// memory its function can change, and a plant that writes nothing, or a wrong value, shows as a
// difference. A redraw is checked as it is called (for a Buffer with a DIB) on the Buffer's object
// and pixels; the sweep draws on hand-built Buffers and sprites in static memory.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufobj.h"
#include "bufimg.h"
#include "button.h"
#include "dialog.h"
#include "buffer.h"
#include "game.h"
#include "hooks.h"
#include "life.h"
#include "scroll.h"
#include "strlist.h"
#include "text.h"
#include "shadow.h"
#include "sprite.h"
#include "win.h"

#define SCROLL (&scroll_module)
extern struct hook_module scroll_module;

// ---- Calls through a vtable --------------------------------------------------------------------

// What the classes put at these slots of a vtable (byte offsets).
enum {
    VT_RESIZE = 0xc,        // a window: be this size (width, height, client)
    VT_CAPTURE_LOST = 0x10,
    VT_SOUND_PLAY = 0x1c,
    VT_MOUSE_MOVE = 0x44,
    VT_LEFT_DOWN = 0x50,
    VT_SOUND_BUSY = 0x5c,
    VT_LEFT_DOUBLE = 0x70,
    VT_SCROLLED = 0xc0,     // the parent: a bar was clicked, with its id and the position
    VT_SCROLLING = 0xc4,    // the parent: a bar is being dragged (the same arguments)
    VT_LIST_POS = 0xd4,     // the parent: a list box selected an item (id, 0)
    VT_LIST_ID = 0xd8,      // the same (id)
    VT_LIST_DOUBLE_A = 0xdc,  // a double click on an item (id, from the right button)
    VT_LIST_DOUBLE_B = 0xe0,  // the same, before (id)
    VT_ITEM_BACK = 0x2c,    // the parent: draw a list box item's background (Win, id, state, rectangle), or answer 0
    VT_WIN_BACK = 0x28,     // the parent: draw a list box's background (its Win), or answer 0
    VT_PARENT_COPY = 0xf4,
    VT_REDRAW = 0xf8,
    VT_LINK = 0x160,        // the parent: a link in a StringBox was clicked
};
// The sounds a parent keeps: for a bar's wheel, keys and arrows (0x80) and its dragging (0x84);
// for a list box's selection (0x90) and double click (0x94).
enum { SND_BAR = 0x80, SND_BAR_DRAG = 0x84, SND_LIST = 0x90, SND_LIST_DOUBLE = 0x94 };

static void *vslot(void *o, int slot) { return *(void **)((uint8_t *)*(void ***)o + slot); }
static int vcall0(void *o, int slot) { return ((int(THISCALL *)(void *))vslot(o, slot))(o); }
static int vcall1(void *o, int slot, int a) { return ((int(THISCALL *)(void *, int))vslot(o, slot))(o, a); }
static int vcall2(void *o, int slot, int a, int b) { return ((int(THISCALL *)(void *, int, int))vslot(o, slot))(o, a, b); }
static int vcall3(void *o, int slot, int a, int b, int c) {
    return ((int(THISCALL *)(void *, int, int, int))vslot(o, slot))(o, a, b, c);
}

// A sound object on the parent, started unless it is already playing.
static void play(struct game_win *parent, int where) {
    void *snd = *(void **)((uint8_t *)parent + where);
    if (snd && !vcall0(snd, VT_SOUND_BUSY)) vcall0(snd, VT_SOUND_PLAY);
}

// ---- Scroll: state ------------------------------------------------------------------------------

static int horizontal(const struct game_scroll *s) { return s->buf.width > -s->buf.neg_height; }

// A new range puts the position at its start; given high to low, it is kept low to high and the bar
// inverted.
void SCROLL_THIS scroll_set_range(struct game_scroll *s, int lo, int hi) {
    if (lo < hi) {
        s->inverted = 0;
    } else {
        s->inverted = 1;
        int t = lo;
        lo = hi, hi = t;
    }
    s->lo = lo;
    s->pos = lo;
    s->hi = hi;
    vcall0(s, VT_REDRAW);
}

// The arrows take the colour and bevel the bar has; each is redrawn.
static void redraw_arrows(struct game_scroll *s) {
    vcall0(s->btn_a, VT_REDRAW);
    vcall0(s->btn_b, VT_REDRAW);
}
// (ImageButton's fill is at 0xa84, its border width at 0xa90 and its two border colours between.)
static int32_t *arrow(struct game_scroll *s, int which, int off) {
    return (int32_t *)((uint8_t *)(which ? s->btn_b : s->btn_a) + off);
}
void SCROLL_THIS scroll_set_button_color(struct game_scroll *s, int color) {
    s->button_color = color;
    *arrow(s, 0, 0xa84) = color;
    *arrow(s, 1, 0xa84) = color;
    redraw_arrows(s);
}
void SCROLL_THIS scroll_set_bevel_thickness(struct game_scroll *s, int n) {
    s->bevel = n;
    *arrow(s, 0, 0xa90) = n;
    *arrow(s, 1, 0xa90) = n;
    redraw_arrows(s);
}
void SCROLL_THIS scroll_set_bevel_upper(struct game_scroll *s, int color) {
    s->bevel_upper = color;
    *arrow(s, 0, 0xa88) = color;
    *arrow(s, 1, 0xa88) = color;
    redraw_arrows(s);
}
void SCROLL_THIS scroll_set_bevel_lower(struct game_scroll *s, int color) {
    s->bevel_lower = color;
    *arrow(s, 0, 0xa8c) = color;
    *arrow(s, 1, 0xa8c) = color;
    redraw_arrows(s);
}

// The thumb's rectangle is a square of the bar's width, less a line on each side if there is a frame.
static void reset_thumb(struct game_scroll *s) {
    s->bar[0] = 0, s->bar[1] = 0, s->bar[2] = s->thickness, s->bar[3] = s->thickness;
    if (s->border != -1) {
        s->bar[0]++, s->bar[1]++, s->bar[2]--, s->bar[3]--;
    }
}
void SCROLL_THIS scroll_set_border_color(struct game_scroll *s, int color) {
    s->border = color;
    reset_thumb(s);
}
void SCROLL_THIS scroll_set_bar_thickness(struct game_scroll *s, int n) {
    s->thickness = n;
    reset_thumb(s);
}
void SCROLL_THIS scroll_set_thumb_rect(struct game_scroll *s) { reset_thumb(s); }

// The pictures for the arrows: each takes three numbers, which an ImageButton arrow also gets (at 0xb10,
// 0xb14 and 0xb18 of the first or second) when the bar lies the way the picture is for: a bar wider than
// it is high for the left and right ones, one narrower (not equal) for the up and down ones.
static void set_arrow_picture(struct game_scroll *s, int slot, int which, int wide, int a, int b, int c) {
    s->spr[slot][0] = a, s->spr[slot][1] = b, s->spr[slot][2] = c;
    int w = s->buf.width, h = -s->buf.neg_height;
    if (wide ? w > h : w < h) {
        *arrow(s, which, 0xb10) = a, *arrow(s, which, 0xb14) = b, *arrow(s, which, 0xb18) = c;
    }
}
void SCROLL_THIS scroll_set_sprite_left(struct game_scroll *s, int a, int b, int c) { set_arrow_picture(s, 0, 0, 1, a, b, c); }
void SCROLL_THIS scroll_set_sprite_right(struct game_scroll *s, int a, int b, int c) { set_arrow_picture(s, 2, 1, 1, a, b, c); }
void SCROLL_THIS scroll_set_sprite_up(struct game_scroll *s, int a, int b, int c) { set_arrow_picture(s, 1, 0, 0, a, b, c); }
void SCROLL_THIS scroll_set_sprite_down(struct game_scroll *s, int a, int b, int c) { set_arrow_picture(s, 3, 1, 0, a, b, c); }

// Move to a position within the range (counted from the far end for an inverted bar) and redraw; only a
// bar that hangs from a window does anything, and that window becomes the one being worked on.
void SCROLL_THIS scroll_set_pos(struct game_scroll *s, int pos) {
    if (!s->win.parent) return;
    *game_win_cur = s->win.parent;
    int lo = s->lo;
    if (pos < lo) s->pos = lo;
    else if (pos > s->hi) s->pos = s->hi;
    else s->pos = pos;
    if (s->inverted) s->pos = s->hi - s->pos + lo;
    vcall0(s, VT_REDRAW);
}

// Where the thumb lies, from the bar's rectangle at 0xa4c (taken as a square of the bar's width at the
// origin, a line in if there is a frame; it is the width of the bar) and the position: left in `out` and
// put back at 0xa4c. While the mouse holds the thumb (`drag` is where, along the bar, it took it) the
// thumb follows the mouse, kept inside the bar (a bar without flag 2 also keeps a thumb's width from each
// end), and the hold is let go. The distance along the bar is the position's share of the room the thumb
// has, by integer division.
void SCROLL_THIS scroll_compute_thumb_rect(struct game_scroll *s, int32_t *out) {
    for (int i = 0; i < 4; i++) out[i] = s->bar[i];
    int w = out[2] - out[0], h = out[3] - out[1];
    out[0] = 0, out[1] = 0, out[2] = w, out[3] = h;
    if (s->border != -1) {
        out[0]++, out[1]++, out[2]++, out[3]++;
    }
    int len = out[2] - out[0];
    int width = s->buf.width, neg_height = s->buf.neg_height;
    if (s->drag == -1) {
        int off, size;
        if (s->flags & 2) {
            off = 0, size = len;
        } else {
            off = len + 1, size = len * 3;
        }
        int room = -1 - size;
        if (s->border != -1) room -= 2;
        int lo = s->lo, hi = s->hi;
        if (width > -neg_height) {
            if (hi != lo) off += (s->pos - lo) * (width + room) / (hi - lo);
            out[0] += off, out[2] += off;
        } else {
            if (hi != lo) off += (s->pos - lo) * (room - neg_height) / (hi - lo);
            out[1] += off, out[3] += off;
        }
    } else {
        int end = out[0] + len;
        int at = (len >> 1) + s->drag;
        s->drag = at;
        int span = width > -neg_height ? width : -neg_height;  // (the bar's own length, along its axis)
        int v;
        if (s->flags & 2) {
            int m = at < span - end ? at : span - end;
            v = m < 0 ? 0 : m;
        } else {
            int m = at < span - 2 * end ? at : span - 2 * end;
            v = end > m ? end : m;
        }
        s->drag = v;
        if (width > -neg_height) out[0] += v, out[2] += v;
        else out[1] += v, out[3] += v;
        s->drag = -1;
    }
    for (int i = 0; i < 4; i++) s->bar[i] = out[i];
}

// ---- Scroll: events ---------------------------------------------------------------------------------------

// The position a parent is told: for an inverted bar, the mirror of the one it keeps.
static int told(const struct game_scroll *s, int inverted) { return inverted ? s->lo - s->pos + s->hi : s->pos; }
// Tell the parent (slot `slot`) the bar's id and position, then the function a screen set (`fn`, read after
// the parent's handler ran, with the id and position as they are then). `inverted` is what the bar was
// when the original looked.
static void tell(struct game_scroll *s, int slot, int inverted, int use_drag) {
    vcall2(s->win.parent, slot, s->id, told(s, inverted));
    void(__attribute__((cdecl)) * fn)(int, int) = use_drag ? s->on_drag : s->on_click;
    if (fn) fn(s->id, told(s, inverted));
}

// The mouse went down: a click beside the thumb starts a track click (which way is `dir`; the press time
// is cleared) and is handled as a mouse move; a click on the thumb leaves `dir` 0. The bar takes the mouse
// capture if nothing has it.
void SCROLL_THIS scroll_on_left_down(struct game_scroll *s, int x, int y) {
    if (!s->win.parent) return;
    int v = horizontal(s) ? x : y;
    int lo = horizontal(s) ? s->bar[0] : s->bar[1];
    int hi = horizontal(s) ? s->bar[2] : s->bar[3];
    if (v < lo) {
        s->dir = -1;
        s->press_time = 0;
        vcall2(s, VT_MOUSE_MOVE, x, y);
    } else if (v > hi) {
        s->dir = 1;
        s->press_time = 0;
        vcall2(s, VT_MOUSE_MOVE, x, y);
    } else {
        s->dir = 0;
    }
    if (*game_win_capture == NULL) {
        *game_win_capture = &s->win;
        *game_win_hover = NULL;
    }
}

// The mouse went up: redraw, and the capture and hover are let go (the capture's owner is told).
void SCROLL_THIS scroll_on_left_up(struct game_scroll *s, int x, int y) {
    (void)x, (void)y;
    if (!s->win.parent) return;
    vcall0(s, VT_REDRAW);
    if (*game_win_capture == &s->win) {
        *game_win_capture = NULL;
        vcall0(s, VT_CAPTURE_LOST);
    }
    if (*game_win_hover == &s->win) *game_win_hover = NULL;
}

// A turn of the wheel (down moves the position up by one, and the other way): the parent becomes the window
// being worked on, the position is kept in range (and mirrored if the bar is inverted), and with `notify`
// the parent, the screen's function and the sound hear of it. The thumb's hold is let go and the bar redrawn.
static void wheel(struct game_scroll *s, int notify, int down) {
    struct game_win *parent = s->win.parent;
    if (!parent) return;
    *game_win_cur = parent;
    if (down) {
        int pos = s->pos + 1;
        s->pos = pos > s->hi ? s->hi : pos;
        if (s->inverted) s->pos = s->lo - s->pos + s->hi;
    } else {
        int pos = s->pos - 1;
        s->pos = pos < s->lo ? s->lo : pos;
        if (s->inverted) s->pos = s->hi - s->pos + s->lo;
    }
    int inverted = s->inverted;
    if (notify) {
        tell(s, VT_SCROLLED, inverted, 0);
        play(s->win.parent, SND_BAR);
    }
    s->drag = -1;
    vcall0(s, VT_REDRAW);
}
void SCROLL_THIS scroll_on_mousewheel_down(struct game_scroll *s, int notify) { wheel(s, notify, 1); }
void SCROLL_THIS scroll_on_mousewheel_up(struct game_scroll *s, int notify) { wheel(s, notify, 0); }

// A key: End and Home (a horizontal bar) or Page Down and Page Up (a vertical one) move the position to
// an end or by a page. The flag the list boxes read is raised whatever the key. Answers whether the key
// was handled. The parent is told the mirror of the position for an inverted bar.
int SCROLL_THIS scroll_on_key_click(struct game_scroll *s, int a, int key) {
    (void)a;
    if (!s->win.parent) return 0;
    *game_scroll_by_key = 1;
    if (horizontal(s)) {
        if (key == 0x23) s->pos = s->hi;
        else if (key == 0x24) s->pos = s->lo;
        else return 0;
    } else {
        if (key == 0x22) {
            s->pos += s->paging;
            if (s->pos > s->hi) s->pos = s->hi;
        } else if (key == 0x21) {
            s->pos -= s->paging;
            if (s->pos < s->lo) s->pos = s->lo;
        } else {
            return 0;
        }
    }
    *game_win_cur = s->win.parent;
    int inverted = s->inverted;
    tell(s, VT_SCROLLED, inverted, 0);
    play(s->win.parent, SND_BAR);
    s->drag = -1;
    vcall0(s, VT_REDRAW);
    return 1;
}

// An arrow was clicked (1 for the one that lowers the position, 2 for the one that raises it): the press
// time is cleared, the flag the list boxes read is lowered, and the parent, the screen's function and the
// sound are told as for the wheel.
void SCROLL_THIS scroll_on_button_clicked(struct game_scroll *s, int which) {
    struct game_win *parent = s->win.parent;
    if (!parent) return;
    s->press_time = 0;
    *game_win_cur = parent;
    *game_scroll_by_key = 0;
    if (which == 1) {
        if (s->pos > s->lo) s->pos--;
    } else if (which == 2) {
        if (s->pos < s->hi) s->pos++;
    }
    int inverted = s->inverted;
    tell(s, VT_SCROLLED, inverted, 0);
    play(s->win.parent, SND_BAR);
    s->drag = -1;
    vcall0(s, VT_REDRAW);
}

// Size: the bar is as wide as the smaller of the two numbers; the second arrow is sized to a square of
// that less two when there is a frame, and moved to the far end (the first stays at the origin).
// Answers what the move does.
int SCROLL_THIS scroll_on_resize(struct game_scroll *s, int a, int b) {
    int t = a > b ? b : a;
    s->thickness = t;
    reset_thumb(s);
    int frame = s->border != -1;
    vcall3(s->btn_b, VT_RESIZE, t - 2 * frame, t - 2 * frame, 0);
    if (a > b) return win_move((struct game_win *)s->btn_b, a - t + frame, frame);
    return win_move((struct game_win *)s->btn_b, frame, b - t + frame);
}

// ---- Scroll: drawing ----------------------------------------------------------------------------------------

static struct game_buffer *win_buf(void *w) { return (struct game_buffer *)((uint8_t *)w + 0x444); }

// The arrows are redrawn, the bar filled (what is behind it if it may be seen through) and framed, the
// thumb filled, the bar's tile put over all of it, the grip picture centred on the thumb, and the thumb's
// bevel drawn a line at a time. The thumb rectangle is worked out on the way.
void SCROLL_THIS scroll_on_redraw(struct game_scroll *s) {
    vcall0(s->btn_a, VT_REDRAW);
    vcall0(s->btn_b, VT_REDRAW);
    graphic_win_fill(&s->win, s->back);
    if (s->border != -1) buffer_box_rect(&s->buf, s->buf.full, (uint8_t)s->border, (uint8_t)s->border);
    int32_t r[4];
    scroll_compute_thumb_rect(s, r);
    game_graphic_win_fill_rect(&s->win, r, s->back);
    if (s->tile) buffer_tile_2(&s->buf, s->tile, 0, 0, 0, 0, s->buf.width, -s->buf.neg_height);
    struct game_sprite *grip = s->grip;
    if (grip) {
        int dy = r[3] - grip->full_h - r[1], dx = r[2] - grip->full_w - r[0];
        int y = ((dy - (dy >> 31)) >> 1) + r[1], x = ((dx - (dx >> 31)) >> 1) + r[0];
        sprite_draw_2(grip, &s->buf, grip->key, x, y, 1, 1);
    }
    for (int i = 0; i < s->bevel; i++) {
        buffer_box_rect(&s->buf, r, s->bevel_upper, s->bevel_lower);
        r[0]++, r[2]--, r[1]++, r[3]--;
    }
}

// ---- ListBox -----------------------------------------------------------------------------------------------------

// A ListBox's Win and Dialog are found through its vbtable (entries 1 and 2).
static struct game_win *lb_win(struct game_listbox *l) { return (struct game_win *)((uint8_t *)l + l->vbptr[1]); }
static struct game_dlg *lb_dlg(struct game_listbox *l) { return (struct game_dlg *)((uint8_t *)l + l->vbptr[2]); }
// The handlers the Win's vtable holds get the Win, 0x48 into the box.
static struct game_listbox *lb_of(void *w) { return (struct game_listbox *)((uint8_t *)w - 0x48); }
// How many columns the list has, and how far one step of its bar goes (a whole column in a list with more
// than one).
static int32_t dlg_columns(struct game_dlg *d) { return *(int32_t *)((uint8_t *)d + 0x54); }
static int dlg_step(struct game_dlg *d) { return dlg_columns(d) > 1 ? *(int32_t *)((uint8_t *)d + 0x58) : 1; }

// Select the position (-1 for none) and bring it into view. Nothing happens if it is selected already, unless
// `force` (at 0x8) is set. With a position: the first shown item moves by steps until the selection shows
// (the bar is moved if it did and there are more items than room), the function a screen set is called with
// the item's id, and with `notify` the parent is told twice (slots 0xd8 and 0xd4) and its sound at 0x90
// plays. The box is redrawn whatever the position.
void SCROLL_THIS listbox_set_selected_pos(struct game_listbox *l, int pos, int notify) {
    struct game_dlg *d = lb_dlg(l);
    if (pos == d->sel && l->force == 0) return;
    d->sel = pos;
    if (pos >= 0) {
        int moved = 0;
        while (d->sel < l->top) {
            moved = 1;
            l->top -= dlg_step(d);
            if (l->top < 0) l->top = 0;
        }
        if (d->count <= l->visible) {
            l->top = 0;
        } else if (d->sel >= l->top + l->visible) {
            do l->top += dlg_step(d);
            while (d->sel >= l->top + l->visible);
            moved = 1;
        }
        if (moved && d->count > l->visible) {
            if (dlg_columns(d) > 1) win_set_horz_pos(lb_win(l), l->top);
            else win_set_vert_pos(lb_win(l), l->top);
        }
        if (l->on_select) l->on_select(dialog_get_selected_id(d));
        if (notify) {
            struct game_win *parent = lb_win(l)->parent;
            if (parent) vcall1(parent, VT_LIST_ID, dialog_pos_to_id(d, pos));
            parent = lb_win(l)->parent;
            if (parent) vcall2(parent, VT_LIST_POS, dialog_pos_to_id(d, pos), 0);
            parent = lb_win(l)->parent;
            if (parent) play(parent, SND_LIST);
        }
    }
    vcall0(lb_win(l), VT_REDRAW);
}

// Select the item with this id (by walking the list from the head, as the Dialog's lookup does), or none.
void SCROLL_THIS listbox_set_selected_id(struct game_listbox *l, int id) {
    struct game_dlg *d = lb_dlg(l);
    if (d->head) {
        d->idx = 0;
        d->cur = d->head;
        int n = d->count;
        for (int i = 0; i < n; i++) {
            if (d->cur->key == id) {
                listbox_set_selected_pos(l, d->idx, 1);
                return;
            }
            d->idx++;
            d->cur = d->cur->next;
        }
    }
    listbox_set_selected_pos(l, -1, 1);
}

// The text of a list node's item: the item (an object of the game's own) is at 0x8 of the node and its
// string at 0x4 of the item.
static const char *node_text(struct game_dlg_node *n) {
    if (!n) return NULL;
    uint8_t *item = *(uint8_t **)((uint8_t *)n + 8);
    return *(const char **)(item + 4);
}

// The position of the first item whose text equals this one (ignoring case), looking from the one after the
// cursor round the ring, or -1 (also with no items or no text). The cursor is put back where it was; if it
// was further back from the head than the list is long, the index is not.
int SCROLL_THIS listbox_find(struct game_listbox *l, const char *text) {
    struct game_dlg *d = lb_dlg(l);
    if (d->count == 0) return -1;
    if (!text) return -1;
    int idx = d->idx, found = -1;
    for (int i = 0; i < d->count; i++) {
        if (d->head) {
            d->cur = d->cur->next;
            if (++d->idx == d->count) d->idx = 0;
        }
        if (game_stricmp(d->cur ? node_text(d->cur) : NULL, text) == 0) {
            found = d->idx;
            break;
        }
    }
    if (idx > d->count - 1) return found;
    d->cur = d->head;
    if (idx < 0) {
        int n = idx < 0 ? -idx : idx;
        if (n > d->count) return found;
        for (; n > 0; n--) d->cur = d->cur->prev;
        d->idx = idx + d->count;
    } else {
        for (int n = idx; n > 0; n--) d->cur = d->cur->next;
        d->idx = idx;
    }
    return found;
}

// A scroll bar the box's Win owns moved: the box's Win is told as the bar's parent is (slot 0xc4).
void SCROLL_THIS listbox_on_scrolled(void *w, int a, int b) {
    struct game_listbox *l = lb_of(w);
    vcall2(lb_win(l), VT_SCROLLING, a, b);
}

// A right click or double click is a left one with the flag at 0x9b8eec raised while it is handled.
void SCROLL_THIS listbox_on_right_down(void *w, int x, int y) {
    *game_listbox_right = 1;
    vcall2(lb_win(lb_of(w)), VT_LEFT_DOWN, x, y);
    *game_listbox_right = 0;
}
void SCROLL_THIS listbox_on_right_double_click(void *w, int x, int y) {
    *game_listbox_right = 1;
    vcall2(lb_win(lb_of(w)), VT_LEFT_DOUBLE, x, y);
    *game_listbox_right = 0;
}

// A double click: the box's parent becomes the window being worked on, the function a screen set (at 0x40)
// gets the selected item's id, and the parent is told of it twice (slots 0xe0 and 0xdc, the second with the
// flag a right click raised) and its sound at 0x94 plays.
void SCROLL_THIS listbox_on_left_double_click(void *w, int x, int y) {
    (void)x, (void)y;
    struct game_listbox *l = lb_of(w);
    struct game_dlg *d = lb_dlg(l);
    *game_win_cur = lb_win(l)->parent;
    if (l->on_double) l->on_double(dialog_get_selected_id(d));
    struct game_win *parent = lb_win(l)->parent;
    if (parent) {
        vcall1(parent, VT_LIST_DOUBLE_B, dialog_get_selected_id(d));
        parent = lb_win(l)->parent;
        vcall2(parent, VT_LIST_DOUBLE_A, dialog_get_selected_id(d), *game_listbox_right);
        parent = lb_win(l)->parent;
        play(parent, SND_LIST_DOUBLE);
    }
}

// The wheel: a turn each way is passed to the Win's scroll bars, one by one (a negative number for down).
void SCROLL_THIS listbox_on_mousewheel(void *w, int delta) {
    struct game_win *win = lb_win(lb_of(w));
    if (delta < 0) {
        for (int n = -delta; n > 0; n--) game_win_wheel_down(win, 1);
    } else if (delta > 0) {
        for (int n = delta; n > 0; n--) game_win_wheel_up(win, 1);
    }
}

// The mouse left: no item is under it any more, and the box is redrawn.
void SCROLL_THIS listbox_on_mouse_leave(void *w, int x, int y) {
    (void)x, (void)y;
    struct game_listbox *l = lb_of(w);
    lb_dlg(l)->hover = -1;
    vcall0(lb_win(l), VT_REDRAW);
}

// ---- StringBox --------------------------------------------------------------------------------------------------

// The bar moved: the box keeps where the text starts (the second number) and is redrawn.
void SCROLL_THIS stringbox_on_scrolled(struct game_stringbox *b, int a, int pos) {
    (void)a;
    b->scroll = pos;
    vcall0(b, VT_REDRAW);
}
// A link was clicked: the parent, if it lets a child be copied from (slot 0xf4), is asked what to do with
// it (slot 0x160); otherwise the click is taken (1).
int SCROLL_THIS stringbox_on_link_clicked(struct game_stringbox *b, int a, int c) {
    struct game_win *parent = b->win.parent;
    if (parent && vcall0(parent, VT_PARENT_COPY)) return vcall2(b->win.parent, VT_LINK, a, c);
    return 1;
}

// ---- ListBox: layout and redraw ---------------------------------------------------------------------------------
//
// The Dialog part's numbers are those of the CheckBox's and RadioButton's layout (see button.c: 0x20 flags,
// 0x24 and 0x28 where the box's pixels are in its parent, 0x2c and 0x30 the width and height asked for, 0x34 the
// width of what an item has before its text, 0x38 and 0x3c the margins above and below, 0x40 the margin at
// the sides, 0x44 the gap, 0x48 the item height asked for or -1, 0x4c the columns asked for, 0x50 the most rows,
// then what the layout makes: 0x54 columns, 0x58 rows, 0x5c and 0x60 the box's width and height, 0x64 and 0x68
// an item's), and the list box's own: 0x6c the background colour, 0xac and 0xb0 the two colours of the frame
// drawn round it, 0xb4 how many frames (each a pixel inside the last). In the object: 0xc its flags (1 when
// the columns need a scroll bar to the side of them, 2 when the rows need one below), 0x18 the first item
// shown, 0x1c how many show, 0x20 how many items the box is meant to show, 0x2c a margin, 0x38 a function a
// screen may set to draw the background itself.

static struct game_buffer *lb_buf(struct game_listbox *l) { return (struct game_buffer *)((uint8_t *)lb_win(l) + 0x444); }

// Lay the items out: the width of a column from the widest text (or the width asked for), the height of
// an item, how many columns and rows there are and how much room they and the scroll bar take. The
// scroll bar's size is a global the game keeps. 7 if there are no items. Neither OpenSMACX nor
// Thinker has this.
int SCROLL_THIS listbox_calculate_dimensions(struct game_listbox *l) {
    struct game_dlg *d = lb_dlg(l);
    struct game_win *win = lb_win(l);
    struct game_buffer *buf = lb_buf(l);
    if (!d->count) return 7;
    l->flags = 0;
    dlg_text_setup(buf, d);
    DLG_F(d, 0x58) = d->count < DLG_F(d, 0x50) ? d->count : DLG_F(d, 0x50);
    int widest = 0;
    dlg_measure(buf, d, 0, &widest);
    if (!widest) widest = DLG_F(d, 0x2c);
    DLG_F(d, 0x64) = DLG_F(d, 0x44) + 2 * l->f2c + 2 + DLG_F(d, 0x34) + widest;
    DLG_F(d, 0x68) = DLG_F(d, 0x48) == -1 ? buffer_text_line_height(buf) + DLG_F(d, 0x44) : DLG_F(d, 0x48);
    if ((DLG_F(d, 0x20) & 2) && DLG_F(d, 0x2c)) {
        DLG_F(d, 0x5c) = DLG_F(d, 0x2c);
        DLG_F(d, 0x54) = DLG_F(d, 0x4c);
        if (DLG_F(d, 0x54) == 1) DLG_F(d, 0x64) = DLG_F(d, 0x5c) - 2 * DLG_F(d, 0x40);
        else DLG_F(d, 0x64) -= DLG_F(d, 0x44);
    } else {
        int cols = DLG_F(d, 0x4c);
        if (cols == 1) {
            int t = DLG_F(d, 0x64) + 2 * DLG_F(d, 0x40);
            DLG_F(d, 0x5c) = DLG_F(d, 0x2c) > t ? DLG_F(d, 0x2c) : t;
            DLG_F(d, 0x64) = DLG_F(d, 0x5c) - 2 * DLG_F(d, 0x40);
        } else {
            DLG_F(d, 0x64) = (DLG_F(d, 0x5c) - 2 * DLG_F(d, 0x40) + DLG_F(d, 0x44)) / cols - DLG_F(d, 0x44);
            int v = (DLG_F(d, 0x64) + DLG_F(d, 0x44)) * cols + 2 * DLG_F(d, 0x40) - DLG_F(d, 0x44);
            DLG_F(d, 0x5c) = DLG_F(d, 0x2c) > v ? DLG_F(d, 0x2c) : v;
        }
        DLG_F(d, 0x54) = DLG_F(d, 0x4c);
    }
    if ((DLG_F(d, 0x20) & 1) && DLG_F(d, 0x30)) {
        DLG_F(d, 0x60) = DLG_F(d, 0x30);
        DLG_F(d, 0x58) = (DLG_F(d, 0x60) - DLG_F(d, 0x3c) - DLG_F(d, 0x38) + DLG_F(d, 0x44)) / DLG_F(d, 0x68);
        if (!(win->flags & 0x40) && DLG_F(d, 0x58) * DLG_F(d, 0x54) < l->f20) {
            if (DLG_F(d, 0x54) > 1) {
                DLG_F(d, 0x60) = DLG_F(d, 0x30) - *game_listbox_bar;
                DLG_F(d, 0x58) = DLG_F(d, 0x60) / DLG_F(d, 0x68);
            } else {
                DLG_F(d, 0x5c) = DLG_F(d, 0x2c) - *game_listbox_bar;
                DLG_F(d, 0x54) = 1;
            }
        }
    } else {
        DLG_F(d, 0x60) = DLG_F(d, 0x68) * DLG_F(d, 0x58) - DLG_F(d, 0x44) + DLG_F(d, 0x3c) + DLG_F(d, 0x38);
    }
    DLG_F(d, 0x68) -= DLG_F(d, 0x44);
    int fit = (DLG_F(d, 0x60) - DLG_F(d, 0x3c) - DLG_F(d, 0x38) + DLG_F(d, 0x44)) / (DLG_F(d, 0x68) + DLG_F(d, 0x44));
    if (d->count <= fit) {
        if (DLG_F(d, 0x20) & 2) DLG_F(d, 0x64) = (DLG_F(d, 0x5c) - 2 * DLG_F(d, 0x40) + DLG_F(d, 0x44)) / DLG_F(d, 0x54) - DLG_F(d, 0x44);
        else DLG_F(d, 0x64) = DLG_F(d, 0x5c) - 2 * DLG_F(d, 0x40);
    }
    l->visible = DLG_F(d, 0x58) * DLG_F(d, 0x54);
    if (l->f20 > l->visible) {
        if (DLG_F(d, 0x54) > 1) {
            if (!(win->flags & 0x40)) DLG_F(d, 0x60) += *game_listbox_bar;
            l->flags |= 1;
        } else {
            if (!(win->flags & 0x40)) DLG_F(d, 0x5c) += *game_listbox_bar;
            if (DLG_F(d, 0x20) & 2) DLG_F(d, 0x64) -= *game_listbox_bar / DLG_F(d, 0x54);
            l->flags |= 2;
        }
    }
    return 0;
}

// The redraw (the Win's slot, so it gets the Win, 0x48 into the box): the clip is the whole Buffer, then a
// function a screen set (called with the Win and three zeros) or else the parent (slot 0x28) may draw the
// background; if neither does, it is copied from the parent's Buffer where the window is see-through
// (flag 0x80000) and has a parent, and filled with the colour otherwise. Then the frames, and the items
// from the first shown, column by column: as many as there are rows in a column (a column that has run out
// of items goes on calling for ones past the end, which draw nothing). Nothing is drawn without a parent.
void SCROLL_THIS listbox_on_redraw(void *w) {
    struct game_listbox *l = (struct game_listbox *)((uint8_t *)w - 0x48);
    struct game_win *win = lb_win(l);
    struct game_win *parent = win->parent;
    if (!parent) return;
    *game_win_cur = parent;
    struct game_buffer *buf = lb_buf(l);
    struct game_dlg *d = lb_dlg(l);
    buffer_set_clip(buf, buf->full);
    int drawn = 0;
    if (l->f38) drawn = ((int(__attribute__((cdecl)) *)(void *, int, int, int))l->f38)(win, 0, 0, 0);
    if (!drawn) drawn = vcall1(parent, VT_WIN_BACK, (int)(intptr_t)win);
    if (!drawn) {
        if ((win->flags & 0x80000) && win->parent)
            buffer_copy_full((struct game_buffer *)((uint8_t *)win->parent + 0x444), buf, DLG_F(d, 0x24), DLG_F(d, 0x28), 0, 0, buf->width,
                             -buf->neg_height);
        else graphic_win_fill(win, DLG_F(d, 0x6c));
    }
    int32_t r[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
    for (int i = 0; i < DLG_F(d, 0xb4); i++) {
        buffer_box_rect(buf, r, DLG_F(d, 0xac), DLG_F(d, 0xb0));
        r[0]++, r[1]++, r[2]--, r[3]--;
    }
    if (!d->count) return;
    strlist_seek_pos((struct game_strlist *)((uint8_t *)d + 0xbc), l->top - 1);
    *game_listbox_row = 0;
    for (int col = 0; col < DLG_F(d, 0x54); col++) {
        for (int row = 0; row < DLG_F(d, 0x58); row++) {
            listbox_draw_item(l, l->top + *game_listbox_row);
            ++*game_listbox_row;
            if (*game_listbox_row == d->count - l->top) break;
        }
    }
}

// ---- ListBox: drawing an item -----------------------------------------------------------------------------------
//
// More of the object: 0x4 whether the items keep a selected flag of their own (at 0x8 of the item; else the
// Dialog's selected position is the one), 0x10 the colour of the text of a selected item (-1 for the
// background's), 0x14 the colour a selected item is filled with when the box has the dialog's focus (-1 for
// none), 0x24 and 0x28 the colours of the frame round a selected item, which is 0x2c pixels thick, and
// 0x30, 0x34 and 0x38 three functions a screen may set. An item has its text at 0x4 and how many rows it takes
// at 0xc; the box's rows are drawn from the top margin down, an item below the ones before it by what they
// take (with one column) or in the column its position says and at the row in it (with more).

static struct game_strlist *lb_list(struct game_dlg *d) { return (struct game_strlist *)((uint8_t *)d + 0xbc); }
static struct game_list_item *lb_item(struct game_strlist *s) { return (struct game_list_item *)s->cur->item; }
static int lb_rows(struct game_strlist *s) { return s->head ? lb_item(s)->rows : 0; }
static int vcall4(void *o, int slot, int a, int b, int c, int d) {
    return ((int(THISCALL *)(void *, int, int, int, int))vslot(o, slot))(o, a, b, c, d);
}

// The item's text, in what the rectangle `r` leaves after the frame of a selected item and the picture's
// width: wrapped if the item takes more than one row, else on one line; what follows a `|` is on the right.
static void lb_item_text(struct game_listbox *l, struct game_buffer *buf, struct game_dlg *d, int32_t *r) {
    struct game_strlist *s = lb_list(d);
    int edge = l->f2c + 1;
    r[2] -= edge;
    r[0] += DLG_F(d, 0x34) + edge;
    buffer_set_clip(buf, r);
    char *text = s->head ? lb_item(s)->text : NULL;
    char *bar = text ? strchr(text, '|') : NULL;
    int rows = lb_rows(s);
    if (bar) *bar = 0;
    if (rows > 1) buffer_wrap_2(buf, text, r);
    else if (text) buffer_write_l_4(buf, text, r, (int)strlen(text));
    if (bar) {
        *bar = '|';
        buffer_write_right_l_6(buf, bar + 1, r, (int)strlen(bar + 1));
    }
    r[0] -= edge + DLG_F(d, 0x34);
    r[2] += edge;
    buffer_set_clip(buf, r);
}

// Draw item `pos` (nothing for a negative one or one past the last). Its rectangle is worked out first
// and made the clip. A function a screen set (0x38) may draw the item (given the Win, the item's id, the
// first shown plus the draw counter, and the rectangle); if not, the parent may (slot 0x2c, with the Win,
// the id, the state - 0 or 1 for the mouse over it, 2 for selected - and the rectangle), else the
// window's pixels are taken from the parent's if it can be seen through (or filled), and the frame is drawn.
// Then the other functions are told (0x30: the Win, id, first shown plus counter, left, top, height;
// 0x34: the Win, id, position, rectangle and text, which may draw the rest itself). A selected item gets a
// frame; an item in a box with the dialog's focus its fill colour first. The text colours and Fonts are those
// of the layout, with the colours set again for the selection (the selected ones' text takes its colour from
// the box or the background). Neither OpenSMACX nor
// Thinker has this.
void SCROLL_THIS listbox_draw_item(struct game_listbox *l, int pos) {
    if (pos < 0) return;
    struct game_dlg *d = lb_dlg(l);
    struct game_win *win = lb_win(l);
    struct game_buffer *buf = lb_buf(l);
    struct game_strlist *s = lb_list(d);
    int32_t r[4] = {DLG_F(d, 0x40), DLG_F(d, 0x38), DLG_F(d, 0x64) - DLG_F(d, 0x40), DLG_F(d, 0x38) + DLG_F(d, 0x68)};
    int step = DLG_F(d, 0x68) + DLG_F(d, 0x44);
    if (DLG_F(d, 0x4c) == 1) {
        int before = 0;
        strlist_seek_pos(s, l->top - 1);
        for (int n = pos - l->top; n > 0; n--) {
            strlist_next_entry(s);
            if (s->cur) before += lb_rows(s);
        }
        r[1] += step * before, r[3] += step * before;
        strlist_next_entry(s);
        r[3] = step * (s->cur ? lb_rows(s) : 0) - DLG_F(d, 0x44) + r[1];
    } else {
        int rel = pos - l->top, rows = DLG_F(d, 0x58), colw = DLG_F(d, 0x64) + DLG_F(d, 0x44);
        r[0] += colw * (rel / rows), r[2] += colw * (rel / rows);
        r[1] = DLG_F(d, 0x38) + step * (rel % rows), r[3] += step * (rel % rows);
    }
    if (pos >= d->count) return;
    int selected;
    if (!l->f4) {
        selected = DLG_F(d, 0xec) == pos;
    } else {
        strlist_seek_pos(s, pos);
        selected = lb_item(s)->lines;
    }
    buffer_set_clip(buf, r);
    int drawn = 0;
    if (l->f38) {
        int id = dialog_pos_to_id(d, pos);
        drawn = ((int(__attribute__((cdecl)) *)(void *, int, int, int32_t *))(intptr_t)l->f38)(win, id, l->top + *game_listbox_row, r);
    }
    dlg_text_setup(buf, d);
    if (!selected) {
        buffer_set_text_color(buf, DLG_F(d, 0x7c), DLG_F(d, 0x88), DLG_F(d, 0x94), DLG_F(d, 0xa0));
        buffer_set_text_color2(buf, DLG_F(d, 0x80), DLG_F(d, 0x8c), DLG_F(d, 0x98), DLG_F(d, 0xa4));
        buffer_set_text_color3(buf, DLG_F(d, 0x84), DLG_F(d, 0x90), DLG_F(d, 0x9c), DLG_F(d, 0xa8));
    } else {
        buffer_set_clip(buf, r);
        int c = l->f10 == -1 ? DLG_F(d, 0x6c) : l->f10;
        buffer_set_text_color(buf, c, DLG_F(d, 0x88), DLG_F(d, 0x94), DLG_F(d, 0xa0));
        buffer_set_text_color2(buf, c, DLG_F(d, 0x8c), DLG_F(d, 0x98), DLG_F(d, 0xa4));
        buffer_set_text_color3(buf, c, DLG_F(d, 0x90), DLG_F(d, 0x9c), DLG_F(d, 0xa8));
    }
    int id = dialog_pos_to_id(d, pos);
    if (!drawn) {
        struct game_win *parent = win->parent;
        if (parent) {
            drawn = vcall4(parent, VT_ITEM_BACK, (int)(intptr_t)win, id, selected ? 2 : DLG_F(d, 0xf0) == pos, (int)(intptr_t)r);
            if (!drawn) {
                if ((win->flags & 0x80000) && win->parent)
                    buffer_copy_full((struct game_buffer *)((uint8_t *)win->parent + 0x444), buf, DLG_F(d, 0x24), DLG_F(d, 0x28), 0, 0,
                                     buf->width, -buf->neg_height);
                else graphic_win_fill(win, DLG_F(d, 0x6c));
            }
        }
        if (!drawn) {
            int32_t f[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
            for (int i = 0; i < DLG_F(d, 0xb4); i++) {
                buffer_box_rect(buf, f, DLG_F(d, 0xac), DLG_F(d, 0xb0));
                f[0]++, f[1]++, f[2]--, f[3]--;
            }
        }
    }
    strlist_seek_pos(s, pos);
    if (l->f30) {
        *game_listbox_cur = (int32_t)(intptr_t)l;
        ((void(__attribute__((cdecl)) *)(void *, int, int, int, int, int))(intptr_t)l->f30)(win, id, l->top + *game_listbox_row, r[0], r[1],
                                                                                               r[3] - r[1]);
    }
    if (l->f34) {
        char *text = s->head ? lb_item(s)->text : NULL;
        if (((int(__attribute__((cdecl)) *)(void *, int, int, int32_t *, char *))(intptr_t)l->f34)(win, id, pos, r, text)) return;
    }
    if (!selected) {
        lb_item_text(l, buf, d, r);
        return;
    }
    if (!drawn && win_is_dialog_focus(win)) {
        buffer_set_clip(buf, r);
        if (l->f14 != -1) game_graphic_win_fill_rect(win, r, l->f14);
    }
    lb_item_text(l, buf, d, r);
    if (!drawn) {
        for (int i = 0; i < l->f2c; i++) {
            buffer_box_rect(buf, r, l->f24, l->f28);
            r[0]++, r[1]++, r[2]--, r[3]--;
        }
    }
}

// ---- Shadow checks -------------------------------------------------------------------------------------------------

#define P(p) ((int)(uintptr_t)(p))

// The stand-ins a sweep gives bars, boxes and parents (see below): what they leave behind is a count and a
// hash of their calls and arguments, so a different call, argument or order shows as a difference in these
// eight bytes.
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }

// The scratch objects the sweep builds, all in one block so one span covers what a function can change in them.
enum { NS = 4, NP = 3, NL = 2, NN = 6, NSB = 2, LBSIZE = 0xb60 };
struct scratch {
    struct game_scroll scroll[NS];
    struct game_win parent[NP];
    uint8_t sound[2][16];
    uint8_t lbox[NL][LBSIZE];  // a ListBox: the vbtable pointer, the Win at 0x48 and the Dialog at 0xa60
    struct game_dlg_node nodes[NL][NN];
    uint8_t items[NL][NN][16];
    char text[NL][NN][8];
    int32_t vbtable[NL][4];
    struct game_stringbox sbox[NSB];
};
static struct scratch S;
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, &S, sizeof S);
    writes_vlog(w);
}
// The globals the handlers write.
static void writes_globals(struct hook_writes *w) {
    hooks_writes_add(w, game_win_cur, 4);
    hooks_writes_add(w, game_win_capture, 4);
    hooks_writes_add(w, game_win_hover, 4);
    hooks_writes_add(w, game_scroll_by_key, 4);
    hooks_writes_add(w, game_listbox_right, 4);
}

// A check that can't be run on the game's own objects (see the top): in shadow mode the game's calls
// run the original (and are counted, to start the sweep); the sweep calls check_NAME.
#define SWEEP_ONLY(ret, name, params, args, writes, fmt, ...)             \
    SHADOW_DECLARE_T(ret, name, params)                                   \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__) \
    static ret THISCALL shadow_##name params {                            \
        if (!hooks_nested && !shadow_bypass()) {                          \
            stats_##name.calls++;                                         \
            shadow_tick(SCROLL);                                          \
        }                                                                 \
        SHADOW_TAIL_##ret(orig_##name args);                              \
    }

#define BAR_STATE(s) WRITES(&(s)->lo, 16)
// A bar's colours: its own and the arrows'.
#define ARROW(s, which, off) ((uint8_t *)((which) ? (s)->btn_b : (s)->btn_a) + (off))
SHADOW_WRITER_T(SCROLL, void, scroll_set_range, (struct game_scroll * s, int lo, int hi), (s, lo, hi),
                (BAR_STATE(s), writes_vlog(&w_)), "%d, %d, %d", P(s), lo, hi)
#define ARROW_COLOR(field, off)                                                                        \
    (WRITES(&(s)->field, 4), WRITES(ARROW(s, 0, off), 4), WRITES(ARROW(s, 1, off), 4), writes_vlog(&w_))
SHADOW_WRITER_T(SCROLL, void, scroll_set_button_color, (struct game_scroll * s, int c), (s, c), ARROW_COLOR(button_color, 0xa84), "%d, %d",
                P(s), c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_bevel_thickness, (struct game_scroll * s, int c), (s, c), ARROW_COLOR(bevel, 0xa90), "%d, %d",
                P(s), c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_bevel_upper, (struct game_scroll * s, int c), (s, c), ARROW_COLOR(bevel_upper, 0xa88),
                "%d, %d", P(s), c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_bevel_lower, (struct game_scroll * s, int c), (s, c), ARROW_COLOR(bevel_lower, 0xa8c),
                "%d, %d", P(s), c)
#define THUMB_WRITES(s) (WRITES(&(s)->bar, sizeof (s)->bar))
SHADOW_WRITER_T(SCROLL, void, scroll_set_border_color, (struct game_scroll * s, int c), (s, c), (WRITES(&s->border, 4), THUMB_WRITES(s)),
                "%d, %d", P(s), c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_bar_thickness, (struct game_scroll * s, int n), (s, n),
                (WRITES(&s->thickness, 4), THUMB_WRITES(s)), "%d, %d", P(s), n)
SHADOW_WRITER_T(SCROLL, void, scroll_set_thumb_rect, (struct game_scroll * s), (s), THUMB_WRITES(s), "%d", P(s))
#define PICTURE_WRITES(s, slot, which) (WRITES(s->spr[slot], 12), WRITES(ARROW(s, which, 0xb10), 12))
SHADOW_WRITER_T(SCROLL, void, scroll_set_sprite_left, (struct game_scroll * s, int a, int b, int c), (s, a, b, c),
                PICTURE_WRITES(s, 0, 0), "%d, %d, %d, %d", P(s), a, b, c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_sprite_right, (struct game_scroll * s, int a, int b, int c), (s, a, b, c),
                PICTURE_WRITES(s, 2, 1), "%d, %d, %d, %d", P(s), a, b, c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_sprite_up, (struct game_scroll * s, int a, int b, int c), (s, a, b, c),
                PICTURE_WRITES(s, 1, 0), "%d, %d, %d, %d", P(s), a, b, c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_sprite_down, (struct game_scroll * s, int a, int b, int c), (s, a, b, c),
                PICTURE_WRITES(s, 3, 1), "%d, %d, %d, %d", P(s), a, b, c)
SHADOW_WRITER_T(SCROLL, void, scroll_set_pos, (struct game_scroll * s, int pos), (s, pos),
                (WRITES(&s->pos, 4), WRITES(game_win_cur, 4), writes_vlog(&w_)), "%d, %d", P(s), pos)
SHADOW_WRITER_T(SCROLL, void, scroll_compute_thumb_rect, (struct game_scroll * s, int32_t *out), (s, out),
                (WRITES(out, 16), WRITES(&s->drag, 4), THUMB_WRITES(s)), "%d, %d", P(s), P(out))

SWEEP_ONLY(void, scroll_on_left_down, (struct game_scroll * s, int x, int y), (s, x, y), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d, %d", P(s), x, y)
SWEEP_ONLY(void, scroll_on_left_up, (struct game_scroll * s, int x, int y), (s, x, y), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d, %d", P(s), x, y)
SWEEP_ONLY(void, scroll_on_mousewheel_down, (struct game_scroll * s, int n), (s, n), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d", P(s), n)
SWEEP_ONLY(void, scroll_on_mousewheel_up, (struct game_scroll * s, int n), (s, n), (writes_scratch(&w_), writes_globals(&w_)), "%d, %d",
           P(s), n)
SWEEP_ONLY(int, scroll_on_key_click, (struct game_scroll * s, int a, int key), (s, a, key),
           (writes_scratch(&w_), writes_globals(&w_)), "%d, %d, %d", P(s), a, key)
SWEEP_ONLY(void, scroll_on_button_clicked, (struct game_scroll * s, int which), (s, which),
           (writes_scratch(&w_), writes_globals(&w_)), "%d, %d", P(s), which)
SWEEP_ONLY(int, scroll_on_resize, (struct game_scroll * s, int a, int b), (s, a, b), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d, %d", P(s), a, b)

SWEEP_ONLY(void, listbox_set_selected_id, (struct game_listbox * l, int id), (l, id), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d", P(l), id)
SWEEP_ONLY(void, listbox_set_selected_pos, (struct game_listbox * l, int pos, int notify), (l, pos, notify),
           (writes_scratch(&w_), writes_globals(&w_)), "%d, %d, %d", P(l), pos, notify)
#define DLG_CURSOR(d) WRITES(&(d)->cur, 0xd4 - 0xc8)
SHADOW_WRITER_T(SCROLL, int, listbox_find, (struct game_listbox * l, const char *text), (l, text), DLG_CURSOR(lb_dlg(l)), "%d, %d", P(l),
                P(text))
SWEEP_ONLY(void, listbox_on_scrolled, (void *w, int a, int b), (w, a, b), (writes_scratch(&w_), writes_globals(&w_)), "%d, %d, %d",
           P(w), a, b)
SWEEP_ONLY(void, listbox_on_right_down, (void *w, int x, int y), (w, x, y), (writes_scratch(&w_), writes_globals(&w_)), "%d, %d, %d",
           P(w), x, y)
SWEEP_ONLY(void, listbox_on_right_double_click, (void *w, int x, int y), (w, x, y), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d, %d", P(w), x, y)
SWEEP_ONLY(void, listbox_on_left_double_click, (void *w, int x, int y), (w, x, y), (writes_scratch(&w_), writes_globals(&w_)),
           "%d, %d, %d", P(w), x, y)
SWEEP_ONLY(void, listbox_on_mousewheel, (void *w, int delta), (w, delta), (writes_scratch(&w_), writes_globals(&w_)), "%d, %d", P(w),
           delta)
SHADOW_WRITER_T(SCROLL, void, listbox_on_mouse_leave, (void *w, int x, int y), (w, x, y),
                (WRITES(&lb_dlg(lb_of(w))->hover, 4), writes_vlog(&w_)), "%d, %d, %d", P(w), x, y)

SHADOW_WRITER_T(SCROLL, void, stringbox_on_scrolled, (struct game_stringbox * b, int a, int pos), (b, a, pos),
                (WRITES(&b->scroll, 4), writes_vlog(&w_)), "%d, %d, %d", P(b), a, pos)
SWEEP_ONLY(int, stringbox_on_link_clicked, (struct game_stringbox * b, int a, int c), (b, a, c), (writes_scratch(&w_)),
           "%d, %d, %d", P(b), a, c)

// ---- Checks of drawing -------------------------------------------------------------------------------------------------
//
// Drawing changes the Buffer's own object and pixels. A call is only checked for Buffers with a DIB: the
// original's use of a DirectDraw surface can't be put back.

static void writes_pixels(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, b, sizeof *b);
    if (!b->bits || b->dd) return;
    int64_t rows = -(int64_t)b->neg_height, pitch = b->pitch;
    if (rows <= 0 || pitch == 0) return;
    uint8_t *a = b->bits, *c = b->bits + pitch * (rows - 1);
    hooks_writes_add(w, a < c ? a : c, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}
static int win_dd(struct game_win *w) { return win_buf(w)->dd || (w->parent && win_buf(w->parent)->dd); }

SHADOW_DECLARE_T(void, scroll_on_redraw, (struct game_scroll * s))
SHADOW_WRITER_BODY(void, scroll_on_redraw, (struct game_scroll * s), (s),
                   (writes_pixels(&w_, &s->buf), THUMB_WRITES(s), WRITES(&s->drag, 4), writes_vlog(&w_)), "%d", P(s))
static void THISCALL shadow_scroll_on_redraw(struct game_scroll *s) {
    if (hooks_nested || win_dd(&s->win)) {
        orig_scroll_on_redraw(s);
        return;
    }
    if (shadow_bypass()) {
        scroll_on_redraw(s);
        return;
    }
    stats_scroll_on_redraw.calls++;
    shadow_tick(SCROLL);
    check_scroll_on_redraw(s);
}

// ---- Sweep ---------------------------------------------------------------------------------------------------------------
//
// Scratch bars, boxes and parents in static memory (S), with a random state each round, and a stand-in
// vtable for all of them whose slots only log the call (see vlog) and answer a number of their own, so
// what a function calls, and with what, is compared. Sounds on the parents are objects with the same
// vtable; the functions a bar holds log too.

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
STUB3(0xc) STUB0(0x10) STUB0(0x1c) STUB2(0x44) STUB2(0xc0) STUB2(0xc4) STUB2(0xd4) STUB1(0xd8)
STUB2(0xdc) STUB1(0xe0) STUB0(0xf8) STUB2(0x160)
// The left click and double click handlers see the flag a right click raises, so they log it.
static int THISCALL stub_0x50(void *o, int a, int b) { return vrec(0x50, o, a, b, *game_listbox_right), 0x51; }
static int THISCALL stub_0x70(void *o, int a, int b) { return vrec(0x70, o, a, b, *game_listbox_right), 0x71; }
// A parent that lets a child copy from it if it has flag 1 (a coin tossed when the sweep built it).
static int THISCALL stub_0xf4(void *o) { return vrec(0xf4, o, 0, 0, 0), (((struct game_win *)o)->flags & 1) != 0; }
// A sound that is busy every other time it is asked.
static int THISCALL stub_0x5c(void *o) { return vrec(0x5c, o, 0, 0, 0), vlog.n & 1; }
// The parent's slot for a list box's background answers 0 and 1 by turns, so both ways are taken.
static int THISCALL stub_0x28(void *o, int a) { return vrec(0x28, o, a, 0, 0), vlog.n & 1; }
// ... and for an item's (the original ListBox_draw_item asks, with the Win, the item's id, the state and its rectangle).
static int THISCALL stub_0x2c(void *o, int a, int b, int c, const int32_t *r) {
    return vrec(0x2c, o, a * 3 + b, c, r[0] * 31 + r[1] * 131 + r[2] * 1031 + r[3] * 10007), vlog.n & 1;
}
static void *stub_vt[0x180 / 4];
static void fill_stub_vt(void) {
    stub_vt[0xc / 4] = (void *)stub_0xc, stub_vt[0x10 / 4] = (void *)stub_0x10, stub_vt[0x1c / 4] = (void *)stub_0x1c;
    stub_vt[0x44 / 4] = (void *)stub_0x44, stub_vt[0x50 / 4] = (void *)stub_0x50, stub_vt[0x5c / 4] = (void *)stub_0x5c;
    stub_vt[0x70 / 4] = (void *)stub_0x70, stub_vt[0xc0 / 4] = (void *)stub_0xc0, stub_vt[0xc4 / 4] = (void *)stub_0xc4;
    stub_vt[0xd4 / 4] = (void *)stub_0xd4, stub_vt[0xd8 / 4] = (void *)stub_0xd8, stub_vt[0xdc / 4] = (void *)stub_0xdc;
    stub_vt[0xe0 / 4] = (void *)stub_0xe0, stub_vt[0xf4 / 4] = (void *)stub_0xf4, stub_vt[0xf8 / 4] = (void *)stub_0xf8;
    stub_vt[0x160 / 4] = (void *)stub_0x160;
    stub_vt[0x28 / 4] = (void *)stub_0x28;
    stub_vt[0x2c / 4] = (void *)stub_0x2c;
}
static void cb_click(int id, int pos) { vrec(0x401, NULL, id, pos, 0); }
static void cb_drag(int id, int pos) { vrec(0x402, NULL, id, pos, 0); }
static void cb_select(int id) { vrec(0x403, NULL, id, 0, 0); }
static void cb_double(int id) { vrec(0x404, NULL, id, 0, 0); }

static struct game_win *any_parent(void) { return rnd_to(10) ? &S.parent[rnd_to(NP)] : NULL; }

static uint8_t *lb_win_at(int k) { return S.lbox[k] + S.vbtable[k][1]; }
static void build(void) {
    memset(&S, 0, sizeof S);
    for (int p = 0; p < NP; p++) {
        S.parent[p].vtable = stub_vt;
        S.parent[p].flags = (uint32_t)rnd_to(2);
        static const int snd[] = {SND_BAR, SND_BAR_DRAG, SND_LIST, SND_LIST_DOUBLE};
        for (int i = 0; i < 4; i++) {
            void *s = rnd_to(3) ? S.sound[rnd_to(2)] : NULL;
            memcpy((uint8_t *)&S.parent[p] + snd[i], &s, sizeof s);
        }
    }
    for (int k = 0; k < 2; k++) *(void ***)S.sound[k] = stub_vt;
    for (int i = 0; i < NS; i++) {
        struct game_scroll *s = &S.scroll[i];
        s->win.vtable = stub_vt;
        s->win.parent = rnd_to(12) ? &S.parent[rnd_to(NP)] : NULL;
        *(void ***)s->btn_a = stub_vt, *(void ***)s->btn_b = stub_vt;
        s->flags = (uint32_t)rnd_to(4), s->id = rnd_to(12) - 2;
        s->border = rnd_to(3) ? rnd_to(256) : -1;
        int len = 20 + rnd_to(80), thick = 8 + rnd_to(10);
        s->buf.width = rnd_to(2) ? len : thick, s->buf.neg_height = -(s->buf.width == len ? thick : len);
        if (rnd_to(10) == 0) s->buf.width = thick, s->buf.neg_height = -thick;  // (a square)
        s->lo = rnd_to(40) - 10, s->hi = s->lo + rnd_to(80) - (rnd_to(8) == 0 ? 5 : 0);
        s->inverted = rnd_to(3) == 0;
        s->pos = rnd_to(8) ? s->lo + rnd_to(s->hi - s->lo + 1 > 0 ? s->hi - s->lo + 1 : 1) : rnd_to(120) - 20;
        s->delay = rnd_to(300), s->rate = 1 + rnd_to(30), s->press_time = rnd_to(3) ? rnd() : 0;
        s->drag = rnd_to(2) ? -1 : rnd_to(60), s->paging = rnd_to(12), s->dir = rnd_to(3) - 1;
        s->thickness = thick, s->button_color = (int)rnd();
        s->bar[0] = rnd_to(4), s->bar[1] = rnd_to(4), s->bar[2] = s->bar[0] + thick, s->bar[3] = s->bar[1] + thick + rnd_to(3);
        s->on_click = rnd_to(2) ? cb_click : NULL, s->on_drag = rnd_to(2) ? cb_drag : NULL;
        for (int a = 0; a < 4; a++)
            for (int b = 0; b < 3; b++) s->spr[a][b] = (int)rnd();
    }
    for (int k = 0; k < NL; k++) {
        S.vbtable[k][1] = 0x48, S.vbtable[k][2] = 0xa60;
        *(int32_t **)S.lbox[k] = S.vbtable[k];
        struct game_listbox *l = (struct game_listbox *)S.lbox[k];
        uint8_t *win = lb_win_at(k);
        *(void ***)win = stub_vt;
        struct game_win *parent = any_parent();
        memcpy(win + 0xc4, &parent, sizeof parent);
        struct game_scroll *sv = rnd_to(2) ? &S.scroll[rnd_to(NS)] : NULL, *sh = rnd_to(2) ? &S.scroll[rnd_to(NS)] : NULL;
        memcpy(win + 0x43c, &sv, sizeof sv);
        memcpy(win + 0x440, &sh, sizeof sh);
        struct game_dlg *d = (struct game_dlg *)(S.lbox[k] + S.vbtable[k][2]);
        int n = rnd_to(NN + 1);
        for (int i = 0; i < n; i++) {
            S.nodes[k][i].key = rnd_to(8) - 2;
            S.nodes[k][i].next = &S.nodes[k][(i + 1) % n], S.nodes[k][i].prev = &S.nodes[k][(i + n - 1) % n];
            static const char *const words[] = {"abc", "ABC", "Abd", "xyz", "", "Xy"};
            strcpy(S.text[k][i], words[rnd_to(6)]);
            char *tp = S.text[k][i];
            memcpy(S.items[k][i] + 4, &tp, sizeof tp);
            uint8_t *ip = S.items[k][i];
            memcpy((uint8_t *)&S.nodes[k][i] + 8, &ip, sizeof ip);
        }
        d->count = !n ? 0 : rnd_to(8) ? n : 1 + rnd_to(NN);
        d->head = n ? &S.nodes[k][0] : NULL;
        d->cur = n ? &S.nodes[k][rnd_to(n)] : NULL, d->idx = n ? rnd_to(n) : rnd_to(NN);
        d->sel = rnd_to(2 * NN + 3) - NN - 1;
        d->hover = rnd_to(NN + 2) - 1;
        *(int32_t *)((uint8_t *)d + 0x54) = 1 + rnd_to(3), *(int32_t *)((uint8_t *)d + 0x58) = 1 + rnd_to(4);
        l->force = rnd_to(3) == 0, l->top = rnd_to(n + 1), l->visible = 1 + rnd_to(4);
        l->on_select = rnd_to(2) ? cb_select : NULL, l->on_double = rnd_to(2) ? cb_double : NULL;
    }
    for (int i = 0; i < NSB; i++) {
        S.sbox[i].win.vtable = stub_vt;
        S.sbox[i].win.parent = any_parent();
        S.sbox[i].scroll = rnd_to(50);
    }
}

// The drawing function on a scratch bar with a Buffer of its own (hand-built pixels in static memory), a
// parent with a Buffer, and sprites of a few colours.
// The ListBox's layout calls no parent and is checked as it is called, on the Dialog part's numbers, the
// cursor and the box's flags and count, and on the Buffer's object and device context (the text colours and
// Fonts it sets). Its redraw calls the screen's function and the parent, and draws the items with
// the ListBox_draw_item, which calls the screen's functions and the parent too, so the game's calls run the
// original and the sweep checks them on scratch boxes (LX, below).
static void writes_text_state(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    int32_t mem = *(int32_t *)((uint8_t *)b + 0x64), now = *(int32_t *)((uint8_t *)b + 0x60);
    hooks_writes_dc(w, (void *)(intptr_t)mem);
    if (now != mem) hooks_writes_dc(w, (void *)(intptr_t)now);
    if (game_font_dc) hooks_writes_dc(w, *game_font_dc);
}
static void writes_lb_layout(struct hook_writes *w, struct game_listbox *l) {
    struct game_dlg *d = lb_dlg(l);
    struct game_buffer *buf = lb_buf(l);
    hooks_writes_add(w, (uint8_t *)d + 0x34, 0xd4 - 0x34);
    hooks_writes_add(w, &l->flags, 4);
    hooks_writes_add(w, &l->visible, 4);
    hooks_writes_add(w, buf, sizeof *buf);
    hooks_writes_dc(w, (void *)(intptr_t) * (int32_t *)((uint8_t *)buf + 0x64));
}
SHADOW_WRITER_T(SCROLL, int, listbox_calculate_dimensions, (struct game_listbox * l), (l), writes_lb_layout(&w_, l), "%d", P(l))

// The scratch list boxes: the vbtable pointer, the Win at 0x48 (with a real Buffer) and the Dialog at 0xa60, a
// ring of items with texts, and a parent with a Buffer.
enum { LXT = 56 };
static struct {
    uint8_t obj[LBSIZE];
    int32_t vbt[4];
    struct game_list_item item[NN];
    struct game_dlg_node node[NN];
    char text[NN][LXT];
    struct {
        struct game_win win;
        struct game_buffer buf;
    } parent;
    uint8_t pix[72 * 48 + 16];
} LX;
// The item drawing makes the item's rectangle the clip, which makes a clip region (a handle, at 0x70 of the
// Buffer, that a check's two runs can't be given alike), so the region isn't compared; the sweep drops it
// (and the device context's) after each check.
static void writes_lb_buffer(struct hook_writes *w, struct game_buffer *b) {
    hooks_writes_add(w, b, 0x70);
    hooks_writes_add(w, (uint8_t *)b + 0x74, sizeof *b - 0x74);
    if (!b->bits || b->dd) return;
    int64_t rows = -(int64_t)b->neg_height, pitch = b->pitch;
    if (rows <= 0 || pitch == 0) return;
    uint8_t *a = b->bits, *c = b->bits + pitch * (rows - 1);
    hooks_writes_add(w, a < c ? a : c, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}
static void lb_drop_region(struct game_listbox *l) {
    struct game_buffer *buf = lb_buf(l);
    int32_t *region = (int32_t *)((uint8_t *)buf + 0x70), dc = *(int32_t *)((uint8_t *)buf + 0x64);
    if (!*region) return;
    if (dc) game_select_clip_rgn((void *)(intptr_t)dc, NULL);
    game_delete_object((void *)(intptr_t)*region);
    *region = 0;
}
static void writes_lb_draw(struct hook_writes *w, struct game_listbox *l) {
    struct game_buffer *buf = lb_buf(l);
    writes_lb_buffer(w, buf);
    writes_text_state(w, buf);
    writes_vlog(w);
    hooks_writes_add(w, game_win_cur, 4);
    hooks_writes_add(w, game_listbox_row, 4);
    hooks_writes_add(w, game_listbox_cur, 4);
    hooks_writes_add(w, l, 0x48);
    hooks_writes_add(w, (uint8_t *)lb_dlg(l) + 0x34, 0xd4 - 0x34);
    hooks_writes_add(w, LX.text, sizeof LX.text);
    hooks_writes_add(w, LX.item, sizeof LX.item);
}
SWEEP_ONLY(void, listbox_on_redraw, (void *w), (w), (writes_lb_draw(&w_, (struct game_listbox *)((uint8_t *)w - 0x48))), "%d", P(w))
SWEEP_ONLY(void, listbox_draw_item, (struct game_listbox * l, int pos), (l, pos), (writes_lb_draw(&w_, l)), "%d, %d", P(l), pos)

enum { DWID = 60, DHGT = 40, PWID = 90, PHGT = 60 };
static struct {
    struct game_scroll bar;
    struct {
        struct game_win win;
        struct game_buffer buf;
    } parent;
    struct game_sprite spr[3];
    uint8_t sdata[3][1024];
    uint8_t pix[2][PWID * PHGT + 16];
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

static void draw_sweep(void) {
    struct palette *pal = *game_palette_copy;
    uint32_t hwnd = *game_win_hwnd;
    const uint8_t *tint = *game_win_tint;
    *game_win_hwnd = 1;
    *game_palette_copy = NULL;
    for (int round = 0; round < 40; round++) {
        memset(&D, 0, sizeof D);
        for (int i = 0; i < 3; i++) make_sprite(&D.spr[i], D.sdata[i]);
        for (int i = 0; i < 256; i++) D.tint[i] = (uint8_t)rnd();
        *game_win_tint = rnd_to(2) ? D.tint : NULL;
        hand_buffer(&D.parent.buf, D.pix[0], PWID, PHGT);
        D.parent.win.vtable = stub_vt;
        D.parent.win.flags = (uint32_t)rnd_to(2);
        struct game_scroll *s = &D.bar;
        s->win.vtable = stub_vt, s->win.parent = rnd_to(8) ? &D.parent.win : NULL;
        s->win.flags = rnd_to(2) ? 0x80000 : 0;
        for (int i = 0; i < 4; i++) s->win.outer[i] = rnd_to(20) - 5, s->win.inner[i] = rnd_to(20) - 5;
        *(void ***)s->btn_a = stub_vt, *(void ***)s->btn_b = stub_vt;
        for (int k = 0; k < 6; k++) {
            int w = rnd_to(2) ? DWID : 12 + rnd_to(10), h = w == DWID ? 12 + rnd_to(10) : DHGT;
            hand_buffer(&s->buf, D.pix[1], w, h);
            s->flags = (uint32_t)rnd_to(4), s->border = rnd_to(3) ? rnd_to(256) : -1;
            s->lo = rnd_to(10) - 3, s->hi = s->lo + rnd_to(30), s->pos = s->lo + rnd_to(s->hi - s->lo + 1);
            s->drag = rnd_to(2) ? -1 : rnd_to(50), s->thickness = 8 + rnd_to(8);
            s->bar[0] = rnd_to(3), s->bar[1] = rnd_to(3), s->bar[2] = s->bar[0] + s->thickness, s->bar[3] = s->bar[1] + s->thickness;
            s->back = rnd_to(256), s->bevel = rnd_to(4), s->bevel_upper = rnd_to(256), s->bevel_lower = rnd_to(256);
            s->grip = rnd_to(3) ? &D.spr[rnd_to(3)] : NULL, s->tile = rnd_to(3) ? &D.spr[rnd_to(3)] : NULL;
            check_scroll_on_redraw(s);
            s->win.flags ^= 0x80000;
            for (int i = 0; i < 4; i++) s->win.outer[i] = rnd_to(20) - 5;
        }
        // The thumb's rectangle by itself, with and without a hold.
        for (int k = 0; k < 6; k++) {
            int32_t out[4];
            s->drag = rnd_to(2) ? -1 : rnd_to(70) - 5, s->pos = s->lo + rnd_to(s->hi - s->lo + 1);
            check_scroll_compute_thumb_rect(s, out);
            s->flags ^= 2;
        }
    }
    *game_win_hwnd = hwnd;
    *game_win_tint = tint;
    *game_palette_copy = pal;
}

// The list boxes: a Buffer with a device context and a Font, items of the texts below, and every number the
// layout reads varied; the layout is checked and then the redraw on what it made, with the first item shown,
// the frames, the flags of the window and the function a screen may set changed between redraws.
static const char *const WORDS[] = {"Ok", "Alpha", "An item with enough words in it to be wide", "", "x", "WWWW MMMM",
                                    "Wide wide wide wide wide wide wide"};
// The function a screen may set for a list box: told the Win, and (from the item drawing) an item's id, its
// place and a rectangle, whose contents are logged and not its address.
static int cdecl_lb_back(void *w, int a, int b, const int32_t *r) {
    return vrec(0x401, w, a, b, r ? r[0] * 31 + r[1] * 131 + r[2] * 1031 + r[3] * 10007 : 0), vlog.n & 1;
}
static int cdecl_lb_item(void *w, int a, int b, int c, int d, int e) {
    return vrec(0x402, w, a, b, c * 31 + d * 131 + e * 1031), vlog.n & 1;
}
static int cdecl_lb_text(void *w, int a, int b, const int32_t *r, const char *text) {
    return vrec(0x403, w, a * 3 + b, r[0] * 31 + r[1] * 131 + r[2] * 1031 + r[3] * 10007, text ? text[0] : -1), vlog.n & 1;
}
static void lx_build(struct game_buffer *big, struct font *font) {
    memset(LX.obj, 0, sizeof LX.obj);
    int32_t *vb = LX.vbt;
    vb[1] = 0x48, vb[2] = 0xa60;
    *(int32_t **)LX.obj = vb;
    struct game_listbox *l = (struct game_listbox *)LX.obj;
    struct game_win *win = lb_win(l);
    win->vtable = stub_vt;
    win->parent = rnd_to(10) ? &LX.parent.win : NULL;
    win->flags = (rnd_to(2) ? 0x80000 : 0) | (rnd_to(3) == 0 ? 0x40 : 0);
    memcpy(lb_buf(l), big, sizeof *big);
    struct game_dlg *d = lb_dlg(l);
    int n = rnd_to(NN + 1);
    for (int i = 0; i < n; i++) {
        snprintf(LX.text[i], LXT, "%s", WORDS[rnd_to(7)]);
        LX.item[i].text = LX.text[i], LX.item[i].lines = 1;
        LX.node[i].pad8 = (int32_t)(intptr_t)&LX.item[i], LX.node[i].key = 10 + i;
        LX.node[i].next = &LX.node[(i + 1) % n], LX.node[i].prev = &LX.node[(i + n - 1) % n];
    }
    d->head = n ? LX.node : NULL, d->cur = n ? &LX.node[rnd_to(n)] : NULL, d->count = n, d->idx = n ? rnd_to(n) : 0;
    l->top = n ? rnd_to(n + 2) - 1 : 0, l->f20 = rnd_to(NN + 3), l->f2c = rnd_to(5);
    l->f38 = rnd_to(2) ? (void *)cdecl_lb_back : NULL;
    l->f4 = rnd_to(2), l->f10 = rnd_to(3) ? rnd_to(256) : -1, l->f14 = rnd_to(3) ? rnd_to(256) : -1;
    l->f24 = rnd_to(256), l->f28 = rnd_to(256);
    l->f30 = rnd_to(2) ? (int32_t)(intptr_t)cdecl_lb_item : 0, l->f34 = rnd_to(2) ? (int32_t)(intptr_t)cdecl_lb_text : 0;
    for (int i = 0; i < n; i++) LX.item[i].rows = 1 + rnd_to(3), LX.item[i].lines = rnd_to(2);
    DLG_F(d, 0x20) = rnd_to(4), DLG_F(d, 0x24) = rnd_to(10), DLG_F(d, 0x28) = rnd_to(10);
    DLG_F(d, 0x2c) = rnd_to(3) ? 40 + rnd_to(60) : 0, DLG_F(d, 0x30) = rnd_to(3) ? 20 + rnd_to(40) : 0;
    DLG_F(d, 0x34) = rnd_to(8), DLG_F(d, 0x38) = rnd_to(4), DLG_F(d, 0x3c) = rnd_to(4), DLG_F(d, 0x40) = rnd_to(4);
    DLG_F(d, 0x44) = rnd_to(4), DLG_F(d, 0x48) = rnd_to(2) ? -1 : 8 + rnd_to(10), DLG_F(d, 0x4c) = 1 + rnd_to(3);
    DLG_F(d, 0x50) = 1 + rnd_to(5), DLG_F(d, 0x5c) = 30 + rnd_to(60);
    DLG_F(d, 0x70) = DLG_F(d, 0x74) = DLG_F(d, 0x78) = (int32_t)(intptr_t)font;
    for (int o = 0x7c; o <= 0xa8; o += 4) DLG_F(d, o) = rnd_to(256);
    DLG_F(d, 0x6c) = rnd_to(256), DLG_F(d, 0xac) = rnd_to(256), DLG_F(d, 0xb0) = rnd_to(256), DLG_F(d, 0xb4) = rnd_to(4);
    DLG_F(d, 0xec) = rnd_to(NN + 2) - 1, DLG_F(d, 0xf0) = rnd_to(NN + 2) - 1;
}

static void lx_sweep(struct game_buffer *big, struct font *font) {
    int32_t row = *game_listbox_row, cur = *game_listbox_cur, bar = *game_listbox_bar;
    memset(&LX, 0, sizeof LX);
    hand_buffer(&LX.parent.buf, LX.pix, 72, 48);
    LX.parent.win.vtable = stub_vt;
    for (int round = 0; round < 30; round++) {
        for (int i = 0; i < 72 * 48; i++) LX.pix[i] = (uint8_t)rnd();
        *game_listbox_bar = rnd_to(12);
        lx_build(big, font);
        struct game_listbox *l = (struct game_listbox *)LX.obj;
        struct game_dlg *d = lb_dlg(l);
        check_listbox_calculate_dimensions(l);
        if (DLG_F(d, 0x58) <= 0) DLG_F(d, 0x58) = 1;  // (the original divides by the rows)
        if (DLG_F(d, 0x64) < 12) DLG_F(d, 0x64) = 12 + rnd_to(30);  // (the cells the drawing sees have some size)
        if (DLG_F(d, 0x68) < 4) DLG_F(d, 0x68) = 4 + rnd_to(10);
        for (int j = 0; j < 4; j++) {
            *game_win_cur = NULL;
            *game_listbox_row = rnd_to(3);
            check_listbox_on_redraw((uint8_t *)lb_win(l));
            lb_drop_region(l);
            for (int pos = -1; pos < d->count + 2; pos++) check_listbox_draw_item(l, pos), lb_drop_region(l);
            l->top = d->count ? rnd_to(d->count + 2) - 1 : 0;
            l->f38 = rnd_to(2) ? (void *)cdecl_lb_back : NULL;
            lb_win(l)->flags ^= 0x80000;
            DLG_F(d, 0xb4) = rnd_to(4), DLG_F(d, 0x6c) = rnd_to(256), DLG_F(d, 0xac) = rnd_to(256), DLG_F(d, 0xb0) = rnd_to(256);
            DLG_F(d, 0x24) = rnd_to(10), DLG_F(d, 0x28) = rnd_to(10);
            DLG_F(d, 0x54) = 1 + rnd_to(3), DLG_F(d, 0x58) = 1 + rnd_to(4), DLG_F(d, 0x4c) = 1 + rnd_to(3);
            DLG_F(d, 0x64) = 12 + rnd_to(30), DLG_F(d, 0x68) = 4 + rnd_to(10);
            DLG_F(d, 0xec) = rnd_to(NN + 2) - 1, DLG_F(d, 0xf0) = rnd_to(NN + 2) - 1, l->f4 = rnd_to(2);
            for (int i = 0; i < d->count; i++) LX.item[i].rows = 1 + rnd_to(3), LX.item[i].lines = rnd_to(2);
        }
    }
    *game_listbox_row = row, *game_listbox_cur = cur, *game_listbox_bar = bar;
}

static void life_sweep_scroll(void);
static void life_sweep_listbox(void);
static void scroll_sweep(void) {
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x2b4f0a31u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    fill_stub_vt();
    // What the sweep sets and puts back by hand.
    struct game_win *capture = *game_win_capture, *hover = *game_win_hover, *cur = *game_win_cur;
    int32_t by_key = *game_scroll_by_key, right = *game_listbox_right;
    for (int round = 0; round < 30; round++) {
        build();
        for (int i = 0; i < NS; i++) {
            struct game_scroll *s = &S.scroll[i];
            check_scroll_set_range(s, rnd_to(40) - 10, rnd_to(40) - 10);
            check_scroll_set_button_color(s, (int)rnd());
            check_scroll_set_bevel_thickness(s, rnd_to(5));
            check_scroll_set_bevel_upper(s, (int)rnd());
            check_scroll_set_bevel_lower(s, (int)rnd());
            check_scroll_set_border_color(s, rnd_to(2) ? -1 : rnd_to(256));
            check_scroll_set_bar_thickness(s, rnd_to(30));
            check_scroll_set_thumb_rect(s);
            check_scroll_set_sprite_left(s, (int)rnd(), (int)rnd(), (int)rnd());
            check_scroll_set_sprite_right(s, (int)rnd(), (int)rnd(), (int)rnd());
            check_scroll_set_sprite_up(s, (int)rnd(), (int)rnd(), (int)rnd());
            check_scroll_set_sprite_down(s, (int)rnd(), (int)rnd(), (int)rnd());
            for (int k = 0; k < 3; k++) {
                s->lo = rnd_to(20) - 5, s->hi = s->lo + rnd_to(40) - 2, s->inverted = rnd_to(2);
                check_scroll_set_pos(s, rnd_to(70) - 20);
                check_scroll_set_pos(s, s->lo - rnd_to(2)), check_scroll_set_pos(s, s->hi + rnd_to(2));
                int32_t out[4] = {0};
                s->drag = rnd_to(2) ? -1 : rnd_to(70) - 5, s->pos = s->lo + rnd_to(s->hi - s->lo + 1 > 0 ? s->hi - s->lo + 1 : 1);
                check_scroll_compute_thumb_rect(s, out);
                s->flags ^= 2, s->drag = -1;
                check_scroll_compute_thumb_rect(s, out);
                *game_win_capture = rnd_to(2) ? &s->win : NULL, *game_win_hover = rnd_to(2) ? &s->win : NULL;
                check_scroll_on_left_down(s, rnd_to(120) - 10, rnd_to(120) - 10);
                *game_win_capture = rnd_to(2) ? &s->win : NULL, *game_win_hover = rnd_to(2) ? &s->win : NULL;
                check_scroll_on_left_up(s, 3, 4);
                s->drag = rnd_to(2) ? -1 : rnd_to(60);
                check_scroll_on_mousewheel_down(s, rnd_to(2));
                s->drag = rnd_to(2) ? -1 : rnd_to(60);
                check_scroll_on_mousewheel_up(s, rnd_to(2));
                static const int keys[] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x41};
                s->drag = rnd_to(2) ? -1 : rnd_to(60), s->press_time = rnd_to(2) ? rnd() : 0;
                check_scroll_on_key_click(s, 0, keys[rnd_to(6)]);
                s->drag = rnd_to(2) ? -1 : rnd_to(60);
                check_scroll_on_button_clicked(s, rnd_to(4));
                check_scroll_on_resize(s, rnd_to(60) - 5, rnd_to(60) - 5);
            }
        }
        for (int k = 0; k < NL; k++) {
            struct game_listbox *l = (struct game_listbox *)S.lbox[k];
            uint8_t *win = lb_win_at(k);
            struct game_dlg *d = lb_dlg(l);
            for (int j = 0; j < 6; j++) {
                *game_win_cur = NULL;
                check_listbox_set_selected_pos(l, rnd_to(NN + 6) - 3, rnd_to(2));
                check_listbox_set_selected_pos(l, d->sel, rnd_to(2));
                check_listbox_set_selected_id(l, rnd_to(8) - 2);
                static const char *const words[] = {"abc", "ABC", "xyz", "xY", "", "nope"};
                check_listbox_find(l, rnd_to(8) ? words[rnd_to(6)] : NULL);
                d->idx = rnd_to(2 * NN + 6) - NN - 3;  // (the search puts the index back by the same walk)
                if (d->idx < 0 && -d->idx > d->count) d->idx = 0;
                check_listbox_find(l, words[rnd_to(6)]);
                check_listbox_on_scrolled(win, rnd_to(5), rnd_to(20));
                *game_listbox_right = rnd_to(2);
                check_listbox_on_right_down(win, 3, 4);
                check_listbox_on_right_double_click(win, 3, 4);
                *game_win_cur = NULL;
                check_listbox_on_left_double_click(win, 3, 4);
                check_listbox_on_mousewheel(win, rnd_to(7) - 3);
                check_listbox_on_mouse_leave(win, 1, 2);
                d->hover = rnd_to(NN + 2) - 1;
            }
        }
        for (int i = 0; i < NSB; i++) {
            check_stringbox_on_scrolled(&S.sbox[i], rnd_to(5), rnd_to(60));
            check_stringbox_on_link_clicked(&S.sbox[i], rnd_to(9), rnd_to(9));
        }
    }
    draw_sweep();
    struct font *lx_font;
    struct game_buffer *lx_buf = button_sweep_buffer(&lx_font);
    if (lx_buf) lx_sweep(lx_buf, lx_font);
    *game_win_capture = capture, *game_win_hover = hover, *game_win_cur = cur;
    *game_scroll_by_key = by_key, *game_listbox_right = right;
    life_sweep_scroll();
    life_sweep_listbox();
    hooks_sweeping = sweeping;
}

// ---- Life cycle -----------------------------------------------------------------------------------

// A Scroll made and unmade: a GraphicWin with its own fields (the game's defaults for colours, delays and
// pictures) and two FlatButtons (0xaac and 0x15f8) for the arrows. The constructor and close start the
// same fields; close also asks each arrow to close itself through its vtable (slot 0x168), and the
// destructor closes and unmakes the arrows as FlatButtons (it sets their vtables first, since the Scroll's
// own destructor runs as the arrows' class has been taken apart). These allocate or free (the arrows'
// names, the Buffers), so in shadow mode the game's calls run the original and the sweep checks Chiron's
// code on twin scroll bars (life.c). The exception frames are not reproduced. Differences from
// OpenSMACX: it has none of these.

#define SC_VTABLE 0x669d58
#define SC_BUFFER_VTABLE 0x669d50
enum { SC_BTN_A = 0xaac, SC_BTN_B = 0x15f8, SC_VT_CLOSE = 0x168 };

static const struct life_set scroll_start[] = {
    LG(0xa14, 0x9b8de0), LG(0xa1c, 0x69702c), LZ(0xa28), LG(0xa20, 0x9b8de4), LG(0xa24, 0x697030), LG(0xa2c, 0x9b8de4),
    LG(0xa30, 0x697028), LC(0xa3c, 0xffffffff), LZ(0xa38), LG(0xa34, 0x697024), LG(0xa40, 0x697020), LZ(0xa44),
    LZ(0xa4c), LZ(0xa50), LZ(0xa54), LZ(0xa58), LG(0xa48, 0x697034), LG(0xa5c, 0x697038), LG(0xa64, 0x69703c),
    LG(0xa68, 0x697040), LG(0xa6c, 0x697044), LG(0xa70, 0x697048), LG(0xa74, 0x9b8e1c), LG(0xa78, 0x9b8e20),
    LZ(0x2144), LZ(0x2148),
};

// The pictures for the four arrows: three numbers each, from the game's table (0x9b8de8 on; the second to
// fourth rows start one word on, as the original's loop reads them).
static void scroll_start_fields(struct game_scroll *s) {
    life_apply(s, scroll_start, (int)(sizeof scroll_start / sizeof scroll_start[0]));
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t d = 4 * i, e = 4 * (i + 1);
        *(uint32_t *)((uint8_t *)s + 0xa7c + d) = *(uint32_t *)game_at(0x9b8de8 + d);
        *(uint32_t *)((uint8_t *)s + 0xa88 + d) = *(uint32_t *)game_at(0x9b8df0 + e);
        *(uint32_t *)((uint8_t *)s + 0xa94 + d) = *(uint32_t *)game_at(0x9b8dfc + e);
        *(uint32_t *)((uint8_t *)s + 0xaa0 + d) = *(uint32_t *)game_at(0x9b8e08 + e);
    }
}

void *SCROLL_THIS scroll_ctor(struct game_scroll *s) {
    graphic_win_ctor(&s->win);
    flat_button_ctor((struct game_flat_button *)((uint8_t *)s + SC_BTN_A));
    flat_button_ctor((struct game_flat_button *)((uint8_t *)s + SC_BTN_B));
    s->win.vtable = (void **)(uintptr_t)game_va(SC_VTABLE);
    s->buf.vtable = (void **)(uintptr_t)game_va(SC_BUFFER_VTABLE);
    scroll_start_fields(s);
    return s;
}

// Starts the fields again, has each arrow close itself, and closes the GraphicWin.
void SCROLL_THIS scroll_close(struct game_scroll *s) {
    scroll_start_fields(s);
    void *arrows[2] = {(uint8_t *)s + SC_BTN_A, (uint8_t *)s + SC_BTN_B};
    for (int i = 0; i < 2; i++) ((void(SCROLL_THIS *)(void *))(*(void ***)arrows[i])[SC_VT_CLOSE / 4])(arrows[i]);
    graphic_win_close(&s->win);
}

void SCROLL_THIS scroll_dtor(struct game_scroll *s) {
    s->win.vtable = (void **)(uintptr_t)game_va(SC_VTABLE);
    s->buf.vtable = (void **)(uintptr_t)game_va(SC_BUFFER_VTABLE);
    scroll_close(s);
    // The second arrow, then the first, as the FlatButtons they are.
    for (int off = SC_BTN_B; off >= SC_BTN_A; off -= SC_BTN_B - SC_BTN_A) {
        struct game_flat_button *f = (struct game_flat_button *)((uint8_t *)s + off);
        f->base.win.vtable = (void **)(uintptr_t)game_va(0x669754);
        f->base.buf.vtable = (void **)(uintptr_t)game_va(0x66974c);
        flat_button_close(f);
        base_button_dtor(&f->base);
    }
    graphic_win_dtor(&s->win);
}

// ---- Life cycle: shadow mode and the sweep ----

SHADOW_PASS_T(SCROLL, void *, scroll_ctor, (struct game_scroll * s), (s))
SHADOW_PASS_T(SCROLL, void, scroll_close, (struct game_scroll * s), (s))
SHADOW_PASS_T(SCROLL, void, scroll_dtor, (struct game_scroll * s), (s))

LIFE_FN(scroll_ctor, orig_scroll_ctor(o), scroll_ctor(o))
LIFE_FN(scroll_close, orig_scroll_close(o), scroll_close(o))
LIFE_FN(scroll_dtor, orig_scroll_dtor(o), scroll_dtor(o))

static const struct life_slot scroll_slots[] = {{0xcc, -1}, {0xd0, -1}};
static const struct life_class scroll_class = {"Scroll", 0x2150, scroll_slots, 2, 0x444, 0x9cc, {0xa14, 0x20}};

static void life_sweep_scroll(void) {
    if (!game_mem_get || *game_ddraw || !hooks_dc_same) return;
    for (int round = 0; round < 4; round++) {
        uint8_t fill = (uint8_t)round * 37 + 5;
        life_ctor_check(&stats_scroll_ctor, &scroll_class, life_orig_scroll_ctor, life_ours_scroll_ctor, fill);
        life_close_check(&stats_scroll_close, &scroll_class, life_orig_scroll_ctor, life_orig_scroll_close, life_ours_scroll_close, fill,
                         4 + round, 3 + round, 0);
        life_close_check(&stats_scroll_dtor, &scroll_class, life_orig_scroll_ctor, life_orig_scroll_dtor, life_ours_scroll_dtor, fill,
                         4 + round, 3 + round, 0);
    }
}

// ---- Life cycle: ListBox ---------------------------------------------------------------------------

// A ListBox is an object with two virtual bases, a GraphicWin (0x48 into it) and a Dialog (0xa60), found
// through the table its first word points to (entries 1 and 2: where each lies from the start). Its
// constructor, given whether it is the whole object, makes the bases (the GraphicWin's and the Dialog's
// constructors) and sets every base's vtables for ListBox and the difference between each base's place and
// the place the table gives (a word before each base); then its own fields, from the game's defaults and
// the Dialog's field at 0xb4 set to 1. Close closes the GraphicWin and the Dialog and starts the fields
// again. The destructor given here is the part of the destruction a ListBox does for itself (the vtables,
// then close); the bases are unmade by the deleting destructor. These allocate or free on the game's heap,
// so in shadow mode the game's calls run the original and the sweep checks Chiron's code on twin boxes
// (life.c). The exception frames are not reproduced. Differences from OpenSMACX: it has none of these.

enum { LB_VBPTR = 0x670584, LB_WIN_VT = 0x67041c, LB_BUF_VT = 0x670414, LB_DLG_VT = 0x670408, LB_WIN = 0x48, LB_DLG = 0xa60 };

static void listbox_vtables(uint8_t *o) {
    int32_t win = life_vb(o, 1), dlg = life_vb(o, 2);
    *(uint32_t *)(o + win) = game_va(LB_WIN_VT);
    *(uint32_t *)(o + win + 0x444) = game_va(LB_BUF_VT);
    *(uint32_t *)(o + dlg) = game_va(LB_DLG_VT);
    *(int32_t *)(o + win - 4) = win - LB_WIN;
    *(int32_t *)(o + dlg - 4) = dlg - LB_DLG;
}

// The box's own fields (0x4 to 0x40) and the Dialog's field at 0xb4.
static void listbox_start_fields(uint8_t *o) {
    uint32_t *f = (uint32_t *)o;
    f[1] = 0, f[2] = 0;
    f[4] = *(uint32_t *)game_at(0x6970e8), f[5] = *(uint32_t *)game_at(0x6970ec);
    f[6] = 0, f[7] = 0, f[8] = 0;
    f[9] = *(uint32_t *)game_at(0x6970e0), f[10] = *(uint32_t *)game_at(0x9b8ee0), f[0xc] = 0, f[0xb] = *(uint32_t *)game_at(0x6970e4);
    f[0xd] = 0, f[0xe] = 0, f[0xf] = 0, f[0x10] = 0;
    *(uint32_t *)(o + life_vb(o, 2) + 0xb4) = 1;
    f[3] = 0;
}

void *SCROLL_THIS listbox_ctor(uint8_t *o, int most_derived) {
    if (most_derived) {
        *(uint32_t *)o = game_va(LB_VBPTR);
        graphic_win_ctor((struct game_win *)(o + LB_WIN));
        dialog_ctor((struct game_dialog *)(o + LB_DLG));
    }
    listbox_vtables(o);
    listbox_start_fields(o);
    return o;
}

void SCROLL_THIS listbox_close(uint8_t *o) {
    graphic_win_close((struct game_win *)(o + life_vb(o, 1)));
    dialog_close((struct game_dialog *)(o + life_vb(o, 2)));
    listbox_start_fields(o);
}

// Given the GraphicWin (the ListBox's second part).
void SCROLL_THIS listbox_dtor(uint8_t *win) {
    uint8_t *o = win - LB_WIN;
    listbox_vtables(o);
    listbox_close(o);
}

// ---- Life cycle: ListBox, shadow mode and the sweep ----

SHADOW_PASS_T(SCROLL, void *, listbox_ctor, (uint8_t * o, int most), (o, most))
SHADOW_PASS_T(SCROLL, void, listbox_close, (uint8_t * o), (o))
SHADOW_PASS_T(SCROLL, void, listbox_dtor, (uint8_t * win), (win))

LIFE_FN(listbox_ctor, orig_listbox_ctor(o, 1), listbox_ctor(o, 1))
LIFE_FN(listbox_close, orig_listbox_close(o), listbox_close(o))
LIFE_FN(listbox_dtor, orig_listbox_dtor((uint8_t *)o + LB_WIN), listbox_dtor((uint8_t *)o + LB_WIN))

// The Win's dialog list (0x48 + 0xcc, 0xd0), the Dialog's data and cursor (0xa60 + 8, 0xc) and list head and
// cursor (0xa60 + 0xc4, 0xc8).
static const struct life_slot listbox_slots[] = {{0x114, -1}, {0x118, -1}, {0xa68, -1}, {0xa6c, -1}, {0xb24, -1}, {0xb28, -1}};
static const struct life_class listbox_class = {"ListBox", 0xb54, listbox_slots, 6, LB_WIN + 0x444, LB_WIN + 0x9cc, {0x10}};

static void life_sweep_listbox(void) {
    uint32_t *head_global[1] = {(uint32_t *)game_at(0x9b3374)};
    if (!game_mem_get || *game_ddraw || !hooks_dc_same) return;
    for (int round = 0; round < 4; round++) {
        uint8_t fill = (uint8_t)(round * 43 + 13);
        life_fill(&listbox_class, life_a, life_b, fill);
        if (life_run(&stats_listbox_ctor, &listbox_class, life_orig_listbox_ctor, life_ours_listbox_ctor, NULL, head_global, 1))
            life_buffer_check_at(&stats_listbox_ctor, LB_WIN + 0x444);
        for (int which = 0; which < 2; which++) {
            life_build(&listbox_class, fill, life_orig_listbox_ctor, NULL);
            life_buffers(LB_WIN + 0x444, 4 + round, 3 + round);
            life_nodes(LB_DLG + 0xbc, round);
            if (round & 1) life_data(LB_DLG, 64);
            struct hook_stats *s = which ? &stats_listbox_dtor : &stats_listbox_close;
            if (life_run(s, &listbox_class, which ? life_orig_listbox_dtor : life_orig_listbox_close,
                         which ? life_ours_listbox_dtor : life_ours_listbox_close, NULL, head_global, 1))
                life_buffer_check_at(s, LB_WIN + 0x444);
            // (Close leaves the bases standing; unmake them as the deleting destructor would.)
            for (int i = 0; i < 2; i++) {
                uint8_t *o = i ? life_b : life_a;
                game_dialog_dtor(o + LB_DLG);
                game_graphic_win_dtor(o + LB_WIN);
            }
        }
    }
}

// ---- Hooks ----------------------------------------------------------------------------------------------------------------

static const struct hook scroll_hooks[] = {
    HOOK(0x609db0, listbox_ctor, 0x6a, 0xff, 0x68, 0x0a, 0x2f, 0x66, 0x00),  // push -1; push handler
    // mov eax, ecx; push esi; mov edx, [eax-0x48]; lea ecx, [eax-0x48]
    HOOK(0x609ec0, listbox_dtor, 0x8b, 0xc1, 0x56, 0x8b, 0x50, 0xb8, 0x8d, 0x48, 0xb8),
    HOOK(0x609f20, listbox_close, 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0x8b, 0x48, 0x04),  // push esi; mov esi, ecx; mov eax, [esi]; mov ecx, [eax+4]
    HOOK(0x6051d0, scroll_ctor, 0x6a, 0xff, 0x68, 0x46, 0x2e, 0x66, 0x00),  // push -1; push handler
    // mov eax, [0x9b8de0]; push esi; mov esi, ecx
    HOOK(0x605370, scroll_close, 0xa1, 0xe0, 0x8d, 0x9b, 0x00, 0x56, 0x8b, 0xf1),
    HOOK(0x406e60, scroll_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xe4, 0x0b, 0x65, 0x00),
    HOOK(0x6059b0, scroll_set_range, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x605a10, scroll_set_button_color, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x605a50, scroll_set_bevel_thickness, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x605a90, scroll_set_bevel_upper, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x605ad0, scroll_set_bevel_lower, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x605b10, scroll_set_border_color, 0x8b, 0x44, 0x24, 0x04, 0x33, 0xd2),
    HOOK(0x605b80, scroll_set_bar_thickness, 0x8b, 0x44, 0x24, 0x04, 0x33, 0xd2),
    HOOK(0x605be0, scroll_set_sprite_left, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x605c30, scroll_set_sprite_right, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x605c80, scroll_set_sprite_up, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x605cd0, scroll_set_sprite_down, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x605d20, scroll_set_pos, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x606c50, scroll_compute_thumb_rect, 0x83, 0xec, 0x08, 0x53, 0x55),
    HOOK(0x606ea0, scroll_set_thumb_rect, 0x8b, 0x81, 0x60, 0x0a, 0x00, 0x00),
    HOOK(0x6061e0, scroll_on_left_down, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    HOOK(0x6062c0, scroll_on_left_up, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x606320, scroll_on_mousewheel_down, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x606440, scroll_on_mousewheel_up, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x606560, scroll_on_key_click, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb),
    HOOK(0x606b20, scroll_on_button_clicked, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xc4, 0x00, 0x00, 0x00),
    HOOK(0x606860, scroll_on_resize, 0x53, 0x8b, 0x5c, 0x24, 0x0c),
    HOOK(0x6066f0, scroll_on_redraw, 0x83, 0xec, 0x10, 0x53, 0x56),
    HOOK(0x609fa0, listbox_set_selected_id, 0x8b, 0x01, 0x53, 0x55, 0x56),
    HOOK(0x60a020, listbox_set_selected_pos, 0x55, 0x56, 0x8b, 0xf1, 0x8b, 0x6c, 0x24, 0x0c),
    HOOK(0x60c7d0, listbox_find, 0x51, 0x56, 0x8b, 0xf1, 0x8b, 0x06),
    HOOK(0x60c6a0, listbox_on_scrolled, 0x8b, 0x41, 0xb8, 0x8b, 0x50, 0x04),
    HOOK(0x60aa20, listbox_on_right_down, 0xc7, 0x05, 0xec, 0x8e, 0x9b, 0x00, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x60c6d0, listbox_on_right_double_click, 0xc7, 0x05, 0xec, 0x8e, 0x9b, 0x00, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x60c710, listbox_on_left_double_click, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x46, 0xb8),
    HOOK(0x60cb70, listbox_on_mousewheel, 0x8b, 0x44, 0x24, 0x04, 0x56),
    HOOK(0x60ce10, listbox_on_mouse_leave, 0x8b, 0x41, 0xb8, 0x8b, 0x50, 0x08),
    HOOK(0x60a1e0, listbox_calculate_dimensions, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb, 0x8b, 0x0e),
    HOOK(0x60b530, listbox_draw_item, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x28, 0x53, 0x55, 0x56),
    HOOK(0x60c350, listbox_on_redraw, 0x83, 0xec, 0x14, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x629d20, stringbox_on_scrolled, 0x8b, 0x44, 0x24, 0x08, 0x8b, 0x11),
    HOOK(0x432720, stringbox_on_link_clicked, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),
};
struct hook_module scroll_module = {"scroll", scroll_hooks, HOOK_COUNT(scroll_hooks), scroll_sweep, NULL, -1};
