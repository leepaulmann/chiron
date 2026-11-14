// The game's Win class (OpenSMACX's and Thinker's Win), the base of every window and widget:
// the geometry and state of a window, its place in the tree of windows, and the lists the game
// keeps of them. The layout is in game.h (struct game_win).
//
// These are the Win functions that make no Windows call and don't use the game's heap: they
// read and write only the Win objects and a few globals. The ones that draw, show or hide
// (InvalidateRect, ShowWindow), read the mouse or keys, or make or free objects are not here.
// Where a function calls through the vtable (the nonclient functions ask the window `nc_owner`
// for a height) it calls through the slot exactly as the original does, as a subclass may
// override it.
//
// Differences from OpenSMACX and Thinker: none of them has this part of the class laid out; the
// names are the inventory's (the original's own symbols are lost), with Win_UNK2, UNK3 and
// UNK4 given names that say what they do. Win_UNK1, UNK5 and the no-op event handlers are left
// as they are: they only return.
//
// Checks. Every function here writes only the Win objects it is given (and their children or
// parents), the point or rectangle it is given, or the globals game.h names, so each is checked
// with the memory it can reach saved and compared (a pure question is compared by its result).
// The sweep builds scratch Wins in static memory (not on the game's heap), joined into a forest
// at random with random flags, sizes and rectangles, and a stand-in vtable whose NC_HEIGHT slot
// returns a number worth adding, then runs every function over them and over the game's own
// windows (the z-order and parent lists' entries) with read-only queries.
//
// Drawing. The functions that paint a window into Buffers are here too: which window is under
// the mouse (win_get_mouse_window, with its hit test on the frame strips' pixels), the screen's
// redraw into the back buffer (win_update_screen and friends, which call the virtual functions of
// the game's own windows), and the nonclient area (win_set_rects, win_on_redraw_nc,
// win_draw_nc_to, win_on_nc_paint). The last two groups use Buffer set-up and clip regions, which
// make and free Windows objects, so in shadow mode the game's calls of them run the original and
// the sweep checks Chiron's code (the screen's redraw over stand-in windows with the back buffer's
// pixels put back; the frame on twin windows, compared Buffer by Buffer). The mouse's window only
// writes a few globals and is checked in the game as well. Differences from OpenSMACX: it has none
// of these; the names are the inventory's, and the Buffer copies go the way the original has
// them (out of the Buffer called on, into the other).
//
// Input. How mouse and key events reach a window is in the section "Mouse and key events": the
// one-event functions (left down, left up, right click, down and up; key click, down and up), the raw
// events a window is given (mouse move and wheel, the buttons, a system key), and the game's own
// calls that find the window under the mouse or the one with the keys (OnKey, OnChar, OnSysKey and
// the four button ones). They call the virtual functions, callbacks and hooks of the game's own
// windows, which draw and open windows, so in shadow mode the game's calls run the original and the
// sweep checks Chiron's code over scratch windows (with logging stand-ins for all of them and
// windows with a list of regions behind them). A system key and the keys asked of MapVirtualKey go
// through the Windows-call journal. Left with the original: the window procedure (WinProc) and the
// mouse functions it calls that draw the cursor, move or size a window by hand and flip the screen
// (OnMouseMove, update_cursor, do_tracking), the key functions that drain the message queue or run
// the cheat code (Win_on_key, Win_on_char), the palette and activation handlers, and the dialog
// list's set-up and removal (they allocate); the scroll wheel handlers that call the Scroll class.
//
// Left with the original: the large frame-overlay function at 0x5f3b80, do_caption_buttons (frees the
// caption buttons) and GraphicWin's compute_min_size, which Win_init and GraphicWin_init call.
//
// The path to the screen. A window's picture reaches the window in three steps: the window draws itself
// into the back buffer (win_update_screen, above, and the GraphicWin's paint in the section below, which
// puts the window's own Buffer into it, or into another Buffer a draw is redirected to), the
// rectangle is taken to the screen (win_client_to_screen), and the back buffer is flipped to the real
// window's DC (win_flip, in screen.c, with the counted DC and palette functions it uses). The
// GraphicWin updates here make the second and third steps; win_update_back_to_window and
// win_update_window_to_buffer redirect the first into a Buffer (the screen behind a window, to put
// back when it goes; a window as it looks). Their game calls run the original (the paint holds a
// clip region, a handle a check can't put back; the redirects draw the game's own windows or make
// a DIB) and the sweep checks Chiron's code: the paint on the back buffer, with the Buffers' scratch
// compared, and on twin Buffers; the window to a Buffer on scratch windows, with the Buffer and the
// globals put back; the screen behind a window on twin Buffers.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "drawcheck.h"
#include "sprite.h"
#include "bufdraw.h"
#include "bufimg.h"
#include "bufobj.h"
#include "game.h"
#include "hooks.h"
#include "life.h"
#include "screen.h"
#include "shadow.h"
#include "text.h"
#include "win.h"

#define WIN (&win_module)
extern struct hook_module win_module;

// ---- The tree ------------------------------------------------------------------------------

// Whether a window and every window above it is shown.
int WIN_THIS win_is_visible(struct game_win *w) {
    if (!(w->flags2 & WIN_F2_VISIBLE)) return 0;
    if (w->parent && !win_is_visible(w->parent)) return 0;
    return 1;
}

// Whether `other` is one of the window's children, or theirs, at any depth.
int WIN_THIS win_is_descendant(struct game_win *w, struct game_win *other) {
    if (!other) return 0;
    for (int i = 0; i < w->n_children; i++)
        if (w->children[i] == other || win_is_descendant(w->children[i], other)) return 1;
    return 0;
}

// Whether the window is `root`'s child or a child of one of theirs: the same question as
// win_is_descendant, asked the other way round.
int WIN_THIS win_is_below(struct game_win *w, struct game_win *root) {
    if (!root) return 0;
    for (int i = 0; i < root->n_children; i++)
        if (root->children[i] == w || win_is_below(w, root->children[i])) return 1;
    return 0;
}

// Whether `child` is one of the window's own children.
int WIN_THIS win_has_child(struct game_win *w, struct game_win *child) {
    if (!child) return 0;
    for (int i = 0; i < w->n_children; i++)
        if (w->children[i] == child) return 1;
    return 0;
}

// Whether the window may take input: no modal window is up, or it is the modal window or below it.
int WIN_THIS win_in_modal(struct game_win *w) {
    struct game_win *modal = *game_win_modal;
    return !modal || modal == w || win_is_below(w, modal);
}

// Whether the window is the focus of its parent's dialog list (or is a dialog itself).
int WIN_THIS win_is_dialog_focus(struct game_win *w) {
    if (w->flags & WIN_F_DIALOG) return 1;
    struct game_win *p = w->parent;
    if (!p) return 0;
    return (p->dlg_head ? p->dlg_cur->win : NULL) == w;
}

void win_set_def_focus(struct game_win *w) { *game_win_def_focus = w; }

// The window that gets the keys: the modal window's focus, or its own if it has none; with no
// modal window, the focus window, or the default one. That window's `root`, if it and what it
// hangs from are shown; otherwise none. Keeps the window it started from in game_win_key.
struct game_win *win_get_key_window(void) {
    struct game_win *w = *game_win_modal;
    if (w) {
        if (*game_win_modal_focus) w = *game_win_modal_focus;
    } else {
        w = *game_win_focus;
        if (!w) w = *game_win_def_focus;
    }
    *game_win_key = w;
    if (!w) return NULL;
    struct game_win *k = w->root;
    if (!k) return NULL;
    if (!(k->flags2 & WIN_F2_VISIBLE)) return NULL;
    if (k->parent && !win_is_visible(k->parent)) return NULL;
    return k;
}

// Add a top-level window to the parent list: at the back if it keeps its place (ALWAYS_TOP),
// otherwise in front of the rest.
void win_add_parent(struct game_win *w) {
    if (!w) return;
    int n = *game_win_parent_n;
    if (w->flags & WIN_F_ALWAYS_TOP) {
        game_win_parents[n] = w;
        *game_win_parent_n = n + 1;
        return;
    }
    for (int i = n; i > 0; i--) game_win_parents[i] = game_win_parents[i - 1];
    game_win_parents[0] = w;
    *game_win_parent_n = n + 1;
}

// The same for a child of the window.
void WIN_THIS win_add_child(struct game_win *w, struct game_win *child) {
    if (!child) return;
    if (child->flags & WIN_F_ALWAYS_TOP) {
        w->children[w->n_children] = child;
        w->n_children++;
        return;
    }
    for (int i = w->n_children; i > 0; i--) w->children[i] = w->children[i - 1];
    w->children[0] = child;
    w->n_children++;
}

// Take a window off the modal stack. The stack is popped first: if the window is the top one,
// the next takes over as the modal window (or none does). Otherwise it is looked for, and
// closed over if found; if it isn't there the stack is as it was.
void WIN_THIS win_release_modal(struct game_win *w) {
    int n = *game_win_modal_n;
    if (!n) return;
    n--;
    *game_win_modal_n = n;
    if (game_win_modals[n] == w) {
        game_win_modals[n] = NULL;
        game_win_modal_aux[n] = 0;
        if (n > 0) {
            *game_win_modal_prev = game_win_modal_aux[n - 1];
            *game_win_modal = game_win_modals[n - 1];
        } else {
            *game_win_modal = NULL;
        }
        return;
    }
    int i = 0;
    while (i < n && game_win_modals[i] != w) i++;
    if (i == n) {
        *game_win_modal_n = n + 1;
        return;
    }
    for (; i < n; i++) {
        game_win_modals[i] = game_win_modals[i + 1];
        game_win_modal_aux[i] = game_win_modal_aux[i + 1];
    }
}

// ---- The z-order list ---------------------------------------------------------------------

// Add a window to the z-order list, back to front, and first the windows hanging from it: the
// children that aren't positioned relative to it (WIN_F_CHILD clear) in the order they are
// held, then, if it is its own root, the ones that are. Stops going through the children
// when the opaque window turns up, and starts the list over there (the windows drawn before
// it are hidden); both rounds look for it. A window is only added if it is shown.
void win_recurse_zorder(struct game_win *w) {
    int found = 0;
    struct game_win *opaque = *game_win_opaque;
    for (int i = 0; i < w->n_children; i++) {
        struct game_win *c = w->children[i];
        if (c->flags & WIN_F_CHILD) continue;
        if (opaque && opaque == c) {
            *game_win_z_n = 0;
            found = 1;
        }
        c = w->children[i];
        if (c->flags2 & WIN_F2_VISIBLE) {
            if (!c->parent || win_is_visible(c->parent)) win_recurse_zorder(c);
            opaque = *game_win_opaque;
        }
        if (opaque && found) break;
    }
    if (!opaque || !found) {
        for (int i = 0; i < w->n_children; i++) {
            if (opaque && opaque == w->children[i]) {
                *game_win_z_n = 0;
                found = 1;
            }
            if (w->root == w) {
                struct game_win *c = w->children[i];
                if ((c->flags & WIN_F_CHILD) && (c->flags2 & WIN_F2_VISIBLE)) {
                    if (!c->parent || win_is_visible(c->parent)) win_recurse_zorder(c);
                    opaque = *game_win_opaque;
                }
            }
            if (opaque && found) break;
        }
    }
    if ((w->flags2 & WIN_F2_VISIBLE) && (!w->parent || win_is_visible(w->parent))) {
        game_win_zorder[*game_win_z_n] = w;
        (*game_win_z_n)++;
    }
}

// Make the z-order list again from the parent list: the windows in front of it are drawn last.
// Where the opaque window itself is a top-level window, the list starts over there and the
// flag is cleared.
void win_update_zorder(void) {
    *game_win_z_n = 0;
    if (*game_win_parent_n <= 0) return;
    struct game_win *opaque = *game_win_opaque;
    for (int i = 0; i < *game_win_parent_n; i++) {
        if (opaque && opaque == game_win_parents[i]) {
            *game_win_z_n = 0;
            *game_win_opaque_flag = 0;
        }
        struct game_win *w = game_win_parents[i];
        if (w->flags2 & WIN_F2_VISIBLE) {
            win_recurse_zorder(w);
            opaque = *game_win_opaque;
        }
    }
}

// Move a window to the front of a list of n windows, if it is there.
static void to_front(struct game_win **list, int n, struct game_win *w) {
    int i = 0;
    while (i < n && list[i] != w) i++;
    if (i >= n) return;
    for (; i > 0; i--) list[i] = list[i - 1];
    list[0] = w;
}

// Bring a window to the front among its siblings (the top-level windows if it has no
// parent), and make the z-order list again. A window that keeps its place isn't moved.
void WIN_THIS win_bring_to_top(struct game_win *w) {
    struct game_win *p = w->parent;
    if (!w || (w->flags & WIN_F_ALWAYS_TOP)) return;
    if (p) to_front(p->children, p->n_children, w);
    else to_front(game_win_parents, *game_win_parent_n, w);
    win_update_zorder();
}

// Bring a top-level window to the front.
void win_bring_parent_to_top(struct game_win *w) {
    if (!w || (w->flags & WIN_F_ALWAYS_TOP)) return;
    to_front(game_win_parents, *game_win_parent_n, w);
    win_update_zorder();
}

// Bring one of the window's children to the front of its children.
void WIN_THIS win_bring_child_to_top(struct game_win *w, struct game_win *child) {
    if (!child || (child->flags & WIN_F_ALWAYS_TOP)) return;
    to_front(w->children, w->n_children, child);
    win_update_zorder();
}

// ---- Coordinates ---------------------------------------------------------------------------
//
// A window's position in its parent (or the screen) is its outer and inner rectangles' left and
// top added; one with WIN_F_CHILD is counted from its parent's, and one with WIN_F_PARENT_INNER
// from the inside of its parent.

// Take a point from the parent's client space to the screen's, or back.
static void parent_to_screen(struct game_win *w, int32_t *x, int32_t *y) {
    if ((w->flags & WIN_F_CHILD) && w->parent) {
        win_client_to_screen_2(w->parent, x, y);
        if (w->flags & WIN_F_PARENT_INNER) {
            *x -= w->parent->inner[0];
            *y -= w->parent->inner[1];
        }
    }
}

static void parent_to_client(struct game_win *w, int32_t *x, int32_t *y) {
    if ((w->flags & WIN_F_CHILD) && w->parent) {
        win_screen_to_client_2(w->parent, x, y);
        if (w->flags & WIN_F_PARENT_INNER) {
            *x += w->parent->inner[0];
            *y += w->parent->inner[1];
        }
    }
}

static void move_rect(int32_t *r, int32_t dx, int32_t dy) {
    r[0] += dx, r[2] += dx;
    r[1] += dy, r[3] += dy;
}

// The point as the screen has it from the window's client area, and the reverse.
void WIN_THIS win_client_to_screen_2(struct game_win *w, int32_t *x, int32_t *y) {
    *x += w->outer[0] + w->inner[0];
    *y += w->outer[1] + w->inner[1];
    parent_to_screen(w, x, y);
}

void WIN_THIS win_screen_to_client_2(struct game_win *w, int32_t *x, int32_t *y) {
    *x -= w->outer[0] + w->inner[0];
    *y -= w->outer[1] + w->inner[1];
    parent_to_client(w, x, y);
}

// The same from the window's nonclient area (the frame is counted: the inner origin comes off).
void WIN_THIS win_nonclient_to_screen_2(struct game_win *w, int32_t *x, int32_t *y) {
    *x += w->outer[0] + w->inner[0];
    *y += w->outer[1] + w->inner[1];
    parent_to_screen(w, x, y);
    *x -= w->inner[0];
    *y -= w->inner[1];
}

void WIN_THIS win_screen_to_nonclient(struct game_win *w, int32_t *x, int32_t *y) {
    *x -= w->outer[0] + w->inner[0];
    *y -= w->outer[1] + w->inner[1];
    parent_to_client(w, x, y);
    *x += w->inner[0];
    *y += w->inner[1];
}

// The same for a rectangle (left, top, right, bottom): moved by what a point at the window's
// origin would be. Nothing happens without one.
void WIN_THIS win_screen_to_client(struct game_win *w, int32_t *r) {
    if (!r) return;
    int32_t dx = -(w->outer[0] + w->inner[0]), dy = -(w->outer[1] + w->inner[1]);
    parent_to_client(w, &dx, &dy);
    move_rect(r, dx, dy);
}

void WIN_THIS win_client_to_screen(struct game_win *w, int32_t *r) {
    if (!r) return;
    int32_t dx = w->outer[0] + w->inner[0], dy = w->outer[1] + w->inner[1];
    parent_to_screen(w, &dx, &dy);
    move_rect(r, dx, dy);
}

void WIN_THIS win_nonscreen_to_client(struct game_win *w, int32_t *r) {
    if (!r) return;
    int32_t dx = -(w->outer[0] + w->inner[0]), dy = -(w->outer[1] + w->inner[1]);
    parent_to_client(w, &dx, &dy);
    move_rect(r, dx + w->inner[0], dy + w->inner[1]);
}

void WIN_THIS win_nonclient_to_screen(struct game_win *w, int32_t *r) {
    if (!r) return;
    int32_t dx = w->outer[0] + w->inner[0], dy = w->outer[1] + w->inner[1];
    parent_to_screen(w, &dx, &dy);
    move_rect(r, dx - w->inner[0], dy - w->inner[1]);
}

// What the frame adds to a size. Flags 4 and 8 add the global margin to the height and to the
// width, a thick border (0x400) or a caption or any frame (0x10, 0x1) twice the border width
// to both, with the bottom border (when it differs) in place of one border at the bottom, a
// caption bar the caption height less the border to the height, and the window `nc_owner`
// names whatever its NC_HEIGHT slot says, to the height. Nothing happens without both of the
// two numbers.
void WIN_THIS win_client_to_nonclient(struct game_win *w, int32_t *x, int32_t *y) {
    if (!x || !y) return;
    uint32_t f = w->flags;
    if (f & WIN_F_MARGIN_H) *y += *game_win_nc_margin;
    if (f & WIN_F_MARGIN_W) *x += *game_win_nc_margin;
    if ((f & WIN_F_THICK) || (f & 0x11)) {
        *x += w->border * 2;
        *y += w->border * 2;
        if (w->bottom != -1) *y += w->bottom - w->border;
    }
    if (f & WIN_F_CAPTION) *y += w->caption_h - w->border;
    if (w->nc_owner) *y += ((int(THISCALL *)(struct game_win *))game_win_slot(w->nc_owner, WIN_VT_NC_HEIGHT))(w->nc_owner);
}

void WIN_THIS win_nonclient_to_client(struct game_win *w, int32_t *x, int32_t *y) {
    if (!x || !y) return;
    uint32_t f = w->flags;
    if (f & WIN_F_MARGIN_H) *y -= *game_win_nc_margin;
    if (f & WIN_F_MARGIN_W) *x -= *game_win_nc_margin;
    if ((f & WIN_F_THICK) || (f & 0x11)) {
        *x -= w->border * 2;
        *y -= w->border * 2;
        if (w->bottom != -1) *y += w->border - w->bottom;
    }
    if (f & WIN_F_CAPTION) *y += w->border - w->caption_h;
    if (w->nc_owner) *y -= ((int(THISCALL *)(struct game_win *))game_win_slot(w->nc_owner, WIN_VT_NC_HEIGHT))(w->nc_owner);
}

// The same for a rectangle: the margin added to the bottom and the right, the border out on every
// side, the caption's height less a border above the top (and the owner's number taken off it).
void WIN_THIS win_client_to_nonclient_2(struct game_win *w, int32_t *r) {
    if (!r) return;
    uint32_t f = w->flags;
    if (f & WIN_F_MARGIN_H) r[3] += *game_win_nc_margin;
    if (f & WIN_F_MARGIN_W) r[2] += *game_win_nc_margin;
    if ((f & WIN_F_THICK) || (f & 0x11)) {
        r[0] -= w->border;
        r[2] += w->border;
        r[1] -= w->border;
        r[3] += w->border;
        if (w->bottom != -1) r[3] += w->bottom - w->border;
    }
    if (f & WIN_F_CAPTION) r[1] += w->border - w->caption_h;
    if (w->nc_owner) r[1] -= ((int(THISCALL *)(struct game_win *))game_win_slot(w->nc_owner, WIN_VT_NC_HEIGHT))(w->nc_owner);
}

// Move the window so its top left is at (x, y), keeping its size: the outer rectangle for a
// window with a frame, the inner one for any other. Returns 0.
int WIN_THIS win_move(struct game_win *w, int x, int y) {
    int32_t *r = (w->flags2 & WIN_F2_FRAME) ? w->outer : w->inner;
    int32_t dx = x - r[0], dy = y - r[1];
    move_rect(r, dx, dy);
    return 0;
}

// What part of a window with a frame a point (in its outer rectangle) is on, as a Windows hit
// test: a corner (HTTOPLEFT 13, HTTOPRIGHT 14, HTBOTTOMLEFT 16, HTBOTTOMRIGHT 17) or an edge
// (HTLEFT 10, HTRIGHT 11, HTTOP 12, HTBOTTOM 15) within resize_border of the side; a point
// on none, or in a window with no frame, is on its caption (HTCAPTION 2) if it has a caption
// (or 0x4000000 or 0x1) and is within title_h of the top, and otherwise nowhere (0).
int WIN_THIS win_on_nc_hittest(struct game_win *w, int x, int y) {
    if (w->flags2 & WIN_F2_FRAME) {
        int b = w->resize_border;
        int right = w->outer[2] - w->outer[0] - b, bottom = w->outer[3] - w->outer[1] - b;
        if (x < b) {
            if (y < b) return 13;
            if (y > bottom) return 16;
        }
        if (x > right) {
            if (y < b) return 14;
            if (y > bottom) return 17;
        }
        if (x < b) return 10;
        if (y < b) return 12;
        if (x > right) return 11;
        if (y > bottom) return 15;
    }
    if (w->flags & 0x4000010) {
        if (w->title_h && y < w->title_h) return 2;
    } else if (w->flags & 1) {
        return 2;
    }
    return 0;
}

// ---- Scroll bars ---------------------------------------------------------------------------

int WIN_THIS win_get_vert_pos(struct game_win *w) { return w->scroll_v ? w->scroll_v->pos : 0; }

int WIN_THIS win_get_horz_pos(struct game_win *w) { return w->scroll_h ? w->scroll_h->pos : 0; }

void WIN_THIS win_set_vert_paging(struct game_win *w, int paging) {
    if (w->scroll_v) w->scroll_v->paging = paging;
}

void WIN_THIS win_set_horz_paging(struct game_win *w, int paging) {
    if (w->scroll_h) w->scroll_h->paging = paging;
}

// Win_UNK8 and Win_UNK9 set one number in each of the scroll bars (what it is isn't known).
void WIN_THIS win_unk8(struct game_win *w, int value) {
    if (w->scroll_v) w->scroll_v->f2144 = value;
    if (w->scroll_h) w->scroll_h->f2144 = value;
}

void WIN_THIS win_unk9(struct game_win *w, int value) {
    if (w->scroll_v) w->scroll_v->f2148 = value;
    if (w->scroll_h) w->scroll_h->f2148 = value;
}

// ---- Calls through the vtable, the dialog list and the rest that calls Windows --------------
//
// A Win's virtual functions are called through its vtable, with the arguments the original
// pushes (the callee pops them). The slots used here, by what the callers do with them: 0x8
// hides a window, 0xc sizes it (width, height, and a flag the callers leave 0), 0x10 tells it
// it lost the mouse capture, 0x30 and 0x34 follow a resize (the second with the new size),
// 0xcc tells a window of a dialog list it gained (1) or lost (0) the focus, 0xf8 tells a scroll
// bar its range or position changed, 0x108, 0x10c and 0x128 lay out or rebuild the nonclient
// area (the last two take (0, what)) and 0x16c is the nonclient height.
enum {
    VT_CAPTURE_LOST = 0x10,
    VT_RESIZED = 0x30,
    VT_ON_SIZE = 0x34,
    VT_FOCUS = 0xcc,
    VT_SCROLL_CHANGED = 0xf8,
    VT_LAYOUT = 0x108,
    VT_NC_REBUILD = 0x10c,
    VT_NC_UPDATE = 0x128,
};
static int vcall0(struct game_win *w, int slot) { return ((int(THISCALL *)(struct game_win *))game_win_slot(w, slot))(w); }
static int vcall1(struct game_win *w, int slot, int a) {
    return ((int(THISCALL *)(struct game_win *, int))game_win_slot(w, slot))(w, a);
}
static int vcall2(struct game_win *w, int slot, int a, int b) {
    return ((int(THISCALL *)(struct game_win *, int, int))game_win_slot(w, slot))(w, a, b);
}
static int vcall3(struct game_win *w, int slot, int a, int b, int c) {
    return ((int(THISCALL *)(struct game_win *, int, int, int))game_win_slot(w, slot))(w, a, b, c);
}

// Take a top-level window off the parent list: it is hidden first, and the list closed up from
// where it was as it stands after that. A window that isn't there is left alone.
void win_remove_parent(struct game_win *w) {
    if (!w) return;
    int n = *game_win_parent_n, i = 0;
    while (i < n && game_win_parents[i] != w) i++;
    if (i >= n) return;
    *game_win_parent_n = n - 1;
    vcall0(w, WIN_VT_HIDE);
    for (int m = *game_win_parent_n; i < m; i++) game_win_parents[i] = game_win_parents[i + 1];
}

// The same for one of the window's children, closing up the list while it is shorter than the
// place being filled.
void WIN_THIS win_remove_child(struct game_win *w, struct game_win *child) {
    if (!child) return;
    int n = w->n_children, i = 0;
    while (i < n && w->children[i] != child) i++;
    if (i >= n) return;
    w->n_children = n - 1;
    vcall0(child, WIN_VT_HIDE);
    while (i < w->n_children) {
        w->children[i] = w->children[i + 1];
        i++;
    }
}

// The scroll bars. A bar's position is kept within its range (counted from the far end for an
// inverted one) and the bar told (through slot 0xf8), if it hangs from a window; the global
// `cur` names that window. Setting a range puts the position at its start; a range given high
// to low is inverted, and kept low to high.
static void scroll_set_pos(struct game_win_scroll *s, int pos) {
    if (!s->win.parent) return;
    *game_win_cur = s->win.parent;
    int lo = s->lo;
    if (pos < lo) s->pos = lo;
    else if (pos > s->hi) s->pos = s->hi;
    else s->pos = pos;
    if (s->inverted) s->pos = s->hi - s->pos + lo;
    vcall0(&s->win, VT_SCROLL_CHANGED);
}

static void scroll_set_range(struct game_win_scroll *s, int a, int b) {
    if (a < b) {
        s->inverted = 0;
    } else {
        s->inverted = 1;
        int t = a;
        a = b, b = t;
    }
    s->lo = a, s->pos = a, s->hi = b;
    vcall0(&s->win, VT_SCROLL_CHANGED);
}

void WIN_THIS win_set_vert_pos(struct game_win *w, int pos) {
    if (w->scroll_v) scroll_set_pos(w->scroll_v, pos);
}

void WIN_THIS win_set_horz_pos(struct game_win *w, int pos) {
    if (w->scroll_h) scroll_set_pos(w->scroll_h, pos);
}

void WIN_THIS win_set_vert_range(struct game_win *w, int a, int b) {
    if (w->scroll_v) scroll_set_range(w->scroll_v, a, b);
}

void WIN_THIS win_set_horz_range(struct game_win *w, int a, int b) {
    if (w->scroll_h) scroll_set_range(w->scroll_h, a, b);
}

// A window with a frame takes a new caption height, border or bottom border and is sized again
// to the rectangle it has.
static void resize_to_outer(struct game_win *w) {
    vcall3(w, WIN_VT_RESIZE, w->outer[2] - w->outer[0], w->outer[3] - w->outer[1], 0);
}

void WIN_THIS win_set_caption_height(struct game_win *w, int h) {
    if (!(w->flags2 & WIN_F2_FRAME)) return;
    w->caption_h = h;
    resize_to_outer(w);
}

void WIN_THIS win_set_border_thickness(struct game_win *w, int b) {
    if (!(w->flags2 & WIN_F2_FRAME)) return;
    w->border = b;
    resize_to_outer(w);
}

void WIN_THIS win_set_bottom_border_thickness(struct game_win *w, int b) {
    if (!(w->flags2 & WIN_F2_FRAME)) return;
    w->bottom = b;
    resize_to_outer(w);
}

// ---- The dialog list ---------------------------------------------------------------------
//
// A window keeps a ring of the windows of its dialog (the list at 0xc8: head, the current node,
// the count and the current node's place). The current one has the focus. The global
// `prev_focus` is left holding the one that had it.

// Give the focus to `win`, if it is in the ring and isn't the focus already: the old one is told
// it lost it (only when the ring has more than one) and the new one that it gained it. Looking
// for it moves the current node on, so one that isn't there leaves the current place past the
// last node.
void WIN_THIS win_set_dialog_focus(struct game_win *w, struct game_win *win) {
    if (!win || !w->dlg_count) return;
    struct game_win *cur = w->dlg_head ? w->dlg_cur->win : NULL;
    if (win == cur) return;
    *game_win_prev_focus = cur;
    if (!w->dlg_head) return;
    int n = w->dlg_count;
    w->dlg_index = 0;
    w->dlg_cur = w->dlg_head;
    for (int i = 0; i < n; i++) {
        if (w->dlg_cur->win == win) {
            if (cur && w->dlg_count > 1) vcall1(cur, VT_FOCUS, 0);
            if (w->dlg_head) {
                struct game_win *c = w->dlg_cur->win;
                if (c) vcall1(c, VT_FOCUS, 1);
            }
            return;
        }
        w->dlg_index++;
        w->dlg_cur = w->dlg_cur->next;
    }
}

// Pass the focus to the next window of the parent's ring. Tells the window itself (not the one
// that had it) that it lost the focus, and the new one that it gained it; nothing happens for
// a window without a parent or one whose parent's ring has fewer than two.
void WIN_THIS win_pass_dialog_focus(struct game_win *w) {
    struct game_win *p = w->parent;
    if (!p || p->dlg_count <= 1) return;
    *game_win_prev_focus = p->dlg_head ? p->dlg_cur->win : NULL;
    if (p->dlg_head) {
        p->dlg_cur = p->dlg_cur->next;
        p->dlg_index++;
        if (p->dlg_index == p->dlg_count) p->dlg_index = 0;
    }
    vcall1(w, VT_FOCUS, 0);
    p = w->parent;
    if (p->dlg_head) {
        struct game_win *c = p->dlg_cur->win;
        if (c) vcall1(c, VT_FOCUS, 1);
    }
}

// ---- The mouse, and showing the window ---------------------------------------------------

// Move the mouse to a point in the window's client area (the screen position it is at).
int WIN_THIS win_set_mouse_pos(struct game_win *w, int x, int y) {
    x += w->outer[0] + w->inner[0];
    y += w->outer[1] + w->inner[1];
    parent_to_screen(w, &x, &y);
    return game_set_cursor_pos(x, y);
}

// Where the mouse is in the window's client area. Nothing happens without both places to put it.
void WIN_THIS win_get_mouse_pos(struct game_win *w, int32_t *px, int32_t *py) {
    if (!px || !py) return;
    int32_t pt[2] = {0, 0};
    game_get_cursor_pos(pt);
    *px = pt[0] - (w->outer[0] + w->inner[0]);
    *py = pt[1] - (w->outer[1] + w->inner[1]);
    parent_to_client(w, px, py);
}

// The state of the left or right mouse button: the system's answer to whether the buttons are
// swapped (GetSystemMetrics 0x17), complemented while the button is down (the top bit of
// GetAsyncKeyState). The original XORs a whole word with the sign of the key state.
int win_get_lbutton_state(void) {
    int down = (int16_t)game_get_async_key_state(1) >> 15;
    return game_get_system_metrics(0x17) ^ down;
}

int win_get_rbutton_state(void) {
    int down = (int16_t)game_get_async_key_state(2) >> 15;
    return game_get_system_metrics(0x17) ^ down;
}

// Maximize or minimize the game's window (ShowWindow 3 or 6; show_maximize is the same as
// maximize): the mouse presses are forgotten, a window with the capture is told it lost it, and
// the hover goes if it was this window. Returns 0.
static int show_state(struct game_win *w, int cmd) {
    struct game_win *capture = *game_win_capture;
    *game_win_press1 = NULL;
    *game_win_press2 = NULL;
    if (capture == w) {
        *game_win_capture = NULL;
        vcall0(w, VT_CAPTURE_LOST);
    }
    if (*game_win_hover == w) *game_win_hover = NULL;
    game_show_window(*game_win_hwnd, cmd);
    return 0;
}

int WIN_THIS win_maximize(struct game_win *w) { return show_state(w, 3); }
int WIN_THIS win_minimize(struct game_win *w) { return show_state(w, 6); }
int WIN_THIS win_show_maximize(struct game_win *w) { return show_state(w, 3); }

// ---- Placing and sizing ------------------------------------------------------------------

// Put the window in the middle of the screen (or of its parent's client area, for one with a
// parent). The sum is made in the rectangle in use (the outer one for a window with a frame) and
// halved towards zero.
int WIN_THIS win_center(struct game_win *w) {
    const int32_t *r = (w->flags2 & WIN_F2_FRAME) ? w->outer : w->inner;
    int x = (r[0] - r[2] + *game_win_screen_w) / 2;
    int y = (r[1] - r[3] + *game_win_screen_h) / 2;
    struct game_win *p = w->parent;
    if (p) {
        x -= p->outer[0] + p->inner[0];
        y -= p->outer[1] + p->inner[1];
        parent_to_client(p, &x, &y);
    }
    return win_move(w, x, y);
}

// The nonclient area, taken off a rectangle that is the whole window: the reverse of
// win_client_to_nonclient_2 (the margin off the bottom and the right, the border in on every
// side, the caption height less a border below the top, and what the owner's slot says).
void WIN_THIS win_nonclient_to_client_2(struct game_win *w, int32_t *r) {
    if (!r) return;
    uint32_t f = w->flags;
    if (f & WIN_F_MARGIN_H) r[3] -= *game_win_nc_margin;
    if (f & WIN_F_MARGIN_W) r[2] -= *game_win_nc_margin;
    if ((f & WIN_F_THICK) || (f & 0x11)) {
        int b = w->border;
        r[0] += b;
        r[2] -= b;
        r[1] += b;
        r[3] -= b;
        if (w->bottom != -1) r[3] += b - w->bottom;
    }
    if (f & WIN_F_CAPTION) r[1] += w->caption_h - w->border;
    if (w->nc_owner) r[1] += vcall0(w->nc_owner, WIN_VT_NC_HEIGHT);
}

// Lay out the caption buttons along the right of the caption: from the right edge less the
// button size twice, the border, the caption height and 2, the rightmost button (0xec) and the
// next (0xe8, whose picture is for a maximized window if the game's window is) and then 0xe4,
// each a gap (3, 1, then none) and the width of the one before to the left.
void WIN_THIS win_redo_caption_buttons(struct game_win *w) {
    int size = w->cap_btn;
    int x = w->outer[2] - size * 2 - w->outer[0] - w->border - w->caption_h - 2;
    if (w->btn_ec) {
        win_move(&w->btn_ec->win, x, size);
        x += -3 - w->btn_ec->width;
    }
    if (w->btn_e8) {
        win_move(&w->btn_e8->win, x, size);
        w->btn_e8->picture = game_is_zoomed(*game_win_hwnd) ? *game_win_pic_zoomed : *game_win_pic_normal;
        x += -1 - w->btn_e8->width;
    }
    if (w->btn_e4) win_move(&w->btn_e4->win, x, size);
}

// What happens when a window is resized to a client width and height: the global `cur` names
// it, the scroll bars go to the right and bottom edges and are sized to them (a window with
// flag 0x40 keeps its bars alone), the callback at 0x400 is called, and the window is told
// (slot 0x34) and its caption buttons put in place.
void WIN_THIS win_resize_event(struct game_win *w, int cw, int ch) {
    *game_win_cur = w;
    if (!(w->flags & 0x40)) {
        struct game_win_scroll *sv = w->scroll_v;
        if (sv) {
            win_move(&sv->win, w->inner[2] - w->inner[0], 0);
            sv = w->scroll_v;
            struct game_win_scroll *sh = w->scroll_h;
            int h = sh ? sh->height - w->inner[1] + w->inner[3] : w->inner[3] - w->inner[1];
            vcall3(&sv->win, WIN_VT_RESIZE, sv->width, h, 0);
        }
        struct game_win_scroll *sh = w->scroll_h;
        if (sh) {
            win_move(&sh->win, 0, w->inner[3] - w->inner[1]);
            sv = w->scroll_v;
            int width = sv ? w->inner[2] - sv->width - w->inner[0] : w->inner[2] - w->inner[0];
            vcall3(&sh->win, WIN_VT_RESIZE, width, -sh->height, 0);
        }
    }
    if (w->resize_cb) ((void (*)(int, int))w->resize_cb)(cw, ch);
    vcall2(w, VT_ON_SIZE, cw, ch);
    win_redo_caption_buttons(w);
}

// Size the window. With `client` set the numbers are a client size (for a window with a frame
// they are made a nonclient one first); the outer rectangle (frame) or inner one (none) takes
// them from its top left, the inner one of a window with a frame is made from it (starting at
// 0 and taken down to the client area), and the window is laid out (0x108), its root sized
// to the same numbers if it is another window, the resize event run and slot 0x30 called.
int WIN_THIS win_resize(struct game_win *w, int cx, int cy, int client) {
    if (client && !(w->flags2 & WIN_F2_FRAME)) {
        w->inner[2] = w->inner[0] + cx;
        w->inner[3] = w->inner[1] + cy;
    } else {
        if (client) win_client_to_nonclient(w, &cx, &cy);
        w->outer[2] = w->outer[0] + cx;
        w->outer[3] = w->outer[1] + cy;
        w->inner[0] = w->outer[0], w->inner[1] = w->outer[1], w->inner[2] = w->outer[2], w->inner[3] = w->outer[3];
        w->inner[2] -= w->inner[0], w->inner[3] -= w->inner[1];
        w->inner[0] = 0, w->inner[1] = 0;
        win_nonclient_to_client_2(w, w->inner);
    }
    vcall0(w, VT_LAYOUT);
    if (w->root != w) vcall3(w->root, WIN_VT_RESIZE, cx, cy, 0);
    win_resize_event(w, w->inner[2] - w->inner[0], w->inner[3] - w->inner[1]);
    vcall0(w, VT_RESIZED);
    return 0;
}

// Ask the window to rebuild one of its nonclient buffers (0 to 3) and get it: the ones at 0xb4,
// 0xb8, 0xc0 and 0xbc, or none for another number.
void *WIN_THIS win_redraw_nc_buffer(struct game_win *w, unsigned which) {
    vcall2(w, VT_NC_REBUILD, 0, (int)which);
    switch (which) {
    case 0: return w->nc_buf_b4;
    case 1: return w->nc_buf_b8;
    case 2: return w->nc_buf_c0;
    case 3: return w->nc_buf_bc;
    default: return NULL;
    }
}

int WIN_THIS win_update_nc_buffer(struct game_win *w, int what) { return vcall2(w, VT_NC_UPDATE, 0, what); }

// ---- Which window is under the mouse -------------------------------------------------------

// What a window's flags mean to the hit test: it draws its frame from the nonclient buffers
// (WIN_F_NC_BUFFERS) so a point on the frame is its own only where those hold a picture (not
// the transparent colour), and one without a frame may let its own pixels decide
// (WIN_F_HIT_PIXELS) unless it takes every point (WIN_F_HIT_ALL).
enum { WIN_F_BIT1 = 0x2, WIN_F_HIT_ALL = 0x100, WIN_F_HIT_PIXELS = 0x1000000, WIN_F_NC_BUFFERS = 0x10000000, WIN_F_THIN = 0x400000 };
enum { VT_HAS_NC = 0xf4, VT_FLUSH = 0xfc, VT_DRAW = 0x124 };

static int in_box(int x, int y, const int32_t *r) { return x >= r[0] && x < r[2] && y >= r[1] && y < r[3]; }

// The Buffer a window of the game's own classes keeps after the Win part (at 0x444): the picture
// whose pixels decide where a window without a frame is hit.
static struct game_buffer *hit_buffer(struct game_win *w) { return (struct game_buffer *)((uint8_t *)w + 0x444); }

// The mouse's place counted from a window's client area: less its place, and in its parent's
// coordinates if it is a child (an inner-positioned child counts from the parent's inner corner).
static void to_client(struct game_win *w, int32_t *px, int32_t *py) {
    *px -= w->outer[0] + w->inner[0];
    *py -= w->outer[1] + w->inner[1];
    if ((w->flags & WIN_F_CHILD) && w->parent) {
        win_screen_to_client_2(w->parent, px, py);
        if (w->flags & WIN_F_PARENT_INNER) {
            *px += w->parent->inner[0];
            *py += w->parent->inner[1];
        }
    }
}

// What the modal stack's last column holds for the top entry: the window the stack held before it
// (a 32-bit pointer kept as a number).
static struct game_win *modal_prev(void) { return (struct game_win *)(uintptr_t)(uint32_t)*game_win_modal_prev; }

// The window (or the one inside it) a point falls in, or none. The point is in the window's
// parent's coordinates for a child; a window that isn't a child is looked for where the mouse
// first was (game_win_mouse_pos) and the point put back when nothing is found. A hit on a frame
// sets game_win_hit_nc. Children come first, then the window's own frame (its nonclient
// buffers decide where a window that draws them is solid), then its client area.
static struct game_win *hit_window(struct game_win *w, int32_t *px, int32_t *py) {
    if (!(w->flags2 & WIN_F2_VISIBLE)) return NULL;
    if (w->parent && !win_is_visible(w->parent)) return NULL;
    int32_t sx = 0, sy = 0;
    int top_level = !(w->flags & WIN_F_CHILD);
    if (top_level) {
        sx = *px, *px = game_win_mouse_pos[0];
        sy = *py, *py = game_win_mouse_pos[1];
    }
    struct game_win *r;
    for (int i = 0; i < w->n_children; i++)
        if (!(w->children[i]->flags & WIN_F_CHILD) && (r = hit_window(w->children[i], px, py))) return r;
    if (w->flags2 & WIN_F2_FRAME) {
        if (!in_box(*px, *py, w->outer)) goto none;
        *px -= w->outer[0];
        *py -= w->outer[1];
        if (w->root == w)
            for (int i = 0; i < w->n_children; i++) {
                struct game_win *c = w->children[i];
                if ((c->flags & WIN_F_PARENT_INNER) && (c->flags & WIN_F_CHILD) && (r = hit_window(c, px, py))) return r;
            }
        if (!in_box(*px, *py, w->inner)) {
            // On the frame: its own, unless it draws the frame from buffers and the point is on a transparent pixel.
            if (!vcall0(w, VT_HAS_NC) || !(w->flags & WIN_F_NC_BUFFERS)) goto frame;
            int top = (w->flags & WIN_F_CAPTION) && !(w->flags & WIN_F_THIN) ? w->caption_h : w->border;
            int bottom = w->bottom == -1 ? w->border : w->bottom;
            int h = w->outer[3] - w->outer[1];
            int px_ = *px, py_ = *py, c;
            if (py_ < top) c = buffer_get_pixel(w->nc_buf_b4, px_, py_);
            else if (py_ > h - bottom) c = buffer_get_pixel(w->nc_buf_c0, px_, py_ - h + bottom);
            else if (px_ < w->border) c = buffer_get_pixel(w->nc_buf_bc, px_, py_ - top);
            else c = buffer_get_pixel(w->nc_buf_b8, px_ - (w->outer[2] - w->outer[0]) + w->border, py_ - top);
            if (c != *game_default_key) goto frame;
            *px += w->outer[0];
            *py += w->outer[1];
            goto none;
        frame:
            *game_win_hit_nc = 1;
            return w;
        }
        *px -= w->inner[0];
        *py -= w->inner[1];
    } else {
        if (!in_box(*px, *py, w->inner)) goto none;
        *px -= w->inner[0];
        *py -= w->inner[1];
    }
    *game_win_hit_nc = 0;
    if (w->root == w)
        for (int i = 0; i < w->n_children; i++) {
            struct game_win *c = w->children[i];
            if (!(c->flags & WIN_F_PARENT_INNER) && (c->flags & WIN_F_CHILD) && (r = hit_window(c, px, py))) return r;
        }
    if (w->flags & WIN_F_BIT1) goto none;
    if (!vcall0(w, VT_HAS_NC)) return w;
    if (!(w->flags & WIN_F_HIT_PIXELS) || (w->flags & WIN_F_HIT_ALL)) return w;
    {
        int c = buffer_get_pixel(hit_buffer(w), *px, *py);
        if ((uint8_t)c == *game_default_key) goto none;
        if (*(int32_t *)((uint8_t *)w + 0xa0) == 0 || (uint8_t)c != 0) return w;
    }
none:
    if (top_level) *px = sx, *py = sy;
    return NULL;
}

// The window under the mouse at (*px, *py), put in the coordinates of the window found. One
// that has the mouse pressed or captured, or is hovered over (in that order, forgotten if it
// is no longer shown) is it wherever the mouse is; otherwise the topmost modal window, or the
// first of the top-level windows, that the point falls in. The answer is the window's root
// unless the hit was on a frame.
struct game_win *win_get_mouse_window(int32_t *px, int32_t *py) {
    game_win_mouse_pos[0] = *px;
    game_win_mouse_pos[1] = *py;
    struct game_win *found = NULL;
    for (;;) {
        struct game_win *w;
        if ((w = *game_win_press1)) {
            if (!(w->flags2 & WIN_F2_VISIBLE)) {
                *game_win_press1 = NULL;
                continue;
            }
            to_client(w, px, py);
            *game_win_hit_nc = 0;
            return w;
        }
        if ((w = *game_win_press2)) {
            if (!(w->flags2 & WIN_F2_VISIBLE)) {
                *game_win_press2 = NULL;
                continue;
            }
            win_screen_to_client_2(w, px, py);
            *px += w->inner[0];
            *py += w->inner[1];
            *game_win_hit_nc = 1;
            return w;
        }
        if ((w = *game_win_capture)) {
            if (!(w->flags2 & WIN_F2_VISIBLE)) {
                *game_win_capture = NULL;
                continue;
            }
            to_client(w, px, py);
            *game_win_hit_nc = 0;
            return w;
        }
        if ((w = *game_win_hover)) {
            if (!(w->flags2 & WIN_F2_VISIBLE)) {
                *game_win_hover = NULL;
                continue;
            }
            win_screen_to_client_2(w, px, py);
            *px += w->inner[0];
            *py += w->inner[1];
            *game_win_hit_nc = 1;
            return w;
        }
        break;
    }
    struct game_win *modal = *game_win_modal;
    if (modal) {
        to_client(modal, px, py);
        *px += modal->inner[0];
        *py += modal->inner[1];
        found = hit_window(modal, px, py);
        if (!found && modal_prev()) {
            struct game_win *prev = modal_prev();
            *px = game_win_mouse_pos[0];
            *py = game_win_mouse_pos[1];
            win_screen_to_client_2(prev, px, py);
            *px += prev->inner[0];
            *py += prev->inner[1];
            found = hit_window(modal_prev(), px, py);
        }
        if (!found) {
            *px = game_win_mouse_pos[0];
            *py = game_win_mouse_pos[1];
            to_client(modal, px, py);
            found = modal;
            *game_win_hit_nc = 0;
        }
    } else {
        for (int i = 0; i < *game_win_parent_n && !found; i++) found = hit_window(game_win_parents[i], px, py);
    }
    if (found && !*game_win_hit_nc) found = found->root;
    return found;
}

// ---- Drawing the windows into the back buffer ------------------------------------------------

static int rect_empty(const int32_t *r) { return r[0] >= r[2] || r[1] >= r[3]; }

// What the Windows call IntersectRect does (the original calls it): the overlap of two
// rectangles into `out`, which may be one of them; an empty one is stored as all zeros.
static int intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (rect_empty(r)) {
        memset(out, 0, 16);
        return 0;
    }
    memcpy(out, r, 16);
    return 1;
}

// Draw the shown windows, back to front, into the back buffer: from the one given (and everything
// behind it, when that is a window that hides all those under it: the `opaque` one) or all of
// them, the area `rect` (or all of the screen). With nothing drawn through another Buffer
// (back_alt) the area is first cleared. Each window with a frame has its frame redrawn (slot
// 0x128) and its root drawn (0x124), unless the one given isn't a root. Then, where the overlay
// is on, the part of the back buffer it lies over (cut to `rect`) is copied into the overlay
// Buffer, which keeps what is under it (Buffer_copy_full copies out of the Buffer it is called on).
void win_update_screen(const int32_t *rect, struct game_win *win) {
    int n = *game_win_z_n;
    if (!n) return;
    if (!*game_win_opaque) win = NULL;
    int start = n - 1;
    if (win) {
        start = 0;
        while (start < n && game_win_zorder[start] != win) start++;
        if (start == n) return;
    } else if (!*game_win_back_alt) {
        if (rect) buffer_fill_rect(game_win_back, rect, 0);
        else buffer_fill_all(game_win_back, 0);
    }
    for (int i = start; i >= 0; i--) {
        struct game_win *w = game_win_zorder[i];
        if (!(w->flags2 & WIN_F2_VISIBLE)) continue;
        if (w->parent && !win_is_visible(w->parent)) continue;
        if (w->flags2 & WIN_F2_FRAME) vcall2(w, VT_NC_UPDATE, (int)(uintptr_t)rect, -1);
        if (win && win != win->root) continue;
        vcall1(w->root, VT_DRAW, (int)(uintptr_t)rect);
    }
    if (!*game_win_overlay_on) return;
    int x0 = game_win_overlay_pos[0], y0 = game_win_overlay_pos[1];
    int32_t r[4] = {x0, y0, x0 + game_win_overlay_size[0], y0 + game_win_overlay_size[1]};
    if (rect && !intersect(r, r, rect)) return;
    buffer_copy_full(game_win_back, game_win_overlay, r[0], r[1], r[0] - x0, r[1] - y0, r[2] - r[0], r[3] - r[1]);
}

// Draw one window's area (all of the screen with no rectangle): the rectangle, which is in
// the window's client coordinates, is taken to the screen's first.
void WIN_THIS win_update_window(struct game_win *w, const int32_t *rect) {
    if (!rect) {
        win_update_screen(NULL, w);
        return;
    }
    int32_t x = w->outer[0] + w->inner[0], y = w->outer[1] + w->inner[1];
    if ((w->flags & WIN_F_CHILD) && w->parent) {
        win_client_to_screen_2(w->parent, &x, &y);
        if (w->flags & WIN_F_PARENT_INNER) {
            x -= w->parent->inner[0];
            y -= w->parent->inner[1];
        }
    }
    int32_t r[4] = {rect[0] + x, rect[1] + y, rect[2] + x, rect[3] + y};
    win_update_screen(r, w);
}

// The place a window's top left lies on the screen, added to the offsets (0x9b7a70, 0x9b7a74) a draw into
// another Buffer is shifted by: the window's own place, and that of its parent when it hangs from one
// (the parent's client area, or with WIN_F_PARENT_INNER its inner rectangle's top left taken off).
static void add_origin(struct game_win *w) {
    int32_t *off = game_win_back_off;
    off[0] += w->outer[0] + w->inner[0];
    off[1] += w->outer[1] + w->inner[1];
    if ((w->flags & WIN_F_CHILD) && w->parent) {
        win_client_to_screen_2(w->parent, &off[0], &off[1]);
        if (w->flags & WIN_F_PARENT_INNER) {
            off[0] -= w->parent->inner[0];
            off[1] -= w->parent->inner[1];
        }
    }
}

// What a window shows with itself hidden, drawn into `dest` (which is made as big as the window, with
// its frame if it has one): the screen behind the window, for the game to put back when it goes. The
// window is marked hidden, nothing is made opaque, and the z-order list is made again with it out;
// the draw is shifted so the window's top left is (0, 0) in `dest`. Then all is put back.
void WIN_THIS win_update_back_to_window(struct game_win *w, struct game_buffer *dest) {
    if (!dest) return;
    *game_win_back_alt = dest;
    *game_win_opaque = NULL;
    game_win_back_off[0] = game_win_back_off[1] = 0;
    uint32_t saved = w->flags2;
    w->flags2 &= ~(uint32_t)WIN_F2_VISIBLE;
    win_update_zorder();
    const int32_t *r;
    if (w->flags2 & WIN_F2_FRAME) {
        win_client_to_screen_2(w, &game_win_back_off[0], &game_win_back_off[1]);
        game_win_back_off[0] -= w->inner[0];
        game_win_back_off[1] -= w->inner[1];
        r = w->outer;
    } else {
        add_origin(w);
        r = w->inner;
    }
    int32_t rect[4] = {r[0], r[1], r[2], r[3]};
    buffer_init(dest, rect[2] - rect[0], rect[3] - rect[1], 0, NULL);
    int dx = game_win_back_off[0] - rect[0], dy = game_win_back_off[1] - rect[1];
    int32_t shifted[4] = {rect[0] + dx, rect[1] + dy, rect[2] + dx, rect[3] + dy};
    win_update_screen(shifted, NULL);
    *game_win_back_alt = NULL;
    *game_win_opaque = NULL;
    w->flags2 = saved;
    win_update_zorder();
}

// What a window looks like (with whatever it hides, if it is opaque, left out), drawn into `dest`: the
// window is marked shown and made the opaque one, the z-order list is made again, and the draw is
// shifted by the window's place, taking its inner rectangle's top left off for a window with a frame,
// so the picture starts at the window's own top left. `dest` is not changed in size.
void WIN_THIS win_update_window_to_buffer(struct game_win *w, struct game_buffer *dest) {
    if (!dest) return;
    *game_win_back_alt = dest;
    *game_win_opaque = w;
    game_win_back_off[0] = game_win_back_off[1] = 0;
    uint32_t saved = w->flags2;
    w->flags2 |= WIN_F2_VISIBLE;
    win_update_zorder();
    add_origin(w);
    if (w->flags2 & WIN_F2_FRAME) {
        game_win_back_off[0] -= w->inner[0];
        game_win_back_off[1] -= w->inner[1];
    }
    win_update_screen(NULL, w);
    *game_win_back_alt = NULL;
    *game_win_opaque = NULL;
    w->flags2 = saved;
    win_update_zorder();
}

// ---- GraphicWin ----------------------------------------------------------------------------------
//
// A GraphicWin is a Win with a Buffer of its own at 0x444 (the pictures it shows are drawn into it), a
// button above all. Its update functions show what was drawn: the window is told to draw the area
// (win_update_window), the area is taken to the screen and the main Buffer's part of it is flipped to the
// window. Its soft updates only mark the area of the window as in need of painting (InvalidateRect). Its
// paint puts the Buffer into the back buffer (or into the Buffer the draw is being redirected to), at the
// window's place, cut to its parent's client area and to the area asked for: with the key colour left out
// (and through one or two colour tables) if the window says so (0x1000000), else the whole Buffer.
// The update functions return 0 as the original does when the window is not shown (and something left in
// eax when it is: nothing relies on it).

#define GW_BUFFER(w) ((struct game_buffer *)((uint8_t *)(w) + 0x444))
#define GW_TABLE(w, off) (*(uint8_t **)((uint8_t *)(w) + (off)))
enum { GW_KEYED = 0x1000000, GW_TABLE_A = 0xa0, GW_TABLE_B = 0xa4 };

// Shows an area (in the window's client coordinates): drawn, taken to the screen, flipped.
static void gw_show(struct game_win *w, int32_t *r) {
    win_update_window(w, r);
    win_client_to_screen(w, r);
    win_flip(r);
}

int WIN_THIS graphic_win_update(struct game_win *w, int x, int y, int cw, int ch, int unused) {
    if (win_is_visible(w)) {
        int32_t r[4] = {x, y, x + cw, y + ch};
        gw_show(w, r);
    }
    return 0;
}

// The area asked for, or the Buffer's whole area.
int WIN_THIS graphic_win_update_2(struct game_win *w, const int32_t *rect, int unused) {
    if (!rect) rect = GW_BUFFER(w)->full;
    int32_t r[4] = {rect[0], rect[1], rect[2], rect[3]};
    if (win_is_visible(w)) gw_show(w, r);
    return 0;
}

// The Buffer's whole area, from (0, 0).
int WIN_THIS graphic_win_update_3(struct game_win *w, int unused) {
    const int32_t *full = GW_BUFFER(w)->full;
    int32_t r[4] = {0, 0, full[2] - full[0], full[3] - full[1]};
    if (win_is_visible(w)) gw_show(w, r);
    return 0;
}

// The area `a` (in the Buffer's coordinates) taken to the window's place on the screen and marked as needing paint.
static int gw_invalidate(struct game_win *w, int32_t *a) {
    int32_t ox = 0, oy = 0;
    win_client_to_screen_2(w, &ox, &oy);
    a[0] += ox, a[2] += ox, a[1] += oy, a[3] += oy;
    return game_invalidate_rect((void *)(uintptr_t)*game_win_hwnd, a, 0);
}

// An area of the Buffer (x, y, width, height), cut to the Buffer; nothing if it is empty.
int WIN_THIS graphic_win_soft_update(struct game_win *w, int x, int y, int cw, int ch) {
    if (!win_is_visible(w)) return 0;
    int32_t a[4] = {x, y, x + cw, y + ch};
    if (!game_intersect_rect(a, a, GW_BUFFER(w)->full)) return 0;
    return gw_invalidate(w, a);
}

// The Buffer's whole area.
int WIN_THIS graphic_win_soft_update_2(struct game_win *w) {
    if (!win_is_visible(w)) return 0;
    int32_t a[4];
    memcpy(a, GW_BUFFER(w)->full, sizeof a);
    return gw_invalidate(w, a);
}

// A rectangle cut to the Buffer, or all of it.
int WIN_THIS graphic_win_soft_update_3(struct game_win *w, const int32_t *rect) {
    if (!win_is_visible(w)) return 0;
    int32_t a[4];
    if (!rect) {
        if (!win_is_visible(w)) return 0;
        memcpy(a, GW_BUFFER(w)->full, sizeof a);
    } else {
        memcpy(a, rect, sizeof a);
        if (!game_intersect_rect(a, a, GW_BUFFER(w)->full)) return 0;
    }
    return gw_invalidate(w, a);
}

// ---- Copying a window into another's frame --------------------------------------------------------
//
// The large function at 0x5f3b80 (the one the task notes call the frame overlay: the copy goes the other way). Buffer_copy_full
// copies from the Buffer it is called on into the one it is given, so this puts parts of this window's frame strips
// (and its own Buffer at 0x444) into another window's strips and picture: the other window has a sprite frame (bit
// 0x10000000, see below), is on the screen over this one, and the pieces of this window it covers are copied into
// its frame, so what is drawn of it (the strips it is drawn from) shows this window under it. The other window's strips
// are placed on the screen from its outer rectangle: the top one at its corner, the left one and the right one below the
// top one's bottom edge (the right one flush with the top one's right edge, worked out from its own left edge less its
// right edge, and counting the left edge twice, as the original does), the bottom one below the left one. This
// window's strips are placed likewise from its own outer rectangle (the top at the corner, the left one below it, the
// bottom one up from the bottom edge, the right one in from the right edge). Each strip of this window, cut to `rect` if
// there is one, goes into each of the other's (the top, left, right, bottom ones, for `which` of -1 or 0, 3, 1, 2; a
// `rect` makes `which` -1) where they overlap, and, for `which` of -1, into the other's picture (a Buffer at 0xa08)
// where it overlaps the other's window rectangle in Windows, if the other draws its frame itself (slot 0xf4) and has
// bit 0x40000000. Then this window's own Buffer, if it draws a frame itself, goes the same way from its own window
// rectangle in Windows. OpenSMACX does not know the function.

struct ovl_src {
    struct game_buffer *buf;
    int32_t r[4];
    int code;
};

static int ovl_empty(const int32_t *r) { return r[2] <= r[0] || r[3] <= r[1]; }

// What `dest` (whose place on the screen is `q`) takes from the other window's strips and picture.
static void ovl_take(struct game_buffer *dest, const int32_t *q0, const int32_t *q1, const struct ovl_src src[4], int have_pic,
                     struct game_buffer *pic, const int32_t *wr, int which) {
    int32_t r[4];
    for (int j = 0; j < 4; j++) {
        int hit = game_intersect_rect(r, q1, src[j].r);
        if (hit && (which == -1 || which == src[j].code))
            buffer_copy_full(dest, src[j].buf, r[0] - q0[0], r[1] - q0[1], r[0] - src[j].r[0], r[1] - src[j].r[1], r[2] - r[0], r[3] - r[1]);
    }
    if (have_pic && which == -1 && game_intersect_rect(r, q1, wr))
        buffer_copy_full(dest, pic, r[0] - q0[0], r[1] - q0[1], r[0] - wr[0], r[1] - wr[1], r[2] - r[0], r[3] - r[1]);
}

void WIN_THIS win_copy_into_frame(struct game_win *w, struct game_win *o, const int32_t *rect, int which) {
    if (!o || !(o->flags2 & WIN_F2_FRAME) || !(o->flags & 0x10000000)) return;
    if (rect) which = -1;
    if (rect && ovl_empty(rect)) return;
    struct game_buffer *ob[4] = {o->nc_buf_b4, o->nc_buf_bc, o->nc_buf_b8, o->nc_buf_c0};  // top, left, right, bottom
    const int32_t ox = o->outer[0], oy = o->outer[1];
    struct ovl_src src[4];
    for (int j = 0; j < 4; j++) src[j].buf = ob[j];
    src[0].code = 0, src[1].code = 3, src[2].code = 1, src[3].code = 2;
    const int32_t *f;
    f = ob[0]->full;
    int32_t top_bottom = f[3] + oy, top_right = f[2] + ox;
    src[0].r[0] = f[0] + ox, src[0].r[1] = f[1] + oy, src[0].r[2] = f[2] + ox, src[0].r[3] = top_bottom;
    f = ob[1]->full;
    int32_t left_bottom = f[3] + top_bottom;
    src[1].r[0] = f[0] + ox, src[1].r[1] = f[1] + top_bottom, src[1].r[2] = f[2] + ox, src[1].r[3] = left_bottom;
    f = ob[3]->full;
    src[3].r[0] = f[0] + ox, src[3].r[1] = f[1] + left_bottom, src[3].r[2] = f[2] + ox, src[3].r[3] = f[3] + left_bottom;
    f = ob[2]->full;
    int32_t rx = f[0] - f[2] + top_right;
    src[2].r[0] = f[0] + rx, src[2].r[1] = f[1] + top_bottom, src[2].r[2] = f[2] + rx, src[2].r[3] = f[3] + top_bottom;
    int32_t wr[4] = {0, 0, 0, 0};
    struct game_buffer *pic = NULL;
    int have_pic = 0;
    if (vcall0(o, 0xf4) && (o->flags & 0x40000000)) {
        game_get_window_rect(*game_win_hwnd, wr);
        pic = *(struct game_buffer **)((uint8_t *)o + 0xa08);
        have_pic = 1;
    }
    if (w->flags2 & WIN_F2_FRAME) {
        struct game_buffer *tb = w->nc_buf_b4, *lb = w->nc_buf_bc, *bb = w->nc_buf_c0, *rb = w->nc_buf_b8;
        struct game_buffer *dest[4] = {tb, lb, bb, rb};
        int32_t xoff[4] = {w->outer[0], w->outer[0], w->outer[0], w->outer[2] - rb->width};
        int32_t yoff[4] = {w->outer[1], w->outer[1] - tb->neg_height, w->outer[3] + bb->neg_height, w->outer[1] - tb->neg_height};
        for (int d = 0; d < 4; d++) {
            const int32_t *df = dest[d]->full;
            int32_t q0[4] = {df[0] + xoff[d], df[1] + yoff[d], df[2] + xoff[d], df[3] + yoff[d]}, q1[4];
            memcpy(q1, q0, sizeof q1);
            if (rect && !game_intersect_rect(q1, q0, rect)) continue;
            ovl_take(dest[d], q0, q1, src, have_pic, pic, wr, which);
        }
    }
    if (!vcall0(w, 0xf4)) return;
    int32_t q0[4], q1[4];
    game_get_window_rect(*game_win_hwnd, q0);
    memcpy(q1, q0, sizeof q1);
    if (rect && !game_intersect_rect(q1, q0, rect)) return;
    ovl_take((struct game_buffer *)((uint8_t *)w + 0x444), q0, q1, src, have_pic, pic, wr, which);
}

// ---- The GraphicWin's frame --------------------------------------------------------------------
//
// A GraphicWin with bit 0x10000000 set draws its frame from sprites (the pieces a subclass sets at 0x9cc to
// 0x9f4; Chiron's names for them: corner sprites, edge tiles) instead of the Win's lines. The four strips of the
// frame (top 0xb4, right 0xb8, left 0xbc, bottom 0xc0) are cleared to the key colour and the pieces drawn on
// them, then the window is told (slot 0xf0) the strip is done. A window with a caption (and without 0x400000) uses
// the caption's set of pieces (9cc, 9d4, 9e8: the top left corner, top right corner and the tile between them)
// and the caption's height, any other window the border's (9ec, 9f0, 9f4); the bottom and the sides share the
// pieces at 9d0 and 9d8 (bottom corners) and 9dc and 9e0 (the tiles of the left and right sides) and 9e4 (the
// bottom tile). Without the bit it is the Win's frame (win_on_redraw_nc).
enum {
    GW_TL = 0x9cc, GW_BL = 0x9d0, GW_TR = 0x9d4, GW_BR = 0x9d8, GW_LEFT_TILE = 0x9dc, GW_RIGHT_TILE = 0x9e0,
    GW_BOTTOM_TILE = 0x9e4, GW_TOP_TILE = 0x9e8, GW_TL_B = 0x9ec, GW_TR_B = 0x9f0, GW_TOP_TILE_B = 0x9f4,
    GW_TITLE_HOOK = 0xa0c, VT_NC_TITLE = 0x164, GW_VT_DONE = 0xf0, WIN_F_SPRITE_FRAME = 0x10000000, WIN_F_NO_CAPTION_PIECES = 0x400000
};
#define GW_SPRITE(w, off) (*(struct game_sprite **)((uint8_t *)(w) + (off)))

static void gw_nc_draw(struct game_buffer *b, struct game_sprite *s, int x, int y) {
    sprite_draw_2(s, b, s->key, x, y, 1, 1);
}

void WIN_THIS graphic_win_on_redraw_nc(struct game_win *w, const int32_t *rect, int which) {
    if (!(w->flags2 & WIN_F2_FRAME)) return;
    if (rect) which = -1;
    int caption = (w->flags & WIN_F_CAPTION) && !(w->flags & WIN_F_NO_CAPTION_PIECES);
    int top = caption ? w->caption_h : w->border;
    struct game_sprite *a = GW_SPRITE(w, caption ? GW_TL : GW_TL_B), *tile = GW_SPRITE(w, caption ? GW_TOP_TILE : GW_TOP_TILE_B),
                       *c = GW_SPRITE(w, caption ? GW_TR : GW_TR_B);
    int bottom = w->bottom == -1 ? w->border : w->bottom;
    if (!(w->flags & WIN_F_SPRITE_FRAME)) {
        win_on_redraw_nc(w, rect, which);
        return;
    }
    struct game_sprite *d = GW_SPRITE(w, GW_BL), *e = GW_SPRITE(w, GW_BR);
    struct game_buffer *b4 = w->nc_buf_b4, *b8 = w->nc_buf_b8, *bc = w->nc_buf_bc, *c0 = w->nc_buf_c0;
    if (b4 && (which == -1 || which == 0)) {
        buffer_fill_all(b4, *game_default_key);
        if (a) gw_nc_draw(b4, a, 0, 0);
        if (c) gw_nc_draw(b4, c, b4->width - c->full_w, 0);
        if (tile && c && a)
            buffer_tile_2(b4, tile, 0, 0, a->full_w, 0, (w->outer[2] - w->outer[0]) - c->full_w - a->full_w, -b4->neg_height);
        if (w->title && (w->flags & WIN_F_CAPTION)) {
            void(__attribute__((cdecl)) * hook)(struct game_win *, const char *, struct game_buffer *) =
                *(void **)((uint8_t *)w + GW_TITLE_HOOK);
            if (hook) hook(w, w->title, b4);
            vcall2(w, VT_NC_TITLE, (int)(uintptr_t)w->title, (int)(uintptr_t)b4);
        }
        vcall2(w, GW_VT_DONE, (int)(uintptr_t)b4, 0);
    }
    if (c0 && (which == -1 || which == 2)) {
        buffer_fill_all(c0, *game_default_key);
        if (d) gw_nc_draw(c0, d, 0, -(d->full_h + c0->neg_height));
        if (e) gw_nc_draw(c0, e, c0->width - e->full_w, -(e->full_h + c0->neg_height));
        struct game_sprite *f = GW_SPRITE(w, GW_BOTTOM_TILE);
        if (f && d && e)
            buffer_tile_2(c0, f, 0, 0, d->full_w, -(c0->neg_height + f->full_h), c0->width - e->full_w - d->full_w, f->full_h);
        vcall2(w, GW_VT_DONE, (int)(uintptr_t)c0, 2);
    }
    if (bc && (which == -1 || which == 3)) {
        buffer_fill_all(bc, *game_default_key);
        if (a) gw_nc_draw(bc, a, 0, -top);
        if (d) gw_nc_draw(bc, d, 0, bottom - bc->neg_height - d->full_h);
        struct game_sprite *g = GW_SPRITE(w, GW_LEFT_TILE);
        if (g && a && d)
            buffer_tile_2(bc, g, 0, 0, 0, a->full_h - top, w->border, bottom - bc->neg_height - d->full_h - a->full_h + top);
        vcall2(w, GW_VT_DONE, (int)(uintptr_t)bc, 3);
    }
    if (b8 && (which == -1 || which == 1)) {
        buffer_fill_all(b8, *game_default_key);
        if (c) gw_nc_draw(b8, c, b8->width - c->full_w, -top);
        if (e) gw_nc_draw(b8, e, b8->width - e->full_w, bottom - b8->neg_height - e->full_h);
        struct game_sprite *h = GW_SPRITE(w, GW_RIGHT_TILE);
        if (h && c && e)
            buffer_tile_2(b8, h, 0, 0, b8->width - h->full_w, c->full_h - top, h->full_w,
                          bottom - b8->neg_height - e->full_h - c->full_h + top);
        vcall2(w, GW_VT_DONE, (int)(uintptr_t)b8, 1);
    }
}

static void(THISCALL *orig_graphic_win_on_paint)(struct game_win *w, const int32_t *clip);
static struct hook_stats stats_graphic_win_on_paint = {"graphic_win_on_paint"};

// The clip: a window hanging from a parent is cut to the parent's client area (its nonclient area, with
// WIN_F_PARENT_INNER, taken from the origin) in screen coordinates and to `clip`; the draw goes to the
// back buffer or, while the draw is redirected, to the other Buffer, shifted by the redirection's offset. A
// window that is not a child gives the Buffer's clip the area asked for (always on the back buffer, even
// while the draw is redirected). A Buffer on a DirectDraw surface is left to the original.
void WIN_THIS graphic_win_on_paint(struct game_win *w, const int32_t *clip) {
    struct game_buffer *own = GW_BUFFER(w);
    if (own->dd) {
        orig_graphic_win_on_paint(w, clip);
        return;
    }
    int32_t ox = 0, oy = 0;
    win_client_to_screen_2(w, &ox, &oy);
    struct game_buffer *alt = *game_win_back_alt;
    struct game_win *parent = w->parent;
    if (parent && (w->flags & WIN_F_CHILD)) {
        int32_t r[4];
        if (w->flags & WIN_F_PARENT_INNER) {
            r[0] = 0, r[1] = 0, r[2] = parent->outer[2] - parent->outer[0], r[3] = parent->outer[3] - parent->outer[1];
            win_nonclient_to_screen(parent, r);
        } else {
            r[0] = 0, r[1] = 0, r[2] = parent->inner[2] - parent->inner[0], r[3] = parent->inner[3] - parent->inner[1];
            win_client_to_screen(parent, r);
        }
        if (clip && !game_intersect_rect(r, r, clip)) return;
        if (alt) {
            r[0] -= game_win_back_off[0], r[2] -= game_win_back_off[0];
            r[1] -= game_win_back_off[1], r[3] -= game_win_back_off[1];
            buffer_set_clip(alt, r);
        } else {
            buffer_set_clip(game_win_back, r);
        }
    } else if (clip) {
        buffer_set_clip(game_win_back, clip);
    }
    struct game_buffer *dst = alt ? alt : game_win_back;
    int x = alt ? ox - game_win_back_off[0] : ox, y = alt ? oy - game_win_back_off[1] : oy;
    int key = *game_default_key;
    if (w->flags & GW_KEYED) {
        uint8_t *a = GW_TABLE(w, GW_TABLE_A), *b = GW_TABLE(w, GW_TABLE_B);
        if (a) buffer_draw_dest_2(own, dst, key, x, y, 1, 1, a);
        else if (b) buffer_draw_multi_table_dest(own, dst, key, x, y, 1, 1, b);
        else buffer_draw_2(own, dst, key, x, y, 1, 1);
    } else {
        buffer_copy_full(own, dst, 0, 0, x, y, w->inner[2] - w->inner[0], w->inner[3] - w->inner[1]);
    }
    buffer_set_clip(dst, dst->full);
}

// Tell every shown window to redraw (slot 0xf8), front to back.
void win_redraw(void) {
    for (int i = 0; i < *game_win_z_n; i++) vcall0(game_win_zorder[i], VT_SCROLL_CHANGED);
}

// Rebuild every shown window's nonclient area (0x128), and flush those that keep one (0xfc).
void win_update(void) {
    for (int i = 0; i < *game_win_z_n; i++) {
        struct game_win *w = game_win_zorder[i];
        vcall2(w, VT_NC_UPDATE, 0, -1);
        w = game_win_zorder[i];
        if (vcall0(w, VT_HAS_NC)) vcall0(game_win_zorder[i], VT_FLUSH);
    }
}

// ---- The nonclient area ------------------------------------------------------------------------
//
// A window with a frame keeps its frame in four Buffers: the top (caption) strip at 0xb4, the
// bottom one at 0xc0, the left one at 0xbc and the right one at 0xb8, which fit round the client
// area. They are made by win_set_rects, drawn by win_on_redraw_nc and, as the window is drawn
// on the screen, copied into the back buffer by win_on_nc_paint. All of these make and free
// Windows objects (a Buffer's DIB, its clip region) or use their results, so their game calls
// run the original in shadow mode and the sweep checks Chiron's code on twin windows.

typedef struct game_buffer buffer_t;
enum { WIN_F_TITLE_LEFT = 0x10000, WIN_F_TITLE_RIGHT = 0x40000, VT_NC_DONE = 0xf0 };

// The height of the top strip (the caption's, unless the window is thin or has none) and the bottom one.
static int nc_top(const struct game_win *w) {
    return (w->flags & WIN_F_CAPTION) && !(w->flags & WIN_F_THIN) ? w->caption_h : w->border;
}
static int nc_bottom(const struct game_win *w) { return w->bottom == -1 ? w->border : w->bottom; }

// Make the four buffers the size the frame needs (the strips along the top and bottom as wide
// as the window, those at the sides as wide as the border and what is left of the height), clear
// them to the transparent colour, and have the window draw its frame into them (slot 0x10c).
void WIN_THIS win_set_rects(struct game_win *w) {
    if (!(w->flags2 & WIN_F2_FRAME)) return;
    int top = nc_top(w), bottom = nc_bottom(w);
    int width = w->outer[2] - w->outer[0], height = w->outer[3] - w->outer[1];
    buffer_t *bufs[4] = {w->nc_buf_b4, w->nc_buf_c0, w->nc_buf_bc, w->nc_buf_b8};
    int bw[4] = {width, width, w->border, w->border};
    int bh[4] = {top, bottom, height - bottom - top, height - bottom - top};
    for (int i = 0; i < 4; i++) {
        buffer_init(bufs[i], bw[i], bh[i], 0, NULL);
        buffer_sync_to_palette(bufs[i], (struct palette *)*game_palette_copy);
        buffer_fill_all(bufs[i], *game_default_key);
    }
    vcall2(w, VT_NC_REBUILD, 0, -1);
}

// Draw the lines of a frame in a strip: a rectangle (cut to the strip), shrunk by a pixel on each
// side (`inward`) or grown by one, as many times as the window's line count, the colours in the
// order given.
static void nc_lines(struct game_win *w, buffer_t *b, int32_t r[4], int c1, int c2, int inward) {
    for (int i = 0; i < w->cap_btn; i++) {
        buffer_box_rect(b, r, c1, c2);
        int d = inward ? 1 : -1;
        r[0] += d, r[1] += d, r[2] -= d, r[3] -= d;
    }
}

// Redraw one strip of the frame (`which`: top 0, right 1, bottom 2, left 3): the part of the
// area `clip` (in the strip's own coordinates) that the strip covers is filled from the window's
// tile picture (from (tx, ty) in it) or its colour, framed with two sets of lines (`r1`, then `r2`
// with the colours the other way round), and for the top strip the title written; then the
// window is told (slot 0xf0).
static void nc_strip(struct game_win *w, buffer_t *b, int which, int32_t clip[4], int tx, int ty, int32_t r1[4], int32_t r2[4],
                     int width, int height, int top) {
    if (intersect(clip, clip, b->full)) {
        buffer_set_clip(b, clip);
        if (w->nc_tile) buffer_tile(b, w->nc_tile, tx, ty, 0, 0, width, height);
        else buffer_fill_all(b, w->nc_color);
        nc_lines(w, b, r1, w->box_color[0], w->box_color[1], 1);
        nc_lines(w, b, r2, w->box_color[1], w->box_color[0], 0);
        if (which == 0 && w->title) {
            int cap = w->cap_btn;
            int32_t tr[4] = {cap, cap, width - cap, top - cap};
            buffer_set_text_color(b, w->text_color[0], w->text_color[1], w->text_color[2], w->text_color[3]);
            buffer_set_font(b, (struct font *)w->nc_font, NULL, NULL, NULL);
            buffer_set_clip(b, tr);
            if (w->flags & WIN_F_TITLE_LEFT) buffer_write_l_4(b, w->title, tr, (int)strlen(w->title));
            else if (w->flags & WIN_F_TITLE_RIGHT) buffer_write_right_l_6(b, w->title, tr, (int)strlen(w->title));
            else buffer_write_cent_l_7(b, w->title, tr, (int)strlen(w->title));
        }
        buffer_set_clip(b, b->full);
    }
    vcall2(w, VT_NC_DONE, (int)(uintptr_t)b, which);
}

// Draw the frame of a window into its four buffers: every one, or the strip `which` (top 0,
// right 1, bottom 2, left 3), and (if `rect` is given, which is in screen coordinates and means
// every strip) just the part of them inside it. Needs a window with a frame and a caption or
// bit 0. The strips' lines count from where the window's rectangle puts them (width and height
// its size, top and bot the strips' heights): the top one is a frame of the whole window, the
// bottom one the same seen from its bottom edge and so on, and the title is written in the top
// one, in the window's font and colours, left, right or centred by the flags.
void WIN_THIS win_on_redraw_nc(struct game_win *w, const int32_t *rect, int which) {
    if (!(w->flags2 & WIN_F2_FRAME) || !(w->flags & (WIN_F_CAPTION | 1))) return;
    int ox = 0, oy = 0;
    if (rect) which = -1, ox = w->outer[0], oy = w->outer[1];
    int top = nc_top(w), bot = nc_bottom(w);
    int width = w->outer[2] - w->outer[0], height = w->outer[3] - w->outer[1];
    int border = w->border, inner_w = w->inner[2] - w->inner[0];
    int side = height - bot;  // where the bottom strip starts
    // The ends of the second frame lines.
    int e_l = border - 1, e_t = top - 1, e_r = inner_w + border + 1, e_b = side + 1;
    int32_t clip[4], r1[4], r2[4];
    // A strip's clip area: the whole strip, or the rectangle asked for moved by (dx, dy).
#define STRIP_CLIP(buf, dx, dy)                                                                           \
    do {                                                                                                  \
        if (rect) clip[0] = rect[0] + (dx), clip[1] = rect[1] + (dy), clip[2] = rect[2] + (dx), clip[3] = rect[3] + (dy); \
        else memcpy(clip, (buf)->full, 16);                                                               \
    } while (0)
    if (w->nc_buf_b4 && (which == -1 || which == 0)) {
        STRIP_CLIP((buffer_t *)w->nc_buf_b4, -ox, -oy);
        r1[0] = 0, r1[1] = 0, r1[2] = width, r1[3] = height;
        r2[0] = e_l, r2[1] = e_t, r2[2] = e_r, r2[3] = e_b;
        nc_strip(w, w->nc_buf_b4, 0, clip, 0, 0, r1, r2, width, height, top);
    }
    if (w->nc_buf_c0 && (which == -1 || which == 2)) {
        STRIP_CLIP((buffer_t *)w->nc_buf_c0, -ox, side - oy);
        r1[0] = 0, r1[1] = bot - height, r1[2] = width, r1[3] = bot;
        r2[0] = e_l, r2[1] = e_t + bot - height, r2[2] = e_r, r2[3] = e_b + bot - height;
        nc_strip(w, w->nc_buf_c0, 2, clip, 0, side, r1, r2, width, height, top);
    }
    if (w->nc_buf_bc && (which == -1 || which == 3)) {
        STRIP_CLIP((buffer_t *)w->nc_buf_bc, -ox, top - oy);
        r1[0] = 0, r1[1] = -top, r1[2] = width, r1[3] = height - top;
        r2[0] = e_l, r2[1] = -1, r2[2] = e_r, r2[3] = e_b - top;
        nc_strip(w, w->nc_buf_bc, 3, clip, 0, top, r1, r2, width, height, top);
    }
    if (w->nc_buf_b8 && (which == -1 || which == 1)) {
        STRIP_CLIP((buffer_t *)w->nc_buf_b8, width - border - ox, top - oy);
        r1[0] = border - width, r1[1] = -top, r1[2] = border, r1[3] = height - top;
        r2[0] = border + e_l - width, r2[1] = -1, r2[2] = border + e_r - width, r2[3] = e_b - top;
        nc_strip(w, w->nc_buf_b8, 1, clip, width - border, top, r1, r2, width, height, top);
    }
#undef STRIP_CLIP
}

// Copy the window's frame strips into `dest`, a Buffer whose top left corner is at (x, y) on the
// screen, each where it lies (cut to dest, which the screen position of each decides), so it
// can be shown or saved; and for a window that draws its frame itself (slot 0xf4), its own picture
// (its Buffer at 0x444) where the game's window is. Buffer_copy_full copies out of the Buffer
// it is called on into the other one.
void WIN_THIS win_draw_nc_to(struct game_win *w, buffer_t *dest, int x, int y) {
    if (!dest) return;
    int32_t R[4] = {x, y, x + dest->width, y - dest->neg_height};
    if (rect_empty(R)) return;
    int32_t cr[4], I[4];
#define GRAB(buf, dy_expr, dx_expr)                                                                                    \
    do {                                                                                                               \
        buffer_t *b_ = (buf);                                                                                          \
        int dx_ = (dx_expr), dy_ = (dy_expr);                                                                          \
        cr[0] = b_->full[0] + dx_, cr[1] = b_->full[1] + dy_, cr[2] = b_->full[2] + dx_, cr[3] = b_->full[3] + dy_;  \
        if (intersect(I, cr, R))                                                                                       \
            buffer_copy_full(b_, dest, I[0] - cr[0], I[1] - cr[1], I[0] - R[0], I[1] - R[1], I[2] - I[0], I[3] - I[1]); \
    } while (0)
    if (w->flags2 & WIN_F2_FRAME) {
        int ox = w->outer[0], oy = w->outer[1];
        buffer_t *b4 = w->nc_buf_b4;
        GRAB(b4, oy, ox);
        GRAB(w->nc_buf_bc, oy - b4->neg_height, ox);
        GRAB(w->nc_buf_c0, w->outer[3] + ((buffer_t *)w->nc_buf_c0)->neg_height, ox);
        GRAB(w->nc_buf_b8, oy - b4->neg_height, w->outer[2] - ((buffer_t *)w->nc_buf_b8)->width);
    }
#undef GRAB
    if (vcall0(w, VT_HAS_NC)) {
        int32_t WR[4] = {0, 0, 0, 0};
        game_get_window_rect(*game_win_hwnd, WR);
        if (intersect(I, WR, R))
            buffer_copy_full(hit_buffer(w), dest, I[0] - WR[0], I[1] - WR[1], I[0] - R[0], I[1] - R[1], I[2] - I[0], I[3] - I[1]);
    }
}

// Copy a window's frame strips (all of them, or the one `which` names) into the back buffer, or
// into the other Buffer a draw is going to (back_alt, with the offset it is at), at the window's
// place on the screen. `rect` (screen coordinates) cuts the back buffer's drawing to it, and
// means every strip. Nothing happens for a window that isn't shown (or has no caption and no bit
// 0); the back buffer is left clipped to all of it.
void WIN_THIS win_on_nc_paint(struct game_win *w, const int32_t *rect, int which) {
    if (!(w->flags & (WIN_F_CAPTION | 1)) || !(w->flags2 & WIN_F2_VISIBLE)) return;
    if (w->parent && !win_is_visible(w->parent)) return;
    int32_t x = 0, y = 0;
    win_client_to_screen_2(w, &x, &y);
    x -= w->inner[0], y -= w->inner[1];
    if (rect) which = -1, buffer_set_clip(game_win_back, rect);
    int top = nc_top(w), border = w->border;
    int width = w->outer[2] - w->outer[0], height = w->outer[3] - w->outer[1];
    int key = *game_default_key;
    // Where the strip goes: its offset from the window's corner, less where the other Buffer is.
#define DRAW(buf, ex, ey)                                                                                              \
    do {                                                                                                               \
        struct game_buffer *alt_ = *game_win_back_alt;                                                                 \
        if (alt_) buffer_draw_2((buf), alt_, key, (ex)-game_win_back_off[0] + x, (ey)-game_win_back_off[1] + y, 1, 1);  \
        else buffer_draw_2((buf), game_win_back, key, (ex) + x, (ey) + y, 1, 1);                                       \
    } while (0)
    if (w->nc_buf_b4 && (which == -1 || which == 0)) DRAW(w->nc_buf_b4, 0, 0);
    if (w->nc_buf_c0 && (which == -1 || which == 2)) DRAW(w->nc_buf_c0, 0, height - nc_bottom(w));
    if (w->nc_buf_bc && (which == -1 || which == 3)) DRAW(w->nc_buf_bc, 0, top);
    if (w->nc_buf_b8 && (which == -1 || which == 1)) DRAW(w->nc_buf_b8, width - border, top);
#undef DRAW
    buffer_set_clip(game_win_back, game_win_back->full);
}

// ---- Mouse and key events --------------------------------------------------------------------
//
// How input reaches a window. The game's window procedure finds the window under the mouse (or
// the one that has the keys) and calls the virtual function of it that takes the raw event
// (slots 0x138 to 0x150: key, char, system key, the buttons going down and up); those (the
// win_on_* functions below) keep the mouse state, and call the one-event functions
// (win_left_down_event and the rest), which call the program's own function for the event. The
// subclass's handler is a virtual function (slots 0x3c to 0x100); an event first goes to a
// plain callback a window may have (0x408 to 0x434, called with the window made the current
// one, in the global `cur`), and the object the window has for the event (0x1c to 0x6c) is let
// go after it: it is asked if it is to be kept (slot 0x5c) and freed through slot 0x1c if not.
// A window that is disabled (WIN_F_DISABLED) or busy (WIN_F2_BUSY) takes no event.
//
// A mouse event comes in two kinds: for the client area, or (`nc` set) for the frame, which has
// slots and objects of its own and no callback.
//
// The functions below call the virtual functions of the game's own windows, which draw and open
// windows, so in shadow mode the game's calls run the original, and the sweep checks Chiron's
// code over scratch windows whose virtual functions, callbacks and hooks log what they get.
//
// Differences from OpenSMACX and Thinker: they have none of this part of the class laid out (the
// slots are named by the vtable of BaseWin, whose handlers are named there: 0x4c on_left_click,
// 0x50 on_left_down, 0x54 on_left_up, 0x58 on_right_click, 0x64 on_key_click, 0x70
// on_left_double_click). Where the original would read through a null dialog window (a dialog
// list with a count but no window) it crashes; Chiron's code leaves that part out.

enum {
    WIN_F_NC_HIT = 0x4000000,  // the frame has a hit test of its own (slot 0x12c) that decides what a click there is
    WIN_F_NO_DBL = 0x200200,   // a disabled window, and one that takes no double click
    // The virtual functions called here, by what they are told.
    VT_KEY_CLICK = 0x64,
    VT_KEY_DOWN = 0x68,
    VT_KEY_UP = 0x6c,
    VT_DEFAULT_BUTTON = 0xb0,  // told a key that stands for a button: -1 for return, -2 for escape
    VT_PASS_FOCUS = 0x100,
    VT_SET_FOCUS = 0x104,
    VT_NC_HIT = 0x12c,
    VT_OWN_REGIONS = 0xf4,  // whether the window keeps a list of regions at 0x8f4 to click in
    VT_ON_KEY = 0x138,
    VT_ON_CHAR = 0x13c,
    VT_ON_SYS_KEY = 0x140,
    VT_ON_L_DOWN = 0x144,
    VT_ON_L_UP = 0x148,
    VT_ON_R_DOWN = 0x14c,
    VT_ON_R_UP = 0x150,
    VT_REGION_CLICK = 0x160,
};
// What a mouse event calls: the callback, the slot and the object let go after, for the client
// area and for the frame.
struct mouse_ev {
    int cb, slot, obj, slot_nc, obj_nc;
};
static const struct mouse_ev EV_MOVE = {0x408, 0x44, 0x1c, 0x7c, 0x4c}, EV_L_DBL = {0x40c, 0x70, 0x44, 0x98, 0x68},
                             EV_R_DBL = {0x410, 0x74, 0x48, 0x9c, 0x6c}, EV_L_CLICK = {0x414, 0x4c, 0x20, 0x80, 0x50},
                             EV_L_DOWN = {0x418, 0x50, 0x24, 0x84, 0x54}, EV_L_UP = {0x41c, 0x54, 0x28, 0x88, 0x58},
                             EV_R_CLICK = {0x420, 0x58, 0x2c, 0x8c, 0x5c}, EV_R_DOWN = {0x424, 0x5c, 0x30, 0x90, 0x60},
                             EV_R_UP = {0x428, 0x60, 0x34, 0x94, 0x64};
enum { CB_KEY_CLICK = 0x42c, CB_KEY_DOWN = 0x430, CB_KEY_UP = 0x434, OBJ_KEY_CLICK = 0x38, OBJ_KEY_DOWN = 0x3c, OBJ_KEY_UP = 0x40 };

static void *field_at(const struct game_win *w, int off) { return *(void **)((const uint8_t *)w + off); }
typedef void (*cb2_t)(int, int);
typedef int (*cbi_t)(int);
typedef int (*cbi2_t)(int, int);
typedef void (*hook2_t)(int, int);
typedef void (*hook1_t)(int);
typedef void (*hook0_t)(void);

static int vcall4(struct game_win *w, int slot, int a, int b, int c, int d) {
    return ((int(THISCALL *)(struct game_win *, int, int, int, int))game_win_slot(w, slot))(w, a, b, c, d);
}
static int vcall5(struct game_win *w, int slot, int a, int b, int c, int d, int e) {
    return ((int(THISCALL *)(struct game_win *, int, int, int, int, int))game_win_slot(w, slot))(w, a, b, c, d, e);
}

// Let go of the object a window made for an event: kept if it says so (slot 0x5c), otherwise freed (0x1c).
static void let_go(void *obj) {
    if (!obj) return;
    if (!vcall0(obj, 0x5c)) vcall0(obj, 0x1c);
}
static int takes_no_events(const struct game_win *w, uint32_t flags_mask) {
    return (w->flags & flags_mask) || (w->flags2 & WIN_F2_BUSY);
}
static void hook_x_y(int index, int x, int y) {
    void *h = game_win_hooks[index];
    if (h) ((hook2_t)h)(x, y);
}
static void hook_after_input(void) {
    void *h = game_win_hooks[0];
    if (h) ((hook0_t)h)();
}

// A mouse event: for the client area the window is made the current one, its callback is called
// (with x and y), then the handler and last the object the window has for it is let go; for the
// frame there is no callback.
static void mouse_event(struct game_win *w, uint32_t mask, int x, int y, int nc, const struct mouse_ev *e) {
    if (takes_no_events(w, mask)) return;
    if (!nc) {
        *game_win_cur = w;
        void *f = field_at(w, e->cb);
        if (f) ((cb2_t)f)(x, y);
        vcall2(w, e->slot, x, y);
        let_go(field_at(w, e->obj));
    } else {
        vcall2(w, e->slot_nc, x, y);
        let_go(field_at(w, e->obj_nc));
    }
}

// The left button went down: as the others, but the game's hook for it (if set) is told after the
// callback (and for the frame too), before the handler.
void WIN_THIS win_left_down_event(struct game_win *w, int x, int y, int nc) {
    if (takes_no_events(w, WIN_F_DISABLED)) return;
    const struct mouse_ev *e = &EV_L_DOWN;
    if (!nc) {
        *game_win_cur = w;
        void *f = field_at(w, e->cb);
        if (f) ((cb2_t)f)(x, y);
    }
    hook_x_y(2, x, y);
    if (!nc) {
        vcall2(w, e->slot, x, y);
        let_go(field_at(w, e->obj));
    } else {
        vcall2(w, e->slot_nc, x, y);
        let_go(field_at(w, e->obj_nc));
    }
}
void WIN_THIS win_left_up_event(struct game_win *w, int x, int y, int nc) { mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_L_UP); }
void WIN_THIS win_right_click_event(struct game_win *w, int x, int y, int nc) {
    mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_R_CLICK);
}
void WIN_THIS win_right_down_event(struct game_win *w, int x, int y, int nc) {
    mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_R_DOWN);
}
void WIN_THIS win_right_up_event(struct game_win *w, int x, int y, int nc) { mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_R_UP); }

// The window a dialog list's focus is on (none for an empty list).
static struct game_win *dialog_cur(const struct game_win *w) { return w->dlg_head ? w->dlg_cur->win : NULL; }
// Whether a key code is not one that makes a character (MapVirtualKey to a character answers 0;
// only the low word counts, as the original tests the 16-bit register).
static int key_is_no_char(int key) { return (uint16_t)game_map_virtual_key((unsigned)key & 0xfff8ffffu, 2) == 0; }

// A key that acts as a click (enter, escape, tab and the others the window uses). Return and
// escape (and the enter key of the number pad) tell the window that its default or cancel button
// was pressed (slot 0xb0). In a dialog the key goes to the focus window first (tab passes the
// focus on; for any other key, a window that uses it says so by answering nonzero, which ends it);
// then the callback at 0x42c and the handler (slot 0x64) are called with (a, key) and what they
// answer is added up.
int WIN_THIS win_key_click_event(struct game_win *w, int a, int key) {
    if (takes_no_events(w, WIN_F_DISABLED)) return 0;
    if (key == 0xd || key == 0x1000d) vcall1(w, VT_DEFAULT_BUTTON, -1);
    else if (key == 0x1b) vcall1(w, VT_DEFAULT_BUTTON, -2);
    if (w->dlg_count) {
        struct game_win *cur = dialog_cur(w);
        if (key == 9) {
            if (cur) vcall0(cur, VT_PASS_FOCUS);
        } else if (cur && win_key_click_event(cur, a, key)) {
            return 1;
        }
    }
    int result = 0;
    *game_win_cur = w;
    void *f = field_at(w, CB_KEY_CLICK);
    if (f) result = ((cbi2_t)f)(a, key);
    result += vcall2(w, VT_KEY_CLICK, a, key);
    let_go(field_at(w, OBJ_KEY_CLICK));
    return result;
}

// A key pressed. In a dialog the focus window's scroll bars are offered it first (slot 0x64 with
// (0, key): an answer of nonzero means it was used, which ends it), and a key that makes no
// character is sent to the focus window as a click (once, for its two bars); then the focus
// window gets the key pressed, and if it uses it that ends it. Then the window's own scroll bars
// and the key as a click in the same way (a window with no bar gets the click, and if it uses it that ends it); and last the callback
// (0x430) and the handler (slot 0x68), their answers added.
int WIN_THIS win_key_down_event(struct game_win *w, int key) {
    if (takes_no_events(w, WIN_F_DISABLED)) return 0;
    int sent = 0;
    if (w->dlg_count) {
        struct game_win *cur = dialog_cur(w);
        if (cur) {
            if (cur->scroll_v) {
                if (vcall2(&cur->scroll_v->win, VT_KEY_CLICK, 0, key)) return 1;
                if (key_is_no_char(key)) {
                    win_key_click_event(cur, 0, key);
                    sent = 1;
                }
            }
            if (cur->scroll_h) {
                if (vcall2(&cur->scroll_h->win, VT_KEY_CLICK, 0, key)) return 1;
                if (!sent && key_is_no_char(key)) win_key_click_event(cur, 0, key);
            }
            if (!cur->scroll_h && !cur->scroll_v && key_is_no_char(key)) win_key_click_event(cur, 0, key);
            if (win_key_down_event(cur, key)) return 1;
        }
    }
    if (w->scroll_v) {
        if (vcall2(&w->scroll_v->win, VT_KEY_CLICK, 0, key)) return 1;
        if (!sent && key_is_no_char(key)) {
            win_key_click_event(w, 0, key);
            sent = 1;
        }
    }
    if (w->scroll_h) {
        if (vcall2(&w->scroll_h->win, VT_KEY_CLICK, 0, key)) return 1;
        if (!sent && key_is_no_char(key)) win_key_click_event(w, 0, key);
    }
    if (!w->scroll_h && !w->scroll_v && !sent && key_is_no_char(key) && win_key_click_event(w, 0, key)) return 1;
    int result = 0;
    *game_win_cur = w;
    void *f = field_at(w, CB_KEY_DOWN);
    if (f) result = ((cbi_t)f)(key);
    result += vcall1(w, VT_KEY_DOWN, key);
    let_go(field_at(w, OBJ_KEY_DOWN));
    return result;
}

// A key let go: the focus window of a dialog first (it ends it if it uses it), then the callback
// (0x434) and the handler (slot 0x6c), their answers added.
int WIN_THIS win_key_up_event(struct game_win *w, int key) {
    if (takes_no_events(w, WIN_F_DISABLED)) return 0;
    if (w->dlg_count) {
        struct game_win *cur = dialog_cur(w);
        if (cur && win_key_up_event(cur, key)) return 1;
    }
    int result = 0;
    *game_win_cur = w;
    void *f = field_at(w, CB_KEY_UP);
    if (f) result = ((cbi_t)f)(key);
    result += vcall1(w, VT_KEY_UP, key);
    let_go(field_at(w, OBJ_KEY_UP));
    return result;
}

// ---- The raw events (slots 0x40 and 0x134 to 0x150) --------------------------------------------

// The mouse moved over the window (`keys` is not used).
void WIN_THIS win_on_mouse_move(struct game_win *w, int x, int y, int keys, int nc) {
    mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_MOVE);
}

// The wheel turned by `delta` (its low word, in 120ths of a notch, signed): the handler (slot
// 0x3c, or 0x78 for the frame) is told the notches, rounded towards zero. The key state and the
// mouse's place the system gives are not used.
void WIN_THIS win_on_mouse_wheel(struct game_win *w, int keys, int delta, int x, int y, int nc) {
    int notches = (int16_t)delta / 120;
    if (takes_no_events(w, WIN_F_DISABLED)) return;
    vcall1(w, nc ? 0x78 : 0x3c, notches);
}

// A button went down: the window is brought to the front of those of its parent (or of the
// top-level ones), a window of a dialog list takes the focus (slot 0x104 of its parent), and the
// press is kept: the window, in the slot for the frame or the client area, with `press_inside` 1
// for the left button and 0 for the right.
static void button_pressed(struct game_win *w, int nc, int inside) {
    if (w->parent) win_bring_child_to_top(w->parent, w);
    else win_bring_parent_to_top(w);
    if (w->parent) vcall1(w->parent, VT_SET_FOCUS, (int)(uintptr_t)w);
    if (nc) *game_win_press2 = w, *game_win_press1 = NULL;
    else *game_win_press1 = w, *game_win_press2 = NULL;
    *game_win_press_inside = inside;
}

// The left button went down. A double click (the first number) is its own event (slot 0x70,
// 0x98 for the frame, and a window that is disabled or takes none gets none); a window that
// says (slot 0xf4) it keeps a list of regions at 0x8f4 is asked (slot 0x160) about the region
// under the mouse and the down event isn't made if it answers 0. Then the down event.
void WIN_THIS win_on_l_button_down(struct game_win *w, int dbl, int x, int y, int keys, int nc) {
    button_pressed(w, nc, 1);
    if (dbl) mouse_event(w, WIN_F_NO_DBL, x, y, nc, &EV_L_DBL);
    if (vcall0(w, VT_OWN_REGIONS)) {
        int field14;
        if (game_spot_check((uint8_t *)w + 0x8f4, x, y, &field14, NULL) >= 0) {
            if (!vcall2(w, VT_REGION_CLICK, field14, *(int *)((uint8_t *)w + field14 * 4 + 0x900))) return;
        }
    }
    win_left_down_event(w, x, y, nc);
}

// The left button went up: if it was pressed in this window and is still inside it, a click
// (slot 0x4c, 0x80 for the frame); then the up event if the window and what it hangs from are shown.
void WIN_THIS win_on_l_button_up(struct game_win *w, int x, int y, int keys, int nc) {
    struct game_win *pressed = *game_win_press1 ? *game_win_press1 : *game_win_press2;
    *game_win_press1 = *game_win_press2 = NULL;
    if (pressed == w) {
        int32_t r[4] = {0, 0, w->inner[2] - w->inner[0], w->inner[3] - w->inner[1]};
        if (in_box(x, y, r)) mouse_event(w, WIN_F_DISABLED, x, y, nc, &EV_L_CLICK);
    }
    if (!(w->flags2 & WIN_F2_VISIBLE)) return;
    if (w->parent && !win_is_visible(w->parent)) return;
    win_left_up_event(w, x, y, nc);
}

// The right button went down: kept as the left one is (but with the mouse taken to be outside), a
// double click is its own event (slot 0x74, 0x9c for the frame), then the down event.
void WIN_THIS win_on_r_button_down(struct game_win *w, int dbl, int x, int y, int keys, int nc) {
    button_pressed(w, nc, 0);
    if (dbl) mouse_event(w, WIN_F_NO_DBL, x, y, nc, &EV_R_DBL);
    win_right_down_event(w, x, y, nc);
}

// The right button went up: a click if it was pressed here and is inside, then the up event.
void WIN_THIS win_on_r_button_up(struct game_win *w, int x, int y, int keys, int nc) {
    struct game_win *pressed = *game_win_press1 ? *game_win_press1 : *game_win_press2;
    *game_win_press1 = *game_win_press2 = NULL;
    if (pressed == w) {
        int32_t r[4] = {0, 0, w->inner[2] - w->inner[0], w->inner[3] - w->inner[1]};
        if (in_box(x, y, r)) win_right_click_event(w, x, y, nc);
    }
    win_right_up_event(w, x, y, nc);
}

// A system key (alt and another): posted to the game's window as a key message, and the default
// answer to the system-key message returned. The window itself is not used.
int WIN_THIS win_on_sys_key(struct game_win *w, int vk, int down, int rep, int scan) {
    intptr_t lparam = (intptr_t)(((uint32_t)scan & 0xffff) << 16 | ((uint32_t)rep & 0xffff));
    game_post_message((void *)(uintptr_t)*game_win_hwnd, down ? 0x100 : 0x101, (uintptr_t)vk, lparam);
    return (int)game_def_window_proc(*game_win_hwnd, down ? 0x104 : 0x105, (uintptr_t)vk, lparam);
}

// ---- The game's own calls into the windows (what its window procedure calls) -----------------
//
// These find the window and call its raw event. They are C functions (the caller clears the
// stack) given what the window procedure has: the ones called OnKey, OnChar and OnSysKey a first
// argument that isn't used; those for the buttons a handle (not used) in front of the double-click
// flag where there is one, then the point, then the key state.

// A key: the window that has the keys is asked, then the game's key hook is told (when the key
// went down), then its hook for every input.
void win_OnKey(int unused, int vk, int down, int rep, int scan) {
    struct game_win *k = win_get_key_window();
    if (k) vcall4(k, VT_ON_KEY, vk, down, rep, scan);
    if (game_win_hooks[1] && down) ((hook1_t)game_win_hooks[1])(vk);
    hook_after_input();
}
void win_OnChar(int unused, int ch, int rep) {
    struct game_win *k = win_get_key_window();
    if (k) vcall2(k, VT_ON_CHAR, ch, rep);
}
void win_OnSysKey(int unused, int vk, int down, int rep, int scan) {
    struct game_win *k = win_get_key_window();
    if (k) vcall4(k, VT_ON_SYS_KEY, vk, down, rep, scan);
}

// Whether a hit on a window's frame, by its hit code (slot 0x12c), starts moving or sizing it:
// code 2 (the caption) and 10 to 17 (the edges and corners).
static int starts_tracking(int code) { return code == 2 || (code > 9 && code <= 0x11); }

// The left button went down at a point (the window under it is looked for, and the point's
// numbers become the place in its client area). A window with a frame that has a hit test of its
// own (and any hit on a frame) is asked what was hit; a nonzero answer brings the window to the
// front, and one that moves or sizes it is kept as the window being tracked (with the point and
// the code) and ends the event. Otherwise the window is told (slot 0x144). The game's hooks follow,
// also when no window was there.
void win_OnLButtonDown(int hwnd, int dbl, int x, int y, int keys) {
    struct game_win *w = win_get_mouse_window(&x, &y);
    if (w) {
        int code = 0;
        if (*game_win_hit_nc || (w->flags & WIN_F_NC_HIT)) code = vcall2(w, VT_NC_HIT, x, y);
        if (code) {
            win_bring_to_top(w);
            if (starts_tracking(code)) {
                *game_win_track = w;
                *game_win_track_code = code;
                game_win_track_pos[0] = x, game_win_track_pos[1] = y;
                return;
            }
        }
        vcall5(w, VT_ON_L_DOWN, dbl, x, y, keys, *game_win_hit_nc);
    }
    hook_x_y(2, x, y);
    hook_after_input();
}
// A button going up ends any tracking; the window under the mouse is told.
void win_OnLButtonUp(int hwnd, int x, int y, int keys) {
    *game_win_track = NULL;
    struct game_win *w = win_get_mouse_window(&x, &y);
    if (w) vcall4(w, VT_ON_L_UP, x, y, keys, *game_win_hit_nc);
}
void win_OnRButtonDown(int hwnd, int dbl, int x, int y, int keys) {
    struct game_win *w = win_get_mouse_window(&x, &y);
    if (w) vcall5(w, VT_ON_R_DOWN, dbl, x, y, keys, *game_win_hit_nc);
    hook_x_y(3, x, y);
    hook_after_input();
}
void win_OnRButtonUp(int hwnd, int x, int y, int keys) {
    *game_win_track = NULL;
    struct game_win *w = win_get_mouse_window(&x, &y);
    if (w) vcall4(w, VT_ON_R_UP, x, y, keys, *game_win_hit_nc);
}

// ---- Shadow checks -------------------------------------------------------------------------

typedef struct game_win *winp;
#define SHADOW_RESULT_winp(v) struct game_win *v =
#define SHADOW_INT_winp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_winp(v) return v
#define SHADOW_TAIL_winp(call) return call

#define P(w) ((int)(uintptr_t)(w))

// Memory a call can reach, as spans for WRITES: ints given (NULL for none), the list of
// windows a Win keeps, and the game's lists.
static void writes_ints(struct hook_writes *w, void *p, size_t size) {
    if (p) hooks_writes_add(w, p, size);
}
static int clamp_count(int n, int max) { return n < 0 ? 0 : n > max ? max : n; }
// The z-order list's array (the parent list follows its rect at 0x9b6e38) and what goes with it.
static void writes_zorder(struct hook_writes *w) {
    hooks_writes_add(w, game_win_zorder, 0x808);
    hooks_writes_add(w, game_win_z_n, 4);
    hooks_writes_add(w, game_win_opaque_flag, 4);
}
static void writes_parents(struct hook_writes *w, int extra) {
    hooks_writes_add(w, game_win_parents, (size_t)(clamp_count(*game_win_parent_n, 43) + extra) * 4);
    hooks_writes_add(w, game_win_parent_n, 4);
}
static void writes_children(struct hook_writes *w, struct game_win *p, int extra) {
    hooks_writes_add(w, p->children, (size_t)(clamp_count(p->n_children, 149) + extra) * 4);
    hooks_writes_add(w, &p->n_children, 4);
}
static void writes_modal(struct hook_writes *w) {
    int n = clamp_count(*game_win_modal_n, 60) + 1;
    hooks_writes_add(w, game_win_modals, (size_t)n * 4);
    hooks_writes_add(w, game_win_modal_aux, (size_t)n * 4);
    hooks_writes_add(w, game_win_modal_n, 4);
    hooks_writes_add(w, game_win_modal_prev, 4);
    hooks_writes_add(w, game_win_modal, 4);
}
// What bring_to_top moves in: the window's siblings (the top-level windows if it has no parent).
static void writes_siblings(struct hook_writes *w, struct game_win *x) {
    if (x->parent) writes_children(w, x->parent, 0);
    else writes_parents(w, 0);
    writes_zorder(w);
}

SHADOW_THIS_FN(WIN, int, win_is_visible, (struct game_win * w), (w), "%d", P(w))
SHADOW_THIS_FN(WIN, int, win_is_descendant, (struct game_win * w, struct game_win *o), (w, o), "%d, %d", P(w), P(o))
SHADOW_THIS_FN(WIN, int, win_is_below, (struct game_win * w, struct game_win *o), (w, o), "%d, %d", P(w), P(o))
SHADOW_THIS_FN(WIN, int, win_has_child, (struct game_win * w, struct game_win *o), (w, o), "%d, %d", P(w), P(o))
SHADOW_THIS_FN(WIN, int, win_in_modal, (struct game_win * w), (w), "%d", P(w))
SHADOW_THIS_FN(WIN, int, win_is_dialog_focus, (struct game_win * w), (w), "%d", P(w))
SHADOW_WRITER(WIN, void, win_set_def_focus, (struct game_win * w), (w), WRITES(game_win_def_focus, 4), "%d", P(w))
SHADOW_WRITER(WIN, winp, win_get_key_window, (void), (), WRITES(game_win_key, 4), "%d", 0)
SHADOW_WRITER(WIN, void, win_add_parent, (struct game_win * w), (w), (writes_parents(&w_, 1)), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_add_child, (struct game_win * w, struct game_win *c), (w, c), (writes_children(&w_, w, 1)),
                "%d, %d", P(w), P(c))
SHADOW_WRITER_T(WIN, void, win_release_modal, (struct game_win * w), (w), (writes_modal(&w_)), "%d", P(w))

SHADOW_WRITER(WIN, void, win_recurse_zorder, (struct game_win * w), (w), (writes_zorder(&w_)), "%d", P(w))
SHADOW_WRITER(WIN, void, win_update_zorder, (void), (), (writes_zorder(&w_)), "%d", 0)
SHADOW_WRITER_T(WIN, void, win_bring_to_top, (struct game_win * w), (w), (writes_siblings(&w_, w)), "%d", P(w))
SHADOW_WRITER(WIN, void, win_bring_parent_to_top, (struct game_win * w), (w), (writes_parents(&w_, 0), writes_zorder(&w_)),
              "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_bring_child_to_top, (struct game_win * w, struct game_win *c), (w, c),
                (writes_children(&w_, w, 0), writes_zorder(&w_)), "%d, %d", P(w), P(c))

SHADOW_WRITER_T(WIN, void, win_screen_to_client, (struct game_win * w, int32_t *r), (w, r), writes_ints(&w_, r, 16), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_client_to_screen, (struct game_win * w, int32_t *r), (w, r), writes_ints(&w_, r, 16), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_nonscreen_to_client, (struct game_win * w, int32_t *r), (w, r), writes_ints(&w_, r, 16), "%d",
                P(w))
SHADOW_WRITER_T(WIN, void, win_nonclient_to_screen, (struct game_win * w, int32_t *r), (w, r), writes_ints(&w_, r, 16), "%d",
                P(w))
#define POINT_WRITES (writes_ints(&w_, x, 4), writes_ints(&w_, y, 4))
SHADOW_WRITER_T(WIN, void, win_client_to_screen_2, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_screen_to_client_2, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_nonclient_to_screen_2, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_screen_to_nonclient, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_client_to_nonclient, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_nonclient_to_client, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y), POINT_WRITES,
                "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_client_to_nonclient_2, (struct game_win * w, int32_t *r), (w, r), writes_ints(&w_, r, 16), "%d",
                P(w))
SHADOW_WRITER_T(WIN, int, win_move, (struct game_win * w, int x, int y), (w, x, y), WRITES(w->inner, 32), "%d, %d, %d", P(w), x,
                y)
SHADOW_THIS_FN(WIN, int, win_on_nc_hittest, (struct game_win * w, int x, int y), (w, x, y), "%d, %d, %d", P(w), x, y)

SHADOW_THIS_FN(WIN, int, win_get_vert_pos, (struct game_win * w), (w), "%d", P(w))
SHADOW_THIS_FN(WIN, int, win_get_horz_pos, (struct game_win * w), (w), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_set_vert_paging, (struct game_win * w, int v), (w, v),
                writes_ints(&w_, w->scroll_v ? &w->scroll_v->paging : NULL, 4), "%d, %d", P(w), v)
SHADOW_WRITER_T(WIN, void, win_set_horz_paging, (struct game_win * w, int v), (w, v),
                writes_ints(&w_, w->scroll_h ? &w->scroll_h->paging : NULL, 4), "%d, %d", P(w), v)
#define UNK_WRITES(field) \
    (writes_ints(&w_, w->scroll_v ? &w->scroll_v->field : NULL, 4), writes_ints(&w_, w->scroll_h ? &w->scroll_h->field : NULL, 4))
SHADOW_WRITER_T(WIN, void, win_unk8, (struct game_win * w, int v), (w, v), UNK_WRITES(f2144), "%d, %d", P(w), v)
SHADOW_WRITER_T(WIN, void, win_unk9, (struct game_win * w, int v), (w, v), UNK_WRITES(f2148), "%d, %d", P(w), v)

// What the sweep's stand-in virtual functions leave behind: a running count and a hash of the
// slot, the window and the arguments of each call, so a different call, argument or order in
// Chiron's run than in the original's shows as a difference in these eight bytes. (The game's
// own windows never touch it.)
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }
// A window's rectangles (what win_move changes), and what a resize leaves behind.
static void writes_rects(struct hook_writes *w, struct game_win *x) {
    if (x) hooks_writes_add(w, x->inner, 32);
}
static void writes_redo(struct hook_writes *w, struct game_win *x) {
    writes_rects(w, x->btn_ec ? &x->btn_ec->win : NULL);
    writes_rects(w, x->btn_e8 ? &x->btn_e8->win : NULL);
    writes_rects(w, x->btn_e4 ? &x->btn_e4->win : NULL);
    if (x->btn_e8) hooks_writes_add(w, &x->btn_e8->picture, 4);
}
static void writes_resize_event(struct hook_writes *w, struct game_win *x) {
    hooks_writes_add(w, game_win_cur, 4);
    writes_rects(w, x->scroll_v ? &x->scroll_v->win : NULL);
    writes_rects(w, x->scroll_h ? &x->scroll_h->win : NULL);
    writes_redo(w, x);
    writes_vlog(w);
}
static void writes_scroll(struct hook_writes *w, struct game_win_scroll *s) {
    writes_ints(w, s ? &s->lo : NULL, 16);
    hooks_writes_add(w, game_win_cur, 4);
    writes_vlog(w);
}
static void writes_dialog(struct hook_writes *w, struct game_win *x) {
    if (x) hooks_writes_add(w, &x->dlg_cur, 12);
    hooks_writes_add(w, game_win_prev_focus, 4);
    writes_vlog(w);
}
static void writes_mouse_state(struct hook_writes *w) {
    hooks_writes_add(w, game_win_capture, 4);
    hooks_writes_add(w, game_win_hover, 4);
    hooks_writes_add(w, game_win_press1, 4);
    hooks_writes_add(w, game_win_press2, 4);
    writes_vlog(w);
}

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call

// The lists. The virtual hide a window gets is the original's own.
SHADOW_WRITER(WIN, void, win_remove_parent, (struct game_win * w), (w), (writes_parents(&w_, 0), writes_vlog(&w_)), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_remove_child, (struct game_win * w, struct game_win *c), (w, c),
                (writes_children(&w_, w, 0), writes_vlog(&w_)), "%d, %d", P(w), P(c))

SHADOW_WRITER_T(WIN, void, win_set_vert_pos, (struct game_win * w, int v), (w, v), writes_scroll(&w_, w->scroll_v), "%d, %d",
                P(w), v)
SHADOW_WRITER_T(WIN, void, win_set_horz_pos, (struct game_win * w, int v), (w, v), writes_scroll(&w_, w->scroll_h), "%d, %d",
                P(w), v)
SHADOW_WRITER_T(WIN, void, win_set_vert_range, (struct game_win * w, int a, int b), (w, a, b),
                writes_scroll(&w_, w->scroll_v), "%d, %d, %d", P(w), a, b)
SHADOW_WRITER_T(WIN, void, win_set_horz_range, (struct game_win * w, int a, int b), (w, a, b),
                writes_scroll(&w_, w->scroll_h), "%d, %d, %d", P(w), a, b)

SHADOW_WRITER_T(WIN, void, win_set_caption_height, (struct game_win * w, int v), (w, v),
                (writes_ints(&w_, &w->caption_h, 4), writes_vlog(&w_)), "%d, %d", P(w), v)
SHADOW_WRITER_T(WIN, void, win_set_border_thickness, (struct game_win * w, int v), (w, v),
                (writes_ints(&w_, &w->border, 4), writes_vlog(&w_)), "%d, %d", P(w), v)
SHADOW_WRITER_T(WIN, void, win_set_bottom_border_thickness, (struct game_win * w, int v), (w, v),
                (writes_ints(&w_, &w->bottom, 4), writes_vlog(&w_)), "%d, %d", P(w), v)

SHADOW_WRITER_T(WIN, void, win_set_dialog_focus, (struct game_win * w, struct game_win *f), (w, f), writes_dialog(&w_, w),
                "%d, %d", P(w), P(f))
SHADOW_WRITER_T(WIN, void, win_pass_dialog_focus, (struct game_win * w), (w), writes_dialog(&w_, w->parent), "%d", P(w))

SHADOW_WRITER_T(WIN, int, win_set_mouse_pos, (struct game_win * w, int x, int y), (w, x, y), (void)0, "%d, %d, %d", P(w), x, y)
SHADOW_WRITER_T(WIN, void, win_get_mouse_pos, (struct game_win * w, int32_t *x, int32_t *y), (w, x, y),
                (writes_ints(&w_, x, 4), writes_ints(&w_, y, 4)), "%d", P(w))
SHADOW_WRITER(WIN, int, win_get_lbutton_state, (void), (), (void)0, "%d", 0)
SHADOW_WRITER(WIN, int, win_get_rbutton_state, (void), (), (void)0, "%d", 0)
SHADOW_WRITER_T(WIN, int, win_maximize, (struct game_win * w), (w), writes_mouse_state(&w_), "%d", P(w))
SHADOW_WRITER_T(WIN, int, win_minimize, (struct game_win * w), (w), writes_mouse_state(&w_), "%d", P(w))
SHADOW_WRITER_T(WIN, int, win_show_maximize, (struct game_win * w), (w), writes_mouse_state(&w_), "%d", P(w))

SHADOW_WRITER_T(WIN, int, win_center, (struct game_win * w), (w), WRITES(w->inner, 32), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_nonclient_to_client_2, (struct game_win * w, int32_t *r), (w, r),
                (writes_ints(&w_, r, 16), writes_vlog(&w_)), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_redo_caption_buttons, (struct game_win * w), (w), writes_redo(&w_, w), "%d", P(w))
SHADOW_WRITER_T(WIN, void, win_resize_event, (struct game_win * w, int cw, int ch), (w, cw, ch),
                writes_resize_event(&w_, w), "%d, %d, %d", P(w), cw, ch)
SHADOW_WRITER_T(WIN, int, win_resize, (struct game_win * w, int cx, int cy, int client), (w, cx, cy, client),
                (WRITES(w->inner, 32), writes_resize_event(&w_, w)), "%d, %d, %d, %d", P(w), cx, cy, client)
SHADOW_WRITER_T(WIN, voidp, win_redraw_nc_buffer, (struct game_win * w, unsigned which), (w, which), writes_vlog(&w_), "%d, %d",
                P(w), (int)which)
SHADOW_WRITER_T(WIN, int, win_update_nc_buffer, (struct game_win * w, int what), (w, what), writes_vlog(&w_), "%d, %d", P(w),
                what)


// ---- Drawing: checks -----------------------------------------------------------------------
//
// The mouse's window writes only a few globals (the buffers it reads pixels from are left as they
// were), so it is checked in the game too. The functions that draw the screen call the virtual
// functions of the game's own windows, which draw with Buffers' clip regions, so in shadow mode
// the game's calls run the original, and the sweep checks Chiron's code over stand-in windows: the
// back buffer's pixels it can reach and the Buffers' fields are put back and compared. The frame's
// functions (win_set_rects and the rest) make and free Windows objects and are checked on twins.

SHADOW_WRITER(WIN, winp, win_get_mouse_window, (int32_t * px, int32_t *py), (px, py),
              (writes_ints(&w_, px, 4), writes_ints(&w_, py, 4), WRITES(game_win_mouse_pos, 8), WRITES(game_win_hit_nc, 4),
               WRITES(game_win_capture, 4), WRITES(game_win_hover, 4), WRITES(game_win_press1, 4), WRITES(game_win_press2, 4)),
              "%d, %d", px ? *px : 0, py ? *py : 0)

// The rows of the back buffer from `t` to `bt`, whole (cut to the buffer): what a draw into an area can write.
static void writes_back_rows(struct hook_writes *w, int t, int bt) {
    struct game_buffer *b = game_win_back;
    if (!b->bits || b->dd) return;
    int rows = -b->neg_height;
    if (t < 0) t = 0;
    if (bt > rows) bt = rows;
    if (t >= bt) return;
    uint8_t *a = b->bits + (intptr_t)b->pitch * t, *c = b->bits + (intptr_t)b->pitch * (bt - 1);
    size_t n = (size_t)(b->pitch < 0 ? -b->pitch : b->pitch) * (size_t)(bt - t);
    hooks_writes_add(w, a < c ? a : c, n);
}
// What drawing the screen writes: the back buffer's rows for the area, the overlay's pixels (what it
// keeps of what is under it), both Buffers' objects, the scratch the fills and copies leave, and what the
// stand-in virtual functions record.
static void writes_screen(struct hook_writes *w, const int32_t *rect) {
    writes_vlog(w);
    hooks_writes_add(w, game_win_back, sizeof *game_win_back);
    hooks_writes_add(w, game_win_overlay, sizeof *game_win_overlay);
    if (game_win_overlay->bits && !game_win_overlay->dd && game_win_overlay->neg_height < 0)  // the overlay's own pixels: what it keeps
        hooks_writes_add(w, (uint8_t *)game_win_overlay->bits - (game_win_overlay->pitch < 0 ? (intptr_t)-game_win_overlay->pitch * (-game_win_overlay->neg_height - 1) : 0),
                         (size_t)(game_win_overlay->pitch < 0 ? -game_win_overlay->pitch : game_win_overlay->pitch) * (size_t)-game_win_overlay->neg_height);
    hooks_writes_add(w, game_fill_gap, 4);
    hooks_writes_add(w, game_copy_rem, 4);
    hooks_writes_add(w, game_mask_units, 4);
    hooks_writes_add(w, game_mask_gap_a, 4);
    hooks_writes_add(w, game_mask_gap_b, 4);
    int rows = -game_win_back->neg_height;
    writes_back_rows(w, rect ? rect[1] : 0, rect ? rect[3] : rows);
}

SHADOW_DECLARE(void, win_update_screen, (const int32_t *rect, struct game_win *win))
SHADOW_WRITER_BODY(void, win_update_screen, (const int32_t *rect, struct game_win *win), (rect, win), writes_screen(&w_, rect),
                   "%d", P(win))
static void shadow_win_update_screen(const int32_t *rect, struct game_win *win) {
    if (!hooks_nested && !shadow_bypass()) stats_win_update_screen.calls++, shadow_tick(WIN);
    orig_win_update_screen(rect, win);
}
SHADOW_DECLARE_T(void, win_update_window, (struct game_win * w, const int32_t *rect))
SHADOW_WRITER_BODY(void, win_update_window, (struct game_win * w, const int32_t *rect), (w, rect), writes_screen(&w_, NULL), "%d",
                   P(w))
static void THISCALL shadow_win_update_window(struct game_win *w, const int32_t *rect) {
    if (!hooks_nested && !shadow_bypass()) stats_win_update_window.calls++, shadow_tick(WIN);
    orig_win_update_window(w, rect);
}
SHADOW_DECLARE(void, win_redraw, (void))
SHADOW_WRITER_BODY(void, win_redraw, (void), (), writes_vlog(&w_), "%d", 0)
static void shadow_win_redraw(void) {
    if (!hooks_nested && !shadow_bypass()) stats_win_redraw.calls++, shadow_tick(WIN);
    orig_win_redraw();
}
SHADOW_DECLARE(void, win_update, (void))
SHADOW_WRITER_BODY(void, win_update, (void), (), writes_vlog(&w_), "%d", 0)
static void shadow_win_update(void) {
    if (!hooks_nested && !shadow_bypass()) stats_win_update.calls++, shadow_tick(WIN);
    orig_win_update();
}

// The frame's functions: the game's calls run the original.
#define PASS_T(ret, name, params, args)                                                   \
    SHADOW_DECLARE_T(ret, name, params)                                                    \
    static ret THISCALL shadow_##name params {                                             \
        if (!hooks_nested && !shadow_bypass()) stats_##name.calls++, shadow_tick(WIN);     \
        return orig_##name args;                                                           \
    }
PASS_T(void, win_update_back_to_window, (struct game_win * w, struct game_buffer *dest), (w, dest))

// What drawing a window into a Buffer writes: what the screen's draw does (the back buffer and the
// overlay, the scratch the fills and copies leave behind), the Buffer drawn into, the z-order list, the
// globals that redirect the draw (0x9b7a68 to 0x9b7a7c, with the opaque flag) and the window's flags.
static void writes_whole_rows(struct hook_writes *w, struct game_buffer *b) {
    if (!b || !b->bits || b->dd || b->neg_height >= 0) return;
    int64_t pitch = b->pitch, rows = -(int64_t)b->neg_height;
    uint8_t *a = b->bits, *z = b->bits + pitch * (rows - 1);
    hooks_writes_add(w, a < z ? a : z, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}
static void writes_to_buffer(struct hook_writes *w, struct game_win *x, struct game_buffer *dest) {
    writes_vlog(w);
    hooks_writes_add(w, game_win_back, sizeof *game_win_back);
    hooks_writes_add(w, game_win_overlay, sizeof *game_win_overlay);
    writes_whole_rows(w, game_win_overlay);
    hooks_writes_add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    writes_whole_rows(w, game_win_back);
    if (dest) hooks_writes_add(w, dest, sizeof *dest);
    writes_whole_rows(w, dest);
    hooks_writes_add(w, game_win_zorder, 0x808);
    hooks_writes_add(w, game_win_z_n, 4);
    hooks_writes_add(w, game_win_back_alt, 0x9b7a7c - 0x9b7a68);
    hooks_writes_add(w, &x->flags2, 4);
}
SHADOW_DECLARE_T(void, win_update_window_to_buffer, (struct game_win * w, struct game_buffer *dest))
SHADOW_WRITER_BODY(void, win_update_window_to_buffer, (struct game_win * w, struct game_buffer *dest), (w, dest),
                   writes_to_buffer(&w_, w, dest), "%d, %d", P(w), P(dest))
static void THISCALL shadow_win_update_window_to_buffer(struct game_win *w, struct game_buffer *dest) {
    if (!hooks_nested && !shadow_bypass()) stats_win_update_window_to_buffer.calls++, shadow_tick(WIN);
    orig_win_update_window_to_buffer(w, dest);
}
// The GraphicWin functions. The update ones draw the game's own windows, so the game's calls run the
// original and the sweep checks Chiron's code (what the screen's draw and the flip write, over the scratch
// windows); the soft updates (InvalidateRect, through the journal) and the paint are checked in the game too.
static void writes_graphic(struct hook_writes *w, struct game_win *x) {
    writes_screen(w, NULL);
    screen_writes_flip(w);
}
#define GW_PASS(name, params, args)                                                 \
    SHADOW_DECLARE_T(int, name, params)                                             \
    SHADOW_WRITER_BODY(int, name, params, args, writes_graphic(&w_, w), "%d", P(w)) \
    static int THISCALL shadow_##name params {                                      \
        if (!hooks_nested && !shadow_bypass()) stats_##name.calls++, shadow_tick(WIN); \
        return orig_##name args;                                                    \
    }
GW_PASS(graphic_win_update, (struct game_win * w, int x, int y, int cw, int ch, int unused), (w, x, y, cw, ch, unused))
GW_PASS(graphic_win_update_2, (struct game_win * w, const int32_t *rect, int unused), (w, rect, unused))
GW_PASS(graphic_win_update_3, (struct game_win * w, int unused), (w, unused))
SHADOW_WRITER_T(WIN, int, graphic_win_soft_update, (struct game_win * w, int x, int y, int cw, int ch), (w, x, y, cw, ch),
                writes_vlog(&w_), "%d, %d, %d, %d, %d", P(w), x, y, cw, ch)
SHADOW_WRITER_T(WIN, int, graphic_win_soft_update_2, (struct game_win * w), (w), writes_vlog(&w_), "%d", P(w))
SHADOW_WRITER_T(WIN, int, graphic_win_soft_update_3, (struct game_win * w, const int32_t *rect), (w, rect), writes_vlog(&w_), "%d, %d",
                P(w), rect ? rect[0] : -1)

// The frame's four strips: each Buffer and the pixels of its rows. Drawing it calls the window's virtual functions,
// so the game's calls run the original and the sweep checks Chiron's code over scratch windows (gw_nc_sweep).
static void gw_nc_writes(struct hook_writes *w, struct game_win *x) {
    void *strips[4] = {x->nc_buf_b4, x->nc_buf_b8, x->nc_buf_bc, x->nc_buf_c0};
    for (int i = 0; i < 4; i++) {
        if (!strips[i]) continue;
        int dup = 0;
        for (int j = 0; j < i; j++) dup |= strips[j] == strips[i];
        if (!dup) dc_writes_pixels(w, strips[i]);
    }
}
SHADOW_DECLARE_T(void, graphic_win_on_redraw_nc, (struct game_win * w, const int32_t *rect, int which))
SHADOW_WRITER_BODY(void, graphic_win_on_redraw_nc, (struct game_win * w, const int32_t *rect, int which), (w, rect, which),
                   (gw_nc_writes(&w_, w), writes_vlog(&w_)), "%d, %d, %d", P(w), P(rect), which)
static void THISCALL shadow_graphic_win_on_redraw_nc(struct game_win *w, const int32_t *rect, int which) {
    if (!hooks_nested && !shadow_bypass()) stats_graphic_win_on_redraw_nc.calls++, shadow_tick(WIN);
    orig_graphic_win_on_redraw_nc(w, rect, which);
}

// What copying a window into another's frame writes: the other window's four strips and, if it has bit 0x40000000, its
// picture (the Buffer at 0xa08). It asks the other window's virtual function, so the game's calls run the original and
// the sweep checks Chiron's code.
static void cif_writes(struct hook_writes *w, struct game_win *o) {
    gw_nc_writes(w, o);
    struct game_buffer *pic = *(struct game_buffer **)((uint8_t *)o + 0xa08);
    if ((o->flags & 0x40000000) && pic) dc_writes_pixels(w, pic);
}
SHADOW_DECLARE_T(void, win_copy_into_frame, (struct game_win * w, struct game_win *o, const int32_t *rect, int which))
SHADOW_WRITER_BODY(void, win_copy_into_frame, (struct game_win * w, struct game_win *o, const int32_t *rect, int which), (w, o, rect, which),
                   (cif_writes(&w_, o), writes_vlog(&w_)), "%d, %d, %d, %d", P(w), P(o), P(rect), which)
static void THISCALL shadow_win_copy_into_frame(struct game_win *w, struct game_win *o, const int32_t *rect, int which) {
    if (!hooks_nested && !shadow_bypass()) stats_win_copy_into_frame.calls++, shadow_tick(WIN);
    orig_win_copy_into_frame(w, o, rect, which);
}

// Painting makes and frees a clip region (a handle) in the Buffer it draws on, which a check can't put back, so the
// game's calls run the original and the sweep checks Chiron's code (paint_check, below).
static void THISCALL shadow_graphic_win_on_paint(struct game_win *w, const int32_t *clip) {
    if (!hooks_nested && !shadow_bypass()) stats_graphic_win_on_paint.calls++, shadow_tick(WIN);
    orig_graphic_win_on_paint(w, clip);
}

PASS_T(void, win_set_rects, (struct game_win * w), (w))
PASS_T(void, win_on_redraw_nc, (struct game_win * w, const int32_t *rect, int which), (w, rect, which))
PASS_T(void, win_draw_nc_to, (struct game_win * w, struct game_buffer *dest, int x, int y), (w, dest, x, y))
PASS_T(void, win_on_nc_paint, (struct game_win * w, const int32_t *rect, int which), (w, rect, which))

// ---- Sweep ---------------------------------------------------------------------------------

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

enum { POOL = 14, REAL = 24 };
// Each scratch window has room behind it for the Buffer a window of the game's own classes keeps
// at 0x444 (the picture that decides where one without a frame is hit).
static struct pool_win {
    struct game_win w;
    struct game_buffer hit;
} pool_ex[POOL];
#define PW(i) (&pool_ex[i].w)
static struct game_win_node nodes[POOL];
static struct game_win_scroll scrolls[2];
static struct game_win_btn btns[4];
static void *stub_vtable[0x180 / 4];

// The stand-in for a subclass's NC_HEIGHT slot: a number that depends on the window.
static int THISCALL stub_nc_height(struct game_win *w) { return w->title_h * 3 + 1; }

// The stand-ins for the other slots the functions call: each only logs the call (see vlog) and
// returns a number of its own. The vtable slots are filled in with them, and the callback a
// window may have at 0x400 is one too, so what a function calls, and with what, is compared.
static int vlog_twin_on;  // the windows are twins: their addresses differ, so they are left out of the record
static void vrec(int slot, const void *w, int a, int b, int c) {
    vlog.hash = (((vlog.hash * 31u + (uint32_t)slot) * 31u + (vlog_twin_on ? 0u : (uint32_t)(uintptr_t)w)) * 31u + (uint32_t)a) * 31u +
                (uint32_t)b * 7u + (uint32_t)c * 13u;
    vlog.n++;
}
#define STUB0(slot) \
    static int THISCALL stub_##slot(struct game_win *w) { return vrec(slot, w, 0, 0, 0), slot + 1; }
#define STUB1(slot) \
    static int THISCALL stub_##slot(struct game_win *w, int a) { return vrec(slot, w, a, 0, 0), slot + 1; }
#define STUB2(slot) \
    static int THISCALL stub_##slot(struct game_win *w, int a, int b) { return vrec(slot, w, a, b, 0), slot + 1; }
#define STUB3(slot) \
    static int THISCALL stub_##slot(struct game_win *w, int a, int b, int c) { return vrec(slot, w, a, b, c), slot + 1; }
// A rectangle a call is given as a pointer is recorded by what it holds (the callee may be handed
// a copy on its own stack), a small number as itself.
static int rect_sig(int a) {
    if ((unsigned)a < 0x1000u) return a;
    const int32_t *r = (const int32_t *)(uintptr_t)a;
    return r[0] * 3 + r[1] * 5 + r[2] * 7 + r[3] * 11;
}
STUB2(0x164) STUB0(0x8) STUB3(0xc) STUB0(0x10) STUB0(0x30) STUB2(0x34) STUB1(0xcc) STUB0(0xf8) STUB0(0x108) STUB2(0x10c) STUB0(0xfc)
static int THISCALL stub_0x128(struct game_win *w, int a, int b) { return vrec(0x128, w, rect_sig(a), b, 0), 0x129; }
static int THISCALL stub_0x124(struct game_win *w, int a) { return vrec(0x124, w, rect_sig(a), 0, 0), 0x125; }
// Told a strip of the frame is drawn: which one (the Buffer is another window's own).
static int THISCALL stub_0xf0(struct game_win *w, int buf, int which) { return vrec(0xf0, NULL, which, 0, 0), 0xf1; }
// Whether the window draws its frame itself: a plain question, from a flag the sweep sets at random.
static int THISCALL stub_0xf4(struct game_win *w) { return (w->flags >> 7) & 1; }
static void stub_callback(int a, int b) { vrec(0x400, NULL, a, b, 0); }
static void fill_event_stubs(void);
static void fill_stub_vtable(void) {
    stub_vtable[WIN_VT_NC_HEIGHT / 4] = (void *)stub_nc_height;
    stub_vtable[0x8 / 4] = (void *)stub_0x8;
    stub_vtable[0xc / 4] = (void *)stub_0xc;
    stub_vtable[0x10 / 4] = (void *)stub_0x10;
    stub_vtable[0x30 / 4] = (void *)stub_0x30;
    stub_vtable[0x34 / 4] = (void *)stub_0x34;
    stub_vtable[0xcc / 4] = (void *)stub_0xcc;
    stub_vtable[0xf8 / 4] = (void *)stub_0xf8;
    stub_vtable[0x108 / 4] = (void *)stub_0x108;
    stub_vtable[0x10c / 4] = (void *)stub_0x10c;
    stub_vtable[0x128 / 4] = (void *)stub_0x128;
    stub_vtable[0x124 / 4] = (void *)stub_0x124;
    stub_vtable[0xf0 / 4] = (void *)stub_0xf0;
    stub_vtable[0x164 / 4] = (void *)stub_0x164;
    stub_vtable[0xf4 / 4] = (void *)stub_0xf4;
    stub_vtable[0xfc / 4] = (void *)stub_0xfc;
    fill_event_stubs();
}

// The stand-ins for the slots the event functions call (see "Mouse and key events"). Each logs its
// call (see vlog); the ones the original reads an answer from give one that depends on the
// arguments, so each answer is met. The callbacks and the game's hooks log too.
#define STUB4(slot)                                                                                    \
    static int THISCALL stub_##slot(struct game_win *w, int a, int b, int c, int d) {                 \
        return vrec(slot, w, a, b, c * 3 + d), slot + 1;                                              \
    }
#define STUB5(slot)                                                                                    \
    static int THISCALL stub_##slot(struct game_win *w, int a, int b, int c, int d, int e) {          \
        return vrec(slot, w, a, b, c * 7 + d * 3 + e), slot + 1;                                      \
    }
STUB1(0x3c) STUB2(0x44) STUB2(0x4c) STUB2(0x50) STUB2(0x54) STUB2(0x58) STUB2(0x5c) STUB2(0x60) STUB2(0x70) STUB2(0x74)
STUB1(0x78) STUB2(0x7c) STUB2(0x80) STUB2(0x84) STUB2(0x88) STUB2(0x8c) STUB2(0x90) STUB2(0x94) STUB2(0x98) STUB2(0x9c)
STUB1(0xb0) STUB0(0x100) STUB1(0x104) STUB4(0x138) STUB2(0x13c) STUB4(0x140) STUB5(0x144) STUB4(0x148) STUB5(0x14c) STUB4(0x150)
// The key slots: whether the key was used (a scroll bar's, asked with (0, key)), and what the handler answers.
static int THISCALL stub_0x64(struct game_win *w, int a, int b) { return vrec(0x64, w, a, b, 0), (a * 5 + b) % 4 == 0 ? 0 : 0x65; }
static int THISCALL stub_0x68(struct game_win *w, int key) { return vrec(0x68, w, key, 0, 0), key % 3; }
static int THISCALL stub_0x6c(struct game_win *w, int key) { return vrec(0x6c, w, key, 0, 0), key & 1; }
// What a hit on the frame is: a code from the point, so each of them turns up (0, the caption's 2, the edges 10 to 17, and the rest).
static int THISCALL stub_0x12c(struct game_win *w, int x, int y) {
    static const int codes[16] = {0, 2, 0, 10, 5, 0x11, 0, 9, 3, 0x10, 2, 0, 0x12, 1, 0xc, 0};
    return vrec(0x12c, w, x, y, 0), codes[(x * 3 + y) & 15];
}
// Whether the region's click is used.
static int THISCALL stub_0x160(struct game_win *w, int a, int b) { return vrec(0x160, w, a, b, 0), (a + b) & 1; }

// The objects a window lets go after an event: kept or not as the sweep sets, and freeing is logged.
static struct stub_link {
    void **vtable;
    int keep;
} links[4];
static void *link_vtable[0x80 / 4];
static int THISCALL stub_link_keep(struct stub_link *l) { return l->keep; }
static int THISCALL stub_link_free(struct stub_link *l) { return vrec(0x1c, l, 0, 0, 0), 0; }

// The callbacks: nine for the mouse (the ones after the first a window may have) and three for keys, and the game's four hooks.
#define CB2(n) \
    static void cb2_##n(int a, int b) { vrec(0x2000 + n, NULL, a, b, 0); }
CB2(0) CB2(1) CB2(2) CB2(3) CB2(4) CB2(5) CB2(6) CB2(7) CB2(8)
static void (*const cb2_table[9])(int, int) = {cb2_0, cb2_1, cb2_2, cb2_3, cb2_4, cb2_5, cb2_6, cb2_7, cb2_8};
static int cbi_key_click(int a, int b) { return vrec(0x2010, NULL, a, b, 0), (a + b) & 3; }
static int cbi_key_down(int a) { return vrec(0x2011, NULL, a, 0, 0), a & 3; }
static int cbi_key_up(int a) { return vrec(0x2012, NULL, a, 0, 0), (a >> 1) & 3; }
static void hook_after(void) { vrec(0x2020, NULL, 0, 0, 0); }
static void hook_key(int a) { vrec(0x2021, NULL, a, 0, 0); }
static void hook_left(int x, int y) { vrec(0x2022, NULL, x, y, 0); }
static void hook_right(int x, int y) { vrec(0x2023, NULL, x, y, 0); }

// A window's callbacks and objects for events, at random.
static void random_event_fields(struct game_win *w) {
    uint8_t *p = (uint8_t *)w;
    for (int i = 0; i < 9; i++) *(void **)(p + 0x408 + 4 * i) = rnd_to(3) ? (void *)cb2_table[i] : NULL;
    *(void **)(p + 0x42c) = rnd_to(3) ? (void *)cbi_key_click : NULL;
    *(void **)(p + 0x430) = rnd_to(3) ? (void *)cbi_key_down : NULL;
    *(void **)(p + 0x434) = rnd_to(3) ? (void *)cbi_key_up : NULL;
    for (int off = 0x1c; off <= 0x6c; off += 4) *(void **)(p + off) = rnd_to(3) ? (void *)&links[rnd_to(4)] : NULL;
}

// Windows with room behind them for the list of regions a subclass keeps at 0x8f4 (the entries at
// 0x980, the answers at 0x900), for the left button's down event, which asks it.
static struct fat_win {
    struct game_win w;
    uint8_t sub[0xa00 - sizeof(struct game_win)];
} fat[3];
_Static_assert(offsetof(struct fat_win, sub) == 0x444, "the sub-class part follows the Win");

static void fill_event_stubs(void) {
    void **v = stub_vtable;
    v[0x3c / 4] = (void *)stub_0x3c, v[0x44 / 4] = (void *)stub_0x44, v[0x4c / 4] = (void *)stub_0x4c, v[0x50 / 4] = (void *)stub_0x50;
    v[0x54 / 4] = (void *)stub_0x54, v[0x58 / 4] = (void *)stub_0x58, v[0x5c / 4] = (void *)stub_0x5c, v[0x60 / 4] = (void *)stub_0x60;
    v[0x64 / 4] = (void *)stub_0x64, v[0x68 / 4] = (void *)stub_0x68, v[0x6c / 4] = (void *)stub_0x6c, v[0x70 / 4] = (void *)stub_0x70;
    v[0x74 / 4] = (void *)stub_0x74, v[0x78 / 4] = (void *)stub_0x78, v[0x7c / 4] = (void *)stub_0x7c, v[0x80 / 4] = (void *)stub_0x80;
    v[0x84 / 4] = (void *)stub_0x84, v[0x88 / 4] = (void *)stub_0x88, v[0x8c / 4] = (void *)stub_0x8c, v[0x90 / 4] = (void *)stub_0x90;
    v[0x94 / 4] = (void *)stub_0x94, v[0x98 / 4] = (void *)stub_0x98, v[0x9c / 4] = (void *)stub_0x9c, v[0xb0 / 4] = (void *)stub_0xb0;
    v[0x100 / 4] = (void *)stub_0x100, v[0x104 / 4] = (void *)stub_0x104, v[0x12c / 4] = (void *)stub_0x12c;
    v[0x138 / 4] = (void *)stub_0x138, v[0x13c / 4] = (void *)stub_0x13c, v[0x140 / 4] = (void *)stub_0x140;
    v[0x144 / 4] = (void *)stub_0x144, v[0x148 / 4] = (void *)stub_0x148, v[0x14c / 4] = (void *)stub_0x14c;
    v[0x150 / 4] = (void *)stub_0x150, v[0x160 / 4] = (void *)stub_0x160;
    link_vtable[0x5c / 4] = (void *)stub_link_keep, link_vtable[0x1c / 4] = (void *)stub_link_free;
}

enum { NBUF = 40 };
static buffer_t nbuf[NBUF], scratch_overlay;
static int nbuf_state;  // 0 not made yet, 1 made, -1 could not be

static const uint32_t FLAG_BITS[] = {WIN_F_MARGIN_H, WIN_F_MARGIN_W, WIN_F_CAPTION, WIN_F_CHILD, WIN_F_THICK, WIN_F_DIALOG,
                                     WIN_F_PARENT_INNER, WIN_F_ALWAYS_TOP, WIN_F_DISABLED, 0x1, 0x4000000,
                                     WIN_F_BIT1, WIN_F_HIT_ALL, WIN_F_HIT_PIXELS, WIN_F_NC_BUFFERS, WIN_F_THIN, WIN_F_TITLE_LEFT,
                                     WIN_F_TITLE_RIGHT, 0x80};
static const uint32_t FLAG2_BITS[] = {WIN_F2_VISIBLE, WIN_F2_VISIBLE, WIN_F2_VISIBLE, WIN_F2_FRAME, WIN_F2_BUSY};

// A forest of scratch windows: a window hangs from a lower-numbered one or from none, and
// has random flags, rectangles and borders.
static void build_forest(void) {
    for (int i = 0; i < POOL; i++) {
        struct game_win *w = PW(i);
        memset(w, 0, sizeof *w);
        w->vtable = stub_vtable;
        w->root = rnd_to(4) ? w : PW(rnd_to(POOL));
        for (unsigned b = 0; b < sizeof FLAG_BITS / sizeof *FLAG_BITS; b++)
            if (rnd_to(3) == 0) w->flags |= FLAG_BITS[b];
        for (unsigned b = 0; b < sizeof FLAG2_BITS / sizeof *FLAG2_BITS; b++)
            if (rnd_to(2) == 0) w->flags2 |= FLAG2_BITS[b];
        w->border = rnd_to(7);
        w->bottom = rnd_to(3) == 0 ? -1 : rnd_to(9);
        w->caption_h = rnd_to(24);
        w->title_h = rnd_to(26);
        w->resize_border = rnd_to(14);
        for (int k = 0; k < 4; k++) w->inner[k] = rnd_to(300) - 40, w->outer[k] = rnd_to(300) - 40;
        if (rnd_to(2)) w->outer[2] = w->outer[0] + rnd_to(120), w->outer[3] = w->outer[1] + rnd_to(120);
        w->nc_owner = rnd_to(3) == 0 ? PW(rnd_to(POOL)) : NULL;
        w->scroll_v = rnd_to(3) ? &scrolls[0] : NULL;
        w->scroll_h = rnd_to(3) ? &scrolls[1] : NULL;
        w->btn_ec = rnd_to(4) ? &btns[0] : NULL;
        w->btn_e8 = rnd_to(4) ? &btns[1] : NULL;
        w->btn_e4 = rnd_to(4) ? &btns[rnd_to(4)] : NULL;
        w->cap_btn = rnd_to(20);
        w->flags &= ~0x40u;
        if (rnd_to(4) == 0) w->flags |= 0x40;
        w->resize_cb = rnd_to(2) ? (void *)stub_callback : NULL;
        for (int k = 0; k < 4; k++)
            (&w->nc_buf_b4)[k] = nbuf_state > 0 ? (void *)&nbuf[rnd_to(NBUF)] : (void *)(uintptr_t)(0x1000 + i * 16 + k);
        w->dlg_index = rnd_to(5);
        nodes[i].win = rnd_to(8) ? PW(rnd_to(POOL)) : NULL;
        nodes[i].next = &nodes[rnd_to(POOL)];
        w->dlg_head = rnd_to(2) ? &nodes[rnd_to(POOL)] : NULL;
        w->dlg_cur = &nodes[rnd_to(POOL)];
        w->dlg_count = rnd_to(4);
        if (i > 0 && rnd_to(5)) {
            struct game_win *p = PW(rnd_to(i));
            w->parent = p;
            if (rnd_to(2)) p->children[p->n_children++] = w;
            else {
                for (int j = p->n_children; j > 0; j--) p->children[j] = p->children[j - 1];
                p->children[0] = w, p->n_children++;
            }
        }
    }
    for (int s = 0; s < 2; s++) {
        struct game_win *w = &scrolls[s].win;
        memset(w, 0, sizeof *w);
        w->vtable = stub_vtable;
        w->root = w;
        w->parent = rnd_to(4) ? PW(rnd_to(POOL)) : NULL;
        w->flags2 = rnd_to(2) ? WIN_F2_FRAME : 0;
        for (int k = 0; k < 4; k++) w->inner[k] = rnd_to(100), w->outer[k] = rnd_to(100);
        scrolls[s].width = rnd_to(30), scrolls[s].height = rnd_to(30) - 5;
        scrolls[s].lo = rnd_to(50) - 10, scrolls[s].hi = scrolls[s].lo + rnd_to(60) - 5;
        scrolls[s].inverted = rnd_to(2);
        scrolls[s].pos = rnd_to(100), scrolls[s].paging = rnd_to(100);
        scrolls[s].f2144 = rnd_to(100), scrolls[s].f2148 = rnd_to(100);
    }
    for (int b = 0; b < 4; b++) {
        struct game_win *w = &btns[b].win;
        memset(w, 0, sizeof *w);
        w->vtable = stub_vtable;
        w->root = w;
        w->flags2 = rnd_to(2) ? WIN_F2_FRAME : 0;
        for (int k = 0; k < 4; k++) w->inner[k] = rnd_to(100), w->outer[k] = rnd_to(100);
        btns[b].width = rnd_to(24), btns[b].height = rnd_to(24), btns[b].picture = rnd_to(100);
    }
}

// The game's globals this module uses, kept for putting back.
struct win_globals {
    struct game_win *zorder[514], *parents[44], *modals[16];
    int32_t z_n, parent_n, modal_n, modal_aux[16], modal_prev, opaque_flag, nc_margin;
    struct game_win *modal, *modal_focus, *focus, *def_focus, *key, *opaque;
    struct game_win *cur, *capture, *hover, *press1, *press2, *prev_focus;
    uint32_t hwnd;
    int32_t screen_w, screen_h, pic_normal, pic_zoomed;
    uint32_t vlog_hash;
    int32_t vlog_n;
    int32_t mouse_pos[2], hit_nc, back_off[2], overlay_on, overlay_pos[2], overlay_size[2];
    struct game_buffer *back_alt;
    struct game_win *track;
    int32_t track_code, track_pos[2], press_inside;
    void *hooks[4];
};

static void globals_save(struct win_globals *g) {
    memcpy(g->zorder, game_win_zorder, sizeof g->zorder);
    memcpy(g->parents, game_win_parents, sizeof g->parents);
    memcpy(g->modals, game_win_modals, sizeof g->modals);
    memcpy(g->modal_aux, game_win_modal_aux, sizeof g->modal_aux);
    g->z_n = *game_win_z_n, g->parent_n = *game_win_parent_n, g->modal_n = *game_win_modal_n;
    g->modal_prev = *game_win_modal_prev, g->opaque_flag = *game_win_opaque_flag, g->nc_margin = *game_win_nc_margin;
    g->modal = *game_win_modal, g->modal_focus = *game_win_modal_focus, g->focus = *game_win_focus;
    g->def_focus = *game_win_def_focus, g->key = *game_win_key, g->opaque = *game_win_opaque;
    g->cur = *game_win_cur, g->capture = *game_win_capture, g->hover = *game_win_hover;
    g->press1 = *game_win_press1, g->press2 = *game_win_press2, g->prev_focus = *game_win_prev_focus;
    g->hwnd = *game_win_hwnd, g->screen_w = *game_win_screen_w, g->screen_h = *game_win_screen_h;
    g->pic_normal = *game_win_pic_normal, g->pic_zoomed = *game_win_pic_zoomed;
    g->vlog_hash = vlog.hash, g->vlog_n = vlog.n;
    memcpy(g->mouse_pos, game_win_mouse_pos, 8), g->hit_nc = *game_win_hit_nc, memcpy(g->back_off, game_win_back_off, 8);
    g->overlay_on = *game_win_overlay_on, memcpy(g->overlay_pos, game_win_overlay_pos, 8), memcpy(g->overlay_size, game_win_overlay_size, 8);
    g->back_alt = *game_win_back_alt;
    g->track = *game_win_track, g->track_code = *game_win_track_code, memcpy(g->track_pos, game_win_track_pos, 8);
    g->press_inside = *game_win_press_inside, memcpy(g->hooks, game_win_hooks, sizeof g->hooks);
}

static void globals_restore(const struct win_globals *g) {
    memcpy(game_win_zorder, g->zorder, sizeof g->zorder);
    memcpy(game_win_parents, g->parents, sizeof g->parents);
    memcpy(game_win_modals, g->modals, sizeof g->modals);
    memcpy(game_win_modal_aux, g->modal_aux, sizeof g->modal_aux);
    *game_win_z_n = g->z_n, *game_win_parent_n = g->parent_n, *game_win_modal_n = g->modal_n;
    *game_win_modal_prev = g->modal_prev, *game_win_opaque_flag = g->opaque_flag, *game_win_nc_margin = g->nc_margin;
    *game_win_modal = g->modal, *game_win_modal_focus = g->modal_focus, *game_win_focus = g->focus;
    *game_win_def_focus = g->def_focus, *game_win_key = g->key, *game_win_opaque = g->opaque;
    *game_win_cur = g->cur, *game_win_capture = g->capture, *game_win_hover = g->hover;
    *game_win_press1 = g->press1, *game_win_press2 = g->press2, *game_win_prev_focus = g->prev_focus;
    *game_win_hwnd = g->hwnd, *game_win_screen_w = g->screen_w, *game_win_screen_h = g->screen_h;
    *game_win_pic_normal = g->pic_normal, *game_win_pic_zoomed = g->pic_zoomed;
    vlog.hash = g->vlog_hash, vlog.n = g->vlog_n;
    memcpy(game_win_mouse_pos, g->mouse_pos, 8), *game_win_hit_nc = g->hit_nc, memcpy(game_win_back_off, g->back_off, 8);
    *game_win_overlay_on = g->overlay_on, memcpy(game_win_overlay_pos, g->overlay_pos, 8), memcpy(game_win_overlay_size, g->overlay_size, 8);
    *game_win_back_alt = g->back_alt;
    *game_win_track = g->track, *game_win_track_code = g->track_code, memcpy(game_win_track_pos, g->track_pos, 8);
    *game_win_press_inside = g->press_inside, memcpy(game_win_hooks, g->hooks, sizeof g->hooks);
}

static struct game_win *any_pool(void) { return rnd_to(8) == 0 ? NULL : PW(rnd_to(POOL)); }

// Random lists and focus windows over the scratch windows.
static void random_globals(void) {
    *game_win_nc_margin = rnd_to(10);
    int n = rnd_to(8);
    *game_win_parent_n = n;
    for (int i = 0; i < n; i++) game_win_parents[i] = PW(rnd_to(POOL));
    *game_win_z_n = rnd_to(4);
    for (int i = 0; i < *game_win_z_n; i++) game_win_zorder[i] = PW(rnd_to(POOL));
    *game_win_opaque = rnd_to(3) ? any_pool() : NULL;
    *game_win_opaque_flag = rnd_to(2);
    *game_win_modal_n = n = rnd_to(6);
    for (int i = 0; i < 16; i++) game_win_modals[i] = any_pool(), game_win_modal_aux[i] = rnd_to(1000);
    *game_win_modal = n ? game_win_modals[n - 1] : any_pool();
    *game_win_modal_prev = rnd_to(1000);
    *game_win_modal_focus = rnd_to(2) ? any_pool() : NULL;
    *game_win_focus = rnd_to(2) ? any_pool() : NULL;
    *game_win_def_focus = any_pool();
    *game_win_key = any_pool();
    *game_win_cur = any_pool(), *game_win_capture = any_pool(), *game_win_hover = any_pool();
    *game_win_press1 = any_pool(), *game_win_press2 = any_pool(), *game_win_prev_focus = any_pool();
    // Not a window of the game's: the original's ShowWindow call (the journal makes it for real)
    // must find none to act on.
    *game_win_hwnd = 0x1;
    *game_win_screen_w = 640 + rnd_to(1500), *game_win_screen_h = 480 + rnd_to(900);
    *game_win_pic_normal = rnd_to(1000), *game_win_pic_zoomed = 1000 + rnd_to(1000);
}

// Every function over one scratch window and its neighbours.
static void sweep_window(struct game_win *w, struct game_win *o) {
    check_win_is_visible(w);
    check_win_is_descendant(w, o);
    check_win_is_below(w, o);
    check_win_has_child(w, o);
    check_win_in_modal(w);
    check_win_is_dialog_focus(w);
    check_win_get_vert_pos(w);
    check_win_get_horz_pos(w);
    check_win_on_nc_hittest(w, rnd_to(160) - 20, rnd_to(160) - 20);
    int x = rnd_to(300) - 100, y = rnd_to(300) - 100;
    int hx = (int)(w->resize_border ? rnd_to(2) * (w->outer[2] - w->outer[0]) : 0);
    check_win_on_nc_hittest(w, hx + rnd_to(3) - 1, y);
    check_win_on_nc_hittest(w, x, w->outer[3] - w->outer[1] - w->resize_border + rnd_to(3) - 1);
    check_win_on_nc_hittest(w, w->resize_border + rnd_to(3) - 1, w->resize_border + rnd_to(3) - 1);
    int32_t r[4], p, q;
    for (int round = 0; round < 3; round++) {
        for (int k = 0; k < 4; k++) r[k] = rnd_to(500) - 150;
        check_win_screen_to_client(w, r);
        check_win_client_to_screen(w, r);
        check_win_nonscreen_to_client(w, r);
        check_win_nonclient_to_screen(w, r);
        check_win_client_to_nonclient_2(w, r);
        p = rnd_to(500) - 150, q = rnd_to(500) - 150;
        check_win_client_to_screen_2(w, &p, &q);
        check_win_screen_to_client_2(w, &p, &q);
        check_win_nonclient_to_screen_2(w, &p, &q);
        check_win_screen_to_nonclient(w, &p, &q);
        check_win_client_to_nonclient(w, &p, &q);
        check_win_nonclient_to_client(w, &p, &q);
    }
    check_win_screen_to_client(w, NULL);
    check_win_client_to_screen(w, NULL);
    check_win_nonscreen_to_client(w, NULL);
    check_win_nonclient_to_screen(w, NULL);
    check_win_client_to_nonclient_2(w, NULL);
    check_win_client_to_nonclient(w, NULL, &q);
    check_win_nonclient_to_client(w, &p, NULL);
    check_win_nonclient_to_client(w, NULL, NULL);
}

// A window of a dialog ring (or none).
static struct game_win *ring_win(struct game_win *w) {
    struct game_win_node *n = w->dlg_head;
    for (int k = rnd_to(4); n && k > 0; k--) n = n->next;
    return n ? n->win : NULL;
}

// The functions that call through the vtable (every slot is a stand-in that logs the call) and
// those that call Windows, over one scratch window.
static void sweep_virtual(struct game_win *w) {
    check_win_set_vert_pos(w, rnd_to(120) - 30);
    check_win_set_horz_pos(w, rnd_to(120) - 30);
    // Ranges both ways round, and an empty one.
    int a = rnd_to(80) - 20, b = rnd_to(80) - 20;
    check_win_set_vert_range(w, a, b);
    check_win_set_horz_range(w, b, a);
    check_win_set_vert_range(w, a, a);
    check_win_set_horz_pos(w, a);
    check_win_set_vert_pos(w, b);
    check_win_set_caption_height(w, rnd_to(40));
    check_win_set_border_thickness(w, rnd_to(12));
    check_win_set_bottom_border_thickness(w, rnd_to(12) - 1);

    check_win_set_dialog_focus(w, rnd_to(3) ? ring_win(w) : any_pool());
    check_win_set_dialog_focus(w, ring_win(w));
    if (w->parent) w->parent->dlg_count = rnd_to(5);
    check_win_pass_dialog_focus(w);
    check_win_pass_dialog_focus(w);

    int32_t r[4];
    for (int k = 0; k < 4; k++) r[k] = rnd_to(400) - 100;
    check_win_nonclient_to_client_2(w, r);
    check_win_nonclient_to_client_2(w, NULL);
    check_win_redo_caption_buttons(w);
    check_win_resize_event(w, rnd_to(500), rnd_to(500));
    check_win_resize(w, rnd_to(300), rnd_to(300), rnd_to(2));
    check_win_resize(w, rnd_to(300), rnd_to(300), 1);
    check_win_resize(w, rnd_to(300), rnd_to(300), 0);
    check_win_center(w);
    check_win_redraw_nc_buffer(w, (unsigned)rnd_to(6));
    check_win_update_nc_buffer(w, rnd_to(5) - 1);

    // The mouse: put back where it is (the game's window is not touched; the hwnd is not one).
    int32_t x, y;
    win_get_mouse_pos(w, &x, &y);
    check_win_set_mouse_pos(w, x, y);
    check_win_get_mouse_pos(w, &x, &y);
    check_win_get_mouse_pos(w, NULL, &y);
    check_win_get_mouse_pos(w, &x, NULL);
    check_win_get_lbutton_state();
    check_win_get_rbutton_state();
    // The capture and hover on the window, and not.
    *game_win_capture = rnd_to(2) ? w : any_pool();
    *game_win_hover = rnd_to(2) ? w : any_pool();
    check_win_maximize(w);
    *game_win_capture = rnd_to(2) ? w : any_pool();
    *game_win_hover = rnd_to(2) ? w : any_pool();
    check_win_minimize(w);
    *game_win_capture = rnd_to(2) ? w : any_pool();
    *game_win_hover = rnd_to(2) ? w : any_pool();
    check_win_show_maximize(w);
}

// ---- Sweep: the events -------------------------------------------------------------------------
//
// The functions that give events to windows call the virtual functions, callbacks and hooks of the
// game's own windows, which draw and open windows, so in shadow mode the game's calls of them run the
// original (as with the drawing functions), and these checks run over the scratch windows, whose
// slots, callbacks and hooks are stand-ins that log what they get. What they leave behind is the
// log, the current window, the mouse presses and the lists a window is brought to the front in.

static void writes_event(struct hook_writes *w) {
    writes_vlog(w);
    hooks_writes_add(w, game_win_cur, 4);
}
// The two presses and whether the mouse is inside (the three words at 0x9b7acc, one after the other).
static void writes_press(struct hook_writes *w) { hooks_writes_add(w, game_win_press1, 12); }
// What the game's calls leave: also the key window and the mouse's, the tracking, the lists, and
// every scratch window's own fields (the front of a list it is in is one of them). The globals the
// mouse state is kept in lie together (0x9b7aa4 to 0x9b7ad8: the frame hit, the tracking, the
// current window, the capture and hover windows, the focus, the key window, the presses).
static void writes_dispatch(struct hook_writes *w) {
    writes_vlog(w);
    hooks_writes_add(w, game_win_hit_nc, 0x34);
    hooks_writes_add(w, game_win_mouse_pos, 8);
    writes_zorder(w);
    writes_parents(w, 0);
    hooks_writes_add(w, pool_ex, sizeof pool_ex);
}

#define PASS_CHECK_T(ret, name, params, args, writes, fmt, ...) \
    PASS_T(ret, name, params, args)                             \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__)
// The same for the C functions (the caller clears the stack).
#define PASS_CHECK_C(ret, name, params, args, writes, fmt, ...)                       \
    SHADOW_DECLARE(ret, name, params)                                                   \
    static ret shadow_##name params {                                                   \
        if (!hooks_nested && !shadow_bypass()) stats_##name.calls++, shadow_tick(WIN);  \
        return orig_##name args;                                                        \
    }                                                                                   \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__)

PASS_CHECK_T(void, win_left_down_event, (struct game_win * w, int x, int y, int nc), (w, x, y, nc), writes_event(&w_),
             "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_left_up_event, (struct game_win * w, int x, int y, int nc), (w, x, y, nc), writes_event(&w_),
             "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_right_click_event, (struct game_win * w, int x, int y, int nc), (w, x, y, nc), writes_event(&w_),
             "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_right_down_event, (struct game_win * w, int x, int y, int nc), (w, x, y, nc), writes_event(&w_),
             "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_right_up_event, (struct game_win * w, int x, int y, int nc), (w, x, y, nc), writes_event(&w_),
             "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(int, win_key_click_event, (struct game_win * w, int a, int key), (w, a, key), writes_event(&w_), "%d, %d, %d", P(w), a,
             key)
PASS_CHECK_T(int, win_key_down_event, (struct game_win * w, int key), (w, key), writes_event(&w_), "%d, %d", P(w), key)
PASS_CHECK_T(int, win_key_up_event, (struct game_win * w, int key), (w, key), writes_event(&w_), "%d, %d", P(w), key)
PASS_CHECK_T(void, win_on_mouse_move, (struct game_win * w, int x, int y, int keys, int nc), (w, x, y, keys, nc),
             writes_event(&w_), "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_on_mouse_wheel, (struct game_win * w, int keys, int delta, int x, int y, int nc), (w, keys, delta, x, y, nc),
             writes_event(&w_), "%d, %d, %d", P(w), delta, nc)
PASS_CHECK_T(void, win_on_l_button_down, (struct game_win * w, int dbl, int x, int y, int keys, int nc), (w, dbl, x, y, keys, nc),
             (writes_event(&w_), writes_press(&w_), writes_siblings(&w_, w)), "%d, %d, %d, %d, %d", P(w), dbl, x, y, nc)
PASS_CHECK_T(void, win_on_l_button_up, (struct game_win * w, int x, int y, int keys, int nc), (w, x, y, keys, nc),
             (writes_event(&w_), writes_press(&w_)), "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(void, win_on_r_button_down, (struct game_win * w, int dbl, int x, int y, int keys, int nc), (w, dbl, x, y, keys, nc),
             (writes_event(&w_), writes_press(&w_), writes_siblings(&w_, w)), "%d, %d, %d, %d, %d", P(w), dbl, x, y, nc)
PASS_CHECK_T(void, win_on_r_button_up, (struct game_win * w, int x, int y, int keys, int nc), (w, x, y, keys, nc),
             (writes_event(&w_), writes_press(&w_)), "%d, %d, %d, %d", P(w), x, y, nc)
PASS_CHECK_T(int, win_on_sys_key, (struct game_win * w, int vk, int down, int rep, int scan), (w, vk, down, rep, scan), (void)0,
             "%d, %d, %d, %d, %d", P(w), vk, down, rep, scan)

PASS_CHECK_C(void, win_OnKey, (int unused, int vk, int down, int rep, int scan), (unused, vk, down, rep, scan), writes_dispatch(&w_),
             "%d, %d, %d, %d", vk, down, rep, scan)
PASS_CHECK_C(void, win_OnChar, (int unused, int ch, int rep), (unused, ch, rep), writes_dispatch(&w_), "%d, %d", ch, rep)
PASS_CHECK_C(void, win_OnSysKey, (int unused, int vk, int down, int rep, int scan), (unused, vk, down, rep, scan),
             writes_dispatch(&w_), "%d, %d, %d, %d", vk, down, rep, scan)
PASS_CHECK_C(void, win_OnLButtonDown, (int hwnd, int dbl, int x, int y, int keys), (hwnd, dbl, x, y, keys), writes_dispatch(&w_),
             "%d, %d, %d, %d", dbl, x, y, keys)
PASS_CHECK_C(void, win_OnLButtonUp, (int hwnd, int x, int y, int keys), (hwnd, x, y, keys), writes_dispatch(&w_), "%d, %d, %d", x, y,
             keys)
PASS_CHECK_C(void, win_OnRButtonDown, (int hwnd, int dbl, int x, int y, int keys), (hwnd, dbl, x, y, keys), writes_dispatch(&w_),
             "%d, %d, %d, %d", dbl, x, y, keys)
PASS_CHECK_C(void, win_OnRButtonUp, (int hwnd, int x, int y, int keys), (hwnd, x, y, keys), writes_dispatch(&w_), "%d, %d, %d", x, y,
             keys)

// Key codes of the kinds the functions tell apart (return, escape, tab, letters and digits, a key
// the system gives no character for, and the shift, control and alt flags on top), and at random.
static int random_key(void) {
    static const int keys[] = {0xd, 0x1b, 0x1000d, 9, 0x41, 0x5a, 0x30, 0x39, 0x20, 0x25, 0x70, 0x2e, 0x111, 0x10041, 0x2001b, 0x4000d};
    if (rnd_to(3)) return keys[rnd_to((int)(sizeof keys / sizeof *keys))];
    return rnd_to(0x130) | (rnd_to(3) ? 0 : 0x10000 << rnd_to(3));
}
static void random_hooks(void) {
    game_win_hooks[0] = rnd_to(2) ? (void *)hook_after : NULL;
    game_win_hooks[1] = rnd_to(2) ? (void *)hook_key : NULL;
    game_win_hooks[2] = rnd_to(2) ? (void *)hook_left : NULL;
    game_win_hooks[3] = rnd_to(2) ? (void *)hook_right : NULL;
}
// A window's dialog list: empty, or with a focus window of another that has no list of its own (the
// original goes through the focus window without looking, so it must be one).
static void random_dialog(struct game_win *w) {
    if (rnd_to(3) == 0) {
        w->dlg_count = 0;
        return;
    }
    struct game_win *cur = PW(rnd_to(POOL));
    if (cur == w) cur = PW(rnd_to(POOL) | 1);
    if (cur == w) cur = PW(0);
    if (cur == w) return;
    cur->dlg_count = 0;
    nodes[0].win = cur;
    w->dlg_head = rnd_to(8) ? &nodes[0] : NULL;
    if (!w->dlg_head) {
        w->dlg_count = 0;  // the original would crash
        return;
    }
    w->dlg_cur = &nodes[0];
    w->dlg_count = 1 + rnd_to(2);
}
static void build_fat(void) {
    for (int i = 0; i < 3; i++) {
        struct game_win *w = &fat[i].w;
        memset(&fat[i], 0, sizeof fat[i]);
        w->vtable = stub_vtable;
        w->root = w;
        for (unsigned b = 0; b < sizeof FLAG_BITS / sizeof *FLAG_BITS; b++)
            if (rnd_to(3) == 0) w->flags |= FLAG_BITS[b];
        for (unsigned b = 0; b < sizeof FLAG2_BITS / sizeof *FLAG2_BITS; b++)
            if (rnd_to(2) == 0) w->flags2 |= FLAG2_BITS[b];
        for (int k = 0; k < 4; k++) w->inner[k] = rnd_to(300) - 40;
        w->parent = rnd_to(4) ? PW(rnd_to(POOL)) : NULL;
        if (w->parent && rnd_to(2) && w->parent->n_children < 149) w->parent->children[w->parent->n_children++] = w;
        // The list of regions: up to four, and what each answers to.
        int32_t *list = (int32_t *)(fat[i].sub + (0x8f4 - 0x444));
        list[0] = (int32_t)(uintptr_t)(fat[i].sub + (0x980 - 0x444));
        list[2] = rnd_to(5);
        int32_t *entries = (int32_t *)fat[i].sub + (0x980 - 0x444) / 4;
        for (int e = 0; e < 4; e++) {
            entries[e * 6 + 0] = rnd_to(100) - 20, entries[e * 6 + 1] = rnd_to(100) - 20;
            entries[e * 6 + 2] = entries[e * 6 + 0] + rnd_to(120), entries[e * 6 + 3] = entries[e * 6 + 1] + rnd_to(120);
            entries[e * 6 + 4] = rnd_to(50), entries[e * 6 + 5] = rnd_to(16);
        }
        int32_t *answers = (int32_t *)fat[i].sub + (0x900 - 0x444) / 4;
        for (int e = 0; e < 16; e++) answers[e] = rnd_to(50);
    }
}

// Every event function over the scratch windows and three that keep a list of regions, with
// callbacks, objects (kept or not), hooks and dialog lists at random.
static void sweep_events(void) {
    for (int i = 0; i < 4; i++) links[i].vtable = link_vtable, links[i].keep = rnd_to(2);
    build_fat();
    random_globals();
    for (int k = 0; k < 3 * POOL + 3; k++) {
        struct game_win *w = k < 3 * POOL ? PW(k % POOL) : &fat[k - 3 * POOL].w;
        random_event_fields(w);
        random_hooks();
        int wide = w->inner[2] - w->inner[0], high = w->inner[3] - w->inner[1];
        int x = rnd_to(3) ? rnd_to(240) - 60 : rnd_to((wide > 0 ? wide : 1) + 6) - 3;
        int y = rnd_to(3) ? rnd_to(240) - 60 : rnd_to((high > 0 ? high : 1) + 6) - 3;
        int nc = rnd_to(2), keys = rnd_to(64);
        check_win_left_down_event(w, x, y, nc);
        check_win_left_up_event(w, x, y, nc);
        check_win_right_click_event(w, x, y, nc);
        check_win_right_down_event(w, x, y, nc);
        check_win_right_up_event(w, x, y, nc);
        check_win_on_mouse_move(w, x, y, keys, nc);
        check_win_on_mouse_wheel(w, keys, (int)rnd(), x, y, nc);
        check_win_on_mouse_wheel(w, keys, rnd_to(600) - 300, x, y, nc ^ 1);
        // The buttons: pressed in this window or another (or none), and let go inside and outside.
        for (int p = 0; p < 4; p++) {
            *game_win_press1 = rnd_to(3) == 0 ? w : any_pool();
            *game_win_press2 = rnd_to(3) == 0 ? w : any_pool();
            int ix = rnd_to(2) ? x : rnd_to((wide > 0 ? wide : 1)), iy = rnd_to(2) ? y : rnd_to((high > 0 ? high : 1));
            check_win_on_l_button_up(w, ix, iy, keys, rnd_to(2));
            *game_win_press1 = rnd_to(3) == 0 ? w : any_pool();
            *game_win_press2 = rnd_to(3) == 0 ? w : any_pool();
            check_win_on_r_button_up(w, ix, iy, keys, rnd_to(2));
        }
        check_win_on_r_button_down(w, rnd_to(2), x, y, keys, nc);
        check_win_on_r_button_down(w, 1, x, y, keys, nc ^ 1);
        // The left button down asks the list of regions, which only the extra-large windows have.
        if (k >= 3 * POOL) {
            for (int p = 0; p < 6; p++) check_win_on_l_button_down(w, rnd_to(2), rnd_to(120) - 20, rnd_to(120) - 20, keys, rnd_to(2));
        } else {
            w->flags &= ~0x80u;  // the pool's windows have no list behind them
            check_win_on_l_button_down(w, rnd_to(2), x, y, keys, nc);
        }
        check_win_on_sys_key(w, random_key(), rnd_to(2), rnd_to(300), rnd_to(300));
        check_win_on_sys_key(w, random_key(), rnd_to(2), (int)rnd(), (int)rnd());
        for (int j = 0; j < 3; j++) {
            int key = random_key();
            random_dialog(w);
            check_win_key_click_event(w, rnd_to(3), key);
            random_dialog(w);
            check_win_key_down_event(w, key);
            random_dialog(w);
            check_win_key_up_event(w, key);
        }
    }
}

// The game's own calls into the windows, with the mouse over the scratch windows (the Buffers they
// are hit-tested by are needed) pressed, captured and hovered at random, a modal window or none,
// and the game's hooks, a tracked window and the key windows at random.
static void sweep_dispatch(void) {
    for (int k = 0; k < 60; k++) {
        random_globals();
        random_hooks();
        for (int i = 0; i < 4; i++) links[i].keep = rnd_to(2);
        for (int i = 0; i < POOL; i++) random_event_fields(PW(i));
        *game_win_press1 = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_press2 = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_capture = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_hover = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_modal = rnd_to(3) == 0 ? any_pool() : NULL;
        *game_win_modal_prev = (int32_t)(uintptr_t)(rnd_to(2) ? any_pool() : NULL);
        *game_win_parent_n = rnd_to(9);
        for (int i = 0; i < *game_win_parent_n; i++) game_win_parents[i] = PW(rnd_to(POOL));
        *game_win_track = rnd_to(3) == 0 ? any_pool() : NULL;
        int dbl = rnd_to(2), keys = rnd_to(64);
        int x = rnd_to(340) - 30, y = rnd_to(300) - 30;
        check_win_OnLButtonDown(0, dbl, x, y, keys);
        x = rnd_to(340) - 30, y = rnd_to(300) - 30;
        check_win_OnLButtonUp(0, x, y, keys);
        x = rnd_to(340) - 30, y = rnd_to(300) - 30;
        check_win_OnRButtonDown(0, dbl, x, y, keys);
        x = rnd_to(340) - 30, y = rnd_to(300) - 30;
        check_win_OnRButtonUp(0, x, y, keys);
        check_win_OnKey(0, random_key(), rnd_to(2), rnd_to(5), rnd_to(256));
        check_win_OnChar(0, rnd_to(256), rnd_to(5));
        check_win_OnSysKey(0, random_key(), rnd_to(2), rnd_to(5), rnd_to(256));
    }
}

// ---- Sweep: drawing and the mouse's window ---------------------------------------------------

// Real Buffers (made once by the game's own code, which only allocates DIBs and device contexts
// that stay for good): the scratch windows' frame strips, and the picture a window may hit-test
// by, filled again with pixels some of which are the transparent colour each round.

static int make_buffer(buffer_t *b, int w, int h) {
    memset(b, 0, sizeof *b);
    game_buffer_ctor(b);
    game_buffer_init(b, w, h, 0, 0);
    return b->bits && !b->dd && b->neg_height;
}
// Pixels at random, a good share of them the transparent colour.
static void scatter(buffer_t *b) {
    if (!b->bits || b->dd) return;
    for (int y = 0; y < -b->neg_height; y++)
        for (int x = 0; x < b->width; x++)
            b->bits[(intptr_t)b->pitch * y + x] = (uint8_t)(rnd_to(2) ? *game_default_key : rnd_to(3) ? (uint8_t)rnd_to(256) : 0);
}
static int scratch_buffers(void) {
    if (!nbuf_state) {
        nbuf_state = 1;
        for (int i = 0; i < NBUF; i++)
            if (!make_buffer(&nbuf[i], 12 + (i * 7) % 30, 10 + (i * 5) % 25)) nbuf_state = -1;
        for (int i = 0; i < POOL; i++)
            if (!make_buffer(&pool_ex[i].hit, 14 + i, 12 + i % 5)) nbuf_state = -1;
        if (!make_buffer(&scratch_overlay, 24, 14)) nbuf_state = -1;
    }
    if (nbuf_state > 0) {
        for (int i = 0; i < NBUF; i++) scatter(&nbuf[i]);
        for (int i = 0; i < POOL; i++) scatter(&pool_ex[i].hit);
        scatter(&scratch_overlay);
    }
    return nbuf_state > 0;
}

// Which window is under a point, over the scratch windows: with the mouse pressed, captured and
// hovered over on windows (shown or not), with a modal window, a window before it, and without.
static void sweep_mouse(void) {
    for (int k = 0; k < 30; k++) {
        *game_win_press1 = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_press2 = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_capture = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_hover = rnd_to(6) == 0 ? any_pool() : NULL;
        *game_win_modal = rnd_to(3) == 0 ? any_pool() : NULL;
        *game_win_modal_prev = (int32_t)(uintptr_t)(rnd_to(2) ? any_pool() : NULL);
        *game_win_parent_n = rnd_to(9);
        for (int i = 0; i < *game_win_parent_n; i++) game_win_parents[i] = PW(rnd_to(POOL));
        int32_t x = rnd_to(340) - 30, y = rnd_to(300) - 30;
        check_win_get_mouse_window(&x, &y);
    }
}

// The screen behind a window, drawn into a Buffer the call makes (so a DIB: checked on twin Buffers, one
// through the original and one through Chiron's code). Besides the Buffers, what the call leaves in the
// globals that redirect the draw, the z-order list and the window's flags, and the calls made through the
// windows' vtables are compared; the globals are put back between the two runs.
struct back_state {
    uint8_t redirect[0x9b7a7c - 0x9b7a68];
    int32_t z_n;
    uint32_t flags2, vhash;
    int32_t vn;
    uint8_t zorder[0x808];
};
static void back_take(struct back_state *s, struct game_win *w) {
    memcpy(s->redirect, game_win_back_alt, sizeof s->redirect);
    s->z_n = *game_win_z_n, s->flags2 = w->flags2, s->vhash = vlog.hash, s->vn = vlog.n;
    memcpy(s->zorder, game_win_zorder, sizeof s->zorder);
}
static void back_put(const struct back_state *s, struct game_win *w) {
    memcpy(game_win_back_alt, s->redirect, sizeof s->redirect);
    *game_win_z_n = s->z_n, w->flags2 = s->flags2, vlog.hash = s->vhash, vlog.n = s->vn;
    memcpy(game_win_zorder, s->zorder, sizeof s->zorder);
}
static void twin_back(struct game_win *w) {
    static buffer_t da, db;
    static int made;
    static struct back_state before, a, b;
    if (made) game_buffer_dtor(&da), game_buffer_dtor(&db);
    memset(&da, 0, sizeof da), memset(&db, 0, sizeof db);
    game_buffer_ctor(&da), game_buffer_ctor(&db);
    made = 1;
    back_take(&before, w);
    vlog.hash = 0, vlog.n = 0;
    hooks_nested++;
    orig_win_update_back_to_window(w, &da);
    hooks_nested--;
    back_take(&a, w);
    back_put(&before, w);
    vlog.hash = 0, vlog.n = 0;
    win_update_back_to_window(w, &db);
    back_take(&b, w);
    back_put(&before, w);
    stats_win_update_back_to_window.calls++;
    const char *d = NULL;
    if (memcmp(&a, &b, sizeof a)) d = "what it leaves in the globals, the z-order list, the window's flags or the calls through the vtables";
    if (!d) d = buffer_twin_diff(&da, &db, 0, 0);
    if (!d) d = buffer_pixels_diff(&da, &db);
    if (d) hooks_mismatch(&stats_win_update_back_to_window, "(%d): %s", P(w), d);
}

// Drawing the screen over the scratch windows and the game's back buffer (put back after each
// check): the whole screen and an area, from a window, with the back buffer or another one,
// with the overlay on and off.
static void sweep_screen(void) {
    struct game_buffer *back = game_win_back;
    if (!back->bits || back->dd || back->width <= 0 || back->neg_height >= 0) return;
    int w = back->width, h = -back->neg_height;
    // The overlay Buffer may not have pixels yet: one that has is put in its place for the sweep.
    struct game_buffer saved_overlay = *game_win_overlay;
    *game_win_overlay = scratch_overlay;
    for (int k = 0; k < 14; k++) {
        *game_win_z_n = rnd_to(7);
        for (int i = 0; i < *game_win_z_n; i++) game_win_zorder[i] = PW(rnd_to(POOL));
        *game_win_back_alt = rnd_to(2) ? (struct game_buffer *)(uintptr_t)1 : NULL;
        *game_win_opaque = rnd_to(2) && *game_win_z_n ? game_win_zorder[rnd_to(*game_win_z_n)] : any_pool();
        *game_win_overlay_on = rnd_to(2);
        game_win_overlay_pos[0] = rnd_to(w + 40) - 20, game_win_overlay_pos[1] = rnd_to(h + 40) - 20;
        game_win_overlay_size[0] = rnd_to(80), game_win_overlay_size[1] = rnd_to(60);
        int32_t rect[4] = {rnd_to(w) - 10, rnd_to(h) - 10, 0, 0};
        rect[2] = rect[0] + rnd_to(120), rect[3] = rect[1] + rnd_to(90);
        struct game_win *win = rnd_to(3) ? (*game_win_z_n ? game_win_zorder[rnd_to(*game_win_z_n)] : NULL) : any_pool();
        check_win_update_screen(rnd_to(2) ? rect : NULL, win);
        check_win_update_screen(rect, NULL);
        check_win_update_window(PW(rnd_to(POOL)), rnd_to(3) ? rect : NULL);
        check_win_update_window(PW(rnd_to(POOL)), NULL);
        check_win_update_window_to_buffer(PW(rnd_to(POOL)), &nbuf[rnd_to(NBUF)]);
        check_win_update_window_to_buffer(PW(rnd_to(POOL)), NULL);
        if (k % 3 == 0) twin_back(PW(rnd_to(POOL)));
        check_win_redraw();
        check_win_update();
    }
    *game_win_overlay = saved_overlay;
}

// ---- Sweep: the frame, on twins ----
//
// Two windows built alike, each with its four frame strips, a tile picture (or none) and a Buffer
// to draw to or copy from. The original runs on the first and Chiron's code on the second, and
// both windows' Buffers (every field, with handles compared by whether they are set, the device
// contexts by content, and the pixels) and the virtual calls made are compared.

struct twin {
    struct game_win w;
    buffer_t hit;  // at 0x444, where a window of the game's classes keeps its picture
    buffer_t nc[4], tile, dest;
};
static struct twin tw[2];
static int tw_built[2][7];

static void twin_buf(int which, int slot, buffer_t *b, int w, int h) {
    if (tw_built[which][slot]) game_buffer_dtor(b);
    memset(b, 0, sizeof *b);
    game_buffer_ctor(b);
    tw_built[which][slot] = 1;
    if (w > 0 && h > 0) game_buffer_init(b, w, h, 0, 0), scatter(b);
}

static const char *const titles[] = {"Hi", "A caption", "Datalinks and the planet", "", "x", "Mind Worm Boil Gate"};

// A window built from `seed`, so the two are alike.
static void twin_make(int which, uint32_t seed, struct font *font) {
    uint32_t keep = rng_state;
    rng_state = seed ? seed : 1;
    struct twin *t = &tw[which];
    struct game_win *w = &t->w;
    memset(w, 0, sizeof *w);
    w->vtable = stub_vtable;
    w->root = w;
    for (unsigned b = 0; b < sizeof FLAG_BITS / sizeof *FLAG_BITS; b++)
        if (rnd_to(2) == 0) w->flags |= FLAG_BITS[b];
    w->flags2 = WIN_F2_VISIBLE | (rnd_to(8) ? WIN_F2_FRAME : 0);
    w->flags &= ~(uint32_t)WIN_F_CHILD;
    w->border = 1 + rnd_to(6);
    w->bottom = rnd_to(3) == 0 ? -1 : rnd_to(8);
    w->caption_h = 4 + rnd_to(20);
    w->cap_btn = rnd_to(2) ? rnd_to(3) : rnd_to(5);
    w->outer[0] = rnd_to(60) - 10, w->outer[1] = rnd_to(60) - 10;
    w->outer[2] = w->outer[0] + 20 + rnd_to(70), w->outer[3] = w->outer[1] + 20 + rnd_to(60);
    w->inner[0] = rnd_to(8), w->inner[1] = rnd_to(8), w->inner[2] = w->inner[0] + 5 + rnd_to(70), w->inner[3] = w->inner[1] + 5 + rnd_to(50);
    w->nc_color = rnd_to(256), w->box_color[0] = rnd_to(256), w->box_color[1] = rnd_to(256);
    for (int k = 0; k < 4; k++) w->text_color[k] = rnd_to(4) ? rnd_to(256) : -1;
    w->title = font && rnd_to(3) ? titles[rnd_to((int)(sizeof titles / sizeof *titles))] : NULL;
    w->nc_font = font && rnd_to(5) ? font : NULL;
    int width = w->outer[2] - w->outer[0], height = w->outer[3] - w->outer[1];
    int top = nc_top(w), bot = nc_bottom(w);
    int exact = rnd_to(4) != 0;
    int sw[4] = {width, width, w->border, w->border}, sh[4] = {top, bot, height - bot - top, height - bot - top};
    twin_buf(which, 0, &t->hit, 16 + rnd_to(20), 10 + rnd_to(20));
    for (int i = 0; i < 4; i++)
        twin_buf(which, 1 + i, &t->nc[i], exact ? sw[i] : 4 + rnd_to(60), exact ? sh[i] : 4 + rnd_to(40));
    twin_buf(which, 5, &t->tile, 3 + rnd_to(30), 3 + rnd_to(25));
    twin_buf(which, 6, &t->dest, 20 + rnd_to(100), 20 + rnd_to(80));
    w->nc_tile = rnd_to(3) ? &t->tile : NULL;
    w->nc_buf_b4 = &t->nc[0], w->nc_buf_c0 = &t->nc[1], w->nc_buf_bc = &t->nc[2], w->nc_buf_b8 = &t->nc[3];
    rng_state = keep;
}

// Compare the twins after one call each; after a difference, build them again alike.
static void twin_compare(struct hook_stats *s, const char *what, uint32_t ha, int32_t na, struct font *font) {
    const char *d = NULL;
    static char msg[200];
    if (ha != vlog.hash || na != vlog.n) d = "calls through the vtable";
    for (int i = 0; i < 7 && !d; i++) {
        buffer_t *a = i == 0 ? &tw[0].hit : i < 5 ? &tw[0].nc[i - 1] : i == 5 ? &tw[0].tile : &tw[0].dest;
        buffer_t *b = i == 0 ? &tw[1].hit : i < 5 ? &tw[1].nc[i - 1] : i == 5 ? &tw[1].tile : &tw[1].dest;
        d = buffer_twin_diff(a, b, 0, 0);
        if (!d) d = buffer_pixels_diff(a, b);
        if (d) snprintf(msg, sizeof msg, "buffer %d: %s", i, d), d = msg;
    }
    if (!d) return;
    hooks_mismatch(s, " (%s): %s", what, d);
    uint32_t seed = rnd() | 1;
    twin_make(0, seed, font), twin_make(1, seed, font);
}

#define TWIN_STEP(stats, what, font, run_a, run_b)                         \
    do {                                                                   \
        vlog.hash = 0, vlog.n = 0, vlog_twin_on = 1;                        \
        run_a;                                                             \
        uint32_t ha_ = vlog.hash;                                          \
        int32_t na_ = vlog.n;                                              \
        vlog.hash = 0, vlog.n = 0;                                         \
        run_b;                                                             \
        twin_compare(&(stats), what, ha_, na_, font);                      \
        vlog_twin_on = 0;                                                  \
    } while (0)

static void twin_sweep(struct font *font, uint32_t hwnd) {
    if (!*game_palette_copy) return;
    static const int32_t none[4] = {0, 0, 0, 0};
    (void)none;
    for (int round = 0; round < 16; round++) {
        uint32_t seed = rnd() | 1;
        twin_make(0, seed, font), twin_make(1, seed, font);
        TWIN_STEP(stats_win_set_rects, "set_rects", font, orig_win_set_rects(&tw[0].w), win_set_rects(&tw[1].w));
        for (int k = 0; k < 10; k++) {
            struct game_win *w = &tw[0].w;
            int32_t r[4] = {w->outer[0] + rnd_to(60) - 20, w->outer[1] + rnd_to(50) - 15, 0, 0};
            r[2] = r[0] + rnd_to(90), r[3] = r[1] + rnd_to(70);
            const int32_t *rp = rnd_to(3) ? r : NULL;
            int which = rnd_to(6) - 1;
            TWIN_STEP(stats_win_on_redraw_nc, "on_redraw_nc", font, orig_win_on_redraw_nc(&tw[0].w, rp, which),
                      win_on_redraw_nc(&tw[1].w, rp, which));
            // On the screen: to another Buffer (the back buffer is never drawn on, and its clip is left as it was).
            game_win_back_off[0] = rnd_to(20) - 5, game_win_back_off[1] = rnd_to(20) - 5;
            *game_win_back_alt = &tw[0].dest;
            struct game_buffer *alt_b = &tw[1].dest;
            vlog.hash = 0, vlog.n = 0, vlog_twin_on = 1;
            orig_win_on_nc_paint(&tw[0].w, rp, which);
            uint32_t ha = vlog.hash;
            int32_t na = vlog.n;
            vlog.hash = 0, vlog.n = 0;
            *game_win_back_alt = alt_b;
            win_on_nc_paint(&tw[1].w, rp, which);
            *game_win_back_alt = NULL;
            twin_compare(&stats_win_on_nc_paint, "on_nc_paint", ha, na, font);
            vlog_twin_on = 0;
            if (hwnd) {
                int x = rnd_to(100) - 30, y = rnd_to(80) - 30;
                *game_win_hwnd = hwnd;
                TWIN_STEP(stats_win_draw_nc_to, "draw_nc_to", font,
                          orig_win_draw_nc_to(&tw[0].w, &tw[0].dest, x, y), win_draw_nc_to(&tw[1].w, &tw[1].dest, x, y));
                *game_win_hwnd = 0x1;
                orig_win_draw_nc_to(&tw[0].w, NULL, x, y);
                win_draw_nc_to(&tw[1].w, NULL, x, y);
            }
        }
    }
}

// A paint on the game's back buffer, or on a pair of twin Buffers it is redirected to: the original runs on the
// first from the back buffer's state as it is, Chiron's code on the second from the same state (the back buffer's
// pixels and what drawing leaves in the game's memory put back in between), and both are compared: the back buffer
// (its clip region is a handle, compared by whether it is set, and passed on to the second run, which frees it),
// the twins, and the scratch the draws leave behind. The back buffer's pixels are put back after.
static void paint_check(struct game_win *w, const int32_t *clip, int to_twins) {
    static struct game_buffer ta, tb;
    static int made;
    static uint8_t *rows_before, *rows_a;
    static size_t cap;
    struct game_buffer *m = game_win_back;
    enum { REGION = 0x70, N_SPANS = 3 };
    int64_t pitch = m->pitch, rows = -(int64_t)m->neg_height;
    uint8_t *base = pitch < 0 ? m->bits + pitch * (rows - 1) : m->bits;
    size_t n = (size_t)(pitch < 0 ? -pitch : pitch) * (size_t)rows;
    if (n > cap) {
        free(rows_before), free(rows_a);
        rows_before = malloc(n), rows_a = malloc(n);
        cap = rows_before && rows_a ? n : 0;
    }
    if (!cap) return;
    struct {
        void *p;
        size_t n;
        uint8_t *before, *a;
    } span[N_SPANS] = {{game_buffer_sprite, sizeof *game_buffer_sprite},
                       {game_sprite_scratch, sizeof *game_sprite_scratch},
                       {game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text)}};
    for (int i = 0; i < N_SPANS; i++) {
        span[i].before = malloc(span[i].n), span[i].a = malloc(span[i].n);
        memcpy(span[i].before, span[i].p, span[i].n);
    }
    if (to_twins) {
        if (made) game_buffer_dtor(&ta), game_buffer_dtor(&tb);
        memset(&ta, 0, sizeof ta), memset(&tb, 0, sizeof tb);
        game_buffer_ctor(&ta), game_buffer_ctor(&tb);
        made = 1;
        int tw = 24 + rnd_to(30), th = 20 + rnd_to(30);
        game_buffer_init(&ta, tw, th, 0, 0), game_buffer_init(&tb, tw, th, 0, 0);
        if (!ta.bits || !tb.bits) return;
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) ta.bits[(intptr_t)ta.pitch * y + x] = tb.bits[(intptr_t)tb.pitch * y + x] = (uint8_t)rnd();
    }
    struct game_buffer obj_before = *m, obj_a;
    memcpy(rows_before, base, n);
    struct game_buffer *saved_alt = *game_win_back_alt;
    *game_win_back_alt = to_twins ? &ta : saved_alt;
    hooks_nested++;
    orig_graphic_win_on_paint(w, clip);
    hooks_nested--;
    obj_a = *m;
    memcpy(rows_a, base, n);
    for (int i = 0; i < N_SPANS; i++) memcpy(span[i].a, span[i].p, span[i].n);  // (the spans overlap: all taken before any is put back)
    for (int i = 0; i < N_SPANS; i++) memcpy(span[i].p, span[i].before, span[i].n);
    memcpy(base, rows_before, n);
    *m = obj_before;
    *(int32_t *)((uint8_t *)m + REGION) = *(int32_t *)((uint8_t *)&obj_a + REGION);
    *game_win_back_alt = to_twins ? &tb : saved_alt;
    graphic_win_on_paint(w, clip);
    const char *d = NULL;
    if (!d) d = buffer_twin_diff(&obj_a, m, 0, 0);
    if (!d && memcmp(rows_a, base, n)) d = "the back buffer's pixels";
    static char msg[100];
    for (int i = 0; i < N_SPANS && !d; i++)
        for (size_t k = 0; k < span[i].n && !d; k++)
            if (span[i].a[k] != ((uint8_t *)span[i].p)[k]) {
                snprintf(msg, sizeof msg, "%s at %p: wrote 0x%x, original 0x%x", i == 0 ? "the Buffers' sprite" : i == 1 ? "the sprite scratch" : "the text scratch",
                         (void *)((uint8_t *)span[i].p + k), ((uint8_t *)span[i].p)[k], span[i].a[k]);
                d = msg;
            }
    if (!d && to_twins) {
        d = buffer_twin_diff(&ta, &tb, 0, 0);
        if (!d) d = buffer_pixels_diff(&ta, &tb);
    }
    stats_graphic_win_on_paint.calls++;
    if (d) hooks_mismatch(&stats_graphic_win_on_paint, "(%d): %s", P(w), d);
    memcpy(base, rows_before, n);
    *game_win_back_alt = saved_alt;
    for (int i = 0; i < N_SPANS; i++) memcpy(span[i].p, span[i].before, span[i].n);
    for (int i = 0; i < N_SPANS; i++) free(span[i].before), free(span[i].a);
}

// The GraphicWin functions over the scratch windows (their Buffer at 0x444 is the picture the hit test uses): the
// updates show an area (the flip draws the real main Buffer in the real window, put back after each
// check), the soft updates mark one, and the paint draws the window's Buffer into the game's back
// buffer or another Buffer, with the key colour, without, and through each colour table.
static void sweep_graphic(uint32_t hwnd) {
    struct game_buffer *back = game_win_back;
    if (!back->bits || back->dd || nbuf_state <= 0 || !hooks_window_pixels || !hwnd) return;
    *game_win_hwnd = hwnd;  // (the real window: the flip draws in it, put back after each check)
    int w = back->width, h = -back->neg_height;
    static uint8_t table_a[256], table_b[256];
    for (int i = 0; i < 256; i++) table_a[i] = (uint8_t)rnd(), table_b[i] = (uint8_t)rnd();
    struct game_buffer saved_overlay = *game_win_overlay;
    *game_win_overlay = scratch_overlay;
    char *saved_text = *game_flip_text;
    void (*saved_hook)(void) = *game_flip_hook;
    *game_flip_text = NULL, *game_flip_hook = NULL;
    int32_t saved_over = *game_win_overlay_on;
    *game_win_overlay_on = 0;
    // The main Buffer's pixels made up, so the flip the updates make changes the window (which shows the last flip).
    size_t main_bytes = (size_t)(back->pitch < 0 ? -back->pitch : back->pitch) * (size_t)-back->neg_height;
    uint8_t *main_px = back->pitch < 0 ? back->bits + (int64_t)back->pitch * (-(int64_t)back->neg_height - 1) : back->bits;
    uint8_t *main_saved = malloc(main_bytes);
    if (!main_saved) return;
    memcpy(main_saved, main_px, main_bytes);
    for (int k = 0; k < 3; k++) {
        for (size_t i = 0; i < main_bytes; i++) main_px[i] = (uint8_t)rnd();
        *game_win_z_n = rnd_to(5);
        for (int i = 0; i < *game_win_z_n; i++) game_win_zorder[i] = PW(rnd_to(POOL));
        *game_win_opaque = rnd_to(2) && *game_win_z_n ? game_win_zorder[rnd_to(*game_win_z_n)] : any_pool();
        *game_win_back_alt = NULL;
        struct game_win *gw = PW(rnd_to(POOL));
        if (rnd_to(4)) gw->flags2 |= WIN_F2_VISIBLE, gw->parent = NULL;  // (mostly shown: a hidden window does nothing)
        int32_t r[4] = {rnd_to(30) - 8, rnd_to(20) - 8, 0, 0};
        r[2] = r[0] + rnd_to(40), r[3] = r[1] + rnd_to(30);
        check_graphic_win_update(gw, r[0], r[1], r[2] - r[0], r[3] - r[1], 0);
        check_graphic_win_update_2(gw, rnd_to(3) ? r : NULL, 0);
        check_graphic_win_update_3(gw, 0);
        for (int j = 0; j < 4; j++) {
            gw = PW(rnd_to(POOL));
            if (rnd_to(4)) gw->flags2 |= WIN_F2_VISIBLE, gw->parent = NULL;
            int32_t q[4] = {rnd_to(30) - 8, rnd_to(20) - 8, 0, 0};
            q[2] = q[0] + rnd_to(40), q[3] = q[1] + rnd_to(30);
            check_graphic_win_soft_update(gw, q[0], q[1], q[2] - q[0], q[3] - q[1]);
            check_graphic_win_soft_update_2(gw);
            check_graphic_win_soft_update_3(gw, rnd_to(3) ? q : NULL);
        }
        // Painting: into the back buffer and into another Buffer, offset by the redirection's.
        for (int j = 0; j < 8; j++) {
            gw = PW(rnd_to(POOL));
            gw->flags = (gw->flags & ~0x1000000u) | (rnd_to(2) ? 0x1000000u : 0);
            *(uint8_t **)((uint8_t *)gw + 0xa0) = rnd_to(2) ? table_a : NULL;
            *(uint8_t **)((uint8_t *)gw + 0xa4) = rnd_to(2) ? table_b : NULL;
            *game_win_back_alt = NULL;
            game_win_back_off[0] = rnd_to(40) - 20, game_win_back_off[1] = rnd_to(40) - 20;
            int32_t c[4] = {rnd_to(w) - 10, rnd_to(h) - 10, 0, 0};
            c[2] = c[0] + rnd_to(w), c[3] = c[1] + rnd_to(h);
            paint_check(gw, rnd_to(3) ? c : NULL, rnd_to(2));
        }
    }
    memcpy(main_px, main_saved, main_bytes);
    free(main_saved);
    *game_win_overlay = saved_overlay;
    *game_flip_text = saved_text, *game_flip_hook = saved_hook;
    *game_win_overlay_on = saved_over;
}

// The GraphicWin's frame: windows with room for the sprites at 0x9cc to 0x9f4 and the title hook at 0xa0c, four
// strips of their own, and sprites of random sizes (any of them missing now and then), with and without the
// sprite-frame bit (without it the Win's frame is called, which returns at once for a window with no caption or
// bit 0), a caption or not, every strip and the whole frame, with and without an area, and the title's hook.
static void gw_title_hook(struct game_win *w, const char *t, struct game_buffer *b) {
    vrec(0xa0c, w, (int)(uintptr_t)t, (int)(uintptr_t)b, 0);
}
static void sweep_nc_graphic(void) {
    static struct {
        struct game_win w;
        uint8_t rest[0xa40 - sizeof(struct game_win)];
    } gn;
    static struct game_sprite spr[11];
    static uint8_t sdata[11][16 * 16];
    static char title[] = "Title";
    for (int k = 0; k < 40; k++) {
        memset(&gn, 0, sizeof gn);
        gn.w.vtable = stub_vtable;
        uint32_t fl = (uint32_t)rnd() & (WIN_F_CAPTION | 0x400000 | 1);
        if (rnd_to(5)) fl |= 0x10000000;
        else fl &= ~0x11u;
        gn.w.flags = fl;
        gn.w.flags2 = rnd_to(8) ? WIN_F2_FRAME : 0;
        gn.w.border = rnd_to(9), gn.w.caption_h = rnd_to(24), gn.w.bottom = rnd_to(3) == 0 ? -1 : rnd_to(14);
        for (int i = 0; i < 4; i++) gn.w.outer[i] = rnd_to(80) - 10;
        if (rnd_to(2)) gn.w.outer[2] = gn.w.outer[0] + 10 + rnd_to(40), gn.w.outer[3] = gn.w.outer[1] + 10 + rnd_to(40);
        gn.w.title = rnd_to(3) ? title : NULL;
        *(void **)((uint8_t *)&gn + GW_TITLE_HOOK) = rnd_to(2) ? (void *)gw_title_hook : NULL;
        int first = rnd_to(NBUF - 4);
        void **strips[4] = {&gn.w.nc_buf_b4, &gn.w.nc_buf_b8, &gn.w.nc_buf_bc, &gn.w.nc_buf_c0};
        for (int i = 0; i < 4; i++) *strips[i] = rnd_to(8) ? (void *)&nbuf[first + i] : NULL;
        for (int i = 0; i < 11; i++) {
            struct game_sprite *sp = &spr[i];
            memset(sp, 0, sizeof *sp);
            for (int j = 0; j < 256; j++) sdata[i][j] = (uint8_t)rnd_to(rnd_to(2) ? 256 : 4);
            sp->data = sdata[i], sp->key = (uint8_t)rnd_to(256);
            sp->width = 1 + rnd_to(10), sp->height = 1 + rnd_to(10), sp->stride = 16;
            sp->full_w = sp->width + rnd_to(3), sp->full_h = sp->height + rnd_to(3);
            *(struct game_sprite **)((uint8_t *)&gn + GW_TL + 4 * i) = rnd_to(6) ? sp : NULL;
        }
        for (int i = 0; i < 4; i++) scatter(&nbuf[first + i]);
        int32_t area[4] = {rnd_to(40) - 10, rnd_to(40) - 10, 0, 0};
        area[2] = area[0] + rnd_to(40), area[3] = area[1] + rnd_to(40);
        int which = rnd_to(6) - 1;
        check_graphic_win_on_redraw_nc(&gn.w, rnd_to(4) ? NULL : area, which);
        stats_graphic_win_on_redraw_nc.calls++;
        if (rnd_to(2)) check_graphic_win_on_redraw_nc(&gn.w, NULL, -1);
    }
}

// A window copied into another's frame: scratch windows (the pool's, with the Buffer at 0x444) and a window with a
// sprite frame (with its strips, its picture at 0xa08 and flags 0x40000000 and bit 7, which the stand-in for slot 0xf4
// reads), placed near each other so that strips meet, with and without an area, and every `which`.
static void sweep_overlay(uint32_t hwnd) {
    static struct {
        struct game_win w;
        uint8_t rest[0xa40 - sizeof(struct game_win)];
    } on;
    uint32_t saved_hwnd = *game_win_hwnd;
    *game_win_hwnd = hwnd;
    for (int k = 0; k < 40; k++) {
        memset(&on, 0, sizeof on);
        on.w.vtable = stub_vtable;
        on.w.flags = 0x10000000u | (rnd_to(2) ? 0x40000000u : 0) | (rnd_to(2) ? 0x80u : 0);
        if (rnd_to(10) == 0) on.w.flags &= ~0x10000000u;
        on.w.flags2 = rnd_to(12) ? WIN_F2_FRAME : 0;
        int first = rnd_to(NBUF - 4);
        for (int i = 0; i < 4; i++) (&on.w.nc_buf_b4)[i] = &nbuf[first + (rnd_to(6) ? i : rnd_to(4))];
        *(void **)((uint8_t *)&on + 0xa08) = &nbuf[rnd_to(NBUF)];
        on.w.outer[0] = rnd_to(40) - 10, on.w.outer[1] = rnd_to(40) - 10;
        on.w.outer[2] = on.w.outer[0] + 20 + rnd_to(80), on.w.outer[3] = on.w.outer[1] + 20 + rnd_to(80);
        struct game_win *w = PW(rnd_to(POOL));
        if (rnd_to(3) == 0) w->flags ^= 0x80;
        w->flags2 = rnd_to(6) ? (w->flags2 | WIN_F2_FRAME) : (w->flags2 & ~(uint32_t)WIN_F2_FRAME);
        w->outer[0] = on.w.outer[0] + rnd_to(30) - 15, w->outer[1] = on.w.outer[1] + rnd_to(30) - 15;
        w->outer[2] = w->outer[0] + 20 + rnd_to(80), w->outer[3] = w->outer[1] + 20 + rnd_to(80);
        for (int i = 0; i < 4; i++) scatter(&nbuf[first + i]);
        scatter((buffer_t *)((uint8_t *)w + 0x444));
        scatter((buffer_t *)*(void **)((uint8_t *)&on + 0xa08));
        int32_t area[4] = {rnd_to(60) - 20, rnd_to(60) - 20, 0, 0};
        area[2] = area[0] + rnd_to(80) - 10, area[3] = area[1] + rnd_to(80) - 10;
        for (int v = 0; v < 6; v++) {
            check_win_copy_into_frame(w, &on.w, rnd_to(3) ? NULL : area, rnd_to(6) - 1);
            stats_win_copy_into_frame.calls++;
        }
    }
    *game_win_hwnd = saved_hwnd;
}

// ---- Life cycle -----------------------------------------------------------------------------

// Making and unmaking a window: the class under Win (a table of defaults the window's handlers start from,
// copied from the game's data), Win and GraphicWin (a Win with a Buffer of its own at 0x444). The destructors
// free the dialog list's nodes (Win) and the Buffer (GraphicWin), and the classes close through Win_close
// (below, with Win_init: it calls the window's virtual functions and takes it out of the game's focus and
// mouse state). These allocate or free on the game's heap and make Windows objects, so in shadow
// mode the game's own calls run the original, and the sweep checks Chiron's code on twin windows (life.c): a
// constructor's whole object, a destructor's frees of the dialog list's blocks and of what the Buffer holds.
// The original's constructors and destructors set up an exception frame (a handler for the case a
// constructor throws, which nothing in the game does); nothing reads it, so it is not reproduced.
// Differences from OpenSMACX: it has none of these; the names are the inventory's, and the base class's two
// functions (0x62ba80 and 0x62bbf0, unnamed there) are called winbase_ctor and winbase_reset here.

// The 37 words the base class starts from the game's table of defaults (0x9bc080 on); the first four go to
// offsets 4, 0xc, 0x10 and 8, the rest in order from 0x14.
static const struct life_set winbase_set[] = {
    LG(0x4, 0x9bc080), LG(0xc, 0x9bc084), LG(0x10, 0x9bc088), LG(0x8, 0x9bc08c),
    LG(0x14, 0x9bc090), LG(0x18, 0x9bc094), LG(0x1c, 0x9bc098), LG(0x20, 0x9bc09c), LG(0x24, 0x9bc0a0),
    LG(0x28, 0x9bc0a4), LG(0x2c, 0x9bc0a8), LG(0x30, 0x9bc0ac), LG(0x34, 0x9bc0b0), LG(0x38, 0x9bc0b4),
    LG(0x3c, 0x9bc0b8), LG(0x40, 0x9bc0bc), LG(0x44, 0x9bc0c0), LG(0x48, 0x9bc0c4), LG(0x4c, 0x9bc0c8),
    LG(0x50, 0x9bc0cc), LG(0x54, 0x9bc0d0), LG(0x58, 0x9bc0d4), LG(0x5c, 0x9bc0d8), LG(0x60, 0x9bc0dc),
    LG(0x64, 0x9bc0e0), LG(0x68, 0x9bc0e4), LG(0x6c, 0x9bc0e8), LG(0x70, 0x9bc0ec), LG(0x74, 0x9bc0f0),
    LG(0x78, 0x9bc0f4), LG(0x7c, 0x9bc0f8), LG(0x80, 0x9bc0fc), LG(0x84, 0x9bc100), LG(0x88, 0x9bc104),
    LG(0x8c, 0x9bc108), LG(0x90, 0x9bc10c), LG(0x94, 0x9bc110),
};
#define WINBASE_VTABLE 0x66ff34

void *WIN_THIS winbase_ctor(struct game_win *w) {
    w->vtable = (void **)(uintptr_t)game_va(WINBASE_VTABLE);
    life_apply(w, winbase_set, (int)(sizeof winbase_set / sizeof winbase_set[0]));
    return w;
}

// Puts the defaults back (and leaves the vtable).
void WIN_THIS winbase_reset(struct game_win *w) {
    life_apply(w, winbase_set, (int)(sizeof winbase_set / sizeof winbase_set[0]));
}

static const struct life_set win_set[] = {
    LV(0xc8, 0x66ff30), LZ(0xcc), LZ(0xd0), LZ(0xd4), LZ(0xd8), LZ(0xdc), LV(0x0, 0x66fdd0), LS(0xa8),
    LZ(0x3fc), LZ(0x9c), LZ(0xa0), LZ(0xa4), LZ(0xac), LZ(0xb0), LZ(0x134), LZ(0x138), LZ(0x188),
    LZ(0x18c), LZ(0x190), LZ(0x194), LZ(0x198), LZ(0x184), LZ(0xc4), LZ(0xf0), LZ(0x19c), LZ(0x12c),
    LC(0x130, 0x1), LG(0xfc, 0x9b7af0), LG(0x100, 0x696d34), LG(0x114, 0x696d38), LG(0x104, 0x9b7af8),
    LG(0x108, 0x696d3c), LG(0x10c, 0x696d40), LG(0x110, 0x696d44), LG(0x118, 0x696d48), LG(0x11c, 0x696d4c),
    LG(0x120, 0x696d50), LG(0x124, 0x9b7afc), LG(0x128, 0x696d54), LG(0xf8, 0x9b7af4),
    LZ(0xe0), LZ(0xe4), LZ(0xe8), LZ(0xec), LZ(0x43c), LZ(0x440), LZ(0xf4), LZ(0x15c), LZ(0x160),
    LZ(0x164), LZ(0x168), LZ(0x13c), LZ(0x140), LZ(0x144), LZ(0x148), LZ(0x14c), LZ(0x150), LZ(0x154),
    LZ(0x158), LZ(0xb4), LZ(0xc0), LZ(0xbc), LZ(0xb8), LZ(0x16c), LZ(0x170), LZ(0x98), LZ(0x400),
    LZ(0x404), LZ(0x408), LZ(0x40c), LZ(0x410), LZ(0x414), LZ(0x418), LZ(0x41c), LZ(0x420), LZ(0x424),
    LZ(0x428), LZ(0x42c), LZ(0x430), LZ(0x434), LZ(0x438), LC(0x174, 0x1), LC(0x178, 0x1), LC(0x17c, 0x1),
    LC(0x180, 0x1), LC(0x1a0, 0x2),
};
#define WIN_VTABLE 0x66fdd0
#define WIN_DLG_VTABLE 0x66ff30

// A new window: not shown, no children, no parent, itself as its root, the game's default colours, fonts and
// frame sizes; the dialog list empty.
void *WIN_THIS win_ctor(struct game_win *w) {
    winbase_ctor(w);
    life_apply(w, win_set, (int)(sizeof win_set / sizeof win_set[0]));
    return w;
}

// Closes the window (the original's close, which leaves it out of the game's lists and focus), gives back
// the dialog list's nodes and the blocks they hold (unless the list is shared: word 0xdc), and puts the
// base class's defaults back.
void WIN_THIS win_dtor(struct game_win *w) {
    w->vtable = (void **)(uintptr_t)game_va(WIN_VTABLE);
    game_win_close(w);
    *(uint32_t *)((uint8_t *)w + 0xc8) = game_va(WIN_DLG_VTABLE);
    if (w->dlg_head) {
        if (!*(int32_t *)((uint8_t *)w + 0xdc)) {
            for (int i = 0; i < w->dlg_count; i++) {
                struct game_win_node *n = w->dlg_head;
                w->dlg_cur = n->next;
                void *payload = (void *)(uintptr_t)n->pad8;
                if (payload) game_free(payload);
                n->pad8 = 0;
                game_free(n);
                w->dlg_head = w->dlg_cur;
            }
        }
        w->dlg_head = NULL, w->dlg_index = 0, w->dlg_count = 0;
    }
    w->dlg_index = 0;
    w->vtable = (void **)(uintptr_t)game_va(WINBASE_VTABLE);
    winbase_reset(w);
}

static const struct life_set graphic_win_set[] = {
    LZ(0xa10), LZ(0x134), LZ(0x138), LZ(0x9cc), LZ(0x9d0), LZ(0x9d4), LZ(0x9d8), LZ(0x9dc), LZ(0x9e0),
    LZ(0x9e4), LZ(0x9e8), LZ(0x9ec), LZ(0x9f0), LZ(0x9f4), LZ(0x9f8), LZ(0x9fc), LZ(0xa00), LZ(0xa04),
};
#define GRAPHIC_WIN_VTABLE 0x66fc50
#define GRAPHIC_WIN_BUFFER_VTABLE 0x66fc48
enum { GW_BUFFER = 0x444, GW_OWN = 0x9cc, GW_PIC = 0xa08, GW_PALETTE = 0xa0c };

static struct game_buffer *gw_buffer(struct game_win *w) { return (struct game_buffer *)((uint8_t *)w + GW_BUFFER); }

// A new GraphicWin: a Win with an empty Buffer, and its own fields cleared (word 0xa0c is the game's current
// palette serial).
void *WIN_THIS graphic_win_ctor(struct game_win *w) {
    win_ctor(w);
    buffer_ctor(gw_buffer(w));
    w->vtable = (void **)(uintptr_t)game_va(GRAPHIC_WIN_VTABLE);
    gw_buffer(w)->vtable = (void **)(uintptr_t)game_va(GRAPHIC_WIN_BUFFER_VTABLE);
    life_apply(w, graphic_win_set, (int)(sizeof graphic_win_set / sizeof graphic_win_set[0]));
    *(uint32_t *)((uint8_t *)w + GW_PIC) = 0;
    *(uint32_t *)((uint8_t *)w + GW_PALETTE) = *(uint32_t *)game_at(0x9b33c0);
    return w;
}

void WIN_THIS graphic_win_dtor(struct game_win *w) {
    w->vtable = (void **)(uintptr_t)game_va(GRAPHIC_WIN_VTABLE);
    gw_buffer(w)->vtable = (void **)(uintptr_t)game_va(GRAPHIC_WIN_BUFFER_VTABLE);
    *(uint32_t *)((uint8_t *)w + 0xa10) = 0;
    buffer_dtor(gw_buffer(w));
    win_dtor(w);
}

// Closes the Win and the Buffer, clears the fields, and deletes the object at 0xa08 (through its first
// virtual function, which is its deleting destructor) if there is one.
void WIN_THIS graphic_win_close(struct game_win *w) {
    game_win_close(w);
    buffer_close(gw_buffer(w));
    void *pic = *(void **)((uint8_t *)w + GW_PIC);
    life_apply(w, graphic_win_set, (int)(sizeof graphic_win_set / sizeof graphic_win_set[0]));
    *(uint32_t *)((uint8_t *)w + GW_PALETTE) = *(uint32_t *)game_at(0x9b33c0);
    if (pic) {
        ((void(WIN_THIS *)(void *, int))(*(void ***)pic)[0])(pic, 1);
        *(void **)((uint8_t *)w + GW_PIC) = NULL;
    }
}

// Sets the window's caption: the old copy is freed, the text copied onto the game's heap (not at all for a null text),
// and a visible window (one whose parent, if any, is visible too) is asked, through its slot 0x128 (with 0 and -1), to
// draw its frame again. No memory leaves the caption empty and the window alone. Differences from OpenSMACX: it
// has none of these.
void WIN_THIS win_set_caption(struct game_win *w, const char *text) {
    if (w->title) game_free((void *)(uintptr_t)w->title), w->title = NULL;
    if (text) {
        char *copy = game_mem_get(strlen(text) + 1);
        w->title = copy;
        if (!copy) return;
        copy[0] = 0;
        strcpy(copy, text);
    }
    if (!(w->flags2 & WIN_F2_VISIBLE)) return;
    if (w->parent && !win_is_visible(w->parent)) return;
    ((void(WIN_THIS *)(void *, int, int))w->vtable[0x128 / 4])(w, 0, -1);
}

void *WIN_THIS scroll_ctor(void *bar);  // scroll.c

// ---- Win_close, Win_init, GraphicWin_init and PullDown_init -----------------------------------------------
//
// Closing a window takes it out of everything the game keeps of it (the press, capture, hover and bubble
// text globals, its parent's list of children or the list of top-level windows, its parent's dialog list),
// closes its children, gives back its scroll bars, caption buttons, caption and frame buffers, and puts the
// fields back as the constructor leaves them. Init closes first, then lays the window out: the four frame
// buffers (0xb4 to 0xc0, each a Buffer made with operator new), the caption's sizes, the position (a negative
// x or y counts from the right or bottom of the parent's inner rectangle, or of the screen), the inner, outer
// and clip rectangles, the caption copied onto the heap, the frame laid out through slot 0x108, the owner
// (nc_owner, word 0xf0) asked through its slot 0x168, the scroll bars (made through slot 0x18 or with the Scroll
// class), the entry in the parent's dialog list and the window's place in its parent's list or the top-level
// list. GraphicWin_init adds the Buffer: its own copy of eleven of the game's frame picture pointers (when flag
// 0x30000000 asks for them), a picture object at 0xa08 (flag 0x40000000), and a Buffer of the client size.
// PullDown_init is GraphicWin_init with a menu's own words set around it.
//
// Quirks kept from the original: init's check against 150 children tests the window's own child count (not the
// parent's), which is 0 after the close, so only the check against 40 top-level windows can ever stop the game;
// a window with flag 0x100000 and without 0x1000 reads its parent's flags, so one with no parent crashes there;
// a failed allocation of a dialog node leaves the ring with a hole where the new node was to go; slot 0xf4 is
// asked only when one of the top four flag bits is set. Differences from OpenSMACX and Thinker: none of them has
// the layout; the inventory names the functions, and Win_init's argument list (x, y, width, height, caption,
// flags, parent, owner and a pointer to the three caption sizes) is read from the disassembly. Left out: the
// exception frame the original sets up for a constructor that throws, which nothing reads.
//
// Checks. A sweep builds twin windows (life.c) with a stand-in vtable whose calls are logged and compared, and
// compares the window, its parent, children and the lists the game keeps, the heap blocks made and freed, and
// the globals the functions write. Not covered: the Scroll class's own creation in Win_init (flags 4 and 8 without
// 0x40; real scroll bars draw when shown), the bubble text branch of Win_close (it redraws the screen), the
// caption's redraw (slot 0x128 with the window shown, which a freshly closed window never is), and the stops the
// game makes when it has too many windows (MessageBox and exit).

static void free_dialog_list(struct game_win *w) {
    if (!w->dlg_head) {
        w->dlg_index = 0;
        return;
    }
    if (!*(int32_t *)((uint8_t *)w + 0xdc)) {
        for (int i = 0; i < w->dlg_count; i++) {
            struct game_win_node *n = w->dlg_head;
            w->dlg_cur = n->next;
            void *payload = (void *)(uintptr_t)n->pad8;
            if (payload) game_free(payload);
            n->pad8 = 0;
            game_free(n);
            w->dlg_head = w->dlg_cur;
        }
    }
    w->dlg_head = NULL, w->dlg_index = 0, w->dlg_count = 0;
    w->dlg_index = 0;
}

// The fifteen words that get the game's defaults back (the same ones the constructor's table sets).
static const struct life_set win_defaults_set[] = {
    LZ(0x12c), LC(0x130, 1), LG(0xfc, 0x9b7af0), LG(0x100, 0x696d34), LG(0x104, 0x9b7af8), LG(0x108, 0x696d3c),
    LG(0x10c, 0x696d40), LG(0x110, 0x696d44), LG(0x114, 0x696d38), LG(0x118, 0x696d48), LG(0x11c, 0x696d4c),
    LG(0x120, 0x696d50), LG(0x124, 0x9b7afc), LG(0x128, 0x696d54), LG(0xf8, 0x9b7af4),
};
// What a close leaves once the caption buttons are gone: the words the constructor zeroes, or sets to 1 or 2.
static const struct life_set win_closed_set[] = {
    LZ(0xf4), LZ(0x98), LC(0x174, 1), LC(0x178, 1), LC(0x17c, 1), LC(0x180, 1),
    LZ(0x15c), LZ(0x160), LZ(0x164), LZ(0x168), LZ(0x13c), LZ(0x140), LZ(0x144), LZ(0x148), LZ(0x14c), LZ(0x150), LZ(0x154),
    LZ(0x158), LZ(0x16c), LZ(0x170), LZ(0x400), LZ(0x404), LZ(0x408), LZ(0x40c), LZ(0x410), LZ(0x414), LZ(0x418),
    LZ(0x41c), LZ(0x420), LZ(0x424), LZ(0x428), LZ(0x42c), LZ(0x430), LZ(0x434), LZ(0x438), LC(0x1a0, 2), LZ(0x9c),
    LZ(0xa0), LZ(0xa4), LZ(0x3fc),
};

// A scroll bar the window keeps (word `off`): given the window's own mapping (slot 0x1c, if flag 0x40), then deleted.
static void close_bar(struct game_win *w, int off) {
    void **slot = (void **)((uint8_t *)w + off);
    if (!*slot) return;
    if (w->flags & 0x40) *slot = (void *)(intptr_t)vcall1(w, 0x1c, (int)(intptr_t)*slot);
    if (*slot) {
        ((void(THISCALL *)(void *, int))(*(void ***)*slot)[0])(*slot, 1);
        *slot = NULL;
    }
}
// A Buffer, caption button or other object the window owns (word `off`): deleted through its first virtual function.
static void delete_owned(struct game_win *w, int off) {
    void **slot = (void **)((uint8_t *)w + off);
    if (!*slot) return;
    ((void(THISCALL *)(void *, int))(*(void ***)*slot)[0])(*slot, 1);
    *slot = NULL;
}

void WIN_THIS win_close(struct game_win *w) {
    // A press that went down in this window or in one below it is forgotten: the first of the two press words that is
    // set is the one asked about.
    struct game_win *first = *game_win_press1 ? *game_win_press1 : *game_win_press2;
    int forget = first == w;
    if (!forget && first) {
        for (int i = 0; i < w->n_children; i++)
            if (w->children[i] == first || win_is_descendant(w->children[i], first)) {
                forget = 1;
                break;
            }
    }
    if (forget) *game_win_press1 = NULL, *game_win_press2 = NULL;
    if (*game_win_capture == w) {
        *game_win_capture = NULL;
        vcall0(w, VT_CAPTURE_LOST);
    }
    if (*game_win_hover == w) *game_win_hover = NULL;
    struct game_win **bubble_win = game_at(0x9b7a4c);
    if (*bubble_win == w && *game_flip_text) {
        *bubble_win = NULL, *game_flip_text = NULL;
        game_flip_restore(game_flip_box, 0);
        win_flip(game_flip_box);
    }
    struct game_win **draw_win = game_at(0x9b7a7c);
    if (*draw_win == w) *draw_win = NULL;
    vcall0(w, WIN_VT_HIDE);
    close_bar(w, 0x43c);
    close_bar(w, 0x440);
    // Out of its parent's dialog list: the node is looked for from the list's cursor, which moves along as it looks.
    if (w->flags & 0x100000) {
        struct game_win *p = w->parent;
        if (p && p->dlg_head) {
            int found = 0;
            for (int i = 0; i < p->dlg_count; i++) {
                if (p->dlg_cur->win == w) {
                    found = 1;
                    break;
                }
                p->dlg_cur = p->dlg_cur->next;
            }
            if (found) {
                struct game_win_node *n = p->dlg_cur;
                n->next->prev = n->prev;
                n->prev->next = n->next;
                if (n == p->dlg_head) p->dlg_head = n->next;
                p->dlg_cur = n->next;
                if (!*(int32_t *)((uint8_t *)p + 0xdc)) {
                    void *payload = (void *)(uintptr_t)n->pad8;
                    if (payload) game_free(payload);
                    n->pad8 = 0;
                    game_free(n);
                }
                p->dlg_count--;
            }
            if (p->dlg_count == 0) p->dlg_head = NULL;
            p->dlg_index = p->dlg_count - 1;
        }
    }
    // The children close themselves, each taking itself out of this list: the loop's counter goes on while the list gets
    // shorter, as in the original, and the count is read again each time round.
    for (int i = 0; i < w->n_children; i++) win_close(w->children[i]);
    if (w->parent) win_remove_child(w->parent, w);
    else win_remove_parent(w);
    w->parent = NULL;
    *(uint32_t *)((uint8_t *)w + 0x19c) = 0;
    *(uint32_t *)((uint8_t *)w + 0xac) = 0;
    *(uint32_t *)((uint8_t *)w + 0xb0) = 0;
    *(uint32_t *)((uint8_t *)w + 0x134) = 0;
    *(uint32_t *)((uint8_t *)w + 0x138) = 0;
    w->root = w;
    for (int off = 0x184; off <= 0x198; off += 4) *(uint32_t *)((uint8_t *)w + off) = 0;
    *game_dc = NULL;
    w->nc_owner = NULL;
    free_dialog_list(w);
    life_apply(w, win_defaults_set, (int)(sizeof win_defaults_set / sizeof win_defaults_set[0]));
    if (w->title) game_free((void *)(uintptr_t)w->title), w->title = NULL;
    delete_owned(w, 0xe4);
    delete_owned(w, 0xe8);
    delete_owned(w, 0xec);
    void *b4 = w->nc_buf_b4;
    life_apply(w, win_closed_set, (int)(sizeof win_closed_set / sizeof win_closed_set[0]));
    if (b4) {
        ((void(THISCALL *)(void *, int))(*(void ***)b4)[0])(b4, 1);
        w->nc_buf_b4 = NULL;
    }
    delete_owned(w, 0xb8);
    delete_owned(w, 0xbc);
    delete_owned(w, 0xc0);
    winbase_reset(w);
}

// A frame buffer: a Buffer on the game's heap, made with operator new.
static void *new_frame_buffer(void) {
    void *b = game_operator_new(0x588);
    return b ? buffer_ctor(b) : NULL;
}
// A node of a dialog list, or its payload: from the arena the list lives in (word 0xdc of the owner), or the heap.
static void *dialog_alloc(struct game_win *owner, int size) {
    void *arena = *(void **)((uint8_t *)owner + 0xdc);
    return arena ? game_string_alloc(arena, size) : game_mem_get((size_t)size);
}
static void stop_with(uint32_t text, uint32_t title) {
    game_message_box_a(NULL, game_at(text), game_at(title), 0);
    game_exit(4);
}

// Puts `w` at the end of its parent's dialog list (the ring of windows the list takes the focus through), and makes it
// the list's current one. The original's order is kept, including a hole left in the ring when a node can't be made.
static void dialog_list_add(struct game_win *parent, struct game_win *w) {
    struct game_win_node *n;
    if (parent->dlg_count == 0) {
        n = dialog_alloc(parent, 0x1c);
        parent->dlg_head = n;
        if (n) {
            n->win = w;
            n->next = n, n->prev = n;
            n->pad8 = (int32_t)(uintptr_t)dialog_alloc(parent, 4);
            if (n->pad8) {
                parent->dlg_cur = n;
                parent->dlg_index = parent->dlg_count;
                parent->dlg_count++;
            }
        }
    } else {
        n = dialog_alloc(parent, 0x1c);
        struct game_win_node *last = parent->dlg_head->prev;
        last->next = n;
        if (n) {
            n->prev = last;
            n->next = parent->dlg_head;
            parent->dlg_head->prev = n;
            parent->dlg_cur = n;
            n->win = w;
            n->pad8 = (int32_t)(uintptr_t)dialog_alloc(parent, 4);
            if (n->pad8) {
                parent->dlg_index = parent->dlg_count;
                parent->dlg_count++;
            }
        }
    }
    if (parent->dlg_count - 1 >= 0) {
        parent->dlg_index = 0;
        parent->dlg_cur = parent->dlg_head;
    }
    w->parent = parent;
}

int WIN_THIS win_init(struct game_win *w, int x, int y, int cw, int ch, const char *title, uint32_t flags, struct game_win *parent,
                      struct game_win *owner, const int32_t *caption) {
    win_close(w);
    if (caption) w->caption_h = caption[0], w->border = caption[1], w->bottom = caption[2];
    *(uint32_t *)((uint8_t *)w + 0x1a0) |= 2;
    if (!(w->nc_buf_b4 = new_frame_buffer())) return 4;
    if (!(w->nc_buf_b8 = new_frame_buffer())) return 4;
    if (!(w->nc_buf_bc = new_frame_buffer())) return 4;
    if (!(w->nc_buf_c0 = new_frame_buffer())) return 4;
    if ((flags & 0xf0000000) && !vcall0(w, 0xf4)) return 3;
    if (flags & 0x80000) flags |= 0x20;
    if (x < 0) x += parent ? parent->inner[2] - parent->inner[0] - cw : *game_win_screen_w - cw;
    if (y < 0) y += (parent ? parent->inner[3] - parent->inner[1] : *game_win_screen_h) - ch;
    w->flags = flags;
    w->nc_owner = owner;
    w->parent = parent;
    if (title && title[0] && !(flags & 0x400080)) {
        flags |= 0x10;
        w->flags = flags;
    }
    if (parent) {
        if (w->n_children == 150) {
            win_close(w);
            stop_with(0x696d80, 0x696d60);
        }
        int n = parent->n_children;
        if (w->flags & 0x2000000) {
            parent->children[n] = w;
        } else {
            for (int k = n; k > 0; k--) parent->children[k] = parent->children[k - 1];
            parent->children[0] = w;
        }
        parent->n_children++;
    } else {
        int n = *game_win_parent_n;
        if (n == 0x28) {
            win_close(w);
            stop_with(0x696db4, 0x696d94);
        }
        if (w->flags & 0x2000000) {
            game_win_parents[n] = w;
        } else {
            for (int k = n; k > 0; k--) game_win_parents[k] = game_win_parents[k - 1];
            game_win_parents[0] = w;
        }
        *game_win_parent_n = n + 1;
    }
    if (!(w->flags & 0x80) && (w->flags & 0x1d)) {
        w->flags2 |= 2;
    } else {
        w->flags2 &= ~2u;
        w->flags &= 0xfffffbeeu;
    }
    // The rectangles: `outer` is where the frame lies, `inner` the client area.
    if (!(w->flags & 0x800) && (w->flags2 & 2)) {
        w->outer[0] = x, w->outer[1] = y, w->outer[2] = x + cw, w->outer[3] = y + ch;
        memcpy(w->inner, w->outer, sizeof w->inner);
        win_nonclient_to_client_2(w, w->inner);
        int ox = w->outer[0], oy = w->outer[1];
        w->inner[0] -= ox, w->inner[2] -= ox;
        w->inner[1] -= oy, w->inner[3] -= oy;
    } else {
        w->inner[0] = x, w->inner[1] = y, w->inner[2] = x + cw, w->inner[3] = y + ch;
        if (w->flags2 & 2) {
            memcpy(w->outer, w->inner, sizeof w->outer);
            win_client_to_nonclient_2(w, w->outer);
            int ox = w->outer[0], oy = w->outer[1];
            w->inner[0] -= ox, w->inner[2] -= ox;
            w->inner[1] -= oy, w->inner[3] -= oy;
        }
    }
    memcpy(w->clip, w->inner, sizeof w->clip);
    if (w->title) game_free((void *)(uintptr_t)w->title), w->title = NULL;
    if (title) {
        char *copy = game_mem_get(strlen(title) + 1);
        w->title = copy;
        if (copy) {
            copy[0] = 0;
            strcpy(copy, title);
            if ((w->flags2 & WIN_F2_VISIBLE) && (!w->parent || win_is_visible(w->parent))) vcall2(w, 0x128, 0, -1);
        }
    }
    vcall0(w, VT_LAYOUT);
    if (owner) {
        int r = vcall1(owner, 0x168, (int)(intptr_t)w);
        if (r) {
            win_close(w);
            return r;
        }
    }
    uint32_t f = w->flags;
    if (f & 0x10) w->title_h = w->caption_h;
    else if (!(f & 0x20)) w->title_h = w->border;
    w->resize_border = (f & 0x400) ? w->border : 0;
    if (f & 4) {
        if (f & 0x40) w->scroll_h = (struct game_win_scroll *)(intptr_t)vcall0(w, 0x18);
        if (!w->scroll_h) {
            void *bar = game_operator_new(0x214c);
            if (bar) bar = scroll_ctor(bar);
            w->scroll_h = bar;
            if (!bar) return 4;
            int sb = *game_win_nc_margin;
            int height = w->inner[3] - sb;
            int width = w->inner[2] - w->inner[0] - ((w->flags & 8) ? sb : 0);
            game_scroll_init_horz_nc(bar, 0, height, width, w, -2);
            vcall1((struct game_win *)bar, WIN_VT_SHOW, 0);
        }
    }
    if (w->flags & 8) {
        if (w->flags & 0x40) w->scroll_v = (struct game_win_scroll *)(intptr_t)vcall0(w, 0x18);
        if (!w->scroll_v) {
            void *bar = game_operator_new(0x214c);
            if (bar) bar = scroll_ctor(bar);
            w->scroll_v = bar;
            if (!bar) return 4;
            int sb = *game_win_nc_margin;
            int right = w->inner[2] - sb;
            int height = w->inner[3] - w->inner[1] - ((w->flags & 4) ? sb : 0);
            game_scroll_init_vert_nc(bar, right, 0, height, w, -1);
            vcall1((struct game_win *)bar, WIN_VT_SHOW, 0);
        }
    }
    game_win_do_caption_buttons(w);
    *game_win_cur = w;
    f = w->flags;
    if ((f & 0x100000) && !(f & 0x200000) && !(w->flags2 & WIN_F2_BUSY) && !((f & 0x1000) && !parent) &&
        !(parent->flags & 0x1000))
        dialog_list_add(parent, w);
    w->flags2 |= 4;
    return 0;
}

int WIN_THIS graphic_win_init(struct game_win *w, int x, int y, int cw, int ch, const char *title, uint32_t flags,
                              struct game_win *parent, struct game_win *owner, const int32_t *caption) {
    graphic_win_close(w);
    if (flags & 0x30000000) {
        static const struct life_set own[] = {
            LG(0x9cc, 0x9b3394), LG(0x9d4, 0x9b339c), LG(0x9d0, 0x9b3398), LG(0x9d8, 0x9b33a0), LG(0x9e8, 0x9b33a4), LG(0x9e4, 0x9b33a8),
            LG(0x9dc, 0x9b33ac), LG(0x9e0, 0x9b33b0), LG(0x9ec, 0x9b33b8), LG(0x9f0, 0x9b33b4), LG(0x9f4, 0x9b33bc),
        };
        life_apply(w, own, (int)(sizeof own / sizeof own[0]));
    }
    if (flags & 0x40000000) *(void **)((uint8_t *)w + GW_PIC) = new_frame_buffer();
    int r = win_init(w, x, y, cw, ch, title, flags, parent, owner, caption);
    if (r) return r;
    *(void **)((uint8_t *)w + 0x448) = w;
    game_graphic_win_compute_min_size(w);
    int32_t wd = cw, ht = ch;
    if (!(flags & 0x800)) {
        win_nonclient_to_client(w, &wd, &ht);
    } else {
        int sb = *game_win_nc_margin;
        if (w->flags & 8) wd += sb;
        if (w->flags & 4) ht += sb;
    }
    r = buffer_init(gw_buffer(w), wd, ht, 0, NULL);
    if (r) return r;
    buffer_sync_to_palette(gw_buffer(w), *game_palette_copy);
    return 0;
}

// A menu's window: the game's default menu colours if none are set yet (word 0x9b7b68), the menu's own word at 0xf30, a
// window with no size, and word 0x964 cleared. Answers GraphicWin_init's answer.
int WIN_THIS pulldown_init(struct game_win *w, int arg) {
    uint32_t *colour = game_at(0x9b7b68);
    if (!*colour) *colour = *(uint32_t *)game_at(0x9bb484);
    *(int32_t *)((uint8_t *)w + 0xf30) = arg;
    int r = graphic_win_init(w, 0, 0, 0, 0, NULL, 0, NULL, NULL, NULL);
    *(int32_t *)((uint8_t *)w + 0x964) = 0;
    return r;
}

// ---- Life cycle: shadow mode and the sweep ----

SHADOW_PASS_T(WIN, void, win_set_caption, (struct game_win * w, const char *text), (w, text))
SHADOW_PASS_T(WIN, void *, winbase_ctor, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void, winbase_reset, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void *, win_ctor, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void, win_dtor, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void *, graphic_win_ctor, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void, graphic_win_dtor, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void, graphic_win_close, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, void, win_close, (struct game_win * w), (w))
SHADOW_PASS_T(WIN, int, win_init,
              (struct game_win * w, int x, int y, int cw, int ch, const char *title, uint32_t flags, struct game_win *parent,
               struct game_win *owner, const int32_t *caption),
              (w, x, y, cw, ch, title, flags, parent, owner, caption))
SHADOW_PASS_T(WIN, int, graphic_win_init,
              (struct game_win * w, int x, int y, int cw, int ch, const char *title, uint32_t flags, struct game_win *parent,
               struct game_win *owner, const int32_t *caption),
              (w, x, y, cw, ch, title, flags, parent, owner, caption))
SHADOW_PASS_T(WIN, int, pulldown_init, (struct game_win * w, int arg), (w, arg))

LIFE_FN(win_set_caption, orig_win_set_caption(o, x), win_set_caption(o, x))
LIFE_FN(winbase_ctor, orig_winbase_ctor(o), winbase_ctor(o))
LIFE_FN(winbase_reset, orig_winbase_reset(o), winbase_reset(o))
LIFE_FN(win_ctor, orig_win_ctor(o), win_ctor(o))
LIFE_FN(win_dtor, orig_win_dtor(o), win_dtor(o))
LIFE_FN(graphic_win_ctor, orig_graphic_win_ctor(o), graphic_win_ctor(o))
LIFE_FN(graphic_win_dtor, orig_graphic_win_dtor(o), graphic_win_dtor(o))
LIFE_FN(graphic_win_close, orig_graphic_win_close(o), graphic_win_close(o))

static const struct life_slot win_slots[] = {{0xcc, -1}, {0xd0, -1}};
static const struct life_slot gwin_slots[] = {{0xcc, -1}, {0xd0, -1}, {GW_PIC, -1}};
static const struct life_class winbase_class = {"WinBase", 0x98};
static const struct life_class win_class = {"Win", 0x444, win_slots, 2, 0, 0, {0x20}};
static const struct life_class gwin_class = {"GraphicWin", 0xa14, gwin_slots, 3, GW_BUFFER, GW_OWN, {0x20, 0x9d0}};

// What the picture object at 0xa08 does when it is deleted: counted (the object is a static one).
static uint32_t pic_deleted;
static void WIN_THIS fake_pic_delete(void *o, int flag) { pic_deleted++; }
static void *fake_pic_vtable[1] = {(void *)fake_pic_delete};
static void *fake_pic = &fake_pic_vtable;

// Two Wins of the class made alike by the original: filled with `fill`, constructed, and given a dialog list
// of `nodes` nodes (each holding a block) of their own.
static void build_win_pair(const struct life_class *c, int graphic, uint8_t fill, int nodes) {
    life_fill(c, life_a, life_b, fill);
    for (int i = 0; i < 2; i++) {
        struct game_win *w = (struct game_win *)(i ? life_b : life_a);
        if (graphic) orig_graphic_win_ctor(w); else orig_win_ctor(w);
        for (int k = 0; k < 2; k++)
            if (c->scribble[k]) *(uint32_t *)((uint8_t *)w + c->scribble[k]) = 0x13572468u;
        struct game_win_node *first = NULL, *prev = NULL;
        for (int k = 0; k < nodes; k++) {
            struct game_win_node *n = game_mem_get(sizeof *n);
            memset(n, 0, sizeof *n);
            n->pad8 = (int32_t)(uintptr_t)game_mem_get(8);
            if (!first) first = n;
            if (prev) prev->next = n, n->prev = prev;
            prev = n;
        }
        if (first) first->prev = prev, prev->next = first;
        w->dlg_head = first, w->dlg_cur = first, w->dlg_count = nodes, w->dlg_index = 0;
    }
}

// The caption setter: the caption's block is compared as a string, and the frame's redraw (slot 0x128 of a stand-in vtable)
// as the calls made and what they were given, which stand in the two counters the check compares as globals.
static const struct life_slot caption_slots[] = {{0xcc, -1}, {0xd0, -1}, {0xe0, 0}};
static const struct life_class caption_class = {"Win caption", 0x444, caption_slots, 3};
static uint32_t caption_calls, caption_args;
static void WIN_THIS caption_redraw(void *w, int a, int b) {
    caption_calls++;
    // (the window as which of the two twins it is, not its address)
    caption_args = caption_args * 31 + (uint32_t)((uint8_t *)w == life_a ? 1 : (uint8_t *)w == life_b ? 1 : 2) + (uint32_t)a * 7 + (uint32_t)b * 13;
}
static void *caption_vtable[0x180 / 4];
static uint8_t caption_parent[0x444];

static void life_sweep_caption(void) {
    uint32_t *globals[2] = {&caption_calls, &caption_args};
    static const char *const texts[] = {"A caption", "", "a much longer caption than the one before, to need more room", NULL};
    caption_vtable[0x128 / 4] = (void *)caption_redraw;
    for (int round = 0; round < 24; round++) {
        life_build(&caption_class, (uint8_t)(round * 29 + 3), life_orig_win_ctor, NULL);
        if (round & 1) life_text(0xe0, "the old caption");
        for (int i = 0; i < 2; i++) {
            uint8_t *o = i ? life_b : life_a;
            *(void **)o = caption_vtable;
            *(uint32_t *)(o + 0x9c) = (round & 2) ? 1 : 0;
            *(void **)(o + 0xc4) = (round & 4) ? (void *)caption_parent : NULL;
        }
        *(uint32_t *)(caption_parent + 0x9c) = (round & 8) ? 0 : 1;
        caption_calls = caption_args = 0;
        life_run(&stats_win_set_caption, &caption_class, life_orig_win_set_caption, life_ours_win_set_caption, (void *)(uintptr_t)texts[round % 4],
                 globals, 2);
        // Give back what the runs made (a caption each).
        for (int i = 0; i < 2; i++) {
            void *c = *(void **)((i ? life_b : life_a) + 0xe0);
            if (c) game_free(c);
        }
    }
}

static void life_sweep_win(void) {
    static uint32_t *no_globals[1];
    uint32_t *pic_globals[1] = {&pic_deleted};
    if (!game_mem_get || *game_ddraw || !hooks_dc_same) return;
    for (int round = 0; round < 6; round++) {
        uint8_t fill = (uint8_t)rnd();
        life_fill(&winbase_class, life_a, life_b, fill);
        life_run(&stats_winbase_ctor, &winbase_class, life_orig_winbase_ctor, life_ours_winbase_ctor, NULL, no_globals, 0);
        life_fill(&winbase_class, life_a, life_b, fill);
        life_run(&stats_winbase_reset, &winbase_class, life_orig_winbase_reset, life_ours_winbase_reset, NULL, no_globals, 0);
        life_fill(&win_class, life_a, life_b, fill);
        life_run(&stats_win_ctor, &win_class, life_orig_win_ctor, life_ours_win_ctor, NULL, no_globals, 0);
        // The destructor, with a dialog list of 0 to 3 nodes, and on the last round one that is shared.
        int nodes = round % 4;
        build_win_pair(&win_class, 0, fill, nodes);
        if (round == 5) *(int32_t *)(life_a + 0xdc) = *(int32_t *)(life_b + 0xdc) = 1;
        void *kept_a = ((struct game_win *)life_a)->dlg_head, *kept_b = ((struct game_win *)life_b)->dlg_head;
        life_run(&stats_win_dtor, &win_class, life_orig_win_dtor, life_ours_win_dtor, NULL, no_globals, 0);
        if (round == 5) {
            // A shared list is left alone by the destructor: free it here.
            for (int i = 0; i < 2; i++) {
                struct game_win_node *head = i ? kept_b : kept_a, *n = head;
                for (int k = 0; k < nodes && n; k++) {
                    struct game_win_node *next = n->next;
                    game_free((void *)(uintptr_t)n->pad8), game_free(n);
                    n = next;
                }
            }
        }
        // GraphicWin: the constructor, then the destructor and the close with a Buffer holding a DIB.
        life_fill(&gwin_class, life_a, life_b, fill);
        life_run(&stats_graphic_win_ctor, &gwin_class, life_orig_graphic_win_ctor, life_ours_graphic_win_ctor, NULL, no_globals, 0);
        {
            const char *d = buffer_twin_diff(gw_buffer((struct game_win *)life_a), gw_buffer((struct game_win *)life_b), 0, 0);
            if (d) hooks_mismatch(&stats_graphic_win_ctor, " (Buffer): %s", d);
        }
        for (int which = 0; which < 2; which++) {
            build_win_pair(&gwin_class, 1, fill, nodes);
            for (int i = 0; i < 2; i++) {
                struct game_win *w = (struct game_win *)(i ? life_b : life_a);
                game_buffer_init(gw_buffer(w), 4 + round, 3 + round, 0, 0);
                if (which && (round & 1)) *(void **)((uint8_t *)w + GW_PIC) = &fake_pic;
            }
            pic_deleted = 0;
            if (which) {
                life_run(&stats_graphic_win_close, &gwin_class, life_orig_graphic_win_close, life_ours_graphic_win_close, NULL, pic_globals, 1);
            } else {
                life_run(&stats_graphic_win_dtor, &gwin_class, life_orig_graphic_win_dtor, life_ours_graphic_win_dtor, NULL, no_globals, 0);
            }
            {
                const char *d = buffer_twin_diff(gw_buffer((struct game_win *)life_a), gw_buffer((struct game_win *)life_b), 0, 0);
                if (d) hooks_mismatch(which ? &stats_graphic_win_close : &stats_graphic_win_dtor, " (Buffer): %s", d);
            }
            // Give back what the close left (the destructor leaves nothing).
            if (which) orig_graphic_win_dtor((struct game_win *)life_a), orig_graphic_win_dtor((struct game_win *)life_b);
        }
    }
}

// ---- Win_close, Win_init, GraphicWin_init and PullDown_init: the sweeps -----------------------------------
//
// Twin windows (life.c), each class built as a block of several windows, so that what the functions do to a window's
// parent, children and the nodes of dialog lists is compared too: the original's Win_close and Win_init on one block,
// Chiron's on the other. Every virtual function the windows call goes to a stand-in that logs the call (which slot, which
// window of the twin, the arguments) and answers from a table; the log is compared as a global. The globals the
// functions use (the press, capture, hover, bubble and draw windows, the cached DC, the top-level list) are set up inside
// the two runs, from offsets into the block, so each run sees its own windows named; the globals are compared after each,
// windows by where in the block they lie.

static struct {
    uint32_t hash;
    int32_t n;
} wlog;
static uint32_t static_deleted;
static void THISCALL static_bar_delete(void *o, int flag) {
    (void)o, (void)flag;
    static_deleted++;
}
static void *static_bar_vt[1] = {(void *)static_bar_delete};
static void *static_bar = &static_bar_vt;  // an object (its first word is its vtable) that isn't on the heap
static int wanswer_copy, wanswer_owner;

// Which of the twins' windows an address is, by offset into its block; anything else by its own address.
static int wid(const void *o) {
    const uint8_t *p = o;
    if (p >= life_a && p < life_a + LIFE_MAX) return (int)(p - life_a);
    if (p >= life_b && p < life_b + LIFE_MAX) return (int)(p - life_b);
    return 0x7fff0000 + (int)((uintptr_t)o & 0xfffff);
}
static void wrec(int slot, const void *o, int a, int b) {
    wlog.hash = (((wlog.hash * 31u + (uint32_t)slot) * 31u + (uint32_t)wid(o)) * 31u + (uint32_t)a) * 31u + (uint32_t)b * 7u;
    wlog.n++;
}
static int THISCALL wst_hide(void *o) { return wrec(0x8, o, 0, 0), 0; }
static int THISCALL wst_lost(void *o) { return wrec(0x10, o, 0, 0), 0; }
// The window's own mapping of a scroll bar it holds: the static bar maps to nothing, any other to itself.
static int THISCALL wst_bar_map(void *o, void *bar) {
    wrec(0x1c, o, bar == (void *)&static_bar, bar != NULL);
    return bar == (void *)&static_bar ? 0 : (int)(intptr_t)bar;
}
static int THISCALL wst_bar_make(void *o) { return wrec(0x18, o, 0, 0), (int)(intptr_t)&static_bar; }
static int THISCALL wst_copy(void *o) { return wrec(0xf4, o, 0, 0), wanswer_copy; }
static int THISCALL wst_layout(void *o) { return wrec(0x108, o, 0, 0), 0; }
static int THISCALL wst_caption(void *o, int a, int b) { return wrec(0x128, o, a, b), 0; }
static int THISCALL wst_owner(void *o, void *w) { return wrec(0x168, o, wid(w), 0), wanswer_owner; }
static int THISCALL wst_nc_height(void *o) { return wrec(0x16c, o, 0, 0), 3; }
static void *wstub_vt[0x180 / 4];
static void *owner_obj = &wstub_vt;
static void fill_wstub_vt(void) {
    wstub_vt[0x8 / 4] = (void *)wst_hide, wstub_vt[0x10 / 4] = (void *)wst_lost, wstub_vt[0x1c / 4] = (void *)wst_bar_map;
    wstub_vt[0x18 / 4] = (void *)wst_bar_make, wstub_vt[0xf4 / 4] = (void *)wst_copy, wstub_vt[0x108 / 4] = (void *)wst_layout;
    wstub_vt[0x128 / 4] = (void *)wst_caption, wstub_vt[0x168 / 4] = (void *)wst_owner, wstub_vt[0x16c / 4] = (void *)wst_nc_height;
}

// The window at an offset into a twin's block; -1 names none, -2 a window that isn't one of the block's.
static void *obj_at(void *o, int off) { return off == -1 ? NULL : off == -2 ? (void *)0x1234 : (uint8_t *)o + off; }

// The game's globals the functions use, the same words in both runs (windows as offsets into the block).
static struct {
    int press1, press2, capture, hover, bubble, draw;
    int dc;
    int n_list, list[5];  // the top-level list
    int margin, screen_w, screen_h;
} cfg;
static int init_result;
static void apply_state(void *o) {
    *game_win_press1 = obj_at(o, cfg.press1), *game_win_press2 = obj_at(o, cfg.press2);
    *game_win_capture = obj_at(o, cfg.capture), *game_win_hover = obj_at(o, cfg.hover);
    *(void **)game_at(0x9b7a4c) = obj_at(o, cfg.bubble), *(void **)game_at(0x9b7a7c) = obj_at(o, cfg.draw);
    *game_dc = cfg.dc ? (void *)0x4242 : NULL;
    *game_win_parent_n = cfg.n_list;
    for (int i = 0; i < cfg.n_list; i++) game_win_parents[i] = obj_at(o, cfg.list[i]);
    *game_win_nc_margin = cfg.margin, *game_win_screen_w = cfg.screen_w, *game_win_screen_h = cfg.screen_h;
    *game_win_cur = NULL;
    *game_flip_text = NULL;  // (no bubble text: the redraw of the screen under it isn't checked)
    wlog.hash = 0, wlog.n = 0, static_deleted = 0;
}
// The words a check compares after each run: the globals above, the log, the number of static bars deleted and the answer.
static uint32_t *state_words[24];
static int n_state_words;
static void collect_state_words(void) {
    int n = 0;
    state_words[n++] = (uint32_t *)game_win_press1, state_words[n++] = (uint32_t *)game_win_press2;
    state_words[n++] = (uint32_t *)game_win_capture, state_words[n++] = (uint32_t *)game_win_hover;
    state_words[n++] = game_at(0x9b7a4c), state_words[n++] = game_at(0x9b7a7c), state_words[n++] = (uint32_t *)game_dc;
    state_words[n++] = (uint32_t *)game_win_parent_n;
    for (int i = 0; i < 6; i++) state_words[n++] = (uint32_t *)&game_win_parents[i];
    state_words[n++] = &wlog.hash, state_words[n++] = (uint32_t *)&wlog.n, state_words[n++] = &static_deleted;
    state_words[n++] = (uint32_t *)&init_result, state_words[n++] = (uint32_t *)game_win_cur;
    n_state_words = n;
}
// What the sweep puts back after a check.
struct saved_state {
    uint32_t words[24];
    struct game_win *parents[44];
    char *flip_text;
    int32_t margin, screen_w, screen_h;
    uint32_t pulldown_colour;
};
static void save_state(struct saved_state *s) {
    for (int i = 0; i < n_state_words; i++) s->words[i] = *state_words[i];
    memcpy(s->parents, game_win_parents, sizeof s->parents);
    s->flip_text = *game_flip_text;
    s->margin = *game_win_nc_margin, s->screen_w = *game_win_screen_w, s->screen_h = *game_win_screen_h;
    s->pulldown_colour = *(uint32_t *)game_at(0x9b7b68);
}
static void restore_state(const struct saved_state *s) {
    for (int i = 0; i < n_state_words; i++) *state_words[i] = s->words[i];
    memcpy(game_win_parents, s->parents, sizeof s->parents);
    *game_flip_text = s->flip_text;
    *game_win_nc_margin = s->margin, *game_win_screen_w = s->screen_w, *game_win_screen_h = s->screen_h;
    *(uint32_t *)game_at(0x9b7b68) = s->pulldown_colour;
}

static void put_both(int off, uint32_t v) {
    *(uint32_t *)(life_a + off) = v;
    *(uint32_t *)(life_b + off) = v;
}
static struct game_win *wb(uint8_t *obj, int base) { return (struct game_win *)(obj + base); }

// A dialog list of `n` nodes on the game's heap in the window at `base` of both blocks (each node holds a payload block);
// the node at index `own` names the window at offset `who`, the others the one at `other`. The cursor starts at a random
// node. `own_node` gets each block's node `own`.
static void build_ring(int base, int n, int own, int who, int other, int shared, struct game_win_node *own_node[2]) {
    uint8_t *objs[2] = {life_a, life_b};
    int start = n ? rnd_to(n) : 0, idx = n ? rnd_to(n) : 0;
    for (int k = 0; k < 2; k++) {
        struct game_win *w = wb(objs[k], base);
        struct game_win_node *first = NULL, *prev = NULL, *cur = NULL;
        own_node[k] = NULL;
        for (int i = 0; i < n; i++) {
            struct game_win_node *nd = game_mem_get(sizeof *nd);
            memset(nd, 0, sizeof *nd);
            nd->pad8 = (int32_t)(uintptr_t)game_mem_get(8);
            nd->win = (struct game_win *)(objs[k] + (i == own ? who : other));
            if (!first) first = nd;
            if (prev) prev->next = nd, nd->prev = prev;
            prev = nd;
            if (i == start) cur = nd;
            if (i == own) own_node[k] = nd;
        }
        if (first) first->prev = prev, prev->next = first;
        w->dlg_head = first, w->dlg_cur = cur, w->dlg_count = n, w->dlg_index = idx;
        *(int32_t *)((uint8_t *)w + 0xdc) = shared;
    }
}
// Gives back the nodes of a ring the check left (and each node's payload).
static void free_ring(uint8_t *obj, int base) {
    struct game_win *w = wb(obj, base);
    struct game_win_node *n = w->dlg_head;
    for (int i = 0; i < w->dlg_count && n; i++) {
        struct game_win_node *next = n->next;
        if (n->pad8) game_free((void *)(uintptr_t)n->pad8);
        game_free(n);
        n = next;
    }
    w->dlg_head = NULL, w->dlg_count = 0;
}
static void free_node(struct game_win_node *n) {
    if (!n) return;
    if (n->pad8) game_free((void *)(uintptr_t)n->pad8);
    game_free(n);
}

// ---- Win_close on a window with a parent, children, a grandchild and dialog lists ----

enum { TW_W = 0, TW_P = 0x500, TW_C0 = 0xa00, TW_C1 = 0xf00, TW_G = 0x1400, TW_SIZE = 0x1900 };
static const struct life_slot tree_slots[] = {
    {0xcc, -1}, {0xd0, -1}, {0xe0, 0}, {0xe4, -1}, {0xe8, -1}, {0xec, -1}, {0xb4, -1}, {0xb8, -1}, {0xbc, -1}, {0xc0, -1}, {0x43c, -1},
    {0x440, -1}, {TW_P + 0xcc, -1}, {TW_P + 0xd0, -1}, {TW_C0 + 0xe0, 0}, {TW_C0 + 0xb4, -1}, {TW_C1 + 0xe0, 0}, {TW_C1 + 0xb4, -1},
    {TW_G + 0xe0, 0}, {TW_G + 0xb4, -1},
};
static const struct life_class tree_class = {"WinTree", TW_SIZE, tree_slots, (int)(sizeof tree_slots / sizeof tree_slots[0])};

static void close_orig(void *o, void *x) {
    (void)x;
    apply_state(o);
    orig_win_close(o);
}
static void close_ours(void *o, void *x) {
    (void)x;
    apply_state(o);
    win_close(o);
}

// The words a close or an init puts back, given made-up values first so that a missed one shows.
static const int junk_words[] = {0x19c, 0xac,  0xb0,  0x134, 0x138, 0x184, 0x188, 0x18c, 0x190, 0x194, 0x198, 0xf0,  0xf4,  0xf8,
                                 0xfc,  0x100, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c, 0x120, 0x124, 0x128, 0x12c, 0x130,
                                 0x13c, 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c, 0x160, 0x164, 0x168, 0x16c, 0x170,
                                 0x174, 0x178, 0x17c, 0x180, 0x1a0, 0x9c,  0xa0,  0xa4,  0xa8,  0x400, 0x404, 0x408, 0x40c, 0x410,
                                 0x414, 0x418, 0x41c, 0x420, 0x424, 0x428, 0x42c, 0x430, 0x434, 0x438};
static void scribble(int base) {
    for (size_t i = 0; i < sizeof junk_words / sizeof junk_words[0]; i++) put_both(base + junk_words[i], rnd());
}

static void sweep_close(void) {
    static const int bases[5] = {TW_W, TW_P, TW_C0, TW_C1, TW_G};
    life_fill(&tree_class, life_a, life_b, (uint8_t)rnd());
    for (int k = 0; k < 2; k++) {
        uint8_t *o = k ? life_b : life_a;
        for (int i = 0; i < 5; i++) {
            orig_win_ctor(wb(o, bases[i]));
            wb(o, bases[i])->vtable = wstub_vt;
        }
    }
    scribble(TW_W), scribble(TW_C0), scribble(TW_C1), scribble(TW_G);
    uint32_t wf = (rnd_to(2) ? 0x40u : 0) | (rnd_to(2) ? 0x100000u : 0) | (rnd_to(4) == 0 ? 0x2000000u : 0);
    int nch = rnd_to(3), grand = nch > 0 && rnd_to(2);
    int kind = rnd_to(3);  // 0: in the top-level list, 1: a child of P, 2: a child of P at a place of its own
    int own_child = kind == 1 ? rnd_to(3) : -1;
    int nodes = rnd_to(4), shared = rnd_to(4) == 0, pn = 1 + rnd_to(3), pown = rnd_to(pn + 1) - 1, pshared = rnd_to(3) == 0;
    int own_ring = rnd_to(2), p_ring = kind && (wf & 0x100000);
    for (int k = 0; k < 2; k++) {
        uint8_t *o = k ? life_b : life_a;
        struct game_win *w = wb(o, TW_W), *p = wb(o, TW_P);
        w->flags = wf;
        w->n_children = nch;
        for (int i = 0; i < nch; i++) {
            w->children[i] = wb(o, i ? TW_C1 : TW_C0);
            w->children[i]->parent = w;
            w->children[i]->n_children = 0;
            w->children[i]->flags = 0;
        }
        if (grand) {
            wb(o, TW_C0)->children[0] = wb(o, TW_G);
            wb(o, TW_C0)->n_children = 1;
            wb(o, TW_G)->parent = wb(o, TW_C0);
            wb(o, TW_G)->n_children = 0, wb(o, TW_G)->flags = 0;
        }
        w->parent = kind ? p : NULL;
        if (kind) {
            p->n_children = own_child >= 0 ? 3 : 2;
            for (int i = 0; i < p->n_children; i++) p->children[i] = wb(o, TW_C1);
            if (own_child >= 0) p->children[own_child] = w;
        }
    }
    struct game_win_node *kept[2] = {NULL, NULL}, *pown_node[2] = {NULL, NULL};
    if (own_ring) build_ring(TW_W, nodes, -1, 0, TW_C1, shared, kept);
    if (p_ring) build_ring(TW_P, pn, pown, TW_W, TW_C1, pshared, pown_node);
    // Scroll bars (a heap object, the static one or none), frame buffers, caption buttons and captions.
    static const int bars[2] = {0x43c, 0x440};
    for (int i = 0; i < 2; i++) {
        int c = rnd_to(4);
        if (c == 1 || c == 2) life_objects(bars[i], 1);
        if (c == 3) put_both(bars[i], (uint32_t)(uintptr_t)&static_bar);
    }
    for (int i = 0; i < 4; i++)
        if (rnd_to(4)) life_objects(0xb4 + 4 * i, 1);
    for (int i = 0; i < 3; i++)
        if (rnd_to(3) == 0) life_objects(0xe4 + 4 * i, 1);
    if (rnd_to(2)) life_text(0xe0, "a caption");
    static const int below[3] = {TW_C0, TW_C1, TW_G};
    int exists[3] = {nch > 0, nch > 1, grand};
    for (int i = 0; i < 3; i++) {
        if (exists[i] && rnd_to(2)) life_text(below[i] + 0xe0, "below");
        if (exists[i] && rnd_to(2)) life_objects(below[i] + 0xb4, 1);
    }
    // The globals: windows of the block named at random.
    static const int named[] = {-1, TW_W, TW_C0, TW_C1, TW_G, TW_P, -2};
    cfg.press1 = named[rnd_to(7)], cfg.press2 = named[rnd_to(7)], cfg.capture = named[rnd_to(7)], cfg.hover = named[rnd_to(7)];
    cfg.bubble = named[rnd_to(7)], cfg.draw = named[rnd_to(7)];
    cfg.dc = rnd_to(2);
    cfg.n_list = rnd_to(5);
    for (int i = 0; i < 5; i++) cfg.list[i] = rnd_to(4) ? -2 : TW_C1;
    if (kind == 0) {
        if (!cfg.n_list) cfg.n_list = 1;
        if (rnd_to(6)) cfg.list[rnd_to(cfg.n_list)] = TW_W;
    }
    cfg.margin = rnd_to(10), cfg.screen_w = 640 + rnd_to(900), cfg.screen_h = 480 + rnd_to(600);
    static struct saved_state before;
    save_state(&before);
    *game_flip_text = NULL;
    life_run(&stats_win_close, &tree_class, close_orig, close_ours, NULL, state_words, n_state_words);
    // Give back what the runs left (a child the loop skipped is closed by the original), then the game's own state.
    for (int k = 0; k < 2; k++) {
        uint8_t *o = k ? life_b : life_a;
        for (int i = 0; i < 3; i++) orig_win_close(wb(o, below[i]));
        if (p_ring) {
            if (pshared && pown_node[k]) free_node(pown_node[k]);  // (taken out of the ring, but a shared list's nodes aren't freed)
            free_ring(o, TW_P);
        }
        if (own_ring && shared) {
            struct game_win_node *n = kept[k];
            for (int i = 0; i < nodes && n; i++) {
                struct game_win_node *next = n->next;
                free_node(n);
                n = next;
            }
        }
    }
    restore_state(&before);
}

// ---- Win_init, GraphicWin_init and PullDown_init ----

enum { WI_P = 0x500, WI_SIZE = 0xa00, GI_P = 0xb00, GI_SIZE = 0x1000, PD_SIZE = 0xf48 };
static const struct life_slot wi_slots[] = {
    {0xcc, -1}, {0xd0, -1}, {0xe0, 0}, {0xe4, -1}, {0xe8, -1}, {0xec, -1}, {0xb4, -1}, {0xb8, -1}, {0xbc, -1}, {0xc0, -1},
    {0x43c, -1}, {0x440, -1}, {WI_P + 0xcc, -1}, {WI_P + 0xd0, -1}, {WI_P + 0xdc, -1},
};
static const struct life_class wi_class = {"WinInit", WI_SIZE, wi_slots, (int)(sizeof wi_slots / sizeof wi_slots[0])};
static const struct life_slot gi_slots[] = {
    {0xcc, -1}, {0xd0, -1}, {0xe0, 0}, {0xe4, -1}, {0xe8, -1}, {0xec, -1}, {0xb4, -1}, {0xb8, -1}, {0xbc, -1}, {0xc0, -1},
    {0x43c, -1}, {0x440, -1}, {GW_PIC, -1}, {GI_P + 0xcc, -1}, {GI_P + 0xd0, -1}, {GI_P + 0xdc, -1},
};
static const struct life_class gi_class = {"GraphicWin_init", GI_SIZE, gi_slots, (int)(sizeof gi_slots / sizeof gi_slots[0]), GW_BUFFER, GW_OWN};
static const struct life_slot pd_slots[] = {
    {0xcc, -1}, {0xd0, -1}, {0xe0, 0}, {0xb4, -1}, {0xb8, -1}, {0xbc, -1}, {0xc0, -1}, {GW_PIC, -1},
};
static const struct life_class pd_class = {"PullDown_init", PD_SIZE, pd_slots, (int)(sizeof pd_slots / sizeof pd_slots[0]), GW_BUFFER, GW_OWN};

// The arguments of one call, and the arena the parent's dialog list may live in (one for each twin, big enough that it
// never grows: the heap isn't touched).
static struct {
    int x, y, cw, ch;
    const char *title;
    uint32_t flags;
    int parent, owner, caption;
    int pd_arg, pd_colour;
} args;
static int32_t caption_sizes[3];
static uint8_t arena_mem[2][4096];
static uint32_t arena_obj[2][5];
static int arena_in_use;
static void arena_reset(void) {
    for (int k = 0; k < 2; k++) {
        memset(arena_mem[k], 0, sizeof arena_mem[k]);
        arena_obj[k][0] = 0, arena_obj[k][1] = (uint32_t)(uintptr_t)arena_mem[k], arena_obj[k][2] = (uint32_t)(uintptr_t)arena_mem[k];
        arena_obj[k][3] = sizeof arena_mem[k], arena_obj[k][4] = sizeof arena_mem[k];
    }
}

enum { WHICH_WIN, WHICH_GRAPHIC, WHICH_PULLDOWN };
static void run_init(void *o, int ours, int kind, int p_base) {
    apply_state(o);
    struct game_win *w = o, *parent = args.parent ? (struct game_win *)((uint8_t *)o + p_base) : NULL;
    struct game_win *owner = args.owner ? (struct game_win *)&owner_obj : NULL;
    const int32_t *caption = args.caption ? caption_sizes : NULL;
    if (kind == WHICH_PULLDOWN) {
        *(uint32_t *)game_at(0x9b7b68) = (uint32_t)args.pd_colour;
        init_result = ours ? pulldown_init(w, args.pd_arg) : orig_pulldown_init(w, args.pd_arg);
    } else if (kind == WHICH_GRAPHIC) {
        init_result = ours ? graphic_win_init(w, args.x, args.y, args.cw, args.ch, args.title, args.flags, parent, owner, caption)
                           : orig_graphic_win_init(w, args.x, args.y, args.cw, args.ch, args.title, args.flags, parent, owner, caption);
    } else {
        init_result = ours ? win_init(w, args.x, args.y, args.cw, args.ch, args.title, args.flags, parent, owner, caption)
                           : orig_win_init(w, args.x, args.y, args.cw, args.ch, args.title, args.flags, parent, owner, caption);
    }
}
static void win_init_orig(void *o, void *x) { (void)x, run_init(o, 0, WHICH_WIN, WI_P); }
static void win_init_ours(void *o, void *x) { (void)x, run_init(o, 1, WHICH_WIN, WI_P); }
static void gwin_init_orig(void *o, void *x) { (void)x, run_init(o, 0, WHICH_GRAPHIC, GI_P); }
static void gwin_init_ours(void *o, void *x) { (void)x, run_init(o, 1, WHICH_GRAPHIC, GI_P); }
static void pulldown_init_orig(void *o, void *x) { (void)x, run_init(o, 0, WHICH_PULLDOWN, 0); }
static void pulldown_init_ours(void *o, void *x) { (void)x, run_init(o, 1, WHICH_PULLDOWN, 0); }

// Random arguments (the flags that would make a real scroll bar are given the stand-in's; one that would stop the game
// is left out) and the state of the globals a call sees.
static void random_init_args(int graphic) {
    static const char *const titles[] = {NULL, "", "A title", "a much longer caption, which takes more room on the game's heap"};
    static const uint32_t bits[] = {0x1, 0x10, 0x20, 0x40, 0x80, 0x400, 0x800, 0x1000, 0x8000, 0x80000, 0x100000, 0x200000, 0x2000000,
                                    0x10000000, 0x20000000, 0x80000000, 0x4, 0x8};
    args.flags = 0;
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++)
        if (rnd_to(4) == 0) args.flags |= bits[i];
    if (graphic && rnd_to(3) == 0) args.flags |= 0x40000000;
    if (graphic && rnd_to(3) == 0) args.flags |= 0x30000000;
    if (args.flags & 0xc) args.flags |= 0x40;
    args.parent = rnd_to(3) != 0;
    if (!args.parent && (args.flags & 0x100000)) args.flags |= 0x1000;
    args.title = titles[rnd_to(4)];
    args.x = rnd_to(200) - 60, args.y = rnd_to(200) - 60;
    args.cw = graphic ? 20 + rnd_to(40) : rnd_to(200), args.ch = graphic ? 16 + rnd_to(30) : rnd_to(200);
    args.owner = rnd_to(3) == 0, args.caption = rnd_to(2);
    for (int i = 0; i < 3; i++) caption_sizes[i] = rnd_to(20);
    wanswer_copy = rnd_to(4) != 0, wanswer_owner = rnd_to(5) == 0 ? 7 : 0;
    cfg.press1 = rnd_to(3) ? -1 : 0, cfg.press2 = rnd_to(3) ? -1 : 0, cfg.capture = rnd_to(3) ? -1 : 0, cfg.hover = rnd_to(3) ? -1 : 0;
    cfg.bubble = rnd_to(3) ? -1 : 0, cfg.draw = rnd_to(3) ? -1 : 0;
    cfg.dc = rnd_to(2);
    cfg.n_list = rnd_to(6);
    for (int i = 0; i < 5; i++) cfg.list[i] = -2;
    cfg.margin = 8 + rnd_to(10), cfg.screen_w = 640 + rnd_to(900), cfg.screen_h = 480 + rnd_to(600);
}

// The parent block: a few children already, flags that decide whether the window takes the dialog list, a dialog list
// (on the heap, or in an arena), and the rectangle a negative position counts from.
static void build_parent(int base) {
    uint32_t pf = rnd_to(4) == 0 ? 0x1000u : 0;
    int nch = rnd_to(4), ring_n = rnd_to(3), in_arena = ring_n == 0 && rnd_to(2);
    int32_t inner[4] = {rnd_to(30), rnd_to(30), 100 + rnd_to(400), 80 + rnd_to(300)};
    uint8_t *objs[2] = {life_a, life_b};
    struct game_win_node *dummy[2];
    if (ring_n) build_ring(base, ring_n, -1, 0, 0, 0, dummy);
    for (int k = 0; k < 2; k++) {
        struct game_win *p = wb(objs[k], base);
        p->flags = pf, p->n_children = nch;
        for (int i = 0; i < nch; i++) p->children[i] = wb(objs[k], 0);
        memcpy(p->inner, inner, sizeof inner);
        if (in_arena) *(uint32_t *)((uint8_t *)p + 0xdc) = (uint32_t)(uintptr_t)arena_obj[k], arena_in_use = 1;
    }
}

// Closes what the runs made (with the original's close, which keeps the stand-in vtable) and gives back the parent's list.
static void cleanup_init(int base, int graphic, int has_parent) {
    for (int k = 0; k < 2; k++) {
        uint8_t *o = k ? life_b : life_a;
        if (graphic) orig_graphic_win_close(wb(o, 0)), orig_graphic_win_dtor(wb(o, 0));
        else orig_win_close(wb(o, 0));
        if (has_parent) {
            struct game_win *p = wb(o, base);
            if (!*(uint32_t *)((uint8_t *)p + 0xdc)) free_ring(o, base);
            else p->dlg_head = NULL, p->dlg_count = 0;
        }
    }
}

static void sweep_init(void) {
    static struct saved_state before;
    save_state(&before);
    *game_flip_text = NULL;
    for (int round = 0; round < 40; round++) {
        arena_reset();
        arena_in_use = 0;
        random_init_args(0);
        life_fill(&wi_class, life_a, life_b, (uint8_t)rnd());
        for (int k = 0; k < 2; k++) {
            uint8_t *o = k ? life_b : life_a;
            orig_win_ctor(wb(o, 0)), orig_win_ctor(wb(o, WI_P));
            wb(o, 0)->vtable = wstub_vt;
        }
        // What the window had before (the init closes it first): made-up values, frame buffers, a caption, children.
        scribble(0);
        if (rnd_to(2)) life_text(0xe0, "an old caption");
        for (int i = 0; i < 4; i++)
            if (rnd_to(2)) life_objects(0xb4 + 4 * i, 1);
        for (int i = 0; i < 3; i++)
            if (rnd_to(4) == 0) life_objects(0xe4 + 4 * i, 1);
        if (args.parent) build_parent(WI_P);
        life_run(&stats_win_init, &wi_class, win_init_orig, win_init_ours, NULL, state_words, n_state_words);
        if (arena_in_use && (arena_obj[0][2] - (uint32_t)(uintptr_t)arena_mem[0]) != (arena_obj[1][2] - (uint32_t)(uintptr_t)arena_mem[1]))
            hooks_mismatch(&stats_win_init, ": the dialog list's arena was used differently");
        cleanup_init(WI_P, 0, args.parent);
        restore_state(&before);
    }
}

// The Buffer of two GraphicWins an init made: the word at 0x448 names the window (so it is compared as where in its block it
// lies), then the Buffers are compared with that word cleared.
static void check_gw_buffer(struct hook_stats *s) {
    uint32_t wa = *(uint32_t *)(life_a + 0x448), wb = *(uint32_t *)(life_b + 0x448);
    if (wa != wb && wa - (uint32_t)(uintptr_t)life_a != wb - (uint32_t)(uintptr_t)life_b)
        hooks_mismatch(s, " (Buffer): the window word at 0x448 = 0x%x, original 0x%x", (unsigned)wb, (unsigned)wa);
    *(uint32_t *)(life_a + 0x448) = *(uint32_t *)(life_b + 0x448) = 0;
    life_buffer_check_at(s, GW_BUFFER);
}

static void sweep_graphic_init(void) {
    static struct saved_state before;
    save_state(&before);
    *game_flip_text = NULL;
    for (int round = 0; round < 10; round++) {
        arena_reset();
        arena_in_use = 0;
        random_init_args(1);
        life_fill(&gi_class, life_a, life_b, (uint8_t)rnd());
        for (int k = 0; k < 2; k++) {
            uint8_t *o = k ? life_b : life_a;
            orig_graphic_win_ctor(wb(o, 0)), orig_win_ctor(wb(o, GI_P));
            wb(o, 0)->vtable = wstub_vt;
        }
        scribble(0);
        if (args.parent) build_parent(GI_P);
        life_run(&stats_graphic_win_init, &gi_class, gwin_init_orig, gwin_init_ours, NULL, state_words, n_state_words);
        check_gw_buffer(&stats_graphic_win_init);
        if (arena_in_use && (arena_obj[0][2] - (uint32_t)(uintptr_t)arena_mem[0]) != (arena_obj[1][2] - (uint32_t)(uintptr_t)arena_mem[1]))
            hooks_mismatch(&stats_graphic_win_init, ": the dialog list's arena was used differently");
        cleanup_init(GI_P, 1, args.parent);
        restore_state(&before);
    }
}

static void sweep_pulldown_init(void) {
    static struct saved_state before;
    save_state(&before);
    *game_flip_text = NULL;
    for (int round = 0; round < 8; round++) {
        random_init_args(1);
        args.parent = 0, args.owner = 0;
        args.pd_arg = rnd_to(100), args.pd_colour = rnd_to(2) ? 0 : 1 + rnd_to(50);
        cfg.n_list = rnd_to(4);
        life_fill(&pd_class, life_a, life_b, (uint8_t)rnd());
        for (int k = 0; k < 2; k++) {
            uint8_t *o = k ? life_b : life_a;
            ((void *(THISCALL *)(void *))(uintptr_t)game_va(0x5f8800))(o);
            wb(o, 0)->vtable = wstub_vt;
        }
        life_run(&stats_pulldown_init, &pd_class, pulldown_init_orig, pulldown_init_ours, NULL, state_words, n_state_words);
        check_gw_buffer(&stats_pulldown_init);
        for (int k = 0; k < 2; k++) orig_graphic_win_dtor(wb(k ? life_b : life_a, 0));
        restore_state(&before);
    }
}

static void life_sweep_win_init(void) {
    if (!game_mem_get || *game_ddraw || !hooks_dc_same || !game_string_alloc) return;
    fill_wstub_vt();
    collect_state_words();
    for (int round = 0; round < 40; round++) sweep_close();
    sweep_init();
    sweep_graphic_init();
    sweep_pulldown_init();
}

static void win_sweep(void) {
    static struct win_globals saved;
    static struct game_win *real[REAL];
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x2545f491u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    globals_save(&saved);

    // The game's own windows, those in its z-order and parent lists, by read-only questions.
    int nreal = 0;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? saved.parent_n : saved.z_n;
        struct game_win **list = pass ? saved.parents : saved.zorder;
        for (int i = 0; i < n && i < (pass ? 44 : 514) && nreal < REAL; i++) {
            int dup = 0;
            for (int j = 0; j < nreal; j++) dup |= real[j] == list[i];
            if (list[i] && !dup) real[nreal++] = list[i];
        }
    }
    check_win_get_key_window();
    for (int i = 0; i < nreal; i++) {
        struct game_win *w = real[i], *o = real[rnd_to(nreal)];
        check_win_is_visible(w);
        check_win_is_descendant(w, o);
        check_win_is_descendant(o, w);
        check_win_is_below(w, o);
        check_win_has_child(w, o);
        check_win_has_child(w, w->n_children ? w->children[rnd_to(w->n_children)] : NULL);
        check_win_in_modal(w);
        check_win_is_dialog_focus(w);
        check_win_on_nc_hittest(w, rnd_to(300), rnd_to(300));
        int32_t r[4], p = rnd_to(800), q = rnd_to(600);
        check_win_get_mouse_pos(w, &p, &q);
        for (int k = 0; k < 4; k++) r[k] = rnd_to(800);
        check_win_screen_to_client(w, r);
        check_win_client_to_screen(w, r);
        check_win_nonscreen_to_client(w, r);
        check_win_nonclient_to_screen(w, r);
        check_win_client_to_screen_2(w, &p, &q);
        check_win_screen_to_client_2(w, &p, &q);
        check_win_nonclient_to_screen_2(w, &p, &q);
        check_win_screen_to_nonclient(w, &p, &q);
        // The nonclient ones ask the owner's vtable: only for a window with none.
        if (!w->nc_owner) {
            check_win_client_to_nonclient(w, &p, &q);
            check_win_nonclient_to_client(w, &p, &q);
            check_win_client_to_nonclient_2(w, r);
        }
    }

    // The Buffers the scratch windows draw into and are hit-tested by (made on the first sweep), and a font for titles.
    scratch_buffers();
    static struct font mine;
    static int mine_made;
    struct font *font = NULL;
    if (*game_font_default && *game_font_dc) {
        if (!mine_made) font_ctor(&mine), game_font_init(&mine, "Arial", 12, 0), mine_made = 1;
        if (mine.hfont) font = &mine;
    }

    // Scratch windows.
    for (int round = 0; round < 40; round++) {
        fill_stub_vtable();
        scratch_buffers();
        build_forest();
        random_globals();
        if (nbuf_state > 0) sweep_mouse();
        random_globals();
        sweep_screen();
        random_globals();
        if (round % 8 == 3) sweep_graphic(saved.hwnd);
        if (nbuf_state > 0) sweep_nc_graphic();
        if (nbuf_state > 0 && saved.hwnd) sweep_overlay(saved.hwnd);
        random_globals();
        for (int k = 0; k < 3 * POOL; k++) sweep_window(PW(k % POOL), any_pool());
        // Lists and focus.
        check_win_get_key_window();
        for (int k = 0; k < 6; k++) check_win_get_key_window(), check_win_in_modal(PW(rnd_to(POOL)));
        check_win_set_def_focus(any_pool());
        check_win_update_zorder();
        for (int k = 0; k < 6; k++) check_win_recurse_zorder(PW(rnd_to(POOL)));
        for (int k = 0; k < 6; k++) {
            check_win_bring_to_top(PW(rnd_to(POOL)));
            struct game_win *p = PW(rnd_to(POOL));
            check_win_bring_child_to_top(p, p->n_children && rnd_to(5) ? p->children[rnd_to(p->n_children)] : any_pool());
            check_win_bring_parent_to_top(*game_win_parent_n && rnd_to(5) ? game_win_parents[rnd_to(*game_win_parent_n)] : any_pool());
        }
        // The opaque window among the top-level ones, so the list starts over there.
        for (int k = 0; k < 4; k++) {
            if (*game_win_parent_n) *game_win_opaque = game_win_parents[rnd_to(*game_win_parent_n)];
            check_win_update_zorder();
            check_win_recurse_zorder(PW(rnd_to(POOL)));
            check_win_bring_to_top(PW(rnd_to(POOL)));
        }
        for (int k = 0; k < 4; k++) {
            check_win_add_parent(PW(rnd_to(POOL)));
            check_win_add_parent(NULL);
            struct game_win *p = PW(rnd_to(POOL));
            if (p->n_children < 140) check_win_add_child(p, any_pool());
        }
        // The modal stack: a window in it at each depth, one not in it, and an empty stack.
        for (int k = 0; k < 8; k++) {
            int n = rnd_to(6);
            *game_win_modal_n = n;
            check_win_release_modal(n && rnd_to(3) ? game_win_modals[rnd_to(n)] : any_pool());
            if (n) check_win_release_modal(game_win_modals[n - 1]);
            if (n) check_win_release_modal(game_win_modals[0]);
        }
        *game_win_modal_n = 0;
        check_win_release_modal(any_pool());
        // Scroll bars and the move.
        for (int k = 0; k < POOL; k++) {
            struct game_win *w = PW(k);
            check_win_set_vert_paging(w, rnd_to(1000));
            check_win_set_horz_paging(w, rnd_to(1000));
            check_win_unk8(w, rnd_to(1000));
            check_win_unk9(w, rnd_to(1000));
            check_win_move(w, rnd_to(500) - 150, rnd_to(500) - 150);
            check_win_move(w, w->inner[0], w->outer[1]);
        }
        // The functions that call through the vtable and Windows, on windows of every shape.
        for (int k = 0; k < 2 * POOL; k++) sweep_virtual(PW(k % POOL));
        // The events (the dispatch needs the Buffers the mouse's window is hit-tested by).
        sweep_events();
        if (nbuf_state > 0) sweep_dispatch();
        // The lists a window leaves: one in them, one not, and the same twice over.
        for (int k = 0; k < 6; k++) {
            check_win_remove_parent(*game_win_parent_n && rnd_to(4) ? game_win_parents[rnd_to(*game_win_parent_n)] : any_pool());
            struct game_win *p = PW(rnd_to(POOL));
            check_win_remove_child(p, p->n_children && rnd_to(4) ? p->children[rnd_to(p->n_children)] : any_pool());
        }
        check_win_remove_parent(NULL);
        check_win_remove_child(PW(0), NULL);
    }
    // The frame's functions, on twin windows.
    if (nbuf_state > 0) {
        fill_stub_vtable();
        twin_sweep(font, saved.hwnd);
    }
    life_sweep_win();
    life_sweep_caption();
    life_sweep_win_init();
    globals_restore(&saved);
    hooks_sweeping = sweeping;
}

// ---- Hooks ---------------------------------------------------------------------------------

static const struct hook win_hooks[] = {
    HOOK(0x5f7e90, win_is_visible, 0xf6, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x01),
    HOOK(0x5ece20, win_is_descendant, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x56, 0x85, 0xed, 0x57),
    HOOK(0x5ecdc0, win_is_below, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x85, 0xdb, 0x57),
    HOOK(0x5ece80, win_has_child, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x5ecec0, win_in_modal, 0xa1, 0xe0, 0x7a, 0x9b, 0x00, 0x53, 0x55, 0x56, 0x85, 0xc0, 0x57),
    HOOK(0x5f2ca0, win_is_dialog_focus, 0x8b, 0x81, 0x98, 0x00, 0x00, 0x00, 0xf6, 0xc4, 0x10),
    HOOK(0x5f2ce0, win_set_def_focus, 0x8b, 0x44, 0x24, 0x04, 0xa3, 0xec, 0x7a, 0x9b, 0x00),
    HOOK(0x5f6a50, win_get_key_window, 0x56, 0x8b, 0x35, 0xe0, 0x7a, 0x9b, 0x00, 0x85, 0xf6),
    HOOK(0x5f5080, win_add_parent, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x5f5200, win_add_child, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x5ee280, win_release_modal, 0x8b, 0x15, 0xe4, 0x7a, 0x9b, 0x00, 0x56, 0x85, 0xd2),
    HOOK(0x5f4ec0, win_recurse_zorder, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x56, 0x57, 0x33, 0xdb),
    HOOK(0x5f5020, win_update_zorder, 0xa1, 0x34, 0x7b, 0x9b, 0x00, 0x55, 0x57, 0x33, 0xed, 0x33, 0xff, 0x3b, 0xc5),
    HOOK(0x5f4ab0, win_bring_to_top, 0x55, 0x56, 0x57, 0x8b, 0xb9, 0xc4, 0x00, 0x00, 0x00, 0x33, 0xed, 0x3b, 0xfd),
    HOOK(0x5f5140, win_bring_parent_to_top, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x0c, 0x33, 0xed, 0x3b, 0xf5, 0x57),
    HOOK(0x5f52e0, win_bring_child_to_top, 0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x33, 0xed, 0x3b, 0xfd),
    HOOK(0x5ecf20, win_screen_to_client, 0x51, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1, 0x85, 0xff),
    HOOK(0x5ecfe0, win_client_to_screen, 0x51, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1, 0x85, 0xff),
    HOOK(0x5ed0a0, win_nonscreen_to_client, 0x51, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1, 0x85, 0xff),
    HOOK(0x5ed170, win_nonclient_to_screen, 0x51, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1, 0x85, 0xff),
    HOOK(0x5ed240, win_client_to_screen_2, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5ed2d0, win_screen_to_client_2, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5ed3f0, win_nonclient_to_screen_2, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5ed520, win_screen_to_nonclient, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5eee70, win_client_to_nonclient, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x85, 0xc0),
    HOOK(0x5eef60, win_nonclient_to_client, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x85, 0xc0),
    HOOK(0x5ef050, win_client_to_nonclient_2, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x5ed7d0, win_move, 0x8a, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x56, 0xa8, 0x02, 0x8b, 0x44, 0x24, 0x0c),
    HOOK(0x5f5ad0, win_on_nc_hittest, 0x8a, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x14, 0xa8, 0x02),
    HOOK(0x5ee050, win_get_vert_pos, 0x8b, 0x81, 0x3c, 0x04, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5ee090, win_get_horz_pos, 0x8b, 0x81, 0x40, 0x04, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5ee0f0, win_set_vert_paging, 0x8b, 0x81, 0x3c, 0x04, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5ee110, win_set_horz_paging, 0x8b, 0x81, 0x40, 0x04, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5ee130, win_unk8, 0x8b, 0x81, 0x3c, 0x04, 0x00, 0x00, 0x8b, 0x54, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x5ee160, win_unk9, 0x8b, 0x81, 0x3c, 0x04, 0x00, 0x00, 0x8b, 0x54, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x5f50e0, win_remove_parent, 0x8b, 0x4c, 0x24, 0x04, 0x56, 0x85, 0xc9),
    HOOK(0x5f5270, win_remove_child, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x4c, 0x24, 0x0c, 0x85, 0xc9),
    HOOK(0x5ee030, win_set_vert_pos, 0x8b, 0x89, 0x3c, 0x04, 0x00, 0x00, 0x85, 0xc9),
    HOOK(0x5ee070, win_set_horz_pos, 0x8b, 0x89, 0x40, 0x04, 0x00, 0x00, 0x85, 0xc9),
    HOOK(0x5ee0b0, win_set_vert_range, 0x8b, 0x89, 0x3c, 0x04, 0x00, 0x00, 0x85, 0xc9),
    HOOK(0x5ee0d0, win_set_horz_range, 0x8b, 0x89, 0x40, 0x04, 0x00, 0x00, 0x85, 0xc9),
    HOOK(0x5f0580, win_set_caption_height, 0xf6, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x02),
    HOOK(0x5f05d0, win_set_border_thickness, 0xf6, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x02),
    HOOK(0x5f0610, win_set_bottom_border_thickness, 0xf6, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x02),
    HOOK(0x5ecc40, win_set_dialog_focus, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x85, 0xdb, 0x57, 0x8b, 0xf1),
    HOOK(0x5ecd20, win_pass_dialog_focus, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xc4, 0x00, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5ec800, win_set_mouse_pos, 0x56, 0x8b, 0xf1, 0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x86, 0x4c, 0x01, 0x00, 0x00),
    HOOK(0x5ec8a0, win_get_mouse_pos, 0x83, 0xec, 0x08, 0x53, 0x8b, 0x5c, 0x24, 0x10, 0x56, 0x57, 0x85, 0xdb, 0x8b, 0xf1),
    HOOK(0x5ec960, win_get_lbutton_state, 0x56, 0x6a, 0x01, 0xff, 0x15, 0x30, 0x93, 0x66, 0x00, 0x0f, 0xbf, 0xf0, 0x6a, 0x17),
    HOOK(0x5ec980, win_get_rbutton_state, 0x56, 0x6a, 0x02, 0xff, 0x15, 0x30, 0x93, 0x66, 0x00, 0x0f, 0xbf, 0xf0, 0x6a, 0x17),
    HOOK(0x5ede60, win_maximize, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x57, 0x8b, 0xf1, 0x33, 0xff, 0x3b, 0xc6),
    HOOK(0x5edeb0, win_minimize, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x57, 0x8b, 0xf1, 0x33, 0xff, 0x3b, 0xc6),
    HOOK(0x5edf00, win_show_maximize, 0xa1, 0xbc, 0x7a, 0x9b, 0x00, 0x56, 0x57, 0x8b, 0xf1, 0x33, 0xff, 0x3b, 0xc6),
    HOOK(0x5ed5c0, win_center, 0x83, 0xec, 0x08, 0x53, 0x8b, 0xd9, 0x56, 0x57),
    HOOK(0x5ef110, win_nonclient_to_client_2, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x5f2760, win_redo_caption_buttons, 0x53, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x9f, 0x28, 0x01, 0x00, 0x00),
    HOOK(0x5ee330, win_resize_event, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x89, 0x35, 0xb8, 0x7a, 0x9b, 0x00),
    HOOK(0x5ed880, win_resize, 0x8b, 0x44, 0x24, 0x0c, 0x56, 0x57, 0x8b, 0xf9, 0x85, 0xc0),
    HOOK(0x5f4c30, win_redraw_nc_buffer, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xf1, 0x57, 0x6a, 0x00, 0x8b, 0x06),
    HOOK(0x5f4ca0, win_update_nc_buffer, 0x8b, 0x54, 0x24, 0x04, 0x8b, 0x01, 0x52, 0x6a, 0x00),
    // push ebx; push ebp; push esi; mov esi, [esp+0x10]; mov ebp, [0x9b7ad0]; mov edx, [0x9b7ac0]
    HOOK(0x5f6f10, win_get_mouse_window, 0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x8b, 0x2d, 0xd0, 0x7a, 0x9b, 0x00, 0x8b, 0x15,
         0xc0, 0x7a, 0x9b, 0x00),
    // mov eax, [0x9b7b30]; sub esp, 0x10; test eax, eax; push ebx; push ebp; push esi; push edi
    HOOK(0x5f7320, win_update_screen, 0xa1, 0x30, 0x7b, 0x9b, 0x00, 0x83, 0xec, 0x10, 0x85, 0xc0, 0x53, 0x55, 0x56, 0x57),
    // sub esp, 0x14; push esi; push edi; mov edi, [esp+0x20]; mov esi, ecx
    HOOK(0x5d55d0, graphic_win_update, 0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1),
    HOOK(0x5d5630, graphic_win_update_2, 0x83, 0xec, 0x14, 0x8b, 0x44, 0x24, 0x18, 0x89, 0x4c, 0x24, 0x00, 0x85, 0xc0),
    HOOK(0x5d56b0, graphic_win_update_3, 0x83, 0xec, 0x10, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0xbe, 0x80, 0x04, 0x00, 0x00),
    HOOK(0x5d5720, graphic_win_soft_update, 0x83, 0xec, 0x20, 0x8b, 0x44, 0x24, 0x2c, 0x53, 0x8b, 0x5c, 0x24, 0x2c, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x5d5890, graphic_win_soft_update_2, 0x83, 0xec, 0x18, 0x56, 0x8b, 0xf1),
    HOOK(0x5d5930, graphic_win_soft_update_3, 0x83, 0xec, 0x2c, 0x56, 0x57, 0x8b, 0xf1),
    HOOK(0x5d5cf0, graphic_win_on_paint, 0x83, 0xec, 0x18, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x57, 0x8d, 0x4c, 0x24, 0x14, 0x50, 0x33, 0xff, 0x51),
    HOOK(0x5f7ec0, win_update_back_to_window, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x14, 0x53, 0x33, 0xdb, 0x56, 0x3b, 0xc3, 0x57, 0x8b, 0xf1),
    HOOK(0x5f8140, win_update_window_to_buffer, 0x8b, 0x44, 0x24, 0x04, 0x33, 0xd2, 0x55, 0x56, 0x3b, 0xc2, 0x57, 0x8b, 0xf1),
    HOOK(0x5f74a0, win_update_window, 0x83, 0xec, 0x14, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x20, 0x8b, 0xf1),
    // mov eax, [0x9b7b30]; push esi; xor esi, esi; test eax, eax
    HOOK(0x5f4cc0, win_redraw, 0xa1, 0x30, 0x7b, 0x9b, 0x00, 0x56, 0x33, 0xf6, 0x85, 0xc0),
    // mov eax, [0x9b7b30]; push edi; xor edi, edi; test eax, eax
    HOOK(0x5f4d00, win_update, 0xa1, 0x30, 0x7b, 0x9b, 0x00, 0x57, 0x33, 0xff, 0x85, 0xc0),
    // push esi; mov esi, ecx; test BYTE PTR [esi+0x9c], 0x2
    HOOK(0x5f2940, win_set_rects, 0x56, 0x8b, 0xf1, 0xf6, 0x86, 0x9c, 0x00, 0x00, 0x00, 0x02),
    // sub esp, 0x64; push ebx; push ebp; push esi; mov esi, ecx
    HOOK(0x5d63f0, graphic_win_on_redraw_nc, 0x83, 0xec, 0x0c, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x5f3b80, win_copy_into_frame, 0x81, 0xec, 0x94, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56),
    HOOK(0x5f2cf0, win_on_redraw_nc, 0x83, 0xec, 0x64, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    // sub esp, 0x40; push ebx; push ebp; push esi; push edi
    HOOK(0x5f37c0, win_draw_nc_to, 0x83, 0xec, 0x40, 0x53, 0x55, 0x56, 0x57),
    // sub esp, 8; push ebx; push esi; mov esi, ecx; test BYTE PTR [esi+0x98], 0x11
    HOOK(0x5f5810, win_on_nc_paint, 0x83, 0xec, 0x08, 0x53, 0x56, 0x8b, 0xf1, 0xf6, 0x86, 0x98, 0x00, 0x00, 0x00, 0x11),
    // The events. push ebx; push esi; mov esi, ecx; push edi
    HOOK(0x5ee750, win_left_down_event, 0x53, 0x56, 0x8b, 0xf1, 0x57),
    // push esi; mov esi, ecx; test DWORD PTR [esi+0x98], 0x200000
    HOOK(0x5ee810, win_left_up_event, 0x56, 0x8b, 0xf1, 0xf7, 0x86, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00),
    HOOK(0x5ee8b0, win_right_click_event, 0x56, 0x8b, 0xf1, 0xf7, 0x86, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00),
    HOOK(0x5ee950, win_right_down_event, 0x56, 0x8b, 0xf1, 0xf7, 0x86, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00),
    HOOK(0x5ee9f0, win_right_up_event, 0x56, 0x8b, 0xf1, 0xf7, 0x86, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00),
    // push ebx; push ebp; push esi; mov esi, ecx; push edi
    HOOK(0x5eea90, win_key_click_event, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57),
    // push ebx; push ebp; push esi; push edi; mov edi, ecx
    HOOK(0x5eeb90, win_key_down_event, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf9),
    // push ebx; push esi; mov esi, ecx; push edi; xor edi, edi
    HOOK(0x5eedc0, win_key_up_event, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    HOOK(0x5f6320, win_on_mouse_move, 0x56, 0x8b, 0xf1, 0xf7, 0x86, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00),
    // push esi; mov eax, 0x88888889 (the division of the wheel's turn by 120)
    HOOK(0x5f62d0, win_on_mouse_wheel, 0x56, 0xb8, 0x89, 0x88, 0x88, 0x88),
    // push ecx; push ebx; push ebp; push esi; mov esi, ecx
    HOOK(0x5f63c0, win_on_l_button_down, 0x51, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    // sub esp, 0x10; mov eax, [0x9b7acc]
    HOOK(0x5f6550, win_on_l_button_up, 0x83, 0xec, 0x10, 0xa1, 0xcc, 0x7a, 0x9b, 0x00),
    // push ebx; push ebp; push esi; mov esi, ecx; xor ebx, ebx
    HOOK(0x5f6710, win_on_r_button_down, 0x53, 0x55, 0x56, 0x8b, 0xf1, 0x33, 0xdb),
    HOOK(0x5f6880, win_on_r_button_up, 0x83, 0xec, 0x10, 0xa1, 0xcc, 0x7a, 0x9b, 0x00),
    // mov eax, [esp+8]; push esi; mov esi, [esp+0x14]
    HOOK(0x5f6230, win_on_sys_key, 0x8b, 0x44, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x14),
    // mov ecx, [0x9b7ae0] (the modal window)
    HOOK(0x5f15c0, win_OnKey, 0x8b, 0x0d, 0xe0, 0x7a, 0x9b, 0x00),
    HOOK(0x5f1660, win_OnChar, 0x8b, 0x0d, 0xe0, 0x7a, 0x9b, 0x00),
    HOOK(0x5f16d0, win_OnSysKey, 0x8b, 0x0d, 0xe0, 0x7a, 0x9b, 0x00),
    // push ecx; push ebx; push ebp; lea eax, [esp+0x1c]
    HOOK(0x5f2330, win_OnLButtonDown, 0x51, 0x53, 0x55, 0x8d, 0x44, 0x24, 0x1c),
    // lea eax, [esp+0xc]; lea ecx, [esp+8]
    HOOK(0x5f2570, win_OnLButtonUp, 0x8d, 0x44, 0x24, 0x0c, 0x8d, 0x4c, 0x24, 0x08),
    // lea eax, [esp+0x10]; lea ecx, [esp+0xc]
    HOOK(0x5f25c0, win_OnRButtonDown, 0x8d, 0x44, 0x24, 0x10, 0x8d, 0x4c, 0x24, 0x0c),
    HOOK(0x5f2620, win_OnRButtonUp, 0x8d, 0x44, 0x24, 0x0c, 0x8d, 0x4c, 0x24, 0x08),
    // mov eax, ecx; mov [eax], 0x66ff34
    HOOK(0x62ba80, winbase_ctor, 0x8b, 0xc1, 0xc7, 0x00, 0x34, 0xff, 0x66, 0x00),
    // mov eax, [0x9bc080]; mov [ecx+4], eax
    HOOK(0x62bbf0, winbase_reset, 0xa1, 0x80, 0xc0, 0x9b, 0x00, 0x89, 0x41, 0x04),
    // push esi; mov esi, ecx; call winbase_ctor
    HOOK_CALL(0x5eb3d0, win_ctor, 0x56, 0x8b, 0xf1, 0xe8, 0xa8, 0x06, 0x04, 0x00),
    HOOK(0x5ebc90, win_dtor, 0x6a, 0xff, 0x68, 0x76, 0x2c, 0x66, 0x00),  // push -1; push handler
    HOOK(0x5d4cf0, graphic_win_ctor, 0x6a, 0xff, 0x68, 0x18, 0x2b, 0x66, 0x00),
    HOOK(0x5d4dd0, graphic_win_dtor, 0x6a, 0xff, 0x68, 0x2a, 0x2b, 0x66, 0x00),
    // push esi; push edi; mov esi, ecx; call win_close
    HOOK(0x5edf50, win_set_caption, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x86, 0xe0, 0x00, 0x00, 0x00),  // push esi; mov esi, ecx; push edi; mov eax, [esi+0xe0]
    HOOK_CALL(0x5d4e40, graphic_win_close, 0x56, 0x57, 0x8b, 0xf1, 0xe8, 0xf7, 0x67, 0x01, 0x00),
    HOOK(0x5eb640, win_close, 0x51, 0xa1, 0xcc, 0x7a, 0x9b, 0x00, 0x8b, 0x15, 0xd0, 0x7a, 0x9b, 0x00),  // push ecx; mov eax, [press1]; mov edx, [press2]
    HOOK(0x5ebd80, win_init, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff, 0x68, 0xc2, 0x2c, 0x66, 0x00),  // mov eax, fs:0; push -1; push handler
    HOOK(0x5d4ef0, graphic_win_init, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff, 0x68, 0x3f, 0x2b, 0x66, 0x00),
    HOOK(0x5f8920, pulldown_init, 0xa1, 0x68, 0x7b, 0x9b, 0x00, 0x56, 0x85, 0xc0, 0x8b, 0xf1),  // mov eax, [0x9b7b68]; push esi; test eax, eax
};
struct hook_module win_module = {"win", win_hooks, HOOK_COUNT(win_hooks), win_sweep, NULL, -1};
