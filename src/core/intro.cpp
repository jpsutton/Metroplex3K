#include "core/intro.h"
#include <algorithm>
#include <cmath>

namespace m3k {

void IntroPlayer::start() {
  if (state_ != IntroState::NotStarted) return;
  state_ = movie_.frames().empty() ? IntroState::Finished : IntroState::Playing;
}

IntroState IntroPlayer::update(int64_t ms) {
  if (state_ == IntroState::Playing && ms >= movie_.durationMs()) state_ = IntroState::Finished;
  return state_;
}

const std::vector<uint32_t>* IntroPlayer::frameAt(int64_t ms, int* index) {
  int i = movie_.frameAtMs(ms);
  if (i < 0) return nullptr;
  if (i != curIndex_) {
    std::vector<uint32_t> px;
    bool ok = movie_.decodeFrame(size_t(i), px);
    const auto& f = movie_.frames()[size_t(i)];
    if (!ok) damaged_++;
    // Use a (partial) picture when the header was valid; otherwise keep the previous picture.
    if (!px.empty()) {
      if (ok || cur_.empty() || px.size() != cur_.size()) { cur_ = std::move(px); w_ = f.width; h_ = f.height; }
      // else damaged: keep the previous good picture
    }
    curIndex_ = i;
  }
  if (index) *index = curIndex_;
  return cur_.empty() ? nullptr : &cur_;
}

size_t IntroPlayer::pullAudio(int64_t ms, float volume, std::vector<int16_t>& out) {
  size_t added = 0;
  int rate = std::max(1, audioRate());
  const auto& chunks = movie_.audioChunks();
  while (nextAudio_ < chunks.size()) {
    int64_t startMs = int64_t(audioFramesDone_ * 1000 / uint64_t(rate));
    if (startMs > ms) break;
    std::vector<int16_t> pcm;
    movie_.decodeAudioChunk(nextAudio_, pcm);
    for (auto& s : pcm) s = int16_t(std::lround(std::clamp(float(s) * volume, -32768.f, 32767.f)));
    size_t frames = pcm.size() / size_t(audioChannels());
    out.insert(out.end(), pcm.begin(), pcm.end());
    audioFramesDone_ += frames;
    added += frames;
    nextAudio_++;
  }
  return added;
}

std::vector<uint32_t> composeIntroWindow(const std::vector<uint32_t>& frame, int w, int h, int winW, int winH) {
  std::vector<uint32_t> out(size_t(std::max(0, winW)) * size_t(std::max(0, winH)), 0xFF000000u);
  if (w <= 0 || h <= 0 || winW <= 0 || winH <= 0 || frame.size() < size_t(w) * h) return out;
  IntroRect r = introRect(winW, winH);
  // Scale the coded frame to 640x480 (horizontal x2 for 320 wide, nearest for any other size).
  for (int dy = 0; dy < r.h; dy++) {
    int wy = r.y + dy;
    if (wy < 0 || wy >= winH) continue;
    int sy = std::min(h - 1, dy * h / r.h);
    for (int dx = 0; dx < r.w; dx++) {
      int wx = r.x + dx;
      if (wx < 0 || wx >= winW) continue;
      int sx = std::min(w - 1, dx * w / r.w);
      out[size_t(wy) * winW + wx] = frame[size_t(sy) * w + sx];
    }
  }
  return out;
}

}  // namespace m3k
