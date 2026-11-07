// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One pass/fail harness for the checks that need a real install of the original game (issues #70, #52, #57, #64, #65, #66).
// It runs on a machine that owns the install; CI only builds it.
//
//   probe_verify GAME_DIR [--ref REFDIR] [--only GROUP[,GROUP...]] [--report out.tsv] [--shots OUTDIR]
//
// Groups: l0 (runs the sibling `conformance --strict`), l1 (save round trips), l2 (network re-tiling vs stored pieces),
// audio (every .xa decodes and is not silent, every .FSC parses), scn (every .SNR scenario loads and runs, the goals window and
// the end screen open), l3 (480-day runs vs the observed saves in REFDIR, files named W5_<City>_sNN.sc3), terrain (REFDIR/QBS_*.sc3
// with a sibling QBS_*.txt of key=value lines seed, difficulty, hills, water, trees, flags, florarng (optional): our generator vs the original's terrain),
// shots. `--skip P1,P2` ignores saves whose file name starts with a prefix (default W5_,Obs,T_,QBS_: files the player or the
// observation runs added to the install).
// Prints one PASS / FAIL / INFO row per check (name, found, expected) and exits 1 when any check FAILs. `--report` also writes the
// rows as tab-separated text. `--shots OUTDIR` renders the fixed scene list of spec 28 6 at 800x600, 1024x768 and 1280x800 to PNG
// files plus manifest.tsv (no pass/fail: compare them with screenshots of the original).
// Thresholds named "baseline" are regression floors taken from earlier measurements on the shipped data, not original values.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include "core/audio.h"
#include "core/gamedata.h"
#include "core/image.h"
#include "core/terrain_gen.h"
#include "core/tiler.h"
#include "core/world_network_map.h"
#include "core/xa_codec.h"
#include "probe_common.h"
#include "../png.h"
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
using namespace probe;

namespace {

struct Row { std::string group, name, found, expected, status; };
std::vector<Row> g_rows;
std::string g_group;
std::vector<std::string> g_skip = {"W5_", "Obs", "T_", "QBS_"};
bool skipped(const fs::path& p) { const std::string n = p.filename().string(); for (const auto& k : g_skip) if (n.compare(0, k.size(), k) == 0) return true; return false; }

void row(const std::string& name, const std::string& found, const std::string& expected, bool pass) {
  g_rows.push_back({g_group, name, found, expected, pass ? "PASS" : "FAIL"});
  std::printf("%-5s %-8s %-62s found %-28s expected %s\n", pass ? "PASS" : "FAIL", g_group.c_str(), name.c_str(), found.c_str(), expected.c_str());
  std::fflush(stdout);
}
void info(const std::string& name, const std::string& found) {
  g_rows.push_back({g_group, name, found, "-", "INFO"});
  std::printf("INFO  %-8s %-62s %s\n", g_group.c_str(), name.c_str(), found.c_str());
}
std::string str(long v) { return std::to_string(v); }
std::string pct(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f%%", v); return b; }

bool readFileBytes(const fs::path& p, Bytes& out) {
  FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  out.resize(size_t(std::max(0L, n)));
  size_t got = n > 0 ? std::fread(out.data(), 1, size_t(n), f) : 0;
  std::fclose(f);
  return got == out.size();
}

std::vector<fs::path> filesWithExt(const fs::path& root, std::initializer_list<const char*> exts) {
  std::vector<fs::path> v;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file()) continue;
    const std::string e = lower(it->path().extension().string());
    for (const char* x : exts) if (e == x) { v.push_back(it->path()); break; }
  }
  std::sort(v.begin(), v.end());
  return v;
}

// ---------------------------------------------------------------- l0
void groupL0(const fs::path& root, const char* argv0) {
  fs::path exe = fs::path(argv0).parent_path() / "conformance";
  if (!fs::exists(exe)) { row("conformance tool next to probe_verify", "missing", "present", false); return; }
  const std::string cmd = "\"" + exe.string() + "\" --strict --allow-extra \"" + root.string() + "\" 2>&1";
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { row("run conformance", "popen failed", "ok", false); return; }
  std::string out; char buf[4096];
  while (size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  const int rc = pclose(p);
  std::istringstream is(out);
  std::string line; int ok = 0, bad = 0;
  while (std::getline(is, line)) {
    size_t e = line.find_last_not_of(" \r\n");
    if (e == std::string::npos) continue;
    size_t s = line.find_last_of(' ', e);
    const std::string last = line.substr(s == std::string::npos ? 0 : s + 1, e - (s == std::string::npos ? 0 : s));
    if (last == "ok") ok++;
    else if (last == "DIFFERS") { bad++; row(line.substr(0, std::min<size_t>(line.size(), 100)), "DIFFERS", "ok", false); }
  }
  row("conformance rows ok (spec 28 s2 counts, L1 .bld/BAT.DAT/SNR/cfg round trips)", str(ok) + " ok / " + str(bad) + " differ", "0 differ", bad == 0);
  row("conformance --strict exit status", str(rc), "0", rc == 0);
}

// ---------------------------------------------------------------- l1
// A save loads, re-saves, re-loads and re-saves again: the world survives (same grids, occupants, networks) and the second save
// equals the first. Byte equality with the shipped file is reported as info (compression bytes may differ, spec 28 s3).
void groupL1(const fs::path& root) {
  int total = 0, stable = 0, sameWorld = 0, identical = 0;
  for (const fs::path& p : filesWithExt(root, {".sc3", ".st3", ".sct"})) {
    Bytes file;
    if (skipped(p) || !readFileBytes(p, file)) continue;
    total++;
    try {
      World a = loadCity(file);
      Bytes s1 = saveCity(a);
      World b = loadCity(s1);
      Bytes s2 = saveCity(b);
      const bool same = a.W == b.W && a.H == b.H && a.alt == b.alt && a.water == b.water && a.zone == b.zone && a.flora == b.flora &&
                        a.occupants.size() == b.occupants.size() && a.surface.size() == b.surface.size() && a.pipes.size() == b.pipes.size() &&
                        a.subway.size() == b.subway.size();
      sameWorld += same;
      stable += s1 == s2;
      identical += s1 == file;
      if (!same || s1 != s2) row("round trip " + p.filename().string(), same ? "second save differs" : "world differs", "stable", false);
    } catch (const std::exception& e) {
      row("round trip " + p.filename().string(), std::string("throws: ") + e.what(), "loads", false);
    }
  }
  row("saves whose world survives load - save - load", str(sameWorld) + " of " + str(total), "all", total > 0 && sameWorld == total);
  row("saves whose second save equals the first", str(stable) + " of " + str(total), "all", total > 0 && stable == total);
  info("saves byte-identical to the shipped file", str(identical) + " of " + str(total) + " (the rest should differ in compression bytes only; spec 28 s3)");
}

// ---------------------------------------------------------------- l2
void groupL2(const fs::path& root) {
  Session s;
  if (!s.open(root)) { row("open install", "failed", "ok", false); return; }
  const char* kindName[] = {"", "road", "rail", "power"};
  const double floorPct[] = {0, 98.5, 98.5, 75.0};   // baseline: measured on the shipped cities (road 99.1-99.7, rail 99.2-99.9, power 78-86)
  std::vector<std::pair<long, long>> agg(4);
  int cities = 0;
  for (const fs::path& path : findCities(root, {})) {
    if (skipped(path)) continue;
    if (!s.load(path)) { row("load " + path.filename().string(), "failed", "loads", false); continue; }
    cities++;
    const TilingRuleSet& rules = s.game.city().config().tiling;
    const World& w = s.game.world();
    auto flags = flagsFromRuleSets(rules);
    for (int kind = 1; kind <= 3; kind++) {
      World copy = w;
      WorldNetworkMap map(copy, flags);
      std::vector<PathTile> drawn;
      std::map<std::pair<int, int>, NetPiece> stored;
      for (const auto& p : w.surface) {
        if (!rules.inSet(netKindToTable(kind), p.pieceId)) continue;
        PathTile t; t.x = p.x; t.z = p.z; drawn.push_back(t); stored[{p.x, p.z}] = p;
      }
      if (stored.empty()) continue;
      Tiler(rules, map).run(kind, drawn);
      std::map<std::pair<int, int>, NetPiece> now;
      for (const auto& p : copy.surface) now[{p.x, p.z}] = p;
      long same = 0;
      for (const auto& kv : stored) {
        auto it = now.find(kv.first);
        if (it != now.end() && it->second.pieceId == kv.second.pieceId && it->second.rot == kv.second.rot) same++;
      }
      agg[size_t(kind)].first += same; agg[size_t(kind)].second += long(stored.size());
      info(std::string("re-tile ") + kindName[kind] + " " + path.filename().string(), str(same) + " of " + str(long(stored.size())));
    }
  }
  for (int kind = 1; kind <= 3; kind++) {
    const double v = agg[size_t(kind)].second ? 100.0 * double(agg[size_t(kind)].first) / double(agg[size_t(kind)].second) : 0.0;
    row(std::string("re-tile ") + kindName[kind] + " pieces identical to the stored ones, all cities (baseline floor)", pct(v), ">= " + pct(floorPct[kind]),
        cities > 0 && v >= floorPct[kind]);
  }
}

// ---------------------------------------------------------------- audio
void groupAudio(const fs::path& root) {
  int xa = 0, xaBad = 0, silent = 0;
  for (const fs::path& p : filesWithExt(root, {".xa"})) {
    Bytes data; PcmBuffer pcm;
    xa++;
    if (!readFileBytes(p, data) || !decodeXa(data, pcm, true)) { xaBad++; row("decode " + p.filename().string(), "fails", "decodes", false); continue; }
    int peak = 0;
    for (int16_t v : pcm.samples) peak = std::max(peak, std::abs(int(v)));
    if (peak < 100) { silent++; row("decode " + p.filename().string(), "silent (peak " + str(peak) + ")", "audible", false); }
  }
  row(".xa files found (spec 28 s2)", str(xa), "814", xa == 814);
  row(".xa files that decode and are not silent", str(xa - xaBad - silent) + " of " + str(xa), "all", xa > 0 && xaBad == 0 && silent == 0);
  int fsc = 0, fscOk = 0;
  for (const fs::path& p : filesWithExt(root, {".fsc"})) {
    Bytes data; FscSchedule sch;
    fsc++;
    const bool ok = readFileBytes(p, data) && FscSchedule::parse(std::string(data.begin(), data.end()), sch) && sch.rows > 0 && sch.cols > 0 &&
                    sch.cells.size() == size_t(sch.rows) * size_t(sch.cols);
    fscOk += ok;
    if (!ok) row("parse " + p.filename().string(), "fails", "parses", false);
  }
  row(".FSC schedules that parse", str(fscOk) + " of " + str(fsc), "all (25 shipped)", fsc > 0 && fscOk == fsc);
}

// ---------------------------------------------------------------- scn
void groupScenarios(const fs::path& root) {
  Session s;
  if (!s.open(root)) { row("open install", "failed", "ok", false); return; }
  int total = 0, loaded = 0, active = 0, ran = 0, goals = 0, endScreen = 0;
  for (const fs::path& p : filesWithExt(root, {".snr"})) {
    total++;
    const std::string n = p.filename().string();
    if (!s.game.loadScenarioFile(p)) { row("load scenario " + n, "fails", "loads", false); continue; }
    loaded++;
    s.frames(3);
    if (s.game.scenarioActive()) active++; else row("scenario " + n + " active", "no", "yes", false);
    s.game.setPaused(false);
    s.frames(80, 200);
    if (s.game.currentScreen() == Screen::InCity || s.game.currentScreen() == Screen::ScenarioEnd) ran++;
    else row("scenario " + n + " runs 80 frames", "screen " + str(int(s.game.currentScreen())), "in city", false);
    s.game.openWindow("goals"); s.frames(4);
    if (s.game.windowOpen("goals")) goals++; else row("scenario " + n + " goals window", "does not open", "opens", false);
    s.game.gotoScreen(Screen::ScenarioEnd); s.frames(4);
    if (s.game.currentScreen() == Screen::ScenarioEnd) endScreen++;
    s.game.gotoScreen(Screen::InCity); s.frames(2);
  }
  row("scenario files (spec 28 s2)", str(total), "13", total == 13);
  row("scenarios that load through Game::loadScenarioFile", str(loaded) + " of " + str(total), "all", total > 0 && loaded == total);
  row("scenarios reported active", str(active) + " of " + str(total), "all", total > 0 && active == total);
  row("scenarios that run 80 frames of simulation", str(ran) + " of " + str(total), "all", total > 0 && ran == total);
  row("goals window opens", str(goals) + " of " + str(total), "all", total > 0 && goals == total);
  row("end screen draws", str(endScreen) + " of " + str(total), "all", total > 0 && endScreen == total);
}

// ---------------------------------------------------------------- l3
int absDay(const m3k::cal::Date& d) { return d.year * 400 + d.month * 32 + d.day; }

// Run each shipped city to the date of the last observed save of the same city and compare the population with the original's
// (spec 28 s5.3: a deviation over 10 % calls for investigation). One run per city; the observed saves are named W5_<City>_sNN.sc3.
void groupL3(const fs::path& root, const fs::path& ref) {
  if (ref.empty() || !fs::is_directory(ref)) { info("observed saves", "no --ref directory given: l3 skipped"); return; }
  std::map<std::string, fs::path> last;
  for (const auto& e : fs::directory_iterator(ref)) {
    const std::string n = e.path().filename().string();
    if (n.compare(0, 3, "W5_") != 0 || lower(e.path().extension().string()) != ".sc3") continue;
    const size_t us = n.rfind("_s");
    if (us == std::string::npos || us < 3) continue;
    const std::string city = n.substr(3, us - 3);
    if (!last.count(city) || last[city].filename().string() < n) last[city] = e.path();
  }
  if (last.empty()) { info("observed saves", "none named W5_<City>_sNN.sc3 in " + ref.string()); return; }
  for (const auto& kv : last) {
    auto shipped = findCities(root, {kv.first});
    if (shipped.empty()) { row("shipped city " + kv.first, "missing", "present", false); continue; }
    Session theirs, ours;
    if (!theirs.open(root) || !theirs.load(kv.second)) { row("load observed " + kv.second.filename().string(), "fails", "loads", false); continue; }
    if (!ours.open(root) || !ours.load(shipped[0])) { row("load shipped " + kv.first, "fails", "loads", false); continue; }
    const int target = absDay(theirs.game.city().clock.date());
    const int observedPop = theirs.game.sim().population();
    // The original's population is not monotonic (water-pump outages dip it, e.g. Madrid): compare with the band of the last three
    // observed saves (spec 28 s5.3: inside the original's min-max band, 10 % tolerance), not with one point.
    int lo = observedPop, hi = observedPop;
    {
      std::vector<std::string> names;
      for (const auto& e2 : fs::directory_iterator(ref)) {
        const std::string n2 = e2.path().filename().string();
        if (n2.compare(0, 3 + kv.first.size() + 2, "W5_" + kv.first + "_s") == 0 && lower(e2.path().extension().string()) == ".sc3") names.push_back(n2);
      }
      std::sort(names.begin(), names.end());
      for (size_t k = names.size() > 3 ? names.size() - 3 : 0; k < names.size(); k++) {
        Session other;
        if (other.open(root) && other.load(ref / names[k])) { const int p2 = other.game.sim().population(); lo = std::min(lo, p2); hi = std::max(hi, p2); }
      }
    }
    ours.game.city().sched.isHold = nullptr;   // a loaded city is paused; tickDay would wait for the hold forever on this single thread
    int days = 0;
    while (absDay(ours.game.city().clock.date()) < target && days < 800) { ours.game.sim().tickDay(); days++; }
    const int ourPop = ours.game.sim().population();
    const bool pass = double(ourPop) >= lo * 0.9 && double(ourPop) <= hi * 1.1;
    const double dev = observedPop > 0 ? 100.0 * (double(ourPop) - observedPop) / observedPop : 100.0;
    row("population after " + str(days) + " days, " + kv.first + " (ours " + str(ourPop) + ", original " + str(lo) + ".." + str(hi) + ")",
        pct(dev) + " vs the last save", "inside the last-3-saves band +-10%", pass);
  }
}

// ---------------------------------------------------------------- ordw
// Evidence for the open quantity W behind the world-based ordinance costs (spec 19 s9.5: Berlin fits W in [25200, 25892], a new city 0) and the
// raw world-layer history charts (spec 10 s6.13). Informational: prints candidate counts per shipped city and the five world charts' words.
void groupOrdW(const fs::path& root) {
  {   // the View Data window's UI images: do they decode, and what does the Aura legend ramp 5E5:09 hold
    GameData gd; std::string err;
    if (gd.open(root, err)) {
      for (uint32_t id : {0x01u, 0x03u, 0x04u, 0x07u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Fu, 0x10u, 0x11u, 0x15u, 0x17u, 0x19u, 0x31u, 0x33u, 0x35u}) {
        ResourceRef rr = gd.db().find({T_UI_IMAGE, 0x5E5, id});
        std::string what = "missing";
        if (rr) { try { Image im = decodeUiImage(rr.data, rr.available, true); what = str(im.w) + "x" + str(im.h);
            if ((id == 0x09 || id == 0x0A) && im.w > 0 && im.h > 1) {   // every row of the legend gradient (middle column) as RRGGBB, to compare with the original's pixels
              std::string all; char q[16];
              for (int y = 0; y < im.h; y++) { const uint32_t p = im.px[size_t(y) * im.w + size_t(im.w / 2)]; std::snprintf(q, sizeof q, "%02X%02X%02X ", p & 255, (p >> 8) & 255, (p >> 16) & 255); all += q; }
              info("ordw UI image 000005E5:" + str(long(id)) + " middle column rows 0.." + str(im.h - 1) + " RRGGBB", all); }
            if (id == 0x09 && im.w > 0 && im.h > 1) { char b[64]; for (int y : {0, im.h / 4, im.h / 2, 3 * im.h / 4, im.h - 1}) { const uint32_t p = im.px[size_t(y) * im.w + size_t(im.w / 2)]; std::snprintf(b, sizeof b, " row%d=(%u,%u,%u,a%u)", y, p & 255, (p >> 8) & 255, (p >> 16) & 255, p >> 24); what += b; } } }
          catch (const FormatError&) { what = "decode fails"; } }
        info("ordw UI image 000005E5:" + str(long(id)), what);
      }
      // Map ramp 5E5:21: dump rows and locate the original's View Data base colours (land (148,166,132), water (132,130,181)).
      if (ResourceRef rr = gd.db().find({T_UI_IMAGE, 0x5E5, 0x21})) {
        try {
          Image im = decodeUiImage(rr.data, rr.available, false);
          const uint32_t tg[2] = {0x84A694u, 0xB58284u};   // 0xBBGGRR of (148,166,132) and (132,130,181)
          for (int t = 0; t < 2; t++) {
            int bd = 1 << 30, bx = 0, by = 0;
            for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) {
              const uint32_t p = im.px[size_t(y) * im.w + x];
              const int d = std::abs(int(p & 255) - int(tg[t] & 255)) + std::abs(int((p >> 8) & 255) - int((tg[t] >> 8) & 255)) + std::abs(int((p >> 16) & 255) - int((tg[t] >> 16) & 255));
              if (d < bd) { bd = d; bx = x; by = y; }
            }
            info(std::string("ordw 5E5:21 nearest to ") + (t ? "water (132,130,181)" : "land (148,166,132)"), "column " + str(bx) + " row " + str(by) + ", L1 distance " + str(bd));
          }
          for (int y = 0; y < std::min(im.h, 8); y++) {
            std::string line; char b[16];
            for (int x = 0; x < std::min(im.w, 12); x++) { const uint32_t p = im.px[size_t(y) * im.w + x]; std::snprintf(b, sizeof b, "%02X%02X%02X ", p & 255, (p >> 8) & 255, (p >> 16) & 255); line += b; }
            info("ordw 5E5:21 row " + str(y) + " columns 0..11 RRGGBB", line);
          }
        } catch (const FormatError&) {}
      }
    }
  }
  for (const fs::path& path : findCities(root, {})) {
    if (skipped(path)) continue;
    Session s;
    if (!s.open(root) || !s.load(path)) continue;
    const World& w = s.game.world();
    {   // train sources (spec 26 5.1): rail end caps (agent 17002) and subway end caps (17420) among the network pieces, and the stations
      static GameData gd2; static bool open2 = false; std::string e2;
      if (!open2) open2 = gd2.open(root, e2);
      if (open2) {
        long rail = 0, sub = 0, stations = 0;
        for (const auto& p : w.surface) if (const OccAttr* a = gd2.occupant(p.pieceId, true)) rail += a->agentType == 17002;
        for (const auto& p : w.subway) if (const OccAttr* a = gd2.occupant(p.pieceId, true)) sub += a->agentType == 17420;
        for (const auto& o : w.occupants) if (const OccAttr* a = gd2.occupant(o.occId)) stations += a->agentType == 12230 || a->agentType == 12360 || a->agentType == 12361;
        info("ordw " + path.filename().string() + " train sources", "rail end caps (17002) " + str(rail) + ", subway end caps (17420) " + str(sub) + ", stations (12230/12360/12361) " + str(stations) + ", surface pieces " + str(long(w.surface.size())) + ", subway pieces " + str(long(w.subway.size())));
      }
    }
    long occ = long(w.occupants.size()), filler = 0, occupied = 0, footprint = 0, footprintNoFiller = 0, nonLot = 0;
    for (const auto& o : w.occupants) {
      const bool f = (o.buildingFlags & 0x100) != 0;
      filler += f; occupied += (o.state & 0xFF) == 1;
      footprint += long(o.sizeX) * o.sizeZ;
      if (!f) footprintNoFiller += long(o.sizeX) * o.sizeZ;
      nonLot += !f && o.sizeX * o.sizeZ == 1;
    }
    long zoned = 0; for (uint8_t z : w.zone) zoned += z != 0;
    const std::string n = path.filename().string();
    info("ordw " + n, "pop " + str(s.game.sim().population()) + ", occupants " + str(occ) + ", filler " + str(filler) + ", occupied " + str(occupied) +
         ", non-filler " + str(occ - filler) + ", footprint tiles " + str(footprint) + ", non-filler footprint " + str(footprintNoFiller) +
         ", zoned tiles " + str(zoned) + ", 1x1 non-filler " + str(nonLot) + ", surface pieces " + str(long(w.surface.size())));
    if (n.compare(0, 6, "Berlin") == 0) {   // coverage circle centres of the original's View Data window (window-relative pixels, 256 tiles per edge)
      // Least-squares refined centres of the original's rings (police r = 31.4 tiles, fire r = 27.6 tiles).
      static const float pol[11][2] = {{91.9f, 231.1f}, {183.0f, 174.0f}, {219.0f, 153.0f}, {194.0f, 237.3f}, {239.0f, 220.0f}, {224.2f, 330.1f}, {145.0f, 197.8f}, {183.0f, 302.8f}, {326.0f, 238.0f}, {156.3f, 260.2f}, {263.0f, 291.0f}};
      static const float fir[11][2] = {{135.0f, 230.0f}, {186.2f, 264.8f}, {165.9f, 316.1f}, {312.0f, 190.0f}, {216.0f, 129.0f}, {99.0f, 223.0f}, {163.0f, 199.0f}, {256.0f, 215.0f}, {214.9f, 295.8f}, {207.0f, 342.0f}, {184.0f, 142.0f}};
      for (int layer = 0; layer < 2; layer++) {
        const auto& cov = layer == 0 ? s.game.city().police : s.game.city().fire;
        const auto circles = cov.stationCircles();
        { std::string rs; for (const auto& c : circles) rs += "R" + str(c.R) + "/side" + str(c.side) + " ";
          info(std::string("ordw ") + n + (layer == 0 ? " police" : " fire") + " funding " + str(cov.funding()) + "%, stamp R / box side per station", rs); }
        for (int swap = 0; swap < 2; swap++) {
          int hit1 = 0, hit2 = 0; double sum = 0, sdx = 0, sdz = 0;
          for (int k = 0; k < 11; k++) {
            const float u = (layer == 0 ? pol : fir)[k][0] - 230.f, v = (layer == 0 ? pol : fir)[k][1] - 47.f;
            float x = (u / 0.734f + v / 0.730f) / 2.f, z = (v / 0.730f - u / 0.734f) / 2.f;
            if (swap) std::swap(x, z);
            double best = 1e9, bdx = 0, bdz = 0;
            for (const auto& c : circles) { const double d = std::hypot(double(c.x - x), double(c.z - z)); if (d < best) { best = d; bdx = double(x - c.x); bdz = double(z - c.z); } }
            hit1 += best <= 2.0; hit2 += best <= 4.0; sum += best; sdx += bdx; sdz += bdz;   // dx, dz = observed circle centre - our station centre
          }
          info(std::string("ordw ") + n + (layer == 0 ? " police" : " fire") + " circle centres, mapping " + (swap ? "(b) x/z swapped" : "(a)"),
               str(hit1) + "/11 within 2 tiles, " + str(hit2) + "/11 within 4, mean distance " + std::to_string(std::lround(sum * 100.0 / 11.0) / 100.0).substr(0, 5) + " tiles, signed mean dx " + std::to_string(std::lround(sdx * 100.0 / 11.0) / 100.0).substr(0, 5) + " dz " + std::to_string(std::lround(sdz * 100.0 / 11.0) / 100.0).substr(0, 5) + " (tiles, x/z of the mapping), stations " + str(long(circles.size())));
        }
      }
    }
    if (n.compare(0, 6, "Berlin") == 0) {   // palette-column histograms of our View Data maps, to compare with the original's (Crime cols 0-9, Land Value cols 11-31 peaking at 31)
      static const struct { DataLayer l; const char* name; bool sentinel; } maps[] = {{DataLayer::Aura, "Aura", true}, {DataLayer::Crime, "Crime", true}, {DataLayer::Density, "Density", false},
        {DataLayer::Fire, "Flammability", true}, {DataLayer::LandValue, "Land Value", true}, {DataLayer::Pollution, "Pollution", false}, {DataLayer::Traffic, "Traffic", true}};
      for (const auto& m : maps) {
        const std::vector<uint8_t> v = s.game.dataMapValues(m.l);
        if (v.empty()) { info("ordw " + n + " " + m.name + " palette columns", "layer has no data"); continue; }
        long hist[32] = {}, tinted = 0;
        for (uint8_t val : v) { if (m.sentinel ? val == 0xFF : val == 0) continue; int row, col; dataMapPaletteCell(m.l, val, row, col); hist[col]++; tinted++; }
        if (m.l == DataLayer::Density) {   // the raw values behind the Density columns: distinct byte values with counts
          std::map<int, long> raw; for (uint8_t val : v) if (val) raw[val]++;
          std::string rs; for (const auto& kv : raw) rs += str(kv.first) + ":" + str(kv.second) + " ";
          info("ordw " + n + " Density raw byte values (value:tiles)", rs);
        }
        std::string hs; for (int c = 0; c < 32; c++) hs += str(hist[c]) + " ";
        info("ordw " + n + " " + m.name + " palette columns 0..31 (tiles per column)", "tinted " + str(tinted) + " of " + str(long(v.size())) + ": " + hs);
      }
    }
    if (n.compare(0, 6, "Berlin") == 0) {   // aura over developed tiles (what the View Data Aura map paints) and the legend ramp image
      std::map<int, long> hist; long cnt = 0; double sum = 0; int mn = 255, mx = 0;
      const City& c = s.game.city();
      for (int x = 0; x < w.W; x++) for (int z = 0; z < w.H; z++) if (c.occupantAt(x, z) >= 0) { const int a = c.aura.aura(x, z); hist[a / 16]++; cnt++; sum += a; mn = std::min(mn, a + 128); mx = std::max(mx, a + 128); }
      std::string hs; for (const auto& kv : hist) hs += str(kv.first * 16) + ":" + str(kv.second) + " ";
      info("ordw " + n + " aura over developed tiles", "n " + str(cnt) + ", mean " + std::to_string(cnt ? sum / double(cnt) : 0.0).substr(0, 6) + ", byte range " + str(mn) + ".." + str(mx) + ", histogram (aura/16 : tiles) " + hs);
    }
    const auto& wc = s.game.city().valves.world.charts;
    for (int k = 0; k < 5; k++) {
      std::string line;
      for (int i = 0; i < 33; i++) line += (i ? " " : "") + str(long(wc[size_t(k)].w[size_t(i)]));
      info("ordw " + n + " world chart " + str(k), line);
    }
  }
}

// ---------------------------------------------------------------- terrain
// Our generator against a terrain the original generated (issue #52): altitude, water surface and sea level must match exactly;
// the flora layer is compared too and, when it differs, broken down so the cause can be found.
void groupTerrain(const fs::path& ref) {
  if (ref.empty() || !fs::is_directory(ref)) { info("terrain references", "no --ref directory given: terrain skipped"); return; }
  int n = 0;
  for (const auto& e : fs::directory_iterator(ref)) {
    const std::string name = e.path().filename().string();
    if (name.compare(0, 4, "QBS_") != 0 || lower(e.path().extension().string()) != ".sc3") continue;
    fs::path txt = e.path(); txt.replace_extension(".txt");
    Bytes param, file;
    if (!readFileBytes(txt, param) || !readFileBytes(e.path(), file)) { row("terrain " + name, "no sibling .txt or unreadable", "seed/difficulty/hills/water/trees/flags", false); continue; }
    std::map<std::string, long> kv;
    { std::istringstream is(std::string(param.begin(), param.end())); std::string line;
      while (std::getline(is, line)) { const size_t q = line.find('='); if (q != std::string::npos) kv[line.substr(0, q)] = std::strtol(line.c_str() + q + 1, nullptr, 0); } }
    n++;
    World ref0;
    try { ref0 = loadCity(file); } catch (const std::exception& ex) { row("terrain " + name, std::string("load throws: ") + ex.what(), "loads", false); continue; }
    TerrainGenOptions o;
    o.seed = uint32_t(kv.count("seed") ? kv["seed"] : 1); o.difficulty = int(kv.count("difficulty") ? kv["difficulty"] : 1);
    o.hills = int(kv.count("hills") ? kv["hills"] : 64); o.water = int(kv.count("water") ? kv["water"] : 64);
    o.trees = int(kv.count("trees") ? kv["trees"] : 64); o.flags = int(kv.count("flags") ? kv["flags"] : 0x24);
    if (kv.count("florarng")) o.floraRng = uint32_t(kv["florarng"]);   // low 17 bits of the flora generator at the start of the apply step
    World gen; generateTerrain(gen, ref0.W, ref0.H, o);
    long altSame = 0, watSame = 0;
    const size_t vn = std::min(gen.alt.size(), ref0.alt.size());
    for (size_t i = 0; i < vn; i++) { altSame += gen.alt[i] == ref0.alt[i]; watSame += gen.water[i] == ref0.water[i]; }
    row("terrain " + name + " altitude grid", str(altSame) + " of " + str(long(ref0.alt.size())), "all", gen.alt.size() == ref0.alt.size() && altSame == long(ref0.alt.size()));
    row("terrain " + name + " water surface grid", str(watSame) + " of " + str(long(ref0.water.size())), "all", gen.water.size() == ref0.water.size() && watSame == long(ref0.water.size()));
    row("terrain " + name + " sea level", str(gen.seaLevel) + " vs " + str(ref0.seaLevel), "equal", gen.seaLevel == ref0.seaLevel);
    long same = 0, oursOnly = 0, theirsOnly = 0, kindDiff = 0;
    std::map<std::pair<int, int>, long> confusion;
    std::map<std::string, long> oursOnlyWhere;
    std::vector<std::string> sample;
    const size_t tn = std::min(gen.flora.size(), ref0.flora.size());
    for (size_t i = 0; i < tn; i++) {
      const int a = gen.flora[i], b = ref0.flora[i];
      if (a == b) { same++; continue; }
      confusion[{a, b}]++;
      const int x = int(i) / ref0.H, z = int(i) % ref0.H;
      if (a && !b) {
        oursOnly++;
        int lo = 255, hi = 0, wet = 0;
        for (int d = 0; d < 4; d++) { const int vx = x + (d & 1), vz = z + (d >> 1); const int al = ref0.alt[ref0.vIdx(vx, vz)]; lo = std::min(lo, al); hi = std::max(hi, al); wet += ref0.water[ref0.vIdx(vx, vz)] > al; }
        oursOnlyWhere[wet ? "a corner under water" : (hi - lo) > 3 ? "slope > 3" : (x == 0 || z == 0 || x == ref0.W - 1 || z == ref0.H - 1) ? "map edge" : "flat dry land"]++;
      } else if (!a && b) theirsOnly++; else kindDiff++;
      if (sample.size() < 8) sample.push_back("(" + str(x) + "," + str(z) + ") ours " + str(a) + " theirs " + str(b));
    }
    row("terrain " + name + " flora layer", str(same) + " of " + str(long(ref0.flora.size())) + " cells equal", "all", gen.flora.size() == ref0.flora.size() && same == long(ref0.flora.size()));
    if (same != long(ref0.flora.size())) {
      info("flora differences", "ours only " + str(oursOnly) + ", original only " + str(theirsOnly) + ", both but different cell " + str(kindDiff));
      for (const auto& w : oursOnlyWhere) info("  ours-only cells on: " + w.first, str(w.second));
      std::vector<std::pair<long, std::pair<int, int>>> top;
      for (const auto& c : confusion) top.push_back({c.second, c.first});
      std::sort(top.rbegin(), top.rend());
      for (size_t i = 0; i < top.size() && i < 10; i++) info("  cell ours -> original", str(top[i].second.first) + " -> " + str(top[i].second.second) + " : " + str(top[i].first));
      for (const auto& sm : sample) info("  sample", sm);
    }
  }
  if (n == 0) info("terrain references", "no QBS_*.sc3 + QBS_*.txt in " + ref.string());
}

// ---------------------------------------------------------------- shots
void groupShots(const fs::path& root, const fs::path& outDir) {
  std::error_code ec;
  fs::create_directories(outDir, ec);
  std::string manifest = "file\tsize\tscene\n";
  const std::pair<int, int> sizes[] = {{800, 600}, {1024, 768}, {1280, 800}};
  auto cities = findCities(root, {"Berlin"});
  int written = 0;
  for (auto sz : sizes) {
    const int W = sz.first, H = sz.second;
    Game g; g.setPersistence(false); g.setIntroEnabled(false);
    std::string err;
    if (!g.init(root, err)) { row("render shots " + str(W) + "x" + str(H), "init failed: " + err, "ok", false); continue; }
    Input in; in.mx = W / 2; in.my = H / 3;
    auto frame = [&](int n) { for (int i = 0; i < n; i++) { g.frame(in, W, H, 16); in.pressed = in.released = in.rightPressed = false; in.text.clear(); for (auto& k : in.key) k = false; } };
    auto shot = [&](const std::string& scene) {
      std::string f = scene + "_" + str(W) + "x" + str(H) + ".png";
      writePng((outDir / f).string(), W, H, g.screen());
      manifest += f + "\t" + str(W) + "x" + str(H) + "\t" + scene + "\n";
      written++;
    };
    frame(3);
    shot("main_menu");
    if (!cities.empty()) { g.requestFile(cities[0].string()); frame(10); }
    shot("in_city");
    const char* wins[] = {"budget", "ordinances", "neighbours", "deals", "advisors", "map", "graphs", "charts", "prefs", "layers", "engine", "goals"};
    for (const char* w : wins) {
      g.closeAllWindows(); g.gotoScreen(Screen::InCity); frame(2);   // every window from the same plain city view
      g.openWindow(w); frame(4);
      shot(std::string("window_") + w);
    }
  }
  std::FILE* f = std::fopen((outDir / "manifest.tsv").string().c_str(), "wb");
  if (f) { std::fwrite(manifest.data(), 1, manifest.size(), f); std::fclose(f); }
  info("screenshots written", str(written) + " PNG files + manifest.tsv in " + outDir.string());
}

}  // namespace

int main(int argc, char** argv) {
  fs::path root, ref, shotsDir, report;
  std::set<std::string> only;
  int used = gameDirFromArgs(argc, argv, root);
  for (int i = 1 + used; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--ref") ref = next();
    else if (a == "--report") report = next();
    else if (a == "--shots") shotsDir = next();
    else if (a == "--skip") { g_skip.clear(); std::istringstream is(next()); std::string g; while (std::getline(is, g, ',')) if (!g.empty()) g_skip.push_back(g); }
    else if (a == "--only") { std::istringstream is(next()); std::string g; while (std::getline(is, g, ',')) only.insert(g); }
    else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }
  if (needInstall(root, "probe_verify GAME_DIR [--ref REFDIR] [--only l0,l1,l2,audio,scn,l3,terrain,ordw] [--skip PREFIXES] [--report out.tsv] [--shots OUTDIR]")) return 2;
  auto want = [&](const char* g) { return only.empty() || only.count(g); };
  struct G { const char* name; std::function<void()> run; };
  const G groups[] = {
      {"l0", [&] { groupL0(root, argv[0]); }},
      {"l1", [&] { groupL1(root); }},
      {"l2", [&] { groupL2(root); }},
      {"audio", [&] { groupAudio(root); }},
      {"scn", [&] { groupScenarios(root); }},
      {"l3", [&] { groupL3(root, ref); }},
      {"terrain", [&] { groupTerrain(ref); }},
      {"ordw", [&] { groupOrdW(root); }},
  };
  for (const G& g : groups)
    if (want(g.name)) { g_group = g.name; std::printf("== %s\n", g.name); g.run(); }
  if (!shotsDir.empty() && want("shots")) { g_group = "shots"; std::printf("== shots\n"); groupShots(root, shotsDir); }
  int pass = 0, fail = 0;
  for (const Row& r : g_rows) { pass += r.status == "PASS"; fail += r.status == "FAIL"; }
  std::printf("\n%d PASS, %d FAIL\n", pass, fail);
  if (!report.empty()) {
    std::string t = "group\tstatus\tcheck\tfound\texpected\n";
    for (const Row& r : g_rows) t += r.group + "\t" + r.status + "\t" + r.name + "\t" + r.found + "\t" + r.expected + "\n";
    if (std::FILE* f = std::fopen(report.string().c_str(), "wb")) { std::fwrite(t.data(), 1, t.size(), f); std::fclose(f); }
  }
  return fail ? 1 : 0;
}
