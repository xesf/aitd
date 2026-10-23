/*
 * dos.c - MS-DOS backend, 32 bit protected mode (Open Watcom with the
 * DOS/32A or DOS/4GW extender).
 *
 *   video     VGA mode 13h, 320 x 200 in 256 colours, written directly at
 *             0xA0000; the 640 x 400 build of Time Gate is halved to fit
 *   keyboard  an INT 9 handler that reads set 1 scancodes from port 0x60
 *   timer     the PIT reprogrammed to 1000 Hz, chained to the BIOS clock
 *   sound     a Sound Blaster 16 (BLASTER variable) playing 16 bit mono
 *             from a DMA buffer the frame loop refills
 *   music     AdLib and CD audio through the mixer; MIDI songs are sent
 *             to an MPU-401 in UART mode
 *
 * There is no audio thread: the mixer runs from platform_poll_event, which
 * the engine calls every frame, so the lock in mutex.h is empty here.
 */

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#define TML_IMPLEMENTATION
#include "platforms/third_party/tml.h"

/* ---- events -------------------------------------------------------------- */

#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static volatile int s_event_head, s_event_tail;

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

/* ---- keyboard ------------------------------------------------------------ */

static void (__interrupt __far *s_old_keyboard)(void);
static volatile int s_extended;

/* Set 1 scancodes: 0xE0 announces an extended key (arrows, right Control),
 * which the engine sees with 0x80 set; bit 7 of the code is the release. */
static void __interrupt __far keyboard_handler(void)
{
    int code = inp(0x60);

    if (code == 0xe0) {
        s_extended = 1;
    } else {
        int key = (code & 0x7f) | (s_extended ? 0x80 : 0);

        s_extended = 0;
        if (key != 0xaa) {      /* the fake Shift some keyboards send */
            event_push((code & 0x80) ? PLATFORM_EVENT_KEY_UP : PLATFORM_EVENT_KEY_DOWN, key);
        }
    }
    outp(0x20, 0x20);
}

/* ---- timer --------------------------------------------------------------- */

#define PIT_HZ 1193182
#define TIMER_HZ 1000

static void (__interrupt __far *s_old_timer)(void);
static volatile u32 s_ticks;
static volatile u32 s_bios_count;

static void __interrupt __far timer_handler(void)
{
    s_ticks++;
    s_bios_count += PIT_HZ / TIMER_HZ;
    if (s_bios_count >= 65536) {
        s_bios_count -= 65536;
        _chain_intr(s_old_timer);   /* the BIOS clock keeps its 18.2 Hz */
    }
    outp(0x20, 0x20);
}

static void timer_install(void)
{
    s_old_timer = _dos_getvect(0x08);
    _disable();
    _dos_setvect(0x08, timer_handler);
    outp(0x43, 0x36);
    outp(0x40, (PIT_HZ / TIMER_HZ) & 0xff);
    outp(0x40, (PIT_HZ / TIMER_HZ) >> 8);
    _enable();
}

static void timer_remove(void)
{
    _disable();
    outp(0x43, 0x36);
    outp(0x40, 0);
    outp(0x40, 0);
    _dos_setvect(0x08, s_old_timer);
    _enable();
}

u32 platform_get_ticks(void)
{
    return s_ticks;
}

void platform_delay(u32 milliseconds)
{
    u32 end = s_ticks + milliseconds;

    while ((s32)(s_ticks - end) < 0) {
    }
}

/* ---- video --------------------------------------------------------------- */

static u8 *const s_vga = (u8 *)0xa0000;
static int s_width, s_height;
static u8 s_palette[768];
static u8 *s_frame;
static int s_video_on;

static void set_mode(int mode)
{
    union REGS r;

    memset(&r, 0, sizeof(r));
    r.w.ax = (unsigned short)mode;
    int386(0x10, &r, &r);
}

static void dos_shutdown(void)
{
    if (s_old_keyboard != NULL) {
        _dos_setvect(0x09, s_old_keyboard);
        s_old_keyboard = NULL;
    }
    if (s_old_timer != NULL) {
        timer_remove();
        s_old_timer = NULL;
    }
    if (s_video_on) {
        set_mode(0x03);
        s_video_on = 0;
    }
}

int platform_video_init(const char *title, int width, int height)
{
    (void)title;
    s_width = width;
    s_height = height;
    s_frame = calloc((size_t)width * height, 1);

    if (getenv("AITD_NO_VIDEO") == NULL) {      /* harness: stay in text mode */
        set_mode(0x13);
        s_video_on = 1;
    }
    s_old_keyboard = _dos_getvect(0x09);
    _dos_setvect(0x09, keyboard_handler);
    timer_install();
    atexit(dos_shutdown);
    return 1;
}

void platform_video_shutdown(void)
{
    dos_shutdown();
    free(s_frame);
    s_frame = NULL;
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    memcpy(s_palette, palette, sizeof(s_palette));
    if (!s_video_on) {
        return;
    }
    outp(0x3c8, 0);
    for (i = 0; i < 768; i++) {
        outp(0x3c9, palette[i] >> 2);   /* the DAC takes 6 bits */
    }
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    if (s_frame == NULL || width != s_width || height != s_height) {
        return;
    }
    memcpy(s_frame, indexed_pixels, (size_t)width * height);
    if (!s_video_on) {
        return;
    }
    while (inp(0x3da) & 8) {        /* wait out a retrace in progress */
    }
    if (width == 320 && height == 200) {
        memcpy(s_vga, indexed_pixels, 64000);
    } else {
        /* 640 x 400: every other pixel of every other row. */
        int x, y;

        for (y = 0; y < 200; y++) {
            const u8 *row = indexed_pixels + (size_t)(y * height / 200) * width;
            u8 *out = s_vga + y * 320;

            for (x = 0; x < 320; x++) {
                out[x] = row[x * width / 320];
            }
        }
    }
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    (void)cursor_id;
}

/* ---- Sound Blaster 16 ---------------------------------------------------- */

#define DMA_HALF 4096                   /* samples in each half of the buffer */

static int s_sb_port, s_sb_irq, s_sb_dma16, s_mpu_port;
static short *s_dma_buffer;             /* in conventional memory */
static volatile int s_half_played;      /* which half the card just finished */
static int s_half_filled = -1;
static u32 s_halves_mixed;          /* for AITD_TRACE */
static void (__interrupt __far *s_old_sb)(void);

static void blaster_settings(void)
{
    const char *env = getenv("BLASTER");

    s_sb_port = 0x220;
    s_sb_irq = 5;
    s_sb_dma16 = 5;
    s_mpu_port = 0x330;
    for (; env != NULL && *env != '\0'; env++) {
        switch (*env) {
        case 'A': case 'a': s_sb_port = (int)strtol(env + 1, NULL, 16); break;
        case 'I': case 'i': s_sb_irq = atoi(env + 1); break;
        case 'H': case 'h': s_sb_dma16 = atoi(env + 1); break;
        case 'P': case 'p': s_mpu_port = (int)strtol(env + 1, NULL, 16); break;
        default: break;
        }
    }
}

static void dsp_write(int value)
{
    int tries = 0xffff;

    while ((inp(s_sb_port + 0xc) & 0x80) && --tries > 0) {
    }
    outp(s_sb_port + 0xc, value);
}

static int dsp_reset(void)
{
    int tries;

    outp(s_sb_port + 6, 1);
    for (tries = 0; tries < 1000; tries++) {
        inp(s_sb_port + 6);         /* a few microseconds each */
    }
    outp(s_sb_port + 6, 0);
    for (tries = 0; tries < 1000; tries++) {
        if ((inp(s_sb_port + 0xe) & 0x80) && inp(s_sb_port + 0xa) == 0xaa) {
            return 1;
        }
    }
    return 0;
}

static int irq_vector(int irq)
{
    return irq < 8 ? 0x08 + irq : 0x70 + irq - 8;
}

static void __interrupt __far sb_handler(void)
{
    inp(s_sb_port + 0xf);           /* acknowledge 16 bit DMA */
    s_half_played ^= 1;
    if (s_sb_irq >= 8) {
        outp(0xa0, 0x20);
    }
    outp(0x20, 0x20);
}

/* Conventional memory through DPMI, so the DMA controller can reach it. */
static void *dos_alloc(u32 bytes)
{
    union REGS r;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = (unsigned short)((bytes + 15) >> 4);
    int386(0x31, &r, &r);
    if (r.w.cflag) {
        return NULL;
    }
    return (void *)((u32)r.w.ax << 4);
}

static void dma16_start(u32 address, u32 bytes)
{
    static const int page_port[4] = { 0x8f, 0x8b, 0x89, 0x8a };
    int channel = s_sb_dma16 & 3;
    u32 words = bytes / 2;
    u32 offset = (address >> 1) & 0xffff;

    outp(0xd4, 4 | channel);                    /* mask */
    outp(0xd8, 0);                              /* clear the flip-flop */
    outp(0xd6, 0x58 | channel);                 /* single, auto-init, read */
    outp(0xc0 + channel * 4, offset & 0xff);
    outp(0xc0 + channel * 4, offset >> 8);
    outp(page_port[channel], (address >> 16) & 0xfe);
    outp(0xc2 + channel * 4, (words - 1) & 0xff);
    outp(0xc2 + channel * 4, (words - 1) >> 8);
    outp(0xd4, channel);                        /* unmask */
}

int backend_audio_open(void)
{
    u8 *raw;
    u32 address;
    int mask_port;

    if (getenv("BLASTER") == NULL) {
        return 0;
    }
    blaster_settings();
    if (!dsp_reset()) {
        return 0;
    }
    /* The buffer must not cross a 128 KB boundary for 16 bit DMA. */
    raw = dos_alloc(DMA_HALF * 4 * 2);
    if (raw == NULL) {
        return 0;
    }
    address = (u32)raw;
    if ((address & 0x1ffff) + DMA_HALF * 4 > 0x20000) {
        address = (address + 0x1ffff) & ~0x1ffffu;
    }
    s_dma_buffer = (short *)address;
    memset(s_dma_buffer, 0, DMA_HALF * 4);

    s_old_sb = _dos_getvect(irq_vector(s_sb_irq));
    _dos_setvect(irq_vector(s_sb_irq), sb_handler);
    mask_port = s_sb_irq < 8 ? 0x21 : 0xa1;
    outp(mask_port, inp(mask_port) & ~(1 << (s_sb_irq & 7)));

    dma16_start(address, DMA_HALF * 4);
    dsp_write(0x41);                            /* output rate */
    dsp_write(MIXER_RATE >> 8);
    dsp_write(MIXER_RATE & 0xff);
    dsp_write(0xb6);                            /* 16 bit, auto-init, FIFO */
    dsp_write(0x10);                            /* mono, signed */
    dsp_write((DMA_HALF - 1) & 0xff);
    dsp_write((DMA_HALF - 1) >> 8);
    s_half_filled = 1;
    return 1;
}

void backend_audio_close(void)
{
    if (s_dma_buffer == NULL) {
        return;
    }
    if (getenv("AITD_TRACE") != NULL) {
        printf("sound: %lu buffer halves mixed\n", (unsigned long)s_halves_mixed);
    }
    dsp_write(0xd9);                            /* end auto-init */
    dsp_reset();
    outp(0xd4, 4 | (s_sb_dma16 & 3));
    _dos_setvect(irq_vector(s_sb_irq), s_old_sb);
    s_dma_buffer = NULL;
}

/* Refills the half the card has finished with. */
static void audio_pump(void)
{
    int free_half;

    if (s_dma_buffer == NULL) {
        return;
    }
    free_half = s_half_played;
    if (free_half != s_half_filled) {
        mixer_render(s_dma_buffer + free_half * DMA_HALF, DMA_HALF);
        s_half_filled = free_half;
        s_halves_mixed++;
    }
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

/* ---- MPU-401 MIDI -------------------------------------------------------- */

static tml_message *s_song;
static tml_message *s_next;
static u32 s_song_start;
static int s_song_loop;
static int s_mpu_ready;

static void mpu_write(int value)
{
    int tries = 0xffff;

    while ((inp(s_mpu_port + 1) & 0x40) && --tries > 0) {
    }
    outp(s_mpu_port, value);
}

static int mpu_init(void)
{
    int tries = 0xffff;

    blaster_settings();
    while ((inp(s_mpu_port + 1) & 0x40) && --tries > 0) {
    }
    outp(s_mpu_port + 1, 0x3f);                 /* UART mode */
    return tries > 0;
}

static void mpu_all_off(void)
{
    int channel;

    for (channel = 0; channel < 16; channel++) {
        mpu_write(0xb0 | channel);
        mpu_write(123);                         /* all notes off */
        mpu_write(0);
    }
}

static void mpu_send(const tml_message *m)
{
    switch (m->type) {
    case TML_NOTE_ON:
    case TML_NOTE_OFF:
    case TML_KEY_PRESSURE:
        mpu_write(m->type | m->channel);
        mpu_write(m->key);
        mpu_write(m->velocity);
        break;
    case TML_CONTROL_CHANGE:
        mpu_write(m->type | m->channel);
        mpu_write(m->control);
        mpu_write(m->control_value);
        break;
    case TML_PROGRAM_CHANGE:
        mpu_write(m->type | m->channel);
        mpu_write(m->program);
        break;
    case TML_CHANNEL_PRESSURE:
        mpu_write(m->type | m->channel);
        mpu_write(m->channel_pressure);
        break;
    case TML_PITCH_BEND:
        mpu_write(m->type | m->channel);
        mpu_write(m->pitch_bend & 0x7f);
        mpu_write((m->pitch_bend >> 7) & 0x7f);
        break;
    default:
        break;
    }
}

static void midi_pump(void)
{
    u32 now;

    if (s_song == NULL) {
        return;
    }
    now = s_ticks - s_song_start;
    while (s_next != NULL && s_next->time <= now) {
        mpu_send(s_next);
        s_next = s_next->next;
    }
    if (s_next == NULL && s_song_loop) {
        mpu_all_off();
        s_next = s_song;
        s_song_start = s_ticks;
    }
}

void platform_midi_stop(void)
{
    if (s_song != NULL) {
        mpu_all_off();
        tml_free(s_song);
        s_song = s_next = NULL;
    }
}

int platform_midi_play(const void *data, u32 size, int loop)
{
    platform_midi_stop();
    if (!s_mpu_ready) {
        s_mpu_ready = mpu_init() ? 1 : -1;
    }
    if (s_mpu_ready < 0 || data == NULL || size == 0) {
        return 0;
    }
    s_song = tml_load_memory(data, (int)size);
    if (s_song == NULL) {
        return 0;
    }
    s_next = s_song;
    s_song_start = s_ticks;
    s_song_loop = loop;
    return 1;
}

/* ---- events -------------------------------------------------------------- */

int platform_poll_event(struct platform_event *out)
{
    audio_pump();
    midi_pump();

    if (s_event_head == s_event_tail) {
        return 0;
    }
    _disable();
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    _enable();
    return 1;
}

/* ---- files and dialogs --------------------------------------------------- */

void backend_default_data_dir(char *out, size_t out_size)
{
    snprintf(out, out_size, ".");
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return 0;
}

void platform_error_box(const char *caption, const char *message)
{
    dos_shutdown();
    fprintf(stderr, "%s: %s\n", caption, message);
}

/* Reads back what the VGA shows: mode 13h memory and the DAC. */
int platform_video_capture(const char *path)
{
    u8 dac[768];
    FILE *f;
    int i;

    if (!s_video_on || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    outp(0x3c7, 0);
    for (i = 0; i < 768; i++) {
        int level = inp(0x3c9) & 0x3f;

        dac[i] = (u8)(level << 2 | level >> 4);
    }
    fprintf(f, "P6\n320 200\n255\n");
    for (i = 0; i < 64000; i++) {
        fwrite(dac + s_vga[i] * 3, 1, 3, f);
    }
    fclose(f);
    return 1;
}
