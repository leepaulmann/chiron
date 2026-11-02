// CHIRON_SOAK=1: report game progress and Chiron's resource use every 30
// seconds during long unattended runs. CHIRON_SOAK=ai additionally hands the
// player's faction to the AI once a game is under way, so turns keep coming
// with nobody at the keyboard.
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
    if (SDL_GetTicks() - last < 30000) return;
    double cpu = cpu_seconds();
    int turn = *(volatile int *)game_addr(CURRENT_TURN);
    fprintf(stderr, "[soak] t=%us turn=%d rss=%ldMB cpu=%.0f%%\n", SDL_GetTicks() / 1000, turn, rss_kb() / 1024,
            last ? 100.0 * (cpu - last_cpu) / ((SDL_GetTicks() - last) / 1000.0) : 0.0);
    last = SDL_GetTicks();
    last_cpu = cpu;
}
