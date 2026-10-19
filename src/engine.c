/*
 * engine.c - startup and teardown.
 *
 *   engine_init      0x4071e1
 *   engine_shutdown  0x407687
 *
 * engine_init is the single best map of the engine: it names every asset
 * family, fixes the frame buffer layout, and shows which subsystems are
 * optional. The order below is the order of the original.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "raster.h"
#include "game.h"
#include "font.h"
#include "savegame.h"
#include "platform.h"
#include "subsystems.h"

/* Asset files opened by name. */
#define PAK_RESOURCES "itd_ress.pak"  /* 0x4071fe and eight more sites */
#define FILE_DEFINES "DEFINES.ITD"    /* 0x407445 */
#define FILE_PRIORITY "Priority.ITD"  /* 0x40752c */
#define FILE_VOICE_CFG "voice.cfg"    /* 0x407393 */

/* Number of times engine_init retries CD audio bring-up (0x407326). */
#define CDAUDIO_INIT_ATTEMPTS 5

s32 g_screen_width;
s32 g_screen_height;
u8 *g_front_buffer;
u8 *g_back_buffer;
u16 g_blit_width;
u16 g_blit_height;
u8 *g_blit_source;

u8 g_opt_cdaudio_enabled = 1;
u8 g_opt_sound_enabled = 1;
s16 g_cdaudio_ready;
s16 g_sound_ready;
s16 g_sound_ready2;     /* 0x4350d7, toggled by the S key in the scene loop */
s32 g_voice_cfg_value;

s16 g_quit_requested;   /* 0x435058 */

void *g_font_data;
void *g_anim_buffer;
void *g_object_table;
void *g_priority_itd;
u16 g_defines_itd[DEFINES_WORDS_MAX];
/* game_main clears 0x1fb8 bytes at 0x4423a4 (0x4078f0) before the first
 * menu pass, and again whenever a new game starts (0x407a20). */
u16 g_game_vars[0xfdc];
s16 g_difficulty[3] = { 1, 1, 1 };  /* 0x444ee2, 0x444ee4, 0x444ee6 */


struct hqr_resource *g_cache_body;
struct hqr_resource *g_cache_trak;
struct hqr_resource *g_cache_hyb;
struct hqr_resource *g_cache_raw;
struct hqr_resource *g_cache_life;
struct hqr_resource *g_cache_sample;
struct hqr_resource *g_cache_anim;
struct hqr_resource *g_cache_mat;

/* Blocks whose contents are still unidentified; sizes are exact. */
/*
 * ITD_RESS entries kept resident: 0x0e/0x0f/0x10 are the three inventory
 * backgrounds (one per playable character, DEFINES word 14), 0x12/0x13/0x14
 * the matching hybrid sprite sets for their frames.
 */
static void *g_pak_entry_0; /* 0x444ac0 */
static void *g_pak_entry_0e;
static void *g_pak_entry_0f;
static void *g_pak_entry_10;
static void *g_pak_entry_12;
static void *g_pak_entry_13;
static void *g_pak_entry_14;

/* Cleared to 0xff over 0xa0 bytes at 0x407507. */
static u16 g_slot_table[0x50];  /* 0x444374 */

static char g_window_title[128];  /* 0x444f5e */

const u8 *engine_resource(int index)
{
    switch (index) {
    case ITD_RESS_UI_SPRITES: return g_pak_entry_0;
    case ITD_RESS_INVENTORY_1: return g_pak_entry_0e;
    case ITD_RESS_INVENTORY_0: return g_pak_entry_0f;
    case ITD_RESS_INVENTORY_2: return g_pak_entry_10;
    case ITD_RESS_INVENTORY_FRAME_1: return g_pak_entry_12;
    case ITD_RESS_INVENTORY_FRAME_0: return g_pak_entry_13;
    case ITD_RESS_INVENTORY_FRAME_2: return g_pak_entry_14;
    default: return NULL;
    }
}

/*
 * 0x4071f9..0x407248
 *
 * The font blob is entry 1 of itd_ress.pak. Failure here is fatal and is
 * reported with a literal string rather than a text id, because the text
 * table has not been loaded yet.
 */
static void load_font(void)
{
    g_font_data = load_malloc_hqr(PAK_RESOURCES, game_current()->font_entry);  /* 0x407203 */

    if (g_font_data == NULL) {                        /* 0x40720d */
        sys_error_box("?", "Error loading font file"); /* 0x407220 */
        exit(1);
    }

    font_set_data(g_font_data);       /* 0x407234 */
    font_set_old_format(game_id() == GAME_AITD1 || game_id() == GAME_AITD2
                        || game_id() == GAME_JACK, game_current()->font_spacing);
    font_set_spacing((u16)game_current()->font_spacing); /* 0x40723e */
    font_set_color_table(g_font_color_table); /* 0x407248, 0x44485c */

    /* Alone in the Dark 1 has one palette for every camera, ITD_RESS
     * entry 3. */
    if (game_current()->palette_source == PALETTE_GLOBAL) {
        u8 *palette = load_malloc_hqr(PAK_RESOURCES, 3);

        if (palette != NULL) {
            memcpy(g_palette_buffer, palette, AITD_PALETTE_BYTES);
            mem_free(palette);
        }
    }
}

/*
 * 0x40725b..0x4072cc
 *
 * The window title is copied out of text record 0x138d before the window is
 * created. A failure is reported using text 0x1388, which is available by
 * this point.
 */
static void create_window(void)
{
    platform_set_icon("AppIcon");   /* 0x407260 */
    platform_set_cursor(0);         /* 0x407267 */

    strcpy(g_window_title, text_get_by_id(TEXT_ID_WINDOW_TITLE)); /* 0x40727e */

    if (!platform_video_init(g_window_title,
                             AITD_SCREEN_WIDTH,     /* edx = 0x140 */
                             AITD_SCREEN_HEIGHT)) { /* ebx = 0xc8 */
        sys_error_box("?", text_get_by_id(TEXT_ID_VIDEO_FAILED)); /* 0x4072b8 */
        exit(1);
    }

    /* The paint callback (0x419b5f) installed at 0x4072cc is folded into
     * platform_video_present on hosts that do not expose WM_PAINT. */
}

/*
 * 0x4072d1..0x407314
 */
static void init_host_services(void)
{
    input_init();        /* 0x4072d1 */
    window_show();       /* 0x4072d6 */
    timer_init();        /* 0x4072db */
    video_mode_query();  /* 0x4072e0 */

    /* Both dimensions come out of the mode record at 0x435160. */
    g_screen_width = video_mode_width();    /* 0x4072e5 */
    g_screen_height = video_mode_height();  /* 0x4072f2 */

    heap_init();          /* 0x407300 */
    rand_init();          /* 0x407305 */
    rand_seed_buffer(g_rand_state); /* 0x40730a, 0x4350e5 */
    palette_init();       /* 0x407314 */
}

/*
 * 0x407319..0x407391
 *
 * CD audio is retried up to five times, each failure raising a retry prompt
 * (text 0x138b). Exhausting the attempts is fatal (text 0x138c). Passing -c
 * skips the whole block, so the game still starts without a disc.
 */
static void init_cdaudio(void)
{
    int attempts;

    if (!g_opt_cdaudio_enabled) {  /* 0x407319 */
        return;
    }

    for (attempts = CDAUDIO_INIT_ATTEMPTS; attempts > 0; attempts--) {
        if (platform_cdaudio_init()) {  /* 0x407336 */
            g_cdaudio_ready = 1;        /* 0x40733f, 0x407348 */
            return;
        }
        sys_error_box("?", text_get_by_id(TEXT_ID_CD_RETRY)); /* 0x407365 */
    }

    sys_error_box("?", text_get_by_id(TEXT_ID_CD_FAILED));  /* 0x407384 */
    exit(1);                                                /* 0x407389 */
}

/*
 * 0x407393..0x4073de
 *
 * voice.cfg holds a single integer. Unlike the CD path, a missing file is
 * not an error: g_voice_cfg_value simply stays zero.
 */
static void init_audio(void)
{
    char *cfg = file_load_text_alloc(FILE_VOICE_CFG);  /* 0x407398 */

    if (cfg != NULL) {
        g_voice_cfg_value = atoi(cfg);  /* 0x4073a9 */
        mem_free(cfg);                  /* 0x4073b6 */
    }

    if (g_opt_sound_enabled && platform_sound_init()) {  /* 0x4073bb, 0x4073c4 */
        g_sound_ready = 1;                               /* 0x4073ce */
        g_sound_ready2 = 1;                              /* 0x4073d7 */
    }
}

/*
 * 0x407440..0x40747e
 *
 * DEFINES.ITD: 0x46 big-endian words (45 in Alone in the Dark 1, 15 in
 * Jack in the Dark; the read stops at the end of the file).
 */
static void load_defines(void)
{
    int handle = file_open(FILE_DEFINES, 1);  /* 0x40744a */

    if (handle == 0) {                        /* 0x407455 */
        fatal_error(0, FILE_DEFINES);         /* 0x407462 */
    }

    file_read_be16(handle, g_defines_itd, (u32)game_current()->defines_words); /* 0x407475 */
    file_close(handle);                       /* 0x40747e */
}

/*
 * 0x407483..0x4074db
 *
 * Two identically sized buffers: the visible page and the work page. The
 * 0x302 byte tail after the pixel plane carries the 768 byte palette.
 */
static void alloc_frame_buffers(void)
{
    size_t bytes = (size_t)g_screen_width * (size_t)g_screen_height
                 + AITD_FRAME_EXTRA;

    g_front_buffer = mem_alloc_checked(bytes);  /* 0x407494 */
    g_back_buffer = mem_alloc_checked(bytes);   /* 0x4074af */

    g_blit_width = (u16)g_screen_width;   /* 0x4074b9 */
    g_blit_height = (u16)g_screen_height; /* 0x4074c5 */
    g_blit_source = g_back_buffer;        /* 0x4074d1 */
    /* Not in the original: the raster state is otherwise first set up by
     * the first scene, and menus or films can draw before one. */
    raster_init(g_back_buffer, g_screen_width, g_screen_height);

    video_buffers_ready();                /* 0x4074db */
}

/*
 * 0x4074e0..0x407603
 *
 * Every cache is created with an explicit byte budget and entry count; those
 * numbers are the engine's real memory profile and are reproduced verbatim.
 */
static void alloc_caches(void)
{
    g_anim_buffer = mem_alloc(0xcd00);           /* 0x4074e5 */
    if (g_anim_buffer == NULL) {                 /* 0x4074ef */
        fatal_error(1, "BufferAnim");            /* 0x4074fd */
    }

    memset(g_slot_table, 0xff, sizeof(g_slot_table));  /* 0x407516 */

    g_pak_entry_0 = load_malloc_hqr(PAK_RESOURCES, game_current()->frame_entry); /* 0x40751b */
    g_priority_itd = load_malloc(FILE_PRIORITY);                 /* 0x407531 */

    /* Samples are only cached when digital audio actually came up. */
    if (g_sound_ready) {                                    /* 0x40753b */
        g_cache_sample = hqr_init_resource(game_current()->sample_pak, 0x186a0, 20); /* 0x407554 */
    }

    g_cache_mat = hqr_init_resource("ListMat", 0xfa00, 5);       /* 0x40756d */
    g_cache_raw = hqr_init_resource_raw(0x4e20, 50);             /* 0x407581 */
    g_cache_life = hqr_init_resource("ListLife", 0xfde8, 100);   /* 0x40759a */
    g_cache_trak = hqr_init_resource("ListTrak", 0x4e20, 100);   /* 0x4075b3 */
    g_cache_hyb = hqr_init_resource("ListHyb", 0x4e20, 10);      /* 0x4075cc */
    g_cache_body = hqr_init_resource("ListBody", 0x9088, 50);    /* 0x4075e5 */
    g_cache_anim = hqr_init_resource("ListAnim", 0x7530, 80);    /* 0x4075fe */

    g_pak_entry_13 = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_FRAME_0); /* 0x407612 */
    g_pak_entry_12 = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_FRAME_1); /* 0x407626 */
    g_pak_entry_14 = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_FRAME_2); /* 0x40763a */
    g_pak_entry_0f = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_0); /* 0x40764e */
    g_pak_entry_0e = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_1); /* 0x407662 */
    g_pak_entry_10 = load_malloc_hqr(PAK_RESOURCES, ITD_RESS_INVENTORY_2); /* 0x407676 */
}

/*
 * 0x4073e0..0x407436
 *
 * Three fixed size blocks. The 0x2ba6 byte one stores a function pointer at
 * offset 0x2b7e which is then called through immediately, i.e. a hand rolled
 * constructor dispatch.
 */
static void alloc_object_tables(void)
{
    g_save_thumbnail = mem_alloc(0x12a0);   /* 0x4073e5 */
    g_object_table = mem_alloc(0x2ba6);     /* 0x4073f4 */

    save_screens_init(g_object_table);      /* 0x407418 via [obj+0x2b7e] */

    /* 0x407436 allocated the 0x238c byte actor table, g_actors here. */
}

/*
 * 0x4071e1 - engine_init
 */
void engine_init(void)
{
    load_font();
    text_table_init();      /* 0x40724d */
    create_window();
    init_host_services();
    init_cdaudio();
    init_audio();
    alloc_object_tables();
    load_defines();
    alloc_frame_buffers();
    alloc_caches();
}

/*
 * 0x407687 - engine_shutdown
 *
 * Audio goes down before video, and the window is destroyed last. Callers
 * always follow this with exit(-1).
 */
void engine_shutdown(void)
{
    music_shutdown();    /* 0x40b3d2 */
    sound_shutdown();    /* 0x402e4f */
    sound_release();     /* 0x402df6 */
    cdaudio_shutdown();  /* 0x4029a7 */
    video_shutdown();    /* 0x41945e */
    mem_free(NULL);      /* 0x420b58, frees the block held in the caller */
    window_destroy();    /* 0x41dae9 */
}
