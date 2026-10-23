/*
 * backend.h - what the shared platform code (common.c, mixer.c) needs from
 * each host backend. The engine itself only sees platform.h.
 *
 * A backend implements platform.h's window, event, timer, MIDI, dialog and
 * capture entry points, and the hooks below. common.c implements the file
 * paths and the disc image; mixer.c implements the sample voice, the music
 * renderer and CD audio on top of the backend's audio device.
 */

#ifndef AITD_BACKEND_H
#define AITD_BACKEND_H

#include <stddef.h>

#include "aitd.h"

/* The mixer's output: 16 bit signed mono at this rate. */
#define MIXER_RATE 44100

/* Where the game data is when AITD_DATA_DIR is not set: the app bundle's
 * resources on macOS, the program's folder or the current folder elsewhere.
 * Writes a path without a trailing separator. */
void backend_default_data_dir(char *out, size_t out_size);

/* Starts the host's audio output. The device pulls samples by calling
 * mixer_render from its callback (or, on hosts without an audio thread,
 * from backend_audio_pump). Returns 1 on success. */
int backend_audio_open(void);
void backend_audio_close(void);

/* A ripped CD track in a format only the host can decode (m4a or mp3 on
 * macOS). Return 0 when the host cannot play it; the mixer then looks for
 * a WAV file itself. */
int backend_cd_file_play(const char *path);
void backend_cd_file_stop(void);
int backend_cd_file_playing(void);

/* Fills `frames` mono samples: music under the sample voice. Called from the
 * audio thread. */
void mixer_render(short *out, int frames);

/* A second generator added over the music: the soundfont MIDI player
 * (midi_tsf.c) on hosts with no system synthesizer. */
void mixer_set_midi_render(void (*render)(short *out, int frames));

/* Opens a .cue image named by AITD_DATA_DIR, or the one in the data folder
 * (common.c). */
const char *common_data_dir(void);

#endif /* AITD_BACKEND_H */
