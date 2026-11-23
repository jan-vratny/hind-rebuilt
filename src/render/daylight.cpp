#include "render/daylight.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace render {

Palette skyAt(float hour, int weather, const SkyColours& sky) {
  Palette day;
  // The sun: round the sky from the east (+x) by the south (+z) to the west, a long summer day:
  // in the original it is still dusk at half past eight in the evening (SARATT02) and dawn at
  // six in the morning (SARATT04), the world about half lit either time.
  const float pi = glm::pi<float>();
  constexpr float kRise = 6.0f, kSet = 20.5f;
  const float dayLen = kSet - kRise, t = std::fmod(hour - kRise + 48.0f, 24.0f);
  const float arc = t < dayLen ? t / dayLen : 1.0f + (t - dayLen) / (24.0f - dayLen);  // 0..1 day, 1..2 night
  const float elev = std::asin(0.85f * std::sin(arc * pi));
  const float az = arc * pi;
  const glm::vec3 flat{std::cos(az), 0.0f, std::sin(az)};
  const glm::vec3 sun = glm::normalize(flat * std::cos(elev) + glm::vec3(0.0f, std::sin(elev), 0.0f));
  const float deg = glm::degrees(elev);

  // Dusk and dawn as the original shows them: a blue-grey sky, the ground evenly some 40 per
  // cent of the day's, no red, no stars yet.
  Palette dusk = day;
  dusk.horizon = {0.30f, 0.33f, 0.36f};
  dusk.zenith = {0.21f, 0.28f, 0.36f};
  dusk.groundHaze = {0.27f, 0.30f, 0.33f};
  dusk.light = glm::vec3(1.0f);
  dusk.ambient = 0.40f, dusk.diffuse = 0.06f;
  Palette night = day;
  night.horizon = {0.05f, 0.065f, 0.11f};
  night.zenith = {0.006f, 0.010f, 0.028f};
  night.groundHaze = {0.02f, 0.025f, 0.035f};
  // At 23:00 the original's ground is all but black, a dark grey (KAZAKS07).
  night.light = {0.66f, 0.72f, 0.88f};
  night.ambient = 0.10f, night.diffuse = 0.12f;
  night.stars = 1.0f;

  auto mix = [](const Palette& a, const Palette& b, float t) {
    Palette p;
    p.horizon = glm::mix(a.horizon, b.horizon, t);
    p.zenith = glm::mix(a.zenith, b.zenith, t);
    p.groundHaze = glm::mix(a.groundHaze, b.groundHaze, t);
    p.light = glm::mix(a.light, b.light, t);
    p.ambient = glm::mix(a.ambient, b.ambient, t);
    p.diffuse = glm::mix(a.diffuse, b.diffuse, t);
    p.stars = glm::mix(a.stars, b.stars, t);
    return p;
  };
  Palette p = deg > 2.0f ? mix(dusk, day, glm::smoothstep(2.0f, 14.0f, deg)) : mix(night, dusk, glm::smoothstep(-12.0f, -1.0f, deg));
  p.stars = 1.0f - glm::smoothstep(-12.0f, -6.0f, deg);
  p.moon = p.stars;
  p.lamps = 1.0f - glm::smoothstep(-6.0f, -1.0f, deg);
  // By day the sun lights the world, by night the moon (high in the opposite sky).
  p.sunDir = deg > -4.0f ? (sun.y > 0.05f ? sun : glm::normalize(glm::vec3(sun.x, 0.05f, sun.z)))
                         : glm::normalize(glm::vec3(-flat.x, 0.8f, -flat.z));

  // Weather: cloud flattens the light into an even grey, the original's overcast sky as dim as
  // the day is.
  if (weather >= 1) {
    const glm::vec3 lum{0.30f, 0.59f, 0.11f};
    const float clearLight = glm::dot(p.light, lum) * (p.ambient + p.diffuse) / 1.1f;
    const float dim = std::clamp(clearLight / 0.82f, 0.0f, 1.0f) * glm::mix(0.5f, 1.0f, glm::smoothstep(-12.0f, 2.0f, deg));
    p.zenith = sky.overcastZenith * dim;
    p.horizon = sky.overcastHorizon * dim;
    p.groundHaze = p.horizon * 0.9f;
    p.light = glm::vec3(1.0f);
    p.ambient = (0.45f + 0.65f * 0.35f) * dim;
    p.diffuse = 0.65f * 0.35f * dim;
    p.stars = p.moon = 0.0f;
  }
  p.shadow = weather >= 1 ? 0.0f : glm::smoothstep(1.0f, 8.0f, deg);
  p.fogDist = weather == 1 ? 12000.0f : weather == 2 ? 3500.0f : weather == 3 ? 1200.0f : weather == 4 ? 4000.0f : 1.0e9f;
  // The cockpit stays in full light through dawn and dusk (SARATT04 at six); in the dark only
  // the instruments' lamps.
  const glm::vec3 lamps = glm::max(night.light * (night.ambient + night.diffuse), glm::vec3(0.30f, 0.27f, 0.30f));
  p.cockpit = glm::mix(lamps, glm::vec3(1.0f), glm::smoothstep(-12.0f, -2.0f, deg));
  // The map's picture is lit like the land (at night the original's map is black but for the
  // water and the lines).
  p.mapLight = std::clamp(glm::dot(p.light, glm::vec3(0.30f, 0.59f, 0.11f)) * (p.ambient + p.diffuse) / 1.1f, 0.0f, 1.0f);
  return p;
}

glm::vec3 cockpitLight(const Palette& p) { return p.cockpit; }

}  // namespace render
