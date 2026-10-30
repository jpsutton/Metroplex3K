// Intro movie player (spec/07-video.md): turns a parsed TgqMovie into timed frames (15 fps) and
// PCM audio chunks, and composes frames into a window. No I/O, no SDL; the app supplies the clock.
#pragma once
#include <cstdint>
#include <vector>
#include "core/video.h"

namespace m3k {

enum class IntroState { NotStarted, Playing, Finished };

class IntroPlayer {
 public:
  // The movie must outlive the player.
  explicit IntroPlayer(const TgqMovie& movie) : movie_(movie) {}

  // NotStarted -> Playing. Finished at once (like Escape) when the movie has no decodable video.
  void start();
  // Escape / skip: Playing -> Finished.
  void skip() { state_ = IntroState::Finished; }
  IntroState state() const { return state_; }

  // Advance the clock to `ms` since start(). Finishes the intro once ms >= movie duration.
  // Returns state after the update.
  IntroState update(int64_t ms);

  // Frame to show at `ms` (0xAARRGGBB, 320x480 as coded), decoded on demand; the last good picture is
  // repeated when a frame is damaged. Returns false if no picture exists yet. `index` (optional)
  // receives the frame number. Pointer valid until the next call.
  const std::vector<uint32_t>* frameAt(int64_t ms, int* index = nullptr);
  int frameWidth() const { return w_; }
  int frameHeight() const { return h_; }
  int damagedFrames() const { return damaged_; }

  // Append all audio due by `ms` (chunks are released in order, a chunk once its first sample time
  // has been reached) to `out`, interleaved s16 scaled by `volume` (0..1). Returns sample frames added.
  size_t pullAudio(int64_t ms, float volume, std::vector<int16_t>& out);
  int audioChannels() const { return movie_.audio().channels == 1 ? 1 : 2; }
  int audioRate() const { return movie_.audio().sampleRate; }

 private:
  const TgqMovie& movie_;
  IntroState state_ = IntroState::NotStarted;
  std::vector<uint32_t> cur_;
  int curIndex_ = -1, w_ = 0, h_ = 0, damaged_ = 0;
  size_t nextAudio_ = 0;
  uint64_t audioFramesDone_ = 0;
};

// Compose a frame into a window buffer (winW x winH, 0xAARRGGBB, black background): the coded
// frame is stretched horizontally x2 (spec 3: 320x480 -> 640x480) and drawn in the centred
// introRect, clipped to the window when the window is smaller.
std::vector<uint32_t> composeIntroWindow(const std::vector<uint32_t>& frame, int w, int h, int winW, int winH);

}  // namespace m3k
