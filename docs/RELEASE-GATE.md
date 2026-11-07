# Release gate

The release gate proves that a build of the committed code behaves like the original game on a real install. CI has no
game data, so the gate runs on a machine that has one, before the release pull request. The result is a small report,
`release-gate/<version>.md`, committed in that pull request. The release workflow refuses to publish without it.

## What the gate runs

`tools/release-gate.sh` builds the committed tree in Release mode (suffix-free version), then runs:

| Step | What it checks | Pass means |
|------|----------------|------------|
| `ctest` | All unit tests | Every test passes |
| `conformance --strict` | Every shipped file parses with the engine's parsers; `.bld`, `BAT.DAT`, `.SNR` and `.cfg` round trips (spec 28 section 2) | Exit status 0 |
| `probe_verify` | The groups below | No FAIL row; exit status 0 |

`probe_verify` groups:

| Group | Checks |
|-------|--------|
| `l0` | Runs `conformance --strict` again and checks the spec 28 counts |
| `l1` | Load, save, load, save of every `.sc3`, `.st3` and `.sct`; the second save equals the first |
| `l2` | Network re-tiling against the stored pieces (baseline floors), terrain generator and other deterministic checks it covers |
| `audio` | Every `.xa` decodes and is audible; every `.FSC` parses |
| `scn` | All 13 scenarios load, run, open the goals window and the end screen |
| `l3` | 480-day runs against the observed reference saves: population within 10 % (needs `--ref`) |
| `shots` | With `--shots DIR`: fixed scenes at 800x600, 1024x768 and 1280x800 as PNG plus `manifest.tsv`. For comparison by eye with the original; not pass/fail. |

Rows marked `INFO` carry measurements and do not affect the result.

## Running it

You need the game install and, for `l3`, the reference saves (`W5_<City>_sNN.sc3`, spec 28 section 5.1; not distributed).

```
tools/release-gate.sh /path/to/game --ref /path/to/reference-saves --shots /tmp/m3k-shots
```

- The working tree must be clean, because the report names the commit. Use `--allow-dirty` for a trial run only.
- Without `--ref` the `l3` group does not run and the report says so. Run it with `--ref` for a release.
- The script exits 0 and prints "GATE PASSED" when everything passes. It always writes `release-gate/<version>.md`;
  never commit a report whose last line is `Result: FAIL`.

## What CI checks

`tools/ci/check-release-gate.sh` runs on the release pull request and again on the merge. It fails unless:

1. `release-gate/<version>.md` exists, has `Version: <version>` and the line `Result: PASS`.
2. The commit named in the report is an ancestor of the release tree, and nothing under `src/`, `tests/`, `tools/*.cpp`,
   `tools/probes/` or `CMakeLists.txt` changed after it. Docs, the changelog and the report itself may change.
3. `CHANGELOG.md` has a section for the version with a date (not "Unreleased").
4. The tag `v<version>` does not exist yet.

If you change code after running the gate, run the gate again.

## Known gaps in the gate

- The `l2` and `l3` groups do not yet cover all of spec 28 (for example the terrain generator against more original
  seeds, the land-value pass against the stored layer, and per-city bands for valves, lot states and power). They are
  tracked as post-1.0 work on GitHub. Add new checks to `tools/probes/probe_verify.cpp` as `row(...)` entries and they
  join the gate automatically.
- Screenshot comparison with the original is by eye.
