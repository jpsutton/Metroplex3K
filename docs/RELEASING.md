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
2. Build the Arch packages in an `archlinux` container (`arch` job).
3. Build on Linux, macOS and Windows in Release mode with the application, tools and probes; run the unit tests; run
   `metroplex3k --version` and check it names the version; package with CPack; check the package contents
   (`tools/ci/check-package.py`); upload the archives as workflow artifacts.
4. Unit tests under AddressSanitizer and UndefinedBehaviorSanitizer on Linux.

When the pull request is merged (a push to `release`), the same workflow runs again and then publishes: it signs the
assets, creates the tag `v<version>` and a GitHub release with the archives, the Arch packages, signatures, `SHA256SUMS` and
the `CHANGELOG.md` section as notes, then asks the pacman repository to update.

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

## Signing, Arch packages and the pacman repository

The release workflow also builds Arch packages (`metroplex3k`, `metroplex3k-tools`) from `packaging/linux/arch/PKGBUILD`
in an `archlinux` container (`tools/ci/build-arch.sh`; `makepkg` runs the unit tests). The package version is read from
`CMakeLists.txt`. On the merge, `tools/ci/sign-release.sh` signs every asset with a detached `.sig`, writes `SHA256SUMS`
(also signed) and attaches the public key. `tools/install-macos.sh` checks the macOS archive against `SHA256SUMS`.

| Secret | Use |
|--------|-----|
| `METROPLEX3K_GPG_KEY` | Armored private key of the release signing key (fingerprint `D101268C3679162D5F57663BC929869EF199693C`; public half `packaging/metroplex3k-signing-key.asc`). The publish job fails without it. |
| `ARCH_REPO_DISPATCH_TOKEN` | Optional. Fine-grained token with Actions read and write on `jpsutton/arch-repo`; used to start its `publish.yml` at once. Without it the repository picks the release up within the hour. |

[jpsutton/arch-repo](https://github.com/jpsutton/arch-repo) is the signed pacman repository for my projects. Nothing is
built there: its publish workflow takes the newest GitHub release of each project in its `sources.txt`, verifies each
package's `.sig` against that project's release key (pinned by fingerprint, `keys/sources/Metroplex3K.asc`), re-signs
with the repository key and publishes to GitHub Pages. Consequences for this project:

- Every release must carry the two `*.pkg.tar.zst` files with `.sig`. A release without packages makes the repository's
  publish fail for all projects, so never remove the `arch` job.
- Changing the signing key means updating `keys/sources/Metroplex3K.asc` and the fingerprint in `sources.txt` there first.

## Repository settings

GitHub branch protection and rulesets are not available for a private repository on the free plan (the API answers
"Upgrade to GitHub Pro or make this repository public"), so the flow is enforced by convention and by the workflows
rather than by branch rules. When that changes, protect the branches as follows:

- `staging`: require a pull request; required status check `fast` (workflow `staging`).
- `release`: require a pull request from `staging`; required status checks `repository checks`, `build (ubuntu-latest)`,
  `build (macos-latest)`, `build (windows-latest)` and `unit tests under ASan and UBSan` (workflow `release`); no direct
  pushes. Anything pushed to `release` publishes a release, so only merge the release pull request.

## Measured times

Fast checks on a private-repo runner: about 35 s with a warm compiler cache (build 7 s, unit tests 9 s, static checks 1 s),
about 3 minutes cold. The cache is refreshed by every push to `staging`. The release workflow takes about 15 minutes
(macOS and Windows builds dominate), and runs once per release.

The publish job (tag and GitHub release) only runs for a push to `release`. A manual run of the workflow builds and
checks everything but skips it, so the publish step is exercised only by the first real release.
