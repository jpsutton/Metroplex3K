// Checks that GlPresenter shows a Game frame pixel for pixel: runs a scripted game, and after each frame reads the
// presenter's output back and compares it to Game::screen() (alpha forced opaque).
//   gl_present_check --game DIR --city FILE [--frames N] [--size WxH]
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/game.h"
#include "gl_presenter.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string game, city;
  int frames = 300, W = 1280, H = 800;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
  }
  if (game.empty() || city.empty()) { std::fprintf(stderr, "usage: gl_present_check --game DIR --city FILE\n"); return 2; }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
  SDL_Window* win = SDL_CreateWindow("check", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  GlPresenter gl;
  if (!win || !gl.init(win)) { std::fprintf(stderr, "no GL 3.3 context\n"); return 3; }
  Game gm; gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) return 1;
  gm.setIntroEnabled(false);
  Input in; gm.requestFile(city);
  for (int i = 0; i < 40 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  gm.setPaused(false);
  int bad = 0;
  std::vector<uint32_t> back;
  for (int f = 0; f < frames; f++) {
    in.held[f / 40 % 4] = true; in.held[(f / 40 + 1) % 4] = false;
    FrameBuffer fb;
    if (!gl.beginFrame(W, H, fb)) { std::fprintf(stderr, "beginFrame failed\n"); return 1; }
    gm.frame(in, 16, fb);
    gl.endFrame();
    if (!gl.readback(back)) { std::fprintf(stderr, "readback failed\n"); return 1; }
    for (size_t i = 0; i < back.size(); i++)
      if (back[i] != (fb.pixels[i] | 0xFF000000u)) { bad++; std::printf("MISMATCH frame %d pixel %zu\n", f, i); break; }
  }
  std::printf("gl presenter: %d frames, %d mismatching\n", frames, bad);
  return bad ? 1 : 0;
}
