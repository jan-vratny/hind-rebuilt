#pragma once
#include "render/gl.hpp"

namespace render {

// A pass over the whole screen, the view, cockpit and HUD alike: what has been drawn is copied
// and drawn back through a fragment shader (`uImage`; `uTime` seconds and `uAmount` 0..1 for the
// shader to use as it likes).
class ScreenPass {
public:
  ScreenPass(const char* name, const char* fragment) : name_(name), fragment_(fragment) {}
  ScreenPass(const ScreenPass&) = delete;
  ScreenPass& operator=(const ScreenPass&) = delete;
  ~ScreenPass();
  // Turns what has been drawn into the frame buffer bound for drawing (w x h).
  void apply(int w, int h, float time = 0.0f, float amount = 0.0f);
  // In two steps: a copy of what has been drawn now, drawn back later over what came since,
  // through the shader (blended by its alpha).
  void capture(int w, int h);
  void drawBack(float time = 0.0f, float amount = 0.0f);

private:
  void draw(float time, float amount);
  const char *name_, *fragment_;
  Program prog_;
  GLuint fbo_ = 0, tex_ = 0, vao_ = 0;
  int w_ = 0, h_ = 0;
  bool built_ = false, ok_ = false;
};

}  // namespace render
