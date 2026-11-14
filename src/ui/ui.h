// What the rebuilt screens (src/ui/<screen>.c) share. A screen is one global object of a class the game builds
// with a static initializer; its functions are replaced as a whole class by one hook module, and none of them has
// a shadow check (the same code runs in on and shadow mode). See "Rebuilding a screen" in CLAUDE.md.
#pragma once

#include <stdint.h>

#include "../engine/hooks.h"

#define UI_THIS __attribute__((thiscall))
#define UI_CDECL __attribute__((cdecl))

// A rectangle as the game keeps them: left, top, right, bottom.
struct ui_rect {
    int32_t left, top, right, bottom;
};

// A hook table entry: the original's address, our function, and the first bytes of the original (whole
// instructions, at least five). There is no check, so the shadow entry is the function itself, and nothing
// calls the original: the trampoline's pointer and the call counts are objects of their own.
#define UI_HOOK(addr, name, ...)                                                                                    \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, (void *)name, \
     &(void *){0}, &(struct hook_stats){#name}}

// The same for a function whose bytes end in a relative call, which is re-aimed in the trampoline.
#define UI_HOOK_CALL(addr, name, ...)                                                                               \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, (void *)name, \
     &(void *){0}, &(struct hook_stats){#name}, 0, 1}

#define UI_HOOK_COUNT(hooks) ((int)(sizeof(hooks) / sizeof((hooks)[0])))

// A call of a virtual function of an object whose vtable is at its start, by slot (a byte offset); the object is
// the first argument, as `this`.
#define UI_VCALL(type, obj, slot, ...) ((type)((*(void ***)(obj))[(slot) / 4]))(obj, ##__VA_ARGS__)
