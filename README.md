# Metroplex3K

Metroplex3K is a C++17 engine that is compatible with *SimCity 3000 Unlimited* (Maxis/EA, 2000; GOG build 2.0.949).
It loads the original game data from **your own installation**, reads and writes the original city, terrain, scenario
and BAT file formats, and re-implements the simulation, rendering and UI.

> **Status: pre-1.0, feature complete apart from the gaps listed below.** The engine was first written from the
> specification in `spec/` with only synthetic fixtures. It has since been run against a real GOG install: parsers,
> save round trips (47 of 60 shipped saves byte-identical, the rest differ only in compression bytes), and budget,
> pollution, crime, water and power results are checked against the shipped saves. The work left before 1.0 is
> verification and packaging, tracked in [issue #106](https://github.com/jpsutton/Metroplex3K/issues/106). See
> [Remaining gaps](#remaining-gaps) and [STATUS.md](STATUS.md).

## Clean-room status

The code was written by an implementation team that only had access to the specification in `spec/`. The spec came
from a separate analysis team and contains no original code, internals, or copied creative content (rules in
[spec/00-README.md](spec/00-README.md)). No original program code was consulted while writing this engine.
One exception to flag: the MPEG-1 VLC tables in `src/core/tqi.cpp` — see [docs/LICENSING.md](docs/LICENSING.md).

**No game assets are redistributed.** This repository contains no art, sound, music, video, text, maps or
buildings from the original game. Everything is read at run time from the copy you own. You must own the game.

## Installing

Pre-built packages are on the [Releases page](https://github.com/jpsutton/Metroplex3K/releases/latest). Release archives
and packages are signed; `SHA256SUMS` and the public key (`metroplex3k-signing-key.asc`) are attached to each release.

| Platform | Packages |
| --- | --- |
| Linux | Arch Linux package repository (below), or a `.tar.gz` for other distributions (Ubuntu 24.04 or newer; needs SDL2 and GLEW 2.2) |
| macOS | `.zip` (Apple silicon), installed by a script (below) |
| Windows | `.zip` (x86-64); `SDL2.dll` is included |

Windows and macOS builds are not signed by Microsoft or Apple, so your system warns the first time you open them. There is
no self-update; install again to update.

### Arch Linux

The packages (`metroplex3k`, and `metroplex3k-tools` with the diagnostics) are built in an Arch container, signed, and
published in the [jpsutton pacman repository](https://github.com/jpsutton/arch-repo), which also carries my other projects.
The package links your system's SDL2 and GLEW, so it works with whatever versions Arch ships. Set the repository up with its
script, then install:

```bash
curl -fsSLO https://jpsutton.github.io/arch-repo/setup.sh
bash setup.sh --install metroplex3k
```

To set it up by hand, add this to `/etc/pacman.conf`, then trust the key and install:

```ini
[jpsutton]
Server = https://jpsutton.github.io/arch-repo/$arch
```

```bash
curl -fsSLO https://jpsutton.github.io/arch-repo/jpsutton-signing-key.asc
sudo pacman-key --add jpsutton-signing-key.asc
sudo pacman-key --lsign-key 8476350364502FEDD331B66BEDB90079E3C73E41
sudo pacman -Sy metroplex3k
```

The package adds Metroplex3K to your application menu. Updates arrive through `pacman -Syu`. To build the package yourself: `cd packaging/linux/arch && makepkg -si`.

### macOS

The easiest way to install or update is the install script. It downloads the latest `.zip`, checks it against the release's
`SHA256SUMS`, and unpacks Metroplex3K into `~/Applications/Metroplex3K`:

```sh
curl -fsSL https://raw.githubusercontent.com/jpsutton/Metroplex3K/release/tools/install-macos.sh | bash
```

Add `-s -- --tools` after `bash` to also install the developer tools. Metroplex3K needs SDL2 (`brew install sdl2`). Files
downloaded with `curl` aren't quarantined, so the program starts without a Gatekeeper prompt. Run the same command again to
update. If you download the `.zip` in a browser instead, macOS blocks the first launch; clear the quarantine with
`xattr -dr com.apple.quarantine ~/Applications/Metroplex3K`.

### Other Linux and Windows

Unpack the archive anywhere and run `metroplex3k`. On Linux, `./install-desktop.sh` in the unpacked folder adds Metroplex3K to
your application menu (`./install-desktop.sh --remove` removes it). See [docs/USER-GUIDE.md](docs/USER-GUIDE.md).

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

Run: `build/metroplex3k [--game-dir DIR] [--city FILE] [--language NAME]`. `--game` is an alias for `--game-dir`.
`NAME` is an installed language (English, French, German, ...); the default is `$M3K_LANGUAGE`, else the OS locale.

`metroplex3k --version` prints the version.

CI (`.github/workflows`): pull requests into `staging` run fast checks (static checks, core build and unit tests, about a
minute). The single pull request from `staging` into `release` runs the extensive checks (Linux, macOS and Windows builds
with the app, unit tests, sanitizers, packaging) and, once merged, publishes the release. See
[docs/RELEASING.md](docs/RELEASING.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Controls

Desktop host (`src/app/main.cpp`) passes mouse, keyboard and dropped files to the platform-neutral `Input` struct.
Keyboard shortcuts come from the game's own hotkey table (`Res/Hotkeys/<language>.IXF`) when present, else from the
built-in defaults (`HotkeyTable::loadDefaults` in `src/core/hotkeys.cpp`). Commands run in `Game::hotkeyCommand`
(`src/core/game.cpp`). Defaults:

| Input | Action |
|-------|--------|
| Left mouse | Click UI, apply the current tool (click, or drag for areas/lines) |
| Right mouse (hold and drag) | Pan the map |
| Mouse wheel | Zoom (5 levels) |
| Arrow keys | Scroll the map |
| Page Up / Page Down | Rotate the view |
| Numpad `+` / `-` | Zoom in / out |
| Numpad `1`-`5` | Jump to a zoom level |
| `/` | Pause / resume |
| `,` / `.` | Slower / faster |
| Space | Centre the map on the pointer |
| `g` | Toggle grid |
| `F12` | Hide / show the toolbars |
| `F11` | Toggle fullscreen (host) |
| `Esc` | Cancel the current tool |
| `Enter` | Confirm dialogs |
| `Ctrl+A`, `Ctrl+B`, `Ctrl+O`, `Ctrl+M`, ... | Advisors, budget, ordinances, maps and other windows |
| `q w e a s d z x c v l r t b` and others | Tool hotkeys |
| `Ctrl+Alt+Shift+C` | Open the cheat box |
| `F10`, `Ctrl+Q` | Quit |
| Drop file/folder | Provide game data or open a city file |

Edge scrolling is off by design (spec 29 3.5). Terrain edit mode accepts only a restricted set of keys.

## Repository layout

```
src/core/       platform-independent engine (no SDL); static library m3k_core
src/app/        SDL2 desktop host (main.cpp)
tools/          developer tools (headless)
tools/probes/   checks that compare the engine with a real install (see tools/probes/README.md)
tests/          test_*.cpp, plain executables registered with CTest
spec/           the compatible-engine specification (00-README.md is the index)
docs/           USER-GUIDE.md, RELEASING.md, RELEASE-GATE.md, ARCHITECTURE.md, ANDROID.md, LICENSING.md, IP-REVIEW.md
CHANGELOG.md    release notes (the release workflow publishes the section for the current version)
release-gate/   release-gate reports, one per released version
STATUS.md       current implementation status
```

## Testing

`ctest` (in the build dir) runs the test executables from `tests/` (one per `test_*.cpp`). They use synthetic fixtures and
need no game data. Passing tests means the code matches the spec as the authors read it, not that it matches the real
game. For checks against a real install, use the probes in `tools/probes/`.

## Tools

All build to `build/` next to the tests and need no SDL. The probes in `tools/probes/` build too (`-DM3K_BUILD_PROBES=OFF` to skip).

| Tool | Usage |
|------|-------|
| `conformance` | `conformance [--strict] [--verbose] <game dir>` — parse every shipped file, print coverage (spec 28). Run this first on a real install. |
| `render_demo` | `render_demo [--game DIR] [--city FILE] [--seed N] [--zoom Z] [--rot R] [--size 128\|192\|256] out.png` — render terrain or a city to PNG |
| `game_demo` | `game_demo [--game DIR] out_prefix` — scripted UI smoke test, writes PNG screenshots |
| `city_sim_demo` | `city_sim_demo [--game DIR] [--seed N] [--years N] [--size 128] out.png` — headless growth check with yearly stats |
| `windows_demo` | `windows_demo [outdir]` — render every in-game window with fake data to `win_<name>.png` |
| `game_play_demo` | `game_play_demo [--game DIR] [--size WxH] out_prefix` — build a small city through `Game`, write screenshots |
| `hud_shot` | `hud_shot --game DIR [--city FILE] --size WxH [--do STEP]... out.png` — scripted headless UI screenshot (steps listed in the file header) |
| `skin_demo` | `skin_demo [outdir [GAMEDIR]]` — render the skinned UI chrome (synthetic art, or real art with `GAMEDIR`) |
| `diag` | `diag GAME_DIR [OUT_DIR]` — report what the engine can read from an install; paste the output into bug reports |
| `frame_bench` | `frame_bench --game DIR --city FILE [--size WxH] [--frames N]` — time `Renderer::render` per zoom, `Game::frame` and a music decode; prints a hash per frame so render changes can be checked for pixel identity |

The renderer rasterises in horizontal bands on a worker pool (`src/core/workpool.h`). Set `M3K_THREADS=N` to choose the thread count (`1` = single-threaded); the default is the hardware thread count, capped at 16.

## Status and limitations

Known gaps, guesses and verification tasks are tracked as [GitHub issues](https://github.com/jpsutton/Metroplex3K/issues)
(see [STATUS.md](STATUS.md) for the labels). Notable points:

- Verified against a real install only where [STATUS.md](STATUS.md) says so; other areas may still differ from the original.
- Original UI skin art and fonts are used when present, with the built-in toolkit and font as fallback.
- Some growth layouts and details are `[OPEN]` in the spec; recommended defaults from `spec/29-open-issues.md` are used.
- Android is not implemented (docs only).

### Remaining gaps

The [1.0 tracking issue (#106)](https://github.com/jpsutton/Metroplex3K/issues/106) lists everything. Milestone
[`1.0`](https://github.com/jpsutton/Metroplex3K/milestone/1) holds what must be done or waived before the release;
milestone [`post-1.0`](https://github.com/jpsutton/Metroplex3K/milestone/2) holds gaps accepted for 1.x.

Before 1.0: packaging, versioning and the release gate are in place. What remains is running the release gate
(`tools/release-gate.sh`, on a machine with the game install; see [docs/RELEASE-GATE.md](docs/RELEASE-GATE.md)) and
cutting the release as described in [docs/RELEASING.md](docs/RELEASING.md).

After 1.0: scenario verification ([#91](https://github.com/jpsutton/Metroplex3K/issues/91)), disaster comparison with the original ([#92](https://github.com/jpsutton/Metroplex3K/issues/92)), an audio listening pass ([#94](https://github.com/jpsutton/Metroplex3K/issues/94)), UI pixel differences ([#95](https://github.com/jpsutton/Metroplex3K/issues/95)), scenario spec polish ([#97](https://github.com/jpsutton/Metroplex3K/issues/97)), BAT tool UI
([#98](https://github.com/jpsutton/Metroplex3K/issues/98)), guessed details in growth
([#99](https://github.com/jpsutton/Metroplex3K/issues/99)), services and pollution
([#100](https://github.com/jpsutton/Metroplex3K/issues/100)), economy and population
([#101](https://github.com/jpsutton/Metroplex3K/issues/101)), networks
([#102](https://github.com/jpsutton/Metroplex3K/issues/102)) and advisors
([#103](https://github.com/jpsutton/Metroplex3K/issues/103)), the Android port and DPI scaling
([#104](https://github.com/jpsutton/Metroplex3K/issues/104)), and open spec items
([#105](https://github.com/jpsutton/Metroplex3K/issues/105)).

## License

Metroplex3K is free software, licensed under the **GNU General Public License, version 3 or (at your option) any
later version** (`GPL-3.0-or-later`). The full text is in [LICENSE](LICENSE). Contributions are accepted under the same
terms. The license covers this repository's code and documentation only. It grants no rights to *SimCity 3000
Unlimited* or any of its assets, which you must own and supply yourself. *SimCity* is a trademark of Electronic Arts
Inc.; this project is not affiliated with or endorsed by EA or Maxis. See [docs/LICENSING.md](docs/LICENSING.md) for
third-party notes and [docs/IP-REVIEW.md](docs/IP-REVIEW.md) for the intellectual-property review.
