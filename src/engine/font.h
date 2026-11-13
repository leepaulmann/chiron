// The game's Font class: setting a Font up, closing it and the font class (see font.c).
// Measuring text with a Font is in text.c.
#pragma once

#include "text.h"

void *TEXT_THIS font_ctor_2(struct font *f, const char *face, int size, int flags);
void TEXT_THIS font_dtor(struct font *f);
int TEXT_THIS font_init(struct font *f, const char *face, int size, int flags);
int TEXT_THIS font_init_2(struct font *f, const char *file, const char *face, int size, int flags);
void TEXT_THIS font_close(struct font *f);
// The font class's own set-up: a device context to measure in, and the default font. The
// game calls both with the C calling convention, so they aren't methods.
int font_init_font_class(struct font *default_font);
void font_close_font_class(void);
