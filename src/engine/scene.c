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
#include "screen.h"

enum { CHUNK = 64, MAX_EXTRA = 64, PREF_BASE = 0x400000 };

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
// A scene's own step: the Social Engineering window's two bubble helpers (src/ui/socialwin.c), which take a text and a point of the
// screen: `which` 0 the window's own (bubble_me_elmo2), 1 the main screen's. The text is one of the game's own (the tutorial note's
// label).
static int social_bubble(int which, int x, int y, int unused) {
    (void)unused;
    typedef void(__attribute__((thiscall)) * bubble_call)(void *, const char *, int, int);
    ((bubble_call)game_at(which ? 0x4b1b70 : 0x4b1b30))(game_at(0x8a6270), game_at(0x6874bc), x, y);
    return which;
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
// A scene's own step: a tutorial note (src/ui/tutwin.c) asked for the way the game asks for one, with tut_map. The note is about
// the square of the first unit; `kind` says what with (0 the square's terrain, 1 a unit and, `dx` and `dy` squares away, a second
// point, 2 a picture and no unit, 3 a base), `mode` how it is placed (0 to 6; see tutwin_place). The labels and the picture are the
// ones the game's own code uses for such notes. The call is modal: it returns when the note has been answered.
static int tut_note(int kind, int mode, int dx, int dy) {
    int x = game_vehs[0].x, y = game_vehs[0].y;
    switch (kind) {
    case 0: return game_tut_map(game_tut_win, game_at(0x68ed18), x, y, -1, NULL, mode, -1, -1);
    case 1: return game_tut_map(game_tut_win, game_at(0x68fd20), x, y, 0, NULL, mode, x + dx, y + dy);
    case 2: return game_tut_map(game_tut_win, game_at(0x687c5c), x, y, -1, game_sprite_farm, mode, -1, -1);
    case 3: return game_tut_map(game_tut_win, game_at(0x68fd20), x, y, -2, NULL, mode, -1, -1);
    case 4:  // the view first moved away from the square, which the note brings back (and a second square, in view only then)
        game_map_win_set_center(game_map_wins[0], x + 9, y + 5, 1);
        return game_tut_map(game_tut_win, game_at(0x68fd20), x, y, 0, NULL, mode, x + dx, y + dy);
    }
    return -1;
}
// The tutorial window's helpers that take a rectangle of one of the game's screens (src/ui/tutwin.c): `which` picks the screen
// (0 base, 1 interface, 2 social, 3 design) and `what` the function: 0 the rectangle's centre as a point on that screen (the *_rect
// function), 1 the same without the screen (the plain centre), 2 a note pointing at that centre (the do_* function; modal, and
// answers the note's own answer). The rectangle is made up. The answer is the point as x * 10000 + y for the first two.
static int tut_rect(int which, int what, int unused, int unused2) {
    static const uint32_t rect_fn[4] = {0x4bc5f0, 0x4bc5a0, 0x4bc640, 0x4bc690}, do_fn[4] = {0x4ba870, 0x4ba8f0, 0x4ba970, 0x4ba9f0};
    int32_t rect[4] = {13, 22, 97, 61}, x = -1, y = -1;
    typedef void(__attribute__((thiscall)) * rect_call)(void *, int32_t *, int32_t *, int32_t *);
    typedef int(__attribute__((thiscall)) * do_call)(void *, int32_t *, char *, int);
    if (what == 0) ((rect_call)game_at(rect_fn[which & 3]))(NULL, rect, &x, &y);
    else if (what == 1) ((rect_call)game_at(0x4ba830))(NULL, rect, &x, &y);
    else return ((do_call)game_at(do_fn[which & 3]))(game_tut_win, rect, game_at(0x68ed18), 0);
    return x * 10000 + y;
}
// The three functions that draw a note's picture (the terrain, a unit, a base), called the way the popup calls them: with the
// picture, the window to draw in, an id and a position. The popup's own Buffer is not copied to the screen until the popup redraws,
// so the window given here is a made-up one whose Buffer (0x444 in) is the main Buffer, which is then flipped to the screen, and the
// picture is seen. `which` is 0 terrain (of the first unit's square, which the note's fields hold while it is up, and are put there
// here), 1 unit, 2 base (id -2 is the first).
static int tut_draw(int which, int id, int x, int y) {
    static const uint32_t draw_fn[3] = {0x4bdf20, 0x4bdf70, 0x4bdfa0};
    typedef int(__attribute__((cdecl)) * draw_call)(void *, void *, int, int, int);
    *(int32_t *)game_at(0x8cc1e4) = game_vehs[0].x, *(int32_t *)game_at(0x8cc1e8) = game_vehs[0].y;
    int answer = ((draw_call)game_at(draw_fn[which % 3]))(NULL, (uint8_t *)game_at(0x8ea3a4) - 0x444, id, x, y);
    win_flip(NULL);
    return answer;
}
// A note at a point of the screen (tut_win, as the tour's helpers call it, with the map window and no picture): `mode` as for
// tut_note, `second` non-zero for a second arrow to the point (300, 200). Modal.
static int tut_at(int x, int y, int mode, int second) {
    typedef int(__attribute__((thiscall)) * win_call)(void *, void *, char *, int, int, void *, int, int, int);
    void *map = game_map_wins[0];
    return ((win_call)game_at(0x4bdfe0))(game_tut_win, (uint8_t *)map + ((int32_t *)*(void **)map)[1], game_at(0x68ed18), x, y, NULL, mode,
                                          second ? 300 : -1, second ? 200 : -1);
}
// TutWin_on_button_clicked(id), called by hand: the notes' own buttons answer 0 (Continue and Close) or -2 (End Tour) and never a
// positive id (only the tour's notes have such buttons), so the branches for them are reached this way, with the object's fields
// put where the branch wants them by pokes.
static int tut_click(int id, int unused, int unused2, int unused3) {
    typedef int(__attribute__((thiscall)) * click_call)(void *, int);
    return ((click_call)game_at(0x4bdcc0))(game_tut_win, id);
}
// TutWin_UNK3: the window the arrows lie over set to the map window (on) or to none.
static int tut_set_win(int on, int unused, int unused2, int unused3) {
    typedef void(__attribute__((thiscall)) * set_call)(void *, void *);
    void *map = game_map_wins[0];
    ((set_call)game_at(0x4bddd0))(game_tut_win, on ? (uint8_t *)map + ((int32_t *)*(void **)map)[1] : NULL);  // (its Win part)
    return on;
}
// A scene's own step: the votes the factions have in the Council (src/ui/councwin.c). The pop size of the first base of faction n
// (1 to 7) is the n-th digit of `digits` counted from the left, so that who may stand for governor (fewer than two others have more
// votes) and how many votes each leader shows can be chosen. (Both modes do the same; the bases are part of the state compared.)
static int give_votes(int digits, int unused, int unused2, int unused3) {
    (void)unused, (void)unused2, (void)unused3;
    int pop[8] = {0};
    for (int f = 7; f >= 1; f--) pop[f] = digits % 10, digits /= 10;
    for (int f = 1; f <= 7; f++)
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == f) {
                game_bases[b].pop_size = (int8_t)pop[f];
                break;
            }
    return 0;
}
// A scene's own step: the tutorial's notes switched off (bit 0x20 of the preferences), so that none comes up over a window the scene
// has opened. (Both modes do the same; the word is part of the globals compared.)
static int tutorial_off(int unused, int unused2, int unused3, int unused4) {
    (void)unused, (void)unused2, (void)unused3, (void)unused4;
    *(uint32_t *)game_at(0x9a6490) &= ~0x20u;
    return 0;
}
static const struct open_entry open_table[] = {
    {"status_redraw", 0x4b9ea0, {0}, 0x8c5568},  // StatusWin_redraw: the panel drawn again
    {"status_on_redraw", 0x4b8890, {0}, 0x8c5568},
    {"status_resize", 0x4b4990, {0}, 0x8c5568},  // StatusWin_UNK1: the fonts made again
    {"status_left", 0x4b89c0, {0}, 0x8c5568},  // the arrows
    {"status_right", 0x4b8bd0, {0}, 0x8c5568},
    {"status_activate", 0x4b8df0, {0}, 0x8c5568},
    {"status_reset", 0x4b8970, {0}, 0x8c5568},
    {"status_reset_active", 0x4b6530, {0}, 0x8c5568},
    {"status_title_0", 0x4b4a20, {0}, 0x8c5568},  // StatusWin_set_title(flags)
    {"status_title_1", 0x4b4a20, {1}, 0x8c5568},
    {"status_title_2", 0x4b4a20, {2}, 0x8c5568},
    {"status_title_4", 0x4b4a20, {4}, 0x8c5568},
    {"status_title_8", 0x4b4a20, {8}, 0x8c5568},
    {"status_title_10", 0x4b4a20, {0x10}, 0x8c5568},
    {"status_hide", 0x4b4cd0, {0}, 0x8c5568},  // StatusWin_UNK2 (hides the destination window)
    {"status_flag", 0x4b9e90, {0}, 0x8c5568},  // StatusWin_UNK4
    {"status_close", 0x4b9f80, {0}, 0x8c5568},
    {"status_active_v0", 0x4b4cf0, {0, -1, 0}, 0x8c5568},  // draw_active(unit 0, no base, no hit rectangle)
    {"status_active_v0r", 0x4b4cf0, {0, -1, 1}, 0x8c5568},
    {"status_active_b0", 0x4b4cf0, {-1, 0, 0}, 0x8c5568},
    {"status_active_b0r", 0x4b4cf0, {-1, 0, 1}, 0x8c5568},
    {"status_active_none", 0x4b4cf0, {-1, -1, 0}, 0x8c5568},
    {"status_dblclick", 0x4b8990, {190, 640}, 0x8c5568},
    {"tut_off", 0, {0}, .host = tutorial_off},
    // The Council's window (src/ui/councwin.c), opened the way the game opens it, with council(faction, proposal, candidate) for the
    // player (faction 1), and its methods called by hand (on the window at 0x6fec80, or on its interface part, 0xa14 into it).
    {"council_gov", 0x428350, {1, 0, 1, 0}, .discard = 1},  // the election of a governor, the player standing
    {"council_gov_2", 0x428350, {1, 0, 2, 0}, .discard = 1},
    {"council_p1", 0x428350, {1, 1, 2, 0}, .discard = 1},   // the Supreme Leader (the candidate named)
    {"council_p2", 0x428350, {1, 2, 0, 0}, .discard = 1},   // the proposals with a yea and a nay
    {"council_p3", 0x428350, {1, 3, 0, 0}, .discard = 1},
    {"council_p5", 0x428350, {1, 5, 0, 0}, .discard = 1},
    {"council_p8", 0x428350, {1, 8, 0, 0}, .discard = 1},
    {"votes_all", 0, {1111111, 0, 0, 0}, .host = give_votes},     // everyone equal: all seven may stand
    {"votes_two", 0, {7654321, 0, 0, 0}, .host = give_votes},     // distinct: the two with the most may stand
    {"votes_three", 0, {7665432, 0, 0, 0}, .host = give_votes},   // a tie for second: three may stand
    {"votes_me_low", 0, {1765432, 0, 0, 0}, .host = give_votes},  // the player (faction 1) is not among the ones that may stand
    {"votes_four", 0, {7777321, 0, 0, 0}, .host = give_votes},    // four of equal strength
    {"votes_pivot", 0, {4552100, 0, 0, 0}, .host = give_votes},   // two others have more than the player: it may not stand, but its votes could decide between them
    {"cw_set_vote", 0x424720, {1, -1, 0, 0}, 0x6fec80},     // set_vote(1, -1): the player votes yea
    {"cw_set_vote_n", 0x424720, {1, -2, 0, 0}, 0x6fec80},
    {"cw_set_vote_a", 0x424720, {1, -3, 0, 0}, 0x6fec80},
    {"cw_set_vote_c", 0x424720, {1, 2, 0, 0}, 0x6fec80},    // the player votes for faction 2
    {"cw_set_vote_ai", 0x424720, {2, 3, 0, 0}, 0x6fec80},   // a vote by an AI faction
    {"cw_resolve_check", 0x424410, {0, 0, 0, 0}, 0x6fec80},  // make_resolution(0)
    {"cw_resolve", 0x424410, {1, 0, 0, 0}, 0x6fec80},        // make_resolution(1)
    {"cw_redraw", 0x426aa0, {0}, 0x6fec80},
    {"cw_draw_info", 0x426470, {0}, 0x6fec80},
    {"cw_draw_all", 0x425d90, {0}, 0x6fec80},
    {"cw_draw_1", 0x425db0, {1, 0, 0, 0}, 0x6fec80},
    {"cw_draw_2", 0x425db0, {2, 0, 0, 0}, 0x6fec80},
    {"cw_draw_3", 0x425db0, {3, 0, 0, 0}, 0x6fec80},
    {"cw_draw_0", 0x425db0, {0, 0, 0, 0}, 0x6fec80},
    {"cw_draw_7", 0x425db0, {7, 0, 0, 0}, 0x6fec80},
    {"cw_click_1", 0x424870, {1, 0, 0, 0}, 0x6fec80},  // the window's own button handler, called with the ids of the buttons
    {"cw_click_2", 0x424870, {2, 0, 0, 0}, 0x6fec80},
    {"cw_click_3", 0x424870, {3, 0, 0, 0}, 0x6fec80},
    {"cw_click_4", 0x424870, {4, 0, 0, 0}, 0x6fec80},
    {"cw_click_5", 0x424870, {5, 0, 0, 0}, 0x6fec80},
    {"cw_click_m2", 0x424870, {-2, 0, 0, 0}, 0x6fec80},
    {"cw_click_m1", 0x424870, {-1, 0, 0, 0}, 0x6fec80},
    {"cw_key_return", 0x425ba0, {0, 0xd, 0, 0}, 0x6fec80},
    {"cw_key_escape", 0x425ba0, {0, 0x1b, 0, 0}, 0x6fec80},
    {"cw_key_x", 0x425ba0, {0, 0x78, 0, 0}, 0x6fec80},
    {"cw_leave", 0x426ec0, {0, 0, 0, 0}, 0x6fec80},
    {"cw_group", 0x426eb0, {0, 0, 0, 0}, 0x6fec80},
    {"cw_passover", 0x426f80, {0, 0, 0, 0}, 0x6fec80},
    {"cw_text_draw", 0x426f90, {0, 0, 0, 0}, 0x6fec80},
    {"cw_arrived_1", 0x4271c0, {1, 0, 0, 0}, 0x6fec80},
    {"cw_arrived_2", 0x4271c0, {2, 0, 0, 0}, 0x6fec80},
    {"cw_is_visible", 0x427150, {0, 0, 0, 0}, 0x6fec80},
    {"cw_show", 0x427130, {0, 0, 0, 0}, 0x6fec80},
    {"cw_status_off", 0x426fa0, {0, 0, 0, 0}, 0x6ff694},  // the interface part's calls (the part is 0xa14 into the window)
    {"cw_status_on", 0x426fa0, {1, 0, 0, 0}, 0x6ff694},
    {"cw_iface_1", 0x4270f0, {1, 0, 0, 0}, 0x6ff694},  // the interface's forwarding of a button click, and its calls that do nothing
    {"cw_iface_2", 0x4270f0, {2, 0, 0, 0}, 0x6ff694},
    {"cw_iface_5", 0x4270f0, {5, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s0", 0x427060, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s1", 0x427070, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s2", 0x427080, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s3", 0x427090, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s4", 0x4270a0, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s5", 0x4270b0, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s6", 0x4270c0, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s7", 0x4270d0, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s8", 0x4270e0, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s9", 0x427110, {0, 0, 0, 0}, 0x6ff694},
    {"cw_iface_s10", 0x427120, {0, 0, 0, 0}, 0x6ff694},
    {"cw_close", 0x427160, {0}, 0x6fec80},
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
    {"prefs_redraw_option", 0x491810, {0}, 0x8578d8},  // PrefWin_redraw_option: a function of the Preferences window nothing calls (the options' frame drawn)
    {"prefs_redraw_volume", 0x491860, {0}, 0x8578d8},  // and another (the sliders' frame, when the audio page is shown)
    {"prefs_key_up", 0x48fcd0, {0, 0x26}, 0x8578d8},  // PrefWin_on_key_click(0, VK_UP): the Preferences window's Up key (an arrow key of the host goes to the focused box, so the call is made here)
    {"prefs_key_down", 0x48fcd0, {0, 0x28}, 0x8578d8},  // and its Down key
    {"social_open", 0x5b5620, {1}, 0, 0, 0, 1},  // social_select(1): the Social Engineering window (src/ui/socialwin.c), modal
    {"social_open_2", 0x5b5620, {2}, 0, 0, 0, 1},  // for faction 2: another faction's models and effects
    {"give_techs_all", 0, {1, 99}, .host = give_techs},  // every tech for faction 1: every social model can be chosen
    {"social_build", 0x4b2740, {-1}, 0x8a6270},  // SocialWin_init(-1): made, not shown (as the tutorial's tour makes it)
    {"social_show", 0x4b2690, {0}, 0x8a6270},  // show: the sub interface's mode set while the window is not up
    {"social_hide", 0x4b26b0, {0}, 0x8a6270},
    {"social_close", 0x4b26d0, {0}, 0x8a6270},
    {"social_unk2", 0x4aef00, {0}, 0x8a6270},  // two functions that do nothing
    {"social_unk3", 0x4b1790, {0}, 0x8a6270},
    {"social_status_on", 0x4b2120, {1}, 0x8a6c84},  // the sub interface (0xa14 in the object): shown, and hidden
    {"social_status_off", 0x4b2120, {0}, 0x8a6c84},
    {"social_iface_ldown", 0x4b2400, {0, 0}, 0x8a6c84},  // its handlers that do nothing
    {"social_iface_rdown", 0x4b2410, {0, 0}, 0x8a6c84},
    {"social_iface_selected", 0x4b2440, {0, 0}, 0x8a6c84},
    {"social_iface_ldouble", 0x4b2450, {0, 0}, 0x8a6c84},
    {"social_iface_rdouble", 0x4b2460, {0, 0}, 0x8a6c84},
    {"social_iface_toggled", 0x4b2670, {0, 0}, 0x8a6c84},
    {"social_iface_group", 0x4b2680, {0, 0, 0}, 0x8a6c84},
    {"social_iface_left", 0x4b2210, {0, 0}, 0x8a6c84},
    {"social_iface_move", 0x4b2420, {0, 0}, 0x8a6c84},
    {"social_iface_leave", 0x4b2430, {0, 0}, 0x8a6c84},
    {"social_left_click", 0x4b1b20, {0, 0}, 0x8a6270},
    {"social_leave", 0x4b1fc0, {0, 0}, 0x8a6270},  // the mouse left the window (the host sends no such event when it goes out of the window)
    {"social_bubble_win", 0, {0, 300, 300, 0}, .host = social_bubble},  // the bubble helpers: a bubble at a point
    {"social_bubble_main", 0, {1, 400, 400, 0}, .host = social_bubble},
    {"social_immune_a", 0x4b0fa0, {3, 1, 0}, 0x8a6270},  // SocialWin_immune(category, model, effect)
    {"social_immune_b", 0x4b0fa0, {2, 1, 0}, 0x8a6270},
    {"social_immune_c", 0x4b0fa0, {3, 3, 0}, 0x8a6270},
    {"give_techs", 0, {1, 40}, .host = give_techs},  // the techs 0 to 39 for faction 1: more than a screenful to build
    {"give_techs_2", 0, {2, 20}, .host = give_techs},  // the techs 0 to 19 for faction 2 (an AI that then has techs to offer the player), and for faction 3
    {"give_techs_3", 0, {3, 20}, .host = give_techs},
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
    {"tut_terrain", 0, {0, 0}, .host = tut_note},   // the tutorial's note on the first unit's square, mode 0
    {"tut_terrain_m2", 0, {0, 2}, .host = tut_note},  // the modes that place it to one side or the other and high or low, and ask for a Cancel button
    {"tut_terrain_m3", 0, {0, 3}, .host = tut_note},
    {"tut_terrain_m4", 0, {0, 4}, .host = tut_note},
    {"tut_terrain_m5", 0, {0, 5}, .host = tut_note},
    {"tut_terrain_m6", 0, {0, 6}, .host = tut_note},
    {"tut_terrain_held", 0, {0, 0x102}, .host = tut_note},   // mode 2 with a bit in the second byte: the note is held (the flag at 0x8cc244; the button then sets the one at 0x8cc248)
    {"tut_terrain_bits", 0, {0, 0x10002}, .host = tut_note},  // mode 2 with a bit above the low half: the button's answer adds it to the word at 0x9a64bc
    {"tut_terrain_both", 0, {0, 0x20102}, .host = tut_note},
    {"tut_terrain_held0", 0, {0, 0x100}, .host = tut_note},   // the same on mode 0, whose Close button answers with a positive id
    {"tut_terrain_bits0", 0, {0, 0x10000}, .host = tut_note},
    {"tut_terrain_both0", 0, {0, 0x20100}, .host = tut_note},
    {"tut_unit", 0, {1, 0, 3, 0}, .host = tut_note},   // a unit, with a second arrow to a square three to the right
    {"tut_unit_far", 0, {1, 0, 60, 0}, .host = tut_note},  // the second square off the screen: no second arrow
    {"tut_picture", 0, {2, 1}, .host = tut_note},   // a note with a picture of its own (the farm's), mode 1 (goes on to a tour)
    {"tut_base", 0, {3, 0}, .host = tut_note},   // a note about a base
    {"tut_scrolled", 0, {4, 0, 2, 0}, .host = tut_note},   // the view moved away first: the map is brought back to the square, and a second square is in view
    {"tut_at_low_right", 0, {900, 600, 0, 0}, .host = tut_at},   // notes at points of the screen: low and right, low and left, high and right, with a second arrow
    {"tut_at_low_left", 0, {100, 600, 0, 0}, .host = tut_at},
    {"tut_at_high_right", 0, {900, 100, 0, 0}, .host = tut_at},
    {"tut_at_second", 0, {500, 300, 0, 1}, .host = tut_at},
    {"tut_at_m3_low", 0, {500, 500, 3, 0}, .host = tut_at},   // modes 3, 4 and 5 with the point below their height limits
    {"tut_at_m4_low", 0, {500, 400, 4, 0}, .host = tut_at},
    {"tut_at_m5_low", 0, {800, 400, 5, 0}, .host = tut_at},
    {"tut_click_1", 0, {1}, .host = tut_click},  // the button handler given a positive id, and the ids around the ones the notes' buttons give
    {"tut_click_2", 0, {2}, .host = tut_click},
    {"tut_click_0", 0, {0}, .host = tut_click},
    {"tut_click_m1", 0, {-1}, .host = tut_click},
    {"tut_click_m2", 0, {-2}, .host = tut_click},
    {"tut_click_m3", 0, {-3}, .host = tut_click},
    {"tut_reset", 0x4bdde0, {0}, 0x8c6e68, 0, 0, 1},  // TutWin_reset: takes the note down if one is up
    {"tut_clear", 0x4ba720, {0}, 0x8c6e68, 0, 0, 1},  // the forgetting of the note's fields (TutWin_UNK1)
    {"tut_setwin", 0x4bddd0, {0}, 0x8c6e68, 0, 0, 1},  // the window the arrows lie over, set to none (TutWin_UNK3)
    {"tut_setwin_map", 0, {1}, .host = tut_set_win},  // and to the map window
    {"tut_move", 0x4bddb0, {0, 0}, 0x8c6e68, 0, 0, 1},  // the window was moved or redrawn: the arrows' window redrawn
    {"tut_arrow", 0x4bdea0, {0}, 0x8c6e68, 0, 0, 1},  // the arrows drawn again
    {"tut_key_x", 0x4ba770, {0, 0x78}, 0x8c6e68, 0, 0, 1},  // a key (the letter x) given to the note
    {"tut_key_esc", 0x4ba770, {0, 0x1b}, 0x8c6e68, 0, 0, 1},  // Escape given to the note: the answer is cleared too
    {"tut_check_1", 0x4ba5f0, {1}},  // the test of whether the note for bit 1 is due (marks it shown if so)
    {"tut_check_4", 0x4ba5f0, {4}},
    {"tut_check2_1", 0x4ba650, {1}},
    {"tut_check2_8", 0x4ba650, {8}},
    {"tut_rect_base", 0, {0, 0}, .host = tut_rect},  // a rectangle's centre as a point on the base screen
    {"tut_rect_iface", 0, {1, 0}, .host = tut_rect},
    {"tut_rect_soc", 0, {2, 0}, .host = tut_rect},
    {"tut_rect_des", 0, {3, 0}, .host = tut_rect},
    {"tut_centre", 0, {0, 1}, .host = tut_rect},  // a rectangle's centre
    {"tut_do_base", 0, {0, 2}, .host = tut_rect},  // a note pointing at a rectangle of the base screen
    {"tut_do_iface", 0, {1, 2}, .host = tut_rect},
    {"tut_do_soc", 0, {2, 2}, .host = tut_rect},
    {"tut_do_des", 0, {3, 2}, .host = tut_rect},
    {"tut_draw_terrain", 0, {0, 0, 150, 60}, .host = tut_draw},  // the three picture callbacks, called with a note up
    {"tut_draw_veh", 0, {1, 0, 150, 60}, .host = tut_draw},
    {"tut_draw_base", 0, {2, -2, 150, 60}, .host = tut_draw},
};

// ---- Steps for the status panel (src/ui/statuswin.c) ----
// The panel shows the square (sw->x, sw->y) and the units there; a click or the mouse can't reach most kinds of square, so a scene
// puts the game there with "host NAME a b c d": `status_look dx dy` moves the panel to the square (dx, dy) from the first unit's and
// redraws it, `status_tile dx dy offset value` writes a word of that square (offset 0, 4, 8 or 12 of the map record), `status_stack
// count proto faction damage` makes `count` more units on the first unit's square (copies of it), `status_base dx dy faction size` a
// base on that square, `status_draw a b c d` calls the panel's battle entry (five arguments, which an open entry can't carry). Both
// modes do the same, and the tables are part of the state compared.
static struct map_tile *status_tile_at(int dx, int dy) {
    int x = game_vehs[0].x + dx, y = game_vehs[0].y + dy;
    return &(*game_map)[(x >> 1) + y * *game_map_half_width];
}
static int status_look(int dx, int dy, int unused, int unused2) {
    (void)unused, (void)unused2;
    typedef void(__attribute__((thiscall)) * set_loc_call)(void *, int, int);
    typedef void(__attribute__((thiscall)) * redraw_call)(void *);
    ((set_loc_call)game_at(0x4b9f90))(game_at(0x8c5568), game_vehs[0].x + dx, game_vehs[0].y + dy);
    ((redraw_call)game_at(0x4b9ea0))(game_at(0x8c5568));
    return 0;
}
static int status_tile(int dx, int dy, int offset, int value) {
    // (offset bit 8: set the bits of the value and keep the rest; bit 9: clear them; bit 10: set one byte only)
    uint32_t *w = (uint32_t *)((uint8_t *)status_tile_at(dx, dy) + (offset & 0xff));
    if (offset & 0x100) *w |= (uint32_t)value;
    else if (offset & 0x200) *w &= ~(uint32_t)value;
    else if (offset & 0x400) *(uint8_t *)w = (uint8_t)value;
    else *w = (uint32_t)value;
    return value;
}
static int status_stack(int count, int proto, int faction, int damage) {
    int first = *game_veh_count;
    for (int i = 0; i < count; i++) {
        int id = first + i;
        game_vehs[id] = game_vehs[0];
        game_vehs[id].proto = (int16_t)proto;
        game_vehs[id].faction = (uint8_t)faction;
        game_vehs[id].damage = (uint8_t)damage;
        game_vehs[id].prev_in_stack = i + 1 < count ? (int16_t)(id + 1) : -1;  // (the game walks the stack from its top by +0x32)
        game_vehs[id].next_in_stack = i > 0 ? (int16_t)(id - 1) : -1;
    }
    if (count) *game_veh_count = first + count;
    status_tile_at(0, 0)->bits |= MAP_UNIT;
    return first;
}
static int status_base(int dx, int dy, int faction, int size) {
    int id = *game_base_count;
    struct base *b = &game_bases[id];
    memset(b, 0, sizeof(*b));
    b->x = (int16_t)(game_vehs[0].x + dx);
    b->y = (int16_t)(game_vehs[0].y + dy);
    b->faction = b->faction_former = (uint8_t)faction;
    b->pop_size = (int8_t)size;
    b->visibility = 0xff;
    strcpy(b->name, "Testbase");
    b->queue[0] = 0;
    *game_base_count = id + 1;
    status_tile_at(dx, dy)->bits |= MAP_BASE;
    return id;
}
static int status_draw(int a, int b, int c, int d) {
    typedef void(__attribute__((thiscall)) * draw_call)(void *, int, int, int, int, int);
    ((draw_call)game_at(0x4b8830))(game_at(0x8c5568), a, b, c, d, 0);
    return 0;
}
static const struct {
    const char *name;
    int (*fn)(int, int, int, int);
} host_table[] = {{"status_look", status_look}, {"status_tile", status_tile}, {"status_stack", status_stack}, {"status_base", status_base}, {"status_draw", status_draw}};
static int host_pending = -1, host_args[4];

void scene_host(const char *name, int a, int b, int c, int d) {
    for (size_t i = 0; i < sizeof(host_table) / sizeof(host_table[0]); i++)
        if (!strcmp(host_table[i].name, name)) {
            host_pending = (int)i;
            host_args[0] = a, host_args[1] = b, host_args[2] = c, host_args[3] = d;
            return;
        }
    fprintf(stderr, "chiron: script: no host step \"%s\"\n", name);
}

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
    if (host_pending >= 0 && !hooks_protecting() && game_at(PREF_BASE)) {
        int i = host_pending;
        host_pending = -1;
        fprintf(stderr, "chiron: script: host %s\n", host_table[i].name);
        int answer = host_table[i].fn(host_args[0], host_args[1], host_args[2], host_args[3]);
        fprintf(stderr, "chiron: script: host %s answered %d\n", host_table[i].name, answer);
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
