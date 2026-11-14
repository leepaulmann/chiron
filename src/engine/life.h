// Twin checks of an object's life cycle: constructors, destructors, init, close and the setters
// that allocate (sweeps only; see life.c).
//
// These functions allocate from the game's heap or free to it, which a check can't rewind, so
// they are not checked like other writers. A sweep builds two objects of the same class from the
// same bytes, runs the original on one and Chiron's code on the other, and compares what they
// hold: every word, with the words that hold heap pointers or handles compared by whether they
// are set, and the blocks behind them by content; the sizes of the blocks each run allocated and
// which of the object's blocks each run freed (the game's malloc and free are logged); and the
// game's globals that the function may move (a list head, a focus pointer).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bufobj.h"
#include "game.h"
#include "hooks.h"

// A word of an object that holds a pointer to a block of the game's heap, or a handle: the two
// objects' differ, so only whether each is set is compared (len < 0), and for a block also what
// it holds, as a string (len 0) or as len bytes.
struct life_slot {
    uint16_t off;
    int16_t len;
};

// A class: its size and the slots of its objects that hold such pointers.
struct life_class {
    const char *name;
    size_t size;
    const struct life_slot *slots;
    int n_slots;
    uint16_t skip_lo, skip_hi;  // a part the word compare leaves out (a Buffer inside, compared on its own), or 0, 0
    uint16_t scribble[2];       // words a close or destructor resets: made-up values are put there before it runs (0: none)
    uint16_t skip2_lo, skip2_hi;  // a second part the word compare leaves out (another Buffer inside), or 0, 0
};

// The two objects every check builds in (in static memory, big enough for any widget).
enum { LIFE_MAX = 0x3240 };
extern uint8_t life_a[LIFE_MAX], life_b[LIFE_MAX];

typedef void (*life_fn)(void *obj, void *arg);

// Fill both objects with the same bytes, `fill` repeated (an object the constructor sets right
// ends up alike whatever it started with; one it leaves bytes of shows).
void life_fill(const struct life_class *c, void *a, void *b, uint8_t fill);

// Run `orig` on `a` and `ours` on `b` (with `arg`), then compare the two objects, the allocations
// and frees of each, and the `n_globals` words `globals` points to (set back to what they were
// before between the two runs, and compared after each). Returns NULL, or a line saying what
// differs first. A global that holds a pointer into the object (a list head) is compared by
// where in the object it points.
const char *life_twin(const struct life_class *c, void *a, void *b, life_fn orig, life_fn ours, void *arg,
                      uint32_t *const *globals, int n_globals);

// A constructor or a close that stores constants and the game's globals into an object, as a table:
// the word at `off` gets zero (LZ), a constant (LC), the value of the game's global at an address
// (LG), the address in the game's image itself (LV, for a vtable or a function) or the object's own
// address (LS). Run in order; `base` is the object's address.
struct life_set {
    uint16_t off;
    uint8_t kind;
    uint32_t v;
};
enum { LIFE_ZERO, LIFE_CONST, LIFE_GLOBAL, LIFE_ADDR, LIFE_SELF };
#define LZ(off) {off, LIFE_ZERO, 0}
#define LC(off, v) {off, LIFE_CONST, v}
#define LG(off, addr) {off, LIFE_GLOBAL, addr}
#define LV(off, addr) {off, LIFE_ADDR, addr}
#define LS(off) {off, LIFE_SELF, 0}
void life_apply(void *base, const struct life_set *t, int n);

// The two functions of a check as life_fn: LIFE_FN(name, original's call, our call) defines
// life_orig_name and life_ours_name, whose object is `o` and argument `x`.
#define LIFE_FN(name, orig_call, ours_call)                    \
    static void life_orig_##name(void *o, void *x) { (void)x; orig_call; } \
    static void life_ours_##name(void *o, void *x) { (void)x; ours_call; }

// One twin check on life_a and life_b: reports a difference to the function's statistics; returns
// whether they matched.
static inline int life_run(struct hook_stats *s, const struct life_class *c, life_fn orig, life_fn ours, void *arg,
                           uint32_t *const *globals, int n_globals) {
    const char *d = life_twin(c, life_a, life_b, orig, ours, arg, globals, n_globals);
    if (d) hooks_mismatch(s, ": %s", d);
    return d == NULL;
}

// Both objects made alike: filled with `fill`, then `ctor` (the original's, as a life_fn) run on each.
void life_build(const struct life_class *c, uint8_t fill, life_fn ctor, void *arg);
// Give both objects a Buffer of their own at `off` (the original's set-up: a DIB and a device context),
// or do nothing if the game has a DirectDraw object (a sweep never has).
void life_buffers(int off, int w, int h);
// Give each object, at `off`, a copy of the text in a block of the game's heap of its own.
void life_text(int off, const char *text);

// The common steps of a sweep over a GraphicWin-based class (its Buffer at 0x444, compared as a Buffer
// after each check).
static inline void life_buffer_check_at(struct hook_stats *s, int off) {
    const char *d = buffer_twin_diff((struct game_buffer *)(life_a + off), (struct game_buffer *)(life_b + off), 0, 0);
    if (d) hooks_mismatch(s, " (Buffer): %s", d);
}
static inline void life_buffer_check(struct hook_stats *s) { life_buffer_check_at(s, 0x444); }

// The value of entry `i` of the virtual base table an object with virtual bases keeps in its first word
// (entry 1 is where the first base, a GraphicWin, lies; entry 2 the second, a Dialog).
static inline int32_t life_vb(const void *o, int i) { return ((const int32_t *)(uintptr_t)*(const uint32_t *)o)[i]; }

// A constructor on twins that start as garbage (`fill` repeated).
static inline void life_ctor_check(struct hook_stats *s, const struct life_class *c, life_fn orig, life_fn ours, uint8_t fill) {
    static uint32_t *none[1];
    life_fill(c, life_a, life_b, fill);
    if (life_run(s, c, orig, ours, NULL, none, 0)) life_buffer_check(s);
}

// A close or a destructor on twins that the original's constructor made, each with a Buffer of its own
// (`w` by `h`) and, if `texts`, a name at 0xa7c and a bubble text at 0xa80 (a button's).
static inline void life_close_check(struct hook_stats *s, const struct life_class *c, life_fn ctor, life_fn orig, life_fn ours,
                                    uint8_t fill, int w, int h, int texts) {
    static uint32_t *none[1];
    life_build(c, fill, ctor, NULL);
    life_buffers(0x444, w, h);
    if (texts) life_text(0xa7c, "a name"), life_text(0xa80, "some bubble text");
    if (life_run(s, c, orig, ours, NULL, none, 0)) life_buffer_check(s);
}

// Both objects get, in each of the `n` words from `off` on, a block of the game's heap of its own that is an object with a
// stand-in vtable (deleting it through slot 0 frees the block), or a copy of a text.
void life_objects(int off, int n);
void life_texts(int off, int n, const char *text);

// Both objects get, for the string list at `off` (as a Dialog keeps its items: 0xbc into the Dialog), a ring of
// `n` nodes each holding an item. A node and an item are blocks of the game's heap, and a stand-in vtable
// (deleting one frees the block) makes the list's own walk free them.
void life_nodes(int off, int n);
// Both Dialogs (at `off`) get data of `size` bytes from the game's arena, which closing them gives back.
void life_data(int off, int size);

// A scalar deleting destructor: runs the destructor and, if bit 0 of the flag is set, frees the object (with the game's free); answers the
// object. (A sweep gives it flag 0: its objects are in static memory.)
#define LIFE_SCALAR(name, type, dtor_fn)                         \
    void *__attribute__((thiscall)) name(type *o, int flag) {    \
        dtor_fn(o);                                              \
        if (flag & 1) game_free(o);                              \
        return o;                                                \
    }

// A class with two virtual bases, a GraphicWin at `win` and a Dialog at `dlg`: one round of its sweep. The constructor on garbage (`ctor_o`
// and `ctor_n`, taking the object and answering with it as the original's does, run as most-derived), then a `close_o`/`close_n` check on
// objects the original made, each with a Buffer, `round` list items and, on odd rounds, data; `extra` (if any) fills in what the class
// holds besides. The bases the close leaves are unmade by the original's own functions. `globals` are compared too.
void life_vb_round(const struct life_class *c, int win, int dlg, struct hook_stats *s_ctor, life_fn ctor_o, life_fn ctor_n,
                   struct hook_stats *s_close, life_fn close_o, life_fn close_n, int round, uint8_t fill, void (*extra)(int round));
