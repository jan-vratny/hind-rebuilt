#include "sim/weapons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/quaternion.hpp>

namespace sim {

namespace {

// Gun pod muzzles and rocket pod fronts relative to their pylon (from the converted Mi-24 model).
constexpr float kPodFrontZ = -0.10f;
constexpr float kUpkMuzzleZ = -0.90f;
constexpr glm::vec3 kBombRelease{0.0f, -0.25f, 1.0f};

struct Ballistics {
  float muzzle;     // m/s relative to the helicopter
  float interval;   // s between rounds
  float spread;     // rad, uniform half-angle
  float drag;       // 1/m, dv/dt = -drag * v^2 (after burn-out for rockets)
  float life;       // s
};
// Yak-B: 4000-5000 rounds/min, 860 m/s. UPK-23: two pods, each GSh-23L ~3400 rounds/min, 715 m/s.
// S-8: rippled in pairs; the motor burns 0.7 s and adds ~550 m/s. S-5: smaller and faster to
// ripple, the motor burns 0.9 s and adds ~500 m/s. Bombs fall free, ~300 m/s at the most.
constexpr Ballistics kYakB{860.0f, 60.0f / 4000.0f, 0.0025f, 0.0006f, 6.0f};
constexpr Ballistics kUpk{715.0f, 60.0f / 6800.0f, 0.003f, 0.0005f, 6.0f};
constexpr Ballistics kS8{35.0f, 0.12f, 0.006f, 0.00015f, 20.0f};
constexpr Ballistics kS5{30.0f, 0.06f, 0.009f, 0.0002f, 18.0f};
constexpr Ballistics kBomb{1.0f, 0.25f, 0.0f, 0.00011f, 90.0f};
// KMGU-2: a compartment throws out its small mines downwards, they scatter and drift down
// (light and draggy) over a strip along the track.
constexpr Ballistics kMine{6.0f, 0.5f, 0.45f, 0.004f, 90.0f};
constexpr int kMinesPerCompartment = 12;  // drawn and laid; the real one holds 36
// R-60: off its rail, the motor burns 3 s to some 2.5 times the speed of sound; the infra-red
// seeker steers it onto the aircraft by proportional navigation at up to ~35 g; a proximity
// fuse sets the rod warhead off near it.
constexpr Ballistics kR60{40.0f, 1.0f, 0.0f, 0.00012f, 20.0f};
constexpr float kR60Burn = 3.0f, kR60Thrust = 260.0f, kR60Turn = 350.0f, kR60Fuse = 7.0f;
// 9M114 Shturm: leaves its tube at ~60 m/s, the motor takes it to ~400 m/s within a second; it
// slows to ~330 m/s by 5 km (about 14 s) and destroys itself at 6 km. Radio commands keep it on
// the sight's line of sight, turning at up to ~12 g.
constexpr Ballistics kShturm{60.0f, 1.0f, 0.0f, 0.0f, 17.0f};
// GShG-7.62: four barrels, ~3500 rounds/min, 7.62 x 54R at ~800 m/s, slowing faster than 12.7 mm.
constexpr Ballistics kGshg{800.0f, 60.0f / 3500.0f, 0.004f, 0.0011f, 4.0f};
// Interceptor drone: dropped off its rack at some 25 m/s, its motor takes it up to its speed.
constexpr Ballistics kInterceptor{25.0f, 1.0f, 0.0f, 0.0f, Weapons::kInterceptorLife};
// Off the rack on the left (side 0) or right pylon: ahead along the helicopter, a touch outwards
// and down.
glm::vec3 interceptorLaunchDir(int side) { return glm::normalize(glm::vec3(side == 1 ? 0.08f : -0.08f, -0.08f, -1.0f)); }
// How hard it can turn, m/s^2 across its flight: its wings' lift, so with the square of its
// speed up to kInterceptorTurn at full speed (a circle of the same ~200 m below that).
float interceptorPull(float speed) {
  const float f = std::min(1.0f, speed / Weapons::kInterceptorSpeed);
  return Weapons::kInterceptorTurn * f * f;
}
// Fresh off the rack it turns gently away from the helicopter, sideways to its flight.
glm::vec3 interceptorPeel(glm::vec3 dir, glm::vec3 pos, glm::vec3 heliPos, float dt) {
  glm::vec3 away = pos - heliPos;
  away.y = 0.0f;
  away -= dir * glm::dot(away, dir);
  if (glm::length(away) < 0.1f) return dir;
  return glm::normalize(dir + glm::normalize(away) * (Weapons::kInterceptorPeel * dt));
}
constexpr float kShturmBurn = 1.0f, kShturmTop = 400.0f, kShturmEnd = 330.0f, kShturmRange = 6000.0f;
constexpr float kShturmTurn = 120.0f;  // m/s^2
constexpr float kS8Burn = 0.7f, kS8Thrust = 790.0f;  // s, m/s^2
constexpr float kS5Burn = 0.9f, kS5Thrust = 560.0f;

const Ballistics& ballistics(WeaponType t) {
  switch (t) {
    case WeaponType::YakB: return kYakB;
    case WeaponType::Upk23: return kUpk;
    case WeaponType::S8: return kS8;
    case WeaponType::S5: return kS5;
    case WeaponType::Shturm: return kShturm;
    case WeaponType::Mine: return kMine;
    case WeaponType::R60: return kR60;
    case WeaponType::DoorGun: return kGshg;
    case WeaponType::Interceptor: return kInterceptor;
    default: return kBomb;
  }
}

// Masses: B-8V20 ~160 kg empty, 20 x 11.3 kg rockets; UB-32A ~100 kg empty, 32 x 3.9 kg;
// UPK-23/250 218 kg loaded; FAB-250 M-54 ~250 kg, OFAB-250-270 ~266 kg, FAB-500 and ZAB-500
// ~500 kg; KMGU-2 ~170 kg loaded; R-60 with its rail ~60 kg; two 9M114 in their tubes ~90 kg
// with the launcher (each missile 31 kg).
const StoreInfo kStoreInfo[kStores] = {
    {"none", "NONE", "NO STORES", WeaponType::YakB, 0, 0.0f, 0.0f, {true, true, true}, true},
    {"b8v20", "B-8V20", "B-8V20 POD: 20 S-8 80 MM ROCKETS", WeaponType::S8, 20, 386.0f, 160.0f, {true, true, false}, true},
    {"ub32", "UB-32", "UB-32A POD: 32 S-5 57 MM ROCKETS", WeaponType::S5, 32, 225.0f, 100.0f, {true, false, false}, true},
    {"upk23", "UPK-23", "UPK-23/250 GUN POD: GSH-23L, 250 ROUNDS", WeaponType::Upk23, 250, 218.0f, 150.0f, {true, false, false}, true},
    {"fab250", "FAB-250", "FAB-250 GENERAL PURPOSE BOMB", WeaponType::Fab250, 1, 250.0f, 0.0f, {true, true, false}, true},
    {"fab500", "FAB-500", "FAB-500 GENERAL PURPOSE BOMB", WeaponType::Fab500, 1, 500.0f, 0.0f, {true, false, false}, true},
    {"ofab250", "OFAB-250", "OFAB-250 BLAST FRAGMENTATION BOMB", WeaponType::Ofab250, 1, 266.0f, 0.0f, {true, true, false}, true},
    {"zab500", "ZAB-500", "ZAB-500 INCENDIARY BOMB", WeaponType::Zab500, 1, 500.0f, 0.0f, {true, false, false}, true},
    {"kmgu", "KMGU-2", "KMGU-2 MINE DISPENSER", WeaponType::Mine, 4, 170.0f, 100.0f, {true, false, false}, true},
    {"r60", "R-60", "R-60 (AA-8 APHID) AIR-TO-AIR MISSILES", WeaponType::R60, 2, 104.0f, 16.0f, {false, true, false}, true},
    {"9m114", "9M114", "9M114 SHTURM (AT-6 SPIRAL): 2 ANTI-TANK MISSILES", WeaponType::Shturm, 2, 90.0f, 28.0f, {false, true, true}, true},
    // Six Litavr drones of some 5 kg on a frame of 15 (the remake's: as the Ukrainian crews
    // hang them under a Mi-8's or Mi-17's pylon).
    {"interceptor", "INTERCEPTOR", "INTERCEPTOR DRONES: 6 ON A RACK, LAUNCHED BY THE CREW", WeaponType::Interceptor, 6, 45.0f, 15.0f,
     {true, false, false}, true},
};

}  // namespace

const char* weaponName(WeaponType t) {
  switch (t) {
    case WeaponType::YakB: return "YAK-B";
    case WeaponType::Upk23: return "UPK-23";
    case WeaponType::S8: return "S-8";
    case WeaponType::S5: return "S-5";
    case WeaponType::Fab250: return "FAB-250";
    case WeaponType::Fab500: return "FAB-500";
    case WeaponType::Ofab250: return "OFAB-250";
    case WeaponType::Zab500: return "ZAB-500";
    case WeaponType::Shturm: return "9M114";
    case WeaponType::Mine: return "KMGU-2";
    case WeaponType::R60: return "R-60";
    case WeaponType::DoorGun: return "GSHG-7.62";
    case WeaponType::Interceptor: return "INTERCEPTOR";
  }
  return "?";
}

bool isBomb(WeaponType t) {
  return t == WeaponType::Fab250 || t == WeaponType::Fab500 || t == WeaponType::Ofab250 || t == WeaponType::Zab500;
}

const char* pylonName(Pylon p) {
  return p == Pylon::Inner ? "INNER PYLONS" : p == Pylon::Outer ? "OUTER PYLONS" : "WINGTIP PYLONS";
}

const StoreInfo& storeInfo(Store s) { return kStoreInfo[int(s)]; }

bool parseStore(const std::string& key, Store& out) {
  for (int i = 0; i < kStores; ++i)
    if (key == kStoreInfo[i].key) {
      out = Store(i);
      return true;
    }
  return false;
}

bool storeFits(Store s, Pylon p, std::string* why) {
  if (storeInfo(s).fits[int(p)]) return true;
  if (why) {
    static const char* where[kPylons] = {"THE INNER PYLONS", "THE OUTER PYLONS", "THE WINGTIPS"};
    *why = std::string(storeInfo(s).name) + " WON'T FIT ON " + where[int(p)];
  }
  return false;
}

float Loadout::mass() const {
  return 2.0f * (storeInfo(inner).mass + storeInfo(outer).mass + storeInfo(wingtip).mass) +
         kDoorGunMass * float(int(doorGun[0]) + int(doorGun[1]));
}

namespace {
const char* const kDoorGunKeys[4] = {"none", "left", "right", "both"};  // by left + 2 * right
}

const char* doorGunsKey(const Loadout& l) { return kDoorGunKeys[int(l.doorGun[0]) + 2 * int(l.doorGun[1])]; }

bool parseDoorGuns(const std::string& key, Loadout& l) {
  for (int i = 0; i < 4; ++i)
    if (key == kDoorGunKeys[i]) {
      l.doorGun[0] = (i & 1) != 0, l.doorGun[1] = (i & 2) != 0;
      return true;
    }
  return false;
}

float referenceStoresMass() { return 4.0f * storeInfo(Store::B8V20).mass; }

glm::vec3 Weapons::pylonPoint(Pylon p) {
  switch (p) {
    case Pylon::Inner: return {-1.70f, -0.79f, 0.0f};
    case Pylon::Outer: return {-2.28f, -0.79f, 0.0f};
    default: return {-3.22f, -1.02f, 0.0f};
  }
}

glm::vec3 Weapons::gunAim(glm::vec3 muzzle, glm::vec3 ownVel, glm::vec3 target, glm::vec3 targetVel, float* time, WeaponType gun) {
  const Ballistics& b = ballistics(gun);
  glm::vec3 dir = glm::normalize(target - muzzle);
  float t = 0.0f;
  for (int iter = 0; iter < 4; ++iter) {
    // Fly a round to where it passes the target's range, see how far it is off, correct.
    glm::vec3 pos = muzzle, vel = ownVel + dir * b.muzzle;
    const float dt = 0.01f;
    t = 0.0f;
    glm::vec3 aim = target;
    for (; t < b.life; t += dt) {
      aim = target + targetVel * t;
      if (glm::dot(pos - muzzle, glm::normalize(aim - muzzle)) >= glm::distance(aim, muzzle)) break;
      vel += (glm::vec3(0.0f, -Helicopter::kGravity, 0.0f) - vel * glm::length(vel) * b.drag) * dt;
      pos += vel * dt;
    }
    dir = glm::normalize(dir * glm::distance(aim, muzzle) + (aim - pos));
  }
  if (time) *time = t;
  return dir;
}

float Weapons::minimumRange(WeaponType t) {
  switch (t) {
    case WeaponType::S5:
    case WeaponType::S8: return 300.0f;
    case WeaponType::Shturm: return 500.0f;
    case WeaponType::R60: return 300.0f;
    default: return 0.0f;
  }
}

float Weapons::effectiveRange(WeaponType t) {
  switch (t) {
    case WeaponType::YakB: return 1500.0f;
    case WeaponType::Upk23: return 2000.0f;
    case WeaponType::S5: return 3000.0f;
    case WeaponType::S8: return 4000.0f;
    case WeaponType::Shturm: return 5000.0f;
    case WeaponType::R60: return kR60Range;
    default: return 1e9f;
  }
}

float Weapons::r60LockRange(const Vehicle& v, glm::vec3 from) {
  if (vehicleType(v.kind).cls != UnitClass::Drone || isJetDrone(v.kind)) return kR60Range;
  // Behind it: within 60 degrees of its tail, its heading being -z turned by v.heading.
  const glm::vec3 tail{-std::sin(v.heading), 0.0f, std::cos(v.heading)};
  const glm::vec3 to = from - v.pos;
  const float flat = glm::length(glm::vec2(to.x, to.z));
  const bool behind = flat > 1.0f && glm::dot(glm::vec2(to.x, to.z) / flat, glm::vec2(tail.x, tail.z)) > 0.5f;
  return behind ? 1500.0f : 600.0f;
}

int Weapons::r60Seek(const Helicopter& heli, const Targets& targets, int tracked) {
  if (tracked < 0 || tracked >= int(targets.list.size())) return -1;
  const Vehicle& v = targets.list[size_t(tracked)];
  const UnitClass c = vehicleType(v.kind).cls;
  if (v.destroyed || v.aboard || v.side != Side::Enemy || (c != UnitClass::Aircraft && c != UnitClass::Helicopter && c != UnitClass::Drone))
    return -1;
  const glm::vec3 nose = heli.orient * glm::vec3(0, 0, -1);
  const glm::vec3 to = v.pos - heli.pos;
  const float d = glm::length(to);
  if (d > r60LockRange(v, heli.pos) || d < 1.0f) return -1;
  return std::acos(std::clamp(glm::dot(to / d, nose), -1.0f, 1.0f)) < kR60Cone ? tracked : -1;
}

void Weapons::groundReload(const Helicopter& heli, float dt) {
  // Only with rounds fired: sitting on the pad before take-off does not use it up.
  const bool fired = yakB_ < kYakBRounds || (loadout_.doorGun[0] && doorRounds_[0] < kDoorGunRounds) || (loadout_.doorGun[1] && doorRounds_[1] < kDoorGunRounds);
  const bool still = fired && !heli.crashed && heli.weightOnWheels > 0.9f && glm::length(heli.vel) < 1.0f;
  grounded_ = still ? grounded_ + dt : 0.0f;
  if (reloadUsed_ || grounded_ < kReloadTime) return;
  reloadUsed_ = true;
  yakB_ = kYakBRounds;
  for (int side = 0; side < 2; ++side)
    if (loadout_.doorGun[side]) doorRounds_[side] = kDoorGunRounds, doorReload_[side] = 0.0f;
  ++reloads_;
}

float Weapons::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 23) - 1.0f;
}

void Weapons::load(const Loadout& l) {
  loadout_ = l;
  loadout_.settle();
  yakB_ = kYakBRounds;
  grounded_ = 0.0f;
  reloadUsed_ = false;
  r60Reload_ = 0.0f;
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    left_[p][0] = left_[p][1] = si.available ? si.rounds : 0;
  }
  for (int side = 0; side < 2; ++side) {
    doorRounds_[side] = l.doorGun[side] ? kDoorGunRounds : 0;
    doorReload_[side] = 0.0f;
    door_[side] = DoorGunner();
    doorAim_[side] = {side ? 1.0f : -1.0f, -0.1f, 0.0f};
  }
  shots.clear();
  armed_ = false;
  selected_ = WeaponType::YakB;
  fired_ = 0;
  cooldown_ = 0;
  turretAz_ = turretEl_ = turretTargetAz_ = turretTargetEl_ = 0;
}

int Weapons::rounds(WeaponType t) const {
  if (t == WeaponType::YakB) return yakB_;
  int n = 0;
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    if (si.available && si.rounds > 0 && si.weapon == t) n += left_[p][0] + left_[p][1];
  }
  return n;
}

float Weapons::storesMass() const {
  // The door guns with their gunners, the rounds as they go.
  float m = 0.0f;
  for (int side = 0; side < 2; ++side)
    if (loadout_.doorGun[side]) m += kDoorGunMass - float(kDoorGunRounds - doorRounds_[side]) * 0.025f;
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    for (int side = 0; side < 2; ++side) {
      if (!si.available || si.rounds <= 0) m += si.mass;
      else m += si.emptyMass + (si.mass - si.emptyMass) * float(left_[p][side]) / float(si.rounds);
    }
  }
  return m;
}

void Weapons::selectNext() {
  // Navigation mode, the gun, then the weapons of the pylons from the inside out, and round to
  // navigation mode again (as the original cycles them).
  std::vector<WeaponType> order{WeaponType::YakB};
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    if (si.available && si.rounds > 0 && std::find(order.begin(), order.end(), si.weapon) == order.end())
      order.push_back(si.weapon);
  }
  // A gunner aboard has his own; the interceptors are the crew's.
  if (gunner_.manned) std::erase_if(order, gunnersWeapon);
  std::erase(order, WeaponType::Interceptor);
  madeSafe_ = false;
  const size_t from = armed_ ? size_t(std::find(order.begin(), order.end(), selected_) - order.begin()) + 1 : 0;
  for (size_t i = from; i < order.size(); ++i)
    if (rounds(order[i]) > 0) {
      selected_ = order[i];
      armed_ = true;
      return;
    }
  armed_ = false;
}

bool Weapons::select(WeaponType t) {
  if (rounds(t) <= 0) return false;
  madeSafe_ = false;
  selected_ = t;
  armed_ = true;
  return true;
}

void Weapons::makeSafe() { armed_ = false, madeSafe_ = true; }

std::string Weapons::status() const {
  if (!armed_) return "NAV";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%s %d", weaponName(selected_), rounds(selected_));
  return buf;
}

glm::vec3 Weapons::turretDirBody() const {
  return {-std::sin(turretAz_) * std::cos(turretEl_), std::sin(turretEl_), -std::cos(turretAz_) * std::cos(turretEl_)};
}

void Weapons::spawn(WeaponType t, const Helicopter& heli, glm::vec3 muzzleBody, glm::vec3 dirBody, float speed,
                    float spread, bool tracer) {
  // Random direction within the dispersion cone.
  glm::vec3 side = glm::normalize(glm::cross(dirBody, std::fabs(dirBody.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
  glm::vec3 up = glm::cross(side, dirBody);
  glm::vec3 d = glm::normalize(dirBody + side * (random() * spread) + up * (random() * spread));
  Projectile p;
  p.type = t;
  p.pos = heli.pos + heli.orient * muzzleBody;
  p.vel = heli.vel + heli.orient * d * speed;
  p.carrier = heli.vel;
  if (t == WeaponType::S8 || t == WeaponType::S5) {
    // A rocket turns into the airflow off its rails and its motor drives it along the pod: it
    // keeps only the helicopter's speed along that axis, not its climb, sink or slip, and flies
    // where the pod points (as the original's do), not along the flight path above the nose.
    const glm::vec3 axis = heli.orient * d;
    p.vel = axis * (std::max(glm::dot(heli.vel, axis), 0.0f) + speed);
  }
  p.tracer = tracer;
  shots.push_back(p);
  if (fired.size() < 256) fired.push_back(t);
}

bool Weapons::bombImpact(const Helicopter& heli, glm::vec3& out) const {
  glm::vec3 pos = heli.pos + heli.orient * (pylonPoint(Pylon::Inner) * glm::vec3(0, 1, 1) + kBombRelease);
  glm::vec3 vel = heli.vel + heli.orient * glm::vec3(0, -kBomb.muzzle, 0);
  const float dt = 0.05f;
  for (float t = 0.0f; t < 60.0f; t += dt) {
    glm::vec3 old = pos;
    vel += (glm::vec3(0, -Helicopter::kGravity, 0) - vel * glm::length(vel) * kBomb.drag) * dt;
    pos += vel * dt;
    if (pos.y < terrain_.groundHeight(pos.x, pos.z)) {
      for (int i = 0; i < 8; ++i) {
        glm::vec3 m = (old + pos) * 0.5f;
        if (m.y < terrain_.groundHeight(m.x, m.z)) pos = m;
        else old = m;
      }
      out = pos;
      return true;
    }
  }
  return false;
}

float Weapons::missileTimeTo(glm::vec3 target) const {
  for (const Projectile& p : shots)
    if (p.type == WeaponType::Shturm) return glm::distance(p.pos, target) / std::max(glm::length(p.vel), 360.0f);
  return -1.0f;
}

bool Weapons::fire(Helicopter& heli, WeaponType t) {
  const Ballistics& b = ballistics(t);
  if (t == WeaponType::YakB) {
    glm::vec3 dir = turretDirBody();
    spawn(WeaponType::YakB, heli, kTurretPivot + dir * 1.05f, dir, b.muzzle, b.spread, fired_ % 4 == 0);
    if (!infiniteAmmo) --yakB_;
    ++fired_;
    return true;
  }
  // Left and right in turn, cycling through the pylons carrying the weapon; a pylon run empty
  // leaves the others to fire.
  std::vector<std::pair<int, int>> stations;
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    if (!si.available || si.rounds <= 0 || si.weapon != t) continue;
    for (int side = 0; side < 2; ++side)
      if (left_[p][side] > 0) stations.emplace_back(p, side);
  }
  if (stations.empty()) return false;
  if (t == WeaponType::Mine) {
    // A compartment of each dispenser of the pair at once.
    const int p = stations.front().first;
    const size_t heard = fired.size();
    for (int side = 0; side < 2; ++side) {
      if (left_[p][side] <= 0) continue;
      glm::vec3 at = pylonPoint(Pylon(p));
      if (side == 1) at.x = -at.x;
      for (int k = 0; k < kMinesPerCompartment; ++k) {
        spawn(WeaponType::Mine, heli, at + glm::vec3(0.0f, -0.3f, 0.0f), {0, -1, 0}, kMine.muzzle * (0.6f + 0.4f * random()),
              kMine.spread, false);
      }
      if (!infiniteAmmo) --left_[p][side];
    }
    fired.resize(heard);  // one sound for the lot
    fired.push_back(WeaponType::Mine);
    mineReleases.push_back({heli.pos, heli.vel, heli.radarAltitude()});
    ++fired_;
    return true;
  }
  // The other side from last time when it has any left; pairs of pods in turn.
  std::vector<std::pair<int, int>> side;
  for (const auto& s : stations)
    if (s.second == fired_ % 2) side.push_back(s);
  const auto& pick = side.empty() ? stations : side;
  const std::pair<int, int> st = pick[size_t(fired_ / 2) % pick.size()];
  glm::vec3 at = pylonPoint(Pylon(st.first));
  if (st.second == 1) at.x = -at.x;
  const bool tracer = t != WeaponType::Upk23 || fired_ % 3 == 0;
  if (isBomb(t)) spawn(t, heli, at + kBombRelease, {0, -1, 0}, b.muzzle, 0.0f, false);
  else if (t == WeaponType::Shturm) spawn(t, heli, at + glm::vec3(0, 0, kPodFrontZ), {0, 0, -1}, b.muzzle, 0.0f, true);
  else if (t == WeaponType::R60) {
    spawn(t, heli, at + glm::vec3(0, -0.2f, kPodFrontZ), {0, 0, -1}, b.muzzle, 0.0f, true);
    shots.back().target = targets_ ? r60Seek(heli, *targets_, r60Track) : -1;
  }
  else if (t == WeaponType::Upk23) spawn(t, heli, at + glm::vec3(0, 0, kUpkMuzzleZ), {0, 0, -1}, b.muzzle, b.spread, tracer);
  else spawn(t, heli, at + glm::vec3(0, 0, kPodFrontZ), {0, 0, -1}, b.muzzle, b.spread, true);
  if (!infiniteAmmo) --left_[st.first][st.second];
  ++fired_;
  return true;
}

void Weapons::killed(Helicopter& heli, const Targets& targets, int k, std::vector<Impact>& impacts) {
  const Vehicle& v = targets.list[size_t(k)];
  const VehicleType& t = vehicleType(v.kind);
  glm::vec3 centre = glm::vec3(v.toWorld() * glm::vec4((t.lo + t.hi) * 0.5f, 1.0f));
  // A man falls; anything else blows up and burns.
  if (t.cls == UnitClass::Soldier) {
    impacts.push_back({WeaponType::YakB, centre, k, false});
    heli.setMessage(t.name + " KILLED", 2.0f);
    return;
  }
  impacts.push_back({WeaponType::S8, centre, k, true});  // the explosion of the vehicle itself
  heli.setMessage(t.name + " DESTROYED", 3.0f);
}

void Weapons::interceptors(Helicopter& heli, float dt) {
  interceptWait_ = std::max(0.0f, interceptWait_ - dt);
  const int p = int(Pylon::Inner);
  if (!targets_ || !doorGunnersFree || interceptorsCrewed || interceptWait_ > 0.0f ||
      storeInfo(loadout_.inner).weapon != WeaponType::Interceptor ||
      left_[p][0] + left_[p][1] <= 0)
    return;
  // A gunner not guiding one already (the two in turn).
  int side = interceptSide_ ^ 1;
  if (guiding(side)) side ^= 1;
  if (guiding(side)) return;
  // The nearest jet drone in reach that no interceptor is after yet (nothing else catches it;
  // with interceptAny any drone).
  int best = -1;
  float bestDist = kInterceptorReach;
  for (size_t i = 0; i < targets_->list.size(); ++i) {
    const Vehicle& v = targets_->list[i];
    const bool kind = interceptAny ? vehicleType(v.kind).cls == UnitClass::Drone : isJetDrone(v.kind);
    if (v.destroyed || v.side != Side::Enemy || !kind || isCruiseMissile(v.kind) || !v.moving() || v.delay > 0.0f)
      continue;
    if (std::any_of(shots.begin(), shots.end(), [&](const Projectile& s) { return s.type == WeaponType::Interceptor && s.target == int(i); }))
      continue;
    if (const float d = glm::distance(v.pos, heli.pos); d < bestDist) bestDist = d, best = int(i);
  }
  if (best >= 0) crewLaunch(heli, best, side);
}

void Weapons::crewLaunch(Helicopter& heli, int target, int side) {
  const int p = int(Pylon::Inner);
  interceptSide_ = side;
  int rack = side;
  if (left_[p][rack] <= 0) rack ^= 1;
  glm::vec3 at = kInterceptorRackPoint;
  if (rack == 1) at.x = -at.x;
  spawn(WeaponType::Interceptor, heli, at, interceptorLaunchDir(rack), kInterceptor.muzzle, 0.0f, false);
  shots.back().target = target;
  shots.back().guide = uint8_t(side + 1);
  if (!infiniteAmmo) --left_[p][rack];
  interceptWait_ = 1.5f;
  heli.setMessage("INTERCEPTOR AWAY - " + std::to_string(left_[p][0] + left_[p][1]) + " LEFT", 2.0f);
}

bool Weapons::guiding(int side) const {
  return std::any_of(shots.begin(), shots.end(), [&](const Projectile& s) {
    return s.type == WeaponType::Interceptor && s.age < 1e9f && s.guide == side + 1;
  });
}

std::string Weapons::orderInterceptor(Helicopter& heli, int target) {
  const int p = int(Pylon::Inner);
  if (storeInfo(loadout_.inner).weapon != WeaponType::Interceptor) return "NO INTERCEPTORS LOADED";
  if (left_[p][0] + left_[p][1] <= 0) return "NO INTERCEPTORS LEFT";
  if (heli.crashed || heli.weightOnWheels > 0.0f) return "NOT IN THE AIR";
  if (interceptorsCrewed) return "THE DOOR GUNNER LAUNCHES THEM";
  int side = interceptSide_ ^ 1;
  if (guiding(side)) side ^= 1;
  if (guiding(side)) return "GUNNERS BUSY GUIDING";
  const auto drone = [&](int i) {
    const Vehicle& v = targets_->list[size_t(i)];
    return !v.destroyed && v.side == Side::Enemy && vehicleType(v.kind).cls == UnitClass::Drone && v.moving() && v.delay <= 0.0f;
  };
  // The drone the pilot has as his target, when it is one in reach; else the nearest enemy drone
  // in reach that none is after yet.
  int best = -1;
  if (targets_ && target >= 0 && target < int(targets_->list.size()) && drone(target)) {
    if (glm::distance(targets_->list[size_t(target)].pos, heli.pos) > kInterceptorReach) return "TARGET OUT OF REACH";
    best = target;
  }
  float bestDist = kInterceptorReach;
  for (size_t i = 0; best < 0 && targets_ && i < targets_->list.size(); ++i) {
    if (!drone(int(i))) continue;
    if (std::any_of(shots.begin(), shots.end(), [&](const Projectile& s) { return s.type == WeaponType::Interceptor && s.target == int(i); }))
      continue;
    if (const float d = glm::distance(targets_->list[i].pos, heli.pos); d < bestDist) bestDist = d, best = int(i);
  }
  if (best < 0) return "NO DRONE IN REACH";
  crewLaunch(heli, best, side);
  return "";
}

bool Weapons::launchInterceptor(Helicopter& heli, int owner) {
  const int p = int(Pylon::Inner);
  if (storeInfo(loadout_.inner).weapon != WeaponType::Interceptor || left_[p][0] + left_[p][1] <= 0 || interceptorOf(owner) ||
      heli.crashed || heli.weightOnWheels > 0.0f)
    return false;
  // Off the pylon on his side when it has any left (the gunner at the sight: the fuller one).
  int side = owner == 3 ? 1 : owner == 1 ? (left_[p][1] > left_[p][0] ? 1 : 0) : 0;
  if (left_[p][side] <= 0) side ^= 1;
  glm::vec3 at = kInterceptorRackPoint;
  if (side == 1) at.x = -at.x;
  spawn(WeaponType::Interceptor, heli, at, interceptorLaunchDir(side), kInterceptor.muzzle, 0.0f, false);
  Projectile& s = shots.back();
  s.owner = uint8_t(owner), s.manual = true, s.target = -1;
  if (!infiniteAmmo) --left_[p][side];
  heli.setMessage("INTERCEPTOR AWAY - " + std::to_string(left_[p][0] + left_[p][1]) + " LEFT", 2.0f);
  return true;
}

void Weapons::steerInterceptor(int owner, glm::vec2 steer) {
  for (Projectile& s : shots)
    if (s.type == WeaponType::Interceptor && s.manual && s.owner == owner) s.steer = glm::clamp(steer, glm::vec2(-1.0f), glm::vec2(1.0f));
}

void Weapons::releaseInterceptor(int owner) {
  for (Projectile& s : shots)
    if (s.type == WeaponType::Interceptor && s.manual && s.owner == owner) s.manual = false, s.steer = glm::vec2(0.0f);
}

bool Weapons::interceptorTarget(const Vehicle& v) {
  const UnitClass c = vehicleType(v.kind).cls;
  return !v.destroyed && !v.aboard && v.side == Side::Enemy && !isCruiseMissile(v.kind) &&
         (c == UnitClass::Drone || c == UnitClass::Aircraft || c == UnitClass::Helicopter);
}

bool Weapons::lockInterceptor(int owner, const Targets& targets, int target) {
  for (Projectile& s : shots)
    if (s.type == WeaponType::Interceptor && s.manual && s.owner == owner && s.age < 1e9f) {
      if (target < 0 || target >= int(targets.list.size())) return false;
      const Vehicle& v = targets.list[size_t(target)];
      if (!interceptorTarget(v) || glm::distance(v.pos, s.pos) > kInterceptorLock) return false;
      s.manual = false, s.locked = true, s.target = target, s.steer = glm::vec2(0.0f);
      return true;
    }
  return false;
}

const Projectile* Weapons::interceptorOf(int owner) const {
  for (const Projectile& s : shots)
    if (s.type == WeaponType::Interceptor && s.manual && s.owner == owner && s.age < 1e9f) return &s;
  return nullptr;
}

const Projectile* Weapons::interceptorWatched(int owner) const {
  if (const Projectile* s = interceptorOf(owner)) return s;
  for (const Projectile& s : shots)
    if (s.type == WeaponType::Interceptor && s.locked && s.owner == owner && s.age < 1e9f) return &s;
  return nullptr;
}

void Weapons::doorGuns(Helicopter& heli, float dt) {
  if (!targets_) return;
  for (int side = 0; side < 2; ++side) {
    DoorGunner& g = door_[side];
    const bool endless = infiniteAmmo && !doorReloads;
    if (doorReloads && loadout_.doorGun[side]) {
      if (doorRounds_[side] <= 0 && doorReload_[side] <= 0.0f) {
        doorReload_[side] = kDoorReload;
        heli.setMessage("RELOADING GSG", 2.0f);
      }
      if (doorReload_[side] > 0.0f) {
        doorReload_[side] -= dt;
        if (doorReload_[side] <= 0.0f) doorReload_[side] = 0.0f, doorRounds_[side] = kDoorGunRounds, ++reloads_, heli.setMessage("GSG LOADED", 2.0f);
        continue;
      }
    }
    if (doorRounds_[side] <= 0 && !endless) continue;
    const float sx = side == 0 ? -1.0f : 1.0f;
    const glm::vec3 pointBody = doorPivot * glm::vec3(sx, 1.0f, 1.0f);
    const glm::vec3 muzzle = heli.pos + heli.orient * pointBody;
    const glm::mat3 toBody = glm::transpose(glm::mat3(heli.orient));
    auto inField = [&](glm::vec3 world) { return doorField(side, toBody * glm::normalize(world - muzzle)); };
    g.cooldown = std::max(g.cooldown - dt, -dt);
    if (doorManned_[side]) {
      // A player has it.
      g.target = -1;
      const glm::vec3 dir = glm::length(doorDir_[side]) > 0.1f ? glm::normalize(doorDir_[side]) : doorAim_[side];
      doorAim_[side] = dir;
      while (doorTrigger_[side] && doorField(side, dir) && g.cooldown <= 0.0f && (doorRounds_[side] > 0 || endless)) {
        g.cooldown += kGshg.interval;
        spawn(WeaponType::DoorGun, heli, pointBody + dir * kDoorBarrel, dir, kGshg.muzzle, kGshg.spread, g.fired++ % 4 == 0);
        if (!endless) --doorRounds_[side];
      }
      continue;
    }
    // Guiding an interceptor: his hands are full.
    if (guiding(side)) {
      g.target = -1, g.burst = 0.0f;
      continue;
    }
    // Twice a second: the nearest enemy in his field and reach, a drone before anything else.
    g.look -= dt;
    if (g.look <= 0.0f) {
      g.look = 0.5f;
      g.target = -1;
      float best = 1e9f;
      for (size_t k = 0; k < targets_->list.size(); ++k) {
        const Vehicle& v = targets_->list[k];
        if (v.destroyed || v.aboard || v.scenery || v.side != Side::Enemy) continue;
        const glm::vec3 c = v.pos + glm::vec3(0.0f, 1.0f, 0.0f);
        float d = glm::distance(c, muzzle);
        if (d > kDoorGunReach || !inField(c)) continue;
        if (vehicleType(v.kind).cls != UnitClass::Drone) d += 2000.0f;
        if (d < best) best = d, g.target = int(k);
      }
    }
    if (g.target < 0 || !doorGunnersFree) {
      // Nothing to fire at: he brings the gun back to point out of the window.
      const glm::vec3 rest{sx, -0.1f, 0.0f};
      doorAim_[side] = glm::normalize(glm::mix(doorAim_[side], glm::normalize(rest), std::min(1.0f, dt * 0.8f)));
      continue;
    }
    const Vehicle& v = targets_->list[size_t(g.target)];
    const UnitClass cls = vehicleType(v.kind).cls;
    const glm::vec3 c = v.pos + glm::vec3(0.0f, cls == UnitClass::Drone ? 0.1f : 1.0f, 0.0f);
    if (v.destroyed || !inField(c)) {
      g.target = -1;
      continue;
    }
    // Something in the air is hit only from close by, flying alongside it at its speed and
    // shooting down at it (the crews hunting the Shaheds fly parallel above them): how good the
    // shot is, 0..1. He holds his fire below kDoorOpenFire; above it he waits while the shot keeps
    // getting better (the pilot closing in), and fires once it stops getting better or gets
    // worse. Having opened fire he keeps on. His aim is the worse the poorer the shot. At things
    // on the ground he fires as before.
    float q = 1.0f;
    bool hold = false;
    const glm::vec3 lead = gunAim(muzzle, heli.vel, c, v.vel, nullptr, WeaponType::DoorGun);
    if (cls == UnitClass::Drone || cls == UnitClass::Aircraft || cls == UnitClass::Helicopter) {
      const glm::vec3 rel = c - muzzle, rv = v.vel - heli.vel;
      q = doorShot(rel, rv);
      if (g.watched != g.target) g.watched = g.target, g.lastShot = q, g.gaining = 0.0f, g.engaged = false;
      if (dt > 0.0f) g.gaining += ((q - g.lastShot) / dt - g.gaining) * std::min(1.0f, dt * 1.0f);
      g.lastShot = q;
      if (q < kDoorOpenFire) g.engaged = false;
      hold = q < kDoorOpenFire || (!g.engaged && g.gaining > kDoorGaining && q < 0.8f);
      if (!hold) g.engaged = true;
    }
    // Not through another of our Hinds.
    for (const OtherHind& o : otherHinds) {
      if (o.heli->crashed) continue;
      const glm::vec3 to = o.heli->pos - muzzle;
      const float d = glm::length(to);
      if (d < glm::distance(c, muzzle) + 20.0f && glm::dot(to, glm::normalize(lead)) > d * std::cos(std::max(0.05f, std::atan(12.0f / std::max(d, 1.0f))))) hold = true;
    }
    if (hold) {
      // Following it with the gun, the finger off the trigger.
      doorAim_[side] = glm::normalize(glm::mix(doorAim_[side], toBody * lead, std::min(1.0f, dt * 4.0f)));
      g.burst = 0.0f;
      g.pause = std::max(g.pause, 0.15f);
      continue;
    }
    if (g.pause > 0.0f) {
      g.pause -= dt;
      if (g.pause <= 0.0f) {
        // A new burst, short; his aim is off, the more so the poorer the shot and further away.
        g.burst = 0.5f + 0.5f * (random() * 0.5f + 0.5f);
        const float sigma = 0.003f + 0.025f * (1.0f - q) + 0.003f * glm::distance(c, muzzle) / kDoorGunReach;
        g.error = glm::vec2(random(), random()) * sigma;
      }
      continue;
    }
    g.burst -= dt;
    if (g.burst <= 0.0f) {
      g.pause = 0.6f + 0.6f * (random() * 0.5f + 0.5f);
      continue;
    }
    g.error *= std::exp(-dt * q / 1.2f);  // walking the tracers onto it, as far as he can see them
    while (g.cooldown <= 0.0f && (doorRounds_[side] > 0 || endless)) {
      g.cooldown += kGshg.interval;
      glm::vec3 dir = lead;
      const glm::vec3 across = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
      dir = glm::normalize(dir + across * g.error.x + glm::cross(across, dir) * g.error.y);
      spawn(WeaponType::DoorGun, heli, pointBody + toBody * dir * kDoorBarrel, toBody * dir, kGshg.muzzle, kGshg.spread, g.fired++ % 4 == 0);
      if (!endless) --doorRounds_[side];
      doorAim_[side] = toBody * dir;
    }
  }
}

Weapons::DoorShotParts Weapons::doorShotParts(glm::vec3 rel, glm::vec3 relVel) {
  // Near: as good as it gets inside 120 m, nothing beyond 450 m. Its speed near enough: 20 m/s
  // (70 km/h) apart still good, 50 m/s (180 km/h) too much (matching a drone's speed while
  // holding the height is hard enough). Below: at least 5 m, best 25 m and more.
  const float d = glm::length(rel);
  const float v = glm::length(relVel) / 45.0f;
  return {std::clamp((450.0f - d) / 330.0f, 0.0f, 1.0f), std::exp(-v * v), std::clamp((-rel.y - 5.0f) / 20.0f, 0.0f, 1.0f)};
}

float Weapons::doorShot(glm::vec3 rel, glm::vec3 relVel) {
  const DoorShotParts p = doorShotParts(rel, relVel);
  return p.near * p.matched * p.below;
}

bool Weapons::doorField(int side, glm::vec3 b) {
  b = glm::normalize(b);
  const float out = b.x * (side == 0 ? -1.0f : 1.0f);
  if (out <= 0.0f) return false;
  const float az = std::atan2(-b.z, out), el = std::asin(std::clamp(b.y, -1.0f, 1.0f));
  return std::fabs(az) < 1.22f && el > -1.22f && el < 0.52f;
}

void Weapons::pull(Helicopter& heli, WeaponType t, bool armed, bool trigger, bool& was, float& cooldown, float dt) {
  bool pressed = trigger && !was;
  was = trigger;
  cooldown = std::max(cooldown - dt, -dt);
  if (!trigger || !armed || heli.crashed) return;
  if (t == WeaponType::YakB && !heli.works(System::Gun)) {
    if (pressed) heli.setMessage(systemFailure(System::Gun), 2.0f);
  } else if (heli.weightOnWheels > 0.0f) {
    if (pressed) heli.setMessage("WEAPONS INHIBITED ON GROUND", 2.0f);
  } else if (rounds(t) <= 0) {
    if (pressed) heli.setMessage(std::string(weaponName(t)) + " EMPTY", 2.0f);
  } else if (isBomb(t) || t == WeaponType::Shturm || t == WeaponType::Mine || t == WeaponType::R60) {
    // One bomb or missile (or mine compartment) per press, left and right in turn.
    if (pressed) fire(heli, t);
  } else if (t == WeaponType::S8 || t == WeaponType::S5) {
    // Rockets leave in pairs, left and right; holding the button ripples them.
    if (cooldown <= 0.0f) {
      fire(heli, t);
      if (rounds(t) > 0) fire(heli, t);
      cooldown = ballistics(t).interval;
    }
  } else {
    while (cooldown <= 0.0f && rounds(t) > 0 && fire(heli, t)) cooldown += ballistics(t).interval;
  }
}

std::vector<WeaponType> Weapons::gunnerChoices() const {
  std::vector<WeaponType> order{WeaponType::YakB, WeaponType::Shturm};
  for (int p = 0; p < kPylons; ++p) {
    const StoreInfo& si = storeInfo(loadout_.at(Pylon(p)));
    if (si.available && si.rounds > 0 && si.weapon != WeaponType::Interceptor &&
        std::find(order.begin(), order.end(), si.weapon) == order.end())
      order.push_back(si.weapon);
  }
  std::erase_if(order, [&](WeaponType t) { return rounds(t) <= 0; });
  return order;
}

void Weapons::setGunner(bool manned, bool armed, WeaponType weapon, bool trigger) {
  if (manned && !gunner_.manned && armed_ && gunnersWeapon(selected_)) armed_ = false;
  gunner_.manned = manned;
  gunner_.armed = manned && armed && weapon != WeaponType::Interceptor && weapon != WeaponType::DoorGun;
  gunner_.weapon = weapon;
  gunner_.trigger = manned && trigger;
}

void Weapons::step(Helicopter& heli, bool trigger, float dt, std::vector<Impact>& impacts, Targets* targets) {
  // Turret slews towards its target direction within its limits.
  float az = std::clamp(turretTargetAz_, -kTurretAzLimit, kTurretAzLimit);
  float el = std::clamp(turretTargetEl_, kTurretElDown, kTurretElUp);
  turretSlewing_ = std::max(std::fabs(az - turretAz_), std::fabs(el - turretEl_)) > kTurretSlewSound;
  turretAz_ += std::clamp(az - turretAz_, -kTurretRate * dt, kTurretRate * dt);
  turretEl_ += std::clamp(el - turretEl_, -kTurretRate * dt, kTurretRate * dt);

  targets_ = targets;
  // With a gunner aboard the turret gun and the 9M114s are his to fire.
  pull(heli, selected_, armed_, trigger && !(gunner_.manned && gunnersWeapon(selected_)), triggerWas_, cooldown_, dt);
  if (gunner_.manned) pull(heli, gunner_.weapon, gunner_.armed, gunner_.trigger, gunner_.was, gunner_.cooldown, dt);

  if (!heli.crashed && heli.weightOnWheels <= 0.0f) doorGuns(heli, dt), interceptors(heli, dt);
  if (r60Reloads && !heli.crashed && loadout_.outer == Store::R60 && pylonRounds(Pylon::Outer) == 0) {
    if (r60Reload_ <= 0.0f) {
      r60Reload_ = kR60Reload;
      heli.setMessage("RELOADING R-60", 2.0f);
    } else if ((r60Reload_ -= dt) <= 0.0f) {
      const int p = int(Pylon::Outer);
      left_[p][0] = left_[p][1] = storeInfo(Store::R60).rounds;
      r60Reload_ = 0.0f;
      ++reloads_;
      heli.setMessage("R-60 LOADED", 2.0f);
    }
  }

  // Fly the projectiles.
  for (Projectile& p : shots) {
    const Ballistics& b = ballistics(p.type);
    glm::vec3 old = p.pos;
    float speed = glm::length(p.vel);
    glm::vec3 acc{0.0f, -Helicopter::kGravity, 0.0f};
    if (p.type == WeaponType::Shturm) {
      // Speed by the motor's program; the commands turn it towards a point on the line of
      // sight a little ahead of it, which brings it onto the line and keeps it there.
      const float want = p.age < kShturmBurn ? kShturm.muzzle + (kShturmTop - kShturm.muzzle) * p.age / kShturmBurn
                                             : kShturmTop - (kShturmTop - kShturmEnd) * std::min(p.age - kShturmBurn, 13.0f) / 13.0f;
      glm::vec3 dir = speed > 1.0f ? p.vel / speed : glm::vec3(0, 0, -1);
      if (guided_ && glm::length(losDir_) > 0.5f) {
        const glm::vec3 d = glm::normalize(losDir_);
        const float along = glm::dot(p.pos - losEye_, d);
        const glm::vec3 aim = losEye_ + d * (std::max(along, 0.0f) + std::max(want, 100.0f) * 0.6f);
        const glm::vec3 to = glm::normalize(aim - p.pos);
        const float angle = std::acos(std::clamp(glm::dot(dir, to), -1.0f, 1.0f));
        const float turn = std::min(angle, kShturmTurn / std::max(speed, 50.0f) * dt);
        if (angle > 1e-5f) {
          const glm::vec3 axis = glm::cross(dir, to);
          if (glm::length(axis) > 1e-6f) dir = glm::normalize(glm::angleAxis(turn, glm::normalize(axis)) * dir);
        }
      }
      p.vel = dir * want;
      p.pos += p.vel * dt;
      p.age += dt;
      if (glm::distance(p.pos, losEye_) > kShturmRange && guided_) p.age = std::max(p.age, b.life);
    } else if (p.type == WeaponType::Interceptor && p.manual) {
      // Flown by a player: up to its speed, turning as he steers it; it goes off by whatever
      // enemy flier it passes close to.
      glm::vec3 dir = speed > 1.0f ? p.vel / speed : heli.forward();
      const glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)) + glm::vec3(1e-5f, 0.0f, 0.0f));
      if (p.age < kInterceptorClear) {
        dir = interceptorPeel(dir, p.pos, heli.pos, dt);
      } else {
        p.turning += (p.steer - p.turning) * std::min(1.0f, dt / kInterceptorRoll);
        const float rate = std::min(kInterceptorSteerRate, interceptorPull(speed) / std::max(speed, 1.0f));
        dir = glm::angleAxis(p.turning.x * rate * dt, glm::vec3(0, 1, 0)) * (glm::angleAxis(p.turning.y * rate * dt, right) * dir);
      }
      if (dir.y > 0.95f) dir = glm::normalize(glm::vec3(dir.x, 0.95f, dir.z));
      p.vel = glm::normalize(dir) * std::min(kInterceptorSpeed, speed + 40.0f * dt);
      p.pos += p.vel * dt;
      p.age += dt;
      int near = -1;
      float nearest = kInterceptorManualFuse;
      if (targets)
        for (size_t i = 0; i < targets->list.size(); ++i) {
          const Vehicle& v = targets->list[i];
          if (!interceptorTarget(v)) continue;
          if (const float d = glm::distance(v.pos, p.pos); d < nearest) nearest = d, near = int(i);
        }
      if (near >= 0) {
        impacts.push_back({p.type, p.pos, near});
        for (int k : targets->damage(near, p.type, p.pos, killer, killerName)) killed(heli, *targets, k, impacts);
        p.age = 1e9f;
        continue;
      }
      // Into our own helicopter (armed once clear of it): the fuselage or the rotor disc.
      if (p.age > kInterceptorClear && !heli.crashed) {
        const glm::vec3 at = glm::inverse(heli.orient) * (p.pos - heli.pos);
        const bool body = glm::all(glm::lessThan(glm::abs(at - glm::vec3(0.0f, 0.0f, 0.5f)), glm::vec3(1.6f, 1.8f, 9.5f)));
        const bool rotor = std::fabs(at.y - 2.2f) < 0.6f && glm::length(glm::vec2(at.x, at.z)) < 8.6f;
        if (body || rotor) {
          // Into the rotor, or its fragments up into the blades: the end. Else the tail rotor
          // gone, or the engine on that side (one engine left: a landing, just about).
          impacts.push_back({p.type, p.pos});
          using Part = Helicopter::HitPart;
          if (!heli.damage.shotDown) heli.damage.lastHitBy = "OUR OWN INTERCEPTOR DRONE";
          if (rotor || 0.5f + 0.5f * random() < kInterceptorSelfRotor) {
            heli.hit(Part::Airframe, heli.damage.airframe);
          } else {
            heli.hit(at.z > 8.0f ? Part::TailRotor : at.x < 0.0f ? Part::Engine1 : Part::Engine2, 100.0f);
          }
          p.age = 1e9f;
          continue;
        }
      }
    } else if (p.type == WeaponType::Interceptor) {
      // Up to its speed and on to its drone (or whatever flier its player locked it on); when
      // that is gone, another enemy drone near it.
      const Vehicle* t = targets && p.target >= 0 ? &targets->list[size_t(p.target)] : nullptr;
      if (t && (t->destroyed || t->aboard)) t = nullptr, p.target = -1, p.locked = false;
      if (!t && targets) {
        float best = 2000.0f;
        for (size_t i = 0; i < targets->list.size(); ++i) {
          const Vehicle& v = targets->list[i];
          if (v.destroyed || v.side != Side::Enemy || vehicleType(v.kind).cls != UnitClass::Drone || !v.moving() || v.delay > 0.0f)
            continue;
          if (const float d = glm::distance(v.pos, p.pos); d < best) best = d, p.target = int(i), t = &v;
        }
      }
      glm::vec3 dir = speed > 1.0f ? p.vel / speed : heli.orient * glm::vec3(0, 0, -1);
      const float want = std::min(kInterceptorSpeed, speed + 40.0f * dt);
      glm::vec3 turn{0.0f};
      if (p.age < kInterceptorClear) {
        dir = interceptorPeel(dir, p.pos, heli.pos, dt);
      } else if (t) {
        const glm::vec3 r = t->pos - p.pos, vr = t->vel - p.vel;
        const float r2 = std::max(glm::dot(r, r), 1.0f);
        turn = 3.5f * glm::cross(glm::cross(r, vr) / r2, p.vel);
        // Before it is up to speed (and when the drone is behind it) straight at it.
        if (glm::dot(r, dir) < 0.0f || speed < 40.0f) turn = (glm::normalize(r) - dir) * kInterceptorTurn;
        turn -= dir * glm::dot(turn, dir);
        if (const float pull = interceptorPull(speed); glm::length(turn) > pull) turn *= pull / glm::length(turn);
      }
      p.vel = dir * want + turn * dt;
      p.vel = glm::normalize(p.vel) * want;
      p.pos += p.vel * dt;
      p.age += dt;
      // On its own once near its drone (or with none to guide it onto).
      if (p.guide && (!t || glm::distance(p.pos, t->pos) < kInterceptorLock)) p.guide = 0;
      if (t && glm::distance(p.pos, t->pos) < kInterceptorFuse) {
        impacts.push_back({p.type, p.pos, p.target});
        for (int k : targets->damage(p.target, p.type, p.pos, killer, killerName)) killed(heli, *targets, k, impacts);
        p.age = 1e9f;
        continue;
      }
    } else if (p.type == WeaponType::R60) {
      if (p.age < kR60Burn && speed > 1.0f) acc += p.vel / speed * kR60Thrust;
      acc -= p.vel * speed * b.drag;
      const Vehicle* t = targets && p.target >= 0 ? &targets->list[size_t(p.target)] : nullptr;
      if (t && (t->destroyed || t->aboard)) t = nullptr, p.target = -1;
      if (t && speed > 1.0f) {
        // Proportional navigation: turn at N times the line of sight's rate, across the flight.
        const glm::vec3 r = t->pos - p.pos, vr = t->vel - p.vel;
        const float r2 = std::max(glm::dot(r, r), 1.0f);
        const glm::vec3 omega = glm::cross(r, vr) / r2;
        glm::vec3 turn = 4.0f * glm::cross(omega, p.vel);
        turn -= p.vel / speed * glm::dot(turn, p.vel / speed);
        if (glm::length(turn) > kR60Turn) turn *= kR60Turn / glm::length(turn);
        acc += turn;
        // Gravity is the motor's and the fins' to carry, not a drop off the line.
        acc.y += Helicopter::kGravity;
      }
      p.vel += acc * dt;
      p.pos += p.vel * dt;
      p.age += dt;
      if (t && glm::distance(p.pos, t->pos) < kR60Fuse) {
        impacts.push_back({p.type, p.pos, p.target});
        for (int k : targets->damage(p.target, p.type, p.pos, killer, killerName)) killed(heli, *targets, k, impacts);
        p.age = 1e9f;
        continue;
      }
    } else {
      if (p.type == WeaponType::S8 && p.age < kS8Burn && speed > 1.0f) acc += p.vel / speed * kS8Thrust;
      else if (p.type == WeaponType::S5 && p.age < kS5Burn && speed > 1.0f) acc += p.vel / speed * kS5Thrust;
      else acc -= p.vel * speed * b.drag;
      p.vel += acc * dt;
      p.pos += p.vel * dt;
      p.age += dt;
    }
    glm::vec3 hit;
    int target = targets && p.type != WeaponType::Mine ? targets->raycast(old, p.pos, hit) : -1;
    // Into another of our Hinds (before anything further along).
    if (p.type != WeaponType::Mine) {
      float nearest = target >= 0 ? glm::distance(old, hit) : 1e9f;
      int8_t struck = -1;
      glm::vec3 where{0.0f};
      for (const OtherHind& o : otherHinds) {
        glm::vec3 body;
        if (o.heli->crashed || !hullHit(*o.heli, old, p.pos, p.type == WeaponType::Interceptor ? 1.0f : 0.0f, body)) continue;
        const glm::vec3 at = o.heli->pos + o.heli->orient * body;
        if (const float d = glm::distance(old, at); d < nearest) nearest = d, struck = o.id, where = at;
      }
      if (struck >= 0) {
        Impact i{p.type, where};
        i.hind = struck;
        impacts.push_back(i);
        p.age = 1e9f;
        continue;
      }
    }
    if (target >= 0) {
      impacts.push_back({p.type, hit, target});
      for (int k : targets->damage(target, p.type, hit, killer, killerName)) killed(heli, *targets, k, impacts);
      p.age = 1e9f;
    } else if (p.pos.y < terrain_.groundHeight(p.pos.x, p.pos.z)) {
      // Find where the segment crossed the ground.
      glm::vec3 a = old, c = p.pos;
      for (int i = 0; i < 10; ++i) {
        glm::vec3 m = (a + c) * 0.5f;
        if (m.y < terrain_.groundHeight(m.x, m.z)) c = m;
        else a = m;
      }
      impacts.push_back({p.type, c});
      if (targets && p.type != WeaponType::YakB && p.type != WeaponType::Upk23 && p.type != WeaponType::Mine)
        for (int k : targets->damage(-1, p.type, c, killer, killerName)) killed(heli, *targets, k, impacts);
      p.age = 1e9f;
    } else if (p.age > b.life) {
      p.age = 1e9f;
    }
  }
  shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& p) { return p.age >= 1e9f; }),
              shots.end());
}

}  // namespace sim
