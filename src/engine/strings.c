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
// Here are all of Time's functions that start, stop and set up the timer, StringStruct's seek and walk
// functions, StringList's sort and the string table's lookup. Left to the original: the procedures the
// timers call (TimerProc and MultimediaProc run on the timer's thread or in the window's handler and
// post a message), StringStruct's constructor, destructor, close, add and remove_all and StringList's
// fill_entry, kill_entry, load and add_special (they allocate and free on the game's heap, or read a
// file), and Strings' constructor, init, shutdown and put (they allocate).
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
    memset(T, 0, sizeof T);  // (a timer message that arrives later finds nothing to do)
    hooks_sweeping = sweeping;
}

// ---- Hooks ---------------------------------------------------------------------------------------------------------

#define TIME_PRO 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xdb  // push ebx; push esi; mov esi, ecx; push edi; xor ebx, ebx
static const struct hook strings_hooks[] = {
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
