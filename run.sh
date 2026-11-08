#!/usr/bin/env bash
# Build (incrementally) and run Metroplex3K from this checkout, to try a change.
#
#   ./run.sh                 build if needed, run with a frame log (frames-<time>.csv in the project dir)
#   ./run.sh --installed     run the installed /usr/bin/metroplex3k instead, for comparison, with the same frame log
#   ./run.sh --no-log        run without a frame log
#   ./run.sh --no-build      skip the build step
#   ./run.sh --fullscreen    start full screen (the new build's --fullscreen flag; for --installed, which has no such
#                            flag, the window is made fullscreen through hyprctl a few seconds after it opens)
#   ./run.sh -- ARGS...      pass the rest to metroplex3k (for example --city "/path/to/City.sc3")
#
# Environment: BUILD_DIR (default build-release; the first build takes several minutes on a laptop, point it at an
# existing build such as /tmp/m3k-build to skip that), M3K_PRESENT=gl|gl-basic|sdl, M3K_RENDER=cpu|gpu,
# M3K_THREADS=N. The game folder is the one the app saved before (~/.config/metroplex3k/gamedir.txt).
#
# To compare two builds: play the same city at the same zoom with the sim running and panning around for
# a minute with each of `./run.sh --installed` and `./run.sh`; the summary line printed on exit gives the mean FPS,
# the 99th percentile and the number of frames over 33 and 50 ms. The CSV has per-phase timings.
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

installed=0 log=1 build=1 fullscreen=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --installed) installed=1 ;;
    --no-log) log=0 ;;
    --no-build) build=0 ;;
    --fullscreen|-f) fullscreen=1 ;;
    --) shift; break ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) break ;;
  esac
  shift
done

if [[ $installed -eq 1 ]]; then
  bin=$(command -v metroplex3k || true)
  [[ -n "$bin" ]] || { echo "run.sh: no installed metroplex3k found" >&2; exit 1; }
else
  build_dir=${BUILD_DIR:-build-release}
  if [[ $build -eq 1 ]]; then
    if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
      cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$build_dir" --target metroplex3k -j "$(nproc)"
  fi
  bin=$build_dir/metroplex3k
  [[ -x "$bin" ]] || { echo "run.sh: $bin not built" >&2; exit 1; }
fi

args=()
if [[ $log -eq 1 ]]; then
  tag=$([[ $installed -eq 1 ]] && echo installed || echo new)
  args+=(--frame-log "frames-$tag-$(date +%H%M%S).csv")
fi

if [[ $fullscreen -eq 1 ]]; then
  if [[ $installed -eq 0 ]]; then
    args+=(--fullscreen)
  elif command -v hyprctl >/dev/null; then
    ( sleep 5; hyprctl dispatch focuswindow class:metroplex3k >/dev/null; hyprctl dispatch fullscreen 0 >/dev/null ) &
  else
    echo "run.sh: the installed build has no --fullscreen flag; press F11 in the game" >&2
  fi
fi

echo "run.sh: $("$bin" --version) from $bin" >&2
exec "$bin" "${args[@]}" "$@"
