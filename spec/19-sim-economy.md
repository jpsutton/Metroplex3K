# 19 — Simulation: Economy (budget, taxes, bonds, ordinances, deals)

Status: partial. Sections marked `[OPEN]` still need confirmation against the original.

Related documents: `14-sim-clock.md` (month/year ticks), `15-sim-growth.md` (RCI demand formula,
land value, the per-zone tax-revenue computation), `16-sim-utilities.md` (power/water/garbage),
`17-sim-services.md` (service facilities and how funding changes their effectiveness),
`18-sim-transport.md` (transit upkeep), `20-sim-population.md`,
`22-advisors-news-petitioners.md` (budget advisor, petitioners), `23-scenarios.md` (scenario
commands touching deals/ordinances), `10-save-format.md` (container of the persisted records).

## 1. Data sources

| File (inside `Apps/Sys/SYS.PAK`, optionally loose) | Sections used | Purpose |
|---|---|---|
| `SC3BudgetLayer.ini` | `[SpecialCosts]`, `[BuildingPlacementCosts]`, `[Bonds]`, `[TuningParameters]` | Placement prices, bonds, tax→land-value conversion, tax→demand table |
| `Ordinances.ini` | `[ORDINANCE_TRIGGERS]`, `[ListOfOrdinances]` | Ordinance catalogue and availability |
| `NeighborDeals.ini` | `[DEAL_RATE_CONSTANTS]`, `[BASE_DEAL_QUANTITIES]`, `[MINIMUM_DEAL_COSTS]`, `[DEAL_PENALTIES]` | Neighbour utility deals |
| `Advisor-Budget.ini` | `[TAX_RATE_CUTOFFS]`, `[TREASURY_NUMBERS]`, `[BUSINESS_DEALS]`, `[BUSINESS_DEALS2]` | Budget advisor thresholds and business-deal offers |
| `SC3Tune.ini` | per-facility `MonthlyUpkeep` / `OptimalMonthlyUpkeep`, `*OrdEffect` keys | Service upkeep and ordinance effect magnitudes (see `17-sim-services.md`) |
| `SC3WorldLayer.ini` | `EconomicPhases`, `ExternalJobDemand`, `JobToResFactor` | Outside-world economy (see §12 and `15-sim-growth.md`) |

Loading rule [STATIC]: for `SC3BudgetLayer` the engine first opens a loose file
`\Sys\SC3BudgetLayer.INI` relative to the application directory and then the packed copy in
`\Sys\SYS.PAK`; when a key exists in both, the packed copy is read last and wins. `NeighborDeals.ini`,
`Ordinances.ini` and `Advisor-Budget.ini` are loaded the same way. A missing key leaves the
built-in default.

`SC3BudgetLayer.ini` contains the key `TaxationDemandEffect` twice: once in `[Admin]` and once in
`[TuningParameters]`. Only the `[TuningParameters]` copy is read [STATIC]. The `[Admin]` copy is
dead data.

## 2. Money representation and treasury operations

* The treasury ("funds") is a signed 64-bit integer number of simoleons [STATIC].
* Every monthly and year-to-date budget accumulator is a signed 64-bit integer [STATIC], except
  the per-month bond-payment array (unsigned 32-bit) and per-department funding (8-bit percent).
* **Spend(amount)** (used for construction, placement, demolition, terraforming, etc.): if
  `funds − amount < 0` the purchase is refused and funds are unchanged; otherwise funds decrease.
  The original never lets an immediate purchase drive the treasury negative [STATIC]. Only the
  year-end settlement (§7) can make funds negative.
* **Add(amount)**: funds increase unconditionally (bond proceeds, cheats, scenario grants).
* After any change of funds the budget broadcasts the "funds changed" notification (no payload)
  [STATIC]. The UI refreshes the treasury display on it.
* **Free-placement mode** [STATIC]: the budget holds a flag that, when set, makes every placement
  and special cost (§13) return 0. It is off in normal play. Each time the player enters the
  cheat text `I am weak`, the flag flips and the cheat is acknowledged (the same acknowledgement
  as every other recognised cheat) [STATIC].
* **Budget flags and persistence** [STATIC]. The budget keeps three session flags, all cleared or
  re-derived when the budget is created or loaded, and **none of them is written to the budget
  save record** (Persisted state): free-placement (above); debt floor (step 11 of §7); and "show the year-end
  budget only when in debt" (step 13 of §7). Cheat effects on the budget are therefore lost on
  save/load.

## 3. Starting funds and new-city settings

When a new city is created the budget reads four values from the new-city settings supplied by
the city-creation dialog/scenario [STATIC]:

| Setting key | Meaning | Handling |
|---|---|---|
| 5 | Starting funds | Copied to the treasury (sign-extended to 64 bits). If absent, funds = 0. |
| `0x1E0` | Residential tax rate | Clamped to `[0, maxRate]` (`maxRate` = 22) |
| `0x1FB` | Commercial tax rate | Same clamp |
| `0x216` | Industrial tax rate | Same clamp |

The starting-funds amount per difficulty level is supplied by the new-city dialog, not the
budget [STATIC, from the city-creation dialog]: difficulty 1 (easy) → 50,000; 2 (medium) →
20,000; 3 (hard) → 10,000. The dialog stores the difficulty (1/2/3) in the new-city settings
alongside the funds. Tax rates, bond terms, ordinance costs and deal rates do not depend on the
difficulty [STATIC]. Outside the
economy, the difficulty choice also does the following in the new-city dialog [STATIC]: (i) it
sets the terrain generator's type (1/2/3) for the preview/random terrain (`11-world-terrain.md`);
(ii) choosing *hard* also sets one more boolean in the dialog's new-city parameters (clear for
easy and medium) [OPEN: meaning; candidates are disasters-on or no-starting-loan]. The simulation does not
use a stored difficulty value during play, and difficulty is not a stored setting [STATIC]. Treat difficulty as
affecting only the starting funds, the generated terrain and that one flag.
Shipped data [DATA, 59/59 files]: every terrain (`.sct`) and `TUTORIAL.SC3` stores funds 50,000
with rates 7/7/7; every starter town (`.st3`) stores funds 20,000 with rates 7/7/7; no shipped
file contains an outstanding bond. A city started from a terrain or starter town begins with the
funds stored in that file unless the new-city dialog overrides them [INFERRED].

`[OBSERVED]` New cities created by the original:

| Difficulty | Funds | Bonds |
|---|---|---|
| Easy | 50,000 | none |
| Medium | 20,000 | none |
| Hard | 10,000 | exactly one outstanding bond |

The Hard bond is stored in the budget record as (1, founding year + 10, 10,000, 12,000, 12,000, 0,
100). That is a 10,000 principal repaid at 100 per month over 120 months (12,000 in total), which
matches the §8 formula with the shipped `[Bonds]` values. The bond makes the budget record 25 bytes
longer (2,006 bytes instead of 1,981). This is the only bond observed in any file, and it
confirms the bond layout of the Persisted state section.

`[OBSERVED]` Yearly costs:
* A lone police station cost §240 a year, i.e. 20 a month at 100 % funding, charged on 1 January.
* With Auto Budget **unchecked** and funds positive, no budget window opened at the year end
  (two year ends observed).

## 4. Taxes

### 4.1 Tax groups

There are exactly three tax groups [STATIC], always in this order:

| Index | Zone family (agent type) | Default rate | Max rate |
|---|---|---|---|
| 0 | Residential (3000) | 7 % | 22 % |
| 1 | Commercial (5000) | 7 % | 22 % |
| 2 | Industrial (8000) | 7 % | 22 % |

The rate is an integer percentage 0–22 stored in one byte. There are no separate per-wealth tax
rates in the budget model of this game [STATIC — the persisted record carries exactly three rates
and the record count field is always 3].

### 4.2 Tax rate → demand effect table (`TaxationDemandEffect`)

`[TuningParameters] TaxationDemandEffect` in `SC3BudgetLayer.ini` is a `;`-separated list of
entries `a,b,n,P` [STATIC]:

| Field | Type | Meaning |
|---|---|---|
| `a` | integer | Effect at 0 % tax, in units of 1/10,000 (`3000` → +0.30) |
| `b` | integer | Effect at 22 % tax, in units of 1/10,000 (`-7000` → −0.70) |
| `n` | integer, clamped to ≤ 22 | Neutral tax rate (effect 0) |
| `P` | integer | Population breakpoint this entry applies from |

Entries are stored keyed (sorted) by `P`. For each entry the engine pre-computes a 23-element
table `E_P[r]`, `r = 0…22`:

* for `r < n`: `E_P[r] = (n − r) · a / n`
* for `r = n`: `E_P[r] = 0`
* for `r > n`: `E_P[r] = (r − n) · b / (22 − n)`

(single-precision float arithmetic; `a` and `b` already scaled by 0.0001).

At every month-end, before taxes are collected, the active table `E[r]` is rebuilt from the
current city population `pop` [STATIC]:

1. Find the entry with the largest `P_lo ≤ pop`. If none exists (only possible if no entry has
   `P = 0`), leave the previous table unchanged.
2. If `pop = P_lo` exactly, or there is no entry with a larger `P`, copy `E_{P_lo}`.
3. Otherwise, with `P_hi` the next larger breakpoint:
   `E[r] = E_{P_lo}[r] + (E_{P_hi}[r] − E_{P_lo}[r]) · pop / (P_hi − P_lo)`.

   **Original defect:** the interpolation uses `pop`, not `pop − P_lo`, so between breakpoints the
   value overshoots towards (and past) the upper table. Reproduce it for faithful demand.

The demand subsystem queries `E[rate]` for each zone family's current rate (rate clamped to 22)
[STATIC]. How the value enters the RCI demand equation is specified in `15-sim-growth.md`.

The budget advisor compares these per-zone effects with `Advisor-Budget.ini`
`[TAX_RATE_CUTOFFS] TaxGrowthThreshold` and `TaxDeclineThreshold` (both scaled by 0.0001, so
shipped 500 → +0.05 and −1000 → −0.10) to decide whether to comment that taxes are encouraging
or choking growth [STATIC].

### 4.3 Land-value → tax conversion (`LV…TaxTransmogrifier`)

`[TuningParameters]` keys `LVToResTaxTransmogrifier`, `LVToComTaxTransmogrifier`,
`LVToIndTaxTransmogrifier` are read and multiplied by 0.0001 (shipped 35/45/50 → 0.0035,
0.0045, 0.0050). `MinAverageLV` is read and clamped to 0–255. They are the per-group tax factor
`T_k` and the land-value floor of the revenue formula in §5.1 [STATIC].

## 5. Revenue

### 5.1 Tax revenue formula

Tax is **not** summed per building or per tile. It is a single product per tax group, evaluated
once per month-end (and also whenever the budget window asks for the "current month" figure)
[STATIC; verified against shipped saves, see below]:

  `revenue_k = trunc( floor(rate_k × N_k / 100) × L′ × T_k )`

| Symbol | Meaning |
|---|---|
| `k` | tax group 0 = R, 1 = C, 2 = I |
| `rate_k` | the group's tax rate, integer percent 0–22 (§4.1) |
| `N_k` | **zone total** of group `k` (below): residents for R, commercial units for C, industrial units for I |
| `L` | the city's **average land value**, an integer 0–255 held by the land-value layer (the `avgLV` byte of its record, `15-sim-growth.md` §6 / land-value record) |
| `L′` | `L`, except that when `N_k > 0` and `L < MinAverageLV` it is raised to `MinAverageLV` (`SC3BudgetLayer.ini [TuningParameters]`, 0–255; shipped 35) |
| `T_k` | `LVToResTaxTransmogrifier`, `LVToComTaxTransmogrifier`, `LVToIndTaxTransmogrifier` × 0.0001 (f32) |

Arithmetic, in order:
1. `floor(rate_k × N_k / 100)` is 32-bit signed integer division (truncation toward zero).
2. That integer is multiplied by `L′` in 32-bit integer arithmetic; the 32-bit product is then
   treated as an **unsigned** value.
3. The product is converted to floating point, multiplied by the single-precision `T_k`, and
   truncated toward zero to a 64-bit integer.

**Zone totals `N_k`** [STATIC] are accumulated by the valve layer (`15-sim-growth.md` §3.3)
during its per-building pass. For every building that is in the *occupied* state (buildings
under construction or abandoned are skipped) and for each item of its
`SC3ValveLayer.ini [AgentValveEffects]` entry whose valve is 3000, 5000 or 8000:

  `N_valve += footprintTiles × supply`

provided the building's agent type belongs to that zone family [INFERRED from the per-family
membership test the valve layer performs; high confidence]. `supply` is the item's signed 16-bit
supply value (residents per tile for R, e.g. 20/23/73/157; commercial or industrial units per
tile for C and I). Unlike the valve supply itself, this total is **not** zeroed for a building
that currently lacks the services which suppress its valve contribution. At the end of each
valve cycle the accumulating totals are latched into the "last" totals and cleared; the budget
always reads the last latched totals. Consequences:
* `N_R` is the residential head-count; `N_C` and `N_I` are approximately the commercial and
  industrial job counts (the 7000 demand per tile in the same entries is slightly lower).
* Civic, park, airport, seaport and other non-zone buildings pay no tax, because they carry no
  3000/5000/8000 *supply* items. (Airport/seaport/spaceport items are on 5001/8001.)

**Validation** [DATA]: The analysis team recomputed the formula for every shipped
`.sc3`/`.SNR` city, approximating `N_k` by the saved valve `S_last` of valves 3000/5000/8000 and
`L` by the saved `avgLV`. In Europolis (R), Madrid (R), Craterville (C) and Frankfurt (C, I) the
prediction equals one of the twelve saved monthly figures exactly; in all 14 cities every group
is within the month-to-month spread of its saved history (e.g. Madison R: predicted 2,875,
saved 2,820–2,904).

### 5.2 Other income streams

* **Transit fares ("other income" line)** [STATIC]: the traffic simulator's fare total for the
  month just completed (a float accumulated per transit trip, `18-sim-transport.md` §7.3),
  truncated toward zero. Shipped Madison shows 32–47 per month.
* Ordinance income, neighbour-deal income and business-deal income: §9, §10, §11.
* Miscellaneous income credited directly during the month: §7 step 7.

## 6. Expenses and funding

### 6.1 Departments (funded services)

Services whose budget can be adjusted appear in the budget as **departments**.
Each department is identified by a 32-bit ID and provides:

* `fullCost` — its monthly cost at 100 % funding (a signed 64-bit value, computed by the
  owning subsystem from its facilities and network tiles; see the owning documents);
* `funding` — an integer percent stored in one byte (shipped saves show 101–108; default 100).

Monthly charge [STATIC]: `charge = floor(funding × fullCost / 100)` (64-bit integer arithmetic).

Departments present in shipped saves (all six appear in every save that has them) [DATA for IDs].
The service mapping of the first four IDs is `[OBSERVED]`: each budget slider was set to a distinct
value in the original's budget window, the city was saved, and the funding byte of each department
entry was read back. The entries are stored in the order Education, Public Health, Fire, Roads,
Police, Mass Transit, which is also the order of the rows in the window. This **replaces** an
earlier inference (police, fire, health, education) that was wrong for all four.

| Department ID | Provided by | Service |
|---|---|---|
| `0x02922A5D` | health/education side | Education [OBSERVED; funding byte at entry offset +4; value 0 after the Education slider was set to its minimum] |
| `0x42922A6C` | health/education side | Public Health [OBSERVED; slider value 25 read back as 0x19] |
| `0x62963928` | public-safety side | Fire [OBSERVED; 57 read back as 0x39] |
| `0xA2963983` | public-safety side | Police [OBSERVED; 31 read back as 0x1F] |
| `0x8290F605` | traffic simulator | Roads: `fullCost = ceil(roadTiles × road cost) + ceil(highwayTiles × highway cost)` [STATIC, `18-sim-transport.md`]; slider value 76 read back as 0x4C [OBSERVED] |
| `0xC290F609` | traffic simulator | Mass transit: `fullCost = Σ station upkeep + ceil(railTiles × rail cost) + ceil(subwayTiles × subway cost)` [STATIC, `18-sim-transport.md`]; slider value 6 read back as 0x06 [OBSERVED] |

In a new city the six funding bytes are all 100 (0x64). The funding byte equals the slider
percentage; values 0 to 76 were observed, and shipped saves go up to 108, so the slider's maximum
is above 100 (the exact range is open).

Unknown department IDs found in a saved budget record are skipped on load (their bytes are
consumed); departments not present in the save keep their freshly registered defaults [STATIC].

The effect of funding on each service's efficiency is owned by the service documents
(`17-sim-services.md`, `18-sim-transport.md`). Facility upkeep figures come from `SC3Tune.ini`
(`OptimalMonthlyUpkeep`, `MonthlyUpkeep` per facility section).

### 6.2 Construction and placement costs

Immediate costs (§13) are charged with Spend() at the moment of the action and are not part of
the monthly budget lines.

## 7. Month-end and year-end processing

The budget runs on the simulation's month boundary (first day of the new month). It is the budget's
monthly step (`14-sim-clock.md` §6.1, monthly item 10), which runs the steps below; the year-end is part of the same run when the closing month is December.
It runs after the demographics, RCI and transit monthly agents (so the transit fares of step 6 are
the month just closed) and before the power and water steps and the city's month handler
[STATIC]. The full ordering is in `14-sim-clock.md` §6.3 rule 7. Let `(month, year)` be the new date, `month` 1–12. The **slot** of the month
just finished is `m = month − 2`, except that when `month = 1` the slot is `m = 11` and the
"budget year" is `year − 1` [STATIC]. All monthly arrays have 12 slots, slot 0 = January.

Steps, in order [STATIC]:

1. Rebuild the tax→demand table for the current population (§4.2).
2. For each tax group `k = 0,1,2`: `t_k` = monthly revenue (§5). `taxHist_k[m] = t_k`,
   `taxYTD_k += t_k`.
3. Ordinances (§9): `ordIncomeHist[m] = ordIncome`, `ordIncomeYTD += ordIncome`;
   `ordCostHist[m] = ordCost`, `ordCostYTD += ordCost`.
4. Neighbour deals (§10.3): `dealIncomeHist[m] = dealIncome`, `dealIncomeYTD += dealIncome`;
   `dealCostHist[m] = dealCost + penalties`, `dealCostYTD += that`; the deal manager's penalty
   accumulator is read and cleared [STATIC: the queried object is the neighbour-deal manager and
   the two sums are over odd and even deal types respectively].
5. Business deals (§11): `bizHist[m] = bizIncome`, `bizYTD += bizIncome`. `bizIncome` is asked
   for each month from the **Budget advisor** subsystem, which owns
   the business deals of §11 and reports the current monthly income of the deals in force [STATIC
   for the source; INFERRED that the value is the sum over accepted deals: shipped Madison shows a
   constant 300 per month, equal to the `MegaMall` value].
6. Transit fare income (§5.2; the "other income" line): `v` = last month's fare total,
   truncated; `otherHist[m] = v`, `otherYTD += v` [STATIC].
7. Miscellaneous income accumulated during the month by direct calls (`miscHist[slot]`, see
   below): `miscYTD += miscHist[m]`; then the next slot (`m+1`, or slot 0 when `m = 11`) is cleared
   to receive the coming month.
8. For each department: `charge = floor(funding·fullCost/100)`; `deptHist[m] = charge`,
   `deptYTD += charge`.
9. Bonds (§8): for each outstanding bond, `payment = monthlyPayment`; add to `bondYTD`; the bond's
   `remaining −= payment`, `paid += payment`. A bond is retired (removed, later bonds shift down,
   `totalPrincipal −= principal`) when `remaining = 0` or when (`bond.month = m` and
   `bond.maturityYear = budget year`). `bondPayHist[m]` = total bond payments this month (u32).
10. **Year-end settlement** — only when `m = 11` (i.e. on 1 January):
    * `funds += taxYTD_R + taxYTD_C + taxYTD_I + ordIncomeYTD + dealIncomeYTD + bizYTD + otherYTD + miscYTD`;
    * for each department: `funds += department year-end credit`, then `funds −= deptYTD`. The
      credit is a 64-bit value that each department supplies at year-end. Each department
      supplies its ID, its full monthly cost and this credit, and starts at 100 % funding
      [STATIC]. The service behind each department ID is given in §6.1. Only roads and transit are
      [STATIC]; police, fire, health and education are [INFERRED] there. The credit is 0 in every
      shipped save [DATA]. Its meaning is [OPEN]. A clone may use 0.
    * `funds −= ordCostYTD + dealCostYTD + bondYTD`;
    * every YTD accumulator is reset to 0.

    **Money therefore changes hands only once a year.** `[OBSERVED]` A small test city (two coal
    plants, pump, police, fire, hospital, one road and a residential zone, Auto Budget on) kept
    its funds unchanged from February to December. Funds dropped by the same §1,344 at each 1
    January (1902 and 1903), and no year-end budget window appeared. Construction purchases, by
    contrast, are charged at once (`25-rendering.md` §9.4). During the year the YTD figures are
    "owed" amounts shown in the budget window.
11. Debt floor: if the budget's "debt floor" flag is set and funds < −99,999, funds are set to
    −99,999 [STATIC]. The flag is clear at creation and load. Each entry of the cheat text
    `so sue me` toggles it [STATIC]. With the flag set the treasury
    can never reach the advisor's losing level of −100,000 (§7.1).
12. Treasury graph: push `(max(funds, −100000) + 100000) / 1000` (integer) as this month's sample
    (§14). On year-end also roll the monthly series into the yearly series; when
    `(budget year + 1) mod 10 = 0` also roll into the decade series.
13. Year-end budget pop-up: when `m = 11`, if `funds > −100000` and (the "show budget only when in
    debt" option is off, or funds < 0), open the budget window (`09-ui-screens.md`) [STATIC for the
    condition; INFERRED for the option's name].
    The option's value is taken from the player's game options when the budget is created, and it
    is updated whenever the player changes that option [STATIC].
14. Announce "funds changed" (the UI refreshes the funds display).

Estimated end-of-year figures shown in the budget window [STATIC], with `month` the current
calendar month (1–12): `estimate = YTD + currentMonthlyAmount × (12 − (month − 1))`, per line.
Total income/expense = sum of the lines above (taxes, ordinance income, deal income, business
income, other income, misc income / departments, ordinance cost, deal cost, bond payments).

### 7.1 Bankruptcy / losing

The budget layer itself never ends the game. The budget advisor reads
`Advisor-Budget.ini [TREASURY_NUMBERS] LOSE_GAME_LEVEL` (shipped 100000) and
`TREASURY_LOW` (shipped 1500) [STATIC]. Funds at or below `−LOSE_GAME_LEVEL` are the losing
condition (the mayor is thrown out); `TREASURY_LOW` drives low-funds warnings and the
business-deal offers (§11). The exact lose sequence (warning count, grace period) is owned by
`22-advisors-news-petitioners.md` [OPEN].

There is **no automatic budget balancing** [STATIC]. Any "auto-budget" option only affects whether the window pops up (step 13).

## 8. Bonds

`[Bonds]` in `SC3BudgetLayer.ini` [STATIC]:

| Key | Units | Use (shipped value) |
|---|---|---|
| `MaxBonds` | count | Maximum simultaneously outstanding bonds (10) |
| `MonthlyPaymentPer1K` | simoleons per month per 1,000 borrowed | Payment rate (10) |
| `MaxBondAmt` | thousands of simoleons | Largest single bond = `MaxBondAmt × 1000` (25 → 25,000) |
| `BondLifespan` | years | Term (10) |

Issuing a bond for a requested amount `x` [STATIC]:

1. `principal = floor(x / 5000) × 5000` (bonds come in 5,000 steps; the UI slider runs from
   5,000 to `MaxBondAmt×1000` in steps of 5,000).
2. Refuse if: outstanding count ≥ `MaxBonds`; or `principal = 0`; or `principal > MaxBondAmt×1000`;
   or `totalPrincipal + principal > MaxBondAmt×1000 × MaxBonds`.
3. Create the bond record: `month` = current month number (1–12 as returned by the calendar,
   stored in one byte), `maturityYear = currentYear + BondLifespan`, `principal`,
   `monthlyPayment = floor(MonthlyPaymentPer1K × principal / 1000)`,
   `totalRepayable = monthlyPayment × BondLifespan × 12`, `remaining = totalRepayable`, `paid = 0`.
4. `totalPrincipal += principal`; append the record; funds += principal (Add()).

With shipped values a 25,000 bond costs 250 per month for 120 months = 30,000 total. Payments are
charged monthly into `bondYTD` and settled at year end (§7). There is no interest-rate variation
and no early repayment [STATIC]. [OPEN] Whether the UI offers early repayment.

Retirement test note: the stored `month` is the 1-based calendar month at issue, while the month
slot `m` compared at month-end is 0-based; the original compares them directly. The practical
effect is that a bond retires at the month-end whose slot index equals the issue month number in
its maturity year, or earlier if `remaining` reaches 0 (after exactly `12 × BondLifespan`
payments). Reproduce as described.

## 9. Ordinances

### 9.1 Catalogue (`Ordinances.ini`)

`[ListOfOrdinances]` lines: `<ordinanceId> = <categoryId>;<year>;<nameStringId>;<costDivisor>`
(all integers; IDs in hex) [STATIC]:

| Field | Meaning |
|---|---|
| `ordinanceId` | 32-bit ordinance ID (key). Lines with ID 0 are ignored. |
| `categoryId` | Advisor/category that owns the ordinance (the panel tab and which advisor proposes it). Lines with category 0 are ignored. |
| `year` | 0 = available from the start. `9999` = becomes available only by a special trigger (§9.3). Otherwise the first year after which it may become available. |
| `nameStringId` | Instance ID of the ordinance name in string table group `0xC29A6083`. |
| `costDivisor` `D` | Population divisor for the cost/income formula. 0 is treated as 100. |

Category IDs used [DATA]: `0x828D0A2F`, `0x628D0C45`, `0xC28D0F40`, `0x028D0FC5`, `0xC28D0B6E`,
`0x022E288E`, `0x828D04EB` (advisor IDs; see `22-advisors-news-petitioners.md`).

`[ORDINANCE_TRIGGERS]` [STATIC] (defaults when key missing in brackets):

| Key | Use |
|---|---|
| `HOMELESS_SHELTER_PERCENT` (10) | Homeless percentage that must be exceeded to unlock the homeless-shelter ordinance |
| `EARTHQUAKE_PREP_BUILDINGS` (300) | Building count that must be exceeded to unlock earthquake retro-fitting |
| `TOURIST_PROMOTION_BUILDINGS` (25) | Commercial building count that must be exceeded to unlock tourist promotion |
| `AEROSPACE_AVAILABLE_YEAR` (1960) | Year that must be exceeded (with an airport) to unlock the aerospace incentive |

### 9.2 State

Each ordinance has two flags: **available** (shown in the ordinance panel) and **enacted**.
Enacting/repealing is a player action through the ordinance panel; it changes no money
immediately [STATIC].

### 9.3 Availability (monthly)

Each month, after costs are computed [STATIC]:

1. With probability 10 % (`rand(100) < 10`) perform the availability scan; otherwise stop.
2. Scan ordinances that are not yet available, in ascending ordinance-ID order. The first one whose
   condition holds becomes available, an "ordinance available" notification carrying the ordinance ID
   is announced (advisors and the UI react to it), and the scan stops (at most one new ordinance per month).
3. Conditions: if `year ≠ 9999`: `currentYear > year`. If `year = 9999`, by ordinance ID:

| Ordinance ID | Name string | Unlock condition |
|---|---|---|
| `0x20D07175` | 125 | city homeless percentage > `HOMELESS_SHELTER_PERCENT` |
| `0x22F6E7F4` | 155 | at least one airport (agent type 12310) and `currentYear > AEROSPACE_AVAILABLE_YEAR` |
| `0x62F6E7CF` | 148 | commercial buildings (agent type 5000 and descendants) > `TOURIST_PROMOTION_BUILDINGS` |
| `0xA2BF1DE5` | 129 | rail stations (12230) + bus stations (12210) + subway stations (12360) > 2 |
| `0xC2D1B025` | 146 | bus stations + subway stations > 2 |
| `0xC2BF1DC5` | 120 | police stations (13130) > 0 |
| `0xC2BF1DD1` | 131 | "building count" > `EARTHQUAKE_PREP_BUILDINGS`, where building count = residential (3000) + commercial (5000) + industrial (8000) + rail stations + subway stations + libraries (14130) + police stations + airports + bus stations |
| any other | — | never (only via scenario command or cheat) |

Counts are numbers of buildings of the given agent type including its descendants in
`CitySim.ini [AgentTypeHierarchy]`. A cheat makes every ordinance available at once (announcing "ordinance available" for each).

### 9.4 Monthly cost or income

Let `P` = total city population and `W` = a quantity obtained from the outside-world layer
(the world layer of `SC3WorldLayer.ini`, save record `206C6E7C:E11BDDF6`) [OPEN]. The three ordinances that use `W` are `0xC2BF1DD1`,
`0x62BF1DAA` and `0x82B9999B` [STATIC]. Which statistic `W` is remains unknown.
Save evidence bounds `W` [DATA]: in shipped cities with one or more of these ordinances enacted,
the whole monthly ordinance cost stays small. Examples: `Madison, WI.sc3` has `0x62BF1DAA` (D 959)
enacted with a total ordinance cost of 514–530 per month. `Liverpool, England.sc3` has two
enacted, with a total of 1,023–1,086 per month. So `W/D` is a few hundred at most, and `W` is a
city-scale statistic of the same order as the population, not a large constant. Implementers
should make `W` configurable (default: total city population `P`) and test against the
original. Integer division throughout. For each
ordinance that is both available and enacted, the monthly value `v` is (positive = income,
negative = cost) [STATIC]:

| Kind | Formula |
|---|---|
| Standard cost | `v = −(floor(P / D) + 1)` |
| Income | `v = floor(P / D)` |
| Cost plus facility count | `v = −(floor(P / D) + 1 + N)`, `N` = count of a facility type |
| World-based cost | `v = −(floor(W / D) + 1)` |
| Free | `v = 0` |

Totals: `ordIncome = Σ v (v > 0)`, `ordCost = Σ −v (v < 0)`. These feed §7 step 3.

### 9.5 Ordinance table

Labels below are descriptive paraphrases for identification; the in-game names come from string
table `0xC29A6083`. "Effect key" names the tuning key (and file) through which the owning
subsystem applies the effect.

| ID | Cat. | Year | Str | D | Money | Effect / owner |
|---|---|---|---|---|---|---|
| `0xC2BF1DC5` | `0x828D0A2F` | 9999 | 120 | 8000 | cost + police stations | youth curfew → crime (`17-sim-services.md`) |
| `0xA0D07129` | `0x628D0C45` | 0 | 121 | 1050 | income | legal gambling → crime up, income (`17-sim-services.md`) |
| `0xE0D07233` | `0xC28D0F40` | 0 | 122 | 8000 | cost + libraries | reading programme → EQ (`20-sim-population.md`) |
| `0x815B4CEF` | `0xC28D0F40` | 0 | 123 | 14000 | cost | youth sports → crime/health |
| `0x015B4D1E` | `0x028D0FC5` | 0 | 124 | 1550 | income | parking fines → traffic (`SC3Aura.ini ParkingFineOrdEffect`) |
| `0x20D07175` | `0xC28D0B6E` | 9999 | 125 | 7250 | cost | homeless shelters → land value (`SC3LandValue.ini HomelessShelterOrdEffect`) |
| `0xC2BF1DEF` | `0x828D0A2F` | 0 | 128 | 2007 | cost | school crossing guards → health quantity x_H (`17-sim-services.md` §5.3); the aura does not read it |
| `0xA2BF1DE5` | `0x028D0FC5` | 9999 | 129 | 1259 | cost | shuttle service → transit use |
| `0x02BF1DFA` | `0x022E288E` | 1960 | 130 | 4125 | cost | water conservation → water demand (`16-sim-utilities.md`) |
| `0xC2BF1DD1` | `0xC28D0B6E` | 9999 | 131 | 900 | world-based cost | earthquake retro-fitting → quake damage (`21-disasters.md`) |
| `0x82E42E30` | `0x022E288E` | 1930 | 132 | 8593 | cost | stairwell lighting → power demand (`SavingDueToStairwellLightingOrdinance`) |
| `0xA2BF1E43` | `0x828D04EB` | 1950 | 133 | 4587 | cost | clean air → pollution (`CleanAirOrdEffect`) |
| `0xE2BF1E2A` | `0x028D0FC5` | 1950 | 134 | 10443 | cost | alternate-day driving (`SC3Aura.ini AltDrivingOrdEffect`) |
| `0x40D07236` | `0xC28D0F40` | 1960 | 135 | 10006 | cost | free clinics → health |
| `0x22BF1E35` | `0x828D04EB` | 1960 | 136 | 9999 | free | leaf-burning ban (`LeafBurningBanOrdEffect`, fire + pollution) |
| `0x00D0723D` | `0xC28D0F40` | 1970 | 137 | 33057 | cost | CPR training → health/life expectancy |
| `0x62BF1DAA` | `0x828D0A2F` | 1970 | 138 | 959 | world-based cost | smoke detectors (`[FlammabilityLayer] SmokeDetectorsOrdEffect`) |
| `0x62BF1DB9` | `0x828D0A2F` | 1970 | 139 | 18613 | cost | neighbourhood watch → crime |
| `0xA2BF1DDC` | `0x028D0FC5` | 1980 | 141 | 1531 | cost | carpool incentive → traffic |
| `0x42BF1E18` | `0x828D04EB` | 1980 | 142 | 13768 | cost + recycling centres | trash presort (`TrashPresortOrdEffect`) |
| `0xC2BF1E04` | `0x828D04EB` | 1980 | 143 | 12583 | cost | car smogging (`CarSmoggingOrdEffect`) |
| `0xE0D07239` | `0xC28D0F40` | 1990 | 144 | 1645 | cost | public smoking ban → health |
| `0xE0D0722E` | `0xC28D0F40` | 1990 | 145 | 39187 | cost | nuclear-free zone (`SC3Aura.ini NukeFreeOrdEffect`; nuclear plants disallowed) |
| `0xC2D1B025` | `0x028D0FC5` | 9999 | 146 | 1000 | free | subsidised mass transit |
| `0x82B9999B` | `0x022E288E` | 1970 | 147 | 2364 | world-based cost | power conservation (`SavingDueToPowerConservationOrdinance`) |
| `0x62F6E7CF` | `0xC28D0B6E` | 9999 | 148 | 3521 | cost | tourist promotion (`TouristPromotionOrdEffect`) |
| `0xA2F6E7DA` | `0xC28D0B6E` | 1915 | 149 | 3011 | cost | conservation corps (`ConservationCorpsOrdEffect`) |
| `0x02F6E7E2` | `0xC28D0B6E` | 1950 | 150 | 42659 | income | industrial pollutant fee (`IndPolluteFeeOrdEffect`) |
| `0x22F6E80C` | `0x828D04EB` | 1990 | 151 | 28703 | cost | landfill gas recovery (`LandfillGasRecoveryOrdEffect`) |
| `0xA2F917CF` | `0x828D04EB` | 1950 | 152 | 20828 | income | industrial waste disposal (`IndWasteDisposalOrdEffect`) |
| `0xE2F6E7EA` | `0xC28D0B6E` | 1930 | 153 | 2546 | cost | clean industry association (`CleanIndAssocOrdEffect`) |
| `0x02F6E7EF` | `0xC28D0B6E` | 1950 | 154 | 3345 | cost | electronics incentive (`ElectronicsIncentiveOrdEffect`) |
| `0x22F6E7F4` | `0xC28D0B6E` | 9999 | 155 | 3593 | cost | aerospace incentive (`AerospaceIncentiveOrdEffect`) |
| `0xA2F6E7F9` | `0xC28D0B6E` | 1970 | 156 | 4256 | cost | electronics job fair (`ElectronicsJobFairOrdEffect`) |
| `0xE2F6E7FE` | `0xC28D0B6E` | 1980 | 157 | 3542 | cost | biotech incentive (`BiotechIncentiveOrdEffect`) |
| `0xC2F6E804` | `0xC28D0B6E` | 1980 | 158 | 3452 | cost | public access cable (`PublicAccessCableOrdEffect`) |
| `0x62F6E808` | `0x828D04EB` | 1970 | 159 | 17612 | cost | lawn chemical ban (`LawnChemBanOrdEffect`) |
| `0x22F6E814` | `0x828D04EB` | 1970 | 160 | 45349 | cost | backyard composting (`BackyardCompostingOrdEffect`) |
| `0x22F6E81B` | `0x828D04EB` | 1960 | 161 | 53517 | cost | paper reduction (`PaperReductionOrdEffect`) |
| `0xC2F6E81F` | `0x828D04EB` | 1940 | 162 | 39315 | cost | tire recycling (`TireRecyclingOrdEffect`, §13) |
| `0x62F6E82B` | `0xC28D0B6E` | 0 | 163 | 9999 | free | farmers' market (`FarmersMarketOrdEffect`) |
| `0xC2F90EB1` | `0x022E288E` | 1930 | 164 | 16521 | cost | water meters → water demand |

`[OBSERVED]` Monthly costs shown in the Ordinances window for shipped Berlin (population 794,278,
February 2067), and in a new city with population 0. In the new city every cost ordinance shows
−§1 and every income ordinance §0.

* **Cost ordinances** show ⌊population / D⌋ + 1, which is the same as ⌈population / D⌉ for these
  values. Water Conservation shows 193 (794,278 / 4125 = 192.6), Stairwell Lighting 93, Crossing
  Guards 396, Public Smoking Ban 483 and Carpool Incentive 519.
* **Income ordinances** show ⌊population / D⌋ with no +1. Parking Fines shows +512, Legalized
  Gambling 756, Industrial Waste Disposal 38 and Industrial Pollutant Fee 18.
* **World-based cost ordinances** show ⌊W / D⌋ + 1, where W is **not** the population. Power
  Conservation shows 11 (D 2364), Smoke Detectors 27 (D 959) and Earthquake Retro-Fitting 29
  (D 900). All three fit one W between 25,200 and 25,892 for Berlin, and W = 0 in a new city. W is the total footprint in tiles of all occupants, filler included
  [INFERRED]. For Berlin that is 25,598, and it reproduces all three costs exactly: 11, 27 and 29. Every other
  candidate count misses: zoned tiles 24,555, non-filler footprint 18,783, surface pieces 19,220.

Values in the "Year", "Str" and "D" columns are the shipped data [DATA]; the "Money" column is
hard-coded per ordinance ID [STATIC]. Effect-key placement: `*OrdEffect` keys live in
`SC3Tune.ini [IndustrialZoneDeveloper]`/`[FlammabilityLayer]`, `SC3Pollution.ini`,
`SC3Aura.ini`, `SC3LandValue.ini`, `SC3PowerLayer.ini`/`SC3WaterLayer.ini`. The hard-coded cost
table also recognises IDs `0x62BF1E21` and `0x82BF1E0E` (fixed income +1) and `0xA2BF1F60`
(cost + police stations), which are not in the shipped INI and therefore inert.

## 10. Neighbour deals

### 10.1 Deal types

Six deal types, indexed 0–5 [STATIC]. Even types are deals where the **player pays** the
neighbour; odd types are deals where the **neighbour pays** the player.

| Type | Meaning | Rate key | Base quantity key | Units-per-1K key | Minimum-cost key |
|---|---|---|---|---|---|
| 0 | buy power from neighbour | `POWER_BUY_RATE` | `BASE_POWER` | `PowerUnitsPer1K` | `MIN_POWER_BUY` |
| 1 | sell power to neighbour | `POWER_SELL_RATE` | `BASE_POWER` | `PowerUnitsPer1K` | `MIN_POWER_BUY` |
| 2 | buy water | `WATER_BUY_RATE` | `BASE_WATER` | `WaterUnitsPer1K` | `MIN_WATER_BUY` |
| 3 | sell water | `WATER_SELL_RATE` | `BASE_WATER` | `WaterUnitsPer1K` | `MIN_WATER_BUY` |
| 4 | export garbage to neighbour (player pays) | `GARBAGE_EXPORT_RATE` | `BASE_GARBAGE` | `GarbageUnitsPer1K` | `MIN_GARBAGE_EXPORT` |
| 5 | import neighbour's garbage (player is paid) | `GARBAGE_IMPORT_RATE` | `BASE_GARBAGE` | `GarbageUnitsPer1K` | `MIN_GARBAGE_EXPORT` |

All keys are in `NeighborDeals.ini` (sections `[DEAL_RATE_CONSTANTS]`, `[BASE_DEAL_QUANTITIES]`,
`[MINIMUM_DEAL_COSTS]`, `[DEAL_PENALTIES]`), read as unsigned integers [STATIC].
`BASE_DEAL_PENALTY` is read but never used [STATIC].

### 10.2 Offer generation

When a neighbour connection of the right kind exists (power line, pipe, or road/rail/highway for
garbage; see `12-networks.md`), the deal manager may generate an offer of type `d` with a chosen
neighbour [STATIC; the offer schedule is in §10.6]:

1. **Rate** (simoleons per `BASE_*` units per month):
   `rate = BaseRate[d] + rand(DEAL_RATE_VARIABLE)` where `rand(n)` is uniform in `[0, n−1]`
   (RNG: `16-sim-utilities.md`/`14-sim-clock.md`, seeded from the clock).
   * Buy types (0, 2, 4): if an opposite (sell) deal of the same commodity already exists, the rate
     is raised to at least that deal's rate. If the resulting rate is not greater than the
     neighbour's reference rate for this commodity, no offer is made.
   * Sell types (1, 3, 5): the rate is raised to at least the neighbour's reference rate.
2. **Size in thousands of people**:
   * Buy types: `k = floor(cityPopulation × PercentPopForBuy / 100000)`, minimum 1.
   * Sell types: `k = floor(neighbourPopulation × PercentPopForSell / 100000)`, minimum 1.
3. **Quantity**: `units = UnitsPer1K × k`, rounded **up** to a multiple of `Base` (the
   `BASE_*` value for the commodity).
4. **Monthly amount**: `amount = floor(units × rate / Base)`.
   * Buy types: if `amount < MinimumCost`, the contract amount is `MinimumCost`.
   * Sell types: if `units` is less than the quantity the player currently has available to sell
     (or equal with an unchanged rate), the offer is dropped [INFERRED reading].
5. **Cancellation penalty**: `penalty = DEAL_PENALTY_MULTIPLIER × amount` (using the computed
   amount before the minimum is applied, for buy types).

### 10.3 Money flow

Each deal stores its type, its quantity `units`, its `rate`, its minimum cost, its cancellation
penalty, its current monthly amount and an age counter in months. At every budget month-end
[STATIC]:

* **Deal income** (odd types 1, 3, 5): for each active deal,
  `amount = floor(units × rate / Base[type])` (unsigned 32-bit), stored as the deal's current
  amount; the sum is the month's deal income (§7 step 4).
* **Deal cost** (even types 0, 2, 4): same formula, but if `amount < minimumCost` the amount is
  `minimumCost`; the sum is the month's deal cost.
* **Penalties**: the deal manager keeps a penalty accumulator. Its value is added to the deal-cost
  line and the accumulator is cleared when the budget reads it (§7 step 4).

Because the amount is recomputed every month from the stored `units`, a change of `units` changes
the bill from the next month-end on.

### 10.4 Quantity updates and failure to deliver

The utilities side reports the current quantity for each deal type (one active deal per type)
[STATIC for the rule; OPEN for the exact caller and quantity measure]:

* Buy types (0, 2, 4): the reported quantity is stored in the deal's `reportedQuantity` field
  (`10-save-format.md` §6.6). Correction: an earlier revision said it overwrites `units`; in
  480-day runs of the original, Moscow's buy deals kept `units` and the monthly amount unchanged
  while `reportedQuantity` followed the actual draw [OBSERVED]. Bill by `units` (§10.3).
* Sell types (1, 3, 5): if the deal's `units` is **less than** the reported value, the deal is
  marked terminated (age set to −1) **without** a penalty. [OPEN: which quantity the caller
  passes; the advisor text describes this as the contract being dropped when the city cannot
  deliver.]

### 10.5 Contract term and cancellation

* On acceptance an offer becomes a deal with age 0 [STATIC].
* **Player cancellation** of the deal of type `d` [STATIC]: if the deal's age is **below 60
  months**, it is marked terminated (age −1) and its `penalty` (§10.2 step 5:
  `DEAL_PENALTY_MULTIPLIER × amount`) is added to the penalty accumulator, i.e. charged as deal
  cost at the next month-end. If the age is 60 months or more it is simply removed without
  penalty. 60 months is therefore the effective contract term. The age is advanced by the deal
  manager's monthly run (§10.6): every deal whose age is not negative gets `age += 1` each month,
  before the rest of that month's deal processing [STATIC].
* Terminated deals (age −1) are excluded from the deal list shown to the player. No sweep removes
  them: they are erased only by the remove operation below, or by accepting a new deal of the same
  type. Billing ignores the age, so a terminated deal keeps paying until it is erased [STATIC,
  SIMMISC FUN_10018f95; cost side not checked].
* **Deal-made flag** [STATIC, SIMMISC]: one byte per (neighbour, deal type).
  * Accepting a deal sets it to 1 (FUN_10017d45). Accepting also erases any existing deal of the
    same type, without clearing that deal's flag.
  * The remove operation (FUN_10017f16) erases the active deal of type d and sets flag[n][d] = 0
    for all four neighbours. Only that type's byte is cleared.
  * Player cancellation runs cancel, then remove, so the flag is cleared. Declining an offer also
    runs remove.
  * Termination by age −1 alone keeps the flag.
  * Gating: a new offer of a commodity is impossible while a deal of that type exists, and a
    neighbour is skipped if either flag of that commodity (both directions) is set.
* **No automatic expiry**: nothing compares the age with 60 except cancellation. A deal past 60
  months runs until cancelled or until the connection is lost.
* **Monthly connection check** (FUN_10018cd5), for every deal with age ≥ 0, after age += 1:
  * type 0: no power-line connections to that neighbour;
  * type 1: no connections, or none of them powered;
  * type 2: no pipe connections;
  * type 3: no pipe connections, or none carrying water;
  * types 4/5: (not a land neighbour, or no road, rail or highway connections) and no seaport.
  On failure the deal is cancelled: age < 60 → terminated with penalty, flag kept; age ≥ 60 →
  removed, flag cleared.
* **Pending offers have no timer** [STATIC, SIMADV ~20314–20409]. Each utilities-advisor cycle
  re-tests the offer:
  * the offer is cleared if the city's shortage or surplus condition no longer holds, or no
    neighbour is eligible;
  * if it still holds and a petition is pending, the neighbour's connection is re-tested; on
    failure the offer is cleared and the petition withdrawn, otherwise the petition is
    refreshed.
  * The petition's own lifetime (spec 22 §3.13.1) also ends it [INFERRED].

### 10.6 Neighbours and offer eligibility

The deal manager keeps exactly **four neighbours**, one per map edge [STATIC]. At new-city
creation:

* each neighbour's population = `2000 + rand(13000)`, its population cap = 4,000,000, its
  "land neighbour" flag = 1;
* the land flag is cleared for the neighbour whose edge-midpoint tile fails the map test the
  manager applies (edge midpoints: `(0, H/2)`, `(W/2, 0)`, `(W−1, H/2)`, `(W/2, H−1)` for
  neighbours 0–3). The test is the **sea-water flag** of the pollution cell that contains the tile
  (`17-sim-services.md` §7.2.1 bit 30). A set flag clears the land flag, so a sea edge has no land
  neighbour [STATIC]. The Neighbors-window region image applies the same test at the same tiles
  (`11-world-terrain.md` §9.3);
* names are drawn from string table `0xC29A6083`: a distinct random instance `0x29 + rand(36)`
  per neighbour, plus a second distinct random instance `2 + rand(36)`.

**Monthly run of the deal manager** [STATIC]. The neighbours subsystem has its own monthly step, which
runs once a month on day 1, early in the monthly work (`14-sim-clock.md` §6.1, monthly item 2). Each run reads the current date and then:
1. if the month is January (the run of 1 January), applies the yearly neighbour population
   growth below;
2. then performs the monthly deal processing, which includes the age increment of §10.5.
[OPEN: which registration path puts this agent in the schedule, and so its exact position among
the other tier-4 agents.]

**Neighbour population growth** [STATIC], once a year on 1 January. It uses a fresh local
generator seeded from the high-resolution timer (`14-sim-clock.md` §8). Population is an
**unsigned** 32-bit value. For each neighbour 0–3 in order:
1. Twelve times: `pop ← (pop + rand(3800) − 1200) mod 2³²`.
2. Draw `q = rand(1000)`. If `q < 15`, `pop ← floor(pop / 2)`.
3. While `pop < 1000`: `pop ← pop + 500`.
4. While `pop > cap`: `pop ← pop − 10,000` (cap = 4,000,000).
Because of the unsigned arithmetic, a small neighbour that falls below zero in step 1 wraps to a
huge value. Step 4 then brings it down to just under the cap, which can produce a sudden jump to
about 3.99 million. This is the original's behaviour, and a faithful clone keeps it.

**Choosing a neighbour for an offer of type `d`** [STATIC]: if a deal of that type already
exists, none. Otherwise start at a random neighbour `r = rand(4)` and test `r+1, r+2, r+3, r+4`
(mod 4); the first neighbour that has no existing deal of either direction for that commodity
(per-neighbour flags for power, water, garbage) and passes the connection test below is used.

Connection test [STATIC]:

| Type | Requirement |
|---|---|
| 0 buy power | neighbour has ≥ 1 power-line connection |
| 1 sell power | as 0, and at least one of the city's power-line connection tiles to that neighbour is currently powered |
| 2 buy water | neighbour has ≥ 1 pipe connection |
| 3 sell water | as 2, and at least one pipe connection tile to that neighbour currently carries water |
| 4, 5 garbage | the city has a seaport (agent type 12330), **or** the neighbour is a land neighbour with ≥ 1 road, rail or highway connection |

**Offer schedule** [STATIC; see `22-advisors-news-petitioners.md` §3.13 for frequency,
conditions, petition variants and type order]: offers are not generated by the deal manager itself; they are raised
by the utilities advisor once per advisor cycle using
`Advisor-Utilities.ini [NEIGHBOR_DEAL_CONSTANTS]` (`LOW_TREASURY_LEVEL`,
`MINIMUM_DEAL_POPULATION`; `MIN_POWER_SELL`, `MIN_WATER_SELL`, `MIN_GARBAGE_IMPORT` are present
in the file but no reader was found). Scenario commands `cmdEnableNewNeighborDeals` /
`cmdDisableNewNeighborDeals` gate new offers (`23-scenarios.md`).

Utility effect: accepted buy-power/buy-water deals add capacity to the city's power/water
network at the neighbour connection; sell deals consume capacity; garbage export removes garbage
from the city's garbage total, import adds it. The capacity accounting is specified in
`16-sim-utilities.md` [OPEN: exact coupling].

## 11. Business deals

`Advisor-Budget.ini` [STATIC]:

* `[BUSINESS_DEALS] MONTHS_TILL_BUSINESS_DEAL` — number of **advisor evaluation cycles** (weeks,
  despite the key name) with low treasury before an offer. See the cadence note below.
* `[BUSINESS_DEALS2]` lines `Name = buildingType;year;value;messageId`:

| Name key | Building type | Year | Value | Message ID |
|---|---|---|---|---|
| `Casino` | 19003 | 1915 | 350 | 149 |
| `Prison` | 19004 | 1900 | 250 | 210 |
| `Toxic_Factory` | 19001 | 1950 | 400 | 16 |
| `MegaMall` | 19002 | 1970 | 300 | 206 |

Offer logic, evaluated by the budget advisor once per advisor cycle [STATIC]. **Cadence**: like
every advisor, the budget advisor sets a "cycle due" flag at the start of each simulated week
(Monday). On the next day boundary (Tuesday) it runs its evaluation and clears the flag
(`22-advisors-news-petitioners.md`). The evaluation therefore runs **once per week**, not once per
month. Every counter below counts weeks, including the one compared with
`MONTHS_TILL_BUSINESS_DEAL` [STATIC]. A clone that counts months would make offers about 4.3 times
too late.

1. Low-treasury counter `c` (in weeks): if `funds < TREASURY_LOW`, `c += 1`; else `c = max(c − 1, 0)`.
2. If no offer is pending, `funds < TREASURY_LOW`, population > 1000 and `c ≥ MONTHS_TILL_BUSINESS_DEAL`:
   pick one deal uniformly at random; it is offered if `year ≤ currentYear`, the deal is enabled
   (scenario scripts can disable deals), the city does not already have that business, and for
   the casino an additional permission check passes [OPEN: likely "gambling not banned"/ordinance
   state]. The offer is raised as advisor message `messageId`. Because the evaluation is weekly,
   an offer can appear in any week of the month.
3. If accepted, the business building is placed by the player (agent types 19001–19004, family
   19000) and pays monthly income [INFERRED: income line `bizIncome`, §7 step 5; shipped Madison
   shows a constant 300 per month, matching `MegaMall`'s value]. Side effects are carried by the
   building's own occupant attributes (pollution for the toxic plant, crime for the casino and
   prison, traffic/commerce for the mall) — see `13-buildings-occupants.md` [OPEN].

## 12. Neighbour cities and the outside economy

Neighbour cities are records held by the neighbour-deal manager; each neighbour has a population
figure that the deal formulas use (§10.2, sell types) [STATIC]. Neighbour population growth is in §10.6. How the outside economy (`SC3WorldLayer.ini` `EconomicPhases`,
`ExternalJobDemand`, `JobToResFactor`) feeds RCI demand, is owned by `15-sim-growth.md`. Taxes reach demand only through the table of §4.2.

## 13. Placement and special costs

`[SpecialCosts]` (simoleons per tile/action) [STATIC]: index → key: 0 `LowDensityZone`,
1 `MedDensityZone`, 2 `HighDensityZone`, 3 `LandfillZone`, 4 `AirportZone`, 5 `SeaportZone`,
6 `Dezone`, 7 `Demolish`, 8 `LevelTerrain`, 9 `RaiseTerrain`, 10 `LowerTerrain`,
11 `DecorativeWater`, 12 `DemolishRubbleTile`. Missing keys cost 0.

`[BuildingPlacementCosts]` lines `<agentType> = <cost> <comment>`: the first integer after `=` is
the cost; the remainder of the line is a comment. Cost lookup for an occupant of agent type `t`
[STATIC]:

1. Among all listed types, choose the one that is `t` itself or the nearest ancestor of `t` in the
   agent-type hierarchy (smallest hierarchy distance; ties: first in key order). The result is
   cached per `t`. If none matches, the cost is 0.
2. If the tire-recycling ordinance (`0xC2F6E81F`) is enacted and `t` is in the
   `TireRecyclingOrdEffect` list, `cost = cost − floor(cost × pct / 100)`.

`[TuningParameters] TireRecyclingOrdEffect = pct:type,type,…` — `pct` (must be ≤ 100) applies to
each listed agent type (shipped list: roads, highway, ramps, bridges and highway pieces).

## 14. Budget history and graphs

* Per-line monthly arrays (12 slots) and YTD accumulators — see §7 and Persisted state.
* Treasury graph series: monthly samples `(max(funds,−100000)+100000)/1000` (12 monthly, 10 yearly,
  10 decade slots), initial value 100 (i.e. funds 0). Stored as the chart block in the budget
  record.

## Persisted state

### Budget record — key `T=0xE11BCC69, G=0xA11BCC54, I=0`

Sub-record of the main city stream (see `10-save-format.md`). No version word, no `0xDEADBEEF`
sentinel. [DATA] Parsed and fully consumed in 59/59 shipped files (15 `.sc3`, 13 `.SNR`,
10 `.st3`, 21 `.sct`); every shipped record is 1,981 bytes (3 tax groups, 6 departments,
0 bonds). The bond block layout is therefore [STATIC] only.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 8 | s64 | funds | Treasury |
| 0x08 | 4 | u32 | taxGroupCount | Always 3 |
| … | 109 × 3 | — | taxGroup[3] | See tax group block |
| … | 4 | u32 | deptCount | Number of department blocks |
| … | 117 × n | — | dept[n] | See department block |
| … | 8 | s64 | ordCostYTD | |
| … | 8 | s64 | ordIncomeYTD | |
| … | 16 × 12 | s64,s64 | (ordCostHist[i], ordIncomeHist[i]) | Interleaved per month i = 0..11 |
| … | 4 | u32 | bondCount | |
| … | 25 × bondCount | — | bond[] | See bond block. Records beyond `MaxBonds` are read and discarded. |
| … | 8 | s64 | bondYTD | |
| … | 4 × 12 | u32 | bondPayHist[12] | |
| … | 8 | s64 | totalPrincipal | Sum of outstanding principals |
| … | 8 | s64 | miscYTD | |
| … | 8 × 12 | s64 | miscHist[12] | |
| … | 132 | — | treasuryChart | 12 × u32 monthly, 10 × u32 yearly, 10 × u32 decade, u32 counter/state |
| … | 8 | s64 | otherYTD | |
| … | 8 × 12 | s64 | otherHist[12] | |
| … | 8 | s64 | dealCostYTD | |
| … | 8 | s64 | dealIncomeYTD | |
| … | 8 | s64 | bizYTD | |
| … | 24 × 12 | s64×3 | (dealCostHist[i], dealIncomeHist[i], bizHist[i]) | Interleaved per month |
| … | 8 | s64 | dealPenaltyAccumulator | |

Tax group block (109 bytes; 4 + 1 + 96 + 8):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | zoneType | 3000, 5000, 8000 |
| 0x04 | 1 | u8 | rate | Percent 0–22 |
| 0x05 | 96 | s64[12] | hist | Monthly revenue by month slot |
| 0x65 | 8 | s64 | ytd | Year-to-date revenue |

Department block (117 bytes):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | deptId | §6.1 |
| 0x04 | 1 | u8 | funding | Percent |
| 0x05 | 8 | s64 | lastCharge | Most recent monthly charge |
| 0x0D | 96 | s64[12] | hist | Monthly charges by slot |
| 0x6D | 8 | s64 | ytd | |

Bond block (25 bytes):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | u8 | month | Calendar month at issue |
| 0x01 | 4 | u32 | maturityYear | Issue year + `BondLifespan` |
| 0x05 | 4 | u32 | principal | |
| 0x09 | 4 | u32 | totalRepayable | |
| 0x0D | 4 | u32 | remaining | |
| 0x11 | 4 | u32 | paid | |
| 0x15 | 4 | u32 | monthlyPayment | |

Mapping of "other", "deal", "biz" lines is [INFERRED] (§7).

### Ordinance record — key `T=0x41193C3A, G=0xE1193C2A, I=0`

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | count | |
| 0x04 | 6 × count | — | entries | `u32 ordinanceId`, `u8 enacted`, `u8 available` |

On load, entries whose ID is not in the current `Ordinances.ini` catalogue are ignored [STATIC].
[DATA] Present in 59/59 shipped files, always `count = 42` (256 bytes), matching the 42 catalogue
lines; saved order is ascending ordinance ID.

### Neighbours and neighbour deals — key `C2910E7D:82937B60:00000000`

Byte layout in `10-save-format.md` §6.6 [DATA 59/59]. Semantics
[STATIC unless marked]:

* **Four neighbours**, one per map edge: index 0 = edge x = 0, 1 = edge z = 0, 2 = edge x = W−1,
  3 = edge z = H−1. Each has a city name, a mayor name, a population, a population ceiling
  (4 000 000), an "exists" flag, connection counts per network and six "deal of type d was accepted
  with this neighbour" flags.
* **New city**: population = 2000 + RandInt(0, 12999) per neighbour; names are drawn from string
  table `C29A6083` (cities: instances 41–76; mayors: instances 2–37), with no repeats among the
  four; a neighbour does not exist when the terrain test at the midpoint of its edge fails
  [INFERRED: midpoint is water].
* **Connections**: when a network piece of kind k (1 road, 2 rail, 3 power, 4 highway, 5 pipe,
  6 subway) is registered on an edge tile, the tile is added to that kind's connection set (packed
  x·65536 + z) and the owning neighbour's count for kind k is incremented. Shipped data shows that
  counts can be lower than set sizes, so a loader keeps both as stored.
* **Deals**: two lists, active deals then pending offers. Accepting an offer moves it to the active
  list and sets the neighbour's flag for that deal type. Each deal records partner name and index,
  type (§10.1), monthly amount, quantity, rate, a penalty, a month counter and a year; see the
  field table for which meanings are still [OPEN].
* Registration path (and so the exact tier-4 position) of the deal manager's monthly agent (§10.6).

## Open questions

* Meaning of the department year-end credit (§7 step 10); 0 in all shipped data.
* World quantity `W` used by three ordinances (§9.4).
* The quantity passed in §10.4. (Resolved: the offer schedule, §10.6; the deal age advances in the
  neighbours' monthly step, §10.5 and `14-sim-clock.md` §6.1.)
* The casino permission check (§11). (Business-deal income source resolved: Budget advisor.)
* Meaning of the extra boolean that the *hard* difficulty sets in the new-city parameters (§3).
