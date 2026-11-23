#include "sim/battle.hpp"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

bool flies(const VehicleType& t) { return t.cls == UnitClass::Aircraft || t.cls == UnitClass::Helicopter || t.cls == UnitClass::Drone; }

// What a weapon does to our units: its reach against ground and air targets (m, 0 none), how
// often it fires (a burst or one round; s, from .. to), the damage of a burst that hits by the
// target's armour (soft, light, heavy) and the chance it hits at point blank.
struct Fire {
  float ground, air;
  float everyMin, everyMax;
  float damage[3];
  float hit;
  EnemyRound round;
};
const Fire kRifle{500.0f, 400.0f, 3.0f, 6.0f, {0.4f, 0.05f, 0.0f}, 0.6f, EnemyRound::Bullet};
const Fire kHmg{1200.0f, 1500.0f, 3.5f, 7.0f, {1.2f, 0.6f, 0.02f}, 0.7f, EnemyRound::Bullet};
const Fire kAaa{1500.0f, 3500.0f, 3.0f, 6.0f, {3.0f, 2.0f, 0.2f}, 0.7f, EnemyRound::Shell};
const Fire kRpg{500.0f, 500.0f, 8.0f, 14.0f, {6.0f, 6.0f, 4.0f}, 0.45f, EnemyRound::Rpg};
const Fire kStrela{0.0f, 4000.0f, 15.0f, 25.0f, {12.0f, 12.0f, 12.0f}, 0.45f, EnemyRound::Strela};
const Fire kRapier{0.0f, 6500.0f, 15.0f, 25.0f, {14.0f, 14.0f, 14.0f}, 0.6f, EnemyRound::Rapier};
// A tank's gun (HE and AP rounds) and coaxial machine gun, at the ground only.
const Fire kTank{2000.0f, 0.0f, 6.0f, 10.0f, {6.0f, 8.0f, 12.0f}, 0.55f, EnemyRound::Shell};
// A gunship's cannon and rockets, a fighter's strafing pass.
const Fire kGunship{2000.0f, 1500.0f, 4.0f, 8.0f, {4.0f, 4.0f, 2.0f}, 0.5f, EnemyRound::Shell};
const Fire kFighter{1500.0f, 1200.0f, 8.0f, 15.0f, {4.0f, 3.0f, 1.0f}, 0.4f, EnemyRound::Shell};

const Fire* fireOf(const VehicleType& t) {
  switch (t.arms) {
    case Arms::Rifle: return &kRifle;
    case Arms::Hmg: return &kHmg;
    case Arms::Aaa: return &kAaa;
    case Arms::Rpg: return &kRpg;
    case Arms::Manpads: return &kStrela;
    case Arms::Sam: return t.radarClass > 0 ? &kRapier : &kStrela;
    case Arms::TankGun: return &kTank;
    case Arms::Gunship: return &kGunship;
    case Arms::Fighter: return &kFighter;
    default: return nullptr;
  }
}

}  // namespace

float GroundBattle::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 24);
}

bool GroundBattle::lineOfSight(glm::vec3 from, glm::vec3 to) const {
  const glm::vec3 d = to - from;
  const int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 40.0f));
  for (int i = 1; i < steps; ++i) {
    const glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain_.groundHeight(p.x, p.z) + 0.3f) return false;
  }
  return true;
}

void GroundBattle::step(Targets& targets, glm::vec3 heliPos, float dt, std::vector<EnemyEvent>& events) {
  if (!enabled) return;
  if (crews_.size() != targets.list.size()) crews_.resize(targets.list.size());
  // The units of each side that can be fought: on the battlefield near the player, not the zone's
  // buildings, not the drones (those are the helicopters' to hunt).
  std::vector<int> ours, theirs;
  for (size_t i = 0; i < targets.list.size(); ++i) {
    const Vehicle& v = targets.list[i];
    const UnitClass cls = vehicleType(v.kind).cls;
    if (v.destroyed || v.aboard || v.scenery || cls == UnitClass::Drone ||
        glm::distance(glm::vec2(v.pos.x, v.pos.z), glm::vec2(heliPos.x, heliPos.z)) >= kActiveRange)
      continue;
    // (Our jets pass through: they fly their raids as the mission has them.)
    if (v.side == Side::Ours && cls != UnitClass::Aircraft) ours.push_back(int(i));
    if (v.side == Side::Enemy) theirs.push_back(int(i));
  }
  if (ours.empty()) return;

  for (size_t i = 0; i < targets.list.size(); ++i) {
    const Vehicle& v = targets.list[i];
    const VehicleType& t = vehicleType(v.kind);
    if (v.side == Side::Neutral || v.destroyed || v.aboard || !v.armed) continue;
    const bool our = v.side == Side::Ours;
    const std::vector<int>& foes = our ? theirs : ours;
    const Fire* f = fireOf(t);
    if (!f) continue;
    Crew& c = crews_[i];
    if (c.missiles < 0) {
      // As at the helicopter: not every man carries a missile, and only a few.
      const bool missile = f->round == EnemyRound::Strela || f->round == EnemyRound::Rapier;
      c.missiles = !missile ? 1 << 30 : t.cls == UnitClass::Soldier ? (random() < 0.4f ? 1 : 0) : 4;
      c.look = random() * 2.0f;
      c.next = f->everyMin + random() * (f->everyMax - f->everyMin);
    }
    if (c.missiles == 0) continue;
    const glm::vec3 muzzle = v.pos + glm::vec3(0.0f, t.cls == UnitClass::Soldier ? 1.5f : 2.0f, 0.0f);
    auto reach = [&](const Vehicle& o) { return flies(vehicleType(o.kind)) && o.pos.y - terrain_.groundHeight(o.pos.x, o.pos.z) > 5.0f ? f->air : f->ground; };

    // Once a second or two: the nearest of ours in reach and in sight.
    c.look -= dt;
    if (c.look <= 0.0f) {
      c.look = 1.5f + random();
      c.target = -1;
      float best = 1e9f;
      for (int k : foes) {
        const Vehicle& o = targets.list[size_t(k)];
        const float d = glm::distance(o.pos, v.pos);
        if (d < best && d <= reach(o) && lineOfSight(muzzle, o.pos + glm::vec3(0.0f, 1.0f, 0.0f))) best = d, c.target = k;
      }
    }
    if (c.target < 0) continue;
    const Vehicle& o = targets.list[size_t(c.target)];
    if (o.destroyed || o.aboard || std::find(foes.begin(), foes.end(), c.target) == foes.end()) {
      c.target = -1;
      continue;
    }
    c.next -= dt;
    if (c.next > 0.0f) continue;
    c.next = f->everyMin + random() * (f->everyMax - f->everyMin);
    const float range = glm::distance(o.pos, v.pos);
    if (range > reach(o)) continue;

    const VehicleType& ot = vehicleType(o.kind);
    const bool missile = f->round == EnemyRound::Strela || f->round == EnemyRound::Rapier;
    EnemyEvent shot{missile || f->round == EnemyRound::Rpg ? EnemyEvent::Launch : EnemyEvent::Muzzle, muzzle, f->round};
    shot.atHeli = false;
    events.push_back(shot);
    if (missile) --c.missiles;
    // The chance falls off to a third at the end of the reach, and to less than half against
    // an aircraft crossing fast.
    float chance = f->hit * (1.0f - 0.65f * range / std::max(reach(o), 1.0f));
    if (flies(ot) && !missile) chance *= glm::length(o.vel) > 40.0f ? 0.35f : 0.8f;
    if (ot.cls == UnitClass::Soldier) chance *= 0.35f;  // men in cover
    if (our) chance *= kOurAim;
    if (random() >= chance) continue;
    if (targets.harm(c.target, f->damage[int(ot.armour)], our ? Killer::Allied : Killer::Enemy) || missile || f->round == EnemyRound::Rpg) {
      EnemyEvent burst{EnemyEvent::Burst, o.pos + glm::vec3(0.0f, 1.0f, 0.0f), f->round};
      burst.atHeli = false;
      events.push_back(burst);
    }
  }
}

}  // namespace sim
