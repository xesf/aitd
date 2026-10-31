/*
 * body.c - LISTBODY.PAK model header reader.
 *
 * See body.h for the layout. The primitives are left as raw bytes for the
 * renderer (r3d.c).
 */

#include <string.h>

#include "aitd.h"
#include "body.h"

static u16 read_u16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static s16 read_s16(const u8 *p)
{
    return (s16)read_u16(p);
}

int body_parse(const void *block, u32 size, struct body *out)
{
    const u8 *bytes = block;
    u32 at;
    int i;

    memset(out, 0, sizeof(*out));
    if (block == NULL || size < 0x12) {
        return 0;
    }
    out->flags = read_u16(bytes);
    for (i = 0; i < 6; i++) {
        out->bounds[i] = read_s16(bytes + 2 + i * 2);
    }
    out->extra_size = read_u16(bytes + 0x0e);
    at = 0x10 + (u32)out->extra_size;
    if (at + 2 > size) {
        return 0;
    }
    out->vertex_count = read_u16(bytes + at);
    at += 2;
    if (at + (u32)out->vertex_count * 6 > size) {
        return 0;
    }
    /* Little endian s16 triples, the layout of struct body_vertex on every
     * target this builds for: aliased rather than copied. */
    out->vertices = (const struct body_vertex *)(const void *)(bytes + at);
    at += (u32)out->vertex_count * 6;

    if (out->flags & BODY_FLAG_HAS_BONES) {
        if (at + 2 > size) {
            return 0;
        }
        out->bone_count = read_u16(bytes + at);
        out->bone_offsets = bytes + at + 2;
        out->bone_stride = (out->flags & BODY_FLAG_BONE_ROTATIONS) ? 0x18 : 0x10;
        at += 2 + (u32)out->bone_count * 2;
        out->bones = bytes + at;
        at += (u32)out->bone_count * out->bone_stride;
        if (at > size) {
            return 0;
        }
    }
    if (at + 2 <= size) {
        out->primitive_count = read_u16(bytes + at);
        out->primitives = bytes + at + 2;
    }
    return 1;
}
