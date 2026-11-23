#include "sim/bowman.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace sim {

namespace {

// Outside the box lo..hi by (0 inside).
float boxDistance(glm::vec3 p, glm::vec3 lo, glm::vec3 hi) {
  const glm::vec3 d = glm::max(glm::max(lo - p, p - hi), glm::vec3(0.0f));
  return glm::length(d);
}

// How far `p` is from a flat convex face.
float faceDistance(glm::vec3 p, const ShapeFace& f) {
  const float above = glm::dot(p - f.pts[0], f.normal);
  const glm::vec3 on = p - f.normal * above;
  bool in = true;
  float edge = 1e9f;
  const size_t n = f.pts.size();
  // Which way round it goes: by its first corner that is not in line.
  const glm::vec3 turn = glm::cross(f.pts[1] - f.pts[0], f.pts[2] - f.pts[1]);
  const float side = glm::dot(turn, f.normal) >= 0.0f ? 1.0f : -1.0f;
  for (size_t i = 0; i < n; ++i) {
    const glm::vec3 a = f.pts[i], b = f.pts[(i + 1) % n], ab = b - a;
    if (side * glm::dot(glm::cross(ab, on - a), f.normal) < 0.0f) in = false;
    const float l2 = glm::dot(ab, ab);
    const float t = l2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    edge = std::min(edge, glm::distance(p, a + ab * t));
  }
  return in ? std::fabs(above) : edge;
}

// How far `local` (in the unit's axes) is from the unit: from its faces when it has them.
float unitDistance(glm::vec3 local, const UnitShape& u) {
  const float box = boxDistance(local, u.lo, u.hi);
  if (!u.faces || u.faces->empty() || box > 2.0f) return box;
  float d = 1e9f;
  for (const ShapeFace& f : *u.faces) d = std::min(d, faceDistance(local, f));
  return d;
}

// The height of the face at (x, z) (its axes), when (x, z) lies under or over it.
bool faceHeight(glm::vec2 xz, const ShapeFace& f, float& y) {
  const size_t n = f.pts.size();
  int sign = 0;
  for (size_t i = 0; i < n; ++i) {
    const glm::vec3 a = f.pts[i], b = f.pts[(i + 1) % n];
    const float c = (b.x - a.x) * (xz.y - a.z) - (b.z - a.z) * (xz.x - a.x);
    if (c == 0.0f) continue;
    const int s = c > 0.0f ? 1 : -1;
    if (sign != 0 && s != sign) return false;
    sign = s;
  }
  y = f.pts[0].y - (f.normal.x * (xz.x - f.pts[0].x) + f.normal.z * (xz.y - f.pts[0].z)) / f.normal.y;
  return true;
}

}  // namespace

float hindDistance(glm::vec3 p, const HindShape& h, glm::vec3* body) {
  const glm::vec3 b = glm::inverse(h.orient) * (p - h.pos);
  if (body) *body = b;
  // The fuselage (the nose to the engines' exhausts), the tail boom with its fin, the stub wings,
  // and the main rotor's disc (8.65 m round the hub, 2.3 m above the middle).
  float d = boxDistance(b, {-1.0f, -1.4f, -8.0f}, {1.0f, 1.4f, 4.0f});
  d = std::min(d, boxDistance(b, {-0.5f, -0.2f, 4.0f}, {0.5f, 2.5f, 9.5f}));
  d = std::min(d, boxDistance(b, {-3.3f, -0.7f, -1.3f}, {3.3f, 0.1f, 0.6f}));
  const float out = std::max(0.0f, glm::length(glm::vec2(b.x, b.z)) - 8.65f);
  const float up = std::max(0.0f, std::fabs(b.y - 2.3f) - 0.3f);
  return std::min(d, std::sqrt(out * out + up * up));
}

void Bowman::place(glm::vec2 xz, float heading, const world::Terrain& terrain) {
  pos = {xz.x, terrain.groundHeight(xz.x, xz.y), xz.y};
  yaw = heading, pitch = 0.0f, stride = 0.0f, walking = false, bob = 0.0f;
  state = State::Ready, draw = 0.0f, fit = 0.0f, arrowAge = 0.0f;
  airborne = false, pain = 0.0f, climb_ = 0.0f, vel = glm::vec2(0.0f);
  health = kHealth, blood = 0.0f, deadFor = 0.0f;
  belt = kM60Belt, reload = 0.0f, lawReady = 0.0f, kick = 0.0f, fireDue_ = 0.0f, letDown_ = false;
}

void Bowman::jump() {
  if (airborne) return;
  airborne = true, climb_ = kJumpSpeed, fallTop_ = pos.y;
}

void Bowman::fall(float dt, const world::Terrain& terrain, const std::vector<UnitShape>& obstacles) {
  constexpr float kOffEdge = 0.7f;  // m: a drop he only steps down
  pain = std::max(0.0f, pain - dt);
  if (!airborne) {
    const float floor = floorAt({pos.x, pos.z}, pos.y, terrain, obstacles);
    if (floor < pos.y - kOffEdge) airborne = true, climb_ = 0.0f, fallTop_ = pos.y;
    else pos.y = floor;
    return;
  }
  climb_ -= Helicopter::kGravity * dt;
  const glm::vec3 next{pos.x, pos.y + climb_ * dt, pos.z};
  // His head against something above him.
  if (climb_ > 0.0f && blocked(next, {}, obstacles)) {
    climb_ = 0.0f;
    return;
  }
  const float floor = floorAt({next.x, next.z}, next.y, terrain, obstacles);
  if (climb_ <= 0.0f && next.y <= floor) {
    if (fallTop_ - floor > kHurtDrop) {
      pain = 1.0f;
      // (Some 30 from 6 m: he lives through more than a man would.)
      hurt((fallTop_ - floor - kHurtDrop) * 8.0f);
    }
    pos.y = floor, airborne = false, climb_ = 0.0f;
    return;
  }
  pos = next;
  fallTop_ = std::max(fallTop_, pos.y);
}

void Bowman::turn(float dYaw, float dPitch) {
  yaw = std::remainder(yaw - dYaw, glm::two_pi<float>());
  pitch = std::clamp(pitch + dPitch, -1.45f, 1.45f);
}

glm::vec3 Bowman::look() const {
  return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

glm::vec3 Bowman::eye() const {
  const glm::vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
  if (dead()) return pos + glm::vec3(0.0f, 0.25f, 0.0f) + right * 0.6f;
  // Down at each footfall and up between, twice a cycle; to the side of the foot he is on. More
  // the faster he goes: some 6 cm walking, 11 cm running.
  const float k = bob * (0.6f + 0.5f * std::min(1.0f, glm::length(vel) / kRun));
  const float down = 0.1f * k * (1.0f - std::cos(2.0f * stride)) * 0.5f, aside = 0.04f * k * std::sin(stride);
  return pos + glm::vec3(0.0f, kEye - down, 0.0f) + right * aside;
}

glm::vec3 Bowman::up() const {
  const glm::vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
  if (dead()) return glm::normalize(glm::vec3(0.0f, 0.3f, 0.0f) - right);
  // His head rolls a degree or so towards the foot he is on.
  const float roll = 0.02f * bob * std::sin(stride);
  return glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) + right * roll);
}

bool Bowman::landing(float d, const world::Terrain& terrain, glm::vec3& at, std::vector<glm::vec3>* path,
                     const std::vector<HindShape>& hinds, const std::vector<UnitShape>& units, bool* struck) const {
  if (struck) *struck = false;
  // Its fuse as in its flight (step): anything within kFuse, checked every half a metre.
  const auto fuse = [&](glm::vec3 a, glm::vec3 b, glm::vec3& where) {
    const int n = std::max(1, int(std::ceil(glm::distance(a, b) / 0.5f)));
    for (int k = 1; k <= n; ++k) {
      const glm::vec3 p = a + (b - a) * (float(k) / float(n));
      for (const HindShape& s : hinds)
        if (glm::distance(p, s.pos) < 20.0f && hindDistance(p, s) < kFuse) return where = p, true;
      for (const UnitShape& u : units) {
        const glm::vec3 local(u.toLocal * glm::vec4(p, 1.0f));
        if (boxDistance(local, u.lo, u.hi) < kFuse + 2.0f && unitDistance(local, u) < kFuse) return where = p, true;
      }
    }
    return false;
  };
  glm::vec3 p = eye() + look() * 0.6f - glm::vec3(0.0f, 0.08f, 0.0f), v = look() * (kSpeedMin + (kSpeedMax - kSpeedMin) * d);
  constexpr float h = 0.02f;
  if (path) path->clear();
  int n = 0;
  for (float t = 0.0f; t < kArrowLife; t += h) {
    if (path && n++ % 5 == 0) path->push_back(p);
    const glm::vec3 next = p + v * h;
    v.y -= Helicopter::kGravity * h;
    if (glm::vec3 where; (!hinds.empty() || !units.empty()) && fuse(p, next, where)) {
      at = where;
      if (path) path->push_back(at);
      if (struck) *struck = true;
      return true;
    }
    const float g = terrain.groundHeight(next.x, next.z);
    if (next.y <= g) {
      // (Between the two steps, where it crosses the ground.)
      const float before = p.y - terrain.groundHeight(p.x, p.z), after = next.y - g;
      const float f = before > after ? before / (before - after) : 1.0f;
      at = p + (next - p) * f;
      at.y = terrain.groundHeight(at.x, at.z);
      if (path) path->push_back(at);
      return true;
    }
    p = next;
  }
  return false;
}

void Bowman::change() {
  letDown();
  weapon = weapon == Weapon::Bow ? Weapon::M60 : weapon == Weapon::M60 ? Weapon::Law : Weapon::Bow;
  aimHind = -1, aimUnit = -1, aimFor = 0.0f, locked = false;
}

bool Bowman::where(int8_t hind, int unit, const std::vector<HindShape>& hinds, const std::vector<UnitShape>& units, glm::vec3& at) {
  if (hind >= 0) {
    for (const HindShape& h : hinds)
      if (h.id == hind) return at = h.pos, true;
    return false;
  }
  if (unit >= 0)
    for (const UnitShape& u : units)
      if (u.index == unit) return at = glm::vec3(glm::inverse(u.toLocal) * glm::vec4((u.lo + u.hi) * 0.5f, 1.0f)), true;
  return false;
}

namespace {
// Nothing of the ground between `a` and `b` (sampled every 10 m).
bool inSight(glm::vec3 a, glm::vec3 b, const world::Terrain& terrain) {
  const int n = std::max(1, int(glm::distance(a, b) / 10.0f));
  for (int k = 1; k < n; ++k) {
    const glm::vec3 p = glm::mix(a, b, float(k) / float(n));
    if (p.y < terrain.groundHeight(p.x, p.z)) return false;
  }
  return true;
}

// `v` turned toward `want` (unit) by no more than `most` (rad), its speed kept.
glm::vec3 turnToward(glm::vec3 v, glm::vec3 want, float most) {
  const float speed = glm::length(v);
  const glm::vec3 d = v / speed;
  const float a = std::acos(std::clamp(glm::dot(d, want), -1.0f, 1.0f));
  if (a <= most) return want * speed;
  glm::vec3 axis = glm::cross(d, want);
  if (glm::length(axis) < 1e-6f) axis = std::fabs(d.y) < 0.9f ? glm::cross(d, glm::vec3(0, 1, 0)) : glm::cross(d, glm::vec3(1, 0, 0));
  axis = glm::normalize(axis);
  const float c = std::cos(most), sn = std::sin(most);
  return (d * c + glm::cross(axis, d) * sn + axis * glm::dot(axis, d) * (1.0f - c)) * speed;
}
}  // namespace

bool Bowman::stepLaw(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
                     const std::vector<UnitShape>& units, Hit& burst, const std::vector<Flare>& flares) {
  lawReady = std::max(0.0f, lawReady - dt);
  const bool pressed = trigger && !lawTrigger_;
  lawTrigger_ = trigger;
  const glm::vec3 eyes = eye(), view = look();
  // Locking on: the nearest to his aim of what he sees within the cone; held there long enough.
  if (weapon == Weapon::Law && !dead() && lawReady <= 0.0f) {
    const auto off = [&](glm::vec3 at) {
      const glm::vec3 to = at - eyes;
      const float d = glm::length(to);
      if (d < 5.0f || d > kLawRange) return 10.0f;
      return std::acos(std::clamp(glm::dot(to / d, view), -1.0f, 1.0f));
    };
    glm::vec3 at;
    bool keep = aimHind >= 0 || aimUnit >= 0;
    if (keep) keep = where(aimHind, aimUnit, hinds, units, at) && off(at) < (locked ? kKeepCone : kLockCone) && inSight(eyes, at, terrain);
    if (!keep) {
      aimHind = -1, aimUnit = -1, aimFor = 0.0f, locked = false;
      float best = kLockCone;
      for (const HindShape& h : hinds)
        if (h.id >= 0 && off(h.pos) < best && inSight(eyes, h.pos, terrain)) best = off(h.pos), aimHind = h.id, aimAt = h.pos;
      for (const UnitShape& u : units) {
        glm::vec3 c;
        if (u.index < 0 || !where(-1, u.index, hinds, units, c)) continue;
        if (off(c) < best && inSight(eyes, c + glm::vec3(0.0f, 0.5f, 0.0f), terrain)) best = off(c), aimHind = -1, aimUnit = u.index, aimAt = c;
      }
    } else {
      aimAt = at;
      aimFor += dt;
      locked = aimFor >= kLockTime;
    }
  } else {
    aimHind = -1, aimUnit = -1, aimFor = 0.0f, locked = false;
  }
  // Fired: off his shoulder along his look; locked, it goes for that.
  if (pressed && weapon == Weapon::Law && !dead() && lawReady <= 0.0f && !rocket.flying) {
    rocket = Rocket{};
    rocket.flying = true;
    rocket.pos = eyes + view * 1.0f + glm::vec3(0.0f, -0.08f, 0.0f) + glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw)) * 0.12f;
    rocket.vel = view * kLawSpeed;
    rocket.guide = view;
    if (locked) rocket.hind = aimHind, rocket.unit = aimUnit, rocket.seen = aimAt;
    for (const Flare& f : flares) rocket.flareSeen = std::max(rocket.flareSeen, f.id);  // (those already out don't lure it)
    lawReady = kLawReady, kick = 1.0f;
    aimHind = -1, aimUnit = -1, aimFor = 0.0f, locked = false;
  }
  if (!rocket.flying) return false;
  Rocket& r = rocket;
  // Where it goes: for what it is locked on (where that will be when it gets there), or along his
  // look a little ahead of itself (dead, he guides it no more: straight on).
  glm::vec3 want = glm::normalize(r.vel);
  glm::vec3 at;
  // A heat seeker on a Hind: the flares it lets go near it, its jammer.
  if (r.hind >= 0 && !r.decoyed()) {
    for (const HindShape& h : hinds) {
      if (h.id != r.hind) continue;
      int newest = -1;
      for (const Flare& f : flares)
        if (f.id > r.flareSeen && glm::distance(f.pos, h.pos) < kLawFlareNear) newest = std::max(newest, f.id);
      for (const Flare& f : flares) r.flareSeen = std::max(r.flareSeen, f.id);
      if (newest >= 0) {
        // (Looking up its exhausts from behind, it tells them apart better.)
        const float fromBehind = glm::dot(glm::normalize(r.pos - h.pos), h.orient * glm::vec3(0.0f, 0.0f, 1.0f));
        if (chance() < kLawFlare - 0.25f * std::max(fromBehind, 0.0f)) r.flare = newest;
      }
      if (!r.jamTried && r.age > 1.0f) {
        r.jamTried = true;
        if (h.jammer && chance() < kLawJam) r.lost = true;
      }
    }
  }
  if (r.flare >= 0) {
    bool has = false;
    for (const Flare& f : flares)
      if (f.id == r.flare) at = f.pos, has = true;
    if (has && glm::length(at - r.pos) > 0.1f) want = glm::normalize(at - r.pos);
    if (!has) r.flare = -1, r.lost = true;  // the flare burnt out: on straight
  } else if (r.lost) {
    // (Straight on.)
  } else if ((r.hind >= 0 || r.unit >= 0) && where(r.hind, r.unit, hinds, units, at)) {
    const glm::vec3 moving = r.seenOnce && dt > 0.0f ? (at - r.seen) / dt : glm::vec3(0.0f);
    r.seen = at, r.seenOnce = true;
    const float t = glm::distance(at, r.pos) / kLawSpeed;
    const glm::vec3 lead = at + moving * std::min(t, 3.0f) - r.pos;
    if (glm::length(lead) > 0.1f) want = glm::normalize(lead);
  } else if (!dead()) {
    // (Also when what it was locked on is gone: then along his look.)
    // (Along his look as it was a moment ago, smoothed: as through honey, his hand's jerks
    // left out; and for a point well ahead of it, so that it comes onto the line gently.)
    r.guide = glm::normalize(glm::mix(r.guide, view, 1.0f - std::exp(-dt / kLawLag)));
    const float along = std::max(glm::dot(r.pos - eyes, r.guide), 0.0f) + 80.0f;
    const glm::vec3 to = eyes + r.guide * along - r.pos;
    if (glm::length(to) > 0.1f) want = glm::normalize(to);
  }
  // In steps short enough not to pass through a rotor.
  const int n = std::max(1, int(std::ceil(kLawSpeed * dt / 1.0f)));
  const float turnRate = r.guided() ? kLawGuidedTurn : kLawTurn;
  const float h = dt / float(n);
  for (int k = 0; k < n; ++k) {
    r.vel = turnToward(r.vel, want, turnRate * h);
    r.pos += r.vel * h;
    r.age += h;
    burst = Hit{r.pos, -1, -1};
    bool off = r.age > kLawLife;
    if (const float g = terrain.groundHeight(r.pos.x, r.pos.z); r.pos.y <= g) off = true, burst.pos.y = g + 0.3f;
    for (const HindShape& s : hinds)
      if (!off && hindDistance(r.pos, s) < kLawFuse) off = true, burst.hind = s.id;
    for (const UnitShape& u : units)
      if (!off && unitDistance(glm::vec3(u.toLocal * glm::vec4(r.pos, 1.0f)), u) < 0.5f) off = true, burst.target = u.index;
    if (!off) continue;
    r.flying = false;
    return true;
  }
  return false;
}

bool Bowman::hurt(float points) {
  if (dead() || points <= 0.0f) return false;
  health = std::max(0.0f, health - points);
  blood = std::min(1.0f, blood + 0.2f + points / 50.0f);
  if (!dead()) return false;
  deadFor = 0.0f, vel = glm::vec2(0.0f), walking = false, bob = 0.0f;
  if (state == State::Drawing) state = State::Ready, draw = 0.0f;
  return true;
}

bool Bowman::stepDead(float dt) {
  blood = std::max(0.0f, blood - dt * 0.5f);
  if (!dead()) return false;
  deadFor += dt;
  return deadFor >= kDeadTime;
}

namespace {

// What a round or a blast does to him: a direct hit's points, the blast's at its middle and how
// far it reaches (falling off with the square, as Targets::damage).
struct ManHarm {
  float round, blast, reach;
};
ManHarm manHarm(WeaponType w) {
  switch (w) {
    case WeaponType::YakB: return {4.0f, 0.0f, 0.0f};
    case WeaponType::Upk23: return {9.0f, 3.0f, 2.0f};
    case WeaponType::DoorGun: return {2.0f, 0.0f, 0.0f};
    case WeaponType::S8: return {0.0f, 70.0f, 10.0f};
    case WeaponType::S5: return {0.0f, 50.0f, 7.0f};
    case WeaponType::Shturm: return {0.0f, 60.0f, 6.0f};
    case WeaponType::R60: return {0.0f, 40.0f, 8.0f};
    case WeaponType::Interceptor: return {0.0f, 40.0f, 6.0f};
    case WeaponType::Mine: return {0.0f, 90.0f, 5.0f};
    case WeaponType::Fab250:
    case WeaponType::Ofab250: return {0.0f, 300.0f, 30.0f};
    case WeaponType::Fab500: return {0.0f, 400.0f, 42.0f};
    case WeaponType::Zab500: return {0.0f, 250.0f, 50.0f};
  }
  return {0.0f, 0.0f, 0.0f};
}

// How far `p` is from his body: a post from his feet to his head.
float bodyDistance(glm::vec3 p, glm::vec3 feet) {
  const float y = std::clamp(p.y, feet.y, feet.y + 1.8f);
  return glm::distance(p, glm::vec3(feet.x, y, feet.z));
}

float segmentDistance(glm::vec3 a, glm::vec3 b, glm::vec3 feet) {
  // (Sampled every 0.1 m: near enough for a man 0.3 m round.)
  const float len = glm::distance(a, b);
  const int n = std::max(1, int(std::ceil(len / 0.1f)));
  float d = 1e9f;
  for (int k = 0; k <= n; ++k) d = std::min(d, bodyDistance(a + (b - a) * (float(k) / float(n)), feet));
  return d;
}

}  // namespace

float Bowman::damageBy(const Impact& i) const {
  if (dead() || i.destroyed) return 0.0f;
  const ManHarm h = manHarm(i.type);
  const float d = bodyDistance(i.pos, pos);
  float points = 0.0f;
  // A round striking right by him (into the ground at his feet): splinters, half of a hit.
  if (h.round > 0.0f && d < 0.6f) points += h.round * 0.5f;
  if (h.blast > 0.0f && d < h.reach) {
    const float f = 1.0f - d / h.reach;
    points += h.blast * f * f;
  }
  return points;
}

float Bowman::damageBy(WeaponType w, glm::vec3 a, glm::vec3 b) const {
  if (dead()) return 0.0f;
  const ManHarm h = manHarm(w);
  if (h.round <= 0.0f || glm::distance(a, b) > 200.0f) return 0.0f;
  // Quickly away when nowhere near.
  const glm::vec3 mid = (a + b) * 0.5f;
  if (glm::distance(mid, pos) > glm::distance(a, b) * 0.5f + 3.0f) return 0.0f;
  return segmentDistance(a, b, pos) < 0.3f ? h.round : 0.0f;
}

float Bowman::chance() {
  rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
  return float(rng_ & 0xffffff) / float(0x1000000);
}

void Bowman::stepM60(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
                     const std::vector<UnitShape>& units, std::vector<Hit>& hits) {
  const auto rnd = [&]() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffff) / float(0x1000000) * 2.0f - 1.0f;
  };
  kick = std::max(0.0f, kick - dt * 12.0f);
  if (reload > 0.0f && (reload -= dt) <= 0.0f) reload = 0.0f, belt = kM60Belt;
  fireDue_ = std::max(0.0f, fireDue_ - dt);
  if (trigger && weapon == Weapon::M60 && !dead() && reload <= 0.0f) {
    while (fireDue_ <= dt && belt > 0) {
      const glm::vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
      glm::vec3 dir = glm::normalize(look() + right * (kM60Spread * rnd()) + glm::vec3(0.0f, kM60Spread * rnd(), 0.0f));
      Projectile r{WeaponType::DoorGun, eye() + look() * 0.9f - glm::vec3(0.0f, 0.12f, 0.0f), dir * kM60Speed};
      // (Flown on for what is left of this frame after it went.)
      r.age = -fireDue_;
      r.tracer = shot_++ % 5 == 0;
      rounds.push_back(r);
      fireDue_ += 1.0f / kM60Rate;
      kick = 1.0f;
      if (--belt == 0) reload = kM60Reload;
    }
  }
  for (Projectile& r : rounds) {
    const float h = dt - std::min(0.0f, r.age);
    r.age = std::max(0.0f, r.age) + h;
    const glm::vec3 a = r.pos;
    r.vel.y -= Helicopter::kGravity * h;
    const glm::vec3 b = a + r.vel * h;
    r.pos = b;
    // Along the way in steps of half a metre: a Hind, a unit near the line, the ground.
    const float len = glm::distance(a, b);
    const int n = std::max(1, int(std::ceil(len / 0.5f)));
    std::vector<const UnitShape*> near;
    for (const UnitShape& u : units) {
      const glm::vec3 la(u.toLocal * glm::vec4(a, 1.0f)), lb(u.toLocal * glm::vec4(b, 1.0f));
      const glm::vec3 lo = glm::min(la, lb), hi = glm::max(la, lb);
      if (glm::all(glm::lessThanEqual(lo, u.hi + 0.5f)) && glm::all(glm::greaterThanEqual(hi, u.lo - 0.5f))) near.push_back(&u);
    }
    for (int k = 1; k <= n && r.age < 1e9f; ++k) {
      const glm::vec3 p = a + (b - a) * (float(k) / float(n));
      for (const HindShape& s : hinds)
        if (hindDistance(p, s) < 0.3f) {
          hits.push_back({p, -1, s.id}), r.age = 1e9f;
          break;
        }
      if (r.age >= 1e9f) break;
      for (const UnitShape* u : near)
        if (unitDistance(glm::vec3(u->toLocal * glm::vec4(p, 1.0f)), *u) < 0.2f) {
          if (u->index >= 0) hits.push_back({p, u->index, -1});
          r.age = 1e9f;
          break;
        }
      if (r.age >= 1e9f) break;
      if (const float g = terrain.groundHeight(p.x, p.z); p.y <= g) {
        hits.push_back({glm::vec3(p.x, g, p.z), -1, -1}), r.age = 1e9f;
        break;
      }
    }
  }
  std::erase_if(rounds, [](const Projectile& r) { return r.age >= kM60Life; });
}

bool Bowman::blocked(glm::vec3 feet, const std::vector<HindShape>& hinds, const std::vector<UnitShape>& obstacles) {
  // His body: a post 0.3 m round from his knees to his head.
  constexpr float kBody = 0.3f;
  for (float h : {0.6f, 1.1f, 1.7f}) {
    const glm::vec3 p = feet + glm::vec3(0.0f, h, 0.0f);
    for (const HindShape& s : hinds)
      if (hindDistance(p, s) < kBody) return true;
    for (const UnitShape& u : obstacles)
      if (unitDistance(glm::vec3(u.toLocal * glm::vec4(p, 1.0f)), u) < kBody) return true;
  }
  return false;
}

float Bowman::floorAt(glm::vec2 xz, float feet, const world::Terrain& terrain, const std::vector<UnitShape>& obstacles) {
  constexpr float kStep = 0.7f, kReach = 0.3f;  // m: what he steps up on, and how far ahead his foot finds it
  float floor = terrain.groundHeight(xz.x, xz.y);
  for (const UnitShape& u : obstacles) {
    if (!u.faces) continue;
    const glm::vec3 local(u.toLocal * glm::vec4(xz.x, feet, xz.y, 1.0f));
    const float up = feet - local.y;  // from its axes to the world's
    if (local.x < u.lo.x - kReach || local.x > u.hi.x + kReach || local.z < u.lo.z - kReach || local.z > u.hi.z + kReach) continue;
    for (const ShapeFace& f : *u.faces) {
      if (std::fabs(f.normal.y) < 0.7f) continue;  // too steep to stand on
      for (const glm::vec2 foot : {glm::vec2(0.0f), glm::vec2(kReach, 0.0f), glm::vec2(-kReach, 0.0f), glm::vec2(0.0f, kReach), glm::vec2(0.0f, -kReach)}) {
        float y;
        if (faceHeight(glm::vec2(local.x, local.z) + foot, f, y) && y <= local.y + kStep) floor = std::max(floor, y + up);
      }
    }
  }
  return floor;
}

void Bowman::walk(glm::vec2 move, bool run, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
                  const std::vector<UnitShape>& obstacles) {
  if (dead()) move = glm::vec2(0.0f);
  if (glm::length(move) > 1.0f) move = glm::normalize(move);
  // Drawing the bow he only steps slowly. He gets going gradually, stops quickly.
  const float top = state == State::Drawing ? kWalk * 0.4f : run ? kRun : kWalk;
  const glm::vec2 fwd{std::sin(yaw), -std::cos(yaw)}, right{std::cos(yaw), std::sin(yaw)};
  const glm::vec2 want = (fwd * move.y + right * move.x) * top;
  {
    const glm::vec2 dv = want - vel;
    const float faster = glm::length(want) > glm::length(vel) + 0.01f ? (run ? kRunAccel : kAccel) : kStop;
    // (In the air too: he gets over a railing he stood at.)
    const float most = faster * dt;
    vel += glm::length(dv) > most ? dv * (most / glm::length(dv)) : dv;
  }
  const float speed = glm::length(vel);
  walking = speed > 0.3f;
  bob = std::clamp(bob + (walking && !airborne ? dt : -dt) * 4.0f, 0.0f, 1.0f);
  if (speed < 0.01f) {
    vel = glm::vec2(0.0f);
    return;
  }
  // (In steps of no more than a quarter of a metre: not through a wall running.)
  const int parts = std::max(1, int(std::ceil(speed * dt / 0.25f)));
  for (int part = 0; part < parts; ++part) {
    const glm::vec2 step = vel * (dt / float(parts));
    // The whole step, or along the obstacle (its x or its z part); stuck inside one (put there),
    // he may walk out.
    // (In the air he keeps his height; off an edge he starts to fall: Bowman::fall.)
    const auto at = [&](glm::vec2 d) {
      const float floor = airborne ? pos.y : floorAt(glm::vec2(pos.x + d.x, pos.z + d.y), pos.y, terrain, obstacles);
      return glm::vec3(pos.x + d.x, floor < pos.y - 0.7f ? pos.y : floor, pos.z + d.y);
    };
    // Only what he is in already he may walk out of (a man walking into him, being put there).
    std::vector<HindShape> hindsNear;
    std::vector<UnitShape> unitsNear;
    for (const HindShape& h : hinds)
      if (!blocked(pos, {h}, {})) hindsNear.push_back(h);
    for (const UnitShape& u : obstacles)
      if (!blocked(pos, {}, {u})) unitsNear.push_back(u);
    bool moved = false;
    for (const glm::vec2 d : {step, glm::vec2(step.x, 0.0f), glm::vec2(0.0f, step.y)}) {
      if (d == glm::vec2(0.0f) || blocked(at(d), hindsNear, unitsNear)) continue;
      // Along a wall: what went into it is gone.
      if (d.x == 0.0f) vel.x = 0.0f;
      if (d.y == 0.0f) vel.y = 0.0f;
      pos = at(d);
      moved = true;
      break;
    }
    if (!moved) {
      vel = glm::vec2(0.0f);
      break;
    }
  }
  // The cycle (two steps) quicker as he goes faster: some 1.4 a second walking, 2.6 running.
  if (!airborne) stride = std::fmod(stride + (std::min(speed, 1.0f) * 0.9f + 0.06f * speed) * glm::two_pi<float>() * dt, glm::two_pi<float>());
}

void Bowman::letDown() {
  if (state != State::Drawing) return;
  state = State::Ready, draw = 0.0f, letDown_ = true;
}

bool Bowman::step(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
                  const std::vector<UnitShape>& units, glm::vec3& burst) {
  switch (state) {
    case State::Ready:
      if (!trigger) letDown_ = false;
      else if (!letDown_) state = State::Drawing, draw = 0.0f;
      break;
    case State::Drawing:
      if (trigger) {
        draw = std::min(1.0f, draw + dt / kDrawTime);
        break;
      }
      // Loosed: from beside his eye along his look.
      arrowPos = eye() + look() * 0.6f - glm::vec3(0.0f, 0.08f, 0.0f);
      arrowVel = look() * (kSpeedMin + (kSpeedMax - kSpeedMin) * draw);
      arrowAge = 0.0f, draw = 0.0f;
      state = State::Flying;
      break;
    case State::Flying: {
      // In steps short enough not to pass through a Hind's rotor disc.
      const int n = std::max(1, int(std::ceil(glm::length(arrowVel) * dt / 0.5f)));
      const float h = dt / float(n);
      for (int k = 0; k < n; ++k) {
        arrowVel.y -= Helicopter::kGravity * h;
        arrowPos += arrowVel * h;
        arrowAge += h;
        bool off = arrowAge > kArrowLife || arrowPos.y <= terrain.groundHeight(arrowPos.x, arrowPos.z);
        for (const HindShape& s : hinds) off = off || hindDistance(arrowPos, s) < kFuse;
        for (const UnitShape& u : units) off = off || unitDistance(glm::vec3(u.toLocal * glm::vec4(arrowPos, 1.0f)), u) < kFuse;
        if (!off) continue;
        burst = arrowPos;
        burst.y = std::max(burst.y, terrain.groundHeight(burst.x, burst.z) + 0.3f);
        state = State::Fitting, fit = kFitTime;
        return true;
      }
      break;
    }
    case State::Fitting:
      fit -= dt;
      if (fit <= 0.0f) state = State::Ready, fit = 0.0f;
      break;
  }
  return false;
}

bool arrowBlast(Helicopter& h, glm::vec3 at, float kPoints, float kReach, float kDirect) {
  if (h.crashed) return false;
  glm::vec3 b;
  const float d = hindDistance(at, {h.pos, h.orient}, &b);
  if (d >= kReach) return false;
  const bool wasDown = h.damage.shotDown;
  if (d < kDirect) {
    // What it struck goes: the tail rotor, an engine (on the side it hit), the tanks below.
    if (b.z > 5.0f) h.hit(Helicopter::HitPart::TailRotor, 100.0f);
    else if (b.y > 0.3f && std::fabs(b.z) < 3.5f) h.hit(b.x < 0.0f ? Helicopter::HitPart::Engine1 : Helicopter::HitPart::Engine2, 70.0f);
    else h.hit(Helicopter::HitPart::FuelTank, 40.0f);
    h.hit(Helicopter::HitPart::Airframe, kPoints * 0.7f);
  } else {
    const float k = 1.0f - (d - kDirect) / (kReach - kDirect);
    h.hit(Helicopter::HitPart::Airframe, kPoints * 0.7f * k * k);
  }
  if (!wasDown) h.damage.lastHitBy = "A LONE BOWMAN";
  return true;
}

}  // namespace sim
