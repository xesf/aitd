/*
 * exe_tables.h - constant tables of the WAITD3.EXE renderer, rebuilt at
 * start-up by exe_tables_init.
 */

#ifndef AITD_EXE_TABLES_H
#define AITD_EXE_TABLES_H

#include "aitd.h"

extern s16 g_sin_table[1024];          /* 0x439abc */
extern u8 g_poly_noise[0x10800];       /* 0x401000.. */
extern u8 g_poly_ramp[16 * 321];       /* 0x42f988 */

#define POLY_NOISE_ORIGIN 0xacc  /* index of 0x401acc in g_poly_noise */

/* Fills the three tables. Call once before anything is drawn. */
void exe_tables_init(void);

#endif /* AITD_EXE_TABLES_H */
