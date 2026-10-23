/*
 * windows.c - Windows backend: Win32 window and GDI, waveOut sound, MIDI
 * through the MCI sequencer, gamepads through XInput.
 *
 * This is the host the original WAITD3.EXE ran on, so most entry points
 * map back to the routines platform.h names: an 8 bit DIB stretched with
 * StretchDIBits stands in for the palette and BitBlt path of
 * video_create_window (0x41e338), MessageBoxA for sys_error_box (0x419d92)
 * and FindWindowA for sys_is_already_running (0x41a152).
 *
 * Builds with MinGW-w64 for Windows 7 and later, and with Open Watcom for
 * Windows 95 and 98 (AITD_WIN9X: no XInput).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

#define CLASS_NAME "AloneInTheDark"

static HWND s_window;
static int s_width, s_height;
static u8 *s_frame;
static struct {
    BITMAPINFOHEADER header;
    RGBQUAD colours[256];
} s_bitmap;
static DWORD s_start;

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

static void mouse_report(enum platform_event_kind kind, int code, LPARAM lparam)
{
    RECT client;
    int x, y;

    GetClientRect(s_window, &client);
    x = (short)LOWORD(lparam) * s_width / (client.right > 0 ? client.right : 1);
    y = (short)HIWORD(lparam) * s_height / (client.bottom > 0 ? client.bottom : 1);
    event_push(kind, code, x < 0 ? 0 : x >= s_width ? s_width - 1 : x,
               y < 0 ? 0 : y >= s_height ? s_height - 1 : y);
}

static void paint(HDC dc)
{
    RECT client;

    if (s_frame == NULL) {
        return;
    }
    GetClientRect(s_window, &client);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, s_width, s_height,
                  s_frame, (BITMAPINFO *)&s_bitmap, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CLOSE:
        event_push(PLATFORM_EVENT_QUIT, 0, 0, 0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        /* lParam carries the set 1 scancode; bit 24 marks the extended
         * keys (arrows, right Control), which the engine sees with 0x80. */
        int code = (int)((lparam >> 16) & 0x7f) | ((lparam & (1 << 24)) ? 0x80 : 0);
        int down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;

        if (down && (lparam & (1 << 30))) {
            return 0;       /* auto-repeat */
        }
        if (code == 0x3e && (GetKeyState(VK_MENU) & 0x8000)) {
            break;          /* Alt+F4 */
        }
        event_push(down ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP, code, 0, 0);
        return 0;
    }
    case WM_MOUSEMOVE:
        mouse_report(PLATFORM_EVENT_MOUSE_MOVE, 0, lparam);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        mouse_report(PLATFORM_EVENT_MOUSE_BUTTON, 1, lparam);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        mouse_report(PLATFORM_EVENT_MOUSE_BUTTON, 2, lparam);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);

        paint(dc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static void midi_service(void);

/* ---- gamepad ------------------------------------------------------------- */

#if !defined(AITD_WIN9X)
/* XInput, loaded at run time so the program also starts without it. */
typedef struct {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
} xinput_gamepad;
typedef struct {
    DWORD packet;
    xinput_gamepad pad;
} xinput_state;
typedef DWORD (WINAPI *xinput_get_state_fn)(DWORD, xinput_state *);

static xinput_get_state_fn s_xinput_get_state;

static void pad_poll(void)
{
    static int tried;
    static const struct { WORD bit; unsigned button; } map[] = {
        { 0x0001, PAD_UP }, { 0x0002, PAD_DOWN }, { 0x0004, PAD_LEFT }, { 0x0008, PAD_RIGHT },
        { 0x0010, PAD_START }, { 0x0020, PAD_SELECT }, { 0x0100, PAD_L }, { 0x0200, PAD_R },
        { 0x1000, PAD_A }, { 0x2000, PAD_B }, { 0x4000, PAD_X }, { 0x8000, PAD_Y },
    };
    xinput_state state;
    unsigned mask = 0;
    size_t i;

    if (!tried) {
        HMODULE lib = LoadLibraryA("xinput1_4.dll");

        if (lib == NULL) {
            lib = LoadLibraryA("xinput9_1_0.dll");
        }
        if (lib != NULL) {
            s_xinput_get_state = (xinput_get_state_fn)(void (*)(void))GetProcAddress(lib, "XInputGetState");
        }
        tried = 1;
    }
    if (s_xinput_get_state == NULL) {
        return;
    }
    memset(&state, 0, sizeof(state));
    if (s_xinput_get_state(0, &state) != ERROR_SUCCESS) {
        pad_update(0, 0, 0, pad_emit);
        return;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (state.pad.buttons & map[i].bit) {
            mask |= map[i].button;
        }
    }
    /* XInput's stick y points up; the pad layout's points down. */
    pad_update(mask, state.pad.lx, -(int)state.pad.ly - 1, pad_emit);
}
#else
static void pad_poll(void)
{
}
#endif

/* ---- window -------------------------------------------------------------- */

int platform_video_init(const char *title, int width, int height)
{
    const char *scale_env = getenv("AITD_WINDOW_SCALE");
    int scale = scale_env != NULL ? atoi(scale_env) : 1;
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    RECT rect;
    DWORD style = WS_OVERLAPPEDWINDOW;
    int i;

    s_width = width;
    s_height = height;
    s_frame = calloc((size_t)width * height, 1);
    memset(&s_bitmap, 0, sizeof(s_bitmap));
    s_bitmap.header.biSize = sizeof(BITMAPINFOHEADER);
    s_bitmap.header.biWidth = width;
    s_bitmap.header.biHeight = -height;     /* top-down */
    s_bitmap.header.biPlanes = 1;
    s_bitmap.header.biBitCount = 8;
    s_bitmap.header.biCompression = BI_RGB;
    s_bitmap.header.biClrUsed = 256;
    for (i = 0; i < 256; i++) {
        s_bitmap.colours[i].rgbRed = s_bitmap.colours[i].rgbGreen = s_bitmap.colours[i].rgbBlue = (BYTE)i;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(instance, "AppIcon");
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassA(&wc);

    if (scale < 1 || scale > 4) {
        scale = 1;
    }
    /* A 640 x 480 client area: the frame stretched to a 4:3 screen. */
    rect.left = rect.top = 0;
    rect.right = 640 * scale;
    rect.bottom = 480 * scale;
    AdjustWindowRect(&rect, style, FALSE);
    s_window = CreateWindowA(CLASS_NAME, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             NULL, NULL, instance, NULL);
    if (s_window == NULL) {
        return 0;
    }
    ShowWindow(s_window, SW_SHOW);
    UpdateWindow(s_window);
    timeBeginPeriod(1);
    s_start = timeGetTime();
    return 1;
}

void platform_video_shutdown(void)
{
    if (s_window != NULL) {
        DestroyWindow(s_window);
        s_window = NULL;
    }
    timeEndPeriod(1);
    free(s_frame);
    s_frame = NULL;
}

void platform_video_set_palette(const u8 *palette)
{
    int i;

    for (i = 0; i < 256; i++) {
        s_bitmap.colours[i].rgbRed = palette[i * 3];
        s_bitmap.colours[i].rgbGreen = palette[i * 3 + 1];
        s_bitmap.colours[i].rgbBlue = palette[i * 3 + 2];
    }
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    HDC dc;

    if (s_window == NULL || width != s_width || height != s_height) {
        return;
    }
    memcpy(s_frame, indexed_pixels, (size_t)width * height);
    dc = GetDC(s_window);
    paint(dc);
    ReleaseDC(s_window, dc);
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    ShowCursor(cursor_id != 0);
}

int platform_poll_event(struct platform_event *out)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            event_push(PLATFORM_EVENT_QUIT, 0, 0, 0);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    pad_poll();
    midi_service();

    if (s_event_head == s_event_tail) {
        return 0;
    }
    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

u32 platform_get_ticks(void)
{
    return (u32)(timeGetTime() - s_start);
}

void platform_delay(u32 milliseconds)
{
    Sleep(milliseconds);
}

/* ---- audio --------------------------------------------------------------- */

#define AUDIO_BUFFERS 4
#define AUDIO_FRAMES 1024

static HWAVEOUT s_wave;
static WAVEHDR s_headers[AUDIO_BUFFERS];
static short s_samples[AUDIO_BUFFERS][AUDIO_FRAMES];
static HANDLE s_audio_event;
static HANDLE s_audio_thread;
static volatile int s_audio_running;

/* Refills each buffer the device hands back. */
static DWORD WINAPI audio_thread(LPVOID arg)
{
    (void)arg;
    while (s_audio_running) {
        int i;

        WaitForSingleObject(s_audio_event, 50);
        for (i = 0; i < AUDIO_BUFFERS; i++) {
            if (s_headers[i].dwFlags & WHDR_DONE) {
                mixer_render(s_samples[i], AUDIO_FRAMES);
                s_headers[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(s_wave, &s_headers[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

int backend_audio_open(void)
{
    WAVEFORMATEX format;
    DWORD id;
    int i;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = MIXER_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = MIXER_RATE * 2;

    s_audio_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&s_wave, WAVE_MAPPER, &format, (DWORD_PTR)s_audio_event, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(s_audio_event);
        return 0;
    }
    for (i = 0; i < AUDIO_BUFFERS; i++) {
        memset(&s_headers[i], 0, sizeof(WAVEHDR));
        s_headers[i].lpData = (LPSTR)s_samples[i];
        s_headers[i].dwBufferLength = sizeof(s_samples[i]);
        waveOutPrepareHeader(s_wave, &s_headers[i], sizeof(WAVEHDR));
        s_headers[i].dwFlags |= WHDR_DONE;
    }
    s_audio_running = 1;
    s_audio_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, &id);
    SetEvent(s_audio_event);
    return s_audio_thread != NULL;
}

void backend_audio_close(void)
{
    int i;

    if (s_wave == NULL) {
        return;
    }
    s_audio_running = 0;
    SetEvent(s_audio_event);
    WaitForSingleObject(s_audio_thread, INFINITE);
    CloseHandle(s_audio_thread);
    waveOutReset(s_wave);
    for (i = 0; i < AUDIO_BUFFERS; i++) {
        waveOutUnprepareHeader(s_wave, &s_headers[i], sizeof(WAVEHDR));
    }
    waveOutClose(s_wave);
    CloseHandle(s_audio_event);
    s_wave = NULL;
}

/* Ripped tracks in other formats (mp3) through MCI. */
static int s_cd_open;

int backend_cd_file_play(const char *path)
{
    char command[1200];

    backend_cd_file_stop();
    snprintf(command, sizeof(command), "open \"%s\" type mpegvideo alias aitd_cd", path);
    if (mciSendStringA(command, NULL, 0, NULL) != 0) {
        return 0;
    }
    s_cd_open = 1;
    return mciSendStringA("play aitd_cd", NULL, 0, NULL) == 0;
}

void backend_cd_file_stop(void)
{
    if (s_cd_open) {
        mciSendStringA("close aitd_cd", NULL, 0, NULL);
        s_cd_open = 0;
    }
}

int backend_cd_file_playing(void)
{
    char mode[32] = "";

    if (!s_cd_open) {
        return 0;
    }
    mciSendStringA("status aitd_cd mode", mode, sizeof(mode), NULL);
    return strcmp(mode, "playing") == 0;
}

/* ---- MIDI ---------------------------------------------------------------- */

/* The MCI sequencer plays a file, so the song goes to a temporary one, as
 * the original's WINMUS music did. Looping restarts it when it ends. */
static int s_midi_open;
static int s_midi_loop;
static char s_midi_path[MAX_PATH];

void platform_midi_stop(void)
{
    if (s_midi_open) {
        mciSendStringA("close aitd_midi", NULL, 0, NULL);
        s_midi_open = 0;
    }
    if (s_midi_path[0] != '\0') {
        DeleteFileA(s_midi_path);
        s_midi_path[0] = '\0';
    }
}

int platform_midi_play(const void *data, u32 size, int loop)
{
    char dir[MAX_PATH], command[MAX_PATH + 64];
    FILE *f;

    platform_midi_stop();
    if (data == NULL || size == 0) {
        return 0;
    }
    GetTempPathA(sizeof(dir), dir);
    if (GetTempFileNameA(dir, "aid", 0, s_midi_path) == 0) {
        s_midi_path[0] = '\0';
        return 0;
    }
    f = fopen(s_midi_path, "wb");
    if (f == NULL) {
        return 0;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    snprintf(command, sizeof(command), "open \"%s\" type sequencer alias aitd_midi", s_midi_path);
    if (mciSendStringA(command, NULL, 0, NULL) != 0) {
        platform_midi_stop();
        return 0;
    }
    s_midi_open = 1;
    s_midi_loop = loop;
    return mciSendStringA("play aitd_midi from 0", NULL, 0, NULL) == 0;
}

/* Restarts a looping song that has ended (polled twice a second). */
static void midi_service(void)
{
    static DWORD last;
    char mode[32] = "";

    if (!s_midi_open || !s_midi_loop || timeGetTime() - last < 500) {
        return;
    }
    last = timeGetTime();
    mciSendStringA("status aitd_midi mode", mode, sizeof(mode), NULL);
    if (strcmp(mode, "stopped") == 0) {
        mciSendStringA("play aitd_midi from 0", NULL, 0, NULL);
    }
}

/* ---- files and dialogs --------------------------------------------------- */

/* The folder that holds the program. */
void backend_default_data_dir(char *out, size_t out_size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if (n == 0 || n >= sizeof(exe)) {
        snprintf(out, out_size, ".");
        return;
    }
    slash = strrchr(exe, '\\');
    if (slash != NULL) {
        *slash = '\0';
    }
    snprintf(out, out_size, "%s", exe);
}

int platform_is_already_running(const char *title)
{
    (void)title;
    return FindWindowA(CLASS_NAME, NULL) != NULL && FindWindowA(CLASS_NAME, NULL) != s_window;
}

void platform_error_box(const char *caption, const char *message)
{
    if (getenv("AITD_NO_DIALOGS") != NULL) {
        fprintf(stderr, "[%s] %s\n", caption, message);
        return;
    }
    MessageBoxA(s_window, message, caption, MB_OK | MB_ICONERROR);
}

int platform_video_capture(const char *path)
{
    FILE *f;
    int x, y;

    if (s_frame == NULL || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_width, s_height);
    for (y = 0; y < s_height; y++) {
        for (x = 0; x < s_width; x++) {
            const RGBQUAD *c = &s_bitmap.colours[s_frame[y * s_width + x]];
            u8 rgb[3] = { c->rgbRed, c->rgbGreen, c->rgbBlue };

            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 1;
}
