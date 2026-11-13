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


// ---- Stand-ins for the vtable slots and the Windows calls the second set of functions makes ----
#define STD __attribute__((stdcall))
static struct {
    int slot;
    void *w;
    int a, b, c;
} vlog_[64];
static int nvlog;
static void vrec(int slot, void *w, int a, int b, int c) {
    if (nvlog < 64) vlog_[nvlog].slot = slot, vlog_[nvlog].w = w, vlog_[nvlog].a = a, vlog_[nvlog].b = b, vlog_[nvlog].c = c;
    nvlog++;
}
#define STUB0(slot) \
    static int WIN_THIS stub_##slot(struct game_win *w) { return vrec(slot, w, 0, 0, 0), slot + 1; }
#define STUB1(slot) \
    static int WIN_THIS stub_##slot(struct game_win *w, int a) { return vrec(slot, w, a, 0, 0), slot + 1; }
#define STUB2(slot) \
    static int WIN_THIS stub_##slot(struct game_win *w, int a, int b) { return vrec(slot, w, a, b, 0), slot + 1; }
#define STUB3(slot) \
    static int WIN_THIS stub_##slot(struct game_win *w, int a, int b, int c) { return vrec(slot, w, a, b, c), slot + 1; }
STUB0(0x8) STUB3(0xc) STUB0(0x10) STUB0(0x30) STUB2(0x34) STUB1(0xcc) STUB0(0xf8) STUB0(0x108) STUB2(0x10c) STUB2(0x128)
static void stub_cb(int a, int b) { vrec(0x400, NULL, a, b, 0); }
static void fill_vtable(void) {
    vtable[0x8 / 4] = (void *)stub_0x8, vtable[0xc / 4] = (void *)stub_0xc, vtable[0x10 / 4] = (void *)stub_0x10;
    vtable[0x30 / 4] = (void *)stub_0x30, vtable[0x34 / 4] = (void *)stub_0x34, vtable[0xcc / 4] = (void *)stub_0xcc;
    vtable[0xf8 / 4] = (void *)stub_0xf8, vtable[0x108 / 4] = (void *)stub_0x108, vtable[0x10c / 4] = (void *)stub_0x10c;
    vtable[0x128 / 4] = (void *)stub_0x128;
}
static void expect_call(int i, int slot, void *w, int a, int b, int c) {
    char what[64];
    snprintf(what, sizeof what, "call %d slot", i);
    check_(what, i < nvlog ? vlog_[i].slot : -1, slot);
    if (i >= nvlog) return;
    snprintf(what, sizeof what, "call %d this", i), check_(what, (long)vlog_[i].w, (long)w);
    snprintf(what, sizeof what, "call %d a", i), check_(what, vlog_[i].a, a);
    snprintf(what, sizeof what, "call %d b", i), check_(what, vlog_[i].b, b);
    snprintf(what, sizeof what, "call %d c", i), check_(what, vlog_[i].c, c);
}

static int cursor[2], cursor_set[2], cursor_sets, shown_cmd, shown_hwnd, shown, swapped, zoomed, got_cursor;
static int16_t keys[4];
static int STD set_cursor_pos_(int x, int y) { return cursor_set[0] = x, cursor_set[1] = y, cursor_sets++, 7; }
static int STD get_cursor_pos_(int32_t *p) { return p[0] = cursor[0], p[1] = cursor[1], got_cursor++, 1; }
static int STD show_window_(uint32_t h, int cmd) { return shown_hwnd = (int)h, shown_cmd = cmd, shown++, 5; }
static int STD get_system_metrics_(int i) { return i == 0x17 ? swapped : -1; }
static int STD is_zoomed_(uint32_t h) { return zoomed; }
static int16_t STD key_state_(int k) { return keys[k]; }

static struct game_win *cur_, *capture_, *hover_, *press1_, *press2_, *prev_focus_;
static uint32_t hwnd_;
static int32_t screen_w_, screen_h_, pic_normal_, pic_zoomed_;

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


    // ---- The second set: calls through the vtable and Windows ----
    game_win_cur = &cur_, game_win_capture = &capture_, game_win_hover = &hover_, game_win_press1 = &press1_;
    game_win_press2 = &press2_, game_win_prev_focus = &prev_focus_, game_win_hwnd = &hwnd_, game_win_screen_w = &screen_w_;
    game_win_screen_h = &screen_h_, game_win_pic_normal = &pic_normal_, game_win_pic_zoomed = &pic_zoomed_;
    game_set_cursor_pos = set_cursor_pos_, game_get_cursor_pos = get_cursor_pos_, game_show_window = show_window_;
    game_get_system_metrics = get_system_metrics_, game_is_zoomed = is_zoomed_, game_get_async_key_state = key_state_;
    fill_vtable();

    // Taking a window off the lists hides it first and closes the list up.
    tree();
    lists();
    parents[0] = &A, parents[1] = &B, parents[2] = &C, parent_n = 3;
    nvlog = 0;
    win_remove_parent(&B);
    EXPECT(parent_n, 2), EXPECT(parents[0] == &A, 1), EXPECT(parents[1] == &C, 1), EXPECT(nvlog, 1);
    expect_call(0, 0x8, &B, 0, 0, 0);
    win_remove_parent(&B), win_remove_parent(NULL);
    EXPECT(parent_n, 2), EXPECT(nvlog, 1);
    win_remove_parent(&C);
    EXPECT(parent_n, 1), EXPECT(parents[0] == &A, 1);
    tree();
    nvlog = 0;
    win_remove_child(&R, &A);
    EXPECT(R.n_children, 1), EXPECT(R.children[0] == &C, 1), EXPECT(nvlog, 1);
    expect_call(0, 0x8, &A, 0, 0, 0);
    win_remove_child(&R, &A), win_remove_child(&R, NULL), win_remove_child(&R, &B);
    EXPECT(R.n_children, 1), EXPECT(nvlog, 1);

    // Scroll bars: the position is kept in the range, an inverted range counts from the far end,
    // and a bar without a parent isn't touched.
    static struct game_win_scroll bsv, bsh;
    memset(&bsv, 0, sizeof bsv), memset(&bsh, 0, sizeof bsh);
    bsv.win.vtable = vtable, bsh.win.vtable = vtable;
    bsv.win.parent = &P;
    init(&W, 0);
    W.scroll_v = &bsv, W.scroll_h = &bsh;
    nvlog = 0;
    win_set_vert_range(&W, 10, 20);
    EXPECT(bsv.lo, 10), EXPECT(bsv.hi, 20), EXPECT(bsv.pos, 10), EXPECT(bsv.inverted, 0), EXPECT(nvlog, 1);
    expect_call(0, 0xf8, &bsv, 0, 0, 0);
    win_set_vert_pos(&W, 15);
    EXPECT(bsv.pos, 15), EXPECT(cur_ == &P, 1);
    win_set_vert_pos(&W, 99);
    EXPECT(bsv.pos, 20);
    win_set_vert_pos(&W, -5);
    EXPECT(bsv.pos, 10);
    win_set_vert_range(&W, 20, 10);
    EXPECT(bsv.lo, 10), EXPECT(bsv.hi, 20), EXPECT(bsv.pos, 10), EXPECT(bsv.inverted, 1);
    win_set_vert_pos(&W, 12);
    EXPECT(bsv.pos, 18);
    win_set_vert_pos(&W, 5);
    EXPECT(bsv.pos, 20);
    win_set_vert_range(&W, 7, 7);
    EXPECT(bsv.lo, 7), EXPECT(bsv.hi, 7), EXPECT(bsv.inverted, 1);
    nvlog = 0;
    win_set_horz_range(&W, 1, 2), win_set_horz_pos(&W, 2);
    EXPECT(bsh.lo, 1), EXPECT(bsh.pos, 1), EXPECT(nvlog, 1);  // no parent: the position isn't set
    W.scroll_v = W.scroll_h = NULL;
    win_set_vert_pos(&W, 1), win_set_horz_pos(&W, 1), win_set_vert_range(&W, 1, 2), win_set_horz_range(&W, 1, 2);
    EXPECT(nvlog, 1);

    // The caption and the borders size a window with a frame again to the rectangle it has.
    init(&W, WIN_F2_FRAME);
    W.outer[0] = 10, W.outer[1] = 20, W.outer[2] = 110, W.outer[3] = 80;
    nvlog = 0;
    win_set_caption_height(&W, 7);
    EXPECT(W.caption_h, 7), EXPECT(nvlog, 1);
    expect_call(0, 0xc, &W, 100, 60, 0);
    win_set_border_thickness(&W, 3), win_set_bottom_border_thickness(&W, 4);
    EXPECT(W.border, 3), EXPECT(W.bottom, 4), EXPECT(nvlog, 3);
    W.flags2 = 0;
    win_set_caption_height(&W, 9), win_set_border_thickness(&W, 9), win_set_bottom_border_thickness(&W, 9);
    EXPECT(W.caption_h, 7), EXPECT(W.border, 3), EXPECT(W.bottom, 4), EXPECT(nvlog, 3);

    // The dialog list: a ring of three windows.
    static struct game_win_node ring[3];
    init(&P, 0), init(&W, 0);
    ring[0].win = &A, ring[1].win = &B, ring[2].win = &C;
    ring[0].next = &ring[1], ring[1].next = &ring[2], ring[2].next = &ring[0];
    P.dlg_head = &ring[0], P.dlg_cur = &ring[0], P.dlg_count = 3, P.dlg_index = 0;
    W.parent = &B;
    nvlog = 0;
    win_set_dialog_focus(&P, &C);
    EXPECT(P.dlg_cur == &ring[2], 1), EXPECT(P.dlg_index, 2), EXPECT(prev_focus_ == &A, 1), EXPECT(nvlog, 2);
    expect_call(0, 0xcc, &A, 0, 0, 0), expect_call(1, 0xcc, &C, 1, 0, 0);
    win_set_dialog_focus(&P, &C), win_set_dialog_focus(&P, NULL);
    EXPECT(nvlog, 2);
    win_set_dialog_focus(&P, &R);  // not in the ring: the search goes all the way round
    EXPECT(P.dlg_cur == &ring[0], 1), EXPECT(P.dlg_index, 3), EXPECT(prev_focus_ == &C, 1), EXPECT(nvlog, 2);
    P.dlg_count = 0;
    win_set_dialog_focus(&P, &A);
    EXPECT(nvlog, 2), EXPECT(P.dlg_index, 3);
    P.dlg_count = 3, P.dlg_cur = &ring[0], P.dlg_index = 0;
    // Passing the focus: the window itself is told it lost it, the next one that it gained it.
    ring[0].win = &W, ring[1].win = &A;
    P.dlg_head = &ring[0];
    nvlog = 0;
    P.flags = 0;
    B.parent = &P;
    win_pass_dialog_focus(&B);
    EXPECT(P.dlg_cur == &ring[1], 1), EXPECT(P.dlg_index, 1), EXPECT(prev_focus_ == &W, 1), EXPECT(nvlog, 2);
    expect_call(0, 0xcc, &B, 0, 0, 0), expect_call(1, 0xcc, &A, 1, 0, 0);
    win_pass_dialog_focus(&B), win_pass_dialog_focus(&B);
    EXPECT(P.dlg_index, 0), EXPECT(P.dlg_cur == &ring[0], 1);
    P.dlg_count = 1;
    nvlog = 0;
    win_pass_dialog_focus(&B);
    EXPECT(nvlog, 0), EXPECT(P.dlg_index, 0);
    B.parent = NULL;
    win_pass_dialog_focus(&B);
    EXPECT(nvlog, 0);

    // The mouse, in a window's client area and the screen.
    init(&W, 0);
    W.outer[0] = 5, W.outer[1] = 6, W.inner[0] = 1, W.inner[1] = 2;
    EXPECT(win_set_mouse_pos(&W, 10, 20), 7);
    EXPECT(cursor_set[0], 16), EXPECT(cursor_set[1], 28);
    init(&P, 0);
    P.outer[0] = 100, P.outer[1] = 200;
    W.parent = &P, W.flags = WIN_F_CHILD;
    win_set_mouse_pos(&W, 10, 20);
    EXPECT(cursor_set[0], 116), EXPECT(cursor_set[1], 228);
    P.inner[0] = 3, P.inner[1] = 4;
    W.flags = WIN_F_CHILD | WIN_F_PARENT_INNER;
    win_set_mouse_pos(&W, 0, 0);  // the parent's inner origin is taken off after its client area is added
    EXPECT(cursor_set[0], 6 + 103 - 3), EXPECT(cursor_set[1], 8 + 204 - 4);
    cursor[0] = 50, cursor[1] = 60;
    int32_t mx = -1, my = -1;
    W.parent = NULL, W.flags = 0;
    win_get_mouse_pos(&W, &mx, &my);
    EXPECT(mx, 44), EXPECT(my, 52);
    W.parent = &P, W.flags = WIN_F_CHILD;
    win_get_mouse_pos(&W, &mx, &my);
    EXPECT(mx, 44 - 103), EXPECT(my, 52 - 204);
    got_cursor = 0, mx = 77;
    win_get_mouse_pos(&W, &mx, NULL), win_get_mouse_pos(&W, NULL, &my);
    EXPECT(mx, 77), EXPECT(got_cursor, 0);
    keys[1] = (int16_t)0x8000, keys[2] = 0x0001;
    swapped = 0;
    EXPECT(win_get_lbutton_state(), -1), EXPECT(win_get_rbutton_state(), 0);
    swapped = 1;
    EXPECT(win_get_lbutton_state(), -2), EXPECT(win_get_rbutton_state(), 1);

    // Maximizing and minimizing: the presses go, a window with the capture is told, and the
    // hover goes if it was this window.
    hwnd_ = 0x1234;
    capture_ = &W, hover_ = &W, press1_ = &A, press2_ = &B;
    nvlog = 0, shown = 0;
    EXPECT(win_maximize(&W), 0);
    EXPECT(capture_ == NULL, 1), EXPECT(hover_ == NULL, 1), EXPECT(press1_ == NULL, 1), EXPECT(press2_ == NULL, 1);
    EXPECT(shown, 1), EXPECT(shown_cmd, 3), EXPECT(shown_hwnd, 0x1234), EXPECT(nvlog, 1);
    expect_call(0, 0x10, &W, 0, 0, 0);
    capture_ = &A, hover_ = &A;
    EXPECT(win_minimize(&W), 0);
    EXPECT(capture_ == &A, 1), EXPECT(hover_ == &A, 1), EXPECT(shown_cmd, 6), EXPECT(nvlog, 1);
    EXPECT(win_show_maximize(&W), 0);
    EXPECT(shown_cmd, 3), EXPECT(shown, 3);

    // Centring: in the screen, or in the parent's client area; sums halved towards zero.
    init(&W, 0);
    screen_w_ = 800, screen_h_ = 600;
    W.inner[2] = 100, W.inner[3] = 50;
    EXPECT(win_center(&W), 0);
    EXPECT(W.inner[0], 350), EXPECT(W.inner[1], 275), EXPECT(W.inner[2], 450), EXPECT(W.inner[3], 325);
    init(&W, WIN_F2_FRAME);
    W.outer[2] = 1001, W.outer[3] = 11;
    win_center(&W);
    EXPECT(W.outer[0], -100), EXPECT(W.outer[1], 294), EXPECT(W.outer[2], 901);
    init(&P, 0), init(&W, 0);
    P.outer[0] = 10, P.outer[1] = 20, P.inner[0] = 1, P.inner[1] = 2;
    W.parent = &P, W.inner[2] = 100, W.inner[3] = 50;
    win_center(&W);
    EXPECT(W.inner[0], 350 - 11), EXPECT(W.inner[1], 275 - 22);

    // The nonclient area taken off a rectangle undoes what the client one put on.
    init(&O, 0);
    O.vtable = vtable;
    vtable[WIN_VT_NC_HEIGHT / 4] = (void *)owner_height;
    init(&W, 0);
    W.flags = WIN_F_MARGIN_H | WIN_F_MARGIN_W | WIN_F_THICK | WIN_F_CAPTION;
    W.border = 2, W.bottom = 5, W.caption_h = 9, W.nc_owner = &O, O.title_h = 4;
    nc_margin = 7;
    int32_t qq[4] = {10, 20, 300, 400}, qq0[4] = {10, 20, 300, 400};
    win_client_to_nonclient_2(&W, qq);
    win_nonclient_to_client_2(&W, qq);
    EXPECT(memcmp(qq, qq0, sizeof qq), 0);
    win_nonclient_to_client_2(&W, NULL);
    W.flags = 0x1, W.nc_owner = NULL;
    win_nonclient_to_client_2(&W, qq);
    EXPECT(qq[0], 12), EXPECT(qq[1], 22), EXPECT(qq[2], 298), EXPECT(qq[3], 398 + 2 - 5);

    // The caption buttons go along the right, 3 and 1 apart; the picture depends on the window.
    static struct game_win_btn b4, b8, bc;
    memset(&b4, 0, sizeof b4), memset(&b8, 0, sizeof b8), memset(&bc, 0, sizeof bc);
    init(&W, WIN_F2_FRAME);
    b4.win.vtable = b8.win.vtable = bc.win.vtable = vtable;
    W.btn_e4 = &b4, W.btn_e8 = &b8, W.btn_ec = &bc;
    W.cap_btn = 5, W.border = 2, W.caption_h = 7;
    W.outer[0] = 10, W.outer[2] = 210;
    b8.width = 20, bc.width = 30;
    b4.win.flags2 = b8.win.flags2 = bc.win.flags2 = WIN_F2_FRAME;
    pic_normal_ = 11, pic_zoomed_ = 22, zoomed = 0;
    win_redo_caption_buttons(&W);
    int start = 210 - 10 - 10 - 2 - 7 - 2;  // 179
    EXPECT(bc.win.outer[0], start), EXPECT(bc.win.outer[1], 5);
    EXPECT(b8.win.outer[0], start - 3 - 30), EXPECT(b8.win.outer[1], 5), EXPECT(b8.picture, 11);
    EXPECT(b4.win.outer[0], start - 3 - 30 - 1 - 20), EXPECT(b4.win.outer[1], 5);
    zoomed = 1;
    win_redo_caption_buttons(&W);
    EXPECT(b8.picture, 22);
    W.btn_e4 = W.btn_e8 = W.btn_ec = NULL;
    win_redo_caption_buttons(&W);

    // Sizing a window with a frame to a client size, and one without; its root hears of it.
    init(&W, WIN_F2_FRAME), init(&R, 0);
    W.outer[0] = 10, W.outer[1] = 20, W.outer[2] = 50, W.outer[3] = 60;
    nvlog = 0;
    EXPECT(win_resize(&W, 100, 80, 0), 0);
    EXPECT(W.outer[2], 110), EXPECT(W.outer[3], 100), EXPECT(W.inner[0], 0), EXPECT(W.inner[1], 0);
    EXPECT(W.inner[2], 100), EXPECT(W.inner[3], 80), EXPECT(cur_ == &W, 1);
    EXPECT(nvlog, 3);
    expect_call(0, 0x108, &W, 0, 0, 0), expect_call(1, 0x34, &W, 100, 80, 0), expect_call(2, 0x30, &W, 0, 0, 0);
    W.flags = WIN_F_THICK, W.border = 2, W.bottom = -1, W.root = &R;
    nvlog = 0;
    win_resize(&W, 100, 80, 1);
    EXPECT(W.outer[2], 10 + 104), EXPECT(W.outer[3], 20 + 84);
    EXPECT(W.inner[0], 2), EXPECT(W.inner[1], 2), EXPECT(W.inner[2], 102), EXPECT(W.inner[3], 82);
    EXPECT(nvlog, 4);
    expect_call(0, 0x108, &W, 0, 0, 0), expect_call(1, 0xc, &R, 104, 84, 0), expect_call(2, 0x34, &W, 100, 80, 0);
    expect_call(3, 0x30, &W, 0, 0, 0);
    init(&W, 0);
    W.root = &R, W.inner[0] = 3, W.inner[1] = 4;
    nvlog = 0;
    win_resize(&W, 40, 30, 1);
    EXPECT(W.inner[2], 43), EXPECT(W.inner[3], 34), EXPECT(W.outer[2], 0);
    expect_call(1, 0xc, &R, 40, 30, 0), expect_call(2, 0x34, &W, 40, 30, 0);

    // The resize event: the scroll bars to the edges, the callback, the slot and the buttons.
    init(&W, 0);
    W.inner[2] = 100, W.inner[3] = 60;
    W.scroll_v = &bsv, W.scroll_h = &bsh;
    memset(&bsv, 0, sizeof bsv), memset(&bsh, 0, sizeof bsh);
    bsv.win.vtable = bsh.win.vtable = vtable;
    bsv.width = 12, bsv.height = 3, bsh.width = 4, bsh.height = 14;
    W.resize_cb = (void *)stub_cb;
    nvlog = 0;
    win_resize_event(&W, 100, 60);
    EXPECT(bsv.win.inner[0], 100), EXPECT(bsv.win.inner[1], 0), EXPECT(bsh.win.inner[0], 0), EXPECT(bsh.win.inner[1], 60);
    EXPECT(nvlog, 4);
    expect_call(0, 0xc, &bsv, 12, 14 - 0 + 60, 0), expect_call(1, 0xc, &bsh, 100 - 12, -14, 0);
    expect_call(2, 0x400, NULL, 100, 60, 0), expect_call(3, 0x34, &W, 100, 60, 0);
    bsv.win.inner[0] = 0, W.scroll_h = NULL;
    nvlog = 0;
    win_resize_event(&W, 1, 2);
    EXPECT(bsv.win.inner[0], 100), EXPECT(bsv.win.inner[1], 0);
    expect_call(0, 0xc, &bsv, 12, 60, 0);
    W.scroll_v = NULL, W.scroll_h = &bsh;
    nvlog = 0;
    win_resize_event(&W, 1, 2);
    expect_call(0, 0xc, &bsh, 100, -14, 0);
    W.flags = 0x40, W.resize_cb = NULL;
    nvlog = 0;
    win_resize_event(&W, 7, 8);
    EXPECT(nvlog, 1);
    expect_call(0, 0x34, &W, 7, 8, 0);

    // The nonclient buffers: asked to rebuild, and the one for the number given.
    init(&W, 0);
    W.nc_buf_b4 = (void *)0xb4, W.nc_buf_b8 = (void *)0xb8, W.nc_buf_bc = (void *)0xbc, W.nc_buf_c0 = (void *)0xc0;
    nvlog = 0;
    EXPECT(win_redraw_nc_buffer(&W, 0), 0xb4), EXPECT(win_redraw_nc_buffer(&W, 1), 0xb8);
    EXPECT(win_redraw_nc_buffer(&W, 2), 0xc0), EXPECT(win_redraw_nc_buffer(&W, 3), 0xbc);
    EXPECT(win_redraw_nc_buffer(&W, 4), 0), EXPECT(win_redraw_nc_buffer(&W, 0xffffffffu), 0);
    EXPECT(nvlog, 6);
    expect_call(2, 0x10c, &W, 0, 2, 0);
    nvlog = 0;
    EXPECT(win_update_nc_buffer(&W, -1), 0x128 + 1);
    expect_call(0, 0x128, &W, 0, -1, 0);

    if (failures) return 1;
    printf("engine_win: ok\n");
    return 0;
}
