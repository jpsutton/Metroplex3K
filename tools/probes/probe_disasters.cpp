// Starts each disaster type in a loaded city and reports how it ends and what it damages (issues #35, #37).
// Disasters run at minute resolution while active, so each type can take up to 750 s of play (1500 quarter-second
// frames); "STILL ACTIVE" at the limit usually means fires are still burning.
//   probe_disasters [GAME_DIR] [city-name-substring]      (default city: Berlin)
#include "probe_common.h"
using namespace probe;

struct Kind { const char* name; DisasterType type; };

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_disasters GAME_DIR [city]")) return 1;
  std::string want = argc > 1 + used ? argv[1 + used] : "Berlin";
  auto cities = findCities(root, {want});
  if (cities.empty()) { std::fprintf(stderr, "no city matches '%s'\n", want.c_str()); return 1; }

  const Kind kinds[] = {{"fire", DisasterType::Fire},         {"tornado", DisasterType::Tornado}, {"earthquake", DisasterType::Earthquake},
                        {"riot", DisasterType::Riot},          {"ufo", DisasterType::UFO},         {"toxic cloud", DisasterType::ToxicCloud},
                        {"locusts", DisasterType::Locusts},    {"space junk", DisasterType::SpaceJunk}};
  std::printf("== %s\n", cities[0].filename().string().c_str());
  for (const Kind& k : kinds) {
    Session s;
    if (!s.open(root) || !s.load(cities[0])) return 2;
    s.game.setDisastersOn(true);   // many saves have the disaster flag off
    s.game.setPaused(false);
    auto count = [&](uint32_t state) { int n = 0; for (const auto& o : s.game.world().occupants) n += o.state == state; return n; };
    const int rubble0 = count(8);
    s.game.startDisaster(k.type);
    if (!s.game.lastMessage().empty()) { std::printf("   %-12s could not start: %s\n", k.name, s.game.lastMessage().c_str()); continue; }
    int frames = 0, peakFire = 0; size_t peakRec = 0;
    bool ended = false;
    while (frames < 1500) {
      s.frames(1, 250);
      frames++;
      peakFire = std::max(peakFire, count(16)); peakRec = std::max(peakRec, s.game.disasters().burning().size());
      if (!s.game.disasterActive()) { ended = true; break; }
    }
    std::printf("   %-12s %s after %4d frames (%.0f s of play); rubble lots +%d, peak burning %d (records %zu)\n", k.name, ended ? "ended" : "STILL ACTIVE", frames,
                frames * 0.25, count(8) - rubble0, peakFire, peakRec);
  }
  return 0;
}
