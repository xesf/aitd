/*
 * inflate.c - raw deflate decoder (RFC 1951) for PAK method 4.
 *
 * The original's inflate_raw (0x42bb75) reads one final-flag bit and two
 * block-type bits per block, then a stored (0x42b2d0), fixed Huffman
 * (0x42b3eb) or dynamic Huffman (0x42b5c7) block, LSB first. This decoder
 * follows the RFC and keeps the engine free of zlib, which the DOS, console
 * and handheld targets do not have.
 *
 * Huffman codes are decoded canonically, one bit at a time against the
 * per-length counts. That is slower than a lookup table but small, and the
 * engine only inflates when it loads a resource.
 */

#include <string.h>

#include "inflate.h"

#define MAX_BITS 15
#define LITLEN_CODES 288
#define DIST_CODES 30

struct stream {
    const u8 *in;
    u32 in_size, in_pos;
    u32 bit_buffer;
    int bit_count;
    u8 *out;
    u32 out_size, out_pos;
    int error;
};

struct huffman {
    u16 count[MAX_BITS + 1];    /* codes of each length */
    u16 symbol[LITLEN_CODES];   /* symbols ordered by code */
};

static const u16 s_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 s_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 s_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const u8 s_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
/* The order code length code lengths are sent in. */
static const u8 s_clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static u32 bits(struct stream *s, int n)
{
    u32 value;

    while (s->bit_count < n) {
        if (s->in_pos >= s->in_size) {
            s->error = 1;
            return 0;
        }
        s->bit_buffer |= (u32)s->in[s->in_pos++] << s->bit_count;
        s->bit_count += 8;
    }
    value = s->bit_buffer & ((1u << n) - 1u);
    s->bit_buffer >>= n;
    s->bit_count -= n;
    return value;
}

/* Builds the canonical code from `n` lengths. Returns 0 when the lengths
 * over-subscribe the code space. */
static int build(struct huffman *h, const u8 *lengths, int n)
{
    u16 offset[MAX_BITS + 1];
    int left = 1;
    int i;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    for (i = 1; i <= MAX_BITS; i++) {
        left = (left << 1) - h->count[i];
        if (left < 0) {
            return 0;
        }
    }
    offset[1] = 0;
    for (i = 1; i < MAX_BITS; i++) {
        offset[i + 1] = (u16)(offset[i] + h->count[i]);
    }
    for (i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offset[lengths[i]]++] = (u16)i;
        }
    }
    return 1;
}

static int decode(struct stream *s, const struct huffman *h)
{
    int code = 0, first = 0, index = 0;
    int len;

    for (len = 1; len <= MAX_BITS; len++) {
        int count = h->count[len];

        code |= (int)bits(s, 1);
        if (s->error) {
            return -1;
        }
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->error = 1;
    return -1;
}

static void put(struct stream *s, u8 value)
{
    if (s->out_pos >= s->out_size) {
        s->error = 1;
        return;
    }
    s->out[s->out_pos++] = value;
}

static void codes(struct stream *s, const struct huffman *lit, const struct huffman *dist)
{
    for (;;) {
        int symbol = decode(s, lit);

        if (s->error) {
            return;
        }
        if (symbol < 256) {
            put(s, (u8)symbol);
        } else if (symbol == 256) {
            return;
        } else {
            u32 length, distance;

            symbol -= 257;
            if (symbol >= 29) {
                s->error = 1;
                return;
            }
            length = s_length_base[symbol] + bits(s, s_length_extra[symbol]);
            symbol = decode(s, dist);
            if (s->error || symbol < 0 || symbol >= DIST_CODES) {
                s->error = 1;
                return;
            }
            distance = s_dist_base[symbol] + bits(s, s_dist_extra[symbol]);
            if (s->error || distance > s->out_pos) {
                s->error = 1;
                return;
            }
            while (length-- > 0 && !s->error) {
                put(s, s->out[s->out_pos - distance]);
            }
        }
        if (s->error) {
            return;
        }
    }
}

static void stored(struct stream *s)
{
    u32 length;

    s->bit_buffer = 0;      /* to the byte boundary */
    s->bit_count = 0;
    if (s->in_pos + 4 > s->in_size) {
        s->error = 1;
        return;
    }
    length = (u32)s->in[s->in_pos] | ((u32)s->in[s->in_pos + 1] << 8);
    if ((length ^ 0xffffu) != ((u32)s->in[s->in_pos + 2] | ((u32)s->in[s->in_pos + 3] << 8))) {
        s->error = 1;
        return;
    }
    s->in_pos += 4;
    if (s->in_pos + length > s->in_size || s->out_pos + length > s->out_size) {
        s->error = 1;
        return;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, length);
    s->in_pos += length;
    s->out_pos += length;
}

static void fixed(struct stream *s)
{
    static struct huffman lit, dist;
    static int built;

    if (!built) {
        u8 lengths[LITLEN_CODES];
        int i;

        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < LITLEN_CODES; i++) lengths[i] = 8;
        build(&lit, lengths, LITLEN_CODES);
        for (i = 0; i < DIST_CODES; i++) lengths[i] = 5;
        build(&dist, lengths, DIST_CODES);
        built = 1;
    }
    codes(s, &lit, &dist);
}

static void dynamic(struct stream *s)
{
    struct huffman lit, dist;
    u8 lengths[LITLEN_CODES + DIST_CODES + 2];
    int nlen, ndist, ncode, i;

    nlen = (int)bits(s, 5) + 257;
    ndist = (int)bits(s, 5) + 1;
    ncode = (int)bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > DIST_CODES) {
        s->error = 1;
        return;
    }

    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < ncode; i++) {
        lengths[s_clen_order[i]] = (u8)bits(s, 3);
    }
    if (s->error || !build(&lit, lengths, 19)) {
        s->error = 1;
        return;
    }

    for (i = 0; i < nlen + ndist;) {
        int symbol = decode(s, &lit);
        int repeat;
        u8 value = 0;

        if (s->error) {
            return;
        }
        if (symbol < 16) {
            lengths[i++] = (u8)symbol;
            continue;
        }
        if (symbol == 16) {
            if (i == 0) {
                s->error = 1;
                return;
            }
            value = lengths[i - 1];
            repeat = 3 + (int)bits(s, 2);
        } else if (symbol == 17) {
            repeat = 3 + (int)bits(s, 3);
        } else {
            repeat = 11 + (int)bits(s, 7);
        }
        if (s->error || i + repeat > nlen + ndist) {
            s->error = 1;
            return;
        }
        while (repeat-- > 0) {
            lengths[i++] = value;
        }
    }

    if (!build(&lit, lengths, nlen) || !build(&dist, lengths + nlen, ndist)) {
        s->error = 1;
        return;
    }
    codes(s, &lit, &dist);
}

int inflate_raw(const u8 *src, u32 src_size, u8 *dst, u32 dst_size, u32 *out_size)
{
    struct stream s;
    int last;

    memset(&s, 0, sizeof(s));
    s.in = src;
    s.in_size = src_size;
    s.out = dst;
    s.out_size = dst_size;

    do {
        last = (int)bits(&s, 1);
        switch (bits(&s, 2)) {
        case 0: stored(&s); break;
        case 1: fixed(&s); break;
        case 2: dynamic(&s); break;
        default: s.error = 1; break;
        }
    } while (!last && !s.error);

    if (out_size != NULL) {
        *out_size = s.out_pos;
    }
    return !s.error;
}
