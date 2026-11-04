// CHIRON_SOAK=1: report game progress and Chiron's resource use every 30
// seconds during long unattended runs. CHIRON_SOAK=ai additionally hands the
// player's faction to the AI once a game is under way, so turns keep coming
// with nobody at the keyboard.
//
// With no human left, the game still gives the player's console a pass over
// its units after each round (Console::human_turn, for watching), and offers
// any unit the AI left with moves and no orders. Nobody gives it orders, so
// the game would wait there for good, at "TURN COMPLETE" with the unit
// selected. When the turn hasn't moved for a while, such units are skipped,
// as the space bar would, and the scripted Return then ends the turn.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "../loader/modules.h"

// terranx.exe 2.0 globals (names from Thinker).
#define CURRENT_TURN 0x9a64d4
#define VEH_COUNT 0x9a64c8
#define FACTION_STATUS 0x9a64e8  // [0]: bitmask of human factions
#define VEH_READY 0x5c1b70       // int veh_ready(int veh): the player's console would offer it
#define VEH_SKIP 0x5c1d20        // void veh_skip(int veh): no more moves this turn
enum { STALL_MS = 30000 };

static void *game_addr(uint32_t addr) {
    char *exe = modules_get_handle(NULL);
    return exe ? exe + (addr - 0x400000) : NULL;
}

static long rss_kb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    long pages = 0, resident = 0;
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        fclose(f);
    }
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

static double cpu_seconds(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
}

// Called from the game thread's message pump, so the game's own functions
// can be called.
static void skip_waiting_units(void) {
    static int turn_seen;
    static Uint32 since;
    if (!game_addr(CURRENT_TURN)) return;
    int turn = *(volatile int *)game_addr(CURRENT_TURN);
    Uint32 now = SDL_GetTicks();
    if (turn != turn_seen || !since) {
        turn_seen = turn, since = now;
        return;
    }
    if (turn <= 0 || now - since < STALL_MS || *(volatile uint8_t *)game_addr(FACTION_STATUS)) return;
    since = now;
    int (*veh_ready)(int) = game_addr(VEH_READY);
    void (*veh_skip)(int) = game_addr(VEH_SKIP);
    int count = *(volatile int *)game_addr(VEH_COUNT), skipped = 0;
    for (int v = 0; v < count; v++)
        if (veh_ready(v)) veh_skip(v), skipped++;
    if (skipped) fprintf(stderr, "[soak] turn %d stuck: skipped %d unit(s) left waiting for orders\n", turn, skipped);
}

void soak_report(void) {
    static int mode = -1;  // 0 off, 1 report, 2 report and AI autoplay
    static Uint32 last;
    static double last_cpu;
    if (mode < 0) {
        const char *env = getenv("CHIRON_SOAK");
        mode = !env ? 0 : !strcmp(env, "ai") ? 2 : *env == '1';
    }
    if (!mode) return;
    volatile uint8_t *humans = game_addr(FACTION_STATUS);
    if (mode == 2 && humans && *humans && *(volatile int *)game_addr(VEH_COUNT) > 0) {
        fprintf(stderr, "[soak] handing human factions 0x%02x to the AI\n", *humans);
        *humans = 0;
    }
    if (mode == 2) skip_waiting_units();
    if (SDL_GetTicks() - last < 30000) return;
    double cpu = cpu_seconds();
    int turn = *(volatile int *)game_addr(CURRENT_TURN);
    fprintf(stderr, "[soak] t=%us turn=%d rss=%ldMB cpu=%.0f%%\n", SDL_GetTicks() / 1000, turn, rss_kb() / 1024,
            last ? 100.0 * (cpu - last_cpu) / ((SDL_GetTicks() - last) / 1000.0) : 0.0);
    last = SDL_GetTicks();
    last_cpu = cpu;
}
