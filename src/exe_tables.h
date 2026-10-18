/*
 * exe_tables.h - constant tables copied out of WAITD3.EXE by
 * tools/extract_tables.py.
 */

#ifndef AITD_EXE_TABLES_H
#define AITD_EXE_TABLES_H

#include "aitd.h"

extern const s16 g_sin_table[1024];          /* 0x439abc */
extern const u8 g_poly_noise[0x10800];       /* 0x401000.. */
extern const u8 g_poly_ramp[16 * 321];       /* 0x42f988 */

#define POLY_NOISE_ORIGIN 0xacc  /* index of 0x401acc in g_poly_noise */

#endif /* AITD_EXE_TABLES_H */
