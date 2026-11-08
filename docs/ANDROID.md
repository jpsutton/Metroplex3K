# Android port notes (not started)

The port is possible because `src/core` has no OS dependency. Plan:

- Build `m3k_core` with the NDK (CMake toolchain file, `-DANDROID_ABI=arm64-v8a`, `-DM3K_BUILD_APP=OFF`) and write a
  new host (SDL2 for Android is the shortest path; reuse `src/app/main.cpp` logic with SDL's Android activity).
- Game files: the user copies the installed game folder to device storage, or picks it with the Storage Access
  Framework. Core needs a plain filesystem path, so copy or mount the picked tree into app-private storage first.
  `extractGogInstaller` is compiled out on Android.
- Input mapping to `Input`: one-finger tap = left click, one-finger drag with a tool armed = tool drag, one-finger drag
  with no tool = pan (feed as held arrows or add a pan field), two-finger pinch = zoom (`Input::wheel`), two-finger
  drag = pan, long press = right click (cancel tool).
- Audio: SDL audio callback calls `AudioSystem::mixAudio` (s16 stereo).
- Memory: sprite DATs are read whole into RAM at mount time. On mobile switch `IxfFile` to a lazy-read or mmap backend.
- Resolution: UI is drawn at window size; add a UI scale factor before shipping.
