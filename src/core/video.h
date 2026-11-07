// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// INTRO.TGQ ("SCxl" EA chunk stream) container parser, movie audio decoder, playback timing.
// spec/07-video.md. The TQI video bitstream is NOT defined by the spec (section 4.4, OPEN), so
// frames are exposed as raw payloads; a TqiDecoder callback can be plugged in. By default the
// built-in decoder in tqi.h (written from the public EA TQI description) is used.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "core/xa_codec.h"

namespace m3k {

struct TgqAudioInfo {
  int channels = 2;
  int compression = 0;        // 7 = EA ADPCM
  int sampleRate = 22050;     // default when tag 0x84 absent
  uint32_t totalFrames = 0;   // tag 0x85
  bool present = false;
};

struct TgqVideoFrame {
  size_t offset = 0, size = 0;  // payload (after 8-byte chunk header) within the file image
  int width = 0, height = 0;    // from payload header
  uint8_t quant0 = 0, quant1 = 0;  // the "quantiser/scale byte pair"
  size_t dataOffset = 0;        // compressed frame data start (payload + 8-byte TQI header)
  uint32_t audioChunksBefore = 0;  // SCDl chunks seen before this frame (interleave position)
};

struct TgqAudioChunk { size_t offset = 0, size = 0; };

// Plugged-in TQI decoder: decode one pIQT payload (starting at width/height) to 0xAARRGGBB pixels.
using TqiDecoder = std::function<bool(const uint8_t* payload, size_t n, int w, int h, std::vector<uint32_t>& argb)>;

class TgqMovie {
 public:
  // Parse the chunk list. Takes the file image. False when no valid chunk stream.
  bool parse(std::vector<uint8_t> data);

  const TgqAudioInfo& audio() const { return audio_; }
  uint32_t declaredAudioChunks() const { return sccl_; }  // SCCl count
  bool sawEnd() const { return sawEnd_; }
  const std::vector<TgqVideoFrame>& frames() const { return frames_; }
  const std::vector<TgqAudioChunk>& audioChunks() const { return audioChunks_; }
  const uint8_t* payload(const TgqVideoFrame& f) const { return data_.data() + f.offset; }

  // Decode all SCDl chunks to one PCM buffer (EA ADPCM, rounding up + saturating, spec 4.3).
  // False if compression is not 7 or a chunk is truncated (decoded part is kept).
  bool decodeAudio(PcmBuffer& out) const;

  // Decode SCDl chunk i, appending interleaved s16 to `out`. False if truncated/out of range.
  bool decodeAudioChunk(size_t i, std::vector<int16_t>& out) const;

  // Decode frame i using a supplied decoder.
  bool decodeFrame(size_t i, const TqiDecoder& dec, std::vector<uint32_t>& argb) const;
  // Decode frame i with the built-in TQI decoder. False if damaged (argb may hold a partial picture).
  bool decodeFrame(size_t i, std::vector<uint32_t>& argb) const;
  const uint8_t* audioPayload(const TgqAudioChunk& c) const { return data_.data() + c.offset; }

  // Frame to show at `ms` after playback start (15 fps, clamped to last). -1 if no frames.
  int frameAtMs(int64_t ms) const;
  // Playback duration: max(video, audio) in ms.
  int64_t durationMs() const;

 private:
  std::vector<uint8_t> data_;
  TgqAudioInfo audio_;
  uint32_t sccl_ = 0;
  bool sawEnd_ = false;
  std::vector<TgqVideoFrame> frames_;
  std::vector<TgqAudioChunk> audioChunks_;
};

constexpr int kTgqFps = 15;

// The built-in TQI decoder as a TqiDecoder.
TqiDecoder defaultTqiDecoder();

// Whether the intro plays (spec 1): no file to open on the command line, intro option != "off"
// (case-insensitive), and the movie exists and is openable.
bool shouldPlayIntro(bool hasCommandLineFile, const std::string& introOption, bool movieAvailable);

// Movie volume from the user's music volume 0..1024: <=128 -> 0 (silent), else value/1024 linear.
float introVolume(int musicVolume);

struct IntroRect { int x, y, w, h; };
// 640x480 centred rect (spec 3).
IntroRect introRect(int screenW, int screenH);

// Pixel-doubling horizontal stretch of a 320x480 frame to 640x480.
std::vector<uint32_t> stretchFrame2x(const std::vector<uint32_t>& argb, int w, int h);

}  // namespace m3k
