// Internal interface between the GDI32 shim files.
#pragma once

#include "gdi.h"

enum gdi_type { GDI_BITMAP = 1, GDI_PEN, GDI_BRUSH, GDI_FONT, GDI_PALETTE, GDI_REGION };

#define GDIOBJ_MAGIC 0x4f494447u  // "GDIO"

struct gdiobj {
    uint32_t magic;
    enum gdi_type type;
    int stock;  // stock objects are never freed
};

struct bitmap {
    struct gdiobj obj;
    struct surface s;
};

struct pen {
    struct gdiobj obj;
    UINT style;  // PS_SOLID 0, PS_NULL 5
    int width;
    COLORREF color;
};

struct brush {
    struct gdiobj obj;
    int null;
    COLORREF color;
};

typedef struct {
    LONG height, width, escapement, orientation, weight;
    BYTE italic, underline, strikeout, charset, out_precision, clip_precision, quality, pitch_family;
    char face[32];
} LOGFONTA;

struct font {
    struct gdiobj obj;
    LOGFONTA lf;
    void *face;  // resolved by gdi32_text.c
};

typedef struct {
    BYTE r, g, b, flags;
} PALETTEENTRY;

struct palette {
    struct gdiobj obj;
    int count;
    PALETTEENTRY entries[256];
};

struct region {
    struct gdiobj obj;
    RECT rect;
};

#define DC_MAGIC 0x43444443u  // "CDDC"

struct dc {
    uint32_t magic;
    int is_window;  // window/screen DC rather than a memory DC
    HWND hwnd;
    struct bitmap *bitmap;
    struct pen *pen;
    struct brush *brush;
    struct font *font;
    struct palette *palette;
    COLORREF text_color, bk_color;
    int bk_mode;  // TRANSPARENT 1, OPAQUE 2
    UINT text_align;
    POINT pos;
    int has_clip;
    RECT clip;  // in DC coordinates
    int stretch_mode, arc_direction;
};

// Where drawing through a DC lands: a surface, the DC origin within it, and
// the clip rectangle in surface coordinates.
struct target {
    struct surface *s;
    int ox, oy;
    RECT clip;
};

struct dc *gdi_dc(HDC hdc);
void *gdi_obj(HGDIOBJ h, enum gdi_type type);
int gdi_target(struct dc *dc, struct target *t);
void gdi_target_done(struct dc *dc);  // after drawing: schedule a screen update

// Pixel access. Raw values are in the surface's own format.
uint32_t gdi_get_raw(const struct surface *s, int x, int y);
void gdi_set_raw(struct surface *s, int x, int y, uint32_t v);
uint32_t gdi_raw_to_rgb(const struct surface *s, uint32_t raw);  // -> 0x00RRGGBB
uint32_t gdi_rgb_to_raw(struct surface *s, uint32_t rgb);
// A COLORREF as a raw pixel value for the DC's target surface.
uint32_t gdi_colorref_to_raw(struct dc *dc, struct surface *s, COLORREF c);
void gdi_fill_span(struct target *t, int x0, int x1, int y, uint32_t raw);  // [x0, x1) in DC coords

void gdi_text_release_font(struct font *f);
