/*
 * editor.h - the runtime editor: viewers for the data of the running
 * title, opened over the game or the main menu with F10 (see editor.c).
 */

#ifndef AITD_EDITOR_H
#define AITD_EDITOR_H

#include "aitd.h"

enum { SC_EDITOR = 0x44 };      /* F10 */

/* Called from every input poll: opens the editor when F10 is down. */
void editor_poll(void);

/* Runs the editor until F10 or Escape, then puts back the screen, the
 * palette and the 3D view it found. */
void editor_run(void);

int editor_active(void);

#endif /* AITD_EDITOR_H */
