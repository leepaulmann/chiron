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

#include <stdint.h>

#include "../loader/pe.h"

enum hook_mode { HOOKS_OFF, HOOKS_ON, HOOKS_SHADOW };

struct hook_stats {
    const char *name;
    unsigned long long calls, mismatches;
};

struct hook {
    uint32_t addr;
    const uint8_t *prologue;  // whole instructions, at least 5 bytes, no relative jumps
    uint8_t prologue_len;
    void *ours, *shadow;
    void **original;  // receives the trampoline to the original
    struct hook_stats *stats;
};

// A module: its hooks, and in shadow mode a sweep that checks every one of
// its functions over their whole input range on the current game state.
struct hook_module {
    const char *name;
    const struct hook *hooks;
    int count;
    void (*sweep)(void);
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

// Shadow mode: print calls and mismatches per function.
void hooks_summary(void);
