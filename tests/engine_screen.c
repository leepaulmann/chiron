// Unit test for how the game's pictures reach the window (screen.c, and the GraphicWin and Win functions in
// win.c that call it): the counted device context, the palette, a Buffer copied to the window, a
// window's background, the flip, the colour cycle, and the GraphicWin updates, with stand-ins for the
// Windows calls that log what they are given. No game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/palette.h"
#include "../src/engine/screen.h"
#include "../src/engine/win.h"

static int failures, line_;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line_, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) (line_ = __LINE__, check_(#expr, (long)(expr), (long)(want)))
#define STD __attribute__((stdcall))

// ---- Stand-ins for the Windows calls: each logs a line of numbers ----
static struct {
    const char *name;
    long a[11];
} log_[64];
static int nlog;
static void note(const char *name, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8) {
    if (nlog < 64) log_[nlog] = (typeof(log_[0])){name, {a0, a1, a2, a3, a4, a5, a6, a7, a8}};
    nlog++;
}
static void reset_log(void) { nlog = 0; }
// The call i of the log is `name`, with these numbers (as many as given).
static void expect_log_(int i, const char *name, int n, const long *want) {
    char what[80];
    snprintf(what, sizeof what, "call %d is %s", i, name);
    check_(what, i < nlog && !strcmp(log_[i].name, name), 1);
    if (i >= nlog) return;
    for (int k = 0; k < n; k++) {
        snprintf(what, sizeof what, "call %d (%s) argument %d", i, name, k);
        check_(what, log_[i].a[k], want[k]);
    }
}
#define expect_log(i, name, n, ...) (line_ = __LINE__, expect_log_(i, name, n, (long[9]){__VA_ARGS__}))

static void *STD null_dc(void *hwnd) { return NULL; }
static int gets, releases;
static void *STD t_get_dc(void *hwnd) { return gets++, note("GetDC", (long)hwnd, 0, 0, 0, 0, 0, 0, 0, 0), (void *)0x1000; }
static int STD t_release_dc(void *hwnd, void *dc) { return releases++, note("ReleaseDC", (long)hwnd, (long)dc, 0, 0, 0, 0, 0, 0, 0), 1; }
static int STD t_bit_blt(void *d, int x, int y, int w, int h, void *s, int sx, int sy, unsigned rop) {
    return note("BitBlt", (long)d, x, y, w, h, (long)s, sx, sy, rop), 1;
}
static int STD t_stretch_blt(void *d, int x, int y, int w, int h, void *s, int sx, int sy, int sw, int sh, unsigned rop) {
    return note("StretchBlt", (long)d, x, y, w, h, (long)s, sx, sy, sw), note("StretchBlt2", sh, rop, 0, 0, 0, 0, 0, 0, 0), 1;
}
static int STD t_validate(void *hwnd, const int32_t *r) { return note("ValidateRect", (long)hwnd, r[0], r[1], r[2], r[3], 0, 0, 0, 0), 1; }
static void *STD t_brush(uint32_t color) { return note("CreateSolidBrush", color, 0, 0, 0, 0, 0, 0, 0, 0), (void *)0x2000; }
static int STD t_fill(void *dc, const int32_t *r, void *brush) {
    return note("FillRect", (long)dc, r[0], r[1], r[2], r[3], (long)brush, 0, 0, 0), 1;
}
static void *STD t_pen(const int32_t *lp) {
    return note("CreatePenIndirect", lp[0], lp[1], lp[3], 0, 0, 0, 0, 0, 0), (void *)0x3000;
}
static void *STD t_select_obj(void *dc, void *o) { return note("SelectObject", (long)dc, (long)o, 0, 0, 0, 0, 0, 0, 0), (void *)0x4000; }
static int STD t_move_to(void *dc, int x, int y, void *old) { return note("MoveToEx", (long)dc, x, y, (long)old, 0, 0, 0, 0, 0), 1; }
static int STD t_line_to(void *dc, int x, int y) { return note("LineTo", (long)dc, x, y, 0, 0, 0, 0, 0, 0), 1; }
static int STD t_delete(void *o) { return note("DeleteObject", (long)o, 0, 0, 0, 0, 0, 0, 0, 0), 1; }
static void *STD t_select_pal(void *dc, void *p, int bg) { return note("SelectPalette", (long)dc, (long)p, bg, 0, 0, 0, 0, 0, 0), NULL; }
static unsigned STD t_realize(void *dc) { return note("RealizePalette", (long)dc, 0, 0, 0, 0, 0, 0, 0, 0), 0; }
static int STD t_animate(void *p, unsigned start, unsigned n, const void *e) {
    return note("AnimatePalette", (long)p, start, n, (long)e, 0, 0, 0, 0, 0), 1;
}
static int STD t_invalidate(void *hwnd, const int32_t *r, int erase) {
    return note("InvalidateRect", (long)hwnd, r[0], r[1], r[2], r[3], erase, 0, 0, 0), 7;
}
static int STD t_color_table(void *dc, unsigned s, unsigned n, const void *c) {
    return note("SetDIBColorTable", (long)dc, s, n, (long)c, 0, 0, 0, 0, 0), n;
}
static int STD t_intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) return memset(out, 0, 16), 0;
    return memcpy(out, r, 16), 1;
}
// A DirectDraw surface and palette that log.
static int STD dd_get_dc(void *self, void **dc) { return *dc = (void *)0x5000, note("dd GetDC", (long)self, 0, 0, 0, 0, 0, 0, 0, 0), 0; }
static int STD dd_release_dc(void *self, void *dc) { return note("dd ReleaseDC", (long)self, (long)dc, 0, 0, 0, 0, 0, 0, 0), 0; }
static int STD dd_set_entries(void *self, int a, int b, int n, void *e) { return note("dd SetEntries", a, b, n, (long)e, 0, 0, 0, 0, 0), 0; }
static void *dd_vtable[0x80 / 4], *ddp_vtable[0x80 / 4];
static struct game_com dd_obj = {dd_vtable}, ddp_obj = {ddp_vtable};


// ---- What a Buffer's set-up uses (as in engine_win.c): a DC and a DIB each get a handle and pixels ----
static int next_handle = 100, dibs_made;
static uint8_t dib_pool[16][70 * 50];
static void *STD t_create_dc(void *dc) { return (void *)(intptr_t)next_handle++; }
static void *STD t_create_dib(void *dc, void *info, int usage, void **bits, void *s, int o) {
    *bits = dib_pool[dibs_made++ % 16];
    return (void *)(intptr_t)next_handle++;
}
static int STD t_ok1(void *a) { return 1; }
static void *STD t_create_rgn(const int32_t *r) { return (void *)(intptr_t)next_handle++; }
static int STD t_select_clip_rgn(void *dc, void *rgn) { return 1; }
static int STD t_set_mode(void *dc, int m) { return 0; }
static int STD t_equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static int STD t_set_rect(int32_t *r, int l, int t, int rt, int b) { return r[0] = l, r[1] = t, r[2] = rt, r[3] = b, 1; }
static void t_free(void *p) { (void)p; }
static void __attribute__((thiscall)) t_spot(void *s) { memset(s, 0, 12); }
static void __attribute__((thiscall)) t_spot_init(void *s, int n) { ((int32_t *)s)[2] = n; }
static void *t_vtable[4];
static void *palette_now_, *font_default_;
static int32_t default_520;

// ---- The game's globals ----
static int32_t dc_count_, copy_own_, over_pos_[2], over_size_[2], back_off_[2], flip_box_[4], flip_border_, flip_color_, flip_fill_;
static void *dc_;
static struct game_com *dd_surface_, *dd_palette_;
static uint32_t pal_handle_, pal_serial_, pal_hold_, hwnd_ = 0x77;
static void *ddraw_;
static struct palette pal_, own_pal_;
static struct palette *pal_ptr = &pal_;
static struct game_buffer back_, overlay_, *back_alt_;
static void *flip_hook_calls_[8];
static int hook_calls;
static void flip_hook(void) { hook_calls++; }
static void (*flip_hook_)(void);
static char *flip_text_;
static struct font *flip_font_;
static int32_t over_on_;
static struct game_win *opaque_, *zorder_[8], *parents_[8];
static int32_t z_n_, parent_n_, opaque_flag_;
static uint8_t key_ = 7;
static uint8_t px_back[200 * 100];

static void *t_vt[4];
static void __attribute__((thiscall)) t_unlock(struct game_buffer *b) { b->base = NULL; }

// A Buffer with pixels, a memory DC and a palette serial number.
static void make_buffer(struct game_buffer *b, uint8_t *bits, int w, int h, long dc) {
    memset(b, 0, sizeof *b);
    b->vtable = t_vt;
    b->bits = bits, b->width = w, b->neg_height = -h, b->pitch = w;
    b->full[2] = w, b->full[3] = h, b->clip[2] = w, b->clip[3] = h;
    *(int32_t *)((uint8_t *)b + 0x64) = (int32_t)dc;
}
static int32_t *at(void *b, int off) { return (int32_t *)((uint8_t *)b + off); }

static void reset(void) {
    reset_log();
    gets = releases = 0;
    dc_count_ = 0, dc_ = NULL, dd_surface_ = NULL, dd_palette_ = NULL, ddraw_ = NULL;
    pal_handle_ = 0x66, pal_serial_ = 5, pal_hold_ = 0, copy_own_ = 0;
    pal_.serial = 5;
    memset(pal_.rgb, 0, sizeof pal_.rgb);
    for (int i = 0; i < 256; i++) pal_.rgb[i][0] = (uint8_t)i, pal_.rgb[i][1] = (uint8_t)(i + 1), pal_.rgb[i][2] = (uint8_t)(i + 2);
    for (int i = 0; i < 5; i++) pal_.cycle[i].id = -1;
    pal_ptr = &pal_;
    back_alt_ = NULL;
}

int main(void) {
    t_vt[1] = (void *)t_unlock;
    dd_vtable[0x44 / 4] = (void *)dd_get_dc, dd_vtable[0x68 / 4] = (void *)dd_release_dc, ddp_vtable[0x18 / 4] = (void *)dd_set_entries;
    game_get_dc = t_get_dc, game_release_dc = t_release_dc, game_bit_blt = t_bit_blt, game_stretch_blt = t_stretch_blt;
    game_validate_rect = t_validate, game_create_solid_brush = t_brush, game_fill_rect = t_fill, game_create_pen_indirect = t_pen;
    game_select_object = t_select_obj, game_move_to_ex = t_move_to, game_line_to = t_line_to, game_delete_object = t_delete;
    game_select_palette = t_select_pal, game_realize_palette = t_realize, game_animate_palette = t_animate;
    game_invalidate_rect = t_invalidate, game_set_dib_color_table = t_color_table, game_intersect_rect = t_intersect;
    game_dc_count = &dc_count_, game_dc = &dc_, game_dd_surface = &dd_surface_, game_dd_palette = &dd_palette_;
    game_palette_serial = &pal_serial_, game_palette_hold = &pal_hold_, game_palette_handle = &pal_handle_;
    game_copy_own_palette = &copy_own_, game_palette_copy = &pal_ptr, game_ddraw = &ddraw_, game_win_hwnd = &hwnd_;
    game_win_back = &back_, game_win_overlay = &overlay_, game_win_back_alt = &back_alt_, game_win_back_off = back_off_;
    game_win_overlay_on = &over_on_, game_win_overlay_pos = over_pos_, game_win_overlay_size = over_size_;
    game_flip_hook = &flip_hook_, game_flip_text = &flip_text_, game_flip_box = flip_box_, game_flip_border = &flip_border_;
    game_flip_text_color = &flip_color_, game_flip_fill = &flip_fill_, game_flip_font = &flip_font_, game_default_key = &key_;
    game_win_opaque = &opaque_, game_win_zorder = zorder_, game_win_parents = parents_, game_win_z_n = &z_n_;
    game_win_parent_n = &parent_n_, game_win_opaque_flag = &opaque_flag_;
    static int32_t fill_gap_, copy_scratch_[4];
    game_fill_gap = &fill_gap_, game_copy_rem = &copy_scratch_[0], game_mask_units = &copy_scratch_[1];
    game_mask_gap_a = &copy_scratch_[2], game_mask_gap_b = &copy_scratch_[3];
    game_create_compatible_dc = t_create_dc, game_create_dib_section = t_create_dib, game_delete_dc = t_ok1;
    game_create_rect_rgn_indirect = t_create_rgn, game_select_clip_rgn = t_select_clip_rgn;
    game_equal_rect = t_equal, game_set_rect = t_set_rect;
    game_set_bk_mode = (void *)t_set_mode, game_set_text_align = (void *)t_set_mode;
    game_palette_now = &palette_now_, game_font_default = (void **)&font_default_;
    game_buffer_vtable = t_vtable, game_buffer_520_default = &default_520, game_free = t_free;
    game_spot_ctor = t_spot, game_spot_dtor = t_spot, game_spot_init = t_spot_init;
    t_vtable[0] = t_vtable[1] = (void *)t_unlock;
    make_buffer(&back_, px_back, 200, 100, 0x6000);

    // The counted DC: the first user gets it with GetDC for the game's window, the last gives it back.
    reset();
    EXPECT((long)win_get_dc(), 0x1000), EXPECT(dc_count_, 1), EXPECT((long)dc_, 0x1000), EXPECT(gets, 1);
    EXPECT((long)win_get_dc(), 0x1000), EXPECT(dc_count_, 2), EXPECT(gets, 1);
    win_release_dc();
    EXPECT(dc_count_, 1), EXPECT(releases, 0), EXPECT((long)dc_, 0x1000);
    win_release_dc();
    EXPECT(dc_count_, 0), EXPECT(releases, 1), EXPECT((long)dc_, 0);
    expect_log(0, "GetDC", 1, 0x77), expect_log(1, "ReleaseDC", 2, 0x77, 0x1000);
    // With a DirectDraw surface, its GetDC and ReleaseDC, and no GDI call.
    reset();
    dd_surface_ = &dd_obj;
    EXPECT((long)win_get_dc(), 0x5000), EXPECT(dc_count_, 1), EXPECT(gets, 0);
    win_release_dc();
    EXPECT(dc_count_, 0), EXPECT((long)dc_, 0), EXPECT(releases, 0);
    expect_log(0, "dd GetDC", 1, (long)&dd_obj), expect_log(1, "dd ReleaseDC", 2, (long)&dd_obj, 0x5000);
    // A DC that can't be had leaves nothing counted.
    reset();
    game_get_dc = null_dc;
    EXPECT((long)win_get_dc(), 0), EXPECT(dc_count_, 0);
    game_get_dc = t_get_dc;

    // A window notes the palette's serial number.
    static struct game_win W;
    memset(&W, 0, sizeof W);
    reset();
    W.pal_serial = 1;
    win_sync_palette(&W);
    EXPECT(W.pal_serial, 5);
    pal_.serial = 9;
    win_sync_palette(&W);
    EXPECT(W.pal_serial, 9);

    // The palette: the main Buffer takes its colours, and unless held or the serial number is the one given, the
    // system palette's colours 10 to 245 are animated.
    reset();
    pal_serial_ = 4;
    EXPECT(palette_set(&pal_), 0);
    EXPECT(pal_ptr == &pal_, 1), EXPECT(pal_serial_, 5);
    int animated = 0;
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "AnimatePalette")) {
            animated++;
            EXPECT(log_[i].a[0], 0x66), EXPECT(log_[i].a[1], 10), EXPECT(log_[i].a[2], 0xec), EXPECT(log_[i].a[3], (long)pal_.rgb[10]);
        }
    EXPECT(animated, 1);
    reset();
    EXPECT(palette_set(&pal_), 0);  // the same serial number
    for (int i = 0; i < nlog; i++) EXPECT(strcmp(log_[i].name, "AnimatePalette") != 0, 1);
    reset();
    pal_serial_ = 4, pal_hold_ = 1;
    palette_set(&pal_);
    EXPECT(pal_serial_, 4);
    for (int i = 0; i < nlog; i++) EXPECT(strcmp(log_[i].name, "AnimatePalette") != 0, 1);
    pal_handle_ = 0;
    EXPECT(palette_set(&pal_), 7);
    reset();
    ddraw_ = (void *)1;
    EXPECT(palette_set(&pal_), 0), EXPECT(nlog, 0);
    dd_palette_ = &ddp_obj;
    EXPECT(palette_set(&pal_), 0);
    expect_log(0, "dd SetEntries", 4, 0, 0, 0x100, (long)&pal_);
    ddraw_ = NULL;

    // A Buffer copied to the window: cut to the Buffer and to the window's rectangle, the source moved by what is cut
    // off the left and top, at the window's place on the screen; made valid; and every user counted back.
    static struct game_buffer B;
    static uint8_t px_b[40 * 30];
    make_buffer(&B, px_b, 40, 30, 0x7000);
    memset(&W, 0, sizeof W);
    W.outer[0] = 100, W.outer[1] = 50, W.inner[0] = 3, W.inner[1] = 4;
    W.clip[0] = 5, W.clip[1] = 6, W.clip[2] = 25, W.clip[3] = 26;
    reset();
    EXPECT(buffer_copy_to_window(&B, &W, 2, 3, 0, 0, 100, 100), 0);
    // The area is 38 by 27 at (0, 0); cut to (5, 6)-(25, 26): the source starts 5 and 6 further on.
    int bit = -1;
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) bit = i;
    EXPECT(bit >= 0, 1);
    expect_log(bit, "BitBlt", 9, 0x1000, 5 + 103, 6 + 54, 20, 20, 0x7000, 7, 9, 0xcc0020);
    expect_log(bit + 1, "ValidateRect", 5, 0x77, 108, 60, 128, 80);
    EXPECT(dc_count_, 0), EXPECT(*at(&B, 0x68), 0), EXPECT(*at(&B, 0x60), 0);
    EXPECT(*at(&B, 0x584) == (int32_t)(intptr_t)&pal_, 1), EXPECT(*at(&B, 0x57c), 1), EXPECT(*at(&B, 0x4a4), 5);
    EXPECT(W.pal_serial, 5);
    int tables = 0;
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "SetDIBColorTable")) tables++;
    EXPECT(tables, 1);
    // Nothing of the area in the window: nothing drawn, the DCs given back.
    reset();
    EXPECT(buffer_copy_to_window(&B, &W, 0, 0, 200, 200, 10, 10), 0);
    for (int i = 0; i < nlog; i++) EXPECT(strcmp(log_[i].name, "BitBlt") != 0, 1);
    EXPECT(dc_count_, 0), EXPECT(*at(&B, 0x68), 0);
    // Refused: a Buffer on a DirectDraw surface, none with pixels, no window, no DC (the Buffer's memory DC).
    reset();
    B.dd = (void *)1;
    EXPECT(buffer_copy_to_window(&B, &W, 0, 0, 0, 0, 1, 1), 0x12), EXPECT(nlog, 0);
    B.dd = NULL;
    EXPECT(buffer_copy_to_window(&B, NULL, 0, 0, 0, 0, 1, 1), 0x10);
    B.bits = NULL;
    EXPECT(buffer_copy_to_window(&B, &W, 0, 0, 0, 0, 1, 1), 7);
    B.bits = px_b;
    *at(&B, 0x64) = 0;
    EXPECT(buffer_copy_to_window(&B, &W, 0, 0, 0, 0, 1, 1), 7), EXPECT(dc_count_, 0);
    EXPECT(*at(&B, 0x68), 1);  // (the Buffer's own count is left up, as in the original)
    *at(&B, 0x68) = 0, *at(&B, 0x60) = 0;
    *at(&B, 0x64) = 0x7000;
    // The Buffer's own palette is used unless the game says otherwise.
    reset();
    own_pal_.serial = 33;
    *at(&B, 0x584) = (int32_t)(intptr_t)&own_pal_, *at(&B, 0x4a4) = 0;
    buffer_copy_to_window(&B, &W, 0, 0, 0, 0, 10, 10);
    EXPECT(*at(&B, 0x4a4), 33), EXPECT(*at(&B, 0x584) == (int32_t)(intptr_t)&own_pal_, 1);
    copy_own_ = 1;
    buffer_copy_to_window(&B, &W, 0, 0, 0, 0, 10, 10);
    EXPECT(*at(&B, 0x4a4), 5), EXPECT(*at(&B, 0x584) == (int32_t)(intptr_t)&pal_, 1);
    copy_own_ = 0;

    // The stretched copy: the rectangle `from` of the window (cut to its rectangle), the area `to` of the Buffer.
    reset();
    int32_t from[4] = {0, 0, 30, 30}, to[4] = {2, 4, 12, 24};
    EXPECT(buffer_copy_to_window_2(&B, &W, from, to), 0);
    int st = -1;
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "StretchBlt")) st = i;
    EXPECT(st >= 0, 1);
    expect_log(st, "StretchBlt", 9, 0x1000, 108, 60, 20, 20, 0x7000, 2, 4, 10);
    expect_log(st + 1, "StretchBlt2", 2, 20, 0xcc0020);
    expect_log(st + 2, "ValidateRect", 5, 0x77, 108, 60, 128, 80);
    EXPECT(dc_count_, 0), EXPECT(*at(&B, 0x68), 0);
    reset();
    EXPECT(buffer_copy_to_window_2(&B, &W, NULL, to), 0x10), EXPECT(buffer_copy_to_window_2(&B, &W, from, NULL), 0x10);
    int32_t far_[4] = {500, 500, 510, 510};
    EXPECT(buffer_copy_to_window_2(&B, &W, far_, to), 0), EXPECT(dc_count_, 0), EXPECT(*at(&B, 0x68), 0);
    B.dd = (void *)1;
    EXPECT(buffer_copy_to_window_2(&B, &W, from, to), 0x12);
    B.dd = NULL;

    // A line: a pen of a palette index, width and style; from (x1, y1) to (x2, y2).
    reset();
    W.pal_serial = 1;
    win_window_line_raw(&W, 40, 50, 10, 20, 0x12345, 3, 2);
    expect_log(0, "GetDC", 0, 0);
    expect_log(1, "CreatePenIndirect", 3, 2, 3, 0x1000000 | 0x2345);
    expect_log(2, "SelectObject", 2, 0x1000, 0x3000);
    expect_log(3, "MoveToEx", 4, 0x1000, 10, 20, 0);
    expect_log(4, "LineTo", 3, 0x1000, 40, 50);
    expect_log(5, "SelectObject", 2, 0x1000, 0x4000);
    expect_log(6, "DeleteObject", 1, 0x3000);
    expect_log(7, "ReleaseDC", 2, 0x77, 0x1000);
    EXPECT(W.pal_serial, 5), EXPECT(dc_count_, 0);

    // A background with no picture: the colour of the palette entry, over the rectangle.
    reset();
    memset(&W, 0, sizeof W);
    W.bg_color = 10;
    int32_t rect[4] = {1, 2, 30, 40};
    win_on_paint(&W, rect);
    expect_log(1, "CreateSolidBrush", 1, (12L << 16) | (11 << 8) | 10);
    expect_log(2, "FillRect", 6, 0x1000, 1, 2, 30, 40, 0x2000);
    expect_log(3, "DeleteObject", 1, 0x2000);
    expect_log(4, "ReleaseDC", 0, 0);
    // A picture tiled: the tiles start so a centred picture would be on a tile.
    static struct game_buffer P;
    static uint8_t px_p[10 * 8];
    make_buffer(&P, px_p, 10, 8, 0x8000);
    memset(&W, 0, sizeof W);
    W.inner[2] = 25, W.inner[3] = 20, W.clip[2] = 1000, W.clip[3] = 1000, W.bg_pic = &P;
    reset();
    win_on_paint(&W, (int32_t[4]){0, 0, 30, 25});
    // Tiles at x = -3, 7, 17, 27 and y = -2, 6, 14, 22: 16 copies.
    int blits = 0, xs[16], ys[16];
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt") && blits < 16) xs[blits] = (int)log_[i].a[1], ys[blits] = (int)log_[i].a[2], blits++;
    EXPECT(blits, 16);
    int want_x[4] = {0, 7, 17, 27}, want_y[4] = {0, 6, 14, 22};  // the first tile is cut at the window's left and top
    for (int i = 0; i < 16 && i < blits; i++) {
        char what[40];
        snprintf(what, sizeof what, "tile %d x", i), check_(what, xs[i], want_x[i % 4]);
        snprintf(what, sizeof what, "tile %d y", i), check_(what, ys[i], want_y[i / 4]);
    }
    EXPECT(dc_count_, 0);
    // A picture centred: the colour round it in four rectangles, the picture in the middle.
    W.bg_center = 1;
    reset();
    win_on_paint(&W, (int32_t[4]){0, 0, 30, 25});
    expect_log(2, "FillRect", 5, 0x1000, 0, 0, 25, 6);
    int fills = 0;
    for (int i = 0; i < nlog; i++) fills += !strcmp(log_[i].name, "FillRect");
    EXPECT(fills, 4);
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) expect_log(i, "BitBlt", 5, 0x1000, 7, 6, 10, 8);

    // The flip: the whole Buffer, or the rectangle, to the window's DC, after the palette is selected into it.
    reset();
    flip_hook_ = flip_hook, hook_calls = 0, over_on_ = 0, flip_text_ = NULL;
    win_flip(NULL);
    EXPECT(hook_calls, 1);
    int sel = -1, real = -1, blt = -1, rel = -1;
    for (int i = 0; i < nlog; i++) {
        if (!strcmp(log_[i].name, "SelectPalette")) sel = i;
        if (!strcmp(log_[i].name, "RealizePalette")) real = i;
        if (!strcmp(log_[i].name, "BitBlt")) blt = i;
        if (!strcmp(log_[i].name, "ReleaseDC")) rel = i;
    }
    EXPECT(sel >= 0 && sel < real && real < blt && blt < rel, 1);
    expect_log(sel, "SelectPalette", 3, 0x1000, 0x66, 0);
    expect_log(blt, "BitBlt", 9, 0x1000, 0, 0, 200, 100, 0x6000, 0, 0, 0xcc0020);
    EXPECT(dc_count_, 0), EXPECT(*at(&back_, 0x68), 0);
    reset();
    win_flip((int32_t[4]){10, 20, 50, 70});
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) expect_log(i, "BitBlt", 9, 0x1000, 10, 20, 40, 50, 0x6000, 10, 20, 0xcc0020);
    // Without a DC nothing is drawn, and the DC isn't counted.
    {
        reset();
        game_get_dc = null_dc;
        win_flip(NULL);
        game_get_dc = t_get_dc;
        EXPECT(dc_count_, 0);
        for (int i = 0; i < nlog; i++) EXPECT(strcmp(log_[i].name, "BitBlt") != 0, 1);
    }
    flip_hook_ = NULL;

    // The colour cycle: the colours of the cycle move one place down, the first goes last, and Windows is given them.
    static struct {
        struct palette p;
        int32_t after;
    } C;
    reset();
    memcpy(&C.p, &pal_, sizeof C.p);
    C.p.cycle[0].id = 7, C.p.cycle[0].start = 20, C.p.cycle[0].count = 4;  // (the slots fill from the first)
    pal_serial_ = 1;
    palette_timer_callback(7, &C.p);
    for (int k = 0; k < 4; k++) {
        int from_ = k < 3 ? 20 + k + 1 : 20;
        EXPECT(C.p.rgb[20 + k][0], from_), EXPECT(C.p.rgb[20 + k][1], from_ + 1), EXPECT(C.p.rgb[20 + k][2], from_ + 2);
    }
    EXPECT(C.p.rgb[24][0], 24);  // outside the cycle
    expect_log(0, "AnimatePalette", 4, 0x66, 20, 4, (long)C.p.rgb[20]);
    EXPECT(pal_serial_, C.p.serial);
    // Not the cycle's id, a held palette, no palette: nothing. All five slots used and none the id: nothing.
    reset();
    palette_timer_callback(8, &C.p), pal_hold_ = 1, palette_timer_callback(7, &C.p), pal_hold_ = 0, palette_timer_callback(7, NULL);
    EXPECT(nlog, 0);
    for (int i = 0; i < 5; i++) C.p.cycle[i].id = 20 + i;
    C.after = 9;  // (the word after the five slots is read too)
    palette_timer_callback(8, &C.p);
    EXPECT(nlog, 0);

    // The GraphicWin updates and soft updates, on a window with a Buffer of its own at 0x444.
    static struct {
        struct game_win w;
        struct game_buffer buf;
    } G;
    static uint8_t px_g[20 * 10];
    memset(&G, 0, sizeof G);
    make_buffer(&G.buf, px_g, 20, 10, 0x9000);
    G.w.outer[0] = 100, G.w.outer[1] = 200, G.w.inner[0] = 1, G.w.inner[1] = 2;
    G.w.flags2 = WIN_F2_VISIBLE;
    reset();
    EXPECT(graphic_win_soft_update_2(&G.w), 7);
    expect_log(0, "InvalidateRect", 6, 0x77, 101, 202, 121, 212, 0);
    reset();
    EXPECT(graphic_win_soft_update(&G.w, 5, 6, 30, 30), 7);  // cut to the Buffer
    expect_log(0, "InvalidateRect", 6, 0x77, 106, 208, 121, 212, 0);
    reset();
    EXPECT(graphic_win_soft_update(&G.w, 50, 6, 3, 3), 0), EXPECT(nlog, 0);  // outside it
    reset();
    EXPECT(graphic_win_soft_update_3(&G.w, NULL), 7);
    expect_log(0, "InvalidateRect", 5, 0x77, 101, 202, 121, 212);
    reset();
    EXPECT(graphic_win_soft_update_3(&G.w, (int32_t[4]){2, 3, 8, 30}), 7);
    expect_log(0, "InvalidateRect", 5, 0x77, 103, 205, 109, 212);
    reset();
    EXPECT(graphic_win_soft_update_3(&G.w, (int32_t[4]){-9, -9, -1, -1}), 0), EXPECT(nlog, 0);
    G.w.flags2 = 0;  // hidden: nothing
    reset();
    EXPECT(graphic_win_soft_update_2(&G.w), 0), EXPECT(graphic_win_soft_update(&G.w, 1, 1, 2, 2), 0);
    EXPECT(graphic_win_soft_update_3(&G.w, NULL), 0), EXPECT(nlog, 0);
    EXPECT(graphic_win_update(&G.w, 1, 1, 2, 2, 0), 0), EXPECT(graphic_win_update_2(&G.w, NULL, 0), 0);
    EXPECT(graphic_win_update_3(&G.w, 0), 0), EXPECT(nlog, 0);

    // Showing what was drawn: drawn, taken to the screen and flipped (the screen's draw does nothing with no windows listed).
    G.w.flags2 = WIN_F2_VISIBLE;
    reset();
    flip_text_ = NULL, over_on_ = 0;
    z_n_ = 0;
    EXPECT(graphic_win_update(&G.w, 2, 3, 5, 6, 0), 0);
    blt = -1;
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) blt = i;
    EXPECT(blt >= 0, 1);
    // (the rectangle in the window's client coordinates taken to the screen: 2 and 3 plus 101 and 202)
    expect_log(blt, "BitBlt", 9, 0x1000, 103, 205, 5, 6, 0x6000, 103, 205, 0xcc0020);
    reset();
    EXPECT(graphic_win_update_2(&G.w, (int32_t[4]){1, 1, 9, 9}, 0), 0);
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) expect_log(i, "BitBlt", 9, 0x1000, 102, 203, 8, 8, 0x6000, 102, 203, 0xcc0020);
    reset();
    EXPECT(graphic_win_update_3(&G.w, 0), 0);
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) expect_log(i, "BitBlt", 9, 0x1000, 101, 202, 20, 10, 0x6000, 101, 202, 0xcc0020);
    reset();
    EXPECT(graphic_win_update_2(&G.w, NULL, 0), 0);
    for (int i = 0; i < nlog; i++)
        if (!strcmp(log_[i].name, "BitBlt")) expect_log(i, "BitBlt", 9, 0x1000, 101, 202, 20, 10, 0x6000, 101, 202, 0xcc0020);

    // The screen behind a window and a window drawn into a Buffer: with no windows to draw, what is left is the
    // place the draw was shifted by, the windows' flags and the redirect put back.
    static struct game_buffer D;
    static uint8_t px_d[8];
    make_buffer(&D, px_d, 2, 4, 0xa000);
    memset(&G.w, 0, sizeof G.w);
    G.w.outer[0] = 30, G.w.outer[1] = 40, G.w.inner[0] = 3, G.w.inner[1] = 4, G.w.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
    z_n_ = 0, parent_n_ = 0;
    win_update_window_to_buffer(&G.w, &D);
    EXPECT(G.w.flags2, WIN_F2_VISIBLE | WIN_F2_FRAME), EXPECT(back_alt_ == NULL, 1), EXPECT(opaque_ == NULL, 1);
    EXPECT(back_off_[0], 30 + 3 - 3), EXPECT(back_off_[1], 40 + 4 - 4);  // a frame: the inner rectangle's top left is taken off again
    G.w.flags2 = WIN_F2_VISIBLE;
    win_update_window_to_buffer(&G.w, &D);
    EXPECT(back_off_[0], 33), EXPECT(back_off_[1], 44);
    G.w.flags2 = 0;  // hidden: shown while it is drawn, hidden again
    win_update_window_to_buffer(&G.w, &D);
    EXPECT(G.w.flags2, 0);
    back_off_[0] = back_off_[1] = 99;
    win_update_window_to_buffer(&G.w, NULL);
    EXPECT(back_off_[0], 99);
    // A child hangs from its parent: the parent's client origin on the screen is added.
    static struct game_win PW_;
    memset(&PW_, 0, sizeof PW_);
    PW_.outer[0] = 500, PW_.outer[1] = 600, PW_.inner[0] = 5, PW_.inner[1] = 6;
    G.w.parent = &PW_, G.w.flags = 0x20, G.w.flags2 = WIN_F2_VISIBLE;
    win_update_window_to_buffer(&G.w, &D);
    EXPECT(back_off_[0], 33 + 505), EXPECT(back_off_[1], 44 + 606);
    G.w.flags = 0x20 | 0x8000;  // from its inner rectangle: that is taken off
    win_update_window_to_buffer(&G.w, &D);
    EXPECT(back_off_[0], 33 + 505 - 5), EXPECT(back_off_[1], 44 + 606 - 6);
    G.w.parent = NULL, G.w.flags = 0;
    // The screen behind a window: it is hidden, the Buffer made as big as the window (its frame too), the draw shifted
    // so the window's top left is (0, 0); the Buffer is left alone here, the stand-in for set-up doing nothing.
    G.w.flags2 = WIN_F2_VISIBLE | WIN_F2_FRAME;
    G.w.outer[2] = 60, G.w.outer[3] = 70;
    win_update_back_to_window(&G.w, &D);
    EXPECT(G.w.flags2, WIN_F2_VISIBLE | WIN_F2_FRAME), EXPECT(back_alt_ == NULL, 1);
    EXPECT(back_off_[0], 30), EXPECT(back_off_[1], 40);  // the window's own top left, the inner rectangle's taken off
    EXPECT(D.width, 30), EXPECT(D.neg_height, -30);  // made as big as the window with its frame
    win_update_back_to_window(&G.w, NULL);

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
