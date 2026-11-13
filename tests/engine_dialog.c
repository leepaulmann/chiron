// Unit test for the Dialog, SpriteBox and BasePop classes and the text functions in dialog.c, on hand-built
// objects with vtables and callbacks that log their calls: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/dialog.h"
#include "../src/engine/game.h"

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

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
