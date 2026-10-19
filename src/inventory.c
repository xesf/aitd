/*
 * inventory.c - the inventory screen (0x40f1c3).
 *
 * Left column: the carried objects, five at a time. Below it the selected
 * object turns in a little 3D view with its counter (bullets, for instance).
 * Right column: the actions the object allows, one per bit in the low 11
 * bits of its flags2 word (text ids 23..33). Picking an action sets the
 * ACTION variable to 1 << bit and runs the object's own life script.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "game.h"
#include "anim.h"
#include "draw.h"
#include "font.h"
#include "frame.h"
#include "hyb.h"
#include "life.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "picture.h"
#include "platform.h"
#include "r3d.h"
#include "raster.h"
#include "subsystems.h"
#include "world.h"

#define VISIBLE_ITEMS 5
#define MAX_ACTIONS 5

/* Positions of the three frame sprites, per character (0x434efc / 0x434f0e). */
static const s16 s_frame_x[9] = { 0, 128, 286, 0, 129, 286, 0, 110, 282 };
static const s16 s_frame_y[9] = { 100, 100, 100, 100, 100, 100, 140, 119, 141 };

/* The older titles: AITD2 lays ITD_RESS 14 + inventory type under the screen with a
 * sprite from entry 18; AITD1 and Jack draw three frames over the game.
 * Both use colour 4 text, the selection in 15 (14 once the actions have
 * the focus), framed rows on a colour 100 bar. */
static enum inventory_style s_style;
static s16 s_status[4] = { 27, 100, 159, 174 };   /* the turning object */

static const s16 s_aitd2_sprite_x[3] = { 127, 118, 124 };
static const s16 s_aitd2_sprite_y[3] = { 136, 104, 131 };

static void aitd2_sprite(void)
{
    static u8 *sprites;
    int type = g_player_type;

    if (s_style != INVENTORY_PICTURE || type < 0 || type > 2) {
        return;
    }
    if (sprites == NULL) {
        u32 size = 0;
        sprites = pak_load("ITD_RESS.PAK", 18, &size);
    }
    if (sprites != NULL) {
        raster_set_clip(0, 0, 319, 199);
        old_sprite_draw(s_aitd2_sprite_x[type], s_aitd2_sprite_y[type], type, sprites, 1);
    }
}

static void scroll_arrows(s16 first)
{
    const u8 *bank = engine_resource(ITD_RESS_UI_SPRITES);

    if (bank == NULL) {
        return;
    }
    raster_set_clip(0, 0, 319, 199);
    if (first > 0) {
        old_sprite_draw(298, 10, 10, bank, 1);
    }
    if (first + VISIBLE_ITEMS < g_inventory_count[g_inventory_index]) {
        old_sprite_draw(298, 74, 9, bank, 1);
    }
}

static s16 s_actions[MAX_ACTIONS];   /* 0x4415e8 */
static s16 s_action_count;           /* 0x4415f2 */
static u8 s_colour_table[256];

static const u8 *frame_data(void)
{
    static const int entries[3] = {
        ITD_RESS_INVENTORY_FRAME_0, ITD_RESS_INVENTORY_FRAME_1, ITD_RESS_INVENTORY_FRAME_2
    };
    int type = g_player_type;

    if (type < 0 || type > 2) {
        type = 0;
    }
    return engine_resource(entries[type]);
}

static void draw_frames(void)
{
    const u8 *data = frame_data();
    int type = g_player_type;
    int i;

    if (s_style != INVENTORY_DIMMED) {
        aitd2_sprite();
        return;
    }
    if (data == NULL || type < 0 || type > 2) {
        return;
    }
    raster_set_clip(0, 0, 319, 199);
    for (i = 0; i < 3; i++) {
        hyb_render(i, s_frame_x[type * 3 + i], s_frame_y[type * 3 + i], data);
    }
}

/* 0x40eda8 - returns the object under the cursor. */
static s16 inventory_draw_list(s16 first, s16 selected, u8 colour)
{
    s16 *list = g_inventory[g_inventory_index];
    s16 y = 28, pitch = 14, start = first;
    s16 chosen = -1;
    u8 normal = 64;
    int i;

    switch (s_style) {
    case INVENTORY_FRAMED:
        frame_draw(160, 50, 320, 100);
        y = (s16)(g_window[1] + 1);
        pitch = 16;
        normal = 4;
        break;
    case INVENTORY_PICTURE:
        raster_set_clip(27, 25, 292, 98);
        raster_rect(27, 292, 25, 98, 0);
        normal = 4;
        break;
    default:
        raster_set_clip(30, 25, 292, 98);
        raster_copy_rect(30, 25, 292, 98, g_back_buffer);
        break;
    }
    if (s_style != INVENTORY_DIMMED) {
        colour = (u8)(colour == 1 ? 15 : 14);
    }

    for (i = 0; i < VISIBLE_ITEMS && first < g_inventory_count[g_inventory_index]; i++) {
        s16 obj = list[first];
        const struct world_object *o = object_get(obj);

        if (first == selected) {
            if (s_style == INVENTORY_FRAMED) {
                if (colour == 15) {
                    raster_rect(10, 309, y, (s16)(y + 16), 100);
                }
                text_draw_centered_shadowed(160, y, o->found_name, colour, 4);
            } else {
                text_draw_centered(160, y, o->found_name, colour);
            }
            chosen = obj;
        } else {
            text_draw_centered(160, y, o->found_name, normal);
        }
        y = (s16)(y + pitch);
        first++;
    }
    if (s_style != INVENTORY_DIMMED) {
        scroll_arrows(start);
    }
    return chosen;
}

/* 0x40f053 */
static void inventory_draw_actions(s16 selected)
{
    s16 y = (s16)(139 - (s16)((s16)(s_action_count * 14) / 2));
    int i;

    if (s_style == INVENTORY_FRAMED) {
        frame_draw(240, 150, 160, 100);
        y = (s16)(150 - (s_action_count * 16) / 2);
        for (i = 0; i < s_action_count; i++, y = (s16)(y + 16)) {
            if (i == selected) {
                raster_rect(170, 309, y, (s16)(y + 16), 100);
                text_draw_centered_shadowed(240, y, s_actions[i], 15, 4);
            } else {
                text_draw_centered(240, y, s_actions[i], 4);
            }
        }
        return;
    }
    if (s_style == INVENTORY_PICTURE) {
        raster_set_clip(162, 100, 292, 174);
        raster_rect(162, 292, 100, 174, 0);
    } else {
        raster_set_clip(161, 100, 292, 174);
        raster_copy_rect(161, 100, 292, 174, g_back_buffer);
    }

    for (i = 0; i < s_action_count; i++) {
        u8 normal = s_style == INVENTORY_PICTURE ? 4 : 64;
        text_draw_centered(240, y, s_actions[i], (u8)(i == selected ? 1 : normal));
        y = (s16)(y + 14);
    }
    draw_frames();
}

/* 0x40eee4 */
static void inventory_draw_object(s16 counter)
{
    raster_set_clip(s_status[0], s_status[1], s_status[2], s_status[3]);
    if (s_style == INVENTORY_DIMMED) {
        raster_copy_rect(27, 100, 159, 174, g_back_buffer);
    } else {
        raster_rect(s_status[0], s_status[2], s_status[1], s_status[3], 0);
    }

    g_preview_angle = (s16)(g_preview_angle - 8);
    r3d_set_camera_target(0, 0, 0, 60, g_preview_angle, 0, 24000);
    r3d_set_clip_y(10000);
    r3d_draw_body(0, 0, 0, 0, 0, 0, hqr_get(g_cache_body, g_preview_body, NULL));

    if (counter != -1) {
        char text[16];

        snprintf(text, sizeof(text), "%d", g_vars[counter]);
        font_set_colour(s_colour_table, s_style == INVENTORY_DIMMED ? 64 : 4);
        font_bind(g_font_data, s_colour_table, 1);
        font_draw_string(g_raster.target, g_raster.pitch, g_raster.height,
                         (s16)(s_status[0] + 4), (s16)(s_status[1] + 4), text);
    }
    draw_frames();
    draw_present();
}

/* 0x40f1c3 */
void inventory_open(void)
{
    s_style = game_current()->inventory_style;
    if (s_style == INVENTORY_NONE) {
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "inventory: not rebuilt for %s yet\n", game_current()->name);
        }
        return;
    }
    const u8 *background;
    s16 first = 0, cursor = 0, drawn = -1, action = 0;
    s16 object = -1;
    int in_actions = 0, done = 0, use = 0, first_frame = 1;
    int repeat = 2;
    u32 repeat_timer = 0;
    int i;

    if (g_inventory_count[g_inventory_index] == 0) {
        return;
    }

    g_preview_angle = 0;

    if (s_style == INVENTORY_DIMMED) {
    /* 0x40f251: dim the game into colours 120..127 and lay the picture
     * over it; the picture's zero pixels let the game show through. */
    memcpy(g_back_buffer, g_screen, 64000);
    for (i = 0; i < 64000; i++) {
        g_back_buffer[i] = (u8)(((g_back_buffer[i] & 0x0f) >> 1) + 120);
    }
    switch (g_player_type) {
    case 1:  background = engine_resource(ITD_RESS_INVENTORY_1); break;
    case 2:  background = engine_resource(ITD_RESS_INVENTORY_2); break;
    default: background = engine_resource(ITD_RESS_INVENTORY_0); break;
    }
    if (background != NULL) {
        memcpy(g_screen, background + 4, 64000);
    }
    for (i = 0; i < 64000; i++) {
        if (g_screen[i] == 0) {
            g_screen[i] = g_back_buffer[i];
        }
    }
    memcpy(g_back_buffer, g_screen, 64000);
    s_status[0] = 27;
    s_status[1] = 100;
    s_status[2] = 159;
    s_status[3] = 174;
    } else if (s_style == INVENTORY_PICTURE) {
        int type = g_player_type;

        picture_load(14 + (type >= 0 && type <= 2 ? type : 0), g_screen, NULL);
        memcpy(g_back_buffer, g_screen, 64000);
        s_status[0] = 27;
        s_status[1] = 100;
        s_status[2] = 159;
        s_status[3] = 174;
    } else {
        raster_push_target(g_screen);
        frame_draw(80, 150, 160, 100);
        raster_pop_target();
        memcpy(s_status, g_window, sizeof(s_status));
    }

    ticks_freeze();
    shaking_save();
    if (g_black_screen) {
        palette_blackout();
    }
    r3d_set_projection((s16)((s_status[2] - s_status[0]) / 2 + s_status[0]),
                       (s16)((s_status[3] - s_status[1]) / 2 + s_status[1]), 128, 400, 390);

    while (!done) {
        input_poll();
        g_scene_scancode = (s16)(g_last_scancode & 0xff);
        g_scene_dir_flags = (s16)g_key_dir_flags;
        g_scene_confirm = (s16)g_key_confirm;

        if (!(g_last_scancode & 0xff) && !g_scene_dir_flags && !g_scene_confirm) {
            repeat = 0;
        }
        if (g_scene_scancode == SC_ESC) {
            use = 0;
            done = 1;
        }

        if (in_actions) {
            if (repeat >= 1) {
                if (repeat == 1 && timer_elapsed_u32(&repeat_timer) > 10) {
                    repeat = -1;
                }
            } else {
                if (g_scene_scancode == SC_ENTER || g_scene_confirm) {
                    object = g_inventory[g_inventory_index][cursor];
                    g_action = (s16)(1 << (s_actions[action] - 23));
                    use = 1;
                    done = 1;
                }
                if (g_scene_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
                    inventory_draw_actions(-1);
                    in_actions = 0;
                    drawn = -1;
                    repeat = 2;
                    inventory_draw_object(object_get(object)->field_24);
                    continue;
                }
                if ((g_scene_dir_flags & DIR_UP) && action > 0) action--;
                if ((g_scene_dir_flags & DIR_DOWN) && action < s_action_count - 1) action++;
                if (g_scene_scancode || g_scene_dir_flags || g_scene_confirm) {
                    repeat = 1;
                    timer_reset_u32(&repeat_timer);
                }
            }
            if (drawn != action) {
                drawn = action;
                inventory_draw_actions(action);
            }
        } else {
            if (repeat >= 1) {
                if (repeat == 1 && timer_elapsed_u32(&repeat_timer) > 10) {
                    repeat = -1;
                }
            } else if (g_scene_scancode != 0x1c && !g_scene_confirm
                       && !(g_scene_dir_flags & (DIR_LEFT | DIR_RIGHT))) {
                if ((g_scene_dir_flags & DIR_UP) && cursor > 0) cursor--;
                if ((g_scene_dir_flags & DIR_DOWN) && cursor < g_inventory_count[g_inventory_index] - 1) cursor++;
                if (cursor >= first + VISIBLE_ITEMS) first++;
                if (cursor < first) first--;
                if ((g_last_scancode & 0xff) == 0xcf) {   /* End */
                    cursor = (s16)(g_inventory_count[g_inventory_index] - 1);
                    if (cursor >= VISIBLE_ITEMS) {
                        first = (s16)(cursor - 4);
                    }
                }
                if ((g_last_scancode & 0xff) == 0xc7) {   /* Home */
                    cursor = 0;
                    first = 0;
                }
                if (g_scene_scancode || g_scene_dir_flags || g_scene_confirm) {
                    repeat = 1;
                    timer_reset_u32(&repeat_timer);
                }
            } else {
                inventory_draw_list(first, cursor, 14);
                in_actions = 1;
                drawn = -1;
                action = 0;
                repeat = 2;
                inventory_draw_object(object_get(object)->field_24);
                continue;
            }

            if (drawn != cursor) {
                u16 flags2;
                int bit;

                object = inventory_draw_list(first, cursor, 1);
                g_preview_body = object_get(object)->found_body;
                flags2 = object_get(object)->flags2;
                s_action_count = 0;
                for (bit = 0; bit < 11; bit++) {
                    if ((flags2 & (1 << bit)) && s_action_count < MAX_ACTIONS) {
                        s_actions[s_action_count++] = (s16)(bit + 23);
                    }
                }
                inventory_draw_actions(-1);
                draw_present();
                drawn = cursor;
            }
        }

        inventory_draw_object(object_get(object)->field_24);
        if (first_frame) {
            first_frame = 0;
            if (g_black_screen) {
                palette_fade(0, 0);
            }
        }
        platform_delay(10);
    }

    ticks_unfreeze();
    g_init_view = 1;
    while (g_key_confirm || (g_last_scancode & 0xff) || g_key_dir_flags) {
        input_poll();
    }
    g_scene_dir_flags = 0;
    g_scene_scancode = 0;
    g_scene_confirm = 0;

    if (use) {
        life_run_global(object);
    }
    shaking_restore();
}
