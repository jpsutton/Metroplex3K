// L0 conformance tool (spec/28-conformance.md section 2): parse every shipped file with the
// engine's parsers and print coverage statistics.
//   conformance [--strict] [--verbose] <game install path>
// With no path (or no usable install) it prints "no game data" and exits 0.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "core/bat.h"
#include "core/font.h"
#include "core/gamedata.h"
#include "core/image.h"
#include "core/ini.h"
#include "core/ixf.h"
#include "core/scenario.h"
#include "core/world.h"

using namespace m3k;

namespace {

struct Counter {
  long ok = 0, fail = 0;
  void add(bool good) { (good ? ok : fail)++; }
  long total() const { return ok + fail; }
};

struct Report {
  struct Row { std::string name, found, expected, status; };
  std::vector<Row> rows;
  std::vector<std::string> failures;
  bool verbose = false;
  void row(const std::string& name, const std::string& found, const std::string& expected, bool match, bool hasExpected = true) {
    rows.push_back({name, found, expected, !hasExpected ? "info" : (match ? "ok" : "DIFFERS")});
  }
  void fail(const std::string& what) {
    if (failures.size() < 200) failures.push_back(what);
  }
};

std::string num(long v) {
  std::string s = std::to_string(v), o;
  int c = 0;
  for (size_t i = s.size(); i-- > 0;) { o.insert(o.begin(), s[i]); if (++c % 3 == 0 && i > 0 && s[i - 1] != '-') o.insert(o.begin(), ','); }
  return o;
}

bool hasMagic(const Bytes& b) { return b.size() >= 4 && rd32(b.data()) == kIxfMagic; }

std::string relPath(const fs::path& root, const fs::path& p) {
  std::error_code ec;
  fs::path r = fs::relative(p, root, ec);
  return ec ? p.string() : r.generic_string();
}

// SII grammar (spec 02 section 4.1). Returns true if the file read to completion.
bool parseSii(const std::string& text, long& records) {
  std::vector<std::string> lines;
  std::string cur;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\r') { lines.push_back(cur); cur.clear(); if (i + 1 < text.size() && text[i + 1] == '\n') i++; }
    else cur.push_back(text[i]);
  }
  if (!cur.empty()) lines.push_back(cur);
  size_t i = 0;
  while (i < lines.size() && !lines[i].empty() && lines[i][0] == ';') i++;
  if (i >= lines.size()) return false;
  int version = 0;
  if (lines[i].find("Version:") != std::string::npos) {
    if (lines[i].compare(0, 8, "Version:") != 0) return false;
    version = std::atoi(lines[i].c_str() + 8);
    i++;
  }
  if (version != 0 && version != 1) return false;
  if (i >= lines.size() || lines[i].compare(0, 13, "Record Count:") != 0) return false;
  long n = std::atol(lines[i].c_str() + 13);
  i++;
  for (long k = 0; k < n; k++, i++) {
    if (i >= lines.size()) return false;
    const char* p = lines[i].c_str();
    unsigned g = 0, id = 0;
    int l, t, r, b, used = 0;
    int got = version == 1 ? std::sscanf(p, " %x,%x,%d,%d,%d,%d%n", &g, &id, &l, &t, &r, &b, &used)
                           : std::sscanf(p, " %x,%d,%d,%d,%d%n", &id, &l, &t, &r, &b, &used);
    (void)used;
    if (got != (version == 1 ? 6 : 5)) return false;
    records++;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool strict = false, verbose = false;
  std::string path;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--strict") strict = true;
    else if (a == "--verbose") verbose = true;
    else if (a == "-h" || a == "--help") {
      std::puts("usage: conformance [--strict] [--verbose] <game install path>\n"
                "Parses every shipped file with the engine's parsers and prints L0 coverage (spec/28 section 2).");
      return 0;
    } else path = a;
  }
  if (path.empty()) {
    std::puts("no game data: pass the game install path (the directory that contains Apps/).");
    return 0;
  }
  std::error_code ec;
  fs::path root = path;
  if (!fs::is_directory(root, ec)) {
    std::printf("no game data: '%s' is not a directory\n", path.c_str());
    return 0;
  }
  if (resolveCI(root, "Apps").empty() && !resolveCI(root, "Sys").empty() && !resolveCI(root, "Res").empty()) root = root.parent_path();
  fs::path apps = resolveCI(root, "Apps");
  if (apps.empty()) {
    std::printf("no game data: no 'Apps' directory under '%s'\n", path.c_str());
    return 0;
  }

  Report rep;
  rep.verbose = verbose;
  std::map<std::string, long> magicByExt;
  long magicTotal = 0, ixfOpened = 0, ixfFailed = 0;
  long strFiles = 0, strRecords = 0, strShort = 0, strBadFiles = 0;
  Counter sii, siiPartial, sprites, uiImages, bmps, fonts, saves, snrFiles, snrScripts, ssc, bld, batDat, occ, cs, pak, pakEntries;
  std::map<std::string, long> spriteKinds;
  Counter scriptsParsed;
  long scenarioStateFiles = 0;
  std::map<std::string, long> spritesPerFile;

  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    std::error_code e2;
    if (it->is_regular_file(e2)) files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());

  for (const auto& p : files) {
    std::string ext = toLower(p.extension().string());
    std::string rel = relPath(root, p);
    std::string lrel = toLower(rel);
    std::error_code e3;
    uintmax_t sz = fs::file_size(p, e3);
    if (e3) continue;

    // Loose files that are not IXF.
    if (ext == ".bmp") {
      Bytes b;
      bool good = false;
      if (readFile(p, b)) { try { Image im = decodeBmp(b.data(), b.size()); good = !im.empty(); } catch (const FormatError&) {} }
      bmps.add(good);
      if (!good) rep.fail("BMP: " + rel);
      continue;
    }
    if (ext == ".fbf") {
      Bytes b;
      bool good = readFile(p, b);
      if (good) { Font f; good = f.load(b.data(), b.size()); }
      fonts.add(good);
      if (!good) rep.fail("FBF: " + rel);
      continue;
    }
    if (ext == ".sii") {
      Bytes b;
      if (readFile(p, b)) {
        long recs = 0;
        bool full = parseSii(std::string(b.begin(), b.end()), recs);
        sii.add(recs > 0 || full);
        if (!full) { siiPartial.add(false); if (verbose) rep.fail("SII stops early (by design in one shipped file): " + rel); }
      }
      continue;
    }
    if (ext == ".ssc") {
      Bytes b;
      bool good = false;
      if (readFile(p, b)) { SscProject pr; good = SscProject::parse(b, pr) && pr.sectionLengthOk; }
      ssc.add(good);
      if (!good) rep.fail("SSC: " + rel);
      continue;
    }
    if (lrel.size() >= 11 && lrel.compare(lrel.size() - 11, 11, "sys/sys.pak") == 0) {
      PakFile pk;
      bool good = pk.open(p);
      if (good) {
        for (const auto& n : pk.names()) {
          IniFile ini;
          bool g2 = pk.load(n, ini);
          pakEntries.add(g2);
          if (!g2) rep.fail("PAK entry: " + n);
        }
      }
      pak.add(good);
      if (!good) rep.fail("PAK: " + rel);
      continue;
    }
    if (sz < 4) continue;
    Bytes data;
    if (!readFile(p, data) || !hasMagic(data)) continue;

    magicTotal++;
    magicByExt[ext.empty() ? "(none)" : ext]++;
    IxfFile f;
    bool opened = f.openMemory(data);
    if (opened) ixfOpened++; else { ixfFailed++; rep.fail("IXF will not open (expected for 5 holiday placeholders): " + rel); continue; }

    bool isSave = ext == ".sc3" || ext == ".st3" || ext == ".sct" || ext == ".snr";
    if (isSave) {
      bool good = true;
      try { std::vector<std::string> w; loadCity(data, &w); } catch (const std::exception& e) { good = false; rep.fail("save " + rel + ": " + e.what()); }
      saves.add(good);
      if (ext == ".snr") {
        ScenarioFile sf;
        std::string err;
        bool g2 = ScenarioFile::load(data, sf, &err);
        snrFiles.add(g2);
        if (!g2) rep.fail("scenario " + rel + ": " + err);
        else {
          if (sf.hasState) scenarioStateFiles++;
          // every script record must parse
          for (const auto& e : f.entries())
            if (e.key.type == kSnrScriptType && e.key.group == kSnrScriptGroup) {
              bool ok = sf.scripts.count(e.key.instance) != 0;
              scriptsParsed.add(ok);
              if (!ok) rep.fail("script " + std::to_string(e.key.instance) + " in " + rel);
            }
          for (const auto& w : sf.warnings) rep.fail("scenario warning " + rel + ": " + w);
        }
      }
    }
    if (ext == ".bld") {
      bool good = true;
      try { BldFile b = BldFile::parse(data); std::string err; good = b.model.validate(&err); if (!good) rep.fail(".bld invalid " + rel + ": " + err); }
      catch (const std::exception& e) { good = false; rep.fail(".bld " + rel + ": " + e.what()); }
      bld.add(good);
    }
    if (lrel.size() >= 7 && lrel.compare(lrel.size() - 7, 7, "bat.dat") == 0) {
      BatDatabase db;
      bool good = db.load(data);
      long listed = good ? long(db.list().size()) : 0;
      batDat.ok += listed;
      if (!good) { batDat.fail++; rep.fail("BAT.DAT: " + rel); }
    }

    bool inSprites = lrel.find("res/sprites/") != std::string::npos || lrel.find("barender/bat.dat") != std::string::npos;
    bool inText = lrel.find("res/text/") != std::string::npos && ext == ".ixf";
    long fileStrings = 0;
    for (const auto& e : f.entries()) {
      const uint8_t* ptr; size_t avail; uint32_t declared = 0;
      if (!f.get(e.key, ptr, avail, &declared)) continue;
      uint32_t t = e.key.type;
      if (t == T_STRING && avail >= 4) {
        uint32_t len = rd32(ptr);
        if (len <= avail - 4) { strRecords++; fileStrings++; if (declared == len) strShort++; }
        else { strBadFiles++; rep.fail("string record overruns: " + rel); }
      }
      if (t == T_SPRITE_IMAGE && inSprites) {
        bool good = true;
        std::string kind = "?";
        if (avail >= 2) { char b[32]; std::snprintf(b, sizeof b, "fmt%u/enc%u", ptr[0], ptr[1]); kind = b; }
        try {
          DecodedSprite d = decodeSprite(ptr, std::min<size_t>(avail, declared ? declared : avail));
          kind += d.isPlane ? " plane" : " rle";
        } catch (const std::exception&) { good = false; }
        sprites.add(good);
        spriteKinds[kind]++;
        if (!good) rep.fail("sprite " + rel + " group " + std::to_string(e.key.group) + " inst " + std::to_string(e.key.instance));
      }
      if (t == T_UI_IMAGE && !inSprites) {
        bool good = true;
        try { decodeUiImage(ptr, std::min<size_t>(avail, declared ? declared : avail)); } catch (const std::exception&) { good = false; }
        uiImages.add(good);
        if (!good) rep.fail("UI image " + rel + " inst " + std::to_string(e.key.instance));
      }
      if ((t == T_OCC_ATTR || t == T_OCC_ATTR_PORT) && !isSave) {
        OccAttr a = parseOccAttr(ptr, std::min<size_t>(avail, declared ? declared : avail));
        occ.add(a.valid);
      }
      if (t == T_CSATTRIB && !isSave) {
        CsAttrib c = parseCsAttrib(ptr, std::min<size_t>(avail, declared ? declared : avail));
        if (c.valid || e.key.group != kBatGroup) cs.add(c.valid); else cs.add(false);
      }
    }
    if (inText && fileStrings > 0) strFiles++;
  }

  std::printf("Metroplex3K conformance (spec/28 section 2) -- %s\n\n", root.string().c_str());
  std::string exts;
  for (const auto& kv : magicByExt) exts += (exts.empty() ? "" : ", ") + num(kv.second) + " " + kv.first;
  rep.row("Files starting with the IXF magic", num(magicTotal), "649", magicTotal == 649);
  rep.row("IXF files that open", num(ixfOpened) + " of " + num(magicTotal), "644 of 649", ixfOpened == 644 && magicTotal == 649);
  rep.row("String tables: files / records", num(strFiles) + " / " + num(strRecords), "477 / 55,952", strFiles == 477 && strRecords == 55952);
  rep.row("  string records with index size = length", num(strShort), "2,863", strShort == 2863);
  rep.row(".SII sidecars", num(sii.ok) + " of " + num(sii.total()), "10 of 10", sii.ok == 10 && sii.fail == 0);
  rep.row("Sprite image payloads", num(sprites.ok) + " of " + num(sprites.total()), "63,931", sprites.ok == 63931 && sprites.fail == 0);
  rep.row("UI images (62B9DA24)", num(uiImages.ok) + " of " + num(uiImages.total()), "729 of 729", uiImages.ok == 729 && uiImages.fail == 0);
  rep.row("Loose BMP files", num(bmps.ok) + " of " + num(bmps.total()), "677 of 677", bmps.ok == 677 && bmps.fail == 0);
  rep.row(".FBF fonts", num(fonts.ok) + " of " + num(fonts.total()), "56 of 56", fonts.ok == 56 && fonts.fail == 0);
  rep.row("Occupant attribute records (TKB1)", num(occ.ok) + " of " + num(occ.total()), "1,530 of 1,530", occ.ok == 1530 && occ.fail == 0);
  rep.row("Composite sprite records (00006300)", num(cs.ok) + " of " + num(cs.total()), "1,425 of 1,425", cs.ok == 1425 && cs.fail == 0);
  rep.row("SYS.PAK: files / entries", num(pak.ok) + " / " + num(pakEntries.ok) + " of " + num(pakEntries.total()), "1 / 51 of 51",
          pak.ok == 1 && pakEntries.ok == 51 && pakEntries.fail == 0);
  rep.row("Saves (.sc3 .st3 .sct .SNR)", num(saves.ok) + " of " + num(saves.total()), "59 of 59", saves.ok == 59 && saves.fail == 0);
  rep.row("Scenario files: records / scripts", num(snrFiles.ok) + " of " + num(snrFiles.total()) + " / " + num(scriptsParsed.ok) + " of " + num(scriptsParsed.total()),
          "13 of 13 / 116", snrFiles.ok == 13 && snrFiles.fail == 0 && scriptsParsed.fail == 0 && scriptsParsed.ok == 116);
  rep.row("Scenario Creator projects (.SSC)", num(ssc.ok) + " of " + num(ssc.total()), "14 of 14", ssc.ok == 14 && ssc.fail == 0);
  rep.row("BAT buildings: .bld files / BAT.DAT entries", num(bld.ok) + " of " + num(bld.total()) + " / " + num(batDat.ok), "17 of 17 / 15",
          bld.ok == 17 && bld.fail == 0 && batDat.ok == 15);
  rep.row("Not covered by engine parsers: .XA, INTRO.TGQ, hotkeys, MenuItem.INI, SSimData, TilingRules", "-", "-", true, false);

  size_t w0 = 0, w1 = 0, w2 = 0;
  for (const auto& r : rep.rows) { w0 = std::max(w0, r.name.size()); w1 = std::max(w1, r.found.size()); w2 = std::max(w2, r.expected.size()); }
  std::printf("%-*s  %-*s  %-*s  %s\n", int(w0), "Check", int(w1), "Found", int(w2), "Expected", "Status");
  for (const auto& r : rep.rows)
    std::printf("%-*s  %-*s  %-*s  %s\n", int(w0), r.name.c_str(), int(w1), r.found.c_str(), int(w2), r.expected.c_str(), r.status.c_str());
  std::printf("\nIXF files by extension: %s\n", exts.empty() ? "none" : exts.c_str());
  if (!spriteKinds.empty()) {
    std::printf("Sprite encodings:");
    for (const auto& kv : spriteKinds) std::printf(" %s=%s", kv.first.c_str(), num(kv.second).c_str());
    std::printf("\n");
  }
  if (scenarioStateFiles) std::printf("Scenario files carrying a run-time state record: %ld\n", scenarioStateFiles);
  long parseFailures = ixfFailed + strBadFiles + sprites.fail + uiImages.fail + bmps.fail + fonts.fail + occ.fail + cs.fail + pak.fail +
                       pakEntries.fail + saves.fail + snrFiles.fail + scriptsParsed.fail + ssc.fail + bld.fail + batDat.fail + sii.fail;
  if (!rep.failures.empty()) {
    std::printf("\nProblems (%zu%s):\n", rep.failures.size(), rep.failures.size() >= 200 ? "+" : "");
    size_t shown = verbose ? rep.failures.size() : std::min<size_t>(rep.failures.size(), 20);
    for (size_t i = 0; i < shown; i++) std::printf("  %s\n", rep.failures[i].c_str());
    if (shown < rep.failures.size()) std::printf("  ... (use --verbose for all)\n");
  }
  std::printf("\n%ld parse failure(s).\n", parseFailures);
  return strict && parseFailures > 0 ? 1 : 0;
}
