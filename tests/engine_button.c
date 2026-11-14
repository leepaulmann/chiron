// Unit test for the button classes (button.c) on hand-built buttons, groups, boxes and parents with
// vtables that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/button.h"
#include "../src/engine/game.h"
#include "life_test.h"
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
static int busy;
static int THIS s_0x10(void *o) { return rec(0x10, o, 0, 0, 0), 0; }
static int THIS s_0x1c(void *o) { return rec(0x1c, o, 0, 0, 0), 0; }
static int THIS s_0x44(void *o, int a, int b) { return rec(0x44, o, a, b, 0), 0; }
static int THIS s_0x5c(void *o) { return rec(0x5c, o, 0, 0, 0), busy; }
static int THIS s_0xb0(void *o, int a) { return rec(0xb0, o, a, 0, 0), 0; }
static int THIS s_0xb4(void *o, int a, int b) { return rec(0xb4, o, a, b, 0), 0; }
static int THIS s_0xb8(void *o, int a, int b, int c) { return rec(0xb8, o, a, b, c), 0; }
static int THIS s_0xbc(void *o, int a, int b) { return rec(0xbc, o, a, b, 0), 0; }
static int THIS s_0xf8(void *o) { return rec(0xf8, o, 0, 0, 0), 0; }
static int THIS s_0xf4(void *o) { return rec(0xf4, o, 0, 0, 0), 1; }
static void *vt[0x180 / 4];
static void cb_click(int id) { rec(0x401, NULL, id, 0, 0); }
static void cb_hook(void) { rec(0x403, NULL, 0, 0, 0); }

static struct game_win *capture, *hover, *press1, *press2, *cur;
static int32_t inside;
static void(__attribute__((cdecl)) * hooks[2])(void);
static struct game_button_defaults defaults;
static struct game_button_fonts fonts;
static struct game_button B[3];
static struct game_win P;
static uint8_t sound[16];
static struct game_button_group G;

static void reset(void) {
    memset(B, 0, sizeof B), memset(&P, 0, sizeof P), memset(&G, 0, sizeof G), ncalls = 0, busy = 0;
    P.vtable = vt;
    for (int i = 0; i < 3; i++) B[i].win.vtable = vt, B[i].win.parent = &P, B[i].id = 10 + i;
    *(void ***)sound = vt;
}

// A CheckBox laid out as the original does: a vbtable pointer, the Win at 0x1c and the Dialog at 0xa34.
static uint8_t box[0xb40];
static int32_t vbt[4] = {0, 0x1c, 0xa34, 0};
static struct game_dlg_node nodes[4];
static struct game_dlg *dlg(void) { return (struct game_dlg *)(box + 0xa34); }
static void make_box(int n) {
    memset(box, 0, sizeof box), memset(nodes, 0, sizeof nodes);
    *(int32_t **)box = vbt;
    *(void ***)(box + 0x1c) = vt;
    for (int i = 0; i < n; i++) nodes[i].key = 100 + i, nodes[i].next = &nodes[(i + 1) % n], nodes[i].prev = &nodes[(i + n - 1) % n];
    dlg()->head = n ? nodes : NULL, dlg()->cur = n ? nodes : NULL, dlg()->count = n;
}

// Drawing: buffers with pixels of their own in static memory.
enum { DW = 12, DH = 8 };
static uint8_t pix_own[DW * DH + 8], pix_parent[DW * DH + 8];
static void hand_buffer(struct game_buffer *b, uint8_t *pixels) {
    memset(b, 0, sizeof *b);
    if (pixels != pix_parent) memset(pixels, 0, DW * DH + 8);
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
static uint8_t sdata_off[4] = {3, 9, 3, 9}, sdata_on[4] = {4, 4, 9, 9};
static struct game_sprite sp_off = {.data = sdata_off, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};
static struct game_sprite sp_on = {.data = sdata_on, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};

static void draw_tests(void) {
    static struct game_button b;
    static struct game_check_button c;
    static struct palette *pal;
    static const uint8_t *tint;
    static uint32_t hwnd = 1;
    game_win_tint = &tint, game_palette_copy = &pal, game_win_hwnd = &hwnd;
    game_sprite_scale = scale, game_sprite_ratio = &ratio_, game_sprite_max_ratio = &max_ratio_, game_sprite_min_size = min_size_;
    game_sprite_scratch = &scratch_;
    static int32_t copy_rem, fill_gap, mask_units, mask_gap_a, mask_gap_b;
    game_copy_rem = &copy_rem, game_fill_gap = &fill_gap, game_mask_units = &mask_units, game_mask_gap_a = &mask_gap_a;
    game_mask_gap_b = &mask_gap_b;
    memset(&par, 0, sizeof par), memset(&b, 0, sizeof b), memset(&c, 0, sizeof c);
    par.win.vtable = vt;
    hand_buffer(&par.buf, pix_parent);
    for (int i = 0; i < DW * DH; i++) pix_parent[i] = (uint8_t)(i + 20);
    // GraphicWin_fill: a plain window is filled with the colour.
    hand_buffer(&b.buf, pix_own);
    b.win.vtable = vt, b.win.parent = &par.win;
    graphic_win_fill(&b.win, 7);
    int all = 1;
    for (int i = 0; i < DW * DH; i++) all &= pix_own[i] == 7;
    EXPECT(all, 1);
    // A see-through one takes its parent's pixels at its place, if the parent allows it (slot 0xf4).
    b.win.flags = 0x80000, b.win.outer[0] = 2, b.win.outer[1] = 1;
    hand_buffer(&b.buf, pix_own);
    ncalls = 0;
    graphic_win_fill(&b.win, 7);
    expect_call(0, 0xf4, &par.win, 0, 0, 0);
    EXPECT(pix_own[0], pix_parent[1 * DW + 2]), EXPECT(pix_own[DW + 1], pix_parent[2 * DW + 3]);
    // ... darkened by the table, if there is one.
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)(255 - i);
    tint = table;
    hand_buffer(&b.buf, pix_own);
    graphic_win_fill(&b.win, 7);
    EXPECT(pix_own[0], 255 - pix_parent[1 * DW + 2]);
    tint = NULL;
    // An ImageButton: the fill, the border round it, the picture centred, and nothing with no parent or window.
    hand_buffer(&b.buf, pix_own);
    b.win.flags = 0, b.fill = 7, b.focus_a = -1, b.border_a = 11, b.border_b = 12, b.border_w = 1, b.picture = &sp_off;
    image_button_on_redraw(&b);
    EXPECT(cur == &par.win, 1);
    EXPECT(pix_own[3 * DW + 5], 7), EXPECT(pix_own[0] == 11 || pix_own[0] == 12, 1);
    // (the 2 by 2 picture: offset ((12 - 2 - 1) >> 1, (8 - 2 - 1) >> 1) = (4, 2))
    EXPECT(pix_own[2 * DW + 4], 3), EXPECT(pix_own[2 * DW + 5], 7), EXPECT(pix_own[3 * DW + 4], 3);
    cur = NULL, hwnd = 0, hand_buffer(&b.buf, pix_own);
    image_button_on_redraw(&b);
    EXPECT(cur == NULL, 1), EXPECT(pix_own[0], 0);
    hwnd = 1, b.win.parent = NULL;
    image_button_on_redraw(&b);
    EXPECT(pix_own[0], 0);
    // A CheckButton: the parent's pixels at its place (here the same), then the picture for the state.
    c.win.vtable = vt, c.win.parent = &par.win, c.pic_off = &sp_off, c.pic_on = &sp_on;
    hand_buffer(&c.buf, pix_own);
    checkbtn_on_redraw(&c);
    EXPECT(pix_own[0], 3), EXPECT(pix_own[1], pix_parent[1]), EXPECT(pix_own[DW], 3);
    c.state = 1, hand_buffer(&c.buf, pix_own);
    checkbtn_on_redraw(&c);
    EXPECT(pix_own[0], 4), EXPECT(pix_own[1], 4), EXPECT(pix_own[DW], pix_parent[DW]);
}

#define win_buf_(w) ((struct game_buffer *)((uint8_t *)(w) + 0x444))

// ---- FlatButton, CheckBox and RadioButton layout and drawing ----

// Text measured by a stand-in (6 pixels a character), on a font 10 high with 8 above the baseline.
static void *selected_font;
static int fake_dc_;
static void *dc_slot_, *default_font_slot_;
static int32_t slack_;
static void *__attribute__((stdcall)) t_select_object(void *dc, void *obj) {
    void *old = selected_font;
    selected_font = obj;
    return old;
}
static void *__attribute__((stdcall)) t_stock_object(int which) { return (void *)(intptr_t)(0x1000 + which); }
static int __attribute__((stdcall)) t_text_extent(void *dc, const char *s, int n, int32_t *size) {
    size[0] = n * 6, size[1] = 10;
    return 1;
}
static struct font tfont;

// The slots a box's parent has: the background of the box and of an item (answering as `back_answer` and
// `item_answer`) and the text of a FlatButton.
static int back_answer, item_answer, flat_answer;
static int32_t item_rect[4];
static int THIS s_0x28(void *o, int win) { return rec(0x28, o, win, 0, 0), back_answer; }
static int THIS s_0x2c(void *o, int win, int id, int state, const int32_t *r) {
    rec(0x2c, o, id, state, 0);
    memcpy(item_rect, r, sizeof item_rect);
    return item_answer;
}
static int THIS s_0x14(void *o, int b, int name, int id) { return rec(0x14, o, b, id, 0), flat_answer; }

// A CheckBox or RadioButton (k = 1) with the Win's Buffer a hand-built one, two pictures, and two items.
static uint8_t tbox[2][0xb40];
static int32_t tvbt[2][4];
static struct game_list_item titems[2][3];
static struct game_dlg_node tnodes[2][3];
static char *ttext[3] = {"ab", "abcd|e", "x"};
static uint8_t tpix[2][40 * 20 + 16], tpar_pix[40 * 20 + 16];
static uint8_t sd_a[4] = {3, 9, 3, 9}, sd_b[4] = {4, 4, 9, 9};
static struct game_sprite sp_a = {.data = sd_a, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};
static struct game_sprite sp_b = {.data = sd_b, .key = 9, .stride = 2, .width = 2, .height = 2, .full_w = 2, .full_h = 2};
static struct {
    struct game_win win;
    struct game_buffer buf;
} tpar;
static void tbuf(struct game_buffer *b, uint8_t *p) {
    memset(b, 0, sizeof *b), memset(p, 0, 40 * 20 + 16);
    b->bits = p, b->width = 40, b->neg_height = -20, b->pitch = 40;
    b->full[2] = 40, b->full[3] = 20, b->clip[2] = 40, b->clip[3] = 20;
    *(struct font **)((uint8_t *)b + 0x52c) = &tfont;
}
static void make_tbox(int k, int n) {
    uint8_t *obj = tbox[k];
    memset(obj, 0, sizeof tbox[k]), memset(titems[k], 0, sizeof titems[k]), memset(tnodes[k], 0, sizeof tnodes[k]);
    tvbt[k][1] = k ? 0x18 : 0x1c, tvbt[k][2] = k ? 0xa30 : 0xa34;
    *(int32_t **)obj = tvbt[k];
    struct game_win *win = (struct game_win *)(obj + tvbt[k][1]);
    win->vtable = vt, win->parent = &tpar.win;
    tbuf(win_buf_(win), tpix[k]);
    struct game_sprite *a = &sp_a, *b = &sp_b;
    memcpy(obj + (k ? 4 : 0x10), &a, sizeof a), memcpy(obj + (k ? 8 : 0x14), &b, sizeof b);
    struct game_dlg *d = (struct game_dlg *)(obj + tvbt[k][2]);
    for (int i = 0; i < n; i++) {
        titems[k][i].text = ttext[i], titems[k][i].lines = 1;
        tnodes[k][i].pad8 = (int32_t)(intptr_t)&titems[k][i], tnodes[k][i].key = 50 + i;
        tnodes[k][i].next = &tnodes[k][(i + 1) % n], tnodes[k][i].prev = &tnodes[k][(i + n - 1) % n];
    }
    d->head = n ? tnodes[k] : NULL, d->cur = n ? tnodes[k] : NULL, d->count = n;
    DLG_F(d, 0x48) = -1, DLG_F(d, 0x4c) = 1, DLG_F(d, 0x50) = 1, DLG_F(d, 0x40) = 1, DLG_F(d, 0x44) = 2, DLG_F(d, 0xb8) = -1;
    DLG_F(d, 0x70) = DLG_F(d, 0x74) = DLG_F(d, 0x78) = (int32_t)(intptr_t)&tfont;
    DLG_F(d, 0xf0) = -1;
}

static void box_tests(void) {
    game_select_object = t_select_object, game_get_stock_object = t_stock_object, game_get_text_extent = t_text_extent;
    dc_slot_ = &fake_dc_, game_font_dc = &dc_slot_;
    default_font_slot_ = &tfont, game_font_default = &default_font_slot_;
    game_font_slack = &slack_;
    tfont.first = -1, tfont.hfont = (void *)1, tfont.ascent = 8, tfont.height = 10, tfont.f1c = 3;
    vt[0x28 / 4] = s_0x28, vt[0x2c / 4] = s_0x2c, vt[0x14 / 4] = s_0x14;
    memset(&tpar, 0, sizeof tpar);
    tpar.win.vtable = vt;
    tbuf(&tpar.buf, tpar_pix);
    for (int i = 0; i < 40 * 20; i++) tpar_pix[i] = (uint8_t)(i % 200 + 20);

    // The CheckBox's layout: the picture is as wide as the larger of the two, an item as high as the text, the
    // box one column as wide as the longest text (6 characters of 6) and the gaps and margins make it; no
    // items, nothing done.
    make_tbox(0, 2);
    struct game_dlg *d = (struct game_dlg *)(tbox[0] + tvbt[0][2]);
    EXPECT(checkbox_calculate_dimensions(tbox[0]), 0);
    EXPECT(DLG_F(d, 0x34), 2), EXPECT(DLG_F(d, 0x68), 10), EXPECT(DLG_F(d, 0x58), 2), EXPECT(DLG_F(d, 0x64), 38);
    EXPECT(DLG_F(d, 0x5c), 40), EXPECT(DLG_F(d, 0x54), 1), EXPECT(DLG_F(d, 0x60), 24);
    EXPECT(d->cur == tnodes[0], 1), EXPECT(d->idx, 0);
    make_tbox(0, 0);
    EXPECT(checkbox_calculate_dimensions(tbox[0]), 7);
    // A height and a width asked for.
    make_tbox(0, 2);
    DLG_F(d, 0x20) = 3, DLG_F(d, 0x2c) = 60, DLG_F(d, 0x30) = 40;
    EXPECT(checkbox_calculate_dimensions(tbox[0]), 0);
    EXPECT(DLG_F(d, 0x5c), 60), EXPECT(DLG_F(d, 0x64), 60 - 2 - 2), EXPECT(DLG_F(d, 0x60), 40), EXPECT(DLG_F(d, 0x58), 40 / 10);

    // The RadioButton's layout: one row at most, so a column of the items' number over the rows, plus one,
    // and the rows of that; the lines of an item are its width over the width asked for (or 100000), plus one.
    make_tbox(1, 2);
    d = (struct game_dlg *)(tbox[1] + tvbt[1][2]);
    EXPECT(radio_calculate_dimensions(tbox[1]), 0);
    EXPECT(DLG_F(d, 0x4c), 3), EXPECT(DLG_F(d, 0x58), 1), EXPECT(DLG_F(d, 0x54), 3), EXPECT(DLG_F(d, 0x64), 38);
    EXPECT(DLG_F(d, 0x5c), 120), EXPECT(DLG_F(d, 0x60), 12), EXPECT(titems[1][0].lines, 1), EXPECT(titems[1][1].lines, 1);
    make_tbox(1, 2);
    DLG_F(d, 0x20) = 2, DLG_F(d, 0x2c) = 100, DLG_F(d, 0x50) = 5;
    EXPECT(radio_calculate_dimensions(tbox[1]), 0);
    // (100 over 1 column, less the margins and picture: 100 - 2 - 2 = 96; "abcd|e" is 36 wide)
    EXPECT(titems[1][0].lines, 1), EXPECT(DLG_F(d, 0x58), 2), EXPECT(DLG_F(d, 0x4c), 1);
    DLG_F(d, 0x2c) = 30;
    EXPECT(radio_calculate_dimensions(tbox[1]), 0);
    EXPECT(titems[1][1].lines, 36 / (30 - 2 - 2) + 1);

    // Drawing items (without text, which needs the whole text drawing): the position, the parent asked, the
    // background taken from the parent, the picture for the state.
    for (int k = 0; k < 2; k++) {
        make_tbox(k, 2);
        d = (struct game_dlg *)(tbox[k] + tvbt[k][2]);
        for (int i = 0; i < 2; i++) titems[k][i].text = NULL;
        EXPECT(k ? radio_calculate_dimensions(tbox[k]) : checkbox_calculate_dimensions(tbox[k]), 0);
        struct game_win *win = (struct game_win *)(tbox[k] + tvbt[k][1]);
        struct game_buffer *buf = win_buf_(win);
        for (int i = 0; i < 2; i++) titems[k][i].text = NULL;
        DLG_F(d, 0x58) = 2, DLG_F(d, 0x64) = 30, DLG_F(d, 0x24) = 3, DLG_F(d, 0x28) = 1;
        item_answer = 0, ncalls = 0;
        memset(tpix[k], 0, sizeof tpix[k]);
        if (k) radio_draw_item(tbox[k], 1, 1);
        else checkbox_draw_item(tbox[k], 1, 1, 0);
        // (column 0, row 1: x = the margin 1, y = one row down (10 + 2) and half the gap (1))
        EXPECT(ncalls, 1), expect_call(0, 0x2c, &tpar.win, 51, 2, 0);
        EXPECT(item_rect[0], 1), EXPECT(item_rect[1], 13), EXPECT(item_rect[2], 1 + DLG_F(d, 0x64)), EXPECT(item_rect[3], 23);
        // the parent's pixels at (3 + 1, 1 + 13), the checked picture (4 in its top left) at (1, 13 + (10 - 2) / 2)
        EXPECT(tpix[k][15 * 40 + 6], tpar_pix[16 * 40 + 9]);
        EXPECT(tpix[k][17 * 40 + 1], 4), EXPECT(tpix[k][17 * 40 + 2], 4), EXPECT(tpix[k][18 * 40 + 1], tpar_pix[19 * 40 + 4]);
        // not hot, the mouse over it: state 1 and the unchecked picture
        d->hover = 0, ncalls = 0;
        memset(tpix[k], 0, sizeof tpix[k]);
        if (k) radio_draw_item(tbox[k], 0, 0);
        else checkbox_draw_item(tbox[k], 0, 0, 0);
        expect_call(0, 0x2c, &tpar.win, 50, 1, 0);
        EXPECT(tpix[k][(1 + 2 + 3) * 40 + 1], 3), EXPECT(item_rect[1], 0 + 1);
        // the parent draws the background itself: nothing copied and no picture
        item_answer = 1;
        memset(tpix[k], 0, sizeof tpix[k]);
        if (k) radio_draw_item(tbox[k], 0, 1);
        else checkbox_draw_item(tbox[k], 0, 1, 0);
        int any = 0;
        for (int i = 0; i < 40 * 20; i++) any |= tpix[k][i];
        EXPECT(any, 0);
        // out of range, or no parent: nothing at all
        ncalls = 0;
        if (k) radio_draw_item(tbox[k], 2, 0), radio_draw_item(tbox[k], -3, 0);
        else checkbox_draw_item(tbox[k], 2, 0, 0), checkbox_draw_item(tbox[k], -3, 0, 0);
        EXPECT(ncalls, 0);
        win->parent = NULL;
        if (k) radio_draw_item(tbox[k], 0, 0);
        else checkbox_draw_item(tbox[k], 0, 0, 0);
        EXPECT(ncalls, 0);
        (void)buf;
    }
    // The redraw: the parent asked for the background (slot 0x28), if it doesn't the parent's pixels are
    // copied, and every item is drawn (each asks the parent).
    for (int k = 0; k < 2; k++) {
        make_tbox(k, 2);
        d = (struct game_dlg *)(tbox[k] + tvbt[k][2]);
        EXPECT(k ? radio_calculate_dimensions(tbox[k]) : checkbox_calculate_dimensions(tbox[k]), 0);
        for (int i = 0; i < 2; i++) titems[k][i].text = NULL;
        struct game_win *win = (struct game_win *)(tbox[k] + tvbt[k][1]);
        d->sel = k ? 1 : 2;
        back_answer = 0, item_answer = 1, ncalls = 0, cur = NULL;
        memset(tpix[k], 0, sizeof tpix[k]);
        if (k) radio_on_redraw(win);
        else checkbox_on_redraw(win);
        EXPECT(cur == &tpar.win, 1), EXPECT(ncalls, 3);
        expect_call(0, 0x28, &tpar.win, (int)(intptr_t)win, 0, 0);
        expect_call(1, 0x2c, &tpar.win, 50, 0, 0);
        expect_call(2, 0x2c, &tpar.win, 51, k ? 2 : 2, 0);
        EXPECT(tpix[k][5 * 40 + 7], tpar_pix[5 * 40 + 7]);
        back_answer = 1, ncalls = 0;
        memset(tpix[k], 0, sizeof tpix[k]);
        if (k) radio_on_redraw(win);
        else checkbox_on_redraw(win);
        EXPECT(tpix[k][5 * 40 + 7], 0), EXPECT(ncalls, 3);
        win->parent = NULL, ncalls = 0;
        if (k) radio_on_redraw(win);
        else checkbox_on_redraw(win);
        EXPECT(ncalls, 0);
    }

    // FlatButton: its look (forced, or pressed or down, or changed) picks the picture, centred; without a name,
    // nothing else is asked of the parent.
    static struct game_flat_button fb;
    static uint8_t fill_byte = 5;
    static char dots_a[] = "...", dots_b[] = "...";
    game_flat_fill = &fill_byte, game_flat_dots_a = dots_a, game_flat_dots_b = dots_b;
    memset(&fb, 0, sizeof fb);
    fb.base.win.vtable = vt, fb.base.win.parent = &tpar.win;
    tbuf(&fb.base.buf, tpix[0]);
    fb.base.focus_a = -1, fb.base.picture = (void *)(intptr_t)-1;
    fb.pic[0] = &sp_a, fb.pic[1] = &sp_b, fb.pic[2] = &sp_a;
    fb.text_color[0] = fb.text_color[1] = fb.text_color[2] = -1;
    cur = NULL;
    flat_button_on_redraw(&fb);
    EXPECT(cur == &tpar.win, 1);
    // (40 by 20: the 2 by 2 picture at (19, 9); the rest is the fill colour)
    EXPECT(tpix[0][9 * 40 + 19], 3), EXPECT(tpix[0][0], 5);
    memset(tpix[0], 0, sizeof tpix[0]);
    fb.base.changed = 1;
    flat_button_on_redraw(&fb);
    EXPECT(tpix[0][9 * 40 + 19], 4);
    fb.base.picture = (void *)(intptr_t)0;
    memset(tpix[0], 0, sizeof tpix[0]);
    flat_button_on_redraw(&fb);
    EXPECT(tpix[0][9 * 40 + 19], 3);
    fb.base.picture = (void *)(intptr_t)7, fb.base.pressed = 1;
    memset(tpix[0], 0, sizeof tpix[0]);
    flat_button_on_redraw(&fb);
    EXPECT(tpix[0][9 * 40 + 19], 3);
    // The plain border, and the parent drawing the name: asked with the button, the name and its id.
    memset(&fb.pic, 0, sizeof fb.pic);
    fb.base.pressed = 0, fb.base.changed = 0, fb.base.border_w = 1, fb.base.border_a = 11, fb.base.border_b = 12;
    memset(tpix[0], 0, sizeof tpix[0]);
    flat_button_on_redraw(&fb);
    EXPECT(tpix[0][0] == 11 || tpix[0][0] == 12, 1), EXPECT(tpix[0][5 * 40 + 5], 5);
    fb.base.name = "Hi", fb.base.id = 6, flat_answer = 1, ncalls = 0;
    flat_button_on_redraw(&fb);
    EXPECT(ncalls, 1), expect_call(0, 0x14, &tpar.win, (int)(intptr_t)&fb, 6, 0);
}

// ---- Life cycle ----

static int life_closes, life_data_closed;
static void LT_THIS life_win_close(void *w) { life_closes++; }
static void LT_THIS life_data_close(void *d) { life_data_closed++; }

static void test_life(void) {
    life_image_init();
    life_buffer_standins();
    life_time_standins();
    game_win_close = life_win_close, game_dialog_data_close = life_data_close;
    game_dialog_defs_a = game_at(0x697098), game_dialog_defs_b = game_at(0x9b8eb4);
    static uint8_t obj[0xb60];

    // BaseButton: the vtables of its class and of its Buffer, no click pending and no auto-repeat, the game's
    // defaults, no name; its two Time objects made (a resolution of 5).
    memset(obj, 0xcc, sizeof obj);
    EXPECT(base_button_ctor((struct game_button *)obj) == (struct game_button *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x670290)), EXPECT(OW(obj, 0x444), IMGVA(0x670288)), EXPECT(OW(obj, 0xa44), 0xffffffffu);
    EXPECT(OW(obj, 0xa48), 0xffffffffu), EXPECT(OW(obj, 0xa84), IMGW(0x69704c)), EXPECT(OW(obj, 0xa90), IMGW(0x697058));
    EXPECT(OW(obj, 0xa94), IMGW(0x9b8e2c)), EXPECT(OW(obj, 0xa98), IMGW(0x9b8e30)), EXPECT(OW(obj, 0xaa0), IMGW(0x69705c));
    EXPECT(OW(obj, 0xa7c), 0), EXPECT(OW(obj, 0xa80), 0), EXPECT(OW(obj, 0xaa8), 0), EXPECT(OW(obj, 0xaac), 0), EXPECT(OW(obj, 0xa74), 0);
    EXPECT(OW(obj, 0xa1c + 0x20), 5), EXPECT(OW(obj, 0xa4c + 0x20), 5), EXPECT(OW(obj, 0xa18), 0xcccccccc);
    // Close: the name and the bubble are freed and the defaults put back.
    char *name = malloc(8), *bubble = malloc(8);
    OW(obj, 0xa7c) = (uint32_t)(uintptr_t)name, OW(obj, 0xa80) = (uint32_t)(uintptr_t)bubble;
    OW(obj, 0xa84) = 1, OW(obj, 0xa44) = 3, OW(obj, 0xaa8) = 0x1234;
    life_closes = 0, life_freed = 0;
    base_button_close((struct game_button *)obj);
    EXPECT(life_closes, 1), EXPECT(life_freed, 2), EXPECT(life_freed_has(name), 1), EXPECT(life_freed_has(bubble), 1);
    EXPECT(OW(obj, 0xa7c), 0), EXPECT(OW(obj, 0xa80), 0), EXPECT(OW(obj, 0xaa8), 0), EXPECT(OW(obj, 0xa84), IMGW(0x69704c)), EXPECT(OW(obj, 0xa44), 0xffffffffu);
    life_freed = 0, life_closes = 0;
    OW(obj, 0) = 0;
    base_button_dtor((struct game_button *)obj);  // (nothing to free: only the closes)
    EXPECT(life_closes, 2), EXPECT(life_freed, 0), EXPECT(OW(obj, 0), IMGVA(0x66ff34));

    // FlatButton: three looks, each with the pieces of its frame from the game's table.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(flat_button_ctor((struct game_flat_button *)obj) == (struct game_flat_button *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x669754)), EXPECT(OW(obj, 0x444), IMGVA(0x66974c)), EXPECT(OW(obj, 0xa18), 0), EXPECT(OW(obj, 0xa14), 0);
    EXPECT(OW(obj, 0xab8), 0xffffffffu), EXPECT(OW(obj, 0xabc), 0xffffffffu), EXPECT(OW(obj, 0xac4), 0xffffffffu);
    EXPECT(OW(obj, 0xac8), 0), EXPECT(OW(obj, 0xad4 + 8), 0);
    for (int i = 0; i < 3; i++) {
        EXPECT(OW(obj, 0xae0 + 4 * i), IMGW(0x9b8e44 + 4 * i)), EXPECT(OW(obj, 0xaec + 4 * i), IMGW(0x9b8e50 + 4 * i));
        EXPECT(OW(obj, 0xaf8 + 4 * i), IMGW(0x9b8e5c + 4 * i)), EXPECT(OW(obj, 0xb04 + 4 * i), IMGW(0x9b8e68 + 4 * i));
        EXPECT(OW(obj, 0xb10 + 4 * i), IMGW(0x9b8e74 + 4 * i)), EXPECT(OW(obj, 0xb1c + 4 * i), IMGW(0x9b8e80 + 4 * i));
        EXPECT(OW(obj, 0xb28 + 4 * i), IMGW(0x9b8e8c + 4 * i)), EXPECT(OW(obj, 0xb34 + 4 * i), IMGW(0x9b8e98 + 4 * i));
        EXPECT(OW(obj, 0xb40 + 4 * i), IMGW(0x9b8ea4 + 4 * i));
    }
    OW(obj, 0xac8) = 5, OW(obj, 0xae4) = 6, OW(obj, 0xab8) = 1;
    life_closes = 0;
    flat_button_close((struct game_flat_button *)obj);
    EXPECT(OW(obj, 0xac8), 0), EXPECT(OW(obj, 0xae4), IMGW(0x9b8e48)), EXPECT(OW(obj, 0xab8), 0xffffffffu), EXPECT(life_closes, 1);
    OW(obj, 0) = 0, OW(obj, 0x444) = 0;
    life_closes = 0;
    flat_button_dtor((struct game_flat_button *)obj);
    EXPECT(life_closes, 3), EXPECT(OW(obj, 0), IMGVA(0x66ff34));

    // PushButton: the pictures of its two looks (18 words from 0x9bc118); ImageButton: no picture.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(push_button_ctor((struct game_push_button *)obj) == (struct game_push_button *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x670c60)), EXPECT(OW(obj, 0x444), IMGVA(0x670c58));
    for (int i = 0; i < 18; i++) EXPECT(OW(obj, 0xab8 + 4 * i), IMGW(0x9bc118 + 4 * i));
    EXPECT(OW(obj, 0xb00), 0xcccccccc);
    push_button_dtor((struct game_push_button *)obj);
    EXPECT(OW(obj, 0), IMGVA(0x66ff34));
    memset(obj, 0xcc, sizeof obj);
    EXPECT(image_button_ctor((struct game_button *)obj) == (struct game_button *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x670a94)), EXPECT(OW(obj, 0x444), IMGVA(0x670a8c)), EXPECT(OW(obj, 0xab8), 0);
    image_button_dtor((struct game_button *)obj);
    EXPECT(OW(obj, 0), IMGVA(0x66ff34));

    // CheckButton: the pictures are the game's, the state and handlers clear; close, then the destructor.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(check_button_ctor((struct game_check_button *)obj) == (struct game_check_button *)obj, 1);
    EXPECT(OW(obj, 0), IMGVA(0x66dc14)), EXPECT(OW(obj, 0x444), IMGVA(0x66dc0c)), EXPECT(OW(obj, 0xa14), 0), EXPECT(OW(obj, 0xa20), 0);
    EXPECT(OW(obj, 0xa28), IMGW(0x697f24)), EXPECT(OW(obj, 0xa24), IMGW(0x697f20)), EXPECT(OW(obj, 0xa2c), 0xcccccccc);
    OW(obj, 0xa14) = 1, OW(obj, 0xa28) = 0;
    life_closes = 0;
    check_button_close((struct game_check_button *)obj);
    EXPECT(OW(obj, 0xa14), 0), EXPECT(OW(obj, 0xa28), IMGW(0x697f24)), EXPECT(life_closes, 1);
    life_closes = 0;
    check_button_dtor((struct game_check_button *)obj);
    EXPECT(life_closes, 2), EXPECT(OW(obj, 0), IMGVA(0x66ff34));

    // CheckBox and RadioButton: objects with two virtual bases (the GraphicWin, then the Dialog), whose places
    // the table the first word points to gives.
    life_vbtable(0x66b0ec, 0x24, 0);
    life_vbtable(0x670718, 0x1c, 0xa34), life_vbtable(0x670590, 0x18, 0xa30);
    static uint8_t big[0xb40];
    memset(big, 0xcc, sizeof big);
    EXPECT(checkbox_ctor(big, 1) == big, 1);
    EXPECT(OW(big, 0), IMGVA(0x670718)), EXPECT(OW(big, 0x1c), IMGVA(0x6705b0)), EXPECT(OW(big, 0x1c + 0x444), IMGVA(0x6705a8));
    EXPECT(OW(big, 0xa34), IMGVA(0x67059c)), EXPECT(OW(big, 0x18), 0), EXPECT(OW(big, 0xa30), 0);
    EXPECT(OW(big, 4), 0), EXPECT(OW(big, 8), 0), EXPECT(OW(big, 0xc), 0), EXPECT(OW(big, 0x10), IMGW(0x697104)), EXPECT(OW(big, 0x14), IMGW(0x697108));
    EXPECT(OW(big, 0x1c + 0xa8), (uint32_t)(uintptr_t)(big + 0x1c)), EXPECT(OW(big, 0xa34 + 0xf0), 0xffffffffu);  // (the Win's root; the Dialog's hover)
    OW(big, 0x10) = 9, OW(big, 4) = 7;
    life_closes = 0, life_data_closed = 0;
    checkbox_dtor(big + 0x1c);
    EXPECT(OW(big, 0x10), IMGW(0x697104)), EXPECT(OW(big, 4), 0), EXPECT(life_closes, 1), EXPECT(life_data_closed, 1);
    EXPECT(OW(big, 0x1c), IMGVA(0x6705b0)), EXPECT(OW(big, 0xa34), IMGVA(0x67059c));

    memset(big, 0xcc, sizeof big);
    EXPECT(radio_ctor(big, 1) == big, 1);
    EXPECT(OW(big, 0), IMGVA(0x670590)), EXPECT(OW(big, 0x18), IMGVA(0x669a6c)), EXPECT(OW(big, 0x18 + 0x444), IMGVA(0x669a64));
    EXPECT(OW(big, 0xa30), IMGVA(0x669a58)), EXPECT(OW(big, 4), IMGW(0x6970f0)), EXPECT(OW(big, 8), IMGW(0x6970f4));
    EXPECT(OW(big, 0xc), 0), EXPECT(OW(big, 0x10), 0), EXPECT(OW(big, 0x14), 0);
    // A part of a bigger object (`most_derived` clear) leaves the bases to the maker.
    uint32_t win_vt = OW(big, 0x18);
    OW(big, 0x18) = 0;
    EXPECT(radio_ctor(big, 0) == big, 1), EXPECT(OW(big, 0x18), win_vt);
    OW(big, 4) = 5, OW(big, 0xc) = 6;
    life_closes = 0, life_data_closed = 0;
    radio_close(big);
    EXPECT(OW(big, 4), IMGW(0x6970f0)), EXPECT(OW(big, 0xc), 0), EXPECT(life_closes, 1), EXPECT(life_data_closed, 1);
    // The deleting destructor closes, unmakes the bases and frees the object only if asked.
    uint8_t *heap_obj = calloc(1, sizeof big);
    memcpy(heap_obj, big, sizeof big);
    life_closes = 0, life_data_closed = 0, life_freed = 0;
    EXPECT(radio_dtor(big + 0x18, 0) == big, 1);
    EXPECT(life_freed, 0), EXPECT(life_closes, 2), EXPECT(OW(big, 0x18), IMGVA(0x66ff34));
    EXPECT(radio_dtor(heap_obj + 0x18, 1) == heap_obj, 1);
    EXPECT(life_freed, 1);
}

int main(void) {
    game_win_capture = &capture, game_win_hover = &hover, game_win_press1 = &press1, game_win_press2 = &press2, game_win_cur = &cur;
    game_win_press_inside = &inside, game_button_hooks = (void *)hooks, game_button_defaults = &defaults, game_button_fonts = &fonts;
    vt[0x10 / 4] = s_0x10, vt[0x1c / 4] = s_0x1c, vt[0x44 / 4] = s_0x44, vt[0x5c / 4] = s_0x5c, vt[0xb0 / 4] = s_0xb0;
    vt[0xb4 / 4] = s_0xb4, vt[0xb8 / 4] = s_0xb8, vt[0xbc / 4] = s_0xbc, vt[0xf8 / 4] = s_0xf8, vt[0xf4 / 4] = s_0xf4;

    // The defaults: the first, second and third number of each of the four colours.
    button_set_def_text_color(1, 2, 3, 4), button_set_def_text_color2(5, 6, 7, 8), button_set_def_text_color3(9, 10, 11, 12);
    EXPECT(defaults.text[0][0], 1), EXPECT(defaults.text[3][0], 4), EXPECT(defaults.text[1][1], 6), EXPECT(defaults.text[2][2], 11);
    EXPECT(sizeof defaults, 0x44);
    struct font f = {0};
    EXPECT(button_set_def_font(NULL, (void *)1, (void *)2), 3);
    EXPECT(button_set_def_font(&f, (void *)7, (void *)8), 0);
    EXPECT(fonts.def == NULL, 1), EXPECT((intptr_t)fonts.def_a, 7), EXPECT((intptr_t)fonts.def_b, 8);
    f.hfont = (void *)1;
    EXPECT(button_set_def_font(&f, (void *)9, (void *)9), 0), EXPECT(fonts.def == &f, 1);

    // State: a change redraws and tells the parent; the same state does nothing.
    reset();
    button_set(&B[0], 0);
    EXPECT(ncalls, 0);
    button_set(&B[0], 2);
    EXPECT(B[0].state, 2), EXPECT(ncalls, 2);
    expect_call(0, 0xf8, &B[0], 0, 0, 0), expect_call(1, 0xb4, &P, 10, 2, 0);
    ncalls = 0;
    button_toggle(&B[0]);
    EXPECT(B[0].state, 0), EXPECT(ncalls, 2);
    button_toggle(&B[0]);
    EXPECT(B[0].state, 1);
    ncalls = 0, B[1].win.parent = NULL;
    button_set(&B[1], 1);
    EXPECT(ncalls, 1);
    ncalls = 0;
    button_on_dialog_focus(&B[0], 0);
    EXPECT(ncalls, 1);

    // The mouse leaving a button that has the press moves it.
    ncalls = 0, press1 = NULL, press2 = &B[0].win, inside = 1;
    button_on_mouse_leave(&B[0], 4, 5);
    expect_call(0, 0x44, &B[0], 4, 5, 0);
    ncalls = 0, inside = 0;
    button_on_mouse_leave(&B[0], 4, 5);
    EXPECT(ncalls, 0);

    // A click: capture and hover go, the sound plays unless busy, the parent hears; a group answers instead.
    reset();
    capture = hover = &B[0].win, hooks[0] = cb_hook, B[0].id = -1, B[0].on_click = cb_click;
    *(void **)((uint8_t *)&P + 0x7c) = sound;
    button_on_left_click(&B[0], 0, 0);
    EXPECT(capture == NULL, 1), EXPECT(hover == NULL, 1), EXPECT(cur == &P, 1);
    expect_call(0, 0x10, &B[0], 0, 0, 0), expect_call(1, 0x403, NULL, 0, 0, 0), expect_call(2, 0x5c, sound, 0, 0, 0);
    expect_call(3, 0x1c, sound, 0, 0, 0), expect_call(4, 0xb0, &P, -1, 0, 0), expect_call(5, 0x401, NULL, -1, 0, 0);
    EXPECT(ncalls, 6);
    ncalls = 0, busy = 1, B[0].disabled = 1;
    button_on_left_click(&B[0], 0, 0);
    EXPECT(ncalls, 1), expect_call(0, 0x401, NULL, -1, 0, 0);

    // A group: the selected button is set to 0 and the clicked one to 1, and the parent hears of it.
    reset();
    EXPECT((intptr_t)button_group_init(&G, 77, 0), 77);
    EXPECT(G.sel, -2), EXPECT(G.count, 0);
    for (int i = 0; i < 3; i++) EXPECT(button_group_add(&G, &B[i]), (int)(intptr_t)&B[i]), EXPECT(B[i].group == &G, 1);
    EXPECT(G.count, 3);
    B[1].state = 1, G.sel = 11, B[2].changed = 1;
    EXPECT(button_group_click(&G, 12), 0);
    EXPECT(B[1].state, 0), EXPECT(B[2].state, 1), EXPECT(G.sel, 12), EXPECT(B[2].changed, 0);
    // set 0, redraw, (set told parent: 0xf8 then 0xb4) ...
    expect_call(0, 0xf8, &B[1], 0, 0, 0), expect_call(1, 0xb4, &P, 11, 0, 0), expect_call(2, 0xf8, &B[1], 0, 0, 0);
    expect_call(3, 0xf8, &B[2], 0, 0, 0), expect_call(4, 0xb4, &P, 12, 1, 0), expect_call(5, 0xf8, &B[2], 0, 0, 0);
    expect_call(6, 0xbc, &P, 11, 12, 0), expect_call(7, 0xb8, &P, 77, 11, 12), expect_call(8, 0xf8, &B[2], 0, 0, 0);
    EXPECT(ncalls, 9);
    // Clicking the selected one again does nothing without flag 1, deselects with flag 2.
    ncalls = 0;
    button_group_click(&G, 12);
    EXPECT(ncalls, 0), EXPECT(G.sel, 12);
    G.flags = 2;
    button_group_click(&G, 12);
    EXPECT(G.sel, -1), EXPECT(B[2].state, 0);
    // set with 0 for the second argument is quiet.
    ncalls = 0, G.flags = 0, G.silent = 0;
    for (int i = 0; i < 3; i++) B[i].id = i;
    button_group_set(&G, 0, 0);
    EXPECT(G.sel, 0), EXPECT(G.silent, 0);
    for (int i = 0; i < ncalls; i++) EXPECT(calls[i].slot != 0xbc && calls[i].slot != 0xb8, 1);
    button_group_close(&G);
    EXPECT(G.count, 0), EXPECT(G.sel, 0), EXPECT(G.buttons[0] == NULL, 1);

    // The Dialog list: ids and positions, and a position back from the head.
    make_box(3);
    struct game_dlg *d = dlg();
    EXPECT(dialog_id_to_pos(d, 101), 1), EXPECT(dialog_id_to_pos(d, 102), 2), EXPECT(dialog_id_to_pos(d, 5), 3);
    dialog_set_selected_id(d, 102);
    EXPECT(d->sel, 2), EXPECT(dialog_get_selected_id(d), 102);
    EXPECT(dialog_pos_to_id(d, 0), 100), EXPECT(dialog_pos_to_id(d, 1), 101), EXPECT(dialog_pos_to_id(d, -1), 102);
    EXPECT(dialog_pos_to_id(d, -3), 100), EXPECT(d->idx, 0), EXPECT(dialog_pos_to_id(d, 9), 100);
    make_box(0);
    EXPECT(dialog_get_selected_id(dlg()), 0);

    // A CheckBox keeps its states as a mask by position.
    make_box(3);
    d = dlg();
    checkbox_set_state_id(box, 101, 1);
    EXPECT(d->sel, 2), EXPECT(checkbox_get_state_id(box, 101), 2), EXPECT(checkbox_get_state_id(box, 100), 0);
    checkbox_flip_state_id(box, 100);
    EXPECT(d->sel, 3);
    checkbox_flip_state_id(box, 101);
    EXPECT(d->sel, 1);
    checkbox_flip_state_pos(box, 4);
    EXPECT(d->sel, 17), EXPECT(checkbox_get_state_pos(box, 4), 16);
    checkbox_set_state_pos(box, 4, 0);
    EXPECT(d->sel, 1);
    ncalls = 0;
    checkbox_set_state_flag(box, 0x55);
    EXPECT(d->sel, 0x55), EXPECT(ncalls, 1), expect_call(0, 0xf8, box + 0x1c, 0, 0, 0);
    // The handlers get the Win, 0x1c into the box.
    ncalls = 0, d->hover = 5;
    checkbox_on_mouse_leave(box + 0x1c, 0, 0);
    EXPECT(d->hover, -1), EXPECT(ncalls, 1);
    ncalls = 0;
    checkbox_on_dialog_focus(box + 0x1c, 0);
    EXPECT(ncalls, 1);
    // The mouse move: a cell of 10 + 2 by 8 + 2 here, columns of one cell.
    *(int32_t *)(box + 0x1c + 0xa34 + 0x28) = 2, *(int32_t *)(box + 0x1c + 0xa34 + 0x48) = 10, *(int32_t *)(box + 0x1c + 0xa34 + 0x4c) = 8;
    *(int32_t *)(box + 0x1c + 0xa34 + 0x3c) = 3;
    d->sel = 0, d->hover = -1, ncalls = 0;
    checkbox_on_mouse_move(box + 0x1c, 25, 11);  // column 2, row 1: cell 7
    EXPECT(d->hover, 7), EXPECT(ncalls, 1);
    d->sel = 1 << 7, ncalls = 0;
    checkbox_on_mouse_move(box + 0x1c, 25, 11);
    EXPECT(d->hover, -1), EXPECT(ncalls, 1);
    ncalls = 0;
    checkbox_on_mouse_move(box + 0x1c, 25, 11);
    EXPECT(ncalls, 0);
    // A double click: the function at 0xc gets the mask.
    ncalls = 0, d->sel = 9;
    *(void **)(box + 0xc) = cb_click;
    *(void **)(box + 0x1c + 0xc4) = &P;
    checkbox_on_left_double_click(box + 0x1c, 0, 0);
    EXPECT(cur == &P, 1), expect_call(0, 0x401, NULL, 9, 0, 0);

    // A CheckButton turns over and tells its parent.
    struct game_check_button c;
    memset(&c, 0, sizeof c);
    c.win.vtable = vt, c.win.parent = &P, c.id = 4, c.on_click = cb_click;
    ncalls = 0;
    checkbtn_on_left_click(&c, 0, 0);
    EXPECT(c.state, 1), EXPECT(ncalls, 4);
    expect_call(0, 0xf8, &c, 0, 0, 0), expect_call(1, 0x401, NULL, 4, 0, 0), expect_call(2, 0xb0, &P, 4, 0, 0), expect_call(3, 0xf8, &c, 0, 0, 0);
    c.win.parent = NULL, ncalls = 0;
    checkbtn_on_left_click(&c, 0, 0);
    EXPECT(c.state, 1), EXPECT(ncalls, 0);

    draw_tests();
    box_tests();
    test_life();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
