// Decoded image types and sprite/UI image decoders. See spec/04-graphics-formats.md.
#pragma once
#include <cstdint>
#include <vector>
#include "core/bytes.h"

namespace m3k {

// RGBA8, 0xAABBGGRR in memory order R,G,B,A (little-endian u32 = A<<24|B<<16|G<<8|R).
struct Image {
  int w = 0, h = 0;
  std::vector<uint32_t> px;  // w*h
  bool empty() const { return w <= 0 || h <= 0; }
};

// 8-bit plane (opacity 0..31 for sprite alpha planes).
struct Plane {
  int w = 0, h = 0;
  std::vector<uint8_t> v;
};

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

// Expand a 16-bit pixel (format 5 = RGB555, 7 = RGB565) to RGBA8 by bit replication.
uint32_t expand16(uint16_t p, int pixelFormat);

struct DecodedSprite {
  bool isPlane = false;  // true: `plane` holds 5-bit opacity levels (format 3)
  Image image;           // colour sprite (key colour -> alpha 0)
  Plane plane;
};

// Decode a Type-0 sprite payload. `size` is the number of bytes available. Throws FormatError.
DecodedSprite decodeSprite(const uint8_t* p, size_t size);

// Decode a Type 0x62B9DA24 UI image (BMP / raw / compressed). Magenta (0xF81F) is made transparent
// when `keyMagenta` is set. Throws FormatError.
Image decodeUiImage(const uint8_t* p, size_t size, bool keyMagenta = true);

// Decode a Windows BMP (24-bit or 8-bit, uncompressed). Optional magenta key.
Image decodeBmp(const uint8_t* p, size_t size, bool keyMagenta = false);

}  // namespace m3k
