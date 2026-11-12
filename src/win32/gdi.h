// Internal interface shared by the USER32 and GDI32 shims.
#pragma once

#include "win32.h"

typedef void *HWND;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef uint32_t COLORREF;
typedef intptr_t LRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;

typedef struct {
    LONG left, top, right, bottom;
} RECT;

typedef struct {
    LONG x, y;
} POINT;

// A pixel buffer: a DIB section, a device-dependent bitmap, or a window's
// framebuffer. Rows are top-down unless `bottom_up` is set (DIB convention).
struct surface {
    int width, height;
    int bpp;  // 1, 4, 8, 16, 24 or 32
    int stride;
    int bottom_up;
    uint8_t *bits;
    uint32_t masks[3];        // 16/32 bpp channel masks (R, G, B)
    uint32_t colors[256];     // colour table as 0x00RRGGBB for <= 8 bpp
    int ncolors;
    uint8_t *inverse;         // lazily built RGB555 -> palette index map
};

static inline uint8_t *surface_row(const struct surface *s, int y) {
    return s->bits + (size_t)(s->bottom_up ? s->height - 1 - y : y) * s->stride;
}
void surface_colors_changed(struct surface *s);
// A device context's state (colours, modes, selected objects) and its size,
// for the engine's shadow checks; NULL if hdc isn't a DC.
void *gdi_dc_state(void *hdc, int *size);

// Display settings reported to the game.
extern int display_width, display_height, display_bpp;
extern int audio_volume;  // percent, applied to the final mix

// ---- USER32 services used by GDI32 ----------------------------------------

// The framebuffer a window draws into, and the window's client rectangle in
// that framebuffer's coordinates. Returns NULL for an unknown window; a NULL
// hwnd means the whole screen.
struct surface *user32_window_surface(HWND hwnd, RECT *client);
// Note that part of the screen changed so the next present picks it up, and
// present now if a frame's worth of time has passed.
void user32_screen_dirty(void);
void user32_present_if_due(void);

// Make the calling thread the one that owns SDL and receives input.
void user32_set_sdl_thread(void);

// ---- GDI32 services used by USER32 ----------------------------------------

HDC gdi_create_window_dc(HWND hwnd);
void gdi_release_window_dc(HDC hdc);
int gdi_fill_rect(HDC hdc, const RECT *r, HGDIOBJ brush);
// For CreateIconIndirect: copy a bitmap out as 32-bit ARGB.
int gdi_bitmap_to_argb(HGDIOBJ bitmap, HGDIOBJ mask, uint32_t **out, int *w, int *h);

// True while a DirectDraw primary surface owns the screen (ddraw.c).
int ddraw_owns_screen(void);
// Show a frame another thread flipped; call on the SDL thread.
void ddraw_present_pending(void);
int user32_on_sdl_thread(void);

// ---- SDL host (sdl_host.c) -------------------------------------------------

void sdl_host_init(void);
// Show the framebuffer in the window, creating the window on first use.
void sdl_host_present(struct surface *fb, const char *title);
void sdl_host_snapshot_tick(void);  // write a held-back CHIRON_SNAPSHOT frame
void sdl_host_save_frame(const char *path);  // a script's "snap FILE"
void sdl_host_wake(void);  // wake a blocked GetMessage from another thread
void sdl_host_warp_mouse(int x, int y);  // framebuffer coordinates
void sdl_host_set_cursor(void *sdl_cursor, int visible);
