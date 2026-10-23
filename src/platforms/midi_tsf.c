/*
 * midi_tsf.c - General MIDI for hosts with no system synthesizer (Linux,
 * the web, Android, the consoles). TinySoundFont plays the song from a
 * soundfont the player provides; the mixer adds it to the output.
 *
 * The soundfont is the first one found of: AITD_SOUNDFONT, a .sf2 file in
 * the data folder, and the usual system locations. Without one the music
 * is silent and everything else plays.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define TSF_IMPLEMENTATION
#define TML_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wnull-pointer-subtraction"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "platforms/third_party/tsf.h"
#include "platforms/third_party/tml.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/mutex.h"

static aitd_mutex s_lock = AITD_MUTEX_INIT;
static tsf *s_synth;
static int s_synth_failed;
static tml_message *s_song;
static tml_message *s_next;     /* the next event to send */
static double s_time_ms;        /* position in the song */
static int s_loop;

static int sf2_name(const char *name)
{
    size_t n = strlen(name);

    return n > 4 && (strcmp(name + n - 4, ".sf2") == 0 || strcmp(name + n - 4, ".SF2") == 0);
}

static int find_in_dir(const char *dir, char *out, size_t out_size)
{
    DIR *handle = opendir(dir);
    struct dirent *entry;

    if (handle == NULL) {
        return 0;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (sf2_name(entry->d_name)) {
            snprintf(out, out_size, "%s/%s", dir, entry->d_name);
            closedir(handle);
            return 1;
        }
    }
    closedir(handle);
    return 0;
}

static int find_soundfont(char *out, size_t out_size)
{
    static const char *const dirs[] = {
        "/usr/share/soundfonts", "/usr/share/sounds/sf2", "/usr/share/sounds/sf3",
        "/usr/local/share/soundfonts"
    };
    const char *env = getenv("AITD_SOUNDFONT");
    size_t i;

    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 1;
    }
    if (find_in_dir(common_data_dir(), out, out_size)) {
        return 1;
    }
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (find_in_dir(dirs[i], out, out_size)) {
            return 1;
        }
    }
    return 0;
}

static int synth_ready(void)
{
    char path[1024];

    if (s_synth != NULL || s_synth_failed) {
        return s_synth != NULL;
    }
    if (!find_soundfont(path, sizeof(path)) || (s_synth = tsf_load_filename(path)) == NULL) {
        fprintf(stderr, "music: no General MIDI soundfont (.sf2); set AITD_SOUNDFONT"
                        " or put one in the data folder\n");
        s_synth_failed = 1;
        return 0;
    }
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "music: soundfont %s\n", path);
    }
    tsf_set_output(s_synth, TSF_MONO, MIXER_RATE, 0.0f);
    tsf_channel_set_bank_preset(s_synth, 9, 128, 0);   /* channel 10: drums */
    return 1;
}

static void send(const tml_message *m)
{
    switch (m->type) {
    case TML_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber(s_synth, m->channel, m->program, m->channel == 9);
        break;
    case TML_NOTE_ON:
        tsf_channel_note_on(s_synth, m->channel, m->key, m->velocity / 127.0f);
        break;
    case TML_NOTE_OFF:
        tsf_channel_note_off(s_synth, m->channel, m->key);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(s_synth, m->channel, m->pitch_bend);
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(s_synth, m->channel, m->control, m->control_value);
        break;
    default:
        break;
    }
}

/* Adds the song to `out` (mixer.c calls this on the audio thread). */
static void midi_render(short *out, int frames)
{
    short block[256];

    aitd_mutex_lock(&s_lock);
    while (frames > 0 && s_song != NULL) {
        int n = frames < 256 ? frames : 256;
        int i;

        s_time_ms += n * (1000.0 / MIXER_RATE);
        while (s_next != NULL && s_next->time <= s_time_ms) {
            send(s_next);
            s_next = s_next->next;
        }
        if (s_next == NULL && s_loop) {
            tsf_note_off_all(s_synth);
            s_next = s_song;
            s_time_ms = 0;
        }
        tsf_render_short(s_synth, block, n, 0);
        for (i = 0; i < n; i++) {
            int mixed = out[i] + block[i];
            out[i] = (short)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
        }
        out += n;
        frames -= n;
    }
    aitd_mutex_unlock(&s_lock);
}

void platform_midi_stop(void)
{
    aitd_mutex_lock(&s_lock);
    if (s_song != NULL) {
        tml_free(s_song);
        s_song = s_next = NULL;
    }
    if (s_synth != NULL) {
        tsf_reset(s_synth);
    }
    aitd_mutex_unlock(&s_lock);
}

int platform_midi_play(const void *data, u32 size, int loop)
{
    tml_message *song;

    platform_midi_stop();
    if (data == NULL || size == 0 || !synth_ready()) {
        return 0;
    }
    song = tml_load_memory(data, (int)size);
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "music: MIDI song of %u bytes %s\n", size, song != NULL ? "playing" : "unreadable");
    }
    if (song == NULL) {
        return 0;
    }
    mixer_set_midi_render(midi_render);
    aitd_mutex_lock(&s_lock);
    tsf_channel_set_bank_preset(s_synth, 9, 128, 0);
    s_song = s_next = song;
    s_time_ms = 0;
    s_loop = loop;
    aitd_mutex_unlock(&s_lock);
    return 1;
}
