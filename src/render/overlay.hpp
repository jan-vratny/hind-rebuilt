#pragma once
#include <glm/glm.hpp>

#include "gamedata/picture.hpp"
#include "render/gl.hpp"

namespace render {

// A seat's cockpit picture from the original game (COCKPITS/PILOT.LBM, GUNNER.LBM) with
// transparent windows, drawn over the 3D view. The picture is a panorama three views wide: its
// middle third is the view ahead, and it slides sideways as the crew member looks left and right,
// as in the original. Its original rows fill the screen height; rows painted over them (ours,
// data/cockpit) come into view as the crew member looks up, the picture sliding down. Beyond its
// ends the edge columns repeat, under its bottom the last row; over its top nothing.
class CockpitOverlay {
public:
  static constexpr float kViewWidth = 640.0f;  // picture pixels of one view

  CockpitOverlay() = default;
  CockpitOverlay(const CockpitOverlay&) = delete;
  CockpitOverlay& operator=(const CockpitOverlay&) = delete;
  ~CockpitOverlay();
  // display: the text display in the view ahead, picture pixels x0, y0, x1, y1 from the top left
  // of the original's picture; top: rows of the picture over the original's.
  bool load(const gamedata::Picture& picture, glm::vec4 display, int top = 0);
  bool loaded() const { return tex_ != 0; }
  // pan: picture pixels the view is turned right of straight ahead (negative: to the left);
  // lift: picture pixels it is tilted up from the seat's default (the picture slides down).
  // tint: the light in the cockpit; lit: the instruments' greys (dial faces' marks) with their
  // lamps on (L), below the original picture's row litFrom.
  void draw(int w, int h, float pan, float lift = 0.0f, glm::vec3 tint = glm::vec3(1.0f), glm::vec3 lit = glm::vec3(1.0f),
            float litFrom = 0.0f);
  // How far the view can turn either way before the picture runs out.
  float maxPan() const { return (float(width_) - kViewWidth) * 0.5f; }

  // Screen pixels per picture pixel, and the screen x of the left edge of the view ahead.
  float scale(int h) const { return float(h) / float(height_ - top_); }
  float frontLeft(int w, int h, float pan) const { return (float(w) - kViewWidth * scale(h)) * 0.5f - pan * scale(h); }
  // The text display on screen: x0, y0, x1, y1 in pixels, y up.
  glm::vec4 displayRect(int w, int h, float pan, float lift = 0.0f) const;

private:
  Program prog_;
  GLuint tex_ = 0, vao_ = 0;
  int width_ = 0, height_ = 0, top_ = 0;
  glm::vec4 display_{0.0f};
};

}  // namespace render
