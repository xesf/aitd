/*
 * explode.c - PKZIP "implode" decompression, PAK method 1 in Alone in the
 * Dark 1 and 2 and Jack in the Dark.
 *
 * The stream opens with two or three Shannon-Fano trees: literals (256
 * codes, only when flag 4 is set), match lengths (64) and the high bits of
 * match distances (64). Each tree is a count byte followed by bytes holding
 * (codes - 1) << 4 | (bit length - 1). Then, LSB first:
 *
 *   1 + literal         a tree code, or 8 raw bits without the literal tree
 *   0 + match           6 (4K window) or 7 (8K window, flag 2) raw distance
 *                       bits, the distance tree, then the length tree; length
 *                       63 adds 8 raw bits; the minimum match is 2, or 3 with
 *                       a literal tree
 *
 * Codes are the canonical codes of their bit lengths with every bit
 * inverted. The flags are the byte after the method byte in the PAK entry.
 * WAITD3.EXE treats method 1 as stored; no Alone in the Dark 3 or Time Gate
 * container uses it.
 */

#include <string.h>

#include "pak.h"

enum {
    EXPLODE_8K_WINDOW = 2,
    EXPLODE_LITERAL_TREE = 4,
    EXPLODE_MAX_BITS = 16
};

struct bit_reader {
    const u8 *src;
    u32 size;
    u32 pos;
    u32 bits;
    int count;
};

struct sf_tree {
    u16 count[EXPLODE_MAX_BITS + 1];
    u16 symbol[256];
};

static u32 get_bits(struct bit_reader *br, int n)
{
    u32 v;

    while (br->count < n) {
        u32 byte = br->pos < br->size ? br->src[br->pos] : 0;

        br->pos++;
        br->bits |= byte << br->count;
        br->count += 8;
    }
    v = br->bits & ((1u << n) - 1);
    br->bits >>= n;
    br->count -= n;
    return v;
}

static int read_tree(struct bit_reader *br, struct sf_tree *t, int codes)
{
    u8 lengths[256];
    u16 offset[EXPLODE_MAX_BITS + 2];
    int bytes = (int)get_bits(br, 8) + 1;
    int n = 0;
    int i;

    while (bytes-- > 0) {
        u32 b = get_bits(br, 8);
        int repeat = (int)(b >> 4) + 1;
        int length = (int)(b & 15) + 1;

        while (repeat-- > 0) {
            if (n >= codes) {
                return 0;
            }
            lengths[n++] = (u8)length;
        }
    }
    if (n != codes) {
        return 0;
    }

    memset(t->count, 0, sizeof(t->count));
    for (i = 0; i < codes; i++) {
        t->count[lengths[i]]++;
    }
    offset[1] = 0;
    for (i = 1; i <= EXPLODE_MAX_BITS; i++) {
        offset[i + 1] = (u16)(offset[i] + t->count[i]);
    }
    for (i = 0; i < codes; i++) {
        t->symbol[offset[lengths[i]]++] = (u16)i;
    }
    return 1;
}

static int decode(struct bit_reader *br, const struct sf_tree *t)
{
    int code = 0, first = 0, index = 0;
    int len;

    for (len = 1; len <= EXPLODE_MAX_BITS; len++) {
        int count = t->count[len];

        code |= (int)(get_bits(br, 1) ^ 1);
        if (code - count < first) {
            return t->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

int pak_explode(const u8 *src, u32 src_size, u8 *dst, u32 dst_size, int flags)
{
    struct bit_reader br = { src, src_size, 0, 0, 0 };
    static struct sf_tree literal, length, distance;
    int has_literal = (flags & EXPLODE_LITERAL_TREE) != 0;
    int low_bits = (flags & EXPLODE_8K_WINDOW) ? 7 : 6;
    int min_match = has_literal ? 3 : 2;
    u32 out = 0;

    if ((has_literal && !read_tree(&br, &literal, 256))
        || !read_tree(&br, &length, 64) || !read_tree(&br, &distance, 64)) {
        return 0;
    }

    while (out < dst_size) {
        if (get_bits(&br, 1)) {
            int c = has_literal ? decode(&br, &literal) : (int)get_bits(&br, 8);

            if (c < 0) {
                return 0;
            }
            dst[out++] = (u8)c;
        } else {
            u32 low = get_bits(&br, low_bits);
            int high = decode(&br, &distance);
            int len = decode(&br, &length);
            u32 dist;

            if (high < 0 || len < 0) {
                return 0;
            }
            dist = (((u32)high << low_bits) | low) + 1;
            if (len == 63) {
                len += (int)get_bits(&br, 8);
            }
            len += min_match;
            while (len-- > 0 && out < dst_size) {
                dst[out] = dist <= out ? dst[out - dist] : 0;
                out++;
            }
        }
        if (br.pos > src_size + 4) {
            return 0;   /* ran off the end of the stream */
        }
    }
    return 1;
}
