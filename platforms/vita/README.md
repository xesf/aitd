# PlayStation Vita

The Vita build stretches the frame 4:3 into the 960 x 544 screen, plays
sound on one audio port and reads the buttons and the left stick. Each
game is its own package with its own LiveArea bubble. It needs a Vita
with HENkaku or Ensō.

This build compiles, but it has not been run on a Vita or in Vita3K yet.

## Build

Install [VitaSDK](https://vitasdk.org) in `~/vitasdk`, or set `VITASDK`,
then:

```sh
make vita
```

This writes one package per game to `build/vita/`:

| Package | Title ID | Data folder |
|---|---|---|
| `aitd-INDARK.vpk` | AITD00001 | `ux0:data/aitd/INDARK` |
| `aitd-JACK.vpk` | AITD00002 | `ux0:data/aitd/JACK` |
| `aitd-INDARK2.vpk` | AITD00003 | `ux0:data/aitd/INDARK2` |
| `aitd-INDARK3.vpk` | AITD00004 | `ux0:data/aitd/INDARK3` |
| `aitd-TIMEGATE.vpk` | AITD00005 | `ux0:data/aitd/TIMEGATE` |
| `aitd-KNIGHTS.vpk` | AITD00006 | `ux0:data/aitd/KNIGHTS` |

## Install

Install each package with VitaShell, then copy the installed game files
into its data folder. Settings that other platforms take from `AITD_*`
variables go into `aitd.env` in the data folder, one `NAME=VALUE` per
line.

## Controls

The same as on the PSP: see [../psp/README.md](../psp/README.md#controls).
