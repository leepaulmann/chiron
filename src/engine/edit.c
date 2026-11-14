// The game's EditBox class (OpenSMACX's and Thinker's names; a line of text with a caret, in a Win with
// a Buffer behind it) and the EditGroup class (up to ten EditBoxes with labels in one dialog; a Win and a
// Dialog as virtual bases, so its functions find them through the object's vbtable). The layouts are in
// game.h.
//
// Here are all of an EditBox's text editing: setting the text, the caret and selection (`caret` and
// `anchor` are positions in the text, the text between them is selected; Shift keeps the anchor), the
// keys (typing, Backspace, Delete, Home, End, the arrows and the number pad's 4 and 6, with Ctrl for a
// word at a time), the mouse's setting of the caret, the blink, the focus, and the two questions the
// text's width answers (does the text up to the caret fit, where does an x lie in the text). The
// EditGroup's text access (set_text, get_text, append_text, set_text_limits), its focus and its
// calculate_dimensions are here too.
//
// Left to the original: the constructors, destructors, close, init and UNK1 of both classes (they
// build objects, a Time object and windows), EditGroup's attach, edit and set_default (they allocate
// text on the game's heap), and both classes' on_redraw (they draw text and pictures through a device
// context).
//
// Differences from OpenSMACX and Thinker: neither lays the EditBox out; the Time object at 0xb4c is the
// caret's blink (a timer of 500 ms, started when the box has the focus). A key press is taken as
// `key & 0xffff` with Shift in bit 16 and Ctrl in bit 17 (the win32 shim's OnKey puts them there), and
// `mods` keeps them (1 and 2). The original's quirks are kept: Right at the end of the text appends a
// space (and beeps if the text is full, but with the caret already moved), a masked text (flag 2, the
// asterisks) is measured whole whatever the first character shown, and text_position compares a
// position in the shown text with the caret in the whole one. Typing replaces a selection without
// scrolling the box. set_text cuts a text that is too long off its own end, in the caller's string.
// A caret that blinks is `blink`, set by the timer's callback (caret_callback).
//
// Checks. A function that touches only the box, a few globals and the redraw slot is checked as it is
// called, on the memory it writes and the Windows calls it makes (the beep and the timer calls go
// through the journal). Those that call another window's handler (the focus handlers) have effects no
// check can put back, so the game's calls of them run the original in shadow mode and the sweep checks
// them on scratch boxes, groups and parents in static memory whose vtables log the calls. The text
// measuring needs a Font: the sweep makes one of its own as text.c's does, and puts it in the
// scratch boxes' Buffers.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufobj.h"
#include "buffer.h"
#include "button.h"
#include "drawcheck.h"
#include "edit.h"
#include "game.h"
#include "hooks.h"
#include "life.h"
#include "menu.h"
#include "shadow.h"
#include "strlist.h"
#include "text.h"
#include "win.h"

#define EDIT (&edit_module)
extern struct hook_module edit_module;

#define P(p) ((int)(uintptr_t)(p))

// ---- Calls through a vtable ------------------------------------------------------------------------

enum {
    VT_CAPTURE_LOST = 0x10,
    VT_REDRAW = 0xf8,
    VT_FOCUS = 0xcc,  // a window gains (1) or loses (0) the dialog focus
};

static void *vslot(void *o, int slot) { return *(void **)((uint8_t *)*(void ***)o + slot); }
static int vcall0(void *o, int slot) { return ((int(THISCALL *)(void *))vslot(o, slot))(o); }
static int vcall1(void *o, int slot, int a) { return ((int(THISCALL *)(void *, int))vslot(o, slot))(o, a); }
static void redraw(struct game_editbox *e) { vcall0(e, VT_REDRAW); }

// ---- EditBox: the text ---------------------------------------------------------------------------------------

// The text as asterisks, for a box that hides it.
static void mask(const struct game_editbox *e, char *out) {
    out[0] = 0;
    strcpy(out, e->text);
    for (char *p = out; *p; p++) *p = '*';
}

// Take a new text, cutting the caller's string from its end to the box's maximum length first (as the original
// does, in the caller's own string). The caret goes to the end and nothing is selected.
void EDIT_THIS edit_set_text(struct game_editbox *e, char *text) {
    e->first = 0;
    e->text[0] = 0;
    if (text) {
        size_t len;
        while ((len = strlen(text)) != 0 && len > e->max_len) text[len - 1] = 0;
        strcpy(e->text, text);
    }
    e->anchor = 0;
    e->caret = (int)strlen(e->text);
    redraw(e);
}

// The timer's callback: the caret is shown, or hidden.
int edit_caret_callback(struct game_editbox *e) {
    e->blink = e->blink == 0;
    return vcall0(e, VT_REDRAW);
}

// Whether the text from the first character shown to the caret is narrower than the box less its padding.
int EDIT_THIS edit_text_fits(struct game_editbox *e) {
    char tmp[0x100];
    int avail = e->buf.width - 2 * e->pad;
    int w;
    if (e->flags & 2) {
        mask(e, tmp);
        w = buffer_text_width_2(&e->buf, tmp, e->caret - e->first);
    } else {
        w = buffer_text_width_2(&e->buf, e->text + e->first, e->caret - e->first);
    }
    return w < avail;
}

// The position in the text for an x in the box: the first character shown if it is left of the padding;
// otherwise the number of characters whose width stays within x, from the first one shown (one less than
// that if it is shorter than the caret's position: the original compares it with the caret in the whole text);
// 0 for an empty text.
int EDIT_THIS edit_text_position(struct game_editbox *e, int x) {
    char tmp[0x100];
    if (x < e->pad) return e->first;
    x -= e->pad;
    const char *s;
    if (e->flags & 2) {
        mask(e, tmp);
        s = tmp;
    } else {
        s = e->text + e->first;
    }
    if (*s == 0) return 0;
    int n = (int)strlen(s), i = 1;
    if (n > 1) {
        do {
            if (buffer_text_width_2(&e->buf, s, i) > x) break;
            i++;
        } while (i < n);
    }
    return i >= e->caret ? e->first + i : e->first + i - 1;
}

// ---- EditBox: the keys ---------------------------------------------------------------------------------------

// Typing a character (below a space, nothing happens): a selection is replaced by it, with the caret after it;
// otherwise it goes in at the caret and the box scrolls until the caret shows. A box with its maximum length
// (or with 255 characters, the size of its text) and no selection beeps. (The original never reads its second argument.)
int EDIT_THIS edit_on_key_click(struct game_editbox *e, int key, int a) {
    (void)a;
    if (key >= 0 && key < 0x20) return 0;
    if (strlen(e->text) == e->max_len && e->anchor == e->caret) {
        game_message_beep(0);
        return 1;
    }
    if (e->anchor != e->caret) {
        if (e->anchor < e->caret) {
            e->text[e->anchor] = (char)key;
            if (e->caret - e->anchor > 1) memmove(e->text + e->anchor + 1, e->text + e->caret, strlen(e->text + e->caret) + 1);
            e->caret = e->anchor + 1;
            e->anchor = e->caret;
        } else {
            e->text[e->caret] = (char)key;
            if (e->anchor - e->caret > 1) memmove(e->text + e->caret + 1, e->text + e->anchor, strlen(e->text + e->anchor) + 1);
            e->caret = e->caret + 1;
            e->anchor = e->caret;
        }
        redraw(e);
        return 1;
    }
    if (strlen(e->text) == 0xff) {
        game_message_beep(0);
        return 1;
    }
    char *p = e->text + e->caret;
    if (*p) memmove(p + 1, p, strlen(p) + 1);
    else p[1] = 0;
    e->text[e->caret] = (char)key;
    e->caret++, e->anchor++;
    while (!edit_text_fits(e)) e->first++;
    redraw(e);
    return 1;
}

// The caret keys and editing keys. The key is the virtual key code with Shift in bit 16 and Ctrl in
// bit 17; Tab and anything else not listed here is left to the caller (0). A caret move with Shift keeps the
// anchor (so selects), and one with Ctrl goes a word. Backspace and Delete with Shift or Ctrl beep.
int EDIT_THIS edit_on_key_down(struct game_editbox *e, int key) {
    if (key == 9) return 0;
    e->mods = 0;
    if (key & 0x10000) e->mods = 1;
    if (key & 0x20000) e->mods |= 2;
    switch (key & 0xffff) {
    case 0x27:  // Right
    case 0x66: {  // (number pad 6)
        int step;
        if (!(e->mods & 2)) {
            step = 1;
        } else {
            char *p = e->text + e->caret;
            if (*p == ' ') {
                step = 1;
            } else {
                char *q = strchr(p, ' ');
                step = q ? (int)(q - e->text) - e->caret + 1 : (int)strlen(p);
            }
        }
        e->caret += step;
        if (e->text[e->caret - 1] == 0) {
            if (strlen(e->text) == e->max_len) {
                game_message_beep(0);
                return 1;
            }
            e->text[e->caret - 1] = ' ';
            e->text[e->caret] = 0;
        }
        while (!edit_text_fits(e)) e->first++;
        if (!(e->mods & 1)) e->anchor = e->caret;
        redraw(e);
        return 1;
    }
    case 0x25:  // Left
    case 0x64: {  // (number pad 4)
        int c = e->caret;
        if (c == 0) {
            if (e->mods & 1) return 1;
            e->anchor = 0;
            redraw(e);
            return 1;
        }
        int step;
        if (!(e->mods & 2)) {
            step = 1;
        } else {
            char *p = e->text + c - 1;
            if (*p == ' ') {
                step = 1;
            } else {
                while (p > e->text && *p != ' ') p--;
                step = c - (int)(p - e->text);
            }
        }
        e->caret = c - step;
        if (e->caret < e->first) e->first = e->caret;
        if (!(e->mods & 1)) e->anchor = e->caret;
        redraw(e);
        return 1;
    }
    case 0x08: {  // Backspace: the selection, or the character before the caret
        if (e->mods != 0) {
            game_message_beep(0);
            return 1;
        }
        int a = e->caret, b = e->anchor;
        if (b != a) {
            if (b < a) {
                memmove(e->text + b, e->text + a, strlen(e->text + a) + 1);
                e->caret = b + 1;
            } else {
                memmove(e->text + a, e->text + b, strlen(e->text + b) + 1);
                e->caret = a + 1;
            }
        } else {
            if (a == 0) {
                game_message_beep(0);
                return 1;
            }
            if (e->text[a]) memmove(e->text + a - 1, e->text + a, strlen(e->text + a) + 1);
            else e->text[a - 1] = 0;
        }
        e->caret--;
        e->anchor = e->caret;
        if (e->caret < e->first) e->first = e->caret;
        redraw(e);
        return 1;
    }
    case 0x2e: {  // Delete: the selection, or the character at the caret
        if (e->mods != 0) {
            game_message_beep(0);
            return 1;
        }
        int a = e->caret, b = e->anchor;
        if (b != a) {
            if (b < a) {
                memmove(e->text + b, e->text + a, strlen(e->text + a) + 1);
                e->caret = b;
            } else {
                memmove(e->text + a, e->text + b, strlen(e->text + b) + 1);
            }
        } else {
            if (e->text[a] == 0) {
                game_message_beep(0);
                return 1;
            }
            memmove(e->text + a, e->text + a + 1, strlen(e->text + a + 1) + 1);
        }
        e->anchor = e->caret;
        if (e->caret < e->first) e->first = e->caret;
        redraw(e);
        return 1;
    }
    case 0x24:  // Home
        e->caret = 0;
        if (!(e->mods & 1)) e->anchor = 0;
        e->first = 0;
        redraw(e);
        return 1;
    case 0x23: {  // End: the first character shown is the first of the end of the text that fills the box
        int len = (int)strlen(e->text);
        e->caret = len;
        if (!(e->mods & 1)) e->anchor = len;
        char *p = e->text + len;
        if (p > e->text) {
            do {
                int avail = e->buf.width - 2 * e->pad;
                if (!(buffer_text_width(&e->buf, p) < avail)) break;
                p--;
            } while (p > e->text);
        }
        if (p != e->text) p++;
        e->first = (int)(p - e->text);
        redraw(e);
        return 1;
    }
    }
    return 0;
}

// ---- EditBox: the mouse, the focus --------------------------------------------------------------------------------

// A press sets the caret where it is, and the anchor too unless Shift is down; the box is taken as the one
// the press is in unless another has a press already.
void EDIT_THIS edit_on_left_down(struct game_editbox *e, int x, int y) {
    (void)y;
    if (!game_edit_press[0]) {
        game_edit_press[0] = &e->win;
        game_edit_press[1] = NULL;
    }
    e->caret = edit_text_position(e, x);
    if (!((uint16_t)game_get_async_key_state(0x10) & 0x8000)) e->anchor = e->caret;
    redraw(e);
}

// The release gives up the press and lets the window know it lost the capture; the caret follows the mouse.
void EDIT_THIS edit_on_left_up(struct game_editbox *e, int x, int y) {
    (void)y;
    if (game_edit_press[0] == &e->win) {
        game_edit_press[0] = NULL;
        vcall0(e, VT_CAPTURE_LOST);
    }
    if (game_edit_press[1] == &e->win) game_edit_press[1] = NULL;
    e->caret = edit_text_position(e, x);
    redraw(e);
}

// A drag moves the caret, for the box that has the press (or the one that has the second kind of press while
// another has the first).
void EDIT_THIS edit_on_mouse_move(struct game_editbox *e, int x, int y) {
    (void)y;
    struct game_win *a = game_edit_press[0], *b = game_edit_press[1];
    if (a != &e->win && !(b == &e->win && a && b)) return;
    e->caret = edit_text_position(e, x);
    redraw(e);
}

// The focus: the blink starts (or stops, with the caret hidden).
void EDIT_THIS edit_on_dialog_focus(struct game_editbox *e, int focus) {
    if (focus) {
        time_start_3(&e->timer);
    } else {
        time_stop(&e->timer);
        e->blink = 0;
    }
    redraw(e);
}

// Pass the focus on: from the box's own place, or, for a box that has flag 1 and is the last of its parent's
// ring, from the first of the ring, the parent's.
void EDIT_THIS edit_pass_dialog_focus(struct game_editbox *e) {
    struct game_win *p = e->win.parent;
    if (!p) return;
    if ((e->flags & 1) && p->dlg_index == p->dlg_count - 1) {
        if (p->dlg_count - 1 >= 0) {
            p->dlg_index = 0;
            p->dlg_cur = p->dlg_head;
        }
        win_pass_dialog_focus(p);
        return;
    }
    win_pass_dialog_focus(&e->win);
}

// Whether the box and its parent (if any) have the dialog focus in their ring.
int EDIT_THIS edit_in_focus(struct game_editbox *e) {
    if (!win_is_dialog_focus(&e->win)) return 0;
    if (e->win.parent && !win_is_dialog_focus(e->win.parent)) return 0;
    return 1;
}

// ---- EditGroup ------------------------------------------------------------------------------------------------

// The Win and the Dialog, through the vbtable; the functions the Win's vtable holds take the Win at its usual
// place, 0x8c into the group (the vbtable's entry 1 says where it really is).
static uint8_t *group_dialog(struct game_editgroup *g) { return (uint8_t *)g + g->vbptr[2]; }
static struct game_buffer *group_buffer(struct game_editgroup *g) { return (struct game_buffer *)((uint8_t *)g + g->vbptr[1] + 0x444); }
static struct game_editgroup *group_of(void *w) { return (struct game_editgroup *)((uint8_t *)w - 0x8c); }
static struct game_win *group_win(struct game_editgroup *g) { return (struct game_win *)((uint8_t *)g + g->vbptr[1]); }
static int32_t *d32(uint8_t *d, int off) { return (int32_t *)(d + off); }

void EDIT_THIS group_set_text(struct game_editgroup *g, char *text, int i) {
    if (g->edit[i]) edit_set_text(g->edit[i], text);
}
char *EDIT_THIS group_get_text(struct game_editgroup *g, int i) { return g->edit[i] ? g->edit[i]->text : NULL; }
// Copy the box's text out (nothing without a box or a place to put it).
void EDIT_THIS group_append_text(struct game_editgroup *g, char *out, int i) {
    if (g->edit[i] && out) {
        out[0] = 0;
        strcpy(out, g->edit[i]->text);
    }
}
void EDIT_THIS group_set_text_limits(struct game_editgroup *g, int n) {
    for (int i = 0; i < 10; i++) g->limit[i] = n;
}

// The group's boxes are told of the dialog focus (the first one alone when it is gained).
void EDIT_THIS group_on_dialog_focus(void *w, int focus) {
    struct game_editgroup *g = group_of(w);
    if (focus) {
        if (g->edit[0]) vcall1(g->edit[0], VT_FOCUS, focus);
        return;
    }
    for (int i = 0; i < 10; i++)
        if (g->edit[i]) vcall1(g->edit[i], VT_FOCUS, 0);
}

// Pass the focus on: the window the Dialog's cursor is at is asked to (slot 0x100), or, if the Dialog has no
// ring or that window is none, the group's Win does it as any window does.
void EDIT_THIS group_pass_dialog_focus(void *w) {
    struct game_editgroup *g = group_of(w);
    struct game_win *win = (struct game_win *)((uint8_t *)w + g->vbptr[1] - 0x8c);
    if (win->dlg_count && win->dlg_head && win->dlg_cur->win) {
        vcall0(win->dlg_cur->win, 0x100);
        return;
    }
    win_pass_dialog_focus(win);
}

// Put a list's cursor on its last item (a seek to -1): the head, then one step back, if there is an item.
static void list_seek_last(struct game_strlist *list) {
    list->cur = list->head;
    if (1 <= list->count) {
        list->cur = list->cur->prev;
        list->idx = list->count - 1;
    }
}

// How big the group is: the Dialog's colours and fonts go to the Buffer, the labels' and the default texts'
// widest widths are measured, and the Dialog's columns, rows and size follow. The Dialog's own fields are
// at 0x20 (flags), 0x2c and 0x30 (the size asked for), 0x34, 0x40, 0x44 (spaces), 0x4c and 0x50 (the number
// of columns and the most rows), and it writes 0x54 to 0x68 (what it worked out); the label list is the Dialog's
// StringStruct at 0xbc. Answers 7 if there are no labels.
int EDIT_THIS group_calculate_dimensions(struct game_editgroup *g) {
    uint8_t *D = group_dialog(g);
    struct game_buffer *B = group_buffer(g);
    struct game_strlist *list = (struct game_strlist *)(D + 0xbc);
    if (*d32(D, 0xcc) == 0) return 7;
    buffer_set_text_color(B, *d32(D, 0x7c), *d32(D, 0x88), *d32(D, 0x94), *d32(D, 0xa0));
    buffer_set_text_color2(B, *d32(D, 0x80), *d32(D, 0x8c), *d32(D, 0x98), *d32(D, 0xa4));
    buffer_set_text_color3(B, *d32(D, 0x84), *d32(D, 0x90), *d32(D, 0x9c), *d32(D, 0xa8));
    buffer_set_font(B, *(struct font **)(D + 0x70), *(struct font **)(D + 0x74), *(struct font **)(D + 0x78), NULL);
    int rows = *d32(D, 0xcc);
    *d32(D, 0x58) = rows < *d32(D, 0x50) ? rows : *d32(D, 0x50);
    // The cursor to the last label (a seek to -1).
    if (rows - 1 >= -1) list_seek_last(list);
    g->label_w = 0, g->edit_w = 0;
    for (int i = 0; i < *d32(D, 0xcc); i++) {
        struct game_strnode *head = list->head;
        if (head) {
            list->cur = list->cur->next;
            if (++list->idx == list->count) list->idx = 0;
        }
        const char *label = NULL;
        if (list->cur) label = *(const char **)((uint8_t *)list->cur->item + 4);
        int w = buffer_text_width(B, label);
        if (w > g->label_w) g->label_w = w;
        w = buffer_text_width(B, g->text[i]);
        if (w > g->edit_w) g->edit_w = w + *d32(D, 0x40) * 2;
    }
    if (g->edit_w <= 10) g->edit_w = 10;
    *d32(D, 0x64) = *d32(D, 0x34) + (*d32(D, 0x44) + *d32(D, 0x40)) * 2 + g->edit_w + g->label_w;
    *d32(D, 0x68) = buffer_text_line_height(B) + *d32(D, 0x44);
    if ((*d32(D, 0x20) & 2) && *d32(D, 0x2c) != 0) {
        *d32(D, 0x5c) = *d32(D, 0x2c);
        *d32(D, 0x54) = *d32(D, 0x2c) / *d32(D, 0x64);
    } else {
        int v = *d32(D, 0x64) * *d32(D, 0x4c);
        if (*d32(D, 0x2c) > v) v = *d32(D, 0x2c);
        *d32(D, 0x5c) = v;
        *d32(D, 0x64) = *d32(D, 0x5c) / *d32(D, 0x4c);
        *d32(D, 0x54) = *d32(D, 0x4c);
    }
    if ((*d32(D, 0x20) & 1) && *d32(D, 0x30) != 0) {
        *d32(D, 0x60) = *d32(D, 0x30);
        *d32(D, 0x58) = *d32(D, 0x30) / *d32(D, 0x68);
        return 0;
    }
    *d32(D, 0x60) = *d32(D, 0x68) * *d32(D, 0x58) + *d32(D, 0x44);
    return 0;
}

// ---- Drawing -----------------------------------------------------------------------------------------------------------------

// The Buffer's first text colour (the one its text is drawn in until a mode is chosen), at 0x53c.
static int32_t buf_text_color(const struct game_buffer *b) { return *(const int32_t *)((const uint8_t *)b + 0x53c); }

// An EditBox draws into its own Buffer: the clip is set to the whole surface and what lies behind is filled (the
// colour `color`), then the frame (`border_w` lines in the two colours, each inside the last), then, with the
// frame taken off again (less `pad` on the sides), the caret if it is showing (a vertical line the height of a line
// in the Buffer's text colour, at the width of the text up to the caret), the text clipped to that rectangle, and the
// selection: the width of the selected text is worked out and its rectangle is recoloured in three steps, text colour
// to a temporary colour (the selection's background plus one), the text's colour to the selected text's colour, and
// the temporary to the background, so the glyph pixels and the background change colours without mixing. The
// selection's background is the Buffer's text colour when it is -1, and the selected text's colour is `color`
// when that is -1. A masked box measures and draws asterisks from the start of its text whatever `first` is (as
// the width questions do). A selection that starts left of `first` starts at `first` but keeps its whole length
// for the width, which makes it too wide.
void EDIT_THIS edit_on_redraw(struct game_editbox *e) {
    char masked[0x100];
    int mk = e->flags & 2;
    if (mk) mask(e, masked);
    struct game_buffer *buf = &e->buf;
    buffer_set_clip(buf, buf->full);
    graphic_win_fill(&e->win, e->color);
    int32_t r[4] = {buf->full[0], buf->full[1], buf->full[2], buf->full[3]};
    for (int i = 0; i < e->border_w; i++) {
        buffer_box_rect(buf, r, e->border_a, e->border_b);
        r[0]++, r[1]++, r[2]--, r[3]--;
    }
    int adj = e->border_w - e->pad;
    r[0] -= adj, r[2] += adj, r[1] -= e->border_w, r[3] += e->border_w;
    int lh = buffer_text_line_height(buf);
    int top = -(lh + buf->neg_height) >> 1;
    if (e->blink) {
        int w = mk ? buffer_text_width_2(buf, masked, e->caret - e->first)
                   : buffer_text_width_2(buf, e->text + e->first, e->caret - e->first);
        int x = w + e->pad;
        buffer_vline(buf, x, top, buffer_text_line_height(buf) + top, buf_text_color(buf));
    }
    buffer_set_clip(buf, r);
    const char *shown = mk ? masked : e->text + e->first;
    buffer_write_l_4(buf, shown, r, (int)strlen(shown));
    if (e->anchor == e->caret) return;
    int x, selw;
    if (e->anchor > e->caret) {
        int w = mk ? buffer_text_width_2(buf, masked, e->caret - e->first)
                   : buffer_text_width_2(buf, e->text + e->first, e->caret - e->first);
        x = w + e->pad;
        selw = buffer_text_width_2(buf, mk ? masked : e->text + e->caret, e->anchor - e->caret);
    } else {
        int lo = e->anchor > e->first ? e->anchor : e->first;
        int w = mk ? buffer_text_width_2(buf, masked, lo - e->first) : buffer_text_width_2(buf, e->text + e->first, lo - e->first);
        x = w + e->pad;
        selw = buffer_text_width_2(buf, mk ? masked : e->text + lo, e->caret - e->anchor);
    }
    int32_t back = e->sel_back != -1 ? e->sel_back : buf_text_color(buf);
    int32_t fore = e->sel_text != -1 ? e->sel_text : e->color;
    buffer_change_color_3(buf, x, top, selw, buffer_text_line_height(buf), e->color, back + 1);
    buffer_change_color_3(buf, x, top, selw, buffer_text_line_height(buf), buf_text_color(buf), fore);
    buffer_change_color_3(buf, x, top, selw, buffer_text_line_height(buf), back + 1, back);
}

// An EditGroup draws what lies behind it, copied from its parent's Buffer at the place the Dialog keeps (0x24 and
// 0x28), makes the parent the current window, then writes the labels: the Dialog's columns (0x54) of rows (0x58),
// each cell `0x64` wide and `0x68` high, from the left margin (`0x34` and the space `0x40`) and half the Dialog's
// space (`0x44`) down, the label list's cursor moving on a label at a time (from the last label, as the
// dimensions' seek leaves it) and wrapping, so a group with fewer labels than cells shows them over again. Without
// a parent nothing is done. A cursor that has no list head but a node (never so in the game) draws nothing; the
// original would read a null item.
void EDIT_THIS group_on_redraw(void *w) {
    struct game_editgroup *g = group_of(w);
    struct game_win *parent = group_win(g)->parent;
    if (!parent) return;
    *game_win_cur = parent;
    uint8_t *D = group_dialog(g);
    struct game_buffer *B = group_buffer(g);
    struct game_strlist *list = (struct game_strlist *)(D + 0xbc);
    buffer_copy_full(dc_win_buf(parent), B, *d32(D, 0x24), *d32(D, 0x28), 0, 0, B->width, -B->neg_height);
    int x1 = *d32(D, 0x34) + *d32(D, 0x40);
    int x2 = *d32(D, 0x64) + x1;
    int y1 = *d32(D, 0x44) >> 1;
    int y2 = *d32(D, 0x68) - *d32(D, 0x44) + y1;
    if (list->count - 1 >= -1) list_seek_last(list);
    for (int col = 0; col < *d32(D, 0x54); col++) {
        int32_t r[4] = {x1 + *d32(D, 0x64) * col, y1, x2 + *d32(D, 0x64) * col, y2};
        for (int row = 0; row < *d32(D, 0x58); row++) {
            const char *label = NULL;
            if (list->head) {
                list->cur = list->cur->next;
                if (++list->idx == list->count) list->idx = 0;
            }
            if (list->cur && list->head) label = *(const char **)((uint8_t *)list->cur->item + 4);
            if (label) buffer_write_l_4(B, label, r, (int)strlen(label));
            r[1] += *d32(D, 0x68), r[3] += *d32(D, 0x68);
        }
    }
}

// ---- Shadow checks ----------------------------------------------------------------------------------------------------

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call
typedef char *charp;
#define SHADOW_RESULT_charp(v) char *v =
#define SHADOW_INT_charp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_charp(v) return v
#define SHADOW_TAIL_charp(call) return call

// What the stand-in slots a sweep gives boxes, groups and parents leave behind is a count and a hash of their
// calls and arguments, so a different call, argument or order shows as a difference in these eight bytes.
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }

// The memory the editing keys can change in a box: the text, the first character shown, the selection and the
// keys, and the Buffer measuring text changes (and the slack the font keeps).
#define KEY_WRITES(e)                                                                                           \
    (WRITES((e)->text, sizeof (e)->text), WRITES(&(e)->first, 4), WRITES(&(e)->mods, 16), WRITES(&(e)->buf, sizeof (e)->buf), \
     WRITES(game_font_slack, 4), writes_vlog(&w_))
#define CARET_WRITES(e) (WRITES(&(e)->anchor, 8), WRITES(&(e)->buf, sizeof (e)->buf), WRITES(game_font_slack, 4), writes_vlog(&w_))
#define PRESS_WRITES (WRITES(game_edit_press, 2 * sizeof *game_edit_press))
#define TIME_WRITES(t) (WRITES((t), sizeof *(t)), WRITES(game_time_flush_flag, 4))

SHADOW_WRITER_T(EDIT, void, edit_set_text, (struct game_editbox * e, char *text), (e, text),
                (WRITES(&e->first, 4), WRITES(e->text, sizeof e->text), WRITES(&e->anchor, 8), text ? WRITES(text, strlen(text) + 1) : 0,
                 writes_vlog(&w_)),
                "%d, %s", P(e), text ? text : "(null)")
SHADOW_WRITER(EDIT, int, edit_caret_callback, (struct game_editbox * e), (e), (WRITES(&e->blink, 4), writes_vlog(&w_)), "%d", P(e))
SHADOW_THIS_FN(EDIT, int, edit_text_fits, (struct game_editbox * e), (e), "%d", P(e))
SHADOW_THIS_FN(EDIT, int, edit_text_position, (struct game_editbox * e, int x), (e, x), "%d, %d", P(e), x)
SHADOW_WRITER_T(EDIT, int, edit_on_key_click, (struct game_editbox * e, int key, int a), (e, key, a), KEY_WRITES(e), "%d, %d, %d", P(e), key,
                a)
SHADOW_WRITER_T(EDIT, int, edit_on_key_down, (struct game_editbox * e, int key), (e, key), KEY_WRITES(e), "%d, 0x%x", P(e), key)
SHADOW_WRITER_T(EDIT, void, edit_on_left_down, (struct game_editbox * e, int x, int y), (e, x, y), (CARET_WRITES(e), PRESS_WRITES),
                "%d, %d, %d", P(e), x, y)
SHADOW_WRITER_T(EDIT, void, edit_on_left_up, (struct game_editbox * e, int x, int y), (e, x, y), (CARET_WRITES(e), PRESS_WRITES),
                "%d, %d, %d", P(e), x, y)
SHADOW_WRITER_T(EDIT, void, edit_on_mouse_move, (struct game_editbox * e, int x, int y), (e, x, y), (CARET_WRITES(e), PRESS_WRITES),
                "%d, %d, %d", P(e), x, y)
SHADOW_WRITER_T(EDIT, void, edit_on_dialog_focus, (struct game_editbox * e, int focus), (e, focus),
                (TIME_WRITES(&e->timer), WRITES(&e->blink, 4), writes_vlog(&w_)), "%d, %d", P(e), focus)
SHADOW_THIS_FN(EDIT, int, edit_in_focus, (struct game_editbox * e), (e), "%d", P(e))

SHADOW_WRITER_T(EDIT, void, group_set_text, (struct game_editgroup * g, char *text, int i), (g, text, i),
                (g->edit[i] ? (WRITES(&g->edit[i]->first, 4), WRITES(g->edit[i]->text, sizeof g->edit[i]->text),
                               WRITES(&g->edit[i]->anchor, 8), text ? WRITES(text, strlen(text) + 1) : 0)
                            : 0,
                 writes_vlog(&w_)),
                "%d, %d, %d", P(g), P(text), i)
SHADOW_THIS_FN(EDIT, charp, group_get_text, (struct game_editgroup * g, int i), (g, i), "%d, %d", P(g), i)
SHADOW_WRITER_T(EDIT, void, group_append_text, (struct game_editgroup * g, char *out, int i), (g, out, i),
                (out && g->edit[i] ? WRITES(out, strlen(g->edit[i]->text) + 1) : 0), "%d, %d, %d", P(g), P(out), i)
SHADOW_WRITER_T(EDIT, void, group_set_text_limits, (struct game_editgroup * g, int n), (g, n), WRITES(g->limit, sizeof g->limit), "%d, %d",
                P(g), n)
#define DIM_WRITES(g)                                                                                  \
    (WRITES(group_buffer(g), sizeof(struct game_buffer)), WRITES(game_font_slack, 4), WRITES(&(g)->label_w, 8),      \
     WRITES(group_dialog(g) + 0x54, 0x6c - 0x54), WRITES(group_dialog(g) + 0xc8, 0xd4 - 0xc8))
SHADOW_WRITER_T(EDIT, int, group_calculate_dimensions, (struct game_editgroup * g), (g), DIM_WRITES(g), "%d", P(g))

// Drawing: the Buffer's pixels and the text state, and for a group the window made current and the label cursor.
// The box sets a clip region (a Windows object the check can't put back), so the game's calls run the original and
// the sweep checks Chiron's code on twin boxes.
SHADOW_DECLARE_T(void, edit_on_redraw, (struct game_editbox * e))
static void THISCALL shadow_edit_on_redraw(struct game_editbox *e) {
    if (!hooks_nested && !shadow_bypass()) {
        stats_edit_on_redraw.calls++;
        shadow_tick(EDIT);
    }
    orig_edit_on_redraw(e);
}
#define GROUP_DRAW_WRITES(g)                                                                              \
    (dc_writes_pixels(&w_, group_buffer(g)), dc_writes_text_state(&w_, group_buffer(g)), WRITES(game_win_cur, 4), \
     WRITES(group_dialog(g) + 0xc8, 0xd4 - 0xc8))
DRAW_CHECKED(EDIT, group_on_redraw, (void *w), (w), dc_win_dd(group_win(group_of(w))), GROUP_DRAW_WRITES(group_of(w)), "%d", P(w))

// The handlers that call another window's handler: the game's calls run the original, are counted (to start
// the sweep), and the sweep checks them on scratch objects.
#define SWEEP_ONLY(ret, name, params, args, writes, fmt, ...)             \
    SHADOW_DECLARE_T(ret, name, params)                                   \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__) \
    static ret THISCALL shadow_##name params {                            \
        if (!hooks_nested && !shadow_bypass()) {                          \
            stats_##name.calls++;                                         \
            shadow_tick(EDIT);                                            \
        }                                                                 \
        SHADOW_TAIL_##ret(orig_##name args);                              \
    }

// The scratch objects the sweep builds.
enum { NE = 3, NP = 2, NG = 2, GSIZE = 0xc00, NLAB = 5 };
struct scratch {
    struct game_editbox box[NE];
    struct game_win parent[NP];
    struct game_win child[NP * 3];
    struct game_win_node nodes[NP][3];
    uint8_t group[NG][GSIZE];  // an EditGroup: the vbtable pointer, its Win at 0x8c and its Dialog at 0xaa4
    int32_t vbtable[NG][3];
    struct game_editbox gbox[NG][10];
    struct game_strnode lnodes[NG][NLAB];
    uint8_t litems[NG][NLAB][16];
    char label[NG][NLAB][12];
    char dtext[NG][10][12];
};
static struct scratch S;
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, &S, sizeof S);
    writes_vlog(w);
}

SWEEP_ONLY(void, edit_pass_dialog_focus, (struct game_editbox * e), (e), (writes_scratch(&w_), WRITES(game_win_prev_focus, 4)), "%d", P(e))
SWEEP_ONLY(void, group_on_dialog_focus, (void *w, int focus), (w, focus), (writes_scratch(&w_), WRITES(game_win_prev_focus, 4)),
           "%d, %d", P(w), focus)
SWEEP_ONLY(void, group_pass_dialog_focus, (void *w), (w), (writes_scratch(&w_), WRITES(game_win_prev_focus, 4)), "%d", P(w))

// ---- Sweep -----------------------------------------------------------------------------------------------------------------------

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void vrec(int slot, const void *o, int a) {
    vlog.hash = ((vlog.hash * 31u + (uint32_t)slot) * 31u + (uint32_t)(uintptr_t)o) * 31u + (uint32_t)a;
    vlog.n++;
}
static int THISCALL stub_0x10(void *o) { return vrec(0x10, o, 0), 0x11; }
static int THISCALL stub_0xcc(void *o, int a) { return vrec(0xcc, o, a), 0xcd; }
static int THISCALL stub_0xf8(void *o) { return vrec(0xf8, o, 0), 0xf9; }
static int parent_copy_answer;  // what a scratch parent answers to the window fill's question (slot 0xf4)
static int THISCALL stub_0xf4(void *o) { return vrec(0xf4, o, 0), parent_copy_answer; }
static int THISCALL stub_0x100(void *o) { return vrec(0x100, o, 0), 0x101; }
static void *stub_vt[0x180 / 4];
// Each multimedia timer the original's run starts is a thread with an LDT entry of its own, which the loader never gives back,
// so a sweep lets only a few of the boxes' timers be one (see strings.c).
static int mm_budget;
// The function a scratch box's timer holds: nothing (a message its real timer posts may be handled later).
static void __attribute__((cdecl)) noop_cb(void *arg) { (void)arg; }

// The Font for the scratch boxes (made once, as text.c's sweep makes its own).
static struct font mine;
static int made;

static void make_text(char *out, int max) {
    static const char alphabet[] = "abc xyz  WW ii..mm";
    int n = rnd_to(max + 1);
    for (int i = 0; i < n; i++) out[i] = alphabet[rnd_to((int)sizeof alphabet - 1)];
    out[n] = 0;
}

// A box with a random state. Its Buffer has the sweep's Font and no markup, as the box's own text measuring wants.
static void build_box(struct game_editbox *e) {
    memset(e, 0, sizeof *e);
    e->win.vtable = stub_vt;
    e->win.parent = rnd_to(8) ? &S.parent[rnd_to(NP)] : NULL;
    // (the dialog flag, which in_focus asks about, on the box and on its parent)
    e->win.flags = rnd_to(2) ? WIN_F_DIALOG : 0;
    if (e->win.parent) e->win.parent->flags = rnd_to(2) ? WIN_F_DIALOG : 0;
    e->flags = rnd_to(4);
    e->pad = rnd_to(6);
    e->buf.width = 30 + rnd_to(150), e->buf.neg_height = -(14 + rnd_to(8));
    *(struct font **)((uint8_t *)&e->buf + 0x52c) = &mine;
    make_text(e->text, rnd_to(6) ? 25 : 0xf0);
    int len = (int)strlen(e->text);
    e->max_len = rnd_to(8) ? (uint32_t)(len + rnd_to(10)) : (uint32_t)rnd_to(len + 2);
    e->caret = rnd_to(len + 1), e->anchor = rnd_to(3) ? e->caret : rnd_to(len + 1);
    e->first = rnd_to(2) ? 0 : rnd_to(e->caret + 1);
    e->mods = rnd_to(4), e->blink = rnd_to(2);
    e->color = (int)rnd();
    e->timer.flags = (uint32_t)rnd_to(4);
    e->timer.cb = NULL, e->timer.cb2 = NULL;
}
static void scrub_timers(unsigned mark) {
    if (hooks_timers_kill_from) hooks_timers_kill_from(mark);
    for (int i = 0; i < NE; i++) game_kill_timer(*game_win_hwnd, (uint32_t)(uintptr_t)&S.box[i].timer);
    for (int g = 0; g < NG; g++)
        for (int i = 0; i < 10; i++) game_kill_timer(*game_win_hwnd, (uint32_t)(uintptr_t)&S.gbox[g][i].timer);
}

static void build_group(int k) {
    uint8_t *raw = S.group[k];
    memset(raw, 0, GSIZE);
    S.vbtable[k][0] = 0, S.vbtable[k][1] = 0x8c, S.vbtable[k][2] = 0xaa4;
    struct game_editgroup *g = (struct game_editgroup *)raw;
    g->vbptr = S.vbtable[k];
    *(void ***)(raw + 0x8c) = stub_vt;  // the Win
    struct game_win *win = (struct game_win *)(raw + 0x8c);
    int n = rnd_to(NLAB + 1);
    uint8_t *D = raw + 0xaa4;
    for (int i = 0; i < 10; i++) {
        build_box(&S.gbox[k][i]);
        g->edit[i] = rnd_to(4) ? &S.gbox[k][i] : NULL;
        make_text(S.dtext[k][i], 12 - 2);
        g->text[i] = rnd_to(8) ? S.dtext[k][i] : (char *)"";
        g->limit[i] = rnd_to(40);
    }
    // The Win's dialog ring: children for the focus.
    int cn = rnd_to(4);
    for (int i = 0; i < cn; i++) {
        struct game_win *c = &S.child[k * 3 + i % 3];
        c->vtable = stub_vt;
        S.nodes[k][i % 3].win = c;
        S.nodes[k][i % 3].next = &S.nodes[k][(i + 1) % cn % 3], S.nodes[k][i % 3].prev = &S.nodes[k][(i + cn - 1) % cn % 3];
    }
    win->dlg_count = cn, win->dlg_head = cn ? &S.nodes[k][0] : NULL, win->dlg_cur = cn ? &S.nodes[k][rnd_to(cn) % 3] : NULL;
    win->dlg_index = rnd_to(cn + 1);
    // The Win's parent, with a ring of its own (the group passes its focus to it when its own ring has nothing to pass it to).
    struct game_win *par = rnd_to(3) ? &S.parent[(k + 1) % NP] : NULL;
    win->parent = par;
    if (par) {
        int q = (k + 1) % NP;
        par->vtable = stub_vt;
        for (int j = 0; j < 3; j++) {
            S.child[q * 3 + j].vtable = stub_vt;
            S.nodes[q][j].win = &S.child[q * 3 + j];
            S.nodes[q][j].next = &S.nodes[q][(j + 1) % 3], S.nodes[q][j].prev = &S.nodes[q][(j + 2) % 3];
        }
        par->dlg_count = rnd_to(4), par->dlg_head = par->dlg_count ? &S.nodes[q][0] : NULL;
        par->dlg_cur = &S.nodes[q][rnd_to(3)], par->dlg_index = rnd_to(3);
    }
    // The Dialog: labels, spacing, sizes and colours.
    struct game_strlist *list = (struct game_strlist *)(D + 0xbc);
    for (int i = 0; i < n; i++) {
        S.lnodes[k][i].id = i;
        S.lnodes[k][i].next = &S.lnodes[k][(i + 1) % n], S.lnodes[k][i].prev = &S.lnodes[k][(i + n - 1) % n];
        make_text(S.label[k][i], 9);
        char *tp = S.label[k][i];
        memcpy(S.litems[k][i] + 4, &tp, sizeof tp);
        S.lnodes[k][i].item = S.litems[k][i];
    }
    list->head = n ? &S.lnodes[k][0] : NULL, list->count = n, list->cur = n ? &S.lnodes[k][rnd_to(n)] : NULL;
    list->idx = n ? rnd_to(n) : 0;
    *d32(D, 0x20) = rnd_to(4);
    *d32(D, 0x2c) = rnd_to(3) ? rnd_to(400) : 0, *d32(D, 0x30) = rnd_to(3) ? rnd_to(300) : 0;
    *d32(D, 0x34) = rnd_to(8), *d32(D, 0x40) = rnd_to(5), *d32(D, 0x44) = rnd_to(6);
    *d32(D, 0x4c) = 1 + rnd_to(4), *d32(D, 0x50) = rnd_to(8);
    for (int off = 0x54; off < 0x6c; off += 4) *d32(D, off) = (int)rnd();
    *(struct font **)(D + 0x70) = rnd_to(6) ? &mine : NULL;
    *(struct font **)(D + 0x74) = rnd_to(2) ? &mine : NULL, *(struct font **)(D + 0x78) = rnd_to(2) ? &mine : NULL;
    for (int off = 0x7c; off <= 0xa8; off += 4) *d32(D, off) = (int)rnd();
    // The Buffer: the font text is measured with.
    *(struct font **)((uint8_t *)group_buffer(g) + 0x52c) = &mine;
    g->f84 = (int)rnd();
}

// The caret, key and mouse functions on every scratch box.
static void sweep_boxes(void) {
    static char buf[300];
    static const int keys[] = {0x08, 0x09, 0x23, 0x24, 0x25, 0x27, 0x2e, 0x64, 0x66, 0x26, 0x41, 0x0d, 0x1b, 0x70, 0x6a};
    for (int i = 0; i < NE; i++) {
        struct game_editbox *e = &S.box[i];
        for (int k = 0; k < 14; k++) {
            build_box(e);
            *game_win_prev_focus = NULL;
            for (int m = 0; m < 3; m++) {
                int key = keys[rnd_to(15)] | (rnd_to(3) == 0 ? 0x10000 : 0) | (rnd_to(3) == 0 ? 0x20000 : 0);
                // Home and End often (they change `first`), Backspace and Delete with a selection, Right and Left at the ends.
                if (rnd_to(4) == 0) e->anchor = rnd_to((int)strlen(e->text) + 1);
                if (rnd_to(6) == 0) e->caret = 0;
                if (rnd_to(6) == 0) e->caret = (int)strlen(e->text);
                check_edit_on_key_down(e, key);
                if (rnd_to(3) == 0) e->mods = 0;
            }
            int c;
            switch (rnd_to(5)) {
            case 0: c = rnd_to(0x20) - 3; break;
            case 1: c = 0x7f + rnd_to(130); break;
            default: c = 'a' + rnd_to(26); break;
            }
            if (rnd_to(8) == 0) e->anchor = rnd_to((int)strlen(e->text) + 1);
            if (rnd_to(5) == 0) e->max_len = (uint32_t)strlen(e->text);
            check_edit_on_key_click(e, c, rnd_to(3));
            // The text (some too long for the box), and the width questions.
            make_text(buf, rnd_to(4) ? 20 : 280);
            check_edit_set_text(e, rnd_to(10) ? buf : NULL);
            e->caret = rnd_to((int)strlen(e->text) + 1), e->first = rnd_to(e->caret + 1);
            check_edit_text_fits(e);
            for (int m = 0; m < 4; m++) {
                int x = rnd_to(e->buf.width + 20) - 8;
                if (rnd_to(5) == 0) e->flags ^= 2;
                check_edit_text_position(e, x);
            }
            // The mouse.
            for (int m = 0; m < 3; m++) {
                game_edit_press[0] = rnd_to(2) ? &e->win : (rnd_to(2) ? &S.box[(i + 1) % NE].win : NULL);
                game_edit_press[1] = rnd_to(2) ? &e->win : (rnd_to(2) ? &S.box[(i + 1) % NE].win : NULL);
                int x = rnd_to(e->buf.width + 20) - 8;
                e->anchor = rnd_to((int)strlen(e->text) + 1);
                check_edit_on_left_down(e, x, 3);
                game_edit_press[0] = rnd_to(2) ? &e->win : NULL, game_edit_press[1] = rnd_to(2) ? &e->win : NULL;
                check_edit_on_left_up(e, x, 3);
                game_edit_press[0] = rnd_to(2) ? &e->win : (rnd_to(2) ? &S.box[(i + 1) % NE].win : NULL);
                game_edit_press[1] = rnd_to(2) ? &e->win : (rnd_to(2) ? &S.box[(i + 1) % NE].win : NULL);
                check_edit_on_mouse_move(e, x, 3);
            }
            check_edit_caret_callback(e);
            check_edit_in_focus(e);
            // The focus: the Time object starts and stops (real timers; see scrub_timers).
            unsigned mark = hooks_timer_mark ? hooks_timer_mark() : 0;
            e->timer.cb = rnd_to(3) ? noop_cb : NULL;
            e->timer.delay = rnd_to(2) ? 500 : mm_budget > 0 && rnd_to(5) == 0 ? (mm_budget--, (uint32_t)rnd_to(0x32)) : 0x40u;
            e->timer.id = rnd_to(2) ? 0 : 0x13370000u + (uint32_t)rnd_to(9000);
            check_edit_on_dialog_focus(e, rnd_to(2));
            scrub_timers(mark);
            // The parent's ring (for pass_dialog_focus).
            struct game_win *p = &S.parent[rnd_to(NP)];
            e->win.parent = rnd_to(8) ? p : NULL;
            p->vtable = stub_vt;
            p->dlg_count = rnd_to(4), p->dlg_head = p->dlg_count ? &S.nodes[0][0] : NULL;
            for (int j = 0; j < 3; j++) {
                S.nodes[0][j].win = &S.child[j];
                S.nodes[0][j].next = &S.nodes[0][(j + 1) % 3], S.nodes[0][j].prev = &S.nodes[0][(j + 2) % 3];
                S.child[j].vtable = stub_vt;
            }
            p->dlg_cur = &S.nodes[0][rnd_to(3)];
            p->dlg_index = p->dlg_count ? rnd_to(p->dlg_count + 1) - (rnd_to(4) == 0) : 0;
            if (rnd_to(3) == 0 && p->dlg_count) p->dlg_index = p->dlg_count - 1;
            e->flags = rnd_to(4);
            check_edit_pass_dialog_focus(e);
            memset(&p->dlg_index, 0, sizeof p->dlg_index);
        }
    }
}

static void sweep_groups(void) {
    for (int k = 0; k < NG; k++) {
        for (int j = 0; j < 8; j++) {
            build_group(k);
            struct game_editgroup *g = (struct game_editgroup *)S.group[k];
            static char buf[300];
            make_text(buf, 40);
            int i = rnd_to(10);
            check_group_set_text(g, rnd_to(8) ? buf : NULL, i);
            check_group_get_text(g, rnd_to(10));
            check_group_append_text(g, rnd_to(6) ? buf : NULL, rnd_to(10));
            check_group_set_text_limits(g, rnd_to(60) - 5);
            check_group_calculate_dimensions(g);
            struct game_win *win = group_win(g);
            *game_win_prev_focus = NULL;
            unsigned mark = hooks_timer_mark ? hooks_timer_mark() : 0;
            check_group_on_dialog_focus(win, rnd_to(2));
            check_group_on_dialog_focus(win, 0);
            scrub_timers(mark);
            check_group_pass_dialog_focus(win);
        }
    }
}

// ---- Drawing sweep -------------------------------------------------------------------------------------------------------------

// Parents with Buffers of their own (pixels from noise), for what the window fill copies from, and the real Buffer
// (a DIB, with the sweep's Font) the boxes and groups draw into.
enum { PW = 100, PH = 30 };
struct dparent {
    struct game_win win;
    struct game_buffer buf;
    uint8_t pix[PW * PH + 16];
};
static struct dparent DP[2];
static uint8_t tint[256];

static void noise(uint8_t *p, int n) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t)rnd_to(8);
}
static void random_rect(int32_t *r) {
    r[0] = rnd_to(20) - 5, r[1] = rnd_to(20) - 5, r[2] = r[0] + rnd_to(30), r[3] = r[1] + rnd_to(30);
}
static void build_parents(void) {
    for (int i = 0; i < 2; i++) {
        struct dparent *p = &DP[i];
        memset(p, 0, sizeof *p);
        p->win.vtable = stub_vt;
        noise(p->pix, sizeof p->pix);
        p->buf.bits = p->pix, p->buf.width = PW, p->buf.neg_height = -PH, p->buf.pitch = PW;
        p->buf.full[2] = PW, p->buf.full[3] = PH, p->buf.clip[2] = PW, p->buf.clip[3] = PH;
    }
}
// A colour from the few the Buffer's pixels hold, so a recolouring is seen.
static int small_color(void) { return rnd_to(8); }

// Twin boxes, with Buffers of their own (DIBs made by the game's code and given the sweep's Font): the original
// draws on one and Chiron's code on the other, from the same state, and the Buffers (fields, device context state
// and pixels), the box and what was asked of the parent are compared. After a difference the twins are made again.
static struct game_editbox TA, TB;
static int twins_made;
static void make_twins(void) {
    for (int i = 0; i < 2; i++) {
        struct game_editbox *t = i ? &TB : &TA;
        if (twins_made) game_buffer_dtor(&t->buf);
        memset(t, 0, sizeof *t);
        game_buffer_ctor(&t->buf);
        game_buffer_init(&t->buf, PW, 24, 0, 0);
        buffer_set_font(&t->buf, &mine, &mine, &mine, &mine);
    }
    twins_made = 1;
}
static void twin_redraw(struct game_editbox *a, struct game_editbox *b) {
    // The same pixels and the same box in both.
    for (int i = 0; i < a->buf.pitch * -a->buf.neg_height; i++) a->buf.bits[i] = b->buf.bits[i] = (uint8_t)rnd_to(8);
    struct game_buffer ka = a->buf, kb = b->buf;
    memcpy(b, a, sizeof *b);
    b->buf = kb, a->buf = ka;
    uint32_t h0 = vlog.hash;
    int32_t n0 = vlog.n;
    hooks_nested++;
    orig_edit_on_redraw(a);
    hooks_nested--;
    uint32_t h1 = vlog.hash;
    int32_t n1 = vlog.n;
    vlog.hash = h0, vlog.n = n0;
    edit_on_redraw(b);
    const char *d = NULL;
    if (vlog.hash != h1 || vlog.n != n1) d = "calls to the parent";
    if (!d) d = buffer_twin_diff(&a->buf, &b->buf, 0, 0);
    if (!d) d = buffer_pixels_diff(&a->buf, &b->buf);
    if (!d && memcmp(a, b, offsetof(struct game_editbox, buf))) d = "the box";
    if (!d && memcmp(&a->text, &b->text, sizeof *a - offsetof(struct game_editbox, text))) d = "the box's fields";
    if (d) {
        hooks_mismatch(&stats_edit_on_redraw, " (%s)", d);
        make_twins();
    }
}

static void sweep_draw(struct game_buffer *real) {
    const uint8_t *saved_tint = *game_win_tint;
    struct game_win *saved_cur = *game_win_cur;
    for (int round = 0; round < 16; round++) {
        build_parents();
        for (int i = 0; i < 256; i++) tint[i] = (uint8_t)rnd_to(8);
        *game_win_tint = rnd_to(2) ? tint : NULL;
        buffer_set_text_color(real, small_color(), small_color(), small_color(), small_color());
        noise(real->bits, (int)(real->pitch * -real->neg_height));
        for (int k = 0; k < 12; k++) {
            struct game_editbox *e = &TA;
            struct game_buffer keep = e->buf;
            build_box(e);
            e->buf = keep;
            buffer_set_text_color(&TA.buf, small_color(), small_color(), small_color(), small_color());
            buffer_set_text_color(&TB.buf, *(int32_t *)((uint8_t *)&TA.buf + 0x53c), *(int32_t *)((uint8_t *)&TA.buf + 0x54c),
                                  *(int32_t *)((uint8_t *)&TA.buf + 0x55c), *(int32_t *)((uint8_t *)&TA.buf + 0x56c));
            e->win.parent = rnd_to(6) ? &DP[rnd_to(2)].win : NULL;
            e->win.flags = rnd_to(2) ? 0x80000 : 0;
            random_rect(e->win.inner), random_rect(e->win.outer);
            parent_copy_answer = rnd_to(4) != 0;
            e->color = small_color();
            e->sel_text = rnd_to(3) ? small_color() : -1, e->sel_back = rnd_to(3) ? small_color() : -1;
            e->border_a = small_color(), e->border_b = small_color(), e->border_w = rnd_to(4);
            e->pad = rnd_to(8), e->blink = rnd_to(2);
            int len = (int)strlen(e->text);
            // The caret and selection around `first`, as typing leaves them, and any other way too.
            e->caret = rnd_to(len + 1);
            e->first = rnd_to(2) ? 0 : rnd_to(e->caret + 1);  // (never past the caret, as the game keeps it)
            e->anchor = rnd_to(3) == 0 ? e->caret : rnd_to(len + 1);
            for (int v = 0; v < 4; v++) {
                twin_redraw(&TA, &TB);
                e->flags ^= rnd_to(2) ? 2 : 0;
                e->blink = !e->blink;
                e->anchor = rnd_to(2) ? e->caret : rnd_to(len + 1);
                if (rnd_to(2)) e->win.flags ^= 0x80000;
                e->border_w = rnd_to(4), e->pad = rnd_to(8);
                e->caret = rnd_to(len + 1), e->first = rnd_to(2) ? 0 : rnd_to(e->caret + 1);
                e->color = small_color();
            }
        }
        for (int k = 0; k < NG; k++) {
            for (int j = 0; j < 3; j++) {
                build_group(k);
                struct game_editgroup *g = (struct game_editgroup *)S.group[k];
                struct game_win *win = group_win(g);
                uint8_t *D = group_dialog(g);
                win->parent = rnd_to(6) ? &DP[rnd_to(2)].win : NULL;
                memcpy(group_buffer(g), real, sizeof *real);
                *d32(D, 0x24) = rnd_to(40), *d32(D, 0x28) = rnd_to(40);
                *d32(D, 0x34) = rnd_to(8), *d32(D, 0x40) = rnd_to(5), *d32(D, 0x44) = rnd_to(8);
                *d32(D, 0x54) = rnd_to(4), *d32(D, 0x58) = rnd_to(5);
                *d32(D, 0x64) = 10 + rnd_to(40), *d32(D, 0x68) = 8 + rnd_to(14);
                check_group_on_redraw(win);
                *d32(D, 0x58) = 1 + rnd_to(6);
                check_group_on_redraw(win);
            }
        }
    }
    *game_win_tint = saved_tint;
    *game_win_cur = saved_cur;
}

static void life_sweep_edit(void);
static void edit_sweep(void) {
    if (!*game_win_hwnd) return;
    // The sweep needs the game's fonts (as text.c's sweep does): the game hasn't set them up yet before its first screen.
    if (!*game_font_default || !*game_font_dc) return;
    if (!made) font_ctor(&mine), game_font_init(&mine, "Arial", 12, 0), made = 1;
    if (!mine.hfont) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x4e5a91c3u ^ (uint32_t)*game_turn * 2654435761u;
    mm_budget = 1;
    if (!rng_state) rng_state = 1;
    // A Buffer with pixels of its own and the sweep's Font, made outside any check and kept.
    static struct game_buffer real;
    static int real_made;
    if (!real_made) {
        game_buffer_ctor(&real);
        game_buffer_init(&real, PW, 24, 0, 0);
        if (real.bits) buffer_set_font(&real, &mine, &mine, &mine, &mine), real_made = 1, make_twins();
    }
    stub_vt[0x10 / 4] = (void *)stub_0x10, stub_vt[0xcc / 4] = (void *)stub_0xcc, stub_vt[0xf8 / 4] = (void *)stub_0xf8;
    stub_vt[0x100 / 4] = (void *)stub_0x100, stub_vt[0xf4 / 4] = (void *)stub_0xf4;
    struct game_win *press[2] = {game_edit_press[0], game_edit_press[1]};
    struct game_win *prev = *game_win_prev_focus;
    memset(&S, 0, sizeof S);
    for (int p = 0; p < NP; p++) S.parent[p].vtable = stub_vt;
    for (int round = 0; round < 10; round++) {
        sweep_boxes();
        sweep_groups();
    }
    if (real_made) sweep_draw(&real);
    menu_draw_sweep();
    memset(&S, 0, sizeof S);  // (a timer message that arrives later finds nothing to do)
    game_edit_press[0] = press[0], game_edit_press[1] = press[1];
    *game_win_prev_focus = prev;
    life_sweep_edit();
    hooks_sweeping = sweeping;
}

// ---- Life cycle -----------------------------------------------------------------------------------

// An EditBox made and unmade: a GraphicWin with its text (inline, 0xa14), the game's defaults for its
// colours and frame, and a Time object for the caret's blink. The constructor and close start the same
// fields; close stops the blink and closes the GraphicWin, and the destructor closes and unmakes the
// Time and the GraphicWin. These make Windows objects (the Buffer) and the Time may have a timer, so in
// shadow mode the game's calls run the original and the sweep checks Chiron's code on twin boxes
// (life.c). The exception frames are not reproduced (nothing reads them). Differences from
// OpenSMACX: it has none of these.

#define EB_VTABLE 0x66a038
#define EB_BUFFER_VTABLE 0x66a030

static void edit_start_fields(struct game_editbox *e) {
    e->text[0] = 0;
    e->max_len = 10;
    e->border_a = *(int32_t *)game_at(0x9b90b8);
    e->border_b = *(int32_t *)game_at(0x9b90bc);
    e->border_w = *(int32_t *)game_at(0x6971e0);
    e->pad = *(int32_t *)game_at(0x6971e4);
    e->color = *(int32_t *)game_at(0x6971dc);
    e->sel_text = *(int32_t *)game_at(0x6971f8);
    e->sel_back = *(int32_t *)game_at(0x6971f4);
    e->mods = 0, e->anchor = 0, e->caret = 0, e->blink = 0, e->first = 0, e->flags = 0;
}

void *EDIT_THIS edit_ctor(struct game_editbox *e) {
    graphic_win_ctor(&e->win);
    time_ctor(&e->timer);
    e->win.vtable = (void **)(uintptr_t)game_va(EB_VTABLE);
    e->buf.vtable = (void **)(uintptr_t)game_va(EB_BUFFER_VTABLE);
    edit_start_fields(e);
    return e;
}

void EDIT_THIS edit_close(struct game_editbox *e) {
    edit_start_fields(e);
    time_close(&e->timer);
    graphic_win_close(&e->win);
}

void EDIT_THIS edit_dtor(struct game_editbox *e) {
    e->win.vtable = (void **)(uintptr_t)game_va(EB_VTABLE);
    e->buf.vtable = (void **)(uintptr_t)game_va(EB_BUFFER_VTABLE);
    edit_close(e);
    time_dtor(&e->timer);
    graphic_win_dtor(&e->win);
}

// ---- Life cycle: shadow mode and the sweep ----

SHADOW_PASS_T(EDIT, void *, edit_ctor, (struct game_editbox * e), (e))
SHADOW_PASS_T(EDIT, void, edit_close, (struct game_editbox * e), (e))
SHADOW_PASS_T(EDIT, void, edit_dtor, (struct game_editbox * e), (e))

LIFE_FN(edit_ctor, orig_edit_ctor(o), edit_ctor(o))
LIFE_FN(edit_close, orig_edit_close(o), edit_close(o))
LIFE_FN(edit_dtor, orig_edit_dtor(o), edit_dtor(o))

static const struct life_slot edit_slots[] = {{0xcc, -1}, {0xd0, -1}};
static const struct life_class edit_class = {"EditBox", 0xb74, edit_slots, 2, 0x444, 0x9cc, {0xb14, 0x20}};

static void life_sweep_edit(void) {
    if (!game_mem_get || *game_ddraw || !hooks_dc_same) return;
    for (int round = 0; round < 4; round++) {
        uint8_t fill = (uint8_t)(round * 53 + 11);
        life_ctor_check(&stats_edit_ctor, &edit_class, life_orig_edit_ctor, life_ours_edit_ctor, fill);
        life_close_check(&stats_edit_close, &edit_class, life_orig_edit_ctor, life_orig_edit_close, life_ours_edit_close, fill, 4 + round,
                         3 + round, 0);
        life_close_check(&stats_edit_dtor, &edit_class, life_orig_edit_ctor, life_orig_edit_dtor, life_ours_edit_dtor, fill, 4 + round,
                         3 + round, 0);
    }
}

// ---- Hooks -------------------------------------------------------------------------------------------------------------------------

static const struct hook edit_hooks[] = {
    HOOK(0x614e50, edit_ctor, 0x6a, 0xff, 0x68, 0x38, 0x31, 0x66, 0x00),  // push -1; push handler
    HOOK(0x408010, edit_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x06, 0x0d, 0x65, 0x00),
    HOOK(0x614f30, edit_close, 0x56, 0x8b, 0xf1, 0x33, 0xc0, 0x88, 0x86, 0x14, 0x0a, 0x00, 0x00),  // push esi; mov esi, ecx; xor eax, eax; mov [esi+0xa14], al
    HOOK(0x6151e0, edit_set_text, 0x53, 0x55, 0x57, 0x8b, 0xf9, 0x33, 0xdb),
    HOOK(0x615e40, edit_caret_callback, 0x8b, 0x4c, 0x24, 0x04, 0x33, 0xc0),
    HOOK(0x615e60, edit_on_left_down, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x85, 0xc0),
    HOOK(0x615ec0, edit_on_left_up, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x8b, 0xf1, 0x3b, 0xc6),
    HOOK(0x615f10, edit_on_mouse_move, 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0xbc, 0x7a, 0x9b, 0x00),
    HOOK(0x615f50, edit_text_fits, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x616030, edit_text_position, 0x81, 0xec, 0x04, 0x01, 0x00, 0x00, 0x53, 0x8b, 0xd9),
    HOOK(0x615630, edit_on_key_click, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x56, 0x57, 0x85, 0xdb),
    HOOK(0x615840, edit_on_key_down, 0x8b, 0x44, 0x24, 0x04, 0x53, 0x56, 0x83, 0xf8, 0x09, 0x57),
    HOOK(0x615df0, edit_on_dialog_focus, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x8b, 0xf1, 0x85, 0xc0),
    HOOK(0x616140, edit_pass_dialog_focus, 0x8b, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x85, 0xc0),
    HOOK_CALL(0x6161a0, edit_in_focus, 0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0xca, 0xfd, 0xff),  // push esi; mov esi, ecx; call win_is_dialog_focus
    HOOK(0x612040, group_set_text, 0x8b, 0x44, 0x24, 0x08, 0x8b, 0x4c, 0x81, 0x04, 0x85, 0xc9),
    HOOK(0x612060, group_get_text, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x44, 0x81, 0x04, 0x85, 0xc0),
    HOOK(0x612080, group_append_text, 0x8b, 0x44, 0x24, 0x08, 0x8b, 0x4c, 0x81, 0x04, 0x85, 0xc9),
    HOOK(0x6120b0, group_set_text_limits, 0x8b, 0x44, 0x24, 0x04, 0x57, 0x8d, 0x79, 0x54),
    HOOK(0x612150, group_calculate_dimensions, 0x55, 0x56, 0x8b, 0xf1, 0x33, 0xed, 0x8b, 0x0e),
    HOOK(0x612670, group_on_dialog_focus, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x615270, edit_on_redraw, 0x81, 0xec, 0x20, 0x01, 0x00, 0x00, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x612450, group_on_redraw, 0x83, 0xec, 0x2c, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x6126c0, group_pass_dialog_focus, 0x8b, 0x81, 0x74, 0xff, 0xff, 0xff, 0x8b, 0x50, 0x04),
};
struct hook_module edit_module = {"edit", edit_hooks, HOOK_COUNT(edit_hooks), edit_sweep, NULL, -1};
