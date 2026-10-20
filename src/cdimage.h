/*
 * cdimage.h - game data and CD audio read from a BIN/CUE disc image.
 *
 * The CUE sheet lists the tracks; the BIN files hold raw 2352 byte
 * sectors (or 2048 byte data sectors). The data track carries an
 * ISO 9660 file system with the game files, and audio tracks are 16 bit
 * 44.1 kHz stereo samples.
 */

#ifndef AITD_CDIMAGE_H
#define AITD_CDIMAGE_H

#include <stddef.h>

#include "aitd.h"

/* Opens a CUE sheet and indexes the file system of its first data track.
 * Returns 1 on success. Files found in the image are copied on first use
 * to `cache_dir`, which must exist. */
int cdimage_open(const char *cue_path, const char *cache_dir);
void cdimage_close(void);
int cdimage_is_open(void);

/* Copies `name` (a path relative to the game folder on the disc, any
 * case, '/' separated) into the cache and writes its path to `out`.
 * Returns 1 when the disc has the file. */
int cdimage_resolve(const char *name, char *out, size_t out_size);

/* Audio track `track` (1 based, as on the disc) as 16 bit stereo samples
 * at 44.1 kHz. Returns a malloc'd buffer the caller frees, or NULL when
 * the track is missing or not audio. `frames` receives the frame count. */
short *cdimage_read_audio(int track, u32 *frames);

#endif /* AITD_CDIMAGE_H */
