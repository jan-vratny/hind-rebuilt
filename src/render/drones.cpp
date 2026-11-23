#include "render/drones.hpp"

#include <cmath>
#include <vector>

namespace render {

namespace {

glm::vec3 xf(const glm::mat4& m, glm::vec3 v) { return glm::vec3(m * glm::vec4(v, 1.0f)); }

// The airframe both share: the fuselage, the wing with its elevons and the fins.
void airframe(MeshBuilder& mb, const glm::mat4& m, glm::vec3 black, glm::vec3 dark) {
  // Fuselage: a slightly flattened tube with a rounded nose (the warhead), narrowing to the engine.
  const struct { float z, r; } stations[] = {{-1.75f, 0.02f}, {-1.71f, 0.08f}, {-1.62f, 0.135f}, {-1.48f, 0.175f},
                                             {-1.25f, 0.2f},  {0.95f, 0.2f},   {1.2f, 0.19f},    {1.38f, 0.165f},
                                             {1.52f, 0.13f},  {1.6f, 0.08f}};
  const int n = 12;
  std::vector<std::vector<glm::vec3>> rings;
  for (const auto& s : stations) {
    std::vector<glm::vec3> ring;
    for (int i = 0; i < n; ++i) {
      float a = (i + 0.5f) * 6.2831853f / n;
      ring.push_back({s.r * std::cos(a), 0.85f * s.r * std::sin(a), s.z});
    }
    rings.push_back(ring);
  }
  mb.loft(m, rings, [&](int, int, glm::vec3) { return black; });

  // Wing: one cropped delta through the fuselage; elevons along the outer trailing edge.
  mb.slab(m, {{-1.25f, -0.02f, 0.95f}, {-0.2f, -0.02f, -0.95f}, {0.2f, -0.02f, -0.95f}, {1.25f, -0.02f, 0.95f},
              {1.25f, -0.02f, 1.32f}, {-1.25f, -0.02f, 1.32f}},
          {0.0f, 0.07f, 0.0f}, black);
  mb.slab(m, {{-1.0f, -0.02f, 1.32f}, {-0.35f, -0.02f, 1.32f}, {-0.35f, -0.02f, 1.44f}, {-1.0f, -0.02f, 1.44f}},
          {0.0f, 0.04f, 0.0f}, dark);
  mb.slab(m, {{0.35f, -0.02f, 1.32f}, {1.0f, -0.02f, 1.32f}, {1.0f, -0.02f, 1.44f}, {0.35f, -0.02f, 1.44f}},
          {0.0f, 0.04f, 0.0f}, dark);

  // Fins at the wing tips, mostly above the wing.
  for (float x : {-1.27f, 1.27f})
    mb.slab(m, {{x, -0.16f, 0.98f}, {x, -0.16f, 1.34f}, {x, 0.36f, 1.42f}, {x, 0.36f, 1.2f}}, {0.04f, 0.0f, 0.0f},
            black);
}

}  // namespace

void buildShahed(MeshBuilder& mb, const glm::mat4& m) {
  const glm::vec3 black{0.17f, 0.17f, 0.18f}, dark{0.11f, 0.11f, 0.12f}, metal{0.38f, 0.37f, 0.35f},
      prop{0.07f, 0.07f, 0.07f};
  airframe(mb, m, black, dark);

  // Engine: the cylinder heads of the four-cylinder boxer stick out of the sides, exhausts below.
  for (float side : {-1.0f, 1.0f})
    for (float z : {1.22f, 1.40f}) {
      mb.cylinder(m, {side * 0.12f, 0.03f, z}, {side * 0.27f, 0.03f, z}, 0.06f, 0.055f, 8, metal);
      mb.cylinder(m, {side * 0.1f, -0.13f, z - 0.05f}, {side * 0.1f, -0.15f, z + 0.25f}, 0.018f, 0.018f, 6, dark);
    }

  // Pusher propeller: spinner and two blades, about 0.75 m across.
  const glm::vec3 hub{0.0f, 0.0f, 1.66f};
  mb.cylinder(m, {0.0f, 0.0f, 1.59f}, hub, 0.07f, 0.07f, 10, dark);
  mb.cylinder(m, hub, {0.0f, 0.0f, 1.8f}, 0.07f, 0.0f, 10, dark);
  const float a = 0.6f;
  const glm::vec3 dir{std::cos(a), std::sin(a), 0.0f}, side{-std::sin(a), std::cos(a), 0.0f};
  for (float s : {-1.0f, 1.0f}) {
    glm::vec3 root = hub + s * dir * 0.05f, tip = hub + s * dir * 0.38f;
    glm::vec3 wr = side * 0.04f + glm::vec3(0, 0, 0.012f) * s, wt = side * 0.022f;
    mb.quad(xf(m, root - wr), xf(m, tip - wt), xf(m, tip + wt), xf(m, root + wr), prop);
  }
}

void buildGeran3(MeshBuilder& mb, const glm::mat4& m) {
  // Blacker than the Shahed, matt.
  const glm::vec3 black{0.11f, 0.11f, 0.12f}, dark{0.07f, 0.07f, 0.08f}, steel{0.62f, 0.62f, 0.6f},
      ring{0.16f, 0.36f, 0.78f}, hole{0.03f, 0.03f, 0.03f};
  airframe(mb, m, black, dark);

  // The air intake on the back of the fuselage, ahead of the engine: a hump with its mouth
  // to the front.
  mb.box(m, {-0.12f, 0.12f, 0.55f}, {0.12f, 0.27f, 1.35f}, black);
  mb.quad(xf(m, {-0.1f, 0.14f, 0.549f}), xf(m, {0.1f, 0.14f, 0.549f}), xf(m, {0.1f, 0.25f, 0.549f}),
          xf(m, {-0.1f, 0.25f, 0.549f}), hole);
  // The turbojet's nozzle sticking out of the tail, a blue ring where it meets the fuselage.
  mb.cylinder(m, {0.0f, 0.0f, 1.45f}, {0.0f, 0.0f, 1.58f}, 0.15f, 0.15f, 12, ring);
  mb.cylinder(m, {0.0f, 0.0f, 1.58f}, {0.0f, 0.0f, 1.92f}, 0.15f, 0.125f, 12, steel);
  mb.cylinder(m, {0.0f, 0.0f, 1.92f}, {0.0f, 0.0f, 1.93f}, 0.125f, 0.0f, 12, hole);
}

void buildLitavr(MeshBuilder& mb, const glm::mat4& m, float propAngle) {
  const glm::vec3 grey{0.21f, 0.21f, 0.22f}, dark{0.11f, 0.11f, 0.12f}, red{0.45f, 0.10f, 0.09f},
      lens{0.04f, 0.05f, 0.08f};
  // Body: a box with bevelled edges, a blunt camera head in front, a cone to the tail.
  const struct { float z, w, h; } stations[] = {{-0.55f, 0.05f, 0.055f}, {-0.53f, 0.064f, 0.072f}, {-0.49f, 0.07f, 0.08f},
                                                {0.22f, 0.07f, 0.08f},   {0.32f, 0.055f, 0.062f},  {0.41f, 0.03f, 0.032f},
                                                {0.45f, 0.01f, 0.01f}};
  std::vector<std::vector<glm::vec3>> rings;
  for (const auto& s : stations)
    rings.push_back({{s.w, 0.55f * s.h, s.z},
                     {0.55f * s.w, s.h, s.z},
                     {-0.55f * s.w, s.h, s.z},
                     {-s.w, 0.55f * s.h, s.z},
                     {-s.w, -0.55f * s.h, s.z},
                     {-0.55f * s.w, -s.h, s.z},
                     {0.55f * s.w, -s.h, s.z},
                     {s.w, -0.55f * s.h, s.z}});
  mb.loft(m, rings, [&](int ring, int, glm::vec3) { return ring < 2 ? dark : grey; });
  for (float x : {-0.024f, 0.024f}) mb.cylinder(m, {x, -0.008f, -0.548f}, {x, -0.008f, -0.562f}, 0.021f, 0.018f, 8, lens);

  // Tail fins in a cross.
  for (int k = 0; k < 4; ++k) {
    const float a = k * 1.570796f;
    const glm::vec3 d{std::cos(a), std::sin(a), 0.0f};
    const float span = k == 1 ? 0.17f : 0.13f;  // the one on top the tallest
    mb.slab(m, {d * 0.05f + glm::vec3(0, 0, 0.22f), d * span + glm::vec3(0, 0, 0.36f), d * span + glm::vec3(0, 0, 0.43f),
                d * 0.02f + glm::vec3(0, 0, 0.43f)},
            glm::vec3(-d.y, d.x, 0.0f) * 0.008f, grey);
  }

  // Arms in an X, a nacelle at the end of each, its propeller behind it.
  for (int k = 0; k < 4; ++k) {
    const float a = 0.785398f + k * 1.570796f;
    const glm::vec3 d{std::cos(a), std::sin(a), 0.0f}, n{-d.y, d.x, 0.0f};
    auto at = [&](float r, float z) { return d * r + glm::vec3(0, 0, z); };
    mb.slab(m, {at(0.06f, -0.02f), at(0.27f, 0.04f), at(0.27f, 0.15f), at(0.06f, 0.17f)}, n * 0.024f, grey);
    mb.slab(m, {at(0.075f, 0.06f), at(0.255f, 0.085f), at(0.255f, 0.105f), at(0.075f, 0.085f)}, n * 0.03f, red);
    const glm::vec3 c = d * 0.28f;
    mb.cylinder(m, c + glm::vec3(0, 0, -0.05f), c + glm::vec3(0, 0, 0.02f), 0.012f, 0.038f, 8, grey);
    mb.cylinder(m, c + glm::vec3(0, 0, 0.02f), c + glm::vec3(0, 0, 0.13f), 0.038f, 0.038f, 8, grey);
    mb.cylinder(m, c + glm::vec3(0, 0, 0.13f), c + glm::vec3(0, 0, 0.155f), 0.04f, 0.04f, 8, red);
    mb.cylinder(m, c + glm::vec3(0, 0, 0.155f), c + glm::vec3(0, 0, 0.2f), 0.038f, 0.014f, 8, grey);
    const glm::vec3 hub = c + glm::vec3(0, 0, 0.205f);
    const float b = propAngle + 0.9f * float(k);
    const glm::vec3 dir{std::cos(b), std::sin(b), 0.0f}, side{-std::sin(b), std::cos(b), 0.0f};
    for (float s : {-1.0f, 1.0f}) {
      const glm::vec3 root = hub + s * dir * 0.015f, tip = hub + s * dir * 0.13f;
      const glm::vec3 wr = side * 0.013f + glm::vec3(0, 0, 0.004f) * s, wt = side * 0.008f;
      mb.quad(xf(m, root - wr), xf(m, tip - wt), xf(m, tip + wt), xf(m, root + wr), dark);
    }
  }
}

void buildBanderol(MeshBuilder& mb, const glm::mat4& m) {
  const glm::vec3 grey{0.60f, 0.61f, 0.62f}, wing{0.55f, 0.56f, 0.57f}, red{0.62f, 0.12f, 0.10f},
      brass{0.72f, 0.56f, 0.30f}, hole{0.04f, 0.04f, 0.04f}, soot{0.16f, 0.15f, 0.15f};
  // Body stations from the nose tip (z = -2.5): half width, top and bottom.
  const struct { float d, w, top, bot; } stations[] = {
      {0.0f, 0.02f, -0.10f, -0.13f}, {0.12f, 0.08f, -0.05f, -0.17f}, {0.35f, 0.15f, 0.05f, -0.20f},
      {0.65f, 0.21f, 0.13f, -0.215f}, {0.95f, 0.25f, 0.19f, -0.22f}, {1.3f, 0.27f, 0.225f, -0.22f},
      {1.6f, 0.275f, 0.235f, -0.22f}, {4.3f, 0.275f, 0.235f, -0.22f}, {4.5f, 0.25f, 0.215f, -0.20f},
      {4.66f, 0.225f, 0.195f, -0.18f}, {4.68f, 0.222f, 0.192f, -0.178f}, {4.85f, 0.19f, 0.17f, -0.16f},
      {5.0f, 0.165f, 0.155f, -0.15f}};
  std::vector<std::vector<glm::vec3>> rings;
  for (const auto& s : stations) {
    const float z = s.d - 2.5f, side = s.bot + 0.5f * (s.top - s.bot), up = s.top - side, down = side - s.bot;
    std::vector<glm::vec3> ring;
    for (int k = 0; k <= 6; ++k) {  // the rounded top, from the right side over to the left
      const float a = k * 3.1415927f / 6.0f;
      ring.push_back({s.w * std::cos(a), side + up * std::sin(a), z});
    }
    ring.push_back({-s.w, side - 0.55f * down, z});  // the bevelled bottom
    ring.push_back({-0.6f * s.w, s.bot, z});
    ring.push_back({0.6f * s.w, s.bot, z});
    ring.push_back({s.w, side - 0.55f * down, z});
    rings.push_back(ring);
  }
  mb.loft(m, rings, [&](int ring, int, glm::vec3) { return ring == 9 ? red : grey; }, true, false);
  // The nozzle: a dark ring and the turbine deep in it.
  const float tail = 2.5f;
  mb.cylinder(m, {0.0f, 0.0f, tail - 0.02f}, {0.0f, 0.0f, tail + 0.02f}, 0.15f, 0.15f, 14, soot);
  mb.cylinder(m, {0.0f, 0.0f, tail + 0.02f}, {0.0f, 0.0f, tail - 0.08f}, 0.13f, 0.11f, 14, soot, hole);

  // Brass suspension lugs on the back.
  for (float z : {-1.15f, 0.0f}) mb.cylinder(m, {0.0f, 0.23f, z}, {0.0f, 0.27f, z}, 0.035f, 0.03f, 8, brass);

  // The wings' pivot under the body, the wings on it swept back.
  const float pivotZ = -0.5f, wingY = -0.27f;
  mb.cylinder(m, {0.0f, -0.21f, pivotZ}, {0.0f, wingY + 0.01f, pivotZ}, 0.15f, 0.14f, 14, grey);
  const float sweep = 0.785398f, half = 1.1f, chord = 0.11f;
  for (float x : {-1.0f, 1.0f}) {
    const glm::vec3 dir{x * std::cos(sweep), 0.0f, std::sin(sweep)}, across{-x * std::sin(sweep), 0.0f, std::cos(sweep)};
    const glm::vec3 base{0.0f, wingY - (x > 0 ? 0.0f : 0.03f), pivotZ};  // one wing over the other
    auto at = [&](float r, float c) { return base + dir * r + across * c; };
    mb.slab(m, {at(0.0f, -chord), at(half - 0.06f, -chord), at(half, -0.5f * chord), at(half, 0.5f * chord),
                at(half - 0.06f, chord), at(0.0f, chord)},
            {0.0f, 0.025f, 0.0f}, wing);
  }

  // The intake: a shallow scoop under the body behind the pivot, its mouth to the front.
  const glm::vec3 scoop[8] = {xf(m, {-0.13f, -0.215f, -0.25f}), xf(m, {0.13f, -0.215f, -0.25f}), xf(m, {0.13f, -0.30f, -0.25f}),
                              xf(m, {-0.13f, -0.30f, -0.25f}),  xf(m, {-0.11f, -0.215f, 0.55f}), xf(m, {0.11f, -0.215f, 0.55f}),
                              xf(m, {0.11f, -0.23f, 0.55f}),    xf(m, {-0.11f, -0.23f, 0.55f})};
  mb.hexa(scoop, grey);
  mb.quad(xf(m, {-0.11f, -0.225f, -0.251f}), xf(m, {0.11f, -0.225f, -0.251f}), xf(m, {0.11f, -0.29f, -0.251f}),
          xf(m, {-0.11f, -0.29f, -0.251f}), hole);

  // Tail fins: two level ones nearly as wide as the wings, one on top.
  for (float x : {-1.0f, 1.0f})
    mb.slab(m, {{x * 0.26f, 0.0f, 1.56f}, {x * 0.76f, 0.0f, 1.62f}, {x * 0.76f, 0.0f, 1.74f}, {x * 0.26f, 0.0f, 1.80f}},
            {0.0f, 0.022f, 0.0f}, grey);
  mb.slab(m, {{0.0f, 0.2f, 1.56f}, {0.0f, 0.52f, 1.66f}, {0.0f, 0.52f, 1.76f}, {0.0f, 0.2f, 1.80f}}, {0.022f, 0.0f, 0.0f},
          grey);
}

void buildR60(MeshBuilder& mb, const glm::mat4& m) {
  const glm::vec3 white{0.84f, 0.84f, 0.80f}, fin{0.80f, 0.80f, 0.76f}, glass{0.70f, 0.74f, 0.76f},
      orange{0.76f, 0.34f, 0.13f}, band{0.20f, 0.20f, 0.20f}, nozzle{0.10f, 0.10f, 0.10f}, steel{0.45f, 0.46f, 0.47f};
  const float h = kR60Half, r = 0.0635f;
  // Body stations from the nose: the dome, the orange cone round it, the tube, the tail.
  const struct { float z, r; glm::vec3 c; } stations[] = {
      {-h, 0.004f, glass},           {-h + 0.006f, 0.024f, glass}, {-h + 0.018f, 0.036f, glass},
      {-h + 0.035f, 0.043f, orange}, {-h + 0.125f, 0.0615f, white}, {-h + 0.135f, r, white},
      {-0.33f, r, band},             {-0.31f, r, white},           {h - 0.08f, r, steel},
      {h - 0.03f, 0.056f, nozzle},   {h, 0.046f, nozzle}};
  const int n = 14;
  std::vector<std::vector<glm::vec3>> rings;
  for (const auto& s : stations) {
    std::vector<glm::vec3> ring;
    for (int i = 0; i < n; ++i) {
      const float a = (i + 0.5f) * 6.2831853f / n;
      ring.push_back({s.r * std::cos(a), s.r * std::sin(a), s.z});
    }
    rings.push_back(ring);
  }
  mb.loft(m, rings, [&](int ring, int, glm::vec3) { return stations[size_t(ring)].c; });
  mb.cylinder(m, {0, 0, h - 0.002f}, {0, 0, h + 0.004f}, 0.03f, 0.03f, 10, glm::vec3(0.03f));  // the nozzle's mouth

  for (int k = 0; k < 4; ++k) {
    const float a = 0.785398f + k * 1.570796f;
    const glm::vec3 d{std::cos(a), std::sin(a), 0.0f}, nrm{-d.y, d.x, 0.0f};
    auto at = [&](float out, float z) { return d * out + glm::vec3(0, 0, z); };
    // The destabilisers: small pointed plates just behind the orange ring.
    mb.slab(m, {at(r - 0.004f, -0.905f), at(r + 0.032f, -0.865f), at(r - 0.004f, -0.855f)}, nrm * 0.004f, fin);
    // The canards: swept in front, square behind.
    mb.slab(m, {at(r - 0.004f, -0.83f), at(r + 0.085f, -0.74f), at(r + 0.085f, -0.66f), at(r - 0.004f, -0.63f)}, nrm * 0.006f, fin);
    // The wings: a long swept leading edge, a short tip, the trailing edge a little forward.
    const float tip = 0.195f;
    mb.slab(m, {at(r - 0.004f, 0.30f), at(tip, 0.82f), at(tip, 0.90f), at(tip - 0.03f, 0.955f), at(r - 0.004f, 0.985f)}, nrm * 0.008f, fin);
    // The rolleron: a wheel let into the wing's trailing tip.
    const glm::vec3 c = at(tip - 0.028f, 0.925f);
    mb.cylinder(m, c - nrm * 0.006f, c + nrm * 0.006f, 0.026f, 0.026f, 10, steel);
  }
}

void buildShturm(MeshBuilder& mb, const glm::mat4& m, float smear) {
  const glm::vec3 olive{0.27f, 0.33f, 0.17f}, joint{0.14f, 0.17f, 0.09f}, silver{0.70f, 0.71f, 0.69f},
      steel{0.38f, 0.40f, 0.38f}, well{0.07f, 0.08f, 0.06f}, fin{0.13f, 0.14f, 0.13f}, black{0.05f, 0.05f, 0.05f};
  const float L = 2.0f * kShturmHalf, r = 0.065f, rt = 0.056f;
  const auto z = [&](float f) { return -kShturmHalf + f * L; };  // along it from the nose (0..1)
  // Body stations from the nose; the tail's facets by the fins are their wells.
  const struct { float f, r; glm::vec3 c; } stations[] = {
      {0.0f, 0.004f, olive},  {0.006f, 0.02f, olive}, {0.025f, 0.036f, olive}, {0.055f, 0.048f, olive},
      {0.095f, 0.057f, olive}, {0.137f, 0.0635f, silver}, {0.18f, 0.0645f, olive}, {0.30f, r, joint},
      {0.303f, r, olive},     {0.47f, r, joint},      {0.474f, r, olive},     {0.83f, r, steel},
      {0.86f, 0.064f, olive}, {0.868f, rt, olive},    {0.975f, rt, olive},   {0.99f, 0.046f, olive},
      {1.0f, 0.03f, black}};
  const int n = 16;
  std::vector<std::vector<glm::vec3>> rings;
  for (const auto& s : stations) {
    std::vector<glm::vec3> ring;
    for (int i = 0; i < n; ++i) {
      const float a = (i + 0.5f) * 6.2831853f / n;
      ring.push_back({s.r * std::cos(a), s.r * std::sin(a), z(s.f)});
    }
    rings.push_back(ring);
  }
  mb.loft(m, rings, [&](int ring, int seg, glm::vec3) {
    // (the wells: the facet each fin springs from, on the tail)
    if (ring == 13 && seg % 4 == 1) return well;
    return stations[size_t(ring)].c;
  });
  mb.cylinder(m, {0, 0, z(1.0f) - 0.001f}, {0, 0, z(1.0f) + 0.004f}, 0.016f, 0.016f, 8, glm::vec3(0.55f, 0.15f, 0.08f));  // the tracer
  // The fitting on top, and a red mark beside it.
  mb.cylinder(m, {0, r + 0.004f, z(0.33f)}, {0, r + 0.004f, z(0.40f)}, 0.007f, 0.007f, 6, black);
  mb.cylinder(m, {0, r - 0.002f, z(0.33f)}, {0, r + 0.012f, z(0.33f)}, 0.01f, 0.01f, 6, black);
  mb.quad(glm::vec3(m * glm::vec4(0.03f, 0.058f, z(0.27f), 1)), glm::vec3(m * glm::vec4(0.045f, 0.047f, z(0.27f), 1)),
          glm::vec3(m * glm::vec4(0.045f, 0.047f, z(0.29f), 1)), glm::vec3(m * glm::vec4(0.03f, 0.058f, z(0.29f), 1)), glm::vec3(0.6f, 0.12f, 0.08f));

  // The wrap-around fins: each a blade curved round the tail, its root on the edge of its well,
  // out to 0.18 m; the trailing edge with a notch towards the tip.
  const int spans = 5, copies = smear > 0.0f ? 3 : 1;
  for (int k = 0; k < 4 * copies; ++k) {
    const float a0 = 6.2831853f / n * (4 * (k % 4) + 1) + smear * float(k / 4) / float(copies);  // the well's edge
    const auto at = [&](float t, float zz) {
      const float a = a0 + 0.9f * t, rr = rt + (0.18f - rt) * t;
      return glm::vec3(m * glm::vec4(rr * std::cos(a), rr * std::sin(a), zz, 1.0f));
    };
    const auto lead = [&](float t) { return z(0.845f) + 0.035f * t; };
    const auto trail = [&](float t) {
      const float notch = t < 0.55f ? t / 0.55f : (1.0f - t) / 0.45f;
      return z(0.945f) - 0.012f * t - 0.09f * notch * notch;
    };
    for (int i = 0; i < spans; ++i) {
      const float t0 = float(i) / spans, t1 = float(i + 1) / spans;
      const glm::vec3 p0 = at(t0, lead(t0)), p1 = at(t1, lead(t1)), p2 = at(t1, trail(t1)), p3 = at(t0, trail(t0));
      mb.quad(p0, p1, p2, p3, fin);
      mb.quad(p3, p2, p1, p0, fin);
    }
  }
}

}  // namespace render
