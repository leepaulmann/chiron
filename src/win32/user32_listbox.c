// USER32: the LISTBOX system class and DlgDirListA.
// The game never shows a list box; it fills a hidden one with DlgDirListA to
// enumerate save folders and files, then reads the entries back with
// LB_GETCOUNT and LB_GETTEXT. So there is no painting here, only the list.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../loader/modules.h"
#include "kernel32.h"
#include "user32.h"

#define LBS_SORT 0x0002u

#define LB_ERR (-1)
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018a
#define LB_GETCOUNT 0x018b
#define LB_DIR 0x018d
#define LB_FINDSTRING 0x018f
#define LB_FINDSTRINGEXACT 0x01a2

#define DDL_READONLY 0x0001u
#define DDL_HIDDEN 0x0002u
#define DDL_SYSTEM 0x0004u
#define DDL_DIRECTORY 0x0010u
#define DDL_ARCHIVE 0x0020u
#define DDL_DRIVES 0x4000u
#define DDL_EXCLUSIVE 0x8000u

struct listbox {
    char **items;
    int count, cap;
    int cursel;
};

static struct listbox *listbox_of(HWND hwnd, int create) {
    struct listbox **slot = (struct listbox **)user32_window_extra(hwnd);
    if (!slot) return NULL;
    if (!*slot && create) {
        *slot = calloc(1, sizeof(**slot));
        (*slot)->cursel = LB_ERR;
    }
    return *slot;
}

static void lb_clear(struct listbox *lb) {
    for (int i = 0; i < lb->count; i++) free(lb->items[i]);
    lb->count = 0;
    lb->cursel = LB_ERR;
}

static int lb_insert(struct listbox *lb, int index, const char *text) {
    if (lb->count == lb->cap) {
        lb->cap = lb->cap ? lb->cap * 2 : 16;
        lb->items = realloc(lb->items, lb->cap * sizeof(*lb->items));
    }
    if (index < 0 || index > lb->count) index = lb->count;
    memmove(lb->items + index + 1, lb->items + index, (lb->count - index) * sizeof(*lb->items));
    lb->items[index] = strdup(text);
    lb->count++;
    return index;
}

static int lb_sorted_index(struct listbox *lb, const char *text) {
    int i = 0;
    while (i < lb->count && strcasecmp(lb->items[i], text) <= 0) i++;
    return i;
}

static int lb_find(struct listbox *lb, int after, const char *text, int exact) {
    size_t n = strlen(text);
    for (int k = 1; k <= lb->count; k++) {
        int i = (after + k) % lb->count;
        if (exact ? !strcasecmp(lb->items[i], text) : !strncasecmp(lb->items[i], text, n)) return i;
    }
    return LB_ERR;
}

// LB_DIR: add the entries matching spec. Directories appear as "[name]",
// as Windows shows them; "." is skipped and ".." kept.
static int lb_dir(HWND hwnd, struct listbox *lb, UINT type, const char *spec) {
    struct win32_find_data fd;
    HANDLE h = FindFirstFileA(spec && *spec ? spec : "*.*", &fd);
    int added = LB_ERR;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            int is_dir = (fd.attrs & 0x10) != 0;
            if (is_dir) {
                if (!(type & DDL_DIRECTORY) || !strcmp(fd.name, ".")) continue;
            } else if ((type & DDL_EXCLUSIVE) && !(type & (DDL_ARCHIVE | DDL_READONLY | DDL_HIDDEN | DDL_SYSTEM))) {
                // Exclusive listings include files only when a file type
                // bit asks for them; any such bit matches our plain files.
                continue;
            }
            char text[264];
            snprintf(text, sizeof(text), is_dir ? "[%s]" : "%s", fd.name);
            int at = (user32_window_style(hwnd) & LBS_SORT) ? lb_sorted_index(lb, text) : lb->count;
            added = lb_insert(lb, at, text);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (type & DDL_DRIVES) {
        int at = (user32_window_style(hwnd) & LBS_SORT) ? lb_sorted_index(lb, "[-c-]") : lb->count;
        added = lb_insert(lb, at, "[-c-]");
    }
    return added;
}

static LRESULT WINAPI listbox_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    struct listbox *lb = listbox_of(hwnd, 1);
    if (!lb) return DefWindowProcA(hwnd, msg, wp, lp);
    const char *text = (const char *)lp;
    int i = (int)wp;
    switch (msg) {
    case LB_ADDSTRING:
        return lb_insert(lb, (user32_window_style(hwnd) & LBS_SORT) ? lb_sorted_index(lb, text) : lb->count, text);
    case LB_INSERTSTRING:
        return lb_insert(lb, i, text);
    case LB_DELETESTRING:
        if (i < 0 || i >= lb->count) return LB_ERR;
        free(lb->items[i]);
        memmove(lb->items + i, lb->items + i + 1, (lb->count - i - 1) * sizeof(*lb->items));
        lb->count--;
        if (lb->cursel >= lb->count) lb->cursel = LB_ERR;
        return lb->count;
    case LB_RESETCONTENT:
        lb_clear(lb);
        return 0;
    case LB_SETCURSEL:
        lb->cursel = (i >= 0 && i < lb->count) ? i : LB_ERR;
        return i == -1 ? 0 : lb->cursel;
    case LB_GETCURSEL:
        return lb->cursel;
    case LB_GETTEXT:
        if (i < 0 || i >= lb->count) return LB_ERR;
        strcpy((char *)lp, lb->items[i]);
        return strlen(lb->items[i]);
    case LB_GETTEXTLEN:
        return i >= 0 && i < lb->count ? (LRESULT)strlen(lb->items[i]) : LB_ERR;
    case LB_GETCOUNT:
        return lb->count;
    case LB_DIR:
        return lb_dir(hwnd, lb, (UINT)wp, text);
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
        return lb->count ? lb_find(lb, i, text, msg == LB_FINDSTRINGEXACT) : LB_ERR;
    case 0x0082:  // WM_NCDESTROY
        lb_clear(lb);
        free(lb->items);
        free(lb);
        *(struct listbox **)user32_window_extra(hwnd) = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void *user32_listbox_proc(void) { return (void *)listbox_proc; }

// DlgDirListA fills the list box with the given id from a path such as
// "saves\*.sav". Any directory part becomes the current directory, and the
// file part is written back into path, as Windows does.
int WINAPI DlgDirListA(HWND dlg, char *path, int list_id, int static_id, UINT type) {
    char spec[260] = "*.*";
    if (modules_trace) fprintf(stderr, "[user32] DlgDirListA \"%s\" type=0x%x\n", path ? path : "", type);
    if (path && *path) {
        char *sep = NULL;
        for (char *p = path; *p; p++)
            if (*p == '\\' || *p == '/' || *p == ':') sep = p;
        int wild = strpbrk(sep ? sep + 1 : path, "*?") != NULL;
        if (sep || !wild) {
            // "dir\spec" or a bare directory name.
            char dir[260];
            if (wild) snprintf(dir, sizeof(dir), "%.*s", (int)(sep - path + 1), path);
            else snprintf(dir, sizeof(dir), "%s", path);
            if (!SetCurrentDirectoryA(dir)) {
                win32_set_last_error(3);  // ERROR_PATH_NOT_FOUND
                return FALSE;
            }
            if (wild) snprintf(spec, sizeof(spec), "%s", sep + 1);
        } else {
            snprintf(spec, sizeof(spec), "%s", path);
        }
        strcpy(path, spec);
    }
    if (list_id) {
        HWND lb = user32_child_by_id(dlg, list_id);
        if (!lb) return FALSE;
        user32_send(lb, LB_RESETCONTENT, 0, 0);
        user32_send(lb, LB_DIR, type, (LPARAM)spec);
    }
    (void)static_id;  // the game passes none
    return TRUE;
}
