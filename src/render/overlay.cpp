#include "render/overlay.hpp"

#include <vector>

#include "render/shaders.hpp"

namespace render {

CockpitOverlay::~CockpitOverlay() {
  if (tex_) glDeleteTextures(1, &tex_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool CockpitOverlay::load(const gamedata::Picture& picture, glm::vec4 display, int top) {
  if (picture.width <= 0 || picture.height <= top || top < 0) return false;
  width_ = picture.width;
  height_ = picture.height;
  top_ = top;
  display_ = display;
  if (!prog_.build("overlay", shaders::kSkyVS, shaders::kOverlayFS)) return false;
  glGenVertexArrays(1, &vao_);
  glGenTextures(1, &tex_);
  glBindTexture(GL_TEXTURE_2D, tex_);
  // Premultiplied alpha, so that filtering does not bleed the colour of the windows (green in the
  // pictures) into their edges.
  std::vector<uint8_t> px = picture.rgba;
  for (size_t i = 0; i + 3 < px.size(); i += 4)
    for (int k = 0; k < 3; ++k) px[i + k] = uint8_t(px[i + k] * px[i + 3] / 255);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

void CockpitOverlay::draw(int w, int h, float pan, float lift, glm::vec3 tint, glm::vec3 lit, float litFrom) {
  if (!tex_) return;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  prog_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glUniform1i(prog_.loc("uImage"), 0);
  glUniform2f(prog_.loc("uImageSize"), float(width_), float(height_));
  // Screen x of the picture's left edge: the view ahead is its middle third.
  prog_.set("uLeft", frontLeft(w, h, pan) - (float(width_) - kViewWidth) * 0.5f * scale(h));
  prog_.set("uScale", scale(h));
  prog_.set("uScreenHeight", float(h));
  prog_.set("uTop", float(top_) - lift);
  prog_.set("uTint", tint);
  prog_.set("uLit", glm::max(lit, tint));
  prog_.set("uLitFrom", litFrom + float(top_));
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

glm::vec4 CockpitOverlay::displayRect(int w, int h, float pan, float lift) const {
  float s = scale(h), l = frontLeft(w, h, pan);
  return {l + display_.x * s, h - (display_.w + lift) * s, l + display_.z * s, h - (display_.y + lift) * s};
}

}  // namespace render
