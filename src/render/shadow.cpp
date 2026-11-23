#include "render/shadow.hpp"

#include <cmath>

namespace render {

namespace {
constexpr float kLift = 0.15f;  // m
}

bool shadowPlane(const world::Terrain& terrain, glm::vec3 pos, glm::vec3 toLight, glm::vec3& at, glm::vec3& normal) {
  const glm::vec3 l = glm::normalize(toLight);
  if (l.y < 0.1f) return false;
  // Down the ray from the thing away from the sun, in steps, then halved to the ground.
  constexpr float kStep = 20.0f, kFar = 2500.0f;
  auto above = [&](float t) {
    const glm::vec3 p = pos - l * t;
    return p.y - terrain.groundHeight(p.x, p.z);
  };
  float lo = 0.0f, hi = -1.0f;
  if (above(0.0f) <= 0.0f) hi = 0.0f;
  for (float t = kStep; hi < 0.0f && t <= kFar; t += kStep) {
    if (above(t) <= 0.0f) hi = t;
    else lo = t;
  }
  if (hi < 0.0f) return false;
  for (int i = 0; i < 12 && hi > 0.0f; ++i) {
    const float mid = 0.5f * (lo + hi);
    (above(mid) > 0.0f ? lo : hi) = mid;
  }
  at = pos - l * hi;
  at.y = terrain.groundHeight(at.x, at.z);
  // The ground's slope over a few metres (the size of the shadow).
  constexpr float d = 4.0f;
  const float hx = terrain.groundHeight(at.x + d, at.z) - terrain.groundHeight(at.x - d, at.z);
  const float hz = terrain.groundHeight(at.x, at.z + d) - terrain.groundHeight(at.x, at.z - d);
  normal = glm::normalize(glm::vec3(-hx, 2.0f * d, -hz));
  // A little above the ground: over the runways and aprons laid on it too.
  at += normal * kLift;
  return true;
}

glm::mat4 shadowMatrix(glm::vec3 at, glm::vec3 normal, glm::vec3 toLight) {
  // The plane n . x + w = 0 and the light's direction (a point at infinity): S = (P . L) I - L P^T.
  const glm::vec4 plane(normal, -glm::dot(normal, at));
  const glm::vec4 light(glm::normalize(toLight), 0.0f);
  const float dot = glm::dot(plane, light);
  glm::mat4 s(0.0f);
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row) s[col][row] = (row == col ? dot : 0.0f) - light[row] * plane[col];
  // w stays 1 (the shader takes the world position as it comes).
  return s / dot;
}

}  // namespace render
