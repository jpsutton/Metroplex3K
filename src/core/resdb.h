// Resource database: a stack of mounted IXF segments. Most recently mounted segment wins
// (spec/02-containers.md 7.4).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/ini.h"
#include "core/ixf.h"

namespace m3k {

struct ResourceRef {
  const uint8_t* data = nullptr;
  size_t available = 0;   // bytes from payload start to end of the segment file
  uint32_t declared = 0;  // size field of the index entry
  explicit operator bool() const { return data != nullptr; }
};

class ResourceDb {
 public:
  // Mount an IXF file. Missing files mount as empty segments. Returns the segment handle (index).
  int mount(const fs::path& path, const std::string& tag = "");
  // Mount an in-memory segment.
  int mountMemory(Bytes data, const std::string& tag = "");
  // Unmount all segments mounted with this tag.
  void unmountTag(const std::string& tag);

  ResourceRef find(const ResKey& k) const;
  bool has(const ResKey& k) const { return bool(find(k)); }
  size_t segmentCount() const { return segs_.size(); }

  // Enumerate every (key) visible in any segment (duplicates collapsed).
  std::vector<ResKey> keysOfType(uint32_t type) const;

 private:
  struct Seg { std::shared_ptr<IxfFile> file; std::string tag; };
  std::vector<Seg> segs_;
};

}  // namespace m3k
