#include "hooks.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "code.h"
#include "game.h"

enum hook_mode hooks_mode = HOOKS_OFF;
int hooks_nested;

// Each module lists its hooks.
extern const struct hook tech_hooks[];
extern const int tech_hook_count;
void tech_sweep(void);

static const struct {
    const char *name;
    const struct hook *hooks;
    const int *count;
    void (*sweep)(void);  // shadow mode: check every function over its inputs
} modules[] = {
    {"technology", tech_hooks, &tech_hook_count, tech_sweep},
};

#define N_MODULES (sizeof(modules) / sizeof(modules[0]))

static double sweep_ms_total[N_MODULES], sweep_ms_max[N_MODULES];

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

enum hook_mode hooks_mode_from_env(void) {
    const char *v = getenv("CHIRON_REIMPL");
    if (!v || !*v || !strcasecmp(v, "on")) return HOOKS_ON;
    if (!strcasecmp(v, "off")) return HOOKS_OFF;
    if (!strcasecmp(v, "shadow")) return HOOKS_SHADOW;
    fprintf(stderr, "chiron: CHIRON_REIMPL=%s not understood; use on, off or shadow\n", v);
    return HOOKS_ON;
}

static int install(struct pe_image *exe, const struct hook *h, enum hook_mode mode) {
    if (h->prologue_len < 5 || !code_matches(exe, h->addr, h->prologue, h->prologue_len)) return -1;
    uint8_t *t = code_stub_alloc(h->prologue_len + 5);
    if (!t) return -1;
    memcpy(t, h->prologue, h->prologue_len);
    code_put_rel32(t + h->prologue_len, 0xe9, (uint32_t)(uintptr_t)code_at(exe, h->addr + h->prologue_len));
    *h->original = t;
    void *target = mode == HOOKS_SHADOW ? h->shadow : h->ours;
    code_put_rel32(code_at(exe, h->addr), 0xe9, (uint32_t)(uintptr_t)target);
    return 0;
}

static void on_signal(int sig) {
    hooks_summary();
    signal(sig, SIG_DFL);
    raise(sig);
}

void hooks_apply(struct pe_image *exe, enum hook_mode mode) {
    hooks_mode = mode;
    if (mode == HOOKS_OFF) return;
    game_bind(exe);
    int installed = 0, total = 0;
    for (size_t m = 0; m < N_MODULES; m++)
        for (int i = 0; i < *modules[m].count; i++) {
            const struct hook *h = &modules[m].hooks[i];
            total++;
            if (install(exe, h, mode) == 0) installed++;
            else fprintf(stderr, "chiron: %s (0x%x) not replaced: unexpected code\n", h->stats->name, h->addr);
        }
    fprintf(stderr, "chiron: %d of %d game functions reimplemented%s\n", installed, total,
            mode == HOOKS_SHADOW ? ", shadowing the originals" : "");
    if (mode == HOOKS_SHADOW) {
        atexit(hooks_summary);
        signal(SIGTERM, on_signal);
        signal(SIGINT, on_signal);
    }
}

void hooks_shadow_tick(void) {
    static int32_t last_turn = -1;
    if (hooks_nested || *game_turn == last_turn) return;
    last_turn = *game_turn;
    for (size_t m = 0; m < N_MODULES; m++) {
        for (int i = 0; i < *modules[m].count; i++) modules[m].hooks[i].stats->sweeps++;
        double start = now_ms();
        modules[m].sweep();
        double ms = now_ms() - start;
        sweep_ms_total[m] += ms;
        if (ms > sweep_ms_max[m]) sweep_ms_max[m] = ms;
    }
}

void hooks_mismatch(struct hook_stats *s, const char *fmt, ...) {
    if (++s->mismatches > 10) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "chiron: shadow mismatch in %s", s->name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void hooks_summary(void) {
    static int done;
    if (hooks_mode != HOOKS_SHADOW || done) return;
    done = 1;
    fprintf(stderr, "chiron: shadow summary (function, calls from the game, mismatches, sweeps)\n");
    for (size_t m = 0; m < N_MODULES; m++) {
        unsigned sweeps = 0;
        for (int i = 0; i < *modules[m].count; i++) {
            const struct hook_stats *s = modules[m].hooks[i].stats;
            fprintf(stderr, "  %-16s %12llu %6llu %6u\n", s->name, s->calls, s->mismatches, s->sweeps);
            sweeps = s->sweeps;
        }
        if (sweeps)
            fprintf(stderr, "  (%s sweeps took %.1f ms on average, %.1f at most)\n", modules[m].name,
                    sweep_ms_total[m] / sweeps, sweep_ms_max[m]);
    }
}
