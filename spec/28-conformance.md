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
| Files starting with the IXF magic | 649 (536 `.ixf`, 37 `.dat`, 21 `.sct`, 17 `.bld`, 15 `.sc3`, 13 `.SNR`, 10 `.st3`) in a fresh install. A played install has more: the game writes IXF-format `.cfg` files and a `.tmp` file, and every city the player saves adds one. Exclude those, or count only the listed extensions outside `Cities/` plus the shipped saves | 02 |
| IXF files that open | 644 of 649. The five `Sc3HolidaysStringsPetitionerTickerY.IXF` files outside `ENGLISH` reach end of file before the index terminator, and the original refuses to open them too. The engine must treat them as missing, not crash | 02, 05 |
| String tables (Type `2026960B`) | 56,062 records in 485 files parse: 55,952 in the 472 readable text files of `Res/Text/**` (477 files including the five placeholders above, which hold no records) plus 110 in the 13 `.SNR` scenario files. Per language folder: DUTCH 7,981, ENGLISH 8,003, ENGLISH-UK 8,061, FRENCH 7,982, GERMAN 7,982, ITALIAN 7,963, SWEDISH 7,980. Use the length prefix of each string, not the index size, because 2,863 records state a size 4 bytes too small | 02, 05 |
| `.SII` sidecars | 10 of 10 parse. Every record matches the span record of the same sprite. One file states a record count one higher than the number of records present | 02, 04 |
| Sprite image payloads | 63,931 decode (63,691 in `Res/Sprites/**` plus 240 in `BARender/BAT.DAT`). Pixel encodings present: 565 RLE 62,702; 555 RLE 90; 8-bit opacity masks 1,139 | 04 |
| UI images (Type `62B9DA24`) | 729 of 729 decode | 04, 08 |
| Loose BMP files | 677 of 677 decode | 04 |
| `.FBF` fonts | 56 of 56 parse (8 distinct fonts, identical in every language folder) | 05 |
| `.XA` audio | 814 of 814 decode with no 16-bit overflow | 06 |
| `INTRO.TGQ` | 1,051 video chunks and 1,088 audio chunks | 07 |
| Occupant attribute records (`TKB1`) | 1,530 of 1,530 parse: 1,108 in the five occupant files of `Res/Occupant/` plus 422 inside the building-set and flora-set sprite archives (`Res/Sprites/BuildingSets/**` 314, `Res/Sprites/FloraSets/**` 108). Count every record whose payload starts with `TKB1`, whatever its Type: the records use four Types (`207EDC0E`, `E223741F`, `FFD30C03`, `220055E1`; see 03 §3.1). Per file: `OccupantAttribs.IXF` 1,043, `OccupantAttribs_2.IXF` 41, `OccupantAttribs_Holidays.IXF` 16, `OccupantAttribs_German.IXF` 5, `HOLIDAYF.IXF` 3; `MBE_0015.dat` 170, `BST_0004.dat` 144; each of the four flora-set `.dat` files 27 | 03, 13 |
| Composite sprite records (Type `00006300`) | 1,425 of 1,425 parse | 25 |
| Automata attribute files in `Res/SSimData/**` | All 24 files and all 8 record kinds parse | 26 |
| `Res/TilingRules/*.txt` | 68 of 68 tokenise. The subset the game actually loads is listed in 12 | 12 |
| Hotkey tables | 7 of 7 files, 881 of 881 lines parse | 08 |
| `MenuItem.INI` | 90 items, 109 buttons and 21 menus. Every string reference resolves | 08, 09 |
| `SYS.PAK` | 51 entries. Each packed INI decodes to text | 02 |
| Saves (`.sc3`, `.st3`, `.sct`, `.SNR`) | 59 of 59. Each packed segment decompresses to exactly its stated raw size, and each inner database parses with the directory ending exactly at the end of the data | 10 |
| Scenario records | 13 of 13 `.SNR` files. All 116 scripts parse: 5,865 commands (5,780 with delay 0) and 9,271 arguments (7,836 kind 0, 1,435 kind 1) | 23 |
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

The simulation is stochastic. The random seed is not saved, so runs are not reproducible (see
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

### 5.1 Reference trajectories of the shipped cities `[OBSERVED]`

Six shipped cities were run in the original (GOG build 2.0.949 under Wine, 800 × 600 windowed)
from the unmodified shipped file, with nothing changed in the city: no budget, tool, ordinance or
deal changes. Disasters were left as stored in each file. Each city was loaded, set to speed 4
(Cheetah) and left running; a copy was saved about every 35–40 days with Save As while the
simulation kept running, until about day 480. Each row below was decoded from one of those saves.
"Day" is the number of days since the shipped file's date. Each city was run **once**: the
simulation is random (§5), so treat each row as one sample, not as an exact target. Row "shipped"
is the shipped file itself.

The saves are not distributed.

Columns: Pop = valve 3000 `Slast`; R/C/I V/cap = valve `V` and `capPct` of 3000/5000/8000; R D =
3000 `Dlast`; C and I S/D = 5000/8000 `Slast`/`Dlast`; Jobs S/D = world-layer jobs valve `Slast`/
`Dlast`; Unemp = newest unemployment chart sample; Lots = number of R/C/I/agriculture buildings
(agent types 3000–9999) by lot state with the `0x200` flag removed (1 occupied, 2 abandoned,
4 under construction, 8 rubble, 16 burnt); Codes = the counts of lot-evaluation outcomes of the
**previous completed pass** (`code:count`, occupied-lot codes 1–8 / empty-tile codes 9–18);
Power row = the current month row of the power production table (10 columns, `10-save-format.md`
§6.14); Water S/U = water-record supply/used; Garbage month = the seven current-month garbage
counters [0]…[6] (`16-sim-utilities.md` §4.5).

Summary of the population (valve 3000 `Slast`) over about 480 days:

| City | Shipped | Min | Max | Last (day) | Character |
|---|---|---|---|---|---|
| Madison | 148844 | 147507 | 151187 | 147507 (483) | stable (±2 %) |
| Moscow | 929879 | 895825 | 951443 | 950952 (467) | stable (−3.7 % … +2.3 %) |
| Madrid | 1176537 | 941203 | 1176537 | 941203 (472) | unstable: −13 % by day 33 (657 code-5 abandonments in that pass), then oscillates between 0.94 M and 1.11 M |
| Berlin | 794278 | 778042 | 812827 | 806102 (491) | stable (−2 % … +2.3 %) |
| London | 304900 | 304900 | 332566 | 332566 (485) | grows steadily (+9 %) |
| Craterville | 208780 | 203175 | 224522 | 211516 (486) | stable with one dip (−2.7 % … +7.5 %) |

Further observations from these runs [OBSERVED]:
* **Unzoned high land-value blocks are stale.** Moscow's 256 all-zone-0 blocks saved above 10
  (e.g. tile (114, 102) at 255) are 1 at day 71 and later; there are none left after the first
  full pass. Recompute them normally (this closes the open note of `15-sim-growth.md` §6.3 step 6g).
* **Craterville recycling.** The shipped ratio 0.2155 belongs to the month in which it was saved:
  one day after load the original writes 0.3233 (three working centres). During the run the ratio
  is 0.30–0.33, with one sample at 0.2005 (one centre zeroed that month).
* **Madrid pumps.** The 40 zero pumps of the shipped file all lie at z ≥ 236 (x 191–214; see
  `16-sim-utilities.md` §3.3 for the brown-out reading). The 17 of them at tiles (191..197, 246..255) have output 0 in the shipped file
  and at day 1, produce at days 33–96, are partly or fully zeroed again at days 127–218 and
  380–442, and produce at 249–349 and 472. Their lot state stays 1 and their saved status stays 0,
  so the zeroing comes from power, pollution or trips (`16-sim-utilities.md` §3.3), not from the
  ring test. The zero-pump months are followed, one pass later (the code counts lag by one pass), by
  passes with many code-4/code-5 abandonments (day 33: 657 code 5; day 218: 453).
  The saved water `supply` follows the pumps. When they produce, Madrid's pipe network splits
  into two 500-pass-capped components that count most facilities twice, and `supply` is about
  1.9 × the facility outputs: 228 528 at day 33, 228 131 at day 61, 223 473 at day 472. When
  they are off, a single component remains and `supply` = Σ outputs (83 837 shipped, 92 865 at
  day 218). A recomputation of both cases from the saved facilities and pipes reproduces the
  saved value exactly (`16-sim-utilities.md` §3.4 step 3).
* **Moscow deals.** The power buy deal (type 0, neighbour 0) keeps `units` 12 600 and monthly
  amount 504, and the water buy deal (type 2, neighbour 3) keeps 5 100 and 204, in every save.
  Within these 480 days the saved deal quantity does not change.
* Speed: the saved speed changes from the shipped value to 4 only because the runs were made at
  speed 4.

Further observations [OBSERVED]:
* **Madrid first-pass loss and rebound.** Residential lots by state and density (L = 3100/3300/
  3500, M = 3700/3900/4100, H = 4300–4700; agent types 3000–4999 only):

  | Save | Day | Occupied L/M/H | Abandoned L/M/H | Construction L/M/H |
  |---|---|---|---|---|
  | shipped | 0 | 5 152 / 3 822 / 928 | 267 / 186 / 120 | 1 283 / 196 / 28 |
  | save 1 | 1 | 5 140 / 3 801 / 918 | 259 / 185 / 111 | 1 369 / 215 / 34 |
  | save 2 | 33 | 6 006 / 3 580 / 854 | 130 / 251 / 94 | 1 336 / 283 / 77 |
  | save 3 | 61 | 6 842 / 3 755 / 897 | 186 / 246 / 96 | 436 / 113 / 34 |
  | save 4 | 96 | 6 956 / 3 773 / 916 | 267 / 237 / 94 | 205 / 122 / 22 |

  Residential lot changes between saves (a lot is the same lot when anchor and building are
  unchanged): days 1→33: 1 123 occupied lots removed (redeveloped or replaced), 1 180 new
  constructions, 969 constructions completed (4 → 1), 867 new lots placed and completed within
  the window, 185 abandoned (1 → 2), 114 re-occupied (2 → 1). Days 33→61: 1 108 completions
  (4 → 1), 13 re-occupations, 61 abandonments. So the rebound comes from **construction that
  completes in the following month**, mostly low-density residential replacing dry dense lots, not
  from re-occupation of abandoned lots.
* **Water service one month after load** (anchor tile bit of the saved bitmap, bit order of
  `10-save-format.md` §6.14): occupied R/C/I lots without water: Berlin shipped 4, day 1 4,
  day 40 **0**, day 73 11; Madrid shipped 9 399, day 33 **1**, day 61 2.
* **Land value and occupied lots.** In the original's own land-value grid, occupied R/C/I lots with
  `LV < ⌊minLV/2⌋` (the abandonment test of `15-sim-growth.md` §4.3): Berlin shipped 0, day 1 0,
  day 40 1, day 73 0; Europolis shipped 0.
* **Moscow power.** The previous-pass result codes contain **no** code 4 (unpowered) in any of the
  16 Moscow saves: no zoned lot was abandoned for lack of power in 480 days.
* **Berlin pollution and crime are steady over the first 40 days** (air and water
  pollution are values of the tile's 2 × 2 cell, crime is the cell value, and traffic is the sum
  of surface density over the cell's 4 tiles; shipped → day 40):

  | Tile | Air | Water | Crime | Traffic | LV |
  |---|---|---|---|---|---|
  | (60, 144) | 2 367 → 2 358 | 401 → 402 | 66 → 66 | 0 → 0 | 24 → 22 |
  | (20, 223) | 1 133 → 1 148 | 516 → 518 | 51 → 51 | 0 → 0 | 24 → 24 |
  | (50, 192) | 2 786 → 2 809 | 1 093 → 1 124 | 10 → 13 | 123 → 159 | 30 → 26 |
  | (11, 216) | 1 112 → 1 167 | 629 → 646 | 59 → 59 | 32 → 28 | 32 → 28 |
  | (61, 71) | 4 455 → 4 767 | 1 187 → 1 602 | 44 → 44 | 236 → 236 | 27 → 10 |

  The cells at (60, 144) and (20, 223) carry no traffic at all.
* **Madrid power margin.** The power-production row stays at 180 000–191 000 per month.
  Building demand (Σ of attribute 0xC9 per building record, `16-sim-utilities.md` §2.6) is
  208 000–218 000. The main network therefore browns out every month, and the cut falls in rows
  z ≈ 236–255, where the edge pumps are. Zero pumps per save:
  * day 96: none;
  * day 127: 6 (z ≥ 253);
  * day 218: 29 (z ≥ 246);
  * days 278 and 472: none.

  Small changes in demand move the cut across the pump rows, so the pumps toggle. Reproduce the
  band, not exact rows.

#### Sample rows (Madison, WI)

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 2032/94 | 148844 | 1600/20 | 1780/11 | 1540/23 | 171154 | 23932/25679 | 60607/82174 | 107853/85577 | 10 | 5615/1/188/0/0 | – / 16:42 | 0,0,28628,0,0,0,0,4195,4293,1187 | 60351/40438 | 7484,0,0,2088,0,5396,0 |
| save 1 | 1 | 2032/95 | 148844 | 1600/20 | 1780/11 | 1540/23 | 171154 | 23932/25679 | 60607/82174 | 107853/85577 | 10 | 5602/1/199/0/0 | – / 16:42 | 0,0,28628,0,0,0,0,4195,4293,1187 | 60351/40438 | 13776,0,0,3894,0,9882,0 |
| save 14 | 483 | 2033/212 | 147507 | 1600/20 | 1186/12 | 1500/25 | 179878 | 25024/25443 | 64068/81419 | 106862/89939 | 7 | 5640/1/164/0/0 | – / 16:42 | 0,0,28888,0,0,0,0,4327,4332,1201 | 60352/40526 | 140010,0,0,39368,0,100642,0 |

Per-save rows for all six cities (about 90 saves in total, same columns) exist in the owner's reference runs and are not distributed. The summary table and the observations above carry the facts an implementation needs.

*Run-to-run spread from one save* `[OBSERVED]`. The 6th Berlin save (8/6/2067) was loaded a second
time in the original and run without changes to 12/20/2067, with a save about every 15 days
(9 saves). The two original runs differ by as much as the engine
differs from either of them. Developer state changes over one month can differ by a factor of 5
between two original runs, so month-level counts from one trajectory cannot be used to check
growth rules.

| Measure, from the 6th Berlin save | First run | Second run |
|---|---|---|
| R lots replaced, state 1 → 4, to 9/12 (first run) / 9/18 (second run) | 131 | 13 |
| R lots removed from state 1, same period | 70 | 14 |
| I lots abandoned (1 → 2), same period | 41 | 12 |
| I valve V at 9/12 (first run) / 9/3 and 9/18 (second run) | −966 | 0, 0 |
| R lots in state 4 at 10/15 (first run) / 10/19 (second run) | 222 | 135 |
| I lots in state 2 at 10/15 (first run) / 10/19 (second run) | 439 | 358 |

In the second run, the committed R demand P (spec 15 §4) went from 872 at load to 1240 by
8/10/2067. That is above V = 1160, so R demand is used up early in each pass, as in the engine.

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
| The industrial walking radius is taken from the `TripMaxDistanceToRoadDefault` key | The industrial radius equals the "default" key value; the default stays at the built-in 6 | 18 |
| Traffic density added per trip goes negative for very large lots | Very large lots subtract density | 18 |
| Land-value curve interpolation between points of a falling segment rises instead | Falling curve segments rise | 15 |
| The air-pollution land-value threshold wraps at 256 | The shipped 400 acts as 144 | 15 |
| The water-pollution land-value penalty starts at the air-pollution threshold | As stated | 15 |
| Residential "move to lower land value" search never finds a target | Houses below half their category's maximum land value always abandon | 15 |
| Tax-effect table interpolation follows population rather than the position within the band | As stated | 19 |
| `.bld` files end with a garbage tail after the model cells | Readers must ignore it and writers may omit it | 24 |
| IXF delete-when-closed compaction | Corrupts the container in the original; do not copy it. Use free slots instead | 02 |

## 8. Suggested implementation order

1. Containers, RefPack, strings, images and fonts (02–05). Use this to build a resource browser
   that checks L0.
2. The save loader and writer with opaque pass-through of open records (10). Use this to check L1.
3. Terrain, rendering and the camera (11, 25), then networks and tiling (12), then the occupant
   model (13).
4. Clock and scheduler (14), then each simulation subsystem (15–21).
5. UI framework, screens and audio (06, 08, 09).
6. Advisors, ticker and petitioners (22); scenarios (23); BAT import (24); automata (26).
