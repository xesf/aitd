/*
 * linux.c - Linux backend: X11 window, ALSA audio, joystick devices.
 * Also the Steam Deck build (its desktop mode and Gaming Mode both run X11
 * programs through XWayland or gamescope).
 *
 * The frame is expanded from 8 bit indexed to 32 bit and stretched to the
 * window in software, which keeps the 4:3 shape of a DOS screen. MIDI
 * music goes through the soundfont player (midi_tsf.c).
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <alsa/asoundlib.h>
#include <linux/joystick.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

static Display *s_display;
static Window s_window;
static GC s_gc;
static XImage *s_image;
static Atom s_wm_delete;
static int s_win_w, s_win_h;     /* the window's client size */
static int s_width, s_height;    /* the game's frame */
static u32 s_palette[256];       /* 0x00RRGGBB */
static u8 *s_frame;              /* the last frame presented */
static struct timespec s_start;

/* ---- events -------------------------------------------------------------- */

#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head, s_event_tail;

static void event_push(enum platform_event_kind kind, int code, int x, int y)
{
    int next = (s_event_tail + 1) % EVENT_QUEUE_SIZE;

    if (next == s_event_head) {
        return;
    }
    s_events[s_event_tail].kind = kind;
    s_events[s_event_tail].code = code;
    s_events[s_event_tail].x = x;
    s_events[s_event_tail].y = y;
    s_event_tail = next;
}

static void pad_emit(int down, int scancode)
{
    event_push(down ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP, scancode, 0, 0);
}

/* X keysym -> PC set 1 scancode, extended keys with 0x80 set. */
static int keysym_scancode(KeySym key)
{
    static const struct { KeySym sym; int code; } map[] = {
        { XK_Escape, 0x01 }, { XK_Tab, 0x0f }, { XK_Return, 0x1c }, { XK_KP_Enter, 0x9c },
        { XK_space, 0x39 }, { XK_BackSpace, 0x0e }, { XK_Control_L, 0x1d },
        { XK_Control_R, 0x9d }, { XK_Shift_L, 0x2a }, { XK_Shift_R, 0x36 },
        { XK_Alt_L, 0x38 }, { XK_semicolon, 0x27 }, { XK_minus, 0x0c }, { XK_equal, 0x0d },
        { XK_comma, 0x33 }, { XK_period, 0x34 }, { XK_slash, 0x35 },
        { XK_Up, 0xc8 }, { XK_Down, 0xd0 }, { XK_Left, 0xcb }, { XK_Right, 0xcd },
        { XK_Home, 0xc7 }, { XK_End, 0xcf }, { XK_Page_Up, 0xc9 }, { XK_Page_Down, 0xd1 },
        { XK_F1, 0x3b }, { XK_F2, 0x3c }, { XK_F3, 0x3d }, { XK_F4, 0x3e }, { XK_F5, 0x3f },
        { XK_F6, 0x40 }, { XK_F7, 0x41 }, { XK_F8, 0x42 }, { XK_F9, 0x43 }, { XK_F10, 0x44 },
        { XK_F11, 0x57 }, { XK_F12, 0x58 },
    };
    static const char letters[] = "qwertyuiopasdfghjklzxcvbnm";
    static const u8 letter_codes[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
        0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
        0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32
    };
    size_t i;

    if (key >= XK_a && key <= XK_z) {
        for (i = 0; letters[i] != '\0'; i++) {
            if ((KeySym)letters[i] == key) {
                return letter_codes[i];
            }
        }
    }
    if (key >= XK_A && key <= XK_Z) {
        return keysym_scancode(key - XK_A + XK_a);
    }
    if (key >= XK_1 && key <= XK_9) {
        return 0x02 + (int)(key - XK_1);
    }
    if (key == XK_0) {
        return 0x0b;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (map[i].sym == key) {
            return map[i].code;
        }
    }
    return 0;
}

/* ---- joystick ------------------------------------------------------------ */

static int s_js_fd = -1;
static unsigned s_js_buttons;
static int s_js_axis[2];
static int s_js_hat[2];
static u32 s_js_retry;

/* The common layout of the Linux joystick driver for Xbox-style pads (the
 * Steam Deck's included): buttons 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 Back,
 * 7 Start; axes 0/1 left stick, 6/7 the D-pad. */
static void js_poll(void)
{
    static const unsigned buttons[8] = {
        PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R, PAD_SELECT, PAD_START
    };
    struct js_event e;
    unsigned mask;

    if (s_js_fd < 0) {
        u32 now = platform_get_ticks();
        char path[32];
        int i;

        if (now - s_js_retry < 2000) {
            return;
        }
        s_js_retry = now;
        for (i = 0; i < 4 && s_js_fd < 0; i++) {
            snprintf(path, sizeof(path), "/dev/input/js%d", i);
            s_js_fd = open(path, O_RDONLY | O_NONBLOCK);
        }
        if (s_js_fd < 0) {
            return;
        }
    }

    errno = 0;
    while (read(s_js_fd, &e, sizeof(e)) == sizeof(e)) {
        int type = e.type & ~JS_EVENT_INIT;

        if (type == JS_EVENT_BUTTON && e.number < 8) {
            if (e.value) {
                s_js_buttons |= buttons[e.number];
            } else {
                s_js_buttons &= ~buttons[e.number];
            }
        } else if (type == JS_EVENT_AXIS) {
            if (e.number < 2) {
                s_js_axis[e.number] = e.value;
            } else if (e.number == 6 || e.number == 7) {
                s_js_hat[e.number - 6] = e.value;
            }
        }
    }
    if (errno != 0 && errno != EAGAIN) {
        close(s_js_fd);     /* unplugged */
        s_js_fd = -1;
        s_js_buttons = 0;
    }

    mask = s_js_buttons;
    if (s_js_hat[0] < 0) mask |= PAD_LEFT;
    if (s_js_hat[0] > 0) mask |= PAD_RIGHT;
    if (s_js_hat[1] < 0) mask |= PAD_UP;
    if (s_js_hat[1] > 0) mask |= PAD_DOWN;
    pad_update(mask, s_js_axis[0], s_js_axis[1], pad_emit);
}

/* ---- window -------------------------------------------------------------- */

static void make_image(void)
{
    if (s_image != NULL) {
        XDestroyImage(s_image);     /* frees the pixels too */
    }
    s_image = XCreateImage(s_display, DefaultVisual(s_display, DefaultScreen(s_display)),
                           24, ZPixmap, 0, malloc((size_t)s_win_w * s_win_h * 4),
                           (unsigned)s_win_w, (unsigned)s_win_h, 32, 0);
}

int platform_video_init(const char *title, int width, int height)
{
    const char *scale_env = getenv("AITD_WINDOW_SCALE");
    int scale = scale_env != NULL ? atoi(scale_env) : 1;
    XSizeHints hints;
    int screen, i;

    s_width = width;
    s_height = height;
    s_frame = calloc((size_t)width * height, 1);
    for (i = 0; i < 256; i++) {
        s_palette[i] = (u32)(i << 16 | i << 8 | i);
    }
    clock_gettime(CLOCK_MONOTONIC, &s_start);

    s_display = XOpenDisplay(NULL);
    if (s_display == NULL) {
        fprintf(stderr, "cannot open the X display\n");
        return 0;
    }
    if (scale < 1 || scale > 4) {
        scale = 1;
    }
    /* A 640 x 480 window: the frame stretched to a 4:3 screen. */
    s_win_w = 640 * scale;
    s_win_h = 480 * scale;
    screen = DefaultScreen(s_display);
    s_window = XCreateSimpleWindow(s_display, RootWindow(s_display, screen), 0, 0,
                                   (unsigned)s_win_w, (unsigned)s_win_h, 0,
                                   BlackPixel(s_display, screen), BlackPixel(s_display, screen));
    XStoreName(s_display, s_window, title);
    XSelectInput(s_display, s_window, KeyPressMask | KeyReleaseMask | ExposureMask
                 | StructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask);
    memset(&hints, 0, sizeof(hints));
    hints.flags = PAspect;
    hints.min_aspect.x = hints.max_aspect.x = 4;
    hints.min_aspect.y = hints.max_aspect.y = 3;
    XSetWMNormalHints(s_display, s_window, &hints);
    s_wm_delete = XInternAtom(s_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(s_display, s_window, &s_wm_delete, 1);
    XMapWindow(s_display, s_window);
    s_gc = DefaultGC(s_display, screen);
    XkbSetDetectableAutoRepeat(s_display, True, NULL);
    make_image();
    XFlush(s_display);
    return 1;
}

void platform_video_shutdown(void)
{
    if (s_display != NULL) {
        if (s_image != NULL) {
            XDestroyImage(s_image);
            s_image = NULL;
        }
        XDestroyWindow(s_display, s_window);
        XCloseDisplay(s_display);
        s_display = NULL;
    }
    free(s_frame);
    s_frame = NULL;
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    for (i = 0; i < 256; i++) {
        s_palette[i] = (u32)palette[i * 3] << 16 | (u32)palette[i * 3 + 1] << 8 | palette[i * 3 + 2];
    }
}

static void draw(void)
{
    u32 *out = (u32 *)s_image->data;
    int x, y;

    for (y = 0; y < s_win_h; y++) {
        const u8 *row = s_frame + (size_t)(y * s_height / s_win_h) * s_width;
        u32 *dst = (u32 *)((u8 *)out + (size_t)y * s_image->bytes_per_line);

        for (x = 0; x < s_win_w; x++) {
            dst[x] = s_palette[row[x * s_width / s_win_w]];
        }
    }
    XPutImage(s_display, s_window, s_gc, s_image, 0, 0, 0, 0, (unsigned)s_win_w, (unsigned)s_win_h);
    XFlush(s_display);
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    if (s_display == NULL || width != s_width || height != s_height) {
        return;
    }
    memcpy(s_frame, indexed_pixels, (size_t)width * height);
    draw();
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    (void)cursor_id;
}

static void mouse_event(enum platform_event_kind kind, int code, int wx, int wy)
{
    int x = wx * s_width / (s_win_w > 0 ? s_win_w : 1);
    int y = wy * s_height / (s_win_h > 0 ? s_win_h : 1);

    event_push(kind, code, x < 0 ? 0 : x >= s_width ? s_width - 1 : x,
               y < 0 ? 0 : y >= s_height ? s_height - 1 : y);
}

int platform_poll_event(struct platform_event *out)
{
    while (s_display != NULL && XPending(s_display) > 0) {
        XEvent e;

        XNextEvent(s_display, &e);
        switch (e.type) {
        case KeyPress:
        case KeyRelease: {
            int code = keysym_scancode(XLookupKeysym(&e.xkey, 0));
            if (code != 0) {
                event_push(e.type == KeyPress ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP,
                           code, 0, 0);
            }
            break;
        }
        case ConfigureNotify:
            if (e.xconfigure.width != s_win_w || e.xconfigure.height != s_win_h) {
                s_win_w = e.xconfigure.width;
                s_win_h = e.xconfigure.height;
                make_image();
            }
            break;
        case Expose:
            if (e.xexpose.count == 0) {
                draw();
            }
            break;
        case MotionNotify:
            mouse_event(PLATFORM_EVENT_MOUSE_MOVE, 0, e.xmotion.x, e.xmotion.y);
            break;
        case ButtonPress:
        case ButtonRelease:
            if (e.xbutton.button == Button1 || e.xbutton.button == Button3) {
                mouse_event(PLATFORM_EVENT_MOUSE_BUTTON, e.xbutton.button == Button1 ? 1 : 2,
                            e.xbutton.x, e.xbutton.y);
            }
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == s_wm_delete) {
                event_push(PLATFORM_EVENT_QUIT, 0, 0, 0);
            }
            break;
        default:
            break;
        }
    }
    js_poll();

    if (s_event_head == s_event_tail) {
        return 0;
    }
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

u32 platform_get_ticks(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)((now.tv_sec - s_start.tv_sec) * 1000 + (now.tv_nsec - s_start.tv_nsec) / 1000000);
}

void platform_delay(u32 milliseconds)
{
    struct timespec t;

    t.tv_sec = milliseconds / 1000;
    t.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

/* ---- audio --------------------------------------------------------------- */

#define AUDIO_FRAMES 512

static snd_pcm_t *s_pcm;
static pthread_t s_audio_thread;
static volatile int s_audio_running;

static void *audio_thread(void *arg)
{
    short buffer[AUDIO_FRAMES];

    (void)arg;
    while (s_audio_running) {
        snd_pcm_sframes_t n;

        mixer_render(buffer, AUDIO_FRAMES);
        n = snd_pcm_writei(s_pcm, buffer, AUDIO_FRAMES);
        if (n < 0) {
            snd_pcm_recover(s_pcm, (int)n, 1);
        }
    }
    return NULL;
}

int backend_audio_open(void)
{
    if (snd_pcm_open(&s_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        s_pcm = NULL;
        return 0;
    }
    if (snd_pcm_set_params(s_pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
                           1, MIXER_RATE, 1, 60000) < 0) {
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return 0;
    }
    s_audio_running = 1;
    if (pthread_create(&s_audio_thread, NULL, audio_thread, NULL) != 0) {
        s_audio_running = 0;
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return 0;
    }
    return 1;
}

void backend_audio_close(void)
{
    if (s_pcm != NULL) {
        s_audio_running = 0;
        pthread_join(s_audio_thread, NULL);
        snd_pcm_drop(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
    }
}

/* The mixer reads ripped WAV tracks itself; there is no other decoder. */
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

/* The folder that holds the program. */
void backend_default_data_dir(char *out, size_t out_size)
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char *slash;

    if (n <= 0) {
        snprintf(out, out_size, ".");
        return;
    }
    exe[n] = '\0';
    slash = strrchr(exe, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    snprintf(out, out_size, "%s", exe);
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return 0;
}

void platform_error_box(const char *caption, const char *message)
{
    fprintf(stderr, "[%s] %s\n", caption, message);
}

int platform_video_capture(const char *path)
{
    FILE *f;
    int x, y;

    if (s_image == NULL || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_win_w, s_win_h);
    for (y = 0; y < s_win_h; y++) {
        const u32 *row = (const u32 *)(s_image->data + (size_t)y * s_image->bytes_per_line);

        for (x = 0; x < s_win_w; x++) {
            u8 rgb[3] = { (u8)(row[x] >> 16), (u8)(row[x] >> 8), (u8)row[x] };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 1;
}
