#include "core/resdb.h"

#include <algorithm>
#include <set>

namespace m3k {

int ResourceDb::mount(const fs::path& path, const std::string& tag) {
  auto f = std::make_shared<IxfFile>();
  f->open(path);  // failure leaves the segment empty, as in the original
  segs_.push_back({f, tag});
  return int(segs_.size()) - 1;
}

int ResourceDb::mountMemory(Bytes data, const std::string& tag) {
  auto f = std::make_shared<IxfFile>();
  f->openMemory(std::move(data));
  segs_.push_back({f, tag});
  return int(segs_.size()) - 1;
}

void ResourceDb::unmountTag(const std::string& tag) {
  segs_.erase(std::remove_if(segs_.begin(), segs_.end(), [&](const Seg& s) { return s.tag == tag; }), segs_.end());
}

ResourceRef ResourceDb::find(const ResKey& k) const {
  for (size_t i = segs_.size(); i-- > 0;) {
    ResourceRef r;
    if (segs_[i].file->get(k, r.data, r.available, &r.declared)) return r;
  }
  return {};
}

std::vector<ResKey> ResourceDb::keysOfType(uint32_t type) const {
  std::set<ResKey> s;
  for (const auto& seg : segs_)
    for (const auto& e : seg.file->entries())
      if (e.key.type == type) s.insert(e.key);
  return {s.begin(), s.end()};
}

}  // namespace m3k
