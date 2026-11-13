// Unit test for the Time class, the string list and the string table's lookup (strings.c) with stand-ins for
// the Windows calls and the game's message pump: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/strlist.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))
#define STD __attribute__((stdcall))

// The Windows calls a timer makes, logged: kind, then what it was given.
static struct {
    int kind;
    uint32_t a, b, c, d, e;
} calls[16];
static int ncalls;
enum { K_KILL_MM = 1, K_KILL = 2, K_SET_MM = 3, K_SET = 4, K_FLUSH = 5 };
static void rec(int kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    if (ncalls < 16) calls[ncalls].kind = kind, calls[ncalls].a = a, calls[ncalls].b = b, calls[ncalls].c = c, calls[ncalls].d = d, calls[ncalls].e = e;
    ncalls++;
}
static uint32_t next_id = 77;
static uint32_t STD s_kill_event(uint32_t id) { return rec(K_KILL_MM, id, 0, 0, 0, 0), 0; }
static int STD s_kill_timer(uint32_t hwnd, uint32_t id) { return rec(K_KILL, hwnd, id, 0, 0, 0), 1; }
static uint32_t STD s_set_event(uint32_t delay, uint32_t res, void *proc, void *user, uint32_t flags) {
    return rec(K_SET_MM, delay, res, (uint32_t)(uintptr_t)proc, (uint32_t)(uintptr_t)user, flags), next_id;
}
static uint32_t STD s_set_timer(uint32_t hwnd, uint32_t id, uint32_t ms, void *proc) {
    return rec(K_SET, hwnd, id, ms, (uint32_t)(uintptr_t)proc, 0), next_id;
}
static void __attribute__((cdecl)) s_flush(void) { rec(K_FLUSH, 0, 0, 0, 0, 0); }

static char proc_mm, proc_tm;
static uint32_t hwnd = 0x1234, flush_flag;
static void cb_a(void *x) { (void)x; }
static void cb_b(void *x, int y) { (void)x, (void)y; }

static void expect_call(int i, int kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    char what[40];
    snprintf(what, sizeof what, "call %d", i);
    check_(what, i < ncalls ? calls[i].kind : -1, kind);
    if (i >= ncalls) return;
    check_("  a", calls[i].a, a), check_("  b", calls[i].b, b), check_("  c", calls[i].c, c), check_("  d", calls[i].d, d);
    check_("  e", calls[i].e, e);
}

// A list of nodes with items that hold text.
static struct game_strlist L;
static struct game_strnode nodes[6];
static uint8_t items[6][16];
static char text[6][8];
static void make_list(int n, const char *const *words) {
    memset(&L, 0, sizeof L), memset(nodes, 0, sizeof nodes);
    for (int i = 0; i < n; i++) {
        nodes[i].id = 10 + i, nodes[i].next = &nodes[(i + 1) % n], nodes[i].prev = &nodes[(i + n - 1) % n];
        strcpy(text[i], words ? words[i] : "x");
        char *tp = text[i];
        memcpy(items[i] + 4, &tp, sizeof tp);
        nodes[i].item = items[i];
    }
    L.head = n ? nodes : NULL, L.cur = n ? nodes : NULL, L.count = n;
}

int main(void) {
    game_time_kill_event = s_kill_event, game_kill_timer = s_kill_timer, game_time_set_event = s_set_event;
    game_set_timer = s_set_timer, game_time_flush = s_flush, game_time_flush_flag = &flush_flag;
    game_time_mm_proc = &proc_mm, game_time_timer_proc = &proc_tm, game_win_hwnd = &hwnd;
    struct game_time T;

    // The constructor clears the object (a resolution of 5), and answers it.
    memset(&T, 0xff, sizeof T);
    EXPECT(time_ctor(&T) == &T, 1);
    EXPECT(T.flags, 0), EXPECT(T.id, 0), EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.delay, 0), EXPECT(T.resolution, 5);
    EXPECT(T.fired, 0), EXPECT(T.arg == NULL, 1), EXPECT(T.arg2, 0), EXPECT(T.f24, 0);
    EXPECT(ncalls, 0);

    // Start with a short delay: a multimedia timer, repeating, with the game's procedure and the object; the
    // old timer is stopped (by its kind) and the pump runs first, since the object isn't a pulse.
    T.id = 5, T.delay = 20, T.flags = 2;
    EXPECT(time_start(&T, cb_a, (void *)0x44, 30, 4), 0);
    expect_call(0, K_KILL_MM, 5, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0);
    expect_call(2, K_SET_MM, 30, 4, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1), EXPECT(ncalls, 3);
    EXPECT(T.id, next_id), EXPECT(T.cb == cb_a, 1), EXPECT(T.arg == (void *)0x44, 1), EXPECT(T.delay, 30), EXPECT(T.resolution, 4);
    EXPECT(T.cb2 == NULL, 1), EXPECT(T.flags, 0), EXPECT(T.fired, 0);
    // A long delay: SetTimer, with the window, the object as the id and the game's timer procedure; the old
    // SetTimer timer is stopped with KillTimer. A pulse object (flag 1) doesn't run the pump.
    ncalls = 0, T.flags = 1, T.id = 6;
    EXPECT(time_start(&T, cb_a, NULL, 0x32, 5), 0);
    expect_call(0, K_KILL_MM, 6, 0, 0, 0, 0);  // (the old delay was 30: a multimedia timer)
    ncalls = 0;
    T.id = 6, T.flags = 1;
    EXPECT(time_start(&T, cb_a, NULL, 0x80, 5), 0);
    expect_call(0, K_KILL, 0x1234, 6, 0, 0, 0);
    expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x80, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(ncalls, 2);
    // No function: nothing is started (7), the object still takes the values.
    ncalls = 0, T.id = 0;
    EXPECT(time_start(&T, NULL, (void *)3, 90, 2), 7);
    EXPECT(T.delay, 90), EXPECT(T.arg == (void *)3, 1), EXPECT(ncalls, 1), expect_call(0, K_FLUSH, 0, 0, 0, 0, 0);
    // No timer could be made: 2.
    ncalls = 0, next_id = 0;
    EXPECT(time_start(&T, cb_a, NULL, 10, 2), 2), EXPECT(T.id, 0);
    next_id = 77;
    // Pulse is the same as start.
    ncalls = 0, T.id = 0, T.flags = 0;
    EXPECT(time_pulse(&T, cb_a, NULL, 10, 3), 0);
    expect_call(1, K_SET_MM, 10, 3, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1);

    // The second kind: a function that takes two arguments (the delay is the fourth).
    ncalls = 0, T.id = 0, T.flags = 0;
    EXPECT(time_start_2(&T, cb_b, (void *)8, 9, 60, 6), 0);
    EXPECT(T.cb2 == cb_b, 1), EXPECT(T.cb == NULL, 1), EXPECT(T.arg2, 9), EXPECT(T.arg == (void *)8, 1), EXPECT(T.delay, 60), EXPECT(T.resolution, 6);
    expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 60, (uint32_t)(uintptr_t)&proc_tm, 0);
    ncalls = 0, T.id = 0;
    EXPECT(time_start_2(&T, NULL, NULL, 0, 40, 6), 7);
    // pulse_2: a multimedia timer that doesn't repeat; a SetTimer one marks the object a pulse.
    ncalls = 0, T.id = 0;
    EXPECT(time_pulse_2(&T, cb_b, NULL, 1, 10, 6), 0);
    expect_call(1, K_SET_MM, 10, 6, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 0), EXPECT(T.flags, 0);
    ncalls = 0, T.id = 0;
    EXPECT(time_pulse_2(&T, cb_b, NULL, 1, 300, 6), 0);
    EXPECT(T.flags, 1), expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 300, (uint32_t)(uintptr_t)&proc_tm, 0);

    // Start again with what the object holds (no kill): needs a function; repeating, and it clears flag 1.
    ncalls = 0, T.id = 0, T.flags = 3, T.cb = NULL, T.cb2 = NULL, T.delay = 15, T.resolution = 7;
    EXPECT(time_start_3(&T), 7), EXPECT(ncalls, 0);
    T.cb = cb_a;
    EXPECT(time_start_3(&T), 0), EXPECT(T.flags, 2), expect_call(0, K_SET_MM, 15, 7, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1);
    ncalls = 0, T.delay = 0x40, T.flags = 3;
    EXPECT(time_start_3(&T), 0), expect_call(0, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x40, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(T.flags, 2);
    // pulse_3: once; a SetTimer one marks the object a pulse and clears flag 2.
    ncalls = 0, T.delay = 15, T.flags = 2;
    EXPECT(time_pulse_3(&T), 0), expect_call(0, K_SET_MM, 15, 7, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 0), EXPECT(T.flags, 2);
    ncalls = 0, T.delay = 0x40, T.flags = 2;
    EXPECT(time_pulse_3(&T), 0), expect_call(0, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x40, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(T.flags, 1);
    T.cb = NULL, T.cb2 = NULL, EXPECT(time_pulse_3(&T), 7);

    // Stop: the timer by its kind, then the pump (unless a pulse); the object keeps the rest.
    ncalls = 0, T.id = 9, T.delay = 5, T.flags = 0, T.cb = cb_a;
    time_stop(&T);
    expect_call(0, K_KILL_MM, 9, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0), EXPECT(ncalls, 2), EXPECT(T.id, 0), EXPECT(T.cb == cb_a, 1);
    ncalls = 0, T.id = 9, T.delay = 0x99, T.flags = 1;
    time_stop(&T);
    expect_call(0, K_KILL, 0x1234, 9, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0;
    time_stop(&T);
    EXPECT(ncalls, 0);

    // Init: the functions and the delay, nothing started; it answers the delay (init_2 the argument).
    ncalls = 0, T.id = 0, T.flags = 0, T.fired = 1, T.f24 = 3;
    EXPECT(time_init(&T, cb_a, (void *)5, 0x1f4, 5), 0x1f4);
    EXPECT(T.cb == cb_a, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.arg == (void *)5, 1), EXPECT(T.delay, 0x1f4), EXPECT(T.resolution, 5);
    EXPECT(T.fired, 0), EXPECT(T.f24, 0), EXPECT(T.arg2, 0), expect_call(0, K_FLUSH, 0, 0, 0, 0, 0), EXPECT(ncalls, 1);
    EXPECT(time_init_2(&T, cb_b, (void *)6, 7, 11, 2), 6);
    EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == cb_b, 1), EXPECT(T.arg == (void *)6, 1), EXPECT(T.arg2, 7), EXPECT(T.delay, 11), EXPECT(T.resolution, 2);

    // The destructor and close stop the timer and clear the object.
    ncalls = 0, T.id = 4, T.delay = 3, T.flags = 2;
    time_dtor(&T);
    expect_call(0, K_KILL_MM, 4, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0);
    EXPECT(T.id, 0), EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.arg == NULL, 1), EXPECT(T.delay, 0), EXPECT(T.resolution, 5), EXPECT(T.flags, 0);
    ncalls = 0, T.id = 4, T.delay = 3, T.flags = 1, T.arg2 = 5, T.fired = 1;
    time_close(&T);
    EXPECT(ncalls, 1), EXPECT(T.id, 0), EXPECT(T.arg2, 0), EXPECT(T.fired, 0);

    // The string table's lookup: the base at or below it, the pointer itself up to the end, nothing beyond.
    struct game_strings tab = {0};
    tab.base = 1000, tab.size = 50;
    EXPECT(strings_get(&tab, 900), 1000), EXPECT(strings_get(&tab, 1000), 1000), EXPECT(strings_get(&tab, 1001), 1001);
    EXPECT(strings_get(&tab, 1050), 1050), EXPECT(strings_get(&tab, 1051), 0), EXPECT(strings_get(&tab, 0xfffffff0u), 0);

    // The string list.
    static const char *const w[] = {"pear", "apple", "fig", "apple", "banana"};
    make_list(5, w);
    EXPECT(strlist_seek_id(&L, 12), 1), EXPECT(L.idx, 2), EXPECT(L.cur == &nodes[2], 1);
    EXPECT(strlist_seek_id(&L, 99), 0), EXPECT(L.idx, 5), EXPECT(L.cur == &nodes[0], 1);
    EXPECT(strlist_current_id(&L), 10);
    EXPECT(strlist_seek_pos(&L, 3), 1), EXPECT(L.idx, 3), EXPECT(L.cur == &nodes[3], 1), EXPECT(strlist_current_id(&L), 13);
    EXPECT(strlist_seek_pos(&L, 5), 0), EXPECT(L.idx, 3), EXPECT(L.cur == &nodes[3], 1);
    EXPECT(strlist_seek_pos(&L, -1), 1), EXPECT(L.idx, 4), EXPECT(L.cur == &nodes[4], 1);
    EXPECT(strlist_seek_pos(&L, -5), 1), EXPECT(L.idx, 0), EXPECT(L.cur == &nodes[0], 1);
    L.idx = 2, L.cur = &nodes[2];
    EXPECT(strlist_seek_pos(&L, -6), 0), EXPECT(L.cur == &nodes[0], 1), EXPECT(L.idx, 2);  // (the cursor went to the head)
    EXPECT(strlist_seek_pos(&L, 0), 1), EXPECT(L.idx, 0);
    EXPECT(strlist_current_entry(&L) == items[0], 1);
    EXPECT(strlist_next_entry(&L) == items[1], 1), EXPECT(L.idx, 1);
    L.idx = 3, L.cur = &nodes[3];
    EXPECT(strlist_next_entry(&L) == items[4], 1), EXPECT(L.idx, 4);
    EXPECT(strlist_next_entry(&L) == items[0], 1), EXPECT(L.idx, 0);  // (round the ring)
    make_list(0, NULL);
    EXPECT(strlist_seek_id(&L, 1), 0), EXPECT(strlist_current_id(&L), 0), EXPECT(strlist_next_entry(&L) == NULL, 1);
    EXPECT(strlist_current_entry(&L) == NULL, 1), EXPECT(strlist_seek_pos(&L, 0), 0), EXPECT(strlist_seek_pos(&L, -1), 0);

    // The sort: the ids and items change places so the texts come out in order (equal ones keep their order:
    // the first of the lowest goes first); the cursor is left where the work put it.
    make_list(5, w);
    strlist_sort(&L, 0);
    {
        const char *got[5];
        int ids[5];
        for (int i = 0; i < 5; i++) {
            memcpy(&got[i], (uint8_t *)nodes[i].item + 4, sizeof(char *));
            ids[i] = nodes[i].id;
        }
        EXPECT(strcmp(got[0], "apple"), 0), EXPECT(strcmp(got[1], "apple"), 0), EXPECT(strcmp(got[2], "banana"), 0);
        EXPECT(strcmp(got[3], "fig"), 0), EXPECT(strcmp(got[4], "pear"), 0);
        EXPECT(ids[0], 11), EXPECT(ids[1], 13), EXPECT(ids[2], 14), EXPECT(ids[3], 12), EXPECT(ids[4], 10);
    }
    EXPECT(L.count, 5);
    make_list(1, w), strlist_sort(&L, 0), EXPECT(nodes[0].id, 10), EXPECT(L.cur == &nodes[0], 1);
    make_list(0, NULL), strlist_sort(&L, 0);

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
