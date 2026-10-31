/*
 * menu_styles.c - the main menus of the titles that do not use AITD3's
 * picture menu (menu.c).
 *
 * Framed (AITD1, AITD2, Jack): a frame of old style sprites around the
 * middle of a black screen and three rows, text 11..13 (new game, load,
 * quit). The selected row sits on a colour 100 bar in colour 15 over a
 * colour 4 shadow; the others are colour 4 (frames in frame.c).
 *
 * Ring (Time Gate, TG320.EX_ 0x405f6b / 0x405c64): ITD_RESS picture 1, a
 * medallion, with seven HYB icons around it, eight on DOS (positions
 * doubled in the 640 x 400 build, TG640.EX_ 0x405e2a); left and right walk the ring
 * and the selected icon is redrawn from its highlighted bank. Enter, space
 * or keypad enter pick, Escape picks quit, 3000 idle ticks give -1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "frame.h"
#include "game.h"
#include "hyb.h"
#include "life.h"
#include "menu.h"
#include "pak.h"
#include "palette.h"
#include "picture.h"
#include "platform.h"
#include "raster.h"
#include "anim.h"
#include "draw.h"
#include "savegame.h"
#include "subsystems.h"

/* The menus draw on whatever the window shows; before any scene has run
 * the raster is not set up yet. */
static void target_begin(void)
{
    if (g_raster.width == 0) {
        raster_init(g_back_buffer, g_screen_width, g_screen_height);
    }
    raster_push_target(presented_buffer());
}

static void wait_released(void)
{
    while (g_key_dir_flags != 0 || (g_last_scancode & 0xff) != 0 || g_key_confirm != 0) {
        input_poll();
    }
}

static int confirm_key(void)
{
    int sc = g_last_scancode & 0xff;

    return sc == SC_ENTER || sc == SC_SPACE || sc == 0x9c || g_key_confirm != 0;
}

/* ---- framed --------------------------------------------------------------- */

static void framed_draw(int selected)
{
    s16 y = 76;
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    memset(g_raster.target, 0, SCREEN_PIXELS);
    frame_draw(160, 100, 320, 80);
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    for (i = 0; i < 3; i++, y = (s16)(y + 16)) {
        if (i == selected) {
            raster_rect((s16)(g_window[0] + 2), (s16)(g_window[2] - 2), y, (s16)(y + 16), 100);
            text_draw_centered_shadowed(160, y, (s16)(11 + i), 15, 4);
        } else {
            text_draw_centered(160, y, (s16)(11 + i), 4);
        }
    }
    raster_pop_target();
    screen_present();
}

/* AITD2's ITD_RESS 59 is a bare 6 bit palette whose first 16 colours are
 * placeholders for the interface overlay. Jack has none; its first
 * picture's palette is taken (an assumption, not checked against the
 * original). */
static void framed_palette(void)
{
    const struct game_profile *profile = game_current();
    u8 palette[AITD_PALETTE_BYTES];
    int i;

    if (profile->menu_palette < 0) {
        return;
    }
    if (picture_load(profile->menu_palette, NULL, palette) != 1) {
        u32 size = 0;
        u8 *raw = pak_load("ITD_RESS.PAK", profile->menu_palette, &size);

        if (raw == NULL || size < AITD_PALETTE_BYTES) {
            if (raw != NULL) {
                mem_free(raw);
            }
            return;
        }
        memcpy(palette, raw, AITD_PALETTE_BYTES);
        mem_free(raw);
        if (profile->palette_source == PALETTE_CAMERA_6BIT) {
            for (i = 0; i < AITD_PALETTE_BYTES; i++) {
                palette[i] = (u8)((palette[i] * 255u) / 63u);
            }
        }
    }
    if (profile->palette_overlay != NULL) {
        for (i = 0; i < 0x30; i++) {
            palette[i] = (u8)((profile->palette_overlay[i] * 255u) / 63u);
        }
    }
    memcpy(g_palette_buffer, palette, sizeof(palette));
}

static int framed_menu(void)
{
    static const int results[3] = {
        MENU_RESULT_NEW_GAME, MENU_RESULT_LOAD_GAME, MENU_RESULT_QUIT
    };
    struct game_timer timer;
    int selected = 0;

    framed_palette();
    framed_draw(selected);
    g_fade_state = 1;
    palette_fade(0x10, 0);
    timer_reset(&timer);
    wait_released();
    while (timer_elapsed(&timer) < 3000) {
        input_poll();
        if (g_key_dir_flags & (DIR_UP | DIR_DOWN)) {
            selected = (selected + ((g_key_dir_flags & DIR_UP) ? 2 : 1)) % 3;
            framed_draw(selected);
            timer_reset(&timer);
            wait_released();
        } else if (confirm_key()) {
            wait_released();
            return results[selected];
        }
    }
    return MENU_RESULT_INTRO;
}

/* ---- ring ----------------------------------------------------------------- */

enum ring_action {
    RING_RESUME,        /* the in-game ring; new game on the title's */
    RING_SAVE,
    RING_LOAD,
    RING_SETUP,
    RING_VOLUME,
    RING_FILM,
    RING_DIFFICULTY,
    RING_QUIT
};

struct ring_entry {
    s16 x, y;
    u8 icon, selected;   /* ITD_RESS HYB banks */
    u8 action;
};

/* Windows (0x451b.. at 0x416c63): seven icons, the bottom one starts
 * Windows' volume control (0x4144c8). */
static const struct ring_entry s_ring_windows[] = {
    { 0x27, 0x01, 0x67, 0x66, RING_RESUME },
    { 0x1c, 0x34, 0x6e, 0x6f, RING_SAVE },
    { 0x2b, 0x6d, 0x5c, 0x5d, RING_LOAD },
    { 0x84, 0x96, 0x72, 0x73, RING_VOLUME },
    { 0xe3, 0x6d, 0x6c, 0x6d, RING_FILM },
    { 0xef, 0x34, 0x5e, 0x5f, RING_DIFFICULTY },
    { 0xe6, 0x01, 0x6a, 0x6b, RING_QUIT }
};

/* DOS (TIMEGATE.EXE 0x19b00, mouse boxes 0x19e40): eight, the bottom two
 * the sound card setup (0x32980) and the volume screen (0x16da0). */
static const struct ring_entry s_ring_dos[] = {
    { 0x27, 0x01, 0x67, 0x66, RING_RESUME },
    { 0x1c, 0x34, 0x6e, 0x6f, RING_SAVE },
    { 0x2b, 0x6d, 0x5c, 0x5d, RING_LOAD },
    { 0x60, 0x8f, 0x70, 0x71, RING_SETUP },
    { 0xaf, 0x8f, 0x72, 0x73, RING_VOLUME },
    { 0xe3, 0x6d, 0x6c, 0x6d, RING_FILM },
    { 0xef, 0x34, 0x5e, 0x5f, RING_DIFFICULTY },
    { 0xe6, 0x01, 0x6a, 0x6b, RING_QUIT }
};

static int ring_dos(void)
{
    return game_current()->music_source == MUSIC_TG_DOS;
}

static const struct ring_entry *ring_entries(int *count)
{
    if (ring_dos()) {
        *count = (int)(sizeof(s_ring_dos) / sizeof(s_ring_dos[0]));
        return s_ring_dos;
    }
    *count = (int)(sizeof(s_ring_windows) / sizeof(s_ring_windows[0]));
    return s_ring_windows;
}

static void ring_icon(int entry, s16 x, s16 y)
{
    u32 size = 0;
    u8 *bank = pak_load("ITD_RESS.PAK", entry, &size);

    if (bank != NULL) {
        hyb_render(0, x, y, bank);
        mem_free(bank);
    }
}

static void ring_icons(int selected)
{
    int i, count;
    const struct ring_entry *ring = ring_entries(&count);

    for (i = 0; i < count; i++) {
        ring_icon(ring[i].icon, SCALED(ring[i].x), SCALED(ring[i].y));
    }
    ring_icon(ring[selected].selected, SCALED(ring[selected].x), SCALED(ring[selected].y));
}

static void ring_draw(int selected)
{
    u8 palette[AITD_PALETTE_BYTES];

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    if (picture_load(1, g_raster.target, palette) == 1) {
        memcpy(g_palette_buffer, palette, sizeof(palette));
        if (g_fade_state != 1) {
            palette_set_range(g_palette_buffer, 0, 256);
        }
    }
    ring_icons(selected);
    raster_pop_target();
    screen_present();
}

/* TG 0x4062c3 / 0x406148: ITD_RESS picture 0x74 (three tarot cards) and
 * three HYB icons; left/right choose, Enter, space or Escape keep the
 * choice for all three difficulty words (0x452580..0x452584). */
static const struct {
    s16 x;
    u8 icon, selected;
} s_cards[3] = { { 0x29, 0x60, 0x61 }, { 0x7a, 0x62, 0x63 }, { 0xce, 0x64, 0x65 } };

static void cards_draw(int selected)
{
    u8 palette[AITD_PALETTE_BYTES];
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    if (picture_load(0x74, g_raster.target, palette) == 1) {
        memcpy(g_palette_buffer, palette, sizeof(palette));
        palette_set_range(g_palette_buffer, 0, 256);
    }
    for (i = 0; i < 3; i++) {
        ring_icon(s_cards[i].icon, SCALED(s_cards[i].x), SCALED(0x25));
    }
    ring_icon(s_cards[selected].selected, SCALED(s_cards[selected].x), SCALED(0x25));
    raster_pop_target();
    screen_present();
}

static void cards_select(void)
{
    int selected = g_difficulty[0];

    if (selected < 0 || selected > 2) {
        selected = selected < 0 ? 0 : 2;
    }
    cards_draw(selected);
    wait_released();
    for (;;) {
        input_poll();
        if (g_key_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
            selected = (selected + ((g_key_dir_flags & DIR_LEFT) ? 2 : 1)) % 3;
            cards_draw(selected);
            wait_released();
        } else if (confirm_key() || (g_last_scancode & 0xff) == SC_ESC) {
            wait_released();
            break;
        }
    }
    g_difficulty[0] = g_difficulty[1] = g_difficulty[2] = (s16)selected;
}

/* ---- the volume screen ---------------------------------------------------
 *
 * TIMEGATE.EXE 0x16da0: ITD_RESS picture 0x75, a lute over the music bar
 * (y 0x40..0x47) and a horn over the sound bar (0x76..0x7d), both x
 * 0x51..0xf0, and the cancel (0x5a, lit 0x5b) and OK (0x68, lit 0x69)
 * icons in the bottom corners. Up and down walk music, sound, buttons;
 * left and right slide the bar's knob a pixel a frame (0..155) or pick a
 * button. Enter keeps the volumes unless cancel is lit, Escape leaves.
 * The Windows build starts Windows' volume control here (TG 0x4144c8). */
enum {
    VOLUME_PICTURE = 0x75,
    VOLUME_LEFT = 0x51,
    VOLUME_RIGHT = 0xf0,
    VOLUME_SPAN = 0x9b,
    VOLUME_MUSIC_Y = 0x40,
    VOLUME_SOUND_Y = 0x76,
    VOLUME_KNOB_LIT = 0xf0,
    VOLUME_KNOB = 0xfe
};

/* 0x16d10: -1 lights no button, 0 OK, 1 cancel. */
static void volume_buttons(int lit)
{
    ring_icon(0x5a, SCALED(1), SCALED(0x9a));
    ring_icon(0x68, SCALED(0x10e), SCALED(0x9a));
    if (lit == 0) {
        ring_icon(0x69, SCALED(0x10e), SCALED(0x9a));
    } else if (lit == 1) {
        ring_icon(0x5b, SCALED(1), SCALED(0x9a));
    }
}

static void volume_knob(int value, s16 y, s16 bottom, int colour)
{
    raster_rect(SCALED((s16)(VOLUME_LEFT + value)), SCALED((s16)(VOLUME_LEFT + value + 2)),
                SCALED(y), SCALED(bottom), colour);
}

/* 0x17402 */
static void volume_draw(const u8 *picture, int row, int music, int sound, int button)
{
    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    memcpy(g_raster.target, picture, SCREEN_PIXELS);
    raster_rect(SCALED(VOLUME_LEFT), SCALED(VOLUME_RIGHT), SCALED(VOLUME_MUSIC_Y),
                SCALED(VOLUME_MUSIC_Y + 7), 0);
    raster_rect(SCALED(VOLUME_LEFT), SCALED(VOLUME_RIGHT), SCALED(VOLUME_SOUND_Y),
                SCALED(VOLUME_SOUND_Y + 7), 0);
    volume_knob(sound, VOLUME_SOUND_Y, VOLUME_SOUND_Y + 6, row == 1 ? VOLUME_KNOB_LIT : VOLUME_KNOB);
    volume_knob(music, VOLUME_MUSIC_Y, VOLUME_MUSIC_Y + 7, row == 0 ? VOLUME_KNOB_LIT : VOLUME_KNOB);
    volume_buttons(row == 2 ? button : -1);
    raster_pop_target();
    screen_present();
}

static void volume_screen(void)
{
    u8 *picture = malloc(SCREEN_PIXELS);
    int sample_volume, music_volume, music, sound, row = 0, button = 0, latch = 0;

    if (picture == NULL) {
        return;
    }
    memset(picture, 0, SCREEN_PIXELS);
    if (picture_load(VOLUME_PICTURE, picture, g_palette_buffer) == 1) {
        palette_set_range(g_palette_buffer, 0, 256);
    }
    sound_volumes(&sample_volume, &music_volume);
    sound = sample_volume * VOLUME_SPAN / SAMPLE_VOLUME_MAX;
    music = music_volume * VOLUME_SPAN / MUSIC_VOLUME_MAX;
    volume_draw(picture, row, music, sound, button);
    wait_released();
    for (;;) {
        int sc;

        input_poll();
        sc = g_last_scancode & 0xff;
        if (sc == SC_ESC) {
            break;
        }
        if (sc == SC_ENTER || sc == 0x9c) {
            if (button == 0) {      /* 0x17251 */
                sound_set_volumes(sound * SAMPLE_VOLUME_MAX / VOLUME_SPAN,
                                  music * MUSIC_VOLUME_MAX / VOLUME_SPAN);
            }
            break;
        }
        if (g_key_dir_flags & DIR_RIGHT) {
            if (row == 0 && music < VOLUME_SPAN) {
                music++;
            } else if (row == 1 && sound < VOLUME_SPAN) {
                sound++;
            } else if (row == 2) {
                button = 0;
            }
        }
        if (g_key_dir_flags & DIR_LEFT) {
            if (row == 0 && music > 0) {
                music--;
            } else if (row == 1 && sound > 0) {
                sound--;
            } else if (row == 2) {
                button = 1;
            }
        }
        if (g_key_dir_flags & (DIR_UP | DIR_DOWN)) {
            if (!latch) {
                row += (g_key_dir_flags & DIR_UP) ? (row > 0 ? -1 : 0) : (row < 2 ? 1 : 0);
            }
            latch = 1;
        } else if (g_key_dir_flags == 0) {
            latch = 0;
        }
        volume_draw(picture, row, music, sound, button);
        platform_delay(14);
    }
    wait_released();
    free(picture);
}

/* TG 0x41778f (TIMEGATE.EXE 0x29a20): new game, load, the sound screens,
 * the second intro film again (0x409835(1)), difficulty, quit; save does
 * nothing. The menu tune stops for a new game (0x417aa8) and for the
 * films, and starts again after them (0x417a3b, 0x417b6e). */
static void ring_film(void)
{
    const char *const *films = game_current()->intro_movies;

    music_play(-1);
    if (films != NULL && films[0] != NULL && films[1] != NULL) {
        movie_play(films[1]);
        g_fade_state = 0;   /* the film's palette is up: ring_draw sets its own */
    }
    music_play((s16)game_current()->menu_music);
}

/* ---- the sound card setup ------------------------------------------------
 *
 * TIMEGATE.EXE 0x32980: ITD_RESS picture 0x7b, five buttons down the left
 * (AUTO DETECT, DIGITAL, MIDI, OK, CANCEL at x 0xa, y 0x28 + 0x1e n,
 * 0x31ea0), and two raised panels (0x31d20) with sunken fields (0x31e00)
 * holding the digital device's name, port and IRQ / DMA, and the MIDI
 * device's name and port (0x32310). Up and down walk the buttons, Enter
 * presses one, Escape cancels (0x32590). DIGITAL and MIDI open a list of
 * DOS cards, then play TEST.WAV or TEST.HMP; OK writes HMISET.CFG.
 *
 * Here the cards are the host's: DIGITAL and MIDI switch the sound
 * effects and the music on and off, and play the test file when they come
 * on; AUTO DETECT switches both on; OK keeps the choice, CANCEL drops it. */
enum { SETUP_PICTURE = 0x7b, SETUP_BUTTONS = 5 };

static const char *const s_setup_labels[SETUP_BUTTONS] = {
    "AUTO DETECT", "  DIGITAL  ", "   MIDI    ", "    OK     ", "  CANCEL   "
};

/* 0x31d20: a raised panel, light top and right, dark left and bottom. */
static void setup_panel(s16 x1, s16 y1, s16 x2, s16 y2)
{
    raster_rect(SCALED((s16)(x1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(y2 - 1)), 4);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y1 + 2)), SCALED((s16)(x1 + 1)), SCALED((s16)(y2 - 1)), 7);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y2 - 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y2 - 1)), 7);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), 0xff);
    raster_line(SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y2 - 2)), 0xff);
    raster_line(SCALED(x1), SCALED(y1), SCALED(x2), SCALED(y1), 0);
    raster_line(SCALED(x1), SCALED(y2), SCALED(x2), SCALED(y2), 0);
    raster_line(SCALED(x1), SCALED(y1), SCALED(x1), SCALED(y2), 0);
    raster_line(SCALED(x2), SCALED(y1), SCALED(x2), SCALED(y2), 0);
}

/* 0x31e00: a sunken field, black inside. */
static void setup_field(s16 x1, s16 y1, s16 x2, s16 y2)
{
    raster_rect(SCALED((s16)(x1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(y2 - 1)), 0);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y1 + 2)), SCALED((s16)(x1 + 1)), SCALED((s16)(y2 - 1)), 0xff);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y2 - 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y2 - 1)), 0xff);
    raster_line(SCALED((s16)(x1 + 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), 7);
    raster_line(SCALED((s16)(x2 - 1)), SCALED((s16)(y1 + 1)), SCALED((s16)(x2 - 1)), SCALED((s16)(y2 - 2)), 7);
}

/* 0x322c0: the x that centres a text of `len` 8 pixel cells in x1..x2. */
static s16 setup_centre(const char *text, s16 x1, s16 x2)
{
    int len = (int)strlen(text);

    return (s16)(x1 + (x2 - x1) / 2 - ((len + 1) / 2) * 8);
}

/* 0x31ea0 (drawn) and 0x320d0 (lit): a button, sunken while lit. */
static void setup_button(int index, int lit)
{
    const char *label = s_setup_labels[index];
    s16 x = 0xa, y = (s16)(0x28 + 0x1e * index);
    s16 x2 = (s16)(x + (s16)strlen(label) * 8 + 0xe), y2 = (s16)(y + 0xc);

    raster_rect(SCALED((s16)(x - 1)), SCALED((s16)(x2 + 1)), SCALED((s16)(y - 1)), SCALED((s16)(y2 + 1)), 0);
    if (!lit) {
        raster_rect(SCALED(x), SCALED(x2), SCALED(y), SCALED(y2), 6);
        raster_line(SCALED(x), SCALED((s16)(y + 1)), SCALED(x), SCALED(y2), 7);
        raster_line(SCALED(x), SCALED(y2), SCALED(x2), SCALED(y2), 7);
        raster_line(SCALED(x), SCALED(y), SCALED(x2), SCALED(y), 0xff);
        raster_line(SCALED(x2), SCALED(y), SCALED(x2), SCALED((s16)(y2 - 1)), 0xff);
        text_draw_string(SCALED((s16)(x + 8)), SCALED((s16)(y + 0xb)), label, 0xff);
    } else {
        raster_rect(SCALED(x), SCALED((s16)(x2 - 1)), SCALED((s16)(y + 1)), SCALED(y2), 1);
        raster_line(SCALED(x), SCALED((s16)(y2 + 1)), SCALED(x2), SCALED((s16)(y2 + 1)), 7);
        raster_line(SCALED(x2), SCALED(y), SCALED(x2), SCALED((s16)(y2 + 1)), 7);
        text_draw_string(SCALED((s16)(x + 7)), SCALED((s16)(y + 0xc)), label, 0xff);
    }
}

static void setup_text(const char *text, s16 x1, s16 x2, s16 y)
{
    text_draw_string(SCALED(setup_centre(text, x1, x2)), SCALED(y), text, 0xff);
}

static void setup_draw(const u8 *picture, int selected, int digital, int midi)
{
    int i;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    memcpy(g_raster.target, picture, SCREEN_PIXELS);
    for (i = 0; i < SETUP_BUTTONS; i++) {
        setup_button(i, i == selected);
    }
    setup_panel(0x78, 0x28, 0x13b, 0x6e);
    setup_field(0x7d, 0x30, 0x136, 0x3e);
    setup_field(0xa5, 0x44, 0x10e, 0x52);
    setup_field(0xaa, 0x58, 0x109, 0x66);
    setup_text(digital ? "Digital audio" : "No Sound", 0x7d, 0x136, 0x3c);
    setup_text(digital ? "Host mixer" : "", 0xa5, 0x10e, 0x50);
    setup_text(digital ? "10 channels" : "", 0xaa, 0x109, 0x64);
    setup_panel(0x78, 0x78, 0x13b, 0xaa);
    setup_field(0x7d, 0x80, 0x136, 0x8e);
    setup_field(0xa5, 0x94, 0x10e, 0xa2);
    setup_text(midi ? "General MIDI" : "No Sound", 0x7d, 0x136, 0x8c);
    setup_text(midi ? "Host synth" : "", 0xa5, 0x10e, 0xa0);
    raster_pop_target();
    screen_present();
}

static void ring_setup(void)
{
    u8 *picture = malloc(SCREEN_PIXELS);
    int selected = 0, latch = 0, done = 0;
    int digital = g_sound_ready2 != 0, midi = music_enabled();
    int was_digital = digital, was_midi = midi;

    if (picture == NULL) {
        return;
    }
    memset(picture, 0, SCREEN_PIXELS);
    if (picture_load(SETUP_PICTURE, picture, g_palette_buffer) == 1) {
        palette_set_range(g_palette_buffer, 0, 256);
    }
    setup_draw(picture, selected, digital, midi);
    wait_released();
    while (!done) {
        int sc;

        input_poll();
        sc = g_last_scancode & 0xff;
        if (sc == SC_ESC) {
            selected = 4;
            break;
        }
        if (g_key_dir_flags & (DIR_UP | DIR_DOWN)) {
            if (!latch) {
                selected += (g_key_dir_flags & DIR_UP) ? (selected > 0 ? -1 : 0)
                                                       : (selected < SETUP_BUTTONS - 1 ? 1 : 0);
                setup_draw(picture, selected, digital, midi);
            }
            latch = 1;
        } else if (g_key_dir_flags == 0) {
            latch = 0;
        }
        if (sc == SC_ENTER || sc == 0x9c) {
            switch (selected) {
            case 0:
                if (!digital) sound_test("TEST.WAV");
                digital = midi = 1;
                break;
            case 1:
                digital ^= 1;
                if (digital) sound_test("TEST.WAV");
                break;
            case 2:
                midi ^= 1;
                music_test(midi ? "TEST.HMP" : NULL);
                break;
            default:
                done = 1;
                break;
            }
            setup_draw(picture, selected, digital, midi);
            wait_released();
        }
        platform_delay(14);
    }
    music_test(NULL);
    if (selected == 3) {
        sound_set_enabled(digital, midi);
    } else {
        sound_set_enabled(was_digital, was_midi);
    }
    wait_released();
    free(picture);
}

/* Enter or keypad Enter on DOS (0x1a25a); the Windows build also takes
 * space. */
static int ring_confirm(void)
{
    int sc = g_last_scancode & 0xff;

    return ring_dos() ? (sc == SC_ENTER || sc == 0x9c) : confirm_key();
}

static int ring_menu(void)
{
    struct game_timer timer;
    int selected = 0, count;
    const struct ring_entry *ring = ring_entries(&count);

    ring_draw(selected);
    g_fade_state = 1;
    palette_fade(0x10, 0);
    timer_reset(&timer);
    wait_released();
    for (;;) {
        if (timer_elapsed(&timer) >= 3000) {
            music_play(-1);
            return MENU_RESULT_INTRO;
        }
        input_poll();
        if (g_key_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
            selected = (selected + ((g_key_dir_flags & DIR_LEFT) ? count - 1 : 1)) % count;
            ring_draw(selected);
            timer_reset(&timer);
            wait_released();
        } else if ((g_last_scancode & 0xff) == SC_ESC) {
            wait_released();
            return MENU_RESULT_QUIT;
        } else if (ring_confirm()) {
            wait_released();
            switch (ring[selected].action) {
            case RING_RESUME:
                music_play(-1);
                return MENU_RESULT_NEW_GAME;
            case RING_LOAD:
                return MENU_RESULT_LOAD_GAME;
            case RING_QUIT:
                return MENU_RESULT_QUIT;
            case RING_FILM:
                ring_film();
                break;
            case RING_SETUP:
                ring_setup();
                break;
            case RING_VOLUME:
                volume_screen();
                break;
            case RING_DIFFICULTY:
                cards_select();
                break;
            default:
                break;
            }
            ring_draw(selected);
            timer_reset(&timer);
        }
    }
}

/* TG 0x409397: the same ring in game, over the scene shrunk to half size
 * (0x4090cd) and remapped into the picture's palette. Returns 1 when a
 * saved game was loaded. The game's sounds stop and the menu tune plays
 * (0x4093a5..0x4093ca); a save or a load closes the menu (0x409676,
 * 0x4096ea), and on the way out the game's tune starts again
 * (0x41f3e0, 0x41f346). */
static int s_ring_fade_in;     /* drawn under a black palette, faded in after */

static void ring_scene_draw(int selected, const u8 *scene, const u8 *scene_palette)
{
    u8 palette[AITD_PALETTE_BYTES], map[256];
    int i, x, y, w = g_screen_width / 2, h = g_screen_height / 2;

    target_begin();
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
    if (picture_load(1, g_raster.target, palette) == 1) {
        memcpy(g_palette_buffer, palette, sizeof(palette));
    }
    if (!ring_dos()) {
        for (i = 0; i < 256; i++) {
            const u8 *c = scene_palette + i * 3;
            int k, best = 16, best_d = 1 << 30;

            for (k = 16; k < 144; k++) {
                const u8 *t = g_palette_buffer + k * 3;
                int d = (c[0] - t[0]) * (c[0] - t[0]) + (c[1] - t[1]) * (c[1] - t[1])
                      + (c[2] - t[2]) * (c[2] - t[2]);
                if (d < best_d) {
                    best_d = d;
                    best = k;
                }
            }
            map[i] = (u8)best;
        }
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                g_raster.target[(y + h / 2) * g_screen_width + x + w / 2]
                    = map[scene[(y * 2) * g_screen_width + x * 2]];
            }
        }
    }
    ring_icons(selected);
    raster_pop_target();
    if (!s_ring_fade_in) {
        palette_set_range(g_palette_buffer, 0, 256);
    }
    screen_present();
}

int menu_ring_options(void)
{
    u8 *scene = malloc(SCREEN_PIXELS);
    u8 scene_palette[AITD_PALETTE_BYTES];
    int selected = 0, loaded = 0, count;
    const struct ring_entry *ring = ring_entries(&count);
    s16 game_music;

    if (scene == NULL) {
        return 0;
    }
    memcpy(scene, g_screen, SCREEN_PIXELS);
    memcpy(scene_palette, g_palette_buffer, sizeof(scene_palette));
    savegame_make_thumbnail(scene);     /* 0x19fd6 */
    ticks_freeze();
    game_music = music_current();
    sound_stop_all();
    if (ring_dos()) {
        /* 0x19f96: the music stops, the ring is silent. The original then
         * shrinks the scene into the middle of the screen over ten
         * frames (0x1a02b); that is left out, the menu opens on the ring. */
        music_play(-1);
        palette_blackout();
        s_ring_fade_in = 1;
        ring_scene_draw(selected, scene, scene_palette);
        s_ring_fade_in = 0;
        g_fade_state = 1;
        palette_fade(0x40, 0);
    } else {
        music_play((s16)game_current()->menu_music);
        ring_scene_draw(selected, scene, scene_palette);
    }
    wait_released();
    for (;;) {
        input_poll();
        if (g_key_dir_flags & (DIR_LEFT | DIR_RIGHT)) {
            selected = (selected + ((g_key_dir_flags & DIR_LEFT) ? count - 1 : 1)) % count;
            ring_scene_draw(selected, scene, scene_palette);
            wait_released();
        } else if ((g_last_scancode & 0xff) == SC_ESC) {
            wait_released();
            break;
        } else if (ring_confirm()) {
            int action = ring[selected].action;

            wait_released();
            if (action == RING_RESUME) {
                break;
            } else if (action == RING_SAVE) {
                if (savegame_save()) {
                    break;
                }
            } else if (action == RING_LOAD) {
                if (savegame_restore(NULL, 1)) {
                    loaded = 1;
                    break;
                }
            } else if (action == RING_SETUP) {
                ring_setup();
            } else if (action == RING_VOLUME) {
                volume_screen();
            } else if (action == RING_FILM) {
                ring_film();
                if (ring_dos()) {
                    music_play(-1);
                }
            } else if (action == RING_DIFFICULTY) {
                cards_select();
            } else if (action == RING_QUIT) {
                g_quit_requested = 1;
                break;
            }
            ring_scene_draw(selected, scene, scene_palette);
        }
    }
    if (ring_dos() && !loaded) {
        palette_fade_out(0x20, 0);      /* 0x1a36e */
    }
    if (!loaded) {      /* a loaded game has started its own tune */
        music_play(-1);
        if (!g_quit_requested) {
            music_play(game_music);
        }
    }
    memcpy(g_palette_buffer, scene_palette, sizeof(scene_palette));
    free(scene);
    ticks_unfreeze();
    g_init_view = 2;
    return loaded;
}

/* Runs the title's own menu; returns -2 for AITD3's, which menu.c draws. */
int menu_style_run(void)
{
    switch (game_current()->menu_style) {
    case MENU_STYLE_NONE:
        return MENU_RESULT_NEW_GAME;
    case MENU_STYLE_FRAMED:
        return framed_menu();
    case MENU_STYLE_RING:
        return ring_menu();
    default:
        return -2;
    }
}
