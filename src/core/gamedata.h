// Access to the user's installed game data: mounts the resource segments listed in Default.ini and
// provides decoded sprites, occupant attributes and composite-sprite records.
#pragma once
#include <map>
#include <memory>
#include <mutex>
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

// Frame of a multi-frame strip image (effect and disaster archives, spec 04 section 2.3: frame width = L + R, frames side by side).
// sprite(cat, id) and spritePlane(cat, id) with a category from fxFrameCat() return the cropped frame of the group-0 image `id`
// (colour, or its opacity plane, which has the same strip geometry), so the renderer draws single frames without strip support.
constexpr uint32_t kFxFrameCatTop = 0xFD000000u;
inline uint32_t fxFrameCat(int frameWidth, int frame) { return kFxFrameCatTop | (uint32_t(frameWidth) & 0xFFFu) << 12 | (uint32_t(frame) & 0xFFFu); }
inline bool isFxFrameCat(uint32_t cat) { return (cat & 0xFF000000u) == kFxFrameCatTop; }
// Same for an opacity plane that has no colour image (the UFO beam): sprite(cat, planeId) returns the frame as a flat pale-blue image that is
// opaque wherever the plane is non-zero, to be drawn together with the plane frame fxFrameCat(fw, frame).
constexpr uint32_t kFxTintBeamTop = 0xFB000000u;
inline uint32_t fxTintBeamCat(int frameWidth, int frame) { return kFxTintBeamTop | (uint32_t(frameWidth) & 0xFFFu) << 12 | (uint32_t(frame) & 0xFFFu); }
inline bool isFxTintBeamCat(uint32_t cat) { return (cat & 0xFF000000u) == kFxTintBeamTop; }

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

// One compiled entry of a sprite script (spec/25-rendering.md section 7.1). S and P expand to Draw + Wait pairs, so G n,
// R n and WD n count compiled entries, not text tokens.
struct ScriptOp {
  enum Kind : uint8_t { Draw, DrawAlpha, Wait, WaitRandom, Goto, RandomSkip, Label, DateWindow, GotoWindowTarget };
  Kind kind = Wait;
  int a = 0, b = 0;            // Draw/DrawAlpha: frame, -; Wait: ms; WaitRandom: base, range; Goto: index; RandomSkip: n; Label: id
  int dx = 0, dy = 0;          // Draw/DrawAlpha pixel offset (added to the script offset)
  bool hasOffset = false;      // false: keep the previous D/DA offset of the script
  int startMD = 0, endMD = 0;  // DateWindow: month*100+day, end day exclusive
  int target = 0;              // DateWindow: label id (WL) or absolute index (WD)
  bool absTarget = false;
};

// A sprite script: "Script[zoom,orient,offX,offY]:op,op,...,END". Every script of a (zoom, orient) slot runs at once.
struct CsScript {
  int zoom = 0, orient = 0, offX = 0, offY = 0;
  std::vector<ScriptOp> ops;
};

// Child sprite attached to a parent record ("DSprites" block): another sprite-instance record (6301:6400:child) drawn
// at the parent's world position + (dx, dy) in 1/256 tile and dz in world-height units, for one (zoom, orient) slot.
struct CsChild { int zoom = 0, orient = 0; uint32_t child = 0; int dx = 0, dy = 0, dz = 0; };

// Composite-sprite ("BIN") record (spec/25-rendering.md 4.2).
struct CsAttrib {
  bool valid = false;
  uint32_t clsid = 0;
  int zoomCount = 0, rotCount = 0, zoomSetCount = 0, animSetCount = 0, animFrames = 0;
  std::vector<CsFrame> frames;
  std::vector<CsScript> scripts;
  std::vector<CsChild> children;
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

// A mutex that can live in a movable class: moving yields a fresh unlocked mutex (the lock guards cache contents,
// which are moved along with it only when no other thread can be using them).
struct CacheMutex {
  std::mutex m;
  CacheMutex() = default;
  CacheMutex(CacheMutex&&) noexcept {}
  CacheMutex& operator=(CacheMutex&&) noexcept { return *this; }
  void lock() { m.lock(); }
  void unlock() { m.unlock(); }
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
  CacheMutex occMu_;   // guards the three occupant attribute caches: the sim worker and the renderer both fill them lazily
  std::unordered_map<uint32_t, OccAttr> occCache_;
  std::unordered_map<uint32_t, OccAttr> netCache_;
  std::unordered_map<uint32_t, OccAttr> portCache_;
  std::unordered_map<uint64_t, CsAttrib> csCache_;
};

// Parse helpers (exposed for tests).
OccAttr parseOccAttr(const uint8_t* p, size_t n);
CsAttrib parseCsAttrib(const uint8_t* p, size_t n);

}  // namespace m3k
