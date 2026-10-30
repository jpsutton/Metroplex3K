#include "core/video.h"
#include <algorithm>
#include "core/bytes.h"
#include "core/fsutil.h"
#include "core/tqi.h"

namespace m3k {

namespace {
bool tagIs(const uint8_t* p, const char* t) { return p[0] == uint8_t(t[0]) && p[1] == uint8_t(t[1]) && p[2] == uint8_t(t[2]) && p[3] == uint8_t(t[3]); }

void parseSchl(const uint8_t* p, size_t n, TgqAudioInfo& a) {
  if (n < 4 || p[0] != 'P' || p[1] != 'T') return;
  size_t pos = 4;  // PT + platform u16
  while (pos < n) {
    uint8_t tag = p[pos++];
    if (tag == 0xFF) break;
    if (pos >= n) break;
    size_t L = p[pos++];
    if (pos + L > n) break;
    uint32_t v = 0;
    for (size_t i = 0; i < L; i++) v = (v << 8) | p[pos + i];
    pos += L;
    switch (tag) {
      case 0x85: a.totalFrames = v; a.present = true; break;
      case 0x82: a.channels = int(v); a.present = true; break;
      case 0x83: a.compression = int(v); a.present = true; break;
      case 0x84: a.sampleRate = int(v); break;
      default: break;
    }
  }
}
}  // namespace

bool TgqMovie::parse(std::vector<uint8_t> data) {
  data_ = std::move(data);
  audio_ = TgqAudioInfo();
  frames_.clear();
  audioChunks_.clear();
  sccl_ = 0;
  sawEnd_ = false;
  size_t pos = 0, n = data_.size();
  bool sawHeader = false;
  uint32_t audioSeen = 0;
  while (pos + 8 <= n) {
    const uint8_t* c = data_.data() + pos;
    uint32_t size = rd32(c + 4);
    if (size < 8 || pos + size > n) break;
    const uint8_t* body = c + 8;
    size_t bn = size - 8;
    if (tagIs(c, "SCHl")) { parseSchl(body, bn, audio_); sawHeader = true; }
    else if (tagIs(c, "SCCl")) { if (bn >= 4) sccl_ = rd32(body); }
    else if (tagIs(c, "pIQT")) {
      if (bn >= 6) {
        TgqVideoFrame f;
        f.offset = pos + 8; f.size = bn;
        f.width = rd16(body); f.height = rd16(body + 2);
        f.quant0 = body[4]; f.quant1 = body[5];
        f.dataOffset = f.offset + kTqiHeaderSize;
        f.audioChunksBefore = audioSeen;
        frames_.push_back(f);
      }
    } else if (tagIs(c, "SCDl")) { audioChunks_.push_back({pos + 8, bn}); audioSeen++; }
    else if (tagIs(c, "SCEl")) { sawEnd_ = true; pos += size; break; }
    pos += size;
  }
  return sawHeader && (!frames_.empty() || !audioChunks_.empty());
}

bool TgqMovie::decodeAudio(PcmBuffer& out) const {
  out.channels = audio_.channels == 1 ? 1 : 2;
  out.sampleRate = audio_.sampleRate;
  out.samples.clear();
  if (audio_.compression != 7) return false;
  bool ok = true;
  for (auto& c : audioChunks_)
    ok &= decodeEaAdpcmChunk(data_.data() + c.offset, c.size, out.channels, out.samples);
  return ok;
}

bool TgqMovie::decodeAudioChunk(size_t i, std::vector<int16_t>& out) const {
  if (i >= audioChunks_.size() || audio_.compression != 7) return false;
  return decodeEaAdpcmChunk(data_.data() + audioChunks_[i].offset, audioChunks_[i].size,
                            audio_.channels == 1 ? 1 : 2, out);
}

bool TgqMovie::decodeFrame(size_t i, const TqiDecoder& dec, std::vector<uint32_t>& argb) const {
  if (i >= frames_.size() || !dec) return false;
  const auto& f = frames_[i];
  return dec(data_.data() + f.offset, f.size, f.width, f.height, argb);
}

bool TgqMovie::decodeFrame(size_t i, std::vector<uint32_t>& argb) const {
  return decodeFrame(i, defaultTqiDecoder(), argb);
}

TqiDecoder defaultTqiDecoder() { return decodeTqiFrame; }

int TgqMovie::frameAtMs(int64_t ms) const {
  if (frames_.empty()) return -1;
  if (ms < 0) ms = 0;
  int64_t i = ms * kTgqFps / 1000;
  return int(std::min<int64_t>(i, int64_t(frames_.size()) - 1));
}

int64_t TgqMovie::durationMs() const {
  int64_t v = int64_t(frames_.size()) * 1000 / kTgqFps;
  int64_t a = 0;
  if (audio_.sampleRate > 0) {
    uint64_t tot = audio_.totalFrames;
    if (!tot) for (auto& c : audioChunks_) if (c.size >= 4) tot += rd32(data_.data() + c.offset);
    a = int64_t(tot * 1000 / uint64_t(audio_.sampleRate));
  }
  return std::max(v, a);
}

bool shouldPlayIntro(bool hasCommandLineFile, const std::string& introOption, bool movieAvailable) {
  if (hasCommandLineFile) return false;
  if (iequals(introOption, "off")) return false;
  return movieAvailable;
}

float introVolume(int musicVolume) {
  if (musicVolume <= 128) return 0.f;
  return float(std::min(musicVolume, 1024)) / 1024.f;
}

IntroRect introRect(int screenW, int screenH) { return {(screenW - 640) / 2, (screenH - 480) / 2, 640, 480}; }

std::vector<uint32_t> stretchFrame2x(const std::vector<uint32_t>& argb, int w, int h) {
  std::vector<uint32_t> out(size_t(w) * 2 * h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint32_t v = argb[size_t(y) * w + x];
      out[size_t(y) * w * 2 + x * 2] = v;
      out[size_t(y) * w * 2 + x * 2 + 1] = v;
    }
  return out;
}

}  // namespace m3k
