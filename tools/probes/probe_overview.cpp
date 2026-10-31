// Compare the engine's overview colours (core/overview.h) with the preview thumbnail stored in each shipped
// city and terrain file. Usage: probe_overview GAME [name-filter...]
// Prints, per file, how many tiles differ by more than the 16-bit display rounding (8 per channel).
#include "probe_common.h"
#include "core/overview.h"

using namespace m3k;

int main(int argc, char** argv) {
  fs::path root;
  int used = probe::gameDirFromArgs(argc, argv, root);
  if (probe::needInstall(root, "probe_overview GAME [name-filter...]")) return 2;
  std::vector<std::string> filters(argv + 1 + used, argv + argc);
  std::vector<fs::path> files;
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    std::string ext = probe::lower(e.path().extension().string());
    if (ext != ".sc3" && ext != ".sct") continue;
    std::string n = probe::lower(e.path().filename().string());
    bool ok = filters.empty();
    for (const auto& f : filters) if (n.find(probe::lower(f)) != std::string::npos) ok = true;
    if (ok) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  long allBad = 0, allTiles = 0;
  for (const auto& f : files) {
    Bytes b;
    if (!readFile(f, b)) continue;
    CityPreview cp;
    World w;
    try { w = loadCity(b); } catch (const FormatError& e) { std::printf("%-40s load failed: %s\n", f.filename().string().c_str(), e.what()); continue; }
    if (!readCityPreview(b, cp) || cp.image.w != w.W || cp.image.h != w.H) { std::printf("%-40s no thumbnail\n", f.filename().string().c_str()); continue; }
    long bad = 0;
    for (int z = 0; z < w.H; z++)
      for (int x = 0; x < w.W; x++) {
        uint32_t a = overviewTileColour(w, x, z), t = cp.image.px[size_t(w.H - 1 - z) * w.W + x];
        for (int s = 0; s < 24; s += 8)
          if (std::abs(int((a >> s) & 255) - int((t >> s) & 255)) > 8) { bad++; break; }
      }
    allBad += bad; allTiles += long(w.W) * w.H;
    std::printf("%-40s %3dx%-3d differing tiles %ld\n", f.filename().string().c_str(), w.W, w.H, bad);
  }
  std::printf("total: %ld of %ld tiles differ\n", allBad, allTiles);
  return 0;
}
