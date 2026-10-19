/*
 * escape.c - the in-game options menu opened with Esc.
 *
 *   scene_escape_menu   0x408545
 *   escape_draw         0x40844b
 *   escape_draw_entry   0x4083dc
 *
 * Eight entries on ITD_RESS picture 17, 14 pixels apart from y = 44:
 * return to game, save, load, difficulty, music, sound effects, detail
 * level and quit. Returns 1 when a saved game was loaded, so the scene loop
 * skips straight to its change handling (0x408250).
 */

#include <string.h>

#include "aitd.h"
#include "game.h"
#include "frame.h"
#include "anim.h"
#include "draw.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "raster.h"
#include "savegame.h"
#include "subsystems.h"
#include "world.h"

enum escape_entry {
    ESCAPE_RETURN,
    ESCAPE_SAVE,
    ESCAPE_LOAD,
    ESCAPE_DIFFICULTY,
    ESCAPE_MUSIC,
    ESCAPE_SOUND,
    ESCAPE_DETAIL,
    ESCAPE_QUIT,
    ESCAPE_COUNT
};


static u8 *s_picture;          /* 0x444960 */
static u8 s_saved_palette[AITD_PALETTE_BYTES];

/* The framed titles (AITD1, AITD2, Jack): a frame over the whole screen
 * and seven rows, 16 apart around its middle, in their own text ids
 * (48 return, 45 save, 46 load, 41/42 music, 43 sound, 49 detail, 47
 * quit); no difficulty row. Facts worked out from the data. */
static const s16 s_framed_rows[] = {
    ESCAPE_RETURN, ESCAPE_SAVE, ESCAPE_LOAD, ESCAPE_MUSIC, ESCAPE_SOUND, ESCAPE_DETAIL, ESCAPE_QUIT
};

static int escape_framed(void)
{
    return game_current()->menu_style == MENU_STYLE_FRAMED;
}

static s16 escape_rows(void)
{
    return escape_framed() ? (s16)(sizeof(s_framed_rows) / sizeof(s_framed_rows[0])) : ESCAPE_COUNT;
}

static s16 escape_choice(s16 row)
{
    return escape_framed() ? s_framed_rows[row] : row;
}

static void escape_draw_framed(s16 selected)
{
    static const s16 texts[] = { 48, 45, 46, 41, 43, 49, 47 };
    s16 top, y;
    int i;

    frame_draw(160, 100, 320, 200);
    top = (s16)(g_window[1] + (g_window[3] - g_window[1]) / 2 - (7 * 16) / 2);
    for (i = 0; i < 7; i++) {
        s16 text = texts[i];

        if (i == 3) {
            text = (s16)(text + (g_cdaudio_ready ? 1 : 0));
        } else if (i == 4) {
            text = (s16)(text + (g_sound_ready2 ? 1 : 0));
        } else if (i == 5) {
            text = (s16)(text + (g_poly_detail ? 1 : 0));
        }
        y = (s16)(top + i * 16);
        if (i == selected) {
            text_draw_centered_shadowed(160, y, text, 15, 4);
        } else {
            text_draw_centered(160, y, text, 4);
        }
    }
    raster_reset_clip();
}

/* 0x4083dc */
static void escape_draw_entry(s16 index, s16 text, s16 selected)
{
    s16 y = (s16)(index * 14 + 44);

    if (index == selected) {
        text_draw_centered_shadowed(160, y, text, 1, 4);
    } else {
        text_draw_centered(160, y, text, 4);
    }
}

/* 0x40844b */
static void escape_draw(s16 selected)
{
    struct raster_image picture = { (u16)g_screen_width, (u16)g_screen_height, g_escape_picture };

    if (escape_framed()) {
        escape_draw_framed(selected);
        return;
    }
    raster_blit(0, 0, &picture);
    escape_draw_entry(ESCAPE_RETURN, TEXT_ID_RETURN_TO_GAME, selected);
    escape_draw_entry(ESCAPE_SAVE, TEXT_ID_SAVE_GAME, selected);
    escape_draw_entry(ESCAPE_LOAD, TEXT_ID_LOAD_GAME, selected);
    escape_draw_entry(ESCAPE_DIFFICULTY, TEXT_ID_DIFFICULTY, selected);
    escape_draw_entry(ESCAPE_MUSIC, (s16)(TEXT_ID_MUSIC_OFF + g_cdaudio_ready), selected);
    escape_draw_entry(ESCAPE_SOUND, (s16)(TEXT_ID_SOUND_OFF + g_sound_ready2), selected);
    escape_draw_entry(ESCAPE_DETAIL, (s16)(TEXT_ID_DETAIL_LOW + g_poly_detail), selected);
    escape_draw_entry(ESCAPE_QUIT, TEXT_ID_QUIT, selected);
}

static void restore_menu_palette(void)
{
    const u8 *picture = s_picture != NULL ? s_picture : g_back_buffer;

    if (escape_framed()) {
        return;     /* the game's palette stays */
    }
    memcpy(g_palette_buffer, picture + (size_t)g_screen_width * (size_t)g_screen_height, 0x300);
}

static void wait_keys_up(int with_mouse)
{
    while ((g_last_scancode & 0xff) || g_key_dir_flags || g_key_confirm
           || (with_mouse && g_mouse_buttons)) {
        input_poll();
    }
    g_scene_dir_flags = 0;
    g_scene_confirm = 0;
    g_scene_scancode = 0;
}

static void redraw(s16 selected)
{
    escape_draw(selected);
    draw_present();
}

/* 0x408545 */
int scene_escape_menu(void)
{
    s16 choice = -1;
    s16 done = 0;
    s16 fade_in = 1;
    s16 selected = 0;

    ticks_freeze();
    shaking_save();
    if (escape_framed()) {
        memcpy(s_saved_palette, g_palette_buffer, sizeof(s_saved_palette));
    }
    palette_blackout();
    if (escape_framed()) {
        memcpy(g_palette_buffer, s_saved_palette, sizeof(s_saved_palette));
    }
    memcpy(g_back_buffer, g_screen, (size_t)g_screen_width * (size_t)g_screen_height); /* 0x402c8b */
    savegame_make_thumbnail(g_back_buffer);
    s_picture = g_escape_picture;
    restore_menu_palette();

    while (!done) {
        screen_clear();
        redraw(selected);
        if (fade_in) {
            fade_in = 0;
            g_fade_state = 1;
            palette_fade(0x20, 0);
        }

        g_message_shown = 1;
        while (!done) {
            input_poll();
            g_scene_scancode = (s16)(g_last_scancode & 0xff);
            g_scene_confirm = (s16)g_key_confirm;
            g_scene_dir_flags = (s16)g_key_dir_flags;

            if (!escape_framed() && g_mouse_buttons && g_mouse_x > 100 && g_mouse_x < 220
                && g_mouse_y > 44 && g_mouse_y < 156) {
                s16 row = (s16)((g_mouse_y - 44) / 14);

                if (row != selected) {
                    selected = row;
                    redraw(selected);
                }
            }

            if (g_message_shown) {
                if (!g_scene_scancode && !g_scene_confirm && !g_scene_dir_flags
                    && !g_mouse_buttons) {
                    g_message_shown = 0;
                }
                continue;
            }
            if (g_scene_scancode == SC_ENTER || g_scene_confirm || g_mouse_buttons) {
                choice = escape_choice(selected);
                break;
            }
            if (g_scene_scancode == SC_ESC) {
                restore_menu_palette();
                choice = ESCAPE_RETURN;
                done = 1;
            }
            if (g_scene_dir_flags & DIR_UP) {
                if (--selected < 0) {
                    selected = (s16)(escape_rows() - 1);
                }
                g_message_shown = 1;
                redraw(selected);
            }
            if (g_scene_dir_flags & DIR_DOWN) {
                if (++selected == escape_rows()) {
                    selected = 0;
                }
                g_message_shown = 1;
                redraw(selected);
            }
        }

        switch (choice) {
        case ESCAPE_RETURN:
            restore_menu_palette();
            done = 1;
            break;
        case ESCAPE_SAVE:
            savegame_save();
            restore_menu_palette();
            fade_in = 1;
            choice = -1;
            done = 0;
            break;
        case ESCAPE_LOAD:
            if (savegame_restore(g_object_table, 1)) {
                g_init_view = 2;
                ticks_unfreeze();
                shaking_restore();
                return 1;
            }
            restore_menu_palette();
            fade_in = 1;
            choice = -1;
            break;
        case ESCAPE_DIFFICULTY:
            menu_difficulty_select(1);
            restore_menu_palette();
            fade_in = 1;
            choice = -1;
            break;
        case ESCAPE_MUSIC:
            music_toggle();
            choice = -1;
            break;
        case ESCAPE_SOUND:
            if (g_sound_ready) {
                g_sound_ready2 ^= 1;
                if (!g_sound_ready2) {
                    sound_stop_all();   /* 0x402e4f */
                }
                g_sample_priority = -1;
                g_sample_current = -1;
                choice = -1;
            }
            break;
        case ESCAPE_DETAIL:
            g_poly_detail ^= 1;
            choice = -1;
            break;
        case ESCAPE_QUIT:
            g_quit_requested = 1;
            wait_keys_up(1);
            ticks_unfreeze();
            restore_menu_palette();
            return 0;
        default:
            break;
        }
    }

    palette_fade_out(0, 0);
    wait_keys_up(0);
    g_init_view = 2;
    ticks_unfreeze();
    shaking_restore();
    return 0;
}
