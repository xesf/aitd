/*
 * picture.h - ITD_RESS full screen pictures, see picture.c.
 */

#ifndef AITD_PICTURE_H
#define AITD_PICTURE_H

#include "aitd.h"

/* Loads picture `entry` into pixels (a screen's worth) and, when the entry has
 * one, its palette widened to 8 bits into palette (768 bytes); either
 * pointer may be NULL. Returns 1 with a palette, 0 without, -1 when the
 * entry is missing. */
int picture_load(int entry, u8 *pixels, u8 *palette);

#endif /* AITD_PICTURE_H */
