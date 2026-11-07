# Metroplex3K user guide

Metroplex3K is an engine that is compatible with a commercial city-building game. It needs the data files of your own
copy of that game (see the README for which game). Nothing from the game is bundled.

## 1. Install

Download the archive for your system from the GitHub releases page and unpack it anywhere.

| System | Archive | Needs |
|--------|---------|-------|
| Linux (x86-64) | `metroplex3k-<version>-Linux-x86_64.tar.gz` | SDL2 and GLEW runtime libraries (Debian/Ubuntu: `sudo apt install libsdl2-2.0-0 libglew2.2`) |
| macOS | `metroplex3k-<version>-Darwin-<arch>.zip` | SDL2 (`brew install sdl2`) |
| Windows (x86-64) | `metroplex3k-<version>-Windows-AMD64.zip` | Nothing. `SDL2.dll` is in the archive. |

Release builds are not code-signed.
- macOS: on first launch, right-click the program and choose Open, or run `xattr -d com.apple.quarantine metroplex3k`.
- Windows: SmartScreen may warn; choose "More info", then "Run anyway".

The separate `-tools` archive holds `diag`, `conformance` and `probe_verify` (section 6).

## 2. Give it your game data

The game root is the folder that contains `Apps/`. Any of these works; the first match wins:

1. `metroplex3k --game-dir "/path/to/game"`. The path may be the game root, its `Apps` folder, or a parent folder.
2. The environment variable `M3K_GAME_DIR`.
3. The choice saved from a previous run (`gamedir.txt` in the settings folder, section 5).
4. Auto-detect in common GOG locations (see the README).

If nothing is found, the first screen asks for it. Drag the game folder, or the GOG installer, onto the window, or type
the path and press "Use this folder". Installer extraction needs [`innoextract`](https://constexpr.org/innoextract/) on
your `PATH`. Without game data the program offers a generated-terrain demo with a built-in font.

Check what the engine can read with the `diag` tool: `diag /path/to/game`.

## 3. Play

Start `metroplex3k`. Options:

```
metroplex3k [--version] [--game-dir DIR] [--city FILE] [--language NAME] [--frame-log FILE]
```

- `--city FILE` opens a city, terrain or scenario file at start.
- `--language NAME` picks an installed language (English, French, German, ...). Default: `$M3K_LANGUAGE`, else the OS locale.

The keys are the game's own hotkey table when your install has one. The README lists the defaults: right mouse button
drag pans, the wheel zooms, Page Up and Page Down rotate, `/` pauses, `F11` toggles full screen, `F10` or `Ctrl+Q` quits.

Saved cities are written to the `Cities` folder of the game root. If the game root is unknown, they go to `Cities` in the
settings folder.

## 4. Environment variables

| Variable | Effect |
|----------|--------|
| `M3K_GAME_DIR` | Game root, as above |
| `M3K_LANGUAGE` | Interface language |
| `M3K_THREADS=N` | Render thread count (`1` = single thread). Default: hardware threads, at most 16. |
| `M3K_RENDER=cpu\|gpu` | Terrain rendering on the CPU (default) or with OpenGL compute when available |
| `M3K_PRESENT=gl\|gl-basic\|sdl` | How frames reach the screen. Falls back to `sdl` when OpenGL is unavailable. |
| `M3K_SIM_THREAD=0` | Run the simulation tick on the frame thread |
| `M3K_NOISE_SEED=N` | Pin the terrain noise seed (reproducible terrain) |
| `M3K_FRAMELOG=FILE` | Same as `--frame-log` |

## 5. Settings

Preferences (`prefs.cfg`) and the remembered game folder (`gamedir.txt`) live here:

| System | Folder |
|--------|--------|
| Linux | `$XDG_CONFIG_HOME/metroplex3k`, else `~/.config/metroplex3k` |
| macOS | `~/Library/Application Support/Metroplex3K` |
| Windows | `%APPDATA%\Metroplex3K` |

Delete `gamedir.txt` to forget the game folder. Delete `prefs.cfg` to reset preferences.

## 6. Troubleshooting

- **"Game data needed" screen.** The folder is wrong or incomplete. Pick the folder that contains `Apps/` and run
  `diag /path/to/game`; paste its output into any bug report.
- **Black or garbled screen, or a crash at start.** Try `M3K_PRESENT=sdl metroplex3k`, then `M3K_RENDER=cpu`.
- **Low frame rate.** Run with `--frame-log frames.csv`; it writes per-frame timings and prints a summary on exit.
- **Check an install against the engine.** `conformance /path/to/game` parses every shipped file and reports coverage.
  `probe_verify /path/to/game` runs the pass/fail checks used for releases (`docs/RELEASE-GATE.md`).
- **Linux: `error while loading shared libraries`.** Install the SDL2 and GLEW libraries from section 1.

Report bugs at the project's GitHub issues page and include `metroplex3k --version`, your system, and the `diag` output.
