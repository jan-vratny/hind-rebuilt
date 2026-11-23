#pragma once
#include <glm/glm.hpp>

#include "render/daylight.hpp"
#include "render/gl.hpp"

namespace render {

// The sky: its colours from the zenith to the horizon, the moon, and at night the real stars of
// the Yale Bright Star Catalogue (down to magnitude 5.5) where they stand over the war zone at
// that hour: Polaris as high as the latitude over the north, the sky turning through the night.
class Sky {
public:
  ~Sky();
  bool init();
  // Where and when: the latitude (degrees north) and the day of the year (1 January = 1).
  void setPlace(float latitudeDeg, int dayOfYear) { latitude_ = latitudeDeg, day_ = dayOfYear; }
  // `hour`: the local time (the clock of the mission); `pixel`: how big a pixel is for the
  // stars (window height / 1080, at least 1).
  void draw(const glm::mat4& viewProj, const Palette& pal, float hour = 0.0f, float pixel = 1.0f);
  // The local sidereal time (radians) at that hour: which right ascension is on the meridian.
  static float siderealAngle(float hour, int dayOfYear);

private:
  Program prog_, starProg_;
  GLuint vao_ = 0, starVao_ = 0, starVbo_ = 0;
  int stars_ = 0;
  float latitude_ = 50.0f;
  int day_ = 182;
};

}  // namespace render
