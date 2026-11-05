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
QUIET_THIS(int, base_pop_exec_3, 0, (void *pop, int a, int b), (pop, a, b))
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
HELD_THIS(graphic_win_redraw, (void *self), (self), (void)0, orig_graphic_win_redraw(SELF))
HELD_THIS(wave_unload, (void *self), (self), (void)0, orig_wave_unload(SELF))
HELD_THIS(wave_load, (void *self, char *file), (self, file), hooks_ui_text(&c_, 0, file), orig_wave_load(SELF, TEXT(0)))
HELD_THIS(wave_play, (void *self), (self), (void)0, orig_wave_play(SELF))
HELD_THIS(sound_fade, (void *self, int ms), (self, ms), c_.arg[0] = ms, orig_sound_fade(SELF, a_[0]))
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

// A tech given away (by a secret project), with its announcement and the
// window that shows it, which allocates as the monuments do.
HELD(void, tech_advance, (void)0, (int faction), (faction), (c_.arg[0] = faction), orig_tech_advance(a_[0]))
// Research paid in, which may discover a tech in the same way.
HELD(void, tech_research, (void)0, (int faction, int labs), (faction, labs), (c_.arg[0] = faction, c_.arg[1] = labs),
     orig_tech_research(a_[0], a_[1]))

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
    fprintf(stderr, "chiron: shadow mismatch: game heap %s while state is protected\n", is_free ? "free" : "malloc");
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
    QUIET_HOOK(0x46b140, draw_tiles, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),            // push ebx; mov ebx, [ebp+12]
    QUIET_HOOK(0x4b9ea0, status_win_redraw, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),           // sub esp, 16
    QUIET_HOOK(0x514880, console_update_data, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),         // mov eax, [ebp+8]
    QUIET_HOOK(0x515400, console_center, 0xa1, 0xb8, 0x92, 0x93, 0x00),                       // mov eax, [0x9392b8]
    QUIET_HOOK(0x5be940, tech_research, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),             // mov eax, [ebp+12]
    QUIET_HOOK(0x5be530, tech_advance, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x75, 0x08),  // push ebx; push esi; mov esi, [ebp+8]
    QUIET_HOOK(0x44c9e0, help_project, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
};
struct hook_module quiet_module = {"quiet", quiet_hooks, HOOK_COUNT(quiet_hooks), NULL, NULL, -1};
