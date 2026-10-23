# Windows 95 and 98

This build is for the systems the original games ran on. It uses the same
backend as the modern Windows build (`src/platforms/windows.c`): GDI,
`waveOut` and the MCI sequencer, all present since Windows 95. Gamepads
are not read, because XInput does not exist there.

## Build

Install [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2)
in `~/watcom`, or point `WATCOM` at it, then:

```sh
make win9x
```

The program is `build/win9x/aitd.exe`, a 32 bit Windows program. It also
runs on current Windows and under Wine.

## Run

Copy `aitd.exe` into the game folder and start it there. The machine
needs a 486 or better and enough memory to hold the game's resources, as
the original's Windows build did.
