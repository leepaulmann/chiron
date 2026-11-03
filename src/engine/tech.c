// Technology queries: whether a faction has or can research a tech, a tech's
// level and category, and its prerequisites. Reimplemented from terranx.exe
// 2.0, with behaviour as documented by OpenSMACX (Brendan Casey,
// GPL-3.0-or-later), whose names we use.
#include "tech.h"

#include "game.h"
#include "hooks.h"

static const struct rules_tech *tech_entry(uint32_t tech) { return &game_tech[tech]; }

// Whether `faction` has `tech`. "No tech" is always had; disabled techs, and
// Transcendent Thought, never are. Faction 0 (the natives) has nothing.
int has_tech(int tech, int faction) {
    if (faction <= 0) return 0;
    if (tech == TECH_NONE) return 1;
    if (tech < 0 || tech >= TECH_TRANSCEND) return 0;
    const struct rules_tech *t = tech_entry(tech);
    if (t->preq1 == TECH_DISABLED || (t->preq2 == TECH_DISABLED && t->preq1 != TECH_NONE)) return 0;
    // As on x86, the shift count wraps at 32.
    return (game_tech_achieved[tech] & (1u << (faction & 31))) != 0;
}

// The tech's level: `level` plus the length of its longest prerequisite chain.
int tech_recurse(int tech, int level) {
    if (tech < 0 || tech >= TECH_COUNT) return level;
    const struct rules_tech *t = tech_entry(tech);
    int a = tech_recurse(t->preq1, level + 1), b = tech_recurse(t->preq2, level + 1);
    return a > b ? a : b;
}

// The tech's main purpose: growth (0), tech (1), wealth (2) or power (3).
// Ties go to the earlier one. The original does not check the range.
int tech_category(uint32_t tech) {
    const struct rules_tech *t = tech_entry(tech);
    int category = 0, best = t->growth;
    if (t->tech > best) category = 1, best = t->tech;
    if (t->wealth > best) category = 2, best = t->wealth;
    return t->power > best ? 3 : category;
}

// Whether `faction` can research `tech` now: it doesn't have it, it has both
// prerequisites, and the tech exists in the game being played.
int tech_avail(uint32_t tech, int faction) {
    if (has_tech((int)tech, faction) || tech >= TECH_COUNT) return 0;
    if (!*game_expansion && tech >= TECH_FIRST_SMACX && tech <= TECH_LAST_SMACX) return 0;
    const struct rules_tech *t = tech_entry(tech);
    if (t->preq1 < TECH_NONE || t->preq2 < TECH_NONE) return 0;
    return has_tech(t->preq1, faction) && has_tech(t->preq2, faction);
}

// Whether `preq` is `parent` or one of its prerequisites, at most `range`
// steps back.
int tech_is_preq(int preq, int parent, uint32_t range) {
    if (preq < 0 || parent < 0) return 0;
    if (preq == parent) return 1;
    if (!range) return 0;
    const struct rules_tech *t = tech_entry(parent);
    return tech_is_preq(preq, t->preq1, range - 1) || tech_is_preq(preq, t->preq2, range - 1);
}

// The tech's value for each purpose. Past the end of the table tech_mil gives
// 0 and the others 1, as in the original. The original compares signed, so a
// negative tech reads whatever lies before the table; the game never asks.
int tech_mil(int tech) { return tech >= TECH_COUNT ? 0 : game_tech[tech].power; }
int tech_tech(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].tech; }
int tech_infra(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].wealth; }
int tech_colonize(int tech) { return tech >= TECH_COUNT ? 1 : game_tech[tech].growth; }

// ---- Hooks and shadow checks ------------------------------------------------

// For each function: a pointer to the original, its statistics, a check that
// runs both and compares, and the shadow entry point the game calls.
#define SHADOW_ENTRY(name, params, call_args)                \
    static int shadow_##name params {                        \
        if (hooks_nested) return orig_##name call_args;      \
        stats_##name.calls++;                                \
        hooks_shadow_tick();                                 \
        return check_##name call_args;                       \
    }

#define DECLARE(name, params)                       \
    static int (*orig_##name) params;               \
    static struct hook_stats stats_##name = {#name};

#define SHADOW1(name, t1)                                       \
    DECLARE(name, (t1))                                         \
    static int check_##name(t1 a) {                             \
        hooks_nested++;                                         \
        int want = orig_##name(a);                              \
        hooks_nested--;                                         \
        int got = name(a);                                      \
        if (got != want) hooks_mismatch(&stats_##name, "(%d) = %d, original %d", (int)a, got, want); \
        return want;                                            \
    }                                                           \
    SHADOW_ENTRY(name, (t1 a), (a))

#define SHADOW2(name, t1, t2)                                   \
    DECLARE(name, (t1, t2))                                     \
    static int check_##name(t1 a, t2 b) {                       \
        hooks_nested++;                                         \
        int want = orig_##name(a, b);                           \
        hooks_nested--;                                         \
        int got = name(a, b);                                   \
        if (got != want) hooks_mismatch(&stats_##name, "(%d, %d) = %d, original %d", (int)a, (int)b, got, want); \
        return want;                                            \
    }                                                           \
    SHADOW_ENTRY(name, (t1 a, t2 b), (a, b))

#define SHADOW3(name, t1, t2, t3)                               \
    DECLARE(name, (t1, t2, t3))                                 \
    static int check_##name(t1 a, t2 b, t3 c) {                 \
        hooks_nested++;                                         \
        int want = orig_##name(a, b, c);                        \
        hooks_nested--;                                         \
        int got = name(a, b, c);                                \
        if (got != want) hooks_mismatch(&stats_##name, "(%d, %d, %d) = %d, original %d", (int)a, (int)b, (int)c, got, want); \
        return want;                                            \
    }                                                           \
    SHADOW_ENTRY(name, (t1 a, t2 b, t3 c), (a, b, c))

SHADOW2(has_tech, int, int)
SHADOW2(tech_recurse, int, int)
SHADOW1(tech_category, uint32_t)
SHADOW2(tech_avail, uint32_t, int)
SHADOW3(tech_is_preq, int, int, uint32_t)
SHADOW1(tech_mil, int)
SHADOW1(tech_tech, int)
SHADOW1(tech_infra, int)
SHADOW1(tech_colonize, int)

// Input sweeps, run once a turn in shadow mode: every tech, a little beyond
// the table at both ends, against every faction and a few out of range.
static void sweep_has_tech(void) {
    for (int t = -3; t <= 100; t++)
        for (int f = -1; f <= 8; f++) check_has_tech(t, f);
}
static void sweep_tech_recurse(void) {
    for (int t = -3; t <= 100; t++)
        for (int l = 0; l <= 2; l++) check_tech_recurse(t, l);
}
static void sweep_tech_category(void) {
    for (uint32_t t = 0; t < TECH_COUNT; t++) check_tech_category(t);  // unchecked range in the original
}
static void sweep_tech_avail(void) {
    for (int t = -3; t <= 100; t++)
        for (int f = -1; f <= 8; f++) check_tech_avail((uint32_t)t, f);
}
static void sweep_tech_is_preq(void) {
    for (int p = -1; p < TECH_COUNT; p++)
        for (int q = -1; q < TECH_COUNT; q++)
            for (uint32_t r = 0; r <= 3; r++) check_tech_is_preq(p, q, r);
}
#define SWEEP_VALUE(name) \
    static void sweep_##name(void) { for (int t = -3; t <= 100; t++) check_##name(t); }
SWEEP_VALUE(tech_mil)
SWEEP_VALUE(tech_tech)
SWEEP_VALUE(tech_infra)
SWEEP_VALUE(tech_colonize)

void tech_sweep(void) {
    sweep_has_tech();
    sweep_tech_recurse();
    sweep_tech_category();
    sweep_tech_avail();
    sweep_tech_is_preq();
    sweep_tech_mil();
    sweep_tech_tech();
    sweep_tech_infra();
    sweep_tech_colonize();
}

#define HOOK(addr, name, ...)                                                                   \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, \
     (void *)shadow_##name, (void **)&orig_##name, &stats_##name}

// The first bytes of each original: whole instructions, no relative jumps.
const struct hook tech_hooks[] = {
    HOOK(0x5b9f20, has_tech, 0x55, 0x8b, 0xec, 0x56, 0x57),                       // push ebp; mov ebp, esp; push esi; push edi
    HOOK(0x5b9f90, tech_recurse, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),             // ...; mov eax, [ebp+8]
    HOOK(0x5b9fe0, tech_category, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x08),            // ...; mov ecx, [ebp+8]
    HOOK(0x5bac20, tech_avail, 0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c),               // ...; mov ecx, [ebp+12]
    HOOK(0x5bcb60, tech_is_preq, 0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08),       // ...; push ebx; mov ebx, [ebp+8]
    HOOK(0x5bdd70, tech_mil, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bdd90, tech_tech, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bddc0, tech_infra, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
    HOOK(0x5bddf0, tech_colonize, 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08),
};
const int tech_hook_count = sizeof(tech_hooks) / sizeof(tech_hooks[0]);
