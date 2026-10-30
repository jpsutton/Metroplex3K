# 01 — Engine architecture, start-up, state machine, install layout

Status: draft. Provenance tags per `00-README.md`.

## 1. Functional decomposition

The table below is a functional decomposition that an implementation can follow. It lists the
behaviour areas of the original program and the documents that specify them [STATIC]. It says
nothing about how the original program was divided into files; an implementation may structure
itself freely.

| Subsystem | Responsibility |
|---|---|
| Application shell | Process start, single-instance check, command line, preferences, language selection, window creation, main loop, intro movie, main menu, credits, crash log, shutdown |
| Framework services | Message/notification delivery, timers, string/date formatting, INI/PAK readers, locale tables |
| Resource database | IXF containers, DB segments, resource factories, `%Language%` path expansion; see `02-containers.md` |
| Graphics | DirectDraw surfaces, blitting, palettes; see `04-graphics-formats.md`, `25-rendering.md` |
| Windowing / UI toolkit | Top-level window class `Gonzo`, child windows/controls, message boxes, input; see `08-ui-framework.md` |
| Sound | Sound/music mixing, XA decode; see `06-audio.md` |
| Video | Intro movie; see `07-video.md` |
| Internet | HTTP/FTP client, cookie and file cache, used by the optional on-line features |
| Debug tools | On-screen debug display (developer only) |
| Tunables | Numeric tunables tables (`SimTune.INI` `[Tunables]`, `SC3Tune.ini`); see §2 |
| City simulator core | Owns the simulation clock, the agent/occupant manager, the layer list and the per-tick scheduler |
| City initialisation / content discovery | Building sets, flora sets, landscapes, plug-in directory scanning, BAT `.DAT` discovery, list of cities/scenarios/terrains/starter towns |
| Street-level simulation | Vehicles, pedestrians, boats, aircraft (automata); see `26-automata.md` |
| Terrain ("dirt") | Terrain grid, generation, editing; see `11-world-terrain.md` |
| Networks | Roads, rail, pipes, power lines; see `12-networks.md` |
| Zones / RCI | Zoning, demand, development; see `15-sim-growth.md` |
| Economy | Budget, ordinances, deals; see `19-sim-economy.md` |
| Services / utilities | Police, fire, health, education, power, water, garbage; see `16-sim-utilities.md`, `17-sim-services.md` |
| Misc layers | Pollution, land value, crime, aura; see `15-sim-growth.md`, `17-sim-services.md` |
| Disasters | Disaster manager and instances; see `21-disasters.md` |
| Advisors, petitioners | Advisor agents, petitioner manager; see `22-advisors-news-petitioners.md` |
| Transit | Mass transit; see `18-sim-transport.md` |
| City sprites | Choosing and placing the sprites of the city view; see `25-rendering.md` |
| In-city UI | Tools, panels, dialogs; see `09-ui-screens.md` |
| Scenarios | Scenario scripts and goals; see `23-scenarios.md` |
| BAT import | Building Architect buildings; see `24-bat-buildings.md` |
| Add-on hook | Loads add-on content from `PlugIn/` (minimal; `02-containers.md` §7.1) |

## 2. Configuration tables read at start-up

* `Sys/Default.ini` holds the start-up tables (full order in `02-containers.md` §7.1):
  * `[DBSegments]` maps a segment ID to a resource file, as `ID=0x80C381D7;path`. The value
    0x80C381D7 selects the IXF segment type. Paths may contain `%Language%`. See `02-containers.md`
    §7.2.
  * `[ResourceFactories]` maps a resource type to the class ID that decodes it
    (`03-resource-types.md` §4).
  * `[GameCommandFactories]` lists class IDs of game-command providers. The program also asks
    `Default.ini` for `[AppServices]`, but the shipped `Default.ini` has no such section, so no
    application services are listed. `SYS.PAK` also contains a `Test.ini` with `[AppServices]` and
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
* Implementations need not reproduce the original's internal component structure. They must,
  however, honour the INI tables as data: segments, factories, agent types and type hierarchy.

## 3. Notifications

Subsystems react to notifications: a notification has a kind and a small payload of up to three
32-bit words. A notification is either queued (delivered later, in posting order) or delivered
immediately to every listener of that kind, in the order the listeners registered [STATIC]. The
notification kinds are internal to the original program and appear in no data file, so this
specification names them by meaning only.

The clock-driven notifications that matter for ordering are specified in `14-sim-clock.md` §3.2:

| Notification | Payload |
|---|---|
| Simulation scheduler started | none |
| New day | day of year (1-based; day 1 is 1 January), year |
| New week | day of year (1-based) |
| New month | day of year (1-based), year |
| New year | day of year (1-based, therefore 1) |
| Simulation paused / resumed, pause and resume requests | pause level [INFERRED] |
| Speed changed, speed-change request | selected speed [INFERRED] |
| Post a news-ticker message | see `22-advisors-news-petitioners.md` |
| Cheat code entered | see `27-platform.md` §5 |

The city also listens for "all disasters ended" (`21-disasters.md`) and for several notifications
owned by other areas; each owning document describes the behaviour it triggers [STATIC].

The simulation itself is driven by the tiered scheduler and clock described in `14-sim-clock.md`.

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
   command line has no `-restart:` switch, find the top-level window with class `Gonzo` and title
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
10. Create the main window (class `Gonzo`, title `SimCity 3000`) and show the per-language splash
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

1. Stop the scheduler and the clock, which unregisters their clock listeners.
2. Release the city subsystems in reverse creation order (`14-sim-clock.md` §5).
3. Save preferences to `SC3.cfg`.
4. Release components and the mutex, then exit.

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
| `Apps/SimUtil2.dat` | 100,000 random bytes, referenced by no module; ignore [DATA][STATIC] |

## Open questions

* The exact main-menu and new-city transitions (owned by `09-ui-screens.md`). In particular: on the
  direct New City setup → In-city path, which terrain is used (a generated map or a selected
  `.sct` terrain) is specified in `09-ui-screens.md` and `11-world-terrain.md`, not here. [OPEN]
