# 13 — Buildings and occupants

Scope: the occupant (building, flora, network piece, vehicle, person) attribute resources, the
occupant/agent ID scheme, building families, building-set sprite attribute resources, default
building appearances for special states, and the data that the zone developer (see
`15-sim-growth.md`) uses to pick a building for a lot.

Resource keys are written `T:G:I` (Type, Group, Instance) as defined in `00-README.md`. Note that
the IXF index stores the three words on disk in the order Group, Instance, Type (see
`02-containers.md`); all keys below are given in logical T:G:I order.

## 1. Files

| File (under `Apps/Res/`) | Content | Records |
|---|---|---|
| `Occupant/OccupantAttribs.IXF` | Base-game occupant attribute records | 1043 |
| `Occupant/OccupantAttribs_2.IXF` | Unlimited add-on landmarks/rewards (incl. European/Asian sets) | 41 |
| `Occupant/OccupantAttribs_German.IXF` | German landmark set | 5 |
| `Occupant/OccupantAttribs_Holidays.IXF` | Holiday building set | 16 |
| `Occupant/HOLIDAYF.IXF` | Holiday filler/replacement records | 3 |
| `BUILDFAM.IXF` | Building families (text) | 31 |
| `Sprites/CSATTRIB.IXF`, `Sprites/CSAttrib_2.IXF`, `Sprites/CSAttrib_German.IXF`, `Sprites/CSAttrib_Holidays.IXF` | Composite-sprite attribute records (binary "BIN" records), one per occupant appearance | 985 / 42 / 5 / 20 |

[DATA] All 1108 occupant attribute records in the five occupant files parse with the format of
section 2. The building-set and flora-set sprite archives (`Sprites/BuildingSets/**`, 314 records;
`Sprites/FloraSets/**`, 108 records) also contain records in the same format, so the installation
holds 1530 such records in total, all of which parse [DATA 1530/1530]. Counts in sections 1.1 and 3
are over the 1108 records of the five occupant files.

### 1.1 Occupant attribute resource keys

| Type | Group | Instance | Used for | Count |
|---|---|---|---|---|
| `207EDC0E` | `0000057E` | occupant ID | Buildings, lots, fillers, garbage piles, landfill pieces, vehicles, people, most things | 664 |
| `220055E1` | `0000057E` | occupant ID | Airport and seaport "part" occupants (carry attribute 0x1F4) | 20 |
| `E223741F` | `A317745F` (base) / `0000057E` (add-on) | occupant ID | Network pieces (road, highway, rail, pipe, subway, power line pieces) | 395 |
| `FFD30C03` | `80F48961` | occupant ID | Flora (trees, wetlands) | 29 |

[DATA] Group `0x57E` = 1406 decimal. The occupant ID (Instance) is unique across all files.
When the same ID appears in more than one file the later-loaded set overrides — see
[OPEN] in section 9.

`Sys/Occupant.ini` section `[OccupantKeys]` lists `T,G,I=(id) name` for a subset of occupants;
it is a debugging/name aid only. [INFERRED]

## 2. TKB1 occupant attribute record format

Each resource payload is a property list:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | char[4] | magic | `TKB1` |
| 0x04 | … | property[] | properties | Repeated until the end of the payload (no count field) |

Each property:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | attrId | Attribute ID (table in section 3) |
| 0x04 | 2 | u16 | valueType | 1 = u8, 2 = u16, 3 = u32, 7 = string, 8 = resource key |
| 0x06 | 2 | u16 | count | Number of values |
| 0x08 | … | values | values | `count` values of `valueType` |

Value encodings:

* type 1: `count` bytes; type 2: `count` × u16; type 3: `count` × u32.
* type 7 (string): for each value, `u32 length` followed by `length` bytes of 8-bit text (no
  terminator).
* type 8 (resource key): for each value, 12 bytes = `u32 Type, u32 Group, u32 Instance`.

[DATA] Only types 1, 3, 7 and 8 occur in shipped data; type 2 is accepted by the reader
[INFERRED]. The property list consumes the payload exactly (no trailing bytes) in all 1108 records.
Properties appear in ascending attrId order in all shipped records, but a reader must not rely on
this [INFERRED].

Writers (BAT, `24-bat-buildings.md`) produce the same layout. Uninitialised filler bytes
(`CD CD CD CD`) appear as the second u32 of attribute 0x72 in shipped data; they carry no meaning
and must be preserved/ignored [DATA].

## 3. Attribute table

Presence counts are out of 1108 records.

| ID | Type × count | Present | Meaning | Tag |
|---|---|---|---|---|
| 0x01 | u32 ×1 | 1108 | Always 0. Record version/flags. | [DATA] |
| 0x64 | string ×1 | 1108 | Internal (designer) name. Not shown to the player; the display name comes from 0x6D. | [DATA]/[INFERRED] |
| 0x65 | u32 ×1 | 1108 | **Agent type ID** (land-use code), e.g. 3500 = high-wealth low-density residential, 12111 = coal power plant. See section 4. This is the key into every land-use keyed INI table. | [DATA]/[STATIC] |
| 0x66 | u32 ×3 | 1108 | Size: (X tiles, height class, Y tiles). X and Y are the lot footprint in tiles (all zone buildings are square, X = Y; the developer uses X as the lot edge and X × Y as the tile count for valve effects); the middle value is a vertical extent class (0 for flat things such as people, vehicles, lots). | [DATA]/[STATIC] |
| 0x67 | key ×1 | 1108 | Appearance key: the composite-sprite attribute record (CSATTRIB family) that draws this occupant. Type `00006300` (static) or `00006301` (animated/multi-state), Group = the CSATTRIB set type (`00006400` base, `552D8026` add-on, `6591AD34` German, `54AACF44` holiday), Instance = sprite record ID (normally equal to the occupant ID). | [DATA] |
| 0x6B | key ×1 | 390 | Query/inspection panel key `21FD6EEF:21FD8E4C:n` (n = panel variant). | [INFERRED] |
| 0x6C | key ×1 | 358 non-zero | Query sound/ambient group key `625C6CC7:A25C6D40:n`. | [INFERRED] |
| 0x6D | key ×1 | 1108 | Display-name string key: Type `2026960B` (string table), Group `A1096A4F`, Instance normally = occupant ID. All-zero Type with Instance `FFFFFFFF` = no name. | [DATA] |
| 0x6E | u8 ×1 | 470 | Flammability (0–255). Absent = not flammable. Trees ≈120–210, wetlands 1, runways 20. Consumer: fire layer, `17-sim-services.md`/`21-disasters.md`. | [INFERRED] |
| 0x6F | key ×1 | 287 non-zero | Building sound: key with Type and Group 0 and Instance = an `AUDIO.INI` `[Sparse]` sound ID. Played on query and on placement (`06-audio.md` §10.4). | [DATA]; use [INFERRED high] |
| 0x70 | key ×1 | 1 non-zero | Second `[Sparse]` sound key of the same kind. | [DATA]; use [OPEN] |
| 0x72 | u32 ×2 | 1108 | Behaviour flag word (first u32; second is junk). Bit meanings: section 3.1. | [DATA]/[OPEN] |
| 0x73 | u32 ×1 | 243 | Special-behaviour type ID: selects the special behaviour of this occupant (e.g. power plant, police station, airport); value 0x5A2 on simple decorative specials. The values are data; an implementation maps each value it supports to a behaviour. | [INFERRED] |
| 0x74 | key ×1 | 37 non-zero | Alternate appearance key (variant sprite). | [INFERRED] |
| 0x75 | key ×1 | 36 non-zero | Second alternate appearance key. | [INFERRED] |
| 0x76–0x7A | key ×1 | 1108 | Always zero (reserved appearance slots). | [DATA] |
| 0x7B | key ×1 | 175 non-zero | Instance-only key; a hashed ID used by the building's help/query text. | [OPEN] |
| 0x7C | key ×1 | 1108 | Description/help string key: `2026960B:62E69238:n`; Instance `FFFFFFFF` = none. | [DATA] |
| 0xC9 | u8 ×1 | 684 | Monthly power consumption of the building (`16-sim-utilities.md` §2.6); absent → 0 | [STATIC] |
| 0xCA | u32 ×1 | 684 | Building flag word. Bit `0x100` = **filler lot** (garden, pool, parking, vacant lot…): such members of a building family go to the filler list used by the zone developer (`15-sim-growth.md` §5.2). Bit `0x400` together with `0x100` on a building of agent type 9300 (farm) marks a **farm field** piece, which the industrial developer removes instead of abandoning (`15-sim-growth.md` §4.3.2) [STATIC]. Other bits (values such as 0x20, 0x80, 0xA00 on airport/seaport pieces) [OPEN]. | [DATA]/[STATIC] for bits 0x100, 0x400 |
| 0x12D | u32 ×1 | 395 | Network piece: network kind (0–3). | [INFERRED] |
| 0x136, 0x137, 0x138 | u32 ×1 | 395 | Network piece: packed connectivity masks for up to three layers. See `12-networks.md`. | [INFERRED] |
| 0x1F4 | u32 ×1 | 20 | Airport/seaport part index (0–9). | [DATA] |

### 3.1 Attribute 0x72 flag word

[OPEN] Bit meanings not yet resolved. Observed values: residential/commercial/industrial zone
buildings `0x0200BEE2`/`0x0203A6E2`; civic buildings `0x00C70624`; power plants `0x0003402C`;
landfill pieces `0x00030026`; airport parts `0x001F4226`; network pieces `0x00070020`; flora
`0x0005B6E0`; vehicles/people `0x0005BEE0`. Implementations must preserve the value.

### 3.1a Run-time occupant flag word [STATIC unless marked]

The "occupant flag word" tested by networks (`12-networks.md` §5.2), disasters (`21-disasters.md`)
and automata (`26-automata.md`) is **not** attribute 0x72 and is not saved. It is a run-time
value that depends on the occupant's **class**:

* **Network pieces** (all classes of the network subsystem) have a fixed word per piece class.
  Every one has 0x4 and 0x800 (network piece). All except pipes and plain power lines also have
  0x4000. Then the network bits of the piece: 0x8000 road, 0x10000 highway, 0x20000 rail,
  0x40000 subway, 0x400000 power line, 0x800000 pipe; crossing pieces combine them (e.g. road + rail
  = 0x2C804). 0x200000 marks highway on-ramps, and 0x80000 / 0x100000 mark two further piece
  variants [OPEN: meaning]. Values in use: road 0xC804, highway 0x14804, rail 0x24804, subway
  0x44804, power line 0x400804, pipe 0x800804, road+highway 0x1C804, road+rail 0x2C804,
  rail+highway 0x34804, rail+subway 0x64804, road+power 0x40C804, highway+power 0x414804,
  rail+power 0x424804, on-ramps 0x214804 / 0x21C804, and the variants 0x8C804, 0x94804, 0xA4804,
  0x480804, 0x10C804, 0x114804, 0x124804.
* **Flora** (trees, `11-world-terrain.md` §6): 0x404. Flora is the only occupant with bit 0x400.
* **Utility buildings**: power plants 0x400104 (they conduct power), water facilities 0x800104
  (they connect to pipes), other utility classes 0x104. **Transit stations**: 0x4104.
* **Other building classes** owned by simulation subsystems (services, garbage, deal buildings,
  disasters' objects, etc.): 0x104; a few landmark/deal classes 0x40000104.
* **Generic (zoned and ordinary) buildings**: **0x104** [STATIC]. The occupant flag getter for
  these classes is a constant `0x104` (same word as the other non-network building classes
  above). There is no per-agent-type variation of this word for generic buildings.
* Automata (vehicles, pedestrians): 0x100A204.

Consequences: bit **0x8000 = road piece** (including every crossing that contains road). So riot
sites and pedestrian spawn tiles are tiles whose occupant is a road piece. Bit **0x400** = flora,
bit **0x800** = network piece, so the "non-flammable / indestructible" exclusions of
`21-disasters.md` skip trees and network pieces. Correction: earlier revisions treated these
as unidentified building flags.

### 3.2 What is NOT in the occupant attributes

[DATA] There are no per-building fields for residents, jobs, cost, upkeep, water use, pollution,
crime or land-value effect. These are keyed by **agent type ID** (attribute 0x65) in INI tables.
Every key of every `[AgentIDModifiers]`, `[AgentValveEffects]` and `[PowerPlantCapacities]` section
is an agent type ID, never an occupant ID (all 392 keys over the seven sections are agent types in
`CitySim.ini [AgentTypes]`) [DATA]. Where another document says "occupant ID" or "occupant type"
for such a key, it means the agent type. Lookups for an occupant use its agent type and fall back
to the nearest listed ancestor (section 4.1).

| Quantity | Source |
|---|---|
| Residents / jobs / commercial and industrial supply and demand | `SC3ValveLayer.ini` `[AgentValveEffects]` (see `15-sim-growth.md` §3) |
| Land-value effect and radius | `SC3LandValue.ini` `[AgentIDModifiers]` (see `15-sim-growth.md` §6) |
| Placement cost | `SC3BudgetLayer.ini` (see `19-sim-economy.md`) |
| Upkeep, capacity of services | `SC3Tune.ini` per-service sections (see `17-sim-services.md`) |
| Crime, pollution and garbage, aura effects | `SC3Crime.ini`, `SC3Pollution.ini`, `SC3Aura.ini` `[AgentIDModifiers]` (see `16-sim-utilities.md`, `17-sim-services.md`) |
| Water use | `SC3WaterLayer.ini` `[AgentIDModifiers]` (see `16-sim-utilities.md` §3) |
| Power plant output | `SC3PowerLayer.ini` `[PowerPlantCapacities]` (see `16-sim-utilities.md` §2) |
| Power use | A one-byte consumption value per occupant, recorded on its anchor tile (`12-networks.md` §9.1). It is **not** in any INI table. It is attribute 0xC9 of the building's own record `[STATIC]` |
| Agent type names and hierarchy | `CitySim.ini` `[AgentTypes]`, `[AgentTypeHierarchy]` |

## 4. ID scheme

Three distinct number spaces exist:

1. **Occupant ID** — the Instance of the attribute record (e.g. 3000 = a specific mansion,
   207 = the coal power plant building). Also the Instance of its display-name string
   (`2026960B:A1096A4F:id`) and, normally, of its CSATTRIB sprite record. [DATA]
2. **Agent type ID** (land-use code) — attribute 0x65 (e.g. 12111 = coal power plant type,
   3500 = ResHighLVLowD). Defined with names and string keys in `CitySim.ini [AgentTypes]` and
   arranged in a tree by `[AgentTypeHierarchy]` (`child=parent`). Land-value modifiers, valve
   effects, building-tracking groups and default building appearances are keyed by agent type,
   and a lookup for a type also matches its ancestors where stated by the consumer. [DATA]
3. **Sprite record ID** — Instance of the 0x67 key; equal to the occupant ID for 1046/1108
   records; differs for holiday re-skins (e.g. holiday occupant 3032 uses sprite `0x2D15`). [DATA]

### 4.1 Agent-type matching (ancestor lookup)

Tables keyed by agent type often list only some types. To find the entry for an occupant of agent
type T [INFERRED, medium; the original's exact metric was not traced]:

1. If the table has an entry for T, use it.
2. Otherwise walk up `[AgentTypeHierarchy]` (`child=parent`) from T. The distance of an entry E is
   the number of child→parent steps from T to E; an entry that is not T or an ancestor of T does not
   match.
3. Use the matching entry with the smallest distance (ties: lowest agent type). If none matches,
   the table's default applies (usually zero).

This nearest-ancestor rule is used by the crime and pollution layers (`17-sim-services.md` §7.1.2,
§7.2.3) and the "type or descendant" test of `12-networks.md` §9.1. The aura layer and land value
use a different fallback: the **lowest-numbered** key that is an ancestor of T, regardless of
distance (`17-sim-services.md` §8.2, `15-sim-growth.md` §6.4) [STATIC].

Zone building agent types (from `CitySim.ini` and `SC3ZoneLayer.ini`):

| Agent type | Meaning | Agent type | Meaning |
|---|---|---|---|
| 3000 | Residential (generic) | 5000 | Commercial (generic) |
| 3100 | Res low density, low land value | 5100 | Com low density, low–medium LV |
| 3300 | Res low density, medium LV | 5500 | Com low density, medium–high LV |
| 3500 | Res low density, high LV | 5700 | Com medium density, low–medium LV |
| 3700 | Res medium density, low LV | 6100 | Com medium density, medium–high LV |
| 3900 | Res medium density, medium LV | 6300 | Com high density, low–medium LV |
| 4100 | Res medium density, high LV | 6700 | Com high density, medium–high LV |
| 4300 | Res high density, low LV | 8000 | Industrial (generic) |
| 4500 | Res high density, medium LV | 8100 / 8300 / 8500 | Ind dirty low / medium / high density |
| 4700 | Res high density, high LV | 8700 / 8900 / 9100 | Ind clean low / medium / high density |
| | | 9300 | Agriculture |

Special-state agent types: 15000 undeveloped, 15100 abandoned, 15300 construction, 15400 damaged,
15410/15411 rubble building/filler, 15420/15421 ashes building/filler. Landmarks are 18000–18045,
19999 (Quigley castle); business deals are 19000–19004; 20000–20002 are add-on/holiday
specials. [DATA]

## 5. Building families (`BUILDFAM.IXF`)

Key: `81F53D09:01F58D3D:n`, n = family number 1–31. [DATA]

Payload is 8-bit text, lines separated by CR LF:

1. First line begins with the 4-character tag `TXT!` followed by a free-text description.
2. `LandUse: <agent type>` — the agent type of the family (decimal).
3. `BuildingCount: <n>` — number of `Key:` lines that follow.
4. `n` lines `Key: <G>,<T>,<I>,<weight>   <comment>` — **note the order Group, Type, Instance**
   (decimal), then an integer weight. The comment after whitespace is ignored.
5. Trailing blank lines may follow.

[DATA] All 31 families parse this way; every member key uses Group 1406 and Type
`207EDC0E` (545184782) or `220055E1` (570447329). Weights are 1 in most families; the
"housing project" families 28–30 and scatter family 31 use weights 2, 3, 4 and 10.

| # | Agent type | Purpose (from description) |
|---|---|---|
| 1 | 3900 | Row houses (row-house rule) |
| 2 | 12310 | Airport runway pieces |
| 3 | 12310 | Airport buildings (airpark) |
| 4 | 3300 | Res low density medium LV |
| 5, 6, 18 | 8100, 8300, 8500 | Ind dirty low/medium/high density |
| 7, 9 | 5100, 5500 | Com low density L–M / M–H LV |
| 8, 23 | 5700, 6100 | Com medium density L–M / M–H LV |
| 10 | 3500 | Res estates |
| 11 | 3100 | Res low density low LV |
| 12 | 3900 | Res medium density medium LV |
| 13, 14, 15 | 4700, 4500, 4300 | Res high density high/medium/low LV |
| 16, 17 | 3700, 4100 | Res medium density low/high LV |
| 19, 20, 21 | 8900, 8700, 9100 | Ind clean medium/low/high density |
| 22, 24 | 6700, 6300 | Com high density M–H / L–M LV |
| 25 | 9300 | Farm |
| 26, 27 | 12330 | Seaport: seawall/pier; seaport yard |
| 28, 29, 30 | 3900, 4300, 4500 | Housing-project rule |
| 31 | 3500 | Low density high LV scatter |

How families are bound to zone developer rules: `15-sim-growth.md` §5.

## 6. Default building data (`SC3BuildingLayer.ini`)

Section `[DefaultBuildingData]`: keys `state:size:agentType[:variant]`, value = one or more
occupant keys `T:G:I` joined by `+` (one is chosen when several are listed [INFERRED: uniformly at
random]).

* `state`: 1 occupied, 2 abandoned, 4 under construction, 8 destroyed (rubble), 16 fire-damaged
  (ashes), 32 eaten (crops). [DATA from comments]
* `size`: lot edge length in tiles (1–5).
* `agentType`: 100 = any building; 3000/5000/8000 = residential/commercial/industrial, or a
  specific agent type (e.g. 6100, 6700). Lookup tries the specific agent type, then its
  ancestors, then 100. [INFERRED]
* `variant`: absent = the main (anchor) tile of the lot; `F` = filler tiles of a multi-tile lot;
  `G` = with accumulating garbage overlay.

Section `[BUILDING_TRACKING_GROUPS]`: agent types whose building counts are tracked (value
always 0). Used by statistics and scenario goals. [INFERRED]

## 7. CSATTRIB composite-sprite records

Key `T:G:I` with T = `00006300` (single-state) or `00006301` (multi-state), G = set type
(`00006400` base set, `552D8026` add-on, `6591AD34` German, `54AACF44` holiday), I = sprite
record ID. Payload begins with ASCII `BIN` + CR. Rendering use is specified in
`25-rendering.md`; the partial layout below is [INFERRED] from the shipped records (985 + 42 + 5 +
20 records; common sizes 324, 624, 984, 2184 bytes).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | char[4] | magic | `BIN` + 0x0D |
| 0x04 | 4 | u32 | version | 1 |
| 0x08 | 4 | u32 | flags | 0x7300 for most static records; other values (e.g. 0x7315, 0x7310) on animated/multi-state records [OPEN] |
| 0x0C | 1 | u8 | zooms | 5 (static) / 2 or 3 in some records [OPEN] |
| 0x0D | 1 | u8 | rotations | 4 (or 8, 0x20 in large records) [OPEN] |
| 0x0E | 1 | u8 | (unknown) | 4 / 8 / 0x30 |
| 0x0F | 1 | u8 | entriesPerState | 0x14 = 20 = 5 zooms × 4 rotations in the common case |
| 0x10 | 4 | u32 | states | 1 for static buildings |
| 0x14 | 2 | u16 | (unknown) | 0 |
| 0x16 | 2 | u16 | count | number of image entries (20 in the common case) |
| 0x18 | 1 | u8 | (unknown) | 0 |
| 0x19 | 14·count | entry[] | images | see below |
| … | … | | trailer | remaining bytes [OPEN] |

Image entry (14 bytes, packed): u16 index (0…count−1), u32 sprite archive type (e.g. 2 =
`Sprites/00000002_Residential.DAT`, `552D8025` = `Landmarks_2`), u16 index again, u32 sprite
instance (normally the sprite record ID), u16 0.


## 8. Building selection for a zone lot

Summary (full algorithm in `15-sim-growth.md` §4–§5): [STATIC]

1. The zone developer picks a **category** (agent type such as 3300) from the zone's category
   list using the tile's land value, water service and the zone's density
   (`[ZoneDeveloperDescriptions]`); for industry it first picks clean, dirty or agriculture from
   the year curve, education and ordinances.
2. It tries up to four of the developer **rules** bound to the zone (`SC3ZoneLayer.ini
   [ZoneDeveloperRules]`) in random order; each rule owns the building families of that category.
3. Family members are split into a filler list (attribute 0xCA bit 0x100), a 1×1 list and a
   larger-building list, each member repeated `weight` times; the rule fills the lot rectangle
   with corner buildings from the large list and per-tile picks from the 1×1 and filler lists.
4. Placed buildings start in the construction state and later become occupied.

Wealth = land-value band of the category (low/medium/high LV), density = the category's density
level, size = attribute 0x66, style = the active building set (§9). Landmarks (agent types
18000–18045, 19999) and business-deal buildings (19000–19004) are never chosen by the developer;
they are placed by the player once unlocked (`22-advisors-news-petitioners.md`,
`19-sim-economy.md`).

## 9. Building sets (American / European / holidays)

* The base set (`OccupantAttribs.IXF`, sprite set `00006400`) is always loaded.
* `OccupantAttribs_2.IXF` + `CSAttrib_2.IXF` hold the Unlimited landmarks/reward buildings.
* `OccupantAttribs_German.IXF` + `CSAttrib_German.IXF`: German landmarks.
* `OccupantAttribs_Holidays.IXF`, `HOLIDAYF.IXF` + `CSAttrib_Holidays.IXF`: holiday buildings;
  `HOLIDAYF.IXF` re-defines three existing occupant IDs (3032, 4027, 4034) with holiday sprites
  (sprite set `54AACF44`), i.e. it overrides base occupants while the holiday set is active.
* European versus American zone buildings are separate sprite archives under
  `Sprites/BuildingSets/<setID>/`; the occupant IDs are shared and only the appearance changes.
  [INFERRED]

## Persisted state

The occupant manager saves occupant instances, not this subsystem. The full byte layout is in
`10-save-format.md` §6.2. This section gives the building-level meaning of the fields.

* **Which occupants are saved**: every building, prop and tree in record `E0FAADC7:A0AB89F0:1`,
  with the count in `20631788:A0AB89F0:0`. Network pieces are saved separately by the network
  manager as 8-byte entries (placement word plus a piece word whose low 16 bits are the network
  occupant ID; `10-save-format.md` §6.2.6) [STATIC][DATA].
* **Identity**: the occupant attribute key, `207EDC0E:0000057E:<occupant ID>` for buildings and props
  or `220055E1:0000057E:<occupant ID>` for airport and seaport parts (flora is saved separately, in the
  flora layer of `10-save-format.md` §6.3). This key selects the attribute record of §2. The
  agent type (`CitySim.ini` `[AgentTypes]`) is stored next to it. If the key no longer resolves
  (a missing BAT or add-on building), the loader creates a generic occupant of that agent type with
  the stored footprint [STATIC].
* **Lot / position**: anchor tile (x, z), altitude (equal to the terrain vertex altitude at the
  anchor for ordinary placement), rotation 0–3, and footprint sizeX × sizeZ (1–63 tiles per side;
  1–5 shipped, always square in shipped files) [STATIC][DATA]. No separate lot rectangle is stored.
  It is anchor plus footprint.
* **Occupant kind** (small integer, not saved). Every occupant class exposes a fixed kind value
  used by several simulation tests. Building and facility classes report **1**. Other classes
  report other small integers (2 and 5 are attested). This is **not** the lot-state word below
  and is **not** attribute 0x72. The aura cell average (`17-sim-services.md` §8.4) counts a tile
  when the occupant query succeeds and the occupant is absent or kind 1. [STATIC]
* **State word** (u32). The low bits hold exactly one state: 1 occupied/functional, 2 abandoned,
  4 under construction, 8 destroyed (rubble), 16 fire-damaged (ashes), 32 eaten (crops; §6)
  [DATA values; meanings from `SC3BuildingLayer.ini`]. Bit 0x200 is an independent **protected**
  flag (shipped values 513, 514, 516): a zone developer never selects a lot that contains a building
  with this flag for redevelopment or replacement, whatever its state [STATIC]. Which events set
  the flag is `[OPEN]`. `15-sim-growth.md` §4 uses these definitions. Construction progress, age and
  condition of zoned buildings are **not** stored per occupant. Only this state word is stored.
* **Name**: a player-given name, saved only when it differs from the type's default name.
* **Build date** (year, day of year): saved only by classes that age, namely power plants, water
  facilities, garbage facilities, stadium, university, city hall, military base, golf course and
  similar [STATIC][DATA].
* **Per-class properties**: utilities, transit stations, fire and police stations, schools,
  hospitals, libraries, museums, jails, garbage facilities and stadiums save extra integer, float
  or string properties keyed by 32-bit property IDs. The table is in `10-save-format.md` §6.2.5.
  The power-plant fields are overworked-month counter, maximum life (months), decline age (months)
  and an expired/replaced flag [INFERRED]. The stadium saves its team name.
* Two per-occupant booleans are saved without a known meaning [OPEN]. One of them is 0 only on
  landfill tiles.

## Open questions

* Bits of attribute 0x72; meanings of 0x7B and of the 0xCA bits other than 0x100.
* Exact override order when an occupant ID is defined by more than one file.
* CSATTRIB record layout beyond the common header and image entries.

## Appearance replacement (city schemes and BAT buildings)

`Sys/SC3CityScheme.ini`, section `[ReplaceableBuildings]`, lists the occupant keys (decimal
`Type,Group,Instance=label`) whose appearance a player may replace with a BAT building. A replacement
changes only the drawing (`00006300:BA1DBF1E:<id>`). Every occupant attribute and simulation value of
the replaced building stays the same. See `24-bat-buildings.md` §2.5 for the full rules. [STATIC]

