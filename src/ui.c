/*
 * ui.c - in-game text and the dialogs reached from the scene loop.
 *
 *   message_show / message_draw  0x4153f6 / 0x41512f  the fading message
 *                                                      lines at the bottom
 *   object_found                 0x40dc7d              "take this object?"
 */

#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "font.h"
#include "life.h"
#include "menu.h"
#include "platform.h"
#include "r3d.h"
#include "raster.h"
#include "subsystems.h"
#include "text.h"
#include "world.h"

#define MESSAGE_SLOTS 5

/* 0x444f06: five slots of { text record, timer }. */
struct message_slot {
    const struct text_record *text;
    u32 timer;
};

static struct message_slot s_messages[MESSAGE_SLOTS];
static u8 s_colour_table[256];

/* ---- text helpers ------------------------------------------------------ */

void text_draw_string(s16 x, s16 y, const char *text, u8 colour)
{
    font_set_colour(s_colour_table, colour);
    font_bind(g_font_data, s_colour_table, 1);
    font_draw_string(g_raster.target, g_raster.pitch, g_raster.height, x, y, text);
}

/* 0x4150bb - eax = x, edx = y, ebx = text id, ecx = colour. */
void text_draw_centered(s16 x, s16 y, s16 id, u8 colour)
{
    const struct text_record *r = text_get_record(id);

    if (r == NULL) {
        return;
    }
    text_draw_string((s16)(x - r->width / 2), y, r->text, colour);
}

/* 0x41502f - the shadow at y + 1 first, then the face. */
void text_draw_centered_shadowed(s16 x, s16 y, s16 id, u8 colour, u8 shadow)
{
    const struct text_record *r = text_get_record(id);

    if (r == NULL) {
        return;
    }
    text_draw_string((s16)(x - r->width / 2), (s16)(y + 1), r->text, shadow);
    text_draw_string((s16)(x - r->width / 2), y, r->text, colour);
}

/* ---- messages ---------------------------------------------------------- */

/* 0x4154a6 */
void message_reset(void)
{
    memset(s_messages, 0, sizeof(s_messages));
}

/* 0x4153f6 - restart the line if it is already up, else take a free slot. */
void message_show(int text_id)
{
    const struct text_record *r = text_get_record(text_id);
    int i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < MESSAGE_SLOTS; i++) {
        if (s_messages[i].text == r) {
            timer_reset_u32(&s_messages[i].timer);
            return;
        }
    }
    for (i = 0; i < MESSAGE_SLOTS; i++) {
        if (s_messages[i].text == NULL) {
            s_messages[i].text = r;
            timer_reset_u32(&s_messages[i].timer);
            return;
        }
    }
}

/* 0x41512f - lines stack upwards from the bottom and fade out after 80
 * ticks; they are gone after 208. Returns whether anything was drawn. */
int message_draw(void)
{
    s16 y = 199;
    int drawn = 0;
    int i;

    for (i = 0; i < MESSAGE_SLOTS; i++) {
        struct message_slot *m = &s_messages[i];
        u32 elapsed;
        s16 x;

        if (m->text == NULL) {
            continue;
        }

        y = (s16)(y - 14);
        x = (s16)(160 - m->text->width / 2);
        elapsed = timer_elapsed_u32(&m->timer);

        if (elapsed >= 0xd0) {
            m->text = NULL;
            continue;
        }

        if (g_black_screen) {
            raster_rect(0, 319, (s16)(y - 14), (s16)(y + 14), 0);
        }
        text_draw_string(x, (s16)(y + 1), m->text->text,
                         (u8)(elapsed >= 0x50 ? ((elapsed - 0x50) >> 3) + 14 : 14));
        if (!g_black_screen) {
            drawn = 1;
        }
    }

    return drawn;
}

/* ---- found object dialog (0x40dc7d, 0x40dae9) --------------------------- */

s16 g_preview_body;    /* 0x4350a9, object body turning in a dialog */
s16 g_preview_angle;   /* 0x4350a7 */

/* 0x41b4bc - remap a picture into one 16 colour band. */
static void dim_picture(u8 *dst, const u8 *src, u8 band)
{
    int i;

    for (i = 0; i < 64000; i++) {
        dst[i] = (u8)((src[i] & 0x0f) + band);
    }
}

/* 0x40dae9 */
static void object_found_draw(s16 choice, s16 name, s16 distance)
{
    raster_copy_rect(50, 0, 270, 190, g_back_buffer);
    r3d_set_camera_target(0, 0, 0, 60, g_preview_angle, 0, distance);
    r3d_set_clip_y(10000);
    r3d_draw_body(0, 0, 0, 0, 0, 0, hqr_get(g_cache_body, g_preview_body, NULL));

    text_draw_centered(160, 20, 20, 1);
    text_draw_centered(160, 34, name, 1);

    if (choice == 0) {
        text_draw_centered_shadowed(100, 132, 21, 1, 4);
        text_draw_centered(220, 132, 22, 64);
    } else if (choice == 1) {
        text_draw_centered(100, 132, 21, 64);
        text_draw_centered_shadowed(220, 132, 22, 1, 4);
    } else if (choice == 2) {
        text_draw_centered_shadowed(160, 132, 10, 1, 4);
    }
}

/* 0x40dc7d - mode 0: walked into it, 1: life FOUND, 2: FOUND from an
 * object life (no refusal cool-down). */
void object_found(s16 object, int mode)
{
    struct world_object *o;
    s16 choice = 1;
    s16 distance = 15000, step = -200;
    int done = 0;

    if (object < 0) {
        return;
    }
    o = object_get(object);
    if (mode != 0 && (o->flags2 & 0x8000)) {
        return;  /* already carried */
    }

    if (mode != 2) {
        /* The object record's track number doubles as the time of the last
         * refusal; the dialog stays away for 180 ticks after one. */
        ticks_update();
        if (o->track_number != 0 && (u16)((u16)g_ticks - (u16)o->track_number) < 180) {
            return;
        }
    }
    o->track_number = 0;

    ticks_freeze();

    if (g_inventory_count[g_inventory_index] + 1 == 40) {
        choice = 2;
    }

    g_preview_body = o->found_body;
    r3d_set_projection(160, 100, 128, 300, 298);
    g_preview_angle = 0;
    raster_set_clip(0, 0, 319, 199);

    if (!g_frame_flag) {
        memcpy(g_back_buffer, g_screen, 64000);
        dim_picture(g_back_buffer, g_back_buffer, 48);
        g_frame_flag = 1;
    }

    {
        struct raster_image back = { 320, 200, g_back_buffer };
        raster_blit(0, 0, &back);
    }
    object_found_draw(choice, o->found_name, distance);
    draw_present();
    g_message_shown = 1;

    while (!done) {
        input_poll();
        g_scene_scancode = (s16)(g_last_scancode & 0xff);
        g_scene_dir_flags = (s16)g_key_dir_flags;
        g_scene_confirm = (s16)g_key_confirm;

        if (g_message_shown) {
            if (!g_scene_scancode && !g_scene_dir_flags && !g_scene_confirm) {
                g_message_shown = 0;
            }
        } else {
            if (g_scene_scancode == SC_ESC) {
                if (choice != 2) {
                    choice = 0;
                }
                done = 1;
            }
            if (choice != 2) {
                if (g_scene_dir_flags & DIR_LEFT) choice = 0;
                if (g_scene_dir_flags & DIR_RIGHT) choice = 1;
            }
            if (g_scene_scancode == SC_ENTER || g_scene_confirm) {
                done = 1;
            }
        }

        g_preview_angle = (s16)(g_preview_angle - 8);
        distance = (s16)(distance + step);
        if (distance < 8000) step = (s16)-step;
        if (distance > 25000) step = (s16)-step;
        object_found_draw(choice, o->found_name, distance);
        draw_present();
        platform_delay(16);
    }

    if (choice == 1) {
        object_take(object);
    } else {
        ticks_update();
        o->track_number = (s16)g_ticks;
    }

    ticks_unfreeze();

    while ((g_last_scancode & 0xff) || g_key_confirm) {
        input_poll();
    }
    g_scene_dir_flags = 0;
    g_scene_scancode = 0;
    g_scene_confirm = 0;
    g_init_view = 1;
}
