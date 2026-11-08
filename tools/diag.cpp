// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Install diagnostic: reports what the engine can read from a real install of the original game.
//   diag GAME_DIR [OUT_DIR]     (OUT_DIR gets PNGs of the skin art that resolved)
// Paste the whole output into a bug report. It never modifies the install.
#include <cstdio>
#include <map>
#include <set>
#include "core/font.h"
#include "core/game.h"
#include "core/gamedata.h"
#include "core/gamedir.h"
#include "core/skin.h"
#include "png.h"
using namespace m3k;

static void hex(const Bytes& b, size_t n) { for (size_t i = 0; i < n && i < b.size(); i++) std::printf("%02X ", b[i]); std::printf("\n"); }

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: diag GAME_DIR [OUT_DIR]\n"); return 1; }
  fs::path outDir = argc > 2 ? fs::path(argv[2]) : fs::path();
  if (!outDir.empty()) { std::error_code ec; fs::create_directories(outDir, ec); }
  fs::path root = findGameRoot(argv[1]);
  std::printf("== root: %s\n", root.empty() ? "(NOT FOUND)" : root.string().c_str());
  if (root.empty()) return 2;

  GameData gd; std::string err;
  bool ok = gd.open(root, err);
  std::printf("== GameData::open: %s %s\n", ok ? "ok" : "FAILED", err.c_str());
  if (!ok) return 3;
  std::printf("   apps dir: %s\n   language: %s\n", gd.appsDir().string().c_str(), gd.language.c_str());

  // SYS.PAK
  fs::path pakPath = resolveCI(gd.appsDir(), "Sys/SYS.PAK");
  PakFile pak;
  bool pakOk = !pakPath.empty() && pak.open(pakPath);
  Bytes raw; if (!pakPath.empty()) readFile(pakPath, raw);
  std::printf("== SYS.PAK: %s  size=%zu  parsed=%s  entries=%zu\n", pakPath.string().c_str(), raw.size(), pakOk ? "yes" : "NO", pak.names().size());
  if (!pakOk) { std::printf("   first bytes: "); hex(raw, 48); }
  else { int n = 0; for (auto& nm : pak.names()) { std::printf("%s%s", n++ ? ", " : "   ", nm.c_str()); } std::printf("\n"); }

  // Default.ini
  IniFile def;
  bool haveDef = gd.ini().load("Default.ini", def);
  std::printf("== Default.ini: %s\n", haveDef ? "loaded" : "NOT LOADED");
  size_t nSeg = 0, nMissing = 0, nBad = 0;
  if (haveDef) {
    auto segs = def.section("DBSegments");
    std::printf("   [DBSegments] lines: %zu   [ResourceFactories]: %zu\n", segs.size(), def.section("ResourceFactories").size());
    for (const auto& kv : segs) {
      std::string v = kv.second; size_t semi = v.find(';');
      if (semi == std::string::npos) { std::printf("   BAD LINE %s=%s\n", kv.first.c_str(), v.c_str()); nBad++; continue; }
      std::string path = trimSpaces(v.substr(semi + 1));
      const std::string dd = "%DataDirectory%";
      if (path.compare(0, dd.size(), dd) == 0) path = path.substr(dd.size());
      for (size_t p; (p = path.find("%Language%")) != std::string::npos;) path.replace(p, 10, gd.language);
      fs::path fp = resolveCI(gd.appsDir(), path);
      nSeg++;
      if (fp.empty()) { nMissing++; std::printf("   missing: %s\n", path.c_str()); continue; }
      IxfFile f; bool o = f.open(fp);
      if (!o || f.entries().empty()) std::printf("   %s: open=%d entries=%zu\n", path.c_str(), o, f.entries().size());
    }
    std::printf("   segments listed=%zu missing=%zu bad=%zu\n", nSeg, nMissing, nBad);
  }

  // Resource DB overview
  ResourceDb& db = gd.db();
  std::printf("== ResourceDb: %zu segments mounted\n", db.segmentCount());
  auto strs = db.keysOfType(T_STRING);
  auto imgs = db.keysOfType(T_UI_IMAGE);
  std::map<uint32_t, int> groups;
  for (auto& k : imgs) groups[k.group]++;
  std::printf("   strings=%zu  ui-images=%zu in %zu groups\n", strs.size(), imgs.size(), groups.size());
  for (auto& g : groups) std::printf("     image group %08X: %d\n", g.first, g.second);
  std::printf("   sample strings: menu1=\"%s\" gui10=\"%s\"\n", gd.string(0x03C09AFF, 1).c_str(), gd.string(0x225872FE, 0xA).c_str());
  size_t nSprites = db.keysOfType(T_SPRITE_IMAGE).size();
  std::printf("   sprite images=%zu  occupant attrs=%zu  csattrib=%zu\n", nSprites, db.keysOfType(T_OCC_ATTR).size(), db.keysOfType(T_CSATTRIB).size());

  // Fonts
  fs::path tdir = resolveCI(gd.appsDir(), "Res/Text/" + gd.language);
  Fonts fonts; fonts.load(tdir);
  std::printf("== Fonts: dir=%s\n", tdir.empty() ? "(missing)" : tdir.string().c_str());
  for (const char* n : {"system9", "main9", "main10", "title9", "title12", "serif17", "systemloadsave"}) {
    Bytes b; fs::path p = tdir.empty() ? fs::path() : resolveCI(tdir, std::string(n) + ".fbf");
    Font f; bool fo = !p.empty() && readFile(p, b) && f.load(b.data(), b.size());
    std::printf("   %-15s file=%s loaded=%s height=%d\n", n, p.empty() ? "missing" : "found", fo ? "yes" : "NO", fo ? f.height() : 0);
  }

  // Menu
  IniFile mi; bool haveMenu = gd.ini().load("MenuItem.INI", mi);
  MenuTree tree; bool parsed = haveMenu && tree.parse(mi);
  std::printf("== MenuItem.INI: loaded=%d parsed=%d items=%zu buttons=%zu menus=%zu\n", haveMenu, parsed, tree.items.size(), tree.buttons.size(), tree.menus.size());

  // Skin
  struct Sz { int w, h; } szs[] = {{800, 600}, {1280, 800}};
  for (auto sz : szs) {
    Skin s; bool l = s.load(gd, sz.h, sz.w);
    std::printf("== Skin %dx%d: loaded=%d images=%zu\n", sz.w, sz.h, l, s.imageCount());
    const char* names[] = {"rootpanel", "bottombar", "bottombar.cap", "rci", "speed.0", "speed.1", "help", "sub.1", "sub.2", "tool.2001",
                           "tool.3001", "button", "check", "radio", "menu.bg", "menu.btn.0"};
    for (const char* n : names) {
      const Image* im = s.get(n);
      std::printf("   %-14s %s", n, im ? "FOUND" : "missing");
      if (im) {
        std::printf(" %dx%d", im->w, im->h);
        if (!outDir.empty() && sz.h == 800) writePng((outDir / (std::string("skin_") + n + ".png")).string(), im->w, im->h, im->px);
      }
      std::printf("\n");
    }
    if (l) { ToolMenuLayout ly = s.layout(); std::printf("   layout heightClass=%d artGroup=%08X\n", ly.heightClass, ly.artGroup); }
  }
  // Main menu background candidates.
  int best = 0; uint32_t bi = 0;
  for (auto& k : imgs) if (k.group == 0x82B9B75C) {
    ResourceRef r = db.find(k);
    try { Image im = decodeUiImage(r.data, r.available, true); if (im.w * im.h > best) { best = im.w * im.h; bi = k.instance; } } catch (...) {}
  }
  std::printf("== Main menu image group 82B9B75C: largest image instance=%08X pixels=%d\n", bi, best);
  return 0;
}
