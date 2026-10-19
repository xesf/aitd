/*
 * mux.c - the DOS Time Gate's cut scenes (AN<n>.MUX), read from its
 * player and decoder in TIMEGATE.EXE (an LE image: the player around
 * 0x1a7f0, the chunk handler 0x1a6c0, the decoder 0x3bd79).
 *
 * The file: a 32 byte header (0x0e: audio rate, 0x10: frames, 0x12:
 * frames a second, 0x1c, 0x1e: width and height), then a table of 32 bit
 * entries, offset in the low 24 bits, 0xc0 in the top byte, one per frame.
 * An entry is a run of chunks: type byte, a byte, 32 bit size, data.
 *   0x70 / 0x65  palette, 768 bytes (0..252)
 *   0x6f / 0x64  picture data, below
 *   0x05         frame number          0x06  8 bit unsigned sound
 * Picture data: flags (1: literals packed with a byte LZ, 2: a change
 * frame with skips, 4: both streams packed with a bit LZ), the offset the
 * picture starts at, the sizes of the control stream and the literals,
 * then the streams. Control bits, 32 to a little-endian word, choose
 * between copying a literal and repeating one: a long run (its length a
 * nibble + 4, or a byte + 20 when the nibble is 0, the nibbles of one
 * byte taken in turn), or two, three or four copies; change frames can
 * also skip a byte's worth of pixels.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "menu.h"
#include "mux.h"
#include "palette.h"
#include "platform.h"
#include "raster.h"
#include "subsystems.h"

enum { HEADER = 0x20, PIXELS = 64000 };

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

struct cursor {
    const u8 *p, *end;
};

static u8 next_byte(struct cursor *c)
{
    return c->p < c->end ? *c->p++ : 0;
}

/* Nibbles of one byte, high then low, shared by successive users. */
struct nibbles {
    int odd;
    u8 held;
};

static int nibble(struct nibbles *n, struct cursor *c)
{
    int v;

    if (!n->odd) {
        n->held = next_byte(c);
        v = n->held >> 4;
    } else {
        v = n->held & 15;
    }
    n->odd ^= 1;
    return v;
}

/* 0x3bca0: control and literals packed together with a bit driven LZ. */
static u32 unpack_bits(struct cursor *in, u8 *out, u32 size)
{
    struct nibbles nib = { 0, 0 };
    u32 bits = 0, n = 0;
    int i;

    while (n < size) {
        int bit;

        if (bits == 0) {
            if (in->p + 4 > in->end) {
                break;
            }
            bits = rd32(in->p);
            in->p += 4;
        }
        bit = (int)(bits >> 31);
        bits <<= 1;
        if (bits == 0) {
            continue;       /* the marker bit: load the next word */
        }
        if (bit) {
            int count = nibble(&nib, in) + 1;
            for (i = 0; i < count && n < size; i++) {
                out[n++] = next_byte(in);
            }
        } else {
            u32 back = next_byte(in);
            int count = nibble(&nib, in) + 2;
            for (i = 0; i < count && n < size; i++, n++) {
                out[n] = back <= n ? out[n - back] : 0;
            }
        }
    }
    return n;
}

/* 0x3bd35: literals packed with a byte LZ (bytes below 0x80 are copied,
 * others a backward distance). */
static u32 unpack_bytes(struct cursor *in, u8 *out, u32 size)
{
    struct nibbles nib = { 0, 0 };
    u32 n = 0;
    int i;

    while (n < size && in->p < in->end) {
        u8 b = next_byte(in);

        if (b < 0x80) {
            out[n++] = b;
        } else {
            s32 from = (s32)n + (s8)b;
            int count = nibble(&nib, in) + 2;
            for (i = 0; i < count && n < size; i++, n++, from++) {
                out[n] = from >= 0 ? out[from] : 0;
            }
        }
    }
    return n;
}

struct bitreader {
    struct cursor *c;
    u32 bits;
    int left;
};

static int bit(struct bitreader *b)
{
    int r;

    if (b->left == 0) {
        b->bits = b->c->p + 4 <= b->c->end ? rd32(b->c->p) : 0;
        b->c->p += 4;
        b->left = 32;
    }
    r = (int)(b->bits >> 31);
    b->bits <<= 1;
    b->left--;
    return r;
}

/* 0x3bd79 */
static void decode_picture(const u8 *data, u32 size, u8 *screen, u8 *scratch)
{
    u8 flags;
    u32 at, control, literals;
    struct cursor ctl, lit;
    struct bitreader bits;
    struct nibbles nib = { 0, 0 };
    u32 di;

    if (size < 13) {
        return;
    }
    flags = data[0];
    at = rd32(data + 1);
    control = rd32(data + 5);
    literals = rd32(data + 9);
    if (control > PIXELS * 2 || literals > PIXELS * 2) {
        return;
    }

    if (flags & 4) {
        struct cursor in = { data + 13, data + size };
        u32 got = unpack_bits(&in, scratch, control + literals);

        ctl.p = scratch;
        ctl.end = scratch + PIXELS * 4;   /* reads run on, as the original's */
        lit.p = scratch + control;
        lit.end = scratch + (got < control + literals ? got : control + literals);
    } else if (flags & 1) {
        struct cursor in = { data + 13, data + size };
        u32 got = unpack_bytes(&in, scratch, literals);

        lit.p = scratch;
        lit.end = scratch + got;
        ctl.p = in.p;       /* the control stream follows */
        ctl.end = data + size;
    } else {
        ctl.p = data + 13;
        ctl.end = data + size;
        lit.p = ctl.p + control;
        lit.end = lit.p + literals;
        if (lit.end > data + size) {
            lit.end = data + size;
        }
    }
    bits.c = &ctl;
    bits.left = 0;
    bits.bits = 0;

    for (di = at; lit.p < lit.end && di < PIXELS;) {
        u8 value;
        u32 count;

        if (!bit(&bits)) {
            screen[di++] = next_byte(&lit);
            continue;
        }
        if (flags & 2) {
            if (!bit(&bits)) {
                value = next_byte(&lit);
                count = (u32)nibble(&nib, &ctl);
                count = count ? count + 4 : next_byte(&ctl) + 0x14u;
            } else if (!bit(&bits)) {
                value = next_byte(&lit);
                count = 2;
            } else if (!bit(&bits)) {
                value = next_byte(&lit);
                count = 3;
            } else if (!bit(&bits)) {
                value = next_byte(&lit);
                count = 4;
            } else {
                di += next_byte(&ctl);     /* unchanged pixels */
                continue;
            }
        } else {
            value = next_byte(&lit);
            if (!bit(&bits)) {
                count = (u32)nibble(&nib, &ctl);
                count = count ? count + 4 : next_byte(&ctl) + 0x14u;
            } else if (!bit(&bits)) {
                count = 2;
            } else if (!bit(&bits)) {
                count = 3;
            } else {
                count = 4;
            }
        }
        if (di + count > PIXELS) {
            count = PIXELS - di;
        }
        memset(screen + di, value, count);
        di += count;
    }
}

int mux_play(const char *name)
{
    char path[1024];
    FILE *fp;
    long size;
    u8 *file, *screen, *scratch, *scaled = NULL, *audio = NULL;
    u32 entries = 0, i, audio_len = 0, rate, fps, clock;
    int skipped = 0;

    if (!platform_resolve_path(name, path, sizeof(path)) || (fp = fopen(path, "rb")) == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    file = malloc((size_t)size);
    if (file == NULL || size < HEADER || fread(file, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(file);
        return 0;
    }
    fclose(fp);
    rate = (u32)(file[0x0e] | (file[0x0f] << 8));
    fps = (u32)(file[0x12] | (file[0x13] << 8));
    if (fps == 0) {
        fps = 12;
    }
    while (HEADER + entries * 4 + 4 <= (u32)size && (file[HEADER + entries * 4 + 3] & 0xc0) == 0xc0) {
        entries++;
    }
    screen = calloc(1, PIXELS);
    scratch = calloc(1, PIXELS * 4);
    if (g_scale > 1) {
        scaled = malloc(SCREEN_PIXELS);
    }

    /* The sound, all of it, before the pictures (type 6 chunks). */
    for (i = 0; i < entries; i++) {
        u32 p = rd32(file + HEADER + i * 4) & 0xffffff;
        u32 end = i + 1 < entries ? rd32(file + HEADER + (i + 1) * 4) & 0xffffff : (u32)size;

        while (p + 6 <= end && end <= (u32)size) {
            u32 len = rd32(file + p + 2);

            if (file[p] == 6 && p + 6 + len <= end) {
                u8 *grown = realloc(audio, audio_len + len);
                if (grown != NULL) {
                    audio = grown;
                    memcpy(audio + audio_len, file + p + 6, len);
                    audio_len += len;
                }
            }
            p += 6 + len;
        }
    }
    if (audio_len > 0 && rate > 0) {
        platform_sound_play(audio, audio_len, rate, 1);
    }

    raster_push_target(g_back_buffer);
    raster_reset_clip();
    clock = timer_get_ms() * 10;
    for (i = 0; i < entries && !skipped; i++) {
        u32 p = rd32(file + HEADER + i * 4) & 0xffffff;
        u32 end = i + 1 < entries ? rd32(file + HEADER + (i + 1) * 4) & 0xffffff : (u32)size;
        struct raster_image image = { 320, 200, screen };

        while (p + 6 <= end && end <= (u32)size) {
            u32 len = rd32(file + p + 2);
            u8 type = file[p];

            if (p + 6 + len > end) {
                break;
            }
            if ((type == 0x70 || type == 0x65) && len >= AITD_PALETTE_BYTES) {
                memcpy(g_palette_buffer, file + p + 6, AITD_PALETTE_BYTES);
                palette_set_range(g_palette_buffer, 0, 256);
            } else if (type == 0x6f || type == 0x64) {
                decode_picture(file + p + 6, len, screen, scratch);
            }
            p += 6 + len;
        }
        if (getenv("AITD_MUX_FRAME") != NULL && (u32)atoi(getenv("AITD_MUX_FRAME")) == i) {
            FILE *dump = fopen(getenv("AITD_MUX_DUMP"), "wb");   /* harness only */
            if (dump != NULL) {
                fwrite(screen, 1, PIXELS, dump);
                fclose(dump);
            }
        }
        if (scaled != NULL) {
            int x, y;
            for (y = 0; y < g_screen_height; y++) {
                for (x = 0; x < g_screen_width; x++) {
                    scaled[y * g_screen_width + x] = screen[(y / g_scale) * 320 + x / g_scale];
                }
            }
            image.width = (u16)g_screen_width;
            image.height = (u16)g_screen_height;
            image.pixels = scaled;
        }
        raster_blit(0, 0, &image);
        screen_present();

        clock += 1000 / fps;
        do {
            input_poll();
            if ((g_last_scancode & 0xff) == SC_ESC) {
                skipped = 1;
                break;
            }
        } while (timer_get_ms() * 10 < clock);
    }
    raster_pop_target();
    platform_sound_stop();
    while ((g_last_scancode & 0xff) != 0) {
        input_poll();
    }
    free(audio);
    free(scaled);
    free(scratch);
    free(screen);
    free(file);
    return 1;
}
