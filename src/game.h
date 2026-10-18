/*
 * game.h - per-title configuration.
 *
 * WAITD3.EXE is one build of an engine that also shipped as Alone in the
 * Dark 1 and 2, Jack in the Dark and Time Gate: Knight's Chase (DOS, and a
 * Windows SVGA build named KNIGHTS). Everything that differs between the
 * titles is described here so the core tests a property, never a game.
 *
 * Facts come from the shipped data sets (data/<GAME>/) and, where noted,
 * from the FITD project (Free in the Dark), used as a reference only.
 */

#ifndef AITD_GAME_H
#define AITD_GAME_H

#include <stddef.h>

#include "aitd.h"

enum game_id {
    GAME_UNKNOWN = 0,
    GAME_AITD1,
    GAME_JACK,      /* Jack in the Dark */
    GAME_AITD2,
    GAME_AITD3,
    GAME_TIMEGATE   /* Time Gate: Knight's Chase, DOS or Windows (KNIGHTS) */
};

/* Where a floor's rooms and cameras live. */
enum floor_layout {
    FLOOR_ETAGE_BLOB,   /* ETAGE##.PAK: entry 0 all rooms, entry 1 all cameras,
                           each a u32 offset table (AITD1, AITD2, Jack) */
    FLOOR_SAL_CAM,      /* SAL##.PAK one room per entry, CAM##.PAK one camera
                           per entry (AITD3) */
    FLOOR_ETAGE_CAMSAL  /* ETAGE##.PAK one room per entry, CAMSAL##.PAK one
                           camera per entry (Time Gate) */
};

/* How the masks drawn over actors are stored. */
enum mask_layout {
    MASK_COVER_ZONES,   /* none: the camera's cover polygons (AITD1) */
    MASK_PAK,           /* MASK##.PAK, one entry per camera (AITD2, Jack, AITD3) */
    MASK_NONE           /* Time Gate */
};

/* Where a camera background takes its palette from. */
enum palette_source {
    PALETTE_GLOBAL,     /* ITD_RESS entry 3, one for the whole game (AITD1) */
    PALETTE_CAMERA_6BIT,/* after the pixels, 0..63 VGA levels (AITD2, Jack) */
    PALETTE_CAMERA_8BIT /* after the pixels, 0..255 (AITD3, Time Gate) */
};

/* Raw life opcode and variable numbering. */
enum life_dialect {
    LIFE_DIALECT_AITD1, /* its own order, see lifemap.c */
    LIFE_DIALECT_AITD2  /* AITD2, Jack, AITD3, Time Gate: the life_op values */
};

struct game_profile {
    enum game_id id;
    const char *name;
    const char *marker_file;        /* only this title ships it */

    int screen_width;
    int screen_height;
    const char *resolution_dir;     /* overlay folder searched first, or NULL */

    enum floor_layout floor_layout;
    const char *room_prefix;        /* "ETAGE" or "SAL" */
    const char *camera_prefix;      /* CAM / CAMSAL, unused for ETAGE blobs */
    const char *background_prefix;  /* "CAMERA" */
    int camera_view_size;           /* bytes per viewed room in a camera */
    int background_header;          /* bytes before the pixels: 4 in AITD3 */
    enum palette_source palette_source;
    const u8 *palette_overlay;      /* 16 interface colours put over the
                                       camera palette, or NULL */

    enum mask_layout mask_layout;

    int object_size;                /* OBJETS.ITD record: 52 or 54 */
    int defines_words;              /* DEFINES.ITD length in words */
    enum life_dialect life_dialect;

    int font_entry;                 /* ITD_RESS entries */
    int frame_entry;                /* the window frame sprites */
    int font_spacing;
    int hyb_frame;                  /* frame_entry is a HYB sprite bank (AITD3);
                                       the older titles use 16 bit offset sprites */

    const char *sample_pak;         /* LISTSAMP or SAMPLES, without .PAK */

    /* game_run arguments for a new game and for the intro (-1: none). */
    int start_floor, start_room;
    int intro_floor, intro_room;
};

/* Chooses a profile by probing the data directory. AITD_GAME overrides the
 * probe with one of: aitd1, jack, aitd2, aitd3, timegate. AITD_RES selects
 * the Windows Time Gate resolution, 320 or 640 (default 640). */
const struct game_profile *game_detect(void);

/* The profile in force. NULL before game_detect has run. */
const struct game_profile *game_current(void);

const struct game_profile *game_profile_by_id(enum game_id id);

/* Shorthand for game_current()->id. */
enum game_id game_id(void);

/* Builds a numbered asset name, e.g. ("SAL", 3) -> "SAL03.PAK". */
void game_build_room_name(const char *prefix, int index, char *out, size_t out_size);

#endif /* AITD_GAME_H */
