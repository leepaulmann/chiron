// USER32: window classes, windows, the message queue, timers and painting.
// All windows belong to the game thread, which is also the thread that owns
// SDL. Other threads may only post messages.
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../loader/modules.h"
#include "../loader/teb.h"
#include "gdi.h"
#include "user32.h"

typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

int display_width = 1024, display_height = 768, display_bpp = 32;

// ---- Window classes -------------------------------------------------------

struct wndclass {
    char name[64];
    UINT style;
    WNDPROC proc;
    int cls_extra, wnd_extra;
    void *instance, *icon, *cursor, *background;
    struct wndclass *next;
};

static struct wndclass *classes;

typedef struct {
    UINT style;
    WNDPROC proc;
    int cls_extra, wnd_extra;
    void *instance, *icon, *cursor, *background;
    const char *menu, *class_name;
} WNDCLASSA;

static struct wndclass *find_class(const char *name) {
    for (struct wndclass *c = classes; c; c = c->next)
        if ((uintptr_t)name > 0xffff && !strcasecmp(c->name, name)) return c;
    return NULL;
}

WORD WINAPI RegisterClassA(const WNDCLASSA *wc) {
    if ((uintptr_t)wc->class_name <= 0xffff || find_class(wc->class_name)) {
        win32_set_last_error(1410);  // ERROR_CLASS_ALREADY_EXISTS
        return 0;
    }
    struct wndclass *c = calloc(1, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", wc->class_name);
    c->style = wc->style;
    c->proc = wc->proc;
    c->cls_extra = wc->cls_extra;
    c->wnd_extra = wc->wnd_extra;
    c->instance = wc->instance;
    c->icon = wc->icon;
    c->cursor = wc->cursor;
    c->background = wc->background;
    c->next = classes;
    classes = c;
    static WORD atom = 0xc000;
    return atom++;
}

BOOL WINAPI UnregisterClassA(const char *name, void *instance) {
    for (struct wndclass **pp = &classes; *pp; pp = &(*pp)->next)
        if ((uintptr_t)name > 0xffff && !strcasecmp((*pp)->name, name)) {
            struct wndclass *c = *pp;
            *pp = c->next;
            free(c);
            return TRUE;
        }
    return FALSE;
}

// ---- Windows --------------------------------------------------------------

#define WINDOW_MAGIC 0x574e4448u  // "HDNW"

struct window {
    uint32_t magic;
    struct wndclass *cls;
    WNDPROC proc;
    struct window *parent, *children, *sibling;
    DWORD style, exstyle;
    LONG id, userdata;
    void *instance;
    RECT rect;  // relative to the parent's client area (screen for top level)
    int visible;
    char title[128];
    RECT invalid;
    int has_invalid, erase;
    uint8_t *extra;
    DWORD tid;          // owning thread
    struct surface fb;  // top-level windows only
};

static struct window *toplevels;
static struct window *focus, *active;

#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define CW_USEDEFAULT ((int)0x80000000)

static int is_window(struct window *w) {
    return w && ((uintptr_t)w & 3) == 0 && (uintptr_t)w > 0x10000 && w->magic == WINDOW_MAGIC;
}

static struct window *toplevel_of(struct window *w) {
    while (w && w->parent) w = w->parent;
    return w;
}

// Offset of a window's client area within its top-level framebuffer.
static void client_origin(struct window *w, LONG *x, LONG *y) {
    *x = *y = 0;
    for (; w && w->parent; w = w->parent) {
        *x += w->rect.left;
        *y += w->rect.top;
    }
}

static void alloc_framebuffer(struct window *w) {
    int width = w->rect.right - w->rect.left, height = w->rect.bottom - w->rect.top;
    if (width <= 0 || height <= 0 || (w->fb.width == width && w->fb.height == height)) return;
    free(w->fb.bits);
    w->fb = (struct surface){.width = width, .height = height, .bpp = 32, .stride = width * 4,
                             .masks = {0xff0000, 0xff00, 0xff}};
    w->fb.bits = calloc((size_t)width * height, 4);
}

LRESULT user32_send(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    struct window *w = hwnd;
    if (!is_window(w) || !w->proc) return 0;
    return w->proc(hwnd, msg, wp, lp);
}

typedef struct {
    void *create_params, *instance, *menu;
    HWND parent;
    int cy, cx, y, x;
    LONG style;
    const char *name, *class_name;
    DWORD exstyle;
} CREATESTRUCTA;

HWND WINAPI CreateWindowExA(DWORD exstyle, const char *class_name, const char *title, DWORD style,
                            int x, int y, int width, int height, HWND parent, void *menu,
                            void *instance, void *param) {
    struct wndclass *cls = find_class(class_name);
    if (!cls) {
        fprintf(stderr, "chiron: CreateWindowExA: unknown class %s\n",
                (uintptr_t)class_name > 0xffff ? class_name : "(atom)");
        win32_set_last_error(1407);  // ERROR_CANNOT_FIND_WND_CLASS
        return NULL;
    }
    struct window *w = calloc(1, sizeof(*w));
    w->magic = WINDOW_MAGIC;
    w->tid = teb_current()->tid;
    w->cls = cls;
    w->proc = cls->proc;
    w->style = style & ~WS_VISIBLE;
    w->exstyle = exstyle;
    w->instance = instance;
    w->id = (style & WS_CHILD) ? (LONG)(uintptr_t)menu : 0;
    w->extra = calloc(1, cls->wnd_extra + 4);
    snprintf(w->title, sizeof(w->title), "%s", title ? title : "");
    if (x == CW_USEDEFAULT) x = y = 0;
    if (width == CW_USEDEFAULT) width = display_width, height = display_height;
    w->rect = (RECT){x, y, x + width, y + height};

    if (modules_trace)
        fprintf(stderr, "[user32] CreateWindowExA class=%s title=\"%s\" style=0x%08x ex=0x%08x %d,%d %dx%d parent=%p\n",
                cls->name, w->title, style, exstyle, x, y, width, height, parent);
    struct window *p = is_window(parent) ? parent : NULL;
    if (p && (style & WS_CHILD)) {
        w->parent = p;
        w->sibling = p->children;
        p->children = w;
    } else {
        // Top-level windows (owned or not) get their own framebuffer.
        w->sibling = toplevels;
        toplevels = w;
        alloc_framebuffer(w);
    }

    CREATESTRUCTA cs = {param, instance, menu, parent, height, width, y, x, style, title, class_name, exstyle};
    if (!user32_send(w, 0x0081 /* WM_NCCREATE */, 0, (LPARAM)&cs)) {
        // A window procedure that does not handle WM_NCCREATE via
        // DefWindowProc returns 0 and aborts creation on Windows; we are lenient.
    }
    if (user32_send(w, 0x0001 /* WM_CREATE */, 0, (LPARAM)&cs) == -1) {
        DestroyWindow(w);
        return NULL;
    }
    user32_send(w, 0x0005 /* WM_SIZE */, 0, (LPARAM)((height << 16) | (width & 0xffff)));
    if (style & WS_VISIBLE) ShowWindow(w, 1);
    return w;
}

static void unlink_window(struct window *w) {
    struct window **head = w->parent ? &w->parent->children : &toplevels;
    for (struct window **pp = head; *pp; pp = &(*pp)->sibling)
        if (*pp == w) {
            *pp = w->sibling;
            break;
        }
}

BOOL WINAPI DestroyWindow(HWND hwnd) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    while (w->children) DestroyWindow(w->children);
    user32_send(w, 0x0002 /* WM_DESTROY */, 0, 0);
    user32_send(w, 0x0082 /* WM_NCDESTROY */, 0, 0);
    unlink_window(w);
    user32_timers_destroyed(w);
    if (focus == w) focus = NULL;
    if (active == w) active = NULL;
    w->magic = 0;
    free(w->fb.bits);
    free(w->extra);
    free(w);
    return TRUE;
}

BOOL WINAPI IsWindow(HWND hwnd) { return is_window(hwnd); }

BOOL WINAPI ShowWindow(HWND hwnd, int cmd) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    int was = w->visible;
    w->visible = cmd != 0;  // SW_HIDE = 0
    if (modules_trace) fprintf(stderr, "[user32] ShowWindow %s cmd=%d\n", w->cls->name, cmd);
    if (w->visible != was) user32_send(w, 0x0018 /* WM_SHOWWINDOW */, w->visible, 0);
    if (w->visible && !w->parent) {
        if (!active) {
            active = w;
            user32_send(w, 0x001c /* WM_ACTIVATEAPP */, 1, 0);
            user32_send(w, 0x0006 /* WM_ACTIVATE */, 1 /* WA_ACTIVE */, 0);
        }
        if (!focus) {
            focus = w;
            user32_send(w, 0x0007 /* WM_SETFOCUS */, 0, 0);
        }
        InvalidateRect(w, NULL, TRUE);
        user32_screen_dirty();
    }
    return was;
}

BOOL WINAPI MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    w->rect = (RECT){x, y, x + width, y + height};
    if (!w->parent) alloc_framebuffer(w);
    user32_send(w, 0x0003 /* WM_MOVE */, 0, (LPARAM)((y << 16) | (x & 0xffff)));
    user32_send(w, 0x0005 /* WM_SIZE */, 0, (LPARAM)((height << 16) | (width & 0xffff)));
    if (repaint) InvalidateRect(w, NULL, TRUE);
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hwnd, RECT *r) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    LONG ox, oy;
    client_origin(w->parent, &ox, &oy);
    *r = (RECT){w->rect.left + ox, w->rect.top + oy, w->rect.right + ox, w->rect.bottom + oy};
    return TRUE;
}

BOOL WINAPI GetClientRect(HWND hwnd, RECT *r) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    *r = (RECT){0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top};
    return TRUE;
}

BOOL WINAPI ClientToScreen(HWND hwnd, POINT *p) {
    LONG ox, oy;
    if (!is_window(hwnd)) return FALSE;
    client_origin(hwnd, &ox, &oy);
    struct window *top = toplevel_of(hwnd);
    p->x += ox + top->rect.left;
    p->y += oy + top->rect.top;
    return TRUE;
}

BOOL WINAPI ScreenToClient(HWND hwnd, POINT *p) {
    LONG ox, oy;
    if (!is_window(hwnd)) return FALSE;
    client_origin(hwnd, &ox, &oy);
    struct window *top = toplevel_of(hwnd);
    p->x -= ox + top->rect.left;
    p->y -= oy + top->rect.top;
    return TRUE;
}

HWND WINAPI GetParent(HWND hwnd) {
    struct window *w = hwnd;
    return is_window(w) ? w->parent : NULL;
}

HWND WINAPI SetParent(HWND hwnd, HWND parent) {
    struct window *w = hwnd, *p = parent;
    if (!is_window(w)) return NULL;
    struct window *old = w->parent;
    unlink_window(w);
    w->parent = is_window(p) ? p : NULL;
    struct window **head = w->parent ? &w->parent->children : &toplevels;
    w->sibling = *head;
    *head = w;
    if (!w->parent) alloc_framebuffer(w);
    return old;
}

BOOL WINAPI IsChild(HWND parent, HWND hwnd) {
    for (struct window *w = hwnd; is_window(w); w = w->parent)
        if (w->parent == parent) return TRUE;
    return FALSE;
}

BOOL WINAPI IsZoomed(HWND hwnd) { return FALSE; }
HWND WINAPI GetFocus(void) { return focus; }

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)

LONG WINAPI GetWindowLongA(HWND hwnd, int index) {
    struct window *w = hwnd;
    if (!is_window(w)) return 0;
    switch (index) {
    case GWL_WNDPROC: return (LONG)(uintptr_t)w->proc;
    case GWL_HINSTANCE: return (LONG)(uintptr_t)w->instance;
    case GWL_ID: return w->id;
    case GWL_STYLE: return w->style | (w->visible ? WS_VISIBLE : 0);
    case GWL_EXSTYLE: return w->exstyle;
    case GWL_USERDATA: return w->userdata;
    }
    if (index >= 0 && index + 4 <= w->cls->wnd_extra) return *(LONG *)(w->extra + index);
    win32_set_last_error(1413);  // ERROR_INVALID_INDEX
    return 0;
}

LONG WINAPI SetWindowLongA(HWND hwnd, int index, LONG value) {
    struct window *w = hwnd;
    if (!is_window(w)) return 0;
    LONG old = GetWindowLongA(hwnd, index);
    switch (index) {
    case GWL_WNDPROC: w->proc = (WNDPROC)(uintptr_t)value; break;
    case GWL_HINSTANCE: w->instance = (void *)(uintptr_t)value; break;
    case GWL_ID: w->id = value; break;
    case GWL_STYLE: w->style = value & ~WS_VISIBLE; break;
    case GWL_EXSTYLE: w->exstyle = value; break;
    case GWL_USERDATA: w->userdata = value; break;
    default:
        if (index >= 0 && index + 4 <= w->cls->wnd_extra) *(LONG *)(w->extra + index) = value;
    }
    return old;
}

LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return proc ? proc(hwnd, msg, wp, lp) : 0;
}

typedef BOOL(WINAPI *WNDENUMPROC)(HWND, LPARAM);

BOOL WINAPI EnumThreadWindows(DWORD tid, WNDENUMPROC proc, LPARAM lp) {
    for (struct window *w = toplevels, *next; w; w = next) {
        next = w->sibling;
        if (!proc(w, lp)) return FALSE;
    }
    return TRUE;
}

// ---- Painting ---------------------------------------------------------------

typedef struct {
    HDC hdc;
    BOOL erase;
    RECT paint;
    BOOL restore, inc_update;
    BYTE reserved[32];
} PAINTSTRUCT;

static void union_into(RECT *dst, const RECT *r) {
    if (r->left < dst->left) dst->left = r->left;
    if (r->top < dst->top) dst->top = r->top;
    if (r->right > dst->right) dst->right = r->right;
    if (r->bottom > dst->bottom) dst->bottom = r->bottom;
}

BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *r, BOOL erase) {
    struct window *w = hwnd;
    if (!hwnd) {
        for (w = toplevels; w; w = w->sibling) InvalidateRect(w, NULL, erase);
        return TRUE;
    }
    if (!is_window(w)) return FALSE;
    RECT all = {0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top};
    if (!r) r = &all;
    if (w->has_invalid) union_into(&w->invalid, r);
    else w->invalid = *r;
    w->has_invalid = 1;
    w->erase |= erase;
    return TRUE;
}

BOOL WINAPI ValidateRect(HWND hwnd, const RECT *r) {
    struct window *w = hwnd;
    if (!is_window(w)) return FALSE;
    w->has_invalid = 0;  // partial validation is treated as full
    return TRUE;
}

HDC WINAPI BeginPaint(HWND hwnd, PAINTSTRUCT *ps) {
    struct window *w = hwnd;
    if (!is_window(w)) return NULL;
    memset(ps, 0, sizeof(*ps));
    ps->hdc = gdi_create_window_dc(hwnd);
    ps->paint = w->has_invalid ? w->invalid : (RECT){0, 0, 0, 0};
    if (w->erase && w->cls->background) {
        RECT all = {0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top};
        gdi_fill_rect(ps->hdc, &all, w->cls->background);
    }
    w->has_invalid = w->erase = 0;
    return ps->hdc;
}

BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps) {
    gdi_release_window_dc(ps->hdc);
    return TRUE;
}

HDC WINAPI GetDC(HWND hwnd) {
    if (hwnd && !is_window(hwnd)) return NULL;
    return gdi_create_window_dc(hwnd);
}

int WINAPI ReleaseDC(HWND hwnd, HDC hdc) {
    gdi_release_window_dc(hdc);
    return 1;
}

struct surface *user32_window_surface(HWND hwnd, RECT *client) {
    struct window *w = hwnd;
    if (!hwnd) {
        // The screen: the main window covers it.
        w = user32_main_window();
        if (!w) return NULL;
        *client = (RECT){0, 0, w->fb.width, w->fb.height};
        return &w->fb;
    }
    if (!is_window(w)) return NULL;
    struct window *top = toplevel_of(w);
    if (!top->fb.bits) return NULL;
    LONG ox, oy;
    client_origin(w, &ox, &oy);
    *client = (RECT){ox, oy, ox + w->rect.right - w->rect.left, oy + w->rect.bottom - w->rect.top};
    return &top->fb;
}

struct window *user32_main_window(void) {
    // The most recently created visible top-level window is the one shown
    // on screen; hidden helper windows are skipped.
    for (struct window *w = toplevels; w; w = w->sibling)
        if (w->visible && w->fb.bits) return w;
    return NULL;
}

// Hit-test a point in main-window coordinates, returning the deepest visible
// child and the point in its client coordinates.
struct window *user32_window_at(int x, int y, POINT *client) {
    struct window *w = user32_main_window();
    if (!w) return NULL;
    for (;;) {
        struct window *hit = NULL;
        for (struct window *c = w->children; c; c = c->sibling)
            if (c->visible && x >= c->rect.left && x < c->rect.right && y >= c->rect.top &&
                y < c->rect.bottom) {
                hit = c;
                break;
            }
        if (!hit) break;
        x -= hit->rect.left;
        y -= hit->rect.top;
        w = hit;
    }
    client->x = x;
    client->y = y;
    return w;
}

// Find a window with pending paint, children first.
static struct window *find_invalid(struct window *list) {
    for (struct window *w = list; w; w = w->sibling) {
        if (!w->visible) continue;
        struct window *c = find_invalid(w->children);
        if (c) return c;
        if (w->has_invalid) return w;
    }
    return NULL;
}

HWND user32_next_paint(void) { return find_invalid(toplevels); }

const char *user32_title(struct window *w) { return w->title; }
struct surface *user32_framebuffer(struct window *w) { return &w->fb; }
UINT user32_class_style(HWND hwnd) {
    struct window *w = hwnd;
    return is_window(w) ? w->cls->style : 0;
}
void *user32_class_cursor(HWND hwnd) {
    struct window *w = hwnd;
    return is_window(w) ? w->cls->cursor : NULL;
}
void user32_set_focus(HWND hwnd) { focus = hwnd; }
DWORD user32_window_tid(HWND hwnd) {
    struct window *w = hwnd;
    return is_window(w) ? w->tid : 0;
}
