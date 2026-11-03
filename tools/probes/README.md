# Probes

Small command-line checks that run the engine against a **real** SimCity 3000 Unlimited install and print numbers
to compare against what the shipped saves record. They never modify the install and ship no game data.

Built with the other tools (`M3K_BUILD_PROBES`, default ON; they are not CTest tests because CI has no install).
Point each at the install with the first argument or `$M3K_GAME_DIR`.

| Probe | What it checks | Issue |
|---|---|---|
| `probe_city_decline GAME [city...]` | Load saved cities, run 450 days, print population, building states and valves; also saved vs recomputed valve supply/demand after the first month. A healthy city stays roughly flat. | #78 |
| `probe_spec_checks GAME [city...]` | Without running the sim forward: land value recomputed vs saved grid (ratio, correlation, Href, centre), valve cap rule, unemployment, recycling ratio and pump outputs vs saved values. `M3K_LV_EXPLAIN=1` prints the worst land-value blocks, `M3K_GARBAGE=1` the garbage counters and landfill store, `M3K_PUMPS=1` per-pump state, `M3K_POWER=1` power deal draw, `M3K_WATER=1` dry lots at load vs after a month (and bitmap orientation), `M3K_TRIPS=1` trip-bitmap orientation, `M3K_RESULTS=1` the original's saved result-code counts vs our first passes (with code 3 by zone), `M3K_PASSES=1` the result codes of every completed pass over 250 days, `M3K_PIPEFLAG=1` occupants carrying flag 0x800000, `M3K_ZONES=1` zone tile counts, `M3K_PWRTRACE=1` power, pump and water state after each month start, `M3K_FACBOX=x0,z0,x1,z1` water facilities in a box (with `M3K_FLOODSEED=x,z`: the spec 16 s3.4 flood from that seed), `M3K_PLANTS=1` power plants after a month, `M3K_LVTILES="x,z;..."` land value and its inputs at tiles after `M3K_LVDAYS` days. | #78 #10 #12 #13 |
| `probe_compare_saves GAME <city> <observed.sc3> [x,z...]` | Runs a shipped city to the date of a save written by the original (spec 28 s5.1 reference runs, kept outside the repo) and diffs the layers: land value, air/water pollution, crime, power and water service, lot states, valves, connection supplies; optional tiles side by side. | #78 #44 #49 |
| `probe_rule_members GAME [ruleIdHex...]` | Members (size, building flag word) of the non-scatter developer rules, for checking the member-list filing of spec 15 s5.5. | #14 |
| `probe_retile GAME [city...]` | Re-run the network tiler over each saved road/rail/power network and diff against the stored pieces. | #47 |
| `probe_cursors GAME [sheet.png]` | Decode every city-view cursor from its `.cur` (what the game shows: black/white with a transparency mask), check the hotspots lie inside the image, report how the colour `.bmp` preview differs (54 of 96 do), and optionally write a contact sheet with the hotspots marked. | #67 |
| `probe_verify GAME [--ref REFDIR] [--only l0,l1,l2,audio,scn,l3] [--report out.tsv] [--shots OUTDIR]` | **The pass/fail harness for everything that needs the real install** (run it on the machine that owns the install; exit status 1 on any FAIL). l0 runs the sibling `conformance --strict` (spec 28 s2 counts and the .bld / BAT.DAT / SNR / cfg round trips); l1 load - save - load - save of every .sc3/.st3/.sct; l2 network re-tiling vs the stored pieces (baseline floors); audio: all 814 .xa decode and are audible, all .FSC parse; scn: all 13 scenarios load, run, open the goals window and the end screen; l3 (needs `--ref` with the observed `W5_<City>_sNN.sc3` saves): population after the same number of days within 10 % of the original (spec 28 s5.3); `--shots` renders the fixed scene list at 800x600, 1024x768 and 1280x800 plus manifest.tsv for comparison with the original. | #70 #52 #57 #64 #65 #66 |
| `probe_xa GAME` | Decode every shipped `.xa`; compare with `ffmpeg` when it is on `PATH`. | #66 |
| `probe_disasters GAME [city]` | Start each disaster in a loaded city and report how it ends and what it damages. | #35 #37 |
| `probe_overview GAME [name...]` | Compare the overview colours (mini-map / thumbnail) with the preview thumbnail of every shipped `.sc3` and `.sct`. Expect 0 differing tiles. | |
| `probe_play_smoke GAME` | Build a small city from empty land (roads, zones, plant, pump, power, pipes) and run 10 years. | |

Exit status is 0 unless the install cannot be opened; numbers are for reading, not pass/fail.
