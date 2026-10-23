# iOS and iPadOS

The iOS build runs the engine on its own thread while UIKit keeps the
main thread. It draws the frame 4:3 in the middle of the screen, plays
sound through an AudioQueue, and reads an on-screen pad, game controllers
and hardware keyboards. It needs iOS 15 or later.

This build compiles for the simulator and for devices, but it has not
been run on either yet.

## Build

You need Xcode with its command line tools.

```sh
make ios-sim      # build/ios-sim/AITD.app, signed for the simulator
make ios          # build/ios/aitd.ipa, unsigned, for sideloading
```

Sideloading tools (AltStore, Sideloadly) sign the IPA with your own
Apple ID when they install it.

## Install a game

Open the Files app, go to **On My iPhone** (or iPad) › **AITD**, and copy
the folder of an installed game there, for example `INDARK3`. You can add
several games; the app asks which one to play when it starts.

## Controls

| On screen | Action |
|---|---|
| Stick (left) | Walk and turn |
| A | Do the selected action |
| B | Back, options menu |
| X | Inventory |
| Y | Map (Alone in the Dark 3) |
| L, R | Previous or next action |
| Menu | Options menu |
| Pause | Pause |

Game controllers use the same layout, and a hardware keyboard has the
desktop keys.

## Music

Time Gate's MIDI music needs a General MIDI soundfont (`.sf2`) in the
game's folder.
