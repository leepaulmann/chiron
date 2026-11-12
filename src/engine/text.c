// The game's Font class and the text measuring of its Buffer class: widths of
// strings, where a line breaks, how tall wrapped text is. Reimplemented from
// terranx.exe 2.0's disassembly; the classes are called Font and Buffer in
// OpenSMACX (GPL-3.0-or-later).
//
// A Font wraps a GDI font. Measuring selects it into the game's one device
// context, asks GetTextExtentPoint32 how wide a string is, and selects the
// system font back (not the one that was selected before: the original does
// not keep it). Those Windows calls are made through the game's imports.
//
// A Buffer draws text in up to four fonts at once and measures text with
// markup in it: {...} switches to the second font, [...] to the third,
// $LINK<text=target> to the fourth for the text, and doubled brackets are
// the bracket itself. The mode and a marker state are kept in the Buffer
// (width and line breaks keep their own mode, at different offsets), so a
// measure leaves them behind; those quirks are kept.
//
// What is left to the original: the Font constructors that create GDI fonts
// (Font_init, Font_init_2, Font_ctor_2), Font_close and the destructor, the
// font class's init and close (a device context, GDI objects and the game's
// heap), the Text file class (files and heap), and every function that
// draws text (it drives the Buffer's device context and DirectDraw surface
// through the object's methods).
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shadow.h"

extern struct hook_module text_module;
#define TEXT (&text_module)

typedef struct game_buffer buffer_t;

// Where the Buffer keeps what text needs (its struct is only laid out as far as drawing reads).
enum {
    MODE_LINES = 0x514,  // the font mode while looking for a line break
    MODE_WIDTH = 0x518,  // the font mode while measuring width
    STATE = 0x51c,       // 1 after $LINK<, 2 after =, else 0
    MULTI = 0x520,       // non-zero: the text has markup
    FONTS = 0x52c,       // four Font pointers: plain and the three modes
    INDENT = 0x10,       // the indent of the first wrapped line, and what is left of it
    INDENT_LEFT = 0x14,
    LINES = 0x18,        // the lines counted by wrap_height
};

static int32_t *at(buffer_t *b, int off) { return (int32_t *)((uint8_t *)b + off); }
static struct font **fonts(buffer_t *b) { return (struct font **)((uint8_t *)b + FONTS); }

// The font in the current mode, or the first.
static struct font *mode_font(buffer_t *b, int mode_off) {
    struct font *f = fonts(b)[*at(b, mode_off)];
    return f ? f : fonts(b)[0];
}

// ---- Font ----------------------------------------------------------------------

void *TEXT_THIS font_ctor(struct font *f) {
    f->first = -1;
    f->flags = 0;
    f->hfont = NULL;
    f->ascent = f->height = 0;
    f->f18 = f->f1c = 0;
    f->name = NULL;
    return f;
}

// Does nothing and says so (the original takes four arguments and returns 1).
int TEXT_THIS font_unk1(struct font *f, int a, int b, int c) { return 1; }

// The width in pixels of the first n characters of s.
static int extent(struct font *f, const char *s, int n) {
    int32_t size[2] = {0, 0};
    void *dc = *game_font_dc;
    game_select_object(dc, f->hfont);
    game_get_text_extent(dc, s, n, size);
    game_select_object(dc, game_get_stock_object(13));  // SYSTEM_FONT
    return size[0];
}

int TEXT_THIS font_width(struct font *f, const char *s) {
    if (!s) return 0;
    return extent(f, s, (int)strlen(s));
}

// n is cut down to the length of s, but not up from below zero.
int TEXT_THIS font_width_2(struct font *f, const char *s, int n) {
    if (!s) return 0;
    int len = (int)strlen(s);
    if (!(n < len)) n = len;
    return extent(f, s, n);
}

// Where to break the first len characters of s so that it fits in *left
// pixels: at a space, or after the first word if even that is too wide and the
// slack global is 0. Returns the start of the next line, or NULL if it all
// fits; *left is what is left of the width. The slack global is added to by
// what each fitting call used.
const char *TEXT_THIS font_find_line_break(struct font *f, const char *str, int *pleft, int len) {
    int left = *pleft;
    const char *s = str;
    while (len) {
        const char *sp = memchr(s + 1, ' ', (size_t)len);
        int w;
        if (sp) {
            w = font_width_2(f, s, (int)(sp - s));
        } else {
            int n = len;
            if (s) {
                int slen = (int)strlen(s);
                if (!(len < slen)) n = slen;
                w = extent(f, s, n);
            } else {
                w = 0;
            }
        }
        left -= w;
        if (left < 0) {
            *pleft = 0;
            if (s == str) return *game_font_slack ? s : (sp ? sp + 1 : NULL);
            return s + 1;
        }
        if (!sp) goto fits;
        len -= (int)(sp - s);
        s = sp;
    }
    // Nothing was left to measure, or all of it was measured and fit.
fits:
    *game_font_slack += *pleft - left;
    *pleft = left;
    return NULL;
}

// ---- Buffer: fonts and colours ----------------------------------------------------

// Takes the four fonts used by plain text and by the three markup modes. The
// first is used only if the font has a GDI font; no font at all is an error.
int TEXT_THIS buffer_set_font(buffer_t *b, struct font *f0, struct font *f1, struct font *f2, struct font *f3) {
    if (!f0) return 3;
    if (f0->hfont) fonts(b)[0] = f0;
    fonts(b)[1] = f1;
    fonts(b)[3] = f3;
    fonts(b)[2] = f2;
    return 0;
}

// Four sets of colours, one for each mode: each takes one value for each of
// the four slots (their meaning is in the drawing, which is the original's).
static void set_colors(buffer_t *b, int first, int a, int c, int d, int e) {
    *at(b, first) = a;
    *at(b, first + 0x10) = c;
    *at(b, first + 0x20) = d;
    *at(b, first + 0x30) = e;
}
void TEXT_THIS buffer_set_text_color(buffer_t *b, int a, int c, int d, int e) { set_colors(b, 0x53c, a, c, d, e); }
void TEXT_THIS buffer_set_text_color2(buffer_t *b, int a, int c, int d, int e) { set_colors(b, 0x540, a, c, d, e); }
void TEXT_THIS buffer_set_text_color3(buffer_t *b, int a, int c, int d, int e) { set_colors(b, 0x544, a, c, d, e); }
void TEXT_THIS buffer_set_text_color_hyper(buffer_t *b, int a, int c, int d, int e) { set_colors(b, 0x548, a, c, d, e); }

// The first font is the game's default until one is set.
static struct font *first_font(buffer_t *b) {
    if (!fonts(b)[0]) fonts(b)[0] = *game_font_default;
    return fonts(b)[0];
}

int TEXT_THIS buffer_text_height(buffer_t *b) { return first_font(b)->height; }

// The height of a line: the height and the ascent, or the other number if the first is below 0.
int TEXT_THIS buffer_text_line_height(buffer_t *b) {
    struct font *f = first_font(b);
    return f->first >= 0 ? f->height + f->first : f->ascent;
}

// ---- Buffer: markup -----------------------------------------------------------------

static int is_special(char c, int with_equals) {
    return c == '{' || c == '}' || c == '[' || c == ']' || c == '$' || (with_equals && c == '=');
}

// The marker at *pp: returns the mode to go on in, and sets the marker state.
// A doubled bracket is skipped over, and the second stays as text.
static int markup(buffer_t *b, int mode_off, const char **pp, int *n) {
    const char *p = *pp;
    int mode = *at(b, mode_off), next = mode, escaped = 0;
    switch (p[0]) {
    case '=': *at(b, STATE) = 2; break;
    case '$':
        if (!strncmp(p, "$LINK<", 6)) *at(b, STATE) = 1, next = 3;
        break;
    case '{': if (p[1] == '{') escaped = 1; else next = 1; break;
    case '}': if (p[1] == '}') escaped = 1; else next = 0; break;
    case '[': if (p[1] == '[') escaped = 1; else next = 2; break;
    case ']': if (p[1] == ']') escaped = 1; else next = 0; break;
    }
    if (escaped) p++, (*n)--, next = *at(b, mode_off);
    *pp = p;
    return next;
}

// After the text up to a marker was measured: step over the marker (all of "$LINK<",
// or up to the > that ends a target), and go on in the new mode.
static void after_marker(buffer_t *b, int mode_off, const char **p, int *n, const char **seg, int next) {
    int state = *at(b, STATE);
    if (state == 1) *p += 5, *n -= 5;
    ++*p;
    *seg = *p;
    if (state == 2) {
        const char *q = strchr(*p, '>');
        next = 0;
        if (q) *n = *n + (int)(*p - q) - 1, *seg = q + 1;
        *at(b, STATE) = 0;
    }
    *at(b, mode_off) = next;
    --*n;
}

int TEXT_THIS buffer_text_width_2(buffer_t *b, const char *str, int n) {
    first_font(b);
    if (!*at(b, MULTI)) return font_width_2(fonts(b)[0], str, n);
    const char *seg = str, *p;
    int total = 0;
    for (;;) {
        p = seg;
        int with_equals = *at(b, MODE_WIDTH) == 3;
        while (n != 0 && !is_special(*p, with_equals)) p++, n--;
        if (n == 0) break;
        int next = markup(b, MODE_WIDTH, &p, &n);
        total += font_width_2(mode_font(b, MODE_WIDTH), seg, (int)(p - seg));
        after_marker(b, MODE_WIDTH, &p, &n, &seg, next);
    }
    total += font_width_2(mode_font(b, MODE_WIDTH), seg, (int)(p - seg));
    *at(b, STATE) = 0;
    return total;
}

int TEXT_THIS buffer_text_width(buffer_t *b, const char *str) {
    if (!str) return 0;
    return buffer_text_width_2(b, str, (int)strlen(str));
}

// The last c in [start, end], searching backwards (the game's own helper).
static const char *rfind(const char *start, const char *end, int c) {
    if (!start || !end) return NULL;
    intptr_t count = end - start;
    if (!count) return NULL;
    for (const char *p = end;;) {
        if (*p == (char)c) return p;
        p--;
        if (!--count) return NULL;
    }
}

// Where the line that a font broke ends: after a space, or at the start of the
// text just measured; else back at the last space.
static const char *line_end(const char *str, const char *seg, const char *r) {
    if (r[-1] == ' ' || r == seg) return r;
    return rfind(str, r, ' ');
}

const char *TEXT_THIS buffer_find_line_break(buffer_t *b, const char *str, int *pleft, int n) {
    if (!pleft) return NULL;
    first_font(b);
    if (!*at(b, MULTI)) return font_find_line_break(fonts(b)[0], str, pleft, n);
    const char *seg = str, *p;
    for (;;) {
        p = seg;
        int with_equals = *at(b, MODE_LINES) == 3;
        while (n != 0 && !is_special(*p, with_equals)) p++, n--;
        if (n == 0) break;
        int next = markup(b, MODE_LINES, &p, &n);
        const char *r = font_find_line_break(mode_font(b, MODE_LINES), seg, pleft, (int)(p - seg));
        if (r) {
            *at(b, STATE) = 0;
            return line_end(str, seg, r);
        }
        after_marker(b, MODE_LINES, &p, &n, &seg, next);
    }
    const char *r = font_find_line_break(mode_font(b, MODE_LINES), seg, pleft, (int)(p - seg));
    *at(b, STATE) = 0;
    return r ? line_end(str, seg, r) : NULL;
}

// The height of text wrapped into width pixels, line by line; the line count is left in the Buffer.
int TEXT_THIS buffer_wrap_height(buffer_t *b, const char *str, int width) {
    if (!str) return 0;
    *at(b, LINES) = 0;
    *game_font_slack = 0;
    *at(b, INDENT_LEFT) = *at(b, INDENT);
    int total = 0;
    do {
        int left = width;
        const char *next = buffer_find_line_break(b, str, &left, (int)strlen(str));
        *game_font_slack = 0;
        total += buffer_text_line_height(b);
        str = next;
        ++*at(b, LINES);
        if (*at(b, INDENT_LEFT)) width -= *at(b, INDENT_LEFT), *at(b, INDENT_LEFT) = 0;
    } while (str);
    return total;
}

// ---- Shadow checks ---------------------------------------------------------------------
//
// The measuring calls make GDI calls on the game's device context; those leave
// it as they found it (the system font selected), so a check can run both.

typedef const char *charp;
#define SHADOW_RESULT_charp(v) const char *v =
#define SHADOW_INT_charp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_charp(v) return v
#define SHADOW_TAIL_charp(call) return call
typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call

// The object and the slack global are what a check of a Buffer text function covers.
#define BUFFER_WRITES (WRITES(b, sizeof *b), WRITES(game_font_slack, 4))

SHADOW_WRITER_T(TEXT, voidp, font_ctor, (struct font * f), (f), WRITES(f, sizeof *f), "%d", 0)
SHADOW_THIS_FN(TEXT, int, font_unk1, (struct font * f, int a, int b, int c), (f, a, b, c), "%d, %d, %d", a, b, c)
SHADOW_THIS_FN(TEXT, int, font_width, (struct font * f, const char *s), (f, s), "%s", s ? s : "(null)")
SHADOW_THIS_FN(TEXT, int, font_width_2, (struct font * f, const char *s, int n), (f, s, n), "%s, %d", s ? s : "(null)", n)
SHADOW_WRITER_T(TEXT, charp, font_find_line_break, (struct font * f, const char *s, int *left, int len), (f, s, left, len),
                (WRITES(left, 4), WRITES(game_font_slack, 4)), "%s, %d, %d", s ? s : "(null)", left ? *left : -1, len)
SHADOW_WRITER_T(TEXT, int, buffer_set_font, (buffer_t * b, struct font *f0, struct font *f1, struct font *f2, struct font *f3),
                (b, f0, f1, f2, f3), WRITES((uint8_t *)b + FONTS, 16), "%d", f0 != NULL)
SHADOW_WRITER_T(TEXT, void, buffer_set_text_color, (buffer_t * b, int a, int c, int d, int e), (b, a, c, d, e),
                WRITES((uint8_t *)b + 0x53c, 0x40), "%d", a)
SHADOW_WRITER_T(TEXT, void, buffer_set_text_color2, (buffer_t * b, int a, int c, int d, int e), (b, a, c, d, e),
                WRITES((uint8_t *)b + 0x540, 0x40), "%d", a)
SHADOW_WRITER_T(TEXT, void, buffer_set_text_color3, (buffer_t * b, int a, int c, int d, int e), (b, a, c, d, e),
                WRITES((uint8_t *)b + 0x544, 0x40), "%d", a)
SHADOW_WRITER_T(TEXT, void, buffer_set_text_color_hyper, (buffer_t * b, int a, int c, int d, int e), (b, a, c, d, e),
                WRITES((uint8_t *)b + 0x548, 0x40), "%d", a)
SHADOW_WRITER_T(TEXT, int, buffer_text_height, (buffer_t * b), (b), WRITES((uint8_t *)b + FONTS, 4), "%d", 0)
SHADOW_WRITER_T(TEXT, int, buffer_text_line_height, (buffer_t * b), (b), WRITES((uint8_t *)b + FONTS, 4), "%d", 0)
SHADOW_WRITER_T(TEXT, int, buffer_text_width_2, (buffer_t * b, const char *s, int n), (b, s, n), BUFFER_WRITES, "%s, %d",
                s ? s : "(null)", n)
SHADOW_WRITER_T(TEXT, int, buffer_text_width, (buffer_t * b, const char *s), (b, s), BUFFER_WRITES, "%s", s ? s : "(null)")
SHADOW_WRITER_T(TEXT, charp, buffer_find_line_break, (buffer_t * b, const char *s, int *left, int n), (b, s, left, n),
                (BUFFER_WRITES, left ? WRITES(left, 4) : 0), "%s, %d, %d", s ? s : "(null)", left ? *left : -1, n)
SHADOW_WRITER_T(TEXT, int, buffer_wrap_height, (buffer_t * b, const char *s, int width), (b, s, width), BUFFER_WRITES,
                "%s, %d", s ? s : "(null)", width)

// ---- Sweep ---------------------------------------------------------------------------------

static uint32_t rng_state = 1;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// Text made of words and well-formed markup: the original runs past the end of a string
// that markers cut short, and its callers give it text from the game's files.
static void make_text(char *out, int words) {
    static const char *const bits[] = {"a", "word", "Mind", "x y", "  ", "WWW", "i", "{{", "}}", "[[", "]]", "$", "=", ">", "<"};
    char *o = out;
    *o++ = 'A';  // a line that starts with a space can make the original wrap forever
    for (int i = 0; i < words; i++) {
        const char *w;
        switch (rnd_to(8)) {
        case 0: o += sprintf(o, "{%s}", bits[rnd_to(6)]); continue;
        case 1: o += sprintf(o, "[%s]", bits[rnd_to(6)]); continue;
        case 2: o += sprintf(o, "$LINK<%s=%s>", bits[rnd_to(6)], bits[rnd_to(6)]); continue;
        default: w = bits[rnd_to((int)(sizeof bits / sizeof *bits))]; break;
        }
        // The bare marker characters only after something harmless, as part of a word.
        if (w[0] == '{' || w[0] == '}' || w[0] == '[' || w[0] == ']') o += sprintf(o, "%s", w);
        else if (w[0] == '$' || w[0] == '=') o += sprintf(o, "q%s", w);
        else o += sprintf(o, "%s", w);
        if (rnd_to(2)) *o++ = ' ';
    }
    *o = 0;
}

static void reset_buffer(buffer_t *b, struct font *font) {
    memset(b, 0, sizeof *b);
    if (rnd_to(2)) fonts(b)[0] = font;
    for (int i = 1; i < 4; i++) fonts(b)[i] = rnd_to(2) ? font : NULL;
    *at(b, MULTI) = rnd_to(3) != 0;
    *at(b, MODE_LINES) = rnd_to(4), *at(b, MODE_WIDTH) = rnd_to(4);
    *at(b, INDENT) = rnd_to(3) ? 0 : rnd_to(40);
    *at(b, INDENT_LEFT) = rnd_to(3) ? 0 : rnd_to(40);
    *at(b, LINES) = rnd_to(5);
}

static void text_sweep(void) {
    static buffer_t b;
    static struct font scratch;
    static char text[512];
    // The game's own default font may have no GDI font, so the sweep makes one of its own (once;
    // Font_init only makes a GDI font, it does not use the game's heap).
    static struct font mine;
    static int made;
    if (!*game_font_default || !*game_font_dc) return;  // the game has not set up its fonts yet
    if (!made) font_ctor(&mine), game_font_init(&mine, "Arial", 12, 0), made = 1;
    struct font *font = &mine;
    if (!font->hfont) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x6d2b79f5u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    check_font_ctor(&scratch);
    for (int round = 0; round < 48; round++) {
        reset_buffer(&b, font);
        // The first buffer font is left to the default when it is NULL; the original crashes
        // with no default font, which is why a font is needed above.
        make_text(text, rnd_to(18));
        int n = (int)strlen(text);
        check_font_unk1(font, round, 1, 2);
        check_font_width(font, text);
        check_font_width(font, NULL);
        check_font_width_2(font, text, rnd_to(n + 6) - 2);
        check_font_width_2(font, NULL, 3);
        for (int k = 0; k < 4; k++) {
            int left = rnd_to(round & 1 ? 40 : 400);
            *game_font_slack = rnd_to(3) ? 0 : rnd_to(30);
            check_font_find_line_break(font, text, &left, n);
            left = rnd_to(300);
            check_font_find_line_break(font, text, &left, rnd_to(n + 1));
            check_font_find_line_break(font, NULL, &left, 0);
        }
        check_buffer_set_font(&b, font, rnd_to(2) ? font : NULL, NULL, font);
        check_buffer_set_font(&b, NULL, font, font, font);
        scratch.hfont = NULL;
        check_buffer_set_font(&b, &scratch, font, font, font);
        scratch.hfont = font->hfont;
        check_buffer_set_text_color(&b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_buffer_set_text_color2(&b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_buffer_set_text_color3(&b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_buffer_set_text_color_hyper(&b, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_buffer_text_height(&b);
        fonts(&b)[0] = rnd_to(2) ? font : NULL;
        check_buffer_text_line_height(&b);
        scratch.first = rnd_to(2) ? -1 : rnd_to(5);
        fonts(&b)[0] = &scratch, scratch.hfont = font->hfont, scratch.ascent = 11, scratch.height = 14;
        check_buffer_text_line_height(&b);
        fonts(&b)[0] = font;
        scratch.first = -1;
        for (int k = 0; k < 6; k++) {
            *at(&b, STATE) = 0;
            check_buffer_text_width(&b, text);
            check_buffer_text_width_2(&b, text, rnd_to(4) ? (int)strlen(text) : 0);
            int left = rnd_to(round & 1 ? 60 : 500);
            *game_font_slack = rnd_to(3) ? 0 : rnd_to(30);
            check_buffer_find_line_break(&b, text, &left, (int)strlen(text));
            check_buffer_wrap_height(&b, text, 20 + rnd_to(300));
            if (k == 3) make_text(text, rnd_to(18));
        }
        check_buffer_text_width(&b, NULL);
        int left = 50;
        check_buffer_find_line_break(&b, text, NULL, 3);
        check_buffer_find_line_break(&b, text, &left, 0);
        check_buffer_wrap_height(&b, NULL, 50);
    }
    hooks_sweeping = sweeping;
}

static const struct hook text_hooks[] = {
    HOOK(0x618ea0, font_ctor, 0x8b, 0xc1, 0x33, 0xc9, 0xc7, 0x00, 0xff, 0xff, 0xff, 0xff),  // mov eax, ecx; xor ecx, ecx; mov [eax], -1
    HOOK(0x618f30, font_unk1, 0xb8, 0x01, 0x00, 0x00, 0x00),                                  // mov eax, 1
    HOOK(0x619280, font_width, 0x83, 0xec, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x10),              // sub esp, 8; push esi; mov esi, [esp+0x10]
    HOOK(0x6192f0, font_width_2, 0x83, 0xec, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x57, 0x8b, 0xf9),
    HOOK(0x619370, font_find_line_break, 0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x53, 0x55),
    HOOK(0x5dac70, buffer_set_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),                     // mov eax, [esp+4]; test eax, eax
    HOOK(0x5dacb0, buffer_set_text_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x5dace0, buffer_set_text_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x5dad10, buffer_set_text_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x5dad40, buffer_set_text_color_hyper, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x5dc790, buffer_text_width, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x57, 0x8b, 0xf9),       // push esi; mov esi, [esp+8]; push edi; mov edi, ecx
    HOOK(0x5dc7c0, buffer_text_width_2, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x57, 0x8b, 0xf9),
    HOOK(0x5dca80, buffer_text_height, 0x8b, 0x81, 0x2c, 0x05, 0x00, 0x00, 0x85, 0xc0),      // mov eax, [ecx+0x52c]; test eax, eax
    HOOK(0x5dcab0, buffer_text_line_height, 0x8b, 0x81, 0x2c, 0x05, 0x00, 0x00, 0x85, 0xc0),
    HOOK(0x5dc410, buffer_find_line_break, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x53, 0x56, 0x57, 0x8b, 0xf9),
    HOOK(0x5dc360, buffer_wrap_height, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x33, 0xdb),
};
struct hook_module text_module = {"text", text_hooks, HOOK_COUNT(text_hooks), text_sweep, NULL, -1};
