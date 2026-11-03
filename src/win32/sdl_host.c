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
static Uint32 wake_event;

void sdl_host_init(void) {
    static int done;
    if (done) return;
    done = 1;
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

static void snapshot_frame(const struct surface *fb) {
    if (!snapshot) snapshot = getenv("CHIRON_SNAPSHOT") ? getenv("CHIRON_SNAPSHOT") : "";
    if (!*snapshot) return;
    if (!snap || snap->w != fb->width || snap->h != fb->height) {
        SDL_FreeSurface(snap);
        snap = SDL_CreateRGBSurfaceWithFormat(0, fb->width, fb->height, 32, SDL_PIXELFORMAT_XRGB8888);
    }
    for (int y = 0; y < fb->height; y++)
        memcpy((Uint8 *)snap->pixels + y * snap->pitch, fb->bits + y * fb->stride, fb->width * 4);
    snap_pending = 1;
    sdl_host_snapshot_tick();
}

void sdl_host_snapshot_tick(void) {
    if (!snap_pending || (last_snapshot && SDL_GetTicks() - last_snapshot < 250)) return;
    last_snapshot = SDL_GetTicks();
    snap_pending = 0;
    SDL_SaveBMP(snap, snapshot);
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
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!texture || tex_w != fb->width || tex_h != fb->height) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    fb->width, fb->height);
        tex_w = fb->width;
        tex_h = fb->height;
        // Letterbox and scale; SDL also maps mouse events back to these
        // logical coordinates.
        SDL_RenderSetLogicalSize(renderer, fb->width, fb->height);
    }
    snapshot_frame(fb);
    SDL_UpdateTexture(texture, NULL, fb->bits, fb->stride);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
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
