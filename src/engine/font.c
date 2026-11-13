// The game's Font class (OpenSMACX's name): making a GDI font from a face name, size and
// flags, optionally from a font file, closing it, and the font class's measuring device
// context. Measuring with a Font is in text.c.
//
// A Font holds a GDI font (0x8), the metrics it was measured with (0xc line height, 0x10 the
// ascent less the internal leading, 0x14 the internal leading, 0x18 ascent, 0x1c descent),
// bit 0 of its flags (set when it came from a file) and the name of the font resource file
// the game made for it (0x24).
//
// These make and free Windows objects (GDI fonts, font resources) and use the game's heap, so
// a shadow check can't rewind them. As for a Buffer's set-up (bufobj.c), the game's own calls
// run the original in shadow mode and the sweep builds two Fonts side by side, runs the
// original on one and Chiron's code on the other, and compares them: every field, the GDI
// fonts behind them by their LOGFONT (hooks_font_same), and the measuring device contexts by
// state (hooks_dc_same). The font class's globals (the DC, the default font and the use
// count) are compared the same way and put back after the sweep.
//
// The font resource path of Font_init_2 (a font file given) only gets as far as the failure
// return in a sweep: Chiron's AddFontResourceA is given a .fot file that the game never
// makes (CreateScalableFontResourceA writes nothing here), so it reports failure. The path
// that goes on is checked by the unit test only.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "game.h"
#include "hooks.h"
#include "shadow.h"

#define FONT (&font_module)
extern struct hook_module font_module;

// The originals, reached through their trampolines.
SHADOW_DECLARE_T(void *, font_ctor_2, (struct font * f, const char *face, int size, int flags))
SHADOW_DECLARE_T(void, font_dtor, (struct font * f))
SHADOW_DECLARE_T(int, font_init, (struct font * f, const char *face, int size, int flags))
SHADOW_DECLARE_T(int, font_init_2, (struct font * f, const char *file, const char *face, int size, int flags))
SHADOW_DECLARE_T(void, font_close, (struct font * f))
static int (*orig_font_init_font_class)(struct font *f);
static struct hook_stats stats_font_init_font_class = {"font_init_font_class"};
static void (*orig_font_close_font_class)(void);
static struct hook_stats stats_font_close_font_class = {"font_close_font_class"};

// The fields of a Font, by where they are.
enum {
    FLAGS = 0x4,     // bit 0: the font came from a file
    HFONT = 0x8,
    LINE = 0xc,      // the height with the external leading
    HEIGHT = 0x10,   // the ascent less the internal leading
    LEADING = 0x14,  // the internal leading
    ASCENT = 0x18,
    DESCENT = 0x1c,
    NAME = 0x24,     // the font resource file's name, from the game's heap
};

static int32_t *at(struct font *f, int offset) { return (int32_t *)((uint8_t *)f + offset); }

// A LOGFONT with room for a longer face name than Windows would take.
struct logfont {
    int32_t height, width, escapement, orientation, weight;
    uint8_t italic, underline, strike_out, char_set, out_precision, clip_precision, quality, pitch_and_family;
    char face[256];
};

// The part of TEXTMETRIC the Font reads, which comes first in the structure.
struct text_metrics {
    int32_t height, ascent, descent, internal_leading, external_leading;
    uint8_t rest[0x40];
};

// Forget the metrics and delete the GDI font.
static void forget_font(struct font *f) {
    *at(f, HEIGHT) = *at(f, LINE) = *at(f, ASCENT) = *at(f, DESCENT) = 0;
    if (f->hfont) game_delete_object(f->hfont), f->hfont = NULL;
}

// Take the font resource away and free its name.
static void forget_resource(struct font *f) {
    if (!f->name) return;
    game_remove_font_resource(f->name);
    if (f->name) game_free(f->name);
    f->name = NULL;
}

// Close: nothing set, the GDI font deleted, and the font resource removed and its name freed.
void TEXT_THIS font_close(struct font *f) {
    f->first = -1;
    forget_font(f);
    forget_resource(f);
}

// The destructor does what close does.
void TEXT_THIS font_dtor(struct font *f) {
    f->first = -1;
    forget_font(f);
    forget_resource(f);
}

// Make the GDI font for a face at a size (the height of a character cell, so drawn with a
// negative height) with flags bit 0 bold, bit 1 italic, bit 2 underlined, measure it in the
// font class's device context, and keep the metrics. A Font that came from a file keeps its
// resource and its first-line value; any other is closed first. Returns 0, 3 for no face and
// 0xd if Windows made no font.
int TEXT_THIS font_init(struct font *f, const char *face, int size, int flags) {
    if (!face) return 3;
    if (!(f->flags & 1)) {
        f->first = -1;
        forget_font(f);
        forget_resource(f);
    } else {
        forget_font(f);
    }
    struct logfont lf;
    memset(&lf, 0, sizeof lf);
    lf.height = -size;
    lf.weight = (flags & 1) ? 700 : 0;
    lf.italic = (flags >> 1) & 1;
    lf.underline = (flags >> 2) & 1;
    lf.out_precision = 7;  // OUT_STROKE_PRECIS
    strncat(lf.face, face, sizeof lf.face - 1);
    f->hfont = game_create_font_indirect(&lf);
    if (!f->hfont) return 0xd;
    void *dc = *game_font_dc;
    game_select_object(dc, f->hfont);
    struct text_metrics tm;
    memset(&tm, 0, sizeof tm);
    game_get_text_metrics(dc, &tm);
    *at(f, LINE) = tm.height + tm.external_leading;
    *at(f, ASCENT) = tm.ascent;
    *at(f, HEIGHT) = tm.ascent - tm.internal_leading;
    *at(f, LEADING) = tm.internal_leading;
    *at(f, DESCENT) = tm.descent;
    game_select_object(dc, game_get_stock_object(13));  // SYSTEM_FONT
    return 0;
}

// Set up with the arguments already: unlike the constructor this doesn't clear the Font first.
void *TEXT_THIS font_ctor_2(struct font *f, const char *face, int size, int flags) {
    font_init(f, face, size, flags);
    return f;
}

// Set up from a font file: close the Font, make a font resource file (.fot) beside it under
// the file's name and add it to Windows, tell every window the fonts changed, then set up as
// Font_init does with the face the file holds. Returns 0x10 for no file or no face, 4 if the
// name can't be had, 1 if Windows won't take the resource, else Font_init's result. Unlike
// the original, a file name shorter than four characters doesn't write before its buffer.
int TEXT_THIS font_init_2(struct font *f, const char *file, const char *face, int size, int flags) {
    f->first = -1;
    forget_font(f);
    forget_resource(f);
    if (!file || !face) return 0x10;
    size_t n = strlen(file);
    char *name = game_mem_get(n + 1);
    f->name = name;
    if (!name) return 4;
    name[0] = 0;
    strcat(name, file);
    if (n >= 4) name[n - 4] = 0;
    strcat(name, game_fot_ext);
    char dir[0x104];
    game_get_current_directory(0x104, dir);
    game_create_scalable_font_resource(0, f->name, file, dir);
    if (!game_add_font_resource(f->name)) return 1;
    game_post_message((void *)0xffff, 0x1d, 0, 0);  // WM_FONTCHANGE to every window
    f->flags |= 1;
    return font_init(f, face, size, flags);
}

// The font class: the first user makes the device context fonts are measured in and sets the
// default font up (12 point, in the class's own face); later users only count. Returns 0, 3 for
// no font and 2 if no device context could be made. The count has gone up even then.
int font_init_font_class(struct font *def) {
    int n = ++*game_font_class_count;
    if (n > 1) return 0;
    if (!def) return 3;
    void *dc = game_create_compatible_dc(NULL);
    *game_font_dc = dc;
    if (!dc) return 2;
    *game_font_default = def;
    if (def->hfont) return 0;
    return font_init(def, game_font_class_face, 12, 0);
}

// The last user lets the device context go and forgets the default font.
void font_close_font_class(void) {
    if (--*game_font_class_count > 0) return;
    if (*game_font_dc) {
        game_delete_dc(*game_font_dc);
        *game_font_dc = NULL;
    }
    *game_font_default = NULL;
}

// ---- Shadow mode ---------------------------------------------------------------------------

// The game's calls run the original; the sweep checks Chiron's code on twin Fonts.
#define PASS_THROUGH(ret, name, params, args)    \
    static ret THISCALL shadow_##name params {   \
        if (!hooks_nested && !shadow_bypass()) { \
            stats_##name.calls++;                \
            shadow_tick(FONT);                   \
        }                                        \
        return orig_##name args;                 \
    }

PASS_THROUGH(void *, font_ctor_2, (struct font * f, const char *face, int size, int flags), (f, face, size, flags))
PASS_THROUGH(void, font_dtor, (struct font * f), (f))
PASS_THROUGH(int, font_init, (struct font * f, const char *face, int size, int flags), (f, face, size, flags))
PASS_THROUGH(int, font_init_2, (struct font * f, const char *file, const char *face, int size, int flags), (f, file, face, size, flags))
PASS_THROUGH(void, font_close, (struct font * f), (f))

static int shadow_font_init_font_class(struct font *f) {
    if (!hooks_nested && !shadow_bypass()) {
        stats_font_init_font_class.calls++;
        shadow_tick(FONT);
    }
    return orig_font_init_font_class(f);
}

static void shadow_font_close_font_class(void) {
    if (!hooks_nested && !shadow_bypass()) {
        stats_font_close_font_class.calls++;
        shadow_tick(FONT);
    }
    orig_font_close_font_class();
}

// The twins after the original ran on `a` (with `dc_a` as the font class's DC) and Chiron's code
// on `b` (with `dc_b`): NULL if they match. A name is compared by what it says, not where it is.
static const char *twin_diff(struct font *a, struct font *b, void *dc_a, void *dc_b, int got, int want) {
    static char diff[160];
    for (int o = 0; o < (int)sizeof(struct font); o += 4) {
        int32_t x = *at(a, o), y = *at(b, o);
        if ((o == HFONT || o == NAME) ? !x != !y : x != y) {
            snprintf(diff, sizeof diff, "field 0x%x = 0x%x, original 0x%x", o, (unsigned)y, (unsigned)x);
            return diff;
        }
    }
    if (a->name && b->name && strcmp(a->name, b->name)) return "resource file name";
    if (hooks_font_same && !hooks_font_same(a->hfont, b->hfont)) return "GDI font (face, size, weight or style)";
    if (hooks_dc_same && !hooks_dc_same(dc_a, dc_b)) return "device context state";
    if (got != want) {
        snprintf(diff, sizeof diff, "= %d, original %d", got, want);
        return diff;
    }
    return NULL;
}

// After a difference the twins are closed and made again alike, so each check that follows is of
// its own function rather than of the first difference.
static void twin_check(struct hook_stats *s, struct font *a, struct font *b, void *dc_a, void *dc_b, int got, int want,
                       const char *what) {
    const char *d = twin_diff(a, b, dc_a, dc_b, got, want);
    if (!d) return;
    hooks_mismatch(s, " (%s): %s", what, d);
    *game_font_dc = dc_a;
    orig_font_dtor(a);
    *game_font_dc = dc_b;
    orig_font_dtor(b);
    memset(a, 0, sizeof *a), memset(b, 0, sizeof *b);
    font_ctor(a), font_ctor(b);
}

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// The font class's globals after a call.
struct class_state {
    int32_t count;
    void *dc, *def;
    int ret;
};

static void class_read(struct class_state *s, int ret) {
    s->count = *game_font_class_count, s->dc = *game_font_dc, s->def = *game_font_default, s->ret = ret;
}

static void font_sweep(void) {
    if (!hooks_dc_same || !hooks_font_same || !game_font_face || !*game_font_face) return;
    static struct font a, b;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    void *saved_dc = *game_font_dc, *saved_default = *game_font_default;
    int32_t saved_count = *game_font_class_count;
    void *dc_a = game_create_compatible_dc(NULL), *dc_b = game_create_compatible_dc(NULL);
    rng_state = 0x1f83d9abu ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    const char *faces[2] = {*game_font_face, game_font_class_face};
    static const int sizes[] = {6, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 30};
    // Each call of the original is made with one device context as the font class's, Chiron's
    // with the other, so the two can be compared afterwards.
#define ORIG(expr) (*game_font_dc = dc_a, (expr))
#define OURS(expr) (*game_font_dc = dc_b, (expr))
#define CHECK(stats, got, want, what) twin_check(stats, &a, &b, dc_a, dc_b, got, want, what)
    for (int round = 0; round < 24; round++) {
        uint8_t fill = (uint8_t)rnd();
        memset(&a, fill, sizeof a), memset(&b, fill, sizeof b);
        font_ctor(&a), font_ctor(&b);
        a.name = b.name = NULL;
        a.flags = b.flags = 0;
        const char *face = faces[rnd_to(2)];
        int size = sizes[rnd_to(13)], flags = rnd_to(8);
        // A new Font made with its arguments.
        void *wa = ORIG(orig_font_ctor_2(&a, face, size, flags)), *wb = OURS(font_ctor_2(&b, face, size, flags));
        CHECK(&stats_font_ctor_2, wb == &b, wa == &a, "new");
        for (int k = 0; k < 6; k++) {
            face = faces[rnd_to(2)], size = sizes[rnd_to(13)], flags = rnd_to(8);
            switch (rnd_to(6)) {
            case 0: {
                // Set up again with another face, size and flags.
                int want = ORIG(orig_font_init(&a, face, size, flags)), got = OURS(font_init(&b, face, size, flags));
                CHECK(&stats_font_init, got, want, "init");
                break;
            }
            case 1: {
                int want = ORIG(orig_font_init(&a, NULL, size, flags)), got = OURS(font_init(&b, NULL, size, flags));
                CHECK(&stats_font_init, got, want, "init without a face");
                break;
            }
            case 2: {
                // As a Font that came from a file: the resource and the first-line value stay.
                a.flags |= 1, b.flags |= 1;
                int want = ORIG(orig_font_init(&a, face, size, flags)), got = OURS(font_init(&b, face, size, flags));
                CHECK(&stats_font_init, got, want, "init of a file's font");
                a.flags &= ~1u, b.flags &= ~1u;
                break;
            }
            case 3: {
                // From the game's own font file (Windows gets no resource to add, see the top).
                int want = ORIG(orig_font_init_2(&a, game_font_file, face, size, flags));
                int got = OURS(font_init_2(&b, game_font_file, face, size, flags));
                CHECK(&stats_font_init_2, got, want, "init_2");
                want = ORIG(orig_font_init_2(&a, NULL, face, size, flags));
                got = OURS(font_init_2(&b, NULL, face, size, flags));
                CHECK(&stats_font_init_2, got, want, "init_2 without a file");
                want = ORIG(orig_font_init_2(&a, game_font_file, NULL, size, flags));
                got = OURS(font_init_2(&b, game_font_file, NULL, size, flags));
                CHECK(&stats_font_init_2, got, want, "init_2 without a face");
                break;
            }
            case 4:
                ORIG(orig_font_close(&a)), OURS(font_close(&b));
                CHECK(&stats_font_close, 0, 0, "close");
                ORIG(orig_font_close(&a)), OURS(font_close(&b));
                CHECK(&stats_font_close, 0, 0, "close of a closed Font");
                break;
            default:
                // A Font with a GDI font and a resource name (init_2 leaves the name when Windows
                // refuses; a font set up as a file's keeps it), closed.
                ORIG(orig_font_init_2(&a, game_font_file, face, size, flags));
                OURS(font_init_2(&b, game_font_file, face, size, flags));
                a.flags |= 1, b.flags |= 1;
                ORIG(orig_font_init(&a, face, size, flags)), OURS(font_init(&b, face, size, flags));
                a.flags &= ~1u, b.flags &= ~1u;
                ORIG(orig_font_close(&a)), OURS(font_close(&b));
                CHECK(&stats_font_close, 0, 0, "close with a resource");
                break;
            }
        }
        // Delete: the destructor does what close does.
        // with a GDI font and a resource name (init_2 leaves the name when Windows refuses; a
        // font set up as a file's keeps it).
        ORIG(orig_font_init_2(&a, game_font_file, faces[0], 12, 0));
        OURS(font_init_2(&b, game_font_file, faces[0], 12, 0));
        a.flags |= 1, b.flags |= 1;
        ORIG(orig_font_init(&a, faces[0], 12, 0)), OURS(font_init(&b, faces[0], 12, 0));
        a.flags &= ~1u, b.flags &= ~1u;
        ORIG(orig_font_dtor(&a)), OURS(font_dtor(&b));
        CHECK(&stats_font_dtor, 0, 0, "delete");
        ORIG(orig_font_dtor(&a)), OURS(font_dtor(&b));
        CHECK(&stats_font_dtor, 0, 0, "delete of a closed Font");
    }

    // The font class. Setting it up with a use count before of -1, 0 or 1, a default font that
    // is open or not (or none at all), and a device context already there or not; each side has
    // its own device contexts and default font.
    static struct font da, db;
    for (int round = 0; round < 12; round++) {
        int count = rnd_to(3) - 1, open = rnd_to(2), nofont = rnd_to(4) == 0, have_dc = rnd_to(2);
        font_ctor(&da), font_ctor(&db);
        da.name = db.name = NULL;
        da.flags = db.flags = 0;
        if (open) ORIG(orig_font_init(&da, faces[0], 12, 0)), OURS(font_init(&db, faces[0], 12, 0));
        void *pre_a = have_dc ? game_create_compatible_dc(NULL) : NULL, *pre_b = have_dc ? game_create_compatible_dc(NULL) : NULL;
        struct class_state ra, rb;
        *game_font_class_count = count, *game_font_dc = pre_a, *game_font_default = NULL;
        int ret = orig_font_init_font_class(nofont ? NULL : &da);
        class_read(&ra, ret);
        *game_font_class_count = count, *game_font_dc = pre_b, *game_font_default = NULL;
        ret = font_init_font_class(nofont ? NULL : &db);
        class_read(&rb, ret);
        const char *d = NULL;
        if (ra.count != rb.count) d = "use count";
        else if (!ra.dc != !rb.dc) d = "device context set";
        else if ((ra.def == &da) != (rb.def == &db) || !ra.def != !rb.def) d = "default font set";
        else if (ra.ret != rb.ret) d = "result";
        else d = twin_diff(&da, &db, ra.dc ? ra.dc : dc_a, rb.dc ? rb.dc : dc_b, 0, 0);
        if (d) hooks_mismatch(&stats_font_init_font_class, " (count %d, %s%s): %s", count, open ? "open" : "closed", nofont ? ", no font" : "", d);
        // Let the fonts and device contexts go, the class's as it stood after each call.
        *game_font_dc = dc_a, orig_font_close(&da);
        *game_font_dc = dc_b, font_close(&db);
        if (ra.dc) game_delete_dc(ra.dc);
        if (rb.dc) game_delete_dc(rb.dc);
        if (pre_a && pre_a != ra.dc) game_delete_dc(pre_a);
        if (pre_b && pre_b != rb.dc) game_delete_dc(pre_b);

        // Closing it with a use count of 0 to 2, a device context there or not, and a default font.
        int before = rnd_to(3);
        have_dc = rnd_to(4) != 0;
        pre_a = game_create_compatible_dc(NULL), pre_b = game_create_compatible_dc(NULL);
        *game_font_class_count = before, *game_font_dc = have_dc ? pre_a : NULL, *game_font_default = &da;
        orig_font_close_font_class();
        class_read(&ra, 0);
        *game_font_class_count = before, *game_font_dc = have_dc ? pre_b : NULL, *game_font_default = &db;
        font_close_font_class();
        class_read(&rb, 0);
        d = NULL;
        if (ra.count != rb.count) d = "use count";
        else if (!ra.dc != !rb.dc) d = "device context set";
        else if (!ra.def != !rb.def) d = "default font set";
        if (d) hooks_mismatch(&stats_font_close_font_class, " (count %d, %s): %s", before, have_dc ? "with a DC" : "no DC", d);
        // The device contexts the class kept (and the spare when none was given) are ours to delete.
        void *left_a = have_dc ? ra.dc : pre_a, *left_b = have_dc ? rb.dc : pre_b;
        if (left_a) game_delete_dc(left_a);
        if (left_b) game_delete_dc(left_b);
    }
    game_delete_dc(dc_a), game_delete_dc(dc_b);
    *game_font_dc = saved_dc, *game_font_default = saved_default, *game_font_class_count = saved_count;
    hooks_sweeping = sweeping;
#undef ORIG
#undef OURS
#undef CHECK
}

static const struct hook font_hooks[] = {
    HOOK(0x618ec0, font_ctor_2, 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x618ee0, font_dtor, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff, 0x8b, 0x46, 0x08),
    HOOK(0x618f40, font_init, 0x83, 0xec, 0x74, 0x53, 0x56, 0x57),
    HOOK(0x6190d0, font_init_2, 0x81, 0xec, 0x04, 0x01, 0x00, 0x00, 0x53, 0x55),
    HOOK(0x619230, font_close, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xff, 0x8b, 0x46, 0x08),
    HOOK(0x6195b0, font_init_font_class, 0xa1, 0x8c, 0xb4, 0x9b, 0x00, 0x56, 0x40),
    HOOK(0x619610, font_close_font_class, 0xa1, 0x8c, 0xb4, 0x9b, 0x00, 0x48),
};
struct hook_module font_module = {"font", font_hooks, HOOK_COUNT(font_hooks), font_sweep, NULL, -1};
