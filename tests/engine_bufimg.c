// Unit test for the picture and Sprite files and the Buffer's copies through Windows, with stand-ins for the file
// mapping object, the C runtime's file calls and the Windows calls (that record what they were asked): no game files
// needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/bufimg.h"
#include "../src/engine/bufobj.h"
#include "../src/engine/game.h"

static int failures;
static void check_(const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL %s = %ld, expected %ld\n", what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(#expr, (long)(expr), (long)(want))
#define AT(b, o) (*(int32_t *)((uint8_t *)(b) + (o)))

#define STD __attribute__((stdcall))
#define THIS __attribute__((thiscall))

// ---- Stand-ins for the Windows objects a Buffer makes ----------------------------------------
static int next_handle = 100;
static uint8_t dib_pixels[64 * 64];
static void *STD create_dc(void *dc) { return (void *)(intptr_t)next_handle++; }
static void *STD create_dib(void *dc, void *info, int usage, void **bits, void *s, int o) {
    *bits = dib_pixels;
    return (void *)(intptr_t)next_handle++;
}
static int STD delete_dc(void *dc) { return 1; }
static int STD delete_object(void *o) { return 1; }
static void *STD create_rgn(const int32_t *r) { return (void *)(intptr_t)next_handle++; }
static int STD select_clip_rgn(void *dc, void *rgn) { return 1; }
static void *STD select_object(void *dc, void *o) { return (void *)7; }
static int STD set_bk_mode(void *dc, int m) { return 0; }
static int STD set_text_align(void *dc, int a) { return 0; }
static int STD intersect(int32_t *out, const int32_t *a, const int32_t *b) {
    int32_t r[4] = {a[0] > b[0] ? a[0] : b[0], a[1] > b[1] ? a[1] : b[1], a[2] < b[2] ? a[2] : b[2], a[3] < b[3] ? a[3] : b[3]};
    if (r[0] >= r[2] || r[1] >= r[3]) return memset(out, 0, 16), 0;
    return memcpy(out, r, 16), 1;
}
static int STD equal(const int32_t *a, const int32_t *b) { return !memcmp(a, b, 16); }
static int STD set_rect(int32_t *r, int l, int t, int rt, int b) { return r[0] = l, r[1] = t, r[2] = rt, r[3] = b, 1; }
static void THIS spot_ctor(void *s) { memset(s, 0, 12); }
static void THIS spot_dtor(void *s) { memset(s, 0, 12); }
static void THIS spot_init(void *s, int n) { ((int32_t *)s)[2] = n; }
static void *THIS vt_dtor(void *b) { return NULL; }
static void *THIS vt_owner(void *b) { return NULL; }
static void *vtable[4] = {(void *)vt_dtor, (void *)vt_owner};

// ---- The file mapping object, as files in memory ---------------------------------------------
static struct {
    const char *name;
    const uint8_t *data;
    int size;
} files[4];
static int n_files;
static char last_opened[300], last_created[300];
static int open_calls, close_calls, release_calls;
static uint8_t made[8192];
static int made_size, made_len = -1;
static int cur_size;
static void *THIS mfile_ctor(struct game_mfile *f) { return f->view = NULL, f->handle = (void *)-1, f->mapping = NULL, f; }
static void THIS mfile_close(struct game_mfile *f) { close_calls++; }
static uint8_t *find_file(struct game_mfile *f, const char *name) {
    snprintf(last_opened, sizeof last_opened, "%s", name);
    open_calls++;
    for (int i = 0; i < n_files; i++)
        if (!strcmp(files[i].name, name)) return cur_size = files[i].size, f->handle = (void *)(intptr_t)(i + 1), f->view = (void *)files[i].data, (uint8_t *)files[i].data;
    return NULL;
}
static uint8_t *THIS mfile_open(struct game_mfile *f, const char *name, int flag) { return find_file(f, name); }
static uint8_t *THIS mfile_open_rw(struct game_mfile *f, const char *name, int flag) { return find_file(f, name); }
static uint8_t *THIS mfile_create(struct game_mfile *f, const char *name, int size, int flag) {
    snprintf(last_created, sizeof last_created, "%s", name);
    if (!strncmp(name, "nodir", 5)) return NULL;
    made_size = size, made_len = -1;
    memset(made, 0, sizeof made);
    return f->view = made;
}
static void THIS mfile_release(struct game_mfile *f) { release_calls++; }
static void THIS mfile_finish(struct game_mfile *f, const uint8_t *end) { made_len = (int)(end - made); }
static uint32_t STD get_file_size(void *h, uint32_t *high) { return (uint32_t)files[(intptr_t)h - 1].size; }
static const char *rfind(const char *start, const char *end, int c) {
    if (!start || !end) return NULL;
    for (long n = end - start; n > 0; n--, end--)
        if (*end == c) return end;
    return NULL;
}

// ---- The C runtime's file calls, to a buffer -------------------------------------------------
static uint8_t bmp_out[8192];
static int bmp_len, fcloses;
static char fopen_name[300], fopen_mode[8];
static void *env_open(const char *name, const char *mode) {
    snprintf(fopen_name, sizeof fopen_name, "%s", name), snprintf(fopen_mode, sizeof fopen_mode, "%s", mode);
    return !strncmp(name, "nodir", 5) ? NULL : (void *)1;
}
static size_t cwrite(const void *buf, size_t len, size_t count, void *fp) {
    memcpy(bmp_out + bmp_len, buf, len * count), bmp_len += (int)(len * count);
    return count;
}
static int cclose(void *fp) { return fcloses++, 0; }

// ---- The Windows calls the loaders and copies make -------------------------------------------
static int dib_sets, dib_set_dc;
static int STD set_dib_color_table(void *dc, unsigned start, unsigned n, const void *c) {
    dib_sets++, dib_set_dc = (int)(intptr_t)dc;
    return (int)n;
}
static int blt_args[11], blts;
static int STD stretch_blt(void *d, int x, int y, int w, int h, void *s, int sx, int sy, int sw, int sh, unsigned rop) {
    int v[11] = {(int)(intptr_t)d, x, y, w, h, (int)(intptr_t)s, sx, sy, sw, sh, (int)rop};
    memcpy(blt_args, v, sizeof v);
    return blts++, 1;
}
static int frees;
static void free_stub(void *p) { frees++, free(p); }
static void *mem_stub(size_t n) { return calloc(1, n); }
static int fake_random(int lo, int hi) { return lo + 7; }

// A PCX file: the header, rows coded by the caller, the 0x0c marker and a palette of r, g, b = i, i + 1, i + 2.
static int pcx(uint8_t *out, int w, int h, int bpl, int version, const uint8_t *coded, int coded_len) {
    memset(out, 0, 0x80);
    out[0] = 0x0a, out[1] = (uint8_t)version, out[2] = 1, out[3] = 8, out[8] = (uint8_t)(w - 1), out[10] = (uint8_t)(h - 1);
    out[0x41] = 1, out[0x42] = (uint8_t)bpl;
    memcpy(out + 0x80, coded, (size_t)coded_len);
    uint8_t *p = out + 0x80 + coded_len;
    *p++ = 0x0c;
    for (int i = 0; i < 256; i++) *p++ = (uint8_t)i, *p++ = (uint8_t)(i + 1), *p++ = (uint8_t)(i + 2);
    return (int)(p - out);
}

int main(void) {
    static void *ddraw, *palette_now, *font_default;
    static int32_t default_520 = 0x1234, video_mode = 3;
    static uint32_t pal_handle = 1, seed;
    static struct palette game_pal, *game_pal_p = &game_pal;
    static int32_t sprite_bytes = 1000;
    game_create_compatible_dc = create_dc, game_create_dib_section = create_dib, game_delete_dc = delete_dc;
    game_delete_object = delete_object, game_create_rect_rgn_indirect = create_rgn, game_select_clip_rgn = select_clip_rgn;
    game_select_object = select_object, game_set_bk_mode = (void *)set_bk_mode, game_set_text_align = set_text_align;
    game_intersect_rect = intersect, game_equal_rect = equal, game_set_rect = set_rect;
    game_ddraw = &ddraw, game_palette_now = &palette_now, game_font_default = (void **)&font_default;
    game_buffer_vtable = vtable, game_buffer_520_default = &default_520;
    game_spot_ctor = spot_ctor, game_spot_dtor = spot_dtor, game_spot_init = spot_init, game_free = free_stub;
    game_mem_get = mem_stub, game_palette_handle = &pal_handle, game_random = fake_random, game_random_seed = &seed;
    game_mfile_ctor = mfile_ctor, game_mfile_close = mfile_close, game_mfile_open = mfile_open, game_mfile_open_rw = mfile_open_rw;
    game_mfile_create = mfile_create, game_mfile_release = mfile_release, game_mfile_finish = mfile_finish;
    game_rfind_char = rfind, game_get_file_size = get_file_size, game_env_open = env_open, game_fwrite = cwrite, game_fclose = cclose;
    game_set_dib_color_table = set_dib_color_table, game_stretch_blt = stretch_blt, game_palette_copy = &game_pal_p;
    game_video_mode = &video_mode, game_sprite_bytes = &sprite_bytes;
    game_pcx_prefix = "PFX", game_pcx_ext = ".pcx", game_pcx_write_ext = ".pcx", game_bmp_write_ext = ".bmp";
    game_bmp_mode = "w+b", game_bmp_signature = "BM";
    game_tga_ext[0] = ".tgf", game_tga_ext[1] = ".tgg", game_tga_ext[2] = ".tga";
    game_bmp_ext[0] = ".bmf", game_bmp_ext[1] = ".bmg", game_bmp_ext[2] = ".bmp";

    static struct game_buffer b;
    memset(&b, 0, sizeof b);
    buffer_ctor(&b);

    // ---- A PCX picture in memory: 3 by 2 pixels in rows of 4, a run of three and a single, then two singles and a run of two.
    static uint8_t file[1200];
    static const uint8_t coded[] = {0xc3, 7, 9, 1, 2, 0xc2, 3};
    int size = pcx(file, 3, 2, 4, 5, coded, sizeof coded);
    EXPECT(buffer_load_pcx_2(&b, file, size, NULL, 0, 256), 0);
    EXPECT(b.width, 3), EXPECT(b.neg_height, -2), EXPECT(b.pitch, 4);
    EXPECT(dib_pixels[0], 7), EXPECT(dib_pixels[2], 7), EXPECT(dib_pixels[3], 9), EXPECT(dib_pixels[4], 1), EXPECT(dib_pixels[7], 3);
    EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
    // Colours come from the file's end into the Buffer's table (blue, green, red, 0), from `first` for `count`.
    EXPECT(((uint8_t *)&b)[0xa4 + 4 * 5 + 2], 5), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 5 + 1], 6), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 5], 7);
    EXPECT(((uint8_t *)&b)[0xa4 + 4 * 255 + 2], 255), EXPECT(((uint8_t *)&b)[0x580], 0), EXPECT(AT(&b, 0x57c), 0);
    // A first colour adds to every pixel and says where the colours go (and are read from the same start).
    EXPECT(buffer_load_pcx_2(&b, file, size, NULL, 10, 3), 0);
    EXPECT(dib_pixels[0], 17), EXPECT(dib_pixels[3], 19), EXPECT(((uint8_t *)&b)[0x580], 10);
    // (the colours are read from the file's first triple on, whatever `first` is: entry 12 gets the third triple)
    EXPECT(((uint8_t *)&b)[0xa4 + 4 * 10 + 2], 0), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 12 + 2], 2), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 13 + 2], 13);
    // With a palette: its colours 10 to 245 are the Buffer's table, and the table is set on the memory device context.
    game_pal.serial = 77;
    dib_sets = 0;
    EXPECT(buffer_load_pcx_2(&b, file, size, &game_pal, 0, 256), 0);
    EXPECT(game_pal.rgb[10][0], 10), EXPECT(game_pal.rgb[10][1], 11), EXPECT(game_pal.rgb[10][2], 12), EXPECT(game_pal.rgb[245][0], 245);
    EXPECT(AT(&b, 0x4a4), game_pal.serial), EXPECT(AT(&b, 0x57c), 1), EXPECT(AT(&b, 0x584) == (int32_t)(intptr_t)&game_pal, 1);
    EXPECT(dib_sets, 1), EXPECT(AT(&b, 0x68), 0), EXPECT(AT(&b, 0x60), 0);
    // Not a 256-colour RLE PCX, or a version before 4 when a palette is wanted: 3. No data: 3. A prefix is skipped.
    static uint8_t bad[1200];
    memcpy(bad, file, (size_t)size);
    bad[0] = 0x0b;
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 3);
    memcpy(bad, file, (size_t)size), bad[2] = 0;
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 3);
    memcpy(bad, file, (size_t)size), bad[3] = 7;
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 3);
    memcpy(bad, file, (size_t)size), bad[0x41] = 3;
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 3);
    memcpy(bad, file, (size_t)size), bad[1] = 3;
    EXPECT(buffer_load_pcx_2(&b, bad, size, &game_pal, 0, 256), 3);
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 0);  // an old version does without a palette
    EXPECT(buffer_load_pcx_2(&b, NULL, 0, NULL, 0, 256), 3);
    memcpy(bad, "PFX\0", 4), memcpy(bad + 4, file, (size_t)size);
    memset(dib_pixels, 0, 16);
    EXPECT(buffer_load_pcx_2(&b, bad, size + 4, NULL, 0, 256), 0);
    EXPECT(dib_pixels[0], 7), EXPECT(dib_pixels[7], 3);
    // Forty bits to a pixel: a row of no bytes, nothing drawn, and the resize still happened.
    memcpy(bad, file, (size_t)size), bad[3] = 40;
    memset(dib_pixels, 0x55, 16);
    EXPECT(buffer_load_pcx_2(&b, bad, size, NULL, 0, 256), 0), EXPECT(dib_pixels[0], 0x55);

    // By name: the extension is added if the name has none, and 3 and 6 for no name and no file.
    files[n_files].name = "pic.pcx", files[n_files].data = file, files[n_files++].size = size;
    EXPECT(buffer_load_pcx(&b, "pic", NULL, 0, 256), 0), EXPECT(strcmp(last_opened, "pic.pcx"), 0), EXPECT(dib_pixels[0], 7);
    EXPECT(buffer_load_pcx(&b, "pic.pcx", NULL, 0, 256), 0), EXPECT(strcmp(last_opened, "pic.pcx"), 0);
    EXPECT(buffer_load_pcx(&b, "nothing", NULL, 0, 256), 6), EXPECT(strcmp(last_opened, "nothing.pcx"), 0);
    EXPECT(buffer_load_pcx(&b, NULL, NULL, 0, 256), 3);
    EXPECT(buffer_load_pcx(&b, ".pcx", NULL, 0, 256), 6), EXPECT(strcmp(last_opened, ".pcx.pcx"), 0);  // a dot first is no extension

    // The targa and bitmap loaders open the file, by the video mode's extension, and give up.
    files[n_files].name = "tex.tga", files[n_files].data = file, files[n_files++].size = size;
    video_mode = 3;
    EXPECT(buffer_load_tga(&b, "tex"), 8), EXPECT(strcmp(last_opened, "tex.tga"), 0);
    video_mode = 1;
    EXPECT(buffer_load_tga_2(&b, "tex", 0, 0, 0x100), 6), EXPECT(strcmp(last_opened, "tex.tgf"), 0);
    video_mode = 2;
    EXPECT(buffer_load_tga(&b, "x"), 6), EXPECT(strcmp(last_opened, "x.tgg"), 0);
    EXPECT(buffer_load_bmp(&b, "x"), 6), EXPECT(strcmp(last_opened, "x.bmg"), 0);
    video_mode = 1;
    EXPECT(buffer_load_bmp_2(&b, "x", 1, 2, 3), 6), EXPECT(strcmp(last_opened, "x.bmf"), 0);
    video_mode = 5;
    EXPECT(buffer_load_bmp(&b, "x"), 6), EXPECT(strcmp(last_opened, "x.bmp"), 0);
    EXPECT(buffer_load_bmp(&b, "tex.tga"), 8), EXPECT(buffer_load_tga(&b, NULL), 3), EXPECT(buffer_load_bmp_2(&b, NULL, 0, 0, 0), 3);

    // ---- Writing a PCX: the 3 by 2 picture back, with the palette of the Buffer.
    EXPECT(buffer_load_pcx_2(&b, file, size, NULL, 0, 256), 0);
    EXPECT(buffer_write_pcx(&b, "out"), 0);
    EXPECT(strcmp(last_created, "out.pcx"), 0), EXPECT(made_size, 0x80 + 2 * 3 * 2);
    EXPECT(made[0], 0x0a), EXPECT(made[1], 5), EXPECT(made[2], 1), EXPECT(made[3], 8), EXPECT(made[8], 2), EXPECT(made[10], 1);
    EXPECT(made[0x41], 1), EXPECT(made[0x42], 4), EXPECT(made[0x43], 0), EXPECT(made[0x10], 0), EXPECT(made[0x40], 0);
    // Row 1: 7 7 7 9 = a run of three, a single below 0x40; row 2: 1 2 3 3 = singles 1 and 2 then a run of two.
    // Single bytes of 0x40 and more get a count of one.
    EXPECT(made[0x80], 0xc3), EXPECT(made[0x81], 7), EXPECT(made[0x82], 9), EXPECT(made[0x83], 1), EXPECT(made[0x84], 2);
    EXPECT(made[0x85], 0xc2), EXPECT(made[0x86], 3), EXPECT(made[0x87], 0x0c);
    EXPECT(made[0x88 + 15], 5), EXPECT(made[0x89 + 15], 6), EXPECT(made[0x8a + 15], 7);  // red, green, blue of colour 5
    EXPECT(made_len, 0x87 + 1 + 768), EXPECT(b.locks, 0), EXPECT(b.base == NULL, 1);
    dib_pixels[0] = 0x50, dib_pixels[1] = 0xc5, dib_pixels[2] = 0xc5, dib_pixels[3] = 0;
    EXPECT(buffer_write_pcx(&b, "name.x"), 0), EXPECT(strcmp(last_created, "name.x"), 0);
    EXPECT(made[0x80], 0xc1), EXPECT(made[0x81], 0x50), EXPECT(made[0x82], 0xc2), EXPECT(made[0x83], 0xc5), EXPECT(made[0x84], 0);  // a 0 in the row's last place, before the 1 below it: a single byte
    EXPECT(buffer_write_pcx(&b, "nodir/x"), 4), EXPECT(b.locks, 0);
    // A run stops at the end of the row, and at 63.
    static struct game_buffer wide;
    memset(&wide, 0, sizeof wide), buffer_ctor(&wide);
    EXPECT(buffer_init(&wide, 64, 1, 0, NULL), 0);
    memset(dib_pixels, 9, 64);
    EXPECT(buffer_write_pcx(&wide, "w"), 0);
    EXPECT(made[0x80], 0xc0 | 63), EXPECT(made[0x81], 9), EXPECT(made[0x82], 9), EXPECT(made[0x83], 0x0c);
    buffer_dtor(&wide);
    EXPECT(buffer_write_pcx(&wide, "w"), 3);  // no pixels

    // ---- Writing a BMP: the 3 by 2 picture with the row pitch 4, the rows from the bottom, 3 bytes each.
    EXPECT(buffer_load_pcx_2(&b, file, size, NULL, 0, 256), 0);
    EXPECT(buffer_write_bmp(&b, "pic"), 0);
    EXPECT(strcmp(fopen_name, "pic.bmp"), 0), EXPECT(strcmp(fopen_mode, "w+b"), 0), EXPECT(fcloses, 1);
    EXPECT(bmp_len, 14 + 40 + 0x400 + 2 * 3);
    EXPECT(bmp_out[0], 'B'), EXPECT(bmp_out[1], 'M'), EXPECT(*(int32_t *)(bmp_out + 2), 0x436 + 4 * 2), EXPECT(*(int32_t *)(bmp_out + 10), 0x436);
    EXPECT(*(int32_t *)(bmp_out + 14), 0x28), EXPECT(*(int32_t *)(bmp_out + 18), 3), EXPECT(*(int32_t *)(bmp_out + 22), 2);
    EXPECT(*(uint16_t *)(bmp_out + 26), 1), EXPECT(*(uint16_t *)(bmp_out + 28), 8), EXPECT(*(int32_t *)(bmp_out + 34), 6);
    EXPECT(*(int32_t *)(bmp_out + 14 + 32), 0x100), EXPECT(*(int32_t *)(bmp_out + 14 + 36), 0x100);
    EXPECT(bmp_out[14 + 40 + 4 * 5 + 2], 5);  // the table, as the Buffer has it (blue first)
    EXPECT(bmp_out[0x436], 1), EXPECT(bmp_out[0x437], 2), EXPECT(bmp_out[0x438], 3);   // the bottom row
    EXPECT(bmp_out[0x439], 7), EXPECT(bmp_out[0x43a], 7), EXPECT(bmp_out[0x43b], 7);   // the top row
    bmp_len = 0;
    EXPECT(buffer_write_bmp(&b, "nodir/x"), 1), EXPECT(bmp_len, 0);
    memset(&wide, 0, sizeof wide), buffer_ctor(&wide);
    EXPECT(buffer_write_bmp(&wide, "e"), 3), EXPECT(fcloses, 2), EXPECT(bmp_len, 0);

    // ---- Sprites.
    static struct game_sprite s, t;
    memset(&s, 0x5a, sizeof s);
    EXPECT(sprite_create_blank(&s, 20, 30, 0x1ff), 0);
    EXPECT(s.stride, 20), EXPECT(s.full_w, 20), EXPECT(s.full_h, 30), EXPECT(s.width, 1), EXPECT(s.height, 1);
    EXPECT(s.left, 0), EXPECT(s.top, 0), EXPECT(s.key, 0xff), EXPECT(s.data != NULL, 1), EXPECT(s.shared, 0x5a5a5a5a);
    free(s.data);
    memset(&s, 0, sizeof s);
    last_created[0] = 0;
    EXPECT(sprite_save(&s, "spr"), 7), EXPECT(last_created[0], 0);  // nothing was made
    static uint8_t spix[16];
    for (int i = 0; i < 16; i++) spix[i] = (uint8_t)(i * 3);
    s.data = spix, s.width = 4, s.height = 3, s.stride = 4, s.full_w = 6, s.full_h = 5, s.left = 1, s.top = 2, s.key = 9;
    EXPECT(sprite_save(&s, "spr"), 0), EXPECT(made_size, 4 * 3 + 0x2c), EXPECT(strcmp(last_created, "spr"), 0);
    EXPECT(*(int32_t *)made, 4), EXPECT(*(int32_t *)(made + 4), 3), EXPECT(*(int32_t *)(made + 8), 6), EXPECT(*(int32_t *)(made + 12), 5);
    EXPECT(*(int32_t *)(made + 16), 1), EXPECT(*(int32_t *)(made + 20), 2), EXPECT(made[24], 9), EXPECT(*(int32_t *)(made + 25), 4);
    EXPECT(made[29], 0), EXPECT(made[30], 3), EXPECT(made[29 + 11], 33), EXPECT(release_calls, 1);
    EXPECT(sprite_save(&s, "nodir/x"), 4);
    // Loaded again into a sprite that shares its pixels (they stay where they are, and the sprite no longer shares them).
    static uint8_t copy[16], saved[64];
    memcpy(saved, made, 29 + 12);
    files[n_files].name = "spr", files[n_files].data = saved, files[n_files++].size = 4 * 3 + 0x2c;
    memset(&t, 0, sizeof t);
    t.data = copy, t.shared = 1, t.width = 2, t.height = 2;
    t.name = mem_stub(4);
    sprite_bytes = 1000, frees = 0;
    EXPECT(sprite_load(&t, "spr"), 0);
    EXPECT(t.width, 4), EXPECT(t.height, 3), EXPECT(t.full_w, 6), EXPECT(t.full_h, 5), EXPECT(t.left, 1), EXPECT(t.top, 2);
    EXPECT(t.key, 9), EXPECT(t.stride, 4), EXPECT(t.shared, 0), EXPECT(t.data == copy, 1), EXPECT(t.name == NULL, 1);
    EXPECT(memcmp(copy, spix, 12), 0), EXPECT(frees, 1), EXPECT(sprite_bytes, 1000);
    // A file that isn't there: 6, and what the sprite held is let go (the game's count of sprite bytes goes down).
    memset(&t, 0, sizeof t);
    t.data = mem_stub(20), t.width = 5, t.height = 4, t.stride = 5, t.full_w = 5, t.shared = 0;
    frees = 0;
    EXPECT(sprite_load(&t, "missing"), 6);
    EXPECT(t.data == NULL, 1), EXPECT(t.width, 0), EXPECT(t.full_w, 0), EXPECT(sprite_bytes, 1000 - 20), EXPECT(frees, 1);

    // ---- The copy through Windows.
    static struct game_buffer src, dst;
    memset(&src, 0, sizeof src), memset(&dst, 0, sizeof dst);
    buffer_ctor(&src), buffer_ctor(&dst);
    EXPECT(buffer_init(&src, 8, 8, 0, NULL), 0), EXPECT(buffer_init(&dst, 12, 6, 0, NULL), 0);
    game_pal_p = NULL;
    blts = 0;
    EXPECT(buffer_copy_3(&src, &dst, 1, 2, 3, 4, 5, 6, 7, 8), 0);
    EXPECT(blts, 1), EXPECT(blt_args[0], AT(&dst, 0x64)), EXPECT(blt_args[5], AT(&src, 0x64)), EXPECT(blt_args[10], 0xcc0020);
    EXPECT(blt_args[1], 5), EXPECT(blt_args[2], 6), EXPECT(blt_args[3], 7), EXPECT(blt_args[4], 8);
    EXPECT(blt_args[6], 1), EXPECT(blt_args[7], 2), EXPECT(blt_args[8], 3), EXPECT(blt_args[9], 4);
    EXPECT(AT(&src, 0x68), 0), EXPECT(AT(&dst, 0x68), 0), EXPECT(AT(&src, 0x60), 0), EXPECT(AT(&dst, 0x60), 0);
    EXPECT(buffer_copy_3(&src, NULL, 1, 2, 3, 4, 5, 6, 7, 8), 0x10);
    // Both Buffers' colours are matched to the game's palette for copies when its serial is new.
    game_pal_p = &game_pal;
    game_pal.serial = 4321;
    dib_sets = 0;
    EXPECT(buffer_copy_3(&src, &dst, 0, 0, 8, 8, 0, 0, 12, 6), 0);
    EXPECT(dib_sets, 2), EXPECT(AT(&src, 0x4a4), 4321), EXPECT(AT(&dst, 0x4a4), 4321), EXPECT(AT(&src, 0x57c), 1), EXPECT(AT(&dst, 0x68), 0);
    EXPECT(buffer_copy_3(&src, &dst, 0, 0, 8, 8, 0, 0, 12, 6), 0), EXPECT(dib_sets, 2);
    // A source with no device context: 7, and the destination is left with a user (as the original leaves it).
    static struct game_buffer empty;
    memset(&empty, 0, sizeof empty), buffer_ctor(&empty);
    blts = 0;
    EXPECT(buffer_copy_3(&empty, &dst, 0, 0, 1, 1, 0, 0, 1, 1), 7), EXPECT(blts, 0), EXPECT(AT(&dst, 0x68), 0);
    EXPECT(buffer_copy_3(&src, &empty, 0, 0, 1, 1, 0, 0, 1, 1), 7), EXPECT(AT(&empty, 0x68), 2);  // both calls left it one user
    // By rectangles (left, top, right, bottom): the width and height are the differences; the answer is 8.
    int32_t from[4] = {1, 2, 5, 9}, to[4] = {3, 4, 10, 6};
    blts = 0;
    EXPECT(buffer_copy_4(&src, &dst, from, to), 8), EXPECT(blts, 1);
    EXPECT(blt_args[1], 3), EXPECT(blt_args[2], 4), EXPECT(blt_args[3], 7), EXPECT(blt_args[4], 2);
    EXPECT(blt_args[6], 1), EXPECT(blt_args[7], 2), EXPECT(blt_args[8], 4), EXPECT(blt_args[9], 7);
    EXPECT(blt_args[0], AT(&dst, 0x64)), EXPECT(AT(&src, 0x68), 0), EXPECT(AT(&dst, 0x68), 0);
    EXPECT(buffer_copy_4(&src, &dst, NULL, to), 3), EXPECT(buffer_copy_4(&src, &dst, from, NULL), 3), EXPECT(buffer_copy_4(&src, NULL, from, to), 8);
    EXPECT(buffer_copy_4(&empty, &dst, from, to), 8), EXPECT(blts, 1);

    // ---- The colours matched to a palette.
    memset(&b, 0, sizeof b), buffer_ctor(&b);
    EXPECT(buffer_init(&b, 4, 4, 0, NULL), 0);
    game_pal.serial = 99;
    game_pal.rgb[3][0] = 11, game_pal.rgb[3][1] = 22, game_pal.rgb[3][2] = 33;
    dib_sets = 0;
    EXPECT(buffer_sync_to_palette(&b, &game_pal), 0);
    EXPECT(dib_sets, 1), EXPECT(AT(&b, 0x4a4), 99), EXPECT(AT(&b, 0x57c), 1), EXPECT(AT(&b, 0x584) == (int32_t)(intptr_t)&game_pal, 1);
    EXPECT(((uint8_t *)&b)[0xa4 + 4 * 3], 33), EXPECT(((uint8_t *)&b)[0xa4 + 4 * 3 + 2], 11), EXPECT(AT(&b, 0x68), 0);
    EXPECT(buffer_sync_to_palette(&b, &game_pal), 0), EXPECT(dib_sets, 1);  // the same serial: nothing to do
    EXPECT(buffer_sync_to_palette(&b, NULL), 3);
    EXPECT(buffer_sync_to_palette(&empty, &game_pal), 7);

    if (failures) return 1;
    printf("engine_bufimg: ok\n");
    return 0;
}
