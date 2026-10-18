/*
 * pak.h - .PAK container reader.
 *
 * Derived from file_load_alloc (0x42025a) and pak_unpack (0x425dca).
 * The format is shared by every game this engine targets: Alone in the Dark
 * 1 through 3, Jack in the Dark and Time Gate.
 *
 * Layout
 * ------
 *   u32 offsets[]     directory, terminated by a zero entry. Slot 0 is a
 *                     dummy, so entry N lives at offsets[N + 1]; that is why
 *                     file_load_alloc seeks to index * 4 + 4 (0x42029b).
 *
 *   per entry, at its offset:
 *     u32 reserved        always 0 in observed data; skipped at 0x4202c1
 *     u32 packed_size     bytes on disk after the header
 *     u32 unpacked_size   bytes after decompression
 *     u8  method          0 stored, 1 implode (AITD1/2, Jack; stored in
 *                         WAITD3.EXE), 2 or 3 legacy LZ, 4 deflate
 *     u8  flags           implode options, see explode.c
 *     u8  extra_skip      further bytes to skip before the payload (0x4202e6)
 *     u8  unused
 *
 * The original reads the payload into the tail of the destination buffer and
 * expands forward over it (0x420332..0x42035d), allocating an extra 0x7d0
 * bytes of slack for method 4. This implementation keeps the payload in its
 * own buffer instead, which is simpler and costs one short-lived allocation.
 */

#ifndef AITD_PAK_H
#define AITD_PAK_H

#include <stdio.h>

#include "aitd.h"

/* Values of the method byte, dispatched by pak_unpack at 0x425dca. */
enum pak_method {
    PAK_METHOD_STORED_0 = 0,  /* memmove, 0x425e0b */
    PAK_METHOD_IMPLODE = 1,   /* AITD1/2, Jack; a memmove in WAITD3.EXE */
    PAK_METHOD_LEGACY_LZ_2 = 2, /* 0x42a8f0 */
    PAK_METHOD_LEGACY_LZ_3 = 3, /* same branch */
    PAK_METHOD_DEFLATE = 4      /* 0x42bb75, raw RFC 1951 */
};

/* Slack the original appends for method 4 (0x4202fc). Kept for fidelity when
 * a caller wants a buffer the engine can scribble past the end of. */
#define PAK_DEFLATE_SLACK 0x7d0

struct pak {
    char path[1024];
    FILE *fp;
    u32 *offsets;   /* directory, entry_count + 1 entries */
    int entry_count;
};

/* Opens a container and reads its directory. Returns 0 on failure. */
int pak_open(struct pak *pak, const char *name);
void pak_close(struct pak *pak);

int pak_entry_count(const struct pak *pak);

/* Reads and decompresses one entry. The caller owns the block and frees it
 * with mem_free. out_size may be NULL. */
void *pak_read_entry(struct pak *pak, int index, u32 *out_size);

int pak_load_into(const char *name, int index, void *dest, u32 dest_size);

/* Convenience: open, read one entry, close. This is what pak_load_entry
 * (0x414760) does via file_load_alloc. */
void *pak_load(const char *name, int index, u32 *out_size);

/* Standalone decompressor, mirroring pak_unpack (0x425dca). Returns 0 when
 * the method is unsupported. */
int pak_unpack(int method, int flags, const u8 *src, u32 src_size, u8 *dst,
               u32 dst_size);

/* PKZIP implode decompression (explode.c). */
int pak_explode(const u8 *src, u32 src_size, u8 *dst, u32 dst_size, int flags);

#endif /* AITD_PAK_H */
