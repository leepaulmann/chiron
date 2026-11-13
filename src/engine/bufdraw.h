// The game's Buffer drawing onto another Buffer, through the Sprite draws (see bufdraw.c).
#pragma once

#include <stdint.h>

#include "game.h"

#define BUFDRAW_THIS __attribute__((thiscall))

int BUFDRAW_THIS buffer_draw(struct game_buffer *b, struct game_buffer *d, int key, int x, int y);
int BUFDRAW_THIS buffer_draw_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den);
int BUFDRAW_THIS buffer_draw_stub(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_stub_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_mono(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int colour);
int BUFDRAW_THIS buffer_draw_mono_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int colour, int num, int den);
int BUFDRAW_THIS buffer_draw_dest(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_unk1(struct game_buffer *b, struct game_buffer *d, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_unk1_2(struct game_buffer *b, struct game_buffer *d, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_source(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_source_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_4(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_3(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_unk2_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_dest_unk2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_multi_table_dest_2(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, const uint8_t *table);
int BUFDRAW_THIS buffer_draw_multi_table_dest(struct game_buffer *b, struct game_buffer *d, int key, int x, int y, int num, int den, const uint8_t *table);
void BUFDRAW_THIS buffer_setup_buff_sprite(struct game_buffer *b, int key);
