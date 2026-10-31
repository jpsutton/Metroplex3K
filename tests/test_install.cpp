// Data-driven path test: a synthetic install on disk, Game::init, growth with real-data configuration,
// sprites drawn via composite-sprite records, save to the install and reload.
#include <cmath>
#include "core/game.h"
#include "core/gamedir.h"
#include "fake_install.h"
#include "testutil.h"
using namespace m3k;

int main() {
  fs::path root = fs::temp_directory_path() / "m3k_fake_install";
  fake::makeInstall(root);
  CHECK(!findGameRoot(root).empty());

  GameData gd; std::string err;
  CHECK(gd.open(root, err));
  CHECK(gd.string(0x03C09AFF, 1) == "Start new city");
  const OccAttr* oa = gd.occupant(100);
  CHECK(oa && oa->agentType == 3100 && oa->sizeX == 1);
  const CsAttrib* cs = gd.csattrib(oa->appearance);
  CHECK(cs && cs->frames.size() == 20);
  const SpriteImage* sp = gd.sprite(2, (100u << 16) | 15);
  CHECK(sp && sp->img.w == 64 && sp->T == 40);

  Game game; game.setPersistence(false); game.setIntroEnabled(false);
  CHECK(game.init(root, err));
  CHECK(game.currentScreen() == Screen::MainMenu);
  Input in;
  auto frame = [&](int n = 1, uint32_t dt = 16) { for (int i = 0; i < n; i++) { game.frame(in, 1280, 800, dt); in.keyDowns.clear(); in.pressed = in.released = false; } };
  frame(2);
  game.runMenuItem(0);   // no-op for unknown ids
  game.gotoScreen(Screen::InCity);
  frame(1);
  // Start a city programmatically via the New City flow.
  game.runCommand(0xE3270FE9);
  frame(2);
  CHECK(game.currentScreen() == Screen::NewCity);

  // Build a flat 64x64 city file, write it into the install and open it.
  World w; w.init(64, 64);
  for (auto& a : w.alt) a = 100;
  for (auto& a : w.water) a = 40;
  w.seaLevel = 40; w.info.flagA = 1; w.info.cityName = "Fakeville"; w.info.mayorName = "M"; w.info.year = 1950; w.info.foundingYear = 1950; w.info.fileKind = 3;
  w.view = {32, 32, 2, 0, true};
  fake::writeFile(root / "Cities" / "fake.sc3", saveCity(w));
  game.requestFile((root / "Cities" / "fake.sc3").string());
  frame(3);
  CHECK(game.currentScreen() == Screen::InCity);
  City& c = game.city();
  std::vector<std::pair<int, int>> road;
  for (int x = 10; x <= 50; x++) road.push_back({x, 30});
  CHECK(game.sim().buildRoad(road));
  CHECK(game.sim().paintZoneType(12, 31, 40, 34, 1));
  CHECK(c.plopAgent(900, 12111, 4, 4, 52, 28, 100));
  std::vector<std::pair<int, int>> line = road; line.push_back({51, 30});
  CHECK(game.sim().buildNetwork(NetworkKind::Power, line));
  game.setPaused(false);
  for (int i = 0; i < 300; i++) frame(1, 2000);
  int built = 0;
  for (const auto& o : game.world().occupants) if (o.occId == 100) built++;
  std::printf("buildings=%d pop=%d\n%s\n", built, game.sim().population(), c.debugStats().c_str());
  CHECK(built > 0);
  CHECK(game.sim().population() > 0);
  // Sprites resolved through data: the green sprite colour must appear on screen.
  game.setView(26, 32, 2);
  frame(2);
  int green = 0;
  for (uint32_t p : game.screen()) if ((p & 0xFFFFFF) == 0x00FF00 || (p & 0xFFFFFF) == 0x00FB00 || (p & 0xFFFFFF) == 0x00FC00) green++;
  std::printf("greenpx=%d\n", green);
  CHECK(green > 0);
  // A started disaster runs at minute resolution: an earthquake must start, play out and end (it used to stay
  // frozen because nothing drove the per-minute instance update).
  game.startDisaster(DisasterType::Earthquake);
  bool sawQuake = false;
  for (int i = 0; i < 1200; i++) { frame(1, 500); sawQuake = sawQuake || game.disasterActive(); if (sawQuake && !game.disasterActive()) break; }
  CHECK(sawQuake);
  CHECK(!game.disasterActive());
  TEST_MAIN_END();
}
