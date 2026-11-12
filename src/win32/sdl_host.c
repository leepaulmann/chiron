// The one real window: an SDL window showing the game's main framebuffer,
// scaled to whatever size the player gives it.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdi.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int tex_w, tex_h;

// How the frame is stretched to the window, from CHIRON_SCALE:
//   sharp (default): blown up by the largest whole number that fits, with
//     hard pixel edges, then smoothly stretched the rest of the way. Pixels
//     stay crisp without some rows and columns coming out wider than others.
//   nearest: hard edges all the way (uneven pixel sizes at fractional scales).
//   smooth: one smooth stretch (soft at large scales).
enum { SCALE_SHARP, SCALE_NEAREST, SCALE_SMOOTH };
static int scale_mode;
static SDL_Texture *scaled;  // sharp: the frame at a whole-number scale
static int scaled_k;
static Uint32 wake_event;

void sdl_host_init(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *mode = getenv("CHIRON_SCALE");
    if (mode && !strcmp(mode, "nearest")) scale_mode = SCALE_NEAREST;
    else if (mode && !strcmp(mode, "smooth")) scale_mode = SCALE_SMOOTH;
    else if (mode && *mode && strcmp(mode, "sharp"))
        fprintf(stderr, "chiron: CHIRON_SCALE=%s: expected sharp, nearest or smooth; using sharp\n", mode);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  // let SIGINT/SIGTERM end the process
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "chiron: SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    wake_event = SDL_RegisterEvents(1);
    SDL_StartTextInput();
}

void sdl_host_wake(void) {
    if (!wake_event) return;
    SDL_Event e = {.type = wake_event};
    SDL_PushEvent(&e);
}

// CHIRON_SNAPSHOT=file.bmp keeps the file showing the latest presented
// frame, for debugging without a person watching. Writes are limited to four
// a second; a frame that arrives sooner is kept and written later by
// sdl_host_snapshot_tick, so the file never stays behind the screen.
static const char *snapshot;
static SDL_Surface *snap;
static int snap_pending;
static Uint32 last_snapshot;

static int keep_frames = -1;

static void snapshot_frame(const struct surface *fb) {
    if (!snapshot) snapshot = getenv("CHIRON_SNAPSHOT") ? getenv("CHIRON_SNAPSHOT") : "";
    // A script's "snap" steps save the frame on screen, so it is kept too.
    if (keep_frames < 0) keep_frames = *snapshot || (getenv("CHIRON_SCRIPT") && strstr(getenv("CHIRON_SCRIPT"), "snap"));
    if (!keep_frames) return;
    if (!snap || snap->w != fb->width || snap->h != fb->height) {
        SDL_FreeSurface(snap);
        snap = SDL_CreateRGBSurfaceWithFormat(0, fb->width, fb->height, 32, SDL_PIXELFORMAT_XRGB8888);
    }
    for (int y = 0; y < fb->height; y++)
        memcpy((Uint8 *)snap->pixels + y * snap->pitch, fb->bits + y * fb->stride, fb->width * 4);
    if (*snapshot) {
        snap_pending = 1;
        sdl_host_snapshot_tick();
    }
}

// Save the frame on screen now (a script's "snap FILE" step).
void sdl_host_save_frame(const char *path) {
    if (!snap || SDL_SaveBMP(snap, path)) fprintf(stderr, "chiron: snap %s: no frame\n", path);
}

void sdl_host_snapshot_tick(void) {
    if (!snap_pending || (last_snapshot && SDL_GetTicks() - last_snapshot < 250)) return;
    last_snapshot = SDL_GetTicks();
    snap_pending = 0;
    SDL_SaveBMP(snap, snapshot);
}

// For sharp scaling, draw the frame into `scaled` at the largest whole-number
// scale that fits the window's real pixels (which differ from its size on a
// high-DPI screen), with hard edges; the caller then stretches `scaled` the
// rest of the way smoothly. Returns 0 to draw the frame directly instead: at
// 1x, or if the renderer has no target textures.
static int sharp_scale(void) {
    if (scale_mode != SCALE_SHARP) return 0;
    int ow, oh;
    if (SDL_GetRendererOutputSize(renderer, &ow, &oh)) return 0;
    int k = ow / tex_w < oh / tex_h ? ow / tex_w : oh / tex_h;
    if (k <= 1) {
        // No whole-number step fits; just stretch smoothly.
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
        return 0;
    }
    if (!scaled || scaled_k != k) {
        if (scaled) SDL_DestroyTexture(scaled);
        scaled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, tex_w * k,
                                   tex_h * k);
        scaled_k = k;
        if (!scaled) {
            scale_mode = SCALE_NEAREST;  // stay with the plain copy from now on
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
            return 0;
        }
        SDL_SetTextureScaleMode(scaled, SDL_ScaleModeLinear);
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    SDL_SetRenderTarget(renderer, scaled);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_SetRenderTarget(renderer, NULL);
    return 1;
}

void sdl_host_present(struct surface *fb, const char *title) {
    sdl_host_init();
    if (!window) {
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, fb->width,
                                  fb->height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) {
            fprintf(stderr, "chiron: SDL_CreateWindow: %s\n", SDL_GetError());
            exit(1);
        }
        // No vsync: presenting happens on the game's thread, and a hidden or
        // occluded window can make a vsynced present block for a long time,
        // slowing the whole game. user32 already limits presents to the
        // display rate.
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!texture || tex_w != fb->width || tex_h != fb->height) {
        if (texture) SDL_DestroyTexture(texture);
        if (scaled) SDL_DestroyTexture(scaled);
        scaled = NULL;
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    fb->width, fb->height);
        tex_w = fb->width;
        tex_h = fb->height;
        // Letterbox and scale; SDL also maps mouse events back to these
        // logical coordinates.
        SDL_RenderSetLogicalSize(renderer, fb->width, fb->height);
        SDL_SetTextureScaleMode(texture, scale_mode == SCALE_SMOOTH ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    snapshot_frame(fb);
    SDL_UpdateTexture(texture, NULL, fb->bits, fb->stride);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, sharp_scale() ? scaled : texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void sdl_host_warp_mouse(int x, int y) {
    if (!window) return;
    int wx, wy;
    SDL_RenderLogicalToWindow(renderer, x, y, &wx, &wy);
    SDL_WarpMouseInWindow(window, wx, wy);
}

void sdl_host_set_cursor(void *sdl_cursor, int visible) {
    if (sdl_cursor) SDL_SetCursor(sdl_cursor);
    SDL_ShowCursor(visible && sdl_cursor ? SDL_ENABLE : SDL_DISABLE);
}
