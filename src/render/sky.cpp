#include "render/sky.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "render/shaders.hpp"

namespace render {

namespace {

struct Star {
  float ra, dec, mag, bv;
};
const Star kStars[] = {
#include "render/stars.inc"
};

}  // namespace

Sky::~Sky() {
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (starVao_) glDeleteVertexArrays(1, &starVao_);
  if (starVbo_) glDeleteBuffers(1, &starVbo_);
}

bool Sky::init() {
  glGenVertexArrays(1, &vao_);
  glGenVertexArrays(1, &starVao_);
  glGenBuffers(1, &starVbo_);
  glBindVertexArray(starVao_);
  glBindBuffer(GL_ARRAY_BUFFER, starVbo_);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof kStars), kStars, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Star), nullptr);
  glBindVertexArray(0);
  stars_ = int(std::size(kStars));
  return prog_.build("sky", shaders::kSkyVS, shaders::kSkyFS) && starProg_.build("stars", shaders::kStarVS, shaders::kStarFS);
}

float Sky::siderealAngle(float hour, int dayOfYear) {
  // The sun is due south at 13:15 on the game's clock (it rises at six and sets at half past
  // eight); at local noon on the March equinox (day 80) the sidereal time is 0 h, and it runs
  // ahead of the sun by four minutes a day.
  const float solar = hour - 1.25f;
  const float lst = solar - 12.0f + float(dayOfYear - 80) * 24.0f / 365.25f;
  return std::fmod(lst / 24.0f + 10.0f, 1.0f) * 2.0f * glm::pi<float>();
}

void Sky::draw(const glm::mat4& viewProj, const Palette& pal, float hour, float pixel) {
  glDisable(GL_DEPTH_TEST);
  prog_.use();
  prog_.set("uInvViewProj", glm::inverse(viewProj));
  prog_.set("uHorizon", pal.horizon);
  prog_.set("uZenith", pal.zenith);
  prog_.set("uGround", pal.groundHaze);
  prog_.set("uMoonDir", pal.sunDir);
  prog_.set("uMoon", pal.moon);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  if (pal.stars > 0.0f && stars_ > 0) {
    starProg_.use();
    starProg_.set("uViewProj", viewProj);
    starProg_.set("uLst", siderealAngle(hour, day_));
    starProg_.set("uLat", glm::radians(latitude_));
    starProg_.set("uPixel", std::max(pixel, 1.0f));
    starProg_.set("uBright", pal.stars);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(starVao_);
    glDrawArrays(GL_POINTS, 0, stars_);
    glDisable(GL_BLEND);
    glDisable(GL_PROGRAM_POINT_SIZE);
  }
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}

}  // namespace render
