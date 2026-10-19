# WAITD3.EXE — reverse engineering notes

Target: `WAITD3.EXE`, 260096 bytes, dated 1996-03-27. Win32 PE, built with
Watcom C/C++. Image base `0x400000`. Analysis done against the IDA database in
this directory; all function and global renames below are applied in the IDB.

Calling convention is Watcom register-based: `eax, edx, ebx, ecx`, then stack.
Argument order in the reconstructed C follows that register order.

## Entry chain

| Address | Name | Role |
|---|---|---|
| `0x425bce` | `start` | stores `&WinMain` in `0x44b2bc`, jumps to CRT startup `0x42a81c` |
| `0x41e9e1` | `_WinMain` | `SetMessageQueue(120)`, records instance, splits the command line into seven 0x80-byte buffers at `0x445728`, tail-calls `game_main` |
| `0x4078b7` | `game_main` | the real entry point |

`WinMain`'s command-line splitting is dead weight on a POSIX host — `argc`/`argv`
arrive already split.

## `game_main` (0x4078b7)

1. `0x4078dd` — `sys_is_already_running()` (`FindWindowA`); `exit(1)` if true.
2. `0x4078f0` — clear `0x1fb8` bytes at `g_game_vars` (`0x4423a4`).
3. `0x407923` — argument scan. Options take `-` or `/`, letter lowercased:
   - `c` → `g_opt_cdaudio_enabled = 0` (`0x407986`)
   - `s` → `g_opt_sound_enabled = 0` (`0x40798f`)
4. `0x407998` — `engine_init()`.
5. `0x4079af` — `game_run(1, 0, 0)`, the intro.
6. `0x4079bd` — infinite dispatch loop over `main_menu_run()`.

`main_menu_run` (`0x408f34`) result codes, from the compare chain at `0x4079c5`:

| Value | Branch | Meaning |
|---|---|---|
| `-1` | `0x4079cc` | replay intro |
| `0` | `0x407a0f` | new game |
| `1` | `0x407a67` | load save |
| `2` | `0x4079ea` | difficulty level |
| `3` | `0x407af6` | quit to desktop |

Every branch either loops back into the menu or calls `engine_shutdown()` and
`exit(-1)`. The loop has no normal exit.

## `engine_init` (0x4071e1)

The single most informative routine in the binary — it names every asset family
and fixes the memory layout.

| Address | Step |
|---|---|
| `0x4071fe` | load font from `itd_ress.pak` entry 1; fatal on failure (literal string, the text table is not up yet) |
| `0x407234` | `font_set_data`, `font_set_spacing(1)`, `font_set_color_table(0x44485c)` |
| `0x40724d` | text table init (`0x414e53`) |
| `0x407260` | icon `"AppIcon"`, cursor |
| `0x40727e` | window title from text id `0x138d` |
| `0x407298` | create window, `0x140` × `0xc8`; failure reported with text id `0x1388` |
| `0x4072d1` | input, window show, timer, video mode query |
| `0x4072e5` | `g_screen_width` / `g_screen_height` from `g_video_mode_info` (`0x435160`) |
| `0x407300` | heap init, rand init, palette init |
| `0x407319` | CD audio, 5 attempts, retry prompt `0x138b`, fatal `0x138c` — skipped by `-c` |
| `0x407393` | `voice.cfg` → `atoi` → `g_voice_cfg_value`; missing file is not an error |
| `0x4073bb` | digital sound, skipped by `-s` |
| `0x4073e0` | allocate `0x12a0`, `0x2ba6` (object table), `0x238c` blocks |
| `0x407418` | constructor dispatched through `[object_table + 0x2b7e]` = `0x419129` |
| `0x407440` | `DEFINES.ITD` read as a flat `0x46`-byte record |
| `0x407483` | two frame buffers of `w*h + 0x302` bytes each |
| `0x4074e5` | `0xcd00`-byte anim buffer, fatal on failure |
| `0x40751b` | resource caches (table below) |
| `0x407608` | six more `itd_ress.pak` entries |

### Frame buffers

`w*h + 0x302`. The `0x302` tail after the pixel plane carries the 768-byte VGA
palette plus 2 bytes. Front buffer `0x435064`, back buffer `0x444bac`;
`g_blit_source` (`0x444858`) points at the back buffer.

### Resource caches — `hqr_init_resource(eax=name, edx=bytes, ebx=entries)` @ `0x4195fc`

| Name | Bytes | Entries | Global | Site |
|---|---|---|---|---|
| `ListSamp` | `0x186a0` | 20 | `0x444a98` | `0x407554`, only if sound came up |
| `ListMat` | `0xfa00` | 5 | `0x444aa8` | `0x40756d` |
| (unnamed) | `0x4e20` | 50 | `0x444a8c` | `0x407581` via `0x41991a` |
| `ListLife` | `0xfde8` | 100 | `0x444a94` | `0x40759a` |
| `ListTrak` | `0x4e20` | 100 | `0x444a84` | `0x4075b3` |
| `ListHyb` | `0x4e20` | 10 | `0x444a88` | `0x4075cc` |
| `ListBody` | `0x9088` | 50 | `0x444a80` | `0x4075e5` |
| `ListAnim` | `0x7530` | 80 | `0x444a9c` | `0x4075fe` |

`itd_ress.pak` entry indices pulled at `0x407608`–`0x40767b`: `0x13`, `0x12`,
`0x14`, `0x0f`, `0x0e`, `0x10`. The font is entry `1` (see Corrections).

## `engine_shutdown` (0x407687)

Music → sound → sound release → CD audio → video → free → window destroy.
Callers always follow with `exit(-1)`.

## Font blob layout — from `font_set_data` (0x420eea)

| Offset | Field |
|---|---|
| `0` | flags → `0x43522e` |
| `2` | first char → `0x43522c` |
| `3` | last char → `0x43522d` |
| `4` | unidentified word → `0x435224`; reads `0x0e0d`, so not a glyph height |
| `8` | glyph table → `0x435228`, 10 bytes per glyph (stride from `0x420f3a`) |

Returns the previous blob, so it doubles as a save/restore pair.
`font_set_color_table` and `font_set_spacing` are single-instruction `xchg`
accessors over `0x435220` and `0x435226`.

## Renamed primitives

| Address | Name | Evidence |
|---|---|---|
| `0x420b28` | `mem_alloc` | malloc |
| `0x420b58` | `mem_free` | tail-calls `_nfree_` |
| `0x402b6f` | `mem_alloc_checked` | alloc + `fatal_error` |
| `0x4145ec` | `fatal_error` | reached by tail jump only, never returns |
| `0x419d92` | `sys_error_box` | `MessageBoxA` |
| `0x41a152` | `sys_is_already_running` | `FindWindowA` |
| `0x401010` | `cdaudio_init` | `mciSendStringA` |
| `0x4203d5` / `0x420728` / `0x420421` | `file_open` / `file_read` / `file_close` | used as a triple on `DEFINES.ITD` |
| `0x42025a` | `file_load_alloc` | open, size, alloc, read, close |
| `0x420a0e` | `file_load_text_alloc` | same, result fed to `atoi` |
| `0x42e8cf` | atoi | `_IsTable` whitespace skip, sign, digit accumulate |
| `0x414fcb` | `text_get_by_id` | no callees, string at record + 2 |
| `0x414760` | `load_malloc_hqr` | wraps `file_load_alloc`; lib386 `LoadMalloc_HQR` |

## Reconstruction

| File | Contents |
|---|---|
| `src/aitd.h` | types, globals, prototypes, enums for text ids and menu results |
| `src/platform.h` | host abstraction — one entry point per Win32 touch point |
| `src/main.c` | `game_main`, argument parsing, menu dispatch |
| `src/engine.c` | `engine_init` / `engine_shutdown` |
| `src/resource.c` | memory, files, .PAK entries, never-evicting HQR caches |
| `src/pak.c` | .PAK container reader and method dispatch |
| `src/game.c` | per-title profiles and data-directory detection |
| `src/font.c`, `src/text.c` | bitmap font (0x420eea..) and the text table (0x414fcb) |
| `src/palette.c` | palette upload and fades |
| `src/input.c` | keyboard to scancode / direction bits (0x402c67), timers |
| `src/menu.c`, `src/menu_backend.c` | main menu (0x408f34) |
| `src/raster.c` | 2D primitives: polygon filler with all fill types, lines, rects, blits |
| `src/r3d.c` | the Adeline 3D library: camera, projection, body drawing |
| `src/anim.c` | keyframe animation, game clock (`ticks_*`) |
| `src/hyb.c` | hybrid 2D vector sprites and RLE bitmaps |
| `src/world.c` | floors, rooms, cameras, backgrounds, object/actor tables |
| `src/actor.c` | movement, tracks, collision, falling, camera selection |
| `src/action.c` | attack / throw / fire states (0x41317b) |
| `src/life.c` | life script interpreter (102 opcodes) and evaluator |
| `src/draw.c` | sort, static/dynamic actor drawing, masks, background anims |
| `src/special.c` | particle and one-shot effect actors |
| `src/sound.c` | VOC samples, CD music, per-frame audio service |
| `src/ui.c`, `src/inventory.c` | messages, "object found" dialog, inventory |
| `src/escape.c` | in-game options menu (0x408545) |
| `src/savegame.c` | save file format, save / load screens |
| `src/loop.c` | `game_run` (0x4077f0) and the scene loop (0x407c7c) |
| `src/cutscene.c` | AN##.PAK animations (0x4025a3) and opcode 71 pictures |
| `src/reader.c` | documents and books (0x4156a6, 0x4160b6, opcode 98) |
| `src/map.c` | the Tab map (0x4019ff) |
| `src/stubs.c` | engine bring-up helpers with no behaviour on a host |
| `src/platform_macos.m` | AppKit + CoreGraphics + AudioToolbox backend |
| `tools/pak_dump.c` | offline container inspector, `--verify` decodes everything |
| `tools/extract_tables.py` | pulls the sine, noise and ramp tables out of the EXE |
| `tools/sync_ida_names.py` | renames the IDA database after the C sources |

The core is C99 with no host dependencies. The macOS backend hands the 8bpp
indexed plane to CoreGraphics through `CGColorSpaceCreateIndexed`, which takes
the palette directly and avoids a per-frame RGBA conversion. Palette entries
are 8-bit per channel and go through unmodified — see the palette section for
why no DAC widening applies.

`build`: `make`. Run needs `AITD_DATA_DIR` pointing at the original assets;
without them `engine_init` fails at the font load exactly as the original does.
Set `AITD_NO_DIALOGS=1` for scripted runs: errors go to stderr instead of a
modal alert that would block forever.

### Enums

Opcode and type numbers are named where they are defined:
`life.h` (`life_op`, the 102 interpreter opcodes; `life_var`, the 39
evaluator variables), `world.h` (`anim_type`, `action_type`, `track_mode`,
`zv_type`, `life_mode`, `scene_zone_type`, `hardcol_type`), `actor.h`
(`track_op`, TRAK opcodes), `r3d.h` (`prim_type`, `bone_type`), `hyb.h`
(`hyb_element`), `draw.h` (`special_type`), `aitd.h` (`itd_ress` entries,
`text_id`), `menu.h` (scancodes and key bits). Names follow the classic
AITD opcode names where the behaviour matches and describe the observed
behaviour otherwise.

### Harness

`make run-<game>` (aitd1, jack, aitd2, aitd3, timegate, knights; `ARGS=`,
`RES=320|640`) runs one title from its `data/` folder and `make check` runs
all of them unattended. `./aitd3 --stage=game` enters the world directly (`AITD_FLOOR`, `AITD_ROOM`,
`AITD_FRAMES` frame budget). Scripted input and capture:

| Variable | Effect |
|---|---|
| `AITD_INPUT="a-b:keys,..."` | hold keys during input polls a..b: `u d l r` arrows, `s` space, `c` ctrl, `e` enter, `x` esc, `t` tab |
| `AITD_DUMP_EVERY=n` + `AITD_DUMP_FRAME=prefix` | PPM of the presented screen every n polls |
| `AITD_TRACE=1` | per-frame player state on stderr |
| `AITD_LIFETRACE=1` | life opcodes as they run |
| `AITD_NOLIFE`, `AITD_CAM=n` | freeze scripts / force a camera |
| `AITD_OPEN_INVENTORY=n` | open the inventory at frame n |
| `AITD_TIMEOUT=s` | leave after s seconds, even inside a modal loop |
| `AITD_SKIP_DOCUMENTS=1` | documents and books return at once |
| `AITD_READ_DOCUMENT=f,text,type` | open a document at frame f |

Input is indexed by poll, not by frame, so scripts also drive blocking
dialogs (inventory, escape menu, save screens). A key held across a
`wait for release` loop keeps that loop spinning: release keys between steps.

## .PAK container format

Confirmed against all 135 containers in `data/`.

```
u32 offsets[]          directory, terminated by a zero entry.
                       Slot 0 is a dummy, so entry N is at offsets[N+1] —
                       hence the `index * 4 + 4` seek at 0x40229b.

per entry, at its offset:
  u32 reserved         always 0 in shipped data; skipped at 0x4202c1
  u32 packed_size      bytes on disk after the header
  u32 unpacked_size
  u8  method
  u8  unused
  u8  extra_skip       further bytes to skip before the payload (0x4202e6)
  u8  unused
```

`pak_unpack` (`0x425dca`) dispatches the method byte:

| Method | Handler | Meaning |
|---|---|---|
| 0, 1 | `0x425e0b` | stored, plain `memmove` |
| 2, 3 | `0x42a8f0` | legacy escape-marker LZ |
| 4 | `0x42bb75` | raw deflate |

### Method 4 is raw deflate

`inflate_block` (`0x42ba93`) reads one bit into the caller's loop flag, then
two bits selecting between three block readers — stored (`0x42b2d0`), fixed
Huffman (`0x42b3eb`), dynamic Huffman (`0x42b5c7`) — and rejects the fourth
encoding. That is RFC 1951 exactly, LSB-first, with the final-block bit as the
loop terminator. Bitstream state lives at `0x44b2cc` (src), `0x44b2d0` (dst),
`0x44b2d4` (accumulator), `0x44b2d8` (bit count).

Verified: a stock raw inflate decodes **11296 of 11296** entries across every
shipped container to exactly the recorded `unpacked_size`. The reconstructed
C reader reproduces this — `./pak_dump --verify` reports 135 containers, 0
failures.

The original reads the payload into the *tail* of the destination buffer and
expands forward over it (`0x420332`..`0x42035d`), allocating `0x7d0` bytes of
slack for method 4. `src/pak.c` uses a separate payload buffer instead.

### Method 2/3 not implemented

`0x42a8f0` is an escape-marker LZ: the first byte is the marker, a following
zero emits the marker literally, and the high bit of the control byte selects
between two match encodings. Both long forms dispatch through jump tables at
`0x43b4bc` and `0x42a96e`, which have not been recovered. No Alone in the Dark
3 container uses it — it exists for the earlier games.

## lib386 lineage

The engine descends from Adeline's lib386, the same library behind LBA1
(`LIB386/LIB_SYS`). The resource cache is the clearest inheritance:

| WAITD3.EXE | lib386 |
|---|---|
| `hqr_init_resource` `0x4195fc` (name, size, entries) | `HQR_Init_Ressource` — same three arguments |
| `load_malloc_hqr` `0x414760` (name, index) | `LoadMalloc_HQR` |
| `load_malloc` `0x4146ba` | `LoadMalloc` |
| cache block LRU stamp | `T_HQR_BLOC.Time` |

The LRU timestamp in lib386's block record confirms the eviction scheme the
cache sizes at `0x40756d`..`0x4075fe` imply.

**Caution:** lib386's `Expand()` is a control-bit LZ77 (8-bit control byte,
literal or 16-bit back-reference token per bit). AITD3's `0x42a8f0` is an
escape-marker scheme. They are *not* the same algorithm and cannot be
substituted for each other.

## Multi-game support

The data sets live in `data/<GAME>/` (INDARK = AITD1, INDARK2 = AITD2,
JACK, INDARK3 = AITD3, TIMEGATE = Time Gate DOS, KNIGHTS = Time Gate for
Windows with `320/` and `640/` resolution folders). `game.c` detects the
title from a marker file (FITD's markers: LISTBOD2, PERE, MER, AN1,
PURSUIT) and every difference is a profile field. FITD (Free in the Dark,
GPLv2) was used as a reference for facts about the older formats, never
for code; everything taken from it was checked against the data.

| | AITD1 | Jack | AITD2 | AITD3 | Time Gate |
|---|---|---|---|---|---|
| PAK method 1 | implode | implode | implode | - | - |
| floors | ETAGE blob | ETAGE blob | ETAGE blob | SAL + CAM | ETAGE + CAMSAL |
| camera view record | 12 | 16 | 16 | 16 | 22 |
| background | 64000 | +6-bit pal | +6-bit pal | hdr 4, +8-bit pal | +8-bit pal |
| palette | ITD_RESS 3 | camera | camera | camera | camera |
| masks | cover polygons | MASK PAK | MASK PAK | MASK PAK | MK files / in CAMERA |
| object record | 52 | 54 | 54 | 54 | 54 |
| DEFINES words | 45 | 70 | 70 | 70 | 70 |
| font (ITD_RESS) | 5, 1-bit | 1, 1-bit | 1, 1-bit | 1 | 2 |
| bodies | 16 byte groups | 24 byte groups (flag 8) | 16 | 16 | normals 0x40, textures 0x80 |
| life opcodes | own order | shared | shared | shared | shared + 102..118 |

- **PAK entries** may open with a descriptor block (AITD2 masks); method 1
  is PKZIP implode in the older titles (`explode.c`), with the flags byte
  choosing the 8K window and the literal tree. All 1,002 containers of the
  six data sets decode.
- **DEFINES.ITD** is big-endian words (0x420728). The words differ per
  title: AITD1 keeps the player object in 7 and the chosen hero in 8
  (LISTBODY/LISTANIM or LISTBOD2/LISTANI2); AITD2/3 the player object in 13.
- **Life scripts**: `lifemap.c` maps AITD1's opcode order and its evaluator
  variables (34 ROOM_Y, 35 TEST_ZV_END_ANIM, then MUSIC, C_VAR, STAGE,
  THROW) onto the shared numbering. Operand shapes differ per title (SAMPLE,
  REP_SAMPLE, SAMPLE_THEN, IN_HAND, DELETE, READ, FIRE, PLAY_SEQUENCE); a
  checker parses every LISTLIFE script of AITD1, Jack, AITD2 and AITD3 to
  its exact length with them. AITD1 and Jack run every script whose life
  mode is not -1.
- **Optimised bodies** (flag 8, Jack): 24 byte groups with their own
  rotation; WAITD3.EXE cannot draw them, the path follows FITD's renderer.
- **Fonts**: AITD1/AITD2/Jack use a 1-bit font (character table of width
  nibble + bit offset), AITD3 and Time Gate the newer one.

- **Time Gate bodies** (TG320.EX_ 0x4228a9, `r3d.c`, `texture.c`): flag
  0x80 adds a UV table and two TEXTURES.PAK pages (256 texels a row, 0 is
  transparent), flag 0x40 appends one light normal per vertex after the
  points (they follow bone rotations and pivots, never translations or
  zooms, and are never clipped). The extra block holds, for animated /
  static bodies: UV table offset +6 / +0, normal count +8 / +2, normal byte
  offset +0x0a / +4, pages +0x0c, +0x0e / +6, +8. Primitives 8 (textured),
  9 (Gouraud) and 10 (textured and lit) are triangles: count, material
  word, three vertex offsets and for 9/10 three normal offsets. The low six
  bits of the material select the page, the upper ten the 12 byte UV
  triple; for 9 the high byte is the colour. Culling is the sign of the
  camera space determinant (0x4214aa). Levels: `(L.(n - P)) / 8 + 32`,
  0..63 (0x411e2f), with L and P the light of the actor's room in the
  camera's 22 byte view record (+10 direction, +16 position), both put
  through the camera transform. Pixels: 8 texel, 9 `shade[colour][level]`,
  10 `shade[texel][level]`, shade = ITD_RESS 54 transposed (0x417cfe).
  Only the high detail fillers are rebuilt (0x41bc2c, 0x412e4f, 0x4133bc;
  the detail option 0x41c9b3 also offers flat and medium sets), and the
  scan conversion steps in fixed point instead of the original Bresenham
  tables: not verified pixel for pixel.
- **AITD1 cover zones** (facts from FITD): view +2 lists entries of a
  rectangle count, an offset to screen polygons and the rectangles; when
  the actor's box (tenths) lies inside one, the background inside the
  polygons is drawn back over the actor. Messages count drawn frames:
  colour 16 for 26 frames, then 16 + (n - 26) / 2, gone after 56.
- **Start-up**: the intro and new game floor/room come from the profile
  (AITD3 1/0 and 0/12). Sequences: AITD3 AN<n>.PAK, AITD2 a table of named
  PAKs (FITD's order, every number its scripts use is in range), Jack only
  PERE; Time Gate's AN<n>.MUX movies and the Windows Smacker files are not
  rebuilt and are skipped with a log line.
- **Main menus** (`menu_styles.c`): AITD1/AITD2/Jack draw FITD's framed
  three-row menu (old 16 bit offset sprites: AffBigCadre/AffSpf; AITD2's
  palette is ITD_RESS 59 with the interface overlay, Jack's is taken from
  picture 2, an assumption). Time Gate (TG320.EX_ game_main 0x41778f, menu
  0x405f6b) shows ITD_RESS picture 1 with seven HYB icons; 0 new game
  (game_run 0/5), 2 load, 5 difficulty, 6 quit, 3000 idle ticks give -1.
- **Smacker** (`smacker.c`): Time Gate for Windows plays ANIM2D\AN<n>.smk
  (TG 0x409835), AN0 and AN1 before the menu. Smacker 2 video (four big
  Huffman trees, mono/full/skip/fill blocks, palette deltas) and audio
  track 0 (DPCM over byte trees, decoded whole and played as one sample),
  written from the published format description. PLAY_SEQUENCE falls back
  to it when there is no AN<n>.PAK. The DOS build's AN<n>.MUX is unknown.
- **Time Gate difficulty** (TG 0x4062c3): ITD_RESS picture 0x74 (three
  tarot cards), HYB icons 0x60..0x65; the choice sets all three words.
  Ring entry 3 starts Windows' volume control and is left out.
- **Time Gate masks**: same RLE blobs as AITD2/3 (x1, y1, x2, y2, width,
  height, then rows of skip/copy byte pairs, 0x42a478), ten slots per
  viewed room, mask `view * 10 + zone`. The DOS build keeps them in
  MK<floor><camera>00.PAK (NK is an alternate set, not used yet). The
  Windows build (TG320.EX_ 0x4146d1) indexes CAMERA##.PAK through entry 0:
  per camera three u32s (picture entry, mask count, second picture); the
  picture is followed by a light table (24 bytes per viewed room, the
  first 12 replace +10..+21 of the view record, 0x40c7c7) and the masks.
  CYMERA##.PAK replaces CAMERA for some flag-chosen cameras (not yet), and
  CAMERX##.PAK holds the 640 x 400 pictures. A zone draws its mask when the
  actor's box overlaps one of its rectangles (0x42a422), where AITD3 wants
  the box inside.

State: AITD1 (Carnby walking in the attic), AITD2 (the opening gate scene),
Jack (the toy shop street and the credits book) and Time Gate (the museum
street and courtyard, textured hero) start and render. KNIGHTS needs a
640 x 480 screen throughout the engine.

## Main menu (0x408f34)

Four entries, indices `0..3`, matching the `game_main` dispatch exactly.

| Aspect | Detail |
|---|---|
| Idle timeout | `timer_elapsed >= 0x384` (900) → returns `-1` → attract mode, replays intro |
| Mouse hit box | x ∈ (100, 220), y ∈ (79, 135); entry = `(y - 79) / 14` |
| Keys | `g_key_dir_flags` bit 0 = up (wraps 0→3), bit 1 = down (wraps 3→0) |
| Confirm | scancode `0x1c` (Enter) in `g_last_scancode`, or `g_key_confirm` |
| Debounce | every navigation waits for release before continuing |

`MENU_RESULT_INTRO` (`-1`) is therefore not a menu item at all — it is the
idle timeout, which is why `game_main` sends it straight back into the intro.

### Rendering

`menu_draw_entry` (`0x408bdd`): text ids `11..14` (`index + 0x0b`), centred at
x = `0xa0` (160), first row y = `0x4f` (79), pitch `0x0e` (14). Selected row is
drawn one pixel lower via `text_draw_centered_outlined` in colour 1 with
outline 4; the others via `text_draw_centered` in colour 4.

`menu_draw_background` (`0x408eb8`): `menu_draw_backdrop` (`0x40b0f8`) paints
the image, then `pak_load_into("itd_ress.pak", 0x2f, g_front_buffer)` pulls the
menu palette. **Entry `0x2f` is a bare 768-byte palette, not a picture** — the
front buffer is only scratch. It is copied to `g_palette_buffer` (`0x44448c`),
the first 16 colours are overwritten from the static overlay at `0x435068`,
then `palette_set(buffer, 0, 0x100)` realises it.

### Palettes are 8-bit, not 6-bit VGA

The overlay at `0x435068` tops out at `0xfc` and the menu palette in
`itd_ress.pak` reaches `0xff` with values that are not multiples of four. This
is the Win32 port, handing palettes to `CreatePalette` as `PALETTEENTRY`
records, so no DAC widening applies. Applying the DOS-era 6-bit→8-bit widening
here overflows and produces garbage.

## Bitmap font

`font_draw_string` (`0x420c78`) walks the string; `font_blit_glyph`
(`0x420e6f`) does the copy. Blob layout, confirmed against `ITD_RESS` entry 1
(8761 bytes, 206 glyphs):

```
+0   u8  flags            bit 1 suppresses drawing entirely (0x420e82)
+2   u8  first_char       32 in the shipped font
+3   u8  last_char        237
+4   u16 unidentified     reads 0x0e0d; not a glyph height
+8       glyph table      (last_char - first_char + 1) * 10 bytes
         pixel data       follows the table
```

Glyph record, 10 bytes:

| Offset | Field |
|---|---|
| `+0` | u32 absolute offset of pixel data in the blob, 0 = blank |
| `+4` | u8 width |
| `+5` | u8 height |
| `+6` | s8 x offset, subtracted from the pen |
| `+7` | s8 y offset, subtracted from the pen |
| `+8` | u8 advance, added to the spacing word at `0x435226` |
| `+9` | u8 padding, zero throughout the shipped font |

Offsets are absolute because the blitter recovers the base as
`glyph_table - 8`, i.e. the blob start (`0x420e4d`). Verified: space has
`offset 0, w 0, h 0, advance 7` (hence the skip at `0x420d03`), `'!'` starts at
2068 which is exactly the end of the table, and all 134 non-blank glyphs
satisfy `offset + w*h <= 8761`.

Pixels are one byte each, **0 is transparent**. With no colour table the bytes
go down as palette indices; with one installed each byte is remapped through it
first (`0x420ec7`) and a zero result stays transparent. `font_set_colour`
(`0x4029cb`) fills that table, which is what gives a string a single colour —
so `font_set_spacing` (`0x420f4e`) really is inter-character spacing, added to
each glyph's advance.

### Text records

`text_get_by_id` (`0x414fcb`) returns a record with a `char *` at `+2` and the
string's pixel width as an s16 at `+6`. `text_draw_centered` (`0x4150bb`)
subtracts `width / 2` from x. `text_draw_centered_shadowed` (`0x41502f`) takes
the shadow colour as a stack argument, draws it at `y + 1` first, then the face
colour on top — that is the selected menu row, white over brown.

## Display orientation

The view is flipped (`isFlipped = YES`) so mouse coordinates arrive top-down,
matching the engine's framebuffer. AppKit implements that with a y-flipping
CTM, but `CGContextDrawImage` places images bottom-up, so drawing straight into
a flipped view renders every frame upside down — and the input then *looks*
wrong too, because a correct top-down mouse disagrees with a mirrored picture.
One bug, two symptoms.

Fixed by undoing the flip around the image only (translate by height, scale
y by -1) in `drawRect:`, leaving the mouse mapping alone.

An engine-side frame dump cannot catch this, since the bug lives past the
framebuffer. `platform_video_capture` renders the view through
`-cacheDisplayInRect:`, i.e. the real `drawRect:` path, and writes a PPM;
`AITD_CAPTURE=<path>` triggers it from any presenting stage. The `video` stage
paints a solid bar across rows 4..11 only, so orientation is checkable by
position:

| Check | Expected | Measured |
|---|---|---|
| video bar | 0.020..0.055 of height | 0.020..0.059 |
| menu text | 0.395..0.670 of height | 0.405..0.664 |

Captured colours differ from the framebuffer palette (colour management in the
capture path), so orientation checks use position and uniformity, not exact
RGB.

## Text table

`text_get_by_id` (`0x414fcb`) is a linear scan over the table at `0x444ac4`,
bounded at `0x1f4` (500) records of 8 bytes:

| Offset | Field |
|---|---|
| `+0` | u16 id |
| `+2` | `char *` text |
| `+6` | s16 pixel width, used to centre |

The source is `TEXTES.PAK`, whose entries are lines of the form
`@<id>:<text><CR><LF>`. Ids are sparse and grouped by purpose. The container
holds 48 entries — several are language variants, and which one the original
selects is not established yet, so `text_table_init` currently takes entry 0.

Menu rows use ids 11..14, matching `index + 0x0b` in `menu_draw_entry`.

## Palette fading

| Address | Name | Role |
|---|---|---|
| `0x40b0f8` | `palette_blackout` | zeroes 0x300 bytes, pushes them as 256 entries |
| `0x40af57` | `palette_fade` | scales `g_palette_buffer` in or out |
| `0x402d67` | `palette_set_range` | eax=palette, edx=first, ebx=count |
| `0x4200e0` / `0x420128` | `palette_scale_in` / `_out` | scale towards or away from black |
| `0x41faf8` / `0x41fb18` | `palette_update_begin` / `_end` | bracket the update |

`palette_fade` **discards its first argument**: `0x40af74` overwrites `eax`
with `0x80` before it is read, and that becomes the step. Levels therefore run
`0, 0x80, 0x100` — three steps, whatever the caller passes. The menu calls it
as `palette_fade(0x10, 0)` and the `0x10` is dead.

`g_fade_state` (`0x43505e`) selects direction: `2` fades out, anything else
fades in. It is left at `1` afterwards.

So the menu never loads a backdrop picture. It blacks the palette, reads the
menu palette into `g_palette_buffer`, and lets the fade raise it from black;
whatever is already in the frame buffer becomes visible as the palette rises.
That is why `menu_draw_background` only pushes the palette immediately when
`0x435062` is set — the normal path stays black and defers to the fade.

Verified on screen: after the fade the brightest channel in the captured
window is `252`, which is the white in the overlay at `0x435068`.

## `game_run` (0x4077f0)

Entering the world, used for both the intro and a new game.

```
1  re-read DEFINES.ITD (0x46 bytes) into g_defines_itd, fatal if absent
2  load_game_defs (0x409490), world_init_tables (0x407bae)
3  world_set_mode(mode)   0x40a9b3 ; 0x44502e = 0xffff
4  world_set_stage(stage) 0x40a6cb ; 0x445006 = 0 ; g_restore_mode = 2
5  scene_load (0x40a123)
6  game_scene_loop(flag) (0x407c7c), actors_refresh (0x4095a3)
7  if 0x435062 == 0: palette_fade_out(8, 0)
```

Called `(1, 0, 0)` for the intro and `(0, 0x0c, 1)` for a new game. Steps 6
and 7 are exactly what the load-save branch of `game_main` does by hand, which
is what confirms those names.

`game_run` does **not** loop. The per-scene loop is `game_scene_loop`
(`0x407c7c`, 0x760 bytes), which calls `input_poll`, `body_render_or_walk`,
`scene_load` and around twenty more. That is the next thing to reconstruct and
the last structural unknown on the boot path.

## `game_scene_loop` (0x407c7c) — input dispatch

The per-scene loop the game runs inside. Its head latches input into the
scene's own copies (`g_scene_scancode`, `g_scene_dir_flags`,
`g_scene_confirm`) then dispatches on PC set-1 scancodes:

| Scancode | Key | Action |
|---|---|---|
| `0x01` | Esc | `scene_escape_menu`; a non-zero result leaves the loop (0x408250) |
| `0x19` | P | pause: spin until released, then block until any key |
| `0x27` | `;` | `music_toggle` + `message_show(g_cdaudio_ready + 0x29)` |
| `0x1f` | S | toggle `g_sound_ready2`, stop sound if off, `message_show(+0x2b)` |
| `0x1c` / `0x17` | Enter / I | `inventory_open` when `0x435056` set; leaves the loop if `0x4350c1` set |
| `0x0f` | Tab | `0x4019ff`, only when `0x444b28 == 0` |
| `0x58` | F12 | held for 5 polls inside a `0x190` ms window, then walks all actors |

The `;` and `S` handlers only fire while `g_message_shown` is clear, so a
message already on screen suppresses them. Both offset their message id by the
toggle's *new* state, which is why the ids come in pairs.

The F12 path walks `g_actor_count` actors through `actor_get` and acts on
those with `[+4] & 0x80` and `[+0xc] & 4` — the first two actor record fields
identified so far.

`g_scene_confirm` bit 0 sets `g_move_speed_flag` to `0x2000`, otherwise 0:
the walk/run selector.

**Not reconstructed:** roughly 400 further instructions of actor stepping,
camera work and drawing that make up the rest of the loop body.

## Corrections made during analysis

Two earlier readings were wrong and have been fixed in the IDB and the code:

- **The font is entry 1 of `ITD_RESS.PAK`, not entry 0.** `0x4071f9` sets
  `edx = 1`; the `xor edx, edx` at `0x40751b` is a different resource. Entry 1
  has `first_char = 32`, `last_char = 237`, which fits a font; entry 0 does not.
- **The word at font blob `[4]` is not a glyph height.** It reads `0x0e0d`
  (3597) in the shipped font, impossible on a 200-line screen. Left
  unidentified rather than guessed.
- **Palettes are 8-bit per channel, not 6-bit VGA.** The 6-bit widening was an
  assumption inherited from the DOS lineage; the data in this binary
  contradicts it (see above). Fixed in the backend and the frame dumper.
- **`0x40b042` is a palette fade-out, not the game loop.** It calls nothing
  but palette routines: the level runs `0x100 -> 0x80 -> 0` and it ends with
  `g_fade_state = 0`. The earlier name `game_loop` was guessed from its call
  position at the end of `game_run`. Renamed `palette_fade_out`. The real loop
  is `0x407c7c`, previously understated as `scene_enter` and now
  `game_scene_loop`.
- **`palette_fade` and `palette_fade_out` both discard their first argument**,
  overwriting it with the 0x80 step before it is read. The `0x10` and `8` at
  the call sites are dead.
- **`0x40b0f8` draws no backdrop image.** It zeroes 768 bytes and pushes them
  as a palette, i.e. a blackout; the earlier name `menu_draw_backdrop` was a
  guess from its call position. Renamed `palette_blackout`.
- **Menu entry 2 is the difficulty selector, not "continue".** Text id 13 is
  the difficulty label, so `0x409107` is not a savegame loader; the earlier
  name `game_load_saved_slot` was guessed before the text table was decoded.
  Renamed to `menu_difficulty_select` and `MENU_RESULT_DIFFICULTY`.
- **`byte_44448C` is a palette buffer, not a savegame path**, and `0x420190`
  is a fixed 768-byte palette copy, not a path builder. Both earlier names were
  guesses from a single call site; `menu_draw_background` shows what they are.
- **`0x41faf8` / `0x41fb18` freeze and resume the game clock**, they do not
  bracket a palette update: the first stores `g_ticks` at 0x4351a4, the second
  moves the tick base so time spent in a menu does not count. Renamed
  `ticks_freeze` / `ticks_unfreeze`.
- **`0x4201ec` clears the screen surface** and **`0x41c7cf` presents it**
  (WinGBitBlt); they were stubbed as `object_system_reset` / `scene_reset`.
- **`0x40b59d` / `0x40b5d6` save and restore the speech flag**
  (`speech_save` / `speech_restore`), not a post-load world hook.
- **`0x409490` loads OBJETS.ITD** (`world_load_objects`), not game defines.
- **`0x43505e` is one global**: the menu's "flag" is `g_fade_state`. The C code
  had two variables for it, so the main menu never requested its fade.
- **Duplicates merged**: the rotated-box routine 0x40b601 existed three times
  (one copy skipped the saved-ZV update the original always does), and
  0x422a04, 0x412731, 0x402cb5 and the font state twice each. The font copy
  meant string widths were measured with no font bound, so every centred
  text was drawn left aligned.

## Escape menu (0x408545)

Freezes the game clock, mutes speech, grabs the screen into the back buffer
and samples an 80 x 50 thumbnail from it (every fourth pixel and row,
0x4144f8). Draws ITD_RESS picture 17 with eight entries at x = 160,
y = 44 + 14 i: return (0x30), save (0x2d), load (0x2e), difficulty (0x35),
music (0x29 + on), sound effects (0x2b + on), detail (0x31 + `g_poly_detail`),
quit (0x2f). The selected entry is drawn shadowed. A successful load returns
1, which sends the scene loop straight to its change handling at 0x408250.

## Cutscenes and pictures

`play_cutscene` (0x4025a3, opcode 92: number, flags, repeat) plays
`AN<n>.PAK`. Entry 0 is the palette (0x300), the first frame (64000) and a
u16 entry count at +0xfd00. Later entries start with a u32 size: 0xfa00 and
up is a raw frame, smaller is a delta stream (0x4028ac) applied over the
previous frame: op 0 end, 1 one byte, 2 one byte twice, 3 u8 count + byte,
anything else u16 count + byte; a zero byte skips. Frames advance every
9/100 s; opcode 97 loads (frame, sample) pairs played as frames appear.
Flags: 1 fade around the start, 2 fade out at the end and full redraw, 4
black palette first, 8 re-upload the palette — the clear before it is
`memset(pal, 0x300, 0)` in Watcom's (dst, c, n) order and clears nothing.

The original frees the first entry at the end of every repeat but keeps
writing later frames into it; the reconstruction keeps it until the end.

Opcode 71 (inline at 0x405db1: entry, time, sample) shows an ITD_RESS
picture (palette at +0xfa04), plays the sample, waits 60 ticks, then until
`time` ticks pass or a key is pressed.

## Documents and books

`text_reader` (0x4156a6: text entry, xmin, ymin, xmax, ymax, mode, colour,
shadow) lays out a TEXTES.PAK entry page by page. Words end at any byte
<= 0x20; a line end followed by an empty line closes a paragraph (+7 px);
0x1a ends the text. Lines are justified except the last of a paragraph and
lines followed by a '#' command. Commands: `#P` page break (ignored at the
top of a page), `#C` centre, `#T` tab ("    "), `#X<n>` CD voice track
`n + voice.cfg`; anything else (`#L` is common) is skipped. A word wider
than the page stops the reader after that page. Up to 77 page starts are
remembered so pages can be turned back.

Modes: 0 arrows beside the text (ITD_RESS entry 0 sprites 1 and 2 at
y = 185), 1 automatic (300 ticks a page), 2 arrows below at y = 191, Enter
quits, no page sound. Page turns play the sample in DEFINES word 0. The
opcode 97 sample list plays one sample after another while reading.

`read_document` (0x4160b6, opcode 33) picks the backdrop: type 0 ITD_RESS 2
(60,10)-(245,190), type 1 entry 3 (48,2)-(260,197) with book page turns,
type 2 entry 4 (50,20)-(250,199), all in colour 124. Opcode 98 (inline at
0x40584e) gives the picture, text and box itself. The page-turn effects
0x41565e / 0x415682 only present the screen in this build.

## Map, difficulty, placing objects

- **Tab map** (0x4019ff): ITD_RESS picture 48 into the back buffer, its own
  palette uploaded directly, and a blinking arrow (0x4013de, 13 lines in
  colours 32..37, 30 ticks on / 30 off, up to 600 ticks or a key) at a
  position hard-coded per floor and room in 0x401724.
- **Difficulty** (0x409107): ITD_RESS picture 13, three rows (Carnby's life
  points, Carnby's hit points, villains' hit points: Low / Average / High)
  changed with left/right, then presets Beginner (2,2,0), Intermediate
  (1,1,1), Expert (0,0,2) and "return". The escape menu passes 1 to skip the
  closing fade.
- **Opcode 50** (0x40e1f0) drops an object in front of another object's
  actor: the body box is pushed along the actor's heading in steps of 10
  (up to 1500) until it clears the actor, then must not hit the scenery
  (0x4143e0) or a solid actor in any room. The object gets flags 0x184 and
  the tick count in +0x30. **Opcode 56** (0x40e366) puts an object at an
  explicit place; **opcode 64** (0x40e41b) at another object's place — when
  the dropped object has an actor, the original moves the script's current
  actor instead.

## Save files

`<data>/SAVE0<d>/SAVE<s>.ITD`, four directories (named California, Montana,
Nevada, Oklahoma on screen, text ids 71..74) of six slots. The file starts
with six big-endian offsets (written byte by byte by 0x420934):

| Offset | Points to |
|---|---|
| 0x00 | thumbnail, 0xfa0 bytes |
| 0x04 | palette, 0x300 bytes |
| 0x08 | name, 0x20 bytes |
| 0x0c | state blocks, back to back |
| 0x10 | u16 VARS.ITD size, then the vars |
| 0x14 | actor table, 0x238c bytes, unscrambled |

Data starts at 0x14, so the actor offset overwrites the first four bytes of
the thumbnail — the original does the same. State blocks and vars are XOR
0x35 (0x4162db). The block table at 0x434f80 lists (pointer, size) pairs;
index 7 is computed (object table, `count * 54`):

difficulty[3], room, floor, camera, player object, player actor, object
count, objects, DEFINES block (0x8c bytes: 0x46 big-endian words), in-hand[2], inventory counts[2],
inventories (2 x 50), inventory enabled, inventory index, quit flag, black
screen, saved speech flag, 0x44502a, game clock, clock frozen-at, music,
the 0x1fb8-byte saved-ZV area, palette effect flag and its 0x316-byte state.

Loading (0x4169f4) frees effect actors, reloads OBJETS/world flags, reads the
blocks, re-enters floor and room, restarts the music, reads vars and actors,
re-applies each actor's current frame (last frame for static ones) and asks
for a fade in.

The save screens are a small object system in the original (0x2ba6 bytes at
0x444974: three drives x four directories x six slots, each with a vtable).
The reconstruction keeps the directory and slot columns, positions, colours
and texts, but not the drive column, and names a save after the date instead
of the typed name of 0x417960.

## LISTBODY model format

Probed across all 788 entries of the shipped `LISTBODY.PAK`.

```
u16 flags
s16 bounds[6]        0x02..0x0d, candidate bounding box
...                  0x0e..: contents depend on flags

flags & 2 clear:     u16 vertex_count @ 0x10, vertices from 0x12
flags & 2 set:       u16 vertex_count @ 0x1a, vertices from 0x1c
vertex:              s16 x, s16 y, s16 z

flags & 2 set:       u16 group_count after the vertex array, then the records
```

Flag distribution: `1` × 337, `3` × 448, `2` × 2, `0` × 1.

Evidence: with those offsets the vertex array fits inside the block for
**337/337** of the `flags==1` entries and **448/448** of the `flags==3` ones.
Setting bit 1 shifts the count by exactly 10 bytes, so the bit gates a 10-byte
header section. It also gates the group table — a group count parses sanely
for every entry that has the bit and none that lack it, marking it as the
skinned/animated flag versus a static mesh. In the simplest body the single
group record contains `26`, that entry's own vertex count: one group spanning
the whole mesh. Coordinates in the small bodies are drawn only from the values
in `bounds`, i.e. box corners, which is what first identified the array.

`./pak_dump --bodies` runs the reconstructed C parser over the container:
788 parsed, 0 failed, 450 with groups.

**Not determined:** the group record stride. Strides of 14, 16, 18 and 20 all
fit inside every block, so block length alone cannot decide it — the layout has
to come from the code. `body_render_or_walk` (`0x4038f6`, `0x2e3f` bytes) reads
`g_cache_body` at six sites and `g_cache_anim` at two; that is the next thing
to read. The primitive list after the group table is likewise unread.

## Open questions

- `0x44502a` — saved with the game but referenced nowhere else.
- Opcode 100 records a palette cycle (0x40d6eb) whose stepping routine
  0x40d598 has no caller in this build; whether the shipped scripts rely
  on it, and what drove it in development, is unknown.
- `0x42a8f0` legacy LZ (methods 2 and 3): the two long match forms dispatch
  through jump tables at `0x43b4bc` and `0x42a96e`, neither recovered. Needed
  only for the earlier games in the series.
- The unnamed cache at `0x407581` (`0x4e20` bytes, 50 entries) has no backing
  container, so what fills it is still unknown.
- The Hex-Rays widget in this IDA Free session returns stale pseudocode; all of
  the above was read off the disassembly.
