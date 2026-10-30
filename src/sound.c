/*
 * sound.c - samples, music and the per frame audio service.
 *
 *   sample_play     0x40b1d6  play if its priority beats the one playing
 *   sample_repeat   0x40b26c  the same, looping
 *   sound_stop_all  0x40b2e8
 *   music_play      0x40b31e  CD track (track 22 + voice.cfg for music 21)
 *   music_toggle    0x40b3f6
 *   audio_frame     0x40b452
 *
 * Samples in LISTSAMP.PAK are Creative Voice Files; only 8 bit PCM data
 * blocks (type 1) occur. Time Gate's SAMPLES.PAK holds RIFF WAVE files,
 * mono 11111 Hz, all 8 bit but one 16 bit. PRIORITY.ITD holds one priority
 * byte per sample.
 */

#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "draw.h"
#include "game.h"
#include "hmp.h"
#include "music.h"
#include "pak.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "subsystems.h"

s16 g_shaking;           /* 0x430da4 */
s16 g_saved_shaking;     /* 0x445020 */
s16 g_sample_priority = -1;  /* 0x444ffc */
s16 g_sample_current = -1;   /* 0x445004 */
static u32 s_music_timer;        /* 0x4415e4 */
static u32 s_music_length;       /* 0x43513c, ticks */
static u32 s_music_wait;         /* 0x4449ec */

/* The DOS Time Gate's volume screen (TIMEGATE.EXE 0x16da0): samples
 * 0..0x7fff (0x6184c), music 0..199 (0x61850). */
static int s_sample_volume = SAMPLE_VOLUME_MAX;
static int s_music_volume = MUSIC_VOLUME_MAX;

/* Hands 8 bit unsigned PCM to the mixer at the sample volume. */
static void pcm_play(const u8 *pcm, u32 len, u32 rate, int loops)
{
    u8 *quiet;
    u32 i;

    if (s_sample_volume >= SAMPLE_VOLUME_MAX) {
        platform_sound_play(pcm, len, rate, loops);
        return;
    }
    quiet = malloc(len);
    if (quiet == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        quiet[i] = (u8)(128 + ((pcm[i] - 128) * s_sample_volume) / SAMPLE_VOLUME_MAX);
    }
    platform_sound_play(quiet, len, rate, loops);
    free(quiet);
}

/* Plays the first PCM block of a VOC file. */
static void voc_play(const u8 *voc, u32 size, int loops)
{
    u32 offset;

    if (voc == NULL || size < 0x1a || memcmp(voc, "Creative Voice File", 19) != 0) {
        return;
    }
    offset = (u32)(voc[0x14] | (voc[0x15] << 8));

    while (offset + 4 <= size) {
        u8 type = voc[offset];
        u32 len = (u32)voc[offset + 1] | ((u32)voc[offset + 2] << 8)
                | ((u32)voc[offset + 3] << 16);
        const u8 *body = voc + offset + 4;

        if (type == 0) {
            break;
        }
        if (type == 1 && len > 2 && offset + 4 + len <= size) {
            u32 rate = 1000000u / (u32)(256 - body[0]);
            pcm_play(body + 2, len - 2, rate, loops);
            return;
        }
        offset += 4 + len;
    }
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Plays the PCM data chunk of a mono WAVE file; 16 bit data is narrowed
 * to the 8 bit unsigned samples the mixer takes. */
static void wav_play(const u8 *wav, u32 size, int loops)
{
    u32 offset = 12, rate = 0, bits = 0, channels = 0;

    if (wav == NULL || size < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return;
    }
    while (offset + 8 <= size) {
        u32 len = le32(wav + offset + 4);
        const u8 *body = wav + offset + 8;

        if (len > size - offset - 8) {
            len = size - offset - 8;
        }
        if (memcmp(wav + offset, "fmt ", 4) == 0 && len >= 16) {
            channels = (u32)(body[2] | (body[3] << 8));
            rate = le32(body + 4);
            bits = (u32)(body[14] | (body[15] << 8));
        } else if (memcmp(wav + offset, "data", 4) == 0 && rate != 0 && channels == 1) {
            if (bits == 8) {
                pcm_play(body, len, rate, loops);
            } else if (bits == 16) {
                u32 n = len / 2, i;
                u8 *narrow = malloc(n);

                if (narrow != NULL) {
                    for (i = 0; i < n; i++) {
                        narrow[i] = (u8)(body[i * 2 + 1] ^ 0x80);   /* high byte, signed to unsigned */
                    }
                    pcm_play(narrow, n, rate, loops);
                    free(narrow);
                }
            }
            return;
        }
        offset += 8 + len + (len & 1);
    }
}

static s16 priority_of(s16 sample)
{
    if (g_priority_itd == NULL || sample < 0) {
        return 0;
    }
    return ((const u8 *)g_priority_itd)[sample];
}

static void start(s16 sample, int loops)
{
    u32 size = 0;
    const u8 *data = hqr_get(g_cache_sample, sample, &size);

    if (data != NULL && size >= 4 && memcmp(data, "RIFF", 4) == 0) {
        wav_play(data, size, loops);
    } else {
        voc_play(data, size, loops);
    }
}

/* 0x40b1d6 */
void sample_play(s16 sample)
{
    s16 priority = 0;

    if (!g_sound_ready2) {
        return;
    }
    if (sample != -1) {
        priority = priority_of(sample);
        if (priority < g_sample_priority) {
            return;
        }
    }
    g_sample_priority = (sample == -1) ? 0 : priority;
    g_sample_current = sample;
    if (sample != -1) {
        start(sample, 1);
    }
}

/* A sound kept as a WAVE or VOC entry of a resource file (the save
 * screens' clicks, TIMEGATE.EXE 0x37fbd then 0x3b75f). */
void sample_play_resource(const char *pak, int entry)
{
    u32 size = 0;
    u8 *data;

    if (!g_sound_ready2 || (data = pak_load(pak, entry, &size)) == NULL) {
        return;
    }
    if (size >= 4 && memcmp(data, "RIFF", 4) == 0) {
        wav_play(data, size, 1);
    } else {
        voc_play(data, size, 1);
    }
    mem_free(data);
}

/* 0x40b26c - count 0 loops until stopped. */
void sample_repeat(s16 sample, u16 count)
{
    s16 priority;

    if (!g_sound_ready2) {
        return;
    }
    priority = priority_of(sample);
    if (priority < g_sample_priority) {
        return;
    }
    g_sample_priority = priority;
    g_sample_current = sample;
    platform_sound_stop();
    start(sample, count);
}

/* 0x40b2e8 */
void sound_stop_all(void)
{
    platform_sound_stop();
    g_sample_priority = -1;
    g_sample_current = -1;
}

/* 0x40b31e */
/* The titles that ship their music as data. Returns 1 when handled. */
static s16 s_data_playing = -1;
static int s_data_muted;            /* music off: the tune waits here */
static s16 s_data_wanted = -1;

static u32 vlq(const u8 *p, const u8 *end, const u8 **next)
{
    u32 v = 0;

    while (p < end) {
        u8 b = *p++;

        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) {
            break;
        }
    }
    *next = p;
    return v;
}

/* Scales the note velocities and channel volumes of a standard MIDI file
 * to the music volume, in place: the hosts' synthesisers have no master
 * volume the engine can reach. */
static void midi_scale(u8 *midi, u32 size)
{
    u32 offset = 14;

    if (s_music_volume >= MUSIC_VOLUME_MAX || size < 14 || memcmp(midi, "MThd", 4) != 0) {
        return;
    }
    while (offset + 8 <= size) {
        u32 chunk = ((u32)midi[offset + 4] << 24) | ((u32)midi[offset + 5] << 16)
                | ((u32)midi[offset + 6] << 8) | midi[offset + 7];
        u8 *p = midi + offset + 8, *end;
        u8 status = 0;
        u32 len;

        if (chunk > size - offset - 8) {
            chunk = size - offset - 8;
        }
        end = p + chunk;
        if (memcmp(midi + offset, "MTrk", 4) == 0) {
            while (p < end) {
                const u8 *next;

                vlq(p, end, &next);
                p = (u8 *)next;
                if (p >= end) {
                    break;
                }
                if (*p & 0x80) {
                    status = *p++;
                }
                if (status == 0xff) {
                    p++;
                    len = vlq(p, end, &next);
                    p = (u8 *)next + len;
                    status = 0;
                } else if (status == 0xf0 || status == 0xf7) {
                    len = vlq(p, end, &next);
                    p = (u8 *)next + len;
                    status = 0;
                } else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) {
                    p++;
                } else if (status >= 0x80 && p + 1 < end) {
                    if ((status & 0xf0) == 0x90 && p[1] != 0) {
                        int v = p[1] * s_music_volume / MUSIC_VOLUME_MAX;
                        p[1] = (u8)(v < 1 ? 1 : v);
                    } else if ((status & 0xf0) == 0xb0 && p[0] == 7) {
                        p[1] = (u8)(p[1] * s_music_volume / MUSIC_VOLUME_MAX);
                    }
                    p += 2;
                } else {
                    break;
                }
            }
        }
        offset += 8 + chunk;
    }
}

static int music_play_data(s16 music)
{
    const struct game_profile *profile = game_current();
    u32 size = 0;
    u8 *data;

    if (profile->music_source == MUSIC_CD) {
        return 0;
    }
    if (music < 0) {
        platform_midi_stop();
        music_adlib_stop();
        s_data_playing = -1;
        s_data_wanted = -1;
        return 1;
    }
    if (s_data_muted) {
        s_data_wanted = music;
        return 1;
    }
    if (music == s_data_playing) {
        return 1;
    }
    /* The DOS Time Gate's first two entries are instrument banks; the
     * Windows build has copies of song 19 there. */
    data = pak_load(profile->music_pak,
                    profile->music_source == MUSIC_TG_DOS && music < 2 ? 19 : music, &size);
    if (data == NULL) {
        return 1;
    }
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "music %d (%u bytes)\n", music, size);
    }
    switch (profile->music_source) {
    case MUSIC_MIDI:
        midi_scale(data, size);
        if (!platform_midi_play(data, size, 1)) {
            fprintf(stderr, "music %d: not a MIDI file the host plays\n", music);
        }
        break;
    case MUSIC_TG_DOS: {
        u32 midi_size = 0;
        u8 *midi = hmp_to_midi(data, size, &midi_size);

        if (midi != NULL && getenv("AITD_MIDI_DUMP") != NULL) {   /* harness only */
            FILE *dump = fopen(getenv("AITD_MIDI_DUMP"), "wb");
            if (dump != NULL) {
                fwrite(midi, 1, midi_size, dump);
                fclose(dump);
            }
        }
        if (midi != NULL) {
            midi_scale(midi, midi_size);
        }
        if (midi == NULL || !platform_midi_play(midi, midi_size, 1)) {
            fprintf(stderr, "music %d: HMP not played\n", music);
        }
        free(midi);
        break;
    }
    case MUSIC_ADLIB:
        platform_set_music_render(music_adlib_render);
        music_adlib_play(data, size);
        break;
    default:
        break;
    }
    mem_free(data);
    s_data_playing = music;
    return 1;
}

/* The tune playing, -1 for none. */
s16 music_current(void)
{
    if (game_current()->music_source == MUSIC_CD) {
        return g_music_pending;
    }
    return s_data_muted ? s_data_wanted : s_data_playing;
}

int music_enabled(void)
{
    if (game_current()->music_source == MUSIC_CD) {
        return g_cdaudio_ready;
    }
    return !s_data_muted;
}

void sound_volumes(int *sample, int *music)
{
    *sample = s_sample_volume;
    *music = s_music_volume;
}

/* A new music volume starts the tune again at that volume. */
void sound_set_volumes(int sample, int music)
{
    s_sample_volume = sample < 0 ? 0 : sample > SAMPLE_VOLUME_MAX ? SAMPLE_VOLUME_MAX : sample;
    music = music < 0 ? 0 : music > MUSIC_VOLUME_MAX ? MUSIC_VOLUME_MAX : music;
    if (music != s_music_volume) {
        s_music_volume = music;
        if (s_data_playing >= 0) {
            s16 m = s_data_playing;

            s_data_playing = -1;
            music_play_data(m);
        }
    }
}

void music_play(s16 music)
{
    s16 track = music;

    if (music_play_data(music)) {
        return;
    }

    if (!g_cdaudio_ready) {
        g_music_pending = music;
        return;
    }
    if (music == -1) {
        platform_cdaudio_stop();
        return;
    }
    if (music == g_music_pending) {
        return;
    }
    g_music_pending = music;
    if (music >= 0) {
        if (game_id() == GAME_AITD1) {
            platform_cdaudio_play_track(track);   /* INDARK.EXE 0x6e67 */
        } else {
            if (music == 21) {
                track = (s16)(g_voice_cfg_value + 21);
            }
            platform_cdaudio_play_track(track + 1);
        }
        s_music_length = 0xffffffffu;   /* the host reports no length */
        timer_reset_u32(&s_music_timer);
    }
}

/* 0x40b3f6 */
void music_toggle(void)
{
    if (game_current()->music_source != MUSIC_CD) {
        /* Music as data: the CD flag says nothing, the tune is held. */
        if (!s_data_muted) {
            s16 m = s_data_playing;

            music_play(-1);
            s_data_wanted = m;
            s_data_muted = 1;
        } else {
            s16 m = s_data_wanted;

            s_data_muted = 0;
            s_data_wanted = -1;
            if (m >= 0) {
                music_play(m);
            }
        }
        return;
    }
    g_cdaudio_ready ^= 1;
    if (!g_cdaudio_ready) {
        platform_cdaudio_stop();
    } else if (g_music_pending >= 0) {
        s16 m = g_music_pending;
        g_music_pending = -1;
        music_play(m);
    }
}

/* 0x40b59d - mute speech while a menu or dialog is up. */
void shaking_save(void)
{
    g_saved_shaking = g_shaking;
    g_shaking = 0;
}

/* 0x40b5d6 */
void shaking_restore(void)
{
    g_shaking = g_saved_shaking;
}

/* 0x40b452 */
void audio_frame(void)
{
    if (!platform_sound_playing()) {
        g_sample_priority = -1;
        g_sample_current = -1;
        if (g_sample_pending != -1) {
            if (g_sample_pending & 0x4000) {
                sample_repeat((s16)(g_sample_pending & 0xbfff), 0);
            } else {
                sample_play(g_sample_pending);
            }
            g_sample_pending = -1;
        }
    }

    if (g_cdaudio_ready && g_music_pending != -1) {
        if (g_music_pending == -2) {
            if (timer_elapsed_u32(&s_music_wait) >= 180) {
                music_play(g_music_next);
            }
        } else if (timer_elapsed_u32(&s_music_timer) >= s_music_length
                   || (timer_elapsed_u32(&s_music_timer) >= 60 && !platform_cdaudio_playing())) {
            /* The host gives no track length; a track that stopped has
             * ended. */
            g_music_pending = -1;
            if (g_music_next != -1) {
                music_play(g_music_next);
                g_music_next = -1;
            }
        }
    }

    if (g_fade_request) {
        g_fade_request = 0;
        if (g_black_screen) {
            palette_set_range(g_palette_buffer, 0, 256);
            palette_fade_out(0, 0);
            memset(g_screen, 0, SCREEN_PIXELS);
            draw_present();
        } else {
            palette_blackout();
            draw_present();
            palette_fade(0, 0);
        }
    }
}
