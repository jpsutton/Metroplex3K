// FBF bitmap fonts and text drawing. See spec/05-text-and-fonts.md section 4.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/fsutil.h"
#include "core/image.h"

namespace m3k {

struct Glyph { int l = 0, t = 0, r = 0, b = 0, adv = 0; };

class Font {
 public:
  bool load(const uint8_t* p, size_t n);
  bool loaded() const { return w_ > 0; }
  // Build the built-in fallback font at integer scale (1 = 5x7 in a 6x8 cell).
  void makeBuiltin(int scale);
  int height() const { return glyphH_; }
  int measure(const std::string& s) const;
  // Draw text with top-left at (x,y) into an RGBA framebuffer. Mono/AA fonts use `colour`.
  void draw(std::vector<uint32_t>& fb, int fbW, int fbH, int x, int y, const std::string& s, uint32_t colour) const;
  bool multicolour() const { return multi_; }
 private:
  int w_ = 0, h_ = 0, glyphH_ = 0;
  bool multi_ = false;
  std::vector<uint8_t> atlas_;
  uint16_t pal_[256] = {};
  Glyph g_[256];
};

// Strings: Windows-1252 bytes, converted to UTF-8 for hosting if needed. Token expansion per
// spec 05 section 3.3 and printf-lite.
struct TextContext {
  std::string mayor = "Mayor", city = "City";
  int population = 0, year = 2000;
  std::string neighbour = "Neighbour";
};
std::string expandTokens(const std::string& s, const TextContext& c);
std::string groupDigits(long long v);  // 1,234,567

// Font registry: loads the FBF files of the current language folder from Res/Text/<lang>/.
class Fonts {
 public:
  void load(const fs::path& textDir);
  const Font& get(const std::string& name) const;  // "system9","main9","main10","title9","title12","serif17"
  const Font& ui() const { return get("main10"); }
 private:
  std::map<std::string, Font> f_;
  mutable Font builtin_[2];
};

}  // namespace m3k
