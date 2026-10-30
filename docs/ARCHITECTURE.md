# Architecture

## Layers

- `src/core` is platform independent. It uses only the C++17 standard library. No SDL, no OS APIs except
  `std::filesystem` and `std::system` (installer extraction, guarded for Android). Everything the game does
  lives here, including the UI (`Game`) and the framebuffer it draws into.
- `src/app` is the desktop host (SDL2). It pumps events into an `Input` struct, calls `Game::frame(Input, w, h, dtMs)`,
  uploads `Game::screen()` to a texture and feeds `Game::audio()->mixAudio()` to the audio device.
  A new platform (Android, web) only needs a new host.
- `tools/` are developer programs (conformance, headless demos that write PNGs).
- `tests/` are plain executables using `tests/testutil.h` (`CHECK`, `TEST_MAIN_END`). No test framework.

## Host contract

`Input` (`ui.h`): mouse position and buttons, wheel, typed text, key edges, held arrows. The host holds one lock
around `Game::frame` and the audio callback. `Game::wantQuit()` ends the loop. File drops go to `Game::dropPath`.

## Data flow

Game data (the user's install) -> `GameData` (mounts IXF segments from `Default.ini [DBSegments]`, sprite DATs, INIs
from `SYS.PAK`) -> consumers: `Renderer` (sprites, palettes), `CityConfig` (INI tunables, zone and family data),
`Fonts`, string tables, `AudioSystem`.

City: `World` (grids, occupants, network pieces, raw preserved records) is owned by `Game`. `City` (`city.h`) owns every
simulation subsystem and runs them per the clock schedule of spec 14. `Sim` is a thin facade used by the UI.

Save: `loadCity` parses outer IXF records and the packed inner database; unknown records are kept in `World::raw`.
`City::flush` serialises live layers into `raw`; `saveCity` writes everything.

## Modules and specs

| Area | Files | Spec |
|---|---|---|
| Containers, RefPack, INI/PAK | ixf, refpack, ini, resdb, records | 02, 10 |
| Images, fonts, text | image, font, ui | 04, 05 |
| Terrain | terrain_gen, terraform, flora | 11 |
| Rendering | render, skin | 25, 08 |
| Clock, economy, population | clock, economy, population | 14, 19, 20 |
| Growth | growth, valves, landvalue (`m3k::growth`) | 15 |
| Utilities, services | utilities, services, simcommon (`m3k::us`) | 16, 17 |
| Networks, traffic | tiling_rules, tiler, network_tools, traffic | 12, 18 |
| Disasters, advisors, news | disasters, advisors, news, petitioners | 21, 22 |
| Scenarios, BAT, platform | scenario, script, bat, settings, cheats | 23, 24, 27 |
| Audio, video, automata | audio, xa_codec, video, tqi, intro, automata | 06, 07, 26 |
| UI | game*, windows* | 08, 09 |

## Namespaces

`m3k` (default), `m3k::us` (utilities and services helpers, separate to avoid clashes with the economy and clock
types), `m3k::growth`. Keep new subsystem types in their own namespace when a short name may collide.

## Adding a subsystem

1. Write a state class with explicit input structs (no pointers into other subsystems). Provide `parse`/`serialize`
   for its saved record if the spec defines one.
2. Add tests with synthetic fixtures.
3. Own it from `City`, register its schedule handler, load it from `World::raw` and write it in `City::flush`.
4. Surface it in the UI through `windows*` and `Game`.
