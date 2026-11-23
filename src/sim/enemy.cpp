#include "sim/enemy.hpp"
#include "sim/weapons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sim {

namespace {

// Guns: muzzle velocity, drag (dv/dt = -drag v^2), rounds per second, spread of the rounds and
// the gunner's aiming error against a hovering helicopter (rad), reach, bursts and pauses (s),
// every how many rounds a tracer, airframe points of a hit.
struct Gun {
  float muzzle, drag, rate, spread, aim, range;
  float burstMin, burstMax, pauseMin, pauseMax;
  int tracerEvery;
  float damage;
  EnemyRound round;
};
// AK-47: 715 m/s, 600 rounds/min fired in short bursts, of little use beyond 500 m. DShK:
// 840 m/s, 600 rounds/min, effective against aircraft to some 1800 m. Twin 35 mm Oerlikon
// (and the ZSU-23-4): ~1100 m/s, guided by radar, out to 4 km.
const Gun kRifle{715.0f, 0.00087f, 10.0f, 0.004f, 0.025f, 550.0f, 0.3f, 1.0f, 1.5f, 4.0f, 5, 0.5f, EnemyRound::Bullet};
const Gun kHmg{840.0f, 0.00045f, 10.0f, 0.002f, 0.025f, 1500.0f, 1.0f, 2.5f, 2.5f, 6.0f, 3, 1.0f, EnemyRound::Bullet};
const Gun kAaa{1100.0f, 0.0002f, 18.0f, 0.0015f, 0.005f, 4000.0f, 1.0f, 2.0f, 2.0f, 5.0f, 2, 8.0f, EnemyRound::Shell};

// Aircraft cannons: the AH-64's 30 mm M230 in its chin turret (and the Cobra's 20 mm), a
// fighter's 20-30 mm fixed forward; fired in short bursts on a pass.
const Gun kGunshipGun{805.0f, 0.0004f, 10.0f, 0.004f, 0.018f, 1500.0f, 0.5f, 1.0f, 3.0f, 6.0f, 3, 3.0f, EnemyRound::Shell};
const Gun kFighterGun{1000.0f, 0.0003f, 25.0f, 0.004f, 0.010f, 1200.0f, 0.4f, 0.9f, 3.0f, 6.0f, 3, 5.0f, EnemyRound::Shell};
// Air-to-air heat seekers (the Stinger on the Apache, the AIM-9 and R-60 on fighters): flown as
// the Strela, from 1 to 5 km, two aboard.
constexpr float kAamMin = 900.0f, kAamMax = 5000.0f;

const Gun* gunOf(Arms a) {
  switch (a) {
    case Arms::Rifle: return &kRifle;
    case Arms::Hmg: return &kHmg;
    case Arms::Aaa: return &kAaa;
    default: return nullptr;
  }
}

// RPG-7: leaves at 117 m/s, the motor takes it to 295 m/s; it bursts after 4.5 s (920 m), which
// the mujaheddin used against helicopters.
constexpr float kRpgRange = 700.0f, kRpgLife = 4.5f, kRpgBurst = 12.0f;
// Strela-2M: ~430 m/s, 0.8 to 4 km, up to 2.3 km higher than the shooter; heat seeker, contact
// fuse. The Mi-24's exhaust screens (EVU) cool the engines' plumes: the seeker locks on from
// 3 km behind, 2 km beside, 1 km ahead, and centres a few metres off the exhausts, so many
// miss. Strela-1/10 on vehicles reach further. Not every man carries a missile ready.
constexpr float kStrelaSpeed = 430.0f, kStrelaTurn = 120.0f, kStrelaLife = 14.0f;
// Rapier: command guided, 650 m/s, out to 6.5 km.
constexpr float kRapierSpeed = 650.0f, kRapierLife = 12.0f, kRapierRange = 6500.0f;

// The helicopter's shape for hits, body axes: the fuselage and the tail boom.
struct Capsule {
  glm::vec3 a, b;
  float r;
};
const Capsule kHull[] = {{{0.0f, -0.4f, -5.6f}, {0.0f, 0.2f, 3.0f}, 1.0f}, {{0.0f, 0.4f, 3.0f}, {0.0f, 0.9f, 9.6f}, 0.45f}};
// The engines' hot exhausts, which a heat seeker sees (left and right, body axes).
const glm::vec3 kExhaust[2] = {{-0.9f, 1.0f, 1.2f}, {0.9f, 1.0f, 1.2f}};

float segmentDistance(glm::vec3 p0, glm::vec3 p1, glm::vec3 q0, glm::vec3 q1, glm::vec3& onP) {
  // Closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9).
  const glm::vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
  const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
  float s = 0.0f, t = 0.0f;
  if (a < 1e-9f) {
    t = e > 1e-9f ? std::clamp(f / e, 0.0f, 1.0f) : 0.0f;
  } else {
    const float c = glm::dot(d1, r);
    if (e < 1e-9f) {
      s = std::clamp(-c / a, 0.0f, 1.0f);
    } else {
      const float b = glm::dot(d1, d2), den = a * e - b * b;
      s = den > 1e-9f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) t = 0.0f, s = std::clamp(-c / a, 0.0f, 1.0f);
      else if (t > 1.0f) t = 1.0f, s = std::clamp((b - c) / a, 0.0f, 1.0f);
    }
  }
  onP = p0 + d1 * s;
  return glm::length(onP - (q0 + d2 * t));
}

}  // namespace

const char* clockOf(const Helicopter& heli, glm::vec3 at) {
  static const char* hours[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
  const glm::vec3 d = at - heli.pos;
  float b = glm::degrees(std::atan2(d.x, -d.z)) - heli.headingDeg();
  b = std::fmod(b + 720.0f + 15.0f, 360.0f);
  return hours[int(b / 30.0f) % 12];
}

float EnemyFire::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 24);
}

float EnemyFire::gauss() { return (random() + random() + random() + random() - 2.0f) * 1.7f; }

void EnemyFire::reset() {
  crews_.clear();
  shots_.clear();
  flareList_.clear();
  chaffList_.clear();
  flares = kFlares;
  chaff = kChaff;
  autoFlares = autoChaff = false;
  wingmen.clear();
  flareTimer_ = chaffTimer_ = 0.0f;
  sinceChaff_ = 1e9f;
}

bool EnemyFire::lineOfSight(glm::vec3 from, glm::vec3 to) const {
  const glm::vec3 d = to - from;
  const int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 30.0f));
  for (int i = 1; i < steps; ++i) {
    const glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain_.groundHeight(p.x, p.z) + 0.3f) return false;
  }
  return true;
}

void EnemyFire::releaseChaff(const Helicopter& heli, int count) { dropChaff(heli, count, chaff, 0); }

void EnemyFire::releaseFlares(const Helicopter& heli, int count) { dropFlares(heli, count, flares, 0); }

void EnemyFire::dropChaff(const Helicopter& heli, int count, int& stock, uint8_t at) {
  if (stock <= 0 || heli.crashed) return;
  const int n = std::min(stock, count);
  stock -= n;
  sinceChaff_ = 0.0f;
  for (int i = 0; i < n; ++i) {
    const float side = i % 2 ? 1.0f : -1.0f;
    const glm::vec3 out = heli.orient * glm::vec3(side * (10.0f + 5.0f * random()), -5.0f, 5.0f + 3.0f * random());
    chaffList_.push_back({heli.pos + heli.orient * glm::vec3(side * 0.6f, 0.3f, 4.5f), heli.vel * 0.3f + out, 0.0f});
  }
  // The Rapier's radar loses the helicopter in the cloud.
  for (EnemyShot& s : shots_)
    if (s.kind == EnemyRound::Rapier && s.at == at && !s.lost && s.lock == -1 && random() < 0.5f) s.lost = true;
}

void EnemyFire::dropFlares(const Helicopter& heli, int count, int& stock, uint8_t at) {
  if (stock <= 0 || heli.crashed) return;
  const int n = std::min(stock, count);
  stock -= n;
  for (int i = 0; i < n; ++i) {
    const float side = i % 2 ? 1.0f : -1.0f;
    const glm::vec3 out = heli.orient * glm::vec3(side * (14.0f + 6.0f * random()), -8.0f - 4.0f * random(), 6.0f + 4.0f * random());
    flareList_.push_back({heli.pos + heli.orient * glm::vec3(side * 0.6f, 0.3f, 4.5f), heli.vel + out, 0.0f, nextFlare_++});
  }
  // Seekers watching the helicopter may take a flare instead.
  for (EnemyShot& s : shots_) {
    if (s.kind != EnemyRound::Strela || s.at != at || s.lost || s.lock != -1) continue;
    const glm::vec3 to = glm::normalize(heli.pos - s.pos), tail = -heli.forward();
    const float fromBehind = glm::dot(-to, tail);  // 1 when it looks up the exhausts
    if (random() < 0.75f - 0.25f * std::max(fromBehind, 0.0f)) s.lock = flareList_.back().id - int(random() * float(n));
  }
}

bool EnemyFire::hitsHelicopter(const Helicopter& heli, glm::vec3 a, glm::vec3 b, float margin, glm::vec3& at) const {
  return hullHit(heli, a, b, margin, at);
}

bool hullHit(const Helicopter& heli, glm::vec3 a, glm::vec3 b, float margin, glm::vec3& at) {
  const glm::quat inv = glm::inverse(heli.orient);
  const glm::vec3 la = inv * (a - heli.pos), lb = inv * (b - heli.pos);
  for (const Capsule& c : kHull) {
    glm::vec3 on;
    if (segmentDistance(la, lb, c.a, c.b, on) < c.r + margin) {
      at = on;
      return true;
    }
  }
  return false;
}

void EnemyFire::damage(Helicopter& heli, const EnemyShot& s, glm::vec3 at) {
  using Part = Helicopter::HitPart;
  // `at` in body axes: which part the round found.
  const bool engines = at.y > 0.3f && at.z > -2.5f && at.z < 3.0f;
  const Part engine = at.x < 0.0f ? Part::Engine1 : Part::Engine2;
  float pts = s.damage;
  Part part = Part::Airframe;
  int failing = -1;  // a system knocked out by the hit
  // Rifle rounds against armour and the fuselage's skin: rarely more than a scratch, seldom
  // through to an engine or the tail rotor's drive.
  const bool rifle = s.kind == EnemyRound::Bullet && s.damage < 1.0f;
  const float r = rifle ? 0.2f + 0.8f * random() : random();
  if (s.kind == EnemyRound::Strela) {
    part = engine;  // it flies up the exhaust
  } else if (at.z > 8.0f && r < 0.4f) {
    part = Part::TailRotor, pts *= 3.0f;
  } else if (engines && r < 0.4f) {
    part = engine, pts *= 3.0f;
  } else if (at.y < 0.0f && at.z > -1.5f && at.z < 2.0f && r < 0.25f) {
    part = Part::FuelTank;
  } else if (rifle) {
    pts *= at.z < -2.5f ? 0.1f : 0.5f;  // the cabins are armoured
  } else if (random() < std::min(0.35f, pts / 25.0f)) {
    // Through the skin into the equipment there: the nose's sensors and gun, the avionics bay
    // behind the cockpits, the jammer on the back behind the rotor, the gear below.
    using S = System;
    static const S nose[] = {S::Hud, S::Hmtd, S::Lltv, S::Flir, S::Las, S::Gun};
    static const S bay[] = {S::Nav, S::Com, S::Twi};
    const float pick = random();
    const S s = at.y < -0.8f && at.z > -4.0f && at.z < 2.0f ? S::Gear
                : at.z < -2.5f                               ? nose[std::min(5, int(pick * 6.0f))]
                : at.z > 2.0f && at.y > 0.3f                 ? S::Jam
                                                             : bay[std::min(2, int(pick * 3.0f))];
    failing = int(s);
  }
  if (s.from != VehicleKind::Count && !heli.damage.shotDown) {
    static const char* names[] = {"", "RIFLE FIRE", "A HEAVY MACHINE GUN", "AN RPG-7", "A STRELA-2 (SA-7)", "ANTI-AIRCRAFT GUNS",
                                  "A SURFACE-TO-AIR MISSILE", "A TANK"};
    const VehicleType& by = vehicleType(s.from);
    // An aircraft by its name: "AN AH-64 APACHE", "A MIG-29".
    if (by.arms == Arms::Gunship || by.arms == Arms::Fighter)
      heli.damage.lastHitBy = (std::string("AEIOU").find(by.name.empty() ? 'X' : by.name[0]) != std::string::npos ? "AN " : "A ") + by.name;
    else
      heli.damage.lastHitBy = names[std::min(int(by.arms), 7)];
  }
  heli.hit(part, pts);
  if (failing >= 0 && !heli.damage.shotDown) heli.fail(System(failing));
}

void EnemyFire::fireFromAir(Crew& c, const Vehicle& v, const VehicleType& t, Helicopter& heli, float dt,
                            std::vector<EnemyEvent>& events) {
  const bool fighter = t.arms == Arms::Fighter;
  const glm::vec3 to = heli.pos - v.pos;
  const float dist = glm::length(to);
  c.cooldown = std::max(0.0f, c.cooldown - dt);
  // Where it points: a fighter along its flight, a gunship's turret round its nose.
  const float speed = glm::length(v.vel);
  const glm::vec3 nose = speed > 1.0f ? v.vel / speed : glm::vec3(std::sin(v.heading), 0.0f, -std::cos(v.heading));
  const float onNose = glm::dot(nose, to / std::max(dist, 1.0f));
  // A missile: the seeker sees the exhausts' heat (as the Strela's), the rails point ahead.
  if (c.missileWait > 0.0f) c.missileWait -= dt;
  if (c.ammo != 0 && c.missileWait <= 0.0f && dist > kAamMin && dist < kAamMax && onNose > 0.8f) {
    const float fromBehind = glm::dot(glm::normalize(v.pos - heli.pos), -heli.forward());
    const float lock = (fromBehind > 0.5f ? 4500.0f : fromBehind > -0.5f ? 3000.0f : 1500.0f);
    if (dist < lock) {
      EnemyShot s;
      s.from = v.kind;
      s.kind = EnemyRound::Strela;
      s.pos = v.pos + nose * 4.0f - glm::vec3(0.0f, 1.0f, 0.0f);
      s.vel = v.vel + glm::normalize(to) * 30.0f;
      s.life = kStrelaLife;
      s.damage = 70.0f;
      s.miss = glm::vec3(gauss(), gauss(), gauss()) * 2.2f;
      shots_.push_back(s);
      events.push_back({EnemyEvent::Launch, s.pos, s.kind});
      heli.setMessage(std::string("MISSILE LAUNCH ") + clockOf(heli, v.pos) + " O'CLOCK", 4.0f);
      c.ammo = c.ammo < 0 ? 1 : c.ammo - 1;
      c.missileWait = 12.0f + 8.0f * random();
      return;
    }
  }
  const Gun& g = fighter ? kFighterGun : kGunshipGun;
  if (dist > g.range || onNose < (fighter ? 0.98f : -0.34f)) {  // (the M230 turns 110 degrees either way)
    c.burst = 0.0f;
    if (c.pause <= 0.0f) c.pause = 0.5f;
    return;
  }
  if (c.pause > 0.0f) {
    c.pause -= dt;
    if (c.pause <= 0.0f) {
      c.burst = g.burstMin + (g.burstMax - g.burstMin) * random();
      const glm::vec3 rel = heli.vel - v.vel;
      c.across = glm::length(rel - to * (glm::dot(rel, to) / std::max(dist * dist, 1.0f)));
      const float sigma = g.aim * (0.4f + c.across / 25.0f) / std::max(skill, 0.2f);
      c.error = glm::vec2(gauss(), gauss()) * sigma;
      events.push_back({EnemyEvent::Muzzle, v.pos + nose * 3.0f, g.round});
    }
    return;
  }
  c.burst -= dt;
  if (c.burst <= 0.0f) {
    c.pause = g.pauseMin + (g.pauseMax - g.pauseMin) * random();
    return;
  }
  while (c.cooldown <= 0.0f) {
    c.cooldown += 1.0f / g.rate;
    // Lead on the closing speed: the rounds leave with the aircraft's own speed.
    const float time = dist / (g.muzzle * std::exp(-g.drag * dist * 0.5f));
    const glm::vec3 aim = heli.pos + (heli.vel - v.vel) * time + glm::vec3(0.0f, 0.5f * Helicopter::kGravity * time * time, 0.0f);
    glm::vec3 dir = glm::normalize(aim - v.pos);
    const glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(side, dir);
    dir = glm::normalize(dir + side * (c.error.x + gauss() * g.spread) + up * (c.error.y + gauss() * g.spread));
    EnemyShot s;
    s.from = v.kind;
    s.kind = g.round;
    s.pos = v.pos + nose * 3.0f;
    s.vel = dir * g.muzzle + v.vel;
    s.life = 4.0f;
    s.damage = g.damage;
    s.drag = g.drag;
    s.tracer = random() * float(g.tracerEvery) < 1.0f;
    shots_.push_back(s);
  }
}

void EnemyFire::fire(Crew& c, const Vehicle& v, const VehicleType& t, Helicopter& heli, float dt,
                     std::vector<EnemyEvent>& events) {
  if (t.arms == Arms::Gunship || t.arms == Arms::Fighter) {
    fireFromAir(c, v, t, heli, dt, events);
    return;
  }
  const bool soldier = t.cls == UnitClass::Soldier;
  const glm::vec3 muzzle = v.pos + glm::vec3(0.0f, soldier ? 1.5f : std::max(t.hi.y * 0.8f, 1.2f), 0.0f);
  const glm::vec3 to = heli.pos - muzzle;
  const float dist = glm::length(to);
  c.cooldown = std::max(0.0f, c.cooldown - dt);

  if (const Gun* g = gunOf(t.arms)) {
    if (dist > g->range) return;
    if (c.pause > 0.0f) {
      c.pause -= dt;
      if (c.pause <= 0.0f) {
        // A new burst: the gunner's error, larger for a fast crossing target and a poor shot.
        c.burst = g->burstMin + (g->burstMax - g->burstMin) * random();
        const glm::vec3 across = heli.vel - to * (glm::dot(heli.vel, to) / std::max(dist * dist, 1.0f));
        c.across = glm::length(across);
        // A radar-laid gun (the ZSU-23-4's) aims at the chaff as much as at the helicopter.
        const float chaffed = t.radarClass > 0 && sinceChaff_ < 5.0f ? 3.0f : 1.0f;
        const float sigma = chaffed * g->aim * (0.4f + c.across / 25.0f) / std::max(skill, 0.2f);
        c.error = glm::vec2(gauss(), gauss()) * sigma;
        events.push_back({EnemyEvent::Muzzle, muzzle, g->round});
      }
      return;
    }
    c.burst -= dt;
    if (c.burst <= 0.0f) {
      c.pause = g->pauseMin + (g->pauseMax - g->pauseMin) * random();
      return;
    }
    c.error *= std::exp(-dt / (2.0f + c.across / 8.0f));  // walking the fire onto it, easier when it hangs still
    while (c.cooldown <= 0.0f) {
      c.cooldown += 1.0f / g->rate;
      // Lead: where it will be when the round gets there, and the drop on the way.
      float time = dist / (g->muzzle * std::exp(-g->drag * dist * 0.5f));
      glm::vec3 aim = heli.pos + heli.vel * time + glm::vec3(0.0f, 0.5f * Helicopter::kGravity * time * time, 0.0f);
      glm::vec3 dir = glm::normalize(aim - muzzle);
      const glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
      const glm::vec3 up = glm::cross(side, dir);
      dir = glm::normalize(dir + side * (c.error.x + gauss() * g->spread) + up * (c.error.y + gauss() * g->spread));
      EnemyShot s;
      s.from = v.kind;
      s.kind = g->round;
      s.pos = muzzle;
      s.vel = dir * g->muzzle;
      s.life = 5.0f;
      s.damage = g->damage;
      s.drag = g->drag;
      s.tracer = random() * float(g->tracerEvery) < 1.0f;
      shots_.push_back(s);
    }
    return;
  }

  if (t.arms == Arms::Rpg) {
    if (c.ammo == 0 || c.cooldown > 0.0f || dist > kRpgRange || dist < 150.0f || heli.radarAltitude() > 350.0f) return;
    const float time = dist / 230.0f;
    const glm::vec3 aim = heli.pos + heli.vel * time + glm::vec3(0.0f, 0.5f * 3.0f * time * time, 0.0f);
    glm::vec3 dir = glm::normalize(aim - muzzle);
    dir = glm::normalize(dir + glm::vec3(gauss(), gauss() * 0.5f, gauss()) * (0.025f / std::max(skill, 0.2f)));
    EnemyShot s;
    s.from = v.kind;
    s.kind = EnemyRound::Rpg;
    s.pos = muzzle;
    s.vel = dir * 117.0f;
    s.life = kRpgLife;
    s.damage = 30.0f;
    shots_.push_back(s);
    events.push_back({EnemyEvent::Launch, muzzle, EnemyRound::Rpg});
    c.ammo = c.ammo < 0 ? 1 : c.ammo - 1;
    c.cooldown = 6.0f + 4.0f * random();
    return;
  }

  if (t.arms == Arms::Manpads || t.arms == Arms::Sam) {
    const bool radar = t.radarClass > 0;  // Rapier: command guided
    const float reach = radar ? kRapierRange : soldier ? 4000.0f : 5000.0f;
    if (c.ammo == 0 || c.cooldown > 0.0f || dist > reach || dist < 700.0f || heli.pos.y - v.pos.y > 2300.0f) return;
    if (!radar) {
      // The seeker needs the exhausts' heat: far from behind, close from ahead.
      const float fromBehind = glm::dot(glm::normalize(muzzle - heli.pos), -heli.forward());
      const float lock = (fromBehind > 0.5f ? 3000.0f : fromBehind > -0.5f ? 2000.0f : 1000.0f) * (soldier ? 1.0f : 1.5f);
      if (dist > lock) return;
    }
    EnemyShot s;
    s.from = v.kind;
    s.kind = radar ? EnemyRound::Rapier : EnemyRound::Strela;
    s.pos = muzzle + glm::vec3(0.0f, 0.5f, 0.0f);
    s.vel = glm::normalize(to + glm::vec3(0.0f, dist * 0.05f, 0.0f)) * 30.0f;
    s.life = radar ? kRapierLife : kStrelaLife;
    s.damage = radar ? 60.0f : 70.0f;
    if (!radar) s.miss = glm::vec3(gauss(), gauss(), gauss()) * 2.2f;
    shots_.push_back(s);
    events.push_back({EnemyEvent::Launch, s.pos, s.kind});
    heli.setMessage(std::string("MISSILE LAUNCH ") + clockOf(heli, muzzle) + " O'CLOCK", 4.0f);
    c.ammo = c.ammo < 0 ? (soldier ? 0 : 3) : c.ammo - 1;
    c.cooldown = 12.0f + 6.0f * random();
  }
}

void EnemyFire::step(Helicopter& heli, const Targets& targets, float dt, std::vector<EnemyEvent>& events,
                     const std::vector<Helicopter*>& others) {
  if (crews_.size() != targets.list.size()) crews_.assign(targets.list.size(), Crew());

  // The launchers switched on fire a pair every second or so, at irregular intervals.
  sinceChaff_ += dt;
  if (autoFlares && flares > 0 && (flareTimer_ -= dt) <= 0.0f) {
    releaseFlares(heli, 2);
    flareTimer_ = 0.6f + 0.9f * random();
  }
  if (autoChaff && chaff > 0 && (chaffTimer_ -= dt) <= 0.0f) {
    releaseChaff(heli, 2);
    chaffTimer_ = 0.6f + 0.9f * random();
  }
  if (wingmen.size() < others.size()) wingmen.resize(others.size());
  for (size_t k = 0; k < others.size(); ++k) {
    if (!others[k]) continue;
    Launchers& w = wingmen[k];
    if (w.autoFlares && w.flares > 0 && (w.flareTimer -= dt) <= 0.0f) {
      dropFlares(*others[k], 2, w.flares, uint8_t(k + 1));
      w.flareTimer = 0.6f + 0.9f * random();
    }
    if (w.autoChaff && w.chaff > 0 && (w.chaffTimer -= dt) <= 0.0f) {
      dropChaff(*others[k], 2, w.chaff, uint8_t(k + 1));
      w.chaffTimer = 0.6f + 0.9f * random();
    }
  }

  // A helicopter standing on its wheels is left alone (one on the apron at the start).
  auto fair = [](const Helicopter* h) { return h && !h->crashed && !h->damage.shotDown && h->weightOnWheels <= 0.0f; };
  std::vector<Helicopter*> helis{&heli};
  helis.insert(helis.end(), others.begin(), others.end());
  const bool anyFair = std::any_of(helis.begin(), helis.end(), fair);
  if (enabled && anyFair) {
    for (size_t i = 0; i < targets.list.size(); ++i) {
      const Vehicle& v = targets.list[i];
      const VehicleType& t = vehicleType(v.kind);
      if (v.destroyed || v.side != Side::Enemy || !v.armed || t.arms == Arms::None) continue;
      Crew& c = crews_[i];
      if (!c.init) {
        c.init = true;
        c.reaction = (2.0f + 4.0f * random()) / std::max(skill, 0.2f);
        c.next = random() * 0.5f;
        c.pause = 0.5f + random();
        if (t.arms == Arms::Manpads && random() > 0.4f) c.ammo = 0;
        if (t.arms == Arms::Gunship || t.arms == Arms::Fighter) c.ammo = 2, c.reaction *= 0.5f;
      }
      if (t.arms == Arms::TankGun) continue;  // (at units on the ground only: sim::GroundBattle)
      const bool air = t.arms == Arms::Gunship || t.arms == Arms::Fighter;
      const float far = t.arms == Arms::Sam                        ? kRapierRange
                        : t.arms == Arms::Fighter                  ? 6000.0f
                        : air || t.arms == Arms::Aaa || t.arms == Arms::Manpads ? 4500.0f
                                                                   : 2200.0f;
      // The nearest helicopter.
      int pick = -1;
      for (size_t k = 0; k < helis.size(); ++k)
        if (fair(helis[k]) && (pick < 0 || glm::distance(v.pos, helis[k]->pos) < glm::distance(v.pos, helis[size_t(pick)]->pos))) pick = int(k);
      Helicopter& target = *helis[size_t(pick)];
      const float dist = glm::distance(v.pos, target.pos);
      if (dist > far) {
        c.alert = std::max(0.0f, c.alert - dt);
        c.sees = false;
        continue;
      }
      c.next -= dt;
      if (c.next <= 0.0f) {
        c.next = 0.5f;
        c.sees = lineOfSight(v.pos + glm::vec3(0.0f, air ? 0.0f : 2.0f, 0.0f), target.pos);
      }
      if (!c.sees) {
        c.alert = std::max(0.0f, c.alert - dt * 0.5f);
        continue;
      }
      c.alert += dt;
      const size_t before = shots_.size();
      if (c.alert >= c.reaction) fire(c, v, t, target, dt, events);
      for (size_t k = before; k < shots_.size(); ++k) shots_[k].at = uint8_t(pick);
    }
  }

  // Flares fall and burn out in five seconds.
  for (Flare& f : flareList_) {
    f.vel += (glm::vec3(0.0f, -Helicopter::kGravity, 0.0f) - f.vel * glm::length(f.vel) * 0.004f) * dt;
    f.pos += f.vel * dt;
    f.age += dt;
  }
  flareList_.erase(std::remove_if(flareList_.begin(), flareList_.end(), [](const Flare& f) { return f.age > 5.0f; }),
                   flareList_.end());
  // Chaff slows in the air at once and drifts down for six seconds.
  for (Chaff& c : chaffList_) {
    c.vel += (glm::vec3(0.0f, -1.5f, 0.0f) - c.vel) * std::min(1.0f, 2.0f * dt);
    c.pos += c.vel * dt;
    c.age += dt;
  }
  chaffList_.erase(std::remove_if(chaffList_.begin(), chaffList_.end(), [](const Chaff& c) { return c.age > 6.0f; }),
                   chaffList_.end());

  for (EnemyShot& s : shots_) {
    const glm::vec3 old = s.pos;
    s.age += dt;
    if (s.kind == EnemyRound::Bullet || s.kind == EnemyRound::Shell) {
      const float speed = glm::length(s.vel);
      s.vel += (glm::vec3(0.0f, -Helicopter::kGravity, 0.0f) - s.vel * speed * s.drag) * dt;
    } else if (s.kind == EnemyRound::Rpg) {
      const float speed = glm::length(s.vel);
      if (s.age < 0.4f) s.vel += s.vel / speed * (450.0f * dt);
      s.vel.y -= 3.0f * dt;
    } else {
      // Missiles: the motor to its speed, then proportional navigation (heat seeker) or the
      // command link (Rapier) towards their target.
      const bool rapier = s.kind == EnemyRound::Rapier;
      const float top = rapier ? kRapierSpeed : kStrelaSpeed;
      const float speed = std::min(top, glm::length(s.vel) + (rapier ? 900.0f : 300.0f) * dt);
      glm::vec3 dir = glm::normalize(s.vel);
      // Guided onto the helicopter it was fired at.
      const bool other = s.at >= 1 && s.at < helis.size() && helis[s.at];
      const Helicopter& aim = other ? *helis[s.at] : heli;
      if (!rapier && !s.jamTried && s.age > 1.5f) {
        // The jammer's flickering heat makes the seeker wander off.
        s.jamTried = true;
        const bool on = other ? (size_t(s.at - 1) < wingmen.size() && wingmen[size_t(s.at - 1)].jammer) : jammer;
        if (on && aim.works(System::Jam) && random() < 0.35f) s.lost = true;
      }
      glm::vec3 target{0.0f};
      bool has = false;
      if (s.lock == -1 && !s.lost) {
        const int hot = aim.engineOut(0) ? 1 : 0;
        target = aim.pos + aim.orient * kExhaust[hot] + s.miss;
        if (rapier) target = aim.pos + glm::vec3(gauss(), gauss(), gauss()) * (2.0f / std::max(skill, 0.2f));
        has = true;
      } else if (!s.lost) {
        for (const Flare& f : flareList_)
          if (f.id == s.lock) target = f.pos, has = true;
        if (!has) s.lost = true;  // the flare burnt out
      }
      if (has) {
        const glm::vec3 los = target - s.pos;
        const float range = glm::length(los);
        const glm::vec3 want = rapier ? los / std::max(range, 1.0f)
                                      : glm::normalize(los + (s.lock == -1 ? aim.vel : glm::vec3(0.0f)) * (range / speed) * 0.8f);
        const glm::vec3 turn = want - dir * glm::dot(want, dir);
        const float maxTurn = (rapier ? 200.0f : kStrelaTurn) / speed * dt;
        const float len = glm::length(turn);
        if (len > 1e-5f) dir = glm::normalize(dir + turn / len * std::min(len, maxTurn));
      }
      s.vel = dir * speed;
    }
    s.pos += s.vel * dt;

    glm::vec3 at;
    const bool missile = s.kind == EnemyRound::Strela || s.kind == EnemyRound::Rapier;
    bool struck = false;
    for (Helicopter* h : helis)
      if (!struck && h && !h->crashed && hitsHelicopter(*h, old, s.pos, missile ? 1.0f : 0.0f, at)) {
        damage(*h, s, at);
        events.push_back({s.kind == EnemyRound::Bullet ? EnemyEvent::Hit : EnemyEvent::Burst, h->pos + h->orient * at, s.kind});
        s.age = 1e9f;
        struck = true;
      }
    if (struck) continue;
    if (s.pos.y < terrain_.groundHeight(s.pos.x, s.pos.z)) {
      if (s.kind != EnemyRound::Bullet) events.push_back({EnemyEvent::Burst, s.pos, s.kind});
      s.age = 1e9f;
      continue;
    }
    if (s.age > s.life) {
      // The RPG's grenade bursts at the end of its flight; its fragments reach some 12 m.
      if (s.kind == EnemyRound::Rpg) {
        events.push_back({EnemyEvent::Burst, s.pos, s.kind});
        for (Helicopter* h : helis) {
          const float d = h ? glm::distance(s.pos, h->pos) : 1e9f;
          if (d < kRpgBurst) {
            EnemyShot frag = s;
            frag.damage = 20.0f * (1.0f - d / kRpgBurst);
            damage(*h, frag, glm::inverse(h->orient) * (s.pos - h->pos));
          }
        }
      } else if (s.kind != EnemyRound::Bullet) {
        events.push_back({EnemyEvent::Burst, s.pos, s.kind});
      }
      s.age = 1e9f;
    }
  }
  shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const EnemyShot& s) { return s.age >= 1e9f; }), shots_.end());
}

}  // namespace sim
