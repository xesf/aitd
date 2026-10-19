/*
 * hmp.h - HMI HMP songs to standard MIDI, see hmp.c.
 */

#ifndef AITD_HMP_H
#define AITD_HMP_H

#include "aitd.h"

/* A malloc'd standard MIDI file, or NULL when the data is not HMP. */
u8 *hmp_to_midi(const u8 *hmp, u32 size, u32 *out_size);

#endif /* AITD_HMP_H */
