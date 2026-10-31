# 20 — Simulation: Population, demographics, EQ, health, workforce

Status: the cohort, EQ, HQ, life-expectancy and strike models are decoded (STATIC), and the
persisted state is verified on all 59 shipped files. Workforce details are still partial.

Related documents: `15-sim-growth.md` (development, building capacity, demand),
`17-sim-services.md` (schools, colleges, hospitals, libraries, museums — facility side, coverage,
funding and strikes as seen from the facility), `19-sim-economy.md` (taxes, ordinances, neighbour
economy), `22-advisors-news-petitioners.md` (demographics advisor), `14-sim-clock.md` (month/year
boundaries).

## 1. Data sources

| File | Sections/keys | Use |
|---|---|---|
| `SC3ResLayer.ini` | `[TuningParameters]` | Cohort model, EQ/HQ decay, ordinance effects on cohorts, strikes (§3–§5) |
| `SC3Tune.ini` | `[School]`, `[College]`, `[Library]`, `[Museum]`, `[Hospital]` (`MinAgeServed`, `MaxAgeServed`, `OptimalDeskCount`, `PopServed`, `EQEffect`, `OptimalEQEffect`, `OptimalBedCount`, `OptimalHQEffect`) | Which ages each facility serves, its capacity and its effect size |
| `SC3WorldLayer.ini` | `JobToResFactor`, `ExternalJobDemand`, `EconomicPhases` | Workforce / outside jobs (owned by `15-sim-growth.md`) |
| `Advisor-Demographics.ini` | `[FUNDING_RATE_CUTOFFS]`, `[ADVICE_TRIGGERS]`, `[PETITIONER_GENERATION]` | Advisor thresholds only (§9) |

`SC3ResLayer.ini [TuningParameters]` keys and their scaling [STATIC]:

| Key | Scale | Meaning |
|---|---|---|
| `ExternalFunSupply` | integer | Recreation supply assumed from outside the city (the constant supply of valve 3001, `15-sim-growth.md`) |
| `NewbornPopPct` | ×0.0001 | Initial share of the population in the age-0 cohort (shipped 136 → 0.0136) |
| `MinNewbornPopPct`, `MaxNewbornPopPct` | ×0.0001 | Clamp range of the newborn share |
| `AgeCohortDecay` | ×0.0001 | Survival ratio between consecutive yearly cohorts (shipped 9970 → 0.997) |
| `BabyTrendChangeProb` | ×0.0001 | Probability per year that the birth-rate trend changes |
| `EQDecay` | ×0.0001 | Yearly retention of EQ for adults |
| `HQDecay` | ×0.0001 | Retention of HQ |
| `OptimalYearlyHQ` | ×0.01 | Yearly HQ gain from optimal health care |
| `OptimalYearlySchoolEQ` | ×0.01 | Yearly EQ gain from optimal schooling |
| `OptimalYearlyCollegeEQ` | ×0.01 | Yearly EQ gain from optimal college |
| `HospitalizationRate` | ×0.0001 | Share of population needing hospital beds |
| `PollutionFactor` | ×1e‑8 (double) | Weight of city pollution in the hospitalisation share (§5.1) |
| `AgeOfRebellion` | years (byte) | Ages below this absorb EQ from their parents' generation each month (§4.3). Despite the name, it has nothing to do with crime |
| `MonthlyYouthCurfewOrdEffect` | ×0.0001 | Monthly **EQ** added to ages `YouthCurfewOrdMinAge…YouthCurfewOrdMaxAge` while the curfew ordinance is enacted |
| `MonthlyReadingOrdEffectAdult`, `…Child` | ×0.0001 | Monthly EQ added while the reading ordinance is enacted. The child value applies to ages `ReadingOrdMinChildAge…ReadingOrdMaxChildAge`, the adult value to all other ages |
| `MonthlyJrSportsOrdEffect` | ×0.0001 | Monthly **EQ** added to ages `JrSportsOrdMinAge…JrSportsOrdMaxAge` |
| `MonthlyCrossingGuardsOrdEffect`, `MonthlyFreeClinicsOrdEffect`, `MonthlyCPRTrainingOrdEffect`, `MonthlySmokingBanOrdEffect` | ×0.0001 | Added to the monthly health gain *x* (§5.2) while the respective ordinance is enacted. Note that crossing guards counts as a health ordinance |
| `MinAcceptableEducationFunding`, `MinAcceptableHealthFunding` | percent (byte) | Strike threshold on department funding |
| `MinAcceptableEducationRating`, `MinAcceptableHealthRating` | 0–4 (byte) | Strike threshold on the rating |
| `EducationStrikeEfficiency`, `HealthStrikeEfficiency` | byte, **used unscaled** | Multiplier applied during a strike (§4.2, §5.2). Defect: it is not divided by 100 |
| `MaxEducationStrikeDuration`, `MaxHealthStrikeDuration` | months (u32) | A strike ends once its counter exceeds this value |

Ordinance IDs referenced by the cohort model [STATIC]: youth curfew `0xC2BF1DC5`, reading
`0xE0D07233`, junior sports `0x815B4CEF`, crossing guards `0xC2BF1DEF`, free clinics
`0x40D07236`, CPR training `0x00D0723D`, smoking ban `0xE0D07239` (see `19-sim-economy.md` §9).

## 2. Population counting

* City population = sum over all residential buildings of their current resident count [STATIC:
  the residential layer sums each residential building's occupancy over its building lists every
  month]. [OPEN] Whether a building's occupancy equals its residential valve supply (footprint
  tiles × the valve-3000 supply per tile of `SC3ValveLayer.ini`), which would make the city
  population equal to `N_R` of `19-sim-economy.md` §5.1. [DATA] The newest population-chart
  sample equals the `Slast` of valve 3000 in 13/13 played cities; use valve 3000 `Slast` as the
  city population (it includes non-family residential supply such as military bases, so it can
  exceed `N_R` by a few thousand). Taxes, ordinances, cohorts and advisors all use this one figure.
* The cohort model below does not count people; it stores **shares** (fractions summing to 1)
  per age, which are multiplied by the city population when absolute numbers are needed
  (e.g. number of school-age children = population × Σ share over the served ages) [STATIC].
* The budget, ordinances (`19-sim-economy.md`), neighbour deals and the tax→demand table all use
  this same city population figure.

## 3. Age distribution (cohorts)

The population is modelled as **90 one-year ages, 0–89**. [STATIC] Per age *a* the model keeps four
single-precision values, all persisted (`10-save-format.md` §6.11):

* `p[a]`: population share (Σ = 1);
* `EQ[a]`: education quotient, 0–150;
* `HQ[a]`: health quotient;
* `LE[a]`: life expectancy, in years, of the people currently aged *a* (≤ 89).

It also keeps a birth-trend state *T* ∈ {rising = 0, falling = 1, steady = 2}. Each month it
recomputes *A\**, the oldest age with a positive share.

The residential layer draws its random numbers from its own generator instance, using the
algorithms of `16-sim-utilities.md` §1. This covers births, the birth trend and strikes. Like every simulation generator it is
seeded from the millisecond timer when the layer is created, and its state is not saved
(`14-sim-clock.md` §8) [STATIC].

### 3.1 Initial distribution (new city)

[STATIC; DATA: every terrain and fresh starter town stores exactly these arrays] With
`N = NewbornPopPct` and `D = AgeCohortDecay`:

1. `p[0] = N`. For `1 ≤ a ≤ 64`, `p[a] = D · p[a−1]`. For `a ≥ 65`, `p[a] = 0`.
2. If `S = Σ p[a]` differs from 1, add `(1 − S) × 0.015384615` (= 1/65) to each of `p[0..64]`.
3. `EQ[a] = floor(85·a / 20)` for `a ≤ 20` (integer division). For `a ≥ 21`, `EQ[a] = EQDecay · EQ[a−1]`.
4. `LE[0] = 65`. For every `a ≥ 1` (including ages with zero share), `LE[a] = 0.99696726 · LE[a−1]`.
5. `HQ[0] = OptimalYearlyHQ × 55.625`. For `a ≥ 1`,
   `HQ[a] = max(0, (LE[a] − 50) × OptimalYearlyHQ × 3.7083333)`. This is the inverse of the
   HQ → LE mapping in §5.3.
6. `T = rising`.

### 3.2 Monthly update: order of steps

At each month boundary the residential layer performs these steps in order. Let *N* be the city
population (§2) at the start of the month. [STATIC]

1. Set the cap of valve 3000 from valve 3001 (`15-sim-growth.md` §3.2 "Cap rule"; earlier
   drafts called this the employment percentage, see §6).
2. Re-apply the education and health department funding to every facility and total their upkeep
   (`19-sim-economy.md`).
3. Distribute education (§4.2). This step uses the shares as they were before this month's changes.
4. Compute the patients (§5.1), the health gain *x* and the health rating (§5.2).
5. Run the per-age loop of §3.3 for a = 89 down to 0.
6. Compute the school and college ratings (§4.4).
7. Run the strike machines in the order health, school, college (§5.4).
8. In January only, apply yearly ageing (§3.4).
9. Renormalise the shares (§3.5).
10. Push chart samples: population; city EQ (§4.5; 0 if *N* = 0); city life expectancy (§5.3; 0 if
    *N* = 0). In January also push yearly samples, and in January of a year divisible by 10 also
    push decade samples.

### 3.3 Per-age loop (a = 89 … 0)

For each age *a*: if `p[a] ≤ 0`, set `p[a] = 0` and move on to the next age. Otherwise: [STATIC]

1. The first such age encountered becomes *A\**.
2. If `LE[a] < a`, multiply `p[a]` by 0.9090909 (single precision; i.e. ÷ 1.1). This is the only
   mortality mechanism.
3. If `N × p[a] < 1`, set `p[a] = 0`. The age is still processed below, with zero people.
4. Add `p[a]` to the running sum *S* used by §3.5.
5. Update EQ (§4.3).
6. Update HQ and LE (§5.3).

### 3.4 Yearly ageing (January)

This runs after the per-age loop and the strikes. [STATIC]

1. For a = 89 down to 1:
   * `p[a] = p[a−1]`.
   * If `a > A*`, set `EQ[a] = 0` and `LE[a] = 0`.
   * Otherwise:
     * `EQ[a] = EQ[a−1]` if `a < 27`, else `EQ[a] = EQDecay · EQ[a−1]`;
     * `LE[a] = LE[a−1]`.

   **HQ is not shifted.** It stays attached to the age, not to the cohort.
2. `A* = A* + 1`.
3. Births. Let *u* be uniform in [0, 1). Then `p[0] = p[1] / AgeCohortDecay + (β − u) × 0.001`,
   where `p[1]` is last year's newborn share and β = 0.75, 0.25 or 0.5 for *T* = rising, falling
   or steady.
4. Clamp and update the trend:
   * if `p[0] < MinNewbornPopPct`, set `p[0] = MinNewbornPopPct` and *T* = rising;
   * else if `p[0] > MaxNewbornPopPct`, set `p[0] = MaxNewbornPopPct` and *T* = falling;
   * otherwise draw *u₂*. If *u₂* < `BabyTrendChangeProb`:
     * if *T* is rising or falling, set *T* = steady;
     * if *T* is steady, draw *u₃*; *T* = rising if *u₃* < 0.5, otherwise falling.
5. `LE[0] = LE[1]` and `EQ[0] = 0`.
6. `S = S + p[0]`. *S* is still the sum from §3.3, which includes the share that has just dropped
   off at age 89. This quirk is original behaviour.

### 3.5 Renormalisation (every month)

If *S* ≠ 1, add `(1 − S) / (A* + 1)` to each of `p[0..A*]`. [STATIC]

## 4. Education quotient (EQ)

The facility side (capacities, effects, funding) is in `17-sim-services.md` §6.

### 4.1 Facility parameters seen by the cohort model

Each education facility *f* exposes five values [STATIC, except that the desk-count source is
INFERRED: the saved desk sums are exact multiples of `OptimalDeskCount` in every played file]:

* its kind (school, college, library or museum);
* its capacity *C_f*: `OptimalDeskCount` for schools and colleges, `PopServed` for libraries and
  museums (not funding-dependent);
* its effect *e_f* (EQ points per person per month);
* its served ages [`MinAgeServed`, `min(MaxAgeServed, 89)`];
* for schools and colleges, its funded staff count, which is only summed for the record.

### 4.2 Distribution (step 3 of §3.2)

Facilities are grouped by kind, one group each for schools, colleges, libraries and museums
[INFERRED]. Each group *g* has two per-age arrays, `gain_g[a]` and `seats_g[a]`, which are cleared
every month. For each facility *f* in the group: [STATIC]

1. Let *lo*, *hi* be its served ages and `Σp = p[lo] + … + p[hi]`. If *lo* > *hi* or Σp ≤ 0, the
   facility does nothing this month.
2. For each age *a* in [*lo*, *hi*]:
   * `s = round(C_f × p[a] / Σp)` (single precision, ties to even);
   * `seats_g[a] += s`;
   * `gain_g[a] += e_f × s × k`. Here *k* = `EducationStrikeEfficiency` (the raw byte, see §1) if
     *f* is a school while schools are on strike, or a college while colleges are on strike.
     Otherwise *k* = 1. Libraries and museums never strike.

### 4.3 Per-age EQ update (inside §3.3)

Before the loop, the parental term is computed once:
`E_par = min(Q(20, 40), 120) / (AgeOfRebellion × 12)`, with *Q* as in §4.5. For age *a*, let
people = `N × p[a]` (after steps 2–3 of §3.3). Then: [STATIC]

1. Facility increment. For each group:
   * if `seats_g[a] ≥ people` and `seats_g[a] ≥ 1`, add `gain_g[a] / seats_g[a]`;
   * otherwise, if people > 0, add `gain_g[a] / people`.

   Δ_fac is the sum of these over all groups. The effect: an oversupplied age gets the full
   effect, and an undersupplied age gets it diluted by the ratio of seats to people.
2. Ordinance increment Δ_ord is the sum of:
   * `MonthlyYouthCurfewOrdEffect`, if the curfew (0xC2BF1DC5) is enacted and *a* is in the curfew
     ages;
   * the reading value, if reading (0xE0D07233) is enacted: the child value inside the child ages,
     the adult value otherwise;
   * `MonthlyJrSportsOrdEffect`, if junior sports (0x815B4CEF) is enacted and *a* is in its ages.
3. `EQ[a] += Δ_ord + Δ_fac`.
4. Statistics. These age windows are hard-coded and independent of the INI ages.
   * If 7 ≤ *a* ≤ 18: `G_school += Δ_fac` and `schoolAgePop += round(people)`.
   * If 19 ≤ *a* ≤ 24: `G_college += Δ_fac` and `collegeAgePop += round(people)`.
5. If *a* < `AgeOfRebellion`: `EQ[a] += E_par`.
6. If *a* ≤ 26 and `EQ[a] < 85·a / 26` (computed as 85·a × 0.03846154), set `EQ[a] = 85·a / 26`.
7. If `EQ[a] > 150`, set `EQ[a] = 150`.

There is no monthly EQ decay. EQ decays only at the yearly shift, for ages 27 and over (§3.4).

### 4.4 School and college ratings

Each rating is computed in double precision and rounded to nearest (ties to even). [STATIC]

* School rating = clamp(round(4 · (G_school / 13) / (`OptimalYearlySchoolEQ` / 12) − 0.4), 0, 4).
* College rating = clamp(round(4 · (G_college / 6) / (`OptimalYearlyCollegeEQ` / 12) − 0.4), 0, 4).

The hard-coded constants are 1/13, 1/6, 1/12 and 0.4, each as the nearest single-precision value
widened to double.

### 4.5 City EQ

Q(lo, hi) is the **unweighted** mean of `EQ[a]` over the ages *a* in [lo, min(hi, 89)] with
`p[a] > 0`. It is rounded to nearest and returned as a byte, or 0 if no age qualifies. [STATIC;
DATA: in all 25 played shipped cities the newest EQ-chart sample equals Q(0, 89) ± 1]

* City EQ (the chart value, the aura input in `17-sim-services.md` §8.4, and the advisors' EQ) is
  Q(0, 89).
* The parental term uses Q(20, 40).
* The industrial developer's clean/dirty choice (`15-sim-growth.md` §4.5) uses Q(20, 65), the
  working-age EQ [STATIC].

## 5. Health quotient and life expectancy

### 5.1 Patients

`patients = trunc((P̄ × PollutionFactor + HospitalizationRate) × N)`, where P̄ = ⌊(*P₁* + *P₂*) / 2⌋
and *P₁*, *P₂* are two city-wide pollution totals supplied by the pollution layer. [STATIC for the
formula; INFERRED that *P₁*, *P₂* are the air and water totals of `17-sim-services.md` §7.2]
`PollutionFactor` is held in double precision.

### 5.2 Health gain *x* and health rating

Let *B* = Σ beds over all hospitals (each `OptimalBedCount`, not funding-dependent). Let
*W* = Σ beds_h × e_h, where e_h is each hospital's funded HQ effect (`17-sim-services.md` §5).
[STATIC]

1. Compute *x*:
   * if patients ≤ 0, *x* = 0.00001;
   * otherwise, with *D* = patients (or *D* = *B* if patients ≤ *B* and *B* > 0), *x* = *W* / *D*.
2. Health rating:
   * if patients ≠ 0 or *x* ≤ 0, rating = clamp(round(4·*x* / (`OptimalYearlyHQ` / 12) − 0.4), 0, 4);
   * otherwise (no patients) the rating is 4.
3. If *N* ≠ 0, add the monthly effect of each enacted ordinance to *x*: crossing guards
   0xC2BF1DEF, free clinics 0x40D07236, CPR training 0x00D0723D [INFERRED for the CPR flag read]
   and smoking ban 0xE0D07239.
4. During a health strike, `x = x × HealthStrikeEfficiency` (the raw byte; see the defect note in
   §1).

### 5.3 Per-age HQ and LE (inside §3.3)

1. If *x* > 0, `HQ[a] += x`. Otherwise, if `HQ[a] > 0`, `HQ[a] = HQDecay × HQ[a]`. HQ therefore
   decays only in months without a positive health gain.
2. `LE[a] = min(89, 50 + HQ[a] / OptimalYearlyHQ × 0.26966292)`. The constant is 24/89, so HQ =
   89·`OptimalYearlyHQ` (89 years of optimal care) gives an LE of 74.

**City life expectancy** is the unweighted mean of `LE[a]` over the ages with `p[a] > 0`, computed
exactly like Q(0, 89) in §4.5. [STATIC; DATA 25/25 against the LE chart] It feeds the aura
(`17-sim-services.md` §8.4) and the advisors.

Mortality: any age whose `LE[a]` is below the age itself loses 1/11 of its share every month
(§3.3).

### 5.4 Strikes (health, schools, colleges)

**Risk** *r* (0–10 000). If the service is already striking, *r* = 10 000. Otherwise *r* =
(`MinAcceptable…Funding` − funding)² when **all three** of these hold:

* its rating < `MinAcceptable…Rating`;
* its department funding % < `MinAcceptable…Funding`;
* at least one facility of that kind exists.

If any of them fails, *r* = 0. [STATIC]

| Service | Rating | Funding | Facilities | Duration key |
|---|---|---|---|---|
| Health | health rating | health | hospitals | `MaxHealthStrikeDuration` |
| Schools | school rating | education | schools | `MaxEducationStrikeDuration` |
| Colleges | college rating | education | colleges | `MaxEducationStrikeDuration` |

The college risk is assumed to mirror the other two exactly. [INFERRED: it has the same structure]

**Machine** (monthly, after the ratings):

* Not striking: if *r* ≠ 0 and `Rand(10000)` < *r*, the strike starts. Its month counter is set to
  0 and a "strike started" notification for that service is announced (advisors and the news
  ticker react to it).
* Striking: counter += 1. The strike ends when counter > its duration key, or funding > 99 %, or no
  facility of that kind remains.

While a strike is active, its multiplier applies to that service's effects (§4.2, §5.2).

## 6. Workforce, jobs and commuting

Jobs and workers are not counted per person; they are the two sides of the **jobs valve 7000**
(`15-sim-growth.md` §3.4) [STATIC]:

* **Jobs** `D` = the jobs valve's demand total of the last completed cycle = Σ over job-providing
  buildings of `footprintTiles × (7000 demand per tile)` from `SC3ValveLayer.ini
  [AgentValveEffects]` (commercial, industrial, civic buildings, stations, parks, …).
* **Workers** `S` = the jobs valve's supply total of the last completed cycle = the workforce
  share of residents (`trunc(residentSupply × w)`, `w` = the workforce fraction defined below) **plus** the constant
  `SC3WorldLayer.ini ExternalJobDemand`, added once per cycle.

**Workforce fraction `w`** (owned here; used by `15-sim-growth.md` §3.4) [STATIC]:

    w = p[20] + p[21] + … + p[65]

the sum of the cohort shares of ages **20 to 65 inclusive** (46 ages). Because the shares sum to
1, `w` is the working-age fraction of the population; it is 0 only if those ages are empty. There
is no per-age employable share, no unemployment coupling and no wealth or EQ weighting. `w` is
evaluated each time residential supply is added to valve 3000 (in the monthly valve update of day
1, before this layer's month-end work), so it uses the shares of the previous month-end. The jobs
valve then receives `round(n × w)` workers (round half to even) for each residential supply `n`.

Verification [DATA]: the jobs-valve supply `Slast` saved in the world layer
(`10-save-format.md` §6.13, field `v0`) against `R × w + ExternalJobDemand`, with `R` = valve 3000
`Slast` and `w` from the saved shares:

| City | `R` | `w` | `R·w + 150` | saved jobs `Slast` |
|---|---|---|---|---|
| Berlin | 794 278 | 0.6471 | 514 102 | 514 166 |
| Madrid | 1 176 537 | 0.6530 | 768 398 | 768 362 |
| Moscow | 929 879 | 0.6383 | 593 732 | 593 747 |
| Madison, WI | 148 844 | 0.7150 | 106 574 | 107 853 |

All 13 played cities agree within 1.2 %; the residual comes from the saved shares having been
updated once more (month-end and January ageing) after the valve update that produced `Slast`.
The world-layer fields `v0`, `v1` and `v2` of `10-save-format.md` §6.13 are the jobs valve's
`Slast`, `Dlast` and `Sbase` (= `ExternalJobDemand`, 150) [DATA 13/13].

**Unemployment** [STATIC; DATA]. The world layer computes the unemployment percentage once a month
(the fourth world-layer chart, `10-save-format.md` §6.13): with `R` = valve 3000 `Slast`,
`K = round(R × w)` (resident workers only, **without** `ExternalJobDemand`) and `J` = the jobs
valve's `Dlast` (jobs):

| Condition | Unemployment % |
|---|---|
| `R = 0` or `K ≤ J` | 0 |
| `J = 0` (and `K > J`) | 100 |
| otherwise | `⌊(100·K − 100·J) / (2·K)⌋` (unsigned integer arithmetic) |

The factor 2 halves the raw jobless share. Test vector (Berlin): `K` = 513 952, `J` = 425 561 →
⌊8 839 100 / 1 027 904⌋ = 8 (saved chart sample 8). Craterville: `K` = 140 724, `J` = 126 521 →
5 (saved 5). The value feeds the unemployment chart and the advisors.

**Correction to earlier drafts.** The monthly residential-layer computation previously described
here as the "employment percentage" (`D = 0` → 0 %; `S < D` → 100 %; else `⌊100 × D / S⌋`) reads
the **amenity valve 3001**, not the jobs valve, and its result is the `capPct` of valve 3000
(`15-sim-growth.md` §3.2 "Cap rule"). It is not an employment figure; unemployment is the
world-layer value above.

**Commuting / outside world** [STATIC, detailed in `15-sim-growth.md` §3.4–3.5]:
* `ExternalJobDemand` models workers from outside who want jobs in the city: it is added to the
  workforce every cycle and is split into commercial and industrial demand like resident workers.
* `JobToResFactor` converts each new job into `JobToResFactor` units of residential demand.
* `EconomicPhases` sets the commercial : industrial split of workforce by city population.
* `[ListOfWorldValveMapping]` applies the repeating economic cycle (booms/recessions) to the
  residential valve's world modifier.
There is no per-neighbour commuter flow; neighbour populations (`19-sim-economy.md` §10.6) are
used only by neighbour deals.

## 7. Wealth classes

Residential buildings belong to three wealth classes (low/medium/high land-value bands,
agent types 3100–4700 in `CitySim.ini`). The residential layer keeps separate building lists per
class and sums their populations [STATIC]. Wealth class has no separate effect on demand. It enters growth
only through the residential categories 3100–4700 that the developers choose between
(`15-sim-growth.md`); there are no per-wealth tax rates (`19-sim-economy.md` §4.1).

## 8. City size classes

No named city-size classes (village/town/city/capital/metropolis/megalopolis) exist in this game:
no such strings exist in the shipped string tables [DATA], and the simulation has no size-class
thresholds [STATIC]. Population milestones (rewards
such as the mayor's house and city hall) are owned by `13-buildings-occupants.md` and
`22-advisors-news-petitioners.md`.

## 9. Demographics advisor and graph data

`Advisor-Demographics.ini` [STATIC for load; semantics INFERRED from key names]:

| Key | Use |
|---|---|
| `[FUNDING_RATE_CUTOFFS] HEALTH_FUNDING_HIGH/MEDIUM`, `EDUCATION_FUNDING_HIGH/MEDIUM` | Funding % bands the advisor comments on |
| `[ADVICE_TRIGGERS] LOW_/HIGH_LIFE_EXPECTANCY` | Years; life-expectancy comment thresholds (values carry a trailing `;` which the integer parser ignores) |
| `LOW_/AVERAGE_/HIGH_EQ_CUTOFF` | EQ bands |
| `LE_AND_EQ_POPULATION_MINIMUM` | Population below which LE/EQ advice is suppressed |
| `RESIDENTIAL_GROWTH_CAP`, `STADIUM_GROWTH_CUTOFF` | Growth advice thresholds |
| `SCHOOL_/COLLEGE_/HOSPITAL_/LIBRARY_/MUSEUM_/ZOO_/MARINA_POPULATION_MINIMUM` | Population at which the advisor starts asking for that facility |
| `CITY_AURA_DREARY_POPULATION`, `LOW_/HIGH_DREARY_RATING`, `CITY_AURA_LOW/HIGH` | Mood/aura comments |
| `[PETITIONER_GENERATION] HOSPITAL_RATIO` | Population per hospital before petitioners ask for one |

Graph series exposed to the UI: population, EQ, life expectancy, health/education ratings,
age distribution (the 90 cohort shares) [INFERRED; see `09-ui-screens.md`].

## Persisted state

Everything in this document is persisted in the residential-layer record `206C6E7C:C106C4F5:0`:
the four per-age arrays, the birth trend, the ratings, the strike flags and counters, the
statistics and the three charts (population, EQ, LE). The byte layout is in
`10-save-format.md` §6.11. [DATA 59/59]

## Open questions

* The exact source of the two pollution totals used for patients (§5.1).
* How facilities are grouped (one group per kind is inferred) and how the groups are ordered.
* The CPR-training flag read is inferred from the key and ID pattern.
* The meaning of the unknown byte at offset 0x1BE of the residential state.
