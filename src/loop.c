/*
 * loop.c - game_run (0x4077f0) and the per scene loop game_scene_loop
 * (0x407c7c).
 *
 * One pass of the scene loop is one frame:
 *   1. latch input and handle the system keys (escape menu, pause, music,
 *      sound, inventory)
 *   2. step every actor: animation, movement, track, falling, hits
 *   3. run every actor's life script
 *   4. service floor / room / camera changes
 *   5. compose and present the frame
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "game.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "subsystems.h"
#include "world.h"

/* Set-1 scancodes the loop tests for. */

/* Message ids offset by the toggle's new state (0x407dc7, 0x407e13). */

#define F12_HOLD_POLLS 5
#define F12_WINDOW 0x190  /* hundredths of a second */

s16 g_scene_scancode;    /* 0x445026 */
s16 g_scene_dir_flags;   /* 0x445028 */
s16 g_scene_confirm;     /* 0x44501e */
s16 g_message_shown;     /* 0x445022 */
s16 g_action;   /* 0x44502c */
s16 g_new_floor;         /* 0x44500c */
s16 g_new_room;          /* 0x44500a */
s16 g_inventory_enabled = 1; /* 0x435056 */
s16 g_music_pending = -1;  /* 0x4350cd */
s16 g_music_next = -1;     /* 0x4350cf */
s16 g_sample_pending = -1; /* 0x444ffa */
u8 g_frame_flag;           /* 0x435137 */
/* 0x435062 - saved and restored around both load paths; skips the closing
 * fade of game_run when set. */
s16 g_menu_state;

/* Harness hooks: stop after this many frames and dump the last one. */
int g_frame_limit;
const char *g_frame_dump_path;

static void dump_screen(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int i, count = g_screen_width * g_screen_height;

    if (fp == NULL) {
        return;
    }
    fprintf(fp, "P6\n%d %d\n255\n", g_screen_width, g_screen_height);
    for (i = 0; i < count; i++) {
        fwrite(g_palette_buffer + g_screen[i] * 3, 1, 3, fp);
    }
    fclose(fp);
}

/* 0x407bae */
void world_reset_flags(void)
{
    int i;

    g_quit_requested = 0;
    for (i = 0; i < 2; i++) {
        g_in_hand[i] = -1;
        g_inventory_count[i] = 0;
    }
    g_action = 0;
    g_inventory_index = 0;
    g_sample_current = -1;
    g_sample_pending = -1;
    g_sample_priority = -1;
    g_music_pending = -1;
    g_music_next = -1;
    g_black_screen = 0;
    g_fade_request = 0;
    g_player_object = 1;
    g_player_actor = 0;
    if (game_id() == GAME_AITD1) {
        /* DEFINES word 7 is the player object (WORLD_NUM_PERSO); word 8
         * the hero chosen (CHOOSE_PERSO): 0 Carnby, 1 Emily, each with
         * their own bodies and animations. */
        int emily = g_defines_itd[8] != 0;

        g_player_object = (s16)g_defines_itd[7];
        hqr_set_file(g_cache_body, emily ? "LISTBOD2" : "LISTBODY");
        hqr_set_file(g_cache_anim, emily ? "LISTANI2" : "LISTANIM");
    }
    g_inventory_enabled = 1;
    message_reset();
}

/* 0x407d41..0x407da5 - wait for release, then for any key. */
static void pause_game(void)
{
    ticks_freeze();

    while ((g_last_scancode & 0xff) != 0) {
        input_poll();
    }

    for (;;) {
        g_scene_scancode = (s16)(g_last_scancode & 0xff);
        if (g_scene_scancode != 0) break;
        g_scene_dir_flags = (s16)g_key_dir_flags;
        if (g_scene_dir_flags != 0) break;
        g_scene_confirm = (s16)g_key_confirm;
        if (g_scene_confirm != 0) break;
        input_poll();
    }

    ticks_unfreeze();
}

/* 0x407cac..0x407f3a. Returns non-zero to leave the loop. */
static int s_frame_number;

static int handle_input(s16 flag, int *hold_count, u32 *hold_start, int *hold_done)
{
    input_poll();

    g_scene_scancode = (s16)(g_last_scancode & 0xff);
    g_scene_dir_flags = (s16)g_key_dir_flags;
    g_scene_confirm = (s16)g_key_confirm;
    g_hqr_evicted = 0;

    if (((g_last_scancode & 0xff) != 0 || (g_scene_confirm & CONFIRM_FIRE) != 0)
        && g_redraw_state == 0) {
        if (flag == 0) {
            return 1;  /* 0x407d07: the intro ends on any key */
        }

        if (g_scene_scancode == SC_ESC && scene_escape_menu()) {
            return 2;
        }
        if (g_scene_scancode == SC_P) {
            pause_game();
        }

        if (!g_message_shown) {
            if (g_scene_scancode == SC_SEMICOLON) {
                music_toggle();
                message_show(g_cdaudio_ready + TEXT_ID_MUSIC_OFF);
                g_message_shown = 1;
            }
            if (g_scene_scancode == SC_S) {
                g_sound_ready2 ^= 1;
                g_sample_priority = -1;
                g_sample_current = -1;
                sound_stop_all();
                message_show(g_sound_ready2 + TEXT_ID_SOUND_OFF);
                g_message_shown = 1;
            }
        }

        if ((g_scene_scancode == SC_ENTER || g_scene_scancode == SC_I)
            && g_inventory_enabled) {
            inventory_open();
            if (g_change_floor) {
                return 2;
            }
        }

        if (g_scene_scancode == SC_TAB && !g_tab_locked) {
            scene_tab_action();
        }
    } else {
        g_message_shown = 0;
    }

    if (timer_get_ms() - *hold_start > F12_WINDOW) {
        *hold_count = 0;
    }
    if (g_scene_scancode == SC_F12) {
        if (!*hold_done) {
            if (*hold_count == 0) {
                *hold_start = timer_get_ms();
            }
            if (++*hold_count == F12_HOLD_POLLS) {
                int i;
                for (i = 0; i < g_object_count; i++) {
                    struct world_object *o = object_get(i);
                    if ((o->flags & 0x80) && (o->flags2 & 4)) {
                        object_take(i);   /* 0x40d8a2 */
                    }
                }
            }
            *hold_done = 1;
        }
    } else {
        *hold_done = 0;
    }

    g_action = (s16)((g_scene_confirm & CONFIRM_ACTION) ? 0x2000 : 0);

    if (g_in_hand[g_inventory_index] != -1) {
        life_run_global(g_in_hand[g_inventory_index]);  /* 0x406735 */
    }

    return 0;
}

/* 0x407f91..0x4081b9 - animation, movement and the per actor extras. */
static void step_actors(void)
{
    s16 i;

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];
        if (a->object >= 0) {
            a->col_by = -1;
            a->hit_by = -1;
            a->hit = -1;
            a->hard_dec = -1;
            a->hard_col = -1;
        }
    }

    g_frame_flag = 0;

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];
        u16 flags;

        g_cur_actor = a;
        g_cur_actor_index = i;

        if (a->object < 0) {
            continue;
        }

        flags = a->flags;
        if ((flags & 1) || (flags & 0x200)) {
            if (g_objects_dirty || g_actors_dirty) {
                actor_process(1);
            } else if (a->visible) {
                if (world_camera_sees_room(a->room)
                    && world_in_view_zone((s16)(a->room_x + a->step_x),
                                          (s16)(a->room_z + a->step_z))) {
                    actor_process(1);
                } else {
                    actor_process(1);
                    g_actors_dirty = 1;
                    a->visible = 0;
                }
            } else {
                actor_process(0);
                if (a->object != -1 && a->body != -1
                    && world_camera_sees_room(a->room)
                    && world_in_view_zone((s16)(a->room_x + a->step_x),
                                          (s16)(a->room_z + a->step_z))) {
                    g_actors_dirty = 1;
                    a->visible = 1;
                }
            }
        }

        if (flags & 0x40) {
            actor_fall();          /* 0x40f7d6 */
        }
        if (a->anim_action_type) {
            actor_anim_action();   /* 0x41317b */
        }
    }
}

/* Whether an actor's script runs this frame. Jack runs every script
 * whose mode is not -1. AITD1 does too, except while bit 4 says the actor
 * is only kept because a camera shows it (its modes 0..2 all ask to run).
 * From AITD2 on the mode must ask for it and bit 4 holds it back. */
static int life_mode_runs(s16 mode)
{
    if (game_id() == GAME_AITD1) {
        return mode != -1 && !(mode & 4);
    }
    if (game_id() == GAME_JACK) {
        return mode != -1;
    }
    return (mode & 3) && !(mode & 4);
}

/* 0x4081be..0x408240 */
static void run_lives(void)
{
    s16 i;

    if (getenv("AITD_NOLIFE") != NULL) {
        return;   /* harness only: freeze the scripts to inspect a view */
    }

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *a = &g_actors[i];

        g_cur_actor = a;
        g_cur_actor_index = i;

        if (a->object >= 0 && a->life != -1 && life_mode_runs(a->life_mode)) {
            life_run(a->life, 0);
        }
        if (g_change_floor) {
            break;
        }
    }
}

/* 0x407c7c */
void game_scene_loop(s16 flag)
{
    int hold_count = 0;
    u32 hold_start = 0;
    int hold_done = 0;

    u32 last_tick = platform_get_ticks() / 17u;

    for (;;) {
        int leave;

        /* One game tick (1/60 s) at least between frames. The scripts see
         * a collision only on a frame where the actor moved; with no tick
         * between two frames the animation gives no step, so an object
         * made pushable on one frame was dropped again on the next. The
         * machines of the time never ran a frame inside one tick. */
        while (platform_get_ticks() / 17u == last_tick) {
            platform_delay(1);
        }
        last_tick = platform_get_ticks() / 17u;

        leave = handle_input(flag, &hold_count, &hold_start, &hold_done);

        if (leave == 1) {
            break;
        }

        if (leave == 0 && !g_change_floor) {
            step_actors();
            run_lives();
            if (g_quit_requested) {
                break;
            }
        }

        /* 0x408250 */
        if (g_change_floor) {
            world_set_floor(g_new_floor);
        }

        if (g_change_room) {
            world_set_room(g_new_room);
            world_setup_camera();
            continue;
        }

        if (getenv("AITD_CAM") == NULL) {
            camera_select();   /* 0x40a57a */
        }

        if (g_init_view) {
            /* 0x408299: give the sprite actors one life pass in the new
             * camera before it is brought up. */
            s16 saved = g_cur_camera;
            s16 i;

            g_cur_camera = g_new_camera;
            for (i = 0; i < ACTOR_MAX; i++) {
                struct actor *a = &g_actors[i];

                g_cur_actor = a;
                g_cur_actor_index = i;
                if (a->object >= 0 && a->life != -1 && (a->flags & 0x200)
                    && (a->life_mode & 3) && !(a->life_mode & 4)) {
                    life_run(a->life, 0);
                    g_actors_dirty = 1;
                }
                if (g_change_floor) {
                    break;
                }
            }
            if (g_quit_requested) {
                break;
            }
            g_cur_camera = saved;
        }

        if (g_init_view) {
            world_setup_camera();
        } else {
            if (g_objects_dirty) {
                world_update_objects();
            }
            if (g_actors_dirty) {
                world_build_actor_list();
            }
        }

        draw_sort_actors();
        bganim_insert();
        if (g_static_dirty) {
            draw_static_actors();
        }
        draw_mark_overlaps();
        draw_frame(g_redraw_state);
        audio_frame();   /* 0x40b452 */

        s_frame_number++;
        if (getenv("AITD_OPEN_INVENTORY") != NULL
            && s_frame_number == atoi(getenv("AITD_OPEN_INVENTORY"))) {
            /* harness only: an empty inventory is stocked with the first
             * objects that have a name, a body and actions. */
            if (g_inventory_count[g_inventory_index] == 0) {
                s16 obj;
                for (obj = 0; obj < g_object_count && g_inventory_count[g_inventory_index] < 7; obj++) {
                    const struct world_object *o = object_get(obj);
                    if (o->found_name > 0 && o->found_body >= 0 && (o->flags2 & 0x7ff)) {
                        g_inventory[g_inventory_index][g_inventory_count[g_inventory_index]++] = obj;
                    }
                }
            }
            fprintf(stderr, "harness: inventory with %d objects\n", g_inventory_count[g_inventory_index]);
            inventory_open();
        }
        if (getenv("AITD_FOUND_OBJECT") != NULL
            && s_frame_number == atoi(getenv("AITD_FOUND_OBJECT"))) {
            /* harness only: the found dialog for the first object not
             * carried that has a name and a body. */
            s16 obj;
            for (obj = 0; obj < g_object_count; obj++) {
                const struct world_object *o = object_get(obj);
                if (o->found_name > 0 && o->found_body >= 0 && !(o->flags2 & 0x8000)) {
                    object_found(obj, 2);
                    break;
                }
            }
        }
        if (getenv("AITD_READ_DOCUMENT") != NULL) {
            int frame = 0, text = 0, type = 0;   /* harness only: frame,text,type */
            if (sscanf(getenv("AITD_READ_DOCUMENT"), "%d,%d,%d", &frame, &text, &type) == 3
                && s_frame_number == frame) {
                palette_fade_out(0x20, 0);
                read_document((s16)text, (s16)type);
                g_init_view = 2;
            }
        }
        if (getenv("AITD_TELEPORT") != NULL) {
            int frame, room, x, y, z, beta = -1;  /* harness only: frame,room,x,y,z[,beta] */
            if (sscanf(getenv("AITD_TELEPORT"), "%d,%d,%d,%d,%d,%d", &frame, &room, &x, &y, &z, &beta) >= 5
                && s_frame_number == frame) {
                struct actor *saved = g_cur_actor;
                s16 saved_index = g_cur_actor_index;

                g_cur_actor = &g_actors[g_player_actor];
                g_cur_actor_index = g_player_actor;
                actor_teleport(getenv("AITD_TELEPORT_FLOOR") != NULL ? (s16)atoi(getenv("AITD_TELEPORT_FLOOR")) : g_cur_floor, (s16)room, (s16)x, (s16)y, (s16)z);
                if (beta >= 0) {
                    g_cur_actor->beta = (s16)beta;
                }
                g_cur_actor = saved;
                g_cur_actor_index = saved_index;
            }
        }
        if (getenv("AITD_SET_GVAR") != NULL && g_vars != NULL) {
            int frame, index, value;    /* harness only: frame,index,value */
            if (sscanf(getenv("AITD_SET_GVAR"), "%d,%d,%d", &frame, &index, &value) == 3
                && s_frame_number == frame && index >= 0 && (u32)index * 2 < g_vars_size) {
                g_vars[index] = (s16)value;
            }
        }
        if (getenv("AITD_ACTORS") != NULL && (s_frame_number == atoi(getenv("AITD_ACTORS")) || (atoi(getenv("AITD_ACTORS")) < 0 && s_frame_number % -atoi(getenv("AITD_ACTORS")) == 0))) {
            int i;      /* harness only: every live actor at that frame */
            for (i = 0; i < ACTOR_MAX; i++) {
                const struct actor *a = &g_actors[i];
                if (a->object == -1) {
                    continue;
                }
                fprintf(stderr, "f%d actor %d obj=%d lm=%d tm=%d tn=%d body=%d flags=%x room=%d pos=%d,%d,%d beta=%d zv=%d,%d,%d,%d,%d,%d life=%d anim=%d screen=%d,%d,%d,%d\n",
                        s_frame_number, i, a->object, a->life_mode, a->track_mode, a->track_number, a->body, a->flags, a->room, a->room_x, a->room_y, a->room_z, a->beta,
                        a->zv[0], a->zv[1], a->zv[2], a->zv[3], a->zv[4], a->zv[5], a->life, a->anim,
                        a->screen[0], a->screen[1], a->screen[2], a->screen[3]);
            }
        }
        if (getenv("AITD_TRACE")) {
            const struct actor *p = &g_actors[g_player_actor];
            fprintf(stderr, "f%d fl=%x pl=%d obj=%d life=%d lm=%d tm=%d anim=%d fr=%d/%d sp=%d pos=%d,%d,%d step=%d,%d beta=%d keys=%d cam=%d room=%d act=%d inhand=%d\n",
                    s_frame_number, p->flags, g_player_actor, p->object, p->life, p->life_mode,
                    p->track_mode, p->anim, p->frame, p->frame_count, p->speed,
                    p->room_x, p->room_y, p->room_z, p->step_x, p->step_z, p->beta,
                    (int)g_key_dir_flags, g_cur_camera, g_cur_room, p->anim_action_type, g_in_hand[g_inventory_index]);
        }
        if (g_frame_limit > 0 && --g_frame_limit == 0) {
            if (g_frame_dump_path != NULL) {
                dump_screen(g_frame_dump_path);
            }
            g_quit_requested = 1;
        }

        if (g_quit_requested) {
            break;
        }
    }

    g_shaking = 0;   /* 0x430da4 */
    sound_stop_all();    /* 0x40b2e8 */
}

/*
 * 0x4077f0 - enter the world. (1, 0, 0) plays the intro, (0, 12, 1) starts a
 * new game: floor `mode`, room `stage`.
 */
void game_run(s16 floor, s16 room, s16 flag)
{
    int handle;
    u8 *saved_present = g_present_buffer;

    handle = file_open("DEFINES.ITD", 1);
    if (handle == 0) {
        fatal_error(0, "DEFINES.ITD");
    }
    file_read_be16(handle, g_defines_itd, (u32)game_current()->defines_words);
    file_close(handle);
    if (getenv("AITD_SET_VAR") != NULL) {
        int index, value;   /* harness only: index=value */
        if (sscanf(getenv("AITD_SET_VAR"), "%d=%d", &index, &value) == 2
            && index >= 0 && index < DEFINES_WORDS_MAX) {
            g_defines_itd[index] = (u16)value;
        }
    }

    draw_init();
    g_present_buffer = g_screen;

    world_load_objects();   /* 0x409490 */
    world_reset_flags();    /* 0x407bae */

    world_set_floor(floor);
    g_cur_camera = -1;

    world_set_room(room);
    g_new_camera = 0;
    if (getenv("AITD_CAM") != NULL) {
        g_new_camera = (s16)atoi(getenv("AITD_CAM"));   /* harness only */
    }
    g_init_view = 2;

    world_setup_camera();

    game_scene_loop(flag);
    world_free();           /* 0x4095a3 */

    if (g_menu_state == 0) {
        palette_fade_out(8, 0);
    }

    g_present_buffer = saved_present;
}
