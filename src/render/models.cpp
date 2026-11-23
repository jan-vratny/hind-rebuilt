#include "render/models.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "render/drones.hpp"

namespace render {

namespace {

const glm::vec3 kTan{0.55f, 0.47f, 0.31f};
const glm::vec3 kGreen{0.29f, 0.35f, 0.21f};
const glm::vec3 kBelly{0.58f, 0.64f, 0.67f};
const glm::vec3 kGlass{0.20f, 0.28f, 0.34f};
const glm::vec3 kDark{0.17f, 0.18f, 0.17f};
const glm::vec3 kTyre{0.12f, 0.12f, 0.12f};
const glm::vec3 kMetal{0.36f, 0.37f, 0.37f};

// Two-tone camouflage in blobs, picked per facet so the pattern reads as patches of polygons.
glm::vec3 camo(glm::vec3 c) {
  float v = std::sin(c.z * 0.55f + 1.4f * std::sin(c.y * 1.1f + c.x * 0.7f)) +
            std::sin(c.z * 0.23f - c.y * 0.9f + c.x * 0.45f + 2.0f);
  return v > 0.25f ? kGreen : kTan;
}

struct Ctx {
  MeshBuilder& mb;
  const glm::mat4& m;
  glm::vec3 p(glm::vec3 v) const { return glm::vec3(m * glm::vec4(v, 1.0f)); }

  void box(glm::vec3 lo, glm::vec3 hi, glm::vec3 col) const { mb.box(m, lo, hi, col); }
  void cyl(glm::vec3 a, glm::vec3 b, float ra, float rb, int sides, glm::vec3 col, glm::vec3 cap) const {
    mb.cylinder(m, a, b, ra, rb, sides, col, cap);
  }
  void cyl(glm::vec3 a, glm::vec3 b, float r, int sides, glm::vec3 col) const {
    mb.cylinder(m, a, b, r, r, sides, col);
  }
  void slab(const std::vector<glm::vec3>& o, glm::vec3 thick, glm::vec3 col) const { mb.slab(m, o, thick, col); }

  // Square beam along a segment.
  void beam(glm::vec3 a, glm::vec3 b, float t, glm::vec3 col) const {
    glm::vec3 d = glm::normalize(b - a);
    glm::vec3 u = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 s1 = glm::normalize(glm::cross(d, u)) * t, s2 = glm::normalize(glm::cross(d, s1)) * t;
    const glm::vec3 c[8] = {
        p(a - s1 - s2), p(a + s1 - s2), p(a + s1 + s2), p(a - s1 + s2),
        p(b - s1 - s2), p(b + s1 - s2), p(b + s1 + s2), p(b - s1 + s2),
    };
    mb.hexa(c, col);
  }
};

// Our own simple Mi-24V, drawn when the original game's model is not there: the fuselage lofted
// through stations along it (z, half width, bottom, top; the cross-section a rounded box), the
// two canopies glazed on it, the engines on its back, wings, tail and gear from simple parts.
struct Station {
  float z, halfW, bottom, top;
};
const Station kFuselage[] = {
    {-6.70f, 0.12f, -0.40f, -0.22f}, {-6.20f, 0.36f, -1.05f, -0.12f}, {-5.40f, 0.58f, -1.45f, 0.08f},
    {-4.40f, 0.72f, -1.45f, 0.25f},  {-3.80f, 0.78f, -1.35f, 0.70f},  {-2.90f, 0.86f, -1.32f, 1.18f},
    {-2.00f, 0.94f, -1.30f, 1.50f},  {2.00f, 0.98f, -1.30f, 1.60f},   {3.10f, 0.78f, -1.02f, 1.62f},
    {4.20f, 0.46f, -0.30f, 1.45f},   {5.20f, 0.30f, 0.70f, 1.38f},    {10.20f, 0.24f, 0.80f, 1.40f},
    {10.90f, 0.20f, 1.00f, 1.50f},
};
constexpr int kRingPoints = 12;

// The rounded box of a station, from the bottom round the right side to the top and back down
// the left.
std::vector<glm::vec3> ring(const Station& s) {
  std::vector<glm::vec3> r;
  const float cy = (s.top + s.bottom) * 0.5f, hy = (s.top - s.bottom) * 0.5f;
  for (int i = 0; i < kRingPoints; ++i) {
    const float a = -1.5707963f + (i + 0.5f) * 6.2831853f / kRingPoints;
    const float c = std::cos(a), sn = std::sin(a);
    // A superellipse (n 3): flatter sides than an ellipse.
    auto f = [](float v) { return std::copysign(std::pow(std::fabs(v), 2.0f / 3.0f), v); };
    r.push_back({s.halfW * f(c), cy + hy * f(sn), s.z});
  }
  return r;
}

// The glazing of the gunner's and the pilot's cockpits on the fuselage (side outline z, y).
bool onCanopy(glm::vec3 c) {
  if (std::fabs(c.x) > 0.75f) return false;
  if (c.z > -5.9f && c.z < -4.35f && c.y > -0.30f + (c.z + 5.9f) * -0.05f) return true;  // gunner
  return c.z > -4.15f && c.z < -2.75f && c.y > 0.45f;                                       // pilot
}

}  // namespace

void buildLandingGear(MeshBuilder& mb, const glm::mat4& m) {
  Ctx c{mb, m};
  // The nose gear's twin wheels, the main gear's on struts out of the sides of the fuselage.
  c.beam({0.0f, -1.20f, -4.25f}, {0.0f, -1.72f, -4.35f}, 0.05f, kMetal);
  for (float x : {-0.20f, 0.20f}) c.cyl({x - 0.07f, -1.74f, -4.35f}, {x + 0.07f, -1.74f, -4.35f}, 0.28f, 10, kTyre);
  for (float sx : {-1.0f, 1.0f}) {
    c.beam({sx * 0.95f, -0.90f, 0.40f}, {sx * 1.50f, -1.55f, 1.15f}, 0.06f, kMetal);
    c.beam({sx * 0.95f, -1.15f, 1.40f}, {sx * 1.50f, -1.55f, 1.15f}, 0.05f, kMetal);
    c.cyl({sx * 1.42f, -1.60f, 1.15f}, {sx * 1.70f, -1.60f, 1.15f}, 0.42f, 12, kTyre);
  }
}

void buildChinGun(MeshBuilder& mb, const glm::mat4& m) {
  // The Yak-B's four barrels out of the turret, along -z from the pivot.
  const glm::vec3 p = sim::Weapons::kTurretPivot;
  Ctx c{mb, m};
  c.cyl(p + glm::vec3(0.0f, 0.0f, -0.15f), p + glm::vec3(0.0f, 0.0f, -0.45f), 0.09f, 0.08f, 8, kDark, kDark);
  for (int k = 0; k < 4; ++k) {
    const float a = 0.785398f + k * 1.570796f;
    const glm::vec3 o{std::cos(a) * 0.04f, std::sin(a) * 0.04f, 0.0f};
    c.cyl(p + o + glm::vec3(0, 0, -0.45f), p + o + glm::vec3(0, 0, -1.05f), 0.016f, 5, kDark);
  }
}

void buildMi24(MeshBuilder& mb, const glm::mat4& m, Mi24Parts parts, bool stores) {
  Ctx c{mb, m};
  std::vector<std::vector<glm::vec3>> rings;
  for (const Station& s : kFuselage) rings.push_back(ring(s));
  mb.loft(m, rings, [](int, int, glm::vec3 p) {
    if (onCanopy(p)) return kGlass;
    if (p.y < -1.0f || (p.y < 0.75f && p.z > 4.0f && std::fabs(p.x) < 0.2f)) return kBelly;
    return camo(p);
  });
  // The chin turret's ball under the gunner.
  const glm::vec3 pivot = sim::Weapons::kTurretPivot;
  c.cyl(pivot + glm::vec3(0.0f, 0.22f, 0.0f), pivot + glm::vec3(0.0f, -0.12f, 0.0f), 0.30f, 0.26f, 8, kDark, kDark);
  // The engines (TV3-117) side by side on the back, their intakes forward, the gearbox between
  // them under the mast, the exhausts out to the sides behind.
  for (float sx : {-1.0f, 1.0f}) {
    c.cyl({sx * 0.48f, 1.62f, -2.10f}, {sx * 0.48f, 1.62f, 1.70f}, 0.36f, 0.36f, 8, kTan, kDark);
    c.cyl({sx * 0.48f, 1.62f, -2.10f}, {sx * 0.48f, 1.62f, -2.40f}, 0.36f, 0.27f, 8, kGreen, kDark);
    c.cyl({sx * 0.62f, 1.58f, 1.70f}, {sx * 1.05f, 1.45f, 2.50f}, 0.27f, 0.24f, 8, kDark, kDark);
  }
  c.box({-0.40f, 1.55f, -0.70f}, {0.40f, 2.00f, 0.90f}, kGreen);
  c.cyl({0.0f, 2.00f, 0.0f}, kMainRotorHub, 0.14f, 6, kMetal);
  // The wings: a little anhedral, the pylons under them, plates at the tips for the 9M114 rails.
  for (float sx : {-1.0f, 1.0f}) {
    c.slab({{sx * 0.90f, -0.40f, -0.05f}, {sx * 3.40f, -0.55f, 0.75f}, {sx * 3.40f, -0.55f, 1.60f}, {sx * 0.90f, -0.40f, 2.10f}},
           {0.0f, 0.12f, 0.0f}, kGreen);
    for (sim::Pylon pl : {sim::Pylon::Inner, sim::Pylon::Outer}) {
      const float x = sx * std::fabs(sim::Weapons::pylonPoint(pl).x);
      c.box({x - 0.05f, -0.42f, 0.30f}, {x + 0.05f, -0.38f, 1.60f}, kDark);
    }
    c.slab({{sx * 3.40f, -0.50f, 0.70f}, {sx * 3.40f, -0.50f, 1.70f}, {sx * 3.40f, -0.90f, 1.70f}, {sx * 3.40f, -0.90f, 0.70f}},
           {0.08f, 0.0f, 0.0f}, kGreen);
  }
  // The tail: the fin swept back and leaning, the stabilisers under it.
  c.slab({{0.0f, 1.20f, 9.20f}, {0.0f, 1.20f, 10.90f}, {0.0f, 2.85f, 11.70f}, {0.0f, 2.85f, 11.10f}}, {0.10f, 0.0f, 0.0f}, kGreen);
  c.cyl({-0.08f, kTailRotorHub.y, kTailRotorHub.z}, kTailRotorHub, 0.10f, 6, kMetal);
  for (float sx : {-1.0f, 1.0f})
    c.slab({{sx * 0.20f, 1.10f, 8.40f}, {sx * 1.90f, 1.10f, 8.90f}, {sx * 1.90f, 1.10f, 9.40f}, {sx * 0.20f, 1.10f, 9.50f}},
           {0.0f, 0.06f, 0.0f}, kGreen);
  if (stores) {
    for (int side = 0; side < 2; ++side) {
      buildStore(mb, m, sim::Store::B8V20, sim::Pylon::Inner, side);
      buildStore(mb, m, sim::Store::B8V20, sim::Pylon::Outer, side);
      buildStore(mb, m, sim::Store::Shturm, sim::Pylon::Wingtip, side);
    }
  }
  if (parts == Mi24Parts::Parked) {
    buildChinGun(mb, m);
    buildLandingGear(mb, m);
    buildMainRotor(mb, m * glm::translate(glm::mat4(1.0f), kMainRotorHub) *
                           glm::rotate(glm::mat4(1.0f), 0.3f, glm::vec3(0, 1, 0)), 0.55f);
    buildTailRotor(mb, m * glm::translate(glm::mat4(1.0f), kTailRotorHub));
  }
}

void buildStore(MeshBuilder& mb, const glm::mat4& m, sim::Store store, sim::Pylon pylon, int side, int rounds) {
  const float sx = side ? 1.0f : -1.0f;
  glm::vec3 axis = sim::Weapons::pylonPoint(pylon);
  axis.x = std::fabs(axis.x) * sx;
  const glm::vec3 kOlive{0.31f, 0.35f, 0.23f}, kLight{0.62f, 0.64f, 0.62f}, kWhite{0.80f, 0.81f, 0.79f};
  const glm::mat4 at = m * glm::translate(glm::mat4(1.0f), axis);
  Ctx c{mb, at};
  // A rack from the bottom of the pylon (y -0.40 in the airframe) down to the store's top.
  auto rack = [&](float top, float z0, float z1) {
    const float y0 = -0.40f - axis.y;
    if (pylon != sim::Pylon::Wingtip && top < y0) c.box({-0.05f, top, z0}, {0.05f, y0 + 0.02f, z1}, kMetal);
  };
  // A bomb: cylindrical body, nose and tail cones, a box tail of four fins.
  auto bomb = [&](float r, float nose, float body0, float body1, float tail, glm::vec3 col) {
    const float y = -0.40f - axis.y - r - 0.06f;
    const glm::vec3 o{0.0f, y, 0.0f};
    c.cyl(o + glm::vec3(0, 0, nose), o + glm::vec3(0, 0, body0), 0.03f, r, 8, col, col);
    c.cyl(o + glm::vec3(0, 0, body0), o + glm::vec3(0, 0, body1), r, r, 8, col, col);
    c.cyl(o + glm::vec3(0, 0, body1), o + glm::vec3(0, 0, tail), r, r * 0.45f, 8, col, kDark);
    for (int k = 0; k < 4; ++k) {
      const float a = 0.785398f + k * 1.570796f;
      const glm::vec3 d{std::cos(a), std::sin(a), 0.0f};
      c.slab({o + d * (r * 0.4f) + glm::vec3(0, 0, tail - 0.05f), o + d * (r * 1.15f) + glm::vec3(0, 0, tail),
              o + d * (r * 1.15f) + glm::vec3(0, 0, tail - 0.35f), o + d * (r * 0.8f) + glm::vec3(0, 0, body1 - 0.05f)},
             glm::vec3(d.y, -d.x, 0.0f) * 0.02f, col);
    }
    rack(y + r, body0 + 0.1f, body1 - 0.1f);
  };
  switch (store) {
    case sim::Store::Interceptor: {
      // A frame as the Ukrainian Mi-8s and Mi-17s carry them, but under the wing between the
      // pylons (it takes the outer ones too) and back towards its trailing edge, where the wing
      // hides it from the door gun above: a cross beam under both pylons, a post at each end
      // leaning back and down, a cross bar per tier, and for each drone two prongs pointing
      // forward that a Litavr is slid onto (left sticking out once it is gone). Two tiers of
      // three, the lower further back (clear of the wheel), the middle one of each further
      // forward so the propellers clear each other; the bottom ones go first. Laid out in the
      // airframe's axes (x out from the fuselage), sim::Weapons::kInterceptorRackPoint in the middle.
      auto L = [&](float x, float y, float z) { return glm::vec3(sx * x - axis.x, y - axis.y, z); };
      c.box(L(1.28f, -0.50f, 1.53f), L(2.90f, -0.46f, 1.57f), kMetal);
      const float tierY[2] = {-0.85f, -1.53f}, barZ[2] = {2.2f, 3.1f}, sideZ[2] = {1.6f, 2.5f};
      for (float x : {1.28f, 2.90f}) {
        c.beam(L(x, -0.48f, 1.55f), L(x, tierY[0] - 0.04f, barZ[0]), 0.02f, kMetal);
        c.beam(L(x, tierY[0] - 0.04f, barZ[0]), L(x, tierY[1] - 0.06f, barZ[1]), 0.02f, kMetal);
      }
      for (int tier = 0; tier < 2; ++tier) {
        const float y = tierY[tier];
        c.box(L(1.28f, y - 0.06f, barZ[tier] - 0.02f), L(2.90f, y - 0.02f, barZ[tier] + 0.02f), kMetal);
        for (int col = 0; col < 3; ++col) {
          const float x = col == 0 ? 2.55f : col == 1 ? 2.10f : 1.65f;  // the outer one first
          const float z = sideZ[tier] - (col == 1 ? 0.3f : 0.0f);
          for (float px : {x - 0.03f, x + 0.03f}) c.cyl(L(px, y - 0.04f, barZ[tier]), L(px, y - 0.04f, z - 0.15f), 0.008f, 4, kMetal);
          if (rounds < 0 || tier * 3 + col < rounds) buildLitavr(mb, at * glm::translate(glm::mat4(1.0f), L(x, y, z)), 1.2f);
        }
      }
      break;
    }
    case sim::Store::None:
    case sim::Store::Kmgu:
      if (store == sim::Store::Kmgu) {
        // KMGU-2: a long box with a rounded nose.
        c.box({-0.19f, -0.10f, -0.3f}, {0.19f, 0.30f, 2.6f}, kLight);
        c.cyl({0.0f, 0.10f, -0.3f}, {0.0f, 0.10f, -0.55f}, 0.2f, 0.08f, 8, kLight, kLight);
        rack(0.30f, 0.3f, 1.8f);
      }
      break;
    case sim::Store::B8V20:
      // B-8V20: a fat pod, a blunt front of tubes, a cone behind.
      c.cyl({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.70f}, 0.25f, 0.25f, 10, kGreen, kDark);
      c.cyl({0.0f, 0.0f, 1.70f}, {0.0f, 0.0f, 2.10f}, 0.25f, 0.12f, 10, kGreen, kDark);
      rack(0.25f, 0.4f, 1.4f);
      break;
    case sim::Store::Shturm:
      // 9M114: two launch tubes side by side under their rail.
      for (float tx : {-0.13f, 0.13f}) c.cyl({tx, 0.0f, 0.0f}, {tx, 0.0f, 2.30f}, 0.12f, 0.12f, 8, kGreen, kDark);
      c.box({-0.05f, 0.10f, 0.4f}, {0.05f, 0.22f, 1.9f}, kMetal);
      if (pylon != sim::Pylon::Wingtip) rack(0.22f, 0.6f, 1.6f);
      break;
    case sim::Store::UB32:
      // UB-32A: a slimmer pod with a blunt front full of tubes.
      c.cyl({0.0f, -0.05f, -0.05f}, {0.0f, -0.05f, 1.6f}, 0.2f, 0.2f, 10, kGreen, kDark);
      c.cyl({0.0f, -0.05f, 1.6f}, {0.0f, -0.05f, 2.0f}, 0.2f, 0.1f, 10, kGreen, kDark);
      rack(0.15f, 0.3f, 1.3f);
      break;
    case sim::Store::Upk23:
      // UPK-23/250: a 3 m pod with the twin GSh-23L barrels sticking out of its nose.
      c.cyl({0, 0, -0.55f}, {0, 0, 2.3f}, 0.21f, 0.21f, 8, kGreen, kDark);
      c.cyl({0, 0, -0.75f}, {0, 0, -0.55f}, 0.12f, 0.21f, 8, kGreen, kDark);
      rack(0.21f, 0.0f, 0.4f);
      for (float bx : {-0.04f, 0.04f}) c.cyl({bx, -0.06f, -0.95f}, {bx, -0.06f, -0.6f}, 0.025f, 4, kDark);
      break;
    case sim::Store::Fab250: bomb(0.163f, -0.30f, 0.15f, 1.15f, 1.55f, kOlive); break;
    case sim::Store::Ofab250: bomb(0.163f, -0.45f, 0.10f, 1.10f, 1.50f, glm::vec3(0.40f, 0.42f, 0.30f)); break;
    case sim::Store::Fab500: bomb(0.20f, -0.45f, 0.05f, 1.45f, 2.00f, kOlive); break;
    case sim::Store::Zab500: bomb(0.20f, -0.45f, 0.05f, 1.45f, 2.00f, glm::vec3(0.36f, 0.30f, 0.22f)); break;
    case sim::Store::R60: {
      // R-60 on its APU-60 launcher: the launcher's body under the pylon, its rail along the
      // missile's back.
      c.box({-0.05f, 0.14f, -0.15f}, {0.05f, 0.40f, 1.25f}, kMetal);
      c.box({-0.025f, 0.104f, -0.45f}, {0.025f, 0.14f, 1.05f}, kMetal);
      buildR60(mb, at * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.04f, 0.30f)));
      break;
    }
  }
}

void buildMainRotor(MeshBuilder& mb, const glm::mat4& m, float droop) {
  Ctx c{mb, m};
  const glm::vec3 col{0.16f, 0.17f, 0.17f};
  for (int i = 0; i < 5; ++i) {
    float a = i * 2.0f * 3.14159265f / 5.0f;
    glm::vec3 dir{std::cos(a), 0.0f, std::sin(a)}, side{-std::sin(a), 0.0f, std::cos(a)};
    float tip = 8.65f;
    glm::vec3 w = side * 0.29f;
    // Long and limp: a stopped blade sags more and more towards its tip.
    const int segments = 6;
    const auto along = [&](int s) {
      const float t = float(s) / float(segments);
      return dir * (0.4f + (tip - 0.4f) * t) + glm::vec3(0.0f, -droop * t * t, 0.0f);
    };
    for (int s = 0; s < segments; ++s) mb.quad(c.p(along(s) - w), c.p(along(s + 1) - w), c.p(along(s + 1) + w), c.p(along(s) + w), col);
  }
}

void buildMainRotorDisc(MeshBuilder& mb) {
  const int n = 48;
  const glm::vec4 col{0.08f, 0.09f, 0.09f, 0.16f};
  for (int i = 0; i < n; ++i) {
    float a0 = i * 2.0f * 3.14159265f / n, a1 = (i + 1) * 2.0f * 3.14159265f / n;
    mb.tri({0, 0, 0}, {8.65f * std::cos(a0), 0, 8.65f * std::sin(a0)},
           {8.65f * std::cos(a1), 0, 8.65f * std::sin(a1)}, col);
  }
}

void buildTailRotor(MeshBuilder& mb, const glm::mat4& m) {
  Ctx c{mb, m};
  const glm::vec3 col{0.18f, 0.18f, 0.18f};
  for (int i = 0; i < 3; ++i) {
    float a = i * 2.0f * 3.14159265f / 3.0f;
    glm::vec3 dir{0.0f, std::cos(a), std::sin(a)}, side{0.0f, -std::sin(a), std::cos(a)};
    glm::vec3 r0 = dir * 0.15f, r1 = dir * 1.95f, w = side * 0.14f;
    mb.quad(c.p(r0 - w), c.p(r1 - w), c.p(r1 + w), c.p(r0 + w), col);
  }
}

void buildDoorView(MeshBuilder& mb, int side, glm::vec3 gunDir) {
  const float sx = side ? 1.0f : -1.0f;
  const glm::vec3 wall{0.30f, 0.33f, 0.28f}, floor{0.19f, 0.20f, 0.18f}, frame{0.12f, 0.13f, 0.12f};
  // The cabin: floor, roof, the side and the ends (body x mirrored on the left).
  const float x0 = -sx * 1.1f, x1 = sx * 1.28f, y0 = -0.95f, y1 = 0.85f, z0 = -3.0f, z1 = 1.6f;
  // The window: fore and aft of the gun, from about the gunner's waist to near the roof.
  const float wz0 = -1.45f, wz1 = 0.25f, wy0 = -0.5f, wy1 = 0.7f;
  auto side_ = [&](float x, float za, float zb, float ya, float yb, glm::vec3 c) {
    mb.quad({x, ya, za}, {x, ya, zb}, {x, yb, zb}, {x, yb, za}, c);
  };
  mb.quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, floor);
  mb.quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, wall * 0.8f);
  mb.quad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, wall * 0.9f);
  mb.quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, wall * 0.9f);
  side_(x1, z0, wz0, y0, y1, wall);
  side_(x1, wz1, z1, y0, y1, wall);
  side_(x1, wz0, wz1, y0, wy0, wall);
  side_(x1, wz0, wz1, wy1, y1, wall);
  // The window's frame, the thickness of the side.
  const float xo = x1 + sx * 0.08f;
  mb.quad({x1, wy0, wz0}, {xo, wy0, wz0}, {xo, wy0, wz1}, {x1, wy0, wz1}, frame);
  mb.quad({x1, wy1, wz0}, {xo, wy1, wz0}, {xo, wy1, wz1}, {x1, wy1, wz1}, frame);
  mb.quad({x1, wy0, wz0}, {xo, wy0, wz0}, {xo, wy1, wz0}, {x1, wy1, wz0}, frame);
  mb.quad({x1, wy0, wz1}, {xo, wy0, wz1}, {xo, wy1, wz1}, {x1, wy1, wz1}, frame);

  buildDoorGun(mb, side, gunDir);
}

void buildDoorGun(MeshBuilder& mb, int side, glm::vec3 gunDir) {
  // The gun on its post by the window, turning about the top of the post.
  buildDoorGun(mb, side, gunDir, sim::Weapons::kDoorPoint * glm::vec3(side ? 1.0f : -1.0f, 1.0f, 1.0f), true);
}

void buildDoorGun(MeshBuilder& mb, int side, glm::vec3 gunDir, glm::vec3 pivot, bool post, bool ammoBox) {
  const float sx = side ? 1.0f : -1.0f;
  const float y0 = -0.95f;
  if (post) mb.cylinder(glm::mat4(1.0f), {pivot.x, y0, pivot.z}, pivot - glm::vec3(0.0f, 0.07f, 0.0f), 0.03f, 0.025f, 6, kDark);
  const glm::vec3 d = glm::length(gunDir) > 0.1f ? glm::normalize(gunDir) : glm::vec3(sx, 0.0f, 0.0f);
  const glm::mat4 gun = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), std::atan2(-d.x, -d.z), glm::vec3(0, 1, 0)) *
                        glm::rotate(glm::mat4(1.0f), std::asin(std::clamp(d.y, -1.0f, 1.0f)), glm::vec3(1, 0, 0));
  const glm::vec3 dark{0.14f, 0.15f, 0.14f}, black{0.08f, 0.08f, 0.08f}, cover{0.31f, 0.34f, 0.37f},
      steel{0.30f, 0.31f, 0.31f}, olive{0.30f, 0.33f, 0.22f};
  // Gun axes: barrels to -z. As the door gunners' pictures show it from behind: the receiver
  // with its lighter top cover, the block of four barrels with its clamps and the flash hider,
  // the sight high on its bracket over the receiver, the belt chute down its left side, the
  // spade grips at the back.
  mb.box(gun, {-0.06f, -0.075f, -0.16f}, {0.06f, 0.05f, 0.2f}, dark);
  mb.box(gun, {-0.066f, 0.05f, -0.13f}, {0.066f, 0.068f, 0.17f}, cover);
  mb.cylinder(gun, {0.0f, -0.01f, -0.16f}, {0.0f, -0.01f, -0.27f}, 0.048f, 0.044f, 10, dark);
  for (int k = 0; k < 4; ++k) {
    const float a = 0.785398f + float(k) * 1.570796f;
    const float bx = 0.022f * std::cos(a), by = -0.01f + 0.022f * std::sin(a);
    mb.cylinder(gun, {bx, by, -0.27f}, {bx, by, -0.6f}, 0.0105f, 0.0105f, 6, steel);
  }
  for (float z : {-0.38f, -0.5f}) mb.cylinder(gun, {0.0f, -0.01f, z}, {0.0f, -0.01f, z - 0.025f}, 0.037f, 0.037f, 10, dark);
  mb.cylinder(gun, {0.0f, -0.01f, -0.59f}, {0.0f, -0.01f, -0.68f}, 0.04f, 0.036f, 10, black);
  // The sight on its bracket: two flat bars up from the cover, and over them an open frame, two
  // thin rings on thin rods: right under the gunner's eye and his aiming ring on the screen, a
  // solid tube would hide the drone he aims at.
  for (float bx : {-0.024f, 0.024f}) mb.box(gun, {bx - 0.005f, 0.068f, -0.03f}, {bx + 0.005f, 0.19f, -0.008f}, steel);
  mb.box(gun, {-0.03f, 0.125f, -0.03f}, {0.03f, 0.137f, -0.008f}, steel);
  const float sightY = 0.215f, sightR = 0.03f, wire = 0.0025f;
  const auto onRing = [&](float a, float z) { return glm::vec3(sightR * std::sin(a), sightY - sightR * std::cos(a), z); };
  for (float z : {-0.15f, 0.085f})
    for (int k = 0; k < 12; ++k)
      mb.cylinder(gun, onRing(float(k) * 0.5236f, z), onRing(float(k + 1) * 0.5236f, z), wire, wire, 4, black);
  for (float a : {0.0f, 2.0944f, -2.0944f}) mb.cylinder(gun, onRing(a, -0.15f), onRing(a, 0.085f), wire, wire, 4, black);
  // The belt chute: links of metal from the receiver's left side down and out.
  const glm::vec3 chute[] = {{-0.06f, -0.02f, 0.0f}, {-0.12f, -0.07f, 0.02f}, {-0.16f, -0.16f, 0.03f}, {-0.17f, -0.28f, 0.03f}};
  for (int k = 0; k + 1 < 4; ++k) mb.cylinder(gun, chute[k], chute[k + 1], 0.028f, 0.028f, 4, olive);
  // The spade grips and the trigger bar between them, on the back plate.
  mb.box(gun, {-0.075f, -0.08f, 0.2f}, {0.075f, 0.04f, 0.225f}, black);
  for (float gx : {-0.075f, 0.075f}) mb.cylinder(gun, {gx, -0.09f, 0.26f}, {gx, 0.05f, 0.26f}, 0.016f, 0.016f, 6, black);
  for (float gy : {-0.09f, 0.05f}) mb.box(gun, {-0.075f, gy - 0.008f, 0.225f}, {0.075f, gy + 0.008f, 0.26f}, black);
  mb.box(gun, {-0.03f, -0.02f, 0.235f}, {0.03f, 0.0f, 0.255f}, steel);
  // The ammunition box on the floor by the post, the belt going up to the chute.
  if (!ammoBox) return;
  const glm::vec3 boxAt{pivot.x - sx * 0.12f, y0, pivot.z + 0.25f};
  mb.box(glm::mat4(1.0f), boxAt + glm::vec3(-0.13f, 0.0f, -0.17f), boxAt + glm::vec3(0.13f, 0.3f, 0.17f), olive);
  mb.box(glm::mat4(1.0f), boxAt + glm::vec3(-0.135f, 0.3f, -0.175f), boxAt + glm::vec3(0.135f, 0.32f, 0.175f), olive * 0.8f);
}

}  // namespace render
