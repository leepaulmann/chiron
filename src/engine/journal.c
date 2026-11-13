// The Windows-call journal: shadow checks of functions that call Windows.
//
// A check runs the original, puts game memory back, and runs Chiron's code. A Windows call
// whose effect lies outside game memory (invalidating part of a window, showing one, moving
// the cursor, posting a message, setting a timer) can't be put back, and one that reads
// changing state (the keyboard, the cursor) can answer the two runs differently. So in shadow
// mode the imports listed below go through a wrapper. Outside a check it just calls Windows.
// During a check's first run (the original) it calls Windows and records the call: which
// function, its arguments (and what in-pointers point to), what it wrote through
// out-pointers, and what it returned. During the second run (Chiron's code) it doesn't call
// Windows: it compares each call with the next recorded one and hands back the recorded
// results. The check reports the first call that differs, or a different number of calls.
// The original's calls are the ones that really happened, as with the rest of a check.
//
// Only calls made on the thread running the check are journaled. Calls whose effects a check
// can put back (drawing into a DC, which WRITES_DC and the pixels cover) and calls that make
// objects the run then uses (GetDC) are not journaled: they run in both runs.
#include "journal.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "code.h"
#include "game.h"

#define STD __attribute__((stdcall))

// What an argument is.
enum { V, IN_RECT, OUT_POINT, OUT_RECT, INOUT_POINT };

struct import {
    const char *name;
    uint32_t iat;  // the import table slot
    int nargs;
    uint8_t kind[6];
    void *real;
};

static struct import imports[] = {
    {"ValidateRect", 0x669278, 2, {V, IN_RECT}},
    {"GetCursorPos", 0x669284, 1, {OUT_POINT}},
    {"SetCursor", 0x669288, 1, {V}},
    {"MapVirtualKeyA", 0x669290, 2, {V, V}},
    {"GetWindowRect", 0x6692a8, 2, {V, OUT_RECT}},
    {"ClientToScreen", 0x6692d0, 2, {V, INOUT_POINT}},
    {"ScreenToClient", 0x6692d4, 2, {V, INOUT_POINT}},
    {"ReleaseCapture", 0x6692e0, 0, {0}},
    {"KillTimer", 0x6692f0, 2, {V, V}},
    {"SetTimer", 0x6692f4, 4, {V, V, V, V}},
    {"InvalidateRect", 0x669304, 3, {V, IN_RECT, V}},
    {"SetCursorPos", 0x669310, 2, {V, V}},
    {"PostMessageA", 0x669314, 4, {V, V, V, V}},
    {"ShowWindow", 0x669320, 2, {V, V}},
    {"MoveWindow", 0x669324, 6, {V, V, V, V, V, V}},
    {"GetKeyState", 0x66932c, 1, {V}},
    {"GetAsyncKeyState", 0x669330, 1, {V}},
    {"GetSystemMetrics", 0x669334, 1, {V}},
    {"GetFocus", 0x669348, 0, {0}},
    {"GetWindowLongA", 0x66934c, 2, {V, V}},
};
enum { N_IMPORTS = sizeof imports / sizeof imports[0] };

static uint32_t call_real(const struct import *im, const uint32_t *a) {
    void *f = im->real;
    switch (im->nargs) {
    case 0: return ((uint32_t(STD *)(void))f)();
    case 1: return ((uint32_t(STD *)(uint32_t))f)(a[0]);
    case 2: return ((uint32_t(STD *)(uint32_t, uint32_t))f)(a[0], a[1]);
    case 3: return ((uint32_t(STD *)(uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2]);
    case 4: return ((uint32_t(STD *)(uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3]);
    case 5: return ((uint32_t(STD *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4]);
    default:
        return ((uint32_t(STD *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))f)(a[0], a[1], a[2], a[3], a[4],
                                                                                              a[5]);
    }
}

static size_t data_size(int kind) { return kind == IN_RECT || kind == OUT_RECT ? 16 : kind == V ? 0 : 8; }

// A recorded call: arguments, the data in-pointers pointed to (before) and out-pointers
// were given (after), and the result.
struct entry {
    int fn;
    uint32_t arg[6];
    uint8_t in[6][16], out[6][16];
    uint32_t ret;
};
enum { LOG_MAX = 256 };
static struct entry log_[LOG_MAX];
static int n_log, cursor, overflow;
static enum { OFF, RECORD, REPLAY } mode;
static pthread_t checker;
static char diff[256];

static void describe(char *out, size_t size, int fn, const uint32_t *a) {
    int k = snprintf(out, size, "%s(", imports[fn].name);
    for (int i = 0; i < imports[fn].nargs && k < (int)size; i++)
        k += snprintf(out + k, size - k, "%s%s0x%x", i ? ", " : "", imports[fn].kind[i] == V ? "" : "&", a[i]);
    if (k < (int)size) snprintf(out + k, size - k, ")");
}

static uint32_t journal_call(int fn, uint32_t *a) {
    const struct import *im = &imports[fn];
    if (mode == OFF || !pthread_equal(pthread_self(), checker)) return call_real(im, a);
    if (mode == RECORD) {
        struct entry *e = n_log < LOG_MAX ? &log_[n_log] : NULL;
        if (!e) overflow = 1;
        else {
            memset(e, 0, sizeof *e);
            e->fn = fn;
            for (int i = 0; i < im->nargs; i++) {
                e->arg[i] = a[i];
                int k = im->kind[i];
                if ((k == IN_RECT || k == INOUT_POINT) && a[i]) memcpy(e->in[i], (void *)(uintptr_t)a[i], data_size(k));
            }
        }
        n_log++;
        uint32_t r = call_real(im, a);
        if (e) {
            e->ret = r;
            for (int i = 0; i < im->nargs; i++) {
                int k = im->kind[i];
                if ((k == OUT_POINT || k == OUT_RECT || k == INOUT_POINT) && a[i])
                    memcpy(e->out[i], (void *)(uintptr_t)a[i], data_size(k));
            }
        }
        return r;
    }
    // Replay: the next recorded call must be this one.
    int at = cursor++;
    if (overflow || *diff) return 0;
    if (at >= n_log) {
        char d[120];
        describe(d, sizeof d, fn, a);
        snprintf(diff, sizeof diff, "call %d to Windows: %s, original made only %d", at + 1, d, n_log);
        return 0;
    }
    const struct entry *e = &log_[at];
    int same = e->fn == fn;
    for (int i = 0; same && i < im->nargs; i++) {
        int k = im->kind[i];
        if (k == V) same = e->arg[i] == a[i];
        else if (!e->arg[i] != !a[i]) same = 0;  // pointers: whether given, and what they hold
        else if ((k == IN_RECT || k == INOUT_POINT) && a[i])
            same = !memcmp(e->in[i], (void *)(uintptr_t)a[i], data_size(k));
    }
    if (!same) {
        char d[100], o[100];
        describe(d, sizeof d, fn, a);
        describe(o, sizeof o, e->fn, e->arg);
        snprintf(diff, sizeof diff, "call %d to Windows: %s, original %s", at + 1, d, o);
        return 0;
    }
    for (int i = 0; i < im->nargs; i++) {
        int k = im->kind[i];
        if ((k == OUT_POINT || k == OUT_RECT || k == INOUT_POINT) && a[i])
            memcpy((void *)(uintptr_t)a[i], e->out[i], data_size(k));
    }
    return e->ret;
}

// One wrapper per import, with its own argument count (stdcall: the callee pops them).
#define W0(i) \
    static uint32_t STD wrap_##i(void) { return journal_call(i, (uint32_t[1]){0}); }
#define W1(i) \
    static uint32_t STD wrap_##i(uint32_t a) { return journal_call(i, (uint32_t[]){a}); }
#define W2(i) \
    static uint32_t STD wrap_##i(uint32_t a, uint32_t b) { return journal_call(i, (uint32_t[]){a, b}); }
#define W3(i) \
    static uint32_t STD wrap_##i(uint32_t a, uint32_t b, uint32_t c) { return journal_call(i, (uint32_t[]){a, b, c}); }
#define W4(i)                                                                        \
    static uint32_t STD wrap_##i(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { \
        return journal_call(i, (uint32_t[]){a, b, c, d});                            \
    }
#define W6(i)                                                                                              \
    static uint32_t STD wrap_##i(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { \
        return journal_call(i, (uint32_t[]){a, b, c, d, e, f});                                            \
    }
W2(0) W1(1) W1(2) W2(3) W2(4) W2(5) W2(6) W0(7) W2(8) W4(9) W3(10) W2(11) W4(12) W2(13) W6(14) W1(15) W1(16) W1(17) W0(18)
    W2(19)
static void *const wrappers[N_IMPORTS] = {
    (void *)wrap_0,  (void *)wrap_1,  (void *)wrap_2,  (void *)wrap_3,  (void *)wrap_4,  (void *)wrap_5,  (void *)wrap_6,
    (void *)wrap_7,  (void *)wrap_8,  (void *)wrap_9,  (void *)wrap_10, (void *)wrap_11, (void *)wrap_12, (void *)wrap_13,
    (void *)wrap_14, (void *)wrap_15, (void *)wrap_16, (void *)wrap_17, (void *)wrap_18, (void *)wrap_19};
_Static_assert(sizeof wrappers / sizeof wrappers[0] == N_IMPORTS, "a wrapper for each import");

void journal_install(struct pe_image *exe) {
    for (int i = 0; i < N_IMPORTS; i++) {
        void **slot = (void **)code_at(exe, imports[i].iat);
        imports[i].real = *slot;
        *slot = wrappers[i];
    }
}

void journal_start(void) {
    n_log = cursor = overflow = 0;
    *diff = 0;
    checker = pthread_self();
    mode = RECORD;
}

void journal_middle(void) {
    if (mode != RECORD) return;
    cursor = 0;
    mode = REPLAY;
}

const char *journal_end(void) {
    int was = mode;
    mode = OFF;
    if (was != REPLAY || overflow) return NULL;  // too many calls to keep: not compared
    if (*diff) return diff;
    if (cursor != n_log) {
        snprintf(diff, sizeof diff, "made %d calls to Windows, original %d (first missing: %s)", cursor, n_log,
                 imports[log_[cursor].fn].name);
        return diff;
    }
    return NULL;
}

void journal_abandon(void) { mode = OFF; }
