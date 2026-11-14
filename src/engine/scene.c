// What the script host (CHIRON_SCRIPT, user32_msg.c) does for scenes beyond
// clicks and keys: write the game's state at a "snap" step, and open screens
// by name with an "open" step. tools/snapcheck.py compares the results.
//
// State file. A snap step writes NAME.state next to the frame NAME.bmp, one
// line per region:
//     REGION ADDR SIZE RECSIZE TOTAL CHUNK0,CHUNK1,...
// ADDR (the game's own address), SIZE and RECSIZE (bytes in one record of a
// table, or the whole size for a block) are in hex; TOTAL is an FNV-1a hash
// of the whole region; the chunks are hashes of each 64 bytes, so a
// difference can be placed to a record and, within it, an offset. Regions:
// factions, faction_defs, bases, vehs, protos, map (the squares), globals (the
// data around the turn counter, with the ranges that vary with timing taken
// out, see globals_skip), and any a scene adds with "state NAME ADDR SIZE"
// (a screen object's fields, say). Tables are read through the pointers
// game.c binds, never copied.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../loader/modules.h"
#include "game.h"
#include "hooks.h"

enum { CHUNK = 64, MAX_EXTRA = 16, PREF_BASE = 0x400000 };

static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

struct region {
    char name[32];
    uint32_t addr;
    size_t size;
};
static struct region extra[MAX_EXTRA];
static int nextra;
// What the last "open" call answered (-2 before any), written to every state file as the region open_result: a screen that
// answers a choice (the research picker) shows it there.
static int32_t open_result = -2;

// "state NAME ADDR SIZE": add a region to every later snap.
void scene_add_state(const char *name, uint32_t addr, uint32_t size) {
    for (int i = 0; i < nextra; i++)
        if (!strcmp(extra[i].name, name)) {
            extra[i].addr = addr, extra[i].size = size;
            return;
        }
    if (nextra == MAX_EXTRA) return;
    snprintf(extra[nextra].name, sizeof(extra[0].name), "%s", name);
    extra[nextra].addr = addr, extra[nextra].size = size;
    nextra++;
}

// Regions of an object that lives only while a screen runs (a window built on the stack, which no fixed address
// names): the screen registers the pieces it wants compared when it opens and clears them when it ends
// (scene_live_state(name, NULL, 0)). They are written with the others at every snap.
static struct {
    char name[32];
    const void *p;
    size_t size;
} live[MAX_EXTRA];

void scene_live_state(const char *name, const void *p, size_t size) {
    int slot = -1;
    for (int i = 0; i < MAX_EXTRA; i++) {
        if (!strcmp(live[i].name, name)) {
            slot = i;
            break;
        }
        if (slot < 0 && !live[i].name[0]) slot = i;
    }
    if (slot < 0) return;
    snprintf(live[slot].name, sizeof(live[slot].name), "%s", name);
    live[slot].p = p, live[slot].size = p ? size : 0;
}

static void write_region(FILE *f, const char *name, const uint8_t *p, uint32_t addr, size_t size, size_t rec) {
    fprintf(f, "%s %x %zx %zx %08x ", name, addr, size, rec, fnv(p, size, 2166136261u));
    for (size_t o = 0; o < size; o += CHUNK)
        fprintf(f, "%s%08x", o ? "," : "", fnv(p + o, size - o < CHUNK ? size - o : CHUNK, 2166136261u));
    fputc('\n', f);
}

// The block of game globals that settings and turn state live in (the turn,
// the unit and base counts, the human factions, the game's rules and
// options) is hashed from GLOBALS_START to GLOBALS_END. Parts of it change
// with wall-clock time or with what the player's screen is doing rather than
// with the game, and are zeroed in the copy that is hashed: see the list.
enum { GLOBALS_START = 0x9a6000, GLOBALS_END = 0x9a7000 };
static const struct {
    uint32_t from, to;
} globals_skip[] = {
    // Same value throughout a run but different in every run, with the same
    // save loaded (two Chiron runs, "on" and "on_b", differ in exactly these
    // 64-byte chunks at every snap and nowhere else): not game state.
    {0x9a6000, 0x9a6480},
    {0x9a6800, 0x9a68c0},
};

void scene_write_state(const char *frame_path) {
    char path[600];
    snprintf(path, sizeof(path), "%s", frame_path);
    char *dot = strrchr(path, '.');
    if (dot) *dot = 0;
    strcat(path, ".state");
    uint8_t *exe = modules_get_handle(NULL);
    FILE *f = exe && game_factions ? fopen(path, "w") : NULL;
    if (!f) return;
#define TABLE(name, ptr, count, type) \
    write_region(f, name, (const uint8_t *)(ptr), (uint32_t)((uint8_t *)(ptr) - exe) + PREF_BASE, (count) * sizeof(type), sizeof(type))
    TABLE("factions", game_factions, FACTION_COUNT, struct faction_data);
    TABLE("faction_defs", game_faction_defs, FACTION_COUNT, struct faction_def);
    TABLE("bases", game_bases, BASE_COUNT, struct base);
    TABLE("vehs", game_vehs, VEH_COUNT + 1, struct veh);
    TABLE("protos", game_protos, PROTO_COUNT, struct unit_proto);
    if (game_map && *game_map && *game_map_area > 0) TABLE("map", *game_map, (size_t)*game_map_area, struct map_tile);
#undef TABLE
    static uint8_t globals[GLOBALS_END - GLOBALS_START];
    memcpy(globals, game_at(GLOBALS_START), sizeof(globals));
    for (size_t i = 0; i < sizeof(globals_skip) / sizeof(globals_skip[0]); i++)
        if (globals_skip[i].to > globals_skip[i].from)
            memset(globals + globals_skip[i].from - GLOBALS_START, 0, globals_skip[i].to - globals_skip[i].from);
    write_region(f, "globals", globals, GLOBALS_START, sizeof(globals), sizeof(globals));
    write_region(f, "open_result", (const uint8_t *)&open_result, 0, sizeof(open_result), sizeof(open_result));
    for (int i = 0; i < nextra; i++)
        write_region(f, extra[i].name, game_at(extra[i].addr), extra[i].addr, extra[i].size, extra[i].size);
    for (int i = 0; i < MAX_EXTRA; i++)
        if (live[i].p) write_region(f, live[i].name, live[i].p, (uint32_t)(uintptr_t)live[i].p, live[i].size, live[i].size);
    fclose(f);
}

// ---- "open NAME": call a screen's entry point ------------------------------------
//
// Some screens no click reaches (the Hall of Fame after a game's end, say).
// The step only queues the call; the script host runs it from the game
// thread's message pump (scene_poll), where the game itself would make such
// a call, and never while shadow mode has state protected. A screen that
// loops until the player closes it keeps pumping messages, so later steps of
// the script go on running inside it. Add a screen as one line of the table:
// its name, the game's address of its entry point (cdecl), and the integer
// arguments to give it.
struct open_entry {
    const char *name;
    uint32_t addr;
    int args[4];
    int (*host)(int, int, int, int);  // a step of the host's own, run instead of calling the game
};
// A scene's own step: the techs 0 up to `count` given to a faction by setting their bits (no New Tech window), so that a base has more to
// build than a screenful. (Both modes do the same, and the faction's table is part of the state compared.)
static int give_techs(int faction, int count, int unused, int unused2) {
    (void)unused, (void)unused2;
    for (int t = 0; t < count && t < TECH_COUNT; t++) game_tech_achieved[t] |= (uint8_t)(1u << (faction & 7));
    return count;
}
static const struct open_entry open_table[] = {
    {"hall_of_fame", 0x44ae50, {0}, NULL},  // void hall_of_fame(int final): 0 only shows the list
    {"tech_pick", 0x5beb70, {1, 1, -1, 0}, NULL},  // int tech_pick(faction, unk, other, label): the research picker for faction 1 (the player in the scenes' save); unk is set because the save plays blind research, which has no picker
    {"give_techs", 0, {1, 40}, give_techs},  // the techs 0 to 39 for faction 1: more than a screenful to build
};

static const struct open_entry *pending;

void scene_open(const char *name) {
    for (size_t i = 0; i < sizeof(open_table) / sizeof(open_table[0]); i++)
        if (!strcmp(open_table[i].name, name)) {
            pending = &open_table[i];
            return;
        }
    fprintf(stderr, "chiron: script: no screen \"%s\" to open\n", name);
}

void scene_poll(void) {
    if (!pending || hooks_protecting() || !game_at(PREF_BASE)) return;
    const struct open_entry *e = pending;
    pending = NULL;
    fprintf(stderr, "chiron: script: opening %s\n", e->name);
    typedef int (*fn4)(int, int, int, int);
    fn4 fn = e->host ? e->host : (fn4)game_at(e->addr);
    open_result = fn(e->args[0], e->args[1], e->args[2], e->args[3]);
    fprintf(stderr, "chiron: script: %s answered %d\n", e->name, open_result);
}
