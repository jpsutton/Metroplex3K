// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Metroplex3K desktop host (SDL2). All game logic is in src/core (Game); this file pumps events and
// drives a FrameBackend so other hosts (Android, later GPU present) can reuse the core unchanged.
#include <SDL.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "core/city_rasterizer.h"
#include "core/game.h"
#include "core/gamedir.h"
#include "core/version.h"
#include "frame_log.h"
#include "gl_compute.h"
#include "gl_presenter.h"
#include "sdl_presenter.h"

using namespace m3k;

// Environment switch: unset or empty -> `def`; "0", "off", "false", "no" -> false; anything else -> true.
static bool envFlag(const char* name, bool def) {
  const char* v = std::getenv(name);
  if (!v || !*v) return def;
  return !(std::strcmp(v, "0") == 0 || std::strcmp(v, "off") == 0 || std::strcmp(v, "false") == 0 || std::strcmp(v, "no") == 0);
}

static fs::path exeDirOf(const char* argv0) {
  std::error_code ec;
  fs::path p = fs::absolute(argv0 ? argv0 : "", ec);
  return ec ? fs::path() : p.parent_path();
}

// SDL key -> Windows virtual-key code (spec/08 section 5.2); 0 = not a bindable key.
static int vkFromSdl(SDL_Keycode k) {
  if (k >= SDLK_a && k <= SDLK_z) return 'A' + int(k - SDLK_a);
  if (k >= SDLK_0 && k <= SDLK_9) return '0' + int(k - SDLK_0);
  if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return 97 + int(k - SDLK_KP_1);
  if (k == SDLK_KP_0) return 96;
  if (k >= SDLK_F1 && k <= SDLK_F12) return 112 + int(k - SDLK_F1);
  switch (k) {
    case SDLK_KP_PLUS: return 107; case SDLK_KP_MINUS: return 109;
    case SDLK_PAGEUP: return 33; case SDLK_PAGEDOWN: return 34;
    case SDLK_LEFT: return 37; case SDLK_UP: return 38; case SDLK_RIGHT: return 39; case SDLK_DOWN: return 40;
    case SDLK_ESCAPE: return 27; case SDLK_SPACE: return 32; case SDLK_RETURN: return 13;
    case SDLK_COMMA: return 188; case SDLK_PERIOD: return 190; case SDLK_SLASH: return 191;
    default: return 0;
  }
}

// Audio callback: stereo S16 from AudioSystem::mixAudio. AudioMixer locks its own voice list; the device lock
// (SDL_LockAudioDevice, via Game::audioLock/audioUnlock) is taken only while Game replaces its AudioSystem.
static void audioCallback(void* user, Uint8* stream, int len) {
  Game* g = static_cast<Game*>(user);
  AudioSystem* a = g->audio();
  if (!a) { std::memset(stream, 0, size_t(len)); return; }
  a->mixAudio(reinterpret_cast<int16_t*>(stream), size_t(len) / 4);
}

int main(int argc, char** argv) {
  std::string gameArg, cityArg, langArg, frameLogArg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--game-dir" || a == "--game") && i + 1 < argc) gameArg = argv[++i];
    else if (a == "--city" && i + 1 < argc) cityArg = argv[++i];
    else if (a == "--frame-log" && i + 1 < argc) frameLogArg = argv[++i];
    else if ((a == "--language" || a == "-l") && i + 1 < argc) langArg = argv[++i];
    else if (a == "--version" || a == "-V") {
      std::cout << "Metroplex3K " << M3K_VERSION_STRING << " (" << M3K_GIT_SHA << ")\n";
      return 0;
    }
    else if (a == "--help" || a == "-h") {
      std::cout << "metroplex3k [--version] [--game-dir DIR] [--city FILE] [--language NAME] [--frame-log FILE]\n"
                   "Language: NAME (English, French, German, ...), $M3K_LANGUAGE, else the OS locale (an installed language).\n"
                   "Game data: DIR (game root, its Apps folder, or a parent), $M3K_GAME_DIR, the saved choice,\n"
                   "or auto-detected GOG install locations. You can also drop the folder or the GOG installer\n"
                   "onto the window, or type the path on the first screen.\n"
                   "Render: $M3K_RENDER=cpu|gpu (default cpu). gpu uses OpenGL compute for terrain when available.\n"
                   "Present: $M3K_PRESENT=gl (default) presents through OpenGL and, with 4.3, composites the city sprites on the\n"
                   "GPU; gl-basic only presents through GL; sdl uses the SDL renderer. Falls back to sdl if OpenGL is unavailable.\n"
                   "Render caches (default on): $M3K_TERRAIN_CACHE=0 / $M3K_ITEM_CACHE=0 turn off the cached background + terrain\n"
                   "layer / the cached sprite draw list. The GPU city needs the terrain cache and turns itself off without it.\n"
                   "Sim thread (default on): $M3K_SIM_THREAD=0 runs the sim day tick on the frame thread instead of a worker thread.\n"
                   "Terrain noise: seeded from the clock like the original; $M3K_NOISE_SEED=N pins it (reproducible captures).\n"
                   "Frame log: --frame-log FILE or $M3K_FRAMELOG=FILE writes a per-frame timing CSV and prints a summary on exit.\n";
      return 0;
    }
  }
  SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");   // closing the window is a request, not a quit
#if defined(__linux__)
  // Prefer native Wayland, fall back to X11 (SDL2 alone picks X11 whenever it is available). An explicit SDL_VIDEODRIVER wins. The environment
  // variable is used rather than SDL_HINT_VIDEODRIVER because sdl2-compat (SDL3 underneath) ignores the hint.
  setenv("SDL_VIDEODRIVER", "wayland,x11", 0);
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << "\n"; return 1; }
  // GL present: the window itself owns a GL context. Falls back to the SDL renderer when unavailable.
  std::unique_ptr<GlPresenter> glPresent;
  bool gpuCityWanted = false;
  SDL_Window* win = nullptr;
#if defined(M3K_HAS_GL_PRESENT) && M3K_HAS_GL_PRESENT
  // Default: GL present with the GPU city. M3K_PRESENT=sdl forces the SDL renderer, gl-basic the GL present alone.
  const char* presentEnv = std::getenv("M3K_PRESENT");
  const std::string presentMode = (presentEnv && *presentEnv) ? presentEnv : "gl";
  if (presentMode == "gl" || presentMode == "gl-basic") {
    gpuCityWanted = presentMode == "gl";
    win = SDL_CreateWindow("Metroplex3K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    glPresent = std::make_unique<GlPresenter>();
    if (!win || !glPresent->init(win)) {
      std::cerr << "metroplex3k: OpenGL present unavailable (context failed); using the SDL renderer.\n";
      glPresent.reset();
      gpuCityWanted = false;
      if (win) { SDL_DestroyWindow(win); win = nullptr; }
    }
  }
#endif
  if (!win) win = SDL_CreateWindow("Metroplex3K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_RESIZABLE);
  if (!win) { std::cerr << "window: " << SDL_GetError() << "\n"; return 1; }

  // Phase 2 compute terrain: init before SDL_Renderer so the hidden GL context is ready.
  GlComputeTerrain glTerrain;
  const bool wantGpu = renderBackendFromEnv() == RenderBackendKind::Gpu;
  if (wantGpu && !glTerrain.init())
    std::cerr << "metroplex3k: M3K_RENDER=gpu requested but OpenGL 4.3 compute is unavailable; using CPU terrain.\n";

  std::srand(uint32_t(SDL_GetTicks()));
  Game game;
  {   // time-seeded terrain noise like the original (spec 25 6.3); $M3K_NOISE_SEED pins it for reproducible captures
    const char* ns = std::getenv("M3K_NOISE_SEED");
    game.seedTerrainNoise(ns && *ns ? uint32_t(std::strtoul(ns, nullptr, 0)) : uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()));
  }
  if (!langArg.empty()) game.setLanguage(langArg);
  fs::path root = gameArg.empty() ? detectGameRoot(exeDirOf(argv[0])) : findGameRoot(gameArg);
  std::string err;
  if (!game.init(root, err) && !err.empty()) std::cerr << err << "\n";
  if (!cityArg.empty()) game.requestFile(cityArg);
  const bool simThread = envFlag("M3K_SIM_THREAD", true);
  game.setAsyncSim(simThread);
  bool terrainCache = envFlag("M3K_TERRAIN_CACHE", true);
  game.renderer().setTerrainLayerCache(terrainCache);
  bool itemCache = envFlag("M3K_ITEM_CACHE", true);
  // GPU city: the sprite pass needs the terrain layer cache; the item cache makes the draw list cheap.
  const bool gpuCity = glPresent && gpuCityWanted && glPresent->hasGpuCity() && terrainCache;   // the sprite pass needs the layer cache
  if (glPresent) glPresent->setGpuCityEnabled(gpuCity);
  game.renderer().setItemListCache(itemCache);

  if (wantGpu && glTerrain.available()) {
    game.renderer().setGpuTerrainWanted(true);
    game.renderer().setGpuTerrainFn(
        [&glTerrain](uint32_t* fb, int w, int h, int zoom, const int8_t* nL, const int8_t* nE,
                     const GpuTerrainTri* tris, int n) {
          return glTerrain.fill(fb, w, h, zoom, nL, nE, tris, n);
        });
  }

  // Create the renderer after prefs load so the initial vsync flag matches Engine Options / OwnPrefs.
  bool wantVsync = game.engineOptions().vsync;
  SDL_Renderer* ren = nullptr;
  std::unique_ptr<SdlPresenter> sdlPresent;
  FrameBackend* presenter = nullptr;
  if (glPresent) {
    presenter = glPresent.get();
  } else {
    Uint32 rflags = SDL_RENDERER_ACCELERATED | (wantVsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    ren = SDL_CreateRenderer(win, -1, rflags);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { std::cerr << "renderer: " << SDL_GetError() << "\n"; return 1; }
    sdlPresent = std::make_unique<SdlPresenter>(ren);
    presenter = sdlPresent.get();
  }
  presenter->setVsync(wantVsync);

  // Audio: opened only when game data with AUDIO.INI is present (also after data is dropped in later);
  // otherwise the game stays silent.
  SDL_AudioDeviceID adev = 0;
  bool audioTried = false;
  auto openAudio = [&] {
    if (adev || audioTried || !game.audio()) return;
    audioTried = true;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audioCallback; want.userdata = &game;
    adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // no format changes allowed: mixer is 44.1k S16 stereo
    if (adev) {
      SDL_PauseAudioDevice(adev, 0);
      game.audioLock = [adev] { SDL_LockAudioDevice(adev); };
      game.audioUnlock = [adev] { SDL_UnlockAudioDevice(adev); };
    } else std::cerr << "audio: " << SDL_GetError() << "\n";
  };
  openAudio();

  FrameLog frameLog;
  if (frameLogArg.empty())
    if (const char* e = std::getenv("M3K_FRAMELOG")) frameLogArg = e;
  if (!frameLogArg.empty()) {
    const bool gpuActive = wantGpu && glTerrain.available();
    if (frameLog.open(frameLogArg, gpuActive ? "gpu" : "cpu", std::string(glPresent ? (gpuCity ? "gpucity," : "glpresent,") : "") + std::string(simThread ? "thread" : "sync") + (terrainCache ? "+terrain-cache" : "") + (itemCache ? "+item-cache" : ""))) std::cerr << "metroplex3k: logging frame times to " << frameLogArg << "\n";
    else std::cerr << "metroplex3k: cannot open frame log " << frameLogArg << "\n";
  }
  auto msBetween = [](FrameLog::Clock::time_point a, FrameLog::Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };

  SDL_StartTextInput();
  Input in;
  uint32_t last = SDL_GetTicks();
  bool panDrag = false;
  std::string curName, curTitle;
  SDL_Cursor* cursor = nullptr;
  while (!game.wantQuit()) {
    const auto tStart = FrameLog::Clock::now();
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.text.clear();
    std::memset(in.key, 0, sizeof in.key);
    in.keyDowns.clear();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: goto done;   // OS-level termination (SIGTERM, session end): leave at once
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_CLOSE) game.requestQuit();   // the close button: the original's Exit prompts
          break;
        case SDL_DROPFILE: game.dropPath(e.drop.file); SDL_free(e.drop.file); break;
        case SDL_MOUSEMOTION: in.mx = e.motion.x; in.my = e.motion.y; break;
        case SDL_MOUSEWHEEL: in.wheel += e.wheel.y; break;
        case SDL_MOUSEBUTTONDOWN:
          in.mx = e.button.x; in.my = e.button.y;
          if (e.button.button == SDL_BUTTON_LEFT) { in.pressed = true; in.down = true; }
          else if (e.button.button == SDL_BUTTON_RIGHT) { in.rightPressed = true; in.rightDown = true; }
          break;
        case SDL_MOUSEBUTTONUP:
          if (e.button.button == SDL_BUTTON_LEFT) { in.released = true; in.down = false; }
          else if (e.button.button == SDL_BUTTON_RIGHT) in.rightDown = false;
          break;
        case SDL_TEXTINPUT: in.text += e.text.text; break;
        case SDL_KEYDOWN:
          if (!e.key.repeat) {
            int vk = vkFromSdl(e.key.keysym.sym);
            if (vk) {
              SDL_Keymod m = SDL_GetModState();
              in.keyDowns.push_back({vk, ((m & KMOD_SHIFT) ? 1 : 0) | ((m & KMOD_CTRL) ? 2 : 0) | ((m & KMOD_ALT) ? 4 : 0)});
            }
          }
          switch (e.key.keysym.sym) {
            case SDLK_RETURN: case SDLK_KP_ENTER: in.key[K_ENTER] = true; break;
            case SDLK_ESCAPE: in.key[K_ESC] = true; break;
            case SDLK_BACKSPACE: in.key[K_BACKSPACE] = true; break;
            case SDLK_UP: in.key[K_UP] = true; break;
            case SDLK_DOWN: in.key[K_DOWN] = true; break;
            case SDLK_PAGEUP: in.key[K_PAGEUP] = true; break;
            case SDLK_PAGEDOWN: in.key[K_PAGEDOWN] = true; break;
            case SDLK_F11: SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); break;
            default: break;
          }
          break;
        default: break;
      }
    }
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    { const SDL_Keymod km = SDL_GetModState(); in.modCtrl = (km & KMOD_CTRL) != 0; in.modAlt = (km & KMOD_ALT) != 0; }
    in.held[0] = ks[SDL_SCANCODE_LEFT];
    in.held[1] = ks[SDL_SCANCODE_RIGHT];
    in.held[2] = ks[SDL_SCANCODE_UP];
    in.held[3] = ks[SDL_SCANCODE_DOWN];
    (void)panDrag;
    int w, h; SDL_GetWindowSize(win, &w, &h);
    uint32_t now = SDL_GetTicks();
    openAudio();

    const bool vsyncNow = game.engineOptions().vsync;
    if (vsyncNow != wantVsync) {
      wantVsync = vsyncNow;
      presenter->setVsync(wantVsync);
    }

    const auto tGame0 = FrameLog::Clock::now();
    FrameBuffer fb;
    if (presenter->beginFrame(w, h, fb)) {
      game.frame(in, now - last, fb);
      presenter->endFrame();
    } else {
      game.frame(in, w, h, now - last);
      if (sdlPresent) sdlPresent->upload(game.screen().data(), w, h);
    }
    last = now;
    const auto tGame1 = FrameLog::Clock::now();
    presenter->present();
    const auto tPresent1 = FrameLog::Clock::now();

    if (game.windowTitle() != curTitle) { curTitle = game.windowTitle(); SDL_SetWindowTitle(win, curTitle.c_str()); }
    if (game.cursorName() != curName) {
      curName = game.cursorName();
      if (cursor) { SDL_FreeCursor(cursor); cursor = nullptr; }
      CursorImage ci;
      if (!curName.empty() && game.cursorImage(curName, ci) && ci.ok()) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ci.image.w, ci.image.h, 32, SDL_PIXELFORMAT_ABGR8888);
        if (surf) {
          SDL_memcpy(surf->pixels, ci.image.px.data(), size_t(ci.image.w) * ci.image.h * 4);
          cursor = SDL_CreateColorCursor(surf, ci.hotX, ci.hotY);
          SDL_FreeSurface(surf);
        }
      }
      SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
    }
    const auto tDelay0 = FrameLog::Clock::now();
    if (!wantVsync) SDL_Delay(8);   // with vsync, present() already paces the loop
    if (frameLog.active()) {
      FrameLog::Sample smp;
      smp.loopMs = msBetween(tStart, FrameLog::Clock::now());   // this iteration's own duration, vsync wait and delay included
      smp.eventsMs = msBetween(tStart, tGame0);
      smp.gameMs = msBetween(tGame0, tGame1);
      smp.presentMs = msBetween(tGame1, tPresent1);
      smp.delayMs = msBetween(tDelay0, FrameLog::Clock::now());
      smp.w = w; smp.h = h;
      frameLog.add(smp, game.lastFramePhases());
    }
  }
done:
  if (adev) SDL_CloseAudioDevice(adev);
  SDL_Quit();
  return 0;
}
