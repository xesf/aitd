# PlayStation Portable

The PSP build draws the frame with the GE as an 8 bit texture and a
colour table, 4:3 in the middle of the screen, plays sound on one audio
channel and reads the buttons and the analog stick. It runs the CPU at
333 MHz. It needs a PSP with custom firmware (or the PPSSPP emulator).

## Build

Install the [pspdev toolchain](https://pspdev.github.io) and put its `bin`
folder on `PATH`, or set `PSPDEV`, then:

```sh
make art          # optional: icons and backgrounds from your game data
make psp
```

This writes one EBOOT per game, each with its own name in the XMB:
`build/psp/<GAME>/EBOOT.PBP` for INDARK, JACK, INDARK2, INDARK3,
TIMEGATE and KNIGHTS. `make art` takes the icon (ICON0) and the
background (PIC1) from the pictures in each game's `data/<GAME>` folder;
without it the EBOOTs have no art. The repository holds no game art.

## Install

Copy each `build/psp/<GAME>/` folder to `PSP/GAME/` on the Memory Stick,
for example `PSP/GAME/INDARK3/`, and put the installed game files in the
same folder. Disc images (`.bin`, `.cue`) work too, but the PSP reads its
Memory Stick slowly, so installed files load faster.

The PSP has no environment variables. Settings that other platforms take
from `AITD_*` variables go into a file named `aitd.env` in the same
folder, one per line:

```
AITD_LANG=ENGLISH
```

## Controls

| Button | Action |
|---|---|
| D-pad, analog stick | Walk and turn |
| Cross | Do the selected action |
| Circle | Back, options menu |
| Square | Inventory |
| Triangle | Map (Alone in the Dark 3) |
| L, R | Previous or next action |
| Start | Options menu |
| Select | Pause |

## Memory

The PSP-1000 has 24 MB for programs and later models 52 MB. The engine
asks for the large memory mode. CD audio is read from the Memory Stick
as it plays, so it does not take memory.

## Music

Time Gate's MIDI music needs a General MIDI soundfont (`.sf2`) in the
game folder. The PSP's CPU is slow for a soundfont synthesizer, so a
small soundfont works best.
