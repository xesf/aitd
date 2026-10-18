/*
 * platform_macos.m - native macOS backend (AppKit, CoreGraphics, AudioToolbox).
 *
 * Replaces the Win32 layer of the original build:
 *
 *   RegisterClassA / CreateWindowExA   0x41e338  video_create_window
 *   PeekMessageA / DispatchMessageA    0x419b5f  idle and paint hook
 *   BeginPaint / BitBlt                          frame presentation
 *   CreatePalette / RealizePalette     0x41fadb  palette_init
 *   MessageBoxA                        0x419d92  sys_error_box
 *   FindWindowA                        0x41a152  sys_is_already_running
 *   mciSendStringA                     0x401010  cdaudio_init
 *   SetTimer / KillTimer               0x41d945  timer_init
 *
 * The 8bpp indexed plane is handed to CoreGraphics as an indexed colour space,
 * which is the closest native match to the VGA DAC the original programmed and
 * avoids converting the framebuffer to RGBA every frame.
 */

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <stdlib.h>

#include <mach/mach_time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "aitd.h"
#include "platform.h"

/* ---- state ------------------------------------------------------------- */

@class aitd_view;

static NSWindow *s_window;
static aitd_view *s_view;
static CGColorSpaceRef s_colorspace;
static CGDataProviderRef s_provider;
static u8 *s_pixels;                 /* the indexed plane handed to CG */
static u8 s_palette[256 * 3];        /* 8 bit per channel, CG order */
static int s_width;
static int s_height;
static uint64_t s_start_ticks;
static mach_timebase_info_data_t s_timebase;
static int s_quit_posted;

/* Events are drained from the AppKit queue into this ring so the engine can
 * keep its original "poll until empty" shape (the Win32 build did the same
 * with PeekMessageA). */
#define EVENT_QUEUE_SIZE 64
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head;
static int s_event_tail;

static void event_push(enum platform_event_kind kind, int code, int x, int y)
{
    int next = (s_event_tail + 1) % EVENT_QUEUE_SIZE;

    if (next == s_event_head) {
        return;  /* full; dropping is preferable to blocking the pump */
    }

    s_events[s_event_tail].kind = kind;
    s_events[s_event_tail].code = code;
    s_events[s_event_tail].x = x;
    s_events[s_event_tail].y = y;
    s_event_tail = next;
}

static int event_pop(struct platform_event *out)
{
    if (s_event_head == s_event_tail) {
        return 0;
    }

    *out = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

/* ---- view -------------------------------------------------------------- */

/*
 * Draws the indexed plane directly. The provider wraps s_pixels so a frame
 * costs one CGImageCreate plus the blit; the palette lives in the colour
 * space, which is rebuilt only when platform_video_set_palette is called.
 */
@interface aitd_view : NSView
@end

@implementation aitd_view

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;

    if (s_pixels == NULL || s_colorspace == NULL || s_provider == NULL) {
        return;
    }

    CGImageRef image = CGImageCreate((size_t)s_width,
                                     (size_t)s_height,
                                     8, 8,
                                     (size_t)s_width,
                                     s_colorspace,
                                     kCGBitmapByteOrderDefault,
                                     s_provider,
                                     NULL,
                                     false,
                                     kCGRenderingIntentDefault);
    if (image == NULL) {
        return;
    }

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];

    /* Nearest neighbour keeps the 320x200 pixel grid intact when the window
     * is scaled up. */
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);

    /*
     * This view is flipped so that mouse coordinates arrive top-down, matching
     * the engine's framebuffer. AppKit implements that with a y-flipping CTM,
     * but CGContextDrawImage places images bottom-up, so drawing straight into
     * a flipped view renders the frame upside down. Undo the flip around the
     * image only.
     */
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0.0, self.bounds.size.height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, self.bounds, image);
    CGContextRestoreGState(ctx);

    CGImageRelease(image);
}

- (void)keyDown:(NSEvent *)event
{
    if (event.isARepeat) {
        return;
    }
    event_push(PLATFORM_EVENT_KEY_DOWN, (int)event.keyCode, 0, 0);
}

/* Modifier keys only arrive as flag changes. Control is the game's second
 * action key, so it has to reach the engine like any other key. */
- (void)flagsChanged:(NSEvent *)event
{
    NSEventModifierFlags mask = 0;

    switch (event.keyCode) {
    case 0x3b: case 0x3e: mask = NSEventModifierFlagControl; break;
    case 0x38: case 0x3c: mask = NSEventModifierFlagShift; break;
    case 0x3a: case 0x3d: mask = NSEventModifierFlagOption; break;
    default: return;
    }

    event_push((event.modifierFlags & mask) ? PLATFORM_EVENT_KEY_DOWN
                                            : PLATFORM_EVENT_KEY_UP,
               (int)event.keyCode, 0, 0);
}

- (void)keyUp:(NSEvent *)event
{
    event_push(PLATFORM_EVENT_KEY_UP, (int)event.keyCode, 0, 0);
}

/* Mouse coordinates are reported in the engine's own 320x200 space so the
 * scale factor of the window never leaks into game logic. */
- (void)report_mouse:(NSEvent *)event kind:(enum platform_event_kind)kind code:(int)code
{
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect bounds = self.bounds;
    int x = 0;
    int y = 0;

    if (bounds.size.width > 0.0 && bounds.size.height > 0.0) {
        x = (int)(point.x * (CGFloat)s_width / bounds.size.width);
        y = (int)(point.y * (CGFloat)s_height / bounds.size.height);
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= s_width) x = s_width - 1;
    if (y >= s_height) y = s_height - 1;

    event_push(kind, code, x, y);
}

- (void)mouseMoved:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_MOVE code:0];
}

- (void)mouseDragged:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_MOVE code:0];
}

- (void)mouseDown:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_BUTTON code:1];
}

- (void)mouseUp:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_BUTTON code:1];
}

- (void)rightMouseDown:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_BUTTON code:2];
}

- (void)rightMouseUp:(NSEvent *)event
{
    [self report_mouse:event kind:PLATFORM_EVENT_MOUSE_BUTTON code:2];
}

@end

/* ---- window delegate --------------------------------------------------- */

@interface aitd_window_delegate : NSObject <NSWindowDelegate>
@end

@implementation aitd_window_delegate

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    event_push(PLATFORM_EVENT_QUIT, 0, 0, 0);
    s_quit_posted = 1;
    return NO;  /* the engine decides when to tear down, not AppKit */
}

@end

static aitd_window_delegate *s_delegate;

/* ---- data directory ---------------------------------------------------- */

/*
 * Assets sit next to the executable by default, or inside Contents/Resources
 * when the binary is bundled. AITD_DATA_DIR overrides both.
 */
static const char *data_dir(void)
{
    static char cached[1024];

    if (cached[0] != '\0') {
        return cached;
    }

    const char *env = getenv("AITD_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(cached, sizeof(cached), "%s", env);
        return cached;
    }

    NSString *resources = [[NSBundle mainBundle] resourcePath];
    if (resources != nil) {
        snprintf(cached, sizeof(cached), "%s", resources.UTF8String);
    } else {
        snprintf(cached, sizeof(cached), ".");
    }

    return cached;
}

/*
 * The original opened "itd_ress.pak" and "DEFINES.ITD" with the casing baked
 * into the binary, which DOS and Windows resolve case insensitively. APFS is
 * usually case insensitive too, but case sensitive volumes exist, so fall back
 * to a directory scan.
 */
/* Save games live in <data>/SAVE0<n>/, the directory layout of the original. */
int platform_save_path(const char *dir_name, const char *file_name, char *out,
                       size_t out_size, int create)
{
    if (snprintf(out, out_size, "%s/%s", data_dir(), dir_name) >= (int)out_size) {
        return 0;
    }
    if (create) {
        mkdir(out, 0755);
    }
    if (file_name != NULL) {
        size_t len = strlen(out);
        if (snprintf(out + len, out_size - len, "/%s", file_name) >= (int)(out_size - len)) {
            return 0;
        }
    }
    return 1;
}

int platform_resolve_path(const char *name, char *out, size_t out_size)
{
    const char *dir = data_dir();
    DIR *handle;
    struct dirent *entry;

    if (snprintf(out, out_size, "%s/%s", dir, name) >= (int)out_size) {
        return 0;
    }

    if (access(out, R_OK) == 0) {
        return 1;
    }

    handle = opendir(dir);
    if (handle == NULL) {
        return 0;
    }

    while ((entry = readdir(handle)) != NULL) {
        if (strcasecmp(entry->d_name, name) == 0) {
            snprintf(out, out_size, "%s/%s", dir, entry->d_name);
            closedir(handle);
            return 1;
        }
    }

    closedir(handle);
    return 0;
}

/* ---- video ------------------------------------------------------------- */

static void rebuild_colorspace(void)
{
    CGColorSpaceRef base = CGColorSpaceCreateDeviceRGB();

    if (s_colorspace != NULL) {
        CGColorSpaceRelease(s_colorspace);
    }

    s_colorspace = CGColorSpaceCreateIndexed(base, 255, s_palette);
    CGColorSpaceRelease(base);
}

int platform_video_init(const char *title, int width, int height)
{
    @autoreleasepool {
        s_width = width;
        s_height = height;

        s_pixels = calloc((size_t)width * (size_t)height, 1);
        if (s_pixels == NULL) {
            return 0;
        }

        s_provider = CGDataProviderCreateWithData(NULL, s_pixels,
                                                  (size_t)width * (size_t)height,
                                                  NULL);
        if (s_provider == NULL) {
            return 0;
        }

        /* A neutral ramp keeps the first frames visible before the game
         * uploads its own palette. */
        for (int i = 0; i < 256; i++) {
            s_palette[i * 3 + 0] = (u8)i;
            s_palette[i * 3 + 1] = (u8)i;
            s_palette[i * 3 + 2] = (u8)i;
        }
        rebuild_colorspace();

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(0, 0, width * 3, height * 3);
        NSUInteger style = NSWindowStyleMaskTitled
                         | NSWindowStyleMaskClosable
                         | NSWindowStyleMaskMiniaturizable
                         | NSWindowStyleMaskResizable;

        s_window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        if (s_window == nil) {
            return 0;
        }

        /* NSWindow defaults releasedWhenClosed to YES for programmatically
         * created windows. Under ARC that is a double release: -close drops
         * one reference and ARC drops another when s_window is cleared. */
        s_window.releasedWhenClosed = NO;

        s_delegate = [[aitd_window_delegate alloc] init];
        s_window.delegate = s_delegate;
        s_window.title = [NSString stringWithUTF8String:title];
        [s_window setAcceptsMouseMovedEvents:YES];

        /* Preserve the 8:5 aspect of the original mode when resizing. */
        s_window.contentAspectRatio = NSMakeSize(width, height);

        s_view = [[aitd_view alloc] initWithFrame:frame];
        s_window.contentView = s_view;
        [s_window makeFirstResponder:s_view];
        [s_window center];
        [s_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        /* AppKit needs to reach the point where it will deliver events, but
         * the engine owns the main loop, so finishLaunching is called
         * directly instead of NSApp run. */
        [NSApp finishLaunching];

        mach_timebase_info(&s_timebase);
        s_start_ticks = mach_absolute_time();

        return 1;
    }
}

void platform_video_shutdown(void)
{
    @autoreleasepool {
        if (s_window != nil) {
            s_window.delegate = nil;  /* delegate is unsafe_unretained */
            [s_window close];
            s_window = nil;
        }
        s_view = nil;
        s_delegate = nil;
    }

    if (s_provider != NULL) {
        CGDataProviderRelease(s_provider);
        s_provider = NULL;
    }
    if (s_colorspace != NULL) {
        CGColorSpaceRelease(s_colorspace);
        s_colorspace = NULL;
    }

    free(s_pixels);
    s_pixels = NULL;
}

/*
 * Palettes in this build are 8 bit per channel, not 6 bit VGA.
 *
 * Evidence: the static overlay at 0x435068 tops out at 0xfc, and the menu
 * palette in itd_ress.pak entry 0x2f reaches 0xff with values that are not
 * multiples of four. This is the Win32 port, which hands the palette to
 * CreatePalette as PALETTEENTRY records, so no DAC widening applies.
 */
void platform_video_set_palette(const u8 *palette)
{
    memcpy(s_palette, palette, sizeof(s_palette));
    rebuild_colorspace();
}

/*
 * Replaces the blit the Win32 build performed from g_blit_source (0x444858)
 * using g_blit_width/g_blit_height (0x444854/0x444856).
 */
void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    if (s_pixels == NULL || width != s_width || height != s_height) {
        return;
    }

    memcpy(s_pixels, indexed_pixels, (size_t)width * (size_t)height);

    @autoreleasepool {
        [s_view setNeedsDisplay:YES];
        [s_view displayIfNeeded];
    }
}

/* The Win32 build loaded an icon resource named "AppIcon" (0x407260). Here the
 * icon is a bundle property, so only an explicit override does anything. */
void platform_set_icon(const char *resource_name)
{
    @autoreleasepool {
        if (resource_name == NULL) {
            return;
        }

        NSString *name = [NSString stringWithUTF8String:resource_name];
        NSImage *image = [NSImage imageNamed:name];

        if (image != nil) {
            [NSApp setApplicationIconImage:image];
        }
    }
}

void platform_set_cursor(int cursor_id)
{
    @autoreleasepool {
        if (cursor_id != 0) {
            [NSCursor unhide];
        } else {
            [NSCursor hide];
        }
    }
}

/* ---- event pump -------------------------------------------------------- */

/*
 * Drains AppKit first, then hands the engine one translated event at a time,
 * matching the PeekMessageA loop the original ran from its idle hook.
 */
int platform_poll_event(struct platform_event *out)
{
    @autoreleasepool {
        for (;;) {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil) {
                break;
            }
            [NSApp sendEvent:event];
        }
    }

    return event_pop(out);
}

u32 platform_get_ticks(void)
{
    uint64_t elapsed = mach_absolute_time() - s_start_ticks;

    if (s_timebase.denom == 0) {
        return 0;
    }

    return (u32)(elapsed * s_timebase.numer / s_timebase.denom / 1000000ull);
}

void platform_delay(u32 milliseconds)
{
    usleep(milliseconds * 1000u);
}

/* ---- audio ------------------------------------------------------------- */

/*
 * AudioQueue stands in for the waveOut device the original drove. The game
 * plays one sample at a time (0x402ec7 stops the current one first), so the
 * backend is a single voice: an 8 bit unsigned buffer at its own rate,
 * resampled to the output rate in the callback.
 */
#define AUDIO_BUFFER_COUNT 3
#define AUDIO_BUFFER_BYTES 2048
#define AUDIO_SAMPLE_RATE 22050

static AudioQueueRef s_queue;
static AudioQueueBufferRef s_buffers[AUDIO_BUFFER_COUNT];
static pthread_mutex_t s_voice_lock = PTHREAD_MUTEX_INITIALIZER;
static u8 *s_voice;
static u32 s_voice_size;
static u32 s_voice_step;    /* 16.16 source samples per output sample */
static u64 s_voice_pos;     /* 16.16 */
static int s_voice_loops;   /* plays left, 0 = forever */
static int s_voice_active;

static void audio_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    u8 *out = buffer->mAudioData;
    u32 i;

    (void)user;

    pthread_mutex_lock(&s_voice_lock);
    for (i = 0; i < AUDIO_BUFFER_BYTES; i++) {
        u32 index;

        if (!s_voice_active) {
            out[i] = 0x80;
            continue;
        }
        index = (u32)(s_voice_pos >> 16);
        if (index >= s_voice_size) {
            if (s_voice_loops == 1) {
                s_voice_active = 0;
                out[i] = 0x80;
                continue;
            }
            if (s_voice_loops > 1) {
                s_voice_loops--;
            }
            s_voice_pos = 0;
            index = 0;
        }
        out[i] = s_voice[index];
        s_voice_pos += s_voice_step;
    }
    pthread_mutex_unlock(&s_voice_lock);

    buffer->mAudioDataByteSize = AUDIO_BUFFER_BYTES;
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

int platform_sound_init(void)
{
    AudioStreamBasicDescription format;

    memset(&format, 0, sizeof(format));
    format.mSampleRate = AUDIO_SAMPLE_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsPacked;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 8;
    format.mBytesPerFrame = 1;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = 1;

    if (AudioQueueNewOutput(&format, audio_callback, NULL, NULL, NULL, 0,
                            &s_queue) != noErr) {
        return 0;
    }

    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        if (AudioQueueAllocateBuffer(s_queue, AUDIO_BUFFER_BYTES,
                                     &s_buffers[i]) != noErr) {
            return 0;
        }
        memset(s_buffers[i]->mAudioData, 0x80, AUDIO_BUFFER_BYTES);
        s_buffers[i]->mAudioDataByteSize = AUDIO_BUFFER_BYTES;
        AudioQueueEnqueueBuffer(s_queue, s_buffers[i], 0, NULL);
    }

    if (AudioQueueStart(s_queue, NULL) != noErr) {
        return 0;
    }

    return 1;
}

void platform_sound_shutdown(void)
{
    if (s_queue != NULL) {
        AudioQueueStop(s_queue, true);
        AudioQueueDispose(s_queue, true);
        s_queue = NULL;
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

    pthread_mutex_lock(&s_voice_lock);
    free(s_voice);
    s_voice = copy;
    s_voice_size = size;
    s_voice_step = (u32)(((u64)rate << 16) / AUDIO_SAMPLE_RATE);
    s_voice_pos = 0;
    s_voice_loops = loops;
    s_voice_active = 1;
    pthread_mutex_unlock(&s_voice_lock);
}

void platform_sound_stop(void)
{
    pthread_mutex_lock(&s_voice_lock);
    s_voice_active = 0;
    free(s_voice);
    s_voice = NULL;
    s_voice_size = 0;
    pthread_mutex_unlock(&s_voice_lock);
}

int platform_sound_playing(void)
{
    int active;

    pthread_mutex_lock(&s_voice_lock);
    active = s_voice_active;
    pthread_mutex_unlock(&s_voice_lock);
    return active;
}

/* ---- CD audio ---------------------------------------------------------- */

/*
 * The original drove a physical drive through mciSendStringA and treated a
 * failure as fatal after five retries (0x407326..0x407389). macOS exposes no
 * redbook playback, so tracks are expected as ripped files beside the game
 * data and a missing set degrades to silence rather than aborting startup.
 */
int platform_cdaudio_init(void)
{
    return 1;
}

void platform_cdaudio_shutdown(void)
{
    platform_cdaudio_stop();
}

void platform_cdaudio_play_track(int track)
{
    char path[1024];
    char name[64];

    snprintf(name, sizeof(name), "track%02d.m4a", track);

    if (!platform_resolve_path(name, path, sizeof(path))) {
        return;
    }

    /* Playback is owned by the music module; the lookup is the platform part. */
}

void platform_cdaudio_stop(void)
{
}

/* ---- misc -------------------------------------------------------------- */

/*
 * FindWindowA was free on Win32. The native equivalent is checking for another
 * instance of the same bundle identifier.
 */
int platform_is_already_running(const char *title)
{
    (void)title;

    @autoreleasepool {
        NSString *bundle_id = [[NSBundle mainBundle] bundleIdentifier];

        if (bundle_id == nil) {
            return 0;  /* unbundled build: no reliable identity to match on */
        }

        NSArray *running = [NSRunningApplication
            runningApplicationsWithBundleIdentifier:bundle_id];

        return running.count > 1 ? 1 : 0;
    }
}

void platform_error_box(const char *caption, const char *message)
{
    /* A modal alert deadlocks any non-interactive run, so anything driving
     * the engine from a script sets AITD_NO_DIALOGS and gets stderr instead. */
    if (getenv("AITD_NO_DIALOGS") != NULL) {
        fprintf(stderr, "[%s] %s\n", caption, message);
        return;
    }

    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];

        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = [NSString stringWithUTF8String:caption];
        alert.informativeText = [NSString stringWithUTF8String:message];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

/*
 * Renders the view exactly as it appears on screen and writes it out as a PPM.
 *
 * This goes through -cacheDisplayInRect:, so it exercises the real drawRect
 * path including the flipped-view CTM. That is the point: a frame dumped from
 * the engine's own buffer cannot catch a display-side orientation bug, but
 * this can.
 */
int platform_video_capture(const char *path)
{
    @autoreleasepool {
        if (s_view == nil) {
            return 0;
        }

        NSRect bounds = s_view.bounds;
        NSBitmapImageRep *rep =
            [s_view bitmapImageRepForCachingDisplayInRect:bounds];

        if (rep == nil) {
            return 0;
        }

        [s_view cacheDisplayInRect:bounds toBitmapImageRep:rep];

        NSInteger w = rep.pixelsWide;
        NSInteger h = rep.pixelsHigh;
        FILE *fp = fopen(path, "wb");

        if (fp == NULL) {
            return 0;
        }

        fprintf(fp, "P6\n%ld %ld\n255\n", (long)w, (long)h);

        for (NSInteger y = 0; y < h; y++) {
            for (NSInteger x = 0; x < w; x++) {
                NSColor *c = [rep colorAtX:x y:y];
                unsigned char rgb[3];

                rgb[0] = (unsigned char)(c.redComponent * 255.0);
                rgb[1] = (unsigned char)(c.greenComponent * 255.0);
                rgb[2] = (unsigned char)(c.blueComponent * 255.0);
                fwrite(rgb, 1, 3, fp);
            }
        }

        fclose(fp);
        return 1;
    }
}
