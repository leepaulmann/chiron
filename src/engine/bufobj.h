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
