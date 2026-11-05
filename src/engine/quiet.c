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
#include "game.h"
#include "shadow.h"

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

// The held copy of string argument i, or NULL.
#define TEXT(i) (c->has_text >> (i) & 1 ? (char *)c->text[i] : NULL)
#define PTR(i) ((void *)(intptr_t)a_[i])

// Messages: a box, or a line in the message bar, for the player's own bases.
HELD(int, popb, 0, (char *label, int flags, int sound, char *picture, void *sprite),
     (label, flags, sound, picture, sprite),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, picture), c_.arg[0] = flags, c_.arg[1] = sound,
      c_.arg[2] = (int)(intptr_t)sprite),
     orig_popb(TEXT(0), a_[0], a_[1], TEXT(1), PTR(2)))
HELD(int, popp, 0, (char *file, char *label, int flags, char *picture, void *fn), (file, label, flags, picture, fn),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, file), hooks_ui_text(&c_, 2, picture), c_.arg[0] = flags,
      c_.arg[1] = (int)(intptr_t)fn),
     orig_popp(TEXT(1), TEXT(0), a_[0], TEXT(2), PTR(1)))
HELD(int, x_pop2, 0, (char *label, char *picture, int unk), (label, picture, unk),
     (hooks_ui_text(&c_, 0, label), hooks_ui_text(&c_, 1, picture), c_.arg[0] = unk),
     orig_x_pop2(TEXT(0), TEXT(1), a_[0]))
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

// Moving the map to a square.
static void(__attribute__((thiscall)) * orig_console_focus)(void *console, int x, int y, int faction);
static struct hook_stats stats_console_focus = {"(held console_focus)"};
static void replay_console_focus(const struct hook_ui_call *c) {
    orig_console_focus((void *)(intptr_t)c->arg[3], c->arg[0], c->arg[1], c->arg[2]);
}
static void __attribute__((thiscall)) shadow_console_focus(void *console, int x, int y, int faction) {
    if (!hooks_protecting()) {
        orig_console_focus(console, x, y, faction);
        return;
    }
    stats_console_focus.calls++;
    struct hook_ui_call c = {.name = "console_focus", .replay = replay_console_focus, .arg = {x, y, faction, (int)(intptr_t)console}};
    hooks_ui_hold(&c);
}

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
    QUIET_HOOK(0x46fd90, map_win_clear_terrain, 0x8b, 0x41, 0x04, 0x53, 0x56),          // mov eax, [ecx+4]; push ebx; push esi
    QUIET_HOOK(0x48c650, popb, SEH_PROLOGUE),
    QUIET_HOOK(0x48c0a0, popp, SEH_PROLOGUE),
    QUIET_HOOK(0x5bf330, x_pop2, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),  // mov eax, [ebp+16]
    QUIET_HOOK(0x625e30, parse_num, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0x09),   // mov eax, [esp+4]; cmp eax, 9
    QUIET_HOOK(0x625e50, parse_say, 0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0x09),
    QUIET_HOOK(0x625ec0, parse_says, 0x8b, 0x54, 0x24, 0x08, 0x85, 0xd2),         // mov edx, [esp+8]; test edx, edx
    QUIET_HOOK(0x4455f0, wave_it, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x64, 0x9a, 0x00),  // mov eax, [0x9a6490]
    QUIET_HOOK(0x5230e0, interlude, SEH_PROLOGUE),
    QUIET_HOOK(0x5108a0, console_focus, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
};
struct hook_module quiet_module = {"quiet", quiet_hooks, HOOK_COUNT(quiet_hooks), NULL, NULL, -1};
