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
| 1.1 | Initial values for a brand-new city of several record fields: the police and fire records, the flammability trailer, the land-value sums, world-layer scalars, the power month index, the disaster manager | 10 §6.5, §6.12–§6.16 | Best source: a city saved by the original immediately after **New City**, before the simulation runs. Such a save has been checked `[OBSERVED]`, and every record in it parses with the layouts of `10-save-format.md`. Capture one per map size and difficulty and use its records as templates. Second-best source: a shipped terrain file (`.sct`) of the same map size [INFERRED]. |
| 1.2 | Default per-class property values for a newly placed civic building (power plants, water facilities, services, transit) | 10 §6.2.5, 16, 17 | Use the values observed on freshly built facilities in the shipped cities. Then initialise age and lifetime fields as in `16-sim-utilities.md` §2.3. |
| 1.3 | Record layouts written by the original (pre-Unlimited) release | 10 §7.3 | Treat records with unknown versions as absent, and initialise the subsystem to its new-city state. |
| 1.4 | Fields with known types but unknown meaning (piece-word bits, two occupant booleans, the six advisor-manager booleans, the outer value `430E2EE6`, minor scalars) | 10 | Preserve them on re-save. Write the most common shipped value for new objects. |
| 1.5 | Index entry order when a container grows past 1023 entries | 02 §2 | Append in key order. This affects byte comparison only, not loading. |

## 2. Simulation fidelity (S2)

| # | Issue | Owner | Recommended interim behaviour |
|---|---|---|---|
| 2.1 | Workforce fraction `w` (share of residents who work) | 20 §6 | Use the recommended formula in 20 and make it configurable. |
| 2.2 | The quantity `W` behind the three "count/world" ordinance costs | 19 §9.4 | Configurable. Default: population / 1000. |
| 2.3 | Who sets each valve's cap percentage, and how the 3001/5001/8001 valves feed back into growth | 15 §3 | Cap = 100 %. Amenity valves affect only the advisor display. |
| 2.4 | City-centre location and the hill reference height in land value | 15 §6 | Centre = centroid of developed tiles. Hill reference = mean terrain altitude. |
| 2.5 | Layouts of the non-scatter build rules (estates, row houses, housing projects B/C, seaport pier, airport runway sequence) | 15 §5.5 | Use the scatter rule with the family's lot size until these are specified. |
| 2.6 | Riot unrest statistic U and signed statistic A | 21 §8.2 | U = unemployment %, A = city aura. |
| 2.7 | Recycling-centre and water-treatment formulas, `DefaultLeak`, expired-plant variant | 16 | Recycling removes `Capacity` units of garbage per month; treatment subtracts its capacity from water pollution (17 §7.2). |
| 2.8 | Source of each building's power consumption | 12 §9.1, 13, 16 | One unit per lot tile, times the building's density class. |
| 2.9 | Identity of several advisor metrics, and the ordinance → acknowledgement topic table | 22 §3.8–§3.13 | Use the documented thresholds against the nearest documented statistic. |
| 2.10 | Department → budget line mapping for police, fire, health and education | 19 §6.1 | As tabled in 19 `[INFERRED]`. |
| 2.11 | Aura layer run trigger | 14 §6, 17 §8 | Run daily, like the police and fire layers. |

## 3. Presentation (S3)

| # | Issue | Owner | Recommended interim behaviour |
|---|---|---|---|
| 3.1 | How a data-map value is mapped to a palette row | 25 §9, 09 §7.2 | Linear mapping of the layer's value range onto palette rows 0–255. |
| 3.2 | Selection highlight, placement preview and cursor cost text | 25, 09 | Translucent tile tint (green = valid, red = invalid). Cost text next to the cursor. |
| 3.3 | Draw-order tie-break between moving and static objects | 25 §5.2 | Static before moving, when keys are equal. |
| 3.4 | Slider, framed-window and tab controls; third-level fly-out layout; bottom-bar anchors P1–P4 | 08 | The tool-menu column (right edge) and the bottom-bar arrangement are now `[OBSERVED]` (08 §4.2, §7). Lay out the rest from the shipped images, matching black-box screenshots. |
| 3.5 | Edge-scroll border sizes, the animation and view-update tick rate | 08 §8, 25 §10 | 8-px border; 60 view updates per second. |
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
