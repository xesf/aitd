/*
 * boot.h - title specific start-up screens, see boot.c.
 */

#ifndef AITD_BOOT_H
#define AITD_BOOT_H

/* Alone in the Dark 1: logo and credits before the menu, the attract
 * sequence when the menu times out, and the character choice before a
 * new game (0 when it was left with Escape). */
void boot_aitd1_intro(void);
void boot_aitd1_attract(void);
int boot_aitd1_new_game(void);

#endif /* AITD_BOOT_H */
