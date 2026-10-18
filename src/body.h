/*
 * body.h - LISTBODY.PAK model format.
 *
 * Derived by probing all 788 entries of the shipped LISTBODY.PAK through the
 * reconstructed container reader. What is stated here held for every entry;
 * what is not yet determined is called out as such rather than guessed.
 *
 * Layout
 * ------
 *   u16 flags
 *   s16 bounds[6]          candidate bounding box, offsets 0x02..0x0d
 *   ...                    0x0e..: contents depend on flags
 *
 *   flags & 2 clear:  u16 vertex_count at 0x10, vertices from 0x12
 *   flags & 2 set:    u16 vertex_count at 0x1a, vertices from 0x1c
 *
 *   vertex: s16 x, s16 y, s16 z
 *
 *   flags & 2 set:    u16 group_count follows the vertex array, then the
 *                     group records themselves.
 *
 * Evidence
 * --------
 *   flags values present: 0 (1 entry), 1 (337), 2 (2), 3 (448).
 *   With the offsets above, the vertex array fits inside the block for
 *   337/337 of the flags==1 entries and 448/448 of the flags==3 entries.
 *   Setting bit 1 shifts the count by exactly 10 bytes, so that bit gates a
 *   10 byte header section.
 *   Bit 1 also gates the group table: a group count parses sanely for every
 *   entry that has the bit and for none that lack it, which is what marks it
 *   as the "skinned/animated" flag as opposed to a static mesh.
 *   In the simplest body the single group record contains 26, the entry's own
 *   vertex count, i.e. one group spanning the whole mesh.
 *
 * Not determined
 * --------------
 *   The group record stride. Strides of 14, 16, 18 and 20 all fit inside
 *   every block, so block length alone cannot decide it; the field layout has
 *   to come from the code that walks the table. sub_4038F6 (0x4038f6, 0x2e3f
 *   bytes) reads g_cache_body at six sites and is the next thing to read.
 *
 *   What follows the group table (primitives: faces, lines, spheres) is
 *   likewise unread.
 */

#ifndef AITD_BODY_H
#define AITD_BODY_H

#include "aitd.h"

/* Bit 1 of the flags word. */
#define BODY_FLAG_HAS_GROUPS 0x0002

struct body_vertex {
    s16 x, y, z;
};

struct body {
    u16 flags;
    s16 bounds[6];

    u16 vertex_count;
    const struct body_vertex *vertices;  /* points into the source block */

    u16 group_count;      /* 0 unless BODY_FLAG_HAS_GROUPS */
    const u8 *groups;     /* raw; stride not yet determined */
    u32 groups_size;      /* bytes from the group table to end of block */
};

/*
 * Parses the header of a decompressed LISTBODY entry. Does not copy: the
 * pointers alias the caller's block, which must outlive the struct.
 * Returns 0 if the block is too short for the layout above.
 */
int body_parse(const void *block, u32 size, struct body *out);

#endif /* AITD_BODY_H */
