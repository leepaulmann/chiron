// Unit test for the Font and Buffer text measuring, with stand-ins for the
// Windows calls: every character is 6 pixels wide, a 'W' 10. No game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/text.h"

static int failures;
#define EXPECT(got, want) check_(#got, (long)(got), (long)(want), __LINE__)
static void check_(const char *what, long got, long want, int line) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}

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

// What drawing text does to the device context, as the stand-ins saw it.
struct out_call {
    int x, y, n;
    char s[40];
    uint32_t color;
    void *font;
};
static struct out_call outs[32];
static int nouts, bk_modes, unlocks;
static uint32_t cur_color;
static int __attribute__((stdcall)) set_bk_mode(void *dc, int mode) {
    bk_modes += mode;
    return 0;
}
static uint32_t __attribute__((stdcall)) set_text_color(void *dc, uint32_t c) {
    uint32_t old = cur_color;
    cur_color = c;
    return old;
}
static int __attribute__((stdcall)) text_out(void *dc, int x, int y, const char *s, int n) {
    struct out_call *o = &outs[nouts++];
    o->x = x, o->y = y, o->n = n, o->color = cur_color, o->font = selected;
    snprintf(o->s, sizeof o->s, "%.*s", n, s);
    return 1;
}
static void __attribute__((thiscall)) unlock_stub(void *b) { unlocks++; }
static void *vtable[4] = {0, unlock_stub, 0, 0};
static int32_t link_table[0x2c0 / 4], text_end;
static int callback_answer, callback_calls;
static int __attribute__((cdecl)) text_callback(const char *s, int x, int y, int n) {
    callback_calls++;
    return callback_answer;
}

static struct game_buffer buf;
static struct font f1, f2;
static int32_t slack;
static void *dc_slot, *default_font_slot;

static int32_t *at(int off) { return (int32_t *)((uint8_t *)&buf + off); }
static void reset(void) {
    memset(&buf, 0, sizeof buf);
    slack = 0;
    *(struct font **)((uint8_t *)&buf + 0x52c) = &f1;
}

int main(void) {
    game_select_object = select_object, game_get_stock_object = stock_object, game_get_text_extent = text_extent;
    dc_slot = &fake_dc, game_font_dc = &dc_slot;
    default_font_slot = &f2, game_font_default = &default_font_slot;
    game_font_slack = &slack;
    game_set_bk_mode = set_bk_mode, game_set_text_color = set_text_color, game_text_out = text_out;
    game_link_rects = link_table, game_text_end_x = &text_end;

    // A Font starts empty.
    struct font f;
    memset(&f, 0x55, sizeof f);
    EXPECT(font_ctor(&f) == &f, 1);
    EXPECT(f.first, -1), EXPECT(f.flags, 0), EXPECT(f.hfont == NULL, 1), EXPECT(f.height, 0), EXPECT(f.name == NULL, 1);
    EXPECT(font_unk1(&f, 1, 2, 3), 1);

    memset(&f1, 0, sizeof f1), memset(&f2, 0, sizeof f2);
    f1.hfont = (void *)0x11, f1.first = -1, f1.ascent = 10, f1.height = 14;
    f2.hfont = (void *)0x22, f2.first = 3, f2.ascent = 9, f2.height = 11;

    // Widths: the font is selected and the system font put back.
    EXPECT(font_width(&f1, "abc"), 18), EXPECT(selected == (void *)0x100d, 1);
    EXPECT(font_width(&f1, NULL), 0);
    EXPECT(font_width_2(&f1, "abcdef", 2), 12), EXPECT(font_width_2(&f1, "ab", 9), 12), EXPECT(font_width_2(&f1, NULL, 1), 0);
    EXPECT(font_width_2(&f1, "Wa", 2), 16);

    // Line breaks: the width left comes back, and a slack is added to.
    int left = 100;
    EXPECT(font_find_line_break(&f1, "ab cd", &left, 5) == NULL, 1), EXPECT(left, 70), EXPECT(slack, 30);
    const char *t = "aaa bbb ccc";  // the words are 18, 24 and 24 wide with their spaces
    left = 50;
    slack = 0;
    const char *r = font_find_line_break(&f1, t, &left, 11);
    EXPECT(r == t + 8, 1), EXPECT(left, 0);  // the third word does not fit: the line ends after the space before it
    left = 100;
    EXPECT(font_find_line_break(&f1, t, &left, 11) == NULL, 1), EXPECT(left, 100 - 66);
    // The first word alone is too wide: after the space, or where it started if the slack is not 0.
    const char *w = "abcdef ghi";
    left = 10, slack = 0;
    EXPECT(font_find_line_break(&f1, w, &left, 10) == w + 7, 1), EXPECT(left, 0);
    left = 10, slack = 5;
    EXPECT(font_find_line_break(&f1, w, &left, 10) == w, 1);
    left = 10, slack = 0;
    EXPECT(font_find_line_break(&f1, "abcdef", &left, 6) == NULL, 1);
    left = 10, slack = 4;
    EXPECT(font_find_line_break(&f1, "abcdef", &left, 6) != NULL, 1);
    left = 7;
    EXPECT(font_find_line_break(&f1, "", &left, 0) == NULL, 1), EXPECT(left, 7);

    // Fonts and colours.
    reset();
    EXPECT(buffer_set_font(&buf, NULL, &f1, &f1, &f1), 3);
    EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f1, 1);
    struct font nothing = {0};
    EXPECT(buffer_set_font(&buf, &nothing, &f2, &f2, &f1), 0);
    EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f1, 1), EXPECT(*(void **)((uint8_t *)&buf + 0x530) == &f2, 1);
    EXPECT(*(void **)((uint8_t *)&buf + 0x534) == &f2, 1), EXPECT(*(void **)((uint8_t *)&buf + 0x538) == &f1, 1);
    buffer_set_text_color(&buf, 1, 2, 3, 4), buffer_set_text_color2(&buf, 5, 6, 7, 8);
    buffer_set_text_color3(&buf, 9, 10, 11, 12), buffer_set_text_color_hyper(&buf, 13, 14, 15, 16);
    EXPECT(*at(0x53c), 1), EXPECT(*at(0x54c), 2), EXPECT(*at(0x55c), 3), EXPECT(*at(0x56c), 4);
    EXPECT(*at(0x540), 5), EXPECT(*at(0x570), 8), EXPECT(*at(0x544), 9), EXPECT(*at(0x564), 11), EXPECT(*at(0x548), 13), EXPECT(*at(0x578), 16);

    // Heights: the default font stands in when none is set.
    reset();
    EXPECT(buffer_text_height(&buf), 14), EXPECT(buffer_text_line_height(&buf), 10);  // first < 0: the ascent
    *(void **)((uint8_t *)&buf + 0x52c) = NULL;
    EXPECT(buffer_text_height(&buf), 11), EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f2, 1);
    *(void **)((uint8_t *)&buf + 0x52c) = NULL;
    EXPECT(buffer_text_line_height(&buf), 11 + 3);

    // Text without markup is the font's width; with it, the markers take no room, and each
    // mode measures in its own font (here 1 and 2 are the same widths).
    reset();
    EXPECT(buffer_text_width(&buf, "abc"), 18), EXPECT(buffer_text_width(&buf, NULL), 0);
    *at(0x520) = 1;
    EXPECT(buffer_text_width(&buf, "ab{cd}e"), 30), EXPECT(*at(0x518), 0);
    EXPECT(buffer_text_width(&buf, "ab{{cd"), 5 * 6);  // "ab{" then "cd": the second { is skipped
    EXPECT(buffer_text_width(&buf, "a[b]c"), 18);
    EXPECT(buffer_text_width(&buf, "a{b"), 12), EXPECT(*at(0x518), 1);  // left in the mode it ended in
    *at(0x518) = 0;
    EXPECT(buffer_text_width(&buf, "x$LINK<abc=def>y"), 12 + 18);  // the target is not text
    EXPECT(*at(0x51c), 0), EXPECT(*at(0x518), 0);
    *(void **)((uint8_t *)&buf + 0x530) = &f2;
    EXPECT(buffer_text_width_2(&buf, "{W}", 3), 10);

    // Line breaks with markup break the same way, and look back for a space.
    reset();
    *at(0x520) = 1;
    left = 100;
    t = "aa {bb} cc";
    EXPECT(buffer_find_line_break(&buf, t, &left, (int)strlen(t)) == NULL, 1), EXPECT(left, 100 - 8 * 6);
    left = 30;
    r = buffer_find_line_break(&buf, t, &left, (int)strlen(t));
    EXPECT(r == t + 7, 1), EXPECT(buffer_find_line_break(&buf, NULL, NULL, 1) == NULL, 1);

    // The height of wrapped text: a line each, and the number of lines kept.
    reset();
    *at(0x520) = 0;
    EXPECT(buffer_wrap_height(&buf, "aaa bbb ccc", 40), 3 * 10), EXPECT(*at(0x18), 3);
    EXPECT(buffer_wrap_height(&buf, "aaa", 100), 10), EXPECT(*at(0x18), 1);
    EXPECT(buffer_wrap_height(&buf, NULL, 100), 0);
    EXPECT(buffer_wrap_height(&buf, "aaa bbb ccc ddd", 60), 2 * 10), EXPECT(*at(0x18), 2);
    *at(0x10) = 30;  // after the first line the width is 30 less
    EXPECT(buffer_wrap_height(&buf, "aaa bbb ccc ddd", 60), 3 * 10), EXPECT(*at(0x18), 3), EXPECT(*at(0x14), 0);


    // ---- Drawing text ----
#define OUT(i, str, ex, ey, ecolor) \
    (EXPECT(strcmp(outs[i].s, str), 0), EXPECT(outs[i].x, ex), EXPECT(outs[i].y, ey), EXPECT(outs[i].color, (uint32_t)(ecolor)))
    // The device context: a Buffer with a DIB hands out its memory one, counting users.
    reset();
    *at(0x64) = 0x1234;
    EXPECT((intptr_t)buffer_get_hdc(&buf), 0x1234), EXPECT(*at(0x60), 0x1234), EXPECT(*at(0x68), 1);
    buffer_get_hdc(&buf);
    EXPECT(*at(0x68), 2);
    buffer_release_hdc(&buf, 1);
    EXPECT(*at(0x68), 1), EXPECT(*at(0x60), 0x1234);
    buffer_release_hdc(&buf, 1);
    EXPECT(*at(0x68), 0), EXPECT(*at(0x60), 0);
    buf.vtable = vtable, buf.base = (uint8_t *)1;  // locked pixels are let go of first
    buffer_get_hdc(&buf);
    EXPECT(unlocks, 1);
    buf.base = NULL;
    buffer_release_hdc(&buf, 5);
    EXPECT(*at(0x68), 0);

    // One run of text: at y plus the font's height; the colour is a palette-index-like value.
    reset();
    *at(0x64) = 0x1234;
    *at(0x53c) = 0x41, *at(0x54c) = -1;
    nouts = bk_modes = 0;
    EXPECT(buffer_write_raw_l(&buf, "abcd", 10, 20, 4), 10 + 24), EXPECT(nouts, 1);
    OUT(0, "abcd", 10, 20 + 14, 0x10ff0041);
    EXPECT(bk_modes, 1), EXPECT(selected == (void *)0x100d, 1), EXPECT(*at(0x68), 0), EXPECT(*at(0x60), 0);
    EXPECT(outs[0].font == f1.hfont, 1);
    // A shadow is drawn first, in its own colour, and moved by its offsets.
    nouts = 0;
    *at(0x54c) = 0x7, *at(0x55c) = 1, *at(0x56c) = 2;
    buffer_write_raw_l(&buf, "ab", 10, 20, 2);
    EXPECT(nouts, 2), OUT(0, "ab", 11, 20 + 14 + 2, 0x10ff0007), OUT(1, "ab", 10, 20 + 14, 0x10ff0041);
    // Colours from the Buffer's palette: the first three bytes of the entry.
    nouts = 0;
    *(uint8_t *)at(0x1c) = 4;
    memcpy((uint8_t *)&buf + 0xa4 + 4 * 7, "\x11\x22\x33\x00", 4);
    memcpy((uint8_t *)&buf + 0xa4 + 4 * 0x41, "\x44\x55\x66\x00", 4);
    buffer_write_raw_l(&buf, "ab", 10, 20, 2);
    OUT(0, "ab", 11, 36, 0x112233), OUT(1, "ab", 10, 34, 0x445566);
    *(uint8_t *)at(0x1c) = 0;
    // Nothing to draw: the x back, and the Buffer's device context left alone. A callback can draw instead.
    nouts = 0;
    EXPECT(buffer_write_raw_l(&buf, "ab", 10, 20, 0), 10), EXPECT(nouts, 0), EXPECT(*at(0x68), 0);
    *(void **)at(0x8) = text_callback;
    callback_answer = 77;
    EXPECT(buffer_write_raw_l(&buf, "ab", 10, 20, 2), 77), EXPECT(nouts, 0), EXPECT(callback_calls, 1);
    callback_answer = 0;
    EXPECT(buffer_write_raw_l(&buf, "ab", 10, 20, 2), 10 + 12), EXPECT(nouts, 2);
    *(void **)at(0x8) = NULL;
    *at(0x64) = 0;  // no device context: the x back
    nouts = 0;
    EXPECT(buffer_write_raw_l(&buf, "ab", 10, 20, 2), 10), EXPECT(nouts, 0);
    *at(0x64) = 0x1234;

    // Text with markup: each run in the font of its mode, the brackets taken out, y below the
    // tallest of the three fonts.
    reset();
    *at(0x64) = 0x1234;
    *at(0x520) = 1;
    f2.hfont = (void *)0x22, f2.height = 20;
    *(struct font **)((uint8_t *)&buf + 0x530) = &f2;
    *at(0x53c) = 1, *at(0x540) = 2, *at(0x54c) = -1, *at(0x550) = -1;
    nouts = 0;
    EXPECT(buffer_write_multi_font_raw_l(&buf, "ab{cd}e", 5, 0, 7), 5 + 5 * 6), EXPECT(nouts, 3);
    OUT(0, "ab", 5, 20, 0x10ff0001), OUT(1, "cd", 5 + 12, 20, 0x10ff0002), OUT(2, "e", 5 + 24, 20, 0x10ff0001);
    EXPECT(outs[0].font == f1.hfont, 1), EXPECT(outs[1].font == f2.hfont, 1), EXPECT(*at(0x510), 0);
    nouts = 0;
    buffer_write_multi_font_raw_l(&buf, "a{{b", 0, 0, 4);  // one { is drawn, the other skipped
    EXPECT(nouts, 2), EXPECT(strcmp(outs[0].s, "a{"), 0), EXPECT(strcmp(outs[1].s, "b"), 0);
    nouts = 0;
    buffer_write_multi_font_raw_l(&buf, "x{y", 0, 0, 3);  // ends in the second font
    EXPECT(nouts, 2), EXPECT(*at(0x510), 1);
    *at(0x510) = 0;
    *at(0x520) = 0;
    nouts = 0;
    buffer_write_multi_font_raw_l(&buf, "a{b}", 0, 0, 4);  // no markup: all of it, brackets too
    EXPECT(nouts, 1), EXPECT(strcmp(outs[0].s, "a{b}"), 0);

    // Links in the text: ~1ab~ is link 1, drawn in its colour with its rectangle kept.
    reset();
    *at(0x64) = 0x1234;
    *at(0x520) = 1, *at(0x524) = 1, *at(0x528) = -1;
    *at(0x53c) = 3, *at(0x54c) = -1;
    memset(link_table, 0, sizeof link_table);
    *(uint16_t *)((uint8_t *)link_table + 0x30 + 0x2c) = 0x99;
    nouts = 0;
    int endx = buffer_write_raw_l(&buf, "xy ~1ab~ z", 0, 0, 10);
    EXPECT(nouts, 3), OUT(0, "xy ", 0, 14, 0x10ff0003), OUT(1, "ab", 18, 14, 0x10ff0099), OUT(2, " z", 30, 14, 0x10ff0003);
    EXPECT(link_table[(3 + 0) * 4], 18), EXPECT(link_table[(3 + 0) * 4 + 1], 14);
    EXPECT(link_table[(3 + 0) * 4 + 2], 30), EXPECT(link_table[(3 + 0) * 4 + 3], 14 + 14), EXPECT(*at(0x528), -1);
    EXPECT(endx, 10 * 6 + 30);  // the original adds the width of all of it to where the text left off
    // A link that is not ended goes on in the next call, whose rectangle is the second.
    nouts = 0;
    buffer_write_raw_l(&buf, "xy ~1ab", 0, 0, 7);
    EXPECT(*at(0x528), 1), EXPECT(nouts, 2);
    nouts = 0;
    buffer_write_raw_l(&buf, "cd~ e", 40, 0, 5);
    EXPECT(nouts, 2), OUT(0, "cd", 40, 14, 0x10ff0099), OUT(1, " e", 52, 14, 0x10ff0003);
    EXPECT(link_table[(3 + 1) * 4], 40), EXPECT(link_table[(3 + 1) * 4 + 2], 52), EXPECT(*at(0x528), -1);

    // Lines of text: the length is cut to the string, nothing to draw gives x, no font 3.
    reset();
    *at(0x64) = 0x1234;
    *at(0x53c) = 1, *at(0x54c) = -1;
    nouts = 0;
    EXPECT(buffer_write_l_3(&buf, "abc", 10, 0, 9), 10 + 18), EXPECT(strcmp(outs[0].s, "abc"), 0);
    EXPECT(buffer_write_l_3(&buf, "abc", 10, 0, 2), 10 + 12), EXPECT(strcmp(outs[1].s, "ab"), 0);
    EXPECT(buffer_write_l_3(&buf, "abc", 10, 0, 0), 10), EXPECT(buffer_write_l_3(&buf, "abc", 10, 0, -4), 10);
    EXPECT(buffer_write_l_3(&buf, NULL, 10, 0, 3), 10), EXPECT(nouts, 2);
    *(struct font **)((uint8_t *)&buf + 0x52c) = &nothing;
    EXPECT(buffer_write_l_3(&buf, "abc", 10, 0, 3), 3);
    *(struct font **)((uint8_t *)&buf + 0x52c) = &f1;
    // With a font of its own for the call, put back after (and the others dropped meanwhile).
    *(struct font **)((uint8_t *)&buf + 0x530) = &f2;
    nouts = 0;
    EXPECT(buffer_write_l(&buf, &f2, "ab", 0, 0, 2), 12), EXPECT(outs[0].font == f2.hfont, 1), EXPECT(outs[0].y, 20);
    EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f1, 1), EXPECT(*(void **)((uint8_t *)&buf + 0x530) == &f2, 1);
    EXPECT(buffer_write_l(&buf, &nothing, "ab", 0, 0, 2), 12), EXPECT(outs[1].font == f1.hfont, 1);
    // A rectangle: centred vertically, and nothing to draw gives 0.
    int32_t rect[4] = {10, 100, 110, 160};
    nouts = 0;
    EXPECT(buffer_write_l_4(&buf, "ab", rect, 2), 10 + 12), EXPECT(outs[0].y, 100 + (160 - 14 - 100) / 2 + 14);
    EXPECT(buffer_write_l_4(&buf, "ab", rect, 0), 0), EXPECT(buffer_write_l_4(&buf, NULL, rect, 2), 0);
    EXPECT(buffer_write_l_4(&buf, "ab", NULL, 2), 0);
    // Centred and right: x moves by what is left of the width.
    nouts = 0;
    EXPECT(buffer_write_cent_l_5(&buf, "abcd", 10, 0, 50, 4), 10 + 13 + 24), EXPECT(outs[0].x, 10 + (50 - 24) / 2);
    EXPECT(buffer_write_cent_l_5(&buf, "abcd", 10, 0, 20, 4), 10 - 2 + 24), EXPECT(outs[1].x, 10 + (20 - 24) / 2);  // towards zero
    EXPECT(buffer_write_cent_l_8(&buf, "abcd", 100, 0, 4), 100 - 12 + 24), EXPECT(outs[2].x, 88);
    EXPECT(buffer_write_right_l_4(&buf, "abcd", 10, 0, 50, 4), 10 + 26 + 24), EXPECT(outs[3].x, 36);
    EXPECT(buffer_write_right_l_5(&buf, "abcd", 100, 0, 4), 100), EXPECT(outs[4].x, 76);
    EXPECT(buffer_write_cent_l_5(&buf, NULL, 7, 0, 50, 4), 7), EXPECT(buffer_write_right_l_5(&buf, "", 7, 0, 4), 7);
    // In a rectangle: the width is the whole string's, the height the font's.
    nouts = 0;
    EXPECT(buffer_write_cent_l_7(&buf, "abcd", rect, 2), 10 + (100 - 24) / 2 + 12), EXPECT(outs[0].x, 10 + (100 - 24) / 2);
    EXPECT(outs[0].y, 100 + (60 - 14) / 2 + 14);
    EXPECT(buffer_write_cent_l_6(&buf, "ab", 10, 100, 100, 60, 2), 10 + (100 - 12) / 2 + 12);
    EXPECT(buffer_write_right_l_6(&buf, "abcd", rect, 4), 110), EXPECT(outs[2].x, 110 - 24);
    EXPECT(buffer_write_right_l_6(&buf, "abcd", rect, 0), 0), EXPECT(buffer_write_cent_l_7(&buf, NULL, rect, 1), 0);
    *(struct font **)((uint8_t *)&buf + 0x52c) = &nothing;
    EXPECT(buffer_write_right_l_6(&buf, "abcd", rect, 4), 3), EXPECT(buffer_write_cent_l_5(&buf, "a", 1, 2, 3, 1), 3);
    *(struct font **)((uint8_t *)&buf + 0x52c) = &f1;
    // The functions with a font put it in for the call.
    nouts = 0;
    EXPECT(buffer_write_cent_l_4(&buf, &f2, "abcd", 100, 0, 4), 88 + 24), EXPECT(outs[0].font == f2.hfont, 1);
    EXPECT(buffer_write_right_l_2(&buf, &f2, "ab", 100, 0, 2), 100), EXPECT(buffer_write_cent_l_3(&buf, &f2, "ab", 10, 100, 100, 60, 2), 10 + 44 + 12);
    EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f1, 1);

    // Wrapped text: a line each, the y below the last, the x the last ended at in a global.
    reset();
    *at(0x64) = 0x1234;
    *at(0x53c) = 1, *at(0x54c) = -1;
    nouts = 0;
    EXPECT(buffer_wrap_3(&buf, "aaa bbb ccc", 5, 100, 40), 100 + 3 * 10), EXPECT(nouts, 3), EXPECT(*at(0x18), 3);
    EXPECT(strcmp(outs[0].s, "aaa "), 0), EXPECT(strcmp(outs[2].s, "ccc"), 0), EXPECT(outs[1].y, 100 + 10 + 14);
    EXPECT(text_end, 5 + 18), EXPECT(buffer_wrap_3(&buf, "aaa", 5, 100, 0), 100), EXPECT(buffer_wrap_3(&buf, NULL, 5, 100, 40), 100);
    // The indent: the first line starts that much further in, the later ones are that much wider.
    *at(0x10) = 20;
    nouts = 0;
    EXPECT(buffer_wrap_3(&buf, "aaa bbb ccc", 5, 100, 40), 100 + 30), EXPECT(outs[0].x, 5), EXPECT(outs[1].x, 25), EXPECT(*at(0x14), 0);
    *at(0x10) = 0;
    int32_t wr[4] = {10, 50, 50, 90};
    nouts = 0;
    EXPECT(buffer_wrap_2(&buf, "aaa bbb ccc", wr), 50 + 30), EXPECT(outs[0].x, 10), EXPECT(buffer_wrap_2(&buf, NULL, wr), 50);
    nouts = 0;
    EXPECT(buffer_wrap_cent_4(&buf, "aaa bbb", 5, 100, 40), 100 + 20), EXPECT(outs[0].x, 5 + (40 - 24) / 2), EXPECT(*at(0x18), 2);
    EXPECT(buffer_wrap_cent_5(&buf, "aaa bbb", 5, 100, 40, 7), 100 + 14), EXPECT(buffer_wrap_cent_6(&buf, "aaa bbb ccc", wr), 50 + 30);
    EXPECT(buffer_wrap_cent_4(&buf, NULL, 5, 100, 40), 100);
    nouts = 0;
    EXPECT(buffer_wrap_right_3(&buf, "aaa bbb", 80, 100, 40), 100 + 20), EXPECT(outs[0].x, 80 - 24), EXPECT(buffer_wrap_right_4(&buf, "aaa bbb", wr), 50 + 20);
    EXPECT(outs[2].x, 10 - 24);  // the original puts them at the left
    EXPECT(buffer_wrap_right_3(&buf, NULL, 1, 100, 40), 100);
    EXPECT(buffer_wrap(&buf, &f2, "aaa bbb", wr), 50 + 2 * 23), EXPECT(buffer_wrap_cent_2(&buf, &f2, "aaa bbb", 5, 100, 40, 9), 100 + 18);
    EXPECT(buffer_wrap_cent(&buf, &f2, "aaa bbb", 5, 100, 40), 100 + 2 * 23), EXPECT(buffer_wrap_right(&buf, &f2, "aaa", 5, 100, 40), 100 + 23);
    EXPECT(buffer_wrap_right_2(&buf, &f2, "aaa", wr), 50 + 23), EXPECT(buffer_wrap_cent_3(&buf, &f2, "aaa", wr), 50 + 23);
    EXPECT(*(void **)((uint8_t *)&buf + 0x52c) == &f1, 1);

    printf(failures ? "text: %d failures\n" : "text: all passed\n", failures);
    return failures != 0;
}
