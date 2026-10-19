/*
 * stubs.c - subsystems reached from game_main and engine_init that have not
 * been reconstructed yet.
 *
 * Each stub keeps the signature and the IDA address so the call graph links
 * and runs; bodies get filled in as the analysis advances. Nothing here
 * invents behaviour that has not been observed in the disassembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "subsystems.h"
#include "life.h"
#include "text.h"
#include "savegame.h"
#include "game.h"
#include "picture.h"

s16 g_init_view;             /* 0x4350b9 */

/* Word pair at 0x435160 that engine_init reads the screen size from. */
static u16 s_video_mode_info[2] = { AITD_SCREEN_WIDTH, AITD_SCREEN_HEIGHT };

/* The Windows Time Gate 640 build runs at 640 x 400 (TG640.EX_ uses
 * 0x280 x 0x190 throughout); the profile carries the size. */
s32 video_mode_width(void)
{
    return game_current() != NULL ? game_current()->screen_width : s_video_mode_info[0];  /* 0x4072e5 */
}

s32 video_mode_height(void)
{
    return game_current() != NULL ? game_current()->screen_height : s_video_mode_info[1]; /* 0x4072f2 */
}

/* 0x41a152 - FindWindowA guard, checked first thing in game_main (0x4078dd). */
int sys_is_already_running(void)
{
    return platform_is_already_running("Alone in the Dark 3");
}

/* 0x414e53 - the container entry carrying the menu ids (11..14). The game
 * ships several text entries, one per language; which index the original
 * selects is not established yet. */
void text_table_init(void)
{
    text_table_load(text_pak_name(), 0);
}

/* The text container: TEXTES.PAK in Alone in the Dark 3, one file per
 * language in the other titles. AITD_LANG picks one (e.g. ENGLISH). */
const char *text_pak_name(void)
{
    static const char *const names[] = {
        "TEXTES.PAK", "ENGLISH.PAK", "USA.PAK", "FRANCAIS.PAK", "DEUTSCH.PAK",
        "ESPAGNOL.PAK", "ITALIANO.PAK"
    };
    static char chosen[32];
    char path[1024];
    size_t i;

    if (chosen[0] != '\0') {
        return chosen;
    }
    if (getenv("AITD_LANG") != NULL) {
        snprintf(chosen, sizeof(chosen), "%s.PAK", getenv("AITD_LANG"));
        if (platform_resolve_path(chosen, path, sizeof(path))) {
            return chosen;
        }
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (platform_resolve_path(names[i], path, sizeof(path))) {
            snprintf(chosen, sizeof(chosen), "%s", names[i]);
            return chosen;
        }
    }
    snprintf(chosen, sizeof(chosen), "%s", names[0]);
    return chosen;
}

/* 0x419129, dispatched through [object_table + 0x2b7e] at 0x407418: builds
 * the save screen objects, which savegame.c replaces. */
void save_screens_init(void *object_table)
{
    (void)object_table;
}

/* 0x40ad85 */
void actor_table_reset(s16 mode) { (void)mode; }

/* engine_init stage helpers, 0x4072d1..0x407314. */
void input_init(void) {}
void window_show(void) {}
void timer_init(void) {}
void video_mode_query(void) {}
void heap_init(void) {}
void rand_init(void) {}
void rand_seed_buffer(void *state) { (void)state; }

/* 0x41fadb - built the GDI palette; the native backend owns this now. */
void palette_init(void) {}

u8 *g_picture_33;       /* 0x44435c, ITD_RESS 33, never read */
u8 *g_escape_picture;   /* 0x444360, ITD_RESS 17 */
u8 *g_menu_picture;     /* 0x444364, ITD_RESS 13 */
u8 *g_tab_picture;      /* 0x444368, ITD_RESS 48 */

/* A full screen picture plus its palette. These slots are Alone in the
 * Dark 3's; the other titles get a black picture until their own menus
 * are rebuilt. */
static u8 *load_picture(int index)
{
    u8 *picture = mem_alloc_checked(0xfd00);

    memset(picture, 0, 0xfd00);
    if (game_id() == GAME_AITD3) {
        picture_load(index, picture, picture + 64000);
    }
    return picture;
}

/* 0x40adc6 */
void video_buffers_ready(void)
{
    g_picture_33 = load_picture(ITD_RESS_PICTURE_33);
    g_tab_picture = load_picture(ITD_RESS_MAP);
    g_escape_picture = load_picture(ITD_RESS_ESCAPE_MENU);
    g_menu_picture = load_picture(ITD_RESS_MENU);
    g_save_picture = load_picture(ITD_RESS_SAVE_SCREEN);
    platform_video_set_palette(g_back_buffer + (size_t)g_screen_width * (size_t)g_screen_height);
}

/* engine_shutdown steps, 0x407687. */
void music_shutdown(void) { platform_cdaudio_stop(); }
void sound_shutdown(void) { platform_sound_shutdown(); }
void sound_release(void) {}
void cdaudio_shutdown(void) { platform_cdaudio_shutdown(); }
void video_shutdown(void) { platform_video_shutdown(); }
void window_destroy(void) {}

/* 0x402d9b - wraps the mixer bring-up the platform layer now provides. */
int sound_init(void)
{
    return platform_sound_init();
}

/* 0x401010 - mciSendStringA based in the original. */
int cdaudio_init(void)
{
    return platform_cdaudio_init();
}

