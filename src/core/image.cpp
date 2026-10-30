#include "core/image.h"

#include "core/refpack.h"

namespace m3k {

uint32_t expand16(uint16_t p, int fmt) {
  uint32_t r, g, b;
  if (fmt == 5 || fmt == 10) {
    r = (p >> 10) & 31; g = (p >> 5) & 31; b = p & 31;
    return rgba(uint8_t((r << 3) | (r >> 2)), uint8_t((g << 3) | (g >> 2)), uint8_t((b << 3) | (b >> 2)));
  }
  r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31;
  return rgba(uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)));
}

DecodedSprite decodeSprite(const uint8_t* p, size_t size) {
  if (size < 0x14) throw FormatError("sprite: payload too short");
  uint8_t fmt = p[0], enc = p[1];
  uint32_t flags = rd32(p + 4), w = rd32(p + 8), h = rd32(p + 12);
  if (w > 8192 || h > 8192) throw FormatError("sprite: implausible size");
  if (flags & 0x8000) throw FormatError("sprite: unsupported codec");
  if (!(flags & 0x00080000)) throw FormatError("sprite: uncompressed encodings not supported");
  Bytes body = refpackDecode(p + 0x14, size - 0x14);

  DecodedSprite out;
  if (enc == 0) {
    if (!(flags & 0x10000000) || fmt != 3) throw FormatError("sprite: unsupported bitmap encoding");
    if (body.size() < size_t(w) * h) throw FormatError("sprite: plane too short");
    out.isPlane = true;
    out.plane.w = int(w); out.plane.h = int(h);
    out.plane.v.assign(body.begin(), body.begin() + size_t(w) * h);
    return out;
  }
  if (enc != 1) throw FormatError("sprite: unknown encoding");
  if (body.size() < 0x10) throw FormatError("sprite: rle too short");
  uint16_t rw = rd16(&body[4]), rh = rd16(&body[6]);
  uint16_t sfmt = rd16(&body[0x0A]), key = rd16(&body[0x0C]);
  if (rw != w || rh != h) throw FormatError("sprite: size mismatch");
  size_t tableEnd = 0x10 + size_t(rh) * 8;
  if (body.size() < tableEnd) throw FormatError("sprite: row table truncated");
  size_t npix = (body.size() - tableEnd) / 2;
  out.image.w = rw; out.image.h = rh;
  out.image.px.assign(size_t(rw) * rh, 0);
  for (int y = 0; y < rh; y++) {
    const uint8_t* row = &body[0x10 + size_t(y) * 8];
    uint32_t pi = rd32(row);
    uint16_t skip = rd16(row + 4), cf = rd16(row + 6);
    uint32_t n = cf & 0x7FFF;
    if (pi + n > npix || skip + n > rw) throw FormatError("sprite: row out of range");
    for (uint32_t i = 0; i < n; i++) {
      uint16_t pv = rd16(&body[tableEnd + (size_t(pi) + i) * 2]);
      if (pv == key) continue;
      out.image.px[size_t(y) * rw + skip + i] = expand16(pv, sfmt);
    }
  }
  return out;
}

Image decodeBmp(const uint8_t* p, size_t size, bool keyMagenta) {
  if (size < 54 || p[0] != 'B' || p[1] != 'M') throw FormatError("bmp: bad header");
  uint32_t dataOff = rd32(p + 10), hdr = rd32(p + 14);
  int32_t w = int32_t(rd32(p + 18)), h = int32_t(rd32(p + 22));
  uint16_t bpp = rd16(p + 28);
  uint32_t comp = rd32(p + 30);
  if (hdr < 40 || comp != 0 || (bpp != 24 && bpp != 8 && bpp != 4)) throw FormatError("bmp: unsupported");
  bool topDown = h < 0;
  if (topDown) h = -h;
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) throw FormatError("bmp: bad size");
  std::vector<uint32_t> pal;
  if (bpp <= 8) {
    uint32_t n = 1u << bpp;
    size_t palOff = 14 + hdr;
    if (palOff + n * 4 > size) throw FormatError("bmp: palette truncated");
    for (uint32_t i = 0; i < n; i++) {
      const uint8_t* e = p + palOff + i * 4;
      pal.push_back(rgba(e[2], e[1], e[0]));
    }
  }
  size_t stride = ((size_t(w) * bpp + 31) / 32) * 4;
  if (dataOff + stride * h > size) throw FormatError("bmp: pixel data truncated");
  Image img; img.w = w; img.h = h; img.px.resize(size_t(w) * h);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = p + dataOff + stride * (topDown ? y : (h - 1 - y));
    for (int x = 0; x < w; x++) {
      uint32_t c;
      if (bpp == 24) c = rgba(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
      else if (bpp == 8) c = pal[row[x]];
      else c = pal[(x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4)];
      if (keyMagenta && (c & 0xFFFFFF) == 0xFF00FF) c = 0;
      img.px[size_t(y) * w + x] = c;
    }
  }
  return img;
}

Image decodeUiImage(const uint8_t* p, size_t size, bool keyMagenta) {
  if (size < 8) throw FormatError("ui image: too short");
  if (p[0] == 'B' && p[1] == 'M') return decodeBmp(p, size, keyMagenta);
  uint32_t kind = rd32(p);
  if (kind == 0x62B9DA24) {
    uint32_t w = rd32(p + 4), h = rd32(p + 8);
    if (w > 8192 || h > 8192 || size < 12 + size_t(w) * h * 4) throw FormatError("ui image: bad true-colour");
    Image img; img.w = int(w); img.h = int(h); img.px.resize(size_t(w) * h);
    for (size_t i = 0; i < img.px.size(); i++) {
      uint32_t v = rd32(p + 12 + i * 4);  // assumed 0x00RRGGBB
      img.px[i] = rgba(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
    }
    return img;
  }
  if (kind != 1 && kind != 2) throw FormatError("ui image: unknown kind");
  if (size < 0x14) throw FormatError("ui image: header truncated");
  uint32_t w = rd32(p + 8), h = rd32(p + 12);
  int32_t fmt = int32_t(rd32(p + 16));
  if (w > 8192 || h > 8192) throw FormatError("ui image: implausible size");
  int bpp = (fmt == 3) ? 1 : (fmt >= 4 && fmt <= 12) ? 2 : (fmt == 13 || fmt == 14) ? 4 : 0;
  if (!bpp) throw FormatError("ui image: bad pixel format");
  Bytes raw;
  if (kind == 1) {
    size_t need = size_t(w) * h * bpp;
    if (size < 0x14 + need) throw FormatError("ui image: raw truncated");
    raw.assign(p + 0x14, p + 0x14 + need);
  } else {
    if (size < 0x18) throw FormatError("ui image: compressed truncated");
    raw = refpackDecode(p + 0x18, size - 0x18);
    if (raw.size() < size_t(w) * h * bpp) throw FormatError("ui image: decompressed too short");
  }
  Image img; img.w = int(w); img.h = int(h); img.px.resize(size_t(w) * h);
  for (size_t i = 0; i < img.px.size(); i++) {
    if (bpp == 2) {
      uint16_t v = rd16(&raw[i * 2]);
      img.px[i] = (keyMagenta && v == 0xF81F && fmt == 7) ? 0 : expand16(v, fmt);
    } else if (bpp == 1) {
      uint8_t g = raw[i];
      img.px[i] = rgba(g, g, g);
    } else {
      uint32_t v = rd32(&raw[i * 4]);
      img.px[i] = rgba(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
    }
  }
  return img;
}

}  // namespace m3k
