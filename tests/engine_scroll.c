// Unit test for the Scroll, ListBox and StringBox classes (scroll.c) on hand-built bars, boxes and parents
// with vtables that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/engine/button.h"
#include "../src/engine/game.h"
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
    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
