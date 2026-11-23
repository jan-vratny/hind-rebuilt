#include "render/screenpass.hpp"

#include "render/shaders.hpp"

namespace render {

ScreenPass::~ScreenPass() {
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  if (tex_) glDeleteTextures(1, &tex_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

void ScreenPass::apply(int w, int h, float time, float amount) {
  capture(w, h);
  if (!ok_ || w <= 0 || h <= 0) return;
  glDisable(GL_BLEND);
  draw(time, amount);
}

void ScreenPass::drawBack(float time, float amount) {
  if (!ok_ || w_ <= 0 || h_ <= 0) return;
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  draw(time, amount);
  glDisable(GL_BLEND);
}

void ScreenPass::capture(int w, int h) {
  if (!built_) {
    built_ = true;
    ok_ = prog_.build(name_, shaders::kSkyVS, fragment_);
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &tex_);
    glGenVertexArrays(1, &vao_);
  }
  if (!ok_ || w <= 0 || h <= 0) return;
  GLint target = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
  if (w != w_ || h != h_) {
    w_ = w, h_ = h;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
  }
  // A copy of the frame (resolved, when multisampled), drawn back through the ramp.
  glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(target));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, GLuint(target));
}

void ScreenPass::draw(float time, float amount) {
  glDisable(GL_DEPTH_TEST);
  prog_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glUniform1i(prog_.loc("uImage"), 0);
  glUniform1f(prog_.loc("uTime"), time);
  glUniform1f(prog_.loc("uAmount"), amount);
  glUniform2f(prog_.loc("uScreen"), float(w_), float(h_));
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
}

}  // namespace render
