# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [semantic versioning](https://semver.org/). The version of a build is
in the `VERSION` file, and `aitd --version` prints it.

## [Unreleased]

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
