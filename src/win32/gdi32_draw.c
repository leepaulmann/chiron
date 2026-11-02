// GDI32: lines and filled shapes.
#include <math.h>
#include <stdlib.h>

#include "gdi32.h"

BOOL WINAPI MoveToEx(HDC hdc, int x, int y, POINT *old) {
    struct dc *dc = gdi_dc(hdc);
    if (!dc) return FALSE;
    if (old) *old = dc->pos;
    dc->pos = (POINT){x, y};
    return TRUE;
}

static void plot(struct target *t, int x, int y, uint32_t raw) { gdi_fill_span(t, x, x + 1, y, raw); }

static void pen_dot(struct target *t, int x, int y, int width, uint32_t raw) {
    if (width <= 1) {
        plot(t, x, y, raw);
        return;
    }
    int r = width / 2;
    for (int yy = -r; yy < width - r; yy++) gdi_fill_span(t, x - r, x - r + width, y + yy, raw);
}

// Bresenham from the current position to (x, y), excluding the end point as
// Windows does.
BOOL WINAPI LineTo(HDC hdc, int x, int y) {
    struct dc *dc = gdi_dc(hdc);
    struct target t;
    if (!dc) return FALSE;
    int x0 = dc->pos.x, y0 = dc->pos.y;
    dc->pos = (POINT){x, y};
    if (dc->pen->style == 5 || gdi_target(dc, &t)) return TRUE;  // PS_NULL
    uint32_t raw = gdi_colorref_to_raw(dc, t.s, dc->pen->color);
    int dx = abs(x - x0), sx = x0 < x ? 1 : -1, dy = -abs(y - y0), sy = y0 < y ? 1 : -1, err = dx + dy;
    while (x0 != x || y0 != y) {
        pen_dot(&t, x0, y0, dc->pen->width, raw);
        int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
    gdi_target_done(dc);
    return TRUE;
}

// Fill and outline the ellipse inscribed in [l, r) x [t, b), optionally
// limited to the pie slice between two radials (counter-clockwise from the
// first to the second, as with the default arc direction).
static BOOL ellipse(HDC hdc, int l, int top, int r, int b, int pie, double a0, double a1) {
    struct dc *dc = gdi_dc(hdc);
    struct target t;
    if (!dc) return FALSE;
    if (gdi_target(dc, &t)) return TRUE;
    double cx = (l + r - 1) / 2.0, cy = (top + b - 1) / 2.0, rx = (r - l) / 2.0, ry = (b - top) / 2.0;
    if (rx <= 0 || ry <= 0) return TRUE;
    int fill = !dc->brush->null, outline = dc->pen->style != 5;
    uint32_t fill_raw = fill ? gdi_colorref_to_raw(dc, t.s, dc->brush->color) : 0;
    uint32_t pen_raw = outline ? gdi_colorref_to_raw(dc, t.s, dc->pen->color) : 0;
    double span = fmod(a1 - a0 + 4 * M_PI, 2 * M_PI);
    for (int y = top; y < b; y++) {
        double ny = (y - cy) / ry;
        if (fabs(ny) > 1) continue;
        double half = rx * sqrt(1 - ny * ny);
        int x0 = (int)ceil(cx - half), x1 = (int)floor(cx + half);
        for (int x = x0; x <= x1; x++) {
            if (pie) {
                // Screen y grows downwards; angles are measured upwards.
                double a = fmod(atan2(cy - y, x - cx) - a0 + 4 * M_PI, 2 * M_PI);
                if (a > span) continue;
            }
            int edge = x == x0 || x == x1;
            if (!edge) {
                double ny2 = (y + (y < cy ? -1 : 1) - cy) / ry;
                edge = fabs(ny2) > 1 || fabs(x - cx) > rx * sqrt(1 - ny2 * ny2);
            }
            if (edge && outline) plot(&t, x, y, pen_raw);
            else if (fill) plot(&t, x, y, fill_raw);
        }
    }
    gdi_target_done(dc);
    return TRUE;
}

BOOL WINAPI Ellipse(HDC hdc, int l, int t, int r, int b) { return ellipse(hdc, l, t, r, b, 0, 0, 0); }

BOOL WINAPI Pie(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2) {
    double cx = (l + r) / 2.0, cy = (t + b) / 2.0;
    double a0 = atan2(cy - y1, x1 - cx), a1 = atan2(cy - y2, x2 - cx);
    struct dc *dc = gdi_dc(hdc);
    if (dc && dc->arc_direction == 2) {  // AD_CLOCKWISE: swap the radials
        double tmp = a0;
        a0 = a1;
        a1 = tmp;
    }
    return ellipse(hdc, l, t, r, b, 1, a0, a1);
}
