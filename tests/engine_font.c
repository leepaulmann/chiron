// Unit test for the Font class's set-up and closing, with stand-ins for the Windows calls that
// record what they were asked: no game files needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/font.h"
#include "../src/engine/game.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))

#define STD __attribute__((stdcall))
static int next_handle = 100, fonts_made, fonts_deleted, removed, added, add_result = 1, posted, dcs_made, dcs_deleted;
static int selected[8], n_selected, create_fails;
static struct {
    int32_t height, width, esc, orient, weight;
    uint8_t italic, underline, strike, charset, out, clip, quality, pitch;
    char face[256];
} last_lf;
static char last_resource[64], last_scalable_ttf[64];
static int32_t tm_values[5] = {20, 16, 4, 3, 2};  // height, ascent, descent, internal and external leading
static void *stock = (void *)0x77;

static void *STD create_font(const void *lf) {
    memcpy(&last_lf, lf, 60);
    strncpy(last_lf.face, (const char *)lf + 28, 31);
    if (create_fails) return NULL;
    fonts_made++;
    return (void *)(intptr_t)next_handle++;
}
static int STD get_tm(void *dc, void *tm) { return memcpy(tm, tm_values, sizeof tm_values), 1; }
static void *STD select_object(void *dc, void *o) {
    if (n_selected < 8) selected[n_selected++] = (int)(intptr_t)o;
    return (void *)7;
}
static void *STD get_stock(int which) { return which == 13 ? stock : NULL; }
static int STD delete_object(void *o) { return fonts_deleted++, 1; }
static int STD remove_resource(const char *f) { return removed++, strncpy(last_resource, f, 63), 1; }
static int STD add_resource(const char *f) { return added++, add_result; }
static int STD create_scalable(int h, const char *fot, const char *ttf, const char *path) {
    return strncpy(last_scalable_ttf, ttf, 63), 1;
}
static int STD post(void *wnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    return posted += (wnd == (void *)0xffff && msg == 0x1d), 1;
}
static unsigned STD cwd(unsigned n, char *buf) { return strcpy(buf, "/game"), 5; }
static void *STD create_dc(void *dc) { return dcs_made++, (void *)(intptr_t)next_handle++; }
static int STD delete_dc(void *dc) { return dcs_deleted++, 1; }
static int frees;
static void free_stub(void *p) { frees++, free(p); }
static void *mem_stub(size_t n) { return malloc(n); }

int main(void) {
    static void *font_dc = (void *)0x55, *font_default;
    static int32_t count;
    static const char *face = "Face", *face_var;
    game_create_font_indirect = create_font, game_get_text_metrics = get_tm, game_select_object = select_object;
    game_get_stock_object = get_stock, game_delete_object = delete_object, game_remove_font_resource = remove_resource;
    game_add_font_resource = add_resource, game_create_scalable_font_resource = create_scalable, game_post_message = post;
    game_get_current_directory = cwd, game_create_compatible_dc = create_dc, game_delete_dc = delete_dc;
    game_free = free_stub, game_mem_get = mem_stub;
    game_font_dc = &font_dc, game_font_default = &font_default, game_font_class_count = &count;
    game_fot_ext = ".fot", game_font_class_face = "Class Face";
    face_var = face, game_font_face = &face_var;

    static struct font f;
    memset(&f, 0x5a, sizeof f);
    font_ctor(&f);

    // A new Font with arguments: bold and underlined at size 12, measured in the font class's DC.
    EXPECT(font_ctor_2(&f, "Face", 12, 5) == &f, 1);
    EXPECT(last_lf.height, -12), EXPECT(last_lf.weight, 700), EXPECT(last_lf.italic, 0), EXPECT(last_lf.underline, 1);
    EXPECT(last_lf.out, 7), EXPECT(strcmp(last_lf.face, "Face"), 0), EXPECT(f.first, -1);
    EXPECT(f.hfont == (void *)100, 1), EXPECT(f.ascent, 22), EXPECT(f.height, 13), EXPECT(f.f14, 3);
    EXPECT(f.f18, 16), EXPECT(f.f1c, 4), EXPECT(n_selected, 2), EXPECT(selected[0], 100), EXPECT(selected[1], 0x77);

    // Setting it up again deletes the old GDI font; italic, not bold.
    EXPECT(font_init(&f, "Other", 9, 2), 0);
    EXPECT(fonts_deleted, 1), EXPECT(last_lf.weight, 0), EXPECT(last_lf.italic, 1), EXPECT(last_lf.underline, 0);
    EXPECT(font_init(&f, NULL, 9, 2), 3);
    EXPECT(fonts_deleted, 1);  // a missing face changes nothing
    create_fails = 1;
    EXPECT(font_init(&f, "Other", 9, 2), 0xd);
    EXPECT(f.hfont == NULL, 1), EXPECT(f.ascent, 0), EXPECT(f.f1c, 0);
    create_fails = 0;

    // From a file: the resource is made from the name with its extension swapped for .fot.
    EXPECT(font_init_2(&f, NULL, "Face", 12, 0), 0x10);
    EXPECT(font_init_2(&f, "x/y.ttf", NULL, 12, 0), 0x10);
    EXPECT(font_init_2(&f, "x/y.ttf", "Face", 12, 0), 0);
    EXPECT(strcmp(f.name, "x/y.fot"), 0), EXPECT(strcmp(last_scalable_ttf, "x/y.ttf"), 0), EXPECT(posted, 1);
    EXPECT(f.flags & 1, 1), EXPECT(f.hfont != NULL, 1), EXPECT(added, 1);
    // Set up again as a file's Font: the resource stays and so does the first-line value.
    f.first = 7;
    EXPECT(font_init(&f, "Face", 14, 0), 0);
    EXPECT(f.first, 7), EXPECT(removed, 0), EXPECT(f.name != NULL, 1);
    // Windows refusing the resource leaves the name and a failed result.
    add_result = 0;
    EXPECT(font_init_2(&f, "x/z.ttf", "Face", 12, 0), 1);
    EXPECT(removed, 1), EXPECT(strcmp(f.name, "x/z.fot"), 0), EXPECT(f.hfont == NULL, 1), EXPECT(f.first, -1);
    add_result = 1;

    // Close removes the resource and frees the name; closing a closed Font does nothing more.
    font_close(&f);
    EXPECT(f.name == NULL, 1), EXPECT(removed, 2), EXPECT(strcmp(last_resource, "x/z.fot"), 0), EXPECT(frees, 2);
    font_close(&f);
    EXPECT(removed, 2), EXPECT(f.first, -1);
    EXPECT(font_init_2(&f, "ab", "Face", 12, 0), 1 - 1);  // a short name: no write before the buffer
    font_dtor(&f);
    EXPECT(f.name == NULL, 1), EXPECT(f.hfont == NULL, 1), EXPECT(f.height, 0), EXPECT(f.ascent, 0);

    // The font class: the first user makes a DC and sets the default font up in its own face.
    memset(&f, 0, sizeof f);
    font_ctor(&f);
    font_dc = NULL;
    EXPECT(font_init_font_class(&f), 0);
    EXPECT(count, 1), EXPECT(dcs_made, 1), EXPECT(font_dc != NULL, 1), EXPECT(font_default == &f, 1);
    EXPECT(strcmp(last_lf.face, "Class Face"), 0), EXPECT(last_lf.height, -12), EXPECT(f.hfont != NULL, 1);
    EXPECT(font_init_font_class(NULL), 0);  // a later user only counts
    EXPECT(count, 2), EXPECT(dcs_made, 1);
    font_close_font_class();
    EXPECT(count, 1), EXPECT(dcs_deleted, 0);
    font_close_font_class();
    EXPECT(count, 0), EXPECT(dcs_deleted, 1), EXPECT(font_dc == NULL, 1), EXPECT(font_default == NULL, 1);
    // No font given to the first user: 3, and the count has gone up.
    EXPECT(font_init_font_class(NULL), 3);
    EXPECT(count, 1);
    count = 0;
    // An open default font isn't set up again.
    int made = fonts_made;
    EXPECT(font_init_font_class(&f), 0);
    EXPECT(fonts_made, made);

    if (failures) return 1;
    printf("engine_font: ok\n");
    return 0;
}
