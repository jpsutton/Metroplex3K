# 14 — Simulation clock, calendar, speeds and update scheduling

Status: draft. Provenance tags per `00-README.md`.

## 1. Calendar

### 1.1 Date representation

The city date is a **Julian Day Number** (JDN, integer days) on the proleptic **Gregorian**
calendar. Months have their real lengths (28–31 days) and leap years follow the Gregorian rule.
There is no 30-day or 25-day "game month" in the clock itself [STATIC].

Conversion from (month `m` 1–12, day `d`, year `y`) to JDN, using integer division throughout
[STATIC]:

1. If `m < 3`: `m' = m + 9`, `y' = y − 1`. Otherwise `m' = m − 3`, `y' = y`.
2. `c = floor(y' / 100)`, `ya = y' − 100·c`.
3. `JDN = floor(146097·c / 4) + floor(1461·ya / 4) + floor((153·m' + 2) / 5) + d + 1721119`.

Conversion from JDN back to (m, d, y) is the exact inverse (standard algorithm):

1. `t = 4·(JDN − 1721119) − 1`; `c = floor(t / 146097)`; `t = floor((t − 146097·c) / 4)·4`.
2. `ya = floor((t + 3) / 1461)`; `t = floor((t + 3 − 1461·ya + 4) / 4)·5`.
3. `m' = floor((t − 3) / 153)`; `d = floor((t − 153·m' + 2) / 5)`; `y = 100·c + ya`.
4. If `m' < 10`: `m = m' + 3`; else `m = m' − 9` and `y = y + 1`.

Validation points: 1 Jan 1900 → 2415021; 1 Jan 2000 → 2451545.

Derived values [STATIC]:

* **Day of year** = `JDN − JDN(31 Dec of year−1)`, so 1 January is day 1.
* **Weekday** = `((JDN + 1) mod 7 + 6) mod 7 + 1`, giving 1 = Monday … 7 = Sunday.
* The city keeps its **founding year** (default 1900 when a city is first created) [STATIC].
* A "set date from (day-of-year, year)" operation exists. For year ≠ 0 it sets
  `JDN = JDN(31 Dec of year−1) + dayOfYear`. Year 0 is a special case: `JDN = dayOfYear + 2415386`
  [STATIC]. 2415386 is 1 January 1901, so day-of-year 1 with year 0 gives **2 January 1901**, not
  1 January. This off-by-one is the original's behaviour, not a typo; reproduce it.

The start-year choices offered by the new-city dialog are in `09-ui-screens.md`. [OPEN] The exact
list of options has not been confirmed here.

### 1.2 Date stamps used by other subsystems

Some subsystems use a coarse "date stamp" of `(month + 12·year)·30 + day` for expiry arithmetic
(for example, the news ticker in `22-advisors-news-petitioners.md`). This is a bookkeeping
convention only. The clock itself is Gregorian [STATIC]. Because the stamp assumes 30-day months,
day 31 of one month and day 1 of the next month get the same stamp.

## 2. The real-time clock

### 2.1 Time compression

The clock converts real elapsed milliseconds into simulated time using a **compression factor `F`**
(simulated seconds per real second). Its default at city creation is `F = 1440` [STATIC].

* `msPerDay = floor(86 400 000 / F)` (60 000 ms at F = 1440)
* `msPerHour = floor(msPerDay / 24)`, but at least 50
* `msPerMinute = floor(msPerHour / 60)`, but at least 50

The 50 ms floor applies only to the hour and minute periods, not to the day [STATIC]. All three
values are recomputed whenever `F` changes. `msPerHour` is computed but never used to advance time:
hours are produced by counting 60 minute ticks (§2.2), so the hour floor has no observable effect.

The value of `F` for each speed is given in §4.

### 2.2 Advancing time

The clock is driven by a periodic timer callback [STATIC]. On each callback:

1. If the clock is paused (pause count > 0), do nothing.
2. Otherwise add `now − last` to an accumulator `acc`, and set `last = now` (milliseconds from the
   system millisecond timer).
3. If any **minute** or **hour** listener is registered: while `acc ≥ msPerMinute`, run one
   *minute tick* and subtract `msPerMinute`.
4. Otherwise: while `acc ≥ msPerDay`, run one *day tick* and subtract `msPerDay`.

A *minute tick* increments the minute counter and notifies minute listeners. When the counter
reaches 60, it resets and runs an *hour tick*. An *hour tick* increments the hour counter and
notifies hour listeners. When that counter reaches 24, it resets and runs a *day tick* [STATIC].

Switching between the two modes keeps `acc` and the minute and hour counters as they are
[INFERRED from the algorithm above, high confidence]. A day tick produced in day mode does not reset
the minute or hour counters. So when minute mode starts again, the counters continue from their old
values, and the first simulated day in minute mode can be shorter than 1440 minutes.

A *day tick* runs these steps in this order [STATIC]:

1. Increment the date (JDN + 1).
2. Notify all **day** listeners.
3. If the new weekday is 1 (Monday), notify all **week** listeners.
4. If the new day of month is 1, notify all **month** listeners.
5. If the new day of year is 1, notify all **year** listeners.
6. Fire, then remove, every one-shot **date alarm** whose date equals the new date.

Listeners within one list are notified in registration order. The six periods are minute, hour,
day, week, month and year. A date alarm is accepted only if its date is strictly later than the
current date [STATIC].

[OPEN] The interval of the timer callback is not known. Recommended: call it at least every 50 ms
(for example every 10–20 ms). Then the 50 ms minute floor of §2.1 is honoured, and a callback rarely
has to process several day ticks at once [INFERRED].

### 2.3 Pause

Pausing is **counted** (nestable) [STATIC]:

* *Pause*: increment the count. On the transition 0 → 1, add `now − last` to the accumulator, so
  that time already elapsed is kept.
* *Resume*: if the count is > 0, decrement it. On the transition 1 → 0, set `last = now`, so that
  the paused interval is discarded.

The city keeps **two** nestable runtime counts on top of the clock's pause count [STATIC]:

1. **City pause count.** Raised by Pause and cleared by Resume. On the transition 0 → 1 the city
   announces "simulation paused" to the user interface and pauses the clock. This is the ordinary
   player/UI pause.
2. **Pending / modal count.** A separate nestable count raised when a subsystem or UI begins a
   blocking operation (modal dialogs, save/load and similar) and cleared when that operation ends.
   Raising it also pauses the clock for the duration.

Both are pure runtime state: they are not written to the city save and have no INI field [STATIC].
Which dialogs and operations raise which count is listed in `09-ui-screens.md` where known.
[OPEN] Not every dialog has been verified.

## 3. Periodic cascades

### 3.1 Cascade per clock period

When the simulation starts, it announces "simulator started" once (the scenario system uses this
announcement, `23-scenarios.md` §4.3). From then on, each clock period triggers a **cascade** of
simulation work [STATIC]:

| Clock period | Cascade | City handler afterwards |
|---|---|---|
| day | the daily work of §6.1 | day handler (§3.2) |
| week (Monday) | the weekly work of §6.1 | week handler |
| month (day 1) | the monthly work of §6.1, run with the clock paused (§6.3 rule 4) | month handler |
| year (1 January) | none: no subsystem has yearly work (yearly effects happen inside the monthly work of 1 January, §6.3 rule 7) | year handler, anniversaries |
| minute | the per-minute disaster work, **only while a disaster instance is active** (§6.3 rule 5) | – |
| hour | never used (no hourly work exists) | – |

**Same-tick order.** On a date that also starts a week, month or year, the cascades run in the order
of §2.2 within the same day tick: daily, then weekly, then monthly, then yearly. On a 1 January that
falls on a Monday all four run, in that order. Each subsystem's work runs **once** per occurrence of
its own period. It is not repeated by the cascades of longer periods [STATIC].

When the simulation stops, all periodic work stops [STATIC].

Per-subsystem documents use "daily", "weekly", "monthly" and "yearly" in the sense of this table.

### 3.2 City day/week/month/year handlers

| Event | Action |
|---|---|
| day | Increment the city's elapsed-day counter; inform the user interface (date display) |
| week | – |
| month | Inform the user interface, the sprite (automata) system and one growth component that a month has passed |
| year | Anniversary check (below) |

All four are [STATIC]. The handlers run **after** that period's cascade (§3.1).

**Anniversaries.** After the yearly handler, the city computes `age = currentYear − foundingYear`
[INFERRED: the second operand is the founding year]. If `age` is in the table below, the city posts
the given triggered news-ticker item. The item IDs are instances in the triggered-news data
`0x42C1ED2D` (see `22-advisors-news-petitioners.md`) [STATIC]:

| City age (years) | Triggered news item |
|---|---|
| 10 | 0x1F4 |
| 25 | 0x1F5 |
| 50 | 0x1F6 |
| 100 | 0x1F7 |
| 250 | 0x1F8 |
| 500 | 0x1F9 |
| 1000 | 0x1FA |
| 2000 | 0x1FB |

## 4. Speeds

The UI offers Pause, Turtle, Llama, Cheetah and African Swallow. Their names are the GUI string
table `0x225872FE` entries 0x64 and 0x66–0x69 [DATA]. The city stores a **current speed** (0–4) and a
**selected speed** (1–4). Each speed maps to a fixed clock compression factor `F` [STATIC]:

| Speed | Name | F (sim s per real s) | Real ms per sim day (`floor(86 400 000 / F)`) | `msPerMinute` after floor | ≈ real time per 30-day month |
|---|---|---|---|---|---|
| 0 | Pause (current speed while paused) | 120 | 720 000 | 500 (no floor) | 6 h; the clock is also paused, so time does not advance |
| 1 | Turtle | 10 080 | 8 571 | 50 (floored from 5) | 4.3 min |
| 2 | Llama | 43 200 | 2 000 | 50 (floored from 1) | 60 s |
| 3 | Cheetah | 129 600 | 666 | 50 (floored from 0) | 20 s |
| 4 | African Swallow | 525 600 | 164 | 50 (floored from 0) | 4.9 s |

The minute floor of §2.1 applies at speeds 1–4 but not at speed 0. The hour period would be
floored only at speeds 3 and 4, but it is unused (§2.1).

The **set speed** operation, with requested speed `s`, runs these steps [STATIC]:

1. If `s` equals both the current and the selected speed, do nothing.
2. If the simulation is in the paused state: set current = 0, and if `s ≠ 0`, set
   selected = min(`s`, 4).
3. Otherwise `s` must be 1–4; if it is not, the request is rejected. Set current = selected = `s`.
4. Apply `F` for the current speed (table above).
5. Announce "speed changed" with the selected speed.

[OPEN] Step 2 with a negative `s` is untested. Recommended: reject negative values, as step 3 does.

Speed changes are requested by the speed buttons and hotkeys (`09-ui-screens.md`). A newly created
city was expected to start at speed 2 (Llama) [STATIC], but the original **starts a new city at
speed 1 (Turtle)** `[OBSERVED]`: the city-information record of a fresh save stores speed 1, and
the running clock measured about 10 s per game day with one speed cell lit. Use speed 1.
Measured at speed 4: one game month every ≈ 5 s, ≈ 166 ms per game day, which matches the
African Swallow row above `[OBSERVED]`. A SimCity 2000 import maps that game's
speed 0–3 to 1–4, with 1 as the default when the import has no speed (§9) [STATIC].

**Minute-resolution side effect.** While per-minute work is active (§3.1), time advances in minute
steps of at least 50 ms. A simulated day then takes at least 1440 × 50 ms = 72 s, whatever the
speed [STATIC]. Per-minute work exists only while a disaster instance is active (§6.3 rule 5), so
the simulation slows down during disasters [STATIC]. A faithful clone should keep this behaviour.

## 5. City subsystems and their order

A city consists of the subsystems below. The city creates, initialises, loads and saves them in this
fixed order, and releases them in reverse order [STATIC]. The order also decides the order of work
within one cascade (§6). The save records each subsystem owns are listed so that the order can be
checked against `10-save-format.md` §5.2, whose payload order is the same [STATIC][DATA].

| # | Subsystem | Save records owned (`10-save-format.md` §5.2) |
|---|---|---|
| 1 | Terrain | `206C6E7C:21737DE5:0` |
| 2 | Occupant manager (buildings and props) | `E0FAADC7:A0AB89F0:1`, `20631788:A0AB89F0:0` |
| 3 | Network manager | `206C6E7C:2147C2DD:0`–`3` |
| 4 | Flora layer | `406B1196:80AB8AB0:0` |
| 5 | Zone manager (zones and zone developers) | `206C6E7C:409FF3BA:*` |
| 6 | Disaster manager and disaster instances | `E1F6ABE2:*:0`, `206C6E7C:21F6ABCA:0` |
| 7 | Neighbours (neighbour cities and deals) | `C2910E7D:82937B60:0` |
| 8 | RCI development (valve-effect) layer | `206C6E7C:80F1E6D3:0` |
| 9 | Ordinances | `41193C3A:E1193C2A:0` |
| 10 | Aura (mayor rating) | `206C6E7C:C259C02D:0` |
| 11 | Pollution | `206C6E7C:C0A81498:0` |
| 12 | Crime | `206C6E7C:20A7AE7F:0` |
| 13 | Police | `206C6E7C:00ABF2EC:0` |
| 14 | Fire | `206C6E7C:A0F42214:0` |
| 15 | Residential layer (population, demographics) | `206C6E7C:C106C4F5:0` |
| 16 | Commercial layer | `206C6E7C:20EC9849:0` |
| 17 | Industrial layer | `206C6E7C:A106CF3D:0` |
| 18 | Land value | `206C6E7C:C0AB8A88:0` |
| 19 | Street-level simulation | none |
| 20 | Traffic and mass transit | `206C6E7C:029CA804:0` |
| 21 | Budget | `E11BCC69:A11BCC54:0` |
| 22 | World layer (`SC3WorldLayer.ini`) | `206C6E7C:E11BDDF6:0` |
| 23 | Power | `206C6E7C:E0AFDF68:0` |
| 24 | Water | `206C6E7C:02BF0033:0` |
| 25 | Flammability | `206C6E7C:61448030:0` |
| 26 | Spacing/reservation grid | `206C6E7C:01FD7A8C:0` |
| 27 | Unidentified subsystem with monthly work (§6.1) | none |
| 28 | Unidentified geometry helper | none |
| 29 | Scenario | `206C6E7C:C3DE4D66:0` (scenario cities only) |
| 30–36 | Advisor manager and the six advisors | `022E288E:422E28E8:0`, the `013DEE82:*:B` topic lists, `206C6E7C:C28D0B6E:0` |
| 37 | Petitioner manager | `206C6E7C:02619041:0` |

The record group IDs are data (they appear in every save). Some equal the identifiers the original
uses for the subsystem internally and some do not; an implementation needs only the record keys.
Where other documents write "subsystem #n", they mean the row number in this table.

## 6. Subsystem schedule

### 6.1 What runs when

Work of each period, in run order. All rows are [STATIC] unless tagged. "Slice" means that the
subsystem processes only part of its map on each run (§6.4).

**Daily** (every day tick, before the city day handler):

1. Disasters (#6): daily area work.
2. Police (#13): coverage slice.
3. Fire (#14): coverage slice.
4. Land value (#18): daily work.
5. Scenario (#29): daily goal and condition check (`23-scenarios.md`).
6. Zone manager (#5): one zone-development step (`15-sim-growth.md` §4).
7. Active disaster instances (#6): day tick (spreading fires, riots and so on, `21-disasters.md`).
8. Pollution (#11): daily slice.
9. Crime (#12): daily slice.
10. Flammability (#25): daily slice.

Items 8–10 [STATIC, high confidence]: each of these three layers has its own daily entry in the
schedule. Pollution and crime also have a monthly step (monthly items 14–15). `17-sim-services.md`
§1 still lists the trigger of these runs as [OPEN] and recommends one run per day, which agrees
with this schedule. The aura layer's trigger is [OPEN] in both documents.

Items 1–5 always run in this order. Items 6–10 always run after items 1–5. Their order among
themselves is subsystem order [INFERRED, medium confidence]. Item 7 runs only when disasters are
present (`21-disasters.md`).

**Weekly** (Mondays, after the daily work of the same day):

1. Street-level simulation (#19): weekly pass.

**Monthly** (day 1 of each month, after the daily and any weekly work of the same day; the clock is
paused for the whole monthly work, §6.3 rule 4):

1. Disaster manager (#6): monthly disaster-manager step, when disasters are enabled
   [INFERRED: only when disasters are enabled].
2. Neighbours (#7): neighbour growth (January) and deal ageing (`19-sim-economy.md` §10.5–§10.6).
3. World layer (#22): economic-phase update (`15-sim-growth.md`).
4. RCI development layer (#8).
5. Ordinances (#9).
6. Residential layer (#15): demographics, employment, health and education (`20-sim-population.md`).
7. Commercial layer (#16).
8. Industrial layer (#17).
9. Traffic and mass transit (#20): monthly traffic work, including the monthly decay
   (`18-sim-transport.md`).
10. Budget (#21): the budget month-end, which includes the year-end on 1 January
    (`19-sim-economy.md` §7).
11. Power (#23).
12. Water (#24).
13. Unidentified subsystem #27.
14. Pollution (#11): monthly step.
15. Crime (#12): monthly step.

Items 1–3 always run before items 4–15. Their order among themselves is subsystem order
[INFERRED, medium confidence]. Items 4–13 run in exactly this order, and items 14–15 come after
them [STATIC].

**Yearly:** no subsystem has yearly work. Yearly effects are performed by the monthly work of
1 January (rule 7).

**Per minute** (only while a disaster instance is active): active disaster instances' minute work,
then the scenario's per-minute work (§6.3 rule 5).

**Not periodic.** These subsystems have no periodic work of their own: terrain, occupant manager,
network manager, flora layer, spacing grid, #28, the advisors and the petitioner manager. They act
on user commands and on requests from other subsystems [STATIC]. Advisors and petitioners have
their own timing (`22-advisors-news-petitioners.md`). The aura layer (#10) has no entry in this
schedule; what advances its sweep is [OPEN]. Recommended: run it daily after flammability.

**Per-building work (weekly).** Besides the layer-wide steps above, every building that has a
lot is itself enrolled in the schedule when it is added to the city, and withdrawn when it is
removed. It is enrolled at the period of the most frequent per-type work defined for its building
type. In the shipped game the only per-type work is the traffic trip generator
(`18-sim-transport.md` §4.1), which is weekly [STATIC].

On each weekly step, the enrolled buildings run **before** the weekly layer-wide steps. They run in
the order in which they were added to the city; buildings added during the step wait until it ends.
Building types with no per-type work are not enrolled. Per-building work does not take the city
lock [STATIC].

### 6.2 Summary by area

| Area | Frequency |
|---|---|
| Disasters | daily area work; daily instance tick; monthly manager step; per-minute instance work during disasters |
| Scenario goals/conditions | daily, plus per-minute work during disasters |
| Zone development | daily (one step per day) |
| Valves, demand, demographics (#8, #15–17) | monthly |
| Land value | daily |
| Police, fire | daily slices |
| Crime, pollution | daily slices plus a monthly step |
| Flammability | daily slice |
| Aura | [OPEN] (§6.1) |
| Neighbours, deals | monthly |
| World layer | monthly |
| Ordinances | monthly |
| Traffic and transit | monthly; street-level simulation weekly |
| Budget, bonds, year-end | monthly (year-end on 1 January) |
| Power, water | monthly |
| Population ageing, demographic graph histories | monthly; ageing and yearly/decade samples on 1 January (rule 7) |
| Budget graph history (funds chart) | monthly; yearly/decade samples on 1 January (rule 7) |

### 6.3 Ordering rules

1. **Within one clock tick** the cascades run in the order of §2.2: daily, then weekly, then
   monthly, then yearly, each only on its matching date [STATIC].
2. **Within one cascade** work runs in the order listed in §6.1 [STATIC except where tagged there].
3. **Each step runs to completion** before the next one starts. Steps are never interleaved
   [STATIC].
4. **Month day.** The monthly cascade runs: (a) pause the clock (§2.3) and mark the city busy;
   (b) the monthly work of §6.1; (c) the city month handler (§3.2); (d) resume the clock and clear
   the busy mark [STATIC]. A clock pause keeps the time already elapsed, and a resume discards the
   paused interval. So real time spent inside the monthly work is **not** credited to the simulated
   clock, and a long month-end does not cause catch-up ticks [STATIC]. Every monthly step therefore
   runs **before** anything that reacts to the month handler. The daily work of the same day has
   already run before the monthly work [STATIC].
5. **Per-minute work** exists only while at least one disaster instance is active. The disaster
   system starts it when an instance needs minute-level updates and stops it when none is left. While
   it exists, the clock runs in minute mode (§2.2), and the per-minute work runs once per simulated
   minute. That work is the active disaster instances' minute work, then the scenario's minute work
   [STATIC]. Minute mode is also what slows the game during disasters (§4). There is never any
   hourly work, so the hour period does nothing [STATIC].
6. **Long cascades and holds.** Before each step of a cascade the simulation checks two kinds of
   condition [STATIC]:
   * **Backlog pause.** If the simulator's own pending-work queue has **more than 3** entries, or
     the shared application message queue has **more than 500** entries, the city takes the same
     busy / clock-pause path as the monthly begin (rule 4) for the rest of that cascade, so the
     time is not credited. Both thresholds are **pending-item counts**, not milliseconds of wall
     time. (An earlier reading of the constant 500 as "500 ms" was wrong.) The busy mark used here
     is **not** part of the hold below.
   * **Hold wait.** The city reports a **hold** when either of the two city counts in §2.3 (pause
     count or pending/modal count) is non-zero. Hold is **not** the monthly busy mark, not a
     disaster lock, and not save/INI state [STATIC]. While a hold is reported and the simulation
     has not been told to stop, the simulation waits before starting the next step. The wait is
     re-checked about once per second. No step is skipped; the cascade continues where it stopped
     once the hold clears.
7. **Budget month-end and year-end.** The budget month-end (monthly item 10) performs the whole
   month-end of `19-sim-economy.md` §7. When the closing month is December (the run on 1 January)
   it also performs the year-end. There is no separate yearly trigger for the budget [STATIC].
   It runs **after** the transport monthly work and the RCI and demographics monthly work, and
   **before** power, water, the city's month handler and everything that reacts to it [STATIC].
   So the fares income the budget books is the value just closed by the transport step in the same
   tick [INFERRED from order].
   On 1 January the yearly cascade runs after the monthly cascade has finished; the budget's
   year-end has already happened by then [STATIC].
   **Demographics and graph sampling** [STATIC]. The residential layer's monthly step (monthly
   item 6) updates employment, health and education. On every run it appends one *monthly* sample
   to each of its three demographic graph histories: population, and two per-capita averages over
   cohorts 0–89 (both 0 when the population is 0). Only when the date of the run is
   **1 January** (month = 1) does it also: (i) perform the yearly cohort ageing shift and the
   newborn/birth-trend update of `20-sim-population.md`; (ii) append one *yearly* sample to each of
   the three histories; (iii) if the new year is a multiple of 10, append one *decade* sample to
   each. The budget's funds chart is sampled the same way inside the budget month-end: a monthly
   sample on every run, a yearly sample on the 1 January run, and a decade sample when the new year
   is a multiple of 10.
   Order on 1 January is therefore: daily work → monthly items 1–5 → demographics, ageing and
   demographic graphs → commercial, industrial → transport → budget month-end, year-end and funds
   chart → power, water, #27 → pollution and crime monthly steps → month handler → yearly handler
   and anniversaries.
8. **RNG and cheats.** Generator state is **not** saved; every generator is reseeded from the clock
   when the city is loaded (§8) [STATIC]. Cheat effects: see `27-platform.md` §5. Only the cheat log
   is saved.

### 6.4 Incremental (time-sliced) layer updates and `UpdatePeriod`

Grid layers do not recompute the whole map on each run. Each run processes a fixed slice of
cells, and the slice size is derived from `UpdatePeriod` [STATIC]:

* **Units:** `UpdatePeriod` is the number of the layer's own runs that make up one complete pass
  over the map. Crime, pollution, police, fire, land value and flammability run daily (§6.1), so
  for them it is also the number of simulated days per complete pass.
* **Crime** (`SC3Crime.ini [TuningParameters] UpdatePeriod`, shipped 40): the crime grid has one
  cell per 2×2 tiles. Cells per run = `floor(W·H / (4·UpdatePeriod))`, where W and H are the city
  size in tiles. A value of 0 is replaced by 1.
* **Police** (`SC3Police.ini`, shipped 30): cells per run = `floor(W·H / (16·UpdatePeriod))`, visited
  in a fixed 16-phase interleaved order. A value of 0 is replaced by 30. See `17-sim-services.md`.
* `SC3Aura.ini` (50), `SC3FireLayer.ini` (30), `SC3LandValue.ini` (60) and `SC3Pollution.ini` (30)
  carry the same key with the same meaning [INFERRED, high confidence]. The exact slice formulas
  belong to their owning documents.

## 7. View-region tracking (not simulation)

The city tracks the visible city-view rectangle and zoom level at three granularities. Services,
utilities, networks, sprites, street-level simulation and transit use this to decide what to update
or animate **on screen** [STATIC]. It does not affect simulation results [INFERRED, medium
confidence].

## 8. Random number generation

Two generators are in use [STATIC]:

1. **The simulation generator** below. Most random decisions use it.
2. **The process-wide C-runtime generator** (Microsoft C runtime `rand()`). These use it:
   * the shuffles of the zone developers' scan order (`15-sim-growth.md`, `10-save-format.md` §6.4);
   * the crime and pollution sweep orders (`17-sim-services.md`);
   * the ambient-sound start cell (`06-audio.md`).

   Its step is *s* ← (*s* × 214013 + 2531011) mod 2³², with result (*s* >> 16) AND 0x7FFF, i.e.
   0–32767. Its state is shared by the whole process and is never saved. Because the result is at
   most 32767, any "rand() mod m" with m > 32768 is biased, and a faithful implementation keeps
   that bias.

* **Integer generator:** 32-bit state `s`. Step: `v = s × 1103515245 + 12345` computed in 64 bits
  (multiplier 0x41C64E6D, increment 0x3039); the new state is `v mod 2^32`; the output is
  `R = floor(v / 2^16) mod 2^32`.
* `Rand(n)`: if `n = 0`, return 0. If `n < 0xFFFFFF`, draw one output and return `R mod n`, a value
  in `[0, n−1]`. Otherwise (n ≥ 0xFFFFFF), draw outputs until `R ≤ n` and return that `R`, a value in
  `[0, n]` (the upper bound is **inclusive** in this branch only) [STATIC]. In this branch the
  original masks `R` with all bits up to the highest set bit of `R` itself, which leaves `R`
  unchanged. So the result is plain rejection sampling, with the same distribution as
  `16-sim-utilities.md` §1.1.
* `RandInt(a, b) = a + Rand(b − a)`, a value in `[a, b−1]` for `b − a < 0xFFFFFF`.
* **Float generator** (separate state `t`): if `t = 0`, set `t = 0x12345678`. Then
  `t ← t × 0x278DDE6D mod 2^32` and `u = t / 2^32`. `RandRange(a, b) = a + (b − a)·u`.
* **Seeding:** seeding with a value `x` sets **both** states to it: `s = x` and `t = x` [STATIC]. So
  the float generator starts from the same seed as the integer generator. The 0x12345678
  substitution applies only when `t` is 0 at the time of a float draw. A seed of −1 means "use the
  system millisecond timer value" (the same value then goes into both states). Each subsystem that
  needs randomness has its **own** generator, seeded this way from the millisecond timer when the
  subsystem is created [STATIC]. The neighbour subsystem
  (neighbour creation, neighbour growth, deal offers) also uses short-lived generators seeded from
  the high-resolution performance counter at the moment of use [STATIC]. The city itself uses
  time-seeded generators only for one-off draws, for example the random loading-progress caption
  and the low 4 bits of a time-derived city identifier [STATIC]. Only the terrain generator is
  seeded from an explicit value, the terrain-generation seed (`11-world-terrain.md`) [STATIC].
* **Persistence:** generator states are **not** saved. No generator state or seed is read from a
  save; every generator is reseeded from the clock when the city's subsystems are created on load
  [STATIC]. The C-runtime generator is not saved either (above). A clone should seed from a clock
  source on load. Bit-exact replay of a saved city is therefore impossible in the original too, and
  conformance tests must be statistical, not bit-exact.

### 8.1 Test vectors [derived from the definition above]

Integer stream seeded with S = 1, first ten raw outputs R (hexadecimal):
000041C6, 59D4967E, BF022781, 959AC46B, 79D0F94B, 60B995FB, 0703D9E2, 584E9CFB, 81FCBF54, BA030FF6.

Real stream seeded with T = 1, first five states T after advancing (hexadecimal; the result is
T / 2³²):
278DDE6D, FB533A69, 59EBECB5, D369BF11, 8B14183D.

Real stream seeded with T = 0 (substituted by 0x12345678 on first use), first three states:
0B5CE118, 03F6A738, 36DFC2D8.

`Rand(n)` for n < 0x00FFFFFF is R mod n. For example, with seed 1 the first `Rand(100)` is 38.

## 9. Importing SimCity 2000 dates

When a city is created from a SimCity 2000 source, the source's elapsed-day count `n` and start year
are converted as follows [STATIC]:

* `day = n mod 25 + 1`
* `month = floor(n / 25) mod 12 + 1`
* `year = floor(n / 300) + startYear`
* If either value is missing, the date is set to 1 January 3000 and the founding year to 3000.
  This is the original's behaviour as written.

## Open questions

* Graph histories owned by subsystems other than the budget and demographics (crime, pollution,
  traffic graphs and so on): when they are sampled.
* What subsystem #27 does in its monthly step, and what #28 is.
* What advances the aura layer's sweep (§6.1).
* The relative order of daily items 6–10 and monthly items 1–3 (§6.1), recorded as subsystem order.
* The interval of the timer callback that drives the clock (§2.2).
* Which specific dialogs and operations raise the city pause count versus the pending/modal count
  (§2.3 / §6.3 rule 6); see also `09-ui-screens.md`.
