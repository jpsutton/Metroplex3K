# 21 — Disasters, fires, riots, strikes and emergency effects

Status: work in progress. Sections are filled in order of implementation criticality.
Cross-references: `14-sim-clock.md` (update scheduling), `16-sim-utilities.md` (power plants,
ageing and explosion), `17-sim-services.md` (fire/police stations, crime, flammability layer),
`13-buildings-occupants.md` (occupant groups), `23-scenarios.md` (scenario commands),
`06-audio.md` (disaster sounds), `25-rendering.md` (effect sprites), `10-save-format.md`.

## 1. Overview

The disaster subsystem is one simulator component (the *disaster layer*) that owns a set of
*disaster objects*, one per disaster type. The set of types is data-driven: the section
`[DisasterDescriptions]` of `SC3DisasterLayer.ini` maps each **disaster type ID** to the **class
ID** of the component that implements it. At start-up the layer instantiates each listed class
and registers it under its type ID. A type ID not listed there does not exist in the game (its
menu item, scenario command and random trigger are all inert). [STATIC]

Each disaster object exposes, at a behavioural level:

* *Start* (with type-specific parameters, see §3) — returns success/failure;
* *Is active*;
* *Stop / end*;
* a periodic *update* driven by the simulation clock;
* a random-trigger check (for types that can occur spontaneously).

The layer itself additionally provides: a global *disasters enabled* flag, the *emergency*
(state-of-emergency) bookkeeping and disaster-relief payment, the generic "destroy this
occupant because of disaster X" routine, and handling of dispatched emergency units.

## 2. Disaster type table

All IDs are `u32`. The *type ID* is used by scenario scripts, by the UI menu and in saves; the
*class ID* is the component class instantiated through `[DisasterDescriptions]`. [STATIC][DATA]

| Type ID | Disaster | Class ID | INI section(s) in `SC3DisasterLayer.ini` |
|---------|----------|----------|------------------------------------------|
| `0x621CDA33` | Fire | `0x428FD431` | `[FireDisaster]` |
| `0x02963821` | Tornado | `0x62A34670` | `[TornadoDisaster]` |
| `0x42963812` | Earthquake | `0x22FE8D32` | — |
| `0x4296380E` | Riot | `0xE2FE8D38` | `[RiotDisaster]` |
| `0xE2963828` | UFO attack ("visitation") | `0x22FE8D3E` | `[UFODisasterTunables]` |
| `0x22963800` | Toxic cloud | `0xC2FE8D43` | `[ToxicCloudDisasterTunables]` |
| `0x13873254` | Whirlpool | `0x52FE8D50` | `[WhirlpoolDisaster]` (empty) |
| `0x13873255` | Locust swarm | `0x52FE8D51` | `[LocustsDisaster]` (empty), `[LocustDisasterTunables]` |
| `0x13873256` | Space junk | `0x52FE8D52` | `[SpaceDebrisDisaster]` (empty) |
| `0xC4C90997` | Parade (not a disaster; an event reusing the framework) | `0x84C92CBE` | `[ParadeEvent]`, `[ParadeEventTunables]`, `[ParadeEventParadeBlocks]`, `[ParadeEventParadeConfigs]` |

The program also contains a tenth disaster implementation that is not listed in `[DisasterDescriptions]` and is therefore never instantiated; it reads the whirlpool tunables. An implementation can ignore it. [STATIC]

Empty sections `[SmiteDisaster]` and `[PlaneCrashDisaster]` exist in the shipped INI but no
type with those names is instantiated; plane crashes, flooding, volcanoes and meteors are **not**
disasters in this game. [DATA][STATIC] Power-plant explosions and nuclear meltdowns are produced by
the power-plant ageing/overwork model (`16-sim-utilities.md`), and the disaster layer supplies the
radiation fall-out (§9). [STATIC]

### 2.1 `SC3DisasterLayer.ini` sections that hold `ID=a,b` pairs

`[FireDisaster]`, `[TornadoDisaster]`, `[RiotDisaster]` and `[ParadeEvent]` contain a single line
`<typeID>=<a>,<b>` (shipped: fire `20,62`, tornado `5,60`, riot `3,63`; parade `1`). [DATA]
The first value is the maximum number of simultaneous instances of that kind (burning buildings
for fire) and the divisor for the disaster sound's intensity; the second is the `AUDIO.INI`
"Disaster"-class sound item ID (§5.1). [STATIC]

### 2.2 `[MiscDisasterTunables]`

| Key | Units | Meaning |
|-----|-------|---------|
| `MaxEmergencyDuration` | months | Upper bound on how long a declared emergency (and its emergency-worker relief bonus) lasts; see §10. |
| `MinDisasterCostToGetRelief` | § (simoleons) | Accumulated destruction cost of the current disaster must **exceed** this for relief to be paid. |
| `ReliefAsPercentageOfCostWithoutEW` | percent | Relief paid as a percentage of the accumulated destruction cost when no emergency was declared. |
| `ReliefAsPercentageOfCostWithEW` | percent | Same, when an emergency ("EW") was declared. |

[STATIC]

## 3. Starting a disaster (menu and scenario)

The layer offers a "start disaster of type T" entry that runs that type's *random start* routine (random location and parameters, warning flag set); the Disasters menu uses it, so menu disasters behave exactly like random occurrences but bypass the occurrence timers and the disasters-enabled flag. [STATIC for the entry; INFERRED that the menu calls it]

A disaster can be started by (a) the player through the Disasters menu (not blocked by the
disasters-enabled flag, §4.3), (b) a scenario script command, (c) the random trigger (§4), or (d) a cheat/console
command. Scenario commands and their parameters (all script arguments are integers; tile
coordinates in scripts are 1-based and are converted to 0-based and clamped to the map size):
[STATIC]

| Script command | Type | Parameters after conversion |
|----------------|------|-----------------------------|
| `cmdStartEarthquake` | Earthquake | magnitude *m* = arg0 × 0.1 clamped to [0,1]; flag = (arg1 ≠ 0). A zero magnitude is a no-op that reports success. |
| `cmdStartFire` | Fire | tile (x, z) = (arg0−1, arg1−1), clamped to [0, size−1]. |
| `cmdStartLocusts` | Locusts | flag = (arg0 ≠ 0). |
| `cmdStartRiot` | Riot | tile (arg0−1, arg1−1) clamped; flag = (arg2 ≠ 0). |
| `cmdStartSpaceJunk` | Space junk | magnitude = arg0 × 0.1 clamped to [0,1]; flag = (arg1 = 1). Zero magnitude: no-op. |
| `cmdStartTornado` | Tornado | start tile (arg0−1, arg1−1); target tile (arg2−1, arg3−1) giving a unit direction vector (start→target, normalised in the x/z plane; a zero-length vector fails); variant = arg4 clamped to [0,2]; arg5 clamped to [0,400] (duration/lifetime parameter); speed = arg6 clamped to [1,500] (a negative value becomes 1) and converted as speed × 256 / 60000 (world units per millisecond of real time, §6.1); flag = (arg7 ≠ 0). |
| `cmdStartUFOAttack` | UFO | flag = (arg0 ≠ 0). |
| `cmdStartToxicCloud` | Toxic cloud | tile (arg0−1, arg1−1) clamped (no zero-floor clamp on the low side in the original); flag = (arg2 ≠ 0); direction vector and other parameters zero (= choose automatically). |
| `cmdStartWhirlpool` | Whirlpool | magnitude = arg0 × 0.1 clamped to [0,1]; flag = (arg1 ≠ 0). Zero magnitude: no-op. |
| `cmdEnableDisableDisasters` | — | Sets the global disasters-enabled flag (§4.1). |

The boolean *flag* passed with most commands is the **warning flag**: when set, the disaster runs
its premonition/warning phases (advisor warnings, premonition sounds, fore-shocks) before striking;
when clear it strikes immediately. Random occurrences set the flag. [STATIC for the earthquake;
INFERRED for the other types, which share the same premonition timers.]

## 4. Random triggers and the disasters-off setting

### 4.1 Scheduling

The disaster layer drives its objects through three scheduler agent types declared in
`CitySim.ini [AgentTypes]` (see `14-sim-clock.md` for scheduler rates): [DATA][STATIC]

| Agent type | Name | Drives |
|---|---|---|
| `0x0288E4C8` | DisasterManager | the per-disaster **random-occurrence check** (one registration per disaster object) |
| `0x2288E4EB` | DisasterInstanceDayTick | the day-rate update of every active disaster instance, and the layer's own bookkeeping |
| `0xC3629AFA` | DisasterInstanceMinuteTick | the minute-rate update of every active disaster instance (animation, movement, burning) |

Tick rates, converted to game time with `14-sim-clock.md` §6 [STATIC per that document]:

| Driver | Scheduler tier | Game-time rate | Used here as |
|---|---|---|---|
| DisasterManager (random-occurrence check) | 4 | once per simulated **month** | "random-check tick" |
| DisasterInstanceDayTick | 2 | once per simulated **day** | "day tick" |
| DisasterInstanceMinuteTick | 0 | once per simulated **minute** while any disaster instance is active (scheduler command 3) | "minute tick" |

Consequences (game time, shipped values): fire random interval 48–240 random-check ticks = 4–20
years between random fire attempts; tornado 48–360 = 4–30 years; riot 48–300 = 4–25 years at
threshold unrest (shorter with higher unrest); earthquake chance 40/10000 per month once the
96-month gap has passed. Minute-tick durations: tornado lifetime 30–60 minute ticks = 0.5–1 game
hour; tornado warning phases 60 ticks = 1 game hour each; earthquake phases T1–T4 0/1/2/3 game
hours; riot brew 80 minutes, riot duration 20–240 minutes. Because a simulated day under minute
resolution takes at least 72 s real time (`14-sim-clock.md` §4), 60 minute ticks last at least
3 s real time. `[INFERRED: that the random checks and the per-instance updates are driven by
the agents named above, as stated in 14-sim-clock.md; the issuer of command 3 is OPEN there]`
The emergency elapsed counter advances on the monthly callback. [INFERRED]

### 4.2 Per-type random rules

| Type | Random rule (details in the type's section) |
|---|---|
| Fire | Interval counter: RandInt(FireIntervalMin, FireIntervalMax) checks between attempts (§5.2). |
| Earthquake | Minimum gap in months since last quake, then chance/10000 per check (§7.2). |
| Tornado, Riot | Tornado: interval counter, RandInt(`0xE48381BC`, `0x048381C8`) checks (§6.1). Riot: unrest-scaled interval (§8.2). `[TornadoDisaster]`, `[RiotDisaster]` give only the max-count/sound pair. |
| UFO | `MinMonthsBetweenUFOAttacks`…`MaxMonthsBetweenUFOAttacks`, `PercentageChanceOfUFOAttackEachMonthTimes100`, `YearOfFirstPossible…Appearance` (§6.3). |
| Locusts | `MinMonthsBetweenLocustAttacks`…`MaxMonthsBetweenLocustAttacks`, `PercentageChanceOfLocustAttackEachYearTimes100` (§6.4). |
| Toxic cloud | Pollution/ordinance score against `ThresholdScoreForToxicCloud`, `AverageDurationBetweenOccurrences` (§6.2). |
| Whirlpool, Space junk | History gap in months × 30 days, then chance/10000 per check; space junk also needs year ≥ `0x44860918` (§6.5, §6.6). Tunables are in `SimTune.INI [Tunables]`; the `SC3DisasterLayer.ini` sections are empty. |

Values named "…Times100" are percentages × 100 (e.g. 33 = 0.33 %). [INFERRED from names]

### 4.3 Disasters enabled ("disasters off")

* The flag is a property of the city (owned by the city object, persisted with the city settings,
  see `10-save-format.md`), not of the disaster layer. Scenario command
  `cmdEnableDisableDisasters` sets it (non-zero argument = enabled). [STATIC]
* When the layer initialises or the flag changes, the layer registers (enabled) or unregisters
  (disabled) every disaster object's random-occurrence check. Disabled therefore means **no
  random disasters**; already-running disasters continue, and disasters started explicitly
  (menu, scenario command) are not blocked by this flag. [STATIC for the registration; menu
  behaviour INFERRED]

### 4.4 UI command enabling

While disasters run, the layer enables and disables the Emergency menu commands. The command IDs
are those of `Sys/MenuItem.INI` (names from that file's comments): [STATIC]/[DATA]

| Command ID (`MenuItem.INI`) | Command | Enabled while |
|---|---|---|
| `0x1000A001` | Emergency › Goto | at least one disaster of any type is active |
| `0x1000A004` | Emergency › Fire Dispatch | at least one active disaster other than the parade |
| `0x1000A003` | Emergency › Police Dispatch | at least one active disaster other than the parade |
| `0x1000A006` | Emergency › Plane Dispatch (crop duster) | a locust swarm is active |

## 5. Fire

### 5.1 Inputs

* **Flammability layer** (owned by `17-sim-services.md`, `SC3Tune.ini [FlammabilityLayer]`): one
  byte 0–255 per cell; written *flam* below.
* **Fire-protection coverage layer** (owned by `17-sim-services.md`, `SC3FireLayer.ini`): one byte
  0–255 per cell; written *cov* below. Fire stations raise it; dispatched fire-fighters add to it
  (§10.3).
* Tunables. All fire tunables except the `[FireDisaster]` pair live in `SimTune.INI`, section
  `[Tunables]`, whose keys are hexadecimal tunable IDs. [DATA][STATIC]

| Tunable ID (`SimTune.INI [Tunables]`) | Name used here | Units | Meaning (shipped value) |
|------|------|------|------|
| `0x047DF002` | FireIntervalMin | random-check ticks (months) | Lower bound of the random-fire interval (48). |
| `0x647DF00F` | FireIntervalMax | random-check ticks (months) | Upper bound (exclusive) of the random-fire interval (240). |
| `0xC47DF01A` | StageMin | burn ticks | Shortest burn-stage duration (10). |
| `0xA47DF024` | StageMax | burn ticks | Longest burn-stage duration (40). |
| `0x647DF02B` | MinFlam | flammability (0–255) | A building ignites only if *flam* at its anchor tile is ≥ this (1). Stored as a byte. |
| `0x647DF032` | SpreadFlam | flammability | Buildings in the spread ring are candidates only if *flam* > this (100). Byte. |
| `0x247DF038` / `0xA47DF03E` | Lo₀ / Hi₀ | percent | Spread chance range for intensity 0 (0 / 20). |
| `0x047DF044` / `0xE47DF04A` | Lo₁ / Hi₁ | percent | Spread chance range for intensity 1 (5 / 50). |
| `0x047DF051` / `0xA47DF05A` | Lo₂ / Hi₂ | percent | Spread chance range for intensity 2 (15 / 95). |
| `0x047DF061` | GroupBias | percent | Chance that a random fire targets occupant group 300 instead of group 100 (15). |

`[FireDisaster]` in `SC3DisasterLayer.ini` holds `0x621CDA33=<MaxFires>,<SoundID>`:
*MaxFires* (shipped 20) caps simultaneous burning buildings and scales the fire sound;
*SoundID* (shipped 62) is the `AUDIO.INI` "Disaster"-class sound item (`DisasterFire`) driven by the
fire (see `06-audio.md`). The same `<count>,<soundID>` meaning applies to `[TornadoDisaster]`
(5, 60 = `DisasterTornado`) and `[RiotDisaster]` (3, 63 = `DisasterRiot`). [STATIC][DATA]

Derived constants computed once at load: [STATIC]

* s = (StageMax − StageMin) / (255 − MinFlam) (float). If 255 − MinFlam ≤ 0, StageMax is forced to 0.
* kᵢ = (Hiᵢ − Loᵢ) / 255 for i = 0, 1, 2 (float).

Fire uses two different clocks [STATIC]:

* The **random-ignition counter** (§5.2) advances once per **random-check tick**, i.e. once per
  simulated month (§4.1). `FireIntervalMin`/`FireIntervalMax` are therefore in months (shipped
  48–240 = 4–20 years between random fire attempts).
* Every **burning record** (§5.4–§5.7) is an active disaster instance. It advances once on every
  **minute tick** and once on every **day tick** (§4.1); the update does not distinguish the two.
  While any fire burns, minute ticks run (an active instance exists), so a burning record advances
  1 440 times per simulated day plus once at each day tick. `StageMin`/`StageMax`
  (shipped 10–40) are therefore, in practice, game minutes.

In the rest of this section, "burn tick" means one such advance of a burning record.

### 5.2 Random ignition

1. The fire object keeps a counter and an interval. Each random-check tick the counter is incremented;
   when counter ≥ interval the counter is reset to 0, a new interval is drawn as
   RandInt(FireIntervalMin, FireIntervalMax) (= FireIntervalMin if Min ≥ Max), and a random fire
   start is attempted. The first interval is drawn the same way at initialisation. [STATIC]
2. A random start fails immediately if the number of currently burning buildings exceeds
   *MaxFires*. [STATIC]
3. Choose the target occupant group: group 300 with probability GroupBias % (Rand(100) <
   GroupBias), otherwise group 100. Group IDs are occupant-group identifiers of the building
   database (`13-buildings-occupants.md`). [STATIC; group membership OPEN]
4. Choose a uniformly random tile (x in [0, width−1), z in [0, height−1), i.e. the last column
   and last row are never chosen; this is a quirk of the original, which draws below the largest
   tile index rather than below the map size [STATIC for the draw; INFERRED, high confidence, that
   the bound is the largest index]. It only shifts the scan start) and scan the whole map
   starting there (a full sweep visiting every tile once, wrapping). For each tile that holds an
   occupant whose type belongs to the chosen group, try to ignite it (§5.3); stop at the first
   success. [STATIC; sweep order as in §8.4 "site search", applied to the whole map]
5. If no building of the group could be ignited, repeat step 4 accepting any occupant. [STATIC]
6. On success, a fire-start notification (news/advisor event and "go to disaster" location) is
   raised. [STATIC]

### 5.3 Igniting a building

A building (occupant) ignites when all hold: *flam* at its anchor tile ≥ MinFlam, and it is not
already burning. Igniting creates a **burning record** for the occupant with: [STATIC]

* intensity *k* = the requested initial intensity (0 for random fires, spread, scenario and riot
  ignition; other disasters may pass a higher value);
* stage timer = 0, step count = 0;
* stage duration = RandInt(StageMin, StageMax);
* a fire visual effect attached to the occupant, playing the animation for the intensity
  (§12);
* the occupant is flagged as burning (it stops functioning as a normal building while burning;
  exact side-effects `[OPEN]`).

The burning-building count is incremented; when it becomes 1 the fire sound starts; on every
change the sound intensity is set to min(100, count × 100 / MaxFires) percent. [STATIC]

### 5.4 Burning: per-tick update and state machine

Each burn tick, for every burning record: [STATIC]

1. If the tile no longer holds the same occupant (it was bulldozed or replaced), the record is
   discarded without further effect.
2. Increment the stage timer. If timer < stage duration, nothing else happens this tick.
3. Otherwise: step count += 1, timer = 0. Read *cov* and *flam* at the occupant's anchor tile.
4. Decide **grow**: if step count < 10, grow = (*cov* < Rand(255)); otherwise grow = true (a fire
   that survives 10 stages can no longer be beaten back).
5. New stage duration: if grow, trunc(StageMax − *flam* × s); else trunc(*flam* × s + StageMin).
   (High flammability makes growing stages short and shrinking stages long.)
6. Spread chance p = trunc(Hiₖ − *cov* × kₖ) using the **current** intensity *k* (before the
   transition below). If p > 0, perform spread (§5.5).
7. Apply the intensity transition:

| Current intensity | grow = true | grow = false |
|---|---|---|
| 0 (small) | → 1 (medium) | fire goes out; building survives; record removed |
| 1 (medium) | → 2 (large) | → 0 |
| 2 (large) | building destroyed (§9): explosion/collapse effect, destruction sound at the tile, record removed, the destroy routine is invoked so rubble and cost accounting happen | → 1 |

The visual effect is switched to the animation of the new intensity on each transition. [STATIC]

Consequences of the formulas [INFERRED]: Rand(255) yields 0…254, so with *cov* = 0 a fire grows
with probability 254/255 at each stage completion and an unprotected building normally burns down
after three stage completions; with *cov* = 255 a fire never grows before step 10, so it shrinks to
intensity 0 and goes out unless it survives 10 stage completions.

### 5.5 Spread

Spread from a burning building with chance p (percent): [STATIC]

1. If the burning count exceeds *MaxFires*, or the building is no longer valid, no spread.
2. **Contact set.** For every occupant returned by the building database's "occupants touching
   this occupant" query, ignite it (§5.3, intensity 0) if Rand(100) < p.
3. **Ring set.** Take the occupant's tile bounding rectangle, enlarge it by 2 tiles on every side,
   clip to the map, and split the enlarged rectangle minus the original into up to four
   non-overlapping strips (top, bottom, left, right). In each strip, collect up to 6 occupants whose
   anchor tile has *flam* > SpreadFlam; ignite each with probability p % (Rand(100) < p).

### 5.6 Extinguishing and dispatched fire-fighters

There is no separate "extinguish" action: fire-fighters (stations and dispatched units) act only by
raising *cov*, which lowers the growth probability and the spread chance. Dispatch is described in
§10.3. [STATIC]

### 5.7 Fire sound visibility

Every 3rd burn tick each burning record checks whether its tile lies inside the current view;
the number of visible fires is tracked and the fire sound is switched on when the first becomes
visible and off when the last leaves the view. This is presentation only. [STATIC]

### 5.8 Rubble cleanup

Destroyed buildings become rubble through the generic destroy routine (§9). Rubble removal is
owned by the building/growth simulation (`15-sim-growth.md`). [OPEN: rubble lifetime]

## 6. Moving disasters

Common rules [STATIC]: each moving disaster instance has a position in world units (tile × 256),
a velocity vector in the x/z plane, a lifetime counter and a phase. Before striking it passes
through premonition phases (warnings, sounds) unless started without the warning flag (§3).
Per minute tick it advances position by its velocity and applies its damage rule at the tile under
it. It ends when its lifetime expires or it leaves the map.

### 6.1 Tornado

Tunables (`SimTune.INI [Tunables]`) [DATA][STATIC]:

| Tunable ID | Meaning (shipped) |
|---|---|
| `0xE48381BC` / `0x048381C8` | Random-occurrence interval min / max, in random-check ticks (48 / 360); drawn with RandInt as for fire (§5.2). |
| `0xA48381D2` / `0x84838207` | Speed min / max for random tornadoes (60 / 120); read as signed 8-bit values and converted to float. Unit: tiles per minute (see "Speed, path and width" below). |
| `0x4483820F` / `0x84838216` / `0x8483821D` | Destruction chance per step (percent) for tornado variant 0 / 1 / 2 (50 / 70 / 95). |
| `0x24838224` / `0x0483822A` | Lifetime min / max, in minute ticks (30 / 60). |
| `0x44838230`, `0xC4838239`, `0x0483823F`, `0x6483824B`, `0xE4838253` | Durations of warning phases 0–4, in minute ticks (0 / 60 / 60 / 60 / 60). |

`[TornadoDisaster]` gives `<MaxTornadoes>,<SoundID>` (shipped 5, 60).

Random start [STATIC]:

1. Fails if the number of active tornadoes exceeds MaxTornadoes.
2. Start tile = (Rand(width), Rand(height)).
3. Direction: u = uniform(−1, 1); direction = (u, 0, √(1 − u²)) — random tornadoes always travel
   towards increasing z.
4. Speed = uniform(SpeedMin, SpeedMax) (float); variant = Rand(3); lifetime = RandInt(LifeMin,
   LifeMax).

Phases [STATIC]: phase 0→1→2→3→4 advance when the phase counter reaches the corresponding
warning duration (counter reset each time; each change updates the warning presentation). At
the end of phase 4 the tornado **touches down** (phase 5): the tornado sprite effect is created
(variant + 1 selects the sprite set), and it starts moving. Without the warning flag it starts in
phase 4 with the counter already at its threshold, i.e. touches down on the next tick.

Movement and damage [STATIC]: in phase 5 the position advances by direction × speed each minute
tick; elapsed += 1; when elapsed ≥ lifetime (or the tornado leaves the map) it ends. On each step,
with probability Rand(100) ≤ DestroyChance[variant], the occupant at the current tile is
destroyed with damage type `0x08` (rubble), an explosion effect and explosion sound (the explosion
notification of `06-audio.md` §10.5) are produced at the tile, and if the occupant was over water (occupant flag bit
0x400000) a splash effect is spawned. Destroyed cost and count are accumulated per tornado.

Speed, path and width [STATIC unless tagged]:

* **Units.** Speed values (random range `0xA48381D2`/`0x84838207` and the scenario argument) are
  converted once to s = value × 256 / 60000 world units per millisecond (256 world units per tile,
  60 000 ms per minute), so the value is in **tiles per minute of real (wall-clock) time** (shipped
  random range 60–120 tiles/min = 1–2 tiles/s). The instance also stores a velocity vector =
  direction × s × 0.001: the same speed in world units per **microsecond**, which is the time unit
  of sprite effects and automata (`26-automata.md` §7). [STATIC for both conversions]
* **Time base.** The movement itself is done by the tornado's sprite effect, which advances with
  elapsed real time, not with simulation ticks. The ground track in tiles per game day therefore
  depends on the game speed (and on the minute-resolution slow-down of `14-sim-clock.md` §4).
  [INFERRED, high confidence, from the real-time units]
* **Path.** The direction vector is fixed when the tornado is created. At touch-down the tornado
  sprite effect (`25-rendering.md`, `26-automata.md`) is created at the start position with the
  direction vector, the speed and the variant, and from then on the effect carries the tornado;
  the disaster layer never alters the direction, so there is **no wandering in the simulation
  logic** `[OPEN: whether the sprite effect adds a visual wobble]`. Random tornadoes therefore
  travel in a straight line with a random heading in the half-plane of increasing z.
* **Width.** Damage is applied only to the occupant at the single tile under the tornado's
  centre on each step (1 tile wide); a large building is hit whenever any of its tiles is under
  the centre `[INFERRED]`.
* **End.** On each update, once the step counter reaches the lifetime the effect is told to stop
  and the tornado ends.

### 6.2 Toxic cloud

Keys in `SC3DisasterLayer.ini [ToxicCloudDisasterTunables]` [DATA][STATIC]:

| Key | Units | Meaning |
|---|---|---|
| `MinCloudsPerInstance`, `MaxCloudsPerInstance` | count | Number of cloud sprites forming one toxic-cloud instance (0 → 1). |
| `MinToxicCloudSpeed`, `MaxToxicCloudSpeed` | as scenario speed | Converted to world units per millisecond of real time as value × 256 / 60000 (same unit and time base as tornado speed, §6.1). |
| `TimeBeforeFirstPremonition`, `…SecondPremonition`, `…ThirdPremonition`, `TimeBeforeToxicCloudVisible` | minute ticks | Warning schedule; the cloud appears after the last. |
| `MinHeightAboveTerrain`, `MaxHeightAboveTerrain` | tiles (stored × 256) | Altitude band of the cloud. |
| `MinCloudDuration`, `MaxCloudDuration` | ticks | Lifetime of the cloud (minimum defaults to 10 if 0). |
| `AverageDurationBetweenOccurrences` | months | Minimum elapsed time between random toxic clouds. |
| `WeightForPollution` | score units | Multiplier of the city pollution ratio. |
| `WeightForCleanIndustryOrdinance`, `WeightForIndPollutantFeeOrdinance`, `WeightForWasteDisposalOrdinance` | score units | Subtracted when that ordinance is in effect. |
| `WeightForEachToxicWastePlant` | score units | Added per Toxic Waste Plant (agent type 19001) in the city. |
| `ThresholdScoreForToxicCloud` | score units | Random cloud possible only when score > threshold. |
| `ScorePerLevelIncrement` | score units | Score per severity level above the threshold (0 → 10). |
| `MinTimeForRainStart`, `MaxTimeForRainStart` | ticks | Delay until the cloud starts raining (Max forced > Min). |
| `MinRainDuration`, `MaxRainDuration` | ticks | Rain duration (Max forced > Min). |
| `MinGapBetweenRains` | ticks | Pause between rain spells. |
| `AbandonRadiusIncrementPerLevel`, `DestructionRadiusIncrementPerLevel` | tiles | Half-size of the abandonment / destruction rectangles is (level + 1) × increment. |
| `DestructionLikelihood`, `AbandonLikelihood` | per 1000 per tick | Chance per tick of one destruction / abandonment event while raining. |

Score [STATIC]: score = WeightForPollution × P_total / P_norm − Σ(weights of active ordinances:
Clean Industry, Industrial Pollutant Fee, Waste Disposal) + WeightForEachToxicWastePlant × N_toxic,
where the pollution term is computed as ⌊WeightForPollution × avgAir ÷ `MaxAirPolluteForUI`⌋.
The product is formed first, then divided with signed 32-bit integer division. avgAir is the
pollution layer's last completed sweep average (`17-sim-services.md` §7.4). `MaxAirPolluteForUI`
is the `SC3Pollution.ini` key of `17-sim-services.md` §7.2.2, the air scale maximum [STATIC]. Correction: P_total
and P_norm are exactly these two values; they were previously OPEN.

Random trigger [STATIC]: a cloud may start only if fewer than the maximum instances are active,
at least `AverageDurationBetweenOccurrences` has elapsed since the previous one, and score >
`ThresholdScoreForToxicCloud`. Severity level = ⌊(score − threshold) / ScorePerLevelIncrement⌋
`[INFERRED]`.

Life cycle (minute ticks) [STATIC]:

1. Premonition phases 0 → 1 → 2 → 3 at the `TimeBefore…Premonition` thresholds; the cloud becomes
   visible when the counter exceeds `TimeBeforeToxicCloudVisible`.
2. **Formation** (entering phase 4): the counter restarts; first rain start = RandRange(
   `MinTimeForRainStart`, `MaxTimeForRainStart`); cloud lifetime = RandRange(`MinCloudDuration`,
   `MaxCloudDuration`). Puffs are placed by scanning tiles outward from the **origin tile** (the
   start position) in the wrap-around raster order of §8.4: after every RandRange(8, 16) scanned
   tiles one puff is placed at a uniformly random point within ±6 tiles (±0x600 world units) of
   the scanned tile (if on the map), at a random altitude between `MinHeightAboveTerrain` and
   `MaxHeightAboveTerrain`, until the instance has its number of puffs (RandRange of
   `Min/MaxCloudsPerInstance`). The cloud's centre is then the centre of the puffs' bounding box.
3. **Drift**: all puffs move together along the start direction vector at the cloud speed
   (`Min/MaxToxicCloudSpeed`, converted as value × 256 / 60000, same unit as tornado speed §6.1).
   The disaster layer does not steer the cloud `[STATIC; drift performed by the puff effects]`.
4. **Rain**: when the counter passes the rain start, rain begins for RandRange(`MinRainDuration`,
   `MaxRainDuration`) ticks; when it stops, the next rain start is scheduled (`MinGapBetweenRains`
   and the rain-start range) `[INFERRED: exact gap arithmetic]`. Damage (below) is applied only
   while raining.
5. **End**: when there are no puffs left or the counter exceeds the lifetime, the cloud
   dissipates (phase 5) and the instance ends.

**Origin**: scripted clouds start at the given tile. Toxic Waste Plants (agent type 19001)
raise the random-trigger score but are **not** the origin; the random origin tile is chosen by
the random-start routine `[OPEN: random origin rule — likely a random tile, not located]`.

Damage while raining, each tick [STATIC]:

1. With probability `DestructionLikelihood`/1000: pick a uniformly random tile in the destruction
   rectangle (centred under the cloud); destroy its occupant with damage type `0x10` (burnt).
2. With probability `AbandonLikelihood`/1000: pick a random tile in the abandonment rectangle; if
   it holds a zoned lot building of agent type Residential (3000), Commercial (5000) or Industrial
   (8000) and the tile's zone type is not 22, apply damage type `0x02` (abandonment).

### 6.3 UFO attack ("visitation")

Keys in `[UFODisasterTunables]` [DATA; semantics INFERRED from key names and partial
static reading]:

| Key | Meaning |
|---|---|
| `MaxUFOInstances` | Maximum simultaneous UFO attacks. |
| `MinMonthsBetweenUFOAttacks`, `MaxMonthsBetweenUFOAttacks` | Window for the next random attack (months). |
| `PercentageChanceOfUFOAttackEachMonthTimes100` | Monthly chance, in 1/100 percent (1/10000), once inside the window. |
| `TimeBeforeFirstPremonitionMessage`, `…Second…`, `…Third…`, `TimeBeforeUFOAttack`, `TimeBeforeUFOAttackEnd` | Warning and attack schedule (minute ticks). |
| `MaxUFOsPerInstance`, `MaxUFOsPerInstanceCheatSwarm` | Saucers per attack (normal / cheat swarm). |
| `PercentageChanceOfUFOExplosionOnExitTimes100` | Chance (1/10000) a saucer explodes when leaving. |
| `MaxAbductionsMadePerAttack`, `MaxCropCirclesMadePerAttack` | Caps on abductions and crop circles per attack. |
| `MaxLandmarkBuildingsDestroyedPerAttack`, `MaxSpaceportBuildingsDestroyedPerAttack`, `MaxLowTechBuildingsDestroyedPerAttack`, `MaxHighTechBuildingsDestroyedPerAttack`, `MaxPowerBuildingsDestroyedPerAttack`, `MaxTotalBuildingsToDestroy` | Per-category and total destruction caps per attack. |
| `YearOfFirstPossibleVisitationAppearance`, `…AbductionAppearance`, `…CropCircleAppearance`, `…AttackAppearance` | Calendar years from which each behaviour is possible (earlier years: only harmless fly-overs, then abductions, then crop circles, then destructive attacks). |
| `PercentageOfBuildingsDestroyedByUFOBombs`, `PercentageOfBuildingsCatchFireByUFOBombs` | Percent of bombed buildings destroyed / set on fire. |

The UFO object persists a history of visits as dated records (Julian day numbers), see
§Persisted state.

**Random occurrence** (each random-check tick, i.e. monthly) [STATIC]:

1. Only if the number of active UFO attacks < `MaxUFOInstances`.
2. If the history list is empty, an entry dated "now" is created first (as for earthquakes, §7.2).
   Let *last* = min(today, date of newest history entry). Continue only if
   today − *last* > `MinMonthsBetweenUFOAttacks` × 30 days.
3. With probability Rand(10000) < `PercentageChanceOfUFOAttackEachMonthTimes100` an attack starts,
   with the warning flag set. `MaxMonthsBetweenUFOAttacks` is loaded but never used. [STATIC]

**Choosing the kind of visit** (every start, random or scripted) [STATIC]. Let *Y* = current
calendar year. Rand(100) draws are independent:

1. If active attacks ≥ `MaxUFOInstances`, or *Y* < `YearOfFirstPossibleVisitationAppearance`,
   nothing happens (start fails).
2. If *Y* ≥ `YearOfFirstPossibleAttackAppearance` and Rand(100) ≥ 10 (90 %): kind = **attack**
   (5). Otherwise continue:
3. If *Y* ≥ `YearOfFirstPossibleCropCircleAppearance` and Rand(100) ≥ 30 (70 %): kind =
   **crop circles** (4).
4. Else if *Y* ≥ `YearOfFirstPossibleAbductionAppearance` and Rand(100) ≥ 5 (95 %): kind =
   **abduction** (3).
5. Else kind = **visitation / fly-over** (2).

The kind number (2–5) is persisted with the instance (values > 5 on load are replaced by 2).
A started instance increments the active count and plays a UFO start notification.

**Phases** [STATIC]. The instance counts minute ticks; phase advances 0→1→2→3→4 when the counter
reaches `TimeBeforeFirstPremonitionMessage`, `…Second…`, `…Third…`, `TimeBeforeUFOAttack`
respectively (premonition messages at each step); the attack itself runs until
`TimeBeforeUFOAttackEnd`. Started without the warning flag, the instance starts directly in phase 3
with all premonition thresholds 0 and an attack window of `TimeBeforeUFOAttackEnd` −
`TimeBeforeUFOAttack` ticks (shipped 185 − 180 = 5). On load, a phase > 5 resets to 0 and a
counter > 10000 resets to 0; an instance loaded in a phase > 3 restarts at phase 3 with its counter
at `TimeBeforeUFOAttack`. Shipped timings: premonitions at 0 / 60 / 120 minute ticks, attack at 180
(3 game hours after the start).

**Saucers and their events** [STATIC]. The saucers themselves are a flying-object swarm in the
automata system (`26-automata.md`); the UFO disaster only chooses targets, hands them to the
swarm, and reacts to events the swarm reports back for a tile (x, z). Events are accepted only in
phases 4–5 (attack running):

| Swarm event | Effect at tile (x, z) |
|---|---|
| Bomb hits ground | r₁ = Rand(100). If r₁ ≤ `PercentageOfBuildingsCatchFireByUFOBombs`: **ignite** — if the occupant at the tile is in UFO category 0 (ordinary/low-tech, see below) and fewer than `MaxLowTechBuildingsDestroyedPerAttack` category-0 buildings have been hit, count it and set it on fire (§5.3, intensity 0). Otherwise (r₁ greater): with probability Rand(100) ≤ `PercentageOfBuildingsDestroyedByUFOBombs` **destroy** it: explosion effect (and a splash effect if the occupant stands over water), explosion sound (the explosion notification of `06-audio.md` §10.5), damage type `0x08`, cost added to the attack, per-category counter incremented. With the shipped values (100 / 100) every bomb takes the ignite branch. |
| Beam (destroy) | The occupant at the tile is destroyed with damage type `0x08` (no extra effect); cost and per-category counter are updated as above. |
| Abduction | Abduction counter += 1 (presentation only). |
| Crop circle | Only if every tile of the 4 × 4 block with corner (x, z) qualifies as crop land (agricultural lot, see locusts §6.4): a crop-circle decal is placed on the block and the crop-circle counter += 1. |

**Launching the swarm** (on entering the attack phase) [STATIC]. Let W be the map width in tiles
`[INFERRED: the map-size quantity used is the width]`.

1. Crop-circle targets (kinds 4 and 5) and bomb/beam targets (kind 5 only) are collected
   (capacity 256 each; rules below).
2. Saucer count n: kind 5 → max(6, ⌊W/16⌋ + 1); kind 4 → max(5, ⌊W/16⌋ + 1); kinds 2 and 3 → 1.
   Then n = min(n, `MaxUFOsPerInstance`). If the UFO-swarm cheat is active, n =
   `MaxUFOsPerInstanceCheatSwarm` instead. (The general formulas are max(3·(s + 2), …) and
   max(3·s + 5, …) with a strength parameter s that is always 0 in the shipped program.)
3. Kinds other than 5 make every saucer passive (no bombing). Kinds other than 2 add 5 roaming
   abduction passes. Each bomb target and each crop-circle target is handed to the swarm.
4. If the cheat swarm is used, or kind = 2, or fewer than 3 bomb targets, or fewer than 3 crop
   targets were found, every saucer additionally gets 2 random waypoints over the map.

**Bomb/beam target selection** (kind 5) [STATIC]. All map tiles are visited in a random
(shuffled) order. A tile contributes the occupant **anchored** at it, if that occupant is a
building lot that is not excluded by its lot flags `[OPEN: which lot flags exclude]`. The occupant
is classified by walking its agent-type hierarchy (`13-buildings-occupants.md`, `CitySim.ini
[AgentTypeHierarchy]`); the first matching ancestor decides:

| UFO category | Agent types (decimal) | Cap per attack |
|---|---|---|
| 4 power | 12101 *PowerPlant* | `MaxPowerBuildingsDestroyedPerAttack` |
| 3 high-tech | 13870 *University*, 1001 *Arcology*, 9100 *IndCleanHighDensity*, 12310 *Airport*, 12330 *Seaport*, 19001 (toxic waste plant) | `MaxHighTechBuildingsDestroyedPerAttack` |
| 2 spaceport | 12350 *Spaceport* | `MaxSpaceportBuildingsDestroyedPerAttack` |
| 1 landmark | 14160 *HistoricBuilding*, 18000 *Landmarks* | `MaxLandmarkBuildingsDestroyedPerAttack` |
| 0 low-tech | anything else, only if not flagged non-flammable/indestructible (occupant flag bits 0x400 or 0x800) | `MaxLowTechBuildingsDestroyedPerAttack` |

Targets are then emitted, up to `MaxTotalBuildingsToDestroy` in total, in this order: low-tech,
landmark, spaceport, high-tech, power, each list truncated to its cap. A target point is the
occupant's anchor tile plus half its footprint size (i.e. the lot centre).

**Crop-circle targets**: tiles holding a 1 × 1 occupant descended from agent type 9300
(*IndAgricultural*, farm field) whose lot has the crop flag `[STATIC; flag meaning INFERRED]`,
capped at `MaxCropCirclesMadePerAttack`.

`MaxAbductionsMadePerAttack` is loaded but never used. [STATIC]

**Per-category destruction counters**: category 1 → landmark, 2 → spaceport, 3 or 4 → a shared
high-tech/power counter, anything else → low-tech. [STATIC]

When the attack ends each saucer leaves; for each saucer independently, Rand(10000) <
`PercentageChanceOfUFOExplosionOnExitTimes100` makes it explode on exit (shipped 1000 = 10 %).

### 6.4 Locust swarm

Keys in `[LocustDisasterTunables]` [DATA; semantics INFERRED from names]: `MaxLocustInstances`,
`MinMonthsBetweenLocustAttacks`/`MaxMonthsBetweenLocustAttacks`,
`PercentageChanceOfLocustAttackEachYearTimes100` (yearly chance in 1/10000), `SwarmSpeed`
(loaded but unused by the disaster logic, see below), premonition/attack timers as for UFOs,
`MinLocustSwarmsPerInstance`/`MaxLocustSwarmsPerInstance`, `SwarmToCropRatio` (one swarm per this
many crop tiles), `MaxLocustsPerInstanceCheatSwarm`, `YearOfFirstPossibleVisitationAppearance`,
Locusts target agricultural industry (agent type 9300 *IndAgricultural*). The crop-duster
dispatch (command `0x1000A006`) is the counter-measure `[INFERRED]`.

**Random occurrence** (each random-check tick = monthly) [STATIC]:

1. Only if **no** locust attack is active (and the count is below `MaxLocustInstances`).
2. A month counter C (reset to 0 whenever a locust attack starts) is incremented.
3. If C > `MinMonthsBetweenLocustAttacks`: draw r = Rand(10000). An attack is due if r <
   `PercentageChanceOfLocustAttackEachYearTimes100` **or** C > `MaxMonthsBetweenLocustAttacks`
   (forced after the maximum). Despite the key name the chance is applied every monthly check.
4. A due attack starts (with the warning flag) only if the city contains at least one crop tile
   (as defined below).

**Start** (random or scripted): fails if active ≥ `MaxLocustInstances` or the current year <
`YearOfFirstPossibleVisitationAppearance` (shipped 0). [STATIC]

**Phases** (minute ticks) [STATIC]: first premonition at `TimeBeforeFirstPremonitionMessage`,
second at `TimeBeforeSecondPremonitionMessage`, swarm launched at `TimeBeforeLocustAttack`.
`TimeBeforeThirdPremonitionMessage`, `TimeBeforeLocustAttackEnd` and `SwarmSpeed` are loaded or
present but not used by the disaster logic (`SwarmSpeed` is read into a variable that nothing
reads; the swarm's speed belongs to the automaton, `26-automata.md`) `[STATIC]`. Without the
warning flag the instance starts at the launch phase with zero thresholds and launches at once.

**Launching the swarm** [STATIC]:

1. Collect up to `MaxNumCropsToAttack` crop targets (N = number found).
2. Number of swarms: if N = 0, 1 or 2 with equal probability (Rand(2)); otherwise
   ⌊(N − 1) / `SwarmToCropRatio`⌋ + 1, clamped to [`MinLocustSwarmsPerInstance`,
   `MaxLocustSwarmsPerInstance`]. With the swarm cheat active, `MaxLocustsPerInstanceCheatSwarm`.
3. A locust-swarm automaton is created with that many swarms and the target list.

**Crop tiles / targets** [STATIC]: tiles are scanned in a fixed order (x = 0…W−1 outer, z = 0…H−1
inner, **not** random) and every tile passing the crop test is recorded with its x, z and the
terrain height there, until `MaxNumCropsToAttack` are found. The crop test is the same as for UFO
crop circles: the tile's occupant is a 1 × 1 lot descended from agent type 9300
(*IndAgricultural*) with the lot's crop flag set `[flag meaning INFERRED: field tile of a farm]`.

**What locusts destroy** [STATIC]: when the swarm automaton reports that a swarm has eaten at a
tile (x, z), the occupant there is destroyed with damage type `0x20` (§9; crops only — the swarm
only reports its crop targets), its cost is added to the attack and the eaten-crop counter is
incremented. Locusts do not destroy flora or other buildings. Other swarm reports (swarm reaches a
tile, swarm dispersed, crop-duster contact) only update presentation and the swarm.
`[INFERRED: exact meaning of the non-damaging reports]`

### 6.5 Whirlpool

Started with a magnitude 0–1 (§3). Appears on water, pulls in boats and destroys marinas and
bridges (damage type `0x08`) — sound items `WhirlpoolDestroysBridge`, `WhirlpoolDestroysMarina`,
`WhirlpoolSucksUpBoat` in `AUDIO.INI`. `[WhirlpoolDisaster]` is empty. No tunables are shipped in `SC3DisasterLayer.ini`, but `SimTune.INI [Tunables]` IDs
`0x24860901`–`0x24860917` are read by the whirlpool: (shipped values in order) 1, 24, 96, 25, 85,
83, 7, then `0x2486090E`–`0x24860917`: 0, 20, 40, 60, 100, 30000, 120, 250000, 1000, 2400. By
analogy with the earthquake table the first block is: max instances (1), minimum gap in months
(24) and a second period (96), magnitude min/max in tenths (25/85), occurrence chance per 10000
(83), and a size parameter (7); the second block holds the warning-phase thresholds (0/20/40/60),
then size/strength/timing values (superseded by the confirmed table below). The same IDs are also read
by the unused, never-instantiated disaster implementation (§2).

Whirlpool tunables, confirmed by use [STATIC]:

| Tunable ID | Meaning (shipped) |
|---|---|
| `0x24860901` | Maximum simultaneous whirlpools (1). |
| `0x24860902` | Minimum gap since the last whirlpool, months; × 30 days (24). |
| `0x24860903` | Loaded, never used (96). |
| `0x24860904` / `0x24860905` | Magnitude min / max, tenths (25 / 85): magnitude = (min + (max − min) × m) / 10 for the start parameter m ∈ [0, 1]. |
| `0x24860906` | Random-occurrence chance per random-check tick, in 1/10000 (83). |
| `0x24860907` | Damage radius R in tiles: half-size of the square around the whirlpool centre that is scanned for victims (7). |
| `0x2486090E`–`0x24860911` | Phase thresholds T0–T3 in minute ticks (0 / 20 / 40 / 60). |
| `0x24860912` | Loaded, never used (100). |
| `0x24860913` | Duration handed to the vortex/water-effect objects it creates (30000) `[INFERRED: milliseconds]`. |
| `0x24860914` | Vortex spin parameter, used × 0.001 (120 → 0.12) `[INFERRED: rotation per tick]`. |
| `0x24860915`–`0x24860917` | Three parameters of the vortex sprite effect (250000 / 1000 / 2400) `[OPEN: presentation only]`. |

Random occurrence (monthly) uses the same rule as the UFO and earthquake: active < max, history
gap > `0x24860902` × 30 days, then Rand(10000) < `0x24860906`. [STATIC]

**Placement** [STATIC]:

1. Find every 8 × 8-tile rectangle consisting entirely of water tiles (`11-world-terrain.md`). If
   there is none, the whirlpool does not start.
2. Keep the rectangles that contain (any tile of) an occupant descended from agent type 12330
   *Seaport* or 14430 *Marina*. If at least one is kept, choose among those; otherwise among all.
3. Up to 16 attempts: choose a rectangle uniformly at random, choose a random point inside it
   (in world units, kept clear of the rectangle's far edges by 6 tiles) and accept it if it lies
   on the map and on a water tile. `[STATIC; exact inset arithmetic INFERRED]`
4. Magnitude = (min + (max − min) × m) / 10 as in the table; the active count is incremented.

**Phases** [STATIC]: the instance steps through premonition phases at minute-tick thresholds
T0–T3 (shipped 0 / 20 / 40 / 60); without the warning flag all thresholds are 0. At the end of
the premonitions a vortex sprite effect is created at the chosen point; it then reports events
back to the whirlpool:

| Vortex event | Effect |
|---|---|
| position update at tile (x₀, z₀) | **Coast damage**: every tile (x, z) with x₀ − R ≤ x < x₀ + R and z₀ − R ≤ z < z₀ + R (R = `0x24860907`) whose occupant is descended from *Seaport* (12330) or *Marina* (14430) is destroyed (damage type `0x08`, sound `AUDIO.INI` item 240, splash and debris effects). The whirlpool sound's on-screen state is refreshed. |
| object (boat, bridge piece) at a tile drawn in | **Boats and bridges**: the object under the vortex is removed — buildings via the occupant layer, and bridge/network pieces via the network layers — counting destroyed items and their cost `[STATIC; per-layer details INFERRED]`. Sound items `WhirlpoolSucksUpBoat`, `WhirlpoolDestroysBridge`. |
| sound cue at a position | positional sound `AUDIO.INI` item 251. |
| vortex finished | the instance ends and is recorded in the history. |

Other coastal buildings, flora and terrain are not affected. [STATIC] Duration: the vortex effect
decides when it is finished `[INFERRED: effect duration parameter `0x24860913`]`.

### 6.6 Space junk

Started with a magnitude 0–1 (§3). Debris falls at points on the map, destroying what it hits
and shaking the screen (sound items `SpaceJunkHits`, `SpaceJunkHitsWater`,
`SpaceJunkShakesScreen`). `SimTune.INI [Tunables]` IDs `0x44860901`–`0x44860906` and `0x4486090E`–`0x44860918` are read by
space junk (shipped: 1, 120, 720, 1, 99, 20; then 0, 60, 120, 180, 300, 3000000, 30, 50, 300000, 10,
1960): max instances, minimum gap and second period in months, magnitude min/max, occurrence chance
per 10000, then warning-phase thresholds (0/60/120/180), and size/timing values ending with the
first calendar year in which space junk can occur (1960) (superseded by the confirmed table below).
The object persists a dated history (§Persisted state).

Space-junk tunables confirmed by use [STATIC]:

| Tunable ID | Meaning (shipped) |
|---|---|
| `0x44860901` | Maximum simultaneous space-junk events (1). |
| `0x44860902` | Minimum gap since the last event, months; × 30 days (120). |
| `0x44860903`, `0x44860904`, `0x44860905` | Loaded, not used by the disaster logic (720 / 1 / 99). |
| `0x44860906` | Random-occurrence chance per random-check tick, in 1/10000 (20). |
| `0x4486090E`–`0x44860912` | Phase thresholds T0–T4 in minute ticks (0 / 60 / 120 / 180 / 300). |
| `0x44860913` | Lifetime/duration handed to the falling-debris automaton (3000000) `[INFERRED: milliseconds]`. |
| `0x44860914` | Base "small impact" percentage B (30). |
| `0x44860915` | Base "crater" percentage threshold A (50). |
| `0x44860916` | Duration of the crater decal effect (300000) `[INFERRED: milliseconds]`. |
| `0x44860917` | Maximum terrain height difference across the crater's corners for a crater decal to be drawn (10). |
| `0x44860918` | First calendar year in which random space junk can occur (1960). |

Random occurrence (monthly) [STATIC]: active < max; current year ≥ `0x44860918`; history gap >
`0x44860902` × 30 days; then Rand(10000) < `0x44860906`. Started with the warning flag.

**Instance set-up** [STATIC]: the stored magnitude *mag* = ⌊m × 10⌋ for the start parameter m
(scenario: argument × 0.1, §3), so *mag* is in tenths. Magnitude class from *mag*/10: ≤ 4.0 →
class 1 (debris-shower parameter 2000); ≤ 6.0 → class 2 (5000); > 6.0 → class 3 (10000)
`[OPEN: whether the parameter is the number of pieces or a duration in ms; the falling pieces
are produced by the debris automaton (26-automata.md), which reports each ground impact back]`.
Phases advance at T0–T3 (premonitions); the debris automaton is created with duration
`0x44860913` and the event runs until T4 (+10 ticks grace) `[INFERRED]`. Without the warning flag
the instance starts in phase 4 with zero thresholds. On load: *mag* < 10 or phase > 7 resets the
phase to 1, counter > 10000 resets to 0, and a saved phase 5 resumes at phase 4.

**Impact of one piece** at world point (x, z) [STATIC]. Let *mag* be the event's magnitude value
(1–99). Adjust the thresholds by magnitude band:

| *mag* | A | B |
|---|---|---|
| < 40 | A₀ + 10 | B₀ + 10 |
| 40–59 | A₀ | B₀ |
| 60–79 | A₀ − 10 | B₀ − 10 |
| ≥ 80 | A₀ − 20 | B₀ − 20 |

(A₀ = `0x44860915`, B₀ = `0x44860914`; shipped A/B = 60/40, 50/30, 40/20, 30/10.)

1. If the piece lands in water: positional sound `AUDIO.INI` item 250 (`SpaceJunkHitsWater`),
   nothing else.
2. Otherwise r = Rand(100). In every case positional sounds 243 (`SpaceJunkHits`) and 200 play at
   the impact tile, plus a splash effect if the occupant there stands over water. Then:
   * r ≤ B: nothing more (near miss).
   * B < r < A: the occupant at the impact tile is **set on fire** (§5.3, intensity 0) and counted.
   * r ≥ A: **crater** of strength c = (r − A) / ((100 − A) × 0.1), a value in [0, 10). If c ≥ 9
     the screen shakes (strength 1000) and sound 244 (`SpaceJunkShakesScreen`) plays. Crater
     footprint, with (i, k) the impact tile: c ≤ 3.3 → tiles i−1…i+2 × k−1…k+2; 3.3 < c ≤ 6.7 →
     i−2…i+2 × k−2…k+2; c > 6.7 → i−2…i+3 × k−2…k+3; clipped to the map. If the footprint
     contains water nothing more happens. Otherwise the terrain in the footprint is lowered into a
     bowl (per-vertex depths drawn with RandInt(2, 4) in a fixed pattern; larger craters use a
     fixed 12-point pattern) and the occupants on the deformed tiles are lost as for any terrain
     edit (`11-world-terrain.md`); when the four corner heights of the footprint differ by ≤
     `0x44860917` a crater decal is drawn. [STATIC; per-vertex pattern INFERRED]

Number and spread of pieces: decided by the debris automaton, which reports each impact
`[OPEN; see the class parameter above]`. Damage radius per impact: 1 tile (fire/near miss) or the
crater footprint (4 × 4, 5 × 5 or 6 × 6 tiles) given above.

## 7. Earthquake

### 7.1 Tunables

All in `SimTune.INI [Tunables]` (hex IDs). Units: "minute ticks" are the disaster-instance minute
tick (agent type `0xC3629AFA` *DisasterInstanceMinuteTick* in `CitySim.ini`; see `14-sim-clock.md`).
[DATA][STATIC]

| Tunable ID | Meaning (shipped value) |
|---|---|
| `0xE423750F` | Maximum simultaneous earthquakes (1). |
| `0xE4237518` | Minimum gap between earthquakes, in months; converted to days as value × 30 (96). |
| `0x44237523` | Loaded but not used by the earthquake logic (480). [STATIC] |
| `0x0423752B` / `0x64237533` | Minimum / maximum magnitude, in tenths of a Richter unit (25 / 85). |
| `0x6423753A` | Random occurrence chance per earthquake tick, in 1/10000 (40). |
| `0x64237541` / `0x44237548` / `0x4423754E` | Per-tile destruction chance near the fault for magnitude class 1 / 2 / 3, in 1/10000 (5000 / 6000 / 7000). |
| `0x44237555` / `0x6423755E` / `0xA4237564` | Per-tile ignition chance (when not destroyed) for class 1 / 2 / 3, in 1/10000 (3000 each). |
| `0x2423756A` / `0x24237579` / `0x6423757F` | Damage half-width around the fault, in tiles, for class 1 / 2 / 3 (2 / 4 / 6). |
| `0x84237585` | Loaded, unused (50). |
| `0xA423758D`,`0x24237593`,`0x442375AA` | Main-shock screen-shake strength for class 1 / 2 / 3 (1000 / 3000 / 5000). |
| `0x442375AF`,`0x842375BE`,`0xC42375C5` | Main-shock screen-shake parameter (duration/frequency) for class 1 / 2 / 3 (1 / 2 / 4). |
| `0xE42375CA`,`0x442375D3`,`0xC42375D9` | Fore-shock (premonition) shake strength for class 1 / 2 / 3 (100 / 500 / 1000). |
| `0x842375E4`,`0xE42375E9`,`0x842375EF` | Fore-shock shake parameter for class 1 / 2 / 3 (0 / 1 / 1); 0 disables the fore-shock shake. |
| `0x442375F6`,`0x842375FB`,`0xC4237602`,`0x04237608` | Phase thresholds T1–T4, in minute ticks since the quake object was created (0 / 60 / 120 / 180). |
| `0x2423760E`,`0x64237616`,`0x8423761C` | Loaded; role `[OPEN]` (1 / 1 / 1). |
| `0xC4237621`,`0x64237626`,`0x6423762B` | Fault-scarp primary vertical offset range per class 1 / 2 / 3 (1 / 1 / 2). |
| `0x24295C2C`,`0x44237637`,`0x4423763D` | Fault-scarp secondary offset per class 1 / 2 / 3 (0 / 0 / 0). |

### 7.2 Random occurrence

On every earthquake tick, if fewer than the maximum quakes are active: [STATIC]

1. Read the current date as a Julian day number. The earthquake keeps a history list of past
   quakes (persisted); if it is empty, an entry for "now" is created first (so a new city never
   gets a quake before the minimum gap has elapsed).
2. Let *last* = min(today, date of the most recent history entry). If today − *last* > gap × 30,
   then with probability chance/10000 (Rand(10000) < chance) a random quake starts with a
   magnitude parameter *m* drawn uniformly from [0, 1).

### 7.3 Magnitude and classes

Richter magnitude *M* = (Min + (Max − Min) × *m*) × 0.1, then stored rounded to tenths
(⌊*M* × 10 + 0.5⌋ / 10). Scenario `cmdStartEarthquake` supplies *m* directly (§3). [STATIC]

| *M* | Class | Fault width (tiles across) |
|---|---|---|
| *M* ≤ 4.0 | 1 | 1 |
| 4.0 < *M* ≤ 6.0 | 2 | 3 |
| *M* > 6.0 | 3 | 5 |

### 7.4 Fault line (footprint)

World coordinates are tiles × 256. Let W = width × 256, H = height × 256. [STATIC]

1. Repeat: z_A = RandInt(0, H); z_B = RandInt(z_A − ¾H, z_A + ¾H) until |z_B − z_A| ≥ 1024
   (4 tiles).
2. x_A = 0, x_B = W − 1: the fault crosses the whole map from the x = 0 edge to the far x edge.
   If z_B falls outside [0, H−1] it is clamped to the edge and x_B is moved back along the line
   so the end point stays on the line.
3. End-point heights are the terrain heights there. The epicentre is the point of the line at
   z = RandInt(z_A, z_B).
4. The line is rasterised (up to 1281 cells) from A to B in tile units. Each cell is jittered by
   −1, 0 or +1 tile (RandInt(−1, 2)) in x and clamped to the map.
5. For class > 1 the line is widened by (class − 1) cells on each side, perpendicular to its
   dominant direction (in z if |Δx| > |Δz|, otherwise in x).

### 7.5 Phases (timing)

The quake object counts minute ticks from its creation and steps through phases: [STATIC]

| Phase | Ends when counter ≥ | On entering next phase |
|---|---|---|
| 1 | T1 | premonition sound/notification 1 |
| 2 | T2 | premonition 2 and fore-shock screen shake (class strength) |
| 3 | T3 | premonition 3 |
| 4 | T4 | **main shock**: fault cells are computed, the earthquake sound starts (if fewer than 2 quakes are already sounding), the date is recorded, the main screen shake starts |
| 5 | shake finishes | — |
| 6 | immediately | damage applied (§7.6), sound stops, quake object is retired and added to the history |

When the quake is started with the scripted/forced flag **clear** (random or menu quakes start
with it set — see §4), all thresholds are 0 and the quake begins directly at phase 4 (no
premonitions). [STATIC; flag semantics INFERRED]

### 7.6 Damage versus distance

Damage does not fall off with distance from the epicentre; it depends only on distance from the
fault line: [STATIC]

1. Terrain (scarp) [STATIC]: only for class ≥ 2. Let P = the class's primary scarp offset
   (`0x64237626` class 2, `0x6423762B` class 3) and Q = its secondary offset (`0x44237637`,
   `0x4423763D`; shipped 0). Walk the fault cells in order i = 0, 1, 2, … with a run boundary
   S = RandRange(3, 7) (range generator with exclusive upper bound, i.e. 3–6):
   * i = S: primary offset p = 1; secondary s = −(h/2) if Q ≠ 0, else 0;
   * i < S: p = RandRange(P, P + 1) (= P); s = RandRange(Q, Q + 1) if Q ≠ 0, else 0;
   * i > S: p = 0; s = −(h/4) if Q ≠ 0, else 0; then S += RandRange(3, 7).
   (h is the terrain's height-step constant.) The result is runs of raised cells separated by
   single level cells — a rift that steps along the fault. For the fault cell at column x with
   fault row z, the four terrain vertices (x, z−1), (x, z), (x, z+1), (x, z+2) are processed:
   vertex z−1 is moved by p, the other three by s; each new height is clamped to the terrain's
   minimum/maximum height. Vertices that are locked (the terrain refuses edits there) are skipped.
   Before editing, occupants on those vertices are removed (class 3: always; class 2: via the
   terrain-edit check). If s ≠ 0 and a vertex that was above sea level ends within 10 height units
   of it, the water there is refilled (`11-world-terrain.md`). With the shipped tunables (Q = 0)
   only vertex z−1 changes: +2 (class 3) or +1 (class 2) on run cells, +1 at boundary cells, 0 on
   the cells after a boundary.
2. Buildings: for every fault cell (x, z) and every tile (x, z') with |z' − z| ≤ half-width(class)
   inside the map:
   * with probability Pdestroy(class)/10000 the occupant anchored at that tile is destroyed
     (§9; with a 1-in-50 chance a visual explosion/splash effect is also spawned);
   * otherwise with probability Pignite(class)/10000 it is set on fire (§5.3, intensity 0) unless
     it is a non-flammable occupant (occupant flag bit 0x400);
   * otherwise nothing.
   Only the occupant whose anchor tile is exactly (x, z') is affected.
3. The earthquake also updates the history entry used by §7.2.


## 8. Riots

### 8.1 Tunables

`[RiotDisaster]` in `SC3DisasterLayer.ini` gives `0x4296380E=<MaxRiots>,<SoundID>` (shipped 3, 63 =
`DisasterRiot`). The rest are in `SimTune.INI [Tunables]`: [DATA][STATIC]

| Tunable ID | Name used here | Meaning (shipped) |
|---|---|---|
| `0x6484A25D` | UnrestThreshold | Riots (random) only when the city unrest statistic U (0–100) ≥ this (8). |
| `0x2484A264` / `0x6484A26A` | IntervalMin / IntervalMax | Random-check interval range (48 / 300). |
| `0x4484A270` | SiteCoverageMax | A tile can host a riot only if its police coverage byte < this (50). |
| `0x4484A278` | SearchRadius | Half-size in tiles of the square searched around a random point (40). |
| `0xE484A27D` / `0xA484A283` | DurMax / DurMin | Riot duration at zero / full police coverage, in minute ticks (240 / 20). |
| `0x2484A289` / `0x2484A290` | — | Pair passed to the rioter crowd automaton (300000 / 600000), see §8.4 `[INFERRED: milliseconds]`. |
| `0x2484A29E` | WarnDelay | Ticks from creation to the "riot brewing" warning (0). |
| `0x2484A2A5` | BrewTime | Ticks from the warning until the riot breaks out (80). |
| `0x4484A2AC` | GrowthPerTick | Added to the riot's strength every tick while active (5). |

Derived: slope = (IntervalMax − IntervalMin) / (100 − UnrestThreshold) (0 if the denominator ≤ 0);
kDur = (DurMax − DurMin) / 255.

### 8.2 Conditions and random trigger

The unrest statistic U is a 0–100 city-wide value supplied by the simulation `[OPEN: which
statistic — believed to combine crime and unemployment; owned by 17-sim-services.md /
20-sim-population.md]`. Each random-check tick: [STATIC]

1. Counter += 1. If active riots ≥ MaxRiots, or U < UnrestThreshold, stop.
2. Base interval I = IntervalMin + trunc((100 − U) × slope), and I = IntervalMin if that is
   smaller. Randomisation: lo = trunc(I × 0.8), hi = trunc(I × 1.2); if lo < hi the effective
   interval is a fresh RandRange(lo, hi) (the layer's range generator, §13) drawn **every check**,
   otherwise it is I. Higher unrest gives shorter intervals. [STATIC]
3. When counter ≥ interval: counter = 0. Number of riots to start
   n = trunc((1 − (A + 128) / 256) × (MaxRiots − active)), clamped to [1, MaxRiots], where A is a
   signed city-wide byte statistic (−128…127) read from another subsystem `[STATIC formula;
   INFERRED: A is the mayor-rating/approval value, so a popular mayor gets fewer simultaneous
   riots]`. For each: pick a random tile (Rand(width), Rand(height)), sweep the square of
   half-size SearchRadius around it (clipped to the map) and start a riot at the first tile whose
   police coverage < SiteCoverageMax and whose occupant is a valid riot site (a building
   reachable from the road network `[INFERRED]`). Random riots start with the warning flag set.

Game-time scale: the check is the monthly random-check tick (§4.1), so with shipped values the
interval runs from 48 months (U = 100) to 300 months (U = 8), ±20 %.

### 8.3 Riot life cycle

| Phase | Behaviour | Leaves when |
|---|---|---|
| 0 Brewing | — | counter ≥ WarnDelay → phase 1 and a "riot brewing" news item (one of 6 variants) |
| 1 Warning | — | counter ≥ BrewTime: if the riot was scripted without warning, or U is still ≥ UnrestThreshold, and the site is still valid → phase 2 (rioters spawned, news, sound starts); otherwise the riot fizzles and is removed |
| 2 Active | strength += GrowthPerTick each tick; every 5 ticks the on-screen test for the riot sound runs (§8.4); every 8 ticks the end test runs | counter ≥ trunc(DurMax − cov × kDur), where cov is the police coverage byte at the riot site → riot ends (news item, one of 3 variants), sound stops |

A riot started without the warning flag begins in phase 1 with the counter at BrewTime, i.e. breaks
out on the next tick. [STATIC]

### 8.4 Spread, damage and suppression

* Rioters set buildings on fire: each ignition goes through the fire rules (§5.3, intensity 0)
  and is counted on the riot. [STATIC]
* Suppression is purely through police coverage at the riot site: stations and dispatched police
  units (§10.3) raise coverage, shortening the duration formula above and preventing new riot
  sites (SiteCoverageMax). [STATIC]

**Riot sites and the site search** [STATIC]. A valid riot site is a tile whose police-coverage byte
< SiteCoverageMax and whose occupant has occupant flag bit `0x8000` set (`13-buildings-occupants.md`;
`[INFERRED: the "can host a crowd" lot flag]`). It is the same flag bit that makes a tile eligible
for pedestrian spawning (`26-automata.md` §5.8) [STATIC]. The search around a start point (cx, cz) covers
the rectangle [cx − SearchRadius, cx + SearchRadius] × [cz − SearchRadius, cz + SearchRadius]
clipped to the map, scanning row by row starting **at** (cx, cz): x increases (wrapping from the
rectangle's right edge back to its left edge) until it returns to cx, then z advances the same
way (wrapping from the bottom edge to the top). The first valid tile wins. The same scan order is
used by the random-fire target search (§5.2).

**Rioters, movement and spread** [STATIC]:

* On breaking out (phase 2) a rioter crowd automaton (`26-automata.md`) is attached to the site's
  occupant, with the two parameters `0x2484A289` / `0x2484A290` (shipped 300000 / 600000,
  `[INFERRED: crowd wander/lifetime times in milliseconds]`). The crowd walks the streets; the
  disaster layer does not move rioters tile by tile.
* When the crowd reports that it is at a tile that the riot accepts, the occupant there is set on
  fire (§5.3, intensity 0) and counted on the riot.
* When the riot is told to relocate (its crowd lost its site), up to 3 attempts are made: pick a
  uniformly random map tile (RandRange(0, W−1), RandRange(0, H−1)), run the site search around it,
  and if the site found differs from the current one, move the riot and a new crowd there. If no
  attempt succeeds the riot ends.
* Every 5 active ticks the riot only re-tests whether its site is on screen, switching the riot
  sound on/off; every 8 ticks the end test (§8.3) runs.


## 9. Occupant destruction, rubble and radiation

Every disaster destroys occupants through one routine of the disaster layer, which takes the
occupant and a **damage type**: [STATIC]

| Damage type | Used by | Result left behind |
|---|---|---|
| `0x02` | toxic rain (abandonment zone) | the lot is switched to its *abandoned* state (no rubble) [INFERRED from the INI key names] |
| `0x08` | tornado, whirlpool, UFO beam/bomb | rubble |
| `0x10` | fire, toxic cloud | ashes (buildings) / burnt tree (flora) |
| `0x20` | locusts (farm-field lots, §6.4); flora | burnt tree for flora; for a lot, the lot's damaged state of type `0x20` `[OPEN: replacement occupant for lots]` |

Rules, in order:

1. Disaster destruction is ignored unless the layer is in its normal active state.
2. Occupants of agent type 12850 (*Landfill*, `CitySim.ini [AgentTypes]`) are never destroyed.
3. If the occupant is a zoned/lot building that is already in a destroyed state equal to the
   requested type, or already burnt (type `0x10`), or already rubble (type `0x08`) and the request
   is `0x08` or `0x02`, nothing happens.
4. The occupant's value is added to the disaster cost (§10.2) except when it is a lot building
   already in a destroyed state of type `0x10` or `0x08`.
5. The occupant is removed from the world.
6. Replacement: for a lot building the lot is switched to the damaged state of the given type and
   its rubble/ashes occupant is placed at the same position (agent types 15410/15411
   *RubbleBuilding/RubbleFiller*, 15420/15421 *AshesBuilding/AshesFiller*). For flora (agent type
   300) destroyed by `0x10` or `0x20`, a burnt variant of the same flora family is placed at the
   tile's terrain height (variant index 8 for fire, 9 otherwise).
7. **Nuclear fall-out.** If the destroyed occupant is of agent type 12115 (*NuclearPlant*), every
   tile within Euclidean distance ≤ 20 (rounded to nearest) of the plant's centre tile is marked
   radioactive in the radiation layer: tiles at distance ≤ 10 always; tiles at distance 11–20
   with probability 9/10 each, independent of the distance (one Rand(10) draw per tile, in the
   order the tiles are visited; a draw of 0 leaves the tile clean). This applies to any destruction of a nuclear
   plant by any disaster, and to the power-plant explosion of `16-sim-utilities.md` if it goes
   through this routine `[OPEN]`.


## 10. Emergency response and dispatch

### 10.1 State of emergency

The player can declare an emergency (the "emergency" button of the disaster UI). Behaviour: [STATIC]

1. A UI sound plays.
2. If an emergency is already active, nothing else happens.
3. If **no** disaster is active, a *false-alarm counter* is incremented; if a disaster **is**
   active, the false-alarm counter is reset to 0 after the notification below.
4. A notification is broadcast carrying (disaster active?, false-alarm counter < 3) so the advisor
   can comment.
5. If false-alarm counter < 3: the emergency becomes active, its elapsed-month counter is reset,
   and if a disaster is active the "emergency declared during disaster" (EW) flag is set.
   With 3 or more consecutive false alarms the declaration is refused.

While an emergency is active the elapsed counter is incremented once per simulated month; when it
exceeds `MaxEmergencyDuration` the emergency ends (active cleared, counter reset, an
"emergency ended" notification is broadcast). [STATIC]

### 10.2 Disaster cost and relief

* Each occupant destroyed by a disaster adds its value (the occupant's cost/price attribute) to
  an accumulated **disaster cost**. [STATIC]
* When the last active disaster ends: an "all disasters over" notification is broadcast, any
  active emergency ends, and **relief** is paid: if accumulated cost > `MinDisasterCostToGetRelief`,
  relief = ⌊pct × cost / 100⌋ (integer arithmetic on unsigned 32-bit values) is credited to the
  city treasury, where pct = `ReliefAsPercentageOfCostWithEW` if the EW flag is set, otherwise
  `ReliefAsPercentageOfCostWithoutEW`. Then cost = 0 and the EW flag is cleared. [STATIC]
* Relief is only paid while the layer's "relief enabled" condition holds `[OPEN: exact
  condition; believed to be normal city mode]`.

### 10.3 Dispatching units

Three dispatch tools exist (cursor files `DISPATCH FIREFIGHTERS CURSOR.cur`, `DISPATCH POLICE
CURSOR.cur`, `DISPATCH PLANE CURSOR.cur`): fire-fighters, police, and the crop-duster plane used
against locusts (§6.4). There is **no military dispatch** in this game. The fire and police tools
are enabled only while a disaster is active (§4.4). [STATIC]

Fire-fighter and police dispatch are implemented by the fire-protection and police layers
(`17-sim-services.md`). Behaviour on dispatching to tile (x, z): [STATIC]

1. **Unit cap.** Let S = number of stations of that service in the city and U = number of units
   currently deployed. If U ≥ S + 1, the oldest deployed unit is recalled first (its coverage
   contribution is removed and its occupant deleted). Hence at most S + 1 units are deployed.
2. A unit occupant is placed at (x + 1, z) (x + 1 clamped to the map) at terrain height:
   fire-fighters use occupant key `0x207EDC0E:0x0000057E:0x000032CC`, police use
   `0x207EDC0E:0x0000057E:0x000032CB` (resources from the user's data; see
   `13-buildings-occupants.md`). A dispatch sound plays (`AUDIO.INI` item 35, `UiDispatchFire`,
   for fire-fighters).
3. **Effect.** The unit stamps its service's coverage layer: for every tile whose
   floor(Euclidean distance) to (x, z) is ≤ R = `DispatchRadius`, the raw coverage accumulator of
   the **4 × 4-tile cell** containing that tile (`17-sim-services.md` §2.1) is increased by
   `DispatchEffect`. A cell therefore gains up to 16 × `DispatchEffect` when the whole cell is inside
   the disc, and its coverage byte is recomputed as clamp(raw / 16, 0, 255), i.e. the average over
   the cell's 16 tiles. (The stamping routine supports a linear fall-off from an inner to an
   outer effect value, effect − (effect − effectEdge) × d / R; dispatch passes the same value for
   both, so the effect is flat over the disc.)
4. Recalling a unit reverses its stamp.

| Key | File | Units | Meaning (shipped) |
|---|---|---|---|
| `DispatchRadius` | `SC3FireLayer.ini [TuningParameters]` | tiles | Radius of a fire-fighter unit's effect (4). |
| `DispatchEffect` | `SC3FireLayer.ini [TuningParameters]` | raw coverage units (1/16 of a coverage byte step) | Coverage added inside the radius (225). |
| `DispatchRadius` | `SC3Police.ini [TuningParameters]` | tiles | Radius of a police unit's effect (4). |
| `DispatchEffect` | `SC3Police.ini [TuningParameters]` | raw coverage units | Police coverage added inside the radius (200). |

Fire-fighters therefore fight fires only through *cov* (§5.4). Police units suppress riots through
police coverage (§8). [STATIC]

## 11. Strikes

Fire and police strikes are not disasters (they are not objects of the disaster layer) but are
disaster-like and are implemented in the fire and police layers. Fire-department strike: [STATIC]

1. Each fire-layer update while not on strike: strike chance (per 10000) is
   * 10000 if the department is already on strike (as in `17-sim-services.md` §3.4; since the
     chance is only evaluated while not on strike, this case has no effect here);
   * 0 if there are no fire stations, or funding % ≥ `MinAcceptableFunding`, or the city
     flammability statistic ≤ `MaxAcceptableFlam` `[OPEN: which statistic — believed average
     flammability]`;
   * otherwise (`MinAcceptableFunding` − funding %)².
   If Rand(10000) < chance the strike starts: strike counter = 0 and a "fire strike started"
   notification is broadcast (used by the advisors and the news ticker).
2. While on strike, each update increments the counter; the strike ends when counter >
   `MaxStrikeDuration`, or funding reaches 100 %, or there are no stations.
3. During a strike every station's coverage contribution is multiplied by
   `StrikeEfficiency` / 100 (integer arithmetic).

The police strike uses the same scheme with `SC3Police.ini` keys `MinAcceptableFunding`,
`MaxAcceptableCrime` (in place of flammability), `StrikeEfficiency` and `MaxStrikeDuration`
`[INFERRED: same structure]`. `UpdatePeriod` in both files is the layer update period; see
`17-sim-services.md`.


## 12. Effects and sprite sequencing

Disaster visuals are sprite effects drawn by the renderer (`25-rendering.md`); sprite archives are
IXF `.dat` files with `.sii` text sidecars (`02-containers.md`, `04-graphics-formats.md`).
All four shipped sidecars parse, and every image ID they list is present in its
archive (EffectSprites 499/499, locust 55/55, space junk 280/280, toxic cloud 45/45). [DATA]

| Archive | Sidecar | Contents (image-ID high word → set) |
|---|---|---|
| `Res/Sprites/EffectSprites.dat` | `EffectSprites.sii` (499 records) | groups `FFFC`, `FFFD`, `FFFE`: tornado (50 frames), plasma ball (10), UFO (5), clouds (16 × 3), debris (80), explosion (4 × 20), water splash (10) |
| `Res/Sprites/disaster_LOCUST.dat` | `disaster_LOCUST.sii` (declares 56, lists 55) | `3FAC` fly, `3FAD` eat, `3FAE` die, `3FB1` giant locust, `3FB9` death spray, `3FCB` shadow |
| `Res/Sprites/disaster_SPACEJUNK.dat` | `Disaster_SPACEJUNK.sii` (280) | `3FB2`–`3FB7` debris pieces, `3FC5`, `3FC8`–`3FCA` impact effects |
| `Res/Sprites/disaster_TOXICCLOUD.dat` | `disaster_TOXICCLOUD.sii` (45) | `4858` cloud (opening / cycle / closing, each with alpha cells), `4859` toxic rain loop (+ alpha top/bottom) |
| `Res/Sprites/disaster_WHIRLPOOL.dat` | none shipped | 95 image/alpha pairs |

Each archive stores every image twice, as two resource types (0 and 1) with the same instance —
colour cells and alpha cells. Within a set, consecutive image IDs are the five zoom levels of one
frame (smallest first), followed by the five alpha cells (sidecar comment convention). [DATA]

Behavioural sequencing [STATIC/INFERRED]:

* **Fire**: one fire effect per burning building; the animation is chosen by intensity (small,
  medium, large) and switched at every intensity transition (§5.4); destruction spawns the
  explosion effect (`EffectSprites` explosion set) and leaves ashes.
* **Tornado**: warning phases show no funnel; at touch-down the tornado set is played looping
  (variant + 1 selects the sub-set) and moves with the instance; each destruction spawns an
  explosion, over water a splash.
* **Toxic cloud**: opening frames → looping cycle → closing frames; while raining the toxic-rain
  loop is drawn under the cloud.
* **Locusts**: fly loop while travelling, eat loop over crops, die sequence and death spray when
  killed by crop-dusting; a shadow sprite follows each swarm.
* **Earthquake**: no sprite set; screen shake (§7.5) plus explosion/splash effects at 1 in 50
  destroyed tiles.
* Sounds: `Res/Sound/Sfx/disasters/*` items mapped by `AUDIO.INI` (see `06-audio.md`); disaster
  loops are driven with start / stop / intensity commands as described in §5.1.


## 13. Random number generators

All disaster randomness uses the game generator defined in `14-sim-clock.md` §8 (also summarised
in `16-sim-utilities.md` §1.1): integer LCG *s* ← *s* × 0x41C64E6D + 0x3039 (mod 2³²) whose output is bits
16…47 of the 64-bit value *s*_old × 0x41C64E6D + 0x3039; `Rand(n)` = output mod *n* (for
*n* < 0xFFFFFF; 0 when *n* = 0). The disaster objects each own a generator seeded from the
system millisecond clock at construction, so disaster randomness is not reproducible across runs
and need not be bit-exact. [STATIC]

A "centre-weighted" variant is used for some ranges: for range [a, b], let m = ⌊(b−a)/2⌋, draw
r = min(Rand(m), Rand(m)), negate r with probability ½, result = a + m + r. [STATIC]

## Persisted state

All disaster records live in the inner record database of the city's main packed segment
(`10-save-format.md`). Each begins with the common versioned header (u16 version, u16 flags = 2,
u32 `0xDEADBEEF`). Census over all 59 shipped `.sc3`/`.SNR`/`.st3`/`.sct` files: the layouts
below parse 59/59 files. [DATA]

### Layer record — key `206C6E7C:21F6ABCA:00000000` (present in 59/59, always 28 bytes)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | `0x01F9` |
| 0x02 | 2 | u16 | flags | 2 |
| 0x04 | 4 | u32 | sentinel | `0xDEADBEEF` |
| 0x08 | 4 | u32 | reserved0 | Written from a layer counter that is zeroed at initialisation; 0 in all shipped files. [STATIC] |
| 0x0C | 4 | u32 | emergencyActive | Non-zero while a state of emergency is active (§10.1). |
| 0x10 | 4 | u32 | flagA | Boolean (0/1) `[OPEN]`. |
| 0x14 | 4 | u32 | disasterCost | Accumulated destruction cost of the current disaster episode (§10.2). One shipped file holds a non-zero, apparently uninitialised value here; treat any value as valid. |
| 0x18 | 4 | u32 | flagB | Boolean (0/1) `[OPEN]`. |

Note: the original does not round-trip `flagA` and `disasterCost` faithfully: after loading, the
game state behind these two fields is not restored to what was saved (an original defect; the
exact effect is [OPEN]). Both are only meaningful during a disaster episode. A compatible engine
should read and write the five fields in this order, restore each to its own meaning, and preserve
unknown values. [STATIC]

### Per-disaster records — key `E1F6ABE2:<G>:00000000`

| G | Disaster | Shipped sizes (files) | Body after header |
|---|---|---|---|
| `621CDA33` | Fire | 12 (59) | s32 count of burning instances (0 in all shipped files), then 12-byte elements (u32, u32, s32; the two u32 are the tile position [INFERRED]) — see `10-save-format.md` §6.5 |
| `02963821` | Tornado | 12 (59) | s32 count of active tornadoes, then 33-byte elements (`10-save-format.md` §6.5) |
| `4296380E` | Riot | 12 (59) | s32 count of active riots, then 20-byte elements (`10-save-format.md` §6.5) |
| `22963800` | Toxic cloud | 12 (49) | s32 count of active clouds, then 32-byte elements (`10-save-format.md` §6.5) |
| `C336F77C` | Earthquake | 48 (59) | u32 count N, then N × 36-byte history entries |
| `0336F76A` | UFO | 60 (58), 108 (1) | u32 count N, then N × 48-byte history entries |
| `24889F78` | Space junk | 36 (47), 84 (2) | u32 count N, then N × 24-byte history entries |
| `45326359` | Locusts | 36 (46), 40 (3) | version 1: u32 count N, N × 24-byte entries; version 2: one extra u32 before the count |
| `C446F87C` | Whirlpool | 44 (47), 76 (2) | u32 count N, then N × 32-byte history entries |
| `C4C90997` | Parade | 46 (49) | version 2; start tile, route rectangle and state words (`10-save-format.md` §6.5); 22 shipped files hold uninitialised filler (`CD`) in the route — write zeros |

The 10 files that lack the toxic-cloud, space-junk, locust, whirlpool and parade records are older
(pre-Unlimited) files; a loader must create default state when a record is absent. [DATA]

History entries (earthquake, UFO, space junk, locusts, whirlpool) are arrays of u32: [DATA][INFERRED]

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | date | Julian day number of the occurrence (e.g. 2415021 = 1 January 1900). |
| 0x04 | 4·(k−1) | u32[] | details | Type-specific values; the last two words are believed to be "buildings destroyed" and "destruction cost" `[INFERRED]`. |

Every shipped file has at least one history entry per type: a seed entry holding only a date
(1 January 1900 for earthquake and UFO; the city's date for the newer types), created so the
minimum-gap rules (§7.2) have a reference. The writer caps N at 50000. [DATA][STATIC]

The city-level "disasters enabled" flag is persisted by the city settings record, not here
(`10-save-format.md`). [STATIC]

## SimTune.INI coverage

Every tunable in `SimTune.INI [Tunables]` is consumed by the disaster subsystem: [DATA][STATIC]

| ID range | Consumer | Section |
|---|---|---|
| `0xE423750F`–`0x4423763D` and `0x24295C2C` | Earthquake | §7.1 |
| `0x047DF002`–`0x047DF061` | Fire | §5.1 |
| `0xE48381BC`–`0xE4838253` | Tornado | §6.1 |
| `0x6484A25D`–`0x4484A2AC` | Riot | §8.1 |
| `0x24860901`–`0x24860917` | Whirlpool (and the unused implementation, §2) | §6.5 |
| `0x44860901`–`0x44860918` | Space junk | §6.6 |

Tunables absent from the file keep built-in defaults `[OPEN: defaults not tabulated]`.

## Open questions

* Issuer of the per-minute disaster tick (scheduler command 3, `14-sim-clock.md` §6.3 rule 5);
  game-time conversion in §4.1 assumes one minute tick per simulated minute.
* Field meanings of active fire, tornado, riot and toxic-cloud instances in their save records
  (element types are known, `10-save-format.md` §6.5; all shipped files have none active).
* Meaning of the detail words of history entries and of two boolean fields of the layer record.
* Riot: which subsystem supplies the unrest statistic U and the signed statistic A (§8.2); the
  rioter crowd's walking rule (owned by the automata, `26-automata.md`).
* Tornado: whether the sprite effect adds visual wobble to the straight path (§6.1).
* UFO: which lot flags exclude bomb targets; saucer flight paths (swarm automaton).
* Locusts: replacement occupant left by damage type `0x20` on a farm lot.
* Whirlpool: exact inset of the random centre inside the chosen water rectangle; meaning of the
  three vortex effect parameters.
* Space junk: meaning of the class parameter 2000/5000/10000 (piece count or duration), how many
  pieces fall and how they are spread (debris automaton), exact crater vertex pattern.
* Toxic cloud: origin tile of random (non-scripted) clouds.
* Which flammability statistic gates fire-fighter strikes (§11).
* Purpose of the unused, never-instantiated tenth disaster implementation (§2).
* Occupant-group membership tests (agent-type hierarchy, `CitySim.ini [AgentTypeHierarchy]`) are
  assumed to follow `13-buildings-occupants.md`.
