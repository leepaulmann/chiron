// The game's Palette class (256 colours with five colour cycles), and the
// colour conversions next to it. Reimplemented from terranx.exe 2.0's
// disassembly; the class is called Palette in OpenSMACX (GPL-3.0-or-later).
#pragma once

#include <stdint.h>

#include "game.h"

// Hue in degrees (0 up to 360, or -1 for a grey), saturation and value from 0 to 1.
struct hsv {
    double h, s, v;
};

void rgb_to_hsv(const uint8_t *rgb, struct hsv *out);
void hsv_to_rgb(const struct hsv *hsv, uint8_t *rgb);

// Palette methods. Where they refuse, they return the game's codes: 3 for
// NULL where a pointer is needed, 7 while there is no GDI palette, 16 for a
// missing table.
void *__attribute__((thiscall)) palette_assign(struct palette *p, const uint8_t *src);
int __attribute__((thiscall)) palette_get_rgbquad(struct palette *p, uint8_t *out, int start, int count);
int __attribute__((thiscall)) palette_set_rgbquad(struct palette *p, const uint8_t *src, int start, int count);
int __attribute__((thiscall)) palette_set_from_dib(struct palette *p, const uint8_t *bitmap_info);
void __attribute__((thiscall)) palette_reseed(struct palette *p);
int __attribute__((thiscall)) palette_get_pos(struct palette *p, int id);
int __attribute__((thiscall)) palette_nearest(struct palette *p, int r, int g, int b, int skip_cycles);
int __attribute__((thiscall)) palette_nearest_hsv(struct palette *p, const struct hsv *target, const struct hsv *hsv, int skip_cycles);
int __attribute__((thiscall)) palette_create_table(struct palette *p, uint8_t *table, int start, int count, int level);
int __attribute__((thiscall)) palette_nearest_in(struct palette *p, int r, int g, int b, int start, int count,
                                                 int skip_cycles);
int __attribute__((thiscall)) palette_blend_table(struct palette *p, const uint8_t *other, uint8_t *table, int start,
                                                  int count, int weight_this, int weight_other);
int __attribute__((thiscall)) palette_table_from_color(struct palette *p, int index, uint8_t *table, int start,
                                                       int count, int weight_this, int weight_color);
int __attribute__((thiscall)) palette_table_from_palette(struct palette *p, struct palette *target, uint8_t *table,
                                                         int start, int count);
int __attribute__((thiscall)) palette_table_to_dib(struct palette *p, uint8_t *bitmap_info, int start, int count);
