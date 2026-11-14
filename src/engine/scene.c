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
    uint32_t self;  // non-zero: a thiscall method of the object at this address (the integer arguments follow it)
    int repeat;     // how many times to call it (0 is once)
    uint32_t unsuspend;  // non-zero: the address of a word that is cleared during the calls and set to 1 again after them
    int discard;         // the function answers nothing (a void one): the answer is not kept
    int (*host)(int, int, int, int);  // a step of the host's own, run instead of calling the game
};
// A scene's own step: the techs 0 up to `count` given to a faction by setting their bits (no New Tech window), so that a base has more to
// build than a screenful. (Both modes do the same, and the faction's table is part of the state compared.)
static int give_techs(int faction, int count, int unused, int unused2) {
    (void)unused, (void)unused2;
    for (int t = 0; t < count && t < TECH_COUNT; t++) game_tech_achieved[t] |= (uint8_t)(1u << (faction & 7));
    return count;
}
// A scene's own step: the Monuments window's records (see monuwin.c). Faction 1's table (0x94ca08, 0x4f4 bytes a faction) gets
// the kinds of `used_mask` (bit k for kind k, 0 to 20) in use, in an order of dates that is not the order of the kinds; the kinds
// of `first_mask` are the first to have it (else the faction that had it before is 2 to 4, whose own record of that kind has a
// date too); `variant` bit 0 leaves the second number of every kind unset (-1), bit 1 the "what" of kind 2, bit 2 that of kinds
// 13 and 14; `shown` is the kind the window shows. (Both modes do the same, and the table is part of the state compared.)
static int give_monuments(int used_mask, int first_mask, int variant, int shown) {
    uint8_t *table = game_at(0x94ca08);
    for (int f = 1; f <= 7; f++)
        for (int k = 0; k < 21; k++) {
            int32_t *m = (int32_t *)(table + 0x4f4 * f + 0x38 * k);
            int own = f == 1;
            memset(m, 0, 0x38);
            m[0] = own ? 60 + 5 * ((k * 7) % 15) : 30 + 4 * f + k;  // when
            if (!own) continue;
            m[1] = (variant & 1) ? -1 : k + 1;  // extra
            m[2] = k == 1 ? 7 : k == 2 ? ((variant & 2) ? -1 : 4) : k == 5 ? 2 : k == 6 ? 3
                 : (k == 13 || k == 14) ? ((variant & 4) ? -1 : 2 + k % 3) : k >= 15 ? 2 + (k - 15) % 6 : 0;  // what
            m[3] = 2 + k % 3;                // other
            m[4] = (used_mask >> k) & 1;     // used
            m[5] = (first_mask >> k) & 1;    // first
        }
    *(int32_t *)(table + 0x4f4 + 0x498) = shown;
    memset(table + 0x4f4 + 0x4ac, 0, 0x48);
    strcpy((char *)table + 0x4f4 + 0x4ac, "Zetaport");
    strcpy((char *)table + 0x4f4 + 0x4c4, "Betatown");
    strcpy((char *)table + 0x4f4 + 0x4dc, "Gammaville");
    return used_mask;
}
static const struct open_entry open_table[] = {
    {"hall_of_fame", 0x44ae50, {0}},  // void hall_of_fame(int final): 0 only shows the list
    {"info_tick", 0x459150, {10, 0, 0, 0}, 0x7ad2a0, 0, 0x7ad34c},  // InfoWin_timer_proc(10): one tick of the info panel's clock, made by hand (a scene stops the real clock with the panel's `suspended` word, which is lifted for the call)
    {"info_tick20", 0x459150, {10, 0, 0, 0}, 0x7ad2a0, 19, 0x7ad34c},  // twenty of them
    {"info_daemon", 0x459260, {10, 0, 0, 0}, 0, 9, 0x7ad34c, 1},  // InfoWin_timer_daemon(10), the clock's callback: ten ticks
    {"info_reset", 0x459280, {0}, 0x7ad2a0, 0, 0x7ad34c},  // InfoWin_reset: ticks once if the panel is held
    {"info_state_a", 0x458f10, {3, 1, 0, 0}, 0x7ad2a0},  // InfoWin_set_state(3, 1)
    {"info_state_b", 0x458f10, {5, 0, 0, 0}, 0x7ad2a0},  // InfoWin_set_state(5, 0)
    {"info_state_c", 0x458f10, {-1, 1, 0, 0}, 0x7ad2a0},  // InfoWin_set_state(-1, 1)
    {"info_unk2", 0x4588b0, {0}, 0x7ad2a0},  // an InfoWin function nothing calls: the box redrawn from the start
    {"info_unk4", 0x458d60, {0}, 0, 0, 0, 1},  // another that nothing calls: registers the panel's strip
    {"tech_pick", 0x5beb70, {1, 1, -1, 0}},  // int tech_pick(faction, unk, other, label): the research picker for faction 1 (the player in the scenes' save); unk is set because the save plays blind research, which has no picker
    {"give_techs", 0, {1, 40}, .host = give_techs},  // the techs 0 to 39 for faction 1: more than a screenful to build
    {"hall_of_fame_new", 0x44ae50, {1}},  // hall_of_fame(1): as at a game's end, the loaded game is added to the list, with a picture of the screen
    {"monument_list", 0x476a50, {0}, .discard = 1},  // monument(0): the Monuments window on its list (the records are given first)
    {"monument_zoom", 0x476a50, {1}, .discard = 1},  // monument(1): the same, at the monument last shown, zoomed
    {"mon_14a", 0, {0x3fff, 0x1555, 0, 0}, .host = give_monuments},  // kinds 0 to 13, the even ones the faction's first
    {"mon_14b", 0, {0x3fff, 0x2aaa, 0, 1}, .host = give_monuments},  // the same, the odd ones the first, kind 1 shown
    {"mon_14c", 0, {0x3fff, 0x1555, 7, 13}, .host = give_monuments},  // the first again, nothing set that may be -1, kind 13 shown
    {"mon_14d", 0, {0x7ffe, 0x2aaa, 7, 14}, .host = give_monuments},  // kinds 1 to 14, the odd ones first, nothing set that may be -1
    {"mon_14e", 0, {0x3fff, 0x1555, 1, 2}, .host = give_monuments},  // kinds 0 to 13, the even ones first, the second number unset
    {"mon_14f", 0, {0x3fff, 0x2aaa, 1, 2}, .host = give_monuments},  // the odd ones first, the second number unset
    {"mon_six", 0, {0x3f, 0x15, 0, 2}, .host = give_monuments},  // kinds 0 to 5: six, so more than four
    {"mon_few", 0, {0x4021, 0, 0, 5}, .host = give_monuments},  // kinds 0, 5 and 14 only: a short list, over the first picture
    {"mon_win_1", 0, {0x8001, 0, 0, 15}, .host = give_monuments},  // kind 0 and one win
    {"mon_win_2", 0, {0x18001, 0, 0, 16}, .host = give_monuments},  // and two
    {"mon_win_3", 0, {0x38001, 0, 0, 17}, .host = give_monuments},
    {"mon_win_4", 0, {0x78001, 0, 0, 18}, .host = give_monuments},
    {"mon_win_5", 0, {0xf8001, 0, 0, 19}, .host = give_monuments},
    {"mon_win_6", 0, {0x1f8001, 0, 0, 20}, .host = give_monuments},
    {"mon_win_gap", 0, {0x28001, 0, 0, 17}, .host = give_monuments},  // wins 15 and 17 with 16 unused
};

static const struct open_entry *pending;

// "poke ADDR VALUE": set a word of the game's memory. Like an "open" it is only queued, and made from the game thread's
// message pump, never while shadow mode has state protected.
#define MAX_POKES 64
static struct {
    uint32_t addr, value;
    int or_in;  // set the bits of `value` and keep the rest
} pokes[MAX_POKES];
static int npokes;

void scene_poke(uint32_t addr, uint32_t value, int or_in) {
    if (npokes < MAX_POKES) pokes[npokes].addr = addr, pokes[npokes].value = value, pokes[npokes++].or_in = or_in;
}

void scene_open(const char *name) {
    for (size_t i = 0; i < sizeof(open_table) / sizeof(open_table[0]); i++)
        if (!strcmp(open_table[i].name, name)) {
            pending = &open_table[i];
            return;
        }
    fprintf(stderr, "chiron: script: no screen \"%s\" to open\n", name);
}

void scene_poll(void) {
    if (npokes && !hooks_protecting() && game_at(PREF_BASE)) {
        for (int i = 0; i < npokes; i++) {
            fprintf(stderr, "chiron: script: poke 0x%x = %u\n", pokes[i].addr, pokes[i].value);
            if (pokes[i].or_in) *(uint32_t *)game_at(pokes[i].addr) |= pokes[i].value;
            else *(uint32_t *)game_at(pokes[i].addr) = pokes[i].value;
        }
        npokes = 0;
    }
    if (!pending || hooks_protecting() || !game_at(PREF_BASE)) return;
    const struct open_entry *e = pending;
    pending = NULL;
    fprintf(stderr, "chiron: script: opening %s\n", e->name);
    typedef int (*fn4)(int, int, int, int);
    typedef int(__attribute__((thiscall)) * method)(void *self, int, int, int);
    if (e->unsuspend) *(uint32_t *)game_at(e->unsuspend) = 0;
    for (int n = 0; n <= e->repeat; n++)
        if (e->host) open_result = e->host(e->args[0], e->args[1], e->args[2], e->args[3]);
        else if (e->self) ((method)game_at(e->addr))(game_at(e->self), e->args[0], e->args[1], e->args[2]);  // (a method that answers nothing)
        else if (e->discard) ((fn4)game_at(e->addr))(e->args[0], e->args[1], e->args[2], e->args[3]);
        else open_result = ((fn4)game_at(e->addr))(e->args[0], e->args[1], e->args[2], e->args[3]);
    if (e->unsuspend) *(uint32_t *)game_at(e->unsuspend) = 1;
    fprintf(stderr, "chiron: script: %s answered %d\n", e->name, open_result);
}
