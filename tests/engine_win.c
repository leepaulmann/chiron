// Unit test for the Win class's geometry, tree and lists (win.c) on small hand-built trees of
// windows: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/bufobj.h"
#include "../src/engine/game.h"
#include "life_test.h"
#include "../src/engine/win.h"

static int failures, line_;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line_, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) (line_ = __LINE__, check_(#expr, (long)(expr), (long)(want)))

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
static int wr_calls;
static int STD intersect_rect_(int32_t *d, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[2] <= r[0] || r[3] <= r[1]) return memset(d, 0, 16), 0;
    return memcpy(d, r, 16), 1;
}
static int STD get_window_rect_(uint32_t h, int32_t *r) {
    r[0] = 90, r[1] = 90, r[2] = 130, r[3] = 130;
    return wr_calls++, 1;
}
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
static int WIN_THIS stub_0x164(struct game_win *w, int title, int buf) { return vrec(0x164, w, title ? 1 : 0, buf ? 1 : 0, 0), 0x165; }
static void gw_title_cb(struct game_win *w, const char *t, struct game_buffer *b) { vrec(0x2030, w, t ? t[0] : 0, b ? 1 : 0, 0); }

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
static void expect_call_(int i, int slot, void *w, int a, int b, int c) {
    char what[64];
    snprintf(what, sizeof what, "call %d slot", i);
    check_(what, i < nvlog ? vlog_[i].slot : -1, slot);
    if (i >= nvlog) return;
    snprintf(what, sizeof what, "call %d this", i), check_(what, (long)vlog_[i].w, (long)w);
    snprintf(what, sizeof what, "call %d a", i), check_(what, vlog_[i].a, a);
    snprintf(what, sizeof what, "call %d b", i), check_(what, vlog_[i].b, b);
    snprintf(what, sizeof what, "call %d c", i), check_(what, vlog_[i].c, c);
}

#define expect_call(...) (line_ = __LINE__, expect_call_(__VA_ARGS__))
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

// ---- Mouse and key events (win.c): stand-ins for the slots, callbacks, hooks and Windows calls ----
#define ES1(slot) \
    static int WIN_THIS es_##slot(struct game_win *w, int a) { return vrec(slot, w, a, 0, 0), 0; }
#define ES2(slot) \
    static int WIN_THIS es_##slot(struct game_win *w, int a, int b) { return vrec(slot, w, a, b, 0), 0; }
#define ES4(slot) \
    static int WIN_THIS es_##slot(struct game_win *w, int a, int b, int c, int d) { return vrec(slot, w, a, b, c * 3 + d), 0; }
#define ES5(slot)                                                                                   \
    static int WIN_THIS es_##slot(struct game_win *w, int a, int b, int c, int d, int e) {         \
        return vrec(slot, w, a, b, c * 7 + d * 3 + e), 0;                                          \
    }
ES1(0x3c) ES2(0x44) ES2(0x4c) ES2(0x50) ES2(0x54) ES2(0x58) ES2(0x5c) ES2(0x60) ES2(0x70) ES2(0x74) ES1(0x78) ES2(0x7c) ES2(0x80)
ES2(0x84) ES2(0x88) ES2(0x8c) ES2(0x90) ES2(0x94) ES2(0x98) ES2(0x9c) ES1(0xb0) ES1(0x104) ES4(0x138) ES2(0x13c) ES4(0x140)
ES5(0x144) ES4(0x148) ES5(0x14c) ES4(0x150)
static int key_ret, scroll_ret, nc_code, spot_ret, spot_field, region_ret, own_regions;
static int WIN_THIS es_0x100(struct game_win *w) { return vrec(0x100, w, 0, 0, 0), 0; }
static int WIN_THIS es_0x64(struct game_win *w, int a, int b) { return vrec(0x64, w, a, b, 0), key_ret; }
static int WIN_THIS es_0x68(struct game_win *w, int a) { return vrec(0x68, w, a, 0, 0), key_ret; }
static int WIN_THIS es_0x6c(struct game_win *w, int a) { return vrec(0x6c, w, a, 0, 0), key_ret; }
static int WIN_THIS es_0x12c(struct game_win *w, int x, int y) { return vrec(0x12c, w, x, y, 0), nc_code; }
static int WIN_THIS es_0xf4(struct game_win *w) { return own_regions; }
static int WIN_THIS es_0x160(struct game_win *w, int a, int b) { return vrec(0x160, w, a, b, 0), region_ret; }
static int WIN_THIS sc_0x64(struct game_win *w, int a, int b) { return vrec(0x64, w, a, b, 0), scroll_ret; }
static struct link_ {
    void **vtable;
    int keep;
} L1, L2;
static void *link_vt[0x80 / 4], *ev_vtable[0x180 / 4], *sc_vtable[0x180 / 4];
static int WIN_THIS link_keep(struct link_ *l) { return l->keep; }
static int WIN_THIS link_free(struct link_ *l) { return vrec(0x1c, l, 0, 0, 0), 0; }
#define ECB(n) \
    static void ecb_##n(int a, int b) { vrec(0x2000 + n, NULL, a, b, 0); }
ECB(0) ECB(1) ECB(2) ECB(3) ECB(4) ECB(5) ECB(6) ECB(7) ECB(8)
static int ecb_key_click(int a, int b) { return vrec(0x2010, NULL, a, b, 0), 100; }
static int ecb_key_down(int a) { return vrec(0x2011, NULL, a, 0, 0), 200; }
static int ecb_key_up(int a) { return vrec(0x2012, NULL, a, 0, 0), 300; }
static void ehook_after(void) { vrec(0x2020, NULL, 0, 0, 0); }
static void ehook_key(int a) { vrec(0x2021, NULL, a, 0, 0); }
static void ehook_left(int x, int y) { vrec(0x2022, NULL, x, y, 0); }
static void ehook_right(int x, int y) { vrec(0x2023, NULL, x, y, 0); }
static void(*const ecb_table[9])(int, int) = {ecb_0, ecb_1, ecb_2, ecb_3, ecb_4, ecb_5, ecb_6, ecb_7, ecb_8};
static void *ehooks[4];
static int mvk_calls;
static unsigned STD mvk_(unsigned code, unsigned type) {
    mvk_calls++;
    return type == 2 && ((code >= 0x41 && code <= 0x5a) || (code >= 0x30 && code <= 0x39) || code == 0x20) ? code : 0;
}
static struct {
    uint32_t hwnd;
    unsigned msg;
    uintptr_t wp;
    intptr_t lp;
} posts[4], defs[4];
static int nposts, ndefs;
static int STD post_(void *wnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    if (nposts < 4) posts[nposts].hwnd = (uint32_t)(uintptr_t)wnd, posts[nposts].msg = msg, posts[nposts].wp = wp, posts[nposts].lp = lp;
    return nposts++, 1;
}
static intptr_t STD defproc_(uint32_t hwnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    if (ndefs < 4) defs[ndefs].hwnd = hwnd, defs[ndefs].msg = msg, defs[ndefs].wp = wp, defs[ndefs].lp = lp;
    return ndefs++, 0x1234;
}
static int WIN_THIS spot_(void *list, int x, int y, int *f14, int *f10) {
    vrec(0x3000, list, x, y, 0);
    if (spot_ret >= 0 && f14) *f14 = spot_field;
    return spot_ret;
}
static void put(struct game_win *w, int off, void *v) { *(void **)((uint8_t *)w + off) = v; }
static void ev_window(struct game_win *w) {
    memset(w, 0, sizeof *w);
    w->vtable = ev_vtable;
    w->root = w;
    w->flags2 = WIN_F2_VISIBLE;
    w->inner[2] = 50, w->inner[3] = 40;
    for (int i = 0; i < 9; i++) put(w, 0x408 + 4 * i, (void *)ecb_table[i]);
}

static int32_t t_mouse_pos[2], t_hit_nc;
static void test_events(void) {
    game_win_mouse_pos = t_mouse_pos, game_win_hit_nc = &t_hit_nc;
    static struct game_win E, D, X, P;
    static struct game_win_scroll S;
    static struct {
        struct game_win w;
        uint8_t sub[0x600];
    } F;
    static struct game_win_node node;
    int32_t track_pos[2] = {0, 0}, track_code = 0, press_inside = 7;
    struct game_win *track = NULL;
    // The slots the events call (the ones a window may leave to its callbacks come from `fill`).
    ev_vtable[0x3c / 4] = (void *)es_0x3c, ev_vtable[0x44 / 4] = (void *)es_0x44, ev_vtable[0x4c / 4] = (void *)es_0x4c;
    ev_vtable[0x50 / 4] = (void *)es_0x50, ev_vtable[0x54 / 4] = (void *)es_0x54, ev_vtable[0x58 / 4] = (void *)es_0x58;
    ev_vtable[0x5c / 4] = (void *)es_0x5c, ev_vtable[0x60 / 4] = (void *)es_0x60, ev_vtable[0x64 / 4] = (void *)es_0x64;
    ev_vtable[0x68 / 4] = (void *)es_0x68, ev_vtable[0x6c / 4] = (void *)es_0x6c, ev_vtable[0x70 / 4] = (void *)es_0x70;
    ev_vtable[0x74 / 4] = (void *)es_0x74, ev_vtable[0x78 / 4] = (void *)es_0x78, ev_vtable[0x7c / 4] = (void *)es_0x7c;
    ev_vtable[0x80 / 4] = (void *)es_0x80, ev_vtable[0x84 / 4] = (void *)es_0x84, ev_vtable[0x88 / 4] = (void *)es_0x88;
    ev_vtable[0x8c / 4] = (void *)es_0x8c, ev_vtable[0x90 / 4] = (void *)es_0x90, ev_vtable[0x94 / 4] = (void *)es_0x94;
    ev_vtable[0x98 / 4] = (void *)es_0x98, ev_vtable[0x9c / 4] = (void *)es_0x9c, ev_vtable[0xb0 / 4] = (void *)es_0xb0;
    ev_vtable[0xf4 / 4] = (void *)es_0xf4, ev_vtable[0x100 / 4] = (void *)es_0x100, ev_vtable[0x104 / 4] = (void *)es_0x104;
    ev_vtable[0x12c / 4] = (void *)es_0x12c, ev_vtable[0x138 / 4] = (void *)es_0x138, ev_vtable[0x13c / 4] = (void *)es_0x13c;
    ev_vtable[0x140 / 4] = (void *)es_0x140, ev_vtable[0x144 / 4] = (void *)es_0x144, ev_vtable[0x148 / 4] = (void *)es_0x148;
    ev_vtable[0x14c / 4] = (void *)es_0x14c, ev_vtable[0x150 / 4] = (void *)es_0x150, ev_vtable[0x160 / 4] = (void *)es_0x160;
    sc_vtable[0x64 / 4] = (void *)sc_0x64;
    link_vt[0x5c / 4] = (void *)link_keep, link_vt[0x1c / 4] = (void *)link_free;
    L1.vtable = L2.vtable = link_vt;
    game_win_track = &track, game_win_track_code = &track_code, game_win_track_pos = track_pos, game_win_press_inside = &press_inside;
    game_win_hooks = ehooks;
    game_map_virtual_key = mvk_, game_post_message = post_, game_def_window_proc = defproc_, game_spot_check = spot_;
    game_win_hwnd = &hwnd_;
    hwnd_ = 0x55;
    ehooks[0] = (void *)ehook_after, ehooks[1] = (void *)ehook_key, ehooks[2] = (void *)ehook_left, ehooks[3] = (void *)ehook_right;
    key_ret = scroll_ret = nc_code = own_regions = region_ret = 0;
    spot_ret = -1;

    // The left button's down event: callback, the game's hook, handler, then the object is let go (unless it says to keep it).
    ev_window(&E);
    put(&E, 0x24, &L1), put(&E, 0x54, &L2);
    L1.keep = L2.keep = 0;
    cur_ = NULL, nvlog = 0;
    win_left_down_event(&E, 3, 4, 0);
    EXPECT(cur_ == &E, 1), EXPECT(nvlog, 4);
    expect_call(0, 0x2004, NULL, 3, 4, 0), expect_call(1, 0x2022, NULL, 3, 4, 0), expect_call(2, 0x50, &E, 3, 4, 0);
    expect_call(3, 0x1c, &L1, 0, 0, 0);
    L1.keep = 1, cur_ = NULL, nvlog = 0;
    win_left_down_event(&E, 3, 4, 0);
    EXPECT(nvlog, 3);
    nvlog = 0, cur_ = NULL;  // the frame: no callback, the other slot and object, the hook still
    win_left_down_event(&E, 5, 6, 1);
    EXPECT(cur_ == NULL, 1), EXPECT(nvlog, 3);
    expect_call(0, 0x2022, NULL, 5, 6, 0), expect_call(1, 0x84, &E, 5, 6, 0), expect_call(2, 0x1c, &L2, 0, 0, 0);
    ehooks[2] = NULL, nvlog = 0;
    win_left_down_event(&E, 1, 2, 0);
    EXPECT(nvlog, 2);
    ehooks[2] = (void *)ehook_left;
    E.flags = WIN_F_DISABLED, nvlog = 0;  // disabled or busy windows are given nothing
    win_left_down_event(&E, 1, 2, 0), win_left_up_event(&E, 1, 2, 0), win_right_click_event(&E, 1, 2, 1);
    win_right_down_event(&E, 1, 2, 1), win_right_up_event(&E, 1, 2, 0), win_on_mouse_move(&E, 1, 2, 0, 0);
    win_on_mouse_wheel(&E, 0, 120, 0, 0, 0);
    EXPECT(nvlog, 0);
    E.flags = 0, E.flags2 = WIN_F2_VISIBLE | WIN_F2_BUSY;
    win_left_down_event(&E, 1, 2, 0), win_left_up_event(&E, 1, 2, 0), win_right_down_event(&E, 1, 2, 1);
    EXPECT(win_key_click_event(&E, 0, 0xd) + win_key_down_event(&E, 3) + win_key_up_event(&E, 3), 0), EXPECT(nvlog, 0);
    E.flags2 = WIN_F2_VISIBLE;

    // The others: the callback after the one for the last, the slot, the object; each for the frame as well.
    L1.keep = 0;
    put(&E, 0x28, &L1), put(&E, 0x58, &L2), put(&E, 0x2c, &L1), put(&E, 0x5c, &L2), put(&E, 0x30, &L1), put(&E, 0x60, &L2);
    put(&E, 0x34, &L1), put(&E, 0x64, &L2), put(&E, 0x1c, &L1), put(&E, 0x4c, &L2);
    static const struct {
        void WIN_THIS (*fn)(struct game_win *, int, int, int);
        int cb, slot, slot_nc;
    } evs[] = {{win_left_up_event, 5, 0x54, 0x88}, {win_right_click_event, 6, 0x58, 0x8c}, {win_right_down_event, 7, 0x5c, 0x90},
               {win_right_up_event, 8, 0x60, 0x94}};
    for (int i = 0; i < 4; i++) {
        cur_ = NULL, nvlog = 0;
        evs[i].fn(&E, 9, 8, 0);
        EXPECT(cur_ == &E, 1), EXPECT(nvlog, 3);
        expect_call(0, 0x2000 + evs[i].cb, NULL, 9, 8, 0), expect_call(1, evs[i].slot, &E, 9, 8, 0), expect_call(2, 0x1c, &L1, 0, 0, 0);
        cur_ = NULL, nvlog = 0;
        evs[i].fn(&E, 9, 8, 1);
        EXPECT(cur_ == NULL, 1), EXPECT(nvlog, 2);
        expect_call(0, evs[i].slot_nc, &E, 9, 8, 0), expect_call(1, 0x1c, &L2, 0, 0, 0);
    }
    cur_ = NULL, nvlog = 0;
    win_on_mouse_move(&E, 2, 3, 99, 0);
    EXPECT(nvlog, 3), expect_call(0, 0x2000, NULL, 2, 3, 0), expect_call(1, 0x44, &E, 2, 3, 0), expect_call(2, 0x1c, &L1, 0, 0, 0);
    nvlog = 0;
    win_on_mouse_move(&E, 2, 3, 99, 1);
    EXPECT(nvlog, 2), expect_call(0, 0x7c, &E, 2, 3, 0), expect_call(1, 0x1c, &L2, 0, 0, 0);

    // The wheel: whole notches towards zero from the low word, for the client area or the frame.
    nvlog = 0;
    win_on_mouse_wheel(&E, 0, 240, 0, 0, 0), win_on_mouse_wheel(&E, 0, -130, 0, 0, 1), win_on_mouse_wheel(&E, 0, -119, 0, 0, 0);
    win_on_mouse_wheel(&E, 0, 0x7fff0000 | 120, 0, 0, 1), win_on_mouse_wheel(&E, 0, 0xffff8000 + 0x10000, 0, 0, 0);
    EXPECT(nvlog, 5);
    expect_call(0, 0x3c, &E, 2, 0, 0), expect_call(1, 0x78, &E, -1, 0, 0), expect_call(2, 0x3c, &E, 0, 0, 0);
    expect_call(3, 0x78, &E, 1, 0, 0), expect_call(4, 0x3c, &E, -273, 0, 0);

    // Keys. The key as a click: return and the pad's enter, escape, others; the callback and handler answers are added.
    L1.keep = L2.keep = 1;
    nvlog = 0, key_ret = 5, L1.keep = 0;  // the object is freed
    put(&E, 0x42c, (void *)ecb_key_click), put(&E, 0x38, &L1);
    EXPECT(win_key_click_event(&E, 7, 0xd), 105), EXPECT(nvlog, 4);
    expect_call(0, 0xb0, &E, -1, 0, 0), expect_call(1, 0x2010, NULL, 7, 0xd, 0), expect_call(2, 0x64, &E, 7, 0xd, 0), expect_call(3, 0x1c, &L1, 0, 0, 0);
    L1.keep = 1;
    nvlog = 0;
    EXPECT(win_key_click_event(&E, 7, 0x1b), 105), expect_call(0, 0xb0, &E, -2, 0, 0);
    nvlog = 0;
    EXPECT(win_key_click_event(&E, 7, 0x1000d), 105), expect_call(0, 0xb0, &E, -1, 0, 0);
    nvlog = 0;
    EXPECT(win_key_click_event(&E, 7, 0x41), 105), EXPECT(nvlog, 2), expect_call(0, 0x2010, NULL, 7, 0x41, 0);
    put(&E, 0x42c, NULL);
    // In a dialog: tab passes the focus on, another key goes to the focus window first.
    ev_window(&D);
    node.win = &D, E.dlg_head = E.dlg_cur = &node, E.dlg_count = 1;
    nvlog = 0, cur_ = NULL;
    EXPECT(win_key_click_event(&E, 7, 9), 5), EXPECT(nvlog, 2), expect_call(0, 0x100, &D, 0, 0, 0), expect_call(1, 0x64, &E, 7, 9, 0);
    nvlog = 0;
    EXPECT(win_key_click_event(&E, 7, 0x41), 1), EXPECT(nvlog, 1), expect_call(0, 0x64, &D, 7, 0x41, 0);  // used: ends it
    key_ret = 0, nvlog = 0;
    EXPECT(win_key_click_event(&E, 7, 0x41), 0), EXPECT(nvlog, 2), expect_call(0, 0x64, &D, 7, 0x41, 0), expect_call(1, 0x64, &E, 7, 0x41, 0);
    E.dlg_count = 0;

    // A key pressed: a key that makes a character isn't a click; one that makes none is, from the window if it has no bars.
    key_ret = 0, nvlog = 0, mvk_calls = 0;
    put(&E, 0x430, (void *)ecb_key_down), put(&E, 0x3c, &L2), L2.keep = 1;
    EXPECT(win_key_down_event(&E, 0x41), 200), EXPECT(mvk_calls, 1), EXPECT(nvlog, 2);
    expect_call(0, 0x2011, NULL, 0x41, 0, 0), expect_call(1, 0x68, &E, 0x41, 0, 0);
    nvlog = 0, mvk_calls = 0;
    EXPECT(win_key_down_event(&E, 0x10025), 200), EXPECT(mvk_calls, 1), EXPECT(nvlog, 3);  // the flags are taken off first
    expect_call(0, 0x64, &E, 0, 0x10025, 0), expect_call(1, 0x2011, NULL, 0x10025, 0, 0), expect_call(2, 0x68, &E, 0x10025, 0, 0);
    key_ret = 3, nvlog = 0;
    EXPECT(win_key_down_event(&E, 0x25), 1), EXPECT(nvlog, 1);  // the click uses it
    // With bars: each is offered the key (an answer of nonzero ends it) and the click is sent once.
    ev_window(&D);
    memset(&S, 0, sizeof S), S.win.vtable = sc_vtable;
    E.scroll_v = E.scroll_h = &S;
    key_ret = 0, scroll_ret = 0, nvlog = 0, mvk_calls = 0;
    EXPECT(win_key_down_event(&E, 0x25), 200), EXPECT(mvk_calls, 1), EXPECT(nvlog, 5);
    expect_call(0, 0x64, &S.win, 0, 0x25, 0), expect_call(1, 0x64, &E, 0, 0x25, 0), expect_call(2, 0x64, &S.win, 0, 0x25, 0);
    expect_call(3, 0x2011, NULL, 0x25, 0, 0), expect_call(4, 0x68, &E, 0x25, 0, 0);
    scroll_ret = 1, nvlog = 0;
    EXPECT(win_key_down_event(&E, 0x25), 1), EXPECT(nvlog, 1);
    E.scroll_v = NULL, scroll_ret = 0, nvlog = 0, mvk_calls = 0;  // only the horizontal bar: the click comes after its question
    EXPECT(win_key_down_event(&E, 0x25), 200), EXPECT(mvk_calls, 1);
    expect_call(0, 0x64, &S.win, 0, 0x25, 0), expect_call(1, 0x64, &E, 0, 0x25, 0);
    E.scroll_h = NULL;
    // In a dialog the focus window is given it first (its bars, its click, then the key itself).
    ev_window(&D);
    D.scroll_v = &S;
    E.dlg_count = 1;
    nvlog = 0, mvk_calls = 0, key_ret = 0;
    EXPECT(win_key_down_event(&E, 0x25), 200), EXPECT(mvk_calls, 2), EXPECT(nvlog, 7);
    // The focus window's bar, its click, then (the key down for it, which does the same again) its handler; then the
    // window's own, which sends no click of its own (the focus window's was one, once for the whole key).
    expect_call(0, 0x64, &S.win, 0, 0x25, 0), expect_call(1, 0x64, &D, 0, 0x25, 0), expect_call(2, 0x64, &S.win, 0, 0x25, 0);
    expect_call(3, 0x64, &D, 0, 0x25, 0), expect_call(4, 0x68, &D, 0x25, 0, 0), expect_call(5, 0x2011, NULL, 0x25, 0, 0);
    expect_call(6, 0x68, &E, 0x25, 0, 0);
    key_ret = 4, nvlog = 0;  // the focus window uses it: ends it
    EXPECT(win_key_down_event(&E, 0x25), 1);
    EXPECT(nvlog, 5);
    key_ret = 0, nvlog = 0;
    EXPECT(win_key_down_event(&E, 0x41), 200), EXPECT(nvlog, 5), expect_call(0, 0x64, &S.win, 0, 0x41, 0), expect_call(1, 0x64, &S.win, 0, 0x41, 0);
    expect_call(2, 0x68, &D, 0x41, 0, 0);
    D.scroll_v = NULL;

    // A key let go: the focus window of a dialog first.
    put(&E, 0x434, (void *)ecb_key_up), put(&E, 0x40, &L2);
    nvlog = 0;
    EXPECT(win_key_up_event(&E, 9), 300), EXPECT(nvlog, 3);
    expect_call(0, 0x6c, &D, 9, 0, 0), expect_call(1, 0x2012, NULL, 9, 0, 0), expect_call(2, 0x6c, &E, 9, 0, 0);
    key_ret = 2, nvlog = 0;
    EXPECT(win_key_up_event(&E, 9), 1), EXPECT(nvlog, 1);
    E.dlg_count = 0;
    key_ret = 2, nvlog = 0;
    EXPECT(win_key_up_event(&E, 9), 302), EXPECT(nvlog, 2);

    // A system key goes to the game's window as a key message, and the default's answer is given back.
    nposts = ndefs = 0;
    EXPECT(win_on_sys_key(&E, 0x41, 1, 3, 0x1e), 0x1234);
    EXPECT(nposts, 1), EXPECT(ndefs, 1);
    EXPECT(posts[0].hwnd, 0x55), EXPECT(posts[0].msg, 0x100), EXPECT(posts[0].wp, 0x41), EXPECT(posts[0].lp, 0x1e0003);
    EXPECT(defs[0].hwnd, 0x55), EXPECT(defs[0].msg, 0x104), EXPECT(defs[0].wp, 0x41), EXPECT(defs[0].lp, 0x1e0003);
    win_on_sys_key(&E, 0x42, 0, 0x10005, 0x1ffff);
    EXPECT(posts[1].msg, 0x101), EXPECT(defs[1].msg, 0x105), EXPECT(posts[1].lp, 0xffff0005);

    // The left button going down. E hangs from P with X in front of it: it comes to the front, takes the focus
    // of P's dialog list, and the press is kept (for the client area or the frame).
    {
        ev_window(&P), ev_window(&X), ev_window(&E);
        E.parent = X.parent = P.parent = NULL;
        P.children[0] = &X, P.children[1] = &E, P.n_children = 2, E.parent = X.parent = &P;
        E.flags = WIN_F_CHILD;
        put(&E, 0x24, &L1), put(&E, 0x44, &L1), put(&E, 0x68, &L2), put(&E, 0x54, &L2);
        L1.keep = L2.keep = 1;
        lists();
        press1_ = press2_ = NULL, press_inside = 7, nvlog = 0, cur_ = NULL;
        win_on_l_button_down(&E, 0, 4, 5, 0, 0);
        EXPECT(P.children[0] == &E, 1), EXPECT(P.children[1] == &X, 1);
        EXPECT(press1_ == &E, 1), EXPECT(press2_ == NULL, 1), EXPECT(press_inside, 1);
        expect_call(0, 0x104, &P, (int)(uintptr_t)&E, 0, 0);  // told to give the focus (a stand-in is its own)
        expect_call(1, 0x2004, NULL, 4, 5, 0), expect_call(2, 0x2022, NULL, 4, 5, 0), expect_call(3, 0x50, &E, 4, 5, 0);
        EXPECT(nvlog, 4);
        // A double click (not taken by a window that takes none) comes first; the frame has its own.
        nvlog = 0;
        win_on_l_button_down(&E, 1, 4, 5, 0, 1);
        EXPECT(press2_ == &E, 1), EXPECT(press1_ == NULL, 1);
        expect_call(1, 0x98, &E, 4, 5, 0), expect_call(2, 0x2022, NULL, 4, 5, 0), expect_call(3, 0x84, &E, 4, 5, 0);
        EXPECT(nvlog, 4);
        nvlog = 0;
        win_on_l_button_down(&E, 1, 4, 5, 0, 0);
        expect_call(1, 0x2001, NULL, 4, 5, 0), expect_call(2, 0x70, &E, 4, 5, 0), expect_call(3, 0x2004, NULL, 4, 5, 0);
        EXPECT(nvlog, 6);
        E.flags = WIN_F_CHILD | 0x200;  // takes no double click
        nvlog = 0;
        win_on_l_button_down(&E, 1, 4, 5, 0, 0);
        expect_call(1, 0x2004, NULL, 4, 5, 0), EXPECT(nvlog, 4);
        // A window with a list of regions: asked which region the mouse is in; the click's answer decides the event.
        own_regions = 1, spot_ret = -1, nvlog = 0;
        win_on_l_button_down(&E, 0, 4, 5, 0, 0);
        EXPECT(nvlog, 5);  // the focus, the question of the list, and the event
        ev_window(&X);
        F.w = E, F.w.parent = NULL;
        *(int32_t *)(F.sub + (0x900 + 3 * 4 - 0x444)) = 0x7777;
        spot_ret = 2, spot_field = 3, region_ret = 0, nvlog = 0;
        win_on_l_button_down(&F.w, 0, 4, 5, 0, 0);
        EXPECT(nvlog, 2);  // the question and the click, which uses it up
        expect_call(0, 0x3000, (uint8_t *)&F.w + 0x8f4, 4, 5, 0), expect_call(1, 0x160, &F.w, 3, 0x7777, 0);
        region_ret = 1, nvlog = 0;
        win_on_l_button_down(&F.w, 0, 4, 5, 0, 0);
        EXPECT(nvlog, 5);
        own_regions = 0, E.flags = WIN_F_CHILD;

        // A window with no parent comes to the front of the top-level ones.
        E.parent = NULL, X.parent = NULL;
        lists();
        parents[0] = &X, parents[1] = &P, parents[2] = &E, parent_n = 3;
        win_on_l_button_down(&E, 0, 1, 1, 0, 0);
        EXPECT(parents[0] == &E, 1), EXPECT(parents[1] == &X, 1), EXPECT(parents[2] == &P, 1);
        E.flags |= WIN_F_ALWAYS_TOP;  // one that keeps its place is not moved
        parents[0] = &X, parents[1] = &P, parents[2] = &E;
        win_on_l_button_down(&E, 0, 1, 1, 0, 0);
        EXPECT(parents[0] == &X, 1), EXPECT(parents[2] == &E, 1);
        E.flags = 0;
        P.children[0] = &X, P.children[1] = &E, P.n_children = 2, E.parent = X.parent = &P;
        win_on_r_button_down(&E, 0, 4, 5, 0, 0);  // the right button: the same, but the mouse is not inside
        EXPECT(P.children[0] == &E, 1), EXPECT(press1_ == &E, 1), EXPECT(press_inside, 0);
        put(&E, 0x30, &L1), put(&E, 0x60, &L2), put(&E, 0x48, &L1), put(&E, 0x6c, &L2);
        nvlog = 0;
        win_on_r_button_down(&E, 1, 4, 5, 0, 1);
        EXPECT(press2_ == &E, 1), EXPECT(press1_ == NULL, 1);
        expect_call(1, 0x9c, &E, 4, 5, 0), expect_call(2, 0x90, &E, 4, 5, 0), EXPECT(nvlog, 3);
        nvlog = 0;
        win_on_r_button_down(&E, 1, 4, 5, 0, 0);
        expect_call(1, 0x2002, NULL, 4, 5, 0), expect_call(2, 0x74, &E, 4, 5, 0), expect_call(3, 0x2007, NULL, 4, 5, 0);
        expect_call(4, 0x5c, &E, 4, 5, 0), EXPECT(nvlog, 5);
    }

    // The left button going up: a click if the press was here and the mouse still is, then the up event if shown.
    {
        ev_window(&E), ev_window(&P);
        put(&E, 0x20, &L1), put(&E, 0x28, &L1), put(&E, 0x50, &L2), put(&E, 0x58, &L2), put(&E, 0x2c, &L1), put(&E, 0x5c, &L2);
        put(&E, 0x34, &L1), put(&E, 0x64, &L2);
        L1.keep = 1;
        press1_ = &E, press2_ = NULL, nvlog = 0;
        win_on_l_button_up(&E, 10, 10, 0, 0);
        EXPECT(press1_ == NULL, 1), EXPECT(press2_ == NULL, 1), EXPECT(nvlog, 4);
        expect_call(0, 0x2003, NULL, 10, 10, 0), expect_call(1, 0x4c, &E, 10, 10, 0), expect_call(2, 0x2005, NULL, 10, 10, 0);
        expect_call(3, 0x54, &E, 10, 10, 0);
        press1_ = NULL, press2_ = &E, nvlog = 0;  // pressed in the frame counts the same
        win_on_l_button_up(&E, 10, 10, 0, 1);
        EXPECT(nvlog, 2), expect_call(0, 0x80, &E, 10, 10, 0), expect_call(1, 0x88, &E, 10, 10, 0);
        press1_ = &E, nvlog = 0;  // outside: no click, but the up event
        win_on_l_button_up(&E, 50, 10, 0, 0);
        EXPECT(nvlog, 2), expect_call(0, 0x2005, NULL, 50, 10, 0);
        press1_ = &E, nvlog = 0;
        win_on_l_button_up(&E, -1, 10, 0, 0);
        EXPECT(nvlog, 2);
        press1_ = &X, nvlog = 0;  // another window had it: no click
        win_on_l_button_up(&E, 10, 10, 0, 0);
        EXPECT(nvlog, 2), expect_call(0, 0x2005, NULL, 10, 10, 0);
        press1_ = NULL, press2_ = &E, nvlog = 0;
        win_on_l_button_up(&E, 10, 10, 0, 0);  // pressed in the frame, let go in the client area: still it
        EXPECT(nvlog, 4);
        E.flags2 = 0, nvlog = 0, press1_ = &E;  // not shown: no up event
        win_on_l_button_up(&E, 10, 10, 0, 0);
        EXPECT(nvlog, 2);
        E.flags2 = WIN_F2_VISIBLE, E.parent = &P, P.flags2 = 0, nvlog = 0, press1_ = NULL;  // what it hangs from is hidden
        win_on_l_button_up(&E, 10, 10, 0, 0);
        EXPECT(nvlog, 0);
        P.flags2 = WIN_F2_VISIBLE, nvlog = 0;
        win_on_l_button_up(&E, 10, 10, 0, 0);
        EXPECT(nvlog, 2);
        E.parent = NULL;
        // The right button: a click if inside (no matter the frame), always the up event.
        press1_ = &E, nvlog = 0;
        win_on_r_button_up(&E, 10, 10, 0, 0);
        EXPECT(press1_ == NULL, 1), EXPECT(nvlog, 4);
        expect_call(0, 0x2006, NULL, 10, 10, 0), expect_call(1, 0x58, &E, 10, 10, 0), expect_call(2, 0x2008, NULL, 10, 10, 0);
        expect_call(3, 0x60, &E, 10, 10, 0);
        press1_ = &E, nvlog = 0;
        win_on_r_button_up(&E, 99, 10, 0, 1);
        EXPECT(nvlog, 1), expect_call(0, 0x94, &E, 99, 10, 0);
    }

    // The game's calls: the key window is told keys (and the hooks), the window under the mouse the buttons.
    {
        ev_window(&E);
        E.parent = NULL;
        modal = NULL, modal_focus = NULL, focus = &E, def_focus = NULL;
        nvlog = 0;
        win_OnKey(0, 0x41, 1, 2, 3);
        EXPECT(key == &E, 1), EXPECT(nvlog, 3);
        expect_call(0, 0x138, &E, 0x41, 1, 9), expect_call(1, 0x2021, NULL, 0x41, 0, 0), expect_call(2, 0x2020, NULL, 0, 0, 0);
        nvlog = 0;
        win_OnKey(0, 0x41, 0, 2, 3);
        EXPECT(nvlog, 2), expect_call(1, 0x2020, NULL, 0, 0, 0);
        win_OnChar(0, 0x61, 4);
        win_OnSysKey(0, 0x12, 1, 5, 6);
        EXPECT(nvlog, 4), expect_call(2, 0x13c, &E, 0x61, 4, 0), expect_call(3, 0x140, &E, 0x12, 1, 5 * 3 + 6);
        focus = NULL;  // nobody has the keys: only the hooks
        nvlog = 0;
        win_OnKey(0, 0x41, 1, 2, 3);
        win_OnChar(0, 1, 1), win_OnSysKey(0, 1, 1, 1, 1);
        EXPECT(nvlog, 2), expect_call(0, 0x2021, NULL, 0x41, 0, 0), expect_call(1, 0x2020, NULL, 0, 0, 0);
        ehooks[0] = ehooks[1] = NULL;
        nvlog = 0;
        win_OnKey(0, 0x41, 1, 2, 3);
        EXPECT(nvlog, 0);
        ehooks[0] = (void *)ehook_after, ehooks[1] = (void *)ehook_key;

        // The mouse: E is pressed, so it is the window under the mouse, and the point is in its client area.
        parents[0] = &E, parent_n = 1, modal = NULL, capture_ = hover_ = press2_ = NULL;
        t_mouse_pos[0] = t_mouse_pos[1] = 0;
        press1_ = &E;
        int32_t px = 30, py = 25;
        win_get_mouse_window(&px, &py);
        press1_ = &E, nvlog = 0, track = NULL;
        win_OnLButtonDown(0, 1, 30, 25, 8);
        EXPECT(nvlog, 3), expect_call(0, 0x144, &E, 1, px, py * 7 + 8 * 3 + t_hit_nc);
        expect_call(1, 0x2022, NULL, px, py, 0), expect_call(2, 0x2020, NULL, 0, 0, 0);
        press1_ = &E, nvlog = 0, track = &E;
        win_OnLButtonUp(0, 30, 25, 4);
        EXPECT(track == NULL, 1), EXPECT(nvlog, 1), expect_call(0, 0x148, &E, px, py, 4 * 3 + t_hit_nc);
        press1_ = &E, nvlog = 0, track = &E;
        win_OnRButtonUp(0, 30, 25, 4);
        EXPECT(track == NULL, 1), EXPECT(nvlog, 1), expect_call(0, 0x150, &E, px, py, 4 * 3 + t_hit_nc);
        press1_ = &E, nvlog = 0;
        win_OnRButtonDown(0, 0, 30, 25, 4);
        EXPECT(nvlog, 3), expect_call(0, 0x14c, &E, 0, px, py * 7 + 4 * 3 + t_hit_nc);
        expect_call(1, 0x2023, NULL, px, py, 0), expect_call(2, 0x2020, NULL, 0, 0, 0);
        // Nothing under the mouse: only the hooks.
        press1_ = NULL, parent_n = 0, nvlog = 0;
        win_OnLButtonDown(0, 0, 500, 500, 0);
        EXPECT(nvlog, 2), expect_call(0, 0x2022, NULL, 500, 500, 0);
        win_OnLButtonUp(0, 500, 500, 0);
        EXPECT(nvlog, 2);
        // A hit on a frame that has a hit test: a nonzero code brings the window to the front, and a code
        // that moves or sizes it starts tracking, which ends the event.
        parents[0] = &X, parents[1] = &E, parent_n = 2, press1_ = &E, E.flags = 0x4000000, nc_code = 2;
        px = 30, py = 25;
        press1_ = NULL, E.flags2 = WIN_F2_VISIBLE, E.inner[0] = E.inner[1] = 0, E.inner[2] = 50, E.inner[3] = 40;
        X.inner[2] = 5, X.inner[3] = 5;
        parents[0] = &X, parents[1] = &E;
        nvlog = 0, track = NULL;
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(track == &E, 1), EXPECT(track_code, 2), EXPECT(track_pos[0], 30), EXPECT(track_pos[1], 25);
        EXPECT(parents[0] == &E, 1), EXPECT(nvlog, 1), expect_call(0, 0x12c, &E, 30, 25, 0);
        nc_code = 0x11, track = NULL, nvlog = 0, parents[0] = &X, parents[1] = &E;
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(track == &E, 1), EXPECT(track_code, 0x11);
        nc_code = 9, track = NULL, nvlog = 0, parents[0] = &X, parents[1] = &E;  // 9 and 18 do not
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(track == NULL, 1), EXPECT(parents[0] == &E, 1), EXPECT(nvlog, 4);
        nc_code = 0x12, nvlog = 0;
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(track == NULL, 1), EXPECT(nvlog, 4);
        nc_code = 0, nvlog = 0, parents[0] = &X, parents[1] = &E;  // no code: not moved
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(parents[0] == &X, 1), EXPECT(nvlog, 4);
        E.flags = 0;  // and without the flag (and not in the frame), it isn't asked
        nc_code = 2, nvlog = 0, track = NULL;
        win_OnLButtonDown(0, 0, 30, 25, 0);
        EXPECT(track == NULL, 1), EXPECT(nvlog, 3);
        ehooks[2] = (void *)ehook_left, ehooks[3] = (void *)ehook_right;
    }
}

// ---- Life cycle ----

static int life_closes;
static void WIN_THIS life_win_close(void *w) { life_closes++; }
static int pic_deleted_;
static void WIN_THIS pic_delete_(void *o, int flag) { pic_deleted_ += flag; }
static void *pic_vtable_[1] = {(void *)pic_delete_};

static int caption_redraws, caption_a, caption_b;
static void LT_THIS caption_redraw(void *w, int a, int b) { caption_redraws++, caption_a = a, caption_b = b; }
static void *caption_vtable[0x180 / 4];

static void test_life(void) {
    life_image_init();
    life_buffer_standins();
    game_win_close = life_win_close;
    static uint8_t obj[0xa40];

    // The base class takes the game's defaults, the first four to the places the original puts them.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(winbase_ctor((struct game_win *)obj) == (struct game_win *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x66ff34)), EXPECT(OW(obj, 4), IMGW(0x9bc080)), EXPECT(OW(obj, 0xc), IMGW(0x9bc084));
    EXPECT(OW(obj, 0x10), IMGW(0x9bc088)), EXPECT(OW(obj, 8), IMGW(0x9bc08c)), EXPECT(OW(obj, 0x14), IMGW(0x9bc090));
    EXPECT(OW(obj, 0x94), IMGW(0x9bc110)), EXPECT(OW(obj, 0x98), 0xcccccccc);
    OW(obj, 0x30) = 5, OW(obj, 0) = 7;
    winbase_reset((struct game_win *)obj);
    EXPECT(OW(obj, 0x30), IMGW(0x9bc0ac)), EXPECT(OW(obj, 0), 7);

    // A new window is hidden, has no children or parent, is its own root and takes the game's colours.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(win_ctor((struct game_win *)obj) == (struct game_win *)obj, 1);
    struct game_win *w = (struct game_win *)obj;
    EXPECT(w->vtable == (void **)(uintptr_t)IMGVA(0x66fdd0), 1), EXPECT(OW(obj, 0xc8), IMGVA(0x66ff30));
    EXPECT(w->root == w, 1), EXPECT(w->flags, 0), EXPECT(w->flags2, 0), EXPECT(w->n_children, 0), EXPECT(w->parent == NULL, 1);
    EXPECT(OW(obj, 0x130), 1), EXPECT(OW(obj, 0x1a0), 2), EXPECT(OW(obj, 0x17c), 1), EXPECT(OW(obj, 0x400), 0), EXPECT(OW(obj, 0x438), 0);
    EXPECT(OW(obj, 0xfc), IMGW(0x9b7af0)), EXPECT(OW(obj, 0x114), IMGW(0x696d38)), EXPECT(OW(obj, 0x128), IMGW(0x696d54));
    EXPECT(OW(obj, 0xf8), IMGW(0x9b7af4)), EXPECT(OW(obj, 0x444), 0xcccccccc);

    // The destructor closes the window, frees the dialog list's blocks and nodes, and puts the defaults back.
    for (int shared = 0; shared < 2; shared++) {
        win_ctor(w);
        struct game_win_node *n[3], *prev = NULL;
        void *payload[3];
        for (int i = 0; i < 3; i++) {
            n[i] = calloc(1, sizeof *n[i]);
            payload[i] = malloc(8);
            n[i]->pad8 = (int32_t)(uintptr_t)payload[i];
            if (prev) prev->next = n[i], n[i]->prev = prev;
            prev = n[i];
        }
        n[2]->next = n[0], n[0]->prev = n[2];
        w->dlg_head = n[0], w->dlg_count = 3, w->dlg_index = 1;
        OW(obj, 0xdc) = (uint32_t)shared;
        OW(obj, 0x30) = 5;
        life_closes = 0, life_freed = 0;
        win_dtor(w);
        EXPECT(life_closes, 1), EXPECT(w->dlg_head == NULL, 1), EXPECT(w->dlg_count, 0), EXPECT(w->dlg_index, 0);
        EXPECT(w->vtable == (void **)(uintptr_t)IMGVA(0x66ff34), 1), EXPECT(OW(obj, 0xc8), IMGVA(0x66ff30)), EXPECT(OW(obj, 0x30), IMGW(0x9bc0ac));
        if (!shared) {
            EXPECT(life_freed, 6), EXPECT(life_freed_has(n[1]), 1), EXPECT(life_freed_has(payload[2]), 1);
            EXPECT(OW(obj, 0xd0) != 0, 1);  // (the cursor is left where the walk ended)
        } else {
            EXPECT(life_freed, 0);
            for (int i = 0; i < 3; i++) free(payload[i]), free(n[i]);
        }
    }

    // A GraphicWin: its Buffer is made, the vtables are its own, and its fields are cleared; close and the
    // destructor close the Win and the Buffer, and close deletes the object at 0xa08.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(graphic_win_ctor(w) == w, 1);
    EXPECT(w->vtable == (void **)(uintptr_t)IMGVA(0x66fc50), 1), EXPECT(OW(obj, 0x444), IMGVA(0x66fc48)), EXPECT(OW(obj, 0xa10), 0);
    EXPECT(OW(obj, 0x9cc), 0), EXPECT(OW(obj, 0xa04), 0), EXPECT(OW(obj, 0xa08), 0), EXPECT(OW(obj, 0xa0c), IMGW(0x9b33c0));
    EXPECT(OW(obj, 0x134), 0), EXPECT(w->root == w, 1);
    OW(obj, 0xa0c) = 1, OW(obj, 0x9d0) = 9;
    void *pic_obj = pic_vtable_;
    void *pic = &pic_obj;
    OW(obj, 0xa08) = (uint32_t)(uintptr_t)pic;
    life_closes = 0, pic_deleted_ = 0;
    graphic_win_close(w);
    EXPECT(life_closes, 1), EXPECT(pic_deleted_, 1), EXPECT(OW(obj, 0xa08), 0), EXPECT(OW(obj, 0xa0c), IMGW(0x9b33c0)), EXPECT(OW(obj, 0x9d0), 0);
    graphic_win_close(w);  // (no picture: nothing to delete)
    EXPECT(pic_deleted_, 1), EXPECT(life_closes, 2);
    life_closes = 0;
    graphic_win_dtor(w);
    EXPECT(life_closes, 1), EXPECT(w->vtable == (void **)(uintptr_t)IMGVA(0x66ff34), 1);

    // Win_set_caption: the old caption freed, the text copied onto the heap (not at all for none); a visible window (whose parent is visible too)
    // draws its frame again, through slot 0x128, with 0 and -1.
    {
        static uint8_t win[0x444], parent[0x444];
        game_free = life_free, game_mem_get = life_mem_get;
        caption_vtable[0x128 / 4] = (void *)caption_redraw;
        memset(win, 0, sizeof win), memset(parent, 0, sizeof parent);
        *(void **)win = caption_vtable;
        struct game_win *cw = (struct game_win *)win;
        win_set_caption(cw, "first");
        EXPECT(strcmp((const char *)cw->title, "first"), 0), EXPECT(caption_redraws, 0);
        char *old = (char *)cw->title;
        life_freed = 0;
        OW(win, 0x9c) = 1, caption_redraws = 0;
        win_set_caption(cw, "second one");
        EXPECT(strcmp((const char *)cw->title, "second one"), 0), EXPECT(life_freed, 1), EXPECT(life_freed_has(old), 1);
        EXPECT(caption_redraws, 1), EXPECT(caption_a, 0), EXPECT(caption_b, -1);
        // A hidden parent keeps the frame as it is; a visible one does not.
        OW(win, 0xc4) = (uint32_t)(uintptr_t)parent, caption_redraws = 0;
        win_set_caption(cw, "third");
        EXPECT(caption_redraws, 0), EXPECT(strcmp((const char *)cw->title, "third"), 0);
        OW(parent, 0x9c) = 1;
        win_set_caption(cw, "fourth");
        EXPECT(caption_redraws, 1);
        // No text: the old one is freed and none is made; the frame is drawn again.
        life_freed = 0, old = (char *)cw->title, caption_redraws = 0;
        win_set_caption(cw, NULL);
        EXPECT(cw->title == NULL, 1), EXPECT(life_freed, 1), EXPECT(life_freed_has(old), 1), EXPECT(caption_redraws, 1);
        OW(win, 0x9c) = 0, caption_redraws = 0;
        win_set_caption(cw, "x");
        EXPECT(caption_redraws, 0);
        free((void *)cw->title);
    }

}

// ---- Win_close, Win_init, GraphicWin_init and PullDown_init ----

// Stand-ins for the slots they call: each logs (slot, window, argument) and answers from a variable.
static struct {
    int slot;
    void *w;
    int a;
} wlog_[64];
static int nwlog, copy_answer_, owner_answer_;
static uint8_t static_bar_obj_[16];
static void wrec_(int slot, void *w, int a) {
    if (nwlog < 64) wlog_[nwlog].slot = slot, wlog_[nwlog].w = w, wlog_[nwlog].a = a;
    nwlog++;
}
static int WIN_THIS wt_hide(struct game_win *w) { return wrec_(0x8, w, 0), 0; }
static int WIN_THIS wt_lost(struct game_win *w) { return wrec_(0x10, w, 0), 0; }
static int WIN_THIS wt_make_bar(struct game_win *w) { return wrec_(0x18, w, 0), (int)(intptr_t)static_bar_obj_; }
static int WIN_THIS wt_bar_map(struct game_win *w, void *bar) {
    return wrec_(0x1c, w, bar == NULL), bar == (void *)static_bar_obj_ ? 0 : (int)(intptr_t)bar;
}
static int WIN_THIS wt_copy(struct game_win *w) { return wrec_(0xf4, w, 0), copy_answer_; }
static int WIN_THIS wt_layout(struct game_win *w) { return wrec_(0x108, w, 0), 0; }
static int WIN_THIS wt_caption(struct game_win *w, int a, int b) { return wrec_(0x128, w, a * 100 + b), 0; }
static int WIN_THIS wt_owner(struct game_win *w, struct game_win *x) { return wrec_(0x168, w, x != NULL), owner_answer_; }
static int WIN_THIS wt_nc_height(struct game_win *w) { return 0; }
static void *wt_vtable[0x180 / 4];
static int fakes_deleted_;
static void WIN_THIS wt_fake_delete(void *o, int flag) { fakes_deleted_ += flag; }
static void *wt_fake_vt[1] = {(void *)wt_fake_delete};
static void *wt_fake_a = &wt_fake_vt, *wt_fake_c = &wt_fake_vt;

static int new_calls_, new_fails_;
static void *wt_new(size_t n) { return ++new_calls_ == new_fails_ ? NULL : calloc(1, n); }
static void WIN_THIS wt_scroll_init(void *bar, int a, int b, int c, void *w, int d) {
    wrec_(0x7000 + (d == -1), bar, a * 1000000 + b * 10000 + c * 100 + (w != NULL));
}
static void WIN_THIS wt_caption_buttons(void *w) { wrec_(0x7100, w, 0); }
static void WIN_THIS wt_min_size(void *w) { wrec_(0x7200, w, 0); }
static int STD wt_message_box(void *w, const char *t, const char *c, int type) { return 0; }
static void wt_exit(int code) {}
static void wt_flip_restore(const int32_t *box, int arg) { wrec_(0x7300, (void *)box, arg); }
static void *wt_dc_;
static char *wt_flip_text_;
static void wt_clear_bubble(void) {}

static struct game_win W1, P1, C0, C1, X1, Y1;
static void wt_win(struct game_win *w) {
    memset(w, 0xcc, sizeof *w);
    win_ctor(w);
    w->vtable = wt_vtable;
}

static void test_close_init(void) {
    static int32_t screen_w = 640, screen_h = 480, margin = 17, flip_box[4] = {1, 2, 3, 4};
    life_image_init();
    life_buffer_standins();
    game_buffer_vtable = wt_fake_vt;  // (the frame buffers a window makes are deleted through it)
    wt_vtable[0x8 / 4] = (void *)wt_hide, wt_vtable[0x10 / 4] = (void *)wt_lost, wt_vtable[0x18 / 4] = (void *)wt_make_bar;
    wt_vtable[0x1c / 4] = (void *)wt_bar_map, wt_vtable[0xf4 / 4] = (void *)wt_copy, wt_vtable[0x108 / 4] = (void *)wt_layout;
    wt_vtable[0x128 / 4] = (void *)wt_caption, wt_vtable[0x168 / 4] = (void *)wt_owner, wt_vtable[0x16c / 4] = (void *)wt_nc_height;
    game_operator_new = wt_new, game_scroll_init_vert_nc = wt_scroll_init, game_scroll_init_horz_nc = wt_scroll_init;
    game_win_do_caption_buttons = wt_caption_buttons, game_graphic_win_compute_min_size = wt_min_size;
    game_message_box_a = wt_message_box, game_exit = wt_exit, game_flip_restore = wt_flip_restore;
    game_win_clear_bubble = wt_clear_bubble;
    game_dc = &wt_dc_, game_flip_text = &wt_flip_text_, game_win_screen_w = &screen_w, game_win_screen_h = &screen_h;
    game_win_nc_margin = &margin;
    game_win_press1 = &press1_, game_win_press2 = &press2_, game_win_capture = &capture_, game_win_hover = &hover_;
    game_win_cur = &cur_, game_win_parents = parents, game_win_parent_n = &parent_n;
    game_flip_box = flip_box;

    // --- Win_close ---
    // A child of P: the press, capture and hover that named it are let go (the capture's owner is told), the window is hidden,
    // taken out of P's list (hidden again there), and set back as the constructor leaves it; what it owned is freed.
    wt_win(&P1), wt_win(&W1), wt_win(&X1), wt_win(&Y1);
    P1.n_children = 3, P1.children[0] = &X1, P1.children[1] = &W1, P1.children[2] = &Y1;
    W1.parent = &P1, W1.flags = 0x1234, W1.flags2 = 7, W1.n_children = 0;
    OW(&W1, 0x19c) = 5, OW(&W1, 0xac) = 6, OW(&W1, 0x134) = 7, OW(&W1, 0x188) = 8, OW(&W1, 0xf0) = 9, OW(&W1, 0x1a0) = 0;
    OW(&W1, 0x13c) = 3, OW(&W1, 0x16c) = 4, OW(&W1, 0x420) = 5, OW(&W1, 0xf4) = 6, OW(&W1, 0x12c) = 10, OW(&W1, 0xfc) = 11;
    W1.title = strdup("caption");
    OW(&W1, 0xe4) = (uint32_t)(uintptr_t)&wt_fake_a, OW(&W1, 0xec) = (uint32_t)(uintptr_t)&wt_fake_a;
    OW(&W1, 0xb4) = (uint32_t)(uintptr_t)&wt_fake_c, OW(&W1, 0xc0) = (uint32_t)(uintptr_t)&wt_fake_c;
    game_free = life_free;
    life_freed = 0, fakes_deleted_ = 0, nwlog = 0;
    press1_ = &W1, press2_ = NULL, capture_ = &W1, hover_ = &W1, wt_dc_ = &W1;
    win_close(&W1);
    EXPECT(press1_ == NULL, 1), EXPECT(capture_ == NULL, 1), EXPECT(hover_ == NULL, 1), EXPECT(wt_dc_ == NULL, 1);
    EXPECT(nwlog, 3), EXPECT(wlog_[0].slot, 0x10), EXPECT(wlog_[1].slot, 0x8), EXPECT(wlog_[2].slot, 0x8), EXPECT(wlog_[2].w == &W1, 1);
    EXPECT(P1.n_children, 2), EXPECT(P1.children[0] == &X1, 1), EXPECT(P1.children[1] == &Y1, 1);
    EXPECT(W1.parent == NULL, 1), EXPECT(W1.root == &W1, 1), EXPECT(W1.flags, 0), EXPECT(W1.flags2, 0), EXPECT(OW(&W1, 0x19c), 0);
    EXPECT(OW(&W1, 0xac), 0), EXPECT(OW(&W1, 0x134), 0), EXPECT(OW(&W1, 0x188), 0), EXPECT(OW(&W1, 0xf0), 0), EXPECT(OW(&W1, 0x1a0), 2);
    EXPECT(OW(&W1, 0x13c), 0), EXPECT(OW(&W1, 0x16c), 0), EXPECT(OW(&W1, 0x420), 0), EXPECT(OW(&W1, 0xf4), 0);
    EXPECT(OW(&W1, 0x12c), 0), EXPECT(OW(&W1, 0x130), 1), EXPECT(OW(&W1, 0xfc), IMGW(0x9b7af0)), EXPECT(OW(&W1, 0x128), IMGW(0x696d54));
    EXPECT(OW(&W1, 0x174), 1), EXPECT(OW(&W1, 0x180), 1), EXPECT(W1.title == NULL, 1), EXPECT(life_freed, 1);
    EXPECT(fakes_deleted_, 4), EXPECT(OW(&W1, 0xe4), 0), EXPECT(OW(&W1, 0xec), 0), EXPECT(OW(&W1, 0xb4), 0), EXPECT(OW(&W1, 0xc0), 0);
    EXPECT(OW(&W1, 0x4), IMGW(0x9bc080)), EXPECT(OW(&W1, 0xc), IMGW(0x9bc084));  // the base class's defaults are back

    // A top-level window leaves the game's list (hidden twice); a window that isn't in it is just hidden once.
    wt_win(&W1), wt_win(&X1);
    parents[0] = &X1, parents[1] = &W1, parents[2] = &Y1, parent_n = 3, nwlog = 0;
    win_close(&W1);
    EXPECT(parent_n, 2), EXPECT(parents[0] == &X1, 1), EXPECT(parents[1] == &Y1, 1), EXPECT(nwlog, 2);
    nwlog = 0;
    win_close(&W1);
    EXPECT(parent_n, 2), EXPECT(nwlog, 1);

    // The children close themselves, each leaving the list, so that the loop skips every other one; a press that went down in a
    // child (or a window below one) is let go too.
    wt_win(&W1), wt_win(&C0), wt_win(&C1), wt_win(&X1);
    W1.n_children = 2, W1.children[0] = &C0, W1.children[1] = &C1, C0.parent = &W1, C1.parent = &W1;
    C0.n_children = 1, C0.children[0] = &X1, X1.parent = &C0;
    press1_ = NULL, press2_ = &X1, parent_n = 0;
    win_close(&W1);
    EXPECT(press2_ == NULL, 1), EXPECT(C0.parent == NULL, 1), EXPECT(X1.parent == NULL, 1), EXPECT(C1.parent == &W1, 1), EXPECT(W1.n_children, 0);
    press1_ = &Y1, press2_ = &X1;  // the first one that is set decides: Y1 is not below W1
    wt_win(&W1), wt_win(&C0), W1.n_children = 1, W1.children[0] = &C0, C0.parent = &W1;
    win_close(&W1);
    EXPECT(press1_ == &Y1, 1), EXPECT(press2_ == &X1, 1);
    press1_ = press2_ = NULL;

    // A scroll bar is first asked of the window (slot 0x1c) when flag 0x40 is set: the static bar maps to none, so isn't deleted.
    wt_win(&W1);
    W1.flags = 0x40;
    OW(&W1, 0x43c) = (uint32_t)(uintptr_t)static_bar_obj_, OW(&W1, 0x440) = (uint32_t)(uintptr_t)&wt_fake_a;
    fakes_deleted_ = 0, nwlog = 0, parent_n = 0;
    win_close(&W1);
    EXPECT(fakes_deleted_, 1), EXPECT(OW(&W1, 0x43c), 0), EXPECT(OW(&W1, 0x440), 0), EXPECT(nwlog, 3);  // hide, two maps

    // The dialog list of the parent: the node is found from the cursor, taken out of the ring (and freed with its block, unless the
    // list is shared), the head and cursor follow, and the count and index are set again.
    for (int shared = 0; shared < 2; shared++) {
        wt_win(&P1), wt_win(&W1);
        struct game_win_node *n[3];
        void *pay[3];
        for (int i = 0; i < 3; i++) n[i] = calloc(1, sizeof *n[i]), pay[i] = calloc(1, 8), n[i]->pad8 = (int32_t)(uintptr_t)pay[i];
        for (int i = 0; i < 3; i++) n[i]->next = n[(i + 1) % 3], n[i]->prev = n[(i + 2) % 3];
        n[0]->win = &X1, n[1]->win = &Y1, n[2]->win = &W1;
        P1.dlg_head = n[0], P1.dlg_cur = n[1], P1.dlg_count = 3, P1.dlg_index = 1;
        OW(&P1, 0xdc) = (uint32_t)shared;
        W1.parent = &P1, W1.flags = 0x100000;
        P1.n_children = 1, P1.children[0] = &W1;
        life_freed = 0;
        win_close(&W1);
        EXPECT(P1.dlg_count, 2), EXPECT(P1.dlg_head == n[0], 1), EXPECT(P1.dlg_cur == n[0], 1), EXPECT(P1.dlg_index, 1);
        EXPECT(n[1]->next == n[0], 1), EXPECT(n[0]->prev == n[1], 1), EXPECT(life_freed, shared ? 0 : 2);
        if (shared) free(pay[2]), free(n[2]);
        // The head itself: the head moves to the next.
        wt_win(&W1);
        n[0]->win = &W1, W1.parent = &P1, W1.flags = 0x100000, P1.dlg_cur = n[1], P1.n_children = 1, P1.children[0] = &W1;
        life_freed = 0;
        win_close(&W1);
        EXPECT(P1.dlg_count, 1), EXPECT(P1.dlg_head == n[1], 1), EXPECT(P1.dlg_cur == n[1], 1), EXPECT(P1.dlg_index, 0);
        if (shared) free(pay[0]), free(n[0]);
        free(pay[1]), free(n[1]);
        // The last node: the list is empty, the head is cleared.
        wt_win(&W1), wt_win(&P1);
        OW(&P1, 0xdc) = (uint32_t)shared;
        n[2] = calloc(1, sizeof *n[2]), n[2]->next = n[2]->prev = n[2], n[2]->win = &W1;
        P1.dlg_head = P1.dlg_cur = n[2], P1.dlg_count = 1;
        W1.parent = &P1, W1.flags = 0x100000, P1.n_children = 1, P1.children[0] = &W1;
        win_close(&W1);
        EXPECT(P1.dlg_count, 0), EXPECT(P1.dlg_head == NULL, 1), EXPECT(P1.dlg_index, -1);
        if (shared) free(n[2]);
    }

    // The bubble text hangs from the window: with no text shown, the close leaves the word alone (taking the text away redraws
    // the screen through win_flip, which this test can't run).
    wt_win(&W1);
    {
        struct game_win **bubble = game_at(0x9b7a4c);
        *bubble = &W1, wt_flip_text_ = NULL, nwlog = 0;
        win_close(&W1);
        EXPECT(*bubble == &W1, 1), EXPECT(nwlog, 1);
        *bubble = NULL;
    }

    // --- Win_init ---
    // A window with a frame, a title, a parent and flags that ask for nothing else: the frame buffers are made, the sizes
    // set from the caption triple, the place counted from the parent, the title copied, the frame laid out (slot 0x108).
    {
        static int32_t caption[3] = {21, 5, -1};
        wt_win(&P1), wt_win(&W1);
        P1.inner[0] = 10, P1.inner[1] = 20, P1.inner[2] = 210, P1.inner[3] = 120;
        P1.n_children = 1, P1.children[0] = &X1;
        game_mem_get = life_mem_get;
        nwlog = 0, parent_n = 0;
        int r = win_init(&W1, -10, -20, 30, 40, "A title", 0x1, &P1, NULL, caption);
        EXPECT(r, 0), EXPECT(W1.caption_h, 21), EXPECT(W1.border, 5), EXPECT(W1.bottom, -1);
        EXPECT(W1.nc_buf_b4 != NULL, 1), EXPECT(W1.nc_buf_b8 != NULL, 1), EXPECT(W1.nc_buf_bc != NULL, 1), EXPECT(W1.nc_buf_c0 != NULL, 1);
        EXPECT(W1.flags, 0x11), EXPECT(W1.flags2, 2 | 4), EXPECT(W1.parent == &P1, 1);
        EXPECT(strcmp(W1.title, "A title"), 0), EXPECT(P1.n_children, 2), EXPECT(P1.children[0] == &W1, 1), EXPECT(P1.children[1] == &X1, 1);
        EXPECT(cur_ == &W1, 1), EXPECT(W1.title_h, 21), EXPECT(W1.resize_border, 0), EXPECT(W1.root == &W1, 1);
        // The frame lies at x = -10 + 200 - 30 = 160, y = -20 + 100 - 40 = 40; the client area is the frame less its edges, from 0.
        int32_t want[4] = {160, 40, 190, 80};
        win_nonclient_to_client_2(&W1, want);
        EXPECT(W1.outer[0], 160), EXPECT(W1.outer[1], 40), EXPECT(W1.outer[2], 190), EXPECT(W1.outer[3], 80);
        EXPECT(W1.inner[0], want[0] - 160), EXPECT(W1.inner[1], want[1] - 40), EXPECT(W1.inner[2], want[2] - 160), EXPECT(W1.inner[3], want[3] - 40);
        EXPECT(W1.clip[0], W1.inner[0]), EXPECT(W1.clip[1], W1.inner[1]), EXPECT(W1.clip[2], W1.inner[2]), EXPECT(W1.clip[3], W1.inner[3]);
        EXPECT(nwlog, 3), EXPECT(wlog_[0].slot, 0x8), EXPECT(wlog_[1].slot, 0x108), EXPECT(wlog_[2].slot, 0x7100);  // (the caption buttons made)
        win_close(&W1);
    }
    // A window without a frame: its client area is where it was put, from the screen's size for a negative place, and it is
    // the front of the top-level list, or the end with flag 0x2000000; an empty title is copied but doesn't make a caption.
    wt_win(&W1), wt_win(&X1), wt_win(&Y1);
    parents[0] = &X1, parents[1] = &Y1, parent_n = 2;
    EXPECT(win_init(&W1, -10, -20, 30, 40, "", 0x20, NULL, NULL, NULL), 0);
    EXPECT(W1.flags, 0x20), EXPECT(W1.flags2, 4), EXPECT(W1.inner[0], 600), EXPECT(W1.inner[1], 420), EXPECT(W1.inner[2], 630), EXPECT(W1.inner[3], 460);
    EXPECT(W1.outer[0], 0), EXPECT(parent_n, 3), EXPECT(parents[0] == &W1, 1), EXPECT(parents[1] == &X1, 1), EXPECT(parents[2] == &Y1, 1);
    EXPECT(W1.title != NULL, 1), EXPECT(W1.title[0], 0), EXPECT(W1.title_h, 0);
    win_close(&W1);
    EXPECT(parent_n, 2);
    wt_win(&W1);
    EXPECT(win_init(&W1, 1, 2, 3, 4, NULL, 0x2000000, NULL, NULL, NULL), 0);
    EXPECT(parents[2] == &W1, 1), EXPECT(parent_n, 3);
    win_close(&W1);
    // Flags 0x80 (or none of 0x1d): no frame, and the bits that need one are cleared (the caption, the thick border, 1).
    wt_win(&W1);
    win_init(&W1, 0, 0, 5, 5, "x", 0x411 | 0x80, NULL, NULL, NULL);
    EXPECT(W1.flags, 0x80), EXPECT(W1.flags2, 4);
    win_close(&W1);
    // Slot 0xf4 is asked when a top flag bit is set, and 0 stops the init (the frame buffers are already there); an owner that
    // answers something stops it too, with that answer (the window is closed again).
    wt_win(&W1);
    copy_answer_ = 0, nwlog = 0;
    EXPECT(win_init(&W1, 0, 0, 5, 5, NULL, 0x10000000, NULL, NULL, NULL), 3);
    EXPECT(wlog_[nwlog - 1].slot, 0xf4), EXPECT(W1.nc_buf_b4 != NULL, 1);
    win_close(&W1);
    wt_win(&W1);
    owner_answer_ = 9, nwlog = 0;
    EXPECT(win_init(&W1, 0, 0, 5, 5, NULL, 0, NULL, (struct game_win *)&P1, NULL), 9);
    EXPECT(wlog_[nwlog - 1].slot, 0x8), EXPECT(W1.nc_buf_b4 == NULL, 1), EXPECT(parent_n, 2);
    owner_answer_ = 0;
    EXPECT(win_init(&W1, 0, 0, 5, 5, NULL, 0, NULL, (struct game_win *)&P1, NULL), 0), EXPECT(W1.nc_owner == &P1, 1);
    win_close(&W1);
    // The scroll bars: made through slot 0x18 for flag 0x40.
    wt_win(&P1), wt_win(&W1);
    P1.flags = 0;
    EXPECT(win_init(&W1, 0, 0, 5, 5, NULL, 0x40 | 0x4 | 0x8, &P1, NULL, NULL), 0);
    EXPECT((void *)W1.scroll_h == (void *)static_bar_obj_, 1), EXPECT((void *)W1.scroll_v == (void *)static_bar_obj_, 1);
    win_close(&W1);
    // (The bars the Scroll class makes, for flags 4 and 8 without 0x40, build real Scroll objects, which this test has no
    // stand-ins for: the soak and the scenes cover them.)
    // The dialog list of the parent takes a window with flag 0x100000 (but not one whose parent is a dialog), the window
    // first in it is the one the list starts at, and the nodes (and their blocks) go when the windows close.
    wt_win(&P1), wt_win(&W1), wt_win(&X1);
    P1.flags = 0x1000;
    EXPECT(win_init(&X1, 0, 0, 5, 5, NULL, 0x100000, &P1, NULL, NULL), 0);
    EXPECT(P1.dlg_count, 0);
    win_close(&X1);
    P1.flags = 0;
    for (int i = 0; i < 2; i++) {
        struct game_win *w = i ? &X1 : &W1;
        if (i) wt_win(&X1);
        EXPECT(win_init(w, 0, 0, 5, 5, NULL, 0x100000, &P1, NULL, NULL), 0);
        EXPECT(P1.dlg_count, i + 1), EXPECT(P1.dlg_head != NULL, 1), EXPECT(P1.dlg_cur == P1.dlg_head, 1), EXPECT(P1.dlg_index, 0);
        EXPECT(P1.dlg_head->win == &W1, 1), EXPECT(P1.dlg_head->pad8 != 0, 1);
        if (i) EXPECT(P1.dlg_head->next->win == &X1, 1), EXPECT(P1.dlg_head->prev == P1.dlg_head->next, 1), EXPECT(P1.dlg_head->next->next == P1.dlg_head, 1);
    }
    life_freed = 0;
    win_close(&X1), win_close(&W1);
    EXPECT(P1.dlg_count, 0), EXPECT(life_freed, 4);
    // An allocation that fails: 4, with the window left as far as it got.
    wt_win(&W1);
    int pn_before = parent_n;
    new_calls_ = 0, new_fails_ = 2;
    EXPECT(win_init(&W1, 0, 0, 5, 5, NULL, 0, NULL, NULL, NULL), 4);
    EXPECT(W1.nc_buf_b4 != NULL, 1), EXPECT(W1.nc_buf_b8 == NULL, 1), EXPECT(parent_n, pn_before);
    new_fails_ = 0;
    win_close(&W1);

    // --- GraphicWin_init and PullDown_init ---
    // The frame's pictures (the eleven words the original copies from the game when flag 0x30000000 is set), the picture
    // object at 0xa08 (flag 0x40000000), the window's own Buffer made as big as its client area, and the Buffer's word 4 naming
    // the window; the minimum size is worked out in between.
    {
        static uint8_t gw[0xa40];
        memset(gw, 0xcc, sizeof gw);
        graphic_win_ctor((struct game_win *)gw);
        ((struct game_win *)gw)->vtable = wt_vtable;
        game_win_close = life_win_close;
        nwlog = 0, parent_n = 0, new_calls_ = 0, copy_answer_ = 1;
        int r = graphic_win_init((struct game_win *)gw, 3, 4, 30, 20, NULL, 0x40000000 | 0x30000000 | 0x800 | 0x80, NULL, NULL, NULL);
        EXPECT(r, 0);
        EXPECT(OW(gw, 0x9cc), IMGW(0x9b3394)), EXPECT(OW(gw, 0x9d0), IMGW(0x9b3398)), EXPECT(OW(gw, 0x9d4), IMGW(0x9b339c));
        EXPECT(OW(gw, 0x9d8), IMGW(0x9b33a0)), EXPECT(OW(gw, 0x9dc), IMGW(0x9b33ac)), EXPECT(OW(gw, 0x9e0), IMGW(0x9b33b0));
        EXPECT(OW(gw, 0x9e4), IMGW(0x9b33a8)), EXPECT(OW(gw, 0x9e8), IMGW(0x9b33a4)), EXPECT(OW(gw, 0x9ec), IMGW(0x9b33b8));
        EXPECT(OW(gw, 0x9f0), IMGW(0x9b33b4)), EXPECT(OW(gw, 0x9f4), IMGW(0x9b33bc));
        EXPECT(OW(gw, 0xa08) != 0, 1), EXPECT(OW(gw, 0x448), (uint32_t)(uintptr_t)gw), EXPECT(new_calls_, 5);  // (four frame buffers and the picture)
        struct game_buffer *buf = (struct game_buffer *)(gw + 0x444);
        EXPECT(buf->width, 30), EXPECT(buf->neg_height, -20);
        EXPECT(wlog_[nwlog - 1].slot, 0x7200);
        EXPECT(((struct game_win *)gw)->inner[0], 3), EXPECT(((struct game_win *)gw)->inner[3], 24);
    }
    // PullDown_init: the menu colour word (0x9b7b68) is taken from the game's default if it isn't set; the menu's own words are set
    // before and after the window's init.
    {
        static uint8_t pd[0xf48];
        memset(pd, 0xcc, sizeof pd);
        graphic_win_ctor((struct game_win *)pd);
        ((struct game_win *)pd)->vtable = wt_vtable;
        IMGW(0x9b7b68) = 0;
        nwlog = 0;
        pulldown_init((struct game_win *)pd, 77);
        EXPECT(IMGW(0x9b7b68), IMGW(0x9bb484)), EXPECT(OW(pd, 0xf30), 77), EXPECT(OW(pd, 0x964), 0);
        EXPECT(((struct game_win *)pd)->flags2 & 4, 4);
        IMGW(0x9b7b68) = 5;
        pulldown_init((struct game_win *)pd, 78);
        EXPECT(IMGW(0x9b7b68), 5), EXPECT(OW(pd, 0xf30), 78);
    }
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
    vtable[0x164 / 4] = (void *)stub_0x164, vtable[0xf0 / 4] = (void *)stub_0xf0, vtable[0xf4 / 4] = (void *)stub_0xf4, vtable[0xfc / 4] = (void *)stub_0xfc;
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

        // The GraphicWin's frame from sprites (bit 0x10000000): each strip is cleared to the key colour, the corner
        // sprites put in its corners, a tile between them and the window told (and the title written first). Without
        // the bit the Win's frame is drawn; without a frame nothing is.
        {
            static uint8_t gmem[0xa40];
            struct game_win *G = (struct game_win *)gmem;
            static struct game_sprite sp[11];
            static uint8_t sd[11][64];
            for (int i = 0; i < 11; i++) {
                memset(sd[i], 21 + i, sizeof sd[i]);
                sp[i] = (struct game_sprite){.data = sd[i], .key = 0, .stride = 8, .width = 3, .height = 2, .full_w = 3, .full_h = 2};
                *(struct game_sprite **)(gmem + 0x9cc + 4 * i) = &sp[i];
            }
            sp[0].full_h = 4, sp[0].height = 4;  // 9cc: the top left corner
            for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
            memcpy(G, &FW, sizeof FW);
            G->flags = WIN_F_CAPTION | 0x10000000;
            G->title = "Hi";
            *(void **)(gmem + 0xa0c) = (void *)gw_title_cb;
            nvlog = 0;
            graphic_win_on_redraw_nc(G, NULL, 0);
            // The top strip (40 wide, 10 high): key colour; the top left corner (9cc) at 0,0; the top right (9d4) at
            // the right edge; the tile (9e8) between them (from the left corner's width, over the window's width less
            // both corners); then the title hook (cdecl), the title slot, and the strip's slot.
            EXPECT(hpx(&s[0], 0, 0), 21), EXPECT(hpx(&s[0], 2, 3), 21), EXPECT(hpx(&s[0], 3, 3), 21 + 7);
            EXPECT(hpx(&s[0], 37, 0), 21 + 2), EXPECT(hpx(&s[0], 36, 9), 21 + 7);  // (the tile: 9e8 is the eighth piece)
            EXPECT(hpx(&s[0], 20, 5), 21 + 7), EXPECT(hpx(&s[1], 5, 2), 99);
            EXPECT(nvlog, 3), expect_call(0, 0x2030, G, 'H', 1, 0), expect_call(1, 0x164, G, 1, 1, 0), expect_call(2, 0xf0, G, 1, 0, 0);
            // A window without a title hook or a caption: no title.
            G->flags = 0x10000000, *(void **)(gmem + 0xa0c) = NULL;
            nvlog = 0;
            graphic_win_on_redraw_nc(G, NULL, 0);
            EXPECT(nvlog, 1), expect_call(0, 0xf0, G, 1, 0, 0);
            // The bottom strip (40 x 5): bottom corners (9d0, 9d8) with their bottom edges on the strip's, the tile
            // (9e4) between them; the sides (3 x 25): the top left corner (9ec when there is no caption: the border's
            // set) at the top, the bottom corner at the bottom.
            for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
            nvlog = 0;
            graphic_win_on_redraw_nc(G, NULL, -1);
            EXPECT(nvlog, 4), expect_call(0, 0xf0, G, 1, 0, 0), expect_call(1, 0xf0, G, 1, 2, 0), expect_call(2, 0xf0, G, 1, 3, 0);
            expect_call(3, 0xf0, G, 1, 1, 0);
            EXPECT(hpx(&s[1], 0, 3), 21 + 1), EXPECT(hpx(&s[1], 0, 4), 21 + 1), EXPECT(hpx(&s[1], 0, 2), key_);
            EXPECT(hpx(&s[1], 38, 4), 21 + 3), EXPECT(hpx(&s[1], 20, 4), 21 + 6);
            // One strip by itself, or the area (screen coordinates): every strip.
            for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
            nvlog = 0;
            graphic_win_on_redraw_nc(G, NULL, 3);
            EXPECT(nvlog, 1), expect_call(0, 0xf0, G, 1, 3, 0), EXPECT(hpx(&s[0], 20, 5), 99), EXPECT(hpx(&s[2], 1, 12), 21 + 4);  // (the left tile, 9dc)
            graphic_win_on_redraw_nc(G, NULL, 1);
            EXPECT(hpx(&s[3], 1, 12), 21 + 5);  // (the right tile, 9e0)
            nvlog = 0;
            graphic_win_on_redraw_nc(G, part, 1);
            EXPECT(nvlog, 4);
            // A missing piece leaves out what needs it (the tile needs both corners).
            *(struct game_sprite **)(gmem + 0x9ec) = NULL;
            for (int i = 0; i < 4; i++) memset(s[i].bits, 99, (size_t)s[i].pitch * -s[i].neg_height);
            graphic_win_on_redraw_nc(G, NULL, 3);
            EXPECT(hpx(&s[2], 1, 20), key_);  // (the tile of 9dc is not drawn without the corner 9ec)
            // Without a frame nothing; without the bit, the Win's frame (nothing for a window with no caption).
            G->flags2 = WIN_F2_VISIBLE;
            nvlog = 0;
            graphic_win_on_redraw_nc(G, NULL, -1);
            EXPECT(nvlog, 0);
            G->flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME, G->flags = 0;
            graphic_win_on_redraw_nc(G, NULL, -1);
            EXPECT(nvlog, 0);
            G->flags = WIN_F_CAPTION;
            graphic_win_on_redraw_nc(G, NULL, 2);
            EXPECT(nvlog, 1), expect_call(0, 0xf0, G, 1, 2, 0);  // (the Win's own redraw of the bottom strip)
        }

        // This window's frame and Buffer copied into another window's frame (the one with the sprite frame): the
        // other's strips go on the screen from its outer corner (the top at the corner, the left and the right under the
        // top, the bottom under the left), this window's likewise, and each of this window's strips is put into each of
        // the other's where they overlap, for the `which` asked for (an area makes it every one) and, if the other
        // draws its own frame, into its picture over its window rectangle.
        {
            game_intersect_rect = intersect_rect_, game_get_window_rect = get_window_rect_;
            static uint8_t obytes[0xa40];
            static struct {
                struct game_win w;
                struct game_buffer own;
            } W;
            struct game_win *O = (struct game_win *)obytes;
            static struct game_buffer os[4], ds[4], pic;
            static uint8_t opx[4][80], dpx[4][80], picpx[40 * 40], ownpx[40 * 40];
            int ow[4] = {20, 3, 3, 20}, oh[4] = {4, 10, 10, 3};  // top, left, right, bottom
            init(&W.w, WIN_F2_VISIBLE | WIN_F2_FRAME);
            init(O, WIN_F2_VISIBLE | WIN_F2_FRAME);
            O->flags = 0x10000000;
            O->outer[0] = 100, O->outer[1] = 100, W.w.outer[0] = 100, W.w.outer[1] = 100, W.w.outer[2] = 120, W.w.outer[3] = 117;
            void **os_slot[4] = {&O->nc_buf_b4, &O->nc_buf_bc, &O->nc_buf_b8, &O->nc_buf_c0};
            void **ds_slot[4] = {&W.w.nc_buf_b4, &W.w.nc_buf_bc, &W.w.nc_buf_b8, &W.w.nc_buf_c0};
            for (int i = 0; i < 4; i++) {  // (the slots are in the order top, left, right, bottom: this window's are 61 to 64)
                hand_buffer(&ds[i], dpx[i], ow[i], oh[i], 61 + i), hand_buffer(&os[i], opx[i], ow[i], oh[i], 9);
                *os_slot[i] = &os[i], *ds_slot[i] = &ds[i];
            }
            hand_buffer(&pic, picpx, 40, 40, 9), hand_buffer(&W.own, ownpx, 40, 40, 8);
            *(struct game_buffer **)(obytes + 0xa08) = &pic;
            f4_result = 0;
            win_copy_into_frame(&W.w, O, NULL, -1);
            EXPECT(hpx(&os[0], 3, 2), 61), EXPECT(hpx(&os[1], 1, 5), 62), EXPECT(hpx(&os[2], 1, 5), 63), EXPECT(hpx(&os[3], 3, 1), 64);
            EXPECT(wr_calls, 0), EXPECT(hpx(&pic, 0, 0), 9);
            // One strip: this window's top into the other's top.
            for (int i = 0; i < 4; i++) memset(opx[i], 9, sizeof opx[i]);
            win_copy_into_frame(&W.w, O, NULL, 0);
            EXPECT(hpx(&os[0], 3, 2), 61), EXPECT(hpx(&os[1], 1, 5), 9), EXPECT(hpx(&os[2], 1, 5), 9), EXPECT(hpx(&os[3], 3, 1), 9);
            win_copy_into_frame(&W.w, O, NULL, 2);
            EXPECT(hpx(&os[3], 3, 1), 64), EXPECT(hpx(&os[1], 1, 5), 9);
            // An area (screen coordinates) cuts every strip and means every one: x 105..109 of the top strip.
            for (int i = 0; i < 4; i++) memset(opx[i], 9, sizeof opx[i]);
            int32_t area[4] = {105, 100, 110, 110};
            win_copy_into_frame(&W.w, O, area, 3);
            EXPECT(hpx(&os[0], 4, 1), 9), EXPECT(hpx(&os[0], 5, 1), 61), EXPECT(hpx(&os[0], 9, 1), 61), EXPECT(hpx(&os[0], 10, 1), 9);
            EXPECT(hpx(&os[1], 1, 5), 9), EXPECT(hpx(&os[3], 6, 1), 9);  // (the others are outside the area)
            int32_t empty_area[4] = {105, 100, 105, 110};
            for (int i = 0; i < 4; i++) memset(opx[i], 9, sizeof opx[i]);
            win_copy_into_frame(&W.w, O, empty_area, -1);
            EXPECT(hpx(&os[0], 5, 1), 9);
            // Not a window with a sprite frame (or without a frame): nothing.
            O->flags = 0;
            win_copy_into_frame(&W.w, O, NULL, -1);
            EXPECT(hpx(&os[0], 5, 1), 9);
            O->flags = 0x10000000, O->flags2 = WIN_F2_VISIBLE;
            win_copy_into_frame(&W.w, O, NULL, -1), win_copy_into_frame(&W.w, NULL, NULL, -1);
            EXPECT(hpx(&os[0], 5, 1), 9);
            O->flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
            // This window without a frame has only its Buffer copied (if it draws a frame itself, slot 0xf4) from
            // its window rectangle in Windows (90, 90 to 130, 130) into the other's strips, where they lie.
            W.w.flags2 = WIN_F2_VISIBLE;
            f4_result = 1;
            win_copy_into_frame(&W.w, O, NULL, 0);
            EXPECT(wr_calls, 1), EXPECT(hpx(&os[0], 5, 1), 8);  // (O has no bit 0x40000000: no picture, one rectangle asked for)
            EXPECT(hpx(&os[1], 1, 5), 9), EXPECT(hpx(&pic, 10, 10), 9);
            // With the picture over the other's window rectangle (bit 0x40000000): for every strip.
            O->flags |= 0x40000000;
            wr_calls = 0;
            win_copy_into_frame(&W.w, O, NULL, 0);
            EXPECT(wr_calls, 2), EXPECT(hpx(&pic, 0, 0), 9);  // (not for one strip)
            win_copy_into_frame(&W.w, O, NULL, -1);
            EXPECT(wr_calls, 4), EXPECT(hpx(&pic, 0, 0), 8), EXPECT(hpx(&pic, 39, 39), 8), EXPECT(hpx(&os[1], 1, 5), 8);
            // The window with a frame as well: each of its strips goes into the picture, too, and the Buffer over them.
            memset(picpx, 9, sizeof picpx);
            W.w.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
            win_copy_into_frame(&W.w, O, NULL, -1);
            EXPECT(hpx(&pic, 12, 12), 8), EXPECT(hpx(&pic, 39, 39), 8);
            // The other window not drawing its frame itself: no picture and no Windows call for its rectangle.
            f4_result = 0;
            for (int i = 0; i < 4; i++) memset(opx[i], 9, sizeof opx[i]);
            memset(picpx, 9, sizeof picpx);
            wr_calls = 0;
            win_copy_into_frame(&W.w, O, NULL, -1);
            EXPECT(wr_calls, 0), EXPECT(hpx(&os[0], 3, 2), 61), EXPECT(hpx(&pic, 12, 12), 9);
        }

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

    test_events();

    test_life();

    test_close_init();

    if (failures) return 1;
    printf("engine_win: ok\n");
    return 0;
}
