# File formats

This page describes the data files the engine reads. Values are little
endian unless a section says otherwise. `u16` and `s16` are unsigned and
signed 16 bit words, `u32` a 32 bit word. Offsets are in bytes from the
start of the record unless noted.

World coordinates are in engine units. Room positions and some zone
coordinates are stored in tenths of that scale; this page calls those
"tenths".

The source file that reads each format is named in its section.

## PAK containers

`src/pak.c`, `src/explode.c`

Almost every data file is a PAK container: a directory of entries, each
stored or compressed.

```
u32 offsets[]       slot 0 is unused; entry N is at offsets[N + 1].
                    The table ends at the first zero after slot 0.

at each offset:
u32 reserved        0
u32 packed_size     bytes of payload on disk
u32 unpacked_size
u8  method
u8  flags           implode options (method 1)
u8  extra_skip      bytes to skip after this header before the payload
u8  unused
```

| Method | Meaning | Used by |
|---|---|---|
| 0 | stored | all games |
| 1 | PKWARE implode | AITD1, AITD2, Jack |
| 4 | raw deflate (RFC 1951) | AITD3, Time Gate |

For method 1, `flags` bit 1 selects the 8 KB window (7 low distance bits
instead of 6) and bit 2 a literal tree (minimum match 3 instead of 2).

Some AITD2 entries open with a descriptor block before their data.
`extra_skip` covers it.

## Floors and rooms

`src/world.c`

A floor is a set of rooms and a set of cameras. The games store them in
three layouts:

| Layout | Games | Rooms | Cameras |
|---|---|---|---|
| ETAGE blob | AITD1, AITD2, Jack | `ETAGE##.PAK` entry 0 | `ETAGE##.PAK` entry 1 |
| SAL and CAM | AITD3 | `SAL##.PAK`, one room per entry | `CAM##.PAK`, one camera per entry |
| ETAGE and CAMSAL | Time Gate | `ETAGE##.PAK`, one room per entry | `CAMSAL##.PAK`, one camera per entry |

In the blob layout, each entry starts with a table of `u32` offsets to
its records. The table ends where the offsets stop rising.

### Room record

```
+0   u16  offset of the hard collision list
+2   u16  offset of the scene zone list
+4   s16  x, y, z of the room, in tenths
+10  u16  camera count
+12  u16  camera indexes, one per camera
```

Both lists start with a `u16` count, followed by 16 byte records:

```
+0   s16  x1, x2, y1, y2, z1, z2    box in room coordinates
+12  s16  parameter
+14  s16  type
```

Hard collision types:

| Type | Effect |
|---|---|
| 3 | blocks; `HARD_COL` reads 255 |
| 9 | blocks; `HARD_COL` reads the parameter |
| 10 | blocks every actor except the player |
| other | blocks |

Scene zone types:

| Type | Effect |
|---|---|
| 0 | room link: an actor entering moves into room `parameter` |
| 8 | sets the actor's floor material |
| 9 | sets `HARD_DEC` |
| 10 | starts the object's zone life script (object field `+0x24`) |

### Camera record

```
+0   s16  alpha, beta, gamma      camera angles (1024 per turn)
+6   s16  x, y, z                 position, in tenths
+12  s16  z offset                added to depth before projection
+14  s16  focal x, focal y
+18  u16  view count
+20  views                        one record per room the camera shows
```

The view record size depends on the game:

| Offset | AITD1 (12 bytes) | AITD2, AITD3, Jack (16 bytes) | Time Gate (22 bytes) |
|---|---|---|---|
| +0 | room | room | room |
| +2 | cover list | mask zones | mask zones |
| +4 | camera zones | camera zones | camera zones |
| +6 | - | background animation zones | background animation zones |
| +8 | - | view rectangles | view rectangles |
| +10 | - | - | light direction x, y, z |
| +16 | - | - | light position x, y, z |

Each list offset is from the start of the camera record.

- **Camera zones** pick the camera. The list is a `u16` polygon count,
  then per polygon a `u16` point count and `(x, z)` points in tenths. The
  engine selects a camera when the followed actor's centre is inside one
  of its zones for the actor's room.
- **View rectangles** decide whether an object in that room is drawn: a
  `u16` count, then `(x1, z1, x2, z2)` rectangles.
- **Mask zones** decide which masks cover an actor: a `u16` zone count,
  then per zone a `u16` rectangle count and `(x1, z1, x2, z2)` rectangles
  in tenths. Mask N of the view belongs to zone N.
- **Background animation zones** tell which actors a background animation
  draws in front of, one rectangle list per animation.
- **Cover lists** (AITD1) combine zones and masks: a `u16` entry count,
  then per entry a `u16` rectangle count, a `u16` offset to a polygon list
  and the rectangles. The polygon list is a `u16` count, then per polygon
  a `u16` point count and `(x, y)` screen points. When an actor's box lies
  inside a rectangle, the background inside the polygons is drawn back
  over it.

## Backgrounds

`src/world.c`

`CAMERA##.PAK` holds one picture per camera of floor `##`, indexed by the
camera number in the room record.

| Game | Entry layout | Palette |
|---|---|---|
| AITD1 | 64000 pixels | `ITD_RESS.PAK` entry 3, shared |
| AITD2, Jack | 64000 pixels, 768 byte palette | 6 bit VGA values |
| AITD3 | 4 byte header, 64000 pixels, 768 byte palette | 8 bit values |
| Time Gate DOS | 64000 pixels, 768 byte palette | 8 bit values |

### Windows Time Gate

`CAMERA##.PAK` (320 x 200) and `CAMERX##.PAK` (640 x 400) start with an
index in entry 0: three `u32` per camera.

```
+0  picture entry
+4  mask count
+8  second picture entry, or 0
```

Entry `picture + 1` is a light table: 24 bytes per viewed room. The first
12 bytes replace the light of that view record (`+10` to `+21`). The
masks follow from entry `picture + 2`.

### Altered cameras in Time Gate

When one of the game variables 75 to 82 is set, some cameras of some
floors load from a second set of files: `CYMERA##.PAK` (`CYMERX##.PAK` at
640) instead of `CAMERA##.PAK`, and `NK` instead of `MK` mask files in
DOS. `s_alt_cameras` in `world.c` lists the variable, floor and cameras.

### Time Gate lights in DOS

`LGHT<floor><camera>.GRD` holds 24 bytes per viewed room. The first 12
replace the light of the view record. When the file is missing, the
lights are set to zero.

## Masks

`src/draw.c`

| Game | Container |
|---|---|
| AITD2, AITD3, Jack | `MASK##.PAK`, one entry per camera |
| Time Gate DOS | `MK<floor><camera>00.PAK`, ten entries per viewed room |
| Time Gate Windows | inside the camera file, see above |

A `MASK##.PAK` entry starts with a `u32` offset per viewed room. Each of
those points to a `u32` offset per mask zone. Offsets are relative to the
table that holds them.

A mask:

```
+0   u16  x1        first column
+2   u16  y1        first row
+4   u16  x2
+6   u16  y2        last row
+8   u16  width
+10  u16  rows
then per row:
     u16  span count
     spans of (skip, copy)
```

`skip` and `copy` are bytes, or `u16` values in the 640 build of Time
Gate. Drawing a mask copies the `copy` pixels from the background to the
screen. In AITD2, AITD3 and Jack the next row starts at the current
position minus `width`. In Time Gate each row starts again at the row's
first column.

## Bodies

`src/r3d.c`, `src/body.c`

`LISTBODY.PAK` (and `LISTBOD2.PAK` in AITD1, the second hero) holds the 3D
models.

```
+0    u16  flags
+2    s16  x1, x2, y1, y2, z1, z2      bounding box
+14   u16  size of the extra block
+16   extra block
      u16  point count
      s16  x, y, z per point
if flags & 2 (animated):
      u16  bone count
      u16  offset of each bone record
      bone records
      u16  primitive count
      primitives
```

A static body has no bone section: the primitive count follows the
points.

Flags:

| Bit | Meaning |
|---|---|
| 1 | sort primitives by depth |
| 2 | animated: has bones |
| 4 | not drawn |
| 8 | Jack only: 24 byte bone records with their own rotation |
| 0x40 | Time Gate: a light normal per vertex after the points |
| 0x80 | Time Gate: texture coordinates and two texture pages |

In an animated body, the extra block keeps the animation state while the
body is in use: `+0` the key frame being left and `+4` the time it was
entered.

Bone record, 16 bytes:

```
+0   u16  offset of its first point (bytes into the point array)
+2   u16  point count
+4   u16  offset of the pivot point
+6   u8   parent bone
+7   u8   this bone's index
+8   s16  type: 0 rotate, 1 translate, 2 zoom (factor = value + 256, 8.8)
+10  s16  three values: angles, offset or zoom factors
```

A primitive starts with a type byte:

| Type | Name | Layout after the type byte |
|---|---|---|
| 0 | line | `u8` fill, `u8` colour, `u8`, `u16` point, `u16` point |
| 1 | polygon | `u8` point count, `u8` fill, `u8` colour, `u16` points |
| 2 | point | `u8` fill, `u8` colour, `u8`, `u16` point |
| 3 | sphere | `u8` fill, `u8` colour, `u8`, `u16` size, `u16` point |
| 6 | big point | as type 2 |
| 7 | "zixel" | as type 2 |
| 8 | textured triangle (Time Gate) | `u8` 3, `u16` material, `u16` points |
| 9 | Gouraud triangle (Time Gate) | as 8, then `u16` normals |
| 10 | lit textured triangle (Time Gate) | as 9 |

Point references are byte offsets into the point array, so point N is
`N * 6`. Polygon fills: 0 flat, 1 noise, 2 transparent (keeps the low
nibble of the pixel), 3 and 6 a horizontal ramp over 16 shades, 4 and 5 a
vertical ramp.

For types 8 to 10, the low six bits of the material select the texture
page, and the upper ten bits select a 12 byte UV triple. For type 9 the
high byte is the colour.

## Animations

`src/anim.c`

`LISTANIM.PAK` (and `LISTANI2.PAK` in AITD1).

```
+0   u16  frame count
+2   u16  bone count
+4   frames
```

A frame is `(bones + 1) * 8` bytes:

```
+0   u16  duration, in 1/60 s ticks
+2   s16  step x, y, z     how far the actor moves during the frame
+8   per bone: s16 type, s16 three values
```

Jack bodies with flag 8 use `bones * 16 + 8` byte frames. The engine
interpolates bone values between frames by elapsed time.

## Objects and variables

`src/world.c`

`OBJETS.ITD` is a `u16` count followed by one record per object: 54
bytes, or 52 in AITD1, which has no `mark` field.

| Offset | Field | Offset | Field |
|---|---|---|---|
| 0x00 | actor slot | 0x1c | floor |
| 0x02 | body | 0x1e | room |
| 0x04 | flags | 0x20 | life mode |
| 0x06 | box type | 0x22 | life script |
| 0x08 | found body | 0x24 | zone life script |
| 0x0a | found name | 0x26 | animation |
| 0x0c | found flags | 0x28 | frame |
| 0x0e | found life script | 0x2a | animation type |
| 0x10 | x, y, z | 0x2c | next animation |
| 0x16 | alpha, beta, gamma | 0x2e | track mode |
| | | 0x30 | track number |
| | | 0x32 | position in track |
| | | 0x34 | mark |

`VARS.ITD` is an array of `s16` script variables. `DEFINES.ITD` is an
array of big endian 16 bit game variables: 45 words in AITD1, 70 in the
other games.

## Life scripts and tracks

`LISTLIFE.PAK` holds one script per entry, and `LISTTRAK.PAK` one track
per entry. Both are arrays of `s16` words. [scripting.md](scripting.md)
describes them.

## Sprites (HYB)

`src/hyb.c`

`LISTHYB.PAK`, `ANIM##.PAK` (background animations) and some interface
graphics use this vector sprite format.

```
+0  u32  offset of the sprite table
+4  u32  offset of the bitmap table
+8  u32  offset of the animation table
```

Each table is `u32` offsets, relative to the table. A sprite is a `u8`
flag byte (bit 7 mirrors it), a `u8` element count, then elements:

```
u8 type, u8 point count, u8 colour 1, u8 colour 2, (s16 x, s16 y) points
```

| Type | Element |
|---|---|
| 0 | another sprite, index in the colour bytes, placed at point 0 |
| 1 | an RLE bitmap, index in the colour bytes, bottom left at point 0 |
| 4 | polygon filled with colour 2, outlined with colour 1 unless it is 0xff |
| 5 | rectangle between two points, filled and outlined the same way |
| 6 | polyline in colour 2 |
| 7 | points in colour 2 |

An animation in `ANIM##.PAK` is a byte, a `u8` frame count, then 8 byte
frames: `u16` sprite and `u16` duration in ticks.

## Text and fonts

`src/text.c`, `src/font.c`

The text container (`TEXTES.PAK`, `ENGLISH.PAK`, `FRANCAIS.PAK` and so on)
holds lines of the form `@<id>:<text>`, ended by CR LF.

AITD3 and Time Gate use a byte-per-pixel font in `ITD_RESS.PAK`: an 8
byte header, then a 10 byte record per glyph (`u32` pixel offset, `u8`
width, `u8` height, `s8` x and y offsets, `u8` advance, `u8` padding),
then the pixels. AITD1, AITD2 and Jack use a 1 bit font with a character
table of widths and bit offsets.

## Saves

`src/savegame.c`

Saves are `SAVE0<directory>/SAVE<slot>.ITD`. The file starts with six big
endian `u32` offsets:

| Offset | Points to |
|---|---|
| 0x00 | thumbnail, 0xfa0 bytes |
| 0x04 | palette, 0x300 bytes |
| 0x08 | name, 0x20 bytes |
| 0x0c | state blocks, one after another |
| 0x10 | `u16` size of the variables, then the variables |
| 0x14 | actor table, 0x238c bytes |

The state blocks and variables are XOR 0x35. The blocks are the
difficulty, room, floor, camera, player, objects, `DEFINES.ITD` words,
inventories, game clock, music and the saved actor boxes. `NOTES.md`
lists them in order.

## Music

`src/music.c`, `src/hmp.c`

### ADLM (AITD1, AITD2, Jack)

`LISTMUS.PAK` entries start with `ADLM`. At `+8`, one `u32` offset per
voice (11 voices, 0 for unused) to its command stream. At `+0x34` is the
instrument bank offset, at `+0x3c` the rhythm register bits, and at
`+0x3d` nonzero for nine melodic channels or zero for six melodic plus
five rhythm voices. An instrument is 13 bytes: one unused byte, then two
operators of six register values.

Commands are byte pairs, opcode then argument. An opcode with bit 7 set
ends the batch for this tick (about 59 a second).

| Opcode | Command |
|---|---|
| 0 | end of stream: loop or stop |
| 1 | wait, with one more byte |
| 2 | note: semitone in bits 0 to 3, octave in bits 4 to 6, bit 7 rest |
| 3 | instrument |
| 4 | attenuation |
| 5 | pitch bend |
| 6 | callback to the game |

### HMP (Time Gate DOS)

`MUSIC.PAK` holds two AdLib instrument banks, then HMI `HMIMIDIP` songs.
The track count is at `+0x30`, and the tracks start at `+0x388`. Each
track has a 12 byte header (number, length including the header,
channel), then MIDI events with running status. Delta times are groups of
7 bits, least significant first, and bit 7 marks the last group. Time
runs at 120 ticks a second.

### MIDI (Time Gate Windows)

`WINMUS.PAK` entries are standard MIDI files.

## Films

`src/smacker.c`, `src/mux.c`

Windows Time Gate plays Smacker 2 files (`ANIM2D/AN<n>.SMK`).

DOS Time Gate plays `AN<n>.MUX`:

```
+0x0e  u16  sound sample rate
+0x10  u16  frame count
+0x12  u16  frames per second
+0x1c  u16  width, height
+0x20  u32  per frame: offset in the low 24 bits, 0xc0 in the top byte
```

A frame is a run of chunks: `u8` type, `u8`, `u32` size, then the data.

| Type | Chunk |
|---|---|
| 0x70, 0x65 | palette, 768 bytes |
| 0x6f, 0x64 | picture |
| 0x05 | frame number |
| 0x06 | 8 bit unsigned sound |

A picture chunk has a flag byte, the start offset, the sizes of a
control stream and a literal stream, then the streams. Flag 1 packs the
literals with a byte LZ, flag 4 packs both streams with a bit LZ, and
flag 2 marks a change frame that can skip pixels. Control bits choose
between copying a literal and repeating one 2, 3, 4 or more times.
