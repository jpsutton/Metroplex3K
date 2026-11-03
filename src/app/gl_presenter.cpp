#include "gl_presenter.h"

#if defined(M3K_HAS_GL_PRESENT) && M3K_HAS_GL_PRESENT

#include <SDL.h>
#include <GL/glew.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "core/gamedata.h"
#include "core/image.h"
#include "core/workpool.h"

namespace m3k {
namespace {

constexpr int kTile = 16;

const char* kVs = R"(#version 330 core
out vec2 uv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// Overlay (UI) over the city. Overlay alpha: 255 opaque, 0 city shows through, otherwise straight-alpha blend.
const char* kFs = R"(#version 330 core
in vec2 uv;
uniform usampler2D uOverlay;
uniform usampler2D uCity;
uniform int uCityVisible;
uniform ivec2 uSize;
out vec4 color;
void main() {
  ivec2 p = clamp(ivec2(uv * vec2(uSize)), ivec2(0), uSize - 1);
  uvec4 ov = texelFetch(uOverlay, p, 0);
  vec3 rgb;
  if (uCityVisible == 0 || ov.a == 255u) {
    rgb = vec3(ov.rgb);
  } else {
    uvec3 c = texelFetch(uCity, p, 0).rgb;
    if (ov.a == 0u) rgb = vec3(c);
    else {
      vec3 cf = vec3(c), of = vec3(ov.rgb);
      rgb = floor(cf + (of - cf) * float(ov.a) / 255.0 + 0.5);
    }
  }
  color = vec4(rgb / 255.0, 1.0);
})";

// One thread per pixel; each 16x16 group walks its tile's item list in draw order (same maths as
// Renderer::drawSprite: opaque overwrite, or 5-bit-alpha blend8 with the optional alpha plane).
const char* kCompose = R"(#version 430
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0) uniform usampler2D uBase;
layout(binding = 1) uniform usampler2D uAtlas;
layout(binding = 2) uniform usampler2D uPlane;
layout(rgba8ui, binding = 3) writeonly uniform uimage2D uOut;
struct Item { int x0, y0, w, h, sc, m2, ax, ay, px, py, blendAll, pad; };
layout(std430, binding = 0) readonly buffer Items { Item items[]; };
layout(std430, binding = 1) readonly buffer TileStart { uint tileStart[]; };
layout(std430, binding = 2) readonly buffer TileIdx { uint tileIdx[]; };
uniform ivec2 uSize;
uniform int uTilesX;
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  uvec4 d = texelFetch(uBase, p, 0);
  uint t = gl_WorkGroupID.y * uint(uTilesX) + gl_WorkGroupID.x;
  for (uint k = tileStart[t]; k < tileStart[t + 1u]; k++) {
    Item it = items[tileIdx[k]];
    ivec2 l = p - ivec2(it.x0, it.y0);
    if (l.x < 0 || l.y < 0 || l.x >= it.w || l.y >= it.h) continue;
    ivec2 s = l / it.sc;
    uvec4 sp = texelFetch(uAtlas, ivec2(it.ax, it.ay) + s, 0);
    if (sp.a == 0u) continue;
    if (it.blendAll == 0) { d = sp; continue; }
    int mask = it.px >= 0 ? int(texelFetch(uPlane, ivec2(it.px, it.py) + s, 0).r) : 31;
    int alpha = min(31, (mask * it.m2) >> 5);
    if (alpha <= 0) continue;
    ivec3 sc = ivec3(sp.rgb), dc = ivec3(d.rgb);
    ivec3 v = clamp((32 * dc + alpha * (sc - dc)) >> 5, ivec3(0), ivec3(255));
    d = uvec4(uvec3(v), 255u);
  }
  imageStore(uOut, p, d);
})";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "gl_presenter shader: %s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint link(std::initializer_list<GLuint> shaders) {
  GLuint p = glCreateProgram();
  for (GLuint s : shaders) { if (!s) { glDeleteProgram(p); return 0; } glAttachShader(p, s); }
  glLinkProgram(p);
  for (GLuint s : shaders) glDeleteShader(s);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { glDeleteProgram(p); return 0; }
  return p;
}

void setupTex(GLuint t, int w, int h, GLenum internal, GLenum fmt) {
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

double msSince(std::chrono::steady_clock::time_point a) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

// Shelf packing with 1 px of padding; false when it does not fit.
bool shelfPlace(int& px, int& py, int& shelf, int size, int w, int h, int& ox, int& oy) {
  if (w > size || h > size) return false;
  if (px + w > size) { px = 0; py += shelf; shelf = 0; }
  if (py + h > size) return false;
  ox = px; oy = py;
  px += w + 1;
  shelf = std::max(shelf, h + 1);
  return true;
}

}  // namespace

GlPresenter::~GlPresenter() {
  if (ctx_) {
    SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
    GLuint texs[] = {overlayTex_, baseTex_, cityTex_, atlasTex_, planeTex_};
    glDeleteTextures(5, texs);
    GLuint bufs[] = {ssboItems_, ssboTileStart_, ssboTileIdx_};
    glDeleteBuffers(3, bufs);
    if (finalProg_) glDeleteProgram(finalProg_);
    if (composeProg_) glDeleteProgram(composeProg_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    SDL_GL_DeleteContext(static_cast<SDL_GLContext>(ctx_));
  }
}

bool GlPresenter::init(SDL_Window* win) {
  win_ = win;
  SDL_GLContext ctx = nullptr;
  struct V { int major, minor; };
  for (V v : {V{4, 3}, V{3, 3}}) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    ctx = SDL_GL_CreateContext(win);
    if (ctx) break;
  }
  if (!ctx) return false;
  SDL_GL_MakeCurrent(win, ctx);
  glewExperimental = GL_TRUE;
  glewInit();
  if (!GLEW_VERSION_3_3) { SDL_GL_DeleteContext(ctx); return false; }
  finalProg_ = link({compile(GL_VERTEX_SHADER, kVs), compile(GL_FRAGMENT_SHADER, kFs)});
  if (!finalProg_) { SDL_GL_DeleteContext(ctx); return false; }
  if (GLEW_VERSION_4_3) composeProg_ = link({compile(GL_COMPUTE_SHADER, kCompose)});
  compute_ = composeProg_ != 0;
  glGenVertexArrays(1, &vao_);
  glGenTextures(1, &overlayTex_);
  if (compute_) {
    glGenTextures(1, &baseTex_); glGenTextures(1, &cityTex_); glGenTextures(1, &atlasTex_); glGenTextures(1, &planeTex_);
    glGenBuffers(1, &ssboItems_); glGenBuffers(1, &ssboTileStart_); glGenBuffers(1, &ssboTileIdx_);
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    atlasSize_ = std::min(4096, int(maxTex));
    setupTex(atlasTex_, atlasSize_, atlasSize_, GL_RGBA8UI, GL_RGBA_INTEGER);
    setupTex(planeTex_, atlasSize_, atlasSize_, GL_R8UI, GL_RED_INTEGER);
  }
  glUseProgram(finalProg_);
  glUniform1i(glGetUniformLocation(finalProg_, "uOverlay"), 0);
  glUniform1i(glGetUniformLocation(finalProg_, "uCity"), 1);
  ctx_ = ctx;
  ok_ = true;
  return true;
}

void GlPresenter::setVsync(bool on) {
  if (!ok_) return;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  SDL_GL_SetSwapInterval(on ? 1 : 0);
}

void GlPresenter::ensureTextures(int w, int h) {
  if (tw_ == w && th_ == h) return;
  setupTex(overlayTex_, w, h, GL_RGBA8UI, GL_RGBA_INTEGER);
  if (compute_) {
    setupTex(baseTex_, w, h, GL_RGBA8UI, GL_RGBA_INTEGER);
    setupTex(cityTex_, w, h, GL_RGBA8UI, GL_RGBA_INTEGER);
  }
  tw_ = w; th_ = h;
  baseValid_ = false;
  stage_.assign(size_t(w) * h, 0);
}

bool GlPresenter::beginFrame(int w, int h, FrameBuffer& out) {
  out = {};
  if (!ok_ || w <= 0 || h <= 0) return false;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  ensureTextures(w, h);
  out.pixels = stage_.data(); out.pitchBytes = w * 4; out.w = w; out.h = h;
  out.citySink = cityEnabled_ ? this : nullptr;
  return true;
}

void GlPresenter::endFrame() {
  if (!ok_ || tw_ <= 0) return;
  glBindTexture(GL_TEXTURE_2D, overlayTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw_, th_, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, stage_.data());
}

void GlPresenter::draw(unsigned fbo, int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  glUseProgram(finalProg_);
  glUniform1i(glGetUniformLocation(finalProg_, "uCityVisible"), cityVisible_ && compute_ ? 1 : 0);
  glUniform2i(glGetUniformLocation(finalProg_, "uSize"), tw_, th_);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, overlayTex_);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, compute_ ? cityTex_ : overlayTex_);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
}

void GlPresenter::present() {
  if (!ok_ || tw_ <= 0) return;
  int dw = 0, dh = 0;
  SDL_GL_GetDrawableSize(win_, &dw, &dh);
  draw(0, dw, dh);
  SDL_GL_SwapWindow(win_);
}

bool GlPresenter::readback(std::vector<uint32_t>& out) {
  if (!ok_ || tw_ <= 0) return false;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  GLuint fbo = 0, rt = 0;
  glGenFramebuffers(1, &fbo); glGenTextures(1, &rt);
  glBindTexture(GL_TEXTURE_2D, rt);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw_, th_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
  const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (complete) {
    draw(fbo, tw_, th_);
    std::vector<uint32_t> raw(size_t(tw_) * th_);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, tw_, th_, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    out.resize(raw.size());
    for (int y = 0; y < th_; y++)   // GL rows run bottom-up
      for (int x = 0; x < tw_; x++) out[size_t(y) * tw_ + x] = raw[size_t(th_ - 1 - y) * tw_ + x] | 0xFF000000u;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &rt);
  return complete;
}

// ---- GPU city ----------------------------------------------------------------------------------------------------

void GlPresenter::resetAtlas() {
  spriteSlots_.clear(); planeSlots_.clear();
  spritePack_ = Packer(); planePack_ = Packer();
}

void GlPresenter::invalidateSprites() { resetAtlas(); }

bool GlPresenter::packSprite(const SpriteImage* s, Slot& out) {
  auto it = spriteSlots_.find(s);
  if (it != spriteSlots_.end()) { out = it->second; return true; }
  const Image& im = s->img;
  Slot sl;
  if (!shelfPlace(spritePack_.x, spritePack_.y, spritePack_.shelf, atlasSize_, im.w, im.h, sl.x, sl.y)) return false;
  glBindTexture(GL_TEXTURE_2D, atlasTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, sl.x, sl.y, im.w, im.h, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, im.px.data());
  spriteSlots_[s] = sl;
  out = sl;
  return true;
}

bool GlPresenter::packPlane(const Plane* p, Slot& out) {
  auto it = planeSlots_.find(p);
  if (it != planeSlots_.end()) { out = it->second; return true; }
  Slot sl;
  if (!shelfPlace(planePack_.x, planePack_.y, planePack_.shelf, atlasSize_, p->w, p->h, sl.x, sl.y)) return false;
  glBindTexture(GL_TEXTURE_2D, planeTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, sl.x, sl.y, p->w, p->h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, p->v.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  planeSlots_[p] = sl;
  out = sl;
  return true;
}

// atlasPos: 4 ints per item (sprite x, y, plane x, y; plane x < 0 = none).
bool GlPresenter::ensureAtlas(const std::vector<GpuSpriteItem>& items, std::vector<int>& atlasPos) {
  for (int attempt = 0; attempt < 2; attempt++) {
    atlasPos.assign(items.size() * 4, -1);
    bool ok = true;
    for (size_t i = 0; i < items.size() && ok; i++) {
      Slot s, p;
      if (!packSprite(items[i].spr, s)) { ok = false; break; }
      atlasPos[i * 4] = s.x; atlasPos[i * 4 + 1] = s.y;
      if (items[i].plane) {
        if (!packPlane(items[i].plane, p)) { ok = false; break; }
        atlasPos[i * 4 + 2] = p.x; atlasPos[i * 4 + 3] = p.y;
      }
    }
    if (ok) return true;
    resetAtlas();   // full: start over with just this frame's sprites
  }
  return false;
}

bool GlPresenter::submitCity(const GpuCityFrame& f) {
  if (!ok_ || !compute_ || !cityEnabled_ || !f.base || !f.items || f.w != tw_ || f.h != th_) return false;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  stats_ = Stats();
  const std::vector<GpuSpriteItem>& items = *f.items;
  const size_t n = items.size();

  auto t0 = std::chrono::steady_clock::now();
  std::vector<int> atlasPos;
  if (!ensureAtlas(items, atlasPos)) return false;
  stats_.atlasMs = msSince(t0);

  if (!baseValid_ || baseGen_ != f.baseGen) {
    glBindTexture(GL_TEXTURE_2D, baseTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw_, th_, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, f.base);
    baseGen_ = f.baseGen; baseValid_ = true;
  }

  // Tile lists: counting sort keeps draw order inside every tile. Rows of tiles are independent.
  t0 = std::chrono::steady_clock::now();
  const int tilesX = (tw_ + kTile - 1) / kTile, tilesY = (th_ + kTile - 1) / kTile;
  const size_t tiles = size_t(tilesX) * tilesY;
  std::vector<uint32_t> tileStart(tiles + 1, 0);
  auto range = [&](const GpuSpriteItem& g, int& tx0, int& tx1, int& ty0, int& ty1) {
    const int x1 = g.x0 + g.w - 1, y1 = g.y0 + g.h - 1;
    if (x1 < 0 || y1 < 0 || g.x0 >= tw_ || g.y0 >= th_) return false;
    tx0 = std::max(0, g.x0) / kTile; tx1 = std::min(tw_ - 1, x1) / kTile;
    ty0 = std::max(0, g.y0) / kTile; ty1 = std::min(th_ - 1, y1) / kTile;
    return true;
  };
  WorkerPool& pool = WorkerPool::shared();
  pool.run(tilesY, [&](int ty) {
    uint32_t* cnt = &tileStart[size_t(ty) * tilesX + 1];   // counts live at tile index + 1, ready for the prefix sum
    for (size_t i = 0; i < n; i++) {
      int tx0, tx1, ty0, ty1;
      if (!range(items[i], tx0, tx1, ty0, ty1) || ty < ty0 || ty > ty1) continue;
      for (int tx = tx0; tx <= tx1; tx++) cnt[tx]++;
    }
  });
  for (size_t t = 0; t < tiles; t++) tileStart[t + 1] += tileStart[t];
  std::vector<uint32_t> tileIdx(tileStart[tiles]);
  pool.run(tilesY, [&](int ty) {
    std::vector<uint32_t> cur; cur.resize(size_t(tilesX));
    for (int tx = 0; tx < tilesX; tx++) cur[size_t(tx)] = tileStart[size_t(ty) * tilesX + tx];
    for (size_t i = 0; i < n; i++) {
      int tx0, tx1, ty0, ty1;
      if (!range(items[i], tx0, tx1, ty0, ty1) || ty < ty0 || ty > ty1) continue;
      for (int tx = tx0; tx <= tx1; tx++) tileIdx[cur[size_t(tx)]++] = uint32_t(i);
    }
  });
  stats_.binMs = msSince(t0);
  stats_.items = int(n); stats_.tileEntries = int(tileIdx.size());
  for (const GpuSpriteItem& g : items) { stats_.blendItems += g.blendAll; stats_.planeItems += g.plane != nullptr; }

  t0 = std::chrono::steady_clock::now();
  std::vector<int> packed(std::max<size_t>(n, 1) * 12, 0);
  for (size_t i = 0; i < n; i++) {
    const GpuSpriteItem& g = items[i];
    int* o = &packed[i * 12];
    o[0] = g.x0; o[1] = g.y0; o[2] = g.w; o[3] = g.h; o[4] = g.sc; o[5] = g.m2;
    o[6] = atlasPos[i * 4]; o[7] = atlasPos[i * 4 + 1]; o[8] = atlasPos[i * 4 + 2]; o[9] = atlasPos[i * 4 + 3];
    o[10] = g.blendAll ? 1 : 0;
  }
  auto upload = [&](GLuint buf, size_t& cap, const void* data, size_t bytes) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    if (bytes > cap) { cap = bytes + bytes / 2; glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(cap), nullptr, GL_DYNAMIC_DRAW); }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(bytes), data);
  };
  upload(ssboItems_, itemsCap_, packed.data(), packed.size() * sizeof(int));
  upload(ssboTileStart_, tileStartCap_, tileStart.data(), tileStart.size() * sizeof(uint32_t));
  upload(ssboTileIdx_, tileIdxCap_, tileIdx.empty() ? static_cast<const void*>(packed.data()) : static_cast<const void*>(tileIdx.data()),
         std::max<size_t>(tileIdx.size(), 1) * sizeof(uint32_t));
  stats_.uploadMs = msSince(t0);

  glUseProgram(composeProg_);
  glUniform2i(glGetUniformLocation(composeProg_, "uSize"), tw_, th_);
  glUniform1i(glGetUniformLocation(composeProg_, "uTilesX"), tilesX);
  glUniform1i(glGetUniformLocation(composeProg_, "uBase"), 0);
  glUniform1i(glGetUniformLocation(composeProg_, "uAtlas"), 1);
  glUniform1i(glGetUniformLocation(composeProg_, "uPlane"), 2);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboItems_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboTileStart_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboTileIdx_);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, baseTex_);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, atlasTex_);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, planeTex_);
  glActiveTexture(GL_TEXTURE0);
  glBindImageTexture(3, cityTex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8UI);
  glDispatchCompute(GLuint(tilesX), GLuint(tilesY), 1);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  return true;
}

}  // namespace m3k

#endif  // M3K_HAS_GL_PRESENT
