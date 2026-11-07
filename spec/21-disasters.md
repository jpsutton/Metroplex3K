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
| `MaxEmergencyDuration` | days | Upper bound on how long a declared emergency (and its emergency-worker relief bonus) lasts; see §10. Shipped value `8`. |
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
The emergency elapsed counter advances once per simulated day (daily item 1 in
`14-sim-clock.md` §6.1), not on the monthly random-check tick. [STATIC]

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
   starting there in the outward square spiral of §8.4 (every map tile once). For each tile that
   holds an occupant whose type belongs to the chosen group, try to ignite it (§5.3); stop at the
   first success. [STATIC]
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

Destroyed buildings become rubble through the generic destroy routine (§9). **Rubble and ashes
have no lifetime**: no timer removes them. Developers skip lots in the rubble (`0x08`) and burnt
(`0x10`) states, and the growth site check rejects them unless the new building has the same agent
type, so rubble stays until the player demolishes it (`DemolishRubbleTile` cost,
`SC3BudgetLayer.ini`). [STATIC negative search over the growth, building and occupant code;
INFERRED conclusion]

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

Movement and damage [STATIC]: in phase 5 the funnel effect moves the tornado (§12.5); each minute
tick elapsed += 1; when elapsed ≥ lifetime (or the tornado leaves the map) it ends. A damage step
happens each time the funnel's reported position enters a **new tile** (correction: not once per
minute tick). On each step, with probability Rand(100) ≤ DestroyChance[variant], the occupant at the current tile is
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
  the disaster layer never alters the direction, and the effect adds **no wobble or jitter**
  (§12.5) [STATIC]. Random tornadoes therefore travel in a straight line with a random heading in the
  half-plane of increasing z. The variant changes only the destroy chance and the debris-throw
  chance (§12.5), not the funnel sprites.
* **Width.** Damage is applied only to the occupant at the single tile under the tornado's
  centre on each step (1 tile wide); a large building is hit whenever any of its tiles is under
  the centre `[INFERRED]`. Ground speed in tiles per second = speed value / 60 (shipped random range
  1–2 tiles/s) [STATIC].
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
   start position) in the outward square-spiral order of §8.4: after every RandRange(8, 16) scanned
   tiles one puff is placed at a uniformly random point within ±6 tiles (±0x600 world units) of
   the scanned tile (if on the map), at a random altitude between `MinHeightAboveTerrain` and
   `MaxHeightAboveTerrain`, until the instance has its number of puffs: n = `MinCloudsPerInstance`
   + ⌊(`MaxCloudsPerInstance` − `MinCloudsPerInstance`) × (level + 1) / 5⌋ (correction: not a random
   range) [STATIC]. The cloud's centre is then the centre of the puffs' bounding box.
3. **No drift**: the cloud speed (`Min/MaxToxicCloudSpeed`, value × 256 / 60000) and the start
   direction are handed to the puffs, but the puffs never apply them (§12.7), and the disaster layer
   does not move them: the cloud stays where it formed [STATIC].
4. **Rain**: when the counter passes the rain start, rain begins for RandRange(`MinRainDuration`,
   `MaxRainDuration`) ticks; when it stops, the next rain start is scheduled (`MinGapBetweenRains`
   and the rain-start range) `[INFERRED: exact gap arithmetic]`. Damage (below) is applied only
   while raining.
5. **End**: when there are no puffs left or the counter exceeds the lifetime, the cloud
   dissipates (phase 5) and the instance ends.

**Origin**: scripted clouds start at the given tile. Toxic Waste Plants (agent type 19001)
raise the random-trigger score but are **not** the origin. Random clouds [STATIC]:

1. Severity level = Rand(5); direction = (sin θ × s, 0, cos θ × s) with θ uniform in [0, 2π) and
   speed s = centre-weighted random (§13) between the converted `Min/MaxToxicCloudSpeed`; started
   with the warning flag (phase 3 timing from `TimeBeforeToxicCloudVisible`).
2. Start point (cx, cz) = centre-weighted random (§13) in [0, W−1] × [0, H−1] (biased to the map
   centre). From there the whole map is visited in the outward spiral of §8.4.
3. A tile qualifies if a per-tile city layer value is 9, 10 or 11 (score 2 / 3 / 4) `[INFERRED:
   the tile's zone type, the three densities of one zone family, probably industry]`, with +4 if an
   occupant test on the tile also passes `[OPEN: which test]`. At the first qualifying tile the qualifying tiles contiguous with it in the
   four axis directions (−x, −z, +x, +z arms) are summed. If that sum > 79 the origin is that tile;
   otherwise the best such cross so far is remembered and the scan continues.
4. At the end of the scan the best cross is used; if no tile qualified, the start point itself.

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
| Crop circle | Only if every tile of the 4 × 4 block with corner (x, z) qualifies as crop land (agricultural lot, see locusts §6.4): the occupant `0x207EDC0E:0x0000057E:0x000013CE` ("Crop Circle", `OccupantAttribs.IXF`, 4 × 4 footprint) is placed with its anchor at (x, z) and the crop-circle counter += 1. It is an ordinary occupant with no removal timer [STATIC; persistence INFERRED]. Whether placement first clears the field occupants under it is not traced. Original quirk: the saucer reports its hover point (lot centre), which this test treats as the block corner. |

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
building lot whose lot state is not abandoned (`0x02`), rubble (`0x08`) or burnt (`0x10`) (normal,
under-construction and locust-eaten lots qualify; there is no historical-building test) [STATIC]. The occupant
is classified by walking its agent-type hierarchy (`13-buildings-occupants.md`, `CitySim.ini
[AgentTypeHierarchy]`); the first matching ancestor decides:

| UFO category | Agent types (decimal) | Cap per attack |
|---|---|---|
| 4 power | 12101 *PowerPlant* | `MaxPowerBuildingsDestroyedPerAttack` |
| 3 high-tech | 13870 *University*, 1001 *Arcology*, 9100 *IndCleanHighDensity*, 12310 *Airport*, 12330 *Seaport*, 19001 (toxic waste plant) | `MaxHighTechBuildingsDestroyedPerAttack` |
| 2 spaceport | 12350 *Spaceport* | `MaxSpaceportBuildingsDestroyedPerAttack` |
| 1 landmark | 14160 *HistoricBuilding*, 18000 *Landmarks* | `MaxLandmarkBuildingsDestroyedPerAttack` |
| 0 low-tech | anything else, only if its occupant flag word has neither 0x400 (flora) nor 0x800 (network piece) (`13-buildings-occupants.md` §3.1a) | `MaxLowTechBuildingsDestroyedPerAttack` |

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
incremented. Locusts destroy no other buildings, but the swarm automaton itself gives flora it
flies over damage type `0x20` (burnt tree, §12.9) [STATIC]. The "arrived" report triggers the
crop-duster check below; "all gone" has no handler. [STATIC]

**Crop duster** [STATIC]: a dispatch (command `0x1000A006`) at tile (x, z) places the dispatch
icon occupant at (x + 1, z) (x + 1 only if x < W − 1) and sends a crop-duster plane there; at most 3
dispatches are active — a 4th reuses (retargets) the oldest. Each spray point (x_s, z_s) the plane
reports marks the rectangle x_s ± 3 × z_s ± 3 (tiles) as sprayed (never cleared during the attack).
When a swarm **arrives** at a crop whose ±3 rectangle intersects any sprayed rectangle (i.e. within
6 tiles on both axes of a spray point), that swarm fades out and is removed (§12.9); swarms are not
checked in flight.

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
| `0x24860913` | Lifetime of the floating wreck objects the whirlpool creates, counted in ≈ ms (30000 ≈ 30.7 s) (§12.6). |
| `0x24860914` | Vortex travel speed, × 0.001 → world units per ms (120 → 0.12, ≈ 0.47 tiles/s) (§12.6). |
| `0x24860915`–`0x24860917` | Vortex effect: frame period µs (250000); centre angular speed × 10⁻⁸ rad/µs (1000); capture half-size in world units (2400) (§12.6). |

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

Other coastal buildings, flora and terrain are not affected. [STATIC] Duration: the whirlpool
stops the vortex after its attack phase; the vortex then closes (≈ 2.75 s) and reports finished
(§12.6). The vortex wanders over open water (§12.6), so the damage square moves with it.

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
class 1 (parameter 2000); ≤ 6.0 → class 2 (5000); > 6.0 → class 3 (10000). The class parameter
is stored but **never read** (not saved either; recomputed on load) — it is neither a piece count
nor a duration [STATIC]. Phases advance at T0–T3 (premonitions); at T3 the debris launcher is
started with interval scale `0x44860913` (§12.11) and at T4 it is stopped; the instance then waits
until no piece is left in flight or the counter reaches T4 + 250 [STATIC]. Without the warning flag
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

Number and spread of pieces [STATIC]: one piece per launcher interval (RandRange(768n, 1536n) µs,
n = ⌊`0x44860913`/1024⌋; shipped 2.25–4.5 s of real time) during the T3–T4 window, each at a uniform
random point of the map (2-tile border), landing ≈ 3.2 tiles east of that point (§12.11). Only pieces
that hit a building or the ground are handled by the rules above; a water hit only plays the water
sound. Damage radius per impact: 1 tile (fire/near miss) or the crater footprint given above.

Crater terrain pattern [STATIC] (vertex offsets from the clipped footprint origin; rim vertices
rise, centre vertices sink; each write clamped to the terrain range and done only if the result
stays above sea level; RandInt(a, b) is a…b−1):

| Crater | Rim (rise by RandInt) | Centre (sink) |
|---|---|---|
| large | 12 vertices (2,1)(3,1)(4,1)(1,2)(5,2)(1,3)(5,3)(1,4)(5,4)(2,5)(3,5)(4,5): +2…3 | (3,3): −1…2 |
| medium | 8 vertices (2,1)(3,1)(1,2)(4,2)(1,3)(4,3)(2,4)(3,4): +1…2 | (2,2)(3,2)(2,3)(3,3): −1…2 |
| small | 4 vertices (2,1)(1,2)(3,2)(2,3): +1…2 | (2,2): −1…2 |

A large crater whose footprint was clipped by the map edge falls back to the small pattern; a
clipped small one is not deformed. Deformation needs every per-tile destruction in the footprint
to succeed and no water tile in it. The decal (§12.11) is drawn when both corner diagonals of the
footprint differ by ≤ `0x44860917` (heights read after the deformation). Original defects that an
implementation may fix: the water-impact sound uses z/25 instead of the tile z; the
footprint clipping tests x against both W and H and never tests the far z edge [STATIC].

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
     it is a flora occupant (occupant flag bit 0x400, `13-buildings-occupants.md` §3.1a);
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
| `0x4484A270` | SiteAuraMax | A tile can host a random riot only if the local aura value there (signed) < this (50). Correction: not police coverage. |
| `0x4484A278` | SearchRadius | Half-size in tiles of the square searched around a random point (40). |
| `0xE484A27D` / `0xA484A283` | DurMax / DurMin | Riot duration at zero / full police coverage, in minute ticks (240 / 20). |
| `0x2484A289` / `0x2484A290` | — | Range of the crowd's throw interval in µs (300000 / 600000 = 0.3–0.6 s), §12.10. |
| `0x2484A29E` | WarnDelay | Ticks from creation to the "riot brewing" warning (0). |
| `0x2484A2A5` | BrewTime | Ticks from the warning until the riot breaks out (80). |
| `0x4484A2AC` | GrowthPerTick | Added to the riot's strength every tick while active (5). |

Derived: slope = (IntervalMax − IntervalMin) / (100 − UnrestThreshold) (0 if the denominator ≤ 0);
kDur = (DurMax − DurMin) / 255.

### 8.2 Conditions and random trigger

The unrest statistic U is the city's **unemployment percentage**: the newest monthly sample of the
"Unemployment" graph series (`09-ui-windows.md` graphs; pushed monthly as 0 if no residents, else
with L = round(residents × workforce fraction) and J = jobs: 0 if L ≤ J, 100 if J = 0, otherwise
⌊(100L − 100J) / (2L)⌋). It is therefore refreshed monthly and lies in 0–49 unless there are no jobs.
[STATIC] Each random-check tick: [STATIC]

1. Counter += 1. If active riots ≥ MaxRiots, or U < UnrestThreshold, stop.
2. Base interval I = IntervalMin + trunc((100 − U) × slope), and I = IntervalMin if that is
   smaller. Randomisation: lo = trunc(I × 0.8), hi = trunc(I × 1.2); if lo < hi the effective
   interval is a fresh RandRange(lo, hi) (the layer's range generator, §13) drawn **every check**,
   otherwise it is I. Higher unrest gives shorter intervals. [STATIC]
3. When counter ≥ interval: counter = 0. Number of riots to start
   n = trunc((1 − (A + 128) / 256) × (MaxRiots − active)), clamped to [1, MaxRiots], where A is a
   the city **aura** value (−127…127, the city-wide average of the aura layer, refreshed at the end of
   each daily aura sweep, `17-sim-services.md` §8.5) [STATIC] — a city with high aura gets fewer
   simultaneous riots. For each of the n riots, independently (a failed try is silent): pick a random
   tile (Rand(width), Rand(height)), search the square of half-size SearchRadius around it
   (clipped to the map) in the outward spiral of §8.4 and start a riot at the first tile whose local
   aura value < SiteAuraMax and whose occupant is a road piece (flag `0x8000`). Random riots start
   with the warning flag set.

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
* Suppression is through police coverage at the riot site: stations and dispatched police
  units (§10.3) raise coverage, shortening the duration formula above. New random riot sites are
  limited by local aura (SiteAuraMax), not coverage. [STATIC]
* Ignition by the crowd needs only an occupant at the hit tile that passes the ordinary ignition
  test (flammability ≥ MinFlam, not burning, §5.3); there is no lot-flag or coverage exclusion.
  [STATIC]

**Riot sites and the site search** [STATIC]. A valid random riot site is a tile whose local aura
value < SiteAuraMax and whose occupant has occupant flag bit `0x8000` set, i.e. a **road piece** (any crossing that
includes road counts) (`13-buildings-occupants.md` §3.1a) [STATIC]. It is the same flag bit that makes a tile eligible
for pedestrian spawning (`26-automata.md` §5.8) [STATIC]. The search around a start point (cx, cz) covers
the rectangle [cx − SearchRadius, cx + SearchRadius] × [cz − SearchRadius, cz + SearchRadius]
clipped to the map, in an **outward square spiral** starting **at** (cx, cz): the start tile, then
legs of growing length 1, 1, 2, 2, 3, 3, … alternating between the x and z axes with the direction
reversing every two legs (+x 1, +z 1, −x 2, −z 2, +x 3, +z 3, …). Tiles outside the rectangle are
skipped; the spiral ends once it has passed the rectangle on all four sides, so every tile of the
rectangle is visited exactly once, nearest rings first. The first valid tile wins, i.e. the
nearest qualifying road tile. Correction: earlier versions described a wrapping raster. The same
spiral is used by the random-fire target search (§5.2), the toxic puff placement (§6.2) and the
toxic origin search. [STATIC]

**Rioters, movement and spread** [STATIC]:

* On breaking out (phase 2) one rioter crowd (a vehicle-type automaton, §12.10) is created on the
  site's road piece with the throw-interval range `0x2484A289` / `0x2484A290`. The crowd drives the
  streets; the riot site follows the crowd ("entered tile" reports). The disaster layer does not
  move rioters itself.
* When a projectile thrown by the crowd hits a building, the occupant there is set on fire (§5.3,
  intensity 0) and counted on the riot.
* When the riot is told to relocate (its crowd was destroyed or its script ended), up to 3 attempts
  are made: pick a uniformly random map tile (RandRange(0, W−1), RandRange(0, H−1)), spiral over the
  **whole map** from there with the road-piece test only (no aura test), and if the road tile found
  differs from the current site, move the riot and a new crowd there. Because the spiral returns the
  nearest road tile, an attempt often finds the current site again and is wasted. If no attempt
  succeeds the riot ends. [STATIC]
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
| `0x20` | locusts (farm-field lots, §6.4); flora under a swarm (§12.9) | burnt tree for flora; a lot is put back in state `0x20`, drawn with the `[DefaultBuildingData]` model for that state (shipped key `32:1:100:F` = "Eaten 1x1 Crops Filler", occupant `0x207EDC0E:0x57E:16312`) |

Rules, in order:

1. Disaster destruction is ignored unless the layer is initialised for the current city and the
   occupant is placed.
2. Occupants of agent type 12850 (*Landfill*, `CitySim.ini [AgentTypes]`) are never destroyed.
3. If the occupant is a zoned/lot building that is already in a destroyed state equal to the
   requested type, or already burnt (type `0x10`), or already rubble (type `0x08`) and the request
   is `0x08` or `0x02`, nothing happens.
4. The routine returns the destruction cost = the occupant type's cost + footprint w × h × the
   per-tile demolition cost (budget tool cost index 12) [STATIC; index meaning INFERRED], except
   when it is a lot building already in a destroyed state of type `0x10` or `0x08` (no cost). The
   **calling disaster** adds it to its own per-instance total; the instance total is added to the
   layer's disaster cost (§10.2) when that disaster ends [STATIC].
5. The occupant is removed from the world (through the city's removal service; if that fails,
   directly from the occupant database).
6. Replacement: a lot building is put back as the **same object** with its lot state set to the
   damage type (`0x08` rubble, `0x10` burnt, `0x20` eaten, `0x02` abandoned); its look is then chosen
   by state from `SC3BuildingLayer.ini [DefaultBuildingData]` (`25-rendering.md`). No separate
   rubble occupant is created (correction: agent types 15410/15411/15420/15421 are the stand-in
   models, not occupants placed by this routine). For flora (agent type 300) destroyed by `0x10` or
   `0x20`, a burnt variant of the same flora family is placed at the tile's terrain height (variant
   index 8 for fire, 9 otherwise; family 4 if the lookup fails). Anything else is simply removed.
   The routine sends no message, sound or effect itself and does not touch network pieces —
   callers do (explosions, splashes, network removal) [STATIC].
7. **Nuclear fall-out.** If the destroyed occupant is of agent type 12115 (*NuclearPlant*), every
   tile within Euclidean distance ≤ 20 (rounded to nearest) of the plant's centre tile is marked
   radioactive in the radiation layer: tiles at distance ≤ 10 always; tiles at distance 11–20
   with probability 9/10 each, independent of the distance (one Rand(10) draw per tile, in the
   order the tiles are visited; a draw of 0 leaves the tile clean). This applies to any destruction of a nuclear
   plant by any disaster. The centre tile is the anchor + half the footprint. Whether the
   power-plant explosion of `16-sim-utilities.md` goes through this routine is not traced.


## 10. Emergency response and dispatch

### 10.1 State of emergency

The player can declare an emergency (the "emergency" button of the disaster UI). Behaviour: [STATIC]

1. A UI sound plays.
2. If an emergency is already active, nothing else happens.
3. If **no** disaster is active, a *false-alarm counter* is incremented; if a disaster **is**
   active, the false-alarm counter is reset to 0 after the notification below.
4. A notification is broadcast carrying (disaster active?, false-alarm counter < 3) so the advisor
   can comment.
5. If false-alarm counter < 3: the emergency becomes active, its elapsed-day counter is reset to
   0, and if a disaster is active the "emergency declared during disaster" (EW) flag is set.
   With 3 or more consecutive false alarms the declaration is refused.

While an emergency is active the elapsed counter is incremented once per simulated **day** (the
disaster layer's daily schedule entry — `14-sim-clock.md` §6.1 daily item 1). After each increment,
if the counter is greater than `MaxEmergencyDuration` the emergency ends (active cleared, counter
reset, an "emergency ended" notification is broadcast). With the shipped value 8 that is the 9th
day after declaration. [STATIC]

### 10.2 Disaster cost and relief

* Each occupant destroyed by a disaster adds its value (the occupant's cost/price attribute) to
  an accumulated **disaster cost**. [STATIC]
* When the last active disaster ends: an "all disasters over" notification is broadcast, any
  active emergency ends, and **relief** is paid: if accumulated cost > `MinDisasterCostToGetRelief`,
  relief = ⌊pct × cost / 100⌋ (integer arithmetic on unsigned 32-bit values) is credited to the
  city treasury, where pct = `ReliefAsPercentageOfCostWithEW` if the EW flag is set, otherwise
  `ReliefAsPercentageOfCostWithoutEW`. Then cost = 0 and the EW flag is cleared. [STATIC]
* The "relief enabled" condition is simply that the layer is initialised for the current city
  (set at the end of its post-load initialisation, cleared at shutdown; not saved, not changed by
  any message), so in play relief is always paid when cost > `MinDisasterCostToGetRelief`
  (unsigned, strict). The cost is reset to 0 whether or not relief was paid. [STATIC]

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
     flammability statistic ≤ `MaxAcceptableFlam` (u8, strict >); the statistic is the truncated
     mean of the flammability layer's cell values (0–255) over **non-zero** cells only, refreshed
     once per full sweep of that layer [STATIC];
   * otherwise (`MinAcceptableFunding` − funding %)².
   If Rand(10000) < chance the strike starts: strike counter = 0 and a "fire strike started"
   notification is broadcast (used by the advisors and the news ticker).
2. While on strike, each update increments the counter; the strike ends when counter >
   `MaxStrikeDuration`, or funding reaches 100 %, or there are no stations.
3. During a strike every station's coverage contribution is multiplied by
   `StrikeEfficiency` / 100 (integer arithmetic).

The police strike uses the same scheme with `SC3Police.ini` keys `MinAcceptableFunding`,
`MaxAcceptableCrime` (in place of flammability), `StrikeEfficiency` and `MaxStrikeDuration`
[STATIC]. Its crime statistic is the crime layer's mean over **crime-generating cells** only: each
2 × 2-tile cell whose signed crime potential is > 0 adds 4 × its crime byte to the sum and 4 to the
count; statistic = sum / count (not a city-wide average) [STATIC]. `UpdatePeriod` in both files is the layer update period; see
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
| `Res/Sprites/disaster_SPACEJUNK.dat` | `Disaster_SPACEJUNK.sii` (280) | `3FB2`–`3FB6` debris pieces, `3FB7` impact explosion, `3FC5`, `3FC8`–`3FCA` crater decals |
| `Res/Sprites/disaster_TOXICCLOUD.dat` | `disaster_TOXICCLOUD.sii` (45) | `4858` "toxic steam" on the ground under the rain (opening / cycle / closing, each with alpha cells); `4859` toxic rain loop (+ alpha top/bottom masks) |
| `Res/Sprites/disaster_WHIRLPOOL.dat` | none shipped | 95 image/alpha pairs: vortex sets `4859xxxx` (see §12.6) and splash sets `2D49`, `4864` |

Each archive stores every image twice, as two resource types (0 and 1) with the same instance —
colour cells and alpha cells. Within a set, consecutive image IDs are the five zoom levels of one
frame (smallest first), followed by the five alpha cells (sidecar comment convention). [DATA]

### 12.1 Common machinery [STATIC unless tagged]

* **Units.** Positions are world units (256 per tile; altitude y = terrain altitude × 256 at
  ground level). Every effect advances by elapsed **real time in microseconds** (`26-automata.md`
  §7); velocities below are in world units per **millisecond** (dt_ms = dt_µs × 0.001). Effects keep
  running while the simulation is paused.
* **Frame strips.** One image per (set, zoom) holds all animation frames side by side
  horizontally. Frame pitch = SpanL + SpanR (the sidecar/span record of the image); frame *f* is the
  cell starting at x = *f* × (SpanL + SpanR); image width = frames × pitch (verified on every set
  quoted below [DATA]). The registration point is (SpanL, SpanT) inside the cell.
* **Zoom index.** z = 0 (farthest) … 4 (closest). Fire, explosion, splash, space-junk pieces,
  craters, saucers and locusts use the image of zoom z directly ("direct zoom"). Tornado funnel and
  spray, toxic puffs, toxic rain and the whirlpool vortex use **shifted zoom**: image index
  k = z + 1 for z < 4 and k = 4 at z = 4, where the zoom-4 image is drawn magnified 2× (spans doubled);
  the smallest image (k = 0) of these sets is never used.
* **Detail level (machine class).** Not a player setting: a machine class 1–7 computed from a CPU
  probe at every start (not saved): CPU generation < 5 → 1; 5 → 2 (3 if > 189 MHz); 6 → 4 (5 if
  > 259 MHz); 7–9 → 5 (6 if > 299 MHz); > 9 → 7. The `-p a b c d` command line overrides it with c
  clamped to 5–10. The same value is the m of `26-automata.md` §7.1. It gates: ground shadows of
  puffs, saucers, locusts and debris (class ≥ 4); explosion frame 0 (skipped below 4); explosion and
  tornado debris counts (§12.3, §12.5); the giant-locust count (§12.9). [STATIC]
* **Draw class.** Effects are inserted into the city renderer with a small integer draw class:
  fire flames 2, explosion 1, splash 2, puff 5 (its shadow 1), toxic rain 1, vortex 1, crater 1,
  saucer/beam/bomb/shadow 1, locust 1. All are depth-sorted with the ordinary sprite list of
  `25-rendering.md` by their (x, z, y) position `[INFERRED: class semantics not resolved]`.

### 12.2 Fire flames [STATIC][DATA]

* One flame effect per burning building. **It always starts at flame size 0** (the requested
  animation is ignored when the flame is attached); each later intensity transition sets size 1 /
  2 / 3 for intensity 0 / 1 / 2, so a fire shows size 0 until its first transition. Extinguishing
  removes the flames. [STATIC]
* Sprites: `EffectSprites` colour `0xFFFE1000 + size × 5 + z`, alpha `0xFFFE1020 + size × 5 + z`.
  15 frames per strip; spans per zoom (L,T,R,B) (4,11,4,1) (8,23,8,1) (16,47,16,1) (32,91,32,1)
  (64,183,64,1) — registration point at the bottom centre.
* Frame period: random per flame, 400 000 + Rand(100 000) µs (0.4–0.5 s); loops (15 → 0).
* Placement on the building: the building's screen rectangle (w × h) is sampled up to 32 times at
  sx = w/8 + Rand(3w/4), sy = Rand(h − 4z); the first point that hits an opaque pixel of the building
  sprite is converted back to a world point and the flame drawn there. With no building, the tile
  centre at ground height. This is done **once**, when the flame is created, at the zoom and rotation
  of that moment; size changes, zoom and rotation never move it (after a rotation the flame may no
  longer sit over the building's pixels). [STATIC]

### 12.3 Explosion [STATIC][DATA]

* Parameters: size 0–3, variant. Callers: fire destruction (size 3, variant 0), tornado hit (size 3,
  variant 2), UFO bomb (size of the bomb, variant 1), UFO exit (variant 5), space-junk impact
  (variant 8).
* Sprite ID = base + v × 0x20 + size × 5 + z with colour base `0xFFFD1000`, alpha base `0xFFFD1200`;
  v = variant with 4 and 6 → 0 and 5 → 1. Sets: v 0 "fire" (sizes 0–3), v 1 "plasma", v 2 "dirt"
  (single size, size forced 0). Variant 8 uses `disaster_SPACEJUNK` `0x3FB70000 + rotation + z × 4`
  (alpha `0x3FB70014 + …`; rotation = view rotation 0–3).
* Frame count: v 0 size 0/1 → 9, size 2 → 11, size 3 → 13; dirt → 12; variant 8 → 13; others → 9.
* One-shot timing: frame *k* is shown once elapsed passes T[k−1] with T = 20 480, 100 000, 150 000,
  300 000, 600 000, then +300 000 each (900 000 … 3 600 000) µs; the effect is removed after the last
  frame: 9 frames ≈ 1.8 s, 11 ≈ 2.4 s, 12 ≈ 2.7 s, 13 ≈ 3.0 s. Below detail class 4 frame 0 is
  skipped.
* Blast push: the effect has a blast stage (after 100 000 µs, a push vector to objects within
  200 / 300 / 400 units for sizes 1 / 2 / 3), but every caller disables it and nothing in the program
  handles the push — **no effect on automata** [STATIC]. An implementation omits it.
* Debris (after 200 000 µs, only when zoom > 0 or variant > 3): variant 5 (UFO): one inner UFO
  part dropped with zero velocity plus class/2 + 2 outer UFO parts in a ring at horizontal speed 0.8,
  offset Rand[20, 30) in x and z; other variants: class/2 pieces offset Rand[10, 25) in x and z and
  768 above — variants 0–2: debris kind Rand(8), vx, vz ∈ [−0.4, 0.4), vy ∈ [0.4, 0.6); variant 8:
  kind Rand(4), vx ∈ [0.4, 1.2), vy ∈ [0.2, 0.4), vz ∈ [−0.16, 0.16). Frame period Rand[32 500,
  195 000) µs. Piece behaviour: §12.13. [STATIC]
* Drawn at the given point, no height offset.

### 12.4 Water splash [STATIC][DATA]

* Default set `EffectSprites` `0xFFFE0000 + z` (alpha `0xFFFE0100 + z`), 8 frames, spans
  (8,15,8,1) … (64,128,64,1). Callers may pass another set from `disaster_WHIRLPOOL.dat`:
  `0x48640000`/`0x48640005` (10 frames; sucked boats/aircraft, falling-UFO space junk) or
  `0x2D490000`/`0x2D490005` (9 frames; sucked wrecks); bridge-collapse splashes pick default /
  `4864` / `2D49` with Rand(3). (`2D4A` is not used.)
* One-shot, 200 000 µs per frame (8 frames ≈ 1.6 s, 9 ≈ 1.8 s, 10 ≈ 2.0 s), no fade; drawn exactly at
  the given surface point.

### 12.5 Tornado funnel [STATIC][DATA]

* Created at touch-down at (x, z, y = max(ground, 4)) with the tornado's direction and speed.
  **All three variants look the same**: the effect discards the variant for its sprite choice.
* **Motion**: velocity = normalise(dir.x, dir.z) × speed; x += dt_ms × vx, z += dt_ms × vz. There is
  **no wobble, jitter or heading change** — the funnel moves in a straight line. Height: every
  4th update y += (terrain × 256 − y) / 8 (integer), minimum 4. When it leaves the map it reports
  "left map" and is removed.
* **Reports**: every 4th update it reports its position (x, z and tile) to the tornado; the
  tornado applies its damage roll (§6.1) **each time the reported tile changes**, not once per
  minute tick. On each new land tile it also rolls Rand(100) < 40 / 70 / 100 (variant 0 / 1 / 2) to
  throw one flying debris piece — the only visual difference between variants. The piece (only when
  zoom > 0; kind Rand(8), frame period Rand[200 000, 300 000) µs) orbits the moving funnel: radius
  from 30 growing 3 × 10⁻⁵ units/µs, random start angle, angular rate 1.5 × 10⁻⁵ rad/µs at constant
  tangential speed 0.45 units/ms, rising ⌊8.94 × 1.3 × 10⁻⁴ × dt_µs⌋ per update. At radius > 160
  (≈ 4.3 s) it is released at 0.45 units/ms in direction angle + π (as coded: back towards the
  centre) with vy = 0.143, then behaves as in §12.13. [STATIC]
* Sprites (`EffectSprites`, shifted zoom, ID = base + k): start/end `0xFFFE5000` (alpha `5100`),
  loop A `0xFFFE5200` (`5300`), loop B `0xFFFE5400` (`5500`), dirt spray `0xFFFE5600` (`5700`),
  water spray `0xFFFE5800` (`5900`). Funnel sets 8 frames, spans (8,46,7,1) … (58,361,59,1); spray
  sets 11 frames, spans (11,13,11,3) … (85,102,86,19). Frame period 200 000 µs for all.
* Sequence: opening = start/end set forwards (8 frames); then loop: at the end of every 8-frame
  cycle the next cycle is loop B if Rand(10) > 6 (30 %), else loop A; on stop the start/end set
  plays backwards (frame 7 → 0) and the effect is removed.
* Spray: a second sprite at (x, y − 1, z), switched every 4th update by the ground under the
  centre: water → water spray, otherwise dirt spray; shown after the opening.

### 12.6 Whirlpool vortex [STATIC][DATA]

* Created at the end of the premonitions with the chosen point, its size, speed 0.12
  (`0x24860914` × 0.001), then `0x24860915` (250 000), `0x24860916` (1000), `0x24860917` (2400):
  * **250 000** = animation frame period in µs (0.25 s).
  * **1000** = angular speed of captured objects at the centre, × 10⁻⁸ → 10⁻⁵ rad/µs (outer value
    4 × 10⁻⁷ rad/µs, built in).
  * **2400** = capture half-size in world units: objects in the square ±2400 (≈ 9.4 tiles) around the
    centre are candidates.
  * **0.12** = travel speed in world units per ms (≈ 0.47 tiles/s); initial velocity
    0.12 × (0.5, 0, 0.125).
  * `0x24860913` (30 000) is not a vortex parameter: it is the lifetime handed to the floating
    wreck objects the whirlpool creates.
* **Motion**: x += dt_ms × vx, z += dt_ms × vz. A new position is accepted only if the 9 × 9-tile
  block starting at ((x − 1152)/256, (z − 1152)/256) is all water; otherwise the position is kept and
  the heading is turned in steps of 2π/14 until the next step is valid; new velocity 0.12 × (cos θ,
  0, sin θ). Height = ground + 0x600 at creation, then (terrain + 6) × 256 every 16 updates. Leaving the
  map finishes it.
* **Sprites** (`disaster_WHIRLPOOL.dat`, shifted zoom, ID = base + size × 5 + k): opening/closing
  `0x48590000` (alpha `0x48591005`), loop `0x4859200A` (alpha `0x4859300F`); 11 frames per strip;
  spans (12,7,12,7) … (192,109,192,109), centred registration. Sequence: opening forwards →
  loop (repeated) → on stop the opening set backwards → finished.
* **Capture** (every 256th update after the first 16): up to 128 objects in the ±2400 square that
  stand on a water tile and are water vessels (marina boats, seaport ships), **aircraft** (planes,
  helicopters — no altitude check, so aircraft over water are caught) or floating wrecks. A
  captured object's own driving stops; a captured wreck's lifetime is reset to 3 600 000 (counted in
  ≈ ms, i.e. ≈ 3686 s: it does not expire while orbiting). Captured motion by distance d to the centre: d ≥ 800 →
  straight in at max(0.12 × dt_ms, 1.0); 80 ≤ d < 800 → in by max(0.06 × dt_ms, 2.5) while orbiting
  at ω = (10⁻⁵ − 4 × 10⁻⁷) × f + 4 × 10⁻⁷ rad/µs, f = clamp(1 − (d − 80)/720, 0.05, 1); d < 80 → orbit at
  10⁻⁵ rad/µs, then the object is removed from the world ("sucked"), a splash is spawned there
  (boat/aircraft: `4864` set; wreck: `2D49` set) and "object sucked" is reported (the whirlpool only
  plays sound 0xFB at the vortex centre; no wreck, statistic or money change). A captured object
  that leaves the map is removed silently.
* **Floating wrecks** (created by the whirlpool, never by sucking) [STATIC][DATA]: one "debris
  field" wreck at the tile centre of each seaport/marina tile the whirlpool destroys (with an
  explosion and sound 0xF0); on a bridge collapse (sound 0xEF): one wreck at the first vehicle
  position on the bridge, one on a random bridge tile, a third on a random bridge tile if more than 5
  vehicles were on it, and one more (with a splash) at each further vehicle position up to 10 —
  kinds Rand(5): car, bus, truck, truck + dude, floater dude. Sprites `disaster_WHIRLPOOL` set +
  zoom (direct, no alpha), frames as horizontal cells: car `485B`, bus `485C`, truck `485D` (1
  frame), truck + dude `485E` and floater dude `4862` (4 frames), debris field `4860` (1 frame);
  `4863` swimming dude, `485F` capsized sailboat, `4861` sinking boat are never used. Frame period
  250 000 µs. Lifetime `0x24860913` = 30 000 counted in ≈ ms (dt_µs/1024) ≈ 30.7 s, then fade-out at
  0.00033 per ms (≈ 3 s) and removal. Wrecks do not drift; only a vortex moves them.
* **Reports to the whirlpool** (update counter c): position (tile) every 4th update once c > 15
  and the vortex moved, and whenever c mod 256 = 128; a bridge/marina scan (first water-standing
  structure within ±1200 world units) when c mod 256 = 128; "object sucked" per captured object;
  "finished" at the end or when leaving the map. §6.5 lists the whirlpool's reactions.
* Duration: until the whirlpool stops it (its attack phase), then the 11-frame closing (≈ 2.75 s).

### 12.7 Toxic cloud puffs and rain [STATIC][DATA]

* Sprites: `EffectSprites` "toxic cloud" `0xFFFC1400 + k` (alpha `0xFFFC1500 + k`, shifted zoom),
  a **single static image** (the strip has 5 cells but only cell 0 is shown); ground shadow
  `0xFFFC1600 + z` (direct zoom), only above detail class 3.
* **Puffs do not drift.** The cloud's velocity is handed to each puff but never applied; puffs
  stay where they were placed (§6.2 step 2), and the disaster layer never moves them. The cloud
  therefore does not travel `[STATIC: no reader of the stored velocity]`.
* Opacity fades in from 0 to 1 at 0.00015 per ms (≈ 6.7 s); on dissipation it fades out at the
  same rate, then the puff reports "gone" (the cloud counts its puffs down, §6.2 step 5) and is
  removed.
* **Rain column** (one per puff while the cloud rains) [STATIC][DATA]:
  * Hangs from its top point T = puff y + 0x960 down to B = (terrain + 4) × 256 (no column if B is
    not below T); world length L = T − B. Registration at the top centre.
  * Images (`disaster_TOXICCLOUD`, shifted zoom k): loop `0x48590005 + k` (one frame, RGBA with its
    own alpha), top mask `0x4859000A + k` (alpha only, 5-bit values ramping ≈ 4 → 31 downwards),
    bottom mask `0x4859000F + k` (31 → 1 downwards). On-screen sizes at zoom 0…4: width 38 / 75 /
    150 / 300 / 600 px; loop tile pitch 34 / 67 / 133 / 266 / 532 px; top ramp 5 / 9 / 17 / 34 / 68 px;
    bottom ramp 12 / 24 / 48 / 95 / 190 px.
  * Assembly, with screen rows counted down from the top point, E = current grown length and S =
    current retracted top (both in screen px of their world length): top segment [S, S + top ramp)
    = loop colours under the top mask (mask row = y − S, so the ramp rides the top); bottom segment
    [E − bottom ramp, E) = loop colours under the bottom mask (its last row at the lower end); middle =
    the loop image tiled vertically, row = (scroll + y) mod tile height, partial tiles clipped. Drawn
    bottom, top, middle. If the column is shorter than both ramps there is no middle and the split
    point is S + (E − S) × top / (top + bottom). Mask and loop alpha combine as 5-bit opacity ×
    the loop's alpha `[INFERRED]`. World-to-screen length L × 2^z / 256 px `[INFERRED]`.
  * **Rate 0.000512 world units per µs (512/s)** drives three things: E grows from 0 to L (the lower
    end runs down to the ground while the top stays), the texture scrolls down at the same rate, and
    after the rain stops S grows at the same rate until S ≥ E, when the column is removed (drains
    top-down in E/512 s). No opacity fade.
  * **Ground steam**: once the column is full a steam effect is created on the ground under it
    (`4858` sets: opening `0x48580000` (alpha `0x48581005`), cycle `0x4858200A` (`0x4858300F`),
    close `0x48584014` (`0x48585019`); 4 frames each, ≥ 200 000 µs per frame; opening → cycle loop →
    close → removed). Stopping the rain sends it to its close animation. Original quirk: a full
    column creates a second steam object on the update after the stop, which plays until the column
    is gone.

### 12.8 UFO swarm [STATIC][DATA]

The saucers are a swarm object owned by the UFO disaster (§6.3); the swarm hands each command to
the saucer with the fewest queued commands (ties → lowest index). Each saucer executes its own
queue in order; a beam or crop-circle command first inserts a "fly there" leg.

* **Sprites** (`EffectSprites`, direct zoom): saucer `0xFFFE1040 + z`, 5 frames (spans (6,6,6,1)
  … (90,97,90,1)), 100 000 µs per frame, looping; each saucer spins forwards or backwards (50/50).
  Shadow `0xFFFE1050 + z` (1 frame, on the ground under the saucer, detail class > 3). Beam
  `0xFFFE1070 + z`, 5 frames, advanced every 300 ms, hanging from 5 units below the saucer
  (registration at the top; spans (5,1,5,16) … (75,1,75,255)). Bomb = plasma ball
  `0xFFFE1300 + z` (alpha `0xFFFE1380 + z`), 8 frames, 100 000 µs, looping.
* **Spawn**: random x, z on the map with a 1-tile margin; y = terrain + 5120 … 25 600 (uniform).
* **Movement**: straight 3-D line to the leg target at **2.6 world units/ms** (≈ 10 tiles/s), no
  steering. Legs are at most 1200 units horizontally (longer trips are split), and never lower than
  terrain + 7680. Idle (empty queue): hop to a random point within ±1280 (±5 tiles) at terrain +
  5120 … 10 240; after the 6th idle hop the saucer leaves by fading.
* **Beam target** (destroy): hover at the lot centre (anchor + footprint/2) at building height +
  (terrain + 16) × 256 for **5001 ms** with the beam shown, then report "beam" for the tile under the
  saucer and climb 5120. **Crop circle**: same 5001 ms hover (no beam), then report "crop circle";
  the UFO places the "Crop Circle" occupant (§6.3), not a decal. Arrival sounds: beam 0x4D, crop 0x4E.
* **Abduction command** (5 per attack for kinds 3–5): executed in place when reached; picks a random
  automaton anywhere on the map (2-tile margin) that is abductable (people/vehicles), pulls it up at
  2 units per update and removes it; reports "abduction" at once. Sound 0x4B.
* **Bombs** (attack kind 5 only): every Rand(10…20) s, unless beaming/hovering, a plasma ball is
  launched towards a ground point 3–10 tiles off on each axis (random signs), in a straight line at
  0.6 units/ms with the vertical component × 8.94 (≈ 3–6 s flight), sound 0x50. On a building hit it
  explodes (plasma explosion, sound 200) and reports "bomb hit" for that tile; ground and water hits
  explode or splash but are not reported to the UFO.
* **Exit** (attack end): each saucer is given an exit command after its queue — explode
  (Rand(10000) < `PercentageChanceOfUFOExplosionOnExitTimes100`: plasma explosion variant 5, sound
  0x4F, removed at once) or fade (opacity 1 → 0 over 3000 ms, then removed). Saucers never fly off
  the map.

### 12.9 Locust swarms [STATIC][DATA]

* **Spawn**: one map edge is chosen for the whole attack (Rand(4)); each swarm starts at a random
  point on that edge at terrain + 5120 … 10 240.
* **Targets**: crop tiles are dealt round-robin: each swarm in turn takes the nearest remaining
  crop (2-D distance from its previous target), until none is left.
* **Sprites** (`disaster_LOCUST`, direct zoom): ID = `0x3FAC0000` + state × 0x10000 + z, alpha +5;
  state 0 fly (`3FAC`), 1 eat (`3FAD`), 2 die (`3FAE`, never selected by the program). 5 frames
  (the art comment says 6), 100 000 µs per frame, looping, forwards or backwards (random per
  swarm). Registration points (fly) (26,16) (52,32) (104,63) (208,127) (416,254). Shadow
  `0x3FCB0000 + z` on the ground under the swarm (detail class > 3).
* **Giant locust fly-by**: once per attack, before the swarms move, a screen-overlay of giant
  locusts (`3FB1`, variants 0–3; variant 4 is never chosen) crosses the screen: 3 (detail < 4), 8
  (detail 4) or 10 + Rand(10) locusts, the first variant 0, the rest 1 + Rand(3), at 30/60/20/−20/−60°
  and speed Rand(0.2, 0.4); sound 0xFE.
* **Movement**: speed 0.4 units/ms (≈ 1.56 tiles/s; `SwarmSpeed` is never sent), straight at the
  target; legs ≤ 1200 units, never lower than terrain + 7680 while travelling. At a crop the swarm
  descends to (terrain + 6) × 256 (+ building height). Idle wander ±1280 units.
* **Eating**: on arrival it switches to the eat animation and reports "arrived" (tile); on the next
  update it reports "eat" once for that tile and moves on (the eat animation is kept if the next crop
  is within 1536 units). The locust disaster destroys the crop on "eat" (§6.4).
* **Passing over trees**: whenever a swarm's tile changes, every tile that drops out of its 5 × 5
  neighbourhood and holds flora is given damage type `0x20` (burnt-tree replacement, §9). So swarms
  strip trees along their path `[STATIC; contradicts the earlier "locusts do not destroy flora"]`.
* **End**: a swarm whose queue is empty is removed on the next update (no fade). A swarm that
  arrives at a crop within 6 tiles (Chebyshev) of a crop-duster spray point (§6.4) fades out over
  9 s and is removed (sound/effect 0xF2). When the last swarm is gone the swarm object reports "all
  gone".
* **Crop duster**: each spray point shows the death-spray effect `0x3FB90000 + z` (alpha +5) at the
  plane's position, 8 frames × 125 000 µs, one-shot (1 s), static.
* **Crop-duster plane** [STATIC][DATA]: a street-sim vehicle, VATTRIB `0xC25C2BF7:0x025C2C45:0x6E`
  (instance 110; only in `VAttrib_Holidays.IXF`: length 100, motion sound 133, ANSPRATT/sprite key
  `0x3FB0` "Locust Swarm Crop Duster Plane", one frame). Home: a random aircraft spawner whose
  Manhattan distance to the target is > 12 tiles (else the first such in list order; else a random
  map tile ≥ 12 away); it starts there on the ground. Speed 0.001 units/µs (≈ 3.9 tiles/s; the record's
  MaxSpeed is overridden). Height: ground + 2560, at least 512 above building tops, sampled 256 units
  ahead. It flies straight at the target tile (heading = atan2 of the difference); on arrival it
  reports a spray point at its position, then steps 512 units (2 tiles) on along its heading and
  sprays again, at most 5 times or until the next point is off the map. A retarget only appends a
  waypoint, flown after the current run (with its own 5 sprays). With no waypoints left it flies home
  in 100-unit steps and is removed within 100 units of home. Whether the plane exists without the
  holiday data is not traced.
* **Dispatch icon**: occupant `0x207EDC0E:0x0000057E:0x00003FAF` ("Locust Swarm Dispatch Icon",
  1 × 1 footprint), placed at (x + 1, z) with sound 246; no timer — removed only when a 4th dispatch
  evicts it or when the locust attack ends (which also removes the planes).

### 12.10 Riot crowd [STATIC][DATA]

* One crowd per riot: a **vehicle-type automaton** (`26-automata.md` VATTRIB key instance `0x54`:
  speed 110 000 raw ≈ 0.95 tiles/s, length field 80, ANSPRATT 11406 = one sequence of frames 0–3,
  sprite CSATTRIB 11406 with 32 rotations, i.e. one sprite shows the whole mob; behaviour script 48 =
  a single opcode 200 "riot"). Not a pedestrian, not counted in the pedestrian cap.
* It starts on the riot-site lot's road piece and drives along road lanes like a vehicle
  (`26-automata.md` §4, §7) `[INFERRED: generic next-piece choice]`.
* Every RandRange(`0x2484A289`, `0x2484A290`) µs (shipped 0.3–0.6 s) it throws an object: a short
  projectile from a random point up to 80 units ahead along its heading, velocity (1.2 cos φ, 0.2,
  1.2 sin φ) with random φ. A projectile that hits a building ignites it (riot ignition, §8.4).
* Reports to the riot: "entered tile" (the riot site moves there; the road piece there gets its two
  per-instance **lock bits** cleared while any crowd stands on it, ref-counted per tile; fire does
  the same on a burning occupant. One bit allows replacement by development (building placement
  fails on a locked tile), the other allows clearing by tools (network placement, terraforming and
  zoning will not remove a locked tile) [STATIC; naming INFERRED]), "left tile"
  (bits set again when no crowd remains), "destroyed" (the
  riot relocates, §8.4), and "hit building" (ignite). Event-driven, no fixed cadence.
* Lifetime: until the riot ends (its duration rule) or the crowd's script ends; then all affected
  lots are restored.

### 12.11 Space junk [STATIC][DATA]

* **Launcher**: while running (attack phase), one piece is spawned whenever the elapsed time passes
  an interval drawn as RandRange(768n, 1536n) µs with n = ⌊`0x44860913` / 1024⌋ (shipped 3 000 000 →
  2.25–4.5 s, mean ≈ 3.4 s). The 2000/5000/10000 class value of §6.6 is **never used**. The number of
  pieces is therefore (attack window ÷ interval), at most one per update.
* **Placement**: a uniform random point (x, z) on the whole map minus a 512-unit (2-tile) border; no
  centre or radius. The piece starts at (x − 5000, ground + 80 000, z).
* **Fall**: straight line, no gravity: x += 0.65 × dt_ms, y −= 8.94 × dt_ms, z unchanged ⇒ ≈ 9 s fall,
  landing ≈ 817 units (3.2 tiles) east of the rolled x.
* **Sprites**: piece kind = Rand(4) → `3FB2` "falling UFO", `3FB3` rock, `3FB4` comet, `3FB5`
  arcology (`3FB6` "Apollo" is never chosen); ID = base + z × 4 + rotation, alpha base + 0x14; 6
  frames, 300 000 µs, looping. Ground shadow `0xFFFE1050 + z` under the piece. No smoke trail.
* **Impact**: building → report "hit building"; land → explosion variant 8 (`3FB7`, 13 frames, 3 s)
  and report "hit ground"; water → splash (falling-UFO piece: `4864` 10-frame set; others: default
  splash) and report "hit water". Each report carries the impact tile and is sent on the impact
  update; §6.6 gives the damage.
* **Crater decal**: class k = ⌊c × 100/255⌋ (0–3) for crater strength c; sets `3FC8` (2 × 2 tiles),
  `3FC9` (3 × 3), `3FCA` (4 × 4), `3FC5` (5 × 5), ID = base + z, alpha +5, one frame, centred on the
  impact point at ground height. Spans z0 → z4: `3FC8` (8,5,8,5) … (128,76,128,75), `3FC9` (12,8,12,7) …
  (192,114,192,113), `3FCA` (16,10,16,9) … (256,151,256,151), `3FC5` (20,12,20,12) … (320,189,320,188).
  Removed at once if any tile change touches its square. Lifetime: `0x44860916` counted down by
  dt_µs/1024 (≈ ms): 300 000 ≈ 307 s; then it fades at 0.00033 per ms, but only the current update's
  dt is applied on each refresh (every 0.25–3 s depending on zoom), so the real fade is much longer
  than 3 s `[STATIC; original defect — an implementation may fade over ≈ 3 s]`.

### 12.13 Debris pieces [STATIC][DATA]

* Sprites: `EffectSprites` `0xFFFD0000 + kind × 5 + z` (kinds 0–7 "explosion debris 0…7"; UFO outer
  parts `0xFFFD0080 + z`, inner parts `0xFFFD0085 + z`), 8-frame strips, direct zoom, no alpha set.
  Ground shadow `0xFFFD0100 + kind × 5 + z` (UFO parts `0xFFFD0180`/`0185 + z`) only when the machine
  class ≥ 4 and fewer than (class − 3) × 20 pieces are alive.
* Motion: x += vx × dt_ms, z += vz × dt_ms, vy −= 0.0003 × dt_ms, y += vy × 8.94 × dt_ms (gravity).
  Hitting a building: horizontal speed 0, vy × 0.2, keeps falling. Ground: bounce (vx, vz × 0.25, vy ×
  −0.4), frame period × 1.5 per bounce; when |vx| + |vy| + |vz| < 0.02 it stops, hides its shadow,
  freezes and is removed ≈ 20 s later. Water: splash (default set) and removed. Off map: removed.
  Nothing explodes on landing; no report to the disaster layer.

### 12.12 Summary of report-backs

| Effect / automaton | Report | Reaction (section) |
|---|---|---|
| tornado funnel | position (every 4th update); left map | damage on each new tile; end (§6.1) |
| whirlpool vortex | position; bridge/marina at tile; object sucked; finished | coast damage, removal and counting, end (§6.5) |
| toxic puff | gone | puff count −1 (§6.2) |
| UFO saucer / bomb | bomb hit building; beam; crop circle; abduction | ignite / destroy, place crop circle, count (§6.3) |
| locust swarm | arrived; eat; all gone | duster check, destroy crop (§6.4) |
| crop duster | spray point | sprayed area (§6.4) |
| riot crowd | entered / left tile; hit building; destroyed | site, ignite, relocate (§8.4) |
| space-junk piece | hit building / ground / water | impact rules (§6.6) |

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
| 0x08 | 4 | u32 | disasterCost | Accumulated destruction cost of the current disaster episode (§10.2) (0 in all shipped files). |
| 0x0C | 4 | u32 | falseAlarms | Consecutive false-alarm counter of §10.1. |
| 0x10 | 4 | u32 | emergencyActive | Boolean: a state of emergency is active (§10.1). |
| 0x14 | 4 | u32 | emergencyElapsed | Days since the emergency was declared (§10.1). One shipped file holds a non-zero, apparently uninitialised value here; treat any value as valid. |
| 0x18 | 4 | u32 | declaredDuringDisaster | Boolean "EW" flag: the emergency was declared while a disaster was active; selects the WithEW relief percentage (§10.2). |

All five are restored on load; disasterCost, falseAlarms and the EW flag directly. emergencyActive
and emergencyElapsed are restored **deferred**: once the game reports that the city view is ready
`[INFERRED: meaning of the trigger notification]`, an emergency saved as active is re-declared
through §10.1 with the saved elapsed count (UI sound, notification; if no disaster is active then,
it counts as a false alarm and re-activates only while the counter is < 3). The original never
clears its "pending" copy, so a repeat of that notification would re-declare an emergency that has
since ended — an original defect an implementation should not copy. [STATIC]

(Correction: earlier versions listed this record as reserved0 / emergencyActive / flagA /
disasterCost / flagB and reported a writer/loader mismatch; both came from misreading object
offsets. The order above is the writer's and the loader's.)

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

### Built-in defaults [STATIC]

No loader passes a default to the getter: a key that is absent (from `SimTune.INI [Tunables]` or
the `SC3DisasterLayer.ini` section) leaves the variable at its built-in initial value, and the
per-key fix-ups of the named-key sections run only when the key is present. Built-in values that
differ from the shipped ones (all others equal the shipped values):

| Disaster | Key → built-in (shipped) |
|---|---|
| Fire | `0x047DF002` 5 (48); `0x647DF00F` 30 (240); `0x647DF02B` MinFlam 50 (1); `0x647DF032` 150 (100); `0x047DF044` 0 (5); `0x047DF051` 0 (15); `0x047DF061` 10 (15); `[FireDisaster]` MaxFires 0 (20), sound 0 (62) |
| Tornado | `0xE48381BC` 12 (48); `0x048381C8` 36 (360); speeds see note; `0x4483820F`/`16`/`1D` 20/50/70 (50/70/95); `0x24838224` 10 (30); `0x0483822A` 400 (60); phases `0xC4838239`…`0xE4838253` 20/40/60/80 (60 each); `[TornadoDisaster]` 0, 0 (5, 60) |
| Riot | `0x6484A25D` 10 (8); `0x2484A264`/`0x6484A26A` 0/0 (48/300); `0x4484A270` 60 (50); `0xE484A27D` 180 (240); `0xA484A283` 0 (20); `0x2484A289`/`90` 100000/300000 (300000/600000); `[RiotDisaster]` max 1 (3), sound 0 (63) |
| Whirlpool | `0x24860902` 16 (24); phases `0x2486090F`–`11` 60/120/180 (20/40/60); `0x24860912` 300 (100); `0x24860914` 60 (120) |
| Space junk | `0x44860902` 16 (120); `0x44860903` 96 (720); `0x44860906` 83 (20); `0x44860912` 750 (300); `0x44860913` 750000 (3000000); `0x44860915` 80 (50) |
| Earthquake | `0xE4237518` 16 (96); `0x44237523` 96 (480); `0x6423753A` 83 (40); `0x64237541`/`0x44237548`/`0x4423754E` 1000/2500/4000 (5000/6000/7000); `0x44237555` 1000 (3000); `0x6423755E` 2500 (3000); `0xA4237564` 4000 (3000); `0xC42375C5` 3 (4); `0x64237626` 2 (1); `0x6423762B` 4 (2); `0x24295C2C`/`0x44237637`/`0x4423763D` −2/−4/−6 (0/0/0) |
| Toxic cloud | Min/MaxClouds 10/20 (1/4); speeds 1/2 (1/3, before × 256/60000); visible 80 (60); heights 35/45 (1/7, before × 256); durations 20/40 (75/150); interval 2 (48); WeightForPollution 200 (675); threshold 160 (120); rain duration 10/30 (75/175); rain gap 15 (125); abandon/destroy radius 6/3 (3/2); likelihoods 50/50 (1000/1000) |
| UFO | MinMonths 16 (300); MaxMonths 96 (600); chance 42 (33); AttackEnd 240 (185); LowTech cap 3 (6); MaxTotal 32 (12); YearVisitation 0 (1900); bomb destroy / fire % 10/10 (100/100) |
| Locust | SwarmSpeed 50 (1000, unused); SwarmToCropRatio 40 (100); YearVisitation 1920 (0); MaxNumCropsToAttack 10 (250). `TimeBeforeLocustAttackEnd` is never read. |
| Misc | MinDisasterCostToGetRelief 0 (500); relief % without / with EW 0/0 (25/35) — so missing relief keys mean no relief |

Fix-ups that always run after loading [STATIC]: fire burn slope = (BurnMax − BurnMin) / (255 −
MinFlam) (if 255 − MinFlam ≤ 0, BurnMax is forced to 0; §5.1); fire spread slopes (hi − lo)/255; riot interval slope and
duration slope (§8.1). Tornado: speed = ⌊value⌋ × 256 / 60000, with the value read as a **signed
8-bit** number (values > 127 wrap negative); the built-in speeds are fractional and truncate to
0, so missing speed keys give speed 0 (original defect); LifetimeMin 0 → 1, LifetimeMax ≤ Min →
Min + 1. Toxic (only when the key is present): MinClouds < 1 → 1; MinCloudDuration 0 → 10;
MaxCloudDuration ≤ Min → Min + 1. Values parse with an atoi-style reader after trimming spaces, so
`Key = 20;` reads 20. Relief percentages are stored as bytes (values > 255 wrap). [STATIC]

**Live instances in saves** [DATA]: none of the 222 parsed files (the 59 shipped city, scenario,
starter-town and terrain files plus 163 observed-run saves) holds an active fire, tornado, riot or
toxic-cloud instance; every such record is the 12-byte empty form (count 0). The 10 starter towns
lack the toxic record. The per-instance element layouts therefore remain unobserved.

## Open questions

* Issuer of the per-minute disaster tick (scheduler command 3, `14-sim-clock.md` §6.3 rule 5);
  game-time conversion in §4.1 assumes one minute tick per simulated minute.
* Field meanings of active fire, tornado, riot and toxic-cloud instances in their save records
  (element types are known, `10-save-format.md` §6.5; no parsed save has one active).
* Meaning of the detail words of history entries.
* Riot: the crowd's next-road-piece choice (generic vehicle rule assumed) and the meaning of the two
  occupant flags cleared under the crowd (§12.10).
* UFO: draw-layer semantics of the effect draw class; exact cap mapping for the high-tech and power
  target lists.
* Locusts: whether the "die" sprite set is ever shown; the crop-duster plane's speed and altitude.
* Whirlpool: exact inset of the random centre inside the chosen water rectangle.
* Toxic cloud: the exact per-tile layer and occupant test of the random-origin search (§6.2).
* Space junk: exact number of pieces per event (depends on how minute ticks map to real time).
* Purpose of the unused, never-instantiated tenth disaster implementation (§2).
* Occupant-group membership tests (agent-type hierarchy, `CitySim.ini [AgentTypeHierarchy]`) are
  assumed to follow `13-buildings-occupants.md`.
