# 18 — Traffic and transit simulation

This document specifies the traffic/trip model, congestion, mass transit (bus, rail, subway),
transit funding and strikes, the trip-result signal consumed by growth, and the statistics that the
transport advisor and UI show. Network geometry, piece tiling, bridges/tunnels and neighbour
connection *construction* belong to `12-networks.md`; vehicle sprites belong to `26-automata.md`;
the clock to `14-sim-clock.md`; zone growth to `15-sim-growth.md`; budget to `19-sim-economy.md`.

Status: work in progress. Tags follow `00-README.md`.

## 1. Overview

The traffic simulation is **trip based and stochastic** [STATIC]:

1. Every zoned building (and some civic buildings) is periodically asked to make one *trip* from
   its lot to a destination *category* (Residential, Commercial, Industrial or "any").
2. The trip is routed by a **greedy depth-first walk with backtracking** across the transport
   networks. Each step prefers the busiest adjacent network tile whose congestion is still below an
   "annoyance" threshold. Each step has a cost, and the trip fails when the accumulated cost exceeds
   a budget.
3. A trip succeeds when it reaches a network tile within walking distance of a building of the
   destination category, or when it leaves the map through a neighbour connection.
4. A successful trip that is *counted* adds traffic density to every tile of its path. Densities
   are 8-bit values per tile and decay multiplicatively once a month.
5. Success or failure is recorded for the origin lot. Growth uses this as the "can reach work /
   has access" signal (see §9).

There is no origin–destination matrix, no shortest-path search and no persistent vehicle
population. Animated vehicles are cosmetic (`26-automata.md`).

## 2. Data sources

| Source | Use |
|---|---|
| `Sys/STTraffic.INI` loose file, else the `STTraffic` entry of `Sys/SYS.PAK` | All traffic tunables, sections `[TrafficLayer]` and `[TrafficTripDestinationThresholds]` [STATIC] |
| `SC3Tune.INI` (loose first, then `SYS.PAK`) sections `RailStation`, `SubwayRailStation`, `SubwayStation`, `BusStop`, key `OptimalMonthlyUpkeep` | Per-station upkeep used by transit funding (§8) [STATIC] |
| `Advisor-Transportation.ini` | Thresholds the transport advisor applies to the statistics of §10 [DATA] |
| `SC3NetMap.ini` | Web links only. It has no role in traffic [DATA] |

### 2.1 `[TrafficLayer]` keys

Every key is optional. When a key is absent the built-in default (the "Default" column) applies.
The keys are parsed as unsigned decimal integers (a trailing `;` is ignored). "u8" means that the
parsed value is truncated to 8 bits. [STATIC] unless noted.

| Key | Type | Default | Meaning / units |
|---|---|---|---|
| `TripCellCostBaseRoad` | u8 | 4 | Base cost units for one step onto a road tile by car. Also scales the trip budget (§5.6) |
| `TripCellCostBaseRoadBus` | u8 | 3 | Base step cost for a bus on a road |
| `TripCellCostBaseHighway` | u8 | 2 | Base step cost for a car on a highway |
| `TripCellCostBaseHighwayBus` | u8 | 1 | Base step cost for a bus on a highway |
| `TripCellCostBaseBusStop` | u8 | 5 | Base step cost for entering a bus stop |
| `TripCellCostBaseRailStation` | u8 | 5 | Base step cost for entering a rail station from outside the rail network |
| `TripCellCostBaseSubwayRailStation` | u8 | 5 | Same, for the subway–rail transfer station |
| `TripCellCostBaseSubwayStation` | u8 | 5 | Same, for a subway station |
| `TripCellCostBaseRail` | u8 | 2 | Base step cost along rail track |
| `TripCellCostBaseSubway` | u8 | 1 | Base step cost along subway track |
| `TripMaxDistanceToRoadRes` | u8 | 5 | Walking radius, in tiles, around residential buildings (§4.3, §5.2) |
| `TripMaxDistanceToRoadCom` | u8 | 4 | The same for commercial buildings |
| `TripMaxDistanceToRoadInd` | u8 | 6 | The same for industrial buildings (but see the defect note below) |
| `TripMaxDistanceToRoadDefault` | u8 | 6 | Intended radius for other buildings (but see the defect note below) |
| `TripDensityAdder` | u8 | 1 | Density added per tile of the origin lot by each counted trip (§6.1) |
| `MonthlyDensityDecrement` | % | 25 | Monthly **retention** factor in percent; stored as value × 0.01 (§6.2) |
| `TrafficMaxDensity` | u8 | 255 | Ceiling for all density layers |
| `TrafficAnnoyanceLevel` | u8 | 150 | Density at or above which a tile is avoided by the route chooser |
| `TrafficDensityTripCostDivisorRoad` | u8 | 32 | Congestion divisor (density units per extra cost unit) for car on road |
| `TrafficDensityTripCostDivisorRoadBus` | u8 | 32 | The same for bus on road |
| `TrafficDensityTripCostDivisorHighway` | u8 | 32 | The same for car on highway |
| `TrafficDensityTripCostDivisorHighwayBus` | u8 | 32 | The same for bus on highway |
| `TrafficDensityTripCostDivisorBusStop` | u8 | 128 | The same for a bus stop |
| `TrafficDensityTripCostDivisorRailStation` | u8 | 128 | The same for a rail station |
| `TrafficDensityTripCostDivisorSubwayRailStation` | u8 | 128 | The same for a subway–rail station |
| `TrafficDensityTripCostDivisorSubwayStation` | u8 | 128 | The same for a subway station |
| `TrafficDensityTripCostDivisorRail` | u8 | 255 | The same for rail track |
| `TrafficDensityTripCostDivisorSubway` | u8 | 255 | The same for subway track |
| `TrafficTripThresholdPercent` | u8 % | 25 | Per-tick chance that a lot which already has a trip result makes another trip (§4.4) |
| `TripChanceCountingBase` | u8 % | 30 | Base chance that a trip is *counted* into density (§4.5) |
| `TripChanceDensityMultiplier` | % | 50 | Stored as value × 0.01. Multiplies the origin's density value in the count chance (§4.5) |
| `TrafficUseMassTransitChance` | u8 % | 50 | Chance that a commuter trip may use mass transit (§4.6) |
| `MaxEfficiencyRoads` / `MaxEfficiencyTransit` | % | 120 | Stored ×0.01, clamped to ≥ 1.0. Funding-efficiency cap (§8) [STATIC, use OPEN] |
| `MysticalERoads` / `MysticalETransit` | float | 30 | Funding-efficiency curve constant (§8) [OPEN] |
| `DamageEfficiencyMultiplier` | ‰ | 100 | Stored ×0.001. Road damage rate when roads are underfunded (§8.4) [STATIC] |
| `RepairEfficiencyMultiplier` | ‰ | 50 | Stored ×0.001. Road repair rate when roads are overfunded (§8.4) [STATIC] |
| `DamagedCostAddToStep` | int | 32 | Extra step cost on a damaged road/highway tile. Only the low 8 bits are used |
| `OptimalMonthlyFundingCostPerRoad` | % | 10 | Stored ×0.01: §/month per road tile at 100 % funding (§8) |
| `OptimalMonthlyFundingCostPerHighway` | % | 50 | Stored ×0.01: §/month per highway tile |
| `OptimalMonthlyFundingCostPerRail` | % | 10 | Stored ×0.01: §/month per rail tile |
| `OptimalMonthlyFundingCostPerSubway` | % | 50 | Stored ×0.01: §/month per subway tile |
| `PerTileTransitIncomeRail` | ‰ | 10 | Stored ×0.001: fare income (§) per counted trip that rides rail (§7.3) |
| `PerTileTransitIncomeSubway` | ‰ | 10 | Stored ×0.001: fare income per counted subway trip |
| `PerTileTransitIncomeBus` | ‰ | 1 | Stored ×0.001: fare income per counted bus trip |
| `ParkingFinesRoadMonthlyDecrementSubtract` | % | 2 | Ordinance adjustment to the road retention factor (§6.2) |
| `AlternateDayCarsRoadHighwayMonthlyDecrementSubtract` | % | 10 | The same, for road and highway |
| `CarpoolingRoadHighwayMonthlyDecrementSubtract` | % | 5 | The same, for road and highway |
| `CrossingGuardsRoadMonthlyDecrementAdd` | % | 2 | The same, added to road |
| `TouristPromotionRoadRailMonthlyDecrementAdd` | % | 3 | The same, added to all three factors |
| `ShuttleServiceDistanceToRoadAdd` | u8 | 1 | Ordinance: tiles added to every walking radius |
| `SubsidizedMassTransitUsePercentAdd` | u8 | 50 | Ordinance: points added to the mass-transit chance (saturating at 255) |
| `SubsidizedMassTransitFaresPercentDecrease` | u8 | 50 | Ordinance: fare income is multiplied by value/100 |
| `MinFundingForNoStrike` | u8 % | 100 | Transit funding below this percentage can cause a strike (§8.2) |
| `StrikeChanceAtZeroFunding` | % | 75 | Stored ×0.01. Strike-chance slope (§8.2) |
| `MaxLengthofStrikeInMonths` | int | 24 | Upper bound on strike length (§8.2) |
| `TrafficSimLobotomy` | — | — | **Not read.** The "lobotomy" switch is always off [STATIC] |
| `TrafficSimLobotomyDensity` | u8 | 128 | Read, but no consumer was found [OPEN] |

The "Default" column is the built-in value that applies when the key is missing. The shipped INI
overrides most of these. The engine must read the INI.

**Defect (must be reproduced).** `TripMaxDistanceToRoadDefault` is stored into the *industrial*
radius instead of the default radius [STATIC]. Consequences:

* the effective industrial radius is the value of `TripMaxDistanceToRoadDefault` when that key is
  present (shipped: 5; `TripMaxDistanceToRoadInd` is then ignored);
* the effective default radius is always the built-in 6.

### 2.2 `[TrafficTripDestinationThresholds]`

There are four keys: `FromResidential`, `FromCommercial`, `FromIndustrial` and `FromMixedOrAll`.
Each value is `a,b,c`: three unsigned integers in percent, each truncated to 8 bits. If a key is
missing, or does not parse as exactly three numbers, the built-in triple is used: Res `10,40,100`,
Com `70,80,100`, Ind `50,70,100`, Mixed `33,66,100` [STATIC]. The values are used as described in
§4.2. The third number is never read.

## 3. Per-tile state

The map is W × H tiles. The traffic subsystem keeps the following grids [STATIC]:

| Grid | Element | Meaning |
|---|---|---|
| Network flags | 16-bit | What transport is present on each tile (§3.1). Rebuilt from occupants; not saved |
| Surface density (live) | u8 | Accumulates this month's counted trips on road, rail track, stations and bus stops |
| Highway density (live) | u8 | The same for highway tiles |
| Subway density (live) | u8 | The same for subway tiles |
| Surface/Highway/Subway density (snapshot) | u8 ×3 | A copy taken at each monthly decay. This is what other subsystems and the map views read |
| Trip-tried bit | 1 bit | 1 = this tile belongs to a lot that has a trip result |
| Trip-ok bit | 1 bit | 1 = the last trip from the lot succeeded |
| Residential / Commercial / Industrial destination bits | 1 bit ×3 | Tile is within the walking radius of a building of that category |
| Visit mask | u8 | Scratch space for a single trip (cleared per trip) |

### 3.1 Network flag bits

| Bit | Set when | Meaning |
|---|---|---|
| 0x0001 | road occupant on the tile, or bus stop | Road |
| 0x0002 | highway occupant | Highway |
| 0x0004 | rail occupant, rail station, subway–rail station | Rail |
| 0x0008 | subway occupant, subway station, subway–rail station | Subway |
| 0x0010 | transit station building | Station / stop tile |
| 0x0020 | subway–rail station | Rail↔subway transfer |
| 0x0040 | highway on-ramp piece: network piece IDs 15130–15141 ("Highway Onramp", agent type 17202) [STATIC] | Road/highway junction tile |
| 0x0080 | road/highway piece reports itself damaged: its damaged flag (piece word bit 27, `10-save-format.md` §6.2.6) is set [STATIC] | Damaged (adds `DamagedCostAddToStep`) |
| 0x0100 | road/highway/rail piece whose record has bit 0 of attribute 0x12D set [STATIC] | Step cost × 0.7. The pieces with the bit are the diagonal ones (highway 11223, 11224, 15052, 15106; road 35, 11207, 11209, 18004; rail 50, 51) [DATA; "diagonal" INFERRED from the 0.7 ≈ 1/√2] |
| 0x0200 | subway piece with the same bit (subway 11621, 11622, 18015, 18018) | Subway step cost × 0.7 |
| 0x0400 | road tile is a neighbour connection | Road exit to a neighbour |
| 0x0800 | highway tile is a neighbour connection | Highway exit |
| 0x1000 | rail tile is a neighbour connection | Rail exit |
| 0x2000 | subway tile is a neighbour connection (query exists; the game has no subway connections) | Subway exit |
| 0x4000 | road/highway/rail piece carries a span record (bridge/tunnel). The bridge/tunnel builder sets it together with a 2-bit direction and an 8-bit length. All three are **saved** in the piece word (`10-save-format.md` §6.2.6 bits 16–26) and restored at load, so the flag comes back when the grid is rebuilt from the pieces [STATIC] | Trip jumps across the span (§5.5) |

Station occupants are recognised by occupant **type ID** [STATIC]:

| Occupant type ID | Building | Flags OR-ed into every tile of its footprint |
|---|---|---|
| 0x2FB2 (12210) | Bus stop | 0x0011 |
| 0x2FC6 (12230) | Rail station | 0x0014 |
| 0x3048 (12360) | Subway station | 0x0018 |
| 0x3049 (12361) | Subway–rail connector station | 0x003C |

When an occupant is added, its flags are OR-ed into the grid. When it is removed, they are cleared
and the grid is rebuilt for the affected area (§11). The neighbour-connection bits come from the
network subsystem's "is this tile a neighbour connection of network type N" query, with N = 1
road, 2 rail, 4 highway, 6 subway, in the network-ID numbering of `12-networks.md` (1 road,
2 rail, 3 power, 4 highway, 5 pipe, 6 subway) [STATIC].

## 4. Trip generation

### 4.1 Which buildings make trips

Each building is classified by walking up its occupant-type parent chain until one of these IDs
matches [STATIC]:

| Category | Occupant group/type IDs |
|---|---|
| 0 Residential | 3000 Residential |
| 1 Commercial | 5000 Commercial, 10000 Government, 12310 Airport, 12350 Spaceport, 18000 Landmarks |
| 2 Industrial | 8000 Industrial, 12330 Seaport |
| 3 Other ("mixed or all") | 1001 Arcology, 11000 Military, 12005 Garbage, 12101 Power plant, 13000 City/state services, 19000 Business deal |

(The names are those of `CitySim.ini` agent types [DATA]. Bus stops, stations, parking, schools
and the like belong to none of these groups, so they neither make trips nor act as destinations.)
Every occupant of a matching type is its own trip maker with its own lot: the weekly firing visits
each building occupant record once (each is a scheduler agent registered when it is added)
[STATIC]. This includes filler pieces placed with a building (parking lots, gardens and similar
fillers carry R/C/I agent types), and the 1 × 1
field pieces of farms (9300 → 8000, category 2, industrial radius) and each airport part
(12310, category 1, commercial radius) [STATIC]. There is no farm- or airport-specific trip rule.
Many of these pieces lie far from roads and fail. DATA, Madison at day ~460: 47 of 176 farm
pieces and 24 of 87 airport parts have a failed last trip. The failures have no growth effect on
airport parts, because the airport developer does not read the trip result (`15-sim-growth.md`
§7); only the R/C/I developers use it (§9.2).

The cross-reference to the occupant family tables is in `13-buildings-occupants.md`. Buildings that
match none of these IDs make no trips.

There are 16 trip generators: one per (category × week-slot 0–3). Generators are assigned **per
building type**, not per building. When the city is set up, the building types of `CitySim.ini`
are visited in the order of the agent-type registry, which is **ascending agent-type ID compared
as unsigned 32-bit numbers** (the registry is an ordered map keyed by type ID, filled from
`CitySim.ini [AgentTypes]`) [STATIC]. Each type that belongs to one of the four
categories gets the generator of its category with the next slot from a single global counter
that runs 0, 1, 2, 3, 0, … [STATIC]. Every building of that type therefore shares that type's
slot. A generator only fires during its week:

> fires iff floor(day_of_month / 7) = slot

`day_of_month` is 1-based (1–31, `14-sim-clock.md` §1). Slot 0 covers days 1–6, slot 1 days
7–13, slot 2 days 14–20, slot 3 days 21–27; days 28 and later fire no generator [STATIC].

**Schedule** [STATIC]. Trip generation is the per-building weekly work of `14-sim-clock.md` §6.1.
It runs once a week, on Mondays, before the weekly layer steps, and visits buildings in the order
they were added to the city. On each run, every building whose type's slot matches the current day
fires once (§4.4).
Consequences: each building fires on at most one Monday per month; a building in slot 0 does
**not** fire in a month whose first Monday is day 7, and nothing fires on a Monday that falls on
day 28–31. Because slots belong to types, all buildings of one type make their trips in the same
week of the month.

Generator transit permissions [STATIC]:

| Category | Rail allowed | Bus allowed | Subway allowed | Subject to mass-transit chance |
|---|---|---|---|---|
| Residential, Commercial | yes | yes | yes | yes |
| Industrial, Other | yes | no | no | no |

(So freight can always use rail.)

### 4.2 Destination choice

Let `rand(N)` be a uniform integer in [0, N). Draw r = rand(100) [STATIC].

| Origin | Destination |
|---|---|
| Residential | r < Res.a → Residential; otherwise Commercial-or-Industrial weighted (below) |
| Commercial | r < Com.a → Residential; r < Com.b → Commercial; otherwise Industrial |
| Industrial | r < Ind.a → Residential; r < Ind.b → Commercial; otherwise Industrial |
| Other | r < Mixed.a → Residential; otherwise Commercial-or-Industrial weighted |

"Commercial-or-Industrial weighted" asks the city for two non-negative numbers (C, I) and draws
s = rand(C + I). The destination is Industrial if s > C, otherwise Commercial [STATIC]. Only the
low 8 bits of s are compared with C; this matters only if C + I > 256, which the shipped data never
reaches. C and I are the **economic-phase weights** (*a*, *b*) of `SC3WorldLayer.ini
EconomicPhases` at the current city population (`15-sim-growth.md` §3.5): the same commercial :
industrial weights that split the workforce. They are held by the jobs valve (7000) of the world
layer and saved with it [STATIC]. No scaling is applied. Example: at 800 000 inhabitants
(between `766:30:10` and `900:32:10`) the weights are about (30, 10), so about 75 % of the
weighted draws go to commercial destinations. Correction: an earlier revision inferred job
totals.

### 4.3 Walking radius and destination bits

When a building of category 0, 1 or 2 is added and is **functional** (its lot state word,
`13-buildings-occupants.md` Persisted state, equals 1 = occupied; buildings without a lot state
word always count) [INFERRED, high: the same state test is used by the other layers], every tile inside its bounding rectangle grown by its
category radius R (clipped to the map) gets the corresponding destination bit set [STATIC]:

* R(Res) = effective `TripMaxDistanceToRoadRes`
* R(Com) = effective `TripMaxDistanceToRoadCom`
* R(Ind) = effective industrial radius (see the defect in §2.1)

When the *Shuttle Service* ordinance is active, every radius (Res, Com, Ind and Default) is
increased by `ShuttleServiceDistanceToRoadAdd` [STATIC]. The radii are recomputed whenever
ordinances change (§11).

### 4.4 When a building actually routes a trip

On each firing [STATIC]:

1. Draw r = rand(100).
2. If r > `TrafficTripThresholdPercent`: route an **uncounted** trip only if the origin lot has
   no trip result yet (its trip-tried bit is clear at the lot's first tile). Otherwise do nothing.
3. Otherwise (r ≤ threshold): route a trip. It is **counted** if the counting test of §4.5 passes.

### 4.5 Counting test

This test only applies to buildings that are functional (as in §4.3). Let d be the value (0–255)
at the lot's first tile of the **valve-effect map** of the RCI development layer (`10-save-format.md`
§6.7 `effectMap`, the map of the last completed cycle; owned by the growth valve layer,
`15-sim-growth.md` §3.3). The traffic layer looks it up by the layer ID 0x80F1E6D3 [STATIC]. Each tile
holds the clamped sum of the 3000/5000/8000 supplies and the 7000 (jobs) demand of
`SC3ValveLayer.ini [AgentValveEffects]` over the occupied buildings covering it. It is saved, so
a loaded city has it at once. Draw q = rand(100). The trip is counted iff

> q ≤ trunc(`TripChanceCountingBase` + d × `TripChanceDensityMultiplier`/100)

Only counted trips add density and transit income.

### 4.6 Mode permission (mass-transit chance)

Let p = the effective mass-transit chance (§8.1). For generators subject to the chance, draw
m = rand(100):

* on the step-2 (uncounted, first-time) path, transit is allowed iff m < p;
* on the step-3 path, transit is allowed iff m ≤ p.

When transit is not allowed, the trip is routed with rail, bus and subway all forbidden (car
only). Industrial and Other generators always route with their fixed permissions [STATIC].

### 4.7 Random number generator

The traffic model uses its own LCG, seeded from the wall clock at start-up: state ← state ×
0x41C64E6D + 0x3039 (mod 2³²). The output is bits 16–47 of the 64-bit value (state_old ×
0x41C64E6D + 0x3039), and rand(N) = output mod N [STATIC]. Because the seed is not saved, trips
are not reproducible between runs.

## 5. Route finding

### 5.1 Trip state

A route is a stack of **nodes**. A node is a tile rectangle (the origin lot for the first node,
then single tiles, or a whole span for bridges/tunnels), a **travel mode**, the mode of the
previous node, the accumulated cost, the direction of the last move, and an "alternatives exist"
flag.

Travel modes [STATIC]:

| Code | Mode |
|---|---|
| 0x80 | At origin (start) |
| 1 | Car on road |
| 2 | Car on highway |
| 3 | Car on a road tile that has highway access (0x40) |
| 4 | Rail |
| 8 | Subway |
| 0x14 | At a rail station |
| 0x18 | At a subway station |
| 0x30 | At a subway–rail station |
| 0x41 | Bus on road |
| 0x42 | Bus on highway |
| 0x43 | Bus on a road tile with highway access |
| 0x50 | At a bus stop |

The visit mask records, per tile, which modes have already examined the tile in this trip. It is
one **byte** per tile, and the **raw mode codes** of the table above are OR-ed into it; there is
no separate bit per mode [STATIC]. A tile is skipped by a node of mode *m* when
(byte AND *m*) = *m*. Because the codes share bits, marks overlap: for example a tile marked
with 0x41 (bus) or 0x50 (bus stop) is also skipped by mode 1 (car), and 0x14 also covers 4 and
0x10. Reproduce the overlaps. At the start of a trip the mask is cleared and the origin
rectangle is marked with 0x80.

### 5.2 First step (from the origin)

Let R be the walking radius of the origin's category (§4.3; Default for category 3 or unknown).
The search area is the origin rectangle grown by R on every side and clipped to the map. The four
corner tiles of the grown rectangle are excluded. The tiles of the area are visited in a random
order (a random permutation; ties are therefore broken randomly) [STATIC]. For each tile that has
not been visited:

| Tile flags | Candidate mode |
|---|---|
| no 0x10, has road | 1 (car on road) |
| has 0x10, bus allowed and road | 0x50 (bus stop) |
| has 0x10 and 0x20, rail or subway allowed | 0x30 |
| has 0x10, rail allowed and rail | 0x14 |
| has 0x10, subway allowed (any flag) | 0x18 |
| anything else | not a candidate (the tile is marked visited) |

The rows are tested top to bottom. A trip can never start directly on a highway or on track.
A trip without an origin occupant (a tile-origin trip, §9.2) uses the default radius [STATIC].

### 5.3 Choosing among candidates (all modes except highway-like modes)

For each candidate, let `d` be its density in the layer for its mode: the live surface density, or
the live subway density for subway-track candidates, or the live highway density for highway
candidates. The rule is [STATIC]:

1. A candidate with d ≥ `TrafficAnnoyanceLevel` is taken only if no candidate has been taken yet.
2. A candidate with d < `TrafficAnnoyanceLevel`:
   * a station/stop candidate (flag 0x10) always replaces the current choice;
   * any other candidate replaces the current choice unless a below-annoyance candidate has
     already been chosen and d is lower than the best d chosen so far. After a station is
     chosen, the best d is treated as 255, so non-station tiles no longer replace it.
3. If more than one candidate was seen (above or below the annoyance level), the new node is
   flagged "alternatives exist" (§5.7). **Original defect** [STATIC]: every new node starts with
   this flag already set, and a successful step never clears it, so in practice every node on the
   path has it. The flag is cleared only on the node passed to a step that finds no candidate.
   Reproduce this: on backtracking, **every** popped node is retried (§5.7 step 4), not only
   nodes that had a second candidate. The origin node is also created with the flag set.

The traffic therefore **follows the busiest uncongested route**: it prefers existing traffic
until the annoyance level is reached, and then spills onto quieter tiles.

### 5.4 Neighbourhood and mode transitions per mode

For modes 1, 4, 8, 0x14, 0x18, 0x30, 0x41 and 0x50, the candidates are the 8-neighbour ring
around the node rectangle (the rectangle grown by 1, clipped, without its four corners). The
transition table is [STATIC]:

| From mode | Candidate tile | New mode |
|---|---|---|
| 1 road | road, not station | 1, or 3 if the tile has 0x40 |
| 1 road | station, bus allowed, road | 0x50 |
| 1 road | station, (rail or subway allowed), 0x20 | 0x30 |
| 1 road | station, rail allowed, rail | 0x14 |
| 1 road | station, subway allowed | 0x18 |
| 0x41 bus | road, not station | 0x41, or 0x43 if 0x40 |
| 0x41 bus | station: same as road except that no bus-stop transition exists | 0x30 / 0x14 / 0x18 |
| 4 rail | rail, not station | 4 |
| 4 rail | station with 0x20 | 0x30 |
| 4 rail | station with rail | 0x14 |
| 8 subway | subway, not station | 8 |
| 8 subway | station with 0x20 | 0x30 |
| 8 subway | station with subway and without road/rail | 0x18 |
| 0x14 rail stn | rail, not station, and previous mode ≠ 4 | 4 (board) |
| 0x18 subway stn | subway, not station, and previous mode ≠ 8 | 8 (board) |
| 0x30 | (rail allowed) rail, not station, previous ≠ 4 | 4 |
| 0x30 | (subway allowed) subway, not station, previous ≠ 8 | 8 |
| 0x14, 0x18, 0x30 (otherwise) | station with road, bus allowed, previous ≠ 0x50 | 0x50 |
| 0x50 bus stop | road, not station, previous ≠ 0x41 | 0x41 (board bus) |

A trip that arrives at a subway station *by subway* and does not end there is turned back into
mode 8. It stays on the subway, so subway riders leave the subway only by ending their trip at a
station [STATIC]. Rail riders can leave a rail station only to an adjacent bus stop (or by ending
the trip there).

**Highway-like modes (2, 3, 0x42, 0x43).** These do not scan a ring. Instead the network piece
under the node reports which of its four orthogonal neighbours may be entered, given the direction
of the last move [STATIC]:
* *M* = the piece record's u32 attribute **0x136** (the road/highway connection matrix; 0x137 and
  0x138 are the rail and subway matrices, used by other callers). A city-wide boolean *B* held by the
  traffic layer, read from the application at start-up, selects the half: *B* true → bits 0–15,
  *B* false → bits 16–31 [STATIC; what *B* means, possibly the driving side, is INFERRED]. Use *B* = **true** for the shipped game [DATA: an implementation check on Berlin gives a highway-snapshot mean absolute difference of 0.70 at day 40 and 10.7 at day 356 against the original's saves with *B* true, against 3.10 and 28.0 with *B* false; the day-356 mean highway density is 18.0 against the original's 19.2].
* With *d* = the node's last move direction (0–3) and *r* = the piece's rotation (0–3, from the
  placement word), row *d* + 4*r* of the table below gives four bit numbers (*b*₀, *b*₁, *b*₂, *b*₃).
  Neighbour (x−1) is permitted iff bit *b*₀ of the selected half is set, (y−1) iff *b*₁, (x+1) iff *b*₂,
  (y+1) iff *b*₃.

  | Row | b₀ b₁ b₂ b₃ | Row | b₀ b₁ b₂ b₃ | Row | b₀ b₁ b₂ b₃ | Row | b₀ b₁ b₂ b₃ |
  |---|---|---|---|---|---|---|---|
  | 0 | 0 1 2 3 | 4 | 15 12 13 14 | 8 | 10 11 8 9 | 12 | 5 6 7 4 |
  | 1 | 4 5 6 7 | 5 | 3 0 1 2 | 9 | 14 15 12 13 | 13 | 9 10 11 8 |
  | 2 | 8 9 10 11 | 6 | 7 4 5 6 | 10 | 2 3 0 1 | 14 | 13 14 15 12 |
  | 3 | 12 13 14 15 | 7 | 11 8 9 10 | 11 | 6 7 4 5 | 15 | 1 2 3 0 |
 Each permitted neighbour (tested
in the order north/−y, west/−x, south/+y, east/+x) is evaluated as follows [STATIC]:

| From | Neighbour (not a station) | New mode | Density layer |
|---|---|---|---|
| 2 | highway | 2, or 3 if 0x40 | highway |
| 3 | road | 1, or 3 if 0x40 | surface |
| 3 | highway (no road) | 2 / 3 | highway |
| 0x42 | highway | 0x42, or 0x43 if 0x40 | highway |
| 0x43 | road | 0x41, or 0x43 if 0x40 | surface |
| 0x43 | highway (no road) | 0x42 / 0x43 | highway |

Selection among the permitted neighbours: the first is taken. A later one replaces it only if its
density is below `TrafficAnnoyanceLevel`; in that case the node is flagged "alternatives exist".

So cars reach highways only through tiles flagged 0x40, and buses ride highways in modes
0x42/0x43.

### 5.5 Step cost

Let d be the density of the chosen tile (as in §5.3), and let `q(X)` = floor(d / `TrafficDensityTripCostDivisorX`)
(integer division). Let D = (`DamagedCostAddToStep` mod 256) if the tile has flag 0x80, else 0.
Let f = 0.7 if the tile has flag 0x100 (0x200 for subway), else 1. Then [STATIC]:

| New mode | Step cost (cost units) |
|---|---|
| 1 road | ceil( (q(Road) + D + `TripCellCostBaseRoad`) × f ) |
| 2 highway | ceil( (q(Highway) + D + `TripCellCostBaseHighway`) × f ) |
| 3 | q(Road) + `TripCellCostBaseRoad` |
| 4 rail | ceil( (q(Rail) + `TripCellCostBaseRail`) × f ) |
| 8 subway | ceil( (q(Subway) + `TripCellCostBaseSubway`) × f ) |
| 0x14 | if previous mode = 4: ceil(q(Rail) + `TripCellCostBaseRail`); else q(RailStation) + `TripCellCostBaseRailStation` |
| 0x18 | if previous = 8: ceil(q(Subway) + `TripCellCostBaseSubway`); else q(SubwayStation) + `TripCellCostBaseSubwayStation` |
| 0x30 | if previous is 4 or 8: q(Subway) + `TripCellCostBaseSubway` (subway constants even when arriving by rail); else q(SubwayRailStation) + `TripCellCostBaseSubwayRailStation` |
| 0x41 | ceil( (q(RoadBus) + D + `TripCellCostBaseRoadBus`) × f ) |
| 0x42 | ceil( (q(HighwayBus) + D + `TripCellCostBaseHighwayBus`) × f ) |
| 0x43 | q(RoadBus) + `TripCellCostBaseRoadBus` |
| 0x50 | if previous = 0x41: q(Highway) + `TripCellCostBaseHighway` (highway constants: an original quirk); else q(BusStop) + `TripCellCostBaseBusStop` |

The multiplication by f is done in single-precision floating point. The new node's cost = parent
cost + step cost.

**Spans.** If the chosen tile is on the surface network, has flag 0x4000, and its piece reports a
span (direction 0..3, length L tiles), then the new node rectangle is moved L tiles further in that
direction (0 = −x, 1 = −y, 2 = +x, 3 = +y in rectangle terms) and the step cost is multiplied by L.
If the piece cannot report a span, the step fails [STATIC]. See `12-networks.md` for bridges and
tunnels.

The direction of the last move is recorded as: 1 if the new node lies wholly at greater y, else 0
if it lies wholly at greater x, else 3 if its far y edge is lower, else 2. [STATIC] This
last-move code is a **different encoding** from the span direction above (there 1 = −y, here
1 = +y); reproduce both as given.

### 5.6 Budget

Budget B = `TripCellCostBaseRoad` × max(W, H) (cost units; the raw INI value) [STATIC]. The test is
made when a node is about to be **expanded**: if B < the node's accumulated cost, the step fails
(strict comparison; cost = B is still expanded). A new node is created and pushed whatever its
cost, and its arrival test (§5.8) runs before the budget test, so a node that ends over budget can
still arrive [STATIC]. On a 256-tile map with the shipped value 4,
B = 1024: roughly 256 road steps, or more on cheaper transit.

### 5.7 Search loop and arrival

The search is a depth-first walk [STATIC]:

1. Push the start node.
2. **Arrival test** on the current node (§5.8). On success, stop.
3. Otherwise compute the next step from the current node (§§5.2–5.5). If there is one, push it
   and make it current, then go to 2.
4. If there is none, backtrack: pop nodes until one is found whose "alternatives exist" flag is
   set *and* that now yields a new step (the visit mask prevents repeats). If the stack empties,
   the trip **fails**. A retried node runs the normal step again (§5.2–§5.5): the ring is
   rescanned from the start, tiles whose visit-mask entry already holds all bits of the node's mode
   are skipped, and the choice rule of §5.3 starts afresh over the remaining candidates. Nothing
   resumes from the old scan position [STATIC].

The visit mask is updated as each node is chosen: the new node's tiles get the **parent's** mode
code OR-ed in, not the new node's own mode. A ring tile examined without a valid transition is
marked at once with the scanning node's code (1, 4, 8, 0x10, 0x41 or 0x50 by branch, §5.4)
[STATIC]. Candidates that were valid but lost the §5.3 choice are not marked, so a retry
(§5.7 step 4) can still take them. Station-mode scans (0x14, 0x18, 0x30) mark their rejects with
0x10 only, so they never block a later rail (4) or subway (8) scan of the tile. Highway-like modes
(2, 3, 0x42, 0x43) use the same skip test; a permitted neighbour that is a station or has no valid
transition is marked with the scanning code itself (OR-ed), valid ones are not. Codes that only
partly overlap do not block each other (a tile marked 1 is still open to mode 3), so a path may
cross its own earlier tiles in another mode. For a span step the marks go on the tile entered
**before** the jump; the landing rectangle is not marked.

### 5.8 Arrival test

| Current mode | Arrives when |
|---|---|
| 1, 0x41 | The node's first tile has 0x400 (road neighbour connection); otherwise the destination test below |
| 2, 0x42 | Only when the tile has 0x800 (highway neighbour connection) |
| 4 | Only when the tile has 0x1000 (rail neighbour connection) |
| 8 | Only when the tile has 0x2000 |
| all other modes | Destination test |

Destination test: some tile of the node rectangle has the destination bit of the trip's destination
category set. For destination "any" (category 3 trips choose Res/Com/Ind, so "any" only arises from
internal callers), any of the three bits counts [STATIC].

So **every neighbour connection counts as a valid destination for every trip** that reaches it,
whatever the trip's origin category or destination [STATIC]. A trip in mode 1 or 0x41 (road,
bus on road) arrives at a road connection (0x400) or by the destination test. Modes 2/0x42
(highway), 4 (rail) and 8 (subway) arrive **only** at a connection of their own network; there is
no destination test while on a highway, rail or subway tile. Rail riders must leave through a
station (0x14/0x30) to reach a destination inside the city. Industrial freight (rail allowed) can
therefore end at any rail connection on the map edge.
This is how neighbour connections absorb commuters (§9.3).

## 6. Density accumulation and decay

### 6.1 Accumulation

When a counted trip succeeds, the amount

> A = signed8( (lot width × lot height × `TripDensityAdder`) mod 256 )

where the width and height of the origin lot are in tiles, and signed8 reinterprets the byte as a
two's-complement value in −128..127 [STATIC]. **Original defect:** when the product mod 256 is 128
or more (for example a 7×7 lot with the shipped adder 3 gives 147, which becomes −109), each trip
*removes* density along its path. A faithful clone reproduces this.

A is added to the live density of every tile of every node on the path. The layer depends on the node's mode [STATIC]:

| Node mode | Surface (live) | Highway (live) | Subway (live) | Fare credited |
|---|---|---|---|---|
| 1 road | + A | | | |
| 2 highway | | + A | | |
| 3, 0x43 | — | — | — | — |
| 4 rail, 0x14 rail station | + A | | | Rail fare |
| 8 subway | | | + A | Subway fare |
| 0x18 subway station | + A | | + A | Subway fare |
| 0x30 subway–rail station | + A | | + A | Rail fare + subway fare |
| 0x41, 0x42 bus | — | — | — | Bus fare |
| 0x50 bus stop | + A | | | Bus fare |
| 0x80 origin | — | — | — | — |

Each update is new = clamp(old + A, 0, `TrafficMaxDensity`), computed in 16-bit signed arithmetic [STATIC].
Fares are credited per **node**, not per tile. A fare is the effective per-trip fare of §8.1,
added to a monthly fare accumulator. Only nodes whose occupant is a transport occupant credit
anything.

Buses therefore put no density on roads; bus riders load only the stops. Mode-3/0x43 road tiles
(the junction tiles) also receive no density from passing trips.
Further details [STATIC]: density and fares are added only on a **successful** counted trip; a
failed trip adds nothing. They are added per tile of each node rectangle, but only for nodes
whose occupant is a network piece. Every node after the origin is a single tile, so in practice
"per node" = "per tile". The origin node (the lot rectangle, mode 0x80) and a destination
building tile receive nothing. A span step moves the 1-tile rectangle by *L* tiles; it does not
widen it.

### 6.2 Monthly decay

Once per simulated month (§11) the retention factors are computed [STATIC]:

* road factor r_R = `MonthlyDensityDecrement`/100
* highway factor r_H = `MonthlyDensityDecrement`/100
* other factor r_O = `MonthlyDensityDecrement`/100

Ordinance adjustments (ordinance IDs are in `19-sim-economy.md`):

| Ordinance | ID | r_R | r_H | r_O |
|---|---|---|---|---|
| Parking Fines | 0x015B4D1E | − ParkingFines/100 | | |
| Alternate Day Driving | 0xE2BF1E2A | − AlternateDay/100 | − AlternateDay/100 | |
| Carpool Incentives | 0xA2BF1DDC | − Carpooling/100 | − Carpooling/100 | |
| Crossing Guards | 0xC2BF1DEF | + CrossingGuards/100 | | |
| Tourist Promotion | 0x62F6E7CF | + TouristPromotion/100 | + TouristPromotion/100 | + TouristPromotion/100 |

[ordinance names INFERRED from the INI key names; the IDs are STATIC]

Then for every tile:

* surface: new = trunc(live × r_R) if the tile is road and not a station; otherwise trunc(live × r_O)
* highway: new = trunc(live × r_H)
* subway: new = trunc(live × r_O)

Each new value replaces the live value **and** is copied into the snapshot layer (truncation
toward zero, single precision). The snapshot is therefore the decayed density, and trips during the
following month add on top of it.

Steady state: a tile that receives T density per month settles at about T × r/(1 − r). With the
shipped r = 0.75 that is 3T.

## 7. Congestion effects

### 7.1 Congestion level shown to the player

The traffic map view and queries read the **snapshot** layers. The value at a tile is the maximum
over the networks present on it [STATIC]:

* surface snapshot if the tile has road, rail, station or transfer flags;
* highway snapshot if it has highway (if larger);
* subway snapshot if it has subway (if larger) — subway only when the caller asks for it.

Three query forms exist [STATIC]:

* **tile value**: the value above for one tile and a caller-supplied network mask (bits 0x01,
  0x04, 0x10 and 0x20 select the surface layer, 0x02 the highway layer, 0x08 the subway layer).
  Tiles with none of the requested networks return 0. The surface value is taken only when
  the tile has a flag that is **also in the mask**, checked bit by bit (0x01 road, 0x04 rail,
  0x10/0x20 station kinds). So the pollution layer, which asks with mask 0x03, reads surface
  density only on **road** tiles and highway density on highway tiles (the larger if both);
  rail, station and subway tiles add nothing to air pollution [STATIC].
* **area average**: Σ(tile value over the rectangle) ÷ (number of tiles in the rectangle, *all*
  tiles, including those without a network), using integer division and an 8-bit result. Original
  defect: in this form, the subway layer replaces the running value whenever it exceeds the
  *caller's initial output value* rather than the value computed so far. A faithful clone compares
  against the caller-supplied initial value;
* **area sum and count**: Σ(tile value) and the number of tiles that have a requested network.

A per-tile accessor that returns the raw snapshot of one layer (surface, highway or subway) also
exists, and is used by the map views.

### 7.2 Effects on pollution and growth

* **Growth**: only through the trip result (§9) and the RCI demand side effects of airports and
  seaports (§12). Congestion itself does not block growth directly. It lengthens trips (higher step
  cost), which makes failures more likely on large or badly connected maps [STATIC].
* **Pollution**: the traffic model writes no pollution. The pollution layer adds
  `T_t × SC3Pollution.ini TrafficAirFactor` of air pollution per tile from the traffic density
  `T_t` (0–255) of this document, reduced while Car Smogging is enacted; the formula is in
  `17-sim-services.md` §7.2.4 step 3 [STATIC].
* **Capacity**: there is no per-road-type capacity in vehicles. Congestion is expressed entirely
  through the 0–`TrafficMaxDensity` density scale, the `TrafficAnnoyanceLevel` avoidance threshold
  and the per-mode cost divisors of §5.5.

### 7.3 Fare income

At each month-end the fare accumulator is reported to the budget (§8.3) and reset.

## 8. Mass transit: ridership, funding and strikes

### 8.1 Effective transit parameters

These are recomputed after load and whenever ordinances or funding change [STATIC]:

1. Per-trip fares: rail = `PerTileTransitIncomeRail`/1000, subway = …Subway/1000,
   bus = …Bus/1000 (§ per counted trip node).
2. Walking radii (§4.3), plus the Shuttle Service bonus.
3. Mass-transit chance p = `TrafficUseMassTransitChance`.
4. If the *Subsidized Mass Transit* ordinance (0xC2D1B025) is active: p ← min(p +
   `SubsidizedMassTransitUsePercentAdd`, 255), and every fare is multiplied by
   `SubsidizedMassTransitFaresPercentDecrease`/100.
5. p ← trunc(p × E_transit), where E_transit is the current transit efficiency factor (1.0 at
   100 % funding; §8.2).

### 8.2 Funding and efficiency

The traffic subsystem holds two funding percentages (integers 0–255, initially 100), both set by
the budget (`19-sim-economy.md`) [STATIC]:

* **road funding** F_R → road efficiency E_R;
* **transit funding** F_T → transit efficiency E_T. F_T is also passed on to every station
  building.

Efficiency curve (the same form for both, with M = `MaxEfficiencyRoads`/`MaxEfficiencyTransit`
after ×0.01 and the ≥ 1 clamp, and K = `MysticalERoads`/`MysticalETransit`). Let f = F/100:

* f ≤ 1: E = f
* f > 1: let g = (K + 1.2/(1 − M)) × 4.999999. Then E = M − 1 / ((g − 1/(1 − M)) × f − g)

E is continuous at f = 1 (E = 1) and approaches M as f grows. With the shipped M = 1.2 and K = 30,
f = 1.2 gives E ≈ 1.1667. If M is exactly 1.0 the formula divides by zero. The original has the
same defect; a clone should treat E = 1 for f > 1 in that case [STATIC; the fallback is a
recommendation]. Arithmetic is single precision.

Uses of efficiency:

* E_T scales the mass-transit chance (§8.1 step 5).
* E_R drives road deterioration and repair (§8.4).

### 8.3 Upkeep

Recomputed whenever a network occupant or station is added or removed [STATIC]:

* **Road upkeep** (§/month) = ceil(road tiles × `OptimalMonthlyFundingCostPerRoad`/100) +
  ceil(highway tiles × `OptimalMonthlyFundingCostPerHighway`/100).
* **Transit upkeep** (§/month) = Σ over station buildings of their `OptimalMonthlyUpkeep` (from
  `SC3Tune.INI`, sections `BusStop`, `RailStation`, `SubwayStation`, `SubwayRailStation`) +
  ceil(rail tiles × `OptimalMonthlyFundingCostPerRail`/100) + ceil(subway tiles ×
  `OptimalMonthlyFundingCostPerSubway`/100).

The tile counts are the network subsystem's per-type tile totals. Both totals are reported to the
budget as the 100 %-funding cost of two budget departments that the traffic subsystem registers:
**roads** (road + highway upkeep) and **mass transit** (rail, subway and stations). See
`19-sim-economy.md` §6.1 [STATIC].

### 8.4 Road deterioration message

Once a month (§11), if E_R ≠ 1 the traffic subsystem sends the network subsystem a new road
**condition** value. Let X be the network's current condition (0..1, as reported by the network
subsystem) [STATIC]:

* E_R < 1 (underfunded): v = min(1, X + (1 − E_R) × `DamageEfficiencyMultiplier`/1000)
* E_R > 1 (overfunded): v = max(0, X − ((E_R − 1)/(M_roads − 1)) × `RepairEfficiencyMultiplier`/1000)

The message carries trunc(v × 100) as an integer percentage; the network divides it by 100 again.
v is the target fraction of road tiles that carry damaged (potholed) pieces; the network converts
or repairs road pieces to reach it, and X is the current fraction (`12-networks.md` §10) [STATIC]. The resulting
damaged pieces show up in the traffic grid as flag 0x80.

### 8.5 Transit strikes

The strike state is one of **Normal** (initial), **Threatened** or **Striking**, plus a counter
of strike months. The state machine runs once a month (§11) [STATIC]:

| State | Condition | Action / next state |
|---|---|---|
| Normal | F_T ≤ 99 and transit upkeep > 0 and rand(100) ≤ S | broadcast "strike threatened"; → Threatened |
| Normal | otherwise | stay |
| Threatened | F_T ≥ 100 or transit upkeep ≤ 0 | broadcast "strike resolved"; → Normal; counter = 0 |
| Threatened | otherwise | broadcast "strike started"; → Striking; counter = 0 |
| Striking | F_T < 100 and counter ≤ `MaxLengthofStrikeInMonths` and upkeep > 0 | counter += 1 |
| Striking | otherwise | broadcast "strike resolved"; → Normal; counter = 0 |

The strike chance S (percent) = trunc((`MinFundingForNoStrike` − F_T) × `StrikeChanceAtZeroFunding`/100)
when F_T < `MinFundingForNoStrike` and the upkeep is positive; otherwise S = 0. Because the test is
`rand(100) ≤ S`, any transit funding below 100 % carries at least a 1 % monthly chance of a threat,
even when S = 0. This is original behaviour.

A strike lasts at most `MaxLengthofStrikeInMonths` + 1 months.

Each change of strike state (threatened, started, resolved) is announced to the advisors, the news
and the street automata; the street automata react to "started" and "resolved"
(`26-automata.md`). After a city is loaded, the announcement for the restored state (threatened
or striking) is repeated [STATIC].

**Effect on trips.** No strike check exists anywhere in the trip model [STATIC]. Trips keep using
transit during a strike. The visible effects are the automata, news and advisor reactions
[INFERRED].

## 9. Feedback into growth and services ("too far from work")

### 9.1 Trip result

Every routed trip, counted or not, writes its outcome to all tiles of the origin lot [STATIC]:

| Query result for a tile | Meaning |
|---|---|
| 2 | No trip has been routed from this lot yet (trip-tried bit clear) |
| 0 | The last trip from this lot succeeded |
| 1 | The last trip from this lot failed (no route within the budget and no reachable destination) |

The query uses the tile passed in by the caller, which is normally the lot's first (minimum x,
minimum y) tile. An area form of the query reports "failed" if any tile of the rectangle is in
state 1. The bits are cleared for the lot when the building is removed (§11).

### 9.2 Consumers

| Consumer | Use of the trip result |
|---|---|
| Zone developer (`15-sim-growth.md`) | Result 1 is the developer's `unfit` input (`15-sim-growth.md` §4.2): for a built R/C/I building it causes an abandonment roll with the zone type's `NoServicePercentage` (`SC3Tune.INI`, `[ResidentialZoneDeveloper]` / `[CommercialZoneDeveloper]` / `[IndustrialZoneDeveloper]`), result code 6, and no redevelopment or upgrade on that visit; an abandoned lot with result 1 is not re-occupied. Results 0 and 2 do not block anything [STATIC for the effect; rule details owned by 15] |
| Garbage facilities (`16-sim-utilities.md` §4.3) | If a garbage facility's lot has result 1, the facility immediately routes a fresh **uncounted** trip with destination "any", trip-length factor 255 (budget = 255 × `TripCellCostBaseRoad`), and only rail permitted besides cars. If that trip fails, the facility's disposal capacity is 0 for the month [STATIC]. Power plants and water facilities do not use the trip result [STATIC for the water and power rules of 16] |
| Landfill developer (`15-sim-growth.md` §7) | On every visit of a landfill-zoned tile it routes a **tile-origin** trip from that tile: no occupant, origin rectangle = the 1 × 1 tile, walking radius = the default radius (6, §2.1), destination "any", uncounted, trip-length factor = map size, cars and rail allowed. Failure gives result code 16. The trip writes the tile's result bits like any other trip [STATIC]. Landfill pieces (agent type 12850, group 12005 Garbage) are also category-3 trip makers through the generators (§4.1). **Open:** in Berlin the original records success for 14 landfill tiles (row z = 99, x 4–17) that have no road piece inside the radius-6 search area and no destination bits, and it never reports code 16 on zone 17. Interim until traced: treat trips whose origin is a landfill tile, or a landfill piece, as successful [INFERRED from DATA] |
| Query tool (`09-ui-screens.md`) | Result 1 adds a "can't reach destinations" line to the building query [STATIC] |

`MaxDistFromTransport` (in the same `SC3Tune.INI` sections) is **not** part of the traffic model.
It is a separate "network within N tiles" proximity test in the network subsystem (+1 with Shuttle
Service). See `15-sim-growth.md`.

### 9.2a Pollution and land value

The traffic model does not write pollution itself. The pollution layer reads the per-tile traffic
density (§7.2, `17-sim-services.md` §7.2.4) [STATIC]. Land value has **no** traffic input
(`15-sim-growth.md` §6.3) [STATIC].

### 9.3 Neighbour connections

A trip that reaches any road/highway/rail neighbour-connection tile succeeds (§5.8), whatever its
destination category. Cities with connections therefore succeed at trips that would otherwise fail
for lack of local jobs [STATIC].

## 10. Statistics

Computed at each monthly decay [STATIC]:

* **Average road traffic** = Σ over all tiles of (live road density if the tile is road, not a
  station, and road ≥ highway; else highway density) ÷ (road tile count + highway tile count).
  The counts come from the network subsystem. The result is 0 when the divisor is 0.
* **Average transit usage** = Σ over all tiles of u ÷ (rail tile count + subway tile count +
  number of station tiles where u came from the surface layer), where u = surface density if the
  tile is a station or rail tile and surface > subway density, else the subway density.
* **Trip failure ratio** = failed trips ÷ trips attempted this month (single-precision float). It
  is updated only when at least one trip failed. Both counters are reset each month.
  **What the counters count** [STATIC]: `tripsAttempted` is incremented at the start of **every**
  routed trip with a destination category 0–3. That includes counted and uncounted generator trips,
  the garbage facilities' and recycling centres' re-route trips (§9.2), and the "can this
  facility reach anything" trips of several civic facility classes (destination "any",
  trip-length factor 255, all modes allowed, uncounted). `tripsFailed` is incremented when the
  search stack empties. Both are cleared in the monthly decay (§11 step 2.1), which runs on
  day 1 **after** that day's weekly work. So when day 1 is a Monday, that Monday's generator trips
  are counted and then immediately cleared; they are missing from the saved month total.
  DATA (Berlin runs): non-zero counts with no Monday since the month start (70 at 2068-03-04,
  88 at 2067-08-06 after a day-1 Monday), i.e. about 15–20 non-generator trips per day. A saved
  total of about 33 % of the generator count per month, against the 21 % that the threshold
  draw alone predicts, is not fully explained [OPEN]. The saved trip bitmaps show only 4–5 % of
  occupied lots with no result, so the first-trip path (§4.4 step 2) adds at most a few percent.

The average road traffic is pushed into the transport advisor and graph history monthly. In
January it is also recorded as the yearly value, and every 10th year as the decade value [STATIC].
The `Advisor-Transportation.ini` thresholds compare against these numbers (`HIGH_ROAD_TRAFFIC`,
`MED_ROAD_TRAFFIC`, `MED_RAIL_USAGE`, `HIGH_RAIL_USAGE` etc.) — see
`22-advisors-news-petitioners.md`.

There is no explicit "commute time" statistic [STATIC].

## 11. Update order

1. **Weekly, on Mondays** (§4.1 "Schedule"; `14-sim-clock.md` §6.2): each building's trip
   generator runs §4.4 when its week-slot matches. Each trip is routed to completion
   synchronously (§5) and densities/fares are applied immediately (§6.1) [STATIC].
2. **Monthly**, at the month boundary, in this exact order [STATIC]:
   1. density decay, snapshot copy and statistics (§6.2, §10);
   2. the strike state machine (§8.5);
   3. recomputation of the effective transit parameters (§8.1);
   4. the road-condition message (§8.4);
   5. fares: last-month fares ← this month's accumulator; accumulator ← 0. The last-month value
      is what the budget reads as transit income [INFERRED: the budget reads the "last month" value].
3. **Event driven**:
   * occupant added/removed (and the bulk "occupants loaded" notifications after a load): update
     the network flags (§3.1), the destination bits (§4.3), the station list and upkeep (§8.3), and
     assign a trip generator (§4.1);
   * on removal of a building the following happens [STATIC]:
     - if it was a functional R/C/I building, the destination bits of all three categories are
       cleared inside its radius area and recomputed from the remaining functional buildings that
       reach into that area;
     - if it is not a transport occupant, the trip-tried and trip-ok bits of its lot are cleared;
     - a station clears its own flag bits (bus stop 0x0011, rail station 0x0014, subway station
       0x0018, subway–rail station 0x003C), zeroes the surface snapshot and surface live density
       of its footprint, leaves the station list and is subtracted from the transit upkeep;
     - a road piece clears 0x0001, 0x0080, 0x0100 and 0x0400; a highway piece clears 0x0002,
       0x0080, 0x0100 and 0x0800; a highway-access piece clears 0x0040; a rail piece clears 0x0004,
       0x0100 and 0x1000; a subway piece clears 0x0008, 0x0200 and 0x2000. Each also zeroes the
       densities of its own network layer (snapshot and live) on its footprint, and the road or
       transit upkeep is recomputed. The span flag 0x4000 is **never cleared** by removal. This
       is an original quirk; a stale flag only matters if a new piece without a span is built on
       the tile;
   * a funding change re-runs §8.2 and §8.1;
   * an ordinance change re-runs §8.1 (walking radii and transit chance) — the monthly step also
     re-runs it.

## Persisted state

The traffic simulator owns one record in the city database (the inner record directory of the
main city record; see `10-save-format.md`) [DATA]:

* Key: **Type 0x206C6E7C, Group 0x029CA804, Instance 0x00000000**.
* Present in every shipped city, scenario, starter town and terrain. The parser
  The analysis team's parser reads **59/59** shipped files, all of them version 9, flags 2.
  Map sizes 128, 192 and 256 occur.

Grid serialisation: an 8-bit grid is written as W rows, from the **highest** first coordinate
(x = W−1) **down to** x = 0. Each row holds H bytes for y = 0..H−1. A bit grid is written in the
same row order. Each row is ceil(H/32) little-endian u32 words, and the bit for y is bit (y mod 32)
of word floor(y/32). The unused high bits of the last word are 0 [STATIC; the row order is
verified by the round-trip size and value distribution in all 59 files, DATA].

Record layout (version 9). G = W × H and B = W × ceil(H/32) × 4:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | 9 |
| 0x02 | 2 | u16 | flags | 2 in all shipped files. If bit 0 is set, a length-prefixed marker string follows the sentinel and another one ends the record (`10-save-format.md`) |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | G | u8 grid | surface snapshot | §3 |
| +G | G | u8 grid | highway snapshot | |
| +2G | G | u8 grid | subway snapshot | |
| +3G | B | bit grid | trip-ok bits | §9.1 |
| +3G+B | B | bit grid | trip-tried bits | §9.1 |
| +3G+2B | 4 | f32 | fares this month | § accumulator (§6.1) |
| +4 | 4 | f32 | fares last month | |
| +8 | 1 | u8 | strike state | 0 = Threatened, 1 = Striking, 2 = Normal (§8.5) |
| +9 | 4 | u32 | strike month counter | |
| +13 | 132 | struct | road-traffic history | 12 monthly + 10 yearly + 10 decade values, each u32, then u32 graph maximum (220 in all files). These feed the transport graph [DATA for the size; INFERRED: index 0 is the most recent] |
| +145 | G | u8 grid | surface live density | |
| +145+G | G | u8 grid | highway live density | |
| +145+2G | G | u8 grid | subway live density | |
| … | 4 | u32 | trips attempted this month | |
| … | 4 | u32 | trips failed this month | |
| … | 4 | f32 | trip failure ratio (last month) | |
| … | 4 | u32 | average transit usage | §10 |
| … | 4 | u32 | average road traffic | §10 |

Total size for an N × N map = 8 + 6N² + 2·N·ceil(N/32)·4 + 8 + 1 + 4 + 132 + 20 bytes. For
N = 256 that is 409 773 bytes [DATA].

Older versions are accepted by the loader. Fields are present by version as follows: the surface
snapshot always; highway/subway snapshots and both bit grids when version ≥ 2; the two fare floats
when ≥ 3; the strike state and counter when ≥ 5; the history when ≥ 6; the live grids when ≥ 7; the
five trailing statistics when ≥ 9. Fields that are absent keep their reset values (zero, Normal) [STATIC].
A writer must always write version 9.

**Not persisted** (rebuilt after load from occupants and ordinances): the network flag grid, the
destination bits, the station list, upkeep totals, funding/efficiency (restored by the budget),
effective transit parameters, the trip-generator assignment and the RNG state [STATIC].

**Legacy import.** When a city is imported from the older SimCity 2000 format, the 8-bit traffic
layer of that format is read at half resolution: each source cell is duplicated into a 2 × 2 block
in both the surface snapshot and the surface live grid. The import happens only if the map is
exactly twice the source size in each axis [STATIC; see `10-save-format.md` for the importer].

## 12. Airports and seaports

Airports and seaports are *zones* (zone types `AirportZone` and `SeaportZone`) whose lots are
developed piece by piece ("puzzle" development) by the zone developer. The development rules are
owned by `15-sim-growth.md`. What matters to the transport model is the following:

| Aspect | Airport | Seaport | Source |
|---|---|---|---|
| Agent type of the developed buildings | 12310 | 12330 | `CitySim.ini` [DATA] |
| Trip category (§4.1) | Commercial: makes commuter trips like a commercial building and is a **commercial destination** for other trips (radius = commercial walking radius) | Industrial: makes trips like industry (car + rail only) and is an **industrial destination** | [STATIC] |
| RCI valve effects (`SC3ValveLayer.ini`, `AgentValveEffects`, format `valve:supply:demand` joined by `+`) | `8001:100:0+5001:500:0+7000:0:5` | `8001:250:0+5001:100:0+7000:0:5` | [DATA]; valve semantics in `15-sim-growth.md` |
| Zoning cost per tile (`SC3BudgetLayer.ini`) | `AirportZone` | `SeaportZone` | [DATA]; see `19-sim-economy.md` |
| Availability year (advisor) | `AIRPORT_AVAILABLE` in `Advisor-Transportation.ini` | — | [DATA] |
| Pollution agent entries (`SC3Pollution.ini`) | by building ID | by building ID | [DATA]; `17-sim-services.md` |

So the effect of airports and seaports on commerce and industry happens through the valve
(demand-cap) system: airports mainly raise commercial capacity (5001) and seaports mainly raise
industrial capacity (8001). The trip model sees them only as ordinary commercial/industrial
buildings [STATIC for the trip model; INFERRED for the valve interpretation of IDs 5001/8001].

The seaport developer requires each new seaport piece to have fewer than two orthogonally adjacent
seaport pieces of the same kind in certain placements, and checks the water/land relationship of
the lot [STATIC, partial; complete rules belong to `15-sim-growth.md`, OPEN here].

`Res/AIRTRMGR.IXF` (two text records under 62DFCCEC:62DFCD34, instances 1 and 2) and
`Res/TRNPROD.IXF` (two text records under 22CEFD99:62CEFDDB) are automata scheduling data for
aircraft and trains. The trip model does not use them; see `26-automata.md` [INFERRED, high].

## 13. Transport advisor inputs

`Advisor-Transportation.ini` [DATA] contains:

* `[FUNDING_RATE_CUTOFFS]`: `ROAD_FUNDING_HIGH/MEDIUM/LOW`, `RAIL_FUNDING_HIGH/MEDIUM`. Percent
  funding thresholds compared with F_R and F_T (§8.2).
* `[TRANSPORTATION_ADVANCEMENTS]`: the years when the subway, bus station, airport and highway
  become available (`SUBWAY_AVAILABLE`, `BUS_STATION_AVAILABLE`, `AIRPORT_AVAILABLE`,
  `HIGHWAY_AVAILABLE`).
* `[TRANSPORTATION_CUTOFFS]`: `ROAD_DAMAGE` (percent; compare with the road condition of §8.4),
  `HIGH_ROAD_TRAFFIC` / `MED_ROAD_TRAFFIC` (compared with *average road traffic*, 0–255 scale,
  §10), `MED_RAIL_USAGE` / `HIGH_RAIL_USAGE` (compared with *average transit usage*, §10).
* `[PETITIONER_GENERATION]`: trigger levels and years for the carpool, crossing-guard and
  alternate-day-driving petitioners and for the bus-stop petitioner. The levels are average road
  traffic values [INFERRED].

The exact advisor logic is in `22-advisors-news-petitioners.md`. The statistics exposed by the
traffic subsystem are: average road traffic, average transit usage, trip failure ratio,
last-month fares, this month's fares, road and transit upkeep, road and transit efficiency, the
strike flag and chance, and the road-traffic history (12 months, 10 years, 10 decades) [STATIC].

## Debug cheat

The cheat string `traffic lights` followed by `ON`/`on` enables debug colouring of traversed
occupants (any other argument disables it). It has no simulation effect [STATIC].

## Open questions

1. (Resolved: flag 0x100/0x200 = piece attribute 0x12D bit 0, §3.1.)
2. (Resolved: the counting test reads the valve-effect map, §4.5.)
3. (Resolved: the (C, I) weights of §4.2 are the `EconomicPhases` weights.)
4. (Resolved: the week-slot is dealt per building type; §4.1.)
5. (Resolved: piece attribute 0x136 matrix, §5.4.) Still open: the meaning of the city-wide
   boolean *B*. (Resolved: span flags 0x4000 are restored from the saved piece word.)
7. Airport/seaport puzzle development rules (§12): `15-sim-growth.md`.
8. `TrafficSimLobotomyDensity`: loaded, but has no known effect.
9. Strike side effects outside the trip model (news, advisor, automata).
10. The direction of the history arrays in the save record (index 0 = most recent is inferred).
