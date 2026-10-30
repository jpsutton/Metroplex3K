# SimCity 3000 Unlimited — Compatible Engine Specification

This document set specifies the observable behaviour, data formats and simulation rules of
*SimCity 3000 Unlimited* (Maxis/EA, 2000; GOG release build "2.0.949") in enough detail for an
independent team to build a compatible engine that:

* loads the **original, user-owned game data** (installed game directory) unchanged;
* loads and saves cities, terrains, starter towns, scenarios and BAT buildings that are
  byte-compatible with the original program;
* reproduces the original simulation, user interface and presentation faithfully on modern systems.

No game assets are redistributed or recreated. The implementation reads everything from the
original installation at run time.

## Clean-room status

This specification was produced by a separate analysis ("dirty") team that examined the original
program. It is the **only** channel of information to the implementation ("clean") team.

The rules below are binding on every document in this directory:

1. **No code.** No source, decompiled or disassembled code from the original program appears
   here, and no pseudocode that transliterates it. Algorithms are given as mathematics, numbered
   prose steps, decision tables and state machines, written from an understanding of the behaviour.
2. **No implementation internals of the original.** No machine addresses, internal function or
   class names, in-memory object layouts or vtable descriptions. The only layouts given are
   on-disk/on-wire formats that a compatible program must read or write.
3. **Functional identifiers are allowed.** Magic numbers, chunk/type/group/instance IDs, class IDs
   that appear in data files, INI section/key names, file names and directory names are interface
   facts that an implementation needs in order to interoperate with the data. They are quoted
   exactly.
   * Numeric IDs that occur **only inside the original program's code**, and never in data files or
     saves, are not interface facts. This covers internal message IDs, interface IDs, window IDs
     and control class IDs. Documents describe the behaviour in words instead ("the month-end
     notification", "the list-box control"). The one exception is a numeric ID used purely as a
     stable cross-reference label between documents: it is allowed only when it is explicitly
     marked "label only; not needed by an implementation".
   * Documents do not describe how the original program was structured internally: which module
     calls which, which component requests which interface, or what a dispatcher has "no case"
     for. They describe what the program does.
   * Documents never refer to the analysis team's tools, scripts or working notes.
4. **No copied creative content.** Game text, images, sounds and music are not reproduced (beyond
   short identifiers and tiny illustrative byte samples of headers). The engine loads them from the
   user's installation.
5. **Provenance tags.** Every non-trivial claim carries one of these tags:
   * `[DATA]` — verified by parsing every relevant file shipped with the game.
   * `[STATIC]` — determined by static analysis of the original program's behaviour.
   * `[OBSERVED]` — seen by running the original program and looking only at its output
     (screenshots, files it wrote). No code was examined.
   * `[INFERRED]` — reasoned from indirect evidence. Confidence is stated.
   * `[OPEN]` — unknown or unverified. Implementers should make it configurable or test against
     the original.

## Document map

| File | Topic |
|------|-------|
| `00-README.md` | This file: scope, rules, conventions, document map |
| `01-architecture.md` | Engine architecture, component model, start-up, main loop, threading, configuration, install layout |
| `02-containers.md` | IXF resource container, sprite DAT archives, SII sidecars, PAK + packed INI, RefPack compression |
| `03-resource-types.md` | Registry of every resource type ID and its payload format |
| `04-graphics-formats.md` | Sprite/image encodings, palettes, BMP/TGA conventions, cursors, UI bitmaps |
| `05-text-and-fonts.md` | String tables, localisation, language selection, bitmap fonts (FBF) |
| `06-audio.md` | XA audio codec, WAV usage, AUDIO.INI semantics, music, ambient and event sounds |
| `07-video.md` | Intro movie (TGQ) playback behaviour |
| `08-ui-framework.md` | Window/control system, code-defined layouts (there are no layout resources), input, cursors, hotkeys |
| `09-ui-screens.md` | Every screen, dialog, tool and panel, with behaviour |
| `10-save-format.md` | City/terrain/starter-town/scenario file formats, all records, byte layouts |
| `11-world-terrain.md` | Map grid, coordinates, terrain, water, flora, terrain generation and editing |
| `12-networks.md` | Roads, highways, rail, subway, pipes, power lines, tiling rules, bridges, neighbour connections |
| `13-buildings-occupants.md` | Occupant (building) attributes, families, lots, landmarks, rewards, building sets |
| `14-sim-clock.md` | Simulation clock, calendar, speeds, update scheduling and ordering |
| `15-sim-growth.md` | Zoning, RCI demand, development/redevelopment, land value, abandonment |
| `16-sim-utilities.md` | Power, water, garbage |
| `17-sim-services.md` | Police, fire, health, education, crime, pollution, aura/mayor rating |
| `18-sim-transport.md` | Traffic model, mass transit, ports |
| `19-sim-economy.md` | Budget, taxes, funding, loans/bonds, ordinances, neighbour deals, business deals |
| `20-sim-population.md` | Population, demographics, EQ, health, life expectancy, workforce |
| `21-disasters.md` | Disasters, fires, riots, strikes, effects |
| `22-advisors-news-petitioners.md` | Advisors, news ticker, petitioners, rewards |
| `23-scenarios.md` | Scenario files, goal scripts, scenario templates |
| `24-bat-buildings.md` | Building Architect Tool data, `.bld` format, import of custom buildings |
| `25-rendering.md` | Isometric projection, zoom, rotation, draw order, animation, effects |
| `26-automata.md` | Vehicles, pedestrians, boats, aircraft, their attributes and paths |
| `27-platform.md` | Settings, registry/config keys, command-line, display modes, modernisation notes |
| `28-conformance.md` | How to verify an implementation against the original data; defects of the original that must be reproduced |
| `29-open-issues.md` | Ranked list of remaining open questions, with recommended interim behaviour |

## Status of this edition

The spec was produced from the GOG build 2.0.949 (English installer, seven language folders).

* **Data formats:** containers, images, strings, fonts, audio, saves and BAT files are complete to
  the byte. They were verified by parsing every shipped file; coverage figures are in
  `28-conformance.md` §2.
* **Save records:** every record of every shipped city-family file is parsed byte-exact, and most
  fields have known meanings (`10-save-format.md` §5.2).
* **Simulation and presentation:** the remaining unknowns are tagged `[OPEN]` in place. The ones
  that matter most are ranked in `29-open-issues.md`, each with a recommended interim behaviour.

## Conventions

* Integers are little-endian unless stated. `u8/u16/u32` are unsigned and `s8/s16/s32` are signed
  two's complement. `f32` is IEEE-754 single precision.
* Byte offsets are hexadecimal with a `0x` prefix. Sizes are decimal bytes unless suffixed.
* A resource key is the triple **(Type, Group, Instance)**, each a `u32`, written `T:G:I` in
  hexadecimal, e.g. `035F62A4:035F62A4:00000001`. Note that the IXF index stores the three
  fields on disk in the order Group, Instance, Type (see `02-containers.md`), whereas keys embedded
  inside record payloads are stored Type, Group, Instance.
* "Tile" means one cell of the city grid. "Cell" in a layer means one element of that layer's grid.
* **World coordinates.** A tile is addressed as **(x, z)**, both 0-based integers; **y** is always
  the vertical axis (altitude). x is the first coordinate: bits 0–10 of the occupant placement word,
  and the row index of every persisted grid. z is the second coordinate: bits 11–21 of the placement
  word, and the position within a row (`10-save-format.md` §6.2.3 and §6 conventions). Persisted
  grids store their rows from x = maximum down to x = 0. W is the extent along x and H the extent
  along z (shipped maps are square, W = H). Sub-tile positions use 256 units per tile on the same
  axes. A cell of a coarser layer (2×2, 4×4 tiles) is addressed (x ÷ k, z ÷ k). Documents do not use
  other letters, such as (a, b) or (x, y), for map positions; screen and image coordinates (x, y) in
  `04`, `08`, `09` and `25` are a different space and are labelled as screen coordinates there.
* Paths are given relative to the game root (the directory containing `Apps/`, `Cities/`, etc.).
  The executable's working directory is `Apps/`.
* Where the original game's behaviour has an obvious defect, the spec records the original
  behaviour (needed for faithful replay of saved cities) and notes the defect.
