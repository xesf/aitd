/*
 * menu.c - main_menu_run (0x408f34).
 *
 * Four entries, selectable by mouse or by up/down keys, confirmed with Enter.
 * The loop also runs an idle timer: when nothing is touched for 900 ticks it
 * returns -1, which game_main dispatches back into the intro. That is the
 * attract mode, and it is what MENU_RESULT_INTRO means.
 *
 * Entry indices line up with the dispatch at 0x4079c5, so the values are the
 * enum in aitd.h: 0 new game, 1 load, 2 continue, 3 quit.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "raster.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "subsystems.h"

/* Hit box and row pitch, all read off the compares at 0x408fd1..0x409005. */
#define MENU_HIT_LEFT 100      /* 0x64, exclusive */
#define MENU_HIT_RIGHT 220     /* 0xdc, exclusive */
#define MENU_HIT_TOP 79        /* 0x4f, exclusive */
#define MENU_HIT_BOTTOM 135    /* 0x87, exclusive */
#define MENU_ROW_HEIGHT 14     /* 0x0e */
#define MENU_ENTRY_COUNT 4     /* wrap limits at 0x409045 and 0x409088 */

/* Idle timeout before the menu gives up and replays the intro (0x408f95). */
#define MENU_IDLE_TIMEOUT 0x384

/* Confirm key (0x4090b5). */
#define SCANCODE_ENTER 0x1c

s16 g_mouse_buttons;      /* 0x445c7a */
s16 g_mouse_x;            /* 0x445c7c */
s16 g_mouse_y;            /* 0x445c7e */
s32 g_key_dir_flags;      /* 0x444bc8, bit 0 = up, bit 1 = down */
s32 g_key_confirm;        /* 0x444bc4 */
s16 g_last_scancode;      /* 0x43516a */

/* Spins until the mouse button is released (0x40901e, 0x4090e6). */
static void wait_mouse_release(void)
{
    while (g_mouse_buttons != 0) {
        input_poll();  /* 0x402c67 */
    }
}

/* Spins until the direction keys are released (0x409062, 0x4090a5). */
static void wait_keys_release(void)
{
    while (g_key_dir_flags != 0) {
        input_poll();
    }
}

static int confirm_pressed(void)
{
    /* 0x4090b5: Enter by scancode, or the separate confirm input. */
    return (g_last_scancode & 0xff) == SCANCODE_ENTER || g_key_confirm != 0;
}

/*
 * 0x408f34
 */
int main_menu_run(void)
{
    struct game_timer timer;
    s16 selection = 0;   /* var_4 */
    s16 result = -1;     /* var_8 */

    {
        int other = menu_style_run();   /* titles with their own menu */
        if (other != -2) {
            return other;
        }
    }

    screen_clear();              /* 0x408f5a */
    menu_draw_background();             /* 0x408f5f */
    menu_draw_entry(0);                 /* 0x408f66 */
    screen_present();                      /* 0x408f6b */

    g_fade_state = 1;                    /* 0x408f70 */
    palette_fade(0x10, 0);              /* 0x408f80 */

    timer_reset(&timer);                /* 0x408f88 */

    for (;;) {
        /* 0x408f8d: the loop ends on idle timeout or on a confirmed pick. */
        if (timer_elapsed(&timer) >= MENU_IDLE_TIMEOUT || result != -1) {
            break;
        }

        input_poll();                   /* 0x408faa */

        /* 0x408faf: mouse pick. */
        if (g_mouse_buttons != 0) {
            int x = g_mouse_x;
            int y = g_mouse_y;

            if (x > MENU_HIT_LEFT && x < MENU_HIT_RIGHT
                && y > MENU_HIT_TOP && y < MENU_HIT_BOTTOM) {
                selection = (s16)((y - MENU_HIT_TOP) / MENU_ROW_HEIGHT);
                result = selection;     /* 0x40900d: a click confirms */

                menu_draw_entry(selection);
                menu_entry_feedback();
                wait_mouse_release();
            }
        }

        /* 0x40902f: up, wrapping past the top to the last entry. */
        if (g_key_dir_flags & DIR_UP) {
            if (--selection < 0) {
                selection = MENU_ENTRY_COUNT - 1;
            }

            menu_draw_entry(selection);
            menu_entry_feedback();
            timer_reset(&timer);        /* 0x40905a: activity resets idle */
            wait_keys_release();
        }

        /* 0x409072: down, wrapping past the last entry to the first. */
        if (g_key_dir_flags & DIR_DOWN) {
            if (++selection > MENU_ENTRY_COUNT - 1) {
                selection = 0;
            }

            menu_draw_entry(selection);
            menu_entry_feedback();
            timer_reset(&timer);
            wait_keys_release();
        }

        if (confirm_pressed()) {
            result = selection;         /* 0x4090c7 */
        }
    }

    /* 0x4090d2: drain the input that triggered the exit before returning. */
    while (confirm_pressed() || g_mouse_buttons != 0) {
        input_poll();
    }

    return result;
}
