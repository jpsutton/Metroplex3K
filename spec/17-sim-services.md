# 17 — Simulation: City services (police, fire, health, education, crime, pollution, aura)

Status: working draft. Provenance tags as defined in `00-README.md`.

The random number generator and the month-based age convention are defined in
`16-sim-utilities.md` §1. Budget/funding mechanics are in `19-sim-economy.md`; population cohorts and
the EQ/HQ/life-expectancy model are in `20-sim-population.md` (this document owns the facility side);
fires as disasters are in `21-disasters.md`; land-value use of these layers is in `15-sim-growth.md`.

## 1. Tunable files and sections

All files below are read first as a loose `\Sys\<name>.INI`, then from `Sys\SYS.PAK`. A missing key
leaves a built-in default. [STATIC]

**`UpdatePeriod` and run frequency.** Every `UpdatePeriod` key is the number of the layer's own
**update runs** that make up one full sweep of the map (`14-sim-clock.md` §6.4); it is not a
number of days. Each run processes one slice of cells (formulas in §3.3, §7.1.4, §7.2.4, §8.3).
How often the runs happen [STATIC unless tagged]:

| Layer | Runs |
|---|---|
| Police (§3.3), fire (§4) | once per simulated day, on the daily command, as daily area work (`14-sim-clock.md` §6.2, police before fire) |
| Crime (§7.1), pollution and garbage disposal (§7.2, `16-sim-utilities.md` §4.3), aura (§8), flammability (§4) | once per simulated day, on the daily cascade, as daily scheduler agents (`14-sim-clock.md` §6.1 items 8–11). Aura also samples its mayor-rating chart on the monthly cascade [STATIC] |
| Health, education (§5, §6) | once per month, in the population layer's month-day work (`14-sim-clock.md` §6.3 rule 7) |

With one run per day the shipped values give full sweeps of 30 days (police, fire, pollution),
40 days (crime) and 50 days (aura).

| File | Section | Keys |
|---|---|---|
| `SC3Tune.ini` | `[FireStation]` | `OptimalCoverageRadius`, `OptimalMaxCoverage`, `OptimalMinCoverage`, `OptimalMonthlyUpkeep`, `MaxEfficiency`, `MysticalE` |
| `SC3Tune.ini` | `[PoliceStation]` | same as fire plus `OppressionThreshold` |
| `SC3Tune.ini` | `[Jail]` | `InmateCapacity`, `MonthlyUpkeep` |
| `SC3Tune.ini` | `[School]` | `OptimalDeskCount`, `OptimalTeacherCount`, `MinAgeServed`, `MaxAgeServed`, `OptimalEQEffect`, `OptimalMonthlyUpkeep`, `MaxEfficiency`, `MysticalE` |
| `SC3Tune.ini` | `[College]` | `OptimalDeskCount`, `OptimalProfCount`, `MinAgeServed`, `MaxAgeServed`, `OptimalEQEffect`, `OptimalMonthlyUpkeep`, `MaxEfficiency`, `MysticalE` |
| `SC3Tune.ini` | `[Library]`, `[Museum]` | `PopServed`, `MinAgeServed`, `MaxAgeServed`, `EQEffect`, `MonthlyUpkeep` |
| `SC3Tune.ini` | `[Hospital]` | `OptimalBedCount`, `OptimalDoctorCount`, `OptimalHQEffect`, `OptimalMonthlyUpkeep`, `MaxEfficiency`, `MysticalE` |
| `SC3Tune.ini` | `[FlammabilityLayer]` | `LeafBurningBanOrdEffect`, `SmokeDetectorsOrdEffect`, `NoWaterEffect` |
| `SC3FireLayer.ini` | `[TuningParameters]` | `UpdatePeriod`, `DispatchRadius`, `DispatchEffect`, `MinAcceptableFunding`, `MaxAcceptableFlam`, `StrikeEfficiency`, `MaxStrikeDuration` |
| `SC3Police.ini` | `[TuningParameters]` | `UpdatePeriod`, `DailyJailReleaseRate`, `ConvictionRate`, `NoJailPenalty`, `DispatchRadius`, `DispatchEffect`, `MaxJailOvercrowding`, `MinAcceptableFunding`, `MaxAcceptableCrime`, `StrikeEfficiency`, `MaxStrikeDuration` |
| `SC3Crime.ini` | `[TuningParameters]` | `UpdatePeriod`, `PoliceFactor`, `YouthCurfewOrdEffect`, `GamblingOrdEffect`, `JrSportsOrdEffect`, `NeighborhoodWatchOrdEffect`, `ConservationCorpsOrdEffect` |
| `SC3Crime.ini` | `[AgentIDModifiers]` | *agentType*`=`*a*`,`*b* (two signed integers; trailing text is comment) |
| `SC3Pollution.ini` | `[TuningParameters]` | `UpdatePeriod`, `MaxAirPolluteForUI`, `MaxWaterPolluteForUI`, `MaxGarbagePolluteForUI`, `AirPollutedThreshold`, `WaterPollutedThreshold`, `GarbagePollutedThreshold`, twelve `*OrdEffect` keys, `TrafficAirFactor`, `GarbageAbductionAmount`, `GarbageScalingFactor` |
| `SC3Pollution.ini` | `[AgentIDModifiers]` | *agentType*`=`*airAmt*`:`*airRadius*`,`*waterAmt*`:`*waterRadius*`,`*garbage* (parsed as five integers `%d:%d,%d:%d,%d`) |
| `SC3Aura.ini` | `[TuningParameters]` | `UpdatePeriod`, police coverage effects, `CrimeFactor`, `FireCoverageFactor`, `NoiseFactor`, pollution factors, `StandingGarbageFactor`, `RadiationFactor`, three ordinance effects, EQ/LE factors and thresholds |
| `SC3Aura.ini` | `[AgentIDModifiers]` | *agentType*`=`*a*`,`*b* |
| `SC3ResLayer.ini` | `[TuningParameters]` | health/education strike keys and EQ/HQ keys (see §5, §6) |

## 2. Service-facility coverage (fire and police stations)

### 2.1 Coverage grids

The fire-coverage and police-coverage layers are **coarse grids of 4 × 4-tile cells**, addressed in
tile coordinates (tile (x, z) belongs to cell (⌊x/4⌋, ⌊z/4⌋)). Each cell holds a signed integer
accumulator *A* and a displayed byte *D* = clamp(trunc(*A* / 16), 0, 255). Because the kernel below is
evaluated per *tile* and summed into the tile's cell, *A*/16 is the average over the cell's 16 tiles.
[STATIC, including the 4 × 4 cell size: both grids address cells as (x >> 2, z >> 2)]
**What the crime rule reads** [STATIC]: for each of its cell's 4 tiles the crime update (§7.1.3)
reads *D* of the police cell containing that tile, divides it by `PoliceFactor` (shipped 1) and
subtracts it, so a crime cell inside one police cell subtracts 4·*D*; the ordinance terms are
then added before the division by 4. DATA (Berlin, 18 police stations, police funding
**103 %**, so by §2.2 *x'* = 1.2 − 1/(125 × 1.03 − 120) ≈ 1.086 and *R* = 32, *Max* = 86,
*Min* = 21; Neighbourhood Watch −16, Junior Sports −8 and Conservation Corps −8 enacted):
computing *D* from the stations with these parameters and **no** `NoJailPenalty` reproduces
the saved crime. Over the crime cells with 0 < crime < 255, (potential − 4·crime − 32) / 4*D*
has median 1.000 (10th–90th percentile 1.00–1.07) at days 1, 40, 127, 278 and 480. The mean *D*
over developed cells is 43.3, against the saved `avgCoverage` 43. Using the base parameters
(100 % funding) instead gives a median of 1.24. The remaining spread comes from crime cells
that the 40-day sweep has not rewritten since their potential changed.
**`NoJailPenalty` is not visible in Berlin** [DATA]: although its 12 jails hold 4 320
inmates, the 1.2 overcrowding cap, the original's *D* is unpenalised in every save of a
480-day run. By §3.2 and §3.3 the penalty applies while the unjailed count *P* > 0 [STATIC].
*P* is not saved and starts at 0 after a load (Persisted state). Why it stays 0 here is
[OPEN]. Recommended until answered: reproduce §3.2 exactly, and check that Berlin's *P*
stays 0; if it does not, do not apply the penalty to a loaded city.

### 2.2 Station parameters and the funding curve

Each station type has three base parameters from `SC3Tune.ini` (`[FireStation]` or `[PoliceStation]`),
each stored as an unsigned byte: radius *R₀* = `OptimalCoverageRadius` (tiles), *Max₀* =
`OptimalMaxCoverage`, *Min₀* = `OptimalMinCoverage`. `OptimalMonthlyUpkeep` (*U₀*, simoleons/month)
is the cost at 100 % funding. *M* = max(1.0, `MaxEfficiency` / 100); *E* = `MysticalE` (real).
[STATIC]

When the funding level *p* (percent, 0–255) of a station is set:

1. *x* = *p* × 0.01 (single precision).
2. The station's upkeep becomes *U* = round-to-nearest(*U₀* × *x*) (ties to even).
3. If *U* > *U₀* (over-funding), *x* is replaced by the saturating efficiency
   *x'* = *M* − 1 / ( (*A* − 1/(1 − *M*)) × *x* − *A* ),
   where *A* = 4.999999 × (*E* + 1.2 / (1 − *M*)). The constants 4.999999 (single precision,
   4.999999046…) and 1.2 are hard-coded. *x'* = 1 at *x* = 1 and tends to *M* as *x* grows; with the
   shipped values (*M* = 1.2, *E* = 30), *x'* = 1.2 − 1/(125·*x* − 120). Under-funding (*U* ≤ *U₀*)
   uses *x* unchanged (linear). [STATIC] Defect: *M* is floored at 1.0, so **any** `MaxEfficiency` ≤ 100
   (including 0 or a missing key) gives 1 − *M* = 0 and a division by zero. Correction: the
   previous text said only 100. Guard it, but preserve results for all other values. A key is
   stored only when present (fire 0x10001020, police 0x10004426).
   `PoliceFactor` (u16, crime cell update) and `FireCoverageFactor` (signed byte, aura) are
   divisors with no zero guard either; 0 faults in the original [STATIC].
4. Effective parameters (truncated, stored as bytes): *R* = trunc(*R₀*·*x*), *Max* = trunc(*Max₀*·*x*),
   *Min* = trunc(*Min₀*·*x*). [STATIC]

The same curve (same hard-coded constants, keys from the facility's own section) is used by schools,
colleges and hospitals (§5, §6). [INFERRED: same key set and loader pattern]

### 2.3 Coverage kernel (stamp)

When a station is added (or its parameters change: old stamp removed, new stamp added), for its lot
rectangle [x₀..x₁] × [z₀..z₁] (tiles): [STATIC]

1. Centre *c* = (x₀ + ⌊(x₁ − x₀)/2⌋, z₀ + ⌊(z₁ − z₀)/2⌋). If *R* = 0 nothing is stamped.
2. For every tile (x, z) in [x₀ − R, x₁ + R] × [z₀ − R, z₁ + R], clipped to the map:
   *d* = ⌊√((c_x − x)² + (c_z − z)²)⌋. If *d* ≤ *R*, add *k* = *Max* − trunc((*Max* − *Min*) × *d* / *R*)
   to the accumulator of the tile's cell, then refresh that cell's *D* (applying the police modifiers
   of §3.3 on the police layer).
3. Removing a station applies the same stamp with *Max* and *Min* negated.

Shipped-value illustration: a fully funded fire station adds 200 at every tile within 25 tiles of its
centre (Max = Min); a police station adds 80 at its centre, falling linearly to 20 at 30 tiles.

### 2.4 Dispatch

Dispatching units to a tile places a temporary unit whose stamp has radius `DispatchRadius` and *Max*
= *Min* = `DispatchEffect` (from `SC3FireLayer.ini` or `SC3Police.ini`). Its lot is the single target
tile, shifted one tile in +x unless at the map edge. At most (number of stations + 1) units exist;
when another dispatch would exceed that, the oldest is withdrawn (its stamp removed) first. The fire
unit is occupant 0x207EDC0E:0x0000057E:0x000032CC. [STATIC]

### 2.5 Funding bookkeeping

Each layer keeps the sum of its stations' current upkeep and reports it to the budget
(`19-sim-economy.md`). A newly added station receives the department's current funding percentage.
[STATIC]

## 3. Police, jails and crime

### 3.1 Police tunables (`SC3Police.ini [TuningParameters]`)

| Key | Stored as | Use |
|---|---|---|
| `UpdatePeriod` | ticks (0 → 30) | Length of a full sweep (3.3). |
| `DailyJailReleaseRate` | value × 0.001 | Fraction of each jail's inmates released per tick. |
| `ConvictionRate` | value × 0.001 | Fraction of arrest potential converted into prisoners. |
| `NoJailPenalty` | value × 0.01 | Coverage multiplier while unjailed prisoners exist. |
| `DispatchRadius`, `DispatchEffect` | bytes | §2.4. |
| `MaxJailOvercrowding` | value × 0.01 | Maximum inmates ÷ capacity. |
| `MinAcceptableFunding` | byte, percent | Strike threshold on funding. |
| `MaxAcceptableCrime` | byte | Strike threshold on average crime. |
| `StrikeEfficiency` | byte, percent | Coverage multiplier during a strike. |
| `MaxStrikeDuration` | ticks | Longest strike. |

Police stations are occupants of group 13130 (0x334A), jails of group 13881 (0x3639), with capacity
`[Jail] InmateCapacity` and upkeep `[Jail] MonthlyUpkeep` (`SC3Tune.ini`). [STATIC] A police tick
is one run of the police layer, i.e. one simulated day (§1). [STATIC]

### 3.2 Jails and prisoners

The police layer keeps an unjailed-prisoner count *P* and, per jail, capacity *Cᵢ* and inmates *Iᵢ*.
Redistribution runs whenever prisoners are added or a jail is added/removed: [STATIC]

1. No jails: *P* ← ⌊95 *P* / 100⌋; stop.
2. *P* ← *P* + Σ*Iᵢ*; *r* = min(*P* / Σ*Cᵢ*, `MaxJailOvercrowding`).
3. For each jail in list order: if *r* > 1, *Iᵢ* = round(*Cᵢ* × *r*), *P* ← max(0, *P* − *Iᵢ*); otherwise
   *Iᵢ* = min(*Cᵢ*, *P*), *P* ← *P* − *Iᵢ*. If *P* is 0 at a jail, that jail gets 0.
4. What remains in *P* is unjailed. A removed jail's inmates are added to *P* first.

Every tick, before the sweep, each jail releases: *Iᵢ* ← *Iᵢ* − round(*Iᵢ* × `DailyJailReleaseRate`).

### 3.3 Police tick (incremental sweep)

Each tick handles *N* = ⌊W·H / (16 × `UpdatePeriod`)⌋ cells (W × H = map size in tiles), so a full
sweep takes `UpdatePeriod` ticks. Cells are visited in 16 phases; in each phase the anchor tiles are
(oₓ + 16i, o_z + 16j), x fastest, with phase offsets (oₓ, o_z) in this order: (4,8), (0,12), (12,0),
(8,12), (12,8), (12,12), (0,4), (12,4), (4,12), (4,4), (4,0), (8,8), (8,4), (0,8), (8,0), (0,0). The
cursor persists between ticks; after the 16th phase the sweep statistics are finalised and the
phase index wraps to the first entry. [STATIC]

Per tick:

1. Strike state machine (3.4).
2. Jail release (3.2).
3. For each of the *N* cells: take the accumulator *A*; during a strike *A* ← trunc(`StrikeEfficiency`
   × *A* / 100); while *P* > 0, *A* ← round(*A* × `NoJailPenalty`); write *D* = clamp(trunc(*A*/16), 0,
   255) to the displayed layer. The penalty is also applied when a stamp (§2.3) refreshes a
   cell's *D*. The crime rule reads this penalised *D* (§2.1). [STATIC] The cell is "developed" if any of its 16 tiles is non-zero in the
   per-tile occupancy layer [INFERRED meaning]. Developed cells add to the sweep statistics (count,
   Σ*D*, count of *D* = 0). If the crime value *c* at the anchor tile and *D* are both non-zero, the
   tick's arrest potential grows by min(*c*, *D*).
4. New prisoners = round(arrest potential × `ConvictionRate`); if positive, add to *P* and redistribute.

End of sweep: average coverage = Σ*D* / count; coverage percent = 100 − ⌊100 × zeros / count⌋ (both 0
if count = 0); statistics reset. [STATIC]

### 3.4 Police strike

Strike chance per tick, out of 10000: 10000 if already striking; else 0 if there are no stations, or
funding ≥ `MinAcceptableFunding`, or city-average crime ≤ `MaxAcceptableCrime`; else
(`MinAcceptableFunding` − funding)². [STATIC]

* Not striking: if `Rand(10000)` < chance, the strike starts, counter ← 0, and the advisors and
  news are told that the police are on strike.
* Striking: counter + 1; the strike ends when counter > `MaxStrikeDuration`, or funding ≥ 100 %, or no
  stations remain. [STATIC] Funding is an integer percentage (one byte), so "≥ 100 %" here and
  "> 99 %" in §5.3 and §6.4 are the same test. [STATIC]

The fire department uses the **same strike machine** with the `SC3FireLayer.ini` keys, testing the
flammability layer's running average against `MaxAcceptableFlam` in place of average crime vs
`MaxAcceptableCrime` [STATIC]. Chance / start / end / `StrikeEfficiency` coverage multiply are
identical in form to the police rules above (no stations → never start; end when counter >
`MaxStrikeDuration`, funding ≥ 100 %, or no stations remain). Advisors/news get the fire-strike
messages instead of the police ones.

## 4. Fire protection and flammability

Fire coverage uses §2 with `[FireStation]` keys; the fire-layer sweep, dispatch and strike mirror
§3.3–3.4 with `SC3FireLayer.ini` keys (`UpdatePeriod`, `DispatchRadius`, `DispatchEffect`,
`MinAcceptableFunding`, `MaxAcceptableFlam`, `StrikeEfficiency`, `MaxStrikeDuration`); there are no
jails. Fire stations are occupant group 13002 (fire-layer membership test uses group 0x32CA = 13002).
[STATIC]

**Flammability layer**: one byte per **tile** (persisted, see below). Tunables `SC3Tune.ini
[FlammabilityLayer]`: `LeafBurningBanOrdEffect` and `SmokeDetectorsOrdEffect` (percent reductions
while those ordinances are active) and `NoWaterEffect` (percent increase on tiles without water
service). The per-occupant base flammability comes from the occupant's flammability attribute
(`13-buildings-occupants.md`). [STATIC]

Tile value [STATIC]. For a tile with an occupant whose flammability attribute is *b* (0–255):

1. If there is no occupant or *b* = 0, the value is 0.
2. *u* = (`LeafBurningBanOrdEffect` if the leaf-burning ban, ordinance 0x22BF1E35, is enacted) +
   (`SmokeDetectorsOrdEffect` if the smoke-detector ordinance, 0x62BF1DAA, is enacted) −
   (`NoWaterEffect` if the tile has no water service). The three keys are bytes; *u* is a signed
   integer and may be negative.
3. If *u* ≥ 100 the value is 0. Otherwise value = min(255, *b* − ⌊*b* · *u* / 100⌋) (division
   truncates toward zero; a negative *u* raises the value).

Each run of the flammability layer recomputes ⌊W·H / 30⌋ tiles (the 30 is a built-in constant,
not an INI key) [STATIC]; see §1 for the run frequency.

**Visit order** [STATIC]: tiles are visited in **row-major order with x fastest** (x ascending,
then z ascending), continuing from a saved cursor `(cursorX, cursorZ)`. Each processed tile
advances `cursorX` by 1; when `cursorX` reaches W it resets to 0 and `cursorZ` advances by 1;
when `cursorZ` reaches H the cursor wraps to `(0, 0)` and the layer finalises its running
average (`⌊sweepSum / sweepCount⌋`, or 0 if none) before the next tile. Only tiles whose new
value is non-zero add to `sweepSum` and `sweepCount`; zero tiles are excluded from both. The
average is stored as a byte and is the value the fire strike compares with
`MaxAcceptableFlam` [STATIC, SIMSERV 0x1000C523, strike chance 0x1000AF08]. Record trailer after
the byte grid: u32 cursorX, u32 cursorZ, u32 sweepSum, u32 sweepCount, u8 average
[STATIC for the fields, INFERRED order]. Tiles per run (⌊W·H/30⌋) are recomputed at load, not
saved. The same cursor and
running sum/count persist across daily runs, so a full map pass takes 30 simulated days on a
square map.

The flammability query classifies a tile's byte as: 0, 1–46, 47–83, 84–121, 122–150, > 150 (six descriptive bands). [STATIC]

Ignition, spread and fire-fighting during fires are specified in `21-disasters.md`.

## 5. Health

### 5.1 Hospital tunables (`SC3Tune.ini [Hospital]`)

| Key | Stored as | Use |
|---|---|---|
| `OptimalBedCount` | u32 | Beds per hospital. Fixed: funding does not change it |
| `OptimalDoctorCount` | u32 | Doctors at 100 % funding |
| `OptimalHQEffect` | value × 0.001 (min 0.001) | HQ effect *e₀* per bed per month at 100 % funding |
| `OptimalMonthlyUpkeep` | u32 | *U₀* |
| `MaxEfficiency`, `MysticalE` | as §2.2 | Over-funding curve |

[STATIC] Each is read once per session from the loose file, then from `SYS.PAK`.

### 5.2 Hospital funding

When the health department's funding *p* % is applied (every month, and when funding changes), each
hospital does the following. [STATIC]

1. Let *x* = 0.01·*p*. Upkeep = round(*U₀*·*x*). Rounding is to nearest, ties to even, everywhere
   in this section.
2. If *x* > 1, replace *x* by the saturating *x'* of §2.2, using this section's `MaxEfficiency` and
   `MysticalE`.
3. Doctors = round(`OptimalDoctorCount`·*x*).
4. Effect *e* = *e₀*·*x*.
5. Beds = `OptimalBedCount`.

Doctors are only summed for the record (`10-save-format.md` §6.11). The cohort model uses beds and
*e* (`20-sim-population.md` §5.2):

*x_H* = Σ(beds·*e*) / max(patients, beds).

Here patients = trunc((⌊(*P₁* + *P₂*) / 2⌋ · `PollutionFactor` + `HospitalizationRate`) × population),
where *P₁* = avgAir and *P₂* = avgWater of the pollution record (§7.4), so the pollution term is
their integer mean [STATIC].

### 5.3 Health rating, ordinances, strikes

* Health rating 0–4 = round(4·*x_H* / (`OptimalYearlyHQ`/12) − 0.4).
* Ordinances add a fixed monthly amount to *x_H*: smoking ban, CPR training, free clinics, and also
  school crossing guards [STATIC: in the original the crossing-guard ordinance acts on health, not
  on the aura; the aura reads only the parking-fines, alternate-driving and nuclear-free
  ordinances, §8.1].
* Pollution raises the number of patients.
* A strike starts with probability (`MinAcceptableHealthFunding` − funding)² / 10000 per month.
  This needs **both** the rating < `MinAcceptableHealthRating` **and** funding <
  `MinAcceptableHealthFunding`, and at least one hospital must exist.
* A strike lasts until its counter exceeds `MaxHealthStrikeDuration` months, or funding > 99 %, or
  no hospital remains.
* During a strike *x_H* is multiplied by `HealthStrikeEfficiency` **unscaled**. This is a defect in
  the original: 20 means ×20, not ×0.2.

Exact formulas, ordering and constants are in `20-sim-population.md` §5. [STATIC]

## 6. Education

### 6.1 Facility tunables (`SC3Tune.ini`)

| Section | Key | Stored as | Use |
|---|---|---|---|
| `[School]`, `[College]` | `OptimalDeskCount` | u32 | Capacity *C* (people), not funding-dependent [INFERRED: the saved capacity sums are exact multiples of it in all played files] |
| `[School]` | `OptimalTeacherCount` | u32 | Teachers at 100 % funding |
| `[College]` | `OptimalProfCount` | u32 | Professors at 100 % funding |
| `[School]`, `[College]` | `MinAgeServed`, `MaxAgeServed` | u8 | Served ages (*MaxAgeServed* capped at 89 when used) |
| `[School]`, `[College]` | `OptimalEQEffect` | value × 0.001 (min 0.001) | Effect *e₀*: EQ points per seated person per month at 100 % funding |
| `[School]`, `[College]` | `OptimalMonthlyUpkeep`, `MaxEfficiency`, `MysticalE` | as §2.2 | Upkeep and over-funding curve |
| `[Library]`, `[Museum]` | `PopServed` | u32 | Capacity *C* (people) |
| `[Library]`, `[Museum]` | `MinAgeServed`, `MaxAgeServed` | u8 | Served ages |
| `[Library]`, `[Museum]` | `EQEffect` | value × 0.001 (min 0.001) | Fixed effect *e* |
| `[Library]`, `[Museum]` | `MonthlyUpkeep` | u32 | Fixed upkeep (no funding curve) |

[STATIC]

### 6.2 School and college funding

When the education funding *p* % is applied: [STATIC]

1. Let *x* = 0.01·*p*. Upkeep = round(*U₀*·*x*).
2. If *x* > 1, replace *x* by the saturating *x'* of §2.2.
3. Staff = round(`OptimalTeacherCount` (or `OptimalProfCount`) · *x*). If staff = 0 and *x* > 0,
   staff = 1.
4. Effect *e* = *e₀*·*x*.

Staff counts are only summed for the record.

Libraries and museums are funded from the education department too: they are included in the
department's funding pass and upkeep total. Their effect and capacity do not depend on funding.
The funding pass keeps three facility lists by kind (0 = school, 1 = college, anything else =
third): libraries (kind 2) and museums (kind 3) share that third list. College funding also has
its own dedicated list; EQ distribution still uses four separate kind groups
(`20-sim-population.md` §4.2). [STATIC]

### 6.3 Coverage of the population

Each month, every facility spreads its capacity over its served ages in proportion to the age
shares. For each age, every facility kind contributes Σ(seats·*e*) / max(people of that age, seats).
An oversupplied age therefore gets the full effect, and an undersupplied age a diluted one. Exact
steps are in `20-sim-population.md` §4.2–§4.3.

### 6.4 Ordinances, ratings, decay, strikes

* Ordinances add fixed monthly EQ to age bands:
  * reading: child or adult value;
  * youth curfew and junior sports: their own age bands.

  They do not affect crime here. Crime-side effects of the same ordinances are in `SC3Crime.ini`
  (§7.1).
* Children younger than `AgeOfRebellion` absorb min(city EQ of ages 20–40, 120) / (`AgeOfRebellion`·12)
  per month.
* Ages up to 26 have a floor of 85·age/26.
* EQ is capped at 150.
* EQ decays only at the yearly shift: ×`EQDecay` for ages 27 and over.
* The school rating uses the EQ gained by ages 6–18 (13 ages, ÷13) against `OptimalYearlySchoolEQ`/12. The
  college rating uses ages 19–24 (÷6) against `OptimalYearlyCollegeEQ`/12. Both use the same
  round(4·g/… − 0.4) form, clamped to 0–4.
* Schools and colleges strike independently. Each needs its own rating <
  `MinAcceptableEducationRating` **and** education funding < `MinAcceptableEducationFunding`; the
  probability is (MinFunding − funding)²/10000 per month.
* A strike ends when its counter exceeds `MaxEducationStrikeDuration`, or funding > 99 %, or none of
  that kind remains.
* During a strike that kind's seat effect is multiplied by `EducationStrikeEfficiency` **unscaled**
  (same defect as health).

When a school or college strike starts or ends, the advisors and news are notified (separately
for schools and colleges). [STATIC] Details are in `20-sim-population.md` §4–§5.

## 7. Crime and pollution layers

### 7.1 Crime

The crime layer keeps three per-cell grids on **2 × 2-tile cells** ((W/2) × (H/2) cells for a
W × H tile map; tile (x, z) belongs to cell (⌊x/2⌋, ⌊z/2⌋)) [STATIC]:

* **crime** *C* — u8, 0–255, the value consumed by police arrests (§3.3), land value
  (`15-sim-growth.md`) and the crime map view;
* **crime potential** *P* — signed 16-bit accumulator built from the occupants around the cell
  (wraps modulo 2¹⁶; read back as signed);
* the **sweep order** — a shuffled list of all cells (below).

#### 7.1.1 Tunables (`SC3Crime.ini`)

| Key | Stored as | Use |
|---|---|---|
| `[TuningParameters] UpdatePeriod` | u16, 0 → 1 | Ticks per full sweep. |
| `PoliceFactor` | u16 divisor | Police coverage is divided by it (no zero guard: 0 is a defect, guard it). |
| `YouthCurfewOrdEffect` | s16 | Added while ordinance 0xC2BF1DC5 is active. |
| `GamblingOrdEffect` | s16 | Added while ordinance 0xA0D07129 is active. |
| `JrSportsOrdEffect` | s16 | Added while ordinance 0x815B4CEF is active. |
| `NeighborhoodWatchOrdEffect` | s16 | Added while ordinance 0x62BF1DB9 is active. |
| `ConservationCorpsOrdEffect` | s16 | Added while ordinance 0xA2F6E7DA is active. |
| `[AgentIDModifiers]` *id*`=`*r*`,`*m* | *r*: u8 radius (tiles); *m*: amount, parsed as a signed integer and clamped to [−127, 127] | Per-occupant crime source. |

[STATIC] The ordinance IDs are those of `Ordinances.ini`. The two numbers in `[AgentIDModifiers]`
are **radius first, amount second** (City Hall `20,-20`: radius 20, amount −20). [STATIC]

#### 7.1.2 Crime potential (occupant stamps)

When an occupant is added to the city (and again with the amount negated when it is removed), the
layer does the following [STATIC]:

1. Occupants that have a lot state word (`13-buildings-occupants.md` Persisted state) are only
   counted when that state equals 1 (occupied); occupants without one always count. [STATIC]
2. Take the occupant's agent type (attribute 0x65; the INI keys are agent types, e.g. 3100, 19999,
   `13-buildings-occupants.md` §3.2). If the table holds that type, use its (*r*, *m*). Otherwise,
   compare every table entry with the occupant's agent type using the agent-type ancestor match of
   `13-buildings-occupants.md` §4.1, which reports whether an entry matches and at what distance. Take the matching entry with the smallest distance (the first one in
   ascending-ID order wins ties), or (0, 0) if none matches. **Insert** the result into the table
   under the occupant's agent type, so it is cached and also saved (§7.3). [STATIC; distance =
   parent-step count, `13-buildings-occupants.md` §4.1]
3. If *m* = 0, stop. Otherwise take the occupant's tile rectangle [x₀..x₁] × [z₀..z₁], grow it by *r*
   on every side, and clip it to [0, W−1] × [0, H−1].
4. For **every tile** in that rectangle add *m* (removal: −*m*) to *P* of the tile's cell. A cell lying
   wholly inside the rectangle therefore gains 4·*m*. The stamp is a flat box, with no distance
   falloff.

On load, the saved modifier table is compared with the current INI table. If any ID present in both
has a different (*r*, *m*), or a saved entry no longer resolves to the same values, *P* is cleared and
rebuilt by re-adding every occupant. Otherwise the saved *P* is kept. [STATIC] An implementation may
always rebuild *P* on load; the result is the same unless the saved file was produced with different
INI values.

#### 7.1.3 Per-cell crime formula

For a cell whose top-left tile is (x, z) (both even), with *P* the cell's potential as a signed value
and *pol*(t) the **police-coverage byte** read at tile t (the coarse police layer of §2.1/§3.3, which
answers per tile) [INFERRED, high: the coverage read is the police coverage]:

  *v* = *P* − Σ over the four tiles t ∈ {(x, z), (x+1, z), (x, z+1), (x+1, z+1)} of ⌊*pol*(t) / `PoliceFactor`⌋
     + Σ over the five crime ordinances that are active of their effect
  *C* = clamp(trunc(*v* / 4), 0, 255)        (division truncates toward zero) [STATIC]

Then *C* is written to the cell. If *P* > 0, the sweep statistics gain sum += 4·*C* and count += 4.
[STATIC]

There is **no smoothing, decay or diffusion** step in the crime layer. Spatial spread comes only from
the radius of each occupant's box stamp, and *C* is recomputed from scratch each time its cell is
visited. [STATIC]

#### 7.1.4 Incremental sweep and sweep order

* Cells per tick *N* = ⌊W·H / (4 · `UpdatePeriod`)⌋, so a full sweep (all (W/2)·(H/2) cells) takes about
  `UpdatePeriod` ticks. *N* is computed when the layer is initialised. [STATIC]
* The cells are visited in a **random permutation**. The order list holds one u16 per cell, with the
  cell's x in the high byte and z in the low byte. It is built in row order (x outer, z inner, both
  ascending) and then shuffled with a forward Fisher–Yates pass: for *i* = 1 … *n*−1, *j* = `rand()`
  mod (*i*+1), swap entries *i* and *j*. Here `rand()` is the Microsoft C runtime generator
  (*s* ← *s*·214013 + 2531011, result (*s* >> 16) & 0x7FFF). Its seed is process-global and is not
  saved. [STATIC for the shuffle; INFERRED, high, that `rand()` is this generator]
* A cursor (index into the list) advances once per processed cell. The cursor ("next cell", in tile
  coordinates = 2 × cell) is kept between ticks. For each step: compute the cell at the cursor; if the
  list is exhausted, finalise the statistics (average = ⌊sum / count⌋, or 0 if count = 0; reset sum
  and count), re-shuffle the list and restart at index 0; then fetch the next cell. [STATIC]
* The sweep average is the "average crime" used by the police strike (§3.4) and is pushed to the crime
  history chart (12 monthly, 10 yearly and 10 per-decade samples; same chart format as the aura layer).
  Monthly sample = `avgCrime`, pushed in the crime month-end; on 1 January also yearly, and every
  10th year also decade (`14-sim-clock.md` §6.3 rule 7) [STATIC]

### 7.2 Pollution

#### 7.2.1 Grids

The pollution layer works on **2 × 2-tile cells** ((W/2) × (H/2)), except for standing garbage,
which is stored per **tile**. [STATIC]

| Grid | Resolution | Element | Meaning |
|---|---|---|---|
| state | cell | u32 | Bits 0–14: **water pollution** (0–32767). Bits 15–29: **air pollution** (0–32767). Bit 30: **sea-water (salt) flag** of the cell. It is set by the terrain when a sea-water surface is written at a vertex of any tile in the cell, and cleared only by a whole-terrain reset (`11-world-terrain.md` §5) [STATIC]. The pollution update never changes it. It is read by the water renderer, by the new-city neighbour test (`19-sim-economy.md` §10.6) and by the Neighbors-window region image (`11-world-terrain.md` §9). It is set in 19 shipped files [DATA]. **Bit 31** is a second cell flag with a getter/setter pair parallel to bit 30. Terrain water tools set only bit 30 (sea water); they never write bit 31. The only writer found is a pollution-layer sync/load path that sets bit 31 when a source tile property (key 2) equals 5. The bit is **clear in every shipped city** [DATA]. A map-overlay query path can tint a tile when the bit is set, and a change posts a redraw message, but nothing in normal play sets it. Implementations may leave it clear and ignore it on load. [STATIC] |
| air source | cell | s32 | Sum of occupant air stamps (7.2.3) |
| water source | cell | s32 | Sum of occupant water stamps |
| garbage source | cell | s16 | Sum of occupant garbage stamps, never negative |
| standing garbage | tile | u16 | Bits 0–14: standing garbage on the tile (0–32767). Bit 15: "registered landfill tile" flag (7.2.5). |

The air and water values are what map views, thresholds and other subsystems read. A tile query
returns the value of the tile's cell. [STATIC]

#### 7.2.2 Tunables (`SC3Pollution.ini [TuningParameters]`)

`UpdatePeriod` (u16, 0 → 1); `MaxAirPolluteForUI` (*Mₐ*), `MaxWaterPolluteForUI` (*M_w*),
`MaxGarbagePolluteForUI` (map scale maxima, which also set the relaxation rates in 7.2.4);
`AirPollutedThreshold`, `WaterPollutedThreshold`, `GarbagePollutedThreshold`; the twelve ordinance
percentages (unsigned bytes); `TrafficAirFactor`; `GarbageAbductionAmount`; `GarbageScalingFactor`
(stored × 0.001 as a real number, *s*; shipped 100 → *s* = 0.1). *s* is **not** used by the
daily pollution/disposal pass; the pollution query UI multiplies a pollution-layer garbage total
by *s* and truncates to an integer for one query line. [STATIC]

`[AgentIDModifiers]` *id*`=`*air*`:`*rA*`,`*water*`:`*rW*`,`*garbage*: air amount, air radius (tiles,
u8), water amount, water radius (u8), and garbage amount. The amounts are signed. **Trees** (ID 300,
for example `-4:5,-5:5,0`) and other negative entries *absorb* pollution by stamping negative values.
This is the only greenery effect. [STATIC]

#### 7.2.3 Source stamps (occupant added / removed)

The occupant is resolved to (*a*, *r_A*, *w*, *r_W*, *g*) as follows. If the table holds the
occupant's agent type, use that entry. Otherwise walk up the parent chain of `CitySim.ini
[AgentTypeHierarchy]`, stopping at the root type 50, and use the first ancestor found in the table
(this is the nearest ancestor, the same rule as crime, §7.1.2). If none is found, use all zeros. The
result is cached in the table under the occupant's agent type, and the cached entries are saved
(7.4). Removal applies the same stamp with *a*, *w* and *g*
negated. Garbage-facility occupants (water treatment 12820, incinerator 12852, waste-to-energy
12853, recycling centre 12851, all within family 12005) are also added to, or removed from, the
facility lists (7.4). [STATIC]

For the lot [x₀..x₁] × [z₀..z₁] (tiles):

1. **Inside the lot**, every tile adds *a* to the air source, *w* to the water source and *g* to the
   garbage source of its cell. After each garbage addition, a negative cell value is set to 0.
2. Let *R* = max(*r_A*, *r_W*). If *R* = 0, stop. Otherwise visit every tile of the rectangle
   [x₀−R..x₁+R] × [z₀−R..z₁+R], clipped to the map, that lies **outside** the lot. Let *dx* be its
   distance outside the lot in x (x₀ − x, x − x₁, or 0) and *dz* the same in z. The distance is
   *d* = *dx* or *dz* when only one of them is non-zero; otherwise
   *d* = max(*dx*, *dz*) + ⌊min(*dx*, *dz*) / 2⌋ (an octagonal metric).
3. If *d* ≤ *r_A*: air source += trunc((*r_A* − *d* + 1) · *a* / (*r_A* + 1)).
   If *d* ≤ *r_W*: water source += trunc((*r_W* − *d* + 1) · *w* / (*r_W* + 1)).
   Garbage does not spread. (trunc is toward zero.) [STATIC]

Each tile adds to its cell, so a cell sums its four tiles' contributions. The per-cell update divides
by 4. When the INI table changes between saving and loading (any saved entry differs), the three
source grids are cleared and rebuilt by re-adding all occupants. Otherwise the saved grids are used.
[STATIC]

#### 7.2.4 Per-cell update

The sweep machinery is identical to the crime sweep (§7.1.4): ⌊W·H / (4·`UpdatePeriod`)⌋ cells per
tick, a shuffled cell order that is re-shuffled when exhausted, and a cursor saved in tile
coordinates. At the start of every tick, the layer sums the capacity of the water-treatment list
(*C_T*) and the burn capacities of the incinerator and WTE lists. Each cell (top-left tile (x, z))
is then processed: [STATIC]

1. Read *a* (air source), *w* (water source) and *g* (garbage source as a signed 16-bit value).
   Apply ordinances, each as *v* ← *v* − trunc(*v*·*e*/100): Clean Air 0xA2BF1E43 → *a*;
   Trash Presort 0x42BF1E18 → *g*; Conservation Corps 0xA2F6E7DA → *w*; Industrial Pollution Fee
   0x02F6E7E2 → *w* and *a*; Lawn Chemical Ban 0x62F6E808 → *w*; Tire Recycling 0xC2F6E81F → *g*.
2. *n* = the number of the cell's four tiles whose zone type is not 17 (landfill). Any non-landfill
   tile that still carries the landfill flag loses the flag, and 32767 is subtracted from the
   landfill store's room `D` (`16-sim-utilities.md` §4.4).
3. Start with *S_A* = *a* and *S_W* = *w*. For each of the four tiles in the order (x, z),
   (x+1, z), (x, z+1), (x+1, z+1), with zone type *zt*:
   * *zt* 1–2: Leaf Burning Ban 0x22BF1E35: *S_A* += trunc(*e*·*a* / (−100·*n*)). Backyard
     Composting 0x22F6E814: *g* += trunc(*g*·*e* / (−100·*n*)).
   * *zt* 5–7: Paper Reduction 0x22F6E81B on *g* (same form).
   * *zt* 9–11 or 15: Industrial Waste Disposal 0xA2F917CF on *g* (same form).
   * *zt* 17: Landfill Gas Recovery 0x22F6E80C: *S_A* += trunc(*e*·*S_A* / (100·(*n* − 4))). This is
     a reduction, because *n* < 4.
   * Standing garbage *G_t* of the tile: if *G_t* > 0, *S_W* += trunc(8·*G_t* / 32767) (garbage
     leaches into water), and the cell counts as "has garbage".
   * Traffic *T_t* (0–255, `18-sim-transport.md`): *S_A* += *T_t*·`TrafficAirFactor`. With Car
     Smogging 0xC2BF1E04: *S_A* −= trunc(*e*·*T_t*·`TrafficAirFactor` / 400).
   * Non-landfill tiles only: the garbage share *q* = trunc(*g*/*n*) is added to the "garbage
     produced" statistic. If the city **recycling ratio** *r* > 0 and *q* > 0, *k* = min(*q*,
     round(*q*·*r*)) is removed from *q* and counted as recycled (for the 2nd and 4th tiles, 0.5 is
     subtracted before rounding when *q*·*r* > 0.5). *r* is computed monthly from the recycling
     centres (`16-sim-utilities.md` §4.3); it is **not** `GarbageScalingFactor` (correction to
     earlier drafts). Then *q* += *G_t*, the tile's standing garbage is cleared, and *q* goes
     through the disposal stages of `16-sim-utilities.md` §4.3; the remainder becomes the tile's
     standing garbage as specified there (including the backlog exchange with the landfill
     store). [STATIC]
4. Water treatment: if *C_T* > 0 and *S_W* > 0, the amount treated is *t* = *C_T* if no history
   exists (last sweep's raw-water total ≤ 0, or last sweep's treated total < *C_T*). Otherwise
   *t* = max(1, round(treatedLast / rawLast · *S_W*)). Then *t* = min(*t*, *S_W*, *C_T*);
   *S_W* −= *t*; *C_T* −= *t*. [STATIC; "last sweep" totals INFERRED]
5. Targets: *A** = clamp(trunc(*S_A*/4), 0, 32767), *W** = clamp(trunc(*S_W*/4), 0, 32767).
   Statistics: Σ*S_A*, Σ*S_W* (after treatment), Σ raw *S_W*, Σ treated, and +4 to the
   polluted-cell count if *a* > 0, *w* > 0 or the cell has garbage.
6. **Relaxation (the only smoothing or decay):** the stored air *A* moves toward *A** by at most
   ⌊*Mₐ*/8⌋ per visit, in either direction. The stored water *W* rises toward *W** by at most
   ⌊*M_w*/12⌋ and falls by at most ⌊*M_w*/120⌋. If a fall would reach 0, *W* is set to 1 instead
   when any of the tiles (x−1, z), (x, z−1), (x+1, z) or (x, z+1) that exists has water > 1. The last
   two lie in the same cell and still hold the old value, so in practice *W* stops at 1 whenever the
   old *W* was > 1 (original behaviour; keep it). [STATIC]

After the whole tick, unused treatment capacity is returned to the plants, and a negative
landfill-store balance (backlog) is pushed through disposal again (`16-sim-utilities.md` §4.3
step 6). [STATIC]

Pollution does **not diffuse** between cells over time and **does not decay** in any other way. The
spread is entirely the static stamp kernel of 7.2.3, and the temporal behaviour is the relaxation in
step 6. The pollution update reads no terrain-water term: water bodies have no special effect.
[STATIC: the update reads only the three source grids, zone types, traffic and standing garbage]
No radiation source appears in `SC3Pollution.ini` or in this update. If radiation exists, it is
handled elsewhere (`21-disasters.md`). Cell bit 30 is not a radiation marker; it is the sea-water flag (7.2.1).

#### 7.2.5 Thresholds and UI

A tile is "air polluted" if air ≥ `AirPollutedThreshold`. Water and garbage use their own thresholds
in the same way (≥ comparisons). The map-view scale maxima are `Max*PolluteForUI`. The query text
bands sit at 1/8, 1/4, 1/2 and 4/5 of the maximum.

Details [STATIC, SIMECO 0x1000C95C query, 0x1000E3A3 / 0x1000E406 / 0x1000E469 overlays]:
* **Map overlay.** The value is v = min(value, M). If v = 0 the tile is not tinted. Otherwise the
  colour index is ⌊(N − 1)·v / M⌋, where N is the colour ramp's entry count. The values are:
  * air and water: the stored, relaxed cell value;
  * garbage: the tile's standing garbage.
  M is `MaxAirPolluteForUI` 11000, `MaxWaterPolluteForUI` 11000 or `MaxGarbagePolluteForUI` 30000.
* **Query text** for the air and water cell value (unsigned compares, string table `0x82E0074C`):

  | Value | Air | Water |
  |---|---|---|
  | heading | 0x187 | 0x18E |
  | 0 | 0x188 | 0x18F |
  | < M/8 (1375) | 0x189 | 0x190 |
  | < M/4 (2750) | 0x18A | 0x191 |
  | < M/2 (5500) | 0x18B | 0x192 |
  | < 4M/5 (8800) | 0x18C | 0x193 |
  | otherwise | 0x18D | 0x194, and the "high" flag is set |

* **Landfill garbage query** (zone 17 tiles only): total × `GarbageScalingFactor` (× 0.001),
  **rounded to nearest** (not truncated). Landfill-zone tiles (zone type 17) are registered
by setting bit 15 of their standing-garbage word, and the registered-tile count is saved. [STATIC]

### 7.3 Crime record `206C6E7C:20A7AE7F:00000000`

Parsed with exact length on **59/59** shipped files (`.sc3`, `.SNR`, `.st3`, `.sct`, including
`TUTORIAL.SC3`). [DATA] Let *n* = W/2 be the cell count per side (64, 96 or 128). Size = 8 + *n*² + 17 +
132 + (42 + 2*n*²) + 2*n*² + 4 + 9*k*. Field meanings are [STATIC] unless marked.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | recVersion | 1 in all files. |
| 0x02 | 2 | u16 | streamVersion | 2 (3 in `.sct` terrains and `Seoul World Cup.SNR`); written by the common record-header writer. |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | *n*² | u8[] | crime | Crime *C* per cell. Rows are stored **last row first**; each row holds *n* bytes. |
| +0 | 4 | u32 | cursorX | Next cell to process, x in tiles (= 2 × cell x). Reset to 0 on load if ≥ W. |
| +4 | 4 | u32 | cursorZ | Same for z (reset to 0 if ≥ H). |
| +8 | 4 | u32 | sweepSum | Running Σ 4·*C* of the current sweep. |
| +12 | 4 | u32 | sweepCount | Running count (4 per counted cell). |
| +16 | 1 | u8 | avgCrime | Average from the last completed sweep. |
| +17 | 132 | u32[33] | chart | History chart: 12 monthly values, 10 yearly, 10 per-decade, then a u32 chart parameter (260 in all files) [DATA; parameter meaning OPEN]. |
| … | 1 | u8 | ordState | Sweep-order iterator state: 1 = running, 2 = exhausted. |
| … | 4 | u32 | ordMode | 0 = permutation list (all files). Non-zero selects a stride mode that the crime layer never uses. |
| … | 1 | u8 | ordRebuild | 1 = rebuild the list on the next reset (1 in all files). |
| … | 4 | s32 | xMin | 0 |
| … | 4 | s32 | xMax | *n* − 1 |
| … | 4 | s32 | zMin | 0 |
| … | 4 | s32 | zMax | *n* − 1 |
| … | 4 | s32 | total | *n*² |
| … | 4 | u32 | listLen | *n*² |
| … | 2·listLen | u16[] | order | Permutation of cells: high byte = x, low byte = z. Stored **in reverse** (the last element is written first). |
| … | 4 | s32 | ordIndex | Cursor into the list (1 in fresh files). |
| … | 4 | s32 | stridePos | 0 (stride mode only) |
| … | 4 | s32 | stride | 1 (stride mode only) |
| … | 2*n*² | s16[] | potential | Crime potential *P* per cell, little-endian. Rows are stored last row first. |
| … | 4 | u32 | k | Number of modifier entries (42–74 in shipped files) |
| … | 9·k | entry[] | modifiers | In ascending ID order: u32 occupant ID, s32 amount *m*, u8 radius *r*. Holds the INI entries plus the cached nearest-match results (§7.1.2), e.g. ID 300 → (0, 0). |

On load: if `recVersion` is 0, a legacy layout is used [OPEN]. The potential grid and modifier table are
handled as described in §7.1.2. When writing, write the current (cached) modifier table.
[STATIC]

### 7.4 Pollution record `206C6E7C:C0A81498:00000000`

Parsed with exact length on **59/59** shipped files. [DATA] *n* = W/2 cells per side, *T* = W tiles
per side. Field meanings are [STATIC] unless marked; "(saved, OPEN)" means preserve the value without
interpreting it.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | recVersion | 3 in all files |
| 0x02 | 2 | u16 | streamVersion | 2, or 3 (`.sct` files and `Seoul World Cup.SNR`) |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4+21 | marker | start marker | **Only if streamVersion ≥ 3:** u32 length 21 + ASCII `Pollution Layer Start` |
| … | 4·*n*² | u32[] | state | Cell state words (7.2.1). Rows stored last row first. |
| … | 37 | struct | garbageStore | Landfill store `0xA2C9E821` (`16-sim-utilities.md` §4.4) in the valve layout of `15-sim-growth.md` Persisted state **without** the ID: s32 `Slast`, s32 `Dlast`, s32 `Sacc`, s32 `Dacc`, f32 `f1`, f32 `f2`, u8 `capPct`, s32 `V`, s32 `Sbase`, s32 `Dbase`. `D` = landfill room, `S` = pending garbage; `Dlast` = `Dacc` and `Slast` = `Sacc` in every played city [STATIC][DATA] |
| … | 2·*T*² | u16[] | standingGarbage | Per tile (7.2.1). Rows stored last row first. |
| … | 56 | s64[7] | garbageTotals | Seven 64-bit garbage-flow counters over the last 12 months: [0] produced (incl. imports), [1] imported, [2] exported, [3] recycled, [4] incinerated, [5] waste-to-energy, [6] placed on landfill (`16-sim-utilities.md` §4.5). Each equals the sum of the 12 history entries below [STATIC][DATA: Liverpool, Berlin, Craterville exact] |
| … | 4 | u32 | avgAir | Last completed sweep's average air: ⌊sumAir ÷ pollutedCount⌋ when pollutedCount > 0 and sumAir > 0, else 0 (both running values as below, taken when the sweep finalises) [STATIC]. Read by the patient count (`20-sim-population.md` §5.1) and the toxic-cloud score (`21-disasters.md` §6.2) |
| … | 4 | u32 | avgWater | Same with sumWater [STATIC]. Read by the patient count |
| … | 2 | u16 | – | 0 in all files (OPEN) |
| … | 4 | s32 | sumAir | Running Σ*S_A* |
| … | 4 | s32 | sumWater | Running Σ*S_W* after treatment |
| … | 4 | u32 | totalStanding | Total standing garbage on tiles |
| … | 4 | u32 | pollutedCount | Running polluted-cell count (+4 per cell) |
| … | 132 | u32[33] | chart | History chart (12 monthly + 10 yearly + 10 decade + u32 parameter), same as the crime record. Monthly sample = min(100, (⌊avgWater × 100 ÷ `MaxWaterPolluteForUI`⌋ + ⌊avgAir × 100 ÷ `MaxAirPolluteForUI`⌋) ÷ 2), unsigned integer arithmetic, pushed in the pollution month-end. In January the current value is also pushed as the yearly value, and every 10th year as the decade value [STATIC] |
| … | 56 | s64[7] | garbageMonth | Current-period values of the 7 counters |
| … | 672 | s64[12][7] | garbageHistory | 12 periods × the 7 counters, period-major |
| … | 1 | u8 | – | 0 in all files (OPEN) |
| … | 4 | u32 | burnCapA | Incinerator burn capacity total [STATIC] |
| … | 4 | u32 | burnCapB | WTE burn capacity total [STATIC] |
| … | 4 | f32 | recycleRatio | City recycling ratio *r* (`16-sim-utilities.md` §4.3) [STATIC] |
| … | 4 | u32 | – | (saved, OPEN) |
| … | 4 | u32 | cursorX | Next cell, tile x (= 2 × cell x) |
| … | 4 | u32 | cursorZ | Next cell, tile z |
| … | 4 | s32 | rawWaterSum | Running Σ raw *S_W* (before treatment) |
| … | 4 | s32 | rawWaterLast | Last sweep's raw total [INFERRED] |
| … | 4 | s32 | treatedSum | Running Σ treated |
| … | 4 | s32 | treatedLast | Last sweep's treated total [INFERRED] |
| … | 42+2*n*² | iterator | sweepOrder | Same format as the crime record's sweep-order block (§7.3, from ordState to stride) |
| … | 4 | u32 | landfillTiles | Number of registered landfill tiles |
| … | 4·*n*² | s32[] | waterSource | Rows stored last row first |
| … | 4·*n*² | s32[] | airSource | Rows stored last row first |
| … | 2·*n*² | s16[] | garbageSource | Rows stored last row first |
| … | 4 | u32 | k | Modifier entries (73–95 in shipped files) |
| … | 18·k | entry[] | modifiers | Ascending ID: u32 ID, s32 air, s32 water, u8 rA, u8 rW, s32 garbage |
| … | 4+8·m₀ | list | waterTreatment | u32 count, then (u32 tileX, u32 tileZ) per plant (12820) |
| … | 4+8·m₁ | list | incinerators | Same (12852) |
| … | 4+8·m₂ | list | wtePlants | Same (12853) |
| … | 4+8·m₃ | list | recyclingCentres | Same (12851) |
| … | 4+19 | marker | end marker | **Only if streamVersion ≥ 3:** u32 length 19 + `Pollution Layer End` |

Size for streamVersion 2 = 8 + 4*n*² + 37 + 2*T*² + 983 + (42 + 2*n*²) + 4 + 10*n*² + 4 + 18*k* + Σ(4 + 8*m*ᵢ).
[DATA]

## 8. Aura (mayor rating)

The aura layer's save record has group **0xC259C02D** (§8.6). The constant 725-byte record of
group 0xE11BDDF6 is the *world layer* (`SC3WorldLayer.ini`: `JobToResFactor`, `ExternalJobDemand`,
`EconomicPhases`); it is owned by `15-sim-growth.md`, not by the aura. [STATIC; DATA: sizes in §8.6]

### 8.1 Tunables (`SC3Aura.ini`)

All values are parsed as unsigned integers and then narrowed to the stated storage type, so a
negative INI value becomes the two's-complement byte/short. [STATIC]

| Key | Stored as | Symbol | Use |
|---|---|---|---|
| `UpdatePeriod` | u16; 0 → 1 | *T* | Runs per full sweep (§8.3) |
| `OptimalPoliceCoverage` | u8 | *P₀* | Police coverage giving the best effect |
| `OptimalPoliceCoverageEffect` | s8 | *E₀* | Aura at coverage *P₀* |
| `OppressivePoliceCoverageEffect` | s8 | *E_hi* | Aura at coverage 255 |
| `InadequatePoliceCoverageEffect` | s8 | *E_lo* | Aura at coverage 0 |
| `CrimeFactor` | s8; 0 → −1 | *F_c* | Divisor for crime |
| `FireCoverageFactor` | s8 | *F_f* | Divisor for fire coverage (0 is not guarded: division by zero) |
| `AirPollutionFactor` | s16 | *F_a* | Divisor for air pollution |
| `WaterPollutionFactor` | s16 | *F_w* | Divisor for water pollution |
| `StandingGarbageFactor` | s8 | *G* | Added on tiles with standing garbage |
| `RadiationFactor` | s8 | *Rad* | Added on irradiated tiles |
| `HighEQFactor`, `LowEQFactor` | s8 | *K_EQhi*, *K_EQlo* | EQ terms |
| `HighEQThreshold`, `LowEQThreshold` | u8 | *Q_hi*, *Q_lo* | EQ thresholds |
| `HighLEFactor`, `LowLEFactor` | s8 | *K_LEhi*, *K_LElo* | Life-expectancy terms |
| `HighLEThreshold`, `LowLEThreshold` | u8 | *L_hi*, *L_lo* | Life-expectancy thresholds (years) |
| `ParkingFineOrdEffect` | s8 | | Added while ordinance 0x015B4D1E is enacted |
| `AltDrivingOrdEffect` | s8 | | Added while ordinance 0xE2BF1E2A is enacted |
| `NukeFreeOrdEffect` | s8 | | Added while ordinance 0xE0D0722E is enacted |

`NoiseFactor` is present in the shipped file but is **never read**; there is no noise term.
`[PoliceStation] OppressionThreshold` is not used by the aura. [STATIC]

**Number-parsing quirk.** This applies to every integer the aura layer reads, including
`[AgentIDModifiers]`. A value string is parsed in base 16 if it starts with `0x`/`0X` **or if it
contains any of the letters a–f / A–F anywhere**, including in trailing comment text. Otherwise it
is parsed in base 10. Parsing stops at the first character that is not a digit of that base. A
leading `-` is accepted and the result wraps modulo 2³². [STATIC] Consequence for the shipped data:
in `[AgentIDModifiers]` the second number is read from a string that still holds the trailing
comment, so `19999=30,30	Quigley Castle` yields an effect of 0x30 = 48. Every other shipped second
number is a single digit and is unaffected. Reproduce this.

### 8.2 Per-building aura modifiers (`[AgentIDModifiers]`)

Entry syntax: *id*`=`*r*`,`*e* [trailing text]. [STATIC]

* *id* is the key: an occupant ID or an occupant group ID.
* *r* is the text before the first comma, stored as a **u8 radius in tiles** (so −1 becomes 255).
* *e* is the text after the comma, stored as an **s8 aura effect** (sign-extended).
* Entries with *e* = 0 are discarded.

Modifier grid: one signed 16-bit accumulator per **aura cell** (4 × 4 tiles, addressed by tile
coordinate as in §2.1). When an occupant is added to (or removed from) the city: [STATIC]

1. If the occupant has a construction/operational state property and that state is not 1, the
   occupant is ignored. [INFERRED: only finished, functional buildings count]
2. Look up the occupant's agent type in the modifier table. On an exact miss, walk the table in
   ascending key order and take the first entry whose key is an ancestor of the type in
   `CitySim.ini [AgentTypeHierarchy]` (the **lowest-numbered** ancestor, not the nearest; land
   value, `15-sim-growth.md` §6.4, uses the same rule). Cache the result, or zero if nothing
   matched, under the occupant's own type. [STATIC]

   The four `[AgentIDModifiers]` tables therefore use two different fallback rules in the
   original: nearest ancestor (crime, pollution) and lowest-numbered ancestor (aura, land value).
   Reproduce both.
3. If *e* ≠ 0, take the occupant's lot rectangle [x₀..x₁] × [z₀..z₁], grow it by *r* tiles on every
   side, and clip it to [0, W−1] × [0, H−1]. For **every tile** of that rectangle, add *e* to the
   modifier accumulator of the tile's cell (on removal, subtract *e*). A cell fully inside the
   rectangle therefore gains 16 *e*.

The modifier grid is not saved; it is rebuilt from the buildings on load. [INFERRED from the record
layout]

### 8.3 Sweep schedule

Run frequency: see §1 (once per simulated day on the daily cascade). Each run processes *N* = ⌊W·H / (16·*T*)⌋ cells, so a full
sweep takes *T* runs (shipped *T* = 50).

The cursor is a phase *k* plus an anchor tile *(cx, cz)*. For each phase it walks the anchors
(oₓ + 16i, o_z + 16j), x fastest. The phase offsets come in this order: [STATIC]

(4,8), (0,12), (4,0), (8,12), (12,8), (0,8), (0,4), (12,4), (4,12), (4,4), (12,0), (8,8), (8,4),
(12,12), (8,0), (0,0).

This is the aura's own table; it differs from the police table in §3.3. The cursor advances after
each cell as follows:

1. *cx* += 16. If *cx* < W, stop.
2. *cz* += 16. If *cz* ≥ H:
   a. *k* += 1;
   b. if *k* > 15, set *k* = 0 and finalise the sweep (§8.5);
   c. set *cz* = o_z(*k*).
3. Set *cx* = oₓ(*k*).

A new layer starts at *k* = 0, *(cx, cz)* = (4, 8). [STATIC; DATA: every untouched map stores
exactly this]

### 8.4 Cell value

Before the per-cell loop, each run reads the three ordinance flags and precomputes two
single-precision slopes: *s_hi* = (*E_hi* − *E₀*) / (255 − *P₀*) and *s_lo* = (*E₀* − *E_lo*) / *P₀*.
For a cell with anchor (*cx, cz*): [STATIC]

1. *S* = the modifier accumulator of the cell (§8.2); *n* = 0.
2. For each of the cell's 16 tiles (*cx* + (i mod 4), *cz* + ⌊i/4⌋), i = 0…15:
   a. The tile counts only if the occupant query for the tile succeeds, and either the query
      returns no occupant or the occupant reports **occupant kind** 1 (`13-buildings-occupants.md`
      Occupant kind). Any other kind is skipped and contributes nothing. Empty tiles therefore
      **do** count; the gate is not the lot-state word (1 = occupied, etc.) and is not attribute
      0x72. Occupant kind is a fixed per-class value, so a kind-1 building still counts when
      abandoned or under construction. Network pieces (kind 2) and flora (kind 5) on a tile
      therefore skip that tile; empty tiles still count. [STATIC]
   b. Police: let *p* be the police coverage at the tile (0–255, the displayed byte of §2). If
      *p* > *P₀*, *v* = (*p* − *P₀*)·*s_hi* + *E₀*; otherwise *v* = *E₀* − (*P₀* − *p*)·*s_lo*. Add
      round-to-nearest(*v*), ties to even.
   c. Add trunc(*f* / *F_f*), where *f* is the fire coverage at the tile (0–255).
   d. Add trunc(*c* / *F_c*), where *c* is the crime at the tile (0–255).
   e. Add trunc(*air* / *F_a*) and trunc(*water* / *F_w*), using the pollution layer's air and water
      values at the tile (`SC3Pollution.ini` scale).
   f. Add *G* if the tile's standing-garbage amount is non-zero. Add *Rad* if the tile is
      irradiated.
   g. EQ term, with *q* = the city's mean EQ over ages 0–89 (a byte, `20-sim-population.md` §4):
      * if *q* ≥ *Q_hi*, add trunc((*q* − *Q_hi*) × *K_EQhi* / (150 − *Q_hi*));
      * otherwise, if *q* ≤ *Q_lo*, add trunc((*Q_lo* − *q*) × *K_EQlo* / *Q_lo*).

      The 150 is a hard-coded EQ ceiling.
   h. Life-expectancy term, with *L* = the city's life expectancy in years (a byte,
      `20-sim-population.md` §5):
      * if *L* ≥ *L_hi*, add trunc((*L* − *L_hi*) × *K_LEhi* / (89 − *L_hi*));
      * otherwise, if *L* ≤ *L_lo*, add trunc((*L_lo* − *L*) × *K_LElo* / *L_lo*).

      The 89 is hard-coded.
   i. Add the effect of each enacted ordinance (§8.1).
   j. *n* += 1.
3. Cell value *A* = 0 if *n* = 0; otherwise *A* = clamp(trunc(*S* / *n*), −127, 127), a signed
   byte. *A* replaces the cell's previous value.
4. Sweep statistics: Σ += *A* · *n*; count += *n*.

All divisions are signed integer divisions that truncate toward zero. Terms g–i are city-wide, but
they are added once per counted tile, so they shift the cell average by a uniform amount. [STATIC]

Shipped-value illustration: optimal police coverage (100) gives +20, no police gives 0, and
saturated police (255) gives −10. Full crime (255) gives −63. Air pollution 300 gives −1.

### 8.5 City aura (mayor rating)

At the end of each full sweep, city aura = trunc(Σ / count), stored as a signed byte (0 if
count = 0). Σ and count then reset to 0. [STATIC]

* This value is the city-wide aura (mayor rating) used by the advisors
  (`22-advisors-news-petitioners.md`, `Advisor-Demographics.ini` `CITY_AURA_*`) and by the graphs.
* The per-cell grid is shown as the aura map. No growth rule reads it: neither the developers
  nor land value (`15-sim-growth.md` §4, §6) have an aura input. [STATIC for those inputs]

History chart: at each month boundary the chart samples (city aura + 127) into a 12-entry monthly
series. In January it also pushes a yearly sample, and in January of years divisible by 10 it
pushes a decade sample. [STATIC for the cadence. The +127 offset is DATA: in all 59 files the
newest monthly sample equals the stored aura + 127]

### 8.6 Aura record `206C6E7C:C259C02D:00000000`

The record has no version/sentinel header. With C = (W/4)·(H/4), size = C + 150. [DATA: 59/59
shipped `.sc3`/`.SNR`/`.st3`/`.sct` files, including `TUTORIAL.SC3`; sizes 1174 / 2454 / 4246 for
128 / 192 / 256 maps]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | C | s8[C] | grid | Cell aura values −127…127, row-major (cell x fastest), (W/4) per row |
| C | 1 | u8 | phase | Sweep phase *k* (0–15; a loaded value > 15 is reset to 0) |
| C+1 | 4 | u32 | cursorX | Next anchor tile x (a loaded value ≥ W is reset to 0) |
| C+5 | 4 | u32 | cursorZ | Next anchor tile z (a loaded value ≥ H is reset to 0) |
| C+9 | 4 | s32 | sweepSum | Running Σ *A*·*n* of the current sweep |
| C+13 | 4 | u32 | sweepCount | Running Σ *n* of the current sweep |
| C+17 | 1 | s8 | cityAura | Last finalised city aura (§8.5) |
| C+18 | 48 | u32[12] | monthly | Chart samples, aura + 127 (new chart: all 127) |
| C+66 | 40 | u32[10] | yearly | Chart samples |
| C+106 | 40 | u32[10] | decade | Chart samples |
| C+146 | 4 | u32 | chartParam | 0x104 in all files (value given to the chart at creation) |

**Order of series entries within each array: index 0 is the newest sample** [STATIC]. Each
array is a fixed-length FIFO: a new sample is inserted at index 0 and the oldest drops off the
far end (`10-save-format.md` §6.12). The same rule applies to every history chart that uses this
layout (aura, crime, pollution, traffic, demographics, budget funds, power, water, …). Untouched
maps (terrains, fresh starter towns) store an all-zero grid, phase 0, cursor (4, 8), zero
statistics and all-127 charts. [DATA]

## Persisted state

All records live in the inner database of the main save record (`10-save-format.md`), with type
0x206C6E7C and the layer's own group ID (table below). Coverage: all 37 shipped `.sc3`, `.SNR` and
`.st3` files other than `TUTORIAL.SC3` were checked for the sizes below; the crime, pollution and
aura layouts were checked on all 59 shipped files including `TUTORIAL.SC3`. [DATA]

| Layer | Record group | Size observed |
|---|---|---|
| Police layer | 0x00ABF2EC | 34 bytes (all files) |
| Fire layer | 0xA0F42214 | 34 bytes (all files) |
| Flammability layer | 0x61448030 | 25 + W·H (one byte per tile; 16409 / 36889 / 65561 for 128 / 192 / 256 maps) |
| Crime layer | 0x20A7AE7F | 21–83 KB; layout §7.3 [DATA 59/59] |
| Pollution layer | 0xC0A81498 | 100–396 KB; layout §7.4 [DATA 59/59] |
| Aura layer | 0xC259C02D | (W/4)·(H/4) + 150; layout §8.6 [DATA 59/59] |
| (World layer, not aura; see §8) | 0xE11BDDF6 | 725 bytes in all files (owned by `15-sim-growth.md`) |

Police / fire layer record (34 bytes, no version/sentinel header): [DATA sizes; meanings STATIC/INFERRED]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | phase | Sweep phase index 0–15 (§3.3) |
| 0x01 | 4 | u32 | cursorX | Next anchor tile x (multiple of 4) |
| 0x05 | 4 | u32 | cursorZ | Next anchor tile z (multiple of 4) |
| 0x09 | 1 | u8 | striking | 1 while on strike |
| 0x0A | 4 | u32 | strikeCounter | Strike tick counter (kept after a strike ends) |
| 0x0E | 4 | u32 | sweepCount | Developed cells visited so far in the current sweep (§3.3 statistics) [STATIC for police] |
| 0x12 | 4 | u32 | sweepSumD | Σ *D* over those cells [STATIC for police] |
| 0x16 | 4 | u32 | sweepZeros | Number of those cells with *D* = 0 [STATIC for police] |
| 0x1A | 4 | u32 | avgCoverage | Last full-sweep average coverage (0–255) [INFERRED] |
| 0x1E | 4 | u32 | pctCovered | Last full-sweep coverage percent (0–100) [INFERRED] |

Correction: fields 0x0E–0x16 were earlier read as upkeep, unjailed prisoners and unknown. They are
the running sweep statistics. DATA, Berlin day 40: 1 945 cells, Σ*D* 84 177 (mean 43.3, matching
avgCoverage 43); 557 of the cells have *D* = 0. The fire record uses the same layout [INFERRED].
**Not saved** [STATIC]: the coverage accumulators (rebuilt from the stations on load) and the
unjailed-prisoner count *P* of §3.2. *P* starts at 0 after a load, so the `NoJailPenalty` of §3.3
is off until the first redistribution leaves prisoners unjailed. Jail inmates are saved with
each jail (`10-save-format.md` §6.2.5).

## Open questions

* Aura non-kind-1 classes closed: network pieces = kind 2, flora = kind 5 (`13` Occupant kind;
  §8.4). (Chart series order closed: index 0 = newest, §8.6.)
* Education funding third list closed: libraries and museums share it (§6.2).
* RCI indicator demand unit closed: *u* = *N*/1000, full slot |*u*| = 1 (`09` §4.5).
* `garbageStore` block of the pollution record closed (§7.4 / `16` §4.4). (Police record fields
  closed, see Persisted state.)
* Fire strike closed: identical machine to police, avg flammability vs `MaxAcceptableFlam` (§3.4).
* `GarbageScalingFactor` closed: query-UI scale factor *s* only (§7.2.2 / `16` §4.1); not disposal.
* Pollution cell bit 31 closed: unused in normal play; clear in all shipped cities (§7.2.1).
* Flammability record trailer fields beyond the visit-order cursor (closed: visit order is
  row-major x-fastest with a continuing cursor, §4).
* Agent-type distance closed: parent-step count (`13` §4.1 / §7.1.2).
