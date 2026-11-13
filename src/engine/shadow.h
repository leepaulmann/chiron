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

// Calls made while a check of a writing function runs ours, or during a
// sweep, go straight to ours: the outer check compares their effects, and
// a check of their own would undo them in a sweep.
static inline int shadow_bypass(void) { return hooks_sweeping || hooks_checking(); }

#define SHADOW_DECLARE(ret, name, params) \
    static ret(*orig_##name) params;      \
    static struct hook_stats stats_##name = {#name};

#define SHADOW_ENTRY(module, ret, name, params, args) \
    static ret shadow_##name params {                \
        if (hooks_nested) return orig_##name args;   \
        if (shadow_bypass()) return name args;       \
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

// Functions that write. `writes` is code that names the memory a call may
// change, with WRITES(pointer, size), using the arguments a, b, c...; the
// check compares that memory as well as the return value. For a function
// that returns nothing, give void as `ret`.
#define WRITES(p, size) hooks_writes_add(&w_, (void *)(p), (size))
// A device context's state in Chiron's GDI (see hooks_dc_state).
#define WRITES_DC(hdc) hooks_writes_dc(&w_, (void *)(hdc))
// The pixels of the real window's framebuffer, which a copy to the window writes (see hooks_window_pixels).
#define WRITES_WINDOW(hwnd) hooks_writes_window(&w_, (void *)(hwnd))

#define SHADOW_WRITER(module, ret, name, params, args, writes, fmt, ...) \
    SHADOW_WRITER_VIA(module, ret, name, params, args, orig_##name args, writes, fmt, __VA_ARGS__)

// The same, calling the original with the expression `call`.
#define SHADOW_WRITER_VIA(module, ret, name, params, args, call, writes, fmt, ...)    \
    SHADOW_WRITER_CHECK(ret, name, params, args, call, writes, fmt, __VA_ARGS__)      \
    SHADOW_WRITER_ENTRY(module, ret, name, params, args)

// Only orig_, stats_ and check_, for a function with a shadow_ of its own.
#define SHADOW_WRITER_CHECK(ret, name, params, args, call, writes, fmt, ...)        \
    SHADOW_DECLARE(ret, name, params)                                               \
    static ret check_##name params {                                                \
        struct hook_writes w_ = {0};                                                \
        writes;                                                                     \
        hooks_writes_begin(&w_);                                                    \
        hooks_nested++;                                                             \
        SHADOW_RESULT_##ret(want) call;                                             \
        hooks_nested--;                                                             \
        hooks_writes_middle(&w_);                                                   \
        SHADOW_RESULT_##ret(got) name args;                                         \
        const char *diff_ = hooks_writes_end(&w_, SHADOW_INT_##ret(got), SHADOW_INT_##ret(want)); \
        if (diff_) hooks_mismatch(&stats_##name, "(" fmt ") %s", __VA_ARGS__, diff_);  \
        hooks_ui_release();                                                         \
        SHADOW_RETURN_##ret(want);                                                  \
    }

// How SHADOW_WRITER keeps a result, by return type.
#define SHADOW_RESULT_int(v) int v =
#define SHADOW_RESULT_void(v)
#define SHADOW_INT_int(v) v
#define SHADOW_INT_void(v) 0
#define SHADOW_RETURN_int(v) return v
#define SHADOW_RETURN_void(v) return
#define SHADOW_TAIL_int(call) return call
#define SHADOW_TAIL_void(call) \
    do {                       \
        call;                  \
        return;                \
    } while (0)

#define SHADOW_WRITER_ENTRY(module, ret, name, params, args) \
    static ret shadow_##name params {                        \
        if (hooks_nested) SHADOW_TAIL_##ret(orig_##name args); \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);     \
        stats_##name.calls++;                                \
        shadow_tick(module);                                 \
        SHADOW_TAIL_##ret(check_##name args);                \
    }

// The same for a void(void) function whose original reads stale stack (see
// hook_stale). shadow_name is an assembly entry that keeps the bytes below
// the game's call, and every path runs the function with them. check_name
// takes them, so a sweep can make them up.
#define SHADOW_WRITER_STALE(module, name, writes)                                \
    SHADOW_DECLARE(void, name, (void))                                           \
    static void check_##name(const struct hook_stale *s) {                       \
        struct hook_writes w_ = {0};                                             \
        writes;                                                                  \
        hooks_writes_begin(&w_);                                                 \
        hooks_nested++;                                                          \
        hooks_stale_call(orig_##name, s);                                        \
        hooks_nested--;                                                          \
        hooks_writes_middle(&w_);                                                \
        hooks_stale_call(name, s);                                               \
        const char *diff_ = hooks_writes_end(&w_, 0, 0);                         \
        if (diff_) hooks_mismatch(&stats_##name, "() %s", diff_);                \
        hooks_ui_release();                                                      \
    }                                                                            \
    void shadow_##name##_in(const struct hook_stale *s);                         \
    void shadow_##name##_in(const struct hook_stale *s) {                        \
        if (hooks_nested) {                                                      \
            hooks_stale_call(orig_##name, s);                                    \
            return;                                                              \
        }                                                                        \
        if (shadow_bypass()) {                                                   \
            hooks_stale_call(name, s);                                           \
            return;                                                              \
        }                                                                        \
        stats_##name.calls++;                                                    \
        shadow_tick(module);                                                     \
        check_##name(s);                                                         \
    }                                                                            \
    void shadow_##name(void);                                                    \
    __asm__(".text\n"                                                            \
            ".globl shadow_" #name "\n"                                          \
            "shadow_" #name ":\n"                                                \
            "    subl $0x100, %esp\n" /* HOOK_STALE_SIZE: the bytes stay put */ \
            "    pushl %esp\n"                                                   \
            "    call shadow_" #name "_in\n"                                     \
            "    addl $0x104, %esp\n"                                            \
            "    ret\n");

// The same for methods, which the game calls with `this` in ecx: ours, orig_
// and shadow_ are thiscall, and the first parameter is the object. The
// callee pops its stack arguments, as the original does.
#define THISCALL __attribute__((thiscall))
#define SHADOW_DECLARE_T(ret, name, params)   \
    static ret(THISCALL *orig_##name) params; \
    static struct hook_stats stats_##name = {#name};

#define SHADOW_ENTRY_T(module, ret, name, params, args) \
    static ret THISCALL shadow_##name params {          \
        if (hooks_nested) return orig_##name args;      \
        if (shadow_bypass()) return name args;          \
        stats_##name.calls++;                           \
        shadow_tick(module);                            \
        return check_##name args;                       \
    }

// A method that returns a value and has no side effects.
#define SHADOW_THIS_FN(module, ret, name, params, args, fmt, ...) \
    SHADOW_DECLARE_T(ret, name, params)                           \
    SHADOW_CHECK(ret, name, params, args, fmt, __VA_ARGS__)       \
    SHADOW_ENTRY_T(module, ret, name, params, args)

// A method that writes: the check, for a function whose orig_ and stats_ are
// declared (SHADOW_DECLARE_T) before ours, and whose shadow_ is its own.
#define SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, ...)                 \
    static ret check_##name params {                                                  \
        struct hook_writes w_ = {0};                                                  \
        writes;                                                                       \
        hooks_writes_begin(&w_);                                                      \
        hooks_nested++;                                                               \
        SHADOW_RESULT_##ret(want) orig_##name args;                                   \
        hooks_nested--;                                                               \
        hooks_writes_middle(&w_);                                                     \
        SHADOW_RESULT_##ret(got) name args;                                           \
        const char *diff_ = hooks_writes_end(&w_, SHADOW_INT_##ret(got), SHADOW_INT_##ret(want)); \
        if (diff_) hooks_mismatch(&stats_##name, "(" fmt ") %s", __VA_ARGS__, diff_);    \
        hooks_ui_release();                                                           \
        SHADOW_RETURN_##ret(want);                                                    \
    }

#define SHADOW_WRITER_T(module, ret, name, params, args, writes, fmt, ...) \
    SHADOW_DECLARE_T(ret, name, params)                                    \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__)  \
    SHADOW_ENTRY_T(module, ret, name, params, args)

// A hook table entry: the original's address, our function, and the first
// bytes of the original (whole instructions, no relative jumps).
#define HOOK(addr, name, ...)                                                                   \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, \
     (void *)shadow_##name, (void **)&orig_##name, &stats_##name}

#define HOOK_COUNT(hooks) ((int)(sizeof(hooks) / sizeof((hooks)[0])))
