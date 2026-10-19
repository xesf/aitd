# Scripting

Every object in the game can run a life script, and a moving object can
follow a track. This page explains how both work and lists their
opcodes. The interpreter is `src/life.c`, the track player is
`actor_do_move` in `src/actor.c`, and `src/lifemap.c` maps each game's
numbering onto one set.

## When scripts run

An object runs its script when it has an actor and its life mode allows
it:

| Meaning | AITD1 | Later games |
|---|---|---|
| Never | -1 | 0 |
| Anywhere on the floor | 0 | 1 |
| Only in the current room | 1 | 2 |
| In any room the camera shows | 2 | 3 |

In each frame, the engine first moves every actor and then runs every
script, in actor slot order. A script runs from its first word every
frame until it reaches `END` or `RETURN`. It does not keep a position
between frames. State lives in variables, in the actor (animation,
track, timers) and in the object record.

`LIFE n` switches the actor to script `n` from the next frame. Room zones
of type 10 start the object's zone script (object field `+0x24`), and
picking an object or choosing an action runs its found script.

## Encoding

A script is an array of little endian `s16` words. Each instruction is an
opcode word followed by its operands.

- **Object prefix.** When bit 15 of the opcode is set, the next word is
  an object number and the instruction acts on that object instead of
  the script's owner. `[obj1] MOVE 0 -1` stops object 1. An object
  without an actor accepts only the opcodes that change its record (body,
  life, flags, position and similar).
- **Words** are immediate values: animation numbers, track numbers, text
  ids, sample numbers.
- **Jumps** are a word offset counted from the word after the offset. A
  jump of 0 continues with the next instruction.
- **Expressions** read a value. See below.

### Expressions

An expression starts with one word:

| First word | Value |
|---|---|
| -1 | the constant in the next word |
| 0 | `VARS.ITD` entry number N, from the next word |
| N | evaluator variable N of the owner |
| N with bit 15 set | variable N of the object in the next word |

Some variables take operands after the variable word. In the table below,
"word" is an immediate and "expr" is a nested expression.

| N | Variable | Value | Operands |
|---|---|---|---|
| 1 | `COL` | actor collided with this frame | - |
| 2 | `HARD_DEC` | scene zone of type 9 the actor is in | - |
| 3 | `HARD_COL` | hard collision parameter touched, 255 for type 3 | - |
| 4 | `HIT` | actor hit | - |
| 5 | `HIT_BY` | actor that hit this one | - |
| 6 | `ANIM` | current animation | - |
| 7 | `END_ANIM` | 1 when the animation finished | - |
| 8 | `FRAME` | current key frame | - |
| 9 | `END_FRAME` | 1 when a key frame finished | - |
| 10 | `BODY` | current body | - |
| 11 | `MARK` | last track mark passed | - |
| 12 | `NUM_TRACK` | track number, -1 when the track ended | - |
| 13 | `CHRONO` | seconds since `START_CHRONO` | - |
| 14 | `ROOM_CHRONO` | seconds since the actor entered its room | - |
| 15 | `DIST` | distance to an object | word |
| 16 | `COL_BY` | actor that collided with this one | - |
| 17 | `IN_INVENTORY` | 1 when the object is carried | expr |
| 18 | `ACTION` | action bits chosen in the inventory | - |
| 19 | `POSREL` | position of an object relative to this one | word (expr in Time Gate) |
| 20 | `KEYBOARD_INPUT` | one direction key held: left, right, up or down | - |
| 21 | `BUTTON` | action keys held | - |
| 22 | `COL_ANY` | `COL`, or `COL_BY` when there is none | - |
| 23 | `ALPHA` | angle | - |
| 24 | `BETA` | angle | - |
| 25 | `GAMMA` | angle | - |
| 26 | `IN_HAND` | object in hand | - |
| 27 | `HIT_FORCE` | force of the last hit | - |
| 28 | `CAMERA` | current camera file | - |
| 29 | `RAND` | random number below the operand | word |
| 30 | `FALLING` | 1 while falling | - |
| 31 | `ROOM` | room | - |
| 32 | `LIFE` | current script | - |
| 33 | `OBJECT_TAKEN` | 1 when the object was taken | word |
| 34 | `MUSIC` | music playing | - (word in Time Gate) |
| 35 | `DEFINE` | `DEFINES.ITD` word | word |
| 36 | `STAGE` | floor | - |
| 37 | `THROW` | 1 when the object was thrown | word |
| 38 | `MATRIX` | relation table lookup between two objects | word word |
| 39 | `MATERIAL` | floor material under the actor | - |
| 40 | `TEST_PROTECT` | copy protection check | - |
| 41 | `END_WAY` | Time Gate only | - |
| 42 | `DIV_BY_2` | Time Gate only: an expression halved | expr |

AITD1 inserts two variables after 33: 34 `ROOM_Y` and 35
`TEST_ZV_END_ANIM` (two words). Its `MUSIC`, `DEFINE`, `STAGE` and
`THROW` are 36 to 39.

When the object in a prefixed variable has no actor, only `DIST`, `ROOM`,
`LIFE`, `STAGE` and `MATRIX` read the object record. The other variables
read 0, except in AITD1, where the contact variables (`COL`, `HARD_DEC`,
`HARD_COL`, `HIT`, `HIT_BY`, `COL_BY`) read -1.

## Control flow

| Instruction | Effect |
|---|---|
| `IF_EQUAL a b jump` | continue when `a == b`, else jump. `IF_DIFFERENT`, `IF_SUP_EQUAL`, `IF_SUP`, `IF_INF_EQUAL` and `IF_INF` test `!=`, `>=`, `>`, `<=` and `<`. |
| `IF_IN v lo hi jump` | Time Gate: continue when `lo <= v <= hi`. `IF_OUT` is the opposite. |
| `GOTO jump` | jump |
| `SWITCH expr` | store a value for the following cases |
| `CASE value jump` | continue when the stored value equals `value`, else jump |
| `MULTI_CASE n values... jump` | continue when the stored value is one of the `n` values, else jump |
| `END`, `RETURN` | stop the script for this frame |

A `SWITCH` block is a chain of cases. Each case ends with a `GOTO` past
the rest of the block.

## Example

The AITD1 attic wardrobe (script 1) becomes pushable while the player
pushes against it. The listing below comes from the disassembler in
`tools/life` with the variable names filled in.

```
   0 IF_EQUAL BODY #2 ->66            closed wardrobe only
   5 IF_EQUAL COL_BY #1 ->62          the player touches it
  10 SWITCH [obj1].ANIM
  13 CASE 2 ->51                      the player's "open" animation
     ...                              open the doors, play sample 37
  51 CASE 5 ->60                      the player's push animation
  54 TYPE 16                          make the wardrobe pushable
  56 VAR 1 #1
  60 GOTO ->64
  62 TYPE 0                           nobody touches it: fixed again
  64 GOTO ->115
     ...
 115 END
```

The player's push animation (5) comes from the "push" action: the hand
object's script plays it while Space is held. Because the wardrobe clears
its pushable flag on any frame without contact, the engine must produce
a movement step every frame. That is one reason the scene loop waits for
a new 1/60 s tick before each frame.

## Opcodes

The first column is the number in AITD2, AITD3, Jack and Time Gate. The
second is the number in AITD1, where it differs. Operands: word, expr
(expression), jump, cases (`MULTI_CASE` count and values) and table (a
count and pairs of words).

| Op | AITD1 | Name | Operands |
|---|---|---|---|
| 0 | 0 | `DO_MOVE` | - |
| 1 | 1 | `ANIM_ONCE` | word word |
| 2 | 2 | `ANIM_ALL_ONCE` | word word |
| 3 | 3 | `BODY` | expr |
| 4 | 4 | `IF_EQUAL` | expr expr jump |
| 5 | 5 | `IF_DIFFERENT` | expr expr jump |
| 6 | 6 | `IF_SUP_EQUAL` | expr expr jump |
| 7 | 7 | `IF_SUP` | expr expr jump |
| 8 | 8 | `IF_INF_EQUAL` | expr expr jump |
| 9 | 9 | `IF_INF` | expr expr jump |
| 10 | 10 | `GOTO` | jump |
| 11 | 11 | `RETURN` | - |
| 12 | 12 | `END` | - |
| 13 | 13 | `ANIM_REPEAT` | word |
| 14 | 14 | `ANIM_MOVE` | word word word word word word word |
| 15 | 15 | `MOVE` | word word |
| 16 | 16 | `HIT` | word word word word expr word |
| 17 | 17 | `MESSAGE` | word |
| 18 | 19 | `VAR` | word expr |
| 19 | 20 | `INC` | word |
| 20 | 21 | `DEC` | word |
| 21 | 22 | `ADD` | word expr |
| 22 | 23 | `SUB` | word expr |
| 23 | 24 | `LIFE_MODE` | word |
| 24 | 25 | `SWITCH` | expr |
| 25 | 26 | `CASE` | word jump |
| 26 | 28 | `START_CHRONO` | - |
| 27 | 29 | `MULTI_CASE` | cases |
| 28 | 30 | `FOUND` | word |
| 29 | 31 | `LIFE` | word |
| 30 | 32 | `DELETE` | expr |
| 31 | 33 | `TAKE` | word |
| 32 | 34 | `IN_HAND` | expr |
| 33 | 35 | `READ` | word word |
| 34 | 36 | `ANIM_SAMPLE` | expr word word |
| 35 | 37 | `SPECIAL` | word |
| 36 | 38 | `DO_REAL_ZV` | - |
| 37 | 39 | `SAMPLE` | word |
| 38 | 40 | `TYPE` | word |
| 39 | 41 | `GAME_OVER` | - |
| 40 | 42 | `MANUAL_ROT` | - |
| 41 | 43 | `RND_FREQ` | word |
| 42 | 44 | `MUSIC` | word |
| 43 | 45 | `SET_BETA` | word word |
| 44 | 46 | `DO_ROT_ZV` | - |
| 45 | 47 | `STAGE` | word word word word word |
| 46 | 48 | `FOUND_NAME` | word |
| 47 | 49 | `FOUND_FLAG` | word |
| 48 | 50 | `FOUND_LIFE` | word |
| 49 | 51 | `CAMERA_TARGET` | word |
| 50 | 52 | `DROP` | expr word |
| 51 | 53 | `FIRE` | expr word word word word word expr |
| 52 | 54 | `TEST_COL` | word |
| 53 | 55 | `FOUND_BODY` | word |
| 54 | 56 | `SET_ALPHA` | word word |
| 55 | 58 | `DO_MAX_ZV` | - |
| 56 | 59 | `PUT` | word word word word word word word word word |
| 57 | 61 | `DO_NORMAL_ZV` | - |
| 58 | 62 | `DO_CARRE_ZV` | - |
| 59 | 63 | `SAMPLE_THEN` | word word |
| 60 | 64 | `LIGHT` | word |
| 61 | 65 | `SHAKING` | word |
| 62 | 66 | `INVENTORY` | word |
| 63 | 67 | `FOUND_WEIGHT` | word |
| 64 | 70 | `PUT_AT` | word word |
| 65 | 71 | `DEF_ZV` | word word word word word word |
| 66 | 72 | `HIT_OBJECT` | word word |
| 67 | 74 | `ANGLE` | word word word |
| 68 | 75 | `REP_SAMPLE` | word word |
| 69 | 76 | `THROW` | word word word word word word word |
| 70 | 77 | `WATER` | word |
| 71 | 78 | `PICTURE` | word word word |
| 72 | 79 | `STOP_SAMPLE` | - |
| 73 | 80 | `NEXT_MUSIC` | word |
| 74 | 81 | `FADE_MUSIC` | word |
| 75 | 82 | `STOP_HIT_OBJECT` | - |
| 76 | 83 | `COPY_ANGLE` | word |
| 77 | 85 | `SAMPLE_THEN_REPEAT` | word word |
| 78 | 86 | `WAIT_GAME_OVER` | - |
| 79 | - | `GET_MATRICE` | - |
| 80 | - | `STAGE_LIFE` | word |
| 81 | - | `CONTINUE_TRACK` | - |
| 82 | - | `ANIM_RESET` | word word |
| 83 | - | `RESET_MOVE_MANUAL` | - |
| 84 | - | `PLUIE` | word |
| 85 | - | `ANIM_HYBRIDE_ONCE` | word word |
| 86 | - | `ANIM_HYBRIDE_REPEAT` | word word |
| 87 | - | `MODIF_C_VAR` | word expr |
| 88 | - | `CALL_INVENTORY` | - |
| 89 | - | `BODY_RESET` | expr expr |
| 90 | - | `DEL_INVENTORY` | - |
| 91 | - | `SET_INVENTORY` | word |
| 92 | - | `PLAY_SEQUENCE` | word word word |
| 93 | - | `2D_ANIM_SAMPLE` | expr word word |
| 94 | - | `SET_GROUND` | word |
| 95 | - | `PROTECT` | - |
| 96 | - | `DEF_ABS_ZV` | word word word word word word |
| 97 | - | `DEF_SEQUENCE_SAMPLE` | table |
| 98 | - | `READ_ON_PICTURE` | word word word word word word word word |
| 99 | - | `FIRE_UP_DOWN` | expr word word word word word word expr |
| 100 | - | `DO_ROT_CLUT` | word word word |
| 101 | - | `STOP_CLUT` | - |
| - | 18 | `MESSAGE_VALUE` | word word |
| - | 27 | `CAMERA` | word |
| - | 57 | `STOP_BETA` | - |
| - | 60 | `C_VAR` | word expr |
| - | 68 | `UP_COOR_Y` | - |
| - | 69 | `SPEED` | word |
| - | 73 | `GET_HARD_CLIP` | - |
| - | 84 | `END_SEQUENCE` | - |

Operand differences:

| Game | Opcode | Operands |
|---|---|---|
| AITD1 | `FIRE` | 6 words |
| AITD1 | `SAMPLE` | expr |
| AITD1 | `REP_SAMPLE` | expr word |
| AITD1 | `SAMPLE_THEN`, `SAMPLE_THEN_REPEAT` | expr expr |
| AITD1 | `IN_HAND`, `DELETE` | word |
| AITD1 | `READ` | 3 words |
| Jack | `PLAY_SEQUENCE` | word |
| Jack | `SAMPLE` | expr |
| Jack | `SAMPLE_THEN` | expr expr |
| Jack | `IN_HAND`, `DELETE` | word |
| Time Gate | `SAMPLE`, `REP_SAMPLE` | expr word |
| Time Gate | `STOP_SAMPLE` | word |
| Time Gate | `ANIM_SAMPLE` | expr word word word |
| Time Gate | `PICTURE` | word word expr word |
| Time Gate | `TAKE` | expr word word word |

Time Gate adds opcodes from 102:

| Op | Name | Operands |
|---|---|---|
| 102 | `IF_IN` | expr expr expr jump |
| 103 | `IF_OUT` | expr expr expr jump |
| 105 | `SET_VOLUME_SAMPLE` | word word |
| 108 | `FADE_IN_MUSIC` | word word word |
| 109 | `SET_MUSIC_VOLUME` | word word |
| 110 | `MUSIC_AND_LOOP` | word word |
| 111 | `MUSIC_THEN` | word word word |
| 112 | `MUSIC_THEN_LOOP` | word word word |
| 113 | `START_FADE_IN_MUSIC` | 5 words |
| 114 | `START_FADE_IN_MUSIC_THEN` | 6 words |
| 115 | `START_FADE_IN_MUSIC_LOOP` | 5 words |
| 116 | `FADE_OUT_MUSIC_STOP` | word word |
| 117 | `MUSIC_ALTER_TEMPO` | word word |
| 121 | value list | a count, then that many expressions |

The operand shapes come from the interpreters of `WAITD3.EXE` and
`TG320.EX_`, and for AITD1 and Jack from the data. With them, every
`LISTLIFE.PAK` script of each game parses to its exact length.
`tools/life/lifecheck.py` and `tools/life/tgcheck.py` run that check.

## Tracks

`MOVE mode number` sets how an actor moves:

| Mode | Movement |
|---|---|
| 0 | none |
| 1 | the arrow keys |
| 2 | follow object `number` |
| 3 | track `number` from `LISTTRAK.PAK` |

The script must call `DO_MOVE` each frame for the movement to happen. The
actor still needs an animation with a step: tracks only steer.

A track is an array of `s16` words read one instruction at a time. An
instruction that is not finished (walking to a point, turning) is read
again on the next frame.

| Op | AITD1 | Name | Operands | Effect |
|---|---|---|---|---|
| 0 | 0 | `WARP` | room x y z | place the actor |
| 1 | 1 | `GOTO_POS` | room x z | turn and walk until within 200 units |
| 2 | 2 | `END` | - | stop; `NUM_TRACK` reads -1 |
| 3 | 3 | `REWIND` | - | start the track again |
| 4 | 4 | `MARK` | n | set `MARK` for the script |
| 5 | - | `STOP` | - | wait for `CONTINUE_TRACK` |
| 6 | 9 | `ROTATE` | beta | turn to an angle |
| 7 | 10 | `COL_OFF` | - | stop colliding with actors |
| 8 | 11 | `COL_ON` | - | collide again |
| 9 | - | `SKIP_WORD` | word | - |
| 10 | 13 | `FALL_OFF` | - | no gravity |
| 11 | 14 | `FALL_ON` | - | gravity again |
| 12 | 15 | `GOTO_POS_Y` | room x y z time | walk to a point while changing height |
| 13 | 16 | `STORE_POS` | - | save the position in the object |
| 14 | 17 | `STAIRS_X` | x y z | climb, height following x |
| 15 | 18 | `STAIRS_Z` | x y z | climb, height following z |
| 16 | 19 | `ANGLES` | alpha beta gamma | set the angles |

AITD1 opcodes 5, 6, 7, 8 and 12 take no operand. Their effect is not yet
known, and the engine skips them.
