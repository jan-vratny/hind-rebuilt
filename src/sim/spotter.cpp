#include "sim/spotter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sim {

namespace {

bool isSpotter(const Vehicle& v) {
  return v.side == Side::Ours && !v.destroyed && !v.aboard && vehicleType(v.kind).original.find("Mi_17") != std::string::npos;
}

bool isDrone(const Vehicle& v) {
  return v.side == Side::Enemy && !v.destroyed && vehicleType(v.kind).cls == UnitClass::Drone && v.moving() && v.delay <= 0.0f;
}

}  // namespace

bool Spotter::sees(glm::vec3 from, glm::vec3 to) const {
  const glm::vec3 d = to - from;
  const int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 60.0f));
  for (int i = 1; i < steps; ++i) {
    const glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain_.groundHeight(p.x, p.z) + 1.0f) return false;
  }
  return true;
}

bool Spotter::active(const Targets& targets) const {
  for (const Vehicle& v : targets.list)
    if (isSpotter(v)) return true;
  return false;
}

std::string Spotter::call(const Helicopter& heli, const Vehicle& drone) {
  const glm::vec3 d = drone.pos - heli.pos;
  float b = glm::degrees(std::atan2(d.x, -d.z)) - heli.headingDeg();
  b = std::fmod(b + 720.0f + 15.0f, 360.0f);
  const int hour = int(b / 30.0f) % 12;
  const float km = glm::length(glm::vec2(d.x, d.z)) / 1000.0f;
  const char* level = d.y < -60.0f ? "LOW" : d.y > 60.0f ? "HIGH" : "LEVEL";
  char buf[80];
  // The jet one is told apart: it is past in half the time; the Banderol faster still.
  std::snprintf(buf, sizeof buf, "%s %d O'CLOCK %.1f KM %s",
                isCruiseMissile(drone.kind) ? "BANDEROL" : isJetDrone(drone.kind) ? "JET SHAHED" : "SHAHED", hour == 0 ? 12 : hour, km,
                level);
  return buf;
}

std::string Spotter::line(const Helicopter& heli, const Targets& targets) const {
  if (designated_ >= 0 && designated_ < int(targets.list.size()) && isDrone(targets.list[size_t(designated_)]))
    return "HIP: " + call(heli, targets.list[size_t(designated_)]);
  return clear_ > 0.0f ? "HIP: NO MORE CONTACTS" : "";
}

void Spotter::step(const Helicopter& heli, const Targets& targets, float dt) {
  look_ -= dt;
  clear_ = std::max(0.0f, clear_ - dt);
  bool lost = false;
  if (designated_ >= int(targets.list.size()) || (designated_ >= 0 && !isDrone(targets.list[size_t(designated_)]))) {
    designated_ = -1;
    look_ = 0.0f;  // the next one called at once (after a kill the player wants to know where to)
    lost = true;
  }
  if (look_ > 0.0f) return;
  look_ = 1.0f;
  // The Hip shot down (or landed): nobody calls the drones out any more, the last call is void.
  if (!active(targets)) {
    designated_ = -1;
    clear_ = 0.0f;
    return;
  }
  // The drones a Mi-17 sees; the one nearest the player.
  int best = -1;
  float bestDist = 1e9f;
  for (const Vehicle& s : targets.list) {
    if (!isSpotter(s)) continue;
    for (size_t k = 0; k < targets.list.size(); ++k) {
      const Vehicle& v = targets.list[k];
      if (!isDrone(v) || glm::distance(v.pos, s.pos) > kSpotRange || !sees(s.pos, v.pos)) continue;
      const float d = glm::distance(v.pos, heli.pos);
      if (d < bestDist) bestDist = d, best = int(k);
    }
  }
  if (best < 0) {
    if (lost) clear_ = 4.0f;
    return;
  }
  // The one called kept unless another is clearly nearer (no flicking between two alike).
  if (designated_ >= 0 && best != designated_ && bestDist > 0.8f * glm::distance(targets.list[size_t(designated_)].pos, heli.pos))
    best = designated_;
  clear_ = 0.0f;
  designated_ = best;
}

}  // namespace sim
