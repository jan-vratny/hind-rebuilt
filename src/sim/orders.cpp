#include "sim/orders.hpp"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

// The answers (SOUND/MAXIMAL.BLK, DRONE019..026): "Hind leader, I'm returning to base / I'm
// following you / I'm attacking your target / I'm resuming the flight plan", then the same with
// "we're" from the formation.
constexpr int kAnswerOne[4] = {69, 67, 68, 70}, kAnswerAll[4] = {73, 71, 72, 74};
const char* const kSaid[4] = {"RETURNING TO BASE", "FOLLOWING YOU", "ATTACKING YOUR TARGET", "RESUMING THE FLIGHT PLAN"};

bool ourHelicopter(const Vehicle& v) {
  return !v.destroyed && !v.aboard && v.side == Side::Ours && vehicleType(v.kind).cls == UnitClass::Helicopter && !v.route.empty();
}

glm::vec2 flat(glm::vec3 p) { return {p.x, p.z}; }

}  // namespace

float Orders::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 24);
}

int Orders::wingman(const Targets& targets) {
  for (size_t i = 0; i < targets.list.size(); ++i)
    if (ourHelicopter(targets.list[i]) && vehicleType(targets.list[i].kind).original.find("Mi_24") != std::string::npos) return int(i);
  return -1;
}

std::vector<int> Orders::formation(const Targets& targets) {
  std::vector<int> v;
  for (size_t i = 0; i < targets.list.size(); ++i)
    if (ourHelicopter(targets.list[i])) v.push_back(int(i));
  return v;
}

Orders::Unit& Orders::unit(int index, const Targets& targets) {
  for (Unit& u : units_)
    if (u.index == index) return u;
  Unit u;
  u.index = index;
  const Vehicle& v = targets.list[size_t(index)];
  u.plan = v.route, u.planNext = v.next, u.planLoop = v.loop;
  u.slot = int(units_.size());
  units_.push_back(u);
  return units_.back();
}

int Orders::give(Order o, bool formation, const Helicopter& heli, Targets& targets, int target, std::string& said) {
  std::vector<int> who;
  if (formation) who = Orders::formation(targets);
  else if (const int w = wingman(targets); w >= 0) who.push_back(w);
  if (who.empty()) {
    said = formation ? "NO FORMATION TO ORDER" : "NO WINGMAN";
    return -1;
  }
  if (o == Order::Attack && (target < 0 || targets.list[size_t(target)].destroyed)) {
    said = "NO TARGET TO ATTACK";
    return -1;
  }
  (void)heli;
  for (int i : who) {
    Unit& u = unit(i, targets);
    u.order = o;
    u.target = o == Order::Attack ? target : -1;
    u.fire = 2.0f + random();
    u.routed = false;
    Vehicle& v = targets.list[size_t(i)];
    v.delay = v.waiting = 0.0f;
    if (o == Order::Plan) {
      // Back onto its plan, at the point it was heading for when first ordered.
      v.route = u.plan, v.loop = u.planLoop;
      v.next = std::min(std::max<size_t>(u.planNext, 1), v.route.size());
      v.route[0].pos = flat(v.pos);
      v.hover = false;
    }
  }
  said = std::string(formation ? "FORMATION: " : "WINGMAN: ") + kSaid[int(o)];
  return (formation ? kAnswerAll : kAnswerOne)[int(o)];
}

void Orders::step(const Helicopter& heli, Targets& targets, const world::Terrain& terrain, float dt, std::vector<Impact>& impacts) {
  const glm::vec3 fwd3 = heli.forward();
  glm::vec2 fwd = glm::length(glm::vec2(fwd3.x, fwd3.z)) > 0.01f ? glm::normalize(glm::vec2(fwd3.x, fwd3.z)) : glm::vec2(0.0f, -1.0f);
  const glm::vec2 right{-fwd.y, fwd.x};
  const float agl = std::max(30.0f, heli.radarAltitude());
  const float speed = glm::length(glm::vec2(heli.vel.x, heli.vel.z));
  for (Unit& u : units_) {
    if (u.index < 0 || size_t(u.index) >= targets.list.size()) continue;
    Vehicle& v = targets.list[size_t(u.index)];
    if (v.destroyed || v.aboard || u.order == Order::Plan) continue;
    if (u.order == Order::Attack && (u.target < 0 || targets.list[size_t(u.target)].destroyed)) u.order = Order::Follow;
    const glm::vec2 at = flat(v.pos);
    std::vector<RoutePoint> route{{at, agl, 1.0f, 0.0f}};
    if (u.order == Order::Follow) {
      // Its place 60 m to the right and 40 m back for each one before it, flown towards a point
      // some seconds ahead of it so that it keeps up (faster while it is behind).
      const glm::vec2 slot = flat(heli.pos) + right * (60.0f * float(u.slot + 1)) - fwd * (40.0f * float(u.slot + 1));
      const float behind = glm::dot(slot - at, fwd);
      const float s = std::clamp(speed + 0.25f * behind, 5.0f, 85.0f);
      route.push_back({slot + fwd * (s * 6.0f), agl, s, 0.0f});
    } else if (u.order == Order::Attack) {
      const Vehicle& t = targets.list[size_t(u.target)];
      const UnitClass cls = vehicleType(t.kind).cls;
      const bool air = cls == UnitClass::Drone || cls == UnitClass::Aircraft || cls == UnitClass::Helicopter;
      const float d = glm::distance(v.pos, t.pos);
      if (air) {
        // Up behind it and alongside, 30 m above, at its speed or faster when far.
        const glm::vec2 tv = flat(t.vel), aim = flat(t.pos) + tv * 4.0f;
        const float tagl = t.pos.y - terrain.groundHeight(t.pos.x, t.pos.z);
        const float s = std::clamp(glm::length(tv) + 0.1f * d, 20.0f, 85.0f);
        route.push_back({aim, std::max(25.0f, tagl) + 30.0f, s, 0.0f});
        route[0].alt = route[1].alt;
      } else {
        // To 1 km of it, its rockets on it from there.
        const glm::vec2 to = flat(t.pos) - at;
        const float len = glm::length(to);
        const glm::vec2 stand = len > 1000.0f ? at + to * ((len - 900.0f) / len) : at;
        route.push_back({stand, 120.0f, 60.0f, 0.0f});
        route[0].alt = 120.0f;
      }
      // Its bursts: the gun at something in the air within 400 m, rockets at the ground within
      // 1.5 km; now and then a hit.
      u.fire -= dt;
      if (u.fire <= 0.0f && d < (air ? 400.0f : 1500.0f)) {
        u.fire = air ? 1.5f + random() : 3.0f + 2.0f * random();
        const WeaponType w = air ? WeaponType::YakB : WeaponType::S8;
        const bool hit = random() < (air ? 0.45f : 0.35f);
        glm::vec3 p = t.pos + glm::vec3(0.0f, 1.0f, 0.0f);
        if (!hit) p += glm::vec3(random() * 40.0f - 20.0f, air ? random() * 20.0f - 10.0f : -1.0f, random() * 40.0f - 20.0f);
        // The wingman's kills are his, another helicopter's the allied forces'.
        const bool wing = u.index == wingman(targets);
        const std::vector<int> killed = targets.damage(hit ? u.target : -1, w, p, wing ? Killer::Wingman : Killer::Allied, wing ? "your wingman" : "");
        if (!air || hit) impacts.push_back({w, p, hit ? u.target : -1, false});
        for (int k : killed) impacts.push_back({WeaponType::S8, targets.list[size_t(k)].pos + glm::vec3(0, 1.2f, 0), k, true});
      }
    } else if (u.order == Order::Home) {
      // Where it took off (our base), down onto it: laid out once; at its end (the last
      // 150 m an aircraft does not fly) it lets down onto the spot.
      const glm::vec2 home = u.plan.empty() ? at : u.plan.front().pos;
      if (!u.routed) {
        u.routed = true;
        route[0].alt = agl;
        if (glm::distance(home, at) > 400.0f) route.push_back({home + glm::normalize(at - home) * 300.0f, 80.0f, 50.0f, 0.0f});
        route.push_back({home, 0.0f, 15.0f, 0.0f});
        v.route = route, v.next = 1, v.loop = false, v.waiting = v.delay = 0.0f;
      } else if (v.next >= v.route.size()) {
        const glm::vec2 to = home - at;
        const float len = glm::length(to);
        if (len > 1.0f) v.pos += glm::vec3(to.x, 0.0f, to.y) / len * std::min(len, 8.0f * dt);
        v.pos.y -= std::min(3.0f * dt, std::max(0.0f, v.pos.y - terrain.groundHeight(v.pos.x, v.pos.z)));
        v.hover = v.pos.y > terrain.groundHeight(v.pos.x, v.pos.z) + 0.5f;
      }
      continue;
    }
    v.route = route, v.next = 1, v.loop = false, v.waiting = v.delay = 0.0f;
    v.hover = true;
  }
}

}  // namespace sim
