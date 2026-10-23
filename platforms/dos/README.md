# MS-DOS

The DOS build runs in 32 bit protected mode under the DOS/32A extender. It
uses VGA mode 13h (320 x 200, 256 colours), a Sound Blaster 16 and, for
Time Gate's music, an MPU-401 MIDI interface.

## Build

Install [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2)
in `~/watcom`, or point `WATCOM` at it, then:

```sh
make dos
```

This writes `build/dos/aitd.exe` and copies `dos32a.exe` next to it. Keep
the two files together.

## Run

Copy `aitd.exe` and `dos32a.exe` into the game folder and run `aitd`. The
program needs a 486 or better and about 64 MB of memory. Under DOSBox or
DOSBox-X, set `memsize=128` and `cycles=max`.

The sound settings come from the `BLASTER` variable, for example
`SET BLASTER=A220 I5 D1 H5 P330 T6`:

| Letter | Meaning | Default |
|---|---|---|
| A | Sound Blaster port | 220 |
| I | IRQ | 5 |
| H | 16 bit DMA channel | 5 |
| P | MPU-401 port | 330 |

Without `BLASTER`, the game runs silent.

## Limits

- The 640 x 400 build of Time Gate shows every other pixel in 320 x 200.
  Set `AITD_RES=320` to play the 320 build instead.
- DOS file names are 8.3, so a BIN/CUE image with long names cannot be
  opened. Use the installed game files, or rename the image and edit the
  `.cue` to match.
- Music and sound go through a software mixer at 44.1 kHz. A slow machine
  can play with `-s` (no sound) to save time.
