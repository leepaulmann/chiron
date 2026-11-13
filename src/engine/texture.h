// The game's Texture class: the polygon fills that draw the map's terrain textures (see texture.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define TEX_THIS __attribute__((thiscall))

// Sets up one edge of the polygon whose corners are in the points table of
// the fill that calls it (the scratch globals): `e` is 15 ints, the first of
// them the direction (+1 or -1) to walk round the polygon from corner `idx`.
// 1, or 0 if the edge is flat or leaves the polygon. The gouraud one also
// interpolates the shading values. (Plain C functions, not methods.)
int texture_setup_edge(int32_t *e, int idx);
int texture_setup_edge_gouraud(int32_t *e, int idx);

// The fills draw a polygon of `n` corners (pairs of x, y in `pts`, with the
// texture's own pair for each in `uv`) on the buffer, clipped to the
// buffer's clip rectangle.
void TEX_THIS texture_draw_trans(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int n, const int32_t *uv);
void TEX_THIS texture_draw_trans_2(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int big);
void TEX_THIS texture_draw_trans_3(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int big);
void TEX_THIS texture_draw_upper_threshold_trans(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int n, const int32_t *uv);
void TEX_THIS texture_draw(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int threshold);
void TEX_THIS texture_draw_2(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv);
void TEX_THIS texture_draw_trans_4(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv);
void TEX_THIS texture_draw_mask_dest(struct game_texture *t, struct game_buffer *b, const int32_t *pts, const uint8_t *mask, const uint8_t *table, int n, const int32_t *uv);
void TEX_THIS texture_draw_coast(struct game_texture *t, struct game_buffer *b, const int32_t *pts, const int32_t *z, const struct game_texture *over, const uint8_t *table, int n, const int32_t *uv);
void TEX_THIS texture_draw_trans_5(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color);
void TEX_THIS texture_draw_3(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int color);
void TEX_THIS texture_draw_trans_6(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int n, const int32_t *uv, int color);
void TEX_THIS texture_draw_upper_threshold_trans_2(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int n, const int32_t *uv, int color);
void TEX_THIS texture_draw_coast_2(struct game_texture *t, struct game_buffer *b, const int32_t *pts, const int32_t *z, const struct game_texture *over, const uint8_t *table, int n, const int32_t *uv, int color);
void TEX_THIS texture_draw_4(struct game_texture *t, struct game_buffer *b, const int32_t *pts, int32_t *z, int n, const int32_t *uv, int threshold, int color);

// Cuts the rectangle of a Buffer out as the texture's pixels (a block of the game's heap) and
// corner tables; see texture.c. The first takes the rectangle as left, top, width and height.
int TEX_THIS texture_extract_2(struct game_texture *t, struct game_buffer *b, int x, int y, int w, int h, void *pool);
int TEX_THIS texture_extract(struct game_texture *t, struct game_buffer *b, const int32_t *rect, void *unused);
