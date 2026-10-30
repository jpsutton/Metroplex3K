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
| 22 | (special, commercial-class) | — |

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
INI key names. [OPEN] How their values feed back into R/C/I growth: no rule in this document
reads them. Question for testing against the original: does a shortage on 3001/5001/8001 raise
the `capPct` of valve 3000/5000/8000 (§3.2)? Until answered, implement 3001/5001/8001 as
bookkeeping valves with no effect on development.

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

`capPct` is non-zero only on the R/C/I valves (shipped saves: 11–56). [OPEN] Which rule sets
`capPct`. The value is saved, so a loaded city keeps it, but a new city starts at 0. Question for
testing: how does `capPct` of valve 3000/5000/8000 change month by month, and does it follow the
shortage on 3001/5001/8001? Recommended until answered: keep the saved value unchanged.

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

### 3.3 Contributions from buildings and connections

* **Buildings.** Every occupant whose agent type has an `[AgentValveEffects]` entry contributes,
  each cycle, while it is in the normal (occupied, functioning) state: for each
  `valve:supply:demand` item, `Sacc += tiles × supply` and `Dacc += tiles × demand` of that valve,
  where `tiles` = lot X × lot Y (attribute 0x66 of `13-buildings-occupants.md`). The values in the
  INI are therefore **per tile**. Buildings in the abandoned, construction or damaged states
  contribute nothing. [STATIC]
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
* **Ordinances.** `[OrdinanceEffects] 0xOrdinanceID=valve,f+valve,f…` (f in thousandths): while
  the ordinance is enacted, each cycle the listed valve's `Dacc` is increased by
  `trunc(Dlast × f/1000)` (sign taken so that a negative `f` reduces demand when `Dlast` is
  positive). [STATIC]

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
  `w` is a floating-point workforce fraction provided by the residential population layer
  [STATIC]. [OPEN] The formula of `w`; neither this document nor `20-sim-population.md` defines
  it. Question for testing: is `w` the employable share of the cohorts (for example the share of
  the population of working age in `20-sim-population.md`)? Recommended until answered: `w` =
  population of working age / total population, 0 when the population is 0.
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

* `powered` (power layer), `watered` (water layer), `LV` (land value of the tile, §6), `access`
  (a transport tile within `MaxDistFromTransport`, see `12-networks.md`).
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
| 8, 9 | never produced by the R/C/I developers [STATIC]; airport, seaport and landfill developers [OPEN] | – |
| 10 | not developed: tile radioactive | R, C, I |
| 11 | not developed: land value below every category | R, C, I |
| 12 | not developed: land value above every category (I: also no category on a watered tile, defect §4.3.2) | R, C, I |
| 13 | not developed: negative demand | R, C |
| 14 | not developed: no power | R, C, I |
| 15 | not developed: no category fits (unwatered) | R, C, I |
| 16 | not developed: no transport access | R, C, I |
| 17 | never produced by the R/C/I developers [STATIC]; other developers [OPEN] | – |
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

**Occupied (state 1).**
1. If `blocked`: abandon [1].
2. If not `powered`, not `access`, `unfit`, or (not `watered` and density level ≥ 2): with
   chance `rand(100) < NoServicePercentage / tiles` (integer division) abandon
   [4 if unpowered, 5 if only water is missing on a dense lot, 6 otherwise].
3. Else if `V < 0` and `rand(−Min) < −((V − P) / tiles)`: abandon [3].
4. Else, if the category's `maxLV / 2 ≤ LV` (land value still adequate), with "flagged" meaning
   the lot is flagged `0x200` (above): [STATIC]
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
5. Else (land value fell below half of the category's `maxLV`): look for a lower-land-value
   category (rule L); if found and `rand(100) < LowerLandValueRedevelopmentPct` redevelop, else
   abandon [2].

`rand(n)` is the simulation RNG returning an integer in `[0, n)` (`14-sim-clock.md`).

Defect (all three R/C/I developers): the lower-land-value search (rule L) never returns a
category, so a lot whose land value drops below half its category's `maxLV` is always abandoned
in step 5 (farm fields: removed, §4.5). `LowerLandValueRedevelopmentPct` therefore has no effect.
[STATIC for each of residential, commercial and industrial]

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
   * `e = max(EQ, 85)` where EQ is the city-wide education quotient reported by the residential
     population layer (`20-sim-population.md` §4; EQ values are capped at 150, so `e` lies in
     85–150 and the next line maps it to 0–100 %) [STATIC that the value comes from that layer;
     INFERRED that it is the city average EQ];
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
nothing is built. Layouts B and C: [OPEN] (other arrangements of the same building).

**Farm `0xA2C1EDA3`** [STATIC]. Parameters: `a` is raised to at least 4, `b` to at least `a`, both
capped at 22; `c` and `d` capped at 32767.
* `a` = minimum lot edge: the lot rectangle must be at least `a × a`.
* `b` is parsed but **not used** by the placement.
* `c` = maximum air pollution and `d` = maximum water pollution (same 0–32767 scales as
  `17-sim-services.md`) allowed on the lot's border tiles (shipped 450 and 450).
Procedure:
1. Surroundings: every in-map tile of the one-tile ring just outside the lot must have zone type
   0 (none), 1 (low residential), 5 (low commercial), 9 (low industrial) or 22; otherwise the
   rule fails.
2. Edges: for each of the four lot edges, walk its tiles excluding the two corner tiles; any tile
   whose air pollution exceeds `c` or water pollution exceeds `d` fails the rule; count the tiles
   with transport access. An edge with at least one such tile is a frontage edge.
3. The farmstead is placed in a strip against a frontage edge: 3–8 tiles along the edge and 3–4
   deep (sizes and the position along the edge are random within the lot), using a building from
   the category's large list; the remaining lot tiles are covered with field pieces (the
   `0x100`+`0x400` filler members, §4.3.2) [INFERRED from structure; exact field tiling OPEN].

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
that side [piece sequence OPEN].

**Airport buildings `0x2207D9EE`** (zone 14) [STATIC]. Used only when `V − P > 0`. The lot is
cut to at most 10 × 10 (keeping its low-x/low-y corner). Attempts, in order, until one succeeds:
(1) the edge tiles of the lot are scanned (low-x column, then high-y row, high-x column, low-y
row; the order depends on the lot's orientation) for the tile with the best frontage score; the
first tile scoring ≥ 2 (else the best one) receives a random building of a random category
[INFERRED: a terminal-type building facing the road; scoring OPEN]; (2) with `rand(100) < 55` one
arrangement step, else another [OPEN]; (3) otherwise a random building of a random category of
the rule is placed at a uniformly random tile of the (cut) lot, if that tile passes the
placement check.

**Estates `0xE22F4A83` and row houses `0x61F44687`**: [OPEN] placement geometry. Row houses
(and possibly estates) place buildings in runs along a line; the per-tile spacing grid saved as
record `01FD7A8C` (`10-save-format.md` §6.3) is the zone developer's record of such runs
[INFERRED; which of the two rules writes it is not confirmed].

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

**Seaport pier `0xC2D3FD7E`** (zone 15): [OPEN] (lays pier pieces into adjacent water).

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
1. Centre values `C_R, C_C, C_I` from the three curves at the current city population.
2. Agent-modifier maps (§6.4) rebuilt.
3. `prevSum := sum`; city average LV `avgLV := max(sum, 0) / count` as a byte (0 when `count`
   = 0) is stored for other subsystems; `sum` and `count` are reset to 0. [STATIC]
4. The hill reference height `Href` (one byte) is refreshed from the terrain at every cycle start
   [STATIC]. [OPEN] Which terrain statistic `Href` is; `11-world-terrain.md` defines no such value.
   Question for testing: is it the sea (water) level or the mean land height above it?
   Recommended until answered: the mean height above the water level of all land tiles.

**Per block** at tile origin (x, y), with `Rw` from 6.1:

1. *Water bonus* `Wb`: let `dist` = Chebyshev distance in blocks from this block to the nearest
   block containing water, computed by `Rw` passes of 3×3 dilation (so only distances 0…`Rw`
   are distinguished). If `dist ≤ Rw`: `Wb = (Rw − dist + 1) × WaterEffect / (Rw + 1)`
   (integer), else 0. [STATIC]
2. *Hill bonus* `Hb`: `h` = terrain height at the block minus the terrain's base (water) level.
   If `h > Href`: `Hb = HillEffect × min(h, 8·Href) / (8·Href)` (with `8·Href` replaced by 1 when
   `Href = 0`; integer division), else 0. Note the original uses `h`, not `h − Href`.
3. `W = (Wb + Hb) / 100` (float).
4. *Block class for unzoned tiles*: commercial if any of the 4 tiles is commercial-class, else
   residential if any is residential-class, else industrial if any is industrial-class, else none.
5. *Agent modifiers*: read the four modifier maps (All, R, C, I) at the block's origin tile
   (x, y), i.e. at map cell (⌊x/4⌋, ⌊y/4⌋) (§6.4), each value clamped to [−150, 150]
   [STATIC for the read at the origin tile; INFERRED for the tile-to-cell division].
6. For each of the 4 tiles:
   a. Class = zone class of the tile (§1), or the block class if unzoned.
   b. Distance to the city centre `(cx, cy)`, in tiles, fetched once per update call
      [STATIC]. [OPEN] How the centre is defined. Question for testing: does the land-value
      peak move when the player scrolls the view, or stay fixed? (The value comes from a
      city-level object that may be the view centre.) Recommended until answered: the map centre
      `(mapW/2, mapH/2)`:
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
7. Block LV `= clamp(ΣT / 4, 1, 255)` (integer division; values ≤ 0 become 1).

All "round" operations are round-half-to-even float-to-int conversions. [STATIC]

### 6.4 Agent ID modifiers (`[AgentIDModifiers]`)

Syntax: `agentType=n:L,n:L,…` (text after the last item following whitespace is a comment).
Each `n` is clamped to [−127, 127]; `L` is `R`, `C`, `I` or `A` (case-insensitive) selecting the
residential, commercial, industrial or all-classes modifier; repeated letters add (8-bit wrap).
[STATIC]

Map construction (at each cycle start): [STATIC]
1. Clear the four maps (All, R, C, I; signed 16-bit cells, one cell per 4×4 tiles, so a map
   has (mapW/4) × (mapH/4) cells [INFERRED: the smoothing of step 3 steps 4 tiles from cell to
   cell]).
2. For every occupant in the city that is occupied (state 1, or has no state): find its
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
* **Filling** [STATIC]. Each landfill tile holds a garbage amount `g` (0–32767); its free
  capacity is `32767 − g`. On a visit, after decay (below), the garbage that the disposal pipeline
  has routed to landfill (stage 3 of `16-sim-utilities.md` §4.3, accumulated since the previous
  visit) is poured into the developer's list of active landfill tiles in list order: each tile takes
  `min(remaining, capacity)`, the amount is removed from the garbage totals, and a tile without a
  landfill piece receives the first landfill piece (occupant ID 10006, key
  `207EDC0E:0000057E:00002716` in the attribute-file key order of `13-buildings-occupants.md`).
  While garbage remains, the active list is extended to further zone-17 tiles. [OPEN] The order
  in which further tiles are added. Recommended until answered: the zone-17 tile nearest
  (Chebyshev distance) to the last tile of the list, ties in row-major order.
* **Decay.** `SC3Tune.ini [LandfillZoneDeveloper] LandfillDecayRate` (per 10 000, capped at
  10 000; shipped 490 ≈ 4.9 %). On each visit to a landfill tile holding garbage amount `g > 0`:
  `d = max(1, round(g × LandfillDecayRate / 10000))`; `g := g − d`. When `g` reaches 0 the
  tile's landfill state is cleared and it is removed from the developer's list of active
  tiles. If the tile still counts toward landfill capacity [INFERRED], `d` is credited back as
  garbage capacity (`16-sim-utilities.md`). [STATIC]

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
* Non-scatter rules (§5.5): housing-project layouts B/C, estates, row houses, farm field tiling,
  airport arrangement step (2) and the seaport pier.
* Who sets the valve `capPct`, and how valves 3001/5001/8001 feed back into R/C/I growth (§3.1,
  §3.2).
* The workforce fraction `w` (§3.4).
* What sets the clean-industry flag (§4.5 step 3).
* The city-centre location (§6.3 step 6b) and the terrain statistic behind `Href` (§6.3).
* Occupant attributes 0x72 bits, 0xC9, 0x7B and the CSATTRIB trailer (`13-buildings-occupants.md`):
  growth does not use them; their meaning is unknown.
* Landfill: the spread order of new landfill tiles (§7).
* Result codes 8, 9 and 17 (§4.3): whether the airport, seaport or landfill developers produce them.
