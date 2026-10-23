/*
 * mixer.c - the audio every backend shares: one sample voice over a music
 * generator, mixed to 16 bit mono at MIXER_RATE.
 *
 * The original drove one waveOut voice (0x402ec7 stops the current sample
 * before it starts another), so a new sample replaces the current one. The
 * music comes from the AdLib renderer (music.c), or from CD audio: a track
 * of the disc image, or a ripped track beside the data.
 *
 * Replaces sound_init (0x402d9b), the sample playback fed from
 * g_cache_sample (0x444a98) and cdaudio_init (0x401010), which drove a
 * physical drive through mciSendStringA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "cdimage.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/mutex.h"

static aitd_mutex s_lock = AITD_MUTEX_INIT;

static u8 *s_voice;          /* 8 bit unsigned */
static u32 s_voice_size;
static u32 s_voice_step;     /* 16.16 source samples per output sample */
static u64 s_voice_pos;      /* 16.16 */
static int s_voice_loops;    /* plays left, 0 = forever */
static int s_voice_active;
static void (*s_music_render)(short *out, int frames, int rate);
static void (*s_midi_render)(short *out, int frames);

/* CD audio: a disc image track or a ripped WAV file, 16 bit stereo at
 * 44.1 kHz, read from disk a block at a time (a track held whole would
 * take up to 80 MB, more than the handhelds have). */
#define CD_BLOCK 4096                   /* stereo frames per read */

static FILE *s_cd_file;
static long s_cd_start;                 /* byte offset of the first frame */
static u32 s_cd_frames, s_cd_pos;       /* length and position, in frames */
static int s_cd_swap;                   /* big-endian samples */
static short s_cd_block[CD_BLOCK * 2];
static u32 s_cd_block_at, s_cd_block_len;   /* frames held in s_cd_block */

static int s_audio_open;

void mixer_render(short *out, int frames)
{
    int i;

    aitd_mutex_lock(&s_lock);
    if (s_music_render != NULL) {
        s_music_render(out, frames, MIXER_RATE);
    } else {
        memset(out, 0, (size_t)frames * sizeof(short));
    }
    if (s_midi_render != NULL) {
        s_midi_render(out, frames);
    }
    for (i = 0; i < frames && s_voice_active; i++) {
        u32 index = (u32)(s_voice_pos >> 16);
        int mixed;

        if (index >= s_voice_size) {
            if (s_voice_loops == 1) {
                s_voice_active = 0;
                break;
            }
            if (s_voice_loops > 1) {
                s_voice_loops--;
            }
            s_voice_pos = 0;
            index = 0;
        }
        mixed = out[i] + ((int)s_voice[index] - 128) * 192;
        out[i] = (short)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
        s_voice_pos += s_voice_step;
    }
    aitd_mutex_unlock(&s_lock);
}

void platform_set_music_render(void (*render)(short *out, int frames, int rate))
{
    aitd_mutex_lock(&s_lock);
    s_music_render = render;
    aitd_mutex_unlock(&s_lock);
}

void mixer_set_midi_render(void (*render)(short *out, int frames))
{
    aitd_mutex_lock(&s_lock);
    s_midi_render = render;
    aitd_mutex_unlock(&s_lock);
}

int platform_sound_init(void)
{
    if (!s_audio_open) {
        s_audio_open = backend_audio_open();
    }
    return s_audio_open;
}

void platform_sound_shutdown(void)
{
    if (s_audio_open) {
        backend_audio_close();
        s_audio_open = 0;
    }
    platform_sound_stop();
}

void platform_sound_play(const void *sample, u32 size, u32 rate, int loops)
{
    u8 *copy;

    if (sample == NULL || size == 0 || rate == 0) {
        return;
    }
    copy = malloc(size);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, sample, size);

    aitd_mutex_lock(&s_lock);
    free(s_voice);
    s_voice = copy;
    s_voice_size = size;
    s_voice_step = (u32)(((u64)rate << 16) / MIXER_RATE);
    s_voice_pos = 0;
    s_voice_loops = loops;
    s_voice_active = 1;
    aitd_mutex_unlock(&s_lock);
}

void platform_sound_stop(void)
{
    aitd_mutex_lock(&s_lock);
    s_voice_active = 0;
    free(s_voice);
    s_voice = NULL;
    s_voice_size = 0;
    aitd_mutex_unlock(&s_lock);
}

int platform_sound_playing(void)
{
    int active;

    aitd_mutex_lock(&s_lock);
    active = s_voice_active;
    aitd_mutex_unlock(&s_lock);
    return active;
}

/* ---- CD audio ---------------------------------------------------------- */

/* The original treated a drive failure as fatal after five retries
 * (0x407326..0x407389). Here a missing track is silence. */
int platform_cdaudio_init(void)
{
    return 1;
}

void platform_cdaudio_shutdown(void)
{
    platform_cdaudio_stop();
}

/* Called with the lock held. */
static void cd_fill(void)
{
    u32 want = s_cd_frames - s_cd_pos < CD_BLOCK ? s_cd_frames - s_cd_pos : CD_BLOCK;
    size_t got;

    s_cd_block_at = s_cd_pos;
    s_cd_block_len = 0;
    if (fseek(s_cd_file, s_cd_start + (long)s_cd_pos * 4, SEEK_SET) != 0) {
        return;
    }
    got = fread(s_cd_block, 4, want, s_cd_file);
    if (s_cd_swap) {
        u8 *b = (u8 *)s_cd_block;
        size_t k;

        for (k = 0; k < got * 4; k += 2) {
            u8 t = b[k];
            b[k] = b[k + 1];
            b[k + 1] = t;
        }
    }
    s_cd_block_len = (u32)got;
}

static void cd_render(short *out, int frames, int rate)
{
    int i;

    (void)rate;     /* the mixer runs at the CD rate */
    for (i = 0; i < frames; i++) {
        u32 at;

        if (s_cd_file == NULL || s_cd_pos >= s_cd_frames) {
            out[i] = 0;
            continue;
        }
        if (s_cd_pos < s_cd_block_at || s_cd_pos >= s_cd_block_at + s_cd_block_len) {
            cd_fill();
            if (s_cd_block_len == 0) {
                s_cd_pos = s_cd_frames;     /* a short file: the track ends */
                out[i] = 0;
                continue;
            }
        }
        at = (s_cd_pos - s_cd_block_at) * 2;
        out[i] = (short)(((int)s_cd_block[at] + s_cd_block[at + 1]) / 2);
        s_cd_pos++;
    }
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* A ripped track as a WAV file in the CD's own format, 16 bit stereo at
 * 44.1 kHz. Finds where its samples start. */
static int wav_location(const char *path, long *offset, u32 *frames)
{
    FILE *f = fopen(path, "rb");
    u8 header[12], chunk[8], fmt[16];
    int format_ok = 0, found = 0;

    if (f == NULL) {
        return 0;
    }
    if (fread(header, 1, 12, f) == 12 && memcmp(header, "RIFF", 4) == 0
        && memcmp(header + 8, "WAVE", 4) == 0) {
        while (fread(chunk, 1, 8, f) == 8) {
            u32 size = le32(chunk + 4);

            if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
                if (fread(fmt, 1, 16, f) != 16) {
                    break;
                }
                format_ok = fmt[0] == 1 && fmt[2] == 2 && le32(fmt + 4) == 44100 && fmt[14] == 16;
                fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR);
            } else if (memcmp(chunk, "data", 4) == 0) {
                if (format_ok) {
                    *offset = ftell(f);
                    *frames = size / 4;
                    found = 1;
                }
                break;
            } else {
                fseek(f, (long)(size + (size & 1)), SEEK_CUR);
            }
        }
    }
    fclose(f);
    return found;
}

static int cd_start(const char *path, long offset, u32 frames, int swap)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    aitd_mutex_lock(&s_lock);
    s_cd_file = f;
    s_cd_start = offset;
    s_cd_frames = frames;
    s_cd_pos = 0;
    s_cd_swap = swap;
    s_cd_block_at = s_cd_block_len = 0;
    s_music_render = cd_render;
    aitd_mutex_unlock(&s_lock);
    return 1;
}

void platform_cdaudio_play_track(int track)
{
    static const char *const ext[] = { "m4a", "mp3", "aiff", "aif" };
    char path[1024];
    char name[64];
    long offset = 0;
    u32 frames = 0;
    int swap = 0;
    size_t i;

    platform_cdaudio_stop();
    if (cdimage_is_open()
        && cdimage_audio_location(track, path, sizeof(path), &offset, &frames, &swap)) {
        if (getenv("AITD_TRACE") != NULL) {
            fprintf(stderr, "cd track %d from the disc image: %u frames\n", track, frames);
        }
        if (cd_start(path, offset, frames, swap)) {
            return;
        }
    }

    snprintf(name, sizeof(name), "track%02d.wav", track);
    if (platform_resolve_path(name, path, sizeof(path))
        && wav_location(path, &offset, &frames) && cd_start(path, offset, frames, 0)) {
        return;
    }
    for (i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
        snprintf(name, sizeof(name), "track%02d.%s", track, ext[i]);
        if (platform_resolve_path(name, path, sizeof(path)) && backend_cd_file_play(path)) {
            return;
        }
    }
}

void platform_cdaudio_stop(void)
{
    if (s_cd_file != NULL) {
        aitd_mutex_lock(&s_lock);
        if (s_music_render == cd_render) {
            s_music_render = NULL;
        }
        fclose(s_cd_file);
        s_cd_file = NULL;
        s_cd_frames = s_cd_pos = 0;
        aitd_mutex_unlock(&s_lock);
    }
    backend_cd_file_stop();
}

int platform_cdaudio_playing(void)
{
    if (s_cd_file != NULL) {
        return s_cd_pos < s_cd_frames;
    }
    return backend_cd_file_playing();
}
