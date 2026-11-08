# Changelog

All notable changes to Metroplex3K are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/). The release workflow publishes the section for the
version in `CMakeLists.txt` as the release notes, so every release needs a `## [x.y.z]` section.

## [1.0.2] - Unreleased

### Added
- A launcher entry and icon on Linux. The Arch package installs `metroplex3k.desktop` and the icon (scalable SVG plus PNGs
  from 16 to 512 px), so Metroplex3K shows up in the application menu. The generic Linux archive carries the same files and
  `install-desktop.sh`, which adds the menu entry for the current user (`--remove` takes it out). The window class and
  Wayland application id are now `metroplex3k`, so desktops match the running window to its icon.

### Changed
- The repository is public. The Arch packages are published in the `jpsutton` pacman repository, and the macOS install
  script works without credentials.

## [1.0.1] - 2026-11-08

### Added
- Arch Linux packages (`metroplex3k` and `metroplex3k-tools`), built from `packaging/linux/arch/PKGBUILD`, signed, and
  published in the `jpsutton` pacman repository (https://github.com/jpsutton/arch-repo). They use the system SDL2 and
  GLEW, which fixes the start-up failure of the generic Linux archive on Arch (`libGLEW.so.2.2` not found).
- A macOS install script, `tools/install-macos.sh`: downloads the latest release, checks it against `SHA256SUMS` and
  installs it without the Gatekeeper quarantine prompt.
- Every release asset now has a detached GPG signature; `SHA256SUMS` and the public key are attached to the release.

### Fixed
- `probe_verify` finds its `conformance` helper when started through a symlink or from `PATH`, and on Windows.

## [1.0.0] - 2026-11-08

First release. Metroplex3K is a clean-room C++17 engine that is compatible with a commercial city-building game. It loads
the original game data from a copy you own and reads and writes the original city, terrain, scenario and BAT file formats.
No game assets are included.

### Added
- Loading and saving of cities, starter towns, terrains and scenarios. 47 of 60 shipped saves round trip byte for byte;
  the rest differ only in compression bytes.
- The simulation: clock and calendar, budget and ordinances, population and demographics, growth and zoning, land value,
  power, water and garbage, police, fire, health and education, pollution and crime, traffic and mass transit, ports,
  neighbour and business deals, advisors, news ticker and petitioners.
- Disasters with their effect layer: fires, riots, tornadoes, UFOs, locusts, toxic clouds, space junk and whirlpools.
- Scenarios with start, goal and results dialogs; all 13 shipped scenarios load and run.
- Network tools (roads, highways, rail, subway, pipes and power lines) with bridges, tunnels and neighbour connections.
- The original user interface art, fonts, menus, hotkeys, cursors, windows and dialogs, loaded from your install, with a
  built-in toolkit and font as fallback.
- Music, ambient sound and sound effects, and the intro movie.
- CPU and optional OpenGL rendering at five zoom levels and four rotations; multi-threaded rasteriser and simulation.
- Game data discovery: command line, `$M3K_GAME_DIR`, auto-detect of common GOG locations, drag and drop of the game
  folder or the GOG installer (needs `innoextract`).
- `metroplex3k --version`.
- Developer tools and real-install probes (`diag`, `conformance`, `probe_verify` and others), shipped as a separate
  archive.
- Release packaging: a `.tar.gz` for Linux and `.zip` archives for macOS and Windows, built by CI.
- Licensed under GPL-3.0-or-later, with SPDX headers on every source file.

### Known limitations
- Android is not implemented.
- Window and DPI scaling of the interface is not implemented.
- Some simulation details are best-effort guesses; see the post-1.0 issues on GitHub.
- Scenarios have not been run to every ending against the original, and disaster looks and timings are not yet compared
  with the original.
- Sound has not had a full listening pass.
- Long-run population: after about 470 simulated days, five of the six reference cities end within 2.1 % of the original's last save. Madrid ends 18.3 % above it (within the release gate's band of the last three saves, but a known deviation; the original's population dips there after pump outages).
- Release builds are not code-signed. macOS Gatekeeper and Windows SmartScreen will warn on first launch.
