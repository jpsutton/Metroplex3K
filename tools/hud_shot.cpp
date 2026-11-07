// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless UI screenshot. Optionally loads a city file, then runs scripted steps in order:
//   hud_shot --game DIR [--city FILE] --size WxH [--do STEP]... out.png
// STEP: move:X,Y | click:X,Y | hold:X,Y (press, 90 frames, release) | frames:N | shot:FILE | ctrl:0|1 | cmd:HEXID[,HEXARG] | win:NAME | dirty | fps
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/game.h"
#include "png.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string game, city, out = "hud.png"; int W = 1024, H = 768; std::vector<std::string> steps;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--do" && i + 1 < argc) steps.push_back(argv[++i]);
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
    else out = a;
  }
  Game g; g.setPersistence(false); std::string err;
  if (!g.init(game, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
  g.setIntroEnabled(false);
  Input in; in.mx = W / 2; in.my = H / 3;
  auto frame = [&](int n) { for (int i = 0; i < n; i++) { g.frame(in, W, H, 16); in.pressed = in.released = false; in.text.clear(); for (auto& k : in.key) k = false; } };
  frame(3);
  if (!city.empty()) { g.requestFile(city); frame(8); }
  for (const auto& st : steps) {
    size_t c = st.find(':'); std::string op = st.substr(0, c), arg = c == std::string::npos ? "" : st.substr(c + 1);
    int x = 0, y = 0; std::sscanf(arg.c_str(), "%d,%d", &x, &y);
    if (op == "move") { in.mx = x; in.my = y; frame(2); }
    else if (op == "click") { in.mx = x; in.my = y; frame(2); in.pressed = in.down = true; frame(1); in.released = true; in.down = false; frame(3); }
    else if (op == "hold") { in.mx = x; in.my = y; frame(2); in.pressed = in.down = true; frame(90); in.released = true; in.down = false; frame(3); }
    else if (op == "frames") frame(std::atoi(arg.c_str()));
    else if (op == "cmd") {   // cmd:TARGET[,ARG] (hex)
      size_t k = arg.find(',');
      uint32_t a1 = k == std::string::npos ? 0 : uint32_t(std::strtoul(arg.c_str() + k + 1, nullptr, 16));
      g.runCommand(uint32_t(std::strtoul(arg.c_str(), nullptr, 16)), a1); frame(3);
    }
    else if (op == "win") { g.openWindow(arg); frame(4); }
    else if (op == "ctrl") in.modCtrl = arg == "1";   // ctrl:1 / ctrl:0 holds the Ctrl key (query tool)
    else if (op == "zones") { CityLayers l = g.layers(); l.zones = true; l.zonedBuildings = l.otherBuildings = arg != "only"; g.setLayers(l); frame(3); }   // zones:only = Zones layer with the buildings hidden
    else if (op == "queryagent") {   // queryagent:TYPE = Ctrl-click the first building of that agent type (decimal) or a type range TYPE-TYPE
      unsigned lo = 0, hi = 0; std::sscanf(arg.c_str(), "%u-%u", &lo, &hi); if (!hi) hi = lo;
      const World& w = g.world(); bool found = false;
      for (const auto& o : w.occupants) {
        if (o.agentType < lo || o.agentType > hi) continue;
        g.setView(o.x, o.z, 2); frame(3);
        int sx, sy;
        if (!g.tileScreenPos(o.x, o.z, sx, sy)) continue;
        in.modCtrl = true; in.mx = sx; in.my = sy; frame(2);
        in.pressed = in.down = true; frame(1); in.released = true; in.down = false; frame(3);
        in.modCtrl = false; found = true;
        std::printf("queried %u at (%d,%d)\n", o.agentType, o.x, o.z);
        break;
      }
      if (!found) std::printf("no building of type %s\n", arg.c_str());
    }
    else if (op == "parade") { std::printf("parade %s\n", g.startParadeNow() ? "started" : "not started"); if (g.paradeRunning()) g.setView(g.paradeX(), g.paradeZ(), 3); frame(std::atoi(arg.c_str()) > 0 ? std::atoi(arg.c_str()) : 60); }
    else if (op == "look") { int zm = 3; std::sscanf(arg.c_str(), "%d,%d,%d", &x, &y, &zm); g.setView(x, y, zm); frame(3); }   // look:X,Z[,ZOOM] centres the view on a tile
    else if (op == "dirty") g.setDirty(true);
    else if (op == "fps") g.mutableEngineOptions().showFps = true;   // engine option: frame counter overlay
    else if (op == "shot") writePng(arg, W, H, g.screen());
  }
  writePng(out, W, H, g.screen());
  std::printf("screen %d\n", int(g.currentScreen()));
  return 0;
}
