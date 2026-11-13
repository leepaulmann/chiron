// The game's Sprite class: the drawing and table functions reimplemented so far (see sprite.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define SPRITE_THIS __attribute__((thiscall))

// Draws the sprite on a buffer at (x, y), scaled by game_sprite_scale. Pixels
// of the key colour are left alone. Returns 0, or 3 if there is no picture or
// buffer.
int SPRITE_THIS sprite_draw_3(struct game_sprite *s, struct game_buffer *b, int key, int x, int y);
// The same, with every other pixel of the sprite drawn as one colour.
int SPRITE_THIS sprite_draw_mono_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int colour);
// The same, through tables of 256 bytes: sprite colour 0 darkens the
// pixel under it by the table, any other colour is drawn as it is.
int SPRITE_THIS sprite_draw_dest(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, const uint8_t *table);
// Only sprite colour 0 counts, and it darkens by the table (no key colour).
int SPRITE_THIS sprite_draw_dest_unk1(struct game_sprite *s, struct game_buffer *b, int x, int y, const uint8_t *table);
// Every pixel of the sprite but the key colour darkens the pixel under it.
int SPRITE_THIS sprite_draw_dest_source(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, const uint8_t *table);
// The new colour is looked up from both the sprite's and the buffer's (a table of 65536).
int SPRITE_THIS sprite_draw_multi_table_dest_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, const uint8_t *table);
// Every pixel but the key colour is drawn as the table's colour for it.
int SPRITE_THIS sprite_draw_4(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, const uint8_t *table);

// Draw_4 for the pixels that are neither the key nor colour 0: the colour under them is darkened
// by the table (scaled down, the colour comes from the sprite's own pixel in some places).
int SPRITE_THIS sprite_draw_dest_unk2_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, const uint8_t *table);

// The wrappers: the same draws with the scale (num / den) given, which is put back after.
int SPRITE_THIS sprite_draw_mono(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int colour, int num, int den);
int SPRITE_THIS sprite_draw(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den, const uint8_t *table);
int SPRITE_THIS sprite_draw_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den);
int SPRITE_THIS sprite_draw_dest_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den, const uint8_t *table);
int SPRITE_THIS sprite_draw_dest_unk1_2(struct game_sprite *s, struct game_buffer *b, int x, int y, int num, int den, const uint8_t *table);
int SPRITE_THIS sprite_draw_dest_source_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den, const uint8_t *table);
int SPRITE_THIS sprite_draw_dest_unk2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den, const uint8_t *table);
int SPRITE_THIS sprite_draw_multi_table_dest(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int num, int den, const uint8_t *table);

void SPRITE_THIS sprite_fill_scaling_table(struct game_sprite *s);
// The tables for the part of a scaled sprite that is on the buffer; see sprite.c.
int SPRITE_THIS sprite_get_clipped_x_table_expand(struct game_sprite *s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6);
int SPRITE_THIS sprite_get_clipped_y_table_expand(struct game_sprite *s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6);
int SPRITE_THIS sprite_get_clipped_x_table_shrink(struct game_sprite *s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6);
int SPRITE_THIS sprite_get_clipped_y_table_shrink(struct game_sprite *s, const int32_t *orig, const int32_t *clip, int32_t *a3, int32_t *a4, int32_t *a5, int32_t *a6);
int SPRITE_THIS sprite_change_color(struct game_sprite *s, int from, int to);

// Cutting a sprite out of a Buffer, and copying one (see sprite.c); a pool is left to the original.
int SPRITE_THIS sprite_extract_2(struct game_sprite *s, struct game_buffer *b, int key, int x, int y, int w, int h, void *pool);
int SPRITE_THIS sprite_extract(struct game_sprite *s, struct game_buffer *b, int key, const int32_t *rect, void *pool);
int SPRITE_THIS sprite_copy(struct game_sprite *s, struct game_sprite *dst);

// What the draw of a sprite writes, for a check of a function that draws through one (bufdraw.c).
struct hook_writes;
extern int sprite_sweep_everything;
void sprite_writes_draw(struct hook_writes *w, const struct game_sprite *s, struct game_buffer *b, int x, int y);
void sprite_writes_wrapped(struct hook_writes *w, const struct game_sprite *s, struct game_buffer *b, int x, int y, int num, int den);

// A sprite made new (key colour 9), closed (what it held let go) and made one with a name only.
void *SPRITE_THIS sprite_ctor(struct game_sprite *s);
void SPRITE_THIS sprite_close(struct game_sprite *s);
int SPRITE_THIS sprite_init(struct game_sprite *s, const char *name, int w, int h);
