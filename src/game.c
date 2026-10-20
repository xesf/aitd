/*
 * game.c - per-title configuration and detection.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "aitd.h"
#include "boot.h"
#include "game.h"
#include "platform.h"

/* The 16 interface colours laid over every camera palette. AITD2.EXE keeps
 * them as VGA levels at file offset 134112 (also used by Jack); WAITD3.EXE
 * at 0x435068. */
static const u8 s_overlay_aitd2[0x30] = {
    0x00, 0x00, 0x00, 0x3f, 0x3f, 0x3f, 0x0c, 0x0c, 0x0e, 0x30, 0x2f, 0x3f,
    0x23, 0x2c, 0x23, 0x2a, 0x1d, 0x2a, 0x2a, 0x21, 0x18, 0x3f, 0x05, 0x2a,
    0x12, 0x14, 0x18, 0x31, 0x15, 0x17, 0x15, 0x25, 0x15, 0x15, 0x2f, 0x3f,
    0x3f, 0x22, 0x15, 0x2b, 0x15, 0x3f, 0x3f, 0x3f, 0x21, 0x3f, 0x3f, 0x3f
};
extern u8 g_palette_overlay[0x30];

/* AITD2 sequence files by number (every number its scripts use, 0..32,
 * is in range). Jack only plays 0. */
static const char *const s_sequences_aitd2[] = {
    "BATL", "GRAP", "CLE1", "CLE2", "COOK", "EXPL", "FALA", "FAL2", "GLIS",
    "GREN", "JEND", "MANI", "MER", "TORD", "PANT", "VERE", "PL21", "PL22",
    "ENDX", "SORT", "EFER", "STAR", "MEDU", "PROL", "GRAS", "STRI", "ITRO",
    "BILL", "PIRA", "PIR2", "VENT", "FIN", "LAST"
};
static const char *const s_sequences_jack[] = { "PERE" };
/* The DOS Time Gate's films before its menu (the Windows build plays the
 * same two as Smacker files, TG320.EX_ 0x41778f). */
static const char *const s_intro_timegate[] = { "AN0.MUX", "AN1.MUX", NULL };
#define SEQUENCES(t) (t), (int)(sizeof(t) / sizeof((t)[0]))

static const struct game_profile s_aitd1 = {
    GAME_AITD1, "Alone in the Dark", "LISTBOD2.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 12, 0, PALETTE_GLOBAL, NULL,
    MASK_COVER_ZONES,
    52, 45, LIFE_DIALECT_AITD1,
    5, 4, 0, 0,
    "LISTSAMP", { 16, 26, 2, 57, 1, 183 },
    NULL, 0, NULL, boot_aitd1_intro, boot_aitd1_attract, boot_aitd1_new_game, MENU_STYLE_FRAMED, INVENTORY_FRAMED, -1,
    MUSIC_ADLIB, "LISTMUS.PAK", -1,
    0, 0, 7, 1
};

static const struct game_profile s_jack = {
    GAME_JACK, "Jack in the Dark", "PERE.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 16, 0, PALETTE_CAMERA_6BIT, s_overlay_aitd2,
    MASK_PAK,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1, 0,
    "LISTSAMP", { 14, 0x50, 8, 0xd0, 0, 185 },
    SEQUENCES(s_sequences_jack), NULL, NULL, NULL, NULL, MENU_STYLE_NONE, INVENTORY_FRAMED, 2,
    MUSIC_ADLIB, "LISTMUS.PAK", -1,
    16, 1, -1, -1
};

static const struct game_profile s_aitd2 = {
    GAME_AITD2, "Alone in the Dark 2", "MER.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 16, 0, PALETTE_CAMERA_6BIT, s_overlay_aitd2,
    MASK_PAK,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1, 0,
    "LISTSAMP", { 14, 0x50, 8, 0xd0, 0, 185 },
    SEQUENCES(s_sequences_aitd2), NULL, NULL, NULL, NULL, MENU_STYLE_FRAMED, INVENTORY_PICTURE, 59,
    MUSIC_ADLIB, "LISTMUS.PAK", -1,
    8, 7, 8, 0
};

static const struct game_profile s_aitd3 = {
    GAME_AITD3, "Alone in the Dark 3", "AN1.PAK",
    320, 200, NULL,
    FLOOR_SAL_CAM, "SAL", "CAM", "CAMERA", 16, 4, PALETTE_CAMERA_8BIT, g_palette_overlay,
    MASK_PAK,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1, 1,
    "LISTSAMP", { 14, 0x50, 8, 0xd0, 0, 185 },
    NULL, 0, NULL, NULL, NULL, NULL, MENU_STYLE_PICTURE, INVENTORY_DIMMED, -1,
    MUSIC_CD, NULL, -1,
    0, 12, 1, 0
};

static const struct game_profile s_timegate = {
    GAME_TIMEGATE, "Time Gate: Knight's Chase", "PURSUIT.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_CAMSAL, "ETAGE", "CAMSAL", "CAMERA", 22, 0, PALETTE_CAMERA_8BIT, NULL,
    MASK_SLOT_FILES,
    54, 70, LIFE_DIALECT_AITD2,
    2, 0, 1, 0,
    "SAMPLES", { 80, 100, 80, 0x104, 0, 185 },
    NULL, 0, s_intro_timegate, NULL, NULL, NULL, MENU_STYLE_RING, INVENTORY_ICONS, -1,
    MUSIC_TG_DOS, "MUSIC.PAK", 0,
    0, 5, -1, -1
};

/* The Windows build keeps ITD_RESS and LISTHYB in a folder per resolution. */
static struct game_profile s_knights_320;
static struct game_profile s_knights_640;

/* Probed in order; the first marker found wins. */
static const struct game_profile *const s_profiles[] = {
    &s_aitd1, &s_jack, &s_aitd2, &s_aitd3, &s_timegate
};

#define PROFILE_COUNT (int)(sizeof(s_profiles) / sizeof(s_profiles[0]))

static const struct game_profile *s_current;

const struct game_profile *game_profile_by_id(enum game_id id)
{
    int i;

    for (i = 0; i < PROFILE_COUNT; i++) {
        if (s_profiles[i]->id == id) {
            return s_profiles[i];
        }
    }
    return NULL;
}

static const struct game_profile *profile_by_name(const char *name)
{
    if (strcasecmp(name, "aitd1") == 0) return &s_aitd1;
    if (strcasecmp(name, "jack") == 0) return &s_jack;
    if (strcasecmp(name, "aitd2") == 0) return &s_aitd2;
    if (strcasecmp(name, "aitd3") == 0) return &s_aitd3;
    if (strcasecmp(name, "timegate") == 0) return &s_timegate;
    return NULL;
}

static int file_present(const char *name)
{
    char path[1024];

    return platform_resolve_path(name, path, sizeof(path));
}

/* Time Gate for Windows: the 320/640 folders hold the resolution specific
 * resources; the 640 build runs at 640 x 480. */
static const struct game_profile *knights_profile(void)
{
    const char *res = getenv("AITD_RES");
    int low = res != NULL && strcmp(res, "320") == 0;

    static const char *const intro[] = { "ANIM2D/AN0.SMK", "ANIM2D/AN1.SMK", NULL };

    s_knights_320 = s_timegate;
    s_knights_320.intro_movies = intro;
    s_knights_320.music_source = MUSIC_MIDI;
    s_knights_320.music_pak = "WINMUS.PAK";
    s_knights_320.mask_layout = MASK_CAMERA_INDEXED;
    s_knights_320.name = "Time Gate: Knight's Chase (Windows, 320x200)";
    s_knights_320.resolution_dir = "320";

    s_knights_640 = s_timegate;
    s_knights_640.intro_movies = intro;
    s_knights_640.music_source = MUSIC_MIDI;
    s_knights_640.music_pak = "WINMUS.PAK";
    s_knights_640.mask_layout = MASK_CAMERA_INDEXED;
    s_knights_640.name = "Time Gate: Knight's Chase (Windows, 640x480)";
    s_knights_640.resolution_dir = "640";
    s_knights_640.screen_width = 640;
    s_knights_640.screen_height = 400;
    s_knights_640.background_prefix = "CAMERX";

    return low ? &s_knights_320 : &s_knights_640;
}

static int folder_present(const char *name)
{
    char path[1024];

    return platform_resolve_path(name, path, sizeof(path))
        && platform_is_directory(path);
}

const struct game_profile *game_detect(void)
{
    const char *override = getenv("AITD_GAME");
    int i;

    s_current = NULL;
    if (override != NULL && override[0] != '\0') {
        s_current = profile_by_name(override);
    }
    for (i = 0; s_current == NULL && i < PROFILE_COUNT; i++) {
        if (file_present(s_profiles[i]->marker_file)) {
            s_current = s_profiles[i];
        }
    }
    if (s_current == NULL) {
        s_current = &s_aitd3;   /* the build this engine was taken from */
    }
    if (s_current->id == GAME_TIMEGATE && folder_present("640")) {
        s_current = knights_profile();
    }
    if (s_current->id == GAME_AITD1 && file_present("000000.VOC")) {
        /* The CD release (speech in VOC files): INDARK.EXE PlayMusic
         * (0x6c2b) only plays CD audio, track = the MUSIC number, and its
         * scripts number the music that way; LISTMUS.PAK is a leftover
         * of the floppy release that this build never opens. */
        static struct game_profile cd;

        cd = *s_current;
        cd.name = "Alone in the Dark (CD)";
        cd.music_source = MUSIC_CD;
        s_current = &cd;
    }
    platform_set_overlay_dir(s_current->resolution_dir);
    return s_current;
}

const struct game_profile *game_current(void)
{
    return s_current;
}

enum game_id game_id(void)
{
    return s_current != NULL ? s_current->id : GAME_UNKNOWN;
}

void game_build_room_name(const char *prefix, int index, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%02d.PAK", prefix, index);
}
