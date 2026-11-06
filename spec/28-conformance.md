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

### 5.1 Reference trajectories of the shipped cities `[OBSERVED]`

Six shipped cities were run in the original (GOG build 2.0.949 under Wine, 800 × 600 windowed)
from the unmodified shipped file, with nothing changed in the city: no budget, tool, ordinance or
deal changes. Disasters were left as stored in each file. Each city was loaded, set to speed 4
(Cheetah) and left running; a copy was saved about every 35–40 days with Save As while the
simulation kept running, until about day 480. Each row below was decoded from one of those saves.
"Day" is the number of days since the shipped file's date. Each city was run **once**: the
simulation is random (§5), so treat each row as one sample, not as an exact target. Row "shipped"
is the shipped file itself.

The saves are kept outside the repository at `/home/jpsutton/Projects/sc3ku-spec/observed-runs/`
(`W5_<city>_sNN.sc3`, plus `W5_Madison_000.sc3`, a re-save straight after load).

Columns: Pop = valve 3000 `Slast`; R/C/I V/cap = valve `V` and `capPct` of 3000/5000/8000; R D =
3000 `Dlast`; C and I S/D = 5000/8000 `Slast`/`Dlast`; Jobs S/D = world-layer jobs valve `Slast`/
`Dlast`; Unemp = newest unemployment chart sample; Lots = number of R/C/I/agriculture buildings
(agent types 3000–9999) by lot state with the `0x200` flag removed (1 occupied, 2 abandoned,
4 under construction, 8 rubble, 16 burnt); Codes = the zone manager's result-code counts of the
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

Further observations, round 4 [OBSERVED]:
* **Madrid first-pass loss and rebound.** Residential lots by state and density (L = 3100/3300/
  3500, M = 3700/3900/4100, H = 4300–4700; agent types 3000–4999 only):

  | Save | Day | Occupied L/M/H | Abandoned L/M/H | Construction L/M/H |
  |---|---|---|---|---|
  | shipped | 0 | 5 152 / 3 822 / 928 | 267 / 186 / 120 | 1 283 / 196 / 28 |
  | s01 | 1 | 5 140 / 3 801 / 918 | 259 / 185 / 111 | 1 369 / 215 / 34 |
  | s02 | 33 | 6 006 / 3 580 / 854 | 130 / 251 / 94 | 1 336 / 283 / 77 |
  | s03 | 61 | 6 842 / 3 755 / 897 | 186 / 246 / 96 | 436 / 113 / 34 |
  | s04 | 96 | 6 956 / 3 773 / 916 | 267 / 237 / 94 | 205 / 122 / 22 |

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
* **Berlin pollution and crime are steady over the first 40 days** (round 5; air and water
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
* **Madrid power margin** (round 5). The power-production row stays at 180 000–191 000 per month.
  Building demand (Σ of attribute 0xC9 per building record, `16-sim-utilities.md` §2.6) is
  208 000–218 000. The main network therefore browns out every month, and the cut falls in rows
  z ≈ 236–255, where the edge pumps are. Zero pumps per save:
  * day 96: none;
  * day 127: 6 (z ≥ 253);
  * day 218: 29 (z ≥ 246);
  * days 278 and 472: none.

  Small changes in demand move the cut across the pump rows, so the pumps toggle. Reproduce the
  band, not exact rows.

#### Madison, WI

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 2032/94 | 148844 | 1600/20 | 1780/11 | 1540/23 | 171154 | 23932/25679 | 60607/82174 | 107853/85577 | 10 | 5615/1/188/0/0 | – / 16:42 | 0,0,28628,0,0,0,0,4195,4293,1187 | 60351/40438 | 7484,0,0,2088,0,5396,0 |
| W5_Madison_s01 | 1 | 2032/95 | 148844 | 1600/20 | 1780/11 | 1540/23 | 171154 | 23932/25679 | 60607/82174 | 107853/85577 | 10 | 5602/1/199/0/0 | – / 16:42 | 0,0,28628,0,0,0,0,4195,4293,1187 | 60351/40438 | 13776,0,0,3894,0,9882,0 |
| W5_Madison_s02 | 41 | 2032/135 | 149566 | 1600/20 | 1780/11 | 1540/24 | 172602 | 23429/25798 | 61831/82554 | 108352/86301 | 9 | 5612/1/187/0/0 | 6:1 / 16:42 | 0,0,28664,0,0,0,0,4320,4299,1187 | 60351/40422 | 61922,0,0,17233,0,44689,0 |
| W5_Madison_s03 | 80 | 2032/174 | 148995 | 1600/20 | 1500/12 | 1520/22 | 166264 | 24736/25699 | 57345/82237 | 107936/83132 | 9 | 5614/2/182/0/0 | 6:1 / 16:42 | 0,0,28588,0,0,0,0,4138,4287,1187 | 60351/40396 | 97384,0,0,27045,0,70339,0 |
| W5_Madison_s04 | 114 | 2032/208 | 150864 | 1600/20 | 1760/11 | 1560/23 | 169448 | 23886/26014 | 59776/83246 | 109260/84724 | 10 | 5669/3/132/0/0 | 6:2 / 16:42 | 0,0,28752,0,0,0,0,4178,4311,1187 | 60351/40397 | 117776,0,0,32560,0,85216,0 |
| W5_Madison_s05 | 153 | 2032/247 | 149887 | 1600/20 | 1386/12 | 1540/24 | 174210 | 24575/25842 | 61550/82695 | 108537/87105 | 10 | 5622/3/177/0/0 | – / 16:42 | 0,0,28808,0,0,0,0,4307,4320,1201 | 60351/40447 | 8600,0,0,2360,0,6240,0 |
| W5_Madison_s06 | 192 | 2032/286 | 148057 | 1600/20 | 1106/12 | 1520/24 | 176000 | 24592/25500 | 62466/81600 | 107100/88000 | 9 | 5639/2/165/0/0 | – / 16:42 | 0,0,28814,0,0,0,0,4316,4320,1201 | 60351/40447 | 52696,0,0,14612,0,38084,0 |
| W5_Madison_s07 | 226 | 2032/320 | 149992 | 1600/20 | 1386/11 | 1520/24 | 173820 | 23599/25828 | 62274/82651 | 108479/86910 | 8 | 5609/1/191/0/0 | – / 16:42 | 0,0,28746,0,0,0,0,4273,4311,1187 | 60351/40504 | 67294,0,0,18827,0,48467,0 |
| W5_Madison_s08 | 265 | 2032/359 | 148986 | 1600/20 | 1666/11 | 1520/24 | 173318 | 23938/25657 | 61714/82103 | 107760/86659 | 9 | 5657/0/138/0/0 | – / 16:42 | 0,0,28734,0,0,0,0,4209,4308,1187 | 60353/40486 | 111270,0,0,30883,0,80387,0 |
| W5_Madison_s09 | 298 | 2033/27 | 148745 | 1600/20 | 1386/12 | 1520/24 | 175296 | 24325/25618 | 62384/81978 | 107596/87648 | 9 | 5636/0/157/0/0 | – / 16:42 | 0,0,28838,0,0,0,0,4245,4323,1201 | 60351/40501 | 117568,0,0,32663,0,84905,0 |
| W5_Madison_s10 | 337 | 2033/66 | 148681 | 1600/20 | 1386/12 | 1500/25 | 177646 | 24628/25780 | 63302/82495 | 108275/88823 | 8 | 5603/1/195/0/0 | – / 16:42 | 0,0,28886,0,0,0,0,4286,4332,1201 | 60354/40502 | 27772,0,0,7694,0,20078,0 |
| W5_Madison_s11 | 377 | 2033/106 | 151187 | 1600/20 | 1666/11 | 1500/25 | 176824 | 23842/26212 | 63623/83878 | 110090/88412 | 8 | 5656/0/142/0/0 | – / 16:42 | 0,0,28936,0,0,0,0,4306,4338,1201 | 60355/40509 | 70770,0,0,19485,0,51285,0 |
| W5_Madison_s12 | 410 | 2033/139 | 150955 | 1600/20 | 1746/12 | 1500/25 | 177134 | 24361/26070 | 63284/83422 | 109492/88567 | 9 | 5604/1/201/0/0 | 6:1 / 16:42 | 0,0,28918,0,0,0,0,4324,4335,1201 | 60353/40509 | 80542,0,0,22039,0,58503,0 |
| W5_Madison_s13 | 450 | 2033/179 | 147637 | 1600/20 | 1466/12 | 1500/25 | 178382 | 24707/25498 | 63587/81595 | 107093/89191 | 9 | 5568/1/232/0/0 | – / 16:42 | 0,0,28830,0,0,0,0,4299,4323,1201 | 60352/40514 | 127764,0,0,35609,0,92155,0 |
| W5_Madison_s14 | 483 | 2033/212 | 147507 | 1600/20 | 1186/12 | 1500/25 | 179878 | 25024/25443 | 64068/81419 | 106862/89939 | 7 | 5640/1/164/0/0 | – / 16:42 | 0,0,28888,0,0,0,0,4327,4332,1201 | 60352/40526 | 140010,0,0,39368,0,100642,0 |

#### Moscow, Russia

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 2065/249 | 929879 | 960/52 | 1040/48 | 1400/13 | 1022124 | 409437/532765 | 131141/131428 | 593747/511062 | 7 | 22479/232/733/0/0 | 2:6 6:1 / 16:134 | 32897,0,0,0,0,116896,0,0,0,0 | 154277/134907 | 100776,0,0,0,100776,0,22232 |
| W5_Moscow_s01 | 1 | 2065/250 | 929879 | 960/52 | 1040/48 | 1400/13 | 1022124 | 409437/532765 | 131141/131428 | 593747/511062 | 7 | 22497/232/715/0/0 | 2:6 6:1 / 16:134 | 32501,0,0,0,0,116896,0,0,0,0 | 154277/134907 | 122729,0,0,0,122729,0,22232 |
| W5_Moscow_s02 | 38 | 2065/287 | 940801 | 960/53 | 1040/49 | 1120/13 | 1029402 | 411420/537558 | 132985/133158 | 600656/514701 | 6 | 22685/219/493/0/0 | 2:3 6:1 / 16:106 | 33516,0,0,0,0,116777,0,0,0,0 | 154289/134947 | 273670,0,0,0,273670,0,21514 |
| W5_Moscow_s03 | 71 | 2065/320 | 937767 | 940/53 | 1020/49 | 0/13 | 1033084 | 411240/536717 | 135037/132540 | 598608/516542 | 7 | 22373/211/850/0/0 | 2:3 6:2 / 16:144 | 33815,0,0,0,0,116657,0,0,0,0 | 154260/134887 | 320104,0,0,0,320104,0,21436 |
| W5_Moscow_s04 | 101 | 2065/350 | 917479 | 940/52 | 1020/49 | -280/14 | 1034594 | 409197/526585 | 137709/129458 | 585475/517297 | 6 | 22573/229/618/0/0 | 2:9 6:4 / 16:155 | 33137,0,0,0,0,116538,0,0,0,0 | 154497/135110 | 317123,0,0,0,317123,0,21686 |
| W5_Moscow_s05 | 138 | 2066/22 | 933012 | 960/52 | 1020/49 | -560/14 | 1049572 | 415492/532621 | 139390/132052 | 595393/524786 | 5 | 22644/274/485/0/0 | 2:11 3:18 6:4 / 16:124 | 34844,0,0,0,0,116418,0,0,0,0 | 154725/135364 | 447311,0,0,0,353700,0,93226 |
| W5_Moscow_s06 | 170 | 2066/54 | 942174 | 960/53 | 1020/50 | -840/14 | 1048834 | 415978/540488 | 138561/134033 | 604531/524417 | 6 | 22605/330/441/0/0 | 2:14 3:37 / 16:115 | 35146,0,0,0,0,116299,0,0,0,0 | 154682/135307 | 473961,0,0,0,352980,0,118852 |
| W5_Moscow_s07 | 206 | 2066/90 | 945547 | 680/53 | 1000/49 | -1120/14 | 1036434 | 410239/543315 | 137724/134400 | 606692/518217 | 6 | 22563/389/458/0/0 | 2:5 3:47 6:1 / 16:125 | 34709,0,0,0,0,116180,0,0,0,0 | 154653/135478 | 640912,0,0,0,352500,0,288437 |
| W5_Moscow_s08 | 238 | 2066/122 | 951443 | 201/54 | 1020/48 | -1680/14 | 1026014 | 407218/550011 | 135224/131961 | 609701/513007 | 7 | 22584/441/350/0/0 | 2:3 3:63 6:2 / 16:132 | 34349,0,0,0,0,115941,0,0,0,0 | 154306/135288 | 20656,0,0,0,20656,0,23370 |
| W5_Moscow_s09 | 273 | 2066/157 | 898036 | 481/51 | 1040/49 | -1960/13 | 1028830 | 410738/523730 | 133250/123811 | 574934/514415 | 7 | 21471/493/1466/0/0 | 2:9 3:49 6:1 / 16:127 | 32168,0,0,0,0,115821,0,0,0,0 | 154068/135073 | 97362,0,0,0,97362,0,20789 |
| W5_Moscow_s10 | 301 | 2066/185 | 895825 | 761/50 | 1020/49 | -1680/13 | 1035568 | 416704/512179 | 130990/130593 | 573495/517784 | 5 | 21686/564/1202/0/0 | 2:6 3:59 5:1 / 16:193 | 32239,0,0,0,0,115702,0,0,0,0 | 154373/135409 | 66271,0,0,0,66271,0,21004 |
| W5_Moscow_s11 | 337 | 2066/221 | 922171 | 1000/52 | 1020/50 | 0/13 | 1035944 | 418621/523187 | 129310/134194 | 590347/517972 | 4 | 22278/606/531/0/0 | 3:78 6:2 / 16:67 | 33562,0,0,0,0,115583,0,0,0,0 | 154338/135439 | 166810,0,0,0,166810,0,21959 |
| W5_Moscow_s12 | 372 | 2066/256 | 939073 | 960/53 | 1000/50 | 280/13 | 1036956 | 418637/536397 | 129859/133035 | 601018/518478 | 6 | 22382/594/434/0/0 | 2:2 3:49 5:1 6:1 / 16:147 | 34516,0,0,0,0,115463,0,0,0,0 | 154291/135506 | 256937,0,0,0,256937,0,19050 |
| W5_Moscow_s13 | 399 | 2066/283 | 925197 | 940/52 | 1000/49 | 560/13 | 1023636 | 413256/531579 | 128108/130997 | 592094/511818 | 6 | 21983/564/827/0/0 | 2:8 / 16:128 | 33135,0,0,0,0,115344,0,0,0,0 | 154271/135579 | 192140,0,0,0,192140,0,22473 |
| W5_Moscow_s14 | 436 | 2066/320 | 937886 | 960/53 | 1020/48 | 840/13 | 1016282 | 408716/536688 | 128788/132974 | 599748/508141 | 6 | 22241/502/614/0/0 | 2:6 / 16:135 | 33499,0,0,0,0,115224,0,0,0,0 | 154252/135635 | 317535,0,0,0,317535,0,21442 |
| W5_Moscow_s15 | 467 | 2066/351 | 950952 | 940/54 | 1040/49 | 1120/13 | 1023078 | 412027/543817 | 129036/134813 | 608100/511539 | 7 | 22491/454/416/0/0 | 2:3 6:2 / 16:116 | 34747,0,0,0,0,115105,0,0,0,0 | 154187/135637 | 339102,0,0,0,339102,0,22204 |

* Moscow deals per save (type, neighbour, units, monthly amount): shipped day 0: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s01 day 1: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s02 day 38: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s03 day 71: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s04 day 101: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s05 day 138: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s06 day 170: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s07 day 206: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s08 day 238: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s09 day 273: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s10 day 301: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s11 day 337: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s12 day 372: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s13 day 399: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s14 day 436: t0 n0 u12600 $504, t2 n3 u5100 $204; W5_Moscow_s15 day 467: t0 n0 u12600 $504, t2 n3 u5100 $204

#### Madrid, Spain

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 2076/319 | 1176537 | 840/58 | 640/66 | 280/16 | 1275916 | 523099/711356 | 141269/161071 | 768362/637958 | 7 | 16322/1294/2579/0/0 | 2:16 3:9 6:85 / 9:128 16:38 | 0,144505,0,0,0,0,0,0,46387,0 | 83837/83837 | 373292,0,0,16970,0,356322,26222 |
| W5_Madrid_s01 | 1 | 2076/320 | 1176537 | 840/58 | 640/66 | 280/16 | 1275916 | 523099/711356 | 141269/161071 | 768362/637958 | 7 | 16284/1286/2682/0/0 | 2:16 3:9 6:85 / 9:128 16:38 | 0,144505,0,0,0,0,0,0,46387,0 | 83837/83837 | 401436,0,0,18252,0,383184,26222 |
| W5_Madrid_s02 | 33 | 2076/352 | 1025011 | 840/50 | 680/54 | 560/13 | 1057196 | 431853/632643 | 115748/139730 | 668086/528598 | 8 | 16955/993/3137/0/0 | 2:10 4:80 5:657 6:61 / 9:128 14:15 15:15 16:72 | 0,136861,0,0,0,0,0,0,43885,0 | 228528/215854 | 406656,0,0,17786,0,388870,24306 |
| W5_Madrid_s03 | 61 | 2077/15 | 1073997 | 0/53 | 920/52 | 840/14 | 1040698 | 411557/635470 | 126966/156900 | 699567/520349 | 10 | 18663/1003/1448/0/0 | 2:15 5:203 6:85 / 9:128 15:5 16:81 | 0,139644,0,0,0,0,0,0,44672,0 | 228131/215231 | 366772,0,0,18444,0,348328,23702 |
| W5_Madrid_s04 | 96 | 2077/50 | 1103849 | 280/54 | 960/57 | 1120/14 | 1109542 | 448357/655912 | 127022/157814 | 720760/554771 | 12 | 19053/1061/978/0/0 | 2:10 6:170 / 9:128 16:30 | 0,144669,0,0,0,0,0,0,46241,0 | 227759/215406 | 489108,0,0,24078,0,465030,25074 |
| W5_Madrid_s05 | 127 | 2077/81 | 1103063 | 560/54 | 860/57 | 1400/15 | 1127296 | 453535/662056 | 131294/154207 | 720244/563648 | 11 | 19212/1050/826/0/0 | 2:16 4:16 6:114 / 9:128 14:1 16:27 | 0,144827,0,0,0,0,0,0,46182,0 | 220017/215931 | 565508,0,0,26190,54368,484950,8948 |
| W5_Madrid_s06 | 158 | 2077/112 | 1109449 | 840/55 | 860/58 | 1648/15 | 1147270 | 459627/666181 | 135858/155152 | 724338/573635 | 10 | 18969/1080/1056/0/0 | 2:15 4:48 5:24 6:124 / 9:128 14:7 16:43 | 0,144900,0,0,0,0,0,0,46102,0 | 207399/207399 | 568884,0,0,26342,57592,484950,24946 |
| W5_Madrid_s07 | 191 | 2077/145 | 1109288 | 900/55 | 840/57 | 1648/15 | 1128658 | 452176/666682 | 133471/155105 | 724187/564329 | 10 | 19027/1137/908/0/0 | 2:11 4:41 5:35 6:112 / 9:128 14:2 16:48 | 0,144873,0,0,0,0,0,0,46023,0 | 207083/207083 | 648296,0,0,29966,134220,484110,26202 |
| W5_Madrid_s08 | 218 | 2077/172 | 1102891 | 900/54 | 860/59 | 1648/15 | 1157968 | 467808/663364 | 133554/154162 | 719851/578984 | 11 | 17788/1303/2580/0/0 | 4:45 5:453 6:93 / 9:128 14:26 15:23 16:73 | 0,144826,0,0,0,0,0,0,45943,0 | 92865/92865 | 521220,0,0,23952,13998,483270,24616 |
| W5_Madrid_s09 | 249 | 2077/203 | 950600 | 920/47 | 820/50 | 1648/13 | 999534 | 396189/584027 | 120930/132278 | 619112/499767 | 9 | 18470/1055/2214/0/0 | 4:25 5:168 6:114 / 9:128 14:9 15:18 16:89 | 0,134620,0,0,0,0,0,0,42604,0 | 226042/209039 | 511172,0,0,23106,5636,482430,18288 |
| W5_Madrid_s10 | 278 | 2077/232 | 994594 | 1060/49 | 1000/51 | 1648/15 | 1039090 | 406618/584631 | 131552/148632 | 647641/519545 | 9 | 19698/1058/981/0/0 | 6:99 / 9:128 16:23 | 0,138753,0,0,0,0,0,0,43875,0 | 225755/211882 | 480088,0,0,25974,0,454114,9156 |
| W5_Madrid_s11 | 313 | 2077/267 | 1034110 | 1020/51 | 980/54 | 1648/15 | 1088874 | 428521/608012 | 136160/150805 | 673351/544437 | 9 | 19897/1062/769/0/0 | 2:5 6:103 / 9:128 16:32 | 0,143415,0,0,0,0,0,0,45290,0 | 225376/211943 | 589036,0,0,29994,77752,481290,24728 |
| W5_Madrid_s12 | 349 | 2077/303 | 1047346 | 980/51 | 920/54 | 1648/16 | 1099224 | 429976/621628 | 140163/149287 | 681976/549612 | 9 | 20214/1133/385/0/0 | 2:15 6:100 / 9:128 16:17 | 0,144613,0,0,0,0,0,0,45630,0 | 224998/211936 | 756324,0,0,37700,225000,480750,25540 |
| W5_Madrid_s13 | 380 | 2077/334 | 1054880 | 980/52 | 920/57 | 1648/16 | 1153426 | 456722/627490 | 142324/150402 | 686887/576713 | 9 | 19730/1202/781/0/0 | 2:19 4:27 5:26 6:96 / 9:128 14:1 16:20 | 0,144916,0,0,0,0,0,0,45679,0 | 203620/203620 | 763140,0,0,37452,225000,479640,23874 |
| W5_Madrid_s14 | 414 | 2078/3 | 1045356 | 980/51 | 860/57 | 1648/16 | 1149764 | 455631/622493 | 141584/149052 | 680729/574882 | 7 | 19724/1272/634/0/0 | 2:5 4:68 5:24 6:109 / 9:128 14:3 16:38 | 0,144748,0,0,0,0,0,0,45574,0 | 203071/203071 | 49444,0,0,2420,0,47024,24088 |
| W5_Madrid_s15 | 442 | 2078/31 | 1045356 | 980/51 | 860/57 | 1648/16 | 1149764 | 455631/622493 | 141584/149052 | 680729/574882 | 7 | 19654/1262/713/0/0 | 2:6 4:33 5:34 6:84 / 9:128 14:3 16:28 | 0,144748,0,0,0,0,0,0,45574,0 | 203071/203071 | 784912,0,0,38862,225000,479100,41502 |
| W5_Madrid_s16 | 472 | 2078/61 | 941203 | 980/46 | 840/49 | 1648/14 | 1019982 | 399038/572596 | 129182/134609 | 615827/509991 | 7 | 18123/1229/2694/0/0 | 4:49 5:323 6:88 / 9:128 14:22 15:16 16:90 | 0,136801,0,0,0,0,0,0,42974,0 | 223473/208024 | 24116,0,0,1176,0,22940,0 |

* shipped day 0: pumps in (191..197, 246..255): 17, output 0: 17, states {1: 17}, status {0: 17}
* W5_Madrid_s01 day 1: pumps in (191..197, 246..255): 17, output 0: 17, states {1: 17}, status {0: 17}
* W5_Madrid_s02 day 33: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s03 day 61: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s04 day 96: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s05 day 127: pumps in (191..197, 246..255): 17, output 0: 6, states {1: 17}, status {0: 17}
* W5_Madrid_s06 day 158: pumps in (191..197, 246..255): 17, output 0: 13, states {1: 17}, status {0: 17}
* W5_Madrid_s07 day 191: pumps in (191..197, 246..255): 17, output 0: 13, states {1: 17}, status {0: 17}
* W5_Madrid_s08 day 218: pumps in (191..197, 246..255): 17, output 0: 17, states {1: 17}, status {0: 17}
* W5_Madrid_s09 day 249: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s10 day 278: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s11 day 313: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s12 day 349: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}
* W5_Madrid_s13 day 380: pumps in (191..197, 246..255): 17, output 0: 14, states {1: 17}, status {0: 17}
* W5_Madrid_s14 day 414: pumps in (191..197, 246..255): 17, output 0: 14, states {1: 17}, status {0: 17}
* W5_Madrid_s15 day 442: pumps in (191..197, 246..255): 17, output 0: 14, states {1: 17}, status {0: 17}
* W5_Madrid_s16 day 472: pumps in (191..197, 246..255): 17, output 0: 0, states {1: 17}, status {0: 17}

#### Berlin, Germany

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 2067/40 | 794278 | 1140/43 | 880/56 | -1680/17 | 851122 | 323516/443836 | 116492/113594 | 514166/425561 | 8 | 9656/310/256/0/0 | 3:58 / 9:80 16:2 | 0,59544,0,0,0,92624,0,5263,0,823 | 184516/132392 | 208374,31728,0,72039,0,0,153029 |
| W5_Berlin_s01 | 1 | 2067/41 | 794278 | 1140/43 | 880/56 | -1680/17 | 851122 | 323516/443836 | 116492/113594 | 514166/425561 | 8 | 9660/310/252/0/0 | 3:58 / 9:80 16:2 | 0,59544,0,0,0,92624,0,5263,0,823 | 184516/132392 | 233600,35694,0,80707,0,0,153029 |
| W5_Berlin_s02 | 40 | 2067/80 | 803467 | 1140/44 | 880/56 | 0/17 | 847354 | 323694/447784 | 114341/115263 | 520115/423677 | 8 | 9703/356/168/0/0 | 3:53 6:1 / 9:80 16:2 | 0,59700,0,0,0,92869,0,5260,0,828 | 184335/132216 | 512944,79320,0,175852,0,0,336762 |
| W5_Berlin_s03 | 73 | 2067/113 | 812827 | 1120/44 | 880/56 | 280/17 | 846746 | 322777/453090 | 114890/116643 | 526504/423373 | 9 | 9737/347/158/0/0 | 2:1 3:47 / 9:80 16:2 | 0,59784,0,0,0,92995,0,5265,0,828 | 184150/132290 | 573980,87252,0,197046,0,0,391940 |
| W5_Berlin_s04 | 107 | 2067/147 | 805335 | 1120/44 | 880/56 | 146/17 | 838996 | 318307/450383 | 115223/115330 | 521975/419498 | 9 | 9666/319/254/0/0 | 2:2 / 9:80 16:2 | 0,59448,0,0,0,92477,0,5252,0,823 | 183972/132090 | 667157,103116,0,225288,0,0,443391 |
| W5_Berlin_s05 | 145 | 2067/185 | 781957 | 1140/42 | 880/57 | -414/17 | 850424 | 323335/439566 | 116197/112169 | 508409/425212 | 9 | 9560/338/320/0/0 | 3:5 / 9:80 16:2 | 0,59268,0,0,0,92190,0,5243,0,823 | 183656/132379 | 78546,11898,0,27023,0,0,50040 |
| W5_Berlin_s06 | 178 | 2067/218 | 791961 | 1160/43 | 860/57 | -686/17 | 853350 | 326054/443945 | 115077/114352 | 515735/426675 | 8 | 9597/361/287/0/0 | 3:17 / 9:80 16:2 | 0,59508,0,0,0,92575,0,5260,0,823 | 183488/132423 | 128105,19830,0,45277,0,0,83672 |
| W5_Berlin_s07 | 215 | 2067/255 | 783674 | 1140/42 | 860/57 | -966/17 | 854234 | 327522/440565 | 114132/112752 | 510469/427117 | 8 | 9458/402/390/0/0 | 3:23 / 9:80 16:2 | 0,59328,0,0,0,92295,0,5248,0,823 | 183294/132403 | 284328,43626,0,98243,0,0,187614 |
| W5_Berlin_s08 | 248 | 2067/288 | 778042 | 1160/42 | 860/58 | 0/16 | 852470 | 329057/437606 | 111731/112120 | 507109/426235 | 8 | 9487/442/319/0/0 | 3:40 / 9:80 16:2 | 0,59112,0,0,0,91952,0,5220,0,818 | 183172/132323 | 357091,55524,0,126145,0,0,232962 |
| W5_Berlin_s09 | 286 | 2067/326 | 793758 | 1160/43 | 840/57 | 280/16 | 851594 | 328576/445613 | 111731/115000 | 518299/425797 | 8 | 9561/430/267/0/0 | 2:2 5:2 / 9:80 16:2 | 0,59412,0,0,0,92414,0,5240,0,823 | 182986/132375 | 537843,83286,0,192576,0,0,345547 |
| W5_Berlin_s10 | 318 | 2067/358 | 800084 | 1140/43 | 860/57 | 560/16 | 847500 | 326355/449882 | 111769/115701 | 522603/423750 | 8 | 9502/344/442/0/0 | 5:1 / 9:80 16:1 | 0,59496,0,0,0,92554,0,5247,0,823 | 182763/132355 | 590171,91218,0,203321,0,0,386178 |
| W5_Berlin_s11 | 356 | 2068/31 | 792686 | 1140/43 | 860/57 | 840/15 | 834962 | 325654/447316 | 105887/114569 | 518443/417481 | 9 | 9522/265/520/0/0 | 2:2 5:3 / 9:80 16:2 | 0,58920,0,0,0,91658,0,5154,0,818 | 182596/132260 | 774085,118980,0,265910,0,0,510884 |
| W5_Berlin_s12 | 389 | 2068/64 | 807756 | 1140/44 | 860/57 | 1400/16 | 844364 | 327064/447891 | 109369/115639 | 520959/422182 | 8 | 9696/209/446/0/0 | 6:1 / 9:80 16:2 | 0,59724,0,0,0,92911,0,5257,0,828 | 182219/132328 | 77305,11898,0,26723,0,0,67564 |
| W5_Berlin_s13 | 422 | 2068/97 | 811874 | 1120/44 | 860/58 | 1680/15 | 840206 | 329593/451674 | 104728/116117 | 524598/420103 | 9 | 9831/79/458/0/0 | – / 9:80 16:2 | 0,59676,0,0,0,92834,0,5258,0,828 | 181988/132292 | 127278,19830,0,43421,0,0,83329 |
| W5_Berlin_s14 | 459 | 2068/134 | 808947 | 1120/44 | 840/58 | 1700/16 | 847424 | 328947/451462 | 109131/115820 | 523659/423712 | 9 | 9971/73/325/0/0 | 2:2 6:2 / 9:80 16:2 | 0,59928,0,0,0,93219,0,5310,0,828 | 181789/132336 | 311954,47592,0,105652,0,0,207740 |
| W5_Berlin_s15 | 491 | 2068/166 | 806102 | 1120/44 | 840/58 | 1680/16 | 859512 | 331126/450209 | 113325/115450 | 522025/429756 | 9 | 9932/67/363/0/0 | 2:3 5:1 6:2 / 9:80 16:2 | 0,60180,0,0,0,93625,0,5312,0,833 | 181590/132386 | 334521,51558,0,114422,0,0,219222 |

*Run-to-run spread from one save* `[OBSERVED]`. `W5_Berlin_s06` (8/6/2067) was loaded a second
time in the original and run without changes to 12/20/2067, with a save about every 15 days
(files `R2_Berlin_from_s06_c01` … `c09`). The two original runs differ by as much as the engine
differs from either of them. Developer state changes over one month can differ by a factor of 5
between two original runs, so month-level counts from one trajectory cannot be used to check
growth rules.

| Measure, from s06 | W5 run | Second run |
|---|---|---|
| R lots replaced, state 1 → 4, to 9/12 (W5) / 9/18 (second run) | 131 | 13 |
| R lots removed from state 1, same period | 70 | 14 |
| I lots abandoned (1 → 2), same period | 41 | 12 |
| I valve V at 9/12 (W5) / 9/3 and 9/18 (second run) | −966 | 0, 0 |
| R lots in state 4 at 10/15 (W5) / 10/19 (second run) | 222 | 135 |
| I lots in state 2 at 10/15 (W5) / 10/19 (second run) | 439 | 358 |

In the second run, the committed R demand P (spec 15 §4) went from 872 at load to 1240 by
8/10/2067. That is above V = 1160, so R demand is used up early in each pass, as in the engine.

#### London, England

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 1991/140 | 304900 | 280/24 | 840/46 | -1680/34 | 389770 | 100102/112965 | 95716/88745 | 207101/194885 | 0 | 13473/1128/43/0/0 | 3:69 6:48 / 9:112 | 0,0,114858,0,0,0,0,0,0,0 | 110655/97000 | 184604,0,0,76744,107860,0,0 |
| W5_London_s01 | 1 | 1991/141 | 304900 | 280/24 | 840/46 | -1680/34 | 389770 | 100102/112965 | 95716/88745 | 207101/194885 | 0 | 13473/1129/42/0/0 | 3:69 6:48 / 9:112 | 0,0,114858,0,0,0,0,0,0,0 | 110655/97000 | 193856,0,0,80572,113284,0,0 |
| W5_London_s02 | 36 | 1991/176 | 306390 | 560/24 | 1080/47 | -1960/33 | 389512 | 102994/113393 | 93268/89169 | 207886/194756 | 2 | 13360/1235/37/0/0 | 3:74 6:29 / 9:112 16:1 | 0,0,114456,0,0,0,0,0,0,0 | 110655/97065 | 231336,0,0,96070,135266,0,0 |
| W5_London_s03 | 68 | 1991/208 | 308336 | 840/24 | 1060/48 | -1680/33 | 386226 | 103634/114063 | 90868/89702 | 209115/193113 | 2 | 13245/1348/30/0/0 | 3:76 5:1 6:39 / 9:112 16:1 | 0,0,114086,0,0,0,0,0,0,0 | 110655/97099 | 252232,0,0,104862,147370,0,0 |
| W5_London_s04 | 103 | 1991/243 | 309802 | 1120/24 | 1040/49 | 0/32 | 386516 | 105705/114564 | 88974/90088 | 210034/193258 | 3 | 13064/1446/57/0/0 | 3:67 6:37 / 9:112 | 0,0,113905,0,0,0,0,0,0,0 | 110655/97158 | 291668,0,0,121222,170446,0,0 |
| W5_London_s05 | 139 | 1991/279 | 311614 | 1520/24 | 1000/51 | 560/31 | 389738 | 109167/115194 | 87378/90597 | 211189/194869 | 2 | 12991/1380/165/0/0 | 6:34 / 9:112 16:18 | 0,0,113976,0,0,0,0,0,0,0 | 110911/97614 | 48760,0,0,20252,28508,0,0 |
| W5_London_s06 | 171 | 1991/311 | 312961 | 1520/24 | 980/51 | 840/32 | 391936 | 109079/115643 | 88724/90935 | 212012/195968 | 3 | 13002/1353/142/0/0 | 6:41 7:1 / 9:112 16:23 | 0,0,114252,0,0,0,0,0,0,0 | 111167/97914 | 56408,0,0,23438,32970,0,0 |
| W5_London_s07 | 209 | 1991/349 | 314916 | 1520/25 | 980/53 | 0/32 | 398060 | 112697/120576 | 88518/87292 | 213327/199030 | 3 | 13006/1335/131/0/0 | 6:33 / 9:112 16:27 | 0,0,114684,0,0,0,0,0,0,0 | 111167/97960 | 138712,0,0,57712,81000,0,0 |
| W5_London_s08 | 241 | 1992/16 | 316433 | 1500/25 | 940/53 | -280/33 | 402894 | 113342/121148 | 90505/87955 | 214339/201447 | 3 | 13021/1372/70/0/0 | 6:33 7:2 / 9:112 16:26 | 0,0,115356,0,0,0,0,0,0,0 | 111423/98236 | 143292,0,0,59462,83830,0,0 |
| W5_London_s09 | 279 | 1992/54 | 319633 | 1500/25 | 940/53 | -560/33 | 403932 | 113565/122365 | 90861/88850 | 216491/201966 | 2 | 12944/1431/58/0/0 | 3:27 6:26 / 9:112 16:1 | 0,0,115640,0,0,0,0,0,0,0 | 111423/98274 | 221812,0,0,92518,129294,0,0 |
| W5_London_s10 | 311 | 1992/86 | 320788 | 1500/25 | 940/53 | -840/32 | 403434 | 114011/122800 | 90142/89131 | 217261/201717 | 3 | 12844/1519/50/0/0 | 3:40 6:35 / 9:112 | 0,0,115534,0,0,0,0,0,0,0 | 111680/98695 | 252464,0,0,105254,147210,0,0 |
| W5_London_s11 | 349 | 1992/124 | 324852 | 1500/25 | 920/54 | 280/32 | 403444 | 115078/124279 | 89091/90227 | 219878/201722 | 3 | 12773/1537/76/0/0 | 3:29 6:30 / 9:112 | 0,0,115624,0,0,0,0,0,0,0 | 111936/98992 | 22108,0,0,9270,12838,0,0 |
| W5_London_s12 | 385 | 1992/160 | 328098 | 1500/26 | 920/55 | 560/32 | 407214 | 116982/125497 | 89253/91124 | 222033/203607 | 3 | 12735/1530/110/0/0 | 6:36 / 9:112 16:29 | 0,0,115945,0,0,0,0,0,0,0 | 111938/99018 | 71412,0,0,29820,41592,0,0 |
| W5_London_s13 | 415 | 1992/190 | 329871 | 1480/26 | 900/56 | 840/32 | 412588 | 119210/126122 | 89924/91551 | 223139/206294 | 3 | 12756/1516/85/0/0 | 6:27 / 9:112 16:19 | 0,0,116428,0,0,0,0,0,0,0 | 112194/99391 | 72280,0,0,30178,42102,0,0 |
| W5_London_s14 | 453 | 1992/228 | 332261 | 1480/26 | 880/58 | 1120/32 | 416986 | 122004/127025 | 89590/92221 | 224738/208493 | 3 | 12760/1478/104/0/0 | 6:30 7:1 / 9:112 16:23 | 0,0,116730,0,0,0,0,0,0,0 | 112197/99408 | 144168,0,0,60182,83986,0,0 |
| W5_London_s15 | 485 | 1992/260 | 332566 | 1480/26 | 840/58 | 1360/33 | 420010 | 122921/126964 | 90372/92133 | 224629/210005 | 3 | 12746/1442/122/0/0 | 6:30 / 9:112 16:37 | 0,0,116988,0,0,0,0,0,0,0 | 112453/99770 | 156316,0,0,65254,91062,0,0 |

#### Craterville

| File | Day | Date | Pop | R V/cap | C V/cap | I V/cap | R D | C S/D | I S/D | Jobs S/D | Unemp | Lots 1/2/4/8/16 | Codes prev pass (1-8 / 9-18) | Power row | Water S/U | Garbage month [0..6] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shipped | 0 | 1933/279 | 208780 | 1420/29 | 0/100 | 0/100 | 253042 | 39286/52014 | 88765/95844 | 141503/126521 | 5 | 4065/144/215/0/0 | 6:4 / – | 0,0,0,0,10586,12098,0,24494,0,2014 | 91860/48296 | 30780,0,0,6160,24620,0,5844 |
| W5_Craterville_s01 | 4 | 1933/283 | 208780 | 1420/29 | 0/100 | 0/100 | 253042 | 39286/52014 | 88765/95844 | 141503/126521 | 5 | 4048/145/232/0/0 | 4:2 6:6 / – | 0,0,0,0,10599,12113,0,24524,0,2008 | 91860/48296 | 57940,0,0,13090,44850,0,5844 |
| W5_Craterville_s02 | 36 | 1933/315 | 210690 | 1420/29 | 0/100 | 0/100 | 253042 | 39286/52345 | 88765/96556 | 142537/126521 | 5 | 4076/148/202/0/0 | 6:12 / – | 0,0,0,0,10610,12125,0,24548,0,1998 | 91881/48320 | 67092,0,0,20552,46540,0,4800 |
| W5_Craterville_s03 | 74 | 1933/353 | 212179 | 1420/30 | 0/100 | 0/100 | 253042 | 39286/51274 | 88765/98425 | 143294/126521 | 5 | 4112/145/154/0/0 | 6:12 / – | 0,0,0,0,10619,12136,0,24566,0,2006 | 91865/48358 | 118640,0,0,35796,82844,0,3646 |
| W5_Craterville_s04 | 112 | 1934/26 | 212688 | 1400/30 | 0/100 | 0/100 | 253042 | 39286/51214 | 88765/98627 | 143626/126521 | 5 | 4142/153/125/0/0 | 6:13 / – | 0,0,0,0,10627,12146,0,24584,0,1998 | 91846/48439 | 166756,0,0,50202,116554,0,5024 |
| W5_Craterville_s05 | 150 | 1934/64 | 220386 | -30/31 | 0/100 | 0/100 | 253042 | 39286/51873 | 88765/104216 | 149779/126521 | 6 | 4230/161/29/0/0 | 6:13 / – | 0,0,0,0,10725,12257,0,24794,0,2043 | 91843/48419 | 31108,0,0,9286,21822,0,0 |
| W5_Craterville_s06 | 188 | 1934/102 | 224522 | -30/31 | 0/100 | 0/100 | 253042 | 39286/50089 | 88765/108440 | 152308/126521 | 7 | 4243/173/4/0/0 | 3:1 6:7 / 13:8 | 0,0,0,0,10774,12314,0,24902,0,2044 | 91827/48419 | 71344,0,0,20740,50604,0,5760 |
| W5_Craterville_s07 | 226 | 1934/140 | 224444 | 250/31 | 0/100 | 0/100 | 253042 | 39286/48661 | 88765/109472 | 152246/126521 | 8 | 4139/165/109/0/0 | 3:1 6:12 / 13:7 16:10 | 0,0,0,0,10776,12315,0,24902,0,2042 | 91827/48419 | 122140,0,0,26108,96032,0,4818 |
| W5_Craterville_s08 | 265 | 1934/179 | 221443 | 530/31 | 0/100 | 0/100 | 253042 | 39286/47872 | 88765/107906 | 150121/126521 | 8 | 3950/154/318/0/0 | 6:10 / – | 0,0,0,0,10745,12280,0,24836,0,2020 | 91850/48387 | 177440,0,0,48778,128662,0,4122 |
| W5_Craterville_s09 | 296 | 1934/210 | 203175 | 810/28 | 0/100 | 0/100 | 253042 | 39286/45440 | 88765/97854 | 137733/126521 | 7 | 4011/160/243/0/0 | 6:10 / – | 0,0,0,0,10519,12022,0,24350,0,1999 | 91857/48226 | 180444,0,0,56064,124380,0,5404 |
| W5_Craterville_s10 | 334 | 1934/248 | 207296 | 1370/29 | 0/100 | 0/100 | 253042 | 39286/53237 | 88765/93821 | 140498/126521 | 3 | 4005/160/245/0/0 | 6:10 / 16:1 | 0,0,0,0,10577,12088,0,24476,0,2005 | 91909/48356 | 26068,0,0,8166,17902,0,4582 |
| W5_Craterville_s11 | 372 | 1934/286 | 209399 | 1420/29 | 0/100 | 0/100 | 253042 | 39286/52258 | 88765/95991 | 141668/126521 | 4 | 4042/169/186/0/0 | 6:19 / – | 0,0,0,0,10607,12123,0,24542,0,2001 | 91894/48369 | 85040,0,0,26474,58566,0,434 |
| W5_Craterville_s12 | 410 | 1934/324 | 211665 | 1420/30 | 0/100 | 0/100 | 253008 | 39268/52513 | 88765/96840 | 142954/126504 | 5 | 4082/160/162/0/0 | 6:12 / – | 0,0,0,0,10632,12151,0,24596,0,2009 | 91880/48348 | 124492,0,0,37614,86878,0,4536 |
| W5_Craterville_s13 | 448 | 1934/362 | 211885 | 1400/30 | 0/100 | 0/100 | 252974 | 39250/51238 | 88765/98280 | 143093/126487 | 5 | 4097/164/131/0/0 | 6:12 / – | 0,0,0,0,10632,12151,0,24596,0,2010 | 91875/48362 | 178652,0,0,53926,124726,0,3648 |
| W5_Craterville_s14 | 486 | 1935/35 | 211516 | 1400/30 | 0/100 | 0/100 | 252974 | 39250/49910 | 88765/99805 | 143492/126487 | 6 | 4062/172/157/0/0 | 6:19 / – | 0,0,0,0,10622,12140,0,24572,0,2005 | 91864/48410 | 22460,0,0,6716,15744,0,4696 |

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
