# PlayStation Portable

The PSP build draws the frame with the GE as an 8 bit texture and a
colour table, 4:3 in the middle of the screen, plays sound on one audio
channel and reads the buttons and the analog stick. It runs the CPU at
333 MHz. It needs a PSP with custom firmware (or the PPSSPP emulator).

## Build

Install the [pspdev toolchain](https://pspdev.github.io) and put its `bin`
folder on `PATH`, or set `PSPDEV`, then:

```sh
make psp
```

This writes `platforms/psp/EBOOT.PBP`.

## Install

Make one folder per game under `PSP/GAME/` on the Memory Stick, for
example `PSP/GAME/AITD3/`. Copy `EBOOT.PBP` and the installed game files
into it. Disc images (`.bin`, `.cue`) work too, but the PSP reads its
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
