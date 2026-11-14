// Unit test for the Time class, the string list and the string table's lookup (strings.c) with stand-ins for
// the Windows calls and the game's message pump: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "life_test.h"
#include "../src/engine/strlist.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))
#define STD __attribute__((stdcall))

// The Windows calls a timer makes, logged: kind, then what it was given.
static struct {
    int kind;
    uint32_t a, b, c, d, e;
} calls[16];
static int ncalls;
enum { K_KILL_MM = 1, K_KILL = 2, K_SET_MM = 3, K_SET = 4, K_FLUSH = 5 };
static void rec(int kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    if (ncalls < 16) calls[ncalls].kind = kind, calls[ncalls].a = a, calls[ncalls].b = b, calls[ncalls].c = c, calls[ncalls].d = d, calls[ncalls].e = e;
    ncalls++;
}
static uint32_t next_id = 77;
static uint32_t STD s_kill_event(uint32_t id) { return rec(K_KILL_MM, id, 0, 0, 0, 0), 0; }
static int STD s_kill_timer(uint32_t hwnd, uint32_t id) { return rec(K_KILL, hwnd, id, 0, 0, 0), 1; }
static uint32_t STD s_set_event(uint32_t delay, uint32_t res, void *proc, void *user, uint32_t flags) {
    return rec(K_SET_MM, delay, res, (uint32_t)(uintptr_t)proc, (uint32_t)(uintptr_t)user, flags), next_id;
}
static uint32_t STD s_set_timer(uint32_t hwnd, uint32_t id, uint32_t ms, void *proc) {
    return rec(K_SET, hwnd, id, ms, (uint32_t)(uintptr_t)proc, 0), next_id;
}
static void __attribute__((cdecl)) s_flush(void) { rec(K_FLUSH, 0, 0, 0, 0, 0); }

static char proc_mm, proc_tm;
static uint32_t hwnd = 0x1234, flush_flag;
static void cb_a(void *x) { (void)x; }
static void cb_b(void *x, int y) { (void)x, (void)y; }

static void expect_call(int i, int kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    char what[40];
    snprintf(what, sizeof what, "call %d", i);
    check_(what, i < ncalls ? calls[i].kind : -1, kind);
    if (i >= ncalls) return;
    check_("  a", calls[i].a, a), check_("  b", calls[i].b, b), check_("  c", calls[i].c, c), check_("  d", calls[i].d, d);
    check_("  e", calls[i].e, e);
}

// A list of nodes with items that hold text.
static struct game_strlist L;
static struct game_strnode nodes[6];
static uint8_t items[6][16];
static char text[6][8];
static void make_list(int n, const char *const *words) {
    memset(&L, 0, sizeof L), memset(nodes, 0, sizeof nodes);
    for (int i = 0; i < n; i++) {
        nodes[i].id = 10 + i, nodes[i].next = &nodes[(i + 1) % n], nodes[i].prev = &nodes[(i + n - 1) % n];
        strcpy(text[i], words ? words[i] : "x");
        char *tp = text[i];
        memcpy(items[i] + 4, &tp, sizeof tp);
        nodes[i].item = items[i];
    }
    L.head = n ? nodes : NULL, L.cur = n ? nodes : NULL, L.count = n;
}

// ---- Life cycle ----

static void check_str_(const char *what, const char *got, const char *want) {
    if (strcmp(got, want)) {
        printf("FAIL %s = \"%s\", expected \"%s\"\n", what, got, want);
        failures++;
    }
}
#define EXPECT_STR_(expr, want) check_str_(#expr, (expr), (want))
static int ll_slot1, ll_deleted, ll_data_closed, ll_add_calls, ll_alloc_fail;
static int ll_data_alloc_size, ll_data_alloc_result;
static char ll_arena[256];
static int ll_fail_after = -1;  // the heap gives nothing after this many blocks
static int ll_arena_used;
static void LT_THIS ll_slot1_fn(void *l, void *item) { ll_slot1++; }
static void LT_THIS ll_delete(void *o, int flag) { ll_deleted++; free(o); }
static void *ll_list_vtable[3] = {NULL, (void *)ll_slot1_fn};
static void *ll_node_vtable[2] = {(void *)ll_delete, NULL};
static void LT_THIS ll_data_close(void *d) { ll_data_closed++; }
static int LT_THIS ll_data_alloc(void *d, int size) { return ll_data_alloc_size = size, ll_data_alloc_result; }
static void *LT_THIS ll_string_alloc(void *arena, int size) {
    char *p = ll_arena + ll_arena_used;
    ll_arena_used += size;
    return p;
}
static int LT_THIS ll_add(void *list, int arg) { return ll_add_calls++, *(const char **)((uint8_t *)list + 0x1c) != NULL; }
static void *ll_mem_get(size_t n) {
    if (ll_alloc_fail || ll_fail_after == 0) return NULL;
    if (ll_fail_after > 0) ll_fail_after--;
    return calloc(1, n);
}
static int ll_slot0_calls;
static void *ll_slot0_item;
static void LT_THIS ll_slot0_fn(void *l, void *item) { (void)l, ll_slot0_calls++, ll_slot0_item = item; }
static void *ll_add_vtable[2] = {(void *)ll_slot0_fn, NULL};
static int ll_boxes, ll_exits;
static int STD ll_box(void *w, const char *text, const char *title, int type) { return (void)w, (void)text, (void)title, (void)type, ll_boxes++, 0; }
static void ll_exit(int code) { (void)code, ll_exits++; }

// StringStruct_add: nodes and items made for the list (from the heap, or an arena), linked in a ring.
static void test_add(void) {
    life_vbtable(0x6693b0, 0x1c, 0);  // the virtual base 0x1c in: its arena word is at 0x24
    IMGW(0x6693c8 + 4) = 0x14, IMGW(0x6693bc + 4) = 0x10;  // where the node's and the item's part vtables lie
    game_message_box_a = ll_box, game_exit = ll_exit;
    static uint8_t obj[0x40];
    struct game_strlist *l = (struct game_strlist *)obj;
    uint32_t *head = game_at(0x9b3374);
    memset(obj, 0, sizeof obj), ll_fail_after = -1, ll_alloc_fail = 0, ll_slot0_calls = 0;
    l->vtable = ll_add_vtable, l->vbptr = (void *)(uintptr_t)game_va(0x6693b0);
    *head = 0x1234;
    // The first: a node that links to itself, an item, the entry function told, the cursor on the node; idx is the old count.
    EXPECT(strlist_add(l, 7), 0);
    struct game_strnode *n0 = l->head;
    EXPECT(n0 != NULL, 1), EXPECT(l->cur == n0, 1), EXPECT(n0->id, 7), EXPECT(n0->next == n0, 1), EXPECT(n0->prev == n0, 1);
    EXPECT(l->count, 1), EXPECT(l->idx, 0), EXPECT(*head, 0), EXPECT(ll_slot0_calls, 1), EXPECT(ll_slot0_item == n0->item, 1);
    EXPECT(OW(n0, 0), game_va(0x6693c8)), EXPECT(OW(n0, 0x14), game_va(0x6693c4)), EXPECT(OW(n0, 0x18), 0);
    EXPECT(OW(n0->item, 0), game_va(0x6693bc)), EXPECT(OW(n0->item, 0x10), game_va(0x6693b8)), EXPECT(OW(n0->item, 0x14), 0);
    // The second goes in before the head, ahead of the cursor; the head stays.
    EXPECT(strlist_add(l, 8), 0);
    struct game_strnode *n1 = l->cur;
    EXPECT(n1 != n0, 1), EXPECT(l->head == n0, 1), EXPECT(n1->id, 8), EXPECT(n0->next == n1, 1), EXPECT(n0->prev == n1, 1);
    EXPECT(n1->next == n0, 1), EXPECT(n1->prev == n0, 1), EXPECT(l->count, 2), EXPECT(l->idx, 1), EXPECT(ll_slot0_calls, 2);
    EXPECT(ll_slot0_item == n1->item, 1);
    EXPECT(strlist_add(l, 9), 0);
    struct game_strnode *n2 = l->cur;
    EXPECT(n1->next == n2, 1), EXPECT(n2->prev == n1, 1), EXPECT(n2->next == n0, 1), EXPECT(n0->prev == n2, 1), EXPECT(l->idx, 2), EXPECT(l->count, 3);
    // From the list's own arena (word 0x18): objects hold it, the heap is left alone.
    ll_arena_used = 0;
    OW(obj, 0x18) = 0x5151;
    EXPECT(strlist_add(l, 10), 0);
    EXPECT(OW(l->cur, 0x18), 0x5151), EXPECT(OW(l->cur->item, 0x14), 0x5151), EXPECT(ll_arena_used, 0x1c + 0x18);
    EXPECT((char *)l->cur == ll_arena, 1), EXPECT(*head, 0);
    // Else from the virtual base's arena (the word 8 into the base).
    ll_arena_used = 0;
    OW(obj, 0x18) = 0, OW(obj, 0x24) = 0x6262;
    EXPECT(strlist_add(l, 11), 0);
    EXPECT(OW(l->cur, 0x18), 0x6262), EXPECT(OW(l->cur->item, 0x14), 0x6262), EXPECT(ll_arena_used, 0x1c + 0x18);
    // The heap gives out nothing for the node: an empty list's head is left empty; otherwise the last node's next is left empty, and 4 is answered.
    OW(obj, 0x24) = 0;
    int boxes = ll_boxes;
    ll_alloc_fail = 1;
    memset(&obj[8], 0, 16);
    EXPECT(strlist_add(l, 1), 4), EXPECT(l->head == NULL, 1), EXPECT(l->count, 0);
    l->head = n0, l->cur = n0, l->count = 3, l->idx = 1;
    EXPECT(strlist_add(l, 1), 4), EXPECT(n0->prev->next == NULL, 1), EXPECT(l->count, 3), EXPECT(l->idx, 1), EXPECT(l->cur == n0, 1);
    n0->prev->next = n0;
    // ... or for the item (the node is in): 4, the count not changed, the entry function not called.
    ll_alloc_fail = 0, ll_fail_after = 1, ll_slot0_calls = 0;
    EXPECT(strlist_add(l, 12), 4), EXPECT(l->cur->id, 12), EXPECT(l->cur->item == NULL, 1), EXPECT(l->count, 3), EXPECT(ll_slot0_calls, 0);
    ll_fail_after = -1;
    memset(&obj[8], 0, 16);
    ll_fail_after = 1;
    EXPECT(strlist_add(l, 13), 4), EXPECT(l->head->item == NULL, 1), EXPECT(l->count, 0), EXPECT(ll_slot0_calls, 0);
    ll_fail_after = -1;
    // An arena with no room is the end of the game (no way back here; the message box and exit are stand-ins).
    EXPECT(ll_boxes, boxes), EXPECT(ll_exits, 0);
}

// The timers' procedures: the object's message is posted once, unless the game's word lets only another object post.
static int posted;
static struct {
    void *w;
    unsigned msg;
    uintptr_t wp;
    intptr_t lp;
} post[4];
static int STD ll_post(void *w, unsigned msg, uintptr_t wp, intptr_t lp) {
    if (posted < 4) post[posted].w = w, post[posted].msg = msg, post[posted].wp = wp, post[posted].lp = lp;
    return posted++, 1;
}
static void test_procs(void) {
    static struct game_time t, other;
    game_post_message = ll_post;
    uint32_t *only = game_at(0x9b90d0);
    *only = 0, posted = 0, memset(&t, 0, sizeof t);
    time_timer_proc((void *)7, 0x113, &t, 1000);
    EXPECT(posted, 1), EXPECT(post[0].w == (void *)(uintptr_t)hwnd, 1), EXPECT(post[0].msg, 0x401), EXPECT(post[0].wp, (uintptr_t)&t), EXPECT(post[0].lp, 0);
    EXPECT(t.fired, 1);
    time_timer_proc((void *)7, 0x113, &t, 1000), time_mm_proc(1, 2, &t, 3, 4);  // (already on its way)
    EXPECT(posted, 1);
    t.fired = 0;
    time_mm_proc(1, 2, &t, 3, 4);
    EXPECT(posted, 2), EXPECT(t.fired, 1), EXPECT(post[1].wp, (uintptr_t)&t);
    time_mm_proc(1, 2, NULL, 3, 4), time_timer_proc(NULL, 0, NULL, 0);
    EXPECT(posted, 2);
    // Only the object the word names posts.
    t.fired = 0, other.fired = 0, *only = (uint32_t)(uintptr_t)&other;
    time_timer_proc(NULL, 0, &t, 0);
    EXPECT(posted, 2), EXPECT(t.fired, 0);
    time_mm_proc(0, 0, &other, 0, 0);
    EXPECT(posted, 3), EXPECT(other.fired, 1), EXPECT(post[2].wp, (uintptr_t)&other);
    *only = 0;
}


static void test_life(void) {
    life_image_init();
    memcpy(game_at(0x6971fc), "dflt", 5);
    IMGW(0x6693a8) = (uint32_t)(uintptr_t)ll_slot1_fn;  // slot 1 of the list base class
    life_vbtable(0x6693b0, 0x18, 0);  // the table of the list's virtual base
    game_free = life_free, game_mem_get = ll_mem_get, game_dialog_data_close = ll_data_close, game_dialog_data_alloc = ll_data_alloc;
    game_string_alloc = ll_string_alloc, game_strlist_add = ll_add;
    uint32_t *head = game_at(0x9b3374);
    static uint8_t obj[0x40];

    // A list made whole: its virtual base's table, vtables (the base's at 0x1c), the global it saved, and empty.
    memset(obj, 0xcc, sizeof obj);
    *head = 0x2222;
    EXPECT(strlist_ctor((struct game_strlist *)obj, 1) == (struct game_strlist *)obj, 1);
    EXPECT(OW(obj, 0), game_va(0x6693a4)), EXPECT(OW(obj, 4), game_va(0x6693b0)), EXPECT(OW(obj, 0x1c), game_va(0x6693a0));
    EXPECT(OW(obj, 0x20), 0x2222), EXPECT(*head, 0), EXPECT(OW(obj, 8), 0), EXPECT(OW(obj, 0xc), 0), EXPECT(OW(obj, 0x10), 0), EXPECT(OW(obj, 0x14), 0);
    EXPECT(OW(obj, 0x18), 0), EXPECT(OW(obj, 0x24), 0xcccccccc);
    // A part of a bigger object keeps its table and leaves the global alone.
    *head = 0x3333;
    OW(obj, 0x20) = 0x44, OW(obj, 8) = 5;
    EXPECT(strlist_ctor((struct game_strlist *)obj, 0) == (struct game_strlist *)obj, 1);
    EXPECT(*head, 0x3333), EXPECT(OW(obj, 0x20), 0x44), EXPECT(OW(obj, 8), 0), EXPECT(OW(obj, 4), game_va(0x6693b0));

    // Emptying: the list's slot 1 is told of each item, then each item and node is deleted through its own vtable.
    for (int which = 0; which < 3; which++) {
        strlist_ctor((struct game_strlist *)obj, 1);
        struct game_strnode *n[3];
        for (int i = 0; i < 3; i++) {
            n[i] = calloc(1, 32);
            n[i]->pad0 = (int32_t)(uintptr_t)ll_node_vtable;
            n[i]->item = calloc(1, 16);
            *(void **)n[i]->item = ll_node_vtable;
        }
        for (int i = 0; i < 3; i++) n[i]->next = n[(i + 1) % 3], n[i]->prev = n[(i + 2) % 3];
        struct game_strlist *l = (struct game_strlist *)obj;
        l->vtable = ll_list_vtable;
        l->head = n[0], l->count = 3, l->idx = 2;
        ll_slot1 = 0, ll_deleted = 0;
        *head = 0x5555;
        if (which == 0) strlist_remove_all(l);
        if (which == 1) strlist_close(obj + 0x1c);
        if (which == 2) EXPECT(strlist_scalar_dtor(obj + 0x1c, 0) == obj, 1);
        EXPECT(ll_slot1, 3), EXPECT(ll_deleted, 6), EXPECT(l->head == NULL, 1), EXPECT(l->count, 0), EXPECT(l->idx, 0);
        if (which == 1) EXPECT(l->vtable == (void **)(uintptr_t)game_va(0x6693a4), 1);
        if (which == 2) EXPECT(*head, OW(obj, 0x20)), EXPECT(OW(obj, 0x1c), game_va(0x6693ac));
    }
    // Remove_all leaves an empty list alone (its index too), and close clears the index.
    strlist_ctor((struct game_strlist *)obj, 1);
    ((struct game_strlist *)obj)->idx = 7;
    strlist_remove_all((struct game_strlist *)obj);
    EXPECT(((struct game_strlist *)obj)->idx, 7);
    strlist_close(obj + 0x1c);
    EXPECT(((struct game_strlist *)obj)->idx, 0);
    // The deleting destructor frees the object when asked and it saved no global; it always clears the global then.
    for (int saved = 0; saved < 2; saved++) {
        uint8_t *heap = calloc(1, 0x40);
        *head = saved ? 0x66 : 0;
        strlist_ctor((struct game_strlist *)heap, 1);
        life_freed = 0;
        *head = 0x77;
        EXPECT(strlist_scalar_dtor(heap + 0x1c, 1) == heap, 1);
        EXPECT(life_freed, saved ? 0 : 1), EXPECT(*head, 0);
        if (saved) free(heap);
    }

    // StringList: a text for the entry (from the heap or an arena) and the two numbers; a kill gives back a text
    // the heap made; a special text drops its '^' marks.
    uint8_t list[0x30], item[16];
    for (int arena = 0; arena < 2; arena++) {
        memset(list, 0, sizeof list), memset(item, 0xcc, sizeof item);
        OW(list, 0x18) = arena ? (uint32_t)(uintptr_t)ll_arena : 0, OW(list, 0x1c) = (uint32_t)(uintptr_t)"entry", OW(list, 0x20) = 3, OW(list, 0x24) = 4;
        ll_arena_used = 0;
        EXPECT(stringlist_fill_entry((struct game_strlist *)list, item), 0);
        EXPECT_STR_((char *)OW(item, 4), "entry"), EXPECT(OW(item, 8), 3), EXPECT(OW(item, 0xc), 4), EXPECT(OW(item, 0), 0xcccccccc);
        EXPECT(arena ? (char *)OW(item, 4) == ll_arena : (char *)OW(item, 4) != ll_arena, 1);
        life_freed = 0;
        EXPECT(stringlist_kill_entry((struct game_strlist *)list, item) != 0, arena ? 1 : 0);  // (an arena keeps the text: answered)
        EXPECT(life_freed, arena ? 0 : 1), EXPECT(OW(item, 4) != 0, arena ? 1 : 0);
        if (!arena) EXPECT(OW(item, 4), 0);
    }
    memset(item, 0xcc, sizeof item), OW(list, 0x18) = 0, OW(list, 0x1c) = 0;
    EXPECT(stringlist_fill_entry((struct game_strlist *)list, item), 0), EXPECT(OW(item, 4), 0xcccccccc), EXPECT(OW(item, 8), 3);
    OW(list, 0x1c) = (uint32_t)(uintptr_t)"x", ll_alloc_fail = 1;
    EXPECT(stringlist_fill_entry((struct game_strlist *)list, item), 4), EXPECT(OW(item, 4), 0);
    ll_alloc_fail = 0;
    OW(item, 4) = 0;
    EXPECT(stringlist_kill_entry((struct game_strlist *)list, item), 0);  // (no text: nothing to give back)
    {
        static const char *in[] = {"abc", "^abc", "^^abc", "^^^abc", "^", "^^", "^^^^"};
        static const int kind[] = {0, 1, 2, 2, 3, 2, 2}, skip[] = {0, 1, 2, 3, 1, 2, 3};
        for (int i = 0; i < 7; i++) {
            memset(list, 0, sizeof list);
            ll_add_calls = 0;
            EXPECT(stringlist_add_special((struct game_strlist *)list, in[i]), 1);
            EXPECT(OW(list, 0x1c), (uint32_t)(uintptr_t)(in[i] + skip[i])), EXPECT(OW(list, 0x20), kind[i]), EXPECT(OW(list, 0x24), 0), EXPECT(ll_add_calls, 1);
        }
        // (an empty text, after the marks, makes the stand-in add answer 1 only for a non-null pointer: always 1 here)
    }

    // Strings: the arena's five words and a flag; init closes what it held, asks for the room and puts the game's
    // default text in; shutdown closes; put copies a text into the arena.
    uint8_t tab[0x18];
    memset(tab, 0xcc, sizeof tab);
    EXPECT(strings_ctor((struct game_strings *)tab) == (struct game_strings *)tab, 1);
    EXPECT(tab[0], 0), EXPECT(tab[1], 0xcc), EXPECT(OW(tab, 4), 0), EXPECT(OW(tab, 8), 0), EXPECT(OW(tab, 0xc), 0), EXPECT(OW(tab, 0x10), 0), EXPECT(OW(tab, 0x14), 0);
    ll_data_closed = 0, ll_arena_used = 0, ll_data_alloc_result = 0;
    EXPECT(strings_init((struct game_strings *)tab, 100), 0);
    EXPECT(ll_data_closed, 0), EXPECT(ll_data_alloc_size, 100), EXPECT(OW(tab, 0x14), 1), EXPECT_STR_(ll_arena, "dflt"), EXPECT(ll_arena_used, 5);
    EXPECT(strings_init((struct game_strings *)tab, 50), 0), EXPECT(ll_data_closed, 1), EXPECT(ll_data_alloc_size, 50);
    ll_data_alloc_result = 4, ll_arena_used = 0;
    EXPECT(strings_init((struct game_strings *)tab, 60), 1), EXPECT(ll_data_closed, 2), EXPECT(OW(tab, 0x14), 0), EXPECT(ll_arena_used, 0);
    ll_data_alloc_result = 0;
    ll_arena_used = 8;
    char *put = strings_put((struct game_strings *)tab, "hello");
    EXPECT(put == ll_arena + 8, 1), EXPECT_STR_(put, "hello"), EXPECT(ll_arena_used, 14);
    OW(tab, 0x14) = 1;
    EXPECT(strings_shutdown((struct game_strings *)tab), 0), EXPECT(ll_data_closed, 3), EXPECT(OW(tab, 0x14), 0);
}

int main(void) {
    game_time_kill_event = s_kill_event, game_kill_timer = s_kill_timer, game_time_set_event = s_set_event;
    game_set_timer = s_set_timer, game_time_flush = s_flush, game_time_flush_flag = &flush_flag;
    game_time_mm_proc = &proc_mm, game_time_timer_proc = &proc_tm, game_win_hwnd = &hwnd;
    struct game_time T;

    // The constructor clears the object (a resolution of 5), and answers it.
    memset(&T, 0xff, sizeof T);
    EXPECT(time_ctor(&T) == &T, 1);
    EXPECT(T.flags, 0), EXPECT(T.id, 0), EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.delay, 0), EXPECT(T.resolution, 5);
    EXPECT(T.fired, 0), EXPECT(T.arg == NULL, 1), EXPECT(T.arg2, 0), EXPECT(T.f24, 0);
    EXPECT(ncalls, 0);

    // Start with a short delay: a multimedia timer, repeating, with the game's procedure and the object; the
    // old timer is stopped (by its kind) and the pump runs first, since the object isn't a pulse.
    T.id = 5, T.delay = 20, T.flags = 2;
    EXPECT(time_start(&T, cb_a, (void *)0x44, 30, 4), 0);
    expect_call(0, K_KILL_MM, 5, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0);
    expect_call(2, K_SET_MM, 30, 4, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1), EXPECT(ncalls, 3);
    EXPECT(T.id, next_id), EXPECT(T.cb == cb_a, 1), EXPECT(T.arg == (void *)0x44, 1), EXPECT(T.delay, 30), EXPECT(T.resolution, 4);
    EXPECT(T.cb2 == NULL, 1), EXPECT(T.flags, 0), EXPECT(T.fired, 0);
    // A long delay: SetTimer, with the window, the object as the id and the game's timer procedure; the old
    // SetTimer timer is stopped with KillTimer. A pulse object (flag 1) doesn't run the pump.
    ncalls = 0, T.flags = 1, T.id = 6;
    EXPECT(time_start(&T, cb_a, NULL, 0x32, 5), 0);
    expect_call(0, K_KILL_MM, 6, 0, 0, 0, 0);  // (the old delay was 30: a multimedia timer)
    ncalls = 0;
    T.id = 6, T.flags = 1;
    EXPECT(time_start(&T, cb_a, NULL, 0x80, 5), 0);
    expect_call(0, K_KILL, 0x1234, 6, 0, 0, 0);
    expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x80, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(ncalls, 2);
    // No function: nothing is started (7), the object still takes the values.
    ncalls = 0, T.id = 0;
    EXPECT(time_start(&T, NULL, (void *)3, 90, 2), 7);
    EXPECT(T.delay, 90), EXPECT(T.arg == (void *)3, 1), EXPECT(ncalls, 1), expect_call(0, K_FLUSH, 0, 0, 0, 0, 0);
    // No timer could be made: 2.
    ncalls = 0, next_id = 0;
    EXPECT(time_start(&T, cb_a, NULL, 10, 2), 2), EXPECT(T.id, 0);
    next_id = 77;
    // Pulse is the same as start.
    ncalls = 0, T.id = 0, T.flags = 0;
    EXPECT(time_pulse(&T, cb_a, NULL, 10, 3), 0);
    expect_call(1, K_SET_MM, 10, 3, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1);

    // The second kind: a function that takes two arguments (the delay is the fourth).
    ncalls = 0, T.id = 0, T.flags = 0;
    EXPECT(time_start_2(&T, cb_b, (void *)8, 9, 60, 6), 0);
    EXPECT(T.cb2 == cb_b, 1), EXPECT(T.cb == NULL, 1), EXPECT(T.arg2, 9), EXPECT(T.arg == (void *)8, 1), EXPECT(T.delay, 60), EXPECT(T.resolution, 6);
    expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 60, (uint32_t)(uintptr_t)&proc_tm, 0);
    ncalls = 0, T.id = 0;
    EXPECT(time_start_2(&T, NULL, NULL, 0, 40, 6), 7);
    // pulse_2: a multimedia timer that doesn't repeat; a SetTimer one marks the object a pulse.
    ncalls = 0, T.id = 0;
    EXPECT(time_pulse_2(&T, cb_b, NULL, 1, 10, 6), 0);
    expect_call(1, K_SET_MM, 10, 6, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 0), EXPECT(T.flags, 0);
    ncalls = 0, T.id = 0;
    EXPECT(time_pulse_2(&T, cb_b, NULL, 1, 300, 6), 0);
    EXPECT(T.flags, 1), expect_call(1, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 300, (uint32_t)(uintptr_t)&proc_tm, 0);

    // Start again with what the object holds (no kill): needs a function; repeating, and it clears flag 1.
    ncalls = 0, T.id = 0, T.flags = 3, T.cb = NULL, T.cb2 = NULL, T.delay = 15, T.resolution = 7;
    EXPECT(time_start_3(&T), 7), EXPECT(ncalls, 0);
    T.cb = cb_a;
    EXPECT(time_start_3(&T), 0), EXPECT(T.flags, 2), expect_call(0, K_SET_MM, 15, 7, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 1);
    ncalls = 0, T.delay = 0x40, T.flags = 3;
    EXPECT(time_start_3(&T), 0), expect_call(0, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x40, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(T.flags, 2);
    // pulse_3: once; a SetTimer one marks the object a pulse and clears flag 2.
    ncalls = 0, T.delay = 15, T.flags = 2;
    EXPECT(time_pulse_3(&T), 0), expect_call(0, K_SET_MM, 15, 7, (uint32_t)(uintptr_t)&proc_mm, (uint32_t)(uintptr_t)&T, 0), EXPECT(T.flags, 2);
    ncalls = 0, T.delay = 0x40, T.flags = 2;
    EXPECT(time_pulse_3(&T), 0), expect_call(0, K_SET, 0x1234, (uint32_t)(uintptr_t)&T, 0x40, (uint32_t)(uintptr_t)&proc_tm, 0), EXPECT(T.flags, 1);
    T.cb = NULL, T.cb2 = NULL, EXPECT(time_pulse_3(&T), 7);

    // Stop: the timer by its kind, then the pump (unless a pulse); the object keeps the rest.
    ncalls = 0, T.id = 9, T.delay = 5, T.flags = 0, T.cb = cb_a;
    time_stop(&T);
    expect_call(0, K_KILL_MM, 9, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0), EXPECT(ncalls, 2), EXPECT(T.id, 0), EXPECT(T.cb == cb_a, 1);
    ncalls = 0, T.id = 9, T.delay = 0x99, T.flags = 1;
    time_stop(&T);
    expect_call(0, K_KILL, 0x1234, 9, 0, 0, 0), EXPECT(ncalls, 1);
    ncalls = 0;
    time_stop(&T);
    EXPECT(ncalls, 0);

    // Init: the functions and the delay, nothing started; it answers the delay (init_2 the argument).
    ncalls = 0, T.id = 0, T.flags = 0, T.fired = 1, T.f24 = 3;
    EXPECT(time_init(&T, cb_a, (void *)5, 0x1f4, 5), 0x1f4);
    EXPECT(T.cb == cb_a, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.arg == (void *)5, 1), EXPECT(T.delay, 0x1f4), EXPECT(T.resolution, 5);
    EXPECT(T.fired, 0), EXPECT(T.f24, 0), EXPECT(T.arg2, 0), expect_call(0, K_FLUSH, 0, 0, 0, 0, 0), EXPECT(ncalls, 1);
    EXPECT(time_init_2(&T, cb_b, (void *)6, 7, 11, 2), 6);
    EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == cb_b, 1), EXPECT(T.arg == (void *)6, 1), EXPECT(T.arg2, 7), EXPECT(T.delay, 11), EXPECT(T.resolution, 2);

    // The destructor and close stop the timer and clear the object.
    ncalls = 0, T.id = 4, T.delay = 3, T.flags = 2;
    time_dtor(&T);
    expect_call(0, K_KILL_MM, 4, 0, 0, 0, 0), expect_call(1, K_FLUSH, 0, 0, 0, 0, 0);
    EXPECT(T.id, 0), EXPECT(T.cb == NULL, 1), EXPECT(T.cb2 == NULL, 1), EXPECT(T.arg == NULL, 1), EXPECT(T.delay, 0), EXPECT(T.resolution, 5), EXPECT(T.flags, 0);
    ncalls = 0, T.id = 4, T.delay = 3, T.flags = 1, T.arg2 = 5, T.fired = 1;
    time_close(&T);
    EXPECT(ncalls, 1), EXPECT(T.id, 0), EXPECT(T.arg2, 0), EXPECT(T.fired, 0);

    // The string table's lookup: the base at or below it, the pointer itself up to the end, nothing beyond.
    struct game_strings tab = {0};
    tab.base = 1000, tab.size = 50;
    EXPECT(strings_get(&tab, 900), 1000), EXPECT(strings_get(&tab, 1000), 1000), EXPECT(strings_get(&tab, 1001), 1001);
    EXPECT(strings_get(&tab, 1050), 1050), EXPECT(strings_get(&tab, 1051), 0), EXPECT(strings_get(&tab, 0xfffffff0u), 0);

    // The string list.
    static const char *const w[] = {"pear", "apple", "fig", "apple", "banana"};
    make_list(5, w);
    EXPECT(strlist_seek_id(&L, 12), 1), EXPECT(L.idx, 2), EXPECT(L.cur == &nodes[2], 1);
    EXPECT(strlist_seek_id(&L, 99), 0), EXPECT(L.idx, 5), EXPECT(L.cur == &nodes[0], 1);
    EXPECT(strlist_current_id(&L), 10);
    EXPECT(strlist_seek_pos(&L, 3), 1), EXPECT(L.idx, 3), EXPECT(L.cur == &nodes[3], 1), EXPECT(strlist_current_id(&L), 13);
    EXPECT(strlist_seek_pos(&L, 5), 0), EXPECT(L.idx, 3), EXPECT(L.cur == &nodes[3], 1);
    EXPECT(strlist_seek_pos(&L, -1), 1), EXPECT(L.idx, 4), EXPECT(L.cur == &nodes[4], 1);
    EXPECT(strlist_seek_pos(&L, -5), 1), EXPECT(L.idx, 0), EXPECT(L.cur == &nodes[0], 1);
    L.idx = 2, L.cur = &nodes[2];
    EXPECT(strlist_seek_pos(&L, -6), 0), EXPECT(L.cur == &nodes[0], 1), EXPECT(L.idx, 2);  // (the cursor went to the head)
    EXPECT(strlist_seek_pos(&L, 0), 1), EXPECT(L.idx, 0);
    EXPECT(strlist_current_entry(&L) == items[0], 1);
    EXPECT(strlist_next_entry(&L) == items[1], 1), EXPECT(L.idx, 1);
    L.idx = 3, L.cur = &nodes[3];
    EXPECT(strlist_next_entry(&L) == items[4], 1), EXPECT(L.idx, 4);
    EXPECT(strlist_next_entry(&L) == items[0], 1), EXPECT(L.idx, 0);  // (round the ring)
    make_list(0, NULL);
    EXPECT(strlist_seek_id(&L, 1), 0), EXPECT(strlist_current_id(&L), 0), EXPECT(strlist_next_entry(&L) == NULL, 1);
    EXPECT(strlist_current_entry(&L) == NULL, 1), EXPECT(strlist_seek_pos(&L, 0), 0), EXPECT(strlist_seek_pos(&L, -1), 0);

    // The sort: the ids and items change places so the texts come out in order (equal ones keep their order:
    // the first of the lowest goes first); the cursor is left where the work put it.
    make_list(5, w);
    strlist_sort(&L, 0);
    {
        const char *got[5];
        int ids[5];
        for (int i = 0; i < 5; i++) {
            memcpy(&got[i], (uint8_t *)nodes[i].item + 4, sizeof(char *));
            ids[i] = nodes[i].id;
        }
        EXPECT(strcmp(got[0], "apple"), 0), EXPECT(strcmp(got[1], "apple"), 0), EXPECT(strcmp(got[2], "banana"), 0);
        EXPECT(strcmp(got[3], "fig"), 0), EXPECT(strcmp(got[4], "pear"), 0);
        EXPECT(ids[0], 11), EXPECT(ids[1], 13), EXPECT(ids[2], 14), EXPECT(ids[3], 12), EXPECT(ids[4], 10);
    }
    EXPECT(L.count, 5);
    make_list(1, w), strlist_sort(&L, 0), EXPECT(nodes[0].id, 10), EXPECT(L.cur == &nodes[0], 1);
    make_list(0, NULL), strlist_sort(&L, 0);

    test_life();
    test_add();
    test_procs();

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
