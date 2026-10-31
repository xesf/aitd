/*
 * body.h - LISTBODY.PAK model format, as the renderer reads it (r3d.c,
 * 0x423ba9 and 0x423e59; TG320.EX_ for the textured bodies).
 *
 *   u16 flags              2 bones, 4 no points, 8 bones with their own
 *                          rotation (Time Gate), plus textures and normals
 *   s16 bounds[6]          x1 x2 y1 y2 z1 z2
 *   u16 extra_size         bytes of the extra block that follows
 *   u8  extra[extra_size]  the animation's scratch words; the texture and
 *                          normal tables' offsets in the titles that have them
 *   u16 vertex_count
 *   s16 vertices[vertex_count][3]
 *
 *   flags & 2:
 *   u16 bone_count
 *   u16 bone_offsets[bone_count]   byte offsets of the records below
 *   bone records, 0x10 bytes (0x18 with flags & 8):
 *     +0 u16  first point, a byte offset into the vertex array
 *     +2 s16  point count
 *     +4 u16  pivot point, a byte offset into the vertex array
 *     +6 u8   parent bone
 *     +7 u8   this bone's index
 *     +8 s16  type: rotate, translate or zoom
 *     +a s16  alpha beta gamma, or x y z, set by the animation
 *     +10     with flags & 8: the bone's rest rotation alpha beta gamma
 *
 *   u16 primitive_count, then the primitives (r3d.c collect_primitives).
 */

#ifndef AITD_BODY_H
#define AITD_BODY_H

#include "aitd.h"

/* Bits of the flags word. */
#define BODY_FLAG_HAS_BONES 0x0002
#define BODY_FLAG_BONE_ROTATIONS 0x0008

struct body_vertex {
    s16 x, y, z;
};

struct body {
    u16 flags;
    s16 bounds[6];
    u16 extra_size;

    u16 vertex_count;
    const struct body_vertex *vertices;  /* points into the source block */

    u16 bone_count;       /* 0 unless BODY_FLAG_HAS_BONES */
    const u8 *bone_offsets;
    const u8 *bones;      /* the records, bone_stride bytes each */
    u16 bone_stride;

    u16 primitive_count;
    const u8 *primitives;
};

/*
 * Parses a decompressed LISTBODY entry up to its primitives. Does not copy:
 * the pointers alias the caller's block, which must outlive the struct.
 * Returns 0 if the block is too short for the layout above.
 */
int body_parse(const void *block, u32 size, struct body *out);

#endif /* AITD_BODY_H */
