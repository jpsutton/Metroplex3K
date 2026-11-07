# 23 — Scenarios, scenario scripts, tutorials, scenario templates

Status: second pass. All 13 `.SNR` files and all 14 `.SSC` files conform to the layouts below. Items marked
`[OPEN]` still need confirmation against the original program.

This document covers:

* the `.SNR` scenario file (an IXF city save plus scenario records);
* the scenario script language (compiled text form stored in `.SNR`) and its run-time model
  (goals, ranks, dialogs, timed/repeating commands, win/lose);
* how scenario text is linked to per-scenario string tables;
* the `.SSC` scenario-template / Scenario Creator project file;
* tutorials (which are ordinary scenarios), starter towns (`.st3`) and terrains (`.sct`) at the
  game-flow level.

Shared city records (the bulk of an `.SNR`) are specified in `10-save-format.md`; the IXF container
in `02-containers.md`; string-table record encoding in `05-text-and-fonts.md`; disaster behaviour in
`21-disasters.md`.

Notation: resource keys are written `T:G:I` (Type, Group, Instance), as in `00-README.md`. Note
that on disk the IXF index stores each entry as (Group, Instance, Type, offset, size); see
`02-containers.md`.

---

## 1. File inventory and discovery

| Item | Location | Notes |
|------|----------|-------|
| Scenario files | `Cities/Scenarios/*.SNR` | 13 shipped. Enumerated with mask `*.SNR` in the `Scenarios` sub-directory of `Cities`. [STATIC][DATA] |
| Per-scenario text | `Apps/Res/Text/<LANG>/<Name>.IXF` | One string table per scenario (e.g. `Betterfeld.IXF`). [DATA] |
| Scenario UI/script text | `Apps/Res/Text/<LANG>/SC3SEStringsScenarioScrpt.IXF`, `SEStringsUI.IXF` | Shared strings for scenario dialogs. [DATA] |
| Command-ID map | `Sys/SYS.PAK` → `SC3ScenarioLayer.ini`, section `[CommandNameToIdMapping]` | A loose `\Sys\SC3ScenarioLayer.INI` is tried first. [STATIC] |
| Scenario templates | `Scripts/<LANG>/*.SSC` | 14 shipped (2 per language × 7). Never opened by the game; used only by the Scenario Creator. [STATIC][DATA] |
| Scenario Creator | `Apps/ScenarioCreator/*.mde` | Microsoft Access application (not to be reimplemented). It writes `.SNR` files directly and saves/loads `.SSC` projects. [DATA] |
| Presenter pictures | `Apps/ScenarioCreator/Presenters/*.BMP` | 12 bitmaps used by the Scenario Creator when building `.SNR` presenter tables. [DATA] |
| Tutorial city | `Cities/TUTORIAL.SC3` | Ordinary city save. [DATA] |

---

## 2. `.SNR` structure

An `.SNR` file is an IXF container [DATA, 13/13]. It contains:

1. **Every record of a normal city save** (`.sc3`) — the city the scenario starts from. These are
   owned by `10-save-format.md`. [DATA]
2. **Scenario records**, listed below. None of these appear in any shipped `.sc3`, `.st3` or `.sct`.
   [DATA]

Shipped `.SNR` files were written by the Scenario Creator, not by the game: the IXF index is
"tight" (data begins immediately after the terminating index entry rather than at a 0x5000-aligned
offset) and string records have their index size equal to the character count, i.e. 4 bytes short.
A reader must therefore size a string record from its own length prefix, not from the index. [DATA]
(see `02-containers.md`).

### 2.1 Scenario record table

| Key `T:G:I` | Count | Encoding | Role |
|-------------|-------|----------|------|
| `04845CBE:C3DE4D66:00000000` | 1 | text lines | Scenario header (entry script, title, string-table group) |
| `23DFAE5F:23DFAE8C:<n>` | 6–12 | text lines | One script ("subroutine") per instance `n` |
| `A4846144:A48461A3:00000000` | 1 | text lines | Text-style (colour) table for dialog strings |
| `44EB6BC8:44EB6BC9:00000000` | 1 | text lines | Presenter table (picture → name/title strings) |
| `26E732E0:19A6CEA1:00000000` | 1 | binary, 12 bytes | Scenario catalogue info (string table, two small integers) |
| `2026960B:19A6CEA1:00000002` | 1 | string record | Short author/company string |
| `62B9DA24:19A6CEA1:00000001` | 1 | Windows BMP (24-bit) | Scenario picture shown in the scenario chooser/intro |
| `2026960B:<scenario table>:01BBE888` | 1 | string record | Author name |
| `2026960B:<scenario table>:<id>` | 0–48 | string records | Extra presenter name/title strings embedded in the file |
| `C3DE4D66` state record (see §8) | 0 | binary | Only in a city saved mid-scenario |

"Scenario table" is the per-scenario string-table group (§6). [DATA]

### 2.2 Text-record conventions

All text scenario records (header, scripts, style table, presenter table) are 8-bit text. [STATIC]

* A line ends at CR (0x0D). LF (0x0A) characters are ignored wherever they occur.
* Numbers are signed 32-bit decimal; resource IDs above 0x7FFFFFFF therefore appear negative
  (e.g. `-95955054` = 0xFA47D792). Fields are separated by runs of spaces.
* Anything after `;` on a script header line is a comment.

### 2.3 Scenario header `04845CBE:C3DE4D66:0`

| Line | Content | Meaning |
|------|---------|---------|
| 1 | `SNR` | Signature (not validated beyond being present) [STATIC] |
| 2 | path | Path of the source city used by the Scenario Creator; informational only [DATA] |
| 3 | `T G I` | Key of the entry-point script. Always `601861727 601861772 n` = `23DFAE5F:23DFAE8C:n` [DATA] |
| 4 | `G I` | String reference of the scenario title (string table G, string I; I is 1 in all shipped files) [DATA] |
| 5 | `P K` | Default **presenter** for the scenario's dialogs: P = presenter ID, K = presenter kind, same codes as `cmdPopUpMsg` a4 [STATIC]: 0 = game-side presenter table (P looked up in the game's built-in presenter list, §4.7.0; it yields picture key, name and title); 1 = advisor (P is an advisor ID from §4.6); 2 = scenario presenter table (P is a row key of §2.6). Shipped `2 0` therefore means game-side presenter 2. If K = 1 and P is not a known advisor, the Utilities advisor `022E288E` is used. [STATIC] Values shipped: `828D04EB 1`, `828D0A2F 1`, `028D0FC5 1`, `C28D0B6E 1`, `628D0C45 1`, `2 0`. [DATA] |
| 6 | `G` | Group of the scenario's string table (equals the G on line 4 in all shipped files) [DATA] |

### 2.4 Script record `23DFAE5F:23DFAE8C:n` — see §3.

### 2.5 Text-style table `A4846144:A48461A3:0`

Line 1: decimal count N. Then N lines of the form

`G , I = s , r1 , g1 , b1 , r2 , g2 , b2 , f , x`

* `G , I`: the string the style applies to. Only I is used as the lookup key; G is ignored. [STATIC]
This is a **news-ticker style table**. Its fields have the same meaning as the global ticker data
lines `class,R,G,B,r2,g2,b2,underline,repeats` of `22-advisors-news-petitioners.md`
(`[Sc3TickerData]`), but they apply only to scenario strings posted with `cmdShowNewsticker`
[STATIC]:

* `s` = ticker **class** 1–7, which selects the ticker queue and expiry. Values 1 and 2 seen. [STATIC][DATA]
* `r1 g1 b1` = text colour, 0–255 each. `r2 g2 b2` = the second (background) colour. Both are
  converted to display colours when the table is loaded. The ticker itself ignores the second colour
  (`22-…` forces it to black); shipped files hold 0,0,0. [STATIC][DATA]
* `f` = **underline** flag (byte). 1 draws the underline that marks a clickable item. It is always 1
  in shipped data. [STATIC][DATA]
* `x` = **repeat count**. 0 means repeat until expiry. It is always 1 in shipped data. [STATIC][DATA]

Use when posting: if string I of a `cmdShowNewsticker` command has an entry in this table, the
item is posted with these attributes. Otherwise it is posted with the ticker's defaults (the
missing-key defaults of `22-advisors-news-petitioners.md`: class 6, white, underline 0, repeats 1).
[STATIC]

### 2.6 Presenter table `44EB6BC8:44EB6BC9:0`

Line 1: count N. Then N lines `P  NAME  TITLE` (three integers):

* `P` presenter (picture) ID; `NAME`, `TITLE` string IDs in the scenario string table.
* The first 12 rows are identical in all 13 files and correspond to the 12 standard presenter
  pictures; later rows (IDs 137+ / 150+ / 156+) add scenario-specific presenters (advisors). [DATA]
* A scenario-table presenter (kind 2, §2.3 line 5 and `cmdPopUpMsg`) with ID P is shown with
  picture `62B9DA24:6504691D:<row picture>`, name string (scenario table, NAME) and title string
  (scenario table, TITLE). [STATIC] The row's picture field is the first integer, the same value as
  the key P; it is a 32-bit hash written as a signed decimal (for example −1862531500). The lookup does not check that
  the row exists; an implementation should show no portrait when it is missing. The 12 standard
  rows are the Scenario Creator's built-in presenters (picture ID, name ID, title ID; e.g.
  −1862531500/53/54). [DATA] No shipped file contains any `62B9DA24:6504691D` picture, and shipped scripts use
  advisor presenters (kind 1) almost exclusively, so the standard rows are effectively unused. [DATA]
* Parsing: each row is read as up to four integers of which exactly three must be present; a row
  that fails stops loading of the table. [STATIC]

### 2.7 Catalogue record `26E732E0:19A6CEA1:0`

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | u32 | stringTable | Group of the scenario's string table (same as header line 6) [DATA] |
| 0x04 | 4 | u32 | scenarioType | Scenario Creator "ScenarioType" code (its localized scenario-type list). Shipped: 7 = the three tutorials and both `.SSC` templates; other scenarios 1, 2, 3, 9 [DATA]. Names of the codes `[OPEN]` (they live only in the Scenario Creator database) |
| 0x08 | 4 | u32 | difficulty | Scenario Creator "Difficulty" level 1–5. Shipped: 1 for tutorials and Seoul World Cup, 5 for London Fires and Rags To Riches [DATA]; 1 = easiest `[INFERRED]` |

The game reads only `stringTable` from this record: the scenario name and description shown in the
scenario chooser are strings 0 and 1 of that table, and the picture is `62B9DA24:19A6CEA1:1`. `scenarioType` and `difficulty` are **not read** by the game. They are only
copied along with the other scenario records (§8) [STATIC]. An engine must preserve them and may
show them as extra information. Field names follow the Scenario Creator's `.SSC` columns that feed
them (§7: the "typeA"/"typeB" strings `7`/`5` and `7`/`1` of the templates) [DATA][INFERRED].

---

## 3. Script language (compiled text form)

### 3.1 Record layout

1. Line 1 is skipped (always `; Script Code`).
2. Line 2: command count C (parsed as a decimal integer; held as 16-bit).
3. C command blocks, each:
   * header line: `delay  cmdId  argc` — three decimal integers (delay unsigned 32-bit, cmdId 16-bit,
     argc 8-bit); anything after them (the `;` comment) is ignored;
   * `argc` argument lines: `kind  value` — two decimal integers (kind 8-bit, value signed 32-bit);
   * exactly **one** further line, which is consumed and ignored (blank in all shipped files).

Every number is read with "leading decimal integer" semantics: parsing stops at the first
non-digit, so a literal written `2.5` is read as 2 (fractions are discarded). [STATIC] Blank lines are
not skipped anywhere except as the single separator line, so a writer must emit exactly the layout
above. [STATIC][DATA]

`cmdId` is resolved through `[CommandNameToIdMapping]` (§3.3). [STATIC][DATA]

### 3.2 Script references

A script is identified by (G, I) with T fixed at `23DFAE5F`; G is always `23DFAE8C`, so the
instance number alone names a script. Commands that take a script (e.g. `cmdSubRoutine`,
`cmdRepeatRegular`) pass the pair `601861772 n`. String references are pairs `stringTable stringID`.
[DATA]

### 3.3 Command table

From `SC3ScenarioLayer.ini` `[CommandNameToIdMapping]` (the engine must read the mapping from the
user's data, the IDs below are the shipped mapping used by all shipped scripts):

| ID | Name | ID | Name | ID | Name |
|----|------|----|------|----|------|
| 0 | cmdAdd | 25 | cmdSubRoutine | 50 | cmdEnableNewNeighborDeals |
| 1 | cmdSubtract | 26 | cmdCountUnpoweredBuildings | 51 | cmdGetSurplusPower |
| 2 | cmdAndCondition | 27 | cmdCountUnwateredBuildings | 52 | cmdGetSurplusWater |
| 3 | cmdAssign | 28 | cmdGetTrafficDensity | 53 | cmdOfferRewardBuilding |
| 4 | cmdGetCrimeValue | 29 | cmdGetFundsAvailable | 54 | cmdGetCurrentDate |
| 5 | cmdGetLandValue | 30 | cmdGetMonthsElapsed | 55 | cmdSetVariable |
| 6 | cmdGetPollutionValue | 31 | cmdEndScenario | 56 | cmdCountStructuresNearLocation |
| 7 | cmdGetPopulation | 32 | cmdNotEqual | 57 | cmdGetTotalDebt |
| 8 | cmdComputePercentage | 33 | cmdCountStructures | 58 | cmdCountStructuresWithAppearanceNearLocation |
| 9 | cmdDecrement | 34 | cmdLessThan | 59 | cmdCountStructuresWithAppearanceInCity |
| 10 | cmdDivide | 35 | cmdAddGoal | 60 | cmdMoveCamera |
| 11 | cmdEndAnd | 36 | cmdMakeDialogText | 61 | cmdEnableDisableDisasters |
| 12 | cmdEndOr | 37 | cmdMarkGoalStatus | 62 | cmdPlaySound |
| 13 | cmdEqual | 38 | cmdAllGoalsMet | 63 | cmdSetScenarioSpeed |
| 14 | cmdGreaterThan | 39 | cmdGoalStatusIs | 64 | cmdStartToxicCloud |
| 15 | cmdIf | 40 | cmdPopUpMsg | 65 | cmdStartWhirlpool |
| 16 | cmdIfElse | 41 | cmdAddRankInfo | 66 | cmdStartSpaceJunk |
| 17 | cmdIncrement | 42 | cmdSetResultsDlgText | 67 | cmdStartLocusts |
| 18 | cmdMultiply | 43 | cmdBeginBlock | 68 | cmdIsOrdinanceEnacted |
| 19 | cmdOrCondition | 44 | cmdEndBlock | 69 | cmdOfferBusinessDeal |
| 20 | cmdRepeatRegular | 45 | cmdGenerateRandomNum | 70 | cmdIsBusinessDealActive |
| 21 | cmdShowNewsticker | 46 | cmdStartEarthquake | 71 | cmdEnableDisableBusinessDeal |
| 22 | cmdStartFire | 47 | cmdStartUFOAttack | 72 | cmdGetRCItaxes |
| 23 | cmdStartRiot | 48 | cmdGetAura | 73 | cmdIsNeighborConnection |
| 24 | cmdStartTornado | 49 | cmdDisableNewNeighborDeals | 74 | cmdIsNeighborDealPresent |

[DATA][STATIC]

(Per-command argument semantics: §4.)

---

### 3.4 Argument kinds

Each argument line is `kind value`. [DATA][STATIC]

| kind | Meaning |
|------|---------|
| 0 | Literal value |
| 1 | Variable reference: the argument's value is the current content of scenario variable number `value` |

* When a command asks for an argument index ≥ its `argc`, the value is 0. [STATIC]
* Commands that **store** a result take the destination variable number as a literal in argument 0
  (kind 0). [DATA]
* Some literals are written with a decimal point (e.g. earthquake magnitudes `2.5`, a divisor `187.33`)
  [DATA]; they load as their integer part (§3.1). [STATIC]

Shipped usage statistics (all 13 files, 116 scripts): 5 865 commands, of which 5 780 have delay 0 and 85 a non-zero delay; 9 271 arguments
(7 836 kind 0, 1 435 kind 1). Every command that occurs in the shipped data always occurs with the
same `argc` value. The `argc` column of §4.6 gives that value; for commands that do not occur in the
shipped data it gives the number of arguments the command reads (missing arguments read as 0, see
above). [DATA]/[STATIC]

---

## 4. Run-time model

### 4.1 Scenario state

A running scenario holds [STATIC]:

* **Variables**: 512 signed 32-bit integers, numbered 0–511, all 0 at scenario start.
* **Scenario clock**: an unsigned counter in "scenario minutes" (1 440 per game day), starting at 0.
* **Start date**: month and year of the city date when the scenario was started.
* **Timeline**: the entry-point script (header line 3) with a cursor to its next unexecuted command.
* **Four repeat lists** (index 0–3) of script references (see `cmdRepeatRegular`).
* **Goals**: map goalID → (string table, string ID, met flag).
* **Ranks**: map rankID → (string table, string ID).
* **Result rank**, intro/progress dialog texts, results-dialog texts, a "speed locked" flag and an
  "active" flag.

### 4.2 Starting a scenario

1. Load the city records as for a normal city (`10-save-format.md`), then the scenario records (§2).
   If the scenario header is absent, the city is not a scenario.
2. If no saved run-time state (§8) is present (a fresh start): put the entry-point script on the
   execution stack, record the current city month and year as the start date.
   If a saved state is present: restore it instead (variables, lists, goals, ranks, clock, current
   timeline script) and do not re-run the start.
3. Point the timeline cursor at the first command of the entry script, mark the scenario active,
   and notify the simulation that a scenario is running.

### 4.3 Clock and evaluation timing

The scenario clock is driven by two agent types of `CitySim.ini` [STATIC]:

| Agent type (`CitySim.ini`) | When it runs | Effect on the scenario clock |
|---------------|------------------|------------------------------|
| `C3629AFA` (DisasterInstanceMinuteTick) | once per simulated minute, but **only while at least one disaster instance is active** (`14-sim-clock.md` §6.3 rule 5) | clock ← clock + 1 |
| `61190076` (AreaAgent) | once per day tick | if the game day differs from the last seen value: clock ← the smallest multiple of 1 440 that is **strictly greater** than the clock (⌊clock/1440⌋·1440 + 1440) |

**Resulting clock rate.** Outside disasters the scenario clock advances by exactly 1 440 per game
day. While a disaster is active it also advances by 1 per simulated minute. A minute tick is
delivered before the day tick that it completes (`14-sim-clock.md` §2.2), so:
* a day that is only partly spent in minute mode still ends at the next multiple of 1 440 (the
  rounding absorbs the partial count);
* a day spent **entirely** in minute mode delivers 1 440 minute steps, which already bring the clock to
  the next multiple of 1 440, and the day step then adds another 1 440. Each such day advances the
  scenario clock by **2 880** (two days).

This is a defect of the original: timeline commands fire earlier, in game days, during long
disasters. A faithful clone reproduces it, because scenario timing in replays depends on it.
[STATIC for each step; INFERRED, high confidence, for the combined effect]

On every agent run, and once when the simulation starts (the "simulator started" notification of
`14-sim-clock.md` §3.1), after updating the clock, the scenario [STATIC]:

1. Reads the current city date (day of month, month, year).
2. If the day differs from the last seen day: runs every script in repeat list 0 (**daily**). If, in
   addition, the day of month is a multiple of 7 (days 7, 14, 21 and 28), runs list 1 (**weekly**). If
   the month differs: runs list 2 (**monthly**). If the year differs: runs list 3 (**yearly**). Lists
   run in the order 0,1,2,3 and scripts in each list in insertion order. "Day" is the day of month:
   the date is read as a (day, month, year) triple. [STATIC; INFERRED, high confidence, that the
   triple's day is day-of-month] The "weekly" list is therefore **not** tied to the Monday week
   notification of `14-sim-clock.md`, and runs four times a month.
3. **Timeline**: while the scenario is active and the next timeline command's `delay` ≤ clock,
   execute that command and advance. Thus `delay` on entry-script commands is an **absolute
   scenario time** (1 440 = one day after start; 525 600 = one 365-day year). Timeline commands must
   be in non-decreasing delay order; a later command never runs before an earlier one. [DATA][STATIC]
4. `delay` is ignored for commands inside subroutine scripts (they run straight through). [STATIC]

### 4.4 Executing a script

A script runs to completion synchronously: each command is fetched in order and evaluated. Each
command evaluation receives an **execute flag** (true = perform side effects; false = "skip": consume
the command and any sub-commands it owns without side effects). If the scenario is no longer
active, the flag is forced false. A command evaluated with the flag false returns 1 (true) unless
stated otherwise. [STATIC]

The value of a script is the value of its last evaluated command. [STATIC]

While a pop-up message raised **from a clock callback** is open, script execution is suspended and
resumes when the pop-up closes. [STATIC]

### 4.5 Control-flow commands (prefix structure)

Control commands own the command(s) that follow them in the same script: [STATIC]

| Command | Structure | Value |
|---------|-----------|-------|
| `cmdIf` | `If C T` | Evaluate C. If C ≠ 0, evaluate T (with the incoming flag) and return its value; else skip T and return 0. |
| `cmdIfElse` | `IfElse C T E` | If C ≠ 0: evaluate T, skip E, return T. Else: skip T, evaluate E, return E. |
| `cmdAndCondition` | `And X1 … Xn EndAnd` | Starts true. Each Xi is evaluated; once one returns 0 the result becomes 0 and the remaining Xi are skipped (short-circuit). |
| `cmdOrCondition` | `Or X1 … Xn EndOr` | Starts false. Once an Xi returns non-zero the result becomes 1 and the rest are skipped. |
| `cmdBeginBlock` | `BeginBlock X1 … Xn EndBlock` | Evaluates all Xi; value = last Xi. |
| `cmdAssign` | `Assign v X` | Evaluates X; if executing, variable v ← value of X. |
| `cmdEndAnd`, `cmdEndOr`, `cmdEndBlock` | terminators | Consumed by the owning command. |

Any command (including a block) can be C, T, E or Xi. Nesting works because each owner consumes its
own terminator. [STATIC]

### 4.6 Command semantics

Notation: `a0…a5` are argument values (after variable resolution), `v` a destination variable
number (literal), `(G,I)` a string reference (string table G, string I, type `2026960B`), and
`(S)` a script reference given as `601861772 n`. "Returns" is the command's value. [STATIC] unless
marked.

| Command | argc | Arguments | Effect / value |
|---------|------|-----------|----------------|
| cmdAdd | 3 | v, a1, a2 | v ← a1 + a2; returns v |
| cmdSubtract | 3 | v, a1, a2 | v ← a1 − a2 |
| cmdMultiply | 3 | v, a1, a2 | v ← a1 × a2 |
| cmdDivide | 3 | v, a1, a2 | v ← a1 ÷ a2 (integer, truncating toward zero); if a2 = 0 then v ← −1 |
| cmdComputePercentage | 3 | v, a1, a2 | v ← (a1 × 100) ÷ a2, signed 32-bit, truncating toward zero (multiplication first, so a1 × 100 wraps at 32 bits); if a2 = 0 then v ← −1. Returns v. [STATIC] (not used in shipped data) |
| cmdIncrement / cmdDecrement | 1 | v | v ← v ± 1 |
| cmdSetVariable | 2 | v, a1 | v ← a1; returns 1 |
| cmdGenerateRandomNum | 3 | v, a1, a2 | v ← uniform random integer in [min(a1,a2), max(a1,a2)] inclusive (v ← a1 if equal). Uses the scenario's own generator instance of the game RNG defined in `14-sim-clock.md` §8 |
| cmdEqual / cmdNotEqual / cmdGreaterThan / cmdLessThan | 2 | a0, a1 | Comparison a0 = a1, ≠, >, < (signed); returns 1/0 |
| cmdGetPopulation, cmdGetLandValue, cmdGetCrimeValue, cmdGetAura, cmdGetFundsAvailable, cmdGetTotalDebt, cmdGetTrafficDensity, cmdGetSurplusPower, cmdGetSurplusWater, cmdCountUnpoweredBuildings, cmdCountUnwateredBuildings | 1 | v | v ← one city statistic, read live from the owning subsystem at the moment the command runs (no caching, no averaging over time). Per-command source, units and scaling: §4.6.1. Returns v (cmdGetSurplusPower / cmdGetSurplusWater return 1). |
| cmdGetMonthsElapsed | 1 | v | v ← (currentYear − startYear) × 12 + (currentMonth − startMonth), using the start month/year recorded at scenario start and persisted (§8). Not clamped; month numbers as in `14-sim-clock.md`. [STATIC] |
| cmdGetCurrentDate | 3 | vYear, vMonth, vDay | Three destination variables (not one): vYear ← current year, vMonth ← current month, vDay ← current day, all from the simulation calendar (`14-sim-clock.md`). Fails (returns 0) if any destination is not a valid variable number; otherwise returns 1. [STATIC] (exact year/month/day order of the three outputs: see §4.6.1) |
| cmdGetRCItaxes | 3 | vR, vC, vI | Three destination variables: vR ← residential, vC ← commercial, vI ← industrial tax rate (percent, 0–255 taken as an unsigned byte; the budget's per-zone-category tax rate of `19-sim-economy.md`). Returns 1. [STATIC] (not used in shipped data) |
| cmdGetPollutionValue | 2 | v, kind | v ← average pollution of kind (0, 1, 2 = air, water, garbage — order `[INFERRED]`) |
| cmdCountStructures | 2 | v, agentType | v ← number of objects in the city whose **agent type** matches (same filter and sources as the next row). The argument is an agent type (`CitySim.ini [AgentTypes]`), not an occupant ID |
| cmdCountStructuresNearLocation | 5 | v, agentType, x, z, radius | v ← 0, then counts objects whose agent type matches `agentType`, within the tile rectangle centred on (x − 1, z − 1). The x/z arguments are 1-based; a value above the map size becomes 0. The rectangle is [cx − r, cx + r] × [cz − r, cz + r], with the low edges clamped at 0 and the high edges at the map width/height. Sources in order, each tried only while the count is still 0: (1) buildings, (2) network pieces (road/rail/highway…), (3) a third group [INFERRED: flora/props]. The type filter takes the agent-type hierarchy object, so a parent type also matches its subtypes [INFERRED]. Shipped use: 16000 Road and 17000 Rail near the Betterfeld landfill (both are network types, found only through source 2); 8100/8700/8900/9100 industrial types. Returns 1. [STATIC] |
| cmdCountStructuresWithAppearanceInCity | 4 | v, T, G, I | v ← number of **occupants** whose occupant attribute key equals (T, G, I) exactly, over the whole map: rectangle centre (0,0) with radius max(W, H) + 1. Shipped T = 545184782 = **`0x207EDC0E`**, G = 1406 = `0x57E`, so I is an occupant ID: the command counts one specific building, for example a landmark. Only buildings are counted. Returns 1. [STATIC, DATA] |
| cmdCountStructuresWithAppearanceNearLocation | 7 | v, T, G, I, x, z, radius | Same match, within the rectangle of `cmdCountStructuresNearLocation` (centre (x − 1, z − 1), 1-based, out-of-map → 0, clamped). [STATIC] |
| cmdIsOrdinanceEnacted | 2 | ordinanceID, want | 1 if (ordinance enacted) = (want ≠ 0) |
| cmdIsNeighborConnection | 3 | a0, a1, want | Tests the neighbour-connection table (`12-networks.md`, `19-sim-economy.md`): t ← "connection exists for (a0, a1)", where a0 and a1 are each truncated to an unsigned byte (a0 = neighbour index; a1 = connection/network kind `[INFERRED]`). Returns t if want = 1, otherwise (NOT t). [STATIC] (not used in shipped data) |
| cmdIsNeighborDealPresent | 3 | n, dealKind, want | Fetches the current deal of neighbour n (n truncated to a byte). t ← 1 iff a deal record exists **and** its deal-type byte equals dealKind (low byte) **and** its neighbour byte equals n. Returns t if want = 1, otherwise (NOT t). [STATIC] Deal-type numbering as in the neighbour-deal record of `19-sim-economy.md` / `10-save-format.md` `[INFERRED]` |
| cmdIsBusinessDealActive | 2 | dealID, want | t ← "business deal dealID is active" (`19-sim-economy.md`). Returns t (0/1) if want = 1, otherwise (NOT t). [STATIC] (not used in shipped data) |
| cmdAddGoal | 3 | goalID, G, I | Adds/replaces goal with text (G,I), not met |
| cmdMarkGoalStatus | 2 | goalID, status | Sets met ← (status ≠ 0); returns 1 if the goal exists |
| cmdGoalStatusIs | 2 | goalID, status | 0 if no such goal; else (met if status ≠ 0, not-met if status = 0) |
| cmdAllGoalsMet | 0 | — | 1 iff every defined goal is met (1 if none) |
| cmdAddRankInfo | 3 | rankID, G, I | Adds/replaces rank with title text (G,I) |
| cmdEndScenario | 1 | rankID | Result rank ← rankID if defined, else −1; scenario becomes inactive; simulation paused; results dialog shown (§4.7) |
| cmdMakeDialogText | 3 | G, I, mood | **Replaces** the single text of the start/progress dialog with string (G,I). Nothing is appended. The third argument is the presenter **mood**, stored in one field shared with `cmdSetResultsDlgText` (last write wins, no range check). It is used only when the dialog presenter is an advisor (kind 1), to pick the advisor portrait mood (§4.6). Default mood 3. It is not a text slot. [STATIC] |
| cmdSetResultsDlgText | 3 | G, I, mood | Same for the results dialog: replaces its single text; mood shared as above [STATIC] |
| cmdPopUpMsg | 6 | G, I, a2, presenter, kind, mood | Modal message (§4.7). a2 = 1 makes the pop-up of the "important" style. **kind** (a4) = presenter kind: 0 game-side presenter table, 1 advisor (presenter = advisor ID, see below), 2 scenario presenter table (§2.6); any other value shows no portrait or name. **mood** is used only for kind 1. [STATIC] |
| cmdShowNewsticker | 2 | G, I | Posts (G,I) to the news ticker (`22-advisors-news-petitioners.md`) |
| cmdPlaySound | 1 | soundID | Plays a UI/event sound by ID (`06-audio.md`) |
| cmdMoveCamera | 3 | x, z, zoom | Centres view on tile (x,z); zoom > 4 is replaced by 2. Out-of-map coordinates: command fails (returns 0) |
| cmdSetScenarioSpeed | 2 | speed, lock | Sets simulation speed (`14-sim-clock.md` speed index); unpauses if paused; lock ≠ 0 prevents the player from changing speed |
| cmdRepeatRegular | 3 | freq, (S) | Adds script S to repeat list freq (0 daily, 1 weekly, 2 monthly, 3 yearly); freq > 3 fails |
| cmdSubRoutine | 2 | (S) | Runs script S immediately; returns its value |
| cmdStartFire | 2 | x, z | Starts a fire; see `21-disasters.md` §3 for all disaster commands |
| cmdStartRiot | 3 | x, z, warning | Starts a riot |
| cmdStartEarthquake | 2 | magnitude×10, warning | Starts an earthquake |
| cmdStartLocusts | 1 | warning | Starts a locust swarm |
| cmdStartUFOAttack | 1 | warning | Starts a UFO attack (not used in shipped scenarios) |
| cmdStartWhirlpool | 2 | magnitude×10, warning | Starts a whirlpool (not used in shipped scenarios) |
| cmdStartSpaceJunk | 2 | magnitude×10, warning | Starts a space-junk fall (not used in shipped scenarios) |
| cmdStartToxicCloud | 3 | x, z, warning | Starts a toxic cloud (not used in shipped scenarios) |
| cmdStartTornado | 8 | start x, start z, target x, target z, variant, duration, speed, warning | Starts a tornado (not used in shipped scenarios) |
| cmdEnableDisableDisasters | 1 | flag | flag ≠ 0 → random (scheduled) disasters enabled; flag = 0 → disabled. Calls the city's "disasters enabled" switch, the same switch as the player's disable-disasters option (`21-disasters.md`); it does not stop disasters already in progress nor scripted `cmdStart*` disasters `[INFERRED]`. Always returns 1. [STATIC] |
| cmdDisableNewNeighborDeals / cmdEnableNewNeighborDeals | 1 | dealType | dealType = −1 means all deal types; 0–5 selects one of the six neighbour-deal types (`19-sim-economy.md`); any other value is silently ignored. Blocks / re-allows **proposing new** deals of that type; existing deals continue (§5). Always returns 1. [STATIC] |
| cmdOfferRewardBuilding | 1 | occupantID | Makes that reward building available (`13-buildings-occupants.md`) |
| cmdOfferBusinessDeal | 1 | dealID | Offers business deal dealID to the player now (the normal business-deal proposal flow, with its advisor dialog). Always returns 1. [STATIC] |
| cmdEnableDisableBusinessDeal | 2 | dealID, flag | flag = 1 → business deal dealID may be offered; any other value → it is withheld. Always returns 1. [STATIC] |

Presenter IDs (advisor IDs, see `22-advisors-news-petitioners.md`) used by `cmdPopUpMsg` and header
line 5: `022E288E` Utilities, `628D0C45` Budget, `C28D0B6E` City Planner, `828D04EB` Environment,
`828D0A2F` Public Safety, `028D0FC5` Transportation, `C28D0F40` Demographics. The portrait shown is
the advisor portrait image `62B9DA24:52DB60A0:I` with I composed from an advisor index
(Utilities 1, Budget 2, City Planner 3, Environment 4, Public Safety 5, Transportation 6,
Demographics 7) in bits 8–15, the mood/variant (1–5; 0 means 3) in bits 0–7, an era/region
variant (0–2) in bits 16–23 and bit 24 for the "important" style; when that image is missing the
lookups fall back to variant 0, then mood 3, then `0x10303`. [STATIC] (The variant source is
defined in `22-advisors-news-petitioners.md`.)

#### 4.6.1 Statistic sources of the query commands

Every query is a live read of the named subsystem's current value. Nothing is averaged over time or
cached by the scenario engine. When the command is skipped (non-executing branch, §4.5) it returns
1 and writes nothing. When a destination is not a valid variable number (0–511) it returns 0.
[STATIC]

| Command | Source (owning document) | Value written / units |
|---|---|---|
| cmdGetPopulation | Population/residential subsystem: total city population (`20-sim-population.md`) | Sims, s32 (the figure the UI shows as city population `[INFERRED]`) |
| cmdGetLandValue | Land-value layer: city-wide land-value summary (`15-sim-growth.md`) | s32 in the layer's own land-value units, unscaled |
| cmdGetCrimeValue | Crime layer: city-wide crime summary (`17-sim-services.md`) | Unsigned byte 0–255 (zero-extended) |
| cmdGetAura | Aura (mayor-rating) layer: city-wide aura (`17-sim-services.md`) | Signed byte −128…127 (sign-extended) |
| cmdGetPollutionValue | Pollution subsystem: one of its three city-wide summaries, selected by kind = 0, 1 or 2 (air, water, garbage — order `[INFERRED]`). Any other kind fails (returns 0, v unchanged) | s32, unscaled |
| cmdGetFundsAvailable | Budget: current treasury balance (`19-sim-economy.md`) | Dollars (simoleons), s32, may be negative |
| cmdGetTotalDebt | Budget: total outstanding debt (bonds/loans) | Dollars, s32 |
| cmdGetRCItaxes | Budget: tax rate of category 0, 1, 2 (R, C, I) | Percent, low byte (0–255) |
| cmdGetTrafficDensity | Traffic simulator: area query over the whole map rectangle, tiles (0, 0) … (W−1, H−1), using the **area average** form of `18-sim-transport.md` §7.1 with network mask 3 (0x01 surface + 0x02 highway; subway not included) [STATIC] | Unsigned byte 0–255 |
| cmdGetSurplusPower | Power layer: v ← max(0, generating capacity − consumption) (`16-sim-utilities.md`) | Power units of the power layer |
| cmdGetSurplusWater | Water layer: v ← capacity − demand, taken from the water-status query. **Not** clamped, so it can be negative | Water units of the water layer |
| cmdCountUnpoweredBuildings | Power layer: number of buildings that need power and do not have it | Count |
| cmdCountUnwateredBuildings | Water layer: same water-status query, its count of buildings without water | Count |
| cmdGetCurrentDate | Simulation calendar (`14-sim-clock.md`) | vYear ← year, vMonth ← month, vDay ← day (the calendar query returns month, day, year; the command stores them in year, month, day order) [STATIC]; month/day numbering per `14-sim-clock.md` |
| cmdGetMonthsElapsed | Scenario engine's own record of the start date (§8) together with the current calendar | Months (see table above) |

The "city-wide summary" values map to these figures of the owning documents [INFERRED, high
confidence: the value widths and signedness match]:

| Command | Figure |
|---|---|
| cmdGetLandValue | `avgLV`, the city average land value of the last complete pass (`15-sim-growth.md` §6.3) |
| cmdGetCrimeValue | `avgCrime`, the average of the last completed crime sweep (`17-sim-services.md` §7.3) |
| cmdGetAura | `cityAura`, the last finalised city aura (`17-sim-services.md` §8.5) |
| cmdGetPollutionValue kind 0 / 1 | `avgAir` / `avgWater` of the last pollution sweep (`17-sim-services.md` §7.4) |
| cmdGetPollutionValue kind 2 | ⌊Σ standing garbage ÷ N⌋ truncated to 16 bits (u16), from the last pollution sweep. Σ runs over every tile's standing-garbage value (`17-sim-services.md` §7.2.1, low 15 bits) except tiles of terrain/zone type 0x11. N is the same divisor as kinds 0/1, so this is the mean standing garbage per counted tile. It is 0 when N = 0 and is held between sweeps. Any kind other than 0–2 returns 0 and leaves the variable unchanged. [STATIC; meaning of N and type 0x11 INFERRED] |

The scenario engine only forwards these values; it never recomputes them.

### 4.7 Dialogs and win/lose

* **Start/progress dialog**: shown when the scenario starts and on demand from the scenario-status
  button; text from `cmdMakeDialogText`, goals listed from the goal map with met/unmet state, title
  from header line 4. [STATIC][INFERRED layout]
* **Pop-up** (`cmdPopUpMsg`): pauses the simulation, substitutes tokens (§6.1), shows the text with the
  presenter portrait; resumes the simulation when closed. One of its buttons opens the
  start/progress dialog. [STATIC]
* **End**: there is no implicit win/lose test — the scripts decide (typically a monthly or daily
  repeat script compares statistics, marks goals, and calls `cmdEndScenario` with a rank, or a
  timeline command at the time limit does so). `cmdEndScenario` shows the **results dialog** with
  the rank title (from `cmdAddRankInfo`) and the `cmdSetResultsDlgText` texts. The results dialog has
  three outcomes: (0) reload this scenario, (1) open the load dialog, (2) leave the scenario and keep
  playing the city. [STATIC][INFERRED outcome labels]
* Time limits are expressed purely by timeline delays or `cmdGetMonthsElapsed` comparisons. [DATA]

#### 4.7.0 Non-advisor presenters [STATIC unless marked]

**Kind 0, the game-side table.** This is a fixed list built into the game; it is not an INI or a
resource table. For presenter ID P it yields:
* picture `62B9DA24:52DB60A0:<pic>` (the advisor-portrait group);
* the name string from table `041F2625`;
* the title string from table `826DD2D0`.

Rows below are an identification aid for the built-in presenters (label only; not needed by an
implementation beyond the picture and string-table lookups named above). Name and title are string
instances in the two tables.

| P | pic | name | title |
|---|---|---|---|
| 2 | FF01 | 0x13 | 0x01 |
| 3 | FF02 | 0x14 | 0x02 |
| 4 | FF12 | 0x15 | 0x1D |
| 5 | FF07 | 0x16 | 0x15 |
| 6 | FF0B | 0x17 | 0x16 |
| 7 | FF08 | 0x18 | 0x22 |
| 8 | FF0E | 0x19 | 0x0C |
| 9 | FF15 | 0x1A | 0x1B |
| 0x0A | FF11 | 0x1B | 0x14 |
| 0x0B | FF0C | 0x1C | 0x28 |
| 0x0C | FF0F | 0x1D | 0x63 |
| 0x0D | FF09 | 0x1E | 0x8F |
| 0x0E | FF13 | 0x1F | 0x74 |
| 0x0F | FF06 | 0x20 | 0x85 |
| 0x10 | FF0A | 0x21 | 0x78 |
| 0x11 | FF0D | 0x22 | 0x10 |
| 0x13 | FF04 | 0x23 | 0xD7 |
| 0x14–0x17 | FF03, FF05, FF10, FF14 | the mayor name of neighbour 0–3 | title 0xA5 with `%NEIGHBOR%` replaced by that neighbour's name |

Other IDs (0, 1, 0x12, ≥ 0x18) give empty strings and no picture. [OBSERVED 800 × 600]
Separation Anxiety, the only shipped scenario with a kind-0 default presenter (`2 0`), shows a
petitioner portrait and presenter name in its start dialog.

**Kind 2.** The row of §2.6 gives picture `62B9DA24:6504691D:P`, and the name and title strings
come from the scenario string table.

**Drawing (kinds 0 and 2).** The picture is cropped from its top-left corner to at most 94 × 117
(advisor portraits are not clipped). It is placed horizontally centred in the dialog's presenter
column, with the name below it, centred, in the standard small UI font. The title string is passed to the pop-up
as its window caption `[INFERRED]`. The start and results dialogs build it but never show it.
Pixel positions of portrait and name in each dialog: §4.7.1.

* When the picture is missing, no portrait is drawn and the name starts at the top of the column.
* Observed defect: for kind 2 (and any kind ≥ 3), the pop-up shows no portrait. Start and results
  dialogs are not affected.
* Observed panel at 800 × 600 (start dialog, kind 0), identical to the advisor case. Screen
  coordinates; the window client origin is at the screen origin:
  * inset fill (123,146,255), with one-pixel border lines (173,182,255) on the left at x = 512, on
    the right at x = 632 and at the bottom at y = 295; there is no top border line, and the fill
    continues up to the title area;
  * portrait frame: a dark line (66,65,66) on the top at y = 139 and on the left at x = 526, and a
    white line on the right at x = 617 and at the bottom at y = 253;
  * name ink rows 271–281, x 527–616, colour (49,52,123);
  * no title line.

#### 4.7.1 Observed layouts at 800 × 600 `[OBSERVED]` (A Better Betterfeld)

Coordinates are window client pixels, as (left, top)–(right, bottom), right/bottom exclusive. All
three dialogs use the standard light-blue dialog skin (frame/panel fill RGB 123,146,255; light
bevels).
* Every inset area is filled with RGB 222,223,255, **not white**: the text boxes, the goals list,
  the results panel, the results text box and the three button panels.
* Text in the text boxes is black.
* The goals list text and the achieved rank are dark blue RGB 49,52,123. Ranks not achieved are
  RGB 165,162,165.
* Body text lines are on a **14 px pitch** (start/status text tops at y 146, 160, 174 …); goal
  texts wrap on the same pitch.

Art [STATIC: keys; OBSERVED: placement]:
* Goal status icon, unmet: `DLG.IXF` group `C2BF3646` instance `252A754F` (32 × 29), drawn at
  (184, 333 + 40·row).
* Goal status icon, met: instance `E50ED85A` (38 × 33, the red "SimCity" stamp). The same image
  marks the achieved rank in the results dialog, at (197,243) there.
* The goals list chooses icon and text colour per goal from its met flag.
* Results-dialog logo button: instance `C5171C41`, 292 × 34 = four 73 × 34 cells (0 disabled
  grey, 1 normal, 2 pressed, 3 hover). Normal is cell 1 at (220,446), (362,446), (504,446), i.e.
  centred in each 134 px panel.
* Pop-up icon: `SYS.IXF` MBIcons `82B9B75B:42E55FD4` cell 1 (the yellow warning triangle,
  32 × 29), at (235,239).
Each dialog centres itself in the window. All three pause the simulation while open: the date did
not advance with the game at top speed.

**Start dialog** (scenario start) and **status dialog** (Review ▸ Scenario Status):
* Frame (163,84)–(637,516). The title bar shows the scenario title.
* Upper-left panel (176,118)–(504,296):
  * Start dialog: greeting heading plus an editable mayor-name field (≈ (322,128)–(503,145)),
    then the text box (181,155)–(503,294) with a vertical scroll bar at x 487–503 when the text overflows.
  * Status dialog: status heading and no name field; the text box starts at y 140.
  * Text = the `cmdMakeDialogText` string, black on white.
* Presenter panel (513,119)–(633,296): portrait frame (527,140)–(618,254), about 91 × 114.
  Presenter name centred below it at y ≈ 271–280. Advisor presenters show their
  advisor portrait; no title line is drawn.
* Goals heading at y ≈ 312, then a goals list box (181,329)–(633,470). One row per goal
  on a 40 px pitch (first row top ≈ 333): a 28 px status icon at x 185–212 (unmet = black "!" on a
  white triangle), then the goal text at x ≈ 220 in two-line wrap.
* OK (check-mark) button ≈ (589,479)–(627,511) at the bottom right.

**Pop-up** (`cmdPopUpMsg`):
* Frame (204,181)–(595,418), 391 × 237. The title bar shows the presenter's dialog title
  (the advisor's name for an advisor presenter).
* Close box at the top right ≈ (564,190)–(582,207).
* Text panel (227,222)–(462,371): a 32 px icon at (234,237) (the yellow warning triangle), then
  the text from x 278, y ≈ 232, wrapped at x ≈ 445. A scroll bar at x 445–461 appears only when
  the text overflows.
* Presenter panel (470,221)–(591,371): portrait frame (485,222)–(576,336), name centred at
  y ≈ 348–358.
* Button strip (205,378)–(594,413): OK (check-mark) button ≈ (544,383)–(581,408).
* A pop-up does not end until OK or close is clicked. The next pop-up of a timeline appears in the
  same place.

**Results dialog** (`cmdEndScenario`):
* Frame (177,101)–(621,498), 444 × 397. The title bar shows the scenario title.
* Results panel (190,135)–(498,289):
  * completion heading (y ≈ 140–157);
  * a results line naming the mayor (y ≈ 162–173). The mayor's name replaces the `%YOURNAME%` token
    of the string; it is not appended;
  * the rank list: one line per rank (`cmdAddRankInfo`) at x ≈ 238, y ≈ 193, 212, 231…
  * Ranks not achieved are drawn in grey. The achieved rank is drawn in dark blue, may wrap to two
    lines, and has a red "SimCity" stamp icon at (195,250)–(235,276) to its left.
* Presenter panel (498,135)–(617,289): portrait frame (511,135)–(603,250), name centred at
  y ≈ 264–275. The portrait used the **mood** of `cmdSetResultsDlgText` (sad face for mood 4 with
  the lowest rank).
* Text box (190,304)–(617,393): the `cmdSetResultsDlgText` string (a results text string).
* Three button panels on a 142 px pitch, each 134 × 76: (189,406)–(323,482), (331,406)–(465,482)
  and (473,406)–(607,482).
  * Each has a caption **centred** and wrapped to two lines at y ≈ 415–440: replay, play another
    scenario, return to the main menu respectively.
  * Below the caption, the 73 × 34 "SimCity" logo button (art above) at y 446–480.
  * These are outcomes 0, 1, 2 of §4.7.
  * There is no close box or OK button.

## 5. Scenario-specific simulation modifications

There is no separate "rules" record: everything a scenario changes is either in the embedded city
save or done by script commands. [STATIC][DATA]

* **Starting conditions** (funds, date, ordinances, loans, zoning, buildings, neighbour connections,
  disaster settings…) are simply the state of the city records stored in the `.SNR`
  (`10-save-format.md`). The Scenario Creator copies a city (`.sc3`) into the scenario. [DATA]
* **Forced disasters**: `cmdStart*` commands, typically on the timeline (fixed time) or in repeat
  scripts guarded by conditions/random numbers (e.g. fire at coordinates drawn with
  `cmdGenerateRandomNum`). Parameters and effects: `21-disasters.md`. [DATA][STATIC]
* **Random disasters**: `cmdEnableDisableDisasters` turns the city's random-disaster generation off/on
  (unused in shipped scenarios). [STATIC][DATA]
* **Neighbour deals**: `cmdDisableNewNeighborDeals −1` (used by one shipped scenario) stops new
  neighbour-deal offers; `cmdEnableNewNeighborDeals` re-allows them (`19-sim-economy.md`). [DATA]
* **Rewards**: `cmdOfferRewardBuilding occupantID` makes a reward/landmark available. [DATA]
* **Speed**: `cmdSetScenarioSpeed speed lock` forces a speed and optionally locks the speed controls;
  the lock persists (§8). [STATIC]
* **Locked tools**: the command set contains **no** tool-locking or UI-restriction command, and no
  scenario record restricts tools. [STATIC] Shipped scenarios restrict nothing. [DATA]
* While a scenario is active the scenario-status button is shown, saves keep the run-time state
  (§8), and the simulation is paused while scenario dialogs are open. [STATIC]

## 6. Text linkage

* Each scenario owns one string-table group G (the "A47D8 family": `FA47D792`, `3A47D7D3`,
  `FA47D7E3`, `DA47D878`, `9A47D7F3`, `9A47D7FE`, `BA47D808`, `BA47D814`, `1A47D81F`, `3A4A6724`,
  `FA47D837`, `DA47D85B`, `3A47D82C`). Strings are type `2026960B`, group G, instance = string ID.
  [DATA]
* The localised text lives in `Res/Text/<LANG>/<file>.IXF` with the same G; the `.SNR` itself
  carries only the author string and some presenter strings. [DATA]
* Instance 0 = short description (chooser), 1 = title, 0x32–0x54 = standard presenter
  names/titles (pairs at 0x32/0x33, 0x35/0x36, … step 3), 0x65+ = script messages. [DATA]
* `TAICHUNG.IXF` (group `7A6C621F`) is a scenario string table with no shipped `.SNR`. [DATA]

### 6.1 Tokens in scenario text

The following tokens are substituted when scenario text is displayed [STATIC]:
`%MAYOR%`, `%YOURNAME%`, `%CITYNAME%`, `%YOURCITY%`, `%POPULATION%`, `%YEAR%`, `%PARADENAME%`,
`%ANYNEIGHBOR%`, `%SCNNAME%` (scenario title), `%SCNV<x> <n>%` (value of scenario variable n), and
`%TOKEN%` (generic). Substitution happens when a scenario string is displayed (pop-ups, dialogs,
ticker). [STATIC][DATA]

`%SCNV<x> <n>%` [STATIC]:
* **Language index source.** The language index is the game's language setting
  (default 1 = US English). At startup it is
  read from `HKLM\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited\Language`:
  * a DWORD < 0x2C is the language index itself;
  * a larger DWORD is a Windows LCID and is mapped to a language;
  * a string value is matched by language name.
  If the value is missing, an INI value (`Language` / `Default Locale`) is tried, then the
  default 1. Index 0 maps to 1. The `Country` registry value is separate (DWORD < 0x39). The
  index-to-language table is `27-platform.md` §1. An implementation should take the index from
  its own language setting.
* Parsing: the token text between the two `%` is read as a word and a decimal integer
  (`%s %d`). The first five characters must be `%SCNV`. The letter x is case-sensitive: `M`
  money, `N` number, `Y` year; any other letter leaves the token unchanged. The space before n is
  required (`%SCNVN5%` is left unchanged). n is not range-checked. Only `N` appears in shipped
  text. [DATA]
* `Y`: the plain signed decimal (`%d`), no separators. 2010 → `2010`.
* `N`: the integer (no decimals, nothing to round) with digits grouped in threes using the
  language's thousands separator, and a leading `-` when negative. Separator by language index
  (`27-platform.md` §1): `,` for 1, 2, 12, 15, 17–20; `.` for 3–9, 13, 14; space for 10, 11, 16.
  Language 11 puts the `-` after the digits. Languages 0, 14 and above 20 give an empty string for
  negative values (original quirk). US English: 1234567 → `1,234,567`, −1500 → `-1,500`.
* `M`: grouped digits as for `N` plus the currency symbol of the language: 1 `$`, 2 `£`, 3 and 7
  `F`, 4 `DM`, 5 `L.`, 6 `Pts`, 8 `kr`, and so on. The symbol goes before the number for
  languages 1, 2, 9, 11, 14, 16, 21–25, 29, 31 and 32, otherwise after. There is a space between
  symbol and number for languages 3–9, 11, 13, 14 and 16. A negative value gets `-` in front of
  the whole string. US English: 25000 → `$25,000`, −750 → `-$750`. [STATIC; the full per-language
  tables are not reproduced here; language 1 = US English is INFERRED]

## 7. `.SSC` — Scenario Creator project / template file

The game never reads `.SSC`. [STATIC] It is the save format of the
Scenario Creator, which compiles it to `.SNR`. Documented so that tools can read the shipped
templates. All **14** shipped files conform, including the section
length field. [DATA]

Primitive types: `u32` little-endian; **WSTR** = u32 byte length L (even) followed by L bytes of
UTF-16LE text, no terminator; **BLOB** = u32 length N followed by N bytes.

| Order | Type | Field | Meaning |
|-------|------|-------|---------|
| 1 | u32 | magic | `0x75CBCE6B` (bytes `6B CE CB 75`) |
| 2 | u32 | reserved | 0 in all files |
| 3 | u32 | version | 3 in all files |
| 4 | WSTR | name | Scenario name |
| 5 | WSTR | description | Chooser description |
| 6 | WSTR | pictureName | File name of the chooser picture |
| 7 | BLOB | picture | Complete Windows BMP file (24-bit; becomes `62B9DA24:19A6CEA1:1`) |
| 8 | u32 | reserved | 0 |
| 9 | WSTR | cityName | File name of the embedded city |
| 10 | BLOB | city | Complete `.sc3` IXF file (the scenario's starting city) |
| 11 | u32 | sectionLength | Byte count from just after this field to the end of the command table |
| 12 | u32 | sectionMagic | `0x012D8EDD` in all files |
| 13 | u32 | nextID | ID allocator / counter (138 or 381 seen) `[INFERRED]` |
| 14 | u32 | reserved | 0 |
| 15 | 8 × WSTR | settings | name; description; default presenter ID (decimal text, = header line 5 P); entry script name (`Main`); source city path; catalogue value a (decimal text); catalogue value b (decimal text); picture path |
| 16 | table | messages | u32 n; n × (u32 textID, WSTR notes, WSTR text, WSTR label) — dialog/pop-up texts |
| 17 | table | tickers | u32 n; n × (u32 textID, WSTR label, WSTR notes, WSTR text, WSTR a, WSTR b, u32 c) |
| 18 | table | variables | u32 n; n × (u32 variableNo, WSTR name, WSTR description) |
| 19 | table | goals | u32 n; n × (u32 textID, WSTR text, u32 goalNo, WSTR notes, WSTR label, s32 x (−1)) |
| 20 | table | ranks | u32 n; n × (u32 textID, WSTR text, u32 rankNo, WSTR label, WSTR notes) |
| 21 | table | presenters | u32 n (0 in all files). Row layout `[OPEN]`. The Creator's presenter table has the columns ImageID (long), NameID (long), TitleID (long), HardCoded (bool), Name (text), Title (text), Image (OLE bitmap), Description (text). The compiled `.SNR` row (§2.6) is (ImageID, NameID, TitleID) [DATA]. The `.SSC` rows probably serialise these columns in the order of the other tables (u32 for long, WSTR for text, BLOB for the image) [INFERRED]. A reader can test candidate layouts against the section length of field 11 |
| 22 | table | scripts | u32 n; n × (u32 scriptID, WSTR name, WSTR author, WSTR description) |
| 23 | table | commands | u32 n; n × (u32 position, u32 scriptID, u32 delay, u32 cmdId, WSTR comment, u32 argc, argc × (WSTR kind, WSTR value)) |

* `kind` is the text `Constant` or `Variable` (→ `.SNR` kinds 0 / 1); `value` is decimal text.
  [DATA]
* `position` orders commands within their script (rows are not stored in order). [DATA]
* IDs (textID, scriptID) are arbitrary 32-bit numbers chosen by the Creator; when compiling, it
  renumbers texts into the scenario string table and scripts into instances of
  `23DFAE5F:23DFAE8C`, and expands a script-ID argument into the pair `601861772 n`. [INFERRED]
* 4 of the 14 files (the ENGLISH and English-UK copies) have stale bytes after the command table
  (41 502 or 404 023 bytes); readers must stop at the end of the command table. [DATA]
* Values a/b of row 15 are `7`/`5` (three-goal template) and `7`/`1` (city-tour template), matching
  the catalogue record fields a/b of §2.7. [DATA]

### 7.1 Scenario Creator output

The Scenario Creator (an Access application; not to be reimplemented) produces a `.SNR` by taking the
embedded city IXF and appending the records of §2 (header, scripts, style table, presenter table,
catalogue, picture, author strings) plus per-language string tables written as separate
`_strings.IXF` files (which ship as `Res/Text/<LANG>/<Name>.IXF`). [DATA][INFERRED for the string
file step] Its IXF writer produces a tight index and string records whose index size omits the
4-byte length prefix (§2). [DATA]

## 8. Persisted state (scenario in progress)

When a city is saved while a scenario is active, the save keeps the scenario records of §2 (the set
of keys the game treats as scenario records is `04845CBE:C3DE4D66`, `A4846144:A48461A3`,
`44EB6BC8:44EB6BC9`, `62B9DA24:6504691D` (custom presenter pictures, instance = presenter ID),
`23DFAE5F:23DFAE8C`, `26E732E0:19A6CEA1`, `2026960B:19A6CEA1`, `62B9DA24:19A6CEA1`, all instances) and
adds one binary **run-time state record**. [STATIC]

Key: `206C6E7C:C3DE4D66:00000000`. No shipped file contains it. [DATA]

The record starts with the standard serial-record header of city records (`10-save-format.md`):
u16 version (written as 1), the flags/extra bytes, then u32 `0xDEADBEEF`; a reader accepts
"u16 version, u8 flags, one more u8 if flags bit 1 is set, u32 sentinel" and rejects the record if the
sentinel differs. [STATIC] A **key** below is 12 bytes: u32 Type, u32 Group, u32 Instance, in
that order. [STATIC] The key encoding is the same as for every other
embedded key in city records. [DATA] Its order is confirmed by the occupant records,
whose embedded keys decode as `207EDC0E:0000057E:<occupant ID>` in Type, Group, Instance order
(`10-save-format.md` §6.2). No shipped mid-scenario save exists to check this record directly.
Example: a repeat-list entry for script 3 is stored as the bytes of `23DFAE5F`, `23DFAE8C`,
`00000003`. Body, in order [STATIC]:

| Order | Size | Type | Field | Meaning |
|-------|------|------|-------|---------|
| 1 | 4 | u32 | clock | Scenario clock (§4.3) |
| 2 | 4 | u32 | startMonth | Month at scenario start |
| 3 | 4 | u32 | startYear | Year at scenario start |
| 4 | 2048 | s32 × 512 | variables | Variables 0–511 |
| 5 | var | 4 × (u32 count, count × key) | repeatLists | Lists 0,1,2,3 in order; keys are `23DFAE5F:23DFAE8C:n` |
| 6 | 4 | u32 | hasTimeline | Non-zero while the entry script still has unexecuted commands |
| 7 | 4 | s32 | cursor | Present only if hasTimeline ≠ 0: index of the last executed timeline command (−1 = none) |
| 8 | 12 | key | timelineScript | Present only if hasTimeline ≠ 0 |
| 9 | 4 | u32 | presenter | Default presenter ID (header line 5 P) |
| 10 | 4 | u32 | presenterMood | Default presenter mood/variant |
| 11 | 4 | u32 | presenterKind | Header line 5 K |
| 12 | 4 | u32 | goalCount | |
| 13 | 13 each | u32 goalID, u8 met, u32 G, u32 I | goals | |
| 14 | 4 | u32 | rankCount | |
| 15 | 4 | s32 | resultRank | Set by `cmdEndScenario` (−1 = none) |
| 16 | 12 each | u32 rankID, u32 G, u32 I | ranks | |
| 17 | 12 | key | dialogText | String key of the start/progress dialog text (zeros if none) |
| 18 | 12 | key | resultsText | String key of the results-dialog text |
| 19 | 1 | u8 | speedLocked | `cmdSetScenarioSpeed` lock |

On load with this record present the scenario resumes: the timeline continues at cursor + 1, lists,
goals and ranks are rebuilt, the start is not re-run. [STATIC] If the record fails to read, loading
the scenario fails. [STATIC] Which file extension the player saves to is a UI matter
(`09-ui-screens.md`) `[OPEN]`.

## 9. Tutorials

* The three tutorials (`Tutorial on Getting Started.SNR`, `Tutorial on Transportation.SNR`,
  `Tutorial on Sea and Air.SNR`) are ordinary scenarios; catalogue values a/b = 7/1. The game has
  no tutorial-specific logic or strings. [STATIC][DATA]
* Guided steps are a script state machine [DATA]:
  1. The entry script adds goals/ranks, sets dialog text, sets (and locks) the speed, and registers a
     **daily** repeat script.
  2. The daily script holds the step number in a variable. For the current step it tests a
     completion condition (typically `cmdCountStructuresNearLocation` at a fixed tile and radius, or a
     statistic compared with `cmdGreaterThan`), combined with flag variables (`cmdEqual`/`cmdAnd`).
  3. When the step is satisfied it shows the next instruction with `cmdPopUpMsg` (an advisor
     presenter), centres the view with `cmdMoveCamera`, sets flags with `cmdSetVariable`, and
     advances the step.
  4. Goals are marked as the player completes them; the final step calls `cmdEndScenario`.
* There is **no** UI highlighting or input-waiting primitive: "waiting for an action" means polling
  the city state once per game day. [STATIC]
* `Cities/TUTORIAL.SC3` is a plain city save (12 record groups like other `.sc3`), not named
  by the game. [STATIC][DATA]

## 10. Starter towns (`.st3`) and terrains (`.sct`) — game flow

* The file browser enumerates `Cities/StarterTowns/*.ST3`, `Cities/Terrains/*.SCT`,
  `Cities/Scenarios/*.SNR` and city saves separately; each is a different file category. [STATIC]
* Both are IXF city saves of the same record formats (`10-save-format.md`). Shipped record-group
  census (group IDs, `T:G`) [DATA]:
  * `.sc3` (15) and `.sct` (21): `E2F42628:62F42635`, `035F62A4:035F62A4`, `C359837E:43598384`,
    `035AD1CB:835AD1D1`, `00000FA1:617E198D`, `A1634F05:01E1A5E6`, `620EDFCE:A20FB3B9`,
    `430E2EE6:530E2EE6`, `0525A6B9:4525A6E2`, `79FDB6C8:59FDB6FD`, `99F47014:79F4702D`, and
    (10/15 and 2/21 files) `630E2EE6:730E2EE6`.
  * `.st3` (10): only the first eight of those; they lack `0525A6B9:4525A6E2`, `79FDB6C8:59FDB6FD`,
    `99F47014:79F4702D` and `630E2EE6:730E2EE6` — a reader must create those subsystems with
    default state.
* Opening a terrain or starter town starts a **new city** from that content (the player then names
  the city and mayor and saves as `.sc3`); the file itself is not overwritten by a normal Save.
  [INFERRED — see `09-ui-screens.md` for the dialogs]
* Opening an `.SNR` loads the city and starts the scenario (§4.2).

## Open questions

* Display names of the catalogue `scenarioType` codes (§2.7). They exist only in the Scenario
  Creator database, and the game does not read them.
* The 2 880-per-day scenario-clock rate during whole days of disaster (§4.3) follows from the
  documented tick order; it should be confirmed against the original by timing a timeline command
  during a long disaster.
* Formulas of the subsystem "city-wide summary" values that the `cmdGet*` queries forward (land
  value, crime, aura, pollution) are mapped in §4.6.1 [INFERRED]; the garbage summary is open. Air/water/garbage order of `cmdGetPollutionValue` kinds 0–2 is inferred.
* `cmdIsNeighborConnection` second argument (connection kind) and the deal-type numbering used by
  `cmdIsNeighborDealPresent` / `cmdDisableNewNeighborDeals` (0–5) need cross-checking with
  `19-sim-economy.md`. Business-deal ID numbering is likewise defined there.
* Appearance-key encoding of `cmdCountStructuresWithAppearance*` (a1, a2, a3): a Type, Group,
  Instance key. Shipped T = `0x207EDC0E` (the occupant attribute type), G = `0x57E`, I = occupant ID
  (§4.6). Whether other T values are accepted is `[OPEN]`.
* Layout and button labels of the start/progress, pop-up and results dialogs (`09-ui-screens.md`).
* Row layout of the `.SSC` presenter table (empty in all shipped files) and meanings of the free-text
  "notes"/"a"/"b"/"c" columns.

