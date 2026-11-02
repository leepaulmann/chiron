#define _GNU_SOURCE
#include "thunks.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "modules.h"

struct import_rec {
    char label[96];  // "DLL!name" or "DLL!#ordinal"
    unsigned hits;
    struct import_rec *next;
};

static struct import_rec *traced;
static uint8_t *arena, *arena_end;

static uint8_t *code_alloc(size_t n) {
    if (!arena || arena + n > arena_end) {
        arena = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED) abort();
        arena_end = arena + 0x10000;
    }
    uint8_t *p = arena;
    arena += (n + 15) & ~15u;
    return p;
}

static struct import_rec *new_rec(const char *dll, const char *name, int ordinal) {
    struct import_rec *r = calloc(1, sizeof(*r));
    if (name)
        snprintf(r->label, sizeof(r->label), "%s!%s", dll, name);
    else
        snprintf(r->label, sizeof(r->label), "%s!#%d", dll, ordinal);
    return r;
}

static void emit_rel32(uint8_t *at, uint8_t opcode, void *target) {
    at[0] = opcode;
    *(int32_t *)(at + 1) = (int32_t)((uint8_t *)target - (at + 5));
}

// Stack on entry: [esp] = return into stub, [esp+4] = rec, [esp+8] = caller.
__attribute__((cdecl, noreturn)) static void trap_hit(struct import_rec *rec, uint32_t caller) {
    char where[128];
    modules_describe_addr(caller, where, sizeof(where));
    fprintf(stderr, "chiron: unimplemented import %s called from %s\n", rec->label, where);
    fflush(stderr);
    abort();
}

__attribute__((cdecl)) static void trace_hit(struct import_rec *rec) {
    if (rec->hits++ < 3) fprintf(stderr, "[trace] %s\n", rec->label);
}

void *thunk_trap(const char *dll, const char *name, int ordinal) {
    struct import_rec *rec = new_rec(dll, name, ordinal);
    uint8_t *p = code_alloc(10);
    p[0] = 0x68;  // push imm32
    *(uint32_t *)(p + 1) = (uint32_t)rec;
    emit_rel32(p + 5, 0xe8, (void *)trap_hit);  // call trap_hit
    return p;
}

void *thunk_trace(const char *dll, const char *name, int ordinal, void *target) {
    struct import_rec *rec = new_rec(dll, name, ordinal);
    rec->next = traced;
    traced = rec;
    uint8_t *p = code_alloc(18);
    p[0] = 0x68;  // push imm32
    *(uint32_t *)(p + 1) = (uint32_t)rec;
    emit_rel32(p + 5, 0xe8, (void *)trace_hit);  // call trace_hit
    p[10] = 0x83, p[11] = 0xc4, p[12] = 0x04;    // add esp, 4
    emit_rel32(p + 13, 0xe9, target);            // jmp target
    return p;
}

void thunk_trace_summary(void) {
    if (!traced) return;
    fprintf(stderr, "[trace] call counts:\n");
    for (struct import_rec *r = traced; r; r = r->next)
        if (r->hits) fprintf(stderr, "[trace] %8u %s\n", r->hits, r->label);
}
