/*
 * input.c - keyboard, mouse and timers.
 *
 * The window procedure (0x41f220) turns a Windows virtual key into a PC set 1
 * scancode through the table at 0x43525c and stores it in g_last_scancode.
 * Every key event then runs the binding table at 0x446084 (0x422e2b): a
 * binding sets or clears a mask in a global while its key is down. The
 * game's bindings come from the static list at 0x4350e5, registered by
 * engine_init (0x40730f):
 *
 *   up    0xc8 -> g_key_dir_flags |= 1     space 0x39 -> g_key_confirm |= 1
 *   down  0xd0 -> g_key_dir_flags |= 2     ctrl  0x1d -> g_key_confirm |= 2
 *   left  0xcb -> g_key_dir_flags |= 4     rctrl 0x9d -> g_key_confirm |= 2
 *   right 0xcd -> g_key_dir_flags |= 8
 *
 * Here the macOS virtual key goes straight to the scancode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "menu.h"
#include "platform.h"
#include "subsystems.h"


struct key_binding {
    u8 scancode;
    s32 mask;
    s32 *target;
};

/* 0x4350e5 */
static const struct key_binding s_bindings[] = {
    { 0xc8, 1, &g_key_dir_flags },
    { 0xd0, 2, &g_key_dir_flags },
    { 0xcb, 4, &g_key_dir_flags },
    { 0xcd, 8, &g_key_dir_flags },
    { 0x39, 1, &g_key_confirm },
    { 0x9d, 2, &g_key_confirm },
    { 0x1d, 2, &g_key_confirm },
};

/* Buffer that every poll pushes to the window. The menu draws into the back
 * buffer; the game composes into g_screen. */
u8 *g_present_buffer;

/* 0x422e2b, binding type 1. */
static void run_bindings(u8 scancode, int down)
{
    size_t i;

    for (i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        if (s_bindings[i].scancode == scancode) {
            if (down) {
                *s_bindings[i].target |= s_bindings[i].mask;
            } else {
                *s_bindings[i].target &= ~s_bindings[i].mask;
            }
        }
    }
}

/*
 * Scripted input for unattended runs: AITD_INPUT="from-to:keys,..." where
 * keys are any of u d l r (arrows), s (space), c (ctrl), e (enter), x (esc),
 * t (tab) and the digits 0..9.
 * Counts are input_poll calls: one per frame in the scene loop.
 */
static int s_poll_number;

static void harness_input(void)
{
    static const char *script;
    static int parsed;
    const char *p;
    int dir = -1, confirm = -1, scancode = -1;

    if (!parsed) {
        script = getenv("AITD_INPUT");
        parsed = 1;
    }
    if (script == NULL) {
        return;
    }

    for (p = script; *p != '\0';) {
        int from = 0, to = 0;
        const char *keys;

        from = (int)strtol(p, (char **)&p, 10);
        if (*p == '-') {
            p++;
            to = (int)strtol(p, (char **)&p, 10);
        } else {
            to = from;
        }
        if (*p == ':') {
            p++;
        }
        keys = p;
        while (*p != '\0' && *p != ',') {
            p++;
        }
        if (s_poll_number >= from && s_poll_number <= to) {
            const char *k;

            dir = 0;
            confirm = 0;
            scancode = 0;
            for (k = keys; k < p; k++) {
                switch (*k) {
                case 'u': dir |= 1; scancode = 0xc8; break;
                case 'd': dir |= 2; scancode = 0xd0; break;
                case 'l': dir |= 4; scancode = 0xcb; break;
                case 'r': dir |= 8; scancode = 0xcd; break;
                case 's': confirm |= 1; scancode = 0x39; break;
                case 'c': confirm |= 2; scancode = 0x1d; break;
                case 'e': scancode = 0x1c; break;
                case 'x': scancode = 0x01; break;
                case 't': scancode = 0x0f; break;
                case '1': case '2': case '3': case '4': case '5':
                case '6': case '7': case '8': case '9':
                    scancode = 0x02 + (*k - '1'); break;
                case '0': scancode = 0x0b; break;
                default: break;
                }
            }
        }
        if (*p == ',') {
            p++;
        }
    }

    if (dir >= 0) {
        g_key_dir_flags = dir;
        g_key_confirm = confirm;
        g_last_scancode = (s16)scancode;
    } else {
        g_key_dir_flags = 0;
        g_key_confirm = 0;
        g_last_scancode = 0;
    }
}

/* The buffer the poll puts on the window: the game screen while the world
 * runs, the back buffer in the menus. */
u8 *presented_buffer(void)
{
    return g_present_buffer != NULL ? g_present_buffer : g_blit_source;
}

/* AITD_TIMEOUT=<seconds> ends an unattended run even inside a modal loop
 * (a document, a menu) that AITD_FRAMES does not reach. */
static void harness_timeout(void)
{
    static time_t deadline = -1;

    if (deadline == -1) {
        const char *env = getenv("AITD_TIMEOUT");

        deadline = (env != NULL && atoi(env) > 0) ? time(NULL) + atoi(env) : 0;
    }
    if (deadline != 0 && time(NULL) >= deadline) {
        fprintf(stderr, "harness: AITD_TIMEOUT reached\n");
        engine_shutdown();
        exit(0);
    }
}

/* 0x402c67 -> 0x41f0cf -> 0x41f220 */
void input_poll(void)
{
    struct platform_event event;

    harness_timeout();

    while (platform_poll_event(&event)) {
        u8 sc;

        switch (event.kind) {
        case PLATFORM_EVENT_QUIT:
            /* 0x41f107: WM_QUIT leaves the process. */
            engine_shutdown();
            exit(0);
            break;

        case PLATFORM_EVENT_KEY_DOWN:
            sc = (u8)event.code;    /* the backend reports PC set 1 scancodes */
            if (sc == 0) {
                break;
            }
            g_last_scancode = sc;
            run_bindings(sc, 1);
            break;

        case PLATFORM_EVENT_KEY_UP: {
            /* 0x41f585: run the release with the key's own code, then put
             * the previous key back unless it was this one. */
            u8 saved = (u8)g_last_scancode;

            sc = (u8)event.code;    /* the backend reports PC set 1 scancodes */
            if (sc == 0) {
                break;
            }
            g_last_scancode = sc;
            run_bindings(sc, 0);
            g_last_scancode = saved;
            if (sc == saved) {
                g_last_scancode = 0;
            }
            break;
        }

        case PLATFORM_EVENT_MOUSE_MOVE:
            g_mouse_x = (s16)event.x;
            g_mouse_y = (s16)event.y;
            break;

        case PLATFORM_EVENT_MOUSE_BUTTON:
            g_mouse_x = (s16)event.x;
            g_mouse_y = (s16)event.y;
            g_mouse_buttons = (s16)(g_mouse_buttons ? 0 : 1);
            break;

        default:
            break;
        }
    }

    s_poll_number++;
    harness_input();
    {
        /* AITD_DUMP_EVERY=n with AITD_DUMP_FRAME=prefix: snapshot every
         * n polls, whatever screen is up. */
        static int every = -1;
        static const char *prefix;
        if (every == -1) {
            const char *e = getenv("AITD_DUMP_EVERY");
            every = e ? atoi(e) : 0;
            prefix = getenv("AITD_DUMP_FRAME");
        }
        const u8 *shown = g_present_buffer != NULL ? g_present_buffer : g_blit_source;
        if (every > 0 && prefix != NULL && s_poll_number % every == 0 && shown != NULL) {
            char name[1024];
            FILE *fp;
            int i;

            snprintf(name, sizeof(name), "%s_%05d.ppm", prefix, s_poll_number);
            if (getenv("AITD_TRACE") != NULL) {
                fprintf(stderr, "dump %05d\n", s_poll_number);
            }
            fp = fopen(name, "wb");
            if (fp != NULL) {
                fprintf(fp, "P6\n%d %d\n255\n", (int)g_screen_width, (int)g_screen_height);
                for (i = 0; i < (int)SCREEN_PIXELS; i++) {
                    fwrite(g_palette_buffer + shown[i] * 3, 1, 3, fp);
                }
                fclose(fp);
            }
        }
    }

    /* Something has to reach the screen from inside the poll: the menu and
     * most of the in-game dialogs spin on these globals. */
    platform_video_present(g_present_buffer != NULL ? g_present_buffer : g_blit_source,
                           g_blit_width, g_blit_height);
    platform_delay(1);
}

/* ---- timers ------------------------------------------------------------ */

/* 0x402913 */
void timer_reset(struct game_timer *timer)
{
    timer_reset_u32(&timer->start);
}

void timer_reset_u32(u32 *timer)
{
    ticks_update();
    *timer = g_ticks;
}

/* 0x402945 - wraps the way the original does. */
u32 timer_elapsed(struct game_timer *timer)
{
    return timer_elapsed_u32(&timer->start);
}

u32 timer_elapsed_u32(const u32 *timer)
{
    u32 now;

    ticks_update();
    now = g_ticks;
    if (now >= *timer) {
        return now - *timer;
    }
    return now + (0xffffffffu - *timer);
}

/* 0x41d019 - hundredths of a second since start. */
u32 timer_get_ms(void)
{
    return platform_get_ticks() / 10u;
}
