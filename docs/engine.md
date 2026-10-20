# How the engine works

This page explains how the engine is put together and why it is shaped
the way it is. For addresses and exact file layouts, see
[NOTES.md](../NOTES.md).

## One engine, six games

Alone in the Dark 1, Jack in the Dark, Alone in the Dark 2 and 3, and both
builds of Time Gate share one engine. Infogrames changed it from game to
game, but the core stayed the same: pre-rendered backgrounds, 3D actors
drawn over them, and scripts that drive every object.

This code starts from `WAITD3.EXE`, the Windows build of Alone in the Dark
3, because it is the most complete version with a 32 bit flat binary. The
C functions follow its routines one by one, and each function names the
original address in a comment. Where another game behaves differently,
the difference is either a field in its profile or a branch on the game
in the code that needs it.

### Profiles

`src/game.c` holds one `struct game_profile` per game. `game_detect`
picks the profile from a file that only one game ships, for example
`PURSUIT.PAK` for Time Gate. The profile then answers every question that
differs between games:

- where floors and cameras live (`ETAGE` blobs, `SAL` and `CAM` files, or
  `ETAGE` and `CAMSAL` files)
- the size of a camera view record (12, 16 or 22 bytes)
- the background layout and where the palette comes from
- how masks are stored
- the size of an object record and of `DEFINES.ITD`
- the numbering of life script opcodes (`lifemap.c`)
- the menus, the inventory screen, the message style and the music source

Code asks `game_current()` for the profile and never tests file names
directly.

## Start-up

`main.c` mirrors `game_main` of the original:

1. `engine_init` opens the window, loads the font, the text table, the
   resource caches and `DEFINES.ITD`.
2. The intro plays. Each game can replace it with its own start-up
   (`boot.c` for the Alone in the Dark 1 logo, credits and character
   choice; Smacker or MUX films for Time Gate).
3. The main menu loops until the player quits. A new game calls
   `game_run(floor, room, 1)`.

`game_run` (`loop.c`) loads the objects, enters the start floor and room,
sets up the camera and runs the scene loop until the player leaves.

## The scene loop

`game_scene_loop` runs one frame per pass. The order matters, because the
scripts read state that the movement step writes:

1. Wait for the next 1/60 s tick. Without this wait, frames with no
   animation step would let scripts miss collisions.
2. Read input. In the intro (`flag` 0), any key ends the scene.
3. Move every actor (`step_actors`): animation, track or keyboard
   movement, collision, gravity and room changes.
4. Run every actor's life script (`run_lives`).
5. Change floor or room if a script or a room link asked for it.
6. Choose the camera (`camera_select`). The camera only changes when the
   followed actor leaves the zone of the current camera.
7. Create and remove actors for the new view, then build the list of
   visible actors.
8. Sort the visible actors and draw the frame.
9. Service sound and music.

## The world

### Floors, rooms and cameras

A floor (`ETAGE##.PAK`) holds rooms and cameras. A room has a position,
collision boxes (hard collisions) and scene zones. Scene zones link rooms,
set a floor material, or start a script when an actor enters them. Room
coordinates are local: when an actor crosses a room link, the engine
moves its position and box into the new room's frame.

A camera has angles, a position, and a list of the rooms it shows. For
each shown room, the view record points to masks and to zone polygons.
The zone polygons decide which camera the engine picks for the followed
actor.

### Objects and actors

Objects (`OBJETS.ITD`) are the permanent state of every thing in the game:
position, body, animation, script and flags. They exist whether or not
anything sees them.

Actors are the live copies of the objects near the player. The engine
creates an actor when its object should run (by life mode) or when a
camera shows it, and copies the state back to the object when the actor
is removed. There are 50 actor slots.

The life mode decides when an object gets an actor and runs its script.
Alone in the Dark 1 numbers the modes one lower than the later games:

| Meaning | AITD1 | Later games |
|---|---|---|
| Never | -1 | 0 |
| Anywhere on the floor | 0 | 1 |
| Only in the current room | 1 | 2 |
| In any room the camera shows | 2 | 3 |

### Scripts and tracks

Every object can have a life script (`LISTLIFE.PAK`). `life.c` runs the
scripts with an expression evaluator for values such as distance,
collision, animation state, inventory and game variables. Opcodes cover
animation, movement, sound, messages, inventory, cut scenes and changes
of floor or camera. `lifemap.c` maps each game's raw opcode numbers onto
one shared set.

A track (`LISTTRAK.PAK`) is a list of moves for an actor that the script
puts in track mode: walk to a point, warp, rotate, set marks that the
script can test, climb stairs, switch collision. Alone in the Dark 1
numbers its track opcodes differently from the later games, so they are
mapped too.

`DEFINES.ITD` holds the game variables and `VARS.ITD` the script
variables. Saves store both with the objects and actors.

## Drawing a frame

The engine keeps three 8 bit buffers:

- the front buffer: the camera background as loaded
- the back buffer: the background plus the static actors
- the screen: the back buffer plus everything that moves

Static actors (doors, furniture) are drawn into the back buffer once.
Each frame copies the back buffer to the screen and draws the moving
actors over it, farthest first. A static actor that something drew over
this frame is drawn again in its sorted place.

### Masks

The backgrounds are pictures, so an actor that walks behind a pillar must
be covered again by the pillar. After each actor, `draw_masks` copies
back the background pixels of every mask whose zone the actor's box
touches. The mask format differs by game:

- Alone in the Dark 1: polygons, filled from the background
- Alone in the Dark 2, 3 and Jack: span lists in `MASK##.PAK`
- Time Gate: span lists in `MK` files (DOS) or inside the camera file
  (Windows)

### 3D bodies

`r3d.c` transforms and projects bodies (`LISTBODY.PAK`). A body is a
point list, bones that move groups of points, and primitives: lines,
flat or patterned polygons, spheres and points. Animations
(`LISTANIM.PAK`) set the bone angles per key frame, and the engine blends
between key frames by time. Primitives are sorted by depth and drawn by
`raster.c`.

Time Gate adds textured and Gouraud shaded triangles (`texture.c`). Their
light level depends on the actor's position against a light plane stored
per camera, so a character darkens when it walks into a shadow.

### Sprites and overlays

`hyb.c` draws "hybrid" vector sprites: animated background pieces, some
actors and the inventory frames. `font.c`, `ui.c`, `inventory.c`,
`reader.c` and the menu files draw the 2D screens on top.

## Sound and music

`sound.c` plays samples (`LISTSAMP.PAK`, or WAV files in Time Gate) and
mixes them into a 16 bit, 44.1 kHz stream. Music depends on the game:

| Game | Source | Code |
|---|---|---|
| Alone in the Dark 1 (floppy), 2 and Jack | AdLib songs | `music.c` plays them on an OPL2 synthesizer in `opl.c` |
| Alone in the Dark 1 (CD), 3 | CD audio | ripped tracks from the data folder |
| Time Gate, DOS | HMP songs | `hmp.c` converts them to MIDI |
| Time Gate, Windows | MIDI | played through the system synthesizer |

## Platform layer

`platform.h` is the only interface to the host: window and palette,
input events, ticks, sound output, CD audio, MIDI and file paths.
`platform_macos.m` implements it with AppKit, CoreGraphics and
AudioToolbox. The engine draws at the game's own resolution (320 x 200,
or 640 x 400 for the Windows Time Gate build), and the platform layer
stretches the picture to a 4:3 window.

A port to another system needs a new `platform_*.c` file and no change
to the engine.

## Testing without playing

`harness.c` and environment variables let a script drive the engine: hold
keys for a range of input polls, dump frames as PPM files, move the
player, set game variables, list actors and trace the player state per
frame. `make check` uses them to run every game unattended. The full list
is in [NOTES.md](../NOTES.md) under "Harness".
