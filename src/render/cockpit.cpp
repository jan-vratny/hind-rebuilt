#include "render/cockpit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "render/font.hpp"

namespace render {

namespace {

const glm::vec3 kPanel{0.33f, 0.55f, 0.66f};      // Soviet instrument blue
const glm::vec3 kPanelLight{0.42f, 0.67f, 0.79f};
const glm::vec3 kPanelEdge{0.22f, 0.38f, 0.47f};
const glm::vec3 kBezel{0.13f, 0.14f, 0.15f};
const glm::vec3 kFace{0.04f, 0.04f, 0.05f};
const glm::vec3 kWhite{0.92f, 0.92f, 0.88f};
const glm::vec3 kNeedle{0.95f, 0.95f, 0.9f};
const glm::vec3 kSky{0.35f, 0.58f, 0.85f};
const glm::vec3 kGround{0.48f, 0.32f, 0.16f};
const glm::vec3 kSymbol{1.0f, 0.62f, 0.1f};
const glm::vec3 kDisplay{0.05f, 0.08f, 0.06f};
const glm::vec3 kDisplayText{0.35f, 1.0f, 0.45f};
const glm::vec3 kAmberText{1.0f, 0.65f, 0.2f};
const glm::vec3 kLampOff{0.20f, 0.17f, 0.12f};
const glm::vec3 kLampRed{1.0f, 0.18f, 0.12f};
const glm::vec3 kLampGreen{0.25f, 1.0f, 0.3f};
const glm::vec3 kLampAmber{1.0f, 0.7f, 0.15f};
const glm::vec3 kMetal{0.30f, 0.31f, 0.32f};
const glm::vec3 kDark{0.10f, 0.11f, 0.11f};
const glm::vec3 kMap{0.66f, 0.58f, 0.40f};
const glm::vec3 kMapLine{0.25f, 0.35f, 0.65f};

constexpr float kPi = 3.14159265f;

// A flat plate (panel, console, display) facing the crew member. Points on it are given in
// metres along its right (u) and up (v) directions; `lift` raises them off the surface so
// dials sit in front of the panel and needles in front of the dials.
struct Plate {
  MeshBuilder& mb;
  glm::vec3 o, u, v, n;

  // yawDeg turns the plate about the vertical (positive: its face turns to the right),
  // tiltDeg leans its top away from the viewer.
  Plate(MeshBuilder& m, glm::vec3 origin, float yawDeg, float tiltDeg) : mb(m), o(origin) {
    float y = glm::radians(yawDeg), t = glm::radians(tiltDeg);
    u = {std::cos(y), 0.0f, -std::sin(y)};
    glm::vec3 face = glm::cross(u, glm::vec3(0, 1, 0));
    v = glm::vec3(0, 1, 0) * std::cos(t) - face * std::sin(t);
    n = glm::cross(u, v);
  }
  glm::vec3 at(glm::vec2 p, float lift) const { return o + u * p.x + v * p.y + n * lift; }

  void rect(glm::vec2 a, glm::vec2 b, float lift, glm::vec3 col) const {
    mb.quad(at({a.x, a.y}, lift), at({b.x, a.y}, lift), at({b.x, b.y}, lift), at({a.x, b.y}, lift), col);
  }
  void frame(glm::vec2 a, glm::vec2 b, float w, float lift, glm::vec3 col) const {
    rect({a.x, a.y}, {b.x, a.y + w}, lift, col);
    rect({a.x, b.y - w}, {b.x, b.y}, lift, col);
    rect({a.x, a.y}, {a.x + w, b.y}, lift, col);
    rect({b.x - w, a.y}, {b.x, b.y}, lift, col);
  }
  void poly(const std::vector<glm::vec2>& pts, float lift, glm::vec3 col) const {
    for (size_t i = 1; i + 1 < pts.size(); ++i) mb.tri(at(pts[0], lift), at(pts[i], lift), at(pts[i + 1], lift), col);
  }
  void disc(glm::vec2 c, float r, float lift, glm::vec3 col, int segs = 24) const {
    std::vector<glm::vec2> pts;
    for (int i = 0; i < segs; ++i) pts.push_back(c + r * dir(2.0f * kPi * i / segs));
    poly(pts, lift, col);
  }
  void segment(glm::vec2 a, glm::vec2 b, float w, float lift, glm::vec3 col) const {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-6f) return;
    glm::vec2 s = glm::vec2(-d.y, d.x) / len * (w * 0.5f);
    a -= d / len * (w * 0.5f);
    b += d / len * (w * 0.5f);
    mb.quad(at(a - s, lift), at(b - s, lift), at(b + s, lift), at(a + s, lift), col);
  }
  // Clockwise angle from 12 o'clock, as on a dial.
  static glm::vec2 dir(float a) { return {std::sin(a), std::cos(a)}; }
  void ticks(glm::vec2 c, float r0, float r1, float a0, float a1, int count, float w, float lift, glm::vec3 col) const {
    for (int i = 0; i < count; ++i) {
      float a = count > 1 ? a0 + (a1 - a0) * i / (count - 1) : a0;
      segment(c + r0 * dir(a), c + r1 * dir(a), w, lift, col);
    }
  }
  void needle(glm::vec2 c, float a, float len, float w, float lift, glm::vec3 col) const {
    glm::vec2 d = dir(a), s{d.y * w * 0.5f, -d.x * w * 0.5f};
    poly({c - d * len * 0.18f + s, c + d * len, c - d * len * 0.18f - s}, lift, col);
  }
  // align: -1 left, 0 centre, +1 right; pos is the baseline start.
  void text(const std::string& str, glm::vec2 pos, float size, float lift, glm::vec3 col, int align = -1) const {
    float unit = size / float(kGlyphH), adv = unit * (kGlyphW + 1);
    float width = str.empty() ? 0.0f : adv * float(str.size()) - unit;
    if (align == 0) pos.x -= width * 0.5f;
    else if (align > 0) pos.x -= width;
    for (char ch : str) {
      const GlyphRows& rows = glyphRows(ch);
      for (int r = 0; r < kGlyphH; ++r) {
        for (int c = 0; c < kGlyphW; ++c) {
          if (!(rows[size_t(r)] >> (kGlyphW - 1 - c) & 1)) continue;
          glm::vec2 p = pos + glm::vec2(c, kGlyphH - 1 - r) * unit;
          rect(p, p + glm::vec2(unit), lift, col);
        }
      }
      pos.x += adv;
    }
  }
  // Flat ring between radii r0 and r1, the part from angle a0 to a1 (clockwise from 12).
  void ring(glm::vec2 c, float r0, float r1, float a0, float a1, int segs, float lift, glm::vec3 col) const {
    for (int i = 0; i < segs; ++i) {
      float t0 = a0 + (a1 - a0) * i / segs, t1 = a0 + (a1 - a0) * (i + 1) / segs;
      mb.quad(at(c + r0 * dir(t0), lift), at(c + r1 * dir(t0), lift), at(c + r1 * dir(t1), lift),
              at(c + r0 * dir(t1), lift), col);
    }
  }
  // Circle cut by the line through c + h * nrm, square to nrm: the part on the nrm side.
  void halfDisc(glm::vec2 c, float r, glm::vec2 nrm, float h, bool positive, float lift, glm::vec3 col) const {
    std::vector<glm::vec2> in, out;
    for (int i = 0; i < 32; ++i) in.push_back(c + r * dir(2.0f * kPi * i / 32));
    auto side = [&](glm::vec2 p) { return (glm::dot(p - c, nrm) - h) * (positive ? 1.0f : -1.0f); };
    for (size_t i = 0; i < in.size(); ++i) {
      glm::vec2 a = in[i], b = in[(i + 1) % in.size()];
      float sa = side(a), sb = side(b);
      if (sa >= 0) out.push_back(a);
      if ((sa >= 0) != (sb >= 0)) out.push_back(a + (b - a) * (sa / (sa - sb)));
    }
    if (out.size() >= 3) poly(out, lift, col);
  }
};

// ---- dials

void dial(const Plate& p, glm::vec2 c, float r) {
  p.disc(c, r * 1.16f, 0.002f, kBezel);
  p.disc(c, r, 0.003f, kFace);
}

// A needle dial with ticks over the sweep a0..a1 (degrees clockwise from 12) and value 0..1.
void sweepDial(const Plate& p, glm::vec2 c, float r, float a0, float a1, int major, int minor, float value,
               std::initializer_list<const char*> labels = {}) {
  dial(p, c, r);
  float A0 = glm::radians(a0), A1 = glm::radians(a1);
  p.ticks(c, r * 0.78f, r * 0.96f, A0, A1, major, r * 0.06f, 0.004f, kWhite);
  if (minor > 0) p.ticks(c, r * 0.87f, r * 0.96f, A0, A1, (major - 1) * minor + 1, r * 0.03f, 0.004f, kWhite);
  int i = 0, n = int(labels.size());
  for (const char* l : labels) {
    float a = n > 1 ? A0 + (A1 - A0) * i++ / (n - 1) : A0;
    glm::vec2 q = c + r * 0.58f * Plate::dir(a) - glm::vec2(0, r * 0.1f);
    p.text(l, q, r * 0.2f, 0.004f, kWhite, 0);
  }
  float a = A0 + (A1 - A0) * std::clamp(value, 0.0f, 1.0f);
  p.needle(c, a, r * 0.9f, r * 0.12f, 0.006f, kNeedle);
  p.disc(c, r * 0.08f, 0.007f, kBezel, 10);
}

void attitude(const Plate& p, glm::vec2 c, float r, float pitchDeg, float rollDeg) {
  dial(p, c, r);
  float roll = glm::radians(rollDeg);
  glm::vec2 nrm{-std::sin(roll), std::cos(roll)};
  float h = -std::clamp(pitchDeg, -40.0f, 40.0f) / 40.0f * r * 0.85f;
  p.halfDisc(c, r * 0.97f, nrm, h, true, 0.004f, kSky);
  p.halfDisc(c, r * 0.97f, nrm, h, false, 0.004f, kGround);
  glm::vec2 along{nrm.y, -nrm.x}, mid = c + nrm * h;
  p.segment(mid - along * r * 0.95f, mid + along * r * 0.95f, r * 0.03f, 0.005f, kWhite);
  for (float k : {-20.0f, -10.0f, 10.0f, 20.0f}) {  // pitch ladder
    glm::vec2 m = mid + nrm * (k / 40.0f * r * 0.85f);
    if (glm::length(m - c) < r * 0.8f) p.segment(m - along * r * 0.18f, m + along * r * 0.18f, r * 0.02f, 0.005f, kWhite);
  }
  // Fixed aircraft symbol and bank scale.
  p.segment(c + glm::vec2(-r * 0.55f, 0), c + glm::vec2(-r * 0.18f, 0), r * 0.06f, 0.007f, kSymbol);
  p.segment(c + glm::vec2(r * 0.18f, 0), c + glm::vec2(r * 0.55f, 0), r * 0.06f, 0.007f, kSymbol);
  p.disc(c, r * 0.05f, 0.007f, kSymbol, 8);
  p.ticks(c, r * 0.88f, r * 1.0f, glm::radians(-60.0f), glm::radians(60.0f), 5, r * 0.03f, 0.006f, kWhite);
}

void compassCard(const Plate& p, glm::vec2 c, float r, float headingDeg) {
  dial(p, c, r);
  float h = glm::radians(headingDeg);
  for (int deg = 0; deg < 360; deg += 10) {
    float a = glm::radians(float(deg)) - h;
    float r0 = deg % 30 == 0 ? r * 0.78f : r * 0.86f;
    p.segment(c + r0 * Plate::dir(a), c + r * 0.95f * Plate::dir(a), r * 0.035f, 0.004f, kWhite);
  }
  const char* names[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    glm::vec2 q = c + r * 0.55f * Plate::dir(glm::radians(90.0f * i) - h) - glm::vec2(0, r * 0.1f);
    p.text(names[i], q, r * 0.22f, 0.005f, i == 0 ? kSymbol : kWhite, 0);
  }
  p.poly({c + glm::vec2(0, r * 1.02f), c + glm::vec2(-r * 0.08f, r * 1.16f), c + glm::vec2(r * 0.08f, r * 1.16f)}, 0.006f,
         kSymbol);  // lubber mark
  p.segment(c - glm::vec2(0, r * 0.4f), c + glm::vec2(0, r * 0.4f), r * 0.05f, 0.006f, kSymbol);
  p.segment(c + glm::vec2(-r * 0.25f, r * 0.05f), c + glm::vec2(r * 0.25f, r * 0.05f), r * 0.05f, 0.006f, kSymbol);
}

void altimeter(const Plate& p, glm::vec2 c, float r, float alt) {
  dial(p, c, r);
  p.ticks(c, r * 0.78f, r * 0.96f, 0.0f, 2.0f * kPi * 0.9f, 10, r * 0.06f, 0.004f, kWhite);
  p.ticks(c, r * 0.88f, r * 0.96f, 0.0f, 2.0f * kPi * 0.98f, 50, r * 0.025f, 0.004f, kWhite);
  for (int i = 0; i < 10; ++i) {
    glm::vec2 q = c + r * 0.58f * Plate::dir(2.0f * kPi * i / 10) - glm::vec2(0, r * 0.1f);
    p.text(std::to_string(i), q, r * 0.2f, 0.004f, kWhite, 0);
  }
  float a = std::max(alt, 0.0f);
  p.needle(c, 2.0f * kPi * a / 10000.0f, r * 0.55f, r * 0.2f, 0.006f, kNeedle);  // 10 000 m per turn
  p.needle(c, 2.0f * kPi * a / 1000.0f, r * 0.9f, r * 0.12f, 0.007f, kNeedle);   // 1000 m per turn
  p.disc(c, r * 0.08f, 0.008f, kBezel, 10);
}

void clockDial(const Plate& p, glm::vec2 c, float r, float seconds) {
  dial(p, c, r);
  p.ticks(c, r * 0.8f, r * 0.96f, 0.0f, 2.0f * kPi * 11 / 12, 12, r * 0.06f, 0.004f, kWhite);
  float m = std::fmod(seconds / 60.0f, 60.0f), hr = std::fmod(seconds / 3600.0f, 12.0f);
  p.needle(c, 2.0f * kPi * hr / 12.0f, r * 0.55f, r * 0.14f, 0.006f, kNeedle);
  p.needle(c, 2.0f * kPi * m / 60.0f, r * 0.88f, r * 0.1f, 0.007f, kNeedle);
  p.needle(c, 2.0f * kPi * std::fmod(seconds, 60.0f) / 60.0f, r * 0.9f, r * 0.04f, 0.008f, kSymbol);
}

float radarScale(float h) {  // RV-5: non-linear, the first 100 m take half the scale
  if (h < 0.0f) return 1.0f;
  return h < 100.0f ? 0.5f * h / 100.0f : std::min(0.5f + 0.5f * (h - 100.0f) / 600.0f, 1.0f);
}

std::string fmt3(float v) {
  char b[16];
  std::snprintf(b, sizeof b, "%03d", int(std::lround(v)));
  return b;
}

// Display window with lines of text.
void display(const Plate& p, glm::vec2 a, glm::vec2 b, std::initializer_list<std::string> lines, float size,
             glm::vec3 col) {
  p.rect(a, b, 0.002f, kDisplay);
  p.frame(a - glm::vec2(0.004f), b + glm::vec2(0.004f), 0.004f, 0.0025f, kBezel);
  float y = b.y - size * 1.4f;
  for (const std::string& l : lines) {
    p.text(l, {(a.x + b.x) * 0.5f, y}, size, 0.003f, col, 0);
    y -= size * 1.6f;
  }
}

// Plain box in body coordinates (axis aligned).
void box(MeshBuilder& mb, glm::vec3 lo, glm::vec3 hi, glm::vec3 col) { mb.box(glm::mat4(1.0f), lo, hi, col); }

}  // namespace

void buildPilotCockpit(MeshBuilder& mb, const InstrumentData& d) {
  // Main panel 0.55 m ahead of the eyes, as wide as the cockpit, top leaning away by 12 degrees.
  Plate p(mb, {0.0f, -0.10f, -3.90f}, 0.0f, 12.0f);
  p.rect({-0.48f, -0.24f}, {0.48f, 0.20f}, 0.0f, kPanel);
  p.rect({-0.29f, -0.22f}, {0.255f, 0.13f}, 0.001f, kPanelLight);
  p.frame({-0.29f, -0.22f}, {0.255f, 0.13f}, 0.004f, 0.0015f, kPanelEdge);
  p.rect({0.265f, -0.22f}, {0.47f, 0.13f}, 0.001f, kPanelLight);
  p.rect({-0.47f, -0.22f}, {-0.30f, 0.17f}, 0.001f, kPanelLight);

  // Text display under the HUD collimator.
  std::string text = d.text.empty() ? "MI-24V" : d.text.substr(d.text.rfind('\n') + 1);  // the latest line
  display(p, {-0.11f, 0.14f}, {0.11f, 0.19f}, {text}, 0.017f, kDisplayText);

  // Upper row: g meter, airspeed, artificial horizon, vertical speed, clock.
  const float up = 0.055f, low = -0.09f, r = 0.046f;
  const float col[5] = {-0.24f, -0.13f, -0.015f, 0.10f, 0.205f};
  sweepDial(p, {col[0], up}, r * 0.9f, -135, 135, 7, 2, (d.gLoad + 2.0f) / 6.0f, {"-2", "0", "2", "4"});
  sweepDial(p, {col[1], up}, r, -150, 150, 10, 5, d.speedKmh / 450.0f, {"0", "1", "2", "3", "4"});
  attitude(p, {col[2], up}, r * 1.15f, d.pitchDeg, d.rollDeg);
  {  // Vertical speed (x10 m/min): zero at 9 o'clock, climb over the top, descent below.
    glm::vec2 c{col[3], up};
    dial(p, c, r);
    p.ticks(c, r * 0.78f, r * 0.96f, glm::radians(-270.0f), glm::radians(90.0f), 7, r * 0.06f, 0.004f, kWhite);
    for (int i = 0; i < 4; ++i) {
      glm::vec2 q = c + r * 0.58f * Plate::dir(glm::radians(-90.0f + 60.0f * i)) - glm::vec2(0, r * 0.1f);
      p.text(std::to_string(i), q, r * 0.2f, 0.004f, kWhite, 0);
    }
    float a = glm::radians(-90.0f + std::clamp(d.verticalSpeed, -30.0f, 30.0f) / 30.0f * 180.0f);
    p.needle(c, a, r * 0.9f, r * 0.12f, 0.008f, kNeedle);
  }
  clockDial(p, {col[4], up}, r * 0.9f, d.clockSeconds);

  // Lower row: rotor torque, radar altimeter, barometric altimeter, HSI, standby horizon.
  sweepDial(p, {col[0], low}, r * 0.9f, -135, 135, 7, 2, d.torque / 130.0f, {"0", "", "", "", "", "", "13"});
  sweepDial(p, {col[1], low}, r, -150, 150, 5, 2, radarScale(d.radarAltitude), {"0", "50", "100", "", "700"});
  if (d.radarAltitude < 0.0f) p.rect({col[1] - 0.015f, low - 0.03f}, {col[1] + 0.015f, low - 0.015f}, 0.009f, kLampRed);
  altimeter(p, {col[2], low}, r * 1.1f, d.baroAltitude);
  compassCard(p, {col[3], low}, r * 1.05f, d.headingDeg);
  attitude(p, {col[4], low}, r * 0.8f, d.pitchDeg, d.rollDeg);

  // Left: warning lights, gear and brake lamps, fuel.
  const char* warnings[] = {"FIRE", "OIL", "HYD", "GEN", "TQ", "RPM", "ICE", "FUEL"};
  for (int i = 0; i < 8; ++i) {
    glm::vec2 a{-0.46f + (i % 2) * 0.08f, 0.13f - (i / 2) * 0.037f};
    bool lit = (i == 4 && d.torque > 100.5f);
    p.rect(a, a + glm::vec2(0.072f, 0.028f), 0.003f, lit ? kLampRed : kLampOff);
    p.text(warnings[i], a + glm::vec2(0.036f, 0.009f), 0.011f, 0.004f, lit ? kWhite : glm::vec3(0.5f), 0);
  }
  glm::vec3 gear = d.gearMoving ? kLampRed : (d.gearDown ? kLampGreen : kLampOff);
  for (int i = 0; i < 3; ++i) {
    glm::vec2 c{-0.43f + i * 0.04f, i == 1 ? -0.035f : -0.055f};
    p.disc(c, 0.015f, 0.003f, kBezel, 12);
    p.disc(c, 0.011f, 0.004f, gear, 12);
  }
  p.text("GEAR", {-0.39f, -0.09f}, 0.012f, 0.004f, kWhite, 0);
  p.disc({-0.43f, -0.15f}, 0.015f, 0.003f, kBezel, 12);
  p.disc({-0.43f, -0.15f}, 0.011f, 0.004f, d.brakes ? kLampAmber : kLampOff, 12);
  p.text("BRK", {-0.43f, -0.185f}, 0.011f, 0.004f, kWhite, 0);
  p.rect({-0.36f, -0.20f}, {-0.33f, -0.12f}, 0.003f, kFace);  // fuel
  p.rect({-0.355f, -0.195f}, {-0.335f, -0.135f}, 0.004f, kLampAmber);
  p.text("FUEL", {-0.345f, -0.115f}, 0.01f, 0.004f, kWhite, 0);

  // Right: moving map, north up, 1 km grid, 6 km across.
  const glm::vec2 m0{0.28f, -0.20f}, m1{0.455f, 0.11f}, mc = (m0 + m1) * 0.5f;
  const float perKm = (m1.x - m0.x) / 6.0f;
  p.rect(m0, m1, 0.002f, kMap);
  p.frame(m0 - glm::vec2(0.006f), m1 + glm::vec2(0.006f), 0.006f, 0.0025f, kBezel);
  float kmx = d.position.x / 1000.0f, kmz = d.position.y / 1000.0f;
  for (float g = std::floor(kmx - 4.0f); g <= kmx + 4.0f; g += 1.0f) {
    float x = mc.x + (g - kmx) * perKm;
    if (x > m0.x && x < m1.x) p.segment({x, m0.y}, {x, m1.y}, 0.0012f, 0.003f, kMapLine);
  }
  for (float g = std::floor(kmz - 6.0f); g <= kmz + 6.0f; g += 1.0f) {
    float y = mc.y - (g - kmz) * perKm;  // north (-z) is up
    if (y > m0.y && y < m1.y) p.segment({m0.x, y}, {m1.x, y}, 0.0012f, 0.003f, kMapLine);
  }
  glm::vec2 home = mc + glm::vec2(d.home.x - d.position.x, -(d.home.y - d.position.y)) / 1000.0f * perKm;
  if (home.x > m0.x + 0.01f && home.x < m1.x - 0.01f && home.y > m0.y + 0.01f && home.y < m1.y - 0.01f) {
    p.segment(home - glm::vec2(0, 0.012f), home + glm::vec2(0, 0.012f), 0.004f, 0.004f, kDark);  // runway
    p.disc(home, 0.006f, 0.0045f, kLampRed, 8);
  }
  {
    float h = glm::radians(d.headingDeg);
    glm::vec2 f = Plate::dir(h), s{f.y, -f.x};
    p.poly({mc + f * 0.014f, mc - f * 0.008f + s * 0.008f, mc - f * 0.008f - s * 0.008f}, 0.005f, kSymbol);
  }

  // HUD collimator on top of the panel: housing and the frame of the glass.
  box(mb, {-0.20f, 0.095f, -3.99f}, {0.20f, 0.12f, -3.93f}, kMetal);
  box(mb, {-0.21f, 0.115f, -3.95f}, {-0.19f, 0.37f, -3.92f}, kDark);
  box(mb, {0.19f, 0.115f, -3.95f}, {0.21f, 0.37f, -3.92f}, kDark);
  box(mb, {-0.21f, 0.365f, -3.96f}, {0.21f, 0.375f, -3.93f}, kMetal);

  // Standby compass up left on the canopy frame, threat warning display up right.
  Plate sc(mb, {-0.31f, 0.41f, -3.90f}, 15.0f, -15.0f);
  sc.rect({-0.04f, -0.035f}, {0.04f, 0.035f}, 0.0f, kPanel);
  sc.rect({-0.028f, -0.02f}, {0.028f, 0.02f}, 0.001f, kFace);
  sc.text(fmt3(std::fmod(d.headingDeg + 360.0f, 360.0f)), {0.0f, -0.008f}, 0.016f, 0.002f, kWhite, 0);
  Plate rw(mb, {0.31f, 0.40f, -3.90f}, -15.0f, -15.0f);
  rw.rect({-0.05f, -0.06f}, {0.05f, 0.06f}, 0.0f, kBezel);
  rw.rect({-0.04f, -0.035f}, {0.04f, 0.05f}, 0.001f, kFace);
  rw.segment({0.0f, -0.02f}, {0.0f, 0.035f}, 0.004f, 0.002f, kLampAmber);   // aircraft silhouette
  rw.segment({-0.025f, 0.012f}, {0.025f, 0.012f}, 0.004f, 0.002f, kLampAmber);
  rw.segment({-0.01f, -0.018f}, {0.01f, -0.018f}, 0.004f, 0.002f, kLampAmber);
  for (int i = 0; i < 4; ++i) rw.disc({-0.03f + i * 0.02f, -0.05f}, 0.006f, 0.002f, kLampOff, 8);
}

void buildGunnerCockpit(MeshBuilder& mb, const InstrumentData& d) {
  // Canopy framing: the heavy round frame of the flat armoured windscreen with its bolts, the
  // sills and the rear arch of the canopy.
  const glm::vec3 kFrameGrey{0.20f, 0.21f, 0.22f}, kBolt{0.33f, 0.34f, 0.35f};
  Plate ws(mb, {0.0f, -0.50f, -5.40f}, 0.0f, 13.0f);
  ws.ring({0.0f, 0.0f}, 0.25f, 0.42f, 0.0f, 2.0f * kPi, 40, 0.0f, kFrameGrey);
  ws.ring({0.0f, 0.0f}, 0.25f, 0.27f, 0.0f, 2.0f * kPi, 40, 0.002f, kBezel);
  for (int i = 0; i < 16; ++i) ws.disc(0.34f * Plate::dir(2.0f * kPi * (i + 0.5f) / 16), 0.009f, 0.003f, kBolt, 8);
  for (float x : {-0.47f, 0.47f}) box(mb, {x - 0.03f, -0.60f, -5.30f}, {x + 0.03f, -0.555f, -4.20f}, kFrameGrey);
  Plate arch(mb, {0.0f, -0.55f, -4.22f}, 0.0f, 0.0f);
  arch.ring({0.0f, 0.0f}, 0.50f, 0.58f, glm::radians(-90.0f), glm::radians(90.0f), 20, 0.0f, kFrameGrey);

  // Sight unit in the middle: blue head with the sight window, eyepieces, column.
  box(mb, {-0.075f, -0.50f, -5.10f}, {0.075f, -0.40f, -5.00f}, kPanelLight);
  Plate sight(mb, {0.0f, -0.45f, -4.995f}, 0.0f, 0.0f);
  sight.rect({-0.04f, -0.02f}, {0.04f, 0.035f}, 0.0f, kFace);
  sight.frame({-0.045f, -0.025f}, {0.045f, 0.04f}, 0.006f, 0.0005f, kBezel);
  box(mb, {-0.095f, -0.61f, -5.06f}, {0.095f, -0.50f, -4.95f}, kDark);
  Plate eyes(mb, {0.0f, -0.555f, -4.948f}, 0.0f, 0.0f);
  for (float x : {-0.042f, 0.042f}) {
    eyes.disc({x, 0.0f}, 0.034f, 0.0f, kMetal, 16);
    eyes.disc({x, 0.0f}, 0.022f, 0.001f, kFace, 16);
  }
  box(mb, {-0.05f, -0.95f, -5.04f}, {0.05f, -0.61f, -4.97f}, kMetal);
  box(mb, {-0.06f, -0.56f, -5.10f}, {-0.02f, -0.50f, -5.06f}, kPanel);

  // Weapon management panels left and right.
  Plate left(mb, {-0.31f, -0.66f, -4.88f}, 30.0f, 35.0f);
  left.rect({-0.14f, -0.08f}, {0.14f, 0.08f}, 0.0f, kPanel);
  left.frame({-0.14f, -0.08f}, {0.14f, 0.08f}, 0.006f, 0.001f, kPanelEdge);
  display(left, {-0.03f, 0.02f}, {0.06f, 0.055f}, {"096"}, 0.018f, kAmberText);
  display(left, {-0.03f, -0.045f}, {0.06f, -0.01f}, {"096"}, 0.018f, kAmberText);
  left.text("CHAFF", {-0.12f, 0.03f}, 0.012f, 0.002f, kWhite);
  left.text("FLARE", {-0.12f, -0.035f}, 0.012f, 0.002f, kWhite);
  Plate right(mb, {0.31f, -0.66f, -4.88f}, -30.0f, 35.0f);
  right.rect({-0.14f, -0.08f}, {0.14f, 0.08f}, 0.0f, kPanel);
  right.frame({-0.14f, -0.08f}, {0.14f, 0.08f}, 0.006f, 0.001f, kPanelEdge);
  const char* loads[] = {"040", "040", "004"};
  for (int i = 0; i < 3; ++i) {
    glm::vec2 a{-0.125f + i * 0.085f, 0.025f};
    display(right, a, a + glm::vec2(0.07f, 0.035f), {loads[i]}, 0.016f, kAmberText);
    right.disc(a + glm::vec2(0.035f, 0.05f), 0.006f, 0.002f, i == 2 ? kLampGreen : kLampOff, 8);
  }
  display(right, {-0.125f, -0.065f}, {-0.045f, -0.025f}, {"1470"}, 0.016f, kAmberText);
  display(right, {-0.035f, -0.065f}, {0.125f, -0.025f}, {"9M114 SHTURM"}, 0.012f, kDisplayText);

  // Text display above the windscreen.
  Plate top(mb, {0.0f, -0.055f, -5.20f}, 0.0f, -25.0f);
  std::string text = d.text.empty() ? "MI-24V" : d.text.substr(d.text.rfind('\n') + 1);  // the latest line
  display(top, {-0.10f, -0.02f}, {0.10f, 0.02f}, {text}, 0.016f, kDisplayText);

  // Duplicate flight instruments down on the left (look left to read them).
  Plate inst(mb, {-0.40f, -0.50f, -4.62f}, 70.0f, 20.0f);
  inst.rect({-0.17f, -0.06f}, {0.17f, 0.06f}, 0.0f, kPanel);
  altimeter(inst, {-0.12f, 0.0f}, 0.035f, d.baroAltitude);
  sweepDial(inst, {-0.04f, 0.0f}, 0.035f, -170, 170, 7, 0, (std::clamp(d.verticalSpeed, -30.0f, 30.0f) + 30.0f) / 60.0f);
  sweepDial(inst, {0.04f, 0.0f}, 0.035f, -150, 150, 10, 0, d.speedKmh / 450.0f);
  sweepDial(inst, {0.12f, 0.0f}, 0.035f, -150, 150, 5, 0, radarScale(d.radarAltitude));
}

}  // namespace render
