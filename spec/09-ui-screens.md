# 09 — UI screens, dialogs, panels and player tools

Status: draft.

This document describes every screen and dialog of the game, the in-game control panel, and the
behaviour of every player tool. It builds on:

* `08-ui-framework.md` — window/control system, image resources, fonts, cursors, hotkeys, input
  routing. This document does not redefine controls; it states which controls each screen has,
  where their images and strings come from, and what they do.
* `05-text-and-fonts.md` — string tables. Strings are cited here as `TABLE:ID` (both hexadecimal),
  i.e. resource key `2026960B:TABLE:ID`.
* `06-audio.md` — sound cues (cited by name/ID only).
* Simulation documents (`15`–`22`) own the rules that tools invoke; this document owns the
  interaction layer (click, drag, preview, cost display, validity feedback).

## 1. Conventions and data sources

### 1.1 String tables used by the UI [DATA]

All strings are loaded from `Res/Text/<LANG>/*.IXF`, resource Type `2026960B`, Group = table ID,
Instance = string ID. The UI uses these tables:

| Table ID | File (ENGLISH) | Used for |
|----------|----------------|----------|
| `225872FE` | `SC3StringsGUI.IXF` | control-panel button captions/tooltips, speed names, view-control tooltips |
| `03C09AFF` | `SC3StringsMenu.IXF` | front-end (main menu) captions |
| `029541F4` | `SC3StringsWindow.IXF` | window titles, labels, data-map legends, graph/chart labels |
| `82E0074C` | `SC3StringsQuery.IXF` | query popup labels and values |
| `015E410C` | `SC3StringsMessage.IXF` | modal message boxes |
| `209E6378` | `SC3StringsApp.IXF` | generic button captions (Yes/No/OK/Cancel/…), file extensions, app title |
| `03C09B1B` | `SC3StringsTOD.IXF` | Tip of the Day |
| `830CDF29` | `SC3StringsCredits.IXF` | credits roll |
| `041F2625` | `SEStringsUI.IXF` | Unlimited-edition additions (scenarios, snapshots, city scheme, new disasters, holidays) |
| `62E69238` | building catalogue tables (`SC3StringsBldCatalog*.IXF`) | building-picker titles and empty-list messages |

### 1.2 UI images [DATA]

All UI bitmaps are image resources (Type `62B9DA24`, format in `04-graphics-formats.md`) stored in
`Res/UI/Shared/*.ixf`. The file-name prefix equals the resource Group. Layout geometry is not stored
in data; it is fixed by the program (see `08-ui-framework.md`). Groups and their contents:

| Group | File | Contents (Instance → use) |
|-------|------|---------------------------|
| `02F78C6D` | `02F78C6D_MainUI.ixf` | control-panel art: sub-menu button faces `0x2001…0xA006` (144×36, one per `SC3MenuBtnDefs` key), root-button faces `0x1001000n/0x1002000n…0x1009000n` (`n`=1,5: 56×32 states; `n`=3: 96-wide fly-out panel backgrounds), panel frames `0xFFFE`/`0xFFFF`, misc. widgets |
| `00640480` | `00640480_MainUI.ixf` | same button faces and fly-out backgrounds for the 640×480 layout, plus bottom bar `0x82790741` (440×64) |
| `00800600` | `00800600_MainUI.ixf` | bottom bar `0x82790741` for 800×600 (600×56) |
| `01024768` | `01024768_MainUI.ixf` | bottom bar `0x82790741` for 1024×768 (824×64) and panel parts `0xFFFC–0xFFFE` |
| `000005E5` | `000005E5_Maps.ixf` | data-map / layer window parts (buttons 104×26, legend bars, map frame 356×189, etc.) |
| `22DEC90F` | `22DEC90F_TerrEdit.ixf` | terrain-editor panel parts |
| `22DEC92D` | `22DEC92D_OtherCtrls.ixf` | slider/scroll parts |
| `42DEC7D2` | `42DEC7D2_GonzoCtrls.ixf` | generic dialog buttons, tabs, check boxes |
| `52DB60A0` | `52DB60A0_Advisors.ixf` | advisor portraits by mood (`0x0AMM`: advisor A, mood M), petitioner portraits `0xFF01–0xFF15`, small portraits `0x010AAAMM` |
| `831D112B` | `831D112B_AdvTopics.ixf` | advisor topic illustrations `0x01–0x53` (118×110) |
| `85535958` | `85535958_CitySchemeUI.ixf` | city-scheme previews: landscapes `0x1001–0x1005`, flora sets `0x2001–0x2005`, building sets `0x3001–0x3003` |
| `C2BF3646` | `DLG.IXF` | generic dialog backgrounds (464×350 etc.) |
| `82B9B75C` | `MAIN.IXF` | front-end backgrounds: `0x22721000`–`0x22721004` and `0x22721006` (640×480), `0x22729921`/`0x01140000` (800×600); panel and button art `0x22729930`–`0x22729938` and `0x01140001`–`0x01140009`; credits frame `0x22729940` (27 images) |
| `82B9B75B` | `SYS.IXF` | system widgets (message boxes, ticker frame, speed buttons, etc.) |

Cursors are loose `.cur` files in `Res/UI/Shared/Cursors/` (see `08-ui-framework.md`). Each tool has
its own cursor; the file names are self-describing (e.g. `zone res low cursor.cur`,
`demolish clear cursor.cur`, `query cursor.cur`). [DATA]

## 2. Command model: the control-panel menu (`MenuItem.INI`)

The in-game button bar ("control panel") is entirely data-driven by `Sys/MenuItem.INI` (packed in
`SYS.PAK`, see `02-containers.md`). The shipped file has 90 menu items, 109 button definitions and
21 menu descriptions; every string reference resolves in the ENGLISH tables (0 missing). [DATA]
The file grammar and execution rules are specified once, in `08-ui-framework.md` §6; §2.1–2.3
below only add the UI meaning of the fields.

### 2.1 `[SC3MenuItemInfo]` — what a button does

Key: menu-item ID (hex, `0x1000xxxx`). Value: 11 comma-separated fields:

Field names follow `08-ui-framework.md` §6.1.

| # | Field (08 §6.1 name) | UI meaning |
|---|-------|---------|
| 1 | enabled | `1` = item available (every shipped item) |
| 2,3 | nameTable, nameID | the button's name, shown as its tooltip [STATIC] |
| 4,5 | altTable, altID | second name; identical to fields 2,3 except for Map View and Lay Subway Rail; used as the tool's short name [INFERRED] |
| 6 | kind | `COMM` = run a command by class ID; `PMSG` = post a message with the given message ID |
| 7 | target | the command class (COMM) or message (PMSG) |
| 8 | arg1 | building-picker category, snapshot size or load-dialog file kind |
| 9 | arg2 | `1` only for Map View and Neighbors, the two windows that toggle open/closed with the same button [INFERRED]; 0 elsewhere [DATA] |
| 10 | arg3 | always 0 [DATA] |
| 11 | `END` | terminator |

### 2.2 `[SC3MenuBtnDefs]` — buttons

Key: button ID (`0x0000PSNN`). Value: `menuItemID, subMenuID, END`. `subMenuID = 0` means a leaf
button; non-zero opens the named menu as a fly-out. A button whose menu item has no
`SC3MenuItemInfo` entry (the eight root category buttons) only opens its sub-menu. The second-level
button face image is Group `02F78C6D` (or `00640480` at 640×480), Instance = button ID; third-level
fly-out entries have no image [DATA] and are drawn as text on a strip in the menu's colour
[INFERRED].

### 2.3 `[SC3MenuDescs]` — menus

Key: menu ID. Value: `titleTable, titleID, stripColour, flags, count, button₁ … button_count, END`.
`stripColour` is `0x00RRGGBB` for fly-out (third-level) menus and 0 for root/second-level panels;
`flags` is `0x000018FF` on all fly-outs [OPEN: meaning]. [DATA] (08 §6.3 calls these fields
`colour` and `style`.)

`[SC3MenuSets]` maps a menu-set ID to an INI file (`SC3MSET_0x00001000 = 0,0,Sys\MenuItem.INI`) and the
`[SC3MSET_0x00001000]` section lists which menus belong to the set (identity map). [DATA]

### 2.4 The shipped menu tree [DATA]

Root `0xD1001000` (9 buttons, in order): Landscape, Zone, Transportation, Utilities, Civic/Special,
Meet (advisors), Review, Options, Emergency.

| Sub-menu | Buttons (order) → command |
|----------|---------------------------|
| Landscape `D1002000` | Trees `4253EC3F`; Water `02E763C1`; Lower `C25AD94F`; Raise `825AD7F6`; Level `825AD954`; Demolish `E3166801` |
| Zone `D1003000` | Residential ▸ (Light `42E88586`, Medium `02E88915`, Dense `A2E8893B`); Commercial ▸ (`02E88949`, `A2E8895A`, `02E88968`); Industrial ▸ (`C2E8898F`, `A2E8899D`, `02E889AA`); Landfill `C2E889B6`; Seaport `02E889C4`; Airport `E2E889D4`; De-zone `82E88C9C`; Demolish |
| Transportation `D1004000` | Road `C2B5DE77`; Highway ▸ (Highway `42B5DE86`, On-ramp `02B5DEBB`); Bus stop `02FF04FE`; Rail ▸ (Lay rail `42B5DE98`, Train station `22FF058F`); Subway ▸ (Lay subway `C2B5DEA6`, Subway station `62FF059B`); Subway-to-rail `22FF0521`; Demolish |
| Utilities `D1005000` | Power lines `42C2F8BF`; Power plants (picker `E2ED3560`, category `62E925A2`); Pipes `22C2F8C5`; Water structures (picker, `E2E92527`); Garbage (picker, `C2F291A9`); Demolish |
| Civic `D1006000` | Police ▸ (Station `42FF052F`, Jail `22FF053F`); Fire station `22FF054C`; Health & Education ▸ (Hospital `E2FF0555`, School `C2FF0563`, College `C2FF0571`, Library `C2FF057A`, Museum `62FF0585`); Parks ▸ (Small park `02FF05AA`, Large park `82FF05B7`, Fountain `22FF05F3`, Pond `22FF05EA`, Playground `C2FF05E0`, Marina `42FF05C2`, Zoo `22FF05CA`, Ballpark `62FF05D1`); Landmarks (picker, `22E92581`); Rewards & Opportunities (picker, `62E92553`); Demolish |
| Meet | message `724A82D0` (open advisor panel) |
| Review `D1008000` | View snapshots (message `2435FF70`, param 0); Take snapshot ▸ (Small/Medium/Large: message `2435FF70` with param 1/2/3); Budget `025DDC4A`; Neighbors `630288E9` (toggle); Ordinances (message `724A82D7`); Scenario goals `844587AE`; Map view / data views `411164F1` (toggle) |
| Options `D1009000` | Preferences `C3001401`; City properties `855A2DC4`; Save `410438DD`; Save as (`410438DD`, param `C3172AEA`); Load ▸ (Saved city / Starter town / Real terrain / Scenario: `610438A4` with param `83172AD7` / `63172B74` / `23172AF2` / `A42C7725`); New city `E3270FE9`; Exit `A10438E9` |
| Emergency `D100A000` | Go to disaster (message `C30177CA`); Warning siren `02B5AECF`; Dispatch police `029A52DD`; Dispatch fire `229A528C`; Dispatch plane `A5354652`; Start disaster ▸ (Fire `C26CE54D`, Tornado `626CE901`, Earthquake `22FE8786`, Riot `02FE87A7`, UFO `42FE87AE`, Toxic cloud `447F1E40`, Whirlpool `447F1E41`, Locusts `447F1E42`, Space debris `447F1E43`) |

The command class IDs in `MenuItem.INI` are the link between the menu data and the engine: an
implementation must map each ID to the behaviour described in sections 4–7. The param values
`83172AD7`, `63172B74`, `23172AF2`, `A42C7725` select the file kind for the shared load dialog
(city `.sc3`, starter town `.st3`, terrain `.sct`, scenario `.snr`); `C3172AEA` forces the
"save as" file dialog. [STATIC]

### 2.5 Control-panel behaviour [STATIC/INFERRED]

1. The root column shows the nine root buttons. Clicking a root button opens its sub-menu panel
   (background image Group `02F78C6D`, Instance `0x10PP0003` where `PP` is the root index 01–09);
   clicking it again, clicking another root button, or pressing Escape closes it.
2. Clicking a leaf second-level button runs its command. Buttons with a fly-out show it next to the
   button; the button face shows the most-recently chosen fly-out entry, and clicking the face itself
   re-runs that entry [INFERRED: standard SC3 behaviour; confirm].
3. A tool command stays active until another tool is chosen, the right mouse button is clicked, or
   Escape is pressed; then the default "query/pan" pointer returns. [INFERRED]
4. Window commands (Budget, Ordinances, Neighbors, Map view, Preferences, …) open modeless or modal
   windows described in section 5–6.


## 3. Front end (outside a city)

### 3.1 Main menu [STATIC]

Shown at start-up (after the intro movie, `07-video.md`) and whenever no city is loaded. Background
art from `MAIN.IXF` (Group `82B9B75C`). The two copyright lines drawn on the menu are strings
`041F2625:197` and `63DE4715:18` [STATIC].
Buttons and results:

| # | Button (caption) | Result |
|---|------------------|--------|
| 1 | Start new city (`03C09AFF:1`) | New City dialog (§3.2) → terrain editor (§3.4) → city |
| 2 | Load city (`03C09AFF:2`) | load dialog, kind "saved city" (§3.3) |
| 3 | Starter town (`03C09AFF:9`) | load dialog, kind "starter town" |
| 4 | Preferences (`03C09AFF:4`) | Preferences dialog (§6.8); handled inside the menu, the menu stays |
| 5 | Load scenario (`041F2625:1`) | load dialog, kind "scenario" |
| 6 | Real city terrain (`03C09AFF:13`) | load dialog, kind "terrain" |
| 7 | (image-only button) | [OPEN] credits (§6.15) or web link; handled inside the menu |
| 8 | Exit (`03C09AFF:6`, art `82B9B75C:22729930`) | quits the program |

Each button has a caption colour (RGB) fixed per button and a hover image keyed with colour
`FF00FF` as transparency. The menu runs as a loop: after a load attempt that fails or is
cancelled, the menu re-appears; after a successful new/load it closes and the in-game UI opens.
[STATIC] The table's captions are the main-menu button captions. `03C09AFF:14` holds an older
copyright line; [OPEN] whether the original shows it anywhere.

A separate in-game dialog offers three of the same choices with the captions `03C09AFF:1` (new
city), `03C09AFF:2` (load city) and `029541F4:2DF` (exit the game) [STATIC captions]. It is
probably the leave-city prompt [INFERRED]. `029541F4:2DF` is not the main-menu Exit caption.

### 3.2 New City dialog [STATIC]

Title `029541F4:282` (alternative title `281` when opened from inside a city). Controls and the
values they produce:

| Control | Choices (string `029541F4:`) | Value produced |
|---------|------------------------------|----------------|
| City name `229`, Mayor name `22A` | free text | stored in the city header |
| Difficulty `22B` | Easy `22C`, Medium `22D`, Hard `22E` | difficulty 1 / 2 / 3; starting funds §50 000 / §20 000 / §10 000 (Hard: the §10 000 is a loan, i.e. funds 10 000 with a matching bond). The amount is formatted into the label's `%s` with the money formatter. |
| City size `233` | Miniature `2CE`, Small `234`, Medium `2D3`, Large `235` | map edge 64 / 128 / 192 / 256 tiles (square map) |
| Start date `22F` | `230`, `231`, `232` | start year 1900 / 1950 / 2000 |
| Disasters `21D` | check box | disasters enabled flag |
| Auto budget `21E` | check box | auto-budget flag (`19-sim-economy.md`) |
| City scheme (§3.5) | Landscape `041F2625:28`, Trees `29`, Buildings `2A`, User-made buildings `2B`, Select `2F`, Description `2E` | scheme selection |

Default size selection: the dialog pre-selects a size from two machine-capability figures
(a CPU class *k* and a memory figure *m*): Large if k ≥ 5 and m ≥ 60; else Medium if k ≥ 3 and
m ≥ 45; else Small if k ≥ 2 and m ≥ 30; else Miniature. [STATIC; the exact units of *k* and *m*
are OPEN — a modern implementation may simply default to Large.] If the previously chosen size
is outside the four choices, Small is selected.

`[OBSERVED]` Defaults on first opening at 1024 × 768:
* City name `New City`, mayor `Defacto`;
* Easy, 1900, Large;
* Disasters and Auto Budget both checked;
* the first landscape, the first tree set (palms) and the first building set.

The dialog shows these controls exactly as tabled above.

On OK: if the selected size differs from the current terrain, a new terrain of size×size is
generated with default generator parameters (seed "random", three feature sliders at 64 and a
fourth parameter 36; see `11-world-terrain.md`), then the terrain editor (§3.4) opens.

### 3.3 Load / save dialogs, scenario picker, starter-town picker [STATIC]

All four "Load" entries (main menu and Options ▸ Load ▸ …) run the same load command
(`610438A4`) with a *file-kind* parameter; Save/Save As run `410438DD`. The kind selects the
directory below the game's `Cities/` folder, the file filter and the dialog title:

| Kind param | Directory | Filter | Title (`029541F4:`) |
|------------|-----------|--------|---------------------|
| `83172AD7` saved city | `Cities/` | `*.sc3` (`209E6378:7F`; `.sc2`/`*.sc*` also listed: `7D`, `7B`) | `220` |
| `63172B74` starter town | `Cities/StarterTowns/` | `*.ST3` | `2CF`, explanatory text `2DB` |
| `23172AF2` real terrain | `Cities/Terrains/` | `*.SCT` | `24C` |
| `A42C7725` scenario | `Cities/Scenarios/` | `*.SNR` | `041F2625:2` |
| `C3172AEA` save as | `Cities/` | `*.sc3` | `226` |

Dialog layout: a "look in" folder field (`286`), a file list, a file-name field (`222`), and a
preview pane that, when "show city stats" (`287`) is on, shows the selected file's city name,
population (`223`), funds (`224`) and date (`225`) read from the file's header records
(`10-save-format.md`). The scenario picker additionally shows the scenario author (`041F2625:2C0`)
and description from the `.SNR` (`23-scenarios.md`). Fonts: `systemloadsave.fbf`
(`05-text-and-fonts.md`).

Save flow: if the target exists ask `015E410C:C` (overwrite?); read-only targets give `26`/`29`;
invalid names `27`, too long `2A`, bad path `2D`, failure `2E`.

Leaving the current city: [STATIC]
* **There is no "modified" or dirty flag.** Neither the Exit path nor the New/Load path tests
  whether the city changed. Letting the simulation run, editing the city, and saving or loading do
  not set or clear anything that controls the prompt.
* **New / Load** (when a city is open): the command carries an "ask to save" argument, which
  defaults to true. If it is true, the game asks `029541F4:250` ("Would you like to save this
  city?") or `015E410C:12` ("Do you want to save your city?"), with a `015E410C:6` "Save" caption.
  Answering Yes opens the save flow, No continues without saving, and Cancel aborts.
* **Exit** takes two arguments: *confirm* and *ask to save*. If *confirm* is set, the game first
  shows `209E6378:A3` "Now leaving Sim City 3000." (title `225872FE:55` "Exit") with OK/Cancel.
  Cancel aborts. If *ask to save* is set and a city is open, it then asks `209E6378:127` "Do you
  want to save the game?" with Yes/No/Cancel (`209E6378:C0`, `C2`, `B6`). Yes saves, No exits, and
  Cancel aborts.
* Which UI entry points pass *ask to save* = false was not traced. [OBSERVED] Closing the window
  shows "Now leaving", and after OK the save prompt follows when a city is open. Implement *ask to
  save* = true for window close, quit hotkeys, menu Exit, New and Load.

Scenario end screens (`23-scenarios.md`): status `041F2625:4`/`5`, greeting `3`, completion `7`,
results `25`, buttons replay `8`, another scenario `9`, main menu `A`.

### 3.4 Terrain editor [STATIC]

`[OBSERVED]` The editor panel appears in two places:
* automatically, after the New City dialog is confirmed and before the city starts;
* as the Real City Terrain editing step.

It is a vertical panel at the right edge with these parts, top to bottom:
* the **Pick Features** diagram (described in `11-world-terrain.md` §3.1), with compass, zoom and
  rotate buttons around it;
* three **Adjust Parameters** sliders (hills, water, trees);
* **Re-Generate Terrain**;
* a 2 × 3 block of tool buttons: lower, raise, level, trees, water, bulldoze;
* **Accept this Terrain**.

The zoom and rotate buttons act on the main view, and rotation also turns the diagram (its compass
letter changes). The simulation does not run until Accept is pressed.

Title `029541F4:2CA`. Buttons: accept (`2CD`), regenerate (`254`); tool buttons with tooltips
`225872FE:A` zoom in, `B` zoom out, `92` rotate CW, `93` rotate CCW, `18` lower, `19` raise, `1A`
level, `17` water, `16` trees, `1B` demolish. Group labels "pick features" (`2CB`) and "adjust
parameters" (`2CC`). Art from Group `22DEC90F`. Terrain generation rules: `11-world-terrain.md`.

### 3.5 City scheme (landscape / flora / building set) [DATA/STATIC]

`Sys/SC3CityScheme.ini` (packed in `SYS.PAK`; a loose `Sys\SC3CityScheme.ini` overrides) lists the
selectable schemes; the selector images are in group `85535958`.

* `[LandScapes]`, `[FloraSets]`, `[BuildingSets]`: key = scheme ID (`0` = the default set; other
  keys are 32-bit IDs shared between sections, e.g. a European scheme uses the same ID in all three).
  Value fields: (1) image group `85535958`, (2) preview image instance (`0x100n` landscapes,
  `0x200n` flora, `0x300n` buildings — one 172×29 / 156×45 / 188×44 picture each), (3) a further
  resource ID, then numeric fields (landscapes: two small integers, a 32-bit resource group such as
  `3FC40000`, two string indices and a sort index; flora/building sets: 0, resource group
  `3FBx0000`, 0, resource group `3FBx0001`, sort index). [DATA layout; field semantics beyond the
  image fields OPEN]
* Flora and building sets load sprite sets from `Res/Sprites/FloraSets/` and
  `Res/Sprites/BuildingSets/` (`13-buildings-occupants.md`, `25-rendering.md`).
* `[ReplaceableBuildings]`: one line per building that the *Building Replacement* dialog may swap,
  key = occupant key in decimal `Type,Group,Instance` (e.g. Type 545184782 = `0x207EDC0E`, Group
  1406 = `0x57E`), value = a developer label (ignored). 323 lines. [DATA]

The city-scheme panel (inside New City and City Properties `855A2DC4`) shows three image buttons
(landscape, trees, buildings); clicking one cycles/chooses among the entries of that section, and
the description field shows the chosen scheme's text. "User-made buildings" (`041F2625:2B`) opens
the Building Replacement dialog: title `041F2625:12F`, a list of replaceable buildings with sizes
(`133`), a selection count `138` (`%x% of %y%`), "replace" `12C`, "clear set" `2BF` with
confirmation `141`, and "import set from selected city" `140`. User buildings are `.bld` files in
`Buildings/` (`*.BLD`, `24-bat-buildings.md`). The choice is saved with the city
(`10-save-format.md`).

## 4. In-game main UI

The in-game screen consists of the full-window 3-D city view (`25-rendering.md`), the control
panel on the left (§2), and a bottom bar. All panels can be hidden with the "hide toolbars"
toggle (`225872FE:A6` / `A9` "show toolbars"); the city view then uses the whole window.

### 4.1 Bottom bar [STATIC]

Background image Instance `82790741` in the resolution group (`00640480`, `00800600`,
`01024768`; `02F78C6D` holds the end cap `82790742`). Contents:

| Element | Strings / images | Behaviour |
|---------|------------------|-----------|
| City name | `225872FE:13` | city name from the save header (`10-save-format.md`) |
| Date | `225872FE:10` | current sim date, month name + year (`14-sim-clock.md`); refreshed on the day/month-changed broadcast |
| Funds | `225872FE:11` | current treasury; refreshed on the funds-changed broadcast |
| Population | `225872FE:40` (`12`) | city population (`20-sim-population.md`) |
| Query button | `225872FE:3C` | arms the query tool (§6.12) |
| News ticker | `225872FE:14`, frame from `SYS.IXF` | see §4.4 |
| RCI indicator | §4.5 | |
| Speed controls | §4.2 | |
| View controls | §4.3 | |

**Field placement and updates** [STATIC]. The four text fields use the rectangles A–D of
`08-ui-framework.md` §4.2: A = city name, B = population, C = funds, D = date. The bar reacts to
six events:

| Event | Payload | Bar reaction |
|---------|---------|--------------|
| City name changed (a city is created or loaded, or renamed) | — | redraw the city name (A) |
| Population changed | — | redraw the population (B), read from the population model |
| Funds changed (whenever the treasury changes) | — | redraw the funds (C) |
| Date changed (from the simulation clock) | new date | store the date, redraw the date (D) |
| Date set (from the simulation clock) | new date | store the date, then redraw all four fields |
| Refresh request | — | read the current date from the clock (fallback values 3000 and 1 if no clock exists), then redraw all four fields |

`[OPEN]` Which clock events produce "date changed" and which "date set"? Probably day/month ticks
and loading or starting a city respectively (`14-sim-clock.md`). Redrawing all fields on both is a
safe implementation.

Formatting: funds are the 64-bit treasury, formatted with the money formatter of
`05-text-and-fonts.md`. A negative balance is drawn in red (255,0,0), otherwise in the bar's
text colour. Population is formatted as a grouped integer. For the Japanese, Chinese, Korean and
Thai languages (language IDs 15 and 17–20, `05-text-and-fonts.md`) the number has no unit suffix; other languages append
the suffix string. [STATIC] The date is formatted from (period, year) by the calendar formatter
of `14-sim-clock.md`. [STATIC]

### 4.2 Speed controls [STATIC]

Images `02F78C6D:52FB06E4`–`52FB06E9` (pause/run and four speed buttons; 48×23 and 10×11 parts).
Tooltips: pause `225872FE:64`, run `65`, speeds 1–4 `66`–`69` (turtle, llama, cheetah, African
swallow). Selecting a speed un-pauses. Sounds: run 23 `UiPlaySim`, pause 24 `UiPauseSim`, speed 1–4
→ 25–28 `UiSimSpeed1`–`4`. Speed semantics (ticks per real second) are in `14-sim-clock.md`.
The speed widget updates its highlighted button whenever the pause state or speed changes,
whatever the cause (hotkeys, modal windows, the simulator), so it always reflects the true state.
Opening a modal window (budget, ordinances, query, …) raises the city's **pending / modal** count
(not the player pause count), which pauses the clock for the duration; closing the modal clears
that count (`14-sim-clock.md` §2.3) [STATIC].

### 4.3 View controls [STATIC]

Buttons with tooltips: zoom in `225872FE:A`, zoom out `B`, rotate clockwise `D`, rotate
counter-clockwise `C`, layer views `A7` (opens §7.1), compass/north indicator `44`, centre `91`,
terrain grid toggle `A5`. Sounds: zoom in 1 `UiZoomIn`, zoom out 2 `UiZoomOut`, rotate 4
`UiRotate`. Zoom has 5 levels and rotation 4 orientations (`25-rendering.md`). "Centre" arms a
one-shot pointer: the next click on the map scrolls that point to the screen centre.

### 4.4 News ticker [STATIC]

A one-line horizontally scrolling strip. Items scroll right-to-left by 2 pixels per step, a step
being taken when more than 50 ms have elapsed since the previous one; consecutive items are
separated by 20 pixels. Items that carry a map location are drawn underlined; clicking such an
item scrolls the view to the location and plays sound 47 `UiClickOnNewsticker`. Urgent items play
54 `NewsTickerAlarm`. Item selection, queues, colours and expiry are specified in
`22-advisors-news-petitioners.md`.

### 4.5 RCI demand indicator [STATIC]

Background image `02F78C6D:42FB77F6` (41×88 px). Three vertical bars labelled R/C/I
(`225872FE:41`/`42`/`43`), drawn in RGB (0,192,0), (0,0,192) and (192,192,0) respectively. Bar
horizontal extents inside the image: R x=5…10, C x=14…19, I x=23…28. The zero line is at y=51;
positive demand grows upward and negative demand downward. Constants stored on the widget: scale
**16.0** px per demand unit, zero-line y **51**, clip bound **62** [STATIC].

**Demand-unit mapping** [STATIC]. The indicator listens for the valve publish of
`15-sim-growth.md` §3.2 step 6 and keeps one float per family valve (3000 → R, 5000 → C,
8000 → I). On each publish of valve *id* with integer reading *N*:

* demand unit *u* = *N* × 0.001 (i.e. *u* = *N* / 1000);
* bar length in pixels = trunc(*u* × 16.0), growing up from y=51 when *u* > 0 and down when
  *u* < 0; *u* = 0 clears the bar.

So |*u*| = 1.0 (≡ |*N*| = 1000 ≡ |*V*| = `Max` for the ±2000 R/C/I valves) is a **full**
positive or negative slot (16 px). A skin that uses *V* × 3 / 2000 as the unit is wrong: the
engine unit is *N*/1000 (= *V*/`Max` when `Max` = 2000), not *V* × 3 / 2000.

Shipped Berlin (`Berlin, Germany.sc3`) load-time family valves [DATA]:

| Valve | *V* | *N* (= *V*×1000/`Max`) | unit *u* | ≈ screenshot fraction of slot |
|---|---:|---:|---:|---|
| 3000 (R) | +1140 | +570 | +0.57 | ~+50–60 % |
| 5000 (C) | +880 | +440 | +0.44 | ~+40 % |
| 8000 (I) | −1680 | −840 | −0.84 | ~−80 % |

(`Max` = 2000, `Min` = −2000 for these three; amenity valves 3001/5001/8001 are not drawn.)

A button under the indicator ("visit SimCity Exchange", `041F2625:190`) opens the user's web
browser after a confirmation; failure messages are `041F2625:1F9`–`1FC`.

## 5. Player tools

### 5.1 Common tool model [STATIC]

Every map tool (menu items with action `COMM` whose class is a tool) is a *game command* object
that, while active, receives the map-view mouse events. The tools share one interaction model:

**Picking.** A screen point is converted to a world position by intersecting the view ray with the
terrain (`25-rendering.md`). World X/Z are fixed-point with 8 fractional bits (256 units per tile);
the tile is `floor(coordinate / 256)`. A pick that misses the terrain, or a tile outside the
editable map rectangle (0 ≤ x < width, 0 ≤ z < height), is ignored.

**States.** `Idle` (hovering), `Dragging` (left button held), plus a transient `Cancelled` state.

| Event | Idle | Dragging |
|-------|------|----------|
| Mouse move to a new tile | compute the **hover preview** for the single tile under the cursor; update the cost read-out | set end tile = tile under cursor; recompute the **area preview** for anchor…end; update the cost read-out |
| Left button down | anchor = end = tile under cursor; enter Dragging; capture the mouse | — |
| Left button up | — | if anchor and end are both inside the map and the drag was not cancelled: **execute** the operation on anchor…end; release capture; hide cost read-out; return to Idle |
| Right button / Escape | deactivate the tool (default pointer returns) [INFERRED] | cancel: discard the preview, nothing is built [INFERRED] |
| Cursor leaves the map view | hide the preview and cost read-out | keep the last end tile |

**Cost read-out.** While a preview exists, the tool asks the owning simulation subsystem for a
*quote*: a pair (valid, cost) for the previewed area. The UI shows the cost next to the cursor
together with an "insufficient funds" state. The insufficient-funds flag is:

  insufficient = (cost ≠ 0) and (cost > current funds)

(funds compared as a signed 64-bit value). The read-out is hidden when cost and preview are cleared.
[STATIC] Colour/placement of the read-out: [OPEN] (see `08-ui-framework.md`).

**Execution and sound.** On release the tool re-quotes and then:

1. if the quote is *invalid* (the subsystem refused the area): nothing changes; play one-shot sound
   **30** (`UiError`);
2. else if *insufficient funds*: nothing changes; play sound **32** (`UiErrorNoMoney`);
3. else: the subsystem performs the operation, charges the quoted cost to the city funds
   (`19-sim-economy.md`), and the tool plays its success sound (table 5.2).

Sound numbers are entries of `Res/Sound/AUDIO.INI` (`06-audio.md`); "one-shot" means section
`[1ShotSfx]`, "fading" means section `[Fading]` (start on press, loop while held, stop on release).

**Undo.** The original has no undo for any tool, and no menu item, hotkey or save record for
one. [STATIC] An implementation must not add a hidden undo that
changes saved state.

**Partial areas.** For drag tools the subsystem quotes and builds only the parts of the area where
the operation is legal (e.g. zoning skips water/occupied tiles; see the owning document). The area
is rejected as a whole (`UiError`) only when nothing in it is legal. [INFERRED from the single
valid/invalid result per quote; confirm against `15-sim-growth.md`, `12-networks.md`]

### 5.2 Tool table

Class IDs are the `MenuItem.INI` command IDs. "Zone code" is the zone-type value written to the
zone layer (`15-sim-growth.md`). "Cursor" is the cursor file shown while the tool is active
(`08-ui-framework.md` §8.2; the `.cur` file names there end in `CURSOR.cur`). Occupant keys are Type `207EDC0E`, Group `0000057E`, Instance as listed
(`13-buildings-occupants.md`).

**Zoning (rectangle drag)** [STATIC]

| Tool | Class | Zone code | Cursor | Success sound |
|------|-------|-----------|--------|---------------|
| Residential light | `42E88586` | 1 | ZONE RES LOW | 38 `UiZoneResidential` |
| Residential medium | `02E88915` | 2 | ZONE RES MED | 38 |
| Residential dense | `A2E8893B` | 3 | ZONE RES HIGH | 38 |
| Commercial light | `02E88949` | 5 | ZONE COM LOW | 38 |
| Commercial medium | `A2E8895A` | 6 | ZONE COM MED | 38 |
| Commercial dense | `02E88968` | 7 | ZONE COM HIGH | 38 |
| Industrial light | `C2E8898F` | 9 | ZONE IND LOW | 38 |
| Industrial medium | `A2E8899D` | 10 | ZONE IND MED | 38 |
| Industrial dense | `02E889AA` | 11 | ZONE IND HIGH | 38 |
| Airport | `E2E889D4` | 14 | ZONE AIR PORTS | 38 |
| Seaport | `02E889C4` | 15 | ZONE SEA PORTS | 38 |
| Landfill | `C2E889B6` | 17 | ZONE LANDFILL | 38 |
| De-zone | `82E88C9C` | 0 | de-zone cursor | 44 `UiDezone` |

All zone tools use the rectangle model of §5.1: the preview is the axis-aligned tile rectangle
spanned by anchor and end (inclusive). The quote and the per-tile legality are owned by the zone
manager (`15-sim-growth.md`).

**Networks (line drag)** [STATIC]

| Tool | Class | Success sound |
|------|-------|---------------|
| Road | `C2B5DE77` | 14 `UiLayRoad` |
| Highway | `42B5DE86` | 14 |
| On-ramp | `02B5DEBB` | 14 |
| Rail | `42B5DE98` | 51 `LayRail` |
| Subway | `C2B5DEA6` | 53 `LaySubway` |
| Power line | `42C2F8BF` | 16 `UiLayPowerLine` |
| Water pipe | `22C2F8C5` | 50 `LayPipe` |

Network tools quote a *path* rather than a rectangle: the path from anchor to end is the
network router's preferred route (straight / one bend / diagonal rules are in `12-networks.md`).
The quote returns validity, cost and the list of path cells used for the preview. Connecting to a
map edge produces the neighbour-connection sounds 36/37/45/46 (`12-networks.md`).

**Terrain (single tile, press-and-hold)** [STATIC]

| Tool | Class (in-city) | In the terrain editor | Sound |
|------|-----------------|------------------------|-------|
| Raise terrain | `825AD7F6` | separate editor variant | fading 18 `UiTerrainRaise` |
| Lower terrain | `C25AD94F` | separate editor variant | fading 19 `UiTerrainLower` |
| Level terrain | `825AD954` | separate editor variant | one-shot 15 `UiLevelTerrain` (rectangle drag) |
| Create water | `02E763C1` | same command | fading 20 `UiLayWater` |
| Plant trees | `4253EC3F` | same command | fading 17 `UiLayTrees` |

The terrain-editor variants (reached from the terrain-editor buttons of §3.4, not from
`MenuItem.INI`) operate before the city exists (no treasury), so no cost read-out is
shown and the funds check of §5.1 never fails [INFERRED]. The in-city variants charge the
`SC3BudgetLayer.ini` costs of §6.2.

Raise/lower act on the tile under the cursor. The quote is the sum of (a) the terrain-change cost
from the terrain subsystem and (b) the demolition cost of anything standing on the affected
tiles; both are charged together. The operation repeats while the button is held [INFERRED:
fading sound "loop while held"; repeat rate OPEN]. On failure the fading sound is stopped and
30/32 is played. Terrain rules: `11-world-terrain.md`.

**Demolish** (`E3166801`, rectangle drag) — removes everything in the rectangle (buildings, networks,
trees, rubble); success sound 5 `UiBulldoze`. Cost = sum of demolition costs (`13-buildings-occupants.md`).

**Building placement ("plop")** [STATIC] — single click; the preview is the building footprint at
the hovered tile. Success sound: a positional "building placed" cue at the tile (`06-audio.md`).

| Tool | Class | Occupant instance |
|------|-------|-------------------|
| Police station | `42FF052F` | `0xD2` |
| Jail | `22FF053F` | `0x1F41` |
| Fire station | `22FF054C` | `0xD3` |
| Hospital | `E2FF0555` | `0xD1` |
| School | `C2FF0563` | `0x1F94` |
| College | `C2FF0571` | `0xD6` |
| Library | `C2FF057A` | `0xF5` |
| Museum | `62FF0585` | `0xD4` |
| Small park | `02FF05AA` | `0x0D` |
| Large park | `82FF05B7` | `0xD5` |
| Zoo | `22FF05CA` | `0xDA` |
| Ballpark | `62FF05D1` | `0xD7` |
| Playground | `C2FF05E0` | `0x234B` |
| Pond | `22FF05EA` | `0x234C` |
| Fountain | `22FF05F3` | `0x234E` |
| Marina | `42FF05C2` | [OPEN] |
| Bus stop | `02FF04FE` | `0x1F96` |
| Train station | `22FF058F` | `0xED` |
| Subway station | `62FF059B` | `0xE9` |
| Subway-to-rail connector | `22FF0521` | `0x3E8A` |

Three further placement commands exist that are not `MenuItem.INI` commands. They place the
occupant groups `0x1F5D–0x1F60`, `0x1F98–0x1F9B` and `0x1F8B–0x1F8E` respectively. Each places
one of four rotation variants; the variant index is (k + viewRotation) mod 4 for the k-th listed
variant [STATIC], i.e. the building always faces the same way relative to the screen. [OPEN]
Which UI element reaches these commands (no menu item or hotkey does)?

**Building pickers** (`E2ED3560` with category parameter): power plants `62E925A2`, water
structures `E2E92527`, garbage `C2F291A9`, landmarks `22E92581`, rewards `62E92553`. These open the
building-picker window (§6.11); choosing a building there arms a plop tool for that occupant.

**Emergency** [STATIC]

* Dispatch police `029A52DD`, dispatch firefighters `229A528C`, dispatch plane (crop duster /
  water bomber) `A5354652`: map tools. Hover shows the dispatch cursor; a click on a tile sends one
  unit of that service to the tile (unit counts and availability: `21-disasters.md`,
  `17-sim-services.md`); sounds 34 `UiDispatchPolice` / 35 `UiDispatchFire` (language-specific
  variants exist in `AUDIO.INI`, see `06-audio.md`). No cost read-out.
* Warning siren `02B5AECF`: immediate command; plays 48 `UiEarlyWarningSiren` and triggers the
  evacuation/warning behaviour of `21-disasters.md`.
* Go to disaster (message `C30177CA`): immediate; the camera centres on the active disaster;
  sound 55 `GoToDisaster`.
* Start disaster ▸ *X*: an **immediate** command (no map click). It sends a "start disaster"
  request with payload (disaster type ID, x = 0, z = 0); the zero location tells the disaster
  manager to choose the location itself (`21-disasters.md`). Disaster type IDs: fire `621CDA33`,
  tornado `02963821`, earthquake `42963812`, riot `4296380E`, UFO `E2963828`, toxic cloud
  `22963800`, whirlpool `13873254`, locusts `13873255`, space debris `13873256` (type IDs as in
  `SC3DisasterLayer.ini`). Non-menu commands send the same request for the parade (`C4C90997`)
  and for one further type that has no entry in `SC3DisasterLayer.ini` [OPEN: which event]. Start-disaster items
  are disabled when disasters are switched off for the city (New City `21D`). [STATIC]

**Query** — see §6.12.

## 6. Windows and dialogs

### 6.0 Window placement `[OBSERVED]`

Measured at 800 × 600 and 1024 × 768, using the frame from its white top-left line to its black
bottom-right line. The position does not depend on other open windows.

| Window | Size | Placement | 800 × 600 | 1024 × 768 |
|---|---|---|---|---|
| Budget | 500 × 480 | centred in the city-view area (client minus 96 px right panel and bottom bar) | (102, 32) | (214, 112) |
| Business deals | 328 × 196 | centred in the client area | (236, 202) | (348, 286) |
| Preferences | 480 × 472 | centred in the client area | (160, 64) | (272, 148) |
| Ordinances | 384 × 354 | fixed | (8, 8) | (8, 8) |
| Meet (advisors) | 383 × 354 | fixed | (8, 8) | (8, 8) |
| Neighbours | 372 × 445 | fixed | (50, 50) | (50, 50) |
| View Data (graphs / charts / data maps, §7.1) | 424 × 440 | fixed | (40, 34) | (40, 34) |
| View City Layers | 296 × 336 | fixed | (32, 32) | (32, 32) |

1280 × 800 was not measured; recommended: the same rules.

### 6.1 Budget window [STATIC]

`[OBSERVED]` (shipped city, February 2067, 1024 × 768).

* **Placement** `[OBSERVED]`: the frame is exactly 500 × 480 px. It is centred in the city-view
  area, which is the client area minus the right control panel (96 px wide) and the bottom bar
  (56 px tall at 800 × 600, 64 px tall at 1024 × 768). That gives (102, 32) at 800 × 600 and
  (214, 112) at 1024 × 768. Other open windows do not change the position. See §6.0.
* **Layout.** A modal panel about 500 × 480 px with two tabs, **Expenditures** and **Income**.
  Top-left is a caption box (`<year> Budget` / `<Month> <year>`). The columns are Adjust,
  Yr. To Date Expense, Year End Estimate and Meet Advisor.
* **Expenditure rows, top to bottom:**
  * Ordinances, Neighbor Deals and Loans, each with an icon button that opens its own window;
  * a blank row;
  * Education, Public Health, Fire, Roads, Police and Mass Transit, each with a horizontal
    funding slider and a "?" advisor button.
* **Number style.** Negative amounts are red, with the minus sign before `§`.
* **Summary block**, two columns: Year to Date Income, Estimated Income, Year to Date Expense
  (red), Estimated Expense (red), Year to Date Cash Flow, Estimated Cash Flow, Current Funds and
  End of Year Funds. A Meet Financial Advisor "?" button sits beside it, and the OK button is at
  the bottom right.
* **Estimates.** In February, the Ordinances estimate was exactly 12 × its year-to-date figure.
  The department estimates were not exact multiples, which suggests they are projected from the
  current funding level. The rule is [OPEN].
* These six slider rows are the six funded departments of `19-sim-economy.md` §6.1.

Opened by Review ▸ Budget (command `025DDC4A`). It is a modal, paged window owned by the
economy subsystem; all values are read from and written to the budget model of
`19-sim-economy.md`. Strings are in table `029541F4`.

**Main page** (title `89`): two columns, Expenditures (`86`) and Income (`87`); each row shows
the year-to-date amount and the year-end estimate and has a button that opens its detail page.

| Column | Rows (label IDs) |
|--------|------------------|
| Expenditures | Police `92`, Fire `94`, Public health `95`, Education `96`, Transit `97`, Ordinances `8F`, Neighbour deals `90`, Loans `236` |
| Income | Residential tax `A3`, Commercial tax `A4`, Industrial tax `A5`, Ordinances `8F`, Neighbour deals `90`, Business deals `A2`, Transit fares `288`, Disaster relief `1AF` |

Summary block: year-to-date income `99`, estimated income `9A`, year-to-date expense `9B`,
estimated expense `9C`, year-to-date cash flow `9D` (= income − expense), estimated cash flow
`9E`, current funds `9F`, end-of-year funds `A0` (= current funds + estimated cash flow for the
rest of the year) [INFERRED formula]. A portrait button "meet financial advisor" (`238`) opens the
advisor window (§6.5) on the budget advisor.

**Detail pages** (title template `8A`, "%s" = row name): an "Adjust" slider (`8B`) for the
funding level (expense rows) or tax rate (tax rows); year-to-date (`8C` expense / `A7` income)
and year-end estimate (`8D`) read-outs which update live as the slider moves; a "meet advisor"
portrait (`8E`) for the responsible advisor; links to the Ordinances (`8F`), Neighbour deals (`90`)
and Business deals (`A2`) windows. Tax pages additionally show an **approval** chart labelled
`1D2` and the tax rate labels `A3`–`A5`; funds (`283`). [STATIC]

**Tax-page approval (`1D2`)** [STATIC]. This is **not** a tax-specific approval percentage and is
**not** computed from R/C/I tax rates, demand, or budget INI keys. It is the city-aura history
chart from the aura layer (`17-sim-services.md` §8.5; save group `C259C02D`), shown under the
window-string label `029541F4:1D2`. The widget is the same chart type used elsewhere for aura
(creation id `0x104`): 12 monthly + 10 yearly + 10 per-decade samples. Each monthly sample is
**cityAura + 127** (unsigned; cityAura is the signed city aura −127…127). A fresh chart is all
127s (neutral). There is no separate mayor-approval metric in the advisor subsystem
(`22-advisors-news-petitioners.md` §7) — the tax page reuses aura under that label. Tax-slider
drags do **not** recompute it; the series advances when the aura layer pushes a month-end sample
(§8.5), and opening the budget binds/loads the chart from the aura record. Aura itself is driven
by `SC3Aura.ini` (`[AgentIDModifiers]`, `[TuningParameters]` `UpdatePeriod`, etc.);
`SC3BudgetLayer.ini` has no approval key. Correction: earlier text only said “approval rating
(`1D2`)” without identifying it as the aura chart.

Adjustment ranges [STATIC]: the tax rate is an integer percent, step 1, range 0–22 (the budget
model clamps any value above 22; `19-sim-economy.md` §4). Funding is an integer percent stored in
one byte, step 1; the budget model applies no upper clamp of its own (shipped saves contain
101–108 %, default 100 %), so the upper limit is whatever the page's adjust control allows
[OPEN: maximum of the funding control]. The adjust control is not the framework slider; it is
drawn with the custom control art of `08-ui-framework.md` §2.3a (group `42DEC7D2`; each row's
details button is image `0x09`). Read-outs are recomputed with the `19-sim-economy.md` charge
formula as the value changes.

**Loans page** (row Loans `236`): a table with columns maturity month/year (`1B3`), total
(`1B4`), amount owed (`1B5`), amount paid (`1B6`); fields borrowed (`1B7`), borrowing limit
(`1B8`), new loan amount (`23B`) and an accept button (`27F`); a note shows the maximum number of
loans (`23D`) and a "consult financial advisor" button (`1B9`). The limits come from
`SC3BudgetLayer.ini` `[Bonds]`: `MaxBonds` (maximum simultaneous loans), `MaxBondAmt` (maximum
amount per loan, in thousands of §), `MonthlyPaymentPer1K` (monthly repayment per §1000
borrowed) and `BondLifespan` (years to maturity). [DATA keys; arithmetic in `19-sim-economy.md`]

**Indicator strip.** One page shows six economic indicators with value templates: city size
(`161`/`1D6`), national population (`165`/`28C`), commerce (`163`/`1DA`), industry (`164`/`1D3`),
unemployment (`168`/`1D5`), pollution (`167`/`1D9`). [STATIC; which page hosts it: OPEN]

### 6.2 Tool costs shown in previews [DATA]

The cost quoted by the tools of §5 comes from `SC3BudgetLayer.ini` (`19-sim-economy.md` owns the
file):

* `[SpecialCosts]` — `LowDensityZone`, `MedDensityZone`, `HighDensityZone`, `LandfillZone`,
  `AirportZone`, `SeaportZone`, `Dezone`: cost per zoned tile; `Demolish`: per demolished tile;
  `DemolishRubbleTile`: per rubble tile cleared; `LevelTerrain`, `RaiseTerrain`, `LowerTerrain`:
  per elementary terrain step; `DecorativeWater`: per water tile created. [INFERRED: per-tile unit]
* `[BuildingPlacementCosts]` — key = occupant group ID (e.g. `16000` road, `17000` rail,
  `17100` power line, `17300` pipes, `16001` highway, `17202` ramp, `17400` subway, bridges
  `16002`–`16004`, tunnels `16006`, neighbour connections `16017`/`17006`/`12102`/`17207`/
  `17310`/`17410`, `300` flora, and every plop-able civic/utility/reward building); value = price
  per placed tile (networks, flora) or per building. A bridge or tunnel section is priced with its
  own key instead of the plain network key.
* For raise/lower terrain the quote also includes the demolition cost of whatever stands on the
  affected tiles (§5.2).

### 6.3 Ordinances window [STATIC]

Opened by Review ▸ Ordinances (message `724A82D7`), from the budget's Ordinances row, and from
the advisor window. Title `029541F4:76` (list header `1C1`). A scrolling list with one row per
ordinance currently *available*: check box, name, current monthly cost/income (`1C3`); footer
"monthly total" (`1C4`) = sum over enacted ordinances. Toggling a check box enacts/repeals the
ordinance immediately (effects and costs: `19-sim-economy.md`). The list and availability come
from `Ordinances.ini` `[ListOfOrdinances]` (key = ordinance ID; value = advisor ID; availability
threshold; name string; cost) and `[ORDINANCE_TRIGGERS]` (conditions that make an ordinance
appear: `HOMELESS_SHELTER_PERCENT`, `EARTHQUAKE_PREP_BUILDINGS`, `TOURIST_PROMOTION_BUILDINGS`,
`AEROSPACE_AVAILABLE_YEAR`). [DATA keys; semantics owned by `19-sim-economy.md`]

`[OBSERVED]` (800 × 600, new city, January 2000, population 0):

* **Frame.** 384 × 354 px with its top-left at (8, 8) of the client area, at 800 × 600 and at
  1024 × 768. There is **no close box**.
* **Strings.** The title is `029541F4:76`. The header row uses `1C2` ("Ordinance") and `1C3`
  ("Current Monthly Cost"), and the footer uses `1C4` ("Monthly Total"). The original does not
  display `1C1` ("Review Ordinances") in this window.
* **Fonts.** Advisor bands, the header row and the footer use the small bold dialog font in dark
  blue; the band text is not the large title font. Ordinance names use the small **regular**
  font. Costs are right-aligned.
* **Cost column.** A row that is not enacted shows the cost it would have if enacted, in grey. At
  population 0 that is −§1, not §0.
* **Title bar.** About 30 px tall, with the title in the large title font.
* **Header row.** "Ordinance" on the left and "Current Monthly Cost" on the right.
* **List box.** From about (4, 62) to (372, 285) relative to the frame, with a vertical scroll bar
  (up and down arrow buttons and a thumb) at its right edge.
* **List rows.** Grouped by advisor:
  * Each group starts with a 24 px band in the darker title blue. The band shows the advisor's
    title in bold and a "?" speech-bubble button at its right that opens that advisor.
  * Below the band come the advisor's ordinances, one 18 px row each: a small check box, the
    name, and the cost right-aligned. The cost is in grey while the ordinance is not enacted.
  * Group order: Utilities Advisor; Health, Education and Aura Advisor; Public Safety Advisor;
    Environment Advisor; City Planner; Transportation Advisor; Financial Advisor.
  * The rows observed, in order:
    * Water Conservation, Power Conservation, Stairwell Lighting, Mandatory Water Meters.
    * Community CPR Training, Free Clinics, Junior Sports, Nuclear Free Zone, Pro-Reading, Public
      Smoking Ban.
    * Mandatory Smoke Detectors, Neighborhood Watch, Crossing Guards.
    * Leaf Burning Ban, Landfill Gas Recovery, Backyard Composting, Paper Reduction Act, Trash
      Presort, Lawn Chemicals Ban, Clean Air, Industrial Waste Disposal, Mandatory Car Smogging,
      Tire Recycling.
    * Industrial Pollutant Impact Fee, Electronics Tax Incentive, Farmer's Market, Conservation
      Corps, Electronics Job Fair, Public Access Cable, Clean Industry Association, Biotech Tax
      Incentives.
    * Parking Fines, Carpool Incentive, Alternate Day Driving.
    * Legalized Gambling.
  * Costs at population 0: −§1 for most rows, and §0 for Leaf Burning Ban, Industrial Waste
    Disposal, Industrial Pollutant Impact Fee, Farmer's Market, Parking Fines and Legalized
    Gambling.
* **Footer.** Below the list box: "Monthly Total" in bold followed by the total. Below that, at the
  bottom right, an OK (check-mark) button of about 44 × 34 px that closes the window.

### 6.4 Business deals window [STATIC]

`[OBSERVED]` The window is opened by the Business Deals row button on the budget's Income tab.
It is 328 × 196 px, centred in the client area (§6.0), with no close box and an OK button.
Each deal row shows the building's display name from `Sc3StringsBuildings`, for example
`A1096A4F:1F48` "Maximum Security Prison" and `A1096A4F:1F55` "Toxic Waste Conversion Plant".
It does not show an internal name.

Title `029541F4:1FE`. Lists current business deals (`1D0`) with each deal's monthly contribution
(`13E`) and the total (`239`); a "finance briefing" (`23A`) text from the financial advisor.
Business-deal offers arrive as petitioners (§6.6) when the triggering reward buildings become
available (casino, toxic waste dump, prison, missile range; `19-sim-economy.md`). Accepting places
the building through the reward picker (§6.11); cancelling a deal removes its income and the
building (`13-buildings-occupants.md`).

### 6.5 Advisor window ("Meet") [STATIC]

Opened by the Meet button (message `724A82D0`) or a portrait in another window. Left: advisor
portraits (`52DB60A0`, instance `0x0AMM` — advisor A 1–7, mood M 1–5 from
`AdvisorMoods.ini`; gold/holiday variants `0x1xxxx`/`0x2xxxx`) with headers "Advisors" (`64`) and
"Petitioner issues" (`63`). Advisor names: `029541F4:156`–`15C`. Choosing an advisor opens the
discussion page: header `75`, tabs "news topics" (`258`), "briefings" (`1BF`) and "ordinances"
(`76`). Each topic shows a title, message text and a topic picture (`831D112B:<id>` via
`Advisor-TopicGfxIDs.ini`). Topic generation: `22-advisors-news-petitioners.md`.

### 6.6 Petitioner dialog [STATIC]

Shown when a petitioner arrives (`22-advisors-news-petitioners.md`). Portrait `52DB60A0:FF01`–`FF15`,
name, heading and message from the petitioner string tables; two choice buttons (the
petitioner's *Button1*/*Button2* strings); "take action" (`029541F4:78`), "impact analysis"
(`79`) which shows the responsible advisor's response (`80`) and the city council's response (`81`)
with "return to petitioner" (`82`), "done" (`83`), "back" (`84`), "OK" (`85`); and "dismiss
petitioner" (`2DE`). Choosing a button applies the petition (enact ordinance, accept deal, …) and
plays a crowd reaction (sounds 70–73: boo, disappointment, neutral, cheer — chosen by the
petitioner's reaction field). [STATIC; reaction choice rule in `22`]

### 6.7 Neighbours window and neighbour deals [STATIC]

`[OBSERVED]` (800 × 600 and 1024 × 768): the frame is 372 × 445 px with its top-left at (50, 50)
of the client area. It has a close box. On opening, the first neighbour (here the north-west one) is selected:
its diamond has a red outline and its name is in the City field.

Opened by Review ▸ Neighbors (`630288E9`, toggle). Title `029541F4:117`; a map with up to four
neighbour cities placed by compass side (`11D` N, `11E` W, `120` S, `11F` E). Selecting a
neighbour shows its name (`29A`), national population (`165`), current deals (`13D`) with rate
(`29B`, in § per unit: `298` per 1000 MW-h, `296` per 1000 m³, `297` per 100 tons) and monthly
contribution (`13E`), and "discuss deal" (`29C`). Deal kinds: sells you power (`143`), buys your
power (`140`), sells you water (`144`), buys your water (`141`), takes your garbage (`142`), gives
you garbage (`145`). Deal offers and terms come from `NeighborDeals.ini` (`19-sim-economy.md`);
an offer is presented as a petitioner-style dialog with accept/decline. Breaking a deal early
incurs the penalty defined there.

**Layout and compass** [STATIC][OBSERVED]:
* The map diamond is drawn at view rotation 0 with the region image of `11-world-terrain.md` §9
  turned 45° clockwise.
* The four neighbour cells sit on the diamond's **diagonal edges**:
  * neighbour 0 (map edge x = 0) is the **upper-right** cell;
  * neighbour 1 (z = 0) is the **upper-left**;
  * neighbour 2 (x = W−1) is the **lower-left**;
  * neighbour 3 (z = H−1) is the **lower-right**.
  Each cell is outlined in light blue, and the selected cell is outlined in red.
* The compass rose (60 × 60 image `000005E5:0x0D`, at (20, 50) in the window) has four letter
  labels at its top, left, bottom and right positions. Label k gets N, W, S, E for
  (rotation + k) mod 4 = 0, 1, 2, 3 respectively, so the letters turn with the view rotation. At
  rotation 0, N is at the top.
* The neighbours are therefore **not** on compass points: at rotation 0, neighbour 0 is north-east,
  1 north-west, 2 south-west and 3 south-east. No text names a neighbour by direction.

The terrain shown in the map diamond (the city in the centre and invented land or sea for the
four neighbours around it) is rebuilt each time the window is built. It is deterministic: it uses
a fixed seed and depends only on the city's terrain and its edge sea-water flags. Nothing about
it is saved. The full algorithm, colour ramp and display orientation are in
`11-world-terrain.md` §9.

### 6.8 Preferences dialog [STATIC]

Opened by Options ▸ Preferences (`C3001401`). Title `029541F4:14E`. Groups and options:

| Group | Option (string) | Values |
|-------|-----------------|--------|
| Performance `1FF` | Traffic visible `200` | at closest 3 zooms `201`, closest 2 `202`, closest `203`, off `20A` |
| | Sims visible `207` | closest 2 zooms `208`, closest `209`, off `20A` |
| | Other animations `204` | on `205`, off `206` |
| | Screen resolution `20B` | list of supported modes (`27-platform.md`) |
| Sound `292` | Music `153`, Ambient sound `219`, Sound effects `14F` | volume sliders (moving the ambience slider plays sound 67) |
| | Enable 3D sounds `291` | check box |
| | Music choices `21A` | per-track check list; "preview highlighted score" `294` plays the selected track |
| General `2D0` | Show tips at startup `293` | check box (same flag as the TOD dialog) |
| | Auto go to disasters `21F` | check box: camera jumps to new disasters |

Changing the resolution (or other restart-only settings) asks `015E410C:18` (restart now?).
There is **no language or country control** anywhere in Preferences. The game language comes only
from the installation/registry at start-up (`27-platform.md` §1), so switching language at run
time is out of scope. [OBSERVED]
Settings persist in the program configuration (`27-platform.md`).

### 6.9 City properties [STATIC]

Options ▸ City properties (`855A2DC4`) reuses the New City form in "edit" mode (title
`029541F4:281`) showing city name, mayor name and the city-scheme panel (§3.5); size, start year
and difficulty are not editable. [INFERRED from shared form]

### 6.10 Scenario goals [STATIC]

Review ▸ Scenario goals (`844587AE`, only enabled in a scenario): the scenario briefing
(`041F2625:3`), goals list (`4`) and status (`5`), titled `24`; content from the scenario script
(`23-scenarios.md`).

### 6.11 Building picker (power, water, garbage, landmarks, rewards) [STATIC]

Command `E2ED3560` with a category parameter. Title by category from the building-catalogue table
`62E69238`: landmarks `44`, rewards & opportunities `2A`, power plants `1`, waste management `75`,
water structures `1D`, generic `7C`. A list "buildings available" (`76`) of the occupants of that
category whose availability conditions hold. For power plants and water structures the condition is
the per-type unlock flag of the utilities advisor (`22-advisors-news-petitioners.md`, section
"Building availability"); the nuclear plant (12115) is also hidden while the Nuclear Free Zone
ordinance (0xE0D0722E) is in force. The cheats `power to the masses` and `water in the desert`
bypass the unlock flag (`27-platform.md`). Garbage facilities use the same unlock flags of the
environment advisor (`[GARBAGE_BUILDINGS]`; cheat `garbage in, garbage out` bypasses them).
**Landmarks have no availability condition** [STATIC]. The city-planner advisor keeps a landmark
list holding one entry per building exemplar in the loaded catalogue whose agent type is 18000 or
a descendant (`13-buildings-occupants.md`). It is rebuilt from the catalogue when the city's
catalogue is set up, and keeps the flags of entries that already exist. Each entry has an
"available" flag, set to 1 when the entry is created and never cleared for landmarks, and a "placed"
flag. "Placed" is set to 1 when a building with that exemplar key is added to the city and back to
0 when one is removed. Every entry is listed. Placed ones are shown greyed and marked `70`, as
below. They are not otherwise filtered: no population, year, money, scenario or advisor condition
applies [STATIC]. List order is the landmark list's order, sorted by exemplar key [INFERRED].
Whether "placed" is saved is [INFERRED: no; it is re-derived from the building-added notifications
as the city's buildings are re-created on load]. Rewards follow the reward manager's earned state. Each entry shows each with picture, name, size (`041F2625:133` "WxH") and cost; landmarks
and rewards already placed are greyed and marked `70`. Empty-list messages: landmarks `77`, rewards
`6E`, power `7A`, waste `79`, water `7B`, other `FFFFFFFF`. Choosing a building arms the plop tool
(§5.2) for that occupant; the picker closes.

### 6.12 Query tool and query popups [STATIC]

`[OBSERVED]` The query tool is the green "?" button in the bottom bar. A click on the map opens
a modal **Query** panel.
* **Flora:** the name (e.g. a pine species) and a small picture, then Elevation (in metres, e.g.
  "26 Meters"), Land Value (word band), Zone Type, Powered (Yes/No), Watered (Yes/No),
  Flammability (word band), Air Pollution (word band) and Water Pollution (word band).
* **Power plant:** a large picture and the name, then:
  * Age (Max) as "N yrs. (M yrs.)": current age and maximum life in whole years;
  * Elevation;
  * Pollution Generated, a word band shown in red when "Hazardous";
  * Maximum Power Capacity, Current Power Capacity and Usage, all in MW-h/Month;
  * a **Replace** button next to OK.

The query tool is the default pointer action (and the bottom-bar "Query" button,
`225872FE:3C`). Clicking a tile opens a popup titled `82E0074C:1`. The popup is a list of
(label, value) lines. Every simulation subsystem that owns something at the tile contributes
lines, in this order: terrain → zone/land value → the occupant's own lines → common status
lines. All strings
are in table `82E0074C`; labels end in a colon, values are either fixed band words or `%s`
templates filled with a formatted number. An occupant with special "flavour" lines (see below)
adds them after its functional lines.

**Common lines (any tile / any occupant)**

| Line | Label | Value | Rule |
|------|-------|-------|------|
| Elevation | `2` | `3` (metres, number) | terrain height of the tile (`11-world-terrain.md`); water tiles show depth with label `17F` |
| Surface type (no occupant) | — | `C1` dirt / `C2` fresh water / `C3` salt water | |
| Zone type | `E` | `F`–`17` (light/medium/dense R/C/I), `18` seaport, `19` airport, `1A` landfill, `195` unzoned | zone code of the tile (§5.2) |
| Land value | `8` | `1C0`, `9`, `A`, `B`, `C`, `D` | land-value byte *v* (0–255): v<35 → `1C0`; v<75 → `9`; v<130 → `A`; v<175 → `B`; v<225 → `C`; else `D` |
| Status suffix (appended to the building name) | — | `C4` under construction, `C5` abandoned, `C6` covered in garbage, `C7` destroyed, `186` workers on strike, `196` aging, `197` defunct, `1B4` decommissioned, `1B8` no access to city | from the occupant's state flags |
| Make historical | button `30` | — | shown for zoned (growable) buildings; toggles the historical flag (`15-sim-growth.md`) |
| Clean-up cost | `31` | `32` (money) | rubble/garbage/destroyed tiles: the demolition cost |
| Funding cost | `86` | `87` (money per month) | every funded civic building |

**Crime** (label `1B`) — crime byte *c*: c=0 → `1C`; c<47 → `1D`; c<84 → `1E`; c<122 → `1F`;
c≤150 → `20`; c>150 → `21`. [STATIC]

**Flammability** (label `22`) — byte *f*: f=0 → `23`; f<72 → `24`; f<129 → `25`; f<187 → `26`;
f≤229 → `27`; f>229 → `28` ("Extreme"), and the value is drawn in red. [STATIC]

**Pollution generated** (label `29`) — produced amount *p* compared with the baseline
*b* = `MaxAirPolluteForUI` + `MaxWaterPolluteForUI` (`SC3Pollution.ini [TuningParameters]`;
shipped 11000 + 11000 = 22000). The bands are: p≤0 → `2A`; p<b/5 → `2B`; p<5b → `2C`;
p<10b → `2D`; p<50b → `2E`; else `2F`.
p = air·round(1.0466667·r_air²) + water·round(1.0466667·r_water²), where air, water, r_air and
r_water are the occupant's pollution modifier entry (`17-sim-services.md`). Additional lines: air
pollution `187` (`188`–`18D`), water pollution `18E` (`18F`–`194`), and for landfill zones
capacity `10C`/`10D` and usage `10E`/`10F` (percent full). [STATIC]

**Per-occupant-family lines** (labels → value templates) [STATIC]

| Occupant family | Lines |
|-----------------|-------|
| Police station | `88`/`89` crimes per month; `8A`/`8B` arrests per month; `8C` rating `8D`–`90`; flavour `91`/`92` |
| Jail | `93`/`94` inmates; `95`/`96` cells; `97` conditions `98`–`9B` |
| Fire station | `9C`/`9D` fires per month; `9E`/`9F` calls per month; `A0` rating `A1`–`A4`; flavour `A5`/`A6` |
| Hospital | `A7`/`A8` beds (system-wide); `A9`/`AA` patients; `AB`/`AC` doctors; `AD` grade `AE`–`B2` |
| School | `B3`/`B4` desks; `B5`/`B6` students; `B7`/`B8` teachers; `B9` grade `BA`–`BE`; flavour `BF`/`C0` |
| College | `C8`/`C9` capacity; `CA`/`CB` students; `CC`/`CD` professors; `CE` grade `CF`–`D3` |
| Library | `D4`/`D5` books system-wide; `D6`/`D7` books in circulation; `D8` grade `D9`–`DD` |
| Museum | `DE`/`DF` attendance (last year); `E0`/`E1`, `E2`/`E3` flavour; `E4` grade `E5`–`E9` |
| Parks | `EA`/`EB` acres system-wide; `EC`/`ED` benches; zoo `EE`/`EF`, `F0`/`F1`; ballpark `F2`/`F3`, `110`/`111` win–loss, `112`/`113`, team name `1AD`; marina `F4`/`F5` |
| Landmarks / rewards | `F6`/`F7` date built; `118`/`119` monthly income; one or two flavour lines per building chosen from `F8`–`17E`, `1B9`–`1BE` |
| Roads / rail / subway | `3C` traffic `3D`–`41`; `43`/`49` rail usage `44`–`48`/`4A`–`4E`; stations: `37`/`38` passengers per day; transit service quality `181` (`182`–`185`) |
| Airport | `FE` air traffic `FF`–`102`; `103`/`104` on-time percentage |
| Seaport | `105` port traffic `106`–`109`; `10A`/`10B` flavour |
| Power plant | `198`/`1B7` max capacity; `56`/`57` current capacity; `58` usage `59`–`5D`; age `1B5`/`1B6` (current and maximum years) |
| Power line / any powered tile | `4` powered `5`/`6` |
| Water pump / tower | `180` max capacity, `5E`/`5F` current; `6B` usage `6C`–`70`; `73` connected to pipes `74`/`75`; `7` watered; `60` effect of water pollution `61`–`65`; desalination `66` coast `67`/`68`, flavour `69`/`6A`; age `1B5`/`1B6` |
| Water treatment | `1A2`/`1A3` capacity `1A4`; `1A5` usage `1A6`–`1AA` |
| Garbage (incinerator, WTE) | `1AB` max, `76`/`77` current disposal capacity; `78` usage `79`–`7D` |
| Recycling centre | `1AE` citywide recycling `1AF`/`1B0`; `1B1`/`1B2` flavour |
| Neighbour connection | `54`/`55` connected-to (neighbour name) |

Band thresholds for traffic lines follow the same 72/129/187 split points as flammability on a
0–255 scale [STATIC]. The utility **Usage** lines (`58` power, `6B` water) show a **number**
("0 MW-h/Month", "0 m³/Month"), not a band word. The band strings `59`–`5D` and `6C`–`70` are
never referenced by code. [STATIC][OBSERVED] The numbers themselves are owned by the
simulation documents (`16`, `17`, `18`).

Query on a building in a scenario or holiday building set uses the same lines. Holiday-specific
flavour strings are in `SC3StringsBuildings_Holidays.IXF` (`13-buildings-occupants.md`).

#### 6.12.1 Query panel layout

Coordinates are pixels relative to the panel's top-left corner. y values are the **top of the FBF
line box** (`05-text-and-fonts.md` §4; ink starts 3 px lower in `main9`/`main10`). Measured at
800 × 600 on the original. [OBSERVED] The sizes are code constants, so they do not change with
resolution. [STATIC]

**Layout types.** There are three fixed sizes; the panel never computes its size from the row
count or text width. [STATIC]

| Type | Size (w × h) | Used when | Picture |
|---|---|---|---|
| 1 | 308 × 280 | the target has no picture: bare ground, water, road, rail, highway, subway, power line, pipe, bridge, landfill zone | none |
| 2 | 308 × 395 | the target has a picture with height/width ≤ 1.2 | top, centred |
| 3 | 423 × 320 | the target has a picture with height/width > 1.2 (tall, narrow sprites: tall buildings, tall flora) | left, centred |

The ratio test uses the rendered picture's size. [STATIC]

**Position.** The first panel opens with its top-left at (40, 40) relative to the game's client
area, wherever the click was. [STATIC][OBSERVED] The panel can be dragged by its title band. Later
panels open where the last one was left, and a drag does not snap back or recentre. [OBSERVED]
The original reads a saved position before falling back to (40, 40). After a game restart the
default was used again; whether the position survives a clean exit was not verified.
[STATIC][OBSERVED]

**Chrome** (the same window chrome as the game's other modal dialogs) [OBSERVED]:
* Top edge, from y = 0 down: 1 px white, then 3 px #EFEBFF, then 1 px #ADB6FF. The title band
  (#7B92FF) runs from y 5 to y 33 (29 px). Below it come 1 px #ADB6FF and a few px of light blend
  into the body.
* Left edge, from x = 0 across: 1 px white, then 3 px #CED3FF, then 1 px #ADB6FF, then a 6 px
  #7B92FF bar at x 5–10 running from the title band to the bottom, then 1 px #ADB6FF.
* Right and bottom edges: 1 px black (x = w−1, y = h−1). There is no light border inside the
  black.
* Body: the standard light-blue cloud texture of the game's dialogs.
* Title: `82E0074C:1` "Query", in `title12`, colour ≈ #DEDFFF, ink from x 8, y 14–30.
* Close box: the dialog close button image, 23 × 23 at (w−31, 7).

**Picture** [OBSERVED; layout STATIC]:
* The occupant's own sprite, drawn with transparency over the cloud background (no fill). It is
  framed by a 1 px #7B8AFF rectangle that sits 1 px outside the image. The box size follows the
  image. Observed sizes: coal plant 130 × 110, 3 × 3 industry 98 × 79, small tower 98 × 77,
  bamboo flora 102 × 135, tall abandoned building 66 × 83.
* Type 2: the box is centred on (160, 97). Type 3: the box is centred on (77, 160).
* The picture is rendered from the occupant by a thumbnail renderer. The exact zoom and scale it
  uses are [OPEN]. Types-1 targets have no picture.

**Name and status lines** (`main10`, default text colour RGB 0x31 0x36 0x7C):
* Name: type 1 and type 2 at x 22; line top 42 (type 1) or 160 (type 2). Type 3 at x 140, line
  top 42.
* Status suffix (`C4`–`C7`, `186`, `196`, `197`, `1B4`, `1B8`, shown as "(Abandoned)",
  "(Decommissioned)" and so on) goes on its own line 20 px below the name, indented by 15 px.
* **Editable name** (rewards and landmarks with a player-renamable name, and marinas): the name is
  drawn in a single-line edit field instead. The field is a white rectangle at (21, 159),
  278 × 18, inside a 1 px #7B8AFF border at (20, 158), 280 × 20; text starts at x 22. Rename
  behaviour: see the next batch [OPEN].

**Rows** (`main9`, 14 px pitch = the font's line height):
* Label column x and value column x:

  | Layout | Label x | Value x |
  |---|---|---|
  | Types 1 and 2, no scrollbar | 23 | 160 |
  | Types 1 and 2, scrollbar shown | 23 | 150 |
  | Type 3 | 141 | 276 |

  Labels are left-aligned and values are left-aligned at the value column.
* First row: one blank line (14 px) below the last name or status line. That gives line top 71
  (type 1), 189 (type 2), 209 (type 2 with a status line), 71 (type 3), and 91 (type 3 with a
  status line).
* **Groups:** each contributing subsystem's lines form a group, and groups are separated by one
  blank row (14 px). See the per-target table below.
* **Wrapping:** a label wraps to the next row at a word boundary when it does not fit before the
  value column. Examples: "Reason for / Abandonment:", "Maximum Power / Capacity:", "Students in /
  Attendance:", "Number of Eggs / Thrown:". The value is drawn on the label's **last** line. Values
  wrap the same way within the value column ("Lack of chocolate / sprinkles"). Each wrapped line
  takes one 14 px row. No truncation or ellipsis was seen.
* **Colours:** values are red (#FF0000) for "No" in Powered/Watered and for "Hazardous" in
  Pollution Generated. Everything else uses the default navy text colour.
* **Scrolling** (type 2): rows are clipped to a viewport ending at y 354 (just above the buttons).
  When the rows overflow, a vertical scrollbar appears at x 285–300, from the first row's top − 1
  down to y 354. Measured: (285, 188, 16 × 167) without a status line; (285, 208, 16 × 147) with
  one. The value column then moves to x 150. Zoned buildings with a "Reason for Abandonment" row
  and colleges overflow; most other targets fit.

**Buttons** (h = panel height, w = panel width) [OBSERVED]:
* **OK**: checkmark image button, 42 × 32 at (w−50, h−37). It is the only control on most
  targets.
* **Replace** (`82E0074C` button text "Replace"): 73 × 33.
  * Alone: at (16, h−38).
  * With Make Historical: at (20, h−58), above the check box.
  * It closes the query panel and opens the **Building Replacement** dialog (§ "Building
    Replacement", strings `041F2625:12F` …), titled with the footprint size, e.g. "Building
    Replacement 4x4". That dialog lists the replaceable buildings of the same footprint; nothing is
    charged until it is confirmed there.
* **Make Historical**: a 13 × 13 check box (white face, dark 1 px frame) at (20, h−23) in type 2,
  or at (138, h−23) in type 3. Label `82E0074C:30` "Make Historical" in `main10` starts 14 px right
  of the box.
* Which targets get which controls:

  | Target | Replace | Make Historical |
  |---|---|---|
  | Zoned (growable) building, not abandoned | yes | yes |
  | Abandoned zoned building | no | yes |
  | Civic/service buildings (fire station, college …), power plants, rewards (e.g. Mayor's House), marinas | yes | no |
  | Landmarks such as "The Wall", flora, networks, terrain, water, landfill | no | no |

**Behaviour** [OBSERVED]:
* The panel is modal.
* The **simulation is paused while the panel is open**: the date did not advance in 20 s at the
  fastest speed. It resumes when the panel closes. The news ticker keeps scrolling.
* **Closes:** OK and the close box. **Does not close:** Esc, Enter, Space, or clicking outside the
  panel; an outside click does not start a new query either.
* Arrow-key scrolling of the map is ignored while the panel is open.
* The query tool stays armed after the panel closes.
* The contents are a snapshot. Because the simulation is paused, nothing changes while the panel
  is open.

**Per-target rows** (one row per line, `|` = blank-row group separator) [OBSERVED]:

| Target (layout) | Name | Rows |
|---|---|---|
| Zoned building (2 or 3) | building name (+ status) | Elevation, Land Value, Zone Type, [Reason for Abandonment — abandoned only], Powered, Watered, Crime, Flammability, Air Pollution, Water Pollution (single group) |
| Flora (2 or 3) | flora name | Elevation, Land Value, Zone Type, Powered, Watered, Flammability, Air Pollution, Water Pollution |
| Landmark "The Wall" (2) | name | Elevation, Land Value, Zone Type, Powered, Watered, Crime, Flammability, Air Pollution, Water Pollution |
| Fire station (2) | name | Elevation, Powered, Watered \| Number of Fires, Number of Calls, Performance Rating, (flavour e.g. Spots on Dalmatian) \| Funding Cost |
| College (2) | name | Elevation, Powered, Watered \| Capacity, Students in Attendance, Number of Professors, Grade … |
| Reward (Mayor's House) (2) | editable name | (flavour, e.g. Number of Eggs Thrown) \| Date Built |
| Marina (2) | editable name | Depth \| Boats Berthed |
| Coal power plant (2) | name | Age (Max), Elevation, Pollution Generated \| Maximum Power Capacity, Current Power Capacity, Usage |
| Bare ground (1) | "Dirt" (`C1`) | Elevation, Land Value, Zone Type, Powered, Watered, Crime, Flammability, Air Pollution, Water Pollution |
| Water (1) | "Fresh Water" / "Salt Water" | Depth, Air Pollution, Water Pollution |
| Road / highway (1) | "Road" / "Highway" | Elevation \| Traffic |
| Bridge (1) | "Bridge Ramp" (piece name) | Elevation \| Traffic |
| Rail, rail station (1) | "Railroad" / "Train House" | Elevation \| Rail Usage |
| Power line (1) | "Power Line" / "Power Line Terminus" | Elevation, Powered |
| Landfill zone (1) | "Landfill" (+ status, e.g. "(Decommissioned)") | Elevation \| Capacity Citywide, Usage |
| **Underground view:** water pipe (1) | "Water Pipe" | Watered (no Elevation) |
| **Underground view:** subway (1) | "Subway" | Rail Usage (no Elevation) |
| **Underground view:** tile with nothing underground | — | the surface tile's normal query (e.g. Dirt) |

Not yet observed: neighbour connections (use the `54`/`55` "Connected to" line from §6.12),
airport zones, pumping stations, highway on-ramps and other station kinds.

#### 6.12.2 Query panel: targeting, commit rules, extra targets

**Target selection** [STATIC]: the query uses the object picked under the pointer by the view's
per-pixel sprite hit test, which is the same pick as the hover highlight (`25-rendering.md` §9.4).
If no sprite is hit, the clicked tile (world coordinate >> 8) is used, in this order:
1. the network piece on that tile, if any;
2. otherwise, if the terrain type is water, the water lines;
3. otherwise, the ground lines.

Overlapping sprites therefore resolve to the topmost opaque pixel, and a click on a multi-tile
building anywhere on its opaque pixels queries that building. In the **underground view** the same
rules apply to underground pieces (pipe, subway). A tile with nothing underground gives the
surface tile's query. [OBSERVED]

**Commit rules** [OBSERVED]:
* **Make Historical:** toggling the box changes only the box. The occupant's historical flag is
  written when the panel is closed with **OK**. Closing with the close box **discards** the change.
  The next query on the same building shows the stored state.
* **Editable name:** apply on OK and discard on close, the same as Make Historical. [INFERRED; not
  exercised]

**Hover:** while the panel is open the map gets no hover highlight. [OBSERVED]

**Red values:** each subsystem can flag a value as an alert, and flagged values are drawn in
#FF0000. [STATIC] Observed or traced alerts:
* Powered "No" and Watered "No";
* Pollution Generated "Hazardous";
* Flammability "Extreme";
* a utility's current capacity of 0 (e.g. "0 m³/Month" on an unpowered water tower);
* recycling "Inadequate".

**Sounds** (`06-audio.md`):
* opening the panel on an occupant plays that occupant's `UiQuery…` sound (§6 of `06`);
* the close box plays `CloseWindow` (56);
* the check box plays `UiCheckboxClick` (13) [INFERRED];
* OK and Replace play `UiButtonClick` (12) [INFERRED].

There is no cursor change over the panel and no open/close animation. [OBSERVED]

**More observed targets** [OBSERVED]:

| Target (layout) | Name | Rows | Controls |
|---|---|---|---|
| Empty zoned tile, e.g. Seaport zone (1) | "Dirt" | Elevation, Land Value, Zone Type, **Reason for Lack of Development** (flavour value, e.g. "Planets not aligned"), Powered, Watered, Crime, Flammability, Air Pollution, Water Pollution | — |
| Water tower (2) | name | Age (Max), Elevation, Powered \| Maximum Water Capacity, Current Water Capacity (red when 0), Usage, Effect of Water Pollution | **no Replace** |
| Recycling center (2) | name | Age (Max), Elevation, Powered \| Citywide Recycling Capacity (band, red "Inadequate"), flavour (Milk Containers Processed) | Replace |
| Solar power plant (2) | name | Age (Max), Elevation, Pollution Generated \| Maximum Power Capacity, Current Power Capacity, Usage | Replace |
| Landfill tile holding garbage (2) | "Landfill" | Elevation \| Capacity Citywide, Usage | — (it has a picture: the garbage sprite) |

Correction to the §6.12.1 controls table: utility buildings such as water towers have **no**
Replace button. Power plants, recycling centres and civic/service buildings do.

**Picture scale:** the thumbnail renderer's zoom choice is [OPEN]. Zoom is **not** fixed:
* a 4 × 4 plant gives 130 px, which matches zoom 2;
* a 3 × 3 industry gives 98 px, which matches zoom 2;
* a 1 × 1 tall building gives 66 × 83, which matches zoom 3;
* 1 × 1 flora gives 98 × 97 and 102 × 135, which match zoom 3–4.

Recommended [INFERRED]: use the largest zoom whose image fits in about 128 px of width and about
135 px of height.

**Make Historical flag** [STATIC][OBSERVED]:
* The box shows, and OK writes, bit **0x200** of the occupant state word: the "protected" flag of
  `13-buildings-occupants.md` §3. It is not one of the two per-occupant save booleans.
* Effects of the flag:
  * the zone developer never picks the lot for redevelopment or replacement (`13` §3);
  * when growth changes the building's state (occupied ↔ abandoned and so on), the bit is carried
    over into the new state word;
  * an abandoned protected building is skipped by the developer's abandoned-lot handling;
  * the **bulldozer refuses it**: the hover tint is red instead of cyan, and a click does nothing
    and charges nothing.
* Disasters ignore the flag.

**Reason rows on zoned tiles** [STATIC]: strings are in table `041F2625` (`SEStringsUI.IXF`). Labels:
`2C1` "Reason for Abandonment:" and `2C2` "Reason for Lack of Development:". The value comes from a
**stored per-tile reason code** that the growth simulation keeps for the zone. It is not random at
query time. Code → (label, value):

| Code | Label | Value |
|---|---|---|
| 1 | 2C1 | 2CB Radiation levels are too high |
| 2 | 2C1 | 2C3 Land value is too low |
| 3 | 2C1 | 2C7 Lack of demand for this zone |
| 4 | 2C1 | 2C8 No power |
| 5 | 2C1 | 2C9 No water |
| 6 | 2C1 | 2CA Poor transportation to other zones |
| 7 | 2C1 | 2C4 Pollution levels are too high |
| 8 | 2C1 | 2C5 Lack of chocolate sprinkles |
| 9 | 2C2 | 2CC Zone is too small |
| 10 | 2C2 | 2CB Radiation levels are too high |
| 11 | 2C2 | 2CD No demand for low land value |
| 12 | 2C2 | 2CE No demand for high land value |
| 13 | 2C2 | 2C7 Lack of demand for this zone |
| 14 | 2C2 | 2C8 No power |
| 15 | 2C2 | 2C9 No water |
| 16 | 2C2 | 2CA Poor transportation to other zones |
| 17 | 2C2 | 2CF Zone is not on a coast line |
| 18 | 2C2 | 2D1 Planets not aligned |

When the row is shown:
* only on zoned tiles;
* on an **empty** zoned tile: always shown, and code 0 shows 2C2 + 2D1;
* on a tile with a building: only if the building is **abandoned**, and code 0 shows 2C1 + 2C5;
* occupied (functional) buildings get no reason row.

The row is placed directly after Zone Type. Which growth step writes each code is not traced; it
belongs to `15-sim-growth.md` [OPEN]. Recommended: the developer stores the reason for its last
refusal on the tile (codes 9–18) and the abandonment cause when it abandons (codes 1–8).

**Neighbour connection** [STATIC]:
* A network piece on the map edge that is a neighbour connection adds the line `54` "Connected
  To:" with value `55` = the neighbour city's name. That name is taken from the neighbour on the
  piece's side: x = 0 → side 0; y = max → side 3; x = max → side 2; otherwise (y = 0) → side 1.
* After it come the piece's normal network lines (Elevation | Traffic or Rail Usage). Layout type
  1.

**Remaining targets** [INFERRED; not observed]:
* Airport zone tile: the same rows as the seaport zone tile above, with Zone Type "Airport".
* Pumping station: the same rows as the water tower.
* Highway on-ramp: like Highway, with the piece's own name.

#### 6.12.3 Query values of service, culture and transport buildings [STATIC]

The tables below use these terms:
* **M** is the city's demographics layer (the population, health and education model of
  `20-sim-population.md`).
* **N(t)** is the citywide count of buildings of agent type t.
* **t** is the aura byte (−128..127) at the building's tile, and **a** is the city's average aura
  (`17-sim-services.md` §8).
* **LV** is the land-value byte at the tile.
* All divisions are integer and all float→int conversions truncate, unless a row says otherwise.

The flavour numbers are deterministic formulas, not random. "red" marks the alert colour.

**Health and education** (systemwide values from M):

| Building | Line | Value |
|---|---|---|
| Hospital | Beds (Systemwide) `A7` | Σ beds of all hospitals (`OptimalBedCount` each) |
| | Patients `A9` | the patients count of `20` §5.1; **red** when beds < patients |
| | Doctors `AB` | Σ funded doctors of all hospitals |
| | Grade `AD` | health rating 0–4 (`20` §5.2): 4 → A, 3 → B, 2 → C, 1 → D, 0 → F; D and F are red |
| School | Desks `B3` | Σ school capacity |
| | Students `B5` | school-age population (ages **6–18**); **red** when desks < students |
| | Teachers `B7` | Σ funded teachers |
| | Grade `B9` | school rating 0–4 (`20` §4.4): 4 → A, 3 → B, 2 → C, 1 → D, 0 → F; D and F are red |
| | Apples for the Teacher `BF` | v = max(0, t)/10 (integer), +3 if the reading ordinance `0xE0D07233` is on, +2 if junior sports `0x815B4CEF` is on |
| College | Capacity `C8` | Σ college capacity |
| | Students in Attendance `CA` | college-age population (ages 19–24); **red** when capacity < students |
| | Professors `CC` | Σ funded professors |
| | Grade `CE` | college rating 0–4, mapped as for School |
| Library | Books Systemwide `D4` | B = EQ×19 + P_lib×10 + S×13 − 52 + (day mod 17), where EQ = M's average EQ of ages 20–65 (byte), P_lib = Σ library capacity (`PopServed`), S = school rating, day = second value of the city date |
| | Books in Circulation `D6` | min(population, EQ × B / 303 + (first date value mod 17)) |
| | Grade `D8` | cov = 100 if population = 0, else min(100, P_lib × 100 / population); s = funding × cov / `[Library] MonthlyUpkeep`; s ≥ 88 → A, ≥ 72 → B, ≥ 64 → C, ≥ 47 → D (red), else F (red) |
| Museum | Attendance (last year) `DE` | `[Museum] PopServed` × population / (P_mus + 1) if population < P_mus, else `[Museum] PopServed`; P_mus = Σ museum capacity |
| | Dinosaur Bones `E0` | (a city counter) mod 53 − (third date value mod 3). It is shown only when that city object exists [INFERRED: the counter is not identified] |
| | Grade `E4` | s = funding × min(100, P_mus × 100 / population) / `[Museum] MonthlyUpkeep`; s ≥ 84 → A, ≥ 66 → B, ≥ 52 → C, ≥ 40 → D (red), else F (red) |
| | Mummies `E2` | never shown (the string is unused) |
| All funded | Funding Cost `86` | the building's monthly upkeep property, as money "§x/Month" |

**Safety** (per station):

| Building | Line | Value |
|---|---|---|
| Police | Crimes per Month `88` | S = Σ crime byte over the diamond \|dx\| + \|dy\| ≤ `[PoliceStation] OptimalCoverageRadius` around the lot centre (clipped to the map), n = tiles; value = S×10/n + S mod 13 (0 if n = 0) |
| | Performance Rating `8C` | R = avgCoverage × 100 / ((Max − Min)/3 + Min), where avgCoverage is the police-coverage layer averaged over tiles within the station's radius, and Min/Max are the coverage tunables (shipped 20/80, so ÷40). R < 50 → Poor (red); R < 80 → Fair; R > `OppressionThreshold` (210) and crimes ≠ 0 → Oppressive (red); else Good |
| | Arrests per Month `8A` | R × crimes / `OppressionThreshold` |
| | Donuts flavour `91` | d = max(\|dx\|,\|dy\|) + min(\|dx\|,\|dy\|)/2 from the station tile to a city reference point (unidentified city object); crimes ≤ 0 → d×10; crimes < 100 → d×(11 − 0.1×crimes); else d×100/crimes |
| Fire | Fires per Month `9C` | Σ flammability byte over the diamond of radius `[FireStation]` radius (25) / (2 × tiles) |
| | Calls per Month `9E` | fires × (1.5 − 0.3 × (t + 127)/255) |
| | Performance Rating `A0` | f = funding / `[FireStation] OptimalMonthlyUpkeep` (30); f < 0.4 → Poor (red); < 0.7 → Fair; < 1.0 → Good; else Excellent |
| | Spots on Dalmatian `A5` | LV × 2/3 at the station tile [INFERRED: the layer is land value] |

**Parks and recreation** (citywide unless noted):

| Building | Line | Value |
|---|---|---|
| Any park | Acres Systemwide `EA` | 1·N(14311 small park) + 9·N(14312 big park) + 16·N(14313 baseball) + 4·N(14314 playground) + 4·N(14315 pond) + 1·N(14316 fountain). The section is omitted when this is 0 |
| | Benches in Use `EC` | let S = round(R-valve supply × working-age share) and J = jobs-valve value; S = 0 → 0; J < S → min(5, (S − J)/acres) × the building's footprint w × h; else 1. Halved when the homeless-shelter ordinance `0x20D07175` is on [INFERRED for J's meaning] |
| Zoo | Attendance `EE` | population × (t + a + 255)/512 × 0.1 / N(14450) |
| | Manure Donated `F0` | (t + 128)/4 × (1 + 0.5 if ordinance `0xA2F917CF` is on) × (1 + 0.25·N(12851 recycling)) |
| Baseball | Little Leaguers `F2` | population × (share of ages 6–10) × (t + 128)/255 × 0.25 / N(14313), capped at 210 |
| | Record `110` | wins = (LV + 128 + a)/20; shown "wins − (25 − wins)" |
| | Season Tickets `112` | wins × 2000 |
| | Team name `1AD` | a per-building stored string, shown in an editable field |
| Marina | Boats Berthed `F4` | population × (t + 128)/255 × (1 + 0.5·N(13871 university)) × (1 + 0.25·N(13873 local college)) × 0.01 / N(14430), capped at 400 |

**Transport hubs**:

| Building | Line | Value |
|---|---|---|
| Airport | Air Traffic `FE` | r = C-valve supply / (N(12310) × 4000); r < 0.8 Light, < 1.1 Medium, < 1.5 Heavy, else Congested (red) |
| | On-Time `103` | min(98, (a + 128)/255 × (1/r) × 100); r = 0 also gives 98 |
| Seaport | Port Traffic `105` | r = I-valve supply / (N(12330) × 4000); same bands as the airport |
| | Barnacles Removed `10A` | (1 − (a + 128)/255) × r × 1 000 000 |
| Bus stop, rail or subway station | Passengers per Day `37` | 4.3 × the traffic simulator's station value at the queried tile (`18-sim-transport.md`); 0 while the transit strike state is active |

**Landmarks and rewards**:
* Date Built `F6`: the occupant's build date (day/month/year), formatted by the locale.
* Monthly Income `118`: business-deal buildings only, the business-deal manager's income for that
  building's type (`19-sim-economy.md`); otherwise 0.
* Flavour numbers:
  * Number of Eggs Thrown `F8` = (255 − a)/20;
  * Number of Pigeons `FC` = N(13000);
  * Miles of Red Tape `FA` = a count from the ordinance manager [INFERRED: number of active
    ordinances].

Not traced: the transit service-quality lines `181`–`185`.

### 6.13 Snapshots (Unlimited edition) [STATIC]

* *Take snapshot* (Review ▸ Take snapshot ▸ small/medium/large; message `2435FF70` with param 1/2/3):
  arms a framing rectangle (cursor `snapshot cursor.cur`) with hints `041F2625:D3` (click or
  Return to take), `D4` (Escape cancels), `D5` (space bar cycles the size). The image is saved as a
  JPEG with a companion info file (city `199`, mayor `19A`, date `19B`, population `19C`, funds
  `19D`, description; default `D2`). An optional loose `SnapShot.ini` section `[SnapShotSpecs]`
  with keys `Width`, `Height`, `BackgroundColor_R`/`_G`/`_B` overrides the custom size and
  background colour.
* *View snapshots* (param 0): album browser, title `041F2625:C8`, albums `198`, delete `19E`
  (failure message `CA`).

### 6.14 Tip of the Day [STATIC]

Shown at start-up when "show tips at startup" is on. Title `03C09B1B:3B`; check box `3D`; "next
tip" `3C`. Tips are strings `03C09B1B:1` … `03C09B1B:39` (57 IDs) **excluding** `0x10`, which is
never shown. Next = current + 1, skipping `0x10`, wrapping from `0x39` to `1`; previous = current − 1,
skipping `0x10`, wrapping from `1` to `0x39`. [STATIC] Initial tip: [OPEN] (random or persisted).

### 6.15 Credits [STATIC]

Played from the front end. Background `MAIN.IXF` image `82B9B75C:22729940` (357×119 frame);
strings `830CDF29:*` (378 entries) scroll upward in order of string ID. The frame is
horizontally centred; on screens wider than 600 px it is offset by (width − 600)/2. Scroll rate:
[OPEN].

### 6.16 Message boxes and confirmations

Generic modal boxes use `SYS.IXF` art and the button captions `209E6378:C0` Yes, `C2` No, `B4` OK,
`B6` Cancel, `114` Abort, `118` Ignore, `116` Retry. Exit confirmation: `225872FE:55` with
`209E6378:127` (save?) and closing text `A3`. The SimCity-Exchange/web prompts are `041F2625:1F4`–`1FC`.

### 6.17 Features not present

There is no sign/label placement tool (no command, menu entry or string). `signpost cursor.cur`
is loaded with the other city-view cursors (`08-ui-framework.md` §8.2) but no tool ever shows it.
SimCity 3000 Unlimited has no "My Sim" and no "U-Drive" feature (they belong to later games). An
implementation must not add them to saved data. [STATIC] The game also contains an IRC-style
chat client and network file-transfer dialogs; they are an optional online feature, out of scope
(`27-platform.md`).

## 7. Data-map overlays and the map window

### 7.1 Map / data-view window [STATIC]

`[OBSERVED]` There is **one** View Data window for data maps, charts and graphs. It is opened by
the control panel's Adjust & Review ▸ View Data button, or by Ctrl+M. It is 424 × 440 px at
(40, 34) of the client area (§6.0).

* **Mode strip.** A vertical strip of 12 icon buttons runs down the left edge and selects the
  mode; the title changes with it.
  * Ten data maps, top to bottom: Aura, Crime, Density, Power, Flammability, Land Value,
    Pollution, Traffic and Transportation, Water Supply, Zones (titles `111`–`11C`, e.g. "Aura
    Data Map").
  * After a gap: Charts (`183`) and Graphs (`184`).
* **Data map mode.**
  * The whole city is drawn as a large diamond filling the right part of the window, with a
    compass rose at the top left.
  * At the bottom left is a "Use As Map" (`2D4`) toggle button.
  * At the bottom right is a legend: a vertical gradient between the "Good" (`127`) and "Bad"
    (`129`) labels.
* **Charts mode.** The chart sits at the top. Below it are the two caption lines. Below those,
  centred, is a column of five radio buttons: Education, Health, Population, Electricity,
  Garbage. There are no tab buttons.
* **Graphs mode.** See §7.3.

The View City Layers dialog of §7.2, with its check boxes, is a different, smaller window. The
neighbours map is not part of the View Data window.

Opened by Review ▸ Map view (command `411164F1`, toggle) and by the bottom-bar "layer views"
button (`225872FE:A7`). Title `029541F4:269` ("view city layers"). It shows a mini-map of the whole
city (frame image `000005E5:2F`/`30`/`32`/`34`, 356×189) with a column of data-map buttons
(104×26 images in group `000005E5`). Selecting a data map:

1. recolours the mini-map with that layer;
2. if "show in city view" (`029541F4:186`) / "use as map" (`2D4`) is checked, the same layer is
   painted over the 3-D city view as a tinted overlay on each tile;
3. displays the legend: a gradient/colour-key image from group `000005E5` plus two end labels
   (placed at legend slots 0 and 3 of 4) or, for categorical maps, one label per category.

Clicking a point on the mini-map centres the main view there (`25-rendering.md`).

### 7.2 Data maps [STATIC]

`[OBSERVED]` In the main view, data layers are chosen in the **View City Layers** dialog, opened
with the layers button on the navigator panel. At 800 × 600 its frame is 296 × 336 px with the top-left at (32, 32) of the client area. The dialog has:
* an *Above Ground* group of six checkboxes: Transportation, Power Lines, Flora, Zoned Buildings,
  Other Buildings, Zones;
* an *Underground* pair of radio buttons: Subways, Water Pipes;
* a *City Layer Views* toggle with eight radio buttons: Aura, Crime, Density, Electric Power,
  Flammability, Land Value, Pollution, Traffic. These are greyed out until the toggle is pressed;
* Apply, Default View and OK buttons.

The selected layer recolours the main view (`25-rendering.md` §9.2) and stays active until it is
changed or Default View is pressed. The legend images listed below belong to the separate
map-window data views.

| Map | Title | Legend image (`000005E5:`) | Legend labels (`029541F4:`) | Extra controls |
|-----|-------|---------------------------|-----------------------------|----------------|
| Aura (mayor rating) | `111` | `0x01` | `127` good … `129` bad | — |
| Zones | `11C` | `0x19` | categorical: `139` R, `13A` C, `13B` I, `13C` government, `2D5` seaports, `2D6` airports, `2D7` landfill | — |
| Crime | `112` | `0x04` | `23E` low … `124` high | toggle `2A1` police-station coverage |
| Density | `115` | `0x10` | `242` high … `243` low | — |
| Power | `113` | `0x11` | categorical: `12A` powered, `23F` unpowered | — |
| Flammability (fire) | `114` | `0x07` | `240` low … `241` high | toggle `2A5` fire-station coverage |
| Land value | `116` | `0x0C` | `244` high … `245` low | — |
| Pollution | `118` | `0x0F` | `246` clean … `247` polluted | selector "show" (`2D8`): air `2A2`, water `2A3`, garbage `2A4` |
| Traffic & transportation | `11A` | `0x15` | `248` light … `249` heavy | — |
| Water supply | `11B` | `0x17` | categorical: `136` watered, `24A` unwatered | — |
| Neighbours | `117` | — | compass `11D` N, `11E` W, `120` S, `11F` E | see §6.7 |

Each continuous map samples one simulation layer (land value, crime, pollution, traffic,
density, aura, fire risk; owners `15`, `17`, `18`) and maps the 0…max range of the layer linearly
onto the legend ramp; categorical maps assign one legend colour per category. The coverage
toggles overlay the police/fire coverage radius layers (`17-sim-services.md`).
[STATIC: layer → map assignment and legends; INFERRED: linear ramp]

**Colour ramps** [STATIC]. The data-map window has no colours of its own for the maps. Its fixed
colours are panel and caption colours: (214,218,255), (124,144,255), (121,125,181) and
(128,128,128) for the map-window frame and labels, and (255,0,255) as the transparent key. The
on-map colours come from the renderer's data palettes `data1Pal`/`data2Pal`/`data3Pal` (64×32
each, loaded from the user's installation). The renderer picks one palette row per tile and
shades it by relief; see `25-rendering.md` §9.2. The legend image of each map (table above) shows
the same ramp and is the reference for which palette and which row direction a map uses. The
layer-value → row quantisation is [OPEN] (`25-rendering.md` §9.2 suggests row = ⌊value/8⌋).

### 7.3 Graphs window [STATIC]

`[OBSERVED]` This is a mode of the View Data window of §7.1, not a separate window: 424 × 440 px at (40, 34), with a close box. Esc closes it. Layout: the plot area is at the top right of the icon strip. Each checked series is drawn as one line, with its name and current value at the line's right end. Below the plot is a 3-column grid of check boxes (Approval Rating, Residents, Land Value, City Size, Unemployment / Pollution, Education, Traffic, Commerce, Nat'l Pop, Water % / Crime, Health, Funds, Industry, Power %). Below that, at the bottom right, are the 1 Year / 10 Years / 100 Years radio buttons. The defaults checked are City Size, Funds and Power %. There is no separate legend column and no push buttons for the range.

Title `029541F4:184` (also `160`). Series (check-box list, any combination shown at once):
city size `161`, residents `162`, commerce `163`, industry `164`, national population `165`,
traffic `166`, pollution `167`, unemployment `168`, crime `169`, power % `16B`, water % `16C`,
health `16D`, education `16E`, land value `16F`. Time-range buttons: 1 year `170`, 10 years `171`,
100 years `172`.

Every series keeps three ring histories: the last **12 monthly** samples (1-year view), the
last **10 yearly** samples (10-year view) and the last **10 per-decade** samples (100-year view).
[STATIC: the snapshot/export routine writes each series as one line of 12 values followed by
lines of 10 values for years and decades.] Each series is drawn as a polyline auto-scaled
between its own minimum and maximum in the shown range; labels `187`/`188` mark the high/low
values. [INFERRED scaling] The history arrays are part of the saved city (`10-save-format.md`).

### 7.4 Charts window [STATIC]

Title `029541F4:183`. Tabs: education `1DD`, health `1E0`, population `1E1`, electricity `1DE`,
garbage `1DF` (plus workforce `1E5`). Pages:

* Education — bar chart of education quotient by resident age (`1E6` axis, `1E4` EQ axis);
  captions `1E2` (population EQ) and `1E3` (workforce EQ) with the values.
* Health — life expectancy by age (`1E6`, `1E0`); captions `1F9` (population life expectancy,
  years) and `1FA` (workforce life expectancy).
* Population — population by age (`1E6`, `1E1`); captions `1FC` (total population) and `1FD`
  (workforce as % of population).
* Electricity — annual average electricity production by plant type: coal `1E8`, oil `1E9`, gas
  `1EA`, wind `1EB`, solar `1EC`, nuclear `1ED`, microwave `1EE`, fusion `1EF`, waste-to-energy
  `1F0`, neighbour deals `1F1`; caption `28A`, total `289` (MW-h per year).
* Garbage — share of garbage by destination (`1F3`): landfill `1F4`, neighbour deals `1F1`,
  incinerators `1F5`, recycling `1F6`, collecting in streets `1F7`; total `1F8` (tons per year).

The underlying statistics are owned by `20-sim-population.md` and `16-sim-utilities.md`.

## 7a. Observed reference (build 2.0.949, black-box)

Everything in this section is `[OBSERVED]` by running the original at 1024 × 768 unless stated.

### 7a.1 Tool-column menus and tooltips

| Root button | Second-level items (tooltip text, top to bottom) |
|---|---|
| Landscape | (not explored) |
| Zoning | Residential, Commercial, Industrial, Landfill, Seaport, Airport, De-zone, Demolish. A plain click activates the item's current density directly; there is no visible fly-out on a click |
| Transportation | Road, Highway, Bus Stop, Rail, Subway, Subway to Rail Connection, Demolish |
| Utilities | power lines, power plants (opens the power-plant picker), water pipes, water buildings (picker), garbage (picker), demolish |
| Civic | Police, Fire Station, Health & Education (fly-out: Hospital, School, College, Library, Museum), Parks & Rec, Landmarks, Rewards & Opportunities, Demolish |
| Meet advisors | (not explored) |
| View / city info | View Snapshots, Take Snapshots, Budget, Neighbors, Ordinances, Scenario Status, View Data |
| Options | Save City, Save As, Load, New City, Exit |

Other behaviour:
* Items whose button carries a small grid badge open a picker dialog or a fly-out (`08` §7).
* A second-level column stays open after a dialog launched from it has closed. Clicking its root
  button again closes it.
* Tooltips appear on hover after a short delay, as small pale-yellow boxes to the left of the
  item.

### 7a.2 Building pickers and year gating

The picker is titled "Select a Power Plant", "Select A Water Building", and so on. It shows:
* a building picture;
* a "Buildings Available" list;
* below it the name, Cost and Capacity/Power, and a short description;
* an OK button.

Selecting an entry updates the lower half. Lists are gated by the city's year:

| Picker | 1900 | 2000 |
|---|---|---|
| Power plants | Coal, Oil | Coal, Oil, Gas, Nuclear, Wind Mill, Solar Power Collector |
| Water buildings | Pumping Station, Water Tower | Pumping Station, Water Tower, Water Treatment Plant, Desalinization Plant |

The Bus Stop tool shows no preview and places nothing in 1900, but works in 2000 (§150 each), so
transit tools are also year-gated. A bus stop need not touch a road (one was placed two tiles
away). Observed prices: Coal §5,000 (6000 MW-h), Wind Mill §250 (200 MW-h), Pumping Station §300
(1500 m³/month), police/fire/school/hospital §500, library §1,000, college §3,000, bus stop §150,
residential zoning §10 per tile.

### 7a.3 Placement feedback

* A valid footprint is tinted translucent cyan; an invalid one (over water, on a road, on an
  existing building) is tinted red.
* The cost label follows the cursor.
* Purchases are charged on release.
* Details are in `25-rendering.md` §9.4.

### 7a.4 Query panels

The query tool is the green "?" in the bottom bar. It stays active after a panel closes, so each
further map click opens another query. The panel is modal: map clicks are ignored until it is
closed with OK or the close box.

| Target | Fields (top to bottom) | Extra controls |
|---|---|---|
| Zoned building (R, C or I) | picture, name, Elevation, Land Value, Zone Type (e.g. "Medium Residential", "Dense Commercial", "Light Commercial", "Medium Industrial"), Powered, Watered, Crime, Flammability, Air Pollution, Water Pollution | **Make Historical** check box; **Replace** unless abandoned. Narrow, tall pictures go on the left (layout type 3, §6.12.1) |
| Flora | name, picture, Elevation, Land Value, Zone Type, Powered, Watered, Flammability, Air Pollution, Water Pollution | — |
| Power plant | picture, name, Age (Max) "N yrs. (M yrs.)", Elevation, Pollution Generated (red when "Hazardous"), Maximum Power Capacity, Current Power Capacity, Usage (MW-h/Month) | **Replace** |
| Landfill | Elevation, Capacity Citywide (tons), Usage (% full) | — |
| Road | Elevation, Traffic (word band) | — (smaller panel) |

The **Make Historical** box is probably what sets one of the two per-occupant booleans in the
occupant record (`10-save-format.md` §6.2.4) [INFERRED].

### 7a.5 Prompts and file dialogs

* **Prompts.** Loading while in a city asks "Would you like to save this city?" (Yes/No). Choosing
  New City from the Options menu asks "Do you want to save your city?". Both are message boxes with
  a warning icon.
* **Save As / Load Saved City.** Both have:
  * a minimap preview of the selected file;
  * Population, Funds, Date, Mayor Name and City Name;
  * a Look In folder drop-down with Scenarios, StarterTowns and Terrains sub-folders;
  * a file list and a File Name field.

  Load adds a **Show City Stats** check box; Save As has editable Mayor Name and City Name fields.
  The default save name is the city name.
* **New City from inside a city.** It opens the same New City Options dialog, about 47 px left and
  32 px higher than the main-menu version. It remembers the last difficulty, year, size and scheme
  choices, but resets the name to "New City".

## 8. Persisted state touched by the UI

The UI does not own any save record of its own; it reads and writes state owned by other
documents. Items that must round-trip through a save for the UI to look identical after loading:

| State | Where specified |
|-------|-----------------|
| City name, mayor name, difficulty, start year, disasters-enabled, auto-budget | city header, `10-save-format.md` |
| City scheme selection (landscape, flora set, building set) and user-building replacements | `10-save-format.md`, `13-buildings-occupants.md` |
| Graph histories: per series 12 monthly + 10 yearly + 10 per-decade samples (§7.3) | `10-save-format.md` |
| Simulation speed / pause state, camera position, zoom, rotation | `14-sim-clock.md`, `25-rendering.md` |
| Enacted ordinances, deals, loans | `19-sim-economy.md` |

Preferences (§6.8), "show tips at startup" and the TOD position are program settings, not city
data (`27-platform.md`).

## 9. Message and command identifiers (cross-reference)

| ID | Kind | Meaning |
|----|------|---------|
| `724A82D0` | message | open advisor (Meet) window |
| `724A82D7` | message | open ordinances window |
| `2435FF70` | message | snapshots: param 0 = view albums, 1/2/3 = take small/medium/large |
| `C30177CA` | message | go to disaster |
| `610438A4` | command | load (param = file kind, §3.3) |
| `410438DD` | command | save / save as (`C3172AEA`) |
| `E3270FE9` | command | new city |
| `A10438E9` | command | exit program |
| `E2ED3560` | command | building picker (param = category, §6.11) |
| `411164F1`, `630288E9` | command | map/data-view window; neighbours window |
| `025DDC4A` | command | budget window (menu item `0x10008001`) |
| `C3001401`, `855A2DC4`, `844587AE` | command | preferences; city properties; scenario goals |

## Open questions

* Meaning of `SC3MenuDescs` field 4 (`0x000018FF`) and of the non-image fields of the
  `SC3CityScheme.ini` scheme sections.
* Exact geometry of each window (fixed in code; see `08-ui-framework.md`).
* Colour/placement of the cursor cost read-out; layer-value → palette-row quantisation of the
  data maps (the ramps themselves are the renderer's `data1–3Pal` palettes, §7.2).
* Split point between "very high" and "extreme" flammability; usage-band ratios for utility
  query lines; meaning of the pollution baseline *b*.
* Repeat rate of raise/lower terrain while the button is held.
* Which UI reaches the three rotation-variant placement commands (§5.2); which event uses the
  start-disaster type that is not in `SC3DisasterLayer.ini`.
* Which clock events drive the bottom bar's "date changed" vs "date set" updates (§4.1).
* Whether the older copyright string `03C09AFF:14` is shown anywhere (§3.1).
* Initial tip selection for Tip of the Day; credits scroll rate.
* The image-only main-menu button (#7).
* Units of the two machine-capability figures used for the default map size.
* Upper limit of the funding adjust control in the budget window (tax range resolved: 0–22).
