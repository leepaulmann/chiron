// USER32: rectangles, system metrics, cursors, message boxes, keyboard maps.
#include <SDL.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../loader/modules.h"
#include "../loader/thunks.h"
#include "gdi.h"
#include "user32.h"

// ---- Rectangles -----------------------------------------------------------

BOOL WINAPI SetRect(RECT *r, int l, int t, int rt, int b) {
    *r = (RECT){l, t, rt, b};
    return TRUE;
}

BOOL WINAPI IsRectEmpty(const RECT *r) { return r->left >= r->right || r->top >= r->bottom; }

BOOL WINAPI EqualRect(const RECT *a, const RECT *b) {
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

BOOL WINAPI PtInRect(const RECT *r, POINT p) {
    return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}

BOOL WINAPI IntersectRect(RECT *out, const RECT *a, const RECT *b) {
    RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
              a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
    if (IsRectEmpty(&r)) {
        *out = (RECT){0, 0, 0, 0};
        return FALSE;
    }
    *out = r;
    return TRUE;
}

BOOL WINAPI UnionRect(RECT *out, const RECT *a, const RECT *b) {
    if (IsRectEmpty(a)) *out = *b;
    else if (IsRectEmpty(b)) *out = *a;
    else
        *out = (RECT){a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
                      a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom};
    return !IsRectEmpty(out);
}

int WINAPI FillRect(HDC hdc, const RECT *r, HGDIOBJ brush) { return gdi_fill_rect(hdc, r, brush); }

// ---- System metrics -------------------------------------------------------

int WINAPI GetSystemMetrics(int index) {
    switch (index) {
    case 0: return display_width;   // SM_CXSCREEN
    case 1: return display_height;  // SM_CYSCREEN
    case 2: case 3: return 16;      // SM_CXVSCROLL, SM_CYHSCROLL
    case 4: return 19;              // SM_CYCAPTION
    case 5: case 6: return 1;       // SM_CXBORDER, SM_CYBORDER
    case 7: case 8: return 3;       // SM_CXDLGFRAME, SM_CYDLGFRAME
    case 11: case 12: return 32;    // SM_CXICON, SM_CYICON
    case 13: case 14: return 32;    // SM_CXCURSOR, SM_CYCURSOR
    case 15: return 19;             // SM_CYMENU
    case 16: return display_width;  // SM_CXFULLSCREEN
    case 17: return display_height; // SM_CYFULLSCREEN
    case 19: return 1;              // SM_MOUSEPRESENT
    case 23: return 0;              // SM_SWAPBUTTON
    case 32: case 33: return 4;     // SM_CXFRAME, SM_CYFRAME
    case 36: case 37: return 4;     // SM_CXDOUBLECLK, SM_CYDOUBLECLK
    case 43: return 3;              // SM_CMOUSEBUTTONS
    case 75: return 1;              // SM_MOUSEWHEELPRESENT
    case 78: return display_width;  // SM_CXVIRTUALSCREEN
    case 79: return display_height; // SM_CYVIRTUALSCREEN
    case 80: return 1;              // SM_CMONITORS
    }
    fprintf(stderr, "chiron: GetSystemMetrics(%d) unknown, returning 0\n", index);
    return 0;
}

// ---- Cursors and icons ----------------------------------------------------

#define CURSOR_MAGIC 0x52535543u  // "CURS"

struct cursor {
    uint32_t magic;
    int system;  // SDL_SystemCursor + 1, or 0 for a bitmap cursor
    uint32_t *argb;
    int w, h, hot_x, hot_y;
    SDL_Cursor *sdl;
};

static struct cursor *current_cursor;
static int cursor_count;  // ShowCursor display counter

static struct cursor *system_cursor(int id) {
    static struct cursor cache[SDL_NUM_SYSTEM_CURSORS];
    int sys;
    switch (id) {
    case 32513: sys = SDL_SYSTEM_CURSOR_IBEAM; break;
    case 32514: sys = SDL_SYSTEM_CURSOR_WAIT; break;
    case 32515: sys = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case 32648: sys = SDL_SYSTEM_CURSOR_NO; break;
    case 32649: sys = SDL_SYSTEM_CURSOR_HAND; break;
    case 32650: sys = SDL_SYSTEM_CURSOR_WAITARROW; break;
    default: sys = SDL_SYSTEM_CURSOR_ARROW;
    }
    struct cursor *c = &cache[sys];
    c->magic = CURSOR_MAGIC;
    c->system = sys + 1;
    return c;
}

void *WINAPI LoadCursorA(void *instance, const char *name) {
    if (!instance && (uintptr_t)name <= 0xffff) return system_cursor((int)(uintptr_t)name);
    // Cursors stored in the game's resources are not loaded yet.
    if ((uintptr_t)name <= 0xffff) fprintf(stderr, "chiron: LoadCursorA(#%u) from module, using arrow\n", (unsigned)(uintptr_t)name);
    else fprintf(stderr, "chiron: LoadCursorA(%s) from module, using arrow\n", name);
    return system_cursor(32512);
}

void *WINAPI LoadIconA(void *instance, const char *name) {
    static int icon;  // icons are never drawn; any unique non-NULL value works
    return &icon;
}

typedef struct {
    BOOL is_icon;
    DWORD hot_x, hot_y;
    HGDIOBJ mask, color;
} ICONINFO;

void *WINAPI CreateIconIndirect(const ICONINFO *info) {
    struct cursor *c = calloc(1, sizeof(*c));
    c->magic = CURSOR_MAGIC;
    c->hot_x = info->hot_x;
    c->hot_y = info->hot_y;
    if (gdi_bitmap_to_argb(info->color, info->mask, &c->argb, &c->w, &c->h)) {
        free(c);
        return NULL;
    }
    return c;
}

BOOL WINAPI DestroyCursor(void *h) {
    struct cursor *c = h;
    if (!c || c->magic != CURSOR_MAGIC || c->system) return TRUE;
    if (current_cursor == c) user32_apply_cursor(system_cursor(32512));
    if (c->sdl) SDL_FreeCursor(c->sdl);
    free(c->argb);
    c->magic = 0;
    free(c);
    return TRUE;
}

// Make `h` the visible cursor. NULL re-applies the current one.
void user32_apply_cursor(void *h) {
    struct cursor *c = h ? h : current_cursor;
    if (!c || c->magic != CURSOR_MAGIC) {
        sdl_host_set_cursor(NULL, 0);
        return;
    }
    if (!c->sdl) {
        if (c->system) {
            c->sdl = SDL_CreateSystemCursor(c->system - 1);
        } else {
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(c->argb, c->w, c->h, 32, c->w * 4,
                                                                SDL_PIXELFORMAT_ARGB8888);
            c->sdl = SDL_CreateColorCursor(s, c->hot_x, c->hot_y);
            SDL_FreeSurface(s);
        }
    }
    current_cursor = c;
    sdl_host_set_cursor(c->sdl, cursor_count >= 0);
}

void *WINAPI SetCursor(void *h) {
    struct cursor *prev = current_cursor;
    if (!h) {
        current_cursor = NULL;
        sdl_host_set_cursor(NULL, 0);
    } else {
        user32_apply_cursor(h);
    }
    return prev;
}

int WINAPI ShowCursor(BOOL show) {
    cursor_count += show ? 1 : -1;
    if (current_cursor) sdl_host_set_cursor(current_cursor->sdl, cursor_count >= 0);
    return cursor_count;
}

// ---- Message boxes --------------------------------------------------------

int WINAPI MessageBoxA(HWND hwnd, const char *text, const char *caption, UINT type) {
    fprintf(stderr, "chiron: MessageBox \"%s\": %s\n", caption ? caption : "", text ? text : "");
    thunk_trace_recent();
    modules_scan_stack((uint32_t)__builtin_frame_address(0));
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, caption ? caption : "", text ? text : "", NULL);
    // Answer OK/Yes to whatever was asked.
    switch (type & 0xf) {
    case 3: case 4: return 6;  // MB_YESNO(CANCEL) -> IDYES
    default: return 1;         // IDOK
    }
}

BOOL WINAPI MessageBeep(UINT type) { return TRUE; }

// ---- Keyboard layout ------------------------------------------------------
// Scan codes for a US keyboard (set 1), indexed by virtual key.

static const uint8_t vk_scan[256] = {
    [0x08] = 0x0e, [0x09] = 0x0f, [0x0d] = 0x1c, [0x10] = 0x2a, [0x11] = 0x1d, [0x12] = 0x38,
    [0x13] = 0x45, [0x14] = 0x3a, [0x1b] = 0x01, [0x20] = 0x39, [0x21] = 0x49, [0x22] = 0x51,
    [0x23] = 0x4f, [0x24] = 0x47, [0x25] = 0x4b, [0x26] = 0x48, [0x27] = 0x4d, [0x28] = 0x50,
    [0x2d] = 0x52, [0x2e] = 0x53, ['0'] = 0x0b, ['1'] = 0x02, ['2'] = 0x03, ['3'] = 0x04,
    ['4'] = 0x05, ['5'] = 0x06, ['6'] = 0x07, ['7'] = 0x08, ['8'] = 0x09, ['9'] = 0x0a,
    ['A'] = 0x1e, ['B'] = 0x30, ['C'] = 0x2e, ['D'] = 0x20, ['E'] = 0x12, ['F'] = 0x21,
    ['G'] = 0x22, ['H'] = 0x23, ['I'] = 0x17, ['J'] = 0x24, ['K'] = 0x25, ['L'] = 0x26,
    ['M'] = 0x32, ['N'] = 0x31, ['O'] = 0x18, ['P'] = 0x19, ['Q'] = 0x10, ['R'] = 0x13,
    ['S'] = 0x1f, ['T'] = 0x14, ['U'] = 0x16, ['V'] = 0x2f, ['W'] = 0x11, ['X'] = 0x2d,
    ['Y'] = 0x15, ['Z'] = 0x2c, [0x60] = 0x52, [0x61] = 0x4f, [0x62] = 0x50, [0x63] = 0x51,
    [0x64] = 0x4b, [0x65] = 0x4c, [0x66] = 0x4d, [0x67] = 0x47, [0x68] = 0x48, [0x69] = 0x49,
    [0x6a] = 0x37, [0x6b] = 0x4e, [0x6d] = 0x4a, [0x6e] = 0x53, [0x6f] = 0x35, [0x70] = 0x3b,
    [0x71] = 0x3c, [0x72] = 0x3d, [0x73] = 0x3e, [0x74] = 0x3f, [0x75] = 0x40, [0x76] = 0x41,
    [0x77] = 0x42, [0x78] = 0x43, [0x79] = 0x44, [0x7a] = 0x57, [0x7b] = 0x58, [0x90] = 0x45,
    [0x91] = 0x46, [0xba] = 0x27, [0xbb] = 0x0d, [0xbc] = 0x33, [0xbd] = 0x0c, [0xbe] = 0x34,
    [0xbf] = 0x35, [0xc0] = 0x29, [0xdb] = 0x1a, [0xdc] = 0x2b, [0xdd] = 0x1b, [0xde] = 0x28,
};

WORD user32_vk_to_scan(WORD vk) { return vk_scan[vk & 0xff]; }

UINT WINAPI MapVirtualKeyA(UINT code, UINT type) {
    switch (type) {
    case 0:  // MAPVK_VK_TO_VSC
        return vk_scan[code & 0xff];
    case 1:  // MAPVK_VSC_TO_VK
        for (int vk = 1; vk < 256; vk++)
            if (vk_scan[vk] == code) return vk;
        return 0;
    case 2:  // MAPVK_VK_TO_CHAR
        if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == ' ') return code;
        return 0;
    }
    return 0;
}

// CharUpperA/CharLowerA take either a string or a single character in the
// low word of the pointer.
char *WINAPI CharUpperA(char *s) {
    if ((uintptr_t)s <= 0xffff) return (char *)(uintptr_t)toupper((int)(uintptr_t)s);
    for (char *p = s; *p; p++) *p = toupper((unsigned char)*p);
    return s;
}

char *WINAPI CharLowerA(char *s) {
    if ((uintptr_t)s <= 0xffff) return (char *)(uintptr_t)tolower((int)(uintptr_t)s);
    for (char *p = s; *p; p++) *p = tolower((unsigned char)*p);
    return s;
}

// ---- Formatting -----------------------------------------------------------

int WINAPIV wsprintfA(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, 1025, fmt, ap);  // Windows limits output to 1024 characters
    va_end(ap);
    return n > 1024 ? 1024 : n;
}
