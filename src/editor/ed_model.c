/*
 * ed_model.c - the model viewer: every body of LISTBODY.PAK drawn by the
 * engine's own renderer (r3d.c), turning, in the palette the editor found
 * on screen (the room's in the game, the menu's before it). Time Gate's
 * textured bodies get their pages and the save drum's light.
 */

#include <stdio.h>
#include <stdlib.h>

#include "aitd.h"
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

static void model_open(void)
{
    struct pak pak;

    s_count = 0;
    if (pak_open(&pak, "LISTBODY.PAK")) {
        s_count = pak_entry_count(&pak);
        pak_close(&pak);
    }
    s_loaded = -1;
}

static void model_close(void)
{
    if (s_body != NULL) {
        mem_free(s_body);
        s_body = NULL;
    }
    s_loaded = -1;
}

static void load(void)
{
    if (s_loaded == s_entry) {
        return;
    }
    model_close();
    s_loaded = s_entry;
    s_body = pak_load("LISTBODY.PAK", s_entry, &s_size);
    s_parsed = s_body != NULL && body_parse(s_body, s_size, &s_info);
}

static void model_key(int sc)
{
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
        r3d_set_projection(SCALED(160), (s16)((top + bottom) / 2), 128, SCALED(300), SCALED(300));
        r3d_set_camera_target(0, cy, 0, s_alpha, s_beta, 0, (s16)distance);
        r3d_set_clip_y(10000);
        r3d_set_light_raw(light_dir, light_pos);
        r3d_draw_body(0, 0, 0, 0, 0, 0, s_body);
        raster_reset_clip();
    }
    if (s_spin) {
        s_beta = (s16)((s_beta + 6) & 0x3ff);
    }
    ed_bars(1, "Up/Down body  Left/Right turn  Space spin  Enter tilt  +/- zoom");
    if (s_body == NULL) {
        snprintf(line, sizeof(line), "LISTBODY %d / %d   empty", s_entry, s_count - 1);
    } else if (s_parsed) {
        snprintf(line, sizeof(line), "LISTBODY %d / %d   %u bytes   flags %04x   %u points",
                 s_entry, s_count - 1, (unsigned)s_size, s_info.flags, s_info.vertex_count);
    } else {
        snprintf(line, sizeof(line), "LISTBODY %d / %d   %u bytes", s_entry, s_count - 1,
                 (unsigned)s_size);
    }
    ed_text(3, (s16)(top + 1), line, g_ed.white);
    if (s_parsed) {
        snprintf(line, sizeof(line), "x %d..%d  y %d..%d  z %d..%d",
                 s_info.bounds[0], s_info.bounds[1], s_info.bounds[2],
                 s_info.bounds[3], s_info.bounds[4], s_info.bounds[5]);
        ed_text(3, (s16)(top + 1 + ed_line()), line, g_ed.grey);
    }
}

const struct ed_viewer ed_view_models = {
    "Models", model_open, model_close, model_key, model_draw
};
