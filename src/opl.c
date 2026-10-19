/*
 * opl.c - a Yamaha YM3812 (OPL2) synthesiser for the AdLib music.
 *
 * Written from the chip's published register description: nine two
 * operator channels (or six plus five rhythm instruments), each operator
 * a sine wave shaped by one of four waveforms, a multiplier, an ADSR
 * envelope, total level and key scaling, with tremolo and vibrato. The
 * envelope and modulation depths follow the documented timings and
 * levels; the arithmetic is floating point rather than the chip's log
 * and exponent tables, so the sound is close, not bit exact.
 */

#include <math.h>
#include <string.h>

#include "opl.h"

#define CHIP_RATE 49716.0       /* 3.579545 MHz / 72 */
#define MAX_ATT 96.0            /* dB, silence */

enum eg_state { EG_OFF, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

struct op {
    /* registers */
    int am, vib, egt, ksr, mult;
    int ksl, tl;
    int ar, dr, sl, rr;
    int wave;
    /* state */
    double phase;               /* in periods */
    double att;                 /* envelope attenuation, dB */
    enum eg_state eg;
    int key;                    /* bit 0 melodic key, bit 1 rhythm key */
    double out[2];              /* last outputs, for feedback */
};

struct channel {
    int fnum, block, key;
    int fb, cnt;
};

static struct op s_ops[18];
static struct channel s_ch[9];
static int s_wave_enable;
static int s_bd;                /* register 0xbd */
static double s_rate = 44100.0;
static double s_lfo_am, s_lfo_vib, s_clock;
static unsigned s_noise = 1;

/* Register offsets 0x00..0x15 to operator numbers; -1 for the holes. */
static const int s_slot_of[0x16] = {
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17
};

/* Channel n uses operators s_ch_op[n][0] (modulator) and [1] (carrier). */
static const int s_ch_op[9][2] = {
    { 0, 3 }, { 1, 4 }, { 2, 5 }, { 6, 9 }, { 7, 10 }, { 8, 11 }, { 12, 15 }, { 13, 16 }, { 14, 17 }
};

static const double s_mult[16] = { 0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15 };

/* Key scale level for block 7, in dB, by the top four bits of fnum. */
static const double s_ksl[16] = {
    0.0, 9.0, 12.0, 13.875, 15.0, 16.125, 16.875, 17.625,
    18.0, 18.75, 19.125, 19.5, 19.875, 20.25, 20.625, 21.0
};

static int eff_rate(const struct op *o, int rate, int ch)
{
    int ksr_val = (s_ch[ch].block << 1) | ((s_ch[ch].fnum >> 9) & 1);
    int r;

    if (rate == 0) {
        return 0;
    }
    r = rate * 4 + (o->ksr ? ksr_val : ksr_val >> 2);
    return r > 63 ? 63 : r;
}

/* dB per sample for a decay or release rate (96 dB in 39.28 s at rate 4,
 * each step of four halving the time, the low two bits in quarters). */
static double decay_step(int r)
{
    double seconds;

    if (r == 0) {
        return 0.0;
    }
    seconds = 39.28 * 4.0 / (4 + (r & 3)) / pow(2.0, (r >> 2) - 1);
    return MAX_ATT / (seconds * s_rate);
}

/* Attack moves exponentially; 2.826 s from silence at rate 4. */
static double attack_coef(int r)
{
    double seconds;

    if (r == 0) {
        return 0.0;
    }
    if (r >= 60) {
        return 1.0;
    }
    seconds = 2.826 * 4.0 / (4 + (r & 3)) / pow(2.0, (r >> 2) - 1);
    return 1.0 - exp(-6.87 / (seconds * s_rate));
}

static void key_on(struct op *o, int which)
{
    if (!o->key) {
        o->phase = 0.0;
        o->eg = EG_ATTACK;
    }
    o->key |= which;
}

static void key_off(struct op *o, int which)
{
    if (o->key) {
        o->key &= ~which;
        if (!o->key && o->eg != EG_OFF) {
            o->eg = EG_RELEASE;
        }
    }
}

void opl_init(int sample_rate)
{
    int i;

    memset(s_ops, 0, sizeof(s_ops));
    memset(s_ch, 0, sizeof(s_ch));
    for (i = 0; i < 18; i++) {
        s_ops[i].att = MAX_ATT;
        s_ops[i].eg = EG_OFF;
    }
    s_wave_enable = 0;
    s_bd = 0;
    s_rate = sample_rate;
    s_lfo_am = s_lfo_vib = 0.0;
}

void opl_write(int reg, int value)
{
    int slot, c;
    struct op *o;

    reg &= 0xff;
    value &= 0xff;
    if (reg == 0x01) {
        s_wave_enable = (value & 0x20) != 0;
        return;
    }
    if (reg == 0xbd) {
        int was = s_bd;

        s_bd = value;
        if (value & 0x20) {
            /* Rhythm keys: BD on channel 6, HH 13, SD 16, TT 14, CY 17. */
            static const struct { int bit, op; } keys[] = {
                { 0x10, 12 }, { 0x10, 15 }, { 0x01, 13 }, { 0x08, 16 }, { 0x04, 14 }, { 0x02, 17 }
            };
            size_t k;

            for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
                if (value & keys[k].bit) {
                    if (!((was & 0x20) && (was & keys[k].bit))) {
                        key_on(&s_ops[keys[k].op], 2);
                    }
                } else {
                    key_off(&s_ops[keys[k].op], 2);
                }
            }
        } else if (was & 0x20) {
            int k;
            for (k = 12; k < 18; k++) {
                key_off(&s_ops[k], 2);
            }
        }
        return;
    }
    if (reg >= 0xa0 && reg <= 0xa8) {
        s_ch[reg - 0xa0].fnum = (s_ch[reg - 0xa0].fnum & 0x300) | value;
        return;
    }
    if (reg >= 0xb0 && reg <= 0xb8) {
        struct channel *ch = &s_ch[reg - 0xb0];
        int key = (value & 0x20) != 0;

        c = reg - 0xb0;
        ch->fnum = (ch->fnum & 0xff) | ((value & 3) << 8);
        ch->block = (value >> 2) & 7;
        if (key && !ch->key) {
            key_on(&s_ops[s_ch_op[c][0]], 1);
            key_on(&s_ops[s_ch_op[c][1]], 1);
        } else if (!key && ch->key) {
            key_off(&s_ops[s_ch_op[c][0]], 1);
            key_off(&s_ops[s_ch_op[c][1]], 1);
        }
        ch->key = key;
        return;
    }
    if (reg >= 0xc0 && reg <= 0xc8) {
        s_ch[reg - 0xc0].fb = (value >> 1) & 7;
        s_ch[reg - 0xc0].cnt = value & 1;
        return;
    }
    if ((reg & 0x1f) >= 0x16) {
        return;
    }
    slot = s_slot_of[reg & 0x1f];
    if (slot < 0) {
        return;
    }
    o = &s_ops[slot];
    switch (reg & 0xe0) {
    case 0x20:
        o->am = (value >> 7) & 1;
        o->vib = (value >> 6) & 1;
        o->egt = (value >> 5) & 1;
        o->ksr = (value >> 4) & 1;
        o->mult = value & 15;
        break;
    case 0x40:
        o->ksl = (value >> 6) & 3;
        o->tl = value & 63;
        break;
    case 0x60:
        o->ar = (value >> 4) & 15;
        o->dr = value & 15;
        break;
    case 0x80:
        o->sl = (value >> 4) & 15;
        o->rr = value & 15;
        break;
    case 0xe0:
        o->wave = value & 3;
        break;
    default:
        break;
    }
}

static double waveform(int wave, double phase)
{
    double p = phase - floor(phase);
    double s = sin(p * 2.0 * M_PI);

    switch (s_wave_enable ? wave : 0) {
    case 1: return s > 0.0 ? s : 0.0;
    case 2: return fabs(s);
    case 3: return fmod(p, 0.5) < 0.25 ? fabs(s) : 0.0;
    default: return s;
    }
}

static void envelope(struct op *o, int ch)
{
    double sustain = o->sl == 15 ? MAX_ATT : o->sl * 3.0;

    switch (o->eg) {
    case EG_ATTACK: {
        double k = attack_coef(eff_rate(o, o->ar, ch));
        o->att -= o->att * k;
        if (o->att < 0.05) {
            o->att = 0.0;
            o->eg = EG_DECAY;
        }
        break;
    }
    case EG_DECAY:
        o->att += decay_step(eff_rate(o, o->dr, ch));
        if (o->att >= sustain) {
            o->att = sustain;
            o->eg = EG_SUSTAIN;
        }
        break;
    case EG_SUSTAIN:
        if (!o->egt) {
            o->att += decay_step(eff_rate(o, o->rr, ch));
        }
        break;
    case EG_RELEASE:
        o->att += decay_step(eff_rate(o, o->rr, ch));
        break;
    default:
        break;
    }
    if (o->att >= MAX_ATT) {
        o->att = MAX_ATT;
        if (o->eg == EG_RELEASE || (o->eg == EG_SUSTAIN && !o->egt)) {
            o->eg = EG_OFF;
        }
    }
}

/* Output of an operator, -1..1, with its phase advanced. */
static double op_output(struct op *o, int ch, double modulation)
{
    const struct channel *c = &s_ch[ch];
    double freq = c->fnum * CHIP_RATE / (double)(1 << (20 - c->block)) * s_mult[o->mult];
    double att, ksl;

    if (o->vib) {
        freq *= pow(2.0, s_lfo_vib * ((s_bd & 0x40) ? 14.0 : 7.0) / 1200.0);
    }
    envelope(o, ch);
    ksl = s_ksl[c->fnum >> 6] - 6.0 * (7 - c->block);
    if (ksl < 0.0) {
        ksl = 0.0;
    }
    att = o->att + o->tl * 0.75;
    switch (o->ksl) {
    case 1: att += ksl * 0.5; break;
    case 2: att += ksl * 0.25; break;
    case 3: att += ksl; break;
    default: break;
    }
    if (o->am) {
        att += s_lfo_am * ((s_bd & 0x80) ? 4.8 : 1.0);
    }
    o->phase += freq / s_rate;
    if (o->phase >= 1024.0) {
        o->phase -= floor(o->phase);
    }
    if (o->eg == EG_OFF || att >= MAX_ATT) {
        return 0.0;
    }
    return waveform(o->wave, o->phase + modulation) * pow(10.0, -att / 20.0);
}

static double channel_output(int ch)
{
    struct channel *c = &s_ch[ch];
    struct op *m = &s_ops[s_ch_op[ch][0]];
    struct op *car = &s_ops[s_ch_op[ch][1]];
    double fb = c->fb ? (m->out[0] + m->out[1]) * pow(2.0, c->fb - 9) * 4.0 : 0.0;
    double mo = op_output(m, ch, fb);

    m->out[1] = m->out[0];
    m->out[0] = mo;
    if (c->cnt) {
        return mo + op_output(car, ch, 0.0);
    }
    return op_output(car, ch, mo * 4.0);
}

/* Rhythm instruments, approximated: the bass drum as channel 6, the tom
 * as a plain operator, snare, cymbal and hi-hat from noise shaped by
 * their operators' envelopes. */
static double rhythm_output(void)
{
    double noise, sum;

    s_noise = s_noise * 1103515245u + 12345u;
    noise = ((s_noise >> 16) & 1) ? 1.0 : -1.0;
    sum = channel_output(6) * 2.0;
    sum += op_output(&s_ops[14], 8, 0.0) * 2.0;                     /* TT */
    sum += op_output(&s_ops[16], 7, 0.0) * (0.5 + 0.5 * noise) * 2.0; /* SD */
    sum += fabs(op_output(&s_ops[13], 7, 0.0)) * noise * 2.0;        /* HH */
    sum += fabs(op_output(&s_ops[17], 8, 0.0)) * noise * 2.0;        /* CY */
    return sum;
}

void opl_generate(short *out, int frames)
{
    int i, ch;

    for (i = 0; i < frames; i++) {
        double sum = 0.0;
        int melodic = (s_bd & 0x20) ? 6 : 9;
        long v;

        /* Tremolo 3.7 Hz triangle 0..1, vibrato 6.1 Hz -1..1. */
        s_clock += 1.0 / s_rate;
        if (s_clock > 1000.0) {
            s_clock -= 1000.0;
        }
        s_lfo_am = fabs(fmod(s_clock * 3.7 * 2.0, 2.0) - 1.0);
        s_lfo_vib = sin(2.0 * M_PI * 6.1 * s_clock);
        for (ch = 0; ch < melodic; ch++) {
            sum += channel_output(ch);
        }
        if (melodic == 6) {
            sum += rhythm_output();
        }
        v = (long)(sum * 4096.0);
        out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}
