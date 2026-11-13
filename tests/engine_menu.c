// Unit test for the Menu, PullDown and Spot classes (menu.c) on hand-built objects with vtables that log
// their calls, and a Font whose characters are 6 pixels wide (a 'W' 10): no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/menu.h"
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
} calls[32];
static int ncalls;
static void rec(int slot, void *o, int a, int b, int c) {
    if (ncalls < 32) calls[ncalls].slot = slot, calls[ncalls].o = o, calls[ncalls].a = a, calls[ncalls].b = b, calls[ncalls].c = c;
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
static int THIS s_0x8(void *o) { return rec(0x8, o, 0, 0, 0), 0; }
static int THIS s_0xc(void *o, int a, int b, int c) { return rec(0xc, o, a, b, c), 0; }
static int THIS s_0x50(void *o, int a, int b) { return rec(0x50, o, a, b, 0), 51; }
static int THIS s_0x54(void *o, int a, int b) { return rec(0x54, o, a, b, 0), 55; }
static int THIS s_0xf8(void *o) { return rec(0xf8, o, 0, 0, 0), 0; }
static int THIS s_0x170(void *o) { return rec(0x170, o, 0, 0, 0), 0; }
static void *vt[0x180 / 4];

// The Windows calls a Font measures with: every character is 6 pixels wide, a 'W' 10.
static void *selected;
static int fake_dc;
static void *__attribute__((stdcall)) select_object(void *dc, void *obj) {
    void *old = selected;
    selected = obj;
    return old;
}
static void *__attribute__((stdcall)) stock_object(int which) { return (void *)(intptr_t)(0x1000 + which); }
static int __attribute__((stdcall)) text_extent(void *dc, const char *s, int n, int32_t *size) {
    int w = 0;
    for (int i = 0; i < n; i++) w += s[i] == 'W' ? 10 : 6;
    size[0] = w, size[1] = 12;
    return 1;
}

static struct font fnt_pd, fnt_menu, fnt_default;
static struct font *pd_font_slot, *menu_font_slot, *default_font_slot;
static void *dc_slot;
static int32_t margin;
static char gaps[20] = "a\0\0\0bb\0\0ccc\0dddd";  // the strings the widths are worked out with, 4 bytes apart (a, bb, ccc, dddd)

static struct game_pulldown PD;
static struct game_menu M;
static struct game_win parent;
static void reset_pd(void) {
    memset(&PD, 0, sizeof PD);
    PD.win.vtable = vt;
    for (int i = 0; i < 64; i++) PD.items[i].id = -1;
    ncalls = 0;
}
static void line(int i, int id, uint32_t flags, char *text, char *tail) {
    PD.items[i].id = id, PD.items[i].flags = flags, PD.items[i].text = text, PD.items[i].tail = tail;
}
static void reset_menu(void) {
    memset(&M, 0, sizeof M);
    M.win.vtable = vt;
    for (int i = 0; i < 15; i++) M.entries[i].id = -1;
    ncalls = 0;
}
static struct game_pulldown PD2;

static int proc_arg;
static void proc_fn(int id) { proc_arg = id; }

int main(void) {
    vt[0x8 / 4] = (void *)s_0x8, vt[0xc / 4] = (void *)s_0xc, vt[0x50 / 4] = (void *)s_0x50, vt[0x54 / 4] = (void *)s_0x54;
    vt[0xf8 / 4] = (void *)s_0xf8, vt[0x170 / 4] = (void *)s_0x170;
    game_select_object = select_object, game_get_stock_object = stock_object, game_get_text_extent = text_extent;
    dc_slot = &fake_dc, game_font_dc = &dc_slot;
    default_font_slot = &fnt_default, game_font_default = (void **)&default_font_slot;
    pd_font_slot = NULL, menu_font_slot = NULL, game_pd_font = (void **)&pd_font_slot, game_menu_font = (void **)&menu_font_slot;
    game_pd_margin = &margin, game_pd_gap = gaps;
    fnt_default.hfont = (void *)0x11, fnt_default.first = -1, fnt_default.ascent = 10, fnt_default.height = 14;
    fnt_pd.hfont = (void *)0x22, fnt_pd.first = 2, fnt_pd.ascent = 9, fnt_pd.height = 11;  // (a line is 13 high)
    fnt_menu.hfont = (void *)0x33, fnt_menu.first = -1, fnt_menu.ascent = 9, fnt_menu.height = 12;

    // ---- Spot ----
    struct game_spot_rect room[4];
    struct game_spot sp;
    EXPECT(spot_ctor(&sp) == (int)(intptr_t)&sp, 1);
    EXPECT(sp.cap, 0), EXPECT(sp.count, 0), EXPECT(sp.rects == NULL, 1);
    memset(room, 0, sizeof room);
    sp.rects = room, sp.cap = 3;
    EXPECT(spot_add(&sp, 7, 1, 10, 20, 5, 6), 0);
    EXPECT(room[0].rect[0], 10), EXPECT(room[0].rect[1], 20), EXPECT(room[0].rect[2], 15), EXPECT(room[0].rect[3], 26);
    EXPECT(room[0].id, 7), EXPECT(room[0].kind, 1), EXPECT(sp.count, 1);
    EXPECT(spot_add(&sp, 8, 2, 12, 22, 5, 6), 1), EXPECT(spot_add(&sp, 9, 1, 0, 0, 4, 4), 2);
    EXPECT(spot_add(&sp, 10, 1, 0, 0, 4, 4), -1), EXPECT(sp.count, 3);
    // The last rectangle that holds the point wins; the right and bottom edges are outside.
    int id = -9, kind = -9;
    int32_t r4[4] = {0, 0, 0, 0};
    EXPECT(spot_check(&sp, 13, 23, &id, &kind), 1), EXPECT(id, 8), EXPECT(kind, 2);
    EXPECT(spot_check(&sp, 11, 21, &id, NULL), 0), EXPECT(id, 7);
    EXPECT(spot_check(&sp, 15, 23, &id, &kind), 1), EXPECT(id, 8);  // (x = 15 is outside the first, inside the second)
    EXPECT(spot_check(&sp, 17, 21, &id, &kind), -1), EXPECT(id, 8);  // (and nothing is written when there is no hit)
    EXPECT(spot_check(&sp, 1, 1, NULL, NULL), 2);
    EXPECT(spot_check_2(&sp, 13, 23, &id, &kind, r4), 1), EXPECT(r4[0], 12), EXPECT(r4[1], 22), EXPECT(r4[2], 17), EXPECT(r4[3], 28);
    EXPECT(spot_check_2(&sp, 100, 23, &id, &kind, r4), -1), EXPECT(r4[0], 12);
    EXPECT(spot_check_2(&sp, 11, 21, NULL, NULL, NULL), 0);
    // By id and kind.
    EXPECT(spot_get_rect(&sp, 7, 1, r4), 0), EXPECT(r4[2], 15);
    EXPECT(spot_get_rect(&sp, 7, 2, r4), -1), EXPECT(spot_get_rect(&sp, 8, 2, NULL), 1);
    EXPECT(spot_replace(&sp, 8, 2, 40, 41, 2, 3), 3);
    EXPECT(room[1].rect[0], 40), EXPECT(room[1].rect[1], 41), EXPECT(room[1].rect[2], 42), EXPECT(room[1].rect[3], 44);
    spot_replace(&sp, 8, 1, 0, 0, 1, 1);
    EXPECT(room[1].rect[0], 40);  // (the kind has to match too)
    // Removing: by place, by id and kind, by kind; the rest keep their order.
    spot_add(&sp, 5, 1, 0, 0, 1, 1);  // (full: not added)
    spot_kill_pos(&sp, 3), spot_kill_pos(&sp, -1), EXPECT(sp.count, 3);
    spot_kill_pos(&sp, 0), EXPECT(sp.count, 2), EXPECT(room[0].id, 8), EXPECT(room[1].id, 9);
    sp.cap = 4, spot_add(&sp, 9, 1, 0, 0, 2, 2), spot_add(&sp, 9, 2, 0, 0, 2, 2);
    spot_kill_specific(&sp, 9, 1), EXPECT(sp.count, 2), EXPECT(room[0].id, 8), EXPECT(room[1].id, 9), EXPECT(room[1].kind, 2);
    spot_add(&sp, 3, 2, 0, 0, 2, 2), spot_add(&sp, 4, 1, 0, 0, 2, 2);
    spot_kill_type(&sp, 2), EXPECT(sp.count, 1), EXPECT(room[0].id, 4);
    spot_kill_type(&sp, 1), EXPECT(sp.count, 0);
    EXPECT(spot_check(&sp, 0, 0, &id, &kind), -1), EXPECT(spot_get_rect(&sp, 4, 1, NULL), -1);
    sp.count = 2, sp.cap = 4;
    EXPECT(spot_clear(&sp), 0), EXPECT(sp.cap, 0), EXPECT(sp.count, 0), EXPECT(sp.rects == NULL, 1);
    // The point tests.
    EXPECT(in_box(5, 5, 5, 5, 3, 3), 1), EXPECT(in_box(4, 5, 5, 5, 3, 3), 0), EXPECT(in_box(8, 5, 5, 5, 3, 3), 0);
    EXPECT(in_box(7, 4, 5, 5, 3, 3), 0), EXPECT(in_box(7, 8, 5, 5, 3, 3), 0), EXPECT(in_box(7, 7, 5, 5, 3, 3), 1);
    int32_t rc[4] = {5, 5, 8, 8};
    EXPECT(in_box_2(5, 5, rc), 1), EXPECT(in_box_2(4, 5, rc), 0), EXPECT(in_box_2(8, 5, rc), 0);
    EXPECT(in_box_2(7, 4, rc), 0), EXPECT(in_box_2(7, 8, rc), 0), EXPECT(in_box_2(7, 7, rc), 1);

    // ---- PullDown: lines ----
    reset_pd();
    line(0, 10, 1, "A", NULL), line(1, 11, 1, "B", NULL), line(2, 12, 0, "C", NULL);
    PD.count = 3, PD.shown = 2;
    EXPECT(pulldown_id_to_index(&PD, 11), 1), EXPECT(pulldown_id_to_index(&PD, 99), -1), EXPECT(pulldown_id_to_index(&PD, -1), -1);
    PD.items[1].id = -1;  // (a free line ends the search)
    EXPECT(pulldown_id_to_index(&PD, 12), -1), PD.items[1].id = 11;
    // Hide and show change the count only when the bit changes.
    EXPECT(pulldown_hide_item(&PD, 11), 0), EXPECT(PD.items[1].flags, 0), EXPECT(PD.shown, 1), EXPECT(PD.dirty, 1);
    PD.dirty = 0, EXPECT(pulldown_hide_item(&PD, 11), 0), EXPECT(PD.shown, 1), EXPECT(PD.dirty, 1);
    EXPECT(pulldown_show_item(&PD, 12), 0), EXPECT(PD.items[2].flags, 1), EXPECT(PD.shown, 2);
    EXPECT(pulldown_show_item(&PD, 12), 0), EXPECT(PD.shown, 2);
    PD.dirty = 0, EXPECT(pulldown_hide_item(&PD, 77), 0xb), EXPECT(pulldown_show_item(&PD, 77), 0xb), EXPECT(PD.dirty, 0);
    PD.items[0].flags = 0x31;
    EXPECT(pulldown_hide_item(&PD, 10), 0), EXPECT(PD.items[0].flags, 0x30);  // (the other bits stay)
    PD.items[0].flags = 0x44;
    EXPECT(pulldown_disable_item(&PD, 10), 0), EXPECT(PD.items[0].flags, 0x46);
    EXPECT(pulldown_check_item(&PD, 10), 0), EXPECT(PD.items[0].flags, 0x4e);
    EXPECT(pulldown_enable_item(&PD, 10), 0), EXPECT(PD.items[0].flags, 0x4c);
    EXPECT(pulldown_uncheck_item(&PD, 10), 0), EXPECT(PD.items[0].flags, 0x44);
    PD.dirty = 0;
    EXPECT(pulldown_disable_item(&PD, 5), 0xb), EXPECT(pulldown_enable_item(&PD, 5), 0xb);
    EXPECT(pulldown_check_item(&PD, 5), 0xb), EXPECT(pulldown_uncheck_item(&PD, 5), 0xb), EXPECT(PD.dirty, 0);
    // The selection is the line unless it is greyed or none.
    PD.sel = -1, EXPECT(pulldown_get_selected(&PD), -1);
    PD.sel = 0, PD.items[0].flags = 3, EXPECT(pulldown_get_selected(&PD), -1);
    PD.items[0].flags = 1, EXPECT(pulldown_get_selected(&PD), 0);
    PD.sel = 2, EXPECT(pulldown_get_selected(&PD), 2);
    EXPECT(pulldown_unk2(&PD, 1), 8), EXPECT(pulldown_unk3(&PD, 1, 2, 3), 8), EXPECT(pulldown_unk5(&PD), 1), EXPECT(pulldown_unk6(&PD), 1);
    pulldown_on_mouse_leave(&PD, 1, 2), EXPECT(ncalls, 0);
    // A separator takes the line the count has reached; the count stays up if it fails.
    reset_pd();
    EXPECT(pulldown_add_separator(&PD, 40), 0), EXPECT(PD.count, 1);
    EXPECT(PD.items[0].id, 40), EXPECT(PD.items[0].flags, 5), EXPECT(PD.shown, 1), EXPECT(PD.dirty, 1);
    EXPECT(pulldown_add_separator(&PD, -1), 3), EXPECT(PD.count, 2);  // (an id of -1)
    PD.items[2].text = "x";
    EXPECT(pulldown_add_separator(&PD, 41), 0xc), EXPECT(PD.count, 3), EXPECT(PD.items[2].id, -1);  // (the line the count points to is taken)
    PD.count = 64;
    EXPECT(pulldown_add_separator(&PD, 42), 1), EXPECT(PD.count, 64);  // (too many: the count goes back)
    PD.count = 63, PD.items[63].id = 9;
    EXPECT(pulldown_add_separator(&PD, 43), 0), EXPECT(PD.items[63].id, 43), EXPECT(PD.count, 64);
    for (int i = 0; i < 64; i++) PD.items[i].id = i;  // (no free line)
    PD.count = 5, PD.items[5].text = NULL;
    EXPECT(pulldown_add_separator(&PD, 44), 1), EXPECT(PD.count, 6);

    // ---- PullDown: the width ----
    reset_pd();
    EXPECT(*game_pd_font == NULL, 1);
    // No text and no lines: the width is cleared; the font becomes the default, and stays.
    PD.width = 99;
    pulldown_compute_length(&PD, NULL);
    EXPECT(PD.width, 0), EXPECT(*game_pd_font == &fnt_default, 1);
    *game_pd_font = &fnt_pd;
    // A line: the text's width, the gap, the tail and the gap again (6 a character: "ab" 12, "a" 6, "cd" 12).
    line(0, 1, 1, "ab", "cd"), line(1, 2, 1, "abcd", NULL), line(2, 3, 1, NULL, "zz");
    pulldown_compute_length(&PD, NULL);
    // The width starts at zero, so the first line, "ab" with the tail "cd", is not narrower than its first sum
    // (12 + 2 * 18 + 12) and takes the second: "cd" 12 + 2 * "dddd" (24) + "ab" 12. "abcd" (24) is not wider.
    EXPECT(PD.width, 12 + 2 * 24 + 12);
    PD.width = 500;
    pulldown_compute_length(&PD, NULL);
    EXPECT(PD.width, 72);  // (cleared first: the lines are measured again)
    // A text with a bar: the part before it, the gaps and what follows.
    PD.width = 0;
    pulldown_compute_length(&PD, "ab|cd");
    EXPECT(PD.width, 12 + 2 * 12 + 12);  // ("bb" is the gap this time)
    PD.width = 36;  // (equal to the first sum, 12 + 2 * 6 + 12: not wider)
    pulldown_compute_length(&PD, "ab|cd");
    EXPECT(PD.width, 48);
    PD.width = 37;
    pulldown_compute_length(&PD, "ab|cd");
    EXPECT(PD.width, 37);
    PD.width = 1000;
    pulldown_compute_length(&PD, "ab|cd");
    EXPECT(PD.width, 1000);
    PD.width = 10;
    pulldown_compute_length(&PD, "abc");  // no bar: its width alone
    EXPECT(PD.width, 18);
    PD.width = 30;
    pulldown_compute_length(&PD, "abc");
    EXPECT(PD.width, 30);
    PD.width = 5, pulldown_compute_length(&PD, "");
    EXPECT(PD.width, 5);

    // ---- PullDown: the mouse ----
    reset_pd();
    EXPECT(pulldown_on_right_down(&PD, 3, 4), 51), expect_call(0, 0x50, &PD, 3, 4, 0);
    EXPECT(pulldown_on_right_up(&PD, 5, 6), 55), expect_call(1, 0x54, &PD, 5, 6, 0);
    // A release: outside the box nothing happens; inside, the line the point is on picks the item.
    *game_pd_font = &fnt_pd;  // (13 a line)
    margin = 4;
    PD.buf.full[0] = 0, PD.buf.full[1] = 0, PD.buf.full[2] = 50, PD.buf.full[3] = 100;
    line(0, 1, 1, "a", NULL), line(1, 2, 0, "b", NULL), line(2, 3, 1, "c", NULL), line(3, 4, 5, "sep", NULL), line(4, 5, 1, "d", NULL);
    PD.count = 5, PD.sel = 77, ncalls = 0;
    pulldown_on_left_up(&PD, 50, 10), pulldown_on_left_up(&PD, -1, 10), pulldown_on_left_up(&PD, 10, 100), pulldown_on_left_up(&PD, 10, -1);
    EXPECT(ncalls, 0), EXPECT(PD.sel, 77);
    pulldown_on_left_up(&PD, 10, 4 + 5);  // the first shown line: 0
    EXPECT(PD.sel, 0), expect_call(0, 0x8, &PD, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0;
    pulldown_on_left_up(&PD, 10, 4 + 13 + 1);  // the second shown line is line 2 (line 1 is hidden)
    EXPECT(PD.sel, 2), expect_call(0, 0x8, &PD, 0, 0, 0);
    ncalls = 0;
    pulldown_on_left_up(&PD, 10, 4 + 26 + 1);  // the third shown is a separator: no selection, a redraw
    EXPECT(PD.sel, -1), expect_call(0, 0xf8, &PD, 0, 0, 0);
    ncalls = 0;
    pulldown_on_left_up(&PD, 10, 4 + 39);  // the fourth shown is line 4
    EXPECT(PD.sel, 4), expect_call(0, 0x8, &PD, 0, 0, 0);
    ncalls = 0;
    pulldown_on_left_up(&PD, 10, 4 + 52);  // there are not that many
    EXPECT(PD.sel, -1), expect_call(0, 0xf8, &PD, 0, 0, 0);
    ncalls = 0;
    line(0, 1, 0, "a", NULL);  // the first line hidden, the mouse above the list: the first line is picked
    margin = 20;
    pulldown_on_left_up(&PD, 10, 2);  // ((2 - 20) / 13 is -1: the first hidden line has the count of -1)
    EXPECT(PD.sel, 0), expect_call(0, 0x8, &PD, 0, 0, 0);
    margin = 4;
    ncalls = 0;
    PD.count = 0, pulldown_on_left_up(&PD, 10, 30);
    EXPECT(PD.sel, -1), expect_call(0, 0xf8, &PD, 0, 0, 0);
    // A font with no first line set is as high as its ascent.
    *game_pd_font = &fnt_menu;
    PD.count = 5, line(0, 1, 1, "a", NULL), margin = 0, ncalls = 0;
    pulldown_on_left_up(&PD, 10, 9);
    EXPECT(PD.sel, 2);  // (9 is the second 9-high line: the second shown line, with line 1 hidden, is line 2)
    expect_call(0, 0x8, &PD, 0, 0, 0);

    // ---- Menu: the titles ----
    reset_menu();
    reset_pd();
    memset(&PD2, 0, sizeof PD2);
    for (int i = 0; i < 64; i++) PD2.items[i].id = -1;
    line(0, 10, 1, "A", NULL), line(1, 11, 1, "B", NULL);
    PD2.items[0].id = 20, PD2.items[1].id = 21, PD2.items[1].flags = 1;
    M.count = 2;
    M.entries[0].id = 100, M.entries[0].pd = &PD, M.entries[0].flags = 1;
    M.entries[1].id = 101, M.entries[1].pd = &PD2, M.entries[1].flags = 3;
    EXPECT(menu_id_to_index(&M, 101), 1), EXPECT(menu_id_to_index(&M, 100), 0), EXPECT(menu_id_to_index(&M, 7), -1);
    EXPECT(menu_id_to_index(&M, -1), -1);
    // The lines of a title's PullDown by the title's id.
    EXPECT(menu_hide_menu_item(&M, 100, 10), 0), EXPECT(PD.items[0].flags, 0);
    EXPECT(menu_show_menu_item(&M, 100, 10), 0), EXPECT(PD.items[0].flags, 1);
    EXPECT(menu_disable_menu_item(&M, 101, 20), 0), EXPECT(PD2.items[0].flags, 2);
    EXPECT(menu_enable_menu_item(&M, 101, 20), 0), EXPECT(PD2.items[0].flags, 0);
    EXPECT(menu_check_menu_item(&M, 100, 11), 0), EXPECT(PD.items[1].flags, 9);
    EXPECT(menu_uncheck_menu_item(&M, 100, 11), 0), EXPECT(PD.items[1].flags, 1);
    EXPECT(menu_hide_menu_item(&M, 5, 10), 0xb), EXPECT(menu_check_menu_item(&M, 5, 10), 0xb), EXPECT(menu_enable_menu_item(&M, 100, 5), 0xb);
    EXPECT(menu_unk11(&M, 101, 21), 1), EXPECT(menu_unk11(&M, 5, 21), -1), EXPECT(menu_unk11(&M, 101, 5), -1);
    EXPECT(menu_unk12(&M, 101, 1), 21), EXPECT(menu_unk12(&M, 101, 64), -1), EXPECT(menu_unk12(&M, 5, 1), -1), EXPECT(menu_unk12(&M, 101, 63), -1);
    EXPECT(menu_unk3(&M, 100, 3), 8), EXPECT(menu_unk3(&M, 5, 3), 0xb), EXPECT(menu_unk5(&M, 100, 1, 2, 3), 8), EXPECT(menu_unk5(&M, 5, 1, 2, 3), 0xb);
    EXPECT(menu_unk2(&M, 4), 0), EXPECT(menu_unk4(&M, 1, 2, 3), 0);
    // A separator: the answer is 0 whatever the PullDown says.
    PD.count = 0;
    EXPECT(menu_add_separator(&M, 100, 55), 0), EXPECT(PD.items[0].id, 10);  // (the line is there: not taken)
    PD.items[0].text = NULL, PD.count = 0;
    EXPECT(menu_add_separator(&M, 100, 55), 0), EXPECT(PD.items[0].id, 55), EXPECT(PD.items[0].flags, 5);
    EXPECT(menu_add_separator(&M, 5, 55), 0xb);
    // A title is hidden, shown, greyed, enabled, and the bar redrawn.
    ncalls = 0;
    EXPECT(menu_unk6(&M, 101), 0), EXPECT(M.entries[1].flags, 2), expect_call(0, 0xf8, &M, 0, 0, 0);
    EXPECT(menu_unk7(&M, 101), 0), EXPECT(M.entries[1].flags, 3);
    EXPECT(menu_unk9(&M, 101), 0), EXPECT(M.entries[1].flags, 1);
    EXPECT(menu_unk8(&M, 101), 0), EXPECT(M.entries[1].flags, 3);
    M.entries[1].flags = 0xf4;
    menu_unk7(&M, 101), EXPECT(M.entries[1].flags, 0xf5);
    menu_unk8(&M, 101), EXPECT(M.entries[1].flags, 0xf7);
    menu_unk6(&M, 101), EXPECT(M.entries[1].flags, 0xf6);
    menu_unk9(&M, 101), EXPECT(M.entries[1].flags, 0xf4);
    ncalls = 0;
    EXPECT(menu_unk6(&M, 5), 0xb), EXPECT(menu_unk8(&M, 5), 0xb), EXPECT(ncalls, 0);
    menu_set_menu_proc(&M, proc_fn), EXPECT(M.proc == proc_fn, 1);
    menu_on_adjust_pulldown_pos(&M, 1, 2), EXPECT(ncalls, 0);

    // The height, from the Font (the default one if it has none; not kept).
    *game_menu_font = NULL;
    EXPECT(menu_requested_height(&M), 28), EXPECT(*game_menu_font == NULL, 1);
    *game_menu_font = &fnt_menu;
    EXPECT(menu_requested_height(&M), 24);
    // Adjust: a window as wide as its parent's client area and as high as the bar, put above it.
    memset(&parent, 0, sizeof parent);
    parent.inner[0] = 10, parent.inner[1] = 50, parent.inner[2] = 210, parent.inner[3] = 150;
    M.win.parent = &parent, M.win.flags2 = 0, ncalls = 0;
    menu_adjust(&M);
    expect_call(0, 0xc, &M, 200, 24, 0), EXPECT(M.win.inner[0], 10), EXPECT(M.win.inner[1], 26), expect_call(1, 0x170, &M, 0, 0, 0), EXPECT(ncalls, 2);
    *game_menu_font = NULL, ncalls = 0;
    menu_adjust(&M);
    EXPECT(*game_menu_font == &fnt_default, 1), expect_call(0, 0xc, &M, 200, 28, 0), EXPECT(M.win.inner[1], 22);
    M.win.parent = NULL, ncalls = 0;
    menu_adjust(&M);
    expect_call(0, 0x170, &M, 0, 0, 0), EXPECT(ncalls, 1);
    *game_menu_font = NULL, default_font_slot = NULL, ncalls = 0;
    menu_adjust(&M);
    EXPECT(ncalls, 0), EXPECT(*game_menu_font == NULL, 1);

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
