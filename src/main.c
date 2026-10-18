/*
 * main.c - process entry and the outer menu dispatch loop.
 *
 * Original chain:
 *   start        0x425bce  stores &WinMain in 0x44b2bc, jumps to the CRT startup
 *   WinMain      0x41e9e1  Win32 plumbing, then tail calls game_main
 *   game_main    0x4078b7  the actual entry point of the game
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "life.h"
#include "raster.h"
#include "savegame.h"
#include "platform.h"
#include "harness.h"
#include "game.h"
#include "palette.h"
#include "subsystems.h"
#include "world.h"


/* Difficulty settings, set to 1 (normal) at 0x407901..0x407913. The first
 * scales the player's life points, the other two the damage dealt and
 * taken (0x413296). */

/* Saved and restored around the load paths (0x4079f1, 0x407a72, 0x407aa0). */

/*
 * 0x407923..0x407996
 *
 * Options are accepted with either a '-' or a '/' lead-in and the option
 * letter is lowercased before the compare, so "-C", "/c", "-c" all match.
 * Both known switches disable a subsystem rather than enable one.
 */
static void parse_command_line(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {          /* 0x40791c: index starts at 1 */
        const char *arg = argv[i];
        int letter;

        if (arg[0] != '-' && arg[0] != '/') {  /* 0x407942, 0x407953 */
            continue;
        }

        letter = tolower((unsigned char)arg[1]);  /* 0x407968 */

        switch (letter) {
        case 'c':                          /* 0x407986 */
            g_opt_cdaudio_enabled = 0;
            break;
        case 's':                          /* 0x40798f */
            g_opt_sound_enabled = 0;
            break;
        default:
            break;
        }
    }
}

/*
 * 0x407a16..0x407a62
 *
 * A fresh game wipes the variable block, resets the object system, then hands
 * control to game_run with stage 0x0c. game_run only returns when the player
 * leaves the game; if that was not an explicit quit the process terminates
 * with -1, exactly as the original does at 0x407a58.
 */
static void start_new_game(void)
{
    screen_clear();                     /* 0x4201ec */
    screen_present();                             /* 0x41c7cf */
    memset(g_game_vars, 0, sizeof(g_game_vars)); /* 0x407a20 */
    actor_table_reset(0);                      /* 0x40ad85 */

    game_run(0, 0x0c, 1);                      /* 0x407a44 */

    if (!g_quit_requested) {                   /* 0x407a49 */
        engine_shutdown();
        exit(-1);
    }
}

/*
 * 0x407a72..0x407af4
 *
 * The slot name built at 0x407a7b is passed to the save reader; a non zero
 * result means the snapshot was accepted and the world can be re-entered.
 */
static void load_saved_game(void)
{
    s16 menu_state = g_menu_state;  /* stashed at 0x407a72 */

    memcpy(g_saved_palette, g_palette_buffer, 0x300); /* 0x407a87 */

    if (savegame_restore(g_object_table, 0)) { /* 0x4192f6 */
        g_menu_state = menu_state;             /* 0x407aa3 */
        shaking_restore();                 /* 0x40b5d6 */
        g_init_view = 2;                       /* 0x4350b9 */
        game_scene_loop(1);                    /* 0x407c7c */
        world_free();                          /* 0x4095a3 */
        palette_fade_out(8, 0);                /* 0x40b042 */

        if (!g_quit_requested) {               /* 0x407ad2 */
            engine_shutdown();
            exit(-1);
        }
    }
    g_menu_state = 0;                          /* 0x407aeb */
}

/*
 * 0x407afd..0x407b11 - the only clean exit out of the dispatch loop.
 */
static void quit_to_desktop(void)
{
    screen_clear();  /* 0x4201ec */
    screen_present();          /* 0x41c7cf */
    engine_shutdown();      /* 0x407687 */
    exit(-1);               /* 0x407b0c */
}

/*
 * 0x4078b7 - game_main
 *
 * Watcom passed argc in eax and argv in edx. The loop at 0x4079bd never
 * terminates on its own: every branch either re-enters the menu or exits the
 * process.
 */
void game_main(int argc, char **argv)
{
    if (sys_is_already_running()) {  /* 0x4078dd, FindWindowA */
        exit(1);                     /* 0x4078e6 */
    }

    memset(g_game_vars, 0, sizeof(g_game_vars));  /* 0x4078f0 */
    g_difficulty[0] = 1;
    g_difficulty[1] = 1;
    g_difficulty[2] = 1;

    parse_command_line(argc, argv);

    engine_init();  /* 0x407998 */

    g_transition_flag = 1;   /* 0x40799d */
    game_run(1, 0, 0);       /* 0x4079af - intro */

    for (;;) {
        s16 choice;

        g_menu_state = 0;             /* 0x4079b4 */
        choice = (s16)main_menu_run(); /* 0x4079bd */

        switch (choice) {
        case MENU_RESULT_INTRO:       /* 0x4079cc */
            g_transition_flag = 1;
            game_run(1, 0, 0);
            break;

        case MENU_RESULT_DIFFICULTY:    /* 0x4079ea */
            menu_difficulty_select(0);  /* 0x4079fc */
            g_menu_state = 0;
            break;

        case MENU_RESULT_NEW_GAME:    /* 0x407a0f */
            start_new_game();
            break;

        case MENU_RESULT_LOAD_GAME:   /* 0x407a67 */
            load_saved_game();
            break;

        case MENU_RESULT_QUIT:        /* 0x407af6 */
            quit_to_desktop();
            break;

        default:
            break;
        }
    }
}

/*
 * Host entry point. The Win32 build reached game_main through WinMain
 * (0x41e9e1), which reserved a 120 slot message queue, recorded the instance
 * handle and split the command line with sscanf into seven fixed 0x80 byte
 * buffers at 0x445728..0x445a28. On a POSIX host argc/argv arrive already
 * split, so that whole stage collapses away.
 */
int main(int argc, char **argv)
{
    /* Staged bring-up driver; a plain run falls through to game_main. */
    if (harness_run(argc, argv)) {
        return 0;
    }

    game_detect();   /* host side: which title the data directory holds */
    game_main(argc, argv);
    return 0;
}
