# Metroplex3K status

Clean-room SimCity 3000 Unlimited compatible engine. C++17, CMake, SDL2 (host app only). Spec in `spec/`.
Build: `mkdir -p build && cd build && cmake .. && make -j8 && ctest`. Run: `build/metroplex3k [--game-dir DIR]`.
Game data: user's own install (GOG). Auto-detect (`gamedir.*`), drag-drop folder / GOG installer (needs `innoextract`),
typed path on first screen, `$M3K_GAME_DIR`.

IMPORTANT: no game install was available while this was written. Everything is built from the spec and tested on
synthetic fixtures only. First run on real data will need fixes. `build/conformance <game dir>` reports parser coverage.

## Layout (src/core is platform independent; src/app is the SDL host; Android port = new host)
- Containers/data: bytes, refpack, ixf, resdb, ini (SYS.PAK), image, gamedata, gamedir, records (raw record store)
- Save: savefile (load), savewrite (save), world.h, records.*
- World: terrain_gen (spec 11 sec 3), terraform (sec 4), flora
- Render: render.* (spec 25 full), font.* (FBF + builtin fallback), ui.* (immediate mode), game.* (screens/HUD/tool menu)
- Sim: clock, economy, population, growth/valves/landvalue (namespace growth), utilities/services (namespace us),
  tiling_rules/tiler/network_tools, traffic, disasters, advisors/news/petitioners, scenario/script, bat, settings/cheats
- city.* owns all subsystems; sim.* is the thin facade used by Game
- Media: audio/xa_codec, video (+tqi/intro when landed), automata

## Known gaps
- Windows (budget detail pages, ordinances, advisors, petitioners, query panels, map/graphs) exist as modules; UI
  hookup in progress (agent K produced src/core/windows.*)
- Original UI art (MainUI.ixf skins) not used; UI is drawn with the toolkit
- Growth: some layouts [OPEN] in spec (housing B/C, estates, row houses, seaport pier, runway) fall back
- Many [OPEN] items use spec/29-open-issues.md recommended defaults; configurable where possible
- Real-data verification pending (other agent closing open issues on an install)
- Intro movie: tqi.* (TQI decoder, unverified on real files; tables from MPEG-1 standard, consulted public ffmpeg docs - review licensing notes) + intro.* player
