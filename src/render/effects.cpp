#include "render/effects.hpp"

#include <algorithm>
#include <cmath>

#include "render/drones.hpp"

namespace render {

namespace {

const glm::vec4 kFire{1.0f, 0.72f, 0.25f, 0.95f};
const glm::vec4 kFireEnd{0.85f, 0.25f, 0.05f, 0.0f};
const glm::vec4 kSmoke{0.22f, 0.21f, 0.2f, 0.75f};
const glm::vec4 kSmokeEnd{0.45f, 0.44f, 0.42f, 0.0f};
const glm::vec4 kDust{0.55f, 0.48f, 0.36f, 0.8f};
const glm::vec4 kDustEnd{0.62f, 0.57f, 0.47f, 0.0f};
const glm::vec4 kTrail{0.82f, 0.82f, 0.8f, 0.55f};
const glm::vec4 kTrailEnd{0.9f, 0.9f, 0.88f, 0.0f};
const glm::vec4 kTracer{1.0f, 0.85f, 0.4f, 1.0f};
const glm::vec4 kRocketBody{0.25f, 0.27f, 0.25f, 1.0f};
const glm::vec4 kBombBody{0.27f, 0.31f, 0.20f, 1.0f};
const glm::vec4 kSpark{1.0f, 0.9f, 0.6f, 1.0f};
const glm::vec4 kBlackSmoke{0.08f, 0.08f, 0.08f, 0.85f};
const glm::vec4 kBlackSmokeEnd{0.3f, 0.3f, 0.3f, 0.0f};

constexpr float kFireTime = 240.0f;  // s a wreck smokes
constexpr glm::vec3 kWind{1.2f, 0.0f, 0.4f};

// Camera-facing polygon: a hexagon reads as a puff without looking like a sprite; `n` more sides
// for a round glow.
void puff(MeshBuilder& mb, glm::vec3 c, float r, glm::vec4 col, glm::vec3 eye, float spin, int n = 6) {
  glm::vec3 f = glm::normalize(eye - c);
  glm::vec3 u = std::fabs(f.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 a = glm::normalize(glm::cross(u, f)), b = glm::cross(f, a);
  for (int i = 0; i < n; ++i) {
    float t0 = spin + 6.2831853f * i / n, t1 = spin + 6.2831853f * (i + 1) / n;
    mb.tri(c, c + r * (a * std::cos(t0) + b * std::sin(t0)), c + r * (a * std::cos(t1) + b * std::sin(t1)), col);
  }
}

// Camera-facing strip from p to q.
void streak(MeshBuilder& mb, glm::vec3 p, glm::vec3 q, float width, glm::vec4 col, glm::vec3 eye) {
  glm::vec3 axis = q - p;
  glm::vec3 side = glm::cross(axis, eye - p);
  if (glm::length(side) < 1e-6f) return;
  side = glm::normalize(side) * (width * 0.5f);
  mb.tri(p - side, q - side, q + side, col);
  mb.tri(p - side, q + side, p + side, col);
}

}  // namespace

float Effects::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float(rng_ >> 8) / float(1u << 24);  // 0..1
}

void Effects::add(glm::vec3 pos, glm::vec3 vel, float life, float size0, float size1, glm::vec4 c0, glm::vec4 c1,
                  float drag) {
  parts_.push_back({pos, vel, 0.0f, life, size0, size1, c0, c1, drag});
}

void Effects::impact(const sim::Impact& i) {
  auto jitter = [&](float r) { return glm::vec3(random() - 0.5f, random() * 0.5f, random() - 0.5f) * r; };
  if (i.destroyed) {
    // The vehicle blows up: fireball, black smoke, debris, and the wreck keeps burning.
    add(i.pos, glm::vec3(0), 0.6f, 2.0f, 9.0f, kFire, kFireEnd);
    add(i.pos + glm::vec3(0, 2.0f, 0), glm::vec3(0, 6.0f, 0), 0.9f, 2.0f, 7.0f, kFire, kFireEnd);
    for (int k = 0; k < 6; ++k)
      add(i.pos + jitter(4.0f), glm::vec3(0, 5.0f, 0) + jitter(8.0f), 5.0f + random() * 3.0f, 2.0f, 9.0f, kBlackSmoke,
          kBlackSmokeEnd);
    for (int k = 0; k < 8; ++k)
      add(i.pos, glm::vec3(0, 14.0f, 0) + jitter(30.0f), 1.2f, 0.4f, 0.3f, kBlackSmoke, kBlackSmoke);
    fires_.push_back({i.pos, 0.0f, 0.0f, i.target});
    return;
  }
  if (i.target >= 0 && (i.type == sim::WeaponType::YakB || i.type == sim::WeaponType::Upk23 || i.type == sim::WeaponType::DoorGun)) {
    // Rounds hitting armour: a spark and a wisp of smoke instead of dust.
    add(i.pos, glm::vec3(0), 0.07f, 0.25f, 0.8f, kSpark, kFire);
    add(i.pos, glm::vec3(0, 1.5f, 0) + jitter(1.5f), 0.7f, 0.2f, 1.0f, kSmoke, kSmokeEnd);
    return;
  }
  switch (i.type) {
    case sim::WeaponType::Mine:
      // A mine dropping into the dust.
      add(i.pos + glm::vec3(0, 0.2f, 0), glm::vec3(0, 1.0f, 0) + jitter(1.0f), 0.4f, 0.3f, 1.0f, kDust, kDustEnd);
      break;
    case sim::WeaponType::YakB:
    case sim::WeaponType::DoorGun:
      add(i.pos + glm::vec3(0, 0.3f, 0), glm::vec3(0, 2.5f, 0) + jitter(3.0f), 0.9f, 0.3f, 1.8f, kDust, kDustEnd);
      break;
    case sim::WeaponType::Upk23:
      add(i.pos + glm::vec3(0, 0.4f, 0), glm::vec3(0, 1.0f, 0), 0.12f, 0.6f, 1.6f, kFire, kFireEnd);
      add(i.pos + glm::vec3(0, 0.5f, 0), glm::vec3(0, 3.0f, 0) + jitter(3.0f), 1.3f, 0.6f, 3.0f, kDust, kDustEnd);
      break;
    case sim::WeaponType::S8:
    case sim::WeaponType::S5:
    case sim::WeaponType::R60:
    case sim::WeaponType::Interceptor:
    case sim::WeaponType::Shturm: {
      const float k5 = i.type == sim::WeaponType::S5 || i.type == sim::WeaponType::Interceptor ? 0.7f
                       : i.type == sim::WeaponType::Shturm                                     ? 0.8f
                                                                                               : 1.0f;
      add(i.pos + glm::vec3(0, 1.0f, 0) * k5, glm::vec3(0), 0.35f, 1.5f * k5, 7.0f * k5, kFire, kFireEnd);
      for (int k = 0; k < 4; ++k)
        add(i.pos + glm::vec3(0, 1.5f, 0) * k5 + jitter(3.0f * k5), glm::vec3(0, 3.5f, 0) + jitter(4.0f),
            (4.0f + random() * 2.0f) * k5, 2.0f, 9.0f, kSmoke, kSmokeEnd);
      for (int k = 0; k < 3; ++k)
        add(i.pos + glm::vec3(0, 0.5f, 0), glm::vec3(0, 6.0f, 0) + jitter(12.0f), 1.5f * k5, 1.0f, 4.0f, kDust, kDustEnd);
      break;
    }
    default: {
      // Bombs: a fireball, a column of dust and smoke, earth thrown high. The incendiary one
      // spreads its fire wide and leaves it burning.
      const bool fire = i.type == sim::WeaponType::Zab500;
      const float k = i.type == sim::WeaponType::Fab500 || fire ? 1.4f : 1.0f;
      add(i.pos + glm::vec3(0, 3.0f, 0) * k, glm::vec3(0, 4.0f, 0), 2.0f, 9.0f * k, 2.5f, kFire, kFireEnd);
      for (int n = 0; n < (fire ? 10 : 3); ++n)
        add(i.pos + jitter(fire ? 30.0f : 8.0f) * glm::vec3(1, 0.2f, 1), glm::vec3(0, 3.0f, 0) + jitter(6.0f), 1.5f,
            5.0f * k, fire ? 4.0f : 1.5f, kFire, kFireEnd);
      for (int n = 0; n < 8; ++n)
        add(i.pos + glm::vec3(0, 3.0f, 0) + jitter(10.0f * k), glm::vec3(0, 7.0f, 0) + jitter(8.0f),
            (8.0f + random() * 5.0f) * k, 2.0f, 14.0f, fire ? kBlackSmoke : kSmoke, fire ? kBlackSmokeEnd : kSmokeEnd);
      for (int n = 0; n < 12; ++n)
        add(i.pos + glm::vec3(0, 1.0f, 0), glm::vec3(0, 22.0f, 0) + jitter(40.0f), 2.5f * k, 1.2f, 5.0f, kDust, kDustEnd);
      if (fire) fires_.push_back({i.pos});
      break;
    }
  }
}

void Effects::follow(const sim::Targets& targets) {
  for (Fire& f : fires_) {
    f.falling = f.target >= 0 && f.target < int(targets.list.size()) && targets.list[size_t(f.target)].fallingDrone();
    if (f.falling) f.pos = targets.list[size_t(f.target)].pos;
  }
}

void Effects::update(float dt, const std::vector<sim::Projectile>& shots) {
  for (Particle& p : parts_) {
    p.age += dt;
    p.pos += p.vel * dt;
    p.vel *= std::max(0.0f, 1.0f - p.drag * dt);  // the air slows the puffs down
  }
  parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.age >= p.life; }),
               parts_.end());
  for (Fire& f : fires_) {
    f.age += dt;
    f.timer -= dt;
    if (f.timer > 0.0f) continue;
    float strength = 1.0f - f.age / kFireTime;  // dies down
    f.timer = f.falling ? 0.03f : 0.12f + 0.25f * (1.0f - strength);  // falling: a close trail
    glm::vec3 p = f.pos + glm::vec3(random() - 0.5f, 0.0f, random() - 0.5f) * 2.0f;
    if (f.falling) {
      // A thin trail behind the falling wreck, its flame at the head.
      add(f.pos, kWind, 2.5f, 0.5f, 2.5f, kBlackSmoke * glm::vec4(1, 1, 1, 0.45f), kBlackSmokeEnd, 0.05f);
      add(f.pos, glm::vec3(0.0f), 0.15f, 0.6f, 0.3f, kFire, kFireEnd);
      continue;
    }
    add(p, glm::vec3(0, 3.0f + 2.0f * strength, 0) + kWind, 7.0f + 4.0f * strength, 1.2f, 5.0f + 6.0f * strength,
        kBlackSmoke * glm::vec4(1, 1, 1, 0.5f + 0.5f * strength), kBlackSmokeEnd, 0.05f);
    if (f.age < 60.0f) add(p, glm::vec3(0, 2.5f, 0), 0.5f, 1.0f, 0.4f, kFire, kFireEnd);
  }
  fires_.erase(std::remove_if(fires_.begin(), fires_.end(), [](const Fire& f) { return f.age > kFireTime; }),
               fires_.end());
  // Rocket motors leave a smoke trail while they burn.
  trailTimer_ += dt;
  if (trailTimer_ >= 0.02f) {
    trailTimer_ = 0.0f;
    for (const sim::Projectile& s : shots) {
      if ((s.type == sim::WeaponType::S8 && s.age < 0.8f) || (s.type == sim::WeaponType::S5 && s.age < 1.0f) ||
          s.type == sim::WeaponType::Shturm || (s.type == sim::WeaponType::R60 && s.age < 3.0f))
        // (from the tail: a missile's position is its nose, some 2 m ahead of its nozzle)
        add(s.law && glm::length(s.vel) > 1.0f ? s.pos - glm::normalize(s.vel) * 1.5f :
            glm::length(s.vel) > 1.0f && (s.type == sim::WeaponType::R60 || s.type == sim::WeaponType::Shturm)
                ? s.pos - glm::normalize(s.vel) * (s.type == sim::WeaponType::R60 ? 2.1f : 1.85f)
                : s.pos,
            glm::vec3(random() - 0.5f, random(), random() - 0.5f), 1.8f, 0.3f, 1.5f, kTrail, kTrailEnd);
    }
  }
}

void Effects::build(MeshBuilder& mb, const std::vector<sim::Projectile>& shotsAt, glm::vec3 eye, glm::vec3 shift) const {
  std::vector<sim::Projectile> shots = shotsAt;
  for (sim::Projectile& s : shots) s.pos += shift;
  for (const Particle& p : parts_) {
    float t = p.age / p.life;
    float r = p.size0 + (p.size1 - p.size0) * std::sqrt(t);
    puff(mb, p.pos, r, glm::mix(p.c0, p.c1, t), eye, p.size1 + p.life);
  }
  for (const sim::Projectile& s : shots) {
    float dist = glm::distance(s.pos, eye);
    float w = std::max(0.1f, dist * 0.0018f);  // stays visible far away
    glm::vec3 dir = glm::normalize(s.vel);
    if (s.type == sim::WeaponType::R60) {
      // Near, the missile is drawn with the scene (render::buildR60); its motor's flame, and
      // further off a streak for it.
      if (dist > kMissileModelRange) streak(mb, s.pos - dir * 2.0f, s.pos, std::max(0.1f, w), kRocketBody, eye);
      if (s.age < 3.0f) puff(mb, s.pos - dir * 2.2f, std::max(0.12f, w * 2.5f), kFire, eye, 0.0f);
    } else if (s.type == sim::WeaponType::Shturm) {
      // Near, drawn with the scene (render::buildShturm); the tracer in its tail (the sight
      // follows it), and further off a streak for it.
      if (dist > kMissileModelRange) streak(mb, s.pos - dir * 1.8f, s.pos, std::max(0.1f, w), kRocketBody, eye);
      puff(mb, s.pos - dir * 1.9f, std::max(0.08f, w * 1.5f), kFire, eye, 0.0f);
    } else if (s.law) {
      // A bowman's LAW rocket: near, drawn with the scene (render::buildLawRocket); a small
      // flame at its nozzle, further off a streak for it.
      if (dist > 400.0f) streak(mb, s.pos - dir * 0.55f, s.pos, std::max(0.06f, w), kRocketBody, eye);
      puff(mb, s.pos - dir * 0.58f, std::max(0.035f, dist * 0.0022f), kFire, eye, 0.0f);
    } else if (s.type == sim::WeaponType::S8 || s.type == sim::WeaponType::S5) {
      const bool s5 = s.type == sim::WeaponType::S5;
      streak(mb, s.pos - dir * (s5 ? 1.0f : 1.5f), s.pos, std::max(s5 ? 0.08f : 0.12f, w), kRocketBody, eye);
      if (s.age < (s5 ? 0.95f : 0.75f)) puff(mb, s.pos - dir * 1.7f, std::max(0.35f, w * 2.0f), kFire, eye, 0.0f);
    } else if (s.type == sim::WeaponType::Interceptor) {
      // Far off a small dark dart (its electric motors show no flame); nearer the drone itself
      // (drawn with the scene).
      if (dist > kInterceptorModelRange) streak(mb, s.pos - dir * 0.9f, s.pos, std::max(0.18f, w), kBombBody, eye);
    } else if (s.type == sim::WeaponType::Mine) {
      streak(mb, s.pos - dir * 0.3f, s.pos, std::max(0.12f, w * 0.7f), kBombBody, eye);
    } else if (sim::isBomb(s.type)) {
      const float len = s.type == sim::WeaponType::Fab500 || s.type == sim::WeaponType::Zab500 ? 2.3f : 1.6f;
      streak(mb, s.pos - dir * len, s.pos, std::max(0.35f, w), kBombBody, eye);
    } else if (s.tracer) {
      // The streak as the crew sees it, flying on with the gun: no longer than the round's flight
      // so far, so that one just fired comes out of the muzzle (not from inside the gun, nor from
      // where the muzzle was a moment ago). (Fired after the helicopter's step, the round's first
      // step is not one the muzzle made: one step less of the helicopter's travel.)
      const float a = std::min(0.012f, s.age);
      streak(mb, s.pos - (s.vel - s.carrier) * a - s.carrier * std::min(a, sim::kPhysicsDt), s.pos, w, kTracer, eye);
    }
  }
}

void Effects::enemy(const sim::EnemyEvent& e) {
  auto jitter = [&](float r) { return glm::vec3(random() - 0.5f, random() * 0.5f, random() - 0.5f) * r; };
  switch (e.type) {
    case sim::EnemyEvent::Muzzle:
      add(e.pos, glm::vec3(0.0f), 0.12f, 0.6f, 0.9f, kFire, kFireEnd, 0.0f);
      add(e.pos, glm::vec3(0, 1.0f, 0) + jitter(1.0f), 1.2f, 0.4f, 1.6f, kSmoke * glm::vec4(1, 1, 1, 0.5f), kSmokeEnd);
      break;
    case sim::EnemyEvent::Launch:
      // The back blast and a cloud of smoke where it was fired from.
      add(e.pos, glm::vec3(0.0f), 0.25f, 1.0f, 2.0f, kFire, kFireEnd, 0.0f);
      for (int k = 0; k < 4; ++k) add(e.pos + jitter(2.0f), glm::vec3(0, 1.0f, 0) + jitter(3.0f), 3.0f, 1.5f, 4.0f, kTrail, kTrailEnd);
      break;
    case sim::EnemyEvent::Burst:
      add(e.pos, glm::vec3(0.0f), 0.4f, 1.5f, e.kind == sim::EnemyRound::Shell ? 2.5f : 5.0f, kFire, kFireEnd, 0.0f);
      for (int k = 0; k < 3; ++k) add(e.pos + jitter(2.0f), jitter(4.0f), 3.0f, 1.5f, 5.0f, kBlackSmoke, kBlackSmokeEnd);
      break;
    case sim::EnemyEvent::Hit:
      add(e.pos, jitter(6.0f), 0.2f, 0.15f, 0.4f, kSpark, kFire, 0.5f);
      break;
  }
}

void Effects::updateEnemy(float dt, const std::vector<sim::EnemyShot>& shots, const std::vector<sim::Flare>& flares) {
  enemyTrail_ += dt;
  if (enemyTrail_ < 0.03f) return;
  enemyTrail_ = 0.0f;
  for (const sim::EnemyShot& s : shots)
    if (s.kind == sim::EnemyRound::Strela || s.kind == sim::EnemyRound::Rapier || s.kind == sim::EnemyRound::Rpg)
      add(s.pos, glm::vec3(random() - 0.5f, random(), random() - 0.5f), s.kind == sim::EnemyRound::Rpg ? 1.0f : 2.5f, 0.4f, 1.8f,
          kTrail, kTrailEnd);
  for (const sim::Flare& f : flares) add(f.pos, glm::vec3(0, 0.5f, 0), 1.5f, 0.6f, 1.6f, kTrail, kTrailEnd);
}

void Effects::buildEnemy(MeshBuilder& mb, const std::vector<sim::EnemyShot>& shots, const std::vector<sim::Flare>& flares,
                         glm::vec3 eye) const {
  // Their tracers burn green, as the Soviet and Chinese rounds the mujaheddin had.
  const glm::vec4 kGreenTracer{0.55f, 1.0f, 0.45f, 1.0f}, kRedTracer{1.0f, 0.45f, 0.25f, 1.0f};
  for (const sim::EnemyShot& s : shots) {
    const float w = std::max(0.1f, glm::distance(s.pos, eye) * 0.0018f);
    const glm::vec3 dir = glm::normalize(s.vel);
    switch (s.kind) {
      case sim::EnemyRound::Bullet:
      case sim::EnemyRound::Shell:
        if (s.tracer) streak(mb, s.pos - s.vel * 0.014f, s.pos, w * (s.kind == sim::EnemyRound::Shell ? 1.5f : 1.0f),
                             s.kind == sim::EnemyRound::Shell ? kRedTracer : kGreenTracer, eye);
        break;
      default:
        streak(mb, s.pos - dir * 1.2f, s.pos, std::max(0.1f, w), kRocketBody, eye);
        puff(mb, s.pos - dir * 1.3f, std::max(0.25f, w * 2.0f), kFire, eye, 0.0f);
        break;
    }
  }
  for (const sim::Flare& f : flares) puff(mb, f.pos, std::max(0.6f, glm::distance(f.pos, eye) * 0.003f), kSpark, eye, f.age * 7.0f);
}

void Effects::buildChaff(MeshBuilder& mb, const std::vector<sim::Chaff>& chaff, glm::vec3 eye) const {
  // As the original's: a burst of white strips, small triangles tumbling out every way (fixed
  // directions, so the cloud does not flicker), and a little grey cloud where the cartridge burst.
  const glm::vec4 kSmoke{0.55f, 0.56f, 0.57f, 0.8f};
  for (const sim::Chaff& c : chaff) {
    const float fade = std::clamp(1.0f - c.age / 6.0f, 0.0f, 1.0f);
    puff(mb, c.pos, 1.0f + 0.5f * c.age, kSmoke * glm::vec4(1, 1, 1, fade), eye, c.age * 0.3f, 10);
    const float spread = 4.0f + 14.0f * std::sqrt(c.age), size = 0.45f;
    for (int k = 0; k < 32; ++k) {
      const glm::vec3 dir = glm::normalize(glm::vec3(std::sin(k * 2.3f + 0.4f), std::cos(k * 1.7f) * 0.8f, std::sin(k * 3.1f + 1.0f)));
      const glm::vec3 p = c.pos + dir * spread * (0.35f + 0.65f * std::fabs(std::sin(k * 5.3f))) - glm::vec3(0.0f, 0.4f * c.age, 0.0f);
      const float t = k * 1.9f + c.age * (2.0f + float(k % 5));
      const glm::vec3 u = glm::normalize(glm::vec3(std::cos(t), std::sin(t * 0.7f + k), std::sin(t)));
      glm::vec3 v = glm::cross(u, dir);
      v = glm::length(v) > 1e-3f ? glm::normalize(v) : glm::vec3(0, 1, 0);
      const float shade = 0.75f + 0.25f * std::fabs(std::cos(t));
      mb.tri(p + u * size, p - u * (0.5f * size) + v * (0.6f * size), p - u * (0.5f * size) - v * (0.6f * size),
             glm::vec4(glm::vec3(0.95f, 0.96f, 0.97f) * shade, fade));
    }
  }
}

void Effects::buildLights(MeshBuilder& mb, const std::vector<glm::vec4>& lights, glm::vec3 eye, float on) const {
  if (on <= 0.0f) return;
  for (const glm::vec4& l : lights) {
    const glm::vec3 p{l};
    const float d = glm::distance(p, eye);
    if (d > 12000.0f) continue;
    const glm::vec4 c = l.w > 0.5f ? glm::vec4(0.35f, 1.0f, 0.45f, on) : glm::vec4(1.0f, 0.97f, 0.88f, on);
    puff(mb, p, std::max(0.12f, d * 0.0017f), c, eye, 0.0f);
  }
}

void Effects::buildEngineGlows(MeshBuilder& mb, const sim::Targets& targets, const std::vector<glm::mat4>& hinds, glm::vec3 eye,
                               float dark, bool nightVision) const {
  if (dark <= 0.0f) return;
  enum class Engine { Piston, Shaft, Jet };
  auto glow = [&](glm::vec3 p, Engine e) {
    if (e != Engine::Jet && !nightVision) return;
    const float d = glm::distance(p, eye);
    if (d > (e == Engine::Piston ? 2500.0f : nightVision ? 8000.0f : 3000.0f)) return;
    if (!nightVision) {
      puff(mb, p, std::max(0.12f, d * 0.0017f), glm::vec4(1.0f, 0.45f, 0.12f, dark * 0.7f), eye, 0.0f, 24);
      return;
    }
    // White: the night vision turns it its own colour, as bright as anything in the view.
    const float r = e == Engine::Jet ? std::max(0.35f, d * 0.004f) : e == Engine::Shaft ? std::max(0.3f, d * 0.003f)
                                                                                     : std::max(0.3f, d * 0.0022f);
    puff(mb, p, r, glm::vec4(1.0f, 1.0f, 1.0f, dark * (e == Engine::Piston ? 0.75f : 1.0f)), eye, 0.0f, 24);
    // The intensifier's bloom round the hottest: a soft halo a few times its size.
    if (e != Engine::Piston) puff(mb, p, r * (e == Engine::Jet ? 3.5f : 2.5f), glm::vec4(1.0f, 1.0f, 1.0f, dark * 0.3f), eye, 0.0f, 24);
  };
  // A Mi-24's exhausts: either side of the engine cowlings, behind the rotor's mast.
  for (const glm::mat4& m : hinds)
    for (float x : {-1.05f, 1.05f}) glow(glm::vec3(m * glm::vec4(x, 1.1f, 0.8f, 1.0f)), Engine::Shaft);
  for (const sim::Vehicle& v : targets.list) {
    if (v.destroyed || v.aboard || !v.moving() || v.delay > 0.0f) continue;
    const sim::VehicleType& t = sim::vehicleType(v.kind);
    const glm::mat4 m = v.toWorld();
    const glm::vec3 lo = t.lo, size = t.hi - t.lo;
    switch (t.cls) {
      case sim::UnitClass::Drone:
        // The nozzle at the tail; the piston engine (its cylinders and exhausts) at the back.
        if (sim::isJetDrone(v.kind))
          glow(glm::vec3(m * glm::vec4(0.0f, 0.0f, sim::isCruiseMissile(v.kind) ? 2.5f : 1.95f, 1.0f)), Engine::Jet);
        else glow(glm::vec3(m * glm::vec4(0.0f, -0.05f, 1.62f, 1.0f)), Engine::Piston);
        break;
      case sim::UnitClass::Helicopter:
        // Two exhausts high on the sides, a little behind the middle.
        for (float x : {-1.0f, 1.0f})
          glow(glm::vec3(m * glm::vec4(x, lo.y + 0.5f * size.y, lo.z + 0.45f * size.z, 1.0f)), Engine::Shaft);
        break;
      case sim::UnitClass::Aircraft:
        if (t.name == "AN-12") {
          // Four turboprops along the wings.
          for (float f : {-0.42f, -0.22f, 0.22f, 0.42f})
            glow(glm::vec3(m * glm::vec4(f * size.x, lo.y + 0.55f * size.y, lo.z + 0.45f * size.z, 1.0f)), Engine::Shaft);
        } else {
          glow(glm::vec3(m * glm::vec4(0.0f, lo.y + 0.35f * size.y, t.hi.z, 1.0f)), Engine::Jet);
        }
        break;
      default: break;
    }
  }
}

void Effects::engineSmoke(glm::vec3 pos, glm::vec3 vel, bool burning) {
  if (random() > 0.35f) return;
  if (burning && random() < 0.5f) add(pos, vel * 0.8f, 0.3f, 0.4f, 0.9f, kFire, kFireEnd, 2.0f);
  add(pos, vel * 0.6f + glm::vec3(0, 1.0f, 0), 3.5f, 0.6f, 4.0f, burning ? kBlackSmoke : kSmoke, burning ? kBlackSmokeEnd : kSmokeEnd, 1.2f);
}

}  // namespace render
