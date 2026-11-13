// The game's picture files and Sprite files, and the Buffer's copies through Windows (see bufimg.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define IMG_THIS __attribute__((thiscall))

int IMG_THIS buffer_load_tga(struct game_buffer *b, const char *name);
int IMG_THIS buffer_load_tga_2(struct game_buffer *b, const char *name, int a, int c, int d);
int IMG_THIS buffer_load_bmp(struct game_buffer *b, const char *name);
int IMG_THIS buffer_load_bmp_2(struct game_buffer *b, const char *name, int a, int c, int d);
int IMG_THIS buffer_load_pcx(struct game_buffer *b, const char *name, struct palette *pal, int first, int count);
int IMG_THIS buffer_load_pcx_2(struct game_buffer *b, const uint8_t *data, int size, struct palette *pal, int first, int count);
int IMG_THIS buffer_write_pcx(struct game_buffer *b, const char *name);
int IMG_THIS buffer_write_bmp(struct game_buffer *b, const char *name);
int IMG_THIS sprite_create_blank(struct game_sprite *s, int w, int h, int key);
int IMG_THIS sprite_save(struct game_sprite *s, const char *name);
int IMG_THIS sprite_load(struct game_sprite *s, const char *name);
int IMG_THIS buffer_copy_3(struct game_buffer *src, struct game_buffer *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);
int IMG_THIS buffer_copy_4(struct game_buffer *src, struct game_buffer *dst, const int32_t *from, const int32_t *to);
int IMG_THIS buffer_sync_to_palette(struct game_buffer *b, struct palette *pal);
