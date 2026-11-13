// Unit test for the Win class's geometry, tree and lists (win.c) on small hand-built trees of
// windows: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/bufobj.h"
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
STUB0(0x8) STUB3(0xc) STUB0(0x10) STUB0(0x30) STUB2(0x34) STUB1(0xcc) STUB0(0xf8) STUB0(0x108) STUB2(0x10c)

// ---- Stand-ins for what the drawing functions use: the Windows calls a Buffer's set-up makes
// (with a DIB each gets its own pixels), and the game's sprite scratch ----
static int next_handle = 100;
static uint8_t dib_pool[16][70 * 50];
static int dibs_made;
static void *STD t_create_dc(void *dc) { return (void *)(intptr_t)next_handle++; }
static void *STD t_create_dib(void *dc, void *info, int usage, void **bits, void *s, int o) {
    *bits = dib_pool[dibs_made++ % 16];
    return (void *)(intptr_t)next_handle++;
}
static int STD t_ok1(void *a) { return 1; }
static void *STD t_create_rgn(const int32_t *r) { return (void *)(intptr_t)next_handle++; }
static int STD t_select_clip_rgn(void *dc, void *rgn) { return 1; }
static void *STD t_select_object(void *dc, void *o) { return (void *)7; }
static int STD t_set_mode(void *dc, int m) { return 0; }
static int STD t_intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) return memset(out, 0, 16), 0;
    return memcpy(out, r, 16), 1;
}
static int STD t_equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static int STD t_set_rect(int32_t *r, int l, int t, int rt, int b) { return r[0] = l, r[1] = t, r[2] = rt, r[3] = b, 1; }
static void t_free(void *p) { (void)p; }
static int STD t_color_table(void *dc, unsigned s, unsigned n, const void *c) { return n; }
static void WIN_THIS t_spot(void *s) { memset(s, 0, 12); }
static void WIN_THIS t_spot_init(void *s, int n) { ((int32_t *)s)[2] = n; }
static void *WIN_THIS t_vt(void *b) { return NULL; }
static void *t_vtable[4] = {(void *)t_vt, (void *)t_vt};

static uint8_t key_ = 7;
static struct game_sprite_scratch sprite_scratch_;
static struct game_sprite buffer_sprite_;
static int32_t sprite_scale_[4], sprite_min_[2];
static float sprite_ratio_;
static double sprite_max_ratio_ = 4.0;

static struct game_buffer back_, overlay_, dest_;
static uint8_t back_px[100 * 60], overlay_px[20 * 10], dest_px[100 * 60];
static void hand_buffer(struct game_buffer *b, uint8_t *bits, int w, int h, int fill) {
    memset(b, 0, sizeof *b);
    memset(bits, fill, (size_t)w * h);
    b->bits = bits, b->width = w, b->neg_height = -h, b->pitch = w;
    b->full[2] = w, b->full[3] = h, b->clip[2] = w, b->clip[3] = h;
}
static int hpx(const struct game_buffer *b, int x, int y) { return b->bits[(intptr_t)b->pitch * y + x]; }

static int f4_result;
static int32_t seen_rect[4];
static int seen_rects;
static int WIN_THIS stub_0xf4(struct game_win *w) { return f4_result; }
static int WIN_THIS stub_0xf0(struct game_win *w, int buf, int which) { return vrec(0xf0, w, buf ? 1 : 0, which, 0), 0xf1; }
static int WIN_THIS stub_0x124(struct game_win *w, int a) {
    vrec(0x124, w, a ? 1 : 0, 0, 0);
    if (a) memcpy(seen_rect, (void *)(uintptr_t)a, 16);
    seen_rects++;
    return 0x125;
}
static int WIN_THIS stub_0x128b(struct game_win *w, int a, int b) { return vrec(0x128, w, a ? 1 : 0, b, 0), 0x129; }
STUB0(0xfc)

// A window that has the picture a hit test may use after the Win part, like the game's own classes.
struct hit_win {
    struct game_win w;
    struct game_buffer hit;
};

static void stub_cb(int a, int b) { vrec(0x400, NULL, a, b, 0); }
static void fill_vtable(void) {
    vtable[0x8 / 4] = (void *)stub_0x8, vtable[0xc / 4] = (void *)stub_0xc, vtable[0x10 / 4] = (void *)stub_0x10;
    vtable[0x30 / 4] = (void *)stub_0x30, vtable[0x34 / 4] = (void *)stub_0x34, vtable[0xcc / 4] = (void *)stub_0xcc;
    vtable[0xf8 / 4] = (void *)stub_0xf8, vtable[0x108 / 4] = (void *)stub_0x108, vtable[0x10c / 4] = (void *)stub_0x10c;
    vtable[0x128 / 4] = (void *)stub_0x128b;
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


    // ---- Drawing, the nonclient area and the mouse's window ----
    static void *ddraw_;
    static struct palette pal_;
    static int32_t mouse_pos_[2], hit_nc_, back_off_[2], overlay_on_, overlay_pos_[2], overlay_size_[2];
    static struct game_buffer *back_alt_;
    static struct palette *pal_ptr = &pal_;
    game_create_compatible_dc = t_create_dc, game_create_dib_section = t_create_dib, game_delete_dc = t_ok1;
    game_delete_object = t_ok1, game_create_rect_rgn_indirect = t_create_rgn, game_select_clip_rgn = t_select_clip_rgn;
    game_select_object = t_select_object, game_set_bk_mode = (void *)t_set_mode, game_set_text_align = (void *)t_set_mode;
    game_intersect_rect = t_intersect, game_equal_rect = t_equal, game_set_rect = t_set_rect, game_set_dib_color_table = t_color_table;
    static void *palette_now_, *font_default_;
    static int32_t default_520 = 0, fill_gap_, copy_scratch_[4];
    game_ddraw = &ddraw_, game_palette_now = &palette_now_, game_font_default = (void **)&font_default_;
    game_buffer_vtable = t_vtable, game_buffer_520_default = &default_520, game_free = t_free;
    game_spot_ctor = t_spot, game_spot_dtor = t_spot, game_spot_init = t_spot_init;
    game_fill_gap = &fill_gap_, game_copy_rem = &copy_scratch_[0], game_mask_units = &copy_scratch_[1];
    game_mask_gap_a = &copy_scratch_[2], game_mask_gap_b = &copy_scratch_[3];
    game_default_key = &key_, game_palette_copy = &pal_ptr;
    game_sprite_scratch = &sprite_scratch_, game_sprite_scale = sprite_scale_, game_sprite_ratio = &sprite_ratio_;
    game_sprite_max_ratio = &sprite_max_ratio_, game_sprite_min_size = sprite_min_, game_buffer_sprite = &buffer_sprite_;
    sprite_scale_[0] = sprite_scale_[1] = 1;
    game_win_mouse_pos = mouse_pos_, game_win_hit_nc = &hit_nc_, game_win_back = &back_, game_win_overlay = &overlay_;
    game_win_back_alt = &back_alt_, game_win_back_off = back_off_, game_win_overlay_on = &overlay_on_;
    game_win_overlay_pos = overlay_pos_, game_win_overlay_size = overlay_size_;
    vtable[0xf0 / 4] = (void *)stub_0xf0, vtable[0xf4 / 4] = (void *)stub_0xf4, vtable[0xfc / 4] = (void *)stub_0xfc;
    vtable[0x124 / 4] = (void *)stub_0x124, vtable[0x128 / 4] = (void *)stub_0x128b;
    pal_.serial = 0;

    // The mouse's window. A top-level window without a frame is hit inside its inner rectangle,
    // and the point comes back in its client coordinates.
    {
        static struct hit_win H, F, M, MP;
        memset(&H, 0, sizeof H), memset(&F, 0, sizeof F), memset(&M, 0, sizeof M), memset(&MP, 0, sizeof MP);
        H.w.vtable = F.w.vtable = M.w.vtable = MP.w.vtable = vtable;
        H.w.root = &H.w, F.w.root = &F.w, M.w.root = &M.w, MP.w.root = &MP.w;
        H.w.flags2 = F.w.flags2 = M.w.flags2 = MP.w.flags2 = WIN_F2_VISIBLE;
        H.w.inner[0] = 10, H.w.inner[1] = 20, H.w.inner[2] = 110, H.w.inner[3] = 80;
        parents[0] = &H.w, parent_n = 1, modal = NULL, modal_prev = 0;
        press1_ = press2_ = capture_ = hover_ = NULL;
        int32_t x = 50, y = 40;
#define MOUSE(px_, py_) (x = (px_), y = (py_), win_get_mouse_window(&x, &y))
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        EXPECT(x, 40), EXPECT(y, 20), EXPECT(hit_nc_, 0), EXPECT(mouse_pos_[0], 50), EXPECT(mouse_pos_[1], 40);
        EXPECT(MOUSE(500, 500) == NULL, 1), EXPECT(x, 500), EXPECT(y, 500);
        EXPECT(MOUSE(10, 20) == &H.w, 1);  // the corner is in, the far edge is not
        EXPECT(MOUSE(110, 30) == NULL, 1);
        H.w.flags2 = 0;  // a window that is not shown is skipped
        EXPECT(MOUSE(50, 40) == NULL, 1);
        H.w.flags2 = WIN_F2_VISIBLE;
        // Its own picture decides, when it asks to: the key colour is see-through, and so is 0 for a window with 0xa0 set.
        f4_result = 1;
        H.w.flags = 0x1000000;
        hand_buffer(&H.hit, back_px, 100, 60, 3);
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        H.hit.bits[20 * 100 + 40] = key_;
        EXPECT(MOUSE(50, 40) == NULL, 1), EXPECT(x, 50), EXPECT(y, 40);
        H.hit.bits[20 * 100 + 40] = 0;
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        *(int32_t *)((uint8_t *)&H.w + 0xa0) = 1;
        EXPECT(MOUSE(50, 40) == NULL, 1);
        H.hit.bits[20 * 100 + 40] = 2;
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        H.w.flags = 0x1000100;  // taking every point
        H.hit.bits[20 * 100 + 40] = key_;
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        H.w.flags = 0, f4_result = 0;

        // A window with a frame: the frame is its own where it is not see-through (and hit_nc is
        // set); the client area is found inside.
        F.w.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
        F.w.flags = WIN_F_CAPTION | 0x10000000;
        F.w.caption_h = 10, F.w.border = 4, F.w.bottom = -1;
        F.w.outer[0] = 5, F.w.outer[1] = 6, F.w.outer[2] = 105, F.w.outer[3] = 86;
        F.w.inner[0] = 4, F.w.inner[1] = 14, F.w.inner[2] = 96, F.w.inner[3] = 76;
        static struct game_buffer nc[4];
        static uint8_t ncpx[4][100 * 40];
        for (int i = 0; i < 4; i++) hand_buffer(&nc[i], ncpx[i], 100, 40, 1);
        F.w.nc_buf_b4 = &nc[0], F.w.nc_buf_b8 = &nc[1], F.w.nc_buf_c0 = &nc[2], F.w.nc_buf_bc = &nc[3];
        parents[0] = &F.w;
        EXPECT(MOUSE(60, 40) == &F.w, 1);
        EXPECT(x, 60 - 5 - 4), EXPECT(y, 40 - 6 - 14), EXPECT(hit_nc_, 0);
        EXPECT(MOUSE(7, 40) == &F.w, 1), EXPECT(hit_nc_, 1), EXPECT(x, 2), EXPECT(y, 34);  // the frame, drawn by the window
        f4_result = 1;
        EXPECT(MOUSE(7, 40) == &F.w, 1), EXPECT(hit_nc_, 1);  // the left strip's pixel is not see-through
        nc[3].bits[24 * 100 + 2] = key_;                      // (2, 34 - caption)
        EXPECT(MOUSE(7, 40) == NULL, 1), EXPECT(x, 7), EXPECT(y, 40);
        EXPECT(MOUSE(60, 12) == &F.w, 1), EXPECT(hit_nc_, 1);  // the caption strip, at (55, 6)
        nc[0].bits[6 * 100 + 55] = key_;
        EXPECT(MOUSE(60, 12) == NULL, 1);
        EXPECT(MOUSE(104, 40) == &F.w, 1);  // the right strip, at (99 - 100 + 4, 34 - 10)
        nc[1].bits[24 * 100 + 3] = key_;
        EXPECT(MOUSE(104, 40) == NULL, 1);
        EXPECT(MOUSE(60, 83) == &F.w, 1);  // the bottom strip, at (55, 77 - 80 + 4)
        nc[2].bits[1 * 100 + 55] = key_;
        EXPECT(MOUSE(60, 83) == NULL, 1);
        f4_result = 0;

        // The mouse pressed, captured or hovered over decides first, wherever it is.
        press1_ = &H.w;
        EXPECT(MOUSE(50, 40) == &H.w, 1), EXPECT(x, 40), EXPECT(y, 20), EXPECT(hit_nc_, 0);
        H.w.flags2 = 0;
        EXPECT(MOUSE(60, 40) == &F.w, 1), EXPECT(press1_ == NULL, 1);  // forgotten, and the next tried
        H.w.flags2 = WIN_F2_VISIBLE;
        capture_ = &H.w;
        EXPECT(MOUSE(500, 500) == &H.w, 1), EXPECT(x, 490), EXPECT(y, 480);
        capture_ = NULL;
        hover_ = &H.w;
        int32_t ex = 50, ey = 40;
        win_screen_to_client_2(&H.w, &ex, &ey);
        EXPECT(MOUSE(50, 40) == &H.w, 1), EXPECT(hit_nc_, 1), EXPECT(x, ex + 10), EXPECT(y, ey + 20);
        hover_ = NULL, press2_ = &H.w;
        EXPECT(MOUSE(50, 40) == &H.w, 1), EXPECT(hit_nc_, 1), EXPECT(x, ex + 10);
        press2_ = &M.w;
        M.w.flags2 = 0;
        EXPECT(MOUSE(60, 40) == &F.w, 1), EXPECT(press2_ == NULL, 1);
        M.w.flags2 = WIN_F2_VISIBLE;

        // A modal window takes the point first, then the window before it; with neither, the modal one.
        M.w.inner[0] = 200, M.w.inner[1] = 0, M.w.inner[2] = 260, M.w.inner[3] = 50;
        MP.w.inner[0] = 300, MP.w.inner[1] = 0, MP.w.inner[2] = 360, MP.w.inner[3] = 50;
        modal = &M.w, modal_prev = (int32_t)(uintptr_t)&MP.w;
        EXPECT(MOUSE(230, 10) == &M.w, 1), EXPECT(x, 30);
        EXPECT(MOUSE(330, 10) == &MP.w, 1), EXPECT(x, 30);
        EXPECT(MOUSE(30, 10) == &M.w, 1), EXPECT(x, 30 - 200);
        modal_prev = 0;
        EXPECT(MOUSE(330, 10) == &M.w, 1);
        modal = NULL;
        parents[0] = &H.w, parents[1] = &F.w, parent_n = 2;  // the first of the top-level windows that has the point
        EXPECT(MOUSE(60, 40) == &H.w, 1);
        EXPECT(MOUSE(7, 8) == &F.w, 1);
        // A child inside its parent is found before the parent's own area, in the parent's coordinates, and answers with its root.
        static struct game_win child;
        init(&child, WIN_F2_VISIBLE);
        child.flags = WIN_F_CHILD, child.parent = &H.w, child.root = &H.w;
        child.inner[0] = 5, child.inner[1] = 5, child.inner[2] = 30, child.inner[3] = 30;
        H.w.children[0] = &child, H.w.n_children = 1;
        H.w.flags = 0;
        EXPECT(MOUSE(50, 40) == &H.w, 1);
        H.w.n_children = 0;
    }

    // Drawing the screen: windows into the back buffer, the area cleared first.
    {
        static struct game_win Z0, Z1, Z2, Zc, Zn;
        init(&Z0, WIN_F2_VISIBLE), init(&Z1, WIN_F2_VISIBLE | WIN_F2_FRAME), init(&Z2, 0);
        init(&Zc, WIN_F2_VISIBLE | WIN_F2_FRAME), init(&Zn, WIN_F2_VISIBLE);
        Zc.parent = &Z0;
        zorder[0] = &Z0, zorder[1] = &Z1, zorder[2] = &Z2, zorder[3] = &Zc, z_n = 4;
        opaque = NULL, back_alt_ = NULL, overlay_on_ = 0;
        hand_buffer(&back_, back_px, 100, 60, 9);
        hand_buffer(&overlay_, overlay_px, 20, 10, 6);
        nvlog = 0;
        win_update_screen(NULL, NULL);
        EXPECT(hpx(&back_, 0, 0), 0), EXPECT(hpx(&back_, 99, 59), 0);
        // From the last of the list to the first: Zc (shown, with a frame), Z2 (not shown), Z1 (a frame), Z0.
        EXPECT(nvlog, 5);
        expect_call(0, 0x128, &Zc, 0, -1, 0), expect_call(1, 0x124, &Zc, 0, 0, 0), expect_call(2, 0x128, &Z1, 0, -1, 0);
        expect_call(3, 0x124, &Z1, 0, 0, 0), expect_call(4, 0x124, &Z0, 0, 0, 0);
        // An area only clears that area, and is given to every call.
        memset(back_px, 9, sizeof back_px);
        int32_t area[4] = {10, 5, 30, 15};
        nvlog = 0, seen_rects = 0;
        win_update_screen(area, NULL);
        EXPECT(hpx(&back_, 10, 5), 0), EXPECT(hpx(&back_, 29, 14), 0), EXPECT(hpx(&back_, 30, 5), 9), EXPECT(hpx(&back_, 9, 5), 9);
        EXPECT(seen_rect[0], 10), EXPECT(seen_rect[3], 15), EXPECT(seen_rects, 3);
        // Another Buffer taking the draw leaves the back buffer as it was.
        memset(back_px, 9, sizeof back_px);
        back_alt_ = &dest_;
        win_update_screen(NULL, NULL);
        EXPECT(hpx(&back_, 0, 0), 9);
        back_alt_ = NULL;
        // From a window (Z1) that hides all under it, and those over it; a window that isn't a root isn't
        // drawn by its root.
        opaque = &Z1;
        nvlog = 0;
        win_update_screen(NULL, &Z1);
        EXPECT(nvlog, 3), expect_call(0, 0x128, &Z1, 0, -1, 0), expect_call(1, 0x124, &Z1, 0, 0, 0), expect_call(2, 0x124, &Z0, 0, 0, 0);
        Z1.root = &Z0;
        nvlog = 0;
        win_update_screen(NULL, &Z1);
        EXPECT(nvlog, 1), expect_call(0, 0x128, &Z1, 0, -1, 0);
        Z1.root = &Z1;
        nvlog = 0;
        win_update_screen(NULL, &Zc);
        EXPECT(nvlog, 5);
        nvlog = 0;
        win_update_screen(NULL, &Zn);  // not in the list at all: nothing
        EXPECT(nvlog, 0);
        opaque = NULL;
        win_update_screen(NULL, &Z1);  // a window is only asked for when something hides all those under it
        EXPECT(nvlog, 5);
        nvlog = 0, z_n = 0;
        win_update_screen(NULL, NULL);
        EXPECT(nvlog, 0);
        z_n = 4;
        // What is under the overlay is copied into it, cut to the area (the back buffer is left alone with
        // another Buffer taking the draw, so the pixels are there to copy).
        overlay_on_ = 1, overlay_pos_[0] = 5, overlay_pos_[1] = 3, overlay_size_[0] = 8, overlay_size_[1] = 4;
        back_alt_ = &dest_;
        for (int y = 0; y < 60; y++)
            for (int x = 0; x < 100; x++) back_px[y * 100 + x] = (uint8_t)(x + y);
        win_update_screen(NULL, NULL);
        EXPECT(hpx(&overlay_, 0, 0), 5 + 3), EXPECT(hpx(&overlay_, 7, 3), 12 + 6), EXPECT(hpx(&overlay_, 8, 0), 6), EXPECT(hpx(&overlay_, 0, 4), 6);
        memset(overlay_px, 6, sizeof overlay_px);
        int32_t cut[4] = {8, 0, 40, 4};
        win_update_screen(cut, NULL);  // only (8..12, 3)
        EXPECT(hpx(&overlay_, 3, 0), 8 + 3), EXPECT(hpx(&overlay_, 2, 0), 6), EXPECT(hpx(&overlay_, 3, 1), 6);
        memset(overlay_px, 6, sizeof overlay_px);
        int32_t away[4] = {50, 50, 60, 60};
        win_update_screen(away, NULL);
        EXPECT(hpx(&overlay_, 0, 0), 6);
        back_alt_ = NULL;
        overlay_on_ = 0;

        // Drawing a window's area: the rectangle (in its client coordinates) taken to the screen first.
        init(&Z0, WIN_F2_VISIBLE);
        Z0.outer[0] = 7, Z0.outer[1] = 9, Z0.inner[0] = 3, Z0.inner[1] = 1;
        int32_t r[4] = {1, 2, 11, 12};
        win_update_window(&Z0, r);  // the last window drawn in the list is Z0
        EXPECT(seen_rect[0], 1 + 10), EXPECT(seen_rect[1], 2 + 10), EXPECT(seen_rect[2], 11 + 10), EXPECT(seen_rect[3], 12 + 10);
        init(&Z2, WIN_F2_VISIBLE);
        Z2.flags = WIN_F_CHILD, Z2.parent = &Z0, Z2.outer[0] = 2, Z2.outer[1] = 4, Z2.inner[0] = 1, Z2.inner[1] = 1;
        int32_t cx = 3, cy = 5;
        win_client_to_screen_2(&Z0, &cx, &cy);
        win_update_window(&Z2, r);
        EXPECT(seen_rect[0], 1 + cx), EXPECT(seen_rect[1], 2 + cy), EXPECT(seen_rect[2], 11 + cx), EXPECT(seen_rect[3], 12 + cy);
        Z2.flags |= WIN_F_PARENT_INNER;
        win_update_window(&Z2, r);
        EXPECT(seen_rect[0], 1 + cx - 3), EXPECT(seen_rect[1], 2 + cy - 1);
        seen_rects = 0;
        win_update_window(&Z0, NULL);  // no rectangle: the whole screen
        EXPECT(seen_rects, 4);
        init(&Z1, WIN_F2_VISIBLE | WIN_F2_FRAME);
        zorder[0] = &Z0, zorder[1] = &Z1, zorder[2] = &Z2, z_n = 3;

        // Every window told to redraw, or to rebuild its frame and flush those that draw one.
        nvlog = 0;
        win_redraw();
        EXPECT(nvlog, 3), expect_call(0, 0xf8, &Z0, 0, 0, 0), expect_call(2, 0xf8, &Z2, 0, 0, 0);
        nvlog = 0, f4_result = 0;
        win_update();
        EXPECT(nvlog, 3), expect_call(0, 0x128, &Z0, 0, -1, 0), expect_call(2, 0x128, &Z2, 0, -1, 0);
        nvlog = 0, f4_result = 1;
        win_update();
        EXPECT(nvlog, 6), expect_call(1, 0xfc, &Z0, 0, 0, 0), expect_call(2, 0x128, &Z1, 0, -1, 0);
        f4_result = 0;
        z_n = 0;
    }

    // The frame. Four strips are made the sizes the frame needs, cleared to the see-through colour,
    // and the window drawn into them; the lines come in the window's colours.
    {
        static struct game_win FW;
        static struct game_buffer s[4], tile;
        init(&FW, WIN_F2_VISIBLE | WIN_F2_FRAME);
        FW.flags = WIN_F_CAPTION;
        FW.caption_h = 10, FW.border = 3, FW.bottom = 5, FW.cap_btn = 1;
        FW.outer[0] = 10, FW.outer[1] = 20, FW.outer[2] = 50, FW.outer[3] = 60;
        FW.inner[0] = 3, FW.inner[1] = 10, FW.inner[2] = 37, FW.inner[3] = 30;
        FW.nc_color = 40, FW.box_color[0] = 41, FW.box_color[1] = 42;
        for (int i = 0; i < 4; i++) buffer_ctor(&s[i]);
        FW.nc_buf_b4 = &s[0], FW.nc_buf_c0 = &s[1], FW.nc_buf_bc = &s[2], FW.nc_buf_b8 = &s[3];
        nvlog = 0;
        win_set_rects(&FW);
        EXPECT(s[0].width, 40), EXPECT(s[0].neg_height, -10), EXPECT(s[1].width, 40), EXPECT(s[1].neg_height, -5);
        EXPECT(s[2].width, 3), EXPECT(s[2].neg_height, -(40 - 5 - 10)), EXPECT(s[3].width, 3), EXPECT(s[3].neg_height, -25);
        EXPECT(hpx(&s[0], 5, 5), key_), EXPECT(hpx(&s[2], 1, 20), key_);
        EXPECT(nvlog, 1), expect_call(0, 0x10c, &FW, 0, -1, 0);
        FW.flags2 = WIN_F2_VISIBLE;  // no frame: nothing
        nvlog = 0;
        win_set_rects(&FW);
        EXPECT(nvlog, 0);
        FW.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
        FW.flags = WIN_F_CAPTION | 0x400000;  // a thin caption: the top strip is a border thick
        win_set_rects(&FW);
        EXPECT(s[0].neg_height, -3);
        EXPECT(s[2].neg_height, -(40 - 5 - 3));
        FW.flags = WIN_F_CAPTION;
        win_set_rects(&FW);

        // Redrawing all of them, with a plain colour: filled, framed by two sets of lines, and told.
        nvlog = 0;
        win_on_redraw_nc(&FW, NULL, -1);
        EXPECT(nvlog, 4), expect_call(0, 0xf0, &FW, 1, 0, 0), expect_call(1, 0xf0, &FW, 1, 2, 0);
        expect_call(2, 0xf0, &FW, 1, 3, 0), expect_call(3, 0xf0, &FW, 1, 1, 0);
        // The caption strip: the outline of the whole window (top and left in the first colour, bottom
        // and right in the second) and, inside, a rectangle from the border and the caption in the
        // colours the other way round.
        EXPECT(hpx(&s[0], 0, 0), 41), EXPECT(hpx(&s[0], 39, 0), 41), EXPECT(hpx(&s[0], 0, 9), 41), EXPECT(hpx(&s[0], 20, 5), 40);
        EXPECT(hpx(&s[0], 10, 9), 42), EXPECT(hpx(&s[0], 2, 9), 42), EXPECT(hpx(&s[0], 1, 9), 40);
        // The bottom strip is the same outline seen from its bottom: its bottom edge is the last row.
        EXPECT(hpx(&s[1], 10, 4), 42), EXPECT(hpx(&s[1], 0, 3), 41), EXPECT(hpx(&s[1], 20, 2), 40);
        // The left and right strips: the outline's sides, and the inner rectangle's.
        EXPECT(hpx(&s[2], 0, 10), 41), EXPECT(hpx(&s[2], 2, 10), 42), EXPECT(hpx(&s[3], 2, 10), 42), EXPECT(hpx(&s[3], 0, 10), 41);
        // One strip by itself: nothing else changes.
        for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
        nvlog = 0;
        win_on_redraw_nc(&FW, NULL, 2);
        EXPECT(nvlog, 1), expect_call(0, 0xf0, &FW, 1, 2, 0);
        EXPECT(hpx(&s[0], 20, 5), 99), EXPECT(hpx(&s[1], 20, 2), 40);
        // The tile picture, from the place in it each strip starts at.
        buffer_ctor(&tile);
        buffer_init(&tile, 40, 40, 0, NULL);
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 40; x++) tile.bits[y * tile.pitch + x] = (uint8_t)(x + 10 * y);
        FW.nc_tile = &tile, FW.cap_btn = 0;
        win_on_redraw_nc(&FW, NULL, 2);
        EXPECT(hpx(&s[1], 0, 0), tile.bits[35 * tile.pitch + 0]), EXPECT(hpx(&s[1], 7, 3), tile.bits[(35 + 3) * tile.pitch + 7]);
        win_on_redraw_nc(&FW, NULL, 1);
        EXPECT(hpx(&s[3], 1, 4), tile.bits[(10 + 4) * tile.pitch + (40 - 3 + 1)]);
        // Only the part inside a rectangle (screen coordinates) is redrawn, whatever strip is asked for.
        for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
        int32_t part[4] = {10 + 5, 20 + 2, 10 + 9, 20 + 4};
        FW.nc_tile = NULL;
        nvlog = 0;
        win_on_redraw_nc(&FW, part, 3);
        EXPECT(nvlog, 4);
        EXPECT(hpx(&s[0], 5, 2), 40), EXPECT(hpx(&s[0], 8, 3), 40), EXPECT(hpx(&s[0], 9, 2), 99), EXPECT(hpx(&s[0], 4, 2), 99);
        EXPECT(hpx(&s[0], 5, 4), 99);
        // A window needs a frame and a caption (or bit 0) to have one drawn.
        FW.flags = 0;
        nvlog = 0;
        win_on_redraw_nc(&FW, NULL, -1);
        EXPECT(nvlog, 0);
        FW.flags = WIN_F_CAPTION;

        // The strips copied into another Buffer, each where it lies on the screen.
        hand_buffer(&dest_, dest_px, 100, 60, 9);
        for (int i = 0; i < 4; i++) memset(s[i].bits, 50 + i, (size_t)s[i].pitch * -s[i].neg_height);
        win_draw_nc_to(&FW, &dest_, 0, 0);
        EXPECT(hpx(&dest_, 10, 20), 50), EXPECT(hpx(&dest_, 49, 29), 50), EXPECT(hpx(&dest_, 9, 20), 9), EXPECT(hpx(&dest_, 10, 19), 9);
        EXPECT(hpx(&dest_, 10, 30), 52), EXPECT(hpx(&dest_, 12, 54), 52), EXPECT(hpx(&dest_, 13, 40), 9);  // under the caption
        EXPECT(hpx(&dest_, 10, 55), 51), EXPECT(hpx(&dest_, 49, 59), 51);                               // ending at the window's bottom
        EXPECT(hpx(&dest_, 47, 30), 53), EXPECT(hpx(&dest_, 49, 54), 53), EXPECT(hpx(&dest_, 46, 40), 9);  // ending at its right
        // A destination at another place is cut: its own corner, and its size.
        hand_buffer(&dest_, dest_px, 100, 60, 9);
        win_draw_nc_to(&FW, &dest_, 20, 30);
        EXPECT(hpx(&dest_, 0, 25), 51), EXPECT(hpx(&dest_, 29, 29), 51), EXPECT(hpx(&dest_, 30, 25), 9), EXPECT(hpx(&dest_, 0, 0), 9);
        EXPECT(hpx(&dest_, 27 - 20 + 20, 5), 53);  // the right strip (47..49) reaches x = 27..29 of it
        win_draw_nc_to(&FW, NULL, 0, 0);

        // The strips drawn on the screen: into another Buffer, at the window's place less the Buffer's.
        hand_buffer(&back_, back_px, 100, 60, 9);
        hand_buffer(&dest_, dest_px, 100, 60, 9);
        s[0].bits[0] = key_;  // see-through
        back_alt_ = &dest_;
        back_off_[0] = 4, back_off_[1] = 6;
        int32_t wx = 0, wy = 0;
        win_client_to_screen_2(&FW, &wx, &wy);
        wx -= FW.inner[0], wy -= FW.inner[1];
        win_on_nc_paint(&FW, NULL, -1);
        int ox = wx - 4, oy = wy - 6;
        EXPECT(hpx(&dest_, ox, oy), 9), EXPECT(hpx(&dest_, ox + 1, oy), 50), EXPECT(hpx(&dest_, ox + 39, oy + 9), 50);
        EXPECT(hpx(&dest_, ox, oy + 35), 51), EXPECT(hpx(&dest_, ox + 39, oy + 39), 51);
        EXPECT(hpx(&dest_, ox, oy + 10), 52), EXPECT(hpx(&dest_, ox + 37, oy + 10), 53), EXPECT(hpx(&dest_, ox + 40, oy + 10), 9);
        EXPECT(hpx(&back_, 0, 0), 9);
        // One strip, and a rectangle that clips the back buffer and is put back.
        hand_buffer(&dest_, dest_px, 100, 60, 9);
        win_on_nc_paint(&FW, NULL, 3);
        EXPECT(hpx(&dest_, ox, oy + 10), 52), EXPECT(hpx(&dest_, ox + 1, oy), 9), EXPECT(hpx(&dest_, ox, oy + 35), 9);
        int32_t clip_rect[4] = {5, 5, 30, 30};
        win_on_nc_paint(&FW, clip_rect, 3);
        EXPECT(hpx(&dest_, ox + 1, oy), 50);  // a rectangle means every strip
        EXPECT(back_.clip[0], 0), EXPECT(back_.clip[2], 100), EXPECT(back_.clip[3], 60);
        // Not shown, no caption: nothing.
        hand_buffer(&dest_, dest_px, 100, 60, 9);
        FW.flags2 = WIN_F2_FRAME;
        win_on_nc_paint(&FW, NULL, -1);
        FW.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME, FW.flags = 0;
        win_on_nc_paint(&FW, NULL, -1);
        EXPECT(hpx(&dest_, ox + 1, oy), 9);
        back_alt_ = NULL;
    }

    if (failures) return 1;
    printf("engine_win: ok\n");
    return 0;
}
