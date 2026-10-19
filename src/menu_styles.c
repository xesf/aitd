/*
 * menu_styles.c - the main menus of the titles that do not use AITD3's
 * picture menu (menu.c).
 *
 * Framed (AITD1, AITD2, Jack): a frame of old style sprites around the
 * middle of a black screen and three rows, text 11..13 (new game, load,
 * quit). The selected row sits on a colour 100 bar in colour 15 over a
 * colour 4 shadow; the others are colour 4 (frames in frame.c).
 *
 * Ring (Time Gate, TG320.EX_ 0x405f6b / 0x405c64): ITD_RESS picture 1, a
 * medallion, with seven HYB icons around it (positions doubled in the
 * 640 x 400 build, TG640.EX_ 0x405e2a); left and right walk the ring
 * and the selected icon is redrawn from its highlighted bank. Enter, space
 * or keypad enter pick, Escape picks quit, 3000 idle ticks give -1.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "frame.h"
#include "game.h"
#include "hyb.h"
#include "life.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "picture.h"
#include "raster.h"
#include "subsystems.h"

/* The menus draw on whatever the window shows; before any scene has run
 * the raster is not set up yet. */
static void target_begin(void)
{
    if (g_raster.width == 0) {
        raster_init(g_back_buffer, g_screen_width, g_screen_height);
    }
    raster_push_target(presented_buffer());
}

static void wait_released(void)
{
    while (g_key_dir_flags != 0 || (g_last_scancode & 0xff) != 0 || g_key_confirm != 0) {
        input_poll();
    }
}

static int confirm_key(void)
{
    int sc = g_last_scancode & 0xff;

    return sc == SC_ENTER || sc == SC_SPACE || sc == 0x9c || g_key_confirm != 0;
}

/* ---- framed --------------------------------------------------------------- */

static void framed_draw(int selected)
{
    s16 y = 76;
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    memset(g_raster.target, 0, SCREEN_PIXELS);
    frame_draw(160, 100, 320, 80);
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    for (i = 0; i < 3; i++, y = (s16)(y + 16)) {
        if (i == selected) {
            raster_rect(10, 309, y, (s16)(y + 16), 100);
            text_draw_centered_shadowed(160, y, (s16)(11 + i), 15, 4);
        } else {
            text_draw_centered(160, y, (s16)(11 + i), 4);
        }
    }
    raster_pop_target();
    screen_present();
}

/* AITD2's ITD_RESS 59 is a bare 6 bit palette whose first 16 colours are
 * placeholders for the interface overlay. Jack has none; its first
 * picture's palette is taken (an assumption, not checked against the
 * original). */
static void framed_palette(void)
{
    const struct game_profile *profile = game_current();
    u8 palette[AITD_PALETTE_BYTES];
    int i;

    if (profile->menu_palette < 0) {
        return;
    }
    if (picture_load(profile->menu_palette, NULL, palette) != 1) {
        u32 size = 0;
        u8 *raw = pak_load("ITD_RESS.PAK", profile->menu_palette, &size);

        if (raw == NULL || size < AITD_PALETTE_BYTES) {
            if (raw != NULL) {
                mem_free(raw);
            }
            return;
        }
        memcpy(palette, raw, AITD_PALETTE_BYTES);
        mem_free(raw);
        if (profile->palette_source == PALETTE_CAMERA_6BIT) {
            for (i = 0; i < AITD_PALETTE_BYTES; i++) {
                palette[i] = (u8)((palette[i] * 255u) / 63u);
            }
        }
    }
    if (profile->palette_overlay != NULL) {
        for (i = 0; i < 0x30; i++) {
            palette[i] = (u8)((profile->palette_overlay[i] * 255u) / 63u);
        }
    }
    memcpy(g_palette_buffer, palette, sizeof(palette));
}

static int framed_menu(void)
{
    static const int results[3] = {
        MENU_RESULT_NEW_GAME, MENU_RESULT_LOAD_GAME, MENU_RESULT_QUIT
    };
    struct game_timer timer;
    int selected = 0;

    framed_palette();
    framed_draw(selected);
    g_fade_state = 1;
    palette_fade(0x10, 0);
    timer_reset(&timer);
    wait_released();
    while (timer_elapsed(&timer) < 3000) {
        input_poll();
        if (g_key_dir_flags & (DIR_UP | DIR_DOWN)) {
            selected = (selected + ((g_key_dir_flags & DIR_UP) ? 2 : 1)) % 3;
            framed_draw(selected);
            timer_reset(&timer);
            wait_released();
        } else if (confirm_key()) {
            wait_released();
            return results[selected];
        }
    }
    return MENU_RESULT_INTRO;
}

/* ---- ring ----------------------------------------------------------------- */

static const struct {
    s16 x, y;
    u8 icon, selected;   /* ITD_RESS HYB banks, 0x451b.. at 0x416c63 */
} s_ring[7] = {
    { 0x27, 0x01, 0x67, 0x66 },
    { 0x1c, 0x34, 0x6e, 0x6f },
    { 0x2b, 0x6d, 0x5c, 0x5d },
    { 0x84, 0x96, 0x72, 0x73 },
    { 0xe3, 0x6d, 0x6c, 0x6d },
    { 0xef, 0x34, 0x5e, 0x5f },
    { 0xe6, 0x01, 0x6a, 0x6b }
};

static void ring_icon(int entry, s16 x, s16 y)
{
    u32 size = 0;
    u8 *bank = pak_load("ITD_RESS.PAK", entry, &size);

    if (bank != NULL) {
        hyb_render(0, x, y, bank);
        mem_free(bank);
    }
}

static void ring_draw(int selected)
{
    u8 palette[AITD_PALETTE_BYTES];
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    if (picture_load(1, g_raster.target, palette) == 1) {
        memcpy(g_palette_buffer, palette, sizeof(palette));
        if (g_fade_state != 1) {
            palette_set_range(g_palette_buffer, 0, 256);
        }
    }
    for (i = 0; i < 7; i++) {
        ring_icon(s_ring[i].icon, SCALED(s_ring[i].x), SCALED(s_ring[i].y));
    }
    ring_icon(s_ring[selected].selected, SCALED(s_ring[selected].x), SCALED(s_ring[selected].y));
    raster_pop_target();
    screen_present();
}

/* TG 0x4062c3 / 0x406148: ITD_RESS picture 0x74 (three tarot cards) and
 * three HYB icons; left/right choose, Enter, space or Escape keep the
 * choice for all three difficulty words (0x452580..0x452584). */
static const struct {
    s16 x;
    u8 icon, selected;
} s_cards[3] = { { 0x29, 0x60, 0x61 }, { 0x7a, 0x62, 0x63 }, { 0xce, 0x64, 0x65 } };

static void cards_draw(int selected)
{
    u8 palette[AITD_PALETTE_BYTES];
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    if (picture_load(0x74, g_raster.target, palette) == 1) {
        memcpy(g_palette_buffer, palette, sizeof(palette));
        palette_set_range(g_palette_buffer, 0, 256);
    }
    for (i = 0; i < 3; i++) {
        ring_icon(s_cards[i].icon, SCALED(s_cards[i].x), SCALED(0x25));
    }
    ring_icon(s_cards[selected].selected, SCALED(s_cards[selected].x), SCALED(0x25));
    raster_pop_target();
    screen_present();
}

static void cards_select(void)
{
    int selected = g_difficulty[0];

    if (selected < 0 || selected > 2) {
        selected = selected < 0 ? 0 : 2;
    }
    cards_draw(selected);
    wait_released();
    for (;;) {
        input_poll();
        if (g_key_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
            selected = (selected + ((g_key_dir_flags & DIR_LEFT) ? 2 : 1)) % 3;
            cards_draw(selected);
            wait_released();
        } else if (confirm_key() || (g_last_scancode & 0xff) == SC_ESC) {
            wait_released();
            break;
        }
    }
    g_difficulty[0] = g_difficulty[1] = g_difficulty[2] = (s16)selected;
}

/* TG 0x41778f: 0 new game, 2 load, 5 difficulty, 6 quit; 1 does nothing,
 * 3 starts Windows' volume control and is left out, 4 replays the second
 * intro film, 5 is the difficulty screen. */
static int ring_menu(void)
{
    struct game_timer timer;
    int selected = 0;

    ring_draw(selected);
    g_fade_state = 1;
    palette_fade(0x10, 0);
    timer_reset(&timer);
    wait_released();
    for (;;) {
        if (timer_elapsed(&timer) >= 3000) {
            return MENU_RESULT_INTRO;
        }
        input_poll();
        if (g_key_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
            selected = (selected + ((g_key_dir_flags & DIR_LEFT) ? 6 : 1)) % 7;
            ring_draw(selected);
            timer_reset(&timer);
            wait_released();
        } else if ((g_last_scancode & 0xff) == SC_ESC) {
            wait_released();
            return MENU_RESULT_QUIT;
        } else if (confirm_key()) {
            wait_released();
            switch (selected) {
            case 0: return MENU_RESULT_NEW_GAME;
            case 2: return MENU_RESULT_LOAD_GAME;
            case 6: return MENU_RESULT_QUIT;
            case 4:     /* TG 0x409835(1): the second intro film again */
                if (game_current()->intro_movies != NULL && game_current()->intro_movies[0] != NULL
                    && game_current()->intro_movies[1] != NULL) {
                    movie_play(game_current()->intro_movies[1]);
                }
                ring_draw(selected);
                timer_reset(&timer);
                break;
            case 5:
                cards_select();
                ring_draw(selected);
                timer_reset(&timer);
                break;
            default:
                fprintf(stderr, "menu: ring entry %d not rebuilt\n", selected);
                timer_reset(&timer);
                break;
            }
        }
    }
}

/* Runs the title's own menu; returns -2 for AITD3's, which menu.c draws. */
int menu_style_run(void)
{
    switch (game_current()->menu_style) {
    case MENU_STYLE_NONE:
        return MENU_RESULT_NEW_GAME;
    case MENU_STYLE_FRAMED:
        return framed_menu();
    case MENU_STYLE_RING:
        return ring_menu();
    default:
        return -2;
    }
}
