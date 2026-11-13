// The game's picture files and Sprite files, and the Buffer's copies through Windows: loading a
// Buffer from a PCX file (and the two bitmap and targa loaders, which open the file and stop),
// writing one as a PCX or BMP file, a Sprite made blank, saved and loaded, the copy of one
// Buffer's rectangle to another through StretchBlt, and the Buffer's colours matched to a palette.
// Names are OpenSMACX's (Buffer_load_pcx, Buffer_load_pcx_2, Buffer_write_pcx, Sprite_save ...).
//
// These open and map files, use the game's heap or make Windows objects, so a shadow check can't
// rewind them. As for a Buffer's set-up (bufobj.c), the game's own calls run the original in shadow
// mode and the sweep builds two Buffers (or Sprites) side by side, runs the original on one and
// Chiron's code on the other, and compares them. A loader is given the same file or the same bytes
// by both; a writer writes a file of its own for each (the sweep then reads them, compares their
// bytes and removes them; it needs CHIRON_USER_DIR, a scratch user folder, which is where
// the files go, and writes nothing without it).
//
// A Buffer drawing into a DirectDraw surface is left to the original (Chiron never makes one),
// as in bufobj.c. Everything else the original does is here, in the same order, through the
// game's own file-mapping object (the class at 0x628380), its C runtime's file calls and
// the Windows calls it makes.
//
// Where the original differs from OpenSMACX: the targa and bitmap loaders (_2) only open the file
// and give up with 8 (OpenSMACX has them load it); the PCX writer copies a header 0x80 bytes long
// of which it sets the first 0x10, the byte at 0x40 to 0x4a and leaves the rest as it finds it on
// the stack, which Chiron makes zero (the sweep leaves those bytes out when it compares files);
// it makes the file 0x80 + 2 * width * height long, whatever the coded data comes to (see write_pcx); the BMP writer writes each row `width` bytes long, not the DIB's padded
// pitch; Sprite_load frees the pixels it has and then copies the file's into the (now missing)
// pixel pointer, which only works for a shared sprite whose pointer stays; and the copy
// (Buffer_copy_3, and a sibling of it, copy_4, which takes rectangles by their corners) takes the Buffer it
// copies from as `this`.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bufobj.h"
#include "buffer.h"
#include "bufimg.h"
#include "game.h"
#include "hooks.h"
#include "palette.h"
#include "shadow.h"
#include "text.h"

#define BUFIMG (&bufimg_module)
extern struct hook_module bufimg_module;

typedef struct game_buffer buffer_t;
typedef struct game_sprite sprite_t;

// The originals, reached through their trampolines.
SHADOW_DECLARE_T(int, buffer_load_tga, (buffer_t * b, const char *name))
SHADOW_DECLARE_T(int, buffer_load_tga_2, (buffer_t * b, const char *name, int a, int c, int d))
SHADOW_DECLARE_T(int, buffer_load_bmp, (buffer_t * b, const char *name))
SHADOW_DECLARE_T(int, buffer_load_bmp_2, (buffer_t * b, const char *name, int a, int c, int d))
SHADOW_DECLARE_T(int, buffer_load_pcx, (buffer_t * b, const char *name, struct palette *pal, int first, int count))
SHADOW_DECLARE_T(int, buffer_load_pcx_2, (buffer_t * b, const uint8_t *data, int size, struct palette *pal, int first, int count))
SHADOW_DECLARE_T(int, buffer_write_pcx, (buffer_t * b, const char *name))
SHADOW_DECLARE_T(int, buffer_write_bmp, (buffer_t * b, const char *name))
SHADOW_DECLARE_T(int, sprite_create_blank, (sprite_t * s, int w, int h, int key))
SHADOW_DECLARE_T(int, sprite_save, (sprite_t * s, const char *name))
SHADOW_DECLARE_T(int, sprite_load, (sprite_t * s, const char *name))
SHADOW_DECLARE_T(int, buffer_copy_3, (buffer_t * src, buffer_t *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh))
SHADOW_DECLARE_T(int, buffer_copy_4, (buffer_t * src, buffer_t *dst, const int32_t *from, const int32_t *to))
SHADOW_DECLARE_T(int, buffer_sync_to_palette, (buffer_t * b, struct palette *pal))

static int32_t *at(void *b, int offset) { return (int32_t *)((uint8_t *)b + offset); }

enum {
    DC_NOW = 0x60,       // the device context in use, 0 when none
    MEM_DC = 0x64,       // the memory device context
    DC_COUNT = 0x68,     // how many users the device context has
    INFO = 0x7c,         // the DIB's BITMAPINFO
    PAL_SERIAL = 0x4a4,  // the serial of the palette the Buffer's colours came from
    PALETTE = 0xa4,      // 256 RGBQUADs
    PAL_FIRST = 0x580,   // the first colour a loaded picture's palette went into
    PAL_FLAG = 0x57c,    // set once the Buffer has colours from a palette
    PAL_PTR = 0x584,     // that palette
    PATH_LEN = 0x100,    // the room the original has for a name and its extension
    SRCCOPY = 0xcc0020,
};

typedef void(THISCALL *buffer_method)(void *);
// The Buffer's second virtual method, which lets go of the pixels.
static void unlock(buffer_t *b) { ((buffer_method)b->vtable[1])(b); }

// The pixels locked for the caller (the lock of a Buffer with no DirectDraw surface only counts),
// and let go.
static uint8_t *lock_pixels(buffer_t *b) {
    b->base = b->bits;
    if (b->base) b->locks++;
    return b->base;
}
static void unlock_pixels(buffer_t *b) {
    if (--b->locks <= 0) b->base = NULL, b->locks = 0;
}

// The device context for a use of the colour table: the memory one, counted (as buffer_get_hdc
// does it, the caller having let go of the pixels first), and let go again.
static void *take_dc(buffer_t *b) {
    *at(b, DC_NOW) = *at(b, MEM_DC);
    ++*at(b, DC_COUNT);
    return (void *)(intptr_t)*at(b, DC_NOW);
}
static void give_dc(buffer_t *b) {
    if (--*at(b, DC_COUNT) <= 0) *at(b, DC_NOW) = 0, *at(b, DC_COUNT) = 0;
}

// Name and extension in the room of a local the original has (a longer name would run over its
// stack; here it is cut).
static void path_set(char *buf, const char *name) { snprintf(buf, PATH_LEN, "%s", name); }
static void path_add(char *buf, const char *s) {
    size_t n = strlen(buf);
    snprintf(buf + n, PATH_LEN - n, "%s", s);
}
// Whether the name has a '.' after its first character: the scan back from its end.
static int has_extension(const char *buf) {
    const char *p = buf + strlen(buf);
    if (p == buf) return 0;
    for (;;) {
        if (*p == '.') return 1;
        if (--p == buf) return 0;
    }
}

// ---- Loading ---------------------------------------------------------------------------------

// The targa and bitmap loaders open the file (a name without an extension gets the one for the
// video mode) and stop, with 8; 3 for no name, 6 if the file isn't there. The first
// of each is the same with the three arguments the second wants given.
static int open_and_stop(const char *name, const char *const ext[3]) {
    struct game_mfile f;
    game_mfile_ctor(&f);
    if (!name) {
        game_mfile_close(&f);
        return 3;
    }
    char buf[PATH_LEN];
    buf[0] = 0;
    path_set(buf, name);
    if (!has_extension(buf)) path_add(buf, *game_video_mode == 1 ? ext[0] : *game_video_mode == 2 ? ext[1] : ext[2]);
    if (!game_mfile_open(&f, buf, 1)) {
        game_mfile_close(&f);
        return 6;
    }
    game_get_file_size(f.handle, NULL);
    game_mfile_close(&f);
    return 8;
}
int THISCALL buffer_load_tga_2(buffer_t *b, const char *name, int a, int c, int d) { return open_and_stop(name, game_tga_ext); }
int THISCALL buffer_load_tga(buffer_t *b, const char *name) { return buffer_load_tga_2(b, name, 0, 0, 0x100); }
int THISCALL buffer_load_bmp_2(buffer_t *b, const char *name, int a, int c, int d) { return open_and_stop(name, game_bmp_ext); }
int THISCALL buffer_load_bmp(buffer_t *b, const char *name) { return buffer_load_bmp_2(b, name, 0, 0, 0x100); }

// A PCX picture in memory, drawn into the Buffer (made the picture's size). `pal`, if given, takes
// the colours 10 to 245 of the picture's table; `first` and `count` say which of the picture's
// 256 colours go to the Buffer's table (from entry `first`) and `first` is added to every pixel.
// A picture beginning with the game's signature string has that and a byte after it left off first.
// Returns 3 for a picture that is not a 256-colour PCX with run-length coding (or a version before 4
// with a palette wanted), the answer of the resize if it fails, 7 if the Buffer has no pixels, else 0.
// A run that goes past the end of a row goes on into the Buffer's padding and the next row; and
// the colours are read from 0x300 bytes before the end of the data, whatever `first` is.
int THISCALL buffer_load_pcx_2(buffer_t *b, const uint8_t *data, int size, struct palette *pal, int first, int count) {
    if (b->dd || *game_ddraw) return orig_buffer_load_pcx_2(b, data, size, pal, first, count);
    if (!data) return 3;
    size_t prefix = strlen(game_pcx_prefix);
    if (!strncmp((const char *)data, game_pcx_prefix, prefix)) data += prefix + 1, size -= (int)prefix + 1;
    uint8_t head[0x80];
    memcpy(head, data, sizeof head);
    const uint8_t *src = data + 0x80;
    if (head[0] != 0x0a || head[2] != 1) return 3;
    if (head[1] < 4 && pal) return 3;
    int xmin = head[4] | head[5] << 8, ymin = head[6] | head[7] << 8, xmax = head[8] | head[9] << 8, ymax = head[10] | head[11] << 8;
    int w = xmax - xmin + 1, h = ymax - ymin + 1;
    // The picture's size becomes the Buffer's: through the owner's resize if there is one.
    int r = buffer_resize(b, w, h);
    if (r) return r;
    int bpp = head[3], planes = head[0x41];
    if ((int)((1u << (bpp & 31)) * (unsigned)planes) != 0x100) return 3;
    int line = 8 / bpp * (head[0x42] | head[0x43] << 8) * planes;
    int remaining = line * h;
    uint8_t *dst = lock_pixels(b);
    if (!dst) return 7;
    if (remaining) {
        int x = 0;
        do {
            uint8_t c = *src++;
            int run = 1;
            if ((c & 0xc0) == 0xc0) run = c & 0x3f, c = *src++;
            memset(dst, (uint8_t)(first + c), (size_t)run);
            x += run, dst += run, remaining -= run;
            if (x >= line) {
                dst += b->pitch - x;
                remaining += x - line;
                x = 0;
            }
        } while (remaining);
    }
    unlock_pixels(b);
    // The colours: from the end of the file, three bytes each, into the Buffer's own table.
    const uint8_t *p = data + size - 0x300;
    for (int i = first; i < first + count; i++, p += 3) {
        uint8_t *q = (uint8_t *)b + PALETTE + 4 * i;
        q[2] = p[0], q[1] = p[1], q[0] = p[2], q[3] = 0;
    }
    *((uint8_t *)b + PAL_FIRST) = (uint8_t)first;
    if (!pal) return 0;
    palette_set_from_dib(pal, (uint8_t *)b + INFO);
    if (b->bits) {
        if ((uint32_t)*at(b, PAL_SERIAL) != pal->serial) {
            *at(b, PAL_SERIAL) = (int32_t)pal->serial;
            palette_get_rgbquad(pal, (uint8_t *)b + PALETTE, 0, 0x100);
            if (b->base) unlock(b);
            void *dc = take_dc(b);
            if (dc) {
                game_set_dib_color_table(dc, 0, 0x100, (uint8_t *)b + PALETTE);
                give_dc(b);
            }
        }
        *at(b, PAL_FLAG) = 1;
        *at(b, PAL_PTR) = (int32_t)(intptr_t)pal;
    }
    return 0;
}

// The file of that name (its extension .pcx if it has none), opened as a view and loaded as above.
// 3 for no name, 6 if the file won't open.
int THISCALL buffer_load_pcx(buffer_t *b, const char *name, struct palette *pal, int first, int count) {
    struct game_mfile f;
    game_mfile_ctor(&f);
    if (!name) {
        game_mfile_close(&f);
        return 3;
    }
    char buf[PATH_LEN];
    buf[0] = 0;
    path_set(buf, name);
    if (!has_extension(buf)) path_add(buf, game_pcx_ext);
    if (!game_mfile_open(&f, buf, 1)) {
        game_mfile_close(&f);
        return 6;
    }
    uint32_t size = game_get_file_size(f.handle, NULL);
    int r = buffer_load_pcx_2(b, f.view, (int)size, pal, first, count);
    game_mfile_close(&f);
    return r;
}

// ---- Writing ---------------------------------------------------------------------------------

// The Buffer as a PCX file: a name with no '.' gets .pcx; 3 if the Buffer has no pixels, 4 if the
// file can't be made, else 0. The header is 0x80 bytes, of which the original sets the first 0x10
// (a 256-colour version 5 file with its size and `pitch` bytes to a row) and the bytes 0x40 to
// 0x4a; Chiron makes the rest zero. Each row is `pitch` bytes (the padding too) coded in runs of 2
// to 63 equal bytes that stop at the end of the row (the byte after the row's end is looked at once, for a
// row's last byte), and any single byte of 0x40 or more behind a count of 1; the palette follows a 0x0c
// byte, as 256 red, green, blue triples. The file is made 0x80 + 2 * width * height bytes long, which is
// less than it can come to (the view runs on into the page, as in the original), and then cut where
// the data ends.
int THISCALL buffer_write_pcx(buffer_t *b, const char *name) {
    if (b->dd || *game_ddraw) return orig_buffer_write_pcx(b, name);
    struct game_mfile f;
    game_mfile_ctor(&f);
    char buf[PATH_LEN];
    buf[0] = 0;
    path_set(buf, name);
    if (!game_rfind_char(buf, buf + strlen(buf), '.')) path_add(buf, game_pcx_write_ext);
    uint8_t *pixels = lock_pixels(b);
    if (!pixels) {
        game_mfile_close(&f);
        return 3;
    }
    int w = b->width, h = -b->neg_height;
    uint8_t *view = game_mfile_create(&f, buf, 0x80 - 2 * (b->neg_height * w), 1);
    if (!view) {
        unlock_pixels(b);
        game_mfile_close(&f);
        return 4;
    }
    unsigned pitch = (unsigned)b->pitch;
    uint8_t head[0x80] = {0};
    head[0] = 0x0a, head[1] = 5, head[2] = 1, head[3] = 8;
    head[8] = (uint8_t)(w - 1), head[9] = (uint8_t)((w - 1) >> 8);
    head[10] = (uint8_t)(0xffff - b->neg_height), head[11] = (uint8_t)((0xffff - b->neg_height) >> 8);
    head[0x41] = 1;
    head[0x42] = (uint8_t)pitch, head[0x43] = (uint8_t)(pitch >> 8);
    memcpy(view, head, sizeof head);
    uint8_t *out = view + 0x80;
    const uint8_t *row = pixels;
    for (int rows = h; rows > 0; rows--, row += pitch) {
        unsigned x = 0;
        while (x < pitch) {
            uint8_t v = row[x], count = 1;
            if (v == row[x + 1]) {
                // The byte at idx is known to match; it joins the run if it is inside the row.
                unsigned idx = x + 1;
                while (idx < pitch && count < 0x3f) {
                    count++;
                    idx = x + count;
                    if (v != row[idx]) break;
                }
            }
            if (count > 1 || v >= 0x40) *out++ = count | 0xc0;
            *out++ = v;
            x += count;
        }
    }
    *out++ = 0x0c;
    for (int i = 0; i < 256; i++) {
        const uint8_t *q = (const uint8_t *)b + PALETTE + 4 * i;
        *out++ = q[2], *out++ = q[1], *out++ = q[0];
    }
    unlock_pixels(b);
    game_mfile_finish(&f, out);
    game_mfile_close(&f);
    return 0;
}

// The Buffer as a BMP file (the name and .bmp), written through the game's fopen and fwrite:
// the file header (14 bytes, size 0x436 + pitch * height, the pixels at 0x436), the info header, the
// 256 colours, then the rows from the bottom, each `width` bytes (not the pitch). 1 if the file
// can't be opened, 3 if the Buffer has no pixels (the file is left empty), else 0.
int THISCALL buffer_write_bmp(buffer_t *b, const char *name) {
    if (b->dd || *game_ddraw) return orig_buffer_write_bmp(b, name);
    char buf[PATH_LEN];
    buf[0] = 0;
    path_set(buf, name);
    path_add(buf, game_bmp_write_ext);
    void *fp = game_env_open(buf, game_bmp_mode);
    if (!fp) return 1;
    uint8_t *base = lock_pixels(b);
    if (!base) {
        game_fclose(fp);
        return 3;
    }
    int pitch = b->pitch, w = b->width, h = -b->neg_height;
    uint8_t head[14] = {0};
    memcpy(head, game_bmp_signature, 2);
    int32_t v = 0x436 - pitch * b->neg_height;
    memcpy(head + 2, &v, 4);
    v = 0x436;
    memcpy(head + 10, &v, 4);
    int32_t info[10] = {0x28, w, h, 0, 0, w * h, 0, 0, 0x100, 0x100};
    uint16_t planes[2] = {1, 8};
    memcpy(&info[3], planes, 4);
    game_fwrite(head, 0xe, 1, fp);
    game_fwrite(info, 0x28, 1, fp);
    game_fwrite((uint8_t *)b + PALETTE, 0x400, 1, fp);
    const uint8_t *row = base - (b->neg_height + 1) * pitch;
    for (int rows = h; rows > 0; rows--, row -= pitch) game_fwrite(row, (size_t)w, 1, fp);
    unlock_pixels(b);
    game_fclose(fp);
    return 0;
}

// ---- Sprite files ----------------------------------------------------------------------------

// A sprite one pixel big, whose row is `w` long and whose full size is w by h; its key colour is
// `key`. The pixel is a block of the game's heap whose bytes are not set (the old pixels are not
// let go). 4 if the heap gave nothing, else 0.
int THISCALL sprite_create_blank(sprite_t *s, int w, int h, int key) {
    s->full_w = s->stride = w;
    s->left = s->top = 0;
    s->width = s->height = 1;
    s->full_h = h;
    s->key = (uint8_t)key;
    s->data = game_mem_get(1);
    return s->data ? 0 : 4;
}

// The sprite as a file of width * height + 0x2c bytes: width, height, full width, full height, left, top
// (4 bytes each), the key colour, the row length (4 bytes), then the pixels (row length times height). 7 for
// a sprite with no pixels, 4 if the file can't be made.
int THISCALL sprite_save(sprite_t *s, const char *name) {
    struct game_mfile f;
    game_mfile_ctor(&f);
    if (!s->data) {
        game_mfile_close(&f);
        return 7;
    }
    uint8_t *p = game_mfile_create(&f, name, s->width * s->height + 0x2c, 1);
    if (!p) {
        game_mfile_close(&f);
        return 4;
    }
    int32_t fields[6] = {s->width, s->height, s->full_w, s->full_h, s->left, s->top};
    memcpy(p, fields, sizeof fields);
    p += sizeof fields;
    *p++ = s->key;
    memcpy(p, &s->stride, 4);
    p += 4;
    memcpy(p, s->data, (size_t)s->height * (size_t)s->stride);
    game_mfile_release(&f);
    game_mfile_close(&f);
    return 0;
}

// Takes what a file made by sprite_save holds. The sprite first lets go of its pixels (unless shared, and
// the game's count of sprite bytes goes down) and its name, and is emptied; then the file is opened (6 if it
// isn't there) and its fields and pixels copied in. The pixels are copied to the pointer the sprite
// has, which is none unless it was shared (the original does the same: a non-shared sprite whose
// file is found brings the game down).
int THISCALL sprite_load(sprite_t *s, const char *name) {
    struct game_mfile f;
    game_mfile_ctor(&f);
    if (!s->shared && s->data) {
        *game_sprite_bytes -= s->height * s->width;
        if (s->data) game_free(s->data);
        s->data = NULL;
    }
    if (s->name) game_free(s->name), s->name = NULL;
    s->stride = s->width = s->height = s->full_w = s->full_h = s->left = s->top = s->shared = 0;
    const uint8_t *p = game_mfile_open_rw(&f, name, 1);
    if (!p) {
        game_mfile_close(&f);
        return 6;
    }
    int32_t fields[6];
    memcpy(fields, p, sizeof fields);
    p += sizeof fields;
    s->width = fields[0], s->height = fields[1];
    s->full_w = fields[2], s->full_h = fields[3], s->left = fields[4], s->top = fields[5];
    s->key = *p++;
    memcpy(&s->stride, p, 4);
    p += 4;
    memcpy(s->data, p, (size_t)s->stride * (size_t)s->height);
    game_mfile_release(&f);
    game_mfile_close(&f);
    return 0;
}

// ---- Copying through Windows -----------------------------------------------------------------

// A Buffer's colours matched to the palette `pal` when the Buffer has pixels and the palette's serial
// is not the one the Buffer has colours from: the Buffer's table is made from the palette and
// (on the memory device context) its DIB's colour table set. The second Buffer is the first's with
// the same colour sync done for a Buffer to copy to, as the original does it twice over.
static void sync_colours(buffer_t *b, struct palette *pal) {
    if (!b->bits || !pal) return;
    if ((uint32_t)*at(b, PAL_SERIAL) != pal->serial) {
        *at(b, PAL_SERIAL) = (int32_t)pal->serial;
        palette_get_rgbquad(pal, (uint8_t *)b + PALETTE, 0, 0x100);
        if (buffer_get_hdc(b)) {
            game_set_dib_color_table((void *)(intptr_t)*at(b, DC_NOW), 0, 0x100, (uint8_t *)b + PALETTE);
            buffer_release_hdc(b, 1);
        }
    }
    *at(b, PAL_FLAG) = 1;
    *at(b, PAL_PTR) = (int32_t)(intptr_t)pal;
}

// The rectangle (sx, sy, sw, sh) of `src` drawn into the rectangle (dx, dy, dw, dh) of `dst`, stretched or shrunk to
// fit, through the two memory device contexts. Both Buffers first have their colours matched to the game's
// palette for copies. `fail` is what comes back if either has no device context (a destination with one is left
// with a user counted, as the original leaves it); 0 otherwise.
static int stretch_copy(buffer_t *src, buffer_t *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int fail) {
    if (dst->base) unlock(dst);
    void *dst_dc = take_dc(dst);
    if (!dst_dc) return fail;
    if (src->base) unlock(src);
    void *src_dc = take_dc(src);
    if (!src_dc) {
        give_dc(dst);
        return fail;
    }
    sync_colours(src, *game_palette_copy);
    sync_colours(dst, *game_palette_copy);
    game_stretch_blt(dst_dc, dx, dy, dw, dh, src_dc, sx, sy, sw, sh, SRCCOPY);
    give_dc(dst);
    buffer_release_hdc(src, 1);
    return 0;
}

// The rectangle (sx, sy, sw, sh) of this Buffer drawn into the rectangle (dx, dy, dw, dh) of `dst` (see stretch_copy).
// 0x10 if there is no destination, 7 if either has no device context.
int THISCALL buffer_copy_3(buffer_t *src, buffer_t *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (!dst) return 0x10;
    if (dst->dd || src->dd || *game_ddraw) return orig_buffer_copy_3(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
    return stretch_copy(src, dst, sx, sy, sw, sh, dx, dy, dw, dh, 7);
}

// The same with the rectangles given by their corners (left, top, right, bottom), the one to copy from
// first. The original has it answer 8 whatever happened (3 only if a rectangle is missing), which nothing calls
// it for.
int THISCALL buffer_copy_4(buffer_t *src, buffer_t *dst, const int32_t *from, const int32_t *to) {
    if (!from || !to) return 3;
    if (!dst) return 8;
    if (dst->dd || src->dd || *game_ddraw) return orig_buffer_copy_4(src, dst, from, to);
    stretch_copy(src, dst, from[0], from[1], from[2] - from[0], from[3] - from[1], to[0], to[1], to[2] - to[0], to[3] - to[1], 8);
    return 8;
}

// The Buffer's colours made from `pal` (the same sync as the copies above, for one Buffer): 7 with no pixels,
// 3 with no palette. The colour table of the memory device context is set when the serial differs, with
// the Buffer's pixels let go first if locked.
int THISCALL buffer_sync_to_palette(buffer_t *b, struct palette *pal) {
    if (b->dd) return orig_buffer_sync_to_palette(b, pal);
    if (!b->bits) return 7;
    if (!pal) return 3;
    if ((uint32_t)*at(b, PAL_SERIAL) != pal->serial) {
        *at(b, PAL_SERIAL) = (int32_t)pal->serial;
        palette_get_rgbquad(pal, (uint8_t *)b + PALETTE, 0, 0x100);
        if (b->base) unlock(b);
        void *dc = take_dc(b);
        if (dc) {
            game_set_dib_color_table(dc, 0, 0x100, (uint8_t *)b + PALETTE);
            give_dc(b);
        }
    }
    *at(b, PAL_FLAG) = 1;
    *at(b, PAL_PTR) = (int32_t)(intptr_t)pal;
    return 0;
}

// ---- Shadow mode -----------------------------------------------------------------------------

// The game's calls run the original; the sweep checks Chiron's code on twins.
#define PASS_THROUGH(ret, name, params, args)                                       \
    static ret THISCALL shadow_##name params {                                     \
        if (!hooks_nested && !shadow_bypass()) {                                    \
            stats_##name.calls++;                                                   \
            shadow_tick(BUFIMG);                                                    \
        }                                                                           \
        return orig_##name args;                                                    \
    }

PASS_THROUGH(int, buffer_load_tga, (buffer_t * b, const char *name), (b, name))
PASS_THROUGH(int, buffer_load_tga_2, (buffer_t * b, const char *name, int a, int c, int d), (b, name, a, c, d))
PASS_THROUGH(int, buffer_load_bmp, (buffer_t * b, const char *name), (b, name))
PASS_THROUGH(int, buffer_load_bmp_2, (buffer_t * b, const char *name, int a, int c, int d), (b, name, a, c, d))
PASS_THROUGH(int, buffer_load_pcx, (buffer_t * b, const char *name, struct palette *pal, int first, int count),
             (b, name, pal, first, count))
PASS_THROUGH(int, buffer_load_pcx_2, (buffer_t * b, const uint8_t *data, int size, struct palette *pal, int first, int count),
             (b, data, size, pal, first, count))
PASS_THROUGH(int, buffer_write_pcx, (buffer_t * b, const char *name), (b, name))
PASS_THROUGH(int, buffer_write_bmp, (buffer_t * b, const char *name), (b, name))
PASS_THROUGH(int, sprite_create_blank, (sprite_t * s, int w, int h, int key), (s, w, h, key))
PASS_THROUGH(int, sprite_save, (sprite_t * s, const char *name), (s, name))
PASS_THROUGH(int, sprite_load, (sprite_t * s, const char *name), (s, name))
PASS_THROUGH(int, buffer_copy_3, (buffer_t * src, buffer_t *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh),
             (src, dst, sx, sy, sw, sh, dx, dy, dw, dh))
PASS_THROUGH(int, buffer_copy_4, (buffer_t * src, buffer_t *dst, const int32_t *from, const int32_t *to), (src, dst, from, to))
PASS_THROUGH(int, buffer_sync_to_palette, (buffer_t * b, struct palette *pal), (b, pal))

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

// The scratch user folder the writers' files go to (and nothing is written without one).
static const char *scratch_dir(void) {
    const char *u = getenv("CHIRON_USER_DIR");
    return u && *u ? u : NULL;
}

// A file of the scratch folder, whole; its size, or -1 if there is none.
static long slurp(const char *name, uint8_t **out) {
    char path[4200];
    snprintf(path, sizeof path, "%s/%s", scratch_dir(), name);
    FILE *f = fopen(path, "rb");
    *out = NULL;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc((size_t)n + 1);
    if (fread(*out, 1, (size_t)n, f) != (size_t)n) n = -1;
    fclose(f);
    return n;
}
static void remove_scratch(const char *name) {
    char path[4200];
    snprintf(path, sizeof path, "%s/%s", scratch_dir(), name);
    unlink(path);
}

// Whether the two files are alike, but for the bytes from `skip_from` to `skip_to` and `skip_from2` to `skip_to2`
// (the stack bytes the original writes into a PCX header); NULL if so, else what differs. Both are removed.
static const char *files_diff(const char *na, const char *nb, long skip_from, long skip_to, long skip_from2, long skip_to2) {
    static char msg[100];
    uint8_t *da, *db;
    long sa = slurp(na, &da), sb = slurp(nb, &db);
    const char *r = NULL;
    if (sa != sb) {
        snprintf(msg, sizeof msg, "file size %ld, original %ld", sb, sa);
        r = msg;
    } else {
        for (long i = 0; i < sa && !r; i++) {
            if ((i >= skip_from && i < skip_to) || (i >= skip_from2 && i < skip_to2)) continue;
            if (da[i] != db[i]) snprintf(msg, sizeof msg, "file byte %ld = 0x%02x, original 0x%02x", i, db[i], da[i]), r = msg;
        }
    }
    free(da), free(db);
    remove_scratch(na), remove_scratch(nb);
    return r;
}

// Two Buffers alike, made with the same pixels (runs of a few values, so the coders find runs) and colour tables.
static void twins_make(buffer_t *a, buffer_t *b, int w, int h) {
    memset(a, 0, sizeof *a), memset(b, 0, sizeof *b);
    buffer_ctor(a), buffer_ctor(b);
    buffer_init(a, w, h, 0, NULL), buffer_init(b, w, h, 0, NULL);
    static const uint8_t values[] = {0, 1, 0x3f, 0x40, 0xbf, 0xc0, 0xc1, 0xff, 5};
    int run = 0, v = 0;
    for (int y = 0; y < -a->neg_height; y++)
        for (int x = 0; x < a->pitch; x++) {
            if (!run) run = 1 + rnd_to(rnd_to(3) ? 5 : 80), v = rnd_to(4) ? values[rnd_to(9)] : rnd_to(256);
            run--;
            a->bits[y * a->pitch + x] = b->bits[y * b->pitch + x] = (uint8_t)v;
        }
    for (int i = 0; i < 0x400; i++) ((uint8_t *)a)[PALETTE + i] = ((uint8_t *)b)[PALETTE + i] = (uint8_t)rnd();
    *at(a, PAL_SERIAL) = *at(b, PAL_SERIAL) = rnd_to(4);
}
static void twins_close(buffer_t *a, buffer_t *b) { buffer_dtor(a), buffer_dtor(b); }

// The field a loaded or copied-to Buffer keeps its palette's address in, made comparable (set or not).
static void normalise(buffer_t *b) { *at(b, PAL_PTR) = *at(b, PAL_PTR) != 0; }

// Compare two twins after one call each: the results, the Buffers' fields, device contexts and pixels,
// and the palettes if given; after a difference they are made again alike. Returns non-zero then.
static int twins_check(struct hook_stats *s, buffer_t *a, buffer_t *b, int got, int want, struct palette *pa,
                       struct palette *pb, const char *what) {
    normalise(a), normalise(b);
    const char *d = buffer_twin_diff(a, b, got, want);
    if (!d) d = buffer_pixels_diff(a, b);
    if (!d && pa && memcmp(pa, pb, sizeof *pa)) d = "palette";
    if (!d) return 0;
    hooks_mismatch(s, " (%s): %s", what, d);
    int w = a->width, h = -a->neg_height;
    twins_close(a, b);
    twins_make(a, b, w > 0 ? w : 8, h > 0 ? h : 8);
    if (pa) memcpy(pb, pa, sizeof *pa);
    return 1;
}

// The game's random seed (the palette's serial is drawn from it): given to both runs alike, and left as
// the original made it.
static uint32_t seed_before;
static void seed_save(void) { seed_before = *game_random_seed; }
static uint32_t seed_second(void) {  // between the runs: the first's end, and the second gets the start
    uint32_t after = *game_random_seed;
    *game_random_seed = seed_before;
    return after;
}

// ---- The PCX loader's sweep ------------------------------------------------------------------

static uint8_t pcx_data[8192];

// A PCX picture: a header, the rows of `bpl` bytes coded in runs, the palette. Returns the length. `kind` picks
// flaws: 1 a wrong first byte, 2 not run-length coded, 3 seven bits to a pixel, 4 forty bits (a row of no bytes),
// 5 three planes.
static int build_pcx(uint8_t *out, int w, int h, int bpl, int version, int kind) {
    uint8_t *o = out;
    memset(o, 0, 0x80);
    o[0] = kind == 1 ? 0x0b : 0x0a, o[1] = (uint8_t)version, o[2] = kind == 2 ? 0 : 1;
    o[3] = kind == 3 ? 7 : kind == 4 ? 40 : 8;
    o[8] = (uint8_t)(w - 1), o[9] = (uint8_t)((w - 1) >> 8), o[10] = (uint8_t)(h - 1), o[11] = (uint8_t)((h - 1) >> 8);
    o[0x41] = kind == 5 ? 3 : 1;
    o[0x42] = (uint8_t)bpl, o[0x43] = (uint8_t)(bpl >> 8);
    o += 0x80;
    static const uint8_t values[] = {0, 1, 0x3f, 0x40, 0xbf, 0xc0, 0xc1, 0xff, 5};
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bpl) {
            uint8_t v = rnd_to(4) ? values[rnd_to(9)] : (uint8_t)rnd();
            int run = rnd_to(3) ? 1 + rnd_to(4) : 1 + rnd_to(70);
            if (run > bpl - x) run = bpl - x;
            while (run > 0) {
                int n = run > 63 ? 63 : run;
                if (n > 1 || v >= 0xc0) *o++ = (uint8_t)(0xc0 | n);
                *o++ = v;
                run -= n, x += n;
            }
        }
    }
    *o++ = 0x0c;
    for (int i = 0; i < 768; i++) *o++ = (uint8_t)rnd();
    return (int)(o - out);
}

static void random_palette(struct palette *pa, struct palette *pb) {
    memset(pa, 0, sizeof *pa), memset(pb, 0, sizeof *pb);
    for (int i = 0; i < 0x400; i++) ((uint8_t *)pa)[i] = ((uint8_t *)pb)[i] = (uint8_t)rnd();
    pa->serial = pb->serial = (uint32_t)rnd_to(3);
}

static void pcx_sweep(void) {
    static buffer_t a, b;
    static struct palette pa, pb;
    static const int sizes[][2] = {{12, 12}, {1, 1}, {7, 3}, {16, 9}, {23, 5}, {40, 20}, {5, 30}, {33, 2}};
    for (int round = 0; round < 24; round++) {
        int w = sizes[round % 8][0] + (round >= 16 ? rnd_to(5) : 0), h = sizes[round % 8][1];
        int pitch = (w + 3) & ~3, bpl = w + rnd_to(pitch - w + 1);
        if (round % 3 == 0) bpl = pitch;
        int kind = round < 8 ? 0 : rnd_to(6) == 0 ? 1 + rnd_to(5) : 0;
        int version = rnd_to(5) == 0 ? 3 : 5;
        int len = build_pcx(pcx_data + 64, w, h, bpl, version, kind);
        const uint8_t *data = pcx_data + 64;
        int size = len;
        if (rnd_to(4) == 0) {  // behind the signature the game's loader looks for
            size_t n = strlen(game_pcx_prefix);
            memmove(pcx_data + 64 + n + 1, pcx_data + 64, (size_t)len);
            memcpy(pcx_data + 64, game_pcx_prefix, n), pcx_data[64 + n] = 0;
            size = len + (int)n + 1;
        }
        // The Buffers: another size now, or the picture's own, and locked by a caller sometimes.
        int bw = rnd_to(3) ? 4 + rnd_to(30) : w, bh = rnd_to(3) ? 4 + rnd_to(20) : h;
        twins_make(&a, &b, bw, bh);
        if (rnd_to(4) == 0) a.base = a.bits, b.base = b.bits, a.locks = b.locks = 1 + rnd_to(2);
        int first = rnd_to(2) ? 0 : rnd_to(200), count = rnd_to(3) == 0 ? 256 - first : rnd_to(256 - first + 1);
        int use_pal = rnd_to(3) != 0;
        random_palette(&pa, &pb);
        if (rnd_to(2)) *at(&a, PAL_SERIAL) = *at(&b, PAL_SERIAL) = (int32_t)pa.serial;
        seed_save();
        int want = orig_buffer_load_pcx_2(&a, data, size, use_pal ? &pa : NULL, first, count);
        uint32_t after = seed_second();
        int got = buffer_load_pcx_2(&b, data, size, use_pal ? &pb : NULL, first, count);
        if (after != *game_random_seed) hooks_mismatch(&stats_buffer_load_pcx_2, " (random seed)");
        twins_check(&stats_buffer_load_pcx_2, &a, &b, got, want, use_pal ? &pa : NULL, use_pal ? &pb : NULL, "picture in memory");
        // Without data.
        if (round % 8 == 0) {
            want = orig_buffer_load_pcx_2(&a, NULL, 0, NULL, 0, 256);
            got = buffer_load_pcx_2(&b, NULL, 0, NULL, 0, 256);
            twins_check(&stats_buffer_load_pcx_2, &a, &b, got, want, NULL, NULL, "no data");
        }
        twins_close(&a, &b);
    }
    // Files of the game's, by name with and without the extension, one it doesn't have, and none.
    static const char *names[] = {"Dot0.pcx", "Dot3", "Jackal.pcx", "hof_1_1", "b13_nofile", "b13_nofile.pcx", NULL};
    for (int i = 0; i < 7; i++) {
        const char *name = names[i];
        twins_make(&a, &b, 4 + rnd_to(40), 4 + rnd_to(20));
        int use_pal = rnd_to(2);
        random_palette(&pa, &pb);
        int first = rnd_to(2) ? 0 : 10, count = rnd_to(2) ? 256 - first : 20;
        seed_save();
        int want = orig_buffer_load_pcx(&a, name, use_pal ? &pa : NULL, first, count);
        uint32_t after = seed_second();
        int got = buffer_load_pcx(&b, name, use_pal ? &pb : NULL, first, count);
        if (after != *game_random_seed) hooks_mismatch(&stats_buffer_load_pcx, " (random seed)");
        twins_check(&stats_buffer_load_pcx, &a, &b, got, want, use_pal ? &pa : NULL, use_pal ? &pb : NULL, name ? name : "no name");
        twins_close(&a, &b);
    }
}

// The targa and bitmap loaders, which only open: the answers for a name, one with and without an extension, a missing
// one and none, in each of the video modes.
static void stub_sweep(void) {
    static buffer_t a;
    memset(&a, 0, sizeof a);
    static const char *names[] = {"Dot0.pcx", "Dot0", ".pcx", "b13_nofile", "", "Jackal.pcx", NULL};
    int32_t mode = *game_video_mode;
    for (int m = 1; m <= 3; m++) {
        *game_video_mode = m;
        for (int i = 0; i < 7; i++) {
            const char *name = names[i];
            const char *shown = name ? name : "none";
            int want = orig_buffer_load_tga_2(&a, name, 0, 0, 0x100), got = buffer_load_tga_2(&a, name, 0, 0, 0x100);
            if (got != want) hooks_mismatch(&stats_buffer_load_tga_2, " (%s, mode %d): = %d, original %d", shown, m, got, want);
            want = orig_buffer_load_tga(&a, name), got = buffer_load_tga(&a, name);
            if (got != want) hooks_mismatch(&stats_buffer_load_tga, " (%s, mode %d): = %d, original %d", shown, m, got, want);
            want = orig_buffer_load_bmp_2(&a, name, 1, 2, 3), got = buffer_load_bmp_2(&a, name, 1, 2, 3);
            if (got != want) hooks_mismatch(&stats_buffer_load_bmp_2, " (%s, mode %d): = %d, original %d", shown, m, got, want);
            want = orig_buffer_load_bmp(&a, name), got = buffer_load_bmp(&a, name);
            if (got != want) hooks_mismatch(&stats_buffer_load_bmp, " (%s, mode %d): = %d, original %d", shown, m, got, want);
        }
    }
    *game_video_mode = mode;
}

// ---- The writers' sweep ----------------------------------------------------------------------

static void writers_sweep(void) {
    static buffer_t a, b;
    static const struct {
        int w, h;
    } sizes[] = {{12, 12}, {1, 1}, {7, 3}, {16, 9}, {23, 5}, {40, 20}, {5, 30}, {3, 40}, {64, 8}};
    for (int round = 0; round < 12; round++) {
        int w = sizes[round % 9].w + rnd_to(2), h = sizes[round % 9].h;
        twins_make(&a, &b, w, h);
        if (rnd_to(4) == 0) a.base = a.bits, b.base = b.bits, a.locks = b.locks = 1 + rnd_to(2);
        // A name with no extension, one with .pcx, and one with another extension.
        static const char *ext[] = {"", ".pcx", ".x1"};
        char na[40], nb[40], fa[48], fb[48];
        const char *e = ext[round % 3];
        snprintf(na, sizeof na, "b13_wa%s", e), snprintf(nb, sizeof nb, "b13_wb%s", e);
        snprintf(fa, sizeof fa, "b13_wa%s", e[0] ? e : ".pcx"), snprintf(fb, sizeof fb, "b13_wb%s", e[0] ? e : ".pcx");
        int want = orig_buffer_write_pcx(&a, na);
        int got = buffer_write_pcx(&b, nb);
        int bad = twins_check(&stats_buffer_write_pcx, &a, &b, got, want, NULL, NULL, "written");
        const char *d = files_diff(fa, fb, 0x10, 0x40, 0x4a, 0x80);
        if (d && !bad) hooks_mismatch(&stats_buffer_write_pcx, " (%dx%d): %s", w, h, d);
        // A folder that isn't there.
        want = orig_buffer_write_pcx(&a, "b13_nodir/x");
        got = buffer_write_pcx(&b, "b13_nodir/x");
        twins_check(&stats_buffer_write_pcx, &a, &b, got, want, NULL, NULL, "no folder");
        // The bitmap.
        want = orig_buffer_write_bmp(&a, "b13_wa");
        got = buffer_write_bmp(&b, "b13_wb");
        bad = twins_check(&stats_buffer_write_bmp, &a, &b, got, want, NULL, NULL, "written");
        d = files_diff("b13_wa.bmp", "b13_wb.bmp", 0, 0, 0, 0);
        if (d && !bad) hooks_mismatch(&stats_buffer_write_bmp, " (%dx%d): %s", w, h, d);
        want = orig_buffer_write_bmp(&a, "b13_nodir/x"), got = buffer_write_bmp(&b, "b13_nodir/x");
        twins_check(&stats_buffer_write_bmp, &a, &b, got, want, NULL, NULL, "no folder");
        twins_close(&a, &b);
    }
    // A Buffer with no pixels.
    memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
    buffer_ctor(&a), buffer_ctor(&b);
    int want = orig_buffer_write_pcx(&a, "b13_wa"), got = buffer_write_pcx(&b, "b13_wb");
    twins_check(&stats_buffer_write_pcx, &a, &b, got, want, NULL, NULL, "no pixels");
    remove_scratch("b13_wa.pcx"), remove_scratch("b13_wb.pcx");
    want = orig_buffer_write_bmp(&a, "b13_wa"), got = buffer_write_bmp(&b, "b13_wb");
    twins_check(&stats_buffer_write_bmp, &a, &b, got, want, NULL, NULL, "no pixels");
    remove_scratch("b13_wa.bmp"), remove_scratch("b13_wb.bmp");
    buffer_dtor(&a), buffer_dtor(&b);
}

// ---- The Sprite functions' sweep -------------------------------------------------------------

// Two Sprites alike but for where their pixels and name are (compared by whether set; the pixels by content for
// `size` bytes if asked): NULL, or what differs.
static const char *sprite_diff(const sprite_t *a, const sprite_t *b, size_t size) {
    static char msg[80];
    for (int o = 0; o < (int)sizeof *a; o += 4) {
        int32_t x = *(const int32_t *)((const uint8_t *)a + o), y = *(const int32_t *)((const uint8_t *)b + o);
        if (o == 8) {
            if ((uint8_t)x != (uint8_t)y) return "key colour";
            continue;
        }
        if (o <= 4 ? !x != !y : x != y) {
            snprintf(msg, sizeof msg, "field 0x%x = %d, original %d", o, y, x);
            return msg;
        }
    }
    if (size && a->data && b->data && memcmp(a->data, b->data, size)) return "pixels";
    return NULL;
}

// A Sprite w by h of its own pixels (random), as the game keeps one: its pixels from the heap.
static void sprite_make(sprite_t *a, sprite_t *b, int w, int h, int stride, int shared) {
    memset(a, 0, sizeof *a), memset(b, 0, sizeof *b);
    size_t n = (size_t)stride * (size_t)h + 8;
    a->data = game_mem_get(n), b->data = game_mem_get(n);
    for (size_t i = 0; i < n; i++) a->data[i] = b->data[i] = (uint8_t)rnd();
    a->stride = b->stride = stride, a->width = b->width = w, a->height = b->height = h;
    a->full_w = b->full_w = w + rnd_to(3), a->full_h = b->full_h = h + rnd_to(3);
    a->left = b->left = rnd_to(3), a->top = b->top = rnd_to(3);
    a->key = b->key = (uint8_t)rnd_to(256), a->shared = b->shared = shared;
}

static void sprite_files_sweep(void) {
    static sprite_t a, b;
    int32_t bytes = *game_sprite_bytes;
    // A blank one, made over a sprite with anything in it.
    for (int round = 0; round < 6; round++) {
        memset(&a, 0xa5, sizeof a), memset(&b, 0xa5, sizeof b);
        int w = rnd_to(300), h = rnd_to(300), key = rnd_to(300) - 20;
        int want = orig_sprite_create_blank(&a, w, h, key), got = sprite_create_blank(&b, w, h, key);
        const char *d = got != want ? "result" : sprite_diff(&a, &b, 0);
        if (d) hooks_mismatch(&stats_sprite_create_blank, " (%d, %d, key %d): %s", w, h, key, d);
        game_free(a.data), game_free(b.data);
    }
    if (!scratch_dir()) {
        *game_sprite_bytes = bytes;
        return;
    }
    // Saved: the files are alike (and 7 for no pixels, 4 for a folder that isn't there).
    for (int round = 0; round < 8; round++) {
        int w = 1 + rnd_to(20), h = 1 + rnd_to(12), stride = round & 1 ? w : w + (h < 8 ? 1 : 0);
        sprite_make(&a, &b, w, h, stride, 0);
        int want = orig_sprite_save(&a, "b13_sa"), got = sprite_save(&b, "b13_sb");
        const char *d = got != want ? "result" : sprite_diff(&a, &b, (size_t)h * (size_t)stride);
        if (!d) d = files_diff("b13_sa", "b13_sb", 0, 0, 0, 0);
        else files_diff("b13_sa", "b13_sb", 0, 0, 0, 0);
        if (d) hooks_mismatch(&stats_sprite_save, " (%dx%d, row %d): %s", w, h, stride, d);
        want = orig_sprite_save(&a, "b13_nodir/x"), got = sprite_save(&b, "b13_nodir/x");
        if (got != want) hooks_mismatch(&stats_sprite_save, " (no folder): = %d, original %d", got, want);
        game_free(a.data), game_free(b.data);
        if (round == 7) {
            memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
            want = orig_sprite_save(&a, "b13_sa"), got = sprite_save(&b, "b13_sb");
            if (got != want) hooks_mismatch(&stats_sprite_save, " (no pixels): = %d, original %d", got, want);
        }
    }
    // Loaded: a file by sprite_save, into sprites that share their pixels (the other kind the game's code brings down), and a
    // file that isn't there, into sprites of their own pixels and a name.
    for (int round = 0; round < 8; round++) {
        int w = 1 + rnd_to(20), h = 1 + rnd_to(12);
        sprite_make(&a, &b, w, h, w, 0);
        if (sprite_save(&a, "b13_sl")) hooks_mismatch(&stats_sprite_save, " (for a load): failed");
        sprite_t la, lb;
        static uint8_t pa[512], pb[512];
        memset(pa, 0x77, sizeof pa), memset(pb, 0x77, sizeof pb);
        memset(&la, 0, sizeof la), memset(&lb, 0, sizeof lb);
        la.data = pa, lb.data = pb, la.shared = lb.shared = 1 + rnd_to(3);
        la.name = game_mem_get(6), lb.name = game_mem_get(6), strcpy(la.name, "named"), strcpy(lb.name, "named");
        la.width = lb.width = 3, la.height = lb.height = 4, la.stride = lb.stride = 3;
        int32_t before = *game_sprite_bytes;
        int want = orig_sprite_load(&la, "b13_sl");
        int32_t after = *game_sprite_bytes;
        *game_sprite_bytes = before;
        int got = sprite_load(&lb, "b13_sl");
        const char *d = got != want ? "result" : after != *game_sprite_bytes ? "sprite bytes" : sprite_diff(&la, &lb, 0);
        if (!d && memcmp(pa, pb, sizeof pa)) d = "pixels";
        if (!d && want == 0 && (la.stride != w || memcmp(pa, a.data, (size_t)w * (size_t)h))) d = "not what was saved";
        if (d) hooks_mismatch(&stats_sprite_load, " (%dx%d): %s", w, h, d);
        remove_scratch("b13_sl");
        game_free(a.data), game_free(b.data);
        // The file is gone now: 6, and the sprite is let go of.
        sprite_make(&la, &lb, w, h, w, 0);
        la.name = game_mem_get(6), lb.name = game_mem_get(6), strcpy(la.name, "named"), strcpy(lb.name, "named");
        if (round & 1) game_free(la.data), game_free(lb.data), la.data = pa, lb.data = pb, la.shared = lb.shared = 1;
        before = *game_sprite_bytes;
        want = orig_sprite_load(&la, "b13_sl");
        after = *game_sprite_bytes;
        *game_sprite_bytes = before;
        got = sprite_load(&lb, "b13_sl");
        d = got != want ? "result" : after != *game_sprite_bytes ? "sprite bytes" : sprite_diff(&la, &lb, 0);
        if (d) hooks_mismatch(&stats_sprite_load, " (no file): %s", d);
        *game_sprite_bytes = bytes;
    }
    *game_sprite_bytes = bytes;
}

// ---- The copy's sweep ------------------------------------------------------------------------

// The rectangles of a copy: they reach past the edges of the Buffers now and then, and are of any size (stretched or
// shrunk). The first is in the source, the second in the destination.
static void random_rects(int a[8], int sw, int sh, int dw, int dh) {
    a[0] = rnd_to(sw + 6) - 3, a[1] = rnd_to(sh + 6) - 3, a[2] = 1 + rnd_to(sw + 4), a[3] = 1 + rnd_to(sh + 4);
    a[4] = rnd_to(dw + 6) - 3, a[5] = rnd_to(dh + 6) - 3, a[6] = 1 + rnd_to(dw + 4), a[7] = 1 + rnd_to(dh + 4);
}

static void copy_sweep(void) {
    static buffer_t sa, sb, da, db;
    static struct palette pal;
    struct palette *saved = *game_palette_copy;
    for (int round = 0; round < 16; round++) {
        int sw = 4 + rnd_to(36), sh = 4 + rnd_to(26), dw = 4 + rnd_to(36), dh = 4 + rnd_to(26);
        twins_make(&sa, &sb, sw, sh);
        twins_make(&da, &db, dw, dh);
        // The game's palette for copies: none, one the Buffers already have colours from, or a new one.
        for (int i = 0; i < 0x400; i++) ((uint8_t *)&pal)[i] = (uint8_t)rnd();
        int kind = rnd_to(4);
        pal.serial = kind == 1 ? (uint32_t)*at(&sa, PAL_SERIAL) : (uint32_t)(100 + rnd_to(3));
        *game_palette_copy = kind == 0 ? NULL : &pal;
        if (rnd_to(5) == 0) sa.base = sa.bits, sb.base = sb.bits, sa.locks = sb.locks = 1;
        if (rnd_to(5) == 0) da.base = da.bits, db.base = db.bits, da.locks = db.locks = 1;
        if (rnd_to(6) == 0) *at(&da, DC_COUNT) = *at(&db, DC_COUNT) = 2;
        int a[8];
        random_rects(a, sw, sh, dw, dh);
        if (rnd_to(4) == 0) a[0] = a[1] = 0, a[2] = sw, a[3] = sh;
        if (rnd_to(4) == 0) a[4] = a[5] = 0, a[6] = dw, a[7] = dh;
        if (rnd_to(5) == 0) a[6] = a[2], a[7] = a[3];
        int same = rnd_to(8) == 0;
        buffer_t *dst_a = same ? &sa : &da, *dst_b = same ? &sb : &db;
        int want = orig_buffer_copy_3(&sa, dst_a, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        int got = buffer_copy_3(&sb, dst_b, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        char what[100];
        snprintf(what, sizeof what, "%d,%d %dx%d to %d,%d %dx%d palette %d%s", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], kind,
                 same ? ", itself" : "");
        int bad = twins_check(&stats_buffer_copy_3, &sa, &sb, got, want, NULL, NULL, what);
        if (!same && !bad) twins_check(&stats_buffer_copy_3, &da, &db, got, want, NULL, NULL, what);
        else if (!same) twins_close(&da, &db), twins_make(&da, &db, dw, dh);
        // The same by rectangles (one of them missing now and then).
        if (!same && !bad) {
            random_rects(a, sw, sh, dw, dh);
            snprintf(what, sizeof what, "%d,%d %dx%d to %d,%d %dx%d palette %d", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], kind);
            int32_t from[4] = {a[0], a[1], a[0] + a[2], a[1] + a[3]}, to[4] = {a[4], a[5], a[4] + a[6], a[5] + a[7]};
            const int32_t *f = rnd_to(10) == 0 ? NULL : from, *t = rnd_to(10) == 0 ? NULL : to;
            want = orig_buffer_copy_4(&sa, &da, f, t), got = buffer_copy_4(&sb, &db, f, t);
            bad = twins_check(&stats_buffer_copy_4, &sa, &sb, got, want, NULL, NULL, what);
            if (!bad) twins_check(&stats_buffer_copy_4, &da, &db, got, want, NULL, NULL, what);
            else twins_close(&da, &db), twins_make(&da, &db, dw, dh);
        }
        // No destination, and a Buffer with no pixels (as the source and as the destination).
        want = orig_buffer_copy_3(&sa, NULL, 0, 0, 4, 4, 0, 0, 4, 4), got = buffer_copy_3(&sb, NULL, 0, 0, 4, 4, 0, 0, 4, 4);
        twins_check(&stats_buffer_copy_3, &sa, &sb, got, want, NULL, NULL, "no destination");
        if (round % 4 == 0) {
            static buffer_t ea, eb;
            memset(&ea, 0, sizeof ea), memset(&eb, 0, sizeof eb);
            buffer_ctor(&ea), buffer_ctor(&eb);
            want = orig_buffer_copy_3(&ea, &da, 0, 0, 4, 4, 0, 0, 4, 4), got = buffer_copy_3(&eb, &db, 0, 0, 4, 4, 0, 0, 4, 4);
            twins_check(&stats_buffer_copy_3, &da, &db, got, want, NULL, NULL, "empty source");
            want = orig_buffer_copy_3(&sa, &ea, 0, 0, 4, 4, 0, 0, 4, 4), got = buffer_copy_3(&sb, &eb, 0, 0, 4, 4, 0, 0, 4, 4);
            twins_check(&stats_buffer_copy_3, &sa, &sb, got, want, NULL, NULL, "empty destination");
            want = orig_buffer_copy_4(&ea, &da, (int32_t[4]){0, 0, 4, 4}, (int32_t[4]){0, 0, 4, 4});
            got = buffer_copy_4(&eb, &db, (int32_t[4]){0, 0, 4, 4}, (int32_t[4]){0, 0, 4, 4});
            twins_check(&stats_buffer_copy_4, &da, &db, got, want, NULL, NULL, "empty source");
            want = orig_buffer_copy_4(&sa, NULL, (int32_t[4]){0, 0, 4, 4}, (int32_t[4]){0, 0, 4, 4});
            got = buffer_copy_4(&sb, NULL, (int32_t[4]){0, 0, 4, 4}, (int32_t[4]){0, 0, 4, 4});
            twins_check(&stats_buffer_copy_4, &sa, &sb, got, want, NULL, NULL, "no destination");
            buffer_dtor(&ea), buffer_dtor(&eb);
        }
        twins_close(&sa, &sb), twins_close(&da, &db);
    }
    *game_palette_copy = saved;
}

// The colour sync of one Buffer.
static void sync_sweep(void) {
    static buffer_t a, b;
    static struct palette pa, pb;
    for (int round = 0; round < 16; round++) {
        twins_make(&a, &b, 4 + rnd_to(20), 4 + rnd_to(20));
        for (int i = 0; i < 0x400; i++) ((uint8_t *)&pa)[i] = ((uint8_t *)&pb)[i] = (uint8_t)rnd();
        pa.serial = pb.serial = rnd_to(2) ? (uint32_t)*at(&a, PAL_SERIAL) : (uint32_t)(100 + rnd_to(3));
        if (rnd_to(4) == 0) a.base = a.bits, b.base = b.bits, a.locks = b.locks = 1 + rnd_to(2);
        if (rnd_to(5) == 0) *at(&a, DC_COUNT) = *at(&b, DC_COUNT) = 1;
        int use = round != 15;
        int want = orig_buffer_sync_to_palette(&a, use ? &pa : NULL), got = buffer_sync_to_palette(&b, use ? &pb : NULL);
        twins_check(&stats_buffer_sync_to_palette, &a, &b, got, want, NULL, NULL, use ? "palette" : "no palette");
        twins_close(&a, &b);
    }
    // A Buffer with no pixels.
    static buffer_t ea, eb;
    memset(&ea, 0, sizeof ea), memset(&eb, 0, sizeof eb);
    buffer_ctor(&ea), buffer_ctor(&eb);
    int want = orig_buffer_sync_to_palette(&ea, &pa), got = buffer_sync_to_palette(&eb, &pa);
    twins_check(&stats_buffer_sync_to_palette, &ea, &eb, got, want, NULL, NULL, "no pixels");
    buffer_dtor(&ea), buffer_dtor(&eb);
}

static void bufimg_sweep(void) {
    if (*game_ddraw || !hooks_dc_same) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x2545f491u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    uint32_t seed = *game_random_seed;
    pcx_sweep();
    stub_sweep();
    if (scratch_dir()) writers_sweep();
    sprite_files_sweep();
    copy_sweep();
    sync_sweep();
    *game_random_seed = seed;
    hooks_sweeping = sweeping;
}

static const struct hook bufimg_hooks[] = {
    HOOK(0x5d7b20, buffer_load_tga, 0x8b, 0x44, 0x24, 0x04, 0x68, 0x00, 0x01, 0x00, 0x00),  // mov eax, [esp+4]; push 0x100
    HOOK(0x5d7b40, buffer_load_tga_2, 0x6a, 0xff, 0x68, 0x9d, 0x2b, 0x66, 0x00),             // push -1; push handler
    HOOK(0x5d7c80, buffer_load_bmp, 0x8b, 0x44, 0x24, 0x04, 0x68, 0x00, 0x01, 0x00, 0x00),
    HOOK(0x5d7ca0, buffer_load_bmp_2, 0x6a, 0xff, 0x68, 0xb2, 0x2b, 0x66, 0x00),
    HOOK(0x5d7de0, buffer_load_pcx, 0x6a, 0xff, 0x68, 0xc7, 0x2b, 0x66, 0x00),
    HOOK(0x5e2690, buffer_load_pcx_2, 0x81, 0xec, 0xfc, 0x00, 0x00, 0x00, 0x53, 0x55),  // sub esp, 0xfc; push ebx, ebp
    HOOK(0x5df590, buffer_write_pcx, 0x6a, 0xff, 0x68, 0x07, 0x2c, 0x66, 0x00),
    HOOK(0x5df910, buffer_write_bmp, 0x81, 0xec, 0xf8, 0x00, 0x00, 0x00),  // sub esp, 0xf8
    HOOK(0x5eaf3f, sprite_create_blank, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x8b, 0x54, 0x24, 0x0c),
    HOOK(0x5eaf8f, sprite_save, 0x6a, 0xff, 0x68, 0x38, 0x2c, 0x66, 0x00),
    HOOK(0x5eb0bf, sprite_load, 0x6a, 0xff, 0x68, 0x4a, 0x2c, 0x66, 0x00),
    HOOK(0x5d9610, buffer_copy_3, 0x51, 0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x14),  // push ecx, ebx, ebp, esi; mov esi, [esp+0x14]
    HOOK(0x5d98f0, buffer_copy_4, 0x83, 0xec, 0x20, 0x55, 0x57, 0x8b, 0xf9),  // sub esp, 0x20; push ebp, edi; mov edi, ecx
    HOOK(0x5de8f0, buffer_sync_to_palette, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x54),     // push esi; mov esi, ecx; mov eax, [esi+0x54]
};
struct hook_module bufimg_module = {"bufimg", bufimg_hooks, HOOK_COUNT(bufimg_hooks), bufimg_sweep, NULL, -1};
