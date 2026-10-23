/*
 * pad.c - gamepad to keyboard, the layout described in pad.h.
 */

#include "platforms/pad.h"

#define STICK_DEADZONE 12000

/* L and R step through the actions in the order of the number row keys
 * (1 Use .. - Drop/Put), from the last one chosen with them. */
#define ACTION_KEYS 11
#define SC_ACTION_FIRST 0x02

static const struct {
    unsigned button;
    int scancode;
} s_map[] = {
    { PAD_UP, 0xc8 },
    { PAD_DOWN, 0xd0 },
    { PAD_LEFT, 0xcb },
    { PAD_RIGHT, 0xcd },
    { PAD_A, 0x39 },
    { PAD_B, 0x01 },
    { PAD_X, 0x1c },
    { PAD_Y, 0x0f },
    { PAD_START, 0x01 },
    { PAD_SELECT, 0x19 },
};

void pad_update(unsigned buttons, int stick_x, int stick_y, pad_emit_fn emit)
{
    static unsigned previous;
    static int action = 4;      /* Fight, the usual starting action */
    static int held_action;     /* an action key to release on the next call */
    unsigned changed;
    unsigned i;

    /* The engine sees a key only while it is down at a poll, so an action
     * key is released one call later. */
    if (held_action != 0) {
        emit(0, held_action);
        held_action = 0;
    }

    if (stick_x < -STICK_DEADZONE) buttons |= PAD_LEFT;
    if (stick_x > STICK_DEADZONE) buttons |= PAD_RIGHT;
    if (stick_y < -STICK_DEADZONE) buttons |= PAD_UP;
    if (stick_y > STICK_DEADZONE) buttons |= PAD_DOWN;

    changed = buttons ^ previous;
    for (i = 0; i < sizeof(s_map) / sizeof(s_map[0]); i++) {
        if (changed & s_map[i].button) {
            emit((buttons & s_map[i].button) != 0, s_map[i].scancode);
        }
    }
    if ((changed & buttons) & (PAD_L | PAD_R)) {
        action = (action + ((buttons & PAD_R) ? 1 : ACTION_KEYS - 1)) % ACTION_KEYS;
        held_action = SC_ACTION_FIRST + action;
        emit(1, held_action);
    }
    previous = buttons;
}
