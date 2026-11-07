#include <cstring>
#include "core/gamedata.h"
#include "core/settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace m3k {

static uint64_t spriteKey(uint32_t cat, uint32_t id) { return (uint64_t(cat) << 32) | id; }

OccAttr parseOccAttr(const uint8_t* p, size_t n) {
  OccAttr a;
  if (n < 4 || p[0] != 'T' || p[1] != 'K' || p[2] != 'B' || p[3] != '1') return a;
  try {
    Reader r(p + 4, n - 4);
    while (r.remaining() >= 8) {
      uint32_t id = r.u32();
      uint16_t vt = r.u16(), cnt = r.u16();
      std::vector<uint32_t> nums;
      std::vector<ResKey> keys;
      std::vector<std::string> strs;
      for (uint16_t i = 0; i < cnt; i++) {
        switch (vt) {
          case 1: nums.push_back(r.u8()); break;
          case 2: nums.push_back(r.u16()); break;
          case 3: nums.push_back(r.u32()); break;
          case 7: { uint32_t l = r.u32(); strs.push_back(r.str(l)); break; }
          case 8: { ResKey k; k.type = r.u32(); k.group = r.u32(); k.instance = r.u32(); keys.push_back(k); break; }
          default: return a;
        }
      }
      switch (id) {
        case 0x65: if (!nums.empty()) a.agentType = nums[0]; break;
        case 0x66: if (nums.size() >= 3) { a.sizeX = nums[0]; a.heightClass = nums[1]; a.sizeZ = nums[2]; } break;
        case 0x67: if (!keys.empty()) a.appearance = keys[0]; break;
        case 0x72: if (!nums.empty()) a.flags = nums[0]; break;
        case 0xCA: if (!nums.empty()) a.buildingFlags = nums[0]; break;
        case 0xC9: if (!nums.empty()) a.powerAttr = int(nums[0]); break;
        case 0x1F4: if (!nums.empty()) a.partIndex = int(nums[0]); break;
        case 0x12D: if (!nums.empty()) a.netAttr12D = nums[0]; break;
        case 0x136: if (!nums.empty()) { a.netMatrix[0] = nums[0]; a.hasNetMatrix = true; } break;
        case 0x137: if (!nums.empty()) a.netMatrix[1] = nums[0]; break;
        case 0x138: if (!nums.empty()) a.netMatrix[2] = nums[0]; break;
        case 0x6D: if (!keys.empty()) a.nameKey = keys[0]; break;
        case 0x6F: if (!keys.empty()) a.sparseId = int(keys[0].instance); break;
        case 0x64: if (!strs.empty()) a.designName = strs[0]; break;
        case 0x6B: if (!keys.empty()) a.pedProdKey = keys[0]; break;
        case 0x6C: if (!keys.empty()) a.vehProdKey = keys[0]; break;
        case 0x7C: if (!keys.empty()) a.descKey = keys[0]; break;
        default: break;
      }
    }
    a.valid = true;
  } catch (const FormatError&) {
    // keep what was parsed
    a.valid = a.agentType != 0;
  }
  return a;
}

// Compile "Script[zoom,orient,offX,offY]:op,op,..." into a flat op list (spec/25-rendering.md section 7.1).
static bool compileScript(const std::string& text, CsScript& out) {
  if (std::sscanf(text.c_str(), "Script[%d,%d,%d,%d]", &out.zoom, &out.orient, &out.offX, &out.offY) != 4) return false;
  const size_t colon = text.find("]:");
  if (colon == std::string::npos) return false;
  std::string body = text.substr(colon + 2);
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t comma = body.find(',', pos);
    std::string tok = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    pos = comma == std::string::npos ? body.size() + 1 : comma + 1;
    size_t b = tok.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    tok = tok.substr(b);
    size_t sp = tok.find(' ');
    const std::string op = tok.substr(0, sp);
    const char* args = sp == std::string::npos ? "" : tok.c_str() + sp + 1;
    int n[4] = {0, 0, 0, 0};
    int got = std::sscanf(args, "%d %d %d %d", &n[0], &n[1], &n[2], &n[3]);
    auto push = [&](ScriptOp::Kind k, int a = 0, int bb = 0) { ScriptOp o; o.kind = k; o.a = a; o.b = bb; out.ops.push_back(o); return &out.ops.back(); };
    if (op == "S" && got >= 3) {
      for (int f = 0; f < n[1]; f++) { push(ScriptOp::Draw, n[0] + f); push(ScriptOp::Wait, n[2]); }
    } else if (op == "P" && got >= 3) {
      for (int f = 0; f < n[1]; f++) { push(ScriptOp::Draw, n[0] + f); push(ScriptOp::Wait, n[2]); }
      for (int f = n[1] - 2; f >= 1; f--) { push(ScriptOp::Draw, n[0] + f); push(ScriptOp::Wait, n[2]); }
    } else if ((op == "D" || op == "DA") && got >= 1) {
      ScriptOp* o = push(op == "D" ? ScriptOp::Draw : ScriptOp::DrawAlpha, n[0]);
      if (got >= 3) { o->dx = n[1]; o->dy = n[2]; o->hasOffset = true; }
    } else if (op == "W" && got >= 1) push(ScriptOp::Wait, n[0]);
    else if (op == "WR" && got >= 2) push(ScriptOp::WaitRandom, n[0], std::max(1, n[1]));
    else if (op == "G" && got >= 1) push(ScriptOp::Goto, n[0]);
    else if (op == "R" && got >= 1) push(ScriptOp::RandomSkip, std::max(1, n[0]));
    else if (op == "L" && got >= 1) push(ScriptOp::Label, n[0]);
    else if (op == "GL") push(ScriptOp::GotoWindowTarget);
    else if ((op == "WL" || op == "WD") && sp != std::string::npos) {
      int target = 0;
      char digits[16] = {0};
      if (std::sscanf(args, "%8s %d", digits, &target) == 2) {
        ScriptOp* o = push(ScriptOp::DateWindow);
        // mmddMMDD: digits 1-2 start month, 3-4 start day, 5-6 end month, 7-8 end day (leading zeros kept in the text)
        int sm = 0, sd = 0, em = 0, ed = 0;
        if (std::strlen(digits) == 8 && std::sscanf(digits, "%2d%2d%2d%2d", &sm, &sd, &em, &ed) == 4) { o->startMD = sm * 100 + sd; o->endMD = em * 100 + ed; }
        o->target = target; o->absTarget = op == "WD";
      }
    }
    // CB, CE, CD, END...: no runtime effect.
  }
  return !out.ops.empty();
}

CsAttrib parseCsAttrib(const uint8_t* p, size_t n) {
  CsAttrib c;
  if (n < 4 + 0x14 || p[0] != 'B' || p[1] != 'I' || p[2] != 'N' || p[3] != '\r') return c;
  try {
    Reader r(p + 4, n - 4);
    r.s32();  // version
    c.clsid = r.u32();
    c.zoomCount = r.u8(); c.rotCount = r.u8(); c.zoomSetCount = r.u8(); c.animSetCount = r.u8();
    c.animFrames = r.u8();
    r.u8();   // RegRects
    r.u32();  // LayerFlags
    uint16_t fc = r.u16();
    for (uint16_t i = 0; i < fc; i++) {
      CsFrame f;
      r.u16();
      f.category = r.u32(); f.spriteId = r.u32();
      f.flags = int8_t(r.u8()); f.offX = r.s16(); f.offY = r.s16();
      c.frames.push_back(f);
    }
    c.valid = true;
    // Optional trailing script block: "BIN\r", s32 version, string name, u32 count, count x string.
    // Callers must pass the record's declared size, or the next record's "BIN\r" would be read here.
    if (r.remaining() >= 16 && r.str(4) == "BIN\r") {
      r.s32();
      uint32_t nl = r.u32();
      if (nl > 64) return c;
      r.skip(nl);
      uint32_t cnt = r.u32();
      for (uint32_t i = 0; i < cnt && i < 256; i++) {
        uint32_t len = r.u32();
        if (len > r.remaining()) break;
        CsScript sc;
        if (compileScript(r.str(len), sc)) c.scripts.push_back(std::move(sc));
      }
      // Then the child sprites: u32 count, count x "Sprite[zoom,rot,childInst(hex),dx,dy,dz]".
      if (r.remaining() >= 4) {
        uint32_t ccnt = r.u32();
        for (uint32_t i = 0; i < ccnt && i < 1024 && r.remaining() >= 4; i++) {
          uint32_t len = r.u32();
          if (len > r.remaining()) break;
          std::string s = r.str(len);
          CsChild ch;
          unsigned child = 0;
          if (std::sscanf(s.c_str(), "Sprite[%d,%d,%x,%d,%d,%d]", &ch.zoom, &ch.orient, &child, &ch.dx, &ch.dy, &ch.dz) == 6) {
            ch.child = child;
            c.children.push_back(ch);
          }
        }
      }
    }
  } catch (const FormatError&) {}
  return c;
}

bool GameData::open(const fs::path& root, std::string& err) {
  std::error_code ec;
  fs::path base = root;
  fs::path apps = resolveCI(base, "Apps");
  if (apps.empty()) {
    // Maybe `root` is the Apps directory itself.
    if (!resolveCI(base, "Sys/SYS.PAK").empty() && !resolveCI(base, "Res").empty()) {
      apps = base;
      base = base.parent_path();
    } else {
      err = "No 'Apps' directory (or Sys/SYS.PAK + Res) found in " + root.string();
      return false;
    }
  }
  if (resolveCI(apps, "Sys/SYS.PAK").empty() && resolveCI(apps, "Sys/Default.ini").empty()) {
    err = "Missing Sys/SYS.PAK in " + apps.string() + " (not a SimCity 3000 Unlimited install?)";
    return false;
  }
  root_ = base; apps_ = apps;
  ini_.setSources(resolveCI(apps, "Sys"));
  {  // language selection (spec 05 section 2): explicit request, else the OS locale, else English; always an installed folder
    LanguageInputs li;
    li.cmdLine = languageRequest;
    li.installed = [&](int id) { return languageInstalled(apps_, id); };
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
      const char* v = std::getenv(var);
      if (v && *v) { uint16_t id = langIdFromPosixLocale(v); if (id) li.registryDword = id; break; }
    }
    if (const char* name = languageFolder(chooseLanguage(li))) language = name;
  }
  mountSegments();
  mountSpriteDats();
  return true;
}

void GameData::mountSegments() {
  IniFile def;
  if (!ini_.load("Default.ini", def)) return;
  std::string lang = language;
  for (const auto& kv : def.section("DBSegments")) {
    std::string v = kv.second;
    size_t semi = v.find(';');
    if (semi == std::string::npos) continue;
    std::string path = trimSpaces(v.substr(semi + 1));
    const std::string dd = "%DataDirectory%";
    if (path.compare(0, dd.size(), dd) == 0) path = path.substr(dd.size());
    const std::string pd = "%PluginDirectory%";
    if (path.compare(0, pd.size(), pd) == 0) path = path.substr(pd.size());
    for (size_t pos; (pos = path.find("%Language%")) != std::string::npos;) path.replace(pos, 10, lang);
    fs::path p = resolveCI(apps_, path);
    db_.mount(p, "base");
  }
}

void GameData::mountSpriteDats() {
  fs::path dir = resolveCI(apps_, "Res/Sprites");
  if (dir.empty()) return;
  for (const auto& f : listFilesCI(dir, ".dat")) db_.mount(f, "sprites");
  // Composite-sprite attribute IXFs are listed in [DBSegments]; BAT.DAT is mounted on demand.
  fs::path bat = resolveCI(apps_, "BARender/BAT.DAT");
  if (!bat.empty()) db_.mount(bat, "bat");
}

void GameData::remountBat() {
  db_.unmountTag("bat");
  fs::path bat = resolveCI(apps_, "BARender/BAT.DAT");
  if (!bat.empty()) db_.mount(bat, "bat");
  spriteCache_.clear(); planeCache_.clear(); csCache_.clear();
}

bool GameData::mountBuildingSet(uint32_t setId) {
  char name[16];
  std::snprintf(name, sizeof name, "%08X", setId);
  fs::path dir = resolveCI(apps_, std::string("Res/Sprites/BuildingSets/") + name);
  if (dir.empty()) return false;
  for (const auto& f : listFilesCI(dir, ".dat")) db_.mount(f, "set");
  spriteCache_.clear(); planeCache_.clear(); occCache_.clear(); csCache_.clear();
  return true;
}

bool GameData::mountFloraSet(uint32_t setId) {
  char name[16];
  std::snprintf(name, sizeof name, "%08X", setId);
  fs::path dir = resolveCI(apps_, std::string("Res/Sprites/FloraSets/") + name);
  if (dir.empty()) return false;
  for (const auto& f : listFilesCI(dir, ".dat")) db_.mount(f, "set");
  spriteCache_.clear(); planeCache_.clear(); occCache_.clear(); csCache_.clear();
  return true;
}

void GameData::unmountSets() {
  db_.unmountTag("set");
  spriteCache_.clear(); planeCache_.clear(); occCache_.clear(); csCache_.clear();
}

const SpriteImage* GameData::sprite(uint32_t cat, uint32_t id) {
  uint64_t k = spriteKey(cat, id);
  auto it = spriteCache_.find(k);
  if (it != spriteCache_.end()) return it->second->ok ? it->second.get() : nullptr;
  auto s = std::make_unique<SpriteImage>();
  if (isFxFrameCat(cat)) {   // one frame of a strip image
    const int fw = int((cat >> 12) & 0xFFFu), frame = int(cat & 0xFFFu);
    const SpriteImage* base = fw > 0 ? sprite(0, id) : nullptr;
    if (base && frame * fw + fw <= base->img.w) {
      s->img.w = fw; s->img.h = base->img.h;
      s->img.px.resize(size_t(fw) * size_t(base->img.h));
      for (int y = 0; y < base->img.h; y++)
        std::copy_n(&base->img.px[size_t(y) * base->img.w + size_t(frame) * fw], fw, &s->img.px[size_t(y) * fw]);
      s->L = base->L; s->T = base->T; s->R = base->R; s->B = base->B; s->hasSpan = base->hasSpan; s->ok = true;
    }
    auto* raw = s.get();
    spriteCache_[k] = std::move(s);
    return raw->ok ? raw : nullptr;
  }
  ResourceRef img = db_.find({T_SPRITE_IMAGE, cat, id});
  if (img) {
    try {
      DecodedSprite d = decodeSprite(img.data, img.available);
      if (!d.isPlane) { s->img = std::move(d.image); s->ok = true; }
    } catch (const FormatError&) {}
  }
  if (s->ok) {
    ResourceRef sp = db_.find({T_SPRITE_SPAN, cat, id});
    if (sp && sp.available >= 8) {
      s->L = int16_t(rd16(sp.data)); s->T = int16_t(rd16(sp.data + 2));
      s->R = int16_t(rd16(sp.data + 4)); s->B = int16_t(rd16(sp.data + 6));
      s->hasSpan = true;
    } else {
      s->L = s->img.w / 2; s->T = s->img.h;  // bottom-centre default (spec 04 open question 2)
      s->R = s->img.w - s->L; s->B = 0;
    }
  }
  auto* raw = s.get();
  spriteCache_[k] = std::move(s);
  return raw->ok ? raw : nullptr;
}

const Plane* GameData::spritePlane(uint32_t cat, uint32_t id) {
  uint64_t k = spriteKey(cat, id);
  auto it = planeCache_.find(k);
  if (it != planeCache_.end()) return it->second.get();
  std::unique_ptr<Plane> pl;
  if (isFxFrameCat(cat)) {
    const int fw = int((cat >> 12) & 0xFFFu), frame = int(cat & 0xFFFu);
    const Plane* base = fw > 0 ? spritePlane(0, id) : nullptr;
    if (base && frame * fw + fw <= base->w) {
      pl = std::make_unique<Plane>();
      pl->w = fw; pl->h = base->h;
      pl->v.resize(size_t(fw) * size_t(base->h));
      for (int y = 0; y < base->h; y++)
        std::copy_n(&base->v[size_t(y) * base->w + size_t(frame) * fw], fw, &pl->v[size_t(y) * fw]);
    }
    auto* raw = pl.get();
    planeCache_[k] = std::move(pl);
    return raw;
  }
  ResourceRef img = db_.find({T_SPRITE_IMAGE, cat, id});
  if (img) {
    try {
      DecodedSprite d = decodeSprite(img.data, img.available);
      if (d.isPlane) pl = std::make_unique<Plane>(std::move(d.plane));
    } catch (const FormatError&) {}
  }
  auto* raw = pl.get();
  planeCache_[k] = std::move(pl);
  return raw;
}

const OccAttr* GameData::occupant(uint32_t id, bool network, bool port) {
  std::lock_guard<CacheMutex> lk(occMu_);
  auto& cache = network ? netCache_ : port ? portCache_ : occCache_;
  auto it = cache.find(id);
  if (it == cache.end()) {
    OccAttr a;
    ResourceRef r;
    if (network) {
      r = db_.find({T_NET_ATTR, 0xA317745F, id});
      if (!r) r = db_.find({T_NET_ATTR, 0x0000057E, id});
    } else if (port) {
      r = db_.find({T_OCC_ATTR_PORT, 0x0000057E, id});
    } else {
      r = db_.find({T_OCC_ATTR, 0x0000057E, id});
      if (!r) r = db_.find({T_FLORA_ATTR, 0x80F48961, id});
    }
    if (r) a = parseOccAttr(r.data, r.declared < r.available ? r.declared : r.available);
    it = cache.emplace(id, a).first;
  }
  return it->second.valid ? &it->second : nullptr;
}

const CsAttrib* GameData::csattrib(const ResKey& k) {
  uint64_t h = (uint64_t(k.group) << 32) | k.instance;
  h ^= (k.type == T_CSATTRIB_ANIM) ? 0x8000000000000000ull : 0;
  auto it = csCache_.find(h);
  if (it == csCache_.end()) {
    CsAttrib c;
    ResourceRef r = db_.find(k);
    if (r) c = parseCsAttrib(r.data, r.declared && r.declared < r.available ? r.declared : r.available);
    it = csCache_.emplace(h, c).first;
  }
  return it->second.valid ? &it->second : nullptr;
}

std::string GameData::string(uint32_t group, uint32_t instance) {
  ResourceRef r = db_.find({T_STRING, group, instance});
  if (!r || r.available < 4) return {};
  uint32_t len = rd32(r.data);
  if (len > r.available - 4) len = uint32_t(r.available - 4);
  return std::string(reinterpret_cast<const char*>(r.data + 4), len);
}

}  // namespace m3k
