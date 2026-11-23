#include "sim/wso.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sim {

namespace {

glm::vec3 centre(const Vehicle& v) {
  const VehicleType& t = vehicleType(v.kind);
  return glm::vec3(v.toWorld() * glm::vec4((t.lo + t.hi) * 0.5f, 1.0f));
}

float angleBetween(glm::vec3 a, glm::vec3 b) {
  return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f));
}

void announce(Helicopter& heli, const Targets& targets, int k) {
  heli.setMessage(std::string("WSO: ") + vehicleType(targets.list[size_t(k)].kind).name, 2.0f);
}

}  // namespace

void Wso::setEnabled(bool on, Helicopter& heli) {
  if (on == enabled_) return;
  enabled_ = on;
  searchTimer_ = 0.0f;
  heli.setMessage(on ? "SILICON WSO ON" : "SILICON WSO OFF", 2.0f);
}

static bool isFlier(const Vehicle& v) {
  const UnitClass c = vehicleType(v.kind).cls;
  return c == UnitClass::Aircraft || c == UnitClass::Helicopter || c == UnitClass::Drone;
}

float Wso::reach(const Vehicle& v) const {
  return airFirst && isFlier(v) ? std::max(kSearchRange, Weapons::kR60Range) : kSearchRange;
}

bool Wso::visible(glm::vec3 from, glm::vec3 to) const {
  glm::vec3 d = to - from;
  int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 25.0f));
  for (int i = 1; i < steps; ++i) {
    glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain_.groundHeight(p.x, p.z) + 0.5f) return false;
  }
  return true;
}

std::vector<int> Wso::candidates(const Helicopter& heli, const Targets& targets) const {
  glm::vec3 f = heli.forward();
  glm::vec2 fh = glm::length(glm::vec2(f.x, f.z)) > 1e-3f ? glm::normalize(glm::vec2(f.x, f.z)) : glm::vec2(0, -1);
  std::vector<std::pair<float, int>> scored;
  for (size_t k = 0; k < targets.list.size(); ++k) {
    const Vehicle& v = targets.list[k];
    if (v.destroyed || v.aboard || v.scenery || (v.side == Side::Ours && !allies)) continue;
    glm::vec3 c = centre(v);
    glm::vec3 d = c - heli.pos;
    float dist = glm::length(d);
    glm::vec2 dh{d.x, d.z};
    if (dist > reach(v) || glm::length(dh) < 1.0f) continue;
    float angle = std::acos(std::clamp(glm::dot(fh, glm::normalize(dh)), -1.0f, 1.0f));
    if (angle > kSearchAngle || !visible(heli.pos, c)) continue;
    // Vehicles and guns before soldiers on foot.
    float rank = vehicleType(v.kind).cls == UnitClass::Soldier ? 2.0f : 1.0f;
    // (With the R-60, the fliers its seeker can have first, then other fliers, then the rest.)
    if (airFirst) rank *= !isFlier(v) ? 100.0f : dist > Weapons::r60LockRange(v, heli.pos) ? 10.0f : 1.0f;
    scored.push_back({rank * dist * (1.0f + 2.0f * angle), int(k)});
  }
  std::sort(scored.begin(), scored.end());
  std::vector<int> out;
  for (auto& s : scored) out.push_back(s.second);
  return out;
}

void Wso::rescan(Helicopter& heli, const Targets& targets) {
  std::vector<int> c = candidates(heli, targets);
  if (c.empty()) {
    target_ = -1;
    heli.setMessage("WSO: NO TARGETS", 2.0f);
    return;
  }
  auto it = std::find(c.begin(), c.end(), target_);
  target_ = it == c.end() || it + 1 == c.end() ? c.front() : *(it + 1);
  announce(heli, targets, target_);
}

void Wso::track(Helicopter& heli, const Targets& targets, glm::vec3 eye, glm::vec3 dir) {
  int best = -1;
  float bestAngle = 0.07f;  // about 4 degrees around the sight
  for (size_t k = 0; k < targets.list.size(); ++k) {
    const Vehicle& v = targets.list[k];
    if (v.destroyed || v.aboard || v.scenery || (v.side == Side::Ours && !allies)) continue;
    glm::vec3 c = centre(v);
    float a = angleBetween(c - eye, dir);
    if (a < bestAngle && glm::distance(c, eye) < reach(v) && visible(eye, c)) {
      bestAngle = a;
      best = int(k);
    }
  }
  if (best < 0) {
    heli.setMessage("WSO: NO TARGET IN SIGHT", 2.0f);
    return;
  }
  target_ = best;
  announce(heli, targets, best);
}

bool Wso::designate(Helicopter& heli, const Targets& targets, int k) {
  if (k < 0 || k >= int(targets.list.size()) || k == target_) return false;
  const Vehicle& v = targets.list[size_t(k)];
  const glm::vec3 c = centre(v);
  if (v.destroyed || glm::distance(c, heli.pos) > reach(v) || !visible(heli.pos, c)) return false;
  target_ = k;  // (not announced: the Hip's line already says which, where)
  return true;
}

void Wso::update(Helicopter& heli, Weapons& weapons, const Targets& targets, float dt) {
  if (target_ >= int(targets.list.size())) target_ = -1;
  if (target_ >= 0) {
    const Vehicle& v = targets.list[size_t(target_)];
    glm::vec3 c = centre(v);
    if (v.destroyed || glm::distance(c, heli.pos) > reach(v) * 1.2f || !visible(heli.pos, c)) {
      target_ = -1;
      searchTimer_ = 1.0f;  // a moment to see the result before he looks for the next one
    }
  }
  // With the R-60: a target its seeker cannot have yet gives way to one it can.
  if (enabled_ && airFirst && target_ >= 0) {
    const Vehicle& v = targets.list[size_t(target_)];
    searchTimer_ -= dt;
    if (glm::distance(v.pos, heli.pos) > Weapons::r60LockRange(v, heli.pos) && searchTimer_ <= 0.0f) {
      searchTimer_ = 0.5f;
      const std::vector<int> c = candidates(heli, targets);
      if (!c.empty() && c.front() != target_) {
        const Vehicle& b = targets.list[size_t(c.front())];
        if (isFlier(b) && glm::distance(b.pos, heli.pos) <= Weapons::r60LockRange(b, heli.pos)) {
          target_ = c.front();
          announce(heli, targets, target_);
        }
      }
    }
  }
  if (enabled_ && target_ < 0) {
    searchTimer_ -= dt;
    if (searchTimer_ <= 0.0f) {
      searchTimer_ = 0.5f;
      std::vector<int> c = candidates(heli, targets);
      if (!c.empty()) {
        target_ = c.front();
        announce(heli, targets, target_);
      }
    }
  }
  if (target_ < 0) return;

  // Lay the gun with lead: the round's path to where the target will be.
  const glm::vec3 muzzle = heli.pos + heli.orient * Weapons::kTurretPivot;
  const glm::vec3 dir = Weapons::gunAim(muzzle, heli.vel, aimPoint(targets), targets.list[size_t(target_)].vel);
  glm::vec3 b = glm::inverse(heli.orient) * dir;
  weapons.aimTurret(std::atan2(-b.x, -b.z), std::asin(std::clamp(b.y, -1.0f, 1.0f)));
}

glm::vec3 Wso::aimPoint(const Targets& targets) const {
  return target_ >= 0 ? centre(targets.list[size_t(target_)]) : glm::vec3(0.0f);
}

bool Wso::lasing(const Helicopter& heli, const Targets& targets) const {
  return target_ >= 0 && angleBetween(aimPoint(targets) - heli.pos, heli.forward()) < kLaserAngle;
}

float Wso::range(const Helicopter& heli, const Targets& targets) const {
  return target_ >= 0 ? glm::distance(aimPoint(targets), heli.pos) : 0.0f;
}

}  // namespace sim
