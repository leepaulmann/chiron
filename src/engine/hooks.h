// Replacing terranx.exe functions with Chiron's own, one at a time.
//
// Each hook names an original function by address and the bytes it must
// start with. Installing it writes a jump there to our function, and builds a
// trampoline (those first bytes plus a jump back) so the original can still
// be called. Modes, from CHIRON_REIMPL:
//   on      the game runs our functions (the default)
//   off     the original code only
//   shadow  every call runs both, compares, and returns the original's
//           answer; mismatches are logged and counted
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../loader/pe.h"

enum hook_mode { HOOKS_OFF, HOOKS_ON, HOOKS_SHADOW };

struct hook_stats {
    const char *name;
    unsigned long long calls, mismatches;
};

struct hook {
    uint32_t addr;
    const uint8_t *prologue;  // whole instructions, at least 5 bytes, no relative jumps (but see ends_in_call)
    uint8_t prologue_len;
    void *ours, *shadow;
    void **original;  // receives the trampoline to the original
    struct hook_stats *stats;
    int shadow_only;  // installed in shadow mode only, to keep checks quiet
    int ends_in_call;  // the prologue's last 5 bytes are a relative call, re-aimed in the trampoline
};

// A module: its hooks, and in shadow mode a sweep that checks every one of
// its functions over their whole input range on the current game state.
struct hook_module {
    const char *name;
    const struct hook *hooks;
    int count;
    void (*sweep)(void);
    // Another module to sweep whenever this one does, for one whose own
    // functions the game seldom calls.
    struct hook_module *also;
    // Shadow mode bookkeeping.
    int32_t last_turn;
    unsigned sweeps;
    double sweep_ms_total, sweep_ms_max;
};

extern enum hook_mode hooks_mode;

// Shadow mode: non-zero while an original function is running, so the
// originals it calls in turn run as they are, without comparing.
extern int hooks_nested;

// The mode from CHIRON_REIMPL (on, off or shadow).
enum hook_mode hooks_mode_from_env(void);

// Install every module's hooks for a recognised terranx.exe.
void hooks_apply(struct pe_image *exe, enum hook_mode mode);

// Shadow mode: called on entry to a shadowed function. The first call of
// each game turn runs the module's sweep.
void hooks_shadow_tick(struct hook_module *m);

// Shadow mode: record a mismatch; the first few are printed.
void hooks_mismatch(struct hook_stats *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Shadow mode for functions that write: the memory a call may change. A
// check saves it, runs the original, saves what that wrote, puts the memory
// back, runs ours, and compares. It then leaves the original's writes, or,
// while hooks_sweeping, the memory as it was before.
struct hook_writes {
    struct {
        void *p;
        size_t size;
    } span[8];
    int n;
    int depth;         // which save buffers, as checks can nest
    int protect_only;  // only saved and put back (hooks_writes_abandon), not a check
};

extern int hooks_sweeping;

// Set by the host: holds the game's clock still for this thread while
// nonzero calls outnumber zero ones. Checks of writing functions hold it, so
// both runs read the same time.
extern void (*hooks_hold_time)(int hold);

void hooks_writes_add(struct hook_writes *w, void *p, size_t size);
// Before the original runs.
void hooks_writes_begin(struct hook_writes *w);
// After the original: save its writes and put the memory back.
void hooks_writes_middle(struct hook_writes *w);
// Instead of the original: put the memory back as it was at begin. For a
// sweep that changes the game state to test on.
void hooks_writes_abandon(struct hook_writes *w);

// All of the game's state a check can put back: its writable memory, the
// map, and rand()'s state.
void hooks_writes_game(struct hook_writes *w);

// Non-zero while a writing function is being checked.
int hooks_checking(void);

// Non-zero while any saved state will be put back: a check, or a sweep's
// protected copy of the game.
int hooks_protecting(void);

// After ours: compare with the original's writes and return value, then
// leave the memory as described above. Returns NULL if they agree, or else
// what differs.
const char *hooks_writes_end(struct hook_writes *w, int got, int want);

// Stale stack. Some originals read locals they never set, and so act on
// what earlier calls left below the stack pointer. A check of such a
// function gives both runs the same HOOK_STALE_SIZE bytes below their entry
// stack pointer (where the return address is): those the game left where it
// made the call.
#define HOOK_STALE_SIZE 0x100
struct hook_stale {
    uint32_t w[HOOK_STALE_SIZE / 4];
};
// The word `below` bytes under the entry stack pointer.
#define HOOK_STALE_AT(s, below) ((s)->w[(HOOK_STALE_SIZE - (below)) / 4])
// Calls fn() with *s below its entry stack pointer.
void hooks_stale_call(void (*fn)(void), const struct hook_stale *s);

// Shadow mode: messages, sounds and movies held back while state is
// protected. A quiet hook on such a function (quiet.c) records the call
// instead of making it. A check compares what the original and ours would
// have shown, as part of their result. When the outermost check of a call
// from the game is over, the original's are made for real, in order, so a
// player sees each message once; a sweep's are dropped.
enum { HOOK_UI_TEXTS = 3, HOOK_UI_TEXT_LEN = 256 };
struct hook_ui_call {
    const char *name;
    void (*replay)(const struct hook_ui_call *c);
    int arg[8];
    unsigned has_text;  // bit i: text[i] holds a string argument (else it was NULL)
    char text[HOOK_UI_TEXTS][HOOK_UI_TEXT_LEN];
};
void hooks_ui_hold(const struct hook_ui_call *c);
// Copy a string argument into c->text[i].
void hooks_ui_text(struct hook_ui_call *c, int i, const char *s);
// After a check has reported: make the held calls, if no state is still
// protected.
void hooks_ui_release(void);

// Shadow mode: print calls and mismatches per function.
void hooks_summary(void);
