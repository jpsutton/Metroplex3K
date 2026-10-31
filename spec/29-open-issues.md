# 29 — Open issues and recommended defaults

This document collects the most important questions the specification still leaves open. Each
owning document marks these items `[OPEN]` and lists them under its own "Open questions" heading;
this list ranks them by impact and gives a recommended interim behaviour for each.

Severity:

* **S1** — affects save compatibility.
* **S2** — affects simulation fidelity.
* **S3** — affects presentation only.
* **S4** — peripheral.

## 1. Save compatibility (S1)

Every record in the 59 shipped city-family files is parsed byte-exact. Every field has a known
type and size (`10-save-format.md` §5.2, §9). The open issues are about **values** and
**meanings**, not about layout.

| # | Issue | Owner | Recommended interim behaviour |
|---|---|---|---|
| 1.1 | Initial values for a brand-new city of several record fields: the police and fire records, the flammability trailer, the land-value sums, world-layer scalars, the power month index, the disaster manager | 10 §6.5, §6.12–§6.16 | **Closed** `[OBSERVED]`: 14 cities (4 map sizes × 3 difficulties, plus start years 1900 and 1950) were saved right after **New City**. All fixed-size records are identical in every file, so one template serves all. The template, layer contents, budget by difficulty and per-city values are in `30-new-city-template.md`. |
| 1.2 | Default per-class property values for a newly placed civic building | 10 §6.2.5, 16, 17 | **Mostly closed** `[OBSERVED]`: values for coal plant, wind mill, pump, water tower, police, fire, hospital, school, college, library, bus stop and plopped landmark. Also observed since: oil, gas, nuclear and solar plants (max life differs per plant, random spread), water treatment, desalination, a small park and a rail/subway station (12361). Still open: the rail-only (12230) and subway-only (12360) stations, other park types, and the meaning of several values. The rows are in `10-save-format.md` §6.2.5. The treatment plant's four properties are 0 when new and grow in use. |
| 1.3 | Record layouts written by the original (pre-Unlimited) release | 10 §7.3 | Treat records with unknown versions as absent, and initialise the subsystem to its new-city state. |
| 1.4 | Fields with known types but unknown meaning (piece-word bits, two occupant booleans, the six advisor-manager booleans, the outer value `430E2EE6`, minor scalars) | 10 | Preserve them on re-save. Write the most common shipped value for new objects. **Observed since:** opening every advisor window in a new city changes no saved record; the utilities-advisor flag record (six booleans) is all 1 in every new city (`30-new-city-template.md` §4). Toggling the six booleans needs advisor events not reproduced yet. |
| 1.5 | Index entry order when a container grows past 1023 entries | 02 §2 | Append in key order. This affects byte comparison only, not loading. |

## 1a. Closed by black-box observation

These items are now settled `[OBSERVED]`:
* new-city speed is 1 (`14-sim-clock.md`);
* the terrain editor's feature picker and sliders exist (`11-world-terrain.md` §3.1, `09` §3.4);
* the record `430E2EE6` is not written by build 2.0.949 (`10` §3.6);
* the timing of disaster seed entries (`10` §6.5);
* the format-revision word is 4 (`10` §3.2);
* money is settled once a year (`19` §7);
* Hard difficulty stores one 10,000 bond (`19` §3);
* the New City flags are Disasters / Auto Budget (`10` §3.2);
* the scheme IDs for each dialog choice are known (`10` §3.7);
* map edges are 64/128/192/256 (`11`);
* pickers and transit tools are gated by year (`09` §7a.2);
* the query panel layouts are known (`09` §7a.4).

## 2. Simulation fidelity (S2)

| # | Issue | Owner | Recommended interim behaviour |
|---|---|---|---|
| 2.1 | Workforce fraction `w` (share of residents who work) | 20 §6, 15 §3.4 | **Closed** `[STATIC]`: `w` = Σ cohort shares of ages 20–65; no unemployment, wealth or EQ weighting. Verified `[DATA]` against the saved jobs-valve supply in 13/13 played cities (within 1.2 %). Unemployment is a separate world-layer value: ⌊(100K − 100J)/(2K)⌋ with K = round(R·w), J = jobs (20 §6). |
| 2.2 | The quantity `W` behind the three "count/world" ordinance costs | 19 §9.4 | Configurable. Default: population / 1000. |
| 2.3 | Who sets each valve's cap percentage, and how the 3001/5001/8001 valves feed back into growth | 15 §3.1–§3.3 | **Closed** `[STATIC]`: monthly cap of 3000/5000/8000 = 0 if D = 0, 100 if S < D, else ⌊100·D/S⌋ of the amenity valve (3001/5001/8001); reproduces all saved caps `[DATA 13/13]`. Smoothing (step 280) applies to every valve. **Narrowed, still open:** the saved demand of 5001/8001 is 1.3–2.1 × the building sum and the extra source is unknown; interim: carry the saved ratio (15 §3.3). |
| 2.4 | City-centre location and the hill reference height in land value | 15 §6 | **Closed** `[STATIC]`: centre = area-weighted centroid of all lot buildings; `Href` = mean (altitude − sea level) over dry vertices; LV curves read at valve 3000 `Slast` (no factor 2); hill height is an 8-bit difference. **Also closed:** the water bonus counts a tile as water when any corner has water surface ≥ ground, so sea-level flats get the full bonus (15 §6.3 step 1); this was the main deficit in dense centres `[STATIC][DATA]`. High saved values on all-zone-0 blocks are stale bytes; the original recomputes them within one pass `[OBSERVED]` (28 §5.1). |
| 2.5 | Layouts of the non-scatter build rules | 15 §5.5 | **Mostly closed** `[STATIC]`: housing projects A/B/C, row houses, estates (member lists by flag bit, mirroring of empty lists) and the runway (sets, slots by flag bit and line number, row order) are specified with test cases from the shipped families. **Still open:** seaport pier piece order, farm field tiling, airport-buildings arrangement step 2 (interims in 15 §5.5). |
| 2.6 | Riot unrest statistic U and signed statistic A | 21 §8.2 | U = unemployment %, A = city aura. |
| 2.7 | Water-treatment formula, `DefaultLeak`, expired-plant variant | 16 | Treatment subtracts its capacity from water pollution (17 §7.2). **Recycling closed** `[STATIC]`: ratio from the centres' INI values, aged after `DeclineAge`, zeroed when unpowered, not occupied, retired or after a failed trip; no funding scaling (16 §4.3). Facility zeroing conditions for water facilities listed in 16 §3.3. |
| 2.8 | Source of each building's power consumption | 12 §9.1, 13, 16 | One unit per lot tile, times the building's density class. **Calibration target** `[OBSERVED]`: in 480 days of Moscow the original abandons no zoned lot for lack of power (no code 4 in any pass, 28 §5.1). |
| 2.9 | Identity of several advisor metrics, and the ordinance → acknowledgement topic table | 22 §3.8–§3.13 | Use the documented thresholds against the nearest documented statistic. |
| 2.10 | Department → budget line mapping | 19 §6.1, 09 §6.1 | **Closed** `[OBSERVED]`: sliders set to distinct values and read back from a save. Education `0x02922A5D`, Public Health `0x42922A6C`, Fire `0x62963928`, Roads `0x8290F605`, Police `0xA2963983`, Mass Transit `0xC290F609`. The earlier inference (police/fire/health/education) was wrong; see 19 §6.1. Slider maximum above 100 is still open. |
| 2.11 | Aura layer run trigger | 14 §6, 17 §8 | Run daily, like the police and fire layers. |

## 3. Presentation (S3)

| # | Issue | Owner | Recommended interim behaviour |
|---|---|---|---|
| 3.1 | How a data-map value is mapped to a palette row | 25 §9.2, 09 §7.2 | **Mostly closed** `[OBSERVED]`: the layer → ramp assignment is known; the band edges remain. Use row = 31 − ⌊value/8⌋. The Land Value data map of a shipped city was displayed: grey (low) to deep blue (high) in 2 × 2-tile blocks, as in `25-rendering.md` §9.2. Band edges were not measured: they need a pixel-to-tile projection that is not calibrated. |
| 3.2 | Selection highlight, placement preview and cursor cost text | 25 §9.4 | **Closed** `[OBSERVED]`: cyan footprint, network path and zone-coloured rectangle; red tint over an occupied target; cost label below and to the right of the cursor. |
| 3.3 | Draw-order tie-break between moving and static objects | 25 §5.2 | Static before moving, when keys are equal. |
| 3.4 | Slider, framed-window and tab controls; bottom-bar anchors P1–P4 | 08 | Fly-out geometry, tool-column placement and the bottom-bar arrangement at 640, 800 and 1024 are now `[OBSERVED]` (08 §4.2, §7; 09 §7a). Lay out the remaining controls from the shipped images. |
| 3.5 | Edge-scroll border sizes, the animation and view-update tick rate | 08 §8, 25 §10 | 8-px border; 60 view updates per second. **Observed:** in the windowed run, parking the pointer 0–12 px from the window edge did not scroll the view, so the edge margin could not be measured; the ticker hint says the view scrolls while the right mouse button is held. **Project decision:** edge scrolling is disabled for now and will be revisited later. |
| 3.6 | Ambient-sound area weighting by city state and zoom | 06 §5.3 | Choose the schedule of the dominant zone type under the view centre. |
| 3.7 | Intro video codec | 07 | Decode with an existing TQI/TGQ decoder (the format is publicly documented). |
| 3.8 | Parade automaton opcode, boat and ship spawn conditions | 26 | Skip parades. Spawn ships only at seaports. |

## 4. Peripheral (S4)

* `SC3.cfg` records 2–15: which subsystem owns each one, plus a few flags in record 1 (27 §4). An
  implementation may use its own preferences file; only record 1 matters for options.
* Country index table 0–56 (27 §2). Only the countries listed in 27 affect content.
* Whether cheat phrases match case-sensitively (27 §5). Recommendation: case-insensitive.
* `[DBSegmentDirectoryFiles]` and the plug-in directory root (02 §7). Not used by the shipped data.

## 5. How to close these issues

Black-box observation of the original running under a compatibility layer is the cheapest way to
settle most S2 and S3 items. For example:

* place a single civic building on a new map and save immediately (closes 1.2);
* compare land value on a flat map with and without hills (closes 2.4);
* record screenshots at each zoom (closes 3.1–3.5).

These observations do not involve the original's code, so the clean team can make them directly.
