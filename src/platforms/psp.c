/*
 * psp.c - PlayStation Portable backend (pspdev toolchain).
 *
 *   video   the 8 bit frame goes to VRAM as a T8 texture with a 256 entry
 *           CLUT, and the GE draws it 4:3 in the middle of the 480 x 272
 *           screen, in 64 pixel columns (textures stop at 512 wide)
 *   input   the buttons and the analog stick, through pad.c
 *   sound   one mono channel at 44.1 kHz, fed by an audio thread
 *   music   MIDI from a soundfont in the game folder (midi_tsf.c)
 *
 * The game data sits next to EBOOT.PBP, one game per folder under
 * ms0:/PSP/GAME/.
 */

#include <pspctrl.h>
#include <pspaudio.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

PSP_MODULE_INFO("AITD", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);
PSP_HEAP_SIZE_KB(-1024);

#define SCREEN_W 480
#define SCREEN_H 272
#define STRIDE 512
#define FB_BYTES (STRIDE * SCREEN_H * 2)

static unsigned int __attribute__((aligned(16))) s_list[64 * 1024];
static u32 __attribute__((aligned(16))) s_clut[256];
static u8 __attribute__((aligned(16))) s_texture[640 * 400];
static int s_width, s_height;
static int s_back;               /* which of the two buffers is drawn into */
static u64 s_start;
static u8 s_palette[768];

/* ---- events -------------------------------------------------------------- */

#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head, s_event_tail;
static volatile int s_exit_requested;

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

/* The Home menu's "quit" reaches the game as a window close. */
static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    s_exit_requested = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int id = sceKernelCreateCallback("aitd exit", exit_callback, NULL);

    (void)args;
    (void)argp;
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

/* ---- video --------------------------------------------------------------- */

typedef struct {
    unsigned short u, v;
    short x, y, z;
} vertex;

int platform_video_init(const char *title, int width, int height)
{
    SceUID thread;
    int i;

    (void)title;
    s_width = width;
    s_height = height;
    /* The engine renders in software; it needs the full clock. */
    scePowerSetClockFrequency(333, 333, 166);

    thread = sceKernelCreateThread("aitd callbacks", callback_thread, 0x11, 0xfa0, 0, NULL);
    if (thread >= 0) {
        sceKernelStartThread(thread, 0, NULL);
    }

    for (i = 0; i < 256; i++) {
        s_clut[i] = 0xff000000u | (u32)i << 16 | (u32)i << 8 | (u32)i;
    }

    sceGuInit();
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBuffer(GU_PSM_5650, (void *)0, STRIDE);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)FB_BYTES, STRIDE);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuClearColor(0);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceRtcGetCurrentTick(&s_start);
    return 1;
}

void platform_video_shutdown(void)
{
    sceGuTerm();
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    memcpy(s_palette, palette, sizeof(s_palette));
    for (i = 0; i < 256; i++) {
        s_clut[i] = 0xff000000u | (u32)palette[i * 3 + 2] << 16
                  | (u32)palette[i * 3 + 1] << 8 | palette[i * 3];
    }
    sceKernelDcacheWritebackRange(s_clut, sizeof(s_clut));
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    /* 4:3 in the middle of the screen: 363 x 272. */
    const int out_w = SCREEN_H * 4 / 3, out_x = (SCREEN_W - out_w) / 2;
    int x;

    if (width != s_width || height != s_height || width * height > (int)sizeof(s_texture)) {
        return;
    }
    memcpy(s_texture, indexed_pixels, (size_t)width * height);
    sceKernelDcacheWritebackRange(s_texture, (unsigned)(width * height));

    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBufferList(GU_PSM_5650, (void *)(s_back ? FB_BYTES : 0), STRIDE);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(32, s_clut);
    sceGuTexMode(GU_PSM_T8, 0, 0, GU_FALSE);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    for (x = 0; x < width; x += 64) {
        int slice = width - x < 64 ? width - x : 64;
        vertex *v = sceGuGetMemory(2 * sizeof(vertex));

        v[0].u = 0;
        v[0].v = 0;
        v[0].x = (short)(out_x + x * out_w / width);
        v[0].y = 0;
        v[0].z = 0;
        v[1].u = (unsigned short)slice;
        v[1].v = (unsigned short)height;
        v[1].x = (short)(out_x + (x + slice) * out_w / width);
        v[1].y = SCREEN_H;
        v[1].z = 0;
        sceGuTexImage(0, 64, 512, width, s_texture + x);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                       2, NULL, v);
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
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
    static const struct { unsigned psp; unsigned button; } map[] = {
        { PSP_CTRL_UP, PAD_UP }, { PSP_CTRL_DOWN, PAD_DOWN },
        { PSP_CTRL_LEFT, PAD_LEFT }, { PSP_CTRL_RIGHT, PAD_RIGHT },
        { PSP_CTRL_CROSS, PAD_A }, { PSP_CTRL_CIRCLE, PAD_B },
        { PSP_CTRL_SQUARE, PAD_X }, { PSP_CTRL_TRIANGLE, PAD_Y },
        { PSP_CTRL_LTRIGGER, PAD_L }, { PSP_CTRL_RTRIGGER, PAD_R },
        { PSP_CTRL_START, PAD_START }, { PSP_CTRL_SELECT, PAD_SELECT },
    };
    SceCtrlData data;
    unsigned mask = 0;
    size_t i;

    sceCtrlPeekBufferPositive(&data, 1);
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (data.Buttons & map[i].psp) {
            mask |= map[i].button;
        }
    }
    pad_update(mask, ((int)data.Lx - 128) * 256, ((int)data.Ly - 128) * 256, pad_emit);
}

int platform_poll_event(struct platform_event *out)
{
    static int quit_sent;

    pad_poll();
    if (s_exit_requested && !quit_sent) {
        event_push(PLATFORM_EVENT_QUIT, 0);
        quit_sent = 1;
    }
    if (s_event_head == s_event_tail) {
        return 0;
    }
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

u32 platform_get_ticks(void)
{
    u64 now;

    sceRtcGetCurrentTick(&now);
    return (u32)((now - s_start) / 1000);
}

void platform_delay(u32 milliseconds)
{
    sceKernelDelayThread(milliseconds * 1000);
}

/* ---- audio --------------------------------------------------------------- */

#define AUDIO_FRAMES 1024

static int s_channel = -1;
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
        sceAudioOutputBlocking(s_channel, PSP_AUDIO_VOLUME_MAX, buffers[which]);
        which ^= 1;
    }
    return 0;
}

int backend_audio_open(void)
{
    s_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_FRAMES, PSP_AUDIO_FORMAT_MONO);
    if (s_channel < 0) {
        return 0;
    }
    s_audio_running = 1;
    s_audio_thread = sceKernelCreateThread("aitd audio", audio_thread, 0x12, 0x10000,
                                           THREAD_ATTR_USER, NULL);
    if (s_audio_thread < 0) {
        sceAudioChRelease(s_channel);
        s_channel = -1;
        return 0;
    }
    sceKernelStartThread(s_audio_thread, 0, NULL);
    return 1;
}

void backend_audio_close(void)
{
    if (s_channel < 0) {
        return;
    }
    s_audio_running = 0;
    sceKernelWaitThreadEnd(s_audio_thread, NULL);
    sceKernelDeleteThread(s_audio_thread);
    sceAudioChRelease(s_channel);
    s_channel = -1;
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

/* The folder of EBOOT.PBP, the program's working folder. */
void backend_default_data_dir(char *out, size_t out_size)
{
    if (getcwd(out, out_size) == NULL) {
        snprintf(out, out_size, ".");
    }
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return 0;
}

void platform_error_box(const char *caption, const char *message)
{
    FILE *f = fopen("error.txt", "w");

    if (f != NULL) {
        fprintf(f, "%s: %s\n", caption, message);
        fclose(f);
    }
}

/* Reads back the screen as shown: the displayed 565 buffer in VRAM. */
int platform_video_capture(const char *path)
{
    const u16 *shown = (const u16 *)((u8 *)sceGeEdramGetAddr() + (s_back ? 0 : FB_BYTES));
    FILE *f;
    int x, y;

    if ((f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    sceKernelDcacheWritebackInvalidateAll();
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (y = 0; y < SCREEN_H; y++) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = shown[y * STRIDE + x];
            u8 rgb[3];

            rgb[0] = (u8)((c & 0x1f) << 3);
            rgb[1] = (u8)(((c >> 5) & 0x3f) << 2);
            rgb[2] = (u8)(((c >> 11) & 0x1f) << 3);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 1;
}
