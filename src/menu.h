/*
 * menu.h - main_menu_run (0x408f34) and its input/timer dependencies.
 */

#ifndef AITD_MENU_H
#define AITD_MENU_H

#include "aitd.h"

/* Timer record the menu keeps on its stack (var_10 at 0x408f85). The original
 * layout is not recovered; only reset/elapsed semantics are. */
struct game_timer {
    u32 start;
};

void timer_reset(struct game_timer *timer);    /* 0x402913 */
u32 timer_elapsed(struct game_timer *timer);   /* 0x402945 */

/* PC set 1 scancodes as left in g_last_scancode by input_poll. */
enum scancode {
    SC_ESC = 0x01,
    SC_TAB = 0x0f,
    SC_I = 0x17,
    SC_P = 0x19,
    SC_ENTER = 0x1c,
    SC_CTRL = 0x1d,
    SC_S = 0x1f,
    SC_SEMICOLON = 0x27,
    SC_SPACE = 0x39,
    SC_F12 = 0x58,
    SC_UP = 0xc8,
    SC_LEFT = 0xcb,
    SC_RIGHT = 0xcd,
    SC_DOWN = 0xd0
};

/* Bits of g_key_dir_flags. */
enum dir_flag {
    DIR_UP = 1,
    DIR_DOWN = 2,
    DIR_LEFT = 4,
    DIR_RIGHT = 8
};

/* Bits of g_key_confirm. */
enum confirm_flag {
    CONFIRM_ACTION = 1,   /* space */
    CONFIRM_FIRE = 2      /* ctrl */
};

void input_poll(void);                         /* 0x402c67 */
void timer_reset_u32(u32 *timer);
u32 timer_elapsed_u32(const u32 *timer);
u32 timer_get_ms(void);                        /* 0x41d019, 1/100 s */
extern u8 *g_present_buffer;
u8 *presented_buffer(void);

void menu_draw_background(void);               /* 0x408eb8 */
void menu_draw_entry(s16 index);               /* 0x408bdd */
void menu_entry_feedback(void);                /* 0x402b02 */
int menu_style_run(void);                      /* menu_styles.c, -2 for AITD3 */

extern s16 g_mouse_buttons;   /* 0x445c7a */
extern s16 g_mouse_x;         /* 0x445c7c */
extern s16 g_mouse_y;         /* 0x445c7e */
extern s32 g_key_dir_flags;   /* 0x444bc8 */
extern s32 g_key_confirm;     /* 0x444bc4 */
extern s16 g_last_scancode;   /* 0x43516a */

#endif /* AITD_MENU_H */
