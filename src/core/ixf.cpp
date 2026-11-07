// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/ixf.h"

namespace m3k {

bool IxfFile::open(const fs::path& path) {
  path_ = path;
  data_.clear(); entries_.clear(); index_.clear();
  std::error_code ec;
  if (path.empty() || !fs::exists(path, ec)) return true;  // missing file = empty segment
  if (!readFile(path, data_)) { data_.clear(); return false; }
  return parse();
}

bool IxfFile::openMemory(Bytes data) {
  path_.clear();
  data_ = std::move(data);
  entries_.clear(); index_.clear();
  return parse();
}

bool IxfFile::parse() {
  if (data_.empty()) return true;  // zero-length file is an empty segment
  if (data_.size() < 4 || rd32(data_.data()) != kIxfMagic) { data_.clear(); return false; }
  size_t pos = 4;
  bool terminated = false;
  std::vector<IxfEntry> live;
  while (pos + 20 <= data_.size()) {
    const uint8_t* e = data_.data() + pos;
    uint32_t g = rd32(e), i = rd32(e + 4), t = rd32(e + 8), off = rd32(e + 12), sz = rd32(e + 16);
    pos += 20;
    if (g == 0 && i == 0 && t == 0) { terminated = true; break; }
    if (off == 0xFFFFFFFFu || sz == 0xFFFFFFFFu) continue;  // free slot
    live.push_back({{t, g, i}, off, sz});
  }
  if (!terminated) { data_.clear(); return false; }  // EOF before terminator: open fails
  for (const auto& e : live) {
    if (e.offset > data_.size()) continue;  // defensive: never observed in shipped data
    if (index_.emplace(e.key, entries_.size()).second) entries_.push_back(e);  // first wins
  }
  return true;
}

bool IxfFile::get(const ResKey& k, const uint8_t*& p, size_t& avail, uint32_t* declared) const {
  auto it = index_.find(k);
  if (it == index_.end()) return false;
  const IxfEntry& e = entries_[it->second];
  p = data_.data() + e.offset;
  avail = data_.size() - e.offset;
  if (declared) *declared = e.size;
  return true;
}

bool IxfFile::getBytes(const ResKey& k, Bytes& out) const {
  const uint8_t* p; size_t avail; uint32_t sz;
  if (!get(k, p, avail, &sz)) return false;
  size_t n = sz < avail ? sz : avail;
  out.assign(p, p + n);
  return true;
}

void IxfWriter::add(const ResKey& k, Bytes payload) { items_.push_back({k, std::move(payload)}); }

Bytes IxfWriter::build() const {
  size_t n = items_.size();
  size_t dataStart = 0x5004 + 0x5000 * (n / 1024);
  Bytes out;
  put32(out, kIxfMagic);
  size_t pos = dataStart;
  for (const auto& it : items_) {
    put32(out, it.key.group); put32(out, it.key.instance); put32(out, it.key.type);
    put32(out, uint32_t(pos)); put32(out, uint32_t(it.data.size()));
    pos += it.data.size();
  }
  for (int i = 0; i < 5; i++) put32(out, 0);  // terminator
  out.resize(dataStart, 0);
  for (const auto& it : items_) out.insert(out.end(), it.data.begin(), it.data.end());
  return out;
}

}  // namespace m3k
