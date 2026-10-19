/*
 * mux.h - the DOS Time Gate's AN<n>.MUX cut scenes, see mux.c.
 */

#ifndef AITD_MUX_H
#define AITD_MUX_H

/* Plays a MUX film with its sound; Escape skips. 0 when it cannot open. */
int mux_play(const char *name);

#endif /* AITD_MUX_H */
