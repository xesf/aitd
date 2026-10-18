/*
 * palette.h - palette upload and fading, see palette.c.
 */

#ifndef AITD_PALETTE_H
#define AITD_PALETTE_H

#include "aitd.h"

void palette_set_range(const u8 *palette, int first, int count); /* 0x402d67 */
void palette_blackout(void);                                     /* 0x40b0f8 */
void palette_fade(s16 ignored, s16 first_index);                 /* 0x40af57 */
void palette_fade_out(s16 ignored, s16 first_index);             /* 0x40b042 */

extern s16 g_fade_state;  /* 0x43505e */

#endif /* AITD_PALETTE_H */
