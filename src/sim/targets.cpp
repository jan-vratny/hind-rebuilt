#include "sim/targets.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "sim/weapons.hpp"

namespace sim {

namespace {

bool isAircraft(VehicleKind k) {
  UnitClass c = vehicleType(k).cls;
  return c == UnitClass::Aircraft || c == UnitClass::Helicopter || c == UnitClass::Drone;
}

// Hit boxes fitted to the original models (vehicle axes: x right, y up, nose to -z).
std::vector<VehicleType>& types() {
  static std::vector<VehicleType> t = {
      {"BRDM-2", "BRDM_2_APC", Armour::Light, 12.0f, {-1.15f, 0.0f, -2.95f}, {1.15f, 2.1f, 2.95f},
       UnitClass::Vehicle, 0, 0.0f, -1, Arms::Hmg},
      {"BMP-2", "BMP2_APC", Armour::Light, 18.0f, {-1.65f, 0.0f, -3.35f}, {1.65f, 2.2f, 3.35f},
       UnitClass::Vehicle, 0, 0.0f, -1, Arms::Hmg},
      {"ZSU-23-4", "ZSU23_Shilka_AAA", Armour::Light, 12.0f, {-1.55f, 0.0f, -3.35f}, {1.55f, 2.5f, 3.4f},
       UnitClass::Vehicle, 3, 8000.0f, -1, Arms::Aaa},
      {"SA-9", "SA_9_Gaskin_SP_SAM", Armour::Light, 12.0f, {-1.15f, 0.0f, -2.95f}, {1.15f, 2.9f, 2.95f},
       UnitClass::Vehicle, 0, 0.0f, -1, Arms::Sam},
      {"SA-13", "SA13_Strela_Mobile_SAM", Armour::Light, 14.0f, {-1.5f, 0.0f, -3.35f}, {1.5f, 3.0f, 3.35f},
       UnitClass::Vehicle, 4, 10000.0f, -1, Arms::Sam},
      {"T-62", "T62_MBT", Armour::Heavy, 30.0f, {-1.65f, 0.0f, -3.35f}, {1.65f, 2.3f, 3.35f},
       UnitClass::Vehicle, 0, 0.0f, -1, Arms::TankGun},
      {"FUEL TRUCK", "Fuel_Bowser_Small", Armour::Soft, 4.0f, {-1.25f, 0.0f, -3.9f}, {1.25f, 2.9f, 3.9f}},
      // Not in the original: drawn by render::buildShahed. Thin skin of composite: a few
      // 12.7 mm hits bring it down.
      {"SHAHED-136", "Shahed_136", Armour::Soft, 2.5f, {-1.25f, -0.35f, -1.75f}, {1.25f, 0.55f, 1.75f}, UnitClass::Drone},
      // Its decoy, the Gerbera: the same shape in foam and plywood, lighter still.
      {"GERBERA DECOY", "Gerbera", Armour::Soft, 1.5f, {-1.25f, -0.35f, -1.75f}, {1.25f, 0.55f, 1.75f}, UnitClass::Drone},
      // The jet Shahed (render::buildGeran3): the same airframe with a turbojet in the tail,
      // a little sturdier for the engine.
      {"GERAN-3", "Geran_3", Armour::Soft, 3.0f, {-1.25f, -0.35f, -1.75f}, {1.25f, 0.55f, 1.95f}, UnitClass::Drone},
      // The S8000 Banderol cruise missile (render::buildBanderol): 5 m long, its wings swept
      // back; as frail as a Shahed, but some 500 km/h.
      {"BANDEROL", "Banderol", Armour::Soft, 2.5f, {-0.85f, -0.3f, -2.5f}, {0.85f, 0.55f, 2.55f}, UnitClass::Drone},
  };
  return t;
}

// Turn rate of units following a route, rad/s; aircraft climb at most this fast, m/s.
constexpr float kTurnRate = 0.6f, kClimbRate = 5.0f;
// The drones: each its own a little (from where it was launched), as no two fly alike: its
// speed, a slow weave either side of its course and its height wandering up and down; at the
// end the dive onto the target, steeply (35 degrees), from kDiveStart out.
constexpr float kDiveStart = 1500.0f, kDiveSlope = 0.7f, kDiveRate = 40.0f;
float droneTrait(const Vehicle& v, float salt) {
  const glm::vec2 p = v.route.empty() ? glm::vec2(v.pos.x, v.pos.z) : v.route[0].pos;
  const float s = std::sin(p.x * 12.9898f + p.y * 78.233f + salt * 37.719f) * 43758.5453f;
  return 2.0f * (s - std::floor(s)) - 1.0f;  // -1..1
}
// Jets in the air no slower than this: the missions have them on a helicopter's pace (a Su-25
// on 200 km/h, under its stall speed); on the way down to land, the approach speed. m/s.
constexpr float kJetSpeed = 125.0f, kJetApproach = 70.0f;
// A drone shot down: it spins so many turns a second about its length, the air slows its way
// along (per second) and it falls no faster than this, m/s.
constexpr float kDroneWreckSpin = 1.0f, kDroneWreckDrag = 0.25f, kDroneWreckSink = 70.0f;

// Damage of one round hitting by armour class (soft, light, heavy). A 12.7 mm bullet cannot
// hurt a tank.
float hitDamage(WeaponType w, Armour a) {
  static const float table[][3] = {
      {1.0f, 0.6f, 0.0f},     // Yak-B 12.7 mm
      {3.0f, 2.5f, 0.15f},    // GSh-23L 23 mm
      {40.0f, 30.0f, 12.0f},  // S-8
      {25.0f, 15.0f, 4.0f},   // S-5
      {200.0f, 150.0f, 80.0f},   // FAB-250
      {300.0f, 250.0f, 150.0f},  // FAB-500
      {200.0f, 150.0f, 60.0f},   // OFAB-250
      {200.0f, 150.0f, 60.0f},   // ZAB-500
      {60.0f, 60.0f, 45.0f},     // 9M114: tandem shaped charge, through any tank's armour
      {0.0f, 0.0f, 0.0f},        // mines lying (they go off under a unit: Targets::step)
      {30.0f, 25.0f, 4.0f},      // R-60: its rods slice an aircraft apart
      {0.5f, 0.15f, 0.0f},       // GShG-7.62 of a door gunner
      {12.0f, 12.0f, 0.5f},      // an interceptor drone's charge: a drone, or a Hip, goes down
  };
  return table[int(w)][int(a)];
}

// Blast and fragments: full effect at the burst, none beyond the radius.
struct Blast {
  float radius;
  float full[3];  // soft, light, heavy armour
};
const Blast* blast(WeaponType w) {
  static const Blast s8{10.0f, {20.0f, 10.0f, 2.0f}}, s5{7.0f, {14.0f, 6.0f, 1.0f}};
  static const Blast fab250{30.0f, {250.0f, 120.0f, 60.0f}}, fab500{42.0f, {350.0f, 200.0f, 100.0f}};
  // Fragmentation: further against soft targets, less against armour; fire: wide, weak on armour.
  static const Blast ofab250{40.0f, {300.0f, 120.0f, 30.0f}}, zab500{50.0f, {200.0f, 60.0f, 12.0f}};
  static const Blast shturm{5.0f, {20.0f, 8.0f, 1.0f}};
  switch (w) {
    case WeaponType::S8: return &s8;
    case WeaponType::S5: return &s5;
    case WeaponType::Fab250: return &fab250;
    case WeaponType::Fab500: return &fab500;
    case WeaponType::Ofab250: return &ofab250;
    case WeaponType::Zab500: return &zab500;
    case WeaponType::Shturm: return &shturm;
    default: return nullptr;
  }
}

}  // namespace

const VehicleType& vehicleType(VehicleKind k) { return types()[size_t(k)]; }

int vehicleTypeCount() { return int(types().size()); }

VehicleKind findVehicleType(const std::string& original) {
  const auto& t = types();
  for (size_t k = 0; k < t.size(); ++k)
    if (t[k].original == original) return VehicleKind(k);
  return VehicleKind::Count;
}

VehicleKind registerVehicleType(const VehicleType& type) {
  VehicleKind k = findVehicleType(type.original);
  if (k != VehicleKind::Count && !type.original.empty()) return k;
  types().push_back(type);
  return VehicleKind(types().size() - 1);
}

glm::mat4 Vehicle::toWorld() const {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), pos) * glm::rotate(glm::mat4(1.0f), -heading, glm::vec3(0, 1, 0));
  if (fallingDrone()) {
    // Shot down, on its way to the ground: the nose along its path, spinning about its length.
    const float pitch = std::atan2(vel.y, std::max(glm::length(glm::vec2(vel.x, vel.z)), 0.01f));
    m = m * glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0)) *
        glm::rotate(glm::mat4(1.0f), sinceDestroyed * kDroneWreckSpin * 2.0f * glm::pi<float>(), glm::vec3(0, 0, 1));
  }
  return m;
}

bool Vehicle::fallingDrone() const {
  return destroyed && glm::length(vel) > 0.5f && vehicleType(kind).cls == UnitClass::Drone;
}

int Targets::add(VehicleKind kind, float x, float z, float headingRad) {
  Vehicle v;
  v.kind = kind;
  v.heading = headingRad;
  v.health = vehicleType(kind).health;
  // Rest on the highest ground under the hull so it does not sink into a slope.
  v.pos = {x, terrain_.groundHeight(x, z), z};
  const VehicleType& t = vehicleType(kind);
  glm::mat4 m = v.toWorld();
  for (float cx : {t.lo.x, t.hi.x})
    for (float cz : {t.lo.z, t.hi.z}) {
      glm::vec3 c = glm::vec3(m * glm::vec4(cx, 0.0f, cz, 1.0f));
      v.pos.y = std::max(v.pos.y, terrain_.groundHeight(c.x, c.z));
    }
  list.push_back(v);
  return int(list.size()) - 1;
}

void Targets::placeExercise() {
  // A convoy driving east 2.5 km north of the runway, with its air defence alongside.
  const glm::vec2 convoy{150.0f, -2600.0f};
  const VehicleKind column[] = {VehicleKind::Brdm2, VehicleKind::Bmp2, VehicleKind::FuelTruck, VehicleKind::Bmp2,
                                VehicleKind::FuelTruck, VehicleKind::Brdm2};
  for (size_t i = 0; i < std::size(column); ++i)
    add(column[i], convoy.x - 28.0f * float(i), convoy.y + 3.0f * std::sin(float(i)), glm::radians(90.0f));
  add(VehicleKind::Zsu23, convoy.x - 60.0f, convoy.y + 70.0f, glm::radians(150.0f));
  add(VehicleKind::Sa9, convoy.x - 110.0f, convoy.y - 60.0f, glm::radians(30.0f));
  // A tank platoon further north-east facing the airfield, covered by an SA-13.
  const glm::vec2 tanks{700.0f, -3300.0f};
  for (int i = 0; i < 3; ++i) add(VehicleKind::T62, tanks.x + 35.0f * i, tanks.y + 8.0f * (i % 2), glm::radians(185.0f));
  add(VehicleKind::Sa13, tanks.x + 40.0f, tanks.y - 90.0f, glm::radians(200.0f));
}

int Targets::airCollision(const std::vector<glm::vec3>& points) const {
  for (size_t i = 0; i < list.size(); ++i) {
    const Vehicle& v = list[i];
    const VehicleType& t = vehicleType(v.kind);
    const bool flier = t.cls == UnitClass::Helicopter || t.cls == UnitClass::Aircraft || t.cls == UnitClass::Drone;
    if (!flier || v.destroyed || v.aboard || v.pos.y < terrain_.groundHeight(v.pos.x, v.pos.z) + 2.0f) continue;
    // A helicopter's blades reach out over its box; the box of the others is near enough.
    const glm::vec3 pad{t.cls == UnitClass::Helicopter ? 3.0f : 0.5f};
    const glm::vec3 lo = t.lo - pad, hi = t.hi + pad;
    if (glm::distance(points.front(), v.pos) > glm::length(hi - lo) + 25.0f) continue;
    const glm::mat4 inv = glm::inverse(v.toWorld());
    for (const glm::vec3& p : points) {
      const glm::vec3 l{inv * glm::vec4(p, 1.0f)};
      if (glm::all(glm::greaterThanEqual(l, lo)) && glm::all(glm::lessThanEqual(l, hi))) return int(i);
    }
  }
  return -1;
}

int Targets::raycast(glm::vec3 a, glm::vec3 b, glm::vec3& hit) const {
  int best = -1;
  float bestT = 2.0f;
  for (size_t i = 0; i < list.size(); ++i) {
    const Vehicle& v = list[i];
    if (v.aboard) continue;
    const VehicleType& t = vehicleType(v.kind);
    glm::vec3 mid = (a + b) * 0.5f;
    float reach = glm::length(b - a) * 0.5f + glm::length(t.hi - t.lo);
    if (glm::distance(mid, v.pos) > reach) continue;
    // Slab test in vehicle axes.
    glm::mat4 inv = glm::inverse(v.toWorld());
    glm::vec3 la = glm::vec3(inv * glm::vec4(a, 1.0f)), lb = glm::vec3(inv * glm::vec4(b, 1.0f));
    glm::vec3 d = lb - la;
    float t0 = 0.0f, t1 = 1.0f;
    bool miss = false;
    for (int k = 0; k < 3 && !miss; ++k) {
      if (std::fabs(d[k]) < 1e-9f) {
        miss = la[k] < t.lo[k] || la[k] > t.hi[k];
        continue;
      }
      float ta = (t.lo[k] - la[k]) / d[k], tb = (t.hi[k] - la[k]) / d[k];
      if (ta > tb) std::swap(ta, tb);
      t0 = std::max(t0, ta);
      t1 = std::min(t1, tb);
      miss = t0 > t1;
    }
    if (!miss && t0 < bestT) {
      bestT = t0;
      best = int(i);
    }
  }
  if (best >= 0) hit = a + (b - a) * bestT;
  return best;
}

void Targets::destroy(int i, Killer by, const std::string& name) {
  Vehicle& v = list[size_t(i)];
  if (v.destroyed) return;
  v.health = 0.0f;
  v.destroyed = true;
  v.sinceDestroyed = 0.0f;
  v.killer = by;
  v.killerName = name;
}

bool Targets::harm(int i, float points, Killer by) {
  Vehicle& v = list[size_t(i)];
  if (v.destroyed || v.aboard || points <= 0.0f) return false;
  v.health -= points;
  if (v.health > 0.0f) return false;
  destroy(i, by);
  return true;
}

std::vector<int> Targets::damage(int i, WeaponType weapon, glm::vec3 at, Killer by, const std::string& name) {
  std::vector<int> killed;
  auto hurt = [&](size_t k, float amount) {
    Vehicle& v = list[k];
    if (v.destroyed || v.aboard || amount <= 0.0f) return;
    v.health -= amount;
    if (v.health <= 0.0f) {
      destroy(int(k), by, name);
      killed.push_back(int(k));
    }
  };
  if (i >= 0) {
    const VehicleType& t = vehicleType(list[i].kind);
    float amount = hitDamage(weapon, t.armour);
    // A 12.7 mm bullet goes through a drone's thin skin and mostly on: it takes some 20 of them
    // to bring a Shahed down (and the spread of the gun makes that many more rounds further off).
    if (weapon == WeaponType::YakB && t.cls == UnitClass::Drone) amount *= 0.125f;
    // An interceptor drone's charge on a helicopter's rear: the tail boom off, the end.
    if (weapon == WeaponType::Interceptor && t.cls == UnitClass::Helicopter) {
      const glm::vec3 local = glm::vec3(glm::inverse(list[i].toWorld()) * glm::vec4(at, 1.0f));
      if (local.z > t.lo.z + 0.6f * (t.hi.z - t.lo.z)) amount = list[i].health;
    }
    hurt(size_t(i), amount);
  }
  if (const Blast* b = blast(weapon)) {
    for (size_t k = 0; k < list.size(); ++k) {
      if (int(k) == i) continue;
      // Distance to the hull rather than its centre: a big vehicle catches more fragments.
      const Vehicle& v = list[k];
      const VehicleType& t = vehicleType(v.kind);
      if (glm::distance(v.pos, at) > b->radius + 20.0f) continue;
      glm::vec3 local = glm::vec3(glm::inverse(v.toWorld()) * glm::vec4(at, 1.0f));
      float dist = glm::distance(local, glm::clamp(local, t.lo, t.hi));
      float f = std::max(0.0f, 1.0f - dist / b->radius);
      hurt(k, b->full[int(t.armour)] * f * f);
    }
  }
  return killed;
}

void roundOnHind(Helicopter& h, WeaponType w, glm::vec3 at, const std::string& by) {
  using Part = Helicopter::HitPart;
  const glm::vec3 b = glm::inverse(h.orient) * (at - h.pos);
  const float pts = hitDamage(w, Armour::Light);
  // A chance of the round finding the tail rotor or an engine, sure for anything big.
  static uint32_t rng = 12345u;
  rng = rng * 1664525u + 1013904223u;
  const bool found = pts >= 10.0f || float(rng >> 16) / 65536.0f < 0.15f;
  const bool wasDown = h.damage.shotDown;
  if (found && b.z > 6.0f) h.hit(Part::TailRotor, pts * 3.0f);
  else if (found && b.y > 0.3f && std::fabs(b.z) < 3.5f) h.hit(b.x < 0.0f ? Part::Engine1 : Part::Engine2, pts * 3.0f);
  else h.hit(Part::Airframe, pts);
  if (!wasDown) h.damage.lastHitBy = by;
}

bool blastOnHind(Helicopter& h, WeaponType w, glm::vec3 at, const std::string& by) {
  const Blast* b = blast(w);
  if (!b || h.crashed) return false;
  const float d = glm::distance(at, h.pos);
  if (d >= b->radius) return false;
  const bool wasDown = h.damage.shotDown;
  const float f = 1.0f - d / b->radius;
  h.hit(Helicopter::HitPart::Airframe, b->full[int(Armour::Light)] * f * f * 0.3f);
  if (!wasDown) h.damage.lastHitBy = by;
  return true;
}

void Targets::setRoute(int i, std::vector<RoutePoint> route, bool loop, float delay, size_t start) {
  Vehicle& v = list[size_t(i)];
  v.route = std::move(route);
  v.loop = loop;
  v.delay = delay;
  v.waiting = 0.0f;
  const size_t n = v.route.size();
  start = n ? std::min(start, n - 1) : 0;
  v.next = n < 2 ? 0 : start + 1 < n ? start + 1 : loop ? 0 : n;
  if (start > 0) {
    const glm::vec2 at = v.route[start].pos, to = v.route[v.next % n].pos;
    v.pos = glm::vec3(at.x, terrain_.groundHeight(at.x, at.y), at.y);
    if (glm::distance(at, to) > 0.1f) v.heading = std::atan2(to.x - at.x, -(to.y - at.y));
  }
  if (n && isAircraft(v.kind)) {
    v.pos.y += v.route[start].alt;
    // Up there before its flight starts: a helicopter holds in the air, its rotor turning.
    v.hover = vehicleType(v.kind).cls == UnitClass::Helicopter && v.route[start].alt > 0.5f;
  }
}

void Targets::step(float dt) {
  for (Vehicle& v : list) {
    const glm::vec3 was = v.pos;
    stepUnit(v, dt);
    if (v.hover && v.pos.y < terrain_.groundHeight(v.pos.x, v.pos.z) + 0.5f) v.hover = false;  // landed
    v.vel = dt > 0.0f ? (v.pos - was) / dt : glm::vec3(0.0f);
  }
  // Mines go off under the enemy's feet and wheels (they are not set off by standing still).
  if (mines.empty()) return;
  for (size_t i = 0; i < list.size(); ++i) {
    Vehicle& v = list[i];
    if (v.destroyed || v.aboard || v.side != Side::Enemy || isAircraft(v.kind) || glm::length(v.vel) < 0.2f) continue;
    const VehicleType& t = vehicleType(v.kind);
    const float reach = t.cls == UnitClass::Soldier ? 1.5f : 0.5f * std::max(t.hi.x - t.lo.x, t.hi.z - t.lo.z);
    for (size_t m = 0; m < mines.size(); ++m) {
      if (glm::distance(glm::vec2(mines[m].x, mines[m].z), glm::vec2(v.pos.x, v.pos.z)) > reach) continue;
      mines.erase(mines.begin() + long(m));
      destroy(int(i), Killer::Player);
      mineKills_.push_back(int(i));
      break;
    }
  }
}

std::vector<int> Targets::takeMineKills() { return std::exchange(mineKills_, {}); }

void Targets::stepUnit(Vehicle& v, float dt) {
  if (v.destroyed) {
    v.sinceDestroyed += dt;
    const float ground = terrain_.groundHeight(v.pos.x, v.pos.z);
    if (vehicleType(v.kind).cls == UnitClass::Drone) {
      // A drone shot down falls on along its path, as a thrown thing (its wing no longer
      // carrying it, slowed by the air), and spinning (Vehicle::toWorld); a heap on the ground.
      if (v.pos.y > ground && !v.struck && !v.spent) {
        v.vel.y = std::max(v.vel.y - 9.81f * dt, -kDroneWreckSink);
        const float drag = std::exp(-kDroneWreckDrag * dt);
        v.vel.x *= drag, v.vel.z *= drag;
        v.pos += v.vel * dt;
      }
      if (v.pos.y <= ground || v.struck || v.spent) v.pos.y = std::max(v.pos.y, ground), v.vel = glm::vec3(0.0f);
    } else if (isAircraft(v.kind)) {
      // Aircraft come down.
      v.pos.y = std::max(ground, v.pos.y - 25.0f * dt);
    }
    return;
  }
  if (!v.moving()) return;
  if (v.delay > 0.0f) {
    v.delay -= dt;
    return;
  }
  if (v.waiting > 0.0f) {
    v.waiting -= dt;
    return;
  }
  const RoutePoint& to = v.route[v.next];
  const RoutePoint& from = v.route[v.next == 0 ? v.route.size() - 1 : v.next - 1];
  glm::vec2 d = to.pos - glm::vec2(v.pos.x, v.pos.z);
  float dist = glm::length(d);
  float speed = std::max(to.speed, 1.0f);
  const bool air = isAircraft(v.kind), soldier = vehicleType(v.kind).cls == UnitClass::Soldier;
  if (vehicleType(v.kind).cls == UnitClass::Aircraft && v.pos.y - terrain_.groundHeight(v.pos.x, v.pos.z) > 3.0f)
    speed = std::max(speed, to.alt > 0.0f ? kJetSpeed : kJetApproach);
  const bool drone = vehicleType(v.kind).cls == UnitClass::Drone;
  // A drone's last leg ends on its target: it dives there, and it comes down onto it.
  const bool diving = drone && !v.loop && v.next + 1 == v.route.size() && dist < kDiveStart;
  // In the dive a Shahed gets a fifth faster; the jet one opens up to full thrust, from its
  // some 330 km/h to some 550; the Banderol from its 500 to 650.
  const bool cruise = isCruiseMissile(v.kind);
  if (drone)
    speed *= 1.0f + (cruise ? 0.02f : 0.08f) * droneTrait(v, 1.0f) +
             (diving ? (cruise ? 0.3f : isJetDrone(v.kind) ? 0.65f : 0.2f) : 0.0f);
  float step = speed * dt;
  if (drone) v.flown += step;
  // Close enough: aircraft and vehicles cannot turn on the spot (a fast jet's turn is wide).
  const float reach = diving ? 20.0f : air ? std::max(150.0f, 1.2f * speed / kTurnRate) : soldier ? 0.5f : 5.0f;
  if (dist <= std::max(step, reach)) {
    v.waiting = to.wait;
    if (++v.next == v.route.size()) v.next = v.loop ? 0 : v.route.size();
    if (drone && v.next == v.route.size()) {
      // At the end of its route the drone dives onto its target and blows up; a decoy just
      // comes down there.
      const glm::vec3 at{to.pos.x, terrain_.groundHeight(to.pos.x, to.pos.y), to.pos.y};
      v.destroyed = true;
      v.health = 0.0f;
      v.pos = at;
      if (isDecoy(v.kind)) {
        v.spent = true;
        return;
      }
      v.struck = true;
      strikes_.push_back(at);
      for (size_t k = 0; k < list.size(); ++k) {
        Vehicle& o = list[k];
        if (&o == &v || o.destroyed || o.aboard) continue;
        const float d = glm::distance(o.pos, at);
        if (d < kStrikeRadius) harm(int(k), (o.scenery ? 80.0f : 40.0f) * (1.0f - d / kStrikeRadius) + 10.0f, Killer::Enemy);
      }
      return;
    }
    if (soldier || dist <= step) {
      v.pos.x = to.pos.x;
      v.pos.z = to.pos.y;
    }
  } else {
    // Soldiers walk straight there; the rest turn at a limited rate and slow down in tight turns.
    float want = std::atan2(d.x, -d.y);
    // A drone weaves a little either side of its course (some 15 m), not on its final dive.
    if (drone && !diving && !cruise)
      want += 0.06f * std::sin(v.flown * 6.2832f / (1200.0f + 500.0f * droneTrait(v, 2.0f)) + 3.0f * droneTrait(v, 3.0f));
    float turn = std::remainder(want - v.heading, 2.0f * 3.14159265f);
    float rate = soldier ? 6.0f : air ? kTurnRate : kTurnRate * 1.5f;
    v.heading += std::clamp(turn, -rate * dt, rate * dt);
    glm::vec2 dir = soldier ? glm::normalize(d) : glm::vec2(std::sin(v.heading), -std::cos(v.heading));
    if (!soldier && !air) step *= std::max(0.2f, std::cos(turn));
    v.pos.x += dir.x * step;
    v.pos.z += dir.y * step;
  }
  const float ground = terrain_.groundHeight(v.pos.x, v.pos.z);
  if (air) {
    // Height along the leg, but never into the ground.
    float legLength = glm::length(to.pos - from.pos);
    float t = legLength > 1.0f ? std::clamp(1.0f - dist / legLength, 0.0f, 1.0f) : 1.0f;
    float want = ground + from.alt + (to.alt - from.alt) * t;
    // (The Banderol follows the ground at its speed: four times as steeply.)
    float climb = cruise ? 4.0f * kClimbRate : kClimbRate, sink = climb;
    if (drone) {
      // Its height wanders 20 to 45 m up and down over some kilometres (never below 25 m).
      // (The Banderol keeps closer to its height, some 10 m.)
      const float amp = cruise ? 10.0f : 32.5f + 12.5f * droneTrait(v, 4.0f), wave = 6500.0f + 2000.0f * droneTrait(v, 5.0f);
      want = std::max(want + amp * std::sin(v.flown * 6.2832f / wave + 3.0f * droneTrait(v, 6.0f)), std::min(want, ground + 25.0f));
      if (diving) want = std::min(want, ground + kDiveSlope * std::max(0.0f, dist - 10.0f)), sink = kDiveRate;
    }
    v.pos.y += std::clamp(want - v.pos.y, -sink * dt, climb * dt);
    v.pos.y = std::max(v.pos.y, ground + (to.alt > 0.0f && from.alt > 0.0f && !diving ? 10.0f : 0.0f));
  } else {
    v.pos.y = ground;
  }
}

int Targets::alive() const {
  return int(std::count_if(list.begin(), list.end(), [](const Vehicle& v) { return !v.destroyed; }));
}

}  // namespace sim
