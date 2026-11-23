#include "sim/debris.hpp"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {
constexpr float kGravity = 9.81f;
constexpr float kBounce = 0.35f;    // of the speed into the ground kept going up
constexpr float kScrape = 0.7f;     // of the speed along the ground kept at a bounce
constexpr float kFriction = 0.5f;   // sliding on the ground
}  // namespace

float Debris::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 23) - 1.0f;
}

void Debris::start(glm::vec3 at, glm::quat orient, glm::vec3 vel, const std::vector<glm::vec3>& centres,
                   const std::vector<float>& radii) {
  pieces_.clear();
  for (size_t i = 0; i < centres.size(); ++i) {
    DebrisPiece p;
    p.pos = at + orient * centres[i];
    p.orient = orient;
    p.radius = i < radii.size() ? radii[i] : 0.5f;
    // Away from the middle of the hull and up, the smaller pieces faster.
    glm::vec3 out = orient * centres[i];
    out.y = 0.0f;
    out = glm::length(out) > 0.1f ? glm::normalize(out) : glm::vec3(random(), 0.0f, random());
    const float kick = (4.0f + 3.0f * random()) * std::clamp(1.5f / p.radius, 0.6f, 1.8f);
    p.vel = vel * (0.55f + 0.15f * random()) + out * kick + glm::vec3(random(), 0.0f, random()) * 2.0f +
            glm::vec3(0.0f, 5.0f + 3.0f * random(), 0.0f);
    p.omega = glm::vec3(random(), random(), random()) * (2.0f + 2.5f / p.radius);
    pieces_.push_back(p);
  }
}

void Debris::step(const world::Terrain& terrain, float dt) {
  for (DebrisPiece& p : pieces_) {
    if (p.resting) continue;
    p.vel.y -= kGravity * dt;
    p.vel -= p.vel * std::min(1.0f, 0.02f * glm::length(p.vel) * dt);
    p.pos += p.vel * dt;
    const float w = glm::length(p.omega);
    if (w > 1e-4f) p.orient = glm::normalize(glm::angleAxis(w * dt, p.omega / w) * p.orient);
    const float ground = terrain.groundHeight(p.pos.x, p.pos.z) + p.radius * 0.5f;
    if (p.pos.y < ground) {
      p.pos.y = ground;
      if (p.vel.y < -2.0f) {
        // A bounce: it loses much of its speed into the ground and some along it.
        p.vel.y = -p.vel.y * kBounce;
        p.vel.x *= kScrape;
        p.vel.z *= kScrape;
        p.omega *= 0.6f;
      } else {
        // Sliding and tumbling along: friction.
        p.vel.y = 0.0f;
        glm::vec2 h{p.vel.x, p.vel.z};
        const float speed = glm::length(h), slow = kFriction * kGravity * dt;
        h = speed > slow ? h * (1.0f - slow / speed) : glm::vec2(0.0f);
        p.vel.x = h.x, p.vel.z = h.y;
        p.omega *= std::max(0.0f, 1.0f - 2.0f * dt);
      }
      if (glm::length(p.vel) < 0.5f) {
        p.resting = true;
        p.vel = p.omega = glm::vec3(0.0f);
      }
    }
  }
}

}  // namespace sim
