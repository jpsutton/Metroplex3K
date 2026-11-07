# 01 — Engine architecture, start-up, state machine, install layout

Status: draft. Provenance tags per `00-README.md`.

## 1. Behaviour areas

The list below names the behaviour areas of the original program and the documents that specify
them [STATIC]. An implementation may structure itself freely.

* Application: process start, single-instance check, command line, preferences, language
  selection, window creation, main loop, intro movie (`07-video.md`), main menu, credits, crash
  log, shutdown.
* Resources and data: IXF containers, resource types, `%Language%` path expansion
  (`02-containers.md`, `03-resource-types.md`); INI and PAK reading; string tables and locale
  (`05-text-and-fonts.md`).
* Presentation: graphics and palettes (`04-graphics-formats.md`, `25-rendering.md`); windows,
  controls, input (`08-ui-framework.md`); sound and music (`06-audio.md`).
* Optional on-line features: HTTP/FTP access with a cookie and file cache. Developer-only debug
  display.
* Numeric tunables (`SimTune.INI` `[Tunables]`, `SC3Tune.ini`; see §2).
* Content discovery: building sets, flora sets, landscapes, plug-in directory scanning, BAT `.DAT`
  discovery, lists of cities, scenarios, terrains and starter towns.
* Simulation: clock and update order (`14-sim-clock.md`); terrain (`11-world-terrain.md`);
  networks (`12-networks.md`); zoning, demand, development (`15-sim-growth.md`); economy
  (`19-sim-economy.md`); utilities and services (`16-sim-utilities.md`, `17-sim-services.md`);
  pollution, land value, crime, aura (`15-sim-growth.md`, `17-sim-services.md`); disasters
  (`21-disasters.md`); advisors and petitioners (`22-advisors-news-petitioners.md`); mass transit
  (`18-sim-transport.md`); vehicles, pedestrians, boats, aircraft (`26-automata.md`).
* City view and UI: sprite choice and placement (`25-rendering.md`); tools, panels, dialogs
  (`09-ui-screens.md`).
* Scenarios (`23-scenarios.md`); BAT import (`24-bat-buildings.md`); add-on content from `PlugIn/`
  (minimal; `02-containers.md` §7.1).

## 2. Configuration tables read at start-up

* `Sys/Default.ini` holds the start-up tables (full order in `02-containers.md` §7.1):
  * `[DBSegments]` maps a segment ID to a resource file, as `ID=0x80C381D7;path`. The value
    0x80C381D7 selects the IXF segment type. Paths may contain `%Language%`. See `02-containers.md`
    §7.2.
  * `[ResourceFactories]` maps a resource type to the class ID that decodes it
    (`03-resource-types.md` §4).
  * `[GameCommandFactories]` lists class IDs of game-command providers. The shipped `Default.ini`
    has no `[AppServices]` section. `SYS.PAK` also contains a `Test.ini` with `[AppServices]` and
    `[GameCommandFactories]` sections; the game never reads that file. An implementation may
    ignore both sections.
  * `[OccAttribCache]`, `[SPLASHBMP]`, `[WINSC3BMP]` and `[MAINMENUBMP]` configure the occupant
    attribute cache and the per-language splash, window and main-menu bitmaps.
  [DATA][STATIC]
* `CitySim.ini [AgentTypes]` defines the agent (occupant) type IDs and their behaviour classes, as
  `typeID=name:0x2026960B,0x41F56C4C,classID`. `[AgentTypeHierarchy]` gives each type's parent type
  [DATA]. See `13-buildings-occupants.md`.
* Additional INIs used during start-up: `Sys/MenuItem.ini` (menus), `Sys/Sprite.ini`,
  `Sys/CitySim.ini`, `Sys/Occupant.ini` and `Sys/Default.ini`.
* **Source of every INI file.** Each INI name is looked up first in `Sys/SYS.PAK`; the loose file
  `Sys/<name>` is read only when the archive is missing or does not contain that name. The full
  rule and the INI grammar are in `02-containers.md` §5.2–§5.3 [STATIC].
* **Tunables files.** Two distinct tunables files exist, both shipped inside `SYS.PAK` (and
  readable loose under the rule above):
  * `Sys/SimTune.INI`, section `[Tunables]`: a table of `0xID=value` numeric tunables. Every
    shipped ID is consumed by the disaster subsystem (`21-disasters.md`) [STATIC][DATA].
  * `Sys/SC3Tune.ini`: named sections of simulation tunables (zone developers, services, economy,
    transit and others), owned by `13-`, `15-`, `16-`, `17-`, `18-` and `19-` [DATA].
* Implementations must honour the INI tables as data: segments, factories, agent types and type
  hierarchy.

## 3. Notifications

The clock-driven notifications that matter for ordering are specified in `14-sim-clock.md` §3.2.
Notifications are delivered in posting order, and immediate ones to listeners in registration
order [STATIC]. Their numeric kinds appear in no data file, so this specification names them by
meaning only:

| Notification | Payload |
|---|---|
| Simulation scheduler started | none |
| New day | day of year (1-based; day 1 is 1 January), year |
| New week | day of year (1-based) |
| New month | day of year (1-based), year |
| New year | day of year (1-based, therefore 1) |
| Simulation paused / resumed, pause and resume requests | pause level [INFERRED] |
| Speed changed, speed-change request | selected speed [INFERRED] |

Each other document describes the behaviour that the notifications it owns trigger. The
simulation itself is driven by the tiered scheduler and clock described in `14-sim-clock.md`.

## 4. Start-up sequence

The steps run in this order [STATIC unless marked]:

1. Set the process error mode so that the system shows no error dialogs.
2. Determine the game root and the **data directory**. The game root is the directory that
   contains `Apps/` (the executable is `Apps/SC3U.exe`). The data directory is the directory that
   holds `Res/` and `Sys/`, i.e. `Apps/`, unless an INI provides `[Directories] Data=`, in which
   case that value is used. Resource paths (`[DBSegments]`, `%DataDirectory%`, the intro movie,
   `Sys/` INI files) are resolved relative to the data directory (`02-containers.md` §7.2,
   `07-video.md` §1) [STATIC; that the default is `Apps/` is INFERRED, high confidence, because
   every such path exists only under `Apps/`].
3. **Single instance.** Create the named mutex `SimCity 3000 Mutex`. If it already exists and the
   command line has no `-restart:` switch, find the top-level window with title
   `SimCity 3000`, restore it, bring it to the foreground, and exit.
4. **Updater hand-off (optional).** If `Updater/UpdateSettings.ini` exists (or the `-UpdaterApp:`
   switch names one), read `[Settings] UpdateMutexName`. Wait for that mutex up to three times at
   1-second intervals; if it never becomes available, exit. An implementation can ignore this step.
5. Choose the default display mode: 640×480 if the machine class is below 4, 800×600 otherwise,
   in 16-bit colour. The machine class is defined in `27-platform.md` §3; an implementation on
   modern hardware may simply default to 800×600. Then apply any saved preference or command-line override (see
   `27-platform.md` §3).
6. Load preferences from `SC3.cfg` (`27-platform.md` §4).
7. Determine language and country (`27-platform.md` §1). The installed languages are those for which
   `Apps/Res/Text/<language>/SC3StringsApp.IXF` exists.
8. Load `SYS.PAK` and the start-up INIs, then register the resource factories
   (`[ResourceFactories]`) **before** mounting the DB segments (`[DBSegments]`), in the order of
   `02-containers.md` §7.1.
9. Discover content: `*.SII` and `*.DAT` directly in `Apps/Res/Sprites/` (not its sub-directories)
   and under `Apps/PlugIn/`, landscapes, and the `*.SNR`, `*.SCT` and `*.ST3` lists. Building- and
   flora-set archives are mounted later, on demand (`02-containers.md` §7.1). See `24-bat-buildings.md` and
   `23-scenarios.md`.
10. Create the main window (title `SimCity 3000`) and show the per-language splash
    bitmap (`Default.ini [SPLASHBMP]`).
11. Initialise sound unless `-sound:off`. If `-music:off`, disable music.
12. If the command line names a file to open (for example when the user double-clicks a saved
    city), skip the intro and open that file, entering the In-city state directly (`07-video.md`
    §1). Otherwise play the intro movie `Apps/Res/UI/Shared/Movies/Intro.tgq` unless
    `-intro:off` is given or the movie cannot be played (`07-video.md` §1).
13. Show the main menu (not when a file was opened in step 12).

## 5. Game-state machine

| State | Entered from | Leaves to |
|---|---|---|
| Start-up (§4) | process start | Intro, or In-city when the command line names a file (§4 step 12) |
| Intro movie | start-up | Main menu (when the movie ends or is skipped) |
| Main menu | intro, or leaving a city | New City setup, Load City, Load Scenario, Starter Town, Terrain Editor, Credits, Exit |
| New City setup (name, difficulty, start year, terrain) | main menu | Terrain Editor or In-city |
| Terrain Editor | main menu or new city | In-city (when the terrain is accepted), or main menu |
| Load City / Starter Town / Scenario selection | main menu | In-city (scenarios also start the scenario script, `23-scenarios.md`) |
| In-city | the states above, or start-up | main menu (quit to menu), Exit |
| Exit | main menu or in-city | process end |

[INFERRED] This structure comes from the shipped UI resources and the content lists. The screens
and transitions themselves are specified in `09-ui-screens.md`.

## 6. Shutdown

1. Stop the scheduler and the clock.
2. Release the city subsystems in reverse creation order (`14-sim-clock.md` §5).
3. Save preferences to `SC3.cfg`.
4. Release the mutex, then exit.

On a fatal error the original writes a crash report (`_stkdmp.txt`) and exits immediately,
without the steps above. A reimplementation need not reproduce this.

[INFERRED] The ordering of steps 1–4 is inferred; the crash report is [STATIC].

## 7. Install layout and directories

Paths in this table are relative to the game root, as everywhere in this specification
(`00-README.md`). Some documents state that a section's paths are relative to `Apps/` (the data
directory, §4 step 2); they say so explicitly. The original accepts `\` and `/` as separators and
compares file names case-insensitively.

| Path (relative to game root) | Use |
|---|---|
| `Apps/` | Working directory; executable and libraries; `Apps/Sys/SYS.PAK`; `Apps/Res/...` resources [DATA] |
| `Apps/Sys/SYS.PAK` | Packed INI files (`02-containers.md`) [DATA] |
| `Apps/Res/` | IXF resource files, sprites, sounds, UI, text (`Apps/Res/Text/<Language>/...`) [DATA] |
| `Apps/BARender/`, `Apps/BACustom/` | Building Architect data [DATA] |
| `Cities/` | User city saves (`*.sc3`) [DATA] |
| `Cities/Scenarios/` | `*.SNR` [DATA] |
| `Cities/StarterTowns/` | `*.ST3` [DATA] |
| `Cities/Terrains/` | `*.SCT` [DATA] |
| `Buildings/` | `*.bld` BAT buildings [DATA] |
| `Scripts/<LANG>/` | `*.SSC` scenario scripts [DATA] |
| `Apps/SimUtil2.dat` | 100,000 random bytes, not read by the game; ignore [DATA][STATIC] |

## Open questions

* The exact main-menu and new-city transitions (owned by `09-ui-screens.md`). In particular: on the
  direct New City setup → In-city path, which terrain is used (a generated map or a selected
  `.sct` terrain) is specified in `09-ui-screens.md` and `11-world-terrain.md`, not here. [OPEN]
