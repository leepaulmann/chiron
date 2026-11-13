// Unit test for the Win class's geometry, tree and lists (win.c) on small hand-built trees of
// windows: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/win.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))

static struct game_win *modal, *modal_focus, *focus, *def_focus, *key, *opaque;
static struct game_win *modals[16], *parents[44], *zorder[514];
static int32_t modal_n, modal_aux[16], modal_prev, parent_n, z_n, opaque_flag, nc_margin;
static void *vtable[0x180 / 4];
static int WIN_THIS owner_height(struct game_win *w) { return w->title_h * 3 + 1; }

static struct game_win R, A, B, C, W, P, O;
static struct game_win_node node;

static void init(struct game_win *w, uint32_t flags2) {
    memset(w, 0, sizeof *w);
    w->vtable = vtable;
    w->root = w;
    w->flags2 = flags2;
}
static void adopt(struct game_win *p, struct game_win *c) {
    c->parent = p;
    p->children[p->n_children++] = c;
}
// R holds A and C; A holds B. A, B and R are shown, C is not.
static void tree(void) {
    init(&R, WIN_F2_VISIBLE), init(&A, WIN_F2_VISIBLE), init(&B, WIN_F2_VISIBLE), init(&C, 0);
    adopt(&R, &A), adopt(&R, &C), adopt(&A, &B);
}
static void lists(void) {
    memset(zorder, 0, sizeof zorder), memset(parents, 0, sizeof parents);
    parent_n = 0, z_n = 0, opaque = NULL, opaque_flag = 1;
}

int main(void) {
    game_win_modal = &modal, game_win_modal_focus = &modal_focus, game_win_focus = &focus, game_win_def_focus = &def_focus;
    game_win_key = &key, game_win_opaque = &opaque, game_win_modals = modals, game_win_parents = parents;
    game_win_zorder = zorder, game_win_modal_n = &modal_n, game_win_modal_aux = modal_aux, game_win_modal_prev = &modal_prev;
    game_win_parent_n = &parent_n, game_win_z_n = &z_n, game_win_opaque_flag = &opaque_flag, game_win_nc_margin = &nc_margin;

    // Visibility needs the window and every window above it shown.
    tree();
    EXPECT(win_is_visible(&B), 1), EXPECT(win_is_visible(&C), 0);
    A.flags2 = 0;
    EXPECT(win_is_visible(&B), 0), EXPECT(win_is_visible(&A), 0), EXPECT(win_is_visible(&R), 1);
    A.flags2 = WIN_F2_VISIBLE, R.flags2 = 0;
    EXPECT(win_is_visible(&A), 0);
    R.flags2 = WIN_F2_VISIBLE;

    // The tree questions.
    EXPECT(win_is_descendant(&R, &B), 1), EXPECT(win_is_descendant(&R, &C), 1), EXPECT(win_is_descendant(&A, &C), 0);
    EXPECT(win_is_descendant(&R, NULL), 0), EXPECT(win_is_descendant(&B, &R), 0), EXPECT(win_is_descendant(&R, &R), 0);
    EXPECT(win_is_below(&B, &R), 1), EXPECT(win_is_below(&C, &A), 0), EXPECT(win_is_below(&R, NULL), 0);
    EXPECT(win_is_below(&A, &A), 0);
    EXPECT(win_has_child(&R, &A), 1), EXPECT(win_has_child(&R, &B), 0), EXPECT(win_has_child(&R, NULL), 0);
    modal = NULL;
    EXPECT(win_in_modal(&C), 1);
    modal = &A;
    EXPECT(win_in_modal(&B), 1), EXPECT(win_in_modal(&A), 1), EXPECT(win_in_modal(&C), 0), EXPECT(win_in_modal(&R), 0);
    modal = NULL;

    // A dialog's focus.
    memset(&node, 0, sizeof node);
    node.win = &A;
    R.dlg_head = R.dlg_cur = &node;
    EXPECT(win_is_dialog_focus(&A), 1), EXPECT(win_is_dialog_focus(&C), 0), EXPECT(win_is_dialog_focus(&R), 0);
    C.flags = WIN_F_DIALOG;
    EXPECT(win_is_dialog_focus(&C), 1);
    C.flags = 0;
    R.dlg_head = NULL;
    EXPECT(win_is_dialog_focus(&A), 0);
    win_set_def_focus(&B);
    EXPECT(def_focus == &B, 1);

    // The window the keys go to.
    modal = NULL, focus = NULL, def_focus = &B;
    EXPECT(win_get_key_window() == &B, 1), EXPECT(key == &B, 1);
    focus = &A;
    EXPECT(win_get_key_window() == &A, 1), EXPECT(key == &A, 1);
    B.root = &A;
    focus = &B;
    EXPECT(win_get_key_window() == &A, 1), EXPECT(key == &B, 1);
    R.flags2 = 0;  // what the root hangs from is hidden
    EXPECT(win_get_key_window() == NULL, 1);
    R.flags2 = WIN_F2_VISIBLE;
    modal = &C, modal_focus = NULL;
    EXPECT(win_get_key_window() == NULL, 1), EXPECT(key == &C, 1);  // C is hidden
    modal_focus = &B;
    EXPECT(win_get_key_window() == &A, 1), EXPECT(key == &B, 1);
    modal = NULL, modal_focus = NULL, focus = NULL, def_focus = NULL;
    EXPECT(win_get_key_window() == NULL, 1), EXPECT(key == NULL, 1);
    B.root = &B;

    // The parent list: a window that keeps its place goes at the back, any other in front.
    lists();
    win_add_parent(&A);
    win_add_parent(NULL);
    win_add_parent(&B);
    B.flags |= WIN_F_ALWAYS_TOP;
    win_add_parent(&B);
    B.flags = 0;
    EXPECT(parent_n, 3), EXPECT(parents[0] == &B, 1), EXPECT(parents[1] == &A, 1), EXPECT(parents[2] == &B, 1);
    tree();
    win_add_child(&R, &B);
    EXPECT(R.n_children, 3), EXPECT(R.children[0] == &B, 1), EXPECT(R.children[1] == &A, 1), EXPECT(R.children[2] == &C, 1);
    B.flags = WIN_F_ALWAYS_TOP;
    win_add_child(&R, &B);
    EXPECT(R.n_children, 4), EXPECT(R.children[3] == &B, 1);
    win_add_child(&R, NULL);
    EXPECT(R.n_children, 4);
    B.flags = 0;

    // The modal stack.
    modals[0] = &A, modals[1] = &B, modals[2] = &C;
    modal_aux[0] = 10, modal_aux[1] = 20, modal_aux[2] = 30;
    modal_n = 3, modal = &C, modal_prev = 0;
    win_release_modal(&C);
    EXPECT(modal_n, 2), EXPECT(modals[2] == NULL, 1), EXPECT(modal_aux[2], 0), EXPECT(modal == &B, 1), EXPECT(modal_prev, 20);
    win_release_modal(&O);  // not there: the stack is as it was
    EXPECT(modal_n, 2), EXPECT(modals[0] == &A, 1), EXPECT(modals[1] == &B, 1), EXPECT(modal == &B, 1);
    win_release_modal(&A);  // not the top: the ones above close over it
    EXPECT(modal_n, 1), EXPECT(modals[0] == &B, 1), EXPECT(modal_aux[0], 20), EXPECT(modal == &B, 1);
    win_release_modal(&B);
    EXPECT(modal_n, 0), EXPECT(modals[0] == NULL, 1), EXPECT(modal == NULL, 1);
    win_release_modal(&B);
    EXPECT(modal_n, 0);

    // The z-order list, back to front: B, A, R (C is hidden).
    tree(), lists();
    parents[0] = &R, parent_n = 1;
    win_update_zorder();
    EXPECT(z_n, 3), EXPECT(zorder[0] == &B, 1), EXPECT(zorder[1] == &A, 1), EXPECT(zorder[2] == &R, 1);
    // An opaque window that is a (hidden) child of R starts the list over: only R is left.
    opaque = &C;
    win_update_zorder();
    EXPECT(z_n, 1), EXPECT(zorder[0] == &R, 1);
    // One that is shown: the windows before it stay, as they come after it in the list.
    opaque = &A;
    win_update_zorder();
    EXPECT(z_n, 3), EXPECT(zorder[0] == &B, 1);
    // A top-level opaque window drops what came before it and clears the flag.
    tree(), lists();
    init(&W, WIN_F2_VISIBLE);
    parents[0] = &R, parents[1] = &W, parent_n = 2, opaque = &W;
    win_update_zorder();
    EXPECT(z_n, 1), EXPECT(zorder[0] == &W, 1), EXPECT(opaque_flag, 0);
    // Children positioned relative to the window (WIN_F_CHILD) come in the second round, for a
    // window that is its own root.
    tree(), lists();
    A.flags = WIN_F_CHILD;
    parents[0] = &R, parent_n = 1;
    win_update_zorder();
    EXPECT(z_n, 3), EXPECT(zorder[0] == &B, 1), EXPECT(zorder[1] == &A, 1), EXPECT(zorder[2] == &R, 1);
    R.root = &A;  // not its own root: its children of that kind are left out
    win_update_zorder();
    EXPECT(z_n, 1), EXPECT(zorder[0] == &R, 1);
    R.root = &R;
    A.flags = 0;
    // Bringing to the front reorders and makes the list again.
    tree(), lists();
    parents[0] = &R, parents[1] = &W, parent_n = 2;
    win_bring_to_top(&W);
    EXPECT(parents[0] == &W, 1), EXPECT(parents[1] == &R, 1), EXPECT(z_n, 4);
    EXPECT(zorder[0] == &W, 1);
    win_bring_to_top(&C);
    EXPECT(R.children[0] == &C, 1), EXPECT(R.children[1] == &A, 1);
    win_bring_to_top(&A);
    EXPECT(R.children[0] == &A, 1), EXPECT(R.children[1] == &C, 1);
    C.flags = WIN_F_ALWAYS_TOP;  // a window that keeps its place isn't moved
    win_bring_to_top(&C);
    win_bring_child_to_top(&R, &C);
    EXPECT(R.children[0] == &A, 1);
    C.flags = 0;
    win_bring_child_to_top(&R, &C);
    EXPECT(R.children[0] == &C, 1), EXPECT(R.children[1] == &A, 1);
    win_bring_child_to_top(&R, NULL);
    win_bring_child_to_top(&R, &O);  // not a child: nothing moves
    EXPECT(R.children[0] == &C, 1);
    win_bring_parent_to_top(&R);
    EXPECT(parents[0] == &R, 1), EXPECT(parents[1] == &W, 1);
    win_bring_parent_to_top(NULL);
    win_bring_parent_to_top(&O);  // not in the list: nothing moves
    EXPECT(parents[0] == &R, 1);

    // Coordinates. W is placed at (10, 20) with an inner origin of (1, 2), under P at (100, 100)
    // with an inner origin of (3, 4).
    init(&W, WIN_F2_VISIBLE), init(&P, WIN_F2_VISIBLE);
    W.outer[0] = 10, W.outer[1] = 20, W.outer[2] = 110, W.outer[3] = 220;
    W.inner[0] = 1, W.inner[1] = 2, W.inner[2] = 90, W.inner[3] = 190;
    P.outer[0] = 100, P.outer[1] = 100, P.inner[0] = 3, P.inner[1] = 4;
    int32_t x = 5, y = 6;
    win_client_to_screen_2(&W, &x, &y);
    EXPECT(x, 16), EXPECT(y, 28);
    win_screen_to_client_2(&W, &x, &y);
    EXPECT(x, 5), EXPECT(y, 6);
    W.parent = &P;  // without WIN_F_CHILD the parent is ignored
    win_client_to_screen_2(&W, &x, &y);
    EXPECT(x, 16), EXPECT(y, 28);
    W.flags = WIN_F_CHILD;
    x = 5, y = 6;
    win_client_to_screen_2(&W, &x, &y);
    EXPECT(x, 16 + 103), EXPECT(y, 28 + 104);
    win_screen_to_client_2(&W, &x, &y);
    EXPECT(x, 5), EXPECT(y, 6);
    W.flags = WIN_F_CHILD | WIN_F_PARENT_INNER;
    win_client_to_screen_2(&W, &x, &y);
    EXPECT(x, 16 + 103 - 3), EXPECT(y, 28 + 104 - 4);
    win_screen_to_client_2(&W, &x, &y);
    EXPECT(x, 5), EXPECT(y, 6);
    win_nonclient_to_screen_2(&W, &x, &y);
    EXPECT(x, 16 + 100 - 1), EXPECT(y, 28 + 100 - 2);
    win_screen_to_nonclient(&W, &x, &y);
    EXPECT(x, 5), EXPECT(y, 6);
    int32_t r[4] = {0, 0, 10, 10};
    win_client_to_screen(&W, r);
    EXPECT(r[0], 111), EXPECT(r[1], 122), EXPECT(r[2], 121), EXPECT(r[3], 132);
    win_screen_to_client(&W, r);
    EXPECT(r[0], 0), EXPECT(r[1], 0), EXPECT(r[2], 10), EXPECT(r[3], 10);
    win_nonclient_to_screen(&W, r);
    EXPECT(r[0], 110), EXPECT(r[1], 120), EXPECT(r[2], 120), EXPECT(r[3], 130);
    win_nonscreen_to_client(&W, r);
    EXPECT(r[0], 0), EXPECT(r[1], 0), EXPECT(r[2], 10), EXPECT(r[3], 10);
    win_client_to_screen(&W, NULL);  // nothing without a rectangle
    // Through two parents.
    init(&O, WIN_F2_VISIBLE);
    O.outer[0] = 1000, O.outer[1] = 2000;
    P.parent = &O, P.flags = WIN_F_CHILD;
    x = 0, y = 0;
    win_client_to_screen_2(&W, &x, &y);
    EXPECT(x, 11 + 103 + 1000 - 3), EXPECT(y, 22 + 104 + 2000 - 4 + 0);
    win_screen_to_client_2(&W, &x, &y);
    EXPECT(x, 0), EXPECT(y, 0);
    P.parent = NULL, P.flags = 0, W.parent = NULL, W.flags = 0;

    // The nonclient conversions: the frame's margin, border, caption and the owner's height.
    init(&O, 0);
    O.vtable = vtable, O.title_h = 4;
    vtable[WIN_VT_NC_HEIGHT / 4] = (void *)owner_height;
    nc_margin = 5;
    W.flags = WIN_F_MARGIN_H | WIN_F_THICK | WIN_F_CAPTION;
    W.border = 2, W.bottom = -1, W.caption_h = 9;
    x = 100, y = 200;
    win_client_to_nonclient(&W, &x, &y);
    EXPECT(x, 104), EXPECT(y, 216);
    win_nonclient_to_client(&W, &x, &y);
    EXPECT(x, 100), EXPECT(y, 200);
    W.bottom = 3, W.flags |= WIN_F_MARGIN_W;
    win_client_to_nonclient(&W, &x, &y);
    EXPECT(x, 109), EXPECT(y, 217);
    win_nonclient_to_client(&W, &x, &y);
    EXPECT(x, 100), EXPECT(y, 200);
    W.nc_owner = &O;
    win_client_to_nonclient(&W, &x, &y);
    EXPECT(x, 109), EXPECT(y, 217 + 13);
    win_nonclient_to_client(&W, &x, &y);
    EXPECT(x, 100), EXPECT(y, 200);
    win_client_to_nonclient(&W, NULL, &y);
    win_nonclient_to_client(&W, &x, NULL);
    EXPECT(x, 100), EXPECT(y, 200);
    W.nc_owner = NULL;
    W.flags = 0x1;  // any frame (flag bit 1) counts the border
    W.bottom = -1, nc_margin = 0;
    win_client_to_nonclient(&W, &x, &y);
    EXPECT(x, 104), EXPECT(y, 204);
    int32_t q[4] = {10, 20, 30, 40};
    W.flags = WIN_F_THICK | WIN_F_MARGIN_H | WIN_F_MARGIN_W | WIN_F_CAPTION, W.bottom = 5, nc_margin = 7;
    win_client_to_nonclient_2(&W, q);
    EXPECT(q[0], 8), EXPECT(q[1], 11), EXPECT(q[2], 39), EXPECT(q[3], 52);
    W.nc_owner = &O;
    win_client_to_nonclient_2(&W, q);
    EXPECT(q[0], 6), EXPECT(q[1], -11), EXPECT(q[2], 48), EXPECT(q[3], 64);
    W.nc_owner = NULL;
    win_client_to_nonclient_2(&W, NULL);

    // Moving a window keeps its size: the outer rectangle with a frame, the inner one without.
    W.flags = 0;
    W.inner[0] = 1, W.inner[1] = 2, W.inner[2] = 11, W.inner[3] = 22;
    EXPECT(win_move(&W, 5, 7), 0);
    EXPECT(W.inner[0], 5), EXPECT(W.inner[1], 7), EXPECT(W.inner[2], 15), EXPECT(W.inner[3], 27), EXPECT(W.outer[0], 10);
    W.flags2 = WIN_F2_FRAME;
    EXPECT(win_move(&W, 0, 0), 0);
    EXPECT(W.outer[0], 0), EXPECT(W.outer[1], 0), EXPECT(W.outer[2], 100), EXPECT(W.outer[3], 200), EXPECT(W.inner[0], 5);

    // The hit test: corners, edges and the caption.
    W.flags2 = WIN_F2_FRAME, W.outer[0] = 0, W.outer[1] = 0, W.outer[2] = 100, W.outer[3] = 50;
    W.resize_border = 4, W.title_h = 10, W.flags = WIN_F_CAPTION;
    EXPECT(win_on_nc_hittest(&W, 2, 2), 13), EXPECT(win_on_nc_hittest(&W, 2, 48), 16);
    EXPECT(win_on_nc_hittest(&W, 98, 2), 14), EXPECT(win_on_nc_hittest(&W, 98, 48), 17);
    EXPECT(win_on_nc_hittest(&W, 2, 20), 10), EXPECT(win_on_nc_hittest(&W, 50, 2), 12);
    EXPECT(win_on_nc_hittest(&W, 98, 20), 11), EXPECT(win_on_nc_hittest(&W, 50, 48), 15);
    EXPECT(win_on_nc_hittest(&W, 50, 5), 2), EXPECT(win_on_nc_hittest(&W, 50, 20), 0);
    W.title_h = 0;
    EXPECT(win_on_nc_hittest(&W, 50, 5), 0);
    W.flags = 0x1;
    EXPECT(win_on_nc_hittest(&W, 50, 20), 2);
    W.flags = 0x4000000, W.title_h = 30;
    EXPECT(win_on_nc_hittest(&W, 50, 20), 2), EXPECT(win_on_nc_hittest(&W, 50, 30), 0);
    W.flags2 = 0;  // no frame: only the caption
    EXPECT(win_on_nc_hittest(&W, 2, 2), 2), EXPECT(win_on_nc_hittest(&W, 2, 40), 0);

    // Scroll bars.
    static struct game_win_scroll sv, sh;
    memset(&sv, 0, sizeof sv), memset(&sh, 0, sizeof sh);
    W.scroll_v = &sv, W.scroll_h = &sh;
    sv.pos = 7, sh.pos = 9;
    EXPECT(win_get_vert_pos(&W), 7), EXPECT(win_get_horz_pos(&W), 9);
    win_set_vert_paging(&W, 11), win_set_horz_paging(&W, 12);
    EXPECT(sv.paging, 11), EXPECT(sh.paging, 12);
    win_unk8(&W, 3), win_unk9(&W, 4);
    EXPECT(sv.f2144, 3), EXPECT(sh.f2144, 3), EXPECT(sv.f2148, 4), EXPECT(sh.f2148, 4);
    W.scroll_v = W.scroll_h = NULL;
    EXPECT(win_get_vert_pos(&W), 0), EXPECT(win_get_horz_pos(&W), 0);
    win_set_vert_paging(&W, 1), win_set_horz_paging(&W, 1), win_unk8(&W, 1), win_unk9(&W, 1);

    if (failures) return 1;
    printf("engine_win: ok\n");
    return 0;
}
