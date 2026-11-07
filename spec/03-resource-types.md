# 03 — Resource type registry

This document lists every resource **Type ID** that occurs in the shipped data, with where it
occurs, how many records exist, what the payload is, and which document owns the payload format.
It also defines the Group/Instance conventions and the generic record encodings shared by many
types.

Container mechanics (IXF index, key order, preallocation, lookup precedence) are in
`02-containers.md`.

## 1. Reading this table

* A key is written `T:G:I` (Type, Group, Instance), as defined in `00-README.md`.
* **The Type is the third `u32` of an IXF index entry.** The index stores Group, Instance, Type in
  that order (`02-containers.md` §1). [STATIC] [DATA]
* Scope: this registry covers every Type ID that occurs in an **IXF index** of a shipped file. The
  records stored inside the packed (RefPack) inner database of saves (`035F62A4:035F62A4:00000001`)
  have their own Type IDs; they are not IXF index entries and are listed only in
  `10-save-format.md` §5–§6.
* Counts are live index entries across all 644 parseable IXF-format files (536 `.ixf`, 37 `.dat`,
  21 `.sct`, 17 `.bld`, 15 `.sc3`, 13 `.snr`, 10 `.st3`; the 5 unreadable files are listed in
  `02-containers.md` §2.2). Total: **51 distinct Type IDs, 200,549 records**. [DATA]
* "Factory" means that the Type ID appears as a key of `[ResourceFactories]` in `Sys/Default.ini`
  (section 4).

## 2. Group and Instance conventions [DATA]

| Data family | Type | Group | Instance |
|-------------|------|-------|----------|
| String tables | `2026960B` | Table ID. Usually one per `.IXF` file in `Res/Text/<LANG>/` and equal to the file's `[DBSegments]` key, but an add-on file may share its base file's table ID (`05-text-and-fonts.md` §1.5–§1.6). The same Group is used for every language, so the language is selected by **which folder is mounted**, not by the key. | String ID |
| Sprites (`.DAT`) | `00000000` image, `00000001` span | Archive group (hex file-name prefix, or `00000000` for unprefixed archives, or the set's own image group) | Image ID |
| UI images | `62B9DA24` | Image set (`52DB60A0` advisors, `82B9B75B` `SYS.IXF`, `82B9B75C` `MAIN.IXF`, …) | Image number within the set |
| Sprite attributes (CSATTRIB) | `00006300` / `00006301` | Attribute-set group (`00006400` base game, `552D8026` set 2, `54AACF44` holidays, `6591AD34` German, or a building/flora set's group) | Occupant / composite-sprite ID |
| Occupant attributes | `207EDC0E`, `E223741F`, `FFD30C03`, `220055E1` | Occupant class group: `0000057E` buildings, `A317745F` network pieces, `80F48961` flora | Occupant ID |
| Automata attributes | `C25C2BF7`, `C22B54FA`, `C25C74F2`, `A20BF670`, `625C6226`, `222B7EE4`, `21FD6EEF`, `625C6CC7` | One fixed group per file (equals the `[DBSegments]` key) | Vehicle / pedestrian / path / block ID |
| City saves | various (see `10-save-format.md`) | One fixed group per record type | Fixed per record type: 0 for most, but `1` for `035F62A4` (inner database) and `0525A6B9`, `79FDB71E` for `79FDB6C8`, script numbers for `23DFAE5F` (§3.3) |
| BAT buildings (`.bld`, `BAT.DAT`) | `E3F87Bxx` etc. | `C3F8D4A9` (in `.bld`), `BA1DBF1E` (in `BAT.DAT`) | Part / building ID |
| Scenario | `23DFAE5F` etc. | Scenario record group | Script / record number |

General rules:

* Group is the namespace that ties records to a file and to a `[DBSegments]` line. Most files
  contain exactly one group (578 of 644). Mixed-group files are the building/flora set archives,
  `OccupantAttribs*.IXF`, saves, scenarios and `.bld` files. [DATA]
* Building sets and flora sets are identified by the 8-hex-digit folder name under
  `Res/Sprites/BuildingSets/` and `Res/Sprites/FloraSets/`; inside, sprite group *g* is followed
  by attribute group *g*+1 (`547B7DF5`/`547B7DF6`, `5432D60E`/`5432D60F`,
  `45576AE7`/`45576AE8`, `257C90E7`/`257C90E8`, `4562073E`/`4562073F`, `556105FA`/`556105FB`).
  The same pattern holds for two loose archives: `552D8025_Landmarks_2.DAT` ↔ `CSAttrib_2.IXF`
  group `552D8026`; `6591AD33_Landmarks_German.DAT` ↔ `CSAttrib_German.IXF` group `6591AD34`.
  It is a naming convention only, not a rule the program applies, and it has exceptions: the
  holiday attributes in `CSAttrib_Holidays.IXF` (`00006300` ×15, `00006301` ×5) use group
  `54AACF44`, the **same** group as the holiday sprites in `54AACF44_Holiday.DAT`; their keys do not
  collide because the Types differ. The base game uses `00006400` for its attributes. [DATA]
* Language is never encoded in a key. Localised files live in per-language folders that share
  identical keys (`02-containers.md` §7).

## 3. Registry

Columns: Type ID; records; files containing it; extensions; main groups; short name; payload;
owning document.

### 3.1 Shared resource types

| Type | Records | Files | Ext | Groups | Name | Payload | Owner |
|------|--------:|------:|-----|--------|------|---------|-------|
| `00000000` | 63,931 | 37 | .dat | 32 groups (`00000006`, `00000008`, `54AACF44`, `00000005`, …) | Sprite image | Compressed sprite image (pixel format, RLE/RefPack). Factory: no | `04-graphics-formats.md` |
| `00000001` | 62,627 | 37 | .dat | same as type 0 | Sprite span | 8 bytes: four `s16` L, T, R, B registration span; same meaning as an `.SII` line | `04-graphics-formats.md`, `02-containers.md` §4 |
| `2026960B` | 56,062 | 485 (text `.IXF` and `.SNR`) | .ixf .snr | 64 groups | String | `u32` length N + N bytes Windows-1252 text. Index `size` may exclude the 4-byte prefix | `05-text-and-fonts.md` |
| `62B9DA24` | 729 | 28 | .ixf .snr | 16 groups (`52DB60A0`, `02F78C6D`, `82B9B75B`, `831D112B`, …) | UI image | Windows BMP or UI image record (see owner) | `08-ui-framework.md`, `04-graphics-formats.md` |
| `00006300` | 1,425 | 11 | .dat .ixf | `00006400`, `547B7DF6`, `5432D60F`, `552D8026`, … (11) | Composite sprite attributes (CSATTRIB) | `BIN\r` binary record: per-occupant list of sprite keys, offsets and rotations | `13-buildings-occupants.md`, `04-graphics-formats.md` |
| `00006301` | 61 | 4 | .dat .ixf | `00006400`, `547B7DF6`, `54AACF44`, `5432D60F` | Composite sprite attributes, variant | `BIN\r` record, same family as `00006300` (larger, multi-part) | `13-buildings-occupants.md` |
| `207EDC0E` | 968 | 7 | .dat .ixf | `0000057E` | Building occupant attributes | `TKB1` property list | `13-buildings-occupants.md` |
| `E223741F` | 405 | 4 | .dat .ixf | `A317745F`, `0000057E` | Network-piece occupant attributes | `TKB1` property list | `13-buildings-occupants.md`, `12-networks.md` |
| `FFD30C03` | 137 | 5 | .dat .ixf | `80F48961` | Flora occupant attributes | `TKB1` property list | `13-buildings-occupants.md`, `11-world-terrain.md` |
| `220055E1` | 20 | 1 | .ixf | `0000057E` | Occupant attributes (port / special class) | `TKB1` property list | `13-buildings-occupants.md` |
| `81F53D09` | 31 | 1 | .ixf (`BUILDFAM.IXF`) | `01F58D3D` | Building family | `TXT!` + text definition | `13-buildings-occupants.md` |

### 3.2 Automata, traffic and simulation-script types

| Type | Records | Files | Ext | Groups | Name | Payload | Owner |
|------|--------:|------:|-----|--------|------|---------|-------|
| `C25C74F2` | 6,356 | 7 (`SSimData/<LANG>/VPathAtt.IXF`) | .ixf | `A25C7520` | Vehicle path attributes | `BIN\r` record (path point lists) | `26-automata.md` |
| `625C6226` | 6,034 | 7 (`SSimData/<LANG>/TrBlkAtt.IXF`) | .ixf | `825C6289` | Traffic-block attributes | `BIN\r` record (nested `BIN\r` sub-records) | `26-automata.md`, `18-sim-transport.md` |
| `A20BF670` | 192 | 1 (`PPathAtt.IXF`) | .ixf | `C20BF6C3` | Pedestrian path attributes | `BIN\r`: point count, points (3 × s32), two u32 | `26-automata.md` |
| `222B7EE4` | 128 | 2 (`AnSprAtt*.IXF`) | .ixf | `822B7E6B` | Animated-sprite attributes | `BIN\r`: sprite key, sequences of frame numbers | `26-automata.md`, `25-rendering.md` |
| `C25C2BF7` | 77 | 2 (`VAttrib*.IXF`) | .ixf | `025C2C45` | Vehicle attributes | `BIN\r` fixed 49-byte record (tag included) with embedded keys | `26-automata.md` |
| `C22B54FA` | 52 | 2 (`PAttrib*.IXF`) | .ixf | `A22B551E` | Pedestrian attributes | `BIN\r` fixed 28-byte record (tag included) with embedded keys | `26-automata.md` |
| `21FD6EEF` | 67 | 2 (`PplProd*.IXF`) | .ixf | `21FD8E4C` | People producer | `BIN!` record: per-country mode tables of weighted pedestrian keys | `26-automata.md` |
| `625C6CC7` | 31 | 1 (`VclProd.IXF`) | .ixf | `A25C6D40` | Vehicle producer | `BIN\r`: f32 rate, count, (u32 weight, vehicle key) list | `26-automata.md` |
| `822B51AA` | 24 | 1 (`SimScrpt.IXF`) | .ixf | `E22B51C4` | Simulation script | CR LF text lines of integers (TAB-separated pairs) | `26-automata.md` |
| `62DFCCEC` | 2 | 1 (`AirTrMgr.IXF`) | .ixf | `62DFCD34` | Air-traffic manager config | CR LF text lines of integers | `26-automata.md` |
| `22CEFD99` | 2 | 1 (`TrnProd.IXF`) | .ixf | `62CEFDDB` | Train producer config | CR LF text lines of integers | `26-automata.md` |
| `A2E3D533` | 14 | 7 (`Res/Hotkeys/<LANG>.IXF`) | .ixf | `A2E3D56D` | Hotkey table | CR LF text: one binding per line, `= ` separated numbers | `08-ui-framework.md` |

### 3.3 Save-file types (`.sc3`, `.st3`, `.sct`, `.SNR`)

All owned by `10-save-format.md` (which has the byte layouts); scenario-only types are owned by
`23-scenarios.md`.

| Type | Records | Files | Group | Instance | Name |
|------|--------:|------:|-------|----------|------|
| `035F62A4` | 118 | 59 | `035F62A4` | 0, 1 | Instance 0: packed-segment directory (20 bytes); Instance 1: packed (RefPack) inner database |
| `00000FA1` | 59 | 59 | `617E198D` | 0 | City information |
| `C359837E` | 59 | 59 | `43598384` | 0 | File-kind word (4 bytes) |
| `035AD1CB` | 59 | 59 | `835AD1D1` | 0 | City state words (six u32; four bytes in `.st3`) |
| `E2F42628` | 59 | 59 | `62F42635` | 0 | Preview / city summary |
| `A1634F05` | 59 | 59 | `01E1A5E6` | 0 | City-view window state |
| `620EDFCE` | 59 | 59 | `A20FB3B9` | 0 | Isometric view state |
| `430E2EE6` | 58 | 58 | `530E2EE6` | 0 | Cheat log: count |
| `79FDB6C8` | 49 | 49 | `59FDB6FD` | 79FDB71E | Versioned header + u32 + string (not in `.st3`) |
| `99F47014` | 49 | 49 | `79F4702D` | 0 | 16 bytes (not in `.st3`) |
| `0525A6B9` | 49 | 49 | `4525A6E2` | 1 | 3 × u32 (not in `.st3`) |
| `630E2EE6` | 24 | 24 | `730E2EE6` | 0 | Versioned header + list |
| `23DFAE5F` | 116 | 13 (.SNR) | `23DFAE8C` | script numbers (e.g. 8D–90) | Scenario goal scripts (text) — `23-scenarios.md` |
| `26E732E0` | 13 | 13 (.SNR) | `19A6CEA1` | 0 | Scenario descriptor (12 bytes) — `23-scenarios.md` |
| `A4846144` | 13 | 13 (.SNR) | `A48461A3` | 0 | Scenario style text table — `23-scenarios.md` |
| `44EB6BC8` | 13 | 13 (.SNR) | `44EB6BC9` | 0 | Scenario presenter text table — `23-scenarios.md` |
| `04845CBE` | 13 | 13 (.SNR) | `C3DE4D66` | 0 | Scenario header text — `23-scenarios.md` |

Keys are written `T:G:I` in full in `10-save-format.md`; record names follow that document.

`.SNR` files also contain `2026960B` strings (scenario-local tables and the author string) and one
`62B9DA24` picture.

### 3.4 BAT types (`.bld`, `BARender/BAT.DAT`)

All owned by `24-bat-buildings.md`.

| Type | Records | Files | Group | Name |
|------|--------:|------:|-------|------|
| `E3F87B0C` | 17 | 17 .bld | `C3F8D4A9` | Building model |
| `E3F87B0D` | 84 | 11 .bld | `C3F8D4A9` | Custom-texture header |
| `E3F87B10` | 84 | 11 .bld | `C3F8D4A9` | Custom-texture pixels (RefPack) |
| `E3F87B0E` | 17 | 17 .bld | `C3F8D4A9` | Thumbnail, 200 × 200, 16-bit, RefPack |
| `E3F87B0F` | 17 | 17 .bld | `C3F8D4A9` | Building info (name, author, …) |
| `644D7178` | 17 | 17 .bld | `C3F8D4A9` | Short string (u8 length) |
| `644D7179` | 17 | 17 .bld | `C3F8D4A9` | u32 value |
| `B9EC8F72` | 17 | 17 .bld | `C3F8D4A9` | u32 value |
| `99F88B9E` | 17 | 17 .bld | `C3F8D4A9` | 4 × u8 |
| `FA662D17` | 3 | 1 .bld | `C3F8D4A9` | Custom prop (view bitmap names, dimensions) |
| `856CD19A` | 15 | 1 (`BAT.DAT`) | `BA1DBF1E` | Imported-BAT directory record |

`BAT.DAT` additionally contains `00006300` (group `BA1DBF1E`) and sprite records.

## 4. `[ResourceFactories]` (`Sys/Default.ini`)

Each line maps a Type ID (key) to a decoder for that type.
[STATIC] [DATA] An implementation needs only the key list: it tells which Types are decoded into
shared, cached resource objects when requested by key. Types without a line (sprite types 0 and 1,
save records, BAT records) are read directly.

| Type | Type |
|------|------|
| `2026960B` | `22CEFD99` |
| `FFD30C03` | `62DFCCEC` |
| `C179C042` | `A20BF670` |
| `207EDC0E` | `C25C74F2` |
| `220055E1` | `625C6226` |
| `E223741F` | `222B7EE4` |
| `00006300` | `822B51AA` |
| `00006301` | `C22B54FA` |
| `21FD6EEF` | `C25C2BF7` |
| `625C6CC7` | `23DFAE5F` |
| `81F53D09` | `62B9DA24` |
| `A2E3D533` | `856CD19A` |

Notes:
* `FFD30C03` appears twice in the section. Both lines are delivered when
  the section is enumerated (`02-containers.md` §5.3). Which of the two registrations is in
  effect afterwards is [OPEN: does the second registration for a Type replace
  the first, or is it ignored?]. It makes no difference to an implementation: `FFD30C03` records
  are `TKB1` property lists either way (§5). `C179C042` is registered but occurs in no shipped
  file. [DATA]
* The five `TKB1` Types (`FFD30C03`, `C179C042`, `207EDC0E`, `220055E1`, `E223741F`) share one
  decoder: decode as a `TKB1` property list. [INFERRED, high]

## 5. Generic record encodings

Many payloads begin with a 4-byte tag that selects the encoding. [DATA]

| First 4 bytes | Name | Used by | Description |
|---------------|------|---------|-------------|
| `42 49 4E 0D` (`BIN` CR) | Binary record | `00006300`, `00006301`, `C25C74F2`, `625C6226`, `A20BF670`, `222B7EE4`, `C25C2BF7`, `C22B54FA`, `625C6CC7` | Readers check the first three bytes `BIN`; if absent, the record is parsed as text lines (development format; not shipped) [STATIC]. The whole 4-byte tag, CR included, is the header: binary little-endian fields start at payload offset 4, using the record primitives below. Record sizes quoted anywhere in this specification (e.g. "fixed 49-byte record") **include** the tag. [DATA: all 77 `C25C2BF7` records are 49 bytes and all 52 `C22B54FA` records 28 bytes, each beginning `BIN` CR] |
| `42 49 4E 21` (`BIN!`) | Binary record, alt tag | `21FD6EEF` | As above; tag compared as a whole `u32`. |
| `54 58 54 21` (`TXT!`) | Text record | `81F53D09` | Text follows the tag. |
| `54 4B 42 31` (`TKB1`) | Property list | `207EDC0E`, `E223741F`, `FFD30C03`, `220055E1` | Sequence of (u32 attrId, u16 valueType, u16 count, values) until end of payload. Value types: 1 = u8, 2 = u16, 3 = u32, 7 = string, 8 = resource key. Full format: `13-buildings-occupants.md` §2. |
| (none) | Plain text | `822B51AA`, `62DFCCEC`, `22CEFD99`, `A2E3D533`, `23DFAE5F` | CR LF separated lines. |

Record primitives (used inside all binary payloads and in the save database): u8, u16, u32, s32,
f32 little-endian; **string** = `u32` byte length N + N bytes, no terminator; **resource key** =
12 bytes `u32 Type, u32 Group, u32 Instance` (note: T, G, I order, unlike the IXF index).
[STATIC] [DATA]

The string type `2026960B` is the only string-table type. Text inside `TKB1` records refers to
strings by key (`2026960B:A1096A4F:n` building names, `2026960B:62E69238:n` catalogue text).
[DATA]

## Open questions

* `00006301` vs `00006300`: the exact semantic difference (multi-tile composite vs single) is for
  `13-buildings-occupants.md` to confirm. [OPEN]
* `220055E1` occurs only for one occupant class (20 records); its distinguishing meaning is
  [OPEN].
* Text-format fallback of `BIN` records is accepted by the readers but no shipped record uses it;
  its grammar is not specified. [OPEN]
