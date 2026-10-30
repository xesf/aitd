/*
 * ed_model.c - the model viewer: every body of LISTBODY.PAK drawn by the
 * engine's own renderer (r3d.c), turning, in the palette the editor found
 * on screen (the room's in the game, the menu's before it). Time Gate's
 * textured bodies get their pages and the save drum's light.
 *
 * PgUp and PgDn (or [ and ]) play an animation of LISTANIM.PAK on a body with bones,
 * keyframe to keyframe as the game blends them (anim.c); the game's clock
 * keeps counting while the editor is open, so the blend runs.
 */

#include <stdio.h>
#include <stdlib.h>

#include "aitd.h"
#include "anim.h"
#include "body.h"
#include "pak.h"
#include "r3d.h"
#include "raster.h"
#include "editor/ed_ui.h"

static int s_count;
static int s_entry;
static int s_loaded = -1;
static u8 *s_body;
static u32 s_size;
static struct body s_info;
static int s_parsed;
static s16 s_alpha = 0x60, s_beta;
static int s_spin = 1;
static int s_zoom = 100;        /* percent */
static int s_anim_count;
static int s_anim = -1;         /* -1: the rest pose */
static int s_anim_loaded = -1;
static u8 *s_anim_data;
static int s_key;

static void model_open(void)
{
    struct pak pak;

    s_count = 0;
    if (pak_open(&pak, "LISTBODY.PAK")) {
        s_count = pak_entry_count(&pak);
        pak_close(&pak);
    }
    s_loaded = -1;
    s_anim_count = 0;
    if (pak_open(&pak, "LISTANIM.PAK")) {
        s_anim_count = pak_entry_count(&pak);
        pak_close(&pak);
    }
}

static void drop_anim(void)
{
    if (s_anim_data != NULL) {
        mem_free(s_anim_data);
        s_anim_data = NULL;
    }
    s_anim_loaded = -1;
}

static void model_close(void)
{
    if (s_body != NULL) {
        mem_free(s_body);
        s_body = NULL;
    }
    s_loaded = -1;
    drop_anim();
}

/* A fresh copy of the body (its pose is the animation's), then keyframe 0
 * of the animation, the pose the blend starts from. */
static void start_anim(void)
{
    u32 size = 0;

    s_loaded = -1;
    if (s_body != NULL) {
        mem_free(s_body);
    }
    s_loaded = s_entry;
    s_body = pak_load("LISTBODY.PAK", s_entry, &s_size);
    s_parsed = s_body != NULL && body_parse(s_body, s_size, &s_info);
    drop_anim();
    s_key = 0;
    if (s_anim < 0 || s_body == NULL) {
        return;
    }
    s_anim_data = pak_load("LISTANIM.PAK", s_anim, &size);
    s_anim_loaded = s_anim;
    if (s_anim_data != NULL && anim_frame_count(s_anim_data) > 0) {
        anim_set_frame(0, s_anim_data, s_body);
    }
}

static void load(void)
{
    if (s_loaded == s_entry) {
        return;
    }
    start_anim();
}

static void model_key(int sc)
{
    if (sc == ED_KEY_PGUP || sc == ED_KEY_PGDN || sc == 0x1a || sc == 0x1b) {
        s_anim += (sc == ED_KEY_PGDN || sc == 0x1b) ? 1 : -1;    /* also [ and ] */
        if (s_anim < -1) {
            s_anim = s_anim_count - 1;
        } else if (s_anim >= s_anim_count) {
            s_anim = -1;
        }
        start_anim();
        return;
    }
    if (ed_list_key(sc, &s_entry, s_count, 10)) {
        return;
    }
    switch (sc) {
    case ED_KEY_LEFT: s_beta = (s16)(s_beta - 16); s_spin = 0; break;
    case ED_KEY_RIGHT: s_beta = (s16)(s_beta + 16); s_spin = 0; break;
    case ED_KEY_SPACE: s_spin ^= 1; break;
    case ED_KEY_ENTER: s_alpha = (s16)((s_alpha + 0x40) & 0x3ff); break;
    case ED_KEY_PLUS: if (s_zoom < 400) s_zoom += 10; break;
    case ED_KEY_MINUS: if (s_zoom > 20) s_zoom -= 10; break;
    default: break;
    }
}

static s16 extent(void)
{
    int i, m = 200;

    for (i = 0; i < 6 && s_parsed; i++) {
        int v = s_info.bounds[i] < 0 ? -s_info.bounds[i] : s_info.bounds[i];
        if (v > m) {
            m = v;
        }
    }
    return (s16)m;
}

static void model_draw(void)
{
    static const s32 light_dir[3] = { -88000, 48000, -100 };
    static const s32 light_pos[3] = { 0, 0, 0 };
    char line[128];
    s16 top = ed_top(), bottom = ed_bottom();
    s32 distance;

    load();
    ed_clear(g_ed.dim);
    if (s_body != NULL) {
        s16 cy = s_parsed ? (s16)((s_info.bounds[2] + s_info.bounds[3]) / 2) : 0;

        distance = (s32)extent() * 400 / s_zoom;
        if (distance > 30000) {
            distance = 30000;
        }
        raster_set_clip(0, top, (s16)(g_screen_width - 1), bottom);
        r3d_set_projection(SCALED(160), (s16)((top + 3 * ed_line() + bottom) / 2), 128, SCALED(300), SCALED(300));
        r3d_set_camera_target(0, cy, 0, s_alpha, s_beta, 0, (s16)distance);
        r3d_set_clip_y(10000);
        r3d_set_light_raw(light_dir, light_pos);
        if (s_anim_data != NULL && anim_frame_count(s_anim_data) > 0
            && anim_set_inter(s_key, s_anim_data, s_body, 0) == 1) {
            s_key = (s_key + 1) % anim_frame_count(s_anim_data);
        }
        r3d_draw_body(0, 0, 0, 0, 0, 0, s_body);
        raster_reset_clip();
    }
    if (s_spin) {
        s_beta = (s16)((s_beta + 6) & 0x3ff);
    }
    ed_bars(1, "Up/Dn body PgUp/Dn anim L/R Spc Ent +/-");
    if (s_body == NULL) {
        snprintf(line, sizeof(line), "LISTBODY %d/%d  empty", s_entry, s_count - 1);
    } else if (s_parsed) {
        snprintf(line, sizeof(line), "LISTBODY %d/%d %ub flags %04x %u pts",
                 s_entry, s_count - 1, (unsigned)s_size, s_info.flags, s_info.vertex_count);
    } else {
        snprintf(line, sizeof(line), "LISTBODY %d/%d  %u b", s_entry, s_count - 1,
                 (unsigned)s_size);
    }
    ed_text(3, (s16)(top + 1), line, g_ed.white);
    if (s_parsed) {
        snprintf(line, sizeof(line), "x %d..%d  y %d..%d  z %d..%d",
                 s_info.bounds[0], s_info.bounds[1], s_info.bounds[2],
                 s_info.bounds[3], s_info.bounds[4], s_info.bounds[5]);
        ed_text(3, (s16)(top + 1 + ed_line()), line, g_ed.grey);
    }
    if (s_anim < 0) {
        snprintf(line, sizeof(line), "LISTANIM -  rest pose (%d anims)", s_anim_count);
    } else if (s_anim_data == NULL) {
        snprintf(line, sizeof(line), "LISTANIM %d  empty", s_anim);
    } else {
        snprintf(line, sizeof(line), "LISTANIM %d/%d key %d/%d %d bones%s", s_anim,
                 s_anim_count - 1, s_key, anim_frame_count(s_anim_data),
                 (s16)(s_anim_data[2] | (s_anim_data[3] << 8)),
                 (s_info.flags & 2) ? "" : " (body: none)");
    }
    ed_text(3, (s16)(top + 1 + 2 * ed_line()), line, s_anim < 0 ? g_ed.grey : g_ed.accent);
}

const struct ed_viewer ed_view_models = {
    "Models", model_open, model_close, model_key, model_draw
};
