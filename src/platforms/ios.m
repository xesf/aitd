/*
 * ios.m - iOS and iPadOS backend (UIKit).
 *
 * UIKit owns the main thread, so the engine runs its own loops on a
 * second thread: main.c's main() is compiled as aitd_main and started
 * once the player has picked a game. Frames reach the screen through a
 * layer whose contents are rebuilt from the 8 bit plane, sound goes
 * through an AudioQueue fed by the mixer, MIDI through the soundfont
 * player (midi_tsf.c).
 *
 * Input: an on-screen pad drawn over the picture, game controllers
 * (GameController) and hardware keyboards. All of them become PC
 * scancodes, as on the other hosts.
 *
 * The games live in the app's Documents folder, one folder per game, which
 * the Files app can reach (UIFileSharingEnabled).
 */

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <GameController/GameController.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aitd.h"
#include "platform.h"
#include "platforms/backend.h"
#include "platforms/pad.h"

int aitd_main(int argc, char **argv);

/* ---- state shared between the engine thread and UIKit -------------------- */

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_width, s_height;
static u8 *s_pixels;                 /* the last frame presented */
static u8 s_palette[768];
static CGColorSpaceRef s_colorspace;
static CALayer *s_layer;
static uint64_t s_start;
static mach_timebase_info_data_t s_timebase;
static char s_data_dir[1024];

static unsigned s_touch_buttons;     /* pad.h bits held on the screen */
static int s_touch_x, s_touch_y;     /* the on-screen stick, -32768..32767 */
static unsigned s_controller_buttons;
static int s_controller_x, s_controller_y;

#define EVENT_QUEUE_SIZE 128
static struct platform_event s_events[EVENT_QUEUE_SIZE];
static int s_event_head, s_event_tail;

static void event_push(enum platform_event_kind kind, int code)
{
    int next;

    pthread_mutex_lock(&s_lock);
    next = (s_event_tail + 1) % EVENT_QUEUE_SIZE;
    if (next != s_event_head) {
        s_events[s_event_tail].kind = kind;
        s_events[s_event_tail].code = code;
        s_events[s_event_tail].x = 0;
        s_events[s_event_tail].y = 0;
        s_event_tail = next;
    }
    pthread_mutex_unlock(&s_lock);
}

static void pad_emit(int down, int scancode)
{
    event_push(down ? PLATFORM_EVENT_KEY_DOWN : PLATFORM_EVENT_KEY_UP, scancode);
}

/* ---- keyboard ------------------------------------------------------------ */

/* HID usage (UIKeyboardHIDUsage) -> PC set 1 scancode. */
static int hid_scancode(long usage)
{
    static const u8 letters[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
    };

    if (usage >= 0x04 && usage <= 0x1d) return letters[usage - 0x04];
    if (usage >= 0x1e && usage <= 0x26) return 0x02 + (int)(usage - 0x1e);  /* 1..9 */
    switch (usage) {
    case 0x27: return 0x0b;     /* 0 */
    case 0x28: return 0x1c;     /* Return */
    case 0x29: return 0x01;     /* Escape */
    case 0x2a: return 0x0e;     /* Backspace */
    case 0x2b: return 0x0f;     /* Tab */
    case 0x2c: return 0x39;     /* Space */
    case 0x2d: return 0x0c;     /* - */
    case 0x33: return 0x27;     /* ; */
    case 0x4f: return 0xcd;     /* Right */
    case 0x50: return 0xcb;     /* Left */
    case 0x51: return 0xd0;     /* Down */
    case 0x52: return 0xc8;     /* Up */
    case 0x58: return 0x9c;     /* keypad Enter */
    case 0xe0: return 0x1d;     /* left Control */
    case 0xe1: return 0x2a;     /* left Shift */
    case 0xe4: return 0x9d;     /* right Control */
    case 0xe5: return 0x36;     /* right Shift */
    default: return 0;
    }
}

/* ---- the game view ------------------------------------------------------- */

@interface AitdPadButton : NSObject
@property (nonatomic) CGRect frame;
@property (nonatomic) unsigned bit;
@property (nonatomic, copy) NSString *label;
@end

@implementation AitdPadButton
@end

@interface AitdGameView : UIView
@end

@implementation AitdGameView {
    NSArray<AitdPadButton *> *_buttons;
    CGRect _stick;
    UITouch *_stickTouch;
    NSMutableDictionary<NSValue *, NSNumber *> *_held;   /* touch -> bit */
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.blackColor;
        _held = [NSMutableDictionary dictionary];
        s_layer = [CALayer layer];
        s_layer.magnificationFilter = kCAFilterNearest;
        [self.layer addSublayer:s_layer];
    }
    return self;
}

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

static AitdPadButton *pad_button(CGRect frame, unsigned bit, NSString *label)
{
    AitdPadButton *b = [AitdPadButton new];

    b.frame = frame;
    b.bit = bit;
    b.label = label;
    return b;
}

/* The picture fills the height 4:3; the pad sits on the bars or over the
 * picture's lower corners. */
- (void)layoutSubviews
{
    [super layoutSubviews];
    CGSize size = self.bounds.size;
    CGFloat h = size.height, w = h * 4.0 / 3.0;
    CGFloat unit = MIN(size.width, size.height) / 7.0;
    CGFloat right = size.width - unit * 0.3;
    CGFloat bottom = size.height - unit * 0.3;

    if (w > size.width) {
        w = size.width;
        h = w * 3.0 / 4.0;
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    s_layer.frame = CGRectMake((size.width - w) / 2, (size.height - h) / 2, w, h);
    [CATransaction commit];

    _stick = CGRectMake(unit * 0.3, bottom - unit * 2.4, unit * 2.4, unit * 2.4);
    _buttons = @[
        pad_button(CGRectMake(right - unit * 1.0, bottom - unit * 1.7, unit, unit), PAD_A, @"A"),
        pad_button(CGRectMake(right - unit * 2.1, bottom - unit * 1.0, unit, unit), PAD_X, @"X"),
        pad_button(CGRectMake(right - unit * 1.0, bottom - unit * 2.8, unit, unit), PAD_B, @"B"),
        pad_button(CGRectMake(right - unit * 2.1, bottom - unit * 2.1, unit, unit), PAD_Y, @"Y"),
        pad_button(CGRectMake(unit * 0.3, unit * 0.3, unit * 1.2, unit * 0.6), PAD_L, @"L"),
        pad_button(CGRectMake(right - unit * 1.2, unit * 0.3, unit * 1.2, unit * 0.6), PAD_R, @"R"),
        pad_button(CGRectMake(size.width / 2 + unit * 0.2, bottom - unit * 0.6, unit * 1.2, unit * 0.5),
                   PAD_START, @"Menu"),
        pad_button(CGRectMake(size.width / 2 - unit * 1.4, bottom - unit * 0.6, unit * 1.2, unit * 0.5),
                   PAD_SELECT, @"Pause"),
    ];
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    NSDictionary *font = @{ NSFontAttributeName: [UIFont boldSystemFontOfSize:14],
                            NSForegroundColorAttributeName: [UIColor colorWithWhite:1 alpha:0.6] };

    CGContextSetStrokeColorWithColor(ctx, [UIColor colorWithWhite:1 alpha:0.35].CGColor);
    CGContextSetLineWidth(ctx, 2);
    CGContextStrokeEllipseInRect(ctx, _stick);
    for (AitdPadButton *b in _buttons) {
        CGSize text = [b.label sizeWithAttributes:font];

        CGContextStrokeEllipseInRect(ctx, b.frame);
        [b.label drawAtPoint:CGPointMake(CGRectGetMidX(b.frame) - text.width / 2,
                                         CGRectGetMidY(b.frame) - text.height / 2)
              withAttributes:font];
    }
}

- (void)updateTouches:(NSSet<UITouch *> *)touches ended:(BOOL)ended
{
    for (UITouch *touch in touches) {
        CGPoint p = [touch locationInView:self];
        NSValue *key = [NSValue valueWithNonretainedObject:touch];

        if (touch == _stickTouch || (!ended && _stickTouch == nil
                                     && CGRectContainsPoint(CGRectInset(_stick, -40, -40), p))) {
            if (ended) {
                _stickTouch = nil;
                s_touch_x = s_touch_y = 0;
            } else {
                CGFloat r = _stick.size.width / 2;
                CGFloat dx = (p.x - CGRectGetMidX(_stick)) / r, dy = (p.y - CGRectGetMidY(_stick)) / r;

                _stickTouch = touch;
                s_touch_x = (int)(MAX(-1.0, MIN(1.0, dx)) * 32767);
                s_touch_y = (int)(MAX(-1.0, MIN(1.0, dy)) * 32767);
            }
            continue;
        }
        [_held removeObjectForKey:key];
        if (!ended) {
            for (AitdPadButton *b in _buttons) {
                if (CGRectContainsPoint(CGRectInset(b.frame, -12, -12), p)) {
                    _held[key] = @(b.bit);
                }
            }
        }
    }
    unsigned mask = 0;
    for (NSNumber *bit in _held.allValues) {
        mask |= bit.unsignedIntValue;
    }
    s_touch_buttons = mask;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self updateTouches:touches ended:NO]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self updateTouches:touches ended:NO]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self updateTouches:touches ended:YES]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self updateTouches:touches ended:YES]; }

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    BOOL handled = NO;

    for (UIPress *press in presses) {
        int code = hid_scancode((long)press.key.keyCode);
        if (code != 0) {
            event_push(PLATFORM_EVENT_KEY_DOWN, code);
            handled = YES;
        }
    }
    if (!handled) {
        [super pressesBegan:presses withEvent:event];
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    for (UIPress *press in presses) {
        int code = hid_scancode((long)press.key.keyCode);
        if (code != 0) {
            event_push(PLATFORM_EVENT_KEY_UP, code);
        }
    }
}

@end

/* ---- game picker --------------------------------------------------------- */

static NSString *documents_dir(void)
{
    return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
}

/* Folders that hold a game: one of the files only some game ships. */
static NSArray<NSString *> *installed_games(void)
{
    static NSArray *markers;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSMutableArray *games = [NSMutableArray array];

    markers = @[ @"LISTBOD2.PAK", @"PERE.PAK", @"MER.PAK", @"AN1.PAK", @"PURSUIT.PAK", @"ITD_RESS.PAK" ];
    for (NSString *name in [[fm contentsOfDirectoryAtPath:documents_dir() error:nil]
                               sortedArrayUsingSelector:@selector(compare:)]) {
        NSString *dir = [documents_dir() stringByAppendingPathComponent:name];
        NSArray *files = [fm contentsOfDirectoryAtPath:dir error:nil];

        for (NSString *file in files) {
            if ([markers containsObject:file.uppercaseString]) {
                [games addObject:name];
                break;
            }
        }
    }
    return games;
}

static void *engine_thread(void *arg)
{
    char *argv[] = { "aitd", NULL };

    (void)arg;
    aitd_main(1, argv);
    exit(0);
    return NULL;
}

static void start_engine(NSString *game)
{
    pthread_attr_t attr;
    pthread_t thread;

    snprintf(s_data_dir, sizeof(s_data_dir), "%s",
             [documents_dir() stringByAppendingPathComponent:game].fileSystemRepresentation);
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_create(&thread, &attr, engine_thread, NULL);
}

@interface AitdViewController : UIViewController
@end

@implementation AitdViewController

- (void)loadView
{
    self.view = [[AitdGameView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    [self.view becomeFirstResponder];

    NSArray *games = installed_games();
    NSString *wanted = [[NSProcessInfo processInfo].environment objectForKey:@"AITD_GAME_FOLDER"];

    if (wanted != nil && [games containsObject:wanted]) {
        start_engine(wanted);
    } else if (games.count == 1) {
        start_engine(games.firstObject);
    } else {
        UIAlertController *pick = [UIAlertController
            alertControllerWithTitle:games.count ? @"Choose a game" : @"No game found"
                             message:games.count ? nil
                                     : @"Copy the folder of an installed game into this app's folder "
                                        "in the Files app, then open the app again."
                      preferredStyle:UIAlertControllerStyleAlert];
        for (NSString *game in games) {
            [pick addAction:[UIAlertAction actionWithTitle:game style:UIAlertActionStyleDefault
                                                   handler:^(UIAlertAction *a) { start_engine(game); }]];
        }
        [self presentViewController:pick animated:YES completion:nil];
    }
}

@end

@interface AitdAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation AitdAppDelegate

- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options
{
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [AitdViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}

@end

int main(int argc, char *argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AitdAppDelegate class]));
    }
}

/* ---- video --------------------------------------------------------------- */

int platform_video_init(const char *title, int width, int height)
{
    (void)title;
    s_width = width;
    s_height = height;
    s_pixels = calloc((size_t)width * height, 1);
    for (int i = 0; i < 256; i++) {
        s_palette[i * 3] = s_palette[i * 3 + 1] = s_palette[i * 3 + 2] = (u8)i;
    }
    mach_timebase_info(&s_timebase);
    s_start = mach_absolute_time();
    return 1;
}

void platform_video_shutdown(void)
{
}

void platform_video_set_palette(const u8 *palette)
{
    pthread_mutex_lock(&s_lock);
    memcpy(s_palette, palette, sizeof(s_palette));
    if (s_colorspace != NULL) {
        CGColorSpaceRelease(s_colorspace);
        s_colorspace = NULL;
    }
    pthread_mutex_unlock(&s_lock);
}

void platform_video_present(const u8 *indexed_pixels, int width, int height)
{
    CGImageRef image;
    CGDataProviderRef provider;
    CFDataRef data;

    if (width != s_width || height != s_height) {
        return;
    }
    pthread_mutex_lock(&s_lock);
    memcpy(s_pixels, indexed_pixels, (size_t)width * height);
    if (s_colorspace == NULL) {
        CGColorSpaceRef base = CGColorSpaceCreateDeviceRGB();
        s_colorspace = CGColorSpaceCreateIndexed(base, 255, s_palette);
        CGColorSpaceRelease(base);
    }
    data = CFDataCreate(NULL, indexed_pixels, (CFIndex)width * height);
    provider = CGDataProviderCreateWithCFData(data);
    image = CGImageCreate((size_t)width, (size_t)height, 8, 8, (size_t)width, s_colorspace,
                          kCGBitmapByteOrderDefault, provider, NULL, false, kCGRenderingIntentDefault);
    pthread_mutex_unlock(&s_lock);
    CGDataProviderRelease(provider);
    CFRelease(data);

    dispatch_async(dispatch_get_main_queue(), ^{
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        s_layer.contents = (__bridge id)image;
        [CATransaction commit];
        CGImageRelease(image);
    });
}

void platform_set_icon(const char *resource_name)
{
    (void)resource_name;
}

void platform_set_cursor(int cursor_id)
{
    (void)cursor_id;
}

static void controller_poll(void)
{
    GCExtendedGamepad *pad = GCController.controllers.firstObject.extendedGamepad;
    unsigned mask = 0;

    if (pad == nil) {
        s_controller_buttons = 0;
        s_controller_x = s_controller_y = 0;
        return;
    }
    if (pad.dpad.up.pressed) mask |= PAD_UP;
    if (pad.dpad.down.pressed) mask |= PAD_DOWN;
    if (pad.dpad.left.pressed) mask |= PAD_LEFT;
    if (pad.dpad.right.pressed) mask |= PAD_RIGHT;
    if (pad.buttonA.pressed) mask |= PAD_A;
    if (pad.buttonB.pressed) mask |= PAD_B;
    if (pad.buttonX.pressed) mask |= PAD_X;
    if (pad.buttonY.pressed) mask |= PAD_Y;
    if (pad.leftShoulder.pressed) mask |= PAD_L;
    if (pad.rightShoulder.pressed) mask |= PAD_R;
    if (pad.buttonMenu.pressed) mask |= PAD_START;
    if (pad.buttonOptions.pressed) mask |= PAD_SELECT;
    s_controller_buttons = mask;
    s_controller_x = (int)(pad.leftThumbstick.xAxis.value * 32767);
    s_controller_y = (int)(-pad.leftThumbstick.yAxis.value * 32767);
}

int platform_poll_event(struct platform_event *out)
{
    int x, y, got = 0;

    controller_poll();
    x = s_touch_x != 0 ? s_touch_x : s_controller_x;
    y = s_touch_y != 0 ? s_touch_y : s_controller_y;
    pad_update(s_touch_buttons | s_controller_buttons, x, y, pad_emit);

    pthread_mutex_lock(&s_lock);
    if (s_event_head != s_event_tail) {
        *out = s_events[s_event_head];
        s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
        got = 1;
    }
    pthread_mutex_unlock(&s_lock);
    return got;
}

u32 platform_get_ticks(void)
{
    return (u32)((mach_absolute_time() - s_start) * s_timebase.numer / s_timebase.denom / 1000000ull);
}

void platform_delay(u32 milliseconds)
{
    usleep(milliseconds * 1000u);
}

/* ---- audio --------------------------------------------------------------- */

#define AUDIO_BUFFER_COUNT 3
#define AUDIO_FRAMES 1024

static AudioQueueRef s_queue;

static void audio_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    (void)user;
    mixer_render(buffer->mAudioData, AUDIO_FRAMES);
    buffer->mAudioDataByteSize = AUDIO_FRAMES * sizeof(short);
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

int backend_audio_open(void)
{
    AudioStreamBasicDescription format;

    memset(&format, 0, sizeof(format));
    format.mSampleRate = MIXER_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsPacked | kLinearPCMFormatFlagIsSignedInteger;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;
    format.mBytesPerFrame = 2;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = 2;
    if (AudioQueueNewOutput(&format, audio_callback, NULL, NULL, NULL, 0, &s_queue) != noErr) {
        return 0;
    }
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        AudioQueueBufferRef buffer;

        if (AudioQueueAllocateBuffer(s_queue, AUDIO_FRAMES * sizeof(short), &buffer) != noErr) {
            return 0;
        }
        memset(buffer->mAudioData, 0, AUDIO_FRAMES * sizeof(short));
        buffer->mAudioDataByteSize = AUDIO_FRAMES * sizeof(short);
        AudioQueueEnqueueBuffer(s_queue, buffer, 0, NULL);
    }
    return AudioQueueStart(s_queue, NULL) == noErr;
}

void backend_audio_close(void)
{
    if (s_queue != NULL) {
        AudioQueueStop(s_queue, true);
        AudioQueueDispose(s_queue, true);
        s_queue = NULL;
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

/* ---- files and dialogs --------------------------------------------------- */

void backend_default_data_dir(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", s_data_dir[0] != '\0' ? s_data_dir
                                  : documents_dir().fileSystemRepresentation);
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
    int i;

    if (s_pixels == NULL || (f = fopen(path, "wb")) == NULL) {
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_width, s_height);
    for (i = 0; i < s_width * s_height; i++) {
        fwrite(s_palette + s_pixels[i] * 3, 1, 3, f);
    }
    fclose(f);
    return 1;
}
