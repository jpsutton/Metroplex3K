# 15 — Growth simulation: zones, demand, development, land value

Scope: zone types, the RCI demand ("valve") model, the zone developers (residential, commercial,
industrial, agriculture, landfill, airport, seaport), redevelopment and abandonment, and the
land-value layer. Building data formats are in `13-buildings-occupants.md`. Clock and update
scheduling are in `14-sim-clock.md`.

All tunables named here are read from the INI files listed below, which the engine loads from
`Apps/Sys/SYS.PAK` (a loose `Apps/Sys/<name>.INI` file is tried first where noted; see
`02-containers.md`). The spec gives semantics; values come from the user's data.

| INI | Sections used here |
|---|---|
| `SC3Tune.ini` | `[ZoneDeveloperRules]`, `[ResidentialZoneDeveloper]`, `[CommercialZoneDeveloper]`, `[IndustrialZoneDeveloper]`, `[LandfillZoneDeveloper]` |
| `SC3ZoneLayer.ini` | `[ZoneDeveloperDescriptions]`, `[ZoneDeveloperRules]` |
| `SC3LandValue.ini` | `[TuningParameters]`, `[AgentIDModifiers]` |
| `SC3ValveLayer.ini` | `[AgentValveEffects]`, `[ConnectionValveEffects]`, `[OrdinanceEffects]` |
| `SC3WorldLayer.ini` | `[WorldLayer]`, `[ListOfWorldValveMapping]` |
| `SC3ResLayer.ini`, `SC3ComLayer.ini`, `SC3IndLayer.ini` | `[TuningParameters]` |
| `SC3BudgetLayer.ini` | `TaxationDemandEffect` |

## 1. Zone types

The zone layer stores one byte per tile. [STATIC] Values:

| Value | Zone | Developer description key in `SC3ZoneLayer.ini` |
|---|---|---|
| 0 | None | — |
| 1, 2, 3 | Residential low / medium / high density | 1, 2, 3 |
| 5, 6, 7 | Commercial low / medium / high density | 5, 6, 7 |
| 9, 10, 11 | Industrial low / medium / high density | 9, 10, 11 |
| 14 | Airport | 14 |
| 15 | Seaport | 15 |
| 17 | Landfill | 17 |
| 22 | Land under a non-zoned (plopped) building, commercial-class for land value. When an occupant is removed, every zone-22 tile of its footprint is reset to 0 [STATIC] | — |

[INFERRED] 4, 8, 12, 13 and others are unused gaps; 22 is treated like commercial by the land
value layer. For land-value purposes, zones group into classes: residential {1,2,3}; commercial
{5,6,7,14,22}; industrial {9,10,11,15}. [STATIC]

## 2. Zone developer descriptions (`SC3ZoneLayer.ini [ZoneDeveloperDescriptions]`)

Syntax: `zone=<developerClassID>,<baseAgentType>:<category>;<category>;…`

* `developerClassID`: which developer handles the zone: `0x619BA64E` residential,
  `0x41A3ADC1` commercial, `0xE1A53B30` industrial, `0xC1F81E7E` airport (zone 14),
  `0x82D2D72B` seaport (zone 15), `0x82348DE5` landfill (zone 17). [DATA]/[INFERRED]
* `baseAgentType`: 3000 / 5000 / 8000 for R / C / I.
* Each category is `agentType,[cleanFlag,]densityLevel,maxLandValue,minLandValue`:
  * residential/commercial: `agentType,density,maxLV,minLV`
  * industrial: `agentType,class,density,maxLV,minLV` with class `D` (dirty), `C` (clean),
    `A` (agriculture).
  * A building category is eligible on a tile when the tile's land value (stored range 1–255, §6) lies in
    [minLV, maxLV] and its density level ≤ the zone's density. [INFERRED]

## 3. RCI demand (valves)

### 3.1 Valves

Demand is modelled by **valves**: named supply/demand accumulators identified by an agent-type-like
ID. [STATIC]

| Valve ID | Role (supplied by → demanded by) |
|---|---|
| 3000 | Residents. Supplied by residential buildings; demanded by jobs (through the jobs valve). Its value is the **residential demand**. |
| 3001 | Residential amenity ("fun"). Supplied by parks, culture, landmarks (`SC3ValveLayer.ini`) and a constant external supply (`SC3ResLayer.ini ExternalFunSupply`); demanded by residential buildings. |
| 5000 | Commercial. Supplied by commercial buildings; demanded by the workforce (through the jobs valve). Its value is the **commercial demand**. |
| 5001 | Commercial connections/customers. Supplied by neighbour connections, airports, seaports, and a constant (`SC3ComLayer.ini InitialConnectionSupply`); demanded by commercial buildings. |
| 7000 | Jobs (special valve, §3.4). Demanded by every job-providing building; supplied by the workforce and by `SC3WorldLayer.ini ExternalJobDemand`. |
| 8000 | Industrial. Supplied by industrial buildings; demanded by the workforce. Its value is the **industrial demand**. |
| 8001 | Industrial connections/freight. Supplied by neighbour connections, airports, seaports and a constant (`SC3IndLayer.ini InitialConnectionSupply`); demanded by industrial buildings. |

[INFERRED, high confidence] The 3001/5001/8001 roles follow from which agents supply them and the
INI key names.

**Feedback of the amenity valves into growth** [STATIC]. The amenity valves act on R/C/I growth
only through `capPct` (§3.2): once a month each of the residential, commercial and industrial
layers sets `capPct` of its family valve (3000, 5000, 8000) from the last completed cycle of its
amenity valve (3001, 5001, 8001), by the rule in §3.2 "Cap rule". In addition:

* valve 8001 is the demand valve of the **seaport** developer: its rules receive 8001's `V` and
  `Max` [STATIC]. The **airport** developer also holds valve 5001, but it calls its rules with a
  fixed demand of 2000 of 2000, so airport growth is not gated by demand [STATIC] (§7);
* a third, unnamed valve with ID `0xA2C9E821` is the **landfill store** (§7 and
  `16-sim-utilities.md` §4.4). It is saved inside the pollution record, not with the valve
  records [STATIC].

Other valves (in data): there are no further valves; `[AgentValveEffects]` items for any other
valve ID are ignored [INFERRED].

### 3.2 Valve state and monthly update

**Schedule** [STATIC]. The valve layer does its work once per month, on day 1, as the **first**
item of the month-day list of `14-sim-clock.md` §6.3 rule 7. That work adds the building,
connection and ordinance contributions of §3.3 and then performs the update below for every valve
(3000, 3001, 5000, 5001, 8000, 8001) [STATIC]. It runs after the daily work of the same day and
before the population, transport and budget month-end work, so the R/C/I unit totals used by the
tax revenue (§4 "Taxable units") are the ones closed in the same tick. The jobs valve 7000 is
updated in the same monthly work [INFERRED, medium: its owner, the world layer, has no schedule
of its own].

Each valve holds (all signed 32-bit integers unless noted):

| Field | Meaning |
|---|---|
| `Sacc`, `Dacc` | supply and demand being accumulated during the current cycle |
| `Slast`, `Dlast` | supply and demand of the previous cycle |
| `Sbase`, `Dbase` | constant supply/demand re-added at the start of every cycle |
| `Max` (> 0), `Min` (< 0) | value limits |
| `V` | current valve value (the demand reading) |
| `f1`, `f2` | f32 modifiers: `f1` = world economic cycle (§3.5), `f2` = tax effect (§3.6) |
| `capPct` | u8 0–100 |
| `smooth` | flag; when set, `V` moves gradually |

Setting `Max` never lets it fall below `Min` and vice versa. [STATIC] Limits are not saved; they
are set when the city is created or loaded:

| Valve | `Max` | `Min` | Provenance |
|---|---|---|---|
| 7000 (jobs) | 2000 | −2000 | [STATIC] |
| 5000, 8000 | 2000 | −2000 | [STATIC] |
| 5001, 8001 | 2 147 483 647 | −2 147 483 648 | [STATIC]; `Sbase` = the layer's `InitialConnectionSupply` (negative values become 0) |
| 3000 | 2000 | −2000 | [INFERRED, high: every shipped save fits these limits, e.g. a residential valve with `D − S` = 22 310 and `capPct` 20 holds `V` = 1600 = 2000 − 400; the setting code was not located] |
| 3001 | 2 147 483 647 | −2 147 483 648 | [INFERRED, high: `V = D − S` exactly in all shipped saves] |

`capPct` is non-zero only on the R/C/I valves (shipped saves: 6–100). It is set by the
**cap rule** below and saved, so a loaded city keeps it until the next month-end.

**Cap rule** [STATIC; DATA 13/13 played cities, every value exact]. Once a month, during the
month-end work of the residential (for 3000), commercial (for 5000) and industrial (for 8000)
layers, i.e. **after** that month's valve update, with `S` = `Slast` and `D` = `Dlast` of the
amenity valve (3001, 5001, 8001):

| Condition | `capPct` of the family valve |
|---|---|
| `D = 0` | 0 |
| `S < D` | 100 |
| otherwise | `⌊100 × D / S⌋` (integer division; the result is ≤ 100) |

So `capPct` is the amenity **usage** in percent: the more of the amenity supply is used, the
lower the ceiling of the family's demand in §3.2 step 4 (`Max − capPct × Max / 100`). The new value
first takes effect at the **next** monthly valve update.
Test vector (Madison, WI): 3001 `S` = 739 500, `D` = 150 984 → 20; 5001 `S` = 282 000, `D` = 33 668
→ 11; 8001 `S` = 503 200, `D` = 120 493 → 23 (saved values 20, 11, 23). Craterville 5001
`S` = 25 000, `D` = 71 566 → 100.

**Industrial ordinance demand cut** [STATIC; DATA]. Directly after setting the industrial cap,
if valve 8000's `Dlast > 0`: while the Power Conservation ordinance (`0x82B9999B`) is enacted,
8000 `Dacc += ⌊PowerConservationOrdEffect × Dlast / −100⌋`, and while the Water Conservation
ordinance (`0x02BF1DFA`) is enacted, 8000 `Dacc += ⌊WaterConservationOrdEffect × Dlast / −100⌋`
(both keys in `SC3IndLayer.ini [TuningParameters]`, shipped 3; division truncates toward 0). The
amount is applied in the next cycle. Test vector: Berlin 8000 `Dlast` = 113 594, one of the two
ordinances enacted → `Dacc` = −3 407 (saved −3 407).

Update (once per month, see **Schedule** above):

1. `Slast := Sacc; Dlast := Dacc; Sacc := Sbase; Dacc := Dbase`.
2. `raw := clamp(Dlast − Slast, Min, Max)`.
3. `raw2 := clamp(raw + trunc((f1 + f2) × Max), Min, Max)` (float product truncated toward zero).
4. `target := max(min(raw2, Max − trunc(capPct × Max / 100)), Min)`.
5. If `smooth` is not set: `V := target`.
   Otherwise, with `step := trunc((Max − Min) × 7 / 100)` and `δ := target − V`:
   * if `−step < δ < step`: `V := target`;
   * else if `δ > 0`: if `target > 0` and `V < 0` and `V + step < 0` then `V := 0`, otherwise
     `V := V + step`;
   * else (`δ ≤ −step`): if `target < 0` and `V > 0` and `V − step > 0` then `V := 0`, otherwise
     `V := V − step`.
6. Publish the normalised reading `N := V × 1000 / Max` if `V ≥ 0`, else `N := −(V × 1000) / Min`
   (so −1000 ≤ N ≤ 1000, negative = oversupply). The UI demand indicator and the advisors read
   `N` per valve ID after every update. [STATIC]

**Smoothing** [STATIC; DATA]. The `smooth` flag is set when **any** valve is created and nothing
clears it; it is not saved. Every valve therefore uses step 5's gradual rule. `step` is computed in
64-bit arithmetic: 280 for the ±2000 valves (3000, 5000, 7000, 8000), 300 647 710 for the
unbounded valves (3001, 5001, 8001, `0xA2C9E821`), which in practice makes those follow `target`
exactly. Because `V` is saved, a loaded city continues from the saved `V`. Note the zero-snap in
step 5: a positive `V` facing a negative `target` drops to 0 first (unless `V ≤ step`), then
moves 280 per month.
Test vectors (Max 2000, Min −2000, step 280): `V` = 1540, `target` = −2000 → 0 → −280 → −560 …
(−2000 after 8 months); `V` = −1000, `target` = 1600 → 0 → 280 → … ; `V` = 1500, `target` = 1700 →
1700 (|δ| < 280); `V` = 200, `target` = −2000 → −80 (`V ≤ step`, so no snap).

### 3.3 Contributions from buildings and connections

* **Buildings.** Every occupant whose agent type has an `[AgentValveEffects]` entry contributes,
  each cycle, while it is in the normal (occupied, functioning) state: for each
  `valve:supply:demand` item, `Sacc += tiles × supply` and `Dacc += tiles × demand` of that valve,
  where `tiles` = lot X × lot Y (attribute 0x66 of `13-buildings-occupants.md`). The values in the
  INI are therefore **per tile**. [STATIC]
  * *Which entry*: the entry of the building's exact agent type; if there is none, the entry of
    its **nearest** ancestor in `CitySim.ini [AgentTypeHierarchy]`. Exactly one entry applies;
    entries of further ancestors are not added. [STATIC; DATA: recomputing 3000 supply, 5000
    supply, 8000 supply and 7000 demand from the saved occupants this way reproduces the saved
    `Slast`/`Dlast` within 1 % in 13/13 played cities, exactly for Craterville 5000 and 8000]
  * *Which states count*: the lot state word with the developer flag `0x200` removed must be 1
    (occupied). So states 1 and 513 contribute; 2/514 (abandoned), 4/516 (under construction),
    8 (rubble) and 16 (burnt) contribute nothing. Occupants without a lot state (network pieces,
    props) contribute if they have an entry. [STATIC; DATA: Madison 5000 `Slast` 23 932 =
    23 390 (state 1) + 488 (state 513) + 54 (later changes)]
  * Contributions are added in the monthly valve-layer work, before the valve update; they are
    not accumulated during the month [INFERRED, high: saved `Sacc` of 5000/8000 is 0 in every
    played city, and `Dacc` holds only the items added after the update, see §3.4].
* Entry syntax: `agentType=valve:supply:demand+valve:supply:demand+…`, whitespace-separated
  trailing text is a comment. Supply and demand are 16-bit signed. [STATIC]
* **Per-tile valve maps.** For items on valves 3000, 5000 and 8000, the building's `supply` is
  added (saturating at 0 and 255) to a per-tile byte map for every tile of its footprint; for
  items on valve 7000, its `demand` is added the same way. These maps are density maps (residents
  and jobs per tile) used by other layers (e.g. traffic). Totals of R, C and I supply are also
  accumulated per category. [STATIC]
* **Neighbour connections.** For each connection kind k (0 road, 1 rail, 2 highway, 3 subway),
  `count_k` = number of neighbour connections of that kind; each item of
  `[ConnectionValveEffects] k=` adds `count_k × supply` and `count_k × demand`. [STATIC]
  `count_k` is the neighbour's saved connection **counter** for that kind (`10-save-format.md`
  §6.6: `nRoad`, `nRail`, `nHighway`, `nSubway`, summed over the neighbours), **not** the number
  of connection tiles. A road crossing the map edge can occupy several edge tiles, and Berlin
  has 19 road-connection tiles but 6 road connections. DATA: with the counters, Berlin's 5001
  and 8001 supplies at day 40 come out exactly at the saved 1 078 500 and 1 030 800 (an
  implementation check reported by the clean side).
* **Ordinances.** `[OrdinanceEffects] 0xOrdinanceID=valve,f+valve,f…` (f in thousandths): while
  the ordinance is enacted, each cycle the listed valve's `Dacc` is increased by
  `trunc(Dlast × f/1000)` (sign taken so that a negative `f` reduces demand when `Dlast` is
  positive). [STATIC]
* **Amenity demand of commerce and industry** [OPEN, narrowed]. For valve 3001 the rule above is
  complete: the saved `Dlast` equals the building sum within 1 % in 13/13 played cities. For 5001
  and 8001 the saved `Dlast` is **larger** than the building sum (the 5001/8001 items of commercial
  and industrial buildings) in every played city, by a factor of 1.33–2.09 (5001) and 1.47–2.01
  (8001):

  | City | 5001 `Dlast` saved | building sum | 8001 `Dlast` saved | building sum |
  |---|---|---|---|---|
  | Madison, WI | 33 668 | 22 148 | 120 493 | 60 527 |
  | Craterville | 71 566 | 38 806 | 171 484 | 87 268 |
  | Berlin | 614 402 | 322 895 | 180 069 | 113 068 |
  | Mount Herrang | 3 104 | 2 378 | 25 301 | 12 755 |

  The excess is not explained by `[ConnectionValveEffects]` (supply only), `[OrdinanceEffects]`
  (valves 5000/8000 only), abandoned or under-construction lots, or by applying ancestor entries
  as well (that would also inflate 5000/8000 supply, which matches the saves). No other rule that
  adds demand to 5001/8001 was found. Recommended until settled: keep the building rule and, when
  loading a city, carry the saved `Dlast` ratio (saved ÷ building sum) as a per-valve multiplier
  on the building demand of 5001 and 8001 until the next save; this keeps the cap rule's input at
  the original's level. Settling observation: in a new city with only zoned commercial
  buildings, record 5001 `Dlast` and the building sum across three consecutive month-end saves.

### 3.4 The jobs valve (7000) and the R/C/I coupling

The jobs valve is created by the world layer with `Max = 2000`, `Min = −2000`. [STATIC]

* When a building adds jobs demand `n` (its 7000 item): jobs `Dacc += n`, and the residents
  valve 3000 receives `Dacc += JobToResFactor × n` (`SC3WorldLayer.ini [WorldLayer]
  JobToResFactor`, an 8-bit integer, 0 treated as 1).
* When workforce supply `n` is added to the jobs valve: jobs `Sacc += n`, and `n` is split
  between commercial (5000) and industrial (8000) **demand** in the ratio `a : b` given by the
  economic phase (§3.5): `c := n·a/(a+b)`, `i := n·b/(a+b)` (integer), remainder
  `r := n − c − i`; if `r > 0` it goes to industry when commerce's running share this cycle
  (`Σc / (Σc + Σi)`, 0 when both are 0) is already ≥ `a/(a+b)`, otherwise to commerce. Then
  valve 5000 `Dacc += c`, valve 8000 `Dacc += i`, and the running sums are updated.
* Workforce supply comes from residents: whenever residential supply `n` (n ≠ 0) is added to
  valve 3000, the jobs valve receives workforce supply `round(n × w)` (round half to even), where
  `w` is the **working-age share** of the population [STATIC]:
  `w = p[20] + p[21] + … + p[65]`, the sum of the cohort shares of ages 20 to 65 inclusive
  (`20-sim-population.md` §3, §6). There is no unemployment, wealth or EQ weighting. `w` is read
  from the cohort shares current at the time of the valve update (day 1, before the population
  month-end work). Full definition and test vectors in `20-sim-population.md` §6.
* At the start of every jobs-valve cycle the running sums are reset and workforce supply
  `ExternalJobDemand` (`SC3WorldLayer.ini`) is added. [STATIC]

Resulting demand readings (before modifiers): residential = `JobToResFactor × jobs − residents`;
commercial = `workforce·a/(a+b) − commercial supply`; industrial = `workforce·b/(a+b) − industrial
supply`, each clamped to the valve limits and then modified by §3.2 steps 3–5.

### 3.5 World economy

* `EconomicPhases=P0:a0:b0 P1:a1:b1 …` — city population `P × 1000` maps to the commercial and
  industrial weights `(a, b)`. Interpolation is linear between neighbouring points using the
  same absolute-difference rule as the city-centre curve (§6.2); beyond the last point the
  last pair is used. If no point at population 0 exists, `(0 → 1:1)` is added. Evaluated every
  world-layer cycle. [STATIC]
* `[ListOfWorldValveMapping] valveID=d0:p0 d1:p1 …` — a repeating cycle for that valve:
  duration `d` in months, percentage `p`. Position in the cycle =
  `((year − 1900) × 12 + (month − 1)) mod Σd` (year clamped to ≥ 1900). The entry containing the
  position sets the valve's `f1 := p / 100`. A duration of 0 ends the list. Shipped data defines
  only valve 3000 (a 150-month cycle with short negative "recessions"). [STATIC]

### 3.6 Tax effect

Taxes exist for the three groups 3000 (R), 5000 (C) and 8000 (I) only; rates are integers 0–22 %
(new-city default 7 %). `SC3BudgetLayer.ini TaxationDemandEffect` defines, per population
breakpoint, a 23-entry table (one float per tax rate). Each month the table for the current
population is interpolated between the two surrounding breakpoints (with the original defect that
the interpolation weight uses the population itself instead of the population minus the lower
breakpoint; see `19-sim-economy.md`), and each of the R, C and I valves receives
`f2 :=` the entry for its group's current tax rate. `f2` is added (with `f1`) as a fraction of
`Max` in §3.2 step 3, so e.g. `f2 = −0.05` lowers that demand by 5 % of full scale. [STATIC]

### 3.7 Advisor thresholds

`Advisor-CityPlanner.ini [DEMAND_CUTOFFS]` gives the normalised-demand thresholds used only for
advisor messages (`22-advisors-news-petitioners.md`).

## 4. Zone developers

There is one developer per zone family (residential, commercial, industrial; plus airport,
seaport and landfill developers, §7). Each developer visits zoned tiles and, per tile, performs
the step of §4.3.

**Rate** [STATIC]. Each zone-manager tick visits exactly `K = (mapW × mapH) / 30` list entries
(integer division; 546 for a 128×128 map, 1228 for 192×192, 2184 for 256×256). Because
`30 × K < mapW × mapH` whenever the tile count is not a multiple of 30, a full pass takes **31**
ticks for all three legal map sizes (the 31st tick visits the remaining 4, 24 or 16 entries and
then rolls the pass over). `K` is recomputed when a city is created or loaded.
**Schedule.** The zone manager ticks once per simulated day, on the daily command of
`14-sim-clock.md` §2.2, and does nothing on the weekly, monthly or yearly commands [STATIC]. It
is not one of the daily area-work items listed in `14-sim-clock.md` §6.2; it is registered with
the scheduler on its own at the daily tier [INFERRED, high], so within a day it runs after the
daily area work (disasters, police, fire, land value, scenario) [INFERRED from registration
order]. A pass therefore takes 31 days.

**Committed demand `P`** [STATIC]. The zone manager keeps one signed accumulator per R/C/I valve
(3000, 5000, 8000), saved as the "valve commitment" table of `10-save-format.md` §6.4. `P` for a
developer is the accumulator of its family's valve.
* Whenever a building is added to the city whose lot state is occupied (1) or under construction
  (4) — in particular every building a developer starts — each accumulator increases by
  `tiles × e`, where `tiles` = footprint X × Y (computed as a signed 8-bit product) and `e` = the
  building's per-tile valve effect for that valve (its `[AgentValveEffects]` supply, §3.3)
  [INFERRED for `e`]. When such a building is removed, the same amount is subtracted.
  Every state change made by a developer (abandon, re-occupy, finish construction, redevelop)
  replaces the occupant: the old one is removed and a new one with the new state is added
  [STATIC], and the same goes for fire and rubble replacements [INFERRED]. Since only states 1
  and 4 count, the net effect on `P` is: abandonment (1 → 2) subtracts the lot's amount;
  re-occupation (2 → 1) adds it; completion (4 → 1) leaves it unchanged; burning or rubble
  (1 or 4 → 16 or 8) subtracts it [INFERRED, medium-high; DATA on the clean side: with the
  abandonment subtraction Berlin's first-pass code-3 count is 54 against 58 saved, instead of
  399].
* At every pass rollover (when the shuffled list is exhausted, below) all accumulators are reset to 0.
So `P` approximates the supply that has been built since the last pass started but is not yet
reflected in the valve (valves update monthly). It can be negative when more was removed than
built. `V − P` is the "uncommitted" demand used throughout §4.3 and §5.

**Scan order** [STATIC][DATA 59/59]. The zone manager visits tiles in passes. A pass walks a
list containing every map tile exactly once, in a random order; the tile's zone type selects the
developer (tiles whose zone has no developer are skipped but still consume a visit). When the list
is exhausted, the per-pass statistics roll over and the list is shuffled again (Fisher–Yates
driven by the C-runtime `rand()`, not the simulation RNG). The list, the position in it and the
statistics (visits per result code and zone type) are saved with the city, so a loaded city
continues the same pass. The full algorithm and the persisted layout are in `10-save-format.md`
§6.4. Result codes 1–8 and 9–18 (§4.3) are counted separately per pass.

**Taxable units** [STATIC]. In each valve-layer update cycle every occupied building adds, per
`[AgentValveEffects]` entry of its agent type for valve 3000/5000/8000, `footprint tiles × supply`
to the R/C/I unit total of that family (no service multiplier). The totals of the last completed
cycle are the `N` of the R/C/I tax revenue (`19-sim-economy.md`) and are saved in
`10-save-format.md` §6.7, together with a per-tile map of the same contributions (clamped 0–255; the
jobs valve 7000 adds its demand there).

### 4.1 Tunables (`SC3Tune.ini`)

Sections `[ResidentialZoneDeveloper]`, `[CommercialZoneDeveloper]`, `[IndustrialZoneDeveloper]`,
all integers: [STATIC]

| Key | Units | Use |
|---|---|---|
| `NoServicePercentage` | percent per lot | chance per visit that an occupied lot lacking a required service is abandoned, divided by the lot's tile count |
| `MaxDistFromTransport` | tiles | maximum distance from the lot to a transport network tile; +1 while the ordinance with ID `0xA2BF1DE5` is enacted |
| `ConstructionTimeFactor` | per (tiles × 100) | chance per visit that construction completes: `ConstructionTimeFactor / (tiles × 100)` |
| `HigherLandValueRedevelopmentPct` | percent | chance to redevelop to a higher-land-value category when one fits |
| `LowerLandValueRedevelopmentPct` | percent | chance to redevelop to a lower-land-value category when land value has fallen (see defect note) |
| `HigherDensityRedevelopmentPct` | percent | chance to redevelop to a higher-density category when one fits |
| `AnarchyFactor` | per 10 000 | chance per visit that a healthy lot is rebuilt with a different building of the same category |

### 4.2 Tile inputs

For the visited tile the developer reads: [STATIC]

* `powered` (power layer), `watered` (water layer), `LV` (land value of the tile, §6), `access`.
* `access` [STATIC]: with `d` = `MaxDistFromTransport` (+1 with ordinance `0xA2BF1DE5`) and
  (x, z) = the visited tile (for an occupied lot this is the lot's **anchor** tile, not its
  footprint), the box [x − d − 1, x + d + 1] × [z − d − 1, z + d + 1], clipped to the map, must
  contain at least one tile occupied by a **transport network piece** (an occupant whose kind
  flag marks it as a transport piece). Bus stops and train or subway stations are **not**
  counted. So the reach is `d + 1` tiles in Chebyshev distance from the anchor. [INFERRED,
  medium: transport pieces are the road, avenue, highway and rail pieces with their bridges and
  ramps; whether subway and power-line or pipe pieces carry the flag was not checked]
  Test vector (`d` = 4): anchor (50, 50) has access if a road piece lies anywhere in
  [45, 55] × [45, 55]; a road at (56, 50) does not count, even for a 4 × 4 lot whose far edge is
  at x = 53.
* `blocked`: the tile is **radioactive** (the radiation flag of the pollution layer,
  `17-sim-services.md` §7.2) [STATIC].
* `unfit`: the lot's last traffic trip **failed** (trip result 1 of `18-sim-transport.md` §9.2),
  read at the lot's anchor tile [INFERRED, high: the per-tile test comes from a city layer other
  than power, water, land value, pollution and transport access, and its effect, an abandonment
  roll with `NoServicePercentage` and no redevelopment, is exactly the trip-failure rule of
  `18-sim-transport.md` §9.2]. Tiles without a trip record are not `unfit`.
* `V` = current value of the zone family's valve (3000, 5000 or 8000; §3.2), `P` = committed demand
  (§4 "Committed demand") [STATIC], `Max`, `Min` = the valve limits.
* For an existing lot, its anchor tile, state (the lot state word of `13-buildings-occupants.md`
  Persisted state: 1 occupied, 2 abandoned, 4 under construction, 8 destroyed (rubble), 16
  fire-damaged; the developers act only on states 1, 2 and 4 and skip lots in other states),
  its building's category (§4.4; "none" when the building is not one of this developer's
  categories, e.g. a plopped building), `tiles` = lot X × Y, and the category's density level.

Only the lot's anchor (origin) tile is processed; other tiles of a lot are ignored.

### 4.3 Per-tile step

Result codes (shown by the zone query tool, `09-ui-screens.md`) are given in brackets. [STATIC]
"Flagged `0x200`" below means bit `0x200` of the same lot state word (the developer flag of
`13-buildings-occupants.md` Persisted state; shipped values 513, 514, 516). A flagged lot keeps
its building: it is never removed or redeveloped by a developer, and an abandoned flagged lot is
re-occupied with the same building [STATIC].

| Code | Meaning | Produced by |
|---|---|---|
| 0 | no result this visit | all |
| 1 | abandoned: tile radioactive | R, C, I |
| 2 | abandoned: land value too low for the building | R, C, I |
| 3 | abandoned: negative demand | R, C, I |
| 4 | abandoned: no power | R, C, I |
| 5 | abandoned: dense lot without water | R, C, I |
| 6 | abandoned: no transport access or trip failed | R, C, I |
| 7 | abandoned: farm pollution | I |
| 8 | never produced [INFERRED] | – |
| 9 | airport developer (zone 14): empty tile powered, serviced (see §7) and with transport access within 6 tiles, but no rule placed anything this visit [STATIC]; saved passes: Berlin 80, London 112, Madrid 128, Sacramento 28, Seoul 2 per pass, each **exactly** the number of zone-14 tiles that no occupant's lot covers [DATA]. The landfill developer also produces code 9, for a landfill tile that is not part of a 2 × 2 landfill square (§7). Seoul has 2 such tiles, so its total is 4 | airport, landfill |
| 10 | not developed: tile radioactive | R, C, I |
| 11 | not developed: land value below every category | R, C, I |
| 12 | not developed: land value above every category (I: also no category on a watered tile, defect §4.3.2) | R, C, I |
| 13 | not developed: negative demand | R, C |
| 14 | not developed: no power | R, C, I |
| 15 | not developed: no category fits (unwatered) | R, C, I |
| 16 | not developed: no transport access | R, C, I |
| 17 | seaport developer (zone 15): the seaport counterpart of code 9, an empty serviceable tile where no rule placed anything [STATIC] | seaport |
| 18 | not developed: no category fits (watered) | R, C |

Codes 1–8 are the occupied-lot codes and 9–18 the empty-tile codes; the pass statistics count
the two groups separately (§4 "Scan order").

**Empty zoned tile.**
1. If `V < 1` or `V − P < 1`: stop (code 13 if `V < 0`, else no code).
2. If `blocked`: stop [10]. If not `powered`: stop [14]. If not `access`: stop [16].
3. If `LV` is below the lowest `minLV` of the zone's categories: stop [11]; above the highest
   `maxLV`: stop [12].
4. Pick a category (§4.4, rule E). None: stop [15, or 18 if `watered`].
5. Start development (§5.3).

**Under construction (state 4).**
* Lot whose building has no category (and is not a filler lot): if `V > 0`, `V − P > 0`, the lot is
  not flagged `0x200` and the building is developer-removable, the building is removed from the
  city (the tile becomes empty zoned land and is developed on a later visit) [STATIC for industry;
  INFERRED, high, for residential and commercial].
* Otherwise, if `powered`, `access`, not `blocked`, and `rand(tiles × 100) < ConstructionTimeFactor`:
  the lot becomes occupied (state 1).

**Abandoned (state 2).** If `powered`, `access`, not `unfit`, not `blocked`, `V > 0` and `V − P > 0`:
choose a category by rule E; if it equals the current one (or the lot carries the
"keep building" flag), re-occupy the same building (state 1), else redevelop with the new category
(§5.3). Lots without a category are redeveloped when the same demand conditions hold.
There is **no water condition** for re-occupation, but rule E (§4.4) admits only density-1
categories on an unwatered tile, so an abandoned **dense** lot on dry land is not re-occupied:
it is redeveloped as a low-density building [STATIC]. Madrid shows this in the original
(`28-conformance.md` §5.1): the dry dense lots abandoned in the first pass after load are
rebuilt, and the number of occupied low-density residential lots rises from 5 152 to 6 842 by
day 61.

**Occupied (state 1).**
1. If `blocked`: abandon [1].
2. If not `powered`, not `access`, `unfit`, or (not `watered` and density level ≥ 2): with
   chance `rand(100) < NoServicePercentage / tiles` (integer division) abandon
   [4 if unpowered, 5 if only water is missing on a dense lot, 6 otherwise].
3. Else if `V < 0` and `rand(−Min) < −((V − P) / tiles)`: abandon [3].
4. Else, if the category's `⌊minLV / 2⌋ ≤ LV` (land value still adequate), with "flagged"
   meaning the lot is flagged `0x200` (above): [STATIC for all three developers; DATA: 97–99.9 %
   of occupied R/C/I lots in the shipped cities satisfy this test, against 41–62 % for the
   `maxLV / 2` test of earlier drafts]
   a. If not flagged, `V > 0`, the lot has a category and a higher-land-value category fits
      (rule H): redevelop with it when `rand(100) < HigherLandValueRedevelopmentPct`, otherwise
      do nothing this visit (b and c are **not** tried).
   b. Else, if `watered` and not flagged:
      * if `V > 0`, the lot has a category and `rand(Max / tiles) < X` (residential: `X = V`;
        commercial and industrial: `X = V − P`): if a higher-density category fits (rule D),
        redevelop with it when `rand(100) < HigherDensityRedevelopmentPct`, otherwise do nothing;
        if none fits, do nothing (c is not tried);
      * otherwise (the `rand(Max / tiles)` test failed, or `V ≤ 0`): if `V > 0`, the lot has a
        category and `rand(10000) < AnarchyFactor`: redevelop with the same category (a fresh
        random building).
   c. Unwatered lots are never redeveloped for density or by anarchy.
5. Else (`LV < ⌊minLV / 2⌋`, land value fell below half of the category's **minimum**): look
   for a lower-land-value category (rule L); if found and `rand(100) <
   LowerLandValueRedevelopmentPct` redevelop, else abandon [2].
   Test vector: residential category 3300 (`minLV` 76, `maxLV` 150): `LV` = 38 → step 4 (kept);
   `LV` = 37 → abandoned [2]. Industrial 8300 (`minLV` 1): never abandoned for land value.

`rand(n)` is the simulation RNG returning an integer in `[0, n)` (`14-sim-clock.md`).

Defect (all three R/C/I developers): the lower-land-value search (rule L) never returns a
category, so a lot whose land value drops below half its category's `minLV` is always abandoned
in step 5 (farm fields: removed, §4.5). `LowerLandValueRedevelopmentPct` therefore has no effect.
[STATIC for each of residential, commercial and industrial]

Note on the category record layout [STATIC]: the residential developer stores each category as
(density, maxLV, minLV) and the commercial and industrial developers as (density, minLV, maxLV);
all three compare the **minimum**. An implementation reading `[ZoneDeveloperDescriptions]`
(`agentType,density,maxLV,minLV`) must use the fourth number (the smaller one) in steps 4–5.

### 4.3.1 Commercial developer differences [STATIC]

The commercial developer follows §4.3 exactly, except:
* step 4b uses `X = V − P` (residential uses `V`);
* its category table is the commercial list of §2; its tunables are `[CommercialZoneDeveloper]`.
Result codes, including 13 (`V < 0` on an empty tile) and 15/18 (no category; 18 when watered),
are as for residential.

### 4.3.2 Industrial developer differences [STATIC]

The industrial developer follows §4.3 with these changes (in addition to the set choice of §4.5):
* **Empty tile:** when `V < 1` or `V − P < 1` it stops **without** a result code (no code 13).
  The category pick first allows agriculture (if the tile qualifies, §4.5); if that yields nothing
  and agriculture was allowed, it retries with agriculture disallowed. If still nothing: code 15
  when unwatered, but code **12** when watered (original defect; residential/commercial give 18).
* **Year curve refresh:** `minClean`/`maxClean` (§4.5) are recomputed on the first industrial visit
  in a January (once per year).
* **Occupied lot, extra rules** (after the `blocked` check, inside the "services present" branch):
  * a farm lot (category 9300) whose tile air pollution > `MaxAgAirPollution` or water pollution
    > `MaxAgWaterPollution` is abandoned [code 7];
  * step 4b uses `X = V − P`.
* **Farm fields.** A building of category 9300 whose flag word (attribute 0xCA,
  `13-buildings-occupants.md`) has both bit `0x100` (filler) and bit `0x400` is a *farm field*.
  Farm fields are never abandoned: where another lot would be abandoned (codes 1, 2, 7) the field
  is instead removed from the city (only if it is a developer-removable building and not
  flagged `0x200`); they are exempt from the demand abandonment of step 3 and from the
  "services missing" test (they are processed as if services were present).
* **Filler lots** (bit `0x100`): the developer keeps the lot's `0x100` state flag in step with a
  per-tile flag of the pollution/hazard layer [INFERRED; exact meaning OPEN].

### 4.4 Category selection rules

Each zone has a category list from `[ZoneDeveloperDescriptions]` (§2); each category =
(agent type, density level d, maxLV, minLV). A category **fits** `LV` when `minLV ≤ LV ≤ maxLV`.
Without water only density-1 categories are eligible (rules E and H). [STATIC]

* **E (empty lot):** among fitting categories pick the one with the greatest `minLV`; ties keep
  the first in list order.
* **H (higher land value):** among fitting categories with `minLV` greater than the current
  category's `minLV` and density ≥ the current density, pick the greatest `minLV`.
* **D (higher density):** among fitting categories with density ≥ current density + 1, pick the
  greatest `minLV` (no water restriction at this point; the caller already required water).
* **L (lower land value):** see the defect note above.

### 4.5 Industrial specifics: clean, dirty and agriculture

The industrial developer follows §4.3 but chooses among three **category sets**: clean
(`C` categories of `[ZoneDeveloperDescriptions]`), dirty (`D`) and agriculture (`A`). [STATIC]

**Year curve.** With `Y` = current year (1900 is the curve origin):

* `minClean = MinMinCleanInd + (MaxMinCleanInd − MinMinCleanInd) × (Y − 1900) / (MinCleanIndFlatYear − 1900)`
  if `Y < MinCleanIndFlatYear`, else `MaxMinCleanInd`;
* `maxClean = MinMaxCleanInd + (MaxMaxCleanInd − MinMaxCleanInd) × (Y − 1900) / (MaxCleanIndFlatYear − 1900)`
  if `Y < MaxCleanIndFlatYear`, else `MaxMaxCleanInd`;

integer arithmetic, results stored as bytes (percent). All six keys are in
`SC3Tune.ini [IndustrialZoneDeveloper]`.

**Set choice for a new industrial lot:**

1. `pAg = PctChanceAg`, minus `LawnChemBanOrdEffect` if ordinance `0x62F6E808` is enacted, plus
   `FarmersMarketOrdEffect` if ordinance `0x62F6E82B` is enacted.
2. Agriculture is allowed on the tile when its air pollution ≤ `MinAgAirPollution` and its water
   pollution ≤ `MinAgWaterPollution`. If the zone has agriculture categories, `pAg ≥ 1`, it is
   allowed, and `rand(100) < pAg`: use the agriculture set.
3. Otherwise, if the zone manager's city-wide "clean industry only" flag is set, use the clean
   set [STATIC that a city-wide flag held by the zone manager forces the clean set; OPEN: what sets
   the flag. Question for testing: is it tied to a scenario or to an ordinance? Recommended until
   answered: flag never set]. Otherwise:
   * `e = max(EQ, 85)` where EQ is the **working-age EQ** reported by the residential population
     layer: `Q(20, 65)` of `20-sim-population.md` §4.5, the unweighted mean EQ of the ages 20–65
     that have a positive share, rounded to a byte [STATIC: the developer reads the layer value
     defined as Q(20, 65); it is **not** the city EQ Q(0, 89) of the charts] (EQ values are
     capped at 150, so `e` lies in 85–150 and the next line maps it to 0–100 %);
     `clean = clamp((e − 85) × 100 / 65, minClean, maxClean)`; `dirty = 100 − clean`.
   * Ordinance adjustments while enacted (all values from `[IndustrialZoneDeveloper]`):

     | Ordinance ID | Key | Effect |
     |---|---|---|
     | `0xA2BF1E43` | `CleanAirOrdEffect` | dirty −= value |
     | `0x62F6E7CF` | `TouristPromotionOrdEffect` | clean += value |
     | `0xA2F6E7DA` | `ConservationCorpsOrdEffect` | clean += value |
     | `0x02F6E7E2` | `IndPolluteFeeOrdEffect` | dirty −= value |
     | `0xE2F6E7EA` | `CleanIndAssocOrdEffect` | clean += value and dirty −= value |
     | `0x02F6E7EF` | `ElectronicsIncentiveOrdEffect` | clean += value |
     | `0x22F6E7F4` | `AerospaceIncentiveOrdEffect` | clean += value |
     | `0xA2F6E7F9` | `ElectronicsJobFairOrdEffect` | clean += value |
     | `0xE2F6E7FE` | `BiotechIncentiveOrdEffect` | clean += value |
     | `0xC2F6E804` | `PublicAccessCableOrdEffect` | clean += value |

   * If `clean ≥ 1` and `rand(100) < clean`: clean set. Else if `dirty ≥ 1`: dirty set. Else no
     category (nothing is built).
4. Within the chosen set apply rule E of §4.4.

**Farms.** An occupied agriculture lot (agent type 9300) is abandoned when its tile's air
pollution > `MaxAgAirPollution` or water pollution > `MaxAgWaterPollution`. [STATIC]

## 5. Developer rules and building families

### 5.1 `SC3ZoneLayer.ini [ZoneDeveloperRules]`

Syntax: `zone,0xRuleID=T,G,family;T,G,family;…` — for zone type `zone`, developer rule class
`RuleID` may build from the listed building families (`T:G:I` keys of `BUILDFAM.IXF`, always
`81F53D09:01F58D3D:family`). [DATA] Each family is filed under its `LandUse` agent type, i.e.
under one category. [STATIC]

Known rule class IDs (from the shipped bindings): [DATA]/[INFERRED]

| Rule ID | Zones | Families | Purpose |
|---|---|---|---|
| `0xA22653C0` | R 1–3 | 11, 4, 31, 10 | low-density scatter |
| `0x222C9D00` | R 2–3 | 12, 16, 17 | medium-density scatter |
| `0x822C9D13` | R 3 | 13, 14, 15 | high-density scatter |
| `0xE22F4A83` | R 1–3 | 10 | estates |
| `0x61F44687` | R 2–3 | 1 | row houses |
| `0x630EA608` | R 2–3 | 16, 28, 29, 30 | housing projects |
| `0x422C9D52` | C 5–7 | 7, 9 | low-density scatter |
| `0x622C9D42` | C 6–7 | 8, 23 | medium-density scatter |
| `0x822C9D62` | C 7 | 22, 24 | high-density scatter |
| `0x422B55F2` | I 9–11 | 5, 20 | low-density scatter |
| `0x822C9D24` | I 10–11 | 6, 19 | medium-density scatter |
| `0xC22C9D34` | I 11 | 18, 21 | high-density scatter |
| `0xA2C1EDA3` | I 9 | 25 | farm (agriculture) |
| `0x81FFC3CE`, `0x2207D9EE` | 14 | 2, 3 | airport runway / airport buildings |
| `0xC2D3FD7E`, `0xE2D3FD92` | 15 | 26, 27 | seaport pier / seaport yard |

### 5.2 Family member lists

When a family is bound to a rule, each member building is copied `weight` times into one of
three lists of that category: [STATIC]/[DATA]

* **Filler list**: members whose attribute 0xCA has bit `0x100` set (filler lots: gardens,
  parking, vacant lots…), any size;
* **Small list**: other members with a 1-tile edge;
* **Large list**: other members with an edge ≥ 2.

Weighted random choice = uniform choice from the list with duplicates.

### 5.3 Starting development

1. **Lot finding** [STATIC]. Starting from the 1 × 1 rectangle at the visited tile, the rectangle
   is grown greedily: alternately try to add one column (first the column just left of it, i.e.
   x0 − 1; if that fails, the column x1 + 1) and one row (first y0 − 1, then y1 + 1), repeating
   until neither a column nor a row can be added. A column/row is added only if it lies inside the
   map and **every** tile of it (over the rectangle's current span) is acceptable:
   a. same zone type as the visited tile;
   b. same watered state as the visited tile [INFERRED: the compared layer is the water layer];
   c. powered (this test is skipped when the caller requests it) [INFERRED: power layer];
   d. land value ≥ the requested minimum (the category's `minLV`);
   e. if a building stands on it: the building must be developer-replaceable and its lot not
      flagged `0x200`; and either it is of the same agent type as the building being replaced
      (or of the special kind 5), or its lot is abandoned (state 2), or it is a filler lot (flag
      word bit `0x100`) that is not destroyed (state 8) or fire-damaged (state 16); otherwise the
      tile is rejected;
   f. a transport tile lies within `MaxDistFromTransport` (+1 with ordinance `0xA2BF1DE5`).
   The resulting rectangle is handed to the rules (step 2), which may cut it further.
2. Up to `min(4, number of rules bound to the zone)` distinct rules are tried in uniformly random
   order; the first rule that places at least one building wins. [STATIC]
3. Placed buildings start in state 4 (construction) using `SC3BuildingLayer.ini` construction
   appearances (`13-buildings-occupants.md` §6).

### 5.4 `SC3Tune.ini [ZoneDeveloperRules]` — rule parameters `0xRuleID=a,b,c,d`

For the scatter rules: [STATIC for `0x422C9D52`; same structure INFERRED for the other scatter
rules]

| Param | Meaning |
|---|---|
| `a` | minimum lot edge (tiles); the candidate rectangle must be at least `a × a` |
| `b` | maximum lot edge (tiles), capped at 10; a larger rectangle is cut down to `b` along each axis, keeping the end whose edge has the better transport frontage |
| `c` | minimum demand in percent: the rule is used only when `(V − P) × 100 / Max ≥ c` |
| `d` | interior/frontage mix in percent (see step 3 below) |

Placement inside the (possibly reduced) rectangle of width `w` and depth `h`:

1. Score the four edges of the rectangle by transport frontage.
2. For each of the four corners in turn (order: the corner shared by edges 3 and 0, then 0–1,
   1–2, 2–3): if both adjacent edges are fully fronted, pick a random building from the large
   list; if it fits (`edge ≤ w` and `≤ h`), place it in that corner (anchored so that it stays
   inside the rectangle) and mark its tiles used.
3. Then for every unused tile, row by row: let `p = 100 − d` if the tile is adjacent to a
   transport tile, else `p = d`. If `rand(100) < p` choose from the small list, else from the
   filler list. If the chosen building fits inside the rectangle from this tile, try to place it
   and mark its tiles used.

Only the nine scatter rules, the farm rule and the seaport-yard rule `0xE2D3FD92` have an entry
in the shipped `[ZoneDeveloperRules]`; the other rules take no parameters. [DATA]

### 5.5 Non-scatter rules

**Housing projects `0x630EA608`** [STATIC]. Used only when `(V − P) × 100 / Max > 0`. Picks one
random building from the category's large list (non-filler members with an edge ≥ 2) and then,
with `r = rand(100)`: `r < 33` layout A, `33 ≤ r < 66` layout B, else layout C (three fixed
arrangements of that building plus filler around it). Layout A, with `e` = the building's edge:
the lot rectangle must be at least `2e × 2e`; starting from a random corner (`rand(4)`) and
continuing through the corners in turn, take the first corner whose `2e`-long stretches of both
adjoining edges are entirely fronted by transport; in the `2e × 2e` square at that corner place
the building in the three `e × e` quadrants that touch those two edges, and fill each tile of the
fourth (inner) quadrant with a random member of the category's filler list. If no corner qualifies
nothing is built.

Layouts B and C [STATIC]. Both use the same building (edge `e`) and the category's 1 × 1 filler
list. A side of the lot is tried starting from a random side (`rand(4)`) and continuing through
the sides in turn; the first side that has the required room and whose frontage strip is
**entirely** fronted by transport is used. Coordinates below are for side 0 (the low-z edge, the
strip running along x from the low-x corner); the other sides are the same pattern rotated so
that "row 0" is the row against that side.

*Layout B* (staggered triple). Room: `3e` along the side and `e + 1` deep. Frontage strip: the
`3e` tiles of row 0. Placement (tile offsets from the lot corner, along × depth):

| Item | Tiles | Building orientation |
|---|---|---|
| filler | row 0, along 0 … e−1 | – |
| building | anchored at (0, 1) | 0 |
| building | anchored at (e, 0) | 1 |
| filler | row e, along e … 2e−1 | – |
| filler | row 0, along 2e … 3e−1 | – |
| building | anchored at (2e, 1) | 0 |

So the middle building sits on the road and the outer two are set back one tile behind a row of
filler. Example, `e` = 2, side 0, lot corner (10, 20): fillers at (10,20) (11,20) (14,20) (15,20)
(12,22) (13,22); buildings anchored at (10,21) (12,20) (14,21).

*Layout C* (pair with a path). Room: `2e + 1` along the side and `e` deep. Frontage strip: the
first `2e` tiles of row 0. Placement: building anchored at (0, 0); a column of `e` filler tiles at
along = `e`, depth 0 … e−1; building anchored at (e + 1, 0). Example, `e` = 3, corner (0, 0):
buildings at (0,0) and (4,0); fillers (3,0) (3,1) (3,2).

If no side qualifies, the layout builds nothing (the next rule is tried, §5.3).

**Farm `0xA2C1EDA3`** [STATIC]. Parameters `a,b,c,d`: `a` is raised to at least 4, `b` to at least
`a`, both capped at 22; `c` and `d` are capped at 32767.
* `a` = minimum lot edge: both lot sides must be ≥ `a`.
* `b` = maximum lot edge: a longer side is cut to `b` (step 4). Correction: the previous revision
  said `b` is unused.
* `c` = maximum air pollution, `d` = maximum water pollution (0–32767 scales of
  `17-sim-services.md`) on the lot's edge tiles (shipped 450 and 450).

*Member lists* (one set per family of the category; members are entered as often as their
weight). Filler members (flag `0x100`) with edge 1 go to **fields**. Non-filler members go by
edge: ≥ 3 → **farmhouse**, 2 → **outbuilding**, ≤ 1 → **yard piece**. A family is registered only
if all four lists are non-empty. The rule fails if the category has no registered family.

Procedure, with lot [x0..x1] × [z0..z1], W = x1 − x0 + 1, H = z1 − z0 + 1:
1. W ≥ `a` and H ≥ `a`, else fail.
2. **Surroundings.** Every in-map tile of the row z0 − 1 (x0…x1), column x0 − 1 (z0…z1),
   column x1 + 1 (z0…z1) and row z1 + 1 (x0…x1) must have zone type 0, 1, 5, 9 or 22. The four
   diagonal corner tiles of the ring are not checked. Otherwise fail.
3. **Edges.** Each of the four lot edges is walked without its two corner tiles: edge 0 = row z0,
   edge 1 = column x1, edge 2 = row z1, edge 3 = column x0. Any tile with air > `c` or water > `d`
   fails the rule. n_i = number of tiles on edge i with transport access. The number of edges with
   n_i > 0 must be **1 or 2**; with 0, or with 3 or more, the rule fails.
4. **Cut.** If W > `b`: keep the high-x end (x0 = x1 − `b` + 1) when n_1 > 0, otherwise keep the low-x
   end (x1 = x0 + `b` − 1); W = `b`. If H > `b`: keep the low-z end (z1 = z0 + `b` − 1) when n_0 > 0,
   otherwise keep the high-z end (z0 = z1 − `b` + 1); H = `b`.
5. **Farmstead side** s = the edge with the largest n_i. Edges are scanned 0…3 and only a strictly
   larger count replaces the current one. Farmstead strip [xs..xe] × [zs..ze], with sizes
   SX × SZ:
   * s = 0: SX = clamp(⌊W/2⌋, 3, 8), SZ = clamp(⌊H/3⌋, 3, 4); xs = x0 + `Rand`(W − SX + 1), zs = z0.
   * s = 1: SX = clamp(⌊W/3⌋, 3, 4), SZ = clamp(⌊H/2⌋, 3, 8); xs = x1 − SX + 1,
     zs = z0 + `Rand`(H − SZ + 1).
   * s = 2: SX = clamp(⌊W/2⌋, 3, 8), SZ = clamp(⌊H/3⌋, 3, 4); xs = x0 + `Rand`(W − SX + 1),
     zs = z1 − SZ + 1.
   * s = 3: SX = clamp(⌊W/3⌋, 3, 4), SZ = clamp(⌊H/2⌋, 3, 8); xs = x0,
     zs = z0 + `Rand`(H − SZ + 1).

   xe = xs + SX − 1, ze = zs + SZ − 1. A per-tile "used" grid over the lot starts clear.
6. **Farmhouse.** Pick a farmhouse (`Rand(list size)`) with edge e. Anchor it at
   (xs + `Rand`(SX − e + 1), zs + `Rand`(SZ − e + 1)), orientation 0. If placed, mark its e × e tiles
   used.
7. **Outbuildings.** Make up to 5 attempts, stopping after 2 successes. Each attempt picks an
   outbuilding (edge e) and an anchor as in step 6. If any of its e × e tiles is already used,
   the attempt is wasted. Otherwise it is placed and, if that succeeds, its tiles are marked used.
8. **Yard.** For each tile of the strip (x from xs to xe, then z from zs to ze) that is not used,
   place a random yard piece (orientation 0) and mark the tile used if placing succeeds.
9. **Fields.** Rotation R = 90° if `Rand(100)` ≤ 50, else 0°. Pick a field piece P. Walk every tile
   of the (cut) lot: when R = 0°, x is the outer loop (x0…x1) and z the inner loop (z0…z1); when
   R = 90°, z is outer and x inner. For a tile that is not used: place P with rotation R and mark
   the tile used whether or not placing succeeds; then, with `Rand(100)` < 30, pick a new P.
   For a used tile, pick a new P. After the last tile the rule **succeeds**.

All random draws use the zone layer's generator (`Rand` as in `16-sim-utilities.md` §1.1), in the
order written. Correction: replaces the interim field tiling and the farmstead sizes inferred
in the previous revision.

**Airport runway `0x81FFC3CE`** (zone 14) [STATIC]. Used only when `V − P > 0` and the rule has
families. The lot must be at least 3 × 3 with at least one edge ≥ 5. Each of the four sides of the
lot is scored by inspecting the one-tile strip just outside it (sides lying along an edge shorter
than 5 get score −20 instead, i.e. a runway is laid along an edge of length ≥ 5). Score of a
strip: for each building overlapping it (at most 10 considered), `+2 × its X size` if its agent
type equals the runway category's agent type, else `−(its X size)`; plus 1 for every strip tile
zoned airport (14). Runways therefore extend existing runways and prefer airport-zoned
surroundings. The side with the highest score is chosen,
scanning sides in order 0–3 and switching on a tie when `rand(100) > 49`. If the best score is
negative and the lot is exactly 5 × 3 nothing is built; otherwise a runway is laid oriented to
that side.

*Runway piece sequence* [STATIC]. **Runway sets.** Each family bound to the rule (shipped: one
family) forms one runway set; a set is chosen uniformly at random when a runway is laid. A
member is filed by its flag word f (attribute 0xCA), its edge `e` (only 1 × 1 members are used)
and its **line number** L (1, 2 or 3; the puzzle-part index of `13-buildings-occupants.md`
attribute 0x1F4 [INFERRED for the attribute]):

| Slot | Condition | Role |
|---|---|---|
| S0 | L = 1, f has `0x800`, none of `0x20`/`0x10`/`0x40`/`0x80` | runway middle |
| S1 | L = 1, f has `0x400`, none of `0x20`/`0x10`/`0x40`/`0x80` | runway middle, alternative |
| S2 | L = 1, f has `0x20` | start cap, alternative |
| S3 | L = 1, f has `0x10` | start cap |
| S4 | L = 1, f has `0x80` | end cap |
| S5 | L = 1, f has `0x40` | end cap, alternative |
| S6 | L = 2, f has `0x800` | line-2 piece |
| S7 | L = 2, f has `0x400` | line-2 piece, alternative |
| S8 | L = 3, f has `0x800` | line-3 piece |
| S9 | L = 3, f has `0x400` | line-3 piece, alternative |
| F | f has `0x100` | filler list |

A slot holds the last matching member. A member whose part index is not 1, 2 or 3 (e.g. 0) fills
no slot. **Set validation** [STATIC]: for each pair (S0, S1), (S2, S3), (S4, S5), (S6, S7),
(S8, S9), if one slot is empty the other must be filled and its flag word must have bit `0x200`;
otherwise the family is not registered as a runway set. (In the shipped set S1, S2, S5 and S6
are empty and their partners carry `0x200`, so the set is valid.) "X, else its alternative rotated r" below means: use slot
X with rotation 0 if it is filled, else the alternative slot with rotation r.

**Layout.** The runway is a strip `L` = min(10, lot length along the chosen side) long. With `D`
= lot depth, at each position along the strip pieces are placed in consecutive rows starting
against the chosen side: row A always; then a line-3 row if `D ≥ 3`; then a line-2 row if
`D ≥ 2` (so a 2-deep lot gets rows A and line-2; a deeper lot A, line-3, line-2):

| Position | Row A | line-3 row | line-2 row |
|---|---|---|---|
| first | S3, else S2 rotated 270° | S8, else S9 rotated 270° | S6, else S7 rotated 270° |
| middle (1 … L−2) | S0, else S1 rotated 90° | first member of F | same piece as first position |
| last | S4, else S5 rotated 270° | same piece as first position | same piece as first position |

Test (shipped category 12310; part index from attribute 0x1F4: 0xA00, 0xA10, 0xA80 = 1; 0x600 =
2; 0xC00 = 3; 0x660 = 0): S0 = 0xA00, S3 = 0xA10, S4 = 0xA80, S7 = 0x600, S8 = S9 = 0xC00, F =
the two 0x100 members; S1, S2, S5 and S6 empty; the 0x660 member is not used by the runway rule.
Pieces used: row A first 0xA10, middle 0xA00, last 0xA80; line-3 row 0xC00 (S8); line-2 row
0x600 rotated 270° (S6 empty → S7); line-3 middle the first filler. A 7 × 3 lot gives 21 pieces.

**Airport buildings `0x2207D9EE`** (zone 14) [STATIC]. Used only when `V − P > 0`. The lot is
cut to at most 10 × 10 (keeping its low-x/low-y corner). Attempts, in order, until one succeeds:
(1) the edge tiles of the lot are scanned (low-x column, then high-y row, high-x column, low-y
row; the order depends on the lot's orientation) for the tile with the best frontage score; the
first tile scoring ≥ 2 (else the best one) receives a random building of a random category
[INFERRED: a terminal-type building facing the road; scoring OPEN]; (2) with `rand(100) < 55` one
arrangement step, else another [OPEN]; (3) otherwise a random building of a random category of
the rule is placed at a uniformly random tile of the (cut) lot, if that tile passes the
placement check.

**Row houses `0x61F44687`** [STATIC]. Used only when `⌊(V − P) × 50 / Max⌋ ≥ 1`.
*Member lists.* When a family is bound, each member is filed by the bits of its building flag
word (attribute 0xCA) and its edge size (a member may go to several lists):

| List | Flag bit | Edge | Role |
|---|---|---|---|
| C1 | `0x1` | 2 | corner at (x0, z0) |
| C2 | `0x2` | 2 | corner at (x1−1, z1−1) |
| C3 | `0x4` | 2 | corner at (x0, z1−1) |
| C4 | `0x8` | 2 | corner at (x1−1, z0) |
| E1 | `0x20` | 2 | run along the x0 edge |
| E2 | `0x80` | 2 | run along the z1 edge |
| E3 | `0x10` | 2 | run along the z0 edge |
| E4 | `0x40` | 2 | run along the x1 edge |
| F | `0x100` | 1 | interior filler |

*Lot.* Width `W` and depth `H` of the found lot must each be **even and ≥ 6**; otherwise the rule
fails. A side longer than 8 is cut to 8 (keeping the low corner), so blocks are 6 × 6, 6 × 8,
8 × 6 or 8 × 8.
*Placement* (all buildings 2 × 2, oriented by their list; "fronted" = the tile has transport
access at distance 0, i.e. touches a road):
1. Corner C1 is placed at (x0, z0) if tiles (x0+1, z0) and (x0, z0+1) are fronted; C2 at
   (x1−1, z1−1) if (x1, z1−1) and (x1−1, z1) are fronted; C3 at (x0, z1−1) if (x0+1, z1) and
   (x0, z1−1) are fronted; C4 at (x1−1, z0) if (x1, z0+1) and (x1−1, z0) are fronted.
2. If no corner was placed the rule fails.
3. Edge runs, each a 2 × 2 building every 2 tiles strictly between the two corners of that edge:
   if C1 and C3: E1 at (x0, z) for z = z0+2, z0+4, … < z1−1; if C1 and C4: E3 at (x, z0) for
   x = x0+2, … < x1−1; if C2 and C3: E2 at (x, z1−1) for x = x0+2, … < x1−1; if C2 and C4: E4 at
   (x1−1, z) for z = z0+2, … < z1−1.
4. Only if all four corners were placed: every tile (x, z) with x0+2 ≤ x < x1−1 and
   z0+2 ≤ z < z1−1 gets a random member of F (a courtyard).
ASCII example, an 8 × 6 block with roads on all four sides (C = corner, E = edge run, f =
filler; x across, z down):

    C C E E E E C C
    C C E E E E C C
    E E f f f f E E
    E E f f f f E E
    C C E E E E C C
    C C E E E E C C

The spacing grid record `01FD7A8C` (`10-save-format.md` §6.3) is not written by this rule.

**Estates `0xE22F4A83`** [STATIC]. Used only when `⌊(V − P) × 10 / Max⌋ ≥ 1`. The lot must be at
least 4 × 4 and is cut to at most 6 × 6 (keeping the low corner).
*Member lists.* Each member is filed by its building flag word (attribute 0xCA, "f") and edge size
`e` (lists are checked in this order; a member may enter several wall/gate lists):

| Condition | List | Used at |
|---|---|---|
| `e` = 1 and f has `0x1` | corner K1 | (x0, z0) |
| `e` = 1 and f has `0x2` | corner K2 | (x1, z1) |
| `e` = 1 and f has `0x4` | corner K3 | (x0, z1) |
| `e` = 1 and f has `0x8` | corner K4 | (x1, z0) |
| otherwise, f has `0x100`: with `0x400` | lawn | interior, 95 % |
| otherwise, f has `0x100`: without `0x400` | garden feature | interior, 5 % |
| otherwise, f has none of `0xF0` and `e` < 4 | mansion | step 1 |
| `e` = 1, f has `0x20`: with `0x800` / without | wall X0 / gate X0 | side x = x0 |
| `e` = 1, f has `0x40`: with `0x800` / without | wall X1 / gate X1 | side x = x1 |
| `e` = 1, f has `0x10`: with `0x400` / without | wall Z0 / gate Z0 | side z = z0 |
| `e` = 1, f has `0x80`: with `0x400` / without | wall Z1 / gate Z1 | side z = z1 |

*Mirroring.* After filing, each pair of opposite lists (K1/K2, K3/K4, wall X0/X1, gate X0/X1,
wall Z0/Z1, gate Z0/Z1) is checked: if one list is empty it is filled with the members of the
other and marked **mirrored**; a mirrored list's pieces are placed rotated 180°, all others with
rotation 0. If both lists of a pair are empty, or the mansion, lawn or feature list is empty, the
family is not registered for the category. Note that 1 × 1 members with flag word 0 land in the
mansion list (so a "mansion" can be 1 × 1).
Test (shipped category 3500): flags 0x102 → K2; 0x108 → K4; 0x240 and 0x640 → gate X1; 0x280 and
0xA80 → gate Z1; 0x680 → wall Z1; 0xA40 → wall X1; 0xD00 → lawn; 0x100 (×2) → feature; 0x000
(×2, 1 × 1) and the 2 × 2 mansions → mansion. K1, K3, wall X0, gate X0, wall Z0 and gate Z0 are
empty and become mirrored copies of K2, K4, wall X1, gate X1, wall Z1 and gate Z1.
1. Pick a random mansion (edge `e`) and place it at
   (x0 + 1 + rand(W − e − 1), z0 + 1 + rand(H − e − 1)), i.e. anywhere inside the one-tile ring.
2. Corners (x0, z0), (x1, z1), (x0, z1), (x1, z0): each gets a corner piece only if that corner
   tile passes the rule's frontage test.
3. Walls: along x = x0 for z0 < z < z1, along z = z0 for x0 < x < x1, along z = z1 for
   x0 < x < x1, along x = x1 for z0 < z < z1 (in that order, increasing coordinate). Each wall
   tile is a wall piece, except that the first time `rand(100) < 20` on a side the tile gets that
   side's **gate** piece instead (at most one gate per side; a side may get none).
4. Every remaining interior tile (x0 < x < x1, z0 < z < z1) not covered by the mansion gets a
   garden feature with `rand(100) < 5`, else a lawn piece.
Test: 6 × 6 lot, mansion edge 3 → mansion anchor x ∈ {x0+1, x0+2}, z likewise; 20 ring tiles
(4 corners, 16 wall or gate tiles); 16 − 9 = 7 interior tiles of lawn or feature.

The per-tile spacing grid saved as record `01FD7A8C` is not touched by the row-house or estate
rules; among the developers only the seaport pier rule (below) updates it [STATIC].

**Airport and seaport pieces ("puzzle zones")**. The airport (zone 14, agent type 12310) and
seaport (zone 15, agent type 12330) are built from part occupants whose attribute records are
filed under instance `220055E1` in the occupant attribute files, with occupant IDs `0xDD`–`0xF6`,
`0x243C`–`0x243F` and `0x2B02`–`0x2B0A`; attribute 0x1F4 gives each part's index 0–9
(`13-buildings-occupants.md`). The runway/pier rules lay the linear parts, the building/yard
rules fill the remaining lot with the other parts. [DATA for the part IDs; rule-to-part mapping
INFERRED]

**Seaport yard `0xE2D3FD92`** (zone 15) [STATIC]. Parameters `a,b,c,d` (shipped 1,7,5,35):
`a` = minimum lot edge (both axes); `b` = maximum lot edge (capped at 10; a larger lot is cut to
`b × b` keeping its low-x/low-y corner); `c` is parsed but not used; `d` = percent chance of
choosing a non-filler building. Used only when `V − P > 0`. Procedure: mark every tile of the
(cut) lot that is not available for development as used; if no tile is free, fail. Then for each
tile, x-major then y: if it is free and has transport access, choose a random non-filler member
of the category when `rand(100) < d`, else a random filler member; if its footprint (edge `e`)
fits inside the cut lot from this tile, try to place it and mark its `e × e` tiles used.

**Seaport pier `0xC2D3FD7E`** (zone 15) [STATIC]. Lot rectangle [x0..x1] × [z0..z1] (inclusive).

*Member lists* (family registration). A member is used only if its edge is 1 and its flag word
(attribute 0xCA) has `0x200`. Its puzzle-part index (attribute 0x1F4) and flag bits file it as
follows. A member with both `0x400` and `0x800` goes only to the `0x400` list. A member is entered
as many times as its weight in the family (the same weighting as the other rules' random picks):

| Part | `0x400` | `0x800` |
|---|---|---|
| 7 | shore Z (Q0) | shore X (Q1) |
| 8 | pier head Z (H0) | pier head X (H1) |
| 9 | pier body X (B1) | pier body Z (B0) |

A family is registered only if B0, B1, H0 and H1 are all non-empty and at least one of Q0, Q1 is.
The rule fails if no family is registered.

*1. Choosing the shore run.* Each of the four lot sides is scanned tile by tile along the edge, in
this order: low-z side (outward direction (0, −1)), high-z side (0, +1), high-x side (+1, 0), low-x
side (−1, 0). A side tile (x, z) on the edge qualifies when the 5 tiles outward from it,
(x, z) + k·d for k = 1…5, all lie on the map, are water, and hold no occupant. In addition, a
terrain query at the 5th tile must succeed [INFERRED: a water-depth/navigability test]. The
rule keeps the longest run of consecutive qualifying tiles over all four sides. Only a strictly
longer run replaces the best one, so on ties the earlier side and the earlier run win. If the
best run is shorter than 5 tiles, nothing is built. If it is longer than 12, it is cut to its
first 12 tiles. Let L be the run length.
*2. Pieces.* Pick a family with `Rand(number of families)`. Then pick (each with `Rand(list size)`):

| Side | Shore piece (rotation) | Pier head (rotation) | Pier body |
|---|---|---|---|
| low z | Q0 (180°), or if Q0 is empty Q1 (270°) | H0 (180°) | B0 |
| high z | Q0 (0°), else Q1 (90°) | H0 (0°) | B0 |
| high x | Q1 (0°), else Q0 (90°) | H1 (0°) | B1 |
| low x | Q1 (180°), else Q0 (270°) | H1 (180°) | B1 |

The pier body uses the head's rotation. These choices are made once per rule call; every tile
uses the same shore, head and body piece.
*3. Laying.* Keep counters: gap g = 5 (tiles since the last pier), placed n = 0, piers p = 0,
and a flag "last was a pier" = false. For each run tile in order, the target is the first
outward tile T = (x, z) + d:
* if g ≥ 5, draw r = `Rand(L)`. The tile is a **pier** if r ≥ L − n − 1 + p, otherwise a shore
  tile. If g < 5 it is a shore tile;
* if T holds an occupant, or placing the chosen piece (head for a pier, shore piece otherwise) at T
  with its rotation fails, nothing changes for this tile;
* otherwise the piece is placed and n ← n + 1. If the tile is a pier, the previous placed tile was
  not a pier, and the spacing grid `01FD7A8C` accepts T [INFERRED: test-and-increment of
  the 5 × 5-block counter, `10-save-format.md` §6.3], then: for k = 2…5 a body piece is placed
  at (x, z) + k·d wherever that tile holds no occupant; g ← 0, p ← p + 1, "last was a pier" ←
  true. Otherwise (shore piece, or a head whose extension was refused, which stays as placed):
  g ← g + 1 and "last was a pier" ← false.

The rule never reports success. Correction: the previous revision left the piece order OPEN with
a single-straight-pier interim.

## 6. Land value

Land value (LV) is one byte (1–255) per 2×2-tile block. [STATIC]

### 6.1 Tunables (`SC3LandValue.ini [TuningParameters]`)

| Key | Parse | Meaning |
|---|---|---|
| `UpdatePeriod` | integer; 0 → 1 | number of update calls needed to cover the whole map once; each call processes `(mapW × mapH) / (UpdatePeriod × 4)` blocks (integer division) |
| `RadPolluteFactor` | `t,b,c` | only `b` is used: `b/1000` added on radioactive tiles (t and c forced to 0) |
| `WaterPolluteFactor`, `AirPolluteFactor`, `GarbagePolluteFactor`, `CrimeFactor` | `t,b,c` | threshold `t`, above-threshold slope `b/1000`, below-threshold slope `c/1000`; see 6.3. Storage widths: water `t` 8-bit, **air `t` 8-bit (only the low byte of the configured value is kept: shipped 400 behaves as 144)**, garbage `t` clamped 0–255, crime `t` clamped 0–255; all `t` first capped at 32767 |
| `ResModMapFactor`, `ComModMapFactor`, `IndModMapFactor`, `AllModMapFactor` | percent | loaded but not used by the land-value computation [STATIC] |
| `WaterEffect` | integer (percent points) | LV bonus at water |
| `WaterEffectRadius` | tiles | stored as `min(radius / 2, 126)` blocks (`Rw`) |
| `HillEffect` | integer (percent points) | maximum LV bonus for elevation |
| `HomelessShelterOrdEffect` | percent | added as `/100` while the ordinance with ID `0x20D07175` is enacted |
| `ResCityCenterEffect`, `ComCityCenterEffect`, `IndCityCenterEffect` | curve, 6.2 | |

Shipped default when a key is missing: `UpdatePeriod` 180, city-centre radius percent 100.
[STATIC]

### 6.2 City-centre effect curve syntax

`E+Rp x0:y0 x1:y1 …` (space-separated tokens):

* `E` (before `+`): edge value — the base LV at and beyond the class's radius.
* `Rp` (after `+`): the radius as a percentage of `(mapW + mapH) / 2` (tiles) [INFERRED that the
  two quantities are the map dimensions].
* Each `x:y`: city population `x × 1000` maps to the class's centre value `y` (0–255).

Centre value `C` for population P: find consecutive points with `x_i·1000 ≤ P < x_{i+1}·1000` and
take `C = y_i + (P − x_i·1000) × |y_{i+1} − y_i| / ((x_{i+1} − x_i)·1000)` (integer division;
the absolute value means a decreasing segment still increases — original behaviour). Beyond the
last point `C` = last `y`; with fewer than two points `C` = first `y`. [STATIC] The same
interpolation is used for `EconomicPhases` (§3.5).

### 6.3 Per-block computation

**Scan order.** Blocks are visited at tile coordinates stepping by 8 in x then 8 in y, starting
from a phase offset; there are 16 phases with tile offsets (x, y): (4,4), (0,6), (2,0), (4,6),
(6,4), (0,0), (2,2), (6,2), (2,6), (0,2), (6,0), (2,4), (4,2), (6,6), (4,0), (0,4). When y passes
the map edge the phase advances; after phase 15 it wraps to 0 and the **cycle-start
recomputation** runs. [STATIC]

**Cycle-start recomputation** (once per complete 16-phase pass):
1. Centre values `C_R, C_C, C_I` from the three curves (§6.2) at population `P` = `Slast` of
   the residents valve 3000 (§3.2), i.e. the residential supply of the last completed valve
   cycle [STATIC]. This is the same number as the city population of the population chart
   (`20-sim-population.md` §2; DATA: the newest population-chart sample equals 3000 `Slast` in
   13/13 played cities). **There is no factor 2**: the curves are read at `P`, not `2P`, and
   there is no per-lot, density-class or unit conversion between `P` and the curve keys
   (`x × 1000`). A fit that needs `2P` is compensating for another term; the most likely ones are
   listed under "Calibration notes" below.
   Test vector (shipped `ResCityCenterEffect=45+80 … 100:61 150:67 …`): `P` = 148 844 →
   `C_R` = 61 + (148 844 − 100 000) × |67 − 61| / 50 000 = 61 + 5 = 66 (integer division).
2. Agent-modifier maps (§6.4) rebuilt.
3. `prevSum := sum`; city average LV `avgLV := max(sum, 0) / count` as a byte (0 when `count`
   = 0) is stored for other subsystems; `sum` and `count` are reset to 0. [STATIC]
4. The hill reference height `Href` (one byte) is refreshed from the terrain at every cycle
   start [STATIC]: over **all** (W+1) × (H+1) terrain vertices whose altitude is **above** the
   vertex's water-surface altitude (dry vertices, `11-world-terrain.md` Persisted state), sum
   (altitude − sea level) and divide by the number of such vertices (integer division, low 8 bits
   kept); `Href` = 0 when no vertex is dry. It is therefore the mean height of dry land above the
   sea level, in altitude units. Shipped values (DATA): Madison, Moscow and Sacramento 1, Berlin and
   Roadless Paradise 2, Madrid and Liverpool 3, London, Seoul and Europolis 4, Frankfurt 5,
   Farmsville 6, Mount Herrang 7, Craterville 75.
   Test vector: a 2 × 2-tile map (9 vertices), sea level 30, altitudes 30 30 30 / 31 32 33 /
   30 40 30 with every water surface at 30: dry vertices are 31, 32, 33, 40 → (1+2+3+10)/4 = 4.

**Per block** at tile origin (x, y), with `Rw` from 6.1:

1. *Water bonus* `Wb`: let `dist` = Chebyshev distance in blocks from this block to the nearest
   block containing water, computed by `Rw` passes of 3×3 dilation (so only distances 0…`Rw`
   are distinguished). If `dist ≤ Rw`: `Wb = (Rw − dist + 1) × WaterEffect / (Rw + 1)`
   (integer), else 0. [STATIC]
   *Water test* [STATIC]: a block contains water when any of its four tiles is a **water tile**,
   and a tile is a water tile when, at **any** of its four corner vertices, the vertex's
   water-surface altitude is **greater than or equal to** its ground altitude
   (`11-world-terrain.md` Persisted state). Dry land lying exactly at the sea level, whose water
   surface equals the ground, therefore counts as water. On the low, flat shipped maps (sea level
   30, much land at 30) large inland areas get the full `WaterEffect`. The distance map is rebuilt
   at every cycle start. [DATA: with this test the recomputed land value of Madison matches the
   saved byte within 5 % for 95 % of unclassed blocks (sample of 942), up from 70 % with a strict
   "surface above ground" test]
   Test vector (Madison, block at tile (126, 124), shipped INI): zones 0/0/22/22 → class C;
   `C_C` = 84, `dc` = 16, `rC` = 153 → base = 84 + round(16 × (45 − 84) / 153) = 80, +1 = 81; the block's own vertices sit at
   the sea level → `dist` = 0, `Wb` = 50, `h` = 0 → `W` = 0.5, `B` = round(81 × 1.5) = 122;
   crime 0 → `q` = 0.2; air 115, water pollution 52 → `p` = 0.098; C modifier 83–86 → `m` ≈ 0.85;
   `T` = round(2.15 × 122) ≈ 262 per tile → stored 255 (saved 255). An unclassed empty block at
   (118, 116) gives `B` = round(1 × 1.5) = 2, `T` = round(1.62 × 2) = 3 (saved 3).
2. *Hill bonus* `Hb`: `h` = (altitude of terrain vertex (x, y), the block's origin vertex) −
   (sea level), computed **modulo 256** (an 8-bit difference). If `h > Href`:
   `Hb = HillEffect × min(h, 8·Href) / (8·Href)` (with `8·Href` replaced by 1 when `Href = 0`;
   integer division), else 0. Note the original uses `h`, not `h − Href`. [STATIC]
   Defect to reproduce: a vertex **below** the sea level gives `h` = 256 − depth (≥ 226 in
   practice), so underwater and shore blocks get the full `HillEffect` (shipped 30) on top of
   the water bonus.
   Test vectors (`HillEffect` 30, `Href` 1, so `8·Href` = 8): `h` = 1 → 0; `h` = 2 → 7;
   `h` = 4 → 15; `h` ≥ 8 → 30; altitude 29 with sea level 30 → `h` = 255 → 30.
3. `W = (Wb + Hb) / 100` (float).
4. *Block class for unzoned tiles*: commercial if any of the 4 tiles is commercial-class, else
   residential if any is residential-class, else industrial if any is industrial-class, else none.
5. *Agent modifiers*: read the four modifier maps (All, R, C, I) at the block's origin tile
   (x, y), i.e. at map cell (⌊x/4⌋, ⌊y/4⌋) (§6.4), each value clamped to [−150, 150]
   [STATIC for the read at the origin tile; INFERRED for the tile-to-cell division].
6. For each of the 4 tiles:
   a. Class = zone class of the tile (§1), or the block class if unzoned.
   b. Distance to the city centre `(cx, cy)`, in tiles, fetched once per update call [STATIC].
      The centre is **not** the view and not the map centre: it is the **area-weighted centroid
      of the city's buildings**, kept up to date by the occupant manager as buildings are added
      and removed [STATIC]:
      * Start (new or loaded city): `sumX = sumZ = n = 0`, centre = (⌊mapW/2⌋, ⌊mapH/2⌋).
      * Each time a lot-bearing occupant is added (every building with a lot state, in any
        state; network pieces, props and flora are not counted), with footprint
        [x0..x1] × [z0..z1], `dx = x1 − x0`, `dz = z1 − z0`, `a = (dx+1)(dz+1)`:
        `sumX += (⌊dx/2⌋ + x0) × a`, and if `dx` is even also `sumX += ⌊dx × (dz+1) / 2⌋`;
        `sumZ` likewise with the axes swapped; `n += a`. Then `cx = ⌊sumX / n⌋`, `cz = ⌊sumZ / n⌋`.
        (The extra term is an original rounding quirk: a 3 × 3 lot at x0 = 10 adds 9·11 + 3 = 102,
        not 99.)
      * When such an occupant is removed, the same amounts are subtracted and the centre
        recomputed; if `n` reaches 0 the centre returns to the map centre.
      The player influences the centre only by where buildings stand. Because the sums are not
      saved, a loaded city rebuilds them from its occupants in load order (the result does not
      depend on the order). DATA: the decoded centroid of the shipped cities is (139, 131)
      Madison, (126, 130) Moscow, (125, 138) Berlin, (125, 124) Madrid, (126, 134) London,
      (82, 105) Farmsville (192-tile map), (108, 119) Seoul, (130, 145) Sacramento: 1–20 tiles from
      the map centre in every played city, which is consistent with the earlier offline fit.
      `dx = |cx − tx|`, `dy = |cy − ty|`, `dc = max(dx, dy) + min(dx, dy) / 2` (integer).
   c. Base: for class K ∈ {R, C, I} with radius `rK = max(1, RpK × ((mapW + mapH)/2) / 100)`:
      `base = E_K` if `dc > rK`, else `base = C_K + round(dc × (E_K − C_K) / rK)` (float slope,
      round-to-nearest); then `base := base + 1`. Modifier `m = mapK / 100`.
      For tiles of no class: `base = 1` and `m = mapAll / 100`.
   d. Pollution/crime sum `p` (floats). Sign convention: the terms are added exactly as
      computed, with the INI signs. A positive `c` raises LV below the threshold and a negative
      `b` lowers it above the threshold. With the shipped values (`c` ≥ 0, `b` < 0), clean tiles
      gain LV and polluted or high-crime tiles lose it [DATA, shipped `SC3LandValue.ini`]:
      * radioactive tile: `p += RadB`;
      * air pollution `v` (0–32767): if `v < tA`: `p += (tA − v) × airC / tA`; else if `v > tA`:
        `p += (v − tA) × airB / (32767 − tA)`;
      * water pollution `v`: if `v < tW`: `p += (tW − v) × waterC / tW`; else if `v > tA`
        (**the air threshold** — original defect): `p += (v − tW) × waterB / (32767 − tW)`;
      * garbage `v`: same form with `tG`, `garbageB`, `garbageC`;
      * crime `v` (0–255): `q = (tCr − v) × crimeC / tCr` if `v < tCr`, `q = (v − tCr) × crimeB /
        (255 − tCr)` if `v > tCr`, else 0.
      Here `xB = b/1000`, `xC = c/1000` of the corresponding INI triple. Sources of the input
      layers: `17-sim-services.md` (pollution, crime), `16-sim-utilities.md` (garbage).
   e. `s = HomelessShelterOrdEffect / 100` if that ordinance is enacted, else 0.
   f. `B = round(base × (1 + W))`; tile value `T = round((1 + q + p + m + s) × B)`.
   g. If the tile has a class, add `T` to the city LV sum and count.
   Note [OBSERVED]: some shipped files hold high values on blocks whose four tiles are all zone 0
   (Moscow 256 such blocks above 10, e.g. tile (114, 102) at 255). These bytes are stale: in a run
   of the original from the shipped Moscow file they are 1 by day 71, and none remain after the
   first full pass (`28-conformance.md` §5.1). Recompute them by the rule; no interim is needed.
7. Block LV `= clamp(ΣT / 4, 1, 255)` (integer division; values ≤ 0 become 1).

All "round" operations are round-half-to-even float-to-int conversions. [STATIC]

Clamps and rounding between the computed value and the stored byte [STATIC]: modifier map values
clamped to [−150, 150] before `/100`; `base` rounded (half to even) before `+1`; `B` and `T`
rounded (half to even); the block value is `ΣT` over **all four** tiles (classed or not) divided
by 4 with truncation toward zero, then values < 1 become 1 and values > 255 become 255. There is
no smoothing, averaging with the previous value or rate limit: each visit overwrites the byte.

**Calibration notes for implementations whose recomputed LV is low near dense centres.** The
original has no "2 × population" rule. A recomputation that matches only at 2P is most likely
missing one of these terms:
0. The water test of step 1 (surface **≥** ground at any corner). This was the dominant missing
   term in the dense centres of the shipped cities: the centres of Madison, Moscow and Madrid lie
   on sea-level flats and get `W` = 0.5 from it.
1. `Href` too large (e.g. using the mean altitude instead of the mean dry height above sea
   level). With the shipped maps `Href` is 1–7, so almost every dry block above the sea gets a
   hill bonus of up to `HillEffect` (30 percentage points of `base`).
2. The underwater wrap of `h` (step 2): shore blocks get the full hill bonus.
3. Modifier maps built from buildings only. §6.4 counts **every** occupied or stateless occupant
   with an entry, including network pieces (`16000` roads +1 R/+3 C/+2 I per tile, `16001`
   highways, `12103` power lines) and flora (`300`, +5 R/+5 C/+2 I/+4 A per tree tile), each
   summed per tile into 4 × 4-tile cells, so a cell full of trees holds +80 R before smoothing
   [INFERRED, medium-high, that network pieces and flora are iterated: the shipped table lists
   them, and the iteration covers the city's whole static occupant set].
4. Air threshold 144 (not 400) and the water-threshold defect of step 6d.
Test vector for one tile (no modifiers, no pollution, crime 0, no ordinance), Madison-like:
`C_R` = 66, `E_R` = 45, `rR` = 0.80 × 256 = 204, `dc` = 51 → base = 66 + round(51 × (−21)/204) =
66 − 5 = 61, +1 = 62; `Wb` = 0, `h` = 2 with `Href` = 1 → `Hb` = 7, `W` = 0.07 → `B` =
round(62 × 1.07) = 66; crime `q` = (25 − 0) × 0.2 / 25 = 0.2; air 0 → `p` = 0.25, water 0 → +0.1,
garbage 0 → 0 (threshold 0): `T` = round((1 + 0.2 + 0.35) × 66) = round(102.3) = 102. Four such
tiles give a stored byte of 102.

### 6.4 Agent ID modifiers (`[AgentIDModifiers]`)

Syntax: `agentType=n:L,n:L,…` (text after the last item following whitespace is a comment).
Each `n` is clamped to [−127, 127]; `L` is `R`, `C`, `I` or `A` (case-insensitive) selecting the
residential, commercial, industrial or all-classes modifier; repeated letters add (8-bit wrap).
[STATIC]

Map construction (at each cycle start): [STATIC]
1. Clear the four maps (All, R, C, I; signed 16-bit cells, one cell per 4×4 tiles, so a map
   has (mapW/4) × (mapH/4) cells [INFERRED: the smoothing of step 3 steps 4 tiles from cell to
   cell]).
2. For every occupant in the city's static occupant set (buildings, network pieces and flora)
   that is occupied (lot state 1 or 513) or has no lot state, **each occupant exactly once**
   [STATIC: the iteration walks the map and returns an occupant only at one tile of its
   footprint; DATA: including network pieces and flora improves the recomputed grid in every
   played city]: find its
   modifier entry — the entry for its exact agent type, else the first entry (in ascending
   agent-type order) whose type is an ancestor of it in `CitySim.ini [AgentTypeHierarchy]`. For
   every tile of its footprint add `A` to the All map, `R` to the R map, `C` to the C map, `I` to
   the I map (at the cell containing that tile).
3. Smooth twice: for each cell in order (outer loop over x, inner over y, in place), replace it
   by the integer average of the up-to-9 cells at offsets −1, 0, +1 cells that lie inside the map
   (the sum is truncated to signed 16 bits before dividing).

The "radius" of an agent's effect is therefore its footprint plus the spread from two 3×3
smoothing passes at 4-tile resolution (about ±8 tiles).

## 7. Landfill developer

Zone 17 (landfill) is handled by developer class `0x82348DE5` (§2), which has no building
families; it places landfill pieces (occupants 10006–10015, agent type 12850) using tiling rules.
[DATA]/[INFERRED]

* **Tiling rules.** `Res/TilingRules/LandfillStartRule.txt` and `LandfillRules1.txt` …
  `LandfillRules6.txt` (loaded in that numeric order; loading fails as a whole if any of the six
  is missing). They use the network tiling-rule grammar of `12-networks.md`. [STATIC]
* **Rule-set selection** [STATIC]. The six sets are not tied to fill levels; they are a priority
  list. After a landfill visit has changed tiles, each changed tile's neighbourhood is matched
  against `LandfillRules1` … `LandfillRules6` in that order. The first set that matches **and**
  yields at least one replacement piece is applied. If a set matches but yields no piece, the
  search stops there; in that case, or when no set matches, `LandfillStartRule` is tried as the
  fallback and applied if it matches with at least one piece. (Fill level enters only through
  the neighbourhood the rules test.)
* **Landfill store** [STATIC; DATA]. Landfill capacity is kept in a valve-like store with ID
  `0xA2C9E821` (the 37-byte `garbageStore` block of the pollution record, `17-sim-services.md`
  §7.4: the valve layout of this document without the ID). Its fields mean:
  * `D` (demand, `Dlast` = `Dacc`) = **spare room** = Σ over registered landfill tiles of
    (32 767 − g). A tile is registered when it is zone 17 and the pollution layer has set its
    landfill flag (`17-sim-services.md` §7.2.5): registering adds (32 767 − g), unregistering
    subtracts it; a non-landfill tile that still carries the flag loses it and 32 767 is
    subtracted. DATA: Berlin 720 registered tiles holding 10 048 989 → `D` = 23 592 240 −
    10 048 989 = 13 543 251 (saved 13 543 251); Mount Herrang 16 tiles, 357 427 → 166 845 (saved
    166 845).
  * `S` (supply, `Slast` = `Sacc`) = garbage **routed to landfill but not yet placed** on tiles
    (stage 3 of `16-sim-utilities.md` §4.3 adds to it).
  * `V` = available room `D − S` as of the last monthly valve update (it lags the live values;
    Berlin saved `V` 13 561 864). Negative `V` means a backlog of routed garbage larger than the
    room.
* **Filling** [STATIC]. Each landfill tile holds a garbage amount `g` (0–32 767; it is the
  standing-garbage word of the tile, `17-sim-services.md` §7.2.1); its free capacity is
  `32 767 − g`. On a visit to a landfill tile the developer (1) applies decay (below), (2) updates
  the tile's registration, then (3) while the store's `S` > 0 walks its list of active landfill
  tiles in list order: for each registered tile `amt = min(S, 32 767 − g)`; `g += amt`; the
  store's `S` and `D` both decrease by `amt`; the landfill counter (slot [6] of the garbage
  statistics, `16-sim-utilities.md` §4.5) increases by `amt`; a tile without a landfill piece (or
  whose piece has flag word bit `0x400`) receives the first landfill piece (occupant ID 10006, key
  `207EDC0E:0000057E:00002716` in the attribute-file key order of `13-buildings-occupants.md`). When
  the list has no active tile and the visited tile qualifies, it is added to the list. [OPEN] The
  order in which further tiles join the list. Recommended until answered: the visited tile is
  appended (tiles join in the order the zone manager's shuffled pass visits them, §4 "Scan
  order").
* **Decay.** `SC3Tune.ini [LandfillZoneDeveloper] LandfillDecayRate` (per 10 000, capped at
  10 000; shipped 490 ≈ 4.9 %). On each visit to a landfill tile holding `g > 0`:
  `d = max(1, round(g × LandfillDecayRate / 10000))`; `g := g − d`. When `g` reaches 0 the tile's
  landfill piece state is cleared and it is removed from the active list. If the tile is
  registered, `d` is credited back as room: the store's `D += d`. [STATIC]
  Test vector: `g` = 20 000 → `d` = 980, `g` = 19 020, store `D` += 980; `g` = 10 → `d` = 1
  (round(0.49) = 0, raised to 1).
* **Default behaviour of the three developers without a category list** [STATIC]. Zones 14, 15
  and 17 have descriptions without categories (`14=0xc1f81e7e,3000`, `15=0x82d2d72b,0`,
  `17=0x82348de5,0`); their developers do not use §4.3 or the category rules:
  * airport (14) [STATIC]: per visited tile, with `access` = a transport piece within **6** tiles
    of the tile (§4.2 box with `d` = 6), `powered` (the power layer's powered bit at the visited
    tile; zoned land conducts, `12-networks.md` §9.1), `blocked` (radioactive, from the
    pollution layer) and S = the tile has **water service** (the water layer's service bit)
    [STATIC; correction: S was previously INFERRED]:
    1. If an airport part stands on the tile and the tile is its anchor: if its state is 2 or 4
       and S, `powered`, `access` and not `blocked` all hold, it becomes occupied (state 1). No
       result code.
    2. Empty tile: `blocked` → code 10. If S: not `powered` → 14; else if `access`: run the
       airport rules (§5.5) with a **fixed demand of 2000 of 2000**, so their `V − P > 0` test
       always passes and valve 5001 does not gate airport growth; nothing placed → code 9,
       otherwise no code. Without `access` → 16. If not S: not `powered` → 14, not `access` → 16,
       else 15.
  * seaport (15): demand valve 8001, rules pier and yard of §5.5, same conditions;
  * landfill (17): no valve demand test; every visit runs decay, registration and filling above.
  Result code 9 comes from the airport developer and from the landfill developer (below); 17
  comes from the seaport developer (§4.3 table); 8 is never produced [INFERRED].
  **Landfill tile test** [STATIC]: on each visit to a landfill tile the developer first checks
  for a transport piece within **10** tiles and then routes a **tile-origin trip** from the visited
  tile (`18-sim-transport.md` §9.2: destination "any", uncounted, trip-length factor = map size,
  cars and rail allowed); if either fails it reports code 16. The trip writes the tile's trip
  result bits, so empty landfill-zoned tiles also carry results. DATA: Berlin's saved passes
  report no code 16 on zone 17, and all of its landfill tiles (including the empty zoned ones)
  carry "tried, succeeded" in every save of a 480-day run. Then the tile qualifies for registration only if it is one corner of a
  2 × 2 square of landfill-zoned (17) tiles, each with a transport piece within 10 tiles, in
  any of the four orientations. Otherwise it reports code 9 and is not registered. DATA:
  Seoul's saved pass has code 9 on 2 airport tiles and 2 landfill tiles (total 4).
  **Which tiles give code 9** [DATA]: in every shipped city with an airport zone, the code-9
  count of both saved passes equals the number of zone-14 tiles not covered by any occupant's
  lot. Madrid: 825 zone-14 tiles, 128 uncovered. Berlin: 453, 80. London: 189, 112.
  Sacramento: 98, 28. Seoul: 98, 2, at tiles (154, 5) and (154, 6); the other 96 tiles are
  covered by occupants of type 12310 in state 1. All uncovered tiles are watered and none
  holds a network piece. These are leftover gaps inside built airports that no airport rule
  can fill, so the count stays the same from pass to pass (the original's runs show 128 in
  all 16 Madrid passes). An implementation that reports more than this count treats some
  covered tiles as empty.
  **Airport lot and rule calls** [STATIC]: the lot handed to the airport rules comes from the
  lot finding of §5.3 step 1, seeded at the visited tile, with transport distance 6, minimum
  land value 0 and the power test active. Up to `min(5, rules bound to zone 14)` distinct
  rules are tried in random order; the R/C/I developers try at most 4.
  **The gaps are not small** [DATA]: the uncovered tiles form 4-connected regions of up to 112
  tiles (London: one region in a 7 × 16 box; Madrid: one 64-tile region in a 9 × 8 box; Berlin:
  one 62-tile region in a 7 × 14 box). All are flat and dry. Even so, in 480-day runs of the
  original no airport part was added in any city: Madrid has 640 parts, London 74 and Berlin
  334, at the start and at the end. Why the runway and airport-building rules place nothing on
  these lots is [OPEN]. The placement check inside the rules is not decoded. Recommended until
  answered: for a loaded city, an airport rule call on a lot inside an existing airport places
  nothing (code 9). A test of a newly zoned airport was **inconclusive** [OBSERVED]: 36
  airport-zone tiles beside a road and power plants, with no water service. They gave code 15
  (no water service) on 29 tiles and code 16 (no transport within 6) on 7, so the rules were
  never called; no part was placed in 18 game months. (Correction: an earlier revision
  reported code 9 here, from a per-zone counter rather than the per-code results.) Remaining
  test: a flat airport zone of at least 10 × 10 with road, power **and water service**,
  counting its parts after a few months.

## Persisted state

All records live in the city's inner database (`10-save-format.md` §4) and start with the
standard 8-byte versioned header (u16 version, u16 flags, u32 `0xDEADBEEF`).

### Valve layers

| Record key | Version | Valves |
|---|---|---|
| `206C6E7C:C106C4F5:00000000` | 3 | 3000, 3001, then residential-layer data (owned by `20-sim-population.md`) |
| `206C6E7C:20EC9849:00000000` | 2 | 5000, 5001 |
| `206C6E7C:A106CF3D:00000000` | 2 | 8000, 8001 |

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version as above, flags 2 |
| 0x08 | 4 | u32 | n | number of valves (2) |
| 0x0C | 41·n | valve[] | valves | layout below |

Valve (41 bytes, packed):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | id | valve ID |
| 0x04 | 4 | s32 | Slast | §3.2 |
| 0x08 | 4 | s32 | Dlast | |
| 0x0C | 4 | s32 | Sacc | |
| 0x10 | 4 | s32 | Dacc | |
| 0x14 | 4 | f32 | f1 | world cycle modifier |
| 0x18 | 4 | f32 | f2 | tax modifier |
| 0x1C | 1 | u8 | capPct | |
| 0x1D | 4 | s32 | V | current valve value |
| 0x21 | 4 | s32 | Sbase | e.g. 25 000 for 3001/5001, 70 000 for 8001 |
| 0x25 | 4 | s32 | Dbase | |

[DATA] All 42 valve records in the 14 shipped `.sc3` cities have this layout; commercial and
industrial records are exactly 94 bytes.

### World layer `206C6E7C:E11BDDF6:00000000`

The world layer holds the jobs valve 7000 (§3.4), the economic phases and the world valve
mapping (§3.5). This document owns the record. Its byte layout, as far as it is decoded, is in
`10-save-format.md` §6.13 (725 bytes in every shipped file).

### Land value layer `206C6E7C:C0AB8A88:00000000`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | hdr | header | version 1, flags 2 |
| 0x08 | (W/2)² | u8[] | lv | land value per 2×2 block, row-major; W = map width in tiles (128, 192 or 256, `11-world-terrain.md` §1), so the block grid is 64×64, 96×96 or 128×128 |
| +0 | 1 | u8 | phase | current scan phase 0–15 (§6.3) |
| +1 | 4 | u32 | scanX | next block tile X |
| +5 | 4 | u32 | scanY | next block tile Y |
| +9 | 1 | u8 | avgLV | city average LV of the last complete pass |
| +10 | 4 | s32 | prevSum | LV sum of the previous complete pass (copied from `sum` at each cycle start) [STATIC] |
| +14 | 4 | s32 | sum | LV sum of classed tiles accumulated in the current pass |
| +18 | 4 | u32 | count | number of classed tiles accumulated in the current pass |
| +22 | 128 | u32[32] | history | average-LV history, most recent first [INFERRED] |
| +150 | 4 | u32 | (unknown) | 260 in all shipped files [OPEN] |

[DATA] Layout verified on 14/14 cities (sizes 9378 and 16546 bytes, i.e. 192- and 256-tile maps).

## Open questions

* Zone-manager position within the day (§4 "Rate"): after the daily area work is inferred.
* `unfit` = failed trip (§4.2) is inferred; confirm against the original.
* Source of the extra demand on valves 5001 and 8001 (§3.3, "Amenity demand of commerce and
  industry"): saved `Dlast` is 1.3–2.1 × the building sum. Interim: carry the saved ratio.
* Non-scatter rules (§5.5): the airport
  buildings arrangement step (2), and the exact flag bits of the estate member lists. Housing
  projects A/B/C, row houses, estates and the runway sequence are now specified.
* What sets the clean-industry flag (§4.5 step 3).
* Whether network pieces and flora are part of the modifier-map iteration (§6.4 step 2; the
  clean-side fit supports it).
* Occupant attributes 0x72 bits, 0xC9, 0x7B and the CSATTRIB trailer (`13-buildings-occupants.md`):
  growth does not use them; their meaning is unknown.
* Landfill: the order in which new tiles join the active list (§7).

Closed in this revision: `capPct` and the amenity feedback (§3.2 cap rule, [STATIC]); valve
smoothing (§3.2, [STATIC]); the occupied-lot land-value test (§4.3 step 4: `minLV/2`, [STATIC]);
the workforce fraction (§3.4, [STATIC]); the industrial EQ input (§4.5, Q(20, 65), [STATIC]);
the LV population (§6.3, valve 3000 `Slast`, [STATIC]); the city centre (§6.3 step 6b, building
centroid, [STATIC]); `Href` (§6.3, [STATIC]); the landfill store (§7, [STATIC]); result codes 8,
9 and 17 (never produced, [INFERRED]).
