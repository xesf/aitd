/*
 * smacker.c - Smacker 2 video, as Time Gate for Windows plays its cut
 * scenes (TG320.EX_ 0x409835 opens ANIM2D\AN<n>.smk through SMACKW32.DLL).
 *
 * Written from the published description of the format: a header, frame
 * sizes and types, four Huffman "big trees" (mono block colours and maps,
 * full block pixels, block types) and per frame a palette delta, audio
 * chunks and the video bit stream. Bits are read least significant first.
 * Audio track 0 (DPCM over byte Huffman trees) is decoded up front and
 * played as one sample. The original's playback loop is not rebuilt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "raster.h"
#include "smacker.h"
#include "subsystems.h"

enum {
    SMK_HEADER = 104,
    SMK_NODE = 0x80000000,
    SMK_FLAG_PALETTE = 1,
    AUDIO_COMPRESSED = 0x80000000,
    AUDIO_PRESENT = 0x40000000,
    AUDIO_16BIT = 0x20000000,
    AUDIO_STEREO = 0x10000000
};

enum smk_block { BLOCK_MONO, BLOCK_FULL, BLOCK_SKIP, BLOCK_FILL };

/* ---- bits ------------------------------------------------------------------ */

struct bits {
    const u8 *data;
    u32 size, pos;   /* pos in bits */
};

static int bit1(struct bits *b)
{
    int v;

    if ((b->pos >> 3) >= b->size) {
        return 0;
    }
    v = (b->data[b->pos >> 3] >> (b->pos & 7)) & 1;
    b->pos++;
    return v;
}

static u32 bitsn(struct bits *b, int n)
{
    u32 v = 0;
    int i;

    for (i = 0; i < n; i++) {
        v |= (u32)bit1(b) << i;
    }
    return v;
}

/* ---- byte trees ------------------------------------------------------------ */

/* Node i: child[0] / child[1], or a leaf when leaf >= 0. */
struct tree8 {
    s16 child[512][2];
    s16 leaf[512];
    int count, present;
};

static int tree8_read(struct bits *b, struct tree8 *t, int depth)
{
    int node = t->count++;

    if (node >= 512 || depth > 32) {
        return -1;
    }
    if (!bit1(b)) {
        t->leaf[node] = (s16)bitsn(b, 8);
        return node;
    }
    t->leaf[node] = -1;
    t->child[node][0] = (s16)tree8_read(b, t, depth + 1);
    t->child[node][1] = (s16)tree8_read(b, t, depth + 1);
    return (t->child[node][0] < 0 || t->child[node][1] < 0) ? -1 : node;
}

static int tree8_load(struct bits *b, struct tree8 *t)
{
    t->count = 0;
    t->present = bit1(b);
    if (t->present) {
        if (tree8_read(b, t, 0) < 0) {
            return 0;
        }
        bit1(b);
    }
    return 1;
}

static int tree8_get(struct bits *b, const struct tree8 *t)
{
    int node = 0;

    if (!t->present) {
        return 0;
    }
    while (t->leaf[node] < 0) {
        node = t->child[node][bit1(b)];
    }
    return t->leaf[node];
}

/* ---- big trees -------------------------------------------------------------- */

struct big_tree {
    u32 *values;
    int count, length;
    int last[3];
};

struct big_ctx {
    struct tree8 *low, *high;
    int escapes[3];
    int last[3];
};

static int big_read(struct bits *b, struct big_tree *t, struct big_ctx *ctx, int depth)
{
    if (t->count + 1 >= t->length || depth > 64) {
        return -1;
    }
    if (!bit1(b)) {
        int val = tree8_get(b, ctx->low) | (tree8_get(b, ctx->high) << 8);
        int i;

        for (i = 0; i < 3; i++) {
            if (val == ctx->escapes[i]) {
                ctx->last[i] = t->count;
                val = 0;
                break;
            }
        }
        t->values[t->count++] = (u32)val;
        return 1;
    } else {
        int node = t->count++;
        int left = big_read(b, t, ctx, depth + 1), right;

        if (left < 0) {
            return -1;
        }
        t->values[node] = SMK_NODE | (u32)left;
        right = big_read(b, t, ctx, depth + 1);
        return right < 0 ? -1 : left + right + 1;
    }
}

static int big_load(struct bits *b, struct big_tree *t, u32 size)
{
    static struct tree8 low, high;
    struct big_ctx ctx;
    int i;

    t->length = (int)((size + 3) >> 2) + 4;
    t->values = calloc((size_t)t->length + 4, sizeof(u32));
    t->count = 0;
    if (t->values == NULL) {
        return 0;
    }
    if (!bit1(b)) {
        /* No tree: every code decodes as 0. */
        t->last[0] = t->last[1] = t->last[2] = 1;
        t->count = 4;
        return 1;
    }
    if (!tree8_load(b, &low) || !tree8_load(b, &high)) {
        return 0;
    }
    ctx.low = &low;
    ctx.high = &high;
    for (i = 0; i < 3; i++) {
        ctx.escapes[i] = (int)bitsn(b, 16);
        ctx.last[i] = -1;
    }
    if (big_read(b, t, &ctx, 0) < 0) {
        return 0;
    }
    bit1(b);
    for (i = 0; i < 3; i++) {
        t->last[i] = ctx.last[i] >= 0 ? ctx.last[i] : t->count++;
    }
    return 1;
}

static void big_reset(struct big_tree *t)
{
    t->values[t->last[0]] = t->values[t->last[1]] = t->values[t->last[2]] = 0;
}

static u32 big_get(struct bits *b, struct big_tree *t)
{
    u32 *p = t->values;
    u32 v;

    while (*p & SMK_NODE) {
        if (bit1(b)) {
            p += *p & ~SMK_NODE;
        }
        p++;
    }
    v = *p;
    if (v != t->values[t->last[0]]) {
        t->values[t->last[2]] = t->values[t->last[1]];
        t->values[t->last[1]] = t->values[t->last[0]];
        t->values[t->last[0]] = v;
    }
    return v;
}

/* ---- the file ----------------------------------------------------------------- */

struct smk {
    u8 *file;
    u32 file_size;
    u32 width, height, frames, flags;
    s32 rate;
    u32 audio_rate[7];
    u32 *frame_size;
    const u8 *frame_type;
    const u8 *frame_data;
    struct big_tree mmap, mclr, full, type;
    u8 palette[768];
    u8 *pixels;
};

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void smk_free(struct smk *s)
{
    free(s->mmap.values);
    free(s->mclr.values);
    free(s->full.values);
    free(s->type.values);
    free(s->frame_size);
    free(s->pixels);
    free(s->file);
}

static int smk_open(struct smk *s, const char *name)
{
    char path[1024];
    FILE *fp;
    long size;
    struct bits b;
    u32 trees_size, i;
    const u8 *p;

    memset(s, 0, sizeof(*s));
    if (!platform_resolve_path(name, path, sizeof(path)) || (fp = fopen(path, "rb")) == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    s->file = malloc((size_t)size);
    if (s->file == NULL || fread(s->file, 1, (size_t)size, fp) != (size_t)size || size < SMK_HEADER
        || memcmp(s->file, "SMK2", 4) != 0) {
        fclose(fp);
        smk_free(s);
        return 0;
    }
    fclose(fp);
    s->file_size = (u32)size;
    p = s->file;
    s->width = rd32(p + 4);
    s->height = rd32(p + 8);
    s->frames = rd32(p + 12);
    s->rate = (s32)rd32(p + 16);
    s->flags = rd32(p + 20);
    trees_size = rd32(p + 52);
    for (i = 0; i < 7; i++) {
        s->audio_rate[i] = rd32(p + 72 + i * 4);
    }
    if (s->width != 320 || s->height != 200 || s->frames == 0
        || SMK_HEADER + s->frames * 5 + trees_size > s->file_size) {
        smk_free(s);
        return 0;
    }
    s->frame_size = malloc(s->frames * sizeof(u32));
    s->pixels = calloc(1, 64000);
    for (i = 0; i < s->frames; i++) {
        s->frame_size[i] = rd32(p + SMK_HEADER + i * 4) & ~3u;
    }
    s->frame_type = p + SMK_HEADER + s->frames * 4;
    b.data = s->frame_type + s->frames;
    b.size = trees_size;
    b.pos = 0;
    if (!big_load(&b, &s->mmap, rd32(p + 56)) || !big_load(&b, &s->mclr, rd32(p + 60))
        || !big_load(&b, &s->full, rd32(p + 64)) || !big_load(&b, &s->type, rd32(p + 68))) {
        smk_free(s);
        return 0;
    }
    s->frame_data = b.data + trees_size;
    return 1;
}

/* Palette delta: 0x80 skip, 0x40 copy from the previous palette, else a
 * new 6 bit colour. */
static const u8 *smk_palette(struct smk *s, const u8 *p, const u8 *end)
{
    u8 old[768];
    u32 size = (u32)p[0] * 4;
    const u8 *next = p + size;
    int n = 0;
    u8 *pal = s->palette;

    memcpy(old, s->palette, sizeof(old));
    p++;
    while (n < 256 && p < next && p < end) {
        int t = *p++;

        if (t & 0x80) {
            n += (t & 0x7f) + 1;
            pal += ((t & 0x7f) + 1) * 3;
        } else if (t & 0x40) {
            int off = *p++, count = (t & 0x3f) + 1;

            while (count-- > 0 && n < 256 && off < 256) {
                memcpy(pal, old + off * 3, 3);
                pal += 3;
                off++;
                n++;
            }
        } else {
            pal[0] = (u8)((t * 255 + 31) / 63);
            pal[1] = (u8)(((p[0] & 0x3f) * 255 + 31) / 63);
            pal[2] = (u8)(((p[1] & 0x3f) * 255 + 31) / 63);
            p += 2;
            pal += 3;
            n++;
        }
    }
    return next;
}

static const int s_block_runs[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048
};

static void smk_video(struct smk *s, const u8 *data, u32 size)
{
    struct bits b = { data, size, 0 };
    int bw = (int)s->width / 4, blocks = bw * ((int)s->height / 4);
    int blk = 0;

    big_reset(&s->mmap);
    big_reset(&s->mclr);
    big_reset(&s->full);
    big_reset(&s->type);
    while (blk < blocks) {
        u32 type = big_get(&b, &s->type);
        int run = s_block_runs[(type >> 2) & 0x3f];

        while (run-- > 0 && blk < blocks) {
            u8 *out = s->pixels + (blk / bw) * 4 * (int)s->width + (blk % bw) * 4;
            int i;

            switch ((enum smk_block)(type & 3)) {
            case BLOCK_MONO: {
                u32 colours = big_get(&b, &s->mclr);
                u32 map = big_get(&b, &s->mmap);
                u8 hi = (u8)(colours >> 8), lo = (u8)colours;

                for (i = 0; i < 4; i++, map >>= 4, out += s->width) {
                    out[0] = (map & 1) ? hi : lo;
                    out[1] = (map & 2) ? hi : lo;
                    out[2] = (map & 4) ? hi : lo;
                    out[3] = (map & 8) ? hi : lo;
                }
                break;
            }
            case BLOCK_FULL:
                for (i = 0; i < 4; i++, out += s->width) {
                    u32 pix = big_get(&b, &s->full);
                    out[2] = (u8)pix;
                    out[3] = (u8)(pix >> 8);
                    pix = big_get(&b, &s->full);
                    out[0] = (u8)pix;
                    out[1] = (u8)(pix >> 8);
                }
                break;
            case BLOCK_SKIP:
                break;
            case BLOCK_FILL:
                for (i = 0; i < 4; i++, out += s->width) {
                    memset(out, (int)(type >> 8), 4);
                }
                break;
            }
            blk++;
        }
    }
}

/* One audio chunk of track 0: unpacked size, then DPCM over byte trees.
 * Appends 8 bit unsigned mono samples. */
static void smk_audio(const u8 *data, u32 size, u8 **out, u32 *len, u32 *cap)
{
    static struct tree8 trees[4];
    struct bits b;
    u32 unpacked, i;
    int stereo, wide, count, pred[2] = { 0, 0 };

    if (size < 4) {
        return;
    }
    unpacked = rd32(data);
    b.data = data + 4;
    b.size = size - 4;
    b.pos = 0;
    if (!bit1(&b)) {
        return;
    }
    stereo = bit1(&b);
    wide = bit1(&b);
    count = 1 << (wide + stereo);
    for (i = 0; i < (u32)count; i++) {
        if (!tree8_load(&b, &trees[i])) {
            return;
        }
    }
    if (*len + unpacked > *cap) {
        u8 *grown;

        *cap = (*len + unpacked) * 2;
        grown = realloc(*out, *cap);
        if (grown == NULL) {
            return;
        }
        *out = grown;
    }
    if (wide) {
        u32 samples = unpacked / 2;

        for (i = (u32)stereo + 1; i-- > 0;) {
            u32 v = bitsn(&b, 16);
            pred[i] = (s16)(((v & 0xff) << 8) | (v >> 8));
        }
        for (i = 0; i < samples; i++) {
            int ch = stereo ? (int)(i & 1) : 0;

            if (i > (u32)stereo) {
                int lo = tree8_get(&b, &trees[ch * 2]);
                int hi = tree8_get(&b, &trees[ch * 2 + 1]);
                pred[ch] = (s16)(pred[ch] + (s16)(lo | (hi << 8)));
            }
            if (!stereo || ch == 0) {
                (*out)[(*len)++] = (u8)((pred[ch] >> 8) ^ 0x80);
            }
        }
    } else {
        for (i = (u32)stereo + 1; i-- > 0;) {
            pred[i] = (int)bitsn(&b, 8);
        }
        for (i = 0; i < unpacked; i++) {
            int ch = stereo ? (int)(i & 1) : 0;

            if (i > (u32)stereo) {
                pred[ch] = (pred[ch] + (s8)tree8_get(&b, &trees[ch])) & 0xff;
            }
            if (!stereo || ch == 0) {
                (*out)[(*len)++] = (u8)pred[ch];
            }
        }
    }
}

/* Walks every frame: palette, audio chunks (track 0 kept), video. */
static void smk_frame_parts(const struct smk *s, u32 frame, const u8 *p,
                            const u8 **video, u32 *video_size, const u8 **pal)
{
    const u8 *end = p + s->frame_size[frame];
    int track;

    *pal = NULL;
    if (s->frame_type[frame] & SMK_FLAG_PALETTE) {
        *pal = p;
        p += (u32)p[0] * 4;
    }
    for (track = 0; track < 7; track++) {
        if (s->frame_type[frame] & (2 << track)) {
            p += rd32(p);
        }
    }
    *video = p;
    *video_size = p < end ? (u32)(end - p) : 0;
}

int smacker_play(const char *name)
{
    struct smk s;
    u8 *audio = NULL;
    u32 audio_len = 0, audio_cap = 0, frame;
    u32 frame_ms;
    const u8 *p;
    u32 clock;
    int skipped = 0;
    u8 *scaled = NULL;

    if (!smk_open(&s, name)) {
        fprintf(stderr, "smacker: cannot open %s\n", name);
        return 0;
    }
    frame_ms = s.rate > 0 ? (u32)s.rate : s.rate < 0 ? (u32)(-s.rate) / 100 : 100;

    /* Audio track 0, whole, before the pictures. */
    if ((s.audio_rate[0] & AUDIO_PRESENT) && (s.audio_rate[0] & AUDIO_COMPRESSED)) {
        for (frame = 0, p = s.frame_data; frame < s.frames; p += s.frame_size[frame], frame++) {
            const u8 *q = p;

            if (s.frame_type[frame] & SMK_FLAG_PALETTE) {
                q += (u32)q[0] * 4;
            }
            if (s.frame_type[frame] & 2) {
                smk_audio(q + 4, rd32(q) - 4, &audio, &audio_len, &audio_cap);
            }
        }
        fprintf(stderr, "smacker: %s, %u frames, %u samples at %u Hz\n", name,
                s.frames, audio_len, s.audio_rate[0] & 0xffffff);
        if (getenv("AITD_SMK_AUDIO") != NULL && audio_len > 0) {   /* harness only */
            FILE *dump = fopen(getenv("AITD_SMK_AUDIO"), "wb");
            if (dump != NULL) {
                fwrite(audio, 1, audio_len, dump);
                fclose(dump);
            }
        }
        if (audio_len > 0) {
            platform_sound_play(audio, audio_len, s.audio_rate[0] & 0xffffff, 1);
        }
    }

    if (g_scale > 1) {
        scaled = malloc(SCREEN_PIXELS);
    }
    raster_push_target(g_back_buffer);
    raster_reset_clip();
    clock = timer_get_ms() * 10;
    for (frame = 0, p = s.frame_data; frame < s.frames && !skipped; p += s.frame_size[frame], frame++) {
        const u8 *video, *pal;
        u32 video_size;
        struct raster_image image = { 320, 200, s.pixels };

        if (p + s.frame_size[frame] > s.file + s.file_size) {
            break;
        }
        smk_frame_parts(&s, frame, p, &video, &video_size, &pal);
        if (pal != NULL) {
            smk_palette(&s, pal, video);
            memcpy(g_palette_buffer, s.palette, sizeof(s.palette));
            palette_set_range(g_palette_buffer, 0, 256);
        }
        smk_video(&s, video, video_size);
        if (g_scale > 1 && scaled != NULL) {
            /* The films are 320 x 200 in both Windows builds; the 640
             * build shows them doubled. */
            int x, y;
            for (y = 0; y < g_screen_height; y++) {
                const u8 *row = s.pixels + (y / g_scale) * 320;
                u8 *out = scaled + (size_t)y * (size_t)g_screen_width;
                for (x = 0; x < g_screen_width; x++) {
                    out[x] = row[x / g_scale];
                }
            }
            image.width = (u16)g_screen_width;
            image.height = (u16)g_screen_height;
            image.pixels = scaled;
        }
        raster_blit(0, 0, &image);
        screen_present();

        clock += frame_ms;
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
    free(scaled);
    free(audio);
    smk_free(&s);
    return 1;
}
