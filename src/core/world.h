// In-memory city: terrain, flora, zones, occupants and network pieces. Coordinates follow
// spec/00-README.md: tile (x, z), y = altitude. Per-vertex grids are (W+1)x(H+1), index x*(H+1)+z.
// Per-tile grids are WxH, index x*H+z. (Files store rows last-first; the loader converts.)
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/image.h"
#include "core/ixf.h"

namespace m3k {

struct Occupant {
  int x = 0, z = 0, y = 0, rot = 0;
  int sizeX = 1, sizeZ = 1;
  uint32_t occId = 0;       // key instance
  uint32_t uid = 0;         // runtime only (never saved): stable instance id, 0 = not assigned yet (City::rebuildIndex)
  uint32_t keyType = 0;     // 0x207EDC0E or 0x220055E1
  uint32_t agentType = 0;
  uint32_t state = 1;       // 1 occupied, 2 abandoned, 4 construction, 8 rubble, 16 fire; |0x200 = lot protected
  uint32_t caps = 0;        // runtime only: occupant capability flags (attr 0x72); 0x400 = ignorable for #28
  uint32_t buildingFlags = 0;  // runtime only: attr 0xCA; bit 0x100 = filler lot
  // Spec 13 §3.1a run-time class flag word (not saved). Generic buildings = 0x104 (Q-BC).
  uint32_t classFlags = 0;
  // Placement-query / network retile queries (spec 14 §6.1). Ordinary buildings answer false to both.
  bool removable = false;      // same boolean network retile / crossable checks use
  bool altClearable = false;   // alternate clearable query (filter mode f ≠ 1)
  std::string name;
  // Per-class data of a saved entry (spec 10 6.2.5), kept so loaded buildings round-trip.
  bool hasDate = false; int32_t dateYear = 0, dateDay = 0;
  std::vector<std::pair<uint32_t, uint32_t>> intProps;   // ascending id
  std::vector<std::pair<uint32_t, std::string>> strProps;
  std::vector<std::pair<uint32_t, float>> floatProps;
  bool boolA = true, boolB = true;  // flags bits 29, 30 (meaning open)
};

struct NetPiece {
  int x = 0, z = 0, y = 0, rot = 0;
  uint32_t pieceId = 0;  // occupant id; key E223741F:A317745F:<id>
  uint32_t pieceWord = 0;
};

struct CityInfo {
  std::string cityName, mayorName;
  int dayOfYear = 1, year = 2000, gameSpeed = 1, foundingYear = 2000;
  int fileKind = 3;  // 3 city, 4 terrain, 5 starter town
  // New City options Disasters / Auto Budget (spec 10 3.2): both 1 when both boxes checked, both 0 when cleared [OBSERVED].
  // flagA = Disasters, flagB = Auto Budget (observed: the original's City Options for Berlin, Germany shows flags 0 / 1 as Disasters off, Auto Budget on).
  uint8_t flagA = 0, flagB = 1;
};

struct SchemeSel { uint32_t buildingSet = 0, floraSet = 0, landscape = 0; };

struct ViewState { float centreX = 0, centreZ = 0; int zoom = 2, rotation = 0; bool valid = false; };

class RawRecordStore;  // records.h

struct World {
  int W = 0, H = 0;
  CityInfo info;
  SchemeSel scheme;
  ViewState view;

  float heightScale = 3.266f;
  uint8_t seaLevel = 30;
  std::vector<uint8_t> alt;    // (W+1)*(H+1)
  std::vector<uint8_t> water;  // (W+1)*(H+1) water surface altitude per vertex
  // Per-vertex sea flag handed over by the terrain generator (spec 11 s3.4 step 10); empty for loaded files. Not saved:
  // the city turns it into the pollution cells' sea-water flag (bit 30, spec 11 s5), which is.
  std::vector<uint8_t> seaVertex;
  std::vector<uint8_t> tileBits;  // W*H/8 bytes, preserved verbatim
  std::vector<uint8_t> flora;  // W*H: kind*16+level
  std::vector<uint8_t> zone;   // W*H zone type
  std::vector<Occupant> occupants;
  std::vector<NetPiece> surface, pipes, subway;
  // Every record of the loaded file (known or not), kept so unknown records round-trip. Set by loadCity;
  // City::flush() replaces the layer records it owns; saveCity() patches the records this struct models.
  std::shared_ptr<RawRecordStore> raw;

  void init(int w, int h);
  int vIdx(int x, int z) const { return x * (H + 1) + z; }
  int tIdx(int x, int z) const { return x * H + z; }
  uint8_t altAt(int x, int z) const { return alt[vIdx(x, z)]; }
  uint8_t waterAt(int x, int z) const { return water[vIdx(x, z)]; }
  bool inTiles(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < H; }

  // Altitude a surface network piece is registered to (spec 25 5.1): pieces lying on the terrain use the highest of
  // their tile's four corners (the stored y is the lowest corner in about half of all slope pieces), while bridge
  // decks, inclines and towers (agents 16003/16004/16011/17003/17204) keep the stored y. Sprites and vehicle paths
  // both measure from this height. agentType 0 = unknown (treated as terrain-bound).
  int surfaceDrawY(const NetPiece& np, uint32_t agentType) const {
    switch (agentType) { case 16003: case 16004: case 16011: case 17003: case 17204: return np.y; default: break; }
    if (!inTiles(np.x, np.z)) return np.y;
    const int top = std::max(std::max(altAt(np.x, np.z), altAt(np.x + 1, np.z)), std::max(altAt(np.x, np.z + 1), altAt(np.x + 1, np.z + 1)));
    return std::max(np.y, top);
  }
  // Deep-water table for seaport pier (spec 15 §5.5 / Q-AX): W×W u16, index z·W+x.
  // Built on first use from per-vertex classification; not saved. Call invalidateDeepWater() after
  // terrain/water/sea-level edits.
  void invalidateDeepWater() const { deepWater_.clear(); }
  bool deepWaterAt(int x, int z) const;  // true when table[z·W+x] != 0xFFFF
  const std::vector<uint16_t>& deepWaterTable() const { ensureDeepWater(); return deepWater_; }

 private:
  void ensureDeepWater() const;
  mutable std::vector<uint16_t> deepWater_;  // W*W; empty = not built yet
};

// Load a .sc3 / .st3 / .sct / .SNR file. Throws FormatError on failure. `warnings` receives
// non-fatal notes (e.g. unreadable optional records).
World loadCity(const Bytes& file, std::vector<std::string>* warnings = nullptr);

// Preview record (spec/10 3.5): what the load dialogs show without loading the city. City-only fields are present
// for file kind 3. `image` is the W x H overview thumbnail (RGBA), empty if absent or undecodable.
struct CityPreview {
  bool hasStats = false;
  uint32_t population = 0, dayOfYear = 0, year = 0;
  int32_t funds = 0;
  std::string mayorName, cityName;
  Image image;
};
bool readCityPreview(const Bytes& file, CityPreview& out);

// Inner record database (the decompressed packed segment, spec/10 section 5).
class InnerDb {
 public:
  void parse(Bytes data);  // throws FormatError
  bool find(const ResKey& k, const uint8_t*& p, size_t& n) const;
  size_t count() const { return recs_.size(); }
 private:
  struct Rec { size_t off, len; };
  Bytes data_;
  std::map<ResKey, Rec> recs_;
};

}  // namespace m3k

namespace m3k {
// Write a city file containing the records this engine understands. Absent records load as
// new-city state in the original (spec 10 section 5.2 notes).
Bytes saveCity(const World& w);
}  // namespace m3k
