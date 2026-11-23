#include "sim/wing_arms.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace sim {

namespace {

bool ourHind(const Vehicle& v) {
  return v.side == Side::Ours && !v.destroyed && vehicleType(v.kind).cls == UnitClass::Helicopter &&
         vehicleType(v.kind).original.find("Mi_24") != std::string::npos;
}

bool r60After(const Weapons* w, int target) {
  return w && std::any_of(w->shots.begin(), w->shots.end(), [&](const Projectile& s) {
           return s.type == WeaponType::R60 && s.age < 1e9f && s.target == target;
         });
}

}  // namespace

void WingArms::arm(const Targets& targets, const Loadout& player, bool infinite, bool r60Reloads) {
  hinds_.clear();
  bool first = true;
  for (size_t i = 0; i < targets.list.size(); ++i) {
    if (!ourHind(targets.list[i]) || targets.list[i].aboard) continue;
    Loadout l;
    l.inner = l.outer = l.wingtip = Store::None;
    if (player.outer != Store::R60 && first) l.outer = Store::R60;
    else l.inner = Store::Interceptor;
    l.settle();
    Hind h;
    h.index = int(i);
    h.heli = std::make_unique<Helicopter>(terrain_);
    const Vehicle& v = targets.list[i];
    h.heli->resetAirborne(v.pos, v.heading, 0.0f);
    h.weapons = std::make_unique<Weapons>(terrain_);
    h.weapons->load(l);
    h.weapons->infiniteAmmo = infinite;
    h.weapons->interceptAny = true;  // (no door gunner of its own at the Shaheds)
    h.weapons->r60Reloads = r60Reloads;
    // (The first of them is the wingman, as Orders has it.)
    h.weapons->killer = first ? Killer::Wingman : Killer::Allied;
    h.weapons->killerName = first ? "your wingman" : "";
    h.look = 1.0f + 0.3f * float(hinds_.size());
    hinds_.push_back(std::move(h));
    first = false;
  }
}

void WingArms::step(Targets& targets, const Weapons* player, float dt, std::vector<Impact>& impacts) {
  for (Hind& h : hinds_) {
    if (h.index < 0 || size_t(h.index) >= targets.list.size()) continue;
    const Vehicle& v = targets.list[size_t(h.index)];
    Helicopter& heli = *h.heli;
    Weapons& w = *h.weapons;
    heli.crashed = v.destroyed || v.aboard;
    heli.pos = v.pos;
    heli.vel = v.vel;
    heli.orient = glm::angleAxis(-v.heading, glm::vec3(0, 1, 0));
    heli.weightOnWheels = v.pos.y - terrain_.groundHeight(v.pos.x, v.pos.z) > 3.0f ? 0.0f : 1.0f;
    w.step(heli, false, dt, impacts, &targets);
    w.fired.clear();  // (heard where the player is: theirs are not)
    if (heli.crashed || heli.weightOnWheels > 0.0f || w.rounds(WeaponType::R60) <= 0) continue;
    if ((h.look -= dt) > 0.0f) continue;
    h.look = 0.5f;
    if (std::any_of(w.shots.begin(), w.shots.end(), [](const Projectile& s) { return s.type == WeaponType::R60 && s.age < 1e9f; }))
      continue;  // one at a time, its result seen first
    // The nearest fast drone ahead (a Banderol, a Geran-3) none of ours is after.
    const glm::vec2 fwd{std::sin(v.heading), -std::cos(v.heading)};
    int best = -1;
    float bestDist = kR60Reach;
    for (size_t i = 0; i < targets.list.size(); ++i) {
      const Vehicle& t = targets.list[i];
      if (t.destroyed || t.aboard || t.side != Side::Enemy || !isJetDrone(t.kind) || !t.moving() || t.delay > 0.0f) continue;
      const glm::vec3 to = t.pos - v.pos;
      const float d = glm::length(to);
      const glm::vec2 flat{to.x, to.z};
      if (d >= bestDist || glm::length(flat) < 1.0f) continue;
      if (std::acos(std::clamp(glm::dot(glm::normalize(flat), fwd), -1.0f, 1.0f)) > kR60Look) continue;
      if (r60After(&w, int(i)) || r60After(player, int(i))) continue;
      best = int(i), bestDist = d;
    }
    if (best < 0) continue;
    // Turned onto it: the launch along the line to it.
    const glm::vec3 to = glm::normalize(targets.list[size_t(best)].pos - heli.pos);
    heli.orient = glm::quatLookAt(to, glm::vec3(0, 1, 0));
    w.r60Track = best;
    if (w.select(WeaponType::R60)) w.step(heli, true, 0.0f, impacts, &targets);  // (the trigger pressed)
    w.fired.clear();
  }
}

std::vector<WingArms::Stores> WingArms::stores() const {
  std::vector<Stores> out;
  for (const Hind& h : hinds_) {
    Stores s;
    s.index = int16_t(h.index);
    for (int p = 0; p < kPylons; ++p) {
      s.store[p] = uint8_t(h.weapons->loadout().at(Pylon(p)));
      for (int side = 0; side < 2; ++side) s.rounds[p][side] = int8_t(std::clamp(h.weapons->sideRounds(Pylon(p), side), 0, 127));
    }
    out.push_back(s);
  }
  return out;
}

std::vector<Projectile> WingArms::shots() const {
  std::vector<Projectile> all;
  for (const Hind& h : hinds_) all.insert(all.end(), h.weapons->shots.begin(), h.weapons->shots.end());
  return all;
}

}  // namespace sim
