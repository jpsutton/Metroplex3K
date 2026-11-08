# Metroplex3K status

Clean-room engine compatible with the original game named in README.md. C++17, CMake, SDL2 (desktop host only). Spec in `spec/`.
Build: `mkdir -p build && cd build && cmake .. && make -j8 && ctest`. Run: `build/metroplex3k [--game-dir DIR]`.
Game data = the user's own install (GOG). Auto-detect (`gamedir.*`), drag-drop folder or GOG installer (needs
`innoextract`), typed path on the first screen, `$M3K_GAME_DIR`. See README.md and docs/.

NOTE: the engine was first written without a game install; it has since been run against a real GOG install. Parsers, round trips (47 of 60 saves byte-identical, the rest only differ in compression bytes), budget, pollution, crime, water and power results are checked against the shipped saves (see closed issues #5, #6, #11, #17, #74, #78). Treat the next sentence as history.

IMPORTANT (history): no game install was available while this was first written. Everything is built from the spec and tested on
synthetic fixtures (tests/fake_install.h builds a tiny install on disk). First run on real data will need fixes.
`build/conformance <game dir>` reports parser coverage over every shipped file type.

## What exists (all in src/core unless noted)
- Containers/data: ixf, refpack, ini (SYS.PAK), resdb, image, gamedata, gamedir, records (raw record preservation)
- Save: savefile (load), savewrite (save), world.h
- World: terrain_gen (spec 11.3, exact RNG), terraform (11.4), flora
- Render: render (spec 25), font (FBF + built-in fallback), skin (original UI art loader), ui (immediate mode)
- Shell: game*.cpp (menus, new city, terrain edit, load/save, HUD, tool menu from MenuItem.INI, tools, previews, hotkeys per
  spec 08 sec 5, cursors, drag/edge scroll, windows, ticker, scenarios, disasters, cheats, intro movie hook)
- Sim: clock, economy, population, growth/valves/landvalue (m3k::growth), utilities/services (m3k::us), tiling_rules/tiler/
  network_tools, traffic, disasters, advisors/news/petitioners, scenario/script, bat, settings/cheats; city.* owns all
- Media: audio/xa_codec, video/tqi/intro, automata (+ game_street.cpp host adapter)
- Host: src/app/main.cpp (SDL2). Tools: conformance, render_demo, game_demo, game_play_demo, city_sim_demo, windows_demo, skin_demo, frame_bench
- 37 test executables; `ctest` must stay green

## Open issues: GitHub is the single source of truth
Every known gap, guess, spec `[OPEN]` item and verification task is a GitHub issue:
https://github.com/jpsutton/Metroplex3K/issues

Labels: severity `S1-save-compat`, `S2-sim-fidelity`, `S3-presentation`, `S4-peripheral` (same scale as spec/29);
state `needs-real-data` (cannot close without a real install), `spec-open` (spec says [OPEN], we use a default),
`blocked-on-spec` (waiting for updated spec files), `spec-gap` (specified, not implemented), `approximation`
(deliberate guess), `bug`, `licensing`, `infra`; plus `area:*` per subsystem.
Release: 1.0 is out (tracking issue #106 and milestone `1.0` are closed); milestone `post-1.0` holds the gaps accepted for 1.x;
label `release-blocker` marks items required for a release.
Umbrella for spec updates: issue #105 (spec updates). When you resolve something, close
its issue with the spec section or commit that answers it. Do not keep gap lists in this file or in code comments only.
