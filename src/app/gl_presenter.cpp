#include "gl_presenter.h"

#if defined(M3K_HAS_GL_PRESENT) && M3K_HAS_GL_PRESENT

#include <SDL.h>
#include <GL/glew.h>

#include <cstdio>

namespace m3k {
namespace {
const char* kVs = R"(#version 330 core
out vec2 uv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
const char* kFs = R"(#version 330 core
in vec2 uv;
uniform sampler2D frame;
out vec4 color;
void main() { color = vec4(texture(frame, uv).rgb, 1.0); })";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "gl_presenter shader: %s\n", log); glDeleteShader(s); return 0; }
  return s;
}
}  // namespace

GlPresenter::~GlPresenter() {
  if (ctx_) {
    SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
    if (tex_) glDeleteTextures(1, &tex_);
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    SDL_GL_DeleteContext(static_cast<SDL_GLContext>(ctx_));
  }
}

bool GlPresenter::init(SDL_Window* win) {
  win_ = win;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) return false;
  SDL_GL_MakeCurrent(win, ctx);
  glewExperimental = GL_TRUE;
  glewInit();
  if (!GLEW_VERSION_3_3) { SDL_GL_DeleteContext(ctx); return false; }
  GLuint vs = compile(GL_VERTEX_SHADER, kVs), fs = compile(GL_FRAGMENT_SHADER, kFs);
  if (!vs || !fs) { SDL_GL_DeleteContext(ctx); return false; }
  prog_ = glCreateProgram();
  glAttachShader(prog_, vs); glAttachShader(prog_, fs);
  glLinkProgram(prog_);
  glDeleteShader(vs); glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
  if (!ok) { glDeleteProgram(prog_); prog_ = 0; SDL_GL_DeleteContext(ctx); return false; }
  glGenVertexArrays(1, &vao_);
  glGenTextures(1, &tex_);
  glUseProgram(prog_);
  glUniform1i(glGetUniformLocation(prog_, "frame"), 0);
  ctx_ = ctx;
  ok_ = true;
  return true;
}

void GlPresenter::setVsync(bool on) {
  if (!ok_) return;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  SDL_GL_SetSwapInterval(on ? 1 : 0);
}

void GlPresenter::ensureTexture(int w, int h) {
  if (tw_ == w && th_ == h) return;
  glBindTexture(GL_TEXTURE_2D, tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  tw_ = w; th_ = h;
  stage_.assign(size_t(w) * h, 0);
}

bool GlPresenter::beginFrame(int w, int h, FrameBuffer& out) {
  out = {};
  if (!ok_ || w <= 0 || h <= 0) return false;
  SDL_GL_MakeCurrent(win_, static_cast<SDL_GLContext>(ctx_));
  ensureTexture(w, h);
  out.pixels = stage_.data(); out.pitchBytes = w * 4; out.w = w; out.h = h;
  return true;
}

void GlPresenter::endFrame() {
  if (!ok_ || tw_ <= 0) return;
  glBindTexture(GL_TEXTURE_2D, tex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw_, th_, GL_RGBA, GL_UNSIGNED_BYTE, stage_.data());
}

void GlPresenter::draw(unsigned fbo, int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  glUseProgram(prog_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
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

}  // namespace m3k

#endif  // M3K_HAS_GL_PRESENT
