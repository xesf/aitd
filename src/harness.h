/*
 * harness.h - staged bring-up driver, see harness.c.
 * Returns non-zero when it handled the run and game_main should be skipped.
 */

#ifndef AITD_HARNESS_H
#define AITD_HARNESS_H

int harness_run(int argc, char **argv);

#endif /* AITD_HARNESS_H */
