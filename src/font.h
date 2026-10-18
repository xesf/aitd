/*
 * font.h - bitmap font rendering, see font.c for the blob layout.
 */

#ifndef AITD_FONT_H
#define AITD_FONT_H

#include "aitd.h"

/* Installs the blob parsed by font_set_data (0x420eea) plus the state the
 * blitter reads: the remap table (0x435220) and the spacing word (0x435226). */
void font_bind(const void *blob, const u8 *colour_table, u16 spacing);

/* 0x420c78 */
void font_draw_string(u8 *target, int pitch, int height,
                      int x, int y, const char *text);

int font_string_width(const char *text);
int font_glyph_advance(u8 ch);
int font_height(void);

/* 0x4029cb */
void font_set_colour(u8 *table, u8 colour);

#endif /* AITD_FONT_H */
