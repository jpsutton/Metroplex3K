# 28 — Conformance and verification

This document tells the implementation team how to check that an engine built from this
specification is compatible with the original data and faithful to the original behaviour. Every
check here can be run with the user's own installation of the game. None needs access to the
original program's code.

Black-box observation of the original game is permitted for the clean team. That means running it,
for example under Wine, and recording what it displays and what files it writes. Several checks
below compare against such recordings.

## 1. Conformance levels

| Level | Name | Meaning |
|---|---|---|
| L0 | Data reading | Every shipped data file the original reads is parsed without error, with the counts given in §2 |
| L1 | Round trip | Files the original writes (city saves `.sc3`, the BAT database `BAT.DAT`, preferences `SC3.cfg`) are read and re-written byte-identically, or with only the differences listed in §3. The retail game never writes terrains, starter towns or scenarios (`10-save-format.md` §8.1); these are only read, and a city started from one is saved as an ordinary `.sc3` |
| L2 | Static behaviour | Deterministic computations match the original exactly for the same inputs (§4) |
| L3 | Dynamic behaviour | Statistical behaviour of the running simulation matches within tolerance (§5) |
| L4 | Presentation | Screens, sounds and rendering match the original visually and audibly (§6) |

A release candidate should meet L0–L2 completely, plus L3 and L4 for every subsystem it claims to
support.

## 2. L0 — data reading checks

The expected counts below were measured on the GOG release 2.0.949 (English installer, all seven
language folders present). Counts are over the whole installation unless a path is given.

| Check | Expected result | Spec |
|---|---|---|
| Files starting with the IXF magic | 649 (536 `.ixf`, 37 `.dat`, 21 `.sct`, 17 `.bld`, 15 `.sc3`, 13 `.SNR`, 10 `.st3`) | 02 |
| IXF files that open | 644 of 649. The five `Sc3HolidaysStringsPetitionerTickerY.IXF` files outside `ENGLISH` reach end of file before the index terminator, and the original refuses to open them too. The engine must treat them as missing, not crash | 02, 05 |
| String tables (Type `2026960B`) | 55,952 records in 477 files parse. Use the length prefix of each string, not the index size, because 2,863 records state a size 4 bytes too small | 02, 05 |
| `.SII` sidecars | 10 of 10 parse. Every record matches the span record of the same sprite. One file states a record count one higher than the number of records present | 02, 04 |
| Sprite image payloads | 63,931 decode (63,691 in `Res/Sprites/**` plus 240 in `BARender/BAT.DAT`). Pixel encodings present: 565 RLE 62,702; 555 RLE 90; 8-bit opacity masks 1,139 | 04 |
| UI images (Type `62B9DA24`) | 729 of 729 decode | 04, 08 |
| Loose BMP files | 677 of 677 decode | 04 |
| `.FBF` fonts | 56 of 56 parse (8 distinct fonts, identical in every language folder) | 05 |
| `.XA` audio | 814 of 814 decode with no 16-bit overflow | 06 |
| `INTRO.TGQ` | 1,051 video chunks and 1,088 audio chunks | 07 |
| Occupant attribute records (`TKB1`) | 1,530 of 1,530 parse: 1,108 in the five occupant files of `Res/Occupant/` plus 422 inside the building-set and flora-set sprite archives (`Res/Sprites/BuildingSets/**` 314, `Res/Sprites/FloraSets/**` 108) | 03, 13 |
| Composite sprite records (Type `00006300`) | 1,425 of 1,425 parse | 25 |
| Automata attribute files in `Res/SSimData/**` | All 24 files and all 8 record kinds parse | 26 |
| `Res/TilingRules/*.txt` | 68 of 68 tokenise. The subset the game actually loads is listed in 12 | 12 |
| Hotkey tables | 7 of 7 files, 881 of 881 lines parse | 08 |
| `MenuItem.INI` | 90 items, 109 buttons and 21 menus. Every string reference resolves | 08, 09 |
| `SYS.PAK` | 51 entries. Each packed INI decodes to text | 02 |
| Saves (`.sc3`, `.st3`, `.sct`, `.SNR`) | 59 of 59. Each packed segment decompresses to exactly its stated raw size, and each inner database parses with the directory ending exactly at the end of the data | 10 |
| Scenario records | 13 of 13 `.SNR` files. All scripts parse | 23 |
| Scenario Creator projects (`.SSC`) | 14 of 14 parse. The game itself never reads them | 23 |
| BAT buildings | 17 of 17 `.bld` files and 15 of 15 buildings in `BAT.DAT` | 24 |

## 3. L1 — round-trip checks

1. **RefPack.** Compressing each of the 59 decompressed save databases with the encoder described in
   `02-containers.md` §6.4 reproduces the shipped compressed stream byte for byte. Matching the
   original encoder is optional, because any valid stream loads. It is still recommended, since it
   lets L1 be checked by byte comparison.
2. **Saves.** For each of the 15 shipped `.sc3` cities: load the file, then save it immediately
   without advancing the clock. The result must equal the original, apart from these differences,
   which a comparison tool should normalise:
   * optional marker strings in records written with the marker flag (see 10 §2.1);
   * uninitialised filler bytes that the original wrote (noted per record in 10 and 21).

   The inner directory must be written sorted by (Group, Instance, Type) as the original does
   (10 §5.1), so it is not a normalised difference. For the 44 terrains, starter towns and scenarios,
   saving produces a city (10 §8.1, §8.6), so compare instead with the `.sc3` that the original
   writes when the same file is loaded and saved at once (black-box recording).

   Every field must survive the round trip. Fields whose meaning is still `[OPEN]` (status **P** in
   the 10 §5.2 catalogue) must be carried through unchanged, so that the original program can still
   load the file.
3. **Cross-load with the original.** Cities saved by the engine must load in the original game
   without error and must look the same: building count, funds, date, population and map appearance.
   Test this with a set of cities that exercise every subsystem.
4. **BAT database.** Importing a `.bld` into `BAT.DAT` must produce records that the original game
   can read (see 24).

### 3.1 Observed reference save

A city saved by the original right after **New City** (256 × 256, difficulty 1, one plopped
landmark, simulation not started) has been checked against `10-save-format.md`. All 11 outer and
58 inner records parse byte for byte, and the write order, index preallocation and directory
sorting match `[OBSERVED]`. Implementers should collect similar reference saves themselves:
* one empty new city per map size and difficulty;
* one city per placed building class.

Use them both as L1 fixtures and as templates for new-city record values (`29-open-issues.md` 1.1,
1.2).

A further five saves made the same way have also been checked; every record parses byte for byte
`[OBSERVED]`. They are:
* a fresh city;
* the same city with six civic buildings, a road and a residential zone, saved immediately;
* that city after 7 months, 17 months and 41 months of simulation at top speed.

Together they show:
* the fixed values written for newly placed buildings (`10-save-format.md` §6.2.5);
* that plant lifetimes stay constant once set;
* when disaster history seed entries appear;
* the city-information fields changing over time (day of year 1 → 214 → 147 → 148; speed 1 → 4).

Seven more were captured and also parse fully:
* fresh cities covering every map size, every difficulty, every start year, and both settings of
  the Disasters and Auto Budget options;
* non-default landscape, tree and building sets;
* a year-2000 city with a bus stop, school, college, library, water tower and wind mill.

Recommended fixture set for L1: one fresh city per (size, difficulty, start year) and one city per
placed building class, all saved by the original.

## 4. L2 — deterministic checks

These computations depend only on their inputs and the generator state, so they must match exactly.

| Item | How to test | Spec |
|---|---|---|
| Random number generators | Given a seed, the integer and real generators must reproduce the test vectors in `14-sim-clock.md` §8.1 | 14 §8 |
| Terrain generator | Same seed and parameters → same altitude grid, water grid and tree requests. Compare with a terrain created in the original game, using a seed captured by black-box observation (save immediately after New City) | 11 |
| Tiling rules | For a fixed set of network drags on flat and sloped ground, the chosen pieces and rotations match those in the original's saved networks. Shipped cities are a ready test set: re-tiling every network region of a loaded city must reproduce its stored pieces | 12 |
| Land-value formula | For a loaded city, one full land-value pass gives the stored layer to within the documented integer rounding | 15 |
| Calendar | Julian day ↔ (day, month, year) conversion over 1900–2999, including leap years | 14 |
| Budget arithmetic | For a loaded city with fixed tax and funding settings, a month's figures match the original's budget window | 19 |
| Draw order | For a fixed scene, the order of the sorted draw list matches the documented key | 25 |
| XA decoding | Matches the documented rounding exactly. Decoders with the reference ffmpeg rounding differ by at most 28 LSB | 06 |

## 5. L3 — dynamic behaviour checks

The simulation is stochastic. The simulation generators are seeded from the system clock, and the
zone developers and layer sweeps use the C-runtime generator, whose seed is not saved (see
`14-sim-clock.md` and `15-sim-growth.md`). Runs therefore cannot be
compared tick for tick. Compare distributions instead:

1. Load a shipped city in both the original and the engine, set the same speed and options, run
   10 simulated years three times each, and record population, R/C/I demand, funds, crime, pollution
   and traffic averages from the graphs window once a year.
2. The engine's mean for each series should fall inside the original's min–max band. Any
   deviation larger than 10 % calls for investigation.
3. Repeat for: a new city on a generated map, with fixed zoning built by a script; each scenario
   (win and lose paths); and a city with disasters triggered from the menu.

Behaviours that must be checked qualitatively:

* growth stages and redevelopment;
* abandonment under bad conditions;
* power and water outages when supply runs short;
* traffic jams;
* fire spread;
* strike cycles;
* advisor topics and petitioners appearing under the documented conditions;
* rewards unlocking;
* neighbour deals.

## 6. L4 — presentation checks

* Screenshots at each zoom level and rotation of the same shipped city, captured in the original
  and in the engine at the same resolution, compared side by side. Terrain shading, building
  placement, draw order and the positions of animated objects should match.
* Every UI screen in `09-ui-screens.md` at 640×480, 800×600 and 1024×768.
* Sound: tool sounds, ambient selection at each zoom, music rotation and disaster sounds.
* Text in every language folder, including the British English folder.

## 7. Known defects of the original that must be reproduced

Faithful behaviour includes several defects. They are collected here so that a reviewer does not
"fix" them by accident. Each is described in full in the owning document.

| Defect | Effect | Spec |
|---|---|---|
| `TripMaxDistanceToRoadDefault` is stored into the industrial walking radius | The industrial radius equals the "default" key value; the default stays at the built-in 6 | 18 |
| Traffic density added per trip is sign-extended from 8 bits | Very large lots subtract density | 18 |
| Land-value curve interpolation uses an absolute difference | Falling curve segments rise | 15 |
| The air-pollution land-value threshold is stored in one byte | The shipped 400 acts as 144 | 15 |
| The water-pollution land-value penalty is triggered by the air threshold | As stated | 15 |
| Residential "move to lower land value" search never succeeds | Houses below half their category's maximum land value always abandon | 15 |
| Tax-effect table interpolation uses population instead of the offset into the band | As stated | 19 |
| BAT model writer compresses too many bytes | A garbage tail follows the model cells in `.bld` files; readers must ignore it and writers may omit it | 24 |
| IXF delete-when-closed compaction | Broken in the original; do not copy it. Use free slots instead | 02 |

## 8. Suggested implementation order

1. Containers, RefPack, strings, images and fonts (02–05). Use this to build a resource browser
   that checks L0.
2. The save loader and writer with opaque pass-through of open records (10). Use this to check L1.
3. Terrain, rendering and the camera (11, 25), then networks and tiling (12), then the occupant
   model (13).
4. Clock and scheduler (14), then each simulation subsystem (15–21).
5. UI framework, screens and audio (06, 08, 09).
6. Advisors, ticker and petitioners (22); scenarios (23); BAT import (24); automata (26).
