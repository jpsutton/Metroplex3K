# Metroplex3K

Metroplex3K is a C++17 engine that is compatible with *SimCity 3000 Unlimited* (Maxis/EA, 2000; GOG build 2.0.949).
It loads the original game data from **your own installation**, reads and writes the original city, terrain, scenario
and BAT file formats, and re-implements the simulation, rendering and UI.

> **Status: experimental.** The engine was written from the specification in `spec/` and tested only on synthetic
> fixtures. It has **never been verified against real game data**. Expect fixes to be needed on first run with a real
> install. See [STATUS.md](STATUS.md).

## Clean-room status

The code was written by an implementation team that only had access to the specification in `spec/`. The spec came
from a separate analysis team and contains no original code, internals, or copied creative content (rules in
[spec/00-README.md](spec/00-README.md)). No original program code was consulted while writing this engine.
One exception to flag: the MPEG-1 VLC tables in `src/core/tqi.cpp` — see [docs/LICENSING.md](docs/LICENSING.md).

**No game assets are redistributed.** This repository contains no art, sound, music, video, text, maps or
buildings from the original game. Everything is read at run time from the copy you own. You must own the game.

## Supplying your game files

You need a copy of SimCity 3000 Unlimited, for example the GOG release. The game root is the folder that contains
`Apps/`. Any of these works:

1. **Command line:** `metroplex3k --game-dir "/path/to/SimCity 3000 Unlimited"`. The path may be the game root, its
   `Apps` folder, or a parent folder such as `GOG Games` (searched up to 3 levels down).
2. **Environment:** `M3K_GAME_DIR=/path/to/game metroplex3k`.
3. **Drag and drop** onto the window: either the installed game folder, or the GOG installer
   (`setup_simcity_3000_unlimited_*.exe`). Installer extraction needs [`innoextract`](https://constexpr.org/innoextract/)
   on `PATH` (Linux/macOS/Windows). Not available on Android.
4. **Type the path** on the first screen ("Use this folder").
5. **Auto-detect**, tried in this order: `$M3K_GAME_DIR`, the choice saved from a previous run, then common locations:
   - Windows: `C:\GOG Games\SimCity 3000 Unlimited`, `C:\Program Files (x86)\GOG Galaxy\Games\...`, `D:\GOG Games\...`
   - Linux/macOS: `~/GOG Games/...`, `~/Games/...`, `~/Games/gog/...` (including Wine/Lutris `drive_c/GOG Games/...`),
     `~/.wine/drive_c/GOG Games/...`, `~/.local/share/...`, `/Applications/...`, `/opt/...`

Manual extraction on Linux: `innoextract -d ~/GOG\ Games/SimCity\ 3000\ Unlimited setup_simcity_3000_unlimited_*.exe`
(use the `app/` folder contents as the root if the result nests it; auto-detect searches below the given folder).

Without game data the app starts on a "need data" screen and offers a generated-terrain demo with a built-in font and menus.

## Building

Requirements: CMake >= 3.16, a C++17 compiler. SDL2 is needed **only** for the desktop app. The core library, tests
and tools need no SDL. If SDL2 is not found the app is skipped with a status message. Use `-DM3K_BUILD_APP=OFF` to
skip it explicitly.

```
mkdir -p build && cd build && cmake .. && make -j8 && ctest
```

### Linux
```
sudo apt install build-essential cmake libsdl2-dev     # Debian/Ubuntu
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```
### macOS
```
brew install cmake sdl2
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```
### Windows (Visual Studio / vcpkg)
```
vcpkg install sdl2:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release
```
Omit the vcpkg steps and pass `-DM3K_BUILD_APP=OFF` to build only core, tests and tools.

Android: design notes only, see [docs/ANDROID.md](docs/ANDROID.md).

Run: `build/metroplex3k [--game-dir DIR] [--city FILE]`.

## Controls

Desktop host (`src/app/main.cpp`, mapping into the platform-neutral `Input` struct):

| Input | Action |
|-------|--------|
| Left mouse | Click UI, apply the current tool (click, or drag for areas/lines) |
| Right mouse | Cancel/context action on the map |
| Mouse wheel | Zoom (5 levels) |
| Arrow keys / WASD | Scroll the map |
| `+` / `-` (`=`) | Zoom in / out |
| Space | Pause / resume |
| `g` | Toggle grid |
| `Esc` | Cancel query/tool, close menu/window, then return to main menu |
| `Enter` | Confirm dialogs |
| `Shift+B`, `O`, `M`, `N`, `P` | Open budget and other windows (see `Game::drawInCity` in `src/core/game.cpp`) |
| `q w e a s d z x c v l r t b` | Tool hotkeys (`Game::loadHotkeys`, spec 08/09) |
| `Tab` | Open the cheat box (see Limitations) |
| `F11` | Toggle fullscreen |
| Drop file/folder | Provide game data or open a city file |

## Repository layout

```
src/core/   platform-independent engine (no SDL, no OS APIs); static library m3k_core
src/app/    SDL2 desktop host (main.cpp)
tools/      developer tools (headless)
tests/      test_*.cpp, plain executables registered with CTest
spec/       the compatible-engine specification (00-README.md is the index)
docs/       ARCHITECTURE.md, ANDROID.md, LICENSING.md
android/    Android port scaffold (notes only)
STATUS.md   current implementation status
```

## Testing

`ctest` (in the build dir) runs about 30 test executables from `tests/`. They use synthetic fixtures and need no game data.
Passing tests means the code matches the spec as the authors read it, not that it matches the real game.

## Tools

All build to `build/` next to the tests and need no SDL.

| Tool | Usage |
|------|-------|
| `conformance` | `conformance [--strict] [--verbose] <game dir>` — parse every shipped file, print coverage (spec 28). Run this first on a real install. |
| `render_demo` | `render_demo [--game DIR] [--city FILE] [--seed N] [--zoom Z] [--rot R] [--size 128\|192\|256] out.png` — render terrain or a city to PNG |
| `game_demo` | `game_demo [--game DIR] out_prefix` — scripted UI smoke test, writes PNG screenshots |
| `city_sim_demo` | `city_sim_demo [--game DIR] [--seed N] [--years N] [--size 128] out.png` — headless growth check with yearly stats |
| `windows_demo` | `windows_demo [outdir]` — render every in-game window with fake data to `win_<name>.png` |

## Status and limitations

- Never verified against real game data; first run on a real install will probably expose bugs.
- Original UI skins are not used; UI is drawn with the built-in toolkit (original fonts are used when present).
- Some growth layouts and many details are `[OPEN]` in the spec; recommended defaults from `spec/29-open-issues.md` are used.
- The intro movie decoder (`tqi.*`) is untested on real files.
- The desktop host does not yet map `Tab` (cheat box) or Left/Right keys into `Input`.
- Android is not implemented (docs only).

## License

The owner has not chosen a license yet. Until one is added, all rights are reserved. See [docs/LICENSING.md](docs/LICENSING.md).
