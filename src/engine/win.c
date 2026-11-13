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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "shadow.h"
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

// ---- Sweep ---------------------------------------------------------------------------------

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

enum { POOL = 14, REAL = 24 };
static struct game_win pool[POOL];
static struct game_win_node nodes[POOL];
static struct game_win_scroll scrolls[2];
static void *stub_vtable[0x180 / 4];

// The stand-in for a subclass's NC_HEIGHT slot: a number that depends on the window.
static int THISCALL stub_nc_height(struct game_win *w) { return w->title_h * 3 + 1; }

static const uint32_t FLAG_BITS[] = {WIN_F_MARGIN_H, WIN_F_MARGIN_W, WIN_F_CAPTION, WIN_F_CHILD, WIN_F_THICK, WIN_F_DIALOG,
                                     WIN_F_PARENT_INNER, WIN_F_ALWAYS_TOP, WIN_F_DISABLED, 0x1, 0x4000000};
static const uint32_t FLAG2_BITS[] = {WIN_F2_VISIBLE, WIN_F2_VISIBLE, WIN_F2_VISIBLE, WIN_F2_FRAME, WIN_F2_BUSY};

// A forest of scratch windows: a window hangs from a lower-numbered one or from none, and
// has random flags, rectangles and borders.
static void build_forest(void) {
    for (int i = 0; i < POOL; i++) {
        struct game_win *w = &pool[i];
        memset(w, 0, sizeof *w);
        w->vtable = stub_vtable;
        w->root = rnd_to(4) ? w : &pool[rnd_to(POOL)];
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
        w->nc_owner = rnd_to(3) == 0 ? &pool[rnd_to(POOL)] : NULL;
        w->scroll_v = rnd_to(3) ? &scrolls[0] : NULL;
        w->scroll_h = rnd_to(3) ? &scrolls[1] : NULL;
        nodes[i].win = &pool[rnd_to(POOL)];
        w->dlg_head = rnd_to(2) ? &nodes[rnd_to(POOL)] : NULL;
        w->dlg_cur = &nodes[rnd_to(POOL)];
        w->dlg_count = rnd_to(4);
        if (i > 0 && rnd_to(5)) {
            struct game_win *p = &pool[rnd_to(i)];
            w->parent = p;
            if (rnd_to(2)) p->children[p->n_children++] = w;
            else {
                for (int j = p->n_children; j > 0; j--) p->children[j] = p->children[j - 1];
                p->children[0] = w, p->n_children++;
            }
        }
    }
    for (int s = 0; s < 2; s++) {
        scrolls[s].pos = rnd_to(100), scrolls[s].paging = rnd_to(100);
        scrolls[s].f2144 = rnd_to(100), scrolls[s].f2148 = rnd_to(100);
    }
}

// The game's globals this module uses, kept for putting back.
struct win_globals {
    struct game_win *zorder[514], *parents[44], *modals[16];
    int32_t z_n, parent_n, modal_n, modal_aux[16], modal_prev, opaque_flag, nc_margin;
    struct game_win *modal, *modal_focus, *focus, *def_focus, *key, *opaque;
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
}

static struct game_win *any_pool(void) { return rnd_to(8) == 0 ? NULL : &pool[rnd_to(POOL)]; }

// Random lists and focus windows over the scratch windows.
static void random_globals(void) {
    *game_win_nc_margin = rnd_to(10);
    int n = rnd_to(8);
    *game_win_parent_n = n;
    for (int i = 0; i < n; i++) game_win_parents[i] = &pool[rnd_to(POOL)];
    *game_win_z_n = rnd_to(4);
    for (int i = 0; i < *game_win_z_n; i++) game_win_zorder[i] = &pool[rnd_to(POOL)];
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

    // Scratch windows.
    for (int round = 0; round < 40; round++) {
        stub_vtable[WIN_VT_NC_HEIGHT / 4] = (void *)stub_nc_height;
        build_forest();
        random_globals();
        for (int k = 0; k < 3 * POOL; k++) sweep_window(&pool[k % POOL], any_pool());
        // Lists and focus.
        check_win_get_key_window();
        for (int k = 0; k < 6; k++) check_win_get_key_window(), check_win_in_modal(&pool[rnd_to(POOL)]);
        check_win_set_def_focus(any_pool());
        check_win_update_zorder();
        for (int k = 0; k < 6; k++) check_win_recurse_zorder(&pool[rnd_to(POOL)]);
        for (int k = 0; k < 6; k++) {
            check_win_bring_to_top(&pool[rnd_to(POOL)]);
            struct game_win *p = &pool[rnd_to(POOL)];
            check_win_bring_child_to_top(p, p->n_children && rnd_to(5) ? p->children[rnd_to(p->n_children)] : any_pool());
            check_win_bring_parent_to_top(*game_win_parent_n && rnd_to(5) ? game_win_parents[rnd_to(*game_win_parent_n)] : any_pool());
        }
        // The opaque window among the top-level ones, so the list starts over there.
        for (int k = 0; k < 4; k++) {
            if (*game_win_parent_n) *game_win_opaque = game_win_parents[rnd_to(*game_win_parent_n)];
            check_win_update_zorder();
            check_win_recurse_zorder(&pool[rnd_to(POOL)]);
            check_win_bring_to_top(&pool[rnd_to(POOL)]);
        }
        for (int k = 0; k < 4; k++) {
            check_win_add_parent(&pool[rnd_to(POOL)]);
            check_win_add_parent(NULL);
            struct game_win *p = &pool[rnd_to(POOL)];
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
            struct game_win *w = &pool[k];
            check_win_set_vert_paging(w, rnd_to(1000));
            check_win_set_horz_paging(w, rnd_to(1000));
            check_win_unk8(w, rnd_to(1000));
            check_win_unk9(w, rnd_to(1000));
            check_win_move(w, rnd_to(500) - 150, rnd_to(500) - 150);
            check_win_move(w, w->inner[0], w->outer[1]);
        }
    }
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
};
struct hook_module win_module = {"win", win_hooks, HOOK_COUNT(win_hooks), win_sweep, NULL, -1};
