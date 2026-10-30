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
#include "core/ixf.h"

namespace m3k {

struct Occupant {
  int x = 0, z = 0, y = 0, rot = 0;
  int sizeX = 1, sizeZ = 1;
  uint32_t occId = 0;       // key instance
  uint32_t keyType = 0;     // 0x207EDC0E or 0x220055E1
  uint32_t agentType = 0;
  uint32_t state = 1;       // 1 occupied, 2 abandoned, 4 construction, 8 rubble, 16 fire
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
  // Which is which is [INFERRED]: flagA = Disasters, flagB = Auto Budget.
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
};

// Load a .sc3 / .st3 / .sct / .SNR file. Throws FormatError on failure. `warnings` receives
// non-fatal notes (e.g. unreadable optional records).
World loadCity(const Bytes& file, std::vector<std::string>* warnings = nullptr);

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
