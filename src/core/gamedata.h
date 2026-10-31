// Access to the user's installed game data: mounts the resource segments listed in Default.ini and
// provides decoded sprites, occupant attributes and composite-sprite records.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/image.h"
#include "core/ini.h"
#include "core/resdb.h"

namespace m3k {

// Resource type IDs (spec/03-resource-types.md).
constexpr uint32_t T_SPRITE_IMAGE = 0x00000000;
constexpr uint32_t T_SPRITE_SPAN = 0x00000001;
constexpr uint32_t T_OCC_ATTR = 0x207EDC0E;
constexpr uint32_t T_OCC_ATTR_PORT = 0x220055E1;
constexpr uint32_t T_NET_ATTR = 0xE223741F;
constexpr uint32_t T_FLORA_ATTR = 0xFFD30C03;
constexpr uint32_t T_CSATTRIB = 0x00006300;
constexpr uint32_t T_CSATTRIB_ANIM = 0x00006301;
constexpr uint32_t T_UI_IMAGE = 0x62B9DA24;
constexpr uint32_t T_STRING = 0x2026960B;

// Parsed TKB1 occupant attribute record.
struct OccAttr {
  bool valid = false;
  uint32_t agentType = 0;       // attr 0x65
  uint32_t sizeX = 1, sizeZ = 1, heightClass = 0;  // attr 0x66
  ResKey appearance;            // attr 0x67
  uint32_t flags = 0;           // attr 0x72
  uint32_t buildingFlags = 0;   // attr 0xCA
  int partIndex = 0;            // attr 0x1F4: puzzle-part index (airport/seaport parts); runway line number (spec 15 s5.5) [INFERRED]
  int powerAttr = -1;           // attr 0xC9 (u8): candidate for the building's power consumption (spec 12 s9.1); -1 = absent
  uint32_t netAttr12D = 0;      // attr 0x12D: network piece bits; bit 0 = step cost x 0.7 (traffic flag 0x100/0x200, spec 18 s3.1)
  uint32_t netMatrix[3] = {0, 0, 0};  // attrs 0x136/0x137/0x138: road/highway, rail, subway connection matrices (spec 18 s5.4)
  bool hasNetMatrix = false;    // attr 0x136 present
  bool filler() const { return (buildingFlags & 0x100) != 0; }
  ResKey nameKey;               // attr 0x6D
  int sparseId = 0;             // attr 0x6F: [Sparse] sound ID (key instance), 0 = none (spec 06 s10.4)
  std::string designName;       // attr 0x64
  ResKey pedProdKey, vehProdKey;  // attr 0x6B / 0x6C: pedestrian / vehicle production keys (automata sources)
  ResKey descKey;               // attr 0x7C: description string key 2026960B:62E69238:n (instance FFFFFFFF = none)
};

struct CsFrame { uint32_t category = 0, spriteId = 0; int8_t flags = 0; int16_t offX = 0, offY = 0; };

// Scripted overlay animation trailing a composite-sprite record (seen on SprInstCLSID 0x7315, the
// fusion plant): "Script[zoom,orient,offX,offY]:WR ..,S start count ms,G 1,END". The overlay plays
// frames[start .. start+count-1] at (offX, offY) from the object's anchor, `ms` per frame.
struct CsScript { int zoom = 0, orient = 0, offX = 0, offY = 0, start = 0, count = 0, ms = 0; };

// Composite-sprite ("BIN") record (spec/25-rendering.md 4.2).
struct CsAttrib {
  bool valid = false;
  uint32_t clsid = 0;
  int zoomCount = 0, rotCount = 0, zoomSetCount = 0, animSetCount = 0, animFrames = 0;
  std::vector<CsFrame> frames;
  std::vector<CsScript> scripts;
  const CsScript* script(int z, int o) const {
    for (const auto& s : scripts) if (s.zoom == z && s.orient == o) return &s;
    return nullptr;
  }
  // Frame for animation frame a, zoom slot z, orientation slot o.
  const CsFrame* frame(int a, int z, int o) const {
    size_t idx = size_t(a) * animSetCount + size_t(z) * zoomSetCount + o;
    return idx < frames.size() ? &frames[idx] : nullptr;
  }
};

struct SpriteImage {
  Image img;
  int L = 0, T = 0, R = 0, B = 0;  // span: registration point (L,T), size (L+R)x(T+B)
  bool ok = false;
  bool hasSpan = false;
};

class GameData {
 public:
  // `root` may be the game root (containing Apps/), the Apps directory itself, or any directory
  // from which one of those can be found. Returns false with `err` set if not a usable install.
  bool open(const fs::path& root, std::string& err);

  const fs::path& root() const { return root_; }
  const fs::path& appsDir() const { return apps_; }
  ResourceDb& db() { return db_; }
  const IniStore& ini() const { return ini_; }

  // Sprite image (colour or plane decoded lazily). Returns nullptr if missing/undecodable.
  const SpriteImage* sprite(uint32_t category, uint32_t id);
  const Plane* spritePlane(uint32_t category, uint32_t id);

  const OccAttr* occupant(uint32_t occupantId, bool network = false, bool port = false);
  const CsAttrib* csattrib(const ResKey& appearance);
  std::string string(uint32_t group, uint32_t instance);

  // Directories of the on-demand sprite sets.
  bool mountBuildingSet(uint32_t setId);
  bool mountFloraSet(uint32_t setId);
  void unmountSets();
  // Re-read Apps/BARender/BAT.DAT after it was rewritten (Building List Editor add / remove).
  void remountBat();

  std::string language = "English";
  // Requested UI language (name as in the spec, e.g. "French"): empty = choose from $LC_ALL / $LC_MESSAGES / $LANG, falling back to
  // English. Resolved by `open()` with spec 05 section 2 (an installed language is always chosen).
  std::string languageRequest;

 private:
  void mountSegments();
  void mountSpriteDats();
  fs::path root_, apps_;
  IniStore ini_;
  ResourceDb db_;
  std::unordered_map<uint64_t, std::unique_ptr<SpriteImage>> spriteCache_;
  std::unordered_map<uint64_t, std::unique_ptr<Plane>> planeCache_;
  std::unordered_map<uint32_t, OccAttr> occCache_;
  std::unordered_map<uint32_t, OccAttr> netCache_;
  std::unordered_map<uint32_t, OccAttr> portCache_;
  std::unordered_map<uint64_t, CsAttrib> csCache_;
};

// Parse helpers (exposed for tests).
OccAttr parseOccAttr(const uint8_t* p, size_t n);
CsAttrib parseCsAttrib(const uint8_t* p, size_t n);

}  // namespace m3k
