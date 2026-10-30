// Metroplex3K desktop application (SDL2). Everything platform independent lives in src/core.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/gamedir.h"
#include "core/render.h"
#include "core/sim.h"
#include "core/terrain_gen.h"
#include "core/world.h"

using namespace m3k;

namespace {

struct CityEntry { fs::path path; std::string label; };

std::vector<CityEntry> listCities(const fs::path& root) {
  std::vector<CityEntry> v;
  struct Dir { const char* sub; const char* ext; const char* tag; };
  const Dir dirs[] = {{"Cities", ".sc3", "city"}, {"Cities/StarterTowns", ".st3", "starter"},
                      {"Cities/Terrains", ".sct", "terrain"}, {"Cities/Scenarios", ".snr", "scenario"}};
  for (const auto& d : dirs) {
    fs::path dir = resolveCI(root, d.sub);
    if (dir.empty()) continue;
    for (const auto& f : listFilesCI(dir, d.ext)) v.push_back({f, std::string(d.tag) + ": " + f.stem().string()});
  }
  return v;
}

struct App {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  int texW = 0, texH = 0;
  GameData data;
  bool haveData = false;
  fs::path root;
  std::vector<CityEntry> cities;
  int cityIdx = -1;
  World world;
  Renderer renderer;
  Camera cam;
  RenderOptions opt;
  bool running = true;
  std::string status;
  uint32_t newSeed = 1;
  SimData simData;
  Sim sim;
  bool simReady = false;
  Tool tool = Tool::None;
  bool paused = true;
  int speed = 1;
  double simAccum = 0;
  int dragX0 = -1, dragZ0 = -1;
  bool toolDown = false;

  void setTitle() {
    std::string t = "Metroplex3K";
    if (world.W) {
      t += " - " + (world.info.cityName.empty() ? std::string("(unnamed)") : world.info.cityName);
      t += "  " + std::to_string(world.W) + "x" + std::to_string(world.H);
      t += "  year " + std::to_string(world.info.year);
      t += "  zoom " + std::to_string(cam.zoom) + " rot " + std::to_string(cam.rot);
      if (simReady) {
        t += "  $" + std::to_string(sim.funds) + "  pop " + std::to_string(sim.population());
        t += "  R" + std::to_string(sim.valveR.value) + " C" + std::to_string(sim.valveC.value) + " I" + std::to_string(sim.valveI.value);
        t += paused ? "  PAUSED" : "  speed " + std::to_string(speed);
        t += std::string("  tool: ") + toolName(tool);
      }
    }
    if (!status.empty()) t += "  [" + status + "]";
    SDL_SetWindowTitle(win, t.c_str());
  }

  bool initGame(const fs::path& r) {
    std::string err;
    GameData g;
    if (!g.open(r, err)) { status = err; return false; }
    data = std::move(g);
    haveData = true; root = data.root();
    saveGameRoot(root);
    cities = listCities(root);
    return true;
  }

  void adopt(World&& w) {
    world = std::move(w);
    if (haveData) {
      data.unmountSets();
      if (world.scheme.buildingSet) data.mountBuildingSet(world.scheme.buildingSet);
      if (world.scheme.floraSet) data.mountFloraSet(world.scheme.floraSet);
    }
    renderer.setWorld(&world, haveData ? &data : nullptr);
    if (haveData && simData.zones.empty()) simData.load(data);
    sim.init(&world, haveData ? &data : nullptr, &simData, SDL_GetTicks());
    simReady = true; paused = true;
    cam.zoom = world.view.valid ? std::max(0, std::min(4, world.view.zoom)) : 2;
    cam.rot = world.view.valid ? (world.view.rotation & 3) : 0;
    float cx = world.view.valid ? world.view.centreX : world.W / 2.f, cz = world.view.valid ? world.view.centreZ : world.H / 2.f;
    renderer.centreOnWorld(cam, cx, cz);
  }

  void loadCity(int idx) {
    if (cities.empty()) return;
    idx = ((idx % int(cities.size())) + int(cities.size())) % int(cities.size());
    Bytes b;
    if (!readFile(cities[idx].path, b)) { status = "cannot read file"; return; }
    try {
      std::vector<std::string> warn;
      World w = m3k::loadCity(b, &warn);
      adopt(std::move(w));
      cityIdx = idx;
      status = cities[idx].label;
      for (auto& s : warn) std::cerr << "warning: " << s << "\n";
    } catch (const std::exception& e) {
      status = std::string("load failed: ") + e.what();
      std::cerr << cities[idx].path << ": " << e.what() << "\n";
    }
  }

  void newCity(int size) {
    TerrainGenOptions o; o.seed = newSeed++ * 2654435761u + uint32_t(SDL_GetTicks());
    World w; generateTerrain(w, size, size, o);
    w.info.cityName = "New City"; w.info.year = 2000;
    w.view = {size / 2.f, size / 2.f, 2, 0, true};
    adopt(std::move(w));
    cityIdx = -1; status = "new terrain";
  }

  void handleDrop(const std::string& path) {
    fs::path p = path;
    std::error_code ec;
    if (fs::is_regular_file(p, ec) && looksLikeGogInstaller(p)) {
      std::string msg;
      fs::path r = extractGogInstaller(p, configDir() / "game", msg);
      status = msg;
      if (!r.empty() && initGame(r)) loadCity(0);
      else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Metroplex3K", msg.c_str(), win);
      return;
    }
    if (fs::is_regular_file(p, ec) && !cities.empty()) {
      Bytes b;
      if (readFile(p, b)) { try { adopt(m3k::loadCity(b)); status = p.filename().string(); } catch (const std::exception& e) { status = e.what(); } }
      return;
    }
    fs::path r = findGameRoot(p, 3);
    if (r.empty()) { status = "no game data found in dropped item"; return; }
    if (initGame(r)) { status = "game data found"; loadCity(0); }
  }

  void applyTool(int x0, int z0, int x1, int z1) {
    if (!simReady) return;
    switch (tool) {
      case Tool::Road: {
        std::vector<std::pair<int, int>> path;
        int x = x0, z = z0;
        while (x != x1) { path.push_back({x, z}); x += x1 > x ? 1 : -1; }
        while (z != z1) { path.push_back({x, z}); z += z1 > z ? 1 : -1; }
        path.push_back({x1, z1});
        sim.buildRoad(path); break;
      }
      case Tool::Bulldoze: sim.bulldoze(x0, z0, x1, z1); break;
      case Tool::None: break;
      default: sim.paintZone(x0, z0, x1, z1, tool); break;
    }
  }

  void ensureTexture(int w, int h) {
    if (tex && texW == w && texH == h) return;
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    texW = w; texH = h;
    renderer.resize(w, h);
  }

  void scrollPixels(float dx, float dy) {
    float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
    float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
    cam.u = std::max(0.f, std::min(float(world.W), cam.u + du));
    cam.v = std::max(0.f, std::min(float(world.H), cam.v + dv));
  }

  void rotate(int dir) {
    // Keep the same world position at the centre.
    float W = float(world.W), H = float(world.H);
    float x, z;
    switch (cam.rot & 3) {
      case 0: x = cam.u; z = cam.v; break;
      case 1: x = cam.v; z = W - cam.u; break;
      case 2: x = W - cam.u; z = H - cam.v; break;
      default: x = H - cam.v; z = cam.u; break;
    }
    cam.rot = (cam.rot + dir + 4) & 3;
    renderer.centreOnWorld(cam, x, z);
  }

  void run() {
    const char* help =
        "Arrows/WASD: scroll   Mouse wheel / +,-: zoom   Q/E: rotate   G: grid\n"
        "L / K: next / previous city   N: new random city (Shift: larger map)\n"
        "1-9: zone R/C/I low/med/high   R: road   B: bulldoze   X: dezone   0: no tool (drag with left mouse)\n"
        "Space: pause   F1/F2/F3: speed   F11: fullscreen   Esc: quit\n"
        "Drop a game folder or the GOG installer .exe onto the window to set the game data.";
    std::cout << help << std::endl;
    bool dragging = false;
    uint32_t last = SDL_GetTicks(), lastSim = last;
    while (running) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        switch (e.type) {
          case SDL_QUIT: running = false; break;
          case SDL_DROPFILE: handleDrop(e.drop.file); SDL_free(e.drop.file); break;
          case SDL_MOUSEWHEEL:
            if (world.W) { cam.zoom = std::max(0, std::min(4, cam.zoom + (e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0))); }
            break;
          case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_MIDDLE || e.button.button == SDL_BUTTON_RIGHT) dragging = true;
            else if (e.button.button == SDL_BUTTON_LEFT && tool != Tool::None && world.W) {
              int tx, tz;
              if (renderer.pickTile(cam, e.button.x, e.button.y, tx, tz)) { dragX0 = tx; dragZ0 = tz; toolDown = true; }
            }
            break;
          case SDL_MOUSEBUTTONUP:
            dragging = false;
            if (e.button.button == SDL_BUTTON_LEFT && toolDown) {
              toolDown = false;
              int tx, tz;
              if (renderer.pickTile(cam, e.button.x, e.button.y, tx, tz)) applyTool(dragX0, dragZ0, tx, tz);
            }
            break;
          case SDL_MOUSEMOTION: if (dragging && world.W) scrollPixels(-float(e.motion.xrel), -float(e.motion.yrel)); break;
          case SDL_KEYDOWN: {
            bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            switch (e.key.keysym.sym) {
              case SDLK_ESCAPE: running = false; break;
              case SDLK_SPACE: paused = !paused; break;
              case SDLK_F1: speed = 1; paused = false; break;
              case SDLK_F2: speed = 2; paused = false; break;
              case SDLK_F3: speed = 3; paused = false; break;
              case SDLK_1: tool = Tool::ZoneRL; break; case SDLK_2: tool = Tool::ZoneRM; break; case SDLK_3: tool = Tool::ZoneRH; break;
              case SDLK_4: tool = Tool::ZoneCL; break; case SDLK_5: tool = Tool::ZoneCM; break; case SDLK_6: tool = Tool::ZoneCH; break;
              case SDLK_7: tool = Tool::ZoneIL; break; case SDLK_8: tool = Tool::ZoneIM; break; case SDLK_9: tool = Tool::ZoneIH; break;
              case SDLK_r: tool = Tool::Road; break;
              case SDLK_b: tool = Tool::Bulldoze; break;
              case SDLK_x: tool = Tool::Dezone; break;
              case SDLK_0: tool = Tool::None; break;
              case SDLK_g: opt.grid = !opt.grid; break;
              case SDLK_q: if (world.W) rotate(-1); break;
              case SDLK_e: if (world.W) rotate(1); break;
              case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: cam.zoom = std::min(4, cam.zoom + 1); break;
              case SDLK_MINUS: case SDLK_KP_MINUS: cam.zoom = std::max(0, cam.zoom - 1); break;
              case SDLK_l: loadCity(cityIdx + 1); break;
              case SDLK_k: loadCity(cityIdx - 1); break;
              case SDLK_n: newCity(shift ? 256 : 128); break;
              case SDLK_F11: {
                bool fs_ = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                SDL_SetWindowFullscreen(win, fs_ ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
              }
              default: break;
            }
            break;
          }
          default: break;
        }
      }
      if (simReady && !paused && world.W) {
        uint32_t nowT = SDL_GetTicks();
        simAccum += double(nowT - lastSim) / 1000.0 * (speed == 1 ? 1 : speed == 2 ? 3 : 10);
        while (simAccum >= 0.25) {
          simAccum -= 0.25;
          sim.tickDay();
          if (sim.day % 30 == 0) sim.tickMonth();
          world.info.dayOfYear++;
          if (world.info.dayOfYear > 365) { world.info.dayOfYear = 1; world.info.year++; }
        }
      }
      lastSim = SDL_GetTicks();
      const Uint8* ks = SDL_GetKeyboardState(nullptr);
      if (world.W) {
        float s = 32.f;
        float dx = 0, dy = 0;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) dx -= s;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) dx += s;
        if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]) dy -= s;
        if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]) dy += s;
        if (dx || dy) scrollPixels(dx, dy);
      }
      int w, h;
      SDL_GetWindowSize(win, &w, &h);
      ensureTexture(w, h);
      if (world.W) {
        renderer.render(cam, opt);
        SDL_UpdateTexture(tex, nullptr, renderer.pixels().data(), w * 4);
      } else {
        std::vector<uint32_t> blank(size_t(w) * h, 0xFF302820u);
        SDL_UpdateTexture(tex, nullptr, blank.data(), w * 4);
      }
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
      uint32_t now = SDL_GetTicks();
      if (now - last > 250) { setTitle(); last = now; }
      SDL_Delay(8);
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string gameArg, cityArg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--game-dir" || a == "--game") && i + 1 < argc) gameArg = argv[++i];
    else if (a == "--city" && i + 1 < argc) cityArg = argv[++i];
    else if (a == "--help" || a == "-h") {
      std::cout << "metroplex3k [--game-dir DIR] [--city FILE]\n"
                   "Game data: DIR (game root, its Apps folder, or a parent), $M3K_GAME_DIR, the saved choice,\n"
                   "or auto-detected GOG install locations. You can also drop the folder or the GOG\n"
                   "installer onto the window.\n";
      return 0;
    }
  }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << "\n"; return 1; }
  App app;
  app.win = SDL_CreateWindow("Metroplex3K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_RESIZABLE);
  if (!app.win) { std::cerr << "window: " << SDL_GetError() << "\n"; return 1; }
  app.ren = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!app.ren) app.ren = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_SOFTWARE);
  if (!app.ren) { std::cerr << "renderer: " << SDL_GetError() << "\n"; return 1; }

  fs::path root = gameArg.empty() ? detectGameRoot() : findGameRoot(gameArg);
  if (!root.empty() && app.initGame(root)) {
    if (!cityArg.empty()) {
      Bytes b;
      if (readFile(cityArg, b)) { try { app.adopt(m3k::loadCity(b)); } catch (const std::exception& e) { std::cerr << e.what() << "\n"; } }
    } else app.loadCity(0);
  } else {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Metroplex3K - game data needed",
        "Metroplex3K needs the data files of your own copy of SimCity 3000 Unlimited.\n\n"
        "Drop the install folder (the one containing 'Apps') or the GOG installer (.exe) onto the "
        "Metroplex3K window, or start with --game-dir <folder>.\n\n"
        "Until then you can press N to explore generated terrain.",
        app.win);
    app.newCity(128);
  }
  app.run();
  SDL_Quit();
  return 0;
}
