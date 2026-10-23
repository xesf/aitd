/*
 * inflate.h - raw deflate decoder (RFC 1951) for PAK method 4.
 */

#ifndef AITD_INFLATE_H
#define AITD_INFLATE_H

#include "aitd.h"

/* Expands `src` into `dst`. Returns 1 when the stream decodes to its end
 * within `dst_size` bytes; `out_size` receives the bytes written. */
int inflate_raw(const u8 *src, u32 src_size, u8 *dst, u32 dst_size, u32 *out_size);

#endif /* AITD_INFLATE_H */
