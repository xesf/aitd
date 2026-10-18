/*
 * game.h - per-title configuration.
 *
 * WAITD3.EXE is one of the later builds of an engine that also shipped as
 * Alone in the Dark 1 and 2, Jack in the Dark and Time Gate: Knight's Chase.
 * The container format, the cache system and the startup shape are common to
 * all of them; what differs is which asset files exist and what they are
 * called.
 *
 * Everything the engine needs to vary between titles is collected here so the
 * core never tests for a specific game inline.
 *
 * Only the Alone in the Dark 3 profile has been checked against real data.
 * The others carry the file names the engine would look for and are marked
 * unverified; each becomes fact as soon as a data set is available to probe.
 */

#ifndef AITD_GAME_H
#define AITD_GAME_H

#include "aitd.h"

enum game_id {
    GAME_UNKNOWN = 0,
    GAME_AITD1,
    GAME_AITD2,
    GAME_AITD3,
    GAME_JACK,      /* Jack in the Dark, the AITD2 promo title */
    GAME_TIMEGATE   /* Time Gate: Knight's Chase */
};

struct game_profile {
    enum game_id id;
    const char *name;

    /* Container holding the font and the shared UI resources. Every known
     * title uses this name (0x4071fe). */
    const char *resource_pak;

    /* Per-room geometry and per-room camera containers. The number suffix is
     * two digits, zero padded. */
    const char *room_prefix;
    const char *camera_prefix;
    const char *mask_prefix;

    /* Cache backing files. These match the cache names passed to cache_create
     * at 0x40756d..0x4075fe. */
    const char *list_body;
    const char *list_anim;
    const char *list_life;
    const char *list_trak;
    const char *list_hyb;
    const char *list_mat;
    const char *list_samp;

    /* Flat .ITD records loaded at startup. */
    const char *defines_itd;
    const char *priority_itd;
    const char *objects_itd;
    const char *vars_itd;

    /* Byte count read from the defines record; 0x46 for Alone in the Dark 3
     * (0x407467). Other titles may carry a different record length. */
    u32 defines_size;

    /* A file that exists only in this title's data set, used for detection. */
    const char *marker_file;

    /* Set when the profile has been confirmed against a real data set. */
    int verified;
};

/* Chooses a profile by probing the data directory. AITD_GAME overrides the
 * probe with one of: aitd1, aitd2, aitd3, jack, timegate. */
const struct game_profile *game_detect(void);

/* The profile in force. NULL before game_detect has run. */
const struct game_profile *game_current(void);

const struct game_profile *game_profile_by_id(enum game_id id);

/* Builds a numbered asset name, e.g. ("SAL", 3) -> "SAL03.PAK". */
void game_build_room_name(const char *prefix, int index, char *out, size_t out_size);

#endif /* AITD_GAME_H */
