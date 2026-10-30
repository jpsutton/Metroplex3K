// Metroplex3K desktop host (SDL2). All game logic is in src/core (Game); this file only pumps events
// and presents the framebuffer, so other hosts (e.g. Android) can reuse the core unchanged.
#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "core/game.h"
#include "core/gamedir.h"

using namespace m3k;

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

// Audio callback: stereo S16 from AudioSystem::mixAudio. The device lock (SDL_LockAudioDevice) is held
// by Game::frame (via audioLock/audioUnlock) except while it renders, so the callback never races with game logic.
static void audioCallback(void* user, Uint8* stream, int len) {
  Game* g = static_cast<Game*>(user);
  AudioSystem* a = g->audio();
  if (!a) { std::memset(stream, 0, size_t(len)); return; }
  a->mixAudio(reinterpret_cast<int16_t*>(stream), size_t(len) / 4);
}

int main(int argc, char** argv) {
  std::string gameArg, cityArg, langArg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--game-dir" || a == "--game") && i + 1 < argc) gameArg = argv[++i];
    else if (a == "--city" && i + 1 < argc) cityArg = argv[++i];
    else if ((a == "--language" || a == "-l") && i + 1 < argc) langArg = argv[++i];
    else if (a == "--help" || a == "-h") {
      std::cout << "metroplex3k [--game-dir DIR] [--city FILE] [--language NAME]\n"
                   "Language: NAME (English, French, German, ...), $M3K_LANGUAGE, else the OS locale (an installed language).\n"
                   "Game data: DIR (game root, its Apps folder, or a parent), $M3K_GAME_DIR, the saved choice,\n"
                   "or auto-detected GOG install locations. You can also drop the folder or the GOG installer\n"
                   "onto the window, or type the path on the first screen.\n";
      return 0;
    }
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << "\n"; return 1; }
  SDL_Window* win = SDL_CreateWindow("Metroplex3K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_RESIZABLE);
  if (!win) { std::cerr << "window: " << SDL_GetError() << "\n"; return 1; }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { std::cerr << "renderer: " << SDL_GetError() << "\n"; return 1; }

  std::srand(uint32_t(SDL_GetTicks()));
  Game game;
  if (!langArg.empty()) game.setLanguage(langArg);
  fs::path root = gameArg.empty() ? detectGameRoot(exeDirOf(argv[0])) : findGameRoot(gameArg);
  std::string err;
  if (!game.init(root, err) && !err.empty()) std::cerr << err << "\n";
  if (!cityArg.empty()) game.requestFile(cityArg);

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

  SDL_StartTextInput();
  SDL_Texture* tex = nullptr; int tw = 0, th = 0;
  Input in;
  uint32_t last = SDL_GetTicks();
  bool panDrag = false;
  std::string curName;
  SDL_Cursor* cursor = nullptr;
  while (!game.wantQuit()) {
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.text.clear();
    std::memset(in.key, 0, sizeof in.key);
    in.keyDowns.clear();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: goto done;
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
    in.held[0] = ks[SDL_SCANCODE_LEFT];
    in.held[1] = ks[SDL_SCANCODE_RIGHT];
    in.held[2] = ks[SDL_SCANCODE_UP];
    in.held[3] = ks[SDL_SCANCODE_DOWN];
    (void)panDrag;
    int w, h; SDL_GetWindowSize(win, &w, &h);
    uint32_t now = SDL_GetTicks();
    openAudio();
    game.frame(in, w, h, now - last); last = now;   // takes the audio lock itself (see Game::audioLock)
    if (!tex || tw != w || th != h) {
      if (tex) SDL_DestroyTexture(tex);
      tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h); tw = w; th = h;
    }
    SDL_UpdateTexture(tex, nullptr, game.screen().data(), w * 4);
    SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
    SDL_SetWindowTitle(win, game.windowTitle().c_str());
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
    SDL_Delay(8);
  }
done:
  if (adev) SDL_CloseAudioDevice(adev);
  SDL_Quit();
  return 0;
}
