// Unit test for the button classes (button.c) on hand-built buttons, groups, boxes and parents with
// vtables that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/button.h"
#include "../src/engine/game.h"
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
    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
