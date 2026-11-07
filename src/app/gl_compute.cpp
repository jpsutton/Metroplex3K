// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gl_compute.h"

#if !defined(M3K_HAS_GL_COMPUTE)
#define M3K_HAS_GL_COMPUTE 0
#endif

#if M3K_HAS_GL_COMPUTE
#include <SDL.h>
#include <GL/glew.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/workpool.h"

namespace m3k {
#if M3K_HAS_GL_COMPUTE
namespace {

static_assert(sizeof(GpuTerrainTri) == 96, "GpuTerrainTri must match GLSL std430 Tri layout");

// Host-built span: integer x-bounds + 16.16 colour steps (bit-exact with software gouraud/shore).
struct GpuTerrainSpan {
  int32_t y, xa, xe, ni, mode;
  int32_t L0, L1, L2, d0, d1, d2;
  int32_t key0, key1, key2, rep0, rep1, rep2;
};
static_assert(sizeof(GpuTerrainSpan) == 68, "GpuTerrainSpan std430 size");

const char* kTerrainComp = R"GLSL(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;

struct Span {
  int y, xa, xe, ni, mode;
  int L0, L1, L2, d0, d1, d2;
  int key0, key1, key2, rep0, rep1, rep2;
};

layout(std430, binding = 0) readonly buffer SpanBuf { Span spans[]; };
layout(std430, binding = 1) readonly buffer NoiseL { int noiseL[]; };
layout(std430, binding = 2) readonly buffer NoiseE { int noiseE[]; };
layout(std430, binding = 3) buffer Fb { uint fb[]; };
layout(std430, binding = 4) readonly buffer RowBuf { int rowStart[]; };

uniform int uW;
uniform int uH;

int clamp255(int v) { return clamp(v, 0, 255); }
uint packRGB(int r, int g, int b) {
  return 0xFF000000u | (uint(b & 255) << 16) | (uint(g & 255) << 8) | uint(r & 255);
}

// Every span mode overwrites its pixel fully. Spans are grouped per row in emission order, so the
// last span covering a pixel wins (CPU parity). Scan backward, stop at first hit: no races.
void main() {
  int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y);
  if (x >= uW || y >= uH) return;
  for (int k = rowStart[y + 1] - 1; k >= rowStart[y]; k--) {
    Span s = spans[k];
    if (x < s.xa || x >= s.xe) continue;
    int i = x - s.xa;
    int r = (s.L0 + s.d0 * i) / 65536;
    int g = (s.L1 + s.d1 * i) / 65536;
    int b = (s.L2 + s.d2 * i) / 65536;
    if (s.mode == 3) {
      int P0 = r, P1 = g, P2 = b;
      int d2 = (P0 - s.key0) * (P0 - s.key0) + (P1 - s.key1) * (P1 - s.key1) + (P2 - s.key2) * (P2 - s.key2);
      int o0, o1, o2;
      if (d2 <= 256) {
        int nz = noiseE[(s.ni + 3 * i) & 1023];
        o0 = s.rep0 + nz; o1 = s.rep1 + nz; o2 = s.rep2 + nz;
      } else {
        int v0 = d2 < 1024 ? (s.rep0 * (1024 - d2) + P0 * (d2 - 256)) / 768 : P0;
        int v1 = d2 < 1024 ? (s.rep1 * (1024 - d2) + P1 * (d2 - 256)) / 768 : P1;
        int v2 = d2 < 1024 ? (s.rep2 * (1024 - d2) + P2 * (d2 - 256)) / 768 : P2;
        o0 = v0 + noiseL[(s.ni + 3 * i) & 1023];
        o1 = v1 + noiseL[(s.ni + 3 * i + 1) & 1023];
        o2 = v2 + noiseL[(s.ni + 3 * i + 2) & 1023];
      }
      fb[y * uW + x] = packRGB(o0, o1, o2);
    } else {
      if (s.mode == 0) {
        r += noiseL[(s.ni + 3 * i) & 1023];
        g += noiseL[(s.ni + 3 * i + 1) & 1023];
        b += noiseL[(s.ni + 3 * i + 2) & 1023];
      } else if (s.mode == 1) {
        int nz = noiseE[(s.ni + i) & 1023];
        r += nz; g += nz; b += nz;
      } else {
        r = clamp255(r); g = clamp255(g); b = clamp255(b);
      }
      fb[y * uW + x] = packRGB(r, g, b);
    }
    return;
  }
}
)GLSL";

void expandSpans(const GpuTerrainTri* tris, int triBegin, int triEnd, int fbH, std::vector<GpuTerrainSpan>& out) {
  out.clear();
  for (int ti = triBegin; ti < triEnd; ti++) {
    const GpuTerrainTri& t = tris[ti];
    struct P { float x, y; float a[3]; };
    P v[3] = {
      {t.p0[0], t.p0[1], {t.p0[2], t.p0[3], t.p0[4]}},
      {t.p1[0], t.p1[1], {t.p1[2], t.p1[3], t.p1[4]}},
      {t.p2[0], t.p2[1], {t.p2[2], t.p2[3], t.p2[4]}},
    };
    std::sort(v, v + 3, [](const P& a, const P& b) {
      return a.y < b.y || (a.y == b.y && a.x < b.x);
    });
    auto cross = [](const P& p, const P& q, float y, float* o) {
      float tt = (q.y == p.y) ? 0.f : (y - p.y) / (q.y - p.y);
      o[0] = p.x + (q.x - p.x) * tt;
      for (int k = 0; k < 3; k++) o[1 + k] = p.a[k] + (q.a[k] - p.a[k]) * tt;
    };
    int ys = std::max(0, int(std::ceil(v[0].y)));
    int ye = std::min(fbH, int(std::ceil(v[2].y)));
    for (int y = ys; y < ye; y++) {
      float fy = float(y), L[4], R[4];
      cross(v[0], v[2], fy, L);
      if (fy < v[1].y) cross(v[0], v[1], fy, R); else cross(v[1], v[2], fy, R);
      if (R[0] < L[0]) for (int k = 0; k < 4; k++) std::swap(L[k], R[k]);
      int xa = int(std::ceil(L[0])), xe = int(std::ceil(R[0]));
      if (xe <= xa) continue;
      int n = xe - xa;
      float inv = 1.f / float(n + 1);
      GpuTerrainSpan s{};
      s.y = y; s.xa = xa; s.xe = xe;
      s.ni = ((t.seed + 143 * (y - t.ytop)) % 1024 + 1024) % 1024;
      s.mode = t.mode;
      s.L0 = int32_t(L[1] * 65536.f); s.L1 = int32_t(L[2] * 65536.f); s.L2 = int32_t(L[3] * 65536.f);
      s.d0 = int32_t((R[1] - L[1]) * inv * 65536.f);
      s.d1 = int32_t((R[2] - L[2]) * inv * 65536.f);
      s.d2 = int32_t((R[3] - L[3]) * inv * 65536.f);
      s.key0 = t.key[0]; s.key1 = t.key[1]; s.key2 = t.key[2];
      s.rep0 = t.rep[0]; s.rep1 = t.rep[1]; s.rep2 = t.rep[2];
      out.push_back(s);
    }
  }
}

bool compileProgram(GLuint& progOut, const char* src, std::string& err) {
  GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    err = log;
    glDeleteShader(sh);
    return false;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glDeleteShader(sh);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(prog, sizeof log, nullptr, log);
    err = log;
    glDeleteProgram(prog);
    return false;
  }
  progOut = prog;
  return true;
}

}  // namespace

GlComputeTerrain::~GlComputeTerrain() {
  if (glctx_) {
    SDL_GL_MakeCurrent(static_cast<SDL_Window*>(window_), static_cast<SDL_GLContext>(glctx_));
    if (ssboTris_) glDeleteBuffers(1, &ssboTris_);
    if (ssboNoiseL_) glDeleteBuffers(1, &ssboNoiseL_);
    if (ssboNoiseE_) glDeleteBuffers(1, &ssboNoiseE_);
    if (ssboFb_) glDeleteBuffers(1, &ssboFb_);
    if (ssboRows_) glDeleteBuffers(1, &ssboRows_);
    if (prog_) glDeleteProgram(prog_);
    SDL_GL_DeleteContext(static_cast<SDL_GLContext>(glctx_));
    glctx_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
    window_ = nullptr;
  }
  ok_ = false;
}

bool GlComputeTerrain::init() {
  if (initTried_) return ok_;
  initTried_ = true;
  if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window* win = SDL_CreateWindow("m3k-gl-compute", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!win) return false;
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) { SDL_DestroyWindow(win); return false; }
  SDL_GL_MakeCurrent(win, ctx);
  glewExperimental = GL_TRUE;
  glewInit();  // ignore benign errors from glewExperimental on some drivers
  if (!GLEW_VERSION_4_3) {
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    return false;
  }
  std::string err;
  GLuint prog = 0;
  if (!compileProgram(prog, kTerrainComp, err)) {
    std::fprintf(stderr, "metroplex3k: terrain compute shader: %s\n", err.c_str());
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    return false;
  }
  window_ = win;
  glctx_ = ctx;
  prog_ = prog;
  glGenBuffers(1, &ssboTris_);
  glGenBuffers(1, &ssboNoiseL_);
  glGenBuffers(1, &ssboNoiseE_);
  glGenBuffers(1, &ssboFb_);
  glGenBuffers(1, &ssboRows_);
  ok_ = true;
  return true;
}

bool GlComputeTerrain::fill(uint32_t* fb, int w, int h, int /*zoom*/,
                            const int8_t* noiseLand, const int8_t* noiseEdge,
                            const GpuTerrainTri* tris, int triCount) {
  if (!ok_ || !fb || w <= 0 || h <= 0) return false;
  if (triCount < 0) return false;
  SDL_GL_MakeCurrent(static_cast<SDL_Window*>(window_), static_cast<SDL_GLContext>(glctx_));

  // Span setup runs on the worker pool: chunks of triangles in order, merged in order (keeps emission order).
  const int chunks = std::max(1, std::min(triCount / 256, WorkerPool::shared().threads() * 4));
  std::vector<std::vector<GpuTerrainSpan>> parts;
  parts.resize(size_t(chunks));
  WorkerPool::shared().run(chunks, [&](int c) {
    expandSpans(tris, int(int64_t(triCount) * c / chunks), int(int64_t(triCount) * (c + 1) / chunks), h, parts[size_t(c)]);
  });
  // Counting sort by row (stable: keeps emission order within a row).
  std::vector<int> rowStart(size_t(h) + 2, 0);
  size_t total = 0;
  for (const auto& part : parts) {
    total += part.size();
    for (const auto& sp : part) rowStart[size_t(sp.y) + 1]++;
  }
  for (int y = 0; y <= h; y++) rowStart[size_t(y) + 1] += rowStart[size_t(y)];
  std::vector<GpuTerrainSpan> spans(total);
  {
    std::vector<int> cur(rowStart.begin(), rowStart.begin() + h);
    for (const auto& part : parts)
      for (const auto& sp : part) spans[size_t(cur[size_t(sp.y)]++)] = sp;
  }
  const int spanCount = int(total);

  const int pix = w * h;
  if (pix > fbCap_) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboFb_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size_t(pix) * sizeof(uint32_t), nullptr, GL_DYNAMIC_COPY);
    fbCap_ = pix;
  }
  if (spanCount > triCap_) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboTris_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size_t(std::max(spanCount, 1)) * sizeof(GpuTerrainSpan), nullptr, GL_DYNAMIC_DRAW);
    triCap_ = std::max(spanCount, 1);
  }

  int nL[1024], nE[1024];
  for (int i = 0; i < 1024; i++) { nL[i] = noiseLand[i]; nE[i] = noiseEdge[i]; }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboNoiseL_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof nL, nL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboNoiseE_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof nE, nE, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRows_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, size_t(h + 1) * sizeof(int), rowStart.data(), GL_DYNAMIC_DRAW);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboFb_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size_t(pix) * sizeof(uint32_t), fb);
  if (spanCount > 0) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboTris_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size_t(spanCount) * sizeof(GpuTerrainSpan), spans.data());
  }

  glUseProgram(prog_);
  glUniform1i(glGetUniformLocation(prog_, "uW"), w);
  glUniform1i(glGetUniformLocation(prog_, "uH"), h);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboTris_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboNoiseL_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboNoiseE_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboFb_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboRows_);

  glDispatchCompute(GLuint((w + 15) / 16), GLuint((h + 15) / 16), 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboFb_);
  void* ptr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size_t(pix) * sizeof(uint32_t), GL_MAP_READ_BIT);
  if (!ptr) return false;
  std::memcpy(fb, ptr, size_t(pix) * sizeof(uint32_t));
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  return true;
}

#else  // !M3K_HAS_GL_COMPUTE

GlComputeTerrain::~GlComputeTerrain() = default;
bool GlComputeTerrain::init() {
  initTried_ = true;
  ok_ = false;
  return false;
}
bool GlComputeTerrain::fill(uint32_t*, int, int, int, const int8_t*, const int8_t*,
                            const GpuTerrainTri*, int) {
  return false;
}

#endif  // M3K_HAS_GL_COMPUTE

}  // namespace m3k
