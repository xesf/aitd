/*
 * editor.c - the runtime editor.
 *
 * F10 opens it from any screen that polls the input: the main menu, the
 * game, a menu in the game. It takes over the window until F10 or Escape,
 * with the game's clock stopped, then puts back what it found: the picture
 * on screen, the palette, the raster target and clip, and the 3D camera,
 * so the game carries on where it was.
 *
 * The viewers (ed_*.c) read the title's data directly; none of them
 * changes it yet. Tab or the number keys switch between them.
 */

#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "anim.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "r3d.h"
#include "raster.h"
#include "subsystems.h"
#include "editor/editor.h"
#include "editor/ed_ui.h"

enum {
    REPEAT_DELAY = 30,      /* 1/100 s before a held key repeats */
    REPEAT_EVERY = 4
};

#define s_viewers g_ed_viewers
#define VIEWER_COUNT g_ed_viewer_count

static int s_active;
static int s_current;

int editor_active(void)
{
    return s_active;
}

void editor_poll(void)
{
    if (!s_active && (g_last_scancode & 0xff) == SC_EDITOR) {
        editor_run();
    }
}

static void wait_released(void)
{
    while ((g_last_scancode & 0xff) != 0 || g_key_dir_flags != 0) {
        input_poll();
    }
}

/* One key press a call, held keys repeating; 0 when none. */
static int next_key(void)
{
    static int held;
    static u32 since, last;
    int sc = g_last_scancode & 0xff;
    u32 now = timer_get_ms();

    if (sc == 0) {
        held = 0;
        return 0;
    }
    if (sc != held) {
        held = sc;
        since = last = now;
        return sc;
    }
    if (now - since >= REPEAT_DELAY && now - last >= REPEAT_EVERY) {
        last = now;
        return sc;
    }
    return 0;
}

static void switch_to(int viewer)
{
    if (s_viewers[s_current]->close != NULL) {
        s_viewers[s_current]->close();
    }
    s_current = (viewer + VIEWER_COUNT) % VIEWER_COUNT;
    ed_use_palette(g_ed_game_palette);
    if (s_viewers[s_current]->open != NULL) {
        s_viewers[s_current]->open();
    }
}

void editor_run(void)
{
    u8 *target = presented_buffer();
    u8 *screen;
    u8 palette[AITD_PALETTE_BYTES];
    struct raster_state raster = g_raster;
    struct r3d_view view;

    if (s_active || target == NULL || (screen = malloc(SCREEN_PIXELS)) == NULL) {
        return;
    }
    s_active = 1;
    memcpy(screen, target, SCREEN_PIXELS);
    memcpy(palette, g_palette_buffer, sizeof(palette));
    memcpy(g_ed_game_palette, palette, sizeof(palette));
    r3d_view_save(&view);
    ticks_freeze();
    if (g_raster.width == 0) {
        raster_init(target, g_screen_width, g_screen_height);
    }
    raster_push_target(target);
    raster_reset_clip();
    wait_released();

    ed_use_palette(g_ed_game_palette);
    if (s_viewers[s_current]->open != NULL) {
        s_viewers[s_current]->open();
    }
    for (;;) {
        int sc;

        s_viewers[s_current]->draw();
        input_poll();
        sc = next_key();
        if (sc == SC_EDITOR || sc == SC_ESC) {
            break;
        } else if (sc == SC_TAB) {
            switch_to(s_current + 1);
        } else if (sc >= SC_1 && sc < SC_1 + VIEWER_COUNT) {
            switch_to(sc - SC_1);
        } else if (sc != 0) {
            s_viewers[s_current]->key(sc);
        }
        platform_delay(10);
    }
    if (s_viewers[s_current]->close != NULL) {
        s_viewers[s_current]->close();
    }
    wait_released();

    raster_pop_target();
    g_raster = raster;
    r3d_view_restore(&view);
    memcpy(target, screen, SCREEN_PIXELS);
    memcpy(g_palette_buffer, palette, sizeof(palette));
    palette_set_range(g_palette_buffer, 0, 256);
    free(screen);
    ticks_unfreeze();
    g_last_scancode = 0;
    g_key_dir_flags = 0;
    g_key_confirm = 0;
    s_active = 0;
}
