/*
 * game.c - per-title configuration and detection.
 *
 * The Alone in the Dark 3 profile is derived from engine_init (0x4071e1) and
 * confirmed against the data set in ./data. The remaining profiles record the
 * names the engine would look for; they are flagged unverified until a data
 * set for each is available to probe.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "aitd.h"
#include "game.h"
#include "platform.h"

/*
 * Alone in the Dark 3. Every name below exists in the shipped data set.
 * The set also carries a second camera family, CAM00..CAM13, alongside
 * CAMERA00..CAM ERA13; which one a room uses has not been established yet.
 */
static const struct game_profile s_aitd3 = {
    GAME_AITD3, "Alone in the Dark 3",
    "ITD_RESS.PAK",
    "SAL", "CAMERA", "MASK",
    "LISTBODY.PAK", "LISTANIM.PAK", "LISTLIFE.PAK", "LISTTRAK.PAK",
    "LISTHYB.PAK", "LISTMAT.PAK", "LISTSAMP.PAK",
    "DEFINES.ITD", "PRIORITY.ITD", "OBJETS.ITD", "VARS.ITD",
    0x46,           /* 0x407467 */
    "SAL00.PAK",
    1
};

/*
 * Alone in the Dark 1. Unverified: the room containers are the reason the
 * profile exists, since this title numbers floors rather than rooms.
 */
static const struct game_profile s_aitd1 = {
    GAME_AITD1, "Alone in the Dark",
    "ITD_RESS.PAK",
    "ETAGE", "CAMERA", "MASK",
    "LISTBODY.PAK", "LISTANIM.PAK", "LISTLIFE.PAK", "LISTTRAK.PAK",
    "LISTHYB.PAK", "LISTMAT.PAK", "LISTSAMP.PAK",
    "DEFINES.ITD", "PRIORITY.ITD", "OBJETS.ITD", "VARS.ITD",
    0x46,
    "ETAGE00.PAK",
    0
};

/* Alone in the Dark 2. Unverified. */
static const struct game_profile s_aitd2 = {
    GAME_AITD2, "Alone in the Dark 2",
    "ITD_RESS.PAK",
    "SAL", "CAM", "MASK",
    "LISTBODY.PAK", "LISTANIM.PAK", "LISTLIFE.PAK", "LISTTRAK.PAK",
    "LISTHYB.PAK", "LISTMAT.PAK", "LISTSAMP.PAK",
    "DEFINES.ITD", "PRIORITY.ITD", "OBJETS.ITD", "VARS.ITD",
    0x46,
    "CAM00.PAK",
    0
};

/* Jack in the Dark. Unverified. Shares the Alone in the Dark 2 layout. */
static const struct game_profile s_jack = {
    GAME_JACK, "Jack in the Dark",
    "ITD_RESS.PAK",
    "SAL", "CAM", "MASK",
    "LISTBODY.PAK", "LISTANIM.PAK", "LISTLIFE.PAK", "LISTTRAK.PAK",
    "LISTHYB.PAK", "LISTMAT.PAK", "LISTSAMP.PAK",
    "DEFINES.ITD", "PRIORITY.ITD", "OBJETS.ITD", "VARS.ITD",
    0x46,
    "JACK.PAK",
    0
};

/* Time Gate: Knight's Chase. Unverified. */
static const struct game_profile s_timegate = {
    GAME_TIMEGATE, "Time Gate: Knight's Chase",
    "ITD_RESS.PAK",
    "SAL", "CAMERA", "MASK",
    "LISTBODY.PAK", "LISTANIM.PAK", "LISTLIFE.PAK", "LISTTRAK.PAK",
    "LISTHYB.PAK", "LISTMAT.PAK", "LISTSAMP.PAK",
    "DEFINES.ITD", "PRIORITY.ITD", "OBJETS.ITD", "VARS.ITD",
    0x46,
    "TIMEGATE.PAK",
    0
};

/* Probed in order; the first marker found wins. The two most specific
 * markers come first so a shared name never decides the match. */
static const struct game_profile *const s_profiles[] = {
    &s_timegate,
    &s_jack,
    &s_aitd1,
    &s_aitd3,
    &s_aitd2
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
    if (strcasecmp(name, "aitd2") == 0) return &s_aitd2;
    if (strcasecmp(name, "aitd3") == 0) return &s_aitd3;
    if (strcasecmp(name, "jack") == 0) return &s_jack;
    if (strcasecmp(name, "timegate") == 0) return &s_timegate;
    return NULL;
}

static int file_present(const char *name)
{
    char path[1024];
    return platform_resolve_path(name, path, sizeof(path));
}

const struct game_profile *game_detect(void)
{
    const char *override = getenv("AITD_GAME");
    int i;

    if (override != NULL && override[0] != '\0') {
        const struct game_profile *forced = profile_by_name(override);

        if (forced != NULL) {
            s_current = forced;
            return s_current;
        }
    }

    for (i = 0; i < PROFILE_COUNT; i++) {
        if (file_present(s_profiles[i]->marker_file)) {
            s_current = s_profiles[i];
            return s_current;
        }
    }

    /* No marker matched. Fall back to the profile this binary was built from
     * rather than refusing to start. */
    s_current = &s_aitd3;
    return s_current;
}

const struct game_profile *game_current(void)
{
    return s_current;
}

void game_build_room_name(const char *prefix, int index, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%02d.PAK", prefix, index);
}
