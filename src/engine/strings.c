// The game's Time class (a timer an object owns), its string list class (OpenSMACX's StringStruct,
// the ring of nodes the Dialog classes keep their items in; StringList is a derived class that holds
// text), and the string table's lookup.
//
// A Time object keeps the id of the timer it started: a multimedia timer (timeSetEvent) for a delay
// shorter than 0x32 ms, or else a SetTimer timer with the object itself as the id. Both call the game's
// own procedures, which post message 0x401 to the window; the window's handler (the original's) calls the
// function or functions the object holds. Every function that changes the timer first stops the old one
// (timeKillEvent or KillTimer) and, unless the object is a pulse made with SetTimer (flag 1), runs the
// game's own message pump for its 0x401 messages (0x5fd370).
//
// Here are all of Time's functions that start, stop and set up the timer and the procedures the timers
// call (TimerProc and MultimediaProc: they post the message), StringStruct's seek and walk functions,
// StringList's sort and the string table's lookup. StringStruct's constructor, destructor, close, add and
// remove_all and StringList's fill_entry, kill_entry and add_special, and Strings' constructor, init,
// shutdown, put and destructor allocate or free on the game's heap and are checked on twins (life.c);
// StringList's load reads a file and is left to the original.
//
// Differences from OpenSMACX and Thinker: start and pulse are the same code in the original (both start a
// repeating multimedia timer); start_2 and pulse_2 differ in whether the multimedia timer repeats and in
// the flag the SetTimer one sets. Init returns its third argument and init_2 its second, as the original's
// register happens to hold them. The string table's lookup answers the base for anything at or below it and
// nothing for anything beyond its end, and is not the offset lookup the name suggests.
//
// Checks. The Windows calls a timer makes go through the journal (timeKillEvent, timeSetEvent, KillTimer,
// SetTimer; the original's run starts the real timer and Chiron's is compared with it), so every Time
// function is checked as it is called, on the object, and the flag the message pump sets. A sweep starts
// real timers on scratch objects, since the original does, and stops them again by the ids from the
// host (hooks_timer_mark) and by KillTimer on the objects. The string list's functions are checked as
// they are called on the cursor; the sort moves items between any number of nodes, so the game's calls of
// it run the original and the sweep checks it on scratch lists.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "life.h"
#include "shadow.h"
#include "strlist.h"

#define STRINGS (&strings_module)
extern struct hook_module strings_module;
extern struct hook_module edit_module;

#define P(p) ((int)(uintptr_t)(p))

typedef void *voidp;
#define SHADOW_RESULT_voidp(v) void *v =
#define SHADOW_INT_voidp(v) ((int)(uintptr_t)(v))
#define SHADOW_RETURN_voidp(v) return v
#define SHADOW_TAIL_voidp(call) return call

// ---- Strings --------------------------------------------------------------------------------------

// The base for anything at or below it; the pointer itself if it is no further than the end of what the
// table holds; and nothing otherwise.
unsigned STR_THIS strings_get(struct game_strings *t, unsigned p) {
    unsigned base = t->base;
    if (p <= base) return base;
    return base + t->size >= p ? p : 0;
}

// ---- Time -------------------------------------------------------------------------------------------

enum { TIME_MM_LIMIT = 0x32 };  // below this many ms, a multimedia timer; otherwise SetTimer

// What each function does first: stop the timer if there is one (a multimedia timer or SetTimer's, by the
// delay), and unless the object is a SetTimer pulse, take the game's own 0x401 messages.
static void time_kill(struct game_time *t) {
    int flush = !(t->flags & 1);
    if (t->id) {
        if (t->delay < TIME_MM_LIMIT) game_time_kill_event(t->id);
        else game_kill_timer(*game_win_hwnd, t->id);
        t->id = 0;
    }
    if (flush) game_time_flush();
}

void *STR_THIS time_ctor(struct game_time *t) {
    t->id = 0, t->cb = NULL, t->cb2 = NULL, t->arg2 = 0, t->arg = NULL, t->delay = 0, t->fired = 0;
    t->resolution = 5, t->f24 = 0, t->flags = 0;
    return t;
}

// The two that clear the object (dtor and close are the same code).
static void time_clear(struct game_time *t) {
    time_kill(t);
    t->cb = NULL, t->flags = 0, t->cb2 = NULL, t->arg2 = 0, t->arg = NULL, t->delay = 0;
    t->resolution = 5, t->fired = 0, t->f24 = 0;
}
void STR_THIS time_dtor(struct game_time *t) { time_clear(t); }
void STR_THIS time_close(struct game_time *t) { time_clear(t); }

// The object takes the functions and the delay, and nothing is started. Answers the argument it was given
// to call them with (init), or the delay (init_2): what the original's register held.
int STR_THIS time_init(struct game_time *t, void *cb, void *arg, uint32_t delay, int res) {
    time_kill(t);
    t->cb = cb, t->flags = 0, t->fired = 0, t->f24 = 0, t->cb2 = NULL, t->arg2 = 0;
    t->arg = arg, t->delay = delay, t->resolution = res;
    return (int)delay;
}
int STR_THIS time_init_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res) {
    time_kill(t);
    t->cb2 = cb2, t->arg2 = arg2, t->arg = arg, t->flags = 0, t->fired = 0, t->f24 = 0, t->cb = NULL;
    t->delay = delay, t->resolution = res;
    return (int)(uintptr_t)arg;
}

// Start the timer from the object's own delay: a multimedia timer for a short delay, SetTimer's for a longer
// one. `periodic` is what the multimedia timer is told (repeating or once), and `pulse` makes a SetTimer timer
// a pulse (flag 1, flag 2 cleared); answers 0, or 2 if no timer was made.
static int time_arm(struct game_time *t, int periodic, int pulse) {
    uint32_t id;
    uint32_t delay = t->delay;
    if (delay < TIME_MM_LIMIT) {
        id = game_time_set_event(delay, (uint32_t)t->resolution, game_time_mm_proc, t, periodic);
    } else {
        if (pulse) t->flags = (t->flags & ~2u) | 1;
        id = game_set_timer(*game_win_hwnd, (uint32_t)(uintptr_t)t, delay, game_time_timer_proc);
    }
    t->id = id;
    return id ? 0 : 2;
}

// The functions that set the object up as they start it: start and pulse (the same code) take one function,
// the _2 ones another with a second argument. Nothing is started if there is no function (7).
int STR_THIS time_start(struct game_time *t, void *cb, void *arg, uint32_t delay, int res) {
    time_kill(t);
    t->arg = arg, t->flags = 0, t->fired = 0, t->f24 = 0, t->cb = cb, t->cb2 = NULL, t->arg2 = 0;
    t->delay = delay, t->resolution = res;
    if (!cb) return 7;
    t->flags = 0;
    return time_arm(t, 1, 0);
}
int STR_THIS time_pulse(struct game_time *t, void *cb, void *arg, uint32_t delay, int res) {
    return time_start(t, cb, arg, delay, res);
}
int STR_THIS time_start_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res) {
    time_kill(t);
    t->arg2 = arg2, t->arg = arg, t->flags = 0, t->fired = 0, t->f24 = 0, t->cb = NULL, t->cb2 = cb2;
    t->delay = delay, t->resolution = res;
    if (!cb2) return 7;
    t->flags = 0;
    return time_arm(t, 1, 0);
}
int STR_THIS time_pulse_2(struct game_time *t, void *cb2, void *arg, int arg2, uint32_t delay, int res) {
    time_kill(t);
    t->arg2 = arg2, t->arg = arg, t->flags = 0, t->fired = 0, t->f24 = 0, t->cb = NULL, t->cb2 = cb2;
    t->delay = delay, t->resolution = res;
    if (!cb2) return 7;
    t->flags = 0;
    return time_arm(t, 0, 1);
}

// Start again with what the object holds; the old timer isn't stopped. Repeating (start_3, which clears
// flag 1) or once (pulse_3, a SetTimer one marked a pulse).
int STR_THIS time_start_3(struct game_time *t) {
    if (!t->cb && !t->cb2) return 7;
    t->flags &= ~1u;
    return time_arm(t, 1, 0);
}
int STR_THIS time_pulse_3(struct game_time *t) {
    if (!t->cb && !t->cb2) return 7;
    return time_arm(t, 0, 1);
}

void STR_THIS time_stop(struct game_time *t) { time_kill(t); }

// ---- The string list ---------------------------------------------------------------------------------------

// The id of the first node that has it, walking from the head (the cursor ends on that node, or back at the
// head if none has it); 1 if found.
int STR_THIS strlist_seek_id(struct game_strlist *l, int id) {
    if (!l->head) return 0;
    int n = l->count;
    l->idx = 0;
    l->cur = l->head;
    for (int i = 0; i < n; i++) {
        if (l->cur->id == id) return 1;
        l->idx++;
        l->cur = l->cur->next;
    }
    return 0;
}

// The cursor to a position: from the head forward, or for a negative one backwards from the end. 0 if it is
// beyond the last position or (negative) further back than the list is long; the cursor is at the head then
// in the second case.
int STR_THIS strlist_seek_pos(struct game_strlist *l, int pos) {
    if (pos > l->count - 1) return 0;
    l->cur = l->head;
    if (pos < 0) {
        int n = (int)(0u - (unsigned)pos);  // (abs; the most negative number stays negative)
        if (n > l->count) return 0;
        for (; n > 0; n--) l->cur = l->cur->prev;
        l->idx = pos + l->count;
        return 1;
    }
    for (int n = pos; n > 0; n--) l->cur = l->cur->next;
    l->idx = pos;
    return 1;
}

int STR_THIS strlist_current_id(struct game_strlist *l) { return l->head ? l->cur->id : 0; }

// The cursor moves on (round the ring) and answers the item it is at.
void *STR_THIS strlist_next_entry(struct game_strlist *l) {
    if (!l->head) return NULL;
    l->cur = l->cur->next;
    if (++l->idx == l->count) l->idx = 0;
    return l->cur->item;
}
void *STR_THIS strlist_current_entry(struct game_strlist *l) { return l->head ? l->cur->item : NULL; }

// The text of the node the cursor is at (the item's string is at 0x4 of the item).
static const char *cur_text(struct game_strlist *l) {
    if (!l->cur) return NULL;
    return *(const char **)((uint8_t *)l->cur->item + 4);
}

// Selection sort by text: for each position, the first item that sorts lowest of the rest is swapped into
// it (the ids and the items change places, not the nodes). The cursor is left wherever the work last put
// it. The original walks with the cursor throughout, so this does, to leave the same one behind.
void STR_THIS strlist_sort(struct game_strlist *l, int unused) {
    (void)unused;
    int i = 0;
    if (l->count - 1 <= 0) return;
    do {
        l->cur = l->head;
        for (int n = i; n > 0; n--) l->cur = l->cur->next;
        l->idx = i;
        const char *min = l->head ? cur_text(l) : NULL;
        int minpos = i;
        int count = l->count;
        for (int j = i + 1; j < count; j++) {
            if (l->head) {
                l->cur = l->cur->next;
                if (++l->idx == l->count) l->idx = 0;
            }
            const char *text = l->cur ? cur_text(l) : NULL;
            if (strcmp(text, min) < 0) {
                min = l->head ? cur_text(l) : NULL;
                minpos = l->idx;
            }
            count = l->count;
        }
        if (minpos != i && minpos <= l->count - 1) {
            l->cur = l->head;
            for (int n = minpos; n > 0; n--) l->cur = l->cur->next;
            l->idx = minpos;
            struct game_strnode *a = l->cur;
            if (strlist_seek_pos(l, i)) {
                struct game_strnode *b = l->cur;
                int id = a->id;
                a->id = b->id, b->id = id;
                void *item = a->item;
                a->item = b->item, b->item = item;
            }
        }
        i++;
    } while (i < l->count - 1);
}

// The procedures a timer calls, on the timer's own thread (the multimedia one) or in the window's handler (SetTimer's): the first
// takes the Time object it was started for as its third argument (the id of a SetTimer timer is the object; the user value of
// a multimedia one is too). Unless the object's message is already on its way (its flag at 0x1c), the game's window is posted
// message 0x401 with the object, and the flag is set (the handler clears it as it calls what the object holds). A word of the
// game's (0x9b90d0), when set, lets only that object post. The two differ in how many arguments Windows gives them (4 and 5,
// the callee pops them), and answer nothing.
static void time_post(struct game_time *t) {
    if (!t) return;
    const struct game_time *only = *(struct game_time **)game_at(0x9b90d0);
    if (only && t != only) return;
    if (t->fired) return;
    game_post_message((void *)(uintptr_t)*game_win_hwnd, 0x401, (uintptr_t)t, 0);
    t->fired = 1;
}
void __attribute__((stdcall)) time_timer_proc(void *hwnd, unsigned msg, struct game_time *t, unsigned tick) {
    (void)hwnd, (void)msg, (void)tick;
    time_post(t);
}
void __attribute__((stdcall)) time_mm_proc(unsigned id, unsigned msg, struct game_time *t, unsigned dw1, unsigned dw2) {
    (void)id, (void)msg, (void)dw1, (void)dw2;
    time_post(t);
}

// ---- Shadow checks -------------------------------------------------------------------------------------------

SHADOW_THIS_FN(STRINGS, unsigned, strings_get, (struct game_strings * t, unsigned p), (t, p), "%d, %d", P(t), (int)p)

#define TIME_WRITES (WRITES(t, sizeof *t), WRITES(game_time_flush_flag, 4))
SHADOW_WRITER_T(STRINGS, voidp, time_ctor, (struct game_time * t), (t), WRITES(t, sizeof *t), "%d", P(t))
SHADOW_WRITER_T(STRINGS, void, time_dtor, (struct game_time * t), (t), TIME_WRITES, "%d", P(t))
SHADOW_WRITER_T(STRINGS, void, time_close, (struct game_time * t), (t), TIME_WRITES, "%d", P(t))
SHADOW_WRITER_T(STRINGS, int, time_init, (struct game_time * t, void *cb, void *arg, uint32_t delay, int res), (t, cb, arg, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d", P(t), P(cb), P(arg), (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_init_2,
                (struct game_time * t, void *cb2, void *arg, int arg2, uint32_t delay, int res), (t, cb2, arg, arg2, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d, %d", P(t), P(cb2), P(arg), arg2, (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_start, (struct game_time * t, void *cb, void *arg, uint32_t delay, int res), (t, cb, arg, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d", P(t), P(cb), P(arg), (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_pulse, (struct game_time * t, void *cb, void *arg, uint32_t delay, int res), (t, cb, arg, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d", P(t), P(cb), P(arg), (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_start_2,
                (struct game_time * t, void *cb2, void *arg, int arg2, uint32_t delay, int res), (t, cb2, arg, arg2, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d, %d", P(t), P(cb2), P(arg), arg2, (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_pulse_2,
                (struct game_time * t, void *cb2, void *arg, int arg2, uint32_t delay, int res), (t, cb2, arg, arg2, delay, res),
                TIME_WRITES, "%d, %d, %d, %d, %d, %d", P(t), P(cb2), P(arg), arg2, (int)delay, res)
SHADOW_WRITER_T(STRINGS, int, time_start_3, (struct game_time * t), (t), TIME_WRITES, "%d", P(t))
SHADOW_WRITER_T(STRINGS, int, time_pulse_3, (struct game_time * t), (t), TIME_WRITES, "%d", P(t))
SHADOW_WRITER_T(STRINGS, void, time_stop, (struct game_time * t), (t), TIME_WRITES, "%d", P(t))

// The timers' procedures run on whatever thread the timer fires on, where a check can't hold the others still, and a multimedia
// timer's thread would count calls and tick the sweep on its own: the game's calls run the original (not counted), and the sweep
// checks Chiron's code (the journal compares the PostMessage the original's run makes with Chiron's). Normal mode runs Chiron's.
#define STD __attribute__((stdcall))
static void(STD *orig_time_timer_proc)(void *hwnd, unsigned msg, struct game_time *t, unsigned tick);
static struct hook_stats stats_time_timer_proc = {"time_timer_proc"};
SHADOW_WRITER_BODY(void, time_timer_proc, (void *hwnd, unsigned msg, struct game_time *t, unsigned tick), (hwnd, msg, t, tick),
                   (t ? WRITES(&t->fired, 4) : (void)0), "%d, %d", P(t), (int)msg)
static void STD shadow_time_timer_proc(void *hwnd, unsigned msg, struct game_time *t, unsigned tick) {
    orig_time_timer_proc(hwnd, msg, t, tick);
}
static void(STD *orig_time_mm_proc)(unsigned id, unsigned msg, struct game_time *t, unsigned dw1, unsigned dw2);
static struct hook_stats stats_time_mm_proc = {"time_mm_proc"};
SHADOW_WRITER_BODY(void, time_mm_proc, (unsigned id, unsigned msg, struct game_time *t, unsigned dw1, unsigned dw2),
                   (id, msg, t, dw1, dw2), (t ? WRITES(&t->fired, 4) : (void)0), "%d, %d", P(t), (int)msg)
static void STD shadow_time_mm_proc(unsigned id, unsigned msg, struct game_time *t, unsigned dw1, unsigned dw2) {
    orig_time_mm_proc(id, msg, t, dw1, dw2);
}

// The list's cursor is `cur`, `count` and `idx` (which lie together: 12 bytes).
#define LIST_CURSOR WRITES(&l->cur, 12)
SHADOW_WRITER_T(STRINGS, int, strlist_seek_id, (struct game_strlist * l, int id), (l, id), LIST_CURSOR, "%d, %d", P(l), id)
SHADOW_WRITER_T(STRINGS, int, strlist_seek_pos, (struct game_strlist * l, int pos), (l, pos), LIST_CURSOR, "%d, %d", P(l), pos)
SHADOW_THIS_FN(STRINGS, int, strlist_current_id, (struct game_strlist * l), (l), "%d", P(l))
SHADOW_WRITER_T(STRINGS, voidp, strlist_next_entry, (struct game_strlist * l), (l), LIST_CURSOR, "%d", P(l))
SHADOW_THIS_FN(STRINGS, voidp, strlist_current_entry, (struct game_strlist * l), (l), "%d", P(l))

// The sort changes the items of as many nodes as the list has, which a check can't name (twelve spans
// at most), so the game's calls run the original, are counted (to start the sweep), and the sweep checks it
// on scratch lists.
#define SORT_NODES 8
static struct scratch_list {
    struct game_strlist list;
    struct game_strnode nodes[SORT_NODES];
    uint8_t items[SORT_NODES][16];
    char text[SORT_NODES][8];
} L;
static void writes_list(struct hook_writes *w) { hooks_writes_add(w, &L, sizeof L); }

SHADOW_DECLARE_T(void, strlist_sort, (struct game_strlist * l, int unused))
SHADOW_WRITER_BODY(void, strlist_sort, (struct game_strlist * l, int unused), (l, unused), writes_list(&w_), "%d, %d", P(l), unused)
static void THISCALL shadow_strlist_sort(struct game_strlist *l, int unused) {
    if (!hooks_nested && !shadow_bypass()) {
        stats_strlist_sort.calls++;
        shadow_tick(STRINGS);
    }
    orig_strlist_sort(l, unused);
}

// ---- Sweep ---------------------------------------------------------------------------------------------------------

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// The functions a scratch Time object holds: they do nothing, so a message a timer posts that the window
// handles later (the original's timers are real) finds nothing to do.
static void stub_cb(void *arg) { (void)arg; }
static void stub_cb2(void *arg, int arg2) { (void)arg, (void)arg2; }

enum { NT = 3 };
static struct game_time T[NT];

// A timer id the sweep's objects can hold without it being one of the game's: timeKillEvent and KillTimer
// are really called with whatever a scratch object holds.
static uint32_t fake_id(void) { return 0x13370000u + (uint32_t)rnd_to(4000); }
static uint32_t any_delay(void) {
    static const uint32_t d[] = {0, 1, 2, 3, 0x31, 0x32, 0x33, 100, 500, 1000};
    return d[rnd_to(10)];  // (a multimedia timer is joined when stopped: the short ones keep that quick, and 0x31 is one case)
}
// Every multimedia timer the original's run starts is a thread with a TEB of its own (an LDT entry, which the loader
// doesn't give back), so a sweep starts only a few: a delay below the limit for an arming call is kept for some of
// them, and made a longer one (SetTimer's) for the rest.
static int mm_budget;
static uint32_t cap_delay(uint32_t d) {
    if (d >= TIME_MM_LIMIT) return d;
    if (mm_budget > 0 && rnd_to(6) == 0) return mm_budget--, d;
    return TIME_MM_LIMIT + d;
}

static void randomize_time(struct game_time *t) {
    t->flags = (uint32_t)rnd_to(4), t->id = rnd_to(3) ? 0 : fake_id();
    t->cb = rnd_to(2) ? stub_cb : NULL, t->cb2 = rnd_to(2) ? stub_cb2 : NULL;
    t->arg2 = (int)rnd(), t->arg = (void *)(uintptr_t)rnd();
    t->delay = any_delay(), t->fired = rnd_to(2), t->resolution = rnd_to(20), t->f24 = (int)rnd();
}

// The original's run of a check starts real timers on the scratch object (SetTimer's, keyed by the object,
// and multimedia ones, by the ids from the host); stop them again.
static void stop_timers(unsigned mark) {
    if (hooks_timers_kill_from) hooks_timers_kill_from(mark);
    for (int i = 0; i < NT; i++) game_kill_timer(*game_win_hwnd, (uint32_t)(uintptr_t)&T[i]);
}
#define TIMED(call)                                          \
    do {                                                     \
        unsigned mark_ = hooks_timer_mark ? hooks_timer_mark() : 0; \
        call;                                                \
        stop_timers(mark_);                                  \
    } while (0)

static const char *const words[] = {"b", "a", "c", "ab", "B", "abc", "", "zz", "a", "A"};

static void build_list(int n) {
    memset(&L, 0, sizeof L);
    for (int i = 0; i < n; i++) {
        struct game_strnode *nd = &L.nodes[i];
        nd->id = rnd_to(6) - 1;
        nd->next = &L.nodes[(i + 1) % n], nd->prev = &L.nodes[(i + n - 1) % n];
        strcpy(L.text[i], words[rnd_to(10)]);
        char *tp = L.text[i];
        memcpy(L.items[i] + 4, &tp, sizeof tp);
        nd->item = L.items[i];
    }
    L.list.head = n ? &L.nodes[0] : NULL;
    L.list.count = n;
    L.list.cur = n ? &L.nodes[rnd_to(n)] : NULL;
    L.list.idx = n ? rnd_to(n) : 0;
}

static void life_sweep_strings(void);
static void strings_sweep(void) {
    if (!*game_win_hwnd) return;  // (a SetTimer timer needs the game's window: with none the timer gets an id of its own)
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x71f3c5a1u ^ (uint32_t)*game_turn * 2654435761u;
    mm_budget = 2;
    if (!rng_state) rng_state = 1;
    // The string table: bases and sizes, and pointers below, at, in and past it.
    for (int k = 0; k < 40; k++) {
        struct game_strings tab = {0};
        tab.base = rnd_to(4) ? rnd() >> rnd_to(30) : 0xfffffff0u, tab.size = rnd_to(4) ? rnd() >> rnd_to(30) : 0x20;
        unsigned p = tab.base + (unsigned)rnd_to(7) - 3;
        if (rnd_to(3) == 0) p = rnd();
        if (rnd_to(5) == 0) p = tab.base + tab.size + (unsigned)rnd_to(3) - 1;
        check_strings_get(&tab, p);
    }
    for (int round = 0; round < 6; round++) {
        for (int i = 0; i < NT; i++) {
            struct game_time *t = &T[i];
            randomize_time(t);
            void *cb = rnd_to(4) ? (void *)stub_cb : NULL;
            void *cb2 = rnd_to(4) ? (void *)stub_cb2 : NULL;
            uint32_t delay = cap_delay(any_delay());
            int res = rnd_to(20);
            void *arg = (void *)(uintptr_t)rnd();
            int arg2 = (int)rnd();
            randomize_time(t), check_time_ctor(t);
            randomize_time(t); TIMED(check_time_dtor(t));
            randomize_time(t); TIMED(check_time_close(t));
            randomize_time(t); TIMED(check_time_init(t, cb, arg, delay, res));
            randomize_time(t); TIMED(check_time_init_2(t, cb2, arg, arg2, delay, res));
            randomize_time(t); TIMED(check_time_start(t, cb, arg, delay, res));
            randomize_time(t); TIMED(check_time_pulse(t, cb, arg, delay, res));
            randomize_time(t); TIMED(check_time_start_2(t, cb2, arg, arg2, delay, res));
            randomize_time(t); TIMED(check_time_pulse_2(t, cb2, arg, arg2, delay, res));
            randomize_time(t), t->delay = cap_delay(t->delay); TIMED(check_time_start_3(t));
            randomize_time(t), t->delay = cap_delay(t->delay); TIMED(check_time_pulse_3(t));
            randomize_time(t); TIMED(check_time_stop(t));
        }
        // The pulse of a SetTimer one and a repeating one, whatever the object held.
        for (int k = 0; k < 4; k++) {
            struct game_time *t = &T[k % NT];
            randomize_time(t), t->delay = 0x32 + (uint32_t)rnd_to(300), t->cb = stub_cb, t->cb2 = rnd_to(2) ? stub_cb2 : NULL;
            TIMED(check_time_start_3(t));
            randomize_time(t), t->delay = 0x32 + (uint32_t)rnd_to(300), t->cb = rnd_to(2) ? stub_cb : NULL, t->cb2 = stub_cb2;
            TIMED(check_time_pulse_3(t));
            randomize_time(t), t->delay = cap_delay((uint32_t)rnd_to(0x32)), t->cb = stub_cb;
            TIMED(check_time_start_3(t));
            randomize_time(t), t->delay = cap_delay((uint32_t)rnd_to(0x32)), t->cb = stub_cb;
            TIMED(check_time_pulse_3(t));
        }
        // The string list.
        for (int k = 0; k < 4; k++) {
            int n = rnd_to(SORT_NODES + 1);
            build_list(n);
            for (int j = 0; j < 6; j++) {
                check_strlist_seek_id(&L.list, rnd_to(7) - 2);
                check_strlist_seek_pos(&L.list, rnd_to(2 * SORT_NODES + 4) - SORT_NODES - 2);
                check_strlist_current_id(&L.list);
                check_strlist_next_entry(&L.list);
                check_strlist_current_entry(&L.list);
            }
            build_list(n);
            check_strlist_sort(&L.list, rnd_to(3));
        }
        // An empty list with a cursor, and a one-node one.
        build_list(0);
        L.list.cur = NULL;
        check_strlist_seek_id(&L.list, 1), check_strlist_seek_pos(&L.list, 0), check_strlist_seek_pos(&L.list, -1);
        check_strlist_next_entry(&L.list), check_strlist_current_id(&L.list), check_strlist_current_entry(&L.list);
        check_strlist_sort(&L.list, 0);
        build_list(1);
        check_strlist_sort(&L.list, 0), check_strlist_next_entry(&L.list), check_strlist_seek_pos(&L.list, -1);
    }
    // The timers' procedures: the object posting or not (its flag set or clear, the word that lets only one post set to it, to
    // another, or clear), and no object at all. The message the original's run posts is taken by the game's own pump
    // (the objects hold functions that do nothing).
    {
        struct game_time **only = (struct game_time **)game_at(0x9b90d0), *saved_only = *only;
        for (int k = 0; k < 16; k++) {
            struct game_time *t = &T[k % NT];
            randomize_time(t);
            t->cb = stub_cb, t->cb2 = stub_cb2, t->id = 0;
            *only = rnd_to(3) == 0 ? NULL : rnd_to(2) ? t : &T[(k + 1) % NT];
            if (rnd_to(2)) check_time_timer_proc((void *)(uintptr_t)*game_win_hwnd, 0x113, rnd_to(8) ? t : NULL, (unsigned)rnd());
            else check_time_mm_proc((unsigned)rnd(), 0x3b9, rnd_to(8) ? t : NULL, (unsigned)rnd(), (unsigned)rnd());
            game_time_flush();
        }
        *only = saved_only;
    }
    memset(T, 0, sizeof T);  // (a timer message that arrives later finds nothing to do)
    life_sweep_strings();
    hooks_sweeping = sweeping;
}

// ---- Life cycle -----------------------------------------------------------------------------------

// The string list's constructor, close, remove_all and deleting destructor, StringList's fill_entry,
// kill_entry and add_special, and the constructor, init, shutdown and put of Strings (the game's string
// table, built on the arena class that Dialog's data uses: closing it is the game's own function at
// 0x5d4580, allocating from it 0x5d4680, and giving it a block 0x5d4620, all left with the original).
//
// A string list is an object with a virtual base: the first word is the vtable, the second the pointer
// to the table of the virtual base's offset, then the ring's head, cursor, count and index; the virtual
// base (the node and item deleting class) follows at 0x1c and keeps the global list head the object was
// made over (0x9b3374: a constructor that makes the whole object, not a part of a bigger one, saves it
// and clears it, and the deleting destructor puts it back). Its vtables are the game's. The destructors
// walk the ring and delete each item and node through their own vtables; the list's own vtable slot 1
// is told of every item first (StringList's kills its text). They free blocks of the game's heap, so in
// shadow mode the game's calls run the original and the sweep checks Chiron's code on twin lists built
// over nodes and items that are real blocks of the game's heap with a stand-in vtable (life.c). The
// exception frames the constructors set up are not reproduced. Differences from OpenSMACX: it has none
// of these. StringStruct_scalar_dtor frees the object only when its saved global is clear, and always
// clears the global when it is asked to free; the sweep gives it flag 0 (the flag 1 path frees the
// object itself, which the twins, in static memory, cannot take), the unit test the other.

enum { SL_VTABLE = 0x6693a4, SL_VB_VTABLE = 0x6693ac, SL_VBPTR = 0x6693b0, SL_VB_PART = 0x6693a0, SL_HEAD = 0x9b3374 };

static void life_vdelete(void *o) {
    void **vt = *(void ***)o;
    uint8_t *part = (uint8_t *)o + (intptr_t)vt[1];
    ((void(STR_THIS *)(void *, int))(*(void ***)part)[0])(part, 1);
}

// Where the virtual base's vtable is kept: the object's address plus 4 plus the offset the base table
// gives (its second word).
static void list_set_part_vtable(struct game_strlist *l, uint32_t vtable) {
    const int32_t *vbtable = (const int32_t *)(uintptr_t)l->vbptr;
    *(uint32_t *)((uint8_t *)l + 4 + vbtable[1]) = vtable;
}

// Deletes every item and node, as the original's loop does (the count is read again after each).
static void strlist_empty(struct game_strlist *l) {
    int i = 0;
    if (l->count > 0) {
        do {
            struct game_strnode *n = l->head;
            l->cur = n->next;
            void *item = n->item;
            ((void(STR_THIS *)(void *, void *))l->vtable[1])(l, item);
            if (item) life_vdelete(item);
            l->head->item = NULL;
            if (l->head) life_vdelete(l->head);
            l->head = l->cur;
            i++;
        } while (i < l->count);
    }
    l->head = NULL, l->idx = 0, l->count = 0;
}

// `most_derived` is set for an object that is not part of a bigger one: it makes the virtual base too.
void *STR_THIS strlist_ctor(struct game_strlist *l, int most_derived) {
    uint8_t *o = (uint8_t *)l;
    if (most_derived) {
        l->vbptr = (void *)(uintptr_t)game_va(SL_VBPTR);
        *(uint32_t *)(o + 0x1c) = game_va(SL_VB_VTABLE);
        *(uint32_t *)(o + 0x20) = *(uint32_t *)game_at(SL_HEAD);
        *(uint32_t *)game_at(SL_HEAD) = 0;
    }
    l->vtable = (void **)(uintptr_t)game_va(SL_VTABLE);
    list_set_part_vtable(l, game_va(SL_VB_PART));
    l->head = NULL, l->cur = NULL, l->count = 0, l->idx = 0;
    *(uint32_t *)(o + 0x18) = 0;
    return l;
}

// What the virtual base's own function does (`vb` is the base, 0x1c into the list): empty the list.
void STR_THIS strlist_close(void *vb) {
    struct game_strlist *l = (struct game_strlist *)((uint8_t *)vb - 0x1c);
    l->vtable = (void **)(uintptr_t)game_va(SL_VTABLE);
    list_set_part_vtable(l, game_va(SL_VB_PART));
    if (l->head) strlist_empty(l);
    l->idx = 0;
}

void STR_THIS strlist_remove_all(struct game_strlist *l) {
    if (l->head) strlist_empty(l);
}

// The deleting destructor, given the virtual base: empties the list, puts back the global it saved and,
// if asked to (bit 0), frees the object unless it saved a global. Answers the object.
void *STR_THIS strlist_scalar_dtor(void *vb, int flag) {
    struct game_strlist *l = (struct game_strlist *)((uint8_t *)vb - 0x1c);
    uint8_t *o = (uint8_t *)l;
    l->vtable = (void **)(uintptr_t)game_va(SL_VTABLE);
    list_set_part_vtable(l, game_va(SL_VB_PART));
    strlist_remove_all(l);
    l->idx = 0;
    uint32_t saved = *(uint32_t *)(o + 0x20);
    *(uint32_t *)(o + 0x1c) = game_va(SL_VB_VTABLE);
    *(uint32_t *)game_at(SL_HEAD) = saved;
    if ((flag & 1) && l) {
        if (!saved) game_free(l);
        *(uint32_t *)game_at(SL_HEAD) = 0;
    }
    return l;
}

// The ring's nodes and items, made for a list by its "new": from the arena the list part names (word 0x18) if it has one, else
// from the one its virtual base names (the word 8 into it), else from the game's heap. An arena that has no room, in either
// case, is the end of the game (the game's message box, then exit(3)); the heap giving nothing makes no object. A new object
// of the two classes begins with the class's vtable, then (at the virtual base's place) the base's vtable, the arena the object
// was made in (what the global 0x9b3374 holds then; a heap object holds none) and last, where the vtable's second word puts it,
// the part's vtable. The global ends up clear whichever way the object was made.
enum { SL_NODE_VT = 0x6693c8, SL_NODE_PART_VT = 0x6693c4, SL_ITEM_VT = 0x6693bc, SL_ITEM_PART_VT = 0x6693b8, SL_VB_VT = 0x6693ac };
static void *strlist_new(struct game_strlist *l, int size, uint32_t vtable, int vb_at, uint32_t part_vtable) {
    uint8_t *o = (uint8_t *)l;
    void *arena = *(void **)(o + 0x18);
    if (!arena) arena = *(void **)(o + ((const int32_t *)(uintptr_t)l->vbptr)[1] + 8);
    uint8_t *p;
    uint32_t saved = 0;
    if (arena) {
        p = game_string_alloc(arena, size);
        if (!p) {
            game_message_box_a(NULL, (const char *)game_at(0x682724), (const char *)game_at(0x682700), 0);
            game_exit(3);
        }
        saved = (uint32_t)(uintptr_t)arena;
    } else {
        p = game_mem_get((size_t)size);
        if (!p) return NULL;
    }
    *(uint32_t *)game_at(SL_HEAD) = 0;
    *(uint32_t *)p = game_va(vtable);
    *(uint32_t *)(p + vb_at) = game_va(SL_VB_VT);
    *(uint32_t *)(p + vb_at + 4) = saved;
    *(uint32_t *)(p + ((const int32_t *)game_at(vtable))[1]) = game_va(part_vtable);
    return p;
}

// Appends a node with the id and an item (told the list's own entry function, its vtable slot 0: StringList's copies the text and
// the numbers in) at the end of the ring and makes it the cursor; answers 0, or 4 if a node or an item could not be had. A list
// with a count of 0 starts a ring (its cursor is set after the entry function is called); the first node's links are to itself.
// The index becomes the old count, and the count one more. A node that cannot be had is still stored: in an empty list as the head, and
// otherwise as the old last node's next, where it is found missing and the call answers 4 (the ring is left broken, as the original's).
int STR_THIS strlist_add(struct game_strlist *l, int id) {
    struct game_strnode *n;
    if (l->count == 0) {
        n = strlist_new(l, 0x1c, SL_NODE_VT, 0x14, SL_NODE_PART_VT);
        l->head = n;
        if (!n) return 4;
        n->id = id, n->next = n, n->prev = n;
        void *item = strlist_new(l, 0x18, SL_ITEM_VT, 0x10, SL_ITEM_PART_VT);
        l->head->item = item;
        if (!item) return 4;
        ((void(STR_THIS *)(void *, void *))l->vtable[0])(l, item);
        l->cur = l->head;
    } else {
        n = strlist_new(l, 0x1c, SL_NODE_VT, 0x14, SL_NODE_PART_VT);
        struct game_strnode *tail = l->head->prev;
        tail->next = n;
        if (!n) return 4;
        n->prev = tail, n->next = l->head, l->head->prev = n;
        l->cur = n;
        n->id = id;
        void *item = strlist_new(l, 0x18, SL_ITEM_VT, 0x10, SL_ITEM_PART_VT);
        l->cur->item = item;
        if (!item) return 4;
        ((void(STR_THIS *)(void *, void *))l->vtable[0])(l, item);
    }
    l->idx = l->count;
    l->count++;
    return 0;
}

// StringList: the list part is followed by the arena the text comes from (0x18, none for the game's heap),
// the text to add (0x1c) and two numbers (0x20, 0x24) that every new entry takes.
int STR_THIS stringlist_fill_entry(struct game_strlist *l, void *item) {
    uint8_t *o = (uint8_t *)l;
    uint8_t *it = (uint8_t *)item;
    const char *text = *(const char **)(o + 0x1c);
    if (text) {
        void *arena = *(void **)(o + 0x18);
        size_t n = strlen(text) + 1;
        char *block = arena ? game_string_alloc(arena, (int)n) : game_mem_get(n);
        *(char **)(it + 4) = block;
        if (!block) return 4;
        block[0] = 0;
        strcat(block, text);
    }
    *(uint32_t *)(it + 8) = *(uint32_t *)(o + 0x20);
    *(uint32_t *)(it + 0xc) = *(uint32_t *)(o + 0x24);
    return 0;
}

// Gives the entry's text back (not a text from an arena, which is the arena's). What it answers is what the
// register holds: the text when an arena keeps it, else nothing worth reading (the original's is whatever the
// free left; every caller ignores it).
int STR_THIS stringlist_kill_entry(struct game_strlist *l, void *item) {
    char **text = (char **)((uint8_t *)item + 4);
    if (!*text) return 0;
    if (*(void **)((uint8_t *)l + 0x18)) return (int)(intptr_t)*text;
    game_free(*text);
    *text = NULL;
    return 0;
}

// Adds an entry for a text that may begin with up to three '^': one makes the number 1, two 2 (the third
// is skipped if there), and a lone one 3 (a text of just "^").
int STR_THIS stringlist_add_special(struct game_strlist *l, const char *text) {
    uint8_t *o = (uint8_t *)l;
    int kind = 0;
    if (text[0] == '^') {
        if (text[1] == 0) kind = 3, text += 1;
        else if (text[1] != '^') kind = 1, text += 1;
        else kind = 2, text += text[2] == '^' ? 3 : 2;
    }
    *(const char **)(o + 0x1c) = text;
    *(int32_t *)(o + 0x20) = kind;
    *(int32_t *)(o + 0x24) = 0;
    return game_strlist_add(l, 0) != 0;
}

// Strings (the table): the arena's five words then a flag at 0x14 that says the default text was put in.
void *STR_THIS strings_ctor(struct game_strings *t) {
    *(uint8_t *)t = 0;
    t->f8 = 0, t->base = 0, t->f10 = 0, t->size = 0;
    t->f14 = 0;
    return t;
}

int STR_THIS strings_init(struct game_strings *t, int size) {
    if (t->f14) game_dialog_data_close(t), t->f14 = 0;
    if (game_dialog_data_alloc(t, size)) return 1;
    const char *dflt = (const char *)game_at(0x6971fc);
    char *block = game_string_alloc(t, (int)strlen(dflt) + 1);
    strcpy(block, dflt);
    t->f14 = 1;
    return 0;
}

int STR_THIS strings_shutdown(struct game_strings *t) {
    game_dialog_data_close(t);
    t->f14 = 0;
    return 0;
}

// The arena (the first five words of Strings and of the Dialog's data): the block (word 1) is freed and the words cleared.
void STR_THIS arena_close(struct game_strings *a) {
    if (a->base) {
        game_free((void *)(uintptr_t)a->base);
        a->base = 0;
    }
    *(uint8_t *)a = 0;
    a->f8 = 0, a->base = 0, a->f10 = 0, a->size = 0;
}

// Strings' destructor is a jump to the arena's close.
void STR_THIS strings_dtor(struct game_strings *t) { arena_close(t); }

// Copies a text into the arena and answers where it went.
char *STR_THIS strings_put(struct game_strings *t, const char *text) {
    char *block = game_string_alloc(t, (int)strlen(text) + 1);
    strcpy(block, text);
    return block;
}

// ---- Life cycle: shadow mode and the sweep ----

SHADOW_PASS_T(STRINGS, void *, strlist_ctor, (struct game_strlist * l, int most), (l, most))
SHADOW_PASS_T(STRINGS, void, strlist_close, (void *vb), (vb))
SHADOW_PASS_T(STRINGS, void, strlist_remove_all, (struct game_strlist * l), (l))
SHADOW_PASS_T(STRINGS, void *, strlist_scalar_dtor, (void *vb, int flag), (vb, flag))
SHADOW_PASS_T(STRINGS, int, stringlist_fill_entry, (struct game_strlist * l, void *item), (l, item))
SHADOW_PASS_T(STRINGS, int, stringlist_kill_entry, (struct game_strlist * l, void *item), (l, item))
SHADOW_PASS_T(STRINGS, int, stringlist_add_special, (struct game_strlist * l, const char *text), (l, text))
SHADOW_PASS_T(STRINGS, void *, strings_ctor, (struct game_strings * t), (t))
SHADOW_PASS_T(STRINGS, int, strings_init, (struct game_strings * t, int size), (t, size))
SHADOW_PASS_T(STRINGS, int, strings_shutdown, (struct game_strings * t), (t))
SHADOW_PASS_T(STRINGS, char *, strings_put, (struct game_strings * t, const char *text), (t, text))
SHADOW_PASS_T(STRINGS, int, strlist_add, (struct game_strlist * l, int id), (l, id))
SHADOW_PASS_T(STRINGS, void, arena_close, (struct game_strings * a), (a))
// (Strings' destructor is a jump in the original, which a hook's trampoline can't take: the original's run is the original's close.)
static void(THISCALL *orig_strings_dtor)(struct game_strings *t);
static struct hook_stats stats_strings_dtor = {"strings_dtor"};
static void THISCALL shadow_strings_dtor(struct game_strings *t) {
    if (!hooks_nested && !shadow_bypass()) {
        stats_strings_dtor.calls++;
        shadow_tick(STRINGS);
    }
    orig_arena_close(t);
}

static uint32_t put_result;
static const char *put_text = "a line of text";
static uint32_t hash_text(const char *s) {
    uint32_t h = 5381;
    for (; s && *s; s++) h = h * 33 + (uint8_t)*s;
    return h;
}

// What an add made, as one number: the id, whether the ring is linked through the new node, the classes' words of the node and
// its item (the vtables, the arena they were made in as set or not, the entry's numbers and text). It is read after the call,
// so a node or an item that is not as the original's shows in the global the twin check compares.
static uint32_t add_result;
static uint32_t hmix(uint32_t h, uint32_t v) { return (h * 31u + v) * 2654435761u + 7; }
static uint32_t ring_hash(struct game_strlist *l, int ret) {
    uint32_t h = (uint32_t)ret;
    struct game_strnode *c = l->cur;
    if (ret || !c) return h;
    const uint8_t *n = (const uint8_t *)c;
    h = hmix(h, (uint32_t)c->id);
    h = hmix(h, c->next == l->head);
    h = hmix(h, c->prev && c->prev->next == c);
    h = hmix(h, c->next && c->next->prev == c);
    h = hmix(h, *(const uint32_t *)n);
    h = hmix(h, *(const uint32_t *)(n + 0x14));
    h = hmix(h, *(const uint32_t *)(n + 0x18) != 0);
    const uint8_t *it = c->item;
    h = hmix(h, it != NULL);
    if (it) {
        h = hmix(h, *(const uint32_t *)it);
        h = hmix(h, *(const uint32_t *)(it + 0x10));
        h = hmix(h, *(const uint32_t *)(it + 0x14) != 0);
        h = hmix(h, *(const uint32_t *)(it + 8));
        h = hmix(h, *(const uint32_t *)(it + 0xc));
        // (The entry's text is only set when the list has text to add; otherwise the block's word is whatever was there.)
        if (*(const char *const *)((const uint8_t *)l + 0x1c)) {
            const char *text = *(const char *const *)(it + 4);
            h = hmix(h, text != NULL);
            h = hmix(h, hash_text(text));
        }
        h = hmix(h, *(const uint32_t *)(it + ((const int32_t *)game_at(SL_ITEM_VT))[1]));
    }
    h = hmix(h, *(const uint32_t *)(n + ((const int32_t *)game_at(SL_NODE_VT))[1]));
    return hmix(h, *(uint32_t *)game_at(SL_HEAD));
}
LIFE_FN(strlist_ctor_most, orig_strlist_ctor(o, 1), strlist_ctor(o, 1))
LIFE_FN(strlist_ctor_part, orig_strlist_ctor(o, 0), strlist_ctor(o, 0))
LIFE_FN(strlist_close, orig_strlist_close((uint8_t *)o + 0x1c), strlist_close((uint8_t *)o + 0x1c))
LIFE_FN(strlist_remove_all, orig_strlist_remove_all(o), strlist_remove_all(o))
LIFE_FN(strlist_scalar_dtor, orig_strlist_scalar_dtor((uint8_t *)o + 0x1c, 0), strlist_scalar_dtor((uint8_t *)o + 0x1c, 0))
LIFE_FN(strings_ctor, orig_strings_ctor(o), strings_ctor(o))
LIFE_FN(strings_init, orig_strings_init(o, (int)(intptr_t)x), strings_init(o, (int)(intptr_t)x))
LIFE_FN(strings_shutdown, orig_strings_shutdown(o), strings_shutdown(o))
LIFE_FN(arena_close, orig_arena_close(o), arena_close(o))
LIFE_FN(strings_dtor, orig_arena_close(o), strings_dtor(o))
LIFE_FN(strings_put, put_result = hash_text(orig_strings_put(o, put_text)), put_result = hash_text(strings_put(o, put_text)))

static const struct life_slot strlist_slots[] = {{8, -1}, {0xc, -1}};
static const struct life_class strlist_class = {"StringStruct", 0x24, strlist_slots, 2};
// The arena's block, and where in it the next text goes.
static const struct life_slot strings_slots[] = {{4, -1}, {8, -1}};
static const struct life_class strings_class = {"Strings", 0x18, strings_slots, 2};

// StringList's entries, on Dialogs (whose item list is a StringList at 0xbc: the arena at 0xd4, the text to
// add at 0xd8, the two numbers at 0xdc and 0xe0), with an item of its own beyond the Dialog (at 0x100).
static uint32_t sl_result;
static const char *sl_texts[] = {"abc", "^abc", "^^abc", "^^^abc", "^", "^^", "^^^"};
#define SL_LIST(o) ((struct game_strlist *)((uint8_t *)(o) + 0xbc))
#define SL_ITEM(o) ((void *)((uint8_t *)(o) + 0x100))

static void life_orig_sl_dialog_ctor(void *o, void *x) { (void)x, game_dialog_ctor(o); }
LIFE_FN(stringlist_fill_entry, sl_result = (uint32_t)orig_stringlist_fill_entry(SL_LIST(o), SL_ITEM(o)),
        sl_result = (uint32_t)stringlist_fill_entry(SL_LIST(o), SL_ITEM(o)))
LIFE_FN(stringlist_kill_entry, orig_stringlist_kill_entry(SL_LIST(o), SL_ITEM(o)), stringlist_kill_entry(SL_LIST(o), SL_ITEM(o)))
LIFE_FN(stringlist_add_special, sl_result = (uint32_t)orig_stringlist_add_special(SL_LIST(o), sl_texts[(int)(intptr_t)x]),
        sl_result = (uint32_t)stringlist_add_special(SL_LIST(o), sl_texts[(int)(intptr_t)x]))

#define SL_ADD(call) \
    int r_ = call;   \
    add_result = ring_hash(SL_LIST(o), r_)
LIFE_FN(strlist_add, SL_ADD(orig_strlist_add(SL_LIST(o), (int)(intptr_t)x)), SL_ADD(strlist_add(SL_LIST(o), (int)(intptr_t)x)))

// The Dialog's data and cursor, its list's head and cursor, and the item's text.
static const struct life_slot stringlist_slots[] = {{8, -1}, {0xc, -1}, {0xc4, -1}, {0xc8, -1}, {0x104, 0}};
static const struct life_class stringlist_class = {"StringList", 0x110, stringlist_slots, 5};

static void life_sweep_stringlist(void) {
    uint32_t *head_global[1] = {(uint32_t *)game_at(SL_HEAD)}, *result_global[1] = {&sl_result}, *add_globals[2] = {&add_result, (uint32_t *)game_at(SL_HEAD)};
    (void)head_global;
    if (!game_dialog_ctor || !game_dialog_dtor || !game_mem_get) return;
    for (int round = 0; round < 6; round++) {
        uint8_t fill = (uint8_t)(round * 19 + 3);
        for (int arena = 0; arena < 2; arena++) {
            // Each of the entry functions on a Dialog the original made, with the text and numbers set and
            // the arena either the Dialog's own data or none.
            for (int which = 0; which < 3; which++) {
                life_build(&stringlist_class, fill, life_orig_sl_dialog_ctor, NULL);
                if (arena) life_data(0, 96);
                uint8_t *objs[2] = {life_a, life_b};
                for (int k = 0; k < 2; k++) {
                    uint8_t *o = objs[k];
                    memset(o + 0x100, 0, 16);
                    *(void **)(o + 0xd4) = arena ? o + 4 : NULL;
                    *(const char **)(o + 0xd8) = round == 5 ? NULL : sl_texts[round % 3];
                    *(int32_t *)(o + 0xdc) = 3 + round, *(int32_t *)(o + 0xe0) = 7 - round;
                }
                if (which == 1) life_text(0x104, "some entry text");
                if (which == 0)
                    life_run(&stats_stringlist_fill_entry, &stringlist_class, life_orig_stringlist_fill_entry, life_ours_stringlist_fill_entry, NULL, result_global, 1);
                if (which == 1)
                    life_run(&stats_stringlist_kill_entry, &stringlist_class, life_orig_stringlist_kill_entry, life_ours_stringlist_kill_entry, NULL, result_global, 1);
                if (which == 2)
                    life_run(&stats_stringlist_add_special, &stringlist_class, life_orig_stringlist_add_special, life_ours_stringlist_add_special,
                             (void *)(intptr_t)(round % 7), result_global, 1);
                // Give back what the check left: the item's text if it came from the heap (fill_entry) or was left (the
                // kill with an arena), then the Dialogs and what they hold, with the original's destructor.
                if ((which == 0 && !arena) || (which == 1 && arena)) {
                    game_free(*(void **)(life_a + 0x104));
                    game_free(*(void **)(life_b + 0x104));
                }
                game_dialog_dtor(life_a), game_dialog_dtor(life_b);
            }
            // An add on lists of 0 to 3 nodes: the list's own arena (its data), or none.
            for (int add = 0; add < 4; add++) {
                life_build(&stringlist_class, fill, life_orig_sl_dialog_ctor, NULL);
                if (arena) life_data(0, 1024);
                life_nodes(0xbc, (round + add) % 4);
                uint8_t *objs[2] = {life_a, life_b};
                for (int k = 0; k < 2; k++) {
                    uint8_t *o = objs[k];
                    memset(o + 0x100, 0, 16);
                    // (The word the list's base keeps its second arena in is another member's of the Dialog: clear.)
                    const int32_t *vbt = (const int32_t *)(uintptr_t)*(uint32_t *)(o + 0xc0);
                    *(uint32_t *)(o + 0xbc + vbt[1] + 8) = 0;
                    *(void **)(o + 0xd4) = arena ? o + 4 : NULL;
                    *(const char **)(o + 0xd8) = sl_texts[(round + add) % 3];  // (always text: an entry without it holds whatever the block did, which freeing it would trip on)
                    *(int32_t *)(o + 0xdc) = 3 + round, *(int32_t *)(o + 0xe0) = 7 - add;
                }
                life_run(&stats_strlist_add, &stringlist_class, life_orig_strlist_add, life_ours_strlist_add, (void *)(intptr_t)(round * 10 + add - 3), add_globals, 2);
                game_dialog_dtor(life_a), game_dialog_dtor(life_b);
            }
        }
    }
}

static void life_sweep_strings(void) {
    static uint32_t *none[1];
    uint32_t *head_global[1] = {(uint32_t *)game_at(SL_HEAD)};
    uint32_t *put_global[1] = {&put_result};
    if (!game_mem_get || !game_string_alloc) return;
    for (int round = 0; round < 4; round++) {
        uint8_t fill = (uint8_t)(round * 29 + 7);
        // The constructor of a whole object, and of a part (whose base table the maker has set).
        life_fill(&strlist_class, life_a, life_b, fill);
        life_run(&stats_strlist_ctor, &strlist_class, life_orig_strlist_ctor_most, life_ours_strlist_ctor_most, NULL, head_global, 1);
        life_fill(&strlist_class, life_a, life_b, fill);
        ((struct game_strlist *)life_a)->vbptr = ((struct game_strlist *)life_b)->vbptr = (void *)(uintptr_t)game_va(SL_VBPTR);
        life_run(&stats_strlist_ctor, &strlist_class, life_orig_strlist_ctor_part, life_ours_strlist_ctor_part, NULL, head_global, 1);
        // Close, remove_all and the destructor on lists of 0 to 3 nodes.
        for (int which = 0; which < 3; which++) {
            life_build(&strlist_class, fill, life_orig_strlist_ctor_most, NULL);
            life_nodes(0, round);
            if (which == 0) life_run(&stats_strlist_close, &strlist_class, life_orig_strlist_close, life_ours_strlist_close, NULL, none, 0);
            if (which == 1) life_run(&stats_strlist_remove_all, &strlist_class, life_orig_strlist_remove_all, life_ours_strlist_remove_all, NULL, none, 0);
            if (which == 2)
                life_run(&stats_strlist_scalar_dtor, &strlist_class, life_orig_strlist_scalar_dtor, life_ours_strlist_scalar_dtor, NULL, head_global, 1);
            // (The constructor saved the global the lists were made over: put it back.)
            if (which != 2) orig_strlist_scalar_dtor(life_a + 0x1c, 0), orig_strlist_scalar_dtor(life_b + 0x1c, 0);
        }
        // Strings.
        life_fill(&strings_class, life_a, life_b, fill);
        life_run(&stats_strings_ctor, &strings_class, life_orig_strings_ctor, life_ours_strings_ctor, NULL, none, 0);
        for (int again = 0; again < 2; again++) {
            life_build(&strings_class, fill, life_orig_strings_ctor, NULL);
            if (again) orig_strings_init((struct game_strings *)life_a, 64), orig_strings_init((struct game_strings *)life_b, 64);
            life_run(&stats_strings_init, &strings_class, life_orig_strings_init, life_ours_strings_init, (void *)(intptr_t)(96 + round), none, 0);
            // Put a text in, then shut it down.
            life_run(&stats_strings_put, &strings_class, life_orig_strings_put, life_ours_strings_put, NULL, put_global, 1);
            life_run(&stats_strings_shutdown, &strings_class, life_orig_strings_shutdown, life_ours_strings_shutdown, NULL, none, 0);
            orig_strings_shutdown((struct game_strings *)life_a), orig_strings_shutdown((struct game_strings *)life_b);
        }
    }
    // The arena's close and Strings' destructor, with and without a block.
    for (int which = 0; which < 4; which++) {
        life_build(&strings_class, (uint8_t)(which * 17 + 5), life_orig_strings_ctor, NULL);
        if (which & 1) orig_strings_init((struct game_strings *)life_a, 64), orig_strings_init((struct game_strings *)life_b, 64);
        if (which < 2) life_run(&stats_arena_close, &strings_class, life_orig_arena_close, life_ours_arena_close, NULL, none, 0);
        else life_run(&stats_strings_dtor, &strings_class, life_orig_strings_dtor, life_ours_strings_dtor, NULL, none, 0);
        orig_strings_shutdown((struct game_strings *)life_a), orig_strings_shutdown((struct game_strings *)life_b);
    }
    life_sweep_stringlist();
}

// ---- Hooks ---------------------------------------------------------------------------------------------------------

#define TIME_PRO 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xdb  // push ebx; push esi; mov esi, ecx; push edi; xor ebx, ebx
static const struct hook strings_hooks[] = {
    HOOK(0x401000, strlist_ctor, 0x55, 0x8b, 0xec, 0x51, 0x8b, 0x55, 0x08),  // push ebp; mov ebp, esp; push ecx; mov edx, [ebp+8]
    HOOK(0x401060, strlist_close, 0x55, 0x8b, 0xec, 0x51, 0x53),
    HOOK(0x402970, strlist_remove_all, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb),
    HOOK(0x4029f0, strlist_scalar_dtor, 0x55, 0x8b, 0xec, 0x56, 0x8d, 0x71, 0xe4),  // ... lea esi, [ecx-0x1c]
    HOOK(0x6086f0, stringlist_fill_entry, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x608770, stringlist_kill_entry, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x8b, 0x46, 0x04),
    HOOK(0x608ba0, stringlist_add_special, 0x8b, 0x44, 0x24, 0x04, 0x53, 0x33, 0xd2),
    // push esi; mov esi, ecx; call the arena's constructor
    HOOK_CALL(0x6168d0, strings_ctor, 0x56, 0x8b, 0xf1, 0xe8, 0x88, 0xdc, 0xfb, 0xff),
    HOOK(0x6168f0, strings_init, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x14, 0x85, 0xc0),
    HOOK_CALL(0x616950, strings_shutdown, 0x56, 0x8b, 0xf1, 0xe8, 0x28, 0xdc, 0xfb, 0xff),
    HOOK(0x616970, strings_put, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xf1),
    HOOK(0x5d45b0, arena_close, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb, 0x8b, 0x46, 0x04),  // push ebx; push esi; mov esi, ecx; xor ebx, ebx; mov eax, [esi+4]
    HOOK(0x6169c0, strings_dtor, 0xe9, 0xeb, 0xdb, 0xfb, 0xff),  // jmp arena_close
    HOOK(0x401100, strlist_add, 0x55, 0x8b, 0xec, 0x51, 0x53, 0x56, 0x8b, 0xf1, 0x33, 0xdb),
    HOOK(0x6167e0, time_timer_proc, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x85, 0xf6),  // push esi; mov esi, [esp+0x10]; test esi, esi
    HOOK(0x616820, time_mm_proc, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x85, 0xf6),
    HOOK(0x6169a0, strings_get, 0x8b, 0x41, 0x04, 0x8b, 0x54, 0x24, 0x04, 0x3b, 0xd0),
    HOOK(0x6161d0, time_ctor, 0x8b, 0xc1, 0x33, 0xc9, 0x89, 0x48, 0x04),
    HOOK(0x616200, time_dtor, TIME_PRO),
    HOOK(0x616260, time_init, TIME_PRO),
    HOOK(0x6162d0, time_init_2, TIME_PRO),
    HOOK(0x616350, time_start, TIME_PRO),
    HOOK(0x616410, time_start_2, TIME_PRO),
    HOOK(0x6164d0, time_pulse, TIME_PRO),
    HOOK(0x616590, time_pulse_2, TIME_PRO),
    HOOK(0x616650, time_start_3, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x08, 0x85, 0xc0),
    HOOK(0x6166c0, time_pulse_3, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x08, 0x85, 0xc0),
    HOOK(0x616730, time_stop, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x37, 0x8b, 0x47, 0x04),
    HOOK(0x616780, time_close, TIME_PRO),
    HOOK(0x401560, strlist_seek_id, 0x55, 0x8b, 0xec, 0x8b, 0x41, 0x08, 0x33, 0xd2, 0x53),
    HOOK(0x4015b0, strlist_seek_pos, 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x401640, strlist_current_id, 0x8b, 0x41, 0x08, 0x85, 0xc0),
    HOOK(0x402500, strlist_next_entry, 0x8b, 0x41, 0x08, 0x85, 0xc0),
    HOOK(0x402530, strlist_current_entry, 0x8b, 0x41, 0x08, 0x85, 0xc0),
    HOOK(0x6087a0, strlist_sort, 0x83, 0xec, 0x0c, 0x55, 0x56, 0x8b, 0xf1, 0x57),
};
// (The EditBox and EditGroup functions are only called while a screen has an edit box, so their sweep is chained to this
// module's, whose functions the game calls all the time: the string table's lookup and the buttons' timers.)
struct hook_module strings_module = {"strings", strings_hooks, HOOK_COUNT(strings_hooks), strings_sweep, &edit_module, -1};
