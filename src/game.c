/*
 * game.c - per-title configuration and detection.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "aitd.h"
#include "game.h"
#include "platform.h"

static const struct game_profile s_aitd1 = {
    GAME_AITD1, "Alone in the Dark", "LISTBOD2.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 12,
    MASK_COVER_ZONES,
    52, 45, LIFE_DIALECT_AITD1,
    5, 4, 0,
    "LISTSAMP",
    0, 0, 7, 1
};

static const struct game_profile s_jack = {
    GAME_JACK, "Jack in the Dark", "PERE.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 16,
    MASK_RAW,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1,
    "LISTSAMP",
    16, 1, -1, -1
};

static const struct game_profile s_aitd2 = {
    GAME_AITD2, "Alone in the Dark 2", "MER.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_BLOB, "ETAGE", NULL, "CAMERA", 16,
    MASK_RAW,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1,
    "LISTSAMP",
    8, 7, 8, 0
};

static const struct game_profile s_aitd3 = {
    GAME_AITD3, "Alone in the Dark 3", "AN1.PAK",
    320, 200, NULL,
    FLOOR_SAL_CAM, "SAL", "CAM", "CAMERA", 16,
    MASK_PAK,
    54, 70, LIFE_DIALECT_AITD2,
    1, 0, 1,
    "LISTSAMP",
    0, 12, 1, 0
};

static const struct game_profile s_timegate = {
    GAME_TIMEGATE, "Time Gate: Knight's Chase", "PURSUIT.PAK",
    320, 200, NULL,
    FLOOR_ETAGE_CAMSAL, "ETAGE", "CAMSAL", "CAMERA", 22,
    MASK_PAK,
    54, 70, LIFE_DIALECT_AITD2,
    2, 0, 1,
    "SAMPLES",
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

    s_knights_320 = s_timegate;
    s_knights_320.name = "Time Gate: Knight's Chase (Windows, 320x200)";
    s_knights_320.resolution_dir = "320";

    s_knights_640 = s_timegate;
    s_knights_640.name = "Time Gate: Knight's Chase (Windows, 640x480)";
    s_knights_640.resolution_dir = "640";
    s_knights_640.screen_width = 640;
    s_knights_640.screen_height = 480;

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
