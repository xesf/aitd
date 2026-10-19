/*
 * opl.h - a YM3812 (OPL2) synthesiser, see opl.c.
 */

#ifndef AITD_OPL_H
#define AITD_OPL_H

void opl_init(int sample_rate);
void opl_write(int reg, int value);
/* Mono 16 bit samples at the rate given to opl_init. */
void opl_generate(short *out, int frames);

#endif /* AITD_OPL_H */
