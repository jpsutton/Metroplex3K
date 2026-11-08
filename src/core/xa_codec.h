// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Maxis XA (EA-XA ADPCM) decoder, WAV loader, EA ADPCM movie-chunk decoder. See spec/06-audio.md
// sections 2-3 and spec/07-video.md section 4.3. Platform independent; no I/O.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace m3k {

// Decoded PCM: interleaved signed 16-bit samples.
struct PcmBuffer {
  int channels = 1;
  int sampleRate = 22050;
  std::vector<int16_t> samples;
  size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};

struct XaHeader {
  uint32_t outSize = 0;  // decoded bytes
  uint16_t formatTag = 0, channels = 0;
  uint32_t sampleRate = 0, avgBytesPerSec = 0;
  uint16_t blockAlign = 0, bitsPerSample = 0;
};

constexpr size_t kXaHeaderSize = 24;
constexpr int kXaFramesPerBlock = 28;

// Parses the 24-byte header. False if too short, bad magic ("XA") or formatTag != 1.
bool parseXaHeader(const uint8_t* p, size_t n, XaHeader& h);

// Per-channel predictor state (history carried across blocks).
struct XaChannelState { int64_t h1 = 0, h2 = 0; };

// Decode one 4-bit code. `tiesEven`: bit-exact original rounding; false = ties up (+inf).
// Returns the full integer y (history value); caller truncates to s16 for output.
int64_t xaDecodeSample(XaChannelState& st, int predictor, int shift, int code, bool tiesEven = true);

// Decode a whole .XA file. Channels other than 1/2 give silence (outSize bytes of zeros, as the
// original). Truncated data is zero-filled to outSize. Returns false on a bad header.
bool decodeXa(const uint8_t* p, size_t n, PcmBuffer& out, bool tiesEven = true);
inline bool decodeXa(const std::vector<uint8_t>& v, PcmBuffer& out, bool tiesEven = true) {
  return decodeXa(v.data(), v.size(), out, tiesEven);
}

// EA ADPCM chunk used by the movie (SCDl payload): u32 N, per channel (s16 cur, s16 prev), then
// ceil(N/28) groups of (pred byte, shift byte, 28 code bytes). Stereo layout; for channels==1 the
// left nibbles are used. Rounds ties up and saturates. Appends N interleaved frames to `out`.
// Returns false if the chunk is truncated (whatever decoded is still appended).
bool decodeEaAdpcmChunk(const uint8_t* p, size_t n, int channels, std::vector<int16_t>& out);

// RIFF/WAVE PCM loader (8-bit unsigned or 16-bit signed, 1-2 channels). 8-bit is widened to s16.
bool loadWav(const uint8_t* p, size_t n, PcmBuffer& out);
inline bool loadWav(const std::vector<uint8_t>& v, PcmBuffer& out) { return loadWav(v.data(), v.size(), out); }

// Decoder chosen by file extension (.xa / .wav, case-insensitive).
bool loadSoundFile(const std::string& name, const std::vector<uint8_t>& data, PcmBuffer& out);

}  // namespace m3k
