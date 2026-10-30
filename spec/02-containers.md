# 02 — Resource containers

This document specifies the on-disk containers used by SimCity 3000 Unlimited: the IXF indexed
resource file (also used for sprite `.DAT` archives, cities, terrains, starter towns, scenarios and
BAT buildings), the `.SII` sprite sidecar text files, the `SYS.PAK` archive of packed INI files,
the INI grammar, RefPack compression, and the start-up resource discovery and lookup rules.

Payload formats of individual resource types are listed in `03-resource-types.md` and specified by
the owning documents (`04`, `05`, `06`, `08`, `10`, `13`, `23`, `24`, `26`).

## 1. Resource keys

A resource is identified by the triple **(Type, Group, Instance)**, three `u32` values. This spec
writes keys as `T:G:I` in hexadecimal (see `00-README.md`).

* **Type** selects the payload format and the factory that decodes it. Every Type ID found in the
  shipped data is registered in `03-resource-types.md`.
* **Group** is a namespace. By convention one IXF file holds one group (or a few); the group ID is
  frequently the hex prefix of the file name (`00000006_Vehicles.DAT`, `52DB60A0_Advisors.ixf`)
  and is the key of the file's line in `[DBSegments]` (section 7).
* **Instance** is the item ID inside the group (string number, image ID, building ID, …).

**Important: the IXF index stores the three key fields in the order Group, Instance, Type — not
T, G, I.** [STATIC] [DATA] The loader maps the first stored field to Group, the second to
Instance and the third to Type. Evidence: in every string-table file the third field is the
string Type `2026960B` and the first field is the constant group listed for that file in
`[DBSegments]`; the third fields of all data files are exactly the Type IDs listed in
`[ResourceFactories]` of `Default.ini`.

Example: the main city record of a `.sc3` file is stored on disk as the 12 bytes
`A4 62 5F 03 | 01 00 00 00 | A4 62 5F 03`, i.e. Group `035F62A4`, Instance `00000001`,
Type `035F62A4`; as a key it is `035F62A4:035F62A4:00000001`.

When a key is serialized *inside* a record payload by the generic record-stream "read key / write
key" operation, the order is **Type, Group, Instance** (three `u32`). [STATIC] Only the index uses
the G, I, T order. Individual payload formats may define their own key layouts; check the owning
document.

## 2. IXF indexed resource file

Used by: `*.IXF`, sprite `*.DAT` (including `BuildingSets/*/*.dat`, `FloraSets/*/*.dat`,
`BARender/BAT.DAT`), `*.sc3` cities, `*.sct` terrains, `*.st3` starter towns, `*.SNR` scenarios
and `*.bld` BAT buildings. [DATA] 649 shipped files carry the magic (536 `.ixf`, 37 `.dat`,
21 `.sct`, 17 `.bld`, 15 `.sc3`, 13 `.snr`, 10 `.st3`).

The original program calls one such file a *segment*.

### 2.1 Layout

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u8[4] | magic | `D7 81 C3 80` (the `u32` value `0x80C381D7`) |
| 0x04 | 20·k | entry[k] | index | Index slots, read sequentially until the terminator |
| … | 20 | entry | terminator | Group = Instance = Type = 0 |
| … | var | — | slack | Unused index capacity (see 2.4); contents are ignored |
| data start | var | bytes | data | Record payloads, located only through the index |

Index entry (20 bytes):

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | group | Group ID |
| 0x04 | 4 | u32 | instance | Instance ID |
| 0x08 | 4 | u32 | type | Type ID |
| 0x0C | 4 | u32 | offset | Absolute file offset of the payload |
| 0x10 | 4 | u32 | size | Payload size in bytes (see 2.3 for a caveat) |

### 2.2 Reading rules [STATIC]

1. Read 4 bytes. If they are not the magic, the file is not a segment; opening fails.
2. A zero-length file opens successfully as an empty segment.
3. Read 20-byte entries from offset 4 onward, one at a time:
   * If group, instance and type are all zero, the index ends. The offset and size fields of the
     terminator are not examined (they are zero in all shipped files [DATA]).
   * Otherwise, if offset = `0xFFFFFFFF` **or** size = `0xFFFFFFFF`, the slot is free and is
     skipped. Its key fields are not examined (shipped free slots are all-`FF` [DATA]).
   * Otherwise the entry is a live record.
4. If the end of file is reached before a terminator has been read, **opening fails** and no
   resources from that file are available. [STATIC] Five shipped files are in this state
   (`Res/Text/<LANG>/Sc3HolidaysStringsPetitionerTickerY.IXF` for DUTCH, English-UK, FRENCH,
   GERMAN, SWEDISH). None of them is referenced by `[DBSegments]`, so they are never loaded.
   [DATA]
5. **Duplicates inside one file:** if a key occurs more than once in one index, the **first** live
   entry in index order is kept and later ones are ignored. [STATIC] No shipped file contains
   a duplicate key. [DATA]
6. Index order carries no meaning. Entries are not sorted by key (220 files are unsorted) and
   not necessarily by offset (27 files, including all large sprite DATs and `OccupantAttribs.IXF`,
   have offsets out of order). [DATA]
7. The data region is not walked. Bytes not covered by any live entry (stale data, trailing
   bytes, slack in the index area) are ignored. [STATIC]

The index is read once, when the segment is opened; later changes to the file's index on disk
are not seen until the segment is mounted again. [STATIC] An implementation may keep the file open
or memory-map it.

### 2.3 Payload reads are not bounded by the size field [STATIC] [DATA]

A payload is read starting at `offset`, and the payload's own structure decides how many bytes
are read; reads are never checked against `size`. Shipped data depends on this:

* 2,863 string records (part of `Res/Text/ENGLISH`, and the text records inside the `.SNR`
  scenarios) have `size` equal to the string's character count, 4 bytes **less** than the actual
  payload (a `u32` length prefix followed by that many bytes). The decoder reads the length prefix
  and then the full string, running 4 bytes past `size`. The other 53,199 string records have
  `size` = character count + 4. [DATA]

An implementation must therefore let a payload decoder read past `size` (bounded by end of file),
or must compute the true extent from the payload's own length fields. Do not treat a mismatch as
corruption.

### 2.4 Index preallocation and data start [STATIC] [DATA]

The writer reserves index space in blocks of 1024 slots (1024 × 20 = 0x5000 bytes). For a segment
holding *n* index slots in use, data begins at

  **dataStart(n) = 4 + 0x5000 × (⌊n / 1024⌋ + 1) = 0x5004 + 0x5000 × ⌊n / 1024⌋**

so a file with up to 1023 records has its first payload at offset `0x5004` (20,484). This is why
even a tiny string table is about 20 KB: 20,484 bytes of header + index area, then the strings.
One slot of every block is effectively reserved for the terminator; when the 1024th slot would be
needed the index grows by another block (2.5.2).

Verified: of the 644 parseable shipped files, 630 have their lowest payload offset exactly at
`dataStart` (603 at `0x5004`, 14 at `0xA004`, and so on up to `0x5F004` for the largest sprite
DATs); the 13 `.SNR` scenarios use a tight index instead (2.6), and
`English-UK/TutorialOnGettingStarted.IXF` has its first live payload after stale data of deleted
records. [DATA]

Slack: bytes between the terminator and `dataStart` are normally zero, but 28 shipped files
(sprite DATs and `OccupantAttribs.IXF`) contain stale non-zero index entries there. Readers must
stop at the first terminator. [DATA]

Alignment: payloads are byte-packed with no alignment or padding between them (offsets mod 4 are
evenly distributed). [DATA]

### 2.5 How the original writes segments [STATIC]

All writes are made of four operations on a segment open for writing. The writer keeps a slot
count *n*, which starts as the number of live entries read when the segment was opened.

#### 2.5.1 Create

Opening a segment for writing opens or creates the file. If the file is empty, the 4-byte magic is
written. Nothing else is written at this point (no terminator yet).

#### 2.5.2 Add a record

To add a record with key *K* and payload *P*:

1. Let *D* = dataStart(*n*). If the file is shorter than *D*, extend it with zero bytes up to
   *D*. The payload position *pos* = current file length (≥ *D*).
2. If (*n* + 1) is a multiple of 1024 (the index block is full):
   1. Add 0x5000 to the stored offset of every existing record.
   2. Rewrite the whole index from offset 4 with these updated entries. The original writes them
      in an order that is not specified here [OPEN: the order is neither file order nor key
      order]. Readers are unaffected (index order carries no meaning, 2.2 rule 6). It matters
      only for byte comparison of a segment that the game grows past 1023 slots; saves never do
      (they hold about 12 outer records), so only a heavily extended `BAT.DAT` could differ. A
      writer may keep the entries in the order they were added.
   3. Move the data region [dataStart(*n*), end of file) to start at dataStart(*n*+1), i.e.
      0x5000 bytes later.
   4. *pos* = new file length.
3. Write a new 20-byte entry (group, instance, type, *pos*, size of *P*) at offset 4 + 20·*n*,
   immediately followed by 20 zero bytes (the new terminator).
4. Write *P* at *pos* (appending to the file).
5. *n* ← *n* + 1.

Consequences: payloads appear in the file in the order they were added; index entries also appear
in the order added; the index is a contiguous run of slots ending in a terminator; the file never
shrinks.

#### 2.5.3 Replace a record

Replacing an existing record is done by deleting it (2.5.4, in-place variant) and adding the new
version (2.5.2). The old payload bytes remain in the file as dead space. This is the origin of the
free slots and data gaps in the shipped text files and `BAT.DAT`. [INFERRED, high confidence]

#### 2.5.4 Delete a record

* **While the segment is open for writing** (the normal case): the matching 20-byte index entry
  is found by scanning the on-disk index from offset 4 and overwritten with 20 bytes of `FF`. The
  slot count *n* is **not** decreased, so subsequent adds still go after the last used slot.
  The payload is left in place.
* **When the segment is not open for writing:** the original instead compacts the file, and the
  result has an unreadable index (an original defect). A compatible implementation must not
  reproduce this; use the in-place variant instead. [STATIC]

Caveat on reopening a file that contains free slots: on open, *n* counts only live entries, but
the on-disk index still has the free slots. A later add writes at 4 + 20·*n*, which then overwrites
a live entry. This is an original defect. A compatible writer should set *n* to the number of
slots up to the terminator (live + free) instead. [STATIC]

### 2.6 Writer variants seen in shipped files [DATA]

| Files | Index sizing | Notes |
|-------|--------------|-------|
| `.ixf`, sprite `.dat`, `.sc3`, `.sct`, `.st3`, `.bld` | Block-preallocated (2.4) | 630 of the 631 parseable non-SNR files match dataStart(n) |
| `.SNR` (13 files) | Tight: data starts at 4 + 20·(n+1), immediately after the terminator | Written by a different tool; readers must not assume `0x5004` |
| Text `.IXF` (58 files) | Block-preallocated, with free slots | 688 free slots, every one all-`FF`, all followed by later live slots |
| 36 files | — | Trailing bytes after the last payload |

**Scenario (`.SNR`) tolerance.** The 13 shipped scenarios break two rules that other files follow:
their index is tight, so data begins immediately after the terminator and not at `0x5004`; and
their 110 string records declare a `size` 4 bytes smaller than the real payload (2.3). A loader
must accept both. It must locate payloads only through index offsets, never through an assumed
data start, and it must let string decoding read the length-prefixed text past `size`. [DATA]

For **writing** saves, an implementation should use the block-preallocated layout of 2.4 with
records added in the order defined by `10-save-format.md`, no free slots, and no trailing bytes.
This reproduces the original's output for a freshly written file [INFERRED]. For byte-identical
output the file is exactly: the 4-byte magic; the *n* index entries in the order added; one
20-byte all-zero terminator; zero bytes up to dataStart(*n*) (`0x5004` for up to 1023 records);
then the payloads, in the order added, with no gaps. The terminator and the zero fill are part
of the output and must be written. [STATIC][DATA]

### 2.7 Validation

All 649 files with the IXF magic were parsed with the rules above: 644 parse cleanly; 5 fail with
"end of file before terminator" (2.2 step 4). 200,549 live entries; 688 free slots; 0 duplicate keys; 0 zero-size
records; 0 payloads extending past end of file. [DATA]

## 3. Sprite `.DAT` archives

Sprite archives are ordinary IXF segments (section 2) with a fixed key convention. [DATA]

| Field | Value |
|-------|-------|
| Group | The archive's group ID. For `XXXXXXXX_Name.DAT` it equals the hex prefix `XXXXXXXX`. Unprefixed archives (`GAME_UI.DAT`, `EffectSprites.dat`, `disaster_*.dat`) use group `00000000`. Building-set and flora-set archives use their own group IDs (see below). |
| Instance | Image ID. |
| Type `00000000` | Image payload (format: `04-graphics-formats.md`). |
| Type `00000001` | 8-byte span / registration record: four `s16` L, T, R, B (same meaning as an `.SII` line; see section 4 and `04-graphics-formats.md`). |

Every image has a type-0 record; most have a type-1 record (62,627 span records for 63,931
images). [DATA]

The hex prefix of the file name equals the group of its images in every shipped prefixed archive:
`00000002_Residential` … `00000015_Boats`, `1000484E_nu18510`, `1000484F_DS18511`,
`10004850_DS18512`, `10004854_GY18516`, `10004856_LO18518`, `54AACF44_Holiday`,
`54DA2E59_HolidayF`, `552D8025_Landmarks_2`, `5544F8BD_Roads_2`, `654FCB99_CityObjects_2`,
`6591AD33_Landmarks_German`. [DATA] The program does not parse the prefix; it only enumerates
`*.DAT` files and loads whatever keys they contain. [STATIC]

Index ordering: the large archives are not sorted by key or by offset; within each image the
span record usually directly precedes or follows the image payload. Order carries no meaning.

### 3.1 `BuildingSets/<hex>/` and `FloraSets/<hex>/`

`Res/Sprites/BuildingSets/<hex8>/` and `Res/Sprites/FloraSets/<hex8>/` hold one `.dat` archive
each (`BuildingSets/6523f3d5/MBE_0015.dat`, `BuildingSets/e523f3fb/BST_0004.dat`,
`FloraSets/39edf62b/Flora_North West.dat`, `FloraSets/79ef435a/Flora_Jungle.dat`,
`FloraSets/c55a6d62/Flora_Desert.dat`, `FloraSets/c55a6d84/BlackForest.dat`). [DATA]

The directory name is the 8-digit hex **set ID** (building style set or flora/terrain set; its
semantics are in `13-buildings-occupants.md` and `11-world-terrain.md`). These archives are mixed:
besides sprites (types 0 and 1 in a sprite group) they carry the set's sprite attributes
(type `00006300`/`00006301`), occupant attributes (type `207EDC0E`, `E223741F`) and flora
attributes (type `FFD30C03`). [DATA]

Keys in this table are written Type : Group (the Instance varies per record), following the
`T:G:I` convention of `00-README.md`.

| Archive | Records (Type : Group × count) |
|---------|------------------|
| `MBE_0015.dat` | `00000000` and `00000001` : `547B7DF5` ×3392 each (images and spans); `207EDC0E` : `0000057E` ×165; `00006300` : `547B7DF6` ×164; `00006301` : `547B7DF6` ×6; `E223741F` : `A317745F` ×5 |
| `BST_0004.dat` | `00000000` and `00000001` : `5432D60E` (images and spans); `207EDC0E` : `0000057E` ×139; `00006300` : `5432D60F` ×141; `00006301` : `5432D60F` ×3; `E223741F` : `A317745F` ×5 |
| `BlackForest.dat` (and the other flora sets) | `00000000` and `00000001` : image group ×540 each; `00006300` : image group + 1 ×27; `FFD30C03` : `80F48961` ×27 |

## 4. `.SII` sprite sidecar files

An `.SII` ("sprite image info") file is a text list of image registration spans that sits next to
a sprite archive in `Res/Sprites/`. The shipped set has 10 files: 5 in version 1 and 5 in
version 0. [DATA]

### 4.1 Grammar [STATIC]

The file is read line by line. A line ends at CR; an LF directly after the CR is dropped. The
grammar below is authoritative; `04-graphics-formats.md` §5 only summarises it.

1. **Leading comments.** Read the first line. While the current line starts with `;`, read the
   next line. This applies **only before the header**: comment lines are not allowed between
   records. The first line that does not start with `;` becomes the current line, even if it is
   blank. A blank line here therefore fails rule 4 and the file is rejected.
2. **Optional version line.** If the current line contains the text `Version:` anywhere, it must
   start with `Version:` (no leading whitespace), followed by optional whitespace and a decimal
   integer; otherwise the file is rejected. The next line becomes current. If the current line
   does not contain `Version:`, the version is 0 and the same line is used for rule 4.
3. Only versions 0 and 1 are accepted.
4. **Count line.** The current line must begin with `Record Count:` (no leading whitespace), then
   optional whitespace and a decimal integer *N*. If it does not, the file is rejected. *N* ≤ 0
   means no records.
5. **Records.** Exactly the next *N* lines are records, one per line, with no skipping: a blank
   line in this range is a record line that fails to parse. Each line is parsed field by field:

   | Version | Fields |
   |---------|--------|
   | 0 | `ID , L , T , R , B` |
   | 1 | `GROUP , ID , L , T , R , B` |

   * `GROUP` and `ID` are hexadecimal of at most 8 digits, with no `0x` prefix. Upper or lower
     case is accepted, and leading whitespace is allowed.
   * `L`, `T`, `R` and `B` are signed decimal integers. Leading whitespace is allowed.
   * Each comma must **immediately** follow the preceding number: whitespace (spaces or tabs) is
     allowed after a comma but not before it.
   * Anything after the last number is ignored, including a stray comma. Shipped files put
     `; comment` text there, such as `; Alpha`.
   * In version 0 the group is 0.
6. If a record line does not parse, reading stops and the file counts as failed. Records parsed
   before the failure **remain registered**. Lines after the *N*th record are ignored.

Each record maps the image key (group, ID) to a span of four signed 16-bit values L, T, R, B. The
values are stored as 16-bit, so values outside −32768…32767 wrap. Their meaning (registration
point and extent) is defined in `04-graphics-formats.md`.

### 4.2 Semantics and verification [DATA]

* Every SII record equals the type-`00000001` span record that has the same group and instance in
  the sprite archive of the same base name. This holds for all 9,032 registered records: 8,977
  from the 9 files that read to completion and 55 from `disaster_LOCUST.sii` (0 mismatches,
  0 duplicates, 0 SII records without a span record). The SII files are therefore redundant
  copies of the archives' span records. Images that lack a span record (for example 1,232 in
  `54AACF44_Holiday.DAT`) have no SII record either (`04-graphics-formats.md` §2.4).
* `disaster_LOCUST.sii` declares `Record Count: 56` but contains only 55 records, followed by a
  blank line. Under rule 4.1.6 the original registers the first 55 records and reports a failure.
  All 55 records match the archive. It is the only shipped SII file that does not read to
  completion; one line of `EffectSprites.sii` has a stray trailing comma, which rule 5 ignores.
* Not every archive has an SII file; for example `disaster_WHIRLPOOL.dat` has none. Spans then come
  only from the archive's type-1 records.
* The original loads all `*.SII` files in `Res/Sprites` **before** the `*.DAT` files (section 7).
  Because the two sources agree everywhere, precedence between them is not observable with the
  shipped data. [STATIC] [DATA]

Parser coverage: 10/10 files read with the grammar above. 9 read to completion; 1 stops early by
design, as described above.

## 5. `SYS.PAK` and INI files

### 5.1 `SYS.PAK` layout [DATA]

`Apps/Sys/SYS.PAK` is an archive of 51 pre-parsed INI files.

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | count | Number of packed files (51) |
| 0x04 | var | entry[count] | directory | One entry per file, as below |
| … | var | — | bodies | Packed INI bodies at the offsets given |

Directory entry:

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | nameLen | Length of the name |
| 0x04 | nameLen | char[] | name | File name only, no directory (e.g. `Default.ini`), no terminator |
| … | 4 | u32 | offset | Absolute file offset of the packed body |

A body extends to the next body's offset, or to the end of the file. The first body starts right
after the directory, at 1292.

Packed INI body:

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | lineCount | Number of lines |
| 0x04 | var | line[lineCount] | lines | Each line is a `u32` length followed by that many bytes, with no terminator and no CR/LF |

The packer removes blank lines and whole-line `;` comments; neither occurs in any shipped body.
It keeps everything else verbatim, including trailing `;` comments inside values and spaces
around `=`. [DATA]

### 5.2 Which copy is read: PAK first, then the loose file [STATIC]

Every INI consumer names two paths: the loose file (for example `Sys\Default.ini` or
`\Sys\SC3Tune.INI`, relative to the game's `Apps` directory) and the archive `Sys\SYS.PAK`. The
source is chosen once, on first access:

1. If the archive exists and opens, take the **file name and extension** of the loose path, with
   the directory dropped, and upper-case it. Look for a directory entry whose upper-cased name is
   equal. If one exists, read from the archive.
2. Otherwise (no archive, or the name is not in it), read the loose file on disk.

So **the packed copy wins**. A loose `Sys\*.INI` is used only for names that are absent from
`SYS.PAK`; to override a tunable, the user must edit the archive. A few consumers check
alternative names first (for example `GoldAdvisorMoods.INI` before `AdvisorMoods.INI`, see
`22-advisors-news-petitioners.md`). For each name they try, the rule above applies.

### 5.3 INI grammar as the game parses it [STATIC]

General rules:

* A section header is a line beginning with `[`. A section ends at the next line that begins with
  `[`.
* A key line is split at the **first** `=`: the key is the text before it and the value is the
  text after it.
* Trimming removes **only space characters (0x20)**, not tabs, from both ends of the key and the
  value.
* `;` is **not** a comment character inside a key line. The value keeps everything after `=`,
  including any `;` and the text after it. Consumers that expect a number parse the leading
  number and ignore the rest. Some values use `;` as a field separator: in `[DBSegments]` the
  value is `class;path` (section 7.2).
* Comma lists, `<…>` groups and similar structures are value syntax, parsed by each consumer. The
  owning documents describe them.

The two sources differ in detail:

| Aspect | Packed copy (SYS.PAK) | Loose file |
|--------|-----------------------|------------|
| Section match | Line must equal `[` + name + `]` exactly (case-sensitive, no spaces) | Section names compared case-insensitively |
| Blank lines and `;` comment lines inside a section | Not present (removed by the packer) | Skipped (after removing leading spaces) |
| Line with empty key or empty value (including a line without `=`) | **Ends the section** for both enumeration and single-key lookup: later lines are not seen | Skipped |
| Key lookup (single value) | Key compared case-insensitively (both upper-cased); the **first** matching line wins | First match wins. Key case rule not determined [OPEN: case-sensitive or not?]; compare case-insensitively, as for the packed copy. Every shipped INI is packed, so this applies only to user-added loose files |
| Section enumeration (all key/value pairs) | Every pair, in file order, **including duplicate keys** | Every pair, in file order, including duplicates |

Consequences for shipped data [DATA]:

* Duplicate keys are meaningful in enumerated sections. `[DBSegments]` lists keys `0x42C1ED2D`,
  `0xA1096A4F`, `0x62E69238`, `0x21FD8E4`, `0x025C2C45`, `0xA22B551E` and `0x822B7E6B` twice, each
  time with a different file, and every line is processed.
* In the packed `SC3DisasterLayer.ini`, section `[ParadeEventParadeConfigs]` contains a line with
  no `=`. When the section is enumerated from the archive, enumeration stops at that line, so the
  three entries after it are never seen by the original. A faithful implementation reproduces
  this.

**Numeric keys and values.** IDs such as `[DBSegments]` keys and class IDs are converted to
numbers as follows [STATIC]:

* A `0x` or `0X` prefix means hexadecimal.
* Otherwise, if the string contains any letter `a`–`f` or `A`–`F`, it is hexadecimal.
* Otherwise it is decimal.

Conversion stops at the first invalid character. The rule accepts short forms such as
`0x21fd8e4` (= `021FD8E4`).

## 6. RefPack compression

RefPack (also called QFS) is the LZ77-style compressor used for save databases, sprites, UI images,
BAT textures and thumbnails. All of them use the same stream format. [STATIC]

### 6.1 Stream header

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 1 | u8 | flags | The original always writes `0x10`. On read, only bit 0 is examined |
| 0x01 | 1 | u8 | magic | `0xFB`. Always written; **not checked** on read |
| 0x02 | 3 | u24 **big-endian** | compSize | Present only if flags bit 0 is set; skipped unread |
| 0x02 or 0x05 | 3 | u24 **big-endian** | rawSize | Decompressed size in bytes |
| 0x05 or 0x08 | … | ops | body | Opcode stream, ending with a stop op |

Reader constraints of the original [STATIC]:

* Flags bit 7 (4-byte sizes in other RefPack variants) is **not supported**: the size fields are
  always read as 3 bytes, so rawSize must be < 2^24 (16,777,216 bytes).
* Where the program needs the decompressed size before decoding, it takes rawSize from offset 2
  when the first two bytes are exactly `10 FB`, and from offset 5 for any other first two bytes.
  **Writers must therefore emit exactly `10 FB` + 3-byte rawSize** (no compSize field). All 59 shipped save streams use this header.
  [DATA]
* A stream whose ops produce more than rawSize bytes is not detected as an error by the original;
  such streams must never be written.

### 6.2 Opcodes

Let *b0* be the first byte of an op. "Literal" bytes are copied from the input to the output.
A "match" of length *n* then copies *n* bytes one at a time from *d* bytes back in the output
(*d* ≥ 1). The copy must be byte by byte: when *d* < *n* it repeats bytes it has just written.

| b0 range | Op length | Literals before match | Match length n | Distance d |
|----------|-----------|-----------------------|----------------|------------|
| `00`–`7F` | 2 (b0, b1) | b0 & 3 | ((b0 >> 2) & 7) + 3 → 3…10 | ((b0 & 0x60) << 3) + b1 + 1 → 1…1024 |
| `80`–`BF` | 3 (b0, b1, b2) | b1 >> 6 | (b0 & 0x3F) + 4 → 4…67 | ((b1 & 0x3F) << 8) + b2 + 1 → 1…16,384 |
| `C0`–`DF` | 4 (b0, b1, b2, b3) | b0 & 3 | ((b0 & 0x0C) << 6) + b3 + 5 → 5…1028 | ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1 → 1…131,072 |
| `E0`–`FB` | 1 | ((b0 & 0x1F) + 1) × 4 → 4…112, no match | — | — |
| `FC`–`FF` | 1 | b0 & 3, then **stop** | — | — |

The literals of an op immediately follow its op bytes. Decoding ends at the stop op, and the
output length should equal rawSize. Verified: all 59 save streams decode to exactly rawSize and
consume exactly the stored compressed length. [DATA]

### 6.3 What an encoder must produce

The original loads any stream that follows 6.1 and 6.2. Its decoder accepts every legal op
sequence, including overlapping matches and ops that carry no literals. Constraints:

1. The header is exactly `10 FB` followed by the 3-byte big-endian raw size, with raw size ≤
   16,777,215.
2. The format limits are a maximum distance of 131,072 and a maximum match length of 1028. The
   original encoder never uses a distance above 131,071.
3. The stream ends with a stop op (`FC`–`FF`) that carries the final 0–3 literals.

### 6.4 Canonical stream (optional, for byte-identical output) [STATIC] [DATA]

Any stream that meets 6.1–6.3 loads in the original, so matching the original's compressed bytes
is **optional**. It is useful only for byte comparison of re-saved files (`28-conformance.md` §3).
This section defines the stream the original writes. A stream built by these rules reproduced
**all 59 shipped save streams byte for byte**. [DATA]

**Tokens.** The input (length *n*) is cut, from left to right, into tokens. Each token is either one
literal byte or a match (length *L*, distance *D* = p − c, copying from an earlier position *c*).
A position at which a token starts is an **anchor**. The bytes covered by a match after its
first byte are not anchors.

**Choice at position *p*.** Let *maxLen* = min(1028, n − p).

1. Candidates are the earlier anchors *c* with p − c ≤ 131,071 whose three bytes at *c* equal the
   three bytes at *p*. Positions *p* with fewer than 3 bytes left never start a match.
2. For a candidate, *L* is the number of equal bytes in in[c…] and in[p…], at most *maxLen*. (The
   match may overlap *p*.) With *d* = D − 1, the op form is the first that fits:

   | Condition | Form | Cost (bytes) |
   |-----------|------|--------------|
   | *d* < 1024 and *L* ≤ 10 | 2-byte op | 2 |
   | *d* < 16,384 and *L* ≤ 67 | 3-byte op | 3 |
   | otherwise | 4-byte op | 4 |

   The **gain** of a candidate is *L* − cost.
3. Visit the candidates from the nearest (largest *c*) to the farthest. Keep a current best,
   which starts as "no match" with length 2 and gain 0. A candidate becomes the new best only if
   its *L* is greater than the current best's length **and** its gain is strictly greater than
   the current best's gain. Stop at the first candidate with *L* = 1028.
4. If no candidate became best, *p* is a literal: it becomes pending, and the next position is
   *p* + 1. Otherwise the best candidate is emitted as a match, and the next position is *p* + *L*.

Consequences: among candidates with equal gain the nearest wins; a longer but more expensive match
does not replace a shorter one with the same or higher gain; the source of a match is always an
anchor, never a position inside an earlier match. Sources more than 131,071 bytes back are never
used.

**Literal flushing.**

* Pending literals are written only when a match is emitted or the input ends.
* Before a match: while more than 3 literals are pending, write a literal-run op (`E0`–`FB`) for
  *k* = min(pending rounded down to a multiple of 4, 112) of them. The remaining 0–3 pending
  literals are carried by the match op itself (its "literals before match" field).
* Match op bytes, with *t* = the number of carried literals (0–3):

  | Form | b0 | b1 | b2 | b3 |
  |------|----|----|----|----|
  | 2-byte | ((d >> 3) AND 0x60) + ((L − 3) << 2) + t | d AND 0xFF | — | — |
  | 3-byte | 0x80 + (L − 4) | ((d >> 8) AND 0x3F) + (t << 6) | d AND 0xFF | — |
  | 4-byte | 0xC0 + (((L − 5) >> 6) AND 0x0C) + ((d >> 12) AND 0x10) + t | (d >> 8) AND 0xFF | d AND 0xFF | (L − 5) AND 0xFF |

  The *t* literal bytes follow the op bytes (6.2).
* At the end of the input: flush literal runs as above, then write the stop op `FC` + *t* followed
  by the last *t* (0–3) literals. The stop op is always written, even when *t* = 0.

### 6.5 Packed-segment codecs

Save files name a codec for their packed segment (`10-save-format.md` §4.2). Both values that
occur are RefPack, and both use **the same framing**: a `u32` little-endian length (compressed
bytes + 4), followed by a RefPack stream as in 6.1. [STATIC]

| Codec | Compressed block layout | Encoder |
|-------|-------------------------|---------|
| `0x65` | `u32` length (compressed bytes + 4), then a RefPack stream | 6.4 |
| `0x67` | Identical to `0x65`. All 59 shipped saves use this value [DATA 59/59] | 6.4 |

The two values decode identically, and a writer should emit `0x67`. No shipped file uses any other
compression. See `10-save-format.md` §4.2 for the full segment record.

## 7. Resource discovery, lookup and loading

### 7.1 Start-up sequence [STATIC]

The application performs these steps in order. All paths are relative to `Apps/`.

1. Read `Sys/Default.ini` (packed copy first, 5.2) and enumerate its sections in this order:
   1. `[ResourceFactories]`: register each Type → decoder class (`03-resource-types.md` §4).
   2. `[DBSegments]`: mount each listed IXF file as a segment (7.2), in file order.
   3. `[DBSegmentDirectoryFiles]`: mount directory segments. The section is empty in the shipped
      data.
   4. `[AppServices]`, `[GameCommandFactories]`, `[SPLASHBMP]`, `[WINSC3BMP]`, `[MAINMENUBMP]`.
2. Read `Sys/CitySim.ini` sections `[AgentTypes]` and `[AgentTypeHierarchy]`.
3. Enumerate `Res/Sprites/*.SII` (non-recursive) and register each as an image-info source
   (section 4).
4. Enumerate `Res/Sprites/*.DAT` (non-recursive; the match is case-insensitive, so it includes
   the lower-case `.dat` files) and mount each as a sprite segment.
5. Read `Sys/MenuItem.ini`.
6. Add-on content under the plug-in directory, `PlugIn\`, is enumerated in the same way (`*.DAT`
   and `*.SII`) when add-ons are enabled. No add-on files are shipped. [STATIC], details [OPEN]

**Building and flora sets are mounted on demand**, not at start-up [STATIC]:

* When a city's building-style set changes, every `*.DAT` in
  `Res/Sprites/BuildingSets/<SET>/` is mounted. `<SET>` is the set ID as 8 **upper-case** hex
  digits; the shipped folders are lower-case, so lookups must be case-insensitive.
* Flora sets in `Res/Sprites/FloraSets/<SET>/` are handled the same way.
* The set IDs come from the city (`10-save-format.md`) and `Sys/SC3CityScheme.ini`
  (`13-buildings-occupants.md`, `11-world-terrain.md`).
* `BARender/BAT.DAT` (imported BAT buildings) is mounted by the simulation initialiser
  (`24-bat-buildings.md`).

### 7.2 `[DBSegments]` lines [STATIC] [DATA]

Each line has the form `<segmentId>=<classId>;<path>`.

* `segmentId` is parsed as a number (5.3). It is the group ID by convention, but the loader does
  not enforce this.
* `classId` is `0x80C381D7` (the IXF segment class, numerically equal to the IXF magic) on every
  shipped line.
* `path` is resolved like this:

  | Path begins with | Resolved to |
  |------------------|-------------|
  | `%DataDirectory%` | That text replaced by the data directory (`01-architecture.md` §4 step 2; default `Apps/`) |
  | `%PluginDirectory%` | That text replaced by the plug-in root followed by `PlugIn\`. The plug-in root is a separate setting of the program whose source was not determined [OPEN]; use the data directory (`Apps/`) as the default. No shipped line uses this prefix |
  | anything else | Relative to the data directory (default `Apps/`) |

  After that, every `%Language%` is replaced by the current language's folder name (7.3).
* The mount reads the index immediately (eager index load, 2.2). Payloads are read lazily, when a
  resource is requested.
* **A missing file mounts as an empty segment without error.** `Res/Sprites/CSAttrib_Korea.IXF`
  and `Res/Occupant/OccupantAttribs_Korea.IXF` are listed but not shipped.
* A file without the IXF magic, or with no index terminator, fails to mount and contributes no
  resources.

### 7.3 Language folder [STATIC]

`%Language%` is replaced by the folder name of the current language.

* Language selection is specified in `05-text-and-fonts.md` and `27-platform.md`: registry value
  `Language` holds a language ID (1–43), a Windows LANGID (a value of 44 or more) or a name; the
  full rules and the LANGID table are in `05-text-and-fonts.md` §2.3–§2.4. For the languages
  listed below the LANGID and the LCID have the same value.
* Folder names for the shipped languages:

  | LANGID | Folder |
  |------|--------|
  | 1033 | `English` |
  | 2057 | `English-UK` |
  | 1036 | `French` |
  | 1031 | `German` |
  | 1040 | `Italian` |
  | 1043 | `Dutch` |
  | 1053 | `Swedish` |

  The on-disk folders are `ENGLISH`, `English-UK`, `FRENCH`, `GERMAN`, `ITALIAN`, `DUTCH` and
  `SWEDISH`; the original relies on case-insensitive file names.
* A language counts as installed only if `Res/Text/<Folder>/SC3StringsApp.IXF` exists.
* Language-dependent files:
  * `Res/Text/%Language%/*.IXF`
  * `Res/Hotkeys/%Language%.IXF`
  * `Res/SSimData/%Language%/VPathAtt.IXF`
  * `Res/SSimData/%Language%/TrBlkAtt.IXF`

  Only one language's copies are mounted, and all languages use identical keys.

### 7.4 Precedence when a key exists in several segments

Within one file, the first index entry wins (2.2).

Across files, the shipped data has real collisions [DATA]. With one language mounted, 214 keys
occur in more than one file, and 210 of them have **different** payloads (all except the 4
`PPLPROD` keys):

| Base file | Overridden by | Keys |
|-----------|---------------|------|
| `Res/Occupant/OccupantAttribs.IXF` (occupant attributes) | Building-set archives: both `MBE_0015.dat` and `BST_0004.dat` (132), `MBE_0015.dat` only (37), `BST_0004.dat` only (11), both sets and `HOLIDAYF.IXF` (1) | 181 |
| `Res/Occupant/OccupantAttribs.IXF` | All four flora-set archives | 27 |
| `Res/Occupant/OccupantAttribs.IXF` | `HOLIDAYF.IXF` only (listed later in `[DBSegments]`) | 2 |
| `PPLPROD.IXF` | `PplProd_Holidays.IXF` | 4 (identical payloads) |

Total 181 + 27 + 2 + 4 = 214 distinct keys. `HOLIDAYF.IXF` overrides 3 keys in all (2 alone plus
the 1 shared with both building sets).

A set's attributes must replace the base attributes while that set is mounted; otherwise
building and flora sets would have no visible effect. The rule to implement is therefore
**"most recently mounted segment wins"**: on-demand set archives override start-up segments, and
later `[DBSegments]` lines override earlier ones. [INFERRED, high confidence from the data; the
search order itself is not confirmed.] Two `[DBSegments]` lines with the same segment ID both
mount (§5.3); they behave exactly like two segments with different IDs, so the later line's file
wins for keys present in both. When a set is unmounted
(replaced by another set), its overrides disappear.

### 7.5 Caching [STATIC]

* Segment indexes are read once, at mount time, and kept in memory. Payload bytes are re-read
  from the file on each open.
* Decoded resources of types listed in `[ResourceFactories]` are decoded once and shared by
  every user while any user still holds them.
* `[OccAttribCache]` in `Default.ini` lists the segment IDs (`Entry1`…`Entry6`) whose occupant
  attributes are pre-loaded into a cache (`13-buildings-occupants.md`). [DATA]


## Persisted state

This subsystem defines no save records of its own. Save files use the IXF container (section 2)
and RefPack (section 6); their records are specified in `10-save-format.md`.

## Open questions

* **Segment search order across files** (7.4): "most recently mounted wins" is inferred from the
  data (building and flora sets must override `OccupantAttribs.IXF`) and is not confirmed. [OPEN]
* **Add-on (`PlugIn\`) enumeration**: the plug-in root, the `addon-enabled` / `default-enabled`
  switches and the order relative to `Res/Sprites` were not traced. No add-on content is shipped.
  [OPEN]
* **Precedence between an `.SII` record and the archive's type-1 span** for the same image is not
  observable with shipped data, because they are identical (4.2). [OPEN]
* **Loose-file INI with a repeated section header**: which occurrence is used was not determined.
  No shipped file has one. [OPEN]
* **`[DBSegmentDirectoryFiles]`** mounts directory-backed segments (each file in a folder is one
  resource). The section is empty in the shipped data, and the file-name-to-key mapping was not
  specified. [OPEN]
