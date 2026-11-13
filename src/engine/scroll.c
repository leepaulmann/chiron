// The game's Scroll (scroll bar), ListBox and StringBox classes (OpenSMACX's and Thinker's names).
// A scroll bar is a Win with a Buffer behind it and two ImageButtons for its arrows; it keeps a
// position between `lo` and `hi` and tells its parent (through slots 0xc0 and 0xc4 of the parent's
// vtable) and a function a screen may have set. A ListBox is a Win and a Dialog list (both virtual
// bases of the box, found through its vbtable) with a scroll bar the Win owns. The layouts are in
// game.h.
//
// Here are the functions that change a bar's state, colours and pictures, its thumb, the mouse, the
// wheel, the keyboard and the arrows (as far as they change state), a resize, and its redraw; the
// ListBox's selection, double clicks, wheel and mouse leave, and its search by text; and two of
// StringBox's. Where the original calls through a vtable (a screen subclasses a bar's parent to hear
// of it; the redraw is slot 0xf8) or through a function pointer a screen set, so does this.
//
// Left to the original: the constructors, destructors, close and init functions (they build
// objects on the game's heap and keep an exception frame; the init_vert, init_horz and _nc ones
// only call init), on_mouse_move and on_button_pressed (the first reads timeGetTime and posts a
// message, the second also sleeps: timing no check can put back), on_left_click (which does
// nothing). ListBox: attach, item and calculate_dimensions (the heap, and text metrics through a
// device context), and everything that draws an item (draw_item, on_redraw, on_scrolling,
// on_mouse_move, on_left_down, on_key_down, UNK1) or reads the keyboard through Windows. StringBox:
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
#include "buffer.h"
#include "game.h"
#include "hooks.h"
#include "scroll.h"
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
static void *stub_vt[0x180 / 4];
static void fill_stub_vt(void) {
    stub_vt[0xc / 4] = (void *)stub_0xc, stub_vt[0x10 / 4] = (void *)stub_0x10, stub_vt[0x1c / 4] = (void *)stub_0x1c;
    stub_vt[0x44 / 4] = (void *)stub_0x44, stub_vt[0x50 / 4] = (void *)stub_0x50, stub_vt[0x5c / 4] = (void *)stub_0x5c;
    stub_vt[0x70 / 4] = (void *)stub_0x70, stub_vt[0xc0 / 4] = (void *)stub_0xc0, stub_vt[0xc4 / 4] = (void *)stub_0xc4;
    stub_vt[0xd4 / 4] = (void *)stub_0xd4, stub_vt[0xd8 / 4] = (void *)stub_0xd8, stub_vt[0xdc / 4] = (void *)stub_0xdc;
    stub_vt[0xe0 / 4] = (void *)stub_0xe0, stub_vt[0xf4 / 4] = (void *)stub_0xf4, stub_vt[0xf8 / 4] = (void *)stub_0xf8;
    stub_vt[0x160 / 4] = (void *)stub_0x160;
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
    *game_win_capture = capture, *game_win_hover = hover, *game_win_cur = cur;
    *game_scroll_by_key = by_key, *game_listbox_right = right;
    hooks_sweeping = sweeping;
}

// ---- Hooks ----------------------------------------------------------------------------------------------------------------

static const struct hook scroll_hooks[] = {
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
    HOOK(0x629d20, stringbox_on_scrolled, 0x8b, 0x44, 0x24, 0x08, 0x8b, 0x11),
    HOOK(0x432720, stringbox_on_link_clicked, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1),
};
struct hook_module scroll_module = {"scroll", scroll_hooks, HOOK_COUNT(scroll_hooks), scroll_sweep, NULL, -1};
