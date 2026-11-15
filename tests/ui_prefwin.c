// Unit test for the Preferences window's pure parts (src/ui/prefwin.c): where each option lives, the orders the boxes are asked
// and told in, the arrow keys, the volume buttons' labels, the window's place and the boxes' heights. No game files needed. The
// numbers are worked out by hand from the original's code (set_preferences at 0x490350, get_preferences at 0x490b00, on_key_click
// at 0x48fcd0, init at 0x48d840).
#include <stdio.h>
#include <string.h>

#include "../src/ui/prefwin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld (0x%lx), expected %ld (0x%lx)\n", line, what, got, got, want, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

// An option's place as (field, mask, inverted) packed for a comparison.
static long place(int id) {
    struct prefwin_place p;
    if (!prefwin_place(id, &p)) return -1;
    return (long)p.field * 0x100000000L + (long)p.mask * 4 + p.inverted;
}
#define PLACE(field, mask, inverted) ((long)(field) * 0x100000000L + (long)(unsigned)(mask) * 4 + (inverted))

int main(void) {
    // Word kinds: A at the fields of 0xa30, 0xa38, 0xa40, 0xa4c and 0xa54, C at 0xa48 and B at the rest.
    for (int f = 0; f < PREFWIN_WORDS; f++) {
        int want = (f == 0 || f == 2 || f == 4 || f == 7 || f == 9) ? PREF_WORD_A : f == 6 ? PREF_WORD_C : PREF_WORD_B;
        EXPECT(prefwin_word_kind(f), want);
    }

    // Places: a few of each box, the inverted ones, and the ones that lie in another field than their neighbours.
    EXPECT(place(0x07), PLACE(0, 0x20, 0));
    EXPECT(place(0x09), PLACE(1, 0x100000, 0));  // word B among the basic items
    EXPECT(place(0x0d), PLACE(0, 0x4, 1));
    EXPECT(place(0x0e), PLACE(0, 0x8000, 1));
    EXPECT(place(0x0f), PLACE(2, 0x20000000, 1));
    EXPECT(place(0x13), PLACE(2, 0x8, 0));
    EXPECT(place(0x15), PLACE(2, 0x80000, 0));
    EXPECT(place(0x1c), PLACE(3, 0x40, 0));
    EXPECT(place(0x28), PLACE(4, 0x80000000u, 0));
    EXPECT(place(0x29), PLACE(5, 0x800, 0));
    EXPECT(place(0x2a), PLACE(5, 0x400000, 1));
    EXPECT(place(0x35), PLACE(6, 0x100, 0));  // (not an item of any box, but asked for)
    EXPECT(place(0x3d), PLACE(7, 0x800, 0));
    EXPECT(place(0x3f), PLACE(8, 0x4000, 0));
    EXPECT(place(0x42), PLACE(7, 0x40000, 1));
    EXPECT(place(0x47), PLACE(8, 0x1000000, 1));
    EXPECT(place(0x16), PLACE(8, 0x200, 0));
    EXPECT(place(0x48), PLACE(10, 0x1, 0));
    EXPECT(place(0x49), PLACE(9, 0x100, 0));
    EXPECT(place(0x4f), PLACE(10, 0x2000000, 1));
    EXPECT(place(0x00), -1);
    EXPECT(place(0x06), -1);
    EXPECT(place(0x50), -1);

    // Every id of every box has a place, and no two options share one.
    for (int box = 0; box < PREFWIN_BOXES; box++)
        for (int i = 0, id; (id = prefwin_set_item(box, i)); i++) EXPECT(place(id) != -1, 1);
    for (int a = 1; a < 0x50; a++)
        for (int b = a + 1; b < 0x50; b++) {
            struct prefwin_place pa, pb;
            if (prefwin_place(a, &pa) && prefwin_place(b, &pb) && pa.field == pb.field && pa.mask == pb.mask)
                check_(__LINE__, "two options share a bit", a * 0x100 + b, 0);
        }

    // The orders. The sizes of the lists: set asks 8, 12, 14, 11 (the first three of which the audio box has no item for), 14
    // (one the box has no item for), 4 and 8; get tells 8, 12, 14, 11, 13, 4 and 8; the window makes 8, 12, 14, 8, 13, 4, 8.
    static const int set_n[] = {8, 12, 14, 11, 14, 4, 8}, get_n[] = {8, 12, 14, 11, 13, 4, 8}, make_n[] = {8, 12, 14, 8, 13, 4, 8};
    for (int box = 0; box < PREFWIN_BOXES; box++) {
        int s = 0, g = 0, m = 0;
        while (prefwin_set_item(box, s)) s++;
        while (prefwin_get_item(box, g)) g++;
        while (prefwin_make_item(box, m)) m++;
        EXPECT(s, set_n[box]);
        EXPECT(g, get_n[box]);
        EXPECT(m, make_n[box]);
    }
    // Basic: asked 7, 8, 9, 0xa, ...; told 7, 8, 0xa, ... and 9 last.
    EXPECT(prefwin_set_item(BOX_BASIC, 2), 0x09);
    EXPECT(prefwin_get_item(BOX_BASIC, 2), 0x0a);
    EXPECT(prefwin_get_item(BOX_BASIC, 7), 0x09);
    // Advanced: 0x17 and 0x18 change places between asking and telling.
    EXPECT(prefwin_set_item(BOX_ADVANCED, 8), 0x17);
    EXPECT(prefwin_set_item(BOX_ADVANCED, 9), 0x18);
    EXPECT(prefwin_get_item(BOX_ADVANCED, 8), 0x18);
    EXPECT(prefwin_get_item(BOX_ADVANCED, 9), 0x17);
    // Automation: 0x2a is fifth when asked, last when told, seventh when made.
    EXPECT(prefwin_set_item(BOX_AUTOMATION, 4), 0x2a);
    EXPECT(prefwin_get_item(BOX_AUTOMATION, 13), 0x2a);
    EXPECT(prefwin_make_item(BOX_AUTOMATION, 6), 0x2a);
    // Audio: asked 0x3d, 0x3e, 0x3f (no items), the others, with 0x47 seventh and 0x16 last; told with 0x16 tenth and 0x47 last.
    EXPECT(prefwin_set_item(BOX_AUDIO, 0), 0x3d);
    EXPECT(prefwin_set_item(BOX_AUDIO, 6), 0x47);
    EXPECT(prefwin_set_item(BOX_AUDIO, 10), 0x16);
    EXPECT(prefwin_get_item(BOX_AUDIO, 9), 0x16);
    EXPECT(prefwin_get_item(BOX_AUDIO, 10), 0x47);
    EXPECT(prefwin_make_item(BOX_AUDIO, 0), 0x40);
    EXPECT(prefwin_make_item(BOX_AUDIO, 7), 0x16);
    // Base warnings: asked all of 0x2b to 0x38 (0x35 included), told and made without 0x35.
    EXPECT(prefwin_set_item(BOX_WARNINGS_A, 10), 0x35);
    EXPECT(prefwin_get_item(BOX_WARNINGS_A, 10), 0x36);
    EXPECT(prefwin_get_item(BOX_WARNINGS_A, 12), 0x38);
    EXPECT(prefwin_make_item(BOX_WARNINGS_A, 9), 0x34);
    EXPECT(prefwin_make_item(BOX_WARNINGS_A, 10), 0x36);
    EXPECT(prefwin_set_item(BOX_WARNINGS_B, 0), 0x39);
    EXPECT(prefwin_get_item(BOX_WARNINGS_B, 3), 0x3c);
    // Map: made 0x48, 0x49, 0x4e, 0x4a, ...; asked and told in order.
    EXPECT(prefwin_make_item(BOX_MAP, 2), 0x4e);
    EXPECT(prefwin_set_item(BOX_MAP, 2), 0x4a);
    EXPECT(prefwin_get_item(BOX_MAP, 7), 0x4f);
    EXPECT(prefwin_set_item(7, 0), 0);
    EXPECT(prefwin_set_item(BOX_BASIC, -1), 0);

    // The pages and their boxes.
    EXPECT(prefwin_box_of_page(0), BOX_BASIC);
    EXPECT(prefwin_box_of_page(1), BOX_ADVANCED);
    EXPECT(prefwin_box_of_page(2), BOX_WARNINGS_A);
    EXPECT(prefwin_box_of_page(3), BOX_AUTOMATION);
    EXPECT(prefwin_box_of_page(4), BOX_AUDIO);
    EXPECT(prefwin_box_of_page(5), BOX_MAP);
    EXPECT(prefwin_box_of_page(6), BOX_WARNINGS_B);
    EXPECT(prefwin_box_of_page(7), -1);

    // The merge: each word of a kind is or'd into its game word.
    int32_t words[PREFWIN_WORDS] = {0x1, 0x10, 0x2, 0x20, 0x4, 0x40, 0x8, 0x80, 0x100, 0x200, 0x400}, out[3] = {0, 0, 0};
    prefwin_merge(words, out);
    EXPECT(out[PREF_WORD_A], 0x1 | 0x2 | 0x4 | 0x80 | 0x200);
    EXPECT(out[PREF_WORD_B], 0x10 | 0x20 | 0x40 | 0x100 | 0x400);
    EXPECT(out[PREF_WORD_C], 0x8);
    out[PREF_WORD_C] = 0x1000;  // (what the window already holds stays)
    prefwin_merge(words, out);
    EXPECT(out[PREF_WORD_C], 0x1008);

    // The arrow keys go round the six tabs.
    EXPECT(prefwin_step_page(0, 1), 5);
    EXPECT(prefwin_step_page(3, 1), 2);
    EXPECT(prefwin_step_page(5, 0), 0);
    EXPECT(prefwin_step_page(4, 0), 5);
    EXPECT(prefwin_step_page(6, 0), 0);  // (from the seventh page, Down is as from the last)
    EXPECT(prefwin_step_page(6, 1), 5);
    EXPECT(prefwin_step_page(-1, 1), 5);

    // The labels of the volume buttons: off is the lower of each pair.
    EXPECT(prefwin_toggle_label(0x55, 0), 0x438);
    EXPECT(prefwin_toggle_label(0x55, 1), 0x439);
    EXPECT(prefwin_toggle_label(0x56, 0), 0x43c);
    EXPECT(prefwin_toggle_label(0x56, 1), 0x43d);
    EXPECT(prefwin_toggle_label(0x57, 0), 0x43a);
    EXPECT(prefwin_toggle_label(0x57, 1), 0x43b);
    EXPECT(prefwin_toggle_label(0x58, 0), 0x43e);
    EXPECT(prefwin_toggle_label(0x58, 1), 0x43f);
    EXPECT(prefwin_toggle_label(0x59, 1), -1);

    // Where the window opens: centred across the screen, at 0x26a from the bottom; an 800 pixel screen puts it at the top and
    // shifts what is laid under it. The width is that of the window plus 0x4e; halves go toward zero.
    struct prefwin_origin o;
    prefwin_origin(&o, 1024, 768, 700);
    EXPECT(o.x, 162), EXPECT(o.y, 768 - 0x26a), EXPECT(o.small, 0);
    prefwin_origin(&o, 1920, 1080, 700);
    EXPECT(o.x, 610), EXPECT(o.y, 1080 - 0x26a), EXPECT(o.small, 0);
    prefwin_origin(&o, 800, 600, 700);
    EXPECT(o.x, 50), EXPECT(o.y, 0), EXPECT(o.small, 1);
    prefwin_origin(&o, 801, 600, 701);
    EXPECT(o.x, 400 - 350), EXPECT(o.small, 0);

    // The boxes' heights for the game's numbers (the area is 376 high, the gap 3, 88 left under the frame): the audio box less
    // 0x50 on a small screen, the first base warnings box 0x28 taller.
    EXPECT(prefwin_box_height(BOX_BASIC, 376, 3, 88, 0), 376);
    EXPECT(prefwin_box_height(BOX_MAP, 376, 3, 88, 0), 376);
    EXPECT(prefwin_box_height(BOX_AUDIO, 376, 3, 88, 0), 285);
    EXPECT(prefwin_box_height(BOX_AUDIO, 376, 3, 88, 0x50), 205);
    EXPECT(prefwin_box_height(BOX_WARNINGS_A, 376, 3, 88, 0), 325);
    EXPECT(prefwin_box_height(BOX_WARNINGS_B, 376, 3, 88, 0), 285);

    if (failures) printf("%d failure(s)\n", failures);
    else printf("ui_prefwin: ok\n");
    return failures != 0;
}
