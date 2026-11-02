// Internal interface between the USER32 shim files.
#pragma once

#include "gdi.h"

struct window;

LRESULT user32_send(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
struct window *user32_main_window(void);
struct window *user32_window_at(int x, int y, POINT *client);
HWND user32_next_paint(void);
const char *user32_title(struct window *w);
struct surface *user32_framebuffer(struct window *w);
UINT user32_class_style(HWND hwnd);
void *user32_class_cursor(HWND hwnd);
void user32_set_focus(HWND hwnd);
void user32_timers_destroyed(HWND hwnd);
void user32_apply_cursor(void *hcursor);
void *user32_window_extra(HWND hwnd);
DWORD user32_window_style(HWND hwnd);
HWND user32_child_by_id(HWND parent, LONG id);
void *user32_listbox_proc(void);

// Win32 entry points that other USER32 files call directly.
BOOL WINAPI DestroyWindow(HWND hwnd);
BOOL WINAPI ShowWindow(HWND hwnd, int cmd);
BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *r, BOOL erase);
HWND WINAPI GetFocus(void);
LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
