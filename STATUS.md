# Metroplex3K status

Clean-room SimCity 3000 Unlimited compatible engine. C++17, CMake, SDL2 (app only). Spec in `spec/`.
Build: `mkdir -p build && cd build && cmake .. && make -j8 && ctest`.

No game install on the dev machine. All parsers are written from the spec and tested on synthetic
fixtures only. First run against real data is untested.

## Done (src/core, no SDL dependency)
- `bytes.h` LE reader/writer helpers
- `refpack.*` RefPack decode + canonical encoder (tests pass)
- `ixf.*` IXF container reader/writer; `resdb.*` mounted segment stack (later mount wins)
- `ini.*` SYS.PAK reader, INI quirks, `IniStore` (PAK first)
- `image.*` sprite (RLE/plane), UI image, BMP decoders
- `gamedata.*` mounts `Default.ini [DBSegments]` + sprite DATs, sprite cache, TKB1 occupant attrs, CSATTRIB BIN records
- `world.h`, `savefile.cpp` loader for .sc3/.st3/.sct/.SNR: city info, terrain, flora, zone, occupants, network pieces, view

## Also done
- `terrain_gen.*` exact spec generator; `render.*` software iso renderer (terrain, sprites, zone/road tint)
- `gamedir.*` auto-detect (env M3K_GAME_DIR, saved, GOG/Wine paths), drag-drop folder or GOG installer (needs `innoextract`)
- `savewrite.cpp` save writer (subset of records, round-trips)
- `sim.*` simplified sim: zone painting, roads, RCI valves, developers, taxes
- `src/app/main.cpp` SDL app: tools 1-9/R/B/X, space pause, F1-F3 speed

## Known deviations / gaps
- Sim: no power/water/land value (assumed present, LV=128), flat tolerance 2 not exact, bootstrap +400 residential demand, simplistic budget, no disasters/services/traffic
- No UI chrome (menus, toolbars); keyboard/mouse only. No audio, no intro video
- All real-data paths untested: no game install here. Expect fixes on first run with real files
- Saved files omit many records (original treats absent as new-city state; unverified)

## Next
1. Test on real data (other agent/system); fix parser mismatches
2. UI: toolbar, status bar, text rendering (FBF fonts, spec 05)
3. Power/water, land value, services; network tiling rules (spec 12)
4. Audio (spec 06), intro video, scenarios
