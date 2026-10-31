/*
 * path.h - Time Gate's route finding, for objects that follow another one
 * around the obstacles of the floor (move mode 5, see path.c).
 */

#ifndef AITD_PATH_H
#define AITD_PATH_H

#include "aitd.h"

/* Loads the floor's obstacles and zones from PURSUIT.PAK (0x41e3ce,
 * 0x41f168, 0x41e051). */
void path_load_floor(s16 floor);

/* Grows the obstacles by the walking margin again (0x41e051); life
 * opcode 95 of Time Gate calls it. */
void path_rebuild_boxes(void);

/* Finds a route between two points of the floor grid (8 room units a
 * cell); returns the number of legs, 0 when there is none (0x41ef3c). */
int path_find(s16 x, s16 z, s16 tx, s16 tz);

/* The point the first leg of the last route leads to, in grid cells. */
void path_first_node(s16 *x, s16 *z);

#endif /* AITD_PATH_H */
