#include "sim/threats.hpp"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

bool lineOfSight(const world::Terrain& terrain, glm::vec3 from, glm::vec3 to) {
  glm::vec3 d = to - from;
  int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 25.0f));
  for (int i = 1; i < steps; ++i) {
    glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain.groundHeight(p.x, p.z) + 0.5f) return false;
  }
  return true;
}

}  // namespace

float radarRange(VehicleKind k) { return vehicleType(k).radarRange; }

Threat greatestThreat(const Helicopter& heli, const Targets& targets, const world::Terrain& terrain) {
  Threat best;
  for (size_t k = 0; k < targets.list.size(); ++k) {
    const Vehicle& v = targets.list[k];
    float range = radarRange(v.kind);
    if (v.destroyed || range <= 0.0f || v.side != Side::Enemy) continue;
    glm::vec3 antenna = v.pos + glm::vec3(0.0f, 3.0f, 0.0f);
    float dist = glm::length(heli.pos - antenna);
    float proximity = 1.0f - dist / range;
    if (proximity <= 0.0f || proximity <= best.proximity || !lineOfSight(terrain, antenna, heli.pos)) continue;
    glm::vec3 to = antenna - heli.pos;
    float bearing = glm::degrees(std::atan2(to.x, -to.z)) - heli.headingDeg();
    bearing = std::remainder(bearing, 360.0f);
    best = {true, ThreatClass(vehicleType(v.kind).radarClass), bearing, proximity, int(k)};
  }
  return best;
}

}  // namespace sim
