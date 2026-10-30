# 04 — Graphics formats

Scope: every image encoding the engine must read from the user's installation:

* sprite archives (`Apps/Res/Sprites/**/*.DAT`, plus `Apps/BARender/BAT.DAT`) and their `.SII` sidecars;
* image resources inside `.IXF` files (UI art, land palettes, scenario thumbnails);
* terrain palettes (`Apps/Res/Dirt/PALETTE/`);
* loose BMP and TGA files, `.plt` lists, and cursors;
* conversion of all of these to the 16-bit display format.

Related documents:

* `02-containers.md`: the IXF index, free slots and RefPack (QFS) decompression.
* `03-resource-types.md`: the type registry.
* `25-rendering.md`: sprite placement, zoom/rotation selection, draw order, animation, blending.
* `11-world-terrain.md`: terrain shading.
* `05-text-and-fonts.md`: bitmap fonts.
* `08-ui-framework.md`: button and control behaviour.
* `24-bat-buildings.md`: BAT texture semantics.

Status tags follow `00-README.md`: `[DATA]`, `[STATIC]`, `[INFERRED]`, `[OPEN]`.

---

## 1. Pixel formats and colour depth

### 1.1 Pixel-format codes

The engine works internally in 16-bit colour. Every stored image declares its pixel layout with a
small integer *pixel-format code*. These codes appear in files, so they are interface facts
`[STATIC]`:

| Code | Bits/pixel | Layout (MSB→LSB) | R / G / B masks | In shipped data |
|------|-----------|------------------|-----------------|-----------------|
| 2 | 4 | palette index | — | no |
| 3 | 8 | one byte per pixel. In sprites and UI images this is an opacity value (§3.5) | — | yes |
| 4 | 16 | "whatever 16-bit layout the display uses" (requested by the game when it loads 24-bit BMPs, §8.1) | display | no (request only) |
| 5 | 16 | `0RRRRRGG GGGBBBBB` (RGB555) | 0x7C00 / 0x03E0 / 0x001F | yes (90 sprites) |
| 6 | 16 | `RRRRRRGG GGGBBBBB` (RGB655) | 0xFC00 / 0x03E0 / 0x001F | no |
| 7 | 16 | `RRRRRGGG GGGBBBBB` (RGB565) | 0xF800 / 0x07E0 / 0x001F | yes (nearly everything) |
| 8 | 16 | `RRRRRGGG GGBBBBBB` (RGB556) | 0xF800 / 0x07C0 / 0x003F | no |
| 10 | 16 | RGB555 with bit 15 forced to 1 | as code 5 | no |
| 4–12 | 16 | the whole range is treated as 16 bpp when computing buffer sizes | | |
| 13–14 | 32 | true colour | | no |

The bits-per-pixel mapping used to size raw buffers is: code 2 → 4 bpp, code 3 → 8 bpp,
codes 4–12 → 16 bpp, codes 13–14 → 32 bpp, anything else → invalid `[STATIC]`.

The original game picks its display format from the hardware channel masks:
0x7C00/0x03E0/0x001F → 5, 0xF800/0x07E0/0x001F → 7, 0xFC00/0x03E0/0x001F → 6,
0xF800/0x07C0/0x003F → 8 `[STATIC]`. On modern hardware an implementation should assume RGB565 (7):
almost every asset is authored in it, so it is the pixel-exact choice.

### 1.2 16-bit to 16-bit conversion

If an image's format differs from the display format, each pixel `p` is converted with
the bit operations below. There is no rounding and no dithering `[STATIC]`. The conversions
listed are the only ones the original performs; any other pair is not converted. The game runs in
16-bit colour (`01-architecture.md` §4 step 5), and shipped art is format 7 (RGB565), 5 (RGB555,
one archive) or 3 (8-bit planes), so an implementation with a fixed RGB565 surface needs only
5 → 7. A modern renderer that expands to 8 bits per channel uses §1.4 instead.

| From → To | Rule |
|-----------|------|
| 5 → 7 | `p' = 2·(p AND 0xFFE0) + (p AND 0x001F)`. Red and green move up one bit, the new green LSB is 0 and blue is unchanged. |
| 7 → 5 | `p' = ((p >> 1) AND 0x7FE0) OR (p AND 0x001F)`. The green LSB is dropped. |
| 5 → 6 | `p' = (p AND 0xFC00) + p`. Red gains a zero LSB. |
| 5 → 8 | `p' = p << 1`. Blue gains a zero LSB. |
| 5 → 10 | set bit 15 |
| 6 → 5 | `p' = ((p >> 1) AND 0x7C00) OR (p AND 0x03FF)` |
| 6 → 8 | `p' = ((p AND 0x03FF) << 1) OR (p AND 0xF800)` |

### 1.3 True colour to 16-bit

When 24-bit or 32-bit source pixels (BMP, BAT renders) are packed into 16 bits, the low bits are
truncated `[STATIC]`:

* to 565: `p = ((R AND 0xF8) << 8) OR ((G AND 0xFC) << 3) OR (B >> 3)`;
* to 555: `p = ((R AND 0xF8) << 7) OR ((G AND 0xF8) << 2) OR (B >> 3)`.

### 1.4 Expanding to 8 bits per channel (modern renderer)

If the clone renders in RGBA8888, first produce the exact 16-bit value as the original would, then
expand each channel with bit replication: `R8 = (R5 << 3) | (R5 >> 2)`,
`G8 = (G6 << 2) | (G6 >> 4)`, `B8 = (B5 << 3) | (B5 >> 2)`. This is a recommendation and not
part of the original. Colour-key tests (§3.3, §6, §8.3) must be done on the original
16-bit value, before expansion.

### 1.5 Palettised (8-bit) images

No shipped sprite is palettised. A palettised sprite encoding exists in the loader (§3.6): its
palette entries are already 16-bit values in the image's declared format, so each index maps
straight to a 16-bit colour, which is then converted as in §1.2 `[STATIC]`. 8-bit BMPs (§8.1)
carry a normal BGRX palette, and each palette entry is packed as in §1.3. The display never
runs in 8-bit mode for gameplay `[INFERRED]`.

---

## 2. Sprite archives

### 2.1 Discovery and load order `[STATIC]`

At start-up the program lists the single directory `Apps/Res/Sprites/` **without descending
into sub-directories** (full sequence: `02-containers.md` §7.1):

1. Read every `*.SII` file in that directory (§5).
2. Then open every `*.DAT` file in that directory (case-insensitive match, so `.dat` is
   included) and index its images.

The sub-directories `BuildingSets/<8-hex-digit id>/` and `FloraSets/<8-hex-digit id>/` are **not**
part of this scan. Their archives are mounted on demand, when a city's building-style set or
flora set is selected (`02-containers.md` §7.1, which also gives the override order). The set IDs
are listed in `SC3CityScheme.ini`, see `13-buildings-occupants.md` and `11-world-terrain.md`.
`Apps/BARender/BAT.DAT` is likewise mounted separately (`24-bat-buildings.md`) but uses the same
image payload.

### 2.2 Shipped archives `[DATA]`

| File (under `Apps/Res/Sprites/` unless stated) | Group | Images | Notes |
|---|---|---|---|
| `00000002_Residential.DAT` | 0x00000002 | 1 360 | |
| `00000003_Commercial.DAT` | 0x00000003 | 1 904 | |
| `00000004_Industrial.DAT` | 0x00000004 | 2 320 | |
| `00000005_Roads.DAT` | 0x00000005 | 6 676 | |
| `00000006_Vehicles.DAT` | 0x00000006 | 9 504 | |
| `00000007_People.DAT` | 0x00000007 | 5 120 | |
| `00000008_Other.DAT` | 0x00000008 | 9 156 | |
| `00000009_Landscape.DAT` | 0x00000009 | 580 | |
| `0000000A_Landmarks.DAT` | 0x0000000A | 1 280 | |
| `0000000B_Utilities.DAT` | 0x0000000B | 2 744 | |
| `00000010_Smoke.DAT` | 0x00000010 | 230 | half are alpha planes |
| `00000014_CityObjects.DAT` | 0x00000014 | 152 | |
| `00000015_Boats.DAT` | 0x00000015 | 2 448 | |
| `1000484E_nu18510.DAT` | 0x1000484E | 290 | half are alpha planes |
| `1000484F_DS18511.DAT` | 0x1000484F | 270 | half are alpha planes |
| `10004850_DS18512.DAT` | 0x10004850 | 290 | half are alpha planes |
| `10004854_GY18516.DAT` | 0x10004854 | 90 | half are alpha planes |
| `10004856_LO18518.DAT` | 0x10004856 | 180 | RGB555; half are alpha planes |
| `54AACF44_Holiday.DAT` | 0x54AACF44 | 8 656 | |
| `54DA2E59_HolidayF.DAT` | 0x54DA2E59 | 240 | |
| `552D8025_Landmarks_2.DAT` | 0x552D8025 | 512 | |
| `5544F8BD_Roads_2.DAT` | 0x5544F8BD | 80 | |
| `654FCB99_CityObjects_2.DAT` | 0x654FCB99 | 208 | |
| `6591AD33_Landmarks_German.DAT` | 0x6591AD33 | 80 | |
| `BuildingSets/6523f3d5/MBE_0015.dat` | 0x547B7DF5 | 3 392 | |
| `BuildingSets/e523f3fb/BST_0004.dat` | 0x5432D60E | 2 760 | |
| `FloraSets/39edf62b/Flora_North West.dat` | 0x257C90E7 | 540 | |
| `FloraSets/79ef435a/Flora_Jungle.dat` | 0x556105FA | 480 | |
| `FloraSets/c55a6d62/Flora_Desert.dat` | 0x4562073E | 540 | |
| `FloraSets/c55a6d84/BlackForest.dat` | 0x45576AE7 | 540 | |
| `EffectSprites.dat` | 0x00000000 | 573 | 236 alpha planes |
| `GAME_UI.DAT` | 0x00000000 | 21 | 8 alpha planes |
| `disaster_LOCUST.dat` | 0x00000000 | 55 | |
| `disaster_SPACEJUNK.dat` | 0x00000000 | 280 | |
| `disaster_TOXICCLOUD.dat` | 0x00000000 | 45 | |
| `disaster_WHIRLPOOL.dat` | 0x00000000 | 95 | |
| `Apps/BARender/BAT.DAT` | 0xBA1DBF1E | 240 | BAT-rendered buildings |

Total: 63 691 sprite images plus 240 in `BAT.DAT`.

### 2.3 Resource keys `[DATA]`

Each image is stored as up to two IXF records that share Group and Instance:

| Type | Group | Instance | Payload |
|------|-------|----------|---------|
| 0x00000000 | archive category | sprite image ID | image payload (§3) |
| 0x00000001 | same | same | 8-byte span record (§2.4) |

* Group is the archive's category ID. It equals the hexadecimal prefix of the file name for
  prefixed archives. For building and flora sets it is the set's own image group, which differs
  from the set ID in the folder name (table above). The unprefixed archives (`EffectSprites`,
  `GAME_UI`, `disaster_*`) use Group 0.
* Instance is the 32-bit sprite image ID (§4).
* Building-set, flora-set and BAT archives also hold non-image records. Their keys (written
  Type / Group) include `207EDC0E` / `0000057E` and `E223741F` / `A317745F` (occupant attributes),
  `00006300` and `00006301` in the image group + 1 (sprite descriptors), and `FFD30C03` /
  `80F48961` (flora attributes); see `02-containers.md` §3.1 for the per-archive list. These
  belong to `13-buildings-occupants.md`, `11-world-terrain.md`, `24-bat-buildings.md` and
  `25-rendering.md`. Image indexing uses only Types 0 and 1.

### 2.4 Span record (Type 1) `[DATA][STATIC]`

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 2 | s16 | L | distance in pixels from the left edge of a frame to the registration point |
| 0x02 | 2 | s16 | T | distance from the top edge to the registration point |
| 0x04 | 2 | s16 | R | distance from the registration point to the right edge of a frame |
| 0x06 | 2 | s16 | B | distance from the registration point to the bottom edge |

* For ordinary single-frame sprites, `L + R = width` and `T + B = height` exactly. This holds for
  all 61 400 span records outside the effect, smoke, disaster and `GAME_UI` archives.
* The registration point (anchor) is at `(L, T)` from the frame's top-left pixel. The
  renderer puts this point on the projected world anchor of the object (`25-rendering.md`).
  Values may be negative.
* **Multi-frame strips.** In the effect and disaster archives an image may hold several animation
  frames side by side. The frame width is then `L + R`, and the frame count is
  `width / (L + R)`. For example, a 312-pixel-wide image with `L = R = 26` holds 6 frames, each
  52 wide `[DATA][INFERRED, high]`. The registration point repeats in every frame.
* In `00000010_Smoke.DAT` 14 records give `L + R = width − 1` and `T + B = height − 1`. Treat
  `L`/`T` as authoritative.
* 1 232 colour images in `54AACF44_Holiday.DAT` have no span record, and none of them has an
  SII record either (§5): no registration data exists for them in the shipped files `[DATA]`
  (see Open questions).
* `EffectSprites.dat` has 72 images without a span record: 70 opacity planes, which take
  their colour image's placement, and 2 colour images `[DATA]` (see Open questions).
* The span record is the binary form of the SII record. Every one of the 9 032 shipped SII
  records has a span record with the same Group and Instance, and the values match exactly.
  The SII files therefore add no registration data beyond the span records `[DATA]`.

---

## 3. Sprite image payload (Type 0)

### 3.1 Outer header `[STATIC][DATA]`

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 1 | u8 | pixelFormat | §1.1 code; 3, 5 or 7 in shipped data |
| 0x01 | 1 | u8 | encoding | 0 = bitmap path (§3.4, §3.6), 1 = RLE-sprite path (§3.3) |
| 0x02 | 2 | u16 | reserved | 0 |
| 0x04 | 4 | u32 | flags | §3.2 |
| 0x08 | 4 | u32 | width | pixels |
| 0x0C | 4 | u32 | height | pixels |
| 0x10 | 4 | u32 | bodyLength | bytes from offset 0x10 to the end of the payload (= payload size − 16) |
| 0x14 | bodyLength − 4 | — | body | RefPack stream when flag 0x00080000 is set |

Shipped combinations (all 63 931 payloads, including `BAT.DAT`) `[DATA]`:

| pixelFormat | encoding | flags | Count | Meaning |
|---|---|---|---|---|
| 7 | 1 | 0x00080000 | 62 702 | RGB565 RLE sprite, RefPack-compressed |
| 5 | 1 | 0x00080000 | 90 | RGB555 RLE sprite, RefPack-compressed (only `10004856_LO18518.DAT`) |
| 3 | 0 | 0x10080000 | 1 139 | 8-bit opacity plane, RefPack-compressed raw bytes |

### 3.2 Flags `[STATIC]`

| Bit / value | Meaning |
|-------------|---------|
| 0x00080000 | the body is RefPack-compressed (decompress from payload offset 0x14) |
| 0x10000000 | raw bitmap: the decompressed body is exactly `width × height × bpp/8` bytes, row-major, top row first, with no row padding |
| 0x00008000 | a codec this loader does not support. The payload is rejected. |
| other low values | the uncompressed encodings in §3.6, used only when `encoding` = 0 and 0x10000000 is clear |

For `encoding` = 1 the loader requires flag 0x00080000 to be set and 0x00008000 to be clear.
Otherwise the image fails to load.

For `encoding` = 0 with 0x00080000 set and 0x10000000 clear (a §3.6 body that is also
RefPack-compressed), the behaviour was not determined [OPEN: is the body decompressed first and
then read as §3.6?]. No shipped payload uses this combination; an implementation should
decompress first and then apply §3.6.

### 3.3 RLE sprite (encoding = 1) `[DATA]`

RefPack-decompressing the body produces a self-contained RLE sprite. Its `size` field equals the
decompressed length.

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | size | total size of this block in bytes |
| 0x04 | 2 | u16 | width | = outer width |
| 0x06 | 2 | u16 | height | = outer height |
| 0x08 | 2 | u16 | version | always 4 |
| 0x0A | 2 | u16 | pixelFormat | = outer byte 0 (5 or 7) |
| 0x0C | 2 | u16 | keyColour | transparent colour in that format: 0xF81F (565 magenta) for format 7, 0x7C1F (555 magenta) for format 5 |
| 0x0E | 2 | u16 | reserved | 0 |
| 0x10 | 8 × height | — | rowTable | one row record per scan line, top row first |
| 0x10 + 8·height | 2 × N | u16[N] | pixels | the stored pixels of all rows, concatenated |

Row record:

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | pixelIndex | index, counted in pixels, of this row's first stored pixel in `pixels` |
| 0x04 | 2 | u16 | skip | transparent pixels before the stored run |
| 0x06 | 2 | u16 | countFlags | bits 0–14: number of stored pixels `n`; bit 15: "solid run" flag |

Invariants verified on every shipped sprite `[DATA]`:

* each row has exactly one stored run;
* `pixelIndex` of row 0 is 0, and `pixelIndex(y+1) = pixelIndex(y) + n(y)`;
* the sum of all `n` equals `N`;
* `skip + n ≤ width`;
* bit 15 is set exactly when the run contains no pixel equal to `keyColour` (also set on empty
  runs). This holds for all 2 567 729 rows, with no exceptions.

Decoding algorithm:

1. Start with a `width × height` canvas that is fully transparent.
2. For each row `y`, take `n` pixels from `pixels[pixelIndex …]` and place them at columns
   `skip … skip + n − 1`.
3. Any placed pixel equal to `keyColour` stays transparent. Bit 15 set means the run contains no
   key pixel, so it may be copied without testing each pixel; the result is the same
   `[STATIC][DATA]`.
4. Convert the opaque pixels to the display format (§1.2).

The colour image has 1-bit transparency only. Smooth edges come from a separate opacity
plane (§3.5).

### 3.4 Raw 8-bit plane (encoding = 0, flags = 0x10080000) `[DATA]`

The decompressed body is exactly `width × height` bytes, rows top to bottom, with no padding.
The pixel format is always 3. In shipped data every byte is an opacity level from 0 to 31
(§3.5).

### 3.5 Opacity (alpha) planes `[DATA][INFERRED]`

Every format-3 sprite is an opacity mask of the same size as a colour sprite, labelled
"Alpha" in the SII comments. Although stored one byte per pixel, the values are **5-bit opacity
levels from 0 to 31**: across all 1 139 shipped masks exactly the 32 values 0–31 occur, and
none is larger `[DATA]`. 0 means fully transparent and 31 means fully opaque `[INFERRED, high]`.
About 65 % of mask pixels are 0. Do not treat the bytes as 0–255 alpha, or every effect will be drawn almost invisible.

**Original blend arithmetic** `[STATIC]`. For a pixel-exact clone:

* Blending is done per 5-bit channel:
  `Blend(s, d, α) = floor((32·d + α·(s − d)) / 32)`, clamped to 31. Here `s` is the source
  (sprite) channel, `d` the destination (screen) channel and `α` a level from 0 to 31.
  α = 0 gives `d`; α = 31 gives `floor((31·s + d)/32)`, so even a fully opaque level keeps 1/32
  of the background.
* For RGB565 the channels used are red = bits 11–15, green = bits 6–10 (the top 5 bits of
  green) and blue = bits 0–4. The result is `(R' << 11) | (G' << 6) | B'`, so the green LSB of a
  blended pixel is always 0.
* RGB555 uses the same formula on its three 5-bit fields.
* Where a mask is combined with a second per-pixel or global level `m2` (also 0–31), the
  effective level is `α = (mask × m2) >> 5`. The source of `m2` for each effect is specified in
  `25-rendering.md` (`[OPEN]` here).
* Key-coloured source pixels are skipped before blending, exactly as in §3.3.

A clone that blends in floating-point RGBA may use `opacity = level / 32` per channel; this
differs from the original by at most one 5-bit step. Masks are used for smoke, clouds, toxic clouds, disasters, effects,
water and fountain sprays (archives with Groups `0x1000484E`–`0x10004856`) and some UI art.

Shipped alpha planes per archive:

| Archive | Alpha planes |
|---------|--------------|
| `00000010_Smoke.DAT` | 115 |
| `0x1000484E_nu18510.DAT` | 145 |
| `0x1000484F_DS18511.DAT` | 135 |
| `0x10004850_DS18512.DAT` | 145 |
| `0x10004854_GY18516.DAT` | 45 |
| `0x10004856_LO18518.DAT` | 90 |
| `EffectSprites.dat` | 236 |
| `GAME_UI.DAT` | 8 |
| disasters | 220 |

The sprite descriptor that references both images defines which colour image a mask belongs to
(`25-rendering.md`). The ID is not derived from a fixed rule. Observed conventions `[DATA]`:

| Archive | Colour ID | Mask ID |
|---|---|---|
| `GAME_UI.DAT` | `ssss0000` | `ssss0001` (the `[FloraSets]` lines of `SC3CityScheme.ini` name both images of each flora-set icon, e.g. `0x3FBE0000` and `0x3FBE0001`, which confirms the pairing) |
| `disaster_LOCUST`, `disaster_WHIRLPOOL`, `disaster_SPACEJUNK` (some sets) | `ssss0000 + z` | `ssss0000 + 5 + z` (z = zoom 0–4) |
| `disaster_TOXICCLOUD` | `4858x00k` | `4858(x+1)00(k+5)` (one phase per 0x1000 block) |
| `EffectSprites.dat` | `FFFCxxxx` | `FFFCxxxx + 0x100` |
| `00000010_Smoke` and the archives with Groups `0x1000484E`–`0x10004856` | odd/even alternation: masks on one parity, colour on the other, same size | |

### 3.6 Uncompressed bitmap encodings (supported by the loader, not shipped) `[STATIC]`

These apply when `encoding` = 0 and flag 0x10000000 is clear. The `flags` value selects the
body layout. Each pixel is u16 in the declared format. The transparent fill colour is the sprite's
key colour.

| flags | Body |
|-------|------|
| 0x001 | raw u16 pixels, `width` per row, rows top to bottom |
| 0x002 | runs of (u8 count, u16 pixel), row-major; a row ends after `width` pixels |
| 0x004 | as 0x002 but column-major (each column top to bottom, columns left to right) |
| 0x008 | stream of u16 pixels, row-major; the value 0x0001 is an escape followed by a u8 count of transparent pixels |
| 0x010 | as 0x008 but column-major |
| 0x080 | u8 paletteCount, u8 transparentIndex, u16 palette[paletteCount], then u8 indices row-major; `transparentIndex` is followed by a u8 count of transparent pixels |
| 0x100 | as 0x080 but column-major |
| 0x8000, 0xC000 | not supported (rejected) |

### 3.7 Decoder coverage

Results of decoding every shipped sprite payload with the rules of this section `[DATA]`:

* 63 931 / 63 931 image payloads decode, across all 36 sprite archives plus `BAT.DAT`;
* 0 header inconsistencies;
* every RLE invariant in §3.3 holds.

---

## 4. Sprite image ID structure

The rendering-side descriptor that turns an object state into a sprite ID belongs to
`25-rendering.md`. This section describes the layout the IDs follow inside the archives.

### 4.1 General form `[DATA]`

`Instance = (setID << 16) | k`

* `setID` (high 16 bits) identifies one visual object or object family.
* `k` (low 16 bits) selects the variant within the set.

### 4.2 Index `k` for static objects `[DATA]`

Buildings, landmarks, city objects, roads, flora, landscape and utilities use
`k = zoom × 4 + rotation`:

* `rotation` 0–3 is the view rotation;
* `zoom` starts at 0 for the most zoomed-out view;
* each zoom step doubles the image width, which the data confirms. For a 1×1 lot, for example,
  widths are 8, 16, 32, 64 (and 128).

Zoom images per set `[DATA]`:

| Content | Zoom images per set | Widths for a 1-tile object |
|---|---|---|
| Buildings, landmarks, city objects, holiday buildings, building sets | 4 (k = 0…15) | 8 / 16 / 32 / 64 |
| Roads, flora, landscape, most utilities, some city objects | 5 (k = 0…19) | 8 … 128 |

Objects with only four zoom images are drawn at the closest zoom (tile width 128) by
enlarging the zoom-3 image to twice its size. The sprite descriptor (composite-sprite record,
Type 0x00006300; see `25-rendering.md`) carries a per-frame flag value 2 that marks a frame as
"draw at 2×" at the closest zoom `[INFERRED, high; consistent with every BAT-rendered set]`. Draw such frames with
nearest-neighbour pixel doubling and doubled span values (2L, 2T, 2R, 2B). `25-rendering.md`
gives the full rule and the frame-record layout.

BAT-rendered buildings (`Apps/BARender/BAT.DAT`, Group 0xBA1DBF1E, and buildings imported
from `.bld` files; see `24-bat-buildings.md`) follow the same building convention: 4 zooms
(8 / 16 / 32 / 64 px per tile) × 4 rotations, `k = zoom × 4 + rotation`, 2× at the closest zoom
`[DATA]`. In `BAT.DAT` every set has 16 images per block. Set 0xBFDD shows widths 24, 48, 96
and 192 for a 3-tile-wide object. `BAT.DAT` also contains Type 0x00006300 composite-sprite
records (data starting with the ASCII marker `BIN` followed by CR) that list the sprite keys
of each BAT building.

Sets with more images add further blocks of 16 or 20:

* animation frames of animated buildings;
* construction or variant states;
* in some sets a second block at `k` = 0x1000 (for example `00000003` set 0x007C,
  `BST_0004` set 0x01CB).

The `ZoomCount`, `RotCount`, `ZoomSetCount`, `AnimSetCount` and `AnimFrames` fields of the sprite
descriptor give the strides (`25-rendering.md` defines the descriptor fields). Image indices follow
`k = animFrame × AnimSetStride + zoom × ZoomSetStride + rotation`, where ZoomSetStride is the number
of images per zoom (one zoom's rotations, headings and slopes) and AnimSetStride the number of
images per animation frame (all zooms of one frame). How the two strides are computed from the
named descriptor fields is specified in `25-rendering.md` `[OPEN]` here. Observed strides `[DATA]`:

* buildings: 4 zooms × 4 rotations, ZoomSetStride 4;
* vehicles: 3 zooms, ZoomSetStride 48 (16 headings × 3 slopes);
* people: 2 zooms, ZoomSetStride 8.

Zoom numbering in this document is the view zoom 0–4, 0 farthest. An object with fewer than 5
image zooms covers the **closest** view zooms: vehicles' image zooms 0–2 are view zooms 2–4, and
people's image zooms 0–1 are view zooms 3–4; at farther zooms they are not drawn
(`25-rendering.md` §4.2) `[INFERRED]`.

The exact descriptor layout is in `25-rendering.md` `[STATIC]`.

### 4.3 Moving objects `[DATA]`

Vehicles (144 images per set, 576 for trains and multi-car sets), boats (144 or 720) and people
(48–144) do not follow the 4-per-zoom pattern. Their `k` runs through headings, slopes and
animation frames first, and then zoom. For example, boat set 0x2BDA has 16 headings, then
repeats.

### 4.4 Effects and disasters `[DATA]`

`EffectSprites.dat` and the `disaster_*` archives use IDs whose high word is a set code. The
code may be a value such as 0xFFFC, 0xFFFE, 0x3FAC–0x3FB2 or 0x4858. The low word encodes
phase, zoom and frame. The SII comments name the sets, for example tornado, plasma ball, UFO,
clouds, debris, explosion, water, locust fly/eat and toxic cloud opening/cycle.

Zoom always runs over 5 levels, either consecutively or as `+1` steps within a phase block.
Opacity planes follow the conventions in §3.5.

### 4.5 Seasonal and holiday variants `[DATA][INFERRED]`

Holiday decorations are separate archives with their own Group: `54AACF44_Holiday` and
`54DA2E59_HolidayF`. The `k` index is not used to encode holiday variants.

When holiday mode is active (`25-rendering.md`, `13-buildings-occupants.md`), the descriptor
records in `CSAttrib_Holidays.IXF` point objects at these sprite IDs. European and Asian-style
building sets are separate archives under `BuildingSets/`, keyed by their own Group.

---

## 5. SII sidecars

### 5.1 Syntax `[STATIC][DATA]`

An SII file is plain ASCII with CR-LF line ends. The authoritative grammar (comment block, optional
`Version:` line, `Record Count:` line, exactly *N* record lines, field and whitespace rules, and
failure behaviour) is `02-containers.md` §4.1. This section only summarises the two dialects that
ship.

**Version 1** (`54AACF44_Holiday.SII`, `54DA2E59_HolidayF.SII`, `5544F8BD_Roads_2.SII`,
`654FCB99_CityObjects_2.SII`, `10004856_LO18518.SII`): leading `;` comment lines, `Version: 1`,
`Record Count: N`, then *N* record lines `GGGGGGGG, IIIIIIII, L, T, R, B`.

**Version 0** (`GAME_UI.SII`, `EffectSprites.sii`, `disaster_LOCUST.sii`,
`Disaster_SPACEJUNK.sii`, `disaster_TOXICCLOUD.sii`): leading `;` comment lines, `Record Count: N`,
then *N* record lines `IIIIIIII, L, T, R, B`. There is no Group field; the Group is 0, which is the
Group of the same-named archive.

Notes on the shipped files `[DATA]`:

* Some record lines have spaces after a comma, and some have tabs and spaces after the last
  number (before a trailing `; comment`). No shipped line has whitespace before a comma.
* One record line of `EffectSprites.sii` ends with a stray comma after the fifth number. Under
  the grammar, text after the last number is ignored, so the line is valid.
* `disaster_LOCUST.sii` declares `Record Count: 56` but has only 55 record lines followed by a
  blank line. The 55 records are registered and the file then counts as failed
  (`02-containers.md` §4.2).

Records registered per file `[DATA]`: Holiday 7 424, HolidayF 240, Roads_2 80, CityObjects_2 208,
LO18518 180, SPACEJUNK 280, EffectSprites 499, GAME_UI 21, LOCUST 55, TOXICCLOUD 45. Total 9 032:
8 977 from the 9 files that read to completion plus 55 from `disaster_LOCUST.sii`.

### 5.2 Meaning for the renderer `[DATA][STATIC]`

`L, T, R, B` have exactly the meaning of the span record (§2.4): the registration point is
`(L, T)` and the frame size is `(L + R) × (T + B)`.

SII files are read before the DAT files. Every shipped SII record duplicates a Type-1 span record
with identical values (§2.4), so which source takes precedence is not observable with shipped data
(`02-containers.md` §4.2). An implementation may ignore SII files entirely when reading shipped
data, or read them first and let the span records overwrite them.

The comments next to records (frame counts, set names) are for humans only and are ignored.

---

## 6. Image resources in IXF files (Type 0x62B9DA24)

### 6.1 Where they occur `[DATA]`

All entries of Type 0x62B9DA24 are images. Group is the image family; Instance is the image ID.

| File | Count | Content |
|------|-------|---------|
| `Apps/Res/UI/Shared/000005E5_Maps.ixf` | 44 | map and data-view widgets |
| `00640480_MainUI.ixf`, `02F78C6D_MainUI.ixf`, `00800600_MainUI.ixf`, `01024768_MainUI.ixf` | 64 + 112 + 1 + 4 | main UI. The per-resolution files hold the bottom bar for 640×480, 800×600 and 1024×768. |
| `22DEC90F_TerrEdit.ixf` | 32 | terrain editor panel |
| `22DEC92D_OtherCtrls.ixf`, `42DEC7D2_GonzoCtrls.ixf` | 7, 28 | generic controls |
| `52DB60A0_Advisors.ixf` | 201 | advisor portraits |
| `831D112B_AdvTopics.ixf` | 83 | advisor topic pictures |
| `85535958_CitySchemeUI.ixf` | 13 | city-scheme dialog art |
| `DLG.IXF`, `MAIN.IXF`, `SYS.IXF` | 11, 27, 84 | dialogs, main-menu backgrounds, system UI |
| `Apps/Res/Dirt/PALETTE/455A72B1_LandPalettes.IXF` | 5 | land palettes (§7) |
| `Cities/Scenarios/*.SNR` | 1 each (13) | scenario thumbnail, key `62B9DA24:19A6CEA1:00000001` (see `23-scenarios.md`) |

### 6.2 Payload kinds `[STATIC]`

The loader reads the first 8 bytes and chooses the kind from the first u32:

| First bytes | Kind | Layout |
|-------------|------|--------|
| `42 4D` ("BM") | Windows BMP | a complete BMP file (§8.1) |
| u32 = 1 | UI raw image | header (below), then `dataSize` bytes of raw pixels |
| u32 = 2 | UI compressed image | header (below), then `dataSize` bytes of compressed data |
| u32 = 0x62B9DA24 | UI true-colour image | u32 width, u32 height, then `width × height` u32 pixels (32-bit colour, packed into the surface pixel by pixel) |
| anything else | invalid | |

UI image header, used by kinds 1 and 2:

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | kind | 1 or 2 |
| 0x04 | 4 | u32 | dataSize | bytes following this 20-byte header (payload size − 20) |
| 0x08 | 4 | u32 | width | pixels |
| 0x0C | 4 | u32 | height | pixels |
| 0x10 | 4 | s32 | pixelFormat | §1.1 code (7 or 3 in shipped data) |
| 0x14 | dataSize | — | data | kind 1: `width × height × bpp/8` raw bytes, rows top to bottom, no padding. Kind 2: u32 compressedLength, equal to dataSize (so it counts itself), then a RefPack stream of exactly dataSize − 4 bytes starting at payload offset 0x18 (header `10 FB`), which decompresses to exactly `width × height × bpp/8` bytes. [DATA 711/711] |

After loading, the image is converted to the display format (§1.2) `[STATIC]`.

Shipped payloads `[DATA]`, decoded 729 / 729:

* 710 are kind 2 with format 7;
* 1 is kind 2 with format 3, an 8-bit plane;
* 18 are 24-bit BMP: the 5 land palettes and 13 scenario thumbnails of 163 × 152.

Kind 1 and the true-colour kind do not occur in shipped files.

### 6.3 Transparency and button strips `[DATA][INFERRED]`

UI images have no alpha channel. 201 of the 710 shipped 16-bit UI images contain pixels equal to
0xF81F (565 magenta), always as background around non-rectangular art such as round buttons and
icons; the other 509 are fully opaque rectangles `[DATA]`. Magenta is the transparent key for
UI drawing, the same key as sprites `[INFERRED, high]`. Key handling per control is in
`08-ui-framework.md`.

A button image is a horizontal strip of equal-width cells, with cell width = image width / cell
count. The strip layouts are part of the button control (`08-ui-framework.md`), summarised here:

* **4 cells.** Order: disabled, normal, pressed, hover. Toggle buttons show "pressed" (or
  "hover", depending on style) while checked. Some styles have 3-slice stretchable cells, where
  the left and right thirds are fixed caps.
* **8 cells** (check boxes and radio buttons), as (checked, unchecked) pairs: normal 0/1,
  hover 2/3, pressed 4/5, disabled 6/7.

For example, the main menu button images (Instances 0x2001…0xA006 in `*_MainUI.ixf`) are
144 × 36, which is four 36 × 36 cells.

---

## 7. Terrain palettes (`Apps/Res/Dirt/PALETTE/`)

### 7.1 Files and slots `[STATIC][DATA]`

The terrain renderer loads up to nine palette images into fixed slots. Loose files are read from
`Res\Dirt\Palette\` (case-insensitive file names):

| Slot | Source | Size (w × h) | Notes |
|------|--------|--------------|-------|
| 0 | `palBasic.bmp` | 1 × 53 | 51 distinct colours: a list of fixed terrain colours |
| 1 | `data1Pal.bmp` | 64 × 32 | data-view colour ramp |
| 2 | `data2Pal.bmp` | 64 × 32 | data-view colour ramp |
| 3 | `data3Pal.bmp` | 64 × 32 | data-view colour ramp (smooth gradient) |
| 4 | land palette from IXF: key `62B9DA24:455A72B1:n` | 32 × 512 | `n` = palette number of the active landscape scheme |
| 5 | `palUnder.bmp` | 32 × 512 | |
| 6 | `palWater.bmp` | 1 × 256 | water colour ramp |
| 7 | `palEdgeL.bmp` | 1 × 512 | map-edge (cliff side) ramp, light face |
| 8 | `palEdgeD.bmp` | 1 × 512 | map-edge (cliff side) ramp, dark face |

All are uncompressed 24-bit BMPs.

The landscape scheme number `n` comes from `SC3CityScheme.ini` section `[LandScapes]`, fourth
field of each entry. The shipped values are 1–5; see `11-world-terrain.md` for the scheme
semantics. `455A72B1_LandPalettes.IXF` instance 1 is byte-identical to `PALLAND.BMP`. The loose
files `ArcticLand.bmp`, `CaribbeanLand.bmp`, `DesertLand.bmp` and `PALLAND.BMP` are not loaded
by name; they are authoring copies `[STATIC]`.

### 7.2 Layout of the 32 × 512 land and under palettes `[DATA][INFERRED]`

Rows are counted in file order, where row 0 is the **first stored** scan line, i.e. the bottom
line of the picture as displayed (BMP rows are stored bottom-up). This differs from the top-first
order the loader produces for display (§8.1): palette row *r* in this section is display row
511 − *r*. The height mappings in `11-world-terrain.md` and `25-rendering.md` use the row numbers
of this section:

* Rows 0–255 form a dark-to-light ramp for terrain below sea level (sea bed).
* Rows 256–511 form the above-sea-level ramp: shore and grass at row 256, rising through
  vegetation and rock to snow at row 511.
* Rows are therefore indexed by terrain height, with sea level at the boundary between rows 255
  and 256 `[INFERRED, high]`.
* Brightness rises monotonically from column 0 to column 31 in every row. The column is a
  lighting or slope-shade index, with 0 the darkest (facing away from the light) and 31 the
  brightest `[INFERRED, high]`.

`palUnder` has the same layout with darker tones `[INFERRED]`. The exact mapping from tile
height and slope to (row, column), and any interpolation, is specified in `11-world-terrain.md`
and `25-rendering.md`. It is `[OPEN]` here.

---

## 8. Loose files

### 8.1 BMP `[DATA][STATIC]`

All 677 shipped `.bmp` files are uncompressed Windows BMPs:

* `BITMAPINFOHEADER` (40 bytes);
* `biCompression` = 0;
* positive height, meaning bottom-up rows;
* rows padded to 4 bytes;
* bit counts 24 (673 files) or 8 (4 files).

The game's BMP reader accepts only `biCompression` = 0 and bit counts 4, 8 or 24, and rejects
everything else. For 4 and 8 bpp it reads the colour table as B, G, R, reserved. The image is
converted to the display format with §1.3 truncation, flipping bottom-up rows so the top row is
first.

Groups of shipped BMPs:

| Location | Count | Bit count | Use |
|----------|-------|-----------|-----|
| `Apps/Res/Sprites/BACK0.BMP` … `BACK3.BMP` | 4 | 24 | view background tile for zoom 0–3, each 128 × 64 |
| `Apps/Res/Sprites/BACK4.BMP` | 1 | 24 | view background tile for zoom 4, 256 × 128 |
| `Apps/Res/Dirt/PALETTE/*.BMP` | 12 | 24 | terrain palettes (§7) |
| `Apps/Res/UI/Shared/Cursors/*.bmp` | 96 | 95 × 24, 1 × 8 | software-cursor images, one per `.cur`; 95 are 32 × 32 and one is 4 × 4 (§8.3) |
| `Apps/Res/BA/**`, `Apps/BACustom/**` | 550 | 24 (one 8) | BAT props, BAT UI and BAT cursor images (`24-bat-buildings.md`) |
| `Apps/ScenarioCreator/**` | 14 | 8 / 24 | Scenario Creator tool art (not used by the game) |

**Building Architect Tool UI art.** `Apps/Res/BA/UI/SHARED/<GGGGGGGG>_<Name>/` holds loose
BMP equivalents of UI IXF images:

* The directory prefix is the image Group.
* Each file name starts with the 8-hex-digit Instance, optionally followed by a descriptive name.
  Examples: `C41602B3_CBUI/00000001.bmp`; `SYS/62B19CE9SysBtns.BMP`, which is the same image
  ID as the `SYS.IXF` image 0x62B19CE9.
* These are 24-bit BMPs. 19 of the 104 files use magenta (255, 0, 255) for transparent areas;
  the rest are opaque. They use the same button-strip conventions as §6.3.

Only the BAT loads them `[DATA][INFERRED]`; see `08-ui-framework.md` for the role of each
instance.

**Background tiles.** The view loads `Res\Sprites\BackN.bmp` with `N` = the current zoom digit
0–4, in the 16-bit display format. It tiles the image behind the city where no terrain is
drawn `[STATIC]`. Tiles at higher zoom show a faint diamond grid matching the tile size.

### 8.2 TGA (BAT textures) `[DATA]`

All 879 `.tga` files are under `Apps/Res/BA/` and `Apps/BACustom/`, and are BAT texture
sources:

| Location | Image type | Bits | Alpha bits | Origin | Count |
|----------|-----------|------|-----------|--------|-------|
| `BA/DETAILS/**`, `BACustom/Details` | 2 (uncompressed true colour) | 32 | 8 | bottom-left | 759 |
| `BA/DETAILS/**` | 2 | 24 | 0 | bottom-left | 3 |
| `BA/PAINTS/**` | 2 | 24 | 0 | bottom-left | 98 |
| `BA/PAINTS/**` | 2 | 32 | 8 | bottom-left | 2 |
| `BA/PAINTS/**` | 10 (RLE true colour) | 24 | 0 | bottom-left | 17 |

In every file the image-ID field and colour map are empty (ID length 0, colour-map type 0).

Header (standard TGA, 18 bytes): u8 idLength, u8 colourMapType, u8 imageType, u16 cmFirst,
u16 cmLength, u8 cmEntrySize, u16 xOrigin, u16 yOrigin, u16 width, u16 height, u8 bitsPerPixel,
u8 descriptor. Descriptor bits 0–3 hold the alpha bit count; bit 5 = 1 means rows are stored
top-first, and it is 0 in all shipped files. Pixel order is B, G, R(, A).

Type-10 RLE follows the TGA standard. Each packet starts with a header byte: if bit 7 is set,
the next pixel is repeated `(header & 0x7F) + 1` times; otherwise `(header & 0x7F) + 1` literal
pixels follow.

TGA files are decoded only by the Building Architect Tool. The game contains no TGA decoder: when
it imports a `.bld` building it uses texture **file names** only as keys that map to texture IDs,
and the textures it draws come from already-rendered sprites (`24-bat-buildings.md`). A compatible
game engine therefore never needs to read TGA pixels `[STATIC]`.

### 8.3 Cursors `[DATA][STATIC]`

* `Apps/Res/UI/Shared/Cursors/` contains 96 `.cur` files and 96 same-named `.bmp` files.
  `Apps/Res/BA/UI/SHARED/CURSORS/` contains 17 of each for the BAT.
* 112 `.cur` files are standard Windows cursor files: resource type 2, one 32 × 32 1-bpp image,
  hotspot in the directory entry (for example the pan arrows have hotspots near their tips).
  One file is an icon-type file (type 1) with an 8-bpp 32 × 32 image.
* Cursors are loaded by file name with the operating system's cursor-from-file facility. The
  file names are hard-coded per tool; the list is in `08-ui-framework.md`. The view uses
  `Res\UI\Shared\Cursors\` (`res\ui\shared\cursors\`).
* There are no animated cursors: no `.ani` files and no RIFF cursors ship. Busy states use the
  static `wait cursor.cur` `[DATA]`.
* The `.bmp` twins are 32 × 32 colour versions with magenta (255, 0, 255) as the transparent
  background. One file uses (254, 0, 254), one uses white, and one is 4 × 4. The sprite view
  explicitly loads `anchor point cursor.BMP` as an image. The other BMPs are not referenced
  by name `[STATIC]`.
* A modern implementation may use either form. The `.cur` hotspot is authoritative.

### 8.4 `.plt` files (BAT texture palettes) `[DATA]`

There are 24 text files, one per BAT detail set (for example
`BA/DETAILS/Contemporary/JPMerge/JPMerge.plt`). Each is a list of CR-LF lines of the form
`a,b="<file name>.TGA"`, where `a,b` is a cell position in the BAT texture picker and the
quoted string names a TGA in the same directory. Semantics are owned by `24-bat-buildings.md`.

`Apps/ScenarioCreator/SimCity Scenario Creator.pal` is a RIFF `PAL ` (Windows logical palette)
for the external Scenario Creator tool. The game does not use it.

### 8.5 Fonts

Bitmap fonts (`*.fbf`, 56 files) are glyph images in their own container format, specified in
`05-text-and-fonts.md`. They are not covered here.

### 8.6 Other image files (not used by the game engine) `[DATA]`

* `*.jpg` (31), `*.png` (67) and `*.ico` (5) are installer leftovers (`tmp/`), per-language
  help-text art (`Res/Text/<LANG>/`) and Scenario Creator icons.
* The shell uses JPEG only for the city-snapshot feature (`%s--f--%d.jpg` naming) and HTML help
  (`HTML\missing.jpg`).
* Besides the formats of §3, §6 and §8.1, the game handles only JPEG and GIF, for the snapshot
  and help features above; no shipped game art uses them. TGA is not decoded by the game (§8.2)
  `[STATIC]`.

---

## 9. Verification summary

| Check | Result |
|-------|--------|
| Sprite image payloads decoded (all archives + `BAT.DAT`) | 63 931 / 63 931 |
| RLE row-table invariants (§3.3) | 0 violations |
| Bit-15 "no key pixel" rule | 2 567 729 / 2 567 729 rows |
| Span record = SII record (every SII record has a span) | 9 032 / 9 032 |
| `L + R = width` and `T + B = height` in single-frame archives | 61 400 / 61 400 |
| IXF Type 0x62B9DA24 images decoded | 729 / 729 |
| Loose BMP decoded | 677 / 677 |
| TGA headers parsed | 879 / 879 |
| CUR parsed | 113 / 113 |
| SII files parsed (grammar of `02-containers.md` §4.1) | 10 / 10; 9 read to completion, `disaster_LOCUST.sii` stops after 55 of 56 declared records |

---

## Open questions

1. Which SII record or span record takes precedence when both exist and disagree? They never
   disagree in shipped data.
2. Registration point for the 1 232 holiday colour images and 2 effect colour images that have
   neither a span record nor an SII record. They may never be drawn; if they are, a sensible
   default is bottom-centre: registration point `(L, T) = (⌊width / 2⌋, height)` in the image's
   own pixel coordinates, i.e. the middle of the bottom edge, placed on the object's projected
   world anchor (§2.4). (The 70 effect opacity planes without spans take the placement of
   their colour image.)
3. How the sprite descriptor links each opacity mask to its colour image, and where the
   second blend level `m2` comes from. Owned by `25-rendering.md`.
4. Mapping of tile height and slope to (row, column) in the land palettes, and the role of
   `palBasic` and the data palettes. Owned by `11-world-terrain.md` and `25-rendering.md`.
5. Transparency key for UI images: magenta 0xF81F is inferred from content. Confirm in the
   window toolkit (`08-ui-framework.md`).
