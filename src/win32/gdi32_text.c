// GDI32: fonts and text, rendered with FreeType from the TrueType files that
// ship with the game. Glyphs are rendered without anti-aliasing, as Windows
// did for these sizes, so text lands on exact palette colours.
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "gdi32.h"

struct font_file {
    char path[4096];
    FT_Face face;
    int bold, italic;
    struct font_file *next;
};

static FT_Library ft;
static struct font_file *fonts;

static int add_font_file(const char *host_path) {
    for (struct font_file *f = fonts; f; f = f->next)
        if (!strcmp(f->path, host_path)) return 1;
    if (!ft && FT_Init_FreeType(&ft)) return 0;
    FT_Face face;
    if (FT_New_Face(ft, host_path, 0, &face)) return 0;
    struct font_file *f = calloc(1, sizeof(*f));
    snprintf(f->path, sizeof(f->path), "%s", host_path);
    f->face = face;
    f->bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    f->italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
    f->next = fonts;
    fonts = f;
    return 1;
}

// The game folder's own fonts are always available, as if installed.
static void load_game_fonts(void) {
    static int done;
    if (done) return;
    done = 1;
    DIR *d = opendir(path_game_root());
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".ttf")) {
            char p[8192];
            snprintf(p, sizeof(p), "%s/%s", path_game_root(), e->d_name);
            add_font_file(p);
        }
    }
    if (d) closedir(d);
}

int WINAPI AddFontResourceA(const char *file) {
    char host[4096];
    if (path_to_host(file, host, sizeof(host))) return 0;
    return add_font_file(host);
}

BOOL WINAPI RemoveFontResourceA(const char *file) { return TRUE; }

BOOL WINAPI CreateScalableFontResourceA(DWORD hidden, const char *fot, const char *ttf, const char *path) {
    return TRUE;  // .fot files only point Windows at the .ttf, which we read directly
}

// ---- Realised fonts -------------------------------------------------------

struct glyph {
    int loaded;
    int left, top, width, rows, pitch, advance;
    uint8_t *bits;  // 1 bpp, MSB first
};

struct rfont {
    struct font_file *file;
    int em_x, em_y;
    int ascent, descent, internal_leading, avg_width, max_width;
    int fake_bold;
    struct glyph glyphs[256];
};

static struct font_file *match_font(const LOGFONTA *lf) {
    load_game_fonts();
    int want_bold = lf->weight >= 600, want_italic = lf->italic != 0;
    struct font_file *best = NULL;
    int best_score = -1;
    for (struct font_file *f = fonts; f; f = f->next) {
        const char *family = f->face->family_name ? f->face->family_name : "";
        int score = 0;
        if (!strcasecmp(family, lf->face)) score = 100;
        else if (*lf->face && strcasestr(family, lf->face)) score = 50;
        else continue;
        score += (f->bold == want_bold) * 10 + (f->italic == want_italic) * 5;
        if (score > best_score) best_score = score, best = f;
    }
    if (!best) {
        static char warned[32];
        if (strcasecmp(warned, lf->face)) {
            fprintf(stderr, "chiron: no font named \"%s\"; using a substitute\n", lf->face);
            snprintf(warned, sizeof(warned), "%s", lf->face);
        }
        // Prefer Arial-like faces, then anything.
        for (struct font_file *f = fonts; f; f = f->next)
            if (f->face->family_name && strcasestr(f->face->family_name, "Arial") && f->bold == want_bold) return f;
        best = fonts;
    }
    return best;
}

static struct rfont *realise(struct font *font) {
    if (font->face) return font->face;
    struct font_file *file = match_font(&font->lf);
    if (!file) return NULL;
    struct rfont *rf = calloc(1, sizeof(*rf));
    rf->file = file;
    FT_Face face = file->face;
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    int upem = face->units_per_EM;
    int win_ascent = os2 ? os2->usWinAscent : face->ascender;
    int win_descent = os2 ? os2->usWinDescent : -face->descender;
    LONG h = font->lf.height;
    // Positive heights are cell heights, negative ones character heights.
    if (h == 0) h = 16;
    rf->em_y = h < 0 ? -h : (int)((int64_t)h * upem / (win_ascent + win_descent) + 0.5);
    if (rf->em_y < 1) rf->em_y = 1;
    rf->em_x = rf->em_y;
    if (font->lf.width > 0 && os2 && os2->xAvgCharWidth)
        rf->em_x = (int)((int64_t)font->lf.width * upem / os2->xAvgCharWidth);
    rf->ascent = (int)(((int64_t)win_ascent * rf->em_y + upem - 1) / upem);
    rf->descent = (int)(((int64_t)win_descent * rf->em_y + upem - 1) / upem);
    rf->internal_leading = rf->ascent + rf->descent - rf->em_y;
    rf->avg_width = os2 && os2->xAvgCharWidth ? (int)((int64_t)os2->xAvgCharWidth * rf->em_x / upem) : rf->em_x / 2;
    rf->max_width = (int)((int64_t)(face->bbox.xMax - face->bbox.xMin) * rf->em_x / upem);
    rf->fake_bold = font->lf.weight >= 600 && !file->bold;
    font->face = rf;
    return rf;
}

void gdi_text_release_font(struct font *f) {
    struct rfont *rf = f->face;
    if (!rf) return;
    for (int i = 0; i < 256; i++) free(rf->glyphs[i].bits);
    free(rf);
    f->face = NULL;
}

// Windows-1252 code points 0x80-0x9f.
static const uint16_t cp1252_high[32] = {
    0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
    0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178,
};

static struct glyph *get_glyph(struct rfont *rf, unsigned char c) {
    struct glyph *g = &rf->glyphs[c];
    if (g->loaded) return g;
    g->loaded = 1;
    FT_Face face = rf->file->face;
    FT_Set_Pixel_Sizes(face, rf->em_x, rf->em_y);
    unsigned cp = c >= 0x80 && c < 0xa0 && cp1252_high[c - 0x80] ? cp1252_high[c - 0x80] : c;
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME)) return g;
    FT_GlyphSlot s = face->glyph;
    g->left = s->bitmap_left;
    g->top = s->bitmap_top;
    g->width = s->bitmap.width;
    g->rows = s->bitmap.rows;
    g->pitch = s->bitmap.pitch < 0 ? -s->bitmap.pitch : s->bitmap.pitch;
    g->advance = (int)((s->advance.x + 32) >> 6) + rf->fake_bold;
    g->bits = malloc((size_t)g->pitch * g->rows + 1);
    memcpy(g->bits, s->bitmap.buffer, (size_t)g->pitch * g->rows);
    return g;
}

static int text_width(struct rfont *rf, const unsigned char *s, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) w += get_glyph(rf, s[i])->advance;
    return w;
}

// ---- Text functions -------------------------------------------------------

typedef struct {
    LONG cx, cy;
} SIZE;

BOOL WINAPI GetTextExtentPoint32A(HDC hdc, const char *s, int n, SIZE *size) {
    struct dc *dc = gdi_dc(hdc);
    struct rfont *rf = dc ? realise(dc->font) : NULL;
    if (!rf) return FALSE;
    size->cx = text_width(rf, (const unsigned char *)s, n);
    size->cy = rf->ascent + rf->descent;
    return TRUE;
}

typedef struct {
    LONG height, ascent, descent, internal_leading, external_leading, avg_char_width, max_char_width;
    LONG weight, overhang, digitized_aspect_x, digitized_aspect_y;
    char first_char, last_char, default_char, break_char;
    BYTE italic, underlined, struck_out, pitch_and_family, charset;
} TEXTMETRICA;

BOOL WINAPI GetTextMetricsA(HDC hdc, TEXTMETRICA *tm) {
    struct dc *dc = gdi_dc(hdc);
    struct rfont *rf = dc ? realise(dc->font) : NULL;
    if (!rf) return FALSE;
    memset(tm, 0, sizeof(*tm));
    tm->height = rf->ascent + rf->descent;
    tm->ascent = rf->ascent;
    tm->descent = rf->descent;
    tm->internal_leading = rf->internal_leading;
    tm->avg_char_width = rf->avg_width;
    tm->max_char_width = rf->max_width;
    tm->weight = dc->font->lf.weight ? dc->font->lf.weight : 400;
    tm->overhang = rf->fake_bold;
    tm->digitized_aspect_x = tm->digitized_aspect_y = 96;
    tm->first_char = 0x20;
    tm->last_char = (char)0xff;
    tm->default_char = 0x1f;
    tm->break_char = 0x20;
    tm->italic = dc->font->lf.italic;
    tm->underlined = dc->font->lf.underline;
    tm->struck_out = dc->font->lf.strikeout;
    tm->pitch_and_family = 0x06 | 0x20;  // variable pitch, TrueType, FF_SWISS
    return TRUE;
}

BOOL WINAPI TextOutA(HDC hdc, int x, int y, const char *str, int n) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return FALSE;
    struct rfont *rf = realise(dc->font);
    struct target t;
    if (!rf || gdi_target(dc, &t)) return TRUE;
    const unsigned char *s = (const unsigned char *)str;
    int width = text_width(rf, s, n);

    UINT align = dc->text_align;
    if (align & 1) x = dc->pos.x, y = dc->pos.y;  // TA_UPDATECP
    switch (align & 6) {
    case 2: x -= width; break;      // TA_RIGHT
    case 6: x -= width / 2; break;  // TA_CENTER
    }
    int baseline = (align & 24) == 24 ? y : (align & 8) ? y - rf->descent : y + rf->ascent;
    if (align & 1) dc->pos.x = (align & 6) == 2 ? x : x + width;

    if (dc->bk_mode == 2) {  // OPAQUE
        uint32_t bk = gdi_colorref_to_raw(dc, t.s, dc->bk_color);
        for (int yy = baseline - rf->ascent; yy < baseline + rf->descent; yy++) gdi_fill_span(&t, x, x + width, yy, bk);
    }
    uint32_t fg = gdi_colorref_to_raw(dc, t.s, dc->text_color);
    int pen_x = x;
    for (int i = 0; i < n; i++) {
        struct glyph *g = get_glyph(rf, s[i]);
        for (int row = 0; row < g->rows; row++)
            for (int col = 0; col < g->width; col++)
                if (g->bits[row * g->pitch + (col >> 3)] & (0x80 >> (col & 7))) {
                    int px = pen_x + g->left + col, py = baseline - g->top + row;
                    gdi_fill_span(&t, px, px + 1 + rf->fake_bold, py, fg);
                }
        pen_x += g->advance;
    }
    if (dc->font->lf.underline) gdi_fill_span(&t, x, x + width, baseline + 1, fg);
    if (dc->font->lf.strikeout) gdi_fill_span(&t, x, x + width, baseline - rf->ascent / 3, fg);
    gdi_target_done(dc);
    return TRUE;
}
