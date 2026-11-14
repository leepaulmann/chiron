// Unit test for the Scroll, ListBox and StringBox classes (scroll.c) on hand-built bars, boxes and parents
// with vtables that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/engine/button.h"
#include "../src/engine/game.h"
#include "life_test.h"
#include "../src/engine/scroll.h"
#include "../src/engine/text.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))
#define THIS __attribute__((thiscall))

// The calls the stand-in slots log: slot, object, up to three arguments.
static struct {
    int slot;
    void *o;
    int a, b, c;
} calls[64];
static int ncalls;
static void rec(int slot, void *o, int a, int b, int c) {
    if (ncalls < 64) calls[ncalls].slot = slot, calls[ncalls].o = o, calls[ncalls].a = a, calls[ncalls].b = b, calls[ncalls].c = c;
    ncalls++;
}
static void expect_call(int i, int slot, void *o, int a, int b, int c) {
    char what[48];
    snprintf(what, sizeof what, "call %d", i);
    check_(what, i < ncalls ? calls[i].slot : -1, slot);
    if (i >= ncalls) return;
    check_("  object", (long)calls[i].o, (long)o);
    check_("  a", calls[i].a, a), check_("  b", calls[i].b, b), check_("  c", calls[i].c, c);
}
static int busy, copy_ok;
static int THIS s_0xc(void *o, int a, int b, int c) { return rec(0xc, o, a, b, c), 0; }
static int THIS s_0x10(void *o) { return rec(0x10, o, 0, 0, 0), 0; }
static int THIS s_0x1c(void *o) { return rec(0x1c, o, 0, 0, 0), 0; }
static int THIS s_0x44(void *o, int a, int b) { return rec(0x44, o, a, b, 0), 0; }
static int THIS s_0x50(void *o, int a, int b) { return rec(0x50, o, a, b, 0), 0; }
static int THIS s_0x5c(void *o) { return rec(0x5c, o, 0, 0, 0), busy; }
static int THIS s_0x70(void *o, int a, int b) { return rec(0x70, o, a, b, 0), 0; }
static int THIS s_0xc0(void *o, int a, int b) { return rec(0xc0, o, a, b, 0), 0; }
static int THIS s_0xc4(void *o, int a, int b) { return rec(0xc4, o, a, b, 0), 0; }
static int THIS s_0xd4(void *o, int a, int b) { return rec(0xd4, o, a, b, 0), 0; }
static int THIS s_0xd8(void *o, int a) { return rec(0xd8, o, a, 0, 0), 0; }
static int THIS s_0xdc(void *o, int a, int b) { return rec(0xdc, o, a, b, 0), 0; }
static int THIS s_0xe0(void *o, int a) { return rec(0xe0, o, a, 0, 0), 0; }
static int THIS s_0xf4(void *o) { return rec(0xf4, o, 0, 0, 0), copy_ok; }
static int THIS s_0xf8(void *o) { return rec(0xf8, o, 0, 0, 0), 0; }
static int THIS s_0x160(void *o, int a, int b) { return rec(0x160, o, a, b, 0), 77; }
static void *vt[0x180 / 4];
static void cb_click(int id, int pos) { rec(0x401, NULL, id, pos, 0); }
static void cb_select(int id) { rec(0x403, NULL, id, 0, 0); }
static void cb_double(int id) { rec(0x404, NULL, id, 0, 0); }

static struct game_win *capture, *hover, *cur;
static int32_t by_key, right_flag;
static int fill_rects, fill_color;
static int32_t fill_rect_seen[4];
static void THIS stand_fill_rect(void *w, const int32_t *r, int color) {
    (void)w;
    fill_rects++, fill_color = color;
    memcpy(fill_rect_seen, r, sizeof fill_rect_seen);
}
static int wheel_downs, wheel_ups;
static void THIS stand_wheel_down(void *w, int n) { (void)w, wheel_downs += n; }
static void THIS stand_wheel_up(void *w, int n) { (void)w, wheel_ups += n; }
static int stand_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

static struct game_scroll *B;
static struct game_win P;
static uint8_t sound[16];

static void reset_bar(int w, int h) {
    memset(B, 0, sizeof *B), memset(&P, 0, sizeof P), ncalls = 0, busy = 0;
    B->win.vtable = vt, P.vtable = vt;
    *(void ***)B->btn_a = vt, *(void ***)B->btn_b = vt, *(void ***)sound = vt;
    B->win.parent = &P;
    B->buf.width = w, B->buf.neg_height = -h;
    B->lo = 0, B->hi = 10, B->pos = 5, B->id = 9, B->thickness = 10, B->border = -1, B->drag = -1, B->paging = 3;
    scroll_set_thumb_rect(B);
    *(void **)((uint8_t *)&P + 0x80) = sound;
    cur = capture = hover = NULL, by_key = 0;
}

// A ListBox as the original lays it out: the vbtable pointer, the Win at 0x48 and the Dialog at 0xa60.
static uint8_t lbox[0xb60];
static int32_t vbt[4] = {0, 0x48, 0xa60, 0};
static struct game_dlg_node nodes[4];
static uint8_t items[4][16];
static char *names[4] = {"Alpha", "beta", "Gamma", "delta"};
static struct game_listbox *LB(void) { return (struct game_listbox *)lbox; }
static struct game_dlg *dlg(void) { return (struct game_dlg *)(lbox + 0xa60); }
static uint8_t *lwin(void) { return lbox + 0x48; }
static void make_list(int n) {
    memset(lbox, 0, sizeof lbox), memset(nodes, 0, sizeof nodes), memset(items, 0, sizeof items);
    *(int32_t **)lbox = vbt;
    *(void ***)lwin() = vt;
    for (int i = 0; i < n; i++) {
        nodes[i].key = 100 + i, nodes[i].next = &nodes[(i + 1) % n], nodes[i].prev = &nodes[(i + n - 1) % n];
        memcpy(items[i] + 4, &names[i], sizeof(char *));
        uint8_t *ip = items[i];
        memcpy((uint8_t *)&nodes[i] + 8, &ip, sizeof ip);
    }
    dlg()->head = n ? nodes : NULL, dlg()->cur = n ? nodes : NULL, dlg()->count = n, dlg()->sel = -1;
    *(int32_t *)((uint8_t *)dlg() + 0x54) = 1, *(int32_t *)((uint8_t *)dlg() + 0x58) = 1;
    LB()->visible = 2;
    *(struct game_win **)(lwin() + 0xc4) = &P;
    ncalls = 0;
}

// Drawing: buffers with pixels of their own in static memory.
enum { DW = 40, DH = 12 };
static uint8_t pix[DW * DH + 8], pix_parent[DW * DH + 8];
static void hand_buffer(struct game_buffer *b, uint8_t *pixels) {
    memset(b, 0, sizeof *b);
    memset(pixels, 0, DW * DH + 8);
    b->bits = pixels, b->width = DW, b->neg_height = -DH, b->pitch = DW;
    b->full[2] = DW, b->full[3] = DH, b->clip[2] = DW, b->clip[3] = DH;
}
static struct game_sprite_scratch scratch_;
static int32_t scale[4] = {1, 1, 0, 0}, min_size_[2];
static float ratio_;
static double max_ratio_ = 4.0;
static struct {
    struct game_win win;
    struct game_buffer buf;
} par;
static uint8_t sdata[4] = {3, 9, 3, 9};
static struct game_sprite sp = {.data = sdata, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};

static void draw_tests(void) {
    static struct palette *pal;
    static const uint8_t *tint;
    static uint32_t hwnd = 1;
    game_win_tint = &tint, game_palette_copy = &pal, game_win_hwnd = &hwnd;
    game_sprite_scale = scale, game_sprite_ratio = &ratio_, game_sprite_max_ratio = &max_ratio_, game_sprite_min_size = min_size_;
    game_sprite_scratch = &scratch_;
    static int32_t copy_rem, fill_gap, mask_units, mask_gap_a, mask_gap_b;
    game_copy_rem = &copy_rem, game_fill_gap = &fill_gap, game_mask_units = &mask_units, game_mask_gap_a = &mask_gap_a;
    game_mask_gap_b = &mask_gap_b;
    memset(&par, 0, sizeof par);
    par.win.vtable = vt;
    hand_buffer(&par.buf, pix_parent);
    // A horizontal bar of 40 by 12: the arrows are redrawn, the bar filled with its colour and framed, the thumb
    // (its rectangle worked out) filled, and the grip picture put in the middle of the thumb.
    reset_bar(DW, DH);
    hand_buffer(&B->buf, pix);
    B->win.parent = &par.win, B->back = 7, B->border = 9, B->thickness = 12;
    scroll_set_thumb_rect(B);  // (1, 1, 11, 11)
    B->lo = 0, B->hi = 10, B->pos = 0, B->grip = &sp;
    scroll_on_redraw(B);
    expect_call(0, 0xf8, B->btn_a, 0, 0, 0), expect_call(1, 0xf8, B->btn_b, 0, 0, 0), EXPECT(ncalls, 2);
    EXPECT(pix[0], 9), EXPECT(pix[5 * DW + 20], 7), EXPECT(pix[(DH - 1) * DW + DW - 1], 9);
    // The thumb: (1, 1, 11, 11) normalized to (0, 0, 10, 10), framed to (1, 1, 11, 11), then moved by the
    // position's share (0 here) plus a bar's width and one (the arrow's room): 11 along.
    EXPECT(fill_rects, 1), EXPECT(fill_color, 7);
    EXPECT(fill_rect_seen[0], 1 + 11), EXPECT(fill_rect_seen[1], 1), EXPECT(fill_rect_seen[2], 11 + 11), EXPECT(fill_rect_seen[3], 11);
    // (a 2 by 2 grip, centred in the 10 by 10 thumb: (12 + (10 - 2) / 2, 1 + 4) = (16, 5); 9 is transparent)
    EXPECT(pix[5 * DW + 16], 3), EXPECT(pix[5 * DW + 17], 7), EXPECT(pix[6 * DW + 16], 3);
    EXPECT(B->bar[0], 12), EXPECT(B->bar[2], 22);
}

// ---- ListBox layout and redraw ----

// Text measured by a stand-in (6 pixels a character), on a font 10 high with 8 above the baseline.
static void *l_selected;
static int l_fake_dc;
static void *l_dc_slot, *l_default_font;
static int32_t l_slack;
static struct font lfont;
static void *__attribute__((stdcall)) l_select_object(void *dc, void *obj) {
    void *old = l_selected;
    l_selected = obj;
    return old;
}
static void *__attribute__((stdcall)) l_stock_object(int which) { return (void *)(intptr_t)(0x1000 + which); }
static int __attribute__((stdcall)) l_text_extent(void *dc, const char *s, int n, int32_t *size) {
    size[0] = n * 6, size[1] = 10;
    return 1;
}
static int back_answer, hook_answer;
static int THIS s_0x28(void *o, int win) { return rec(0x28, o, win, 0, 0), back_answer; }
static int __attribute__((cdecl)) back_hook(void *w, int a, int b, int c) { return rec(0x401, w, a, b, c), hook_answer; }
static int item_answer;
static int32_t item_seen[4];
static int THIS s_0x2c(void *o, int win, int id, int state, const int32_t *r) {
    rec(0x2c, o, id, state, 0);
    memcpy(item_seen, r, sizeof item_seen);
    return item_answer;
}
static int __attribute__((cdecl)) item_fn(void *w, int id, int pos, int x, int y, int h) {
    return rec(0x402, w, id, pos, x * 10000 + y * 100 + h), 0;
}
static int stop_after;
static int __attribute__((cdecl)) text_fn(void *w, int id, int pos, const int32_t *r, const char *text) {
    return rec(0x403, w, id, pos, r[0] * 10000 + r[1] * 100 + r[3]), stop_after;
}
static uint8_t lpix[DW * DH + 8];
static int __attribute__((stdcall)) l_intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) return memset(out, 0, 16), 0;
    return memcpy(out, r, 16), 1;
}
static int __attribute__((stdcall)) l_equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static void *l_ddraw;

static void lb_tests(void) {
    game_select_object = l_select_object, game_get_stock_object = l_stock_object, game_get_text_extent = l_text_extent;
    l_dc_slot = &l_fake_dc, game_font_dc = &l_dc_slot;
    l_default_font = &lfont, game_font_default = &l_default_font;
    game_font_slack = &l_slack;
    lfont.first = -1, lfont.hfont = (void *)1, lfont.ascent = 8, lfont.height = 10;
    static int32_t row_counter, bar_width = 7;
    game_listbox_row = &row_counter, game_listbox_bar = &bar_width;
    static int32_t cur_box;
    game_listbox_cur = &cur_box;
    game_intersect_rect = l_intersect, game_equal_rect = l_equal, game_ddraw = &l_ddraw;
    vt[0x28 / 4] = s_0x28, vt[0x2c / 4] = s_0x2c;

    // The layout: the widest text ("Alpha", 5 characters of 6) and the margins make a column; an item is a line
    // high and a gap; the rows are as many as items (at most the rows allowed); the box wants 5 items shown, so
    // it has a bar below (flag 2) which takes its width from the box's.
    make_list(2);
    struct game_dlg *d = dlg();
    uint8_t *lw = lwin();
    struct game_buffer *buf = (struct game_buffer *)(lw + 0x444);
    *(struct font **)((uint8_t *)buf + 0x52c) = &lfont;
    DLG_F(d, 0x70) = DLG_F(d, 0x74) = DLG_F(d, 0x78) = (int32_t)(intptr_t)&lfont;
    DLG_F(d, 0x50) = 5, DLG_F(d, 0x48) = -1, DLG_F(d, 0x44) = 2, DLG_F(d, 0x40) = 1, DLG_F(d, 0x4c) = 1;
    LB()->f2c = 1, LB()->f20 = 5, LB()->flags = 9;
    EXPECT(listbox_calculate_dimensions(LB()), 0);
    EXPECT(DLG_F(d, 0x58), 2), EXPECT(DLG_F(d, 0x68), 8), EXPECT(DLG_F(d, 0x60), 18), EXPECT(DLG_F(d, 0x54), 1);
    EXPECT(DLG_F(d, 0x64), 36), EXPECT(DLG_F(d, 0x5c), 38 + 7), EXPECT(LB()->visible, 2), EXPECT(LB()->flags, 2);
    // A window with the flag that keeps the bar out of it (0x40) doesn't give up room for one.
    make_list(2);
    *(struct font **)((uint8_t *)buf + 0x52c) = &lfont;
    DLG_F(d, 0x70) = DLG_F(d, 0x74) = DLG_F(d, 0x78) = (int32_t)(intptr_t)&lfont;
    DLG_F(d, 0x50) = 5, DLG_F(d, 0x48) = -1, DLG_F(d, 0x44) = 2, DLG_F(d, 0x40) = 1, DLG_F(d, 0x4c) = 1;
    LB()->f2c = 1, LB()->f20 = 5, ((struct game_win *)lw)->flags = 0x40;
    EXPECT(listbox_calculate_dimensions(LB()), 0);
    EXPECT(DLG_F(d, 0x5c), 38), EXPECT(LB()->flags, 2);
    // A height asked for and two columns: the rows are what the height holds, the columns share the width.
    make_list(2);
    *(struct font **)((uint8_t *)buf + 0x52c) = &lfont;
    DLG_F(d, 0x70) = DLG_F(d, 0x74) = DLG_F(d, 0x78) = (int32_t)(intptr_t)&lfont;
    DLG_F(d, 0x20) = 3, DLG_F(d, 0x2c) = 100, DLG_F(d, 0x30) = 40, DLG_F(d, 0x50) = 5, DLG_F(d, 0x48) = 9, DLG_F(d, 0x44) = 2;
    DLG_F(d, 0x40) = 1, DLG_F(d, 0x4c) = 2, DLG_F(d, 0x38) = 3, DLG_F(d, 0x3c) = 4;
    LB()->f2c = 0, LB()->f20 = 1;
    EXPECT(listbox_calculate_dimensions(LB()), 0);
    // (a height of 40 less the margins and plus a gap, over an item 9 high: (40 - 4 - 3 + 2) / 9 = 3 rows)
    EXPECT(DLG_F(d, 0x58), 3), EXPECT(DLG_F(d, 0x54), 2), EXPECT(DLG_F(d, 0x5c), 100), EXPECT(DLG_F(d, 0x60), 40);
    EXPECT(DLG_F(d, 0x68), 7), EXPECT(LB()->visible, 6), EXPECT(LB()->flags, 0);
    EXPECT(DLG_F(d, 0x64), (100 - 2 + 2) / 2 - 2);
    make_list(0);
    EXPECT(listbox_calculate_dimensions(LB()), 7);

    // The redraw: nothing without a parent; the screen's function and then the parent may draw the background
    // (the window is filled with its colour if neither does, or takes the parent's pixels if it can be seen
    // through); then the frame, and the items from the first shown, column by column (each asks the parent
    // for its background: the items here have no text).
    make_list(2);
    d = dlg();
    for (int i = 0; i < 2; i++) memset(items[i] + 4, 0, sizeof(char *));
    memset(&par, 0, sizeof par);
    par.win.vtable = vt;
    hand_buffer(&par.buf, pix_parent);
    for (int i = 0; i < DW * DH; i++) pix_parent[i] = (uint8_t)(i % 100 + 20);
    struct game_win *win = (struct game_win *)lw;
    win->vtable = vt, win->parent = &par.win;
    hand_buffer(buf, lpix);
    *(struct font **)((uint8_t *)buf + 0x52c) = &lfont;
    DLG_F(d, 0x6c) = 7, DLG_F(d, 0xb4) = 1, DLG_F(d, 0xac) = 11, DLG_F(d, 0xb0) = 12, DLG_F(d, 0x54) = 1, DLG_F(d, 0x58) = 2;
    DLG_F(d, 0x40) = 1, DLG_F(d, 0x38) = 1, DLG_F(d, 0x64) = 30, DLG_F(d, 0x68) = 4, DLG_F(d, 0x44) = 1, DLG_F(d, 0x4c) = 2;
    DLG_F(d, 0xf0) = -1;
    item_answer = 1, back_answer = 0, ncalls = 0, cur = NULL;
    listbox_on_redraw(lw);
    EXPECT(cur == &par.win, 1), EXPECT(ncalls, 3), expect_call(0, 0x28, &par.win, (int)(intptr_t)win, 0, 0);
    expect_call(1, 0x2c, &par.win, 100, 0, 0), expect_call(2, 0x2c, &par.win, 101, 0, 0), EXPECT(row_counter, 2);
    // (item 1, in the second row: the gap and the row's height below the top margin)
    EXPECT(item_seen[0], 1), EXPECT(item_seen[1], 6), EXPECT(item_seen[2], 29), EXPECT(item_seen[3], 10);
    EXPECT(lpix[0] == 11 || lpix[0] == 12, 1), EXPECT(lpix[5 * DW + 5], 7), EXPECT(lpix[DW + 1], 7);
    // See-through: the parent's pixels at its place.
    hand_buffer(buf, lpix);
    win->flags = 0x80000;
    listbox_on_redraw(lw);
    EXPECT(lpix[5 * DW + 5], pix_parent[5 * DW + 5]);
    // The parent draws the background: nothing is filled or copied.
    hand_buffer(buf, lpix);
    back_answer = 1, ncalls = 0;
    listbox_on_redraw(lw);
    EXPECT(lpix[5 * DW + 5], 0), EXPECT(ncalls, 3);
    // A function the screen set: told the Win and three zeros first; if it draws, the parent isn't asked.
    LB()->f38 = (void *)back_hook, hook_answer = 1, back_answer = 0, ncalls = 0, win->flags = 0;
    hand_buffer(buf, lpix);
    listbox_on_redraw(lw);
    expect_call(0, 0x401, lw, 0, 0, 0), EXPECT(lpix[5 * DW + 5], 0);
    hook_answer = 0, ncalls = 0;
    listbox_on_redraw(lw);
    expect_call(1, 0x28, &par.win, (int)(intptr_t)win, 0, 0), EXPECT(lpix[5 * DW + 5], 7);
    LB()->f38 = NULL;
    // Two columns of two rows, three items short: the second column keeps asking for items past the end.
    DLG_F(d, 0x54) = 2, ncalls = 0;
    listbox_on_redraw(lw);
    EXPECT(ncalls, 3), EXPECT(row_counter, 4);
    // The first item shown is where the drawing starts.
    DLG_F(d, 0x54) = 1, LB()->top = 1, ncalls = 0;
    listbox_on_redraw(lw);
    EXPECT(ncalls, 2), expect_call(1, 0x2c, &par.win, 101, 0, 0), EXPECT(row_counter, 1);
    // No parent, nothing; no items, just the frame.
    win->parent = NULL, ncalls = 0, cur = NULL;
    listbox_on_redraw(lw);
    EXPECT(ncalls, 0), EXPECT(cur == NULL, 1);
    win->parent = &par.win, d->count = 0, d->head = NULL;
    hand_buffer(buf, lpix);
    listbox_on_redraw(lw);
    EXPECT(lpix[0] == 11 || lpix[0] == 12, 1);

    // Drawing an item: one column takes each item below the rows the ones before it took; a selected item (the
    // Dialog's position, or the item's own flag if the box asks for those) is asked for with state 2, filled
    // if the box has the dialog's focus, and framed; the screen's functions are told.
    make_list(2);
    d = dlg();
    for (int i = 0; i < 2; i++) memset(items[i] + 4, 0, sizeof(char *));
    win = (struct game_win *)lw;
    win->vtable = vt, win->parent = &par.win, win->flags = 0x1000;
    *(struct font **)((uint8_t *)buf + 0x52c) = &lfont;
    DLG_F(d, 0x6c) = 7, DLG_F(d, 0xb4) = 0, DLG_F(d, 0x40) = 1, DLG_F(d, 0x38) = 2, DLG_F(d, 0x64) = 30, DLG_F(d, 0x68) = 4;
    DLG_F(d, 0x44) = 1, DLG_F(d, 0x4c) = 1, DLG_F(d, 0xf0) = 1, DLG_F(d, 0xec) = -1;
    *(int32_t *)(items[0] + 0xc) = 2, *(int32_t *)(items[1] + 0xc) = 1;
    LB()->f10 = -1, LB()->f14 = 9, LB()->f24 = 11, LB()->f28 = 12, LB()->f2c = 2;
    hand_buffer(buf, lpix);
    item_answer = 1, ncalls = 0, fill_rects = 0;
    listbox_draw_item(LB(), 1);
    // (item 0 took two rows of 4 and a gap: item 1 is 5 * 2 below the margin, one row high: 5 - 1 = 4 deep)
    EXPECT(ncalls, 1), expect_call(0, 0x2c, &par.win, 101, 1, 0);
    EXPECT(item_seen[0], 1), EXPECT(item_seen[1], 12), EXPECT(item_seen[2], 29), EXPECT(item_seen[3], 16), EXPECT(fill_rects, 0);
    listbox_draw_item(LB(), 0);
    EXPECT(item_seen[1], 2), EXPECT(item_seen[3], 2 + 5 * 2 - 1), EXPECT(calls[1].b, 0);
    ncalls = 0;
    listbox_draw_item(LB(), 2), listbox_draw_item(LB(), -1);
    EXPECT(ncalls, 0);
    // Selected, the parent leaving the background to the box: the window is filled, the focus fill asked for
    // (the colour 9, the item's rectangle) and the frame drawn, two thick.
    DLG_F(d, 0xec) = 1, item_answer = 0, ncalls = 0, hand_buffer(buf, lpix);
    DLG_F(d, 0x4c) = 2, DLG_F(d, 0x58) = 2, DLG_F(d, 0x38) = 1;
    listbox_draw_item(LB(), 1);
    expect_call(0, 0x2c, &par.win, 101, 2, 0);
    EXPECT(fill_rects, 1), EXPECT(fill_color, 9), EXPECT(fill_rect_seen[0], 1), EXPECT(fill_rect_seen[1], 6), EXPECT(fill_rect_seen[3], 10);
    EXPECT(lpix[6 * DW + 1] == 11 || lpix[6 * DW + 1] == 12, 1), EXPECT(lpix[7 * DW + 3] == 11 || lpix[7 * DW + 3] == 12, 1);
    EXPECT(lpix[7 * DW + 2], 7);  // (the fill shows at the second frame's corner; it is clipped to the item)
    // The box's own flag instead: item 0 is selected if its second field says so.
    LB()->f4 = 1, *(int32_t *)(items[0] + 8) = 1, *(int32_t *)(items[1] + 8) = 0;
    ncalls = 0;
    listbox_draw_item(LB(), 0), listbox_draw_item(LB(), 1);
    EXPECT(calls[0].b, 2), EXPECT(calls[1].b, 1);
    // The functions a screen set: the first is told after the item (the Win, id, position and place), the second
    // too and, if it answers nonzero, draws the rest.
    LB()->f4 = 0, DLG_F(d, 0xec) = -1, row_counter = 3;
    LB()->f30 = (int32_t)(intptr_t)item_fn, LB()->f34 = (int32_t)(intptr_t)text_fn, stop_after = 1, ncalls = 0, fill_rects = 0;
    DLG_F(d, 0xec) = 0;
    listbox_draw_item(LB(), 0);
    EXPECT(ncalls, 3), expect_call(1, 0x402, lw, 100, 3, 10000 + 1 * 100 + 4), expect_call(2, 0x403, lw, 100, 0, 10000 + 1 * 100 + 5);
    EXPECT(*game_listbox_cur == (int32_t)(intptr_t)LB(), 1), EXPECT(fill_rects, 0);
    stop_after = 0, ncalls = 0;
    listbox_draw_item(LB(), 0);
    EXPECT(fill_rects, 1);
    LB()->f30 = LB()->f34 = 0;
    // A function at 0x38 that draws: the parent isn't asked and nothing is filled.
    LB()->f38 = (void *)back_hook, hook_answer = 1, ncalls = 0, fill_rects = 0;
    listbox_draw_item(LB(), 0);
    EXPECT(ncalls, 1), EXPECT(calls[0].slot, 0x401), EXPECT(calls[0].a, 100), EXPECT(calls[0].b, 3), EXPECT(fill_rects, 0);
}

// ---- Life cycle ----

static int life_closes, life_data_closed, life_arrow_closes;
static void LT_THIS life_win_close(void *w) { life_closes++; }
static void LT_THIS life_data_close(void *d) { life_data_closed++; }
static void LT_THIS life_arrow_close(void *b) { life_arrow_closes++; }
static void *life_arrow_vtable[0x180 / 4];

static void test_life(void) {
    life_image_init();
    life_buffer_standins();
    life_time_standins();
    game_win_close = life_win_close, game_dialog_data_close = life_data_close;
    game_dialog_defs_a = game_at(0x697098), game_dialog_defs_b = game_at(0x9b8eb4);
    life_arrow_vtable[0x168 / 4] = (void *)life_arrow_close;
    static uint8_t obj[0x2150];

    // A Scroll: its own vtables and defaults, the arrows' pictures (three numbers each, the second row on
    // starting one word along), and two FlatButtons for the arrows.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(scroll_ctor((struct game_scroll *)obj) == (struct game_scroll *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x669d58)), EXPECT(OW(obj, 0x444), IMGVA(0x669d50)), EXPECT(OW(obj, 0xa14), IMGW(0x9b8de0));
    EXPECT(OW(obj, 0xa1c), IMGW(0x69702c)), EXPECT(OW(obj, 0xa3c), 0xffffffffu), EXPECT(OW(obj, 0xa28), 0), EXPECT(OW(obj, 0xa4c), 0);
    EXPECT(OW(obj, 0xa48), IMGW(0x697034)), EXPECT(OW(obj, 0xa70), IMGW(0x697048)), EXPECT(OW(obj, 0xa74), IMGW(0x9b8e1c));
    EXPECT(OW(obj, 0xa78), IMGW(0x9b8e20)), EXPECT(OW(obj, 0x2144), 0), EXPECT(OW(obj, 0x2148), 0), EXPECT(OW(obj, 0x214c), 0xcccccccc);
    for (int i = 0; i < 3; i++) {
        EXPECT(OW(obj, 0xa7c + 4 * i), IMGW(0x9b8de8 + 4 * i)), EXPECT(OW(obj, 0xa88 + 4 * i), IMGW(0x9b8df4 + 4 * i));
        EXPECT(OW(obj, 0xa94 + 4 * i), IMGW(0x9b8e00 + 4 * i)), EXPECT(OW(obj, 0xaa0 + 4 * i), IMGW(0x9b8e0c + 4 * i));
    }
    EXPECT(OW(obj, 0xaac), IMGVA(0x669754)), EXPECT(OW(obj, 0x15f8), IMGVA(0x669754));  // (the arrows are FlatButtons)
    // Close: the defaults again, each arrow asked to close through its vtable, and the GraphicWin closed.
    OW(obj, 0xa14) = 1, OW(obj, 0x2144) = 5;
    OW(obj, 0xaac) = (uint32_t)(uintptr_t)life_arrow_vtable, OW(obj, 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
    life_closes = 0, life_arrow_closes = 0;
    scroll_close((struct game_scroll *)obj);
    EXPECT(OW(obj, 0xa14), IMGW(0x9b8de0)), EXPECT(OW(obj, 0x2144), 0), EXPECT(life_arrow_closes, 2), EXPECT(life_closes, 1);
    // The destructor unmakes the arrows as the FlatButtons they are, and the GraphicWin.
    life_closes = 0, life_arrow_closes = 0;
    OW(obj, 0xaac) = (uint32_t)(uintptr_t)life_arrow_vtable, OW(obj, 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
    scroll_dtor((struct game_scroll *)obj);
    EXPECT(life_arrow_closes, 2), EXPECT(OW(obj, 0), IMGVA(0x66ff34)), EXPECT(OW(obj, 0xaac), IMGVA(0x66ff34)), EXPECT(OW(obj, 0x15f8), IMGVA(0x66ff34));
    EXPECT(life_closes, 1 + 3 + 3 + 1);  // (the Scroll's close, each arrow's FlatButton close, BaseButton close and Win destructor, the Win's)

    // A ListBox: two virtual bases, the GraphicWin 0x48 in and the Dialog 0xa60 in.
    life_vbtable(0x66b0ec, 0x24, 0), life_vbtable(0x670584, 0x48, 0xa60);
    static uint8_t big[0xb60];
    memset(big, 0xcc, sizeof big);
    EXPECT(listbox_ctor(big, 1) == big, 1);
    EXPECT(OW(big, 0), IMGVA(0x670584)), EXPECT(OW(big, 0x48), IMGVA(0x67041c)), EXPECT(OW(big, 0x48 + 0x444), IMGVA(0x670414));
    EXPECT(OW(big, 0xa60), IMGVA(0x670408)), EXPECT(OW(big, 0x44), 0), EXPECT(OW(big, 0xa5c), 0);
    EXPECT(OW(big, 4), 0), EXPECT(OW(big, 8), 0), EXPECT(OW(big, 0x10), IMGW(0x6970e8)), EXPECT(OW(big, 0x14), IMGW(0x6970ec));
    EXPECT(OW(big, 0x24), IMGW(0x6970e0)), EXPECT(OW(big, 0x28), IMGW(0x9b8ee0)), EXPECT(OW(big, 0x2c), IMGW(0x6970e4)), EXPECT(OW(big, 0x40), 0);
    EXPECT(OW(big, 0xa60 + 0xb4), 1), EXPECT(OW(big, 0xc), 0), EXPECT(OW(big, 0x48), IMGVA(0x67041c));
    OW(big, 0x10) = 9, OW(big, 0x40) = 4, OW(big, 0xa60 + 0xb4) = 6;
    life_closes = 0, life_data_closed = 0;
    listbox_close(big);
    EXPECT(OW(big, 0x10), IMGW(0x6970e8)), EXPECT(OW(big, 0x40), 0), EXPECT(OW(big, 0xa60 + 0xb4), 1), EXPECT(life_closes, 1), EXPECT(life_data_closed, 1);
    // A part of a bigger object leaves the bases to the maker; the destructor, given the GraphicWin, sets the
    // vtables and closes.
    OW(big, 0x48) = 0;
    EXPECT(listbox_ctor(big, 0) == big, 1), EXPECT(OW(big, 0x48), IMGVA(0x67041c));
    OW(big, 0x10) = 9, OW(big, 0x48) = 0;
    life_closes = 0, life_data_closed = 0;
    listbox_dtor(big + 0x48);
    EXPECT(OW(big, 0x48), IMGVA(0x67041c)), EXPECT(OW(big, 0x10), IMGW(0x6970e8)), EXPECT(life_closes, 1), EXPECT(life_data_closed, 1);

    // A StringBox: a GraphicWin with its own fields, a Scroll (0xa20) and a list of links (0x2b70); the constructor starts the fields, close puts
    // them back, closes the GraphicWin, empties the list and closes the Scroll, and the destructor unmakes the list, the Scroll and the GraphicWin.
    {
        static uint8_t sbx[0x2ba0];
        life_vbtable(0x66b0ec, 0x24, 0);
        uint32_t *head = game_at(0x9b3374);
        *head = 0x2468;
        memset(sbx, 0xcc, sizeof sbx);
        EXPECT(stringbox_ctor(sbx) == sbx, 1);
        EXPECT(OW(sbx, 0), IMGVA(0x66adc8)), EXPECT(OW(sbx, 0x444), IMGVA(0x66adc0)), EXPECT(OW(sbx, 0xa18), 0xffffffffu), EXPECT(OW(sbx, 0xa1c), 0);
        EXPECT(OW(sbx, 0xa14), 0), EXPECT(OW(sbx, 0x2b6c), 0), EXPECT(OW(sbx, 0xa20), IMGVA(0x669d58)), EXPECT(OW(sbx, 0x2b70), IMGVA(0x6698c4));
        EXPECT(OW(sbx, 0x2b74), IMGVA(0x66b0ec)), EXPECT(OW(sbx, 0x2b9c), 0x2468), EXPECT(*head, 0), EXPECT(OW(sbx, 0x2b78), 0), EXPECT(OW(sbx, 0x2b88), 0);
        OW(sbx, 0xa14) = 4, OW(sbx, 0xa18) = 5, OW(sbx, 0xa1c) = 6, OW(sbx, 0x2b6c) = 7, OW(sbx, 0x2b84) = 3;
        OW(sbx, 0xa20 + 0xaac) = (uint32_t)(uintptr_t)life_arrow_vtable, OW(sbx, 0xa20 + 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
        life_closes = 0, life_arrow_closes = 0;
        stringbox_close(sbx);
        EXPECT(OW(sbx, 0xa14), 0), EXPECT(OW(sbx, 0xa18), 0xffffffffu), EXPECT(OW(sbx, 0xa1c), 0), EXPECT(OW(sbx, 0x2b6c), 0), EXPECT(OW(sbx, 0x2b84), 0);
        EXPECT(life_closes, 2), EXPECT(life_arrow_closes, 2);
        OW(sbx, 0) = 0, OW(sbx, 0xa20) = 0, OW(sbx, 0x2b9c) = 0x1357;
        stringbox_dtor(sbx);
        EXPECT(OW(sbx, 0), IMGVA(0x66ff34)), EXPECT(OW(sbx, 0xa20), IMGVA(0x66ff34)), EXPECT(*head, 0x1357), EXPECT(OW(sbx, 0x2b70), IMGVA(0x6693a4));
        EXPECT(OW(sbx, 0x2b98), IMGVA(0x6693ac));
        // The scalar deleting destructors of StringBox and Scroll (the arrows of the Scroll close through their vtable).
        void *o = calloc(1, 0x2ba0);
        stringbox_ctor(o);
        OW(o, 0xa20 + 0xaac) = OW(o, 0xa20 + 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
        life_freed = 0;
        EXPECT(stringbox_scalar_dtor(o, 1) == o, 1), EXPECT(life_freed_has(o), 1);
        o = calloc(1, 0x2ba0);
        OW(o, 0xaac) = OW(o, 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
        life_freed = 0;
        EXPECT(scroll_scalar_dtor(o, 1) == o, 1), EXPECT(life_freed_has(o), 1);
        o = calloc(1, 0x2ba0);
        OW(o, 0xaac) = OW(o, 0x15f8) = (uint32_t)(uintptr_t)life_arrow_vtable;
        life_freed = 0;
        EXPECT(scroll_scalar_dtor(o, 0) == o, 1), EXPECT(life_freed_has(o), 0);
        free(o);
    }

}

int main(void) {
    static struct game_scroll scroll_b;
    B = &scroll_b;
    game_win_capture = &capture, game_win_hover = &hover, game_win_cur = &cur;
    game_scroll_by_key = &by_key, game_listbox_right = &right_flag, game_graphic_win_fill_rect = stand_fill_rect;
    game_win_wheel_down = stand_wheel_down, game_win_wheel_up = stand_wheel_up, game_stricmp = stand_stricmp;
    vt[0xc / 4] = s_0xc, vt[0x10 / 4] = s_0x10, vt[0x1c / 4] = s_0x1c, vt[0x44 / 4] = s_0x44, vt[0x50 / 4] = s_0x50;
    vt[0x5c / 4] = s_0x5c, vt[0x70 / 4] = s_0x70, vt[0xc0 / 4] = s_0xc0, vt[0xc4 / 4] = s_0xc4, vt[0xd4 / 4] = s_0xd4;
    vt[0xd8 / 4] = s_0xd8, vt[0xdc / 4] = s_0xdc, vt[0xe0 / 4] = s_0xe0, vt[0xf4 / 4] = s_0xf4, vt[0xf8 / 4] = s_0xf8;
    vt[0x160 / 4] = s_0x160;

    // The range: low to high, or inverted when given high to low (or equal); the position starts at the low end.
    reset_bar(40, 10);
    scroll_set_range(B, 3, 20);
    EXPECT(B->lo, 3), EXPECT(B->hi, 20), EXPECT(B->pos, 3), EXPECT(B->inverted, 0), EXPECT(ncalls, 1);
    expect_call(0, 0xf8, B, 0, 0, 0);
    scroll_set_range(B, 20, 3);
    EXPECT(B->lo, 3), EXPECT(B->hi, 20), EXPECT(B->pos, 3), EXPECT(B->inverted, 1);
    scroll_set_range(B, 5, 5);
    EXPECT(B->lo, 5), EXPECT(B->hi, 5), EXPECT(B->inverted, 1);

    // The position is kept in range, mirrored for an inverted bar, and needs a parent.
    reset_bar(40, 10);
    scroll_set_pos(B, 7);
    EXPECT(B->pos, 7), EXPECT(cur == &P, 1), EXPECT(ncalls, 1);
    scroll_set_pos(B, -4), EXPECT(B->pos, 0);
    scroll_set_pos(B, 99), EXPECT(B->pos, 10);
    B->inverted = 1, scroll_set_pos(B, 3), EXPECT(B->pos, 7);
    B->win.parent = NULL, cur = NULL, ncalls = 0;
    scroll_set_pos(B, 1);
    EXPECT(B->pos, 7), EXPECT(ncalls, 0), EXPECT(cur == NULL, 1);

    // The arrows' colours and bevel go to the bar and both arrows, which are redrawn.
    reset_bar(40, 10);
    scroll_set_button_color(B, 33);
    EXPECT(B->button_color, 33), EXPECT(*(int32_t *)(B->btn_a + 0xa84), 33), EXPECT(*(int32_t *)(B->btn_b + 0xa84), 33);
    EXPECT(ncalls, 2), expect_call(0, 0xf8, B->btn_a, 0, 0, 0), expect_call(1, 0xf8, B->btn_b, 0, 0, 0);
    scroll_set_bevel_thickness(B, 2), scroll_set_bevel_upper(B, 5), scroll_set_bevel_lower(B, 6);
    EXPECT(B->bevel, 2), EXPECT(*(int32_t *)(B->btn_b + 0xa90), 2);
    EXPECT(B->bevel_upper, 5), EXPECT(*(int32_t *)(B->btn_a + 0xa88), 5);
    EXPECT(B->bevel_lower, 6), EXPECT(*(int32_t *)(B->btn_b + 0xa8c), 6);
    EXPECT(ncalls, 8);

    // The thumb's rectangle: a square of the bar's width, less a line each side with a frame.
    reset_bar(40, 10);
    scroll_set_bar_thickness(B, 8);
    EXPECT(B->thickness, 8), EXPECT(B->bar[2], 8), EXPECT(B->bar[0], 0);
    scroll_set_border_color(B, 4);
    EXPECT(B->border, 4), EXPECT(B->bar[0], 1), EXPECT(B->bar[2], 7), EXPECT(B->bar[3], 7);
    scroll_set_border_color(B, -1), EXPECT(B->bar[0], 0), EXPECT(B->bar[2], 8);

    // The arrows' pictures: kept in the bar's four slots; the left and right ones reach the arrows on a wide bar,
    // the up and down ones on a narrow one (and not on a square one).
    reset_bar(40, 10);
    scroll_set_sprite_left(B, 1, 2, 3), scroll_set_sprite_right(B, 4, 5, 6), scroll_set_sprite_up(B, 7, 8, 9), scroll_set_sprite_down(B, 10, 11, 12);
    EXPECT(B->spr[0][1], 2), EXPECT(B->spr[2][2], 6), EXPECT(B->spr[1][0], 7), EXPECT(B->spr[3][1], 11);
    EXPECT(*(int32_t *)(B->btn_a + 0xb14), 2), EXPECT(*(int32_t *)(B->btn_b + 0xb18), 6);
    EXPECT(*(int32_t *)(B->btn_a + 0xb10), 1), EXPECT(*(int32_t *)(B->btn_b + 0xb10), 4);
    reset_bar(10, 40);
    scroll_set_sprite_left(B, 1, 2, 3), scroll_set_sprite_up(B, 7, 8, 9), scroll_set_sprite_down(B, 10, 11, 12);
    EXPECT(*(int32_t *)(B->btn_a + 0xb10), 7), EXPECT(*(int32_t *)(B->btn_b + 0xb14), 11);
    reset_bar(10, 10);
    scroll_set_sprite_up(B, 7, 8, 9), scroll_set_sprite_left(B, 1, 2, 3);
    EXPECT(*(int32_t *)(B->btn_a + 0xb10), 0);

    // The thumb. A wide bar of 40, thumb 10, position 5 of 0..10: the room is 40 - 31 = 9, so the thumb has
    // moved 5 * 9 / 10 = 4 and the arrow's width and one (11): from 15 to 25.
    int32_t r[4];
    reset_bar(40, 10);
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 15), EXPECT(r[1], 0), EXPECT(r[2], 25), EXPECT(r[3], 10), EXPECT(B->bar[0], 15), EXPECT(B->bar[2], 25);
    // (flag 2: no arrow room, the thumb is a width wide: 5 * 29 / 10 = 14)
    reset_bar(40, 10), B->flags = 2;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 14), EXPECT(r[2], 24);
    // A narrow bar the same way along y.
    reset_bar(10, 40);
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 0), EXPECT(r[1], 15), EXPECT(r[2], 10), EXPECT(r[3], 25);
    // With a frame (thumb 1, 1 to 9, 9): the room is 40 - 27 = 13, so 5 * 13 / 10 = 6 and 9.
    reset_bar(40, 10), B->border = 3, scroll_set_thumb_rect(B);
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 16), EXPECT(r[1], 1), EXPECT(r[2], 24), EXPECT(r[3], 9);
    // Held by the mouse (the offset in the bar): the thumb follows it, a thumb's width from each end.
    reset_bar(40, 10), B->drag = 7;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 12), EXPECT(r[2], 22), EXPECT(B->drag, -1);
    reset_bar(40, 10), B->drag = 0;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 10);
    reset_bar(40, 10), B->drag = 100;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 20), EXPECT(r[2], 30);
    reset_bar(40, 10), B->drag = 100, B->flags = 2;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 30), EXPECT(r[2], 40);
    reset_bar(10, 40), B->drag = 7;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[1], 12), EXPECT(r[3], 22), EXPECT(r[0], 0);
    reset_bar(40, 10), B->drag = 3, B->flags = 2;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 8);  // 5 + 3
    reset_bar(40, 10), B->drag = -300, B->flags = 2;
    scroll_compute_thumb_rect(B, r);
    EXPECT(r[0], 0);

    // The mouse goes down: beside the thumb a track click starts (as a move); on it, nothing but a hold; the bar
    // takes the capture if nothing has it.
    reset_bar(40, 10);
    scroll_compute_thumb_rect(B, r);  // thumb at 15..25
    B->press_time = 99;
    scroll_on_left_down(B, 3, 4);
    EXPECT(B->dir, -1), EXPECT(B->press_time, 0), EXPECT(capture == &B->win, 1), EXPECT(hover == NULL, 1);
    expect_call(0, 0x44, B, 3, 4, 0), EXPECT(ncalls, 1);
    ncalls = 0, capture = NULL, hover = &P;
    scroll_on_left_down(B, 30, 4);
    EXPECT(B->dir, 1), expect_call(0, 0x44, B, 30, 4, 0);
    ncalls = 0, capture = &P;
    scroll_on_left_down(B, 20, 4);
    EXPECT(B->dir, 0), EXPECT(ncalls, 0), EXPECT(capture == &P, 1);
    reset_bar(10, 40);
    scroll_compute_thumb_rect(B, r);  // 15..25 along y
    scroll_on_left_down(B, 3, 30);
    EXPECT(B->dir, 1), expect_call(0, 0x44, B, 3, 30, 0);
    B->win.parent = NULL, B->dir = 5, ncalls = 0;
    scroll_on_left_down(B, 3, 30);
    EXPECT(B->dir, 5), EXPECT(ncalls, 0);
    // ... and up: redraw, and capture and hover let go.
    reset_bar(40, 10);
    capture = &B->win, hover = &B->win;
    scroll_on_left_up(B, 0, 0);
    EXPECT(capture == NULL, 1), EXPECT(hover == NULL, 1), EXPECT(ncalls, 2);
    expect_call(0, 0xf8, B, 0, 0, 0), expect_call(1, 0x10, B, 0, 0, 0);

    // The wheel: by one in range, the parent, the function and the sound told with `notify`, the hold let go.
    reset_bar(40, 10);
    B->on_click = cb_click, B->drag = 4;
    scroll_on_mousewheel_down(B, 1);
    EXPECT(B->pos, 6), EXPECT(cur == &P, 1), EXPECT(B->drag, -1);
    expect_call(0, 0xc0, &P, 9, 6, 0), expect_call(1, 0x401, NULL, 9, 6, 0), expect_call(2, 0x5c, sound, 0, 0, 0);
    expect_call(3, 0x1c, sound, 0, 0, 0), expect_call(4, 0xf8, B, 0, 0, 0), EXPECT(ncalls, 5);
    ncalls = 0, busy = 1;
    scroll_on_mousewheel_up(B, 1);
    EXPECT(B->pos, 5), expect_call(0, 0xc0, &P, 9, 5, 0), expect_call(1, 0x401, NULL, 9, 5, 0), expect_call(2, 0x5c, sound, 0, 0, 0);
    expect_call(3, 0xf8, B, 0, 0, 0), EXPECT(ncalls, 4);
    ncalls = 0;
    scroll_on_mousewheel_up(B, 0);
    EXPECT(B->pos, 4), EXPECT(ncalls, 1);
    B->pos = 10, scroll_on_mousewheel_down(B, 0), EXPECT(B->pos, 10);
    B->pos = 0, scroll_on_mousewheel_up(B, 0), EXPECT(B->pos, 0);
    // An inverted bar keeps the mirror and tells the parent the position itself.
    reset_bar(40, 10);
    B->inverted = 1;
    scroll_on_mousewheel_down(B, 1);
    EXPECT(B->pos, 4), expect_call(0, 0xc0, &P, 9, 6, 0);
    reset_bar(40, 10);
    B->win.parent = NULL;
    scroll_on_mousewheel_down(B, 1);
    EXPECT(B->pos, 5), EXPECT(ncalls, 0);

    // Keys: End and Home on a wide bar, Page Down and Page Up on a narrow one; other keys are not handled.
    reset_bar(40, 10);
    EXPECT(scroll_on_key_click(B, 0, 0x23), 1), EXPECT(B->pos, 10), EXPECT(by_key, 1);
    expect_call(0, 0xc0, &P, 9, 10, 0);
    EXPECT(scroll_on_key_click(B, 0, 0x24), 1), EXPECT(B->pos, 0);
    ncalls = 0;
    EXPECT(scroll_on_key_click(B, 0, 0x22), 0), EXPECT(scroll_on_key_click(B, 0, 0x41), 0), EXPECT(ncalls, 0);
    reset_bar(10, 40);
    EXPECT(scroll_on_key_click(B, 0, 0x22), 1), EXPECT(B->pos, 8);
    EXPECT(scroll_on_key_click(B, 0, 0x22), 1), EXPECT(scroll_on_key_click(B, 0, 0x22), 1), EXPECT(B->pos, 10);
    EXPECT(scroll_on_key_click(B, 0, 0x21), 1), EXPECT(B->pos, 7);
    B->pos = 1, scroll_on_key_click(B, 0, 0x21), EXPECT(B->pos, 0);
    B->inverted = 1, B->pos = 2, ncalls = 0, scroll_on_key_click(B, 0, 0x22), EXPECT(B->pos, 5);
    expect_call(0, 0xc0, &P, 9, 5, 0);  // (the mirror of 5 in 0..10)
    B->win.parent = NULL, by_key = 0;
    EXPECT(scroll_on_key_click(B, 0, 0x22), 0), EXPECT(by_key, 0);

    // The arrows: one step down or up, within the range; the press time is cleared and the key flag lowered.
    reset_bar(40, 10);
    B->press_time = 5, by_key = 1, B->on_click = cb_click;
    scroll_on_button_clicked(B, 1);
    EXPECT(B->pos, 4), EXPECT(B->press_time, 0), EXPECT(by_key, 0), expect_call(0, 0xc0, &P, 9, 4, 0), expect_call(1, 0x401, NULL, 9, 4, 0);
    scroll_on_button_clicked(B, 2), scroll_on_button_clicked(B, 2), EXPECT(B->pos, 6);
    B->pos = 10, scroll_on_button_clicked(B, 2), EXPECT(B->pos, 10);
    B->pos = 0, scroll_on_button_clicked(B, 1), EXPECT(B->pos, 0);
    ncalls = 0;
    scroll_on_button_clicked(B, 7);
    EXPECT(B->pos, 0), EXPECT(ncalls > 0, 1);

    // A resize: the bar is as wide as the smaller number, the second arrow is sized to it and put at the far end.
    reset_bar(40, 10);
    EXPECT(scroll_on_resize(B, 50, 12), 0);
    EXPECT(B->thickness, 12), EXPECT(B->bar[2], 12), EXPECT(B->bar[3], 12);
    expect_call(0, 0xc, B->btn_b, 12, 12, 0);
    EXPECT(*(int32_t *)(B->btn_b + 0x13c), 38), EXPECT(*(int32_t *)(B->btn_b + 0x140), 0);  // (the Win's inner left and top)
    scroll_on_resize(B, 12, 50);
    EXPECT(*(int32_t *)(B->btn_b + 0x13c), 0), EXPECT(*(int32_t *)(B->btn_b + 0x140), 38);
    B->border = 5;
    scroll_on_resize(B, 50, 12);
    EXPECT(B->bar[0], 1), EXPECT(B->bar[2], 11), EXPECT(calls[ncalls - 1].a, 10);
    EXPECT(*(int32_t *)(B->btn_b + 0x13c), 39), EXPECT(*(int32_t *)(B->btn_b + 0x140), 1);
    scroll_on_resize(B, 12, 50);
    EXPECT(*(int32_t *)(B->btn_b + 0x13c), 1), EXPECT(*(int32_t *)(B->btn_b + 0x140), 39);

    // The ListBox. Selecting a position scrolls the first item shown to it, calls the function a screen set with
    // the item's id, and tells the parent twice and plays the sound.
    make_list(4);
    LB()->on_select = cb_select, *(void **)((uint8_t *)&P + 0x90) = sound, P.vtable = vt;
    listbox_set_selected_pos(LB(), 3, 1);
    EXPECT(dlg()->sel, 3), EXPECT(LB()->top, 2);
    expect_call(0, 0x403, NULL, 103, 0, 0), expect_call(1, 0xd8, &P, 103, 0, 0), expect_call(2, 0xd4, &P, 103, 0, 0);
    expect_call(3, 0x5c, sound, 0, 0, 0), expect_call(4, 0x1c, sound, 0, 0, 0), expect_call(5, 0xf8, lwin(), 0, 0, 0), EXPECT(ncalls, 6);
    ncalls = 0;
    listbox_set_selected_pos(LB(), 3, 1);
    EXPECT(ncalls, 0);
    LB()->force = 1;
    listbox_set_selected_pos(LB(), 3, 0);
    EXPECT(ncalls, 2), expect_call(0, 0x403, NULL, 103, 0, 0), expect_call(1, 0xf8, lwin(), 0, 0, 0);
    LB()->force = 0, ncalls = 0;
    listbox_set_selected_pos(LB(), 0, 0);
    EXPECT(LB()->top, 0), EXPECT(dlg()->sel, 0), EXPECT(ncalls, 2);
    listbox_set_selected_pos(LB(), -1, 1);
    EXPECT(dlg()->sel, -1), EXPECT(ncalls, 3), expect_call(2, 0xf8, lwin(), 0, 0, 0);
    // With more room than items the first shown stays at 0; a column list steps by a column.
    LB()->visible = 9, LB()->top = 3, listbox_set_selected_pos(LB(), 1, 0);
    EXPECT(LB()->top, 0);
    LB()->visible = 1, LB()->top = 0, *(int32_t *)((uint8_t *)dlg() + 0x54) = 2, *(int32_t *)((uint8_t *)dlg() + 0x58) = 2;
    listbox_set_selected_pos(LB(), 3, 0);
    EXPECT(LB()->top, 4);
    // By id: found, or none.
    make_list(4);
    listbox_set_selected_id(LB(), 102);
    EXPECT(dlg()->sel, 2), EXPECT(LB()->top, 1);
    listbox_set_selected_id(LB(), 55);
    EXPECT(dlg()->sel, -1);
    make_list(0);
    dlg()->sel = 2;
    listbox_set_selected_id(LB(), 1);
    EXPECT(dlg()->sel, -1), EXPECT(ncalls, 1);
    // The search by text, ignoring case, from the item after the cursor; the cursor is put back.
    make_list(4);
    dlg()->cur = &nodes[1], dlg()->idx = 1;
    EXPECT(listbox_find(LB(), "GAMMA"), 2), EXPECT(dlg()->cur == &nodes[1], 1), EXPECT(dlg()->idx, 1);
    EXPECT(listbox_find(LB(), "alpha"), 0), EXPECT(listbox_find(LB(), "nope"), -1), EXPECT(listbox_find(LB(), NULL), -1);
    EXPECT(listbox_find(LB(), "beta"), 1);  // (it looks at every item, the cursor's last)
    dlg()->idx = 3, dlg()->cur = &nodes[3];
    EXPECT(listbox_find(LB(), "Alpha"), 0), EXPECT(dlg()->cur == &nodes[3], 1), EXPECT(dlg()->idx, 3);
    dlg()->idx = -1, dlg()->cur = &nodes[3];
    EXPECT(listbox_find(LB(), "Delta"), 3), EXPECT(dlg()->cur == &nodes[3], 1), EXPECT(dlg()->idx, 3);
    make_list(0);
    EXPECT(listbox_find(LB(), "x"), -1);

    // Handlers the Win's vtable holds get the Win (0x48 into the box).
    make_list(3);
    listbox_on_scrolled(lwin(), 5, 6);
    expect_call(0, 0xc4, lwin(), 5, 6, 0);
    ncalls = 0, right_flag = 0;
    listbox_on_right_down(lwin(), 7, 8);
    expect_call(0, 0x50, lwin(), 7, 8, 0), EXPECT(right_flag, 0);
    listbox_on_right_double_click(lwin(), 9, 1);
    expect_call(1, 0x70, lwin(), 9, 1, 0);
    ncalls = 0, dlg()->sel = 1, LB()->on_double = cb_double, *(void **)((uint8_t *)&P + 0x94) = sound, right_flag = 1;
    listbox_on_left_double_click(lwin(), 0, 0);
    EXPECT(cur == &P, 1);
    expect_call(0, 0x404, NULL, 101, 0, 0), expect_call(1, 0xe0, &P, 101, 0, 0), expect_call(2, 0xdc, &P, 101, 1, 0);
    expect_call(3, 0x5c, sound, 0, 0, 0), expect_call(4, 0x1c, sound, 0, 0, 0), EXPECT(ncalls, 5);
    ncalls = 0, dlg()->hover = 4;
    listbox_on_mouse_leave(lwin(), 0, 0);
    EXPECT(dlg()->hover, -1), EXPECT(ncalls, 1);
    wheel_downs = wheel_ups = 0;
    listbox_on_mousewheel(lwin(), -3), EXPECT(wheel_downs, 3), EXPECT(wheel_ups, 0);
    listbox_on_mousewheel(lwin(), 2), EXPECT(wheel_ups, 2);
    listbox_on_mousewheel(lwin(), 0), EXPECT(wheel_downs + wheel_ups, 5);

    // StringBox: the offset and a redraw; a link goes to a parent that allows it, which answers.
    static struct game_stringbox sb;
    memset(&sb, 0, sizeof sb);
    sb.win.vtable = vt, sb.win.parent = &P, ncalls = 0;
    stringbox_on_scrolled(&sb, 1, 42);
    EXPECT(sb.scroll, 42), expect_call(0, 0xf8, &sb, 0, 0, 0);
    ncalls = 0, copy_ok = 1;
    EXPECT(stringbox_on_link_clicked(&sb, 3, 4), 77), expect_call(0, 0xf4, &P, 0, 0, 0), expect_call(1, 0x160, &P, 3, 4, 0);
    ncalls = 0, copy_ok = 0;
    EXPECT(stringbox_on_link_clicked(&sb, 3, 4), 1), EXPECT(ncalls, 1);
    sb.win.parent = NULL, ncalls = 0;
    EXPECT(stringbox_on_link_clicked(&sb, 3, 4), 1), EXPECT(ncalls, 0);

    draw_tests();
    lb_tests();
    test_life();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
