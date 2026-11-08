// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Builds a tiny synthetic "game install" on disk (SYS.PAK, string table, occupant attributes, building
// family, composite sprite record and sprites) so the data-driven code paths run without real game files.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/fsutil.h"
#include "core/image.h"
#include "core/ixf.h"
#include "core/refpack.h"

namespace fake {
using namespace m3k;

inline void writeFile(const fs::path& p, const Bytes& b) {
  fs::create_directories(p.parent_path());
  FILE* f = std::fopen(p.string().c_str(), "wb");
  if (f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
}
inline Bytes str(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline Bytes strRec(const std::string& s) { Bytes b; put32(b, uint32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); return b; }

// PAK of pre-parsed INI files: count, directory (nameLen, name, offset), bodies (lineCount, lines).
inline Bytes makePak(const std::vector<std::pair<std::string, std::vector<std::string>>>& files) {
  Bytes dir, bodies;
  size_t dirSize = 4;
  for (auto& f : files) dirSize += 4 + f.first.size() + 4;
  for (auto& f : files) {
    put32(dir, uint32_t(f.first.size())); dir.insert(dir.end(), f.first.begin(), f.first.end());
    put32(dir, uint32_t(dirSize + bodies.size()));
    put32(bodies, uint32_t(f.second.size()));
    for (auto& l : f.second) { put32(bodies, uint32_t(l.size())); bodies.insert(bodies.end(), l.begin(), l.end()); }
  }
  Bytes out; put32(out, uint32_t(files.size()));
  out.insert(out.end(), dir.begin(), dir.end()); out.insert(out.end(), bodies.begin(), bodies.end());
  return out;
}

// RLE sprite payload (type 0) for a w*h image of one colour (RGB565).
inline Bytes makeSprite(int w, int h, uint16_t colour) {
  Bytes rows, body; std::vector<uint16_t> stored;
  for (int y = 0; y < h; y++) {
    put32(rows, uint32_t(stored.size())); put16(rows, 0); put16(rows, uint16_t(w | 0x8000));
    for (int x = 0; x < w; x++) stored.push_back(colour);
  }
  put32(body, uint32_t(0x10 + rows.size() + stored.size() * 2));
  put16(body, uint16_t(w)); put16(body, uint16_t(h)); put16(body, 4); put16(body, 7); put16(body, 0xF81F); put16(body, 0);
  body.insert(body.end(), rows.begin(), rows.end());
  for (auto v : stored) put16(body, v);
  Bytes comp = refpackEncode(body);
  Bytes out = {7, 1, 0, 0};
  put32(out, 0x00080000); put32(out, uint32_t(w)); put32(out, uint32_t(h)); put32(out, uint32_t(comp.size() + 4));
  out.insert(out.end(), comp.begin(), comp.end());
  return out;
}

inline void tkbU32(Bytes& b, uint32_t id, std::vector<uint32_t> v) {
  put32(b, id); put16(b, 3); put16(b, uint16_t(v.size())); for (auto x : v) put32(b, x);
}
inline void tkbKey(Bytes& b, uint32_t id, uint32_t t, uint32_t g, uint32_t i) {
  put32(b, id); put16(b, 8); put16(b, 1); put32(b, t); put32(b, g); put32(b, i);
}

// Creates <root>/Apps/... and returns root. Occupant 100 (agent 3100, 1x1) in the residential family 4.
inline fs::path makeInstall(const fs::path& root) {
  fs::remove_all(root);
  fs::path apps = root / "Apps";
  std::vector<std::pair<std::string, std::vector<std::string>>> ini;
  ini.push_back({"Default.ini", {
      "[DBSegments]",
      "0x03c09aff=0x80c381d7;Res\\Text\\%Language%\\SC3StringsMenu.IXF",
      "0x0000057e=0x80c381d7;Res\\Occupant\\OccupantAttribs.IXF",
      "0x81F53D09=0x80c381d7;Res\\BUILDFAM.IXF",
      "0x00006400=0x80c381d7;Res\\Sprites\\CSATTRIB.IXF"}});
  ini.push_back({"SC3ZoneLayer.ini", {
      "[ZoneDeveloperDescriptions]", "1=0x619BA64E,3000:3100,1,255,0",
      "[ZoneDeveloperRules]", "1,0xA22653C0=81F53D09,01F58D3D,4"}});
  ini.push_back({"SC3ValveLayer.ini", {"[AgentValveEffects]", "3100=3000:10:0+7000:0:0"}});
  ini.push_back({"SC3PowerLayer.ini", {"[PowerPlantCapacities]", "12111=6000", "[CoalPowerPlant]", "MaxLifeSpan=600", "DeclineAge=480"}});
  ini.push_back({"CitySim.ini", {"[AgentTypes]", "3100=ResLowLV:0x2026960B,0x41F56C4C,1", "[AgentTypeHierarchy]", "3100=3000"}});
  ini.push_back({"MenuItem.INI", {"[Admin]", "x=1"}});
  writeFile(apps / "Sys" / "SYS.PAK", makePak(ini));
  { IxfWriter w; w.add({0x2026960B, 0x03C09AFF, 1}, strRec("Start new city")); writeFile(apps / "Res/Text/ENGLISH/SC3StringsApp.IXF", w.build());
    IxfWriter m; m.add({0x2026960B, 0x03C09AFF, 1}, strRec("Start new city")); writeFile(apps / "Res/Text/ENGLISH/SC3StringsMenu.IXF", m.build()); }
  { IxfWriter w; Bytes a = str("TKB1");
    tkbU32(a, 0x65, {3100}); tkbU32(a, 0x66, {1, 0, 1}); tkbKey(a, 0x67, 0x6300, 0x6400, 100); tkbU32(a, 0x72, {0x0200BEE2, 0});
    w.add({0x207EDC0E, 0x57E, 100}, a);
    writeFile(apps / "Res/Occupant/OccupantAttribs.IXF", w.build()); }
  { IxfWriter w; w.add({0x81F53D09, 0x01F58D3D, 4}, str("TXT! r\r\nLandUse: 3100\r\nBuildingCount: 1\r\nKey: 1406,545184782,100,1\r\n")); writeFile(apps / "Res/BUILDFAM.IXF", w.build()); }
  { Bytes cs = str("BIN\r"); put32(cs, 1); put32(cs, 0x7300); cs.push_back(5); cs.push_back(4); cs.push_back(4); cs.push_back(20); cs.push_back(1); cs.push_back(0);
    put32(cs, 0); put16(cs, 20);
    for (int k = 0; k < 20; k++) { put16(cs, uint16_t(k)); put32(cs, 2); put32(cs, (100u << 16) | uint32_t(k)); cs.push_back(0); put16(cs, 0); put16(cs, 0); }
    IxfWriter w; w.add({0x6300, 0x6400, 100}, cs); writeFile(apps / "Res/Sprites/CSATTRIB.IXF", w.build()); }
  { IxfWriter w;
    for (int k = 0; k < 20; k++) {
      int zoom = k / 4, width = 8 << zoom;
      w.add({0, 2, (100u << 16) | uint32_t(k)}, makeSprite(width, width / 2 + 8, 0x07E0));
      Bytes span; put16(span, 0); put16(span, uint16_t(width / 2 + 8)); put16(span, uint16_t(width)); put16(span, 0);
      w.add({1, 2, (100u << 16) | uint32_t(k)}, span);
    }
    writeFile(apps / "Res/Sprites/00000002_Residential.DAT", w.build()); }
  fs::create_directories(apps / "Res/Dirt/Palette");
  fs::create_directories(root / "Cities");
  return root;
}
}  // namespace fake
