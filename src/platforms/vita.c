/*
 * vita.c - PlayStation Vita backend (VitaSDK).
 *
 *   video   the 8 bit frame expanded to 32 bit and stretched 4:3 into a
 *           960 x 544 framebuffer in CDRAM, by the CPU
 *   input   the buttons and the left stick, through pad.c
 *   sound   one mono BGM port at 44.1 kHz, fed by an audio thread
 *   music   MIDI from a soundfont in the game folder (midi_tsf.c)
 *
 * Each game is its own package (VITA_GAME_DIR) and reads its data from
 * ux0:data/aitd/<VITA_GAME_DIR>.
 */

#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

#ifndef VITA_GAME_DIR
#define VITA_GAME_DIR "INDARK"
#endif

/* Up to 512 MB of the heap; the engine and a disc image's index fit
 * easily. */
int _newlib_heap_size_user = 192 * 1024 * 1024;

#define SCREEN_W 960
#define SCREEN_H 544
#define FB_SIZE (((SCREEN_W * SCREEN_H * 4) + 0x3ffff) & ~0x3ffff)

static void *s_fb[2];
static SceUID s_fb_block[2];
static int s_back;
static int s_width, s_height;
static u32 s_palette[256];       /* ABGR, as the display reads it */
static u8 s_rgb[768];
static u8 *s_frame;

/* ---- events -------------------------------------------------------------- */

#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head, s_event_tail;

static void event_push(enum platform_event_kind kind, int code)
{
    int next = (s_event_tail + 1) % EVENT_QUEUE_SIZE;

    if (next == s_event_head) {
        return;
    }
    s_events[s_event_tail].kind = kind;
    s_events[s_event_tail].code = code;
    s_events[s_event_tail].x = 0;
    s_events[s_event_tail].y = 0;
    s_event_tail = next;
}

static void pad_emit(int down, int scancode)
{
    event_push(down ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP, scancode);
}

/* ---- video --------------------------------------------------------------- */

static void *alloc_cdram(SceUID *block)
{
    void *base = NULL;

    *block = sceKernelAllocMemBlock("aitd fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                    FB_SIZE, NULL);
    if (*block < 0 || sceKernelGetMemBlockBase(*block, &base) < 0) {
        return NULL;
    }
    memset(base, 0, FB_SIZE);
    return base;
}

static void show(void *fb)
{
    SceDisplayFrameBuf frame;

    memset(&frame, 0, sizeof(frame));
    frame.size = sizeof(frame);
    frame.base = fb;
    frame.pitch = SCREEN_W;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    frame.width = SCREEN_W;
    frame.height = SCREEN_H;
    sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

int platform_video_init(const char *title, int width, int height)
{
    int i;

    (void)title;
    s_width = width;
    s_height = height;
    s_frame = calloc((size_t)width * height, 1);
    scePowerSetArmClockFrequency(444);
    for (i = 0; i < 2; i++) {
        s_fb[i] = alloc_cdram(&s_fb_block[i]);
        if (s_fb[i] == NULL) {
            return 0;
        }
    }
    for (i = 0; i < 256; i++) {
        s_palette[i] = 0xff000000u | (u32)i << 16 | (u32)i << 8 | (u32)i;
    }
    show(s_fb[0]);
    s_back = 1;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    return 1;
}

void platform_video_shutdown(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (s_fb[i] != NULL) {
            sceKernelFreeMemBlock(s_fb_block[i]);
            s_fb[i] = NULL;
        }
    }
    free(s_frame);
    s_frame = NULL;
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    memcpy(s_rgb, palette, sizeof(s_rgb));
    for (i = 0; i < 256; i++) {
        s_palette[i] = 0xff000000u | (u32)palette[i * 3 + 2] << 16
                     | (u32)palette[i * 3 + 1] << 8 | palette[i * 3];
    }
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    /* 4:3 in the middle of the panel: 725 x 544. */
    const int out_w = SCREEN_H * 4 / 3, out_x = (SCREEN_W - out_w) / 2;
    static int columns[SCREEN_W];
    static int columns_for;
    u32 *fb = s_fb[s_back];
    int x, y;

    if (s_frame == NULL || width != s_width || height != s_height) {
        return;
    }
    memcpy(s_frame, indexed_pixels, (size_t)width * height);
    if (columns_for != width) {
        for (x = 0; x < out_w; x++) {
            columns[x] = x * width / out_w;
        }
        columns_for = width;
    }
    for (y = 0; y < SCREEN_H; y++) {
        const u8 *row = indexed_pixels + (size_t)(y * height / SCREEN_H) * width;
        u32 *out = fb + y * SCREEN_W + out_x;

        for (x = 0; x < out_w; x++) {
            out[x] = s_palette[row[columns[x]]];
        }
    }
    show(fb);
    sceDisplayWaitVblankStart();
    s_back ^= 1;
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    (void)cursor_id;
}

static void pad_poll(void)
{
    static const struct { unsigned vita; unsigned button; } map[] = {
        { SCE_CTRL_UP, PAD_UP }, { SCE_CTRL_DOWN, PAD_DOWN },
        { SCE_CTRL_LEFT, PAD_LEFT }, { SCE_CTRL_RIGHT, PAD_RIGHT },
        { SCE_CTRL_CROSS, PAD_A }, { SCE_CTRL_CIRCLE, PAD_B },
        { SCE_CTRL_SQUARE, PAD_X }, { SCE_CTRL_TRIANGLE, PAD_Y },
        { SCE_CTRL_LTRIGGER, PAD_L }, { SCE_CTRL_RTRIGGER, PAD_R },
        { SCE_CTRL_START, PAD_START }, { SCE_CTRL_SELECT, PAD_SELECT },
    };
    SceCtrlData data;
    unsigned mask = 0;
    size_t i;

    memset(&data, 0, sizeof(data));
    sceCtrlPeekBufferPositive(0, &data, 1);
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (data.buttons & map[i].vita) {
            mask |= map[i].button;
        }
    }
    pad_update(mask, ((int)data.lx - 128) * 256, ((int)data.ly - 128) * 256, pad_emit);
}

int platform_poll_event(struct platform_event *out)
{
    pad_poll();
    if (s_event_head == s_event_tail) {
        return 0;
    }
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

u32 platform_get_ticks(void)
{
    static SceUInt64 start;
    SceUInt64 now = sceKernelGetProcessTimeWide();

    if (start == 0) {
        start = now;
    }
    return (u32)((now - start) / 1000);
}

void platform_delay(u32 milliseconds)
{
    sceKernelDelayThread(milliseconds * 1000);
}

/* ---- audio --------------------------------------------------------------- */

#define AUDIO_FRAMES 1024

static int s_port = -1;
static volatile int s_audio_running;
static SceUID s_audio_thread = -1;

static int audio_thread(SceSize args, void *argp)
{
    static short buffers[2][AUDIO_FRAMES];
    int which = 0;

    (void)args;
    (void)argp;
    while (s_audio_running) {
        mixer_render(buffers[which], AUDIO_FRAMES);
        sceAudioOutOutput(s_port, buffers[which]);
        which ^= 1;
    }
    return 0;
}

int backend_audio_open(void)
{
    s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_FRAMES, MIXER_RATE,
                                 SCE_AUDIO_OUT_MODE_MONO);
    if (s_port < 0) {
        return 0;
    }
    s_audio_running = 1;
    s_audio_thread = sceKernelCreateThread("aitd audio", audio_thread, 0x10000100 - 10,
                                           0x10000, 0, 0, NULL);
    if (s_audio_thread < 0) {
        sceAudioOutReleasePort(s_port);
        s_port = -1;
        return 0;
    }
    sceKernelStartThread(s_audio_thread, 0, NULL);
    return 1;
}

void backend_audio_close(void)
{
    if (s_port < 0) {
        return;
    }
    s_audio_running = 0;
    sceKernelWaitThreadEnd(s_audio_thread, NULL, NULL);
    sceKernelDeleteThread(s_audio_thread);
    sceAudioOutReleasePort(s_port);
    s_port = -1;
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
    snprintf(out, out_size, "ux0:data/aitd/%s", VITA_GAME_DIR);
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return 0;
}

void platform_error_box(const char *caption, const char *message)
{
    char path[256];
    FILE *f;

    snprintf(path, sizeof(path), "ux0:data/aitd/%s/error.txt", VITA_GAME_DIR);
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "%s: %s\n", caption, message);
        fclose(f);
    }
}

int platform_video_capture(const char *path)
{
    FILE *f;
    int i;

    if (s_frame == NULL || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_width, s_height);
    for (i = 0; i < s_width * s_height; i++) {
        fwrite(s_rgb + s_frame[i] * 3, 1, 3, f);
    }
    fclose(f);
    return 1;
}
