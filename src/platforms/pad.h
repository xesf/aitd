/*
 * pad.h - one gamepad layout for every backend. The engine reads only the
 * keyboard, so the backend reports its pad's state here and gets keys
 * back.
 *
 *   D-pad or left stick   arrow keys (walk and turn)
 *   A / Cross             Space (the selected action)
 *   B / Circle            Escape (back, options menu)
 *   X / Square            Enter (inventory)
 *   Y / Triangle          Tab (map in Alone in the Dark 3)
 *   L / R                 the previous and next action (number row keys)
 *   Start                 Escape
 *   Select                P (pause)
 */

#ifndef AITD_PAD_H
#define AITD_PAD_H

enum pad_button {
    PAD_UP = 1 << 0,
    PAD_DOWN = 1 << 1,
    PAD_LEFT = 1 << 2,
    PAD_RIGHT = 1 << 3,
    PAD_A = 1 << 4,
    PAD_B = 1 << 5,
    PAD_X = 1 << 6,
    PAD_Y = 1 << 7,
    PAD_L = 1 << 8,
    PAD_R = 1 << 9,
    PAD_START = 1 << 10,
    PAD_SELECT = 1 << 11
};

/* Pushes a key event: `down` 1 for a press, 0 for a release, `scancode`
 * a PC set 1 code. */
typedef void (*pad_emit_fn)(int down, int scancode);

/* Reports the pad: `buttons` a mask of enum pad_button, the stick in
 * -32768..32767 (0 when the pad has none). Emits a key event for every
 * change since the last call. */
void pad_update(unsigned buttons, int stick_x, int stick_y, pad_emit_fn emit);

#endif /* AITD_PAD_H */
