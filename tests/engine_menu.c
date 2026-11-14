// Unit test for the Menu, PullDown and Spot classes (menu.c) on hand-built objects with vtables that log
// their calls, and a Font whose characters are 6 pixels wide (a 'W' 10): no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "life_test.h"
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


// ---- Drawing ----
#define STD __attribute__((stdcall))
static struct {
    int x, y;
    char s[24];
} outs[32];
static int nouts;
static int STD s_bk_mode(void *dc, int mode) { return 0; }
static uint32_t STD s_text_color(void *dc, uint32_t c) { return 0; }
static int STD s_text_out(void *dc, int x, int y, const char *s, int n) {
    if (nouts < 32) outs[nouts].x = x, outs[nouts].y = y, snprintf(outs[nouts].s, sizeof outs[nouts].s, "%.*s", n, s);
    return nouts++, 1;
}
static struct {
    int count;
    int32_t rect[4];
    int color;
} fills;
static void THIS s_fill_rect(void *win, const int32_t *r, int color) {
    fills.count++, memcpy(fills.rect, r, 16), fills.color = color;
}
enum { DW = 72, DH = 60 };
static uint8_t pix[DW * DH + 256];
static void hand_buffer(struct game_buffer *b, uint8_t *p, struct font *f) {
    memset(b, 0, sizeof *b);
    b->bits = p, b->width = DW, b->neg_height = -DH, b->pitch = DW;
    b->full[2] = DW, b->full[3] = DH, b->clip[2] = DW, b->clip[3] = DH;
    *(int32_t *)((uint8_t *)b + 0x64) = 1, *(int32_t *)((uint8_t *)b + 0x60) = 1;
    *(struct font **)((uint8_t *)b + 0x52c) = f;
    *(int32_t *)((uint8_t *)b + 0x53c) = 5;  // the text colour
}
static const uint8_t *tint_ptr;
static int32_t style[7] = {3, 7, 8, 9, 4, 3, 1}, menu_colors[3] = {20, 21, 22}, menu_colors2[2] = {23, 24};
static void *pd_hooks[4] = {NULL, NULL, (void *)12, (void *)6};
static uint8_t sdata[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
static struct game_sprite check_spr = {.data = sdata, .key = 0, .stride = 2, .width = 2, .height = 2, .full_w = 3, .full_h = 3};
static struct game_sprite *check_ptr = &check_spr;
static char gap2[] = "ab";
static struct game_sprite_scratch scratch_;
static int32_t scale[4] = {1, 1, 0, 0}, min_size_[2];
static float ratio_;
static double max_ratio_ = 4.0;

static int hook_log[8], hook_log_n;
static void __attribute__((cdecl)) item_hook(struct game_pulldown *p, int sel, int32_t *r) {
    if (hook_log_n < 8) hook_log[hook_log_n++] = sel;
}
static void __attribute__((cdecl)) frame_hook(struct game_pulldown *p) {
    if (hook_log_n < 8) hook_log[hook_log_n++] = -1;
}

static int draws_done;
static void s_do_all_draws(void) { draws_done++; }

static void draw_tests(void) {
    game_do_all_draws = s_do_all_draws;
    static int32_t cr, fg, mu, mga, mgb;
    game_copy_rem = &cr, game_fill_gap = &fg, game_mask_units = &mu, game_mask_gap_a = &mga, game_mask_gap_b = &mgb;
    game_sprite_scale = scale, game_sprite_ratio = &ratio_, game_sprite_max_ratio = &max_ratio_, game_sprite_min_size = min_size_;
    game_sprite_scratch = &scratch_;
    game_set_bk_mode = s_bk_mode, game_set_text_color = s_text_color, game_text_out = s_text_out;
    game_win_tint = &tint_ptr, game_graphic_win_fill_rect = s_fill_rect;
    static struct game_win *cur_win;
    game_win_cur = &cur_win;
    game_pd_style = style, game_pd_margin = &style[6], game_pd_hooks = pd_hooks, game_pd_check = &check_ptr;
    game_menu_colors = menu_colors, game_menu_colors2 = menu_colors2, game_menu_gap = gap2;
    *game_pd_font = &fnt_pd, *game_menu_font = &fnt_menu;
    // A PullDown: the fill and a one-line frame, then the lines (from 3 + 1 in and 1 down, 13 high): "Open" with its key
    // and a hotkey, selected (the window's fill of the rectangle from edge to edge, in the selected background); a
    // separator (two lines); a checked "Quit" (the mark, the text moved by its width); a hidden line.
    reset_pd();
    hand_buffer(&PD.buf, pix, &fnt_pd);
    static char open_t[] = "Open", key_t[] = "Ctrl+O", quit_t[] = "Quit";
    line(0, 1, 1, open_t, key_t), line(1, 2, 5, NULL, NULL), line(2, 3, 9, quit_t, NULL), line(3, 4, 0, quit_t, NULL);
    PD.items[0].hotkey = open_t + 1;
    PD.count = 4, PD.sel = 0;
    memset(pix, 1, sizeof pix);
    nouts = 0, fills.count = 0;
    pulldown_on_redraw(&PD);
    EXPECT(PD.ff38, 0);
    EXPECT(pix[30 * DW + 30], 7), EXPECT(pix[0] == 3 || pix[0] == 4, 1), EXPECT(pix[(DH - 1) * DW + 30] == 3 || pix[(DH - 1) * DW + 30] == 4, 1);
    EXPECT(fills.count, 1), EXPECT(fills.rect[0], 1), EXPECT(fills.rect[1], 1), EXPECT(fills.rect[2], 71), EXPECT(fills.rect[3], 14);
    EXPECT(fills.color, 12);
    EXPECT(pix[20 * DW + 30], 3), EXPECT(pix[21 * DW + 30], 4), EXPECT(pix[20 * DW + 2], 3);  // (the separator)
    EXPECT(pix[14 * DW + 10], 8), EXPECT(pix[14 * DW + 15], 8), EXPECT(pix[14 * DW + 17], 7), EXPECT(pix[14 * DW + 9], 7);  // (the hotkey's underline)
    int open_i = -1, quit_i = -1, key_i = -1;
    for (int i = 0; i < nouts && i < 32; i++) {
        if (!strcmp(outs[i].s, "Open") && open_i < 0) open_i = i;
        if (!strcmp(outs[i].s, "Quit") && quit_i < 0) quit_i = i;
        if (!strcmp(outs[i].s, "Ctrl+O") && key_i < 0) key_i = i;
    }
    EXPECT(open_i >= 0 && quit_i >= 0 && key_i >= 0, 1);
    EXPECT(outs[open_i].x, 4), EXPECT(outs[open_i].y, 13), EXPECT(outs[quit_i].x, 4 + 3), EXPECT(outs[quit_i].y, 13 + 26 + 13 - 13);
    EXPECT(outs[key_i].x, 68 - 36);
    // The mark (data 1 at the first pixel): at the text's left, centred in the line.
    EXPECT(pix[32 * DW + 4], sdata[0]);
    // Without a font nothing is drawn; a Buffer with none refuses it.
    *game_pd_font = NULL;
    default_font_slot = &fnt_default;
    memset(pix, 1, sizeof pix);
    fnt_default.hfont = NULL;
    pulldown_on_redraw(&PD);
    EXPECT(*game_pd_font == &fnt_default, 1);
    fnt_default.hfont = (void *)0x11;
    *game_pd_font = &fnt_pd;
    // The function that draws the frame takes over the fill and frame; the one that draws a line is told 1 or 0 and
    // the rectangle, and the fill of the selected line is left to it.
    pd_hooks[0] = (void *)item_hook, pd_hooks[1] = (void *)frame_hook;
    memset(pix, 1, sizeof pix);
    hook_log_n = 0, fills.count = 0;
    pulldown_on_redraw(&PD);
    EXPECT(PD.ff38 != 0 && PD.ff3c != 0, 1), EXPECT(pix[30 * DW + 30], 1), EXPECT(fills.count, 0);
    EXPECT(hook_log_n, 3), EXPECT(hook_log[0], -1), EXPECT(hook_log[1], 1), EXPECT(hook_log[2], 0);  // (the frame, then the lines: the selected and the checked)
    pd_hooks[0] = pd_hooks[1] = NULL;
    // pulldown_update draws the line it is given and the one it moves to (the second one is not drawn if it is the first
    // again, and a line that is not shown is not drawn), each in the place the redraw puts it, and then the game draws
    // what is waiting. The selected line has its rectangle filled; the second's hotkey is underlined from the text
    // height (the Font's, 11, here, not the line's 13) rather than the line height. The window is hidden here, so
    // nothing is marked for painting.
    PD.ff38 = 0, PD.ff3c = 0, PD.sel = 0;
    memset(pix, 1, sizeof pix);
    nouts = 0, fills.count = 0, draws_done = 0;
    pulldown_update(&PD, 0, 2);
    EXPECT(draws_done, 1), EXPECT(fills.count, 2);  // (the selected line, then line 2, cleared in the unselected background: the
    // second line's rectangle is not widened by the text space, and starts after the check mark)
    EXPECT(fills.rect[0], 7), EXPECT(fills.rect[1], 27), EXPECT(fills.rect[2], 68), EXPECT(fills.rect[3], 40), EXPECT(fills.color, style[1]);
    EXPECT(nouts >= 3, 1);
    EXPECT(strcmp(outs[0].s, "Open") == 0 || strcmp(outs[1].s, "Open") == 0 || strcmp(outs[2].s, "Open") == 0, 1);
    EXPECT(pix[14 * DW + 15], 8), EXPECT(pix[32 * DW + 4], sdata[0]);  // (the underline, the check mark of line 2)
    EXPECT(pix[30 * DW + 30], 1);  // (nothing else is filled)
    nouts = 0, fills.count = 0, draws_done = 0;
    pulldown_update(&PD, 2, 2);  // (the same line twice is drawn once)
    EXPECT(nouts, 1), EXPECT(strcmp(outs[0].s, "Quit"), 0), EXPECT(outs[0].x, 7), EXPECT(outs[0].y, 39), EXPECT(fills.count, 1);
    EXPECT(fills.rect[0], 7 - style[0]), EXPECT(fills.rect[2], 68 + style[0]), EXPECT(fills.color, style[1]);  // (as the first line: widened)
    memset(pix, 1, sizeof pix);
    pulldown_update(&PD, -1, 0);  // (as the second line: the underline one row higher, from the text height)
    EXPECT(pix[13 * DW + 15], 8), EXPECT(pix[14 * DW + 15] != 8, 1);
    memset(pix, 1, sizeof pix);
    pulldown_update(&PD, 0, -1);
    EXPECT(pix[14 * DW + 15], 8), EXPECT(pix[13 * DW + 15] != 8, 1);
    nouts = 0, draws_done = 0;
    pulldown_update(&PD, 3, -1);  // (line 3 is not shown)
    EXPECT(nouts, 0), EXPECT(draws_done, 1);
    pulldown_update(&PD, -1, -1), EXPECT(nouts, 0), EXPECT(draws_done, 2);
    nouts = 0;
    pulldown_update(&PD, -1, 1);  // (a separator: lines, no text)
    EXPECT(nouts, 0), EXPECT(pix[20 * DW + 30], 3), EXPECT(pix[21 * DW + 30], 4);

    // A Menu: the fill, then the titles in a row from the gap string's width in ("ab" is 12), the open one on its
    // background, the greyed one in the greyed colour, and the right one against the right edge.
    reset_menu();
    hand_buffer(&M.buf, pix, &fnt_menu);
    static char file_t[] = "File", edit_t[] = "Edit", help_t[] = "Help";
    static struct game_spot_rect rects[3];
    rects[1].rect[0] = 30;
    M.spots.rects = rects, M.spots.cap = 3, M.spots.count = 3;
    M.count = 3, M.open = 1;
    M.entries[0].flags = 1, M.entries[0].text = file_t, M.entries[0].hotkey = file_t;
    M.entries[1].flags = 3, M.entries[1].text = edit_t;
    M.entries[2].flags = 5, M.entries[2].text = help_t;
    memset(pix, 1, sizeof pix);
    nouts = 0;
    menu_on_redraw(&M);
    EXPECT(pix[2 * DW + 2], 20), EXPECT(pix[0], 20);
    EXPECT(pix[5 * DW + 18], 23), EXPECT(pix[5 * DW + 41], 23), EXPECT(pix[5 * DW + 17], 20), EXPECT(pix[5 * DW + 42], 20);  // (the open title: 18 to 42)
    int f = -1, e = -1, h = -1;
    for (int i = 0; i < nouts && i < 32; i++) {
        if (!strcmp(outs[i].s, "File") && f < 0) f = i;
        if (!strcmp(outs[i].s, "Edit") && e < 0) e = i;
        if (!strcmp(outs[i].s, "Help") && h < 0) h = i;
    }
    EXPECT(f >= 0 && e >= 0 && h >= 0, 1);
    EXPECT(outs[f].x, 12), EXPECT(outs[e].x, 12 + 24), EXPECT(outs[h].x, 72 - 12 - 24);
    // The hotkey of "File" (its first letter, 6 wide) is underlined from the title's left, in the Buffer's text colour.
    EXPECT(pix[(DH - (DH - 12) / 2) * DW + 12], 24), EXPECT(pix[(DH - (DH - 12) / 2) * DW + 17], 24), EXPECT(pix[(DH - (DH - 12) / 2) * DW + 11], 20);
    // Without a font of its own the default one is taken; with none at all nothing is drawn.
    *game_menu_font = NULL, default_font_slot = NULL;
    memset(pix, 1, sizeof pix);
    menu_on_redraw(&M);
    EXPECT(pix[0], 1), EXPECT(*game_menu_font == NULL, 1);
    default_font_slot = &fnt_default, *game_menu_font = &fnt_menu;
}

// ---- Life cycle ----

static int life_closes, pd_deleted_;
static void LT_THIS life_win_close(void *w) { life_closes++; }
static void LT_THIS fake_pd_delete_(void *o, int flag) { pd_deleted_ += flag; }
static void *fake_pd_vtable_[1] = {(void *)fake_pd_delete_};
static void *fake_pd_obj_ = fake_pd_vtable_;

static void test_life(void) {
    life_image_init();
    life_buffer_standins();
    life_time_standins();
    game_win_close = life_win_close;

    // Spot: room for n rectangles from the game's heap (the old room given back first); shutdown and the
    // destructor give it back and empty the list.
    struct game_spot s;
    memset(&s, 0xcc, sizeof s);
    s.rects = NULL;
    life_freed = 0;
    int got = spot_init(&s, 3);
    EXPECT(got != 0, 1), EXPECT(s.rects == (void *)(intptr_t)got, 1), EXPECT(s.cap, 3), EXPECT(s.count, 0), EXPECT(life_freed, 0);
    void *first = s.rects;
    s.count = 2;
    EXPECT(spot_init(&s, 5) != 0, 1), EXPECT(life_freed, 1), EXPECT(life_freed_has(first), 1), EXPECT(s.cap, 5), EXPECT(s.count, 0);
    void *second = s.rects;
    spot_shutdown(&s);
    EXPECT(life_freed, 2), EXPECT(life_freed_has(second), 1), EXPECT(s.rects == NULL, 1), EXPECT(s.cap, 0), EXPECT(s.count, 0);
    spot_shutdown(&s);  // (nothing held: nothing freed)
    EXPECT(life_freed, 2);
    s.rects = malloc(8), s.cap = 1, s.count = 1;
    void *third = s.rects;
    spot_dtor(&s);
    EXPECT(life_freed, 3), EXPECT(life_freed_has(third), 1), EXPECT(s.rects == NULL, 1), EXPECT(s.cap, 0);

    // A Menu: no titles (id -1, no text), nothing open (-1), its Spot list empty; the destructor frees the texts and
    // deletes each title's PullDown.
    static uint8_t obj[0xf50];
    memset(obj, 0xcc, sizeof obj);
    EXPECT(menu_ctor((struct game_menu *)obj) == (struct game_menu *)obj, 1);
    struct game_menu *m = (struct game_menu *)obj;
    EXPECT(OW(obj, 0), IMGVA(0x6700b0)), EXPECT(OW(obj, 0x444), IMGVA(0x6700a8)), EXPECT(m->proc == NULL, 1), EXPECT(m->count, 0);
    EXPECT(OW(obj, 0xa20), 0), EXPECT(m->open, -1), EXPECT(m->spots.rects == NULL, 1), EXPECT(m->spots.cap, 0);
    EXPECT(OW(obj, 0xa1c), 0xcccccccc), EXPECT(OW(obj, 0xa28), 0xcccccccc);
    for (int i = 0; i < 15; i++) {
        EXPECT(m->entries[i].id, -1), EXPECT(m->entries[i].text == NULL, 1), EXPECT(m->entries[i].flags, 0);
        EXPECT(m->entries[i].hotkey == NULL, 1), EXPECT(m->entries[i].pd == NULL, 1);
    }
    EXPECT(OW(obj, 0xa38 + 15 * 0x14), 0xcccccccc);
    char *text = malloc(8), *text2 = malloc(8);
    m->entries[0].text = text, m->entries[0].id = 5, m->entries[0].flags = 3, m->entries[0].hotkey = (char *)0x77;
    m->entries[7].text = text2, m->entries[7].pd = (struct game_pulldown *)&fake_pd_obj_;
    m->entries[14].pd = (struct game_pulldown *)&fake_pd_obj_;
    m->proc = (void *)1, m->count = 3, m->open = 2, m->spots.rects = malloc(8);
    void *spots = m->spots.rects;
    life_freed = 0, pd_deleted_ = 0, life_closes = 0;
    menu_dtor(m);
    EXPECT(life_freed, 3), EXPECT(life_freed_has(text), 1), EXPECT(life_freed_has(text2), 1), EXPECT(life_freed_has(spots), 1), EXPECT(pd_deleted_, 2);
    EXPECT(m->entries[0].id, -1), EXPECT(m->entries[0].text == NULL, 1), EXPECT(m->entries[0].flags, 0), EXPECT(m->entries[0].hotkey == NULL, 1);
    EXPECT(m->entries[7].pd == NULL, 1), EXPECT(m->proc == NULL, 1), EXPECT(m->count, 0), EXPECT(m->open, -1), EXPECT(m->spots.rects == NULL, 1);
    EXPECT(life_closes, 1), EXPECT(OW(obj, 0), IMGVA(0x66ff34));

    // A PullDown: 64 empty lines, a shown flag and the game's two font words; the destructor frees each line's
    // text and key text.
    memset(obj, 0xcc, sizeof obj);
    EXPECT(pulldown_ctor((struct game_pulldown *)obj) == (struct game_pulldown *)obj, 1);
    struct game_pulldown *p = (struct game_pulldown *)obj;
    EXPECT(OW(obj, 0), IMGVA(0x66ff40)), EXPECT(OW(obj, 0x444), IMGVA(0x66ff38)), EXPECT(p->items[0].id, -1), EXPECT(p->items[63].id, -1);
    EXPECT(p->items[63].text == NULL, 1), EXPECT(p->items[63].tail == NULL, 1), EXPECT(p->items[17].flags, 0), EXPECT(p->items[17].hotkey == NULL, 1);
    EXPECT(p->count, 0), EXPECT(p->width, 0), EXPECT(p->sel, -1), EXPECT(p->shown, 0), EXPECT(p->menu == NULL, 1), EXPECT(p->dirty, 1);
    EXPECT(p->fa14, 0), EXPECT(p->ff18, 0), EXPECT(p->ff1c, 0), EXPECT(p->ff38, (int)IMGW(0x9b7b58)), EXPECT(p->ff3c, (int)IMGW(0x9b7b5c));
    EXPECT(OW(obj, 0xf40), 0xcccccccc);
    char *line = malloc(8), *key = malloc(8), *last = malloc(8);
    p->items[2].text = line, p->items[2].tail = key, p->items[63].text = last, p->items[2].id = 4;
    p->dirty = 0, p->ff38 = 0;
    life_freed = 0, life_closes = 0;
    pulldown_dtor(p);
    EXPECT(life_freed, 3), EXPECT(life_freed_has(line), 1), EXPECT(life_freed_has(key), 1), EXPECT(life_freed_has(last), 1);
    EXPECT(p->items[2].text == NULL, 1), EXPECT(p->items[2].tail == NULL, 1), EXPECT(p->items[2].id, 4), EXPECT(p->dirty, 1);
    EXPECT(p->ff38, (int)IMGW(0x9b7b58)), EXPECT(life_closes, 1);
}

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

    draw_tests();

    test_life();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
