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

    printf(failures ? "text: %d failures\n" : "text: all passed\n", failures);
    return failures != 0;
}
