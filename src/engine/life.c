// The twin check of an object's life cycle (see life.h).
#include "life.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quiet.h"

uint8_t life_a[LIFE_MAX] __attribute__((aligned(16))), life_b[LIFE_MAX] __attribute__((aligned(16)));

void life_fill(const struct life_class *c, void *a, void *b, uint8_t fill) {
    memset(a, fill, c->size);
    memset(b, fill, c->size);
}

static uint32_t word(const void *obj, int off) { return *(const uint32_t *)((const uint8_t *)obj + off); }

// Whether two words match: equal, or both pointing the same place in their own object.
static int same_word(uint32_t x, uint32_t y, const void *a, const void *b, size_t size) {
    if (x == y) return 1;
    uint32_t da = x - (uint32_t)(uintptr_t)a, db = y - (uint32_t)(uintptr_t)b;
    return da <= size && da == db;
}

static int was_freed(const struct quiet_heap_log *log, uint32_t p) {
    for (int i = 0; p && i < log->frees && i < 32; i++)
        if (log->freed[i] == p) return 1;
    return 0;
}

static int cmp_u32(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return a < b ? -1 : a > b;
}

// Which of an object's slots held each freed pointer before the run (-1 for none), sorted.
static void freed_slots(const struct life_class *c, const struct quiet_heap_log *log, const uint32_t *before,
                        uint32_t *out) {
    int n = log->frees < 32 ? log->frees : 32;
    for (int i = 0; i < n; i++) {
        out[i] = (uint32_t)-1;
        for (int s = 0; s < c->n_slots; s++)
            if (before[s] && before[s] == log->freed[i]) out[i] = (uint32_t)s;
    }
    qsort(out, (size_t)n, sizeof out[0], cmp_u32);
}

const char *life_twin(const struct life_class *c, void *a, void *b, life_fn orig, life_fn ours, void *arg,
                      uint32_t *const *globals, int n_globals) {
    static char diff[200];
    uint32_t before_a[32] = {0}, before_b[32] = {0}, saved[32], after_a[32], after_b[32];
    static struct quiet_heap_log log_a, log_b;
    for (int s = 0; s < c->n_slots && s < 32; s++) before_a[s] = word(a, c->slots[s].off), before_b[s] = word(b, c->slots[s].off);
    for (int g = 0; g < n_globals; g++) saved[g] = *globals[g];

    quiet_heap_log(&log_a);
    orig(a, arg);
    quiet_heap_log(NULL);
    for (int g = 0; g < n_globals; g++) after_a[g] = *globals[g], *globals[g] = saved[g];
    quiet_heap_log(&log_b);
    ours(b, arg);
    quiet_heap_log(NULL);
    for (int g = 0; g < n_globals; g++) after_b[g] = *globals[g];

    // Every word that isn't a slot.
    for (size_t o = 0; o + 4 <= c->size; o += 4) {
        int slot = (o >= c->skip_lo && o < c->skip_hi) || (o >= c->skip2_lo && o < c->skip2_hi);
        for (int s = 0; s < c->n_slots; s++) slot |= c->slots[s].off == o;
        if (slot) continue;
        uint32_t x = word(a, (int)o), y = word(b, (int)o);
        if (!same_word(x, y, a, b, c->size)) {
            snprintf(diff, sizeof diff, "%s field 0x%x = 0x%x, original 0x%x", c->name, (unsigned)o, (unsigned)y, (unsigned)x);
            return diff;
        }
    }
    // The slots: whether each was set, and what a block that the run set holds.
    for (int s = 0; s < c->n_slots; s++) {
        int off = c->slots[s].off, len = c->slots[s].len;
        uint32_t x = word(a, off), y = word(b, off);
        // A slot was touched if it changed, or if the block it held was freed (the heap may hand it out again).
        int touched_a = s < 32 && (x != before_a[s] || was_freed(&log_a, before_a[s]));
        int touched_b = s < 32 && (y != before_b[s] || was_freed(&log_b, before_b[s]));
        if (!x != !y || touched_a != touched_b) {
            snprintf(diff, sizeof diff, "%s slot 0x%x is %s, original %s", c->name, off, y ? "set" : "empty", x ? "set" : "empty");
            return diff;
        }
        if (len < 0 || !x || !touched_a) continue;
        if (len == 0 ? strcmp((const char *)(uintptr_t)x, (const char *)(uintptr_t)y) != 0
                     : memcmp((const void *)(uintptr_t)x, (const void *)(uintptr_t)y, (size_t)len) != 0) {
            snprintf(diff, sizeof diff, "%s block at 0x%x holds other bytes than the original's", c->name, off);
            return diff;
        }
    }
    for (int g = 0; g < n_globals; g++)
        if (!same_word(after_a[g], after_b[g], a, b, c->size)) {
            snprintf(diff, sizeof diff, "%s global %d = 0x%x, original 0x%x", c->name, g, (unsigned)after_b[g], (unsigned)after_a[g]);
            return diff;
        }
    // The heap: what each run allocated, and which of the object's blocks it freed.
    if (log_a.mallocs != log_b.mallocs) {
        snprintf(diff, sizeof diff, "%s made %d allocations, original %d", c->name, log_b.mallocs, log_a.mallocs);
        return diff;
    }
    if (log_a.frees != log_b.frees) {
        snprintf(diff, sizeof diff, "%s freed %d blocks, original %d", c->name, log_b.frees, log_a.frees);
        return diff;
    }
    int nm = log_a.mallocs < 32 ? log_a.mallocs : 32;
    qsort(log_a.malloc_size, (size_t)nm, sizeof(uint32_t), cmp_u32);
    qsort(log_b.malloc_size, (size_t)nm, sizeof(uint32_t), cmp_u32);
    if (memcmp(log_a.malloc_size, log_b.malloc_size, (size_t)nm * 4)) {
        snprintf(diff, sizeof diff, "%s allocated blocks of other sizes than the original's", c->name);
        return diff;
    }
    uint32_t fa[32], fb[32];
    freed_slots(c, &log_a, before_a, fa);
    freed_slots(c, &log_b, before_b, fb);
    int nf = log_a.frees < 32 ? log_a.frees : 32;
    if (memcmp(fa, fb, (size_t)nf * 4)) {
        snprintf(diff, sizeof diff, "%s freed other blocks than the original did", c->name);
        return diff;
    }
    return NULL;
}

#include "game.h"

void life_apply(void *base, const struct life_set *t, int n) {
    for (int i = 0; i < n; i++) {
        uint32_t v = 0;
        switch (t[i].kind) {
        case LIFE_CONST: v = t[i].v; break;
        case LIFE_GLOBAL: v = *(uint32_t *)game_at(t[i].v); break;
        case LIFE_ADDR: v = game_va(t[i].v); break;
        case LIFE_SELF: v = (uint32_t)(uintptr_t)base; break;
        }
        *(uint32_t *)((uint8_t *)base + t[i].off) = v;
    }
}

void life_build(const struct life_class *c, uint8_t fill, life_fn ctor, void *arg) {
    // The Dialog family's constructors save the game's list head (0x9b3374) in the object and clear it: put it
    // back between the two, so that both objects save the same.
    uint32_t *head = game_at(0x9b3374);
    uint32_t saved = *head;
    life_fill(c, life_a, life_b, fill);
    ctor(life_a, arg);
    *head = saved;
    ctor(life_b, arg);
    for (int i = 0; i < 2; i++)
        if (c->scribble[i]) *(uint32_t *)(life_a + c->scribble[i]) = *(uint32_t *)(life_b + c->scribble[i]) = 0x13572468u;
}

void life_buffers(int off, int w, int h) {
    if (*game_ddraw) return;
    game_buffer_init(life_a + off, w, h, 0, 0);
    game_buffer_init(life_b + off, w, h, 0, 0);
}

void life_text(int off, const char *text) {
    uint8_t *objs[2] = {life_a, life_b};
    for (int i = 0; i < 2; i++) {
        char *copy = game_mem_get(strlen(text) + 1);
        strcpy(copy, text);
        *(char **)(objs[i] + off) = copy;
    }
}

static void __attribute__((thiscall)) fake_delete_block(void *o, int flag) { game_free(o); }
static void *fake_block_vtable[2] = {(void *)fake_delete_block, NULL};

void life_objects(int off, int n) {
    uint8_t *objs[2] = {life_a, life_b};
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < n; i++) {
            void **o = game_mem_get(16);
            memset(o, 0, 16);
            o[0] = fake_block_vtable;
            *(void ***)(objs[k] + off + 4 * i) = o;
        }
}

void life_texts(int off, int n, const char *text) {
    for (int i = 0; i < n; i++) life_text(off + 4 * i, text);
}

void life_nodes(int off, int n) {
    uint8_t *objs[2] = {life_a, life_b};
    for (int k = 0; k < 2; k++) {
        struct game_strlist *l = (struct game_strlist *)(objs[k] + off);
        struct game_strnode *first = NULL, *prev = NULL;
        for (int i = 0; i < n; i++) {
            struct game_strnode *nd = game_mem_get(sizeof *nd);
            memset(nd, 0, sizeof *nd);
            nd->pad0 = (int32_t)(uintptr_t)fake_block_vtable;
            void **item = game_mem_get(16);
            memset(item, 0, 16);
            item[0] = fake_block_vtable;
            nd->item = item, nd->id = i + 1;
            if (!first) first = nd;
            if (prev) prev->next = nd, nd->prev = prev;
            prev = nd;
        }
        if (first) first->prev = prev, prev->next = first;
        l->head = first, l->cur = first, l->count = n, l->idx = 0;
    }
}

void life_data(int off, int size) {
    game_dialog_data_alloc(life_a + off + 4, size);
    game_dialog_data_alloc(life_b + off + 4, size);
}

void life_vb_round(const struct life_class *c, int win, int dlg, struct hook_stats *s_ctor, life_fn ctor_o, life_fn ctor_n,
                   struct hook_stats *s_close, life_fn close_o, life_fn close_n, int round, uint8_t fill, void (*extra)(int round)) {
    uint32_t *head_global[1] = {(uint32_t *)game_at(0x9b3374)};
    life_fill(c, life_a, life_b, fill);
    if (life_run(s_ctor, c, ctor_o, ctor_n, NULL, head_global, 1)) life_buffer_check_at(s_ctor, win + 0x444);
    life_build(c, fill, ctor_o, NULL);
    life_buffers(win + 0x444, 4 + round, 3 + round);
    life_nodes(dlg + 0xbc, round);
    if (round & 1) life_data(dlg, 64);
    if (extra) extra(round);
    if (life_run(s_close, c, close_o, close_n, NULL, head_global, 1)) life_buffer_check_at(s_close, win + 0x444);
    for (int i = 0; i < 2; i++) {
        uint8_t *o = i ? life_b : life_a;
        game_dialog_dtor((struct game_dialog *)(o + dlg));
        game_graphic_win_dtor((struct game_win *)(o + win));
    }
}
