// Shadow-mode wrappers and hook table entries for reimplemented functions.
//
// For a function `name` that returns a value and has no side effects,
// SHADOWn(module, type, name, argument types...) defines:
//   orig_name    the trampoline to the original, filled in by hooks_apply
//   stats_name   its call and mismatch counts
//   check_name   runs the original and ours with the same arguments,
//                reports a difference and returns the original's answer
//   shadow_name  what the game calls in shadow mode
// `module` is the struct hook_module whose sweep a call may start, or NULL
// for a function the game can call while that module's data isn't ready.
//
// Functions that write instead write their own check_ and shadow_.
#pragma once

#include <stddef.h>

#include "hooks.h"

static inline void shadow_tick(struct hook_module *m) {
    if (m) hooks_shadow_tick(m);
}

#define SHADOW_DECLARE(ret, name, params) \
    static ret(*orig_##name) params;      \
    static struct hook_stats stats_##name = {#name};

#define SHADOW_ENTRY(module, ret, name, params, args) \
    static ret shadow_##name params {                \
        if (hooks_nested) return orig_##name args;   \
        stats_##name.calls++;                        \
        shadow_tick(module);                         \
        return check_##name args;                    \
    }

#define SHADOW_CHECK(ret, name, params, args, fmt, ...)                                                \
    static ret check_##name params {                                                                   \
        hooks_nested++;                                                                                \
        ret want = orig_##name args;                                                                   \
        hooks_nested--;                                                                                \
        ret got = name args;                                                                           \
        if (got != want)                                                                               \
            hooks_mismatch(&stats_##name, "(" fmt ") = %d, original %d", __VA_ARGS__, (int)got, (int)want); \
        return want;                                                                                   \
    }

#define SHADOW_FN(module, ret, name, params, args, fmt, ...)    \
    SHADOW_DECLARE(ret, name, params)                           \
    SHADOW_CHECK(ret, name, params, args, fmt, __VA_ARGS__)     \
    SHADOW_ENTRY(module, ret, name, params, args)

#define SHADOW1(m, ret, name, t1) SHADOW_FN(m, ret, name, (t1 a), (a), "%d", (int)a)
#define SHADOW2(m, ret, name, t1, t2) SHADOW_FN(m, ret, name, (t1 a, t2 b), (a, b), "%d, %d", (int)a, (int)b)
#define SHADOW3(m, ret, name, t1, t2, t3) \
    SHADOW_FN(m, ret, name, (t1 a, t2 b, t3 c), (a, b, c), "%d, %d, %d", (int)a, (int)b, (int)c)
#define SHADOW4(m, ret, name, t1, t2, t3, t4)                                                          \
    SHADOW_FN(m, ret, name, (t1 a, t2 b, t3 c, t4 d), (a, b, c, d), "%d, %d, %d, %d", (int)a, (int)b, \
              (int)c, (int)d)
#define SHADOW5(m, ret, name, t1, t2, t3, t4, t5)                                                    \
    SHADOW_FN(m, ret, name, (t1 a, t2 b, t3 c, t4 d, t5 e), (a, b, c, d, e), "%d, %d, %d, %d, %d", \
              (int)a, (int)b, (int)c, (int)d, (int)e)

// A hook table entry: the original's address, our function, and the first
// bytes of the original (whole instructions, no relative jumps).
#define HOOK(addr, name, ...)                                                                   \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, \
     (void *)shadow_##name, (void **)&orig_##name, &stats_##name}

#define HOOK_COUNT(hooks) ((int)(sizeof(hooks) / sizeof((hooks)[0])))
