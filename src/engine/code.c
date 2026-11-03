#include "code.h"

#include <string.h>
#include <sys/mman.h>

static uint8_t *arena, *arena_end;

uint8_t *code_stub_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (n > 0x1000) return NULL;
    if (!arena || arena + n > arena_end) {
        arena = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED) {
            arena = NULL;
            return NULL;
        }
        arena_end = arena + 0x1000;
    }
    uint8_t *p = arena;
    arena += n;
    return p;
}

void code_put_rel32(uint8_t *at, uint8_t opcode, uint32_t target) {
    at[0] = opcode;
    int32_t rel = (int32_t)(target - ((uint32_t)(uintptr_t)at + 5));
    memcpy(at + 1, &rel, 4);
}

int code_matches(struct pe_image *exe, uint32_t addr, const uint8_t *expect, size_t n) {
    if (addr < exe->pref_base || addr + n > exe->pref_base + exe->size) return 0;
    return !memcmp(code_at(exe, addr), expect, n);
}
