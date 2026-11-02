// DDRAW: DirectDraw surfaces in system memory, shown through the SDL window.
// terranx.exe uses DirectDraw for its built-in movie player (the EA video
// library linked into the executable), and optionally for the whole game.
// While a primary surface exists it owns the screen; GDI drawing resumes when
// it is released.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../loader/modules.h"
#include "../loader/thunks.h"
#include "gdi.h"
#include "user32.h"

#define DD_OK 0
#define DDERR_INVALIDPARAMS 0x80070057u
#define DDERR_NOTFOUND 0x887600ffu
#define DDERR_UNSUPPORTED 0x80004001u
#define DDERR_NOPALETTEATTACHED 0x8876023cu
#define E_NOINTERFACE 0x80004002u

#define DDSD_CAPS 0x1
#define DDSD_HEIGHT 0x2
#define DDSD_WIDTH 0x4
#define DDSD_PITCH 0x8
#define DDSD_BACKBUFFERCOUNT 0x20
#define DDSD_LPSURFACE 0x800
#define DDSD_PIXELFORMAT 0x1000

#define DDSCAPS_BACKBUFFER 0x4
#define DDSCAPS_COMPLEX 0x8
#define DDSCAPS_FLIP 0x10
#define DDSCAPS_FRONTBUFFER 0x20
#define DDSCAPS_OFFSCREENPLAIN 0x40
#define DDSCAPS_PALETTE 0x100
#define DDSCAPS_PRIMARYSURFACE 0x200
#define DDSCAPS_SYSTEMMEMORY 0x800
#define DDSCAPS_VISIBLE 0x8000

#define DDPF_PALETTEINDEXED8 0x20
#define DDPF_RGB 0x40

#define DDBLT_COLORFILL 0x400
#define DDBLT_KEYSRC 0x8000
#define DDBLTFAST_SRCCOLORKEY 0x1

typedef struct {
    DWORD size, flags, fourcc, bits, rmask, gmask, bmask, amask;
} DDPIXELFORMAT;

typedef struct {
    DWORD size, flags, height, width;
    LONG pitch;
    DWORD backbuffers, refresh, alpha_bits, reserved;
    void *surface;
    DWORD ck_dest_overlay[2], ck_dest_blt[2], ck_src_overlay[2], ck_src_blt[2];
    DDPIXELFORMAT format;
    DWORD caps;
} DDSURFACEDESC;

_Static_assert(sizeof(DDSURFACEDESC) == 108, "DDSURFACEDESC layout");

typedef struct {
    BYTE r, g, b, flags;
} PALENTRY;

// ---- Objects ----------------------------------------------------------------

struct ddpalette {
    const void *vtbl;
    int refs;
    PALENTRY entries[256];
};

struct ddsurface {
    const void *vtbl;
    int refs;
    int width, height, bpp, pitch;
    DWORD caps;
    uint8_t *bits;
    struct ddsurface *back;  // flip chain: primary -> back buffer
    struct ddpalette *palette;
    DWORD src_colorkey;
    int has_colorkey;
};

struct ddraw {
    const void *vtbl;
    int refs;
};

static int mode_width = 640, mode_height = 480, mode_bpp = 8;
static struct ddsurface *primary;
static struct surface screen;  // XRGB copy of the primary for presenting

// Unimplemented methods trap with the interface and method name, so the
// next missing piece is obvious from the log.
static void *method_trap(const char *iface, const char *name) { return thunk_trap(iface, name, 0); }

// ---- Presenting -----------------------------------------------------------

// The movie player decodes and flips on its own thread, but only the SDL
// thread may render. Other threads convert the frame into `screen` and leave
// it for the SDL thread, which shows it the next time it pumps messages.
static pthread_mutex_t screen_lock = PTHREAD_MUTEX_INITIALIZER;
static int frame_pending;

static void present_primary(void) {
    struct ddsurface *s = primary;
    if (!s || !s->bits) return;
    pthread_mutex_lock(&screen_lock);
    if (screen.width != s->width || screen.height != s->height) {
        free(screen.bits);
        screen = (struct surface){.width = s->width, .height = s->height, .bpp = 32, .stride = s->width * 4};
        screen.bits = calloc((size_t)s->width * s->height, 4);
    }
    uint32_t pal[256] = {0};
    if (s->bpp == 8 && s->palette)
        for (int i = 0; i < 256; i++)
            pal[i] = s->palette->entries[i].r << 16 | s->palette->entries[i].g << 8 | s->palette->entries[i].b;
    for (int y = 0; y < s->height; y++) {
        const uint8_t *src = s->bits + (size_t)y * s->pitch;
        uint32_t *dst = (uint32_t *)(screen.bits + (size_t)y * screen.stride);
        for (int x = 0; x < s->width; x++) {
            switch (s->bpp) {
            case 8: dst[x] = pal[src[x]]; break;
            case 16: {
                uint16_t v = ((const uint16_t *)src)[x];  // RGB565
                dst[x] = ((v >> 11) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | ((v & 31) * 255 / 31);
                break;
            }
            case 32: dst[x] = ((const uint32_t *)src)[x] & 0xffffff; break;
            }
        }
    }
    if (user32_on_sdl_thread()) {
        sdl_host_present(&screen, "Sid Meier's Alpha Centauri");
        frame_pending = 0;
    } else {
        frame_pending = 1;
    }
    pthread_mutex_unlock(&screen_lock);
    if (frame_pending) sdl_host_wake();
}

void ddraw_present_pending(void) {
    if (!frame_pending) return;
    pthread_mutex_lock(&screen_lock);
    if (frame_pending && primary) sdl_host_present(&screen, "Sid Meier's Alpha Centauri");
    frame_pending = 0;
    pthread_mutex_unlock(&screen_lock);
}

int ddraw_owns_screen(void) { return primary != NULL; }

// Give the screen back to GDI. Windows does this on RestoreDisplayMode, on
// leaving exclusive mode and when the DirectDraw object goes away, even if
// the game still holds references to its surfaces.
static void release_screen(void) {
    if (!primary) return;
    pthread_mutex_lock(&screen_lock);
    primary = NULL;
    frame_pending = 0;
    pthread_mutex_unlock(&screen_lock);
    InvalidateRect(NULL, NULL, TRUE);
    user32_screen_dirty();
}

// ---- IDirectDrawPalette ---------------------------------------------------

static DWORD WINAPI pal_QueryInterface(struct ddpalette *p, const void *iid, void **out) {
    *out = NULL;
    return E_NOINTERFACE;
}
static DWORD WINAPI pal_AddRef(struct ddpalette *p) { return ++p->refs; }
static DWORD WINAPI pal_Release(struct ddpalette *p) {
    if (--p->refs > 0) return p->refs;
    free(p);
    return 0;
}
static DWORD WINAPI pal_GetCaps(struct ddpalette *p, DWORD *caps) {
    *caps = 0x4 | 0x40;  // DDPCAPS_8BIT | DDPCAPS_ALLOW256
    return DD_OK;
}
static DWORD WINAPI pal_GetEntries(struct ddpalette *p, DWORD flags, DWORD base, DWORD n, PALENTRY *out) {
    for (DWORD i = 0; i < n && base + i < 256; i++) out[i] = p->entries[base + i];
    return DD_OK;
}
static DWORD WINAPI pal_Initialize(struct ddpalette *p, void *dd, DWORD flags, PALENTRY *e) { return DD_OK; }
static DWORD WINAPI pal_SetEntries(struct ddpalette *p, DWORD flags, DWORD base, DWORD n, const PALENTRY *in) {
    for (DWORD i = 0; i < n && base + i < 256; i++) p->entries[base + i] = in[i];
    if (primary && primary->palette == p) present_primary();
    return DD_OK;
}

static const void *palette_vtbl[] = {pal_QueryInterface, pal_AddRef, pal_Release, pal_GetCaps,
                                     pal_GetEntries, pal_Initialize, pal_SetEntries};

// ---- IDirectDrawClipper ---------------------------------------------------
// Clippers only matter for windowed DirectDraw; accept and ignore them.

struct ddclipper {
    const void *vtbl;
    int refs;
    HWND hwnd;
};

static DWORD WINAPI clip_QueryInterface(struct ddclipper *c, const void *iid, void **out) {
    *out = NULL;
    return E_NOINTERFACE;
}
static DWORD WINAPI clip_AddRef(struct ddclipper *c) { return ++c->refs; }
static DWORD WINAPI clip_Release(struct ddclipper *c) {
    if (--c->refs > 0) return c->refs;
    free(c);
    return 0;
}
static DWORD WINAPI clip_GetClipList(struct ddclipper *c, const RECT *r, void *list, DWORD *size) {
    return DDERR_UNSUPPORTED;
}
static DWORD WINAPI clip_GetHWnd(struct ddclipper *c, HWND *out) {
    *out = c->hwnd;
    return DD_OK;
}
static DWORD WINAPI clip_Initialize(struct ddclipper *c, void *dd, DWORD flags) { return DD_OK; }
static DWORD WINAPI clip_IsClipListChanged(struct ddclipper *c, BOOL *changed) {
    *changed = FALSE;
    return DD_OK;
}
static DWORD WINAPI clip_SetClipList(struct ddclipper *c, const void *list, DWORD flags) { return DD_OK; }
static DWORD WINAPI clip_SetHWnd(struct ddclipper *c, DWORD flags, HWND hwnd) {
    c->hwnd = hwnd;
    return DD_OK;
}

static const void *clipper_vtbl[] = {clip_QueryInterface, clip_AddRef,           clip_Release,
                                     clip_GetClipList,    clip_GetHWnd,          clip_Initialize,
                                     clip_IsClipListChanged, clip_SetClipList, clip_SetHWnd};

// ---- IDirectDrawSurface ---------------------------------------------------

static void *surface_vtbl[36];

static void fill_format(DDPIXELFORMAT *f, int bpp) {
    memset(f, 0, sizeof(*f));
    f->size = sizeof(*f);
    f->bits = bpp;
    if (bpp == 8) {
        f->flags = DDPF_RGB | DDPF_PALETTEINDEXED8;
    } else if (bpp == 16) {
        f->flags = DDPF_RGB;
        f->rmask = 0xf800, f->gmask = 0x07e0, f->bmask = 0x001f;
    } else {
        f->flags = DDPF_RGB;
        f->rmask = 0xff0000, f->gmask = 0xff00, f->bmask = 0xff;
    }
}

static struct ddsurface *new_surface(int w, int h, int bpp, DWORD caps) {
    struct ddsurface *s = calloc(1, sizeof(*s));
    s->vtbl = surface_vtbl;
    s->refs = 1;
    s->width = w;
    s->height = h;
    s->bpp = bpp;
    s->pitch = ((w * bpp / 8) + 3) & ~3;
    s->caps = caps | DDSCAPS_SYSTEMMEMORY;
    s->bits = calloc(1, (size_t)s->pitch * h + 4);
    return s;
}

static DWORD WINAPI surf_QueryInterface(struct ddsurface *s, const uint8_t *iid, void **out) {
    // IDirectDrawSurface2/3 share the v1 layout plus extra methods at the
    // end; the game only uses v1 methods through them.
    uint32_t d1 = *(const uint32_t *)iid;
    if (d1 == 0x57805885 || d1 == 0xda044e00 || d1 == 0x6c14db80) {  // Surface2, Surface3, Surface
        s->refs++;
        *out = s;
        return DD_OK;
    }
    fprintf(stderr, "chiron: IDirectDrawSurface::QueryInterface {%08x-...} not supported\n", d1);
    *out = NULL;
    return E_NOINTERFACE;
}

static DWORD WINAPI surf_AddRef(struct ddsurface *s) { return ++s->refs; }

static DWORD WINAPI surf_Release(struct ddsurface *s) {
    if (--s->refs > 0) return s->refs;
    if (s == primary) release_screen();
    if (s->back) surf_Release(s->back);
    if (s->palette) pal_Release(s->palette);
    free(s->bits);
    free(s);
    return 0;
}

static DWORD WINAPI surf_GetAttachedSurface(struct ddsurface *s, const DWORD *caps, struct ddsurface **out) {
    if (s->back && (*caps & DDSCAPS_BACKBUFFER)) {
        s->back->refs++;
        *out = s->back;
        return DD_OK;
    }
    *out = NULL;
    return DDERR_NOTFOUND;
}

static DWORD WINAPI surf_GetCaps(struct ddsurface *s, DWORD *caps) {
    *caps = s->caps;
    return DD_OK;
}

static void describe(struct ddsurface *s, DDSURFACEDESC *d) {
    DWORD size = d->size ? d->size : sizeof(*d);
    memset(d, 0, size < sizeof(*d) ? size : sizeof(*d));
    d->size = size;
    d->flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_LPSURFACE;
    d->width = s->width;
    d->height = s->height;
    d->pitch = s->pitch;
    d->caps = s->caps;
    fill_format(&d->format, s->bpp);
}

static DWORD WINAPI surf_GetSurfaceDesc(struct ddsurface *s, DDSURFACEDESC *d) {
    describe(s, d);
    return DD_OK;
}

static DWORD WINAPI surf_GetPixelFormat(struct ddsurface *s, DDPIXELFORMAT *f) {
    fill_format(f, s->bpp);
    return DD_OK;
}

static DWORD WINAPI surf_Lock(struct ddsurface *s, const RECT *r, DDSURFACEDESC *d, DWORD flags, HANDLE event) {
    describe(s, d);
    d->surface = s->bits;
    if (r) d->surface = s->bits + (size_t)r->top * s->pitch + r->left * (s->bpp / 8);
    return DD_OK;
}

static DWORD WINAPI surf_Unlock(struct ddsurface *s, void *p) {
    if (s == primary) present_primary();
    return DD_OK;
}

static DWORD WINAPI surf_Flip(struct ddsurface *s, struct ddsurface *target, DWORD flags) {
    struct ddsurface *b = target ? target : s->back;
    if (!b) return DDERR_INVALIDPARAMS;
    uint8_t *tmp = s->bits;
    s->bits = b->bits;
    b->bits = tmp;
    if (s == primary) present_primary();
    return DD_OK;
}

static int bytes_pp(struct ddsurface *s) { return s->bpp / 8; }

static void read_rect(struct ddsurface *s, const RECT *r, RECT *out) {
    *out = r ? *r : (RECT){0, 0, s->width, s->height};
}

// Copy (scaling with nearest neighbour) between surfaces of the same format,
// optionally skipping the source colour key.
static void copy_rect(struct ddsurface *dst, RECT dr, struct ddsurface *src, RECT sr, int keyed) {
    int bpp = bytes_pp(dst), dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || bytes_pp(src) != bpp) return;
    for (int y = 0; y < dh; y++) {
        int ty = dr.top + y, sy = sr.top + y * sh / dh;
        if (ty < 0 || ty >= dst->height || sy < 0 || sy >= src->height) continue;
        uint8_t *drow = dst->bits + (size_t)ty * dst->pitch;
        const uint8_t *srow = src->bits + (size_t)sy * src->pitch;
        if (!keyed && sw == dw && dr.left >= 0 && dr.right <= dst->width && sr.left >= 0 && sr.right <= src->width) {
            memmove(drow + dr.left * bpp, srow + sr.left * bpp, (size_t)dw * bpp);
            continue;
        }
        for (int x = 0; x < dw; x++) {
            int tx = dr.left + x, sx = sr.left + x * sw / dw;
            if (tx < 0 || tx >= dst->width || sx < 0 || sx >= src->width) continue;
            uint32_t v = 0;
            memcpy(&v, srow + sx * bpp, bpp);
            if (keyed && v == src->src_colorkey) continue;
            memcpy(drow + tx * bpp, &v, bpp);
        }
    }
}

static void fill_rect(struct ddsurface *dst, RECT r, uint32_t color) {
    int bpp = bytes_pp(dst);
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > dst->width) r.right = dst->width;
    if (r.bottom > dst->height) r.bottom = dst->height;
    for (int y = r.top; y < r.bottom; y++) {
        uint8_t *row = dst->bits + (size_t)y * dst->pitch;
        for (int x = r.left; x < r.right; x++) memcpy(row + x * bpp, &color, bpp);
    }
}

static DWORD WINAPI surf_Blt(struct ddsurface *s, const RECT *dr, struct ddsurface *src, const RECT *sr, DWORD flags,
                             const DWORD *fx) {
    RECT d, r;
    read_rect(s, dr, &d);
    if (flags & DDBLT_COLORFILL) {
        fill_rect(s, d, fx[20]);  // DDBLTFX.dwFillColor
    } else if (src) {
        read_rect(src, sr, &r);
        copy_rect(s, d, src, r, (flags & DDBLT_KEYSRC) && src->has_colorkey);
    }
    if (s == primary) present_primary();
    return DD_OK;
}

static DWORD WINAPI surf_BltFast(struct ddsurface *s, DWORD x, DWORD y, struct ddsurface *src, const RECT *sr,
                                 DWORD trans) {
    RECT r;
    read_rect(src, sr, &r);
    RECT d = {x, y, x + (r.right - r.left), y + (r.bottom - r.top)};
    copy_rect(s, d, src, r, (trans & DDBLTFAST_SRCCOLORKEY) && src->has_colorkey);
    if (s == primary) present_primary();
    return DD_OK;
}

static DWORD WINAPI surf_GetBltStatus(struct ddsurface *s, DWORD flags) { return DD_OK; }
static DWORD WINAPI surf_GetFlipStatus(struct ddsurface *s, DWORD flags) { return DD_OK; }
static DWORD WINAPI surf_IsLost(struct ddsurface *s) { return DD_OK; }
static DWORD WINAPI surf_Restore(struct ddsurface *s) { return DD_OK; }

static DWORD WINAPI surf_GetPalette(struct ddsurface *s, struct ddpalette **out) {
    if (!s->palette) {
        *out = NULL;
        return DDERR_NOPALETTEATTACHED;
    }
    s->palette->refs++;
    *out = s->palette;
    return DD_OK;
}

static DWORD WINAPI surf_SetPalette(struct ddsurface *s, struct ddpalette *p) {
    if (p) p->refs++;
    if (s->palette) pal_Release(s->palette);
    s->palette = p;
    if (s->back && p) surf_SetPalette(s->back, p);
    return DD_OK;
}

static DWORD WINAPI surf_SetColorKey(struct ddsurface *s, DWORD flags, const DWORD *key) {
    if (flags & 0x8) {  // DDCKEY_SRCBLT
        s->has_colorkey = key != NULL;
        if (key) s->src_colorkey = key[0];
    }
    return DD_OK;
}

static DWORD WINAPI surf_GetColorKey(struct ddsurface *s, DWORD flags, DWORD *key) {
    if (!(flags & 0x8) || !s->has_colorkey) return DDERR_NOTFOUND;
    key[0] = key[1] = s->src_colorkey;
    return DD_OK;
}

static DWORD WINAPI surf_SetClipper(struct ddsurface *s, void *clipper) { return DD_OK; }

static const struct {
    int slot;
    const char *name;
    void *fn;
} surface_methods[] = {
    {0, "QueryInterface", surf_QueryInterface},
    {1, "AddRef", surf_AddRef},
    {2, "Release", surf_Release},
    {3, "AddAttachedSurface", NULL},
    {4, "AddOverlayDirtyRect", NULL},
    {5, "Blt", surf_Blt},
    {6, "BltBatch", NULL},
    {7, "BltFast", surf_BltFast},
    {8, "DeleteAttachedSurface", NULL},
    {9, "EnumAttachedSurfaces", NULL},
    {10, "EnumOverlayZOrders", NULL},
    {11, "Flip", surf_Flip},
    {12, "GetAttachedSurface", surf_GetAttachedSurface},
    {13, "GetBltStatus", surf_GetBltStatus},
    {14, "GetCaps", surf_GetCaps},
    {15, "GetClipper", NULL},
    {16, "GetColorKey", surf_GetColorKey},
    {17, "GetDC", NULL},
    {18, "GetFlipStatus", surf_GetFlipStatus},
    {19, "GetOverlayPosition", NULL},
    {20, "GetPalette", surf_GetPalette},
    {21, "GetPixelFormat", surf_GetPixelFormat},
    {22, "GetSurfaceDesc", surf_GetSurfaceDesc},
    {23, "Initialize", NULL},
    {24, "IsLost", surf_IsLost},
    {25, "Lock", surf_Lock},
    {26, "ReleaseDC", NULL},
    {27, "Restore", surf_Restore},
    {28, "SetClipper", surf_SetClipper},
    {29, "SetColorKey", surf_SetColorKey},
    {30, "SetOverlayPosition", NULL},
    {31, "SetPalette", surf_SetPalette},
    {32, "Unlock", surf_Unlock},
    {33, "UpdateOverlay", NULL},
    {34, "UpdateOverlayDisplay", NULL},
    {35, "UpdateOverlayZOrder", NULL},
};

// ---- IDirectDraw ----------------------------------------------------------

static void *ddraw_vtbl[24], *ddraw2_vtbl[24];

static DWORD WINAPI dd_QueryInterface(struct ddraw *dd, const uint8_t *iid, void **out) {
    uint32_t d1 = *(const uint32_t *)iid;
    if (d1 == 0x6c14db80 || d1 == 0xb3a6f3e0) {  // IID_IDirectDraw, IID_IDirectDraw2
        // Each interface version is its own small object with the matching
        // vtable; they share all state, which is global.
        struct ddraw *o = calloc(1, sizeof(*o));
        o->vtbl = d1 == 0xb3a6f3e0 ? ddraw2_vtbl : ddraw_vtbl;
        o->refs = 1;
        *out = o;
        return DD_OK;
    }
    fprintf(stderr, "chiron: IDirectDraw::QueryInterface {%08x-...} not supported\n", d1);
    *out = NULL;
    return E_NOINTERFACE;
}

static DWORD WINAPI dd_AddRef(struct ddraw *dd) { return ++dd->refs; }
static DWORD WINAPI dd_Release(struct ddraw *dd) {
    if (--dd->refs > 0) return dd->refs;
    release_screen();
    free(dd);
    return 0;
}

static DWORD WINAPI dd_CreateClipper(struct ddraw *dd, DWORD flags, struct ddclipper **out, void *outer) {
    struct ddclipper *c = calloc(1, sizeof(*c));
    c->vtbl = clipper_vtbl;
    c->refs = 1;
    *out = c;
    return DD_OK;
}

static DWORD WINAPI dd_CreatePalette(struct ddraw *dd, DWORD flags, const PALENTRY *entries, struct ddpalette **out,
                                     void *outer) {
    struct ddpalette *p = calloc(1, sizeof(*p));
    p->vtbl = palette_vtbl;
    p->refs = 1;
    if (entries) memcpy(p->entries, entries, sizeof(p->entries));
    *out = p;
    return DD_OK;
}

static DWORD WINAPI dd_CreateSurface(struct ddraw *dd, const DDSURFACEDESC *d, struct ddsurface **out, void *outer) {
    DWORD caps = (d->flags & DDSD_CAPS) ? d->caps : 0;
    struct ddsurface *s;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        s = new_surface(mode_width, mode_height, mode_bpp, caps | DDSCAPS_VISIBLE | DDSCAPS_FRONTBUFFER);
        if ((caps & DDSCAPS_FLIP) && (d->flags & DDSD_BACKBUFFERCOUNT) && d->backbuffers)
            s->back = new_surface(mode_width, mode_height, mode_bpp, DDSCAPS_BACKBUFFER | DDSCAPS_FLIP);
        primary = s;
    } else {
        int bpp = (d->flags & DDSD_PIXELFORMAT) ? (int)d->format.bits : mode_bpp;
        if (!(d->flags & DDSD_WIDTH) || !(d->flags & DDSD_HEIGHT)) return DDERR_INVALIDPARAMS;
        s = new_surface(d->width, d->height, bpp, caps ? caps : DDSCAPS_OFFSCREENPLAIN);
    }
    if (modules_trace)
        fprintf(stderr, "[ddraw] CreateSurface %dx%dx%d caps=0x%x%s\n", s->width, s->height, s->bpp, caps,
                s->back ? " +backbuffer" : "");
    *out = s;
    return DD_OK;
}

typedef DWORD(WINAPI *ENUMMODESCALLBACK)(DDSURFACEDESC *desc, void *ctx);

static DWORD WINAPI dd_EnumDisplayModes(struct ddraw *dd, DWORD flags, const DDSURFACEDESC *filter, void *ctx,
                                        ENUMMODESCALLBACK cb) {
    static const int modes[][2] = {{640, 480}, {800, 600}, {1024, 768}};
    static const int depths[] = {8, 16, 32};
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
        for (size_t b = 0; b < sizeof(depths) / sizeof(depths[0]); b++) {
            DDSURFACEDESC desc = {.size = sizeof(desc),
                                  .flags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT,
                                  .width = modes[m][0], .height = modes[m][1],
                                  .pitch = modes[m][0] * depths[b] / 8};
            fill_format(&desc.format, depths[b]);
            if (filter && (filter->flags & DDSD_WIDTH) && filter->width != desc.width) continue;
            if (filter && (filter->flags & DDSD_HEIGHT) && filter->height != desc.height) continue;
            if (filter && (filter->flags & DDSD_PIXELFORMAT) && filter->format.bits != desc.format.bits) continue;
            if (cb(&desc, ctx) == 0) return DD_OK;  // DDENUMRET_CANCEL
        }
    return DD_OK;
}

static DWORD WINAPI dd_FlipToGDISurface(struct ddraw *dd) { return DD_OK; }

static DWORD WINAPI dd_GetCaps(struct ddraw *dd, DWORD *driver, DWORD *hel) {
    DWORD *caps[2] = {driver, hel};
    for (int i = 0; i < 2; i++) {
        if (!caps[i]) continue;
        DWORD size = caps[i][0];
        memset(caps[i] + 1, 0, size - 4);
        caps[i][1] = 0x40 | 0x200 | 0x8000 | 0x40000;  // BLT, BLTSTRETCH, PALETTE, COLORKEY
        caps[i][2] = 0x2000;                           // DDCAPS2_WIDESURFACES
    }
    return DD_OK;
}

static DWORD WINAPI dd_GetDisplayMode(struct ddraw *dd, DDSURFACEDESC *d) {
    memset(d, 0, sizeof(*d));
    d->size = sizeof(*d);
    d->flags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
    d->width = mode_width;
    d->height = mode_height;
    d->pitch = mode_width * mode_bpp / 8;
    fill_format(&d->format, mode_bpp);
    return DD_OK;
}

static DWORD WINAPI dd_GetMonitorFrequency(struct ddraw *dd, DWORD *hz) {
    *hz = 60;
    return DD_OK;
}

static DWORD WINAPI dd_GetVerticalBlankStatus(struct ddraw *dd, BOOL *in_vblank) {
    *in_vblank = TRUE;
    return DD_OK;
}

static DWORD WINAPI dd_RestoreDisplayMode(struct ddraw *dd) {
    release_screen();
    return DD_OK;
}

static DWORD WINAPI dd_SetCooperativeLevel(struct ddraw *dd, HWND hwnd, DWORD flags) {
    if (flags & 0x8) release_screen();  // DDSCL_NORMAL
    return DD_OK;
}

static DWORD WINAPI dd_SetDisplayMode(struct ddraw *dd, DWORD w, DWORD h, DWORD bpp) {
    if (modules_trace) fprintf(stderr, "[ddraw] SetDisplayMode %ux%ux%u\n", w, h, bpp);
    if (bpp != 8 && bpp != 16 && bpp != 32) return DDERR_UNSUPPORTED;
    mode_width = w;
    mode_height = h;
    mode_bpp = bpp;
    return DD_OK;
}

// IDirectDraw2 adds a refresh rate and flags.
static DWORD WINAPI dd2_SetDisplayMode(struct ddraw *dd, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags) {
    return dd_SetDisplayMode(dd, w, h, bpp);
}

static DWORD WINAPI dd2_GetAvailableVidMem(struct ddraw *dd, const DWORD *caps, DWORD *total, DWORD *free_mem) {
    if (total) *total = 64u << 20;
    if (free_mem) *free_mem = 64u << 20;
    return DD_OK;
}

static DWORD WINAPI dd_WaitForVerticalBlank(struct ddraw *dd, DWORD flags, HANDLE event) { return DD_OK; }

static const struct {
    int slot;
    const char *name;
    void *fn;
} ddraw_methods[] = {
    {0, "QueryInterface", dd_QueryInterface},
    {1, "AddRef", dd_AddRef},
    {2, "Release", dd_Release},
    {3, "Compact", NULL},
    {4, "CreateClipper", dd_CreateClipper},
    {5, "CreatePalette", dd_CreatePalette},
    {6, "CreateSurface", dd_CreateSurface},
    {7, "DuplicateSurface", NULL},
    {8, "EnumDisplayModes", dd_EnumDisplayModes},
    {9, "EnumSurfaces", NULL},
    {10, "FlipToGDISurface", dd_FlipToGDISurface},
    {11, "GetCaps", dd_GetCaps},
    {12, "GetDisplayMode", dd_GetDisplayMode},
    {13, "GetFourCCCodes", NULL},
    {14, "GetGDISurface", NULL},
    {15, "GetMonitorFrequency", dd_GetMonitorFrequency},
    {16, "GetScanLine", NULL},
    {17, "GetVerticalBlankStatus", dd_GetVerticalBlankStatus},
    {18, "Initialize", NULL},
    {19, "RestoreDisplayMode", dd_RestoreDisplayMode},
    {20, "SetCooperativeLevel", dd_SetCooperativeLevel},
    {21, "SetDisplayMode", dd_SetDisplayMode},
    {22, "WaitForVerticalBlank", dd_WaitForVerticalBlank},
};

static void build_vtables(void) {
    static int done;
    if (done) return;
    done = 1;
    for (size_t i = 0; i < sizeof(surface_methods) / sizeof(surface_methods[0]); i++)
        surface_vtbl[surface_methods[i].slot] =
            surface_methods[i].fn ? surface_methods[i].fn : method_trap("IDirectDrawSurface", surface_methods[i].name);
    for (size_t i = 0; i < sizeof(ddraw_methods) / sizeof(ddraw_methods[0]); i++)
        ddraw_vtbl[ddraw_methods[i].slot] =
            ddraw_methods[i].fn ? ddraw_methods[i].fn : method_trap("IDirectDraw", ddraw_methods[i].name);
    ddraw_vtbl[23] = method_trap("IDirectDraw", "(slot 23)");
    memcpy(ddraw2_vtbl, ddraw_vtbl, sizeof(ddraw_vtbl));
    ddraw2_vtbl[21] = dd2_SetDisplayMode;
    ddraw2_vtbl[23] = dd2_GetAvailableVidMem;
}

// ---- Exports --------------------------------------------------------------

DWORD WINAPI DirectDrawCreate(const void *guid, struct ddraw **out, void *outer) {
    build_vtables();
    struct ddraw *dd = calloc(1, sizeof(*dd));
    dd->vtbl = ddraw_vtbl;
    dd->refs = 1;
    *out = dd;
    return DD_OK;
}

typedef BOOL(WINAPI *DDENUMCALLBACKA)(void *guid, const char *desc, const char *name, void *ctx);

DWORD WINAPI DirectDrawEnumerateA(DDENUMCALLBACKA cb, void *ctx) {
    cb(NULL, "Primary Display Driver", "display", ctx);
    return DD_OK;
}
