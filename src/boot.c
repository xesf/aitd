/*
 * boot.c - the start-up screens of Alone in the Dark 1.
 *
 * WAITD3.EXE goes from its intro scene straight to the menu; the first
 * title has more around it, worked out from its data:
 *
 *   logo     ITD_RESS 2 (picture), 1 (palette) and 0 (a 3D body). The
 *            picture shows for 180 ticks, then thunder (DEFINES word 4),
 *            a white flash and the armadillo turns away into the dark
 *            (zoom 8920 to 16000 by 50, beta down by 8 a frame). A key
 *            skips it and the credits.
 *   title    ITD_RESS 13 for 48 ticks, then the credits (DEFINES word 3,
 *            + 1) read by themselves on the book, ITD_RESS 7.
 *   choice   ITD_RESS 10, a frame around the chosen half; the other
 *            character's story (DEFINES words 5 and 6) is read on
 *            ITD_RESS 14 and word 8 records who was chosen.
 *   attract  when the menu times out: a random character, the car scene
 *            (floor 7, room 1), the logo and the credits again.
 */

#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "boot.h"
#include "draw.h"
#include "frame.h"
#include "life.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "picture.h"
#include "platform.h"
#include "r3d.h"
#include "raster.h"
#include "subsystems.h"

enum {
    AITD1_BODY_LOGO = 0,
    AITD1_PALETTE_LOGO = 1,
    AITD1_PICTURE_LOGO = 2,
    AITD1_BOOK = 7,
    AITD1_CHOICE = 10,
    AITD1_TITLE = 13,
    AITD1_STORY = 14,

    DEFINE_SAMPLE_PAGE = 0,
    DEFINE_CREDITS = 3,
    DEFINE_SAMPLE_THUNDER = 4,
    DEFINE_STORY_DETECTIVE = 5,
    DEFINE_STORY_HEIRESS = 6,
    DEFINE_CHOSEN = 8
};

static int key_down(void)
{
    return (g_last_scancode & 0xff) != 0 || g_key_confirm != 0 || g_key_dir_flags != 0
        || g_mouse_buttons != 0;
}

static void wait_release(void)
{
    while (key_down()) {
        input_poll();
    }
}

static void show(void)
{
    draw_present();
}

/* Outside a scene the poll presents g_present_buffer: point it at the
 * screen these pages draw on. */
static u8 *present_begin(void)
{
    u8 *saved = g_present_buffer;

    draw_init();
    g_present_buffer = g_screen;
    return saved;
}

/* Returns 1 when a key cut it short. */
static int logo(void)
{
    u32 size = 0;
    u8 *body = pak_load("ITD_RESS.PAK", AITD1_BODY_LOGO, &size);
    u8 *raw = pak_load("ITD_RESS.PAK", AITD1_PALETTE_LOGO, &size);
    u8 palette[AITD_PALETTE_BYTES], saved[AITD_PALETTE_BYTES];
    struct game_timer timer;
    s16 zoom = 8920, beta = 256;
    int skipped = 0;

    memcpy(saved, g_palette_buffer, sizeof(saved));
    if (raw != NULL && size >= AITD_PALETTE_BYTES) {
        memcpy(palette, raw, sizeof(palette));
    } else {
        memcpy(palette, g_palette_buffer, sizeof(palette));
    }
    if (raw != NULL) {
        mem_free(raw);
    }

    raster_reset_clip();
    picture_load(AITD1_PICTURE_LOGO, g_screen, NULL);
    memcpy(g_palette_buffer, palette, sizeof(palette));
    palette_blackout();
    show();
    palette_fade(8, 0);

    timer_reset(&timer);
    for (;;) {
        input_poll();
        if (timer_elapsed(&timer) <= 180) {
            if (key_down()) {
                skipped = 1;
                break;
            }
            continue;
        }

        /* The strike: a white screen for a moment, then the armadillo. */
        sample_play((s16)g_defines_itd[DEFINE_SAMPLE_THUNDER]);
        memset(g_palette_buffer, 0xff, sizeof(palette));
        palette_set_range(g_palette_buffer, 0, 256);
        r3d_set_projection(160, 100, 128, 500, 490);
        while (!key_down() && body != NULL) {
            /* Only the top 141 rows are cleared: the name under the
             * animal stays. */
            memset(g_screen, 0, 45120);
            r3d_set_camera_target(0, 0, 0, 8, beta, 0, zoom);
            r3d_draw_body(0, 0, 0, 0, 0, 0, body);
            show();
            if (zoom == 8920) {
                memcpy(g_palette_buffer, palette, sizeof(palette));
                palette_set_range(g_palette_buffer, 0, 256);
            }
            input_poll();
            zoom = (s16)(zoom + 50);
            if (zoom > 16000) {
                break;
            }
            beta = (s16)(beta - 8);
            platform_delay(16);
        }
        skipped = key_down();
        break;
    }
    if (body != NULL) {
        mem_free(body);
    }
    wait_release();
    palette_fade_out(skipped ? 32 : 16, 0);
    memcpy(g_palette_buffer, saved, sizeof(saved));
    return skipped;
}

static void credits(void)
{
    struct game_timer timer;
    u8 palette[AITD_PALETTE_BYTES];

    memcpy(palette, g_palette_buffer, sizeof(palette));
    raster_reset_clip();
    picture_load(AITD1_TITLE, g_screen, NULL);
    palette_blackout();
    show();
    palette_fade(8, 0);

    timer_reset(&timer);
    do {
        input_poll();
    } while (timer_elapsed(&timer) < 0x30 && !key_down());

    reader_set_backdrop(AITD1_BOOK);
    palette_set_range(g_palette_buffer, 0, 256);
    sample_play((s16)g_defines_itd[DEFINE_SAMPLE_PAGE]);
    g_page_turn_style = 1;
    text_reader((s16)(g_defines_itd[DEFINE_CREDITS] + 1), 48, 2, 260, 197, 1, 26, 0);
    memcpy(g_palette_buffer, palette, sizeof(palette));
}

void boot_aitd1_intro(void)
{
    u8 *saved = present_begin();

    if (!logo()) {
        credits();
    }
    g_present_buffer = saved;
}

void boot_aitd1_attract(void)
{
    g_defines_itd[DEFINE_CHOSEN] = (u16)(rand() & 1);
    game_run(7, 1, 0);
    boot_aitd1_intro();
}

static void choice_draw(const u8 *picture, int choice)
{
    raster_reset_clip();
    memcpy(g_screen, picture, 64000);
    if (choice == 0) {
        frame_draw(80, 100, 160, 200);
        raster_reset_clip();
        raster_copy_rect(10, 10, 149, 190, picture);
    } else {
        frame_draw(240, 100, 160, 200);
        raster_reset_clip();
        raster_copy_rect(170, 10, 309, 190, picture);
    }
    show();
}

/* Returns 0 when Escape left the choice. */
int boot_aitd1_new_game(void)
{
    u8 *picture = malloc(64000);
    u8 *saved;
    int choice = 0;

    if (picture == NULL) {
        return 1;
    }
    saved = present_begin();
    picture_load(AITD1_CHOICE, picture, NULL);
    palette_blackout();
    choice_draw(picture, choice);
    palette_fade(0x40, 0);
    wait_release();

    for (;;) {
        input_poll();
        if (g_key_dir_flags & DIR_LEFT) {
            choice = 0;
            choice_draw(picture, choice);
            wait_release();
        } else if (g_key_dir_flags & DIR_RIGHT) {
            choice = 1;
            choice_draw(picture, choice);
            wait_release();
        } else if ((g_last_scancode & 0xff) == SC_ESC) {
            wait_release();
            palette_fade_out(0x40, 0);
            free(picture);
            g_present_buffer = saved;
            return 0;
        } else if ((g_last_scancode & 0xff) == SC_ENTER || g_key_confirm) {
            wait_release();
            break;
        }
    }
    palette_fade_out(0x40, 0);

    /* The chosen character's story: the choice screen as it stands, the
     * other half replaced from ITD_RESS 14. */
    picture_load(AITD1_STORY, picture, NULL);
    raster_reset_clip();
    if (choice == 0) {
        raster_copy_rect(160, 0, 319, 199, picture);
    } else {
        raster_copy_rect(0, 0, 159, 199, picture);
    }
    memcpy(g_back_buffer, g_screen, 64000);   /* the reader's backdrop */
    show();
    palette_set_range(g_palette_buffer, 0, 256);
    g_page_turn_style = 0;
    if (choice == 0) {
        text_reader((s16)(g_defines_itd[DEFINE_STORY_HEIRESS] + 1), 165, 5, 314, 194, 2, 15, 0);
        g_defines_itd[DEFINE_CHOSEN] = 1;
    } else {
        text_reader((s16)(g_defines_itd[DEFINE_STORY_DETECTIVE] + 1), 5, 5, 154, 194, 2, 15, 0);
        g_defines_itd[DEFINE_CHOSEN] = 0;
    }
    palette_fade_out(64, 0);
    free(picture);
    g_present_buffer = saved;

    game_run(7, 1, 0);   /* the car arriving at Derceto */
    return 1;
}
