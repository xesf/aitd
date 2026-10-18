/*
 * picture.c - full screen pictures in ITD_RESS.PAK, in every title's layout.
 *
 * The entry size tells the layout apart:
 *   64000   pixels only (Alone in the Dark 1 documents)
 *   64768   pixels, then the palette (AITD2, Jack, Time Gate)
 *   64770   a 2 byte header, the palette, then the pixels (AITD1 title)
 *   64772   a 4 byte header, pixels, palette (AITD3, read at 0x414822)
 * AITD2 and Jack palettes are VGA levels (0..63) and are widened.
 */

#include <string.h>

#include "aitd.h"
#include "game.h"
#include "pak.h"
#include "picture.h"

enum {
    PIXELS = 64000,
    PALETTE = AITD_PALETTE_BYTES
};

int picture_load(int entry, u8 *pixels, u8 *palette)
{
    u32 size = 0;
    u8 *data = pak_load("ITD_RESS.PAK", entry, &size);
    const u8 *src_pixels = NULL;
    const u8 *src_palette = NULL;

    if (data == NULL) {
        return -1;
    }
    switch (size) {
    case PIXELS:
        src_pixels = data;
        break;
    case PIXELS + PALETTE:
        src_pixels = data;
        src_palette = data + PIXELS;
        break;
    case PIXELS + PALETTE + 2:
        src_palette = data + 2;
        src_pixels = data + 2 + PALETTE;
        break;
    default:
        if (size >= PIXELS + PALETTE + 4) {
            src_pixels = data + 4;
            src_palette = data + 4 + PIXELS;
        } else if (size >= PIXELS) {
            src_pixels = data;
        }
        break;
    }
    if (src_pixels != NULL && pixels != NULL) {
        memcpy(pixels, src_pixels, PIXELS);
    }
    if (src_palette != NULL && palette != NULL) {
        int i;

        memcpy(palette, src_palette, PALETTE);
        if (game_current()->palette_source == PALETTE_CAMERA_6BIT) {
            for (i = 0; i < PALETTE; i++) {
                palette[i] = (u8)((palette[i] * 255u) / 63u);
            }
        }
    }
    mem_free(data);
    return src_palette != NULL;
}
