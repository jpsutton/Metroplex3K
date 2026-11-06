# 22 — Advisors, News Ticker, Petitioners, Opinion and Rewards

Status: first-pass draft. Sections are tagged per `00-README.md`. Text content (advice, ticker,
petitioner dialogue) is never reproduced here; only the linkage from logic to string IDs is given.
The engine loads all text from the user's installation (see `05-text-and-fonts.md`).

## 1. Overview

The subsystem has four cooperating parts:

| Part | Owner | Role |
|------|-------|------|
| Advisor topics | simulation side (advisor component) | Seven advisors evaluate city metrics each advisor cycle and raise/clear numbered *topics*. A topic has a heading, a message body, an optional ticker line, a mood value and a topic picture. |
| Petitioners | simulation side (petitioner manager) | Occasionally presents a citizen with a request (ordinance, gift, deal). Accept/decline has effects and posts ticker lines. |
| Reward buildings | City-planner advisor | Unlocks reward buildings (mayor's house, city hall, statue …) and business-deal buildings when conditions are met. |
| News ticker | application shell | A single scrolling text line at the bottom of the city view. Fed by advisors, petitioners, disasters and other subsystems, plus a pool of random filler messages. |

**There is no condition/expression mini-language in any of these INI files.** [STATIC] Every
trigger condition is fixed game behaviour, specified in this document; the INI files only provide numeric thresholds,
ID lists and presentation attributes. Section 8 gives the exact line grammar of every INI file
involved, which is the whole of the "language".

## 2. String tables and resource linkage

All text comes from string tables (resource Type `0x2026960B`, Group = table ID, Instance =
string ID; see `05-text-and-fonts.md`). The file names below are those shipped under
`Apps/Res/Text/<LANGUAGE>/`. [DATA]

| Table ID (Group) | File(s) | Instance = | Use |
|---|---|---|---|
| `0x825ADC5A` | `Sc3StringsAdvisorHeading.IXF`, `Sc3MBEStringsAdvisorHeading.IXF` | topic ID | Advisor topic heading (title line) |
| `0x225ADC6B` | `Sc3StringsAdvisorMessage.IXF`, `Sc3MBEStringsAdvisorMessage.IXF` | topic ID | Advisor topic body |
| `0xC25B11C3` | `Sc3StringsAdvisorTicker.IXF`, `Sc3MBEStringsAdvisorTicker.IXF` | topic ID | Ticker line for the topic (optional — a topic without an entry here posts no ticker line) |
| `0x82B85965` | `SC3StringsAdvisorHelpTitles.IXF` | help ID | Advisor "help/briefing" titles |
| `0xE26EC3B8` | `SC3StringsAdvisorHelp.IXF` | help ID | Advisor "help/briefing" bodies |
| `0xA26EC340` | `SC3StringsAdvisorHelpHeadings.IXF` | help ID | Help headings |
| `0x42C1ED2D` | `SC3StringsNewstickerTriggered.IXF`, `SC3HolidaysStringsNewstickerTriggered.IXF` | message ID | Event ("triggered") ticker news |
| `0x547BA060` | `SC3MBEStringsNewstickerTriggered.IXF` | message ID | Event ticker news added by Unlimited ("MBE" strings) |
| `0xC2498F0E` | `SC3StringsNewstickerMessages.IXF` | message ID | Random filler ticker news |
| `0xE26DD125` / `0x549B17B2` | `Sc3[Holidays]StringsPetitionerAdvice.IXF` | petitioner ID | Advisor's comment on a petition |
| `0xA26DD23B` / `0x449B17B8` | `…PetitionerButton1.IXF` | petitioner ID | Accept button caption |
| `0xE26DD247` / `0x449B17BC` | `…PetitionerButton2.IXF` | petitioner ID | Decline button caption |
| `0xC26DD29B` / `0x249B17CB` | `…PetitionerHeading.IXF` | petitioner ID | Petition heading |
| `0xC26DD2AB` / `0x249B17CE` | `…PetitionerMessage.IXF` | petitioner ID | Petition body |
| `0x826DD2D0` / `0x649B17D1` | `…PetitionerName.IXF` | petitioner ID | Petitioner's name |
| `0xE26DD2FA` / `0x049B17D4` | `…PetitionerReaction1.IXF` | petitioner ID | Reaction after accept |
| `0x226DD30B` / `0x649B17D8` | `…PetitionerReaction2.IXF` | petitioner ID | Reaction after decline |
| `0x026DD322` / `0x649B17DB` | `…PetitionerTicker.IXF` | petitioner ID | Ticker line when petitioner appears |
| `0xE27EBF4D` / `0x549B17DF` | `…PetitionerTickerX.IXF` | petitioner ID | Ticker line after accept |
| `0xE27EBF70` / `0x049B17E2` | `…PetitionerTickerY.IXF` | petitioner ID | Ticker line after decline |

The first ID of each petitioner pair is the base game, the second the Holidays pack; the
mapping is configured in `PetitionerManager.ini` (section 5.1), not hard-coded.

ID ranges present in the shipped English tables [DATA]:

* Advisor topics: base `0x01`–`0x13A` (212 headings, 219 bodies, 200 ticker lines); Unlimited
  additions `0x191`–`0x19B`, `0x19D` (12 topics, in the MBE files).
* Triggered news `0x42C1ED2D`: `0x64`–`0x72`, `0x1F4`–`0x341` (gaps); holiday file `0x3E8`–`0x3F0`.
* MBE triggered news `0x547BA060`: `0x64`–`0x92`, `0xC8`–`0xC9`.
* Random filler `0xC2498F0E`: `0x22`–`0x11A` (247 strings; `0x99` and `0x112` absent).
* Petitioners: base `0x01`–`0xFA`; Holidays `0x12C`, `0x12D`, `0x12F`, `0x131`.
* Help topics: `0x01`–`0x271A` in blocks (per-advisor blocks of 1000: `0x3E9…`, `0x7D0…`, `0xBB8…`,
  `0xFA0…`, `0x1388…`, `0x1770…`, `0x1B59…`, `0x2711…`).

Note: five non-English copies of `Sc3HolidaysStringsPetitionerTickerY.IXF` are truncated (no
index terminator) and fail to open in the original; the engine must tolerate a missing table and
simply show no ticker line. [DATA]

### 2.1 Token substitution in advisor/ticker text

Strings may contain tokens that the advisor component replaces before display. [STATIC]

| Token | Replaced by |
|---|---|
| `%MAYOR%`, `%YOURNAME%` | Mayor name |
| `%CITYNAME%`, `%YOURCITY%` | City name |
| `%POPULATION%` | Current population |
| `%YEAR%` | Current year |
| `%PARADENAME%` | Name used by parade/celebration messages [INFERRED] |
| `%ANYNEIGHBOR%` | Name of a (random) neighbour city |
| `%NEIGHBOR%` | Name of the specific neighbour the topic refers to |
| `%YEAROFDEAL%` | Year a neighbour/business deal was struck |
| `%TOKEN%` | Generic per-topic argument supplied by the raising logic |

Topics may also carry a list of per-instance argument strings; these are substituted in order
into the heading/body/ticker text when the topic is rendered. [STATIC]

## 3. Advisors

### 3.1 The seven advisors

Each advisor is a separate component with its own INI file. Names are those used in the
program's debug cheats (for identification only). [STATIC]

| Advisor | INI file | Advisor ID |
|---|---|---|
| Budget | `Advisor-Budget.ini` | `0x628D0C45` |
| City planning | `Advisor-CityPlanner.ini` | `0xC28D0B6E` |
| Health & education ("demographics") | `Advisor-Demographics.ini` | `0xC28D0F40` |
| Environment | `Advisor-Environment.ini` | `0x828D04EB` |
| Public safety | `Advisor-PublicSafety.ini` | `0x828D0A2F` |
| Transportation | `Advisor-Transportation.ini` | `0x028D0FC5` |
| Utilities | `Advisor-Utilities.ini` | `0x022E288E` |
| (Petitioner manager) | `PetitionerManager.ini` | — (its save record uses Group `0x02619041`, see P.2) |

The *advisor ID* identifies the advisor in messages and in data: after each evaluation cycle an
advisor broadcasts an "advisor updated" notification carrying its advisor ID (UI refresh), and
`PetitionerManager.ini` `[PETITIONER_ADVICE_MAPPING]` uses these IDs (section 5.1). [STATIC]

All INI files are read from the loose file `\Sys\<name>` if present, otherwise from the packed
`Sys\SYS.PAK` (see `02-containers.md`). Each has an `[Admin]` section with `Enabled=ON`.

#### 3.1.1 Threshold sections per file [DATA; readers STATIC]

The shipped files contain exactly these sections (besides `[Admin]`). Every threshold key is read
from the section it appears in here; there are no other threshold sections. Key names are unique
across all seven files (no key name occurs twice), so one flat key → value map is safe.

| File | Sections | Read by |
|---|---|---|
| `Advisor-Budget.ini` | `TAX_RATE_CUTOFFS`, `TREASURY_NUMBERS` | budget advisor |
| | `BUSINESS_DEALS` (`MONTHS_TILL_BUSINESS_DEAL`), `BUSINESS_DEALS2` (`Name=type;year;cost;petition`) | business deals, `19-sim-economy.md` §11 |
| `Advisor-CityPlanner.ini` | `ADVICE_TRIGGERS`, `GROWTH_CAP_LIMITS`, `DEMAND_CUTOFFS`, `DEMAND_RESPONSIVENESS` | city-planning advisor |
| | `REWARD_BUILDINGS`, `REWARD_BUILDINGS_2`, `BUSINESS_DEAL_BUILDINGS` | rewards, section 6 |
| `Advisor-Demographics.ini` | `FUNDING_RATE_CUTOFFS`, `ADVICE_TRIGGERS`, `PETITIONER_GENERATION` | health & education advisor |
| `Advisor-Environment.ini` | `POLLUTION_RATE_CUTOFFS`, `GARBAGE_BUILDINGS`, `PETITIONER_GENERATION` (empty) | environment advisor |
| `Advisor-PublicSafety.ini` | `FUNDING_RATE_CUTOFFS` (police/fire funding), **`ADVICE_GENERATION`** (`FIRE_COVERAGE_CUTOFF`, `FIRE_STATION_MIN_POP`, `JAIL_MIN_POP`, `HIGH_CRIME_LEVEL`, `MED_CRIME_LEVEL`), `PETITIONER_GENERATION` (`POLICE_STATION_RATIO`) | public-safety advisor |
| `Advisor-Transportation.ini` | `FUNDING_RATE_CUTOFFS`, `TRANSPORTATION_ADVANCEMENTS`, **`TRANSPORTATION_CUTOFFS`** (`ROAD_DAMAGE`, `HIGH_ROAD_TRAFFIC`, `MED_ROAD_TRAFFIC`, `MED_RAIL_USAGE`, `HIGH_RAIL_USAGE`), `PETITIONER_GENERATION` | transportation advisor |
| `Advisor-Utilities.ini` | `FUNDING_RATE_CUTOFFS`, `CONSUMPTION_RATE_CUTOFFS`, `POWER_BUILDINGS`, `WATER_BUILDINGS`, `NEIGHBOR_DEAL_CONSTANTS`, `PETITIONER_GENERATION` (`WATER_CONSERVATION_YEAR`) | utilities advisor |

There are no sections named `CRIME_RATE_CUTOFFS` or `TRAFFIC_RATE_CUTOFFS`, no `ADVICE_TRIGGERS`
in the public-safety and transportation files, and no `PETITIONER_GENERATION` in the city-planner
file. The crime and traffic thresholds are in `ADVICE_GENERATION` and `TRANSPORTATION_CUTOFFS`.
The SIMADV loader reads each key by (section, key) pair; it parses crime and funding cutoffs with
`atoi` into a byte, and population minimums into a u32 [STATIC].

The ordinance-petition thresholds (`POPULATION_FOR_GAMBLING`, `MAX_GAMBLING_AURA`,
`PRO_READING_AURA`, `PRO_READING_POP`, `LE_LEVEL_FOR_CLINIC`, `PARKING_FINES_YEAR`,
`POWER_CONSERVATION_YEAR`, `HOMELESS_SHELTER_POPULATION` and the other `*_YEAR` keys) are not in
any advisor file. They are in `PetitionerManager.ini [PETITIONER_ORDINANCE_TRIGGER_MAPPINGS]`
(section 5).

Keys that are loaded but never read by any rule or petition (dead data; reproduce by ignoring)
[STATIC]: `RESIDENTIAL_/COMMERCIAL_/INDUSTRIAL_DEMAND_NEGATIVE`, `INDUSTRIAL_DEMAND_POSITIVE`,
`HEALTH_FUNDING_*`, `EDUCATION_FUNDING_*`, `COLLEGE_POPULATION_MINIMUM`, `OVERALL_POLLUTION_*`,
`POWER_FUNDING_*`, `WATER_FUNDING_*`, `WATER_CONSUMPTION_CRITICAL/HIGH`, `WATER_CONSERVATION_YEAR`,
`POLICE_STATION_RATIO`, `MIN_POWER_SELL`, `MIN_WATER_SELL`, `MIN_GARBAGE_IMPORT`.

No petition chance is configurable. Every petition probability in sections 3.7–3.13 is a constant
in code.

### 3.2 Topic attributes

A *topic* is identified by a 32-bit topic ID (the string Instance). For each topic ID:

* **Heading / body / ticker text**: from tables `0x825ADC5A`, `0x225ADC6B`, `0xC25B11C3`.
* **Mood** (integer 1–5): `AdvisorMoods.ini`, section `[AdvisorMoods]`, key `0x%08X` (topic ID as
  8 upper-case hex digits with `0x` prefix). If the key is absent, `GoldAdvisorMoods.ini` (same
  section/key format) is consulted. If absent in both, the mood is 0. [STATIC]
* **Topic picture**: `Advisor-TopicGfxIDs.ini` / `GoldAdvisor-TopicGfxIDs.ini`, section
  `[AdvisorTopicGfxIDs]`, value `0xGROUP,0xINSTANCE`. All shipped values use group `0x831D112B`, the
  advisor-topic image file `Res/UI/Shared/831D112B_AdvTopics.ixf`. [DATA]

"Gold" is the internal name of the *Unlimited* additions: the Gold files contain only the
12 Unlimited topics `0x191`–`0x19B`, `0x19D`; the base files contain the original game's topics.
The program consults both and the base file first; there is no mode switch. [STATIC]

### 3.3 Advisor mood

The advisor's displayed mood (portrait expression) is the **integer mean** of the moods of all
of its currently visible (not suppressed) topics: sum of moods ÷ count, truncated. If the advisor
has no visible topic, its mood is **3**. [STATIC] Topic moods are therefore static per topic;
there is no other mood computation. Mood scale semantics: 1 = best … 5 = worst [INFERRED from
which topics carry 1 vs 5; confirm against portrait images in `52DB60A0_Advisors.ixf`].

### 3.4 Topic life cycle (cool-down state machine)

[STATIC; meaning of flags INFERRED from behaviour]

Each advisor keeps an ordered list of *topic instances*. An instance holds: topic ID; *group key*
K (32-bit, 0 = none); the ticker item it posted (if any); a list of argument strings; a *limit* L
(0–255); a *counter* C (starts 0); and flags *persistent* P, *expiring* E, *sticky* S,
*suppressed* (hidden) H and *re-raised* R.

**Raising.** Advisor rules call *raise(topic, K, L, P, E, S)*. Rules use only these parameter
combinations (all other parameters 0):

| Form | Meaning |
|---|---|
| raise(topic) | plain condition topic, no key |
| raise(topic, K) | condition topic in key group K |
| raise(topic, K, L) | condition topic with re-show interval L |
| raise(topic, K, L, P=1) | persistent topic |
| raise(topic, K, L, P=1, E=1) | one-shot event topic |
| raise(topic, K, L, P=1, E=1, S=1) | sticky one-shot event topic |

Raise procedure:

1. If an instance with the same topic ID exists: if it is expiring (E), it is deleted (its ticker
   item withdrawn) and processing continues at step 2 (a fresh instance replaces it); otherwise its
   R flag is set and raising ends.
2. A new instance is created with C = 0, H = 0, R = 1 and the given K, L, P, E, S.
3. Its ticker line is posted (section 3.5), unless the advisor's ticker table lacks the string.
4. If K ≠ 0, every existing instance with the same key K is deleted (withdrawing its ticker item),
   then the new instance is appended. If K = 0 and an instance with this topic ID already exists,
   the new one is discarded. Otherwise it is appended.

Thus a key groups mutually exclusive topics (e.g. "taxes low / on target / high" for one zone):
only the most recently raised topic of a group is listed.

**Update** (after all rules of the cycle have run), for each instance in list order:

1. If S or P is set: if C < L then C += 1; else H := 1 and its ticker item is withdrawn.
   (Such instances are never removed by the update; they stay hidden until re-raised.)
2. Else if E is set: if C < L then C += 1; else the instance is deleted.
3. Else if R is set: if H is set, C += 1, and when C ≥ L: C := 0, H := 0 and the ticker line is
   posted again. (Visible re-raised instances are unchanged.)
4. Else (the condition was not raised this cycle): the instance is deleted and its ticker item
   withdrawn.

Finally R is cleared on every instance.

**Dismissal.** When the player dismisses a topic in the advisor panel, it becomes hidden (H := 1,
ticker item withdrawn) only if L ≠ 0 or P or E is set; an L = 0 condition topic cannot be
dismissed. A dismissed condition topic that keeps being raised re-appears after L further cycles.

**Forced topics.** Each advisor also has a list of *forced* topic IDs (filled by debug cheats and
by some events); every forced topic is raised as raise(topic) at the end of each cycle, after the
update. [STATIC]

Mood (section 3.3) and the advisor panel consider only instances with H = 0.

### 3.5 Ticker posting by advisors

When a topic first appears (or re-appears after suppression) and its ticker string exists, a
ticker item is posted with key (`0xC25B11C3`, topic ID), with any map location attached by the
topic (which makes the ticker line clickable). The ticker attributes come from the ticker
data files (section 4.2). Withdrawing a topic removes its pending ticker item. [STATIC]

### 3.6 Advisor evaluation cycle

**Cadence** [STATIC]. Advisors and the petitioner manager are not scheduler agents. Each of the
seven advisors and the petitioner manager registers two listeners with the simulation calendar
(`14-sim-clock.md` §2.2): one with period code 4 (**week**) and one with period code 3 (**day**).

* The week notification (Mondays) only sets the component's "needs update" flag.
* Every day notification checks the flag; if it is set, one full evaluation runs and the flag is
  cleared (the flag is cleared on every day notification, set or not).

Because the calendar notifies day listeners before week listeners on the same date, the flag set
on Monday is consumed by the **Tuesday** day notification: every advisor and the petitioner
manager evaluates exactly **once per game week, on Tuesday** [STATIC for the registration;
INFERRED for the weekday from the listener order in `14-sim-clock.md`]. All "cycle" counts in this
document (limits L, cool-downs, streaks such as "≥ 7 cycles") are therefore **weeks**.
Within one Tuesday the components run in calendar-listener registration order, i.e. the order in
which they were initialised (`14-sim-clock.md` §5) [INFERRED]. The flag is not saved; after
loading, the first evaluation happens on the first Tuesday after the next Monday.

Additionally, the advisor panel can trigger an on-demand gather-and-rules pass (metrics and
rules only, no update step), which is why saved topic instances can carry R = 1 [STATIC].
Advisors also listen for the "ordinance changed" notification (argument = ordinance ID) and
raise an ordinance-specific acknowledgement topic immediately (e.g. CPR `0x00D0723D` → `0x2D`,
clinics `0x40D07236` → `0x28`, youth sports `0x815B4CEF` → `0x29`) [STATIC; full table OPEN].

One evaluation:

1. Gather metrics from the simulation subsystems.
2. Run the advisor's rules (they raise topics, section 3.4).
3. Update all instances, then clear the R flags (section 3.4).
4. Run the advisor's *post-update* rules (petitioner requests, reward offers, business deals);
   topics they raise survive into the next update because their R flag is set after the clear.
5. Raise all forced topics.
6. Broadcast the "advisor updated" notification with the advisor ID.

### 3.7 Budget advisor rules [STATIC]

Inputs: per-zone tax rate r_z (integer percent, zones R, C, I), per-zone tax effect x_z (the
budget model's tax-rate growth factor for r_z, see `19-sim-economy.md`), treasury T, population
N, and the budget's **estimated end-of-year totals**: I = total estimated income and E = total
estimated expense, exactly as shown in the budget window (`19-sim-economy.md` §7, "Estimated
end-of-year figures": per line, YTD + current monthly amount × (13 − month), summed over the income
lines and over the expense lines). They are read fresh at each advisor evaluation. [STATIC]

Thresholds (`Advisor-Budget.ini`): G = `[TAX_RATE_CUTOFFS] TaxGrowthThreshold` × 0.0001,
D = `TaxDeclineThreshold` × 0.0001 (both parsed as integers then scaled; shipped 0.05 and −0.1);
TL = `[TREASURY_NUMBERS] TREASURY_LOW`; LG = `LOSE_GAME_LEVEL` (both 64-bit simoleons).

**Tax topics** (one per zone, key group per zone):

| Condition (evaluated in order) | R topic | C topic | I topic |
|---|---|---|---|
| x_z < D and r_z ≥ 2 | `0x3A` | `0x3E` | `0x42` |
| else x_z < G and r_z ≠ 0 | `0x39` | `0x3D` | `0x41` |
| else | `0x38` | `0x3C` | `0x40` |
| Key group | `0x82A493A5` | `0x42A493C2` | `0x02A493DD` |

**Cash-flow topic** (key `0xC2EBE5E9`):

| Condition | Topic |
|---|---|
| I ≤ 1.10·E and I ≥ 0.98·E and TL < T < 3·TL | `0x10D` |
| I ≤ 1.10·E and I < 0.98·E and T ≤ TL + 2·(E − I) | `0xD0` |
| I ≤ 1.10·E and I < 0.98·E and T > TL + 2·(E − I) | `0x45` |
| I > 1.10·E and T > 4·TL | `0x44` |
| otherwise | none |

(1.10·E and 0.98·E are computed as ⌊E·110/100⌋ and ⌊E·98/100⌋ in 64-bit integers.)

**Reserve topic** (key `0xC2EBE7F2`):

| Condition (in order) | Topic |
|---|---|
| T > 2·TL + 5·E and N > 10000 | `0x47` |
| TL ≤ T (and T ≥ 0) | none |
| 0 < T < TL | `0x49` |
| T = 0 | none |
| T < 0 and T ≤ −LG | `0xCE`, and the **game is lost** (mayor fired) |
| T < 0 and T ≥ 8·⌊LG / −10⌋ (≈ −0.8·LG) | `0x125` if N ≤ 50000, else `0x4A` |
| T < 0 otherwise | `0xCD` |

**Trash-deal hint** (key `0x031C8D5D`): if T < TL, no neighbour deal is active, no neighbour deal of
kind 5 exists (kind 5 = import the neighbour's garbage, the player is paid; `19-sim-economy.md` §10.1) and the city has no building of type `0x302A`: raise `0x04`
if the city's neighbour-connection count < 4, else `0x03`.

**Post-update rules**: low-treasury business-deal offers (`19-sim-economy.md` §11). The `[BUSINESS_DEALS2]` fourth field
(called "message ID" there) is a **petition ID** in the petitioner tables: 149 = `0x95`
(casino), 210 = `0xD2` (prison), 16 = `0x10` (toxic-waste plant), 206 = `0xCE` (mega-mall). Petition
IDs and advisor topic IDs are **separate ID spaces** (they index different string tables, see §2), so
petition `0xCE` (mega-mall) is unrelated to topic `0xCE` (game lost) above; the
answers to those petitions are dispatched to the budget advisor's deal handler (section 5.5), and occasional petitioners `0x11`/`0x12`/`0xD5` (business
deal follow-ups, 2% chance per cycle when the corresponding business exists [INFERRED]).

### 3.8 Environment advisor rules [STATIC; metric identity INFERRED]

Evaluated only when population ≥ 100. Thresholds: `Advisor-Environment.ini [POLLUTION_RATE_CUTOFFS]`
`WATER_POLLUTION_HIGH` (WH), `WATER_POLLUTION_MEDIUM` (WM), `AIR_POLLUTION_HIGH` (AH),
`AIR_POLLUTION_MEDIUM` (AM) — integers compared with the city-wide water- and air-pollution
indices WP and AP (same scale as the pollution graphs, `17-sim-services.md`).
`OVERALL_POLLUTION_*` are loaded but not used by these rules.

| Rule | Condition | Topic (key group) |
|---|---|---|
| Water (only if the city has a water system) | WP > WH | `0x7C` (`0x82A497F0`) |
| | WM < WP ≤ WH | `0x7B` (`0x82A497F0`) |
| | WP ≤ WM and `0x7B` or `0x7C` currently listed | event `0xF4`, L = 6 (`0x82A497F0`) |
| Air | AP > AH | `0x7E` (`0x22A497FB`) |
| | AM < AP ≤ AH | `0x7F` (`0x22A497FB`) |
| | AP ≤ AM and `0x7E` or `0x7F` currently listed | event `0x80`, L = 6 (`0x22A497FB`) |
| Garbage (no trash-export deal active) | garbage not accumulating, surplus capacity within limit | `0x78` (`0xC2F8060C`) |
| | not accumulating but near capacity | `0x77` if waste-to-energy is unlocked, else `0xF8` (see Garbage metrics) |
| | accumulating, year < 1920 | `0x76` |
| | accumulating, 1920 ≤ year < 1970 | `0xEF` |
| | accumulating, 1970 ≤ year ≤ 1982 | `0xF0` |
| | accumulating, year > 1982 | `0xF2` if recycling is unlocked, else `0xF1` |
| Trash export hint | accumulating ≥ 7 cycles, no export deal: < 4 neighbour connections and no seaport | `0x70` (`0x03387EA5`) |
| | otherwise with ≥ 1 connection and no existing deal | `0x6F` (`0x03387EA5`) |
| Problem solved | accumulation streak ended after ≥ 7 cycles | `0xF3`, L = 60 |

The hard-coded years 1920/1970/1982 select which garbage-solution advice text applies.

**Garbage metrics** [STATIC]. Each cycle the environment advisor reads from the pollution layer
(`17-sim-services.md` §7.4, `16-sim-utilities.md` §4.5):
* the **previous month's** history entries of the garbage counters: produced *G0*, imported
  *G1*, exported *G2*, recycled *G3*, incinerated *G4*, landfilled *G6* (*G5*, waste-to-energy,
  is read but not used). "Previous month" is the entry before the current month, wrapping from
  January to December;
* the burn capacities per pollution run of the incinerators *Bi* and waste-to-energy plants *Bw*;
* the landfill store's `Dlast`, *R* (spare room on registered landfill tiles);
* the accumulating flag *A* = the map's total standing garbage was non-zero at the end of the
  last completed pollution sweep. The total is a running sum, kept as garbage is set on or
  taken off tiles during the sweep.

From these:
* demand *GD* = *G0* + *G1* (64-bit; imports are thus counted twice, since *G0* already
  includes them; reproduce);
* capacity *GC* = ⌊*R* / 12⌋ + 30·(*Bi* + *Bw*) + *G3*. The advisor also has a term for landfill
  decay, but it asks the landfill developer for an interface that nothing implements, so the
  factor is always 0 and the term vanishes (original defect; reproduce);
* "not accumulating, surplus within limit" (topic `0x78`) = not *A* and *GD* ≤ *GC*;
* "not accumulating but near capacity" = not *A* and *GD* > *GC*: topic `0x77` if the
  waste-to-energy plant (12853) is unlocked (section "Building availability"), else `0xF8`.
  Correction: the earlier reading tied this to a trash-import deal;
* "accumulating" = *A*. The accumulation counter rises by 1 each cycle with *A* and falls by 1
  (not below 0) without it;
* the "problem solved" event `0xF3` (L = 60) fires when the counter is > 6 and *A* is clear.
  Without an export deal it also needs 10·*GD* < 7·*GC*.

The year bands of the accumulating case use the city year: < 1920 → `0x76`; < 1970 → `0xEF`;
≤ 1982 → `0xF0`; later → `0xF2` if the recycling centre (12851) is unlocked, else `0xF1`.

### 3.9 Public-safety advisor rules [STATIC; metric identity INFERRED]

Thresholds (`Advisor-PublicSafety.ini`): PH/PM = `POLICE_FUNDING_HIGH/MEDIUM`, FH/FM =
`FIRE_FUNDING_HIGH/MEDIUM` (percent of recommended funding), CH/CM = `HIGH_CRIME_LEVEL` /
`MED_CRIME_LEVEL` (city crime index), `FIRE_STATION_MIN_POP`, `JAIL_MIN_POP`,
`POLICE_STATION_RATIO` (population per police station, petition trigger),
`FIRE_COVERAGE_CUTOFF` (percent). Metrics: police/fire funding percent pf/ff (department
funding bytes), crime index K, population N, station counts, police coverage percent Q.
Strike-warning counters: each is that layer's **strike chance per tick** (`17-sim-services.md`
§3.4, 0–10 000; 10 000 while striking), divided by 100 with integer division. It is read at each
advisor cycle. So "counter ≥ 16" means a strike chance ≥ 1 600. This is the same scheme as W_s in
3.12. CORRECTION: it is not a count of months of under-funding [STATIC for police; INFERRED for
fire, whose layer mirrors police].

| Rule (key group) | Condition, first match wins | Topic |
|---|---|---|
| Police (`0x42A498C9`) | police on strike | `0x9F` |
| | no police station | `0x11F` |
| | strike-warning counter ≥ 16 | `0xA0` |
| | pf > PH and K > CM | `0x132` |
| | pf > PH and K ≤ CM | `0xFB` |
| | PM < pf ≤ PH | `0xA2` |
| | pf ≤ PM | `0xA0` (same topic as the strike-warning case) [STATIC, SIMADV] |
| Fire (`0x22A498D1`) | firefighters on strike | `0xA7` |
| | no fire station: N > `FIRE_STATION_MIN_POP` → `0xFC`, else `0x120` | |
| | strike-warning counter ≥ 16 | `0xA8` |
| | ff > FH | `0x130` |
| | FM < ff ≤ FH | `0xAA` |
| | ff ≤ FM | `0xA9` |
| Crime (no key) | K > CH → `0xBC`; else K > CM → `0xC5` | |
| Jails (`0xC358210E`) | no jail (agent type 13881 count = 0): only if N > 1000; then K > CM → `0xBA`. Jail present: police-layer value J3 ≠ 0 → `0xB9`; else 10·J2 > 8·J1 → `0xBB` (J2 more than 80 % of J1); else nothing. J1, J2, J3 = police layer getters +0x70, +0x74, +0x78 [STATIC; INFERRED: J1 jail capacity, J2 inmates, J3 "jail full" flag]. CORRECTION: the earlier "over capacity by more than 8× a capacity term" reading was wrong | |
| Police coverage (`0xC3582DBE`, only N > 1000) | Q < 50 → `0x9B`; Q < 75 → `0x9C`; Q < 99 → `0x9D`; else (Q ≥ 99): K > CM and pf < 100 → `0x9E`; else K < CM → `0xC2` | |
| Fire coverage (`0x235981F6`, only N > 1000) | Qf < 50 → `0xA3`; Qf < 75 → `0xA4`; Qf < 99 → `0xA5`; else ff ≥ 100 → `0xC4` | |
| `0xD9FDDF6D` | float Ra > 0.8 → `0x193` | |
| `0x1A4A387F` | float Rb > 0.8 → `0x199` | |

Metric sources [STATIC, SIMADV safety metric gather]: Q = police layer getter +0x6C, Qf = fire layer
getter +0x6C (both percent); station counts by agent type: police 13130, fire 13002, jail 13881;
Ra = interface `0x22963800`/`0xE475EAD4` method +0x10, Rb = interface `0x13873256`/`0x24889F6F`
method +0x18 (both truncated to float) [OPEN: meaning of Ra/Rb]. The crime topics use key 0.

Public-safety petitions (post-update; probability r(100) < chance per cycle, cool-down L in
cycles). Every randomised request is skipped while the same petition is pending. No petition at
all when N < 100. [STATIC, SIMADV; CORRECTION of the earlier table]

1. Strikes (no random, not gated by pending): police **not** striking and warning counter > 15 →
   `0x9F`; police striking → `0xA0`; argument `0x438293B1`, L = 6. Fire not striking and warning
   counter > 15 → `0xCC`; fire striking → `0xA2`; argument `0xE38293BE`, L = 6.
2. K > CH and N > 50000 → `0x22`, 4 %, L = 48.
3. Crime branch, first match wins:
   * K > CH and N > 10000: curfew (`0xC2BF1DC5`) not enacted → `0x66` 2 %; youth sports
     (`0x815B4CEF`) not enacted → `0x6F` 3 %; then `0x37` 4 %. All L = 84.
   * else K > CM and N > 1000 (this includes K > CH with N ≤ 10000): youth sports not enacted →
     `0x6F` 2 %; then `0x37` 2 %. L = 84.
   * else K < CM and N > 100: curfew enacted → `0x67` 3 %; youth sports enacted → `0x70` 2 %. L = 84.
4. K < CM and topic `0xBC` or `0xC5` is still in the advisor's topic list → `0x38` with chance
   40 %, requested as a persistent petition (P = 1), L = 6.
5. K < CM and N > 10000: Q > 98 → `0x23` 5 %, L = 60; pf > 105 → `0xD3` 5 %, L = 36.
6. Police stations > 0, no jail, N > `JAIL_MIN_POP` → `0x0B`, L = 36 (no random, no pending test).
7. No fire station and N > `FIRE_STATION_MIN_POP` → `0x32` 3 %, L = 24.

All comparisons with CH/CM are on bytes (the INI values are stored as u8).
`POLICE_STATION_RATIO` is loaded but not read by either public-safety rule function.
`FIRE_COVERAGE_CUTOFF` is used only by the fire-disaster message handler (`0x621CDA33`): when the
fire coverage at the fire's cell is below the cutoff the advisor raises topic `0x9A` (key 0, L = 6)
[STATIC, SIMADV].

### 3.10 Transportation advancement announcements [STATIC]

Each transportation-advisor cycle draws r = uniform 0–99 (shared LCG).

* If r < 10: announce **at most one** new transport technology, checked in this order, the first
  whose "announced" flag is clear and whose `Advisor-Transportation.ini
  [TRANSPORTATION_ADVANCEMENTS]` year is ≤ the current year:
  `SUBWAY_AVAILABLE` → topic `0x63`, `BUS_STATION_AVAILABLE` → `0x64`, `AIRPORT_AVAILABLE` →
  `0x87`, `HIGHWAY_AVAILABLE` → `0x110`. The topic is raised as a sticky one-shot event
  (P = E = S = 1) with L = 120 and no key; the announced flag is set.
* Otherwise (r ≥ 10): an announcement topic still listed is deleted once the city has built the
  corresponding facility (subway, bus stations, airport, highway respectively).

**Technology flags** [STATIC]. The four flags are 1 subway, 2 bus stops, 3 airport, 4 highway.
They are not saved. They are re-derived when the city is set up, and again when the "city
opened" notification arrives (message `0x632ABA43`, posted after a city is loaded or started
[INFERRED name]). At that point each flag ← (its year ≤ the current year), with no announcement.
During play a flag becomes set only through the announcement above. A technology whose year
passes during play therefore stays unavailable until the 10 % draw announces it.
Every time a flag is (re)evaluated, the advisor posts a "tool enabled/disabled" message
(`0x025F0A92`, tool ID, flag) for each gated tool:

| Flag | Tool command IDs (`08-ui-framework.md`) |
|---|---|
| subway | `0x10004201` subway, `0x10004202` subway station, `0x10004009` subway–rail station |
| bus | `0x10004004` bus stop |
| airport | `0x10003006` airport zone |
| highway | `0x10004301` highway, `0x10004302` highway on-ramp |

An announcement posts "enabled" for its tools. The tools are greyed while their flag is clear. The
hot keys that say "only if available" follow the same flags. The city-planning advisor reads
flags 3 and 4 (3.11).

#### 3.10.1 Transportation advisor rules [STATIC; metric identities INFERRED from topic meaning]

Thresholds (`Advisor-Transportation.ini`): RFH, RFM, RFL = `ROAD_FUNDING_HIGH/MEDIUM/LOW`,
TFH, TFM = `RAIL_FUNDING_HIGH/MEDIUM` (percent of recommended funding); RD = `ROAD_DAMAGE`
as a fraction. It is parsed as a real (C `atof`), multiplied by the 32-bit constant 0.01 in
extended precision, and stored as a 32-bit float. So `ROAD_DAMAGE=15` gives RD ≈ 0.15 (the nearest
float just below 0.15) [STATIC]. THR, TMR = `HIGH_ROAD_TRAFFIC`,
`MED_ROAD_TRAFFIC`; TMU, THU = `MED_RAIL_USAGE`, `HIGH_RAIL_USAGE`. Metrics: road funding rf and
transit funding tf (percent); nRd = road + highway tiles, nTr = rail + subway tiles (two counts
each); damage = the road condition X of `12-networks.md` §10: potholed road pieces ÷ road-piece
count. It is read once per advisor cycle and stored as a 32-bit float [STATIC]; fail = fraction of failed commutes (real); Tr = road
traffic index; Ru = mass-transit usage index; nB = buildings of agent type 12210 (bus stops),
nT = agent types 12230 + 12360 + 12361 (transit stations); airport zone tiles, airport tiles with a
non-development result code (9, 10, 14, 15, 16, 18), airport buildings (12310).

The rules run in this order:

1. nRd + nTr = 0 → `0x53` (plain).
2. Roads (key `0xC2A499BA`): if nRd = 0 → `0x123`. Else first match: rf > RFH and damage < 0.01 →
   `0x59`; rf > RFM and damage < 0.01 → `0x5A`; rf > RFL and damage ≤ RD → `0x5B`; else `0x5C`.
   When damage is NaN (highways but no road pieces, `12-networks.md` §10), every damage test
   above counts as true. With rf > RFH such a city gets `0x59` [STATIC: the comparison
   branches treat an unordered result as "less"].
3. Transit (key `0xE2A499BE`): if nTr = 0 → `0x124`. Else: transit strike → `0x61`; strike-warning
   counter > 10 → `0x60`; tf < TFM → `0x60`; tf ≤ TFH → `0x5F`; else `0x5E`.
4. Transit use (key `0xA3372CAE`), only if nTr > 2: Ru > Tr and Ru > TMU → `0x11B`; else 3·Ru < Tr
   → `0x11C`.
5. Traffic (key `0x43372E49`), only if nRd > 0: Tr > THR → `0x4F`; else Tr > TMR → `0x50` if
   nTr = 0, no transit station and nB ≤ 0, otherwise `0x51`; else, if fail ≤ 0.2 → `0x52`.
6. fail > 0.2 → `0x56` (plain).
7. Airport zone tiles > 0, failing airport tiles > 0 and no airport building → `0x62` (plain).
8. Technology announcements (3.10).

The constants 0.01 and 0.2 are hard-coded (32-bit floats).

**Transportation petitions (post-update)** — nothing at all if N < 100. Plain requests;
"not pending" and r(n) as in 3.11.1; Y = current year.

| Petition | Condition | Chance | L |
|---|---|---|---|
| `0xA4` / else `0xA3` | transit on strike / else strike-warning counter > 15 (argument `0xA3829374`) | always | 6 |
| `0x72` (carpool) | Tr > `CARPOOL_TRIGGER_LEVEL`, Y ≥ `CARPOOL_TRIGGER_YEAR`, carpool ordinance not enacted, not pending | r(100) = 0 | 120 |
| `0x74` (crossing guards) | Tr > `CROSSING_GUARD_TRIGGER_LEVEL`, Y > `CROSSING_GUARD_TRIGGER_YEAR`, not enacted, not pending | r(100) = 0 | 120 |
| `0xD8` (alternate-day driving) | Tr > `ALTERNATE_DAY_DRIVING_LEVEL`, Y > `ALTERNATE_DAY_DRIVING_YEAR`, ordinance `0xE2BF1E2A` available, not pending | r(100) = 0 | 120 |
| `0x0F` | Tr > `BUS_STOP_TRIGGER_LEVEL` and nB < 2 | always | 24 |
| `0x15` / `0x1C` | Tr > THR and nTr < 2: nB < 2 → `0x15`, else `0x1C` | always | 24 |
| `0x0D` | Ru > THU, rail tiles > 2 and n(12230) + n(12361) > 2 | always | 24 |
| `0x16` / `0x1A` | Ru > THU, subway tiles > 2, n(12360) + n(12361) > 2, neither pending; r(2) = 0 → both `0x16` and `0x1A`, 1 → `0x1A` only | always | 24 |

Note the carpool year test is inclusive (≥) while the other year tests are strict.

### 3.11 City-planning advisor rules [STATIC; metric identities INFERRED where noted]

Thresholds (`Advisor-CityPlanner.ini`):

| Symbol | Key | Unit / use |
|---|---|---|
| SS, MS | `[ADVICE_TRIGGERS] SMALL_SIZE_CITY`, `MEDIUM_SIZE_CITY` | population; select the rule set by city size |
| NS | `[ADVICE_TRIGGERS] NON_DEV_SENSITIVITY` | **tile count**; every "many tiles" test below is `count > NS` [INFERRED: key-to-slot mapping by load order] |
| LLV | `[ADVICE_TRIGGERS] LOW_LAND_VALUE` | city-average land value (same scale as the land-value map, `15-sim-growth.md` §6) |
| LAF, LWF | `[ADVICE_TRIGGERS] LOW_AIR_POLLUTION_FARMS`, `LOW_WATER_POLLUTION_FARMS` | city air / water pollution index (0–255) |
| CC, IC | `[GROWTH_CAP_LIMITS] COMMERCIAL_CAP_PROBLEM`, `INDUSTRIAL_CAP_PROBLEM` | compared with the commercial / industrial demand-cap measure (0–255, `15-sim-growth.md` §3) |
| RW | `[DEMAND_RESPONSIVENESS] RESPONSIVENESS_IN_MONTHS` | months of demand history (0–12) |
| RP, CP | `[DEMAND_CUTOFFS] RESIDENTIAL_DEMAND_POSITIVE`, `COMMERCIAL_DEMAND_POSITIVE` | demand (valve) value; growth petitions (3.11.1) |
| HEQ, HJR | `[ADVICE_TRIGGERS] MIN_HI_TECH_EQ`, `HI_TECH_JOB_RATIO` | EQ points; percent (clean-industry share of industrial buildings) |
| PR, PA | `[ADVICE_TRIGGERS] PARK_RATIO`, `PARK_AURA_CUTOFF` | ratio (non-park to park); mayor-rating/aura value (signed) |
| — | `*_DEMAND_NEGATIVE`, `INDUSTRIAL_DEMAND_POSITIVE` | loaded, never referenced (dead keys) [STATIC] |

Metrics gathered each cycle:

* N population; Y year; dR, dC, dI = current R/C/I demand (valve values 3000/5000/8000);
  aR, aC, aI = *accumulated demand* = current demand + the sum of the last RW monthly demand
  samples of that zone family (the advisor keeps its own 12-entry ring of samples [OPEN: whether a sample is taken per advisor cycle (weekly) or per month]).
* zR, zC, zI = number of zoned tiles of zone types 1–3, 5–7, 9–11; zSea, zAir, zFill = tiles of
  zone 15, 14, 17 (`15-sim-growth.md` §1).
* x(z, c) = the zone developers' **previous-pass tallies**: the resultPrev counts of the zone
  record (`10-save-format.md` §6.4) for result code c, summed over the zone types of family z
  [STATIC]. This is not a per-tile grid.
* Building counts by agent type: airport (12310), seaport (12330), farms (9300), clean industry
  (8700 + 8900 + 9100), medium-density commercial (5700 + 6100), high-density commercial
  (6300 + 6700).
* LV = city-average land value; AP, WP = air / water pollution index.
* capC, capI = `capPct` (0–100) of valves **5000** and **8000**: the family valves, whose cap the
  cap rule of `15-sim-growth.md` §3.2 sets from 5001 / 8001 [STATIC]. Correction: the previous
  text said 0–255 [INFERRED].
* Flags A and B (arguments 3 and 4) are answered by the **transportation advisor**. They are its
  technology flags of 3.10: index 1 subway, 2 bus stops, 3 airport, 4 highway. So A = "airport
  available" and B = "highway available" [STATIC].
* Garbage: Lf = number of registered landfill tiles × 32767; Lr = the landfill store's `Dlast`
  (room left, `16-sim-utilities.md` §4.4) [STATIC].

Rules (each line raises its topic when the condition holds):

| Topic | Raise form | Condition |
|---|---|---|
| `0x83` | key `0xE2A4935D` | capC > CC, no airport building, commercial capped "needs connections" (cap cause 3 reported by the demand-cap model) |
| `0x2B` | key `0xE2A4935D` | capC > CC, airport count ≥ 1, airport capacity fully used [INFERRED: two transport counters equal], cap cause 4 |
| `0x84` | key `0xE2A4935D` | capC > CC, airport count ≥ 1, cap cause 4, otherwise |
| `0x82` | key `0xA2A4933D` | capI > IC, 1 ≤ K ≤ 3, Kt = 0 and no seaport (see note) |
| `0xE5` | key `0xA2A4933D` | capI > IC and Kt < K (see note) |
| `0x86` | key `0xA2A4933D` | capI > IC, otherwise, ≥ 1 seaport and K ≠ 4 |
| `0xBF` | plain | N < SS; zR > NS; dR < 0 and aR < 0; zC < NS or zI < NS |
| `0xD7` | plain | N < SS; (zC > NS and aC < 0) or (zI > NS and aI < 0); dR > 0; zR < NS |
| `0xC9` | plain | N < SS; zR < NS; dR > 0; x(C,3) > NS or x(I,3) > NS |
| `0xE4` | plain | N < SS; x(R,3) > NS; dR < 0; zC < NS or zI < NS |
| `0xDE` | plain | N < SS; AP < LAF; WP < LWF; farm count < NS |
| `0xD8` | plain | SS ≤ N < MS and, for any family f ∈ {R, C, I}: zf > NS, df > 0 and x(f,14)+x(f,15)+x(f,16) > NS |
| `0xD1` | event, L = 4 | any family with x(f,4) > NS (abandoned: no power) |
| `0xD2` | event, L = 4 | any family with x(f,5) > NS (abandoned: no water) |
| `0xD3` | event, L = 4 | any family with x(f,6) > NS (abandoned: no access / unfit) |
| `0xD9` | event, L = 6 | x(R,1)+x(C,1)+x(I,1) > 0 (blocked tiles abandoned, e.g. radiation) |
| `0xDA` | event, L = 6 | x(R,12)+x(I,12) > 0 (land value too high to develop) |
| `0xC8` | persistent, L = 8 | clean-industry count > 0 |
| `0xDC` | persistent, L = 8 | medium-density commercial count > 0 |
| `0xDD` | persistent, L = 8 | high-density commercial count > 0 |
| `0xDB` | plain | LV < LLV |
| `0xE2` | plain | zSea > 5 and seaport tiles with result codes 9, 12, 14, 15, 16 or 17 > 5 |
| `0xE0` | plain | zFill ≠ 0 and landfill tiles with result code 16 (no access) ≠ 0 |
| `0xE1` | plain | otherwise (previous row false): Lf > 0 and Lr = 0 (landfill exists and has no room left) [STATIC] |

All size-dependent rows (`0xBF`…`0xD8`) are skipped when N ≥ MS. All comparisons are strict.
"Event" = raise(topic, 0, L, P=1, E=1); "persistent" = raise(topic, 0, L, P=1) (section 3.4).
The commercial/industrial cap topics share their key groups, so only one of each is listed.
Post-update rules (rewards) are in section 6; growth petitions in section 3.11.1.

Notes on the cap rows: K = number of neighbour sides that have a neighbour city (0–4), Kr = sides
connected by road/highway, Kt = sides connected by rail [INFERRED from topic meaning]. Exact
conditions: `0x83` when no airport building and cap-cause flag A is set; with ≥ 1 airport building:
`0x2B` if Kr = K and cap-cause flag B is set, else `0x84` if flag B is set, else nothing. Flags A
and B are the transportation advisor's airport-available and highway-available flags (above)
[STATIC]. Industrial: if 1 ≤ K ≤ 3, Kt = 0 and no seaport → `0x82`; else if Kt < K → `0xE5`; else if
K ≠ 4 and ≥ 1 seaport → `0x86`.

#### 3.11.1 City-planning growth petitions (post-update) [STATIC]

Requested as plain petitions with cool-down L (cycles) via the petitioner manager (section 5.3).
"Not pending" = the petition is not already in the pending list. r(n) as in section 4.7.

| Petition | Condition | Chance | L |
|---|---|---|---|
| `0x14` | topic `0x2B` or `0x84` currently listed and capC < 40 | always | 48 |
| `0x1B` | no airport building and capC > CC | always | 48 |
| `0xDD` (farmers' market) | farm count > 0, ordinance `0x62F6E82B` available, not pending | r(100) < 2 | 120 |
| `0x29` / `0x2A` | zC > NS, dC ≥ CP, aC ≥ CP, N > 500, neither pending; then r(2): 0 → request **both** `0x29` and `0x2A` (fall-through in the original), 1 → `0x2A` only | always | 12 |
| `0x2F` | zR > NS, dR ≥ RP, aR ≥ RP, not pending | always | 12 |
| `0x27` / `0x28` | city EQ > HEQ and HJR × (industrial building count) < 100 × (clean-industry count), neither pending, r(100) < 3; then r(2) as for `0x29`/`0x2A` (0 → both, 1 → `0x28`) | 3% | 30 |
| `0x49` | PR × Pk < B − F − Pk and aura < PA, not pending | r(100) < 2 | 120 |
| `0x4A` | (map tiles ÷ 2) < B − 2·Pk − F (unsigned 32-bit comparison), not pending | r(100) < 2 | 120 |

Pk = park measure = n(14311) + 9·n(14312) + 16·n(14313) + 4·n(14314) + 4·n(14315) + n(14316), where
n(t) is the number of buildings of agent type t (the multipliers are hard-coded; the agent types
are the park/plaza family, `13-buildings-occupants.md`). F = farm-building count. B = the occupant manager's total occupant count plus the network
subsystem's running piece counts for road, highway, rail and power line (`12-networks.md` §10:
flags 0x8000, 0x10000, 0x20000, 0x400000) [STATIC]. Correction: these are network counts, not
flora/automata counts. Map
tiles = map width × height. Draws happen only when the earlier conditions hold (short-circuit), so
the shared-LCG sequence depends on evaluation order: rows are evaluated top to bottom.

Reward-related metrics (section 6.2), now resolved [STATIC]: the "airport measure" is the number
of airport buildings (agent type 12310); the national-park / skating-rink "park measure" is the
count of agent type 14310 buildings; the theme-park "city flag" is ordinance `0x62F6E7CF`
(tourism promotion) enacted; the university's extra condition is ordinance `0xE0D07233`
(pro-reading campaign) enacted (not a college).

### 3.12 Health & education advisor rules [STATIC; metric identities INFERRED where noted]

These rules are ordinary direct topic raises (same mechanism as the other advisors); they
run every cycle like the others. Thresholds (`Advisor-Demographics.ini [ADVICE_TRIGGERS]`):
`HOSPITAL_POPULATION_MINIMUM` (HP), `SCHOOL_POPULATION_MINIMUM` (SP), `LIBRARY_POPULATION_MINIMUM`
(LP), `MUSEUM_POPULATION_MINIMUM` (MP), `LE_AND_EQ_POPULATION_MINIMUM` (QP),
`STADIUM_GROWTH_CUTOFF` (STP), `RESIDENTIAL_GROWTH_CAP` (RGC) — all populations;
`LOW_LIFE_EXPECTANCY`/`HIGH_LIFE_EXPECTANCY` (LL, LH, years); `LOW_EQ_CUTOFF`,
`AVERAGE_EQ_CUTOFF`, `HIGH_EQ_CUTOFF` (EL, EA, EH, EQ points); `HIGH_DREARY_RATING` (DH);
`[PETITIONER_GENERATION] HOSPITAL_RATIO` (HR, population). The zoo/marina, aura and dreary keys
are used by the petitions (3.12.1). The `[FUNDING_RATE_CUTOFFS]` keys are read when the advisor
loads its INI, but no rule uses them [STATIC].

Metrics: N population; EQ = Q(20, 65) and LE = the same unweighted mean of `LE[a]` over ages
20–65 with `p[a] > 0`, rounded to a byte (`20-sim-population.md` §4.5) [STATIC]. These are the
working-age means, **not** the city chart values over ages 0–89; nH, nS, nC = number of buildings of agent type 13310
(hospital/clinic), 13010 (school), 13873 (college); nSt = number of agent type 14600 (stadium);
fH, fE = health and education funding percent; dreary rating Dr.

**Dreary rating Dr** (0–100) is the residential amenity usage in percent: the `capPct` that the
cap rule of `15-sim-growth.md` §3.2 computes from valve 3001's `Slast`/`Dlast` at the residential
layer's last month-end (0 if `D` = 0; 100 if `S` < `D`; else ⌊100·D/S⌋, capped at 100) [STATIC].
It equals the saved `capPercent` of valve 3000 (`10-save-format.md` §6.11) [INFERRED: same value,
same month-end; DATA test vector: Madison 3001 S 739 500, D 150 984 → 20]. A high Dr means the
parks/culture amenity supply is nearly used up.

The per-service values below all come from the residential (population) layer. They are taken
at the start of each advisor cycle (weekly, 3.6) [STATIC]. Except for W_s they are that layer's
results from its last monthly update (`20-sim-population.md` §4.4, §5.1–§5.2). They are the same fields that
the residential record stores (`10-save-format.md` §6.11) [STATIC]:

| Value | Health | School | College |
|---|---|---|---|
| strike flag | healthStrike | schoolStrike | collegeStrike |
| F_s | healthRating (0–4) | schoolRating (0–4) | collegeRating (0–4) |
| D_s | patients | schoolAgePop (ages 6–18) | collegeAgePop (ages 19–24) |
| C_s | hospitalBeds | schoolDesks | collegeDesks |
| W_s | ⌊r_health / 100⌋ | ⌊r_school / 100⌋ | ⌊r_college / 100⌋ |

* **F_s is the service rating** of `20-sim-population.md` §5.2 and §4.4 (health rating; school and
  college ratings), not a funding class. The rule text keeps the names "adequate" (3) and
  "high" (4), but the test is on the rating value [STATIC].
* **W_s** is the strike risk r_s of `20-sim-population.md` §5.4 (0–10 000), divided by 100 with
  integer division. It is evaluated when the advisor reads it, from the same inputs as the monthly
  strike machine: current rating, the department funding last applied, and whether a facility of
  that kind exists [STATIC; that the funding input is the last applied value is INFERRED]. While a
  strike is active W_s = 100. Otherwise, W_s > 15 holds exactly when rating < `MinAcceptable…Rating`,
  facilities exist, and funding ≤ `MinAcceptable…Funding` − 40 (because (MinF − f)² ≥ 1600)
  [STATIC]. W_s is not a month counter.
* CL = libraryCapacity and CM = museumCapacity (Σ `PopServed`, same record) [STATIC].
* The residential layer also exposes the teacher, professor and doctor sums. The advisor does not
  read them [STATIC].

**Health** (key `0xE2A49484`, first match):

| Condition | Topic |
|---|---|
| health workers on strike | `0x65` |
| nH > 0 and W_health > 15 | `0x66` |
| nH > 0 and D_health > ⌊3·C_health / 2⌋ and fH > 99 | `0x131` |
| nH > 0 and F_health = 4 / = 3 / otherwise | `0x69` / `0x68` / `0x67` |
| nH = 0 and N > HP / N ≤ HP | `0xBD` / `0x121` |

**Education** (key `0xA2A4947C`, first match):

| Condition | Topic |
|---|---|
| teachers on strike | `0x6A` |
| college staff on strike | `0x104` |
| nS > 0 and W_school > 15 | `0x6B` |
| nC > 0 and W_college > 15 | `0x13A` |
| nS > 0, D_school > ⌊3·C_school/2⌋, fE > 95 | `0x135` |
| nS > 0, C_school > D_school, fE < 85 | `0x134` |
| nC > 0, D_college > ⌊3·C_college/2⌋, fE > 95 | `0x139` |
| nC > 0, C_college > D_college, fE < 85 | `0x138` |
| nS > 0 and F_school = 3 | `0x6C` |
| nC > 0 and F_college = 3 | `0x137` |
| nS > 0 and F_school = 4 | `0x6D` |
| nC > 0 and F_college = 4 | `0x136` |
| nS > 0 (none of the above) | `0x6B` |
| nS = 0 and N ≤ SP and nC = 0 | `0x126` |
| nS = 0 otherwise | `0x6E` |

**Facility topics** (plain, no key, each independent):

| Condition | Topic |
|---|---|
| N > SP, D_school > C_school, F_school ≠ 4 | `0xAD` |
| N > LP and N > CL | `0xAB` |
| N > MP and N > CM | `0xAC` |
| N > HP, D_health > C_health, nH > 0 | `0xB8` |

**Life expectancy / EQ** (only if N > QP):

| Rule (key) | Condition, first match | Topic |
|---|---|---|
| LE (`0x6307D0FB`) | LE < LL and F_health ∉ {3, 4} | `0xAE` |
| | LE > LH | `0xB1` |
| EQ (`0x2307D105`) | EQ < EL and F_school ∉ {3, 4}: if `0xB3` or `0xB4` is listed, nothing; else r(2) = 0 → `0xB3`, 1 → `0xB4` | |
| | EQ < EA | `0xB5` |
| | EQ < EH | `0xB6` |
| | otherwise | `0xB7` |

**Recreation** (after the EQ rule): if N > STP and nSt = 0 → `0x27`; else if N > RGC and Dr > DH
→ `0x26`. Both are raised with key `0x2307D105` when N > QP, else with no key. Because of the
shared key, raising either deletes the listed EQ topic (original behaviour).

#### 3.12.1 Health & education petitions (post-update) [STATIC]

Rows are evaluated top to bottom; "not pending" and the r(n) draws as in 3.11.1. Draws happen only
when the preceding conditions of the row hold.

| Petition | Condition | Chance | L |
|---|---|---|---|
| `0x9C` / else `0x9B` | health workers on strike / else W_health > 15 (argument `0xA38293DE`) | always | 6 |
| `0x9E` / else `0x9D` | teachers on strike / else W_school > 15 (argument `0xA38293EF`) | always | 6 |
| *(the following four rows only if N > 200)* | | | |
| `0x05` | nH ≥ 1, F_health = 4, not pending (argument `0xC3829422`). If the draw fails, the remaining rows of this group are skipped | r(100) < 2 | 40 |
| `0x0C` | nH = 0, N > HR, not pending. If the draw fails, `0x02` is skipped | r(100) < 10 | 12 |
| `0x02` | nH > 1, C_health < D_health, F_health < 3, not pending | r(100) < 5 | 12 |
| *(the following three rows only if N > SP)* | | | |
| `0x1D` | nS = 0, not pending | r(100) < 10 | 12 |
| `0x19` | C_school < D_school, F_school ∉ {3, 4}, not pending | r(100) < 10 | 12 |
| `0x06` | D_school < C_school < 10·D_school / 7 (i.e. 7·C < 10·D), F_school ∈ {3, 4}, not pending | r(100) < 10 | 120 |
| *(the following rows only if N > 500; a = aura (mayor rating, signed), Dr = dreary rating)* | | | |
| `0x3C` | a < `CITY_AURA_LOW` and Dr > `HIGH_DREARY_RATING` | always | 18 |
| `0x39` | a < `CITY_AURA_LOW` and Dr ≤ `HIGH_DREARY_RATING`; or a ≥ `CITY_AURA_LOW`, Dr ≥ 71 and N > `CITY_AURA_DREARY_POPULATION` | always | 24 |
| `0x3B` | otherwise, a > `CITY_AURA_HIGH`, Dr < `LOW_DREARY_RATING`, not pending | r(100) < 4 | 24 (persistent request) |
| *(always)* | | | |
| `0x9A` | no building of agent type 14450 (zoo) and N > `ZOO_POPULATION_MINIMUM`, not pending | r(100) < 2 | 60 |
| `0x99` | no building of agent type 14430 (marina) and N > `MARINA_POPULATION_MINIMUM`, not pending | r(100) < 2 | 60 |

The hard-coded 71 (dreary) is a constant in the program, not an INI value. Of the
`Advisor-Demographics.ini` keys, only `[FUNDING_RATE_CUTOFFS] *` and `COLLEGE_POPULATION_MINIMUM`
are never used. They are read at load, but no rule or petition tests them [STATIC].

### 3.13 Utilities advisor rules [STATIC; metric identities INFERRED from topic meaning]

Thresholds (`Advisor-Utilities.ini [CONSUMPTION_RATE_CUTOFFS]`, percent): PCC, PCH, PCM =
`POWER_CONSUMPTION_CRITICAL/HIGH/MEDIUM`; WCM = `WATER_CONSUMPTION_MEDIUM`.
`[FUNDING_RATE_CUTOFFS]` and `WATER_CONSUMPTION_CRITICAL/HIGH` are loaded but unused by these
rules. Metrics: PU power used, PC power capacity, PD power demanded (32-bit); U_off = number of
unpowered buildings, U_all = number of buildings needing power; WU water used, WC water capacity
(64-bit); W_dry = buildings without water (needing it), W_all = buildings needing water; Nb =
number of neighbour connections/neighbours (0–4); flags DS_p, DB_p, DS_w, DB_w = a sell-power,
buy-power, sell-water, buy-water deal is active; plant and facility counts as defined below; N
population.

Facility counts used by the ageing rules [STATIC]. "Count of type T" is the number of buildings
whose agent type is T **or a descendant of T** in `[AgentTypeHierarchy]`. DATA: no city with plants
and without a buy deal ever lists `0x10E`, so the count must include descendants (no building has
the bare type 12101).

* nP = count of 12101 (power plant: 12111–12118 and waste-to-energy 12853).
* nPo, nNe = the power layer's ageing-plant and ageing-nuclear counts from its last monthly update
  (`16-sim-utilities.md` §2.6 steps 5–6). These are functional plants (lot state 1) with age ≥ their
  own *Ld*, and for nNe also nuclear (12115). Because of the original's counting defect, only
  plants not yet reached by an earlier plant's power distribution are counted.
* nW = count of 12007 (plumbing: pump 12810, water treatment 12820, desalination 12830, tower
  12840). Water treatment plants are included in nW but never in nWo.
* nWo = water facilities (pumps, towers, desalination) counted during the last monthly water pass
  (`16-sim-utilities.md` §3.3). A facility counts when it gets as far as the ageing step 6 with
  age > *Ld*. That means it is functional, not retired this month, and its tile is powered; the ring
  and pollution tests do not stop it being counted.
* nI = count of 12852 (incinerator) + count of 12853 (waste-to-energy). WTE plants are therefore
  counted in both nP and nI.
* nIo = incinerators plus WTE plants in the garbage layer's lists, counted at its last monthly
  update, whose lot state is 1 and whose age ≥ the decline age. For incinerators the decline age is
  the global `SC3Tune.ini [Incinerator] DeclineAge`, with no per-building spread (after loading:
  `MaxLifespan` 0 → 1; if `DeclineAge` > `MaxLifespan`, `DeclineAge` = ⌊`MaxLifespan`/2⌋). For WTE
  plants it is the plant's own *Ld* [INFERRED: the WTE entry uses the plant's lifespan, as for the
  power count].

| Topic (key) | Condition | Notes |
|---|---|---|
| `0x05` (none) | not DS_p, PC > 2·PU, Nb > 0, no power-line neighbour connection | connect to sell surplus |
| `0x06` (none) | not DB_p, PU > ⌊95·PC/100⌋, Nb > 0, no power-line connection | connect to buy |
| `0x07` (none) | not DS_w, WC > 2·WU (64-bit), Nb > 0, no pipe connection | |
| `0x08` (none) | not DB_w, WU > ⌊95·WC/100⌋, Nb > 0, no pipe connection | |

Power state (key `0x62A49A83`, first match):

| Condition | Topic |
|---|---|
| not DB_p and nP = 0 | `0x10E` |
| PD ≤ 0 | none |
| DB_p | `0x11` |
| PU > ⌊PCC·PC/100⌋ | if `0x0F` or `0x09` listed: none; else r(2) = 0 → `0x0F`, 1 → `0x09` |
| PU > ⌊PCH·PC/100⌋ | if `0x10` or `0x0A` listed: none; else r(2) = 0 → `0x10`, 1 → `0x0A` |
| PU > ⌊PCM·PC/100⌋ | `0x11` |
| not DS_p | `0x12` |

Other power topics: `0x0E` (key `0x038A5570`) if nP > 0, not DB_p and PD > PC. Grid coverage (key
`0x23440D97`), only if PC > PD or DB_p: U_off > U_all/2 (signed halving) → `0x13`; else U_off > 10
→ `0x14`; else if `0x13` or `0x14` is listed → event `0x15`, L = 6.

Water state (key `0x22A49AFA`):

| Condition | Topic |
|---|---|
| WC = 0: W_all > 30 | `0x16` (else none) |
| WU = WC, not DB_w: Nb < 4 and agent type 12830 (desalination) available | `0x18` |
| WU = WC, not DB_w, otherwise | `0x17` |
| WU > ⌊WC·WCM/100⌋ | `0x19` |
| otherwise | `0x1A` |

Water coverage (same key): if W_dry > 20 and (WC > WU or DB_w): 2·W_dry > W_all and W_all > 50 →
`0x1B`, else `0x1C`. Otherwise, if `0x1B` or `0x1C` is listed → event `0x1D`, L = 6.

Ageing (plain): nPo > nP/4 → `0x100`; nNe > 0 → `0xFF`; nWo > nW/4 → `0x101`; nIo > nI/2 →
`0x102` (integer division truncating toward zero).

The rule draws use a generator seeded separately at the start of each utilities evaluation (same
as 3.13.1) [STATIC; seed source OPEN]. Plant announcements from `[POWER_BUILDINGS]` /
`[WATER_BUILDINGS]` are listed in 3.15.

### 3.13.1 Utilities advisor: neighbour-deal offers (post-update) [STATIC; metric identities INFERRED]

Neighbour-deal **offers are generated here**, once per utilities-advisor cycle (section 3.6),
and presented as petitions. The deal manager (`19-sim-economy.md` §10) only computes the terms and
picks the neighbour. Deal types d as in `19-sim-economy.md` §10.1.

Metrics as in 3.13, plus garbage capacity GC and garbage produced GP (64-bit; GC =
landfill-related capacity per month [INFERRED]) and treasury T. `MINIMUM_DEAL_POPULATION` (MDP) and `LOW_TREASURY_LEVEL` (LTL, 64-bit) are
read from `Advisor-Utilities.ini [NEIGHBOR_DEAL_CONSTANTS]`. **`MIN_POWER_SELL`, `MIN_WATER_SELL`
and `MIN_GARBAGE_IMPORT` are never read**; hard-coded 1000 / 1000 / 500 are used instead [STATIC].

Each deal type has a fixed *category key* (used as the petition argument, and to find the
category's pending petition) and three alternative offer petitions:

| d | Category key | Offer petitions (r(3) = 0, 1, 2) | Offer condition (all must hold) |
|---|---|---|---|
| 1 sell power | `0x63248EA7` | `0xB3`, `0xB4`, `0xB5` | eligible neighbour; PD < ⌊4·PC/10⌋; PC − PD > 1000; N > MDP |
| 0 buy power | `0x83248FD7` | `0xAC`, `0xAD`, `0xCA` | eligible neighbour; PU ≥ ⌊8·PC/10⌋ or U_off ≥ 31 |
| 3 sell water | `0xC3249313` | `0xB9`, `0xBA`, `0xBB` | eligible neighbour; WU < ⌊4·WC/10⌋; WC − WU > 1000; N > MDP |
| 2 buy water | `0x232493D6` | `0xA5`, `0xA6`, `0xA7` | eligible neighbour; WU ≥ ⌊8·WC/10⌋ or W_dry > 30 |
| 5 import garbage | `0x232494CF` | `0xBF`, `0xC0`, `0xC1` | eligible neighbour; GP < ⌊6·GC/10⌋; GC − GP > 500; N > MDP |
| 4 export garbage | `0x63249696` | `0xC4`, `0xC5`, `0xF7` | eligible neighbour; GP ≥ ⌊9·GC/10⌋ |

Per category, each cycle, in the order of the table:

1. If a deal of this type is already active: the category's offer petition is withdrawn. For an
   active buy-power deal older than 24 months the informational petition `0xB1` is requested, for
   a buy-water deal older than 24 months `0xAA` (argument = category key); for an active
   garbage-export deal `0xC3` is withdrawn/handled the same way [INFERRED].
2. Else, if the condition fails or new offers are disabled (deal-manager state 4, e.g. scenario
   `cmdDisableNewNeighborDeals` [INFERRED]): any pending offer is cleared.
3. Else, if an offer petition of this category is already pending: if the deal manager's stored
   offer is still valid it is kept (re-raised); otherwise the offer is cleared and the petition
   withdrawn.
4. Else: draw r(3) to choose the petition variant; ask the deal manager to generate an offer of
   type d (neighbour choice and terms: `19-sim-economy.md` §10.2, §10.6); if it produced a valid
   offer, request the petition with argument = category key and L = 1 and record the offer
   (the deal list is announced as "about to change" before and "changed" after recording it, so
   that open deal windows refresh).
   The offer stays pending only while it keeps being re-raised (step 3).
5. Sell-power only: if T < LTL and the sell-power petition is visible and is not `0xB5`, the
   budget advisor additionally raises topic `0x25` (plain).

The draws here come from a generator seeded separately for each evaluation (not the shared
simulation LCG) [STATIC; seed source OPEN]. Answers to `0xA5`–`0xCA`, `0xE9`–`0xEE`, `0xF7`,
`0xF8` go to the utilities deal handler (section 5.5).

### 3.15 Other advisors

Additional per-advisor facts (rule tables are in 3.7–3.13). Each
`Advisor-*.ini` key is a threshold in the unit shown by its name (percent funding, population,
year, EQ points, life-expectancy years, pollution/crime/traffic index). Topic IDs per advisor can
be recovered from the topic headings; `AdvisorMoods.ini` lists every topic. Known non-budget
rule facts:

* **Public safety** raises disaster-report topics on disaster start and posts a triggered ticker
  item chosen at random (table below, section 4.7).
* **Utilities** owns the neighbour-deal petitioners and `[NEIGHBOR_DEAL_CONSTANTS]`
  (`LOW_TREASURY_LEVEL`, `MINIMUM_DEAL_POPULATION`; `MIN_POWER_SELL`, `MIN_WATER_SELL`,
  `MIN_GARBAGE_IMPORT` are never read, see 3.13.1), and announces new power/water plants when the year passes the `year`
  field of `[POWER_BUILDINGS]` / `[WATER_BUILDINGS]` lines (`ID=Name;year;topic` — the third field
  is the announcement topic ID, 0 = none; e.g. gas plant topic `0x1F` = 31).
* **Environment** `[GARBAGE_BUILDINGS]` has the same `ID=Name;year;topic` form (recycling
  `0x73`, incinerator `0x72`, waste-to-energy `0x74`).
* **Transportation** `[TRANSPORTATION_ADVANCEMENTS]` years announce subway (`0x63`), bus (`0x64`),
  airport (`0x87`) and highway (`0x110`) availability [STATIC, section 3.10].

## 4. News ticker

### 4.1 Ticker data files

`SC3Newscast.INI`, section `[TickerData Files]`, lists the ticker data files as repeated
`entry=<filename>` lines (shipped: `SC3TickerData.INI`, `SC3HolidaysTickerData.INI`,
`GoldTickerData.INI`). Each is loaded in order from `\Sys\` (loose file) or `SYS.PAK`. If a key occurs
again (in the same or a later file) the **first** definition is kept; however a repeated class-7
line is still appended to the random pool again (so duplicates raise its weight). [STATIC]

Each data file has section `[Sc3TickerData]` with lines:

`0xTABLE,0xID=class,R,G,B,r2,g2,b2,underline,repeats`

| Field | Meaning |
|---|---|
| `TABLE` | String table ID (Group) of the text |
| `ID` | String ID (Instance) |
| `class` | Ticker class 1–7 (queue selection, below) |
| `R,G,B` | Text colour, 0–255 each |
| `r2,g2,b2` | Parsed and ignored; the second colour is always black (0). Shipped value is always 255,0,255 |
| `underline` | 0/1. 1 draws a 1-pixel line under the text (used for items that are clickable) |
| `repeats` | How many times the item is shown; 0 = repeat until it expires |

[STATIC; DATA: all 1197 shipped lines have 9 numeric fields; class ∈ {1,2,3,4,6,7},
underline ∈ {0,1}, repeats ∈ {0,1,2,3}.]

All numbers accept decimal or `0x` hex. A key not present in any file gets defaults: class 6,
white text, black second colour, underline 0, repeats 1. [STATIC]

Every entry with class 7 is also added to the **random pool** (in file order). [STATIC]

Shipped usage by table [DATA]:

| Table | Lines | Typical class |
|---|---|---|
| `0x026DD322` petition appears | 176 | 3–4, underline 1, repeats 2 |
| `0xE27EBF4D` / `0xE27EBF70` petition outcome | 102 + 102 | 3, repeats 1–2 |
| `0x42C1ED2D` triggered news | 328 (+9 holiday) | 1–3 |
| `0x547BA060` MBE triggered news | 49 | 1 or 3 |
| `0xC25B11C3` advisor ticker | 200 (+12 Gold) | 1–6 |
| `0xC2498F0E` random filler | 247 | 7 (all) |

### 4.2 Queues

Posted items go into one of six FIFO queues indexed by class 1–6. Each queue holds at most
64 items; posting to a full queue first discards the oldest. A posted item records: its unique
item number, text, colours, underline flag (forced to 1 if the poster attached a map location),
remaining *repeats*, and its *post date* D = (month + 12·year)·30 + day, computed from the
calendar date (`14-sim-clock.md`). [STATIC] This stamp is a bookkeeping convention of the ticker
only; the calendar itself has real month lengths. Because every month counts as 30 stamp units,
the stamps are not contiguous: day 31 of a month and day 1 of the next month get the **same**
stamp, and the stamp jumps forward by 3 (or 2 in leap years) from 28 (29) February to 1 March.
Ages and expiry (below) are differences of stamps, so an item can expire up to a day early or late
compared with real days. A faithful clone keeps this quirk. [STATIC]

Expiry ages (items older than this are discarded when their queue is examined), in game days:

| Class | Queue | Expiry |
|---|---|---|
| 1 | Q1 | 1080 |
| 2 | Q2 | 1080 |
| 3 | Q3 | 90 |
| 4 | Q4 | 1080 |
| 5 | Q5 | 1080 |
| 6 | Q6 | 270 |

### 4.3 Selection of the next message

Whenever the ticker needs a new item (the previous one has scrolled far enough to make room),
with today = T, *last* = item number last shown, *run* = count of consecutive queued items shown
since the last random filler: [STATIC]

1. Purge Q1 of expired items. If Q1 is non-empty, show its head (even if equal to *last*).
2. Else purge Q2. If its head exists and is not *last*, show it.
3. Else purge Q4. If its head exists, is older than 360 days (T − D > 360) and is not *last*, show it.
4. Else purge Q3. If its head exists and is not *last*, show it.
5. Else, if *run* < 5:
   a. If Q4 contains any item younger than 360 days and Q4's head is not *last*, show Q4's head.
   b. Else purge Q5; if its head exists and is not *last*, show it.
   c. Else purge Q6; if its head exists and is not *last*, show it.
   d. Else go to step 6.
6. Show a random filler (uniformly chosen from the random pool, using the game RNG of
   `14-sim-clock.md` §8, time-seeded) and set *run* = 0.

"Show the head" means: set *last* to its item number; display it; remove it from the front of
its queue; then, if its *repeats* was 0, or *repeats* − 1 is still > 0 (decrementing it), append
a copy to the back of the same queue (with the original post date); and increment *run*.

A random filler whose string cannot be loaded is shown as a blank placeholder in white.

### 4.4 Removal and reset

* An item can be withdrawn by item number (searches all six queues; used when an advisor topic
  clears).
* Loading/starting a city clears all queues. [STATIC]

### 4.5 Scrolling and presentation

* Scroll step: 2 pixels every time more than 50 ms have elapsed since the last step (≈40 px/s
  at full frame rate). [STATIC]
* Consecutive items are separated by a 20-pixel gap. [STATIC]
* Items are drawn in their text colour; underline drawn on the last row if the underline flag is
  set. Double-byte code pages are stepped per character (2 bytes) for code pages 15 (Japanese),
  17, 18, 20 and Thai combining sequences for 19. [STATIC]
* Clicking an item that carries a map location plays a UI click sound and centres the city view
  on that location. [STATIC]

### 4.6 Cheat-code ticker responses

Typing a recognised cheat word posts a triggered-news item from table `0x42C1ED2D`, IDs
`0x325`–`0x341` (see `27-platform.md` / cheat list); the cheat list itself belongs to the
application shell. [STATIC]

### 4.7 Event ("triggered") news sources

Triggered news items are posted directly by the subsystem that detects the event, keyed by
(table, message ID); presentation comes from the ticker data files. Known posters [STATIC]:

| Source | Table / IDs |
|---|---|
| Disasters (public-safety advisor, on disaster start; `21-disasters.md` for disaster type IDs) | see table below |
| Disaster progress (`SIMDSTR`) | `0x42C1ED2D` / `0x547BA060` stage messages chosen inside each disaster's state machine (`21-disasters.md`) [OPEN: per-stage list] |
| Building completed (`SIMGEOM`) | `0x42C1ED2D`, table 4.7.1 |
| City start / load (`SIMCITY`) | `0x42C1ED2D`, section 4.7.2 |
| Cheat words (application shell) | `0x42C1ED2D` `0x325`–`0x341` |
| City anniversaries (city age 10, 25, 50, 100, 250, 500, 1000, 2000 years) | `0x42C1ED2D` `0x1F4`–`0x1FB` respectively (`01-architecture.md`) |
| Advisors | `0xC25B11C3` topic IDs (section 3.5) |
| Petitioners | `0x026DD322`, `0xE27EBF4D`, `0xE27EBF70` and Holidays equivalents (section 5) |

Disaster start → ticker item (r(n) = uniform integer 0…n−1 from the shared LCG):

| Disaster type ID | Table | Message ID |
|---|---|---|
| `0x22963800` | `0x547BA060` | `0x67` + r(3) |
| `0x02963821` | `0x42C1ED2D` | `0x25D` + r(3) |
| `0x13873254` | `0x547BA060` | `0x73` + r(3) |
| `0x13873255` | `0x547BA060` | `0x7E` + r(3) |
| `0x13873256` | `0x547BA060` | `0x8D` + r(2) |
| `0x4296380E` | `0x42C1ED2D` | `0x26C` + r(3) |
| `0x42963812` | `0x42C1ED2D` | `0x24B` + r(3) |
| `0xE2963828` | `0x42C1ED2D` | `0x234` + r(3) |
| `0x621CDA33` (fire) | `0x42C1ED2D` | if a building occupies the fire's tile: `0x27C` + r(4); else `0x280` + r(2) |

The item carries the disaster location (clickable). If the location is outside the current view
(or for one disaster kind when fewer than 3 are active) the view-jump/alert UI is triggered as
well [STATIC; UI detail OPEN].

#### 4.7.1 Building news [STATIC; trigger INFERRED: a building of the agent type is added to the city]

When a building of one of these agent types appears, the ticker item (`0x42C1ED2D`, ID) is posted.
"first" = only when the city's count of that agent type is exactly 1 after the addition (else no
item); "10th" = count exactly 10.

| Agent type | Message ID | | Agent type | Message ID |
|---|---|---|---|---|
| 1001 | `0x2A9` | | 13010 (first) | `0x2B6` |
| 13872 | `0x2AF` | | | |
| 1002 | `0x2B4` | | 13130 (first) | `0x2BA` |
| 10110 | `0x2A4` | | 13310 (first) | `0x2B8` |
| 10120 | `0x2A5` | | 13871 | `0x2B2` |
| 11001 | `0x2AB` | | 13873 (first) | `0x2B7` |
| 11002 | `0x2AA` | | 13874 | `0x2B0` |
| 12210 | first → `0x20A`, 10th → `0x20B` | | 14003 | `0x2AC` |
| 12350 | `0x2B5` | | 14170 | `0x2B1` |
| 12810 (first) | `0x2BB` | | 14312 | `0x29D` |
| 12820 | `0x2BE` | | 14313 | `0x29E` |
| 12830 | `0x2BD` | | 14314 | `0x29F` |
| 12840 | `0x2BC` | | 14325 | `0x2B3` |
| 12851 (first) | `0x2C0` | | 14370 | `0x2AE` |
| 12852 (first) | `0x2BF` | | 14430 | `0x2A1` |
| 13002 (first) | `0x2B9` | | 14450 | `0x2A0` |
| 14550 | `0x2AD` | | 14600 | `0x2A2` |
| 19001 | `0x2A6` | | 19002 | `0x2A8` |
| 19003 | `0x2A3` | | 19004 | `0x2A7` |
| 20001 (Holidays) | r(3): `0x3E8`, `0x3EB`, `0x3EC` | | 20002 (Holidays) | r(3): `0x3EE`, `0x3EF`, `0x3F0` |

Any other agent type that belongs to the landmark family (18000, `13-buildings-occupants.md`)
posts `0x1FC + r(12)` (`0x1FC`–`0x207`). Other types post nothing. r(n) here is drawn from the
building subsystem's own generator [STATIC]. Decimal agent types are as in `13-buildings-occupants.md`
(e.g. 12810 = water pump, 12840 = water tower inferred from the utilities INI lists).

#### 4.7.2 City start / load news [STATIC; mode meanings INFERRED]

When a city starts (or is loaded), the city subsystem posts and keeps references (so it can
withdraw them later) to:

* in game mode 3: one random item `0x2ED`–`0x2F2` (uniform), then `0x2F9`;
* in game mode 7: all of `0x64`–`0x68`, then one item per advisor present, `0x69`, `0x6A`, … in
  advisor order, and finally every remaining ID up to `0x72`;
* otherwise: `0x2F9`.

`0x2F9` is also posted when the simulation is started for the first time in a session and the
city's list of start-up items is empty. Game-mode values are those of `01-architecture.md`
[OPEN: mode 3 / 7 names].

## 5. Petitioners

A *petitioner* is a citizen visiting the mayor with a numbered petition (petitioner ID, the
string Instance in the petitioner tables). Some petitions are informational (one acknowledge
button); others have accept/decline buttons (those with entries in the Button1/Button2 tables).

### 5.1 PetitionerManager.ini

| Section | Content |
|---|---|
| `[PETITIONER_ORDINANCE_TRIGGER_MAPPINGS]` | Thresholds for ordinance petitions (table in 5.4). `*_YEAR` keys are calendar years; `*_AURA` keys are city-aura values; `*_POP`/`*_POPULATION` keys are populations; `LE_LEVEL_FOR_CLINIC` is life expectancy in years. |
| `[PETITIONER_ADVICE_SIZE] NumberOfPetitionerMessages` | Size of the advice mapping (shipped 210). |
| `[PETITIONER_ADVICE_MAPPING]` | `n=<advisorId>;` for n = 1…NumberOfPetitionerMessages: which advisor's portrait/voice delivers the advisor comment on petition n (0 = none). Shipped non-zero: 16 → Environment, 24 and 26 → Utilities, 149 → Public safety, 206 and 210 → City planning. |
| `[PETITIONER_STRINGS_<CAT>]` (11 sections) | Each lists one or more `NAME=0xTABLE` lines: the string tables for that category, searched in file order. CAT ∈ ADVICE, BUTTON1, BUTTON2, HEADING, MESSAGE, NAME, REACTION1, REACTION2, TICKER, TICKER_X, TICKER_Y. |
| `[PetitionerExtraInfo Filenames]` | `entry=<file>` list (shipped `PetitionerExtraInfo.INI`, `HolidaysPetitionerExtraInfo.INI`). |

For every text category the program takes the **first table in the section that contains the
petitioner ID** (so base and Holidays IDs can share categories without colliding). [STATIC]

### 5.2 PetitionerExtraInfo files

Section `[PetitionerExtraInfo]`, lines `0x%08X=<a>,<b>` keyed by petitioner ID [STATIC, DATA:
182 + 3 shipped lines]:

* `a` — portrait image: resource (Type `0x62B9DA24`, Group `0x52DB60A0`, Instance `a`) in
  `Res/UI/Shared/52DB60A0_Advisors.ixf`. It also selects the petitioner's *character type*, which
  chooses the voice sound played on accept/decline (sound IDs `0x9B`–`0xA6`, see
  `06-audio.md`; table in 5.5) [STATIC].
* `b` — optional second image (same Type/Group, Instance `b`); 0 = none.

Files are searched in `[PetitionerExtraInfo Filenames]` order; the first file containing the key
wins. Missing key → no images.

### 5.3 Requests, pending list and cool-down

Advisors (and the petitioner manager itself) *request* petitions. A request (petition ID,
argument, flags P/E, limit L) is ignored if the same petition is already pending — in that case
the pending instance is marked re-raised (R). Otherwise a pending instance is appended with
C = 0, H = 0. Each petitioner-manager cycle (weekly, Tuesday; same mechanism as advisors, section 3.6): metrics are gathered,
ordinance petitions are generated (5.4), then each pending instance is updated [STATIC]:

1. P set: if L = 0 or C < L then C += 1, else H := 1 (hidden, ticker withdrawn).
2. Else E set: if C < L then C += 1, else delete.
3. Else R set: if H set: if L = 0 or C < L then C += 1, else delete. (Visible: unchanged.)
4. Else: delete (ticker withdrawn).

Then all R flags are cleared and the forced petitions (cheat list) are re-requested. Answering a
petition hides it (H := 1); it then stays in the list as a cool-down marker for L cycles while its
trigger still holds, which prevents the same petition from re-appearing. Ordinance petitions use
L = 120 cycles. [STATIC] When a petition becomes pending, its ticker line (category TICKER, table
`0x026DD322`/`0x649B17DB`) is posted with the petition's map location if any.

**Presentation (petitioner panel)** [STATIC; ordering INFERRED]. There is no "next petition"
selection: at the end of each petitioner-manager cycle (weekly, section 3.6) the manager broadcasts
a "petitions changed" notification. The petitioner panel and its petitioner list
(`09-ui-screens.md`) react by fetching from the manager the complete
list of presentable petitions (pending and not hidden), in pending-list order, and reconciling its
row of petitioner portraits with it:

1. remember the tokens of the entries currently shown;
2. rebuild the list from the manager; an entry without a token gets token = C-runtime `rand()` + 1;
3. an entry whose token was already shown is updated in place; any other entry is added;
4. previously shown entries not present any more are removed.

So **all** visible pending petitions are shown at once and the player chooses which petitioner to
open. Opening one sends an "open petition" request (the petition dialog is built from the petition's
string categories, section 5.2); answering sends a "petition answered" notification, which records the button result
and leads to the response dispatch of 5.5. The list therefore changes only once per week (plus
immediately after an answer), and a petition stays on screen while it remains pending and visible
(section 5.3 update rules).

### 5.4 Ordinance petitions [STATIC]

Generated only when population ≥ 100. Each row is evaluated every cycle; if the preconditions
hold and the petition is not already pending, it is requested with probability 1/100
(uniform draw 0–99 equal to 0, shared LCG). "Avail." = ordinance available; "on" = enacted.
Year comparisons are strict (current year > key). Accept enacts (=1) or repeals (=0) the
ordinance; decline does nothing to the ordinance. Ordinance IDs as in `19-sim-economy.md` §9.5.

| Petition | Preconditions | Accept action |
|---|---|---|
| `0x69` | N > `POPULATION_FOR_GAMBLING`, gambling avail., not on, aura < `MAX_GAMBLING_AURA` | `0xA0D07129` = 1 |
| `0x68` | N > `POPULATION_FOR_GAMBLING`, gambling avail. and on | `0xA0D07129` = 0 |
| `0x6A` | aura < `PRO_READING_AURA`, N > `PRO_READING_POP`, reading avail. not on | `0xE0D07233` = 1 |
| `0x63` | smoke detectors avail. not on | `0x62BF1DAA` = 1 |
| `0x64` | neighbourhood watch avail. not on | `0x62BF1DB9` = 1 |
| `0x61` / `0x62` | CPR avail.; not on → `0x61`, on → `0x62` | `0x00D0723D` = 1 / = 0 |
| `0x6B` / `0x6D` | smoking ban avail.; not on → `0x6B`, on → `0x6D` | `0xE0D07239` = 1 / = 0 |
| `0x6E` | life expectancy < `LE_LEVEL_FOR_CLINIC`, clinics avail. not on | `0x40D07236` = 1 |
| `0x71` | shelters avail. not on, N > `HOMELESS_SHELTER_POPULATION` | `0x20D07175` = 1 |
| `0x73` | year > `SHUTTLE_SERVICE_YEAR`, shuttle avail. not on | `0xA2BF1DE5` = 1 |
| `0x76` / `0x75` | year > `POWER_CONSERVATION_YEAR`, avail.; not on → `0x76`, on → `0x75` | `0x82B9999B` = 1 / = 0 |
| `0x7F` | year > `NUCLEAR_FREE_YEAR`, nuclear-free avail. not on | `0xE0D0722E` = 1 |
| `0x80` / `0x81` | year > `PARKING_FINES_YEAR`, avail.; not on → `0x80`, on → `0x81` | `0x015B4D1E` = 1 / = 0 |
| `0xD6` | year > `ELECTRONICS_JOB_FAIR_YEAR`, avail. not on | `0xA2F6E7F9` = 1 |
| `0xDC` | year > `WATER_METERS_YEAR` | `0xC2F90EB1` = 1 |
| `0xDE` | year > `TIRE_RECYCLING_YEAR` | `0xC2F6E81F` = 1 |
| `0xDF` | year > `TOURIST_PROMOTION_YEAR` | `0x62F6E7CF` = 1 |
| `0xE0` | year > `CONSERVATION_CORPS_YEAR` | `0xA2F6E7DA` = 1 |
| `0xE1` | year > `PAPERWORK_REDUCTION_YEAR` | `0x22F6E81B` = 1 |
| `0xE2` | year > `PUBLIC_ACCESS_TV_YEAR` | `0xC2F6E804` = 1 |
| `0xE4` | year > `BACKYARD_COMPOST_YEAR` | `0x22F6E814` = 1 |
| `0xE5` | year > `IND_WASTE_DISPOSAL_YEAR` | `0xA2F917CF` = 1 |
| `0xF1` | year > `AEROSPACE_TAX_YEAR` | `0x22F6E7F4` = 1 |
| `0xF3` | year > `CLEAN_INDUSTRY_ASSOC_YEAR` | `0xE2F6E7EA` = 1 |
| `0xF4` | quake retrofit avail. not on, plus a disaster-history condition [OPEN] | `0xC2BF1DD1` = 1 |
| `0xF5` | year > `BIOTECH_TAX_INCENTIVE_YEAR` | `0xE2F6E7FE` = 1 |
| `0xF6` | year > `STAIRWELL_LIGHTING_YEAR` | `0x82E42E30` = 1 |

(For rows `0xDC`–`0xF6` the ordinance must also be available and not enacted.)

Further ordinance petitions are requested by the advisors (thresholds in their
`[PETITIONER_GENERATION]` sections, e.g. `CARPOOL_TRIGGER_LEVEL/YEAR`, `CROSSING_GUARD_*`,
`ALTERNATE_DAY_DRIVING_*`, `BUS_STOP_TRIGGER_LEVEL`, `WATER_CONSERVATION_YEAR`,
`POLICE_STATION_RATIO`, `HOSPITAL_RATIO`) [STATIC: keys; exact rules OPEN]. Their accept actions:

| Petition | Accept action | Petition | Accept action |
|---|---|---|---|
| `0x66` | `0xC2BF1DC5` = 1 (curfew) | `0x67` | `0xC2BF1DC5` = 0 |
| `0x6F` | `0x815B4CEF` = 1 (youth sports) | `0x70` | `0x815B4CEF` = 0 |
| `0x72` | `0xA2BF1DDC` = 1 (carpool) | `0x74` | `0xC2BF1DEF` = 1 (crossing guards) |
| `0x77` | `0x02BF1DFA` = 1 (water conservation) | `0x78` | `0x02BF1DFA` = 0 |
| `0x7B` | `0xA2BF1E43` = 1 (clean air) | `0x7C` | `0xA2BF1E43` = 0 |
| `0x85` | `0x42BF1E18` = 1 (trash presort) | `0x86` | `0xC2BF1E04` = 1 (car smogging) |
| `0xD4` | `0x62F6E808` = 1 (lawn chemicals) | `0xD8` | `0xE2BF1E2A` = 1 (alternate-day driving) |
| `0xDD` | `0x62F6E82B` = 1 (farmers' market) | `0xF2` | `0x22F6E80C` = 1 (landfill gas) |

### 5.5 Response dispatch [STATIC]

On answer, the program (1) records the answer and posts the outcome ticker line — category
TICKER_X (`0xE27EBF4D`/`0x549B17DF`) after accept, TICKER_Y (`0xE27EBF70`/`0x049B17E2`) after
decline, if the string exists; (2) plays the petitioner's voice reaction; (3) applies the effect by
petition ID:

| Petition IDs | Effect handled by |
|---|---|
| `0x01`, `0x04`, `0x07`, `0x0A`, `0x84`, `0x88`, `0x8A`–`0x90`, `0x94`, `0x96`, `0x97`, `0xCD`, `0x12C`, `0x12F` | City planner: reward/landmark gift — accept makes the reward building available to place (section 6) |
| `0x10`, `0x11`, `0x12`, `0x95`, `0xCE`, `0xD2` | Budget: business deals (`19-sim-economy.md` §11) |
| `0x9B`–`0x9E` | Strikes, accept only: department funding ← 110 % if it is below 110 %. `0x9B`/`0x9C` → health, `0x9D`/`0x9E` → education [STATIC] |
| `0x9F`, `0xA0`, `0xA2`, `0xCC` | Accept only: funding ← 110 % if below. `0x9F`/`0xA0` → police, `0xA2`/`0xCC` → fire [STATIC] |
| `0xA3`, `0xA4` | Accept only: transit funding ← 110 % if below. Then, if the transit strike state is not "normal", it is set to **striking** with counter 0 and the "strike started" notification is posted (`18-sim-transport.md` §8.5). So accepting while a strike is only threatened starts it (original behaviour) [STATIC] |
| `0xA5`–`0xCA` (deal IDs), `0xE9`–`0xEE`, `0xF7`, `0xF8` | Utilities: neighbour deals (`19-sim-economy.md` §10) |
| ordinance petitions | table 5.4 |
| `0xD7` | accept: treasury +100000, nothing else; decline: sets the "refused Vinnie" flag, whose only reader is the `zyxwvu` cheat (`27-platform.md`) [STATIC]. Petition 0xD7 is raised only by the `call cousin Vinnie` cheat. Retyping the cheat while it is pending only re-marks it. Once it has been answered, or has gone unanswered for its 6-cycle limit, it stays hidden for the rest of the session [STATIC; not tested in play] |
| `0x12D` | accept: starts the holiday parade (automata, `26-automata.md`) |
| all others | informational only |

Declining any of the strike petitions has no effect [STATIC]. The ticker line and voice still
play.

**Voice reaction** [STATIC]. Not played when the petition goes unanswered (answer 0). For
neighbour-deal term petitions `0xE9`–`0xEE` the sound is fixed: `0x9B` on accept, `0xA4` on any
other answer. Otherwise, with t = the petitioner's character type (the `a` value of its
PetitionerExtraInfo line, 5.2):

| t | Accept | Decline |
|---|---|---|
| 2, 3 | `0xA4` | `0x9C` |
| 4 | `0xA2` | `0x9E` |
| 5 | `0xA0` | `0x9E` |
| 6 | `0xA3` | `0x9D` |
| 7 | `0xA4` | `0x9E` |
| 8 | `0x9F` | `0x9D` |
| 9, 10 | `0xA6` | `0x9D` |
| 11 | `0xA5` | `0x9C` |
| 12 | `0xA2` | `0x9E` |
| 13 | `0xA6` | `0x9D` |
| 14 | `0xA4` | `0x9C` |
| 15 | `0xA1` | `0x9D` |
| 16 | `0xA6` | `0x9D` |
| 17 | `0xA4` | `0x9C` |
| 18 | `0xA4` | `0x9B` |
| other (incl. no line) | none | none |

## 6. Reward and business-deal buildings

Rewards are owned by the **city-planning advisor** and delivered through petitioners.

### 6.1 `Advisor-CityPlanner.ini` lists [STATIC, DATA]

`[REWARD_BUILDINGS]` and `[BUSINESS_DEAL_BUILDINGS]` lines:
`<buildingType>=<name>;<minPopulation>;<minYear>;<petitionId>;<special>`

| Field | Meaning |
|---|---|
| key | Building (occupant agent) type, decimal (e.g. 10110 = mayor's house) |
| name | Ignored (label; may contain spaces/tabs) |
| minPopulation | City population required (0 = none) |
| minYear | Calendar year required (0 = none) |
| petitionId | Petitioner ID that announces/offers the reward (decimal) |
| special | 0 = population/year rule only; 1 = an extra hard-coded condition applies (6.2) |

Entries whose building type is unknown to the building catalogue are dropped. Business-deal
entries are loaded into the same list but marked as deals and are skipped by the reward logic
(they are offered by the budget advisor, `19-sim-economy.md` §11).

`[REWARD_BUILDINGS_2]` holds the thresholds of the special conditions:
`PARKS_NEEDED_FOR_NATIONAL_PARK`, `LE_NEEDED_FOR_MEDICAL_CENTER` (years),
`EQ_NEEDED_FOR_SCIENCE_CENTER`, `EQ_NEEDED_FOR_UNIVERSITY` (EQ points),
`FUNDS_NEEDED_FOR_STOCK_EXCHANGE` (simoleons, 64-bit), `AIRPORT_NEEDED_FOR_SPACEPORT`.

### 6.2 Offer rule (each city-planner cycle, post-update) [STATIC]

Each reward entry has two state flags: **available** (the reward can be placed from the reward
menu; once set it stays set) and **owned** (the building already exists [INFERRED]). For every
entry that is not a deal and not owned, with N = population, Y = current year:

* Base condition: N ≥ minPopulation and Y ≥ minYear.
* If special = 1, the base condition is AND-ed with (strict comparisons):

| Building type | Petition | Extra condition |
|---|---|---|
| 1002 stock exchange | `0x0A` | treasury > `FUNDS_NEEDED_FOR_STOCK_EXCHANGE` |
| 11001 defense contractor | `0x8F` | the military-base reward (11002) is owned |
| 12350 spaceport | `0x96` | number of airport buildings (agent type 12310) > `AIRPORT_NEEDED_FOR_SPACEPORT` |
| 13871 university | `0x97` | city EQ > `EQ_NEEDED_FOR_UNIVERSITY` and ordinance `0xE0D07233` (pro-reading) enacted |
| 13872 medical research | `0x8E` | life expectancy > `LE_NEEDED_FOR_MEDICAL_CENTER` |
| 13874 science/R&D centre | `0x84` | city EQ > `EQ_NEEDED_FOR_SCIENCE_CENTER` |
| 14003 national park | `0x88` | park measure PM > `PARKS_NEEDED_FOR_NATIONAL_PARK` (PM defined below; CORRECTION: not a count of agent type 14310); the population test uses a different population measure [OPEN] |
| 14325 theme park | `0x8D` | number of airport buildings (12310) > 5 and ordinance `0x62F6E7CF` (tourism promotion) enacted |
| 14550 lighthouse | `0x8B` | population test uses the same alternative measure as national park [OPEN: likely coastal/seaport related] |
| 20001 haunted house (Holidays) | `0x12C` | Holidays building set enabled; calendar month m (1 = January) with 6 < m < 10, i.e. July–September [STATIC; CORRECTION of the earlier 0-based reading]; not offered when the player's **country** (`27-platform.md` §1) is 30 Italy, 31 Japan, 32 Korea, 34 Netherlands or 49 Taiwan [STATIC: the values and the country-name table; INFERRED: that the application getter returns the country index] |
| 20002 skating rink (Holidays) | `0x12F` | Holidays building set enabled; calendar month 8 < m < 12, i.e. September–November [STATIC]; park measure PM > 29 |

**Park measure PM** [STATIC]: a weighted sum of park buildings by agent type, counting each type
present: 14311 small park ×1, 14312 big park ×9, 14313 baseball ×16, 14314 playground ×4,
14315 pond ×4, 14316 fountain ×1. Agent type 14310 ("Park", the parent of these in
`[AgentTypeHierarchy]`) is counted separately and is not part of PM. [DATA check] An original
save of Berlin in September 2067 had both `0x12C` and `0x12F` pending. That needs month 9 to be
inside both ranges, and PM > 29 (Berlin has 1059 park buildings of types 14311–14316).
The month is the calendar month from the city date, 1–12.

When the condition holds:

1. For building types 1002, 10110 (mayor's house), 10120 (city hall), 13005 (courthouse), 13871
   (university), 14150 (statue) and 14170 (performing arts) the entry becomes **available**
   immediately (these petitions are informational).
2. If not owned, the entry's petition is requested as a *persistent* petition (P = 1) whose
   argument is the building type.

When the condition does not hold and the entry is not available, any pending, visible petition
for it is withdrawn.

**Accepting** a reward petition with buttons makes the entry available. The mapping is:
`0x84`→13874, `0x88`→14003, `0x8B`→14550, `0x8C`→14370 (golf course), `0x8D`→14325,
`0x8E`→13872, `0x8F`→11001, `0x94`→14600 (stadium), `0x96`→12350, `0xCD`→11002 (military
base), `0x12C`→20001, `0x12F`→20002. Declining has no effect beyond answering the petition
(it may be offered again after the cool-down while the condition still holds).

Placing reward buildings, their effects and costs are in `13-buildings-occupants.md`.

## 7. Opinion / approval

SimCity 3000 Unlimited has **no opinion poll and no separate mayor-approval rating** in this
subsystem. [STATIC: no such data, strings or logic in the advisor/petitioner component] Citizen
mood is expressed through the city *aura* (`17-sim-services.md`), which the advisors and the
petitioner manager read as an input (e.g. `MAX_GAMBLING_AURA`, `PRO_READING_AURA`,
`CITY_AURA_LOW/HIGH`, `LOW/HIGH_DREARY_RATING`). Approval-like feedback appears only as advisor
topics, petitions (e.g. informational "city aura low" or "holiday for the mayor" petitions) and
ticker lines. The budget tax pages' "approval rating" label (`029541F4:1D2`) is the same aura
history chart, not a second metric (`09-ui-screens.md` §6.1). The only "lose" condition here is
the budget advisor's debt rule (section 3.7).

## 8. INI grammar (complete)

All files use the common INI reader (`02-containers.md`): `[Section]` headers, `key=value`
lines, keys case-insensitive [INFERRED], whitespace around `=` tolerated. No expressions,
variables or conditionals exist. Value forms:

| Form | Used by | Grammar |
|---|---|---|
| Integer | all threshold keys | optional sign, decimal digits; may be followed by `;` and junk (ignored, e.g. `LOW_LIFE_EXPECTANCY=60;`) |
| Hex pair | TopicGfxIDs | `0xGROUP,0xINSTANCE` |
| Mood | AdvisorMoods | key `0x%08X`, value integer 1–5 |
| Ticker | TickerData | key `0xTABLE,0xID`, nine comma-separated integers |
| Building list | `[REWARD_BUILDINGS]`, `[BUSINESS_DEAL_BUILDINGS]`, `[POWER_BUILDINGS]`, … | key = building ID (decimal), value `Name;f1;f2;…` (name ignored; fields `;`-separated) |
| File list | `[TickerData Files]`, `[PetitionerExtraInfo Filenames]` | repeated `entry=filename` |
| Advice mapping | `[PETITIONER_ADVICE_MAPPING]` | key decimal petition number, value `0xADVISORID;` or `0;` |
| Table list | `[PETITIONER_STRINGS_*]` | `NAME=0xTABLEID` (name ignored; order significant) |
| Extra info | `[PetitionerExtraInfo]` | key `0x%08X` petition ID, value `a,b` decimal |
| Deal list | `[BUSINESS_DEALS2]` | key = name, value `type;year;value;petitionId` (see `19-sim-economy.md` §11) |

Numeric fields are parsed with C-library semantics: leading whitespace skipped, an optional sign,
then digits; parsing stops at the first non-digit (so `60;` reads as 60). Hexadecimal fields
(`0x…`) are parsed as unsigned 32-bit. A missing key leaves the compiled-in default (0 unless
stated). [STATIC]

## Persisted state

All records below live in the inner record database of a city file (`10-save-format.md` §5;
key = Type:Group:Instance). Integers little-endian. Verified against the shipped files: 259/259
topic records, 37/37 petitioner records, 37/37
city-planner records and 27/27 utilities records in all shipped `.sc3/.st3/.SNR` files parse
exactly to the record end. [DATA]

The news ticker queues, the random pool and the forced-topic lists are **not** saved; the
ticker starts empty after loading. [STATIC/DATA: no record]

### P.1 Advisor topic list — `013DEE82:<advisorId>:0000000B`

One record per advisor (Group = advisor ID from section 3.1).

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | 1 |
| 0x02 | 2 | u16 | subVersion | 2 |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4 | u32 | listVersion | 1 (reader requires 1) |
| 0x0C | 4 | u32 | count | Number of topic instances |
| 0x10 | 4 | u32 | reserved1 | 1 in all files; write 1 |
| 0x14 | 4 | u32 | reserved2 | 1 in all files; write 1 |
| 0x18 | var | entry[count] | topics | In list order |

Topic entry:

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| +0 | 4 | u32 | topicId | Topic ID |
| +4 | 4 | u32 | tickerItem | Ticker item number at save time (stale after load; write any, e.g. 0) |
| +8 | 4 | u32 | groupKey | Key group K (0 = none) |
| +12 | 1 | u8 | sticky | S flag |
| +13 | 1 | u8 | reserved | 0 |
| +14 | 1 | u8 | limit | L |
| +15 | 1 | u8 | counter | C |
| +16 | 1 | u8 | flags | bit0 H (hidden), bit1 P, bit2 E, bit3 R |
| +17 | 4 | u32 | argCount | Number of argument entries |
| +21 | var | arg[argCount] | args | Token arguments |

Argument entry: u32 `argIndex`; u32 `len` + `len` bytes token name (no terminator, e.g. the
coordinate tokens `%X%`, `%Y%`); u32 `a`; u32 `b`; u32 `value` [field meanings OPEN; shipped
files only contain coordinate pairs].

Observed combinations [DATA]: plain condition topics have S=0, L=0, flags 0 or 8; event topics
have L>0 with flags 7 (H|P|E) or 11 (H|P|R) and sometimes S=1.

### P.2 Petitioner pending list — `206C6E7C:02619041:00000000`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | 1 |
| 0x02 | 2 | u16 | subVersion | 2 |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4 | u32 | listVersion | 1 |
| 0x0C | 4 | u32 | count | Number of pending petitions |
| 0x10 | 4 | u32 | reserved1 | 1 |
| 0x14 | 4 | u32 | reserved2 | 1 |
| 0x18 | var | entry[count] | petitions | In list order |

Petition entry: u32 petitionId; u32 argument (the requester-supplied argument; 0 in most
shipped entries, non-zero mainly for L = 12 deal petitions [OPEN meaning]); u32 tickerItem; u32 reserved (0 in all files); u8 reserved (0); u8 L; u8 C; u8 flags
(bit0 H answered/hidden, bit1 P, bit2 E, bit3 R); u32 argCount + args (same format as P.1).
Shipped data: ordinance petitions L = 120; persistent petitions (P) with L = 0; others with
L = 6, 12 or 84 [DATA].

### P.3 Reward state — `206C6E7C:C28D0B6E:00000000`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | u16 | version | 1 |
| 0x02 | 2 | u16 | subVersion | 2 |
| 0x04 | 4 | u32 | sentinel | 0xDEADBEEF |
| 0x08 | 4 | u32 | count | Number of entries |
| 0x0C | 14·count | entry[] | rewards | Only rewards that are available or owned are written |

Entry: u32 Type, u32 Group, u32 Instance — the resource key identifying the reward building
(all shipped entries use Type `0x207EDC0E`, Group `0x0000057E`; the Instance is an occupant ID,
`13-buildings-occupants.md` §1); u8 owned; u8 available. On load, entries not present are neither
owned nor available.

Building type → saved Instance [DATA: every shipped city that has reward entries; each saved
Instance is an occupant whose agent type (attribute 0x65) is the building type]:

| Type | Instance | Occupant | Type | Instance | Occupant |
|---|---|---|---|---|---|
| 1002 | `00001F51` | Stock Exchange | 14003 | `00001F49` | Geyser Park (national park) |
| 10110 | `000000F3` | Mayor's House | 14150 | `000000DB` | Historic Statue |
| 10120 | `000000D0` | City Hall | 14170 | `00001F4B` | Performing Arts Center |
| 11001 | `00001F57` | Defense Contractor | 14325 | `00001F5D` | Theme Park |
| 11002 | `00001F8B` | Military Base | 14370 | `00001F6E` | Country Club |
| 12350 | `00001F52` | Space Port | 14550 | `00001F53` | Lighthouse |
| 13005 | `00001F4D` | County Courthouse | 14600 | `00001F54` | Stadium |
| 13871 | `00001F98` | University | 19001 | `00001F55` | Toxic Waste Conversion Plant |
| 13872 | `00001F56` | Medical Research Center | 19002 | `00001F95` | GigaMall |
| 13874 | `00001F6A` | Science Center | 19003 | `00001F5B` | Casino Row |
| 19004 | `00001F48` | Maximum Security Prison | 19999 | `00004E20` | Quigley Castle |
| 20001 | `000061A8` | Haunted House | 20002 | `00002D18` | Winter Wonderland |

Rule: the Instance is the **lowest occupant ID** with that agent type. Four types have more than
one occupant: 11002 (`1F8B`–`1F8E`), 13871 (`1F98`–`1F9B`), 14325 (`1F5D`–`1F60`, `2351`) and
14600 (`1F54`, and `3FCD` in `OccupantAttribs_2.IXF`). The saved Instance is always the lowest:
European Berlin also saves `1F54`, and Sacramento saves `1F5D` with owned = 1 although no `1F5D`
building is in the city (its theme park is another variant). So the key is canonical per type and
**owned** is per agent type, not per occupant.

Deal buildings (19001–19004, 19999) are in the same record. Entries are written in **ascending
building type** order (every shipped city). Owned = 1 matched a placed building of that type in
every case. Owned = 1 with available = 0 is common (Madrid has 13 such entries).

### P.4 Advisor-manager flags — `022E288E:422E28E8:00000000`

This record is owned by the advisor manager (class 0x422E28E8), not by the utilities advisor. The six
booleans are probably one per advisor slot [INFERRED]; all six are 1 in every shipped file.

14 bytes: u16 1, u16 2, u32 0xDEADBEEF, then six u8 booleans (all 1 in every shipped file)
[OPEN: meaning. The count matches the six `[POWER_BUILDINGS]` lines that carry an
announcement topic (gas, nuclear, wind, solar, microwave, fusion), but the values are 1 even in
cities dated before those years, so they are probably not "announced" flags]. Write six 1 bytes
when unknown.

## Open questions

* Which UI actions trigger on-demand rule evaluation (cadence itself resolved: weekly, 3.6).
* Advisor rule wiring is now transcribed for all seven advisors (3.7–3.13). Remaining gaps:
  exact identities of several metrics marked [INFERRED] (demand-cap cause flags A/B, neighbour
  counts K/Kr/Kt, the four extra counts in B (3.11.1), garbage capacity GC, utilities Nb), the
  seed source of the utilities advisor's private generator, and the full ordinance → acknowledgement
  topic table for the "ordinance changed" notification (3.6).
* Whether the city-planner demand-history ring is sampled weekly or monthly (3.11).
* Exact metric definitions behind several rules (garbage surplus/limit, airport and park
  measures for rewards, the alternative population measure used for national park and
  lighthouse, jail capacity term, police strength measure).
* Petitioner panel: whether the manager keeps tokens stable across refreshes (otherwise every refresh re-creates all portraits).
* PetitionerExtraInfo character-type → voice-sound table.
* Meaning of the six utilities-advisor booleans (P.4) and the topic argument fields `a`/`b`/`value`
  (P.1). (Resolved: reward-state keys map to building types, P.3.)
* (Resolved: declining petition `0xD7` sets the flag read by `zyxwvu`.) Whether that flag is saved with the city is OPEN.
* Per-stage triggered-news IDs of the disaster state machines (`SIMDSTR`); network events were not
  found to post news (no `SIMNTWRK` reference to the news tables).

## Building availability (utilities advisor)

[STATIC] `Advisor-Utilities.ini [POWER_BUILDINGS]` and `[WATER_BUILDINGS]` list the gated types as
`agentType=Name;year;topic` (shipped: coal, oil, pump and tower year 0; gas 1955, nuclear 1965,
wind 1980, solar 1990, microwave 2020, fusion 2050; water treatment 1935, desalination 1960). Each
entry has an unlock flag, which is not saved:
* At city load, every entry whose year ≤ the current year is unlocked.
* Each month, with probability 10 % (rand(100) < 10), the first locked entry whose year ≤ the
  current year is unlocked: power list first, then water. The advisor announces it with the
  entry's topic number.
The environment advisor keeps the same kind of list for `Advisor-Environment.ini
[GARBAGE_BUILDINGS]` (recycling 12851 in 1970, incinerator 12852 in 1920, waste-to-energy 12853
in 2000). It has the same load-time and monthly 10 % unlock rules, and the garbage picker lists
only unlocked entries (cheat `garbage in, garbage out` bypasses this) [STATIC].
`desalinationAvailable` in the advisor rules is the unlock flag of type 12830.
