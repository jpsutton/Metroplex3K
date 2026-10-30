# 24 — Building Architect Tool (BAT) data and custom buildings

Status: complete draft. Items tagged `[OPEN]` are unverified and listed at the end.

This document specifies:

1. the `.bld` custom-building file (byte layout of every record);
2. how the game discovers, renders, catalogues and persists custom buildings;
3. the BAT voxel model and the renderer that turns it into game sprites;
4. the BAT resource files (`.met`, `.plt`, TGA/BMP conventions, `BAT.DAT`, `BATConfig.ini`);
5. BAT application functionality;
6. `MaxisAddOn.dll`.

Container mechanics (IXF index, free slots, RefPack) are in `02-containers.md`. Sprite encodings are
in `04-graphics-formats.md`. Resource keys are written `T:G:I` (Type:Group:Instance) as everywhere
in this specification. Remember that the IXF index stores each entry on disk in the order
(Group, Instance, Type, offset, size) — see `02-containers.md`.

## 1. The `.bld` file

### 1.1 Container

A `.bld` is an IXF container `[DATA]` (17/17 shipped files in `Buildings/`). Properties observed
on every shipped file `[DATA]`:

* the index is one preallocated 1024-entry block, so record data starts at file offset 20484
  (4 + 1024 × 20);
* every record has Group `C3F8D4A9`;
* the Type identifies the record kind and the Instance identifies the item within a kind.

### 1.2 Record table

| Key (T:G:I) | Count | Record |
|---|---|---|
| `E3F87B0C:C3F8D4A9:A3F8D615` | 1 | Building model (voxel grid, spans, texture palette) — §1.3 |
| `E3F87B0D:C3F8D4A9:n` | 0..n | Embedded custom texture header, n = 0,1,2… — §1.4 |
| `E3F87B10:C3F8D4A9:n` | 0..n | Embedded custom texture pixels, paired with the header of the same n — §1.5 |
| `FA662D17:C3F8D4A9:n` | 0..n | Embedded custom prop description (only for textures that are custom props) — §1.6 |
| `E3F87B0F:C3F8D4A9:63FBE1CC` | 1 | Building information (names, author, etc.) — §1.7 |
| `644D7178:C3F8D4A9:A44D71AA` | 1 | Extra string property — §1.8 |
| `644D7179:C3F8D4A9:A44D71AB` | 1 | Extra u32 property — §1.8 |
| `B9EC8F72:C3F8D4A9:99EC9601` | 1 | Extra u32 property — §1.8 |
| `99F88B9E:C3F8D4A9:B9F88BD2` | 1 | Four u8 properties — §1.8 |
| `E3F87B0E:C3F8D4A9:E3F8D655` | 1 | Thumbnail image (200 × 200, 16-bit) — §1.9 |

All 17 shipped files contain exactly the model, information, the four property records and the
thumbnail; 12 of 17 also contain embedded textures, and one (HiTech Lab) contains custom props
`[DATA]`.

Rule for the four property records `[STATIC]`: their Instance is the building's information
Instance (`63FBE1CC`) unless it equals `63FBE1CC`, in which case the fixed substitute Instance in the
table above is used. Since the information Instance is always `63FBE1CC` in files written by the
BAT, a writer must always use the substitutes `A44D71AA`, `A44D71AB`, `99EC9601`, `B9F88BD2`.


### 1.3 Building model record `E3F87B0C:C3F8D4A9:A3F8D615`

#### 1.3.1 Concepts

The BAT model is a voxel grid:

* The lot is `L` × `L` city tiles, `L` ∈ {1,2,3,4} `[DATA]` (shipped: 1–4).
* Each tile is divided into 8 × 8 *cells*, so the model grid is `N` × `N` columns with `N = 8·L`
  `[DATA]` (17/17). Readers clamp `N` to at most 48 `[STATIC]`.
* Each column holds a vertical stack of occupied cells at integer heights `z` (0 = ground level).
  An occupied cell is one *block*; it has a shape, a quarter-turn rotation and six faces, each of
  which can carry a paint and a detail. A cell can instead be a *prop cell* that carries pieces of a
  pre-drawn prop image.
* All texture references are 8-bit indices into a per-model *texture palette* of at most 256
  entries (index 0 = none).

#### 1.3.2 Byte layout (version 8, the only version written; all 17 shipped files)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | version | 8 `[DATA]` |
| 0x01 | 1 | u8 | orientation | 0–3, quarter-turns of the model relative to its lot (§1.3.6) |
| 0x02 | 1 | u8 | N | grid columns per side, = 8 × lot size |
| 0x03 | 1 | u8 | lotSize | lot side in tiles (1–4) |
| 0x04 | 1 | u8 | marker | 0xEA |
| 0x05 | 2 | u16 | H | height limit of the editing volume, in cell levels (§1.3.7) |
| 0x07 | 2 | u16 | marker | 0xEAEA |
| 0x09 | 4 | u32 | cellCount | number of cell slots including the null slot 0 |
| 0x0D | 4 | u32 | marker | 0xEAEAEAEA |
| 0x11 | 4 | u32 | 4 | size in bytes of one column-head entry (constant) |
| 0x15 | 4 | u32 | 32 | size in bytes of one cell record (constant) |
| 0x19 | 4 | u32 | marker | 0xEAEAEAEA |
| 0x1D | 4 | u32 | gridBytes | `N·N·4` |
| 0x21 | 4·N·N | u32[N][N] | columnHead | for row `r` = 0..N−1 then column `c` = 0..N−1: index of the lowest cell of that column, 0 = empty column |
| … | 4 | u32 | marker | 0xEAEAEAEA |
| … | 4 | u32 | packedSize | byte length `P` of the packed cell block that follows |
| … | P | bytes | packedCells | packed cell array (§1.3.3) |
| … | 4 | u32 | marker | 0xEAEAEAEA |
| … | 4 | u32 | paletteCount | 1–256 entries |
| … | var | entries | palette | `paletteCount` × (u32 nameLength, nameLength bytes of name without terminator, u32 textureId) (§1.3.5) |
| … | 4 | u32 | marker | 0xEAEAEAEA (record ends here) |

All marker values must be written exactly; the reader skips them without checking `[STATIC]`.
The parser in the dirty room verifies every marker, constant and length on 17/17 files `[DATA]`.

#### 1.3.3 Packed cell block

`packedCells` = u32 `P` (repeated, equal to `packedSize`) followed by an EA RefPack stream
(`02-containers.md`) `[DATA]`. The decompressed data starts with `cellCount` × 32-byte cell
records. Readers use only the first `cellCount × 32` bytes.

Writer defect, recorded for byte-exactness `[STATIC]` `[DATA]`: the original writer compresses
`B = n + floor(n/9) + 1000` bytes where `n = cellCount × 32`, i.e. the cell array plus whatever
followed it in memory. The RefPack header therefore announces `B` bytes (verified 17/17). A
compatible writer should emit the cell array followed by `B − n` filler bytes (zeros are fine; the
content is never read).

#### 1.3.4 Cell record (32 bytes)

Cell 0 is a null sentinel and is all zero `[DATA]`. Every other cell is reachable from exactly one
column head; following the `next` links from the head visits the column's cells in strictly
increasing `z` `[DATA]` (17/17).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | packed | bits 0–16 `next` (index of the next-higher cell in the column, 0 = top); bits 17–25 `z` (0–511); bits 26–29 `shape` (0–15); bits 30–31 `rot` (0–3) |
| 0x04 | 4 | u32 | faceFlags | bits 0–5: one flag per face slot (§ below); bit 6 occasionally set `[OPEN]`; other bits 0 |
| 0x08 | 24 | 6 × 4 bytes | face[0..5] | face slots, see next tables |

Shape codes `[DATA]` (counts over all 17 files): 0 = 43 983 cells (full cube), 1 = 1 390,
2 = 1 237, 3 = 42, 4 = 82, 5 = 58, 6 = 138, 7 = 112, 9 = 3, 10 = 6, 15 = 5 501 (prop cell).
Shape 0 is the cube. Shapes 1–14 are the BAT's partial blocks: wedges, ramps, roof corners and
half-height ramps. Shape 8 is a legacy floor marker (§1.8, model revision). Shape 15 marks a prop
cell. The exact geometry of every shape is given in §3.2 `[STATIC]`.

Face slot of a block cell (shape ≠ 15) `[STATIC]` (palette usage count over bytes 0 and 2 of
each slot) `[DATA]` (all indices < paletteCount, 17/17):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| +0 | 1 | u8 | paint | palette index of the paint texture, 0 = unpainted |
| +1 | 1 | u8 | paintOffset | high nibble / low nibble = horizontal / vertical tile offset of this face inside the paint texture, in 16-pixel units `[INFERRED, medium]` |
| +2 | 1 | u8 | detail | palette index of the detail (decal) texture, 0 = none |
| +3 | 1 | u8 | detailOffset | high nibble / low nibble = which 16×16 tile of the detail image lies on this face (observed 0–2 each) `[INFERRED, medium]` |

Slot assignment `[STATIC]`: face slot k holds the paint and detail of polygon k of the cell's
shape, as listed in the shape table of §3.2. The slot number therefore depends on the shape. For
the cube (shape 0) and the prop shape (15), with rot = 0, the slots are:

* slot 0 = top;
* slot 1 = −x wall;
* slot 2 = −y wall;
* slot 3 = +x wall;
* slot 4 = +y wall;
* slot 5 = bottom.

The model axis x runs along the row index and y along the column index, so slot 1 faces row − 1,
slot 2 faces column − 1, slot 3 faces row + 1 and slot 4 faces column + 1. This orientation is
`[INFERRED, medium]`: across all shipped cells, each wall flag correlates at 0.63–0.69 with the
corresponding neighbour cell being empty, and the top flag correlates at 0.99 with no block above.
A few shapes have a seventh polygon (their bottom). It has no slot and always uses the default
appearance. The flags are a cached
per-face attribute (most likely "face is exposed/editable"); a renderer must recompute visibility
from geometry and must not rely on them `[INFERRED]`. Writers should preserve them.

Face slot of a prop cell (shape = 15) `[STATIC]` `[DATA]`:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| slot 0 | 4 | — | unused | 0 |
| slot 1–4, +0 | 1 | u8 | image | palette index of the prop image for view k = slot − 1 (§1.6) |
| slot 1–4, +1 | 1 | s8 | dx | horizontal offset of this cell from the prop's anchor cell, in cells |
| slot 1–4, +2 | 1 | u8 | — | 0 |
| slot 1–4, +3 | 1 | s8 | dz | vertical offset of this cell from the prop's anchor, in cells (0, −1, −2 …) |
| slot 5 | 4 | — | unused | 0 |

A prop occupies a box of cells (its `.met` dimensions); every cell of the box is a prop cell with
the same four image indices and its own offsets. When a single image is needed for a prop cell the
program uses the slot `1 + rot` `[STATIC]`.

#### 1.3.5 Texture palette

Entry 0 is always present and means "none"; its name is empty and its textureId is undefined
(garbage) `[DATA]`. Entries 1–255 name a texture; empty names mark unused slots. The name is the
file name of the texture as listed in the BAT libraries (lower-case in practice, e.g. `brick 06.tga`,
`bench1 3.bmp`) or the pseudo-name `color R,G,B` (decimal 0–255) for a solid colour paint `[DATA]`.
`textureId` is the 32-bit texture identifier (§1.4). Name conventions recognised by the reader
`[STATIC]`:

* If a name has at least 10 characters and contains the separator ` |` followed by 8 hex digits,
  the part after the separator is parsed as the texture ID and the part before it is the name.
* For version-6 models (legacy) unnamed entries are named from a built-in table of stock texture
  names, or `CustomV6TextureID<n>` if not in the table.

Resolution of an entry when the model is loaded `[STATIC]`: look the name up in the texture
library (stock BAT textures from `Res/BA/PAINTS`, `Res/BA/DETAILS`, `Res/BA/PROPS`, custom ones
from `BACustom/…`, and the textures embedded in the same `.bld`, §1.4); unresolved entries are
left empty and the faces using them render unpainted `[INFERRED]`.

#### 1.3.6 Orientation

`orientation` records how many quarter-turns the user has rotated the building on its lot.
Before rendering for the game, a model with orientation 1 is rotated by one quarter-turn backwards
and a model with orientation 3 by one quarter-turn forwards, so that both end with orientation 0;
orientations 0 and 2 are left untouched `[STATIC]` (shipped values: 0 ×10, 1 ×2, 2 ×1, 3 ×3). A
rotation turns the column grid by 90° and adds ±1 (mod 4) to every cell's `rot` field `[STATIC]`. Face slots are not permuted. A slot stays attached to its polygon, and the
polygon turns with the cell's `rot` (§3.2.3).

#### 1.3.7 Height limit

`H` is the height of the editing volume in cell levels; every cell has `z < H` `[DATA]`. The BAT
offers default limits by lot size (28, 64, 108, 144 levels for lots 1–4, built-in table) and shipped
files store 28, 65, 109, 144 and larger user-chosen values (257, 288, 385) `[DATA]`. Older readers
reject `H > 512` and `N > 32` `[STATIC]`.

#### 1.3.8 Legacy versions

The reader also accepts versions 4–7 `[STATIC]`; none ship. Differences: version 7 stores `H − 1`;
version 6 stores a palette of (u32 index, u32 legacyId) pairs; versions 4–5 store the grid as u16
and the cells in an older 76-byte form. An implementation may reject versions other than 8 and
report the file as unsupported `[INFERRED]`.

### 1.4 Embedded texture header `E3F87B0D:C3F8D4A9:n`

Present for every non-stock texture used by the model (custom paints, details and prop images
from `BACustom/`), so that the `.bld` is self-contained `[DATA]` (11/17 files, 84 textures in total, 2–17 per file).
`n` counts from 0.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | version | 0x0A |
| 0x01 | 16 | u32[4] | imageRect | x0, y0, x1, y1 (exclusive) of the source image inside the padded pixel buffer |
| 0x11 | 16 | u32[4] | swatchRect | sub-rectangle used as the palette swatch (paints: the 32×32 at the bottom-left; details and props: = imageRect) `[INFERRED]` |
| 0x21 | 4 | u32 | textureId | ID matching the palette entry's textureId |
| 0x25 | 4 | u32 | sourceKind | 4 = 32-bit TGA detail, 5 = TGA paint, 2 = BMP prop image `[DATA]` (meaning `[INFERRED]`) |
| 0x29 | 4 | f32 | width | padded buffer width in pixels |
| 0x2D | 4 | f32 | height | padded buffer height |
| 0x31 | 4 | f32 | invWidth | 1/width |
| 0x35 | 4 | f32 | invHeight | 1/height |
| 0x39 | 4 | u32 | nameLength | |
| 0x3D | var | chars | name | texture file name (no terminator) |
| … | 12 | u32[3] | pixelKey | in-memory key order Type, Group, Instance = `E3F87B10`, `C3F8D4A9`, `n` |
| … | 4 | u32 | category | 6 = paint, 7 = detail, 16 = prop image `[DATA]` |

Padded buffer dimensions are the next power of two ≥ the image dimension, independently per axis
(16→16, 16×32→32×32, 32×48→64×64, 62×78→128×128 for props) `[DATA]`.

### 1.5 Embedded texture pixels `E3F87B10:C3F8D4A9:n`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | format | 0x0EF1 = 32-bit B,G,R,A |
| 0x02 | 2 | u16 | width | padded width |
| 0x04 | 2 | u16 | height | padded height |
| 0x06 | 4 | u32 | length | record size − 6 (= 4 + RefPack stream length) |
| 0x0A | var | RefPack | pixels | decompresses to width × height × 4 bytes, rows top to bottom, 4 bytes per pixel in order B, G, R, A |

Pixels outside `imageRect` are filler. For TGA sources the alpha channel is copied from the file;
for BMP sources, pure magenta (255,0,255) marks transparency `[DATA]` (readme files in
`BACustom/`). Verified on all 84 embedded textures `[DATA]`.

### 1.6 Embedded custom prop `FA662D17:C3F8D4A9:n`

Exists for an embedded texture `n` that is a custom prop (only HiTech Lab: 3 records) `[DATA]`.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | version | 0x0A |
| 0x01 | var | 4 × (u32 len, chars) | viewImage[0..3] | image file names for the four views, as in the `.met` line (§4.1) |
| … | 4 | u32 | sizeX | prop box size in cells |
| … | 4 | u32 | sizeY | |
| … | 4 | u32 | sizeZ | |

### 1.7 Building information `E3F87B0F:C3F8D4A9:63FBE1CC`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | lotSize | = model lotSize `[DATA]` |
| 0x01 | 1 | u8 | nameLength | |
| 0x02 | var | chars | name | building name shown in game |
| … | 1 | u8 | authorLength | |
| … | var | chars | author | |
| … | 4 | u32 | reserved | 0 in all shipped files |
| … | 4 | u32 | buildingId | 32-bit building ID (§2.3) |

The current reader stops after `author`; the trailing 8 bytes are present in all 17 shipped files
`[DATA]` and must be written. `buildingId` equals the instance used for this building in the game's
rendered-building database (§2.2) for all 15 buildings that appear there `[DATA]`.

### 1.8 Extra property records

| Key | Layout | Meaning | Shipped values |
|---|---|---|---|
| `644D7178:C3F8D4A9:A44D71AA` | u8 length + chars | free text (description) carried into the game's building directory | empty ×16, one space ×1 |
| `644D7179:C3F8D4A9:A44D71AB` | u32 | building category (table below) | 0x208 ×3, 0x209 ×6, 0x20A ×4, 0x20F ×1, 0x212 ×1, 0x217 ×2 |
| `B9EC8F72:C3F8D4A9:99EC9601` | u32 | language code of the program that saved the file | 1 ×17 |
| `99F88B9E:C3F8D4A9:B9F88BD2` | u8 ×4 | model revision (major, minor, build, 0) | (1,0,0,0) ×16, (2,0,28,0) ×1 |

**Building category** `[STATIC]` `[DATA]`. The value is the Instance of a string in the BAT's own
string table (Group `63DE4715` in `Res/Text/<LANG>/BATStringsMain.IXF`), and the BAT uses that
string as the category label in its save dialog (a drop-down whose default is 0x217). The 16 codes
are:

| Code | Category | Code | Category |
|---|---|---|---|
| 0x208 | Residential | 0x210 | College |
| 0x209 | Commercial | 0x211 | Library |
| 0x20A | Industrial | 0x212 | Museum |
| 0x20B | Police Station | 0x213 | Historic Statue |
| 0x20C | Jail | 0x214 | Mayor's House |
| 0x20D | Fire Station | 0x215 | City Hall |
| 0x20E | Hospital | 0x216 | Performing Arts Center |
| 0x20F | School | 0x217 | Other |

The category is descriptive metadata only. The game's import does not copy it into the
rendered-building database (§2.2), and no simulation rule reads it `[STATIC]`. A player can use it
to choose a sensible building to replace (§2.4.1). It does not set the occupant's behaviour.

**Language code** `[STATIC]`. When the BAT saves, it stores the framework's current language code.
That code is derived from the Windows user locale and defaults to 1, the US-English code used
everywhere in this specification (`05-text-and-fonts.md`). If the record is missing, the reader
substitutes the running program's language code. The game ignores the value. A writer should
store 1 unless it implements language codes.

**Model revision** `[STATIC]`. This is the revision of the voxel-model conventions:

* A file without the record is treated as revision (1,0,0,0).
* A newly created model starts at (2,0,28,0).
* If the record is present but too short, each missing byte defaults to the matching byte of
  (2,0,28,0).

For revision 1 (major < 2), the model library performs a one-time conversion when the BAT edits
the model `[STATIC]`. Legacy files marked the lot floor with cells of shape 8. The conversion
removes a shape-8 floor marker from the bottom of the corner columns and raises every cell by one
level. It then sets the revision to (2,0,28,0). When the BAT adds the floor layer (§3.2.6), it uses a
shape-8 floor cell in a revision-1 model and a full cube with default (unpainted) faces in a
revision-2 model. A compatible engine that only renders
`.bld` files can rely on the floor layer already present in saved files (§3.2.6). It does not need to perform the edit-time
conversion `[INFERRED]`.

`[STATIC]` Reader defaults when a record is missing: empty string; category 0; the running
program's language code; (1,0,0,0).

A further optional record `DA6DC9A9:C3F8D4A9:63FBE1CC` = (u32 length + chars: owner string;
u32 buildingId) is written by the BAT when a registered-owner string exists `[STATIC]`; none of the
shipped files contain it. When it is absent, the BAT stamps the model with the registered owner
name of the installation (taken from the game's registration data in the system registry) and
generates a fresh random building ID `[STATIC]`. The owner string is informational only. A
compatible engine does not need the registration data: it only needs to preserve this record, and
may stamp any owner name of its choosing when it creates the record.

### 1.9 Thumbnail `E3F87B0E:C3F8D4A9:E3F8D655`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | format | 0x07F0 = 16-bit RGB565 |
| 0x02 | 2 | u16 | width | 200 |
| 0x04 | 2 | u16 | height | 200 |
| 0x06 | 4 | u32 | length | record size − 6 |
| 0x0A | var | RefPack | pixels | 200 × 200 × u16 little-endian RGB565, rows top to bottom; 0xF81F (magenta) = transparent |

The thumbnail is a preview render of the building, used in BAT file dialogs and the game's
building browser `[DATA]` (17/17).

### 1.10 Parser coverage

The analysis team's parser reads every record of 17/17 shipped `.bld` files with all
markers, constants, lengths, list links, palette ranges and cross-references asserted `[DATA]`.

## 2. How the game imports custom buildings

### 2.1 Overview

The game never draws a `.bld` directly. Importing a building renders it once into ordinary game
sprites plus a composite-sprite record, stores them in the *rendered-building database*
`Apps/BARender/BAT.DAT`, and from then on the building is drawn exactly like a shipped building
`[STATIC]`. The `.bld` is not needed again unless the building is re-imported.

### 2.2 The rendered-building database `Apps/BARender/BAT.DAT`

An IXF container (`02-containers.md`). The shipped file holds 15 of the 17 shipped `.bld`
buildings, pre-rendered (all except Tutorial House and Zweikky Unpainted) `[DATA]`. All records
have Group `BA1DBF1E` `[DATA]`. Per building with ID `B` (§2.3):

| Key (T:G:I) | Content |
|---|---|
| `856CD19A:BA1DBF1E:B` | Directory record (layout below) |
| `00006300:BA1DBF1E:B` | Composite-sprite (CSATTRIB, `BIN\r`) record; layout in `25-rendering.md` §4.2 |
| `00000000:BA1DBF1E:S` and `00000001:BA1DBF1E:S` | The sprite image and its 8-byte sprite header, for each of 16 sprite IDs `S` (format in `04-graphics-formats.md`) |

Directory record `[DATA]` (15/15) `[STATIC]`:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | lotSize | 1–4 |
| 0x01 | 12 | u32[3] | appearanceKey | Type, Group, Instance = `00006300`, `BA1DBF1E`, `B` |
| 0x0D | var | u32 len + chars | name | from the `.bld` information record |
| … | var | u32 len + chars | author | |
| … | var | u32 len + chars | description | from `644D7178` |
| … | var | u32 len + chars | owner | registered-owner string (§1.8), empty in shipped data |
| … | 4 | u32 | buildingId | `B` |

Composite-sprite record written for a BAT building `[DATA]` (15/15): Version 1,
SprInstCLSID `00007300`, ZoomCount 5, RotCount 4, ZoomSetCount 4, AnimSetCount 20, AnimFrames 1,
RegRects 0, LayerFlags 0, FrameCount 20. Frames 0–15 are view zooms 0–3 × view rotations 0–3
(index = zoom·4 + rotation), each naming its own sprite (Category `BA1DBF1E`). Frames 16–19 (zoom
4) reuse the zoom-3 sprites of rotations 0–3 with Flags = 2, which means "draw this image at twice
its size" `[INFERRED, high]` (the BAT renders nothing at the 128-px scale). The 16 sprite IDs of a
building are consecutive 32-bit values allocated at import time `[DATA]`.

Sprite sizes `[DATA]`: at zoom z the sprite is `8·2^z·lotSize` pixels wide (8/16/32/64 px per
tile for zoom 0–3); heights at zooms 0–2 are the zoom-3 height divided by 8, 4, 2 and rounded up.
The 8-byte sprite header holds (s16 0, s16 height − W/4, s16 width, s16 W/4) with W = nominal
width `8·2^z·lotSize` for 237 of the 240 shipped BAT sprites; three sprites of one building are 1–2
px narrower than W (width and W/4 field then differ slightly) `[DATA]` (field meanings in
`04-graphics-formats.md`). The analysis team verified the directory record,
the composite-sprite record and the sprite headers of 15/15 buildings `[DATA]`.

### 2.3 Building ID

Every BAT building has a 32-bit ID `B` that becomes the Instance of its directory record and of its
composite-sprite record. It is chosen by the BAT when the building is saved (a random/unique value;
shipped IDs look like `45FEEAC6`) and stored in the information record's trailing u32 (§1.7) and in
the optional `DA6DC9A9` record `[DATA]` `[STATIC]`. When a loader finds no stored ID it generates a
new one `[STATIC]`, so re-importing such a file creates a different building. A compatible engine
should read the ID from the information record when present.

### 2.4 Import procedure `[STATIC]`

1. **Selection.** The game's BAT-import dialog lists the `*.BLD` files of `<game root>\Buildings\`
   (the directory above `Apps\`, plus `Buildings\`), showing for each the name and the lot size as
   `WxH` `[STATIC]`, and also lists the buildings already in the database (from the directory
   records) `[INFERRED]`. The user picks buildings to add and to remove (screen description:
   `09-ui-screens.md`).
2. **Prepare.** The directory `Apps\BARender\` is created if missing. If `BAT.DAT` exists it is first
   copied to `Apps\Res\BA\RenderedBuildingsBackup.IXF` (an existing backup is deleted first); then
   `BAT.DAT` is opened for read/write (created if absent).
3. **Load.** Each selected `.bld` that exists is loaded (§1). Missing files are skipped silently.
   The loaded buildings are bucketed by lot size and processed bucket by bucket.
4. **Render.** For each building: normalise orientation (§1.3.6), render the four sprite sizes
   for the four view rotations (§3), allocate 16 sprite IDs, write the sprites, the composite-sprite
   record and the directory record (§2.2). A progress bar runs from 0 to 100 % in steps of 16 % per
   lot-size bucket after an initial 100-unit phase `[STATIC]` (cosmetic).
5. **Remove.** Buildings the user removed are deleted from the database: records whose keys are not
   in the keep-list are dropped `[INFERRED]`.
6. **Register.** The game then knows the BAT buildings as the set of directory records in
   `BAT.DAT`; each directory record is a resource of type `856CD19A` that the game collects into its
   BAT-building list, which feeds the BAT section of the building/landmark placement UI
   `[STATIC]`.

### 2.4.1 How a BAT building enters a city: appearance replacement `[STATIC]`

A BAT building is never an occupant type of its own. It does **not** grow in zones and cannot be
plopped as a landmark. The game uses it only as a **replacement appearance** for an existing
*replaceable* building:

1. **Replaceable buildings.** `Sys/SC3CityScheme.ini` (in `SYS.PAK`) section `[ReplaceableBuildings]`
   lists the occupant keys whose appearance may be swapped, one per line. Each key is the
   occupant-attribute key `Type,Group,Instance` in decimal, for example `545184782,1406,209` =
   `207EDC0E:0000057E:000000D1`. The value after `=` is a developer label and is ignored. The
   shipped list has 323 entries. They cover zone buildings (residential, commercial, industrial)
   and civic buildings: hospital, police, fire, museum, college, school, library, jail, mayor's
   house, park and airport pieces. The game parses each line into a key and keeps the keys as a
   set. A building is replaceable exactly when its occupant key (the key of its TKB1 attribute
   record, `13-buildings-occupants.md` §1) is in the set.
2. **Choosing.** The player opens the Building Replacement dialog. It can be opened from the city
   scheme panel ("user-made buildings") or for a queried building ("Building Queried:", buttons
   *Replace* and *Revert*). See `09-ui-screens.md` §3.3. The dialog lists the BAT buildings
   from the rendered-building database, filtered by name, size and architect, and the player picks
   one to stand in for the replaceable building. The list shows each building's lot size, and a
   replacement must have the same footprint as the building it replaces `[INFERRED, medium]`. The
   game's directory record (§2.2) carries the lot size, and the replaceable building's own lot is
   attribute 0x66 X × Y.
3. **Effect.** Only the drawing changes. The building keeps the occupant key of the replaced
   building, so it keeps all occupant attributes: agent type (attribute 0x65), and hence zone or
   civic function, residents, jobs, cost, upkeep, service capacity, land-value effect, pollution
   and flammability, as defined in `13-buildings-occupants.md` §3.2. The renderer draws the BAT
   building's composite-sprite record `00006300:BA1DBF1E:<buildingId>`, read from the directory
   record's appearance key, instead of the occupant's attribute-0x67 appearance. *Revert*
   restores the original appearance.
4. **Per-city set.** The replacements chosen for a city form that city's "user building set". It
   is saved with the city and can be copied from another saved city ("import set") or cleared
   ("clear set"). It is stored in its own outer record of the city file, `99F47014:79F4702D:0`
   (`10-save-format.md` §3.7): a list of (replaced occupant key, replacement appearance key
   `00006300:BA1DBF1E:<buildingId>`) pairs. The occupant record does not change. Because the set is
   keyed by occupant key, a replacement applies to every building of that occupant type in the
   city, not to one building instance [STATIC].

Consequences for the engine:

* None of the `.bld` content affects the simulation. Lot size only limits which buildings can be
  replaced. The category of §1.8 is a label.
* A BAT building that the player has not used as a replacement has no presence in the city.

### 2.5 Persistence in saved cities

A BAT building appears in a city only as the replacement appearance of a replaceable occupant
(§2.4.1). The replacement is stored in the user-building-set record `99F47014:79F4702D:0`
(`10-save-format.md` §3.7) and references the composite-sprite key `00006300:BA1DBF1E:B`
`[STATIC]`. The rendered sprites
live only in `BAT.DAT`, and no sprite or model data is embedded in the city file `[DATA]`: none of
the 90 decompressed records of the 30+ shipped cities, scenarios, starter towns and terrains
contains the BAT group ID `BA1DBF1E`, the directory type or the BAT list type (the shipped saves
contain no BAT buildings). Consequently:

* moving a city to another installation requires the same buildings to be imported there with the
  same IDs (re-importing the same `.bld` reuses the ID stored in it);
* when `BAT.DAT` lacks the referenced record, the replacement is loaded with no appearance and the
  building is drawn with its normal appearance `[STATIC lookup; INFERRED visible result]`. The
  occupant itself is unaffected. To keep the save round-trippable, an implementation should keep
  the pair and write it back.

## 3. The BAT renderer (producing game sprites)

### 3.1 Coordinate system and projection

* Model axes: row index `r` and column index `c` of the column grid (0..N−1) and level `z`.
* Rendering uses a 2:1 axonometric (dimetric) projection like the game's (`25-rendering.md` §2).
  The program renders one *master* image per view at 128 px per tile, i.e. 16 px per cell, and
  derives all four game zooms from it (§3.1.1) `[STATIC]`.
* Exact projection `[STATIC]` `[DATA]`. Let s be half the screen width of one cell's footprint
  diamond: s = 8 px in the master, 4 px at zoom 3. A model point (x, y, z), in cells and levels
  relative to the far corner of the lot at ground level, maps to the screen offset (screen y grows
  downwards):

  X = s·(y − x), Y = s·(x + y)/2 − s·z.

  So in view 0 the +x (row) axis runs down-left, the +y (column) axis runs down-right, and one
  level rises by s, the same as the height of a cell's footprint diamond. The program keeps this
  as one fixed screen offset for each of the 12 vertices of §3.2.1. At zoom 3 this gives 4 px per
  level. Fitting silhouettes of 13 shipped `.bld` models against their pre-rendered zoom-3 sprites
  agrees: the mirror choice above has mean IoU 0.85 against 0.75 for the mirrored one, and 4.0 px
  per level beats 3.5, 4.25, 4.5 and 5.
* Sprite canvas (zoom 3): the width is 64·lotSize. The lot diamond, at z = 0, which is the bottom
  of the floor layer of §3.2.6, occupies the bottom 32·lotSize rows, with the lot's far corner at
  the top centre of that diamond. The canvas extends upwards to the highest drawn pixel.
* Four view rotations. Frame k of each zoom in the composite-sprite record is view k `[DATA]`.
  Before rendering view k, the model is turned k quarter-turns counter-clockwise: (x, y) →
  (N−1−y, x), applied k times, with every cell's `rot` advanced by k `[STATIC]`. Silhouette fits
  confirm this direction for view 1 (mean IoU 0.87 against 0.73) `[DATA]`.

#### 3.1.1 Zoom images `[STATIC]`

The view is rendered once into the master image at 128 px per tile. The background is cleared to
the colour key 0xFFFF00FF (opaque magenta), which marks transparent pixels. The master is then
resampled by factors 1/2, 1/4, 1/8 and 1/16 to give the zoom 3, 2, 1 and 0 sprites (64, 32, 16
and 8 px per tile). Sizes are rounded up, and images are aligned at the bottom edge. Zoom 4 is the
zoom-3 image magnified ×2 by the game (§2.2).

The resampling filter is an area average `[INFERRED, low]`. Among the filters tested against the
shipped sprites, an alpha-weighted box average (transparent pixels excluded) aligned at the bottom
edge reproduces the zoom-2 sprites best, but it is not exact. Mean absolute error per channel
against a 2× reduction of the zoom-3 sprite is 7.8 levels, and 14.6 and 16.0 for the 4× and 8×
reductions. Because the master is not stored, the exact
kernel is `[OPEN]`.

The program also contains an optional tone adjustment for the three smallest zooms (colour
multiplied by 0.9, 0.8 or 0.7, then offsets added). The shipped pre-rendered sprites show no trace
of it: the mean colour is identical at every zoom (138.7, 136.7, 124.6) `[DATA]`. An engine should
not apply it.

### 3.2 Geometry

#### 3.2.1 Cell vertices `[STATIC]`

Each block cell occupies the unit box x ∈ [0,1], y ∈ [0,1], z ∈ [0,1] in its own frame. x is the
row direction, y is the column direction and z is up, in units of one cell / one level. All
shapes are built from 12 vertices, arranged as 3 rings of 4 corners:

* corner 0 = (1,1), corner 1 = (0,1), corner 2 = (0,0), corner 3 = (1,0);
* vertices 0–3 are corners 0–3 at z = 0 (bottom ring);
* vertices 4–7 are corners 0–3 at z = 1 (top ring);
* vertices 8–11 are corners 0–3 at z = ½ (middle ring).

#### 3.2.2 Face normal code

Each polygon carries a one-byte normal code = kind × 8 + dir.

`dir` (0–7) is a horizontal direction in 45° steps:

| dir | Direction | dir | Direction |
|---|---|---|---|
| 0 | +x+y | 4 | −x−y |
| 1 | +y | 5 | −y |
| 2 | −x+y | 6 | +x−y |
| 3 | −x | 7 | +x |

`kind` selects the surface type. The program uses the "normal used by the lighting tables" column
for shading:

| kind | Surface | Normal used by the lighting tables (horizontal part × unit dir, vertical part) |
|---|---|---|
| 0 | vertical wall | (1, 0) |
| 1 | 45° slope | (0.7071, 0.7071) |
| 2 | top (horizontal, facing up) | (0, 1) |
| 3 | steep slope ("60°") | (0.866, 0.5) |
| 4 | shallow slope ("30°", the half-height ramps) | (0.5, 0.866) |
| 5 | underside (faces down) | (0, −1) |
| 6 | bottom | down |
| 7 | legacy floor marker (faces up) | (0, 1) |

The shallow and steep slopes really rise ½ level per cell and 1 level per ½ cell. The 30° and 60°
labels are the program's lighting approximation, and an engine should use the table values to
reproduce the original shading.

#### 3.2.3 Rotation

A cell with `rot` = r is its rot-0 shape turned by r quarter-turns counter-clockwise (from +x
towards +y) about the vertical axis through the cell centre `[STATIC]`:

* every vertex index v = 4·ring + corner becomes 4·ring + ((corner + r) mod 4);
* every normal dir becomes (dir + 2r) mod 8.

The face slots do not move, because slot k always belongs to polygon k.

#### 3.2.4 Shape table (rot = 0) `[STATIC]`

The 14 cell shapes are fixed geometry, specified here as behaviour: a cell of shape k is drawn as
exactly the polygons listed for k, each filled with the texture of the given face slot. Each
polygon is written as "slot: kind/dir — vertex coordinates (x,y,z) in drawing order", in the cell's
local unit cube (x, y across the cell, z up); §3.2.3 rotates them. Texture corners follow §3.3.

| Shape | Description | Polygons (slot: normal — vertices) |
|---|---|---|
| 0 | cube | 0: top — (1,1,1)(1,0,1)(0,0,1)(0,1,1); 1: wall −x — (0,1,1)(0,0,1)(0,0,0)(0,1,0); 2: wall −y — (0,0,1)(1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: wall +y — (1,1,1)(0,1,1)(0,1,0)(1,1,0); 5: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 1 | half cube cut along the vertical diagonal plane through (0,0) and (1,1); keeps the x ≥ y half | 0: top — (1,1,1)(1,0,1)(0,0,1); 1: wall −x+y — (1,1,1)(0,0,1)(0,0,0)(1,1,0); 2: wall −y — (0,0,1)(1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: bottom — (0,0,0)(1,0,0)(1,1,0) |
| 2 | 45° ramp (wedge) rising from z=0 at x=0 to z=1 at x=1 | 0: wall +y — (1,1,1)(0,1,0)(1,1,0); 1: 45° −x — (1,1,1)(1,0,1)(0,0,0)(0,1,0); 2: wall −y — (1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 3 | 45° valley (inner) roof corner: top corners (1,1),(1,0),(0,0) high, corner (0,1) at ground | 0: 45° +y — (1,0,1)(0,0,1)(0,1,0); 1: 45° −x — (1,1,1)(1,0,1)(0,1,0); 2: wall −y — (0,0,1)(1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: wall +y — (1,1,1)(0,1,0)(1,1,0); 5: wall −x — (0,0,1)(0,0,0)(0,1,0); 6 (no slot): bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 4 | 45° hip (outer) roof corner: apex above corner (1,0) | 0: 45° +y — (1,0,1)(0,1,0)(1,1,0); 1: 45° −x — (1,0,1)(0,0,0)(0,1,0); 2: wall −y — (1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,0)(1,0,0); 4: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 5 | inverted ramp (overhang): full top, underside sloping from z=1 at x=0 to z=0 at x=1 | 0: top — (1,1,1)(1,0,1)(0,0,1)(0,1,1); 1: wall −y — (0,0,1)(1,0,1)(1,0,0); 2: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 3: wall +y — (1,1,1)(0,1,1)(1,1,0); 4: underside — (0,1,1)(0,0,1)(1,0,0)(1,1,0) |
| 6 | cube with the upper corner at (0,1) cut off by the plane through (1,1,1), (0,0,1) and (0,1,0) | 0: top — (1,1,1)(1,0,1)(0,0,1); 1: wall −x — (0,0,1)(0,0,0)(0,1,0); 2: wall −y — (0,0,1)(1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: wall +y — (1,1,1)(0,1,0)(1,1,0); 5: steep −x+y — (1,1,1)(0,0,1)(0,1,0); 6 (no slot): bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 7 | corner tetrahedron: apex above (1,0), base triangle (0,0),(1,0),(1,1) | 0: steep −x+y — (1,0,1)(0,0,0)(1,1,0); 1: wall −y — (1,0,1)(1,0,0)(0,0,0); 2: wall +x — (1,0,1)(1,1,0)(1,0,0); 3: bottom — (0,0,0)(1,0,0)(1,1,0) |
| 8 | legacy floor marker (no volume) | 0: floor — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 9 | upper half of a two-cell shallow ramp: z = ½ at x=0 rising to 1 at x=1 | 0: wall +y — (1,1,1)(0,1,½)(0,1,0)(1,1,0); 1: shallow −x — (1,1,1)(1,0,1)(0,0,½)(0,1,½); 2: wall −y — (1,0,1)(1,0,0)(0,0,0)(0,0,½); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: wall −x — (0,1,½)(0,0,½)(0,0,0)(0,1,0); 5: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 10 | lower half of a two-cell shallow ramp: z = 0 at x=0 rising to ½ at x=1 | 0: wall +y — (1,1,½)(0,1,0)(1,1,0); 1: shallow −x — (1,1,½)(1,0,½)(0,0,0)(0,1,0); 2: wall −y — (1,0,½)(1,0,0)(0,0,0); 3: wall +x — (1,0,½)(1,1,½)(1,1,0)(1,0,0); 4: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 11 | shallow valley corner, upper: corners (1,1),(1,0),(0,0) at 1, corner (0,1) at ½ | 0: shallow +y — (1,0,1)(0,0,1)(0,1,½); 1: shallow −x — (1,1,1)(1,0,1)(0,1,½); 2: wall −y — (0,0,1)(1,0,1)(1,0,0)(0,0,0); 3: wall +x — (1,0,1)(1,1,1)(1,1,0)(1,0,0); 4: wall +y — (1,1,1)(0,1,½)(0,1,0)(1,1,0); 5: wall −x — (0,0,1)(0,0,0)(0,1,0)(0,1,½); 6 (no slot): bottom |
| 12 | shallow valley corner, lower: corners (1,1),(1,0),(0,0) at ½, corner (0,1) at 0 | 0: shallow +y — (1,0,½)(0,0,½)(0,1,0); 1: shallow −x — (1,1,½)(1,0,½)(0,1,0); 2: wall −y — (0,0,½)(1,0,½)(1,0,0)(0,0,0); 3: wall +x — (1,0,½)(1,1,½)(1,1,0)(1,0,0); 4: wall +y — (1,1,½)(0,1,0)(1,1,0); 5: wall −x — (0,0,½)(0,0,0)(0,1,0); 6 (no slot): bottom |
| 13 | shallow hip corner, upper: corner (1,0) at 1, the others at ½ | 0: shallow +y — (1,0,1)(0,1,½)(1,1,½); 1: shallow −x — (1,0,1)(0,0,½)(0,1,½); 2: wall −y — (1,0,1)(1,0,0)(0,0,0)(0,0,½); 3: wall +x — (1,0,1)(1,1,½)(1,1,0)(1,0,0); 4: wall +y — (1,1,½)(0,1,½)(0,1,0)(1,1,0); 5: wall −x — (0,1,½)(0,0,½)(0,0,0)(0,1,0); 6 (no slot): bottom |
| 14 | shallow hip corner, lower: corner (1,0) at ½, the others at 0 | 0: shallow +y — (1,0,½)(0,1,0)(1,1,0); 1: shallow −x — (1,0,½)(0,0,0)(0,1,0); 2: wall −y — (1,0,½)(1,0,0)(0,0,0); 3: wall +x — (1,0,½)(1,1,0)(1,0,0); 4: bottom — (1,1,0)(1,0,0)(0,0,0)(0,1,0) |
| 15 | prop cell: the table holds a cube, but the cell is never drawn as geometry | as shape 0 |

Shapes 9 + 10 and 11 + 12, and likewise 13 + 14, pair up to make two-cell shallow slopes. Shipped
models use shapes 0–7, 9, 10 and 15 (§1.3.4).

Texture corner codes per vertex `[STATIC]`: each polygon also lists, for every vertex, the face
texture corner it maps to:

* 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left;
* 4 = left edge at half height, 6 = right edge at half height;
* code 5 occurs once (shape 12, slot 3, at vertex (1,1,½)) and should be treated as the right
  edge at half height `[INFERRED]`.

Read along the face, "top" is the vertex with the larger z (for slopes, the higher edge), and
"left" is the first vertex of the polygon's top edge. Mid-height corners therefore sample the
texture at v = 8 texels inside a 16-texel face (§3.3).

#### 3.2.5 Visibility per view `[STATIC]`

The shape table also lists, for each of the 4 view rotations, the polygons that face the camera.
For the cube, view 0 shows top, +x and +y. View 1 shows top, −y and +x. View 2 shows top, −x and
−y. View 3 shows top, −x and +y. So in view 0 the camera looks from the +x,+y side (the cube
corner (1,1) is nearest the viewer), and view k is the camera turned clockwise by k
quarter-turns, which is the model turned counter-clockwise. A second list, also per view, adds
the polygons seen edge-on (for example shape 1's diagonal wall). An engine can instead compute
visibility from the normals: draw a polygon in view k when its normal, turned by −k quarter-turns,
has a positive component towards (+1,+1, up).

#### 3.2.6 Floor layer

When the BAT saves a model, and in some editing operations, it gives every column a cell at level
0 `[STATIC]`. In a column whose lowest cell is above level 0, or which is empty, it inserts a
floor cell at z = 0:

* model revision ≥ 2: a cube whose top and four wall slots have the default (unpainted)
  appearance;
* revision 1: a shape-8 floor marker.

All 7 296 columns of the 17 shipped files have a cell at z = 0 `[DATA]`, so every lot has a solid
ground layer one level thick. A renderer can rely on it. If it meets a column without a z = 0 cell
(for example in a hand-made file), it should insert the revision-≥ 2 floor cell to match what the
BAT would have saved `[INFERRED]`.

#### 3.2.7 Drawing

Each block cell is drawn as its shape, turned by `rot` (§3.2.3). Faces hidden by an adjacent full cube are not drawn. Prop cells
are not rasterised as geometry: the prop image for the current view (slot `1 + view rotation`,
§1.3.4) is blitted with its anchor at the prop's anchor cell, using the BMP magenta key or TGA alpha
for transparency `[INFERRED]`. Draw order is back-to-front (painter's order) by cell position.

### 3.3 Texture mapping `[STATIC]`

Texture coordinates of a face corner are, in texels:

* u = 16 × u₀ + 16 × (high nibble of the paint/detail offset byte);
* v = 16 × v₀ + 8 × (low nibble of the offset byte).

(u₀, v₀) is the corner's position on the face, given by its texture-corner code in the shape
table (§3.2.4): top-left (0,0), top-right (1,0), bottom-right (1,1), bottom-left (0,1), left
mid-height (0,½), right mid-height (1,½). v₀ is measured downwards from the face's top edge,
matching the top-to-bottom row order of the texture `[INFERRED, medium]`, because the program
builds the numeric corner table at run time. One full face therefore covers 16 × 16 texels, and
the low-nibble offset steps in half-face (8-texel) units, which is what the half-height shapes
need.

The coordinates are normalised by the padded texture size (`invWidth`, `invHeight` of §1.4) with a +0.5 texel
centre offset. Paints tile (wrap) across faces; details are placed on top of the paint, with
transparency from the detail's alpha (TGA) or magenta key (BMP). `color R,G,B` paints are flat
colours.

### 3.4 Lighting `[STATIC]`

Each face gets a colour multiplier computed from six directional lights and its normal **n**:
shade = min(1, Σᵢ max(0, **n**·**dᵢ**) · colourᵢ) per channel, where **dᵢ** = (cos a·cos e,
sin a·cos e, sin e) for light azimuth a and elevation e. The texel colour is multiplied by the
shade `[INFERRED]` (modulation). The program keeps three light sets — *Construct*, *Texture* and
*Preview*; sprites for the game use **Preview**. Light directions are fixed relative to the model (the lot), **not** to the camera
`[DATA]`. Each view renders the model turned (§3.1) under the same model-space lights. The program precomputes
shades for 8 horizontal facings and for faces sloped at 45°, 30° and 60°, plus top and bottom.

| Light | Azimuth a | Elevation e | INI key (`Res/BA/BATConfig.ini`, section `[Lights]`) | Built-in colour if key missing |
|---|---|---|---|---|
| 1 | 0 | 0 | `PreviewNorth` | 255,249,232 |
| 2 | 90° | 0 | `PreviewWest` | 98,98,133 |
| 3 | 0 | 180° | `PreviewSouth` | 98,98,133 |
| 4 | 90° | 180° | `PreviewEast` | 255,249,232 |
| 5 | 15° | 90° | `PreviewTop` | 127,127,127 |
| 6 | 15° | 270° | `PreviewTop` | 127,127,127 |

INI values are `R,G,B` in 0–255, converted to 0–1. Shipped values: North and South 190,174,164,
West and East 71,71,91, Top 141,141,141 — so with the shipped INI one visible wall is warm and
bright, the other cool and dark, roofs mid-grey. `Construct*` and `Texture*` keys set the other two
sets (used only inside the BAT). Light 1 (`North`, direction +x) lights the walls facing +x (row + 1). Light 3 (`South`, −x)
lights the walls facing −x. `West` (+y) and `East` (−y) light the walls facing column + 1 and
column − 1 `[STATIC]`. On screen this gives `[DATA]`:

| View | Left visible wall (model normal) | Light | Right visible wall | Light |
|---|---|---|---|---|
| 0 | +x | North | +y | West |
| 1 | −y | East | +x | North |
| 2 | −x | South | −y | East |
| 3 | +y | West | −x | South |

With the shipped INI (North = South warm, West = East cool) the left wall is the bright, warm one
in views 0 and 2, and the right wall is in views 1 and 3. This is verified on the pre-rendered
sprites of 13 buildings. The mean colour of the
North/South-lit wall pixels is about (160, 152, 137) and that of the West/East-lit walls about
(114, 112, 109). Roofs average about (133, 134, 122).

The measured contrast between the two walls (about 1.4 : 1) is lower than plain modulation by the
shipped light colours predicts (about 2.6 : 1). The exact way the shade combines with the texel
colour is therefore `[OPEN]`: it may involve an ambient term or a remapped shade range. Textures
differ per face, so the measurement is only indicative.

## 4. BAT resource files

### 4.1 Prop description `.met` `[DATA]` (14 files: 8 under `Res/BA/PROPS/<category>/`, 6 in `BACustom/Props/`)

Plain text, CR LF line ends, one line per placeable orientation of the prop:

  `X,Y,Z:"view0","view1","view2","view3"`

X, Y, Z are the prop's box size in cells; the four quoted names are BMP files in the same folder, one
per camera rotation. Props usually list four lines whose image lists are rotations of each other
(so the user can turn the prop), with X and Y swapped as needed; single-line files give one fixed
orientation. Images are 24-bit BMP with magenta (255,0,255) transparency, named `<prop> <k>.bmp`.
The analysis team's parser reads 14/14 `.met` files (429 lines) with every
referenced image present in the same folder `[DATA]`.

### 4.2 Detail palette layout `.plt` `[DATA]` (24 files under `Res/BA/DETAILS/<style>/<set>/`)

Plain text lines `col,row="file name"`: the cell (column, row) of the BAT's detail-palette grid
where the named TGA of the same folder appears. Several `.plt` files may exist per folder
(e.g. `gothic.plt` and `gothicplus.plt`). 24/24 files (809 lines) parse with this grammar; 6 entries name files that are not
shipped and must be skipped `[DATA]`.

### 4.3 Textures

* Stock paints: `Res/BA/PAINTS/{GROUND,ROOFS,WALLS}/*.tga`; stock details:
  `Res/BA/DETAILS/{Traditional,Contemporary}/<set>/*.tga`; stock props: `Res/BA/PROPS/…` `[DATA]`.
* Custom: `BACustom/{Paints,Details,Props}` (and subfolders) `[DATA]`.
* Image sizes should be multiples of 16 px; one 16×16 block covers one block face (readme files)
  `[DATA]`. TGA details use 32-bit with alpha; BMP uses magenta as transparent.
* Library folder names known to the program: `Res\BA\`, `BACustom\`, `Paints\`, `Details\`,
  `Props\`, `Architecture\`, `Building Decoration\`, `Plaza and Street\`, `Yard Objects\`,
  `Flora\`, `Industrial\`, `Rooftops\`, `Vehicles\`, `Ground\`, `Roofs\`, `Walls\`, `Colors\`,
  `Contemporary\`, `Traditional\` `[STATIC]`.

### 4.4 `BATConfig.ini` `[DATA]`

Sections: `[Admin]` (`Enabled`), `[Lights]` (§3.4: `Construct*`, `Texture*`, `Preview*` with
`North`, `West`, `South`, `East`, `Top`; values `R,G,B`), `[Colors]` (BAT editor UI colours,
values `A,R,G,B`: `ConstructSelectedBlock`, `ConstructLeftPlane`, `ConstructRightPlane`,
`ConstructFlatPlane`, `ConstructFaceOutline`, `ConstructLeftGrid`, `ConstructRightGrid`,
`ConstructBottomGrid`, `ConstructIsoGrid`, `ConstructLeftShadowOutline`,
`ConstructRightShadowOutline`, `ConstructBottomShadowOutline`, `ConstructShadow`,
`ConstructIsoVolumeEdge`, `BackgroundFill`, `SelectedHighlight`, `ErasableHighlight`,
`PropsBackground`, `PaletteBackground`, and the handle keys `Construct{Left,Right,Flat}PlaneHandle`
and `…Active`). Only `[Lights]` `Preview*` affects game sprites.

### 4.5 `BAT.DAT`

See §2.2. It is not a texture library; it is the game's database of imported (rendered) BAT
buildings, shipped pre-filled with 15 buildings.

## 5. BAT application (`Baapp.exe`) — functional summary

The BAT is a separate program sharing the model/renderer library with the game `[STATIC]`. It runs
from `Apps/`, single instance (named mutex), uses the game's UI framework and fonts
(`systemloadsave.fbf`, `title12.fbf`, `title9.fbf`, `main9.fbf`, `main10.fbf`, `Serif17.fbf`,
`system9.fbf`), text tables `Res/Text/<LANG>/BATStringsMain.IXF` and `BAMBEStringsMain.ixf`, UI
bitmaps under `Res/BA/UI/Shared/…`, cursors under `Res/BA/UI/Shared/Cursors/` and hotkeys
`Res/BA/Hotkeys/<LANG>/000000B1.KEY`, `000000B2.KEY` `[DATA]` `[STATIC]`.

Modes (from the light sets and cursors) `[STATIC]` `[INFERRED]`:

* **Construct**: place/erase blocks of the block palette in the grid volume; tools Block, Plane
  (fill a plane), Line, Eraser, Reducer, Eyedropper, Magnifier, Hand (pan); lot size and height
  limit set per building.
* **Paint**: apply paints with the Paint Brush (one face) or Paint Bucket (connected faces).
* **Detail**: apply details (decals) with the Decal tool; details span several faces according to
  their size in 16-px units.
* **Props**: place props (Sprite tool) from the categorised prop palette; rotate before placing.
* **File/Preview**: preview with the Preview lights; save `.bld` (§1), with name, author and
  description; rotate the whole building (orientation byte).
* **Publish**: opens `Res/Text/<LANG>/Publish.html` in the browser (the "City Exchange" links)
  `[STATIC]`.

The BAT itself never writes `BAT.DAT`; buildings reach the game only through the in-game import
(§2.4) `[INFERRED]`.

## 6. `MaxisAddOn.dll`

A data-free add-on library shipped with the game `[STATIC]`. It contains no BAT, building-set or
file-format behaviour and reads no files. Building sets (`Res/Sprites/BuildingSets/<id>/`,
`SC3CityScheme.ini` sections `[BuildingSets]` and `[ReplaceableBuildings]`) are discovered by the
game itself (`13-buildings-occupants.md`). An implementation can ignore this file.

## 7. Persisted state

No save-file record is owned by this subsystem. Persistent state is: the `.bld` files (§1), the
database `Apps/BARender/BAT.DAT` (§2.2) and its backup `Apps/Res/BA/RenderedBuildingsBackup.IXF`
(same format), and each city's user building set, the record `99F47014:79F4702D:0` of
`10-save-format.md` §3.7, which references `00006300:BA1DBF1E:<buildingId>`.

## Open questions

1. (Resolved: §3.2.) Remaining: the exact numeric texture-corner table. The corner codes are
   resolved, but the program builds the numeric values at run time.
2. (Vertical scale resolved: §3.1.) Still open: the exact rasteriser rules (edge inclusion,
   texture filtering) and the exact resampling kernel from the 128-px master (§3.1.1).
3. (Wall/light assignment resolved: §3.4.) Still open: how the shade combines with the texel
   colour. Plain modulation over-predicts the wall contrast.
4. (Resolved: §1.8, §2.4.1.) Remaining: whether the replacement dialog strictly requires an equal
   lot size. (Replacements apply per occupant type; the per-city set is the record
   `99F47014:79F4702D:0`, `10-save-format.md` §3.7.)
5. (Resolved, §2.5.) Visible result when a saved city references a BAT building missing from
   `BAT.DAT` is inferred, not observed.
6. Meaning of face-flag bit 6 and the exact semantics of the per-face flags.
7. Whether orientation 2 is intentionally left unrotated on import.
8. Exact removal semantics during import (keep-list vs explicit delete list).
