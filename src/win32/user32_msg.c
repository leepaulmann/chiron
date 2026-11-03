// USER32: the message queue, timers, input from SDL, and DefWindowProc.
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "../loader/modules.h"
#include "../loader/teb.h"
#include "gdi.h"
#include "user32.h"

typedef struct {
    HWND hwnd;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    POINT pt;
} MSG;

enum {
    WM_PAINT = 0x000f,
    WM_CLOSE = 0x0010,
    WM_QUIT = 0x0012,
    WM_ERASEBKGND = 0x0014,
    WM_SETCURSOR = 0x0020,
    WM_NCCREATE = 0x0081,
    WM_KEYDOWN = 0x0100,
    WM_KEYUP = 0x0101,
    WM_CHAR = 0x0102,
    WM_SYSKEYDOWN = 0x0104,
    WM_SYSKEYUP = 0x0105,
    WM_TIMER = 0x0113,
    WM_MOUSEMOVE = 0x0200,
    WM_LBUTTONDOWN = 0x0201,
    WM_MOUSEWHEEL = 0x020a,
};

// ---- Queues ---------------------------------------------------------------
// Each thread has its own queue, as on Windows: soundx.dll runs a message
// loop on its own thread and must not see the game window's input. Only the
// thread that owns SDL (the game thread) pumps input, timers and paints.

#define QUEUE_SIZE 1024
#define MAX_QUEUES 16

struct msgqueue {
    DWORD tid;
    MSG msgs[QUEUE_SIZE];
    int head, count;
    int quit_posted, quit_code;
};

static struct msgqueue queues[MAX_QUEUES];
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;
static DWORD sdl_tid;

static POINT cursor_pos;
static uint8_t key_state[256];  // bit 7 = down, bit 0 = toggled
static uint8_t async_pressed[256];

static DWORD current_tid(void) { return teb_current()->tid; }

// Must be called with q_lock held.
static struct msgqueue *queue_for(DWORD tid) {
    struct msgqueue *free_slot = NULL;
    for (int i = 0; i < MAX_QUEUES; i++) {
        if (queues[i].tid == tid) return &queues[i];
        if (!queues[i].tid && !free_slot) free_slot = &queues[i];
    }
    if (free_slot) free_slot->tid = tid;
    return free_slot;
}

static void enqueue_to(DWORD tid, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    pthread_mutex_lock(&q_lock);
    struct msgqueue *q = queue_for(tid);
    if (q && q->count < QUEUE_SIZE) {
        q->msgs[(q->head + q->count++) % QUEUE_SIZE] = (MSG){hwnd, msg, wp, lp, SDL_GetTicks(), cursor_pos};
        pthread_cond_broadcast(&q_cond);
    } else {
        fprintf(stderr, "chiron: message queue full, dropping 0x%x\n", msg);
    }
    pthread_mutex_unlock(&q_lock);
    if (tid == sdl_tid && current_tid() != sdl_tid) sdl_host_wake();
}

// Input generated on the SDL thread goes to that thread's queue.
static void enqueue(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { enqueue_to(sdl_tid, hwnd, msg, wp, lp); }

static int matches(const MSG *m, HWND hwnd, UINT lo, UINT hi) {
    if (hwnd && m->hwnd != hwnd) return 0;
    if (lo || hi) return m->message >= lo && m->message <= hi;
    return 1;
}

// Take (or peek at) the first matching message from the calling thread's
// queue, including a pending WM_QUIT.
static int dequeue(MSG *out, HWND hwnd, UINT lo, UINT hi, int remove) {
    int found = 0;
    pthread_mutex_lock(&q_lock);
    struct msgqueue *q = queue_for(current_tid());
    for (int i = 0; q && i < q->count; i++) {
        MSG *m = &q->msgs[(q->head + i) % QUEUE_SIZE];
        if (!matches(m, hwnd, lo, hi)) continue;
        *out = *m;
        if (remove) {
            for (int j = i; j > 0; j--) q->msgs[(q->head + j) % QUEUE_SIZE] = q->msgs[(q->head + j - 1) % QUEUE_SIZE];
            q->head = (q->head + 1) % QUEUE_SIZE;
            q->count--;
        }
        found = 1;
        break;
    }
    if (!found && q && q->quit_posted) {
        *out = (MSG){NULL, WM_QUIT, q->quit_code, 0, SDL_GetTicks(), cursor_pos};
        if (remove) q->quit_posted = 0;
        found = 1;
    }
    pthread_mutex_unlock(&q_lock);
    return found;
}

static int queue_has_messages(void) {
    pthread_mutex_lock(&q_lock);
    struct msgqueue *q = queue_for(current_tid());
    int n = q ? q->count + q->quit_posted : 0;
    pthread_mutex_unlock(&q_lock);
    return n > 0;
}

DWORD user32_window_tid(HWND hwnd);

BOOL WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD tid = hwnd ? user32_window_tid(hwnd) : current_tid();
    if (!tid) return FALSE;
    enqueue_to(tid, hwnd, msg, wp, lp);
    return TRUE;
}

BOOL WINAPI PostThreadMessageA(DWORD tid, UINT msg, WPARAM wp, LPARAM lp) {
    enqueue_to(tid, NULL, msg, wp, lp);
    return TRUE;
}

void WINAPI PostQuitMessage(int code) {
    pthread_mutex_lock(&q_lock);
    struct msgqueue *q = queue_for(current_tid());
    if (q) q->quit_posted = 1, q->quit_code = code;
    pthread_mutex_unlock(&q_lock);
}

LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return user32_send(hwnd, msg, wp, lp);
}

// ---- Timers ---------------------------------------------------------------

typedef void(WINAPI *TIMERPROC)(HWND, UINT, UINT, DWORD);

struct timer {
    HWND hwnd;
    UINT id;
    UINT interval;
    uint32_t due;
    TIMERPROC proc;
    struct timer *next;
};
static struct timer *timers;

UINT WINAPI SetTimer(HWND hwnd, UINT id, UINT ms, TIMERPROC proc) {
    struct timer *t;
    for (t = timers; t; t = t->next)
        if (t->hwnd == hwnd && t->id == id && hwnd) break;
    if (!t) {
        t = calloc(1, sizeof(*t));
        t->next = timers;
        timers = t;
        static UINT next_id = 0x7000;
        t->id = hwnd ? id : next_id++;
    }
    t->hwnd = hwnd;
    t->interval = ms < 10 ? 10 : ms;
    t->due = SDL_GetTicks() + t->interval;
    t->proc = proc;
    return t->id;
}

BOOL WINAPI KillTimer(HWND hwnd, UINT id) {
    for (struct timer **pp = &timers; *pp; pp = &(*pp)->next)
        if ((*pp)->hwnd == hwnd && (*pp)->id == id) {
            struct timer *t = *pp;
            *pp = t->next;
            free(t);
            return TRUE;
        }
    return FALSE;
}

void user32_timers_destroyed(HWND hwnd) {
    for (struct timer **pp = &timers; *pp;) {
        if ((*pp)->hwnd == hwnd) {
            struct timer *t = *pp;
            *pp = t->next;
            free(t);
        } else {
            pp = &(*pp)->next;
        }
    }
}

// Produce a WM_TIMER for the first due timer. Returns ms until the next one
// is due (or -1 for none) when nothing fired.
static int fire_timer(MSG *out, HWND hwnd) {
    uint32_t now = SDL_GetTicks();
    int wait = -1;
    for (struct timer *t = timers; t; t = t->next) {
        if (hwnd && t->hwnd != hwnd) continue;
        int32_t left = (int32_t)(t->due - now);
        if (left <= 0) {
            t->due = now + t->interval;
            *out = (MSG){t->hwnd, WM_TIMER, t->id, (LPARAM)t->proc, now, cursor_pos};
            return 0;
        }
        if (wait < 0 || left < wait) wait = left;
    }
    return wait < 0 ? -1 : wait;
}

// ---- Input from SDL ---------------------------------------------------------

static WORD sdl_to_vk(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0x70 + (k - SDLK_F1);
    int numlock = (SDL_GetModState() & KMOD_NUM) != 0;
    switch (k) {
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_PAUSE: return 0x13;
    case SDLK_CAPSLOCK: return 0x14;
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_SPACE: return 0x20;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;
    // The numeric keypad moves units; without NumLock Windows reports the
    // navigation keys printed on it.
    case SDLK_KP_0: return numlock ? 0x60 : 0x2d;
    case SDLK_KP_1: return numlock ? 0x61 : 0x23;
    case SDLK_KP_2: return numlock ? 0x62 : 0x28;
    case SDLK_KP_3: return numlock ? 0x63 : 0x22;
    case SDLK_KP_4: return numlock ? 0x64 : 0x25;
    case SDLK_KP_5: return numlock ? 0x65 : 0x0c;
    case SDLK_KP_6: return numlock ? 0x66 : 0x27;
    case SDLK_KP_7: return numlock ? 0x67 : 0x24;
    case SDLK_KP_8: return numlock ? 0x68 : 0x26;
    case SDLK_KP_9: return numlock ? 0x69 : 0x21;
    case SDLK_KP_MULTIPLY: return 0x6a;
    case SDLK_KP_PLUS: return 0x6b;
    case SDLK_KP_MINUS: return 0x6d;
    case SDLK_KP_PERIOD: return numlock ? 0x6e : 0x2e;
    case SDLK_KP_DIVIDE: return 0x6f;
    case SDLK_NUMLOCKCLEAR: return 0x90;
    case SDLK_SCROLLLOCK: return 0x91;
    case SDLK_SEMICOLON: return 0xba;
    case SDLK_EQUALS: return 0xbb;
    case SDLK_COMMA: return 0xbc;
    case SDLK_MINUS: return 0xbd;
    case SDLK_PERIOD: return 0xbe;
    case SDLK_SLASH: return 0xbf;
    case SDLK_BACKQUOTE: return 0xc0;
    case SDLK_LEFTBRACKET: return 0xdb;
    case SDLK_BACKSLASH: return 0xdc;
    case SDLK_RIGHTBRACKET: return 0xdd;
    case SDLK_QUOTE: return 0xde;
    }
    return 0;
}

WORD user32_vk_to_scan(WORD vk);

static WPARAM mouse_keys(void) {
    WPARAM wp = 0;
    if (key_state[0x01] & 0x80) wp |= 0x01;
    if (key_state[0x02] & 0x80) wp |= 0x02;
    if (key_state[0x10] & 0x80) wp |= 0x04;
    if (key_state[0x11] & 0x80) wp |= 0x08;
    if (key_state[0x04] & 0x80) wp |= 0x10;
    return wp;
}

static void post_mouse(UINT msg, int x, int y) {
    cursor_pos.x = x;
    cursor_pos.y = y;
    POINT client;
    HWND target = user32_window_at(x, y, &client);
    if (target) enqueue(target, msg, mouse_keys(), (LPARAM)((client.y << 16) | (client.x & 0xffff)));
}

static void set_key(WORD vk, int down) {
    if (down) {
        if (!(key_state[vk] & 0x80)) key_state[vk] ^= 1;
        key_state[vk] |= 0x80;
        async_pressed[vk] = 1;
    } else {
        key_state[vk] &= ~0x80;
    }
}

static void handle_sdl_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_QUIT: {
        struct window *w = user32_main_window();
        if (w) enqueue(w, WM_CLOSE, 0, 0);
        else PostQuitMessage(0);
        break;
    }
    case SDL_MOUSEMOTION:
        post_mouse(WM_MOUSEMOVE, e->motion.x, e->motion.y);
        user32_apply_cursor(NULL);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int down = e->type == SDL_MOUSEBUTTONDOWN;
        int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1
              : e->button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        if (b < 0) break;
        static const WORD vks[3] = {0x01, 0x02, 0x04};
        set_key(vks[b], down);
        UINT msg = WM_LBUTTONDOWN + b * 3 + (down ? 0 : 1);
        if (down && e->button.clicks == 2) {
            // Double clicks are only reported to classes that ask for them.
            POINT client;
            HWND target = user32_window_at(e->button.x, e->button.y, &client);
            if (target && (user32_class_style(target) & 0x0008)) msg += 2;  // WM_xBUTTONDBLCLK
        }
        post_mouse(msg, e->button.x, e->button.y);
        break;
    }
    case SDL_MOUSEWHEEL: {
        HWND target = GetFocus();
        if (target)
            enqueue(target, WM_MOUSEWHEEL, (WPARAM)((e->wheel.y * 120) << 16) | mouse_keys(),
                    (LPARAM)((cursor_pos.y << 16) | (cursor_pos.x & 0xffff)));
        break;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        WORD vk = sdl_to_vk(e->key.keysym.sym);
        if (!vk) break;
        int down = e->type == SDL_KEYDOWN;
        set_key(vk, down);
        HWND target = GetFocus();
        if (!target) break;
        int alt = (SDL_GetModState() & KMOD_ALT) != 0;
        LPARAM lp = 1 | ((LPARAM)user32_vk_to_scan(vk) << 16);
        if (alt) lp |= 1 << 29;
        if (!down) lp |= 0xc0000000u;
        else if (e->key.repeat) lp |= 0x40000000;
        UINT msg = alt || vk == 0x12 ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP);
        enqueue(target, msg, vk, lp);
        break;
    }
    case SDL_TEXTINPUT: {
        HWND target = GetFocus();
        if (!target) break;
        // Decode UTF-8 into Windows-1252 (Latin-1 range only).
        const unsigned char *s = (const unsigned char *)e->text.text;
        while (*s) {
            unsigned c = *s++;
            if (c >= 0xc0 && c < 0xe0 && *s) c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
            else if (c >= 0xe0) {
                while (*s && (*s & 0xc0) == 0x80) s++;
                c = '?';
            }
            if (c < 0x100) enqueue(target, WM_CHAR, c, 1);
        }
        break;
    }
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_EXPOSED) user32_screen_dirty();
        break;
    }
}

// Present the screen when something changed, at most once per frame.
static int screen_dirty;
static uint32_t last_present;

void user32_screen_dirty(void) { screen_dirty = 1; }

static void maybe_present(int force) {
    struct window *w = user32_main_window();
    if (!w || !screen_dirty || ddraw_owns_screen()) return;
    uint32_t now = SDL_GetTicks();
    if (!force && now - last_present < 8) return;
    last_present = now;
    screen_dirty = 0;
    sdl_host_present(user32_framebuffer(w), user32_title(w));
}

void user32_present_if_due(void) {
    if (current_tid() == sdl_tid) maybe_present(0);
}

// CHIRON_SCRIPT drives the game for unattended testing. It is a list of
// "<seconds> <action>" steps separated by ';', where an action is
// "click X Y" (left click in screen coordinates), "dblclick X Y",
// "move X Y", "key NAME" (an SDL key name such as Return or Escape,
// optionally "Ctrl+NAME"), "type TEXT" (typed characters, up to the next
// ';') or "close" (the window's close button).
// "<seconds> every <interval> <action>" repeats an action from then on:
//   CHIRON_SCRIPT="25 click 835 362; 40 every 0.5 key Return; 600 close"
// Keys stay down for 150 ms, like a real press, so code that polls
// GetAsyncKeyState sees them held.
static SDL_Keycode held_key, held_mod;
static Uint32 release_at;

static void push_key(SDL_Keycode k, int down) {
    SDL_Event e = {.type = down ? SDL_KEYDOWN : SDL_KEYUP};
    e.key.keysym.sym = k;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&e);
}

static void release_keys(void) {
    if (held_key) push_key(held_key, 0);
    if (held_mod) push_key(held_mod, 0);
    held_key = held_mod = 0;
}

static void push_click(int x, int y, int clicks) {
    SDL_Event e = {.type = SDL_MOUSEMOTION};
    e.motion.x = x, e.motion.y = y;
    SDL_PushEvent(&e);
    e = (SDL_Event){.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT, e.button.state = SDL_PRESSED, e.button.clicks = clicks;
    e.button.x = x, e.button.y = y;
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP, e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

static void run_action(const char *step) {
    int x, y;
    char name[64];
    if (sscanf(step, " click %d %d", &x, &y) == 2) {
        push_click(x, y, 1);
    } else if (sscanf(step, " move %d %d", &x, &y) == 2) {
        SDL_Event e = {.type = SDL_MOUSEMOTION};
        e.motion.x = x, e.motion.y = y;
        SDL_PushEvent(&e);
    } else if (sscanf(step, " dblclick %d %d", &x, &y) == 2) {
        push_click(x, y, 1);
        push_click(x, y, 2);
    } else if (sscanf(step, " type %31[^;]", name) == 1) {
        SDL_Event e = {.type = SDL_TEXTINPUT};
        snprintf(e.text.text, sizeof(e.text.text), "%.31s", name);
        SDL_PushEvent(&e);
    } else if (sscanf(step, " key %63[^;]", name) == 1) {
        int ctrl = !strncasecmp(name, "Ctrl+", 5);
        SDL_Keycode k = SDL_GetKeyFromName(name + (ctrl ? 5 : 0));
        if (k != SDLK_UNKNOWN) {
            release_keys();
            if (ctrl) push_key(held_mod = SDLK_LCTRL, 1);
            push_key(k, 1);
            held_key = k;
            release_at = SDL_GetTicks() + 150;
        }
    } else if (!strncmp(step, " close", 6)) {
        SDL_Event e = {.type = SDL_QUIT};
        SDL_PushEvent(&e);
    }
}

#define MAX_REPEATS 8
static struct {
    char action[96];
    Uint32 interval, next;
} repeats[MAX_REPEATS];
static int nrepeats;

void soak_report(void);

static void run_test_script(void) {
    static const char *next;
    static int started;
    static Uint32 t0;
    if (!started) {
        started = 1;
        next = getenv("CHIRON_SCRIPT");
        t0 = SDL_GetTicks();
    }
    if (held_key && SDL_TICKS_PASSED(SDL_GetTicks(), release_at)) release_keys();
    while (next && *next) {
        double at;
        int used;
        if (sscanf(next, " %lf%n", &at, &used) != 1) {
            next = NULL;
            break;
        }
        if (!SDL_TICKS_PASSED(SDL_GetTicks(), t0 + (Uint32)(at * 1000))) break;
        const char *step = next + used;
        const char *end = strchr(step, ';');
        next = end ? end + 1 : NULL;
        fprintf(stderr, "chiron: script at %.1fs:%.*s\n", at, end ? (int)(end - step) : (int)strlen(step), step);
        double every;
        int skip;
        if (sscanf(step, " every %lf%n", &every, &skip) == 1 && nrepeats < MAX_REPEATS) {
            int n = end ? (int)(end - step - skip) : (int)strlen(step + skip);
            snprintf(repeats[nrepeats].action, sizeof(repeats[0].action), "%.*s", n, step + skip);
            repeats[nrepeats].interval = (Uint32)(every * 1000);
            repeats[nrepeats].next = SDL_GetTicks();
            nrepeats++;
        } else {
            run_action(step);
        }
    }
    for (int i = 0; i < nrepeats; i++)
        if (SDL_TICKS_PASSED(SDL_GetTicks(), repeats[i].next)) {
            repeats[i].next += repeats[i].interval;
            run_action(repeats[i].action);
        }
    soak_report();
}

static void pump_sdl(int wait_ms) {
    SDL_Event e;
    run_test_script();
    if (wait_ms != 0 && SDL_WaitEventTimeout(&e, wait_ms < 0 ? 100 : wait_ms)) handle_sdl_event(&e);
    while (SDL_PollEvent(&e)) handle_sdl_event(&e);
    ddraw_present_pending();
    sdl_host_snapshot_tick();
}

// Look for a message: posted/input first, then (on the SDL thread) timers,
// then paint.
static int get_next(MSG *m, HWND hwnd, UINT lo, UINT hi, int remove, int *timer_wait) {
    *timer_wait = -1;
    if (dequeue(m, hwnd, lo, hi, remove)) return 1;
    if (current_tid() != sdl_tid) return 0;
    if ((!lo && !hi) || (lo <= WM_TIMER && hi >= WM_TIMER)) {
        MSG t;
        int w = fire_timer(&t, hwnd);
        if (w == 0) {
            *m = t;
            return 1;
        }
        *timer_wait = w;
    }
    if ((!lo && !hi) || (lo <= WM_PAINT && hi >= WM_PAINT)) {
        HWND p = user32_next_paint();
        if (p && (!hwnd || p == hwnd)) {
            *m = (MSG){p, WM_PAINT, 0, 0, SDL_GetTicks(), cursor_pos};
            return 1;
        }
    }
    return 0;
}

// The game's main thread owns SDL; other threads (soundx.dll's) only use
// their own queues.
void user32_set_sdl_thread(void) { sdl_tid = current_tid(); }
int user32_on_sdl_thread(void) { return current_tid() == sdl_tid; }

static __thread int idle_peeks;

BOOL WINAPI PeekMessageA(MSG *m, HWND hwnd, UINT lo, UINT hi, UINT flags) {
    if (current_tid() == sdl_tid) {
        sdl_host_init();
        pump_sdl(0);
        maybe_present(0);
    }
    int wait;
    if (get_next(m, hwnd, lo, hi, flags & 1 /* PM_REMOVE */, &wait)) {
        idle_peeks = 0;
        return TRUE;
    }
    // The game polls PeekMessage in a tight loop while idle; yield a little
    // after a run of empty polls so an idle menu does not pin a CPU core.
    if (++idle_peeks >= 64) {
        idle_peeks = 0;
        SDL_Delay(1);
    }
    return FALSE;
}

BOOL WINAPI GetMessageA(MSG *m, HWND hwnd, UINT lo, UINT hi) {
    if (current_tid() == sdl_tid) sdl_host_init();
    for (;;) {
        int wait, on_sdl = current_tid() == sdl_tid;
        if (on_sdl) pump_sdl(0);
        if (get_next(m, hwnd, lo, hi, 1, &wait)) return m->message != WM_QUIT;
        if (on_sdl) {
            maybe_present(1);
            pump_sdl(wait < 0 ? -1 : wait);
        } else {
            pthread_mutex_lock(&q_lock);
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50 * 1000000L;
            if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
            pthread_cond_timedwait(&q_cond, &q_lock, &ts);
            pthread_mutex_unlock(&q_lock);
        }
    }
}

DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms);
DWORD WINAPI GetTickCount(void);

// Wait for handles or for a message to arrive in this thread's queue, which
// is reported as WAIT_OBJECT_0 + n. Polls in short slices.
DWORD WINAPI MsgWaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, DWORD wake_mask) {
    DWORD start = GetTickCount();
    for (;;) {
        if (current_tid() == sdl_tid) pump_sdl(0);
        if (queue_has_messages()) return n;
        DWORD elapsed = GetTickCount() - start;
        if (ms != 0xffffffffu && elapsed >= ms) return 0x102;  // WAIT_TIMEOUT
        DWORD slice = ms == 0xffffffffu || ms - elapsed > 10 ? 10 : ms - elapsed;
        if (n) {
            DWORD r = WaitForMultipleObjects(n, hs, all, slice);
            if (r != 0x102) return r;
        } else {
            SDL_Delay(slice);
        }
    }
}

BOOL WINAPI TranslateMessage(const MSG *m) {
    // Printable characters arrive as SDL text input; Windows also produces
    // WM_CHAR for these control keys and for Ctrl+letter.
    if (m->message != WM_KEYDOWN) return FALSE;
    WPARAM c = 0;
    if (m->wparam == 0x08 || m->wparam == 0x09 || m->wparam == 0x0d || m->wparam == 0x1b) c = m->wparam;
    else if ((key_state[0x11] & 0x80) && m->wparam >= 'A' && m->wparam <= 'Z') c = m->wparam - 'A' + 1;
    if (!c) return FALSE;
    enqueue(m->hwnd, WM_CHAR, c, m->lparam);
    return TRUE;
}

// Windows only calls a WM_TIMER's lParam as a TIMERPROC when it matches a
// timer registered with that procedure; a posted WM_TIMER with any other
// lParam goes to the window procedure.
static int registered_timer_proc(HWND hwnd, UINT id, void *proc) {
    for (struct timer *t = timers; t; t = t->next)
        if (t->hwnd == hwnd && t->id == id && (void *)t->proc == proc) return 1;
    return 0;
}

LRESULT WINAPI DispatchMessageA(const MSG *m) {
    if (m->message == WM_TIMER && m->lparam) {
        if (registered_timer_proc(m->hwnd, m->wparam, (void *)m->lparam)) {
            ((TIMERPROC)m->lparam)(m->hwnd, WM_TIMER, m->wparam, SDL_GetTicks());
            return 0;
        }
        // terranx.exe posts its own WM_TIMER messages with small non-pointer
        // lParams; calling them used to crash during combat animations.
        if (modules_trace)
            fprintf(stderr, "[user32] WM_TIMER lParam 0x%08x is not a registered TIMERPROC\n", (unsigned)m->lparam);
    }
    if (m->message == WM_PAINT) {
        LRESULT r = user32_send(m->hwnd, WM_PAINT, 0, 0);
        // A window procedure that ignores WM_PAINT would be asked forever.
        extern BOOL WINAPI ValidateRect(HWND, const RECT *);
        ValidateRect(m->hwnd, NULL);
        return r;
    }
    return user32_send(m->hwnd, m->message, m->wparam, m->lparam);
}

LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE: return TRUE;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR:
        user32_apply_cursor(user32_class_cursor(hwnd));
        return TRUE;
    }
    return 0;
}

// ---- Keyboard and cursor state --------------------------------------------

short WINAPI GetKeyState(int vk) {
    uint8_t s = key_state[vk & 0xff];
    return (short)(((s & 0x80) ? 0x8000 : 0) | (s & 1));
}

short WINAPI GetAsyncKeyState(int vk) {
    // Async state must see input even when the game polls without pumping.
    if (current_tid() == sdl_tid) pump_sdl(0);
    uint8_t s = key_state[vk & 0xff];
    short r = (short)(((s & 0x80) ? 0x8000 : 0) | async_pressed[vk & 0xff]);
    async_pressed[vk & 0xff] = 0;
    return r;
}

BOOL WINAPI GetCursorPos(POINT *p) {
    *p = cursor_pos;
    return TRUE;
}

BOOL WINAPI SetCursorPos(int x, int y) {
    cursor_pos.x = x;
    cursor_pos.y = y;
    sdl_host_warp_mouse(x, y);
    return TRUE;
}

BOOL WINAPI ReleaseCapture(void) { return TRUE; }

UINT WINAPI GetKeyboardType(int which) {
    switch (which) {
    case 0: return 4;   // enhanced 101/102-key keyboard
    case 2: return 12;  // function keys
    }
    return 0;
}
