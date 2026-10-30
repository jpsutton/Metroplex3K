// EA "TQI" intra-frame video decoder (payload of `pIQT` chunks in TGQ movies, spec/07-video.md 4.4).
// Written from the public format description (MultimediaWiki "Electronic Arts TQI"): MPEG-1 style
// intra blocks. No macroblock headers; 16x16 macroblocks of 6 blocks (Y0 Y1 Y2 Y3 Cb Cr), each block
// = DC size VLC + differential DC, then MPEG-1 run/level AC VLCs until EOB, standard zigzag.
// The bit stream is read as 32-bit little-endian words, most significant bit first.
// Frame header (8 bytes): u16 width, u16 height, u8 quantiser, 3 bytes zero.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace m3k {

constexpr size_t kTqiHeaderSize = 8;
constexpr int kTqiMaxDim = 4096;

// One AC (run, level) VLC entry (MPEG-1 table B.14, sign bit follows the code). Entries
// [0, n) are real symbols; escape and end-of-block are listed separately.
struct TqiAcCode { uint16_t code; uint8_t len; uint8_t run; uint8_t level; };
const std::vector<TqiAcCode>& tqiAcCodes();
constexpr uint16_t kTqiEscCode = 0x1;  constexpr int kTqiEscLen = 6;   // escape, then 6b run + level
constexpr uint16_t kTqiEobCode = 0x2;  constexpr int kTqiEobLen = 2;   // end of block
// DC size VLC (size 0..11), luma (blocks 0-3) or chroma.
void tqiDcCode(bool chroma, int size, uint16_t& code, int& len);

extern const uint8_t kTqiZigzag[64];  // zigzag index -> raster index

// Quantiser scale for a frame quantiser byte: (215 - 2q) * 5, clamped to >= 5.
int tqiQScale(int quant);
// Dequantised coefficient at raster index i (AC), in units of 2x the orthonormal 8x8 DCT, so that
// pixel = IDCT(coef / 2). MPEG-1 style: (|l|*matrix*qscale >> 3), forced odd, sign restored.
int tqiDequantAc(int level, int rasterIndex, int quant);
// Dequantised DC from a DC level (same units): 64 per level, i.e. pixel step 4.
inline int tqiDequantDc(int level) { return level * 64; }

// Decode one pIQT payload (starting at the width/height header) to 0xAARRGGBB pixels, size
// width*height from the header (w/h arguments are only a hint and ignored if they disagree).
// Returns true when the whole frame decoded. On a damaged/truncated stream returns false but
// `argb` still holds the partially decoded picture (undecoded macroblocks mid-grey), unless the
// header itself is invalid (then argb is empty). Never reads out of bounds; bounded work.
bool decodeTqiFrame(const uint8_t* payload, size_t n, int w, int h, std::vector<uint32_t>& argb);

// Same, but exposes planar 4:2:0 output (Y w*h, Cb/Cr ((w+1)/2)*((h+1)/2)).
struct TqiPlanes { int w = 0, h = 0; std::vector<uint8_t> y, cb, cr; };
bool decodeTqiPlanes(const uint8_t* payload, size_t n, TqiPlanes& out);

// YCbCr (BT.601, limited range) -> 0xFFRRGGBB.
uint32_t tqiYuvToArgb(int y, int cb, int cr);

}  // namespace m3k
