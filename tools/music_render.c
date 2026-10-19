/*
 * music_render - renders an AdLib tune (one LISTMUS.PAK entry) to a WAV
 * file with the engine's player and OPL2 synthesiser.
 *
 *   music_render <data dir> <tune> <seconds> <out.wav>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "music.h"
#include "pak.h"
#include "platform.h"

static void put32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv)
{
    enum { RATE = 44100 };
    u32 size = 0;
    u8 *tune;
    int seconds, frames, i;
    short *pcm;
    FILE *f;

    if (argc != 5) {
        fprintf(stderr, "usage: music_render <data dir> <tune> <seconds> <out.wav>\n");
        return 1;
    }
    setenv("AITD_DATA_DIR", argv[1], 1);
    tune = pak_load("LISTMUS.PAK", atoi(argv[2]), &size);
    if (tune == NULL) {
        fprintf(stderr, "no tune %s\n", argv[2]);
        return 1;
    }
    seconds = atoi(argv[3]);
    frames = seconds * RATE;
    pcm = calloc((size_t)frames, sizeof(short));
    music_adlib_render(pcm, 1, RATE);   /* sets the chip up at the rate */
    music_adlib_play(tune, size);
    for (i = 0; i < frames; i += 1024) {
        music_adlib_render(pcm + i, frames - i < 1024 ? frames - i : 1024, RATE);
    }
    f = fopen(argv[4], "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, RATE * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, frames * 2); fwrite(pcm, 2, (size_t)frames, f);
    fclose(f);
    return 0;
}
