/*
 * frame.h - the window frames of Alone in the Dark 1, AITD2 and Jack, drawn
 * from their old style sprite bank (ITD_RESS frame_entry). See frame.c.
 */

#ifndef AITD_FRAME_H
#define AITD_FRAME_H

#include "aitd.h"

/* Inside of the last frame drawn: x1, y1, x2, y2. */
extern s16 g_window[4];

/* Sprite `index` of an old style bank; transparent skips colour 0. */
void old_sprite_draw(s16 x, s16 y, int index, const u8 *bank, int transparent);

/* A frame centred on x, y; clears and clips to its inside (g_window). */
void frame_draw(s16 x, s16 y, s16 width, s16 height);

#endif /* AITD_FRAME_H */
