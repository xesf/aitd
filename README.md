# aitd

A reimplementation of the Alone in the Dark engine in C99. It runs these
games from their original data files:

| Game | Data folder |
|---|---|
| Alone in the Dark (1992) | `data/INDARK` |
| Jack in the Dark (1993) | `data/JACK` |
| Alone in the Dark 2 (1993) | `data/INDARK2` |
| Alone in the Dark 3 (1994) | `data/INDARK3` |
| Time Gate: Knight's Chase, DOS (1995) | `data/TIMEGATE` |
| Time Gate: Knight's Chase, Windows (1996) | `data/KNIGHTS` |

The base is `WAITD3.EXE`, the Windows build of Alone in the Dark 3. Each
function comes from its disassembly, and the C code names the original
address in a comment. The other titles add their own differences on top:
`TG320.EX_` and `TG640.EX_` for Windows Time Gate, `TIMEGATE.EXE` for DOS
Time Gate, and the data files for the parts that no executable here
covers. No code from other projects is used.

The repository has no game data. You need your own copy of each game.

## Requirements

- macOS with the Xcode command line tools (`clang`, AppKit, CoreGraphics,
  AudioToolbox).
- zlib, which ships with macOS.

Only `src/platform_macos.m` is specific to macOS. The rest is plain C99.

## Build

```sh
make
```

The build writes the `aitd3` executable to the repository root.

## Add the game data

Copy the installed files of each game into its data folder. File names
stay in upper case, as on the original disks.

```
data/INDARK/    LISTBOD2.PAK, ETAGE00.PAK, ...
data/INDARK3/   AN1.PAK, ...
data/KNIGHTS/   the Windows install, with its 320/ and 640/ folders
```

The engine detects the game from a file that only that game has:
`LISTBOD2.PAK`, `PERE.PAK`, `MER.PAK`, `AN1.PAK` or `PURSUIT.PAK`. A
`640/` folder marks the Windows build of Time Gate.

For the Alone in the Dark 3 music, put the CD audio tracks next to the
data as `track02.m4a`, `track03.m4a` and so on. The engine also accepts
`.mp3`, `.wav` and `.aiff` files.

## Run

```sh
make run-aitd1      # also run-jack, run-aitd2, run-aitd3, run-timegate, run-knights
make run-knights RES=320
```

To start a game without the menus, use the `game` stage:

```sh
make run-aitd1 ARGS=--stage=game
```

The window is 640 x 480, and the picture fills it at a 4:3 ratio as on a
DOS screen. To make it larger, set `AITD_WINDOW_SCALE=2`.

## Controls

| Key | Action |
|---|---|
| Arrow keys | Walk and turn. Press Up twice to run. |
| Space | Use the selected action (fight, open, push, and others) |
| Enter or I | Inventory |
| Tab | Map (Alone in the Dark 3) |
| Esc | Options menu: save, load, quit |
| P | Pause |
| S | Sound on or off |
| ; | Music on or off |

## Check every game

```sh
make check
```

`make check` runs each game unattended for 300 frames and prints `ok` or
the exit status per game. Set `FRAMES` and
`TIMEOUT` to change the length.

The engine also reads environment variables that script input, dump
frames, move the player and trace state. `NOTES.md` lists them under
"Harness".

## Documentation

- [docs/engine.md](docs/engine.md) explains how the engine is put
  together: the frame loop, the world, rendering, scripts, sound and the
  per-game profiles.
- [NOTES.md](NOTES.md) is the reverse engineering reference: addresses,
  file formats and the differences between the games.

## Layout

```
src/        the engine (C99) and the macOS platform layer
tools/      data checkers and the scripts that sync names into IDA
data/       game data, not in the repository
ida/        IDA database, not in the repository
```
