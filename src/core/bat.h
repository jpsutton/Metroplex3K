// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// BAT custom buildings: .bld reader/writer, BAT.DAT (rendered-building database), import,
// user building set (appearance replacement) and occupant attributes. spec/24-bat-buildings.md.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/gamedata.h"
#include "core/image.h"
#include "core/ini.h"
#include "core/ixf.h"

namespace m3k {

constexpr uint32_t kBldGroup = 0xC3F8D4A9;
constexpr ResKey kBldModelKey{0xE3F87B0C, kBldGroup, 0xA3F8D615};
constexpr ResKey kBldInfoKey{0xE3F87B0F, kBldGroup, 0x63FBE1CC};
constexpr ResKey kBldDescKey{0x644D7178, kBldGroup, 0xA44D71AA};
constexpr ResKey kBldCategoryKey{0x644D7179, kBldGroup, 0xA44D71AB};
constexpr ResKey kBldLangKey{0xB9EC8F72, kBldGroup, 0x99EC9601};
constexpr ResKey kBldRevisionKey{0x99F88B9E, kBldGroup, 0xB9F88BD2};
constexpr ResKey kBldThumbKey{0xE3F87B0E, kBldGroup, 0xE3F8D655};
constexpr ResKey kBldOwnerKey{0xDA6DC9A9, kBldGroup, 0x63FBE1CC};
constexpr uint32_t kBatGroup = 0xBA1DBF1E;
constexpr uint32_t kBatDirType = 0x856CD19A;

struct BldCell {
  uint32_t packed = 0;      // next:17 z:9 shape:4 rot:2
  uint32_t faceFlags = 0;
  uint8_t face[6][4] = {};  // paint, paintOffset, detail, detailOffset (prop cells: image, dx, 0, dz)
  uint32_t next() const { return packed & 0x1FFFF; }
  uint32_t z() const { return (packed >> 17) & 0x1FF; }
  uint32_t shape() const { return (packed >> 26) & 0xF; }
  uint32_t rot() const { return (packed >> 30) & 3; }
  void setRot(uint32_t r) { packed = (packed & 0x3FFFFFFF) | ((r & 3) << 30); }
};

struct BldPaletteEntry {
  std::string name;
  uint32_t textureId = 0;
  // Name convention `<name> |XXXXXXXX` (spec 1.3.5): splits off the texture ID. False if not present.
  bool split(std::string& baseName, uint32_t& id) const;
  // `color R,G,B` pseudo-name for a solid paint.
  bool solidColor(int rgb[3]) const;
};
// Building category label of spec 1.8 (codes 0x208..0x217); nullptr for unknown codes.
const char* batCategoryName(uint32_t code);

struct BldModel {
  uint8_t version = 8, orientation = 0, N = 8, lotSize = 1;
  uint16_t H = 28;
  std::vector<uint32_t> columnHead;  // N*N, row-major [row][col]; 0 = empty
  std::vector<BldCell> cells;        // cells[0] is the null sentinel
  std::vector<BldPaletteEntry> palette;
  uint32_t head(int r, int c) const { return columnHead[size_t(r) * N + size_t(c)]; }
  // Cell indices of one column, bottom to top. Throws FormatError on cycles.
  std::vector<uint32_t> column(int r, int c) const;
  // Quarter-turn the model: forward = (x,y)->(N-1-y,x) with rot+1; backward is the inverse with rot-1.
  void rotate(bool forward);
  // Orientation 1 -> one turn backwards, 3 -> one turn forwards, then orientation = 0 (spec 1.3.6).
  void normalizeOrientation();
  // Ensure a z=0 cube floor cell in every column (spec 3.2.6, revision >= 2 form).
  void ensureFloor();
  // Consistency: links in range, strictly increasing z, every cell reachable once, palette indices valid.
  bool validate(std::string* err = nullptr) const;
};

struct BldTexture {
  // Header record E3F87B0D (section 1.4).
  uint8_t version = 0x0A;
  uint32_t imageRect[4] = {}, swatchRect[4] = {};
  uint32_t textureId = 0, sourceKind = 0;
  float width = 0, height = 0, invWidth = 0, invHeight = 0;
  std::string name;
  uint32_t category = 0;  // 6 paint, 7 detail, 16 prop image
  // Pixel record E3F87B10 (section 1.5): padded buffer, BGRA, rows top to bottom.
  uint16_t pxFormat = 0x0EF1, pxW = 0, pxH = 0;
  Bytes bgra;
};

struct BldProp {  // record FA662D17 (section 1.6)
  uint32_t index = 0;
  std::string view[4];
  uint32_t size[3] = {};
};

struct BldFile {
  BldModel model;
  std::vector<BldTexture> textures;  // by index n
  std::vector<BldProp> props;
  // Building information (1.7).
  uint8_t lotSize = 1;
  std::string name, author;
  uint32_t reserved = 0, buildingId = 0;
  bool hasInfoTrailer = true;
  std::string description;           // 644D7178 (u8 length + chars)
  uint32_t category = 0x217;         // 644D7179
  uint32_t languageCode = 1;         // B9EC8F72
  uint8_t revision[4] = {2, 0, 28, 0};
  bool hasThumb = false;
  uint16_t thumbFormat = 0x07F0, thumbW = 200, thumbH = 200;
  std::vector<uint16_t> thumb;       // RGB565, thumbW*thumbH
  bool hasOwner = false;
  std::string owner;                 // DA6DC9A9
  uint32_t ownerBuildingId = 0;
  std::vector<std::pair<ResKey, Bytes>> unknown;  // preserved, unrecognised records

  // Parse an IXF .bld image. Throws FormatError on structural problems or unsupported model version.
  static BldFile parse(const Bytes& data);
  // Write the original layout (1024-entry index block). Writes the model with the original's
  // over-long compression buffer (n + n/9 + 1000 bytes, zero filler).
  Bytes serialize() const;
  uint32_t effectiveId() const;
  // Revision (major < 2) models use shape-8 floor markers; the engine renders them as stored (spec 1.8).
  bool needsRevisionConversion() const;  // stored ID, or a stable hash if none
};

// ---- BAT resource text files (spec 24 section 4) ------------------------------------------------
// `.met` prop description: lines `X,Y,Z:"view0","view1","view2","view3"`.
struct BatPropLine { int x = 0, y = 0, z = 0; std::string view[4]; };
// Returns the lines parsed; malformed lines are skipped (counted in `bad` when non-null).
std::vector<BatPropLine> parseBatMet(const std::string& text, size_t* bad = nullptr);
// `.plt` detail palette layout: lines `col,row="file name"`.
struct BatPaletteCell { int col = 0, row = 0; std::string file; };
std::vector<BatPaletteCell> parseBatPlt(const std::string& text, size_t* bad = nullptr);

// ---- rendering (approximate; spec 3 leaves rasteriser details open) -------------------------
struct BatLights {
  uint8_t north[3] = {255, 249, 232}, west[3] = {98, 98, 133}, south[3] = {98, 98, 133}, east[3] = {255, 249, 232},
          top[3] = {127, 127, 127};
  static BatLights fromIni(const IniFile& ini);  // [Lights] Preview* keys, "R,G,B"
};

// Render view 0..3 of the (normalised) model at 128 px per tile, then reduce by `reduce` (1,2,4,8,16).
// Cubes only: every non-prop shape is drawn as a flat-coloured cube; prop cells and textures are not
// rasterised. Transparent pixels have alpha 0. Bottom aligned. Returns an empty image for an empty model.
Image renderBldView(const BldModel& m, int view, int reduce, const BatLights& lights = BatLights());

// Encode an RGBA image as a type-0 sprite payload (RGB565 RLE, RefPack) and its 8-byte span record.
Bytes encodeSprite565(const Image& img);
Bytes makeSpanRecord(int16_t L, int16_t T, int16_t R, int16_t B);

// ---- BAT.DAT --------------------------------------------------------------------------------
struct BatDirEntry {
  uint8_t lotSize = 1;
  ResKey appearance;  // 00006300:BA1DBF1E:B
  std::string name, author, description, owner;
  uint32_t buildingId = 0;
  static bool parse(const Bytes& b, BatDirEntry& out);
  Bytes serialize() const;
};

struct BatImportResult { std::vector<uint32_t> imported; std::vector<std::string> skipped; };

class BatDatabase {
 public:
  bool load(const Bytes& ixf);          // false if not an IXF
  Bytes build() const;
  std::vector<BatDirEntry> list() const;  // sorted by building ID
  bool has(uint32_t buildingId) const;
  bool find(uint32_t buildingId, BatDirEntry& out) const;
  // Remove a building's directory, composite-sprite and sprite records.
  bool remove(uint32_t buildingId);
  // Render and add (or replace) a building: 16 sprites, composite record, directory record.
  // Returns the building ID, or 0 and `err` on failure (bad lot size).
  uint32_t import(const BldFile& b, const BatLights& lights = BatLights(), std::string* err = nullptr);
  // Sprite IDs of a building's composite record (frames 0-15).
  std::vector<uint32_t> spriteIds(uint32_t buildingId) const;
  size_t recordCount() const { return recs_.size(); }
  const Bytes* record(const ResKey& k) const;

 private:
  uint32_t allocateSpriteBase() const;
  std::map<ResKey, Bytes> recs_;
};

// Import every .bld listed; build the keep-list semantics: when `keepOnly` is non-null, buildings in
// the database that are not in it are removed afterwards (spec 2.4 step 5).
BatImportResult batImport(BatDatabase& db, const std::vector<BldFile>& files, const std::set<uint32_t>* keepOnly = nullptr,
                          const BatLights& lights = BatLights());
// Convenience for an install: reads <root>/Buildings/*.bld, imports into Apps/BARender/BAT.DAT,
// writing the backup Apps/Res/BA/RenderedBuildingsBackup.IXF first. Returns false on I/O failure.
bool batImportInstall(const fs::path& gameRoot, const std::vector<std::string>& bldNames, BatImportResult& out,
                      std::string& err);

// ---- user building set / appearance replacement ----------------------------------------------
constexpr ResKey kUserBuildingSetKey{0x99F47014, 0x79F4702D, 0};

struct UserBuildingSet {
  std::vector<std::pair<ResKey, ResKey>> pairs;  // (replaced occupant key, replacement appearance key)
  static bool parse(const Bytes& payload, UserBuildingSet& out);
  Bytes serialize() const;                       // 16 zero bytes when empty
  const ResKey* find(const ResKey& occKey) const;
  void set(const ResKey& occKey, const ResKey& appearance);  // replace existing pair or add
  bool revert(const ResKey& occKey);
  void clear() { pairs.clear(); }
};

// Occupant keys listed in SC3CityScheme.ini [ReplaceableBuildings] ("Type,Group,Instance=label", decimal).
class ReplaceableSet {
 public:
  void load(const IniFile& scheme);
  bool contains(const ResKey& occKey) const { return keys_.count(occKey) != 0; }
  size_t size() const { return keys_.size(); }
 private:
  std::set<ResKey> keys_;
};

// Can `bat` stand in for the occupant `occKey` (replaceable, same square footprint)?
bool canReplace(const ReplaceableSet& rs, const ResKey& occKey, const OccAttr& occ, const BatDirEntry& bat);

// Occupant attributes after replacement: everything from `base` except the appearance, which becomes
// the BAT composite key when the replacement is in `db` (missing -> normal appearance, spec 2.5).
OccAttr effectiveOccAttr(const OccAttr& base, const ResKey& occKey, const UserBuildingSet& set, const BatDatabase* db);
// Generic occupant attribute for a key that no longer resolves (missing BAT/add-on building).
OccAttr genericOccAttr(uint32_t agentType, uint32_t sizeX, uint32_t sizeZ);

// Read / write the set record of a city file image.
bool readUserBuildingSet(const Bytes& cityFile, UserBuildingSet& out);
Bytes writeUserBuildingSet(const Bytes& cityFile, const UserBuildingSet& set);

}  // namespace m3k
