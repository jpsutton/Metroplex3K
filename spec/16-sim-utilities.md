# 16 — Simulation: Utilities (power, water, garbage)

Status: working draft. Provenance tags as defined in `00-README.md`.

This document specifies the power, water and garbage subsystems: their tunables, the per-building
supply/consumption rules, plant ageing and failure, and the monthly update. Network connectivity
(which tiles a power line or pipe reaches, how conduction through buildings works) is owned by
`12-networks.md`; this document consumes the result as "tile *t* is connected to utility network *n*".
Prices of neighbour deals are in `19-sim-economy.md`. Pollution layers that utilities write into are
in `17-sim-services.md`.

## 1. Common facilities

### 1.1 Random number generator

The simulation uses the pseudo-random generator of `14-sim-clock.md` §8 (one generator per
subsystem). A generator has two independent 32-bit states, *S* (integer stream) and *T* (real
stream). At creation **both** are set to the same seed: the supplied seed, or the system
millisecond tick count when the seed is −1 (the simulation always uses −1). [STATIC]

Integer stream, `Rand(n)` for an unsigned bound *n*:

1. If *n* = 0 the result is 0 and the state is not advanced.
2. Advance: form the 64-bit value *V* = *S* × 0x41C64E6D + 0x3039, computed without overflow. The new
   state is *S* ← *V* mod 2³². The raw output *R* is bits 16–47 of *V*, i.e. ⌊*V* / 2¹⁶⌋ mod 2³². This
   is the same generator as `14-sim-clock.md` §8.
3. If *n* < 0x00FFFFFF, the result is *R* mod *n*.
4. Otherwise let *m* = 2^(⌊log₂ *R*⌋+1) − 1 (all bits up to the highest set bit of *R*, i.e. the mask
   is built from *R*, not from *n*); take *R* AND *m*; if that exceeds *n*, go back to step 2; else
   return it. Because *R* AND *m* = *R*, the mask has no effect: the result is the first raw output
   *R* with *R* ≤ *n*, so this branch returns values in [0, *n*] **inclusive**, unlike step 3,
   which returns [0, *n* − 1]. [STATIC; original behaviour, reproduce as described]

`RandInt(a, b)` = *a* + `Rand(b − a)`, i.e. uniform on [*a*, *b* − 1] (and *a* when *a* = *b*). [STATIC]

Real stream, `RandUnit()`: if *T* = 0 it is first set to 0x12345678; then *T* ← *T* × 0x278DDE6D mod 2³²
and the result is *T* / 2³² (treating *T* as unsigned), a value in [0, 1). `RandReal(a, b)` = *a* +
(*b* − *a*) × `RandUnit()`. [STATIC]

Because the seed is time-derived, bit-exact replay of random events is neither possible nor required;
the stream definitions are given so that distributions match. [INFERRED]

### 1.2 Calendar ages

All facility ages are in **simulation months**: age = (12 × current year + current month) − (12 × build
year + build month). Newly constructed facilities record the current date as build date. [STATIC]

## 2. Power

### 2.1 Tunables (`SC3PowerLayer.ini`)

| Section / key | Units | Meaning |
|---|---|---|
| `[PowerPlantCapacities]` *agentType*`=`*n* | power units per month | Nominal output of a plant of that occupant type. Keys are decimal agent type IDs (`13-buildings-occupants.md` §3.2): 12111 coal, 12112 oil, 12113 gas, 12114 microwave, 12115 nuclear, 12116 fusion, 12117 solar, 12118 wind, 12853 waste-to-energy. [DATA] |
| `[MiscPowerLayerTunables] SavingDueToPowerConservationOrdinance` | percent | Reduction of total city demand while the Power Conservation ordinance is in force. [STATIC] |
| `[MiscPowerLayerTunables] SavingDueToStairwellLightingOrdinance` | percent | Reduction of total city demand while the Stairwell Lighting ordinance is in force. [STATIC] |
| `[MiscPowerPlantTunables] MinOverworkMonths`, `MaxOverworkMonths` | months | Bounds of the random overwork tolerance (see 2.5). [STATIC] |
| `[<Type>PowerPlant] MaxLifeSpan` | months | Nominal lifetime. A value of 0 is replaced by 1. [STATIC] |
| `[<Type>PowerPlant] DeclineAge` | months | Age at which output begins to fall. If it is ≥ `MaxLifeSpan`, it is replaced by ⌊`MaxLifeSpan`/2⌋. [STATIC] |
| `[<Type>PowerPlant] PercentageVariationInLongevity` | percent | Per-plant random spread of both ages (see 2.3). Stored as a fraction (value × 0.01). [STATIC] |
| `[WasteToEnergyPowerPlant] OptimalGarbageCap` | garbage units/month | Garbage the WTE plant can burn per month (see 4.1, 4.3). [STATIC] |

The `<Type>` section names are `CoalPowerPlant`, `OilPowerPlant`, `GasPowerPlant`,
`MicroWavePowerPlant`, `NuclearPowerPlant`, `FusionPowerPlant`, `SolarPowerPlant`, `WindPowerPlant`,
`WasteToEnergyPowerPlant` (exact spelling, including the capital W in `MicroWave`). The key is spelled
`MaxLifeSpan` (capital S) in this file, unlike `MaxLifespan` in `SC3Tune.ini`. [STATIC]

Each file is read first as a loose `\Sys\SC3PowerLayer.INI`, then from `Sys\SYS.PAK`. A key that is
absent leaves the built-in default (0). [STATIC]

### 2.2 Plant state

Each power plant carries: nominal capacity *C* (from `[PowerPlantCapacities]` by its agent type),
current output *O*, current assigned load *L*, overwork counter *W* (months, 16-bit), build date,
lifetime *Lmax*, decline age *Ld*, and a "has been warned/notified" flag. [STATIC]

### 2.3 Lifespan randomisation (on construction)

Let *v* = `PercentageVariationInLongevity` / 100. [STATIC]

* If *v* = 0: *Lmax* = `MaxLifeSpan`, *Ld* = `DeclineAge`.
* Otherwise draw one factor *f* = `RandReal(1 − v, 1 + v)` and set *Lmax* = trunc(`MaxLifeSpan` × *f*),
  *Ld* = trunc(`DeclineAge` × *f*) (the same *f* for both).
* Finally, if *Ld* ≥ *Lmax*, set *Lmax* = *Ld* + 1.

### 2.4 Output with age

With age *a* in months (1.2): [STATIC]

| Condition | Output *O* |
|---|---|
| *a* ≥ *Lmax* | 0 (output exhausted; retirement is a separate test, 2.6 step 4) |
| *Ld* < *a* < *Lmax* | *C* − ⌊*C* × (*a* − *Ld*) / (*Lmax* − *Ld*)⌋ (unsigned integer arithmetic) |
| *a* ≤ *Ld* | *C* |

The plant query window shows age and lifetime in whole years (months ÷ 12, truncated). [STATIC]

### 2.5 Overwork and explosion

Each month the power layer assigns the plant a load *L* (2.6). When *L* is assigned: if *L* > *O* then
*W* ← *W* + 1, otherwise *W* ← 0. [STATIC]

Overwork check (performed for each plant in the monthly update): draw *r* = `RandInt(MinOverworkMonths,
MaxOverworkMonths)`; if *r* < *W*, the plant explodes. [STATIC] With shipped values (18, 30) a plant
cannot explode before 19 consecutive overloaded months and is certain to by the 30th. [INFERRED]

Explosion: an explosion visual effect and sound are spawned at the plant's lot, the plant is
removed and replaced by rubble, and the advisors and news ticker are told which plant type
exploded and where. [STATIC] (Fire spreading from the explosion, if any, is in `21-disasters.md`.)

Expiry: at *a* ≥ *Lmax* output is 0. The player may "rebuild" an expired plant: a new building of the
same occupant type is placed on the same lot with a fresh build date. [STATIC; the exact UI prompt
is in `22-advisors-news-petitioners.md`/`09-ui-screens.md`]

### 2.6 Monthly power update

The power layer holds a per-tile **powered bit** (1 bit per tile, row-major, 32 tiles per word) and a
per-tile **consumption byte** (power units/month; each conducting occupant writes its whole
consumption into the cell of its anchor tile only, all other cells stay 0, `12-networks.md` §9.1).
[STATIC] Which building supplies the consumption value is [OPEN] (see Open questions).
Connectivity of power lines, conductive buildings and zones is defined in `12-networks.md`; the
power layer computes a connected component by repeated row-wise flood expansion, capped at 600
expansion passes. [STATIC]

Each month, in this order: [STATIC]

1. Read whether the Power Conservation and Stairwell Lighting ordinances are in force (ordinance
   IDs 0x82B9999B and 0x82E42E30 respectively, as in `19-sim-economy.md` §9.5 [DATA]).
2. Load the month's neighbour power deals: up to four slots, one per neighbour, each with a
   purchased amount (`19-sim-economy.md`). Advance a 12-month rolling history of the layer's
   statistics.
3. Reset the month's statistics (produced, consumed, consumer tiles, powered consumer tiles) and
   clear all powered bits and all "reached" marks.
4. Per-plant monthly step, for every plant in list order:
   * If the plant is flagged expired, only recompute its output (2.4).
   * Otherwise run the overwork check (2.5); if it explodes, stop processing that plant.
   * Otherwise, if age > *Lmax* (strictly greater, so a plant spends exactly one month at age
     *Lmax* with output 0 before it is retired), the plant is **retired**: its occupant is replaced in place by the
     occupant's expired variant, which inherits *Lmax* and *Ld* and is flagged expired [STATIC;
     the visual variant and its zero output are INFERRED].
   * Recompute output *O* (2.4).
5. Count functional plants whose age ≥ *Ld* ("ageing plants"; reported to advisors).
6. For each plant not yet reached by an earlier pass, run **distribution** (2.7) seeded at the
   plant's tile. Then, for each neighbour connection whose slot has a non-zero deal amount and that
   was not reached, run distribution seeded at the connection tile.
7. Post-processing: buildings on unpowered tiles are flagged unpowered (consumed by
   `15-sim-growth.md`); statistics are added to the rolling history.
8. Spare capacity % = ⌊(100·produced − 100·consumed) / produced⌋ if produced > consumed, else 0.
   Unpowered consumers = total consumer tiles − powered consumer tiles (≥ 0).
9. Notify the UI and advisors that the power statistics have changed.

### 2.7 Distribution within one network (supply allocation and brown-outs)

Given a seed tile: [STATIC]

1. Compute the connected component *K* containing the seed.
2. *S* = Σ *O* over plants whose tile is in *K* and which are not yet marked reached; mark them
   reached; produced += *S*.
3. Mark each neighbour connection whose tile is in *K* as reached; its slot is "active".
4. If **no** neighbour connection is active in *K*, *S* ← trunc(1.1 × *S*) (a hard-coded 10 % margin
   applies only to networks without a neighbour connection).
5. *D* = Σ deal amounts of the active slots.
6. *k* = 1 − (*p*_cons if Power Conservation) − (*p*_stair if Stairwell Lighting), *p* = key/100.
7. Budget *B* = trunc((*S* + *D*) / *k*).
8. Visit the tiles of *K* in row-major order (z ascending, then x ascending), within the rows spanned
   by *K*. For each tile with consumption *c*:
   * *c* = 0: set its powered bit.
   * *c* > 0: count it as a consumer, set its powered bit; if *c* ≤ *B*, *B* ← *B* − *c*. Otherwise
     (**brown-out**) *B* ← 0, the active deal slots are zeroed, **all remaining tiles of *K* in scan
     order stay unpowered**, consumed += *S* + *D*, and distribution ends.
9. If the scan completes, leftover *L* = trunc(*B* × *k*); consumed += *S* + *D* − *L*. The leftover is
   credited back to the active slots (first against power sold, then against power bought) so that
   unused purchased power is reported as unused. [STATIC; reporting use INFERRED]

Consequences: when demand exceeds supply, the tiles that lose power are those **last in row-major
order** (largest z, then largest x) of the network; the tile on which the budget runs out is itself
still powered. Each plant counts only towards the first network that reaches it.

## 3. Water

### 3.1 Tunables (`SC3WaterLayer.ini [TuningParameters]`)

| Key | Units | Meaning |
|---|---|---|
| `WaterFromPipe` | tiles (16-bit; 0 → 3) | Reach of water service from a pipe: the piped area is dilated by this many tiles (3.4). [STATIC] |
| `WaterPumpDistanceFactor` | percent | Output of a pump whose nearest fresh water is two tiles away (3.3). [STATIC] |
| `WaterTowerCapa`, `WaterPumpCapa`, `DesaltPlantCapa` | water units/month | Nominal output of a water tower, pump and desalination plant. [STATIC] |
| `WaterTowerLife`, `WaterPumpLife`, `DesaltPlantLife` | **years** | Lifetime *L*. The facility gets *Lmax* = 12·*L* months and *Ld* = 6·*L* months (no random spread). [STATIC] |
| `WaterConservationFactor` | percent | Demand reduction under the water-conservation ordinance. [STATIC] |
| `WaterMeterFactor` | percent | Demand reduction under the water-meter ordinance. [STATIC] |
| `DefaultConsumption` | water units per tile | Consumption for occupants not listed in `[AgentIDModifiers]`. [STATIC] |
| `DefaultLeak` | — | Read but no consumer found. [OPEN] |

`[AgentIDModifiers]`: *agentType*`=`*n* (text after the number is a comment). At most 127
entries are kept; they are sorted by ID and looked up by binary search. A building's monthly
consumption is *n* × (lot width × lot depth in tiles); buildings without an entry use
`DefaultConsumption` × footprint. [STATIC]

`SC3Tune.ini [WaterTreatment]`: `OptimalPollutionCap` (water-pollution units the plant removes per
month at full funding), `MaxLifespan`, `DeclineAge` (months). [DATA; treatment behaviour OPEN]

### 3.2 Facility kinds

There are three water-facility kinds: water tower, water pump and desalination plant (capacities
`WaterTowerCapa`, `WaterPumpCapa`, `DesaltPlantCapa`). [STATIC]

### 3.3 Facility output (monthly)

For each facility, in list order: [STATIC]

1. If the facility is not functional, or is retired this month, its output is 0. A water facility
   is retired when age ≥ *Lmax* (greater **or equal**; power plants use strictly greater, 2.6) and
   is then replaced by its expired variant as for power plants. [STATIC; the two tests really
   differ in the original]
2. If its tile is not powered (`16` §2), output is 0.
3. Pumps: examine the **ring** of tiles at Chebyshev distance 1 around the pump tile (the 8 border
   tiles of the 3 × 3 box). If any is fresh (non-salt) water: output = capacity, status "good".
   Otherwise examine the ring at distance 2 (border of the 5 × 5 box): if any is fresh water, output =
   ⌊capacity × `WaterPumpDistanceFactor` / 100⌋, status "far"; else output 0, status "no water".
   A ring tile qualifies when it is inside the map, is a water tile, and its **salt flag** equals
   the wanted value (clear for pumps, set for desalination plants) [STATIC]. The salt flag is
   **not** derived from the shape of the water body: it is the stored sea-water bit, bit 30 of
   the tile's 2 × 2 pollution cell (`17-sim-services.md` §7.2.1), which the terrain sets when a
   sea surface is written [INFERRED, medium: the flag source is a per-cell flag of the
   pollution layer]. Consequences: in the 40 shipped files without the bit set, every water tile
   is fresh and pumps work on the sea; in the 19 files with the bit, pumps next to sea cells get no
   water. An implementation must use the saved bit, not a geometric "edge-connected body ≥ 8 wide"
   rule. Water tile: a tile is water when, at any of its four corner vertices, the water-surface
   altitude is greater than or equal to the ground altitude (the same test as the land-value
   water bonus, `15-sim-growth.md` §6.3 step 1) [STATIC for land value; INFERRED, medium, that the
   pump ring uses the same terrain test].
   **Conditions that zero a pump, tower or desalination plant**, in order [STATIC]: (a) the
   facility is not functional (its lot state, `0x200` removed, is not 1) or it is retired this
   month (age ≥ *Lmax*); (b) its tile is unpowered; (c) the ring test fails (pumps,
   desalination); (d) the water-pollution cut of step 5 (pollution at the facility tile ≥ *m*);
   (e) the facility's trip failed twice (`18-sim-transport.md` §9.2 "utility facility"
   [INFERRED for water facilities; STATIC for garbage facilities and recycling centres]). The saved status property (`10-save-format.md` §6.2.5,
   `8351FDF5`) is only written by the ring test, so a pump zeroed by (a), (b), (d) or (e) keeps
   status 0 ("good"). DATA: Madrid has 40 pumps with output 0 and status 0, all built on the same
   day as 42 producing pumps, so age is not the cause; check (b), (d) and (e).
4. Desalination plants: examine the border of the box from (x − 1, z − 1) to (x + 3, z + 3) (the ring
   around its 3 × 3 lot anchored at (x, z)); any **salt** water tile → full capacity, else 0.
5. Water pollution: let *p* be the water-pollution value at the facility tile (0–32767,
   `17-sim-services.md` §7.2) and *m* a city-wide water-pollution maximum reported by the pollution
   layer [STATIC]; *m* is the constant `SC3Pollution.ini MaxWaterPolluteForUI` (shipped value:
   11000) [INFERRED, medium; the alternative is the running maximum of the water grid]. If *m* ≠ 0: *p* ≥ *m* → output 0; else output ←
   ⌊(*m* − *p*) × output / *m*⌋. The mean *p* over facilities is kept for reporting.
6. Ageing: if age > *Ld*, output ← output − ⌊(age − *Ld*) × output / (*Lmax* − *Ld*)⌋, and the facility
   counts as ageing.

Towers need neither water adjacency nor anything but power. [STATIC]

### 3.4 Distribution (monthly)

1. *k* = 1 − (`WaterConservationFactor`/100 if that ordinance is active) − (`WaterMeterFactor`/100 if
   active). [STATIC]
2. Facility outputs are computed (3.3).
3. For each facility with non-zero output not yet reached this month, a pipe-network component is
   flood-filled from it (row-wise expansion, at most 500 passes; connectivity in `12-networks.md`). All
   producing facilities inside the component are marked reached and their outputs summed into the
   supply *S*.
4. If a neighbour water-purchase deal connection lies in the component, supply is treated as
   unlimited; a water-sale deal is served from *S* (`19-sim-economy.md`). [STATIC]
5. The component's pipe mask is dilated by `WaterFromPipe` tiles; tiles inside the dilated mask are
   serviceable.
6. Water spreads from the facility rows as an expanding front over serviceable tiles, alternating a
   downward and an upward row sweep, each sweep growing the front by one tile horizontally and
   vertically. When the front first reaches a building, the building is marked watered and its
   consumption (3.1) is charged against the budget trunc(*S* / *k*). When the budget reaches 0 the
   spread stops; buildings not yet reached are dry. [STATIC; exact sweep tie-breaking OPEN]

Hence, under shortage, the buildings farthest (along the pipe-served area) from the water sources
lose water first. [INFERRED]

## 4. Garbage

### 4.1 Tunables

| Source | Key | Meaning |
|---|---|---|
| `SC3Pollution.ini [AgentIDModifiers]` | 5th field | Garbage produced per building (per 2 × 2 cell share, see 4.2). [STATIC] |
| `SC3Pollution.ini [TuningParameters]` | `GarbageAbductionAmount` | Garbage that may silently vanish per **month** (the counter resets at every month-end, 4.3). |
| | `GarbageScalingFactor` | Stored as value × 0.001. It is **not** used by production or disposal; the per-tile reduction that earlier drafts attributed to it is the recycling ratio (4.3). [STATIC for disposal; its remaining use OPEN] |
| | `GarbagePollutedThreshold`, `MaxGarbagePolluteForUI` | Threshold for "garbage-polluted" tile tests and the scale maximum of the garbage map. |
| | `TrashPresortOrdEffect`, `TireRecyclingOrdEffect`, `BackyardCompostingOrdEffect`, `PaperReductionOrdEffect`, `IndWasteDisposalOrdEffect` | Percent reductions of garbage (4.2). |
| `SC3Tune.ini [Incinerator]` | `OptimalGarbageCap` | Garbage one incinerator burns **per pollution run** (per simulated day with the shipped `UpdatePeriod` 30) at 100 % funding; the monthly capacity is 30 × the value. Held internally as ×30 and divided by 30 per run. [STATIC; DATA: Liverpool, 3 incinerators: current-month counter 135 000 = 3 × 1 500 × 30, 12-month total 1 620 000] |
| `SC3PowerLayer.ini [WasteToEnergyPowerPlant]` | `OptimalGarbageCap` | Same for the waste-to-energy plant (per run; ×30 per month). [STATIC] |
| `SC3Tune.ini [Incinerator]`, `[RecyclingCenter]` | `MaxLifespan`, `DeclineAge` | Facility lifetime in months (`SC3Tune.ini` spells `MaxLifespan`). |
| `SC3Tune.ini [RecyclingCenter]` | `OptimalPopServed`, `OptimalPctReduction` | Population a recycling centre serves and the percent of their garbage it removes; combined into the city recycling ratio of 4.3. [STATIC] |
| `SC3Tune.ini [LandfillZoneDeveloper]` | `LandfillDecayRate` | Landfill decay (see `15-sim-growth.md`, landfill zone). |

### 4.2 Garbage production

Garbage is produced per 2 × 2-tile pollution cell from the occupants' `[AgentIDModifiers]` garbage
field, then reduced by the active ordinances, each as *g* ← *g* − trunc(*g* × effect / 100): [STATIC]

| Ordinance ID | Key | Applies to |
|---|---|---|
| 0x42BF1E18 | `TrashPresortOrdEffect` | all garbage |
| 0xC2F6E81F | `TireRecyclingOrdEffect` | all garbage |
| 0x22F6E814 | `BackyardCompostingOrdEffect` | tiles of zone types 1–2 (light and medium residential; high-density residential, zone type 3, is **not** included — original behaviour, reproduce), divided per tile of the cell |
| 0x22F6E81B | `PaperReductionOrdEffect` | tiles of zone types 5–7 (commercial), per tile |
| 0xA2F917CF | `IndWasteDisposalOrdEffect` | tiles of zone types 9–11 and 15 (industrial), per tile |

(Zone-type codes are those of the zone layer, `15-sim-growth.md`.) The per-tile variants subtract
trunc(*g* × effect / (100 × *n*)) for each qualifying tile, *n* being the number of non-landfill tiles
in the cell. [STATIC]

### 4.3 Disposal pipeline

**Period.** Disposal is not a separate monthly step. It runs inside every update run of the
pollution layer (`17-sim-services.md` §7.2.4), on the garbage produced by the cells processed in
that run [STATIC]. One full pollution sweep takes `SC3Pollution.ini UpdatePeriod` runs (shipped
value: 30), one run per simulated day. Burn capacities are the INI values **per run**; imported
garbage enters as ⌊amount / 30⌋ per run [STATIC].

**Per-run set-up** [STATIC]. At the start of a run: `A` = Σ burn capacity of the waste-to-energy
plants, `B` = Σ burn capacity of the incinerators (each facility's funded, aged capacity for one
run; a facility whose trip failed contributes 0, see below). If a garbage **import** deal (deal
kind 5, `19-sim-economy.md` §10.1) exists, its monthly amount `m` contributes `i = ⌊m / 30⌋`:
`i` is added to the counters *produced* [0] and *imported* [1] (§4.5), is disposed through the
stages below, and any part not disposed is added to the landfill store's pending amount `S`
(§4.4).

**Recycling ratio** `r` [STATIC] (recomputed once a month): with `P` = the city population
(`20-sim-population.md` §2), sort the recycling centres by their current reduction percentage
(descending). Walk them while `served < P`: `served += popServed_c`,
`w += pct_c × popServed_c / 100`. Then `r = w / P` if `served < P`, else `r = w / served`; `r` = 0
when there is no centre. A centre's monthly values [STATIC]:
* 0 and 0 if it is expired, if its age ≥ `MaxLifespan` (it is then retired), if its tile is
  unpowered, if its lot state is not 1, or if its trip failed twice (§4.3 "Traffic access");
* otherwise `OptimalPctReduction` and `OptimalPopServed` (`SC3Tune.ini [RecyclingCenter]`), both
  reduced linearly after `DeclineAge`: value − ⌊(age − `DeclineAge`) × value /
  (`MaxLifespan` − `DeclineAge`)⌋.
There is **no** funding scaling (correction: an earlier revision inferred one). The per-centre
properties saved with the occupant (`10-save-format.md` §6.2.5) are the unaged INI values.
`r` is saved as the f32 "ratio" of the pollution record (`17-sim-services.md` §7.4).
Test vectors: `P` = 100 000, two new centres (45 %, 50 000): served 100 000, w = 45 000 →
`r` = 0.45; one centre → `r` = 0.225. Craterville (`P` = 208 780, three centres): all three
working → 0.3233; one of them zeroed → 0.2155 (saved 0.2155), so one centre is zeroed by a
condition above, or all three are aged by the same factor √(2/3) [DATA].

**Per tile** (inside the cell update of `17-sim-services.md` §7.2.4 step 3, non-landfill tiles):
1. `q` = the tile's share of the cell's garbage; *produced* [0] += `q`.
2. Recycling: if `r > 0` and `q > 0`: `k = min(q, round(q × r))` (for the 2nd and 4th tile of the
   cell 0.5 is subtracted before rounding when `q × r > 0.5`); `q −= k`; *recycled* [3] += `k`.
3. `q += G_t` (the tile's standing garbage), the tile's standing garbage is cleared.
4. `q` goes through the **stages**, each taking as much as it can:
   1. waste-to-energy: `min(q, A)`; `A` and `q` decrease; counter *WTE* [5];
   2. incinerators: `min(q, B)`; counter *incinerated* [4];
   3. landfill: if the store's available room `V > 0` (§4.4): `min(q, V)` is added to the store's
      pending amount `S` (it is placed on landfill tiles later by the landfill developer,
      `15-sim-growth.md` §7, which counts it in *landfill* [6]);
   4. export: if a garbage export deal (kind 4) exists, **all** of the remainder; counter
      *exported* [2];
   5. abduction: up to `GarbageAbductionAmount` minus the amount already abducted **this month**
      (the abduction counter resets at every month-end) silently disappears.
5. A remainder `x > 0` becomes standing garbage:
   * if `x` < 32 767 and the store's `V < 0` (backlog): the tile also takes back part of the
     backlog, `t = min(−V, ⌊g / n⌋)` (1 if the cell's garbage is 0), capped so that `x + t` ≤
     32 767; the store's `S −= t`; standing garbage = `x + t`;
   * else if `x` < 32 768: standing garbage = `x`;
   * else standing garbage = 32 767 and `x − 32 767` is added to the store's `S` (backlog).
6. After the whole run, if the store's `V < 0`, the backlog `−V` is pushed through the stages once
   more and what they take is removed from `S`.

**Traffic access of garbage facilities** [STATIC]. Once a month each garbage facility checks the
trip result of its lot (`18-sim-transport.md` §9.1). If the last trip failed, it routes a fresh
uncounted trip (rules in `18-sim-transport.md` §9.2); if that trip also fails, the facility's
disposal capacity is 0 for the month, so its share falls through to the later stages above.

Incinerators and WTE plants emit air pollution via their `SC3Pollution.ini` entries (12852 and
12853) like any other source (§7 of `17-sim-services.md`). WTE plants also generate power (§2).

### 4.4 Landfill store

The landfill store is a valve-like accumulator (ID `0xA2C9E821`, saved as the 37-byte
`garbageStore` block of the pollution record) with **room** `D` = Σ over registered landfill
tiles of (32 767 − tile garbage), **pending** `S` = garbage routed to landfill and not yet placed,
and **available** `V` = `D − S` as of the last monthly valve update. Its maintenance (registration,
filling of tiles, decay credit) is specified in `15-sim-growth.md` §7. [STATIC; DATA: `D`
reproduced exactly from the saved tiles in Berlin and Mount Herrang]

### 4.5 Monthly garbage balance and statistics

The pollution layer keeps seven 64-bit counters [STATIC; slot labels DATA-checked]:

| Slot | Meaning |
|---|---|
| [0] | produced (tile shares, plus imported garbage) |
| [1] | imported (deal kind 5) |
| [2] | exported (deal kind 4) |
| [3] | recycled |
| [4] | burned by incinerators |
| [5] | burned by waste-to-energy plants |
| [6] | placed on landfill tiles |

At each month-end the month's values go to the 12-month history (for slot [0] the value stored is
produced − abducted), the exported amount is reported to the export deal for billing, and all
counters, including the abduction counter, restart at 0. Balance over any period:

produced [0] = recycled [3] + incinerated [4] + WTE [5] + exported [2] + abducted
+ (change of standing garbage on tiles) + (change of the store's pending `S`) + placed [6]
− (pending garbage that was routed earlier and placed in this period).

DATA (12-month block of the record): Craterville produced 2 438 840 = recycled 677 884 +
incinerated 1 619 638 + landfill 136 128 + 5 190 (abducted or left standing); Berlin produced
9 373 189 against recycled 3 230 413 + landfill 6 143 765 = 9 374 178 (989 more placed than
produced: backlog from before the period).

## 5. Update order

Power (monthly, §2.6): ordinance flags → deals → statistics reset → per-plant step (expired /
overwork-explosion / retirement / output) → distribution from each unreached plant, then from each
unreached neighbour connection with a deal → post-processing → statistics-changed notification. [STATIC]

Water (monthly, §3.4): ordinance factor → facility outputs (power check, adjacency, pollution,
ageing) → per-network distribution.

Schedule [STATIC]: power and water each do their work once per month, on day 1, as two
consecutive items of the month-day list of `14-sim-clock.md` §6.3 rule 7 (after the budget
month-end, before the month notification). Power runs first, so pumps see the current powered
bits [INFERRED, high: creation order]. Garbage disposal runs inside the pollution-layer runs (§4.3).
Police and fire coverage: daily incremental sweeps (`17-sim-services.md` §3.3).

## Persisted state

Both utility layers write one record each into the inner database of the main save record; full
byte layouts are in `10-save-format.md` §6.14 [DATA: all 59 shipped saves parse with that layout].

**Power layer `206C6E7C:E0AFDF68:0`** (version 1, 668 bytes). It stores only statistics:

* a 12-slot monthly production table of 10 categories with a current-slot index (0–11), and the
  per-category annual total, which always equals the sum of the 12 slots of that category;
* the `power %` graph history (auto-scaling, `10-save-format.md` §6.19).

The power distribution grids, per-network supply and brown-out state are **not** saved; they are
recomputed by the next monthly update (§2.6). [STATIC]

**Water layer `206C6E7C:02BF0033:0`** (version 4). It stores the per-tile water-service bitmap
(trimmed to the range of non-zero words), the `water %` graph history (fixed scale 100), the month's
total supply and consumption, the neighbour sale/purchase-deal accumulators of §3.4 step 4, the
mean facility water pollution (§3.3 step 5) and the count of ageing facilities (§3.3 step 6). The
facility list is not saved; it is rebuilt from the occupants. [STATIC]

**Per-plant and per-facility state.** Neither record contains per-plant age, lifetime (*Lmax*),
decline age (*Ld*), overwork counter (*W*) or the warned/expired flag. These belong to the plant
occupant and are saved with it as integer properties of the plant's entry in the occupant record
`E0FAADC7:A0AB89F0:1`. `10-save-format.md` §6.2.5 lists the per-class property IDs, and the
power-plant values there are named by inference (overwork counter, maximum life and decline age in
months, expired flag). The build date in the same entry gives the plant's age. If a property is
absent, re-derive *Lmax* and *Ld* (§2.3) and start *W* at 0. [STATIC layout][INFERRED names]

## Open questions

* Source of the per-tile power consumption byte (§2.6; `12-networks.md` §9.1 names occupant
  attribute 0xC9 as the leading candidate, low confidence). Recommended until answered: use
  attribute 0xC9 when present, else 0. [OPEN]
* The expired-plant variant: which occupant replaces a retired plant, and whether it ever produces.
* Water: exact row-sweep order of the spreading front; role of `DefaultLeak`; water treatment
  plant behaviour (`OptimalPollutionCap`).
* Garbage: the remaining use of `GarbageScalingFactor` (not disposal). Closed: recycling ratio (4.3), abduction resets monthly (4.3), store
  (4.4), counter labels (4.5).
* Pumps: whether the ring test uses the land-value water-tile test (§3.3 step 3, INFERRED);
  whether purchased water is included in the saved `supply` statistic.
* Which ordinance ID is Water Conservation vs Water Meters (§3.4); settle from
  `19-sim-economy.md` §9.5.
* Whether *m* in §3.3 step 5 is the constant `MaxWaterPolluteForUI` or a running maximum.
