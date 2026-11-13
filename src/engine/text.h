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

// Drawing text into the Buffer (rect is left, top, right, bottom).
typedef struct game_buffer game_buffer_t;
void *TEXT_THIS buffer_get_hdc(game_buffer_t *b);
void TEXT_THIS buffer_release_hdc(game_buffer_t *b, int n);
int TEXT_THIS buffer_write_raw_l(game_buffer_t *b, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_multi_font_raw_l(game_buffer_t *b, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_l(game_buffer_t *b, struct font *f, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_l_2(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_write_l_3(game_buffer_t *b, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_l_4(game_buffer_t *b, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_write_cent_l(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w, int n);
int TEXT_THIS buffer_write_cent_l_2(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_write_cent_l_3(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w, int h, int n);
int TEXT_THIS buffer_write_cent_l_4(game_buffer_t *b, struct font *f, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_cent_l_5(game_buffer_t *b, const char *s, int x, int y, int w, int n);
int TEXT_THIS buffer_write_cent_l_6(game_buffer_t *b, const char *s, int x, int y, int w, int h, int n);
int TEXT_THIS buffer_write_cent_l_7(game_buffer_t *b, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_write_cent_l_8(game_buffer_t *b, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_right_l(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w, int n);
int TEXT_THIS buffer_write_right_l_2(game_buffer_t *b, struct font *f, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_right_l_3(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_write_right_l_4(game_buffer_t *b, const char *s, int x, int y, int w, int n);
int TEXT_THIS buffer_write_right_l_5(game_buffer_t *b, const char *s, int x, int y, int n);
int TEXT_THIS buffer_write_right_l_6(game_buffer_t *b, const char *s, const int32_t *rect, int n);
int TEXT_THIS buffer_wrap(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect);
int TEXT_THIS buffer_wrap_2(game_buffer_t *b, const char *s, const int32_t *rect);
int TEXT_THIS buffer_wrap_3(game_buffer_t *b, const char *s, int x, int y, int w);
int TEXT_THIS buffer_wrap_cent(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w);
int TEXT_THIS buffer_wrap_cent_2(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w, int step);
int TEXT_THIS buffer_wrap_cent_3(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect);
int TEXT_THIS buffer_wrap_cent_4(game_buffer_t *b, const char *s, int x, int y, int w);
int TEXT_THIS buffer_wrap_cent_5(game_buffer_t *b, const char *s, int x, int y, int w, int step);
int TEXT_THIS buffer_wrap_cent_6(game_buffer_t *b, const char *s, const int32_t *rect);
int TEXT_THIS buffer_wrap_right(game_buffer_t *b, struct font *f, const char *s, int x, int y, int w);
int TEXT_THIS buffer_wrap_right_2(game_buffer_t *b, struct font *f, const char *s, const int32_t *rect);
int TEXT_THIS buffer_wrap_right_3(game_buffer_t *b, const char *s, int x, int y, int w);
int TEXT_THIS buffer_wrap_right_4(game_buffer_t *b, const char *s, const int32_t *rect);

// "Flying" text, laid out over several calls into a global queue and flushed, and lists of
// strings drawn with it. A list is the game's circular list class: a head, the node it stands
// at (each node has a key, an entry and the next node), how many entries there are and which
// one it stands at. An entry has a text and a kind.
struct list_entry {
    int32_t f0;
    const char *text;
    int32_t kind;
};
struct list_node {
    int32_t f0;
    int32_t key;
    struct list_entry *entry;
    struct list_node *next;
};
struct str_list {
    int32_t f0, f4;
    void *head;
    struct list_node *cur;
    int32_t count, index;
};
_Static_assert(sizeof(struct str_list) == 0x18, "layout of the game's list");

int TEXT_THIS buffer_wrap_height_flying(game_buffer_t *b, const char *s, int width);
int TEXT_THIS buffer_wrap_height_flying_2(game_buffer_t *b, const char *s);
int TEXT_THIS buffer_wrap_height_flying_flush(game_buffer_t *b);
int TEXT_THIS buffer_wrap_flying(game_buffer_t *b, const char *s, int x, int y, int width);
int TEXT_THIS buffer_wrap_flying_2(game_buffer_t *b, const char *s);
int TEXT_THIS buffer_wrap_flying_flush(game_buffer_t *b);
int TEXT_THIS buffer_wrap_cent_flying(game_buffer_t *b, const char *s, int x, int y, int width);
int TEXT_THIS buffer_wrap_cent_flying_2(game_buffer_t *b, const char *s);
int TEXT_THIS buffer_wrap_cent_flying_flush(game_buffer_t *b);
int TEXT_THIS buffer_write_strings_height(game_buffer_t *b, struct str_list *list, int width, int flags);
int TEXT_THIS buffer_write_strings(game_buffer_t *b, struct str_list *list, int x, int y, int width, int flags);
