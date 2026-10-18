/*
 * world.c - floors, rooms, cameras, objects and actor instantiation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "game.h"
#include "life.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "r3d.h"
#include "subsystems.h"
#include "world.h"

u8 **g_floor_rooms;
int g_floor_room_count;
u8 **g_floor_cameras;
int g_floor_camera_count;

u8 *g_room_data;
u8 *g_room_cameras[64];
s16 g_camera_zone[64];
u8 *g_room_hardcol;
s16 g_room_hardcol_count;
u8 *g_room_scezone;
s16 g_room_scezone_count;
s16 g_room_camera_count;

u8 *g_camera_masks;
u8 *g_camera_bganims;

s16 g_cur_floor;
s16 g_cur_room;
s16 g_cur_camera = -1;
s16 g_new_camera;
s16 g_viewed_room = -1;
s8 g_visible_rooms[64];

struct world_object *g_objects;
s16 g_object_count;
struct actor g_actors[ACTOR_MAX];
struct actor *g_cur_actor = g_actors;
s16 g_cur_actor_index;

s16 *g_vars;
u32 g_vars_size;

s16 g_visible_count;
s16 g_visible_actors[ACTOR_MAX];
s16 g_draw_list[ACTOR_MAX + 32];

s16 g_player_object = 1;
s16 g_player_actor;

s16 g_change_floor;
s16 g_change_room;
s16 g_redraw_state;
s16 g_actors_dirty;
s16 g_objects_dirty;
s16 g_static_dirty;

/* Special cutscene backgrounds substituted for a camera (0x414883). */

/* 0x407b1b */
struct world_object *object_get(int index)
{
    if (index < 0) {
        return NULL;
    }
    return &g_objects[index];
}

/* ---- loading ----------------------------------------------------------- */

/* 0x414d84 - every entry of a container, NULL terminated. */
static u8 **load_all_entries(const char *name, int *out_count)
{
    struct pak pak;
    u8 **table;
    int count, i;

    if (!pak_open(&pak, name)) {
        fatal_error(0, name);
    }
    count = pak_entry_count(&pak);
    pak_close(&pak);

    table = mem_alloc_checked(sizeof(*table) * (size_t)(count + 1));
    for (i = 0; i < count; i++) {
        table[i] = file_load_alloc(name, i);
    }
    table[count] = NULL;

    *out_count = count;
    return table;
}

static void build_name(char *out, size_t size, const char *prefix, int index)
{
    snprintf(out, size, "%s%02d.PAK", prefix, index);
}

int g_camera_view_size = 16;

/* ETAGE##.PAK blobs of the earlier titles: entry 0 holds every room and
 * entry 1 every camera, each behind a u32 offset table. The tables built
 * from them point into the blobs. */
static u8 *s_room_blob;
static u8 *s_camera_blob;

static u8 **split_blob(u8 *blob, u32 size, int *out_count)
{
    u32 first = blob_u32(blob, 0);
    int max = (int)(first / 4);
    u32 last = 0;
    u8 **table = mem_alloc_checked(sizeof(*table) * (size_t)(max + 1));
    int count = 0;

    /* Offsets rise until the table ends (FITD reads it the same way). */
    while (count < max) {
        u32 offset = blob_u32(blob, count * 4);

        if (offset >= size || offset <= last) {
            break;
        }
        table[count++] = blob + offset;
        last = offset;
    }
    table[count] = NULL;
    *out_count = count;
    return table;
}

static void load_floor_blobs(s16 floor)
{
    char name[32];
    u32 room_size = 0, camera_size = 0;

    build_name(name, sizeof(name), game_current()->room_prefix, floor);
    s_room_blob = pak_load(name, 0, &room_size);
    s_camera_blob = pak_load(name, 1, &camera_size);
    if (s_room_blob == NULL || s_camera_blob == NULL) {
        fatal_error(0, name);
    }
    g_floor_rooms = split_blob(s_room_blob, room_size, &g_floor_room_count);
    g_floor_cameras = split_blob(s_camera_blob, camera_size, &g_floor_camera_count);
}

static void free_all_entries(u8 **table);

/* Rooms and cameras of the current floor, however they were loaded. */
static void free_floor(void)
{
    if (s_room_blob != NULL) {
        mem_free(g_floor_cameras);
        mem_free(g_floor_rooms);
        mem_free(s_room_blob);
        mem_free(s_camera_blob);
        s_room_blob = s_camera_blob = NULL;
    } else {
        free_all_entries(g_floor_cameras);
        free_all_entries(g_floor_rooms);
    }
    g_floor_cameras = g_floor_rooms = NULL;
}

/* 0x414e0b */
static void free_all_entries(u8 **table)
{
    u8 **p;

    if (table == NULL) {
        return;
    }
    for (p = table; *p != NULL; p++) {
        mem_free(*p);
    }
    mem_free(table);
}


/* 0x40a9b3 */
void world_set_floor(s16 floor)
{
    const struct game_profile *profile = game_current();
    char name[32];

    ticks_freeze();

    free_floor();
    if (g_camera_masks != NULL) {
        mem_free(g_camera_masks);
        g_camera_masks = NULL;
    }
    if (g_camera_bganims != NULL) {
        mem_free(g_camera_bganims);
        g_camera_bganims = NULL;
    }

    /* 0x40a9f6: three caches are rewound here (0x4198e3); entries never
     * move in this port, so there is nothing to rewind. */

    g_cur_floor = floor;
    g_camera_view_size = profile->camera_view_size;

    if (profile->floor_layout == FLOOR_ETAGE_BLOB) {
        load_floor_blobs(floor);
    } else {
        build_name(name, sizeof(name), profile->room_prefix, floor);   /* SAL / ETAGE */
        g_floor_rooms = load_all_entries(name, &g_floor_room_count);

        build_name(name, sizeof(name), profile->camera_prefix, floor); /* CAM / CAMSAL */
        g_floor_cameras = load_all_entries(name, &g_floor_camera_count);
    }

    g_cur_camera = -1;
    g_change_room = 1;
    g_change_floor = 0;

    ticks_unfreeze();
}

/* 0x40a6cb */
void world_set_room(s16 room)
{
    s16 old_x = 0, old_y = 0, old_z = 0;
    s16 old_camera = -1;
    s16 found = 0;
    int i;

    ticks_freeze();

    if (g_cur_camera != -1) {
        old_x = ROOM_X(g_room_data);
        old_y = ROOM_Y(g_room_data);
        old_z = ROOM_Z(g_room_data);
        old_camera = blob_s16(g_room_data, 12 + g_cur_camera * 2);
    }

    g_room_data = g_floor_rooms[room];
    g_cur_room = room;
    g_room_camera_count = blob_s16(g_room_data, 10);

    /* +0 points at the collision boxes, +2 at the scene zones. */
    g_room_scezone = g_room_data + (blob_s16(g_room_data, 2) & ~1);
    g_room_scezone_count = blob_s16(g_room_scezone, 0);
    g_room_scezone += 2;

    g_room_hardcol = g_room_data + (blob_s16(g_room_data, 0) & ~1);
    g_room_hardcol_count = blob_s16(g_room_hardcol, 0);
    g_room_hardcol += 2;

    for (i = 0; i < g_room_camera_count && i < 64; i++) {
        s16 cam_index = blob_s16(g_room_data, 12 + i * 2);
        u8 *cam;
        int j, views;

        if (old_camera == cam_index) {
            found = (s16)i;
        }

        cam = g_floor_cameras[cam_index];
        g_room_cameras[i] = cam;

        views = CAMERA_VIEW_COUNT(cam);
        for (j = 0; j < views; j++) {
            if (blob_s16(CAMERA_VIEW(cam, j), 0) == g_cur_room) {
                break;
            }
        }
        g_camera_zone[i] = (s16)(blob_s16(CAMERA_VIEW(cam, j), 4) >> 1);
    }

    /* 0x40a87e: carry every actor across into the new room's frame. */
    if (old_camera != -1) {
        s16 dx = (s16)((ROOM_X(g_room_data) - old_x) * 10);
        s16 dy = (s16)((ROOM_Y(g_room_data) - old_y) * 10);
        s16 dz = (s16)((ROOM_Z(g_room_data) - old_z) * 10);

        for (i = 0; i < ACTOR_MAX; i++) {
            struct actor *a = &g_actors[i];

            if (a->object == -1) {
                continue;
            }
            if (i == g_player_actor) {
                a->world_x = a->room_x;
                a->world_y = a->room_y;
                a->world_z = a->room_z;
            } else {
                a->world_x = (s16)(a->world_x - dx);
                a->world_y = (s16)(a->world_y + dy);
                a->world_z = (s16)(a->world_z + dz);
            }
        }
    }

    g_new_camera = found;
    g_init_view = 1;
    g_change_room = 0;

    ticks_unfreeze();
}

/*
 * 0x414883 - load a camera's background into the front buffer and take its
 * palette. A handful of cameras are swapped for a different picture from
 * ITD_RESS.PAK once the story has moved on (flags at 0x444b2a..).
 */
void world_load_background(s16 camera)
{
    static const struct {
        int flag;   /* DEFINES word 5 + flag */
        s16 floor, room, camera, room2;
        int entry;
    } swaps[] = {
        { 0, 9, 10, 17, -1, 61 },
        { 0, 9, 10, 1, -1, 62 },
        { 1, 12, 11, 6, -1, 63 },
        { 1, 12, 11, 4, -1, 64 },
        { 1, 0, 15, 1, 0, 5 },
        { 1, 0, 15, 7, 0, 6 },
        { 2, 3, 4, 11, -1, 10 },
        { 4, 1, 6, 24, -1, 65 },
        { 3, 13, 13, 5, 14, 11 },
        { 3, 13, 13, 13, -1, 12 },
        { 6, 1, 0, 17, -1, 9 },
        { 5, 11, 6, 11, -1, 66 },
    };
    const struct game_profile *profile = game_current();
    char name[32];
    int entry = -1;
    const char *pak = name;
    u8 *picture;
    size_t i;

    for (i = 0; game_id() == GAME_AITD3 && i < sizeof(swaps) / sizeof(swaps[0]); i++) {
        if (g_alt_bg_flag(swaps[i].flag) && g_cur_floor == swaps[i].floor
            && (g_cur_room == swaps[i].room || g_cur_room == swaps[i].room2)
            && camera == swaps[i].camera) {
            entry = swaps[i].entry;
            pak = "ITD_RESS.PAK";
            break;
        }
    }

    if (entry < 0) {
        build_name(name, sizeof(name), profile->background_prefix, g_cur_floor);
        entry = camera;
    }

    /* 0x414822: in AITD3 a 4 byte header, 64000 pixels and the palette;
     * the earlier titles have no header, AITD1 no palette either. */
    {
        u32 size = 0;
        int header = profile->background_header;

        picture = (pak == name) ? pak_load(pak, entry, &size) : load_malloc_hqr(pak, entry);
        if (picture == NULL) {
            fatal_error(0, pak);
        }
        if (pak != name) {
            header = 4;     /* ITD_RESS pictures always carry the header */
            size = 64000 + AITD_PALETTE_BYTES + 4;
        }
        memmove(g_front_buffer, picture + header,
                size >= (u32)header + 64000 + AITD_PALETTE_BYTES ? 64000 + AITD_PALETTE_BYTES : 64000);
        mem_free(picture);
    }

    switch (profile->palette_source) {
    case PALETTE_GLOBAL:
        memcpy(g_front_buffer + 64000, g_palette_buffer, AITD_PALETTE_BYTES);
        break;
    case PALETTE_CAMERA_6BIT:
        memcpy(g_palette_buffer, g_front_buffer + 64000, AITD_PALETTE_BYTES);
        if (profile->palette_overlay != NULL) {
            memcpy(g_palette_buffer, profile->palette_overlay, 48);
        }
        for (i = 0; i < AITD_PALETTE_BYTES; i++) {
            g_palette_buffer[i] = (u8)((g_palette_buffer[i] * 255u) / 63u);
        }
        break;
    case PALETTE_CAMERA_8BIT:
        memcpy(g_palette_buffer, g_front_buffer + 64000, AITD_PALETTE_BYTES);
        if (profile->palette_overlay != NULL) {
            memcpy(g_palette_buffer, profile->palette_overlay, 48);
        }
        break;
    }
}

/* 0x40a123 - bring up the current camera. */
void world_setup_camera(void)
{
    const char *mask_prefix = "MASK";
    char name[32];
    u8 *cam;
    s16 cam_file;
    int i, count;
    const u8 *list;

    ticks_freeze();
    bganim_reset();

    if (g_camera_bganims != NULL) {
        mem_free(g_camera_bganims);
    }
    if (g_camera_masks != NULL) {
        mem_free(g_camera_masks);
    }

    cam_file = blob_s16(g_room_data, 12 + g_new_camera * 2);
    world_load_background(cam_file);
    g_cur_camera = g_new_camera;

    /* 0x40a192: the alternate mask set NASK01 replaces MASK01. */
    if (game_id() == GAME_AITD3 && g_alt_bg_flag(6) && g_cur_floor == 1) {
        mask_prefix = "NASK";
    }
    build_name(name, sizeof(name), mask_prefix, g_cur_floor);
    g_camera_masks = file_load_alloc(name, cam_file);

    build_name(name, sizeof(name), "ANIM", g_cur_floor);
    g_camera_bganims = file_load_alloc(name, cam_file);

    cam = g_room_cameras[g_cur_camera];
    r3d_set_camera_angles(blob_s16(cam, 0), blob_s16(cam, 2), blob_s16(cam, 4));
    r3d_set_camera_position(
        (s16)((s16)(blob_s16(cam, 6) - ROOM_X(g_room_data)) * 10),
        (s16)((s16)(ROOM_Y(g_room_data) - blob_s16(cam, 8)) * 10),
        (s16)((s16)(ROOM_Z(g_room_data) - blob_s16(cam, 10)) * 10));
    r3d_set_projection(160, 100, blob_s16(cam, 12), blob_s16(cam, 14),
                       blob_s16(cam, 16));

    world_build_visible_rooms();
    world_update_objects();
    world_build_actor_list();
    draw_sort_actors();
    draw_measure_actors();
    draw_static_actors();

    if (g_init_view == 2) {
        g_redraw_state = 2;
    } else if (g_redraw_state != 2) {
        g_redraw_state = 1;
    }
    g_init_view = 0;

    ticks_unfreeze();

    /* 0x40a3bf: background animations of the first viewed room. */
    list = cam + (blob_s16(cam, 0x1a) & ~1);
    count = blob_s16(list, 0);
    for (i = 0; i < count; i++) {
        bganim_add((u16)i);
    }
}

/* 0x4095a3 */
void world_free(void)
{
    music_shutdown();
    sound_stop_all();

    free_floor();
    if (g_camera_masks != NULL) {
        mem_free(g_camera_masks);
        g_camera_masks = NULL;
    }
    if (g_camera_bganims != NULL) {
        mem_free(g_camera_bganims);
        g_camera_bganims = NULL;
    }
    if (g_vars != NULL) {
        mem_free(g_vars);
        g_vars = NULL;
    }
}

/* 0x409490 */
void world_load_objects(void)
{
    int handle = file_open("OBJETS.ITD", 1);
    u8 count[2];
    int i;

    if (handle == 0) {
        fatal_error(0, "OBJETS.ITD");
    }

    file_read(handle, count, 2);
    g_object_count = (s16)(count[0] | (count[1] << 8));

    if (g_objects == NULL) {
        g_objects = mem_alloc_checked(sizeof(struct world_object) * (size_t)g_object_count);
    }
    for (i = 0; i < g_object_count; i++) {
        /* Alone in the Dark 1 records stop before `mark` (52 bytes). */
        struct world_object *o = &g_objects[i];

        memset(o, 0, sizeof(*o));
        file_read(handle, o, (u32)game_current()->object_size);
        if (game_current()->object_size < (int)sizeof(*o)) {
            o->mark = -1;
        }
        o->flags |= 0x20;
    }
    file_close(handle);

    g_vars = load_malloc("VARS.ITD");
    g_vars_size = file_size("VARS.ITD");

    for (i = 0; i < ACTOR_MAX; i++) {
        g_actors[i].object = -1;
    }
}

/* ---- visibility -------------------------------------------------------- */

/* 0x409ac4 - rooms linked by scene zones of type 0, plus every room the
 * camera sees. */
void world_build_visible_rooms(void)
{
    s8 *out = g_visible_rooms;
    const u8 *room = g_floor_rooms[g_cur_room];
    const u8 *col = room + (blob_s16(room, 2) & ~1);
    const u8 *cam = g_room_cameras[g_cur_camera];
    int count, i;

    *out = -1;

    count = blob_s16(col, 0);
    col += 2;
    for (i = 0; i < count; i++, col += 16) {
        if (blob_s16(col, 14) == ZONE_ROOM_LINK) {
            s8 linked = (s8)blob_s16(col, 12);
            if (!world_room_is_visible(linked) && out < g_visible_rooms + 62) {
                *out++ = linked;
                *out = -1;
            }
        }
    }

    count = CAMERA_VIEW_COUNT(cam);
    for (i = 0; i < count; i++) {
        s8 viewed = (s8)blob_s16(CAMERA_VIEW(cam, i), 0);
        if (!world_room_is_visible(viewed) && out < g_visible_rooms + 62) {
            *out++ = viewed;
            *out = -1;
        }
    }
}

/* 0x4098e9 */
int world_room_is_visible(s16 room)
{
    const s8 *p;

    for (p = g_visible_rooms; *p != -1; p++) {
        if (*p == (s8)room) {
            return 1;
        }
    }
    return 0;
}

/* 0x409943 - also selects the viewed-room record for world_in_view_zone. */
int world_camera_sees_room(s16 room)
{
    const u8 *cam = g_room_cameras[g_cur_camera];
    int count = CAMERA_VIEW_COUNT(cam);
    int i;

    for (i = 0; i < count; i++) {
        if (blob_s16(CAMERA_VIEW(cam, i), 0) == room) {
            g_viewed_room = (s16)i;
            return 1;
        }
    }
    g_viewed_room = -1;
    return 0;
}

/* 0x4099d6 - x, z in tenths, against the selected room's view rectangles. */
int world_in_view_zone(s16 x, s16 z)
{
    const u8 *cam;
    const u8 *zones;
    int count, i;

    if (g_cur_camera == -1) {
        return 0;
    }
    /* Alone in the Dark 1 views have no zone list (+8 is the light). */
    if (g_camera_view_size < 16) {
        return 1;
    }

    cam = g_room_cameras[g_cur_camera];
    zones = cam + (blob_s16(CAMERA_VIEW(cam, g_viewed_room), 8) & ~1);
    count = blob_s16(zones, 0);
    zones += 2;

    for (i = 0; i < count; i++, zones += 8) {
        if (x >= blob_s16(zones, 0) && z >= blob_s16(zones, 2)
            && x <= blob_s16(zones, 4) && z <= blob_s16(zones, 6)) {
            return 1;
        }
    }
    return 0;
}

/* ---- saved boxes ------------------------------------------------------- */

/* 0x40c814 - actor->object indexes the table; object 0 is never saved. */
void object_save_zv(s16 *owner)
{
    struct actor *a = (struct actor *)owner;
    s16 index = a->object;
    u16 *slot;

    if (index <= 0 || index >= OBJECT_SAVED_ZV_MAX) {
        return;
    }
    slot = OBJECT_SAVED_ZV(index);
    memcpy(&slot[1], a->zv, sizeof(a->zv));
    slot[0] = 1;
}

/* 0x40c8e1 */
void object_restore_zv(s16 *owner)
{
    struct actor *a = (struct actor *)owner;
    s16 index = a->object;

    if (index <= 0 || index >= OBJECT_SAVED_ZV_MAX) {
        return;
    }
    memcpy(a->zv, &OBJECT_SAVED_ZV(index)[1], sizeof(a->zv));
}

/* ---- zv computation ---------------------------------------------------- */

/* 0x4227a7 */
void zv_from_body(const u8 *body, s16 *zv)
{
    int i;

    for (i = 0; i < 6; i++) {
        zv[i] = blob_s16(body, 2 + i * 2);
    }
}

/* 0x4227bd - y centred, x and z set to the y half extent. */
static void zv_from_body_centred(const u8 *body, s16 *zv)
{
    s16 half;

    zv_from_body(body, zv);
    half = (s16)((u16)(zv[1] + zv[5]) >> 1);
    zv[1] = half;
    zv[5] = half;
    zv[0] = (s16)-half;
    zv[4] = (s16)-half;
}

/* 0x40b821 - square in x/z on the larger extent. */
static void zv_cube(const u8 *body, s16 *zv, struct actor *a)
{
    s16 size;

    zv_from_body(body, zv);
    size = (s16)(zv[1] - zv[0]);
    if (size < (s16)(zv[5] - zv[4])) {
        size = (s16)(zv[5] - zv[4]);
    }
    zv[0] = (s16)(size / -2);
    zv[1] = (s16)(size / 2);
    zv[4] = (s16)(size / -2);
    zv[5] = (s16)(size / 2);
    object_save_zv((s16 *)a);
}

/* 0x40b601 - box of the eight corners rotated by the object's angles:
 * eax = body, edx = zv, ebx = alpha, ecx = beta, then gamma and the actor
 * whose saved box is refreshed (0x40c814). */
void zv_rotated(const u8 *body, s16 *zv, s16 alpha, s16 beta, s16 gamma,
                struct actor *a)
{
    s16 xmin = 32000, ymin = 32000, zmin = 32000;
    s16 xmax = -32000, ymax = -32000, zmax = -32000;
    s16 x = 0, y = 0, z = 0;
    int i;

    zv_from_body(body, zv);

    for (i = 0; i < 8; i++) {
        switch (i) {
        case 0: x = zv[0]; y = zv[2]; z = zv[4]; break;
        case 1: z = zv[5]; break;
        case 2: x = zv[1]; break;
        case 3: z = zv[4]; break;
        case 4: y = zv[3]; break;
        case 5: x = zv[0]; break;
        case 6: z = zv[5]; break;
        case 7: x = zv[1]; break;
        }
        r3d_rotate_point(x, y, z, alpha, beta, gamma);
        if (g_r3d_x < xmin) xmin = g_r3d_x;
        if (g_r3d_x > xmax) xmax = g_r3d_x;
        if (g_r3d_y < ymin) ymin = g_r3d_y;
        if (g_r3d_y > ymax) ymax = g_r3d_y;
        if (g_r3d_z < zmin) zmin = g_r3d_z;
        if (g_r3d_z > zmax) zmax = g_r3d_z;
    }

    zv[0] = xmin;
    zv[1] = xmax;
    zv[2] = ymin;
    zv[3] = ymax;
    zv[4] = zmin;
    zv[5] = zmax;
    object_save_zv((s16 *)a);
}

/* 0x40ba3e */
static void zv_clear(struct actor *a)
{
    memset(a->zv, 0, sizeof(a->zv));
    object_save_zv((s16 *)a);
}

/* ---- actors ------------------------------------------------------------ */

/* 0x411160 */
void actor_set_move_mode(s16 mode, s16 param)
{
    struct actor *a = g_cur_actor;

    a->track_mode = mode;
    switch ((u16)mode) {
    case TRACK_NONE:
    case TRACK_MANUAL:
        a->track_number = -1;
        a->mark = -1;
        break;
    case TRACK_FOLLOW:
        a->track_number = param;
        a->mark = -1;
        break;
    case TRACK_SCRIPT:
        a->track_number = param;
        a->position_in_track = 0;
        a->mark = -1;
        break;
    default:
        break;
    }
}

static void clamp_screen_box(struct actor *a)
{
    if (g_r3d_bbox.xmin < 0) g_r3d_bbox.xmin = 0;
    if (g_r3d_bbox.xmax > 319) g_r3d_bbox.xmax = 319;
    if (g_r3d_bbox.ymin < 0) g_r3d_bbox.ymin = 0;
    if (g_r3d_bbox.ymax > 199) g_r3d_bbox.ymax = 199;
    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
}

/* 0x40bab7 - returns the actor slot, or -1 when all 50 are taken. */
s16 actor_create(s16 body_index, s16 zv_type, s16 flags, s16 zone, s16 x,
                 s16 y, s16 z, s16 stage, s16 room, s16 alpha, s16 beta,
                 s16 gamma, s16 anim, s16 frame, s16 anim_type, s16 anim_info,
                 s16 object)
{
    struct actor *a = NULL;
    u8 *body = NULL;
    s16 i;
    int j;

    for (i = 0; i < ACTOR_MAX; i++) {
        if (g_actors[i].object == -1) {
            a = &g_actors[i];
            break;
        }
    }
    if (a == NULL) {
        return -1;
    }

    g_cur_actor = a;
    g_cur_actor_index = i;

    a->object = object;
    a->body = body_index;
    a->flags = (u16)flags;
    a->stage = stage;
    a->room = room;
    a->room_x = a->world_x = x;
    a->room_y = a->world_y = y;
    a->room_z = a->world_z = z;

    if (room != g_cur_room) {
        const u8 *r = g_floor_rooms[a->room];
        a->world_x = (s16)(a->world_x - (s16)(ROOM_X(g_room_data) - ROOM_X(r)) * 10);
        a->world_y = (s16)(a->world_y + (s16)(ROOM_Y(g_room_data) - ROOM_Y(r)) * 10);
        a->world_z = (s16)(a->world_z + (s16)(ROOM_Z(g_room_data) - ROOM_Z(r)) * 10);
    }

    a->alpha = alpha;
    a->beta = beta;
    a->gamma = gamma;
    a->dyn_flags = 1;
    a->anim = anim;
    a->frame = frame;
    a->anim_type = anim_type;
    a->anim_info = anim_info;
    a->pose_slot = -1;
    a->anim_flag = 0;
    a->end_frame = 1;
    a->end_anim = 1;
    a->new_anim = -1;
    a->new_anim_type = 0;
    a->new_anim_info = -1;
    a->step_x = a->step_y = a->step_z = 0;
    a->slide[0] = a->slide[1] = a->slide[2] = 0;
    for (j = 0; j < 3; j++) {
        a->col[j] = -1;
    }
    a->col_by = -1;
    a->hard_dec = -1;
    a->hard_col = -1;
    a->material = 255;
    a->rot[0] = a->rot[1] = a->rot[2] = 0;
    a->fall[0] = a->fall[1] = a->fall[2] = 0;
    a->falling = 0;
    a->rot_dir = 0;
    a->speed = 0;
    a->track_mode = 0;
    a->track_number = -1;
    a->anim_action_type = ACTION_NONE;
    a->hit = -1;
    a->hit_by = -1;
    a->visible = 0;

    if (!(flags & 0x200) && body_index != -1) {
        body = hqr_get(g_cache_body, a->body, NULL);

        if (anim != -1) {
            const u8 *anim_data = hqr_get(g_cache_anim, a->anim, NULL);

            anim_set_frame(frame, anim_data, body);
            a->frame_count = (s16)anim_frame_count(anim_data);
            a->end_anim = 0;
            r3d_measure_body((s16)(a->world_x + a->step_x),
                             (s16)(a->world_y + a->step_y),
                             (s16)(a->world_z + a->step_z),
                             a->alpha, a->beta, a->gamma, body);
            clamp_screen_box(a);
        } else if (!(a->flags & 4)) {
            a->flags &= ~1;
        }
    }

    timer_reset_u32(&a->room_chrono);
    timer_reset_u32(&a->chrono);

    if (OBJECT_SAVED_ZV(object)[0]) {
        object_restore_zv((s16 *)a);
        return i;
    }

    switch (zv_type) {
    case ZV_CUBE:
        if (body_index != -1) zv_cube(body, a->zv, a); else zv_clear(a);
        break;
    case ZV_BODY:
        if (body_index != -1) { zv_from_body(body, a->zv); object_save_zv((s16 *)a); }
        else zv_clear(a);
        break;
    case ZV_CENTRED:
        if (body_index != -1) { zv_from_body_centred(body, a->zv); object_save_zv((s16 *)a); }
        else zv_clear(a);
        break;
    case ZV_ROTATED:
        if (body_index != -1) zv_rotated(body, a->zv, alpha, beta, gamma, a);
        else zv_clear(a);
        break;
    case ZV_ZONE: {
        /* 0x40c06d: the box is the scene zone of type 9 tagged `zone`. */
        const u8 *r = g_floor_rooms[room];
        const u8 *sce = r + (blob_s16(r, 0) & ~1);
        int count = blob_s16(sce, 0);
        int k;

        sce += 2;
        for (k = 0; k < count; k++, sce += 16) {
            if (blob_s16(sce, 14) == HARDCOL_TAGGED && blob_s16(sce, 12) == zone) {
                break;
            }
        }
        if (k == count) {
            zv_clear(a);
            break;
        }

        for (j = 0; j < 6; j++) {
            a->zv[j] = blob_s16(sce, j * 2);
        }
        x = y = z = 0;
        a->room_x = a->world_x = (s16)(a->zv[1] / 2 + a->zv[0] / 2);
        a->room_y = a->world_y = (s16)(a->zv[2] / 2 + a->zv[3] / 2);
        a->room_z = a->world_z = (s16)(a->zv[4] / 2 + a->zv[5] / 2);
        if (room != g_cur_room) {
            const u8 *ar = g_floor_rooms[a->room];
            a->world_x = (s16)(a->world_x - (s16)(ROOM_X(g_room_data) - ROOM_X(ar)) * 10);
            a->world_y = (s16)(a->world_y + (s16)(ROOM_Y(g_room_data) - ROOM_Y(ar)) * 10);
            a->world_z = (s16)(a->world_z + (s16)(ROOM_Z(g_room_data) - ROOM_Z(ar)) * 10);
        }
        break;
    }
    default:
        break;
    }

    a->zv[0] = (s16)(a->zv[0] + x);
    a->zv[1] = (s16)(a->zv[1] + x);
    a->zv[2] = (s16)(a->zv[2] + y);
    a->zv[3] = (s16)(a->zv[3] + y);
    a->zv[4] = (s16)(a->zv[4] + z);
    a->zv[5] = (s16)(a->zv[5] + z);
    object_save_zv((s16 *)g_cur_actor);

    return i;
}

/* 0x409654 - give an actor slot back to its world object. */
void world_remove_actor(s16 index)
{
    struct actor *a = &g_actors[index];
    struct world_object *o;
    u16 flags;

    if (a->object == -2) {
        a->object = -1;
        special_release(a->frame);
        return;
    }
    if (a->object < 0) {
        return;
    }

    o = object_get(a->object);
    o->actor = -1;
    a->object = -1;

    o->body = a->body;
    o->anim = a->anim;
    o->frame = a->frame;
    o->anim_type = a->anim_type;
    o->anim_info = a->anim_info;

    flags = a->flags;
    flags &= ~8;
    o->flags = flags;
    o->flags |= (u16)(a->dyn_flags << 5);

    o->life = a->life;
    o->life_mode = (s16)(a->life_mode & 3);
    o->track_mode = a->track_mode;
    if (o->track_mode != TRACK_NONE) {
        o->track_number = a->track_number;
        o->position_in_track = a->position_in_track;
        o->mark = a->mark;
    }

    o->x = (s16)(a->step_x + a->room_x);
    o->y = (s16)(a->step_y + a->room_y);
    o->z = (s16)(a->step_z + a->room_z);
    o->alpha = a->alpha;
    o->beta = a->beta;
    o->gamma = a->gamma;
    o->stage = a->stage;
    o->room = a->room;

    g_actors_dirty = 1;
}

/*
 * 0x409bdb - drop actors that left the view, instantiate objects that came
 * into it.
 */
void world_update_objects(void)
{
    s16 i;

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];
        int keep = 0;

        if (a->object == -1) {
            continue;
        }

        a->life_mode &= ~4;

        if (a->stage == g_cur_floor) {
            switch (a->life_mode) {
            case LIFE_MODE_ALWAYS:
                keep = 1;
                break;
            case LIFE_MODE_ROOM:
                keep = (a->room == g_cur_room);
                break;
            case LIFE_MODE_VISIBLE:
                keep = world_room_is_visible(a->room);
                break;
            default:
                break;
            }
            if (!keep && a->body != -1 && world_camera_sees_room(a->room)
                && world_in_view_zone((s16)(a->step_x + a->room_x),
                                      (s16)(a->step_z + a->room_z))) {
                a->life_mode |= 4;
                keep = 1;
            }
        }

        if (!keep) {
            world_remove_actor(i);
        }
    }

    for (i = 0; i < g_object_count; i++) {
        struct world_object *o = object_get(i);
        int keep = 0;
        s16 slot;

        if (o->actor != -1) {
            if (g_player_object == i) {
                g_player_actor = o->actor;
            }
            continue;
        }
        if (o->stage != g_cur_floor) {
            continue;
        }

        switch (o->life_mode & 3) {
        case LIFE_MODE_OFF: keep = 0; break;
        case LIFE_MODE_ALWAYS: keep = 1; break;
        case LIFE_MODE_ROOM: keep = (o->room == g_cur_room); break;
        case LIFE_MODE_VISIBLE: keep = world_room_is_visible(o->room); break;
        }

        if (!keep) {
            if (o->body == -1 || !world_camera_sees_room(o->room)
                || !world_in_view_zone(o->x, o->z)) {
                continue;
            }
            o->life_mode |= 4;
        }

        slot = actor_create(o->body, o->zv_type, (s16)(o->flags & ~0x20),
                            o->found_name, o->x, o->y, o->z, o->stage,
                            o->room, o->alpha, o->beta, o->gamma, o->anim,
                            o->frame, o->anim_type, o->anim_info, i);
        o->actor = slot;

        if (slot != -1) {
            struct actor *a = &g_actors[slot];

            g_cur_actor = a;
            g_cur_actor_index = slot;
            if (g_player_object == i) {
                g_player_actor = slot;
            }
            a->dyn_flags = (s16)((o->flags & 0x20) >> 5);
            a->life = o->life;
            a->life_mode = o->life_mode;
            a->object = i;
            actor_set_move_mode(o->track_mode, o->track_number);
            a->position_in_track = o->position_in_track;
            a->mark = o->mark;
            g_actors_dirty = 1;
        }
    }

    g_objects_dirty = 0;
}

/* 0x409fd2 - actors the camera can see, in slot order. */
void world_build_actor_list(void)
{
    s16 i;

    g_visible_count = 0;

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];

        if (a->object == -2) {
            g_visible_actors[g_visible_count++] = i;
            if (world_camera_sees_room(a->room)) {
                a->visible = 1;
            }
            continue;
        }

        a->visible = 0;
        if (a->object == -1 || a->body == -1) {
            continue;
        }
        if (!world_camera_sees_room(a->room)) {
            continue;
        }
        if (!world_in_view_zone((s16)(a->step_x + a->room_x),
                                (s16)(a->step_z + a->room_z))) {
            continue;
        }

        g_visible_actors[g_visible_count] = i;
        if ((a->flags & 0x221) == 0) {
            a->flags |= 8;
            g_static_dirty = 1;
        }
        g_visible_count++;
        a->visible = 1;
    }

    g_actors_dirty = 0;
}
