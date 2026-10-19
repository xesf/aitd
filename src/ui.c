/*
 * ui.c - in-game text and the dialogs reached from the scene loop.
 *
 *   message_show / message_draw  0x4153f6 / 0x41512f  the fading message
 *                                                      lines at the bottom
 *   object_found                 0x40dc7d              "take this object?"
 */

#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "game.h"
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
    u32 frames;     /* draws so far, for titles that count frames */
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
            s_messages[i].frames = 0;
            return;
        }
    }
    for (i = 0; i < MESSAGE_SLOTS; i++) {
        if (s_messages[i].text == NULL) {
            s_messages[i].text = r;
            timer_reset_u32(&s_messages[i].timer);
            s_messages[i].frames = 0;
            return;
        }
    }
}

/* 0x41512f - lines stack upwards from the bottom and fade out after 80
 * ticks; they are gone after 208. Returns whether anything was drawn. */
int message_draw(void)
{
    const struct message_style *fade = &game_current()->message;
    /* TG640.EX_ 0x40aa24 doubles the base line, the pitch and the centre. */
    s16 pitch = SCALED(14);
    s16 y = (s16)(SCALED(fade->y) + pitch);
    int drawn = 0;
    int i;

    for (i = 0; i < MESSAGE_SLOTS; i++) {
        struct message_slot *m = &s_messages[i];
        u32 elapsed;
        s16 x;

        if (m->text == NULL) {
            continue;
        }

        y = (s16)(y - pitch);
        x = (s16)(SCALED(160) - m->text->width / 2);
        elapsed = fade->per_frame ? ++m->frames : timer_elapsed_u32(&m->timer);

        if (elapsed >= (u32)fade->life) {
            m->text = NULL;
            continue;
        }

        if (g_black_screen) {
            raster_rect(0, (s16)(g_screen_width - 1), (s16)(y - pitch), (s16)(y + pitch), 0);
        }
        text_draw_string(x, (s16)(y + 1), m->text->text,
                         (u8)(fade->colour + (elapsed >= (u32)fade->fade_after
                                              ? (elapsed - fade->fade_after) / fade->fade_step : 0)));
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

/* Time Gate's version (TG320.EX_ 0x4150d7 / 0x414f7e) has the same
 * layout with its own colours, a fixed camera the object tilts and turns
 * in front of, the scene dimmed into palette band 16..31 by brightness and
 * a 360 tick cool-down after a refusal. */
static int found_tg(void)
{
    return game_current()->inventory_style == INVENTORY_ICONS;
}

/* TG 0x42920d: each colour to the closest band entry in brightness. */
static void dim_by_brightness(u8 *pixels, u8 first, u8 last)
{
    u8 map[256];
    int c, k;

    for (c = 0; c < 256; c++) {
        const u8 *rgb = g_palette_buffer + c * 3;
        int lum = rgb[0] * 30 + rgb[1] * 59 + rgb[2] * 11, best = first, best_d = 1 << 30;

        for (k = first; k <= last; k++) {
            const u8 *b = g_palette_buffer + k * 3;
            int d = abs(b[0] * 30 + b[1] * 59 + b[2] * 11 - lum);
            if (d < best_d) {
                best_d = d;
                best = k;
            }
        }
        map[c] = (u8)best;
    }
    for (c = 0; c < (int)SCREEN_PIXELS; c++) {
        pixels[c] = map[pixels[c]];
    }
}

/* 0x40dae9 */
static void object_found_draw(s16 choice, s16 name, s16 distance)
{
    int tg = found_tg();
    u8 normal = tg ? 0xff : 64, hi = tg ? 0xf3 : 1, shadow = tg ? 0xff : 4;

    if (tg) {
        struct raster_image back = { (u16)g_screen_width, (u16)g_screen_height, g_back_buffer };

        raster_blit(0, 0, &back);
        r3d_set_camera_position(0, 0, (s16)-distance);
        r3d_set_camera_angles(0, 0, 0);
        r3d_set_clip_y(10000);
        r3d_draw_body(0, 0, 0, 0x80, g_preview_angle, 0, hqr_get(g_cache_body, g_preview_body, NULL));
    } else {
        raster_copy_rect(50, 0, 270, 190, g_back_buffer);
        r3d_set_camera_target(0, 0, 0, 60, g_preview_angle, 0, distance);
        r3d_set_clip_y(10000);
        r3d_draw_body(0, 0, 0, 0, 0, 0, hqr_get(g_cache_body, g_preview_body, NULL));
    }

    text_draw_centered(SCALED(160), SCALED(20), 20, tg ? normal : 1);
    text_draw_centered(SCALED(160), SCALED(34), name, tg ? normal : 1);

    if (choice == 0) {
        text_draw_centered_shadowed(SCALED(100), SCALED(132), 21, hi, shadow);
        text_draw_centered(SCALED(220), SCALED(132), 22, normal);
    } else if (choice == 1) {
        text_draw_centered(SCALED(100), SCALED(132), 21, normal);
        text_draw_centered_shadowed(SCALED(220), SCALED(132), 22, hi, shadow);
    } else if (choice == 2) {
        text_draw_centered_shadowed(SCALED(160), SCALED(132), 10, hi, shadow);
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
        if (o->track_number != 0
            && (u16)((u16)g_ticks - (u16)o->track_number) < (found_tg() ? 360 : 180)) {
            return;
        }
    }
    o->track_number = 0;

    ticks_freeze();

    if (g_inventory_count[g_inventory_index] + 1 == 40) {
        choice = 2;
    }

    g_preview_body = o->found_body;
    r3d_set_projection(SCALED(160), SCALED(100), 128, SCALED(300), SCALED(298));
    g_preview_angle = 0;
    raster_reset_clip();

    if (!g_frame_flag) {
        memcpy(g_back_buffer, g_screen, SCREEN_PIXELS);
        if (found_tg()) {
            dim_by_brightness(g_back_buffer, 16, 31);
        } else {
            dim_picture(g_back_buffer, g_back_buffer, 48);
        }
        g_frame_flag = 1;
    }

    {
        struct raster_image back = { (u16)g_screen_width, (u16)g_screen_height, g_back_buffer };
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
