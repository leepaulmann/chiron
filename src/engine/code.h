// Helpers for changing terranx.exe's code in memory: checking what is there,
// writing jumps, and allocating executable stubs. Addresses are the game's
// own (at its preferred base); the helpers account for where it is mapped.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../loader/pe.h"

// Where the game's address `addr` is in memory.
static inline uint8_t *code_at(struct pe_image *exe, uint32_t addr) { return exe->base + (addr - exe->pref_base); }

// Whether the n bytes at the game's address `addr` are `expect`.
int code_matches(struct pe_image *exe, uint32_t addr, const uint8_t *expect, size_t n);

// Write a 5-byte jump or call (opcode 0xe9 or 0xe8) at `at` to `target`.
void code_put_rel32(uint8_t *at, uint8_t opcode, uint32_t target);

// Executable memory for stubs, never freed. NULL if none is left.
uint8_t *code_stub_alloc(size_t n);
