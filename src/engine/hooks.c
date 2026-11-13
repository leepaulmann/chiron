#include "hooks.h"
#include "journal.h"

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

// Each module defines its hooks and sweep.
extern struct hook_module sprite_module;
extern struct hook_module bufdraw_module;
extern struct hook_module texture_module;
extern struct hook_module bufobj_module;
extern struct hook_module bufimg_module;
extern struct hook_module font_module;
extern struct hook_module win_module;
extern struct hook_module button_module;
extern struct hook_module tech_module, map_module, mapgen_module, base_module, production_module, upkeep_module, social_module,
    faction_module, strategy_module, build_module, veh_module, spot_module, council_module, path_module, score_module, move_module, combat_module, events_module, ai_module, setup_module, probe_module, save_module, rules_module, diplomacy_module, palette_module, buffer_module, text_module, quiet_module;
static struct hook_module *const modules[] = {&tech_module, &map_module, &mapgen_module, &base_module, &production_module,
                                              &upkeep_module, &social_module, &faction_module, &strategy_module, &build_module, &veh_module, &spot_module, &council_module, &path_module, &score_module, &move_module, &combat_module, &events_module, &ai_module, &setup_module, &probe_module, &save_module, &rules_module, &diplomacy_module, &palette_module, &buffer_module, &text_module, &sprite_module, &texture_module, &bufobj_module, &bufimg_module, &bufdraw_module, &font_module, &win_module, &button_module, &quiet_module};

#define N_MODULES (sizeof(modules) / sizeof(modules[0]))

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

static int install(struct pe_image *exe, const struct hook *h, enum hook_mode mode, int skip) {
    if (h->prologue_len < 5 || !code_matches(exe, h->addr, h->prologue, h->prologue_len)) return -1;
    uint8_t *t = code_stub_alloc(h->prologue_len + 5);
    if (!t) return -1;
    memcpy(t, h->prologue, h->prologue_len);
    if (h->ends_in_call) {
        const uint8_t *call = h->prologue + h->prologue_len - 5;
        int32_t rel;
        memcpy(&rel, call + 1, 4);
        uint32_t target = h->addr + h->prologue_len + (uint32_t)rel;
        code_put_rel32(t + h->prologue_len - 5, 0xe8, (uint32_t)(uintptr_t)code_at(exe, target));
    }
    code_put_rel32(t + h->prologue_len, 0xe9, (uint32_t)(uintptr_t)code_at(exe, h->addr + h->prologue_len));
    *h->original = t;
    if (skip) return 1;
    void *target = mode == HOOKS_SHADOW ? h->shadow : h->ours;
    code_put_rel32(code_at(exe, h->addr), 0xe9, (uint32_t)(uintptr_t)target);
    return 0;
}

// Whether a comma-separated list holds a word.
static int listed(const char *list, const char *word) {
    size_t n = strlen(word);
    for (const char *p = list; (p = strstr(p, word)); p += n)
        if ((p == list || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) return 1;
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
    // In shadow mode some Windows calls go through the journal (journal.c); its wrappers
    // must be in the import table before game_bind reads it.
    if (mode == HOOKS_SHADOW) journal_install(exe);
    game_bind(exe);
    // CHIRON_SKIP lists modules (by name) and functions (by name or
    // address) to leave with the original's code, for bisecting: a skipped
    // function's trampoline is still made, so Chiron's own calls to the
    // original keep working, but the game's calls aren't redirected.
    const char *skip = getenv("CHIRON_SKIP");
    int installed = 0, total = 0, skipped = 0;
    for (size_t m = 0; m < N_MODULES; m++)
        for (int i = 0; i < modules[m]->count; i++) {
            const struct hook *h = &modules[m]->hooks[i];
            if (h->shadow_only && mode != HOOKS_SHADOW) continue;
            if (!h->shadow_only) total++;
            char addr[16];
            snprintf(addr, sizeof(addr), "0x%x", h->addr);
            int off = !h->shadow_only && skip &&
                      (listed(skip, modules[m]->name) || listed(skip, h->stats->name) || listed(skip, addr));
            int r = install(exe, h, mode, off);
            if (r == 0) installed += !h->shadow_only;
            else if (r > 0) skipped++;
            else fprintf(stderr, "chiron: %s (0x%x) not replaced: unexpected code\n", h->stats->name, h->addr);
        }
    if (skipped) fprintf(stderr, "chiron: %d functions left with the original (CHIRON_SKIP)\n", skipped);
    fprintf(stderr, "chiron: %d of %d game functions reimplemented%s\n", installed, total,
            mode == HOOKS_SHADOW ? ", shadowing the originals" : "");
    if (mode == HOOKS_SHADOW) {
        atexit(hooks_summary);
        signal(SIGTERM, on_signal);
        signal(SIGINT, on_signal);
    }
}

void hooks_shadow_tick(struct hook_module *m) {
    if (hooks_nested || hooks_sweeping || *game_turn == m->last_turn) return;
    m->last_turn = *game_turn;
    m->sweeps++;
    double start = now_ms();
    const char *outer = hooks_sweep_module;
    hooks_sweep_module = m->name;
    m->sweep();
    hooks_sweep_module = outer;
    double ms = now_ms() - start;
    m->sweep_ms_total += ms;
    if (ms > m->sweep_ms_max) m->sweep_ms_max = ms;
    if (m->also) hooks_shadow_tick(m->also);
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

int hooks_sweeping;
const char *hooks_sweep_module;
void (*hooks_hold_time)(int hold);
void *(*hooks_dc_state)(void *hdc, int *size);
int (*hooks_dc_same)(void *a, void *b);
int (*hooks_font_same)(void *a, void *b);

// Two save buffers per nesting level: the memory before the original ran,
// and after.
enum { WRITES_DEPTH = 8 };
static struct {
    uint8_t *buf[2];
    size_t cap;
    int ui_start, ui_mid;  // the held UI calls before the original ran, and after
} saves[WRITES_DEPTH];
static int writes_depth, checking_depth;

// Held UI calls. ui_n counts them all; only the first UI_MAX are kept.
enum { UI_MAX = 64 };
static struct hook_ui_call ui_log[UI_MAX];
static int ui_n;

void hooks_ui_hold(const struct hook_ui_call *c) {
    if (ui_n < UI_MAX) ui_log[ui_n] = *c;
    ui_n++;
}

void hooks_ui_text(struct hook_ui_call *c, int i, const char *s) {
    if (!s) return;
    c->has_text |= 1u << i;
    snprintf(c->text[i], sizeof(c->text[i]), "%s", s);
}

void hooks_ui_release(void) {
    if (writes_depth) return;
    // Calls made here may start checks of their own, which hold and
    // release theirs, so each release replays from a copy of its own.
    int n = ui_n < UI_MAX ? ui_n : UI_MAX;
    if (!n) {
        ui_n = 0;
        return;
    }
    struct hook_ui_call *pending = malloc((size_t)n * sizeof(*pending));
    if (!pending) abort();
    memcpy(pending, ui_log, (size_t)n * sizeof(*pending));
    ui_n = 0;
    for (int i = 0; i < n; i++) pending[i].replay(&pending[i]);
    free(pending);
}

static int ui_same(const struct hook_ui_call *a, const struct hook_ui_call *b) {
    if (a->name != b->name || memcmp(a->arg, b->arg, sizeof(a->arg)) || a->has_text != b->has_text) return 0;
    for (int i = 0; i < HOOK_UI_TEXTS; i++)
        if ((a->has_text >> i & 1) && strcmp(a->text[i], b->text[i])) return 0;
    return 1;
}

static const char *ui_text(const struct hook_ui_call *c) { return c->has_text & 1 ? c->text[0] : c->has_text & 2 ? c->text[1] : ""; }

// The held calls of the original, [start, mid), against ours, [mid, ui_n).
static const char *ui_compare(int start, int mid, char *diff, size_t size) {
    int want = mid - start, got = ui_n - mid;
    for (int i = 0; i < want && i < got; i++) {
        if (mid + i >= UI_MAX) break;
        const struct hook_ui_call *o = &ui_log[start + i], *c = &ui_log[mid + i];
        if (!ui_same(c, o)) {
            snprintf(diff, size, "call %d to the UI: %s \"%s\" (%d, %d), original %s \"%s\" (%d, %d)", i + 1, c->name,
                     ui_text(c), c->arg[0], c->arg[1], o->name, ui_text(o), o->arg[0], o->arg[1]);
            return diff;
        }
    }
    if (got != want) {
        snprintf(diff, size, "made %d calls to the UI, original %d", got, want);
        return diff;
    }
    return NULL;
}

void hooks_writes_add(struct hook_writes *w, void *p, size_t size) {
    if (w->n >= (int)(sizeof(w->span) / sizeof(w->span[0]))) abort();
    w->span[w->n].p = p;
    w->span[w->n].size = size;
    w->n++;
}

void hooks_writes_dc(struct hook_writes *w, void *hdc) {
    int size;
    void *p = hooks_dc_state ? hooks_dc_state(hdc, &size) : NULL;
    if (p) hooks_writes_add(w, p, size);
}

static size_t writes_total(const struct hook_writes *w) {
    size_t total = 0;
    for (int i = 0; i < w->n; i++) total += w->span[i].size;
    return total;
}

static void writes_copy(const struct hook_writes *w, uint8_t *buf, int save) {
    for (int i = 0; i < w->n; i++) {
        if (save) memcpy(buf, w->span[i].p, w->span[i].size);
        else memcpy(w->span[i].p, buf, w->span[i].size);
        buf += w->span[i].size;
    }
}

void hooks_writes_begin(struct hook_writes *w) {
    if (writes_depth >= WRITES_DEPTH) abort();
    if (!writes_depth && ui_n) hooks_ui_release();
    w->depth = writes_depth++;
    saves[w->depth].ui_start = ui_n;
    size_t total = writes_total(w);
    if (total > saves[w->depth].cap) {
        for (int i = 0; i < 2; i++) {
            free(saves[w->depth].buf[i]);
            saves[w->depth].buf[i] = malloc(total);
            if (!saves[w->depth].buf[i]) abort();
        }
        saves[w->depth].cap = total;
    }
    // The clock is held before the state is saved: a timer firing between
    // the two would set its flag after the save, in the original's run only.
    if (!w->protect_only) {
        if (!checking_depth++ && hooks_mode == HOOKS_SHADOW) journal_start();
        if (hooks_hold_time) hooks_hold_time(1);
    }
    writes_copy(w, saves[w->depth].buf[0], 1);
}

void hooks_writes_abandon(struct hook_writes *w) {
    writes_copy(w, saves[w->depth].buf[0], 0);
    ui_n = saves[w->depth].ui_start;
    writes_depth--;
    if (!w->protect_only) {
        if (!--checking_depth) journal_abandon();
        if (hooks_hold_time) hooks_hold_time(0);
    }
}

int hooks_checking(void) { return checking_depth > 0; }

// The bytes go below where fn's entry stack pointer will be (this one less
// the return address the call pushes), and nothing writes there until fn
// does.
_Static_assert(HOOK_STALE_SIZE == 0x100, "hooks_stale_call copies 0x40 words");
__asm__(".text\n"
        ".globl hooks_stale_call\n"
        "hooks_stale_call:\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    movl 12(%esp), %eax\n"  // fn
        "    movl 16(%esp), %esi\n"  // s
        "    leal -0x104(%esp), %edi\n"
        "    movl $0x40, %ecx\n"
        "    cld\n"
        "    rep movsl\n"
        "    call *%eax\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    ret\n");

void hooks_writes_game(struct hook_writes *w) {
    hooks_writes_add(w, game_data, game_data_size);
    if (*game_map)
        hooks_writes_add(w, *game_map, (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile));
    if (*game_abstract_map) hooks_writes_add(w, *game_abstract_map, (size_t)*game_abstract_area);
    hooks_writes_add(w, game_rand_state(), sizeof(uint32_t));
}
int hooks_protecting(void) { return writes_depth > 0; }

void hooks_writes_middle(struct hook_writes *w) {
    saves[w->depth].ui_mid = ui_n;
    if (checking_depth == 1) journal_middle();
    writes_copy(w, saves[w->depth].buf[1], 1);
    writes_copy(w, saves[w->depth].buf[0], 0);
}

const char *hooks_writes_end(struct hook_writes *w, int got, int want) {
    static char diff[640];
    const char *result = NULL;
    const uint8_t *orig = saves[w->depth].buf[1];
    // CHIRON_SHADOW_DIFFS=1 lists every range that differs, not just the first.
    static int list = -1;
    if (list < 0) list = getenv("CHIRON_SHADOW_DIFFS") != NULL;
    for (int i = 0; i < w->n && (!result || list); i++) {
        const uint8_t *mem = w->span[i].p;
        size_t size = w->span[i].size;
        if (memcmp(mem, orig, size))
            for (size_t j = 0; j < size; j++)
                if (mem[j] != orig[j]) {
                    if (!result) {
                        snprintf(diff, sizeof(diff), "wrote 0x%02x at %p, original 0x%02x", mem[j], (void *)(mem + j),
                                 orig[j]);
                        result = diff;
                        if (!list) break;
                    }
                    size_t k = j;
                    while (k < size && mem[k] != orig[k]) k++;
                    fprintf(stderr, "chiron:   differs at %p, %zu bytes\n", (void *)(mem + j), k - j);
                    j = k;
                }
        orig += size;
    }
    if (!result && got != want) {
        snprintf(diff, sizeof(diff), "= %d, original %d", got, want);
        result = diff;
    }
    if (!result) result = ui_compare(saves[w->depth].ui_start, saves[w->depth].ui_mid, diff, sizeof(diff));
    if (checking_depth == 1) {
        const char *calls = journal_end();
        if (!result) result = calls;
    }
    // Keep the original's held calls, for an outer check or for release.
    ui_n = hooks_sweeping ? saves[w->depth].ui_start : saves[w->depth].ui_mid;
    writes_copy(w, saves[w->depth].buf[hooks_sweeping ? 0 : 1], 0);
    writes_depth--;
    checking_depth--;
    if (hooks_hold_time) hooks_hold_time(0);
    return result;
}

// The summary can be asked for by two threads at once, the signal handler
// and exit; one prints it, and the other waits for it, as either may end
// the process when it returns.
static int summary_state;  // 0 not started, 1 printing, 2 printed

void hooks_summary(void) {
    if (hooks_mode != HOOKS_SHADOW) return;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&summary_state, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        for (int i = 0; i < 500 && __atomic_load_n(&summary_state, __ATOMIC_ACQUIRE) != 2; i++) usleep(10000);
        return;
    }
    fprintf(stderr, "chiron: shadow summary (function, calls from the game, mismatches, sweeps)\n");
    if (writes_depth) fprintf(stderr, "  (%d saved states still open)\n", writes_depth);
    for (size_t m = 0; m < N_MODULES; m++) {
        const struct hook_module *mod = modules[m];
        for (int i = 0; i < mod->count; i++) {
            const struct hook_stats *s = mod->hooks[i].stats;
            fprintf(stderr, "  %-16s %12llu %6llu %6u\n", s->name, s->calls, s->mismatches, mod->sweeps);
        }
        if (mod->sweeps)
            fprintf(stderr, "  (%s sweeps took %.1f ms on average, %.1f at most)\n", mod->name,
                    mod->sweep_ms_total / mod->sweeps, mod->sweep_ms_max);
    }
    __atomic_store_n(&summary_state, 2, __ATOMIC_RELEASE);
}
