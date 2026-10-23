/*
 * platform.h - host abstraction layer.
 *
 * The 1996 build talks to Win32 directly (USER32/GDI32/KERNEL32 plus
 * mciSendStringA for CD audio). Every one of those touch points is isolated
 * here so the engine core stays platform agnostic. Each host has a backend
 * in src/platforms/ (macos.m, linux.c, windows.c, ...). common.c and
 * mixer.c there implement the file paths, the disc image and the audio
 * mixing for all of them; see platforms/backend.h.
 *
 * IDA addresses name the original routine that each entry point replaces.
 */

#ifndef AITD_PLATFORM_H
#define AITD_PLATFORM_H

#include "aitd.h"

/* Returned by platform_poll_events. */
enum platform_event_kind {
    PLATFORM_EVENT_NONE = 0,
    PLATFORM_EVENT_QUIT,
    PLATFORM_EVENT_KEY_DOWN,
    PLATFORM_EVENT_KEY_UP,
    PLATFORM_EVENT_MOUSE_MOVE,
    PLATFORM_EVENT_MOUSE_BUTTON
};

struct platform_event {
    enum platform_event_kind kind;
    int code;   /* PC set 1 scancode (extended keys | 0x80), or button index */
    int x, y;   /* mouse position, native 320x200 space */
};

/* Reads aitd.env from the program's folder into the environment, for hosts
 * that have no other way to set the AITD_* variables. Called first. */
void platform_early_init(void);

/* Window and framebuffer.
 * Replaces video_create_window (0x41e338), which wrapped RegisterClassA +
 * CreateWindowExA and installed the paint callback set at 0x41e64e. */
int platform_video_init(const char *title, int width, int height);
void platform_video_shutdown(void);

/* Uploads an 8bpp indexed surface. The original blitted g_blit_source
 * (0x444858) using g_blit_width/g_blit_height (0x444854/0x444856). */
void platform_video_present(const u8 *indexed_pixels, int width, int height);

/* 256 entry palette, 3 bytes per entry, VGA 0..63 range as stored in the
 * tail of the frame buffers allocated at 0x407483..0x4074b4. */
void platform_video_set_palette(const u8 *vga_palette);

/* win_set_icon (0x41e462) / win_set_cursor (0x41e4c0). No-ops are fine on
 * hosts without the concept. */
void platform_set_icon(const char *resource_name);
void platform_set_cursor(int cursor_id);

/* Replaces the PeekMessageA / TranslateMessage / DispatchMessageA pump that
 * the Win32 build ran from its idle hook. Returns 0 when the queue drains. */
int platform_poll_event(struct platform_event *out);

/* Milliseconds since platform_video_init. The original used a WM_TIMER
 * driven counter installed by SetTimer. */
u32 platform_get_ticks(void);
void platform_delay(u32 milliseconds);

/* Digital audio. Replaces sound_init (0x402d9b) and the sample playback
 * paths fed from g_cache_sample (0x444a98). */
int platform_sound_init(void);
void platform_sound_shutdown(void);
/* One voice: a new sample replaces the current one. loops: plays, 0 = forever. */
void platform_sound_play(const void *sample, u32 size, u32 rate, int loops);
void platform_sound_stop(void);
int platform_sound_playing(void);
/* A music generator mixed under the samples (called on the audio thread). */
void platform_set_music_render(void (*render)(short *out, int frames, int rate));

/* Redbook / CD audio. Replaces cdaudio_init (0x401010), which issued
 * mciSendStringA commands and was retried five times by engine_init. */
int platform_cdaudio_init(void);
void platform_cdaudio_shutdown(void);
void platform_cdaudio_play_track(int track);
void platform_cdaudio_stop(void);
int platform_cdaudio_playing(void);

/* A standard MIDI file, played through the host's General MIDI synth. */
int platform_midi_play(const void *data, u32 size, int loop);
void platform_midi_stop(void);

/* Single instance guard. Replaces sys_is_already_running (0x41a152), which
 * used FindWindowA on the registered class name. */
int platform_is_already_running(const char *title);

/* Captures the window exactly as displayed, as a PPM. Used to verify the
 * presentation path itself, which an engine-side frame dump cannot check. */
int platform_video_capture(const char *path);

/* Modal error reporting. Replaces sys_error_box (0x419d92) -> MessageBoxA. */
void platform_error_box(const char *caption, const char *message);

/* Case insensitive resolution of a DOS style asset name against the data
 * directory. Needed on case sensitive filesystems; the original opened
 * "itd_ress.pak" and "DEFINES.ITD" verbatim. */
int platform_resolve_path(const char *name, char *out, size_t out_size);
void platform_set_overlay_dir(const char *name);
int platform_is_directory(const char *path);
void platform_make_dir(const char *path);
int platform_save_path(const char *dir_name, const char *file_name, char *out,
                       size_t out_size, int create);

#endif /* AITD_PLATFORM_H */
