// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/cheats.h"
#include "core/compat_names.h"
#include <algorithm>
#include <cstdlib>

namespace m3k {

namespace {
using K = CheatKind;
const std::vector<CheatInfo>& build() {
  static const std::vector<CheatInfo> t = {
    // gameplay / developer (27 section 5.3); IDs from 5.1a
    {"I am weak", 0x62E8630B, K::Toggle, false, 0, 0},
    {"***", 0xE37FB7B8, K::Sets, false, 0, 0},
    {"power to the masses", 0xC2FA8E82, K::Sets, false, 0, 0},
    {"water in the desert", 0x42FA93FE, K::Sets, false, 0, 0},
    {"garbage in, garbage out", 0xC2FAA2F5, K::Sets, false, 0, 0},
    {"so sue me", 0x23B4418F, K::Toggle, false, 0, 0},
    {"I love red tape", 0xC37A5AAA, K::Action, false, 0, 0},
    {"call cousin Vinnie", 0xA3558FD6, K::Action, false, 0, 0},
    {"UFO Swarm", 0x6303EFA6, K::Toggle, false, 0, 0},
    {"Locust Swarm", 0xC5326321, K::Toggle, false, 0, 0},
    {"The Birds", 0x0367F938, K::Toggle, false, 0, 0},
    {"Terrain one up", 0xA31CF2EF, K::Action, false, 0, 0},
    {"Terrain one down", 0xC31CF2F6, K::Action, false, 0, 0},
    {"Terrain ten up", 0xC31CF2FA, K::Action, false, 0, 0},
    {"Terrain ten down", 0x831CF2FF, K::Action, false, 0, 0},
    {"traffic lights", 0x837961EA, K::Action, true, 0, 0},
    {"tuneup", 0xA42483B1, K::Action, false, 0, 0},
    {"force petitioners to say", 0x031DCE55, K::Action, true, 0, 0},
    {"force Moe to say", 0x43110C27, K::Action, true, 0, 0},
    {"force Gus to say", 0xC310F7D4, K::Action, true, 0, 0},
    {"stop forcing advice", 0x831109B4, K::Action, false, 0, 0},
    // phrases whose IDs are [OPEN]: never logged
    {"pay tribute to your king", 0, K::Sets, false, 0, 0},
    {"zyxwvu", 0, K::Action, false, 0, 0},
    {"let's make a deal", 0, K::Sets, false, 0, 0},
    {"nerdz rool", 0, K::Sets, false, 0, 0},
    {"Salt on", 0, K::Action, false, 0, 0},
    {"Salt off", 0, K::Action, false, 0, 0},
    {"Load terrain", 0, K::Action, true, 0, 0},
    {"force Mortimer to say", 0, K::Action, true, 0, 0},
    {"force Constance to say", 0, K::Action, true, 0, 0},
    {"force Maria to say", 0, K::Action, true, 0, 0},
    {"force Randall to say", 0, K::Action, true, 0, 0},
    {"force Karen to say", 0, K::Action, true, 0, 0},
    // shell ticker responses (5.2)
    {"fund", 0xC352E7AE, K::Ticker, false, 0x325, 0x326},
    {"advisor", 0x0352E7C2, K::Ticker, false, 0x327, 0},
    {"moremoney", 0x0352E7CC, K::Ticker, false, 0x32A, 0},
    {"maxis", 0x8352E7D6, K::Ticker, false, 0x32B, 0x32C},
    {"1234", 0x4352E7E1, K::Ticker, false, 0x32D, 0},
    {"sc3k", 0x4352E7ED, K::Ticker, false, 0x32E, 0},
    {"mayor", 0x4352E7F6, K::Ticker, false, 0x32F, 0},
    {"hello", 0xA352E800, K::Ticker, false, 0x330, 0},
    {"simon says", 0x03546E10, K::Ticker, true, 0x341, 0},
    {compat::kCheatPhraseGame, 0xE352E809, K::Ticker, false, 0x331, 0},
    {"money", 0xA352E814, K::Ticker, false, 0x332, 0},
    {"ticker", 0xE352E822, K::Ticker, false, 0x333, 0},
    {"electronic arts", 0x4352E828, K::Ticker, false, 0x334, 0},
    {"erts", 0x0352E873, K::Ticker, false, 0x335, 0},
    {"will wright", 0x0352E87C, K::Ticker, false, 0x336, 0},
    {"help", 0x0352E885, K::Ticker, false, 0x337, 0x338},
    {"sim", 0xE352E88F, K::Ticker, false, 0x339, 0x33A},
    {"easter egg", 0x4352E897, K::Ticker, false, 0x33B, 0},
    {"llama", 0xC352E8A2, K::Ticker, false, 0x33C, 0},
    {"scurk", 0xC352E8AC, K::Ticker, false, 0x33D, 0},
    {"bat", 0xC352E8B4, K::Ticker, false, 0x33E, 0},
    {"porntipsguzzardo", 0x8352E8BD, K::Ticker, false, 0x33F, 0},
    {"broccoli", 0x2352E8C7, K::Ticker, false, 0x340, 0},
  };
  return t;
}

bool startsWithCI(const std::string& s, const std::string& p) {
  if (s.size() < p.size()) return false;
  for (size_t i = 0; i < p.size(); i++)
    if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(p[i]))) return false;
  return true;
}
}  // namespace

const std::vector<CheatInfo>& cheatTable() { return build(); }

const CheatInfo* findCheatByPhrase(const std::string& phrase) {
  for (const auto& c : build()) if (iequals(phrase, c.phrase)) return &c;
  return nullptr;
}
const CheatInfo* findCheatById(uint32_t id) {
  if (!id) return nullptr;
  for (const auto& c : build()) if (c.id == id) return &c;
  return nullptr;
}

CheatMatch matchCheat(const std::string& in) {
  CheatMatch m;
  size_t best = 0;
  for (const auto& c : build()) {
    size_t n = std::char_traits<char>::length(c.phrase);
    if (n > best && startsWithCI(in, c.phrase)) { m.info = &c; best = n; }
  }
  if (!m.info) return m;
  size_t p = best;
  while (p < in.size() && in[p] == ' ') p++;
  m.arg = in.substr(p);
  m.allCaps = true;
  for (char ch : in) if (ch >= 'a' && ch <= 'z') m.allCaps = false;
  return m;
}

uint16_t tickerMessageFor(const CheatMatch& m) {
  if (!m.info || m.info->kind != CheatKind::Ticker) return 0;
  if (m.info->tickerMsgAlt) return m.allCaps ? m.info->tickerMsg : m.info->tickerMsgAlt;
  return m.info->tickerMsg;
}

bool CheatRegistry::setActive(uint32_t id, bool on) {
  if (!on) { active_.erase(id); return false; }
  active_.insert(id);
  if (id && std::find(log_.begin(), log_.end(), id) == log_.end()) { log_.push_back(id); return true; }
  return false;
}

bool CheatRegistry::active(const std::string& phrase) const {
  const CheatInfo* c = findCheatByPhrase(phrase);
  if (!c) return false;
  return c->id ? active(c->id) : phraseOn_.count(toLower(c->phrase)) != 0;
}

CheatResult CheatRegistry::submit(const std::string& entered) {
  CheatResult r;
  r.match = matchCheat(entered);
  if (!r.match) return r;
  const CheatInfo& c = *r.match.info;
  r.tickerMsg = tickerMessageFor(r.match);
  if (c.kind == CheatKind::Ticker && c.takesArg) r.tickerEcho = r.match.arg;
  uint32_t key = c.id;
  std::string lp = toLower(c.phrase);
  if (c.kind == CheatKind::Sets) {
    r.stateOn = true;
    if (key) r.newlyLogged = setActive(key, true); else phraseOn_.insert(lp);
  } else if (c.kind == CheatKind::Toggle) {
    bool on = !(key ? active(key) : phraseOn_.count(lp) != 0);
    r.stateOn = on;
    if (key) r.newlyLogged = setActive(key, on);
    else if (on) phraseOn_.insert(lp); else phraseOn_.erase(lp);
  }
  return r;
}

int terrainCheatDelta(const CheatInfo* c) {
  if (!c) return 0;
  std::string p = toLower(c->phrase);
  if (p == "terrain one up") return 1;
  if (p == "terrain one down") return -1;
  if (p == "terrain ten up") return 10;
  if (p == "terrain ten down") return -10;
  return 0;
}
int shiftTerrainAltitude(int v, int d) { return std::clamp(v + d, 2, 253); }
int32_t soSueMeClamp(int32_t funds) { return funds < -99999 ? -99999 : funds; }
bool trafficLightsArgOn(const CheatMatch& m) { return m.arg == "on" || m.arg == "ON"; }
long forcedMessageId(const CheatMatch& m) {
  if (m.arg.empty()) return -1;
  char* end = nullptr;
  long v = std::strtol(m.arg.c_str(), &end, 0);
  return end == m.arg.c_str() ? -1 : v;
}

// ---- records --------------------------------------------------------------------------------
static void putHeader(Bytes& b) { put16(b, 1); b.push_back(0x02); b.push_back(0); put32(b, 0xDEADBEEF); }
static bool readHeader(Reader& r) {
  r.u16();
  uint8_t flags = r.u8();
  if (flags & 2) r.u8();
  return r.u32() == 0xDEADBEEF;
}

Bytes serializeCheatLog(const std::vector<uint32_t>& ids) {
  Bytes b;
  putHeader(b);
  put32(b, uint32_t(ids.size()));
  for (uint32_t id : ids) put32(b, id);
  return b;
}
bool parseCheatLog(const Bytes& p, std::vector<uint32_t>& ids) {
  ids.clear();
  try {
    Reader r(p);
    if (!readHeader(r)) return false;
    uint32_t n = std::min<uint32_t>(r.u32(), 1024);
    for (uint32_t i = 0; i < n; i++) ids.push_back(r.u32());
    return true;
  } catch (const FormatError&) { ids.clear(); return false; }
}
Bytes serializeCheatCount(uint32_t v) {
  Bytes b;
  putHeader(b);
  put32(b, v);
  return b;
}
bool parseCheatCount(const Bytes& p, uint32_t& v) {
  try { Reader r(p); if (!readHeader(r)) return false; v = r.u32(); return true; }
  catch (const FormatError&) { return false; }
}

bool readCheatLog(const Bytes& city, std::vector<uint32_t>& ids) {
  IxfFile f;
  Bytes b;
  if (!f.openMemory(city) || !f.getBytes(kCheatLogKey, b)) return false;
  return parseCheatLog(b, ids);
}

Bytes writeCheatLog(const Bytes& city, const std::vector<uint32_t>& ids) {
  IxfFile f;
  IxfWriter w;
  bool done = ids.empty();
  if (f.openMemory(city)) {
    for (const auto& e : f.entries()) {
      Bytes b;
      if (e.key == kCheatLogKey) {
        if (!ids.empty()) { w.add(e.key, serializeCheatLog(ids)); done = true; }
        continue;
      }
      if (f.getBytes(e.key, b)) w.add(e.key, std::move(b));
    }
  }
  if (!done) w.add(kCheatLogKey, serializeCheatLog(ids));
  return w.build();
}

}  // namespace m3k
