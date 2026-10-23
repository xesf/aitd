# The browser

The web build is the engine compiled to WebAssembly with Emscripten. It
runs in current versions of Chrome, Edge, Firefox and Safari, and needs no
special server headers.

## Build

Install [Emscripten](https://emscripten.org) (3.1.50 or later), make `emcc`
available, then:

```sh
make web          # writes build/web/: index.html, aitd.js, aitd.wasm
make web-serve    # serves it on http://localhost:8080
```

`build/web/` is a static site. Copy it to any web host.

## Import a game

Open the page and select **Import game folder…**. Pick the folder of an
installed game: Alone in the Dark 1, 2 or 3, Jack in the Dark, or Time
Gate. The page copies the game files into the browser's storage
(IndexedDB) and leaves out disc images and programs. The files are not
uploaded anywhere. After the import, select **Play**.

Saves are written into the same storage, so they stay between visits.
**Remove** deletes a game and its saves.

## Controls

The keys are the same as on the desktop: the arrows walk, Space acts,
Enter opens the inventory, Esc opens the menu, and 1 to 0 and - choose
an action. A gamepad works too, with the layout in
[the Linux notes](../linux/README.md#gamepad).

## Sound and music

Sound starts with the game. Time Gate's music is MIDI, which the page
plays from a General MIDI soundfont: put a `.sf2` file in the game folder
before you import it. The CD music of Alone in the Dark 3 and of the CD
release of Alone in the Dark 1 plays from ripped `track02.wav` files and
so on, as disc images are not imported.

## How it works

The engine keeps its own loops. Emscripten's ASYNCIFY lets it hand
control back to the browser from `platform_delay` and every few
milliseconds from the event poll, so the page keeps drawing and receiving
input. `src/platforms/web.c` is the backend.
