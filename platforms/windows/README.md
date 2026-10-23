# Windows

The Windows build draws through GDI, plays sound through `waveOut`, plays
MIDI music on the system synthesizer through MCI, and reads Xbox-style
gamepads through XInput. It runs on Windows 7 and later.

## Build

The build cross-compiles with MinGW-w64, from macOS or Linux:

```sh
brew install mingw-w64                        # macOS
sudo apt install gcc-mingw-w64-x86-64         # Debian, Ubuntu
make windows
```

The program is `build/windows/aitd.exe`. It needs no DLLs.

## Run

Put `aitd.exe` in the game folder, or set `AITD_DATA_DIR`. The window opens
at 640 x 480; `AITD_WINDOW_SCALE=2` doubles it.

The CD releases play their music from a BIN/CUE image in the data folder,
or from ripped tracks named `track02.wav` (or `.mp3`) and so on.

## Gamepad

The pad layout is the same on every platform; see
[the Linux notes](../linux/README.md#gamepad).

## Windows 95 and 98

`make win9x` builds the same backend with Open Watcom for the older
systems, without XInput. See [../win9x/README.md](../win9x/README.md).
