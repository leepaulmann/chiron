// Unit test for the EditBox and EditGroup classes (edit.c) on hand-built boxes and groups with vtables that log
// their calls and a text measure of 7 pixels a character: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/edit.h"
#include "../src/engine/game.h"
#include "life_test.h"
#include "../src/engine/strlist.h"
#include "../src/engine/text.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))
static void check_str(const char *what, const char *got, const char *want) {
    if (strcmp(got, want)) {
        printf("FAIL %s = \"%s\", expected \"%s\"\n", what, got, want);
        failures++;
    }
}
#define EXPECT_STR(expr, want) check_str(#expr, (expr), (want))
#define THIS __attribute__((thiscall))
#define STD __attribute__((stdcall))

// The calls the stand-in slots log: slot and object.
static struct {
    int slot;
    void *o;
    int a;
} calls[32];
static int ncalls;
static void rec(int slot, void *o, int a) {
    if (ncalls < 32) calls[ncalls].slot = slot, calls[ncalls].o = o, calls[ncalls].a = a;
    ncalls++;
}
static int THIS s_0x10(void *o) { return rec(0x10, o, 0), 0; }
static int THIS s_0xcc(void *o, int a) { return rec(0xcc, o, a), 0; }
static int THIS s_0xf8(void *o) { return rec(0xf8, o, 0), 0; }
static int THIS s_0x100(void *o) { return rec(0x100, o, 0), 0; }
static void *vt[0x180 / 4];

// The text measure: 7 pixels a character. The font and DC are stand-ins.
static void *STD select_object(void *dc, void *obj) { return obj; }
static void *STD stock_object(int which) { return NULL; }
static int STD text_extent(void *dc, const char *s, int n, int32_t *size) {
    size[0] = n > 0 ? 7 * n : 0, size[1] = 12;
    return 1;
}

// The Windows calls: the beep, the timers, the keyboard.
static int beeps, shift_down;
static int STD s_beep(unsigned type) { return beeps++, 1; }
static int16_t STD s_async_key(int key) { return key == 0x10 && shift_down ? (int16_t)0x8000 : 0; }
static int nsets, nkills;
static uint32_t STD s_set_timer(uint32_t hwnd, uint32_t id, uint32_t ms, void *proc) { return nsets++, 55; }
static int STD s_kill_timer(uint32_t hwnd, uint32_t id) { return nkills++, 1; }
static uint32_t STD s_set_event(uint32_t d, uint32_t r, void *p, void *u, uint32_t f) { return nsets++, 56; }
static uint32_t STD s_kill_event(uint32_t id) { return nkills++, 0; }
static void __attribute__((cdecl)) s_flush(void) {}
static void cb(void *x) { (void)x; }

static struct font font;
static struct game_editbox E;
static void *dc_slot, *font_slot;
static int32_t slack;
static uint32_t flush_flag, hwnd = 5;
static struct game_win *press[2];
static struct game_win *prev_focus;

// A box: the text, the caret, the anchor and the first character shown.
static void make_box(const char *text, int caret, int anchor, int first) {
    memset(&E, 0, sizeof E);
    E.win.vtable = vt;
    *(struct font **)((uint8_t *)&E.buf + 0x52c) = &font;
    E.buf.width = 60, E.buf.neg_height = -14;
    E.pad = 2, E.max_len = 20;
    strcpy(E.text, text);
    E.caret = caret, E.anchor = anchor, E.first = first;
    press[0] = press[1] = NULL;
    ncalls = beeps = 0;
}


// What drawing text does, as the stand-ins saw it (the text calls, where each went).
static struct {
    int x, y;
    char s[24];
} outs[16];
static int nouts;
static int STD s_bk_mode(void *dc, int mode) { return 0; }
static uint32_t STD s_text_color(void *dc, uint32_t c) { return 0; }
static int STD s_text_out(void *dc, int x, int y, const char *s, int n) {
    if (nouts < 16) outs[nouts].x = x, outs[nouts].y = y, snprintf(outs[nouts].s, sizeof outs[nouts].s, "%.*s", n, s);
    return nouts++, 1;
}

// The clipping's Windows calls.
static int regions_made;
static void *STD s_create_rgn(const int32_t *r) { return regions_made++, (void *)(intptr_t)(100 + regions_made); }
static int STD s_select_clip_rgn(void *dc, void *rgn) { return 1; }
static int STD s_delete_object(void *o) { return 1; }
static int STD s_intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) {
        memset(out, 0, 16);
        return 0;
    }
    memcpy(out, r, 16);
    return 1;
}
static int STD s_equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static void *ddraw_slot;

enum { DW = 60, DH = 14 };
static uint8_t pix[DW * DH];
static struct {
    struct game_win win;
    struct game_buffer buf;
} parent_w;
static uint8_t ppix[DW * DH];
static const uint8_t *tint_ptr;
static struct game_win *cur_win;
static void hand_buffer(struct game_buffer *b, uint8_t *p) {
    memset(b, 0, sizeof *b);
    b->bits = p, b->width = DW, b->neg_height = -DH, b->pitch = DW;
    b->full[2] = DW, b->full[3] = DH, b->clip[2] = DW, b->clip[3] = DH;
    *(int32_t *)((uint8_t *)b + 0x64) = 1, *(int32_t *)((uint8_t *)b + 0x60) = 1;  // (a device context)
    *(struct font **)((uint8_t *)b + 0x52c) = &font;
}
static void colors(struct game_buffer *b, int c) { *(int32_t *)((uint8_t *)b + 0x53c) = c; }

static void draw_tests(void) {
    game_set_bk_mode = s_bk_mode, game_set_text_color = s_text_color, game_text_out = s_text_out;
    game_win_tint = &tint_ptr, game_win_cur = &cur_win;
    game_create_rect_rgn_indirect = s_create_rgn, game_select_clip_rgn = s_select_clip_rgn, game_delete_object = s_delete_object;
    game_intersect_rect = s_intersect, game_equal_rect = s_equal, game_ddraw = &ddraw_slot;
    static int32_t cr, fg, mu, mga, mgb;
    game_copy_rem = &cr, game_fill_gap = &fg, game_mask_units = &mu, game_mask_gap_a = &mga, game_mask_gap_b = &mgb;
    // An EditBox: the fill, a one-line frame, the caret, the selection recoloured, and the text where it goes.
    make_box("abcd", 2, 2, 0);
    hand_buffer(&E.buf, pix);
    colors(&E.buf, 5);
    E.color = 7, E.border_a = 3, E.border_b = 4, E.border_w = 1, E.pad = 2, E.sel_back = 9, E.sel_text = 8;
    memset(pix, 1, sizeof pix);
    nouts = 0;
    edit_on_redraw(&E);
    EXPECT(pix[7 * DW + 30], 7), EXPECT(pix[0] == 3 || pix[0] == 4, 1), EXPECT(pix[(DH - 1) * DW + 30] == 3 || pix[(DH - 1) * DW + 30] == 4, 1);
    EXPECT(E.buf.clip[0], 2), EXPECT(E.buf.clip[1], 0), EXPECT(E.buf.clip[2], 58), EXPECT(E.buf.clip[3], DH);  // (the text's rectangle: the frame's inside, 1 line less the padding 2 on the sides)
    EXPECT(nouts, 2), EXPECT_STR(outs[0].s, "abcd");  // (the text, drawn twice: its shadow, then itself)
    // The caret (blinking) is a line the height of the text at the width of "ab" and the padding, in the text colour.
    E.blink = 1;
    memset(pix, 1, sizeof pix);
    edit_on_redraw(&E);
    EXPECT(pix[5 * DW + 16], 5), EXPECT(pix[5 * DW + 15], 7), EXPECT(pix[5 * DW + 17], 7);
    // A selection (here from the caret back to 1): "bc" is 14 wide, from the width of "a" and the padding; its
    // background colour becomes the selection's.
    E.blink = 0, E.anchor = 1, E.caret = 3;
    memset(pix, 1, sizeof pix);
    edit_on_redraw(&E);
    EXPECT(pix[5 * DW + 9], 9), EXPECT(pix[5 * DW + 22], 9), EXPECT(pix[5 * DW + 8], 7), EXPECT(pix[5 * DW + 23], 7);
    // The other way round (anchor beyond the caret) gives the same rectangle from the caret on.
    E.anchor = 3, E.caret = 1;
    memset(pix, 1, sizeof pix);
    edit_on_redraw(&E);
    EXPECT(pix[5 * DW + 9], 9), EXPECT(pix[5 * DW + 22], 9), EXPECT(pix[5 * DW + 23], 7);
    // No selection background of its own: the text colour.
    E.sel_back = -1;
    memset(pix, 1, sizeof pix);
    edit_on_redraw(&E);
    EXPECT(pix[5 * DW + 9], 5);
    // Masked: asterisks are measured and written.
    E.flags = 2, E.sel_back = 9, nouts = 0;
    edit_on_redraw(&E);
    EXPECT(nouts, 2), EXPECT_STR(outs[0].s, "****");

    // An EditGroup: the parent's pixels at the Dialog's place, the parent made current, the labels cell by cell
    // from the last, the cursor moving on and wrapping.
    static uint8_t graw[0xc00];
    static int32_t gvb[3] = {0, 0x8c, 0xaa4};
    static struct game_strnode ln[2];
    static uint8_t litem[2][16];
    static char lab[2][8] = {"ab", "cd"};
    memset(graw, 0, sizeof graw);
    struct game_editgroup *g = (struct game_editgroup *)graw;
    g->vbptr = gvb;
    uint8_t *D = graw + 0xaa4;
    struct game_strlist *list = (struct game_strlist *)(D + 0xbc);
    for (int i = 0; i < 2; i++) {
        char *tp = lab[i];
        memcpy(litem[i] + 4, &tp, sizeof tp);
        ln[i].item = litem[i], ln[i].next = &ln[(i + 1) % 2], ln[i].prev = &ln[(i + 1) % 2];
    }
    list->head = &ln[0], list->cur = &ln[0], list->count = 2, list->idx = 0;
    struct game_buffer *gb = (struct game_buffer *)(graw + 0x8c + 0x444);
    static uint8_t gpix[DW * DH];
    hand_buffer(gb, gpix);
    struct game_win *gwin = (struct game_win *)(graw + 0x8c);
    memset(&parent_w, 0, sizeof parent_w);
    hand_buffer(&parent_w.buf, ppix);
    for (int i = 0; i < DW * DH; i++) ppix[i] = (uint8_t)(i % 7 + 10);
    gwin->parent = &parent_w.win;
    *(int32_t *)(D + 0x24) = 3, *(int32_t *)(D + 0x28) = 2;
    *(int32_t *)(D + 0x34) = 4, *(int32_t *)(D + 0x40) = 2, *(int32_t *)(D + 0x44) = 6;
    *(int32_t *)(D + 0x54) = 2, *(int32_t *)(D + 0x58) = 2, *(int32_t *)(D + 0x64) = 25, *(int32_t *)(D + 0x68) = 9;
    *(int32_t *)(D + 0xcc) = 2;
    memset(gpix, 0, sizeof gpix);
    nouts = 0;
    group_on_redraw(graw + 0x8c);
    EXPECT(cur_win == &parent_w.win, 1);
    EXPECT(gpix[0], ppix[2 * DW + 3]), EXPECT(gpix[DW + 1], ppix[3 * DW + 4]);
    // (columns 25 apart from 4 + 2 in; rows 9 apart, the first at 6 / 2 + (6 - 14 - 3) / 2 down, with the font's height added)
    EXPECT(outs[0].x, 6), EXPECT(outs[4].x, 31), EXPECT(outs[0].y, 12), EXPECT(outs[2].y, 21), EXPECT(outs[6].y, 21);
    EXPECT(nouts, 8), EXPECT_STR(outs[0].s, "ab"), EXPECT_STR(outs[2].s, "cd"), EXPECT_STR(outs[4].s, "ab"), EXPECT_STR(outs[6].s, "cd");
    EXPECT(list->idx, 1), EXPECT(list->cur == &ln[1], 1);
    // No parent: nothing.
    gwin->parent = NULL, nouts = 0, cur_win = NULL;
    group_on_redraw(graw + 0x8c);
    EXPECT(nouts, 0), EXPECT(cur_win == NULL, 1);
}

// ---- Life cycle ----

static int life_closes;
static void LT_THIS life_win_close(void *w) { life_closes++; }

static void test_life(void) {
    life_image_init();
    life_buffer_standins();
    life_time_standins();
    game_win_close = life_win_close;
    static uint8_t obj[0xb80];
    memset(obj, 0xcc, sizeof obj);

    // An EditBox: its own vtables, no text, room for ten characters, the game's colours and frame, no selection;
    // its blink Time made (a resolution of 5).
    EXPECT(edit_ctor((struct game_editbox *)obj) == (struct game_editbox *)obj, 1);
    struct game_editbox *e = (struct game_editbox *)obj;
    EXPECT(OW(obj, 0), IMGVA(0x66a038)), EXPECT(OW(obj, 0x444), IMGVA(0x66a030)), EXPECT(e->text[0], 0), EXPECT(e->max_len, 10);
    EXPECT(e->border_a, (int)IMGW(0x9b90b8)), EXPECT(e->border_b, (int)IMGW(0x9b90bc)), EXPECT(e->border_w, (int)IMGW(0x6971e0));
    EXPECT(e->pad, (int)IMGW(0x6971e4)), EXPECT(e->color, (int)IMGW(0x6971dc)), EXPECT(e->sel_text, (int)IMGW(0x6971f8));
    EXPECT(e->sel_back, (int)IMGW(0x6971f4)), EXPECT(e->mods, 0), EXPECT(e->flags, 0), EXPECT(e->anchor, 0), EXPECT(e->caret, 0);
    EXPECT(e->blink, 0), EXPECT(e->first, 0), EXPECT(e->timer.resolution, 5), EXPECT(e->text[1], (char)0xcc);
    // Close starts the fields again, stops the Time and closes the GraphicWin.
    strcpy(e->text, "abc"), e->max_len = 99, e->caret = 3, e->flags = 4, e->color = 1;
    e->timer.delay = 77;
    life_closes = 0;
    edit_close(e);
    EXPECT(e->text[0], 0), EXPECT(e->text[1], 'b'), EXPECT(e->max_len, 10), EXPECT(e->caret, 0), EXPECT(e->flags, 0), EXPECT(e->color, (int)IMGW(0x6971dc));
    EXPECT(e->timer.delay, 0), EXPECT(life_closes, 1);
    // The destructor closes, unmakes the Time and the GraphicWin.
    life_closes = 0;
    edit_dtor(e);
    EXPECT(life_closes, 2), EXPECT(OW(obj, 0), IMGVA(0x66ff34));
}

int main(void) {
    game_select_object = select_object, game_get_stock_object = stock_object, game_get_text_extent = text_extent;
    dc_slot = (void *)1, game_font_dc = &dc_slot, font_slot = &font, game_font_default = &font_slot;
    font.first = -1, font.ascent = 12, font.height = 14, font.hfont = (void *)1;
    game_font_slack = &slack;
    game_message_beep = s_beep, game_get_async_key_state = s_async_key;
    game_set_timer = s_set_timer, game_kill_timer = s_kill_timer, game_time_set_event = s_set_event, game_time_kill_event = s_kill_event;
    game_time_flush = s_flush, game_time_flush_flag = &flush_flag, game_win_hwnd = &hwnd;
    game_edit_press = press, game_win_prev_focus = &prev_focus;
    vt[0x10 / 4] = s_0x10, vt[0xcc / 4] = s_0xcc, vt[0xf8 / 4] = s_0xf8, vt[0x100 / 4] = s_0x100;

    // set_text: cut to the maximum length from its end, in the caller's own string; the caret goes to the end.
    char longtext[] = "abcdefghij";
    make_box("zz", 1, 0, 1);
    E.max_len = 6;
    edit_set_text(&E, longtext);
    EXPECT_STR(E.text, "abcdef"), EXPECT_STR(longtext, "abcdef"), EXPECT(E.first, 0), EXPECT(E.anchor, 0), EXPECT(E.caret, 6);
    EXPECT(ncalls, 1), EXPECT(calls[0].slot, 0xf8), EXPECT(calls[0].o == &E, 1);
    edit_set_text(&E, NULL);
    EXPECT_STR(E.text, ""), EXPECT(E.caret, 0);
    E.max_len = 0;
    char another[] = "xyz";
    edit_set_text(&E, another);
    EXPECT_STR(E.text, ""), EXPECT_STR(another, "");

    // The timer's callback toggles the blink and redraws.
    make_box("", 0, 0, 0);
    edit_caret_callback(&E);
    EXPECT(E.blink, 1), EXPECT(ncalls, 1);
    edit_caret_callback(&E);
    EXPECT(E.blink, 0);

    // Does the text up to the caret fit? 7 pixels a character in 60 less the padding on both sides (56): seven do.
    make_box("abcdefgh", 7, 7, 0);
    EXPECT(edit_text_fits(&E), 1);
    E.caret = 8, EXPECT(edit_text_fits(&E), 0);
    E.first = 1, EXPECT(edit_text_fits(&E), 1);
    E.flags = 2, E.first = 0, E.caret = 8, EXPECT(edit_text_fits(&E), 0);  // (asterisks: the same width, measured whole)
    E.first = 2, EXPECT(edit_text_fits(&E), 1);  // (the width is of caret - first characters, but of the text from its start)
    E.caret = 7, EXPECT(edit_text_fits(&E), 1);

    // Where an x lies in the text.
    make_box("abcdefghij", 5, 5, 0);
    EXPECT(edit_text_position(&E, 1), 0);  // (left of the padding: the first shown)
    E.first = 3, EXPECT(edit_text_position(&E, 0), 3), E.first = 0;
    EXPECT(edit_text_position(&E, 2), 0);   // (x 0: one character is wider, so none)
    EXPECT(edit_text_position(&E, 22), 2);  // (20 pixels: two whole characters; one less than 3 as 3 < the caret 5)
    EXPECT(edit_text_position(&E, 100), 10);  // (past the end: all ten)
    E.caret = 2, EXPECT(edit_text_position(&E, 22), 3);  // (3 is not below the caret: no step back)
    E.first = 4, E.caret = 6;
    EXPECT(edit_text_position(&E, 2 + 14), 6);  // (from "efghij": three are over 14, so i = 3; 3 < the caret 6: 4 + 3 - 1)
    make_box("", 0, 0, 0);
    EXPECT(edit_text_position(&E, 30), 0);
    make_box("abc", 0, 0, 0);
    E.flags = 2, E.first = 1;
    EXPECT(edit_text_position(&E, 2 + 14), 4);  // (asterisks, from the start of the whole: i = 3, not below the caret 0: first + 3)

    // Typing: a character at the caret; the first character shown moves on when the caret would not show.
    make_box("", 0, 0, 0);
    EXPECT(edit_on_key_click(&E, 'a', 0), 1);
    EXPECT_STR(E.text, "a"), EXPECT(E.caret, 1), EXPECT(E.anchor, 1), EXPECT(ncalls, 1);
    for (const char *c = "bcdefg"; *c; c++) edit_on_key_click(&E, *c, 0);
    EXPECT_STR(E.text, "abcdefg"), EXPECT(E.first, 0);
    edit_on_key_click(&E, 'h', 0);
    EXPECT_STR(E.text, "abcdefgh"), EXPECT(E.first, 1);
    make_box("ac", 1, 1, 0);
    edit_on_key_click(&E, 'b', 0);
    EXPECT_STR(E.text, "abc"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2);
    EXPECT(edit_on_key_click(&E, 0x1f, 0), 0);
    EXPECT(edit_on_key_click(&E, 0, 0), 0), EXPECT(ncalls, 1);
    EXPECT(edit_on_key_click(&E, 0x20, 0), 1);
    EXPECT_STR(E.text, "ab c");
    EXPECT(edit_on_key_click(&E, -2, 0), 1);  // (a character with the top bit)
    EXPECT((unsigned char)E.text[3], 0xfe);
    // A full box beeps and changes nothing; a selection can be replaced.
    make_box("abcd", 2, 2, 0);
    E.max_len = 4;
    EXPECT(edit_on_key_click(&E, 'x', 0), 1), EXPECT(beeps, 1), EXPECT_STR(E.text, "abcd"), EXPECT(ncalls, 0);
    E.anchor = 1;  // select "b"
    EXPECT(edit_on_key_click(&E, 'x', 0), 1), EXPECT_STR(E.text, "axcd"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2), EXPECT(beeps, 1);
    make_box("hello", 3, 1, 0);  // select "el" (anchor before the caret)
    edit_on_key_click(&E, 'X', 0);
    EXPECT_STR(E.text, "hXlo"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2);
    make_box("hello", 1, 3, 0);  // the caret before the anchor
    edit_on_key_click(&E, 'X', 0);
    EXPECT_STR(E.text, "hXlo"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2);
    make_box("hello", 1, 2, 0);  // one character selected
    edit_on_key_click(&E, 'X', 0);
    EXPECT_STR(E.text, "hXllo"), EXPECT(E.caret, 2);
    {  // 255 characters
        make_box("", 0, 0, 0);
        E.max_len = 400;
        memset(E.text, 'q', 0xff), E.text[0xff] = 0, E.caret = E.anchor = 0xff;
        EXPECT(edit_on_key_click(&E, 'x', 0), 1), EXPECT(beeps, 1), EXPECT(E.text[0xff], 0);
    }

    // The caret keys. Right: one character (Ctrl: a word, to just after the next space, or to the end); Shift
    // keeps the anchor; at the end of the text a space is added (a full box beeps, with the caret already moved).
    make_box("ab cd ef", 2, 2, 0);
    EXPECT(edit_on_key_down(&E, 0x27), 1);
    EXPECT(E.caret, 3), EXPECT(E.anchor, 3), EXPECT(E.mods, 0), EXPECT(ncalls, 1);
    EXPECT(edit_on_key_down(&E, 0x27 | 0x10000), 1);
    EXPECT(E.caret, 4), EXPECT(E.anchor, 3), EXPECT(E.mods, 1);
    make_box("ab cd ef", 0, 0, 0);
    edit_on_key_down(&E, 0x27 | 0x20000);
    EXPECT(E.caret, 3), EXPECT(E.mods, 2);
    E.caret = 3, edit_on_key_down(&E, 0x27 | 0x20000);
    EXPECT(E.caret, 6);
    edit_on_key_down(&E, 0x27 | 0x20000 | 0x10000);
    EXPECT(E.caret, 8), EXPECT(E.anchor, 6), EXPECT(E.mods, 3);
    make_box("ab cd ef", 6, 6, 0);
    edit_on_key_down(&E, 0x66 | 0x20000);  // (number pad 6, Ctrl, no space ahead: to the end)
    EXPECT(E.caret, 8);
    E.mods = 0, E.anchor = 8;
    edit_on_key_down(&E, 0x27);
    EXPECT_STR(E.text, "ab cd ef "), EXPECT(E.caret, 9), EXPECT(E.anchor, 9), EXPECT(E.first, 2);
    make_box("ab cd ef", 8, 8, 0);
    E.max_len = 8, ncalls = 0;
    EXPECT(edit_on_key_down(&E, 0x27), 1);
    EXPECT(beeps, 1), EXPECT(E.caret, 9), EXPECT_STR(E.text, "ab cd ef"), EXPECT(ncalls, 0);
    // Left: Ctrl goes back to the space before the word (and lands on it), or the start.
    make_box("ab cd ef", 8, 8, 0);
    edit_on_key_down(&E, 0x25 | 0x20000);
    EXPECT(E.caret, 5), EXPECT(E.anchor, 5);
    E.caret = 6, E.anchor = 6, edit_on_key_down(&E, 0x25 | 0x20000);
    EXPECT(E.caret, 5);
    E.caret = 2, edit_on_key_down(&E, 0x64 | 0x20000);
    EXPECT(E.caret, 0);
    make_box("abcdef", 3, 3, 3);
    edit_on_key_down(&E, 0x25);
    EXPECT(E.caret, 2), EXPECT(E.first, 2), EXPECT(E.anchor, 2);
    make_box("abc", 0, 2, 0);
    edit_on_key_down(&E, 0x25);
    EXPECT(E.caret, 0), EXPECT(E.anchor, 0), EXPECT(ncalls, 1);
    make_box("abc", 0, 2, 0);
    EXPECT(edit_on_key_down(&E, 0x25 | 0x10000), 1);
    EXPECT(E.anchor, 2), EXPECT(ncalls, 0);  // (Shift at the start: nothing, not even a redraw)
    // Home and End.
    make_box("abcdefghijkl", 5, 5, 3);
    edit_on_key_down(&E, 0x24);
    EXPECT(E.caret, 0), EXPECT(E.anchor, 0), EXPECT(E.first, 0);
    edit_on_key_down(&E, 0x23);
    EXPECT(E.caret, 12), EXPECT(E.anchor, 12), EXPECT(E.first, 5);  // (the last seven characters fit)
    make_box("ab cd ef", 0, 0, 0);
    edit_on_key_down(&E, 0x23);
    EXPECT(E.caret, 8), EXPECT(E.first, 0);  // (the loop doesn't measure the whole text: as the original)
    edit_on_key_down(&E, 0x24 | 0x10000);
    EXPECT(E.caret, 0), EXPECT(E.anchor, 8);
    // Backspace: the character before the caret (the end of the text too), a selection, a beep at the start or
    // with Shift or Ctrl down.
    make_box("ab cd ef", 3, 3, 0);
    EXPECT(edit_on_key_down(&E, 0x08), 1);
    EXPECT_STR(E.text, "abcd ef"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2);
    E.caret = E.anchor = 7, edit_on_key_down(&E, 0x08);
    EXPECT_STR(E.text, "abcd e"), EXPECT(E.caret, 6);
    make_box("ab cd ef", 4, 1, 0);
    edit_on_key_down(&E, 0x08);
    EXPECT_STR(E.text, "ad ef"), EXPECT(E.caret, 1), EXPECT(E.anchor, 1);
    make_box("ab cd ef", 1, 4, 3);
    edit_on_key_down(&E, 0x08);
    EXPECT_STR(E.text, "ad ef"), EXPECT(E.caret, 1), EXPECT(E.anchor, 1), EXPECT(E.first, 1);
    make_box("ab", 0, 0, 0);
    EXPECT(edit_on_key_down(&E, 0x08), 1), EXPECT(beeps, 1), EXPECT(ncalls, 0);
    E.caret = E.anchor = 1, EXPECT(edit_on_key_down(&E, 0x08 | 0x10000), 1), EXPECT(beeps, 2), EXPECT_STR(E.text, "ab");
    EXPECT(edit_on_key_down(&E, 0x2e | 0x20000), 1), EXPECT(beeps, 3);
    // Delete: the character at the caret, or the selection; a beep at the end.
    make_box("ab cd ef", 2, 2, 0);
    edit_on_key_down(&E, 0x2e);
    EXPECT_STR(E.text, "abcd ef"), EXPECT(E.caret, 2), EXPECT(E.anchor, 2);
    make_box("ab cd ef", 1, 5, 0);
    edit_on_key_down(&E, 0x2e);
    EXPECT_STR(E.text, "a ef"), EXPECT(E.caret, 1), EXPECT(E.anchor, 1);
    make_box("ab cd ef", 5, 1, 0);
    edit_on_key_down(&E, 0x2e);
    EXPECT_STR(E.text, "a ef"), EXPECT(E.caret, 1), EXPECT(E.anchor, 1);
    make_box("ab", 2, 2, 0);
    EXPECT(edit_on_key_down(&E, 0x2e), 1), EXPECT(beeps, 1), EXPECT(ncalls, 0);
    // Tab and other keys are not taken; Tab leaves the keys alone, others record them.
    make_box("ab", 1, 1, 0);
    E.mods = 7;
    EXPECT(edit_on_key_down(&E, 9), 0), EXPECT(E.mods, 7);
    EXPECT(edit_on_key_down(&E, 0x41 | 0x10000), 0), EXPECT(E.mods, 1), EXPECT(ncalls, 0);
    EXPECT(edit_on_key_down(&E, 0x26), 0), EXPECT(E.mods, 0);
    EXPECT(edit_on_key_down(&E, 0x70000 | 0x6a), 0);

    // The mouse. A press sets the caret and (without Shift) the anchor, and takes the press if none is taken; the
    // release gives it up and tells the window it lost the capture; a drag moves the caret for the press's box.
    make_box("abcdefghij", 0, 0, 0);
    edit_on_left_down(&E, 2 + 21, 5);
    EXPECT(E.caret, 4), EXPECT(E.anchor, 4), EXPECT(press[0] == &E.win, 1), EXPECT(ncalls, 1);  // (the caret was 0: no step back)
    shift_down = 1;
    edit_on_left_down(&E, 2 + 49, 5);
    EXPECT(E.anchor, 4), EXPECT(E.caret, 8), shift_down = 0;
    ncalls = 0;
    edit_on_mouse_move(&E, 2 + 14, 5);
    EXPECT(E.caret, 2), EXPECT(ncalls, 1);  // (3 is below the caret 8: one less)
    struct game_win other;
    press[0] = &other, press[1] = &E.win, ncalls = 0;
    edit_on_mouse_move(&E, 2 + 35, 5);
    EXPECT(E.caret, 6), EXPECT(ncalls, 1);
    press[1] = NULL, ncalls = 0;
    edit_on_mouse_move(&E, 2 + 7, 5);
    EXPECT(E.caret, 6), EXPECT(ncalls, 0);
    press[0] = NULL, press[1] = &E.win;
    edit_on_mouse_move(&E, 2 + 7, 5);
    EXPECT(ncalls, 0);
    press[0] = &E.win, press[1] = &E.win;
    edit_on_left_up(&E, 2 + 7, 5);
    EXPECT(press[0] == NULL, 1), EXPECT(press[1] == NULL, 1), EXPECT(ncalls, 2), EXPECT(calls[0].slot, 0x10), EXPECT(calls[1].slot, 0xf8);
    EXPECT(E.caret, 1);
    press[0] = &other, press[1] = NULL, ncalls = 0;
    edit_on_left_up(&E, 2 + 14, 5);
    EXPECT(press[0] == &other, 1), EXPECT(ncalls, 1);
    EXPECT(E.caret, 3);

    // The focus: gained, the blink timer starts (with the box's own callback and 500 ms: SetTimer's); lost, it stops
    // and the caret is hidden.
    make_box("ab", 1, 1, 0);
    E.timer.cb = cb, E.timer.delay = 500, E.timer.resolution = 5, E.blink = 1;
    nsets = nkills = 0;
    edit_on_dialog_focus(&E, 1);
    EXPECT(nsets, 1), EXPECT(E.timer.id, 55), EXPECT(E.blink, 1), EXPECT(ncalls, 1);
    edit_on_dialog_focus(&E, 0);
    EXPECT(nkills, 1), EXPECT(E.timer.id, 0), EXPECT(E.blink, 0), EXPECT(ncalls, 2);
    // Without a function the timer isn't started.
    E.timer.cb = NULL, nsets = 0;
    edit_on_dialog_focus(&E, 5);
    EXPECT(nsets, 0), EXPECT(ncalls, 3);

    // In focus: the box and its parent (if any) are in a dialog ring (flag 0x10 of a window's second flags word in
    // the original: bit 12 of the first word).
    make_box("", 0, 0, 0);
    struct game_win parent;
    memset(&parent, 0, sizeof parent);
    EXPECT(edit_in_focus(&E), 0);
    E.win.flags = 0x1000, EXPECT(edit_in_focus(&E), 1);
    E.win.parent = &parent, EXPECT(edit_in_focus(&E), 0);
    parent.flags = 0x1000, EXPECT(edit_in_focus(&E), 1);

    // Passing the focus on: the window passes it itself, or a last box with flag 1 starts the parent's ring over.
    static struct game_win kids[3];
    static struct game_win_node nodes[3];
    memset(&parent, 0, sizeof parent);
    parent.vtable = vt;
    for (int i = 0; i < 3; i++) {
        kids[i].vtable = vt;
        nodes[i].win = &kids[i], nodes[i].next = &nodes[(i + 1) % 3], nodes[i].prev = &nodes[(i + 2) % 3];
    }
    parent.dlg_count = 3, parent.dlg_head = nodes, parent.dlg_cur = &nodes[1], parent.dlg_index = 1;
    make_box("", 0, 0, 0);
    E.win.parent = &parent;
    edit_pass_dialog_focus(&E);
    EXPECT(parent.dlg_index, 2), EXPECT(parent.dlg_cur == &nodes[2], 1);
    EXPECT(ncalls, 2), EXPECT(calls[0].slot, 0xcc), EXPECT(calls[0].o == &E, 1), EXPECT(calls[0].a, 0);
    EXPECT(calls[1].o == &kids[2], 1), EXPECT(calls[1].a, 1);
    E.flags = 1, ncalls = 0;  // (the last of the ring: back to the first, then from the parent)
    edit_pass_dialog_focus(&E);
    EXPECT(parent.dlg_index, 0), EXPECT(parent.dlg_cur == &nodes[0], 1), EXPECT(ncalls, 0);  // (the parent has no parent)
    parent.dlg_index = 1, parent.dlg_cur = &nodes[1];
    edit_pass_dialog_focus(&E);
    EXPECT(parent.dlg_index, 2);
    E.win.parent = NULL, ncalls = 0;
    edit_pass_dialog_focus(&E);
    EXPECT(ncalls, 0);

    // EditGroup: text access by index.
    static struct game_editbox gbox[2];
    static uint8_t raw[0xc00];
    static int32_t vbt[3] = {0, 0x8c, 0xaa4};
    struct game_editgroup *g = (struct game_editgroup *)raw;
    memset(raw, 0, sizeof raw);
    g->vbptr = vbt;
    for (int i = 0; i < 2; i++) {
        memset(&gbox[i], 0, sizeof gbox[i]);
        gbox[i].win.vtable = vt, gbox[i].max_len = 20;
    }
    g->edit[1] = &gbox[0], g->edit[3] = &gbox[1];
    ncalls = 0;
    char hello[] = "hello";
    group_set_text(g, hello, 1);
    EXPECT_STR(gbox[0].text, "hello"), EXPECT(gbox[0].caret, 5), EXPECT(ncalls, 1);
    group_set_text(g, hello, 2);
    EXPECT(ncalls, 1);
    EXPECT(group_get_text(g, 1) == gbox[0].text, 1), EXPECT(group_get_text(g, 0) == NULL, 1);
    char out[16] = "junk";
    group_append_text(g, out, 1);
    EXPECT_STR(out, "hello");
    out[0] = 'x', group_append_text(g, out, 0), EXPECT(out[0], 'x');
    group_append_text(g, NULL, 1);
    group_set_text_limits(g, 17);
    for (int i = 0; i < 10; i++) EXPECT(g->limit[i], 17);
    EXPECT(g->limit[0] + g->limit[9], 34);

    // Focus handlers of the group: they take the Win 0x8c into the group.
    uint8_t *win = raw + 0x8c;
    ncalls = 0;
    group_on_dialog_focus(win, 1);
    EXPECT(ncalls, 0);  // (edit[0] is none)
    g->edit[0] = &gbox[0];
    group_on_dialog_focus(win, 1);
    EXPECT(ncalls, 1), EXPECT(calls[0].slot, 0xcc), EXPECT(calls[0].o == &gbox[0], 1), EXPECT(calls[0].a, 1);
    ncalls = 0;
    group_on_dialog_focus(win, 0);
    EXPECT(ncalls, 3), EXPECT(calls[0].o == &gbox[0], 1), EXPECT(calls[1].o == &gbox[0], 1), EXPECT(calls[2].o == &gbox[1], 1);
    EXPECT(calls[0].a + calls[1].a + calls[2].a, 0);
    // Passing the focus on: to the window the Win's ring is at (slot 0x100), or the Win does it.
    struct game_win *gw = (struct game_win *)win;
    gw->vtable = vt;
    ncalls = 0;
    group_pass_dialog_focus(win);
    EXPECT(ncalls, 0);  // (no ring, no parent)
    gw->dlg_count = 3, gw->dlg_head = nodes, gw->dlg_cur = &nodes[2];
    group_pass_dialog_focus(win);
    EXPECT(ncalls, 1), EXPECT(calls[0].slot, 0x100), EXPECT(calls[0].o == &kids[2], 1);
    nodes[2].win = NULL, ncalls = 0;
    group_pass_dialog_focus(win);
    EXPECT(ncalls, 0);

    // calculate_dimensions: the Dialog's colours and fonts go to the Buffer, the widest label and text are measured
    // (7 pixels a character) and the sizes follow.
    uint8_t *D = raw + 0xaa4;
    memset(raw + 0x8c, 0, 0x444 + 0x588);
    gw->vtable = vt;
    struct game_buffer *B = (struct game_buffer *)(raw + 0x8c + 0x444);
    memset(D, 0, 0x100);
    static struct game_strlist *list;
    list = (struct game_strlist *)(D + 0xbc);
    static struct game_strnode ln[2];
    static uint8_t litem[2][16];
    static char names[2][12] = {"Name", "Faction"};
    for (int i = 0; i < 2; i++) {
        char *tp = names[i];
        memcpy(litem[i] + 4, &tp, sizeof tp);
        ln[i].item = litem[i], ln[i].next = &ln[(i + 1) % 2], ln[i].prev = &ln[(i + 1) % 2], ln[i].id = i;
    }
    list->head = &ln[0], list->cur = &ln[0], list->count = 2, list->idx = 0;
    g->text[0] = (char *)"Bob", g->text[1] = (char *)"Alexander";
    *(int32_t *)(D + 0x40) = 3, *(int32_t *)(D + 0x34) = 10, *(int32_t *)(D + 0x44) = 4, *(int32_t *)(D + 0x4c) = 1;
    *(int32_t *)(D + 0x50) = 5;
    *(int32_t *)(D + 0x7c) = 11, *(int32_t *)(D + 0x88) = 12, *(int32_t *)(D + 0x94) = 13, *(int32_t *)(D + 0xa0) = 14;
    *(struct font **)(D + 0x70) = &font;
    *(struct font **)((uint8_t *)B + 0x52c) = &font;
    EXPECT(group_calculate_dimensions(g), 0);
    EXPECT(g->label_w, 49), EXPECT(g->edit_w, 63 + 6);
    EXPECT(*(int32_t *)(D + 0x64), 10 + 2 * (4 + 3) + 69 + 49), EXPECT(*(int32_t *)(D + 0x68), 12 + 4);
    EXPECT(*(int32_t *)(D + 0x58), 2), EXPECT(*(int32_t *)(D + 0x5c), 142), EXPECT(*(int32_t *)(D + 0x54), 1);
    EXPECT(*(int32_t *)(D + 0x60), 16 * 2 + 4);
    EXPECT(*(int32_t *)((uint8_t *)B + 0x53c), 11), EXPECT(*(int32_t *)((uint8_t *)B + 0x54c), 12);
    EXPECT(list->cur == &ln[1], 1), EXPECT(list->idx, 1);  // (a walk to the last, then one step a label: the last)
    // A width asked for: columns by it; a height asked for: rows by it.
    *(int32_t *)(D + 0x20) = 2, *(int32_t *)(D + 0x2c) = 300;
    group_calculate_dimensions(g);
    EXPECT(*(int32_t *)(D + 0x5c), 300), EXPECT(*(int32_t *)(D + 0x54), 300 / 142);
    *(int32_t *)(D + 0x20) = 3, *(int32_t *)(D + 0x30) = 80;
    group_calculate_dimensions(g);
    EXPECT(*(int32_t *)(D + 0x60), 80), EXPECT(*(int32_t *)(D + 0x58), 80 / 16);
    *(int32_t *)(D + 0x20) = 0, *(int32_t *)(D + 0x2c) = 1000, *(int32_t *)(D + 0x4c) = 4;
    group_calculate_dimensions(g);
    EXPECT(*(int32_t *)(D + 0x5c), 1000), EXPECT(*(int32_t *)(D + 0x64), 250), EXPECT(*(int32_t *)(D + 0x54), 4);
    // No labels: 7, nothing changes.
    list->count = 0, *(int32_t *)(D + 0x64) = 99;
    EXPECT(group_calculate_dimensions(g), 7), EXPECT(*(int32_t *)(D + 0x64), 99);

    draw_tests();

    test_life();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
