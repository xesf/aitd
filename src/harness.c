/*
 * harness.c - staged bring-up driver.
 *
 * The engine is reconstructed incrementally, so it is useful to run it up to
 * a chosen point and stop there. game_main (main.c) stays a faithful
 * transcription of 0x4078b7; everything scaffolding-shaped lives here.
 *
 *   --stage=init     run engine_init, report what came up, shut down
 *   --stage=video    init, then present a test pattern for a few seconds
 *   --stage=font     init, then draw the font glyph sheet from ITD_RESS entry 1
 *   --stage=intro    init, then game_run(1, 0, 0) - the intro
 *   --stage=menu     init, then one pass of main_menu_run
 *   --stage=install  copy the game folder out of the disc image
 *
 * AITD_FRAMES caps how long the presenting stages run, so a scripted check
 * never blocks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "cdimage.h"
#include "game.h"
#include "harness.h"
#include "platform.h"
#include "subsystems.h"

static int frame_budget(void)
{
    const char *env = getenv("AITD_FRAMES");
    int frames = (env != NULL) ? atoi(env) : 0;

    return (frames > 0) ? frames : 180;  /* ~3 seconds at 60 Hz */
}

static void report_state(void)
{
    const struct game_profile *profile = game_current();

    printf("game            : %s%s\n",
           profile != NULL ? profile->name : "(none)",
           "");
    printf("screen          : %dx%d\n", g_screen_width, g_screen_height);
    printf("front/back      : %p / %p\n",
           (void *)g_front_buffer, (void *)g_back_buffer);
    printf("font blob       : %p\n", g_font_data);
    printf("cd audio ready  : %d (option %d)\n",
           g_cdaudio_ready, g_opt_cdaudio_enabled);
    printf("sound ready     : %d (option %d)\n",
           g_sound_ready, g_opt_sound_enabled);
    printf("voice.cfg       : %d\n", g_voice_cfg_value);
    printf("caches          : body=%p anim=%p life=%p samp=%p\n",
           (void *)g_cache_body, (void *)g_cache_anim,
           (void *)g_cache_life, (void *)g_cache_sample);
}

/* Drains the event queue and reports whether a quit was requested. */
static int pump_events(void)
{
    struct platform_event event;

    while (platform_poll_event(&event)) {
        if (event.kind == PLATFORM_EVENT_QUIT) {
            return 1;
        }
        if (event.kind == PLATFORM_EVENT_KEY_DOWN && event.code == 53) {
            return 1;  /* Escape */
        }
    }

    return 0;
}

/*
 * A palette and a pattern that exercise the whole video path: an indexed
 * plane, a 6 bit VGA palette, and the present call.
 */
static void draw_test_pattern(void)
{
    u8 *palette = g_back_buffer + (size_t)g_screen_width * (size_t)g_screen_height;
    int x, y, i;

    for (i = 0; i < 256; i++) {
        palette[i * 3 + 0] = (u8)i;
        palette[i * 3 + 1] = (u8)(255 - i);
        palette[i * 3 + 2] = (u8)((i * 3) & 0xff);
    }

    for (y = 0; y < g_screen_height; y++) {
        for (x = 0; x < g_screen_width; x++) {
            g_back_buffer[y * g_screen_width + x] = (u8)((x ^ y) & 0xff);
        }
    }

    /*
     * Orientation marker: a solid bar across rows 4..11 only. Index 1 is
     * (1, 254, 3) in the ramp above, which nothing else in the pattern
     * produces over a whole row. If the presented window shows this bar near
     * the bottom, the display path is flipped.
     */
    for (y = 4; y < 12; y++) {
        for (x = 0; x < g_screen_width; x++) {
            g_back_buffer[y * g_screen_width + x] = 1;
        }
    }

    platform_video_set_palette(palette);
}

/*
 * Writes what the engine is about to present as a PPM: the indexed plane
 * resolved through the palette that sits in the frame buffer tail. Lets a
 * scripted run check pixels without capturing a window.
 */
static void dump_frame(const char *path)
{
    const u8 *palette = g_blit_source
                      + (size_t)g_screen_width * (size_t)g_screen_height;
    FILE *fp = fopen(path, "wb");
    int i;
    int count = g_blit_width * g_blit_height;

    if (fp == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", g_blit_width, g_blit_height);

    for (i = 0; i < count; i++) {
        u8 index = g_blit_source[i];

        /* Palettes are 8 bit per channel here, matching the backend. */
        fwrite(palette + index * 3, 1, 3, fp);
    }

    fclose(fp);
    printf("wrote %s (%dx%d)\n", path, g_blit_width, g_blit_height);
}

static int run_presenting_loop(const char *label);

static int run_presenting_stage(const char *label)
{
    const char *dump = getenv("AITD_DUMP_FRAME");

    if (dump != NULL) {
        dump_frame(dump);
    }

    return run_presenting_loop(label);
}

static int run_presenting_loop(const char *label)
{
    int frames = frame_budget();
    int i;

    printf("stage %s: presenting %d frames\n", label, frames);

    for (i = 0; i < frames; i++) {
        if (pump_events()) {
            printf("stage %s: quit after %d frames\n", label, i);
            break;
        }

        platform_video_present(g_blit_source, g_blit_width, g_blit_height);

        if (i == 0) {
            const char *capture = getenv("AITD_CAPTURE");

            if (capture != NULL && platform_video_capture(capture)) {
                printf("captured %s\n", capture);
            }
        }

        platform_delay(16);
    }

    return 0;
}

int harness_run(int argc, char **argv)
{
    const char *stage = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--stage=", 8) == 0) {
            stage = argv[i] + 8;
        }
    }

    if (stage == NULL) {
        stage = getenv("AITD_STAGE");
    }

    if (stage == NULL) {
        return 0;  /* not a harness run; game_main takes over */
    }

    game_detect();

    if (strcmp(stage, "init") == 0) {
        engine_init();
        report_state();
        engine_shutdown();
        return 1;
    }

    if (strcmp(stage, "video") == 0) {
        engine_init();
        report_state();
        draw_test_pattern();
        run_presenting_stage("video");
        engine_shutdown();
        return 1;
    }

    if (strcmp(stage, "intro") == 0) {
        engine_init();
        g_transition_flag = 1;
        game_run(1, 0, 0);           /* 0x4079af */
        run_presenting_stage("intro");
        engine_shutdown();
        return 1;
    }

    if (strcmp(stage, "game") == 0) {
        extern int g_frame_limit;
        extern const char *g_frame_dump_path;
        const char *floor = getenv("AITD_FLOOR");
        const char *room = getenv("AITD_ROOM");

        engine_init();
        g_frame_limit = frame_budget();
        g_frame_dump_path = getenv("AITD_DUMP_FRAME");
        game_run(floor ? (s16)atoi(floor) : (s16)game_current()->start_floor,
                 room ? (s16)atoi(room) : (s16)game_current()->start_room, 1);
        engine_shutdown();
        return 1;
    }

    if (strcmp(stage, "install") == 0) {
        /* Copy the game folder out of the disc image in AITD_DATA_DIR. */
        char path[1024];
        int count;

        platform_resolve_path("ITD_RESS.PAK", path, sizeof(path));
        count = cdimage_extract_all();
        printf("installed %d files from the disc image\n", count);
        return 1;
    }

    if (strcmp(stage, "menu") == 0) {
        int choice;

        engine_init();
        choice = main_menu_run();    /* 0x4079bd */
        printf("stage menu: main_menu_run returned %d\n", choice);
        run_presenting_stage("menu");
        engine_shutdown();
        return 1;
    }

    fprintf(stderr, "unknown stage '%s'\n", stage);
    return 1;
}
