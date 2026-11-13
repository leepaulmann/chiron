// GDI32: device contexts, objects, bitmaps, palettes and bit-block transfers.
// Everything is rendered in software into DIB sections and the main window's
// 32-bit framebuffer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdi32.h"

// ---- Objects --------------------------------------------------------------

static struct brush stock_brushes[6] = {
    {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 0, 0xffffff}, {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 0, 0xc0c0c0},
    {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 0, 0x808080}, {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 0, 0x404040},
    {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 0, 0x000000}, {{GDIOBJ_MAGIC, GDI_BRUSH, 1}, 1, 0},
};
static struct pen stock_pens[3] = {
    {{GDIOBJ_MAGIC, GDI_PEN, 1}, 0, 1, 0xffffff},
    {{GDIOBJ_MAGIC, GDI_PEN, 1}, 0, 1, 0x000000},
    {{GDIOBJ_MAGIC, GDI_PEN, 1}, 5, 1, 0},
};
static struct font stock_font = {{GDIOBJ_MAGIC, GDI_FONT, 1}, {.height = 16, .weight = 700, .face = "Arial"}};
static struct palette stock_palette = {{GDIOBJ_MAGIC, GDI_PALETTE, 1}, 20, {{0}}};
static uint8_t stock_bitmap_bits[4];
static struct bitmap stock_bitmap = {{GDIOBJ_MAGIC, GDI_BITMAP, 1},
                                     {.width = 1, .height = 1, .bpp = 1, .stride = 4,
                                      .bits = stock_bitmap_bits, .colors = {0, 0xffffff}, .ncolors = 2}};

// The 20 static colours of the Windows system palette.
static const uint32_t system_colors[20] = {
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0, 0xc0dcc0, 0xa6caf0,
    0xfffbf0, 0xa0a0a4, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff,
};

static void init_stock_palette(void) {
    if (stock_palette.entries[19].r) return;
    for (int i = 0; i < 20; i++)
        stock_palette.entries[i] = (PALETTEENTRY){system_colors[i] >> 16, system_colors[i] >> 8, system_colors[i], 0};
}

void *gdi_obj(HGDIOBJ h, enum gdi_type type) {
    struct gdiobj *o = h;
    if (!o || (uintptr_t)o < 0x10000 || o->magic != GDIOBJ_MAGIC || (type && o->type != type)) return NULL;
    return o;
}

HGDIOBJ WINAPI GetStockObject(int i) {
    init_stock_palette();
    if (i >= 0 && i <= 5) return &stock_brushes[i];
    if (i >= 6 && i <= 8) return &stock_pens[i - 6];
    if (i >= 10 && i <= 17 && i != 15) return &stock_font;
    if (i == 15) return &stock_palette;
    return NULL;
}

BOOL WINAPI DeleteObject(HGDIOBJ h) {
    struct gdiobj *o = gdi_obj(h, 0);
    if (!o) return FALSE;
    if (o->stock) return TRUE;
    if (o->type == GDI_BITMAP) {
        struct bitmap *b = (struct bitmap *)o;
        free(b->s.bits);
        free(b->s.inverse);
    }
    if (o->type == GDI_FONT) gdi_text_release_font((struct font *)o);
    o->magic = 0;
    free(o);
    return TRUE;
}

HGDIOBJ WINAPI CreateSolidBrush(COLORREF c) {
    struct brush *b = calloc(1, sizeof(*b));
    b->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_BRUSH, 0};
    b->color = c;
    return b;
}

typedef struct {
    UINT style;
    POINT width;
    COLORREF color;
} LOGPEN;

HGDIOBJ WINAPI CreatePenIndirect(const LOGPEN *lp) {
    struct pen *p = calloc(1, sizeof(*p));
    p->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_PEN, 0};
    p->style = lp->style;
    p->width = lp->width.x > 0 ? lp->width.x : 1;
    p->color = lp->color;
    return p;
}

HGDIOBJ WINAPI CreateFontIndirectA(const LOGFONTA *lf) {
    struct font *f = calloc(1, sizeof(*f));
    f->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_FONT, 0};
    f->lf = *lf;
    return f;
}

HGDIOBJ WINAPI CreateRectRgnIndirect(const RECT *r) {
    struct region *g = calloc(1, sizeof(*g));
    g->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_REGION, 0};
    g->rect = *r;
    return g;
}

typedef struct {
    WORD version, count;
    PALETTEENTRY entries[];
} LOGPALETTE;

HGDIOBJ WINAPI CreatePalette(const LOGPALETTE *lp) {
    struct palette *p = calloc(1, sizeof(*p));
    p->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_PALETTE, 0};
    p->count = lp->count > 256 ? 256 : lp->count;
    memcpy(p->entries, lp->entries, p->count * sizeof(PALETTEENTRY));
    return p;
}

UINT WINAPI GetPaletteEntries(HGDIOBJ h, UINT start, UINT n, PALETTEENTRY *out) {
    struct palette *p = gdi_obj(h, GDI_PALETTE);
    if (!p) return 0;
    if (!out) return p->count;
    UINT i = 0;
    for (; i < n && start + i < (UINT)p->count; i++) out[i] = p->entries[start + i];
    return i;
}

BOOL WINAPI AnimatePalette(HGDIOBJ h, UINT start, UINT n, const PALETTEENTRY *in) {
    // On a true-colour display palette animation changes nothing on screen;
    // we still keep the entries so later lookups see them.
    struct palette *p = gdi_obj(h, GDI_PALETTE);
    if (!p) return FALSE;
    for (UINT i = 0; i < n && start + i < (UINT)p->count; i++)
        if (p->entries[start + i].flags & 0x01) p->entries[start + i] = in[i];  // PC_RESERVED only
    return TRUE;
}

UINT WINAPI GetSystemPaletteEntries(HDC hdc, UINT start, UINT n, PALETTEENTRY *out) {
    init_stock_palette();
    if (!out) return 256;
    for (UINT i = 0; i < n && start + i < 256; i++) {
        UINT k = start + i;
        // Static colours occupy the first and last ten entries.
        out[i] = k < 10 ? stock_palette.entries[k] : k >= 246 ? stock_palette.entries[k - 236] : (PALETTEENTRY){0};
    }
    return n;
}

UINT WINAPI SetSystemPaletteUse(HDC hdc, UINT use) { return 1; /* SYSPAL_STATIC */ }

// ---- Device contexts ------------------------------------------------------

static struct dc *new_dc(int is_window, HWND hwnd) {
    init_stock_palette();
    struct dc *dc = calloc(1, sizeof(*dc));
    dc->magic = DC_MAGIC;
    dc->is_window = is_window;
    dc->hwnd = hwnd;
    dc->bitmap = is_window ? NULL : &stock_bitmap;
    dc->pen = &stock_pens[1];
    dc->brush = &stock_brushes[0];
    dc->font = &stock_font;
    dc->palette = &stock_palette;
    dc->text_color = 0x000000;
    dc->bk_color = 0xffffff;
    dc->bk_mode = 2;
    dc->stretch_mode = 1;
    dc->arc_direction = 1;
    return dc;
}

struct dc *gdi_dc(HDC hdc) {
    struct dc *dc = hdc;
    return dc && (uintptr_t)dc > 0x10000 && dc->magic == DC_MAGIC ? dc : NULL;
}

void *gdi_dc_state(void *hdc, int *size) {
    struct dc *dc = gdi_dc(hdc);
    if (dc) *size = sizeof *dc;
    return dc;
}

// Whether two DCs are in the same state, taking two bitmaps of the same size and format,
// with the same pixels and colours, as the same: for checks that build two objects side by
// side, one with the game's code and one with Chiron's.
int gdi_dc_same(void *hdc_a, void *hdc_b) {
    struct dc *a = gdi_dc(hdc_a), *b = gdi_dc(hdc_b);
    if (!a || !b) return !a == !b;
    struct dc x = *a, y = *b;
    if (!x.bitmap != !y.bitmap) return 0;
    if (x.bitmap && x.bitmap != y.bitmap) {
        const struct surface *s = &x.bitmap->s, *t = &y.bitmap->s;
        if (s->width != t->width || s->height != t->height || s->bpp != t->bpp || s->stride != t->stride ||
            s->bottom_up != t->bottom_up || s->ncolors != t->ncolors ||
            memcmp(s->colors, t->colors, sizeof s->colors) || memcmp(s->masks, t->masks, sizeof s->masks) ||
            x.bitmap->obj.stock != y.bitmap->obj.stock)
            return 0;
        if (s->bits && t->bits && memcmp(s->bits, t->bits, (size_t)s->stride * s->height)) return 0;
        if (!s->bits != !t->bits) return 0;
    }
    x.bitmap = y.bitmap = NULL;
    return !memcmp(&x, &y, sizeof x);
}

// Whether two font objects were made from the same LOGFONT, for checks that build two Fonts
// side by side. The bytes of the face name after its end are leftovers and don't count.
int gdi_font_same(void *ha, void *hb) {
    struct font *a = gdi_obj(ha, GDI_FONT), *b = gdi_obj(hb, GDI_FONT);
    if (!a || !b) return !a == !b;
    LOGFONTA x = a->lf, y = b->lf;
    if (strncmp(x.face, y.face, sizeof x.face)) return 0;
    memset(x.face, 0, sizeof x.face), memset(y.face, 0, sizeof y.face);
    return !memcmp(&x, &y, sizeof x);
}

HDC gdi_create_window_dc(HWND hwnd) { return new_dc(1, hwnd); }

void gdi_release_window_dc(HDC hdc) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc || !dc->is_window) return;
    dc->magic = 0;
    free(dc);
}

HDC WINAPI CreateCompatibleDC(HDC hdc) { return new_dc(0, NULL); }

HDC WINAPI CreateDCA(const char *driver, const char *device, const char *output, const void *mode) {
    return new_dc(1, NULL);  // "DISPLAY": the whole screen
}

BOOL WINAPI DeleteDC(HDC hdc) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return FALSE;
    dc->magic = 0;
    free(dc);
    return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h) {
    struct dc *dc = gdi_dc(hdc);
    struct gdiobj *o = gdi_obj(h, 0);
    if (!dc || !o) return NULL;
    void *old = NULL;
    switch (o->type) {
    case GDI_BITMAP:
        if (dc->is_window) return NULL;
        old = dc->bitmap;
        dc->bitmap = (struct bitmap *)o;
        break;
    case GDI_PEN: old = dc->pen; dc->pen = (struct pen *)o; break;
    case GDI_BRUSH: old = dc->brush; dc->brush = (struct brush *)o; break;
    case GDI_FONT: old = dc->font; dc->font = (struct font *)o; break;
    case GDI_REGION:
        dc->has_clip = 1;
        dc->clip = ((struct region *)o)->rect;
        return (HGDIOBJ)(uintptr_t)2;  // SIMPLEREGION
    case GDI_PALETTE: return NULL;  // only through SelectPalette
    }
    return old;
}

HGDIOBJ WINAPI SelectPalette(HDC hdc, HGDIOBJ h, BOOL background) {
    struct dc *dc = gdi_dc(hdc);
    struct palette *p = gdi_obj(h, GDI_PALETTE);
    if (!dc || !p) return NULL;
    struct palette *old = dc->palette;
    dc->palette = p;
    return old;
}

UINT WINAPI RealizePalette(HDC hdc) {
    struct dc *dc = gdi_dc(hdc);
    return dc ? dc->palette->count : 0;
}

int WINAPI SelectClipRgn(HDC hdc, HGDIOBJ rgn) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0;
    struct region *r = gdi_obj(rgn, GDI_REGION);
    dc->has_clip = r != NULL;
    if (r) dc->clip = r->rect;
    return r ? 2 : 1;  // SIMPLEREGION : NULLREGION
}

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF c) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0xffffffffu;
    COLORREF old = dc->text_color;
    dc->text_color = c;
    return old;
}

int WINAPI SetBkMode(HDC hdc, int mode) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0;
    int old = dc->bk_mode;
    dc->bk_mode = mode;
    return old;
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0xffffffffu;
    UINT old = dc->text_align;
    dc->text_align = align;
    return old;
}

int WINAPI SetStretchBltMode(HDC hdc, int mode) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0;
    int old = dc->stretch_mode;
    dc->stretch_mode = mode;
    return old;
}

int WINAPI SetArcDirection(HDC hdc, int dir) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return 0;
    int old = dc->arc_direction;
    dc->arc_direction = dir;
    return old;
}

BOOL WINAPI GdiFlush(void) { return TRUE; }

int WINAPI GetDeviceCaps(HDC hdc, int index) {
    switch (index) {
    case 2: return 1;                                  // TECHNOLOGY: DT_RASDISPLAY
    case 8: return display_width;                      // HORZRES
    case 10: return display_height;                    // VERTRES
    case 12: return display_bpp;                       // BITSPIXEL
    case 14: return 1;                                 // PLANES
    case 24: return display_bpp <= 8 ? 20 : -1;        // NUMCOLORS
    case 38: return 0x0001 | 0x0080 | 0x0800 | 0x0200 | (display_bpp <= 8 ? 0x0100 : 0);  // RASTERCAPS
    case 88: case 90: return 96;                       // LOGPIXELSX/Y
    case 104: return display_bpp <= 8 ? 256 : 0;       // SIZEPALETTE
    case 106: return display_bpp <= 8 ? 20 : 0;        // NUMRESERVED
    case 108: return display_bpp <= 8 ? 18 : 24;       // COLORRES
    case 4: return 320;                                // HORZSIZE (mm)
    case 6: return 240;                                // VERTSIZE (mm)
    }
    fprintf(stderr, "chiron: GetDeviceCaps(%d) unknown, returning 0\n", index);
    return 0;
}

// ---- Pixels ---------------------------------------------------------------

static int mask_shift(uint32_t m) { return m ? __builtin_ctz(m) : 0; }
static int mask_bits(uint32_t m) { return m ? __builtin_popcount(m) : 0; }

static uint32_t expand(uint32_t v, int bits) {
    // Scale a `bits`-wide channel value up to 8 bits.
    if (bits >= 8) return v >> (bits - 8);
    v <<= 8 - bits;
    return v | (v >> bits);
}

uint32_t gdi_get_raw(const struct surface *s, int x, int y) {
    const uint8_t *row = surface_row(s, y);
    switch (s->bpp) {
    case 32: return ((const uint32_t *)row)[x];
    case 24: return row[x * 3] | row[x * 3 + 1] << 8 | row[x * 3 + 2] << 16;
    case 16: return ((const uint16_t *)row)[x];
    case 8: return row[x];
    case 4: return (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xf;
    case 1: return (row[x >> 3] >> (7 - (x & 7))) & 1;
    }
    return 0;
}

void gdi_set_raw(struct surface *s, int x, int y, uint32_t v) {
    uint8_t *row = surface_row(s, y);
    switch (s->bpp) {
    case 32: ((uint32_t *)row)[x] = v; break;
    case 24: row[x * 3] = v, row[x * 3 + 1] = v >> 8, row[x * 3 + 2] = v >> 16; break;
    case 16: ((uint16_t *)row)[x] = v; break;
    case 8: row[x] = v; break;
    case 4: {
        int sh = (x & 1) ? 0 : 4;
        row[x >> 1] = (row[x >> 1] & ~(0xf << sh)) | ((v & 0xf) << sh);
        break;
    }
    case 1: {
        int sh = 7 - (x & 7);
        row[x >> 3] = (row[x >> 3] & ~(1 << sh)) | ((v & 1) << sh);
        break;
    }
    }
}

uint32_t gdi_raw_to_rgb(const struct surface *s, uint32_t raw) {
    if (s->bpp <= 8) return raw < 256 ? s->colors[raw] : 0;
    if (s->bpp == 24) return raw & 0xffffff;
    uint32_t r = (raw & s->masks[0]) >> mask_shift(s->masks[0]);
    uint32_t g = (raw & s->masks[1]) >> mask_shift(s->masks[1]);
    uint32_t b = (raw & s->masks[2]) >> mask_shift(s->masks[2]);
    return expand(r, mask_bits(s->masks[0])) << 16 | expand(g, mask_bits(s->masks[1])) << 8 |
           expand(b, mask_bits(s->masks[2]));
}

void surface_colors_changed(struct surface *s) {
    free(s->inverse);
    s->inverse = NULL;
}

static uint8_t nearest_index(const struct surface *s, uint32_t rgb) {
    int best = 0;
    long best_d = 1L << 30;
    int r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
    for (int i = 0; i < s->ncolors; i++) {
        uint32_t c = s->colors[i];
        long dr = r - (int)(c >> 16 & 0xff), dg = g - (int)(c >> 8 & 0xff), db = b - (int)(c & 0xff);
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (!d) break;
        }
    }
    return best;
}

uint32_t gdi_rgb_to_raw(struct surface *s, uint32_t rgb) {
    if (s->bpp <= 8) {
        // Exact colours are common (text and fills in palette colours), so
        // check those before the approximate inverse table.
        for (int i = 0; i < s->ncolors; i++)
            if (s->colors[i] == rgb) return i;
        if (!s->inverse) {
            s->inverse = malloc(32768);
            for (int i = 0; i < 32768; i++)
                s->inverse[i] = nearest_index(s, expand(i >> 10, 5) << 16 | expand(i >> 5 & 31, 5) << 8 |
                                                     expand(i & 31, 5));
        }
        return s->inverse[(rgb >> 9 & 0x7c00) | (rgb >> 6 & 0x3e0) | (rgb >> 3 & 0x1f)];
    }
    if (s->bpp == 24) return rgb & 0xffffff;
    uint32_t v = 0;
    for (int c = 0; c < 3; c++) {
        uint32_t ch = rgb >> (16 - 8 * c) & 0xff;
        int bits = mask_bits(s->masks[c]);
        v |= ((bits >= 8 ? ch << (bits - 8) : ch >> (8 - bits)) << mask_shift(s->masks[c])) & s->masks[c];
    }
    return v;
}

uint32_t gdi_colorref_to_raw(struct dc *dc, struct surface *s, COLORREF c) {
    if ((c >> 16) == 0x10ff) return s->bpp <= 8 ? (c & 0xff) : gdi_raw_to_rgb(s, c & 0xff);  // DIBINDEX
    if ((c >> 24) == 0x01) {  // PALETTEINDEX
        PALETTEENTRY e = dc->palette->entries[(c & 0xffff) % 256];
        c = e.r | e.g << 8 | e.b << 16;
    }
    uint32_t rgb = (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    return gdi_rgb_to_raw(s, rgb);
}

// ---- Targets --------------------------------------------------------------

int gdi_target(struct dc *dc, struct target *t) {
    RECT client;
    if (dc->is_window) {
        t->s = user32_window_surface(dc->hwnd, &client);
        if (!t->s) return -1;
    } else {
        if (!dc->bitmap || !dc->bitmap->s.bits) return -1;
        t->s = &dc->bitmap->s;
        client = (RECT){0, 0, t->s->width, t->s->height};
    }
    t->ox = client.left;
    t->oy = client.top;
    t->clip = client;
    if (t->clip.left < 0) t->clip.left = 0;
    if (t->clip.top < 0) t->clip.top = 0;
    if (t->clip.right > t->s->width) t->clip.right = t->s->width;
    if (t->clip.bottom > t->s->height) t->clip.bottom = t->s->height;
    if (dc->has_clip) {
        RECT c = {dc->clip.left + t->ox, dc->clip.top + t->oy, dc->clip.right + t->ox, dc->clip.bottom + t->oy};
        if (c.left > t->clip.left) t->clip.left = c.left;
        if (c.top > t->clip.top) t->clip.top = c.top;
        if (c.right < t->clip.right) t->clip.right = c.right;
        if (c.bottom < t->clip.bottom) t->clip.bottom = c.bottom;
    }
    return 0;
}

void gdi_target_done(struct dc *dc) {
    if (!dc->is_window) return;
    user32_screen_dirty();
    user32_present_if_due();
}

void gdi_fill_span(struct target *t, int x0, int x1, int y, uint32_t raw) {
    y += t->oy;
    x0 += t->ox;
    x1 += t->ox;
    if (y < t->clip.top || y >= t->clip.bottom) return;
    if (x0 < t->clip.left) x0 = t->clip.left;
    if (x1 > t->clip.right) x1 = t->clip.right;
    if (x0 >= x1) return;
    struct surface *s = t->s;
    if (s->bpp == 32) {
        uint32_t *row = (uint32_t *)surface_row(s, y);
        for (int x = x0; x < x1; x++) row[x] = raw;
    } else if (s->bpp == 8) {
        memset(surface_row(s, y) + x0, raw, x1 - x0);
    } else {
        for (int x = x0; x < x1; x++) gdi_set_raw(s, x, y, raw);
    }
}

int gdi_fill_rect(HDC hdc, const RECT *r, HGDIOBJ hbrush) {
    struct dc *dc = gdi_dc(hdc);
    struct target t;
    if (!dc || gdi_target(dc, &t)) return 0;
    COLORREF color;
    struct brush *b = gdi_obj(hbrush, GDI_BRUSH);
    if (b) {
        if (b->null) return 1;
        color = b->color;
    } else {
        // (HBRUSH)(COLOR_xxx + 1): a system colour.
        static const COLORREF sys[] = {0xc0c0c0, 0x808000, 0x800000, 0x808080, 0xc0c0c0, 0xffffff};
        uintptr_t idx = (uintptr_t)hbrush - 1;
        color = idx < sizeof(sys) / sizeof(sys[0]) ? sys[idx] : 0xc0c0c0;
    }
    uint32_t raw = gdi_colorref_to_raw(dc, t.s, color);
    for (int y = r->top; y < r->bottom; y++) gdi_fill_span(&t, r->left, r->right, y, raw);
    gdi_target_done(dc);
    return 1;
}

// ---- DIB sections ---------------------------------------------------------

typedef struct {
    DWORD size;
    LONG width, height;
    WORD planes, bit_count;
    DWORD compression, size_image;
    LONG xppm, yppm;
    DWORD clr_used, clr_important;
} BITMAPINFOHEADER;

// Fill a surface's format and colour table from a BITMAPINFO.
static int surface_from_info(struct surface *s, struct dc *dc, const BITMAPINFOHEADER *h, UINT usage) {
    memset(s, 0, sizeof(*s));
    s->width = h->width;
    s->height = h->height < 0 ? -h->height : h->height;
    s->bottom_up = h->height > 0;
    s->bpp = h->bit_count;
    if (h->compression != 0 && h->compression != 3) {
        fprintf(stderr, "chiron: unsupported DIB compression %u\n", h->compression);
        return -1;
    }
    s->stride = ((s->width * s->bpp + 31) / 32) * 4;
    const uint8_t *after = (const uint8_t *)h + h->size;
    if (s->bpp == 16) {
        s->masks[0] = 0x7c00, s->masks[1] = 0x3e0, s->masks[2] = 0x1f;
    } else if (s->bpp == 32) {
        s->masks[0] = 0xff0000, s->masks[1] = 0xff00, s->masks[2] = 0xff;
    }
    if (h->compression == 3) {  // BI_BITFIELDS
        memcpy(s->masks, h->size >= 52 ? (const uint8_t *)h + 40 : after, 12);
        if (h->size < 52) after += 12;
    }
    if (s->bpp <= 8) {
        s->ncolors = h->clr_used ? (int)h->clr_used : 1 << s->bpp;
        if (s->ncolors > 256) s->ncolors = 256;
        for (int i = 0; i < s->ncolors; i++) {
            if (usage == 1 && dc) {  // DIB_PAL_COLORS: indices into the DC palette
                PALETTEENTRY e = dc->palette->entries[((const WORD *)after)[i] % 256];
                s->colors[i] = e.r << 16 | e.g << 8 | e.b;
            } else {
                const uint8_t *q = after + i * 4;  // RGBQUAD
                s->colors[i] = q[2] << 16 | q[1] << 8 | q[0];
            }
        }
    }
    return 0;
}

HGDIOBJ WINAPI CreateDIBSection(HDC hdc, const BITMAPINFOHEADER *info, UINT usage, void **bits,
                                HANDLE section, DWORD offset) {
    struct bitmap *b = calloc(1, sizeof(*b));
    b->obj = (struct gdiobj){GDIOBJ_MAGIC, GDI_BITMAP, 0};
    if (section) fprintf(stderr, "chiron: CreateDIBSection with a file mapping is not supported\n");
    if (surface_from_info(&b->s, gdi_dc(hdc), info, usage)) {
        free(b);
        return NULL;
    }
    b->s.bits = calloc(1, (size_t)b->s.stride * b->s.height + 4);
    if (bits) *bits = b->s.bits;
    return b;
}

UINT WINAPI SetDIBColorTable(HDC hdc, UINT start, UINT n, const BYTE *rgbquad) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc || !dc->bitmap || dc->bitmap->s.bpp > 8) return 0;
    struct surface *s = &dc->bitmap->s;
    UINT i = 0;
    for (; i < n && start + i < 256; i++) {
        const BYTE *q = rgbquad + i * 4;
        s->colors[start + i] = q[2] << 16 | q[1] << 8 | q[0];
    }
    if (start + i > (UINT)s->ncolors) s->ncolors = start + i;
    surface_colors_changed(s);
    return i;
}

// ---- Bit-block transfers --------------------------------------------------

enum {
    SRCCOPY = 0x00cc0020,
    SRCPAINT = 0x00ee0086,
    SRCAND = 0x008800c6,
    SRCINVERT = 0x00660046,
    NOTSRCCOPY = 0x00330008,
    DSTINVERT = 0x00550009,
    BLACKNESS = 0x00000042,
    WHITENESS = 0x00ff0062,
    PATCOPY = 0x00f00021,
};

static int same_format(const struct surface *a, const struct surface *b) {
    if (a->bpp != b->bpp) return 0;
    if (a->bpp <= 8) return a->ncolors <= b->ncolors && !memcmp(a->colors, b->colors, a->ncolors * 4);
    return a->bpp == 24 || !memcmp(a->masks, b->masks, sizeof(a->masks));
}

static uint32_t apply_rop(DWORD rop, uint32_t src, uint32_t dst, uint32_t pat, uint32_t all) {
    switch (rop) {
    case SRCPAINT: return src | dst;
    case SRCAND: return src & dst;
    case SRCINVERT: return src ^ dst;
    case NOTSRCCOPY: return ~src & all;
    case DSTINVERT: return ~dst & all;
    case PATCOPY: return pat;
    default: return src;
    }
}

static void blit(struct dc *ddc, int dx, int dy, int dw, int dh, struct dc *sdc, int sx, int sy, int sw, int sh,
                 DWORD rop) {
    struct target dt, st;
    if (gdi_target(ddc, &dt)) return;
    int needs_src = rop != BLACKNESS && rop != WHITENESS && rop != PATCOPY && rop != DSTINVERT;
    if (needs_src && (!sdc || gdi_target(sdc, &st))) return;
    static int warned;
    if (!warned && rop != SRCCOPY && rop != SRCPAINT && rop != SRCAND && rop != SRCINVERT && rop != NOTSRCCOPY &&
        rop != DSTINVERT && rop != BLACKNESS && rop != WHITENESS && rop != PATCOPY) {
        fprintf(stderr, "chiron: raster op 0x%08x treated as SRCCOPY\n", rop);
        warned = 1;
    }

    // Negative extents mirror the image.
    int flip_x = (dw < 0) != (sw < 0), flip_y = (dh < 0) != (sh < 0);
    if (dw < 0) dx += dw + 1, dw = -dw;
    if (dh < 0) dy += dh + 1, dh = -dh;
    if (sw < 0) sx += sw + 1, sw = -sw;
    if (sh < 0) sy += sh + 1, sh = -sh;
    if (!dw || !dh) return;

    struct surface *d = dt.s, *s = needs_src ? st.s : NULL;
    uint32_t all = d->bpp == 32 ? 0xffffffffu : (1u << d->bpp) - 1;
    uint32_t fixed = rop == BLACKNESS ? gdi_rgb_to_raw(d, 0) : rop == WHITENESS ? gdi_rgb_to_raw(d, 0xffffff) : 0;
    uint32_t pat = 0;
    if (rop == PATCOPY) {
        struct brush *b = ddc->brush;
        pat = b->null ? 0 : gdi_colorref_to_raw(ddc, d, b->color);
    }
    int same = s && same_format(s, d);
    int unscaled = sw == dw && sh == dh && !flip_x && !flip_y;

    // A lookup table for 8-bit sources speeds up the common palette blit.
    uint32_t lut[256];
    if (s && s->bpp <= 8 && !same)
        for (int i = 0; i < s->ncolors; i++) lut[i] = gdi_rgb_to_raw(d, s->colors[i]);

    for (int y = 0; y < dh; y++) {
        int ty = dy + y + dt.oy;
        if (ty < dt.clip.top || ty >= dt.clip.bottom) continue;
        int yy = flip_y ? dh - 1 - y : y;
        int src_y = sy + (int)((int64_t)yy * sh / dh) + (s ? st.oy : 0);
        if (s && (src_y < st.clip.top || src_y >= st.clip.bottom)) continue;

        int x0 = dx + dt.ox, x1 = dx + dw + dt.ox;
        int start = x0 < dt.clip.left ? dt.clip.left - x0 : 0;
        int end = x1 > dt.clip.right ? dw - (x1 - dt.clip.right) : dw;
        if (start >= end) continue;

        if (same && unscaled && rop == SRCCOPY && s->bpp >= 8) {
            int sx0 = sx + st.ox + start, n = end - start;
            if (sx0 < st.clip.left) n -= st.clip.left - sx0, start += st.clip.left - sx0, sx0 = st.clip.left;
            if (sx0 + n > st.clip.right) n = st.clip.right - sx0;
            if (n > 0)
                memmove(surface_row(d, ty) + (x0 + start) * (d->bpp / 8), surface_row(s, src_y) + sx0 * (s->bpp / 8),
                        n * (d->bpp / 8));
            continue;
        }
        for (int x = start; x < end; x++) {
            int tx = x0 + x;
            uint32_t v;
            if (!needs_src) {
                v = rop == PATCOPY ? pat : rop == DSTINVERT ? ~gdi_get_raw(d, tx, ty) & all : fixed;
            } else {
                int xx = flip_x ? dw - 1 - x : x;
                int src_x = sx + (int)((int64_t)xx * sw / dw) + st.ox;
                if (src_x < st.clip.left || src_x >= st.clip.right) continue;
                uint32_t raw = gdi_get_raw(s, src_x, src_y);
                uint32_t src = same ? raw : s->bpp <= 8 ? lut[raw & 0xff] : gdi_rgb_to_raw(d, gdi_raw_to_rgb(s, raw));
                v = rop == SRCCOPY ? src : apply_rop(rop, src, gdi_get_raw(d, tx, ty), pat, all);
            }
            gdi_set_raw(d, tx, ty, v);
        }
    }
    gdi_target_done(ddc);
}

BOOL WINAPI BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
    struct dc *ddc = gdi_dc(dst);
    if (!ddc) return FALSE;
    blit(ddc, x, y, w, h, gdi_dc(src), sx, sy, w, h, rop);
    return TRUE;
}

BOOL WINAPI StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
    struct dc *ddc = gdi_dc(dst);
    if (!ddc) return FALSE;
    blit(ddc, x, y, w, h, gdi_dc(src), sx, sy, sw, sh, rop);
    return TRUE;
}

int WINAPI SetDIBits(HDC hdc, HGDIOBJ hbm, UINT start, UINT lines, const void *bits, const BITMAPINFOHEADER *info,
                     UINT usage) {
    struct bitmap *b = gdi_obj(hbm, GDI_BITMAP);
    if (!b || !b->s.bits) return 0;
    struct surface src;
    if (surface_from_info(&src, gdi_dc(hdc), info, usage)) return 0;
    src.bits = (uint8_t *)bits;
    // `start` counts scan lines from the bottom of the image.
    int n = lines < (UINT)src.height ? (int)lines : src.height;
    for (int i = 0; i < n; i++) {
        int y = src.height - 1 - (int)start - i;  // top-down row index
        if (y < 0 || y >= b->s.height) continue;
        for (int x = 0; x < src.width && x < b->s.width; x++) {
            uint32_t raw = gdi_get_raw(&src, x, y);
            gdi_set_raw(&b->s, x, y, same_format(&src, &b->s) ? raw : gdi_rgb_to_raw(&b->s, gdi_raw_to_rgb(&src, raw)));
        }
    }
    return n;
}

int gdi_bitmap_to_argb(HGDIOBJ color, HGDIOBJ mask, uint32_t **out, int *w, int *h) {
    struct bitmap *c = gdi_obj(color, GDI_BITMAP), *m = gdi_obj(mask, GDI_BITMAP);
    if (!m) return -1;
    // Without a colour bitmap the mask holds AND bits on top, XOR bits below.
    int width = m->s.width, height = c ? m->s.height : m->s.height / 2;
    uint32_t *px = malloc(width * height * 4);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            // A set (white) AND-mask bit leaves the screen showing through.
            int transparent = gdi_raw_to_rgb(&m->s, gdi_get_raw(&m->s, x, y)) != 0;
            uint32_t rgb = c ? gdi_raw_to_rgb(&c->s, gdi_get_raw(&c->s, x, y))
                             : gdi_raw_to_rgb(&m->s, gdi_get_raw(&m->s, x, y + height));
            px[y * width + x] = transparent ? 0 : 0xff000000u | rgb;
        }
    *out = px;
    *w = width;
    *h = height;
    return 0;
}
