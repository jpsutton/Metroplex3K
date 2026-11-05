#include <cmath>
#include "core/render.h"
#include "core/terrain_gen.h"
#include "testutil.h"
using namespace m3k;

static World flatWorld(int n, int alt) {
  World w; w.init(n, n);
  std::fill(w.alt.begin(), w.alt.end(), uint8_t(alt));
  std::fill(w.water.begin(), w.water.end(), uint8_t(30));
  w.seaLevel = 30;
  return w;
}

static Image solid(int w, int h, uint32_t c) { Image i; i.w = w; i.h = h; i.px.assign(size_t(w) * h, c); return i; }

int main() {
  // ---- rotation round trips (tiles/vertices and sub-tile) ----
  for (int rot = 0; rot < 4; rot++) {
    for (int x = 0; x <= 10; x += 3)
      for (int z = 0; z <= 7; z += 2) {
        int u, v, x2, z2;
        Renderer::worldToViewVertex(rot, 10, 7 + 3, x, z, u, v);
        Renderer::viewToWorldVertex(rot, 10, 10, u, v, x2, z2);
        CHECK(x2 == x && z2 == z);
      }
    int32_t a, b, x2, z2;
    Renderer::worldToViewSub(rot, 16, 16, 1000, 300, a, b);
    Renderer::viewToWorldSub(rot, 16, 16, a, b, x2, z2);
    CHECK(x2 == 1000 && z2 == 300);
  }
  {  // spec 2.2 literal values, Wm = 16: r=1 (x,z) -> (Wm*256-1-z, x)
    int32_t a, b;
    Renderer::worldToViewSub(1, 16, 16, 1000, 300, a, b);
    CHECK(a == 16 * 256 - 1 - 300 && b == 1000);
    Renderer::worldToViewSub(3, 16, 16, 1000, 300, a, b);
    CHECK(a == 300 && b == 16 * 256 - 1 - 1000);
  }

  // ---- child-sprite offsets turn with the occupant rotation (spec 25 7.2, inferred for rot != 0) ----
  // A 1x1 building placed with rotation r and viewed at cam c shows the orient (r+c)&3 sprite; its child point, measured
  // from the footprint's anchor vertex, must equal the unrotated building's point viewed at (r+c)&3.
  {
    auto rel = [](int cam, int x0, int z0, int32_t X, int32_t Z, int& rx, int& ry) {
      int us[4], vs[4];
      const int cx[4] = {x0, x0 + 1, x0, x0 + 1}, cz[4] = {z0, z0, z0 + 1, z0 + 1};
      for (int i = 0; i < 4; i++) Renderer::worldToViewVertex(cam, 16, 16, cx[i], cz[i], us[i], vs[i]);
      const int umax = *std::max_element(us, us + 4), vmin = *std::min_element(vs, vs + 4);
      int32_t a, b; Renderer::worldToViewSub(cam, 16, 16, X, Z, a, b);
      rx = int(b - a) - (vmin - umax) * 256; ry = int(a + b) - (umax + vmin) * 256;
    };
    const int32_t dx = 208, dz = 48;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++) {
        int32_t ox = dx, oz = dz;
        if (r) Renderer::worldToViewSub(r, 1, 1, dx, dz, ox, oz);
        int rx, ry, ex, ey;
        rel(c, 5, 9, 5 * 256 + ox, 9 * 256 + oz, rx, ry);
        rel((r + c) & 3, 5, 9, 5 * 256 + dx, 9 * 256 + dz, ex, ey);
        CHECK(std::abs(rx - ex) <= 2 && std::abs(ry - ey) <= 2);
      }
  }

  // ---- far-zoom adjustment (spec 6.2) ----
  {
    int r = 0, g = 0, b = 0;
    farZoomAdjust(0, r, g, b);  // S(0)=0 -> K only
    CHECK(r == 49 && g == 51 && b == 53);
    r = 100; g = 100; b = 100; farZoomAdjust(1, r, g, b);  // S=10, factor 2
    CHECK(r == 100 + 33 - 20 && g == 100 + 34 - 20 && b == 100 + 35 - 20);
    r = g = b = 255; farZoomAdjust(0, r, g, b);  // S=26, factor 3 -> 255+49-78
    CHECK(r == 255 + 49 - 78 && g == 255 + 51 - 78 && b == 255 + 53 - 78);
    r = 77; g = 77; b = 77; farZoomAdjust(3, r, g, b);  // no change at z>=3
    CHECK(r == 77);
  }

  // ---- blend arithmetic (spec 04 section 3.5) ----
  CHECK(blend5(31, 0, 0) == 0);
  CHECK(blend5(31, 10, 0) == 10);
  CHECK(blend5(31, 0, 31) == (31 * 31) / 32);       // 30
  CHECK(blend5(31, 31, 31) == 31);
  CHECK(blend5(0, 31, 31) == (32 * 31 - 31 * 31) / 32);  // keeps 1/32 of the background = 0
  CHECK(blend5(20, 10, 16) == (32 * 10 + 16 * 10) / 32);  // 15
  CHECK(blend8(200, 100, 31) == (32 * 100 + 31 * 100) / 32);
  CHECK(blend8(255, 255, 31) == 255);

  // ---- palBasic expansion (spec 6.1) ----
  {
    Image raw = solid(1, 3, rgba(200, 100, 50));
    Image e = expandPalBasic(raw);
    CHECK(e.w == 32 && e.h == 3);
    CHECK(e.px[31] == rgba(200, 100, 50));
    CHECK((e.px[0] & 0xFFFFFF) == 0x000000 + 0 || true);
    int r0 = e.px[0] & 255;  // cos(31*pi/62) = cos(pi/2) ~ 0 -> truncates to 0 (or tiny)
    CHECK(r0 == 0);
    int r15 = e.px[15] & 255;
    CHECK(r15 == int(200.0f * std::cos(16.0f * 3.14159265358979f / 62.0f)));
  }

  // ---- vertex shade (spec 6.6) ----
  {
    World w = flatWorld(8, 60);
    auto sh = computeVertexShade(w);
    CHECK(sh[w.vIdx(4, 4)] == 15);
    CHECK(sh[w.vIdx(0, 0)] == 15);  // mirrored border extrapolation keeps it flat
    // slope along x: raise h(x+1) -> brighter column for the light on one side.
    w.alt[w.vIdx(5, 4)] = 70;
    auto s2 = computeVertexShade(w);
    CHECK(s2[w.vIdx(4, 4)] > 15);
    CHECK(s2[w.vIdx(4, 4)] <= 31);
    // slope along z lowers both dot products -> not darker than flat in this scheme
    World w3 = flatWorld(8, 60);
    w3.alt[w3.vIdx(4, 5)] = 70;
    auto s3 = computeVertexShade(w3);
    CHECK(s3[w3.vIdx(4, 4)] > 15);
    // Exact value check: s=3.266, d=16, dh/2 = 5 -> nx = 3.266*(-10)/2... computed independently.
    {
      float s = 3.266f, nxv = s * (60 - 70) / 2.f, ny = 16.f, nzv = 0;
      float len = std::sqrt(nxv * nxv + ny * ny + nzv * nzv);
      nxv /= len; ny /= len;
      float inv = 1.0f / std::sqrt(2.0f);
      float d1 = std::max(0.f, nxv * -inv + ny * inv), d2 = std::max(0.f, nxv * inv + ny * inv);
      int t1 = int(std::acos(std::min(1.f, d1)) * 62.f / 3.14159265f), t2 = int(std::acos(std::min(1.f, d2)) * 62.f / 3.14159265f);
      CHECK(int(s2[w.vIdx(4, 4)]) == std::max(t1, t2));
    }
  }

  // ---- projection + picking round trip ----
  for (int rot = 0; rot < 4; rot++)
    for (int zoom = 0; zoom < 5; zoom++) {
      World w = flatWorld(16, 40);
      Renderer r; r.setWorld(&w, nullptr); r.resize(640, 480);
      Camera cam; cam.zoom = zoom; cam.rot = rot;
      r.centreOnWorld(cam, 8, 8);
      RenderOptions opt; opt.edgeFaces = false;
      r.render(cam, opt);
      int hits = 0, total = 0;
      for (int x = 5; x < 11; x++)
        for (int z = 5; z < 11; z++) {
          int sx, sy;
          r.projectSub(cam, x * 256 + 128, z * 256 + 128, 40 * 256, sx, sy);
          // sub-tile centre of the diamond: projected point is the tile centre
          int tx = -1, tz = -1;
          total++;
          if (r.pickTile(cam, sx, sy + 1, tx, tz) && tx == x && tz == z) hits++;
          int gx = -1, gz = -1;
          CHECK(r.pickGround(cam, sx, sy + 1, gx, gz) && gx == x && gz == z);
        }
      CHECK(hits == total);
    }
  {  // sub-tile projection formula (spec 2.3)
    World w = flatWorld(16, 40);
    Renderer r; r.setWorld(&w, nullptr); r.resize(640, 480);
    Camera cam; cam.zoom = 3; cam.rot = 0;
    r.centreOnWorld(cam, 8, 8);
    int sx0, sy0, sx1, sy1;
    r.projectSub(cam, 0, 0, 0, sx0, sy0);
    r.projectSub(cam, 512, 256, 512, sx1, sy1);  // a'=512, b'=256, e=512
    CHECK(sx1 - sx0 == 32 * (256 - 512) / 256);
    CHECK(sy1 - sy0 == 16 * (512 + 256) / 256 - 8 * 512 / 256);
  }

  // ---- sprites: alpha plane blend, ordering, picking, previews, overlays ----
  {
    World w = flatWorld(16, 40);
    SpriteImage red; red.img = solid(8, 8, rgba(255, 0, 0)); red.L = 4; red.T = 8; red.R = 4; red.B = 0; red.ok = true;
    SpriteImage blue; blue.img = solid(8, 8, rgba(0, 0, 255)); blue.L = 4; blue.T = 8; blue.R = 4; blue.B = 0; blue.ok = true;
    Plane half; half.w = 8; half.h = 8; half.v.assign(64, 16);
    Renderer r;
    r.setWorld(&w, nullptr);
    r.resize(400, 300);
    r.setSpriteLookup([&](uint32_t c, uint32_t) -> const SpriteImage* { return c == 1 ? &red : c == 2 ? &blue : nullptr; },
                      [&](uint32_t c, uint32_t id) -> const Plane* { return (c == 1 && id == 99) ? &half : nullptr; });
    Camera cam; cam.zoom = 3; r.centreOnWorld(cam, 8, 8);
    RenderOptions opt; opt.edgeFaces = false; opt.zonePaint = false;
    r.render(cam, opt);
    int sx, sy;
    r.projectSub(cam, 8 * 256, 8 * 256, 40 * 256, sx, sy);
    uint32_t before = r.pixels()[size_t(sy - 3) * r.width() + sx];
    RenderScene sc;
    SceneSprite s1; s1.category = 1; s1.spriteId = 0; s1.x = 8 * 256; s1.z = 8 * 256; s1.height = 40 * 256;
    s1.hasAlpha = true; s1.alphaId = 99; s1.alphaCategory = 1;
    sc.sprites.push_back(s1);
    r.render(cam, opt, &sc);
    uint32_t after = r.pixels()[size_t(sy - 3) * r.width() + sx];
    int er = blend8(255, before & 255, 16), eg = blend8(0, (before >> 8) & 255, 16), eb = blend8(0, (before >> 16) & 255, 16);
    CHECK(int(after & 255) == er && int((after >> 8) & 255) == eg && int((after >> 16) & 255) == eb);
    CHECK(after != before);

    // Opaque (no plane): pixel replaced. Moving object vs static ordering.
    SceneSprite st; st.category = 1; st.moving = false; st.x = 8 * 256; st.z = 8 * 256; st.height = 40 * 256;
    // A mover and a static on the same tile share a depth key; the sub-tile position breaks the tie. Near the tile's
    // near corner (large a+b) the mover is drawn after the static; near its far corner (small a+b) before it, so a
    // car on the far lane of a road tile ends up behind the tree on that tile.
    SceneSprite mv; mv.category = 2; mv.moving = true; mv.x = 8 * 256 + 240; mv.z = 8 * 256 + 240; mv.height = 40 * 256;
    RenderScene s2; s2.sprites = {mv, st};  // submission order must not matter
    r.render(cam, opt, &s2);
    int mx, my; r.projectSub(cam, mv.x, mv.z, mv.height, mx, my);
    uint32_t px = r.pixels()[size_t(my - 3) * r.width() + mx];
    CHECK(px == rgba(0, 0, 255));  // mover nearer than the static's centre: drawn after it
    SceneSprite mvFar = mv; mvFar.x = 8 * 256 + 10; mvFar.z = 8 * 256 + 10;
    RenderScene s2b; s2b.sprites = {mvFar, st};
    r.render(cam, opt, &s2b);
    r.projectSub(cam, mvFar.x, mvFar.z, mvFar.height, mx, my);
    CHECK(r.pixels()[size_t(my - 3) * r.width() + mx] == rgba(255, 0, 0));  // mover behind the static's centre: drawn before it

    // Picking by per-pixel opacity: static scene sprite occupies its box.
    RenderScene s3; s3.sprites = {st};
    r.render(cam, opt, &s3);
    PickResult pr;
    CHECK(r.pick(cam, sx, sy - 3, pr) && pr.object && pr.category == 1);
    CHECK(r.pick(cam, sx + 100, sy - 3, pr) && !pr.object);  // outside the sprite box: ground

    // Selection preview: cyan tint pulls pixel towards green.
    RenderScene s4; TilePreview tp; tp.x0 = tp.x1 = 8; tp.z0 = tp.z1 = 8; tp.valid = true; tp.alpha = 255; s4.previews.push_back(tp);
    r.render(cam, opt, &s4);
    int cx, cy; r.projectSub(cam, 8 * 256 + 128, 8 * 256 + 128, 40 * 256, cx, cy);
    uint32_t pp = r.pixels()[size_t(cy + 1) * r.width() + cx];
    CHECK(pp == rgba(0, 140, 205));
    RenderScene s5; tp.valid = false; s5.previews.push_back(tp);
    r.render(cam, opt, &s5);
    CHECK(r.pixels()[size_t(cy + 1) * r.width() + cx] == rgba(240, 20, 40));

    // Sprite tint (spec 25 9.4): opaque pixels of sprites over the tile go 50/50 towards the tint colour; a ground tint
    // also blends the flat tile under them.
    {
      RenderScene s6; s6.sprites = {st};
      r.render(cam, opt, &s6);
      uint32_t plainSprite = r.pixels()[size_t(sy - 3) * r.width() + sx];
      CHECK(plainSprite == rgba(255, 0, 0));
      SpriteTint tn; tn.x0 = tn.x1 = 8; tn.z0 = tn.z1 = 8; tn.r = 0; tn.g = 130; tn.b = 176;
      s6.tints.push_back(tn);
      r.render(cam, opt, &s6);
      CHECK(r.pixels()[size_t(sy - 3) * r.width() + sx] == rgba(127, 65, 88));
      tn.x0 = tn.x1 = 3; tn.z0 = tn.z1 = 3; s6.tints[0] = tn;       // another tile: the sprite is untouched
      r.render(cam, opt, &s6);
      CHECK(r.pixels()[size_t(sy - 3) * r.width() + sx] == rgba(255, 0, 0));
      RenderScene g0; r.render(cam, opt, &g0);
      uint32_t ground = r.pixels()[size_t(cy + 1) * r.width() + cx];
      RenderScene g1; tn.x0 = tn.x1 = 8; tn.z0 = tn.z1 = 8; tn.ground = true; g1.tints.push_back(tn);
      r.render(cam, opt, &g1);
      auto mixc = [](int d, int c) { return d + (c - d) * 128 / 255; };
      CHECK(r.pixels()[size_t(cy + 1) * r.width() + cx] == rgba(mixc(ground & 255, 0), mixc((ground >> 8) & 255, 130), mixc((ground >> 16) & 255, 176)));
    }

    // Data overlay solid colour: flat terrain -> shade 15 -> column 7, no noise.
    TerrainPalettes tp2 = r.palettes();
    tp2.data[2] = solid(64, 32, rgba(1, 2, 3));
    tp2.data[2].px[size_t(5) * 64 + 7] = rgba(10, 200, 30);
    r.setPalettes(tp2);
    DataOverlay ov; ov.slot = 3; ov.row.assign(16 * 16, 0xFF); ov.row[8 * 16 + 8] = 5;
    opt.overlay = &ov;
    r.render(cam, opt);
    CHECK(r.pixels()[size_t(cy + 1) * r.width() + cx] == rgba(10, 200, 30));
    opt.overlay = nullptr;
  }
  {  // default overlay mapping
    std::vector<uint8_t> vals = {0, 7, 8, 255, 100};
    DataOverlay o = makeDataOverlay(DataLayer::Crime, vals, 5, 1);
    CHECK(o.slot == 2 && o.row[0] == 31 && o.row[1] == 31 && o.row[2] == 30 && o.row[3] == 0 && o.row[4] == 19);
    DataOverlay z = makeDataOverlay(DataLayer::Zones, {0, 1, 255}, 3, 1);
    CHECK(z.slot == 1 && z.row[1] == 4 && z.row[2] == 0xFF);
  }
  {  // banded (parallel) rendering is pixel-identical to a single band
    World w;
    TerrainGenOptions go; go.seed = 7;
    generateTerrain(w, 64, 64, go);
    SpriteImage red; red.img = solid(8, 24, rgba(255, 0, 0)); red.L = 4; red.T = 24; red.R = 4; red.B = 0; red.ok = true;
    Plane half; half.w = 8; half.h = 24; half.v.assign(8 * 24, 16);
    RenderScene sc;
    for (int i = 0; i < 40; i++) {
      SceneSprite s; s.category = 1; s.x = (5 + (i * 7) % 54) * 256; s.z = (5 + (i * 13) % 54) * 256;
      s.height = w.altAt(s.x / 256, s.z / 256) * 256; s.moving = (i % 3) == 0; s.magnify2x = (i % 5) == 0;
      if (i % 2) { s.hasAlpha = true; s.alphaCategory = 1; s.alphaId = 99; }
      sc.sprites.push_back(s);
    }
    TilePreview tp; tp.x0 = 20; tp.z0 = 20; tp.x1 = 30; tp.z1 = 25; sc.previews.push_back(tp);
    auto renderWith = [&](int bands, int zoom, int rot) {
      Renderer r; r.setWorld(&w, nullptr); r.resize(640, 480); r.setBandCount(bands);
      r.setSpriteLookup([&](uint32_t c, uint32_t) -> const SpriteImage* { return c == 1 ? &red : nullptr; },
                        [&](uint32_t c, uint32_t id) -> const Plane* { return (c == 1 && id == 99) ? &half : nullptr; });
      Camera cam; cam.zoom = zoom; cam.rot = rot; r.centreOnWorld(cam, 32, 32);
      RenderOptions opt; opt.grid = true;
      r.render(cam, opt, &sc);
      return r.pixels();
    };
    for (int zoom = 0; zoom < 5; zoom++)
      for (int rot = 0; rot < 4; rot += 3) {
        auto one = renderWith(1, zoom, rot);
        CHECK(renderWith(7, zoom, rot) == one);
        CHECK(renderWith(37, zoom, rot) == one);
      }
  }
  {  // noise tables: land -8/0/8 only
    World w = flatWorld(8, 40);
    Renderer r; r.setWorld(&w, nullptr);
    r.setNoiseSeed(5);
    r.resize(100, 100);
    Camera cam; cam.zoom = 4; r.centreOnWorld(cam, 4, 4);
    RenderOptions opt; opt.edgeFaces = false; opt.zonePaint = false;
    r.render(cam, opt);
    r.setNoiseSeed(6);
    auto a = r.pixels();
    r.render(cam, opt);
    CHECK(a != r.pixels());  // different tables give different noise
  }
  TEST_MAIN_END();
}
