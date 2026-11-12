// The game's Font class and the Buffer's text measuring (see text.c).
#pragma once

#include <stdint.h>

#include "game.h"

// A Font wraps a GDI font. The rest of the 0x28 bytes (height, ascent, the
// face name) are only set by Font_init, which stays the original's.
struct font {
    int32_t first;   // -1 while no font is set
    uint32_t flags;  // bit 0: the font came from a file
    void *hfont;     // the GDI font
    int32_t ascent;  // 0xc
    int32_t height;  // 0x10
    int32_t f14;
    int32_t f18, f1c, f20;
    char *name;
};
_Static_assert(sizeof(struct font) == 0x28, "layout of the game's Font");

#define TEXT_THIS __attribute__((thiscall))

void *TEXT_THIS font_ctor(struct font *f);
int TEXT_THIS font_unk1(struct font *f, int a, int b, int c);
int TEXT_THIS font_width(struct font *f, const char *s);
int TEXT_THIS font_width_2(struct font *f, const char *s, int n);
const char *TEXT_THIS font_find_line_break(struct font *f, const char *s, int *width_left, int len);

int TEXT_THIS buffer_set_font(struct game_buffer *b, struct font *f0, struct font *f1, struct font *f2, struct font *f3);
void TEXT_THIS buffer_set_text_color(struct game_buffer *b, int a, int c, int d, int e);
void TEXT_THIS buffer_set_text_color2(struct game_buffer *b, int a, int c, int d, int e);
void TEXT_THIS buffer_set_text_color3(struct game_buffer *b, int a, int c, int d, int e);
void TEXT_THIS buffer_set_text_color_hyper(struct game_buffer *b, int a, int c, int d, int e);
int TEXT_THIS buffer_text_width(struct game_buffer *b, const char *s);
int TEXT_THIS buffer_text_width_2(struct game_buffer *b, const char *s, int n);
int TEXT_THIS buffer_text_height(struct game_buffer *b);
int TEXT_THIS buffer_text_line_height(struct game_buffer *b);
const char *TEXT_THIS buffer_find_line_break(struct game_buffer *b, const char *s, int *width_left, int len);
int TEXT_THIS buffer_wrap_height(struct game_buffer *b, const char *s, int width);
