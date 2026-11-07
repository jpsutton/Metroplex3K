// Starts a disaster in a loaded city, follows its effect sprites with the camera and writes PNG frames, to check the disaster visuals
// (src/core/disaster_fx.*) against the shipped art.
//   probe_disaster_fx [GAME_DIR] CITY TYPE OUTPREFIX [frames-between-shots=40] [shots=6] [zoom=3]
// TYPE: fire tornado earthquake riot ufo toxic locusts junk whirlpool
#include <cstring>
#include "probe_common.h"
#include "../png.h"
using namespace probe;

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_disaster_fx GAME_DIR CITY TYPE OUTPREFIX [gap] [shots] [zoom]")) return 1;
  if (argc < 4 + used) { std::fprintf(stderr, "usage: probe_disaster_fx [GAME_DIR] CITY TYPE OUTPREFIX [gap] [shots] [zoom]\n"); return 1; }
  const std::string city = argv[1 + used], type = argv[2 + used], prefix = argv[3 + used];
  const int gap = argc > 4 + used ? std::atoi(argv[4 + used]) : 40, shots = argc > 5 + used ? std::atoi(argv[5 + used]) : 6;
  const int zoom = argc > 6 + used ? std::atoi(argv[6 + used]) : 3;
  struct K { const char* n; DisasterType t; };
  const K kinds[] = {{"fire", DisasterType::Fire}, {"tornado", DisasterType::Tornado}, {"earthquake", DisasterType::Earthquake}, {"riot", DisasterType::Riot},
                     {"ufo", DisasterType::UFO}, {"toxic", DisasterType::ToxicCloud}, {"locusts", DisasterType::Locusts}, {"junk", DisasterType::SpaceJunk},
                     {"whirlpool", DisasterType::Whirlpool}};
  const K* kind = nullptr;
  for (const K& k : kinds) if (type == k.n) kind = &k;
  if (!kind) { std::fprintf(stderr, "unknown type %s\n", type.c_str()); return 1; }
  auto cities = findCities(root, {city});
  if (cities.empty()) { std::fprintf(stderr, "no city matches '%s'\n", city.c_str()); return 1; }
  Session s;
  if (!s.open(root) || !s.load(cities[0])) return 2;
  s.game.setDisastersOn(true);
  s.game.setPaused(false);
  s.game.startDisaster(kind->t);
  if (!s.game.lastMessage().empty()) std::printf("start: %s\n", s.game.lastMessage().c_str());
  s.frames(2, 100);
  for (int i = 0; i < shots; i++) {
    for (int f = 0; f < gap; f++) {
      s.frames(1, 100);
      double x, z;
      if (s.game.disasterFx().focus(x, z)) s.game.setView(int(x), int(z), zoom);
    }
    double x = -1, z = -1; s.game.disasterFx().focus(x, z);
    char name[512]; std::snprintf(name, sizeof name, "%s_%02d.png", prefix.c_str(), i);
    writePng(name, 1280, 800, s.game.screen());
    std::printf("shot %d: fx=%zu focus=(%.1f,%.1f) active=%d\n", i, s.game.disasterFx().count(), x, z, int(s.game.disasterActive()));
  }
  return 0;
}
