// Dumps the geometry of the disaster / effect sprites: image size, span, strip frame count and whether the id has an
// opacity plane. Used to build the FX set table in src/core/disaster_fx.cpp from the shipped data.
//   probe_fx_sprites [GAME_DIR] [hex-id-prefix ...]     (prefix = leading hex digits of the image id, e.g. 3fac FFFE50)
// With M3K_DUMP=DIR every listed colour image is also written to DIR/<id>.png on a grey background (opacity plane at
// id + $M3K_ALPHA_DELTA, default 5, blended when its size matches), to eyeball what a sprite set shows.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/gamedata.h"
#include "core/gamedir.h"
#include "core/resdb.h"
#include "probe_common.h"
#include "../png.h"
using namespace m3k;

int main(int argc, char** argv) {
  fs::path root;
  int used = probe::gameDirFromArgs(argc, argv, root);
  if (probe::needInstall(root, "probe_fx_sprites GAME_DIR [id-prefix...]")) return 1;
  std::vector<std::string> pre;
  for (int i = 1 + used; i < argc; i++) pre.push_back(probe::lower(argv[i]));
  GameData d; std::string err;
  if (!d.open(root, err)) { std::fprintf(stderr, "open: %s\n", err.c_str()); return 1; }
  std::vector<ResKey> keys = d.db().keysOfType(T_SPRITE_IMAGE);
  std::sort(keys.begin(), keys.end(), [](const ResKey& a, const ResKey& b) { return a.group != b.group ? a.group < b.group : a.instance < b.instance; });
  for (const ResKey& k : keys) {
    if (k.group != 0) continue;
    char hex[16]; std::snprintf(hex, sizeof hex, "%08x", k.instance);
    if (!pre.empty()) {
      bool ok = false;
      for (const auto& p : pre) if (std::string(hex).compare(0, p.size(), p) == 0) ok = true;
      if (!ok) continue;
    }
    const SpriteImage* s = d.sprite(0, k.instance);
    if (s) {
      const int fw = s->L + s->R;
      if (const char* dir = std::getenv("M3K_DUMP")) {
        const uint32_t ad = std::getenv("M3K_ALPHA_DELTA") ? uint32_t(std::strtoul(std::getenv("M3K_ALPHA_DELTA"), nullptr, 0)) : 5u;
        const Plane* pl = d.spritePlane(0, k.instance + ad);
        const bool usePl = pl && pl->w == s->img.w && pl->h == s->img.h;
        std::vector<uint32_t> px(s->img.px.size());
        for (size_t i = 0; i < px.size(); i++) {
          const uint32_t c = s->img.px[i];
          if (!(c >> 24)) { px[i] = 0xFF808080u; continue; }
          const int a = usePl ? pl->v[i] : 31;
          auto mix = [&](int sh) { return uint32_t((int((c >> sh) & 255) * a + 128 * (31 - a)) / 31); };
          px[i] = 0xFF000000u | mix(16) << 16 | mix(8) << 8 | mix(0);
        }
        writePng(std::string(dir) + "/" + hex + ".png", s->img.w, s->img.h, px);
      }
      std::printf("%s colour %4dx%-4d span L%-4d T%-4d R%-4d B%-4d frames %d%s\n", hex, s->img.w, s->img.h, s->L, s->T, s->R, s->B,
                  fw > 0 ? s->img.w / fw : 0, s->hasSpan ? "" : " (no span)");
    } else if (const Plane* p = d.spritePlane(0, k.instance)) {
      std::printf("%s plane  %4dx%-4d\n", hex, p->w, p->h);
    } else std::printf("%s undecodable\n", hex);
  }
  return 0;
}
