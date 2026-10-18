/*
 * savegame.h - save files and the save / load screens, see savegame.c.
 */

#ifndef AITD_SAVEGAME_H
#define AITD_SAVEGAME_H

#include "aitd.h"

extern u8 *g_save_thumbnail;   /* 0x44497c, 80 x 50 */
extern u8 *g_save_picture;     /* 0x44436c, ITD_RESS 79 */
extern u8 g_saved_palette[0x300];         /* 0x44202c */
extern u8 g_palette_effect_state[0x316];  /* 0x444bcc */

int savegame_save(void);                   /* 0x41928f */
void savegame_make_thumbnail(const u8 *screen); /* 0x4144f8 */
int savegame_write_slot(int dir, int slot, const char *name);
int savegame_read_slot(int dir, int slot);

#endif /* AITD_SAVEGAME_H */
