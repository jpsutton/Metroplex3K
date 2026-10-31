# 10 — City, terrain, starter-town and scenario file formats

Status: working draft. Layers 1–3 (container, packed segment, inner record database) are complete and
verified on all 59 shipped files. Per-record payload layouts are given where decoded; the rest are
catalogued with size, version and owning subsystem and marked `[OPEN]`.

Related documents: `02-containers.md` (IXF container, RefPack), `03-resource-types.md`,
`11-world-terrain.md` (terrain record), `23-scenarios.md` (scenario scripts and texts).

## 1. Overview

A saved city (`.sc3`), starter town (`.st3`), terrain (`.sct`) and scenario (`.SNR`) all use the same
three-layer structure [DATA, 59/59 files]:

1. **Outer container**: an IXF resource container (see `02-containers.md`). It holds a handful of
   small, uncompressed records (city metadata, view position, preview thumbnail data and so on) plus
   one large compressed record.
2. **Packed segment**: the large record `035F62A4:035F62A4:00000001` holds a RefPack-compressed blob.
3. **Inner record database**: the decompressed blob is itself a small keyed database: a header, the
   simulation records concatenated, and a trailing directory of (Type, Group, Instance, offset)
   entries. Each simulation subsystem writes one or more records into it.

Shipped corpus used for verification: 15 `.sc3` (in `Cities/`), 10 `.st3` (in
`Cities/StarterTowns/`), 21 `.sct` (in `Cities/Terrains/`) and 13 `.SNR` (in `Cities/Scenarios/`).

Throughout, resource keys are written `Type:Group:Instance` (see `00-README.md`). Remember that the
IXF index stores each key on disk as Group, Instance, Type (see `02-containers.md`).

## 2. Primitive encodings used inside records

All records are produced by a single serial-record writer with these primitives [STATIC]:

| Primitive | Encoding |
|---|---|
| u8 / s8 | 1 byte |
| u16 / s16 | 2 bytes little-endian |
| u32 / s32 | 4 bytes little-endian |
| f32 | 4 bytes IEEE-754 little-endian |
| bool | 1 byte, 0 = false, 1 = true |
| string | u32 byte length N, then N bytes of 8-bit text, no terminator, no padding |
| resource key | 12 bytes: u32 Type, u32 Group, u32 Instance |
| raw block | N bytes copied verbatim (length known from context) |
| arrays | elements back to back, no count unless the record writes one explicitly |

There is no alignment or padding anywhere.

### 2.1 Versioned record header ("DEADBEEF header")

Most records begin with the same versioned header [STATIC][DATA]. Its length depends on flag bit 1,
so offsets after `flags` are relative:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | Record-specific layout version |
| 0x02 | 1 | u8 | flags | bit 0 = "marker strings present"; bit 1 = a second flag byte follows. Other bits unused |
| 0x03 | 0 or 1 | u8 | flags2 | Present only if flags bit 1 is set. Always 0 in shipped files; ignored by the reader |
| 0x03 or 0x04 | 4 | u32 | sentinel | 0xDEADBEEF. At 0x04 when flags bit 1 is set, at 0x03 when it is clear |

The game always writes bit 1 set and `flags2` = 0, so every header it produces is 8 bytes:
`vv vv 02 00 EF BE AD DE` (no markers) or `vv vv 03 00 EF BE AD DE` (markers) [DATA 59/59]. Because
`flags2` is always 0, some sections of this document read bytes 2–3 as one u16 "stream version"
whose value is 2 (no markers) or 3 (markers); that reading is equivalent for every shipped file.

**Marker strings.** Flag bit 0 states whether the record's own begin/end marker strings are present
(string encoding as above, e.g. `City Data Start` … `City Data End`, `DirtBag Start` … `DirtBag End`).
The header itself never contains or reads them. Each record places its markers itself, normally
directly after the header and as the last item of the payload; the record layouts in §3 and §6 show
where. A few records ignore the flag and never write markers (the crime layer, §6.10); this is stated
in their sections. Markers carry no data. A loader must skip them wherever the record layout says they
occur when bit 0 is set; a writer should write `flags` = 0x02 and omit them [DATA][STATIC]. Terrain
files (`.sct`) and `Seoul World Cup.SNR` were produced with markers on.

Reading rule [STATIC]: read version, `flags`, and `flags2` if bit 1 is set, then read the u32 that
follows. If it is not 0xDEADBEEF, the record is a legacy (pre-header) record: the loader rewinds to the
start of the record and parses it with version = 0 and flags = 0. No shipped file uses legacy
records.

## 3. Outer IXF records

### 3.1 Catalogue

Counts are "files containing the record / files of that kind". Sizes are in bytes [DATA].

| Key (T:G:I) | .sc3 | .st3 | .sct | .SNR | Size | Purpose |
|---|---|---|---|---|---|---|
| `035F62A4:035F62A4:00000000` | 15/15 | 10/10 | 21/21 | 13/13 | 20 | Packed-segment directory (§4.1) |
| `035F62A4:035F62A4:00000001` | 15/15 | 10/10 | 21/21 | 13/13 | 123 093 – 809 680 | Packed segment: compressed inner database (§4.2) |
| `00000FA1:617E198D:00000000` | 15 | 10 | 21 | 13 | 88 – 136 | City information (name, mayor, date, size …) (§3.2) |
| `C359837E:43598384:00000000` | 15 | 10 | 21 | 13 | 4 | File-kind/format word (§3.3) |
| `035AD1CB:835AD1D1:00000000` | 15 | 10 | 21 | 13 | 24 (st3: 4) | City state words (§3.4) |
| `E2F42628:62F42635:00000000` | 15 | 10 | 21 | 13 | 1 685 – 31 388 | Preview / city-summary block (§3.5) |
| `430E2EE6:530E2EE6:00000000` | 14 | 10 | 21 | 13 | 12 | Cheat log: count (§3.6) |
| `630E2EE6:730E2EE6:00000000` | 10 | 0 | 2 | 12 | 16 – 32 | Cheat log: IDs of cheats used (§3.6) |
| `79FDB6C8:59FDB6FD:79FDB71E` | 15 | 0 | 21 | 13 | 16 | Versioned u32 + string (§3.7) |
| `99F47014:79F4702D:00000000` | 15 | 0 | 21 | 13 | 16 | User building set (BAT replacements), empty in shipped files (§3.7) |
| `0525A6B9:4525A6E2:00000001` | 15 | 0 | 21 | 13 | 12 | Scheme selection: building set, flora set, landscape (§3.7) |
| `A1634F05:01E1A5E6:00000000` | 15 | 10 | 21 | 13 | 12 – 52 | City-view window state (§3.8) |
| `620EDFCE:A20FB3B9:00000000` | 15 | 10 | 21 | 13 | 24 – 70 | Isometric view position/zoom/rotation (§3.8) |
| `26E732E0:19A6CEA1:00000000` | – | – | – | 13 | 12 | Scenario descriptor (see `23-scenarios.md`) |
| `62B9DA24:19A6CEA1:00000001` | – | – | – | 13 | ≈74 840 | Scenario picture (BMP) |
| `2026960B:19A6CEA1:00000002` | – | – | – | 13 | 5 | Scenario short string |
| `04845CBE:C3DE4D66:00000000` | – | – | – | 13 | 133 – 191 | Scenario header text |
| `44EB6BC8:44EB6BC9:00000000` | – | – | – | 13 | 264 – 672 | Scenario text table |
| `A4846144:A48461A3:00000000` | – | – | – | 13 | 3 – 714 | Scenario text table |
| `23DFAE5F:23DFAE8C:<n>` | – | – | – | 1–6 each | 20 – 37 902 | Scenario goal scripts, Group = script number (see `23-scenarios.md`) |
| `2026960B:<group>:<n>` | – | – | – | 1 | 9 – 34 | Scenario-local string table (string type 2026960B; Group = table ID, Instance = string ID). Each of the 13 scenarios uses one group of its own; all shipped groups are of the form 0x?A47D??? or 0x3A4A6724 (e.g. `FA47D792` in `A Better Betterfeld.SNR`); a reader must accept any group other than `19A6CEA1` [DATA 13/13] |

Every shipped file contains the same outer-record set for its kind, except that `TUTORIAL.SC3` lacks
`430E2EE6:530E2EE6:00000000` and `630E2EE6:730E2EE6:00000000` is optional [DATA]. A loader must treat
every outer record other than the packed segment and its directory as optional.

Container details that matter to a reader [DATA]:

* Files written by the game (`.sc3`, `.st3`, `.sct`) preallocate the IXF index for 1024 entries, so
  the first record starts at offset 20 484 (4 + 0x5000); records follow back to back in index order
  with no gaps and nothing after the last record.
* `.SNR` files are produced by the separate Scenario Creator tool, not by the game. Their index is
  compact (first record at 544–1 564), they contain an unreferenced gap of about 20 KB and up to
  63 586 trailing bytes, and their string records (`2026960B:*:*`) have an index size 4 bytes smaller
  than the record (the size counts characters only; see `23-scenarios.md`). A reader must locate
  records only through the index and must size string records from their own length prefix.

### 3.2 City information `00000FA1:617E198D:00000000` (all kinds) [DATA 59/59][STATIC]

Versioned record, version 4 in all files. Written by the city object; read first on load.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 4 |
| — | var | string | marker | Only if header flag bit 0: `City Data Start` |
| +0 | 16 | s32×4 | appVersion | Version of the program that wrote the file: major, minor, build, revision. Shipped: 2.0.876–2.0.941 (cities, scenarios, terrains), 1.0.544 (starter towns) |
| +16 | 4 | u32 | (unknown) | 3 in all shipped files; **4** in files written by build 2.0.949 `[OBSERVED]`, whose appVersion is (2, 0, 949, 0). It comes from the same version object as appVersion, so it is probably a format revision [INFERRED]. Write 4 together with appVersion 2.0.949 |
| +20 | var | string | author | Login name of the user who saved the file (e.g. a short user ID). Informational only |
| … | 4 | u32 | mapWidth | City size in tiles along X: 128, 192 or 256 |
| … | 4 | u32 | mapHeight | City size in tiles along Y; equals mapWidth in all files |
| … | 4 | u32 | mapDepth | Third size value passed to map creation; equals mapWidth in all files [OPEN meaning]. Write mapWidth |
| … | var | string | cityName | City name |
| … | var | string | mayorName | Mayor name |
| … | 4 | u32 | dayOfYear | Current day within the year, 1-based (1–365) [INFERRED from values 1–364] |
| … | 4 | u32 | year | Current calendar year |
| … | 4 | u32 | gameSpeed | Simulation speed setting at save time, 1–4 [INFERRED] (see `14-sim-clock.md`) |
| … | 4 | u32 | foundingYear | Starting year chosen when the city was founded: 1900, 1950 or 2000 |
| … | 1 | u8 | flagA | Boolean, restored on load. One of the two New City options **Disasters** / **Auto Budget**: flagA and flagB are both 1 when both boxes are checked and both 0 when both are cleared `[OBSERVED]`. Which flag is which is [OPEN] [INFERRED: flagB = Auto Budget, since it is 1 in all shipped files]. Shipped: 1 in starter towns and a few cities, else 0 |
| … | 1 | u8 | flagB | Boolean, restored on load: the other of Disasters / Auto Budget (see flagA) `[OBSERVED]`. 1 in all shipped files |
| … | 1 | u8 | flagC | Boolean written from a global service; ignored on load [OPEN]; 0 or 1 |
| — | var | string | marker | Only if header flag bit 0: `City Data End` |

The map size read here drives the dimensions of every per-tile layer in the inner database. A loader
must therefore read this record before the packed segment.

### 3.3 File kind `C359837E:43598384:00000000` [DATA 59/59]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | s32 | fileKind | 3 = city (`.sc3` and `.SNR`), 4 = terrain (`.sct`), 5 = starter town (`.st3`) |

### 3.4 City state words `035AD1CB:835AD1D1:00000000` [DATA 59/59][STATIC]

No versioned header. The writer emits six values; the loader reads them in order and keeps
whatever it has read when the record ends early [STATIC]. The shipped starter towns (written by
program version 1.0.544) contain only the first field (4 bytes); all other files contain all six
(24 bytes) [DATA].

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | stateA | Restored into a city-level state word on load. The current program resets it to 0 immediately before writing, so every file it writes has 0. The 1.0.544 starter towns hold 1–10 here (one distinct value per town) [OPEN meaning] |
| 0x04 | 4 | u32 | cityId | 1 or 2 in shipped files. In cities created by build 2.0.949 it is a large value fixed when the city is created (e.g. 0x24F4FD33) and unchanged in later saves of that city. It grows with the real time at which the city was created `[OBSERVED]`, so it is a time-derived city identifier [INFERRED]. A new city should store a value derived from the current time |
| 0x08 | 4 | s32 | stateB | 0, 1 or 2 [OPEN] |
| 0x0C | 4 | s32 | stateC | 0 or 550–711 [OPEN] |
| 0x10 | 4 | s32 | stateD | Value from a global service; 1 in all files [OPEN] |
| 0x14 | 4 | s32 | stateE | Value from a global service; 54 in all files [OPEN] |

A writer should copy fields 1–5 from the loaded file (round-trip) and write 0 for field 0. Cities
created by build 2.0.949 store 0, *id*, 1, 54, 1, 54, where *id* is the creation stamp above
`[OBSERVED, 9 new cities]`; use that for new cities. A reader must accept a 4-byte record.

### 3.5 Preview record `E2F42628:62F42635:00000000` [DATA 59/59]

Used by the city-selection screens to show a summary without loading the city.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 16 | s32×4 | appVersion | As in §3.2 |
| 0x10 | 4 | u32 | population | City kind only (fileKind 3). Total population |
| … | 4 | s32 | funds | City kind only. Treasury in simoleons |
| … | 4 | u32 | (reserved) | City kind only. 0 in all files [OPEN] |
| … | 4 | u32 | dayOfYear | City kind only. As §3.2 |
| … | 4 | u32 | year | City kind only. As §3.2 |
| … | var | string | mayorName | City kind only |
| … | var | string | cityName | City kind only |
| … | 4 | u32 | imageVersion | Always 2 |
| … | 4 | u32 | packedSize | Size P of the image block below |
| … | 4 | u32 | width | Image width in pixels (cities/terrains: map width; starter towns: 162) |
| … | 4 | u32 | height | Image height (cities/terrains: map height; starter towns: 152) |
| … | 4 | u32 | pixelFormat | Pixel-format code (`04-graphics-formats.md` §1.1): 5 = RGB555, 7 = RGB565. Both occur in shipped cities; the code follows the display surface format at save time [INFERRED]. A writer should use 7 |
| … | P | bytes | image | u32 blockSize (= P, includes itself), then a RefPack stream that decompresses to width × height × 2 bytes |

For terrains and starter towns the city-only fields are absent: `imageVersion` follows `appVersion`
directly. Decision rule: city-only fields are present iff fileKind (§3.3) is 3 [DATA].

### 3.6 Cheat log `430E2EE6:530E2EE6:00000000` and `630E2EE6:730E2EE6:00000000` [DATA][STATIC]

Written by the application shell. Both are optional.

`430E2EE6:530E2EE6`:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 1 |
| 0x08 | 4 | u32 | value | **Not** a cheat count: present in 58/59 shipped files, including files with no logged cheats, with values 2–30 [DATA]. Meaning [OPEN]. Preserve on re-save. A brand-new city saved before any simulation has run omits the whole record `[OBSERVED]`; do the same |

`630E2EE6:730E2EE6`:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 1 |
| 0x08 | 4 | u32 | n | Number of IDs (loader caps at 1024) |
| 0x0C | 4·n | u32[] | cheatIds | IDs of cheat codes that have been used in this city (e.g. 0x62E8630B). See `27-platform.md` §5.1a for the phrase → ID table and the logging rule |

The record is omitted when no cheat has been used.

### 3.7 Other small records

`79FDB6C8:59FDB6FD:79FDB71E` (cities, terrains, scenarios; not starter towns):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 1 |
| 0x08 | 4 | u32 | value | 1 in all files; supplied by a global service [OPEN] |
| 0x0C | var | string | text | Empty in all files [OPEN] |

`99F47014:79F4702D:00000000` (cities, terrains, scenarios): **user building set**, i.e. the city's
BAT appearance replacements (`24-bat-buildings.md` §2.4.1) [STATIC][DATA]. No versioned header.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 12 | key | reserved | Always written as 00000000:00000000:00000000; read and ignored |
| 0x0C | 4 | u32 | count | Number of replacements |
| 0x10 | 24·count | (key, key)[] | replacements | Per replacement: the replaced building's occupant attribute key (`207EDC0E:0000057E:<occupant ID>` or `220055E1:0000057E:<occupant ID>`, one of the `[ReplaceableBuildings]` keys of `SC3CityScheme.ini`), then the replacement appearance key `00006300:BA1DBF1E:<buildingId>` |

Every shipped file has count 0 (16 zero bytes). A writer that has no replacements writes 16 zero
bytes. The original writes the pairs in its own sorted order [INFERRED: ascending by replaced key];
a reader must not depend on the order. On load each pair is applied in file order; when the
replacement appearance key cannot be found among the loaded resources (the building is missing from
`BAT.DAT`), the pair is applied with no appearance, so the building keeps its normal appearance
[STATIC for the lookup; INFERRED for the visible result]. Replacements are keyed by occupant key, so
they apply per occupant type, not per building instance [STATIC]: every building with that occupant key is drawn with the replacement.
"Import set" in the city-scheme dialog takes the set from another city file, i.e. from this record
of that file [INFERRED].

`0525A6B9:4525A6E2:00000001` (cities, terrains, scenarios): the city's **scheme selection**, three
u32 IDs, each 0 = the default entry [DATA 59/59: every shipped value is a key of the named section]:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | buildingSet | Key of `SC3CityScheme.ini` `[BuildingSets]` (e.g. 0xE523F3FB, 0x6523F3D5) |
| 0x04 | 4 | u32 | floraSet | Key of `[FloraSets]` (e.g. 0xC55A6D84; see `11-world-terrain.md` §6.3) |
| 0x08 | 4 | u32 | landscape | Key of `[LandScapes]` (terrain colouring scheme, e.g. 0xE55B8055) |

`[OBSERVED]` Mapping from New City dialog choices (icon positions, left to right) to stored IDs.
In each row, position 1 is the default and stores 0:

| Choice row | Position 2 | Position 3 | Position 4 |
|---|---|---|---|
| Landscape | 0xC55A6D62 (snowy terrain) | 0xC55A6D84 (desert terrain) | 0x655B8076 (sandy terrain) |
| Trees | 0xC55A6D62 | 0xC55A6D84 | 0x79EF435A |
| Buildings | 0xE523F3FB | 0x6523F3D5 | — |

The same 32-bit value can be a key in more than one section, so each field must be looked up in its
own section. Position 5 of the landscape and tree rows was not observed.

Round-trip them; an unknown ID should be treated as 0 [INFERRED].

### 3.8 View state `A1634F05:01E1A5E6:00000000` and `620EDFCE:A20FB3B9:00000000` [DATA][STATIC]

`A1634F05:01E1A5E6` (city-view window):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 2 |
| — | var | string | marker | If flag bit 0: `WinCityView Start` |
| +0 | 4 | u32 | value | 0 in all files [OPEN] |
| — | var | string | marker | If flag bit 0: `WinCityView End` |

`620EDFCE:A20FB3B9` (isometric view):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | §2.1, version 1 |
| — | var | string | marker | If flag bit 0: `WinCityViewIso Start` |
| +0 | 4 | f32 | centreX | View centre, x tile coordinate (`00-README.md` conventions) [INFERRED]; 0–256 range in shipped files |
| +4 | 4 | f32 | centreZ | View centre, z tile coordinate [INFERRED] |
| +8 | 4 | s32 | zoom | Zoom level in the numbering of `25-rendering.md` §2 (0 = farthest … 4 = closest) [INFERRED]. Shipped values 0–3 (0: 1 file, 1: 35, 2: 19, 3: 4) [DATA 59/59] |
| +12 | 4 | s32 | rotation | View rotation 0–3 (`25-rendering.md`) [INFERRED]. Shipped values 0, 1, 3 [DATA] |
| — | var | string | marker | If flag bit 0: `WinCityViewIso End` |

## 4. Packed segment

### 4.1 Segment directory `035F62A4:035F62A4:00000000`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | codec | Default compression codec for segments: 0x67 (103) = RefPack. Loader accepts 0x65 or 0x67, rejects anything else [STATIC] |
| 0x04 | 4 | s32 | count | Number of packed segments (1 in all shipped files) |
| 0x08 | 12·count | key[] | segments | Key of each segment record, as (Type, Group, Instance) |

In all 59 files this is `67 00 00 00 01 00 00 00 A4 62 5F 03 A4 62 5F 03 01 00 00 00`, i.e. one
segment with key `035F62A4:035F62A4:00000001` [DATA]. Segment instances are allocated sequentially
from 1 [STATIC].

### 4.2 Segment record `035F62A4:035F62A4:00000001`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | codec | 0x67 (all shipped files) or 0x65. Both are RefPack (`02-containers.md` §6.5); no other value is accepted [STATIC] |
| 0x04 | 4+n | string | codecVersion | Codec version text (u32 length n, then n chars). Both codecs write `0.90` (n = 4); the loader ignores it |
| 0x08+n | 4 | u32 | packedSize | Size P of the packed block that follows |
| 0x0C+n | 4 | u32 | rawSize | Size of the decompressed inner database |
| 0x10+n | P | bytes | packed | Packed block (below) |

With the string `0.90` written by the game (n = 4) the three fields sit at 0x0C, 0x10 and 0x14.

Packed block (both codecs) [DATA 59/59 for 0x67; STATIC for 0x65]:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | blockSize | Equals P (the size includes these 4 bytes) |
| 0x04 | P−4 | bytes | refpack | One RefPack stream (`10 FB` + 3-byte big-endian raw size), see `02-containers.md` |

The RefPack header's size field equals `rawSize`. All 59 shipped segments decompress to exactly
`rawSize` bytes and consume exactly P−4 compressed bytes [DATA].

The "0x67" and "4" values that open the record are therefore the codec ID and the length of the codec
version string, and "0.90" is the codec's version text, not a game version.

Codec notes [STATIC][DATA]: both codec IDs use byte-identical framing, a u32 length (compressed
bytes + 4) followed by one RefPack stream, and a reader decodes them the same way
(`02-containers.md` §6.5). The game writes 0x67. No other compression is used for file data. On load,
the decompressed size must equal `rawSize`. The encoder used for writing is specified in
`02-containers.md` §6.4.

## 5. Inner record database (decompressed segment)

### 5.1 Framing

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | count | Number of directory entries |
| 0x04 | 4 | u32 | dirOffset | Offset of the directory from the start of the decompressed data |
| 0x08 | … | bytes | records | Record payloads, concatenated, no padding |
| dirOffset | 16·count | entry[] | directory | One entry per record |

Directory entry:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | type | Record Type |
| 0x04 | 4 | u32 | group | Record Group |
| 0x08 | 4 | u32 | instance | Record Instance |
| 0x0C | 4 | u32 | offset | Start of the record's payload in the decompressed data |

Rules [DATA, 59/59][STATIC]:

* `dirOffset + 16·count` equals the decompressed size exactly.
* Record sizes are not stored. Sort entries by offset; a record extends to the next record's offset,
  and the last record extends to `dirOffset`. Records are contiguous; the first starts at offset 8.
* The directory is **not** in payload order. The game writes it sorted by (Group, Instance, Type)
  ascending, compared as unsigned u32 values [DATA 59/59]. A reader must not rely on directory order;
  a writer that wants byte-identical output must use this sort (§8.3 step 3).
* Entries whose offset is not below `dirOffset` are ignored by the loader.

### 5.2 Record catalogue (payload order as written)

Every record in the 59 shipped databases is listed below, in the order in which the payloads are
laid out (which is the order in which the city object asks its simulator subsystems to save, §8).
Sizes in bytes over all shipped files; "Ver" is the DEADBEEF-header version ("–" = the record has no
versioned header). Kinds: C = `.sc3`, S = `.SNR`, T = `.st3`, R = `.sct` (terrain); a kind is listed
when at least one file of that kind contains the record. Subsystem ownership [STATIC];
sizes/versions/kinds [DATA].

Status: **D** = decoded (every byte typed, semantics documented), **P** = partially decoded (every
byte typed, the meaning of some fields `[OPEN]`; such fields must be preserved on round trip). No
record is opaque [DATA 59/59].

| # | Key (T:G:I) | Kinds | Size | Ver | Owner subsystem | Status | Layout |
|---|---|---|---|---|---|---|---|
| 1 | `206C6E7C:21737DE5:00000000` | CSTR | 35 343 – 140 335 | 3 | Terrain ("dirt": altitude, water, sea level) | D | `11-world-terrain.md` Persisted state; summary §6.1 |
| 2 | `E0FAADC7:A0AB89F0:00000001` | CSTR | 8 – 697 628 | 2 | Occupant manager: every building and prop instance (not flora, not network pieces) | D (some per-class property meanings open) | §6.2 |
| 3 | `20631788:A0AB89F0:00000000` | CSTR | 16 | 2 | Occupant manager counters (entry count, entry format) | D | §6.2.1 |
| 4 | `206C6E7C:2147C2DD:00000001` | CST | 1 392 – 153 768 | 4 | Network manager: surface-layer piece list | D | `12-networks.md` |
| 5 | `206C6E7C:2147C2DD:00000002` | CST | 1 128 – 80 848 | 4 | Network manager: pipe-layer piece list | D | `12-networks.md` |
| 6 | `206C6E7C:2147C2DD:00000003` | CST | 112 – 47 616 | 4 | Network manager: subway-layer piece list | D | `12-networks.md` |
| 7 | `206C6E7C:2147C2DD:00000000` | CSTR | 24 | 4 | Network manager header (three list counts) | D | `12-networks.md` |
| 8 | `406B1196:80AB8AB0:00000000` | CSTR | 16 400 – 65 552 | 3 | Flora (tree) layer: kind×16 + level per tile; the only persistent form of flora occupants | D | §6.3; `11-world-terrain.md` §6 |
| 9 | `206C6E7C:409FF3BA:00000000` | CSTR | 50 274 – 198 060 | – | Zone manager (zone type per tile, developer scan order and statistics) | D (element 0 of zoneTileCount OPEN) | §6.4 |
| 10 | `206C6E7C:409FF3BA:<z+1>` (12 records, I = 2,3,4,6,7,8,0A,0B,0C,0F,10,12) | CSTR | 4 | – | Zone manager: developer class per zone type | D | §6.4 |
| 11 | `E1F6ABE2:02963821:00000000` | CSTR | 12 | 1 | Disaster: tornado | P | §6.5 |
| 12 | `E1F6ABE2:C446F87C:00000000` | CSR | 44 – 76 | 1 | Disaster: whirlpool | P | §6.5 |
| 13 | `E1F6ABE2:45326359:00000000` | CSR | 36 – 40 | 1, 2 | Disaster: locust swarm | P | §6.5 |
| 14 | `E1F6ABE2:24889F78:00000000` | CSR | 36 – 84 | 1 | Disaster: space junk | P | §6.5 |
| 15 | `E1F6ABE2:4296380E:00000000` | CSTR | 12 | 1 | Disaster: riot | P | §6.5 |
| 16 | `E1F6ABE2:22963800:00000000` | CSR | 12 | 1 | Disaster: toxic cloud | P | §6.5 |
| 17 | `E1F6ABE2:C336F77C:00000000` | CSTR | 48 | 1 | Disaster: earthquake | P | §6.5 |
| 18 | `E1F6ABE2:621CDA33:00000000` | CSTR | 12 | 1 | Disaster: fire | P | §6.5 |
| 19 | `E1F6ABE2:0336F76A:00000000` | CSTR | 60 – 108 | 1 | Disaster: UFO | P | §6.5 |
| 20 | `206C6E7C:21F6ABCA:00000000` | CSTR | 28 | 505 | Disaster manager (emergency state) | P | §6.5 |
| 21 | `E1F6ABE2:C4C90997:00000000` | CSR | 46 | 2 | Disaster: parade/celebration event | P | §6.5 |
| 22 | `C2910E7D:82937B60:00000000` | CSTR | 273 – 849 | – | Neighbours (neighbour cities, names, deals) | P | §6.6 |
| 23 | `206C6E7C:80F1E6D3:00000000` | CSTR | 32 800 – 131 104 | 2 | RCI valve-effect layer (two per-tile maps + R/C/I taxable-unit totals) | D | §6.7 |
| 24 | `41193C3A:E1193C2A:00000000` | CSTR | 256 | – | Ordinances | D | `19-sim-economy.md`; summary §6.8 |
| 25 | `206C6E7C:C259C02D:00000000` | CSTR | 1 174 – 4 246 | – | Aura (mayor-rating) layer: signed byte per 4×4-tile cell, sweep state, chart | D | §6.9 |
| 26 | `206C6E7C:C0A81498:00000000` | CSTR | 100 846 – 396 366 | 3 | Pollution layers (air/water per cell, standing garbage, sweep order, chart) | D | §6.10; `17-sim-services.md` §7.4 |
| 27 | `206C6E7C:20A7AE7F:00000000` | CSTR | 21 151 – 82 789 | 1 | Crime layer (crime and potential per 2×2-tile cell, sweep order, chart) | D | §6.10; `17-sim-services.md` §7.3 |
| 28 | `206C6E7C:00ABF2EC:00000000` | CSTR | 34 | – | Police coverage state | P | `17-sim-services.md` |
| 29 | `206C6E7C:A0F42214:00000000` | CSTR | 34 | – | Fire coverage state | P | `17-sim-services.md` |
| 30 | `206C6E7C:C106C4F5:00000000` | CSTR | 1 994 – 2 046 | 3 | Residential layer: valves 3000/3001, population cohorts, EQ/HQ/life expectancy, strikes, charts | D | §6.11 |
| 31 | `206C6E7C:20EC9849:00000000` | CSTR | 94 – 144 | 2 | Commercial layer: valves 5000/5001 | D | §6.11 |
| 32 | `206C6E7C:A106CF3D:00000000` | CSTR | 94 – 144 | 2 | Industrial layer: valves 8000/8001 | D | §6.11 |
| 33 | `206C6E7C:C0AB8A88:00000000` | CSTR | 4 258 – 16 546 | 1 | Land-value layer (half resolution) | P | §6.12 |
| 34 | `206C6E7C:029CA804:00000000` | CSTR | 102 573 – 409 773 | 9 | Traffic simulator | D | `18-sim-transport.md` |
| 35 | `E11BCC69:A11BCC54:00000000` | CSTR | 1 981 – 2 231 | – | Budget (funds, taxes, departments, bonds, histories) | D | `19-sim-economy.md` |
| 36 | `206C6E7C:E11BDDF6:00000000` | CSTR | 725 | 2 | World layer (`SC3WorldLayer.ini`: economic phases, five charts); **not** the aura | P | §6.13 |
| 37 | `206C6E7C:E0AFDF68:00000000` | CSTR | 668 | 1 | Utilities: power layer (production table, `power %` chart) | P | §6.14 |
| 38 | `206C6E7C:02BF0033:00000000` | CSTR | 212 – 8 404 | 4 | Utilities: water layer (service bitmap, `water %` chart, monthly totals) | P | §6.14 |
| 39 | `206C6E7C:61448030:00000000` | CSTR | 16 409 – 65 561 | 1 | Flammability layer | P | `17-sim-services.md`; §6.15 |
| 40 | `206C6E7C:01FD7A8C:00000000` | CSTR | 1 032 – 4 104 | 1 | Geometry spacing/reservation grid, one byte per 4×4-tile block (segment flag, point flag, overlap counter) | D (clients OPEN) | §6.3 |
| 41 | `013DEE82:022E288E:0000000B` | CSTR | 45 – 276 | 1 | Advisor topic list: utilities advisor | D | `22-advisors-news-petitioners.md` P.1 |
| 42 | `013DEE82:C28D0F40:0000000B` | CSTR | 66 – 192 | 1 | Advisor topic list | D | idem |
| 43 | `013DEE82:828D0A2F:0000000B` | CSTR | 66 – 263 | 1 | Advisor topic list | D | idem |
| 44 | `013DEE82:828D04EB:0000000B` | CSTR | 24 – 129 | 1 | Advisor topic list | D | idem |
| 45 | `022E288E:422E28E8:00000000` | CSR | 14 | 1 | Advisor manager: six booleans, one per advisor slot [INFERRED]; meaning OPEN | P | idem P.4 |
| 46 | `206C6E7C:C28D0B6E:00000000` | CSTR | 12 – 292 | 1 | Reward buildings state | D | idem P.3 |
| 47 | `013DEE82:C28D0B6E:0000000B` | CSTR | 24 – 150 | 1 | Advisor topic list | D | idem P.1 |
| 48 | `013DEE82:028D0FC5:0000000B` | CSTR | 87 – 150 | 1 | Advisor topic list | D | idem |
| 49 | `013DEE82:628D0C45:0000000B` | CSTR | 87 – 171 | 1 | Advisor topic list | D | idem |
| 50 | `206C6E7C:02619041:00000000` | CSTR | 24 – 1 569 | 1 | Petitioners (pending petitions) | D | idem P.2 |
| — | `206C6E7C:C3DE4D66:00000000` | (none shipped) | — | 1 | Scenario run-time state (goal/script progress) | D | `23-scenarios.md` §8 |

Notes on the catalogue:

* Instances of `206C6E7C:409FF3BA` other than 0 are numbered zone type + 1 (zone types in
  `15-sim-growth.md`); there is one per developed zone type.
* The record set differs by kind only as shown: terrains have no network lists (4–6) and an empty
  occupant record (8 bytes); starter towns lack several disaster records (12–14, 16 and 21 are
  `CSR` only, i.e. absent from `.st3`) and record 45. A loader must treat every record as optional
  and initialise the subsystem to its new-city state when its record is absent [DATA][INFERRED].
* `206C6E7C:C259C02D` is the aura (mayor-rating) layer, driven by `SC3Aura.ini`.
  `206C6E7C:E11BDDF6` is the world layer, driven by `SC3WorldLayer.ini` [STATIC, confirmed].
Subsystem order (load and save order) [STATIC]: terrain; occupant manager; network manager;
geometry layer; zone manager; disaster manager; neighbours; RCI development layer; ordinances; aura;
pollution; crime; police; fire; residential; commercial; industrial; land value; traffic; budget;
world layer; power; water; flammability; scenario; advisor manager; the six advisors; petitioners.
Records appear in the packed segment in this order (the table above). A few further subsystems
exist in the original but write no record in any shipped file and need no persisted state [DATA].

Parser coverage over the 59 files is reported in §9.

## 6. Inner record layouts

Conventions: W, H = map width and height in tiles from §3.2 (all shipped maps are square:
128, 192 or 256). "hdr" = the 8-byte versioned header of §2.1; "markers" = the optional begin/end
strings of §2.1 (present only when the header flag bit 0 is set; unless a section says otherwise
they directly follow the header and end the payload).
A "map layer" is a raw grid of cells written row after row with no header of its own; its cell
size and resolution are given per record. **Row order of every map layer and grid in this document**
(terrain, zone, flora, RCI, aura, crime, pollution, land value, flammability) [DATA 59/59]; the
spacing grid of §6.3 is too sparse to confirm from data and is assumed to follow the same rule
[INFERRED]:
a grid of R × C cells has one row per value of the first coordinate x (cell x = tile x ÷ cell edge)
and C cells per row, indexed by the second coordinate z. Rows are stored **last row first**: the
first stored row is x = R − 1 and the last is x = 0; within a row cells run z = 0 … C − 1. So cell
(x, z) is at element index (R − 1 − x)·C + z (for the vertex grids of the terrain, R = W + 1). The
coordinate names are those of `00-README.md` (Conventions). Verified by checking every grid against
the building anchors of §6.2.3 (for example all but one of 151 465 zoned-building anchors fall on a
zoned tile of the zone grid, and none of 280 734 building/prop anchors falls on a flora cell). Unless stated, a reader must reject a record whose
header version is greater than the version listed (§7).

### 6.1 Terrain `206C6E7C:21737DE5:0` — decoded

Full layout and semantics in `11-world-terrain.md` (Persisted state). Summary [DATA 59/59]:
hdr (version 3), markers `DirtBag Start`/`DirtBag End`, f32 scale constant, (W+1)·(H+1) u8 vertex
altitudes (rows stored last-to-first), u8 sea level, (W+1)·(H+1) u8 vertex water levels (same order),
W·H/8 bytes of per-tile bit flags (W·H/32 u32 words).

### 6.2 Occupant manager `E0FAADC7:A0AB89F0:1` and `20631788:A0AB89F0:0`

Status: decoded byte-exact; every byte of both records in all 59 shipped files is accounted for
(59/59 files, 8 225 274 bytes, 284 321 entries) [DATA]. Some property meanings remain `[OPEN]`
(§6.2.5).

The occupant manager stores every building (zoned, civic, landmark, reward, BAT) and prop. **Flora
(trees) is not stored here**: its only persistent form is the flora layer of §6.3. **Network pieces (road, rail, highway, power line, pipe,
subway) are not in this record**; they are stored, in a compact 8-byte form, in the network lists
`206C6E7C:2147C2DD:1..3` (§6.2.6) [DATA][STATIC]. Terrains contain an empty manager (8-byte header,
count 0).

#### 6.2.1 Counters record `20631788:A0AB89F0:00000000` (16 bytes, all files)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version 2, flags 2 |
| 0x08 | 4 | u32 | count | Number of entries in `E0FAADC7:A0AB89F0:1` (0–6 351 shipped) [DATA][STATIC] |
| 0x0C | 4 | u32 | entryFormat | Entry format; always 1. Entries are read only when it is 1 [DATA][STATIC] |

The loader reads this record **before** the entry record (it needs `count`). Only version 2 occurs
in shipped files and only version 2 may be written, exactly the 16 bytes above [DATA][STATIC].
The original also parses two other layouts of this record without error: version 1 (count followed by
five u32 counters) and version 3 (count, entryFormat). For both, the entry record is then **not**
read, so the city loads with no buildings, props or BAT buildings; networks, flora and all layers load
normally [STATIC]. No file with version 1 or 3 is known; the layouts are given only so that a reader
can reproduce this behaviour. Anything after `entryFormat` in a version-2 record is ignored.

#### 6.2.2 Entry record `E0FAADC7:A0AB89F0:00000001` (8 – 697 628 bytes)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version 2, flags 2 (markers never present in shipped files) |
| 0x08 | … | entry[count] | entries | `count` from §6.2.1, back to back, variable length |

Entry (minimum 24 bytes; 28 in the common case):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | place | Packed placement word, see §6.2.3 |
| 0x04 | 4 | u32 | flags | Footprint size, presence bits and two state bits, see §6.2.4 |
| 0x08 | 4 | u32 | agentType | Agent type of the occupant (the numeric type IDs of the `AgentTypes` hierarchy, e.g. 3300 = 0xCE4 residential class), used as a fallback when the key cannot be resolved (§6.2.7) [STATIC][DATA] |
| 0x0C | 12 | key | key | Occupant attribute key `T:G:I`: `207EDC0E:0000057E:<occupant ID>` for buildings/props, `220055E1:0000057E:<occupant ID>` for airport and seaport puzzle-zone parts (agent types 12310 and 12330 only) [DATA]. Flora is not stored here; see §6.3; resolves to the occupant attribute record (`13-buildings-occupants.md` §1.1) |
| … | var | string | name | Present iff flags bit 21. Player-given name; written only when it differs from the type's default name [STATIC] |
| … | 8 | s32,s32 | datePair | Present iff flags bit 22. Build date as (year, day of year, 1-based), e.g. (2000, 1), (1954, 117). Written only by classes that keep a build date (utilities, garbage facilities, some civic buildings and landmarks; §6.2.5) [STATIC][DATA] |
| … | 4 | u32 | state | Present iff flags bit 27 (always set by the writer). Occupant state word: 1 occupied/functional, 2 abandoned, 4 under construction, 8 destroyed (rubble), 16 fire-damaged (ashes); 0x200 bit combined (513, 514, 516) [DATA values; meanings per `13-buildings-occupants.md` Persisted state] |
| … | 4 | u32 | extra | Present iff flags bit 28. Never set in shipped files [OPEN] |
| … | 4+8·n | u32 n; n×(u32 id, u32 value) | intProps | Present iff flags bit 23. Per-class integer properties, sorted by ascending id (§6.2.5) |
| … | 4+… | u32 n; n×(u32 id, string) | strProps | Present iff flags bit 24. Per-class string properties, ascending id |
| … | 4+8·n | u32 n; n×(u32 id, f32 value) | floatProps | Present iff flags bit 25. Per-class float properties, ascending id |
| … | var | u32 n; n×(u32 id, object) | objProps | Present iff flags bit 26. Per-class serialisable sub-objects. Never present in shipped files [OPEN layout] |

The optional fields appear in exactly the order of the table (name, datePair, state, extra, intProps,
strProps, floatProps, objProps), which is **not** the bit order [STATIC][DATA].

#### 6.2.3 Placement word (`place`)

| Bits | Field | Meaning |
|---|---|---|
| 0–10 | x | Tile coordinate (first map axis), 0–2047 |
| 11–21 | z | Tile coordinate (second map axis), 0–2047 |
| 22–29 | y | Altitude of the occupant, in terrain altitude units (8 bits) |
| 30–31 | rot | Orientation 0–3 |

Verification [DATA]: of the 284 321 entries, 280 734 have keys of type `207EDC0E` (buildings and
props) and 3 587 have keys of type `220055E1` (airport and seaport parts, excluded from this check
because their altitude is levelled by the port). For 280 648 of the 280 734 building/prop entries, y equals the terrain vertex
altitude at vertex (x, z), where x selects the terrain vertex row and z the column in the terrain
record of `11-world-terrain.md` (whose rows are stored last-to-first, i.e. vertex (x, z) is at index
(W − x)·(W+1) + z of the stored altitude array). The same holds for 418 356 of 421 736 network
pieces; the exceptions are raised pieces (bridges, elevated sections). The anchor tile is (x, z);
the footprint covers sizeX × sizeZ tiles from it [STATIC; footprint direction INFERRED].

The same packing is used for network pieces (§6.2.6) [STATIC].

#### 6.2.4 Flags word (`flags`)

| Bits | Meaning |
|---|---|
| 0–5 | Footprint size along x, in tiles (1–5 shipped) |
| 6–11 | Footprint size along z, in tiles |
| 12–19 | Always 0 |
| 20 | Always 1 when written. A reader ignores it |
| 21 | name present |
| 22 | datePair present |
| 23 | intProps present |
| 24 | strProps present |
| 25 | floatProps present |
| 26 | objProps present |
| 27 | state present (always 1 when written) |
| 28 | extra present |
| 29 | Occupant boolean A, copied to and from the occupant [OPEN meaning]. 0 exactly for the 2 051 landfill entries (agent type 12850), 1 for all others [DATA] |
| 30 | Occupant boolean B, copied to and from the occupant [OPEN meaning]. 1 in all shipped entries |
| 31 | Always 0 |

#### 6.2.5 Per-class data (`state`, `datePair`, property lists)

There is no class tag in the entry. The occupant's class follows from its key (occupant attributes)
and agent type. Each class writes the common fields and then its own properties, identified by
32-bit property IDs [STATIC]. Every shipped entry falls into one of the signatures below
(284 321 entries) [DATA]. Agent-type names are from `CitySim.ini` `[AgentTypes]`.

| Entries | Classes (agent types) | Owner | Fields present | Property IDs → value [DATA ranges; meaning] |
|---|---|---|---|---|
| 275 260 | zoned buildings, props, most civic buildings | — | none | — |
| 3 030 | bus station 12210, rail station 12230, subway station 12360, subway/rail station 12361 | transport | intProps | `6352CCCF` ∈ {5, 10}; `6352CCD0` ∈ {5, 6, 10, 11, 12} [OPEN] |
| 2 190 | water pump 12810, water tower 12840, desalination plant 12830 | water | datePair, intProps | `8351FDF3` = two u16 (low, high) e.g. (1500, 1500), (300, 300), (1500, 904) [INFERRED: capacity, current output]; `8351FDF4` = two u16 (low 0–≈1200, high 2 tower / 3 pump) [INFERRED: high = facility type code; low OPEN]; `8351FDF5` ∈ {0, 1, 3} [INFERRED: water-source status of the facility]; `8351FDF6` bool [OPEN] |
| 521 (+82) | power plants 12111–12118 (and waste-to-energy 12853) | power | datePair, intProps | `2351FAF8` = consecutive overworked months (u16; 0–3); `2351FAF9` = maximum life in months; `2351FAFA` = decline age in months; `2351FAFB` = bool "life expired / plant replaced" [INFERRED from the power-plant model in `16-sim-utilities.md`] |
| 82 | waste-to-energy 12853 | power + garbage | datePair, intProps | the four power-plant properties plus `A352179A` (0–1 750), `A352179B` (0–52 500), `A352179C` (0–52 500) [OPEN] |
| 92 | water treatment 12820 | water | datePair, intProps | `0351D624` ∈ {0, 42 500, 60 000}; `0351D625`, `0351D626` 0–1 800 000; `0351D627` bool [OPEN] |
| 67 | incinerator 12852 | garbage | datePair, intProps | `E352C57C` (0–1 500), `E352C57D`, `E352C57E` (0–45 000), `E352C57F` bool [OPEN] |
| 126 | recycling centre 12851 | garbage | datePair, intProps | `4352C330` (45), `4352C331` (50 000), `4352C332` bool [OPEN] |
| 386 | fire station 13002 | fire | intProps | `2352C88D` = three bytes (b0, b1, b2) packed in the low 24 bits; `2352C88E` ∈ {30, 50}; `2352C88F` 30–51 [OPEN] |
| 304 | police station 13130 | police | intProps | `A352C8E8` = three bytes packed as above; `A352C8E9` ∈ {30, 50}; `A352C8EA` 28–60 [OPEN] |
| 408 | hospital 13310 | health | intProps, floatProps | `A352D404` (1 500), `A352D406` 49–60, `A352D407` 99–117; float `A352D405` (≈0.14) [OPEN] |
| 751 | K-12 school 13010 | education | intProps | `8352D5E9` 98–117, `8352D5EA` 29–36 [OPEN] |
| 103 | local college 13873 | education | intProps, floatProps | `2352D114` 147–175, `2352D116` 122–150; float `2352D115` [OPEN] |
| 350 | library 14130 | education | intProps | `A3544E00` 49–60 [OPEN] |
| 154 | museum 14140 | education | intProps | `63544FDD` 73–90 [OPEN] |
| 83 | jail 13881 | police | intProps | `0352FBBD` 0–360 [INFERRED: inmates], `0352FBBE` 60–90 [OPEN] |
| 22 | stadium 14600 | — | datePair, strProps | `43530181` = team name string |
| 283 | university 13871, military base 11002, city hall 10120, golf course 14370 and other civic/landmark types | — | datePair | — |
| 109 | parks, landmarks and other nameable buildings | — | name (± datePair) | — |

Rules [STATIC][DATA]:

* `state` is the occupant state word: 1 occupied/functional, 2 abandoned, 4 under construction,
  8 destroyed (rubble), 16 fire-damaged; bit 0x200 is an additional developer flag (shipped values
  513, 514, 516). Semantics in `13-buildings-occupants.md` (Persisted state).
* Property lists are written in ascending ID order. A writer must write every property its class
  owns; the class loaders read them by ID, and several stop reading at the first missing ID.
* `name` is written only when it differs from the occupant type's default name.

**Values written for newly placed buildings** `[OBSERVED]` (build 2.0.949, buildings placed on
1 January 1900 and saved immediately; property IDs as in the table above):

| Building (agent type) | Footprint | Flags word | Date pair | Integer properties | Float properties |
|---|---|---|---|---|---|
| Coal power plant (12111) | 4 × 4 | 0x68D00104 | (1900, 1) | `2351FAF8` = 0, `2351FAF9` = 732, `2351FAFA` = 586, `2351FAFB` = 0 (a second plant placed the same day: 0, 857, 685, 0) | — |
| Water pumping station (12810) | 1 × 1 | 0x68D00041 | (1900, 1) | `8351FDF3` = 1500, `8351FDF4` = 196608, `8351FDF5` = 3, `8351FDF6` = 0 | — |
| Police station (13130) | 3 × 3 | 0x689000C3 | none | `A352C8E8` = 1265437, `A352C8E9` = 30, `A352C8EA` = 30 | — |
| Fire station (13002) | 3 × 3 | 0x689000C3 | none | `2352C88D` = 13092632, `2352C88E` = 30, `2352C88F` = 30 | — |
| Hospital (13310) | 3 × 3 | 0x6A9000C3 | none | `A352D404` = 1500, `A352D406` = 50, `A352D407` = 100 | `A352D405` = 0.12 |
| Landmark, plopped (18044) | 5 × 5 | 0x68100145 | none | none | — |

Further newly placed buildings (founded 2000, saved immediately) `[OBSERVED]`:

| Building (agent type) | Footprint | Flags word | Date pair | Integer properties | Float properties |
|---|---|---|---|---|---|
| Wind mill (12118) | 1 × 1 | 0x68D00041 | (2000, 1) | `2351FAF8` = 0, `2351FAF9` = 1606, `2351FAFA` = 1285, `2351FAFB` = 0 | — |
| Water tower (12840) | 2 × 2 | 0x68D00082 | (2000, 1) | `8351FDF3` = 600, `8351FDF4` = 131072, `8351FDF5` = 0, `8351FDF6` = 0 | — |
| School (13010) | 3 × 3 | 0x689000C3 | none | `8352D5E9` = 100, `8352D5EA` = 30 | — |
| City college (13873) | 3 × 3 | 0x6A9000C3 | none | `2352D114` = 150, `2352D116` = 125 | `2352D115` = 0.25 |
| Library (14130) | 2 × 2 | 0x68900082 | none | `A3544E00` = 50 | — |
| Bus stop (12210) | 1 × 1 | 0x68900041 | none | `6352CCCF` = 5, `6352CCD0` = 5 | — |
| Oil power plant (12112) | 4 × 4 | 0x68D00104 | (2000, 1) | `2351FAF8` = 0, `2351FAF9` = 752, `2351FAFA` = 601, `2351FAFB` = 0 | — |
| Gas power plant (12113) | 4 × 4 | 0x68D00104 | (2000, 1) | `2351FAF8` = 0, `2351FAF9` = 976, `2351FAFA` = 781, `2351FAFB` = 0 | — |
| Nuclear power plant (12115) | 4 × 4 | 0x68D00104 | (2000, 1) | `2351FAF8` = 0, `2351FAF9` = 647, `2351FAFA` = 517, `2351FAFB` = 0 | — |
| Solar power collector (12117) | 4 × 4 | 0x68D00104 | (2000, 1) | `2351FAF8` = 0, `2351FAF9` = 1216, `2351FAFA` = 973, `2351FAFB` = 0 | — |
| Water treatment plant (12820) | 2 × 2 | 0x68D00082 | (2000, 1) | `0351D624` = 0, `0351D625` = 0, `0351D626` = 0, `0351D627` = 0 | — |
| Desalination plant (12830) | 3 × 3 | 0x68D000C3 | (2000, 1) | `8351FDF3` = 5000, `8351FDF4` = 327680 (high half 5), `8351FDF5` = 3, `8351FDF6` = 0 | — |
| Rail/subway station (12361) | 1 × 1 | 0x68900041 | none | `6352CCCF` = 10, `6352CCD0` = 10 | — |
| Small park (14311) | 1 × 1 | 0x68100041 | none | none | — |

Footprint sizes follow the flags word's low 12 bits, as in §6.2.4. Classes with a date pair are
exactly the ones that age (power and water facilities). Flags bit 22 (date present) is set only for
them; bit 25 (float properties present) is set only for the hospital and the college.

All have state 1. The power-plant pair (732, 586) and (857, 685) is (maximum life, decline age) in
months. They differ per plant because of the random lifetime spread of `16-sim-utilities.md` §2.3,
and they stayed unchanged over the following three game years, as did the overwork and expired
words (0). The pump's 1500 equals its rated capacity (1500 m³/month shown in the purchase
dialog). The meanings of the other values are [OPEN]; write them as observed for a new building.

#### 6.2.6 Network pieces (`206C6E7C:2147C2DD:1..3`)

Network pieces are serialised by the network manager (`12-networks.md` Persisted state) as a
compact 8-byte entry with entry format 1 [STATIC][DATA]. These entries are **not** references into
the occupant record. They are the pieces themselves, so no entry of `E0FAADC7` matches them.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | place | Placement word of §6.2.3 (x, z, altitude y, rotation) |
| 0x04 | 4 | u32 | piece | Piece word, below |

Piece word [STATIC; values DATA]:

| Bits | Label (label only; not needed by an implementation) | Meaning |
|---|---|---|
| 0–15 | `6355941D` | Network occupant ID. The piece's attribute key is `E223741F:A317745F:<ID>` |
| 16 | `6355941E` | Piece flag [OPEN] |
| 17–18 | `6355941F` | 2-bit piece field [OPEN] |
| 19–26 | `63559420` | 8-bit piece field [OPEN; non-zero only on a handful of raised surface pieces] |
| 27 | `63559421` | Piece flag [OPEN] |
| 28 | — | Occupant boolean A (as flags bit 29 of §6.2.4) |
| 29 | — | Occupant boolean B (as flags bit 30 of §6.2.4) |
| 30–31 | — | 0 |

Shipped values: the high half is 0x3000 in 421 636 of 421 736 entries (both booleans set, other
fields 0). It is 0x0000 in 61 entries, 0x0800 in 3, and 0x30xx with bits 16–26 set in about 30
surface pieces [DATA]. Footprint is always 1×1 and is not stored.

#### 6.2.7 Load and save procedure

Save [STATIC]: for every occupant held by the manager, in manager iteration order, the occupant
is asked to describe itself. Occupants that decline (none in practice) are skipped. Then the
entry is written and `count` is incremented. The counters record is written after the entry record.
Network pieces are not iterated here.

Load [STATIC]:

1. Read the counters record. If its version is not 2, no occupants are loaded.
2. For each of `count` entries, read the entry. If `entryFormat` ≠ 1, loading stops.
3. Resolve `key` to an occupant type and create the occupant from it.
4. If the key cannot be resolved (for example, a BAT or add-on building whose files are absent),
   create a generic occupant from `agentType` with footprint sizeX × sizeZ instead. If that also
   fails, skip the entry.
5. Apply `place`, the booleans, `state`, `name`, `datePair` and the class properties, then insert the
   occupant into the world at (x, z).

The occupant record must be loaded before the network lists, because pieces are inserted into the
same world grid. Network lists are loaded by the network manager.

BAT buildings [STATIC][INFERRED]: a BAT-made building is saved exactly like a shipped building.
Its key is `207EDC0E:0000057E:<its occupant ID>`. Its appearance (`00006300:BA1DBF1E:<id>`) is
**not** stored in the save. It is reached through the occupant attribute 0x67 of that occupant type
(`13-buildings-occupants.md`, `24-bat-buildings.md`). No shipped save contains a BAT building, so
this is not verified against data.

### 6.3 Geometry-subsystem layers `406B1196:80AB8AB0:0` (flora) and `206C6E7C:01FD7A8C:0` (spacing grid)

`406B1196:80AB8AB0:00000000` [DATA 59/59]:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version 3 |
| 0x08 | 4 | u32 | width | = W |
| 0x0C | 4 | u32 | height | = H |
| 0x10 | W·H | u8[] | cells | One byte per tile, cell (x, z) at index (W − 1 − x)·H + z, i.e. rows last-first like every map layer (§6 conventions) [DATA 59/59] |

**This is the flora (tree) layer.** Flora occupants (group `FFD30C03:80F48961`, see
`11-world-terrain.md` §6.1) are **not** stored in the occupant records of §6.2; this layer is their
only persistent form [STATIC]. (This is why non-zero cells overlap other occupants only at
background rates.)

Cell value: 0 = no flora on the tile. Otherwise **cell = kind × 16 + level**, i.e. high nibble =
flora kind, low nibble = density/size level [STATIC][DATA]:

| kind (high nibble) | Flora family | level 0–5 → occupant instance | level 6–7 → instance (one of two, see below) | level 8–9 → instance |
|---|---|---|---|---|
| 0 | none | — (no object) | — | — |
| 1 | coastal wetlands | level 0: 0x1B8E (7054); levels 1–5: 0x1B8E or 0x1B8F (7054/7055, one of two) | 0x1B8E or 0x1B8F (7054/7055) | 0x1B91 (7057, burned flora) |
| 2 | palm | 0x1B80 + level (7040–7045) | 0x1B86 or 0x1B8D (7046/7053) | 0x23F0 (9200) |
| 3 | deciduous / generic | 0x0006 + level (6–11) | 0x000C or 0x1B90 (12/7056) | 0x1B91 (7057) |
| 4 | pine | 0x1B58 + level (7000–7005) | 0x1B5E or 0x1B8C (7006/7052) | 0x23F1 (9201) |

For kind 1, level 0 is always 7054 and levels 1–7 all use the 7054/7055 pair. Kinds above 4 and levels above 9 are invalid:
on load no flora is created for such a cell [STATIC]. 9200/9201 are not defined in the shipped
`Occupant.ini` [DATA], so in practice the creation of those fails as well [INFERRED].

Shipped data uses only kinds 2, 3, 4 and levels 0–6. Distribution over all 59 files: kind 2
122 275 cells, kind 3 119 408, kind 4 30 456; level 0: 956, 1: 120 062, 2: 51 655, 3: 31 366,
4: 21 589, 5: 15 082, 6: 31 429. The maximum value is 0x46 [DATA 59/59].

**Save** [STATIC]: start from an all-zero W × H grid. For each flora object in the flora collection,
read its anchor tile (x, z) and its kind/level byte and store the byte at (x, z) if
0 ≤ x < W and 0 ≤ z < H (otherwise skip). When two flora objects share an anchor tile, the one
visited later wins. A city without flora writes all zeros.

**Load** [STATIC]:

1. Read the header. If the version is ≤ 2, skip the grid (no flora is created).
2. Read W and H. If they differ from the city's map size, the load fails.
3. Visit cells in storage order (x descending from W − 1, then z ascending). For each non-zero byte b: kind =
   b ÷ 16, level = b mod 16. If kind ≤ 4 and level ≤ 9, choose the occupant instance from the
   table above, create that flora occupant, set its level to `level`, place it on tile (x, z) and
   add it to the flora collection.
4. Choosing between the two instances of a level 6–7 cell (and of kind-1 levels 1–7) uses one draw of
   the flora subsystem's own random generator, which returns 0 or 1 (the second instance is used
   for 1). A draw is taken for **every** valid non-zero cell, even when both table entries are equal.
   The generator is created at subsystem start with an unspecified seed `[INFERRED]` (not
   reproducible), so which of the two sprites a dense cell shows after reload may differ from the
   saved city. The cell value itself (kind, level) is always preserved.

Round-trip the grid verbatim when flora is not modified.

`206C6E7C:01FD7A8C:00000000` [DATA 59/59]: hdr (version 1), then a map layer of (W/4)·(H/4) u8
cells (one byte per 4×4-tile block), written by the geometry subsystem's quarter-resolution grid.
99.3 % of cells are 0. The other values are 1, 2, 3, 4, 8, 12, 16 and 20 [DATA].

This grid is a **spacing / reservation map** kept by the geometry subsystem for other subsystems.
It is not derived from flora (non-zero blocks hold no flora in most cases [DATA]). Callers give
positions in tiles. A position is mapped to the 4 × 4 block that contains it, and positions outside
the map are skipped. Each cell byte holds three independent fields [STATIC]:

| Bits | Field | Operations offered by the subsystem |
|---|---|---|
| 0 (0x01) | segment flag | *test segment*: true if any block within ±4 tiles (one block) of an axis-aligned segment between two tile positions has the flag set. *Reserve segment*: if the test is false, set the flag on all those blocks, otherwise fail. *Release segment*: clear the flag on the same blocks. *Test block*: the flag of a single block. |
| 1 (0x02) | point flag | *test point*: true if any block in the 3 × 3 block neighbourhood (±4 tiles) of a position has the flag. *Reserve point*: if the test is false, set the flag on the 3 × 3 neighbourhood, otherwise fail. *Release point*: clear it on the 3 × 3 neighbourhood. *Test block*: the flag of a single block. |
| 2–5 (0x3C) | overlap counter c (0–15) | *add*: if any block in the 5 × 5 block neighbourhood (±8 tiles) of a position already has c = 15, fail and change nothing; otherwise c ← c + 1 on all 25 blocks. *Remove*: c ← c − 1 on the 25 blocks (no underflow check). *Test block*: c ≠ 0. |
| 6–7 | unused | Always 0 in shipped data [DATA]; preserved by all operations [STATIC] |

The shipped values match this layout: 1, 2 and 3 are flag combinations, and 4–20 are c = 1–5
[DATA]. The reserve/add operations make sure two features of the same class keep a minimum spacing
or a maximum local density. **[OPEN]** which features use each field. The zone developer
(`15-sim-growth.md`) uses *add* on the counter while it auto-places a run of lot decorations along a
line, and adds the run only if *add* succeeds [STATIC]. The clients of the two flags were not
traced. Each client must call *release*/*remove* when its feature is deleted, so the grid stays
consistent with the city. An engine that does not model these clients should round-trip the grid
verbatim. A new city starts with all zeros [INFERRED].

Both records are consumed exactly in 59/59 shipped files [DATA].

### 6.4 Zone manager `206C6E7C:409FF3BA:*`

Owner: the zone manager (zone layer plus the zone developers' scheduler), see `15-sim-growth.md`.
Status: decoded; every byte consumed in 59/59 files by `savezone_parse` [DATA 59/59][STATIC].

**Instance 0** (50 274 – 198 060 bytes). It has **no versioned header** and no markers, even in
terrain files. Tile coordinates are (x, z) as in `00-README.md` (Conventions), both 0-based. W, H as in §6 conventions; N = W·H.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | N | u8[W][H] | zoneGrid | Zone type per tile (values in `15-sim-growth.md` §1). Stored as W rows of H bytes; the **first row stored is x = W−1** and the last is x = 0 (rows last-to-first, as in the terrain record); within a row, bytes run z = 0 … H−1 |
| +0 | 4 | u32 | nValve | Number of entries in the valve-commitment table (3 in cities in progress; 0 in every terrain and starter town and in 3 unplayed cities/scenarios: 34 of 59 files) |
| +4 | 8·nValve | {u32 valveId, s32 amount}[] | valveCommit | One entry per valve, in ascending `valveId` (3000, 5000, 8000). `amount` = running sum of the valve effects of zone lots that are occupied or under construction (see below) [STATIC]; may be negative |
| +… | 92 | u32[23] | failCur | Per zone type 0–22: number of developer visits in the **current** scan pass whose result code was 9–18 |
| +… | 92 | u32[23] | failPrev | Same, for the **previous** completed scan pass |
| +… | 4 | u32 | nPrev | Entry count of the previous-pass result table |
| +… | 6·nPrev | {u8 code, u8 zone, u32 count}[] | resultPrev | Visits per (result code, zone type) in the previous pass. Ascending by (code, zone). `code` < 19 and `zone` < 23, else the loader skips the entry |
| +… | 4 | u32 | nCur | Entry count of the current-pass result table |
| +… | 6·nCur | {u8 code, u8 zone, u32 count}[] | resultCur | Same, for the current pass |
| +… | 92 | u32[23] | zoneTileCount | Number of tiles of each zone type 1–22 (equals the count in zoneGrid in all files); element 0 is **not** the unzoned-tile count [OPEN meaning of element 0] |
| +… | var | scan | scanOrder | Developer scan-order state, layout below |
| +… | 4 | u32 | cursorX | Tile the developers visit next (x) |
| +… | 4 | u32 | cursorZ | Tile the developers visit next (z) |
| +… | 4 | u32 | okCur | Visits in the current pass with result code 1–8 |
| +… | 4 | u32 | okPrev | Same, previous pass |
| +… | 4 | u32 | failCurTotal | Visits in the current pass with code 9–18 (= Σ failCur) |
| +… | 4 | u32 | failPrevTotal | Same, previous pass (= Σ failPrev) |

Scan-order block `scan` (42 + 2·n bytes):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | s8 | state | 1 = pass in progress, 2 = pass complete (the next visit starts a new pass). 1 in all files |
| 0x01 | 4 | u32 | mode | 0 = shuffled tile list (all files). Non-zero = stride mode (below) |
| 0x05 | 1 | u8 | listValid | 1 = the tile list matches the rectangle (all files) |
| 0x06 | 4 | s32 | x0 | Scan rectangle: first x (0) |
| 0x0A | 4 | s32 | x1 | last x (W−1) |
| 0x0E | 4 | s32 | z0 | first z (0) |
| 0x12 | 4 | s32 | z1 | last z (H−1) |
| 0x16 | 4 | s32 | count | Tiles in the rectangle, (x1−x0+1)·(z1−z0+1) = N |
| 0x1A | 4 | u32 | n | Number of list entries that follow (= N in mode 0) |
| 0x1E | 2·n | u16[] | list | The pass's visiting order, **stored in reverse**: the first u16 in the file is list element n−1, the last is element 0. Each element = x·256 + z |
| +0 | 4 | s32 | index | Number of list elements already taken in this pass (next element to take) |
| +4 | 4 | s32 | stridePos | Stride-mode position (0 in all files) |
| +8 | 4 | s32 | strideStep | Stride-mode step (1 in all files) |

Rules [STATIC][DATA]:

* **Cursor.** (cursorX, cursorZ) is always the list element at index − 1 (verified 59/59). The
  developers process the cursor tile first, then take the next element.
* **Visiting.** Each developer tick processes a fixed number of tiles [OPEN: count per tick; see
  `15-sim-growth.md`]. For each: look up the zone type z of the cursor tile; if zone z has a developer
  (instance z+1 below), run its per-tile step (`15-sim-growth.md` §4.3), which returns a result code.
  A code of 1–18 is recorded: resultCur[(code, z)] += 1; codes 1–8 add 1 to okCur; codes 9–18 add 1
  to failCur[z] and to failCurTotal. Then, if the pass is complete, roll over (next point), and finally
  take the next list element as the new cursor.
* **Taking the next element** (mode 0): if index ≥ n, set state 2 and take nothing; else take
  element index, increment index, x = element ÷ 256, z = element mod 256; if index now equals n,
  set state 2.
* **Pass rollover** (state 2 when a tile has been processed): every valveCommit amount := 0;
  resultPrev := copy of resultCur, then every resultCur count := 0 (keys kept); failPrev := failCur, failCur := 0; okPrev := okCur,
  okCur := 0; failPrevTotal := failCurTotal, failCurTotal := 0; then a new pass starts: state 1,
  index 0, the list is rebuilt if its length differs from count, and it is **re-shuffled**.
* **List build and shuffle.** Build: for x from x0 to x1, for z from z0 to z1, append x·256 + z.
  Shuffle (every new pass, including the first): for i = 1 … n−1 in increasing order, j = r mod (i+1),
  swap elements i and j, where r is the next value of the C-runtime `rand()` generator of the original
  (Microsoft LCG: seed := seed·214 013 + 2 531 011 mod 2³², r = (seed ÷ 65 536) mod 32 768) — **not**
  the simulation RNG of `14-sim-clock.md`. Because r < 32 768, positions above 32 767 only ever swap
  with the first 32 768 positions (a bias that a faithful clone must keep). The original never seeds
  this generator [STATIC], so it starts from the C runtime's initial seed 1 in each thread
  [INFERRED: Microsoft C runtime behaviour]. It is a single generator shared by every part of the
  program that uses the C-runtime `rand()` on the same thread (for example the audio cell choice of
  `06-audio.md` §5.2, disasters, services and the user interface), so the shuffle depends on all
  earlier draws in that thread [INFERRED]. Exact reproduction is therefore impractical; an
  implementation stays compatible because the list is saved.
* **Stride mode** (mode ≠ 0; unused in shipped files): on each take, index += 1; if index > count the
  pass is complete; else stridePos := (stridePos + strideStep) mod count, x = x0 + stridePos mod
  (x1−x0+1), z = z0 + stridePos ÷ (x1−x0+1). The step is chosen from a size-dependent table [OPEN].
* **Valve commitments.** The table is the developers' **committed demand** `P` of
  `15-sim-growth.md` §4 (one accumulator per R/C/I valve). When a building enters or leaves the city
  while its lot state is occupied (1) or under construction (4), for each table entry:
  amount += s·m·e, where e = the building's per-tile valve effect for that valve
  (`15-sim-growth.md` §3.3), m = the building's footprint in tiles, X × Y, computed as a **signed
  8-bit product** (so footprints above 127 tiles wrap), and s = +1 on add, −1 on removal [STATIC].
  At every pass rollover (below) every amount is reset to 0. Because removals of buildings added
  before the last rollover are still subtracted, the amount can be negative (Madrid, Spain.sc3 holds
  −37 961) [STATIC][DATA]. The developers read it as `P` when they decide whether to build
  (`15-sim-growth.md` §4).
* Removing a building also resets to 0 any zone-22 tiles under its footprint [STATIC].
* **Loading.** The cursor is reset to 0 if it is not inside the map. If zoneTileCount cannot be read
  the loader recounts it from the grid [STATIC]. A writer should emit exactly the layout above, both
  result tables in ascending key order.
* New-city values (all terrains and starter towns, and the 3 other files with nValve = 0): nValve = 0;
  the previous-pass table holds 91 keys, every count 0; the current-pass table is empty (nCur = 0);
  all counters 0; index 1; mode 0; state 1 [DATA 34/34]. The record size is then
  3N + 900 bytes (111 492 for 192 × 192, 197 508 for 256 × 256). The smallest record in the
  catalogue (50 274 bytes) is a played 128 × 128 city, not a new one.

**Instances z+1** for each zone type z that has a developer (z = 1,2,3,5,6,7,9,10,11,14,15,17), 4 bytes,
no header [DATA 59/59]. The writer creates one per zone type that has a developer; the developer
itself then appends nothing (records are 4 bytes in all files). On load, the record is applied only if
the stored class equals the class of the developer currently configured for that zone
(`SC3ZoneLayer.ini [ZoneDeveloperDescriptions]`) [STATIC]:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | developerClass | Developer class ID for zone type z: 0x619BA64E residential, 0x41A3ADC1 commercial, 0xE1A53B30 industrial, 0xC1F81E7E airport, 0x82D2D72B seaport, 0x82348DE5 landfill (`15-sim-growth.md` §2) |

### 6.5 Disasters `E1F6ABE2:<disaster>:0` and manager `206C6E7C:21F6ABCA:0` — decoded (detail words open)

Owner: disaster subsystem. Every record begins with hdr (§2.1); the version
column is the highest version a reader accepts. Semantics and behaviour: `21-disasters.md`
Persisted state. Verified by parsing: [DATA 59/59, every record consumed exactly].

**Active-instance lists** (moving or burning disasters; empty in every shipped file, so the element
layouts are [STATIC] only). Body after hdr: s32 n, then n elements:

| Group | Disaster | Ver | Element (in order) | Size |
|---|---|---|---|---|
| `621CDA33` | fire | 1 | u32, u32, s32 | 12 |
| `02963821` | tornado | 1 | u32, u32, f32, f32, f32, s8, f32, u32, u32 | 33 |
| `4296380E` | riot | 1 | u32, u32, s32, u32, u32 | 20 |
| `22963800` | toxic cloud | 1 | u32, u32, s32, f32, f32, f32, u32, u32 | 32 |

The first two u32 of each element are the instance's tile position (x, z) [INFERRED]; the rest is
type-specific state (`21-disasters.md`) [OPEN in detail].

**History lists** (one entry per past occurrence). Body after hdr: u32 n (1 ≤ n ≤ 50 000), then n
fixed-size entries, each an array of u32 whose first word is the date (Julian day number, see
`14-sim-clock.md`):

| Group | Disaster | Ver | Entry size | Notes |
|---|---|---|---|---|
| `C336F77C` | earthquake | 1 | 36 (9 × u32) | |
| `0336F76A` | UFO | 1 | 48 (12 × u32) | |
| `24889F78` | space junk | 1 | 24 (6 × u32) | |
| `45326359` | locust swarm | 2 | 24 (6 × u32) | Version 2 inserts one s32 between the hdr and n (59 in the only v2 file; [OPEN]) |
| `C446F87C` | whirlpool | 1 | 32 (8 × u32) | |

In a brand-new city saved before any simulation has run, every list is empty (n = 0, and the
locust record carries its extra s32 = 0) `[OBSERVED]`. An empty list receives a seed entry
(holding only a Julian-day date, all other words 0) the first time that disaster's random
occurrence check runs [STATIC][DATA]. Observed in a city founded on 1 January 1900:
* the earthquake and UFO lists gained their seed entry dated 8 February 1900;
* the space-junk list was still empty in 1903, consistent with its 1960 year gate
  (`21-disasters.md`).

A loader must accept n = 0. Entries written by real occurrences carry non-zero
detail words (shipped examples: space junk (date, 26, 0, 0, 6, 181); UFO (date, 17, 5, 5, 0, 0, 0,
0, 0, 5, 325, 0)). Detail-word meanings are **[OPEN]**; round-trip them verbatim. The same five
types may also write a companion record with Group + 1 (e.g. `E1F6ABE2:24889F79:0`) holding their
active instances (u32 count, then self-serialised instance objects); no shipped file contains one
[STATIC].

**Parade/celebration `E1F6ABE2:C4C90997:0`** (version 2, 46 bytes; absent from `.st3`):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | Version 2 |
| 0x08 | 4 | u32 | a | 1 in 48 files, 17 in one [OPEN] |
| 0x0C | 1 | u8 | b | 0 [OPEN] |
| 0x0D | 4 | u32 | c | 0 [OPEN] |
| 0x11 | 4 | u32 | x | Parade start tile x [INFERRED] |
| 0x15 | 4 | u32 | z | Parade start tile z [INFERRED] |
| 0x19 | 1 | u8 | d | 0, 8, 16 or 24 [OPEN: likely a direction code] |
| 0x1A | 16 | s32[4] | route | (x1, z1, x2, z2) of the route; equals (x, z, …) when set. 22 shipped files hold uninitialised filler (0xCDCDCDCD) here when no parade was ever placed; a writer should write 0 |
| 0x2A | 4 | u32 | e | Present only if version ≥ 2. 0 or a large count [OPEN] |

**Manager `206C6E7C:21F6ABCA:0`** (28 bytes): hdr version **505** (0x01F9), then five u32
(reserved, emergencyActive, flagA, disasterCost, flagB) — see `21-disasters.md`. Terrains hold
0xCDCDCDCD in disasterCost (uninitialised); a reader must accept any value [DATA].

### 6.6 Neighbours `C2910E7D:82937B60:0` — decoded (deal fields partly open)

Owner: neighbour/deal manager. No versioned header, no markers. Semantics in
`19-sim-economy.md` §10, §12 and its Persisted state. Verified by parsing all shipped files.
[DATA 59/59].

Connection kinds used below (same enumeration as `12-networks.md` §8): 1 road, 2 rail, 3 power
line, 4 highway, 5 water pipe, 6 subway.

**1. Four neighbour blocks**, in neighbour-index order 0–3. Neighbour index by map edge [STATIC]:
0 = the edge x = 0, 1 = the edge z = 0, 2 = the edge x = W−1, 3 = the edge z = H−1 (x is the first
map coordinate, 0…W−1; z the second, 0…H−1). Corner tiles: (0, 0) → 0, (W−1, 0) → 1,
(W−1, H−1) → 2, (0, H−1) → 3.

| Size | Type | Field | Meaning |
|---|---|---|---|
| var | string | cityName | Neighbour city name (new city: a random entry of string table `C29A6083`, instances 41–76, distinct across the four) |
| var | string | mayorName | Neighbour mayor name (new city: distinct entry of the same table, instances 2–37) |
| 4 | u32 | population | Neighbour population (new city: 2000 + RandInt(0, 12999), drawn from the neighbour subsystem's own instance of the simulation generator of `14-sim-clock.md` §8 [INFERRED], which is seeded from the clock, so values are not reproducible) |
| 4 | u32 | populationCap | 4 000 000 in every shipped file and at creation [STATIC: constant; INFERRED: growth ceiling] |
| 1 | u8 | exists | 1 = a neighbour city exists on this edge. At creation it is cleared when the tile at the middle of that edge fails a terrain test [INFERRED: edge midpoint under water] |
| 4 | u32 | nRoad | Number of road connections to this neighbour |
| 4 | u32 | nRail | Number of rail connections |
| 4 | u32 | nPower | Number of power-line connections |
| 4 | u32 | nPipe | Number of water-pipe connections |
| 4 | u32 | nHighway | Number of highway connections |
| 4 | u32 | nSubway | Number of subway connections (always 0) |
| 6 | u8 × 6 | dealMade[6] | Boolean per deal type 0–5 (`19-sim-economy.md` §10.1): set to 1 when a deal of that type with this neighbour is accepted [STATIC] |

Note the count order (road, rail, power, pipe, highway, subway) differs from the kind numbering.

**2. Two deal lists**: first the **active deals**, then the **pending offers**. Each list is
`u32 n` followed by n deal entries:

| Size | Type | Field | Meaning |
|---|---|---|---|
| var | string | neighbourName | Partner city name (copy of that neighbour's cityName) [DATA 59/59] |
| var | string | text | Display text template; shipped files hold a placeholder string [DATA] |
| 4 | s32 | penalty | Cancellation penalty currently due [INFERRED; changes over the life of a deal] |
| 4 | s32 | monthlyAmount | Simoleons per month paid or received [DATA: for sell types = trunc(quantity × rate / BASE)] |
| 1 | u8 | neighbourIndex | 0–3 (§1 above) [DATA 59/59: consistent with neighbourName] |
| 1 | u8 | dealType | 0–5 (`19-sim-economy.md` §10.1) |
| 4 | s32 | monthCounter | Small month counter (1–58 seen; advances with the date and wraps) [OPEN] |
| 4 | s32 | quantity | Units per month [INFERRED] |
| 4 | s32 | rate | For sell types the rate per `BASE_*` units; for buy types equals monthlyAmount in shipped files [DATA][OPEN meaning for buy types] |
| 4 | u32 | sizeOrTerm | 9–12 in shipped deals [OPEN: k of §10.2 or term in years] |
| 4 | u32 | year | A calendar year (1951–2053 in shipped deals) [INFERRED: contract start or end year] |

Shipped files contain 0–2 active deals and 0–2 pending offers.

**3. Six connection sets**, each `u32 n` followed by n u32 values, in this order: rail, road,
pipe, power, highway, subway. Each value is a packed map-edge tile **x·65536 + z** (x in the high
16 bits, z in the low 16 bits) [STATIC; DATA: with this reading the per-edge set sizes match the
neighbour counts]. Values are written in ascending numeric order (sorted set, no duplicates)
[DATA 59/59]. The per-neighbour counts of §1 equal the number of set entries on that neighbour's
edge in 1 396 of 1 416 (file, kind, neighbour) cases; in four shipped files (Berlin, Fall of the
Wall, Frankfurt, Frankfurtfest) some counts are lower than the set sizes, never higher. A loader
must keep both as stored and not recompute one from the other [DATA].

### 6.7 RCI development layer `206C6E7C:80F1E6D3:0` — decoded

Owner: the valve layer of the growth subsystem (it applies `SC3ValveLayer.ini`
effects, `15-sim-growth.md` §3.3). Every byte consumed in 59/59 files by `savezone_parse`
[DATA 59/59][STATIC]. Size = 8 + 2·W·H + 24 (32 800 / 73 760 / 131 104 bytes).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version 2 (markers bracket the payload when flag bit 0 is set) |
| 0x08 | N | u8[W][H] | effectMap | Per-tile valve-effect map of the **last completed** update cycle. Same row order as the zone grid of §6.4 (first row stored is x = W−1, bytes within a row run z = 0 … H−1) |
| +0 | N | u8[W][H] | effectAcc | The same map being accumulated for the cycle in progress. All zero in every shipped file |
| +0 | 4 | u32 | resUnits | Last completed cycle: residential total (see below) |
| +4 | 4 | u32 | comUnits | Last completed cycle: commercial total |
| +8 | 4 | u32 | indUnits | Last completed cycle: industrial total |
| +12 | 4 | u32 | resAcc | Accumulators for the cycle in progress (0 in all files) |
| +16 | 4 | u32 | comAcc | |
| +20 | 4 | u32 | indAcc | |

Semantics [STATIC]:

* During an update cycle, every building whose occupant state is "occupied" contributes, for each
  entry `valve:supply:demand` of its agent type in `SC3ValveLayer.ini [AgentValveEffects]`
  (`15-sim-growth.md` §3.3):
  * for valve 3000, 5000 or 8000: on every tile of its footprint, effectAcc := min(255, max(0,
    effectAcc + supply)); and if the building's agent type belongs to that valve's family, the
    matching accumulator (resAcc, comAcc or indAcc) += footprint tiles × supply;
  * for valve 7000 (jobs): on every footprint tile, effectAcc := clamp(effectAcc + demand, 0, 255).
  * The accumulators and the map do **not** use the building's 0/1 service multiplier (the valve
    supply/demand sums do, `15-sim-growth.md` §3.3).
* At the end of the cycle (after the valve updates): effectMap := effectAcc, effectAcc := 0;
  (resUnits, comUnits, indUnits) := (resAcc, comAcc, indAcc), accumulators := 0. Saves are taken
  outside the cycle, so the accumulating fields are always zero on disk.
* resUnits / comUnits / indUnits are the "N" of the monthly R/C/I tax revenue formula in
  `19-sim-economy.md` (taxable units per class). effectMap is a display/diagnostic layer [INFERRED].
* A writer may write effectAcc and the accumulators as zero.

### 6.8 Ordinances `41193C3A:E1193C2A:0` — decoded

u32 count (42 in all files), then count × { u32 ordinanceId, u8 enacted, u8 available }. No header.
See `19-sim-economy.md`.

### 6.9 Aura layer `206C6E7C:C259C02D:0` — decoded

This is the aura (mayor-rating) layer of `17-sim-services.md` §8; its tunables are in
`SC3Aura.ini`. [STATIC: the `SC3Aura.ini` loader, the aura sweep and this record's reader/writer
belong to the same object] The record has no versioned header and no markers. With
C = (W/4)·(H/4), size = C + 150 (1174 / 2454 / 4246 bytes for 128 / 192 / 256 maps).
[DATA 59/59]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | C | s8[C] | grid | Map layer, one cell per 4×4 tiles, (W/4) rows of (H/4) cells, last row first (§6 conventions); aura −127…127 |
| C | 1 | u8 | phase | Sweep phase 0–15 (a reader resets a value > 15 to 0) |
| C+1 | 4 | u32 | cursorX | Next anchor tile x (reset to 0 if ≥ W) |
| C+5 | 4 | u32 | cursorZ | Next anchor tile z (reset to 0 if ≥ H) |
| C+9 | 4 | s32 | sweepSum | Σ (cell value × counted tiles) so far in the current sweep |
| C+13 | 4 | u32 | sweepCount | Σ counted tiles so far in the current sweep |
| C+17 | 1 | s8 | cityAura | City aura from the last completed sweep |
| C+18 | 132 | chart | history | History chart (layout below); samples are cityAura + 127 |

**History chart (132 bytes)**, the same format as in §6.12 and §6.13: u32[12] monthly samples,
u32[10] yearly samples, u32[10] decade samples, then u32 scale. Index 0 is the newest sample in each
series [INFERRED from data]. Scale is the value given when the chart is created: 260 for the aura in
all files. A freshly created aura chart holds 127 in every sample. [DATA]

Untouched maps (terrains, fresh starter towns) store an all-zero grid, phase 0, cursor (4, 8), zero
sums, cityAura 0 and all-127 charts. [DATA]

### 6.10 Pollution `206C6E7C:C0A81498:0` and crime `206C6E7C:20A7AE7F:0` — decoded

Both are fully decoded and parse with exact length on **59/59** shipped files. [DATA] The
authoritative field tables are in `17-sim-services.md` §7.3 (crime) and §7.4 (pollution). Summary,
with *n* = W/2 (cells per side) and *T* = W (tiles per side):

* **Common conventions.** Header: the §2.1 header, here described as u16 recVersion followed by
  bytes 2–3 read as u16 streamVersion (2 = markers off; 3 = marker flag on, in `.sct` files and
  `Seoul World Cup.SNR`; see §2.1), u32 0xDEADBEEF. All grids are stored row by row with the **last row first**,
  little-endian. Both records embed the same **sweep-order block**: u8 state, u32 mode (0), u8 rebuild
  flag, five s32 (xMin 0, xMax *n*−1, zMin 0, zMax *n*−1, total *n*²), u32 length *n*², the cell
  permutation as *n*² u16 values (high byte = cell x, low byte = cell z) written in **reverse**
  element order, and then three s32 (index, stride position 0, stride 1). The block is 42 + 2*n*²
  bytes. Both also embed a 132-byte history chart (33 u32: 12 monthly, 10 yearly, 10 per-decade and
  a parameter that is 260 in all files).
* **Crime** (recVersion 1): hdr; *n*² u8 crime; u32 cursorX, u32 cursorZ (tiles); u32 sweepSum;
  u32 sweepCount; u8 avgCrime; chart; sweep-order block; *n*² s16 crime potential; u32 *k*; *k* ×
  (u32 occupant ID, s32 amount, u8 radius). The crime record ignores the marker flag: there are no
  start/end markers, even with streamVersion 3. Size = 203 + 5*n*² + 9*k* (*k* = 42–74).
* **Pollution** (recVersion 3): hdr; [streamVersion ≥ 3: u32 21 + `Pollution Layer Start`];
  *n*² u32 cell state (bits 0–14 water, 15–29 air, 30–31 flags); 37-byte garbage-store block
  (preserve as-is); *T*² u16 standing garbage per tile (bit 15 = landfill flag); 7 s64 garbage
  totals; u32, u32, u16, s32, s32, u32, u32; chart; 7 s64; 12 × 7 s64 history; u8; u32, u32, f32,
  u32, u32 cursorX, u32 cursorZ, 4 × s32 water-treatment sums; sweep-order block; u32
  landfillTiles; *n*² s32 water source; *n*² s32 air source; *n*² s16 garbage source; u32 *k*; *k* ×
  18-byte modifier entries (u32 ID, s32 air, s32 water, u8 rA, u8 rW, s32 garbage); four facility
  lists (water treatment 12820, incinerator 12852, WTE 12853, recycling 12851), each a u32 count
  followed by (u32 tileX, u32 tileZ) pairs; [streamVersion ≥ 3: u32 19 + `Pollution Layer End`].

The derived grids (crime potential, pollution sources) and the modifier tables can be rebuilt from
the occupants on load. The original rebuilds them only when the saved modifier table disagrees with
the INI file, so a writer must still emit them. [STATIC]

### 6.11 Valve records (growth) and residential layer — decoded

`206C6E7C:20EC9849:0` (commercial, version 2) and `206C6E7C:A106CF3D:0` (industrial, version 2):
hdr; start marker (if flag bit 0: `Commercial Layer Start` / `Industrial Layer Start`, directly
after the header); u32 n (= 2); n valve blocks; end marker (if flag bit 0: `Commercial Layer End` /
`Industrial Layer End`). Size 94 bytes without markers, 144 with [DATA 59/59].

Valve block (41 bytes) [DATA 59/59; semantics in `15-sim-growth.md` §3]:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | valveId | 3000, 3001, 5000, 5001, 8000, 8001 |
| 0x04 | 4 | s32 | supplyLast | Supply total of the last completed period |
| 0x08 | 4 | s32 | demandLast | Demand total of the last completed period |
| 0x0C | 4 | s32 | supplyAcc | Supply accumulator of the current period |
| 0x10 | 4 | s32 | demandAcc | Demand accumulator of the current period |
| 0x14 | 4 | f32 | factor1 | World-cycle factor |
| 0x18 | 4 | f32 | factor2 | Second factor |
| 0x1C | 1 | u8 | capPercent | Percent cap (≤ 100) |
| 0x1D | 4 | s32 | value | Current valve value V |
| 0x21 | 4 | s32 | supplyBase | |
| 0x25 | 4 | s32 | demandBase | |

`206C6E7C:C106C4F5:0` (residential layer, version 3) is decoded. It holds the hdr, u32 n (= 2),
the two valve blocks (3000, 3001), the 1 900-byte residential state below, and then the end marker
(present only when flags bit 0 is set). Record size is 1 994 bytes, or 2 046 bytes with markers.
[DATA 59/59; field order STATIC from the writer] Semantics are in
`20-sim-population.md`; the facility side is in `17-sim-services.md` §5–§6.

Residential state (offsets relative to the end of the second valve block):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x000 | 132 | chart | popChart | City population history (chart format of §6.9) |
| 0x084 | 132 | chart | eqChart | City EQ history (scale 150) |
| 0x108 | 132 | chart | leChart | City life-expectancy history (scale 89) |
| 0x18C | 1 | u8 | schoolStrike | 1 while schools are on strike |
| 0x18D | 1 | u8 | healthStrike | 1 while hospitals are on strike |
| 0x18E | 4 | u32 | schoolStrikeMonths | School strike month counter (kept after the strike ends) |
| 0x192 | 4 | u32 | healthStrikeMonths | Health strike month counter |
| 0x196 | 1 | u8 | schoolRating | School rating 0–4 |
| 0x197 | 1 | u8 | healthRating | Health rating 0–4 |
| 0x198 | 4 | u32 | patients | Hospital patients needed this month |
| 0x19C | 1 | u8 | collegeStrike | 1 while colleges are on strike |
| 0x19D | 4 | u32 | collegeStrikeMonths | College strike month counter |
| 0x1A1 | 1 | u8 | collegeRating | College rating 0–4 |
| 0x1A2 | 4 | u32 | schoolAgePop | People aged 7–18 (sum of rounded per-age counts) |
| 0x1A6 | 4 | u32 | collegeAgePop | People aged 19–24 |
| 0x1AA | 4 | u32 | schoolDesks | Σ desk capacity of all schools |
| 0x1AE | 4 | u32 | collegeDesks | Σ desk capacity of all colleges |
| 0x1B2 | 4 | u32 | hospitalBeds | Σ beds of all hospitals |
| 0x1B6 | 4 | u32 | museumCapacity | Σ `PopServed` of all museums |
| 0x1BA | 4 | u32 | libraryCapacity | Σ `PopServed` of all libraries |
| 0x1BE | 1 | u8 | unknown | 0 in all files [OPEN] |
| 0x1BF | 1 | u8 | birthTrend | 0 = rising, 1 = falling, 2 = steady |
| 0x1C0 | 360 | f32[90] | EQ | Education quotient by age 0–89 |
| 0x328 | 360 | f32[90] | HQ | Health quotient by age 0–89 |
| 0x490 | 360 | f32[90] | share | Population share by age 0–89 (sums to 1) |
| 0x5F8 | 360 | f32[90] | LE | Life expectancy (years) of the cohort at each age |
| 0x760 | 4 | u32 | teachers | Σ funded teacher count of all schools |
| 0x764 | 4 | u32 | professors | Σ funded professor count of all colleges |
| 0x768 | 4 | u32 | doctors | Σ funded doctor count of all hospitals |

The total is 0x76C = 1 900 bytes. The capacity, population and staff sums are recomputed each month
before use; a loader may take them as-is. Department funding percentages are not stored here; they
come from the budget record. In new maps, `share`, `EQ`, `HQ` and `LE` hold the initial distribution
of `20-sim-population.md` §3.1, all charts hold 0 with scales 10000/150/89, and every other field
is 0. [DATA]

### 6.12 Land value `206C6E7C:C0AB8A88:0` — partially decoded

[DATA 59/59][STATIC]:

| Size | Type | Field | Meaning |
|---|---|---|---|
| 8 | hdr | header | version 1 (no markers in any file) |
| (W/2)·(H/2) | u8[] | landValue | One byte per 2×2 block, (W/2) rows of (H/2) cells, last row first (§6 conventions); 0–255 (`15-sim-growth.md`) |
| 1 | u8 | phase | Scan phase (0–15) |
| 4 | u32 | cursorX | Scan position |
| 4 | u32 | cursorZ | Scan position (z) |
| 1 | u8 | averageLV | Average land value |
| 4 | s32 | sumA | [OPEN] |
| 4 | s32 | sumB | Sum of land values [INFERRED] |
| 4 | u32 | count | Number of cells summed [INFERRED] |
| 132 | u32 × 33 | history | 32 history entries + final u32 (260 in all files) [INFERRED: graph history, same shape as the aura trailer] |

### 6.13 World layer `206C6E7C:E11BDDF6:0` — partially decoded

This is the *world layer*: the object that loads `SC3WorldLayer.ini` (`JobToResFactor`,
`ExternalJobDemand`, `EconomicPhases`, `[ListOfWorldValveMapping]`) and owns the jobs valve. It is
**not** the aura (see §6.9). [STATIC] Its semantics belong to `15-sim-growth.md`.

725 bytes in every file, hdr version 2. [DATA 59/59]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x000 | 8 | hdr | header | Version 2 |
| 0x008 | 4 | s32 | v0 | 0 in terrains [OPEN] |
| 0x00C | 4 | s32 | v1 | 0 in terrains [OPEN] |
| 0x010 | 4 | s32 | v2 | 150 in all files [OPEN] |
| 0x014 | 12 | s32[3] | v3–v5 | 0 in all files [OPEN] |
| 0x020 | 4 | s32 | v6 | −512000 in played cities, 0 in terrains [OPEN; equals −2000 × 256] |
| 0x024 | 4 | s32 | v7 | 255 or 0 [OPEN] |
| 0x028 | 4 | s32 | v8 | 0 [OPEN] |
| 0x02C | 16 | s32[4] | phaseParams | Multiples of 256 (e.g. 25·256, 125·256, 10·256, 50·256) [INFERRED: economic-phase state in 24.8 fixed point; OPEN] |
| 0x03C | 1 | u8 | flag | 0 in all files [OPEN] |
| 0x03D | 660 | chart[5] | histories | Five history charts in the §6.9 chart format. Scales in a new map: 100000, 10000, 10000, 100, 10000 |
| 0x2D1 | 4 | u32 | last | [OPEN] (close to the newest sample of chart 5) |

The writer emits, in order: the header block (0x000–0x03C), the five charts and the final u32.
[STATIC for the order; field meanings OPEN] The five charts are, in order: city size, commerce,
industry, unemployment (percent, scale 100) and national (SimNation) population; see §6.19 for the
evidence [INFERRED].

### 6.14 Utilities `206C6E7C:E0AFDF68:0` (power) and `206C6E7C:02BF0033:0` (water) — decoded

Verified by parsing: [DATA 59/59, both records consumed exactly]. Semantics:
`16-sim-utilities.md` Persisted state.

**Power layer `206C6E7C:E0AFDF68:0`**. 668 bytes in every file, hdr version 1.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x000 | 8 | hdr | header | Version 1 |
| 0x008 | 4 | s32 | monthIndex | Row of the production table for the current month, 0–11 [DATA 59/59 range; INFERRED meaning] |
| 0x00C | 480 | s32[12][10] | production | 12 rows (one per month slot) × 10 columns (production categories), row-major [STATIC order] |
| 0x1EC | 40 | s32[10] | annualTotal | Per category: the sum of the 12 rows of that column [DATA 59/59] |
| 0x214 | 132 | chart | powerChart | `power %` graph history (§6.19); auto-scale (stored max = max(largest sample, 10) in 59/59) |
| 0x298 | 4 | u32 | tail | 0 in all files [OPEN] |

The 10 columns are the categories of the electricity chart (`09-ui-screens.md` §7.4: nine plant
types plus neighbour deals) [INFERRED; column order OPEN]. Per-plant state (build date, lifetime,
decline age, overwork months) is **not** in this record (see `16-sim-utilities.md` Persisted state).

**Water layer `206C6E7C:02BF0033:0`**. hdr version 4. Size = 212 + 4·n bytes
(212 – 8 404 in shipped files).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | Version 4 |
| 0x08 | 4 | u32 | firstWord | Index of the first stored word of the tile bitmap |
| 0x0C | 4 | u32 | n | Number of stored words; firstWord + n ≤ W·H/32 |
| 0x10 | 4·n | u32[n] | bits | Words firstWord … firstWord+n−1 of a W·H-bit per-tile bitmap (32 tiles per word, same word/bit order as the terrain tile flags of `11-world-terrain.md` [INFERRED]); the writer trims leading and trailing all-zero words (first and last stored word are non-zero; n = 0 when all are zero) [STATIC][DATA]. Bit set = tile has water service [INFERRED]. Words outside the stored range are 0 |
| +0 | 132 | chart | waterChart | `water %` graph history (§6.19); fixed scale, max = 100 in all files |
| +132 | 8 | s64 | supply | Total water produced this month [INFERRED] |
| +140 | 8 | s64 | used | Water consumed this month [INFERRED] |
| +148 | 16 | s64 × 2 | saleDeal | Water delivered to a neighbour sale deal this month, and a second accumulator [STATIC partial; 0 in all files] |
| +164 | 16 | s64 × 2 | purchaseDeal | Water drawn from a neighbour purchase deal this month, and that amount × deal rate / 1000 [STATIC partial; 0 in all files] |
| +180 | 4 | u32 | statA | [OPEN] (4 – 12 038 in shipped cities) |
| +184 | 4 | u32 | statB | [OPEN] (968 – 21 445) |
| +188 | 4 | u32 | meanPollution | Mean water-pollution value at the facility tiles this month (`16-sim-utilities.md` §3.3 step 5) [STATIC] |
| +192 | 4 | u32 | ageingCount | Number of facilities past their decline age this month (§3.3 step 6) [STATIC] |

Offsets marked + are relative to the end of `bits`. The facility list itself is **not** saved; the
water layer rebuilds it from the occupants on load [STATIC: no facility entries are written].

### 6.15 Flammability `206C6E7C:61448030:0` — partially decoded

hdr (version 1; loader rejects higher), a W·H u8 map layer (flammability per tile), then u32 × 4
and u8 (sweep/state fields, [OPEN]). Total 25 + W·H bytes [DATA 59/59]. See `17-sim-services.md`.

### 6.16 Police `206C6E7C:00ABF2EC:0`, fire `206C6E7C:A0F42214:0`

34 bytes, no header; layout in `17-sim-services.md` (u8, u32, u32, u8, u32 × 6) [DATA 59/59].

### 6.17 Advisors and petitioners

`013DEE82:<advisor>:0000000B` topic lists, `206C6E7C:02619041:0` petitions,
`206C6E7C:C28D0B6E:0` rewards and `022E288E:422E28E8:0` utilities-advisor flags: layouts in
`22-advisors-news-petitioners.md` (Persisted state) [DATA 59/59].

### 6.18 Networks, traffic, budget, scenario state

See `12-networks.md`, `18-sim-transport.md`, `19-sim-economy.md` and `23-scenarios.md` §8. All
parse 59/59 with `save_parse` except the scenario state, which no shipped file contains.

### 6.19 Graph-history block ("chart", 132 bytes) and where the 14 graph series are stored

There is **no separate graph-history record**. Each of the 14 series of the Graphs window
(`09-ui-screens.md` §7.3) is a small chart object owned by the subsystem that produces the data,
and it is written **inside that subsystem's record** as one 132-byte block [STATIC][DATA 59/59].
Every record that embeds a history uses this same block (it is also used by series that are not
shown in the Graphs window, such as the aura and treasury histories).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 48 | u32[12] | monthly | Last 12 monthly samples |
| 0x30 | 40 | u32[10] | yearly | Last 10 yearly samples |
| 0x58 | 40 | u32[10] | decade | Last 10 per-decade samples |
| 0x80 | 4 | u32 | max | Graph maximum (vertical scale) |

Rules [STATIC]:

* Samples are unsigned. Each array is a fixed-length FIFO: a new sample enters and the oldest
  leaves. Index 0 is the newest sample [INFERRED from data, see §6.9].
* A chart is either **fixed-scale** or **auto-scale** (a property set by the owning subsystem when
  the chart is created; it is not stored). Fixed-scale: `max` is the scale given at creation, and
  on load and on each new sample any sample greater than `max` is clamped to `max`.
  Auto-scale: after load and whenever a new sample exceeds the current `max`, `max` is recomputed
  as trunc(largest of the 32 samples × scale factor), but never below a floor (10 by default; the
  factor is 1.0 by default). A loader may simply take the stored `max`.
* Verified: the power chart (auto-scale) stores max = max(largest sample, 10) in 59/59 files; the
  water chart (fixed scale) stores 100 in 59/59 [DATA].
* A writer must emit all 33 words; a new chart holds the subsystem's initial sample in all 32
  slots (often 0; 127 for the aura) and its creation scale in `max`.

Location of the 14 series (title string instance IDs, hexadecimal, in table `029541F4`) [STATIC owner; DATA position]:

| ID | Series | Record | Position |
|---|---|---|---|
| 0x161 | city size | World layer `206C6E7C:E11BDDF6` | chart 1 of 5 (§6.13) |
| 0x163 | commerce | World layer | chart 2 |
| 0x164 | industry | World layer | chart 3 |
| 0x168 | unemployment (percent) | World layer | chart 4 |
| 0x165 | national (SimNation) population | World layer | chart 5 |
| 0x162 | residents | Residential layer `206C6E7C:C106C4F5` | popChart (§6.11) |
| 0x16E | education | Residential layer | eqChart |
| 0x16D | health | Residential layer | leChart |
| 0x166 | traffic | Traffic `206C6E7C:029CA804` | road-traffic history (`18-sim-transport.md`) |
| 0x167 | pollution | Pollution `206C6E7C:C0A81498` | (§6.10) |
| 0x169 | crime | Crime `206C6E7C:20A7AE7F` | (§6.10) |
| 0x16B | power % | Power layer `206C6E7C:E0AFDF68` | offset 0x214 (§6.14) |
| 0x16C | water % | Water layer `206C6E7C:02BF0033` | (§6.14) |
| 0x16F | land value | Land value `206C6E7C:C0AB8A88` | final 132 bytes (§6.12) |

The mapping of the five world-layer charts is [INFERRED from data]: chart 4 is the only chart with
scale 100 and holds 0–9 (a percentage, so unemployment; high confidence); chart 5 holds 35–71 million
and grows slowly in every city (national population; high confidence); chart 1 is the largest
city-level count (city size); charts 2 and 3 are commerce and industry in that order (medium
confidence: chart 3 exceeds chart 2 in the industrial and farming cities `Craterville.sc3`,
`Farmsville.sc3`).

## 7. Version handling on load

### 7.1 General rules [STATIC]

1. **No global file version.** Nothing in the file is checked against the program version. The
   `appVersion` fields (§3.2, §3.5) are informational; files written by 1.0.544 (starter towns) and by
   2.0.876–2.0.941 all load.
2. **Per-record versions.** Each versioned record carries its own layout version (§2.1). A loader
   reads the header, then parses the payload according to that version. Fields added in later
   versions are read only when the stored version is high enough; the writer always writes the
   current version (table 7.3).
3. **Legacy records.** If the four bytes after the version/flags are not 0xDEADBEEF, the record
   predates the header: the reader rewinds to the record start and parses it as version 0 with no
   flags. No shipped file contains such a record.
4. **Marker strings.** If flag bit 0 is set, the record's start and end marker strings are skipped
   at the positions its layout gives (§2.1); records documented as marker-free (crime, §6.10) have
   none even when the bit is set. Implementations should not validate their text.
5. **Missing records.** Before loading, every subsystem is initialised to its new-city state. The
   city loader then asks each subsystem, in subsystem order (§5.2), to load its records; a subsystem
   whose record is absent or fails to parse keeps its initial state and the load continues (the city
   loader ignores the subsystems' load results). Optional outer records (cheat log, view state, §3.7
   records) are likewise skipped when absent. A missing or unreadable city information record (§3.2)
   or packed segment (§4) aborts the load.
6. **Truncated records.** Readers stop at the first field that cannot be read and keep the values read
   so far (this is how the 4-byte state-word record of the 1.0.544 starter towns loads, §3.4).
7. **File kind.** A missing file-kind record (§3.3) is treated as kind 3 (city).
8. **Container codecs.** The segment directory and segment records accept codec 0x65 or 0x67 only
   (§4); the decompressed size must equal the stored raw size or the segment is rejected.
9. **Newer versions.** Some readers explicitly reject a header version higher than the one they
   write (flammability rejects > 1; networks accept exactly 4). An implementation should treat any
   record whose version is higher than the current version in table 7.3 as absent.

### 7.2 Load order [STATIC]

1. Read the city information record (§3.2); this fixes the map size.
2. Read the file kind (§3.3) and the state words (§3.4); create the map and all subsystems.
3. Apply name, mayor, date, speed, founding year and the two restored flags from §3.2.
4. Read `79FDB6C8:59FDB6FD` (§3.7).
5. Ask each simulator subsystem, in subsystem order (§5.2), to load its inner records from the packed
   segment. The occupant manager precedes the networks and all layers that refer to occupants.
6. Other components (application shell, views, scenario layer) then read their own records.

### 7.3 Versions written and accepted

"Accepted" lists what the original reads [STATIC]; "–" = no versioned header. Every shipped file
and every file written by the 2.0 builds uses exactly the "Written" version. Where the original also
reads an older version, the table says so; the older layouts are **not specified** unless given
here or in the owning section. **[OPEN]**: what are the older layouts (needed only for cities saved
by SimCity 3000 before Unlimited)? Until they are known, an implementation should treat a record with
an older version as absent (§7.1 rule 5) and report that the city may be incomplete.

| Record | Written | Accepted / version-dependent fields |
|---|---|---|
| `617E198D` city information | 4 | Layout of §3.2 read without a version test |
| `530E2EE6`, `730E2EE6` cheat log | 1 | — |
| `59FDB6FD` | 1 | — |
| `01E1A5E6` city-view window | 2 | — |
| `A20FB3B9` isometric view | 1 | — |
| `21737DE5` terrain | 3 | see `11-world-terrain.md` |
| `E0FAADC7:A0AB89F0` occupants | 2 | counters versions 1 and 3 load no entries (§6.2.1) |
| `20631788:A0AB89F0` | 2 | — |
| `2147C2DD` networks | 4 | exactly 4 (`12-networks.md`) |
| `406B1196:80AB8AB0` geometry layer | 3 | [OPEN] |
| `409FF3BA` zone manager | – | — |
| disaster lists `E1F6ABE2:*` | 1 (parade 2) | locusts: version ≥ 2 adds a leading s32 (1 and 2 both occur); parade: version ≥ 2 adds the trailing u32 |
| `21F6ABCA` disaster manager | 505 | [OPEN] |
| `82937B60` neighbours | – | — |
| `80F1E6D3` RCI layer | 2 | an older version is also read [OPEN layout] |
| `C0A81498` pollution | 3 | versions 1 and 2 are also read [OPEN layout] |
| `20A7AE7F` crime | 1 | version 0 is also read [OPEN layout] |
| `C106C4F5` residential | 3 | [OPEN] |
| `20EC9849`, `A106CF3D` valves | 2 | industrial: version 1 is also read [OPEN layout] |
| `C0AB8A88` land value | 1 | no version test |
| `029CA804` traffic | 9 | older versions accepted, fields by version (`18-sim-transport.md`) |
| `E11BDDF6` world | 2 | version 1 lacks the first field after the header (v0 at 0x08), so all later fields move 4 bytes earlier [INFERRED from position] |
| `E0AFDF68` power | 1 | [OPEN] |
| `02BF0033` water | 4 | older versions are also read [OPEN layout] |
| `61448030` flammability | 1 | rejects > 1 |
| `01FD7A8C` | 1 | — |
| advisor and petition lists | 1 | inner list version must be 1 (`22-advisors-news-petitioners.md`) |
| `C3DE4D66` scenario state | 1 | see `23-scenarios.md` §8 |

## 8. Writing a save

No checksum, CRC, signature or encryption exists at any level. The only integrity data are the sizes
in the IXF index, the segment header (packed and raw sizes) and the RefPack header [DATA 59/59;
STATIC]. `Apps/SimUtil2.dat` (100 000 high-entropy bytes) is not referenced by any module and plays no
part in saving or loading [STATIC]; an implementation ignores it.

### 8.1 Which files the game writes [STATIC]

The retail game writes only cities: Save and Save As write `<name>.sc3` in `Cities/` (dialogs in
`09-ui-screens.md` §3.3). The extension is the string `209E6378:0000008E` (`.sc3`); the save code also
loads `209E6378:0000008C` (`.sc2`) to recognise and replace that extension [INFERRED]. Terrains
(`.sct`) and starter towns (`.st3`) are only loaded by the retail game (the shipped ones were written
by internal builds, see the `author`/`appVersion` fields of §3.2), and scenarios (`.SNR`) are produced
by the separate Scenario Creator tool (`23-scenarios.md`). A city started from a terrain, starter town
or scenario is saved as an ordinary city: file kind 3 (§3.3), city-only preview fields present
(§3.5), and, for a scenario in progress, the scenario records plus the scenario run-time state
(`23-scenarios.md` §8).

### 8.2 Procedure [STATIC]

1. Notify all components that a save to the target path is about to start.
2. Create a unique temporary file in the program's working directory (`Apps/`): try the name
   `RZ_<n>.tmp` with n = the current time in seconds since 1970, creating it exclusively (fail if it
   exists); on failure decrement n and retry, up to 10 000 attempts. The empty file is closed again.
3. Open an IXF database on the temporary file for writing. The index is preallocated for 1024
   entries (0x5000 bytes), so the first record is written at offset 20 484 (`02-containers.md`).
4. Ask every component to write its records into the open file. They are written in this order, which is also the order of the
   IXF index entries and of the record data (back to back) in every game-written file [DATA]:

   | # | Record | Writer |
   |---|---|---|
   | 1 | `430E2EE6:530E2EE6:0` (§3.6; not a cheat count). Not written by build 2.0.949 in normal play `[OBSERVED]`: it is absent from six game-written saves, including saves made after three game years of simulation. It occurs only in the shipped files. A writer should omit it; a loader must accept either | application shell |
   | 2 | `630E2EE6:730E2EE6:0` cheat IDs (only if at least one cheat was used) | application shell |
   | 3 | `C359837E:43598384:0` file kind | city |
   | 4 | `035AD1CB:835AD1D1:0` state words (field 0 forced to 0) | city |
   | 5 | `E2F42628:62F42635:0` preview (§8.4) | city |
   | 6 | `79FDB6C8:59FDB6FD:79FDB71E` | city |
   | 7 | `99F47014:79F4702D:0` user building set (§3.7) | city scheme settings |
   | 8 | `0525A6B9:4525A6E2:1` scheme selection (§3.7) | city |
   | 9 | `00000FA1:617E198D:0` city information (§8.5) | city |
   | — | every simulator subsystem record, in subsystem order (§5.2), into the packed segment (§8.3) | subsystems |
   | 10 | `A1634F05:01E1A5E6:0` | city view |
   | 11 | `620EDFCE:A20FB3B9:0` | isometric view |
   | — | scenario records and `206C6E7C:C3DE4D66:0`, only while a scenario is active | scenario layer |

   If any subsystem reports a write failure, the save is reported as failed.
5. Close the database. Closing writes the segment directory record `035F62A4:035F62A4:0`, then the
   segment record `035F62A4:035F62A4:1` (§8.3), and finally the IXF index (entries in write order,
   then the all-zero terminator).
6. If the target file exists, rename it to a backup name formed by replacing the last character of
   the target path with `_` (for example `Madison.sc3` becomes `Madison.sc_`).
7. Copy the temporary file to the target path (failing if the target exists). If the copy fails,
   rename the backup back to the target. If it succeeds, delete the temporary file and the backup.
8. Notify all components that the save to the path has completed, and refresh the user interface
   (title bar, menus). The city's file kind becomes 3 for later saves.

Operating-system errors map to messages: file not found `015E410C:27`, path not found `2D`, name too
long `2A`, anything else `2E` (`09-ui-screens.md`). A modern implementation may replace steps 2 and
6–7 with "write to a temporary file in the target directory, then atomically replace", provided the
resulting file bytes are the same.

### 8.3 Building the packed segment [STATIC][DATA]

1. When the first subsystem record is written, a packed segment is started with the next free
   instance (1) under Type/Group 0x035F62A4. The inner database begins with the 8-byte header of §5.1,
   whose two words are filled in at step 3.
2. Each subsystem record is appended to the buffer as it is written; its key and start offset are
   remembered. Records are therefore laid out in write order, which is the subsystem order of §5.2.
3. On close, the directory is appended: one 16-byte entry (Type, Group, Instance, offset) per record,
   **sorted by (Group, Instance, Type) ascending** (true in all 59 shipped files). Then the two header
   words are filled in: count and directory offset.
4. On close of the file, the segment directory record of §4.1 is written first: u32 0x67, s32 1, key
   `035F62A4:035F62A4:1`.
5. The inner database is compressed with the RefPack encoder of `02-containers.md` §6.4, and the
   segment record of §4.2 is written after the directory record: u32 0x67, string `0.90`, u32 P,
   u32 raw size, then the packed block (u32 P followed by the RefPack stream). These two records are
   therefore the last two IXF index entries and the last two payloads in the file, directory before
   segment, in all 59 shipped files [DATA] (consistent with §8.2 step 5).

A writer that wants byte-identical output must reproduce the RefPack encoder exactly; any valid
RefPack stream loads correctly.

### 8.4 Preview record (§3.5)

Written by the city for the load dialogs [STATIC]: application version, then (cities only)
population, treasury, 0, day of year, year, mayor name, city name, then image version 2 and a
thumbnail of W × H pixels for a W × H city, 16 bits per pixel in the current display pixel format
(code 5 or 7), compressed like the segment block (u32 length + RefPack). The thumbnail is the city
overview map, one pixel per tile: pixel column x, row H − 1 − z shows tile (x, z).

Colour of tile (x, z) [DATA: reproduces every pixel of the 37 shipped .sc3/.sct thumbnails, up to
16-bit rounding; `tools/probes/probe_overview`]:

1. Zone type ≠ 0 with a colour below: that colour, even over water. 1/2/3 (0,255,0) (0,205,0)
   (0,148,0); 5/6/7 (0,0,255) (0,0,205) (0,0,148); 9/10/11 (255,255,0) (205,205,0) (148,148,0);
   14 (49,206,255); 15 (49,255,156); 17 (156,98,49); 22 (238,90,0).
2. Else water (106,105,156) when any of the tile's four corners has altitude ≤ its water surface.
3. Else land: d = alt(x, z) − water(x, z) of the tile's (x, z) corner selects a band starting at
   d = 1, 2, 4, 6, 8, 26, 52, 77, 103, 128, 154. Band colours per landscape (§3.7 scheme selection):

| Landscape | Band colours, lowest first |
|---|---|
| 0 (default) | (123,148,106) (139,157,106) (156,161,115) (156,149,115) (148,133,98) (131,113,90) (123,101,90) (115,101,90) (106,97,98) (115,109,115) (123,121,123) |
| `C55A6D62` | (213,214,222) (197,210,222) (164,186,213) (148,165,197) (131,145,180) (123,131,156) (115,115,131) |
| `C55A6D84` | (139,93,65) (156,117,74) (180,145,90) (189,165,106) (180,170,106) |
| `655B8076` | (139,117,41) (139,117,49) (139,115,49) (148,125,49) (139,121,49) (131,105,57) |
| `E55B8055` | (82,153,74) (74,137,65) (65,121,57) (57,117,57) (65,117,57) (82,117,57) |

Higher bands of the shorter rows do not occur in shipped files [OPEN]. Buildings, networks and
flora on unzoned tiles are not drawn. The navigator mini-map uses the same colours [OBSERVED]. The
Neighbors Map uses its own region image (`11-world-terrain.md` §9); its colour ramp equals the
default-landscape row above.

### 8.5 City information (§3.2)

Fields are written in order: application version (major, minor, build, revision of the running
program), the constant 3, the operating-system user name, map width, height and the third size value,
city name, mayor name, day of year, year, speed, founding year, the two flags restored on load, and a
third flag taken from a global service. A compatible engine should write its own version numbers
here; the loader ignores them.

### 8.6 Differences by file kind (what a reader must tolerate) [DATA]

| Kind | Differences from a city save |
|---|---|
| `.sc3` (city) | Reference layout. Cheat records optional; `TUTORIAL.SC3` lacks the cheat count |
| `.sct` (terrain) | File kind 4; marker strings on (flags 3) in most versioned records; preview without city-only fields; empty occupant manager (8 bytes); no network lists |
| `.st3` (starter town) | File kind 5; written by 1.0.544: no `59FDB6FD`, `79F4702D`, `4525A6E2` or cheat records; 4-byte state words; 162 × 152 preview without city-only fields; no records for several disasters or the utilities-advisor flags |
| `.SNR` (scenario) | File kind 3; extra scenario outer records (`23-scenarios.md`); container irregularities of §3.1 |

## 9. Parser coverage

The layouts in this document account for every byte of every shipped file: IXF index, every outer
record, the packed segment (header, RefPack), the inner header and directory, and every inner
record. Result: **59/59 files; 100 % of bytes typed** (95 019 710 bytes of outer records and
decompressed inner databases) [DATA]. No record is opaque.

The same layouts also parse, byte for byte, a city that the original program saved itself
`[OBSERVED]`. That file was a new 256 × 256 city at difficulty 1 (funds 50,000, 1 January 1900),
containing one plopped landmark, and was saved before the simulation was started. Its 11 outer
records and 58 inner records match this document in:
* write order (§8, apart from the omitted record of §3.6);
* index preallocation (data starting at 20 484);
* the directory sorted by (Group, Instance, Type);
* every per-record layout.

Its single occupant entry has flags 0x68100145 (footprint 5 × 5, state present), state 1, and no
date pair or property lists. This shows that a newly plopped landmark carries no optional data.

Per record (n = number of records over all files; status: D decoded, P every field typed but some
meanings open; X documented in another document):

| Record | n | Bytes | Typed bytes | Status |
|---|---|---|---|---|
| Segment record (header + RefPack, compressed) | 59 | 21 901 812 | all | D |
| Inner header + directory | 59 | 55 688 | 55 688 | D |
| `617E198D` city information | 59 | 6 496 | 6 496 | D |
| `43598384` file kind | 59 | 236 | 236 | D |
| `835AD1D1` state words | 59 | 1 216 | 1 216 | P |
| `62F42635` preview | 59 | 692 319 | 692 319 | D |
| `530E2EE6` / `730E2EE6` cheat log | 58 / 24 | 696 / 488 | all | D |
| `59FDB6FD`, `79F4702D`, `4525A6E2` | 49 each | 784 / 784 / 588 | all | P / D / D |
| `01E1A5E6`, `A20FB3B9` views | 59 each | 1 588 / 2 428 | all | P |
| `035F62A4:0` segment directory | 59 | 1 180 | 1 180 | D |
| `21737DE5` terrain | 59 | 7 456 821 | all | D |
| `E0FAADC7:A0AB89F0` occupants | 59 | 8 225 274 | all | D |
| `20631788:A0AB89F0` | 59 | 944 | 944 | P |
| `2147C2DD:0..3` networks | 59+37+36+16 | 3 376 016 | all | D |
| `406B1196:80AB8AB0` | 59 | 3 482 544 | all | P |
| `409FF3BA:0` zone manager | 59 | 10 507 806 | all | D |
| `409FF3BA:<z+1>` developer IDs | 708 | 2 832 | 2 832 | D |
| disaster lists (10 kinds) | 540 | 17 242 | all (blocks opaque) | P |
| `21F6ABCA` disaster manager | 59 | 1 652 | 1 652 | P |
| `82937B60` neighbours | 59 | 23 601 | 23 601 | P |
| `80F1E6D3` RCI layer | 59 | 6 965 088 | all | D |
| `E1193C2A` ordinances | 59 | 15 104 | 15 104 | D |
| `C259C02D` aura | 59 | 226 450 | all | D |
| `C0A81498` pollution | 59 | 21 042 944 | all | D |
| `20A7AE7F` crime | 59 | 4 390 545 | all | D |
| `00ABF2EC` / `A0F42214` police, fire | 59 each | 2 006 each | all | P |
| `C106C4F5` residential | 59 | 118 790 | all | D |
| `20EC9849` / `A106CF3D` valves | 59 each | 6 646 each | all | D |
| `C0AB8A88` land value | 59 | 879 958 | all | P |
| `029CA804` traffic | 59 | 21 770 207 | all | D |
| `A11BCC54` budget | 59 | 117 879 | all | D |
| `E11BDDF6` world | 59 | 42 775 | all | P |
| `E0AFDF68` power | 59 | 39 412 | all | P |
| `02BF0033` water | 59 | 174 432 | all | P |
| `61448030` flammability | 59 | 3 483 075 | all | P |
| `01FD7A8C` | 59 | 218 072 | all | P |
| `013DEE82:*` advisor topics | 413 | 38 114 | all | D |
| `422E28E8` utilities-advisor flags | 49 | 686 | 686 | P |
| `C28D0B6E` rewards, `02619041` petitions | 59 each | 4 572 / 22 251 | all | D |
| Scenario outer records (`19A6CEA1`, `C3DE4D66`, `A48461A3`, `44EB6BC9`, `23DFAE8C`) | 13–116 | 1 592 829 | strings only | X (`23-scenarios.md`) |

Records marked **P** have every field typed, but some field meanings are open. An implementation must preserve those fields
unchanged when it re-saves a city, and must initialise them as described in the owning subsection
when it creates a new city. The fields whose meaning is still open are:

* geometry layers `406B1196:80AB8AB0:0` and `206C6E7C:01FD7A8C:0` (§6.3);
* the world layer's header scalars (§6.13; chart identities are inferred, §6.19);
* some per-class property values in the occupant record (§6.2.5);
* deal penalty/month fields in the neighbours record (§6.6);
* the detail words of disaster history entries (§6.5);
* two water statistics and the order of the power categories (§6.14);
* the six advisor-manager booleans (record 45);
* minor scalars in the pollution, flammability, land-value, police and fire records.

## Open questions

1. Meanings of the open fields listed at the end of §9 (all byte layouts are complete).
2. §3.2: meaning of the constant 3 after `appVersion`, of the third map-size value, and of flags A–C.
3. §3.4: meanings of the six state words (field 0 held 1–10 in the 1.0.544 starter towns).
4. §3.7: meaning of the u32 and string of `79FDB6C8:59FDB6FD:79FDB71E` (1 and empty in every
   shipped file). (`99F47014:79F4702D` and `0525A6B9:4525A6E2` are now decoded.)
5. §3.8: semantics of the view fields (centre units, zoom/rotation encoding) and the window-view u32.
6. §8.1: exact extension-fixing rule for user-typed names (`.sc2` → `.sc3`).
7. §8.4: preview colours of land bands not present in shipped files (high land in non-default landscapes).
8. Whether the file kind written (§3.3) can be 4 when the game saves from the terrain editor; the
   retail save path observed always produces cities.
