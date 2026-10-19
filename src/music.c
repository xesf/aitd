/*
 * music.c - the AdLib music of Alone in the Dark 1, AITD2 and Jack.
 */

#include <stdio.h>

#include "aitd.h"
#include "music.h"

void music_adlib_play(const u8 *data, u32 size)
{
    static int reported;

    (void)data;
    (void)size;
    if (!reported) {
        reported = 1;
        fprintf(stderr, "music: AdLib tunes not rebuilt yet\n");
    }
}

void music_adlib_stop(void)
{
}
