/*
 * menu_backend.c - menu drawing behind main_menu_run.
 *
 * These sit between the reconstructed menu loop and the platform layer. The
 * original routines are named in the comments; where the engine's own units
 * or pixel layout are not yet recovered, the choice made here is stated.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "font.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "platform.h"
#include "raster.h"
#include "life.h"
#include "subsystems.h"

/* Read off menu_draw_entry (0x408bdd). */
#define MENU_ENTRY_COUNT_DRAW 4
#define MENU_FIRST_ROW_Y 79      /* 0x4f */
#define MENU_ROW_PITCH 14        /* 0x0e */
#define MENU_TEXT_X 160          /* 0xa0, centred */
#define MENU_TEXT_ID_FIRST 11    /* 0x0b, id = index + 0x0b */
#define MENU_COLOUR_SELECTED 1
#define MENU_COLOUR_NORMAL 4

/* ---- drawing ----------------------------------------------------------- */


u8 g_palette_buffer[AITD_PALETTE_BYTES];  /* 0x44448c */
/*
 * 0x435068 - static data in the binary, read straight out of the image.
 * Eight bit per channel, like every other palette here.
 */
u8 g_palette_overlay[0x30] = {
    0x00, 0x00, 0x00, 0xfc, 0xfc, 0xfc,
    0x30, 0x30, 0x38, 0xc0, 0xbc, 0xfc,
    0x78, 0x58, 0x3c, 0x00, 0x00, 0x00,
    0xf0, 0x70, 0x10, 0xfc, 0xfc, 0xfc,
    0x48, 0x50, 0x60, 0xc4, 0x54, 0x5c,
    0x54, 0x94, 0x54, 0x54, 0xbc, 0xfc,
    0xfc, 0x88, 0x54, 0xac, 0x54, 0xfc,
    0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xf8,
};

/*
 * 0x408eb8
 *
 * menu_draw_backdrop paints the image; the rest of the routine is palette
 * work. Entry 0x2f is a bare 768 byte palette, not a picture: the original
 * loads it into the front buffer purely as scratch, copies it into
 * g_palette_buffer, then overwrites the first 16 colours from
 * g_palette_overlay before realising it.
 */
void menu_draw_background(void)
{
    u8 *palette = g_back_buffer
                + (size_t)g_screen_width * (size_t)g_screen_height;

    palette_blackout();  /* 0x408ed0 */

    /* 0x408ee5: scratch-load through the front buffer, as the original does. */
    if (pak_load_into("itd_ress.pak", ITD_RESS_MENU_PALETTE,
                      g_front_buffer, AITD_PALETTE_BYTES)) {
        memcpy(g_palette_buffer, g_front_buffer, AITD_PALETTE_BYTES); /* 0x408ef6 */
    }

    /* 0x408f0d: the first 16 entries come from the overlay, not the file. */
    memcpy(g_palette_buffer, g_palette_overlay, sizeof(g_palette_overlay));

    memcpy(palette, g_palette_buffer, AITD_PALETTE_BYTES);
    memset(g_back_buffer, 0,
           (size_t)g_screen_width * (size_t)g_screen_height);

    /*
     * 0x408f12: the original only pushes the palette straight away when
     * 0x435062 is set. In the normal path it stays black here and
     * palette_fade raises it a moment later.
     */
}

/* The menus draw on whatever the window shows. */
static void menu_target_begin(void)
{
    if (g_raster.width == 0) {
        raster_init(g_back_buffer, g_screen_width, g_screen_height);
    }
    raster_push_target(presented_buffer());
}

/*
 * 0x408bdd
 *
 * ITD_RESS picture 13 as the backdrop, then four entries, text ids 11..14
 * (index + 0x0b), centred on x = 160 from y = 79 with a 14 pixel pitch. The
 * selected row is drawn shadowed one pixel lower. The menu is composed in
 * the back buffer, which is what the menu presents.
 */
void menu_draw_entry(s16 index)
{
    struct raster_image picture = { (u16)g_screen_width, (u16)g_screen_height, g_menu_picture };
    int i;

    menu_target_begin();
    raster_blit(0, 0, &picture);

    for (i = 0; i < MENU_ENTRY_COUNT_DRAW; i++) {
        s16 y = (s16)(MENU_FIRST_ROW_Y + i * MENU_ROW_PITCH);
        s16 text = (s16)(MENU_TEXT_ID_FIRST + i);

        if (i == index) {
            text_draw_centered_shadowed(MENU_TEXT_X, (s16)(y + 1), text,
                                        MENU_COLOUR_SELECTED, MENU_COLOUR_NORMAL);
        } else {
            text_draw_centered(MENU_TEXT_X, y, text, MENU_COLOUR_NORMAL);
        }
    }
    raster_pop_target();
}

/* 0x402b02 - a click/keypress acknowledgement in the original. */
void menu_entry_feedback(void)
{
}


/* 0x408c99 - the three difficulty rows (text 80..82 with the level name
 * 93 + level), then the presets 83..85 and "previous menu" 86. */
static void difficulty_draw(s16 selected)
{
    struct raster_image picture = { (u16)g_screen_width, (u16)g_screen_height, g_menu_picture };
    s16 y = 48;
    s16 i;

    menu_target_begin();
    raster_blit(0, 0, &picture);
    for (i = 0; i < 3; i++, y = (s16)(y + 14)) {
        s16 level = (s16)(TEXT_ID_LEVEL_FIRST + g_difficulty[i]);

        if (i == selected) {
            text_draw_centered_shadowed(120, (s16)(y + 1), (s16)(TEXT_ID_LIFE_POINTS + i), 1, 4);
            text_draw_centered_shadowed(240, (s16)(y + 1), level, 1, 4);
        } else {
            text_draw_centered(120, y, (s16)(TEXT_ID_LIFE_POINTS + i), 4);
            text_draw_centered(240, y, level, 4);
        }
    }
    for (i = 3; i < 7; i++) {
        s16 row_y = (s16)(92 + (i - 3) * 14);
        s16 text = (s16)(TEXT_ID_PRESET_FIRST + i - 3);

        if (i == selected) {
            text_draw_centered_shadowed(160, (s16)(row_y + 1), text, 1, 4);
        } else {
            text_draw_centered(160, row_y, text, 4);
        }
    }
    raster_pop_target();
}

static int pressed(void)
{
    return (g_last_scancode & 0xff) == SC_ENTER || g_key_confirm || g_mouse_buttons;
}

static void wait_dir_release(void)
{
    while (g_key_dir_flags) {
        input_poll();
    }
}

/* 0x409107 - quiet = 1 from the escape menu: no fade out at the end. */
void menu_difficulty_select(s16 quiet)
{
    static const s16 presets[3][3] = { { 2, 2, 0 }, { 1, 1, 1 }, { 0, 0, 2 } };
    s16 selected = 0;
    int done = 0;

    menu_draw_background();
    screen_clear();
    difficulty_draw(0);
    screen_present();
    g_fade_state = 1;
    palette_fade(0x10, 0);

    while (!done) {
        input_poll();
        if (g_mouse_buttons && g_mouse_x > 50 && g_mouse_x < 270
            && g_mouse_y > 48 && g_mouse_y < 146) {
            s16 row = (s16)((g_mouse_y - 48) / 14);

            if (row != selected) {
                selected = row;
                difficulty_draw(selected);
                menu_entry_feedback();
            }
        }
        if ((g_key_dir_flags & DIR_LEFT) && selected < 3) {
            if (--g_difficulty[selected] < 0) {
                g_difficulty[selected] = 2;
            }
            difficulty_draw(selected);
            menu_entry_feedback();
            wait_dir_release();
        }
        if (((g_key_dir_flags & DIR_RIGHT) || g_mouse_buttons) && selected < 3) {
            if (++g_difficulty[selected] > 2) {
                g_difficulty[selected] = 0;
            }
            difficulty_draw(selected);
            menu_entry_feedback();
            while (g_key_dir_flags || g_mouse_buttons) {
                input_poll();
            }
        }
        if (g_key_dir_flags & DIR_UP) {
            if (--selected < 0) {
                selected = 6;
            }
            difficulty_draw(selected);
            menu_entry_feedback();
            wait_dir_release();
        }
        if (g_key_dir_flags & DIR_DOWN) {
            if (++selected > 6) {
                selected = 0;
            }
            difficulty_draw(selected);
            menu_entry_feedback();
            wait_dir_release();
        }
        if (pressed() && selected >= 3 && selected <= 5) {
            memcpy(g_difficulty, presets[selected - 3], sizeof(presets[0]));
            difficulty_draw(selected);
            menu_entry_feedback();
        }
        if (pressed() && selected == 6) {
            while ((g_last_scancode & 0xff) || g_key_confirm || g_mouse_buttons) {
                input_poll();
            }
            done = 1;
        }
    }
    if (!quiet) {
        palette_fade_out(0x10, 0);
    }
}
