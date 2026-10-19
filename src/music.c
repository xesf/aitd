/*
 * music.c - the AdLib music of Alone in the Dark 1, AITD2 and Jack.
 *
 * LISTMUS.PAK tunes ("ADLM") are played by the ADLIBM.COM driver shipped
 * with those games (a PKLITE packed TSR on interrupt 0xf0). This is a
 * translation of that driver's logic, read from its unpacked code; the
 * driver offsets are given for reference.
 *
 * A tune: "ADLM", then for each of 11 voices a 32 bit offset to its
 * command stream (0: unused) at +8, the instrument bank offset at +0x34,
 * the 0xbd register bits at +0x3c and at +0x3d nonzero for nine melodic
 * channels, zero for six melodic plus five rhythm voices. An instrument is
 * 13 bytes: one unused, then two operators of 6 (KSL/TL, 0x20, 0xc0,
 * 0xe0, 0x60, 0x80 values). Commands are byte pairs, opcode and argument;
 * an opcode with bit 7 set ends the batch for this tick:
 *   0 end (loop when looping is on, else stop)   1 wait (+ one more byte)
 *   2 note (semitone low nibble, octave bits 4-6, bit 7 rest)
 *   3 instrument   4 attenuation   5 pitch bend   6 game callback
 * The driver is stepped about 59 times a second.
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "music.h"
#include "opl.h"

enum {
    VOICES = 11,
    FLAG_RESTART = 0x0002,      /* read from the start next tick */
    FLAG_LOOP = 0x0020,
    FLAG_MUTED = 0x0040,
    INSTRUMENT_SIZE = 13,
    UPDATE_HZ_X100 = 5882       /* 1000 / 17 ms ticks */
};

struct voice {
    int index;
    u16 flags;
    s32 start, cur;             /* offsets in the tune, -1: none */
    u16 count, reload;
    u8 instrument;
    u16 tempo;
    u16 note;                   /* bit 15: already played */
    s8 bend;
    u16 notes;
    u8 target, fade_count, fade_speed, volume, attenuation;
};

struct shadow {
    u16 note;
    u16 muted;
    u8 instrument, level, bend;
};

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static u8 *s_tune;
static u32 s_tune_size;
static s32 s_bank;              /* instrument bank offset */
static struct voice s_voices[VOICES];
static struct shadow s_shadow[VOICES];
static int s_melodic;           /* driver 0x711 */
static u8 s_bd;                 /* driver 0x710 */
static int s_paused = 1;
static int s_rate;
static u32 s_until_update;

/* Operator register offsets per voice (driver 0x41d / 0x435). */
static const u8 s_ops_melodic[VOICES][2] = {
    { 0x00, 0x03 }, { 0x01, 0x04 }, { 0x02, 0x05 }, { 0x08, 0x0b }, { 0x09, 0x0c },
    { 0x0a, 0x0d }, { 0x10, 0x13 }, { 0x11, 0x14 }, { 0x12, 0x15 }, { 0xff, 0xff }, { 0xff, 0xff }
};
static const u8 s_ops_rhythm[VOICES][2] = {
    { 0x00, 0x03 }, { 0x01, 0x04 }, { 0x02, 0x05 }, { 0x08, 0x0b }, { 0x09, 0x0c },
    { 0x0a, 0x0d }, { 0x10, 0x13 }, { 0x14, 0xff }, { 0x12, 0xff }, { 0x15, 0xff }, { 0x11, 0xff }
};

/* Channel of an operator offset, for its 0xc0 register (driver 0x6cf). */
static const s8 s_feedback_channel[0x16] = {
    0, 1, 2, -1, -1, -1, -1, -1, 3, 4, 5, -1, -1, -1, -1, -1, 6, 7, 8, -1, -1, -1
};

/* 0xbd bits of the rhythm voices 6..10: bass drum, snare, tom, cymbal,
 * hi-hat (driver 0x6e5). */
static const u8 s_rhythm_bit[5] = { 0x10, 0x08, 0x04, 0x02, 0x01 };

/* F-numbers, 25 steps of bend per semitone from C up (driver 0x463); the
 * driver reads them through 12 pointers, one per semitone, so a negative
 * bend reaches back into the semitone below. Values from the driver. */
static const u16 s_fnum[12 * 25] = {
    0x157, 0x158, 0x159, 0x15a, 0x15a, 0x15b, 0x15c, 0x15d, 0x15e, 0x15f,
    0x15f, 0x160, 0x161, 0x162, 0x163, 0x164, 0x164, 0x165, 0x166, 0x167,
    0x168, 0x168, 0x169, 0x16a, 0x16b, 0x16c, 0x16d, 0x16d, 0x16e, 0x16f,
    0x170, 0x171, 0x172, 0x173, 0x174, 0x174, 0x175, 0x176, 0x177, 0x178,
    0x179, 0x17a, 0x17b, 0x17b, 0x17c, 0x17d, 0x17e, 0x17f, 0x180, 0x181,
    0x181, 0x183, 0x183, 0x184, 0x185, 0x186, 0x187, 0x188, 0x189, 0x18a,
    0x18b, 0x18c, 0x18d, 0x18e, 0x18e, 0x18f, 0x190, 0x191, 0x192, 0x193,
    0x194, 0x195, 0x196, 0x197, 0x198, 0x198, 0x19a, 0x19a, 0x19b, 0x19c,
    0x19d, 0x19f, 0x19f, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a4, 0x1a5, 0x1a6,
    0x1a7, 0x1a8, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad, 0x1ae, 0x1af, 0x1b0,
    0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b5, 0x1b6, 0x1b7, 0x1b8, 0x1b9, 0x1ba,
    0x1bb, 0x1bc, 0x1bd, 0x1bf, 0x1c0, 0x1c1, 0x1c2, 0x1c3, 0x1c4, 0x1c5,
    0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce, 0x1cf,
    0x1d0, 0x1d2, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d7, 0x1d8, 0x1d9, 0x1da,
    0x1db, 0x1dd, 0x1de, 0x1df, 0x1e0, 0x1e1, 0x1e2, 0x1e3, 0x1e4, 0x1e5,
    0x1e6, 0x1e8, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ef, 0x1f0, 0x1f1,
    0x1f2, 0x1f3, 0x1f4, 0x1f6, 0x1f7, 0x1f8, 0x1f9, 0x1fa, 0x1fb, 0x1fd,
    0x1fe, 0x1ff, 0x200, 0x201, 0x202, 0x203, 0x205, 0x206, 0x207, 0x208,
    0x20a, 0x20b, 0x20c, 0x20d, 0x20f, 0x210, 0x211, 0x212, 0x214, 0x215,
    0x216, 0x217, 0x219, 0x21a, 0x21b, 0x21c, 0x21d, 0x21f, 0x220, 0x221,
    0x222, 0x224, 0x225, 0x226, 0x227, 0x229, 0x22a, 0x22c, 0x22d, 0x22e,
    0x22f, 0x231, 0x232, 0x234, 0x235, 0x236, 0x237, 0x239, 0x23a, 0x23b,
    0x23c, 0x23e, 0x23f, 0x241, 0x242, 0x243, 0x245, 0x246, 0x247, 0x248,
    0x24a, 0x24b, 0x24d, 0x24e, 0x250, 0x251, 0x252, 0x254, 0x255, 0x257,
    0x258, 0x259, 0x25b, 0x25c, 0x25e, 0x25f, 0x260, 0x262, 0x263, 0x264,
    0x266, 0x267, 0x269, 0x26a, 0x26b, 0x26d, 0x26e, 0x270, 0x271, 0x273,
    0x274, 0x276, 0x277, 0x279, 0x27a, 0x27c, 0x27d, 0x27f, 0x280, 0x282,
    0x283, 0x285, 0x286, 0x288, 0x289, 0x28a, 0x28c, 0x28d, 0x28f, 0x291,
    0x292, 0x294, 0x295, 0x297, 0x299, 0x29a, 0x29c, 0x29d, 0x29f, 0x2a0,
    0x2a2, 0x2a3, 0x2a5, 0x2a7, 0x2a8, 0x2aa, 0x2ab, 0x2ad, 0x2af, 0x2b0,
};

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static void reset_shadow(int i)        /* driver 0x7c7 */
{
    s_shadow[i].level = 0xff;
    s_shadow[i].instrument = 0xff;
    s_shadow[i].muted = FLAG_MUTED;
    s_shadow[i].note = 0xffff;
    s_shadow[i].bend = 0x9c;
}

/* Frequency and key of an OPL channel (driver 0x900). `retrigger` keys
 * off first; `off` leaves the key up. */
static void set_frequency(int channel, u8 note, int bend, int retrigger, int off)
{
    int semitone = note & 15, octave = (note >> 4) & 7;
    int index = semitone * 25 + bend;
    u16 f;

    if (retrigger) {
        opl_write(0xb0 + channel, 0);
    }
    if (index < 0) {
        index += 12 * 25;
        if (octave > 0) {
            octave--;
        }
    }
    if (index >= 12 * 25) {
        index -= 12 * 25;
        octave++;
    }
    if (note & 0x80) {
        off = 1;
    }
    f = s_fnum[index];
    opl_write(0xa0 + channel, f & 0xff);
    opl_write(0xb0 + channel, (octave << 2) + (f >> 8) + (off ? 0 : 0x20));
}

/* A voice's key, with the rhythm voices on register 0xbd (driver 0x871). */
static void key(int voice, u8 note, int bend, int retrigger, int off)
{
    u8 bit, bd;

    if (s_melodic || voice < 6) {
        set_frequency(voice, note, bend, retrigger, off);
        return;
    }
    if (voice == 6) {
        set_frequency(6, note, bend, retrigger, 1);
    } else if (voice == 8 && !(note & 0x80)) {
        /* The tom; the snare / hi-hat channel sits a fifth above it. */
        int octave = note & 0x70, semitone = (note & 15) + 7;

        set_frequency(8, note, bend, retrigger, 1);
        if (semitone >= 12) {
            semitone -= 12;
            if (octave != 0x70) {
                octave += 0x10;
            }
        }
        set_frequency(7, (u8)(octave | semitone), bend, retrigger, 1);
    }
    bit = s_rhythm_bit[voice - 6];
    bd = (u8)(s_bd & ~bit);
    opl_write(0xbd, bd);
    if (!off && !(note & 0x80)) {
        bd |= bit;
        opl_write(0xbd, bd);
    }
    s_bd = bd;
}

/* Operator registers of an instrument (driver 0x971). */
static void load_operator(const u8 *op, u8 offset)
{
    s8 channel = offset < 0x16 ? s_feedback_channel[offset] : -1;

    if (channel >= 0) {
        opl_write(0xc0 + channel, op[2]);
    }
    opl_write(0x60 + offset, op[4]);
    opl_write(0x80 + offset, op[5]);
    opl_write(0x20 + offset, op[1]);
    opl_write(0xe0 + offset, op[3]);
}

/* Total level scaled by a volume 0..127 (driver 0x831). */
static void set_level(const u8 *op, u8 offset, int volume)
{
    int level;

    if (offset == 0xff) {
        return;
    }
    level = 63 - (((op[0] & 0x3f) * volume * 2 + 0x7f) / 0xfe);
    opl_write(0x40 + offset, (op[0] & 0xc0) | level);
}

static const u8 *instrument(u8 n)
{
    s32 at = s_bank + n * INSTRUMENT_SIZE;

    if (s_bank < 0 || at + INSTRUMENT_SIZE > (s32)s_tune_size) {
        return NULL;
    }
    return s_tune + at;
}

/* Brings the OPL in line with a voice (driver 0xb83). */
static void apply(struct voice *v)
{
    struct shadow *sh = &s_shadow[v->index];
    const u8 (*ops)[2] = s_melodic ? s_ops_melodic : s_ops_rhythm;
    u8 op1 = ops[v->index][0], op2 = ops[v->index][1];
    u16 muted = v->flags & FLAG_MUTED;
    const u8 *ins;
    int level;

    if (muted != sh->muted) {
        sh->muted = muted;
        if (muted) {
            key(v->index, (u8)(v->note | 0x80), v->bend, 1, 1);
            reset_shadow(v->index);
            return;
        }
    } else if (muted) {
        return;
    }
    if (op1 == 0xff) {
        return;
    }
    if (v->instrument != sh->instrument) {
        sh->instrument = v->instrument;
        ins = instrument(v->instrument);
        if (ins != NULL) {
            load_operator(ins + 1, op1);
            if (op2 != 0xff) {
                load_operator(ins + 7, op2);
            }
        }
        sh->level = 0xff;
    }
    level = v->volume - v->attenuation;
    if (level < 0) {
        level = 0;
    }
    if (level != sh->level) {
        sh->level = (u8)level;
        ins = instrument(v->instrument);
        if (ins != NULL) {
            /* Two operators: the modulator keeps the voice volume. */
            set_level(ins + 1, op1, op2 == 0xff ? level : v->volume);
            set_level(ins + 7, op2, level);
        }
    }
    if ((u8)v->bend != sh->bend) {
        int glide = v->note == sh->note;

        sh->bend = (u8)v->bend;
        v->note |= 0x8000;
        sh->note = v->note;
        key(v->index, (u8)v->note, v->bend, !glide, 0);
    } else if (v->note != sh->note) {
        v->note |= 0x8000;
        sh->note = v->note;
        key(v->index, (u8)v->note, v->bend, 1, 0);
    }
}

/* One tick of a voice's command stream (driver 0xa4f). */
static void step(struct voice *v)
{
    if (v->flags & FLAG_MUTED) {
        return;
    }
    if (v->flags & FLAG_RESTART) {
        v->cur = v->start;
        v->flags &= ~FLAG_RESTART;
        v->notes = 0;
    } else {
        if (v->target != v->volume) {
            if (v->fade_count-- == 0) {
                int vol = v->volume + (v->target > v->volume ? 1 : -1);

                v->fade_count = v->fade_speed;
                v->volume = (u8)(vol < 0 ? 0 : vol > 127 ? 127 : vol);
            }
        }
        if (--v->count != 0) {
            return;
        }
        v->count = v->reload;
    }

    for (;;) {
        u8 op, arg;
        int more;

        if (v->cur < 0 || (u32)v->cur + 2 > s_tune_size) {
            v->flags |= FLAG_MUTED;
            return;
        }
        op = s_tune[v->cur];
        arg = s_tune[v->cur + 1];
        v->cur += 2;
        more = !(op & 0x80);
        switch (op & 0x7f) {
        case 0:
            v->flags |= FLAG_RESTART;
            if (!(v->flags & FLAG_LOOP)) {
                v->flags |= FLAG_MUTED;
            }
            break;
        case 1:
            if ((u32)v->cur < s_tune_size) {
                v->reload = (u16)(((s_tune[v->cur] << 8) | arg) + v->tempo);
                v->count = v->reload;
                v->cur++;
            }
            break;
        case 2:
            v->notes++;
            v->note = arg;
            break;
        case 3:
            v->instrument = arg;
            break;
        case 4:
            v->attenuation = arg;
            break;
        case 5:
            v->bend = (s8)arg;
            break;
        default:
            break;          /* 6: a callback into the game, 7..9 nothing */
        }
        if (!more || (v->flags & FLAG_MUTED)) {
            return;
        }
    }
}

static void update(void)               /* driver 0xb24 */
{
    int i;

    if (s_paused || s_tune == NULL) {
        return;
    }
    for (i = 0; i < VOICES; i++) {
        step(&s_voices[i]);
        apply(&s_voices[i]);
    }
}

static void reset_chip(void)           /* driver 0x9bb */
{
    int i;

    opl_write(0x01, 0x20);
    opl_write(0x08, 0x00);
    opl_write(0xbd, s_bd);
    for (i = 0; i < 18; i++) {
        static const u8 offsets[18] = {
            0x00, 0x03, 0x01, 0x04, 0x02, 0x05, 0x08, 0x0b, 0x09,
            0x0c, 0x0a, 0x0d, 0x10, 0x13, 0x11, 0x14, 0x12, 0x15
        };
        opl_write(0x60 + offsets[i], 0xff);
        opl_write(0x80 + offsets[i], 0xff);
    }
    for (i = 0; i < 9; i++) {
        opl_write(0xa0 + i, 0);
        opl_write(0xb0 + i, 0);
    }
    for (i = 0; i < VOICES; i++) {
        reset_shadow(i);
    }
    if (!s_melodic) {
        set_frequency(6, 0, 0, 0, 1);
        set_frequency(7, 7, 0, 0, 1);
        set_frequency(8, 0, 0, 0, 1);
    }
}

void music_adlib_play(const u8 *data, u32 size)
{
    int i;

    if (data == NULL || size < 0x40 || memcmp(data, "ADLM", 4) != 0) {
        return;
    }
    pthread_mutex_lock(&s_lock);
    free(s_tune);
    s_tune = malloc(size);
    if (s_tune == NULL) {
        pthread_mutex_unlock(&s_lock);
        return;
    }
    memcpy(s_tune, data, size);
    s_tune_size = size;

    /* Load (driver 0xe76). */
    s_melodic = s_tune[0x3d];
    s_bd = (u8)((s_tune[0x3c] & 0xc0) | (s_melodic ? 0 : 0x20));
    s_bank = rd16(s_tune + 0x34);
    for (i = 0; i < VOICES; i++) {
        struct voice *v = &s_voices[i];
        u16 lo = rd16(s_tune + 8 + i * 4), hi = rd16(s_tune + 10 + i * 4);

        memset(v, 0, sizeof(*v));
        v->index = i;
        v->start = (lo | hi) ? lo : -1;
        v->flags = FLAG_MUTED;
    }
    reset_chip();

    /* Start every voice at full volume (driver 0xca2, flag 0x80). */
    for (i = 0; i < VOICES; i++) {
        struct voice *v = &s_voices[i];

        if (v->start < 0) {
            continue;
        }
        v->volume = v->target = 0x7f;
        v->attenuation = 0;
        v->fade_speed = v->fade_count = 1;
        v->flags = FLAG_RESTART;
    }
    s_paused = 0;
    pthread_mutex_unlock(&s_lock);
}

void music_adlib_stop(void)
{
    int i;

    pthread_mutex_lock(&s_lock);
    for (i = 0; i < VOICES; i++) {
        s_voices[i].flags |= FLAG_MUTED;
        apply(&s_voices[i]);
    }
    s_paused = 1;
    pthread_mutex_unlock(&s_lock);
}

void music_adlib_render(short *out, int frames, int rate)
{
    pthread_mutex_lock(&s_lock);
    if (rate != s_rate) {
        s_rate = rate;
        opl_init(rate);
        reset_chip();
    }
    while (frames > 0) {
        int n;

        if (s_until_update == 0) {
            update();
            s_until_update = (u32)((u64)rate * 100 / UPDATE_HZ_X100);
        }
        n = (int)(s_until_update < (u32)frames ? s_until_update : (u32)frames);
        opl_generate(out, n);
        out += n;
        frames -= n;
        s_until_update -= (u32)n;
    }
    pthread_mutex_unlock(&s_lock);
}
