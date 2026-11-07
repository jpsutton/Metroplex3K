# 26 — Automata (vehicles, pedestrians, boats, aircraft)

## 1. Overview

Automata are the small moving objects drawn on the city view: cars, trucks, buses, trains, subway
trains (underground view only), pedestrians, boats, aircraft and emergency/special vehicles. They are
driven by a separate "street simulation" layer that runs alongside the city simulation. [STATIC]

* Tunables: INI section `[MiscStrtSimTunables]` and the vehicle lists described in §6, in
  `Sys/SC3StrtSimLayer.ini` (packed in `Sys/SYS.PAK`; a loose `\Sys\SC3StrtSimLayer.INI` is tried
  first). [STATIC]
* Data: the attribute resources in `Res/SSimData/` (§3) and the sprites in `Res/Sprites/`
  (`00000006_Vehicles.DAT`, `00000007_People.DAT`, `00000015_Boats.DAT`, `00000010_Smoke.DAT`,
  holiday DATs). Sprite image encoding is in `04-graphics-formats.md`; how sprites are placed is in
  `25-rendering.md`.

## 2. Resource files and keys

All files below are IXF containers (`02-containers.md`). Each record is one serialized object.
Key = Type:Group:Instance. All records of one file share Type and Group; Instance is the object ID. [DATA]

| File (under `Res/SSimData/`) | Type | Group | Instances | Content |
|---|---|---|---|---|
| `VATTRIB.IXF`, `VAttrib_Holidays.IXF` | C25C2BF7 | 025C2C45 | 0x07–0x63, holiday 0x64–0x6E | Vehicle attribute (one per vehicle model) |
| `VCLPROD.IXF` | 625C6CC7 | A25C6D40 | 31 records, 0x01–0x401 | Vehicle production (weighted model lists) |
| `PATTRIB.IXF`, `PAttrib_Holidays.IXF` | C22B54FA | A22B551E | 0x01–0x32, holiday 0x34–0x39 | Pedestrian attribute |
| `PPLPROD.IXF`, `PplProd_Holidays.IXF` | 21FD6EEF | 21FD8E4C | 0x01–0x3F | Pedestrian production (weighted, dated, per country) |
| `ANSPRATT.IXF`, `AnSprAtt_Holidays.IXF` | 222B7EE4 | 822B7E6B | 128 records | Animated-sprite attribute (frame sequences) |
| `PPATHATT.IXF` | A20BF670 | C20BF6C3 | 192 records (48 shapes × 4 rotations) | Pedestrian path through a tile |
| `<LANG>/VPATHATT.IXF` | C25C74F2 | A25C7520 | 908 records | Vehicle path through a tile |
| `<LANG>/TRBLKATT.IXF` | 625C6226 | 825C6289 | 862 records | Traffic-block attribute (per network tile piece) |
| `Res/SIMSCRPT.IXF` (not in SSimData) | 822B51AA | E22B51C4 | 24 text records | Behaviour scripts (§3.10) |
| `Res/TRNPROD.IXF` | 22CEFD99 | 62CEFDDB | 2 text records | Train/subway consists and intervals (§5.7) |
| `Res/AIRTRMGR.IXF` | 62DFCCEC | 62DFCD34 | 2 text records | Aircraft model lists and caps (§5.7) |

`<LANG>` is the language directory (`ENGLISH`, `English-UK`, `DUTCH`, `FRENCH`, `GERMAN`,
`ITALIAN`, `SWEDISH`); lookup should be case-insensitive (`English-UK` uses `VPathAtt.IXF`,
`TrBlkAtt.IXF`). The English-UK variant exists because traffic drives on the left. [DATA]/[INFERRED]

## 3. Record encoding

All layouts below were verified against the shipped data. They read
every record of the eight types in all 24 shipped `SSimData` files and consumes every byte: VATTRIB
77, VCLPROD 31, PATTRIB 52, PPLPROD 67, ANSPRATT 128, PPATHATT 192, VPATHATT 6,356 and TRBLKATT
6,034, with 0 failures. [DATA]

### 3.1 Common conventions

* **Header line.** A record (and every nested sub-object that has its own header) starts with a
  text line terminated by CR (0x0D). If the line is `BIN` the rest is binary, as described below.
  Otherwise the record is in the equivalent **text form**: one value or one tuple per CR-terminated
  line, as decimal integers separated by spaces, in the same order as the binary fields. All shipped
  records are binary, but a loader should accept the text form so that it can read hand-made add-ons.
  [STATIC]
* PPLPROD alone uses a 4-byte magic instead: `BIN!` (0x214E4942) for binary or `TXT!` (0x21545854)
  for text. [STATIC]
* **Key reference** (12 bytes): u32 Type, u32 Group, u32 Instance (Type, Group, Instance order). Note that
  this differs from the IXF index order (`02-containers.md`). A zero key means "none". [DATA]
* **String**: u32 length, then that many bytes, with no terminator. [DATA]
* **Speed values** are stored as u32 and converted on load to a rate by multiplying by
  1/450,000,000 (≈ 2.22222 × 10⁻⁹). The result is a speed in 1/256-tile units per microsecond of
  elapsed time (§7), so a stored value v moves v / 115,200 tiles per second (a typical road lane,
  64,000, ≈ 0.56 tiles/s). Treat the factor as exact. [STATIC for the factor; INFERRED for the
  distance unit, see §7]
* **Fixed-point positions** are s32 values in 1/256 of a tile, measured from the tile's corner (0..255
  inside one tile; 256 = the next tile). [DATA]/[STATIC]
* **Instance numbering for per-piece data.** PPATHATT, VPATHATT and TRBLKATT instances are
  (PieceID << 8) | rotation, with rotation 0..3. Every shipped piece has all four rotations, except
  for a few VPATHATT pieces that have only rotations 0 and 1. [DATA]

### 3.2 VATTRIB — vehicle attribute (Type C25C2BF7)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | header line |
| 4 | 1 | u8 | Emergency | 1 = emergency vehicle (siren/flashing; set for the police car, ambulance and fire pumper); any other value = 0 [INFERRED] |
| 5 | 4 | s32 | MotionSound | sound ID of a looping sound attached to the vehicle (0 = none). It starts when the vehicle spawns, its position is updated every 10 updates, and it stops when the vehicle is removed. Shipped: 0 for road vehicles; 131–169 for trains, boats, aircraft and the holiday vehicle. Sound IDs are as in `06-audio.md`. [STATIC] |
| 9 | 4 | s32 | ParamB | 113–125 for road vehicles; 0 for rail, boats and aircraft. The street-sim layer's sound and movement logic does not read it; its consumer is [OPEN]. Probably a per-vehicle sound ID used elsewhere [INFERRED, low confidence]. |
| 13 | 4 | s32 | SirenSound | sound ID of the siren (0 = none; shipped non-zero only for emergency vehicles, equal to ParamB or 130). It is attached at spawn like MotionSound. While driving, at every 4th piece entered it is re-triggered with probability 1/2, using variant 0 with probability 80% and otherwise variant 1 or 2 with equal probability. It stops on removal. [STATIC] |
| 17 | 4 | s32 | Length | vehicle length in 1/256 tile, used for spacing (cars 28–63, trucks 79–90, rail cars 67–76, aircraft 100, boats 89–328) [INFERRED] |
| 21 | 12 | key | Animation | ANSPRATT key (§3.6) |
| 33 | 12 | key | Script | behaviour script in `Res/SIMSCRPT.IXF` (§3.10); zero key = none |
| 45 | 4 | u32 | MaxSpeed | speed (see §3.1) |

Script usage in the shipped data (key Type 822B51AA, Group E22B51C4): 2001 for self-driven road vehicles and locomotives, 2002 for
trailing rail cars, 0x30 for parade/holiday vehicles, and none for boats and aircraft. [DATA]

### 3.3 VCLPROD — vehicle production table (Type 625C6CC7)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 4 | f32 | Rate | relative production rate (shipped 0.05–7.0) |
| 8 | 4 | u32 | N | entry count |
| 12 | 16·N | | Entries | N × (u32 Weight, key VATTRIB) |

Choose a vehicle by weighted random selection: draw r uniformly in [0, ΣWeight) and take the first
entry whose cumulative weight exceeds r. Instances 0x01–0x19 are land-traffic classes. Instances
0x3FC–0x401 list boats (sail boats, freighters, etc.) and are used for water traffic. [DATA]/[INFERRED]

### 3.4 PATTRIB — pedestrian attribute (Type C22B54FA)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 12 | key | Animation | ANSPRATT key |
| 16 | 12 | key | Script | behaviour script (SIMSCRPT) |

### 3.5 PPLPROD — pedestrian production table (Type 21FD6EEF)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | char[4] | Magic | `BIN!` |
| 4 | 4 | u32 | NumCountries | |
| … | | | Country × N | see below |

Country block: String Name; u32 NumModes; then NumModes × Mode.
Mode: u32 DateCode; u32 N; f32 Rate; u32 Radius (8 in all shipped data); then N × (u32 Weight,
key PATTRIB). Rate is the pedestrian cap of a source using this mode, and Radius is its spawn radius
in tiles (§5.8). [STATIC]

* **Country selection.** The name is a country code. The block whose name equals the running
  language's country code is used (shipped: `gb` for English-UK). If none matches, the block named
  `def` is used. A name with the suffix `:holidays` is used only when holiday content is enabled
  (`PplProd_Holidays.IXF`). [STATIC]/[DATA]
* **DateCode.** On the first mode of a block: if DateCode ≥ 1,010,101 it is absolute, packed as bytes
  (most to least significant) StartMonth, StartDay, EndMonth, EndDay. Otherwise, it and the later
  codes in the block are deltas, with the running value starting at −1. The shipped data uses
  absolute codes, e.g. 1/1–9/30, 10/1–10/31, 11/1–11/30 and 12/1–12/31. The mode whose range contains
  the current game date applies. [STATIC]/[DATA]
* **Weights.** These are cumulative in the same way as the per-mode list deltas: the stored value is
  added to a running total that starts at −1. A random draw in [0, total] picks the first entry whose
  cumulative value is ≥ the draw. [STATIC]

### 3.6 ANSPRATT — animated-sprite attribute (Type 222B7EE4)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 12 | key | SpriteInstance | Type 00006300 sprite-instance record (`25-rendering.md` §4.2) |
| 16 | 4 | u32 | NumSequences | 1–4 |
| 20 | | | Sequences | NumSequences × (u32 SequenceID, u8 Length, Length × u16 Frame) |

Each sequence lists animation-frame numbers in playback order, one entry per animation tick; for
example, frames 0,0,0,0,1,1,1,1,… hold each image for 4 ticks. Every Frame value in the shipped data is less than the referenced sprite instance's AnimFrames
(128/128 records; all sprite-instance keys resolve to `CSATTRIB*.IXF`, Group 0x6400) [DATA]. On load each
Frame value is multiplied by the sprite instance's AnimSetCount to give the base frame index, and the zoom/orientation
offset is added at draw time (`25-rendering.md` §4.2). Observed SequenceIDs are 1, 2, 6, 10–14,
16 and 20, with 20 being the default motion loop. [STATIC]/[DATA]/[INFERRED]

**Orientation slot of an automaton sprite** [STATIC][DATA]. Vehicle and train sprite instances have
RotCount 32 and ZoomSetCount 48. People have RotCount 8 and ZoomSetCount 8, with no slope slots.

The slot within a zoom set is

  slot = TBL[gradeClass][(VIEWROT[viewRotation] + heading) & 31]

* heading: the automaton's 32-step heading in the original's convention.
* VIEWROT = {0, 8, 16, 24}. It is the table at STRTSIM 0x1003265C.
* TBL is the 5 × 32-byte table at STRTSIM 0x10032660:

  | gradeClass | Meaning | Slot by heading |
  |---|---|---|
  | 0 | steep downhill (2 steps) | headings 0–3 and 28–31 → 36, 4–11 → 37, 12–19 → 38, 20–27 → 39 |
  | 1 | gentle downhill (1 step) | same pattern → 44–47 |
  | 2 | level | identity: slot = heading 0–31 |
  | 3 | gentle uphill | same pattern → 40–43 |
  | 4 | steep uphill | same pattern → 32–35 |

  On a slope the heading snaps to the nearest of the four road diagonals.
* gradeClass = clamp(round(g) + 2, 0, 4), with thresholds at |g| = 0.5 and 1.5. g is the stored grade of the current path:
  * a VPATHATT path stores g = (h_last − h_first) / √(Δa² + Δb²), in raw path units;
  * a straight segment stores g = Δh / horizontal distance.

  So a 1-step slope gives g ≈ ±1 and a 2-step slope g ≈ ±2. Turns and curves force class 2. The
  class is recomputed after every movement step (STRTSIM 0x10019660; slot lookup 0x10005072).
* Rounding is [OBSERVED]: a train in Berlin on rail piece 46 (g = ±1.698) showed the steep frame
  (slot 38). On piece 16008 (g ±1.016) it showed the gentle frame (slot 46). The float constant
  itself was not read.
* Which sign is uphill is [INFERRED] from the sprite extents. On heading-diagonal 0 (facing up the
  screen), class-4 frames are tall and class-0 frames are short.

### 3.7 PPATHATT — pedestrian path through a piece (Type A20BF670)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 4 | s32 | N | number of points (1–14) |
| 8 | 12·N | | Points | N × (s32 a, s32 h, s32 b), fixed-point 1/256 tile; h is height |
| 8+12N | 4 | u32 | EntryHeading | 0, 8, 16 or 24 (stored ÷ 4 → 8-way heading index 0..7) |
| 12+12N | 4 | u32 | ExitHeading | same encoding |

### 3.8 VPATHATT — vehicle path through a piece (Type C25C74F2)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 4 | s32 | N | number of points (≥ 2) |
| 8 | 12·N | | Points | N × (s32 a, s32 h, s32 b), 1/256 tile |
| 8+12N | 1 | u8 | EntryHeading | 0/8/16/24 in 32-step units (ignored on load; recomputed) |
| 9+12N | 1 | u8 | ExitHeading | 0/8/16/24 |
| 10+12N | 4 | s32 | Lane | lane/stop index; 1 is added on load [OPEN] |

Derived on load: [STATIC]
* Cumulative arc length at each point, with segment length
  √(Δa² + Δb² + (0.204125·Δh)²), rounded toward zero.
* Grade = (h_last − h_first) / √((a_last − a_first)² + (b_last − b_first)²).
* Initial heading = the 32-step direction of (a₁ − a₀, b₁ − b₀), via atan2 scaled to 0..31; any
  result ≥ 32 is replaced by 0.

### 3.9 TRBLKATT — traffic-block attribute (Type 625C6226)

One record per network piece and rotation. It defines the lanes that vehicles can occupy on that piece.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | — | `BIN\r` | |
| 4 | 16 | s32×4 | Box | piece offset/extent in 1/256 tile (each value is divided by 256 on load); usually 0, ±256 or ±512 for multi-tile pieces |
| 20 | 1 | u8 | Flag | 1 on 99 of 862 records (31 piece IDs: 0x27, 0x2B, 0x33, 0x36, 0x3A, 0x45, 0x149, 0x14C, 0x14D, 0x2BC7–0x2BCE, 0x2D62, 0x2D63, 0x2D83–0x2D86, 0x36CE–0x36D0, 0x3AC5–0x3AC7, 0x4664, 0x4665). Stored as a boolean on the piece; its consumer is [OPEN]. Many flagged pieces also have a pedestrian set, so it is possibly a "pedestrian crossing / shared" marker [INFERRED, low confidence]. |
| 21 | | | PedestrianSet (set A) | sub-object with its own `BIN\r` line: u32 N; N × PedLane |
| … | | | VehicleSet (set B) | sub-object with its own `BIN\r` line: u32 N; N × VehLane |

* PedLane = u32 LaneID; s32 a, h, b (node position); u32 M; M × PedConnection.
* VehLane = u32 LaneID; s8 LaneType (0/1/2); s32 a, h, b (entry point); u32 M; M × VehConnection.
* Both connection kinds are 10 bytes: s8 Next; u8 Heading; u32 Path; u32 Value.
  * Next is the LaneID of the next lane or node on the same piece, or −1 to leave the piece. On exit,
    Heading gives the exit edge as 0/8/16/24 (32-step units).
  * VehConnection: Path is the VPATHATT instance to follow, and Value is the lane speed (§3.1). 64,000
    is the most common speed; others are 95,999, 110,000, 60,000, 70,000 and 55,000.
  * PedConnection: Path is a **PPATHATT** instance (e.g. 0x100 = shape 1, rotation 0), or 0 on an
    exit connection (Next = −1). Value is 0 or 1 and is not a speed. It is probably a "this walk
    crosses the carriageway" flag [INFERRED; OPEN].

**Set A is the pedestrian graph, set B the vehicle lanes.** [STATIC: pedestrians move only on set A
and vehicles only on set B]/[DATA: set-A
nodes sit at the sidewalk corners of the tile (15 or 240 in 1/256-tile units), and set-A paths are
PPATHATT instances] Set A is non-empty on 177 of 862 records (45 piece IDs: intersections, some
transit pieces and the default piece 0x1D). On pieces without a set A, pedestrians use the per-piece
PPATHATT paths (§3.7, `PPATHATT` instance = (PieceID << 8) | rotation). VehLane LaneType counts are
0: 2,808, 1: 384, 2: 280. Its meaning is [OPEN]; probably 0 = general traffic and 1/2 = turn or
transit-only lanes [INFERRED, low confidence]. [DATA]

### 3.10 Behaviour scripts (`Res/SIMSCRPT.IXF`, Type 822B51AA, Group E22B51C4)

**File format.** Each record is plain text with CR LF line endings (a reader should also accept a
bare CR). The layout is: [STATIC]/[DATA]

1. Line 1: the step count S (decimal; read as a 16-bit signed value).
2. S steps. Each step is:
   * a line `op<TAB>argcount`, where op is read as u16 and argcount as u8;
   * argcount lines, each holding one signed 32-bit decimal argument;
   * one separator line, which is read and ignored. The shipped files have a blank line here, and the
     last step is followed by trailing blank lines.

Arguments that are resource keys may be written as unsigned decimal values above 2³¹ (for example
2183877034 = 0x822B51AA); read them modulo 2³². This layout parses
24/24 shipped records, and every call target resolves to a record in the same file. [DATA]

**Execution model.** Each automaton runs one script through a small call stack: [STATIC]

* A stack frame holds the script, the current step index (starting at −1), a repeat limit and a
  repeat counter (starting at 0).
* The automaton starts in the script named by its attribute record (VATTRIB or PATTRIB Script key),
  with repeat limit 1.
* To advance, the step index is incremented. If it reaches S, the repeat counter is incremented. If
  the counter is now at least the repeat limit, the frame is popped and the step returns "done" to
  the caller frame. Otherwise the index wraps to step 0.
* The step's opcode is then executed. Every opcode starts a **behaviour** that runs over several
  updates. While the behaviour runs, the script is suspended. When the behaviour ends, it reports
  one of three results: *done* (go on to the next step), *fail* (pop this script frame and report
  *fail* to the caller frame, which pops in turn, so a failure unwinds the whole stack), or
  *running* (stay suspended).
* When the outermost script frame finishes or fails, the automaton has nothing left to do and is
  removed (§5.6). [STATIC]/[INFERRED: removal on empty stack]

**Randomised arguments.** Many arguments come as a pair (value, jitter). If jitter is 0 the value
is used as is. Otherwise the value is drawn uniformly from the integers in
[value − jitter, value + jitter). The draw uses the street-sim LCG and takes r mod n, with
n = 2·jitter, so the upper bound is exclusive. [STATIC]

**Opcodes.** Pedestrian scripts and vehicle scripts use separate opcode tables. [STATIC]

| Op | Args | Pedestrians | Vehicles |
|---|---|---|---|
| 0 | 5: Count, CountJitter, Sequence, Speed, SpeedJitter (pedestrians); 3: Count, CountJitter, Sequence (vehicles) | **Walk.** Walk along PPATHATT paths for Count path pieces, using animation sequence Sequence (ANSPRATT SequenceID) and speed Speed (speed units, §3.1). At each piece end, the next piece is chosen from the paths leaving through the current exit edge. If there is none, the step fails. When Count pieces are done, the step is done. | **Drive.** Drive along lanes for Count pieces, using animation sequence Sequence. The speed comes from the vehicle's MaxSpeed and the lane speed (§7). Script 2001 is a single Drive of 10,000 pieces with sequence 20, so an ordinary vehicle drives 10,000 pieces and then its script ends. |
| 1 | 3: Sequence, Loops, LoopsJitter | **Animate in place.** Play ANSPRATT sequence Sequence Loops times. Timing: an accumulator starts at 0 and adds the update's elapsed time. While it is ≤ 300,000 time units (§7) the current frame is held. After that, the sequence advances one entry per update. When it wraps past its last entry, one loop is counted. If Loops loops are done, the step is done; otherwise the accumulator resets to 0 (another hold) and the sequence plays again. | Present in the opcode table but not used by any shipped vehicle script [OPEN semantics]. |
| 2 | 5: Type, Group, Instance, Repeat, RepeatJitter | **Call.** Push the script with key Type:Group:Instance with repeat limit rand(Repeat ± RepeatJitter). The callee runs Repeat times and then returns "done". If the key does not resolve, the step fails. | Same as pedestrians. |
| 3 | 1: Sequence | — (not in the pedestrian table) | **Follow.** Set the animation sequence. If it changed, restart at frame 0. Then follow the vehicle ahead (rail-car trailer behaviour; script 2002). The step does not end on its own: the car follows until its leader is gone. [STATIC]/[INFERRED end condition] |
| 200 | 0 | — | **Parade.** Used only by script 0x30. Ordinary vehicles understand only opcodes 0–3, so opcode 200 is meaningful only to the parade automaton [INFERRED]. A clone must never run it on an ordinary vehicle. Its exact behaviour is [OPEN]; §5.7 (parade/holiday automata) describes where parade vehicles come from. |

Pedestrian script roles in the shipped data (instances 1–0x15): Walk and Animate steps mixed, with
Call steps to share sub-behaviours (e.g. instance 0x12 calls 0x11 three times, give or take 2).
Instances 1–0x15 are pedestrian behaviours, 0x30 is the parade behaviour, 2001 (0x7D1) is driving
and 2002 (0x7D2) is follow-the-leader for rail cars. [DATA]

## 4. Paths and lanes

Automata do not path-find over the map. They follow pre-authored per-piece paths: [STATIC]/[DATA]

1. Each network piece instance on the map (road, highway, rail, etc., with a piece ID and rotation
   from `12-networks.md`) selects the TRBLKATT record whose Instance = (PieceID << 8) | rotation.
   If no record exists, the default record 0x1D00 is used.
2. A vehicle on that piece occupies one lane (TRBLKATT lane entry). It follows the VPATHATT path
   named by the chosen lane connection, moving through the path's points in order. Its position is
   the 1/256-tile fixed-point point (a, h, b) offset by the piece's tile origin.
3. At the end of the path, the connection's Next field gives the next lane on the same piece.
   Next = −1 means the vehicle leaves the piece through the edge given by Heading
   (0/8/16/24 = the four tile edges in 32-step units). It then enters the neighbouring tile's piece
   through a lane whose entry point matches.
4. When a lane has several connections, one is picked at random (uniformly) [INFERRED]. This
   produces turning at intersections.
5. Pedestrians use PPATHATT paths in the same way, with 8-way entry/exit headings
   (EntryHeading/ExitHeading ÷ 4). On pieces whose TRBLKATT record has a pedestrian set (set A,
   §3.9), they walk that node graph instead, and each connection names the PPATHATT path to use.

Path point coordinates are relative to the piece's anchor tile corner, in 1/256 tile, with
h as height. A "Box" offset in TRBLKATT (multiples of 256) handles multi-tile pieces. [DATA]

## 5. Spawning

### 5.1 Buildings as sources

**Buildings as sources.** Each occupant attribute record (`13-buildings-occupants.md`) can carry
two attributes. In `OccupantAttribs*.IXF` they appear as follows: [DATA]
* **0x6B** = key of a PPLPROD table (pedestrians): 390 records, 112 agent types.
* **0x6C** = key of a VCLPROD table (vehicles): 358 records, 99 agent types.
* Both: zone buildings, landmarks, city hall / mayor's house, bus station 12210, airport 12310,
  seaport 12330, marina 14430 and many civic buildings.
* 0x6B only: rail station 12230, subway station 12360, spaceport 12350, casinos 19001–19004.
* 0x6C only: water pump 12810.
* Neither: the subway/rail connector 12361.

How each source is used [STATIC]:
* **Pedestrians.** Pedestrians spawn only on **road tiles**, i.e. tiles whose occupant has the
  road-piece flag 0x8000 of the run-time flag word (`13` §3.1a). Buildings are never spawn tiles.
  * A building with attribute 0x6B whose anchor lies in the active view rectangle gets its own
    pedestrian manager, using its PPLPROD table and centred on its anchor. That manager spawns
    pedestrians on road tiles within anchor ± the mode's radius (8), as described in §5.8.
  * A global default manager uses PPLPROD `21FD6EEF:21FD8E4C:1`.
* **Vehicles.** Attribute 0x6C does **not** make the building a spawn point. Its VCLPROD table is
  merged into the vehicle-mix table of the 32 × 32-tile cell that holds the building's anchor, so it
  only weights which models are chosen for road traffic in that cell (§5.5).
* **Trains** spawn from **rail end caps** (agent type 17002), each with its own spawner using
  TRNPROD instance 1. **Subway trains** spawn from subway end caps (17420, TRNPROD2). Stations are
  not train sources.
* **Ships** come from seaports (§5.4). **Leisure boats** come from marinas (14430, §5.3).
  **Aircraft** come from a per-airport manager [INFERRED for the airport identification].

**Keeping sources in step with the city** [STATIC]: this is event-driven; there is **no periodic
city scan**.
* The occupant-added and occupant-removed notifications create and tear down:
  * train spawners;
  * airport and seaport managers;
  * marina boat sources;
  * per-building pedestrian managers (only when the building is inside the active rectangle);
  * the VCLPROD cell contributions (always).
* On activation and on every view-rectangle change (§5.6), the newly visible strips are scanned
  and pedestrian managers are created for buildings anchored there. Managers in strips that left
  the view are removed.
* The only periodic work is:
  * each spawner's own timer;
  * a daily date check that re-picks the PPLPROD mode (§5.8).

### 5.2 Weighted model choice

**Weighted choice.** The model is chosen by weighted random selection (§3.3, §3.5). For pedestrians,
the mode whose date range contains the current game date applies, which yields seasonal and holiday
pedestrians. [STATIC]

### 5.3 Leisure boats

**Boats.** Leisure boats spawn at water tiles within `MaxMarinaToBoatDistance` tiles of a marina.
The test is squared Euclidean distance ≤ value², with the value taken from
`[MiscStrtSimTunables]` in `SC3StrtSimLayer.ini` (shipped value: 30). One of VCLPROD 0x400 or
0x401 is used, depending on a per-location condition [OPEN which condition]. 0x400 holds models
73/74/99 (weights 300/300/400); 0x401 holds model 55. Each marina keeps its own live-boat
count. [STATIC]/[DATA]

### 5.4 Ships at seaports

**Port/sea traffic (ships at seaports).** Each seaport building has its own ship spawner with a
timer (time units, §7): [STATIC]
1. While the spawner is not ready (its sea route has not been set up yet, e.g. no reachable water
   edge), each expiry retries the setup and, if it still fails, re-arms with 360,000,000 (6 min).
2. Once ready, each expiry makes one spawn attempt and re-arms with a uniform interval in
   [120,000,000, 360,000,000) (2–6 min).
3. Spawn attempt: build a 2-bit index. Bit 0 is set when either of the city conditions with codes
   4 and 5 holds [OPEN meaning; probably sea-connection or trade conditions]. Bit 1 is set when at
   least one object of type 0x2AFA exists [OPEN identity]. The index selects VCLPROD 0x3FC + index.
   If the port's live ship count is below trunc(Rate of that VCLPROD), one ship is spawned (model
   by weighted draw, §3.3) and follows the port's route; otherwise nothing happens. Here VCLPROD
   Rate is therefore the per-port **maximum number of simultaneous ships**. [STATIC]

### 5.5 Road traffic from traffic density

**Traffic density → road vehicles.** Ordinary road traffic is spawned only inside the current
view rectangle (the tiles visible on screen), from the traffic-density map of the traffic simulation
(`18-sim-transport.md`, one u8 density d = 0..255 per tile). [STATIC] Three quantities are
recomputed whenever the view rectangle changes (scroll, zoom, rotate):
* S = sum of d over the tiles in the view rectangle, and C = the number of tiles the density map
  reports for that sum (non-road tiles contribute nothing). C is the tile count of the "area sum
and count" query of `18-sim-transport.md` §7.1: the number of tiles in the area that carry a
requested network. [STATIC]
* **Vehicle limit** L = trunc(S × 0.008), capped at 200. In the **event mode** described in §5.6,
  L = trunc(S × 0.2) with no cap instead. [STATIC]
* **Average density** A = trunc(S / C) as a byte (0 if C = 0).
* After recomputing, the layer broadcasts message 0x402 with argument trunc(L × 100 / 200), which is
  the expected traffic level as a percentage. Its listener is probably the ambient traffic sound
  (`06-audio.md`). [STATIC]/[INFERRED listener]

  0.008 is a single-precision constant (0.008000000380). Spawning has three parts:
  1. **Initial fill.** Whenever the layer becomes active (§5.6), and for each newly exposed strip
     after a view change, every tile in the area that holds a drivable network piece gets n vehicles:
     n = trunc(d × 0.008), plus 1 more if a uniform random float in [0,1) is less than the fractional
     part (d × 0.008 − n). So the expected count per tile is d × 0.008, which is 2.04 at the maximum
     density 255. [STATIC]
  2. **Trickle (every update).** If the current count N < L, run k = max(1, trunc((L − N) × 0.1))
     spawn attempts. Each attempt picks a uniformly random start tile in the view rectangle and
     scans tiles row by row (x fastest), wrapping inside the rectangle, until it is back at the start.
     It spawns one vehicle on the first drivable tile where a draw r = rand(max(A,1)) in [0, A)
     satisfies r ≤ d. Tiles at or above the average density therefore always qualify, and a tile of
     density d < A qualifies with probability (d+1)/A. [STATIC]
  3. **Per-tile placement.** Placement stops as soon as N exceeds L or N > 199, whichever comes
     first. Each vehicle's model is chosen by a weighted draw from the **mix table** of the 32×32-tile
     map cell that contains the tile (cell index = (y div 32) × cellsPerRow + (x div 32)), and the
     vehicle starts on a lane of the tile's piece. [STATIC]
  * **Mix tables.** Each 32×32-tile cell keeps a merged weighted list of VATTRIB models. It is built
    by merging VCLPROD tables (§3.3): a default table when the layer starts, then the VCLPROD tables
    (occupant attribute 0x6C) of buildings in the cell as they are added or removed. When a table
    with Rate R is merged into a cell whose accumulated Rate is R₀, its weights are scaled by
    R / R₀ (by 1 if R₀ = 0) and added to the weights of the same models, and the cell's Rate becomes
    R₀ + R. The cell's weights are then rescaled so that they sum to about 10,000,000; the rescale
    repeats while the sum differs from 10,000,000 by more than 1,000,000. A draw is r = rand(sum) and
    picks the first model whose running weight total exceeds r. [STATIC; the exact rounding of the
    scaled weights is INFERRED]
  * In event mode (§5.6) the model is instead chosen uniformly from a global list, believed to be
    the key-vehicle list. [STATIC]/[INFERRED list identity]
  * Spawning is enabled only when the street-sim layer is active (§5.6). [STATIC]

### 5.6 Activation and removal (all automata)

**Activation.** The street-sim layer is active only while both of these hold: [STATIC]
* the view's zoom index (0 = farthest … 4 = closest, `25-rendering.md`) is ≥ the automata zoom
  threshold Z. Z = 5 − p, where p is an automata-detail preference in 0..3, so p = 3 gives Z = 2 and
  p = 0 gives Z = 5 (never active). The default before any preference is applied is Z = 2: automata
  appear at the three closest zooms. [STATIC; the name of the user-facing option is OPEN]
* the view's secondary mode value is 0. A non-zero value, believed to be an alternate view such as
  the underground view, deactivates the layer. [STATIC]/[INFERRED meaning]

When the layer becomes inactive, every automaton is removed and road-vehicle spawning is disabled.
When it becomes active, spawning is enabled, the view rectangle is recorded, and the view is filled
at once using the initial-fill rule of §5. [STATIC]

**View changes.** The layer re-examines the view after every change of the view. This includes zoom, rotation, and scrolling when the view centre has moved
by more than one tile in total. The old and new view rectangles are compared. Automata in the parts
of the old rectangle that are no longer visible are removed, and the newly exposed strips are
filled with the initial-fill rule. The limit L and average density A are recomputed (§5).
[STATIC]/[INFERRED: that the old-only strips are removed rather than merely hidden]

**Removal of an individual automaton.** An automaton is removed (and the road-vehicle count N
decremented) when any of these happens: [STATIC]
1. Its script finishes, i.e. the outermost frame completes its repeats. For ordinary road vehicles
   this happens after 10,000 pieces (script 2001).
2. A script step fails. For vehicles: there is no onward piece or lane at the end of the current
   piece (a dead end, or the network was deleted), or the per-piece region test reports that the
   vehicle has left the active area. For pedestrians: there is no onward path at a piece end, or a
   Call target is missing.
3. The area it is in leaves the view rectangle (above), or the layer is deactivated.

There is no despawn timer. With script 2001, ordinary vehicles live until they leave the view, hit a
dead end, or drive 10,000 pieces. [STATIC]

**Event mode.** The layer has a three-state event mode: 0 = off, 1 = armed, 2 = active. A
notification from outside the layer moves it from 0 to 1. On that transition the layer resets an
internal 70 × 70 grid of per-area values to 150 and increments an event counter. While the mode is
armed, update elapsed time is accumulated, and after 15,000,000 time units (§7) the mode becomes
active. While it is active:
* the road-vehicle limit is L = trunc(S × 0.2), uncapped;
* each new road vehicle's model is drawn uniformly from a fixed global vehicle list, believed to be
  the preloaded key-vehicle list, instead of from the cell mix tables.

[STATIC] The trigger is the disaster layer's "state of emergency declared" notification
(`21-disasters.md` §10.1) with its "a disaster is really active" flag set; a false alarm does not arm
the mode. [STATIC]

### 5.7 Other sources

* **Key vehicles.** `[KeyVehiclesForLowMemoryMachines]` / `[KeyVehiclesForHighMemoryMachines]` in
  `SC3StrtSimLayer.ini` list `name=VATTRIB instance` pairs. These vehicles are always loaded (their
  sprites are preloaded). The Low list is used when the machine's memory class value is below 49,
  otherwise the High list. (Both shipped lists are identical.) [STATIC]
* **Emergency vehicles.** `[EmergencyVehiclesFor…MemoryMachines]` list the vehicle used for each
  emergency dispatch type: fire pumper, ambulance and police car (shipped VATTRIB instances 43, 41
  and 28). These are spawned when the player dispatches emergency services (`21-disasters.md`) and
  drive to the target. [STATIC]/[INFERRED]
* **Parade/holiday automata.** These are VATTRIB records 100–109 (VATTRIB 110 is an
  aircraft), using script 0x30 (record 84 also uses it but is the riot crowd, §5.9), plus holiday pedestrians from `PplProd_Holidays.IXF`. They are
  available only with holiday content enabled. [DATA]
* **Parades** are a city **event** owned by the disaster/event layer, not the street sim, which
  only moves the vehicles. [STATIC]

  *Settings* — all in `SC3DisasterLayer.ini`:
  * `[ParadeEventTunables]`: `RoadTilesRequired` 6, `MaxParadeDuration` 4500, `ParadeCost` 100,
    `MinPopulation` 10000, `MinCommercial` 30, `MinAura` 30, `SpectatorDensity` 3,
    `PetitionerTiming` 3, `MinMonthsBetweenEvents` 120, `EventProbability` 100.
  * `[ParadeEventParadeBlocks]`: `n=VATTRIB,spacing`. Blocks 1–9 are VATTRIB 100–107 and 109,
    with spacing 450 (500 for the band entries).
  * `[ParadeEventParadeConfigs]`: one line per country,
    `M/D:nameString:min,max:leadBlocks:middleBlocks:tailBlocks`; for example
    `us=11/25:247:8,10:1:2,3,4,5,6,7,8:9`.

  *Trigger* — checked on the event layer's tick.
  1. After a parade, nothing happens until `MinMonthsBetweenEvents − PetitionerTiming` months
     have passed.
  2. Then all of the following must hold:
     * the active-event count is below its cap, and a config exists for the country;
     * rand(100) ≤ `EventProbability`;
     * the config date's month is within `PetitionerTiming` months ahead
       ((configMonth − month) mod 12 ≤ `PetitionerTiming`);
     * day of month < 7;
     * funds > `ParadeCost`;
     * population > `MinPopulation`;
     * the commercial valve (5000) ≥ `MinCommercial`;
     * city aura ≥ `MinAura`;
     * a route exists (below).
  3. If all hold, a petitioner offers the parade (petition `0x12D`). Declined or ignored offers
     return to step 2 after a few ticks.
  4. If accepted, the parade starts when today is within 7 days of the config date. The
     "last parade" month is then recorded.

  *Start.*
  * The config whose date is nearest (on a 366-day circular calendar) is used.
  * `ParadeCost` is deducted.
  * **Route:** a spiral search from a start point (staying 2 tiles inside the map). It looks for a
    straight road run of at least `RoadTilesRequired` tiles:
    * every tile has the road flag 0x8000 and aura ≥ `MinAura`;
    * there are at least 3 tiles of zone class 6 [INFERRED: commercial] in its 3 × 3 neighbourhood;
    * the axis and the direction are chosen at random when the piece allows both.

  *Vehicles.*
  * One lead vehicle is drawn from the lead blocks.
  * Then uniform[min, max] − 1 followers are drawn from the middle blocks, except the last, which
    is from the tail blocks. That is about 8–10 vehicles.
  * Each is placed behind the previous one at its block's spacing, at the route start, facing
    along the route.
  * Spectators are spawned at `SpectatorDensity` per route tile.
  * A parade sound plays while parade vehicles are in view.

  *Not traced:* what opcode 200 does inside the vehicle (speed comes from VATTRIB: 110 000, or
  200 for record 107), and how the parade ends. `MaxParadeDuration` is read but never used.
  [OPEN]
* **Trains and subway trains.** These are driven by `Res/TRNPROD.IXF` (Type 22CEFD99, Group
  62CEFDDB, a text record). Each spawner is a timer bound to a source object on the map and to one
  TRNPROD record. The source objects are the **rail end caps** (agent type 17002, TRNPROD
  instance 1) and **subway end caps** (17420, TRNPROD instance 2), not stations. [STATIC]
  * **TRNPROD text format** (CR LF lines): `MinSeconds MaxSeconds` (two integers on one line;
    each is multiplied by 1,000,000 on load to give time units, §7); `NumConsists`; then for each
    consist: a blank line; `Locomotive` (VATTRIB instance); then four lists, each written as a count
    line followed by that many VATTRIB-instance lines: **List1**, **List2**, **List3**, **List4**.
    [STATIC]/[DATA: both shipped records parse this way]
  * Shipped records: instance 1 (rail, 30–90 s, 4 consists, locomotive 59 with car pools drawn from
    58, 60, 63, 64, 67, 68, 87, 90, 91) and instance 2 (subway, 60–120 s, locomotive 89, car 92). [DATA]
  * **Timer.** Each spawner accumulates elapsed time. When it reaches the current interval, it
    resets the accumulator, attempts a spawn, and draws a new interval uniformly from
    [Min, Max) × 1,000,000. [STATIC]
  * **Spawn attempt.** On its first attempt the spawner looks for a rail piece on one of the four
    tiles orthogonally adjacent to its source object, scanning in a fixed order. The first one found
    fixes the departure direction (heading 0/8/16/24). If none is found, nothing spawns. The spawner
    then picks a consist uniformly at random and builds the train on that tile: [STATIC]
    * Rail spawner: the Locomotive; then 1 car from List1 (if non-empty); then 1–3 cars (uniform
      count) chosen with replacement from List3; then 1–4 cars chosen with replacement from List2;
      then 1 car from List4 (if non-empty). [STATIC; the relative order of the List3 and List2
      blocks is INFERRED from code order]
    * Subway spawner: the Locomotive followed by 1–3 cars from List3. [STATIC]
    * Cars are linked as a chain, each following the one ahead (script 2002, op 3). If any car
      cannot be placed, the attempt is abandoned. [STATIC]
  * The locomotive runs script 2001 (Drive). The train therefore follows rail lanes until it leaves
    the view, reaches a dead end, or completes 10,000 pieces (§5.6). [STATIC]
* **Aircraft.** These are configured by `Res/AIRTRMGR.IXF` (Type 62DFCCEC, Group 62DFCD34, text,
  CR LF lines). [STATIC]/[DATA]
  * **Format:** line 1 `MaxActive` (integer); line 2 `N`; then N lines, each a VATTRIB instance.
    Shipped: instance 1 = MaxActive 3, models 75, 77, 78, 79 (fixed-wing aircraft); instance 2 =
    MaxActive 5, model 76 (helicopter). That line 1 is the simultaneous-aircraft cap is [INFERRED]
    from a count-below-maximum check in the spawner. [DATA]/[INFERRED]
  * **Airport traffic.** Every airport building has its own spawner with a timer. The interval is
    drawn uniformly from [20,000,000, 40,000,000) time units (20–40 s, §7) and redrawn after each
    attempt. On each expiry: [STATIC]
    1. If the airport has an aircraft ready to leave, it is sent off (departure) [STATIC; departure
       details OPEN].
    2. An arriving aircraft is spawned at a random map edge, 5 tiles inside the map: one of the four
       sides is chosen uniformly; the coordinate along that side is uniform in [5, size − 5) and the
       other coordinate is 5 or size − 5. Its model is chosen uniformly from the record's list
       (instance 1). It flies towards the airport [INFERRED].
  * **Helicopters.** A single global spawner (created when the layer starts) uses AIRTRMGR
    instance 2. Every 1,000,000–3,000,000 time units (uniform, redrawn each time), if fewer than
    MaxActive of its aircraft exist, it spawns one helicopter. [STATIC] Correction: occupant group
    0x385E is the **marina** group (agent type 14430). Its buildings are registered with the
    leisure-boat source list (§5.3), not with the helicopter spawner. The helicopter's source and
    destination buildings are [OPEN].
  * Aircraft have no behaviour script (VATTRIB Script = none). Their flight path is computed
    procedurally [OPEN: curve and altitude profile].
* **Ships at seaports.** These are the port/sea traffic described in §5: VCLPROD 0x3FC–0x3FF chosen
  by a 2-bit index, with bit 1 set when at least one occupant of type/group 0x2AFA (believed to be
  the seaport) exists. [STATIC]/[INFERRED meaning]
* **Default tables.** When the layer starts it loads these records: PPLPROD 21FD6EEF:21FD8E4C:1 (the
  default pedestrian table), VCLPROD 625C6CC7:A25C6D40:1 (the default vehicle table, which is the
  table merged into every mix-table cell first, §5), TRNPROD instances 1 and 2, and AIRTRMGR
  instances 1 and 2. [STATIC]/[INFERRED: that VCLPROD 1 is the cell default]

### 5.8 Pedestrians

[STATIC except where tagged]
* **Sources.** Each building whose occupant record carries attribute 0x6B (a PPLPROD key) gets a
  pedestrian source centred on the building. Commercial buildings are the main carriers of 0x6B,
  which is why pedestrians cluster around commercial areas [INFERRED from the data]. There is also
  a global source that uses the default table PPLPROD 1 (§5.7). It is created once the event mode
  of §5.6 becomes active [STATIC]; its centre is not resolved [OPEN].
* **Mode selection.** The source selects its PPLPROD country block as in §3.5, then a mode:
  * Undated table: draw r = rand(1000) and take the first mode whose cumulative threshold is ≥ r.
  * Dated table (shipped): take the mode whose range StartMonth/StartDay … EndMonth/EndDay contains
    today's game date (inclusive at both ends). The end date is remembered, and every update the
    source compares the current game date with it. When the date passes the end, the mode is chosen
    again. This is how holiday pedestrians (e.g. the 10/1–10/31, 11/1–11/30 and 12/1–12/31 modes,
    and the `:holidays` blocks when holiday content is on) appear and disappear on schedule.
* **Cap.** On each mode selection, cap = trunc(Rate) plus 1 more if a uniform float in [0,1) is less
  than the fractional part of Rate.
* **Area.** The spawn area is the square centre ± Radius tiles (mode Radius, shipped 8), clamped to
  the map. Eligible tiles are those whose occupant has bit 0x8000 set in its occupant flag word.
  This is the **same** flag that marks a valid riot site (`21-disasters.md` §8.4): both tests read
  the same flag word and bit [STATIC]. The bit is the road-piece flag (`13-buildings-occupants.md`
  §3.1a): pedestrians spawn on road tiles. Correction: previously OPEN, with a guess that it was a lot flag
  for sidewalks, plazas or public buildings.
* **Fill.** When an area (or a newly covered strip of it after the centre moves) is filled, and the
  source's live count is below its cap: let C be the number of eligible tiles in the strip and
  n = min(cap − count, 4 × C). If C < n, every eligible tile in scan order (rows, then x) gets
  cap div C pedestrians, and the remainder cap mod C is spread with an accumulator (a tile receives
  its share only while the running accumulator stays below C). [STATIC] The C ≥ n branch was not
  traced [OPEN].
* **Point spawns.** Other layers can request n pedestrians at a given tile, which is honoured only
  if that tile is eligible.
* **Model.** For each pedestrian, r = rand(1000) is drawn and the first PATTRIB entry of the mode
  whose cumulative threshold (pre-scaled to 0..1000) is ≥ r is taken. The pedestrian then runs the
  PATTRIB Script (§3.10).
* **Global cap.** There are never more than 400 pedestrians in total. Spawning stops at 400.

### 5.9 Disaster automata

Some disaster visuals are moving objects driven by the automata/sprite-effect machinery. The
disaster layer, not the spawning rules above, creates and removes them, so they are outside the
traffic limit L, the pedestrian cap and the activation zoom rules. They are never persisted (§9).
Full movement, sprite and timing rules are in `21-disasters.md` §12; summary [STATIC]:

| Disaster | Object | Data records | Movement |
|---|---|---|---|
| UFO attack | swarm of saucers (sprite effects) | none (EffectSprites sets) | straight legs at 2.6 units/ms, command queue per saucer (§12.8) |
| Plague of locusts | locust swarms (sprite effects) | none (disaster_LOCUST sets) | straight legs at 0.4 units/ms, greedy nearest-crop order (§12.9) |
| Riot | **one road-vehicle automaton per riot** | VATTRIB instance `0x54` (ANSPRATT/CSATTRIB 11406, script 48) | drives road lanes like a vehicle; throws projectiles every 0.3–0.6 s (§12.10) |
| Tornado | funnel sprite effect | none | straight line, no wobble (§12.5) |
| Whirlpool | vortex sprite effect | none | wanders over open water at 0.12 units/ms (§12.6) |
| Space junk | falling pieces from a launcher | none | straight fall, no gravity (§12.11) |

Script 48 (opcode 200, §3.10) is shared: VATTRIB 84 (`0x54`), which is the **riot crowd**, and the
parade vehicles 100–109 use it. For the riot crowd, opcode 200 means "route to the road piece of the
riot-site lot, then keep driving" [STATIC; parade use of opcode 200 not traced]. The "event mode" of
§5.6 is triggered by the state-of-emergency declaration of `21-disasters.md` §10.1, not by the riot
[STATIC].

## 6. Vehicle classes (from the shipped data)

| VATTRIB range | Kind | Script |
|---|---|---|
| 7–57, 83, 93–96 | road vehicles (cars, trucks, buses, service vehicles) | 2001 |
| 28, 41, 43 | emergency: police, ambulance, fire pumper (Emergency = 1) | 2001 |
| 59, 89 | locomotives | 2001 |
| 58, 60–68, 87, 90–92 | rail/subway cars (followers) | 2002 |
| 69–74, 99 | boats (VCLPROD 0x3FC–0x400) | none |
| 97, 98 | unclassified non-road vehicles [OPEN] | none |
| 75–79, 110 | aircraft (DC-3, helicopter, etc.) | none |
| 84, 100–109 | parade/holiday vehicles | 0x30 |

[DATA; kinds INFERRED from the INI names and record values]

## 7. Movement and animation

* **Time units.** All street-sim timers and the per-update elapsed time are in **microseconds**: the
  TRNPROD intervals are written in seconds and multiplied by 1,000,000 on load. [STATIC] Speeds
  (§3.1) are therefore distance per microsecond after the ×1/450,000,000 conversion. The distance is
  in 1/256-tile units [INFERRED: this gives a road lane 64,000 ≈ 0.55 tiles/s, which is plausible].
* **Real time.** Automata and sprite effects (including the moving disasters of `21-disasters.md`
  §6.1–§6.2, whose speeds use the same microsecond time base) advance by elapsed **real**
  (wall-clock) time, not by simulated time: their on-screen speed does not change with the game
  speed setting. [INFERRED, high confidence: all their timers are in microseconds and none is tied to
  the simulation clock] They do **not** freeze while the game is paused: with the simulation paused, cars and trains keep moving and building animations (smoke) keep running [OBSERVED].

* Speed: a vehicle moves at most at its MaxSpeed and at most at the lane's Speed (§3.1 for the
  conversion factor) [INFERRED min rule]. Vehicles keep a gap of at least their Length from the
  vehicle ahead [INFERRED].
* Facing: the heading of the current path segment, quantised to 32 directions (vehicles) or 8
  (people), selects the orientation slot of the sprite instance (`25-rendering.md` §4.2).
* Animation: the ANSPRATT sequence (default SequenceID 20) advances one entry per animation tick
  and loops. [STATIC]/[INFERRED]
* Behaviour scripts (SIMSCRPT, §3.10) drive pedestrian activities, vehicle driving and rail-car
  following.

### 7.1 `MaxAnimationsBase` (not an automata cap)

`MaxAnimationsBase` is in section `[BuildingAnimationTunables]` of `Sys/SC3Tune.ini` (shipped value:
10). It limits **building animations**, i.e. animated occupant sprites drawn by the building/geometry
layer, and has nothing to do with street automata. [STATIC]
* It is read once. If the key is missing, the default is 10.
* The effective cap is MaxAnimations = MaxAnimationsBase + 3 × m, where m is a signed per-machine
  detail level taken from the global settings object [OPEN: which user setting; presumably the
  same memory/detail class as the key-vehicle list choice of §5.7].
* The building layer keeps a list of currently animating buildings. A further building animation is
  admitted only while the list holds no more than MaxAnimations − 2 entries (MaxAnimations = 1 is
  special-cased) [STATIC; the eviction policy when full is OPEN]. See `25-rendering.md` for the
  animation playback itself.

The automata caps are separate: 200 road vehicles from density spawning (§5), 400 pedestrians
(§5.8), per-port ship caps (§5) and per-manager aircraft caps (§5.7).

## 8. Effect on simulation

Automata are presentation only. The street-sim layer reads simulation state (networks, buildings,
traffic density, date, holiday flag) and writes nothing back to the simulation layers. No
simulation formula in the other documents uses automata counts. [INFERRED, high confidence: the
layer has no save records and no outputs to simulation maps were found.] There is no "U-Drive"
style player-controlled vehicle in this game. [INFERRED: no related strings or resources]

## 9. Persisted state

None. The street-sim module writes no versioned save records: it has none of the 0xDEADBEEF
sentinels that every saved record carries. Automata are respawned after a load. [STATIC]

## Open questions

* Opcode 200 (parade) behaviour and opcode 1 for vehicles (§3.10).
* The identity of the event mode's fixed vehicle list (§5.6); the centre of the global
  pedestrian source (§5.8).
* VATTRIB ParamB consumer; TRBLKATT Flag and VehLane LaneType; PedConnection Value; the VPATHATT
  Lane field.
* City conditions 4/5 and object type 0x2AFA in ship selection; marina condition choosing VCLPROD
  0x400 vs 0x401.
* Aircraft departure and flight path; airport and helicopter source building groups (0x385e, 0x426a,
  0x440c are the occupant groups tested when sources are registered).
* Which settings drive the automata zoom threshold p (§5.6) and the animation detail level m (§7.1).
* Pedestrian fill branch when eligible tiles ≥ needed count (§5.8).
