/*
 * savegame.c - save files and the save / load screens.
 *
 *   save_xor          0x4162db  every state block is XOR 0x35 on disk
 *   save_block        0x416325  the block table at 0x434f80
 *   save_write_header 0x4166df  thumbnail, palette, name, difficulty
 *   save_read_header  0x416695
 *   save_write_state  0x416899
 *   save_read_state   0x4169f4
 *   save_file_write   0x416e39
 *   save_file_read    0x416d8c
 *   save_screen_run   0x418dc4 save / 0x418aaa load
 *   savegame_save     0x41928f
 *   savegame_restore  0x4192f6
 *
 * File layout. The first six dwords are big endian offsets (written byte by
 * byte by 0x420934, read back by 0x4207bf):
 *
 *   [0] thumbnail, 80 x 50 = 0xfa0 bytes
 *   [1] palette, 0x300 bytes
 *   [2] name, 0x20 bytes
 *   [3] block 0 (difficulty), then blocks 1.. back to back, XOR 0x35
 *   [4] u16 size of VARS.ITD, then the vars, XOR 0x35
 *   [5] the actor table, 0x238c bytes, plain
 *
 * The data starts at 0x14, so offset [5] overwrites the first four bytes of
 * the thumbnail; the original does the same.
 *
 * The save screens of the original are a small object system (0x2ba6 bytes
 * at 0x444974: three drives, four directories, six slots). The drive column
 * is not reproduced; the directory and slot columns, their geometry and the
 * texts are. Save names are typed in the original (0x417960); here the slot
 * gets a name built from the date.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "raster.h"
#include "savegame.h"
#include "subsystems.h"
#include "world.h"

enum {
    SAVE_DIRS = 4,
    SAVE_SLOTS = 6,
    SAVE_NAME_BYTES = 0x20,
    THUMB_W = 80,
    THUMB_H = 50,
    ACTOR_TABLE_BYTES = 0x238c
};

/* Byte offsets of the big endian section offsets at the head of a file. */
enum save_header {
    SAVE_HDR_THUMBNAIL = 0x00,
    SAVE_HDR_PALETTE = 0x04,
    SAVE_HDR_NAME = 0x08,
    SAVE_HDR_BLOCKS = 0x0c,
    SAVE_HDR_VARS = 0x10,
    SAVE_HDR_ACTORS = 0x14
};

/* Indexes into the block table at 0x434f80. */
enum save_block_id {
    SAVE_BLOCK_DIFFICULTY = 0,
    SAVE_BLOCK_OBJECTS = 7,
    SAVE_BLOCK_FROZEN_AT = 19
};

extern s16 g_palette_effect;
extern s32 g_screen_width, g_screen_height;

static s16 s_unknown_44502a;   /* 0x44502a, only referenced by the table */
u8 g_palette_effect_state[0x316];  /* 0x444bcc, opcode 100 */
u8 g_saved_palette[0x300];         /* 0x44202c */
u8 *g_save_thumbnail;              /* 0x44497c */
u8 *g_save_picture;                /* 0x44436c, ITD_RESS 79 */

struct save_block {
    void *ptr;
    u32 size;
};

/* 0x434f80. Index 7 is the object table, sized by the object count. */
static struct save_block s_blocks[] = {
    { g_difficulty, 6 },
    { &g_cur_room, 2 },
    { &g_cur_floor, 2 },
    { &g_cur_camera, 2 },
    { &g_player_object, 2 },
    { &g_player_actor, 2 },
    { &g_object_count, 2 },
    { NULL, 0 },
    { g_defines_itd, 0x8c },
    { g_in_hand, 4 },
    { g_inventory_count, 4 },
    { g_inventory, 0xc8 },
    { &g_inventory_enabled, 2 },
    { &g_inventory_index, 2 },
    { &g_quit_requested, 2 },
    { &g_black_screen, 2 },
    { &g_saved_speech_flag, 2 },
    { &s_unknown_44502a, 2 },
    { &g_ticks, 4 },
    { NULL, 4 },                    /* ticks frozen at, filled below */
    { &g_music_pending, 2 },
    { g_game_vars, 0x1fb8 },
    { &g_palette_effect, 2 },
    { g_palette_effect_state, 0x316 },
};

#define BLOCK_COUNT ((int)(sizeof(s_blocks) / sizeof(s_blocks[0])))

/* 0x4162db */
static void save_xor(void *data, u32 size)
{
    u8 *p = data;
    u32 i;

    for (i = 0; i < size; i++) {
        p[i] ^= 0x35;
    }
}

/* 0x416325 */
static void *save_block(int index, u32 *size)
{
    if (index >= BLOCK_COUNT) {
        *size = 0;
        return NULL;
    }
    if (index == SAVE_BLOCK_OBJECTS) {
        *size = (u32)g_object_count * sizeof(struct world_object);
        return g_objects;
    }
    if (index == SAVE_BLOCK_FROZEN_AT) {
        s_blocks[SAVE_BLOCK_FROZEN_AT].ptr = ticks_frozen_at();
    }
    *size = s_blocks[index].size;
    return s_blocks[index].ptr;
}

/* 0x420934 */
static void file_write_be32(FILE *fp, u32 v)
{
    u8 b[4] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };

    fwrite(b, 1, 4, fp);
}

/* 0x4207bf */
static u32 file_read_be32(FILE *fp)
{
    u8 b[4] = { 0 };

    if (fread(b, 1, 4, fp) != 4) {
        return 0;
    }
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}

static void put_offset(FILE *fp, long slot, u32 offset)
{
    fseek(fp, slot, SEEK_SET);
    file_write_be32(fp, offset);
    fseek(fp, (long)offset, SEEK_SET);
}

static int seek_offset(FILE *fp, long slot)
{
    u32 offset;

    fseek(fp, slot, SEEK_SET);
    offset = file_read_be32(fp);
    return offset != 0 && fseek(fp, (long)offset, SEEK_SET) == 0;
}

static void save_path(int dir, int slot, char *out, size_t size, int create)
{
    char dir_name[16];
    char file_name[16];

    snprintf(dir_name, sizeof(dir_name), "SAVE0%d", dir);
    snprintf(file_name, sizeof(file_name), "SAVE%d.ITD", slot);
    platform_save_path(dir_name, file_name, out, size, create);
}

/* 0x416e39: the header (0x4166df) then the state (0x416899). */
static int save_file_write(const char *path, const char *name)
{
    FILE *fp = fopen(path, "wb");
    char name_field[SAVE_NAME_BYTES];
    u8 difficulty[6];
    u32 offset = SAVE_HDR_ACTORS;
    u32 size;
    u16 vars_size = (u16)g_vars_size;
    void *p;
    int i;

    if (fp == NULL) {
        return 0;
    }
    sound_stop_all();

    put_offset(fp, SAVE_HDR_THUMBNAIL, offset);
    fwrite(g_save_thumbnail, 1, THUMB_W * THUMB_H, fp);
    offset += THUMB_W * THUMB_H;
    put_offset(fp, SAVE_HDR_PALETTE, offset);
    fwrite(g_palette_buffer, 1, 0x300, fp);
    offset += 0x300;
    put_offset(fp, SAVE_HDR_NAME, offset);
    memset(name_field, 0, sizeof(name_field));
    strncpy(name_field, name, sizeof(name_field) - 1);
    fwrite(name_field, 1, sizeof(name_field), fp);
    offset += sizeof(name_field);
    put_offset(fp, SAVE_HDR_BLOCKS, offset);
    memcpy(difficulty, g_difficulty, sizeof(difficulty));
    save_xor(difficulty, sizeof(difficulty));
    fwrite(difficulty, 1, sizeof(difficulty), fp);
    offset += sizeof(difficulty);

    for (i = 1; (p = save_block(i, &size)) != NULL; i++) {
        save_xor(p, size);
        fwrite(p, 1, size, fp);
        save_xor(p, size);
        offset += size;
    }

    put_offset(fp, SAVE_HDR_VARS, offset);
    fwrite(&vars_size, 1, 2, fp);
    save_xor(g_vars, g_vars_size);
    fwrite(g_vars, 1, g_vars_size, fp);
    save_xor(g_vars, g_vars_size);
    offset += g_vars_size + 2;

    put_offset(fp, SAVE_HDR_ACTORS, offset);
    size = (u32)fwrite(g_actors, 1, ACTOR_TABLE_BYTES, fp);
    fclose(fp);
    return size == ACTOR_TABLE_BYTES;
}

/* 0x416695 - what the slot screen shows. Any pointer may be NULL. */
static int save_read_header(const char *path, u8 *thumb, u8 *palette, char *name,
                          s16 *difficulty)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    if (thumb != NULL && seek_offset(fp, SAVE_HDR_THUMBNAIL)) {
        fread(thumb, 1, THUMB_W * THUMB_H, fp);
    }
    if (palette != NULL && seek_offset(fp, SAVE_HDR_PALETTE)) {
        fread(palette, 1, 0x300, fp);
    }
    if (name != NULL) {
        memset(name, 0, SAVE_NAME_BYTES);
        if (seek_offset(fp, SAVE_HDR_NAME)) {
            fread(name, 1, SAVE_NAME_BYTES - 1, fp);
        }
    }
    if (difficulty != NULL && seek_offset(fp, SAVE_HDR_BLOCKS)) {
        fread(difficulty, 1, 6, fp);
        save_xor(difficulty, 6);
    }
    fclose(fp);
    return 1;
}

/* 0x416d8c: opens the file, then the state reader 0x4169f4. */
static int save_file_read(const char *path)
{
    FILE *fp = fopen(path, "rb");
    s16 camera;
    s16 music;
    u16 vars_size = 0;
    u32 size;
    void *p;
    int i;

    if (fp == NULL) {
        return 0;
    }

    for (i = 0; i < ACTOR_MAX; i++) {
        if (g_actors[i].object == -2) {
            g_actors[i].object = -1;
            special_release(g_actors[i].frame);
        }
    }
    world_load_objects();   /* 0x409490 */
    world_reset_flags();    /* 0x407bae */

    if (!seek_offset(fp, SAVE_HDR_BLOCKS)) {
        fclose(fp);
        return 0;
    }
    for (i = 0; (p = save_block(i, &size)) != NULL; i++) {
        fread(p, 1, size, fp);
        save_xor(p, size);
    }

    /* 0x416ad7: the palette effect handlers are reinstalled and the clock
     * marked frozen; the escape menu unfreezes it on the way out. */
    g_ticks_frozen = 1;

    camera = g_cur_camera;
    world_set_floor(g_cur_floor);
    g_cur_camera = -1;
    world_set_room(g_cur_room);
    music = g_music_pending;
    g_music_pending = -1;
    music_play(music);

    if (seek_offset(fp, SAVE_HDR_VARS)) {
        fread(&vars_size, 1, 2, fp);
        g_vars_size = (g_vars_size & 0xffff0000u) | vars_size;
        fread(g_vars, 1, g_vars_size, fp);
        save_xor(g_vars, g_vars_size);
    }

    if (!seek_offset(fp, SAVE_HDR_ACTORS)
        || fread(g_actors, 1, ACTOR_TABLE_BYTES, fp) != ACTOR_TABLE_BYTES) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];
        u8 *body;
        const u8 *anim;

        if (a->flags & 0x20) {
            a->object = -1;
        }
        if (a->object == -1 || a->body == -1) {
            continue;
        }
        body = hqr_get(g_cache_body, a->body, NULL);
        if (a->anim == -1) {
            continue;
        }
        anim = hqr_get(g_cache_anim, a->anim, NULL);
        if (body == NULL || anim == NULL) {
            continue;
        }
        if (a->flags & 8) {
            anim_set_frame(a->frame_count - 1, anim, body);
        } else {
            anim_set_frame(a->frame, anim, body);
        }
    }

    g_new_camera = camera;
    g_fade_request = 1;
    g_actors_dirty = 1;
    g_objects_dirty = 1;
    return 1;
}

/* ---- the slot screens ------------------------------------------------- */

struct save_screen {
    int saving;
    int dir;          /* +3520 */
    int slot;         /* +722 */
    int on_slots;     /* which column has the focus */
    int exists[SAVE_SLOTS];
    char names[SAVE_SLOTS][SAVE_NAME_BYTES];
    u8 thumb[THUMB_W * THUMB_H];
    u8 palette[0x300];
    s16 difficulty[3];
};

static void scan_dir(struct save_screen *s)
{
    char path[1024];
    int i;

    for (i = 0; i < SAVE_SLOTS; i++) {
        save_path(s->dir, i, path, sizeof(path), 0);
        s->exists[i] = save_read_header(path, NULL, NULL, s->names[i], NULL);
    }
}

/* 0x417e22 */
static void save_screen_draw(struct save_screen *s)
{
    struct raster_image picture = { (u16)g_screen_width, (u16)g_screen_height, g_save_picture };
    int i;

    raster_blit(0, 0, &picture);
    text_draw_centered_shadowed(160, 2, s->saving ? TEXT_ID_SAVE_GAME : TEXT_ID_LOAD_GAME, 1, 4);

    /* 0x417531 - the directories. */
    for (i = 0; i < SAVE_DIRS; i++) {
        s16 y = (s16)(105 + 18 * i);
        u8 colour = 20;

        if (i == s->dir) {
            if (!s->on_slots) {
                colour = 1;
            }
            raster_rect(67, 130, (s16)(y - 1), (s16)(y + 15), 162);
        }
        text_draw_string(68, y, text_get_by_id(TEXT_ID_SAVE_DIR_FIRST + i), colour);
    }

    /* 0x417607 - the slots of the current directory. */
    for (i = 0; i < SAVE_SLOTS; i++) {
        s16 y = (s16)(86 + 18 * i);
        u8 colour = 20;

        if (i == s->slot && s->on_slots) {
            raster_rect(172, 310, (s16)(y - 1), (s16)(y + 15), 162);
            colour = 1;
        }
        if (s->exists[i]) {
            text_draw_string(174, y, s->names[i], colour);
        }
    }

    /* 0x417743 / 0x417a5f - the preview of the selected slot. */
    if (s->on_slots && (s->exists[s->slot] || s->saving)) {
        struct raster_image thumb = { THUMB_W, THUMB_H, s->thumb };
        const s16 *diff = s->difficulty;

        if (s->exists[s->slot]) {
            char path[1024];

            save_path(s->dir, s->slot, path, sizeof(path), 0);
            save_read_header(path, s->thumb, s->palette, NULL, s->difficulty);
        } else {
            memcpy(s->thumb, g_save_thumbnail, sizeof(s->thumb));
            diff = g_difficulty;
        }
        raster_blit(10, 21, &thumb);
        text_draw_centered(174, 25, TEXT_ID_LIFE_POINTS, 4);
        text_draw_centered(172, 39, TEXT_ID_HIT_POINTS, 4);
        text_draw_centered(174, 53, TEXT_ID_VILLAIN_POINTS, 4);
        for (i = 0; i < 3; i++) {
            text_draw_centered(280, (s16)(25 + 14 * i), (s16)(diff[i] + TEXT_ID_LEVEL_FIRST), 4);
        }
    }
    draw_present();
}

static void wait_release(void)
{
    do {
        input_poll();
    } while ((g_last_scancode & 0xff) || g_key_dir_flags || g_key_confirm || g_mouse_buttons);
}

/* 0x418dc4 (save) and 0x418aaa (load), with the column handlers 0x417f82
 * and 0x4181e3. */
static int save_screen_run(int saving)
{
    struct save_screen s;
    char path[1024];
    int result = 0;

    memset(&s, 0, sizeof(s));
    s.saving = saving;
    scan_dir(&s);
    wait_release();
    save_screen_draw(&s);
    g_fade_state = 1;
    palette_fade(0, 0);

    for (;;) {
        input_poll();
        if ((g_last_scancode & 0xff) == SC_ESC) {
            break;
        }
        if (g_key_dir_flags & DIR_UP) {
            if (s.on_slots) {
                s.slot = s.slot <= 0 ? SAVE_SLOTS - 1 : s.slot - 1;
            } else {
                s.dir = s.dir <= 0 ? SAVE_DIRS - 1 : s.dir - 1;
                scan_dir(&s);
            }
        } else if (g_key_dir_flags & DIR_DOWN) {
            if (s.on_slots) {
                s.slot = s.slot >= SAVE_SLOTS - 1 ? 0 : s.slot + 1;
            } else {
                s.dir = s.dir >= SAVE_DIRS - 1 ? 0 : s.dir + 1;
                scan_dir(&s);
            }
        } else if (g_key_dir_flags & DIR_RIGHT) {
            s.on_slots = 1;
        } else if (g_key_dir_flags & DIR_LEFT) {
            s.on_slots = 0;
        } else if ((g_last_scancode & 0xff) == SC_ENTER || g_key_confirm) {
            if (!s.on_slots) {
                s.on_slots = 1;
            } else if (saving) {
                time_t now = time(NULL);
                char name[SAVE_NAME_BYTES];

                strftime(name, sizeof(name), "%d/%m/%y %H:%M", localtime(&now));
                save_path(s.dir, s.slot, path, sizeof(path), 1);
                result = save_file_write(path, name);
                message_show(result ? TEXT_ID_GAME_SAVED : TEXT_ID_SAVE_FAILED);
                break;
            } else if (s.exists[s.slot]) {
                save_path(s.dir, s.slot, path, sizeof(path), 0);
                result = save_file_read(path);
                break;
            }
        } else {
            continue;
        }
        save_screen_draw(&s);
        wait_release();
    }
    wait_release();
    return result;
}

/* 0x41928f */
int savegame_save(void)
{
    int result;

    memcpy(g_saved_palette, g_palette_buffer, 0x300);
    result = save_screen_run(1);
    memcpy(g_palette_buffer, g_saved_palette, 0x300);
    return result;
}

/* 0x4192f6. The in-game caller has the world loaded already; the main
 * menu does not, so the drawing state game_run would set up is made here. */
int savegame_restore(void *object_table, int in_game)
{
    int result;

    (void)object_table;
    if (!in_game) {
        draw_init();
        g_present_buffer = g_screen;
    }
    memcpy(g_saved_palette, g_palette_buffer, 0x300);
    result = save_screen_run(0);
    memcpy(g_palette_buffer, g_saved_palette, 0x300);
    return result;
}

/* Harness / tooling: direct access to a slot without the screens. */
int savegame_write_slot(int dir, int slot, const char *name)
{
    char path[1024];

    save_path(dir, slot, path, sizeof(path), 1);
    return save_file_write(path, name);
}

int savegame_read_slot(int dir, int slot)
{
    char path[1024];

    save_path(dir, slot, path, sizeof(path), 0);
    return save_file_read(path);
}

/* 0x4144f8 - every fourth pixel of every fourth row. */
void savegame_make_thumbnail(const u8 *screen)
{
    int x, y;

    for (y = 0; y < THUMB_H; y++) {
        for (x = 0; x < THUMB_W; x++) {
            g_save_thumbnail[y * THUMB_W + x] = screen[y * 4 * 320 + x * 4];
        }
    }
}
