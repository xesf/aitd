/*
 * cutscene.c - full screen animations and pictures started by life scripts.
 *
 *   play_cutscene   0x4025a3  AN<n>.PAK, opcode 92
 *   cutscene_delta  0x4028ac  frame delta decoder
 *   show_picture    0x405db1  opcode 71, an ITD_RESS picture
 *
 * AN<n>.PAK entry 0 is the palette (0x300 bytes), the first frame (64000)
 * and at +0xfd00 a u16 entry count. Every later entry starts with a u32
 * size: 0xfa00 and up means a raw frame follows, anything smaller a delta
 * stream applied over the previous frame. Frames advance every 9/100 s.
 * Opcode 97 fills a table of (frame, sample) pairs played as frames show.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "game.h"
#include "mux.h"
#include "smacker.h"
#include "anim.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "picture.h"
#include "raster.h"
#include "subsystems.h"

enum cutscene_flag {
    CUTSCENE_FADE_IN = 1,     /* fade out what is up, fade the first frame in */
    CUTSCENE_FADE_OUT = 2,    /* fade out at the end, full redraw after */
    CUTSCENE_BLACKOUT = 4,    /* black palette before the first frame */
    CUTSCENE_CLEAR_PAL = 8    /* see the end of play_cutscene */
};

enum {
    FRAME_BYTES = 64000,
    FRAME_RAW_MIN = 0xfa00,
    FIRST_COUNT_OFFSET = 0xfd00,
    FRAME_TICKS = 9            /* 1/100 s */
};

s16 g_cutscene_samples[30 * 2];   /* 0x44232c, (frame, sample) pairs */
s16 g_cutscene_sample_count;      /* 0x444ff4 */

/* 0x4028ac. Opcodes: 0 end; 1 one byte; 2 one byte twice; 3 u8 count and
 * a byte; anything else u16 count and a byte. A zero byte skips instead of
 * writing. */
enum delta_op {
    DELTA_END = 0,
    DELTA_BYTE = 1,
    DELTA_WORD = 2,                 /* one byte written twice */
    DELTA_RUN = 3                   /* u8 count; larger opcodes use a u16 count */
};

static void cutscene_delta(const u8 *src, u8 *dst)
{
    for (;;) {
        u8 op = *src++;
        u32 count;
        u8 value;

        switch (op) {
        case DELTA_END:
            return;
        case DELTA_BYTE:
            value = *src++;
            if (value) {
                *dst = value;
            }
            dst += 1;
            continue;
        case DELTA_WORD:
            value = *src++;
            if (value) {
                dst[0] = value;
                dst[1] = value;
            }
            dst += 2;
            continue;
        case DELTA_RUN:
            count = *src++;
            break;
        default:
            count = (u32)(src[0] | (src[1] << 8));
            src += 2;
            break;
        }
        value = *src++;
        if (value) {
            memset(dst, value, count);
        }
        dst += count;
    }
}

static u32 read_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void blit_frame(u8 *pixels)
{
    struct raster_image frame = { (u16)g_screen_width, (u16)g_screen_height, pixels };

    raster_blit(0, 0, &frame);
}

/* 0x4025a3 */
void play_cutscene(s16 number, s16 flags, s16 repeat)
{
    char name[32];
    u8 palette[0x300];
    u8 *first = NULL;
    s16 count = 2;
    int starting = 1;
    int done = 0;
    u32 clock;

    {
        const struct game_profile *profile = game_current();
        char path[1024];

        if (profile->sequence_names != NULL) {
            if (number < 0 || number >= profile->sequence_count) {
                fprintf(stderr, "sequence %d: no such sequence\n", number);
                return;
            }
            snprintf(name, sizeof(name), "%s.PAK", profile->sequence_names[number]);
        } else {
            snprintf(name, sizeof(name), "AN%d.pak", number);
        }
        /* Time Gate for Windows plays ANIM2D\AN<n>.smk (TG 0x409835), the
         * DOS build AN<n>.MUX. */
        if (!platform_resolve_path(name, path, sizeof(path))) {
            char movie[32];

            snprintf(movie, sizeof(movie), "ANIM2D/AN%d.SMK", number);
            if (smacker_play(movie)) {
                g_init_view = 1;
                return;
            }
            snprintf(movie, sizeof(movie), "AN%d.MUX", number);
            if (mux_play(movie)) {
                g_init_view = 1;
                return;
            }
            fprintf(stderr, "sequence %s: not a PAK sequence, skipped\n", name);
            return;
        }
    }
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "%6u sequence %s flags %d repeat %d\n", timer_get_ms(), name, flags, repeat);
    }
    g_sample_current = -1;
    g_sample_priority = -1;
    clock = timer_get_ms();

    while (!done) {
        s16 i;

        for (i = 0; i < count - 1; i++) {
            u8 *entry = file_load_alloc(name, (u16)i);
            s16 j;

            if (entry == NULL) {
                fatal_error(0, name);
            }
            if (i == 0) {
                memcpy(palette, entry, sizeof(palette));
                if (game_current()->palette_source == PALETTE_CAMERA_6BIT) {
                    size_t k;
                    for (k = 0; k < sizeof(palette); k++) {
                        palette[k] = (u8)((palette[k] * 255u) / 63u);
                    }
                }
                /* The picture on screen fades out under its own palette
                 * before the first frame replaces it; fading after the
                 * blit showed the new frame in the old colours. */
                if (starting) {
                    if (flags & CUTSCENE_FADE_IN) {
                        palette_fade_out(0x10, 0);
                    }
                    if (flags & CUTSCENE_BLACKOUT) {
                        palette_blackout();
                    }
                }
                blit_frame(entry + 0x300);
                memcpy(&count, entry + FIRST_COUNT_OFFSET, 2);
                if (starting) {
                    first = entry;
                }
            } else {
                /* Later frames build on the first entry's picture. */
                if (read_u32(entry) >= FRAME_RAW_MIN) {
                    memcpy(first + 0x300, entry + 4, FRAME_BYTES);
                } else {
                    cutscene_delta(entry + 4, first + 0x300);
                }
                blit_frame(first + 0x300);
            }

            while (timer_get_ms() - clock < FRAME_TICKS) {
            }
            clock += FRAME_TICKS;

            if (i == 0 && starting) {
                memcpy(g_palette_buffer, palette, sizeof(palette));
                screen_present();
                if (flags & CUTSCENE_FADE_IN) {
                    palette_fade(0x10, 0);
                } else {
                    palette_set_range(palette, 0, 256);
                }
                starting = 0;
            } else {
                screen_present();
            }

            for (j = 0; j < g_cutscene_sample_count; j++) {
                if (g_cutscene_samples[j * 2] == i + 1) {
                    sample_play(g_cutscene_samples[j * 2 + 1]);
                }
            }

            if (i != 0) {
                mem_free(entry);
            } else if (entry != first) {
                mem_free(entry);   /* a repeat reloads entry 0 */
            }

            input_poll();
            if ((g_last_scancode & 0xff) == SC_ESC) {
                sound_stop_all();
                done = 1;
                break;
            }
        }
        if (--repeat == 0) {
            done = 1;
        }
    }
    mem_free(first);
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "%6u sequence %s done\n", timer_get_ms(), name);
    }

    /* 0x40284d: memset(g_palette_buffer, 0x300, 0) in Watcom's (dst, c, n)
     * order clears nothing, so only the upload remains. */
    if (flags & CUTSCENE_CLEAR_PAL) {
        palette_set_range(g_palette_buffer, 0, 256);
    }
    if (flags & CUTSCENE_FADE_OUT) {
        palette_fade_out(0x10, 0);
    }
    screen_clear();
    screen_present();
    g_fade_state = 2;
    g_init_view = (flags & CUTSCENE_FADE_OUT) ? 2 : 1;
}

/* 0x405db1 - opcode 71: picture, time, sample. */
void show_picture(s16 entry, s16 time, s16 sample)
{
    u8 palette[0x300];
    struct game_timer timer;

    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "%6u picture %d time %d\n", timer_get_ms(), entry, time);
    }
    ticks_freeze();
    {
        int has_palette;

        screen_clear();
        screen_present();
        has_palette = picture_load(entry, g_back_buffer, palette);
        if (has_palette >= 0) {
            struct raster_image frame = { (u16)g_screen_width, (u16)g_screen_height, g_back_buffer };

            if (has_palette == 1) {
                memcpy(g_palette_buffer, palette, sizeof(palette));
            }
            raster_blit(0, 0, &frame);
            if (has_palette == 1) {
                palette_set_range(palette, 0, 256);
            }
            screen_present();
        }
    }
    sample_play(sample);

    timer_reset(&timer);
    while (timer_elapsed(&timer) < 60) {
        input_poll();
    }
    timer_reset(&timer);
    while (timer_elapsed(&timer) < (u32)time && !(g_last_scancode & 0xff) && !g_key_confirm) {
        input_poll();
    }

    ticks_unfreeze();
    g_init_view = 1;
    screen_clear();
    screen_present();
}

/* A film by its extension: Smacker (.SMK) or the DOS Time Gate's .MUX. */
int movie_play(const char *name)
{
    size_t n = strlen(name);

    if (n > 4 && (strcmp(name + n - 4, ".MUX") == 0 || strcmp(name + n - 4, ".mux") == 0)) {
        return mux_play(name);
    }
    return smacker_play(name);
}
