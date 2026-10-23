/*
 * web.c - browser backend (Emscripten).
 *
 * The engine keeps its blocking loops: the build uses ASYNCIFY, and this
 * backend yields to the browser from platform_delay and, every few
 * milliseconds, from platform_poll_event, so the page keeps drawing and
 * delivering keys. Frames go to a canvas, sound to Web Audio, which pulls
 * samples from the mixer on the page's own thread. MIDI music plays from a
 * soundfont (midi_tsf.c).
 *
 * The page (platforms/web/index.html) keeps the game files in IndexedDB,
 * mounted at /data, and starts the engine with AITD_DATA_DIR pointing at
 * the chosen game.
 */

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

static int s_width, s_height;
static u8 *s_frame;
static u32 *s_rgba;
static u32 s_palette[256];       /* RGBA as the canvas stores it */
static double s_start;
static double s_last_yield;

/* ---- events -------------------------------------------------------------- */

#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head, s_event_tail;

/* The page reports keys, mouse and gamepad through these exports. */
EMSCRIPTEN_KEEPALIVE void web_event(int kind, int code, int x, int y)
{
    int next = (s_event_tail + 1) % EVENT_QUEUE_SIZE;

    if (next == s_event_head) {
        return;
    }
    s_events[s_event_tail].kind = (enum platform_event_kind)kind;
    s_events[s_event_tail].code = code;
    s_events[s_event_tail].x = x * s_width / 640;
    s_events[s_event_tail].y = y * s_height / 480;
    s_event_tail = next;
}

static void pad_emit(int down, int scancode)
{
    web_event(down ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP, scancode, 0, 0);
}

EMSCRIPTEN_KEEPALIVE void web_pad(int buttons, int stick_x, int stick_y)
{
    pad_update((unsigned)buttons, stick_x, stick_y, pad_emit);
}

/* The page passes the game folder and options as environment variables;
 * Emscripten builds the C environment once, so later changes to its ENV
 * object do not reach getenv. */
EMSCRIPTEN_KEEPALIVE void web_setenv(const char *name, const char *value)
{
    setenv(name, value, 1);
}

/* Keeps the page alive: hands control back to the browser at most every
 * 8 ms (a yield costs a few milliseconds of its own). */
static void yield(int force)
{
    double now = emscripten_get_now();

    if (force || now - s_last_yield >= 8.0) {
        s_last_yield = now;
        emscripten_sleep(0);
    }
}

/* ---- video --------------------------------------------------------------- */

int platform_video_init(const char *title, int width, int height)
{
    int i;

    s_width = width;
    s_height = height;
    s_frame = calloc((size_t)width * height, 1);
    s_rgba = calloc((size_t)width * height, 4);
    for (i = 0; i < 256; i++) {
        s_palette[i] = 0xff000000u | (u32)i << 16 | (u32)i << 8 | (u32)i;
    }
    s_start = emscripten_get_now();
    EM_ASM({
        var canvas = document.getElementById('canvas');
        canvas.width = $1;
        canvas.height = $2;
        document.title = UTF8ToString($0);
        Module.aitdContext = canvas.getContext('2d');
        Module.aitdImage = Module.aitdContext.createImageData($1, $2);
    }, title, width, height);
    return 1;
}

void platform_video_shutdown(void)
{
    free(s_frame);
    free(s_rgba);
    s_frame = NULL;
    s_rgba = NULL;
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    for (i = 0; i < 256; i++) {
        s_palette[i] = 0xff000000u | (u32)palette[i * 3 + 2] << 16
                     | (u32)palette[i * 3 + 1] << 8 | palette[i * 3];
    }
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    int i, n = width * height;

    if (s_frame == NULL || width != s_width || height != s_height) {
        return;
    }
    memcpy(s_frame, indexed_pixels, (size_t)n);
    for (i = 0; i < n; i++) {
        s_rgba[i] = s_palette[indexed_pixels[i]];
    }
    EM_ASM({
        Module.aitdImage.data.set(HEAPU8.subarray($0, $0 + $1));
        Module.aitdContext.putImageData(Module.aitdImage, 0, 0);
    }, s_rgba, n * 4);
    yield(0);
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    (void)cursor_id;
}

int platform_poll_event(struct platform_event *out)
{
    yield(0);
    if (s_event_head == s_event_tail) {
        return 0;
    }
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

u32 platform_get_ticks(void)
{
    return (u32)(emscripten_get_now() - s_start);
}

void platform_delay(u32 milliseconds)
{
    emscripten_sleep(milliseconds);
    s_last_yield = emscripten_get_now();
}

/* ---- audio --------------------------------------------------------------- */

/* The page's Web Audio node calls this for each block. */
EMSCRIPTEN_KEEPALIVE void web_audio_render(short *out, int frames)
{
    mixer_render(out, frames);
}

int backend_audio_open(void)
{
    return EM_ASM_INT({
        return Module.aitdStartAudio ? Module.aitdStartAudio($0) : 0;
    }, MIXER_RATE);
}

void backend_audio_close(void)
{
    EM_ASM({
        if (Module.aitdStopAudio) Module.aitdStopAudio();
    });
}

int backend_cd_file_play(const char *path)
{
    (void)path;
    return 0;
}

void backend_cd_file_stop(void)
{
}

int backend_cd_file_playing(void)
{
    return 0;
}

/* ---- files and dialogs --------------------------------------------------- */

void backend_default_data_dir(char *out, size_t out_size)
{
    snprintf(out, out_size, "/data");
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return 0;
}

void platform_error_box(const char *caption, const char *message)
{
    fprintf(stderr, "[%s] %s\n", caption, message);
    EM_ASM({
        if (Module.aitdError) Module.aitdError(UTF8ToString($0), UTF8ToString($1));
    }, caption, message);
}

/* Saves live in the game folder; IndexedDB keeps them once the page syncs
 * it, which it does after every save (platform_save_path callers write and
 * close the file, then the page's timer notices). */
int platform_video_capture(const char *path)
{
    FILE *f;
    int i;

    if (s_frame == NULL || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_width, s_height);
    for (i = 0; i < s_width * s_height; i++) {
        u32 c = s_palette[s_frame[i]];
        u8 rgb[3];

        rgb[0] = (u8)c;
        rgb[1] = (u8)(c >> 8);
        rgb[2] = (u8)(c >> 16);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 1;
}
