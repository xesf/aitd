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
 * blocks (type 1) occur. PRIORITY.ITD holds one priority byte per sample.
 */

#include <string.h>

#include "aitd.h"
#include "draw.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "subsystems.h"

s16 g_speech_flag;           /* 0x430da4 */
s16 g_saved_speech_flag;     /* 0x445020 */
s16 g_sample_priority = -1;  /* 0x444ffc */
s16 g_sample_current = -1;   /* 0x445004 */
static u32 s_music_timer;        /* 0x4415e4 */
static u32 s_music_length;       /* 0x43513c, ticks */
static u32 s_music_wait;         /* 0x4449ec */

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
            platform_sound_play(body + 2, len - 2, rate, loops);
            return;
        }
        offset += 4 + len;
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
    const u8 *voc = hqr_get(g_cache_sample, sample, &size);

    voc_play(voc, size, loops);
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
void music_play(s16 music)
{
    s16 track = music;

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
        if (music == 21) {
            track = (s16)(g_voice_cfg_value + 21);
        }
        platform_cdaudio_play_track(track + 1);
        s_music_length = 0xffffffffu;   /* the host reports no length */
        timer_reset_u32(&s_music_timer);
    }
}

/* 0x40b3f6 */
void music_toggle(void)
{
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
void speech_save(void)
{
    g_saved_speech_flag = g_speech_flag;
    g_speech_flag = 0;
}

/* 0x40b5d6 */
void speech_restore(void)
{
    g_speech_flag = g_saved_speech_flag;
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
        } else if (timer_elapsed_u32(&s_music_timer) >= s_music_length) {
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
            memset(g_screen, 0, 64000);
            draw_present();
        } else {
            palette_blackout();
            draw_present();
            palette_fade(0, 0);
        }
    }
}
