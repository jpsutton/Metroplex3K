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

## Next
1. Test for savefile (synthetic round trip) + `tools/` dump utility
2. Game dir discovery (`gamedir.*`): CLI arg, env `M3K_GAME_DIR`, config file, GOG/Wine default paths, folder prompt
3. SDL app: window, terrain renderer (spec 25 sec 6), buildings/networks/flora via CSATTRIB, scroll/zoom/rotate
4. Terrain generator (spec 11 sec 3) for New City
5. Minimal sim: zoning, RCI growth, budget
6. Save writer (spec 10 sec 8)
7. GOG installer helper (innoextract hook)

## Notes
- Android: keep `src/core` free of OS APIs; app layer isolated in `src/app`.
- If interrupted: read this file, run build + ctest, continue at "Next".
