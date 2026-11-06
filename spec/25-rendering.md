# 25 — Rendering (isometric view)

## 1. Overview

The city is drawn as a 2:1 isometric (dimetric) projection of the tile grid, with discrete zoom
levels and four view rotations. Every visible object is a pre-rendered sprite from the sprite DAT
archives in `Res/Sprites/` (`02-containers.md`, `04-graphics-formats.md`). Each sprite has a span
record (L, T, R, B) that gives its registration point; see §4. [DATA]

Related documents: `11-world-terrain.md` (altitudes, water, palette semantics), `12-networks.md`
(network piece selection), `13-buildings-occupants.md` (occupant sprite keys), `26-automata.md`
(moving objects), `08-ui-framework.md` (input, cursors).

## 2. Projection, zoom and rotation

### 2.1 Zoom levels

There are exactly **5 zoom levels**, numbered z = 0 (farthest) to z = 4 (closest). [STATIC] Every
pixel metric doubles per level: [STATIC]/[DATA]

| z | Tile width W (px) | Tile height H (px) | Half-width W/2 | Half-height H/2 | Altitude step A (px per altitude unit) |
|---|---|---|---|---|---|
| 0 | 8 | 4 | 4 | 2 | 1 |
| 1 | 16 | 8 | 8 | 4 | 2 |
| 2 | 32 | 16 | 16 | 8 | 4 |
| 3 | 64 | 32 | 32 | 16 | 8 |
| 4 | 128 | 64 | 64 | 32 | 16 |

So W = 8·2^z, H = 4·2^z, A = 2^z. Screenshots of a 256-tile map at all five zooms show the same
scale: at z = 0 the whole map spans 2048 px across, with its top corner at the horizontal screen
centre, and the tile diamonds double in size with each zoom step `[OBSERVED]`. Some sprite sets (all BAT buildings and the add-on sets) contain
only four native zooms (8–64 px per tile). They cover zoom 4 with 2× magnified zoom-3 images (§4.2). The tile diamond is exactly 2:1. The widths of the shipped 1×1
building sprites at each zoom (8, 16, 32, 64, 128 px) confirm the table. [DATA]

At zoom 4, the scroll origin (the screen position of the view's top-left corner, in projected
pixels) is always rounded down to an even pixel in both axes when zoom changes. [STATIC]

### 2.2 Rotations

There are **4 view rotations**, r = 0..3. Rotation is applied to tile coordinates before projection.
Let the map be `Wm` tiles along x and `Hm` tiles along z (`11-world-terrain.md`; world coordinates (x, z) per `00-README.md`, y is altitude). A world tile (x, z)
and a view tile (u, v) are related as follows: [STATIC]

| r | world x from view (u,v) | world z from view (u,v) |
|---|---|---|
| 0 | u | v |
| 1 | v | Wm − 1 − u |
| 2 | Wm − 1 − u | Hm − 1 − v |
| 3 | Hm − 1 − v | u |

The inverse mapping gives the view tile of a world tile. Successive rotations turn the map by a
quarter turn each. The shipped maps are square, so `Wm` = `Hm`.

For sub-tile positions (automata, effects, free-moving 3-D objects), world coordinates are fixed
point with **256 units per tile** on each ground axis, so a map of `Wm` tiles spans
0..`Wm`·256 − 1. Absolute sub-tile positions rotate like tiles, with `Wm`·256 − 1 − c in place of
`Wm − 1 − c` (r = 1: (x, z) → (Wm·256 − 1 − z, x); r = 2: (Wm·256 − 1 − x, Hm·256 − 1 − z);
r = 3: (z, Wm·256 − 1 − x), where (x, z) is the sub-tile world position and the result is the
view-space pair). [INFERRED, high confidence: must agree with the tile rotation above]

The original also has a fixed-constant variant that uses 0xFFFF − c regardless of map size (exact
only for 256-tile maps). It is used only to lay out the parts of a composite 3-D object relative to
each other: every part is rotated and projected, and the part offsets are then taken relative to the
smallest projected (screen) x and y of the group. Because only differences are used, the constant cancels
and the result is the same for every map size. It is **not** a defect in observable behaviour, and
an implementation may use either constant for that purpose. The group's anchor position itself is
kept in unrotated world coordinates. [STATIC]

### 2.3 Projection formula

For a view tile (u, v) whose relevant altitude is h (terrain altitude units, see
`11-world-terrain.md`), the projected pixel position of the tile's top vertex is: [STATIC]

* screen_x = (v − u) · W/2 − scroll_x
* screen_y = (u + v) · H/2 − h · A − scroll_y

Increasing u moves down-left on screen and increasing v moves down-right, so view tile (0, 0) is the
top corner of the map. For sub-tile fixed-point coordinates (a', b', height e), in units of 1/256
tile: [STATIC]

* screen_x = (W/2) · (b' − a') / 256
* screen_y = (H/2) · (a' + b') / 256 − A · e / 256

Divisions truncate toward zero (integer arithmetic). One altitude unit of terrain therefore equals
256 vertical fixed-point units. [STATIC]

## 3. Screen-to-tile picking

Picking is done per view tile, using the drawn content rather than a flat plane. [STATIC]

1. Convert the mouse position to projected pixels by adding the scroll origin.
2. Consider candidate view tiles near the inverse flat projection
   u ≈ (2·sy/H − 2·sx/W)/2 and v ≈ (2·sy/H + 2·sx/W)/2, scanning a small neighbourhood. Tiles
   further down the screen are tested later, and a later hit overrides an earlier one, so the
   front-most object wins.
3. For each candidate tile, take the object that occupies it (building, network piece or terrain).
   * **Flat ground tiles** (no object, or an object flagged as ground-level): test whether the point
     lies inside the tile diamond with vertices (sx, sy) offsets (0, 0), (W/2, H/2), (0, H) and
     (−W/2, H/2), placed at the tile's projected position, raised by h·A and centred on the tile.
   * **Objects with sprites**: compute the object's on-screen bounding box. Its x origin is the
     footprint anchor's projected x. Its width is footprint_size · W. Its y origin is the anchor's
     projected y minus the sprite's top extent. Its height is the sprite height. If the point is
     inside the box, perform a per-pixel opacity test on the sprite image for the current zoom and
     rotation. The tile is hit only if the pixel is opaque.
4. The result is the world tile, obtained by applying the view→world mapping of §2.2. Callers
   that need sub-tile precision also receive the fixed-point position (tile·256) and the altitude
   (h·256).

When several overlapping objects pass the pixel test, the one whose depth key
(described in §5) is nearest the viewer wins. [STATIC]

## 4. Sprite placement and the sprite-ID scheme

### 4.1 Sprite archives

Images live in the sprite DATs (`02-containers.md`, `04-graphics-formats.md`). Each image is
addressed by (Category, SpriteID). In the archive, Type 0 holds the image, Type 1 holds the 8-byte
span, Group is the category and Instance is the sprite ID. The category is the number in the DAT
file-name prefix (5 = Roads, 6 = Vehicles, 7 = People, 0x15 = Boats, and so on). [DATA]

The span is four s16 values (L, T, R, B). The image is W = L + R pixels wide and H = T + B pixels
high, and its registration (anchor) point lies L pixels from the left edge and T pixels from the
top. To draw a sprite with its anchor at screen point (px, py), blit the image's top-left corner at
(px − L, py − T). L, R or B can be negative, which puts the anchor outside the image. [DATA]

For buildings and network pieces, the anchor is the projected **top vertex** of the object's
footprint at the object's base altitude. Here W = footprint_size · tile width, and L is typically 0 for
1×1 building sprites. That is, the image's left edge is at the footprint's leftmost corner,
and T gives how far the image extends above that corner. [DATA]/[INFERRED]

### 4.2 Sprite-instance records (type 0x00006300)

A sprite-instance record maps an object (occupant, network piece, automaton, flora, effect) to all
of its images for every zoom, rotation and animation frame. Records have Type 0x00006300 and live in
`Res/Sprites/CSATTRIB*.IXF` (Group 0x00006400), in the building-set and flora-set DATs and in BAT
`.bld` files. The record is referenced by key from occupant attributes (`13-buildings-occupants.md`)
and from animated-sprite attributes (`26-automata.md` §3.6). [DATA]

The record starts with the line `BIN` followed by CR (0x0D). A text form also exists: the first line
is something other than `BIN`, and the fields are written as `Name: value` lines (`Version:`,
`SprInstCLSID:`, `ZoomCount:`, `RotCount:`, `ZoomSetCount:`, `AnimSetCount:`, `AnimFrames:`,
`RegRects:`, `LayerFlags:`, `Frames:`, then one `Frame: idx, cat, id, b, x, y` line per frame, with
cat and id in hex). Lines starting with `;` are comments. All shipped records are binary. [STATIC]/[DATA]

Binary layout (offsets after the 4-byte `BIN\r` line):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | s32 | Version | 1 in all shipped records |
| 0x04 | 4 | u32 | SprInstCLSID | Draw-behaviour class of the instance (table below) |
| 0x08 | 1 | u8 | ZoomCount | Number of zoom levels that have images (5, 3 or 2) |
| 0x09 | 1 | u8 | RotCount | Number of distinct facings (4 for buildings, 32 for vehicles, 8 for people) |
| 0x0A | 1 | u8 | ZoomSetCount | Images per zoom per animation frame (orientation slots) |
| 0x0B | 1 | u8 | AnimSetCount | = ZoomCount × ZoomSetCount |
| 0x0C | 1 | u8 | AnimFrames | Number of animation frames |
| 0x0D | 1 | u8 | RegRects | Number of extra registration rectangles (0 or 1 in shipped data) |
| 0x0E | 4 | u32 | LayerFlags | Per-record layer bitmask; meaning partly [OPEN], see §7 (per-zoom substitution) |
| 0x12 | 2 | u16 | FrameCount | = AnimSetCount × AnimFrames |
| 0x14 | 15·n | | Frames | FrameCount entries |

Frame entry (15 bytes):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 2 | u16 | Index | Equals the entry's position (0..n−1) in all shipped data |
| 2 | 4 | u32 | Category | Sprite category (DAT group) |
| 6 | 4 | u32 | SpriteID | Sprite instance in the DAT |
| 10 | 1 | s8 | Flags | 0 = draw the image at native size; 2 = draw the image **magnified 2×** (see below) |
| 11 | 2 | s16 | OffsetX | 0 in all shipped data |
| 13 | 2 | s16 | OffsetY | 0 in all shipped data |

This layout parses all 1,425 shipped records
(51,008 frames) with no bytes left over. Only 300 frames reference a span that is missing from the
installed DATs. [DATA]

**Magnified frames (Flags = 2).** 3,096 shipped frames have Flags = 2. All of them are in zoom
slot 4, and every one names the same (Category, SpriteID) as the zoom-3 frame with the same
animation frame and orientation. These records (BAT buildings, building sets, holiday and add-on
sets) supply only 8/16/32/64-px-per-tile artwork, so the closest zoom reuses the 64-px image. Draw
such a frame by scaling the image by 2 in both axes, with pixel doubling (nearest neighbour), and
use doubled span values (2L, 2T, 2R, 2B) for the anchor. A loader must not assume that zoom-4 images
are natively 128 px per tile. The projection itself always has 5 zoom levels (§2.1). [DATA]/[INFERRED: pixel doubling]

**Frame indexing.** The frame for animation frame a, zoom slot z and orientation slot o is: [DATA]

  index = a · AnimSetCount + z · ZoomSetCount + o.

The shipped sprite IDs follow the convention SpriteID = (SetID << 16) | index, where SetID is the
sprite-instance record's Instance. Implementations must still use the stored (Category, SpriteID)
and must not derive it. [DATA]

**Zoom slot to view zoom.** For ZoomCount = 5, slot z is view zoom z. Vehicles have
ZoomCount = 3; their slot-0 images are about one-quarter of a 32-px tile, which suggests the slots
cover view zooms 2..4. People have ZoomCount = 2, which suggests view zooms 3..4. At zooms without
images the object is not drawn. [INFERRED, medium confidence]

**Orientation slot.** For 4-facing objects (buildings, network pieces), o = (object facing + view
rotation) mod 4 [INFERRED]. For vehicles (ZoomSetCount 48), slots 0–31 are 32 flat headings at
11.25° steps and slots 32–47 are slope/turn variants [INFERRED from sprite extents]. For people
(ZoomSetCount 8), the 8 slots are 8 compass headings. [INFERRED]

Observed SprInstCLSID values (count of records): 0x7300 (855; static building/flora sprite), 0x0672
(291), 0x7312 (121; buildings with an animated overlay frame), 0x7302 (75; vehicles/boats/aircraft),
0x7310 (52; pedestrians), 0x059D–0x05A0 and 0x0674–0x0677 (4 each), and 0x7315 (1). [DATA] The
exact behaviour behind each class is [OPEN]. A compatible engine can derive the behaviour from the
counts above together with LayerFlags.

## 5. Draw order

### 5.1 Layers

Each frame is composed back to front in this order: [STATIC partial, INFERRED order]

1. **Off-map background.** The area outside the map is filled with a repeating tile from
   `Res/Sprites/BACK<z>.BMP`, where `<z>` is the zoom digit 0–4. The shipped images are 128×64 for
   zooms 0–3 and 256×128 for zoom 4. [DATA]/[STATIC]
2. **City surface.** Terrain, water and ground-level pieces are drawn in tile order (§6). The result
   is cached as a surface layer and is redrawn only for dirty rectangles (§10).
3. **Objects.** Buildings, raised network pieces, flora, automata and effects are drawn in
   depth-sorted order (§5.2).
4. **Overlays.** Data-map tinting, selection highlights, placement previews and the cursor (§9).
5. **UI windows** on top (`08-ui-framework.md`).

**Draw height of network pieces** [OBSERVED: Berlin road (132,137)/(132,138) and rail ramp
(147–149,74), view rotation 0]:
* A network piece lying on the terrain (road, rail, power line and the like, on flat or sloped
  ground) is registered at the altitude of one fixed corner **in the piece's own frame**: corner
  (1, 0) of the unrotated piece, turned with the piece rotation r. It does not depend on the view
  rotation. The screen x anchor stays the left vertex (u_max, v_min).
  * World corner by piece rotation: r = 0 → (x+1, z); r = 1 → (x, z) [INFERRED];
    r = 2 → (x, z+1); r = 3 → (x+1, z+1) [INFERRED].
* Evidence:
  * Solving H from sprite T over the sloped terrain-bound pieces of 8 shipped cities gives this
    corner for 877 of 1,108 rot-0 pieces and 809 of 1,017 rot-2 pieces. In the remaining cases the
    sprite top is not the tile's top corner, so the method does not apply. H is the same at all
    four camera rotations.
  * Covered: road 11201/31 (slope along z), road 11202/32 and rail 46/16008 (slope along x).
  * Rotations 1 and 3 are too rare to confirm (38 and 7 pieces).
  * Rendering Berlin (132,137), (142,135), (147,125) and (148,74) with this rule gives continuous
    ramps.
  * The earlier "world corner (x+1, z+1)" rule held only for some piece families.
* The stored piece altitude is not used for terrain-bound pieces. The save stores either the
  lowest or the highest corner (Berlin roads: 91 vs 90).
* Pieces whose stored altitude is above every corner keep it. These are bridge decks, inclines and
  elevated pieces, agent types 16003, 16004, 16011, 17003, 17204 and others. Rail incline 17004
  (piece 59) has its own sprite shape; it was drawn correctly by the front-corner rule at this
  site.
* Highway pieces (16001) store y at ground level. The deck elevation is in the sprite and in the
  path h (+1024, i.e. +4 steps, relative to the highest corner) [DATA].
* Highway bridge pieces (17204) use the **bridge level** as their vehicle base: the water surface at
  the tile, not their stored y. Span paths are +1806, transition inclines run 1550 to 1806, and both
  join the highway (+4 over ground) only on that base [INFERRED, 2 Berlin sites].
* Vehicle paths use a different base: the **highest** corner. VPATHATT heights on slope pieces are 0 at the high edge and −256
  per step at the low edge (piece 11201: 0 / −256, piece 31: 0 / −512). So an automaton's height is
  the max-corner altitude × 256 plus the path h [DATA].

### 5.2 Depth key

Each object that occupies tiles is entered into a per-frame draw list. Its depth key is computed from
the view-space anchor tile (u, v) of its footprint and its base altitude h: [STATIC]

  K = 0x3FFFC000 − 0x4000·(u + v) − 0x100·⌊h / 4⌋   (unsigned 32-bit)

Entries are drawn in **decreasing K**: farther diagonal rows first, and within a row lower altitude
before higher. [INFERRED from the picking rule, which treats a smaller key as nearer]

Each entry also carries a view-space fixed-point box (1/256 tile) used to order moving objects
(automata, effects) against static objects whose footprints they overlap. For an object with
footprint size s whose view-space anchor tile is (p, q), the box spans [(p − s + 1)·256, (p + 1)·256] on
the first axis and [q·256, (q + s)·256] on the second. Objects with the "inset" flag have the box
shrunk by 100 units on every side. [STATIC] A moving object is drawn after every static object whose
box lies entirely behind it on both axes, and before any static object in front. The tie-break
rule is given below.

**Mover versus static ordering** [STATIC, SIMSPR 0x1000C7A6 / 0x1000C802 / 0x1000E52A]:

* Every draw-list entry has a layer byte. Statics are always layer 1.
* Comparator:
  * If the two entries have the same layer, or their boxes do not overlap (touching counts as
    overlap), the higher key draws first.
  * Otherwise the lower layer draws first.
* A post-pass moves each entry of layer ≥ 2 to after the last of its next 6 entries that should
  precede it, at most 7 times per position. So a mover of layer ≥ 2 draws over every static whose
  box it overlaps, whatever the keys.
* Static box: [(x−s+1)·256, (x+1)·256] × [z·256, (z+s)·256] in unrotated world units. It is shrunk
  by 100 per side when tile flag bit 1 is set. Bit 1 is forced for footprints with s > 1; for 1 × 1
  pieces the caller sets it [OPEN which pieces]. The box has no height range.
* Mover key: 0x40000000 − 0x40·(u'+v') − (z >> 2), with the view-rotated sub-tile position and the
  height in 1/256 altitude units.
* Mover box:
  * flags & 3 = 0: centred on the anchor, with half-size from its screen width (at least 100);
  * otherwise snapped to its tile (tile·256 − 2 … tile·256 + 258), shrunk by 50 when flags bit 1
    is set.
* Which layer and flags cars and trains pass is [OPEN]: they are set by another module. Recommended:
  layer 2 with the anchor-centred box, so a car draws over the deck it is entering.

For picking (§3) a finer key is used: 0x40000000 − 0x4000·(u + v) − 0x40·h; the smaller value is
nearer. [STATIC]

## 6. Terrain rendering

* **Terrain is not sprite-based.** `00000009_Landscape.DAT` holds only flora and landscape objects
  (29 sprite sets), and no terrain-slope sprites exist. The terrain module rasterises each tile
  procedurally as a quadrilateral and colours it from the terrain palettes (`04-graphics-formats.md`
  §7). [DATA]/[STATIC]
* **Tile polygon.** The four vertices are the tile's corner altitudes from the vertex grid
  (`11-world-terrain.md`), taken in view-rotated order. At the tile's projected position (§2.3)
  their offsets are left (−W/2, H/2 − h_left·A), top (0, −h_top·A), right (W/2, H/2 − h_right·A) and
  bottom (0, H − h_bottom·A). When the left and right corners have equal height but the top and
  bottom corners do not, the quad is filled as two triangles split along the left–right diagonal.
  Otherwise it is split along the top–bottom diagonal. [STATIC; split rule INFERRED from a
  "fold" flag]
* **Palette slots.** The terrain renderer works from nine palette images (slot table in
  `04-graphics-formats.md` §7.1): 0 `palBasic`, 1–3 `data1Pal`–`data3Pal`, 4 land palette
  (`62B9DA24:455A72B1:n`), 5 `palUnder`, 6 `palWater`, 7 `palEdgeL`, 8 `palEdgeD`. [STATIC]
### 6.1 Palette images as tables

All row numbers in this section count **from the top scan line of the image** (image y = 0 is the
top row as displayed). This is the reverse of BMP storage order, so top-down row r is BMP file row
(height − 1 − r). [STATIC; consistent with the shipped data, see below] Each palette image is
converted once to display pixels and used as a lookup table T[row][column].

* **palBasic expansion.** `palBasic.bmp` is 1 pixel wide. On load, each of its 53 rows (pixels
  read in raster order) is expanded to 32 columns. Column k (0..31) is the row colour multiplied
  per channel by cos((31 − k)·π/62), truncated to an integer. Column 31 is the colour itself and
  column 0 is black. Only column 31 is used by the terrain renderer (§6.4). [STATIC]
* The other slots are used at their native size: land and palUnder are 32 × 512, palWater 1 × 256,
  palEdgeL/palEdgeD 1 × 512, data ramps 64 × 32. [DATA]

### 6.2 Per-vertex terrain colour (land and palUnder)

Terrain colour is computed **per vertex** and then interpolated across the tile (§6.3). For a
vertex v with altitude alt(v), water-surface level water(v) (the per-vertex water grid of
`11-world-terrain.md`; equal to the sea level where there is no lake or river) and shade value
shade(v): [STATIC]

* row = 256 + water(v) − alt(v), in top-down rows. A vertex exactly at the water surface uses row
  256. A vertex d units above the water uses row 256 − d. A vertex d units below it uses row 256 + d.
  The result is always in 0..511 because both values are bytes.
* column = ⌊shade(v) / 2⌋, giving 0..31.
* colour = T[row][column] in the tile's terrain palette: the land palette (slot 4) for normal
  terrain, or `palUnder` (slot 5) in the underground view (§9.3).

Check against the data: in BMP file order this is file row 255 + alt − water, so land above water
uses file rows 256–511 (shore to snow) and submerged ground uses file rows 255 down to 0 (lighter
near the surface, darker with depth). This matches the palette contents described in
`04-graphics-formats.md` §7.2. [DATA]

shade(v) is a per-vertex byte (0..63 in practice) held by the terrain object beside the altitude and
water grids. It encodes slope lighting. The formula that derives it from neighbouring altitudes is
given in §6.6 where known.

Each vertex colour is cached (one 16-bit display colour per vertex of the (W+1)×(H+1) vertex grid).
The cache is invalidated when the vertex changes. The whole cache is cleared whenever the zoom level
changes and either the old or the new zoom is below 3, because of the far-zoom adjustment below. [STATIC]

**Far-zoom colour adjustment (zoom 0, 1, 2).** At zooms below 3, every terrain, water and edge
colour, and every palBasic tint, is adjusted per channel c (0..255) as
c' = c + K[z][channel] − S(c)·(3 − z), where S(c) = ⌊(c + 5) / 10⌋ (c/10 rounded half up) and K is: [STATIC]

| z | K for channel 1 | channel 2 | channel 3 |
|---|---|---|---|
| 0 | 49 | 51 | 53 |
| 1 | 33 | 34 | 35 |
| 2 | 16 | 17 | 18 |

The result always stays within 0..255. The effect is an atmospheric haze: contrast is reduced by
about 10 % per zoom step below 3, and the colour is lifted toward a slightly bluish light grey.
Channels 1–3 are taken to be R, G, B. [INFERRED from the increasing bias, medium confidence]

### 6.3 Tile rasterisation (Gouraud with noise)

Each tile is a convex polygon of four projected vertices (see "Tile polygon" above), filled as one
or two triangles. The polygon filler is a scan-line **Gouraud** shader: [STATIC]

1. Each vertex carries an R, G, B triple (0..255 per channel).
2. Along each polygon edge, x and R, G, B are interpolated linearly per scan line, in floating
   point.
3. Across each span, R, G, B are interpolated linearly from the left edge value to the right edge
   value, with step (right − left)/(span width + 1). Values are truncated to integers per pixel.
4. **Noise texture.** A signed 1024-entry noise table N is added to the colour. For scan line y of
   the tile (relative to the polygon's top), the table index starts at (seed + 143·y) mod 1024 and
   advances by 1 per pixel (mono mode: the same N value is added to R, G and B) or by 3 per pixel
   (RGB mode: three consecutive entries are added to R, G and B). The tile seed is
   (−169·z_tile − 327·x_tile) mod 1024, where (x_tile, z_tile) is the tile's world position.
   The addition is done in 8 bits **without clamping**. [STATIC]
5. **Grid lines.** When the grid display option is on, an edge delta of −16 (−8 at zooms 0–1) is
   added, with clamping to 0..255, to the first |dx/dy| + 1 pixels of each span on the polygon's
   left edges and to the polygon's top horizontal row. This darkens the tile's upper-left and
   upper-right edges and forms the grid. [STATIC]

The two noise tables are generated once at start-up from a pseudo-random generator seeded with the
system time, so their exact contents vary between sessions: [STATIC]

* **Land noise (RGB mode):** each of the 1024 entries is independently −8, 0 or +8 with equal
  probability.
* **Edge/water noise (mono mode):** alternating runs. A run of 1–8 entries (uniform) of either +8
  or −9 (chosen 50/50 per run) is followed by a run of 1–8 zeros, repeated until 1024 entries
  are filled.

Land tiles use land noise in RGB mode. Water tiles use the edge/water noise in mono mode (with the
seed formula's coefficients swapped: (−327·x_tile − 169·z_tile) is the same value). Map-edge earth
faces use the edge/water noise in mono mode. Map-edge water faces use no noise. [STATIC]

### 6.4 Water surface

A water tile is drawn as a second polygon whose vertices are at the water-surface height of each
corner (water(v)·A instead of alt(v)·A), on top of the land polygon. Its per-vertex colour is: [STATIC]

* depth = max(0, water(v) − alt(v));
* colour = palWater[depth] (top-down row; row 0 is the lightest, used at the shoreline where depth
  is 0, and the colour darkens with depth);
* then the far-zoom adjustment (§6.2), cached per vertex like land.

Because colour is interpolated from vertex depths, there are no shoreline sprites. Shoreline tiles
(tiles where some corners are above the water surface and some are not) use a dedicated
**colour-key** variant of the land fill: [STATIC]

1. Vertex colours: a corner whose altitude is above its water level gets the normal land colour
   (§6.2, including tint). A corner at or below the water level gets the key colour
   (160, 160, 160), after the far-zoom adjustment.
2. The replacement colour R is palWater row 0 (the shallowest water colour), after the far-zoom
   adjustment. It is averaged with the tile's palBasic tint, or with palBasic row 51 under the
   condition described under "Tints" below.
3. The tile is Gouraud-filled as usual (land noise, RGB mode, same seed as a land tile). For every
   pixel, compute the squared RGB distance D² between the interpolated colour (before noise) and
   the key:
   * D² ≤ 256 (distance ≤ 16): the pixel is R plus the mono edge/water noise value at the current
     noise index;
   * 256 < D² < 1024: the pixel is R·(1024 − D²)/768 + P·(D² − 256)/768 per channel, plus land
     noise, where P is the interpolated colour;
   * D² ≥ 1024: the pixel is P plus land noise.

The result is a crisp waterline that cuts across the tile where the interpolated colour reaches
the key, with a narrow blended fringe. The grid-line delta applies as for land.

**Tints.** A terrain tile may carry a palBasic tint index (0 = none). When it is non-zero, every
vertex colour of the tile is averaged 50/50 per channel with palBasic[index] (column 31):
c = (c + tint) / 2, truncated. On water tiles with no tint, the vertex colour is also averaged
with palBasic row 51 when the corner's tile has **polluted water**. That is the pollution layer's
"water polluted" test: the water-pollution amount (low 15 bits of the pollution cell) ≥
`WaterPollutedThreshold` (`SC3Pollution.ini [TuningParameters]`; the built-in default is 16000).
Polluted water therefore looks brownish-grey (row 51 = #6E5200 averaged in). It is not the salt
flag. [STATIC]

**Water animation.** The terrain renderer has no time-dependent term: the noise seed depends only
on tile position. Water is therefore static in the terrain layer. Any visible water motion (waves,
glints) comes from sprites or effects drawn above it. [STATIC for the terrain layer; INFERRED overall]

### 6.5 Map-edge faces

Along the map borders that face the viewer, the renderer draws a vertical face below each border
tile edge, so that the map looks like a slab. Each face segment spans one tile edge, between two
border vertices c1 and c2. Which two vertices are used, and the horizontal placement (half a tile
wide, at the left or right half of the tile's diamond), follow from the view rotation and the
border. [STATIC] Vertical extents at each of the two vertices, in projected pixels (A = altitude
step):

| Level | Height |
|---|---|
| surface | water(v) if water(v) > alt(v), else alt(v) |
| ground | alt(v) |
| base | −4 altitude units (normal); 0 if the segment has the "no water/flush" flag |

The face is drawn in two parts, each with the index-Gouraud filler (palette **index** is
interpolated per pixel, then looked up in a 512-entry table):

1. **Water part** (only when the surface is above the ground at either vertex, and the
   segment does not have the flush flag): a quad from the surface down to the ground. Index 0 at the
   surface vertices, and depth = water(v) − alt(v) at the ground vertices. The table rows 0–255 are
   blue water tones. No noise.
2. **Earth part:** a quad from the ground down to the base. Index 256 at the ground vertices
   (260 with the flush flag), and 260 + alt(v) at the base vertices, clamped to 511. Equivalently,
   index = 256 + depth below the ground surface. Rows 256–511 are earth tones that darken
   downward. Mono edge noise is applied.

**Light and dark faces.** Both parts use palEdgeL or palEdgeD. The two front borders alternate:
for one border the table is palEdgeL when the rotation r is even and palEdgeD when it is odd. For
the other border it is the reverse. Light therefore comes from a fixed world direction, and the
faces swap shade as the view rotates. [STATIC]

**Observed rule (authoritative)** [OBSERVED]: measured on the original at all four rotations (zoom 0,
Berlin, mean earth-face colour just above the slab's bottom edge):

| Rotation | Lower-left face | Lower-right face | Light face (palEdgeL) |
|---|---|---|---|
| 0 | (113,103,95) | (95,90,85) | lower-left |
| 1 | (95,91,86) | (112,102,94) | lower-right |
| 2 | (112,102,95) | (96,91,85) | lower-left |
| 3 | (92,86,84) | (112,102,94) | lower-right |

So the **lower-left** face uses palEdgeL at even rotations and palEdgeD at odd rotations, and the
lower-right face the opposite: the shading alternates with rotation parity on screen.
Correction: an earlier revision gave a per-world-border formula ((r & 1) for constant-x faces,
(¬r & 1) for constant-z faces). With the view transform that formula gives a view-fixed result,
which contradicts this measurement; use the table.

**Flush ("no water") segments** [STATIC]:
* Each edge segment carries a flag, bit 0x40 of its edge-tile byte. The flag is supplied by the
  caller that creates the segment; which segments get it is [OPEN].
* When the flag is set:
  * the surface height ignores water (surface = alt);
  * the water part is skipped;
  * the base is at altitude 0 instead of −4;
  * the earth part's top index is 260 instead of 256.
* A flush segment is therefore a plain earth slab without the extra 4-unit skirt or a water
  face.

### 6.6 Vertex shade (slope lighting)

The shade byte of every vertex is recomputed by the terrain object whenever altitudes change. Only
the vertices in the changed rectangle are recomputed. It is also recomputed for the whole map on
load and after generation. Shade does **not** depend on the view rotation. [STATIC]

For the vertex at grid position (x, z), with altitude h(x, z) (x is the first index of the altitude
grid, see `11-world-terrain.md`):

1. Take the four neighbours h(x−1, z), h(x+1, z), h(x, z−1) and h(x, z+1). At the map border, a
   missing neighbour is replaced by the mirror extrapolation clamp(2·h(x, z) − h(opposite
   neighbour), 0, 255).
2. Form the normal n = (s·(h(x−1, z) − h(x+1, z)) / 2, d, s·(h(x, z−1) − h(x, z+1)) / 2) and
   normalise it. Here d = 16.0 (the horizontal spacing) and s = the terrain record's
   `heightScale` (world units per altitude unit, 3.266 in shipped files; `11-world-terrain.md`).
   The second component points up.
3. There are two fixed lights: L1 = (−1, 1, 0)/√2 and L2 = (1, 1, 0)/√2. Both are 45° above
   the horizon, one on each side along the x axis.
4. For each light i: t_i = ⌊ acos(max(0, n·L_i)) · 62 / π ⌋, so 0..31 (31 when the light
   grazes or is behind the surface).
5. shade(x, z) = max(t_1, t_2). The palette column is ⌊shade / 2⌋ (§6.2), so only columns 0–15
   are reachable.

Consequences: flat ground has n·L = cos 45°, giving t = 15 and column 7. Slopes in any
direction raise t and therefore use brighter columns (8–15). Slopes along the b axis lower both
dot products. Slopes along the a axis lower one of them, and the maximum takes that one. [STATIC]
Use single-precision floats and truncation as stated. Column differences of one step are visible.
* Multi-tile buildings are **not sliced**. Every building and network sprite covers its whole
  footprint: shipped widths are exact multiples of the tile width (footprint × 8·2^z). It is drawn
  once, at its anchor tile's depth key, with the box from §5.2. [DATA]/[STATIC]

## 7. Zoom-dependent detail and building animation

* **Per-zoom substitution.** The view keeps a per-tile cell grid. Each cell has a run-time 16-bit
  flag word; it is **neither** the sprite-instance `LayerFlags` field **nor** an occupant flag.
  [STATIC]
  * The bit with mask {0x10, 0x20, 0x40, 0x80, 0x100} for z = 0..4 means "the cell's object image
    is built and valid at zoom z".
  * The bit is set when the object rebuilds its image for that zoom and rotation. Rebuilding is
    done lazily for cells that are present (bit 0x4000) and not yet valid. The bit is cleared
    through the view's deferred update queue when the object changes, and set again after the
    rebuild.
  * While the bit is clear, the generic substitute sprite chosen by footprint size is drawn.
    Visually, a building briefly shows the footprint placeholder until its image is ready.
  * Other bits: 0x4000 = cell present/rebuildable. 0x1 is tested together with 0x4000 [OPEN
    meaning].
  * A clone with all images ready simply draws the object's own image at every zoom.
  * The `LayerFlags` record field (§4.2) is unrelated to this mask. [STATIC]
* Automata appear only at zooms for which their sprite-instance record has images (§4.2): vehicles
  at the 3 closest zooms and people at the 2 closest zooms. [INFERRED]
* **Animated buildings.** These have sprite-instance records with AnimFrames > 1 (SprInstCLSID
  0x7312). Animation frame a is drawn by adding a·AnimSetCount to the frame index. The extra frame
  is usually a small overlay image (lights, smoke, flags) whose anchor is offset through negative L
  values in its span. [DATA] No separate timer for these frames was found in the view module. The
  only time-driven building animation is the sprite-script runner (§7.1). The other frame choices
  follow simulation state. [INFERRED]
* Building overlay animation is script-driven (§7.1). Smoke plumes and other attached effects are
  child sprites (§7.2). The number of buildings animating at once is capped (§7.3).

### 7.1 Sprite scripts (text sprite-instance records, type 0x00006301)

Text sprite-instance records (key type 0x00006301, CLSID 0x7315) can end with a script block:
an optional `ScriptCountries:<n>` / `ScriptCountry:<name>` header (one block per country; see §8),
then `Scripts: <n>` followed by n script lines, then an optional `DSprites: <n>` block (§7.2).
A script line may continue onto the next line when it ends in `\`. [STATIC]

**Script header.** `Script[zoom,rot,offX,offY]:` followed by comma-separated ops, terminated by
`END` (any token starting with `E`). The script belongs to one (zoom 0–4, rotation 0–3) slot. A
slot may hold **several scripts**. All of them run at the same time, each with its own step
index and timer, and each draws its own overlay. A renderer must run every script of the slot,
not only the first. offX/offY are added to the pixel offset of every D/DA/S/P draw in the script.
[STATIC]

**Compilation.** Each text op compiles to one or more entries in a flat op list. `G n` targets
and `R n` arithmetic count **compiled entries**, not text tokens: [STATIC]

| Text | Compiled to | Runtime meaning |
|---|---|---|
| `S start count ms` | for f = start … start+count−1: `Draw(f)`, `Wait(ms)` (2·count entries) | play frames in sequence |
| `P start count ms` | as S forward (start … start+count−1), then backward start+count−2 … start+1, each `Draw`+`Wait(ms)` | ping-pong loop (not sound) |
| `D f dx dy` | `Draw(f)` at (offX+dx, offY+dy) | show frame f opaque |
| `DA f dx dy` | `DrawAlpha(f)` at (offX+dx, offY+dy) | show frame f, using frame **f+1** as its alpha plane |
| `W ms` | `Wait(ms)` | see timing below |
| `WR base range` | `WaitRandom` | next = now + base + rand() mod range, a uniform value in [base, base+range) ms |
| `G n` | `Goto` | step := n (absolute compiled index) |
| `R n` | `RandomSkip` | step := step + 1 + (rand() mod n), so it lands on one of the next n entries (normally n `G` entries) |
| `L n` | `Label(n)` | no-op marker. It is skipped at no cost in the same update |
| `WL mmddMMDD n` | `DateWindow(label n)` | see below |
| `WD mmddMMDD n` | `DateWindow(index n)` | as WL but n is an absolute compiled index |
| `GL n` | `GotoWindowTarget` | step := position of label **t**, where t is the target of the most recently armed WL. The number written after GL is **ignored** |
| `CB a b`, `CE a b`, `CD` | (nothing) | parsed. No runtime effect was found |

Frame numbers f are indices into the record's frame list. [STATIC] D with fewer than three numbers
(for example `D 0`) keeps the dx/dy of the previous D/DA in the same script. [STATIC]

**Runner.** Each running animated sprite owns a millisecond clock, `now`, that starts at 0 when its
animation starts. For each script of the current (zoom, rotation) slot it also keeps a step index
(start 0) and a next-time value (start 0, so the first entry runs at once). On every view update
with elapsed time dt: [STATIC]

1. now += dt_µs >> 10. Elapsed time is in microseconds, and the shift divides by 1024, not 1000.
   The animation clock therefore runs about 2.3% slow against real time.
2. For each script with next-time ≤ now: if step ≥ list length, step := 0, so a script with no
   final `G` loops from the start. Skip any Label entries. Execute **exactly one** other entry:
   * Draw / DrawAlpha: set this script's current overlay frame and offset, invalidate the old and
     new rectangles, then step+1.
   * Wait(ms): next := now + ms + j·((ms>>6) + (ms>>5)), then step+1. j = 0..3 is fixed for each
     sprite object, which gives a deterministic 0–14% per-building slowdown that desynchronises
     neighbours (for W 200 the period is 200/209/218/227 ms). A clone should pick j uniformly per
     building instance.
   * WaitRandom: as defined in the table, then step+1. No j term applies.
   * Goto / RandomSkip / GotoWindowTarget: change step only. The new entry runs on the next update.
   * DateWindow: arm the date window (start mm/dd, end MM/DD, target n), then step+1.
3. After the entry, if a date window is armed and today's **game date** (month, day from the city
   clock) is outside [start, end), with the start inclusive and the end day **exclusive**: jump to
   the target (label n for WL, index n for WD) and disarm. While the date is inside, the window
   stays armed and is re-checked after every later entry.

Because a Draw and the following Wait are separate entries, each takes its own update. The frame
period of `S … ms` is therefore ms (plus the j term) plus about one update interval. **It is not
a global 15 Hz tick.** For example, `S 35 5 200` holds each frame about 200–230 ms. [STATIC]

Holiday chains: `L 1, WL 01010331 2, …frames…, GL 1, L 2, WL 04010430 3, …` play only the block
whose window contains today's game date. Every other block's WL jumps straight to the next label,
and a block's GL leaves it for its WL target. [STATIC]

**State lifetime.** Script state (clock, steps, timers) is created when a building's animation is
started by the cap logic (§7.3) and destroyed when it is stopped. A restarted animation begins
again at step 0. The state is also re-bound when the view zoom or rotation changes, because each
(zoom, rotation) slot has its own scripts. [STATIC]

**Pause.** The elapsed time comes from the view's real-time update, not from the simulation clock,
so game speed has no effect. [STATIC] View updates **continue while the game is paused**: with the
simulation paused, building animations (smoke plumes) keep running, automata keep moving, and the
view scrolls normally. [OBSERVED]

### 7.2 Attached child sprites (smokestacks and other effects)

Smoke plumes are **not** particles and do not depend on wind or pollution. They are ordinary
scripted child sprites listed in the parent building's text record: [STATIC]/[DATA]

```
DSprites: <n>
Sprite[zoom,rot,childInst(hex),dx,dy,dz]
```

Each line attaches a child sprite instance (`6301:6400:childInst`) to the parent's (zoom,
rotation) slot. When the parent starts animating, each child of the current slot is created at the
parent's world position + (dx, dy, dz). [STATIC] The child is projected and depth-sorted as a
separate object.

Coordinate rules [OBSERVED: checked against the parent sprites of records 0x84 (1 × 1), 0xA0 (2 × 2)
and 0x13B0 (3 × 3) in all four orientations; every predicted point lands on a chimney top]:

* The parent origin is the footprint's **minimum world corner**: world (x·256, z·256) for an
  occupant whose footprint spans x … x + sizeX − 1 and z … z + sizeZ − 1.
* dx is added along world x and dy along world z, in 1/256-tile units.
* dz is a height in the units where one altitude step is 256. On screen it rises dz × A / 256 px,
  where A is the zoom's pixels per altitude step, the same scale as the parent's altitude.
* Shipped data repeats the **same** (dx, dy, dz) in all four rotation slots. The offsets are
  therefore a fixed world point. Camera rotation needs no extra handling: project the world point
  with the current view rotation, exactly like any other world position.
* Some slots list fewer children than others (0x13A0 has two extra stacks in slots 0, 2 and 3 but
  not in slot 1). Always use the children of the slot actually drawn.
* For an occupant placed with its own rotation r ≠ 0 the offsets must turn with the building
  [INFERRED: all shipped child-bearing occupants have rotation 0]. The sprite drawn is orientation
  (r + camera rotation) & 3, so place the child at the screen offset from the parent's anchor that
  the rule above gives for an unrotated footprint (sizes from the occupant attribute) seen at view
  rotation (r + camera rotation) & 3.
It is ticked from the parent's update with the same dt and runs its own scripts (§7.1). When the
parent stops, all children are removed. [STATIC]

Shipped data (`CSATTRIB.IXF`):

| Parent record | Child | Children per slot (typical) |
|---|---|---|
| 0x84, 0x85, 0xA1, 0x138C | 0xFC (smoke) | 1 |
| 0xA0 | 0xFC | 2 |
| 0x13A0 | 0xFC | 2–4 |
| 0x13B0 | 0xFC | up to 8 |
| 0xC9, 0xCA | 0x484F | 1 |
| 0xCB | 0x484E | 2 |
| 0xCF | 0x4850 | 4 |
| 0x1F49 | 0x4854 | 1 |

Offsets per slot differ by rotation (example: 0x84, zoom 3 rotation 1: dx 208, dy 48, dz 1264).
Parents are mapped to occupants through the occupant appearance key. [DATA]

**Smoke record 0xFC** has 230 frames, all from sprite category 0x10 (`Smoke.DAT`), and one script
per zoom. Each script draws even frame indices with `DA`, so even frame f is the colour image and
odd frame f+1 is its alpha plane. Per zoom, the frames used are: [DATA]

| Zoom | Segment A (8 frames) | Segment B (8 frames) | Tail (7 frames) |
|---|---|---|---|
| 4 | 0–14 | 80–94 | 160–172 |
| 3 | 16–30 | 96–110 | 174–186 |
| 2 | 32–46 | 112–126 | 188–200 |
| 1 | 48–62 | 128–142 | 202–214 |
| 0 | 64–78 | 144–158 | 216–228 |

(Only even values are listed; each is paired with the next odd frame.) The script has the form
`R 4, G 5, G 22, G 40, G 56, WR 200 200, <A: DA/W 200 ×8>, <B: DA/W 200 ×8>, R 3, G 21, G 21,
<tail: DA/W 200 ×7>, D 0, WR 800 900, R 2, G 56, G 5`. It starts at a random point (a full puff, the
middle of the puff, a short wait or the idle gap) and plays 200 ms frames. After the tail it shows
frame 0 and idles 0.8–1.7 s, then randomly either idles again or starts a new puff. There is no
lifetime, spawn rate, drift or scale beyond what is drawn into the frames. Per-zoom scale is a
separate frame range for each zoom. [STATIC]/[DATA]

### 7.3 Animation cap (`MaxAnimationsBase`)

* Cap = `MaxAnimationsBase` (`Sys/SC3Tune.ini [BuildingAnimationTunables]`, default and shipped
  value 10) + 3·m. m is the machine-profile byte that also sets the default detail levels (the
  machine class 1–7 of `27-platform.md` §3), so the cap is 13–31. It is read once. [STATIC];
  m = machine class [INFERRED].
* The cap counts animating **buildings** (parents). Their child sprites and automata are not
  counted. [STATIC]
* Selection runs on every view change (scroll, zoom, rotate) and on the animation-option message.
  First, gather up to 200 animatable objects in the visible rectangle. Then keep every currently
  animating building that is still among them. Fill the remaining (cap − kept) slots with
  **uniformly random** picks from the other candidates; if the candidates fit, take them all.
  Finally, stop animations that were not kept and start the new ones. Distance to the view centre
  is not used. [STATIC]
* All building animation is off unless the animation display option is enabled and a zoom-like
  view value is ≥ 2 (the closer zoom levels). [STATIC for the gate; INFERRED that the value is the
  zoom index]
* Day/night cycle: none was found (there are no night sprites or strings). [INFERRED]
* **Construction appearance.** There are no multi-stage construction sprites. While a lot is in
  state 4 (under construction), the building layer draws a substitute **construction-site
  occupant** in place of the building, chosen from `SC3BuildingLayer.ini [DefaultBuildingData]` with
  key `4:size:agentType[:F|:G]` (`13-buildings-occupants.md` §6). The anchor tile uses the plain
  key, the filler tiles of a multi-tile lot use `:F`, and `:G` is used for lots with accumulating
  garbage. Shipped examples: occupants 136 (1×1), 166 (2×2), 195 (3×3) and 9000 (4×4) in group
  1406 (0x57E), with agent-type-specific alternatives for 3000/5000/8000. The site occupant is an
  ordinary sprite object; any animation comes from its own sprite-instance record. [DATA]
* **Construction timing.** The appearance lasts until the growth simulation completes construction.
  This is a per-visit random test with probability `ConstructionTimeFactor / (tiles × 100)`, and
  it requires power and road access (`15-sim-growth.md` §4.3). There is no fixed duration and no
  stage counter. On completion the lot switches to state 1 and the real building's sprite is drawn.
  [STATIC via `15-sim-growth.md`] The "Construction" agent (15300) and the "Construction Vehicle"
  (202) of `CitySim.ini` are the agent types of these site occupants and their associated
  automata. [INFERRED]

## 8. Seasonal / holiday sprite swaps

* Holiday content is registered as extra data files in the `Default.ini` resource-segment list
  (`AnSprAtt_Holidays.IXF`, `VAttrib_Holidays.IXF`, `PAttrib_Holidays.IXF`, `PplProd_Holidays.IXF`,
  `CSAttrib_Holidays.IXF`, `OccupantAttribs_Holidays.IXF`, and the sprite DATs `54AACF44_Holiday.DAT`
  and `54DA2E59_HolidayF.DAT`). Holiday records add new IDs; they do not overwrite base records.
  [DATA]
* Seasonal pedestrians come from date-ranged PPLPROD modes (`26-automata.md` §3.5). Country blocks
  suffixed `:holidays` are used only when holiday content is enabled. The sprite-script layer in the
  view also understands `ScriptCountries:` lists and the `:holidays` suffix. [STATIC]

## 9. Overlays

Cursor files are listed in `08-ui-framework.md`.

### 9.1 Terrain tile kinds

The terrain layer draws a list of per-tile primitives. Each primitive is created for one tile
(x, z) and is one of the following kinds: [STATIC]

| Kind | Geometry | Vertex colour | Fill |
|---|---|---|---|
| Land | ground corners | land/palUnder palette, §6.2 (+ optional palBasic tint) | Gouraud + land noise (RGB) |
| Water | water-surface corners | palWater by depth, §6.4 | Gouraud + edge/water noise (mono) |
| Shoreline | ground corners | land colour or key grey, §6.4 | colour-key Gouraud |
| Solid colour | ground corners | palette slot S, row R, column ⌊shade/2⌋ | Gouraud, **no noise** |
| Edge face | border segment, §6.5 | palEdgeL/palEdgeD by index | index-Gouraud |

All kinds apply the far-zoom adjustment and the grid-line delta.

### 9.2 Data-map overlays and solid-colour tiles

A "solid colour" tile is created with (x, z, palette slot S, row R). Its vertex colour is
T_S[R][⌊shade(v)/2⌋]: a fixed row of the chosen palette, with the column still driven by the
vertex's slope shade, so relief stays visible. It **replaces** the terrain colour of that tile
rather than blending with it, and no noise is added. [STATIC]

* S = 1, 2 or 3 selects `data1Pal`/`data2Pal`/`data3Pal` (64 × 32). In these the row R (0..31,
  top-down) is the data class of the tile and the column is the relief shade. Only columns
  0–15 are reachable through the shade (§6.6). [STATIC]
* S = 0 selects the cos-expanded palBasic (§6.1), giving 53 fixed colours with relief shading.
* The cell resolution is **one map tile**: each overlay value colours exactly one tile polygon,
  and colours are interpolated only within the tile, between its corners' shade values.
  [STATIC]

**Layer → ramp** `[OBSERVED]` (shipped city at 2067, all eight layers of the View City Layers
dialog, `09-ui-screens.md` §7.2).

`data1Pal` runs from grey in its bottom row to deep blue in its top row, in about 11 visible bands.
`data2Pal` runs from grey through orange to dark red. `data3Pal` is a smooth blue-to-orange
gradient that none of the eight layers was seen to use.

| Layer | Ramp | Low value | High value | Tiles coloured |
|---|---|---|---|---|
| Land Value | data1Pal | grey | dark blue | zoned and developed land, in 2 × 2-tile blocks (the land-value grid) |
| Density | data1Pal | grey | blue | zoned land |
| Electric Power | data1Pal, top band | — | deep blue = powered | powered tiles only; unpowered land keeps its terrain colour |
| Aura | data1Pal for positive, data2Pal for negative | light blue | deep blue (good) / dark red (bad) | developed land |
| Crime | data2Pal | grey | orange | zoned land |
| Pollution | data2Pal | grey | dark red-orange | every land tile; a smooth field across zoned and unzoned land |
| Flammability | data2Pal | light orange | red-orange | land with buildings or flora |
| Traffic | data2Pal along networks (grey → orange with load) | — | — | road and rail tiles; zoned tiles are tinted by zone type (greens, a tan/yellow and a blue-violet) |

In every layer, buildings, flora and automata are hidden while the layer is shown. Networks stay
visible, and land outside the layer's tile set keeps its normal terrain colour. The low end of
both ramps is the grey bottom row (≈ RGB 189,194,197 lit / 181,186,189 shaded). Colours are shaded
by relief like terrain, so on-screen values differ slightly from the palette entries.

Quantisation [INFERRED]: row = 31 − ⌊value × 32 / 256⌋, i.e. low values on the bottom grey row and
high values on the top row of the ramp, consistent with all eight screenshots. The exact band
edges are [OPEN].

**Pollution value for the overlay** [OPEN]:
* The pollution layer stores 15-bit air and water amounts per 2 × 2-tile cell
  (`17-sim-services.md` §7.2.1). None of its interfaces scales them to a byte for the view. The
  only UI scaling in the pollution code divides by `MaxAirPolluteForUI` / `MaxWaterPolluteForUI`
  (shipped 11000 each). It is used for the history chart (value × 100 ÷ max) and for the query
  bands.
* Recommended: overlay byte = min(255, ⌊v × 256 / MaxPolluteForUI⌋), with v the cell's air (or
  water) amount. It is consistent with the chart's scale, and a cell at the UI maximum lands on
  the top row.
* The clean side's guess v / 128 saturates at 32 640, which is about 3 × the UI maximum, so most
  real cities would sit in the bottom third of the ramp.

**Navigator minimap colours** [STATIC + OBSERVED]:
* The navigator's minimap has no data layer by default. It draws land and water from the same
  ramp image as the Neighbors window: `62B9DA24:000005E5:00000021` (32 × 32), pixel (column k,
  row 3).
* Measured on screen, the minimap colours are exactly these pixels:

  | Column k | Colour | Column k | Colour |
  |---|---|---|---|
  | 0 (water) | #6A699C | 6 | #83715A |
  | 1 | #7B916A | 7 | #7B655A |
  | 2 | #8B9D6A | 8 | #73655A |
  | 3 | #9CA173 | 9 | #6A6162 |
  | 4 | #9C9573 | 10 | #736D73 |
  | 5 | #948562 | 11 | #7B797B |

* Use k from the tile's height above water by the rule of `11-world-terrain.md` §9: v < 1 → 0;
  v < 10 → v/2 + 1; else v × 10/256 + 5, capped at 11. [INFERRED: same rule as the Neighbors
  image]
* There is no single base land colour: land shades run green → khaki → brown → grey with height.
  The clean side's guess of RGB 90,130,70 is close to columns 1–2 only.
* Map-edge pixels outside the diamond are the navigator panel's own background.
* A data layer selected through the navigator's layers button draws its colours over this base.

**Zones layer** [OBSERVED; palBasic row assignment partly INFERRED]:
* With the **Zones** box ticked in View City Layers and Zoned Buildings / Other Buildings
  unticked, every lot is drawn as a flat palBasic colour with relief shading. This is overlay
  S = 0: the colour replaces the terrain colour and no noise is added.
* Roads, rails, highways, power lines, vehicles and flora are drawn normally on top. The zone
  colour is not blended with them.
* Empty zoned tiles and tiles holding a zoned building get the same colour. Hidden buildings stay
  pickable by the query tool.
* Lots of **non-zoned** buildings (civic buildings, landmarks such as The Wall, hospitals) get a
  rust colour: palBasic row 34.
* Measured flat-tile colours (16-bit screen):

  | Zone | Flat-tile colour | palBasic row |
  |---|---|---|
  | Dense industrial | #C67D00 | 21 (#D89300) |
  | Medium industrial | #AD8A18 | 22 (#C59C27) |
  | Light industrial | #A59242 | 23 (#BBA44A) |
  | Dense residential | #215D08 | 24 (#2F6F10) |
  | Medium residential | #427129 | 25 (#4C8236) |
  | Unzoned building lot | #AD5D21 | 34 (#C76A2B) |

  Each measured colour is about 0.88 × its row, which is the flat-tile relief shade.
* By the row pattern [INFERRED]:
  * light residential = row 26 (#5B9648);
  * dense, medium and light commercial = rows 27–29 (blues #4346B1, #565FA6, #6A72AB);
  * seaport = row 30 (#71BFCA);
  * airport = row 31 (#67B88D);
  * landfill = rows 32/33 (browns).
* Zone colours appear elsewhere only as the 50/50 palBasic tint of §6.4 on zoned terrain, and in
  the zoning drag preview.

### 9.3 Underground view

In the underground view the land tiles are drawn with palette slot 5 (`palUnder`) in place of the
land palette, with the same row and column rules (§6.2). [STATIC] Tiles that are neither flat nor
a gentle single-axis ramp are tinted 50/50 with palBasic row 52 (dark grey). A gentle single-axis
ramp is a tile with two opposite pairs of equal corners whose difference is at most 2 altitude
units. This marks ground that is too steep to build on. [STATIC; meaning INFERRED] Underground
network pieces (pipes, subway tunnels and stations) are drawn as sprites from their own
categories (`12-networks.md`). Above-ground objects are hidden. [INFERRED]

### 9.4 Selection highlight, placement preview, cost text

Observed behaviour `[OBSERVED]`:
* **Building placement preview:** the building's footprint tiles are filled with a translucent
  **cyan** tint at the hovered position. The footprint is shown as tiles, not as the building
  sprite.
* **Network drag preview:** the planned path is drawn as a one-tile-wide translucent cyan line along
  the route of `12-networks.md` §6.2, stair-step included.
* **Zone drag preview:** the rectangle is tinted translucently in the zone's colour (light green for
  residential).
* **Query hover:** with the Query tool armed, the object under the cursor is tinted. [OBSERVED,
  measured from 16-bit screen captures of the original]
  * **Building:** the whole building sprite, including its lot, is tinted per pixel. Only opaque
    sprite pixels change; there is no bounding box.
  * **Anything else** (bare ground, water, road/rail pieces, trees): one tile is tinted. The flat
    tile diamond and any flora sprite on that tile are tinted together.
  * **Blend:** out ≈ 0.5·in + 0.5·C per channel. The fitted keep factor is 0.46–0.49 per channel,
    with a fit error of about 2/255. C ≈ RGB (0, 130, 176), a light blue/cyan. Example: white
    (247,247,239) → (115,186,206); dark grey (41,36,41) → (16,81,115).
* **Occupied target (placement tool)** uses the same mechanism and blend factor, with
  C ≈ RGB (212, 30, 33) (red). It covers every building in the planned footprint plus the
  footprint ground tiles. [OBSERVED]
* Dispatch tools could not be tested: they are disabled without an active emergency. It is also
  unconfirmed whether child sprites (§7.2) are tinted with their parent. [OPEN]
* **Occupied target:** hovering a placement tool over an existing building tints that building's
  sprite red.
* **Cost label:** a small box with a white background and a 1-pixel black border, showing `§` and the
  total cost with thousands separators. It sits below and to the right of the cursor (about
  45 px down at 1024 × 768) and updates live during a drag. The amount includes clearing costs,
  e.g. §5,005 for a §5,000 plant on a tile with trees.
* The cost is charged on release, immediately, even while the simulation is paused.

[OPEN] These are drawn by the view module, not the terrain module. Known facts: the terraform
tools compute a planned cost for the tile under the cursor every time the cursor moves
(`11-world-terrain.md`), and flag the preview as unaffordable when funds are short (see
`09-ui-screens.md`). The tint mechanism of §6.4 (palBasic row averaged into the vertex colours)
is the terrain-side highlight primitive.

## 10. Scrolling, redraw and persisted view state

* **Edge-scroll margin.** `ScrollMarginFactor` in section `Navigation` of the user preference file
  sets the margin (stored as 5.0 divided by a display-dependent value). `ScrollRateInPixelsPerMinute`
  in `SC3Tune.ini` applies to the credits screen, not the map.
* **Keyboard scroll.** On every view update, while the city view has keyboard focus, the four
  arrow keys are polled. With step s, the view scrolls by up (0, −s), down (0, +s), left (−s, 0) or
  right (+s, 0) screen pixels. When two perpendicular keys are held, it scrolls diagonally by
  (±s, ±s). Any other combination (three or four keys, or two opposite keys) does not scroll.
  s is held per zoom level (a five-entry table) and is reselected on every zoom change. All five
  entries are hard-coded to **32.0 pixels per update**, and no preference overrides them, so the
  step is the same number of screen pixels at every zoom. [STATIC]
* **Drag and edge scroll** use the velocity rules in `08-ui-framework.md` §8.3, applied through the
  same per-update scroll call. [STATIC]
* **Update rate.** The original's view update is **not throttled to a fixed rate** on current
  hardware. [OBSERVED] Measured under Wine on a modern CPU at 800 × 600 with the simulation paused:
  * a held arrow key, or a right-button drag 100 px from its anchor, crossed an entire 256-tile map
    at zoom 4 in under 0.5 s, which is roughly 20 000+ px/s;
  * a single key tap (press and immediate release) moved the view by about 300 px, that is, about
    10 updates of 32 px;
  * screen redraws, captured at 60 fps, each showed jumps of 150–300 px.

  The loop therefore runs many updates per displayed frame, and the scroll speed depends on the
  machine. There is no original px/s figure to match. On zoom change the view passes 60.0 to its
  update scheduler [STATIC], which reads as the designers' intended rate. **Recommended:** run view
  updates at a fixed 60 Hz. That gives keyboard scroll = 32 px × 60 = 1920 px/s, and drag scroll =
  the `08-ui-framework.md` §8.3 per-update step × 60.
* **Redraw.** The city surface is cached and updated through dirty rectangles (per tile changes and
  automata movement). Automata and animations are redrawn as the view updates; use the same 60 Hz
  recommendation. Building animation timing does not depend on the update rate, because scripts use
  their own millisecond clock (§7.1).

### Persisted state: view record

Every saved city contains a top-level IXF record with key **620EDFCE:A20FB3B9:00000000**, 24 bytes,
present in 15/15 shipped `.sc3` files: [DATA]/[STATIC]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | Version | 1 |
| 0x02 | 2 | u16 | Flags | 2 (bit 0 set would add begin/end marker strings; see `10-save-format.md`) |
| 0x04 | 4 | u32 | Sentinel | 0xDEADBEEF |
| 0x08 | 4 | f32 | CenterX | view centre, tile units (integer-valued in shipped saves) |
| 0x0C | 4 | f32 | CenterY | view centre, tile units |
| 0x10 | 4 | s32 | Zoom | 0–4 (shipped saves: 1–3) |
| 0x14 | 4 | s32 | Rotation | 0–3 |

On load the view is restored to that zoom, rotation and centre. [STATIC] Whether CenterX/Y are
world or view coordinates is [INFERRED: world tile coordinates].

## Open questions

* Terrain: which edge segments are created with the flush flag (§6.5). The quad-split (shape) table for land tiles
  (the edge-face classifier is known).
* Data-map overlays: the mapping from each data layer, and its 0..255 value, to a ramp (slot 1–3)
  and row 0..31. This is decided in the view module, not traced.
* The underground view's sprite set and the hiding of above-ground objects. The selection highlight, the
  placement preview and the cost text at the cursor (view module, not traced).
* None for the view update rate: it is unthrottled in the original; 60 Hz is recommended (§10).
* The meaning of each SprInstCLSID and of LayerFlags; whether the 2× magnification uses any filtering.
* Edge-scroll rectangle sizes.
* Which layer and flags cars and trains pass to the sprite manager (§5.2).
