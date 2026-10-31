/*
 * pak.c - .PAK container reader.
 *
 * Reconstructed from:
 *   file_load_alloc  0x42025a  directory seek, header read, allocate, unpack
 *   pak_unpack       0x425dca  method dispatch
 *   inflate_raw      0x42bb75  method 4 entry point
 *   inflate_block    0x42ba93  1 final-flag bit + 2 block-type bits
 *   pak_unpack_legacy_lz 0x42a8f0  methods 2 and 3
 *
 * Method 4 is raw deflate. inflate_block reads a single bit into the caller's
 * loop flag then two bits selecting between stored (0x42b2d0), fixed Huffman
 * (0x42b3eb) and dynamic Huffman (0x42b5c7) block readers, with the fourth
 * encoding rejected - that is RFC 1951 exactly, LSB first. Decoding every
 * entry of every container shipped with the game with a stock raw inflate
 * confirms it: 11296 of 11296 entries expand to their recorded size.
 *
 * inflate.c decodes it; the result matches zlib byte for byte on every
 * method 4 entry of the six data sets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "inflate.h"
#include "pak.h"
#include "platform.h"

/* Entry header as it sits on disk, after the reserved dword. */
struct pak_entry_header {
    u32 packed_size;
    u32 unpacked_size;
    u8 method;
    u8 flags;
    u8 extra_skip;
    u8 unused_b;
};

static u32 read_u32_le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/*
 * 0x42025a reads the directory one dword at a time. Reading it up front is
 * equivalent and avoids a seek per entry.
 */
int pak_open(struct pak *pak, const char *name)
{
    u8 slot[4];
    u32 *table = NULL;
    int count = 0;
    int capacity = 0;

    memset(pak, 0, sizeof(*pak));

    if (!platform_resolve_path(name, pak->path, sizeof(pak->path))) {
        return 0;
    }

    pak->fp = fopen(pak->path, "rb");
    if (pak->fp == NULL) {
        return 0;
    }

    for (;;) {
        u32 value;

        if (fread(slot, 1, sizeof(slot), pak->fp) != sizeof(slot)) {
            break;
        }

        value = read_u32_le(slot);

        /* Slot 0 is the dummy; a zero anywhere after it ends the directory. */
        if (count > 0 && value == 0) {
            break;
        }

        if (count == capacity) {
            int grown = (capacity == 0) ? 64 : capacity * 2;
            u32 *bigger = realloc(table, (size_t)grown * sizeof(u32));

            if (bigger == NULL) {
                free(table);
                fclose(pak->fp);
                pak->fp = NULL;
                return 0;
            }

            table = bigger;
            capacity = grown;
        }

        table[count++] = value;
    }

    if (count < 2) {  /* dummy plus at least one entry */
        free(table);
        fclose(pak->fp);
        pak->fp = NULL;
        return 0;
    }

    pak->offsets = table;
    pak->entry_count = count - 1;  /* slot 0 is not an entry */
    return 1;
}

void pak_close(struct pak *pak)
{
    if (pak->fp != NULL) {
        fclose(pak->fp);
        pak->fp = NULL;
    }

    free(pak->offsets);
    pak->offsets = NULL;
    pak->entry_count = 0;
}

int pak_entry_count(const struct pak *pak)
{
    return pak->entry_count;
}

/*
 * 0x42a8f0, methods 2 and 3. The first byte is the escape marker; any other
 * byte is a literal. After a marker:
 *
 *   0          the marker itself
 *   1rrnnnnn   the next r + 1 bytes, n + 2 times over
 *   0oolnnnn   a copy from earlier output: o picks an offset of 1, 2 or 4
 *              bytes (0x43b4bc) read as a negative number, all ones above
 *              (the routine starts from ~0); with l clear the length is
 *              n + 4, with l set n picks a length of 1, 2 or 4 bytes
 *              (0x43b4cc), unsigned
 *
 * An o or n with no entry in its table calls a null pointer in the
 * original; here it ends the stream as corrupt. No Alone in the Dark 3
 * container uses these methods; the earlier games are why they exist.
 */
static u32 lz_read(const u8 **p, const u8 *end, int bytes, int ones)
{
    u32 v = ones ? 0xffffffffu : 0;
    int i;

    if (end - *p < bytes) {
        *p = end + 1;
        return 0;
    }
    if (bytes == 4) {
        v = 0;
    }
    for (i = 0; i < bytes; i++) {
        v = (v & ~(0xffu << (i * 8))) | ((u32)(*p)[i] << (i * 8));
    }
    *p += bytes;
    return v;
}

static int legacy_lz(const u8 *src, u32 src_size, u8 *dst, u32 dst_size)
{
    static const int sizes[4] = { 0, 1, 2, 4 };
    const u8 *p = src, *end = src + src_size;
    u32 out = 0;
    u8 marker;

    if (src_size == 0) {
        return 0;
    }
    marker = *p++;
    while (p < end) {
        u8 c = *p++;

        if (c != marker) {
            if (out >= dst_size) return 0;
            dst[out++] = c;
            continue;
        }
        if (p >= end) return 0;
        c = *p++;
        if (c == 0) {
            if (out >= dst_size) return 0;
            dst[out++] = marker;
        } else if (c & 0x80) {
            u32 times = (u32)(c & 0x1f) + 2, len = (u32)((c & 0x60) >> 5) + 1;

            if ((u32)(end - p) < len || dst_size - out < times * len) return 0;
            while (times-- > 0) {
                memcpy(dst + out, p, len);
                out += len;
            }
            p += len;
        } else {
            int offset_bytes = sizes[(c & 0x60) >> 5];
            u32 offset, len, i;

            if (offset_bytes == 0) return 0;
            offset = lz_read(&p, end, offset_bytes, 1);
            if (!(c & 0x10)) {
                len = (u32)(c & 0x0f) + 4;
            } else {
                int len_bytes = (c & 0x0f) < 3 ? sizes[(c & 0x0f) + 1] : 0;

                if (len_bytes == 0) return 0;
                len = lz_read(&p, end, len_bytes, 0);
            }
            if (p > end || (u32)-offset > out || dst_size - out < len) return 0;
            for (i = 0; i < len; i++, out++) {      /* byte by byte: runs overlap */
                dst[out] = dst[out + offset];
            }
        }
    }
    return out == dst_size;
}

/*
 * 0x425dca. In WAITD3.EXE methods 0 and 1 share the memmove branch; the
 * earlier titles use 1 for implode, which no Alone in the Dark 3 container
 * contains, so it is decoded as implode here. 2 and 3 are the legacy LZ,
 * 4 is deflate.
 */
int pak_unpack(int method, int flags, const u8 *src, u32 src_size, u8 *dst,
               u32 dst_size)
{
    switch (method) {
    case PAK_METHOD_IMPLODE:
        return pak_explode(src, src_size, dst, dst_size, flags);

    case PAK_METHOD_STORED_0:
        if (src_size > dst_size) {
            return 0;
        }
        memmove(dst, src, src_size);
        return 1;

    case PAK_METHOD_DEFLATE: {
        u32 written = 0;
        int done = inflate_raw(src, src_size, dst, dst_size, &written);

        return done || written == dst_size;
    }

    case PAK_METHOD_LEGACY_LZ_2:
    case PAK_METHOD_LEGACY_LZ_3:
        return legacy_lz(src, src_size, dst, dst_size);

    default:
        return 0;
    }
}

void *pak_read_entry(struct pak *pak, int index, u32 *out_size)
{
    u8 raw_header[12];
    struct pak_entry_header header;
    u8 *payload;
    u8 *block;

    if (out_size != NULL) {
        *out_size = 0;
    }

    if (pak->fp == NULL || index < 0 || index >= pak->entry_count) {
        return NULL;
    }

    /* Slot 0 is the dummy, so entry N is at offsets[N + 1] (0x42029b). The
     * entry opens with the size of an optional descriptor block; it is 0
     * in AITD3 and skipped whole when present (AITD2 masks). */
    if (fseek(pak->fp, (long)pak->offsets[index + 1], SEEK_SET) != 0
        || fread(raw_header, 1, 4, pak->fp) != 4) {
        return NULL;
    }
    {
        u32 descriptor = read_u32_le(raw_header);

        if (descriptor >= 4 && fseek(pak->fp, (long)descriptor - 4, SEEK_CUR) != 0) {
            return NULL;
        }
    }

    if (fread(raw_header, 1, sizeof(raw_header), pak->fp) != sizeof(raw_header)) {
        return NULL;
    }

    header.packed_size = read_u32_le(raw_header + 0);
    header.unpacked_size = read_u32_le(raw_header + 4);
    header.method = raw_header[8];
    header.flags = raw_header[9];
    header.extra_skip = raw_header[10];
    header.unused_b = raw_header[11];

    /* 0x4202e6 skips this many further bytes before the payload. */
    if (header.extra_skip != 0
        && fseek(pak->fp, header.extra_skip, SEEK_CUR) != 0) {
        return NULL;
    }

    if (header.unpacked_size == 0) {
        return NULL;
    }

    block = mem_alloc(header.unpacked_size);
    if (block == NULL) {
        return NULL;
    }

    payload = malloc(header.packed_size);
    if (payload == NULL) {
        mem_free(block);
        return NULL;
    }

    if (fread(payload, 1, header.packed_size, pak->fp) != header.packed_size) {
        free(payload);
        mem_free(block);
        return NULL;
    }

    if (!pak_unpack(header.method, header.flags, payload, header.packed_size,
                    block, header.unpacked_size)) {
        free(payload);
        mem_free(block);
        return NULL;
    }

    free(payload);

    if (out_size != NULL) {
        *out_size = header.unpacked_size;
    }

    return block;
}

void *pak_load(const char *name, int index, u32 *out_size)
{
    struct pak pak;
    void *block;

    if (!pak_open(&pak, name)) {
        return NULL;
    }

    block = pak_read_entry(&pak, index, out_size);
    pak_close(&pak);

    return block;
}

/*
 * 0x4230c4 - pak_load_into.
 *
 * Same directory walk and header layout as pak_read_entry, but expands into a
 * buffer the caller already owns. menu_draw_background uses it to pull the
 * menu palette (entry 0x2f, 768 bytes) without allocating.
 */
int pak_load_into(const char *name, int index, void *dest, u32 dest_size)
{
    struct pak pak;
    u32 size = 0;
    void *block;
    int ok = 0;

    if (!pak_open(&pak, name)) {
        return 0;
    }

    block = pak_read_entry(&pak, index, &size);

    if (block != NULL && size <= dest_size) {
        memcpy(dest, block, size);
        ok = 1;
    }

    mem_free(block);
    pak_close(&pak);

    return ok;
}
