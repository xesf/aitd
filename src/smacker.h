/*
 * smacker.h - Smacker 2 cut scenes (Time Gate for Windows), see smacker.c.
 */

#ifndef AITD_SMACKER_H
#define AITD_SMACKER_H

/* Plays a 320 x 200 Smacker file with its first audio track; Escape skips.
 * Returns 0 when the file cannot be opened or is not supported. */
int smacker_play(const char *name);

#endif /* AITD_SMACKER_H */
