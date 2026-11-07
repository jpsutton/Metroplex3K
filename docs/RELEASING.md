# Branches, CI and releasing

## Branches

| Branch | Purpose | CI |
|--------|---------|----|
| `staging` | The default branch. Feature and fix branches are merged here through pull requests, so changes collect in one place. | `staging.yml`: fast checks only (about a minute; seconds with a warm cache) |
| `release` | Only released states. Updated by one pull request from `staging` per release. | `release.yml`: the extensive checks and the packages |
| `feat/*`, `fix/*` | Short-lived work. Branch from `staging`, open a PR into `staging`, delete after merge. | `staging.yml` |

## Fast checks (pull requests into `staging`)

`.github/workflows/staging.yml`, on Ubuntu, with a compiler cache:

1. `tools/ci/static-checks.sh`: SPDX headers, the original game's name only where allowed, no internals of the original
   program, no game assets or binaries tracked, no conflict markers, no large files, a changelog section for the version.
2. Build the core library and unit tests at -O1 (no SDL, tools or app) and run `ctest`.

Run the same thing locally: `tools/ci/static-checks.sh`, then
`cmake -S . -B build -G Ninja -DM3K_BUILD_APP=OFF -DM3K_BUILD_TOOLS=OFF -DM3K_BUILD_PROBES=OFF && cmake --build build && ctest --test-dir build -j`.

## Release checks (pull request `staging` to `release`)

`.github/workflows/release.yml`:

1. Repository checks: the static checks and the release-gate check (`docs/RELEASE-GATE.md`).
2. Build on Linux, macOS and Windows in Release mode with the application, tools and probes; run the unit tests; run
   `metroplex3k --version` and check it names the version; package with CPack; check the package contents
   (`tools/ci/check-package.py`); upload the archives as workflow artifacts.
3. Unit tests under AddressSanitizer and UndefinedBehaviorSanitizer on Linux.

When the pull request is merged (a push to `release`), the same workflow runs again and then publishes: it creates the
tag `v<version>` and a GitHub release with the archives and the `CHANGELOG.md` section as notes.

Packages (CPack): `metroplex3k-<version>-<OS>-<arch>.tar.gz` (Linux) or `.zip` (macOS, Windows), plus the same name with
`-tools` for `diag`, `conformance` and `probe_verify`. A manual run (Actions, "release", Run workflow) builds and checks
everything and publishes nothing. Packages can also be built locally: `cmake --build build --target package`.

## Cutting a release

1. On `staging`, make sure `staging.yml` is green.
2. Set the version in `CMakeLists.txt` (`project(Metroplex3K VERSION x.y.z ...)`) and add a `## [x.y.z] - YYYY-MM-DD` section to
   `CHANGELOG.md` (replace "Unreleased"). Commit.
3. On a machine with the game install: `tools/release-gate.sh /path/to/game --ref /path/to/refs`. Commit
   `release-gate/x.y.z.md` (only if it ends with `Result: PASS`).
4. Open one pull request from `staging` to `release` (template: `?template=release.md`). Wait for `release.yml`.
5. Merge it with a merge commit. The push to `release` publishes the release. Do not push to `release` any other way.
6. After the release, bump the version on `staging` for the next cycle when work resumes.

Versions follow Semantic Versioning. Development builds report `<version>-dev`; release builds report `<version>`.

## Versioning of builds

`CMakeLists.txt` holds the numeric version. `M3K_VERSION_SUFFIX` defaults to `-dev`; the release workflow and the
release-gate script pass an empty suffix. `metroplex3k --version` prints the version and the git commit.
