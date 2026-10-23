# Third-party code

These headers are copied unchanged from their upstream projects. They keep
their own licences, which are compatible with the GPL that covers the rest
of the engine.

| File | Project | Licence |
|---|---|---|
| `tsf.h` | TinySoundFont by Bernhard Schelling | MIT |
| `tml.h` | TinyMidiLoader by Bernhard Schelling | zlib |

The hosts without a system MIDI synthesizer use them to play the Time Gate
music from a General MIDI soundfont (`.sf2`) that you provide.
