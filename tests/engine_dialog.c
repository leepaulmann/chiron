// Unit test for the Dialog, SpriteBox and BasePop classes and the text functions in dialog.c, on hand-built
// objects with vtables and callbacks that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/bufobj.h"
#include "../src/engine/dialog.h"
#include "../src/engine/text.h"
#include "../src/engine/game.h"
#include "../src/engine/win.h"
#define PTR_(o, off) (*(void **)((uint8_t *)(o) + (off)))
#include "life_test.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))
#define THIS __attribute__((thiscall))

// What the stand-ins log: slot, object, arguments.
static struct {
    int slot;
    void *o;
    int a, b, c, d;
} calls[64];
static int ncalls;
static void rec(int slot, void *o, int a, int b, int c, int d) {
    if (ncalls < 64) calls[ncalls].slot = slot, calls[ncalls].o = o, calls[ncalls].a = a, calls[ncalls].b = b, calls[ncalls].c = c, calls[ncalls].d = d;
    ncalls++;
}
static void expect_call(int i, int slot, void *o, int a, int b, int c, int d) {
    char what[48];
    snprintf(what, sizeof what, "call %d", i);
    check_(what, i < ncalls ? calls[i].slot : -1, slot);
    if (i >= ncalls) return;
    check_("  object", (long)calls[i].o, (long)o);
    check_("  a", calls[i].a, a), check_("  b", calls[i].b, b), check_("  c", calls[i].c, c), check_("  d", calls[i].d, d);
}
static void THIS s_dtor(void *o, int flag) { rec(0x100, o, flag, 0, 0, 0); }
static void THIS s_list1(void *l, void *item) { rec(0x101, l, (int)(intptr_t)item, 0, 0, 0); }
static int THIS s_4(void *o, int a, int x, int y, int b) { return rec(4, o, a, x, y, b), 44; }
static int THIS s_8(void *o) { return rec(8, o, 0, 0, 0, 0), 0; }
static int THIS s_b0(void *o, int a) { return rec(0xb0, o, a, 0, 0, 0), 0; }
static int THIS s_e8(void *o) { return rec(0xe8, o, 0, 0, 0, 0), 0; }
static int THIS s_f8(void *o) { return rec(0xf8, o, 0, 0, 0, 0), 0; }
static void *vt[0x180 / 4], *vt_sb[0x180 / 4], *item_vt[2], *list_vt[2];

// The data functions: closing one logs, making one gives the data (or fails).
static int data_fail;
static char data_block[64];
static void THIS data_close(void *data) {
    rec(0x300, data, 0, 0, 0, 0);
    uint8_t *f = data;
    memset(f, 0, 0x14);
}
static int THIS data_alloc(void *data, int size) {
    rec(0x301, data, size, 0, 0, 0);
    if (data_fail) return 1;
    void **f = data;
    f[1] = data_block, f[2] = data_block, f[3] = (void *)(intptr_t)size, f[4] = (void *)(intptr_t)size;
    return 0;
}

static int posted[8], nposted;
static int __attribute__((stdcall)) post_message(void *wnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    if (nposted < 8) posted[nposted] = (int)msg * 1000 + (int)wp;
    nposted++;
    (void)wnd, (void)lp;
    return 1;
}
static int fn_answer, fn_id;
static int sprite_fn(int id) {
    fn_id = id;
    return fn_answer;
}

static struct game_dialog_defs_a da;
static struct game_dialog_defs_b db;
static struct game_pop_defs pdefs;
static struct game_pop_fonts pfonts;
static int32_t ext_a[3], ext_b[2], pop_open, pop_fallout_on, screen_w = 1024;
static uint32_t hwnd = 7;
static struct game_win *focus;
static char pop_file[16];

// A popup in a block of its own, with a Dialog and a CheckBox's Dialog placed after it.
static struct {
    struct game_base_pop pop;
    struct game_dialog dlg, chk;
} BP;
static int32_t vb_dlg[4], vb_chk[4];
static struct game_dlist_node nodes[4];
static void *items[4];
static void *ext_win[1];
static struct font f_ok, f_bad;

static void set_list(struct game_dlist *l, int n) {
    l->vtable = list_vt;
    for (int i = 0; i < n; i++) {
        nodes[i].vtable = item_vt, nodes[i].key = 10 + i, items[i] = item_vt, nodes[i].item = &items[i];
        nodes[i].next = &nodes[(i + 1) % n], nodes[i].prev = &nodes[(i + n - 1) % n];
    }
    l->head = n ? &nodes[0] : NULL, l->cur = NULL, l->count = n, l->idx = 0;
}
static void reset_bp(void) {
    memset(&BP, 0, sizeof BP);
    BP.pop.vtable = vt;
    vb_dlg[2] = (int32_t)((uint8_t *)&BP.dlg - ((uint8_t *)&BP.pop + 0x21d0));
    vb_chk[2] = (int32_t)((uint8_t *)&BP.chk - ((uint8_t *)&BP.pop + 0x2228));
    BP.pop.dlg_vbtable = vb_dlg, BP.pop.chk_vbtable = vb_chk;
    ncalls = 0;
}


// What the popup entry points pass on: the arguments (9 for pops_18, 6 for pop_ask_6) of the one call.
static int fwd_args[9], fwd_calls, fwd_kind;
static int fwd_stub(int kind, const int *a, int n) {
    fwd_kind = kind, fwd_calls++;
    memcpy(fwd_args, a, (size_t)n * sizeof *a);
    return 4242 + kind;
}
static int pops_18_stub(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return fwd_stub(0, (const int[]){a, b, c, d, e, f, g, h, i}, 9);
}
static int pop_ask_6_stub(int a, int b, int c, int d, int e, int f) { return fwd_stub(1, (const int[]){a, b, c, d, e, f}, 6); }
static const char xyz_text[] = "xyz";
#define XYZ ((int)(intptr_t)xyz_text)
#define POPFILE_ ((int)(intptr_t)game_pop_file)
// A call of the entry point, to get one call of pops_18 (kind 0) or pop_ask_6 (kind 1) with the expected arguments.
#define EXPECT_FWD(fn, which, call_args, ...)                                                            \
    do {                                                                                                 \
        const int exp_[] = __VA_ARGS__;                                                                  \
        fwd_calls = 0;                                                                                   \
        int r_ = fn call_args;                                                                           \
        check_(#fn " calls", fwd_calls, 1);                                                              \
        check_(#fn " answer", r_, 4242 + (strcmp(#which, "ask") == 0));                                  \
        check_(#fn " target", fwd_kind, strcmp(#which, "ask") == 0);                                     \
        for (size_t k_ = 0; k_ < sizeof exp_ / sizeof *exp_; k_++) {                                     \
            char what_[48];                                                                              \
            snprintf(what_, sizeof what_, #fn " argument %zu", k_);                                      \
            check_(what_, fwd_args[k_], exp_[k_]);                                                       \
        }                                                                                                \
    } while (0)

// ---- Life cycle ----

static int life_data_closed, life_item_slot1, life_deleted;
static void LT_THIS life_data_close(void *d) { life_data_closed++; }
static void LT_THIS life_slot1(void *l, void *item) { life_item_slot1++; }
static void LT_THIS life_delete(void *o, int flag) { life_deleted++; free(o); }
static void *life_list_vtable[3] = {NULL, (void *)life_slot1};
static void *life_node_vtable[2] = {(void *)life_delete, NULL};

// The stand-ins the popup's and SpriteBox's life cycle need: the original's parts log their calls, and the arrows and the
// objects the popup deletes count theirs.
static int pop_log[32], pop_nlog, pop_deleted, pop_button_closes, pop_win_closes;
static void pop_note(int what) { if (pop_nlog < 32) pop_log[pop_nlog++] = what; }
static void LT_THIS pop_win_close(void *w) { pop_win_closes++; }
static void *LT_THIS stub_dialogs_ctor(void *o, int most) {
    pop_note(0x612830 + most);
    OW(o, 0) = IMGVA(0x680100), OW(o, 0x44) = IMGVA(0x680200), OW(o, 0x70) = IMGVA(0x680400), OW(o, 0xf8) = IMGVA(0x680500);
    graphic_win_ctor((struct game_win *)((uint8_t *)o + 0x188)), dialog_ctor((struct game_dialog *)((uint8_t *)o + 0xba0));
    return o;
}
static void LT_THIS stub_dialogs_dtor_part(void *w) { pop_note(0x406910); }
static void LT_THIS stub_checkbox_close(void *o) { pop_note(0x60e7c0); }
static void *LT_THIS stub_arena_ctor(void *a) { pop_note(0x5d4560); return a; }
static void LT_THIS stub_arena_close(void *a) { pop_note(0x5d45b0); }
static void LT_THIS stub_button_close(void *b) { pop_button_closes++; }
static void LT_THIS stub_delete(void *o, int flag) { pop_deleted += flag; }
static void *stub_button_vtable[0x180 / 4], *stub_object_vtable[1] = {(void *)stub_delete};

static void *null_alloc_dlg(size_t n) { (void)n; return NULL; }
#define NULL_ALLOC_DLG null_alloc_dlg
static void test_life(void) {
    life_image_init();
    game_free = life_free, game_mem_get = life_mem_get, game_dialog_data_close = life_data_close;
    game_dialog_defs_a = game_at(0x697098), game_dialog_defs_b = game_at(0x9b8eb4);
    life_vbtable(0x66b0ec, 0x24, 0);
    static uint8_t obj[0xf8];
    uint32_t *head = game_at(0x9b3374);

    // A new Dialog: the game's list head saved in its base (0xe8) and cleared, the vtables of the Dialog and of
    // the list (and its base), the list empty, no data, and the game's defaults.
    memset(obj, 0xcc, sizeof obj);
    *head = 0x1111;
    EXPECT(dialog_ctor((struct game_dialog *)obj) == (struct game_dialog *)obj, 1);
    struct game_dialog *d = (struct game_dialog *)obj;
    EXPECT(d->vtable == (void **)(uintptr_t)IMGVA(0x6703fc), 1), EXPECT(*head, 0), EXPECT(d->ve8, 0x1111);
    EXPECT(d->list.vtable == (void **)(uintptr_t)IMGVA(0x6698c4), 1), EXPECT(d->list.vbptr == (void *)(uintptr_t)IMGVA(0x66b0ec), 1);
    // (The list's virtual base, whose vtable is at 0xe4, ends with the list's own.)
    EXPECT(d->ve4 == (void *)(uintptr_t)IMGVA(0x6698c0), 1), EXPECT(d->list.head == NULL, 1), EXPECT(d->list.count, 0);
    EXPECT(d->list.idx, 0), EXPECT(d->list.buf == NULL, 1), EXPECT(d->data_flag, 0), EXPECT(d->data == NULL, 1), EXPECT(d->cursor == NULL, 1);
    EXPECT(d->data_size, 0), EXPECT(d->data_left, 0), EXPECT(d->size, -1), EXPECT(d->hover, -1), EXPECT(d->sel, 0);
    EXPECT(d->flags, IMGW(0x6970dc)), EXPECT(d->w, (int)IMGW(0x697098)), EXPECT(d->h, (int)IMGW(0x9b8eb4)), EXPECT(d->font == (void *)(uintptr_t)IMGW(0x9b8ec0), 1);
    EXPECT(d->color[3][2], (int)IMGW(0x6970d8)), EXPECT(d->color[0][0], (int)IMGW(0x6970ac)), EXPECT(d->v48, -1), EXPECT(d->ext == (void *)0xcccccccc, 1);

    // The destructor closes the Dialog (which empties the list through the list's vtable and the items' own),
    // walks the list's vtables back, gives the data back and puts the game's list head back.
    d->list.vtable = life_list_vtable;
    struct game_dlist_node *n[2];
    for (int i = 0; i < 2; i++) {
        n[i] = calloc(1, 32);
        *(void **)n[i] = life_node_vtable;
        n[i]->item = calloc(1, 16);
        *(void **)n[i]->item = life_node_vtable;
    }
    n[0]->next = n[1], n[1]->next = n[0], d->list.head = n[0], d->list.count = 2;
    life_data_closed = 0, life_item_slot1 = 0, life_deleted = 0;
    OW(obj, 0xbc) = (uint32_t)(uintptr_t)life_list_vtable;
    dialog_dtor(d);
    EXPECT(life_deleted, 4), EXPECT(life_item_slot1, 2), EXPECT(d->list.head == NULL, 1), EXPECT(d->list.count, 0), EXPECT(d->list.idx, 0);
    EXPECT(life_data_closed, 2), EXPECT(*head, 0x1111), EXPECT(d->vtable == (void **)(uintptr_t)IMGVA(0x6703fc), 1);
    EXPECT(d->list.vtable == (void **)(uintptr_t)IMGVA(0x6693a4), 1), EXPECT(d->ve4 == (void *)(uintptr_t)IMGVA(0x6693ac), 1);

    // Setters: the old copy freed, the text copied onto the game's heap; no text answers 3 and leaves things as they are, no memory 5 (4 for
    // the default cancel text) and an empty copy.
    {
        static uint8_t popobj[0x3230];
        struct game_base_pop *p = (struct game_base_pop *)popobj;
        life_buffer_standins();
        memset(popobj, 0, sizeof popobj);
        EXPECT(pop_set_ok_text(p, "OK"), 0), EXPECT(strcmp((char *)(uintptr_t)OW(popobj, 0x3224), "OK"), 0);
        char *old = (char *)(uintptr_t)OW(popobj, 0x3224);
        life_freed = 0;
        EXPECT(pop_set_ok_text(p, "Fine"), 0), EXPECT(strcmp((char *)(uintptr_t)OW(popobj, 0x3224), "Fine"), 0), EXPECT(life_freed, 1);
        EXPECT(life_freed_has(old), 1), EXPECT(pop_set_ok_text(p, NULL), 3), EXPECT(strcmp((char *)(uintptr_t)OW(popobj, 0x3224), "Fine"), 0);
        EXPECT(pop_set_cancel_text(p, "No"), 0), EXPECT(strcmp((char *)(uintptr_t)OW(popobj, 0x3228), "No"), 0);
        EXPECT(pop_set_cancel_text(p, NULL), 3), EXPECT(OW(popobj, 0x3224) != 0, 1);
        uint32_t *def_ok = game_at(0x9b8d80), *def_cancel = game_at(0x9b8d84);
        *def_ok = *def_cancel = 0;
        EXPECT(pop_set_def_ok_text("Yes"), 0), EXPECT(strcmp((char *)(uintptr_t)*def_ok, "Yes"), 0);
        EXPECT(pop_set_def_cancel_text("Nope"), 0), EXPECT(strcmp((char *)(uintptr_t)*def_cancel, "Nope"), 0);
        EXPECT(pop_set_def_ok_text(NULL), 3), EXPECT(pop_set_def_cancel_text(NULL), 3);
        old = (char *)(uintptr_t)*def_ok, life_freed = 0;
        EXPECT(pop_set_def_ok_text("Again"), 0), EXPECT(life_freed_has(old), 1);
        game_mem_get = NULL_ALLOC_DLG;
        EXPECT(pop_set_def_ok_text("x"), 5), EXPECT(*def_ok, 0), EXPECT(pop_set_def_cancel_text("x"), 4), EXPECT(*def_cancel, 0);
        EXPECT(pop_set_ok_text(p, "x"), 5), EXPECT(OW(popobj, 0x3224), 0), EXPECT(pop_set_cancel_text(p, "x"), 5), EXPECT(OW(popobj, 0x3228), 0);
        game_mem_get = life_mem_get;
    }

    // A popup: made from its parts in order, the game's tables and defaults copied in, the external window (the game's word at 0x9b8d04 is set
    // in the test image) sent to the combined widget's Dialog; close gives back the texts, the file name and the eight objects, asks each
    // button to close, and closes the parts; the destructor then unmakes them.
    {
        static uint8_t popobj[0x3230];
        struct game_base_pop *p = (struct game_base_pop *)popobj;
        life_buffer_standins();
        life_time_standins();
        game_sprite_bytes = game_at(0x9b6618);
        IMGW(0x9b8ec0) = IMGW(0x9b8ec4) = IMGW(0x9b8ec8) = 0;  // (no fonts for the SpriteBox's Buffer)
        game_win_close = pop_win_close, game_dialogs_ctor = stub_dialogs_ctor, game_dialogs_dtor_part = stub_dialogs_dtor_part;
        game_checkbox_close = stub_checkbox_close, game_arena_ctor = stub_arena_ctor, game_arena_close = stub_arena_close;
        stub_button_vtable[0x168 / 4] = (void *)stub_button_close;
        life_vbtable(0x680100, 0x188, 0xba0), life_vbtable(0x680200, 0x144, 0xb5c), life_vbtable(0x680400, 0x118, 0xb30), life_vbtable(0x680500, 0x90, 0xaa8);
        life_vbtable(0x66b0ec, 0x24, 0), life_vbtable(0x6708ac, 0x24, 0), life_vbtable(0x6708b4, 0x8c, 0xaa4);
        game_dialog_data_close = life_data_close;
        game_dialog_defs_a = game_at(0x697098), game_dialog_defs_b = game_at(0x9b8eb4);
        uint32_t *head = game_at(0x9b3374);
        *head = 0x4242;
        memset(popobj, 0, sizeof popobj);
        pop_nlog = 0;
        EXPECT(pop_ctor(p) == p, 1);
        EXPECT(OW(popobj, 0), IMGVA(0x6698d4)), EXPECT(OW(popobj, 0x444), IMGVA(0x6698cc)), EXPECT(OW(popobj, 0xa3c), (uint32_t)(uintptr_t)popobj);
        EXPECT(pop_nlog, 2), EXPECT(pop_log[0], 0x5d4560), EXPECT(pop_log[1], 0x612831);
        EXPECT(memcmp(popobj + 0x2e64, game_at(0x9b8ab8), 0x118), 0), EXPECT(memcmp(popobj + 0x2f7c, game_at(0x9b8bd0), 0x118), 0);
        EXPECT(OW(popobj, 0x30a4), IMGW(0x9b8dcc)), EXPECT(OW(popobj, 0x3094), IMGW(0x9b8cf8));
        EXPECT(OW(popobj, 0x31b8), IMGW(0x9b8d10)), EXPECT(OW(popobj, 0x31c4), IMGW(0x9b8d1c)), EXPECT(OW(popobj, 0x31c4 + 0xc + 8), IMGW(0x9b8d28 + 8));
        EXPECT(OW(popobj, 0x31c4 + 0x54 + 8), IMGW(0x9b8d70 + 8)), EXPECT(OW(popobj, 0x3104), IMGW(0x696ed8)), EXPECT(OW(popobj, 0x3108), IMGW(0x696edc));
        EXPECT(popobj[0x3178], (uint8_t)IMGW(0x696f24)), EXPECT(popobj[0x31a0], (uint8_t)IMGW(0x696f54)), EXPECT(OW(popobj, 0x21cc), 0xffffffffu);
        EXPECT(OW(popobj, 0x3100), 0), EXPECT(OW(popobj, 0xa44), 0xffffffffu), EXPECT(OW(popobj, 0x322c), IMGW(0x9b8dd0));
        EXPECT(OW(popobj, 0x2154), IMGVA(0x66b0ec)), EXPECT(OW(popobj, 0x2150), IMGVA(0x6698c4)), EXPECT(OW(popobj, 0x2180), IMGVA(0x6698c4));
        EXPECT(OW(popobj, 0x217c), 0x4242), EXPECT(OW(popobj, 0x21ac), 0), EXPECT(*head, 0), EXPECT(OW(popobj, 0x2118), 0);
        // The external window (the word the test image holds) reaches the Dialog (0xba0 on from the widget): its size, flags bits 0 and 1.
        EXPECT(OW(popobj, 0xa14), IMGW(0x9b8d04));
        EXPECT(OW(popobj, 0x21d0 + 0xba0 + 0x2c), IMGW(0x696ec4)), EXPECT(OW(popobj, 0x21d0 + 0xba0 + 0x30), IMGW(0x696ec8));
        EXPECT(OW(popobj, 0x21d0 + 0xba0 + 0x20) & 3, 3);
        if (IMGW(0x9b8d08) != 0x2000) EXPECT(OW(popobj, 0x30f8), IMGW(0x9b8d08));
        // The buttons' own vtables stand in, and the popup holds a text each, a file name and two objects.
        OW(popobj, 0xa5c) = OW(popobj, 0x15a8) = (uint32_t)(uintptr_t)stub_button_vtable;
        char *ok = malloc(4), *cancel = malloc(4), *file = malloc(4);
        OW(popobj, 0x3224) = (uint32_t)(uintptr_t)ok, OW(popobj, 0x3228) = (uint32_t)(uintptr_t)cancel, OW(popobj, 0xa48) = (uint32_t)(uintptr_t)file;
        void *objs[2] = {stub_object_vtable, stub_object_vtable};
        OW(popobj, 0x20f8) = (uint32_t)(uintptr_t)&objs[0], OW(popobj, 0x2100) = (uint32_t)(uintptr_t)&objs[1];
        OW(popobj, 0x3108) = 1, OW(popobj, 0x30f8) = 7, OW(popobj, 0x3100) = 9, OW(popobj, 0xa40) = 5, OW(popobj, 0x21c8) = 3;
        life_freed = 0, pop_deleted = 0, pop_button_closes = 0, pop_win_closes = 0, life_data_closed = 0, pop_nlog = 0;
        pop_close(p);
        EXPECT(life_freed, 3), EXPECT(life_freed_has(ok), 1), EXPECT(life_freed_has(cancel), 1), EXPECT(life_freed_has(file), 1);
        EXPECT(OW(popobj, 0x3224), 0), EXPECT(OW(popobj, 0x3228), 0), EXPECT(OW(popobj, 0xa48), 0), EXPECT(OW(popobj, 0x20f8), 0), EXPECT(OW(popobj, 0x2100), 0);
        EXPECT(pop_deleted, 2), EXPECT(pop_button_closes, 2), EXPECT(OW(popobj, 0x3108), IMGW(0x696ed8)), EXPECT(OW(popobj, 0x30f8), IMGW(0x9b8d08));
        EXPECT(OW(popobj, 0x3100), 0), EXPECT(OW(popobj, 0xa40), 0), EXPECT(OW(popobj, 0x21c8), 0), EXPECT(OW(popobj, 0xa44), 0xffffffffu);
        EXPECT(pop_nlog, 1), EXPECT(pop_log[0], 0x60e7c0), EXPECT(life_data_closed >= 1, 1);
        // The destructor: its vtables first, then close and each part unmade (the two lists put the game's head back).
        life_freed = 0, pop_nlog = 0, pop_win_closes = 0;
        *head = 0x9999;
        OW(popobj, 0x217c) = 0x1111;
        OW(popobj, 0x21ac) = 0x2222;
        pop_dtor(p);
        EXPECT(pop_nlog >= 3, 1), EXPECT(pop_log[1], 0x406910), EXPECT(pop_log[pop_nlog - 1], 0x5d45b0), EXPECT(OW(popobj, 0), IMGVA(0x66ff34));
        EXPECT(OW(popobj, 0x21a8), IMGVA(0x6693ac)), EXPECT(OW(popobj, 0x2178), IMGVA(0x6693ac)), EXPECT(*head, 0x1111);
        EXPECT(OW(popobj, 0x2150), IMGVA(0x6693a4)), EXPECT(OW(popobj, 0x2180), IMGVA(0x6693a4)), EXPECT(pop_win_closes >= 4, 1);
    }

    // A SpriteBox: an object with two virtual bases (the first word's table: the GraphicWin at 0x8c, the Dialog at 0xaa4), a Spot list, a list of
    // sprites with a table of its own, the game's list head saved at 0x50, and the Buffer's colours and fonts from the game's table.
    {
        static uint8_t sb[0xb98];
        life_buffer_standins();
        game_win_close = pop_win_close;
        IMGW(0x9b8ec0) = IMGW(0x9b8ec4) = IMGW(0x9b8ec8) = 0;
        game_dialog_data_close = life_data_close;
        game_dialog_defs_a = game_at(0x697098), game_dialog_defs_b = game_at(0x9b8eb4);
        life_vbtable(0x6708b4, 0x8c, 0xaa4), life_vbtable(0x6708ac, 0x24, 0), life_vbtable(0x66b0ec, 0x24, 0);
        uint32_t *head = game_at(0x9b3374);
        *head = 0x3131;
        memset(sb, 0xcc, sizeof sb);
        EXPECT(sprite_box_ctor(sb, 1) == sb, 1);
        EXPECT(OW(sb, 0), IMGVA(0x6708b4)), EXPECT(OW(sb, 0x8c), IMGVA(0x670738)), EXPECT(OW(sb, 0x8c + 0x444), IMGVA(0x670730)), EXPECT(OW(sb, 0xaa4), IMGVA(0x670724));
        EXPECT(OW(sb, 0x88), 0), EXPECT(OW(sb, 0xaa0), 0), EXPECT(OW(sb, 0x28), IMGVA(0x6708ac)), EXPECT(OW(sb, 0x24), IMGVA(0x6708a4));
        EXPECT(OW(sb, 0x4c), IMGVA(0x6708a0)), EXPECT(OW(sb, 0x50), 0), EXPECT(OW(sb, 0xaa4 + 0xe8), 0x3131), EXPECT(*head, 0), EXPECT(OW(sb, 0x2c), 0), EXPECT(OW(sb, 0x3c), 0);
        EXPECT(OW(sb, 4), 0), EXPECT(OW(sb, 0x1c), 1), EXPECT(OW(sb, 0x20), 1), EXPECT(OW(sb, 0x54), 0), EXPECT(OW(sb, 0x84), 0), EXPECT(OW(sb, 0x88), 0);
        EXPECT(OW(sb, 8), IMGW(0x9b8fc4)), EXPECT(OW(sb, 0x68), 0), EXPECT(OW(sb, 0x90 - 0x8), 0);
        // Close starts the numbers again and closes the bases.
        OW(sb, 4) = 5, OW(sb, 0x1c) = 0, OW(sb, 0x60) = 9, OW(sb, 0x84) = 3, OW(sb, 8) = 77, OW(sb, 0x38) = 4;
        pop_win_closes = 0;
        sprite_box_close(sb);
        EXPECT(OW(sb, 4), 0), EXPECT(OW(sb, 0x1c), 1), EXPECT(OW(sb, 0x60), 0), EXPECT(OW(sb, 0x84), 0), EXPECT(OW(sb, 8), IMGW(0x9b8fc4));
        EXPECT(OW(sb, 0x38), 0), EXPECT(pop_win_closes, 1);
        // The destructor (given the GraphicWin): the vtables again, close, the list's own vtable put back to the first class's and the head too.
        OW(sb, 0x8c) = 0, OW(sb, 0xaa4) = 0, OW(sb, 0x50) = 0x5151;
        sprite_box_dtor(sb + 0x8c);
        EXPECT(OW(sb, 0x8c), IMGVA(0x670738)), EXPECT(OW(sb, 0xaa4), IMGVA(0x670724)), EXPECT(OW(sb, 0x24), IMGVA(0x66943c)), EXPECT(*head, 0x5151);
        EXPECT(OW(sb, 0x4c), IMGVA(0x6693ac));
    }
    // A Dialog's scalar deleting destructor.
    {
        void *d = calloc(1, 0xf4);
        dialog_ctor(d);
        life_freed = 0;
        EXPECT(dialog_scalar_dtor((struct game_dialog *)d, 1) == d, 1), EXPECT(life_freed_has(d), 1);
        d = calloc(1, 0xf4);
        dialog_ctor(d);
        life_freed = 0;
        EXPECT(dialog_scalar_dtor((struct game_dialog *)d, 0) == d, 1), EXPECT(life_freed, 0);
        free(d);
    }

}


// ---- BasePop layout and drawing ----
//
// A popup as the original lays it out (the table of its widget's virtual bases at 0x21d0, the Win at 0x188 and the Dialog at 0xba0 into
// it), a Buffer with pixels of its own, text measured by a stand-in (6 pixels a character, a line 12 high), and the widget's layout, the
// window's virtual functions and the function a screen set standing in with logs.
static uint8_t PO[0x3230] __attribute__((aligned(16)));
static int32_t pvbt[4] = {0, 0x188, 0xba0, 0};
static void *pvt[0x200 / 4];
static int pcalc_answer, pcalc_wide, pcalc_high, p180_answer;
#define PI(off) (*(int32_t *)(PO + (off)))
#define PD(off) (*(int32_t *)(PO + 0x2d70 + (off)))
static int THIS p_calc(void *w) {
    rec(0x500, w, PD(0x2c), PD(0x30), PD(0x20), 0);
    PD(0x5c) = pcalc_wide, PD(0x60) = pcalc_high;
    return pcalc_answer;
}
static int THIS p_174(void *o) { return rec(0x174, o, 0, 0, 0, 0), 0; }
static int THIS p_180(void *o) { return rec(0x180, o, 0, 0, 0, 0), p180_answer; }
static int p_handler(void *pic, void *host, int id, int x, int y) {
    rec(0x300, pic, (int)(intptr_t)host, id, x * 1000 + y, *(int32_t *)game_at(0x9b8cf4) == (int32_t)(intptr_t)PO);
    return 0;
}
static int p_fake_dc, p_spot_inits;
static void *p_dc_slot, *p_default_font;
static int32_t p_slack, p_fly_x, p_fly_y, p_fly_left, p_link_table[0x2c0 / 4], p_text_end;
static char p_fly_text[1024];
static void *p_selected;
static void *__attribute__((stdcall)) p_select_object(void *dc, void *obj) {
    void *old = p_selected;
    p_selected = obj;
    return old;
}
static void *__attribute__((stdcall)) p_stock_object(int which) { return (void *)(intptr_t)(0x1000 + which); }
static int __attribute__((stdcall)) p_text_extent(void *dc, const char *s, int n, int32_t *size) {
    size[0] = n * 6, size[1] = 12;
    return 1;
}
static void THIS p_spot_init(void *s, int n) { p_spot_inits++; }
static struct font pfont, pbfont;
static uint8_t ppix[200 * 100 + 8];
static struct game_sprite psp[3];
static uint8_t pspd[3][64];
static struct game_dlist_node pnode[3];
static struct {
    void *vt;
    const char *text;
    int32_t kind;
} pitem[3];
static struct game_dlist_node snode[2];
static void *sitem_[2];

static uint8_t PH[0x444 + sizeof(struct game_buffer) + 16] __attribute__((aligned(16)));
static int p_nouts;
static int __attribute__((stdcall)) p_intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) return memset(out, 0, 16), 0;
    return memcpy(out, r, 16), 1;
}
static int __attribute__((stdcall)) p_equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static char p_outs[8][16];
static int __attribute__((stdcall)) p_set_bk_mode(void *dc, int mode) { return 0; }
static uint32_t __attribute__((stdcall)) p_set_text_color(void *dc, uint32_t c) { return 0; }
static int __attribute__((stdcall)) p_text_out(void *dc, int x, int y, const char *s, int n) {
    if (p_nouts < 8) snprintf(p_outs[p_nouts], sizeof p_outs[0], "%.*s", n, s);
    p_nouts++;
    return 1;
}

static void p_reset(void) {
    memset(PO, 0, sizeof PO), memset(ppix, 5, sizeof ppix), memset(psp, 0, sizeof psp);
    PTR_(PO, 0) = pvt, PTR_(PO, 0x21d0) = pvbt, PTR_(PO, 0x2358) = pvt;
    struct game_buffer *b = (struct game_buffer *)(PO + 0x444);
    b->bits = ppix, b->width = 200, b->neg_height = -100, b->pitch = 200;
    b->full[2] = 200, b->full[3] = 100, b->clip[2] = 200, b->clip[3] = 100;
    PTR_(PO, 0xa3c) = PO;
    for (int i = 0x3110; i <= 0x311c; i += 4) PTR_(PO, i) = &pfont;
    PTR_(PO, 0x316c) = &pbfont;
    PI(0x3104) = PI(0x3108) = 1, PI(0x2350) = -1, PI(0x21cc) = -1, PI(0xa44) = -1;
    PI(0xa18) = PI(0xa1c) = 0x2000;
    for (int i = 0; i < 3; i++) {
        memset(pspd[i], 9 + i, sizeof pspd[i]);
        psp[i].data = pspd[i], psp[i].key = 1, psp[i].width = psp[i].stride = 4, psp[i].height = 2, psp[i].full_w = 4, psp[i].full_h = 2;
    }
    ncalls = 0;
}

static void test_pop_layout(void) {
    life_image_init();
    game_select_object = p_select_object, game_get_stock_object = p_stock_object, game_get_text_extent = p_text_extent;
    p_dc_slot = &p_fake_dc, game_font_dc = &p_dc_slot;
    p_default_font = &pfont, game_font_default = &p_default_font;
    game_font_slack = &p_slack, game_link_rects = p_link_table, game_text_end_x = &p_text_end;
    game_flying_x = &p_fly_x, game_flying_y = &p_fly_y, game_flying_left = &p_fly_left, game_flying_text = p_fly_text;
    game_flying_gap = " ", game_flying_pad_height = " ", game_flying_pad = " ", game_flying_pad_cent = " ";
    game_set_bk_mode = p_set_bk_mode, game_set_text_color = p_set_text_color, game_text_out = p_text_out;
    game_spot_init = p_spot_init, game_free = life_free;
    game_dialog_calc = p_calc;
    int32_t screen = 800;
    game_win_screen_w = &screen;
    pfont.hfont = (void *)1, pfont.first = -1, pfont.ascent = 9, pfont.height = 12;
    pbfont.hfont = (void *)2, pbfont.first = -1, pbfont.ascent = 8, pbfont.height = 10;
    pvt[0x174 / 4] = (void *)p_174, pvt[0x180 / 4] = (void *)p_180;
    struct game_base_pop *p = (struct game_base_pop *)PO;
    uint32_t handler = (uint32_t)(uintptr_t)game_at(0x6044d0);
    int32_t *g_width = game_at(0x9b8cec), *g_height = game_at(0x9b8ce8), *g_drawing = game_at(0x9b8cf4);

    // The layout with no picture, no lines, no external window: three rows of buttons (two asked for and one for the flags), margins
    // as asked on a narrow screen, a width of what the widget asked (50) and the margins, a button seven quarters of the font high
    // and the room shared by the rows.
    p_reset();
    PI(0x20f4) = 2, PI(0x21c0) = 4, PI(0x21c4) = 3, PI(0x21b0) = 2, PI(0x21b4) = 5, PI(0x21b8) = 6, PI(0x21bc) = 7;
    PD(0x2c) = 50, PI(0x21c8) = 0;
    pcalc_wide = pcalc_high = 0, pcalc_answer = 0;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PI(0x30b0), 3), EXPECT(PI(0x30e0), 4), EXPECT(PI(0x30e4), 3), EXPECT(PI(0x30e8), 6), EXPECT(PI(0x30ec), 7);
    EXPECT(PI(0x30f0), 2), EXPECT(PI(0x30f4), 5), EXPECT(PI(0x30d8), 0), EXPECT(PI(0x30dc), 0);
    EXPECT(PI(0x30d0), 17), EXPECT(PI(0x30cc), (58 - 8 - 2 * 3) / 3), EXPECT(PI(0x30bc), 58 + 5 + 2);
    int text_h = PI(0x30d4);
    EXPECT(PI(0x30c0), 8 + text_h + 17 + 3 + 7 + 6), EXPECT(*g_width, 2 + 4), EXPECT(*g_height, 4 + text_h + 6);
    EXPECT(ncalls, 2), expect_call(0, 0x500, PO + 0x21d0, 50, 0, 0, 0), expect_call(1, 0x174, PO, 0, 0, 0, 0);
    EXPECT(PI(0x3110) != 0, 1);
    // A wide screen (a width of 1024) makes the margins one and a half times; flag 0x400 stops that; a widget of its own size (0x5c)
    // is the width unless the picture is wider; a width asked for takes the margin (21cc) and the widget.
    screen = 1024, PI(0x30a8) = 0;
    pcalc_wide = 70, pcalc_high = 9;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PI(0x30e0), 6), EXPECT(PI(0x30e4), 4), EXPECT(PI(0x30bc), 70 + 12 + 5 + 2), EXPECT(PI(0x30c4), 9);
    PI(0x30a8) = 0x400 | 0x100 | 0x40;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PI(0x30e0), 4), EXPECT(PI(0x30b0), 3), EXPECT(PI(0x30d0), 0);  // (flag 0x100: no button row of its own; 0x40 one more)
    screen = 800, PI(0x30a8) = 0, PI(0x21cc) = 20;
    EXPECT(pop_calculate_dimensions(p), 0), EXPECT(PI(0x30bc), 20 + 4 + 70 + 5 + 2);
    // A picture, scaled 3 to 2: its size scaled, the width with the margin.
    PI(0x21cc) = -1, PTR_(PO, 0x2144) = &psp[0], psp[0].full_w = 20, psp[0].full_h = 8, PI(0x3104) = 3, PI(0x3108) = 2;
    EXPECT(pop_calculate_dimensions(p), 0), EXPECT(PI(0x30d8), 30 + 4), EXPECT(PI(0x30dc), 12);
    EXPECT(PI(0x30bc), 70 + 8 + 5 + 2), EXPECT(*g_width, 2 + 4 + 34);
    PTR_(PO, 0x2144) = NULL, PI(0x3104) = PI(0x3108) = 1;
    // The widget's answer when it isn't 0 ends the layout where it was.
    PI(0x30bc) = -1, pcalc_answer = 5, ncalls = 0;
    EXPECT(pop_calculate_dimensions(p), 5), EXPECT(PI(0x30bc), -1), EXPECT(ncalls, 1);
    pcalc_answer = 0;
    // What a kind of widget is given: the double click function for kinds 1 and any other, the popup's own item, the text limits for
    // kind 4, the picture's scale and the screen's function for kind 8.
    PI(0x2350) = 1;
    EXPECT(pop_calculate_dimensions(p), 0), EXPECT((uint32_t)PI(0x2234), handler);
    PI(0x2350) = 0, PI(0xa44) = -1, PD(0xec) = 99;
    EXPECT(pop_calculate_dimensions(p), 0), EXPECT((uint32_t)PI(0x2224), handler), EXPECT(PD(0xec), 0);
    PI(0x2350) = 4, PI(0xa44) = 7;
    EXPECT(pop_calculate_dimensions(p), 0), EXPECT(((struct game_editgroup *)(PO + 0x22c8))->limit[3], 7);
    PI(0x2350) = 8, PI(0x3160) = 11, PI(0x3164) = 12, PI(0x3168) = 13, PI(0x3104) = 3, PI(0x3108) = 2;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PI(0x22a8), 11), EXPECT(PI(0x22c0), 12), EXPECT(PI(0x22a4), 13), EXPECT(PI(0x2260), 2), EXPECT(PI(0x225c), 3);
    PI(0x3168) = 0, PI(0x3104) = PI(0x3108) = 1, PI(0x2350) = -1, PI(0xa44) = -1;
    // An external window: no margin sideways, the widget given the window's room and laid out for it (its flags 3), the popup the window's
    // size, and the widget's own height cleared when it has no kind.
    PI(0xa14) = 1, PI(0xa18) = 0x2000, PI(0xa1c) = 7, PI(0xa20) = 100, PI(0xa24) = 60, PI(0x30f8) = 1, PI(0x30fc) = 2, pcalc_high = 4;
    ncalls = 0;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PI(0x30e0), 0), EXPECT(PI(0x30e4), 3), EXPECT(PD(0x2c), 100 - 5 - 2), EXPECT(PI(0x30f8), 1), EXPECT(PI(0x30fc), 7);
    EXPECT(PD(0x20) & 3, 3), EXPECT(PI(0x30bc), 100), EXPECT(PI(0x30c0), 60), EXPECT(PI(0x30c4), 0), EXPECT(PD(0x60), 0);
    EXPECT(PD(0x30), 60 - 17 - PI(0x30d4) - 7 - 6), EXPECT(ncalls, 2);  // (flag 0x2 off: the widget is laid out once)
    PI(0x30a8) = 2, PI(0x2350) = 3, ncalls = 0;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(ncalls, 3), EXPECT(PI(0x30c4), 4), EXPECT(PD(0x20) & 3, 3);  // (laid out twice; kind 3 keeps its own height)
    EXPECT(PD(0x60), 4), EXPECT(PI(0x30c0), 60);
    // Drawn in another window (its Buffer measures the text), the width and height go to where the popup is put.
    memcpy(PH + 0x444, PO + 0x444, sizeof(struct game_buffer));
    PI(0xa14) = 0, PI(0x30a8) = 0, PI(0x2350) = -1, PI(0x30b4) = 11, PI(0x30b8) = 13, PTR_(PO, 0xa3c) = PH;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(*g_width, 11 + 2 + 4 + 0), EXPECT(*g_height, 13 + 4 + PI(0x30d4) + 6);
    PTR_(PO, 0xa3c) = PO;
    // The lines: the list is walked from its first line (the place reset), each kind measured, and left at the last. A blank line
    // (kind 3) is a font high.
    p_reset();
    PI(0x21c0) = 4, PD(0x2c) = 50, PI(0x20f4) = 1;
    for (int i = 0; i < 3; i++) {
        pitem[i].text = i == 1 ? "ab cd" : "hello", pitem[i].kind = i == 0 ? 3 : i == 1 ? 2 : 0;
        pnode[i].item = &pitem[i], pnode[i].next = &pnode[(i + 1) % 3], pnode[i].prev = &pnode[(i + 2) % 3];
    }
    PTR_(PO, 0x2188) = pnode, PTR_(PO, 0x218c) = &pnode[2], PI(0x2190) = 3, PI(0x2194) = 2;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PTR_(PO, 0x218c) == &pnode[2], 1), EXPECT(PI(0x2194), 2), EXPECT(PI(0x30d4) >= 12 + 12, 1);
    PI(0x2190) = 1, PTR_(PO, 0x218c) = &pnode[1], PI(0x2194) = 5;
    EXPECT(pop_calculate_dimensions(p), 0);
    EXPECT(PTR_(PO, 0x218c) == &pnode[0], 1), EXPECT(PI(0x2194), 0), EXPECT(PI(0x30d4), 9);  // (a blank line only, its text not measured: the line height, which with no leading is the font's 9)
}

static void test_pop_draw(void) {
    struct game_base_pop *p = (struct game_base_pop *)PO;
    struct game_win *w = (struct game_win *)PO;
    int32_t *g_width = game_at(0x9b8cec), *g_height = game_at(0x9b8ce8), *g_drawing = game_at(0x9b8cf4);
    static void *ddraw;
    game_ddraw = &ddraw, game_intersect_rect = p_intersect, game_equal_rect = p_equal;
    static struct game_sprite_scratch p_scratch;
    static int32_t p_scale[4] = {1, 1, 0, 0}, p_min_size[2];
    static float p_ratio;
    static double p_max_ratio = 4.0;
    static struct palette *p_pal;
    static const uint8_t *p_tint;
    static uint32_t p_hwnd = 1;
    static int32_t p_copy_rem, p_fill_gap, p_mask_units, p_mask_gap_a, p_mask_gap_b;
    game_win_tint = &p_tint, game_palette_copy = &p_pal, game_win_hwnd = &p_hwnd;
    game_sprite_scale = p_scale, game_sprite_ratio = &p_ratio, game_sprite_max_ratio = &p_max_ratio, game_sprite_min_size = p_min_size;
    game_sprite_scratch = &p_scratch;
    game_copy_rem = &p_copy_rem, game_fill_gap = &p_fill_gap, game_mask_units = &p_mask_units, game_mask_gap_a = &p_mask_gap_a;
    game_mask_gap_b = &p_mask_gap_b;
    pvt[0x180 / 4] = (void *)p_180;
    // The background is a fill (the popup's colour) unless the window's function (slot 0x180) says it has been drawn; the popup is
    // put where it is asked to be, and Buffer_clear_links asks the Buffer for its links to be made anew.
    p_reset();
    PI(0x310c) = 77, PI(0x30b4) = 10, PI(0x30b8) = 20, p180_answer = 0, p_spot_inits = 0;
    pop_on_redraw(p);
    EXPECT(ppix[0], 77), EXPECT(ppix[200 * 99 + 199], 77), EXPECT(w->inner[0], 10), EXPECT(w->inner[1], 20), EXPECT(p_spot_inits, 1);
    EXPECT(ncalls, 1), expect_call(0, 0x180, PO, 0, 0, 0, 0);
    memset(ppix, 5, sizeof ppix), p180_answer = 1, ncalls = 0;
    pop_on_redraw(p);
    EXPECT(ppix[0], 5), EXPECT(ncalls, 1);
    // A check can hold the link clearing: it is logged and does nothing.
    p_spot_inits = 0, buffer_clear_links_held = 1, buffer_clear_links_log.n = 0, buffer_clear_links_log.hash = 0;
    pop_on_redraw(p);
    buffer_clear_links_held = 0;
    EXPECT(p_spot_inits, 0), EXPECT(buffer_clear_links_log.n, 1), EXPECT(buffer_clear_links_log.hash, (uint32_t)(uintptr_t)(PO + 0x444));
    // The background tile takes the place of the fill without an external window.
    memset(ppix, 5, sizeof ppix), p180_answer = 0, PTR_(PO, 0x2148) = &psp[1];
    pop_on_redraw(p);
    EXPECT(ppix[0], 10), EXPECT(ppix[3], 10), EXPECT(ppix[4], 10), EXPECT(ppix[200 * 99 + 199], 10);  // (tiled over the whole Buffer)
    PTR_(PO, 0x2148) = NULL;
    // The frame picture, centred (with the indent) if it is narrower and lower than the Buffer.
    memset(ppix, 5, sizeof ppix), p180_answer = 1, PI(0x21c8) = 1, PTR_(PO, 0x214c) = &psp[2];
    pop_on_redraw(p);
    EXPECT(ppix[49 * 200 + 98], 11), EXPECT(ppix[49 * 200 + 97], 5), EXPECT(ppix[48 * 200 + 98], 5);
    psp[2].width = 300;
    memset(ppix, 5, sizeof ppix);
    pop_on_redraw(p);
    EXPECT(ppix[49 * 200 + 98], 5);
    psp[2].width = 4;
    // The picture goes to the function a screen set, with the window, the id of the SpriteBox's first entry and the place (the popup
    // is named in the global meanwhile), or is drawn scaled.
    PTR_(PO, 0x214c) = NULL, PI(0x30e0) = 4, PI(0x30f0) = 2, PI(0x30e8) = 6;
    PTR_(PO, 0x2144) = &psp[0], PTR_(PO, 0x3168) = (void *)p_handler;
    snode[0].item = &sitem_[0], snode[0].key = 33, snode[0].next = &snode[1], snode[0].prev = &snode[1];
    snode[1].item = &sitem_[1], snode[1].key = 34, snode[1].next = &snode[0], snode[1].prev = &snode[0];
    PTR_(PO, 0x2264 + 8) = snode, PTR_(PO, 0x2264 + 0xc) = &snode[1], PI(0x2264 + 0x10) = 2, PI(0x2264 + 0x14) = 1;
    memset(ppix, 5, sizeof ppix), ncalls = 0, PI(0x30d8) = 3, PI(0x30dc) = 5;
    pop_on_redraw(p);
    EXPECT(ncalls, 2), expect_call(1, 0x300, &psp[0], (int)(intptr_t)PO, 33, 7 * 1000 + 10, 1), EXPECT(*g_drawing, 0);
    EXPECT(PTR_(PO, 0x2264 + 0xc) == snode, 1), EXPECT(PI(0x2264 + 0x14), 0), EXPECT(ppix[10 * 200 + 7], 5);
    // (no text: the width without the picture and the indent, and the height at least the picture's below the margin)
    EXPECT(*g_width, 7 + 3 - 3 - 1), EXPECT(*g_height, 10 > 5 + 10 ? 10 : 5 + 10);
    PTR_(PO, 0x3168) = NULL;
    pop_on_redraw(p);
    EXPECT(ppix[10 * 200 + 7], 9), EXPECT(ppix[11 * 200 + 10], 9), EXPECT(ppix[10 * 200 + 6], 5), EXPECT(*g_drawing, 0);
    PI(0x30dc) = 0, PI(0x3104) = 2, PI(0x3108) = 1;  // (twice as large)
    memset(ppix, 5, sizeof ppix);
    pop_on_redraw(p);
    EXPECT(ppix[10 * 200 + 7], 9), EXPECT(ppix[10 * 200 + 14], 9), EXPECT(ppix[10 * 200 + 15], 5), EXPECT(*g_height, 10);
    // Text: the lines are drawn from the right of the picture (width and height as far as they came) in the popup's colours.
    PTR_(PO, 0x2144) = NULL, PI(0x30d8) = 0, PI(0x30d4) = 12, PI(0x30bc) = 100, PI(0x30dc) = 0;
    pitem[0].text = "hi", pitem[0].kind = 0;
    pnode[0].item = &pitem[0], pnode[0].next = pnode[0].prev = &pnode[0];
    PTR_(PO, 0x2188) = pnode, PTR_(PO, 0x218c) = pnode, PI(0x2190) = 1, PI(0x2194) = 0;
    PI(0x3120) = 31, PI(0x3124) = 32, PI(0x3128) = 33, PI(0x312c) = 34;
    p_nouts = 0;
    *(void **)(PO + 0x444 + 0x64) = &p_fake_dc;  // (the device context text is drawn through)
    pop_on_redraw(p);
    EXPECT(p_nouts >= 1, 1), EXPECT(strcmp(p_outs[0], "hi"), 0), EXPECT(*g_width, 7 - 0 - 1), EXPECT(*g_height > 10, 1);
    // In another window (its Buffer is drawn in; the popup's own only has its links cleared), the place is added to where the popup is put
    // and nothing is moved or filled.
    memcpy(PH + 0x444, PO + 0x444, sizeof(struct game_buffer));
    memset(ppix, 5, sizeof ppix), PTR_(PO, 0xa3c) = PH, PI(0x30b4) = 20, PI(0x30b8) = 30, p_nouts = 0, ncalls = 0;
    pop_on_redraw(p);
    EXPECT(w->inner[0], 10), EXPECT(ncalls, 1), EXPECT(p_nouts >= 1, 1), EXPECT(*g_width, 7 + 20 - 0 - 1);
    PTR_(PO, 0xa3c) = PO;
}

int main(void) {
    vt[1] = (void *)s_4, vt[0x8 / 4] = (void *)s_8, vt[0xb0 / 4] = (void *)s_b0, vt[0xe8 / 4] = (void *)s_e8, vt[0xf8 / 4] = (void *)s_f8;
    memcpy(vt_sb, vt, sizeof vt_sb);
    vt_sb[1] = (void *)(intptr_t)0x94;
    item_vt[0] = (void *)s_dtor, item_vt[1] = NULL;
    list_vt[1] = (void *)s_list1;
    game_dialog_data_close = data_close, game_dialog_data_alloc = data_alloc, game_post_message = post_message;
    game_dialog_defs_a = &da, game_dialog_defs_b = &db, game_pop_defs = &pdefs, game_pop_fonts = &pfonts;
    game_pop_ext_a = ext_a, game_pop_ext_b = ext_b, game_pop_open = &pop_open, game_pop_fallout_on = &pop_fallout_on;
    game_win_screen_w = &screen_w, game_win_hwnd = &hwnd, game_win_modal_focus = &focus, game_pop_file = pop_file;
    f_ok.hfont = (void *)1;

    // ---- Text ----
    char b[40];
    EXPECT((char *)(intptr_t)purge_leading(strcpy(b, "   ab c ")) == b + 3, 1);
    EXPECT((char *)(intptr_t)purge_leading(strcpy(b, "ab")) == b, 1);
    EXPECT((char *)(intptr_t)purge_leading(strcpy(b, "    ")) == b + 4, 1);
    purge_trailing(strcpy(b, "ab c   ")), EXPECT(strcmp(b, "ab c"), 0);
    purge_trailing(strcpy(b, "   ")), EXPECT(b[0], 0);
    purge_trailing(strcpy(b, "")), EXPECT(b[0], 0);
    purge_trailing(strcpy(b, " a")), EXPECT(strcmp(b, " a"), 0);
    purge_spaces(strcpy(b, "  hello  world  ")), EXPECT(strcmp(b, "hello  world"), 0);
    purge_spaces(strcpy(b, "  ")), EXPECT(b[0], 0);
    purge_spaces(strcpy(b, "x")), EXPECT(strcmp(b, "x"), 0);
    kill_lf(strcpy(b, "ab\ncd\n")), EXPECT(strcmp(b, "ab\ncd"), 0);  // (the last one: strrchr)
    kill_lf(strcpy(b, "ab\ncd")), EXPECT(strcmp(b, "ab"), 0);
    kill_lf(strcpy(b, "abcd")), EXPECT(strcmp(b, "abcd"), 0);
    add_lf(strcpy(b, "ab")), EXPECT(strcmp(b, "ab\n"), 0);
    add_lf(strcpy(b, "")), EXPECT(strcmp(b, "\n"), 0);

    // ---- Text variables ----
    static int32_t nums[10], genders[10], plurals[10], language;
    static char vars[2560];
    static const int8_t range[2] = {'0', '9'};
    static char articles[6][20] = {" de ", " le ", " la ", "De ", "Le ", "La "};
    game_parse_nums = nums, game_parse_genders = genders, game_parse_plurals = plurals, game_parse_text = vars;
    game_findnum_range = range, game_fr_articles = articles[0], game_language = &language;
    {
        char s1[] = "ab 7 cd x9";
        EXPECT(findnum(s1) == s1 + 3, 1), EXPECT(findnum(s1 + 4) == s1 + 9, 1), EXPECT(findnum(strcpy(b, "none")), 0);
        EXPECT(findnum(NULL), 0), EXPECT(findnum(strcpy(b, "")), 0);
        char out[512];
#define PS(src, ...) (out[0] = 0, parse_string(src, out))
        memset(out, 'Z', sizeof out);
        EXPECT(parse_string(NULL, out), 3), EXPECT(parse_string("x", NULL), 3), EXPECT(out[0], 'Z');
        nums[0] = 12, nums[3] = -45, nums[9] = 1;
        EXPECT(PS("no dollar", out), 0), EXPECT(strcmp(out, "no dollar"), 0);
        EXPECT(PS("", out), 0), EXPECT(out[0], 0);
        // a line with no directive is added to the buffer's text (the original's strcat)
        strcpy(out, "old"), EXPECT(parse_string("text", out), 0), EXPECT(strcmp(out, "oldtext"), 0);
        strcpy(out, "old"), EXPECT(parse_string("$X text", out), 0), EXPECT(strcmp(out, "old$X text"), 0);
        strcpy(out, "old"), EXPECT(parse_string("a $NUM0 b", out), 0), EXPECT(strcmp(out, "a 12 b"), 0);  // (a directive starts it afresh)
        strcpy(out, "old"), EXPECT(parse_string("a $$ b", out), 0), EXPECT(strcmp(out, "a $ b"), 0);
        EXPECT(PS("a $$ b $NUM0 c $NUMBER3 d $HEX9.", out), 0);
        EXPECT(strcmp(out, "a $ b 12 c -45 d 1."), 0);
        // an error stops the line, with what came before it (and what was before the failing word is not copied)
        memset(out, 'Z', sizeof out);
        EXPECT(PS("ok $NUM3 then $NUMx more", out), 14), EXPECT(strcmp(out, "ok -45"), 0);
        memset(out, 'Z', sizeof out);
        EXPECT(PS("$HEXq", out), 14), EXPECT(out[0], 0);
        EXPECT(PS("$NUMBERq", out), 14);
        // the strings
        strcpy(vars + 2 * 256, "Zakharov"), strcpy(vars + 5 * 256, "");
        EXPECT(PS("Hi $NAME2, ok", out), 0), EXPECT(strcmp(out, "Hi Zakharov, ok"), 0);
        EXPECT(PS("a $X b 5 c $Y", out), 0), EXPECT(strcmp(out, "a  c $Y"), 0);  // (the first digit anywhere)
        EXPECT(PS("a $ZZ", out), 0), EXPECT(strcmp(out, "a $ZZ"), 0);  // (no digit: the rest goes as it is)
        EXPECT(PS("a $HALF 2 b", out), 0), EXPECT(strcmp(out, "a Zakharov b"), 0);
        EXPECT(PS("a $NAME5 b", out), 0), EXPECT(strcmp(out, "a  b"), 0);
        EXPECT(PS("$LINK<a>b$LINKS 2", out), 0), EXPECT(strcmp(out, "$LINK<a>bZakharov"), 0);
        // choices
        strcpy(vars + 1 * 256, "he:she:it"), strcpy(vars + 4 * 256, "a:b:c:d"), strcpy(vars + 6 * 256, "u:v");
        strcpy(vars + 7 * 256, "p:q:r:s:t:w"), strcpy(vars + 8 * 256, "k:l:m:n:o");
        genders[1] = 1, plurals[1] = 0, genders[2] = 2, plurals[2] = 1;
        EXPECT(PS("$<M:he:she>|$<F:he:she>|$<M1:x:y>|$<M2:x:y>|$<m:x:y:z>|$<n:x:y:z>", out), 0);
        EXPECT(strcmp(out, "he|he|x|y|x|z"), 0);  // (1 colon: the number, a digit after M sets it; 2: gender)
        EXPECT(PS("$<1:x:y>$<2:x:y>$<#9:s:p>$<#0:s:p>$<#3:s:p>", out), 0);
        EXPECT(strcmp(out, "xyspp"), 0);  // (1 is the only singular)
        EXPECT(PS("$<F:a:b:c:d>$<M2:a:b:c:d>$<N1:a:b:c:d>$<F2:a:b:c:d>", out), 0);
        EXPECT(strcmp(out, "bcbd"), 0);  // (3 colons: gender not 0, and plural)
        EXPECT(PS("$<F:0:1:2:3:4:5>$<N2:0:1:2:3:4:5>$<M:0:1:2:3:4>$<M:0:1:2:3:4:5:6:7>", out), 0);
        EXPECT(strcmp(out, "1547"), 0);  // (5 colons: gender and 3 for plural; other counts: the count)
        EXPECT(PS("[$< F :x:y:z>]", out), 0), EXPECT(strcmp(out, "[y]"), 0);
        // the words in a variable
        EXPECT(PS("$<M 1>|$<F 1>|$<N 1>|$<M 4>|$<F2 4>|$<N 6>|$<M 7>|$<M 8>", out), 0);
        EXPECT(strcmp(out, "he|she|it|a|d|u|p|o"), 0);
        EXPECT(PS("x$<1>y", out), 0), EXPECT(strcmp(out, "xy"), 0);  // (no digit after it)
        EXPECT(PS("$<M 5>|$<M 3>|$<M>|$<Q:a>.", out), 0), EXPECT(strcmp(out, "|||."), 0);
        EXPECT(PS("$<M:a:b", out), 0), EXPECT(strcmp(out, "<M:a:b"), 0);  // (no end: the dollar sign goes)
        // a variable's whole room
        memset(vars + 2 * 256, 'w', 255), vars[2 * 256 + 255] = 0;
        EXPECT(PS("<$NAME2>", out), 0), EXPECT(strlen(out), 255 + 2);
        strcpy(vars + 2 * 256, "Zakharov");
        // French
        language = 1;
        EXPECT(PS("a le arbre et la eau de homme de chat", out), 0);
        EXPECT(strcmp(out, "a l'arbre et l'eau d'homme de chat"), 0);
        EXPECT(PS("Le hotel, La eau. Les yak", out), 0), EXPECT(strcmp(out, "L'hotel, L'eau. Les yak"), 0);
        EXPECT(PS("$NAME2 de $NAME1", out), 0), EXPECT(strcmp(out, "Zakharov d'he:she:it"), 0);
        language = 0;
        EXPECT(PS("a le arbre", out), 0), EXPECT(strcmp(out, "a le arbre"), 0);
    }

    // ---- Script text ----
    EXPECT(btoi("1011"), 11), EXPECT(btoi("102"), 2), EXPECT(btoi("x"), 0), EXPECT(btoi(""), 0), EXPECT(btoi("0"), 0);
    EXPECT(htoi("1F"), 31), EXPECT(htoi("ffz"), 255), EXPECT(htoi("aB9"), 0xab9), EXPECT(htoi("g"), 0), EXPECT(htoi(""), 0);
    EXPECT(htoi("FFFFFFFF"), -1), EXPECT(btoi("11111111111111111111111111111111"), -1);
    EXPECT(stoi("42"), 42), EXPECT(stoi("-7"), -7), EXPECT(stoi("  9"), 9), EXPECT(stoi("abc"), 0), EXPECT(stoi(""), 0);
    EXPECT(stoi("0"), 0), EXPECT(stoi("007"), 7), EXPECT(stoi("0b101"), 5), EXPECT(stoi("0B11x"), 3), EXPECT(stoi("0x1f"), 31);
    EXPECT(stoi("0Xa0"), 160), EXPECT(stoi("0d19"), 19), EXPECT(stoi("0D8"), 8), EXPECT(stoi("0c5"), 0), EXPECT(stoi("0xg"), 0);
    EXPECT(stoi("0b"), 0), EXPECT(stoi("0x"), 0), EXPECT(stoi("0d"), 0), EXPECT(stoi("00x5"), 0);
    EXPECT(stoi("4294967297"), 1), EXPECT(stoi("-2147483649"), 2147483647), EXPECT(stoi("\t 5"), 5);  // (it wraps)
    {
        static char line[] = "alpha, 12 ,0x1F,0b101,,  tail  ", item[64];
        static char *cursor, *item_buf = item;
        game_text_cursor = &cursor, game_text_item_buf = &item_buf;
        cursor = line;
        EXPECT(text_item() == (int)(intptr_t)item, 1), EXPECT(strcmp(item, "alpha"), 0), EXPECT(cursor == line + 6, 1);
        EXPECT(text_item_number(), 12), EXPECT(cursor == line + 11, 1);
        EXPECT(text_item_number(), 31), EXPECT(text_item_binary(), 0), EXPECT(strcmp(item, "0b101"), 0);  // (the binary digits start at once)
        EXPECT(text_item_number(), 0), EXPECT(strcmp(item, ""), 0);  // (an empty item)
        EXPECT(text_item_hex(), 0), EXPECT(strcmp(item, "tail"), 0), EXPECT(*cursor, 0);  // (at the end: no step)
        EXPECT(text_item_hex(), 0), EXPECT(*cursor, 0), EXPECT(item[0], 0);
        static char hexline[] = "ff,zz";
        cursor = hexline;
        EXPECT(text_item_hex(), 255), EXPECT(text_item_hex(), 0);
        // on a Text object
        static uint8_t obj[0x160];
        *(char **)(obj + 0x150) = line, *(char **)(obj + 0x15c) = item;
        EXPECT(text_obj_item(obj) == (int)(intptr_t)item, 1), EXPECT(strcmp(item, "alpha"), 0), EXPECT(*(char **)(obj + 0x150) == line + 6, 1);
        EXPECT(text_obj_item_number(obj), 12), EXPECT(text_obj_item_hex(obj), 0), EXPECT(text_obj_item_binary(obj), 0);
        EXPECT(*(char **)(obj + 0x150) == line + 22, 1);
        // the index: a pointer to labels, each an offset, a name and a 0
        static char index[64];
        size_t at = 0;
        const char *const labels[3] = {"Alpha", "BETA", "g"};
        for (int i = 0; i < 3; i++) {
            *(int32_t *)(index + at) = 100 + i, at += 4;
            strcpy(index + at, labels[i]), at += strlen(labels[i]) + 1;
        }
        static uint8_t text[0x118];
        strcpy((char *)text, "Script.TXT");
        *(int32_t *)(text + 0x100) = 3, *(char **)(text + 0x108) = index;
        EXPECT(text_index_search(text, "script", "#alpha"), 100), EXPECT(text_index_search(text, "SCRIPT.txt", "beta"), 101);
        EXPECT(text_index_search(text, "script", "G"), 102), EXPECT(text_index_search(text, "script", "#gg"), -1);
        EXPECT(text_index_search(text, "other", "alpha"), -1), EXPECT(text_index_search(text, "script.bin", "alpha"), -1);
        *(int32_t *)(text + 0x100) = 2;
        EXPECT(text_index_search(text, "script", "g"), -1);  // (only two labels)
        // the game's four indexes: the first with labels that has it
        static uint8_t slots[4 * 0x118];
        game_text_slots = slots;
        EXPECT(text_search_index("script", "alpha"), -1);
        memcpy(slots + 2 * 0x118, text, sizeof text);
        EXPECT(text_search_index("script", "beta"), 101), EXPECT(text_search_index("script", "g"), -1);
        memcpy(slots + 3 * 0x118, text, sizeof text);
        *(int32_t *)(slots + 3 * 0x118 + 0x100) = 3;
        EXPECT(text_search_index("script", "g"), 102), EXPECT(text_search_index("script", "alpha"), 100);
    }

    // ---- Popup entry points ----
    game_pops_18 = pops_18_stub, game_pop_ask_6 = pop_ask_6_stub;
    EXPECT_FWD(popup, pops, (11, 22), {POPFILE_, 11, -1, 0, 0, 0, 1, 1, 22});
    EXPECT_FWD(popup_2, pops, (11, 22, 33), {11, 22, -1, 0, 0, 0, 1, 1, 33});
    EXPECT_FWD(popup_3, pops, (11, 22, XYZ, 44, 55), {11, 22, 3, XYZ, 44, 0, 1, 1, 55});
    EXPECT_FWD(popup_4, pops, (11, 22, 33, 44), {POPFILE_, 11, 22, 0, 33, 0, 1, 1, 44});
    EXPECT_FWD(popup_5, pops, (11, 22, 33, 44, 55), {11, 22, 33, 0, 44, 0, 1, 1, 55});
    EXPECT_FWD(popup_6, pops, (11, 22, 33), {POPFILE_, 11, -1, 0, 22, 0, 1, 1, 33});
    EXPECT_FWD(popup_7, pops, (11, 22, 33, 44), {11, 22, -1, 0, 33, 0, 1, 1, 44});
    EXPECT_FWD(popup_8, pops, (11, 22, 33, 44, 55), {POPFILE_, 11, 22, 33, 44, 0, 1, 1, 55});
    EXPECT_FWD(popup_9, pops, (11, 22, 33, 44, 55, 66), {11, 22, 33, 44, 55, 0, 1, 1, 66});
    EXPECT_FWD(popups, pops, (11, 22, 33), {POPFILE_, 11, -1, 0, 0, 22, 1, 1, 33});
    EXPECT_FWD(popups_2, pops, (11, 22, 33, 44, 55), {POPFILE_, 11, -1, 0, 0, 22, 33, 44, 55});
    EXPECT_FWD(popups_3, pops, (11, 22, 33, 44), {11, 22, -1, 0, 0, 33, 1, 1, 44});
    EXPECT_FWD(popups_4, pops, (11, 22, 33, 44, 55, 66), {11, 22, -1, 0, 0, 33, 44, 55, 66});
    EXPECT_FWD(popups_5, pops, (11, 22, 33, 44, 55), {POPFILE_, 11, 22, 0, 33, 44, 1, 1, 55});
    EXPECT_FWD(popups_6, pops, (11, 22, 33, 44, 55, 66, 77), {POPFILE_, 11, 22, 0, 33, 44, 55, 66, 77});
    EXPECT_FWD(popups_7, pops, (11, 22, XYZ, 44, 55, 66), {11, 22, 3, XYZ, 44, 55, 1, 1, 66});
    EXPECT_FWD(popups_8, pops, (11, 22, XYZ, 44, 55, 66, 77, 88), {11, 22, 3, XYZ, 44, 55, 66, 77, 88});
    EXPECT_FWD(popups_9, pops, (11, 22, 33, 44, 55, 66), {11, 22, 33, 0, 44, 55, 1, 1, 66});
    EXPECT_FWD(popups_10, pops, (11, 22, 33, 44, 55, 66, 77, 88), {11, 22, 33, 0, 44, 55, 66, 77, 88});
    EXPECT_FWD(popups_11, pops, (11, 22, 33, 44), {POPFILE_, 11, -1, 0, 22, 33, 1, 1, 44});
    EXPECT_FWD(popups_12, pops, (11, 22, 33, 44, 55, 66), {POPFILE_, 11, -1, 0, 22, 33, 44, 55, 66});
    EXPECT_FWD(popups_13, pops, (11, 22, 33, 44, 55), {11, 22, -1, 0, 33, 44, 1, 1, 55});
    EXPECT_FWD(popups_14, pops, (11, 22, 33, 44, 55, 66, 77), {11, 22, -1, 0, 33, 44, 55, 66, 77});
    EXPECT_FWD(popups_15, pops, (11, 22, 33, 44, 55, 66), {POPFILE_, 11, 22, 33, 44, 55, 1, 1, 66});
    EXPECT_FWD(popups_16, pops, (11, 22, 33, 44, 55, 66, 77, 88), {POPFILE_, 11, 22, 33, 44, 55, 66, 77, 88});
    EXPECT_FWD(popups_17, pops, (11, 22, 33, 44, 55, 66, 77), {11, 22, 33, 44, 55, 66, 1, 1, 77});
    EXPECT_FWD(popup_ask, ask, (11, 22, 33, 44, 55), {POPFILE_, 11, 22, 33, 44, 55});
    EXPECT_FWD(popup_ask_2, ask, (11, 22, 33, 44, 55), {11, 22, 33, 0, 44, 55});
    EXPECT_FWD(popup_ask_3, ask, (11, 22, 33, 44), {POPFILE_, 11, 22, 0, 33, 44});
    EXPECT_FWD(popup_ask_4, ask, (11, 22, XYZ, 44, 55), {11, 22, 3, XYZ, 44, 55});
    EXPECT_FWD(popup_ask_5, ask, (11, XYZ, 33, 44), {POPFILE_, 11, 3, XYZ, 33, 44});
    // (a null string answers -1 without a call)
    fwd_calls = 0;
    EXPECT(popup_3(1, 2, 0, 4, 5), -1), EXPECT(popups_7(1, 2, 0, 4, 5, 6), -1), EXPECT(popups_8(1, 2, 0, 4, 5, 6, 7, 8), -1);
    EXPECT(popup_ask_4(1, 2, 0, 4, 5), -1), EXPECT(popup_ask_5(1, 0, 3, 4), -1), EXPECT(fwd_calls, 0);

    // ---- Dialog ----
    struct game_dialog d;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) da.color[i][j] = 100 + 10 * i + j;
    da.w = 11, da.v44 = 12, da.v4c = 13, da.v50 = 14, da.v6c = 15, da.flags = 16;
    db.h = 21, db.v34 = 22, db.v40 = 23, db.font = (void *)24, db.font_a = 25, db.font_b = 26, db.vac = 27, db.vb0 = 28, db.vb4 = 29, db.vb8 = 30;
    memset(&d, 0xab, sizeof d);
    d.vtable = vt, d.list.vtable = list_vt, d.list.head = NULL, d.list.count = 0;
    ncalls = 0;
    EXPECT(dialog_close(&d), -1);
    expect_call(0, 0x300, &d.data_flag, 0, 0, 0, 0), EXPECT(ncalls, 1);
    EXPECT(d.ext == NULL, 1), EXPECT(d.size, -1), EXPECT(d.flags, 16), EXPECT(d.x, 0), EXPECT(d.y, 0), EXPECT(d.w, 11), EXPECT(d.h, 21);
    EXPECT(d.sel, 0), EXPECT(d.hover, -1), EXPECT(d.v38, 0), EXPECT(d.v3c, 0), EXPECT(d.v40, 23), EXPECT(d.v44, 12), EXPECT(d.v48, -1);
    EXPECT(d.v34, 22), EXPECT(d.v4c, 13), EXPECT(d.v50, 14), EXPECT(d.v54[0], 0), EXPECT(d.v54[5], 0), EXPECT(d.v6c, 15);
    EXPECT(d.vac, 27), EXPECT(d.vb0, 28), EXPECT(d.vb4, 29), EXPECT(d.vb8, 30);
    EXPECT(d.font == (void *)24, 1), EXPECT(d.font_a, 25), EXPECT(d.font_b, 26);
    EXPECT(d.color[0][0], 100), EXPECT(d.color[1][2], 112), EXPECT(d.color[3][1], 131), EXPECT(d.color[2][0], 120);
    // The list is emptied: each item told to the list, deleted, then its node.
    set_list(&d.list, 3);
    ncalls = 0;
    dialog_close(&d);
    expect_call(0, 0x101, &d.list, (int)(intptr_t)&items[0], 0, 0, 0), expect_call(1, 0x100, &items[0], 1, 0, 0, 0);
    expect_call(2, 0x100, &nodes[0], 1, 0, 0, 0);
    expect_call(3, 0x101, &d.list, (int)(intptr_t)&items[1], 0, 0, 0), expect_call(4, 0x100, &items[1], 1, 0, 0, 0);
    expect_call(5, 0x100, &nodes[1], 1, 0, 0, 0), expect_call(8, 0x100, &nodes[2], 1, 0, 0, 0);
    expect_call(9, 0x300, &d.data_flag, 0, 0, 0, 0), EXPECT(ncalls, 10);
    EXPECT(d.list.head == NULL, 1), EXPECT(d.list.count, 0), EXPECT(d.list.idx, 0);
    EXPECT(nodes[0].item == NULL, 1);
    // An item that is not there is not deleted.
    set_list(&d.list, 2), nodes[0].item = NULL, ncalls = 0;
    dialog_close(&d);
    expect_call(0, 0x101, &d.list, 0, 0, 0, 0), expect_call(1, 0x100, &nodes[0], 1, 0, 0, 0), expect_call(2, 0x101, &d.list, (int)(intptr_t)&items[1], 0, 0, 0);
    // init: needs a size of 0 or more; the data is made when it is not 0.
    d.size = -1, EXPECT(dialog_init(&d), 1);
    d.size = 0, ncalls = 0;
    EXPECT(dialog_init(&d), 0), EXPECT(d.list.buf == NULL, 1), EXPECT(ncalls, 0);
    d.size = 24, EXPECT(dialog_init(&d), 0), EXPECT(d.list.buf == (void *)&d.data_flag, 1);
    expect_call(0, 0x301, &d.data_flag, 24, 0, 0, 0), EXPECT(d.data == data_block, 1), EXPECT(d.data_size, 24);
    data_fail = 1, d.size = 9, ncalls = 0, d.list.buf = NULL;
    EXPECT(dialog_init(&d), 4), EXPECT(d.list.buf == NULL, 1);
    data_fail = 0;
    // The sizes and places.
    ncalls = 0;
    EXPECT(dialog_init_2(&d, 3, 4, 50, 60, 0), 0);
    EXPECT(d.x, 3), EXPECT(d.y, 4), EXPECT(d.w, 50), EXPECT(d.h, 60), EXPECT(d.flags, 16 | 3), EXPECT(d.size, 0), EXPECT(d.list.buf == NULL, 1);
    EXPECT(dialog_init_2(&d, 3, 4, 50, 60, 16), 0), EXPECT(d.size, 16), EXPECT(d.list.buf == (void *)&d.data_flag, 1);
    data_fail = 1, EXPECT(dialog_init_2(&d, 1, 2, 3, 4, 8), 4), data_fail = 0;
    EXPECT(d.x, 1), EXPECT(d.size, 8);
    int32_t rect[4] = {10, 20, 40, 100};
    EXPECT(dialog_init_3(&d, rect, 0), 0), EXPECT(d.x, 10), EXPECT(d.y, 20), EXPECT(d.w, 30), EXPECT(d.h, 80), EXPECT(d.flags, 16 | 3);
    ncalls = 0, d.x = 99;
    EXPECT(dialog_init_3(&d, NULL, 5), 3), EXPECT(d.x, 99), EXPECT(ncalls, 0);
    EXPECT(dialog_init_3(&d, rect, 12), 0), EXPECT(d.size, 12), EXPECT(d.list.buf == (void *)&d.data_flag, 1);
    EXPECT(dialog_init_4(&d, 0), 0), EXPECT(d.size, 0), EXPECT(d.x, 0), EXPECT(d.list.buf == NULL, 1);
    EXPECT(dialog_init_4(&d, 7), 0), EXPECT(d.size, 7), EXPECT(d.list.buf == (void *)&d.data_flag, 1);
    int block[2];
    EXPECT(dialog_init_5(&d, 5, 6, 7, 8, block), 0);
    EXPECT(d.x, 5), EXPECT(d.y, 6), EXPECT(d.w, 7), EXPECT(d.h, 8), EXPECT(d.ext == block, 1), EXPECT(d.list.buf == block, 1), EXPECT(d.flags, 16 | 3);
    EXPECT(d.size, -1);
    EXPECT(dialog_init_6(&d, rect, block), 0), EXPECT(d.x, 10), EXPECT(d.w, 30), EXPECT(d.h, 80), EXPECT(d.ext == block, 1);
    EXPECT(dialog_init_6(&d, NULL, block), 3);
    d.flags = 0, d.x = 77;
    EXPECT(dialog_init_7(&d, block), 0), EXPECT(d.size, -1), EXPECT(d.ext == block, 1), EXPECT(d.list.buf == block, 1), EXPECT(d.flags, 16);
    EXPECT(d.x, 0);
    // UNK1: slot 1 with the place.
    d.x = 8, d.y = 9, ncalls = 0;
    EXPECT(dialog_unk1(&d, 3, 4), 44), expect_call(0, 4, &d, 3, 8, 9, 4);
    // The setters.
    EXPECT(dialog_set_font(&d, NULL, 1, 2), 3);
    EXPECT(dialog_set_font(&d, &f_ok, 5, 6), 0), EXPECT(d.font == &f_ok, 1), EXPECT(d.font_a, 5), EXPECT(d.font_b, 6);
    EXPECT(dialog_set_font(&d, &f_bad, 7, 8), 0), EXPECT(d.font == &f_ok, 1), EXPECT(d.font_a, 7), EXPECT(d.font_b, 8);
    dialog_set_text_color(&d, 1, 2, 3, 4), dialog_set_text_color2(&d, 5, 6, 7, 8), dialog_set_text_color3(&d, 9, 10, 11, 12);
    EXPECT(d.color[0][0], 1), EXPECT(d.color[1][0], 2), EXPECT(d.color[2][0], 3), EXPECT(d.color[3][0], 4);
    EXPECT(d.color[0][1], 5), EXPECT(d.color[3][1], 8), EXPECT(d.color[0][2], 9), EXPECT(d.color[3][2], 12);
    EXPECT(dialog_set_def_font(NULL, 1, 2), 3), EXPECT(dialog_set_def_font(&f_ok, 3, 4), 0), EXPECT(db.font == &f_ok, 1), EXPECT(db.font_a, 3);
    EXPECT(dialog_set_def_font(&f_bad, 5, 6), 0), EXPECT(db.font == &f_ok, 1), EXPECT(db.font_b, 6);
    dialog_set_def_text_color(1, 2, 3, 4, 2);
    EXPECT(da.color[0][2], 1), EXPECT(da.color[1][2], 2), EXPECT(da.color[2][2], 3), EXPECT(da.color[3][2], 4);
    dialog_set_def_text_color(5, 6, 7, 8, 9);  // (out of range: the first)
    EXPECT(da.color[0][0], 5), EXPECT(da.color[3][0], 8);
    dialog_set_def_text_color(9, 8, 7, 6, 3);  // (3 reaches the next row, and the last row's the flags)
    EXPECT(da.color[1][0], 9), EXPECT(da.color[2][0], 8), EXPECT(da.color[3][0], 7), EXPECT(da.flags, 6);
    dialog_set_def_text_color(1, 1, 1, 1, -4), EXPECT(da.color[0][0], 1);

    // ---- BasePop ----
    struct game_base_pop *p = &BP.pop;
    reset_bp();
    screen_w = 1024;
    pop_set_height(p, 100), EXPECT(BP.dlg.h, 150);
    pop_set_height(p, 101), EXPECT(BP.dlg.h, 151);
    pop_set_height(p, -101), EXPECT(BP.dlg.h, -151);
    screen_w = 1023, pop_set_height(p, 100), EXPECT(BP.dlg.h, 100);
    screen_w = 1280, p->flags = 0x400, pop_set_height(p, 100), EXPECT(BP.dlg.h, 100);
    p->flags = 0, p->ext = ext_win, pop_set_height(p, 100), EXPECT(BP.dlg.h, 100);
    pop_set_loc(p, 5, 6), EXPECT(p->x, 5), EXPECT(p->y, 6);
    pop_set_loc(p, 0x2000, 7), EXPECT(p->x, 5), EXPECT(p->y, 7);
    pop_set_loc(p, 8, 0x2000), EXPECT(p->x, 8), EXPECT(p->y, 7);
    BP.chk.sel = 0x55, EXPECT(pop_read_check(p), 0x55);
    p->flags = 0, EXPECT(pop_on_key_click(p, 1, 2), 1), EXPECT(pop_on_key_up(p, 1), 1);
    p->flags = 0x4000, EXPECT(pop_on_key_click(p, 1, 2), 0), EXPECT(pop_on_key_up(p, 1), 0);
    // The key: the selected button's id, the CheckBox's mask for kind 1.
    set_list(&BP.dlg.list, 3), BP.dlg.sel = 2, p->def_button = 0, p->answer = 99, p->flags = 0;
    EXPECT(pop_on_key_down(p, 13), 1), EXPECT(p->answer, 12);
    p->def_button = 1, EXPECT(pop_on_key_down(p, 13), 1), EXPECT(p->answer, 0x55);
    p->def_button = -1, p->answer = 7, EXPECT(pop_on_key_down(p, 13), 1), EXPECT(p->answer, 0);
    p->def_button = 0, BP.dlg.list.count = 0, p->answer = 7, EXPECT(pop_on_key_down(p, 13), 1), EXPECT(p->answer, 0);
    p->flags = 0x4000, p->answer = 7, EXPECT(pop_on_key_down(p, 13), 0), EXPECT(p->answer, 7);
    // A double click.
    reset_bp(), set_list(&BP.dlg.list, 3), BP.dlg.sel = 1, p->def_button = 0, p->answer = 5, p->result = 8;
    focus = NULL, pop_double_click(), EXPECT(ncalls, 0), EXPECT(p->answer, 5);
    focus = (struct game_win *)p, pop_double_click();
    EXPECT(p->answer, 11), EXPECT(p->result, 0), expect_call(0, 0xe8, p, 0, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0, p->flags = 0x100 | 0x4000, p->result = 3, p->ext = ext_win, ext_win[0] = vt;
    pop_double_click(), expect_call(0, 8, p, 0, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0, p->flags = 0, pop_double_click(), expect_call(0, 0xe8, ext_win, 0, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0, p->def_button = 1, p->result = 9, pop_double_click(), EXPECT(ncalls, 0), EXPECT(p->result, 9);
    p->def_button = -1, p->answer = 6, pop_double_click(), EXPECT(p->answer, 6), EXPECT(p->result, 0);
    focus = NULL;
    // The left button: a hit on a kind 0 rectangle tells the popup.
    struct game_spot_rect rs[2] = {{{10, 10, 30, 30}, 0, 4}, {{20, 20, 60, 60}, 1, 5}};
    p->spots.rects = rs, p->spots.cap = 2, p->spots.count = 2, ncalls = 0;
    pop_on_left_down(p, 15, 15), expect_call(0, 0xb0, p, 4, 0, 0, 0), EXPECT(ncalls, 1);
    pop_on_left_down(p, 40, 40), pop_on_left_down(p, 90, 90), EXPECT(ncalls, 1);
    // The external window.
    reset_bp();
    pop_set_external_window(p, NULL, 1, 2, 3, 4), EXPECT(p->ext == NULL, 1), EXPECT(p->ext_a, 1), EXPECT(p->ext_d, 4), EXPECT(BP.dlg.w, 0);
    EXPECT(p->x, 0), EXPECT(BP.dlg.flags, 0);
    pop_set_external_window(p, ext_win, 7, 0x2000, 30, 40);
    EXPECT(p->ext == ext_win, 1), EXPECT(BP.dlg.w, 30), EXPECT(BP.dlg.h, 40), EXPECT(p->x, 7), EXPECT(p->y, 0), EXPECT(BP.dlg.flags, 3);
    pop_set_external_window(p, ext_win, 0x2000, 9, 31, 41), EXPECT(p->x, 7), EXPECT(p->y, 9), EXPECT(BP.dlg.h, 41);
    pop_set_def_external_window(1, 2, 3, 4, 5);
    EXPECT(ext_a[0], 1), EXPECT(ext_a[2], 3), EXPECT(ext_b[0], 4), EXPECT(ext_b[1], 5);
    // Fonts and colours.
    EXPECT(pop_set_string_font(p, NULL, 1, 2, 3), 3), EXPECT(p->string_a, 0);
    EXPECT(pop_set_string_font(p, &f_ok, 1, 2, 3), 0), EXPECT(p->string_font == &f_ok, 1), EXPECT(p->string_a, 1), EXPECT(p->string_b, 2), EXPECT(p->string_c, 3);
    pop_set_string_font(p, &f_bad, 4, 5, 6), EXPECT(p->string_font == &f_ok, 1), EXPECT(p->string_c, 6);
    EXPECT(pop_set_button_font(p, NULL, 1, 2), 3);
    EXPECT(pop_set_button_font(p, &f_ok, 1, 2), 0), EXPECT(p->button_font == &f_ok, 1), EXPECT(p->button_a, 1), EXPECT(p->button_b, 2);
    pop_set_string_color(p, 1, 2, 3, 4), pop_set_string_color2(p, 5, 6, 7, 8), pop_set_string_color3(p, 9, 10, 11, 12);
    pop_set_string_color_hyper(p, 13, 14, 15, 16);
    EXPECT(p->string_color[0][0], 1), EXPECT(p->string_color[3][0], 4), EXPECT(p->string_color[0][1], 5), EXPECT(p->string_color[2][2], 11);
    EXPECT(p->string_color[3][3], 16), EXPECT(p->string_color[1][3], 14);
    pop_set_button_color(p, 0x1ff, 2, 3, 4), pop_set_button_color2(p, 5, 6, 7, 8), pop_set_button_color3(p, 9, 10, 11, 12);
    EXPECT(p->button_flag[0], 0xff), EXPECT(p->button_flag[1], 5), EXPECT(p->button_flag[2], 9);
    EXPECT(p->button_color[0][0], 2), EXPECT(p->button_color[1][0], 3), EXPECT(p->button_color[2][0], 4), EXPECT(p->button_color[0][1], 6);
    EXPECT(p->button_color[2][2], 12);
    EXPECT(pop_set_def_string_font(NULL, 1, 2, 3), 3);
    EXPECT(pop_set_def_string_font(&f_ok, 1, 2, 3), 0), EXPECT(pfonts.string_font == &f_ok, 1), EXPECT(pfonts.string_a, 1), EXPECT(pfonts.string_c, 3);
    pop_set_def_string_font(&f_bad, 7, 8, 9), EXPECT(pfonts.string_font == &f_ok, 1), EXPECT(pfonts.string_b, 8);
    EXPECT(pop_set_def_button_font(NULL, 1, 2), 3);
    EXPECT(pop_set_def_button_font(&f_ok, 4, 5), 0), EXPECT(pfonts.button_font == &f_ok, 1), EXPECT(pfonts.button_a, 4), EXPECT(pfonts.button_b, 5);
    pop_set_def_string_color(1, 2, 3, 4), pop_set_def_string_color2(5, 6, 7, 8), pop_set_def_string_color3(9, 10, 11, 12);
    pop_set_def_string_color_hyper(13, 14, 15, 16);
    EXPECT(pdefs.string[0][0], 1), EXPECT(pdefs.string[3][0], 4), EXPECT(pdefs.string[2][1], 7), EXPECT(pdefs.string[1][3], 14), EXPECT(pdefs.string[3][3], 16);
    pop_set_def_button_color(1, 2, 3, 4), pop_set_def_button_color2(5, 6, 7, 8), pop_set_def_button_color3(9, 10, 11, 12);
    EXPECT(pdefs.button[0][0], 1), EXPECT(pdefs.button[3][0], 4), EXPECT(pdefs.button[1][1], 6), EXPECT(pdefs.button[3][2], 12);
    // The file name, the modal state and the flag bits.
    EXPECT(pop_set_def_file("popups"), 0), EXPECT(strcmp(pop_file, "popups"), 0);
    EXPECT(pop_set_def_file("123456789012"), 0), EXPECT(strcmp(pop_file, "123456789012"), 0);
    EXPECT(pop_set_def_file("1234567890123"), 3), EXPECT(strcmp(pop_file, "123456789012"), 0);
    EXPECT(pop_set_def_file(NULL), 3);
    EXPECT(pop_set_def_file(""), 0), EXPECT(pop_file[0], 0);
    pop_open = 0, EXPECT(pop_on_modal(), 1), pop_open = 5, EXPECT(pop_on_modal(), 0);
    pop_open = 0, pop_fallout_on = 0, pop_fallout(), EXPECT(pop_open, 0);
    pop_fallout_on = 2, pop_fallout(), EXPECT(pop_open, 1);
    p->f20 = 0xf0, pop_unk3(p, 1), EXPECT(p->f20, 0xf1), pop_unk4(p, 1), EXPECT(p->f20, 0xf3), pop_unk3(p, 0), EXPECT(p->f20, 0xf2);
    pop_unk4(p, 0), EXPECT(p->f20, 0xf0);

    // ---- SpriteBox ----
    static struct {
        struct game_sprite_box sb;
        uint8_t tail[0x40];
    } SB;
    memset(&SB, 0, sizeof SB);
    SB.sb.vtable = vt_sb, *(void ***)(SB.tail + 8) = vt;
    set_list(&SB.sb.list, 4);
    EXPECT(sprite_box_id_to_pos(&SB.sb, 12), 2), EXPECT(SB.sb.list.cur == &nodes[2], 1);
    EXPECT(sprite_box_id_to_pos(&SB.sb, 99), 4);
    EXPECT(sprite_box_pos_to_id(&SB.sb, 3), 13), EXPECT(SB.sb.list.idx, 3);
    EXPECT(sprite_box_pos_to_id(&SB.sb, 0), 10), EXPECT(SB.sb.list.idx, 0);
    EXPECT(sprite_box_pos_to_id(&SB.sb, -1), 13), EXPECT(SB.sb.list.idx, 3), EXPECT(sprite_box_pos_to_id(&SB.sb, -4), 10), EXPECT(SB.sb.list.idx, 0);
    EXPECT(sprite_box_pos_to_id(&SB.sb, -5), 10), EXPECT(SB.sb.list.idx, 0), EXPECT(SB.sb.list.cur == &nodes[0], 1);  // (further back than a turn)
    EXPECT(sprite_box_pos_to_id(&SB.sb, 4), 10), EXPECT(SB.sb.list.idx, 0);  // (past the end)
    SB.sb.list.head = NULL, SB.sb.list.count = 0;
    EXPECT(sprite_box_pos_to_id(&SB.sb, 0), 0), EXPECT(sprite_box_id_to_pos(&SB.sb, 1), 0);
    ncalls = 0;
    sprite_box_on_dialog_focus((uint8_t *)&SB.sb + 0x8c, 0), expect_call(0, 0xf8, SB.tail + 8, 0, 0, 0, 0);
    // The mouse: the sprite hit tells the function for the event; a non-zero answer posts Enter.
    struct game_spot_rect sr[1] = {{{10, 10, 30, 30}, 0, 6}};
    SB.sb.spot.rects = sr, SB.sb.spot.cap = 1, SB.sb.spot.count = 1;
    void *w = (uint8_t *)&SB.sb + 0x8c;
    void(THIS * handlers[7])(void *, int, int) = {sprite_box_on_left_down, sprite_box_on_left_up, sprite_box_on_right_click, sprite_box_on_right_down,
                                                 sprite_box_on_right_up, sprite_box_on_left_double_click, sprite_box_on_right_double_click};
    for (int i = 0; i < 7; i++) {
        memset(SB.sb.fn, 0, sizeof SB.sb.fn);
        SB.sb.fn[i + 1] = sprite_fn;
        fn_answer = 1, fn_id = -1, nposted = 0;
        handlers[i](w, 15, 15);
        char what[24];
        snprintf(what, sizeof what, "handler %d id", i), check_(what, fn_id, 6);
        EXPECT(nposted, 1), EXPECT(posted[0], 0x100 * 1000 + 0xd);
        fn_answer = 0, fn_id = -1, nposted = 0;
        handlers[i](w, 15, 15), EXPECT(fn_id, 6), EXPECT(nposted, 0);
        fn_id = -1, handlers[i](w, 50, 50), EXPECT(fn_id, -1);  // (no sprite there)
        SB.sb.fn[i + 1] = NULL, fn_answer = 1, handlers[i](w, 15, 15), EXPECT(nposted, 0);  // (no function)
        // (only this event's function is called)
        SB.sb.fn[(i + 1) % 7 + 1] = sprite_fn, fn_id = -1, handlers[i](w, 15, 15), EXPECT(fn_id, -1);
    }

    test_pop_layout();
    test_pop_draw();
    game_test_image = NULL;
    test_life();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
