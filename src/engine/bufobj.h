// The game's Buffer class: set-up, clipping and closing (see bufobj.c).
#pragma once

#include <stdint.h>

#include "game.h"

void *__attribute__((thiscall)) buffer_ctor(struct game_buffer *b);
void __attribute__((thiscall)) buffer_dtor(struct game_buffer *b);
void __attribute__((thiscall)) buffer_close(struct game_buffer *b);
int __attribute__((thiscall)) buffer_init(struct game_buffer *b, int w, int h, int flags, void **surface);
int __attribute__((thiscall)) buffer_resize(struct game_buffer *b, int w, int h);
int __attribute__((thiscall)) buffer_set_clip(struct game_buffer *b, const int32_t *rect);
int __attribute__((thiscall)) buffer_set_clip_2(struct game_buffer *b, int x, int y, int w, int h);

// Tiling a picture over an area, and drawing a frame with it (see bufobj.c). The tiles take the
// picture (a Buffer, or a Sprite), where in it to start, the area, and for the masked one the key
// colour; they return 7 if the Buffer has no pixels, else 0.
int __attribute__((thiscall)) buffer_tile_mask(struct game_buffer *b, struct game_buffer *src, int sx, int sy, int x, int y, int w, int h, int key);
int __attribute__((thiscall)) buffer_tile(struct game_buffer *b, struct game_buffer *src, int sx, int sy, int x, int y, int w, int h);
int __attribute__((thiscall)) buffer_tile_2(struct game_buffer *b, struct game_sprite *spr, int sx, int sy, int x, int y, int w, int h);
void __attribute__((thiscall)) buffer_box_sprite(struct game_buffer *b, const int32_t *rect, const struct game_box_frame *f);
void __attribute__((thiscall)) buffer_clear_links(struct game_buffer *b);

// The comparison of two twin Buffers a check makes (see bufobj.c): NULL if every field matches (handles and heap pointers by whether
// they are set, the two device contexts by content, the results `got` and `want`), else what differs; and the pixels of the two.
const char *buffer_twin_diff(struct game_buffer *a, struct game_buffer *b, int got, int want);
const char *buffer_pixels_diff(struct game_buffer *a, struct game_buffer *b);
