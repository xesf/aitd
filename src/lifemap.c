/*
 * lifemap.c - raw life opcodes and evaluator variables of each title onto
 * the life_op / life_var values the interpreter switches on.
 *
 * Alone in the Dark 2 and 3, Jack in the Dark and Time Gate share one
 * numbering. Alone in the Dark 1 has its own order (it predates opcodes
 * such as ANIM_RESET and has a few that were dropped later). The order
 * below is confirmed by parsing every LISTLIFE script of the AITD1 data
 * to its exact length.
 */

#include "game.h"
#include "life.h"

static const s16 s_aitd1_ops[] = {
    LIFE_OP_DO_MOVE, LIFE_OP_ANIM_ONCE, LIFE_OP_ANIM_ALL_ONCE, LIFE_OP_BODY,
    LIFE_OP_IF_EQUAL, LIFE_OP_IF_DIFFERENT, LIFE_OP_IF_SUP_EQUAL, LIFE_OP_IF_SUP,
    LIFE_OP_IF_INF_EQUAL, LIFE_OP_IF_INF, LIFE_OP_GOTO, LIFE_OP_RETURN,
    LIFE_OP_END, LIFE_OP_ANIM_REPEAT, LIFE_OP_ANIM_MOVE, LIFE_OP_MOVE,
    LIFE_OP_HIT, LIFE_OP_MESSAGE, LIFE_OP_MESSAGE_VALUE, LIFE_OP_VAR,
    LIFE_OP_INC, LIFE_OP_DEC, LIFE_OP_ADD, LIFE_OP_SUB,
    LIFE_OP_LIFE_MODE, LIFE_OP_SWITCH, LIFE_OP_CASE, LIFE_OP_CAMERA,
    LIFE_OP_START_CHRONO, LIFE_OP_MULTI_CASE, LIFE_OP_FOUND, LIFE_OP_LIFE,
    LIFE_OP_DELETE, LIFE_OP_TAKE, LIFE_OP_IN_HAND, LIFE_OP_READ,
    LIFE_OP_ANIM_SAMPLE, LIFE_OP_SPECIAL, LIFE_OP_DO_REAL_ZV, LIFE_OP_SAMPLE,
    LIFE_OP_TYPE, LIFE_OP_GAME_OVER, LIFE_OP_MANUAL_ROT, LIFE_OP_RND_FREQ,
    LIFE_OP_MUSIC, LIFE_OP_SET_BETA, LIFE_OP_DO_ROT_ZV, LIFE_OP_STAGE,
    LIFE_OP_FOUND_NAME, LIFE_OP_FOUND_FLAG, LIFE_OP_FOUND_LIFE, LIFE_OP_CAMERA_TARGET,
    LIFE_OP_DROP, LIFE_OP_FIRE, LIFE_OP_TEST_COL, LIFE_OP_FOUND_BODY,
    LIFE_OP_SET_ALPHA, LIFE_OP_STOP_BETA, LIFE_OP_DO_MAX_ZV, LIFE_OP_PUT,
    LIFE_OP_C_VAR, LIFE_OP_DO_NORMAL_ZV, LIFE_OP_DO_CARRE_ZV, LIFE_OP_SAMPLE_THEN,
    LIFE_OP_LIGHT, LIFE_OP_SHAKING, LIFE_OP_INVENTORY, LIFE_OP_FOUND_WEIGHT,
    LIFE_OP_UP_COOR_Y, LIFE_OP_SPEED, LIFE_OP_PUT_AT, LIFE_OP_DEF_ZV,
    LIFE_OP_HIT_OBJECT, LIFE_OP_GET_HARD_CLIP, LIFE_OP_ANGLE, LIFE_OP_REP_SAMPLE,
    LIFE_OP_THROW, LIFE_OP_WATER, LIFE_OP_PICTURE, LIFE_OP_STOP_SAMPLE,
    LIFE_OP_NEXT_MUSIC, LIFE_OP_FADE_MUSIC, LIFE_OP_STOP_HIT_OBJECT, LIFE_OP_COPY_ANGLE,
    LIFE_OP_END_SEQUENCE, LIFE_OP_SAMPLE_THEN_REPEAT, LIFE_OP_WAIT_GAME_OVER
};

#define AITD1_OP_COUNT (int)(sizeof(s_aitd1_ops) / sizeof(s_aitd1_ops[0]))

s16 life_opcode_map(s16 raw)
{
    if (game_current()->life_dialect != LIFE_DIALECT_AITD1) {
        return raw;
    }
    return (raw >= 0 && raw < AITD1_OP_COUNT) ? s_aitd1_ops[raw] : LIFE_OP_INVALID;
}

/* Variables 1..33 agree; AITD1 then has ROOM_Y and TEST_ZV_END_ANIM, which
 * shift MUSIC, C_VAR, STAGE and THROW up by two. */
s16 life_var_map(s16 raw)
{
    if (game_current()->life_dialect != LIFE_DIALECT_AITD1) {
        return raw;
    }
    switch (raw) {
    case 34: return LIFE_VAR_ROOM_Y;
    case 35: return LIFE_VAR_TEST_ZV_END_ANIM;
    case 36: return LIFE_VAR_MUSIC;
    case 37: return LIFE_VAR_DEFINE;
    case 38: return LIFE_VAR_STAGE;
    case 39: return LIFE_VAR_THROW;
    default: return raw;
    }
}
