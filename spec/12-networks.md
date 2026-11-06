# 12 — Networks: roads, highways, rail, subway, pipes, power lines

Status: draft. Sections marked `[OPEN]` still need confirmation.

Scope: per-tile network pieces, the TilingRules data language that turns a drawn path into concrete
network pieces, placement tools and costs, bridges/tunnels, highway on-ramps, neighbour
connections at the map edge, and the topology side of power/water propagation. Capacity and
supply semantics (how much power/water a plant produces, how much a building consumes) belong to
`16-sim-utilities.md`. Traffic belongs to `18-sim-transport.md`. Neighbour-deal economics belong to
`19-sim-economy.md`.

## 1. Network types

Six network kinds are tiled by rule tables, plus one auxiliary piece set for diagonals. All
per-network tables in the program are indexed in this order `[STATIC]`:

| Index | Prefix in file names | Network |
|-------|----------------------|---------|
| 0 | `ROAD` | Road (surface street) |
| 1 | `HWAY` | Highway (elevated, 2×2 footprint pieces) |
| 2 | `RAIL` | Rail |
| 3 | `SUBW` | Subway (underground layer) |
| 4 | `PIPE` | Water pipe (underground layer) |
| 5 | `POWR` | Power line |
| 6 | `DIAG` | Diagonal-capable piece set (membership set only) |

Other subsystems use their own numbering for the same networks; do not mix them up:

* Neighbour connections and `SC3ValveLayer.ini [ConnectionValveEffects]` use 0 = road, 1 = rail,
  2 = highway, 3 = subway `[DATA]`.
* The neighbour-connection query, the tiler entry point and the power layer use 1 = road,
  2 = rail, 3 = power, 4 = highway, 5 = pipe, 6 = subway `[STATIC]` (see also
  `18-sim-transport.md`).

## 2. Piece identifiers

A network piece on a tile is an occupant (see `13-buildings-occupants.md`) identified by its
occupant ID, plus a rotation 0–3. Everywhere in the TilingRules files a piece is written as a single
decimal integer `P` that packs both `[STATIC]`:

* occupant ID = `P >> 8` (integer division by 256);
* rotation = `P & 0xFF` (values 0–3).

Examples: `7424` = 0x1D00 = occupant 29, rotation 0; `2873600` = 0x2BD900 = occupant 11225,
rotation 0; `11008` = 0x2B00 = occupant 43, rotation 0.

The value `255` in a *direction* field is a wildcard meaning "this tile itself / no direction" and is
stored internally as 31 (see §4.5).

## 3. TilingRules directory: what is loaded

All files live in `Apps/Res/TilingRules/` (path built as `<Apps>\Res\TilingRules\<name>`; the
program prefixes the resource directory and `TilingRules\`) `[STATIC]`. Each file is read whole;
the reader allows at most 131 071 bytes per file `[STATIC]`.

Files loaded by the network subsystem at start-up, in this order `[STATIC]`:

1. Set files: `ROAD_GRND_Set.txt`, `HWAY_GRND_Set.txt`, `RAIL_GRND_Set.txt`,
   `SUBW_GRND_Set.txt`, `PIPE_GRND_Set.txt`, `POWR_GRND_Set.txt`, `DIAG_Set.txt`.
2. `Collapse.txt`; then for each network the Convert, Complex_Convert, Protected and Bridges files:
   `ROAD_GRND_Convert.txt`, `ROAD_GRND_Complex_Convert.txt`, `Road_GRND_Protected.txt`
   (name compared case-insensitively by the file system), `ROAD_GRND_Bridges.txt`, the same four
   for `RAIL` and `HWAY`, then `SUBW` and `PIPE` (Convert, Complex_Convert, Protected — no
   Bridges), then `POWR` (all four).
3. Rule files: `ROAD_GRND_SimpleRules.txt`, `ROAD_GRND_ComplexRules.txt`,
   `ROAD_GRND_final.txt`, and `<NET>_GRND_SIMPLERULES.txt`, `<NET>_GRND_COMPLEXRULES.txt`,
   `<NET>_GRND_final.txt` for `RAIL`, `HWAY`, `SUBW`, `PIPE`, `POWR`.

Files loaded by the zone subsystem (landfill appearance), not by networks `[STATIC]`:
`LandfillStartRule.txt`, `LandfillRules1.txt` … `LandfillRules6.txt` (see §4.8).

Files shipped but **never read** by the game `[STATIC]`:
`ROAD_GRND_SlopeRULES.txt`, `HWAY_GRND_SlopeRULES.txt`, `RAIL_GRND_SlopeRULES.txt`,
`SUBW_GRND_SlopeRULES.txt`, `PIPE_GRND_SlopeRULES.txt`, `POWR_GRND_SlopeRULES.txt`,
`ROAD_Exits.txt`, `RAIL_Exits.txt`, `SUBW_Exits.txt`, `networkIntesection.txt`,
`ROAD_GRND_FinalRules.txt`, `PIPE_GRND_Bridges.txt`, `SUBW_GRND_Bridges.txt`. They are
development artefacts; their information (slope pieces, network crossings, exits) is hard-coded in
the program and is specified in the sections below. A compatible engine must not depend on them,
but they are documented in §4.9 because they describe the same tables.

## 4. TilingRules language

### 4.1 Lexical layer (all files)

* The file is split into tokens at any of the four delimiter bytes: space (0x20), comma (0x2C),
  line feed (0x0A), tab (0x09) `[STATIC]`. Carriage return is **not** a delimiter; it remains glued
  to the end of the preceding token and is ignored by the integer conversion (which stops at the
  first non-digit). Empty tokens are skipped.
* Integers are decimal, parsed with C `atoi` semantics (optional leading whitespace and sign, then
  digits; stops at the first non-digit).
* Line structure carries no meaning; only the token sequence does.

### 4.2 Set files (`<NET>_GRND_Set.txt`, `DIAG_Set.txt`)

Grammar: an arbitrary sequence of tokens. For every token, leading non-digit characters are
discarded and the following run of digits is taken as an integer; tokens with no digits are
ignored `[STATIC]`. Braces and other punctuation are therefore noise. Every integer is an occupant
ID that belongs to the set (there is **no** leading count: e.g. the first entry `73` of
`HWAY_GRND_Set.txt` is a member) `[STATIC]`.

Meaning: the set of occupant IDs that make up each network. The program exposes "is this
occupant ID in set N" as a query for other subsystems; the tiler itself classifies pieces by
occupant flags (§5.2), not by these sets `[STATIC]`. `DIAG_Set.txt` is loaded but has no
effect on play `[STATIC]`; it only documents the diagonal pieces (§6.2.3).

### 4.3 Convert and Complex_Convert files

Grammar: `count` then a sequence of `from to` pairs, each value a packed piece (§2) `[STATIC]`.

* The first non-zero token is the declared count; it is not used to bound reading (all pairs
  present are read). In the shipped files the declared count always equals the number of pairs
  `[DATA]`.
* Each pair is decoded into (from-ID, from-rotation) and (to-ID, to-rotation).
* Parser quirk to reproduce: a pair slot is considered empty while its decoded ID is 0, so a value
  below 256 never occupies a slot; the next token is tried instead. No shipped file triggers this
  `[DATA]`.

Meaning `[STATIC]`: a *view* mapping used only while describing an existing neighbour piece to the
rule matcher (§4.6 step 3). When a neighbouring tile holds piece (ID, rotation) and the table has a
pair whose `from` equals it exactly, the matcher sees `to` instead. If several pairs match, the
**last** one in the file wins. The map itself is not changed by a conversion. `Convert` is used by
the simple and final passes; `Complex_Convert` by the complex pass. Typical use: a sloped or
bridge piece is presented as the equivalent flat straight piece.

### 4.4 Protected, Collapse and Bridges files (flat integer lists)

Grammar `[STATIC]`: `count` followed by a flat list of integers. The first non-zero token is the
count (not used as a bound); every later token is appended to the list verbatim (no `>>8`
decoding). Declared counts match the item counts in all shipped files `[DATA]`.

**Protected** (`<NET>_GRND_Protected.txt`; none for DIAG): plain occupant IDs. A piece whose ID is
in its network's protected list is never removed or re-tiled by the tiler (§4.6 step 2) and is not
overwritten by rule results (§4.6 step 6) `[STATIC]`. The lists hold stations, toll booths,
on-ramps, crossings, bridge and tunnel parts `[DATA]`. For power lines, protected pieces met
inside the re-tile region are additionally queued for the complex pass `[STATIC]`.

**Collapse** (`Collapse.txt`): after the count, a sequence of groups. Each group starts with its
length `L` **including the length token itself**, followed by `L−1` occupant IDs `[DATA]`
(the shipped file contains 19 groups and the lengths sum exactly to the list length). The groups enumerate the occupant IDs that make up one
multi-piece structure (bridges, highway interchanges, on-ramps, rail and road overpasses) so that
damaging or bulldozing one piece of the structure removes all pieces of the same group in the
surrounding structure `[INFERRED]`. The program rejects a group whose length token exceeds 29 and
examines at most 18 groups when the destroyed piece is occupant 11224 or 15106 `[STATIC]`.

**Bridges** (`ROAD`, `HWAY`, `RAIL`, `POWR`): after the count, a sequence of bridge-design
records `[DATA]`:

| Position | Field | Meaning |
|----------|-------|---------|
| 0 | `L` | Record length in tokens, including this token |
| 1 | `axis` | 0 or 1 for road/rail/power (axis of the span); 0–3 for highway (direction) |
| 2 | `n` | Minimum span length W (water tiles) for which the design is chosen (road: 18, 10, 6, 1; rail, power, highway: 1); the first matching record in file order wins, so designs are listed in decreasing `n` `[STATIC]` |
| 3.. | segments | Five segments, each `k` followed by `k` packed pieces (§2): start ramp, left filler, repeating span body, right filler, end ramp (§6.4) |

Example (first road record): `21, 0, 18, 1, A, 1, B, 9, S1…S9, 1, B, 1, A'` — ramp A, filler B,
nine span pieces, filler B, ramp A'. When a dragged road/rail/power/highway path crosses water
and the path is at least 4 tiles long, the tool builds a bridge from these records (§6.4).

### 4.5 Rule files (SIMPLERULES, COMPLEXRULES, final)

Grammar `[STATIC]`: a stream of *records*. Each record begins with a tag token 0–5 and has a fixed
number of following tokens:

| Tag | Following tokens | Meaning |
|-----|------------------|---------|
| 0 | `N` | Header: declared number of rules (informational) |
| 1 | `K` | Start of a rule: rule key `K` (neighbour connection mask, §4.6) |
| 2 | `C` | Declared number of condition records (informational) |
| 3 | `dir`, `piece` | One condition |
| 4 | `R` | Number of result records that follow; closes the condition list |
| 5 | `dir`, `piece` | One result |

* Parsing is a token-driven state machine: a tag token selects the record; the following
  token(s) fill it; then a tag is expected again. Tokens whose first character is not a digit are
  skipped. An unknown tag value is ignored.
* `dir` value 255 is stored as 31 (the tile itself). `piece` is decoded per §2.
* A rule is complete when the number of tag-5 records equals the `R` of its tag-4 record. The
  rule (key, conditions, results) is appended to the table in file order. The key of a tag-1
  record is retained for following rules until the next tag-1 record.
* In all shipped files the header `N` equals the number of rules and each `C` equals the number
  of conditions `[DATA]`.

Direction codes (`dir`) are offsets (dx, dz) from the tile being tiled, where dx is the first map
coordinate and dz the second `[STATIC]`:

| dir | (dx,dz) | dir | (dx,dz) | dir | (dx,dz) | dir | (dx,dz) |
|----:|---------|----:|---------|----:|---------|----:|---------|
| 0 | (−1, 0) | 6 | (+1,+1) | 12 | (0,−2) | 18 | (+2,+2) |
| 1 | (0,−1) | 7 | (−1,+1) | 13 | (+1,−2) | 19 | (+1,+2) |
| 2 | (+1, 0) | 8 | (−2, 0) | 14 | (+2,−2) | 20 | (0,+2) |
| 3 | (0,+1) | 9 | (−2,−1) | 15 | (+2,−1) | 21 | (−1,+2) |
| 4 | (−1,−1) | 10 | (−2,−2) | 16 | (+2, 0) | 22 | (−2,+2) |
| 5 | (+1,−1) | 11 | (−1,−2) | 17 | (+2,+1) | 23 | (−2,+1) |

Codes 24–31 all mean (0,0); 31 (written 255) is the canonical "this tile".

The *rotation* field of a condition has special values when the condition's ID is 0:

| Condition (ID 0, rot r) | Matches a neighbour that is… |
|-------------------------|------------------------------|
| r = 0 | not a piece of this network (empty, off-map, or a foreign occupant) |
| r = 1 | empty but part of the path currently being drawn, or a map-edge tile carrying a neighbour connection of this network |
| r = 5 | empty / ignorable (exactly) |

Results with ID 0 mean "remove this network's piece at that tile".

### 4.6 Evaluation semantics (the tiler)

The tiler runs once for every placement or demolition of a network path. Input: the network kind,
the ordered list of path tiles, and the map. Steps `[STATIC]`:

1. **Region.** Around every path tile, take a square window of radius r (r = 5 for road, highway
   and subway; r = 2 for rail, power and pipe). Inside it, flood-fill (4-connected) from the path
   tile through tiles that hold a piece of this network. All reached tiles plus all path tiles
   form the *re-tile set*.
2. **Clearing.** Each tile of the re-tile set that holds an unprotected piece of this network
   (per §4.4 Protected) is emptied of that piece. Tiles holding protected pieces are removed from
   the set. On path tiles that already carry a network piece (flag 0x800), a crossing handler runs first;
   tiles it resolves are taken out of the set (it places the combined crossing piece of §5.3
   directly) `[STATIC]`.
3. **Slopes.** Every remaining tile whose four corner heights are not all equal, but which is a
   *valid slope* (see §6.3), gets its slope piece from the hard-coded slope table (§6.3) and is
   removed from the set. Other non-flat tiles are left for the rules.
Before step 3, the tiler copies the working set S (after clearing and the removal of
   protected tiles) into a list **C**, which it keeps unchanged [STATIC, SIMNTWRK].
4. **Simple pass** (SIMPLERULES, neighbourhood radius 1, mask over directions 0–7, Convert view):
   evaluate every tile in S. A rule's result **targets** (T + result offset), not the evaluated
   tile, are collected during the pass:
   * after the pass, every collected target is removed from S;
   * targets of (ID 0, rot 0) results go to the final-pass queue instead.
   A tile whose rule has no self result therefore stays in S. Targets are collected even if
   placing the piece failed. Repeat the whole pass while the size of S changes.
5. **Complex pass** (COMPLEXRULES, radius 2, all 24 directions plus the tile itself, Complex_Convert
   view):
   * It evaluates **every tile of C**, the full re-tile set, including tiles that the simple pass
     already tiled. For power, the queued protected tiles are added to C.
   * The neighbourhood includes the tile itself as dir 31, with its current piece. So rules with a
     self condition (e.g. subway: self = 11611 r0, east = 11611, west = 16039 → self = 16012
     "Subway Double Wide"; the highway sign family 15145–15148) fire on pieces placed earlier in
     this run.
   * Result targets are removed from S, never from C.
   * Repeat while the previous iteration applied at least one result other than (ID 0, rot 0),
     at most 5 iterations.
   CORRECTION: the earlier text evaluated only tiles the simple pass had left.
6. **Final pass** (final, radius 1, mask over directions 0–3, Convert view): evaluate once every
   tile still in S (never a simple or complex target) plus the queued (ID 0, rot 0) targets.

[DATA] Re-tiling all stored networks of the 14 shipped cities with steps 4–6 as above, together
with the exit sides of §4.8, gives identical pieces for:
* highway: 99.1 % (93.9 % with the old reading);
* subway: 70.3 % (40.7 % with the old reading);
* power: 86.1 % (82.4 % with the old reading);
* road and rail: unchanged at 99.7 % and 99.97 %.

**Evaluating one tile T against a rule table:**

1. Skip T unless it is flat (all four corner heights equal).
2. Build the *neighbourhood description* of T: a list of entries (dir, ID, rot) and a mask.
   * Simple and final passes (3×3, clipped to the map): only tiles holding a piece of this network
     add an entry (dir, ID, rot), after the Convert view. Empty and foreign tiles add no entry, and
     the tile itself is not included [STATIC].
   * Complex pass (5×5 including the tile itself as dir 31): a piece of this network adds
     (dir, ID, rot) after the Complex_Convert view; a foreign occupant adds (dir, 0, 0), or
     (dir, 0, 5) if it has the "ignorable" flag 0x400; an empty tile adds (dir, 0, 5), or (dir, 0, 1)
     when the tile is in list C. No shipped rule file has a condition with ID 0, so these
     special entries never decide a shipped match [DATA]. In the complex pass, positions off the
     map add (dir, 0, 5), changed to (dir, 0, 1) when the position is exactly one step outside the
     map edge on the side that holds a neighbour connection of this network.
   * The **mask** is a bit set: bit d is set when the neighbour in direction d (d below the pass
     limit: 8 for simple, 4 for final) holds a piece of this network, or is a tile of the path being
     drawn. In the final pass for road, highway, rail and subway, a neighbouring piece sets its bit
     only if that piece exposes a connection on the side facing T (as defined by the piece's
     connection data, see `13-buildings-occupants.md`); for subway pieces carrying flags 0x40100 the
     bit is set unconditionally. Map-edge rule: if T is in the first column (dx-edge) bit 0, first
     row bit 1, last column bit 2, last row bit 3 is set when a neighbour connection of this network
     exists at T. The complex pass never sets mask bits (its mask is 0).
3. Scan the rule table in file order. A rule is a candidate if its key is 256 (wildcard) or equals
   the mask. A candidate *matches* if every one of its conditions (dir, ID, rot) finds an entry
   with the same dir and the same ID whose rot equals the condition's rot, or the condition's rot
   is 0 and the entry's rot is 0 or 5. A rule with no conditions matches. The **first** matching
   rule wins; its results are used.
4. Validate the results: every result target (T + dir offset) must be on the map, flat, and either
   empty or hold a piece of this network (or a piece the network may cross, §5.3) that is not
   protected; otherwise the rule is not applied to T.
5. Apply the results: for each (dir, ID, rot) the target tile receives occupant ID with rotation
   rot at the terrain altitude of the tile (ID 0: remove this network's piece there). If the target
   is occupied by a removable piece of this network or a crossable piece, that piece is removed
   first. Targets of results are recorded as done, except results with ID 0 and rotation 0, whose
   targets are queued for the final pass.

Final-pass keys are the 4-bit connection mask (bit 0 west/−x, 1 north/−z, 2 east/+x, 3 south/+z),
so each `<NET>_GRND_final.txt` is a 16-entry table "mask → piece" `[DATA]` (e.g. road: mask 0 and
15 → occupant 43; single-bit masks → dead-end piece 11225 in four rotations; masks 5 and 10 →
straight piece 29 rotations 1 and 0).

### 4.7 Landfill rules (zone subsystem)

`LandfillStartRule.txt` and `LandfillRules1.txt`…`LandfillRules6.txt` use exactly the rule
grammar of §4.5 (all rules have key 256) `[DATA]`; they are loaded by the zone developer from
`<Apps>\Res\TilingRules\` and drive the appearance of landfill zones. Their evaluation belongs to
`15-sim-growth.md` `[OPEN]` there.

### 4.8 Unused files

* `<NET>_GRND_SlopeRULES.txt`: valid rule-grammar files (keys 5, 10, 20, 40, 80, 160) that encode
  the slope-piece choice; the program uses the hard-coded table of §6.3 instead `[STATIC]`.
* `<NET>_Exits.txt` (ROAD, RAIL, SUBW): `count` (344) then 344 pairs `occupantID, value`. The values
  are identical to the piece attributes 0x136 (road/highway), 0x137 (rail) and 0x138 (subway) for
  all 310 IDs that have an attribute record [DATA]. The program uses the attributes.

**Exposed sides for the final-pass mask** (§4.6 step 2) [DATA: reproduces the stored pieces of
all 14 shipped cities]:
* Take all 32 bits of the piece's matrix: 0x136 for road and highway, 0x137 for rail, 0x138
  for subway. These are both halves of the spec 18 §5.4 turn matrix.
* The piece exposes side k (0 W/−x, 1 N/−z, 2 E/+x, 3 S/+z) at rotation r iff some set bit b
  satisfies ((b mod 4) + r) mod 4 = k. In other words: OR the eight nibbles, then rotate that
  4-bit mask left by r.
* Correction: an earlier revision used only the low 16 bits. That gives the same result for every
  piece whose two halves expose the same sides. It is wrong for the highway on-ramp pieces
  15130–15141. These are in the road set and the road protected list. Their low half exposes the
  ramp's highway side, and their high half the road side. Example: 15141 has matrix 0x20000080.
  Its low half gives side 3, its high half side 1, so at r = 2 it exposes N and S. With the low
  half only, the road tile beside the ramp counts the ramp as closed and becomes 39 or 29
  instead of 43 or 39 [DATA].
* Examples at r = 0:
  * 29, 32, 69, 11202, 11204: W, E
  * 31, 11201: N, S
  * 35, 11203: W, S
  * 39, 15098: W, N, S
  * 11225: S
  * 11258: W, E, S
  * 43: all four sides
* A piece without a matrix is treated as open on all sides.
* Re-tiling the stored networks of the 14 shipped cities with this rule and the neighbour
  connections gives identical pieces for:
  * road: 99.85 % of 121 189 tiles (99.74 % with the low half only);
  * rail: 99.97 %;
  * highway: 99.12 % (99.10 %).
  Without it (every neighbour counted as open), road and rail give 99.36 % and 99.40 % [DATA].
* Remaining road differences, about 100 tiles in total, all at pairs of 11204 pieces:
  * stored 29 where the re-tile gives 11204: 16 in Berlin, 18 in Liverpool, 11 in London;
  * stored 39 where it gives 11204, and the reverse.
  No neighbour-exposure rule explains them. `[INFERRED]` They depend on the order in which the
  pieces were drawn.
* `networkIntesection.txt`: a 3×3 matrix of quoted hexadecimal packed pieces, row/column 0 = road,
  1 = rail, 2 = power line; diagonal `"0"`. Road×rail = occupant 0x45 (69), road×power = 0x43 (67),
  rail×power = 0x47 (71); rotation 0 or 1 depending on which network runs along the first axis
  `[DATA]`.
* `ROAD_GRND_FinalRules.txt`: a brace-syntax table `{mask, {piece}}` for masks 0–15 identical in
  content to `ROAD_GRND_final.txt` `[DATA]`.
* `PIPE_GRND_Bridges.txt`, `SUBW_GRND_Bridges.txt`: contain only `0`.

Parser coverage: a reference parser implementing §4.1–4.8 parses **68/68** shipped files with no
errors `[DATA]`. Rule counts: ROAD simple 430 / complex 195 / final 16; HWAY 158 / 673 / 16;
RAIL 334 / 0 / 16; SUBW 342 / 97 / 16; PIPE 342 / 0 / 16; POWR 54 / 62 / 16.

## 5. Per-tile network state

### 5.1 Layers

The network subsystem keeps three tile layers `[STATIC]`:

| Layer | Networks | Notes |
|-------|----------|-------|
| Surface | road, highway, rail, power line | one network occupant per tile; crossings are dedicated occupants (§5.3) |
| Pipe | water pipe | underground; independent of the surface layer |
| Subway | subway | underground; independent of the surface and pipe layers |

A tile can therefore carry one surface piece, one pipe piece and one subway piece at the same time.
Highway pieces are drawn as 2-tile-wide carriageways; each tile holds its own highway occupant
`[INFERRED]`. The piece itself (occupant ID, rotation, altitude, footprint) is an occupant; see
`13-buildings-occupants.md`.

### 5.2 Network membership (occupant flags)

Whether an occupant is a piece of a given network is decided from its occupant flag word, not from
the Set files `[STATIC]`. "Has F" below means all bits of F are set:

| Test | Flags required |
|------|----------------|
| network piece (generic) | has 0x800 |
| road piece | has 0x8000 and 0x800 |
| highway piece | has 0x10000 and 0x800 |
| rail piece | has 0x20000 and 0x800 |
| subway piece | has 0x40000 |
| pipe piece | has 0x800000 |
| power-line piece | has 0x400000 and 0x800 |
| "ignorable" (treated as empty by the tiler) | has 0x400 |

The Set files (§4.2) are exposed as "is occupant ID a member of network N's set" queries for
other subsystems `[STATIC]`; the tiler itself does not consult them `[STATIC]`.

### 5.3 Crossings (which networks can share a tile)

Pipes and subways never conflict with surface networks (separate layers). On the surface layer,
a new network may replace (cross) an existing occupant only if the occupant is removable and either
has the ignorable flag 0x400, or is a network piece (0x800) that belongs to the same network, or
whose occupant ID is in the crossing list of the new network `[STATIC]`:

| New network | Existing occupant IDs it may cross |
|-------------|------------------------------------|
| road | 14, 15, 44, 45, 73, 92, 10001, 10036, 10037, 15051 |
| rail | 14, 15, 29, 30, 73, 92, 10001, 10036, 10037, 11204, 15051 |
| highway | 14, 15, 29, 30, 44, 45, 92, 10001, 10036, 10037, 11204 |
| power line | 29, 30, 44, 45, 73, 11204, 15051 |

(14/15/92/10001/10036/10037 are power-line pieces, 29/30/11204 road pieces, 44/45 rail pieces,
73/15051 highway pieces `[DATA]` from the Set files.) The crossing itself is resolved by the
rules: the rule tables contain results placing the combined pieces (road×rail 69, road×power 67,
rail×power 71, power over highway, road under highway; agent types 17105 "Power over road", 17209
"Power over highway", 17203 "Road under highway" in `CitySim.ini [AgentTypes]`) `[DATA]`.
Subway pieces with flags 0x40100 are always considered connected in the final pass `[STATIC]`.

**Crossing handler** `[STATIC]`. Before the rule passes, every path tile that already holds a
surface piece of *another* network is offered to the crossing handler (§4.6 step 2). It succeeds
only if the tile is not water (unless the path is a bridge), the existing occupant is accepted by
the tool's x- or z-crossing filter (§6.2.1) and its ID is not in the new network's Protected list.
The existing piece is classified by occupant ID:

| Existing occupant ID | Class |
|----------------------|-------|
| 29, 11204 | 1 road |
| 44 | 2 rail |
| 14, 15, 92 | 3 power line |
| 73 | 4 highway (first orientation) |
| 15051 | 5 highway (second orientation) |
| anything else | no crossing; the tile is left to the rules |

The new network's class is 1 road, 2 rail, 3 power; a new highway is 4, or 5 when its carriageway
pairing offset (§6.2.4) has a non-zero z component. The combined piece is read from this hard-coded
table T[c][r] (packed as occupant/rotation; "–" = none, the crossing fails):

| c \ r | 1 road | 2 rail | 3 power | 4 hwy | 5 hwy |
|--------|--------|--------|---------|-------|-------|
| 1 road | – | 69/0 | 67/0 | 11258/2 | 15098/2 |
| 2 rail | 69/1 | – | 71/0 | 16006/2 | 16007/2 |
| 3 power | 67/1 | 71/1 | – | 15036/2 | 15037/2 |
| 4 hwy | 11258/0 | 16006/0 | 15036/0 | – | – |
| 5 hwy | 15098/0 | 16007/0 | 15037/0 | – | – |

Orientation selects which of the two transposed cells is used. Let E be the existing class and N
the new class:

* New road, rail or power: use T[N][E] if the existing piece's rotation is non-zero (it runs along
  x), else T[E][N]. Exceptions: existing occupant 15 with rotation 0 uses T[N][E]; existing 11204
  with rotation 2 uses T[E][N].
* New highway: with pairing offset (ox, oy) and carriageway index (first/second of the pair): for
  the first carriageway use T[N][E] unless ox < 0 or oy < 0; for the second carriageway use T[N][E]
  unless ox > 0 or oy > 0; otherwise T[E][N].

The existing occupant is removed and the combined piece is created at the tile's terrain height
and inserted into the surface layer; the tile then leaves the re-tile set. (Occupant IDs 16006 and
16007 here are rail-under-highway pieces; do not confuse them with agent type 16006, the tunnel.)
These choices match `networkIntesection.txt` for road×rail×power `[DATA]`.

## 6. Placement tools, costs, slopes, bridges, tunnels

### 6.1 Cost per piece

Placement costs come from `SC3BudgetLayer.ini [BuildingPlacementCosts]`, keyed by agent-type ID
(the hierarchy is `CitySim.ini [AgentTypeHierarchy]`), in simoleons per piece `[DATA]`. Network
keys: 16000 road, 16001 highway, 17202 highway ramp, 17000 rail, 17400 subway, 17300 pipes,
17100 power lines, 16002 road-bridge part, 16003 suspension-bridge part, 16004 girder-bridge
part, 16006 tunnel, and the neighbour-connection pieces 16017 road, 17006 rail, 12102 power,
17207 highway, 17310 pipes, 17410 subway. Note that the comments in the shipped INI
("Rail Bridge", "Highway Bridge") do not match the agent-type names; the key is authoritative.
A piece whose own type has no entry uses its nearest ancestor's entry `[INFERRED]`. Demolition
costs `[SpecialCosts] Demolish` per tile and `DemolishRubbleTile` per rubble tile `[DATA]`.
Cost evaluation and the ordinance discount (`TireRecyclingOrdEffect`) belong to
`19-sim-economy.md`. Slopes add no separate surcharge: a sloped tile simply costs its (sloped)
piece `[INFERRED]`.

### 6.2 Drag routing

#### 6.2.1 Network tools and their parameters

There are seven network drag tools. Each is named in the tool menu by the menu item ID given below
(as listed in `MenuItem.INI`) [DATA]. Each tool has a per-network parameter set that drives the shared
routing algorithm below `[STATIC]`:

| Tool | Menu item ID (`MenuItem.INI`) | Net enum (§1, second list) | Cost key (per tile) | Wide | MinRun | Staircase | AnySlope |
|------|----------|---------------------------|---------------------|------|--------|-----------|----------|
| Road | `C2B5DE77` | 1 | 16000 | no | 3 | yes | yes |
| Highway | `42B5DE86` | 4 | 16001 | yes | 3 | yes | yes |
| Rail | `42B5DE98` | 2 | 17000 | no | 3 | yes | yes |
| Subway | `C2B5DEA6` | 6 | 17400 | no | 0 | yes | yes |
| Highway on-ramp | `02B5DEBB` | 4 | 16001 | no | 3 | no | no |
| Power line | `42C2F8BF` | 3 | 17100 | no | 1 | no | yes |
| Water pipe | `22C2F8C5` | 5 | 17300 | no | 0 | yes | yes |

* **Cost key**: the agent type whose `SC3BudgetLayer.ini [BuildingPlacementCosts]` entry is the
  per-tile price (§6.1). It is looked up once when the tool is activated.
* **Wide**: the path is two tiles wide (highway carriageway pair, §6.2.4).
* **MinRun**: number of consecutive straight steps the path must have made before it may enter a
  water tile; 0 means water is not examined at all (subway and pipe run under water).
* **Staircase**: diagonal drags produce an alternating x/z staircase (otherwise an L).
* **AnySlope**: if *no*, every path tile must be flat or a valid slope (§6.3); if *yes*, other
  terrain is accepted (the network manager levels or rejects it when building, §6.2.6).

Each tool also has three acceptance filters: one deciding whether
an existing occupant on a tile may be built over, one for crossing it while travelling along x,
one for crossing it while travelling along z `[STATIC]`. Their exact occupant sets are the crossing
lists of §5.3 `[INFERRED]`.

The on-ramp tool uses its own path builder (§7).

#### 6.2.2 Mouse protocol

`[STATIC]`

1. **Press**: the tile under the cursor becomes the drag start S (and the current end E = S).
2. **Move** (button held): E is updated to the tile under the cursor; the path S→E is rebuilt from
   scratch (§6.2.3) and the preview is redrawn (§6.2.5). The price of the path is computed and the
   tool records whether the city can afford it.
3. **Release**: the path is rebuilt one last time and committed:
   * if the path is marked invalid, the error sound (event 30) plays and nothing is built;
   * else if the price exceeds the city's funds, the "no money" sound (event 32) plays and nothing
     is built;
   * else the path is handed to the network manager (§6.2.6) and the build sound (event 14) plays.
4. Moving with the button up only moves the single-tile hover highlight (a path with S = E).

#### 6.2.3 Path construction

Coordinates are tile coordinates (x, z) as defined in `00-README.md` (Conventions). The builder walks from S toward E one tile per step,
choosing a step direction (dx, dz) ∈ {(±1,0), (0,±1)} before each step; it never makes a true
diagonal step `[STATIC]`. Let Δx = E.x − cur.x and Δz = E.z − cur.z before the step, and
sgn(v) = +1 if v ≥ 1, else −1.

1. **Initial direction**: if |Δz| < |Δx| move along x (dx = sgn Δx), otherwise along z
   (dz = sgn Δz). Ties (including S = E) choose z.
2. **Per step** (in order of precedence):
   1. If the current tile is not flat (a slope or rough tile) and the path is not already blocked,
      keep the current axis (only the sign is re-evaluated). The path cannot turn on a slope.
   2. Otherwise, for tools **without** Staircase (power, on-ramp) while the path is valid: keep the
      current direction, unless the path has already reached E's coordinate along the current axis,
      in which case re-choose by rule 4. This yields an L: first leg along the initial (major) axis,
      second leg along the other axis.
   3. Otherwise (Staircase tools, or any tool once the path is blocked): if |Δx| = |Δz|, alternate
      axes: if the previous step was along x, step along z (sgn Δz), else step along x (sgn Δx).
   4. Otherwise step along the major axis: x if |Δx| > |Δz|, else z.
   5. If Δx = Δz = 0 the walk ends.
3. **Shape that results** (Staircase tools): a straight leg along the major axis until the remaining
   offset is square, then a staircase that alternates z, x, z, x … (the first staircase step is along
   the axis *not* used by the straight leg). Example: S = (0,0), E = (10,4): x-steps to (6,0), then
   (6,1), (7,1), (7,2), (8,2), (8,3), (9,3), (9,4), (10,4). The staircase is what the rule tables
   turn into diagonal pieces.
4. **Bounds and limits**: the walk stays inside the map; a path is limited to 512 tiles in total
   (including the second carriageway of a highway) `[STATIC]`.
5. **Per-tile validation** (a tile failing any check marks the path *blocked* from that tile on;
   later tiles are still added to the preview but are shown as invalid):
   * the tile must be on the map; when AnySlope is *no* it must be flat or a valid slope;
   * an existing occupant on the tile must be accepted by the placement filter, or, failing that,
     by the crossing filter for the current axis (a *crossing*). When the tile is not flat, a
     crossing additionally requires the terrain to slope along the travel direction;
   * for MinRun > 0: a water tile is accepted only if the path has made at least MinRun consecutive
     straight steps immediately before it (counted from the last turn); a path may contain only one
     water crossing — the second entry into water blocks the path;
   * for the highway, after the path has moved sideways relative to its width (a turn), a crossing
     tile blocks the path.
6. Steps 2–5 repeat until E is reached or the 512-tile limit is hit.

`DIAG_Set.txt` is loaded at start-up but no part of the program queries it (unlike the six
network sets, which are exposed as membership queries) `[STATIC]`; diagonal appearance comes only
from the staircase routing plus the rule tables.

#### 6.2.4 Highway (wide) paths

For the highway each routed tile T is paired with a second tile T + w, where w is the width
offset perpendicular to the initial direction `[STATIC]`: moving along x uses w = (0, s) and moving
along z uses w = (s, 0), where s = +1 normally, flipped to −1 when the start lies on the last row or
column so that the pair stays on the map `[INFERRED]` (sign rule partly decoded). Both tiles of every pair are validated and added to the
path. If S = E, the hover preview shows the 2×2 block S, S + w and the matching pair.

#### 6.2.5 Preview and price

Every path tile is drawn in a highlight overlay with one of two states: *valid* or *invalid*
`[STATIC]`. After routing:

1. The whole tile list is submitted to the network manager's validation (§6.2.6 step 1), which
   returns how many leading tiles are buildable; all tiles after that count are set to *invalid*.
2. **Price** = (number of tiles in state *valid*) × (per-tile price of the tool's cost key)
   `[STATIC]`. Invalid tiles are free. Bridges, tunnels and terrain levelling are not included in
   this preview price (§6.4).
3. If the price is non-zero and greater than the city's current funds, **every** tile is set to
   *invalid* (whole path red) and the tool's "insufficient funds" flag is set `[STATIC]`.
4. The bottom-bar cost readout shows the price and the insufficient flag (`09-ui-screens.md`).

There are **no partial builds**: a drag the city cannot afford in full builds nothing (rejected at
release with the "no money" sound). The network manager repeats the funds check against the total
before building, and also refuses if the price exceeds funds `[STATIC]`.

#### 6.2.6 Commit (network manager)

`[STATIC]` unless stated:

1. Validate (SIMNTWRK 0x1000938B, also used by the preview) [STATIC]: the path is split into
   straight **runs**, broken at water, at turns (a turn tile belongs to both runs) and at a further
   per-tile test. Every run of ≥ 3 tiles (≥ 2 for the final run) is **strip-graded**
   (0x10008064 → 0x100077E7; the highway uses its own grader). If grading a run fails, the
   buildable prefix stays at the index before that run: that run and all later tiles are invalid
   (for a single straight drag the whole preview is red and nothing is built).
   * **Strip grade** (SIMDIRT 0x10007DC0) [STATIC; reconstruction checked against observations].
     Skipped when every tile of the run is already a valid slope (§6.3). startH = c0 of the first
     tile if it is flat (then s = 1), else trunc(mean of its 4 corners) (s = 0); endH likewise for
     the last tile (last = n − 1, minus 1 if the end tile is flat). The walk runs in increasing
     coordinate (startH/endH swapped for a drag in the negative direction). Along z the *main*
     vertex of step k is (x + 1, z0 + k) and its *mirror* (x, z0 + k); along x main (x0 + k, z),
     mirror (x0 + k, z + 1). For k = s … last + 1: lo = max(0, endH − 2 − 2(last − k)),
     hi = min(255, endH + 2 + 2(last − k)); at k = s also lo = max(lo, startH), hi = min(hi, startH),
     otherwise lo = max(lo, prev − 2), hi = min(hi, prev + 2); if hi < lo the run fails; the main
     vertex is clamped into [lo, hi] and becomes prev. Then each mirror vertex is set to its main
     vertex, and the surroundings are re-sloped ring by ring (|step| ≤ 4) as for the terrain
     tools. Cost = (RaiseTerrain × raised + LowerTerrain × lowered) >> 2 plus the ring costs.
   * **Permission**: for every vertex the grade changes, the up to four tiles sharing it are
     checked in all three layers (surface, pipe, subway) regardless of the network kind. A surface
     occupant passes if it reports kind 5 or passes the network manager's terrain-change filter and
     is not in the kind's ID list; pipe/subway occupants must pass the filter and not be in the
     list (the surface result overwrites rather than ANDs the flag). Any failure fails the run.
   * At commit the grade is applied and **charged** (terrain apply flag 0). Afterwards, for
     non-highway kinds, tiles the grade left invalid are re-graded one tile at a time (up to 100
     rounds).
   * [OBSERVED, Mount Herrang, road/rail/power/pipe/subway] A drag along +z over six tiles ending
     on a tile with corners 92/94/92/92 changes only the mirror corner (2,79) 92 → 94 (end tile
     becomes a clean slope, the tile before it flat) and adds a charge of 25·2 >> 2 = 12. The
     same drag one tile longer needs a raise at a corner of a water-tower tile and is rejected
     entirely (whole preview red, nothing built); the reverse drag (end to start) is also
     rejected.
2. Check funds ≥ total price; otherwise nothing is built.
3. Endpoint levelling [STATIC unless marked]. It is done once at the start of the path and once at
   the end. The end call is made only on the non-bridge branch, and only when no tunnel was
   built.
   * **Tiles.** A is the first (or last) path tile that will hold a piece. B is the next path
     tile in the drag direction (at the end: the previous one), but only for the highway. For
     every other kind, B = A, so a single tile is levelled. `[INFERRED]` For the highway, B is
     the second tile of the two-tile end.
   * **Occupant test.**
     * At the start, an occupant on A or B that is removable and has the tested flag set is
       removed, and the tile counts as empty. Any other occupant counts as present.
     * At the end, any occupant counts as present, and none is removed.
     * Levelling is skipped if A or B holds an occupant.
   * **Slope test.** Levelling is skipped if both A and B pass the valid-slope test of §6.3
     (flat, or a straight ramp of rise ≤ 2). A tile off the map fails the test.
   * **Level.** If both tiles are already flat, nothing happens. Otherwise the corner rectangle
     covering A and B is levelled with the terrain level operation (`11-world-terrain.md`):
     * 4 corners for one tile, 6 for the highway's two tiles.
     * Target height = (sum of the rectangle's perimeter corners + n/2) / n, with integer
       division (the average rounded half up). For one tile, (c0 + c1 + c2 + c3 + 2) / 4.
     * Every corner of the rectangle is set to the target, with no offset or clamp.
     * Surrounding corners are re-sloped outward ring by ring, as for the level tool, so that no
       step between neighbouring corners exceeds 4.
     * The level is refused if two consecutive perimeter corners differ by more than 4.
   * **Permission.** For every corner the operation would change, each of the up to four tiles
     sharing the corner is checked in the layer of the network kind: surface for road, rail,
     power and highway; pipe layer; subway layer. An empty tile passes. An occupant passes only
     if it is in the network manager's terrain-change filter set (filter 0xA1C085DB, entry 10).
     `[INFERRED]` That set holds network pieces and flora. If any tile fails, the whole levelling
     is dropped: no corner changes, no message, and the network is still built on the unchanged
     terrain.
   * **Cost.** The terrain operation computes a cost, but the network manager charges nothing
     for it.
   * **Not yet decoded.** One more call argument, and the exact flag tested at the start-side
     occupant removal.
4. Bridge attempt (road, rail, power, highway only; path length ≥ 4 tiles): §6.4.
5. Tunnel attempt (road, rail, and highway): §6.4. [STATIC, SIMNTWRK 0x10014A23] The actual
   commit order is: start-endpoint levelling; bridge attempt; validate and strip-grade the path
   (step 1); tunnel attempt, made whenever no bridge was built (not gated on grading); end-point
   levelling only if no tunnel was built.
6a. Neighbour-connection offer for path ends on the map edge: §8.
6. Place pieces: the tile list goes to the tiler (§4.6). For the highway, a second tiler pass is
   run for road (net 1) over the tiles where the new highway meets existing roads, to create the
   on/off-ramp and road-under-highway pieces (§7).
7. Charge the price through the budget (`19-sim-economy.md`) `[INFERRED]`.

### 6.3 Slopes

A tile's four corner heights are taken in the order c0 = (x, z), c1 = (x+1, z),
c2 = (x+1, z+1), c3 = (x, z+1), in terrain height units (`11-world-terrain.md`). Let
d1 = c1 − c0, d2 = c2 − c0, d3 = c3 − c0 `[STATIC]`.

* **Flat**: d1 = d2 = d3 = 0. Only flat tiles are evaluated by the rule passes.
* **Valid slope** (for routing): either (c1 = c0 and c3 = c2 and |c2 − c0| ≤ 2) or
  (c2 = c1 and c3 = c0 and |c2 − c0| ≤ 2).
* **Slope piece**: the pattern (d1, d2, d3) is matched against this table; the first match gives
  the pattern index p:

| p | (d1, d2, d3) | p | (d1, d2, d3) |
|---|--------------|---|--------------|
| 0 | (−1, −1, 0) | 4 | (−2, −2, 0) |
| 1 | (0, −1, −1) | 5 | (0, −2, −2) |
| 2 | (+1, +1, 0) | 6 | (+2, +2, 0) |
| 3 | (0, +1, +1) | 7 | (0, +2, +2) |

Hard-coded slope pieces by p (occupant ID / rotation) `[STATIC]`:

| Network | p0 | p1 | p2 | p3 | p4 | p5 | p6 | p7 |
|---------|----|----|----|----|----|----|----|----|
| road | 11202/0 | 11201/0 | 11202/2 | 11201/2 | 32/0 | 31/0 | 32/2 | 31/2 |
| rail | 16008/0 | 16009/0 | 16008/2 | 16009/2 | 46/0 | 47/0 | 46/2 | 47/2 |
| subway | 11616/0 | 11617/0 | 11616/2 | 11617/2 | 11614/0 | 11615/0 | 11614/2 | 11615/2 |
| pipe | 11650/0 | 11649/0 | 11650/2 | 11649/2 | 11648/0 | 11647/0 | 11648/2 | 11647/2 |
| power (wire) | 11262/0 | 11262/1 | 11262/2 | 11262/3 | 11263/0 | 11263/1 | 11263/2 | 11263/3 |
| power (pylon) | 18081/0 | 18080/0 | 18081/2 | 18080/2 | 18083/0 | 18082/0 | 18083/2 | 18082/2 |
| highway (A) | 15102/0 | 15105/0 | 15104/2 | 15103/2 | 14094/0 | 15101/0 | 15100/2 | 15099/2 |
| highway (B) | 15104/0 | 15103/0 | 15102/2 | 15105/2 | 15100/0 | 15099/0 | 14094/2 | 15101/2 |

* Power: the pylon row is used when (x + z) is a multiple of 5, or when the drawn path does not
  contain both neighbours of the tile along the slope direction (for even p the tiles (x±1, z),
  for odd p the tiles (x, z±1)); otherwise the wire row.
* Highway: row A is used when the drawn path contains the tile one step across the carriageway
  (for even p (x, z+1), for odd p (x+1, z)); otherwise row B.

### 6.4 Bridges and tunnels

Bridges `[STATIC]` unless stated. They exist for road, rail, power line and highway; pipes and
subways have no bridge records and simply pass under water. A bridge is attempted only when the
committed path has at least 4 tiles and the network's Bridges table is non-empty.

**1. Locating the water span.** Walk the path in order and classify each tile as water or land
with the terrain water test (`11-world-terrain.md`): a tile is water if any of its corners has
water ≥ ground [STATIC]. Shore tiles with some corners submerged are part of the span. DATA example:
in Roadless Paradise, a highway drag along x = 39/40 from z = 159 meets water at z = 164. Tiles
164..172 are water by this test, and z = 173 is land (E0). Tiles 174..178 are water again and so
form a second crossing.

* The span starts at the first water tile; the walk stops at the first land tile after it. If no
  land tile follows the water, or if any water tile in the path already holds an occupant, no
  bridge is built (the path is then tiled normally and the water tiles are rejected).
* For the span tiles, read the terrain height of each tile's first corner (x, z); let **h_w** be the
  smallest of them (heights in terrain units, `11-world-terrain.md`; a corner off the map reads
  as 23).
* Let the bounding box of the span tiles be [x0..x1] × [z0..z1]. The span runs along **z** (axis
  1) when x1 − x0 < 2, z1 ≠ z0, and either z1 − z0 > 1 or x1 = x0 or the second tile changed x;
  otherwise along **x** (axis 0). **W** (span length) = (extent along the chosen axis) + 1 tiles.
  Let a be the unit step along the axis ((1,0) or (0,1)), p the perpendicular unit step, and W0 the
  span tile with the smallest coordinates.

**2. Choosing the design.** Scan the network's Bridges records in file order and take the first
whose `axis` field equals the span axis and whose `n` field is ≤ W `[STATIC]` (this confirms `n`
as the minimum span length of the design). Highway records are selected by direction instead of
length: two passes are made, one per carriageway; pass 1 uses direction 0 for an x span and 3 for
a z span, pass 2 uses 2 for x and 1 for z. If no record matches, no bridge is built.

Shipped designs `[DATA]` (W ranges follow from the selection rule):

| Network | Axis | n | Body length k | Applies to W |
|---------|------|---|---------------|--------------|
| road | each | 18 | 9 | ≥ 18 |
| road | each | 10 | 5 | 10–17 |
| road | each | 6 | 6 | 6–9 |
| road | each | 1 | 2 | 1–5 |
| rail | each | 1 | 2 | ≥ 1 |
| power | each | 1 | 6 | ≥ 1 |
| highway | dir 0–3 | 1 | 3 | ≥ 1 |

**3. Price and confirmation.** Price = W × cost of agent type **16002** for road, rail and power
bridges, and W × cost of agent type **16004** for highway bridges (charged once, on the first
carriageway pass) `[STATIC]`. Agent type 16003 is not used by the bridge builder. If the price
exceeds the city's funds, the "insufficient funds" message (string table `029541F4`, instance
`0x2E0`) is shown and nothing is built. Otherwise the player is asked to confirm with a yes/no
dialog titled by instance `0x252` whose text is instance `0x28B` (road, rail, highway) or `0x2D2`
(power) with the price substituted. Declining cancels the whole drag. The bridge price is separate
from, and in addition to, the per-tile drag price of §6.2.5.

**4. Fitting.** With body length k (the third segment of the record):

* repeats m = ⌊W / k⌋ (0 if k > W; at most 256);
* remainder r = W − m·k; left filler count = ⌊r / 2⌋, right filler count = r − ⌊r / 2⌋.

**5. Abutment terrain.** Constants: deck clearance D = 5 and shore step R = 3 (power lines:
D = R = 1). For each bank a *bank height* B is computed from the terrain corners 3 tiles outside
the span (start side: corners W0 − 3a and W0 − 3a + p; far side: E0 + 3a and E0 + 3a + p, where
E0 = W0 + W·a): B = the average of the two corner heights (default 20 at the start, h_w + 1 at
the far end, if a corner is off the map), then clamped into [h_w + D − 4, h_w + D + 4]. The
following terrain corners are then set (start side shown; the far side mirrors it, using E0 and +a):

| Corners | New height |
|---------|------------|
| W0 − 3a, W0 − 3a + p, W0 − 4a, W0 − 4a + p | B (start side: see the defect below) |
| W0 − a, W0 − a + p | h_w + D |
| W0, W0 + p | h_w + R |

**Start-bank defect** [STATIC, checked in the disassembly at 0x10001744, 0x100018D1 and
0x10001A98]. The start-side B is stored in a one-byte variable, and the permission checks use it.
The first far-bank corner read, h(E0 + 3a), then writes into the same byte. As a result, the
start-side corners in the B row are **set** to the raw, unaveraged, unclamped height of corner
E0 + 3a, or 23 if that corner is off the map. The far side's own B is not affected. The highway
builder has the same defect (0x10002D69, 0x10002FB2, then the start-side sets from 0x10003241).
The W0 − 4a approach altitude uses the same byte.

Every corner change is first checked: the 8 start-bank and 8 far-bank corners are checked before
anything is written; a corner that already has its target height is not checked. A corner may
change only if none of the up to four tiles that share it (in the surface, pipe and subway layers)
holds an occupant that forbids terrain changes (an occupant passes if it reports kind 5, or is in
the network's terrain-change set and not in the kind's exclusion list). If one of these 16 checks
fails, nothing is changed, the "construction discontinued" message (instance `0x285`) is shown
and the drag is cancelled.

**Order and partial failure (road, rail, power)** [STATIC, SIMNTWRK 0x1000122A]: (1) the 16
checks; (2) the start-bank corners are **written immediately**; (3) the start approach is graded
with the strip grader of §6.2.6 on the vertex list (W0 − 4a, B), (W0 − 3a, follow), (W0 − 2a,
follow), (W0 − a, h_w + 5), whose permission test covers the whole affected block; (4) the river
bed is lowered, the far-bank corners written and the far approach graded; (5) the pieces are
placed and the price charged. If step 3 or 4 fails, `0x285` is shown and the drag is cancelled
**without undo and without charge**: the start-bank corners written in step 2 stay. [OBSERVED,
Roadless Paradise, road (36,155)→(36,180), W = 8, price §800 accepted] No piece was built, funds
were unchanged, and corners (36,162)/(37,162) were left at 85 (h_w + 5) and (36,163)/(37,163) at
83 (h_w + 3) with h_w = 80; the approach grade reached the 2 × 2 train station at
(37..38, 158..159).

**Preview of water tiles** [STATIC, drag path builder SIMNTWRK 0x1000488D]. For road, rail and
power a water tile is shown valid only if the path is still valid and the straight run before
it has at least p8 = 3 tiles; only one water crossing is allowed per drag. After the water a
countdown of 3 land tiles starts; the water tiles (and the countdown tiles) stay pending until a
valid land tile is reached after the countdown. If the drag ends (or becomes invalid) first,
every pending tile is set invalid, and validation keeps only the leading buildable tiles.
[OBSERVED] Road (34,158)→(34,175) on Roadless Paradise ends two land tiles past the water: the
water tiles were red, only the four approach tiles were built, no bridge was offered, and the
full 18-tile price (§180) was charged.

**5a. Highway abutments** [STATIC]. The highway builder runs two passes, one per carriageway.
Record direction: pass 0 uses dir 0 for an x axis and 3 for a z axis; pass 1 uses 2 and 1. A
record is chosen by direction only; its length field is not compared. The price is the cost of
agent type 16004 × W, asked once in pass 0 (prompt instance `0x28B`; not enough funds `0x2E0`).
Differences from the road/rail/power builder:

* h_w = the lowest first corner over both carriageway tiles of every water tile.
* Each corner row has three corners across the carriageway (+0, +p, +2p) instead of two.
* Bank height B = ⌊(h(W0 − 3a) + h(W0 − 3a + p) + h(W0 − 3a + 2p)) / 3⌋. It defaults to 20 if a
  read fails. B is then raised to at least h_w − 2 and lowered to at most h_w + 6. Correction:
  the previous revision said there is no upper clamp. The far bank uses E0 + 3a, +p, +2p, with
  the same clamp, and defaults to the clamped start B.
* Corners set (start side; the far side mirrors with E0 and +a):

  | Corners | New height |
  |---|---|
  | W0 − 3a, W0 − 4a, each at +0, +p, +2p | B |
  | W0 − a, each at +0, +p, +2p | h_w + 2 |
  | W0, each at +0, +p, +2p | h_w + 2 |

* River bed: for k = 1 … W − 1, the corners W0 + k·a, +p and +2p are lowered to h_w where they are
  higher. The road builder lowers +0 and +p only.
* Order: check all 12 start and 12 far corners; set the start corners; lay the start approach;
  lower the river bed; set the far corners; lay the far approach; place the deck. Any failed check
  shows `0x285` and cancels before anything changes.
* Approach tiles (altitude 0 = follow the terrain):

  | Tiles | Altitude |
  |---|---|
  | W0 − 4a and W0 − 4a + p | B |
  | W0 − 3a, W0 − 2a, each at +0 and +p | 0 |
  | W0 − a and W0 − a + p | h_w + 2 |
  | Far side: E0 + 3a; E0 + 2a, E0 + a; E0, each at +0 and +p | B_far; 0; h_w + 2 |

  Unlike the road builder, it places no slope or flat pieces on the approach tiles itself.
* Deck: start ramp at W0 − a and end ramp at E0, both at altitude h_w + 2 (road: +5 / +7). The
  fillers and body are at h_w. Pass 0 pieces go in row W0, pass 1 pieces in row W0 + p.
* Shipped `HWAY_GRND_Bridges.txt` records (id, rotation); every record has n = 1 and k = 3:

  | dir | start ramp | filler | body | end ramp |
  |---|---|---|---|---|
  | 0 | 16055 r0 | 16045 r0 | 16045 r0, 16043 r2, 16045 r0 | 16054 r2 |
  | 2 | 16054 r0 | 16045 r2 | 16045 r2, 16043 r0, 16045 r2 | 16055 r2 |
  | 3 | 16046 r2 | 16044 r2 | 16044 r2, 16042 r2, 16044 r2 | 16047 r0 |
  | 1 | 16047 r2 | 16044 r0 | 16044 r0, 16042 r0, 16044 r0 | 16046 r0 |

* The highway builder never sets the pier map flag, so 16042 and 16043 in the pier list are
  never flagged by highway bridges.
* Defects [STATIC, checked in the disassembly]:
  * The far-bank average reads three corners, but the second and third reads write the same byte
    (0x10002FD8, 0x10002FFB) and the sum uses the start side's third byte. So the far
    B = ⌊(h(E0 + 3a) + h(E0 + 3a + 2p) + h(W0 − 3a + 2p)) / 3⌋, clamped as above.
  * The start-bank defect of the road builder (step 5) applies too. The start-side B corners and
    the W0 − 4a approach altitude get the raw h(E0 + 3a).

**6. Pieces.** Placed along the axis, with occupant altitude given in terrain height units
(segments are the record's five segments; only the first piece of segments 1, 2, 4 and 5 is used):

| Position | Piece | Altitude |
|----------|-------|----------|
| W0 − 3a, W0 − 2a | the network's normal slope piece for that tile (§6.3), or if the tile is not a valid slope the flat straight piece (road 29, rail 44, power 92) with rotation = axis | terrain |
| W0 − a | segment 1 (start ramp) | h_w + 5 for axis 0, h_w + 7 for axis 1 (power: h_w + 1) |
| next ⌊r/2⌋ tiles | segment 2 (filler) | h_w |
| next m·k tiles | segment 3 pieces in order, repeated m times | h_w |
| next r − ⌊r/2⌋ tiles | segment 4 (filler) | h_w |
| E0 | segment 5 (end ramp) | as the start ramp |
| E0 + a, E0 + 2a | as W0 − 3a / W0 − 2a | terrain |

Body pieces whose occupant ID is one of 83, 87, 90, 91, 11265, 11275, 11300, 16042, 16043, 16049,
16061, 16062, 18057, 18058, 18067, 18068 also set a per-tile map flag at their tile, cleared again
when such a piece is demolished `[STATIC]`. `[INFERRED]` it marks bridge piers (tiles boats cannot
pass); see `26-automata.md`.

**7. Maximum span.** There is no explicit maximum: the span is bounded only by the 512-tile path
limit (§6.2.3), the 256-repeat cap, and the routing rules (straight run of MinRun = 3 tiles before
the water, one water crossing per drag) `[STATIC]`.

Tunnels (agent type 16006, cost key 16006) `[STATIC]`:

1. Road and rail tunnel with the builder below. Highways also have a tunnel builder (see the end
   of this list); an earlier revision said only road and rail can tunnel.
2. The direction comes from the **last step** of the drag only: d = last tile − previous tile,
   which must be a unit axis step. Travel direction d: 0 = −x, 1 = −z, 2 = +x, 3 = +z (the span
   direction encoding of `18-sim-transport.md` §5.5 and `10-save-format.md` §6.2.6).
3. The entrance tile E is the **last dragged tile** [STATIC, SIMNTWRK 0x1001C102]. With its
   corners c0 = (x, z), c1 = (x+1, z), c2 = (x+1, z+1), c3 = (x, z+1) (heights after the strip
   grade, which runs first), its rise along d is (c1 − c0) + (c2 − c3) for +x,
   (c0 − c1) + (c3 − c2) for −x, (c3 − c0) + (c2 − c1) for +z, (c0 − c3) + (c1 − c2) for −z. If the
   rise is < 4 there is no tunnel. Let h = the lowest corner height of E (threshold T = (h + 4)
   as a byte). The start tile and tiles in the middle of the drag are never examined.
   [OBSERVED] A road drag on Mount Herrang over a hill from (55,90) to (80,90), ending on a tile
   that falls along +x, built 26 terrain-following pieces and no tunnel prompt.
4. **Walk.** Start with L = 1 and P = E + d. Repeat: step to P + d; if that tile is off the map,
   stop with **no tunnel**. Otherwise P ← that tile and L ← L + 1, and continue while the lowest
   corner height of P is ≥ h + 4. The walk ends at the first tile whose lowest corner is below
   h + 4; that tile is the exit X = E + L·d. If L < 3, no tunnel is built. The exit therefore
   always lies beyond the dragged end; the walk alone chooses it. On success the last path tile
   is replaced by X + d, so the network continues one tile past the exit. Portal shaping: E's
   hill-side corners → h + 4 and approach side → h; X's hill-side corners → h + 4 and far side →
   h; the two far corners of X + d → h.
5. **Price** = (L − 1) × the cost of agent type 16006, charged per tunnel tile. Correction: the
   previous revision guessed once per tunnel. It must not exceed the city's funds, and the player
   confirms with a yes/no prompt (string table `029541F4`, instance `0x253`, price substituted).
   If shaping the portal terrain fails after confirmation, message instance `0x284` is shown and
   nothing is placed.
6. **Pieces.** Only **two** pieces are placed: one portal at E and one at X. Nothing is placed on
   the tiles between them. The entrance gets the span record (direction d, length L). The exit
   gets the span record (direction (d + 2) mod 4, length L). Both pieces therefore point at each
   other and the length is exactly the tile distance between them. Portal piece (occupant ID,
   rotation):

   | Network | d = 0 | d = 1 | d = 2 | d = 3 |
   |---|---|---|---|---|
   | road | 64, rot 0 | 63, rot 0 | 64, rot 2 | 63, rot 2 |
   | rail | 18024, rot 0 | 18019, rot 0 | 18024, rot 2 | 18019, rot 2 |

   The exit uses the column for (d + 2) mod 4. DATA: every span pair in the shipped files matches.
   For example, Mount Herrang has road piece 64 at x 56 (dir 2, len 23) and at x 79 (dir 0,
   len 23). Trips jump between the portals using the span (`18-sim-transport.md` §5.5).
7. **Highway tunnels** [STATIC]. Same straight-drag rule as road/rail, for network type highway
   only, checked across the two-tile carriageway width. Minimum walk count 3. The walk threshold
   is **h + 8** (road/rail use h + 4). Price = 2 × walk_count × cost of agent type **16006** — the
   same cost key as road/rail tunnels (shipped `[BuildingPlacementCosts] 16006=1000`); there is no
   separate highway-tunnel cost key. Confirm prompt is the same instance `0x253`. Terrain-shape
   failure after confirm still shows `0x284` and places nothing.
   Each end gets **four** pieces: two without a span record and two carrying a span. Both span
   pieces of one end share the travel direction (entrance) or its opposite (exit), with
   length = walk_count + 1 [STATIC; was INFERRED]. Correction: earlier text listed span IDs
   18025–18028; the tables are **18027–18030**. Non-span IDs **18088–18091** were already right.
   Packed as `(occupant_id << 8) | rotation`. By travel direction d (0 = −x, 1 = −z, 2 = +x, 3 = +z):

   | d | span A | span B | plain A | plain B |
   |---|---|---|---|---|
   | 0 | 18030, rot 0 | 18028, rot 0 | 18091, rot 0 | 18089, rot 0 |
   | 1 | 18029, rot 0 | 18027, rot 0 | 18090, rot 0 | 18088, rot 0 |
   | 2 | 18030, rot 2 | 18028, rot 2 | 18091, rot 2 | 18089, rot 2 |
   | 3 | 18029, rot 2 | 18027, rot 2 | 18090, rot 2 | 18088, rot 2 |

   Exit end uses the row for (d + 2) mod 4. Placement order at each end: plain A, plain B, span A,
   span B. The two plain pieces are placed with no span; the two span pieces get direction and
   length as above.

   **Tile offsets within each portal** `[STATIC]`. Each end occupies a **2×2** block: two tiles
   along the travel axis (approach + hillward) by the two carriageway tiles. Let P = (xₚ, zₚ) be
   the max-x, max-z corner of that 2×2. Let δ be the piece direction written into the span
   records at that end (entrance δ = d; exit δ = (d + 2) mod 4). Offsets from P:

   | δ | plain A | plain B | span A | span B |
   |---|---|---|---|---|
   | 0 (−x) | (0, 0) | (0, −1) | (−1, 0) | (−1, −1) |
   | 1 (−z) | (−1, 0) | (0, 0) | (−1, −1) | (0, −1) |
   | 2 (+x) | (−1, −1) | (−1, 0) | (0, −1) | (0, 0) |
   | 3 (+z) | (0, −1) | (−1, −1) | (0, 0) | (−1, 0) |

   Equivalently: span pieces sit on the forward edge of the 2×2 (the edge faced by δ); plain
   pieces sit on the opposite (approach) edge. On each edge, A is the right-hand tile and B the
   left-hand tile when facing δ in the x/z plane (+x / +z). Matched entrance/exit span tiles lie
   `walk_count + 1` steps apart along the travel axis (the span length). No shipped file contains
   a highway tunnel.
8. Bridges never carry span records. Their decks are ordinary pieces on every tile (above).

### 6.5 Demolition

Bulldozing a network tile removes its piece and re-runs the tiler with the bulldozed tiles as the
path (§4.6), so neighbours are re-tiled into dead ends/straights `[INFERRED]`. Destroying one piece
of a multi-piece structure listed in `Collapse.txt` removes the whole group (§4.4) `[INFERRED]`.

## 7. Highways, on-ramps, stations

**Highway drags.** After the highway pieces of a drag have been tiled, the tiler is run a second
time for the road network over the road tiles adjoining the new highway, so roads that meet the
highway are re-tiled into road-under-highway pieces (§5.3, occupant 11258/15098) `[STATIC]`.

**On-ramp tool** (menu item `02B5DEBB`, §6.2.1). It is a point tool: dragging is ignored and only the
tile under the cursor (A) matters `[STATIC]`:

1. Candidate anchors are tried in this order: A + (0,+1), A + (+1,0), A + (0,−1), A + (−1,0), A.
   For each anchor, two orientations are tried: orientation 0 (ramp footprint is the anchor plus
   its two neighbours at ±1 in z; adjacent highway piece 73, underpass piece 11258) then
   orientation 1 (footprint along x: ±1 in x; pieces 15051 and 15098).
2. A candidate fits when all three footprint tiles are on the map and buildable (accepted by the
   ramp tool's placement filter), the highway tiles diagonally beyond the two ends of the footprint
   on the highway side hold the expected highway piece (one end: the highway piece; other end: the
   highway piece or the underpass piece), and the tile beyond at least one end of the footprint is
   accepted by the tool's road filter (the ramp must meet a road) `[STATIC]`.
3. The first fitting candidate is placed as a 3-tile lot through the general lot-placement service
   (`13-buildings-occupants.md`); its occupant is the on-ramp structure (agent type 17202
   "OffRamp") `[INFERRED]`.
4. If no orthogonal candidate fits, a diagonal ramp is tried in each of the four quadrants around A:
   it requires the diagonal neighbour to hold a highway curve piece (occupant 15111, 15112, 15113 or
   15114) and the two tiles beyond it to hold highway pieces (73 or 15051) `[STATIC]`.
5. Price: the tool's per-tile cost key is 16001 (highway), charged per valid preview tile
   (§6.2.5) `[STATIC]`; `[OPEN]` whether the 17202 entry of `[BuildingPlacementCosts]` is used
   instead by the lot-placement service.

On-ramp and road-under-highway pieces are protected (§4.4) `[DATA]`. Rail stations, subway
stations and subway-to-rail stations are buildings (agent types 12230, 12360, 12361) whose
occupants carry network flags so that they join the rail/subway networks; see
`18-sim-transport.md`.

## 8. Neighbour connections

Connections are stored by the neighbour manager as a set of edge tiles per network kind, with the
enumeration 1 = road, 2 = rail, 3 = power, 4 = highway, 5 = pipe (water), 6 = subway `[STATIC]`
(persistence: `10-save-format.md`, neighbours record). A connection is accepted only on a map-edge
tile; the edge decides which of the four neighbours it counts for: x = 0 → neighbour 0 (except
tile (0, H − 1) → 3), z = 0 → 1, x = W − 1 → 2, z = H − 1 → 3 `[STATIC]`.

**Creation trigger** `[STATIC]`. Connections are created only by a network drag being committed
(§6.2.6), after the funds check and before tiling:

1. Non-highway networks (road, rail, power, pipe, subway): each of the two **ends** of the drag
   path (first and last tile; once if they coincide) is examined. If the end tile lies on the map
   edge (x = 0, z = 0, x = W − 1 or z = H − 1), a neighbour city exists on that edge, no
   connection of this kind exists at the tile yet, and no connection has been made by this drag
   yet, the player is offered a connection. Tiles in the middle of a drag that runs along the edge
   do not create connections.
2. Highway: the first up to 4 path tiles from the start, then the first up to 4 from the end, are
   scanned for edge tiles; up to two edge tiles (the two carriageways) are collected. If neither
   has a highway connection and the neighbour exists, a single offer covers both tiles.
3. **Price** = the `[BuildingPlacementCosts]` entry of the connection agent type: 16017 road,
   17006 rail, 12102 power, 17207 highway, 17310 pipe, 17410 subway (each inherits from its
   network's type, `CitySim.ini [AgentTypeHierarchy]`). If the price is non-zero and the funds are
   less than (drag price + connection price), the insufficient-funds message (`029541F4:0x2E0`)
   is shown and no connection is made (the drag itself is still built).
4. **Offer**: a yes/no dialog titled `029541F4:0x252` with text `029541F4:0x251` and the price
   substituted. On *yes*: the connection(s) are added for the tile(s), the price is charged
   immediately, and a confirmation sound plays (event 36 road/highway, 37 rail/subway, 46 power,
   45 pipe).
5. The connection is not a separate occupant: the edge tile keeps an ordinary network piece, which
   the tiler shapes as if the network continued off the map (§4.6). Agent types 16017 … 17410 act
   as price keys `[STATIC]`.

**Removal** `[STATIC]`. When a network piece on a tile is bulldozed, every connection of every kind
at that tile is removed (queried and removed for kinds 1, 4, 2, 6, 5, 3 in that order).

**Notification**: adding or removing a connection triggers no separate event `[STATIC]`; deal availability is recomputed from the connection counts
(`19-sim-economy.md`).

Topological effects:

* Tiler: an edge tile carrying a connection of the network being tiled behaves as if the network
  continued off the map on that side (§4.6, mask bits 0–3; complex pass off-map entries rot 1).
* Power: a power-conducting occupant placed on an edge tile that carries a power connection
  registers an external power feed for that side (up to 4 feeds); a feed is used by the network
  that reaches its tile (§9.1).

## 9. Power and water propagation

Both utilities use 1-bit-per-tile grids and the same bounded flood fill `[STATIC]`. Capacity
values, consumption and failure semantics belong to `16-sim-utilities.md`.

**Bounded flood fill.** Given a seed tile and a *conductor* grid, the reached set starts as the
seed. Each iteration spreads the reached set by one tile horizontally (±1 in x) restricted to
conductor tiles, then by one tile vertically (±1 in z) restricted to conductor tiles. Iteration
stops when nothing changes or after a fixed maximum number of iterations: **600** for power,
**500** for water. A very long winding line can therefore fail to carry beyond the cap.

### 9.1 Power

1. **Conductor grid.** Every occupant that is a building (agent type 100 or descendant) or a
   power-line piece (agent type 17100 or descendant) and is not damaged (agent type 15400) marks
   as conductor its footprint rectangle **expanded by 2 tiles on every side**, clamped to the map.
   So buildings and lines conduct to anything within 2 tiles of their footprint `[STATIC]`.
   **Zoned land conducts too** `[STATIC]`: every tile whose zone type is neither 0 (unzoned)
   nor 17 (landfill) marks the same expanded rectangle (the tile ±2). The zone types that count
   include airport, seaport and zone 22 (land under a plopped building). When a rectangle is
   zoned, its expanded rectangle is set. When it is de-zoned (to 0) or zoned as landfill, the
   expanded rectangle is cleared and rebuilt tile by tile from the remaining buildings, lines
   and zoned tiles. Empty zoned land is therefore powered whenever it belongs to a powered
   component; it has no consumption of its own. DATA: London's 112 empty airport tiles, 78 of
   which are more than 2 tiles from any building or line, are all reported powered (result
   code 9, `15-sim-growth.md` §4.3), not unpowered (code 14). Correction: the previous text
   listed only buildings and lines.
2. **Demand grid.** A per-tile byte grid. Each conducting occupant writes its power consumption
   (one byte, 0–255) into the cell of its **anchor tile only**; the other tiles of its footprint keep
   0 `[STATIC]`. The byte is the occupant's **own building record** (the occupant-attribute record whose key the occupant entry stores, `10-save-format.md` §6.2), u8 attribute **0xC9**; 0 when the record has no 0xC9 `[STATIC]`, taken while the lot is occupied (state 1/513); for other lot states a substitute record supplies it and it is effectively 0, so only occupied lots consume. See `16-sim-utilities.md` §2.6 for the data check. `16-sim-utilities.md` §2.6
   uses this anchor-only grid.
3. For each power plant not yet visited this cycle: flood-fill from the plant's tile; every plant
   whose tile lies in the reached set is marked visited and its capacity added to the network's
   supply; every edge feed (§8) whose tile is reached adds its supply. The power-conservation
   ordinances reduce consumption (`16-sim-utilities.md`).
4. **Allocation order**: the reached set is scanned in order of ascending z and, for equal z, ascending x.
   Tiles with zero demand are marked powered. A tile with demand d is marked powered **first**, then
   d is compared with the remaining supply: if d ≤ remaining, d is subtracted; otherwise remaining
   becomes 0, the tile **stays powered**, and every later tile in scan order is left unpowered
   `[STATIC]`. This agrees with `16-sim-utilities.md` §2.7. (Tiles are handled in groups of 32 along x;
   the tiles that follow the boundary tile inside its group of 32 are also left unpowered, which is
   the same as "every later tile".)

### 9.2 Water

1. **Conductor grid** `[STATIC]`: when an occupant carrying the pipe flag 0x800000 (§5.2) is
   added, every tile of its footprint rectangle is marked as conductor; when it is removed the
   bits are cleared. There is **no** expansion (unlike power's 2-tile margin) and ordinary
   buildings do **not** conduct water: only pipe-layer pieces form the water network. A water
   facility joins a network only through the flood seeded at its own anchor tile, so it must be
   reached by a pipe piece (`[INFERRED]` the facility tile itself must touch a pipe tile within one
   flood step).
2. For each water source (pump, tower, desalination plant) with non-zero output not yet visited
   this cycle: seed its anchor tile and run the bounded flood fill (cap 500 iterations) over the
   conductor grid; sources inside the reached set are marked visited and their outputs summed.
3. **Coverage**: the reached pipe tiles are dilated `WaterFromPipe` times (key in
   `SC3WaterLayer.ini [TuningParameters]`, tiles; value 0 is replaced by 3; shipped value 7). Each
   dilation step grows by one tile in x then one tile in z, so the coverage is every tile within
   Chebyshev distance `WaterFromPipe` of a reached pipe tile `[STATIC]`.
4. **Allocation order under shortage** (owned by `16-sim-utilities.md` §3.4) `[STATIC]`: unlike
   power's row-major scan, water is handed out by an expanding front that starts at the source
   tiles and grows one tile per sweep through the covered area, alternating a top-to-bottom and a
   bottom-to-top row sweep. Each building is charged its consumption when the front first reaches
   it; when the budget is exhausted the spread stops, so buildings farthest from the sources (in
   front steps) go dry first. Exact tie-breaking inside a sweep: `[OPEN]`.

## 10. Ageing / upkeep

Network pieces have no age; nothing ages per piece and no age data is persisted (see Persisted
state). Monthly upkeep of roads/highways is driven by the transport funding keys (`STTraffic.ini`
`OptimalMonthlyFundingCostPerRoad`, `OptimalMonthlyFundingCostPerHighway`) and the budget; see
`18-sim-transport.md` and `19-sim-economy.md`.

**Road condition (potholes)** `[STATIC]`. Road wear is held only as the number of damaged
(potholed) road pieces on the map; potholed roads are agent type 16016 `[DATA]`.

* **Condition** X = (number of damaged road pieces) ÷ (number of road tiles), a fraction 0–1. This
  is the value `18-sim-transport.md` §8.4 reads, and also the transportation advisor's "damage"
  (`22-advisors-news-petitioners.md` §3.10.1) [STATIC]. Exact terms [STATIC]:
  * Numerator: count every occupant on the whole map that is a road piece (§5.2: flags 0x8000 and
    0x800) and whose occupant ID is 15040, 15041, 15042 or 9051. It is recounted each time X is
    asked for.
  * Denominator: the network's running count of road pieces, meaning occupants carrying flag
    0x8000. It goes up by one when such an occupant is added and down by one when it is removed.
    Highway, rail, subway and power-line pieces are kept in their own counters (flags 0x10000,
    0x20000, 0x40000, 0x400000). Pipes are counted elsewhere. The road count is the first of the
    two road counts the transportation advisor reads.
  * The division is a real division (extended precision), and the result is returned as a real.
    With no road pieces it is 0 ÷ 0. The original does not guard this, so the result is NaN
    (IEEE not-a-number; the original's floating-point exceptions are masked). See
    `22-advisors-news-petitioners.md` §3.10.1 for how a NaN behaves in the advisor tests.
* **Setting a new condition** v (0–1, from `18-sim-transport.md` §8.4; it arrives as a whole
  percentage and is divided by 100) [STATIC]:
  1. Scan every tile of the map. N = tiles whose surface-layer piece is a road piece. D = tiles
     whose surface-layer piece is potholed (road piece with ID 15040, 15041, 15042 or 9051).
     T = trunc(N × v), computed in floating point. If T = D, stop.
  2. **Damage (T > D).** k = T − D, s = max(0, ⌊N ÷ k⌋ − 1). Query all road pieces of the map, in
     the occupant query's order (`map order`). Keep a skip counter c = 0 and a gap
     g = `Rand(s)` (the network subsystem's generator, `16-sim-utilities.md` §1.1; g = 0 if
     s < 1). For each piece in order:
     * if c < g: c ← c + 1 (the piece is skipped; every road piece counts here, candidate or not);
     * otherwise, if the piece is a **candidate**, collect it, draw a new g the same way and set
       c ← 0. A non-candidate is just passed; c stays as it is;
     * stop the walk once **k + 1** pieces have been collected (original off-by-one), or at the end
       of the list.

     A candidate is a road piece (flags 0x8000 and 0x800) whose occupant ID is **29, 35 or 11204**
     and that passes the occupant's own validity check [INFERRED: true for every placed piece].
     Then, for each collected piece in collection order:
     * ID 29 → draw r = raw generator output mod 3 (the same value as `Rand(3)`). The new ID is
       15040, 15041 or 15042 for r = 0, 1, 2;
     * ID 35 → new ID 9051 (no draw);
     * ID 11204 → nothing. It is collected and counts towards k + 1, but it is not converted
       (original defect).

     The piece is replaced in place: the old one is removed, and a new piece with the new ID is
     created at the same placement and inserted. The new piece's **damaged flag is set**: piece
     word bit 27 (`10-save-format.md` §6.2.6), which gives traffic flag 0x80.
  3. **Repair (T < D).** k = D − T, s = max(0, ⌊D ÷ k⌋ − 1); the divisor here is D, not N. Query
     all potholed pieces of the map in the same order and walk them with the same skip-and-gap
     rule, collecting every piece reached when c ≥ g. There is **no limit of k**: the walk always
     runs to the end of the list, so it typically repairs more than k pieces (original behaviour).
     Each collected 15040, 15041 or 15042 is replaced by **29**, and 9051 by **35**. The new piece
     is created with its damaged flag clear.
* Shapes [DATA]: in the road piece IDs of `18-sim-transport.md` §3.1, 35 is a diagonal road piece
  (it has attribute 0x12D bit 0). 29 is an orthogonal road piece; 11204 is a road piece that is a
  candidate but is never converted. Sacramento (the only shipped city with potholes) has 15040, 15042
  and 9051, each with piece-word bit 27 set.
* Potholed pieces are ordinary network pieces in the save (§Persisted state), damaged flag
  included, so the condition and the 0x80 flag survive save and load. Correction: the previous
  revision left the shapes and the restore piece OPEN, stopped the damage walk after k pieces,
  limited repair to D − T pieces, and used N as the repair divisor.

## Persisted state

The network subsystem does not store tiles; it stores, per layer, the list of network-piece
occupants it manages. The pieces themselves (ID, position, rotation, altitude) are stored by the
occupant manager (`13-buildings-occupants.md`, `10-save-format.md`). All records below are inner
records of the city database (`10-save-format.md`) with Type `206C6E7C` and Group `2147C2DD`
`[DATA]` (verified in 59/59 shipped `.sc3`/`.SNR`/`.st3`/`.sct` files that contain the group).

**Instance 0 — header** (24 bytes)

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 2 | u16 | version | 4 (only value read; other versions make the load fail) |
| 0x02 | 2 | u16 | flags | 2 in all shipped files (versioned-header flags, see `10-save-format.md`) |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4 | u32 | surfaceCount | number of pieces in the surface layer (road, highway, rail, power) |
| 0x0C | 4 | u32 | pipeCount | number of pieces in the pipe layer |
| 0x10 | 4 | u32 | subwayCount | number of pieces in the subway layer |
| 0x14 | 4 | u32 | entryFormat | 1; entries are read only when this is 1 |

**Instance 1 — surface layer list; Instance 2 — pipe layer list; Instance 3 — subway layer list.**
A list record is omitted when its count is 0 `[DATA]`.

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 2 | u16 | version | 4 |
| 0x02 | 2 | u16 | flags | 2 |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 8·n | entry[n] | entries | n = the matching count from instance 0 |

Entry (8 bytes):

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | place | Placement word: tile x, tile z, altitude and rotation, packed as in `10-save-format.md` §6.2.3 |
| 0x04 | 4 | u32 | piece | Piece word: bits 0–15 = network piece occupant ID; bits 16–27 = small per-piece fields; bits 28–29 = the two occupant booleans. The common high half `0x3000` simply means both booleans are set. See `10-save-format.md` §6.2.6 |

Each list entry **is** the network piece itself, not a reference to an entry in the occupant record.
On load, the piece occupant is created from its key `E223741F:A317745F:<piece ID>` at the given place
and inserted into the indicated layer [STATIC][DATA 59/59]. On save the program writes the same records for every
piece in each layer, in layer iteration order `[INFERRED]`. Example (Madison, WI): 7026 surface,
3516 pipe, 0 subway entries.

Nothing else (connectivity, power/water state, ages) is persisted by the network subsystem; it is
recomputed after load `[INFERRED]`.

## Open questions

1. Drag routing (§6.2): the meaning of the on-ramp's lot occupant and whether the 17202 price key
   is used; the network manager's validation rule for the buildable prefix (§6.2.6 step 1). Endpoint
   levelling (step 3) is decoded except one call argument.
2. The exact meaning of the map flag set by bridge pier pieces. The highway abutment corner list
   and the bank-height defects are closed (§6.4 steps 5 and 5a).
3. The meaning of the piece-word bits 16–27 in the persisted lists (`10-save-format.md` §6.2.6).
4. Neighbour-exists test used before offering a connection (believed: the neighbour city on that
   edge exists, see `19-sim-economy.md`).
5. Water: whether a facility must sit on/next to a pipe tile to seed the flood, and the sweep
   tie-breaking of the allocation front.
6. Closed: the per-occupant power consumption byte is attribute 0xC9 (§9.1 step 2).
7. Collapse groups: exact trigger (demolition vs. disaster damage) and neighbourhood searched.
8. Closed: highway tunnel portal tile offsets within each end (§6.4 item 7) — 2×2 footprint,
   offsets from the max-x/max-z corner by piece-direction δ, A = right / B = left when facing δ.
