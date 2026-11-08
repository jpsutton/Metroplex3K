// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Game glue for the user-made buildings dialogs (Building Replacement Manager, Building List Editor).
#include <algorithm>
#include <fstream>
#include "core/fsutil.h"
#include "core/game.h"

namespace m3k {

void Game::loadBatDb() {
  batDb_ = BatDatabase();
  if (!haveData_) return;
  fs::path p = resolveCI(data_.appsDir(), "BARender/BAT.DAT");
  Bytes b;
  if (!p.empty() && readFile(p, b)) batDb_.load(b);
}

void Game::fillBrmBat() {
  brmState_.bat.clear();
  bleState_.active.clear();
  for (const BatDirEntry& e : batDb_.list()) {
    UbBuilding u;
    u.key = u.appearance = e.appearance; u.name = e.name; u.author = e.author; u.buildingId = e.buildingId; u.lot = e.lotSize;
    brmState_.bat.push_back(u);
  }
  // Listed by name (observed in the Building List Editor's Active List).
  std::sort(brmState_.bat.begin(), brmState_.bat.end(), [](const UbBuilding& a, const UbBuilding& b) { return toLower(a.name) < toLower(b.name); });
  bleState_.active = brmState_.bat;
}

void Game::openUserBuildings() {
  brmState_ = UserBuildingsState();
  loadBatDb();
  fillBrmBat();
  if (haveData_) {
    IniFile ini;
    if (data_.ini().load("SC3CityScheme.ini", ini)) {
      for (const auto& kv : ini.section("ReplaceableBuildings")) {
        unsigned long long a, b, c;
        if (std::sscanf(kv.first.c_str(), "%llu , %llu , %llu", &a, &b, &c) != 3) continue;
        brmState_.totalReplaceable++;
        const OccAttr* o = data_.occupant(uint32_t(c));
        if (!o || !o->valid) continue;
        UbBuilding u;
        u.key = ResKey{uint32_t(a), uint32_t(b), uint32_t(c)};
        u.appearance = o->appearance; u.name = kv.second; u.lot = int(o->sizeX); u.zone = ubZoneClass(o->agentType);
        brmState_.replaceable.push_back(u);
      }
    }
  }
  for (const auto& pr : userSet_.pairs) brmState_.set.set(pr.first, pr.second);
  showBrm_ = true; showBle_ = false;
}

void Game::refreshBleFiles() {
  bleState_.files.clear();
  bleState_.fileSel = -1; bleState_.fileScroll = 0;
  fs::path root = resolveCI(data_.root(), "Buildings");
  if (bleDir_.empty()) bleDir_ = root;
  if (bleDir_.empty()) return;
  bleState_.folder = bleDir_.filename().string();
  if (bleDir_ != root) bleState_.files.push_back({"", bleDir_.parent_path().string(), 0});
  std::vector<BuildingListState::File> dirs, bld;
  std::error_code ec;
  for (const auto& de : fs::directory_iterator(bleDir_, ec)) {
    std::string ext = de.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (de.is_directory(ec)) dirs.push_back({de.path().filename().string(), de.path().string(), 1});
    else if (ext == ".bld") bld.push_back({de.path().stem().string(), de.path().string(), 2});
  }
  auto byLabel = [](const BuildingListState::File& x, const BuildingListState::File& y) { return x.label < y.label; };
  std::sort(dirs.begin(), dirs.end(), byLabel); std::sort(bld.begin(), bld.end(), byLabel);
  for (auto& f : dirs) bleState_.files.push_back(f);
  for (auto& f : bld) bleState_.files.push_back(f);
}

void Game::openBuildingList() {
  ensureFileIcons();
  bleState_ = BuildingListState();
  fillBrmBat();
  bleState_.activeSel = bleState_.active.empty() ? -1 : 0;
  bleState_.icons[0] = &fileIcons_[0]; bleState_.icons[1] = &fileIcons_[1]; bleState_.icons[2] = &fileIcons_[4];
  bleDir_.clear();
  refreshBleFiles();
  showBle_ = true;
}

void Game::handleUserBuildingsResult(const WindowResult& r, bool editor) {
  for (const auto& e : r.events) {
    switch (e.act) {
      case Act::OpenBuildingList: openBuildingList(); break;
      case Act::BatOpenFolder: bleDir_ = e.text; refreshBleFiles(); break;
      case Act::BatSelectFile: {
        Bytes b; bleState_.fileThumb = Image(); bleState_.fileLot = 0;
        if (!readFile(e.text, b)) break;
        try {
          BldFile f = BldFile::parse(b);
          bleState_.fileLot = f.lotSize;
          if (f.hasThumb && f.thumb.size() >= size_t(f.thumbW) * f.thumbH) {
            Image im; im.w = f.thumbW; im.h = f.thumbH; im.px.resize(size_t(im.w) * im.h);
            for (size_t i = 0; i < im.px.size(); i++) {
              uint16_t v = f.thumb[i];
              im.px[i] = rgb(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
            }
            bleState_.fileThumb = im;
          }
        } catch (const std::exception&) {}
        break;
      }
      case Act::BatRemove: {
        fs::path p = resolveCI(data_.appsDir(), "BARender/BAT.DAT");
        if (p.empty() || !batDb_.remove(e.id)) break;
        Bytes out = batDb_.build();
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        f.close();
        data_.remountBat();
        int keep = bleState_.activeSel;
        fillBrmBat();
        bleState_.activeSel = std::min(keep, int(bleState_.active.size()) - 1);
        break;
      }
      case Act::BatAdd: {
        BatImportResult res; std::string err;
        if (!batImportInstall(data_.root(), {fs::path(e.text).filename().string()}, res, err)) { message("Import failed: " + err); break; }
        data_.remountBat();
        loadBatDb(); fillBrmBat();
        break;
      }
      case Act::Done:
        if (!editor) { userSet_.clear(); for (const auto& pr : brmState_.set.pairs) userSet_.set(pr.first, pr.second); }
        break;
      default: break;
    }
  }
  if (r.close || r.has(Act::Close) || r.has(Act::Done)) {
    if (editor) { showBle_ = false; fillBrmBat(); }
    else showBrm_ = false;
  }
}

void Game::drawUserBuildings() {
  WinEnv E = env();
  uiHit_ = true;
  if (showBrm_) {
    Input live = ui_.in;
    if (showBle_) { ui_.in = Input(); ui_.in.mx = ui_.in.my = -1; }
    WindowResult r = brmDlg_.draw(ui_, E, brmState_);
    ui_.in = live;
    handleUserBuildingsResult(r, false);
  }
  if (showBle_) handleUserBuildingsResult(bleDlg_.draw(ui_, E, bleState_), true);
  ui_.in.pressed = ui_.in.released = false;
}

}  // namespace m3k
