// The game's Buffer class: the drawing primitives reimplemented so far (see buffer.c).
#pragma once

#include <stdint.h>

#include "game.h"

void __attribute__((thiscall)) buffer_hline(struct game_buffer *b, int x1, int x2, int y, int color);
void __attribute__((thiscall)) buffer_vline(struct game_buffer *b, int x, int y1, int y2, int color);
void __attribute__((thiscall)) buffer_dotted_hline(struct game_buffer *b, int x1, int x2, int y, int color);
void __attribute__((thiscall)) buffer_dotted_vline(struct game_buffer *b, int x, int y1, int y2, int color);
int __attribute__((thiscall)) buffer_get_pixel(struct game_buffer *b, int x, int y);
int __attribute__((thiscall)) buffer_put_pixel(struct game_buffer *b, int x, int y, int color);
int __attribute__((thiscall)) buffer_fill_rect(struct game_buffer *b, const int32_t *rect, int color);
int __attribute__((thiscall)) buffer_fill_all(struct game_buffer *b, int color);
int __attribute__((thiscall)) buffer_fill(struct game_buffer *b, int x, int y, int w, int h, int color);
void __attribute__((thiscall)) buffer_line(struct game_buffer *b, int x1, int y1, int x2, int y2, int color);
void __attribute__((thiscall)) buffer_pat_line(struct game_buffer *b, int x1, int y1, int x2, int y2, int c1, int c2, int a, int bl, int phase);
void __attribute__((thiscall)) buffer_pat_hline(struct game_buffer *b, int x1, int x2, int y, int c1, int c2, int a, int bl, int phase);
void __attribute__((thiscall)) buffer_pat_vline(struct game_buffer *b, int x, int y1, int y2, int c1, int c2, int a, int bl, int phase);
void __attribute__((thiscall)) buffer_box(struct game_buffer *b, int x1, int y1, int x2, int y2, int c1, int c2);
int __attribute__((thiscall)) buffer_box_rect(struct game_buffer *b, const int32_t *rect, int c1, int c2);
void __attribute__((thiscall)) buffer_dotted_box(struct game_buffer *b, int x1, int y1, int x2, int y2, int color);
int __attribute__((thiscall)) buffer_dotted_box_rect(struct game_buffer *b, const int32_t *rect, int color);
int __attribute__((thiscall)) buffer_copy_full(struct game_buffer *d, struct game_buffer *s, int x, int y, int sx, int sy, int w, int h);
int __attribute__((thiscall)) buffer_copy_mask(struct game_buffer *d, struct game_buffer *s, int x, int y, int sx, int sy, int w, int h, int key);
