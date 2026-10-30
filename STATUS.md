# Metroplex3K status

Clean-room SimCity 3000 Unlimited compatible engine. C++17, CMake, SDL2 (desktop host only). Spec in `spec/`.
Build: `mkdir -p build && cd build && cmake .. && make -j8 && ctest`. Run: `build/metroplex3k [--game-dir DIR]`.
Game data = the user's own install (GOG). Auto-detect (`gamedir.*`), drag-drop folder or GOG installer (needs
`innoextract`), typed path on the first screen, `$M3K_GAME_DIR`. See README.md and docs/.

IMPORTANT: no game install was available while this was written. Everything is built from the spec and tested on
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
- Host: src/app/main.cpp (SDL2). Tools: conformance, render_demo, game_demo, game_play_demo, city_sim_demo, windows_demo, skin_demo
- 37 test executables; `ctest` must stay green

## Known gaps / approximations
- Never run against real game data. Parsers follow the spec byte-for-byte but are only verified on synthetic input.
- Skin: bottom bar art, speed/view button art and main-menu art helpers exist (skin.h) but only the tool-menu column and
  RCI indicator are hooked into Game; the rest of the HUD uses the toolkit drawing.
- Advisor inputs (Game::fillAdvisorInputs) fill only the main fields; many advisor rules see zeros.
- Traffic data-map layer has no per-tile source; street sim needs real sprite data to show anything.
- Spec [OPEN] items use the defaults from spec/29-open-issues.md; several growth layouts (housing B/C, estates, row houses,
  seaport pier, runway sequence) fall back to the scatter rule.
- MPEG-1 tables in tqi.cpp: see docs/LICENSING.md. License not chosen.
- Android: design notes only (docs/ANDROID.md).
