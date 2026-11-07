Release-gate reports, one file per released version (`<version>.md`), written by `tools/release-gate.sh` and committed
in the release pull request. CI refuses a release without a passing report for the version in `CMakeLists.txt`.
See [docs/RELEASE-GATE.md](../docs/RELEASE-GATE.md).
