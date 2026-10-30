// IXF indexed resource container. See spec/02-containers.md section 2.
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "core/bytes.h"
#include "core/fsutil.h"

namespace m3k {

struct ResKey {
  uint32_t type = 0, group = 0, instance = 0;
  bool operator==(const ResKey& o) const { return type == o.type && group == o.group && instance == o.instance; }
  bool operator<(const ResKey& o) const {
    if (type != o.type) return type < o.type;
    if (group != o.group) return group < o.group;
    return instance < o.instance;
  }
};
struct ResKeyHash {
  size_t operator()(const ResKey& k) const {
    uint64_t h = k.type * 0x9E3779B97F4A7C15ull;
    h ^= (k.group + 0x7F4A7C15u + (h << 6) + (h >> 2));
    h ^= (k.instance * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2));
    return size_t(h);
  }
};

struct IxfEntry { ResKey key; uint32_t offset = 0, size = 0; };

// One mounted IXF file. Index is read eagerly; payloads are read from the in-memory file image.
// (Whole-file read keeps this portable, including Android asset-style access. Large sprite DATs
// are tens of MB; acceptable for desktop. A mmap/lazy-read backend can replace `data_` later.)
class IxfFile {
 public:
  // Opens a file. A missing file yields an empty segment (spec 7.2). Returns false (and leaves the
  // segment empty) when the file exists but is not a valid IXF.
  bool open(const fs::path& path);
  // Open from memory (used for saves and tests).
  bool openMemory(Bytes data);

  bool has(const ResKey& k) const { return index_.count(k) != 0; }
  // Payload view: starts at the entry offset and extends to end of file (spec 2.3: reads are not
  // bounded by the size field). `declaredSize` receives the index size.
  bool get(const ResKey& k, const uint8_t*& p, size_t& availableBytes, uint32_t* declaredSize = nullptr) const;
  // Payload copy bounded by the declared size.
  bool getBytes(const ResKey& k, Bytes& out) const;

  const std::vector<IxfEntry>& entries() const { return entries_; }
  const fs::path& path() const { return path_; }
  size_t fileSize() const { return data_.size(); }
  const Bytes& image() const { return data_; }

 private:
  bool parse();
  fs::path path_;
  Bytes data_;
  std::vector<IxfEntry> entries_;                       // live entries in index order
  std::unordered_map<ResKey, size_t, ResKeyHash> index_;  // key -> entries_ position (first wins)
};

// Container writer using the original's block-preallocated layout (spec 2.4, 2.6).
class IxfWriter {
 public:
  void add(const ResKey& k, Bytes payload);
  Bytes build() const;  // magic, n entries, terminator, zero fill to dataStart(n), payloads
 private:
  struct Item { ResKey key; Bytes data; };
  std::vector<Item> items_;
};

constexpr uint32_t kIxfMagic = 0x80C381D7;

}  // namespace m3k
