/*
 * hmp.c - HMI "HMP" songs to standard MIDI files.
 *
 * The DOS Time Gate keeps its music as HMP (HMIMIDIP013195) in MUSIC.PAK,
 * the same songs the Windows build ships as standard MIDI in WINMUS.PAK.
 * Worked out from the data: the number of tracks is at 0x30 and the
 * tracks start at 0x388, each a 12 byte header (number, length including
 * the header, channel) then MIDI events with running status. Delta times
 * are groups of 7 bits, least significant first, the last group marked by
 * bit 7. Time runs at 120 ticks a second (the song lengths in the header
 * agree), written here as 60 ticks per quarter at 120 beats a minute.
 */

#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "hmp.h"

enum { TRACKS_AT = 0x388, TRACK_HEADER = 12 };

struct out {
    u8 *data;
    u32 size, cap;
};

static int put(struct out *o, const void *p, u32 n)
{
    if (o->size + n > o->cap) {
        u32 cap = (o->size + n) * 2 + 256;
        u8 *grown = realloc(o->data, cap);

        if (grown == NULL) {
            return 0;
        }
        o->data = grown;
        o->cap = cap;
    }
    memcpy(o->data + o->size, p, n);
    o->size += n;
    return 1;
}

static int put_be32(struct out *o, u32 v)
{
    u8 b[4];

    b[0] = (u8)(v >> 24);
    b[1] = (u8)(v >> 16);
    b[2] = (u8)(v >> 8);
    b[3] = (u8)v;
    return put(o, b, 4);
}

static int put_vlq(struct out *o, u32 v)
{
    u8 b[5];
    int n = 0, i;

    b[n++] = (u8)(v & 0x7f);
    while ((v >>= 7) != 0) {
        b[n++] = (u8)(0x80 | (v & 0x7f));
    }
    for (i = n - 1; i >= 0; i--) {
        if (!put(o, &b[i], 1)) {
            return 0;
        }
    }
    return 1;
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* One HMP track as an MTrk chunk. */
static int convert_track(struct out *o, const u8 *p, const u8 *end, int first)
{
    u32 length_at;
    u8 status = 0;

    if (!put(o, "MTrk", 4)) {
        return 0;
    }
    length_at = o->size;
    put_be32(o, 0);
    if (first) {
        /* 120 beats a minute: 500000 microseconds a quarter. */
        static const u8 tempo[] = { 0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20 };
        put(o, tempo, sizeof(tempo));
    }
    while (p < end) {
        u32 delta = 0;
        int shift = 0;
        u8 b;

        do {
            b = *p++;
            delta |= (u32)(b & 0x7f) << shift;
            shift += 7;
        } while (!(b & 0x80) && p < end && shift < 28);
        if (p >= end) {
            break;
        }
        put_vlq(o, delta);
        if (*p == 0xff) {
            u8 type = p[1], len = p[2];

            if (p + 3 + len > end) {
                break;
            }
            put(o, p, 2);
            put_vlq(o, len);
            put(o, p + 3, len);
            p += 3 + len;
            if (type == 0x2f) {
                break;
            }
            continue;
        }
        if (*p == 0xf0 || *p == 0xf7) {
            /* System exclusive: not used by these songs; stop safely. */
            break;
        }
        if (*p & 0x80) {
            status = *p++;
        }
        if (status == 0) {
            break;
        }
        {
            u8 event[3];
            int data = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;

            event[0] = status;
            event[1] = event[2] = 0;

            if (p + data > end) {
                break;
            }
            memcpy(event + 1, p, (size_t)data);
            p += data;
            put(o, event, (u32)(1 + data));
        }
    }
    {
        /* Always close the track. */
        static const u8 eot[] = { 0x00, 0xff, 0x2f, 0x00 };
        if (o->size < 4 || memcmp(o->data + o->size - 3, eot + 1, 3) != 0) {
            put(o, eot, sizeof(eot));
        }
    }
    {
        u32 len = o->size - length_at - 4;
        u8 b[4];

        b[0] = (u8)(len >> 24);
        b[1] = (u8)(len >> 16);
        b[2] = (u8)(len >> 8);
        b[3] = (u8)len;
        memcpy(o->data + length_at, b, 4);
    }
    return 1;
}

u8 *hmp_to_midi(const u8 *hmp, u32 size, u32 *out_size)
{
    struct out o = { NULL, 0, 0 };
    u32 tracks, i, at = TRACKS_AT, written = 0;
    static const u8 header[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };

    if (hmp == NULL || size < TRACKS_AT || memcmp(hmp, "HMIMIDIP", 8) != 0) {
        return NULL;
    }
    tracks = rd32(hmp + 0x30);
    put(&o, header, sizeof(header));
    put(&o, "\0\0\0\x3c", 4);   /* track count (patched below), division 60 */

    for (i = 0; i < tracks && at + TRACK_HEADER <= size; i++) {
        u32 len = rd32(hmp + at + 4);

        if (len < TRACK_HEADER || at + len > size) {
            break;
        }
        if (convert_track(&o, hmp + at + TRACK_HEADER, hmp + at + len, written == 0)) {
            written++;
        }
        at += len;
    }
    if (o.data == NULL || written == 0) {
        free(o.data);
        return NULL;
    }
    o.data[10] = (u8)(written >> 8);
    o.data[11] = (u8)written;
    *out_size = o.size;
    return o.data;
}
