# Probes

Small command-line checks that run the engine against a **real** SimCity 3000 Unlimited install and print numbers
to compare against what the shipped saves record. They never modify the install and ship no game data.

Built with the other tools (`M3K_BUILD_PROBES`, default ON; they are not CTest tests because CI has no install).
Point each at the install with the first argument or `$M3K_GAME_DIR`.

| Probe | What it checks | Issue |
|---|---|---|
| `probe_city_decline GAME [city...]` | Load saved cities, run 450 days, print population, building states and valves; also saved vs recomputed valve supply/demand after the first month. A healthy city stays roughly flat. | #78 |
| `probe_retile GAME [city...]` | Re-run the network tiler over each saved road/rail/power network and diff against the stored pieces. | #47 |
| `probe_xa GAME` | Decode every shipped `.xa`; compare with `ffmpeg` when it is on `PATH`. | #66 |
| `probe_disasters GAME [city]` | Start each disaster in a loaded city and report how it ends and what it damages. | #35 #37 |
| `probe_play_smoke GAME` | Build a small city from empty land (roads, zones, plant, pump, power, pipes) and run 10 years. | |

Exit status is 0 unless the install cannot be opened; numbers are for reading, not pass/fail.
