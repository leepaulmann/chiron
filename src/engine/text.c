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
// heap), the Text file class (files and heap), $LINK< hyperlinks in drawn
// text (they keep targets on the game's heap), and the functions that
// draw strings from the game's string lists (write_strings, the "flying"
// wraps).
//
// Drawing text (write_raw_l and the write_ and wrap_ functions around it)
// gets the Buffer's device context, then selects the font, sets the colour
// and calls TextOut, as the original does, through the game's imports. A
// Buffer with a DirectDraw surface is not checked in shadow mode (the
// original runs alone there).
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

// ---- Buffer: the device context ----------------------------------------------------------

// Where the Buffer keeps what drawing text needs.
enum {
    DRAW_HOOK = 0x8,       // a function that may draw the text itself: cdecl (text, x, y, length), non-zero if it did
    BUF_FLAGS = 0x1c,         // bit 2: colours are indexes into the Buffer's own palette
    DC_NOW = 0x60,           // the device context in use, 0 when none
    MEM_DC = 0x64,        // the memory device context of a Buffer with a DIB
    DC_COUNT = 0x68,      // how many users the device context has
    PALETTE = 0xa4,       // 256 entries of 4 bytes (the first three are the colour)
    DRAW_MODE = 0x510,    // the font mode text is drawn in
    TILDE_LINKS = 0x524,  // non-zero: the text has ~N link~ markers
    TILDE_LINK = 0x528,   // the link a call ended in the middle of, or -1
    COLORS = 0x53c,       // colour slot 0 of mode 0; slots are 0x10 apart, modes 4
    SHADOW_COLOR = 0x54c,
    SHADOW_DX = 0x55c,
    SHADOW_DY = 0x56c,
};

typedef void(THISCALL *buffer_method)(void *);
typedef int(__attribute__((stdcall)) * dd_get_dc)(void *surface, void **dc);
typedef int(__attribute__((stdcall)) * dd_release_dc)(void *surface, void *dc);

static void *dd_method(buffer_t *b, int offset) { return (*(void ***)b->dd)[offset / 4]; }

// The Buffer's second virtual method, which the original calls to let go of the pixels before
// it asks for a device context, and after a DirectDraw call that failed.
static void buffer_unlock(buffer_t *b) { ((buffer_method)b->vtable[1])(b); }

// The device context to draw in: a Buffer with a DIB has its memory device context, one with a
// DirectDraw surface asks the surface (the first time) for its own. Counts a user.
void *TEXT_THIS buffer_get_hdc(buffer_t *b) {
    if (b->base) buffer_unlock(b);
    if (!b->dd) {
        *at(b, DC_NOW) = *at(b, MEM_DC);
        ++*at(b, DC_COUNT);
        return (void *)(intptr_t)*at(b, MEM_DC);
    }
    if (!*at(b, DC_NOW)) {
        if (((dd_get_dc)dd_method(b, 0x44))(b->dd, (void **)at(b, DC_NOW))) buffer_unlock(b);
    }
    ++*at(b, DC_COUNT);
    return (void *)(intptr_t)*at(b, DC_NOW);
}

// Lets go of n users of it; the last gives the device context back.
void TEXT_THIS buffer_release_hdc(buffer_t *b, int n) {
    int left = *at(b, DC_COUNT) - n;
    *at(b, DC_COUNT) = left;
    if (!b->dd) {
        if (left <= 0) *at(b, DC_NOW) = 0, *at(b, DC_COUNT) = 0;
        return;
    }
    int dc = *at(b, DC_NOW);
    if (!dc || left > 0) return;
    if (((dd_release_dc)dd_method(b, 0x68))(b->dd, (void *)(intptr_t)dc)) buffer_unlock(b);
    *at(b, DC_COUNT) = 0;
    *at(b, DC_NOW) = 0;
}

// ---- Buffer: one run of text ------------------------------------------------------------------

static void *cur_dc(buffer_t *b) { return (void *)(intptr_t)*at(b, DC_NOW); }

// The colour handed to Windows for a colour slot: an entry of the Buffer's palette (its first
// byte ends up as the red of a COLORREF's blue, which the original does not mind), or the
// low 16 bits of the slot in a form no real palette has.
static uint32_t text_colorref(buffer_t *b, int32_t slot) {
    if (*(uint8_t *)at(b, BUF_FLAGS) & 4) {
        const uint8_t *e = (uint8_t *)b + PALETTE + (intptr_t)slot * 4;
        return (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
    }
    return (uint16_t)slot | 0x10ff0000u;
}

static int32_t color_slot(buffer_t *b, int base, int mode) { return *at(b, base + 4 * mode); }

// What ends a run of text: the system font back, and the device context let go.
static void end_draw(buffer_t *b) {
    game_select_object(cur_dc(b), game_get_stock_object(13));  // SYSTEM_FONT
    buffer_release_hdc(b, 1);
}

typedef int(__attribute__((cdecl)) * text_callback)(const char *s, int x, int y, int n);

// Draws n characters of s in the current mode's font and colours (shadow first, if the mode has
// one) at x, and at y plus the height of the font (the tallest of the first three when there is
// markup). Returns the x the text ends at, or what the callback in the Buffer says if it draws
// the text instead and answers non-zero. If the Buffer's link flag is set, text may hold
// ~N link~ markers: the linked part is drawn in link N's colour, and the rectangle it took is
// saved for the game to find. A link not ended by the call is remembered for the next. The
// original reads its rectangle's bottom from a stack slot it only sets when there is markup, so
// that is where links are only drawn. Differences from OpenSMACX: none known (the function is
// not in it).
int TEXT_THIS buffer_write_raw_l(buffer_t *b, const char *s, int x, int y, int n) {
    text_callback cb = *(text_callback *)at(b, DRAW_HOOK);
    if (cb) {
        int r = cb(s, x, y, n);
        if (r) return r;
    }
    if (!n) return x;
    if (!buffer_get_hdc(b)) return x;
    game_set_bk_mode(cur_dc(b), 1);
    int fidx = 0, cidx = 0, maxh = 0;
    if (!*at(b, MULTI)) {
        y += fonts(b)[0]->height;
    } else {
        int mode = *at(b, DRAW_MODE);
        fidx = fonts(b)[mode] ? mode : 0;
        cidx = color_slot(b, COLORS, mode) != -1 ? mode : 0;
        for (int k = 0; k < 3; k++)
            if (fonts(b)[k] && fonts(b)[k]->height > maxh) maxh = fonts(b)[k]->height;
        y += maxh;
    }
    game_select_object(cur_dc(b), fonts(b)[fidx]->hfont);
    int32_t shadow = color_slot(b, SHADOW_COLOR, cidx);
    if (shadow != -1) {
        game_set_text_color(cur_dc(b), text_colorref(b, shadow));
        game_text_out(cur_dc(b), color_slot(b, SHADOW_DX, cidx) + x, color_slot(b, SHADOW_DY, cidx) + y, s, n);
    }
    int32_t color = color_slot(b, COLORS, cidx);
    if (!*at(b, TILDE_LINKS)) {
        game_set_text_color(cur_dc(b), text_colorref(b, color));
        game_text_out(cur_dc(b), x, y, s, n);
        end_draw(b);
        return font_width_2(fonts(b)[fidx], s, n) + x;
    }
    const char *p = s;
    int left = n, cur_x = x, flag = 0, link, len;
    const char *tilde;
    uint8_t *table = (uint8_t *)game_link_rects;  // per link: three rectangles of 16 bytes, then a colour
    if (*at(b, TILDE_LINK) >= 0) {
        link = *at(b, TILDE_LINK);
        *at(b, TILDE_LINK) = -1;
        flag = 1;  // stays set for the links after it in this call too
        goto in_link;
    }
    for (;;) {
        tilde = strchr(p, '~');
        len = tilde ? (int)(tilde - p) : left;
        game_set_text_color(cur_dc(b), text_colorref(b, color));
        game_text_out(cur_dc(b), cur_x, y, p, len);
        if (!tilde) {
            end_draw(b);
            return font_width_2(fonts(b)[fidx], s, n) + cur_x;  // the width of all of it, as the original does
        }
        link = atoi(tilde + 1);
        cur_x += font_width_2(fonts(b)[fidx], p, len);
        p += len + 2;  // the ~ and the digit
        left -= len + 2;
    in_link:
        tilde = strchr(p, '~');
        if (tilde) len = (int)(tilde - p);
        else len = left, *at(b, TILDE_LINK) = link;
        game_set_text_color(cur_dc(b), *(uint16_t *)(table + link * 0x30 + 0x2c) | 0x10ff0000u);
        game_text_out(cur_dc(b), cur_x, y, p, len);
        int w = font_width_2(fonts(b)[fidx], p, len);
        int32_t *r = (int32_t *)(table + (3 * link + flag) * 16);
        r[0] = cur_x, r[1] = y, r[2] = cur_x + w, r[3] = maxh + y;
        cur_x += w;
        p += len + 1;  // the ~
        left -= len + 1;
        if (*at(b, TILDE_LINK) >= 0) break;
    }
    end_draw(b);
    return font_width_2(fonts(b)[fidx], s, n) + cur_x;
}

// Text the original has to draw itself: $LINK<text=target> hyperlinks keep a copy of the target
// on the game's heap and put up a warning when there are too many, and a call can end in the
// middle of one.
static int link_text(buffer_t *b, const char *s) {
    if (!*at(b, MULTI)) return 0;
    return *at(b, DRAW_MODE) == 3 || *at(b, STATE) || (s && strstr(s, "$LINK<"));
}

SHADOW_DECLARE_T(int, buffer_write_multi_font_raw_l, (buffer_t * b, const char *s, int x, int y, int n))

// Draws text with markup: {...}, [...] switch the font mode, and a doubled bracket is the
// bracket (one is drawn, the other skipped; a lone $ is skipped). Each run between markers is
// drawn by write_raw_l in the mode it is in, and the x the last ends at is returned. Hyperlinks
// ($LINK<...) are left to the original.
int TEXT_THIS buffer_write_multi_font_raw_l(buffer_t *b, const char *s, int x, int y, int n) {
    if (!*at(b, MULTI)) return buffer_write_raw_l(b, s, x, y, n);
    if (link_text(b, s)) return orig_buffer_write_multi_font_raw_l(b, s, x, y, n);
    const char *seg = s, *p;
    for (;;) {
        p = seg;
        while (n != 0 && !is_special(*p, 0)) p++, n--;
        if (n == 0) break;
        int mode = *at(b, DRAW_MODE), next = mode, escaped = 0;
        switch (*p) {
        case '{': if (p[1] == '{') escaped = 1; else next = 1; break;
        case '}': if (p[1] == '}') escaped = 1; else next = 0; break;
        case '[': if (p[1] == '[') escaped = 1; else next = 2; break;
        case ']': if (p[1] == ']') escaped = 1; else next = 0; break;
        }
        if (escaped) p++, n--;
        x = buffer_write_raw_l(b, seg, x, y, (int)(p - seg));
        seg = ++p;
        *at(b, DRAW_MODE) = next;
        n--;
    }
    return buffer_write_raw_l(b, seg, x, y, (int)(p - seg));
}

// ---- Buffer: lines of text in a font, centred, right-aligned ------------------------------------

static int half(int v) { return v / 2; }  // towards zero: the original's cdq, sub, sar

static int has_font(buffer_t *b) { return fonts(b)[0] && fonts(b)[0]->hfont; }

// How many characters to draw: n or the length of s, whichever is less; 0 for nothing.
static int draw_length(const char *s, int n) {
    int len = (int)strlen(s), m = len < n ? len : n;
    return m < 0 ? 0 : m;
}

// The first three fonts are kept while one is set for a call, and put back after. A font with
// no GDI font is not set. Setting a font clears the other two.
struct saved_fonts {
    struct font *f[3];
};
static void font_push(buffer_t *b, struct font *f, struct saved_fonts *s) {
    for (int i = 0; i < 3; i++) s->f[i] = fonts(b)[i];
    if (f && f->hfont) fonts(b)[0] = f, fonts(b)[1] = NULL, fonts(b)[2] = NULL;
}
static void font_pop(buffer_t *b, const struct saved_fonts *s) {
    for (int i = 0; i < 3; i++) fonts(b)[i] = s->f[i];
}

// The functions that give back x when there is nothing to draw, 3 when there is no font, and
// draw the text where they are told.
int TEXT_THIS buffer_write_l_3(buffer_t *b, const char *s, int x, int y, int n) {
    if (!s) return x;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return x;
    return buffer_write_multi_font_raw_l(b, s, x, y, m);
}

int TEXT_THIS buffer_write_l(buffer_t *b, struct font *font, const char *s, int x, int y, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_l_3(b, s, x, y, n);
    font_pop(b, &sv);
    return r;
}

// Centred vertically in a rectangle (left, top, right, bottom). Gives back 0 for nothing to draw.
int TEXT_THIS buffer_write_l_4(buffer_t *b, const char *s, const int32_t *rect, int n) {
    if (!s || !rect || !has_font(b)) return 0;
    int m = draw_length(s, n);
    if (!m) return 0;
    int top = rect[1];
    int y = top + half(rect[3] - fonts(b)[0]->height - top);
    return buffer_write_multi_font_raw_l(b, s, rect[0], y, m);
}

int TEXT_THIS buffer_write_l_2(buffer_t *b, struct font *font, const char *s, const int32_t *rect, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_l_4(b, s, rect, n);
    font_pop(b, &sv);
    return r;
}

// Centred on a width of w from x.
int TEXT_THIS buffer_write_cent_l_5(buffer_t *b, const char *s, int x, int y, int w, int n) {
    if (!s) return x;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return x;
    int tw = buffer_text_width_2(b, s, m);
    return buffer_write_multi_font_raw_l(b, s, x + half(w - tw), y, m);
}

// Centred in a rectangle both ways. The width is that of the whole string, not of the n drawn.
int TEXT_THIS buffer_write_cent_l_7(buffer_t *b, const char *s, const int32_t *rect, int n) {
    if (!s || !rect) return 0;
    int left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return 0;
    int tw = buffer_text_width_2(b, s, (int)strlen(s));
    int x = left + half(right - tw - left);
    int y = top + half(bottom - fonts(b)[0]->height - top);
    return buffer_write_multi_font_raw_l(b, s, x, y, m);
}

// Centred on x.
int TEXT_THIS buffer_write_cent_l_8(buffer_t *b, const char *s, int x, int y, int n) {
    if (!s) return x;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return x;
    int tw = buffer_text_width_2(b, s, m);
    return buffer_write_multi_font_raw_l(b, s, x - (tw >> 1), y, m);
}

// The rectangle's own corners are x, y and x + w, y + h.
int TEXT_THIS buffer_write_cent_l_6(buffer_t *b, const char *s, int x, int y, int w, int h, int n) {
    int32_t rect[4] = {x, y, x + w, y + h};
    return buffer_write_cent_l_7(b, s, rect, n);
}

int TEXT_THIS buffer_write_cent_l(buffer_t *b, struct font *font, const char *s, int x, int y, int w, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_cent_l_5(b, s, x, y, w, n);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_write_cent_l_2(buffer_t *b, struct font *font, const char *s, const int32_t *rect, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_cent_l_7(b, s, rect, n);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_write_cent_l_3(buffer_t *b, struct font *font, const char *s, int x, int y, int w, int h, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int32_t rect[4] = {x, y, x + w, y + h};
    int r = buffer_write_cent_l_7(b, s, rect, n);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_write_cent_l_4(buffer_t *b, struct font *font, const char *s, int x, int y, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_cent_l_8(b, s, x, y, n);
    font_pop(b, &sv);
    return r;
}

// Right-aligned in a width of w from x.
int TEXT_THIS buffer_write_right_l_4(buffer_t *b, const char *s, int x, int y, int w, int n) {
    if (!s) return x;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return x;
    int tw = buffer_text_width_2(b, s, m);
    return buffer_write_multi_font_raw_l(b, s, x + (w - tw), y, m);
}

// Ending at x.
int TEXT_THIS buffer_write_right_l_5(buffer_t *b, const char *s, int x, int y, int n) {
    if (!s) return x;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return x;
    int tw = buffer_text_width_2(b, s, m);
    return buffer_write_multi_font_raw_l(b, s, x - tw, y, m);
}

// Ending at the right of a rectangle, centred vertically in it.
int TEXT_THIS buffer_write_right_l_6(buffer_t *b, const char *s, const int32_t *rect, int n) {
    if (!s || !rect) return 0;
    if (!has_font(b)) return 3;
    int m = draw_length(s, n);
    if (!m) return 0;
    int tw = buffer_text_width_2(b, s, m);
    int x = rect[2] - tw;
    int top = rect[1];
    int y = top + half(rect[3] - fonts(b)[0]->height - top);
    return buffer_write_multi_font_raw_l(b, s, x, y, m);
}

int TEXT_THIS buffer_write_right_l(buffer_t *b, struct font *font, const char *s, int x, int y, int w, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_right_l_4(b, s, x, y, w, n);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_write_right_l_2(buffer_t *b, struct font *font, const char *s, int x, int y, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_right_l_5(b, s, x, y, n);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_write_right_l_3(buffer_t *b, struct font *font, const char *s, const int32_t *rect, int n) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_write_right_l_6(b, s, rect, n);
    font_pop(b, &sv);
    return r;
}

// ---- Buffer: wrapped text ----------------------------------------------------------------------
//
// Text drawn line by line (the breaks are buffer_find_line_break's), each line a run of
// write_l; the Buffer counts the lines, and the functions give back the y below the last.

// Left-aligned in a width of w from x; the first line may be indented (the Buffer's indent,
// which makes the following lines wider). The x each line ends at is left in a global, and a
// line that could not be drawn leaves 3 there (no font) or its start x (nothing to draw).
static int wrap_left(buffer_t *b, const char *s, int x, int y, int w) {
    *game_font_slack = 0;
    *at(b, LINES) = 0;
    *at(b, INDENT_LEFT) = *at(b, INDENT);
    do {
        int left = w;
        *game_font_slack = 0;
        const char *next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        int len = next ? (int)(next - s) : (int)strlen(s), r;
        if (!has_font(b)) {
            r = 3;
        } else {
            int m = draw_length(s, len);
            r = m ? buffer_write_multi_font_raw_l(b, s, x, y, m) : x;
        }
        *game_text_end_x = r;
        y += buffer_text_line_height(b);
        ++*at(b, LINES);
        if (*at(b, INDENT_LEFT)) {
            int il = *at(b, INDENT_LEFT);
            *at(b, INDENT_LEFT) = 0;
            w -= il, x += il;
        }
        s = next;
    } while (s);
    return y;
}

int TEXT_THIS buffer_wrap_2(buffer_t *b, const char *s, const int32_t *rect) {
    if (!s || !rect) return rect ? rect[1] : 0;
    return wrap_left(b, s, rect[0], rect[1], rect[2] - rect[0]);
}

int TEXT_THIS buffer_wrap_3(buffer_t *b, const char *s, int x, int y, int w) {
    if (!s || !w) return y;
    return wrap_left(b, s, x, y, w);
}

int TEXT_THIS buffer_wrap(buffer_t *b, struct font *font, const char *s, const int32_t *rect) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_2(b, s, rect);
    font_pop(b, &sv);
    return r;
}

// Centred lines, each step below the last, or a line's height below it if height is set.
static int wrap_centered(buffer_t *b, const char *s, int x, int y, int w, int height, int step) {
    *game_font_slack = 0;
    *at(b, LINES) = 0;
    do {
        int left = w;
        *game_font_slack = 0;
        const char *next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        buffer_write_cent_l_5(b, s, x, y, w, next ? (int)(next - s) : (int)strlen(s));
        ++*at(b, LINES);
        y += height ? buffer_text_line_height(b) : step;
        s = next;
    } while (s);
    return y;
}

int TEXT_THIS buffer_wrap_cent_4(buffer_t *b, const char *s, int x, int y, int w) {
    return s ? wrap_centered(b, s, x, y, w, 1, 0) : y;
}

int TEXT_THIS buffer_wrap_cent_5(buffer_t *b, const char *s, int x, int y, int w, int step) {
    return s ? wrap_centered(b, s, x, y, w, 0, step) : y;
}

int TEXT_THIS buffer_wrap_cent_6(buffer_t *b, const char *s, const int32_t *rect) {
    if (!s || !rect) return rect ? rect[1] : 0;
    return wrap_centered(b, s, rect[0], rect[1], rect[2] - rect[0], 1, 0);
}

int TEXT_THIS buffer_wrap_cent(buffer_t *b, struct font *font, const char *s, int x, int y, int w) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_cent_4(b, s, x, y, w);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_wrap_cent_2(buffer_t *b, struct font *font, const char *s, int x, int y, int w, int step) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_cent_5(b, s, x, y, w, step);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_wrap_cent_3(buffer_t *b, struct font *font, const char *s, const int32_t *rect) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_cent_6(b, s, rect);
    font_pop(b, &sv);
    return r;
}

// Right-aligned lines, ending at x, in a width of w; the indent works as in wrap_left.
static int wrap_right(buffer_t *b, const char *s, int x, int y, int w) {
    *game_font_slack = 0;
    *at(b, LINES) = 0;
    *at(b, INDENT_LEFT) = *at(b, INDENT);
    do {
        int left = w;
        *game_font_slack = 0;
        const char *next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        buffer_write_right_l_5(b, s, x, y, next ? (int)(next - s) : (int)strlen(s));
        ++*at(b, LINES);
        y += buffer_text_line_height(b);
        if (*at(b, INDENT_LEFT)) {
            w -= *at(b, INDENT_LEFT);
            *at(b, INDENT_LEFT) = 0;
        }
        s = next;
    } while (s);
    return y;
}

int TEXT_THIS buffer_wrap_right_3(buffer_t *b, const char *s, int x, int y, int w) {
    return s ? wrap_right(b, s, x, y, w) : y;
}

// The original draws each line ending at the rectangle's left, not its right.
int TEXT_THIS buffer_wrap_right_4(buffer_t *b, const char *s, const int32_t *rect) {
    if (!s || !rect) return rect ? rect[1] : 0;
    return wrap_right(b, s, rect[0], rect[1], rect[2] - rect[0]);
}

int TEXT_THIS buffer_wrap_right(buffer_t *b, struct font *font, const char *s, int x, int y, int w) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_right_3(b, s, x, y, w);
    font_pop(b, &sv);
    return r;
}

int TEXT_THIS buffer_wrap_right_2(buffer_t *b, struct font *font, const char *s, const int32_t *rect) {
    struct saved_fonts sv;
    font_push(b, font, &sv);
    int r = buffer_wrap_right_4(b, s, rect);
    font_pop(b, &sv);
    return r;
}

// ---- Buffer: "flying" text and lists of strings -------------------------------------------------------
//
// Text laid out over several calls. A call to the wrap_flying functions says where the first
// line goes and how wide it may be; each call after that adds text to the line being filled
// (the pieces are joined with a gap) until it is full, when the line is drawn and the next
// started below it. A flush draws what is left. The queue lives in the game's globals: the
// x and y of the line, the width left for it (0 means nothing is queued: calls do nothing),
// the text so far, and the width the text of the line has used (the same slack that
// find_line_break adds to). The height variants only count: they leave the lines (the Buffer
// counts them) and the y below the last. The indent works as in wrap_left.
//
// A list of strings (the game's own circular list class, whose few methods the original inlines
// in places) holds entries with a text and a kind: 0 is running text, anything else starts a
// block of its own after the text before is flushed (2: centred, wrapped; 3: a heading line in
// the first font; others: flying text again, from a new place).

// Where the list keeps its parts: a head (non-zero when it has entries), the node it stands at
// (a ring: each node has the key handed to the Buffer's list callback, an entry and the next
// node), how many entries there are and which one it stands at.

enum { LIST_HOOK = 0xc };  // a callback in the Buffer: cdecl (key, kind), called as the list steps to an entry

static const char *list_text(struct str_list *l) { return l->head ? l->cur->entry->text : NULL; }
static int list_kind(struct str_list *l) { return l->head ? l->cur->entry->kind : 0; }
static void list_advance(struct str_list *l) {
    if (!l->head) return;
    l->cur = l->cur->next;
    if (++l->index == l->count) l->index = 0;
}
static void list_rewind(struct str_list *l, int flags) {
    if (!(flags & 2) && l->count - 1 >= 0) l->index = 0, l->cur = l->head;
}
static void list_callback(buffer_t *b, struct str_list *l, int kind) {
    typedef int(__attribute__((cdecl)) * cb_t)(int key, int kind);
    cb_t cb = *(cb_t *)at(b, LIST_HOOK);
    if (cb) cb(l->head ? l->cur->key : 0, kind);
}

// Back to plain text: the three modes and the marker state.
static void reset_modes(buffer_t *b) {
    *at(b, DRAW_MODE) = *at(b, MODE_WIDTH) = *at(b, MODE_LINES) = 0;
    *at(b, STATE) = 0;
}

// Start a queue: the line starts at x, y and may be w wide.
static void queue_start(buffer_t *b, int x, int y, int w) {
    *at(b, LINES) = 0;
    *game_flying_x = x;
    *game_flying_y = y;
    *game_flying_left = w;
    *game_font_slack = 0;
    game_flying_text[0] = 0;
}

// -- the height of flying text

// Adds s to the queue, line by line; gives back the y below what is queued.
int TEXT_THIS buffer_wrap_height_flying_2(buffer_t *b, const char *s) {
    if (!*game_flying_left) return 0;
    if (!s) return *game_flying_y;
    first_font(b);
    if (!*s) return *game_flying_y;
    for (;;) {
        int left = *game_flying_left - *game_font_slack;
        const char *next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        if (!next) break;
        ++*at(b, LINES);
        *game_flying_y += buffer_text_line_height(b);
        *game_font_slack = 0;
        if (*at(b, INDENT_LEFT)) *game_flying_left -= *at(b, INDENT_LEFT), *at(b, INDENT_LEFT) = 0;
        s = next;
    }
    *game_font_slack += buffer_text_width_2(b, game_flying_pad_height, (int)strlen(game_flying_pad_height));
    return *game_flying_y;
}

// Starts a queue of width w (the y counts from 0) and adds s.
int TEXT_THIS buffer_wrap_height_flying(buffer_t *b, const char *s, int w) {
    *at(b, INDENT_LEFT) = *at(b, INDENT);
    *at(b, LINES) = 0;
    *at(b, STATE) = 0;
    *game_flying_y = 0;
    *game_font_slack = 0;
    *game_flying_left = w;
    return buffer_wrap_height_flying_2(b, s);
}

// Ends the queue (a line that has anything in it counts); gives back the y below it.
int TEXT_THIS buffer_wrap_height_flying_flush(buffer_t *b) {
    if (*game_font_slack) {
        *game_flying_y += buffer_text_line_height(b);
        ++*at(b, LINES);
    }
    *game_flying_left = 0;
    reset_modes(b);
    return *game_flying_y;
}

// -- drawing flying text

// Draws what is queued in the first font at the line's x and y (nothing without a GDI font).
static void flying_draw(buffer_t *b) {
    buffer_write_l_3(b, game_flying_text, *game_flying_x, *game_flying_y, (int)strlen(game_flying_text));
}

// Adds s to the queue: each time the line is full it is drawn and the next started below; the
// rest of s waits in the queue. Gives back the y of the line being filled.
int TEXT_THIS buffer_wrap_flying_2(buffer_t *b, const char *s) {
    if (!*game_flying_left) return 0;
    if (!s || !*s) return *game_flying_y;
    for (;;) {
        if (game_flying_text[0]) {
            strcat(game_flying_text, game_flying_gap);
            *game_font_slack += buffer_text_width_2(b, game_flying_pad, (int)strlen(game_flying_pad));
        }
        const char *next;
        if (*game_font_slack < *game_flying_left) {
            int left = *game_flying_left - *game_font_slack;
            next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        } else {
            next = s;  // no room for any more: draw what is there
        }
        if (!next) {
            strcat(game_flying_text, s);
            return *game_flying_y;
        }
        size_t used = strlen(game_flying_text), n = (size_t)(next - s);
        memmove(game_flying_text + used, s, n);
        game_flying_text[used + n] = 0;
        flying_draw(b);
        game_flying_text[0] = 0;
        *game_font_slack = 0;
        ++*at(b, LINES);
        *game_flying_y += buffer_text_line_height(b);
        if (*at(b, INDENT_LEFT)) {
            *game_flying_left -= *at(b, INDENT_LEFT);
            *game_flying_x += *at(b, INDENT_LEFT);
            *at(b, INDENT_LEFT) = 0;
        }
        s = next;
    }
}

// Starts a queue at x, y of width w, and adds s.
int TEXT_THIS buffer_wrap_flying(buffer_t *b, const char *s, int x, int y, int w) {
    *at(b, INDENT_LEFT) = *at(b, INDENT);
    *at(b, STATE) = 0;
    queue_start(b, x, y, w);
    return buffer_wrap_flying_2(b, s);
}

// Draws the line that is left and ends the queue; gives back the y of that line (the line's own
// height is added only if something was drawn).
int TEXT_THIS buffer_wrap_flying_flush(buffer_t *b) {
    if (game_flying_text[0]) {
        flying_draw(b);
        *game_flying_y += buffer_text_line_height(b);
        ++*at(b, LINES);
    }
    game_flying_text[0] = 0;
    *game_flying_left = 0;
    reset_modes(b);
    return *game_flying_y;
}

// -- the same, each line centred in the width

static void flying_draw_cent(buffer_t *b) {
    buffer_write_cent_l_5(b, game_flying_text, *game_flying_x, *game_flying_y, *game_flying_left, (int)strlen(game_flying_text));
}

// As wrap_flying_2, but centred (every line is drawn, font or not, and the indent is not used).
int TEXT_THIS buffer_wrap_cent_flying_2(buffer_t *b, const char *s) {
    if (!*game_flying_left) return 0;
    if (!s || !*s) return *game_flying_y;
    for (;;) {
        if (game_flying_text[0]) {
            strcat(game_flying_text, game_flying_gap);
            *game_font_slack += buffer_text_width_2(b, game_flying_pad_cent, (int)strlen(game_flying_pad_cent));
        }
        const char *next;
        if (*game_font_slack < *game_flying_left) {
            int left = *game_flying_left - *game_font_slack;
            next = buffer_find_line_break(b, s, &left, (int)strlen(s));
        } else {
            next = s;
        }
        if (!next) {
            strcat(game_flying_text, s);
            return *game_flying_y;
        }
        size_t used = strlen(game_flying_text), n = (size_t)(next - s);
        memmove(game_flying_text + used, s, n);
        game_flying_text[used + n] = 0;
        flying_draw_cent(b);
        game_flying_text[0] = 0;
        *game_font_slack = 0;
        ++*at(b, LINES);
        *game_flying_y += buffer_text_line_height(b);
        s = next;
    }
}

// Starts a queue of centred lines at x, y of width w (the indent and the marker state are left as
// they were), and adds s.
int TEXT_THIS buffer_wrap_cent_flying(buffer_t *b, const char *s, int x, int y, int w) {
    queue_start(b, x, y, w);
    return buffer_wrap_cent_flying_2(b, s);
}

// Draws the line that is left; gives back the y below it, but leaves the queue's y at that line.
int TEXT_THIS buffer_wrap_cent_flying_flush(buffer_t *b) {
    if (game_flying_text[0]) {
        flying_draw_cent(b);
        ++*at(b, LINES);
    }
    game_flying_text[0] = 0;
    *game_flying_left = 0;
    reset_modes(b);
    return *game_flying_y + buffer_text_line_height(b);
}

// -- lists of strings

// The height of a list of strings laid out in a width, as write_strings would draw them.
// Unless flags has bit 1 set the list is rewound first, and it is left standing at its last
// entry.
int TEXT_THIS buffer_write_strings_height(buffer_t *b, struct str_list *list, int width, int flags) {
    if (!list) return 0;
    list_rewind(list, flags);
    int kind = list_kind(list);
    list_callback(b, list, kind);
    int total = 0;
    if (kind == 3) {
        first_font(b);
        total = buffer_text_line_height(b);
        *at(b, INDENT_LEFT) = *at(b, INDENT), *at(b, LINES) = 0, *at(b, STATE) = 0;
        *game_flying_y = 0, *game_flying_left = width, *game_font_slack = 0;
        buffer_wrap_height_flying_2(b, NULL);
    } else {
        const char *text = list_text(list);
        if (kind == 2) {
            total = text ? buffer_wrap_height(b, text, width) : 0;
            text = NULL;
        }
        *at(b, INDENT_LEFT) = *at(b, INDENT), *at(b, LINES) = 0, *at(b, STATE) = 0;
        *game_flying_y = 0, *game_flying_left = width, *game_font_slack = 0;
        buffer_wrap_height_flying_2(b, text);
    }
    while (list->index < list->count - 1) {
        list_advance(list);
        kind = list_kind(list);
        list_callback(b, list, kind);
        if (list->head && kind != 0) {
            total += buffer_wrap_height_flying_flush(b);
            if (kind == 3) {
                total += buffer_text_line_height(b);
                buffer_wrap_height_flying(b, NULL, width);
            } else if (kind == 2) {
                total += buffer_wrap_height(b, list_text(list), width);
                buffer_wrap_height_flying(b, NULL, width);
            } else {
                buffer_wrap_height_flying(b, list_text(list), width);
            }
        } else {
            buffer_wrap_height_flying_2(b, list_text(list));
        }
    }
    return total + buffer_wrap_height_flying_flush(b);
}

// Draws a list of strings from x, y in a width of w, down to the bottom of the Buffer, and gives
// back the y below the lowest.
int TEXT_THIS buffer_write_strings(buffer_t *b, struct str_list *list, int x, int y, int w, int flags) {
    if (!list) return y;
    list_rewind(list, flags);
    int kind = list_kind(list), bottom = y;
    list_callback(b, list, kind);
    if (kind == 3) {
        first_font(b);
        bottom = y + buffer_text_line_height(b);
        *at(b, INDENT_LEFT) = *at(b, INDENT), *at(b, LINES) = 0, *at(b, STATE) = 0;
        queue_start(b, x, bottom, w);
        buffer_wrap_flying_2(b, NULL);
    } else {
        const char *text = list_text(list);
        if (kind == 2) {
            if (text) {
                *at(b, LINES) = 0;
                const char *p = text;
                do {
                    *game_font_slack = 0;
                    int left = w;
                    const char *next = buffer_find_line_break(b, p, &left, (int)strlen(p));
                    buffer_write_cent_l_5(b, p, x, bottom, w, next ? (int)(next - p) : (int)strlen(p));
                    ++*at(b, LINES);
                    bottom += buffer_text_line_height(b);
                    p = next;
                } while (p);
            }
            text = NULL;
        }
        *at(b, INDENT_LEFT) = *at(b, INDENT), *at(b, LINES) = 0, *at(b, STATE) = 0;
        queue_start(b, x, bottom, w);
        buffer_wrap_flying_2(b, text);
    }
    while (list->index < list->count - 1 && bottom <= -b->neg_height) {
        list_advance(list);
        kind = list_kind(list);
        if (list->head && kind != 0) {
            if (game_flying_text[0]) {
                buffer_write_l_3(b, game_flying_text, *game_flying_x, *game_flying_y, (int)strlen(game_flying_text));
                *game_flying_y += buffer_text_line_height(b);
                ++*at(b, LINES);
            }
            game_flying_text[0] = 0;
            *game_flying_left = 0;
            reset_modes(b);
            int line = *game_flying_y;
            list_callback(b, list, kind);
            if (line >= bottom) bottom = line;
            if (kind == 3) {
                first_font(b);
                bottom += buffer_text_line_height(b);
                buffer_wrap_flying(b, NULL, x, bottom, w);
            } else if (kind == 2) {
                bottom = buffer_wrap_cent_4(b, list_text(list), x, bottom, w);
                buffer_wrap_flying(b, NULL, x, bottom, w);
            } else {
                buffer_wrap_flying(b, list_text(list), x, bottom, w);
            }
        } else {
            list_callback(b, list, kind);
            buffer_wrap_flying_2(b, list_text(list));
        }
    }
    int r = buffer_wrap_flying_flush(b);
    return r >= bottom ? r : bottom;
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

// ---- Shadow checks: drawing text ----------------------------------------------------------------
//
// Drawing changes the Buffer's object (the device context in use and its count), the pixels of
// the rows the text can reach, the device context itself (its text colour, mode and font live in
// Chiron's GDI), and a few globals (the hyperlink rectangles, the slack and the x a wrapped line
// ended at). A call is only checked for a Buffer with a DIB: the original's use of a DirectDraw
// surface can't be put back.

static int abs_int(int v) { return v < 0 ? -v : v; }

static int64_t font_extent(const struct font *f) {
    return f ? (int64_t)abs_int(f->height) + abs_int(f->ascent) + abs_int(f->first) : 0;
}

// The pixel rows from y0 to y1 and what the text can reach around and below them: lines more of
// them, each a line of the tallest font and a step. Whole rows, so the width does not matter.
static void writes_text(struct hook_writes *w, buffer_t *b, int y0, int y1, int lines, int step) {
    hooks_writes_add(w, b, sizeof *b);
    hooks_writes_add(w, game_flying_text, (size_t)((uint8_t *)game_font_slack + 4 - (uint8_t *)game_flying_text));
    hooks_writes_dc(w, (void *)(intptr_t)*at(b, MEM_DC));
    if (*at(b, DC_NOW) != *at(b, MEM_DC)) hooks_writes_dc(w, (void *)(intptr_t)*at(b, DC_NOW));
    if (game_font_dc) hooks_writes_dc(w, *game_font_dc);
    if (!b->bits || b->dd) return;
    int64_t h = font_extent(game_font_default ? *game_font_default : NULL), dy = 0;
    for (int k = 0; k < 4; k++)
        if (font_extent(fonts(b)[k]) > h) h = font_extent(fonts(b)[k]);
    for (int mode = 0; mode < 4; mode++)
        if (abs_int(color_slot(b, SHADOW_DY, mode)) > dy) dy = abs_int(color_slot(b, SHADOW_DY, mode));
    int64_t margin = 2 * h + dy + 16;
    int64_t lo = y0 - margin, hi = (int64_t)y1 + margin + (int64_t)lines * (2 * h + dy + abs_int(step));
    int64_t rows = -(int64_t)b->neg_height;
    if (lo < 0) lo = 0;
    if (hi > rows - 1) hi = rows - 1;
    if (lo > hi) return;
    int64_t pitch = b->pitch;
    uint8_t *a = b->bits + pitch * lo, *c = b->bits + pitch * hi;
    uint8_t *start = a < c ? a : c;
    hooks_writes_add(w, start, (size_t)((pitch < 0 ? -pitch : pitch) * (hi - lo + 1)));
}

// The rows of a call, as a pair in parentheses (a macro argument can't carry a bare comma on).
#define UNPACK(...) __VA_ARGS__
#define Y(y) ((y), (y))
#define YH(y, h) ((y), (y) + (h))
#define RT(r) (((r) ? (r)[1] : 0), ((r) ? (r)[3] : 0))
#define LN(s) ((s) ? (int)strlen(s) + 1 : 1)

// A check of a function that draws, for the buffer b and the text s: not for text the original
// draws itself, and not for a DirectDraw surface.
#define TEXT_CHECKED(name, params, args, rows, lines, step, fmt, ...)                                        \
    SHADOW_WRITER_BODY(int, name, params, args, writes_text(&w_, b, UNPACK rows, lines, step), fmt, __VA_ARGS__)    \
    static int THISCALL shadow_##name params {                                                               \
        if (hooks_nested || b->dd || link_text(b, s)) return orig_##name args;                               \
        if (shadow_bypass()) return name args;                                                               \
        stats_##name.calls++;                                                                                \
        shadow_tick(TEXT);                                                                                   \
        return check_##name args;                                                                            \
    }
#define TEXT_WRITER(name, params, args, rows, lines, step, fmt, ...) \
    SHADOW_DECLARE_T(int, name, params)                              \
    TEXT_CHECKED(name, params, args, rows, lines, step, fmt, __VA_ARGS__)

#define P_S "%s, %d"
#define STR(s) (s) ? (s) : "(null)"

SHADOW_DECLARE_T(void, buffer_release_hdc, (buffer_t * b, int n))
SHADOW_DECLARE_T(voidp, buffer_get_hdc, (buffer_t * b))

#define HDC_WRITES (WRITES(b, sizeof *b), hooks_writes_dc(&w_, (void *)(intptr_t)*at(b, MEM_DC)))
SHADOW_WRITER_BODY(voidp, buffer_get_hdc, (buffer_t * b), (b), HDC_WRITES, "%d", 0)
static void *THISCALL shadow_buffer_get_hdc(buffer_t *b) {
    if (hooks_nested || b->dd) return orig_buffer_get_hdc(b);
    if (shadow_bypass()) return buffer_get_hdc(b);
    stats_buffer_get_hdc.calls++;
    shadow_tick(TEXT);
    return check_buffer_get_hdc(b);
}
SHADOW_WRITER_BODY(void, buffer_release_hdc, (buffer_t * b, int n), (b, n), HDC_WRITES, "%d", n)
static void THISCALL shadow_buffer_release_hdc(buffer_t *b, int n) {
    if (hooks_nested || b->dd) {
        orig_buffer_release_hdc(b, n);
        return;
    }
    if (shadow_bypass()) {
        buffer_release_hdc(b, n);
        return;
    }
    stats_buffer_release_hdc.calls++;
    shadow_tick(TEXT);
    check_buffer_release_hdc(b, n);
}

TEXT_WRITER(buffer_write_raw_l, (buffer_t * b, const char *s, int x, int y, int n), (b, s, x, y, n), Y(y), 1, 0,
            "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_CHECKED(buffer_write_multi_font_raw_l, (buffer_t * b, const char *s, int x, int y, int n), (b, s, x, y, n), Y(y), 1, 0,
             "%s, %d, %d, %d", STR(s), x, y, n)

TEXT_WRITER(buffer_write_l, (buffer_t * b, struct font * font, const char *s, int x, int y, int n), (b, font, s, x, y, n),
            Y(y), 1, 0, "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_WRITER(buffer_write_l_2, (buffer_t * b, struct font * font, const char *s, const int32_t *rect, int n), (b, font, s, rect, n),
            RT(rect), 1, 0, "%s, %d", STR(s), n)
TEXT_WRITER(buffer_write_l_3, (buffer_t * b, const char *s, int x, int y, int n), (b, s, x, y, n), Y(y), 1, 0,
            "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_WRITER(buffer_write_l_4, (buffer_t * b, const char *s, const int32_t *rect, int n), (b, s, rect, n), RT(rect), 1, 0,
            "%s, %d", STR(s), n)

TEXT_WRITER(buffer_write_cent_l, (buffer_t * b, struct font * font, const char *s, int x, int y, int w, int n),
            (b, font, s, x, y, w, n), Y(y), 1, 0, "%s, %d, %d, %d, %d", STR(s), x, y, w, n)
TEXT_WRITER(buffer_write_cent_l_2, (buffer_t * b, struct font * font, const char *s, const int32_t *rect, int n),
            (b, font, s, rect, n), RT(rect), 1, 0, "%s, %d", STR(s), n)
TEXT_WRITER(buffer_write_cent_l_3, (buffer_t * b, struct font * font, const char *s, int x, int y, int w, int h, int n),
            (b, font, s, x, y, w, h, n), YH(y, h), 1, 0, "%s, %d, %d, %d, %d, %d", STR(s), x, y, w, h, n)
TEXT_WRITER(buffer_write_cent_l_4, (buffer_t * b, struct font * font, const char *s, int x, int y, int n),
            (b, font, s, x, y, n), Y(y), 1, 0, "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_WRITER(buffer_write_cent_l_5, (buffer_t * b, const char *s, int x, int y, int w, int n), (b, s, x, y, w, n), Y(y), 1, 0,
            "%s, %d, %d, %d, %d", STR(s), x, y, w, n)
TEXT_WRITER(buffer_write_cent_l_6, (buffer_t * b, const char *s, int x, int y, int w, int h, int n), (b, s, x, y, w, h, n),
            YH(y, h), 1, 0, "%s, %d, %d, %d, %d, %d", STR(s), x, y, w, h, n)
TEXT_WRITER(buffer_write_cent_l_7, (buffer_t * b, const char *s, const int32_t *rect, int n), (b, s, rect, n), RT(rect), 1,
            0, "%s, %d", STR(s), n)
TEXT_WRITER(buffer_write_cent_l_8, (buffer_t * b, const char *s, int x, int y, int n), (b, s, x, y, n), Y(y), 1, 0,
            "%s, %d, %d, %d", STR(s), x, y, n)

TEXT_WRITER(buffer_write_right_l, (buffer_t * b, struct font * font, const char *s, int x, int y, int w, int n),
            (b, font, s, x, y, w, n), Y(y), 1, 0, "%s, %d, %d, %d, %d", STR(s), x, y, w, n)
TEXT_WRITER(buffer_write_right_l_2, (buffer_t * b, struct font * font, const char *s, int x, int y, int n),
            (b, font, s, x, y, n), Y(y), 1, 0, "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_WRITER(buffer_write_right_l_3, (buffer_t * b, struct font * font, const char *s, const int32_t *rect, int n),
            (b, font, s, rect, n), RT(rect), 1, 0, "%s, %d", STR(s), n)
TEXT_WRITER(buffer_write_right_l_4, (buffer_t * b, const char *s, int x, int y, int w, int n), (b, s, x, y, w, n), Y(y), 1, 0,
            "%s, %d, %d, %d, %d", STR(s), x, y, w, n)
TEXT_WRITER(buffer_write_right_l_5, (buffer_t * b, const char *s, int x, int y, int n), (b, s, x, y, n), Y(y), 1, 0,
            "%s, %d, %d, %d", STR(s), x, y, n)
TEXT_WRITER(buffer_write_right_l_6, (buffer_t * b, const char *s, const int32_t *rect, int n), (b, s, rect, n), RT(rect), 1,
            0, "%s, %d", STR(s), n)

TEXT_WRITER(buffer_wrap, (buffer_t * b, struct font * font, const char *s, const int32_t *rect), (b, font, s, rect), RT(rect),
            LN(s), 0, "%s", STR(s))
TEXT_WRITER(buffer_wrap_2, (buffer_t * b, const char *s, const int32_t *rect), (b, s, rect), RT(rect), LN(s), 0, "%s", STR(s))
TEXT_WRITER(buffer_wrap_3, (buffer_t * b, const char *s, int x, int y, int w), (b, s, x, y, w), Y(y), LN(s), 0,
            "%s, %d, %d, %d", STR(s), x, y, w)
TEXT_WRITER(buffer_wrap_cent, (buffer_t * b, struct font * font, const char *s, int x, int y, int w), (b, font, s, x, y, w),
            Y(y), LN(s), 0, "%s, %d, %d, %d", STR(s), x, y, w)
TEXT_WRITER(buffer_wrap_cent_2, (buffer_t * b, struct font * font, const char *s, int x, int y, int w, int step),
            (b, font, s, x, y, w, step), Y(y), LN(s), step, "%s, %d, %d, %d, %d", STR(s), x, y, w, step)
TEXT_WRITER(buffer_wrap_cent_3, (buffer_t * b, struct font * font, const char *s, const int32_t *rect), (b, font, s, rect),
            RT(rect), LN(s), 0, "%s", STR(s))
TEXT_WRITER(buffer_wrap_cent_4, (buffer_t * b, const char *s, int x, int y, int w), (b, s, x, y, w), Y(y), LN(s), 0,
            "%s, %d, %d, %d", STR(s), x, y, w)
TEXT_WRITER(buffer_wrap_cent_5, (buffer_t * b, const char *s, int x, int y, int w, int step), (b, s, x, y, w, step), Y(y),
            LN(s), step, "%s, %d, %d, %d, %d", STR(s), x, y, w, step)
TEXT_WRITER(buffer_wrap_cent_6, (buffer_t * b, const char *s, const int32_t *rect), (b, s, rect), RT(rect), LN(s), 0, "%s",
            STR(s))
TEXT_WRITER(buffer_wrap_right, (buffer_t * b, struct font * font, const char *s, int x, int y, int w), (b, font, s, x, y, w),
            Y(y), LN(s), 0, "%s, %d, %d, %d", STR(s), x, y, w)
TEXT_WRITER(buffer_wrap_right_2, (buffer_t * b, struct font * font, const char *s, const int32_t *rect), (b, font, s, rect),
            RT(rect), LN(s), 0, "%s", STR(s))
TEXT_WRITER(buffer_wrap_right_3, (buffer_t * b, const char *s, int x, int y, int w), (b, s, x, y, w), Y(y), LN(s), 0,
            "%s, %d, %d, %d", STR(s), x, y, w)
TEXT_WRITER(buffer_wrap_right_4, (buffer_t * b, const char *s, const int32_t *rect), (b, s, rect), RT(rect), LN(s), 0, "%s",
            STR(s))

// ---- Shadow checks: flying text and lists of strings ------------------------------------------------
//
// These change the queue's globals (the text, the x and y, the width left and the slack all lie
// within one stretch, which writes_text saves), the Buffer, and the list's own position; the
// drawing ones the pixels and the device context as well. As above, text the original would
// draw itself ($LINK<), a Buffer with a DirectDraw surface and a list callback are left to the
// original.

// Every entry's text of a list: does one hold a hyperlink the original keeps on the heap, and
// how many characters might be drawn.
static int list_walk(struct str_list *l, int *chars) {
    int link = 0, n = 0;
    if (chars) *chars = 0;
    if (!l || !l->head || l->count < 0 || l->count > 4096) return 0;
    struct list_node *node = l->head;
    for (int i = 0; i < l->count && node; i++, node = node->next) {
        const char *t = node->entry ? node->entry->text : NULL;
        if (!t) continue;
        n += (int)strlen(t) + 1;
        if (strstr(t, "$LINK<")) link = 1;
    }
    if (chars) *chars = n;
    return link;
}

static int list_skip(buffer_t *b, struct str_list *l) {
    if (*at(b, LIST_HOOK) || link_text(b, NULL)) return 1;
    return l && *at(b, MULTI) && list_walk(l, NULL);
}
static int list_chars(struct str_list *l) {
    int chars;
    list_walk(l, &chars);
    return chars + 1;
}
#define LIST_W(l) ((l) ? WRITES((l), sizeof(struct str_list)) : 0)

// The queue's globals: what the measuring ones write besides the Buffer.
#define FLY_HEIGHT_WRITES (BUFFER_WRITES, WRITES(game_flying_x, 16))
#define FLY_ROWS(y, lines) writes_text(&w_, b, (y), (y), (lines), 0)

#define FLY_WRITER(name, params, args, writes, skip, fmt, ...)                \
    SHADOW_DECLARE_T(int, name, params)                                       \
    SHADOW_WRITER_BODY(int, name, params, args, writes, fmt, __VA_ARGS__)     \
    static int THISCALL shadow_##name params {                                \
        if (hooks_nested || (skip)) return orig_##name args;                  \
        if (shadow_bypass()) return name args;                                \
        stats_##name.calls++;                                                 \
        shadow_tick(TEXT);                                                    \
        return check_##name args;                                             \
    }

#define FLY_DRAW_SKIP(s) (b->dd || link_text(b, s) || link_text(b, game_flying_text))
#define QUEUE_LINES(s) (LN(s) + LN(game_flying_text))

FLY_WRITER(buffer_wrap_height_flying_2, (buffer_t * b, const char *s), (b, s), FLY_HEIGHT_WRITES, 0, "%s", STR(s))
FLY_WRITER(buffer_wrap_height_flying, (buffer_t * b, const char *s, int w), (b, s, w), FLY_HEIGHT_WRITES, 0, "%s, %d", STR(s), w)
FLY_WRITER(buffer_wrap_height_flying_flush, (buffer_t * b), (b), FLY_HEIGHT_WRITES, 0, "%d", 0)
FLY_WRITER(buffer_write_strings_height, (buffer_t * b, struct str_list * l, int w, int flags), (b, l, w, flags),
           (FLY_HEIGHT_WRITES, LIST_W(l)), *at(b, LIST_HOOK), "%d, %d", w, flags)

FLY_WRITER(buffer_wrap_flying_2, (buffer_t * b, const char *s), (b, s), FLY_ROWS(*game_flying_y, QUEUE_LINES(s)), FLY_DRAW_SKIP(s),
           "%s", STR(s))
FLY_WRITER(buffer_wrap_flying, (buffer_t * b, const char *s, int x, int y, int w), (b, s, x, y, w), FLY_ROWS(y, LN(s)),
           FLY_DRAW_SKIP(s), "%s, %d, %d, %d", STR(s), x, y, w)
FLY_WRITER(buffer_wrap_flying_flush, (buffer_t * b), (b), FLY_ROWS(*game_flying_y, 1), FLY_DRAW_SKIP(NULL), "%d", 0)
FLY_WRITER(buffer_wrap_cent_flying_2, (buffer_t * b, const char *s), (b, s), FLY_ROWS(*game_flying_y, QUEUE_LINES(s)),
           FLY_DRAW_SKIP(s), "%s", STR(s))
FLY_WRITER(buffer_wrap_cent_flying, (buffer_t * b, const char *s, int x, int y, int w), (b, s, x, y, w), FLY_ROWS(y, LN(s)),
           FLY_DRAW_SKIP(s), "%s, %d, %d, %d", STR(s), x, y, w)
FLY_WRITER(buffer_wrap_cent_flying_flush, (buffer_t * b), (b), FLY_ROWS(*game_flying_y, 1), FLY_DRAW_SKIP(NULL), "%d", 0)
FLY_WRITER(buffer_write_strings, (buffer_t * b, struct str_list * l, int x, int y, int w, int flags), (b, l, x, y, w, flags),
           (FLY_ROWS(y, list_chars(l)), LIST_W(l)), b->dd || list_skip(b, l), "%d, %d, %d, %d", x, y, w, flags)

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

// ---- Sweep: drawing text ------------------------------------------------------------------------

// Text made of words and well-formed markup, and ~N link~ markers if links: no $LINK< (the
// original draws those itself), and the first word always starts it.
static void make_draw_text(char *out, int words, int links) {
    static const char *const bits[] = {"a", "word", "Mind", "x y", "  ", "WWW", "i", "{{", "}}", "[[", "]]", "q$", "q=", ">", "<", "Hello", "mmm mmm mmm"};
    char *o = out;
    *o++ = 'A';
    for (int i = 0; i < words; i++) {
        switch (rnd_to(links ? 8 : 6)) {
        case 0: o += sprintf(o, "{%s}", bits[rnd_to(6)]); break;
        case 1: o += sprintf(o, "[%s]", bits[rnd_to(6)]); break;
        case 6: o += sprintf(o, "~%d%s~", rnd_to(9), bits[rnd_to(6)]); break;
        case 7: o += sprintf(o, "~%dlink ", rnd_to(9)); break;  // a link left open
        default: o += sprintf(o, "%s", bits[rnd_to((int)(sizeof bits / sizeof *bits))]); break;
        }
        if (rnd_to(2)) *o++ = ' ';
    }
    *o = 0;
}

// A Buffer with a DIB of its own, made once by the game's own code (it only allocates the
// DIB and a device context, which stay for good).
static buffer_t *draw_buffer(void) {
    static buffer_t db;
    static int state;
    if (!state) {
        state = -1;
        game_buffer_ctor(&db);
        game_buffer_init(&db, 160, 64, 0, 0);
        if (db.bits && !db.dd && *at(&db, MEM_DC) && db.neg_height != 0) state = 1;
    }
    return state > 0 ? &db : NULL;
}

static void reset_draw(buffer_t *b, struct font *font, struct font *nogdi, int round) {
    for (int i = 0; i < 4; i++) fonts(b)[i] = rnd_to(4) ? font : NULL;
    fonts(b)[0] = round % 9 == 8 ? nogdi : (round % 9 == 7 ? NULL : font);
    int palette = rnd_to(3) == 0;
    *(uint8_t *)at(b, BUF_FLAGS) = (uint8_t)((*(uint8_t *)at(b, BUF_FLAGS) & ~4) | (palette ? 4 : 0));
    for (int i = 0; i < 256; i++) *(uint32_t *)((uint8_t *)b + PALETTE + 4 * i) = (uint32_t)rnd();
    for (int mode = 0; mode < 4; mode++) {
        *at(b, COLORS + 4 * mode) = rnd_to(5) ? (palette ? rnd_to(256) : (int)(rnd() & 0xffff) - rnd_to(2) * 0x8000) : -1;
        *at(b, SHADOW_COLOR + 4 * mode) = rnd_to(2) ? -1 : (palette ? rnd_to(256) : (int)(rnd() & 0xffff));
        *at(b, SHADOW_DX + 4 * mode) = rnd_to(5) - 2;
        *at(b, SHADOW_DY + 4 * mode) = rnd_to(5) - 2;
    }
    int multi = rnd_to(3) != 0;
    *at(b, MULTI) = multi;
    *at(b, TILDE_LINKS) = multi && rnd_to(3) == 0;
    *at(b, TILDE_LINK) = multi && rnd_to(4) == 0 ? rnd_to(9) : -1;
    *at(b, DRAW_MODE) = rnd_to(3);  // not 3, the hyperlink mode
    *at(b, STATE) = 0;
    *at(b, MODE_LINES) = rnd_to(3), *at(b, MODE_WIDTH) = rnd_to(3);
    *at(b, INDENT) = rnd_to(3) ? 0 : rnd_to(30);
    *at(b, INDENT_LEFT) = rnd_to(3) ? 0 : rnd_to(30);
    *at(b, LINES) = rnd_to(5);
    *at(b, DRAW_HOOK) = 0;
    *at(b, DC_COUNT) = rnd_to(3);
    *at(b, DC_NOW) = rnd_to(2) ? 0 : *at(b, MEM_DC);
    *game_text_end_x = (int32_t)rnd();
    // The colours of the links.
    for (int link = 0; link < 10; link++) *(uint16_t *)((uint8_t *)game_link_rects + link * 0x30 + 0x2c) = (uint16_t)rnd();
}

// The Buffer's pixels all one value that text hardly ever is, so that a line a check draws shows:
// drawn over the same text from an earlier check (or in the colour of the pixels) it changes nothing.
static void clear_bits(buffer_t *b) {
    if (!b->bits || b->dd) return;
    int64_t rows = -(int64_t)b->neg_height, pitch = b->pitch;
    if (rows <= 0) return;
    uint8_t *a = b->bits, *c = b->bits + pitch * (rows - 1);
    memset(a < c ? a : c, 0x55, (size_t)((pitch < 0 ? -pitch : pitch) * rows));
}

// Flying text and lists of strings, over the Buffer's current state: queues of several calls and a
// flush (and calls with nothing queued, a width of 0, no text), then lists of up to six entries
// of every kind, rewound or left where they stand. The widths are more than any indent, which
// the original would loop on forever.
static void flying_sweep(buffer_t *b, int links) {
    static char t[3][400];
    static struct list_node nodes[6];
    static struct list_entry entries[6];
    static char texts[6][240];
    static struct str_list list;
    int w = 40 + rnd_to(110);
    int x = rnd_to(60), y = rnd_to(30) - 5;  // where the text is on the Buffer, not off it
    for (int i = 0; i < 3; i++) make_draw_text(t[i], rnd_to(i ? 8 : 14), links);

    // A check puts the state back when it is done, so a queue is set up before the check of each
    // call that works on one, with the calls themselves (their own checks are further down).
    int w2 = 31 + rnd_to(10);
    *game_font_slack = rnd_to(2) ? 0 : 1 + rnd_to(30);
    check_buffer_wrap_height_flying_flush(b);  // a flush with nothing queued, and the calls that follow it
    check_buffer_wrap_height_flying_2(b, t[0]);
    for (int k = 0; k < 3; k++) {
        const char *first = k == 0 ? t[0] : (k == 1 ? "A" : t[1]);
        buffer_wrap_height_flying(b, first, k == 2 ? w2 : w);
        check_buffer_wrap_height_flying_2(b, t[1]);
        buffer_wrap_height_flying(b, first, k == 2 ? w2 : w);
        check_buffer_wrap_height_flying_2(b, "");
        check_buffer_wrap_height_flying_2(b, NULL);
        clear_bits(b);
        check_buffer_wrap_height_flying_flush(b);
        buffer_wrap_height_flying(b, first, w);
        buffer_wrap_height_flying_2(b, t[2]);
        clear_bits(b);
        check_buffer_wrap_height_flying_flush(b);
    }
    check_buffer_wrap_height_flying(b, t[0], w);
    check_buffer_wrap_height_flying(b, NULL, w);
    check_buffer_wrap_height_flying(b, t[2], 0);

    check_buffer_wrap_flying_flush(b);
    check_buffer_wrap_flying_2(b, t[0]);
    for (int k = 0; k < 3; k++) {
        const char *first = k == 0 ? t[0] : (k == 1 ? "A" : t[1]);
        buffer_wrap_flying(b, first, x, y, k == 2 ? w2 : w);
        check_buffer_wrap_flying_2(b, t[1]);
        buffer_wrap_flying(b, first, x, y, k == 2 ? w2 : w);
        check_buffer_wrap_flying_2(b, "");
        check_buffer_wrap_flying_2(b, NULL);
        clear_bits(b);
        check_buffer_wrap_flying_flush(b);
        buffer_wrap_flying(b, first, x, y, w);
        buffer_wrap_flying_2(b, t[2]);
        clear_bits(b);
        check_buffer_wrap_flying_flush(b);
    }
    check_buffer_wrap_flying(b, t[0], x, y, w);
    check_buffer_wrap_flying(b, NULL, x, y, w);
    check_buffer_wrap_flying(b, t[2], x, y, 0);
    check_buffer_wrap_flying(b, t[1], x, y, w2);  // about a word to a line (more than the indent)

    check_buffer_wrap_cent_flying_flush(b);
    check_buffer_wrap_cent_flying_2(b, t[0]);
    for (int k = 0; k < 3; k++) {
        const char *first = k == 0 ? t[0] : (k == 1 ? "A" : t[1]);
        buffer_wrap_cent_flying(b, first, x, y, k == 2 ? w2 : w);
        check_buffer_wrap_cent_flying_2(b, t[1]);
        buffer_wrap_cent_flying(b, first, x, y, k == 2 ? w2 : w);
        check_buffer_wrap_cent_flying_2(b, "");
        check_buffer_wrap_cent_flying_2(b, NULL);
        clear_bits(b);
        check_buffer_wrap_cent_flying_flush(b);
        buffer_wrap_cent_flying(b, first, x, y, w);
        buffer_wrap_cent_flying_2(b, t[2]);
        clear_bits(b);
        check_buffer_wrap_cent_flying_flush(b);
    }
    check_buffer_wrap_cent_flying(b, t[0], x, y, w);
    check_buffer_wrap_cent_flying(b, NULL, x, y, w);
    check_buffer_wrap_cent_flying(b, t[2], x, y, 0);
    check_buffer_wrap_cent_flying(b, t[1], x, y, 1 + rnd_to(30));

    for (int round = 0; round < 3; round++) {
        static const int kinds[] = {0, 0, 0, 1, 2, 3};
        int count = round == 0 ? rnd_to(2) : rnd_to(7);
        for (int i = 0; i < count; i++) {
            make_draw_text(texts[i], rnd_to(12), links);
            entries[i].f0 = 0, entries[i].kind = kinds[rnd_to(6)];
            entries[i].text = rnd_to(8) ? texts[i] : NULL;
            nodes[i].f0 = 0, nodes[i].key = (int32_t)rnd(), nodes[i].entry = &entries[i], nodes[i].next = &nodes[(i + 1) % count];
        }
        int at_index = count ? rnd_to(count) : 0;
        for (int flags = 0; flags < 4; flags += 2) {
            list.head = count ? &nodes[0] : NULL;
            list.cur = count ? &nodes[at_index] : NULL;
            list.count = count, list.index = at_index;
            check_buffer_write_strings_height(b, &list, w, flags);
            list.cur = count ? &nodes[at_index] : NULL;
            list.index = at_index;
            check_buffer_write_strings(b, &list, x, y, w, flags);
            list.cur = count ? &nodes[at_index] : NULL;
            list.index = at_index;
            check_buffer_write_strings_height(b, &list, 0, flags);
            check_buffer_write_strings(b, &list, x, y, 31 + rnd_to(10), flags);
        }
    }
    check_buffer_write_strings(b, NULL, x, y, w, 0);
    check_buffer_write_strings_height(b, NULL, w, 0);
}

static void draw_sweep(struct font *font) {
    buffer_t *b = draw_buffer();
    if (!b) return;
    static struct font nogdi, tall;
    static char text[900];
    uint8_t links[10 * 0x30];
    memcpy(links, game_link_rects, sizeof links);
    int32_t saved_end = *game_text_end_x;
    char saved_queue[0x6d4];
    memcpy(saved_queue, game_flying_text, sizeof saved_queue);
    tall = *font, tall.first = 2, tall.height += 3;
    nogdi = *font, nogdi.hfont = NULL;
    for (int round = 0; round < 45; round++) {
        reset_draw(b, round & 4 ? &tall : font, &nogdi, round);
        for (int k = 0; k < 6; k++) {
            make_draw_text(text, rnd_to(round & 1 ? 40 : 10), *at(b, TILDE_LINKS));
            const char *s = text;
            int len = (int)strlen(s);
            int x = rnd_to(220) - 30, y = rnd_to(90) - 20, w = rnd_to(150) - (rnd_to(8) == 0 ? 20 : 0);
            // Half the time cut short, but not between the two halves of a doubled bracket: the
            // original counts on its length covering both and runs on past the end of the text.
            int n = len;
            if (rnd_to(3) == 0) {
                n = rnd_to(len + 1);
                while (n > 0 && strchr("{}[]$", s[n - 1])) n--;
            } else if (rnd_to(8) == 0) {
                n = rnd_to(4) - 3;
            }
            int32_t rect[4] = {x, y, x + w, y + rnd_to(60) - 5};
            struct font *f = rnd_to(4) ? (rnd_to(2) ? font : &tall) : (rnd_to(2) ? &nogdi : NULL);
            int step = rnd_to(20) - 3;
            *game_font_slack = rnd_to(4) ? 0 : rnd_to(20);
            check_buffer_get_hdc(b);
            check_buffer_release_hdc(b, 1 + rnd_to(2));
            int raw_n = n < 0 ? 0 : (n > len ? len : n);  // the original reads on to a marker otherwise
            if (fonts(b)[0]) check_buffer_write_raw_l(b, s, x, y, raw_n);
            if (fonts(b)[0]) check_buffer_write_multi_font_raw_l(b, s, x, y, raw_n);
            check_buffer_write_l(b, f, s, x, y, n);
            check_buffer_write_l_2(b, f, s, rect, n);
            check_buffer_write_l_3(b, s, x, y, n);
            check_buffer_write_l_4(b, s, rect, n);
            check_buffer_write_cent_l(b, f, s, x, y, w, n);
            check_buffer_write_cent_l_2(b, f, s, rect, n);
            check_buffer_write_cent_l_3(b, f, s, x, y, w, rnd_to(40), n);
            check_buffer_write_cent_l_4(b, f, s, x, y, n);
            check_buffer_write_cent_l_5(b, s, x, y, w, n);
            check_buffer_write_cent_l_6(b, s, x, y, w, rnd_to(40), n);
            check_buffer_write_cent_l_7(b, s, rect, n);
            check_buffer_write_cent_l_8(b, s, x, y, n);
            check_buffer_write_right_l(b, f, s, x, y, w, n);
            check_buffer_write_right_l_2(b, f, s, x, y, n);
            check_buffer_write_right_l_3(b, f, s, rect, n);
            check_buffer_write_right_l_4(b, s, x, y, w, n);
            check_buffer_write_right_l_5(b, s, x, y, n);
            check_buffer_write_right_l_6(b, s, rect, n);
            check_buffer_wrap(b, f, s, rect);
            check_buffer_wrap_2(b, s, rect);
            check_buffer_wrap_3(b, s, x, y, w);
            check_buffer_wrap_cent(b, f, s, x, y, w);
            check_buffer_wrap_cent_2(b, f, s, x, y, w, step);
            check_buffer_wrap_cent_3(b, f, s, rect);
            check_buffer_wrap_cent_4(b, s, x, y, w);
            check_buffer_wrap_cent_5(b, s, x, y, w, step);
            check_buffer_wrap_cent_6(b, s, rect);
            check_buffer_wrap_right(b, f, s, x, y, w);
            check_buffer_wrap_right_2(b, f, s, rect);
            check_buffer_wrap_right_3(b, s, x, y, w);
            check_buffer_wrap_right_4(b, s, rect);
            flying_sweep(b, *at(b, TILDE_LINKS));
        }
        // Nothing to draw, and no text at all.
        if (fonts(b)[0]) check_buffer_write_raw_l(b, "abc", 5, 5, 0);
        check_buffer_write_l_3(b, NULL, 5, 5, 3);
        check_buffer_write_l_4(b, "abc", NULL, 3);
        check_buffer_wrap_2(b, NULL, (int32_t[4]){3, 4, 50, 60});  // (a rectangle is needed: the original reads it)
        check_buffer_wrap_cent_5(b, NULL, 3, 4, 50, 9);
    }
    memcpy(game_link_rects, links, sizeof links);
    *game_text_end_x = saved_end;
    memcpy(game_flying_text, saved_queue, sizeof saved_queue);
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
    draw_sweep(font);
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
    HOOK(0x5e3503, buffer_get_hdc, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x46, 0x50),
    HOOK(0x5e3563, buffer_release_hdc, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    HOOK(0x5dbd00, buffer_write_raw_l, 0x83, 0xec, 0x14, 0x53, 0x55, 0x56),
    HOOK(0x5dcae0, buffer_write_multi_font_raw_l, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08),
    HOOK(0x5db580, buffer_write_l, 0x83, 0xec, 0x0c, 0x53, 0x55, 0x56),
    HOOK(0x5db660, buffer_write_l_2, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dcea0, buffer_write_l_3, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5dcf40, buffer_write_l_4, 0x83, 0xec, 0x10, 0x53, 0x8b, 0x5c, 0x24, 0x18),
    HOOK(0x5db6c0, buffer_write_cent_l, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5db720, buffer_write_cent_l_2, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5db780, buffer_write_cent_l_3, 0x83, 0xec, 0x1c, 0x53, 0x55, 0x56),
    HOOK(0x5db810, buffer_write_cent_l_4, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dd020, buffer_write_cent_l_5, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5dd0e0, buffer_write_cent_l_6, 0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18),
    HOOK(0x5dd130, buffer_write_cent_l_7, 0x83, 0xec, 0x10, 0x53, 0x55, 0x56),
    HOOK(0x5dd250, buffer_write_cent_l_8, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5db870, buffer_write_right_l, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5db8d0, buffer_write_right_l_2, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5db930, buffer_write_right_l_3, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dd300, buffer_write_right_l_4, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5dd3b0, buffer_write_right_l_5, 0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10),
    HOOK(0x5dd450, buffer_write_right_l_6, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c),
    HOOK(0x5db990, buffer_wrap, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dd530, buffer_wrap_2, 0x51, 0x8b, 0x44, 0x24, 0x0c, 0x53),
    HOOK(0x5dd730, buffer_wrap_3, 0x53, 0x56, 0x8b, 0x74, 0x24, 0x0c),
    HOOK(0x5db9e0, buffer_wrap_cent, 0x83, 0xec, 0x0c, 0x55, 0x56, 0x57),
    HOOK(0x5dbb00, buffer_wrap_cent_2, 0x83, 0xec, 0x10, 0x55, 0x56, 0x57),
    HOOK(0x5dbc00, buffer_wrap_cent_3, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dd920, buffer_wrap_cent_4, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x33, 0xd2),
    HOOK(0x5dda00, buffer_wrap_cent_5, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x33, 0xc0),
    HOOK(0x5ddab0, buffer_wrap_cent_6, 0x51, 0x8b, 0x44, 0x24, 0x0c, 0x56),
    HOOK(0x5dbc50, buffer_wrap_right, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5dbcb0, buffer_wrap_right_2, 0x53, 0x55, 0x56, 0x8d, 0xb1, 0x2c, 0x05, 0x00, 0x00),
    HOOK(0x5ddbb0, buffer_wrap_right_3, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x5ddc90, buffer_wrap_right_4, 0x51, 0x8b, 0x44, 0x24, 0x0c, 0x56),
    HOOK(0x5dad70, buffer_write_strings_height, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x56, 0x57, 0x33, 0xff),
    HOOK(0x5db040, buffer_write_strings, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x33, 0xdb),
    HOOK(0x5ddda0, buffer_wrap_height_flying, 0x8b, 0x51, 0x10, 0x33, 0xc0, 0x89, 0x41, 0x18),
    HOOK(0x5ddde0, buffer_wrap_height_flying_2, 0xa1, 0x98, 0x3a, 0x9b, 0x00, 0x53, 0x33, 0xdb),
    HOOK(0x5ddef0, buffer_wrap_height_flying_flush, 0xa1, 0x9c, 0x3a, 0x9b, 0x00, 0x56, 0x33, 0xf6),
    HOOK(0x5ddf60, buffer_wrap_flying, 0x8b, 0x51, 0x10, 0x33, 0xc0, 0x89, 0x51, 0x14),
    HOOK(0x5ddfb0, buffer_wrap_flying_2, 0x51, 0xa1, 0x98, 0x3a, 0x9b, 0x00, 0x55),
    HOOK(0x5de1d0, buffer_wrap_flying_flush, 0xa0, 0xcc, 0x33, 0x9b, 0x00, 0x56, 0x84, 0xc0),
    HOOK(0x5de2e0, buffer_wrap_cent_flying, 0x8b, 0x54, 0x24, 0x08, 0x33, 0xc0, 0x89, 0x41, 0x18),
    HOOK(0x5de320, buffer_wrap_cent_flying_2, 0x51, 0xa1, 0x98, 0x3a, 0x9b, 0x00, 0x56),
    HOOK(0x5de4d0, buffer_wrap_cent_flying_flush, 0xa0, 0xcc, 0x33, 0x9b, 0x00, 0x56, 0x84, 0xc0),
};
struct hook_module text_module = {"text", text_hooks, HOOK_COUNT(text_hooks), text_sweep, NULL, -1};
