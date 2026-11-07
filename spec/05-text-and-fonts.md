# 05 — Text, localisation and fonts

Scope: string-table resources, language selection and language folders, text substitution tokens,
the `.FBF` bitmap font format and font usage.

Related documents: `02-containers.md` (IXF container), `03-resource-types.md` (type registry),
`08-ui-framework.md` (controls that draw text), `22-advisors-news-petitioners.md` (users of the
advisor/ticker/petitioner string tables), `27-platform.md` (registry keys).

## 1. String tables

### 1.1 Files

String tables are IXF containers (see `02-containers.md`) stored in `Res/Text/<LanguageFolder>/`
with the extension `.IXF` (one file uses lower-case `.ixf`). Each language folder holds the same
set of file names (about 68 files), plus fonts (`*.FBF`) and a `Publish.html`/`PUBLISH.JPG` pair.
[DATA]

Shipped language folders: `ENGLISH`, `English-UK`, `FRENCH`, `GERMAN`, `ITALIAN`, `DUTCH`,
`SWEDISH`. File-name case differs between folders (e.g. `main10.fbf` vs `MAIN10.FBF`); the original
runs on a case-insensitive file system, so an implementation on a case-sensitive system must match
folder and file names case-insensitively. [DATA]

### 1.2 Resource key

| Key part | Value | Meaning |
|----------|-------|---------|
| Type | `0x2026960B` | "string" resource type (same for every table) |
| Group | table ID (one per file, e.g. `0x209E6378` for `SC3StringsApp.IXF`) | selects the table |
| Instance | string ID | selects the string within the table |

[STATIC][DATA] Within the IXF index the three key fields are stored in the on-disk order
Group, Instance, Type (see `02-containers.md`). The language folders hold 477 `.IXF` files: 472
readable string tables and 5 four-byte placeholders that cannot be opened (§1.4). Every one of the
55,952 live index entries in the 472 readable files has Type `0x2026960B`. [DATA]

Program code refers to a string by the pair (Group, Instance). Several files may share one Group
(for example the base-game and add-on advisor heading files both use Group `0x825ADC5A`); since all
files of the language folder are mounted into one resource database, their entries merge into one
logical table. Instance IDs do not collide between such files. [DATA]

### 1.3 Record payload

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | length | Number of characters N that follow |
| 0x04 | N | u8[N] | text | Characters, 8-bit, no terminator |

[DATA] All 55,952 records parse with this layout.

* The index entry's `size` field is **not reliable**: in 443 files it equals N+4, but in 29 files,
  **all in the `ENGLISH` folder** (e.g. its newsticker, petitioner and advisor tables; 2,753
  records), it equals N (the length prefix is not counted). The text records inside the `.SNR`
  scenarios have the same defect (`02-containers.md` §2.3). A reader must take the length from the payload prefix and ignore the
  index size. Records never overlap. [DATA]
* Encoding: single-byte Windows ANSI code page. All shipped languages use Windows-1252
  (accented Latin letters appear as bytes 0xC4–0xFC, typographic quotes/dashes as 0x84, 0x96,
  0x97, 0x9C, etc.). Decode as Windows-1252. [DATA][STATIC]
* Line breaks are a single LF (0x0A). A handful of strings contain CR, 0x03 or 0x04 control bytes
  [DATA]; see §3 for their meaning.
* No UTF-16 string tables exist. [DATA]

### 1.4 Empty/placeholder files

`Sc3HolidaysStringsPetitionerTickerY.IXF` is 4 bytes (magic only) in the `DUTCH`, `English-UK`,
`FRENCH`, `GERMAN` and `SWEDISH` folders. It has no index terminator, so under the container rules
**opening it fails** and it contributes no strings (`02-containers.md` §2.2 rule 4). No
`[DBSegments]` line refers to it, so this never matters in practice. [STATIC][DATA]

Index reading, free slots (offset or size = 0xFFFFFFFF; the key fields are not examined) and the
data start (4 + 0x5000 × k, because the index is preallocated) follow `02-containers.md` §2.2 and
§2.4. [DATA]

### 1.5 Table ID registry (Group values)

Table IDs are the same in every language folder. [DATA]

| File | Group | Strings (EN) |
|------|-------|--------------|
| `SC3StringsApp.IXF` | `0x209E6378` | 91 |
| `SC3StringsMenu.IXF` | `0x03C09AFF` | 8 |
| `SC3StringsTOD.IXF` | `0x03C09B1B` | 62 |
| `SC3StringsMessage.IXF` | `0x015E410C` | 47 |
| `SC3StringsGUI.IXF` | `0x225872FE` | 135 |
| `SC3StringsWindow.IXF` | `0x029541F4` | 462 |
| `SC3StringsQuery.IXF` | `0x82E0074C` | 428 |
| `SC3StringsSim.IXF` | `0xC29A6083` | 155 |
| `SC3StringsCredits.IXF` | `0x830CDF29` | 378 |
| `Sc3StringsBuildings.IXF` | `0xA1096A4F` | 1120 |
| `SC3StringsBuildings_Holidays.IXF` | `0xA1096A4F` | 3 |
| `SC3MBEStringsBuildings.IXF` | `0x453BBDE0` | 498 |
| `SC3StringsBldCatalog.IXF`, `SC3StringsBldCatalog_Holidays.IXF` | `0x62E69238` | 105 + 2 |
| `SC3StringsBldCatalog_2.IXF` | `0x1A15D9EE` | 15 |
| `SC3StringsAdvisorHelp.IXF` | `0xE26EC3B8` | 107 |
| `SC3StringsAdvisorHelpHeadings.IXF` | `0xA26EC340` | 33 |
| `SC3StringsAdvisorHelpTitles.IXF` | `0x82B85965` | 107 |
| `Sc3StringsAdvisorHeading.IXF`, `Sc3MBEStringsAdvisorHeading.IXF` | `0x825ADC5A` | 212 + 12 |
| `Sc3StringsAdvisorMessage.IXF`, `Sc3MBEStringsAdvisorMessage.IXF` | `0x225ADC6B` | 219 + 12 |
| `Sc3StringsAdvisorTicker.IXF`, `Sc3MBEStringsAdvisorTicker.IXF` | `0xC25B11C3` | 200 + 12 |
| `SC3StringsNewstickerMessages.IXF` | `0xC2498F0E` | 247 |
| `SC3StringsNewstickerTriggered.IXF`, `SC3HolidaysStringsNewstickerTriggered.IXF` | `0x42C1ED2D` | 328 + 9 |
| `SC3MBEStringsNewstickerTriggered.IXF` | `0x547BA060` | 49 |
| `Sc3StringsPetitionerAdvice.IXF` | `0xE26DD125` | 109 |
| `Sc3StringsPetitionerButton1.IXF` | `0xA26DD23B` | 109 |
| `Sc3StringsPetitionerButton2.IXF` | `0xE26DD247` | 109 |
| `Sc3StringsPetitionerHeading.IXF` | `0xC26DD29B` | 183 |
| `Sc3StringsPetitionerMessage.IXF` | `0xC26DD2AB` | 183 |
| `Sc3StringsPetitionerName.IXF` | `0x826DD2D0` | 182 |
| `Sc3StringsPetitionerReaction1.IXF` | `0xE26DD2FA` | 109 |
| `Sc3StringsPetitionerReaction2.IXF` | `0x226DD30B` | 109 |
| `Sc3StringsPetitionerTicker.IXF` | `0x026DD322` | 176 |
| `Sc3StringsPetitionerTickerX.IXF` | `0xE27EBF4D` | 102 |
| `Sc3StringsPetitionerTickerY.IXF` | `0xE27EBF70` | 102 |
| `Sc3HolidaysStringsPetitioner{Advice,Button1,Button2,Heading,Message,Name,Reaction1,Reaction2,Ticker,TickerX}.IXF` | `0x549B17B2`, `0x449B17B8`, `0x449B17BC`, `0x249B17CB`, `0x249B17CE`, `0x649B17D1`, `0x049B17D4`, `0x649B17D8`, `0x649B17DB`, `0x549B17DF` | 4 each (2 for TickerX) |
| `SEStringsUI.IXF` | `0x041F2625` | 137 |
| `SC3PStringsUI.ixf` | `0x541F07DB` | 13 |
| `SC3SEStringsScenarioScrpt.IXF` | `0x444288FD` | 56 |
| `BATStringsMain.IXF` | `0x63DE4715` | 174 |
| `BAMBEStringsMain.IXF` | `0x9A0884AD` | 55 |
| Scenario text files (`Betterfeld`, `Criminalville`, `FallOfTheWall`, `Frankfurfest`, `LondonFires`, `LordOfTheLocust`, `RagsToRiches`, `SeoulWorldCup`, `SeparationAnxiety`, `TopOfTheWorld`, `TransportationTutorial`, `TutorialOnGettinStarted` (spelled without the second `g` in every
folder; `English-UK` additionally holds a copy named `TutorialOnGettingStarted`),
`TutorialOnSeaAndAir`, `TAICHUNG` (English only)) | one Group each, e.g. `0xFA47D792` (Betterfeld) | 38–86 |

The scenario text Group IDs are referenced from the scenario files (see `23-scenarios.md`).

### 1.6 How string tables are mounted and looked up

String tables are **not** discovered by scanning the folder. They are mounted through the
`[DBSegments]` section of the packed INI `Sys/Default.ini` (inside `Sys/SYS.PAK`; see
`02-containers.md`). Each line has the form `segmentId=0x80C381D7;path`, where `0x80C381D7` (the IXF
magic as a little-endian u32) selects the IXF reader and `path` is relative to `Apps/`. For most
string tables the segment ID equals the table's Group ID, but this is only a convention: the
add-on advisor files are mounted under their own segment IDs (`0x447B7587`
`SC3MBEStringsAdvisorHeading.IXF`, `0x447B758B` `Sc3MBEStringsAdvisorMessage.IXF`, `0x547B757F`
`Sc3MBEStringsAdvisorTicker.IXF`) although their records use the base files' Groups
(`0x825ADC5A`, `0x225ADC6B`, `0xC25B11C3`). The path contains `%Language%`, e.g.
`0x03c09b1b=0x80c381d7;Res\Text\%Language%\SC3StringsTOD.IXF`. `%Language%` is replaced by the folder
name of the current language (§2.1). [DATA][STATIC]

* 65 lines of `[DBSegments]` mount text files (including the 14 scenario tables). A base file and its add-on file are combined in one of two ways,
  and both have the same effect, because entries of all mounted segments merge by key
  (`02-containers.md` §7.4): either the same segment ID is listed twice (`0x42C1ED2D`,
  `0x62E69238`, `0xA1096A4F`; every such line is mounted), or the add-on file gets its own segment
  ID (the three advisor tables above). [DATA][STATIC]
* Not mounted (and not referenced by any program code): `Sc3StringsPetitionerTickerX.IXF`,
  `Sc3StringsPetitionerTickerY.IXF`, `Sc3HolidaysStringsPetitionerTickerX.IXF`. An implementation may
  ignore them. [DATA][STATIC]
* Scenario text files are ordinary `[DBSegments]` lines too, mounted at start-up like every
  other table and never unmounted: 14 lines, segment ID = Group (e.g.
  `0xfa47d792=0x80c381d7;Res\Text\%Language%\Betterfeld.IXF`; `Taichung.IXF` as `0x7A6C621F`).
  Each `.SNR` refers to its table by that Group (`23-scenarios.md` §6). [DATA]

Lookup:

1. A string reference (Group, Instance) resolves to the record with key (0x2026960B, Group, Instance)
   in the mounted segments.
2. A missing string yields an empty string. [STATIC] Instance `0xFFFFFFFF` exists in some tables as a
   developer placeholder and is never referenced. [DATA]

## 2. Language selection

### 2.1 Language IDs and folder names

The game uses a numeric language ID (1–43). Only IDs 1–20 have folder names; IDs 21–43 have no
folder and can never be available. [STATIC]

| ID | Folder | Locale name | | ID | Folder | Locale name |
|----|--------|------|-|----|--------|------|
| 1 | `English` | english-us | | 11 | `Finnish` | finnish |
| 2 | `English-UK` | english-uk | | 12 | `Hebrew` | hebrew |
| 3 | `French` | french | | 13 | `Russian` | russian |
| 4 | `German` | german | | 14 | `Portuguese` | portuguese |
| 5 | `Italian` | italian | | 15 | `Japanese` | japanese |
| 6 | `Spanish` | spanish | | 16 | `Polish` | polish |
| 7 | `Dutch` | dutch | | 17 | `SimplifiedChinese` | chinese-simplified |
| 8 | `Danish` | danish | | 18 | `TraditionalChinese` | chinese-traditional |
| 9 | `Swedish` | swedish | | 19 | `Thai` | thai |
| 10 | `Norwegian` | norwegian | | 20 | `Korean` | korean |

Folder names are matched case-insensitively (`ENGLISH` on disk = `English`). [STATIC][DATA]

### 2.2 Availability

At start-up, for each ID 1–43 the program checks whether the file
`Res/Text/<Folder>/SC3StringsApp.IXF` exists. A language is **available** only if it does. With the
shipped data the available set is {1, 2, 3, 4, 5, 7, 9}. [STATIC][DATA]

### 2.3 Choosing the language

1. Read the registry value `Language` under
   `HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited` (key path as stored
   by the installer, see `27-platform.md`).
   * If it is a DWORD below 44, it is a language ID.
   * If it is a DWORD of 44 or more, it is a Windows language identifier (LANGID, low 16 bits of an
     LCID) and is mapped with the table in §2.4.
   * If it is a string, it is matched (case-insensitively) against the language names of §2.1.
   The GOG installer's language chooser (`language_setup.ini` in the game root) writes Windows
   LANGIDs: 1033 (0x0409, English), 2057 (0x0809, English-UK), 1036 (French), 1040 (Italian),
   1031 (German), 1043 (Dutch), 1053 (Swedish). [DATA]
2. Likewise read the DWORD `Country` value under the same key (accepted if below 57); it selects the
   country used by localised sounds and parade names (§3.3). Default country: 54 (United States).
3. If step 1 produced a language that is available, use it.
4. Otherwise a stored "Default Locale"/"Language" preference is tried [OPEN: its exact storage]; if
   that is missing or unavailable, the current default is kept. The default language is ID 1
   (`English`). [STATIC]
5. Quirk: if the Ctrl or Shift key is held down when the language is chosen at start-up
   (`01-architecture.md` §4 step 7), steps 1–4 are skipped and the defaults stay in effect
   (language 1 `English`, country 54). [STATIC]
6. The text code page follows the chosen language: Windows-1252 for every shipped language
   (§1.3). [STATIC]

Resulting behaviour for an implementer: language folder = folder of the chosen ID if available, else
`English`. There is no per-file fallback: every string table is loaded from the one chosen folder.
[STATIC]

### 2.4 LANGID → language ID mapping

Applied when the registry holds a Windows LANGID, and usable to pick a default from the OS locale.

| LANGID values | Language ID |
|---------------|-------------|
| 0x0809, 0x0C09, 0x1409, 0x1809, 0x1C09, 0x2009, 0x2409, 0x2809, 0x2C09, 0x3009, 0x3409 (English: UK, Australia, NZ, Ireland, South Africa, Jamaica, Caribbean, Belize, Trinidad, Zimbabwe, Philippines) | 2 (English-UK) |
| 0x040C, 0x080C, 0x0C0C, 0x100C, 0x140C, 0x180C | 3 |
| 0x0407, 0x0807, 0x0C07, 0x1007, 0x1407 | 4 |
| 0x0410, 0x0810 | 5 |
| 0x040A, 0x0C0A, 0x100A … 0x500A (all Spanish except 0x080A) | 6 |
| 0x080A (Spanish-Mexico) | 41 |
| 0x0413, 0x0813 | 7 |
| 0x0406 | 8 |
| 0x041D, 0x081D | 9 |
| 0x0414, 0x0814 | 10 |
| 0x040B | 11 |
| 0x040D | 12 |
| 0x0419 | 13 |
| 0x0416, 0x0816 | 14 |
| 0x0411 | 15 |
| 0x0415 | 16 |
| 0x0804, 0x1004, 0x1404 | 17 |
| 0x0404, 0x0C04 | 18 |
| 0x041E | 19 |
| 0x0412, 0x0812 | 20 |
| 0x0439 | 21 |
| 0x0401, 0x0801 … 0x4001 (Arabic) | 22 |
| 0x0402 | 23 |
| 0x0422 | 25 |
| 0x0405 | 26 |
| 0x0408 | 27 |
| 0x040E | 28 |
| 0x040F | 29 |
| 0x0418 | 30 |
| 0x041B | 32 |
| 0x041C | 33 |
| 0x041F | 34 |
| 0x042A | 43 |
| anything else (including 0x0409 US and 0x1009 Canada) | 1 |

[STATIC] The table covers every 16-bit value: any value not listed maps to 1.

### 2.5 English-UK

`English-UK` is a complete, separate language folder (ID 2), not an overlay: it contains the full set
of string tables and fonts. It is selected when the registry language is 2 or a UK-family LANGID.
[STATIC][DATA]

### 2.6 Other localised paths

`%LANGUAGE%`/`%Language%` in resource paths (`Sys/Default.ini`), internal paths and URLs expands to the folder name of the current
language (for example `Res/Text/%LANGUAGE%/Publish.html`). Other per-language folders use the same
folder names: `Res/SSimData/<Folder>/`, `Res/BA/HOTKEYS/<Folder>/`, `Scripts/<Folder>/`. [STATIC][DATA]

## 3. Formatting and substitution tokens

### 3.1 Character-level conventions

* LF (0x0A) = line break. CR (0x0D) occurs in one string and is ignored by the renderer. [DATA][INFERRED]
* Four strings in `SC3StringsApp.IXF` (instances 0x8D, 0x8F, 0x91 = byte 0x03; 0x93 = byte 0x04)
  consist of a single control byte; they are separator/format codes used by the number formatter
  (e.g. digit-grouping and decimal settings) and are not displayed as text. [DATA][INFERRED]
* No colour or style escape codes occur in shipped strings; colours and fonts are chosen by the
  control that displays the string (see §5 and `08-ui-framework.md`). [DATA]

### 3.2 printf-style fields

Some strings are printf format strings filled by program code: `%s` (string), `%d` (signed decimal),
`%u` (unsigned decimal), `%x` (hex). A literal percent sign is written `%` followed by a non-format
character (e.g. "50% ..."). The argument order is fixed by the calling code; translations keep the
same order. [DATA]

### 3.3 Named tokens `%NAME%`

Named tokens are replaced at display time. The general text expander (used for advisor, ticker,
petitioner and scenario text) knows: [STATIC]

| Token | Replaced by |
|-------|-------------|
| `%MAYOR%`, `%YOURNAME%` | the mayor's name of the current city |
| `%CITYNAME%`, `%YOURCITY%` | the current city's name |
| `%POPULATION%` | current city population, formatted with digit grouping |
| `%YEAR%` | current simulation year |
| `%ANYNEIGHBOR%` | the name of a neighbour chosen **uniformly at random** among the four slots (0–3), drawn anew for each expansion with a time-seeded generator [STATIC] |
| `%PARADENAME%` | a parade name chosen by **country** (see below) |

`%PARADENAME%` mapping (country ID → string `0x041F2625:<instance>` in `SEStringsUI.IXF`): [STATIC]

| Country ID (name) | Instance |
|-------------------|----------|
| 6 (Brazil) | 0x258 |
| 17 (France) | 0x259 |
| 19 (Germany) | 0x25A |
| 31 (Japan) | 0x25B |
| 32 (Korea) | 0x25C |
| 45 (Spain) | 0x25D |
| 46 (Sweden) | 0x25E |
| 49 (Taiwan) | 0x25F |
| 54 (United States) | 0x260 |
| any other | token left as-is/empty [OPEN] |

Further tokens are substituted by the subsystem that owns the message (each defines the value): [DATA][STATIC]

| Token | Used in | Meaning |
|-------|---------|---------|
| `%NEIGHBOR%` | advisor messages | the neighbour concerned by the message: for a neighbour-deal offer, the deal's neighbour; for the four per-neighbour advisor topics (types 0x14–0x17), neighbour slot 0–3 respectively [STATIC] |
| `%X%`, `%Y%`, `%Z%`, `%W%` | advisor/petitioner messages | message-specific numeric or text arguments (counts, amounts, names) |
| `%LANDMARK%` | ticker | name of a landmark |
| `%YEAROFDEAL%` | petitioner/neighbour deal text | year a deal was made |
| `%SCN_NAME%` | scenario script text | scenario name |
| `%x%`, `%y%` | scenario editor UI | coordinates |
| `%s%`, `%d%` | BAT/query strings | positional arguments |

Token names are case-sensitive and delimited by `%` on both sides. See
`22-advisors-news-petitioners.md` for the values supplied per message.

## 4. FBF bitmap fonts

### 4.1 Files

`.FBF` files live in each language folder: `SYSTEM9`, `MAIN9`, `MAIN10`, `TITLE9`, `TITLE12`,
`SERIF17`, `systemloadsave`, `systemloadsavebat` (8 fonts × 7 folders = 56 files). The eight fonts
are byte-identical in all seven shipped folders. [DATA]

### 4.2 Layout

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | version | 1 |
| 0x04 | 4 | u32 | width | Atlas width in pixels (640 in all files) |
| 0x08 | 4 | u32 | height | Atlas height in pixels |
| 0x0C | 4 | u32 | format | 1 = 8-bit indexed pixels followed by a 256-entry 16-bit palette (only value used) |
| 0x10 | width × height | u8[] | pixels | Glyph atlas, row-major, top row first, one palette index per pixel |
| 0x10 + W·H | 512 | u16[256] | palette | RGB565 colours (bits 15–11 red, 10–5 green, 4–0 blue) |
| 0x210 + W·H | 5120 | glyph[256] | glyphs | One record per character code 0–255 |

Glyph record (20 bytes):

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | s32 | left | Atlas x of the glyph's left edge (inclusive) |
| 0x04 | 4 | s32 | top | Atlas y of the top edge (inclusive) |
| 0x08 | 4 | s32 | right | Atlas x of the right edge (exclusive) |
| 0x0C | 4 | s32 | bottom | Atlas y of the bottom edge (exclusive) |
| 0x10 | 4 | s32 | advance | Horizontal pen advance in pixels |

File size = 16 + W·H + 512 + 5120 exactly. [DATA][STATIC]

Character codes are the bytes of the Windows-1252 string; every code 0–255 has a record. All glyphs
of one font have the same height (bottom − top), which is the line height; glyphs are drawn with their
top at the line's top (no separate baseline/bearing fields). Width = right − left; the advance is
normally width + 1 (one pixel spacing). There is **no kerning table**. [DATA]

| Font | Atlas H | Glyph H | Pixel kind |
|------|---------|---------|------------|
| `SYSTEM9` | 28 | 14 | 2-level (ink/background) |
| `MAIN9` | 42 | 14 | 2-level |
| `MAIN10` | 48 | 16 | 2-level |
| `TITLE9` | 54 | 18 | anti-aliased, 16 levels |
| `TITLE12` | 92 | 23 | anti-aliased, 16 levels |
| `SERIF17` | 145 | 29 | anti-aliased, 16 levels |
| `systemloadsave` | 51 | 17 | full colour |
| `systemloadsavebat` | 48 | 16 | full colour |

[DATA]

### 4.3 Pixel meaning

* Mono/anti-aliased fonts use indices 1–17 only: index 1 = full ink, indices 2–15 = decreasing ink
  coverage, index 16 = background (no ink), index 17 = atlas filler between glyphs (never inside a
  glyph rectangle in practice). [DATA]
* **Drawing is a palette blit, not a blend** [STATIC]. The
  loader creates an 8-bit surface and loads the file's 565 palette into it unchanged. When the
  font's colour-key flag is set, the colour of atlas pixel (0, 0) becomes the source colour key
  (in `TITLE12` that pixel is index 16, the background shade). Each glyph is then drawn with a
  plain surface blit: every non-key pixel is written as its palette colour, and the destination is
  never read. Consequence: anti-aliased edge pixels keep the shade the font was pre-mixed
  against, even over a textured background. [OBSERVED] The window-title text of `TITLE12` at
  800×600 reproduces the stored palette entries exactly: indices 1–16 as (R5, G6), B5 = 31:
  (27,55) (27,54) (26,53) (25,51) (24,50) (23,49) (23,47) (22,46) (21,45) (20,44) (19,42)
  (19,41) (18,40) (17,38) (16,37) (15,36).
* Palette recolour [STATIC]: a font palette can be rebuilt for another
  text colour T and background B: unpack both to 8 bits (r8 = r5·255/31, g8 = g6·255/63, floored),
  set entry i+1 = pack(T + trunc((B − T)·i/16)) for i = 0..14 per channel, entry 16 = B, and pack
  by truncation (`r>>3`, `g>>2`, `b>>3`). Frame (window-bar) titles do **not** use it: they are
  drawn with the file palette as stored. Caller-coloured anti-aliased text does use it
  [OBSERVED, exact on every level]: the scenario start dialog title (T = (27,55,31),
  B = `TITLE12` entry 16 = (15,36,31)) and the Budget window's "Income" tab caption
  (T = (49,52,123); B = 565 (21,47,31), the background entry of the `MAIN*`/`SYSTEM9` fonts, which
  is not the caption font's own entry 16). Where B comes from for each caller is open. For the
  non-anti-aliased fonts (`MAIN9`, `MAIN10`, `SYSTEM9`, whose glyphs use only indices 1, 16, 17)
  only entry 1 matters, i.e. the ink becomes the requested text colour. [INFERRED]
* The `systemloadsave*` fonts are multi-coloured (indices across the palette); they are drawn with
  their palette colours, index of colour 0xF81F (magenta) being transparent. [DATA][INFERRED]

Verification: all 56 shipped FBF files parse with this layout, every glyph rectangle lies inside
the atlas, and sample text rendered from the atlas shows legible glyphs including accented
characters. [DATA]

## 5. Font usage

At start-up the shell registers fonts under numeric **font IDs** (labels only; not needed by an
implementation, which needs only the role of each font). Files are loaded from
`Res/Text/<LanguageFolder>/`. [STATIC]

| File | Font IDs | Typical use [INFERRED] |
|------|----------|------------------------|
| `system9.fbf` | 0, 11 | default/system text, small labels |
| `main10.fbf` | 1, 2, 14 | main dialog and panel text |
| `title9.fbf` | 3, 12 | small titles |
| `title12.fbf` | 13, and the framework's default text font ("default UI font"; see below) | window titles, large headings, default for controls without an explicit font |
| `main9.fbf` | 15 (falls back to `system9` if missing) | dense lists, ticker |
| `Serif17.fbf` | 16 | newspaper/advisor headlines |
| `systemloadsave.fbf` | 17 | load/save dialog text |
| `systemloadsavebat.fbf` | none (the game does not load it) | used only by the Building Architect Tool |

`title12.fbf` is registered twice: as font ID 13 and as the framework's default text font, which
`08-ui-framework.md` §2.4 calls the "default UI font". The internal identifier of the default-font
slot occurs in no data file; where other documents quote it (`0xE2B14587`), it is a label only;
not needed by an implementation. No other aliases exist. [STATIC]

Default text colour assigned to every font: RGB (0x31, 0x36, 0x7C). (Other documents may cite the
colour's internal identifier `0x430743B8`: label only; not needed by an implementation.) [STATIC]

For the East-Asian and Thai languages (IDs 15, 17, 18, 19, 20) the shell substitutes Windows bitmap
`.FON` fonts instead of FBF: `font12j.fon`/`font16j.fon` (Japanese), `font16gh.fon` (Simplified
Chinese), `font16b5.fon` (Traditional Chinese), `font20th.fon` (Thai), `font12k.fon`/`font16k.fon`
(Korean); the 9/10-pixel slots use the 12-pixel variant and the title/serif slots the 16-pixel one,
and, for these languages only, one extra font slot (label only, not needed by an implementation:
1001) is mapped to `systemloadsave.fbf`. Language 17 (Simplified Chinese) uses the GB-encoded
`font16gh.fon` and language 18 (Traditional Chinese) the Big5-encoded `font16b5.fon`; for languages
15–20 the default UI font is the 16-pixel variant (`font16j.fon`, `font16gh.fon`, `font16b5.fon`,
`font20th.fon`, `font16k.fon`). None of these languages ship with the GOG build. [STATIC][DATA]

## 6. TrueType fonts

No `.ttf` files ship and the game draws no player-visible text with TrueType fonts. An
implementation needs only the FBF fonts of §4 (and the `.FON` substitutes above for unshipped
languages). [DATA][STATIC]

## Open questions

* §3.2 printf fields [OPEN]: is the formatter full C printf (flags, width, precision such as
  `%3d`, `%.1f`, and `%%`) or a reduced one? Shipped strings use only `%s`, `%d`, `%u`, `%x` and a
  bare `%` before a non-format character; an implementation should accept exactly those and leave
  any other `%` sequence as literal text.
* §3.2/§3.3 token precedence [OPEN]: when a string contains both `%NAME%` tokens and printf
  fields (`%s%`, `%d%` look like both), which expansion runs first? Recommended: expand complete
  `%NAME%` tokens first, then printf fields.
* §3.1 number formatting [OPEN]: the digit-grouping character, decimal rule and currency-symbol
  placement encoded by the 0x03/0x04 strings are not specified. Until resolved, group digits in
  threes with the separator of the language's Windows locale.
* §4.3 anti-alias blend [OPEN]: gamma and rounding of the linear blend.

* Storage location of the "Default Locale"/"Language" fallback preference (§2.3 step 4).
* Exact blending formula for anti-aliased FBF levels (linear coverage assumed).
* Meaning of the 0x03/0x04 control-byte strings in `SC3StringsApp.IXF`.
* `%PARADENAME%` behaviour for countries not in the table.
