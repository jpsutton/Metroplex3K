// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Generic save-file record store (spec/10-save-format.md). Keeps every outer IXF record and every
// inner database record verbatim (known or not), so a loaded city can be re-saved byte-exactly for
// everything the engine does not interpret. Also: versioned-header / marker helpers, the outer
// record structs of section 3, and replaceable "new city" template data (spec 29 item 1.1).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ixf.h"

namespace m3k {

// ---- versioned record header (section 2.1) ----
struct RecHeader {
  uint16_t version = 0;
  uint8_t flags = 0, flags2 = 0;
  bool legacy = false;     // no DEADBEEF sentinel: version 0, flags 0, body starts at 0
  size_t bodyOffset = 0;   // first byte after the header
  bool markers() const { return (flags & 1) != 0; }
};
// Throws FormatError if the buffer is too short for the version/flags bytes.
RecHeader parseRecHeader(const uint8_t* p, size_t n);
void writeRecHeader(Bytes& b, uint16_t version, bool markers = false);  // 8 bytes: vv vv 02|03 00 EF BE AD DE
void putString(Bytes& b, const std::string& s);          // u32 length + bytes
std::string readString(Reader& r);
void putMarker(Bytes& b, bool enabled, const char* text);  // writes a marker string only if enabled
void skipMarker(Reader& r, const RecHeader& h);            // skips one marker string iff header flag bit 0

// ---- key constants ----
namespace keys {
constexpr ResKey SegmentDir{0x035F62A4, 0x035F62A4, 0}, Segment1{0x035F62A4, 0x035F62A4, 1};
constexpr ResKey CityInfo{0x00000FA1, 0x617E198D, 0}, FileKind{0xC359837E, 0x43598384, 0},
    StateWords{0x035AD1CB, 0x835AD1D1, 0}, Preview{0xE2F42628, 0x62F42635, 0},
    CheatCount{0x430E2EE6, 0x530E2EE6, 0}, CheatIds{0x630E2EE6, 0x730E2EE6, 0},
    Misc79FD{0x79FDB6C8, 0x59FDB6FD, 0x79FDB71E}, UserBuildingSet{0x99F47014, 0x79F4702D, 0},
    SchemeSel{0x0525A6B9, 0x4525A6E2, 1}, ViewWindow{0xA1634F05, 0x01E1A5E6, 0},
    ViewIso{0x620EDFCE, 0xA20FB3B9, 0};
constexpr ResKey Terrain{0x206C6E7C, 0x21737DE5, 0}, OccCounters{0x20631788, 0xA0AB89F0, 0},
    OccEntries{0xE0FAADC7, 0xA0AB89F0, 1}, NetHeader{0x206C6E7C, 0x2147C2DD, 0},
    Flora{0x406B1196, 0x80AB8AB0, 0}, ZoneMgr{0x206C6E7C, 0x409FF3BA, 0},
    Spacing{0x206C6E7C, 0x01FD7A8C, 0}, RciLayer{0x206C6E7C, 0x80F1E6D3, 0},
    ResLayer{0x206C6E7C, 0xC106C4F5, 0}, ComLayer{0x206C6E7C, 0x20EC9849, 0},
    IndLayer{0x206C6E7C, 0xA106CF3D, 0}, LandValue{0x206C6E7C, 0xC0AB8A88, 0},
    WorldLayer{0x206C6E7C, 0xE11BDDF6, 0}, Aura{0x206C6E7C, 0xC259C02D, 0},
    PowerLayer{0x206C6E7C, 0xE0AFDF68, 0}, WaterLayer{0x206C6E7C, 0x02BF0033, 0},
    Flammability{0x206C6E7C, 0x61448030, 0};
inline ResKey zoneDeveloperClass(int zone) { return {0x206C6E7C, 0x409FF3BA, uint32_t(zone + 1)}; }
}  // namespace keys

// ---- raw store ----
struct OuterRecord {
  ResKey key;
  Bytes data;                // payload; may extend past declaredSize (gap bytes up to the next record)
  uint32_t declaredSize = 0; // size written to the IXF index
};
struct InnerRecord { ResKey key; Bytes data; uint32_t segment = 0; };  // segment: index into segmentKeys
struct InnerOrphan { ResKey key; int64_t offsetFromDir = 0; uint32_t segment = 0; };  // directory entries the loader ignores

class RawRecordStore {
 public:
  // Parse an IXF city-family file image (.sc3/.st3/.sct/.SNR). Throws FormatError.
  static RawRecordStore parse(const Bytes& file);
  // Canonical layout: index preallocated for 1024 entries, records back to back in `outer` order with the
  // segment directory + segment records at `segmentPos`, inner directory sorted by (group, instance, type).
  // Byte-identical to the input for game-written files (given the canonical RefPack encoder).
  Bytes serialize() const;

  OuterRecord* findOuter(const ResKey& k);
  const OuterRecord* findOuter(const ResKey& k) const;
  void setOuter(const ResKey& k, Bytes data);  // replace in place or insert before the segment records
  bool removeOuter(const ResKey& k);
  InnerRecord* findInner(const ResKey& k);
  const InnerRecord* findInner(const ResKey& k) const;
  void setInner(const ResKey& k, Bytes data);  // replace in place or append
  bool removeInner(const ResKey& k);

  // Replace/insert every record of `other` (outer and inner) into this store, keeping all other records.
  void overlayFrom(const RawRecordStore& other);
  // Same but only records whose key is in `only`.
  void overlayKeys(const RawRecordStore& other, const std::vector<ResKey>& only);

  std::vector<OuterRecord> outer;      // excludes the segment directory and segment records
  std::vector<InnerRecord> inner;      // payload order
  std::vector<InnerOrphan> orphans;
  std::vector<ResKey> segmentKeys;     // keys listed by the segment directory (instances 1..)
  size_t segmentPos = SIZE_MAX;        // index in `outer` before which the segment records go (SIZE_MAX = end)
  uint32_t codec = 0x67;
  std::string codecVersion = "0.90";

 private:
  struct SegCache { Bytes raw, record; };  // original decoded inner db + segment record, reused when unchanged
  std::vector<SegCache> cache_;
};

// ---- outer record structs (section 3) ----
struct CityInfoRec {
  RecHeader hdr; int32_t appVersion[4] = {2, 0, 949, 0}; uint32_t constant3 = 3; std::string author = "m3k";
  uint32_t mapW = 0, mapH = 0, mapDepth = 0; std::string cityName, mayorName;
  uint32_t dayOfYear = 1, year = 2000, gameSpeed = 1, foundingYear = 2000; uint8_t flagA = 0, flagB = 1, flagC = 0;
  static CityInfoRec parse(const Bytes& b);  // throws FormatError
  Bytes write(bool markers = false) const;
};
struct StateWordsRec {  // section 3.4; accepts a 4-byte record
  uint32_t stateA = 0, cityId = 1; int32_t stateB = 0, stateC = 0, stateD = 1, stateE = 54; int fieldsPresent = 6;
  static StateWordsRec parse(const Bytes& b);
  Bytes write() const;  // 24 bytes; stateA forced to 0 (spec 3.4), or 4 bytes when fieldsPresent==1
};
struct PreviewHeader {  // section 3.5 (fields up to the image block)
  int32_t appVersion[4] = {0, 0, 0, 0}; bool cityFields = false; uint32_t population = 0; int32_t funds = 0, reserved = 0;
  uint32_t dayOfYear = 0, year = 0; std::string mayorName, cityName; uint32_t imageVersion = 2, packedSize = 0, width = 0, height = 0, pixelFormat = 7;
  size_t imageOffset = 0;  // offset of the image block (u32 size + RefPack) in the payload
  static PreviewHeader parse(const Bytes& b, int32_t fileKind);
};
struct CheatIdsRec { std::vector<uint32_t> ids; static CheatIdsRec parse(const Bytes& b); Bytes write() const; };

// ---- new-city templates (spec 29 item 1.1) ----
// Defaults are built from the spec where known (zone manager, valves, RCI, land value, world layer,
// spacing grid, aura, power, water, flammability, disaster lists). Anything the spec cannot give is
// absent. Replace/extend with captured records via set() / mergeFrom() / fromStore().
class TemplateSet {
 public:
  static TemplateSet defaults(int W, int H);
  static TemplateSet fromStore(const RawRecordStore& captured);  // all records of a captured new-city/terrain save
  void setInner(const ResKey& k, Bytes b) { inner_[k] = std::move(b); }
  void setOuter(const ResKey& k, Bytes b) { outer_[k] = std::move(b); }
  const Bytes* inner(const ResKey& k) const { auto it = inner_.find(k); return it == inner_.end() ? nullptr : &it->second; }
  const Bytes* outer(const ResKey& k) const { auto it = outer_.find(k); return it == outer_.end() ? nullptr : &it->second; }
  void mergeFrom(const TemplateSet& o);  // o wins
  // Add every template record that `store` lacks. Returns number added.
  size_t fillMissing(RawRecordStore& store) const;
  const std::map<ResKey, Bytes>& innerMap() const { return inner_; }
  const std::map<ResKey, Bytes>& outerMap() const { return outer_; }
 private:
  std::map<ResKey, Bytes> inner_, outer_;
};

}  // namespace m3k
