// Shadow mode only: hooks that keep the game quiet while a check runs or a
// sweep's copy of the game is in use.
//
// The game's message pump and drawing do nothing then, so the original and
// ours see the same state, no input is handled twice or lost to a rewind,
// and neither run paints over what the other left in the windows, on the
// heap, which checks don't put back. (Placing a unit, such as a fungal
// tower, reveals and repaints the squares around it at once.) Map
// generation redraws less often as a result.
//
// Messages, sounds and movies are held back instead (hooks_ui_hold): a
// check compares what each run would have shown, and the original's are
// shown once the check is over. The text a message fills in (parse_say and
// the like) is held with it, in order, as checks don't put back the game's
// text variables either.
#include <stdio.h>

#include "diplomacy.h"
#include "game.h"
#include "quiet.h"
#include "shadow.h"

// What a held question answers (0 unless a sweep says otherwise), and
// whether a sweep's menu reads script.txt (see quiet.h).
int quiet_answer, quiet_text;

#define QUIET(name, label, params, args)                   \
    static void (*orig_##name) params;                     \
    static struct hook_stats stats_##name = {label};       \
    static void shadow_##name params {                     \
        if (hooks_protecting()) {                          \
            stats_##name.calls++;                          \
            return;                                        \
        }                                                  \
        orig_##name args;                                  \
    }
QUIET(do_all_non_input, "(quiet pump)", (void), ())
QUIET(do_all_draws, "(quiet paint)", (void), ())
QUIET(draw_radius, "(quiet draw_radius)", (int x, int y, int radius, int unk), (x, y, radius, unk))
QUIET(draw_tile, "(quiet draw_tile)", (int x, int y, int unk), (x, y, unk))
QUIET(draw_map, "(quiet draw_map)", (int unk), (unk))
static void(__attribute__((thiscall)) * orig_map_win_clear_terrain)(void *win);
static struct hook_stats stats_map_win_clear_terrain = {"(quiet MapWin_clear_terrain)"};
static void __attribute__((thiscall)) shadow_map_win_clear_terrain(void *win) {
    if (hooks_protecting()) {
        stats_map_win_clear_terrain.calls++;
        return;
    }
    orig_map_win_clear_terrain(win);
}

static void(__attribute__((thiscall)) * orig_map_win_clear_alt)(void *win, int x, int y, int unk);
static struct hook_stats stats_map_win_clear_alt = {"(quiet MapWin_clear_alt)"};
static void __attribute__((thiscall)) shadow_map_win_clear_alt(void *win, int x, int y, int unk) {
    if (hooks_protecting()) {
        stats_map_win_clear_alt.calls++;
        return;
    }
    orig_map_win_clear_alt(win, x, y, unk);
}

QUIET(draw_tiles, "(quiet draw_tiles)", (int x, int y, int unk), (x, y, unk))
static void(__attribute__((thiscall)) * orig_status_win_redraw)(void *win);
static struct hook_stats stats_status_win_redraw = {"(quiet StatusWin_redraw)"};
static void __attribute__((thiscall)) shadow_status_win_redraw(void *win) {
    if (hooks_protecting()) {
        stats_status_win_redraw.calls++;
        return;
    }
    orig_status_win_redraw(win);
}

// A held call: `fill` puts the arguments in c, `replay` makes the call from
// them. Returns `quiet` while held.
#define HELD(ret, fn, quiet, params, args, fill, replay_call)                \
    static ret (*orig_##fn) params;                                           \
    static struct hook_stats stats_##fn = {"(held " #fn ")"};              \
    static void replay_##fn(const struct hook_ui_call *c) {                  \
        const int *a_ = c->arg;                                                 \
        (void)a_;                                                               \
        replay_call;                                                            \
    }                                                                           \
    static ret shadow_##fn params {                                           \
        if (!hooks_protecting()) return orig_##fn args;                       \
        stats_##fn.calls++;                                                   \
        struct hook_ui_call c_ = {.name = #fn, .replay = replay_##fn};      \
        fill;                                                                   \
        hooks_ui_hold(&c_);                                                     \
        return quiet;                                                           \
    }

// Originals the talks call that read stack slots they never set (the player's
// menu, a pledge, the endings of the talks): called from communicate()'s
// frame by the original and from ours by the sweep's check, they would see
// different leftovers. In shadow mode they run on a cleared stack, so that
// both runs see the same.
#define SCRUBBED(name)                                                \
    static void *orig_##name;                                          \
    static struct hook_stats stats_##name = {"(scrubbed " #name ")"}; \
    void shadow_##name(void);                                          \
    __asm__(".text\n"                                                 \
            ".globl shadow_" #name "\n"                               \
            "shadow_" #name ":\n"                                     \
            "    pushl %edi\n"                                        \
            "    pushl %ecx\n"                                        \
            "    pushl %eax\n"                                        \
            "    leal -0x400(%esp), %edi\n"                           \
            "    movl $0x100, %ecx\n"                                 \
            "    xorl %eax, %eax\n"                                   \
            "    cld\n"                                               \
            "    rep stosl\n"                                         \
            "    popl %eax\n"                                         \
            "    popl %ecx\n"                                         \
            "    popl %edi\n"                                         \
            "    jmp *orig_" #name "\n")
SCRUBBED(diplomacy_menu);
SCRUBBED(pledge_truce);
SCRUBBED(diplomacy_ends);
SCRUBBED(net_loan);
SCRUBBED(net_maps);
SCRUBBED(diplomacy_caption);

// The diplomacy window's dialogs (X_pops_11), in a sweep's copies of the
// game: the player's answers to the talks (diplomacy.c) the sweep makes
// up, held and answered from quiet_talk_answers in turn (0 once they run
// out), so that both runs of a check get the same ones; the check resets
// quiet_talk_next for each run. The Datalinks a player may open from one
// (help_topic) and the phrases read from a faction's file
// (get_noun_phrase, get_phrase, which open it, allocating) are held too,
// a phrase answered with its arguments. Outside a sweep they run as they
// are: a check of a game call that reached them would be reported by the
// heap guard.
int quiet_talk_answers[QUIET_TALK_ANSWERS], quiet_talk_next;
static int talk_held(void) { return hooks_sweeping && hooks_protecting(); }
static int (*orig_x_pops_11)(char *label, int flags, void *sprite, void *fn);
static struct hook_stats stats_x_pops_11 = {"(held x_pops_11)"};
// Held only in a sweep, whose held calls are dropped, never made.
static void replay_dropped(const struct hook_ui_call *c) {}
// In a check of a game call (the Council's messages), a leader's message
// is held like the others: answered 0, the original's made after.
static void replay_x_pops_11(const struct hook_ui_call *c) {
    orig_x_pops_11(c->has_text & 1 ? (char *)c->text[0] : NULL, c->arg[0], (void *)(intptr_t)c->arg[1],
                   (void *)(intptr_t)c->arg[2]);
}
static int shadow_x_pops_11(char *label, int flags, void *sprite, void *fn) {
    if (!hooks_protecting()) return orig_x_pops_11(label, flags, sprite, fn);
    stats_x_pops_11.calls++;
    struct hook_ui_call c = {.name = "x_pops_11",
                             .replay = talk_held() ? replay_dropped : replay_x_pops_11,
                             .arg = {flags, (int)(intptr_t)sprite, (int)(intptr_t)fn}};
    hooks_ui_text(&c, 0, label);
    hooks_ui_hold(&c);
    if (!talk_held()) return 0;
    return quiet_talk_next < QUIET_TALK_ANSWERS ? quiet_talk_answers[quiet_talk_next++] : 0;
}
// The same for a message read from a faction's own file (X_pops_13), which
// the AI's offers of a truce or a treaty use.
static int (*orig_x_pops_13)(char *file, char *label, int flags, void *sprite, void *fn);
static struct hook_stats stats_x_pops_13 = {"(held x_pops_13)"};
static void replay_x_pops_13(const struct hook_ui_call *c) {
    orig_x_pops_13(c->has_text & 2 ? (char *)c->text[1] : NULL, c->has_text & 1 ? (char *)c->text[0] : NULL, c->arg[0],
                   (void *)(intptr_t)c->arg[1], (void *)(intptr_t)c->arg[2]);
}
static int shadow_x_pops_13(char *file, char *label, int flags, void *sprite, void *fn) {
    if (!hooks_protecting()) return orig_x_pops_13(file, label, flags, sprite, fn);
    stats_x_pops_13.calls++;
    struct hook_ui_call c = {.name = "x_pops_13",
                             .replay = talk_held() ? replay_dropped : replay_x_pops_13,
                             .arg = {flags, (int)(intptr_t)sprite, (int)(intptr_t)fn}};
    hooks_ui_text(&c, 0, label);
    hooks_ui_text(&c, 1, file);
    hooks_ui_hold(&c);
    if (!talk_held()) return 0;
    return quiet_talk_next < QUIET_TALK_ANSWERS ? quiet_talk_answers[quiet_talk_next++] : 0;
}
static void (*orig_help_topic)(int kind, int id);
static struct hook_stats stats_help_topic = {"(swept help_topic)"};
static void shadow_help_topic(int kind, int id) {
    if (!talk_held()) {
        orig_help_topic(kind, id);
        return;
    }
    stats_help_topic.calls++;
    struct hook_ui_call c = {.name = "help_topic", .replay = replay_dropped, .arg = {kind, id}};
    hooks_ui_hold(&c);
}
#define TALK_PHRASE(fn)                                                                        \
    static char *(*orig_##fn)(int faction, int unk);                                           \
    static struct hook_stats stats_##fn = {"(swept " #fn ")"};                                 \
    static char *shadow_##fn(int faction, int unk) {                                           \
        static char phrase[32];                                                                \
        if (!talk_held()) return orig_##fn(faction, unk);                                     \
        stats_##fn.calls++;                                                                    \
        struct hook_ui_call c = {.name = #fn, .replay = replay_dropped, .arg = {faction, unk}}; \
        hooks_ui_hold(&c);                                                                     \
        snprintf(phrase, sizeof(phrase), "(" #fn " %d %d)", faction, unk);                     \
        return phrase;                                                                         \
    }
TALK_PHRASE(get_noun_phrase)
TALK_PHRASE(get_phrase)

// The held copy of string argument i, or NULL.
#define TEXT(i) (c->has_text >> (i) & 1 ? (char *)c->text[i] : NULL)
#define PTR(i) ((void *)(intptr_t)a_[i])

// Messages: a box, or a line in the message bar, for the player's own bases.
HELD(int, popb, 0, (char *label, int flags, int sound, char *picture, void *sprite),
     (label, flags, sound, picture, sprite),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, picture), c_.arg[0] = flags, c_.arg[1] = sound,
      c_.arg[2] = (int)(intptr_t)sprite),
     orig_popb(TEXT(0), a_[0], a_[1], TEXT(1), PTR(2)))
HELD(int, popp, quiet_answer, (char *file, char *label, int flags, char *picture, void *fn), (file, label, flags, picture, fn),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, file), hooks_ui_text(&c_, 2, picture), c_.arg[0] = flags,
      c_.arg[1] = (int)(intptr_t)fn),
     orig_popp(TEXT(1), TEXT(0), a_[0], TEXT(2), PTR(1)))
HELD(int, x_pop2, 0, (char *label, char *picture, int unk), (label, picture, unk),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, picture), c_.arg[0] = unk),
     orig_x_pop2(TEXT(0), TEXT(1), a_[0]))
// A message without a picture, as for a tech that wins a free one.
HELD(int, x_pop, 0, (char *label, int unk), (label, unk), (hooks_ui_text(&c_, 0, label), c_.arg[0] = unk),
     orig_x_pop(TEXT(0), a_[0]))
// A message with a faction leader's portrait, as when a probe team's
// victim lets it go, and the intelligence report a probe team's
// infiltration opens (a window, which allocates from the game's heap).
// In a sweep's talks (quiet_talk_pops set), one answers like the leader's
// messages (x_pops_11), from quiet_talk_answers.
int quiet_talk_pops;
static int x_pops_answer(void) {
    if (!quiet_talk_pops || !hooks_sweeping) return 0;
    return quiet_talk_next < QUIET_TALK_ANSWERS ? quiet_talk_answers[quiet_talk_next++] : 0;
}
HELD(int, x_pops, x_pops_answer(), (char *label, void *sprite, void *fn), (label, sprite, fn),
     (hooks_ui_text(&c_, 0, label), c_.arg[0] = (int)(intptr_t)sprite, c_.arg[1] = (int)(intptr_t)fn),
     orig_x_pops(TEXT(0), PTR(0), PTR(1)))
HELD(void, report_intel, (void)0, (int faction), (faction), (c_.arg[0] = faction), orig_report_intel(a_[0]))
// The text variables a message fills in: a number, a string from the
// string table, or a string.
HELD(int, parse_num, slot > 9 ? 3 : 0, (int slot, int value), (slot, value), (c_.arg[0] = slot, c_.arg[1] = value),
     orig_parse_num(a_[0], a_[1]))
HELD(int, parse_say, slot > 9 ? 3 : 0, (int slot, int text, int gender, int plural), (slot, text, gender, plural),
     (c_.arg[0] = slot, c_.arg[1] = text, c_.arg[2] = gender, c_.arg[3] = plural),
     orig_parse_say(a_[0], a_[1], a_[2], a_[3]))
HELD(int, parse_says, !text || slot > 9 ? 3 : 0, (int slot, char *text, int gender, int plural),
     (slot, text, gender, plural), (hooks_ui_text(&c_, 0, text), c_.arg[0] = slot, c_.arg[1] = gender, c_.arg[2] = plural),
     orig_parse_says(a_[0], TEXT(0), a_[1], a_[2]))
// Sounds, and the movies that play when a secret project is completed.
HELD(void, wave_it, (void)0, (int sound), (sound), (c_.arg[0] = sound), orig_wave_it(a_[0]))
HELD(void, interlude, (void)0, (int id, char *text, int unk1, int unk2), (id, text, unk1, unk2),
     (hooks_ui_text(&c_, 0, text), c_.arg[0] = id, c_.arg[1] = unk1, c_.arg[2] = unk2),
     orig_interlude(a_[0], TEXT(0), a_[1], a_[2]))
// Ecological damage: a message, an explosion and a pause, the last of
// which, waiting on the clock a check holds still, would never end.
HELD(int, pop2, 0, (char *label, char *picture, int base), (label, picture, base),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, picture), c_.arg[0] = base),
     orig_pop2(TEXT(0), TEXT(1), a_[0]))
HELD(void, boom, (void)0, (int x, int y, int flags), (x, y, flags), (c_.arg[0] = x, c_.arg[1] = y, c_.arg[2] = flags),
     orig_boom(a_[0], a_[1], a_[2]))
HELD(void, clock_wait, (void)0, (int ms), (ms), (c_.arg[0] = ms), orig_clock_wait(a_[0]))
HELD(int, ambience, 0, (int sound), (sound), (c_.arg[0] = sound), orig_ambience(a_[0]))
// A sound at a square, as a missile's launch: held, it answers 0.
static int(__attribute__((thiscall)) * orig_fx_play_loc)(void *fx, int sound, int x, int y);
static struct hook_stats stats_fx_play_loc = {"(held fx_play_loc)"};
static void replay_fx_play_loc(const struct hook_ui_call *c) {
    orig_fx_play_loc((void *)(intptr_t)c->arg[3], c->arg[0], c->arg[1], c->arg[2]);
}
static int __attribute__((thiscall)) shadow_fx_play_loc(void *fx, int sound, int x, int y) {
    if (!hooks_protecting()) return orig_fx_play_loc(fx, sound, x, y);
    stats_fx_play_loc.calls++;
    struct hook_ui_call c = {.name = "fx_play_loc", .replay = replay_fx_play_loc, .arg = {sound, x, y, (int)(intptr_t)fx}};
    hooks_ui_hold(&c);
    return 0;
}

// Moving the map to a square: held, it answers 0 (not already in view).
static int(__attribute__((thiscall)) * orig_console_focus)(void *console, int x, int y, int faction);
static struct hook_stats stats_console_focus = {"(held console_focus)"};
static void replay_console_focus(const struct hook_ui_call *c) {
    orig_console_focus((void *)(intptr_t)c->arg[3], c->arg[0], c->arg[1], c->arg[2]);
}
static int __attribute__((thiscall)) shadow_console_focus(void *console, int x, int y, int faction) {
    if (!hooks_protecting()) return orig_console_focus(console, x, y, faction);
    stats_console_focus.calls++;
    struct hook_ui_call c = {.name = "console_focus", .replay = replay_console_focus, .arg = {x, y, faction, (int)(intptr_t)console}};
    hooks_ui_hold(&c);
    return 0;
}

// The tutorial's note on something first seen on the map (encounter(),
// from spot_all()): its window builds a popup, which allocates from the
// game's heap. Held, it answers 0.
static int(__attribute__((thiscall)) * orig_tut_map)(void *win, char *label, int x, int y, int a, void *sprite, int b,
                                                     int x2, int y2);
static struct hook_stats stats_tut_map = {"(held tut_map)"};
static void replay_tut_map(const struct hook_ui_call *c) {
    const int *a = c->arg;
    orig_tut_map((void *)(intptr_t)a[7], c->has_text & 1 ? (char *)c->text[0] : NULL, a[0], a[1], a[2],
                 (void *)(intptr_t)a[3], a[4], a[5], a[6]);
}
static int __attribute__((thiscall)) shadow_tut_map(void *win, char *label, int x, int y, int a, void *sprite, int b,
                                                    int x2, int y2) {
    if (!hooks_protecting()) return orig_tut_map(win, label, x, y, a, sprite, b, x2, y2);
    stats_tut_map.calls++;
    struct hook_ui_call c = {.name = "tut_map", .replay = replay_tut_map,
                             .arg = {x, y, a, (int)(intptr_t)sprite, b, x2, y2, (int)(intptr_t)win}};
    if (label) hooks_ui_text(&c, 0, label);
    hooks_ui_hold(&c);
    return 0;
}

// The popup base_production() keeps on its stack: while state is protected
// its dialogs are dropped, as the object is gone by the time they could be
// shown.
#define QUIET_THIS(ret, fn, quiet, params, args)                                  \
    static ret(__attribute__((thiscall)) * orig_##fn) params;                     \
    static struct hook_stats stats_##fn = {"(quiet " #fn ")"};                   \
    static ret __attribute__((thiscall)) shadow_##fn params {                     \
        if (!hooks_protecting()) return orig_##fn args;                           \
        stats_##fn.calls++;                                                       \
        return quiet;                                                             \
    }
QUIET_THIS(int, popup_start, 0, (void *pop, char *file, char *label, int a, int b, int c, int d),
           (pop, file, label, a, b, c, d))
QUIET_THIS(void, base_pop_init, (void)0, (void *pop, int a, int b), (pop, a, b))
QUIET_THIS(void, base_pop_set_width, (void)0, (void *pop, int w), (pop, w))
QUIET_THIS(void, base_pop_string, (void)0, (void *pop, char *s), (pop, s))
// An item added to a dialog's list (call_council() lists its proposals
// even for the AI), and a leader's portrait put in such a dialog
// (buy_council_vote()): they allocate.
QUIET_THIS(int, dialogs_item, 0, (void *dialogs, char *text, int id), (dialogs, text, id))
QUIET_THIS(void, sprite_box_sprite, (void)0, (void *box, void *sprite, int a, int b), (box, sprite, a, b))
// A menu's choice: the next of quiet_menu_answers while a sweep lists
// some (several menus in one call), else quiet_answer.
int quiet_menu_answers[QUIET_MENU_ANSWERS], quiet_menu_count, quiet_menu_next;
static int menu_answer(void) {
    if (!quiet_menu_count) return quiet_answer;
    return quiet_menu_next < quiet_menu_count ? quiet_menu_answers[quiet_menu_next++] : -1;
}
QUIET_THIS(int, base_pop_exec_3, menu_answer(), (void *pop, int a, int b), (pop, a, b))
// The planet window, where the talks' base menus and battle plans show
// bases and lines between them: it draws.
QUIET_THIS(void, plan_win_init, (void)0, (void *win, int a), (win, a))
QUIET_THIS(void, plan_win_find_view, (void)0, (void *win, int a, int b, int c, int d, int e, int f, int g, int h),
           (win, a, b, c, d, e, f, g, h))
QUIET_THIS(void, plan_win_back_redraw, (void)0, (void *win), (win))
QUIET_THIS(void, plan_win_add_line_2, (void)0, (void *win, int a, int b, int c), (win, a, b, c))
QUIET_THIS(void, plan_win_monocenter, (void)0, (void *win, int x, int y, int a), (win, x, y, a))
QUIET_THIS(void, plan_win_on_redraw, (void)0, (void *win), (win))
QUIET_THIS(void, plan_win_close, (void)0, (void *win), (win))
QUIET_THIS(void, win_show, (void)0, (void *win, int a), (win, a))
// A list box shown again after its list changed (base_kill's messages),
// and the base window told a base is gone.
QUIET_THIS(int, listbox_update_changes, 7, (void *box), (box))
QUIET_THIS(void, base_win_base_killed, (void)0, (void *win, int base), (win, base))
// A picture for such a dialog: reported missing, as loading it would
// allocate from the game's heap.
static int (*orig_pcx_dimensions)(char *file, int *w, int *h);
static struct hook_stats stats_pcx_dimensions = {"(quiet pcx_dimensions)"};
static int shadow_pcx_dimensions(char *file, int *w, int *h) {
    if (!hooks_protecting()) return orig_pcx_dimensions(file, w, h);
    stats_pcx_dimensions.calls++;
    return 1;
}

// Calls on one of the game's own objects, held like the others: the object
// is kept in arg[4].
#define HELD_THIS(fn, params, args, fill, replay_call)                            \
    static void(__attribute__((thiscall)) * orig_##fn) params;                    \
    static struct hook_stats stats_##fn = {"(held " #fn ")"};                    \
    static void replay_##fn(const struct hook_ui_call *c) {                      \
        const int *a_ = c->arg;                                                   \
        (void)a_;                                                                 \
        replay_call;                                                              \
    }                                                                             \
    static void __attribute__((thiscall)) shadow_##fn params {                    \
        if (!hooks_protecting()) {                                                \
            orig_##fn args;                                                       \
            return;                                                               \
        }                                                                         \
        stats_##fn.calls++;                                                       \
        struct hook_ui_call c_ = {.name = #fn, .replay = replay_##fn};           \
        c_.arg[4] = (int)(intptr_t)self;                                          \
        fill;                                                                     \
        hooks_ui_hold(&c_);                                                       \
    }
#define SELF ((void *)(intptr_t)a_[4])
// The console's refresh after a unit is upgraded (the info and status
// windows), and its centring on a lost base.
HELD_THIS(console_update_data, (void *self, int unk), (self, unk), c_.arg[0] = unk, orig_console_update_data(SELF, a_[0]))
HELD_THIS(console_center, (void *self), (self), (void)0, orig_console_center(SELF))
// Focusing the map on a base the talks show.
HELD_THIS(map_win_focus, (void *self, int x, int y), (self, x, y), (c_.arg[0] = x, c_.arg[1] = y),
          orig_map_win_focus(SELF, a_[0], a_[1]))
// Centring the map on a base that changed hands.
HELD_THIS(map_win_set_center, (void *self, int x, int y, int unk), (self, x, y, unk),
          (c_.arg[0] = x, c_.arg[1] = y, c_.arg[2] = unk), orig_map_win_set_center(SELF, a_[0], a_[1], a_[2]))
HELD_THIS(graphic_win_redraw, (void *self), (self), (void)0, orig_graphic_win_redraw(SELF))
HELD_THIS(wave_unload, (void *self), (self), (void)0, orig_wave_unload(SELF))
HELD_THIS(wave_load, (void *self, char *file), (self, file), hooks_ui_text(&c_, 0, file), orig_wave_load(SELF, TEXT(0)))
HELD_THIS(wave_play, (void *self), (self), (void)0, orig_wave_play(SELF))
HELD_THIS(sound_fade, (void *self, int ms), (self, ms), c_.arg[0] = ms, orig_sound_fade(SELF, a_[0]))
// The base window after a base's upkeep: redrawn, or opened on the base.
HELD_THIS(base_win_on_redraw, (void *self), (self), (void)0, orig_base_win_on_redraw(SELF))
HELD_THIS(base_win_zoom, (void *self, int base, int unk), (self, base, unk), (c_.arg[0] = base, c_.arg[1] = unk),
          orig_base_win_zoom(SELF, a_[0], a_[1]))
// The base window told of a unit killed: redrawn if it shows its home
// base or the square.
HELD_THIS(base_win_check_base, (void *self, int base), (self, base), c_.arg[0] = base, orig_base_win_check_base(SELF, a_[0]))
HELD_THIS(base_win_check_loc, (void *self, int x, int y, int unk), (self, x, y, unk),
          (c_.arg[0] = x, c_.arg[1] = y, c_.arg[2] = unk), orig_base_win_check_loc(SELF, a_[0], a_[1], a_[2]))
// And of a unit whose state changed, as a former's work.
HELD_THIS(base_win_check_veh, (void *self, int veh), (self, veh), c_.arg[0] = veh, orig_base_win_check_veh(SELF, a_[0]))
// A notice in the message bar, such as a unit's promotion.
HELD_THIS(netmsg_pop, (void *self, char *label, int delay, int unk, char *sound), (self, label, delay, unk, sound),
          (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, sound), c_.arg[0] = delay, c_.arg[1] = unk),
          orig_netmsg_pop(SELF, TEXT(0), a_[0], a_[1], TEXT(1)))
// Combat: an interceptor shown flying to its target, which waits on the
// clock, and a sound effect (a planet buster's). An attacker's step
// towards its target is dropped instead while combat.c sets
// quiet_drop_scoot: the battle takes the unit out of its stack for it,
// which the step expects, so shown afterwards it would fail.
int quiet_drop_scoot;
static void (*orig_veh_scoot)(int veh, int x, int y, int dir, int unk);
static struct hook_stats stats_veh_scoot = {"(held veh_scoot)"};
static void replay_veh_scoot(const struct hook_ui_call *c) {
    orig_veh_scoot(c->arg[0], c->arg[1], c->arg[2], c->arg[3], c->arg[5]);
}
static void shadow_veh_scoot(int veh, int x, int y, int dir, int unk) {
    if (!hooks_protecting()) {
        orig_veh_scoot(veh, x, y, dir, unk);
        return;
    }
    stats_veh_scoot.calls++;
    if (quiet_drop_scoot) return;
    struct hook_ui_call c = {.name = "veh_scoot", .replay = replay_veh_scoot, .arg = {veh, x, y, dir, 0, unk}};
    hooks_ui_hold(&c);
}
HELD_THIS(fx_play, (void *self, int id), (self, id), c_.arg[0] = id, orig_fx_play(SELF, a_[0]))
// Pods and monoliths: a sound faded out or stopped, and a square drawn with
// its radar range.
HELD_THIS(fx_fade, (void *self, int id), (self, id), c_.arg[0] = id, orig_fx_fade(SELF, a_[0]))
HELD_THIS(fx_stop, (void *self, int id), (self, id), c_.arg[0] = id, orig_fx_stop(SELF, a_[0]))
// A unit's step (order_veh): the map's cursor moved on with a unit shown
// quickly, and the engine's pitch for a slow one.
HELD_THIS(console_cursor_next, (void *self, int x, int y), (self, x, y), (c_.arg[0] = x, c_.arg[1] = y),
          orig_console_cursor_next(SELF, a_[0], a_[1]))
HELD_THIS(council_win_set_vote, (void *self, int faction, int vote), (self, faction, vote),
          (c_.arg[0] = faction, c_.arg[1] = vote), orig_council_win_set_vote(SELF, a_[0], a_[1]))
HELD_THIS(fx_engine_pitch, (void *self, int pitch), (self, pitch), c_.arg[0] = pitch, orig_fx_engine_pitch(SELF, a_[0]))
// Movies and help for secret projects.
HELD(void, amovie_project, (void)0, (int project), (project), (c_.arg[0] = project), orig_amovie_project(a_[0]))
HELD(void, amovie_project_2, (void)0, (char *name), (name), (hooks_ui_text(&c_, 0, name)), orig_amovie_project_2(TEXT(0)))
HELD(void, help_project, (void)0, (int project), (project), (c_.arg[0] = project), orig_help_project(a_[0]))

// The monuments the game keeps of what was built: the Monuments window
// they update allocates from the game's heap, whose bookkeeping a check
// would put back without the memory it hands out, so they are held too.
#define HELD_MON(fn)                                                                                          \
    HELD(void, fn, (void)0, (int faction), (faction), (c_.arg[0] = faction), orig_##fn(a_[0]))
HELD_MON(mon_prototype_built)
HELD_MON(mon_native_life_bred)
HELD_MON(mon_naval_unit_built)
HELD_MON(mon_air_unit_built)
HELD_MON(mon_first_in_space)
HELD_MON(mon_built_preserve)
// The base's name is passed as it was: the game may keep the pointer.
HELD(void, mon_facility_built, (void)0, (int faction, char *name), (faction, name),
     (c_.arg[0] = faction, c_.arg[1] = (int)(intptr_t)name), orig_mon_facility_built(a_[0], PTR(1)))
HELD(void, mon_secret_project, (void)0, (int faction, int project), (faction, project),
     (c_.arg[0] = faction, c_.arg[1] = project), orig_mon_secret_project(a_[0], a_[1]))
HELD(void, mon_winning_trans, (void)0, (int faction, int other), (faction, other),
     (c_.arg[0] = faction, c_.arg[1] = other), orig_mon_winning_trans(a_[0], a_[1]))
HELD(void, mon_colony_founded, (void)0, (int faction, char *name), (faction, name),
     (c_.arg[0] = faction, c_.arg[1] = (int)(intptr_t)name), orig_mon_colony_founded(a_[0], PTR(1)))
// A base conquered (its name on the caller's stack, so copied) and a
// faction eliminated, from a unit's step.
HELD(void, mon_conquer_base, (void)0, (int faction, char *name), (faction, name),
     (c_.arg[0] = faction, hooks_ui_text(&c_, 0, name)), orig_mon_conquer_base(a_[0], TEXT(0)))
HELD(void, mon_killed_faction, (void)0, (int faction, int other), (faction, other),
     (c_.arg[0] = faction, c_.arg[1] = other), orig_mon_killed_faction(a_[0], a_[1]))
// The debugging log (a line appended to its file, opened each time,
// which allocates): capture_base() and setup_player() write to it.
HELD(void, log_say, (void)0, (char *text, int a, int b, int c), (text, a, b, c),
     (hooks_ui_text(&c_, 0, text), c_.arg[0] = a, c_.arg[1] = b, c_.arg[2] = c), orig_log_say(TEXT(0), a_[0], a_[1], a_[2]))
HELD(void, log_say5, (void)0, (char *text, char *text2, int a, int b, int c), (text, text2, a, b, c),
     (hooks_ui_text(&c_, 0, text), hooks_ui_text(&c_, 1, text2), c_.arg[0] = a, c_.arg[1] = b, c_.arg[2] = c),
     orig_log_say5(TEXT(0), TEXT(1), a_[0], a_[1], a_[2]))
// The game's end (end_of_game, checked so in the score sweep only): the
// credits, the score screen, the faction's epitaph, the hall of fame, the
// replay, and the monument to the win. All are windows, which allocate.
HELD(void, show_credits, (void)0, (void), (), (void)0, orig_show_credits())
HELD(void, show_replay, (void)0, (void), (), (void)0, orig_show_replay())
HELD(void, report_score, (void)0, (int final), (final), (c_.arg[0] = final), orig_report_score(a_[0]))
HELD(void, quayle, (void)0, (int faction), (faction), (c_.arg[0] = faction), orig_quayle(a_[0]))
HELD(void, hall_of_fame, (void)0, (int final), (final), (c_.arg[0] = final), orig_hall_of_fame(a_[0]))
HELD(void, mon_winning_unify, (void)0, (int faction, int other), (faction, other),
     (c_.arg[0] = faction, c_.arg[1] = other), orig_mon_winning_unify(a_[0], a_[1]))
// The text a capture's interlude read, freed (a base renamed for the
// Assistant Worm's killer, in capture_base()).
HELD(void, text_close, (void)0, (void), (), (void)0, orig_text_close())

// A tech given away (by a secret project), with its announcement and the
// window that shows it, which allocates as the monuments do.
HELD(void, tech_advance, (void)0, (int faction), (faction), (c_.arg[0] = faction), orig_tech_advance(a_[0]))
// A tech gained some other way, as by the AI's trades in diplomacy.
HELD(void, tech_achieved, (void)0, (int faction, int tech, int from, int steal), (faction, tech, from, steal),
     (c_.arg[0] = faction, c_.arg[1] = tech, c_.arg[2] = from, c_.arg[3] = steal),
     orig_tech_achieved(a_[0], a_[1], a_[2], a_[3]))
// Research paid in, which may discover a tech in the same way.
HELD(void, tech_research, (void)0, (int faction, int labs), (faction, labs), (c_.arg[0] = faction, c_.arg[1] = labs),
     orig_tech_research(a_[0], a_[1]))

// The Planetary Council's session, whose window allocates (call_council()
// opens it). The game calls it last in a faction's turn but for the
// autosave, which a check of faction_upkeep() leaves out, so made after the
// check it comes in order. A vote cast while the window is open (bought in
// buy_council_vote()) is told to it, which may end the session.
HELD(void, council, (void)0, (int faction, int prop, int target), (faction, prop, target),
     (c_.arg[0] = faction, c_.arg[1] = prop, c_.arg[2] = target), orig_council(a_[0], a_[1], a_[2]))

// A menu read from script.txt, while a sweep sets quiet_text: not opened,
// and no line read (each item is the line last read), so that the menu's
// answer (quiet_answer) can be checked.
static int (*orig_x_text_open)(char *file, char *label);
static struct hook_stats stats_x_text_open = {"(quiet x_text_open)"};
static int shadow_x_text_open(char *file, char *label) {
    if (!quiet_text || !hooks_protecting()) return orig_x_text_open(file, label);
    stats_x_text_open.calls++;
    return 0;
}
static char *(*orig_text_get)(void);
static struct hook_stats stats_text_get = {"(quiet text_get)"};
static char *shadow_text_get(void) {
    if (!quiet_text || !hooks_protecting()) return orig_text_get();
    stats_text_get.calls++;
    return *game_text_line;
}

// The battle (battle_fight_2): the status and battle windows drawn, the
// map squares around the fight redrawn, and input put aside. The battle
// window made the interface's mode, and its timer restarted after a
// question, are held, so that they come once and in order; so are the
// preferences saved (after a pause the player may turn off), and the
// monument to an enemy destroyed, which updates the Monuments window as
// the others do.
QUIET(flush_input, "(quiet flush_input)", (void), ())
QUIET(draw_tile_fixup, "(quiet draw_tile_fixup)", (int x, int y, int a, int b), (x, y, a, b))
QUIET(draw_tile_fixup_2, "(quiet draw_tile_fixup_2)", (int x1, int y1, int x2, int y2, int a, int b), (x1, y1, x2, y2, a, b))
QUIET_THIS(void, status_win_draw, (void)0, (void *win, int veh_att, int veh_def, int off, int def, int combat),
           (win, veh_att, veh_def, off, def, combat))
QUIET_THIS(void, status_win_reset, (void)0, (void *win), (win))
QUIET_THIS(void, battle_win_clear, (void)0, (void *win), (win))
HELD_THIS(set_iface_mode, (void *self), (self), (void)0, orig_set_iface_mode(SELF))
HELD_THIS(battle_win_pulse_timer, (void *self), (self), (void)0, orig_battle_win_pulse_timer(SELF))
HELD(void, prefs_save, (void)0, (int unk), (unk), (c_.arg[0] = unk), orig_prefs_save(a_[0]))
HELD(void, mon_enemy_destroyed, (void)0, (int faction, int other), (faction, other),
     (c_.arg[0] = faction, c_.arg[1] = other), orig_mon_enemy_destroyed(a_[0], a_[1]))
// A new game writes two settings to the preferences file and deletes the
// undo saves: held, so a sweep's checks of it leave the files alone.
HELD(void, prefs_put, (void)0, (char *key, char *value), (key, value),
     (hooks_ui_text(&c_, 0, key), hooks_ui_text(&c_, 1, value)), orig_prefs_put(TEXT(0), TEXT(1)))
HELD(void, prefs_put_2, (void)0, (char *key, int value, int flag), (key, value, flag),
     (hooks_ui_text(&c_, 0, key), c_.arg[0] = value, c_.arg[1] = flag), orig_prefs_put_2(TEXT(0), a_[0], a_[1]))
HELD(void, wipe_undo, (void)0, (void), (), (void)0, orig_wipe_undo())

// The heap guard: nothing may allocate from the game's heap, or free to
// it, while state is protected, as a check puts back the heap's
// bookkeeping but not the memory it hands out. Any that does counts as a
// mismatch, and the first few say where they were called from.
void modules_scan_stack(uint32_t esp) __attribute__((weak));
void *orig_heap_malloc, *orig_heap_free;
static struct hook_stats stats_heap_malloc = {"(heap guard: malloc)"}, stats_heap_free = {"(heap guard: free)"};
void heap_guard(int is_free, uint32_t esp);
void heap_guard(int is_free, uint32_t esp) {
    if (!hooks_protecting()) return;
    struct hook_stats *s = is_free ? &stats_heap_free : &stats_heap_malloc;
    s->calls++;
    if (++s->mismatches > 3) return;
    fprintf(stderr, "chiron: shadow mismatch: game heap %s while state is protected%s%s%s\n", is_free ? "free" : "malloc",
            hooks_sweep_module ? " (in the " : "", hooks_sweep_module ? hooks_sweep_module : "",
            hooks_sweep_module ? " sweep)" : "");
    if (modules_scan_stack) modules_scan_stack(esp);
}
// Entered in place of malloc and free: registers kept, then on to them.
void shadow_heap_malloc(void), shadow_heap_free(void);
#define HEAP_STUB(name, n)                   \
    __asm__(".text\n"                        \
            "shadow_" #name ":\n"            \
            "    pushal\n"                   \
            "    leal 32(%esp), %eax\n"      \
            "    pushl %eax\n"               \
            "    pushl $" #n "\n"            \
            "    call heap_guard\n"          \
            "    addl $8, %esp\n"            \
            "    popal\n"                    \
            "    jmp *orig_" #name "\n")
HEAP_STUB(heap_malloc, 0);
HEAP_STUB(heap_free, 1);

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
#define QUIET_HOOK(addr, name, ...)                                                                     \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), NULL, (void *)shadow_##name, \
     (void **)&orig_##name, &stats_##name, 1}
// The messages and movies start with: push ebp; mov ebp, esp; mov eax, fs:0
#define SEH_PROLOGUE PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00
static const struct hook quiet_hooks[] = {
    QUIET_HOOK(0x5fcb20, do_all_non_input, 0xc7, 0x05, 0x9c, 0x7b, 0x9b, 0x00, 0x20, 0x00, 0x00, 0x00),  // mov dword [0x9b7b9c], 0x20
    QUIET_HOOK(0x5fcbb0, do_all_draws, 0x83, 0xec, 0x1c, 0xc7, 0x05, 0x9c, 0x7b, 0x9b, 0x00, 0x01, 0x00, 0x00, 0x00),
    QUIET_HOOK(0x46aef0, draw_radius, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x10),  // push ebx; mov ebx, [ebp+16]
    QUIET_HOOK(0x46af40, draw_tile, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),
    QUIET_HOOK(0x46b190, draw_map, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                   // push esi; push edi
    QUIET_HOOK(0x46fb90, map_win_clear_alt, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x41, 0x04, 0x57),  // mov eax, [ecx+4]; push edi
    QUIET_HOOK(0x46fd90, map_win_clear_terrain, 0x8b, 0x41, 0x04, 0x53, 0x56),          // mov eax, [ecx+4]; push ebx; push esi
    QUIET_HOOK(0x48c650, popb, SEH_PROLOGUE),
    QUIET_HOOK(0x48c0a0, popp, SEH_PROLOGUE),
    QUIET_HOOK(0x5bf330, x_pop2, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),  // mov eax, [ebp+16]
    QUIET_HOOK(0x5bf310, x_pop, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),   // mov eax, [ebp+12]
    QUIET_HOOK(0x5bf5d0, x_pops, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),  // mov eax, [ebp+16]
    QUIET_HOOK(0x54c560, diplomacy_menu, SEH_PROLOGUE),
    QUIET_HOOK(0x53d160, pledge_truce, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x60, 0xf6, 0x93, 0x00),
    QUIET_HOOK(0x53e470, diplomacy_ends, PUSH_EBP_MOV_EBP_ESP, 0x33, 0xc0),
    QUIET_HOOK(0x539510, net_loan, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x60, 0xf6, 0x93, 0x00),
    QUIET_HOOK(0x539580, net_maps, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x60, 0xf6, 0x93, 0x00),
    QUIET_HOOK(0x5399a0, diplomacy_caption, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00),
    QUIET_HOOK(0x5bf7d0, x_pops_11, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x14),  // mov eax, [ebp+20]
    QUIET_HOOK(0x5bf830, x_pops_13, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x18),  // mov eax, [ebp+24]
    QUIET_HOOK(0x46b310, map_win_focus, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x14),  // sub esp, 0x14
    QUIET_HOOK(0x44cb60, help_topic, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),  // mov eax, [ebp+12]
    QUIET_HOOK(0x58f8b0, get_noun_phrase, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00),  // sub esp, 0x100
    QUIET_HOOK(0x58f810, get_phrase, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),  // mov ecx, [ebp+8]
    QUIET_HOOK(0x4acbd0, report_intel, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x0d, 0x2c, 0x39, 0x7d, 0x00),  // mov ecx, [0x7d392c]
    QUIET_HOOK(0x625e30, parse_num, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0x09),   // mov eax, [esp+4]; cmp eax, 9
    QUIET_HOOK(0x625e50, parse_say, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0x09),
    QUIET_HOOK(0x625ec0, parse_says, 0x8b, 0x54, 0x24, 0x08, 0x85, 0xd2),         // mov edx, [esp+8]; test edx, edx
    QUIET_HOOK(0x4455f0, wave_it, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x64, 0x9a, 0x00),  // mov eax, [0x9a6490]
    QUIET_HOOK(0x5230e0, interlude, SEH_PROLOGUE),
    QUIET_HOOK(0x5108a0, console_focus, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    QUIET_HOOK(0x4be650, tut_map, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),  // sub esp, 12
    QUIET_HOOK(0x406380, popup_start, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0x0c, 0x1b, 0x69, 0x00),  // push ecx; mov eax, [0x691b0c]
    QUIET_HOOK(0x6015b0, base_pop_init, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf1),                    // push ebx, ebp, esi, edi; mov esi, ecx
    QUIET_HOOK(0x601b20, base_pop_set_width, 0x8b, 0x81, 0x14, 0x0a, 0x00, 0x00),               // mov eax, [ecx+0xa14]
    QUIET_HOOK(0x601980, base_pop_string, 0x8b, 0x44, 0x24, 0x04, 0x33, 0xd2),                  // mov eax, [esp+4]; xor edx, edx
    {0x6470a6, (const uint8_t[]){0xff, 0x35, 0xa8, 0x06, 0x9c, 0x00}, 6, NULL, (void *)shadow_heap_malloc,
     (void **)&orig_heap_malloc, &stats_heap_malloc, 1},  // malloc: push dword [0x9c06a8]
    {0x644ef2, (const uint8_t[]){0x56, 0x8b, 0x74, 0x24, 0x08}, 5, NULL, (void *)shadow_heap_free, (void **)&orig_heap_free,
     &stats_heap_free, 1},  // free: push esi; mov esi, [esp+8]
    QUIET_HOOK(0x5e2480, pcx_dimensions, 0x6a, 0xff, 0x68, 0x1c, 0x2c, 0x66, 0x00),             // push -1; push 0x662c1c
    QUIET_HOOK(0x602600, base_pop_exec_3, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),                  // mov eax, fs:0
    QUIET_HOOK(0x5d5a70, graphic_win_redraw, 0xa1, 0x28, 0x7b, 0x9b, 0x00),                     // mov eax, [0x9b7b28]
    QUIET_HOOK(0x4c6ea0, wave_unload, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),                      // push esi; mov esi, ecx; push edi; xor edi, edi
    QUIET_HOOK(0x4c6c20, wave_load, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),
    QUIET_HOOK(0x4c6920, wave_play, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff),
    QUIET_HOOK(0x4c6600, sound_fade, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0xf1),                  // push esi; mov esi, ecx
    QUIET_HOOK(0x403ba0, amovie_project, PUSH_EBP_MOV_EBP_ESP, 0x68, 0x48, 0x27, 0x68, 0x00),    // push 0x682748
    QUIET_HOOK(0x403be0, amovie_project_2, SEH_PROLOGUE),
// The monuments start with: push ebp; mov ebp, esp; push ecx; mov eax, [0x9a64d4]
#define MON_PROLOGUE PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xd4, 0x64, 0x9a, 0x00
    QUIET_HOOK(0x476ee0, mon_prototype_built, MON_PROLOGUE),
    QUIET_HOOK(0x477640, mon_native_life_bred, MON_PROLOGUE),
    QUIET_HOOK(0x477440, mon_naval_unit_built, MON_PROLOGUE),
    QUIET_HOOK(0x477540, mon_air_unit_built, MON_PROLOGUE),
    QUIET_HOOK(0x477740, mon_first_in_space, MON_PROLOGUE),
    QUIET_HOOK(0x476fe0, mon_facility_built, MON_PROLOGUE),
    QUIET_HOOK(0x477840, mon_built_preserve, MON_PROLOGUE),
    QUIET_HOOK(0x477100, mon_secret_project, MON_PROLOGUE),
    QUIET_HOOK(0x4779d0, mon_winning_trans, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xd4, 0x64, 0x9a, 0x00),
    QUIET_HOOK(0x428350, council, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),  // sub esp, 8
    QUIET_HOOK(0x424720, council_win_set_vote, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0xd9),  // push ebx; mov ebx, ecx
    QUIET_HOOK(0x612a70, dialogs_item, 0x8b, 0xc1, 0x8b, 0x88, 0x80, 0x01, 0x00, 0x00),  // mov eax, ecx; mov ecx, [eax+0x180]
    QUIET_HOOK(0x6108e0, sprite_box_sprite, 0x8b, 0x44, 0x24, 0x04, 0x53),  // mov eax, [esp+4]; push ebx
    QUIET_HOOK(0x5beca0, x_text_open, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x0c, 0x1b, 0x69, 0x00),  // mov eax, [0x691b0c]
    QUIET_HOOK(0x5fd570, text_get, 0xa1, 0xf4, 0x7c, 0x9b, 0x00),                         // mov eax, [0x9b7cf4]
    QUIET_HOOK(0x46b140, draw_tiles, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),            // push ebx; mov ebx, [ebp+12]
    QUIET_HOOK(0x4b9ea0, status_win_redraw, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),           // sub esp, 16
    QUIET_HOOK(0x514880, console_update_data, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),         // mov eax, [ebp+8]
    QUIET_HOOK(0x515400, console_center, 0xa1, 0xb8, 0x92, 0x93, 0x00),                       // mov eax, [0x9392b8]
    QUIET_HOOK(0x46b1f0, map_win_set_center, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x56, 0x8b, 0xf1),  // push ecx, ebx, esi; mov esi, ecx
    QUIET_HOOK(0x5be940, tech_research, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),             // mov eax, [ebp+12]
    QUIET_HOOK(0x5bb000, tech_achieved, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
    QUIET_HOOK(0x5be530, tech_advance, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x75, 0x08),  // push ebx; push esi; mov esi, [ebp+8]
    QUIET_HOOK(0x44c9e0, help_project, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    QUIET_HOOK(0x405140, pop2, SEH_PROLOGUE),
    QUIET_HOOK(0x504aa0, boom, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x44, 0x01, 0x00, 0x00),   // sub esp, 0x144
    QUIET_HOOK(0x50dd90, clock_wait, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x35, 0x68, 0x93, 0x66, 0x00),  // push ebx; push esi; mov esi, [0x669368]
    QUIET_HOOK(0x41e790, base_win_on_redraw, 0xa1, 0x2c, 0x39, 0x7d, 0x00, 0x56, 0x8b, 0xf1),  // mov eax, [0x7d392c]; push esi; mov esi, ecx
    QUIET_HOOK(0x41a920, base_win_zoom, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10, 0x53, 0x56),  // sub esp, 16; push ebx; push esi
    QUIET_HOOK(0x41a7a0, base_win_check_base, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0xf1),       // push esi; mov esi, ecx
    QUIET_HOOK(0x60c9b0, listbox_update_changes, 0xa1, 0x28, 0x7b, 0x9b, 0x00, 0x53),        // mov eax, [0x9b7b28]; push ebx
    QUIET_HOOK(0x41a620, base_win_base_killed, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57, 0x8b, 0xf1),  // push ebx, esi, edi; mov esi, ecx
    QUIET_HOOK(0x41a7e0, base_win_check_loc, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0xd9),        // push ebx; mov ebx, ecx
    QUIET_HOOK(0x47a890, netmsg_pop, SEH_PROLOGUE),
    QUIET_HOOK(0x55a150, veh_scoot, SEH_PROLOGUE),
    QUIET_HOOK(0x446a00, fx_play, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x64, 0x9a, 0x00),  // mov eax, [0x9a6490]
    QUIET_HOOK(0x476b70, mon_colony_founded, MON_PROLOGUE),
    QUIET_HOOK(0x446b40, ambience, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x64, 0x9a, 0x00),     // mov eax, [0x9a6490]
    QUIET_HOOK(0x446960, fx_play_loc, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x64, 0x9a, 0x00),  // mov eax, [0x9a6490]
    QUIET_HOOK(0x41a8b0, base_win_check_veh, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57, 0x8b, 0xf1),  // push esi; push edi; mov esi, ecx
    QUIET_HOOK(0x5fd120, flush_input, 0x83, 0xec, 0x1c, 0x8d, 0x44, 0x24, 0x00),               // sub esp, 28; lea eax, [esp]
    QUIET_HOOK(0x46af90, draw_tile_fixup, PUSH_EBP_MOV_EBP_ESP, 0xa0, 0x94, 0x64, 0x9a, 0x00),  // mov al, [0x9a6494]
    QUIET_HOOK(0x46b040, draw_tile_fixup_2, PUSH_EBP_MOV_EBP_ESP, 0xa0, 0x94, 0x64, 0x9a, 0x00),
    QUIET_HOOK(0x4b8830, status_win_draw, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),             // sub esp, 16
    QUIET_HOOK(0x4b8970, status_win_reset, 0x8b, 0x81, 0xd4, 0x15, 0x00, 0x00),                // mov eax, [ecx+0x15d4]
    QUIET_HOOK(0x422c50, battle_win_clear, 0x56, 0x8b, 0xf1, 0x68, 0x28, 0xd5, 0x78, 0x00),   // push esi; mov esi, ecx; push 0x78d528
    QUIET_HOOK(0x45d310, set_iface_mode, 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x2c, 0x39, 0x7d, 0x00),  // push esi; mov esi, ecx; mov ecx, [0x7d392c]
    QUIET_HOOK(0x421b20, battle_win_pulse_timer, 0x6a, 0x05, 0x68, 0x88, 0x13, 0x00, 0x00),    // push 5; push 5000
    QUIET_HOOK(0x59e5d0, prefs_save, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x54),                  // sub esp, 0x54
    QUIET_HOOK(0x477210, mon_enemy_destroyed, MON_PROLOGUE),
    QUIET_HOOK(0x446ad0, fx_fade, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                  // mov eax, [ebp+8]
    QUIET_HOOK(0x446a90, fx_stop, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    QUIET_HOOK(0x5109b0, console_cursor_next, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),  // mov eax, [ebp+12]
    QUIET_HOOK(0x446b10, fx_engine_pitch, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x81, 0xec, 0x28, 0x00, 0x00),  // mov eax, [ecx+0x28ec]
    QUIET_HOOK(0x477320, mon_conquer_base, MON_PROLOGUE),
    QUIET_HOOK(0x477a60, mon_killed_faction, MON_PROLOGUE),
    QUIET_HOOK(0x6262f0, log_say, 0xa1, 0xf8, 0xbf, 0x9b, 0x00),   // mov eax, [0x9bbff8]
    QUIET_HOOK(0x626250, log_say5, 0xa1, 0xf8, 0xbf, 0x9b, 0x00),
    QUIET_HOOK(0x428df0, show_credits, PUSH_EBP_MOV_EBP_ESP, 0x6a, 0xff),                    // push -1
    QUIET_HOOK(0x5adbf0, show_replay, SEH_PROLOGUE),
    QUIET_HOOK(0x4acb80, report_score, 0xa1, 0x2c, 0x39, 0x7d, 0x00),                      // mov eax, [0x7d392c]
    QUIET_HOOK(0x496670, quayle, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                    // mov eax, [ebp+8]
    QUIET_HOOK(0x44ae50, hall_of_fame, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x68, 0x20, 0x46, 0x68, 0x00),  // push esi; push 0x684620
    QUIET_HOOK(0x477940, mon_winning_unify, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xd4, 0x64, 0x9a, 0x00),  // mov eax, [0x9a64d4]
    QUIET_HOOK(0x5fd530, text_close, 0xa1, 0xf4, 0x7c, 0x9b, 0x00, 0x85, 0xc0),  // mov eax, [0x9b7cf4]; test eax, eax
    QUIET_HOOK(0x59e510, prefs_put, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xcc, 0x00, 0x69, 0x00),  // mov eax, [0x6900cc]
    QUIET_HOOK(0x59e530, prefs_put_2, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x50),              // sub esp, 0x50
    QUIET_HOOK(0x5abec0, wipe_undo, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x50),
    QUIET_HOOK(0x48bc60, plan_win_init, 0x56, 0x8b, 0xf1, 0xb8, 0x01, 0x00, 0x00, 0x00),  // push esi; mov esi, ecx; mov eax, 1
    QUIET_HOOK(0x48b450, plan_win_find_view, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x14),  // push ecx; mov eax, [ebp+20]
    QUIET_HOOK(0x48b3f0, plan_win_back_redraw, 0x56, 0x8b, 0xf1, 0x6a, 0x00),               // push esi; mov esi, ecx; push 0
    QUIET_HOOK(0x48ae90, plan_win_add_line_2, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),       // mov eax, [ebp+12]
    QUIET_HOOK(0x48b8e0, plan_win_monocenter, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x14),       // sub esp, 20
    QUIET_HOOK(0x48af30, plan_win_on_redraw, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),        // sub esp, 44
    QUIET_HOOK(0x48bc50, plan_win_close, 0xc7, 0x81, 0x68, 0x1a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00),
    QUIET_HOOK(0x5ed9d0, win_show, 0x83, 0xec, 0x14, 0x56, 0x8b, 0xf1),                      // sub esp, 20; push esi; mov esi, ecx
};
struct hook_module quiet_module = {"quiet", quiet_hooks, HOOK_COUNT(quiet_hooks), NULL, NULL, -1};
