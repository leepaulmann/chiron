// The game's Menu (menu bar), PullDown (drop-down menu) and Spot (hit rectangles) classes (OpenSMACX's
// and Thinker's names). A Menu is a GraphicWin with up to 15 titles, each with a PullDown of up to 64
// lines; the Spot list in the Menu tells which title the mouse is over. The layouts are in game.h.
//
// Here are the Spot list's functions (all of them that do not use the heap), a PullDown's functions
// that show, hide, grey, check and look up its lines, answer which is selected, work out how wide it
// has to be (compute_length), and read the left button's release; and a Menu's functions that do the
// same to a PullDown by the id of its title, show, hide, grey and enable a title, set the function a
// pick is told to, and size the bar to its window (adjust). Where the original calls through a vtable
// (slot 0x8 closes a PullDown, 0xc sizes a window, 0x50 and 0x54 take the mouse, 0xf8 redraws, 0x170
// lays the titles out) or through the function a screen set, so does this.
//
// Left to the original:
//  - the constructors, destructors, init functions and everything that frees or allocates a line's
//    text (PullDown_add_item, rename_item, the two that clear the lines (UNK1, UNK4), Menu_add_menu,
//    add_menu_item, rename_menu_item, UNK1 and UNK10, Spot_init, Spot_shutdown and its twin Spot_dtor):
//    a check puts the game's heap back wrongly, so none can be checked;
//  - everything that draws (PullDown_on_redraw and pulldown_update, Menu_on_redraw): text and lines
//    need a Font and a device context a sweep cannot build as the game does;
//  - the handlers that take the mouse and the keys (PullDown_show, hide, on_left_down, mouse_move,
//    on_mouse_move, on_key_click; Menu_on_left_down, on_left_up, mouse_move, on_mouse_move,
//    on_key_click, menu_event, hide_menus): they open and close windows and run do_all_tasks_2
//    (0x5fc9f0, the screen's message loop) and the PullDown redraw (0x5f9f60), which no scratch
//    object can stand in for. PullDown_on_left_up is here: it only closes the PullDown (slot 0x8) or
//    redraws it;
//  - Menu_compute (it calls Spot_add and the Font's widths on the Menu's own Spot list; it is next
//    to what it needs but writes the list the Menu's heap block holds) and Menu_set_def_font (it ends
//    in a jump to Win_adjust_menus, which redraws every Menu).
//
// Differences from OpenSMACX and Thinker: neither lays the classes out; the inventory's UNK1 and
// UNK4 of PullDown clear the lines, UNK2 and UNK3 answer 8, UNK5 and UNK6 answer 1; Menu's UNK6 to
// UNK9 hide, show, grey and enable a title (flags 1 and 2), UNK3 and UNK5 pass on PullDown's UNK2 and
// UNK3, UNK11 is PullDown's id_to_index by title and UNK12 gives the id of a line by its place. Quirks
// kept: add_separator leaves the line count raised when it fails (all but "too many"); Menu's
// add_separator answers 0 whatever PullDown's answers; get_selected does not look at whether the
// selection is in range; on_left_up with its first line hidden and the mouse above the list picks
// that line.
//
// Checks. A function that writes only the object, a Spot list, an out-pointer or a few globals is
// checked as it is called, on the memory it writes (a Menu's functions list the PullDown they reach).
// One that calls through a vtable has effects no check can put back (a slot may close a window), so
// the game's calls of it run the original in shadow mode, and the sweep checks it on scratch objects
// whose vtables log the calls. A plant that writes nothing, or a wrong value, shows as a difference.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "menu.h"
#include "shadow.h"
#include "text.h"
#include "win.h"

#define MENU (&menu_module)
extern struct hook_module menu_module;

// ---- Calls through a vtable --------------------------------------------------------------------

// What the classes put at these slots of a vtable (byte offsets).
enum {
    VT_CLOSE = 0x8,        // a PullDown or a Menu: take it down
    VT_RESIZE = 0xc,       // a window: be this size (width, height, client)
    VT_LEFT_DOWN = 0x50,
    VT_LEFT_UP = 0x54,
    VT_REDRAW = 0xf8,
    VT_LAYOUT = 0x170,     // a Menu: work out where its titles lie
};

static void *vslot(void *o, int slot) { return *(void **)((uint8_t *)*(void ***)o + slot); }
static int vcall0(void *o, int slot) { return ((int(THISCALL *)(void *))vslot(o, slot))(o); }
static int vcall2(void *o, int slot, int a, int b) { return ((int(THISCALL *)(void *, int, int))vslot(o, slot))(o, a, b); }
static int vcall3(void *o, int slot, int a, int b, int c) {
    return ((int(THISCALL *)(void *, int, int, int))vslot(o, slot))(o, a, b, c);
}

// ---- Spot --------------------------------------------------------------------------------------

int in_box(int x, int y, int left, int top, int w, int h) {
    if (x < left) return 0;
    if (x >= left + w) return 0;
    if (y < top) return 0;
    return y < top + h;
}
int in_box_2(int x, int y, const int32_t *rect) {
    if (x < rect[0]) return 0;
    if (x >= rect[2]) return 0;
    if (y < rect[1]) return 0;
    return y < rect[3];
}

// Empty the list without freeing its room (the answer is 0).
int MENU_THIS spot_clear(struct game_spot *s) {
    s->cap = 0, s->count = 0, s->rects = NULL;
    return 0;
}
int MENU_THIS spot_ctor(struct game_spot *s) {
    s->cap = 0, s->count = 0, s->rects = NULL;
    return (int)(intptr_t)s;  // (the object, as a 32-bit address)
}

static void set_rect(struct game_spot_rect *r, int x, int y, int w, int h) {
    r->rect[0] = x, r->rect[1] = y, r->rect[2] = x + w, r->rect[3] = y + h;
}
// A new rectangle at the end, answering where it went, or -1 if the room is full.
int MENU_THIS spot_add(struct game_spot *s, int id, int kind, int x, int y, int w, int h) {
    int at = s->count;
    if (at >= s->cap) return -1;
    s->count = at + 1;
    struct game_spot_rect *r = &s->rects[at];
    set_rect(r, x, y, w, h);
    r->id = id, r->kind = kind;
    return at;
}
// Move every rectangle with this id and kind (the answer is the count).
int MENU_THIS spot_replace(struct game_spot *s, int id, int kind, int x, int y, int w, int h) {
    for (int i = 0; i < s->count; i++) {
        struct game_spot_rect *r = &s->rects[i];
        if (r->id == id && r->kind == kind) set_rect(r, x, y, w, h);
    }
    return s->count;
}
// Remove the rectangle at a place, moving the ones after it down.
static void remove_at(struct game_spot *s, int i) {
    for (; i < s->count - 1; i++) s->rects[i] = s->rects[i + 1];
    s->count--;
}
void MENU_THIS spot_kill_pos(struct game_spot *s, int index) {
    if (index < 0 || index >= s->count) return;
    remove_at(s, index);
}
// The last to first walk keeps every place it has still to look at in order.
void MENU_THIS spot_kill_specific(struct game_spot *s, int id, int kind) {
    for (int i = s->count - 1; i >= 0; i--)
        if (s->rects[i].id == id && s->rects[i].kind == kind && i < s->count) remove_at(s, i);
}
void MENU_THIS spot_kill_type(struct game_spot *s, int kind) {
    for (int i = s->count - 1; i >= 0; i--)
        if (s->rects[i].kind == kind && i < s->count) remove_at(s, i);
}
// The last rectangle (the one drawn on top) that holds the point, its place or -1; its id and kind
// (and rectangle) are given if asked for.
static int spot_at(const struct game_spot *s, int x, int y) {
    for (int i = s->count - 1; i >= 0; i--) {
        const int32_t *r = s->rects[i].rect;
        if (x >= r[0] && x < r[2] && y >= r[1] && y < r[3]) return i;
    }
    return -1;
}
int MENU_THIS spot_check(struct game_spot *s, int x, int y, int *id, int *kind) {
    int i = spot_at(s, x, y);
    if (i < 0) return -1;
    if (id) *id = s->rects[i].id;
    if (kind) *kind = s->rects[i].kind;
    return i;
}
int MENU_THIS spot_check_2(struct game_spot *s, int x, int y, int *id, int *kind, int32_t *rect) {
    int i = spot_at(s, x, y);
    if (i < 0) return -1;
    if (id) *id = s->rects[i].id;
    if (kind) *kind = s->rects[i].kind;
    if (rect) memcpy(rect, s->rects[i].rect, 16);
    return i;
}
// The last rectangle with this id and kind.
int MENU_THIS spot_get_rect(struct game_spot *s, int id, int kind, int32_t *rect) {
    for (int i = s->count - 1; i >= 0; i--) {
        if (s->rects[i].id != id || s->rects[i].kind != kind) continue;
        if (rect) memcpy(rect, s->rects[i].rect, 16);
        return i;
    }
    return -1;
}

// ---- PullDown ----------------------------------------------------------------------------------

// Where a line is, from its id: the first free line (id -1) ends the search.
static int find_line(const struct game_pulldown *p, int id) {
    for (int i = 0; i < 64; i++) {
        int x = p->items[i].id;
        if (x == -1) break;
        if (x == id) return i;
    }
    return -1;
}
int MENU_THIS pulldown_id_to_index(struct game_pulldown *p, int id) { return find_line(p, id); }

// A separator takes the next line: the count goes up first and stays up if it fails, except when there
// are too many lines. Answers 1 for too many lines or no free line at all, 3 for an id of -1, 0xc for a
// line already there, else 0.
int MENU_THIS pulldown_add_separator(struct game_pulldown *p, int id) {
    int n = p->count + 1;
    p->count = n;
    if (n > 0x40) {
        p->count = n - 1;
        return 1;
    }
    if (id == -1) return 3;
    int i;
    for (i = 0; i < 64; i++)
        if (p->items[i].id == -1) break;
    if (i == 64) return 1;
    struct game_pd_item *it = &p->items[n - 1];
    if (it->text) return 0xc;
    it->id = id;
    it->flags = 5;
    p->shown++;
    p->dirty = 1;
    return 0;
}
int MENU_THIS pulldown_unk2(struct game_pulldown *p, int a) {
    (void)p, (void)a;
    return 8;
}
int MENU_THIS pulldown_unk3(struct game_pulldown *p, int a, int b, int c) {
    (void)p, (void)a, (void)b, (void)c;
    return 8;
}

// Show or hide a line: the number shown follows its bit. Answers 0xb for an id that isn't there.
int MENU_THIS pulldown_hide_item(struct game_pulldown *p, int id) {
    int i = find_line(p, id);
    if (i < 0) return 0xb;
    struct game_pd_item *it = &p->items[i];
    if (it->flags & 1) {
        it->flags &= ~1u;
        p->shown--;
    }
    p->dirty = 1;
    return 0;
}
int MENU_THIS pulldown_show_item(struct game_pulldown *p, int id) {
    int i = find_line(p, id);
    if (i < 0) return 0xb;
    struct game_pd_item *it = &p->items[i];
    if (!(it->flags & 1)) {
        it->flags |= 1;
        p->shown++;
    }
    p->dirty = 1;
    return 0;
}
// Grey, ungrey, check and uncheck a line (bits 2 and 8).
static int line_bits(struct game_pulldown *p, int id, uint32_t set, uint32_t clear) {
    int i = find_line(p, id);
    if (i < 0) return 0xb;
    struct game_pd_item *it = &p->items[i];
    it->flags = (it->flags | set) & ~clear;
    p->dirty = 1;
    return 0;
}
int MENU_THIS pulldown_disable_item(struct game_pulldown *p, int id) { return line_bits(p, id, 2, 0); }
int MENU_THIS pulldown_enable_item(struct game_pulldown *p, int id) { return line_bits(p, id, 0, 2); }
int MENU_THIS pulldown_check_item(struct game_pulldown *p, int id) { return line_bits(p, id, 8, 0); }
int MENU_THIS pulldown_uncheck_item(struct game_pulldown *p, int id) { return line_bits(p, id, 0, 8); }

// The selected line, or -1 if none or greyed (the selection is not looked at for being in range).
int MENU_THIS pulldown_get_selected(struct game_pulldown *p) {
    int sel = p->sel;
    if (sel == -1) return -1;
    if (((const struct game_pd_item *)((const uint8_t *)p->items + sel * 20))->flags & 2) return -1;
    return sel;
}

// Widen the PullDown for a line's text (or, with none, for every line) in the Font it is drawn in (the
// game's default one if it has none yet, which it then keeps): the text's part before a '|' and the key
// that follows, with the gaps between them (the strings 0x696e20 to 0x696e2c). A width already wider is
// kept; otherwise it becomes this line's, with a different gap (the strings 4 bytes on).
static const char *gap(int k) { return game_pd_gap + 4 * k; }
void MENU_THIS pulldown_compute_length(struct game_pulldown *p, const char *text) {
    struct font *f = *game_pd_font;
    if (!f) {
        f = *game_font_default;
        *game_pd_font = f;
    }
    if (text) {
        const char *bar = strchr(text, '|');
        if (bar) {
            int old = p->width, len = (int)(bar - text);
            const char *tail = bar + 1;
            int w = font_width_2(f, text, len);
            w += font_width(f, gap(0)) * 2;
            w += font_width(f, tail);
            if (old > w) {
                p->width = old;
                return;
            }
            int v = font_width_2(f, text, len);
            v += font_width(f, gap(1)) * 2;
            v += font_width(f, tail);
            p->width = v;
        } else {
            int old = p->width;
            if (old > font_width(f, text)) p->width = old;
            else p->width = font_width(f, text);
        }
        return;
    }
    p->width = 0;
    for (int i = 0; i < 64; i++) {
        struct game_pd_item *it = &p->items[i];
        if (!it->text) continue;
        int cur = p->width;
        if (it->tail) {
            int w = font_width(f, gap(2));
            w = font_width(f, it->text) + w * 2;
            w += font_width(f, it->tail);
            if (cur <= w) {
                int v = font_width(f, gap(3));
                v = font_width(f, it->tail) + v * 2;
                v += font_width(f, it->text);
                cur = v;
            }
            p->width = cur;
        } else {
            if (cur > font_width(f, it->text)) p->width = cur;
            else p->width = font_width(f, it->text);
        }
    }
}

// A right click is handled like a left one, and a right release like a left release (the slots 0x50 and 0x54).
int MENU_THIS pulldown_on_right_down(struct game_pulldown *p, int x, int y) { return vcall2(p, VT_LEFT_DOWN, x, y); }
int MENU_THIS pulldown_on_right_up(struct game_pulldown *p, int x, int y) { return vcall2(p, VT_LEFT_UP, x, y); }
void MENU_THIS pulldown_on_mouse_leave(struct game_pulldown *p, int x, int y) { (void)p, (void)x, (void)y; }
int MENU_THIS pulldown_unk5(struct game_pulldown *p) {
    (void)p;
    return 1;
}
int MENU_THIS pulldown_unk6(struct game_pulldown *p) {
    (void)p;
    return 1;
}

// The height of a line in a Font: its ascent while its first number is negative (no font set), else its
// height plus that number.
static int line_height(const struct font *f) { return f->first < 0 ? f->ascent : f->height + f->first; }

// The left button went up: a release outside the PullDown's box does nothing. Inside, the line is the
// number of lines the mouse is down from the top (less the gap above the first) of the Font's height;
// the selection is turned from a count of the shown lines to a line, and a line that is a separator or
// not there is no selection. With none the PullDown is redrawn (slot 0xf8), else it is closed (slot 0x8).
void MENU_THIS pulldown_on_left_up(struct game_pulldown *p, int x, int y) {
    const int32_t *r = p->buf.full;
    if (!(x >= r[0] && x < r[2] && y >= r[1] && y < r[3])) return;
    p->sel = (y - *game_pd_margin) / line_height(*game_pd_font);
    int count = p->count, shown = -1, i = 0;
    if (count > 0) {
        for (;;) {
            if (p->items[i].flags & 1) shown++;
            if (shown == p->sel) break;
            if (++i >= count) break;
        }
    }
    if (i == count) {
        p->sel = -1;
    } else {
        p->sel = i;
        if (p->items[i].flags & 4) p->sel = -1;
    }
    if (p->sel == -1) vcall0(p, VT_REDRAW);
    else vcall0(p, VT_CLOSE);
}

// ---- Menu --------------------------------------------------------------------------------------

// Where a title is, from its id (the first free one, with id -1, ends the search).
static int find_title(const struct game_menu *m, int id) {
    for (int i = 0; i < 15; i++) {
        int x = m->entries[i].id;
        if (x == -1) break;
        if (x == id) return i;
    }
    return -1;
}
int MENU_THIS menu_id_to_index(struct game_menu *m, int id) { return find_title(m, id); }

// The functions that pass a line's change on to the title's PullDown: 0xb if there is no such title.
int MENU_THIS menu_add_separator(struct game_menu *m, int menu_id, int id) {
    int i = find_title(m, menu_id);
    if (i < 0) return 0xb;
    pulldown_add_separator(m->entries[i].pd, id);
    return 0;
}
int MENU_THIS menu_unk2(struct game_menu *m, int a) {
    (void)m, (void)a;
    return 0;
}
int MENU_THIS menu_unk3(struct game_menu *m, int menu_id, int a) {
    int i = find_title(m, menu_id);
    if (i < 0) return 0xb;
    return pulldown_unk2(m->entries[i].pd, a);
}
int MENU_THIS menu_unk4(struct game_menu *m, int a, int b, int c) {
    (void)m, (void)a, (void)b, (void)c;
    return 0;
}
int MENU_THIS menu_unk5(struct game_menu *m, int menu_id, int a, int b, int c) {
    int i = find_title(m, menu_id);
    if (i < 0) return 0xb;
    return pulldown_unk3(m->entries[i].pd, a, b, c);
}
#define LINE_CALL(name, fn)                                         \
    int MENU_THIS name(struct game_menu *m, int menu_id, int id) {  \
        int i = find_title(m, menu_id);                             \
        if (i < 0) return 0xb;                                      \
        return fn(m->entries[i].pd, id);                            \
    }
LINE_CALL(menu_hide_menu_item, pulldown_hide_item)
LINE_CALL(menu_show_menu_item, pulldown_show_item)
LINE_CALL(menu_disable_menu_item, pulldown_disable_item)
LINE_CALL(menu_enable_menu_item, pulldown_enable_item)
LINE_CALL(menu_check_menu_item, pulldown_check_item)
LINE_CALL(menu_uncheck_menu_item, pulldown_uncheck_item)

// Show, hide, grey or ungrey a title (flags 1 and 2) and redraw the bar.
static int title_bits(struct game_menu *m, int menu_id, unsigned set, unsigned clear) {
    int i = find_title(m, menu_id);
    if (i < 0) return 0xb;
    m->entries[i].flags = (uint8_t)((m->entries[i].flags | set) & ~clear);
    vcall0(m, VT_REDRAW);
    return 0;
}
int MENU_THIS menu_unk6(struct game_menu *m, int menu_id) { return title_bits(m, menu_id, 0, 1); }
int MENU_THIS menu_unk7(struct game_menu *m, int menu_id) { return title_bits(m, menu_id, 1, 0); }
int MENU_THIS menu_unk8(struct game_menu *m, int menu_id) { return title_bits(m, menu_id, 2, 0); }
int MENU_THIS menu_unk9(struct game_menu *m, int menu_id) { return title_bits(m, menu_id, 0, 2); }

void MENU_THIS menu_set_menu_proc(struct game_menu *m, void(__attribute__((cdecl)) * proc)(int)) { m->proc = proc; }

// A line's place in a title's PullDown, from its id (-1 if the title or the line isn't there).
int MENU_THIS menu_unk11(struct game_menu *m, int menu_id, int id) {
    int i = find_title(m, menu_id);
    if (i < 0) return -1;
    return pulldown_id_to_index(m->entries[i].pd, id);
}
// The id of a line from its place (-1 for no such title or a place of 64 or more; a negative place is
// not looked at).
int MENU_THIS menu_unk12(struct game_menu *m, int menu_id, int index) {
    int i = find_title(m, menu_id);
    if (i < 0) return -1;
    if (index >= 0x40) return -1;
    return ((const struct game_pd_item *)((const uint8_t *)m->entries[i].pd->items + index * 20))->id;
}

// The Font the bar is drawn in (the game's default one if it has none yet, which it then keeps) is
// twice its height tall. The bar is as wide as the window it hangs from and sits above its client area,
// with the titles laid out again (slot 0x170).
int MENU_THIS menu_requested_height(struct game_menu *m) {
    (void)m;
    struct font *f = *game_menu_font;
    if (!f) f = *game_font_default;
    return f->height * 2;
}
void MENU_THIS menu_adjust(struct game_menu *m) {
    struct font *f = *game_menu_font;
    if (!f) {
        f = *game_font_default;
        *game_menu_font = f;
        if (!f) return;
    }
    struct game_win *parent = m->win.parent;
    if (parent) {
        vcall3(m, VT_RESIZE, parent->inner[2] - parent->inner[0], f->height * 2, 0);
        f = *game_menu_font;
        parent = m->win.parent;
        win_move(&m->win, parent->inner[0], parent->inner[1] - f->height * 2);
    }
    vcall0(m, VT_LAYOUT);
}
void MENU_THIS menu_on_adjust_pulldown_pos(struct game_menu *m, int a, int b) { (void)m, (void)a, (void)b; }

// ---- Shadow checks -----------------------------------------------------------------------------

#define P(p) ((int)(uintptr_t)(p))

// The stand-ins a sweep gives the vtables (see below): what they leave behind is a count and a hash of
// their calls and arguments, so a different call, argument or order shows as a difference in these
// eight bytes.
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }

// The scratch objects the sweep builds, all in one block so one span covers what a function can change.
enum { NPD = 3, NM = 3, NSP = 3, SPCAP = 6, NPOOL = 12 };
struct scratch {
    struct game_pulldown pd[NPD];
    struct game_menu menu[NM];
    struct game_win parent[2];
    struct game_spot spot[NSP];
    struct game_spot_rect rects[NSP][SPCAP];
    struct font font[2];
    char text[NPOOL][24];
    int32_t out[8];
};
static struct scratch S;
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, &S, sizeof S);
    writes_vlog(w);
}

// A check that can't be run on the game's own objects (see the top): in shadow mode the game's calls run
// the original (and are counted, to start the sweep); the sweep calls check_NAME.
#define SWEEP_ONLY(ret, name, params, args, writes, fmt, ...)             \
    SHADOW_DECLARE_T(ret, name, params)                                   \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__) \
    static ret THISCALL shadow_##name params {                            \
        if (!hooks_nested && !shadow_bypass()) {                          \
            stats_##name.calls++;                                         \
            shadow_tick(MENU);                                            \
        }                                                                 \
        SHADOW_TAIL_##ret(orig_##name args);                              \
    }

// What a PullDown's lines hold: its lines, how many are used and shown, the width, the selection, the
// show count and the flag.
#define PD_STATE(p) WRITES(&(p)->items, offsetof(struct game_pulldown, ff38) - offsetof(struct game_pulldown, items))
static void writes_pd(struct hook_writes *w, struct game_pulldown *p) {
    if (p) hooks_writes_add(w, &p->items, offsetof(struct game_pulldown, ff38) - offsetof(struct game_pulldown, items));
}
static struct game_pulldown *title_pd(struct game_menu *m, int id) {
    int i = find_title(m, id);
    return i < 0 ? NULL : m->entries[i].pd;
}
#define SPOT_WRITES(s) (WRITES((s), sizeof *(s)), writes_spot_rects(&w_, (s)))
static void writes_spot_rects(struct hook_writes *w, struct game_spot *s) {
    if (s->rects && s->cap > 0) hooks_writes_add(w, s->rects, (size_t)s->cap * sizeof(struct game_spot_rect));
}
#define OUT(p) ((p) ? WRITES((p), 4) : (void)0)

SHADOW_FN(MENU, int, in_box, (int a, int b, int c, int d, int e, int f), (a, b, c, d, e, f), "%d, %d, %d, %d, %d, %d", a, b, c, d, e, f)
SHADOW_FN(MENU, int, in_box_2, (int a, int b, const int32_t *r), (a, b, r), "%d, %d, %d", a, b, P(r))
SHADOW_WRITER_T(MENU, int, spot_clear, (struct game_spot * s), (s), (WRITES(s, sizeof *s)), "%d", P(s))
SHADOW_WRITER_T(MENU, int, spot_ctor, (struct game_spot * s), (s), (WRITES(s, sizeof *s)), "%d", P(s))
SHADOW_WRITER_T(MENU, int, spot_add, (struct game_spot * s, int id, int kind, int x, int y, int w, int h), (s, id, kind, x, y, w, h),
                SPOT_WRITES(s), "%d, %d, %d, %d, %d, %d, %d", P(s), id, kind, x, y, w, h)
SHADOW_WRITER_T(MENU, int, spot_replace, (struct game_spot * s, int id, int kind, int x, int y, int w, int h), (s, id, kind, x, y, w, h),
                SPOT_WRITES(s), "%d, %d, %d, %d, %d, %d, %d", P(s), id, kind, x, y, w, h)
SHADOW_WRITER_T(MENU, void, spot_kill_pos, (struct game_spot * s, int index), (s, index), SPOT_WRITES(s), "%d, %d", P(s), index)
SHADOW_WRITER_T(MENU, void, spot_kill_specific, (struct game_spot * s, int id, int kind), (s, id, kind), SPOT_WRITES(s), "%d, %d, %d",
                P(s), id, kind)
SHADOW_WRITER_T(MENU, void, spot_kill_type, (struct game_spot * s, int kind), (s, kind), SPOT_WRITES(s), "%d, %d", P(s), kind)
SHADOW_WRITER_T(MENU, int, spot_check, (struct game_spot * s, int x, int y, int *id, int *kind), (s, x, y, id, kind),
                (OUT(id), OUT(kind)), "%d, %d, %d", P(s), x, y)
SHADOW_WRITER_T(MENU, int, spot_check_2, (struct game_spot * s, int x, int y, int *id, int *kind, int32_t *rect), (s, x, y, id, kind, rect),
                (OUT(id), OUT(kind), rect ? WRITES(rect, 16) : (void)0), "%d, %d, %d", P(s), x, y)
SHADOW_WRITER_T(MENU, int, spot_get_rect, (struct game_spot * s, int id, int kind, int32_t *rect), (s, id, kind, rect),
                (rect ? WRITES(rect, 16) : (void)0), "%d, %d, %d", P(s), id, kind)

SHADOW_WRITER_T(MENU, int, pulldown_add_separator, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_THIS_FN(MENU, int, pulldown_unk2, (struct game_pulldown * p, int a), (p, a), "%d", a)
SHADOW_THIS_FN(MENU, int, pulldown_unk3, (struct game_pulldown * p, int a, int b, int c), (p, a, b, c), "%d, %d, %d", a, b, c)
SHADOW_WRITER_T(MENU, int, pulldown_hide_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_WRITER_T(MENU, int, pulldown_show_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_WRITER_T(MENU, int, pulldown_disable_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_WRITER_T(MENU, int, pulldown_enable_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_WRITER_T(MENU, int, pulldown_check_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_WRITER_T(MENU, int, pulldown_uncheck_item, (struct game_pulldown * p, int id), (p, id), PD_STATE(p), "%d, %d", P(p), id)
SHADOW_THIS_FN(MENU, int, pulldown_id_to_index, (struct game_pulldown * p, int id), (p, id), "%d", id)
SHADOW_THIS_FN(MENU, int, pulldown_get_selected, (struct game_pulldown * p), (p), "%d", P(p))
SHADOW_WRITER_T(MENU, void, pulldown_compute_length, (struct game_pulldown * p, const char *text), (p, text),
                (WRITES(&p->width, 4), WRITES(game_pd_font, 4)), "%d, %s", P(p), text ? text : "(null)")
SHADOW_WRITER_T(MENU, void, pulldown_on_mouse_leave, (struct game_pulldown * p, int x, int y), (p, x, y), WRITES(p, sizeof *p),
                "%d, %d, %d", P(p), x, y)
SHADOW_THIS_FN(MENU, int, pulldown_unk5, (struct game_pulldown * p), (p), "%d", P(p))
SHADOW_THIS_FN(MENU, int, pulldown_unk6, (struct game_pulldown * p), (p), "%d", P(p))
SWEEP_ONLY(int, pulldown_on_right_down, (struct game_pulldown * p, int x, int y), (p, x, y), writes_scratch(&w_), "%d, %d, %d", P(p), x, y)
SWEEP_ONLY(int, pulldown_on_right_up, (struct game_pulldown * p, int x, int y), (p, x, y), writes_scratch(&w_), "%d, %d, %d", P(p), x, y)
SWEEP_ONLY(void, pulldown_on_left_up, (struct game_pulldown * p, int x, int y), (p, x, y), writes_scratch(&w_), "%d, %d, %d", P(p), x, y)

SHADOW_WRITER_T(MENU, int, menu_add_separator, (struct game_menu * m, int menu_id, int id), (m, menu_id, id),
                writes_pd(&w_, title_pd(m, menu_id)), "%d, %d, %d", P(m), menu_id, id)
SHADOW_THIS_FN(MENU, int, menu_unk2, (struct game_menu * m, int a), (m, a), "%d", a)
SHADOW_THIS_FN(MENU, int, menu_unk3, (struct game_menu * m, int menu_id, int a), (m, menu_id, a), "%d, %d", menu_id, a)
SHADOW_THIS_FN(MENU, int, menu_unk4, (struct game_menu * m, int a, int b, int c), (m, a, b, c), "%d, %d, %d", a, b, c)
SHADOW_THIS_FN(MENU, int, menu_unk5, (struct game_menu * m, int menu_id, int a, int b, int c), (m, menu_id, a, b, c), "%d, %d, %d",
               menu_id, a, b)
SWEEP_ONLY(int, menu_unk6, (struct game_menu * m, int menu_id), (m, menu_id), writes_scratch(&w_), "%d, %d", P(m), menu_id)
SWEEP_ONLY(int, menu_unk7, (struct game_menu * m, int menu_id), (m, menu_id), writes_scratch(&w_), "%d, %d", P(m), menu_id)
SWEEP_ONLY(int, menu_unk8, (struct game_menu * m, int menu_id), (m, menu_id), writes_scratch(&w_), "%d, %d", P(m), menu_id)
SWEEP_ONLY(int, menu_unk9, (struct game_menu * m, int menu_id), (m, menu_id), writes_scratch(&w_), "%d, %d", P(m), menu_id)
#define MENU_LINE_CHECK(name)                                                                                  \
    SHADOW_WRITER_T(MENU, int, name, (struct game_menu * m, int menu_id, int id), (m, menu_id, id),            \
                    writes_pd(&w_, title_pd(m, menu_id)), "%d, %d, %d", P(m), menu_id, id)
MENU_LINE_CHECK(menu_hide_menu_item)
MENU_LINE_CHECK(menu_show_menu_item)
MENU_LINE_CHECK(menu_disable_menu_item)
MENU_LINE_CHECK(menu_enable_menu_item)
MENU_LINE_CHECK(menu_check_menu_item)
MENU_LINE_CHECK(menu_uncheck_menu_item)
SHADOW_WRITER_T(MENU, void, menu_set_menu_proc, (struct game_menu * m, void(__attribute__((cdecl)) * proc)(int)), (m, proc),
                WRITES(&m->proc, 4), "%d, %d", P(m), P(proc))
SHADOW_THIS_FN(MENU, int, menu_unk11, (struct game_menu * m, int menu_id, int id), (m, menu_id, id), "%d, %d", menu_id, id)
SHADOW_THIS_FN(MENU, int, menu_unk12, (struct game_menu * m, int menu_id, int index), (m, menu_id, index), "%d, %d", menu_id, index)
SHADOW_THIS_FN(MENU, int, menu_id_to_index, (struct game_menu * m, int id), (m, id), "%d", id)
SHADOW_THIS_FN(MENU, int, menu_requested_height, (struct game_menu * m), (m), "%d", P(m))
SHADOW_WRITER_T(MENU, void, menu_on_adjust_pulldown_pos, (struct game_menu * m, int a, int b), (m, a, b), WRITES(m, sizeof *m),
                "%d, %d, %d", P(m), a, b)
SWEEP_ONLY(void, menu_adjust, (struct game_menu * m), (m), (writes_scratch(&w_), WRITES(game_menu_font, 4)), "%d", P(m))

// ---- Sweep -------------------------------------------------------------------------------------
//
// Scratch lists, PullDowns and Menus in static memory (S), with a random state each round, and a
// stand-in vtable for the objects whose slots only log the call (see vlog) and answer a number of
// their own, so what a function calls, and with what, is compared.

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
#define STUB2(slot) \
    static int THISCALL stub_##slot(void *o, int a, int b) { return vrec(slot, o, a, b, 0), slot + 1; }
#define STUB3(slot) \
    static int THISCALL stub_##slot(void *o, int a, int b, int c) { return vrec(slot, o, a, b, c), slot + 1; }
STUB0(0x8) STUB3(0xc) STUB2(0x50) STUB2(0x54) STUB0(0xf8) STUB0(0x170)
static void *stub_vt[0x180 / 4];
static void fill_stub_vt(void) {
    stub_vt[0x8 / 4] = (void *)stub_0x8, stub_vt[0xc / 4] = (void *)stub_0xc, stub_vt[0x50 / 4] = (void *)stub_0x50;
    stub_vt[0x54 / 4] = (void *)stub_0x54, stub_vt[0xf8 / 4] = (void *)stub_0xf8, stub_vt[0x170 / 4] = (void *)stub_0x170;
}

static const char *const words[NPOOL] = {"&Save|Ctrl+S", "Open", "E&xit|Alt+F4", "&&Both|F1", "|Bare", "Wide wide text|X", "a", "",
                                         "Un&do|Ctrl+Z", "Long menu text here", "&Z|", "Tab|Shift+Ctrl+Alt+Del"};

static void build(void) {
    memset(&S, 0, sizeof S);
    for (int i = 0; i < NPOOL; i++) strcpy(S.text[i], words[i]);
    for (int k = 0; k < 2; k++) {
        S.font[k].first = rnd_to(3) - 1, S.font[k].ascent = 4 + rnd_to(9), S.font[k].height = 5 + rnd_to(10);
        S.parent[k].vtable = stub_vt;
        for (int i = 0; i < 4; i++) S.parent[k].inner[i] = rnd_to(300) - 20, S.parent[k].outer[i] = rnd_to(300) - 20;
    }
    for (int k = 0; k < NPD; k++) {
        struct game_pulldown *p = &S.pd[k];
        p->win.vtable = stub_vt;
        int used = rnd_to(9);
        for (int i = 0; i < 64; i++) {
            struct game_pd_item *it = &p->items[i];
            it->id = i < used ? (rnd_to(8) == 0 ? -1 : rnd_to(14)) : -1;
            if (rnd_to(30) == 0) it->id = rnd_to(14);  // (a line after a free one)
            it->flags = (uint32_t)rnd_to(16) | (rnd_to(3) ? 0 : 0x10);
            it->text = rnd_to(3) ? S.text[rnd_to(NPOOL)] : NULL;
            it->tail = rnd_to(2) ? S.text[rnd_to(NPOOL)] : NULL;
            it->hotkey = rnd_to(2) ? it->text : NULL;
        }
        p->count = rnd_to(5) ? rnd_to(10) : 55 + rnd_to(12);
        p->width = rnd_to(150);
        p->sel = rnd_to(11) - 1;
        p->shown = rnd_to(10), p->dirty = (uint8_t)rnd_to(2);
        int w = 20 + rnd_to(60), h = 20 + rnd_to(60);
        p->buf.full[0] = rnd_to(5) - 2, p->buf.full[1] = rnd_to(5) - 2, p->buf.full[2] = w, p->buf.full[3] = h;
    }
    for (int k = 0; k < NSP; k++) {
        struct game_spot *s = &S.spot[k];
        s->rects = S.rects[k], s->cap = rnd_to(8) ? SPCAP : rnd_to(SPCAP + 1);
        s->count = rnd_to(s->cap + 1);
        for (int i = 0; i < SPCAP; i++) {
            struct game_spot_rect *r = &S.rects[k][i];
            r->rect[0] = rnd_to(40), r->rect[1] = rnd_to(40), r->rect[2] = r->rect[0] + rnd_to(30), r->rect[3] = r->rect[1] + rnd_to(30);
            r->kind = rnd_to(3), r->id = rnd_to(4);
        }
    }
    for (int k = 0; k < NM; k++) {
        struct game_menu *m = &S.menu[k];
        m->win.vtable = stub_vt;
        m->win.parent = rnd_to(6) ? &S.parent[rnd_to(2)] : NULL;
        int used = rnd_to(6);
        m->count = used, m->open = rnd_to(4) - 1, m->flags = (uint32_t)rnd_to(8);
        for (int i = 0; i < 15; i++) {
            struct game_menu_entry *e = &m->entries[i];
            e->id = i < used ? (rnd_to(10) == 0 ? -1 : 100 + rnd_to(6)) : -1;
            if (rnd_to(40) == 0) e->id = 100 + rnd_to(6);
            e->flags = (uint8_t)rnd_to(8), e->text = S.text[rnd_to(NPOOL)];
            e->pd = &S.pd[rnd_to(NPD)];
        }
    }
}

// An id a PullDown has, or one it hasn't.
static int pd_id(struct game_pulldown *p) { return rnd_to(5) ? p->items[rnd_to(10)].id : rnd_to(18) - 2; }
static int menu_pick(struct game_menu *m) { return rnd_to(5) ? m->entries[rnd_to(7)].id : 98 + rnd_to(10); }

static void menu_sweep(void) {
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x6e1d2b97u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    fill_stub_vt();
    void *saved_pd_font = *game_pd_font, *saved_menu_font = *game_menu_font;
    int32_t saved_margin = *game_pd_margin;
    for (int round = 0; round < 30; round++) {
        build();
        // The in_box functions.
        for (int j = 0; j < 12; j++) {
            int x = rnd_to(30) - 5, y = rnd_to(30) - 5, l = rnd_to(12) - 3, t = rnd_to(12) - 3, w = rnd_to(20) - 2, h = rnd_to(20) - 2;
            check_in_box(x, y, l, t, w, h);
            check_in_box(l, t, l, t, w, h), check_in_box(l + w, t + h, l, t, w, h), check_in_box(l + w - 1, t + h - 1, l, t, w, h);
            int32_t r[4] = {l, t, l + w, t + h};
            check_in_box_2(x, y, r);
            check_in_box_2(l, t, r), check_in_box_2(l + w, t + h, r), check_in_box_2(l + w - 1, t + h - 1, r);
        }
        for (int k = 0; k < NSP; k++) {
            struct game_spot *s = &S.spot[k];
            for (int j = 0; j < 6; j++) {
                check_spot_add(s, rnd_to(4), rnd_to(3), rnd_to(40), rnd_to(40), rnd_to(30), rnd_to(30));
                check_spot_replace(s, rnd_to(4), rnd_to(3), rnd_to(40), rnd_to(40), rnd_to(30), rnd_to(30));
                check_spot_kill_pos(s, rnd_to(s->cap + 3) - 1);
                check_spot_kill_specific(s, rnd_to(4), rnd_to(3));
                check_spot_kill_type(s, rnd_to(3));
                int id = -99, kind = -99, id2 = -99, kind2 = -99;
                int32_t rect[4] = {-1, -1, -1, -1}, rect2[4] = {-1, -1, -1, -1};
                int x = rnd_to(50) - 5, y = rnd_to(50) - 5;
                check_spot_check(s, x, y, &id, &kind);
                check_spot_check(s, x, y, rnd_to(2) ? &id : NULL, rnd_to(2) ? &kind : NULL);
                check_spot_check_2(s, x, y, &id2, &kind2, rect);
                check_spot_check_2(s, x, y, rnd_to(2) ? &id2 : NULL, rnd_to(2) ? &kind2 : NULL, rnd_to(2) ? rect2 : NULL);
                check_spot_get_rect(s, rnd_to(4), rnd_to(3), rect);
                check_spot_get_rect(s, rnd_to(4), rnd_to(3), rnd_to(2) ? rect2 : NULL);
                if (s->count > 0) {  // (a point inside a rectangle that is there)
                    const int32_t *r = s->rects[rnd_to(s->count)].rect;
                    check_spot_check(s, r[0], r[1], &id, &kind);
                    check_spot_check_2(s, r[2] - 1, r[3] - 1, &id2, &kind2, rect);
                    check_spot_get_rect(s, s->rects[rnd_to(s->count)].id, s->rects[rnd_to(s->count)].kind, rect);
                }
                struct game_spot copy = *s;
                check_spot_clear(&copy);
                check_spot_ctor(&copy);
            }
        }
        // The PullDowns.
        void *font = *game_font_default;
        for (int k = 0; k < NPD; k++) {
            struct game_pulldown *p = &S.pd[k];
            for (int j = 0; j < 6; j++) {
                int saved_count = p->count;
                p->count = rnd_to(5) ? saved_count : 63 + rnd_to(4);
                check_pulldown_add_separator(p, pd_id(p));
                check_pulldown_add_separator(p, -1);
                p->count = saved_count;
                check_pulldown_hide_item(p, pd_id(p));
                check_pulldown_show_item(p, pd_id(p));
                check_pulldown_disable_item(p, pd_id(p));
                check_pulldown_enable_item(p, pd_id(p));
                check_pulldown_check_item(p, pd_id(p));
                check_pulldown_uncheck_item(p, pd_id(p));
                check_pulldown_id_to_index(p, pd_id(p));
                p->sel = rnd_to(5) ? rnd_to(12) - 1 : -1;
                check_pulldown_get_selected(p);
                check_pulldown_unk2(p, rnd_to(9)), check_pulldown_unk3(p, rnd_to(9), rnd_to(9), rnd_to(9));
                check_pulldown_unk5(p), check_pulldown_unk6(p);
                check_pulldown_on_mouse_leave(p, rnd_to(9), rnd_to(9));
                check_pulldown_on_right_down(p, rnd_to(70), rnd_to(70));
                check_pulldown_on_right_up(p, rnd_to(70), rnd_to(70));
                // The release: the font and the gap above the first line vary, and so does the point.
                *game_pd_font = &S.font[rnd_to(2)];
                *game_pd_margin = rnd_to(8) - 1;
                int x = rnd_to(90) - 10, y = rnd_to(90) - 10;
                check_pulldown_on_left_up(p, x, y);
                if (p->buf.full[2] > 3) check_pulldown_on_left_up(p, p->buf.full[0] + rnd_to(p->buf.full[2] - p->buf.full[0]), *game_pd_margin + rnd_to(40));
                // Its width, with the game's own font (a game has one).
                if (font) {
                    *game_pd_font = rnd_to(2) ? font : NULL;
                    p->width = rnd_to(150);
                    check_pulldown_compute_length(p, NULL);
                    for (int t = 0; t < 4; t++) {
                        p->width = rnd_to(rnd_to(2) ? 150 : 4);
                        *game_pd_font = rnd_to(2) ? font : NULL;
                        check_pulldown_compute_length(p, rnd_to(7) ? S.text[rnd_to(NPOOL)] : NULL);
                    }
                }
            }
        }
        *game_pd_font = saved_pd_font;
        // The Menus.
        for (int k = 0; k < NM; k++) {
            struct game_menu *m = &S.menu[k];
            for (int j = 0; j < 6; j++) {
                check_menu_add_separator(m, menu_pick(m), rnd_to(5) ? rnd_to(14) : -1);
                check_menu_unk2(m, rnd_to(9));
                check_menu_unk3(m, menu_pick(m), rnd_to(9));
                check_menu_unk4(m, rnd_to(9), rnd_to(9), rnd_to(9));
                check_menu_unk5(m, menu_pick(m), rnd_to(9), rnd_to(9), rnd_to(9));
                check_menu_unk6(m, menu_pick(m)), check_menu_unk7(m, menu_pick(m));
                check_menu_unk8(m, menu_pick(m)), check_menu_unk9(m, menu_pick(m));
                int mid = menu_pick(m);
                struct game_pulldown *p = title_pd(m, mid);
                int id = p ? pd_id(p) : rnd_to(10);
                check_menu_hide_menu_item(m, mid, id), check_menu_show_menu_item(m, mid, id);
                check_menu_disable_menu_item(m, mid, id), check_menu_enable_menu_item(m, mid, id);
                check_menu_check_menu_item(m, mid, id), check_menu_uncheck_menu_item(m, mid, id);
                check_menu_set_menu_proc(m, rnd_to(2) ? (void *)check_menu_unk2 : NULL);
                check_menu_unk11(m, mid, id);
                check_menu_unk12(m, mid, rnd_to(12) - 2);
                check_menu_unk12(m, mid, 62 + rnd_to(4));
                check_menu_id_to_index(m, menu_pick(m));
                check_menu_on_adjust_pulldown_pos(m, rnd_to(9), rnd_to(9));
                *game_menu_font = rnd_to(4) ? &S.font[rnd_to(2)] : NULL;
                if (*game_menu_font || *game_font_default) {
                    check_menu_requested_height(m);
                    check_menu_adjust(m);
                }
            }
        }
    }
    *game_pd_font = saved_pd_font, *game_menu_font = saved_menu_font, *game_pd_margin = saved_margin;
    hooks_sweeping = sweeping;
}

// ---- Hooks -------------------------------------------------------------------------------------

static const struct hook menu_hooks[] = {
    HOOK(0x5fa7a0, in_box, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x5fa7e0, in_box_2, 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x4c, 0x24, 0x04),
    HOOK(0x5fa820, spot_clear, 0x33, 0xc0, 0x89, 0x41, 0x04),
    HOOK(0x5fa860, spot_ctor, 0x8b, 0xc1, 0x33, 0xc9, 0x89, 0x48, 0x04),
    HOOK(0x5fa900, spot_replace, 0x8b, 0x41, 0x08, 0x53, 0x33, 0xdb),
    HOOK(0x5fa960, spot_add, 0x8b, 0x41, 0x08, 0x8b, 0x51, 0x04),
    HOOK(0x5fa9c0, spot_kill_pos, 0x53, 0x8b, 0x5c, 0x24, 0x08),
    HOOK(0x5faa10, spot_kill_specific, 0x51, 0x8b, 0xd1, 0x55, 0x8b, 0x6a, 0x08),
    HOOK(0x5faa90, spot_kill_type, 0x51, 0x8b, 0xd1, 0x55, 0x8b, 0x6a, 0x08),
    HOOK(0x5fab00, spot_check, 0x8b, 0x41, 0x08, 0x53, 0x56),
    HOOK(0x5fab70, spot_check_2, 0x8b, 0x41, 0x08, 0x53, 0x56),
    HOOK(0x5fac00, spot_get_rect, 0x8b, 0x41, 0x08, 0x56, 0x48),
    HOOK(0x5f8be0, pulldown_add_separator, 0x8b, 0x91, 0x20, 0x0f, 0x00, 0x00),
    HOOK(0x5f8c90, pulldown_unk2, 0xb8, 0x08, 0x00, 0x00, 0x00),
    HOOK(0x5f8ca0, pulldown_unk3, 0xb8, 0x08, 0x00, 0x00, 0x00),
    HOOK(0x5f8cb0, pulldown_hide_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f8d20, pulldown_show_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f8d90, pulldown_disable_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f8df0, pulldown_enable_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f9040, pulldown_check_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f90a0, pulldown_uncheck_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5f9d00, pulldown_id_to_index, 0x56, 0x8b, 0x74, 0x24, 0x08),
    HOOK(0x5f9f40, pulldown_get_selected, 0x8b, 0x81, 0x28, 0x0f, 0x00, 0x00),
    HOOK(0x5f9d30, pulldown_compute_length, 0x51, 0xa1, 0x68, 0x7b, 0x9b, 0x00),
    HOOK(0x5f9460, pulldown_on_right_down, 0x8b, 0x54, 0x24, 0x08, 0x8b, 0x01),
    HOOK(0x5f9570, pulldown_on_right_up, 0x8b, 0x54, 0x24, 0x08, 0x8b, 0x01),
    HOOK(0x5f9480, pulldown_on_left_up, 0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1),
    HOOK(0x5fa740, pulldown_on_mouse_leave, 0xc2, 0x08, 0x00, 0x90, 0x90),
    HOOK(0x5fa750, pulldown_unk5, 0xb8, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x5fa760, pulldown_unk6, 0xb8, 0x01, 0x00, 0x00, 0x00),
    HOOK(0x5fb160, menu_add_separator, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb1c0, menu_unk2, 0x33, 0xc0, 0xc2, 0x04, 0x00),
    HOOK(0x5fb1d0, menu_unk3, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb230, menu_unk4, 0x33, 0xc0, 0xc2, 0x0c, 0x00),
    HOOK(0x5fb240, menu_unk5, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb2a0, menu_unk6, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb360, menu_unk7, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb420, menu_unk8, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb4e0, menu_unk9, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb300, menu_hide_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb3c0, menu_show_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb480, menu_disable_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb540, menu_enable_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb760, menu_check_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb7c0, menu_uncheck_menu_item, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb820, menu_set_menu_proc, 0x8b, 0x44, 0x24, 0x04, 0x89, 0x81, 0x14, 0x0a, 0x00, 0x00),
    HOOK(0x5fb8e0, menu_unk11, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb930, menu_unk12, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5fb990, menu_id_to_index, 0x56, 0x8b, 0x74, 0x24, 0x08),
    HOOK(0x5fc5a0, menu_adjust, 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x94, 0x7b, 0x9b, 0x00),
    HOOK(0x5fc6a0, menu_requested_height, 0xa1, 0x94, 0x7b, 0x9b, 0x00),
    HOOK(0x5fc6c0, menu_on_adjust_pulldown_pos, 0xc2, 0x08, 0x00, 0x90, 0x90),
};

struct hook_module menu_module = {"menu", menu_hooks, HOOK_COUNT(menu_hooks), menu_sweep, NULL, -1};
