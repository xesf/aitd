/*
 * palette.c - palette upload and fading.
 *
 * Reconstructed from:
 *   palette_blackout    0x40b0f8  256 black entries
 *   palette_fade        0x40af57  fade in or out over g_palette_buffer
 *   palette_set_range   0x402d67  eax=palette, edx=first, ebx=count
 *   palette_scale_in    0x4200e0
 *   palette_scale_out   0x420128
 *
 * The menu never loads a backdrop picture. It blacks the palette, reads the
 * menu palette into g_palette_buffer, and lets palette_fade bring it up from
 * black; whatever pixels are already in the frame buffer become visible as
 * the palette rises.
 */

#include <string.h>

#include "aitd.h"
#include <stdio.h>
#include <stdlib.h>

#include "anim.h"
#include "game.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "subsystems.h"

/*
 * 0x40af74 overwrites the caller's first argument with this, so the fade
 * always runs in 0x80 steps: levels 0, 0x80, 0x100.
 */
#define FADE_STEP 0x80
#define FADE_FULL 0x100

/*
 * The DOS titles (AITD1, AITD2, Jack) fade by the step their caller gives,
 * one step per screen refresh (INDARK.EXE FadeInPhys / FadeOutPhys), so
 * palette_fade_out(8) takes 32 refreshes. WAITD3 ignores the argument and
 * runs three steps. Each step is held for a 1/60 s tick: without that the
 * fade between the intro and the game finished before it was seen.
 */
static int fade_step(s16 requested)
{
    if (game_id() == GAME_AITD1 || game_id() == GAME_AITD2 || game_id() == GAME_JACK) {
        return (requested > 0 && requested <= FADE_FULL) ? requested : 8;
    }
    return FADE_STEP;
}

static void fade_present(void)
{
    u32 start = platform_get_ticks();

    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "%6u fade step\n", start);
    }
    platform_video_present(g_present_buffer != NULL ? g_present_buffer : g_blit_source,
                           g_blit_width, g_blit_height);
    while (platform_get_ticks() - start < 17) {
        platform_delay(1);
    }
}

s16 g_fade_state;  /* 0x43505e, 2 requests a fade out */

/*
 * 0x4200e0 / 0x420128 - scales a palette towards or away from black.
 * level 0 is black, FADE_FULL is the source unchanged.
 */
static void palette_scale(const u8 *src, u8 *dst, int level)
{
    int i;

    if (level > FADE_FULL) {
        level = FADE_FULL;
    }
    if (level < 0) {
        level = 0;
    }

    for (i = 0; i < AITD_PALETTE_BYTES; i++) {
        dst[i] = (u8)((src[i] * level) / FADE_FULL);
    }
}

/* 0x402d67. The backend owns the whole table, so a partial range is applied
 * by copying into the shadow copy the engine keeps in g_palette_buffer. */
void palette_set_range(const u8 *palette, int first, int count)
{
    (void)first;
    (void)count;

    platform_video_set_palette(palette);
}

/* 0x40b0f8 */
void palette_blackout(void)
{
    u8 black[AITD_PALETTE_BYTES];

    memset(black, 0, sizeof(black));
    palette_set_range(black, 0, 256);
}

/*
 * 0x40af57
 *
 * The first argument is accepted and ignored, exactly as the original does,
 * so call sites keep reading the way the disassembly does.
 */
void palette_fade(s16 step_request, s16 first_index)
{
    u8 scaled[AITD_PALETTE_BYTES];
    int level;
    int step = fade_step(step_request);

    ticks_freeze();  /* 0x40af7b */

    if (g_fade_state == 2) {
        /* 0x40af8a: fade out, full down to black. */
        for (level = FADE_FULL; level >= 0; level -= step) {
            palette_scale(g_palette_buffer, scaled, level);
            palette_set_range(scaled, 0, 256);
            fade_present();
        }
    } else {
        /* 0x40afe5: fade in, black up to full. */
        for (level = 0; level < FADE_FULL + step; level += step) {
            palette_scale(g_palette_buffer, scaled, level > FADE_FULL ? FADE_FULL : level);
            palette_set_range(scaled, first_index, 256);
            fade_present();
        }
    }

    g_fade_state = 1;        /* 0x40b02e */
    ticks_unfreeze();    /* 0x40b037 */
}

/*
 * 0x40b042 - palette_fade_out
 *
 * Mirror image of palette_fade's fade-in branch: the level runs from full
 * down to black in 0x80 steps, and g_fade_state is left at 0. Like
 * palette_fade it accepts a first argument and never reads it (0x40b05f
 * overwrites eax with the step), so call sites keep their original shape.
 */
void palette_fade_out(s16 step_request, s16 first_index)
{
    u8 scaled[AITD_PALETTE_BYTES];
    int level;
    int step = fade_step(step_request);

    ticks_freeze();

    for (level = FADE_FULL; level > -step; level -= step) {
        palette_scale(g_palette_buffer, scaled, level < 0 ? 0 : level);
        palette_set_range(scaled, first_index, 256);
        fade_present();
    }

    g_fade_state = 0;
    ticks_unfreeze();
}
