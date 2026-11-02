# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [semantic versioning](https://semver.org/). The version of a build is
in the `VERSION` file, and `aitd --version` prints it.

## [Unreleased]

## [0.4.0] - 2026-11-02

### Added
- An editor, opened with F10 over the main menu or the game, with viewers
  for the pictures, the models, the font, the texts and the life scripts
  of the running game, and the animations played on the models. F10 or
  Esc goes back to where you were. The life
  listing shows the variables' current values, the objects that run the
  script, and the lines it ran last. `--stage=life-check` lists every
  script of a game and fails on any that does not list to its end.
- Hold Shift with Up to run, as well as pressing Up twice.
- The DOS Time Gate's sound card setup screen, from the setup icon of the
  menu: DIGITAL and MIDI switch the sound effects and the music, and play
  the test sound and tune.
- Time Gate plays its samples on ten channels at once, each with its own
  volume, including the lists of samples some rooms play in turn and the
  sounds that fade with distance from the camera.
- PAK containers packed with methods 2 and 3 can be read.
- The harness spawns an effect at the player with `AITD_SPECIAL=frame,type`,
  `h` in `AITD_INPUT` holds Shift, `f` presses F10, and `AITD_LOAD=dir,slot` starts the
  game stage from a saved game.

### Fixed
- Time Gate: the guard who comes to Ben at the tool box walks round the
  counter and reaches him, so the game goes on instead of freezing. Guards
  that follow Ben take routes round obstacles.
- Alone in the Dark 1: WATER no longer runs its operand as the next
  instruction, and the actors are cut at the water line while it is on.
  Tracks set the actors' walk, run and stand speeds.
- Alone in the Dark 1, 2 and Jack in the Dark draw the coloured circles of
  the dust cloud an actor leaves when it dies.
- The save and load screens of Alone in the Dark 1, 2 and Jack in the Dark
  are laid out as in Alone in the Dark 1 and show the picture of the
  selected save.
- The car of the Alone in the Dark 1 intro no longer circles for ever when
  the game runs at a low frame rate (seen on the Vita).
- Time Gate stops the music for a new game and for the intro films of the
  menu, and starts the game's tune again when the game's menu closes (the
  DOS menu is silent, the Windows one plays the menu tune). Saving from
  that menu goes back to the game.
- Time Gate no longer shows the scene in the inventory's colours for a
  moment when the inventory opens.
- Time Gate's menu comes back in its own colours after the cinematic
  option.
- The DOS Time Gate's menu has the original's eight icons and fades in on
  the medallion alone. Its
  sound icon opens the original volume screen, with a music and a sound
  bar. Music played from data (AdLib, MIDI, HMP) can now be switched off
  with ; as well.
- Time Gate's save and load screens are the original's turning wooden
  drum, with the picture of each saved game on a face. Saves made from the
  game's menu now keep a picture.

## [0.3.0] - 2026-10-25

### Added
- `make art` makes PSP and Vita icons and backgrounds from the pictures in
  your own game data (`aitd --stage=pictures` dumps them). The Vita
  packages get an icon, a LiveArea background and a start-up image, and
  the PSP builds one EBOOT per game with its own title, icon and
  background.
- `aitd --stage=pictures` writes every full screen picture of a game's
  `ITD_RESS.PAK` as a PPM file.

### Changed
- `make dist` packs the PSP build as one folder per game, ready to copy
  to `PSP/GAME/`.

## [0.2.0] - 2026-10-23

### Added
- Builds for Linux (also the Steam Deck), Windows 7 and later, Windows 95
  and 98, MS-DOS, web browsers, the PlayStation Portable, the PlayStation
  Vita, and iOS and iPadOS. Each has its notes in `platforms/<name>/`.
  The Vita and iOS builds compile but have not run yet.
- `make dist` packages every platform whose toolchain is installed as
  `dist/aitd-<platform>-<version>.zip`, and a GitHub workflow builds all
  of them on each push and publishes a release for each `v` tag.
- Time Gate music on hosts without a system MIDI synthesizer, played with
  TinySoundFont from a General MIDI soundfont (`.sf2`) that you provide.
  `AITD_SOUNDFONT` names the file; otherwise the engine looks in the data
  folder and the usual system folders.
- One gamepad layout for every platform: the D-pad or left stick walks,
  A does the action, X opens the inventory, B and Start go back, Y opens
  the map, and L and R step through the actions.
- Ripped CD tracks can be WAV files (`track02.wav`) on every platform.
- `aitd.env` in the program's folder sets `AITD_*` variables on hosts
  without an environment (the consoles). `AITD_NO_SOUND` and
  `AITD_NO_CD` do what `-s` and `-c` do.
- `VERSION` file, this changelog and `aitd --version`.

### Changed
- The program is now called `aitd` instead of `aitd3`.
- The engine no longer needs zlib. Its own inflate decodes PAK method 4
  and gives the same bytes as zlib on every entry of the six games.
- CD audio streams from the disc image or the WAV file instead of loading
  whole tracks, which took up to 80 MB.
- The AdLib synthesizer runs in single precision with tables, fast enough
  for the PSP. Its output correlates 0.9991 with the previous version.
- The platform layer is split into shared code (`src/platforms/common.c`,
  `mixer.c`) and one small backend per host. `src/platforms/backend.h`
  lists what a new host needs. The source list is in `sources.mk`.

## [0.1.0] - 2026-10-23

The first version. It runs all six games from their original data on
macOS.

### Added
- The engine of Alone in the Dark 3 for Windows (`WAITD3.EXE`), rebuilt
  function by function, with per-game profiles for Alone in the Dark 1,
  Jack in the Dark, Alone in the Dark 2 and 3, and the DOS and Windows
  builds of Time Gate: Knight's Chase.
- PAK containers with stored, implode and deflate entries; floors,
  cameras, masks, bodies, animations, life scripts and tracks of every
  game.
- Rendering: masks, patterned and textured polygons, Gouraud shading and
  the Time Gate light planes.
- Menus, inventories, documents, saves, the map, cut scenes, Smacker
  and MUX films.
- Sound: samples, AdLib music on an OPL2 emulator, HMP and MIDI music,
  CD audio, and the speech of the CD release of Alone in the Dark 1.
- BIN/CUE disc images as the installation, with CD audio played from the
  image.
- Number row keys that choose an action without opening the inventory.
- A harness that drives the engine from environment variables, and
  `make check`, which runs every game unattended.
