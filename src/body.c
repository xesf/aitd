/*
 * body.c - LISTBODY.PAK model header reader.
 *
 * See body.h for the layout and the evidence behind it. Only the parts that
 * hold for every shipped entry are parsed here; the group record contents and
 * the primitive list that follows are left as raw bytes.
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
    u32 count_offset;
    u32 vertex_offset;
    u32 vertex_bytes;
    int i;

    memset(out, 0, sizeof(*out));

    if (block == NULL || size < 0x12) {
        return 0;
    }

    out->flags = read_u16(bytes);

    for (i = 0; i < 6; i++) {
        out->bounds[i] = read_s16(bytes + 2 + i * 2);
    }

    /* Bit 1 inserts a 10 byte section ahead of the vertex count. */
    count_offset = (out->flags & BODY_FLAG_HAS_GROUPS) ? 0x1a : 0x10;
    vertex_offset = count_offset + 2;

    if (size < vertex_offset) {
        return 0;
    }

    out->vertex_count = read_u16(bytes + count_offset);
    vertex_bytes = (u32)out->vertex_count * 6;

    if (vertex_bytes / 6 != out->vertex_count
        || vertex_offset + vertex_bytes > size) {
        return 0;
    }

    /*
     * The source blocks are little endian s16 triples, which is also the
     * layout of struct body_vertex on every target this builds for. Aliasing
     * the block avoids copying every mesh twice.
     */
    out->vertices = (const struct body_vertex *)(const void *)(bytes + vertex_offset);

    if (out->flags & BODY_FLAG_HAS_GROUPS) {
        u32 group_offset = vertex_offset + vertex_bytes;

        if (group_offset + 2 > size) {
            return 0;
        }

        out->group_count = read_u16(bytes + group_offset);
        out->groups = bytes + group_offset + 2;
        out->groups_size = size - (group_offset + 2);
    }

    return 1;
}
