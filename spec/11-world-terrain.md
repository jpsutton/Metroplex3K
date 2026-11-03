# 11 — World grid, terrain, water and flora

Status: draft. Anything not verified is tagged `[OPEN]`.

Related documents: `10-save-format.md` (container and record framing), `25-rendering.md`
(projection and drawing), `09-ui-screens.md` (terrain-editor screen), `15-sim-growth.md`
(land value), `13-buildings-occupants.md` (flora occupants), `19-sim-economy.md` (costs).

## 1. Map sizes and grids

| Map size | Tiles per side | Vertex (corner) grid | Evidence |
|---|---|---|---|
| Small | 128 | 129 × 129 | [DATA] `Mount Herrang.sc3`, `Tutorial on Sea and Air.SNR` |
| Medium | 192 | 193 × 193 | [DATA] `Bonn, Germany.sct`, `Munich, Germany.sct`, 5 saves/towns |
| Large | 256 | 257 × 257 | [DATA] all other shipped city-family files |

* Maps are always square in shipped data [DATA]. The terrain store is defined for a
  rectangular W × H tile grid (both dimensions are read separately) [STATIC], but the engine
  only needs to support the three square sizes above.
* Altitude is stored per **vertex** (tile corner): a (W+1) × (H+1) grid of unsigned bytes [DATA].
  A tile (x, z) has the four corners (x, z), (x+1, z), (x, z+1), (x+1, z+1).
* The water surface is also stored per vertex: a second (W+1) × (H+1) byte grid holding the
  water-surface altitude at that corner [DATA].
* A third per-tile bitset (one bit per tile) is persisted [DATA]; its meaning is `[OPEN]`
  (see Persisted state).

### Coordinate convention

* Coordinates follow `00-README.md` (Conventions): a vertex or tile is (x, z), and y is altitude.
  The first coordinate x selects the **row** of the persisted grids and the second coordinate z
  selects the position within the row [STATIC]. This is the same pair as the occupant placement
  word (`10-save-format.md` §6.2.3): an occupant anchored at tile (x, z) sits at the altitude of
  vertex (x, z) in 280 648 of 280 734 shipped building/prop entries [DATA 59/59].
* A tile (x, z) with 0 ≤ x < W and 0 ≤ z < H has the corners (x, z), (x+1, z), (x, z+1) and
  (x+1, z+1). Changing one vertex affects the up to four tiles that share it:
  (x−1, z−1), (x−1, z), (x, z−1) and (x, z), clipped to the map [STATIC].
* Sub-tile positions use 256 units per tile. A vertex (x, z) is at fixed-point position
  (256·x, 256·z) [STATIC], which matches `25-rendering.md`.

### Altitude units

* Altitude is an unsigned byte, 0–255 [DATA]. The generator clamps generated values to 16–240
  [STATIC]. A newly created terrain store is filled with 127, and its water level byte is set to
  246 until a generator or a file sets real values [STATIC].
* On screen one altitude unit is A = 2^z pixels at zoom z, i.e. a quarter of the tile diamond's
  height (`25-rendering.md`) [STATIC].
* A floating-point **height scale** is persisted with the terrain. It is 3.266 in all 59 shipped
  files [DATA]. When a terrain store is created, the value is taken from the city's world-scale
  setting. When a file is loaded, the file's value replaces it. The terrain reports it to other
  subsystems as "world height per altitude unit", or 0.0 while the terrain is not initialised
  [STATIC]. Its exact use elsewhere is `[OPEN]` (probably 3-D placement of
  automata and effects).
* Shipped terrains use the altitude range 4–255 [DATA]. The sea level is 30 in most shipped
  `.sct` files and 30–116 elsewhere [DATA].

### Slopes and buildability

* Slope is implied by the corner altitudes. Over all 59 shipped files, adjacent vertices (sharing
  a tile edge) differ by 0–4 units in 99.98 % of cases; differences of 5–8 occur in about 0.02 %
  [DATA]. Diagonal differences reach 11 [DATA]. The editing tools enforce a maximum step of 4 units
  between 8-neighbour vertices (edge and diagonal) along the edited front (section 4.4) [STATIC];
  pre-existing steeper steps elsewhere are left alone.
* Network grading keeps every vertex on a path within 4 units of the nearby path vertices (within
  ±3 positions along the path and at most 1 vertex apart on each axis). A vertex lower than
  (highest neighbour − 4) is raised to that value, and a vertex higher than (lowest neighbour + 4)
  is lowered to that value. The cut and fill amounts are accumulated separately for costing
  [STATIC]. See `12-networks.md`.
* Which corner patterns a building or zone accepts is decided by the occupant's placement rules
  (`13-buildings-occupants.md`) [OPEN for the terrain side].

## 2. Random number generator

The terrain generator uses the same linear congruential generator as the rest of the game
[STATIC]:

* State: one u32. Seeding sets the state to the seed; a seed of 0xFFFFFFFF means "use the
  millisecond system timer" [STATIC].
* Step: compute the 64-bit product V = state × 0x41C64E6D + 0x3039 (unsigned, full 64-bit
  arithmetic). The new state is V mod 2^32. The output is bits 16..47 of V, i.e.
  (V >> 16) mod 2^32 [STATIC].
* Uniform integer in [0, N): if N = 0 the result is 0 and no step is taken. If N < 0xFFFFFF, take
  one output and return output mod N. Otherwise (huge N, never used by the terrain code) keep
  taking outputs until one is ≤ N, and return it [STATIC].
* Uniform integer in [lo, hi): lo + uniform(hi − lo) [STATIC].

The terrain generator owns a private generator instance; it is seeded once at the start of each
generation with the seed passed by the caller [STATIC].

## 3. Terrain generation ("New City" random terrain)

All statements in this section are [STATIC] unless tagged otherwise. The algorithm is fully
deterministic for a given seed and option set, so a faithful implementation reproduces the
original terrain exactly, provided integer arithmetic, truncation and RNG call order are followed.

### 3.1 Generator interface and options

The generator works on its own vertex grids, then copies the result into the city terrain.

| Operation | Inputs | Effect |
|---|---|---|
| Initialise | map width W and height H in tiles | creates R × C working grids with R = W + 1 rows and C = H + 1 columns: altitude (u8), water surface (u8), tree density (u8), sea mask (1 bit) |
| Set difficulty | 1, 2 or 3 | selects the band-compression table (3.4 step 7) |
| Generate | seed (u32), hills (u8), water (u8), trees (u8), flags (u8) | runs steps 1–12 below |
| Apply | target terrain | copies water level, altitudes, water surface + sea bit, tree density into the target, vertex by vertex, rows in ascending order |

The row index of the working grids is the first coordinate of the city terrain's vertex
accessors (the same index that is the row index of the persisted altitude array).

Option semantics:

* **seed**: 0xFFFFFFFF means "use the system millisecond timer".
* **hills** (0–255): only the low byte is used for the terrain; larger means more relief.
* **water** (0–255): amount of water. Values ≥ 128 flood 90 % of the map.
* **trees** (0–255): forest density.
* **flags** (bit field):

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0x01 | coast (sea) along the column-0 edge |
| 1 | 0x02 | coast along the last-row edge (row R−1) |
| 2 | 0x04 | coast along the last-column edge (column C−1) |
| 3 | 0x08 | coast along the row-0 edge |
| 4 | 0x10 | river running from row 0 to row R−1 |
| 5 | 0x20 | river running from column 0 to column C−1 |
| 6 | 0x40 | central lake (lowers the centre control point; adds water) |
| 7 | 0x80 | central mountain (raises the centre control point; halves the hilliness divisor) |

* **difficulty**: the New City dialog passes its difficulty choice (1 = easy, 2 = medium,
  3 = hard). Any other value behaves like 1.
* The New City dialog's terrain preview calls Generate with seed = timer, hills = 64, water = 64,
  trees = 64 and flags = 0x24 (coast on the last-column edge plus a river across columns).
* Map edge per New City size `[OBSERVED]` (from the saves): Miniature 64, Small 128, Medium 192,
  Large 256 tiles. Miniature maps are drawn with the same earth edge faces, which look
  proportionally deep on such a small map.
* **Correction** `[OBSERVED]`: after the New City dialog is confirmed, the game opens a
  **Terrain Edit** panel over the generated map before the city starts (`09-ui-screens.md` §3.4).
  That panel makes the generator inputs reachable:
  * **Pick Features** is a small isometric diagram of the map, drawn with compass, zoom and rotate
    buttons. Clicking one of its four edge bands toggles a coastline on that map edge.
    Clicking its centre cycles one exclusive centre feature, in this order: river running from
    the north-west to the south-east edge → river running from the south-west to the north-east
    edge → central mountain → nothing → central lake → back to the first river. These are the
    coast-edge, river-direction, mountain and lake flag bits of the generator input.
  * **Adjust Parameters** is three vertical sliders: hills, water and trees. All start in the
    middle position, which corresponds to 64.
  * **Re-Generate Terrain** runs the generator with the current picker flags and slider values.
    Changing the picker alone does not regenerate the map.
  * The initial map (before any change) uses hills = water = trees = 64 and the default feature
    set: one river from the north-west edge plus a coast on one edge [OBSERVED]. This matches
    flags = 0x24 [STATIC].
  * The panel also carries the terraforming tools of §4 (lower, raise, level, trees, water,
    bulldoze) and an **Accept this Terrain** button that starts the city.

  Which slider position maps to which numeric value outside the middle position is [OPEN].
  Recommended: linear, 0 at the bottom to 128 at the top. Shipped hand-made terrains (`.sct`) come from editing or external tools, not from
  the generator [INFERRED].
  The in-game "terrain editor" consists of the tools in section 4.
* The New City dialog offers map sizes 64, 128, 192 and 256 tiles (default 256). Shipped data
  only uses 128/192/256 [DATA].

### 3.2 Random helpers used by the generator

Let rand(n) be the uniform integer of section 2 (output mod n; rand(0) = 0 with no RNG step).

* **randRange(lo, hi)** = lo + rand(hi − lo).
* **centreRand(lo, hi)**: half = ⌊(hi − lo)/2⌋; a = rand(half); b = rand(half); m = min(a, b);
  then if rand(2) ≠ 0, m = −m; result = lo + half + m. Exactly three RNG draws, in that order.
  The distribution is triangular around the midpoint.
* **lowRand(lo, hi)** = lo + min(rand(hi − lo), rand(hi − lo)) (two draws).
* **jitter2(a, b, s)**: if s < 2, return a. Otherwise d = s − 1, x = max(a, b) − d,
  y = min(a, b) + d. If x < y, v = centreRand(x, y + 1), else v = ⌊(x + y)/2⌋. Clamp v to
  16..240.
* **jitter4(a, b, c, e, s)**: as jitter2 but x = max of the four − d and y = min of the four + d
  (returns a if s < 2).
* **uniformReal(a, b)**: uses a second 32-bit state S2 that is set to the seed whenever the
  generator is seeded. If S2 = 0 it becomes 0x12345678. Then S2 = S2 × 0x278DDE6D mod 2^32, and
  the result is a + (b − a) × S2 / 2^32. It does not advance the main LCG.

### 3.3 Notation

g[r][c] is the working altitude grid, w[r][c] the water-surface grid, t[r][c] the tree grid and
m[r][c] the sea mask. R and C are the vertex counts. M = min(R − 1, C − 1). qR = ⌊(R − 1)/4⌋,
qC = ⌊(C − 1)/4⌋. L is the water level. All grid values are bytes; stores wrap modulo 256 except
where a clamp is stated.

### 3.4 Algorithm

1. **Seed and clear.** Seed the main RNG and S2 with the seed. Set g, w and t to 0 and clear m.
   The value 0 in g means "not yet generated" during step 4.
2. **Control lattice.** There are 25 control points k = 0..24 with column index ci = ⌊k/5⌋ and row
   index ri = k mod 5, at g[ri·qR][ci·qC]. Build three 25-bit masks from the flag bits:

   | Flag bit | Lowered points | Row-midpoint points | Column-midpoint points |
   |---|---|---|---|
   | 0 (0x01) | ci = 0 (k 0–4) | k 0–3 | – |
   | 1 (0x02) | ri = 4 (k 4,9,14,19,24) | – | k 4,9,14,19 |
   | 2 (0x04) | ci = 4 (k 20–24) | k 20–23 | – |
   | 3 (0x08) | ri = 0 (k 0,5,10,15,20) | – | k 0,5,10,15 |
   | 6 (0x40) | k 12 (centre) | – | – |
   | 4, 5, 7 | – | – | – |

   For k = 0..24 in order: base = 96 − ⌊M/4⌋ if k is lowered, else 96; store
   jitter2(base, base, 16). If flag 0x80 is set, then overwrite the centre g[2qR][2qC] with
   jitter2(b, b, 16) where b = 2⌊M/4⌋ + 96.
3. **Edge midpoints.** For k = 0..24 in order: if k is a row-midpoint point, set
   g[ri·qR + ⌊qR/2⌋][ci·qC] = ⌊(g[ri·qR + qR][ci·qC] + g[ri·qR][ci·qC]) / 2⌋. If k is a
   column-midpoint point, set g[ri·qR][ci·qC + ⌊qC/2⌋] = ⌊(g[ri·qR][ci·qC + qC] + g[ri·qR][ci·qC]) / 2⌋.
   This keeps coast edges uniformly low.
4. **Midpoint displacement.** Run Subdivide(r0 = 0, c0 = 0, r1 = R − 1, c1 = C − 1, s = M,
   depth = 8), defined as:
   1. mr = ⌊(r0 + r1)/2⌋, and if |r1 − r0| is odd, add 1 when rand(2) ≠ 0. Then
      mc = ⌊(c0 + c1)/2⌋, and if |c1 − c0| is odd, add 1 when rand(2) ≠ 0 (row decision first).
   2. Read the corners A = g[r0][c0], B = g[r0][c1], P = g[r1][c0] and Q = g[r1][c1].
   3. Fill only cells that are still 0, in this order: top g[r0][mc] = jitter2(A, B, s); bottom
      g[r1][mc] = jitter2(P, Q, s); left g[mr][c0] = jitter2(A, P, s); right
      g[mr][c1] = jitter2(B, Q, s); centre g[mr][mc] = jitter4(top, bottom, left, right, s).
      Each uses the cell's current value if it was already non-zero.
   4. If depth = 0, stop. Otherwise depth −= 1 and s = ⌊s/2⌋, then process the quadrants in the order
      (r0, c0)–(mr, mc), (r0, mc)–(mr, c1), (mr, c0)–(r1, mc), (mr, mc)–(r1, c1), each with the
      new s and depth.
5. **Water level.** Build a 256-bin histogram of g. Set L to (minimum altitude − 1) provisionally.
   Let a = max(water, 4) and k = 1 + 2 × (number of set flag bits among 0–3) + (3 if flag 0x40).
   The target count is T = ⌊⌊(R·C·a·k) / 256⌋ / 6⌋ (32-bit unsigned arithmetic). If water ≥ 128,
   T = ⌊R·C·9/10⌋ instead. If T = 0 then T = 1. L is the smallest altitude whose cumulative
   histogram count is ≥ T. Fill w with L.
6. **Hill thresholds.** Let D = 128 if flag 0x80 else 256 and f = 1 − √(hills / D × 0.5) (double
   precision). With N = R·C: t1 = trunc(N·f), t2 = trunc(N·f³), t3 = trunc(N·f⁴). Then:
   b1 = the first altitude v > L at which the histogram sum over (L, v] reaches t1;
   b2 = the first v > b1 at which the sum over (b1, v] reaches t2;
   b3 = the first v > b2 at which the sum over (b2, v] reaches t3.
   Any threshold that is never reached is 255.
7. **Band compression.** Pick the divisors (d1, d2, d3) and the quantise flag by difficulty:

   | Difficulty | d1 | d2 | d3 | Quantise (step 12) |
   |---|---|---|---|---|
   | 1 (easy; also any other value) | 12 | 6 | 3 | yes |
   | 2 (medium) | 10 | 5 | 3 | no |
   | 3 (hard) | 6 | 3 | 2 | no |

   With p = L + 2, c1 = p + (b1 − p)/d1 and c2 = c1 + (b2 − b1)/d2 (integer division truncating
   toward zero), every vertex with v ≥ p becomes:
   * v < b1: p + (v − p)/d1
   * b1 ≤ v < b2: c1 + (v − b1)/d2
   * b2 ≤ v < b3: c2 + (v − b2)/d3
   * v ≥ b3: v + (c2 + (b3 − b2)/d3 − b3)

   Vertices below p are unchanged. Low hills are therefore flattened strongly and high ground less.
8. **Beaches along coast edges.** For each coast flag in the order bit 0, 1, 2, 3, keep a run
   depth k that starts at 3 for each edge. Walk along the edge: bit 0 walks rows r = 0..R−1 at
   column 0 going inward along +c; bit 1 walks columns at row R−1 going inward along −r; bit 2
   walks rows at column C−1 going inward along −c; bit 3 walks columns at row 0 going inward along
   +r. At each edge vertex whose altitude is > L: draw u = centreRand(0, 7) (range 1..5), move k one
   step toward u (k + 1 if u > k, k − 1 if u < k). Then, for j = 0..k−1, cap the j-th inward vertex
   at L + 3 − (k − j). Edge vertices at or below L are skipped, with no RNG draw.
9. **River** (only if flag 0x10 or 0x20 is set):
   1. Base depth D = trunc(√R × 0.5) × (water + 64) / 128 (integer division). If D ≥ L,
      D = L − 1. Set the start depth and end depth to D.
   2. If flag 0x20: P0 = (randRange(⌊R/4⌋, ⌊3R/4⌋), 0) and P3 = (randRange(⌊R/4⌋, ⌊3R/4⌋), C − 1),
      drawn in that order. Then P1 = (randRange(⌊R/8⌋, ⌊7R/8⌋), randRange(⌊C/8⌋, ⌊7C/8⌋)) and
      P2 = (randRange(⌊R/8⌋, ⌊7R/8⌋), randRange(⌊C/8⌋, ⌊7C/8⌋)), each drawn row first, then column.
      If flag 0x01, start depth = max(D, L − g[P0]). If flag 0x04, end depth = max(D, L − g[P3]).
   3. If flag 0x10 (evaluated after 0x20; if both are set the 0x10 points replace the 0x20 ones,
      but the RNG draws of both still happen): P0 = (0, randRange(⌊C/4⌋, ⌊3C/4⌋)) and
      P3 = (R − 1, randRange(⌊C/4⌋, ⌊3C/4⌋)), with P1 and P2 as above. If flag 0x08, start depth =
      max(D, L − g[P0]). If flag 0x02, end depth = max(D, L − g[P3]).
   4. Sample the cubic Bézier curve through P0..P3 at t = i/1024 for i = 1..1024. Weights:
      (1−t)³, 3t(1−t)², 3t²(1−t), t³, expanded as 1 − 3t + 3t² − t³, 3t − 6t² + 3t³, 3t² − 3t³, t³.
      Each coordinate is truncated toward zero.
   5. For each sample n = 0..1023, skipping a sample identical to the previous one: depth
      e = (start + ⌊(end − start)·n / 1024⌋) mod 256. Then for the directions (−1, 0), (+1, 0),
      (0, −1), (0, +1) in that order (row step, column step): draw x = randRange(0, 3) and carve
      from the sample point with level λ = (L − (e + x)) mod 256. Carving repeats these actions:
      if λ < g at the current vertex, set it to λ; step one vertex; λ += 2 if λ < L, else λ += 1.
      It stops when the vertex leaves the grid or λ ≥ 252. The result is a V-shaped valley
      (slope 2 per vertex below water level, 1 above).
10. **Sea mask** (salt water). Clear m. Then for each coast flag (bits 0, 1, 2, 3, walking
    inward exactly as in step 8): budget n = ⌊water × R / 256⌋ (R is used for every edge) and
    land counter = 5. Walk inward from the edge vertex. At each vertex: stop if n = 0; if
    w < g − 1 there, decrement the land counter; decrement n; set m at the vertex. Stop when the
    land counter reaches 0. The mask marks sea water as opposed to fresh water (it limits tree
    density in step 11 and is passed to the terrain with the water surface).
11. **Trees.** clusters = ⌊trees × R × C / 65536⌋. If 0, skip. ρmax = ⌊trees/4⌋, raised to 2 if it
    is smaller. For each cluster: cr = rand(R); cc = rand(C); ρ = centreRand(0, ρmax);
    count = 1 if ρ ≤ 1, else centreRand(0, ρ²). For each of count trees: d = rand(ρ) if ρ > 1, else
    0; θ = uniformReal(0, 6.2832); r = cr + trunc(cos θ · d); c = cc + trunc(sin θ · d). If (r, c)
    is inside the grid and g[r][c] > w[r][c], set t[r][c] = lowRand(0, 128 if m[r][c] else 256).
    Later trees overwrite earlier ones.
12. **Quantise** (easy table only): every vertex with v > L + 2 becomes
    ((v − L − 2) with bit 0 cleared) + L + 2, which gives even terrace steps above the shore.

After Generate, Apply copies L into the terrain's water level, g into the altitude grid, w and m
into the water-surface grid and sea flag, and t into the per-vertex tree density. How the tree
density becomes flora occupants is in section 6.

## 4. Terrain editing and terraforming

### 4.1 Tool commands

The terrain tools are selected from the tool menu. The menu items name each tool by the ID given
below, as listed in `MenuItem.INI` (see `09-ui-screens.md`) [DATA]. Each tool performs the matching
terrain operation and charges the matching `[SpecialCosts]` price [STATIC].

| Tool | Menu item ID (`MenuItem.INI`) | Tool sound ID | Cost key in `SC3BudgetLayer.ini` `[SpecialCosts]` |
|---|---|---|---|
| Raise terrain | `0x825AD7F6` | 18 | `RaiseTerrain` |
| Lower terrain | `0xC25AD94F` | 19 | `LowerTerrain` |
| Level terrain | `0x825AD954` | – (none) | `LevelTerrain` |
| Place water | `0x02E763C1` | 20 | `DecorativeWater` |

Tree planting is a separate tool (menu item `0x4253EC3F`, sound 17); see section 6.

**Tree tool** [OBSERVED; cost STATIC]:
* One click plants flora on one tile. The price is the Flora entry of
  `SC3BudgetLayer.ini [BuildingPlacementCosts]` (`300=3`, §3 per tile); the hover label shows §3.
* Legality, from the hover tint (cyan = allowed, red = refused):
  * bare land and tiles that already hold flora are allowed. On a flora tile it adds or replaces
    trees and charges §3;
  * tiles occupied by networks, buildings or park pieces show red and are refused: nothing is
    placed and nothing is charged;
  * open water shows the cyan footprint, but a click plants nothing and charges nothing.
* Which flora kind and density the tool plants is [OPEN]. Use the city's flora set (§6).

**Clearing under terrain tools** (§4.2 step 3) [OBSERVED]:
* The clearing service removes **flora only**. When raise, lower or level would move vertices
  under trees, the trees on the affected tiles are removed and charged as part of the total. For
  example, a raise on a shore tile with several trees was previewed at §40–55 and charged §70, with
  the propagated neighbours' trees removed too. The per-tile clearing price was not separated from
  the propagation cost; use `[SpecialCosts] Demolish` (5) per cleared flora tile [INFERRED].
* Tiles holding **roads, other networks, park pieces or buildings** make the operation
  **illegal**: red preview (the hover label still shows a price), and a click does nothing and
  charges nothing. They are not bulldozed automatically.
* Power lines and pipes were not tested separately [OPEN]. They are networks and are expected to
  block the operation like roads.

The terrain reads the four prices by index from the budget's `[SpecialCosts]` table. The table
is indexed in this fixed order [STATIC]: 0 `LowDensityZone`, 1 `MedDensityZone`, 2
`HighDensityZone`, 3 `LandfillZone`, 4 `AirportZone`, 5 `SeaportZone`, 6 `Dezone`, 7 `Demolish`,
8 `LevelTerrain`, 9 `RaiseTerrain`, 10 `LowerTerrain`, 11 `DecorativeWater`. Each price is money
per altitude unit per vertex; all terrain costs below are computed as ⌊price × units / 4⌋, i.e.
one tile corner moved by one unit costs a quarter of the key's value (shipped values: 25, 25, 25
and 150, so one unit on a whole tile costs §25, and wetting a whole dry tile costs §150).

### 4.2 Common tool behaviour

All statements are [STATIC] unless tagged otherwise.

Every terrain tool works in two phases against the terrain: **plan** (compute the change without
writing it, return legality, a money cost and the affected rectangle) and **commit** (write the
change). The tool combines the plan with a second price and with the city's funds:

1. **Footprint.** Raise and lower use the single tile under the cursor: tile (⌊x/256⌋, ⌊y/256⌋)
   with its four corner vertices. There is no brush-size setting. Level uses a rectangle (see 4.5).
   Water uses the tile under the cursor (see 4.6).
2. **Plan.** The terrain plans the operation (sections 4.3–4.6). It returns: legal yes/no, the
   terrain cost C_t in money, and the affected rectangle R (the bounding box of every vertex the
   operation would change, including propagation).
3. **Clearing price.** The tool asks the city's clearing/bulldoze service for the price C_c of
   clearing rectangle R (flag value 0, one pass) [STATIC]; that service also reports whether
   clearing is possible. Its rules are owned by the occupant/demolition subsystem
   (`13-buildings-occupants.md`); the `[SpecialCosts]` `Demolish` key is the likely unit price
   [INFERRED].
4. **Total.** Total T = C_t + C_c. The operation is *unaffordable* when T ≠ 0 and T exceeds the
   current funds (signed 64-bit comparison).
5. **Hover preview.** While the mouse moves without a button, the tool only plans, so the UI can
   show T and highlight R. The vertices that would change mark their (up to four) adjacent tiles
   in a run-time "changed" bitset that the renderer uses for the highlight.
6. **Apply** (on button press and on every drag tick while the button is held):
   * if the plan was illegal, UI sound ID 30 ("not allowed" [INFERRED]) is played and nothing
     changes;
   * else if unaffordable, UI sound ID 32 ("no money" [INFERRED]) is played and nothing changes;
   * else the clearing service clears R (charging C_c), the terrain commits the plan (4.9), the
     total T is deducted from the funds [INFERRED: the deduction call was not traced], and the
     tool sound is started (sound IDs: raise 18, lower 19, water 20); on release it
     is stopped.
   * Each drag tick re-plans at the current cursor tile, so holding the button over one tile
     keeps raising (or lowering) it by one altitude unit per tick. The tick rate is the UI's
     mouse-drag rate [INFERRED].
   * Which of the tool's two apply handlers is "press" and which is "drag" was not separated;
     both behave as above. The sound action codes used are start, a looping parameter change
     (value 0x400) and stop [STATIC]; the exact looping semantics are `[OPEN]`.

Operation codes (passed to validators, 4.7): raise = 1, lower = 2, level = 4, water = 32.
Codes 8 and 16 belong to further terrain operations that the four tools do not use (most likely
the grading done for network and building placement, see `12-networks.md`) [INFERRED].

### 4.3 Raise and lower: per-click change

For raise (lower), the target altitude of each corner of the footprint tile is its current
altitude + 1, capped at 255 (− 1, floored at 0).

1. Walk the corners of the rectangle in this order, building an ordered closed **ring**: along
   the first edge (first coordinate increasing, second coordinate = low), then along the second
   coordinate at the high first coordinate, then back along the high-second edge, then back down
   the low-first edge. For a single tile this is (x, z), (x+1, z), (x+1, z+1), (x, z+1).
2. Each ring entry holds the vertex and its **target** altitude. While building the ring, each new
   entry is compared with the previous entry: they must be edge-adjacent (|Δa| + |Δb| = 1) and
   their target altitudes must differ by at most 4. Otherwise the operation is illegal. (So a tile
   that already has an edge step of 5 or more cannot be raised or lowered.) The ring holds at most
   1024 entries; overflow makes the operation illegal.
3. Propagate (4.4) and accumulate the propagation cost.
4. **Base cost:** let n be the number of vertices of the rectangle (inclusive of all corners,
   i.e. 4 for one tile) whose altitude actually changes (raise: altitude < 255; lower: altitude
   > 0). C_t += ⌊P · n / 4⌋ with P = `RaiseTerrain` (or `LowerTerrain`).
5. Run the validators (4.7) with the affected rectangle, the cost and the operation code.

With the shipped prices (25) a single raise click on an unconstrained tile therefore costs 25.

### 4.4 Slope propagation (shared by raise, lower and level)

The terrain enforces a **maximum altitude difference of 4 units between any two vertices that are
neighbours in the 8-neighbourhood** (edge or diagonal) across the moving front. Propagation grows
outwards from the ring one layer at a time:

1. Direction codes (Δa, Δb): 0 = (−1, −1), 1 = (0, −1), 2 = (+1, −1), 3 = (+1, 0),
   4 = (+1, +1), 5 = (0, +1), 6 = (−1, +1), 7 = (−1, 0). For each ring entry i (in ring order, the
   ring being closed) let p = the code of the direction from entry i to entry i−1, q = the code
   towards entry i+1, and turn t = (q − p) mod 8. The outward neighbours examined are at
   directions (p + k) mod 8 for:

   | Situation | k values |
   |---|---|
   | ring has exactly 1 entry (p taken as 3) | 2, 4, 6, 8 (the four edge neighbours) |
   | t = 4 (straight edge) | 2 (the perpendicular, outward for the ring orientation of 4.3) |
   | t = 6 (convex corner) | 2, 3, 4 (two edge neighbours and the diagonal between them) |
   | t = 0 (dead end) | 2, 4, 6 |
   | t = 2 (concave corner) | none: the entry is carried into the new ring |
   | any other t | none |

   When the diagonal (k = 3) at a convex corner changes, the k = 2 neighbour is also re-inserted
   before it so the new ring stays edge-connected [STATIC; detail simplified].
2. For each outward neighbour v that lies inside the vertex grid (0 ≤ x ≤ W, 0 ≤ z ≤ H):
   * Let hi = the highest and lo = the lowest target altitude among the inner-ring entries
     i−3 … i+2 that are within distance 1 of v on both axes, plus the entry most recently added
     to the new ring. The index is reduced modulo the ring length n using **unsigned 32-bit**
     arithmetic, so for i < 3 the index i − 3 wraps as (2^32 + i − 3) mod n, which is generally
     not n + i − 3 (an original quirk; reproduce it for identical results).
   * Allowed band: [max(hi − 4, 0), min(lo + 4, 255)].
   * If v's altitude is below the band it is raised to the lower bound; the raise amount is added
     to the "raised" total. If above, it is lowered to the upper bound; the amount is added to
     "lowered". Otherwise v is unchanged.
   * A changed v joins the new ring with its new altitude. An unchanged v does not; instead the
     inner entry itself is carried into the new ring (the front stays put there). Neighbours
     outside the grid are also replaced by the carried inner entry. Duplicate consecutive entries
     are merged and, where two consecutive entries are not edge-adjacent, the intermediate inner
     entry is inserted to keep the ring closed.
3. After the layer: cost C_t += ⌊(P_raise · raised + P_lower · lowered) / 4⌋, where P_raise =
   `RaiseTerrain` and P_lower = `LowerTerrain` (for all three tools, including level).
4. Repeat with the new ring until a layer changes nothing (raised + lowered = 0), or until the new
   ring would exceed 1020 entries (propagation simply stops there; the plan stays legal).
5. Affected rectangle R: the bounding box of the final ring, extended by one vertex towards the
   low side on both axes (not below 0) and clamped to W − 1 / H − 1 on the high side, in tile
   units.

The effect: raising a tile on a slope drags the surrounding terrain up in a cone with gradient 4
units per vertex; the cone is cut off where existing terrain already satisfies the band.
Propagation is not blocked by buildings or networks inside the terrain itself; such restrictions
come only from validators and the clearing service (4.7).

### 4.5 Level

* Footprint: the rectangle spanned by the drag start tile and the current cursor tile (both
  inclusive, normalised so low ≤ high) [STATIC; drag semantics INFERRED].
* Target altitude L: the current altitude of the vertex at the drag-start corner (the low corner
  of the first tile clicked).
* The ring is the rectangle perimeter with every target = L, checked as in 4.3 step 2 (trivially
  satisfied), then propagated as in 4.4.
* Base cost: C_t += ⌊`LevelTerrain` · Σ|alt(v) − L| / 4⌋ over all vertices v of the rectangle
  (inclusive), i.e. the price is per altitude unit moved per vertex.
* Commit sets every vertex of the rectangle to L and applies the propagation.
* Operation code 4.

### 4.6 Decorative water

All [STATIC] unless tagged.

* Footprint: the tile under the cursor (4 corner vertices).
* Legality checks, in order:
  1. The tile must not already be fully wet: count the tile's corners whose altitude ≤ water
     surface; if all 4 are wet the operation is illegal.
  2. Every tile in the 3 × 3 block centred on the target tile (clipped to the map) is checked for
     an occupant. An occupant there makes the operation illegal unless it carries occupant flag
     bit 0x400 and reports itself removable [STATIC for the test; that this admits flora and
     similar clearable objects is INFERRED].
  3. Validators (4.7) with operation code 32.
* Cost: n = number of footprint vertices that are dry (altitude > water surface). C_t =
  ⌊`DecorativeWater` · n / 4⌋. The clearing price of the 3 × 3 block is added as in 4.2 step 3.
* Commit: for each dry footprint vertex, set its water surface equal to its altitude. That
  vertex then counts as wet (altitude ≤ surface). No propagation is done and the sea level is
  not changed. Repeated application on neighbouring tiles grows a pond whose surface follows
  the ground; it does not flood lower neighbouring vertices.
* After commit, derived data is recomputed for the vertex rectangle extended by 3 on every side
  (clamped to the grid), and the rest of the game is told which tile rectangle changed; see 4.9.

### 4.6.1 Water-surface effect of raise, lower and level

When raise, lower or level commits, the water surface of the affected area is reset to the
global sea level (water level byte) [STATIC]. Consequences: decorative water and raised lakes
inside the affected area disappear; vertices lowered below the sea level become sea-level water;
vertices raised above it become dry. The exact rectangle reset is the affected rectangle R of
4.4 [INFERRED: the bounds expression was only partly decoded].

### 4.7 Validators and limits near buildings, networks and the edge

* Other subsystems can register **terraform validators** with the terrain. After planning, each
  validator is called with (affected rectangle R, planned cost, operation code). If any returns
  "refuse", the operation is illegal [STATIC].
* The disaster subsystem registers a validator while a disaster is active: operations 1, 2, 4,
  8 and 16 (not water) are refused when R comes within 5 tiles (1280 fixed-point units) of the
  disaster's position on both axes [STATIC]. See `21-disasters.md`.
* Buildings, zones, networks and flora inside R are handled by the clearing service of 4.2 step
  3: their clearing price is added to the cost, and they are cleared on commit [STATIC for the
  call; which objects refuse clearing (for example landmarks) belongs to the demolition rules,
  INFERRED].
* Map edge: propagation never leaves the vertex grid; edge vertices can be raised and lowered
  like any other [STATIC].

### 4.8 Global altitude shift and salt toggles (cheat commands)

The terrain accepts five cheat commands with the identifiers `Load terrain`, `Salt on`,
`Salt off`, `Terrain one up`, `Terrain one down`, `Terrain ten up` and `Terrain ten down`
[STATIC]. These are the typed cheat codes. They load a terrain, switch salt water on or off, and
shift the whole terrain up or down by 1 or 10 altitude units [INFERRED from the names]. Rendering
of underwater areas depends on whether the water is salt (sea) or fresh [INFERRED].

**Salt off** clears the sea-water flag (pollution-cell bit 30) of every tile. **Salt on**
recomputes the flags from the current terrain [STATIC]:
1. Clear the flag of every tile.
2. For each map edge, count the edge vertices that are under water (altitude < water surface). The
   edges are taken in this order:
   * z = 0 (bit 1);
   * x = W−1 (bit 2);
   * z = H−1 (bit 4);
   * x = 0 (bit 8).

   An edge is a **sea edge** when count × 100 > (vertex count along the edge) × 60, i.e. more than
   60 % of it is wet.
3. For each sea edge and each vertex along it, walk inward perpendicular to the edge with budget
   n = ⌊(vertex count) / 4⌋ and land counter = 5. At each vertex:
   * stop if n = 0;
   * if the water surface is below altitude − 1 (land), decrement the land counter;
   * decrement n;
   * set the sea flag for that vertex: the same four-tile marking as when a surface is written
     with the sea flag.

   Stop when the land counter reaches 0.

This is the generator's step 10 (§3.4) with the coast flags derived from today's water and a
budget of a quarter of the side instead of ⌊water × R / 256⌋. Neither command changes altitudes
or the water surface; only the salt bit changes. The water renderer and the neighbour features
that read it change accordingly.

### 4.9 Commit order and notifications

For raise, lower and level the commit repeats the plan computation and writes as it goes [STATIC]:

1. Build the ring (4.3/4.5) with target altitudes.
2. For each propagation layer: compute the layer (4.4), add its cost, then write the new
   altitude of every vertex that changed in that layer.
3. Write the footprint vertices (raise: +1 unless 255; lower: −1 unless 0; level: = L) and add
   the base cost.
4. Reset the water surface of the affected area to sea level (4.6.1).
5. Recompute derived terrain data (slopes, textures, shore flags) for the rectangle from
   (footprint low corner − 3, clamped to 0) to the affected rectangle's high corner (clamped to
   the grid), then tell the rest of the game that this tile rectangle changed. Networks, occupants
   and the display then update that area.

## 5. Water

* **Sea level** (water level byte): a single altitude value per map (persisted) [DATA].
* **Water surface grid**: a per-vertex byte (persisted). In all shipped files it is never below the
  sea level [DATA]. In most files it equals the sea level everywhere. 22 files contain raised
  water surfaces (lakes or rivers above sea level, e.g. `Frankfurt, Germany.sct`,
  `London, England.sct`) [DATA].
* A vertex is **under water** when its altitude is below the water surface at that vertex. The
  generator treats a vertex as land when its altitude is greater than its water surface [STATIC].
  The rule for when a whole tile counts as water (any corner, all corners, or the tile centre) is
  `[OPEN]`; the shipped data is consistent with "all four corners under water" for open water and
  "some corners under water" for shoreline tiles [INFERRED].
* **Sea (salt) vs fresh water**: the generator produces a per-vertex sea flag next to the water
  surface (section 3.4 step 10) and hands it to the terrain together with the water surface
  [STATIC]. Vertices reached from a coastal edge are sea; inland lakes and rivers are fresh.
  **Storage**: the terrain does not keep the flag itself. When a water surface is written at
  vertex (x, z) together with a set sea flag, the terrain sets the per-cell **sea-water flag** (bit
  30 of the pollution state word, `17-sim-services.md` §7.2.1) for each of the up to four tiles
  that share the vertex: (x−1, z−1), (x−1, z), (x, z−1) and (x, z), clipped to the map. Each tile
  maps to its 2 × 2 cell [STATIC]. Writing a surface with the sea flag clear never clears the
  flag. Only a whole-terrain reset clears all flags [STATIC]. The flag is therefore persisted
  with the pollution record, not with the terrain record. It can be stale after later terrain
  edits: in the 19 shipped files that have any flags set, 67–96 % of the flagged cells contain
  water [DATA]. Users of the flag: the water renderer (salt water colour, `25-rendering.md`), the
  new-city neighbour test (`19-sim-economy.md` §10.6) and the Neighbors-window region image
  (section 9).
* Coastline: there is no separate coastline object. Shore tiles are tiles whose corners straddle
  the water surface. They are drawn with the edge and shore palettes (section 7) [INFERRED].
* Land-value effects of water are configured in `SC3LandValue.ini` `[TuningParameters]`:
  `WaterEffect` (land-value bonus next to water) and `WaterEffectRadius` (radius in tiles). See
  `15-sim-growth.md`.

## 6. Flora

### 6.1 Flora occupants

Trees are ordinary occupants of agent type 300 (`Flora` in `CitySim.ini`) [DATA]. Their IDs come
from `Occupant.ini` (group `0xFFD30C03`, `0x80F48961`) [DATA]:

| Instance IDs | Kind |
|---|---|
| 6–12 | generic tree density levels 1–7 (sparse … dense) |
| 7000–7006 | pine-tree variants (1 small … 5 big) |
| 7040–7046 | palm-tree variants |
| 7052–7056 | additional pine/palm/deciduous variants and coastal wetlands |
| 7057 | burned flora (left after fires) |

Flora is **not** saved in the occupant record. It is saved in the flora layer
`406B1196:80AB8AB0:0`, one byte per tile holding kind × 16 + level (kinds: 1 wetlands, 2 palm,
3 deciduous, 4 pine). On load, each byte is converted back to a fixed flora occupant ID; for two of
the levels the ID is chosen at random between two alternatives. See `10-save-format.md` §6.3 for
the table [STATIC][DATA 59/59].

### 6.2 Tree density from terrain generation

The generator yields a tree density 0–255 per vertex (section 3.4 step 11). When the terrain
receives a density d for vertex (x, z) [STATIC]:

* if the terrain is still being built (new-terrain staging), the density is buffered per vertex;
* otherwise, if d ≥ 32, the city's flora service is asked to plant flora at fixed-point position
  (256·a, 256·b) with density level ⌊d / 32⌋ (1–7); d < 32 plants nothing.

**Planting a tree (the flora service).** Planting at tile (x, z) with level n works as follows
[STATIC; checked against an original new city, where all 65,536 flora cells were predicted]:

1. If the tile already holds an occupant:
   * if it is a replaceable flora occupant, keep its kind, remove it and go on (re-plant at the new
     level);
   * otherwise plant nothing.
2. If no kind was kept, choose one by height. Let h = (altitude of vertex (x, z) − sea level) mod 256.
   * h = 1–4 makes palm possible.
   * h = 3–126 makes deciduous possible.
   * h = 8–126 makes pine possible.
   * Where palm and deciduous are both possible (h = 3–4), draw `Rand(2)`: non-zero gives palm, 0
     gives deciduous.
   * Otherwise, where deciduous and pine are both possible (h ≥ 8), draw `Rand(2)`: 0 gives pine,
     non-zero gives deciduous.
   * With no draw, the kind is palm if palm is possible, else deciduous. So h = 0, h = 5–7 and
     h ≥ 127 all give deciduous.

   | h | Kind |
   |---|---|
   | 0 | deciduous |
   | 1–2 | palm |
   | 3–4 | 50 % palm, 50 % deciduous |
   | 5–7 | deciduous |
   | 8–126 | 50 % deciduous, 50 % pine |

   The chooser returns wetlands (kind 1) for a wet tile. Step 3 refuses every wet tile, so this
   path never plants wetlands.
3. Refuse if the tile is wet. A tile is wet when **any** of its four corners has water surface ≥
   altitude. The test includes equality, so a dry-looking tile with one corner exactly at sea level
   is refused. The kind draw of step 2 has already been made by this point.
4. Create occupant `FFD30C03:80F48961`. Its instance comes from the (kind, n) table of
   `10-save-format.md` §6.3; one more `Rand(2)` picks between the two alternatives. This draw is
   made for every tree that is planted. Kind must be 0–4 and n must be 0–9, otherwise nothing is
   planted.
5. Set the occupant's height to the tile altitude and add it to the occupant collection.

The saved flora byte is kind × 16 + n, with n = ⌊d / 32⌋ used as is (1–7), not reduced by one.

The draws use a generator of `16-sim-utilities.md` §1.1 that belongs to the flora service alone.
Nothing else draws from it. It is seeded from the millisecond tick count when the flora layer is
set up [STATIC]. Every call is `Rand(2)`, so each result depends only on bit 16 of the new state,
and the low 17 bits of the state evolve on their own.

In the original new city with terrain seed 411355049, the low 17 bits at the start of the apply
step were `0x0FF57`. The apply step visits tiles with x outer and z inner. A clone does not have to
reproduce the original's draws [INFERRED].

### 6.3 Flora sets (regional variants)

`SC3CityScheme.ini` section `[FloraSets]` maps a flora-set ID to a record (comma-separated
values): a constant type `0x85535958`, an index (`0x2001`–`0x2005`), 0, a name string ID, 0, a
description string ID and a final small integer [DATA]. The sprites of each non-default set live in
`Res/Sprites/FloraSets/<setID as 8 hex digits>/*.DAT`; the game enumerates `*.DAT` in that
directory [DATA][STATIC]:

| Set ID | Sprite file | Region |
|---|---|---|
| 0 | default sprites | default |
| `0x39EDF62B` | `Flora_North West.dat` | North-West |
| `0x79EF435A` | `Flora_Jungle.dat` | Jungle |
| `0xC55A6D62` | `Flora_Desert.dat` | Desert |
| `0xC55A6D84` | `BlackForest.dat` | Black Forest |

A selected set replaces the flora sprites while keeping the occupant IDs [INFERRED]. Whether trees grow or
spread after placement is `[OPEN]`; the terrain does not do it.

### 6.4 Effects

* Land value: `SC3LandValue.ini` `[AgentIDModifiers]` key `300` gives the flora modifier for
  residential, commercial, industrial and agricultural land (format `value:R,value:C,…`). See
  `15-sim-growth.md` [DATA].
* Placement cost: `SC3BudgetLayer.ini` `[BuildingPlacementCosts]` key `300` is the price per
  flora placement [DATA].
* Aura: `SC3Aura.ini` key `300` [DATA]. Pollution effects of flora are configured in
  `SC3Pollution.ini` if present (`[OPEN]`).

## 7. Terrain colouring and palettes

All palettes are 24-bit uncompressed BMP files in `Res/Dirt/Palette/` [DATA]:

| File | Size (w × h) | Role [STATIC unless noted] |
|---|---|---|
| `palBasic.bmp` | 1 × 53 | basic colours (palette slot 0) |
| `data1Pal.bmp`, `data2Pal.bmp`, `data3Pal.bmp` | 64 × 32 | data-map overlay ramps (slots 1–3) |
| land palette (resource) | 32 × 512 | land colour by altitude and shading (slot 4) |
| `palUnder.bmp` | 32 × 512 | underwater ground colours (slot 5) |
| `palWater.bmp` | 1 × 256 | water colours (slot 6) |
| `palEdgeL.bmp` | 1 × 512 | map-edge cliff face, light side (slot 7) [INFERRED] |
| `palEdgeD.bmp` | 1 × 512 | map-edge cliff face, dark side (slot 8) [INFERRED] |

* The **land palette** is not loaded from a loose file. It is the resource with type
  `0x62B9DA24`, group `0x455A72B1` and instance = the landscape's palette index, from
  `Res/Dirt/Palette/455A72B1_LandPalettes.IXF` (instances 1–5) [DATA][STATIC]. `Default.ini`
  registers that file for type-group `0x455A72B1`.
* The landscape is chosen by `SC3CityScheme.ini` `[LandScapes]`: the key is the landscape ID
  (0 = default, `0xC55A6D62`, `0xC55A6D84`, `0x655B8076`, `0xE55B8055`) and the fourth value is the
  palette instance (1, 3, 2, 5, 4 respectively) [DATA]. The meaning of the other values (type
  `0x85535958`, an index `0x1001`–`0x1005`, name string ID, two further IDs and a small integer) is
  `[OPEN]`.
* The loose `PALLAND.BMP` is identical to instance 1. `ArcticLand.bmp`, `CaribbeanLand.bmp` and
  `DesertLand.bmp` are not identical to any instance and appear to be unused sources [DATA].
* Rows and columns are indexed per terrain vertex. Row = 256 + (water-surface height − altitude),
  so rows below 256 are above the water surface and rows above 256 are under water. Column =
  ⌊shade / 2⌋, where shade comes from the vertex normal and two fixed lights. `25-rendering.md` §6
  gives the full shading, noise, haze, shoreline and edge-face rules [STATIC].

## 8. Map edges

* The map is a closed square. The vertex grid includes the outer border vertices (W+1 per side).
  Generated coasts lie on edges (section 3.4) [STATIC].
* Rivers and coasts that touch an edge continue off-map visually (the edge palettes draw the cut
  face) [INFERRED].
* Neighbour connections (roads, rails, pipes and so on crossing the border) are in
  `12-networks.md`. The terrain has no special edge rule beyond clipping all operations to the
  grid [STATIC].

## 9. Neighbour region image (Neighbors window terrain)

The Neighbors window (`09-ui-screens.md` §6.7) shows the city inside a larger diamond of
surrounding land: the city's own terrain in the middle third, and invented terrain for the four
neighbours around it. That surrounding terrain is **not stored anywhere** and is **not random per
session**. It is rebuilt each time the window is built, by the deterministic procedure below. The
procedure uses a fixed seed, so it depends only on the city's current altitude and water-surface
grids and on four sea-water flags at the edge midpoints. The same city therefore always shows the
same neighbours' terrain, and the neighbours' terrain changes only when those inputs change. All
statements in this section are [STATIC] unless tagged otherwise.

Verification [OBSERVED]: an implementation of this section reproduces the Neighbors-window
screenshot of `Berlin, Germany.sc3` with 93 % agreement of the palette index per sampled pixel.
The remainder comes from display scaling and overlay outlines. With any other seed, agreement
falls to about 45–50 %.

### 9.1 Working image

* The procedure fills a 256 × 256 image I(p, q) of 16-bit cells. Here p is the horizontal pixel
  coordinate (column) and q is the vertical one (row). Before anything else, every cell is set to
  the marker value **0xFFFF**, which means "not yet set".
* Writing a signed height h stores h modulo 65536. A negative value is therefore stored as its
  16-bit two's complement, and a stored −1 is the same as the marker (see 9.5).
* Reading a height takes the **low 8 bits as a signed byte** (−128…127). Every height read in
  9.4–9.6 uses this rule. A test for "unset" compares the full 16-bit cell with 0xFFFF.
* The image uses a private generator instance (the LCG of section 2). It is seeded with the
  constant **0x83010C56** at the start of every build.

Coordinate correspondence: column p follows the city's second coordinate z, and row q follows
the first coordinate x. The image side q = 0 faces the map edge x = 0 (neighbour 0, numbering of
`10-save-format.md` §6.6). Side q = 255 faces x = W−1 (neighbour 2), side p = 0 faces z = 0
(neighbour 1) and side p = 255 faces z = H−1 (neighbour 3).

### 9.2 Step 1: the city in the middle

R = W+1 and C = H+1 are the vertex counts. For i = 0…85 (outer loop) and j = 0…85 (inner loop):

1. a = ⌊i·R / 86⌋ and b = ⌊j·C / 86⌋. These are the vertex coordinates: x = a, z = b.
2. h = altitude(a, b) − waterSurface(a, b), clamped to 0…255. Underwater vertices give 0.
3. I(j + 85, i + 85) = h.

This fills the block p, q ∈ 85…170. Heights of 128 or more are later read back as negative (9.1),
so they draw as water. This is a quirk of the original and must be kept.

### 9.3 Step 2: sea flags of the four edges

For each neighbour, take the tile at the middle of its edge and read the **sea-water flag** of
that tile. The flag is held per 2 × 2-tile pollution cell (`17-sim-services.md` §7.2.1, bit 30), so
the tile (x, z) uses cell (⌊x/2⌋, ⌊z/2⌋).

| Neighbour | Test tile (x, z) |
|---|---|
| 0 | (0, ⌊C/2⌋) |
| 1 | (⌊R/2⌋, 0) |
| 2 | (R − 2, ⌊C/2⌋) |
| 3 | (⌊R/2⌋, C − 2) |

These are the same edge-midpoint tiles that the deal manager tests when a new city creates its
neighbours (`19-sim-economy.md` §10.6). The window repeats the test live each time it is built. It
does **not** use the stored neighbour `exists` byte.

### 9.4 Step 3: edges

Seed the generator (9.1). Then process the edges in the order neighbour 0, 2, 1, 3. No edge step
draws a random number.

| Neighbour | Sea flag set: fill with −20 | Sea flag clear: copy the city's edge outward |
|---|---|---|
| 0 | for r = 0…85, cells (p, r) with r ≤ p ≤ 255 − r | for p = 86…171: I(p, 0) = I(p, 86) |
| 2 | for r = 0…85, cells (p, 255 − r) with r ≤ p ≤ 255 − r | for p = 86…171: I(p, 255) = I(p, 169) |
| 1 | for r = 0…85, cells (r, q) with r ≤ q ≤ 255 − r | for q = 86…171: I(0, q) = I(86, q) |
| 3 | for r = 0…85, cells (255 − r, q) with r ≤ q ≤ 255 − r | for q = 86…171: I(255, q) = I(169, q) |

* The −20 fill makes a trapezoid of sea between the image edge and the city block, so a coastal
  neighbour shows as open water.
* The copy moves the raw 16-bit cell value. The source cell at index 171 lies outside the city
  block, so it still holds the marker, and the marker is what gets copied. The copy rows are
  86 and 169, not 85 and 170. Both off-by-one details are exact.

### 9.5 Step 4: corners and midpoint displacement

1. For each corner in the order (0, 0), (0, 255), (255, 0), (255, 255): if the cell is unset, write
   centreRand(−2, 10) (section 3.2).
2. Run **fill(0, 0, 255, 255)**. fill(x0, y0, x1, y1) works on the rectangle with corners
   (x0, y0) and (x1, y1), where x is p and y is q. It repeats these steps:
   1. If x0 + 1 ≥ x1 **and** y0 + 1 ≥ y1, stop.
   2. mx = ⌊(x0 + x1)/2⌋. If x0 + x1 is odd and rand(2) ≠ 0, then mx += 1. Compute my from y0 and
      y1 the same way (its own rand(2) draw, only when the sum is odd).
   3. d = ⌊min(x1 − x0, y1 − y0) / 2⌋.
   4. In this order, write each of the following points only if its cell is unset:
      * (x0, my) = mix(I(x0, y0), I(x0, y1), d)
      * (x1, my) = mix(I(x1, y0), I(x1, y1), d)
      * (mx, y0) = mix(I(x0, y0), I(x1, y0), d)
      * (mx, y1) = mix(I(x0, y1), I(x1, y1), d)
      * (mx, my): draw rand(2). If it is non-zero, use mix(I(x0, y0), I(x1, y1), d). Otherwise
        use mix(I(x0, y1), I(x1, y0), d). This draw is made only when the centre cell is unset.
   5. Recurse, in this order, into fill(x0, y0, mx, my), fill(mx, y0, x1, my) and
      fill(x0, my, mx, y1). Then continue the loop with (x0, y0) = (mx, my), which handles the
      fourth quadrant (mx, my, x1, y1).
3. **mix(a, b, d)** takes a and b as signed bytes:
   * m = (a + b)/2, truncated toward zero
   * lo = max(m − d, −127) and hi = min(m + d, 127)
   * if lo < hi − 2: v = centreRand(lo, hi). If v < 0, the result is (2v)/3 truncated toward zero.
     Otherwise the result is v.
   * otherwise the result is (lo + hi)/2, truncated toward zero.

   The result is stored as a signed byte value.

The rule for negative values (2v/3) makes the generated sea shallower than the generated land is
high. A result of −1 is stored as 0xFFFF, so that cell counts as unset again. A later step may
overwrite it, and a cell that is never revisited keeps the marker. Exactly the call order above
(including the conditional rand(2) draws) is required to reproduce the original image.

### 9.6 Step 5: colouring

Each cell's signed height h (the low byte read of 9.1, so a cell still holding the marker reads
as −1) is turned into a colour index k:

| h | k |
|---|---|
| h < 1 | 0 (water) |
| 1 ≤ h < 10 | ⌊h/2⌋ + 1 (1…5) |
| h ≥ 10 | ⌊10h/256⌋ + 5 (5…9), capped at 11 |

The colour is the pixel at column k, row 3 of the ramp image `62B9DA24:000005E5:00000021`
(32 × 32, in `Res/UI/Shared/000005E5_Maps.ixf`; decoding in `04-graphics-formats.md` §6) [DATA].
The coloured 256 × 256 image is what the window displays.

### 9.7 Display

* The window shows the image rotated so that it forms a diamond. At view rotation 0, the image is
  turned 45° clockwise [OBSERVED]. Its top side (q = 0, neighbour 0) then faces upper right, its
  left side (p = 0, neighbour 1) faces upper left, its right side (neighbour 3) faces lower right
  and its bottom side (neighbour 2) faces lower left. The city block is the small diamond in the
  centre.
* The image widget receives an angle that depends on the view rotation: 315°, 45°, 135° and 225°
  for rotations 0, 1, 2 and 3 [STATIC]. Each rotation step therefore turns the diamond by a
  further 90°, consistent with 315° meaning 45° clockwise [INFERRED].
* The widget occupies the window rectangle (31, 40)–(333, 342), which is 302 × 302 pixels. The
  rotated image is scaled so that the diamond's diagonals fill it [STATIC rectangle; OBSERVED
  fit]. The resampling filter is [OPEN]. Nearest-neighbour is visually indistinguishable.
* The neighbour outlines, names and populations are drawn on top (`09-ui-screens.md` §6.7).

## Persisted state

The terrain is a record inside the main compressed city stream (see `10-save-format.md` for the
container, the inner record directory and the versioned record header). It is present in every
city-family file (.sc3, .st3, .sct, .SNR): 59/59 shipped files parse [DATA].

* Record key (Type:Group:Instance) in the inner directory: `206C6E7C:21737DE5:00000000` [DATA].
* Record header: the versioned header of `10-save-format.md` §2.1 (u16 version = 3, u8 flags,
  u8 flags2 = 0, u32 0xDEADBEEF). Flags bit 0 set means that the begin/end marker strings are present. Shipped `.sct` files and one scenario use flags = 3;
  all other files use flags = 2 [DATA].

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | 3 |
| 0x02 | 1 | u8 | flags | bit 0: marker strings present; bit 1: flags2 follows (always set) |
| 0x03 | 1 | u8 | flags2 | 0 |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4+13 | u32 + chars | start marker | only if flags bit 0: length 13 then `DirtBag Start` (no terminator) |
| +0 | 4 | f32 | heightScale | world units per altitude unit (3.266 in all shipped files) |
| +4 | (W+1)(H+1) | u8[] | altitude | vertex altitudes; W+1 rows (x) of H+1 bytes (z), **rows stored from the last row (x = W) to the first** |
| … | 1 | u8 | waterLevel | global sea level, altitude units |
| … | (W+1)(H+1) | u8[] | waterSurface | per-vertex water surface altitude; same layout and reversed row order |
| … | W·H/8 | u32[W·H/32] | tileBits | one bit per tile, see below |
| … | 4+11 | u32 + chars | end marker | only if flags bit 0: length 11 then `DirtBag End` |

Notes:

* Row order: the grids have W+1 rows (indexed by the first coordinate x) of H+1 bytes (indexed by
  z). The writer emits row x = W first, then W−1, …, then 0, and the reader fills rows in the same
  descending order [STATIC]. Reading the file in order therefore gives the rows reversed. Rendered
  heightmaps of all 59 files are coherent (coasts, rivers and valleys are continuous) [DATA].
* Verification: a parser walks the inner record directory, finds this record, checks the length
  formula and the markers, and renders the heightmaps. It succeeds on 59/59 shipped city-family
  files (21 `.sct`, 10 `.st3`, 13 `.SNR`, 15 `.sc3`) [DATA].
* After loading, the engine recomputes all derived terrain data (slopes, textures, shore flags)
  for the whole map [STATIC].
* tileBits: tile (x, z) is bit (i mod 32) of the little-endian u32 number ⌊i/32⌋, where
  i = x + z·W [STATIC]. The bit is all 0 in every shipped `.sct` and starter town. In saved cities
  it marks a few tiles, mostly in exact 3 × 3 blocks on flat land (for example 99 = 11 blocks in
  `Madison, WI.sc3`) [DATA]. This suggests a per-tile flag set by certain 3 × 3 objects. The terrain
  itself never sets or reads these bits, so their meaning is `[OPEN]`. A new terrain starts with all bits clear. Preserve the bits on load and
  save.
* Three further per-tile bitsets exist at run time and are not persisted [STATIC].
* Sizes: W = H = 128, 192 or 256; record length = 8 + (21 if markers) + 4 + 2(W+1)(H+1) + 1 +
  W·H/8 + (15 if markers) [DATA].

## Open questions

* Meaning of the persisted per-tile bitset (3 × 3 block pattern in saved cities).
* Terraforming: the clearing service's rules (which occupants refuse to be cleared, price per
  object) are owned by the demolition subsystem; whether costs are charged while editing terrain
  before a city is founded; exact water-surface reset rectangle on raise/lower/level; the
  press-vs-drag handler split and sound looping; the tree-planting tool's rules.
* The run-time per-tile "changed" bitset used for the plan preview highlight versus the persisted
  tileBits (they are different bitsets).
* The exact rule for when a tile counts as water. (The sea flag storage is resolved in §5.)
* Neighbors-window region image (§9.7): the resampling filter used when the rotated image is
  scaled into the 302 × 302 widget, and the turning direction for view rotations 1–3.
* A whole-terrain operation exists that clears every sea flag and then re-marks sea cells by
  walking inward from edges where more than 60 % of the edge is under water (similar to §3.4 step
  10). Which command triggers it (probably the `Salt on` cheat of §4.8) and its exact walk rules
  have not been documented.
* Row and column indexing of the 32 × 512 land and underwater palettes and of the 512-entry edge
  palettes; how the data-map palettes (64 × 32) are indexed.
* World-space meaning of the persisted height scale (3.266).
* Whether flora grows or spreads over time (the terrain does not do it).
