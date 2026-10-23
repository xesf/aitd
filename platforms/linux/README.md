# Linux

The Linux build uses X11 for the window, ALSA for sound and the kernel's
joystick devices (`/dev/input/js*`) for gamepads. The same build runs on
the Steam Deck.

## Build

On Linux, install a C compiler and the X11 and ALSA headers, then build:

```sh
sudo apt install build-essential libx11-dev libasound2-dev   # Debian, Ubuntu
sudo pacman -S base-devel libx11 alsa-lib                     # Arch, SteamOS
make linux
```

The program is `build/linux/aitd`. On macOS or another system, `make linux`
builds it in a Docker container made from the `Dockerfile` here.

## Run

Put the program next to the game data, or set `AITD_DATA_DIR`:

```sh
AITD_DATA_DIR=~/games/INDARK ./aitd
```

## Music

Alone in the Dark 1, 2 and Jack play their AdLib music, and the CD
releases play their CD tracks. Time Gate's music is MIDI. Linux has no
system MIDI synthesizer, so the engine plays it from a General MIDI
soundfont (`.sf2`). It uses the first one it finds:

1. the file named by `AITD_SOUNDFONT`
2. a `.sf2` file in the data folder
3. `/usr/share/soundfonts`, `/usr/share/sounds/sf2`

On Debian and Ubuntu, `sudo apt install fluid-soundfont-gm` installs one.
Without a soundfont, Time Gate runs without music.

## Gamepad

| Button | Key | Action |
|---|---|---|
| D-pad, left stick | Arrows | Walk and turn |
| A | Space | Do the selected action |
| B | Esc | Back, options menu |
| X | Enter | Inventory |
| Y | Tab | Map (Alone in the Dark 3) |
| LB, RB | 1 to 9, 0, - | Previous or next action |
| Start | Esc | Options menu |
| Back | P | Pause |

## Steam Deck

Build on the Deck in desktop mode, or copy a build made on another Linux
system. Add `aitd` to Steam as a non-Steam game and set its launch options
to point at the data, for example
`AITD_DATA_DIR=/home/deck/games/INDARK %command%`. Under Steam Input, use
the "Gamepad" layout so the pad reaches the game as a joystick.
