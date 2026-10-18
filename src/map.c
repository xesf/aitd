/*
 * map.c - the map shown with Tab.
 *
 *   scene_tab_action  0x4019ff
 *   map_marker_at     0x401724  where the player is, by floor and room
 *   map_marker        0x4013de  a blinking arrow for up to 600 ticks
 *
 * ITD_RESS picture 48 is the map; there is no marker for places off it.
 */

#include <string.h>

#include "aitd.h"
#include "anim.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "raster.h"
#include "savegame.h"
#include "subsystems.h"
#include "world.h"

enum {
    MARKER_TIME = 600,
    MARKER_BLINK = 30
};

static int key_down(void)
{
    return (g_last_scancode & 0xff) || g_key_confirm;
}

static void wait_blink(struct game_timer *timer)
{
    while (timer_elapsed(timer) < MARKER_BLINK && !key_down()) {
        input_poll();
    }
}

/* 0x4013de - an arrow pointing down at (x, y). */
static void map_marker(s16 x, s16 y)
{
    struct raster_image map = { (u16)g_screen_width, (u16)g_screen_height, g_back_buffer };
    struct game_timer total;
    struct game_timer blink;

    raster_blit(0, 0, &map);
    screen_present();
    timer_reset(&total);
    while (timer_elapsed(&total) < MARKER_TIME && !key_down()) {
        timer_reset(&blink);
        /* The head. */
        raster_line(x, y, (s16)(x - 3), (s16)(y - 4), 32);
        raster_line(x, y, (s16)(x - 2), (s16)(y - 4), 33);
        raster_line(x, y, (s16)(x - 1), (s16)(y - 4), 34);
        raster_line(x, y, x, (s16)(y - 12), 35);
        raster_line(x, y, (s16)(x + 1), (s16)(y - 4), 34);
        raster_line(x, y, (s16)(x + 2), (s16)(y - 4), 33);
        raster_line(x, y, (s16)(x + 3), (s16)(y - 4), 32);
        /* The feathers. */
        raster_line(x, (s16)(y - 11), (s16)(x - 3), (s16)(y - 14), 37);
        raster_line(x, (s16)(y - 9), (s16)(x - 3), (s16)(y - 12), 36);
        raster_line(x, (s16)(y - 7), (s16)(x - 3), (s16)(y - 10), 35);
        raster_line(x, (s16)(y - 11), (s16)(x + 3), (s16)(y - 14), 37);
        raster_line(x, (s16)(y - 9), (s16)(x + 3), (s16)(y - 12), 36);
        raster_line(x, (s16)(y - 7), (s16)(x + 3), (s16)(y - 10), 35);
        screen_present();   /* 0x41c92a, the 7 x 15 box only */
        wait_blink(&blink);

        timer_reset(&blink);
        raster_copy_rect((s16)(x - 3), (s16)(y - 14), (s16)(x + 3), y, g_back_buffer); /* 0x41fa30 */
        screen_present();
        wait_blink(&blink);
    }
}

/* 0x401724 */
static void map_marker_at(s16 floor, s16 room)
{
    switch (floor) {
    case 0:
        if (room == 0 || room == 15) map_marker(207, 134);
        else if (room == 1) map_marker(187, 139);
        else if (room == 12) map_marker(183, 189);
        else if (room == 14) map_marker(211, 113);
        break;
    case 1:
    case 13:
        map_marker(193, 75);
        break;
    case 2:
        if (room == 0) map_marker(172, 162);
        else if (room <= 2) map_marker(220, 174);
        break;
    case 3:
        if (room <= 1) map_marker(245, 61);
        else if (room <= 3) map_marker(204, 61);
        else if (room == 5) map_marker(123, 65);
        else if (room == 4 || room == 6 || room == 7 || room == 9) map_marker(156, 70);
        break;
    case 4:
        map_marker(93, 70);
        break;
    case 5:
        map_marker(90, 45);
        break;
    case 6:
    case 7:
    case 8:
        map_marker(98, 122);
        break;
    case 9:
        if ((room >= 2 && room <= 3) || (room >= 5 && room <= 7)) map_marker(212, 94);
        else if (room >= 8 && room <= 10) map_marker(233, 105);
        break;
    case 10:
        map_marker(173, 100);
        break;
    case 11:
        map_marker(117, 31);
        break;
    case 12:
        if (room == 11) map_marker(241, 142);
        else if (room == 13) map_marker(279, 138);
        break;
    default:
        break;
    }
}

/* 0x4019ff */
void scene_tab_action(void)
{
    u8 palette[0x300];
    s16 floor = g_cur_floor;
    s16 room = g_cur_room;

    ticks_freeze();
    g_message_shown = 1;
    while (g_last_scancode & 0xff) {
        input_poll();
    }
    memcpy(palette, g_tab_picture + 64000, sizeof(palette));
    memcpy(g_back_buffer, g_tab_picture, 64000);           /* 0x4201d2 */
    memcpy(g_saved_palette, g_palette_buffer, 0x300);
    palette_set_range(palette, 0, 256);
    map_marker_at(floor, room);
    g_init_view = 1;
    palette_fade_out(0x10, 0);
    screen_clear();
    screen_present();
    ticks_unfreeze();
}
