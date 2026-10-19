/*
 * music.h - the AdLib music of Alone in the Dark 1, AITD2 and Jack, see
 * music.c.
 */

#ifndef AITD_MUSIC_H
#define AITD_MUSIC_H

#include "aitd.h"

/* Starts a LISTMUS.PAK tune (the data is copied); stops the previous one. */
void music_adlib_play(const u8 *data, u32 size);
void music_adlib_stop(void);
/* Mono 16 bit output, stepping the driver as time passes (audio thread). */
void music_adlib_render(short *out, int frames, int rate);

#endif /* AITD_MUSIC_H */
